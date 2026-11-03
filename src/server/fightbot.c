/*
 * XPilot TNG, a multiplayer space war game.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Fightbot: a robot driver ("fightbot") for deathmatch and dogfight maps
 * such as New Dark Hell or The Killing Field.
 *
 * Smart robots use this driver on maps without a team ball game and
 * without a race when smartBotType is "auto" or "fight".
 *
 *  - Perception: only what a human pilot gets.  Ships, shots, mines and
 *    items within a human's view (the largest view a client may have)
 *    around the ship, and only ships the server lets the bot see (cloak,
 *    sensors, limitedVisibility).  Enemy positions on the radar when
 *    playersOnRadar is on, at the radar's resolution, their speed from
 *    consecutive dots.  Everything is seen with the reaction delay of the
 *    skill level.
 *  - Hunting: the target is the closest known enemy, attackers first.  A
 *    Dijkstra field to the target over a navigation grid leads the way
 *    around walls.  Enemies nobody knows about are searched where they
 *    were last seen, then at bases and fuel stations.
 *  - Dogfight: sampling model predictive control on the server's physics,
 *    as in the race bot.  Candidate manoeuvres are simulated a little under
 *    two seconds ahead and scored on dodging shots (and virtual shots of
 *    enemies pointing at the bot), rams and walls, on pointing at the lead
 *    of the target early and on keeping the fighting distance.
 *  - Firing: whenever a shot of any gun would hit an enemy on its predicted
 *    course and the line is free; at the target also shots that cover the
 *    ways it can dodge.
 *  - Tactics: fight, fall back for a moment when outgunned or under heavy
 *    fire and attack again, refuel, fetch items the ship needs.  Items are
 *    used when they help.
 *  - Reprogrammed with an ECM (or at war after a kill) the bot goes for the
 *    new enemy only and searches him.
 *  - Learning: per opponent, for the session, which motion model predicts
 *    his flight best and how dangerous he is.  Per map, kept on disk, the
 *    fighting genes (distance, aggression, evasion, aim, spread, how long
 *    to fall back) adapt with an evolution strategy rewarded by kills and
 *    deaths, in $XDG_DATA_HOME/xpilot-tng/fightbot/.
 *
 * The skill level (fightRobotLevel) sets turn rate, reaction delay, aim
 * and steering errors, misjudged plans, overlooked shots and aggression.
 */

#include "xpserver.h"
#include <sys/stat.h>

#define FB_TYPE_NAME		"fight"

#define FB_SHIP_R		16.0	/* collision radius of a ship (px) */
#define FB_INF			1e30f
#define FB_H			18	/* rollout horizon (ticks) */
#define FB_NEAR_H		16	/* ticks shots and ships are predicted */
#define FB_AIM_H		8	/* ticks the aim is scored */
#define FB_MAX_CAND		128
#define FB_MAX_SHOTS		64
#define FB_MAX_ENEMIES		16
#define FB_HIST			8	/* ticks of position history */
#define FB_NFIELDS		12	/* cached distance fields to moving goals */
#define FB_PRED_T		8	/* opponent model: prediction horizon (ticks) */
#define FB_NMODELS		3	/* opponent model: acceleration factors */
#define FB_GUN_OFF		12.0	/* the gun sits this far ahead (px) */
#define FB_VIEW_HW		(MAX_VIEW_WIDTH / 2.0)
#define FB_VIEW_HH		(MAX_VIEW_HEIGHT / 2.0)
#define FB_MEMORY		(4 * 12)	/* ticks an unseen enemy is remembered */
#define FB_THREAT_TICKS		(5 * 12)
#define FB_BOUNCE_COST		60.0
#define FB_SHOT_COST		4e4	/* a hit by a seen shot */
#define FB_VSHOT_COST		1.5e3	/* a hit by a virtual shot */
/*
 * Skill below level 100 (see Fb_unskill(): 0 at level 100, 1 at level 1).
 */
#define FB_DELAY_MAX		3.5	/* reaction delay at level 1 (ticks) */
#define FB_PLAN_NOISE		500.0	/* noise on plan costs at level 1 */
#define FB_STEER_ERR		4.0	/* heading error at level 1 (dir units) */
#define FB_AIM_ERR		3.0	/* aiming error at level 1 (dir units) */
#define FB_POWER_LOSS		0.08	/* share of the thrust lost at level 1 */
#define FB_OVERLOOK		0.30	/* share of shots overlooked at level 1 */
#define FB_LEARN_SUBDIR		"xpilot-tng/fightbot"
#define FB_LEARN_VERSION	1
#define FB_SAVE_TICKS		(300 * 12)

#ifndef FB_DEBUG
#define FB_DEBUG 0
#endif
#define FB_LOG(...) do { if (FB_DEBUG) { printf("%6lu ", FbTick); printf(__VA_ARGS__); fflush(stdout); } } while (0)

enum {
    FB_HUNT,		/* known enemy, out of reach: fly there */
    FB_ENGAGE,		/* dogfight */
    FB_FALLBACK,	/* outgunned: gain room, then attack again */
    FB_REFUEL,
    FB_ITEM,
    FB_SEARCH,		/* nobody known: look for enemies */
    FB_RUN,		/* attack run: fast pass with a volley, extend, again */
    FB_NMODES
};

static const char *fb_mode_name[FB_NMODES] = {
    "hunting", "fighting", "falling back", "refueling", "fetching an item",
    "searching", "attack run"
};

/*
 * Genes: the parameters the evolution strategy adapts, per map.
 */
/* Tactics, chosen per opponent from how they went against him */
enum { FB_TAC_DOGFIGHT, FB_TAC_RUN };

enum { G_DIST, G_AGGR, G_EVADE, G_AIM, G_SPREAD, G_BACK, FB_NGENES };

static const struct {
    const char	*name;
    double	init, lo, hi, sigma;
} fb_gene_def[FB_NGENES] = {
    { "distance",   0.42, 0.18, 0.90, 0.06 },	/* fighting distance / shot range */
    { "aggression", 0.65, 0.20, 1.00, 0.08 },	/* how much it takes to fall back */
    { "evasion",    1.00, 0.40, 2.50, 0.15 },	/* weight of dodging */
    { "aim",        1.00, 0.30, 3.00, 0.15 },	/* weight of pointing at the lead */
    { "spread",     0.80, 0.00, 2.00, 0.15 },	/* shots covering dodges */
    { "fallback",   30.0, 12.0, 72.0, 5.00 },	/* ticks spent falling back */
};

/* ------------------------------------------------------------------------
 * Data types
 * ------------------------------------------------------------------------ */

typedef struct {
    float	*dist;		/* cost to the goal per nav cell (px) */
    int32_t	*next;		/* next cell towards the goal, -2 in the goal */
    int		goal;		/* nav cell of the goal */
    double	gx, gy;
    unsigned long time;
    bool	valid;
} fb_field_t;

/* Where a ship was at some tick. */
typedef struct {
    double	x, y, vx, vy;
    int		dir;
    bool	valid, thrust, shield;
} fb_snap_t;

/* What a bot knows about one enemy. */
typedef struct {
    bool	known, seen, radar;
    double	x, y, vx, vy;	/* absolute px, px/tick */
    double	ax, ay;		/* acceleration, from what was seen */
    int		dir;
    bool	shield;
    unsigned long last_seen, last_known, radar_time;
    double	qx, qy;		/* last radar dot */
    double	fire_rate;	/* shots per tick he was seen to fire */
    int		guns;		/* most shots he was seen to fire at once */
} fb_contact_t;

/* What all fightbots learned about one opponent this session. */
typedef struct {
    char	name[MAX_CHARS];
    double	err[FB_NMODELS];	/* mean squared prediction error */
    double	ax, ay;
    double	pvx, pvy;
    unsigned long pv_time;
    double	pred[FB_PRED_T][FB_NMODELS][2];
    unsigned long pred_time[FB_PRED_T];
    double	kills_us;	/* fightbots he killed */
    double	killed;		/* times fightbots killed him */
    double	tac_n[2], tac_r[2];	/* fights per tactic and their outcome */
} fb_opp_t;

typedef struct {
    int		level;		/* 0: use options.fightRobotLevel */
    bool	learn;
    int		mode;
    unsigned long mode_since;
    int		target_id;
    int		war_id;		/* reprogrammed or at war: only this one */
    fb_contact_t c[NUM_IDS];
    unsigned long threat_time[NUM_IDS];
    /* flight */
    int		plan_len;
    short	plan_dir[FB_H];
    bool	plan_thr[FB_H];
    double	aim_off, steer_off;
    unsigned long aim_time, steer_time;
    /* tactics */
    unsigned long fallback_until, fallback_cool, last_pick;
    double	danger;		/* near misses, decaying */
    double	my_fire;	/* shots per tick fired */
    bool	search_valid;
    double	search_x, search_y;
    unsigned long search_time;
    int		station;
    double	item_x, item_y;
    double	item_ban_x, item_ban_y;	/* an item we did not get to */
    unsigned long item_ban_until;
    unsigned long still_ticks;		/* ticks without moving */
    unsigned long unstick_until;	/* searching elsewhere after standing */
    int		push_ticks;		/* thrusting without moving */
    int		frozen_ticks;		/* the same, escapes included */
    int		escape_dir;
    unsigned long escape_until;
    bool	destruct;
    unsigned long last_missile, last_mine, last_ecm, last_transport;
    unsigned long last_hyper, last_eshield, last_phase;
    /* what this tick's scan found */
    double	missile_dist, heat_dist, mine_dist;
    int		unavoidable;
    /* learning */
    double	gene[FB_NGENES], eps[FB_NGENES];
    bool	anti;		/* next episode mirrors this one */
    bool	ep_active;
    int		ep_kills;
    int		kills, deaths;
    int		last_killer;
    double	pvx, pvy;
    unsigned long alive_ticks;
    /* tactic against the current opponent (FB_TAC_*) and that fight */
    int		tac, tac_opp;
    bool	tac_active;
    unsigned long tac_seen;
    /* attack runs */
    int		run_phase;	/* 0 none, 1 inbound, 2 extending */
    int		run_side;
    double	run_ux, run_uy, run_min, run_gx, run_gy;
    unsigned long run_since;
} fb_data_t;

typedef struct {
    double	x, y, vx, vy;
    int		dir;
    int		hits;
    bool	crashed;
} fb_state_t;

typedef struct {
    int		len;
    short	dir[FB_H];
    bool	thr[FB_H];
} fb_cand_t;

/*
 * A shot as the rollouts see it: its way over the next ticks, bounces off
 * walls included (positions relative to the ship, from tick t0 on).
 */
typedef struct {
    float	px[FB_NEAR_H + 1], py[FB_NEAR_H + 1];
    int		n;		/* positions on its way (it ends after) */
    unsigned	reach;		/* bit k: may meet the ship in step k (from t0) */
    double	t0;		/* fired at this tick (virtual shots) */
    double	weight, r;
} fb_shot_t;

typedef struct {
    double	x, y, vx, vy;
} fb_enemy_t;

/* The best plan: this tick's action and what it runs into. */
typedef struct {
    bool	valid;
    int		dir;
    bool	thrust;
    int		shot_t, ram_t;
} fb_flags_t;

typedef struct {
    player_t	*pl;
    fb_data_t	*d;
    double	ox, oy;			/* origin: ship position (px) */
    fb_state_t	s0;
    double	acc, max_bounce, fric_keep, dt, ship_r, margin, clear0;
    double	wall_r;		/* touching walls: less where we fit (tubes) */
    int		turn, frames;
    bool	grav;
    /* goal */
    const fb_field_t *field;
    double	goal_slack, run_w;
    bool	point;
    double	gx, gy, arrive_r;	/* point goal, relative */
    double	vmax;
    bool	have_car;
    double	carx, cary;		/* relative */
    /* combat */
    bool	has_target, engage, run;
    double	tx, ty, tvx, tvy, tax, tay;	/* relative */
    double	d_pref, d_min, aim_w, band_w, shot_speed, shot_life;
    /* threats */
    int		nshots;
    fb_shot_t	shots[FB_MAX_SHOTS];
    int		nen;
    fb_enemy_t	en[FB_MAX_ENEMIES];
    double	ram_cost;
    double	thrust_cost;		/* per tick of thrust: fuel is short */
    int		worm_ok;		/* the wormhole our way goes through, -1 */
    double	vlim;			/* faster than this costs */
    bool	through;		/* point goal to fly through (items) */
} fb_ctx_t;

typedef struct {
    bool	loaded, dirty;
    char	map[MAX_CHARS];
    double	mean[FB_NGENES], sigma[FB_NGENES];
    double	baseline;
    long	episodes;
} fb_learn_t;

static void Fb_shot_path(fb_shot_t *sh, double ox, double oy, double x, double y,
			 double vx, double vy, double life, bool bounce);

/* ------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------ */

static const poly_t	*FbPdata;
static int		FbNumPolys = -1;
static int		FbMapW = -1, FbMapH = -1;
static bool		FbMapOk;
static bool		FbWrap;
static double		FbW, FbH;
static int		FineW, FineH;
static double		FineCX, FineCY, FineStep;
static float		*ShipClear;
static int		NavW, NavH, NavN, NavF;
static double		NavCX, NavCY;
static float		*NavRX, *NavRY, *NavClear;
static uint8_t		*NavEdge;
static float		*NavCost;

typedef struct { float f; int32_t c; } fb_heap_t;
static fb_heap_t	*Heap;
static int		HeapN, HeapCap;

/* wormholes: nav cell, can be entered, can be an exit */
#define FB_MAX_WORM	64
static int		NumWorm;
static int		WormCell[FB_MAX_WORM];
static bool		WormIn[FB_MAX_WORM], WormOut[FB_MAX_WORM];
static double		WormX[FB_MAX_WORM], WormY[FB_MAX_WORM];

static bool		FbShotGrav;	/* shots fall in the map's gravity */
static double		FbGravX, FbGravY;	/* gravity where the firing ship is */

static fb_field_t	Fields[FB_NFIELDS];
static fb_field_t	*StationF;
static int		NumStationF;
static int		FieldBudget;

static fb_snap_t	Hist[FB_HIST][NUM_IDS];
static int		HistHead, HistCount;
static unsigned long	SeenTick[NUM_IDS];	/* a fightbot had him in view */
static fb_opp_t		Opp[NUM_IDS];

static fb_learn_t	Learn;
static unsigned long	FbTick, LastSave;
static int		NumFightbots;

static const int	nb_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int	nb_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
static const double	fb_model_k[FB_NMODELS] = { 0.0, 0.5, 1.0 };

#define FB_PASS_R	9.0	/* clearance of a nav node (narrow tubes: nose first) */
#define FB_EDGE_R	8.0	/* clearance along a nav edge */
#define FB_TIGHT_R	20.0	/* below this, ways cost much more */
#define FB_PREF_R	45.0	/* below this clearance edges cost more */
#define FB_PREF_W	0.6

/* ------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------ */

static inline fb_data_t *Fb_data(player_t *pl)
{
    return (fb_data_t *)pl->robot_data_ptr->private_data;
}

static inline bool Fb_is_fightbot(player_t *pl)
{
    const char *name;

    if (!Player_is_robot(pl) || pl->robot_data_ptr == NULL)
	return false;
    name = Robot_type_name(pl);
    return name != NULL && strcmp(name, FB_TYPE_NAME) == 0;
}

static inline bool Fb_ally(player_t *a, player_t *b)
{
    if (a == b)
	return true;
    if (BIT(world->rules->mode, TEAM_PLAY) && a->team == b->team)
	return true;
    return Team_immune(a->id, b->id);
}

static inline bool Fb_id_ok(int id)
{
    return id >= 0 && id < NUM_IDS;
}

static inline double Fb_px(click_t c)
{
    return (double)c / CLICK;
}

static inline double Fb_wdx(double dx)
{
    if (FbWrap) {
	if (dx > FbW * 0.5)
	    dx -= FbW;
	else if (dx < -FbW * 0.5)
	    dx += FbW;
    }
    return dx;
}

static inline double Fb_wdy(double dy)
{
    if (FbWrap) {
	if (dy > FbH * 0.5)
	    dy -= FbH;
	else if (dy < -FbH * 0.5)
	    dy += FbH;
    }
    return dy;
}

/* Absolute position back into the map. */
static inline void Fb_wrap_pos(double *x, double *y)
{
    if (FbWrap) {
	while (*x < 0) *x += FbW;
	while (*x >= FbW) *x -= FbW;
	while (*y < 0) *y += FbH;
	while (*y >= FbH) *y -= FbH;
    } else {
	LIMIT(*x, 0.0, FbW - 1.0);
	LIMIT(*y, 0.0, FbH - 1.0);
    }
}

static inline double Fb_dist(double x1, double y1, double x2, double y2)
{
    return LENGTH(Fb_wdx(x2 - x1), Fb_wdy(y2 - y1));
}

static inline int Fb_angle_diff(int a, int b)
{
    int d = MOD2(a - b, RES);

    return d > RES / 2 ? d - RES : d;
}

static inline int Fb_dir_of(double x, double y)
{
    return MOD2((int)(findDir(x, y) + 0.5), RES);
}

static inline int Fb_turn_toward(int dir, int want, int rate)
{
    int d = Fb_angle_diff(want, dir);

    LIMIT(d, -rate, rate);
    return MOD2(dir + d, RES);
}

static int Fb_level(const fb_data_t *d)
{
    int lvl = d->level > 0 ? d->level : options.fightRobotLevel;

    LIMIT(lvl, 1, 100);
    return lvl;
}

/*
 * How far below level 100: 0 at level 100, 1 at level 1.  Rising faster
 * than linear from level 100 down, so middle levels feel it too.
 */
static double Fb_unskill(int level)
{
    return pow((100 - level) / 99.0, 0.7);
}

static int Fb_turn_rate(int level)
{
    return 3 + (level * 9) / 100;	/* same as the other smart robots */
}

static int Fb_delay(int level)
{
    return (int)(Fb_unskill(level) * FB_DELAY_MAX + 0.5);
}

static bool Fb_player_active(player_t *pl)
{
    return pl != NULL && !Player_is_tank(pl) && Player_is_alive(pl)
	&& !Player_is_paused(pl) && !Player_is_waiting(pl);
}

static double Fb_gauss(void)
{
    double u1 = MAX(rfrac(), 1e-9), u2 = rfrac();

    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

/*
 * Fuel a ship "normally" has: what it starts with, unless the tanks hold
 * less.  Low and full are judged against this, as a pilot would.
 */
static double Fb_fuel_ref(player_t *pl)
{
    double ref = world->items[ITEM_FUEL].initial;

    if (ref < 200.0)
	ref = 1000.0;
    return MAX(MIN(ref, pl->fuel.max), 100.0);
}

static double Fb_shot_range(void)
{
    return MAX(options.shotSpeed * options.shotLife, 200.0);
}

/* ------------------------------------------------------------------------
 * Map analysis: clearance grid (as the race bot)
 * ------------------------------------------------------------------------ */

static inline int Fb_fine_index(double x, double y)
{
    int ix, iy;

    if (FbWrap) {
	while (x < 0) x += FbW;
	while (x >= FbW) x -= FbW;
	while (y < 0) y += FbH;
	while (y >= FbH) y -= FbH;
    }
    ix = (int)(x / FineCX);
    iy = (int)(y / FineCY);
    if (x < 0 || y < 0 || ix >= FineW || iy >= FineH)
	return -1;
    return iy * FineW + ix;
}

static inline float Fb_ship_clear(double x, double y)
{
    int i = Fb_fine_index(x, y);

    return i < 0 ? 0.0f : ShipClear[i];
}

/* Minimum clearance along a segment (absolute px), stops below 'need'. */
static double Fb_segment_clear(double x1, double y1, double x2, double y2, double need)
{
    double dx = Fb_wdx(x2 - x1), dy = Fb_wdy(y2 - y1);
    double len = LENGTH(dx, dy), m = 1e9;
    int i, n = (int)(len / FineStep) + 1;

    for (i = 0; i <= n; i++) {
	double f = (double)i / n;
	int k = Fb_fine_index(x1 + dx * f, y1 + dy * f);
	double c = k < 0 ? 0.0 : ShipClear[k];

	if (c < m) {
	    m = c;
	    if (m < need)
		return m;
	}
    }
    return m;
}

static inline bool Fb_line_free(double x1, double y1, double x2, double y2)
{
    return Fb_segment_clear(x1, y1, x2, y2, 1.0) >= 1.0;
}

/* 1-D squared distance transform (Felzenszwalb & Huttenlocher). */
static void Fb_dt1d(const double *f, double *d, int n, int *v, double *z)
{
    int q, k = 0;

    v[0] = 0;
    z[0] = -1e30;
    z[1] = 1e30;
    for (q = 1; q < n; q++) {
	double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k]))
		   / (2.0 * q - 2.0 * v[k]);

	while (s <= z[k]) {
	    k--;
	    s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k]))
		/ (2.0 * q - 2.0 * v[k]);
	}
	k++;
	v[k] = q;
	z[k] = s;
	z[k + 1] = 1e30;
    }
    k = 0;
    for (q = 0; q < n; q++) {
	while (z[k + 1] < q)
	    k++;
	d[q] = (double)(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

/* Exact Euclidean clearance (px) of every fine cell to the nearest obstacle. */
static bool Fb_edt(const uint8_t *obst, float *out)
{
    int maxn = 3 * MAX(FineW, FineH) + 4;
    double *f = malloc(sizeof(double) * maxn), *d = malloc(sizeof(double) * maxn);
    double *z = malloc(sizeof(double) * (maxn + 1));
    double *tmp = malloc(sizeof(double) * (size_t)FineW * FineH);
    int *v = malloc(sizeof(int) * maxn);
    int x, y, i, n, off;
    const double big = 1e12;
    double cell = 0.5 * (FineCX + FineCY);

    if (!f || !d || !z || !tmp || !v) {
	free(f); free(d); free(z); free(tmp); free(v);
	return false;
    }
    for (x = 0; x < FineW; x++) {
	if (FbWrap) {
	    n = 3 * FineH;
	    off = FineH;
	    for (i = 0; i < n; i++)
		f[i] = obst[(i % FineH) * FineW + x] ? 0.0 : big;
	} else {
	    n = FineH + 2;
	    off = 1;
	    f[0] = f[n - 1] = 0.0;
	    for (i = 0; i < FineH; i++)
		f[i + 1] = obst[i * FineW + x] ? 0.0 : big;
	}
	Fb_dt1d(f, d, n, v, z);
	for (y = 0; y < FineH; y++)
	    tmp[y * FineW + x] = d[y + off];
    }
    for (y = 0; y < FineH; y++) {
	if (FbWrap) {
	    n = 3 * FineW;
	    off = FineW;
	    for (i = 0; i < n; i++)
		f[i] = tmp[y * FineW + (i % FineW)];
	} else {
	    n = FineW + 2;
	    off = 1;
	    f[0] = f[n - 1] = 0.0;
	    for (i = 0; i < FineW; i++)
		f[i + 1] = tmp[y * FineW + i];
	}
	Fb_dt1d(f, d, n, v, z);
	for (x = 0; x < FineW; x++) {
	    double c = sqrt(MIN(d[x + off], 1e8)) * cell - 0.5 * cell;

	    out[y * FineW + x] = (float)MAX(c, 0.0);
	}
    }
    free(f); free(d); free(z); free(tmp); free(v);
    return true;
}

/* ------------------------------------------------------------------------
 * Map analysis: navigation grid and distance fields
 * ------------------------------------------------------------------------ */

static inline int Fb_nav_cell(double x, double y)
{
    int ix, iy;

    if (FbWrap) {
	while (x < 0) x += FbW;
	while (x >= FbW) x -= FbW;
	while (y < 0) y += FbH;
	while (y >= FbH) y -= FbH;
    }
    ix = (int)(x / NavCX);
    iy = (int)(y / NavCY);
    LIMIT(ix, 0, NavW - 1);
    LIMIT(iy, 0, NavH - 1);
    return iy * NavW + ix;
}

static inline int Fb_nav_nb(int c, int dx, int dy)
{
    int x = c % NavW + dx, y = c / NavW + dy;

    if (FbWrap) {
	x = (x + NavW) % NavW;
	y = (y + NavH) % NavH;
    } else if (x < 0 || x >= NavW || y < 0 || y >= NavH)
	return -1;
    return y * NavW + x;
}

/* Chebyshev distance of two nav cells. */
static int Fb_cell_dist(int a, int b)
{
    int dx = ABS(a % NavW - b % NavW), dy = ABS(a / NavW - b / NavW);

    if (FbWrap) {
	dx = MIN(dx, NavW - dx);
	dy = MIN(dy, NavH - dy);
    }
    return MAX(dx, dy);
}

static void Fb_heap_push(float f, int32_t c)
{
    int i;

    if (HeapN >= HeapCap)
	return;
    i = HeapN++;
    while (i > 0) {
	int parent = (i - 1) / 2;

	if (Heap[parent].f <= f)
	    break;
	Heap[i] = Heap[parent];
	i = parent;
    }
    Heap[i].f = f;
    Heap[i].c = c;
}

static fb_heap_t Fb_heap_pop(void)
{
    fb_heap_t top = Heap[0], last = Heap[--HeapN];
    int i = 0;

    while (HeapN > 0) {
	int child = 2 * i + 1;

	if (child >= HeapN)
	    break;
	if (child + 1 < HeapN && Heap[child + 1].f < Heap[child].f)
	    child++;
	if (Heap[child].f >= last.f)
	    break;
	Heap[i] = Heap[child];
	i = child;
    }
    if (HeapN > 0)
	Heap[i] = last;
    return top;
}

static void Fb_field_free(fb_field_t *fd)
{
    free(fd->dist);
    free(fd->next);
    fd->dist = NULL;
    fd->next = NULL;
    fd->valid = false;
}

static void Fb_map_free(void)
{
    int i;

    free(ShipClear); free(NavRX); free(NavRY); free(NavClear); free(Heap);
    free(NavEdge); free(NavCost);
    ShipClear = NULL;
    NavRX = NavRY = NavClear = NULL;
    NavEdge = NULL;
    NavCost = NULL;
    Heap = NULL;
    HeapCap = HeapN = 0;
    for (i = 0; i < FB_NFIELDS; i++)
	Fb_field_free(&Fields[i]);
    if (StationF) {
	for (i = 0; i < NumStationF; i++)
	    Fb_field_free(&StationF[i]);
	free(StationF);
	StationF = NULL;
    }
    NumStationF = 0;
    FbMapOk = false;
}

/*
 * Dijkstra: spread the values in the heap over the nav grid.  With a stop
 * cell, only as far as needed around it: everything within 'beyond' px
 * more than its value.
 */
static int RelaxStop = -1;
static float RelaxLimit = FB_INF;

static void Fb_relax(fb_field_t *fd)
{
    while (HeapN > 0) {
	fb_heap_t e = Fb_heap_pop();
	int j;

	if (e.f > fd->dist[e.c])
	    continue;
	if (e.f > RelaxLimit)
	    break;
	if (e.c == RelaxStop)
	    RelaxLimit = e.f + 800.0f;
	for (j = 0; j < 8; j++) {
	    int n;
	    float nd;

	    if (!(NavEdge[e.c] & (1 << j)))
		continue;
	    n = Fb_nav_nb(e.c, nb_dx[j], nb_dy[j]);
	    nd = e.f + NavCost[e.c * 8 + j];
	    if (nd < fd->dist[n]) {
		fd->dist[n] = nd;
		fd->next[n] = e.c;
		Fb_heap_push(nd, n);
	    }
	}
    }
}

/*
 * Dijkstra over the nav grid from the goal (gx, gy) (absolute px), for a
 * ship at (sx, sy) (sx < 0: everywhere).
 */
static bool Fb_field_compute_for(fb_field_t *fd, double gx, double gy, double sx, double sy)
{
    int c, r, gcell = Fb_nav_cell(gx, gy), seeded = 0;

    RelaxStop = sx >= 0.0 ? Fb_nav_cell(sx, sy) : -1;
    RelaxLimit = FB_INF;

    if (fd->dist == NULL || fd->next == NULL) {
	Fb_field_free(fd);
	fd->dist = malloc(sizeof(float) * NavN);
	fd->next = malloc(sizeof(int32_t) * NavN);
	if (!fd->dist || !fd->next) {
	    Fb_field_free(fd);
	    return false;
	}
    }
    for (c = 0; c < NavN; c++) {
	fd->dist[c] = FB_INF;
	fd->next[c] = -1;
    }
    HeapN = 0;
    /* the open nodes nearest to the goal */
    for (r = 0; r <= 6 && seeded == 0; r++) {
	int dx, dy;

	for (dy = -r; dy <= r; dy++) {
	    for (dx = -r; dx <= r; dx++) {
		int n = Fb_nav_nb(gcell, dx, dy);
		double dd;

		if (n < 0 || NavClear[n] < FB_PASS_R)
		    continue;
		dd = Fb_dist(gx, gy, NavRX[n], NavRY[n]);
		if (dd > (r + 0.75) * MAX(NavCX, NavCY))
		    continue;
		if (dd < fd->dist[n]) {
		    fd->dist[n] = (float)dd;
		    fd->next[n] = -2;
		    Fb_heap_push((float)dd, n);
		    seeded++;
		}
	    }
	}
    }
    Fb_relax(fd);
    /*
     * Wormholes: going in takes the ship to one of the exits at random, so
     * an entrance is worth the mean of its exits (and a little for the
     * jump).  Better ways found that way spread on.
     */
    for (r = 0; r < 2 && NumWorm > 0; r++) {
	bool changed = false;
	int w, k;

	for (w = 0; w < NumWorm; w++) {
	    double sum = 0.0;
	    int n = 0;
	    float v;

	    if (WormCell[w] < 0 || !WormIn[w])
		continue;
	    for (k = 0; k < NumWorm; k++) {
		if (k == w || !WormOut[k] || WormCell[k] < 0)
		    continue;
		/* an exit with no way on is a bad draw */
		sum += fd->dist[WormCell[k]] >= FB_INF ? 5000.0 : fd->dist[WormCell[k]];
		n++;
	    }
	    if (n == 0)
		continue;
	    v = (float)(sum / n + 80.0);
	    if (v < fd->dist[WormCell[w]]) {
		fd->dist[WormCell[w]] = v;
		fd->next[WormCell[w]] = -3;	/* into the wormhole */
		Fb_heap_push(v, WormCell[w]);
		changed = true;
	    }
	}
	if (!changed)
	    break;
	Fb_relax(fd);
    }
    RelaxStop = -1;
    RelaxLimit = FB_INF;
    HeapN = 0;
    fd->gx = gx;
    fd->gy = gy;
    fd->goal = gcell;
    fd->time = FbTick;
    fd->valid = true;
    return true;
}

static bool Fb_field_compute(fb_field_t *fd, double gx, double gy)
{
    return Fb_field_compute_for(fd, gx, gy, -1.0, -1.0);
}

/* Nav cell near (x, y) with the lowest cost to the goal (via straight hop). */
static int Fb_field_best_cell(const fb_field_t *fd, double x, double y, double *val)
{
    int c = Fb_nav_cell(x, y), dx, dy, best = -1;
    double bv = FB_INF;

    for (dy = -1; dy <= 1; dy++) {
	for (dx = -1; dx <= 1; dx++) {
	    int n = Fb_nav_nb(c, dx, dy);
	    double v;

	    if (n < 0 || fd->dist[n] >= FB_INF)
		continue;
	    v = fd->dist[n] + Fb_dist(x, y, NavRX[n], NavRY[n]);
	    if (v < bv) {
		bv = v;
		best = n;
	    }
	}
    }
    if (val)
	*val = bv;
    return best;
}

/* Cost to go from (x, y); large when off the field. */
static double Fb_field_value(const fb_field_t *fd, double x, double y)
{
    double v;

    if (Fb_field_best_cell(fd, x, y, &v) < 0)
	return 1e5 + Fb_dist(x, y, fd->gx, fd->gy) * 3.0;
    return v;
}

/*
 * Farthest point along the field's path from (x, y) still reachable in a
 * straight line with clearance 'clear' ("string pulling").
 */
static bool Fb_field_carrot(const fb_field_t *fd, double x, double y,
			    double clear, int max_steps, double *cx, double *cy)
{
    int cur = Fb_field_best_cell(fd, x, y, NULL), i;

    if (cur < 0)
	return false;
    *cx = NavRX[cur];
    *cy = NavRY[cur];
    for (i = 0; i < max_steps; i++) {
	int nx = fd->next[cur];

	if (nx < 0)
	    break;
	if (Fb_segment_clear(x, y, NavRX[nx], NavRY[nx], clear) < clear)
	    break;
	*cx = NavRX[nx];
	*cy = NavRY[nx];
	cur = nx;
    }
    if (fd->next[cur] == -2
	&& Fb_segment_clear(x, y, fd->gx, fd->gy, clear) >= clear) {
	*cx = fd->gx;
	*cy = fd->gy;
    } else if (fd->next[cur] == -3) {
	/* the way goes through a wormhole: fly right into it */
	for (i = 0; i < NumWorm; i++) {
	    if (WormCell[i] == cur && WormIn[i]
		&& Fb_segment_clear(x, y, WormX[i], WormY[i], clear * 0.5) >= clear * 0.5) {
		*cx = WormX[i];
		*cy = WormY[i];
		break;
	    }
	}
    }
    return true;
}

static bool Fb_map_build(void)
{
    long blocks = (long)world->x * world->y;
    int nb = 1, x, y, c, i, j;
    uint8_t *obs;

    Fb_map_free();
    FbPdata = pdata;
    FbNumPolys = num_polys;
    FbMapW = world->cwidth;
    FbMapH = world->cheight;

    while (blocks / ((long)nb * nb) > 90000)
	nb++;
    FbWrap = BIT(world->rules->mode, WRAP_PLAY) ? true : false;
    FbW = (double)world->cwidth / CLICK;
    FbH = (double)world->cheight / CLICK;
    NavF = 5;
    NavW = MAX(1, (int)(FbW / (BLOCK_SZ * nb) + 0.5));
    NavH = MAX(1, (int)(FbH / (BLOCK_SZ * nb) + 0.5));
    NavN = NavW * NavH;
    NavCX = FbW / NavW;
    NavCY = FbH / NavH;
    FineW = NavW * NavF;
    FineH = NavH * NavF;
    FineCX = NavCX / NavF;
    FineCY = NavCY / NavF;
    FineStep = MIN(FineCX, FineCY);

    ShipClear = malloc(sizeof(float) * (size_t)FineW * FineH);
    obs = malloc((size_t)FineW * FineH);
    NavRX = malloc(sizeof(float) * NavN);
    NavRY = malloc(sizeof(float) * NavN);
    NavClear = malloc(sizeof(float) * NavN);
    HeapCap = NavN * 8 + 64;
    Heap = malloc(sizeof(fb_heap_t) * HeapCap);
    NavEdge = calloc(NavN, 1);
    NavCost = malloc(sizeof(float) * NavN * 8);
    NumStationF = Num_fuels();
    StationF = calloc(MAX(NumStationF, 1), sizeof(fb_field_t));
    if (!ShipClear || !obs || !NavRX || !NavRY || !NavClear || !Heap
	|| !NavEdge || !NavCost || !StationF) {
	free(obs);
	Fb_map_free();
	return false;
    }

    /* Rasterize the walls ships hit */
    for (y = 0; y < FineH; y++) {
	for (x = 0; x < FineW; x++) {
	    int cx = (int)(((x + 0.5) * FineCX) * CLICK);
	    int cy = (int)(((y + 0.5) * FineCY) * CLICK);

	    int g;
	    group_t *gp;

	    LIMIT(cx, 0, world->cwidth - 1);
	    LIMIT(cy, 0, world->cheight - 1);
	    g = is_inside(cx, cy, NONBALL_BIT, NULL);
	    /* a ship enters a wormhole, it does not hit it */
	    obs[y * FineW + x] = g != NO_GROUP
		&& ((gp = groupptr_by_id(g)) == NULL || gp->type != WORMHOLE);
	}
    }
    /* thin walls fall between the cell centres: walk the edges too */
    for (i = 0; i < num_polys; i++) {
	const poly_t *p = &pdata[i];
	group_t *gp = groupptr_by_id(p->group);
	const int *e = edgeptr + p->edges;
	double ex = Fb_px(p->pos.cx), ey = Fb_px(p->pos.cy);

	if (p->is_decor || (gp != NULL && ((gp->hitmask & NONBALL_BIT)
					   || gp->type == FRICTION || gp->type == WORMHOLE)))
	    continue;
	for (j = 0; j < p->num_points; j++) {
	    double dx = Fb_px(e[2 * j]), dy = Fb_px(e[2 * j + 1]);
	    int k, n = (int)(LENGTH(dx, dy) / (0.5 * FineStep)) + 1;

	    for (k = 0; k <= n; k++) {
		int fi = Fb_fine_index(ex + dx * k / n, ey + dy * k / n);

		if (fi >= 0)
		    obs[fi] = 1;
	    }
	    ex += dx;
	    ey += dy;
	}
    }
    if (!Fb_edt(obs, ShipClear)) {
	free(obs);
	Fb_map_free();
	return false;
    }
    free(obs);

    /* Nav nodes sit at the most open point of their cell */
    for (c = 0; c < NavN; c++) {
	int nx = c % NavW, ny = c / NavW, fx, fy;
	double best = -1e9;

	NavRX[c] = (float)((nx + 0.5) * NavCX);
	NavRY[c] = (float)((ny + 0.5) * NavCY);
	NavClear[c] = 0.0f;
	for (fy = 0; fy < NavF; fy++) {
	    for (fx = 0; fx < NavF; fx++) {
		int fi = (ny * NavF + fy) * FineW + nx * NavF + fx;
		double ox = (fx - (NavF - 1) * 0.5) * FineCX;
		double oy = (fy - (NavF - 1) * 0.5) * FineCY;
		double sc = MIN(ShipClear[fi], 60.0f) - 0.6 * LENGTH(ox, oy);

		if (sc > best) {
		    best = sc;
		    NavRX[c] = (float)((nx * NavF + fx + 0.5) * FineCX);
		    NavRY[c] = (float)((ny * NavF + fy + 0.5) * FineCY);
		    NavClear[c] = ShipClear[fi];
		}
	    }
	}
    }

    /* Edges with a cost that keeps a little away from walls */
    for (c = 0; c < NavN; c++) {
	if (NavClear[c] < FB_PASS_R)
	    continue;
	for (j = 0; j < 8; j++) {
	    int n = Fb_nav_nb(c, nb_dx[j], nb_dy[j]);
	    double m, len, pen;

	    if (n < 0 || NavClear[n] < FB_PASS_R)
		continue;
	    m = Fb_segment_clear(NavRX[c], NavRY[c], NavRX[n], NavRY[n], FB_EDGE_R);
	    if (m < FB_EDGE_R)
		continue;
	    len = Fb_dist(NavRX[c], NavRY[c], NavRX[n], NavRY[n]);
	    pen = MAX(0.0, (FB_PREF_R - m) / FB_PREF_R);
	    NavEdge[c] |= (uint8_t)(1 << j);
	    NavCost[c * 8 + j] = (float)(len * (1.0 + FB_PREF_W * pen * pen
						+ (m < FB_TIGHT_R ? 4.0 * sqr((FB_TIGHT_R - m) / FB_TIGHT_R) : 0.0)));
	}
    }
    NumWorm = MIN(Num_wormholes(), FB_MAX_WORM);
    for (i = 0; i < NumWorm; i++) {
	wormhole_t *w = Wormhole_by_index(i);

	WormX[i] = Fb_px(w->pos.cx);
	WormY[i] = Fb_px(w->pos.cy);
	WormCell[i] = Fb_nav_cell(WormX[i], WormY[i]);
	WormIn[i] = w->type == WORM_NORMAL || w->type == WORM_IN;
	WormOut[i] = w->type == WORM_NORMAL || w->type == WORM_OUT;
    }
    FbMapOk = true;
    /* the ways to all fuel stations, once (they show which ones are reachable) */
    for (i = 0; i < NumStationF; i++) {
	fuel_t *f = Fuel_by_index(i);

	Fb_field_compute(&StationF[i], Fb_px(f->pos.cx), Fb_px(f->pos.cy));
    }
    FB_LOG("[fightbot] map analysed: fine %dx%d (%.1f px), nav %dx%d\n",
	   FineW, FineH, FineCX, NavW, NavH);
    return true;
}

static bool Fb_map_update(void)
{
    bool same = FbPdata == pdata && FbNumPolys == num_polys
	&& FbMapW == world->cwidth && FbMapH == world->cheight;

    if (same)
	return FbMapOk;	/* failed before: don't retry every tick */
    return Fb_map_build();
}

/*
 * Field to a moving goal (absolute px).  Fields are shared by all
 * fightbots; one whose goal lies within two nav cells and that is at most
 * max_age ticks old serves.  Only a few are computed per tick, otherwise
 * an older one near the goal is used.
 */
static const fb_field_t *Fb_goal_field(double gx, double gy, unsigned long max_age,
					double sx, double sy)
{
    int gc, i, best = -1, near = -1, nearc = -1, bd = 1 << 30, nd = 1 << 30, ncd = 1 << 30;
    int slot = -1;
    unsigned long oldest = ~0UL;
    bool covers;

    if (!FbMapOk)
	return NULL;
    gc = Fb_nav_cell(gx, gy);
    for (i = 0; i < FB_NFIELDS; i++) {
	fb_field_t *f = &Fields[i];
	int cd;

	if (!f->valid) {
	    if (slot < 0 || Fields[slot].valid)
		slot = i;
	    continue;
	}
	cd = Fb_cell_dist(gc, f->goal);
	if (cd > 8)
	    covers = false;
	else	/* it reaches out to us */
	    covers = Fb_field_best_cell(f, sx, sy, NULL) >= 0;
	if (cd <= 3 && FbTick - f->time <= max_age && cd < bd && covers) {
	    bd = cd;
	    best = i;
	}
	if (cd <= 8 && cd < nd) {
	    nd = cd;
	    near = i;
	}
	if (covers && cd < ncd) {
	    ncd = cd;
	    nearc = i;
	}
	if ((slot < 0 || Fields[slot].valid) && f->time < oldest) {
	    oldest = f->time;
	    slot = i;
	}
    }
    if (best >= 0)
	return &Fields[best];
    if (FieldBudget > 0) {
	if (near >= 0)
	    slot = near;	/* the same goal moved on */
	FieldBudget--;
	if (slot >= 0 && Fb_field_compute_for(&Fields[slot], gx, gy, sx, sy))
	    return &Fields[slot];
	return NULL;
    }
    return nearc >= 0 ? &Fields[nearc] : NULL;
}

static const fb_field_t *Fb_station_field(int fs)
{
    fuel_t *f;

    if (fs < 0 || fs >= NumStationF || !FbMapOk)
	return NULL;
    if (!StationF[fs].valid) {
	if (FieldBudget <= 0)
	    return NULL;
	FieldBudget--;
	f = Fuel_by_index(fs);
	if (!Fb_field_compute(&StationF[fs], Fb_px(f->pos.cx), Fb_px(f->pos.cy)))
	    return NULL;
    }
    return &StationF[fs];
}

/* ------------------------------------------------------------------------
 * Learning: genes per map, kept in a small text file
 * ------------------------------------------------------------------------ */

static const char *Fb_learn_path(bool mkdirs)
{
    static char path[PATH_MAX];
    const char *base = Conf_user_data_dir();
    char name[MAX_CHARS], *p;
    int i, n = 0;

    for (i = 0; Learn.map[i] != '\0' && n < (int)sizeof(name) - 1; i++) {
	char ch = Learn.map[i];

	if (isalnum((unsigned char)ch) || ch == '-' || ch == '_')
	    name[n++] = (char)tolower((unsigned char)ch);
	else if (n > 0 && name[n - 1] != '_')
	    name[n++] = '_';
    }
    name[n] = '\0';
    if (n == 0)
	strlcpy(name, "unnamed", sizeof(name));

    if (base == NULL)
	return NULL;
    snprintf(path, sizeof(path), "%s/" FB_LEARN_SUBDIR "/%s.txt", base, name);
    if (mkdirs) {
	for (p = path + 1; (p = strchr(p, '/')) != NULL; p++) {
	    *p = '\0';
	    mkdir(path, 0755);
	    *p = '/';
	}
    }
    return path;
}

static void Fb_learn_reset(void)
{
    int g;

    for (g = 0; g < FB_NGENES; g++) {
	Learn.mean[g] = fb_gene_def[g].init;
	Learn.sigma[g] = fb_gene_def[g].sigma;
    }
    Learn.baseline = -1.0;
    Learn.episodes = 0;
}

static void Fb_learn_save(void)
{
    const char *path;
    char tmp[PATH_MAX + 8];
    FILE *fp;
    bool ok = true;
    int g;

    if (!Learn.loaded)
	return;
    path = Fb_learn_path(true);
    if (path == NULL)
	return;
    /* write a temporary file and rename it, a crash never leaves half a file */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if ((fp = fopen(tmp, "w")) == NULL)
	return;
    if (fprintf(fp, "fightbot %d %ld %.4f\n", FB_LEARN_VERSION,
		Learn.episodes, Learn.baseline) < 0)
	ok = false;
    for (g = 0; g < FB_NGENES && ok; g++) {
	if (fprintf(fp, "%s %.5f %.5f\n", fb_gene_def[g].name, Learn.mean[g],
		    Learn.sigma[g]) < 0)
	    ok = false;
    }
    if (fclose(fp) != 0)
	ok = false;
    if (ok && rename(tmp, path) == 0)
	Learn.dirty = false;
    else
	remove(tmp);
    LastSave = FbTick;
}

static void Fb_learn_load(void)
{
    const char *path;
    FILE *fp;
    char line[256];

    if (Learn.loaded && strcmp(Learn.map, world->name) == 0)
	return;
    if (Learn.loaded && Learn.dirty)
	Fb_learn_save();
    memset(&Learn, 0, sizeof(Learn));
    strlcpy(Learn.map, world->name, sizeof(Learn.map));
    Fb_learn_reset();
    Learn.loaded = true;

    path = Fb_learn_path(false);
    if (path == NULL || (fp = fopen(path, "r")) == NULL)
	return;
    if (fgets(line, sizeof(line), fp) != NULL) {
	int ver;
	long eps;
	double base;

	if (sscanf(line, "fightbot %d %ld %lf", &ver, &eps, &base) == 3
	    && ver == FB_LEARN_VERSION) {
	    Learn.episodes = eps;
	    Learn.baseline = base;
	    while (fgets(line, sizeof(line), fp) != NULL) {
		char name[64];
		double m, s;
		int g;

		if (sscanf(line, "%63s %lf %lf", name, &m, &s) != 3)
		    continue;
		for (g = 0; g < FB_NGENES; g++) {
		    if (strcmp(name, fb_gene_def[g].name) != 0)
			continue;
		    LIMIT(m, fb_gene_def[g].lo, fb_gene_def[g].hi);
		    LIMIT(s, fb_gene_def[g].sigma * 0.3, fb_gene_def[g].sigma);
		    Learn.mean[g] = m;
		    Learn.sigma[g] = s;
		}
	    }
	    FB_LOG("[fightbot] genes loaded from %s\n", path);
	}
    }
    fclose(fp);
}

/* A life starts: try genes a little off the mean (mirrored every second life). */
static void Fb_episode_start(fb_data_t *d)
{
    int g;

    for (g = 0; g < FB_NGENES; g++) {
	if (d->anti)
	    d->eps[g] = -d->eps[g];
	else
	    d->eps[g] = d->learn ? Fb_gauss() : 0.0;
	d->gene[g] = Learn.mean[g] + d->eps[g] * Learn.sigma[g];
	LIMIT(d->gene[g], fb_gene_def[g].lo, fb_gene_def[g].hi);
    }
    d->anti = !d->anti;
    d->ep_kills = 0;
    d->ep_active = true;
}

/*
 * A life ended: kills minus the death.  The mean moves towards genes that
 * did better than usual (an evolution strategy with a running baseline).
 */
static void Fb_episode_end(fb_data_t *d, bool died)
{
    double reward = d->ep_kills - (died ? 1.0 : 0.0), adv;
    int g;

    if (!d->ep_active)
	return;
    d->ep_active = false;
    if (!d->learn || !Learn.loaded)
	return;
    if (Learn.episodes == 0 && Learn.baseline < -0.99 && Learn.baseline > -1.01)
	Learn.baseline = reward;
    adv = reward - Learn.baseline;
    LIMIT(adv, -3.0, 3.0);
    for (g = 0; g < FB_NGENES; g++) {
	double s = Learn.sigma[g];

	Learn.mean[g] += 0.08 * adv * d->eps[g] * s;
	LIMIT(Learn.mean[g], fb_gene_def[g].lo, fb_gene_def[g].hi);
	Learn.sigma[g] = MAX(s * 0.998, fb_gene_def[g].sigma * 0.3);
    }
    Learn.baseline = 0.95 * Learn.baseline + 0.05 * reward;
    Learn.episodes++;
    Learn.dirty = true;
}

/* ------------------------------------------------------------------------
 * History and opponent models (shared by all fightbots)
 * ------------------------------------------------------------------------ */

static void Fb_record_history(void)
{
    int i;

    HistHead = (HistHead + 1) % FB_HIST;
    if (HistCount < FB_HIST)
	HistCount++;
    for (i = 0; i < NUM_IDS; i++)
	Hist[HistHead][i].valid = false;
    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);
	fb_snap_t *s;

	if (!Fb_id_ok(pl->id) || !Fb_player_active(pl))
	    continue;
	s = &Hist[HistHead][pl->id];
	s->x = Fb_px(pl->pos.cx);
	s->y = Fb_px(pl->pos.cy);
	s->vx = pl->vel.x;
	s->vy = pl->vel.y;
	s->dir = pl->dir;
	s->thrust = Player_is_thrusting(pl);
	s->shield = BIT(pl->used, HAS_SHIELD) || Player_uses_emergency_shield(pl)
	    || Player_is_phasing(pl);
	s->valid = true;
    }
}

static const fb_snap_t *Fb_snap(int id, int delay)
{
    delay = MIN(delay, HistCount - 1);
    delay = MAX(delay, 0);
    return &Hist[(HistHead - delay + FB_HIST) % FB_HIST][id];
}

static fb_opp_t *Fb_opp(player_t *e)
{
    fb_opp_t *o;
    int k;

    if (!Fb_id_ok(e->id))
	return NULL;
    o = &Opp[e->id];
    if (strcmp(o->name, e->name) != 0) {
	memset(o, 0, sizeof(*o));
	strlcpy(o->name, e->name, sizeof(o->name));
	for (k = 0; k < FB_NMODELS; k++)
	    o->err[k] = 400.0 * (1.0 + 0.01 * k);	/* linear first */
    }
    return o;
}

/* The motion model that predicted him best so far. */
static double Fb_opp_accel_k(player_t *e)
{
    fb_opp_t *o = Fb_opp(e);
    int k, best = 0;

    if (o == NULL)
	return 0.0;
    for (k = 1; k < FB_NMODELS; k++) {
	if (o->err[k] < o->err[best])
	    best = k;
    }
    return fb_model_k[best];
}

/* How dangerous he has been for fightbots: about 0.5 .. 2. */
static double Fb_opp_respect(player_t *e)
{
    fb_opp_t *o = Fb_opp(e);
    double r;

    if (o == NULL)
	return 1.0;
    r = (o->kills_us + 1.0) / (o->killed + 1.0);
    LIMIT(r, 0.5, 2.0);
    return r;
}

/*
 * Which tactic for a fight against him: the one that went best against
 * him so far (dogfight a little favoured), with some exploration while
 * there are few fights to go by.
 */
static int Fb_choose_tactic(player_t *e)
{
    fb_opp_t *o = Fb_opp(e);
    double v[2];
    int k;

    if (o == NULL)
	return FB_TAC_DOGFIGHT;
    for (k = 0; k < 2; k++)
	v[k] = (o->tac_r[k] + (k == FB_TAC_DOGFIGHT ? 0.5 : 0.0)) / (o->tac_n[k] + 1.0)
	    + Fb_gauss() * 0.5 / sqrt(o->tac_n[k] + 1.0);
    return v[FB_TAC_RUN] > v[FB_TAC_DOGFIGHT] ? FB_TAC_RUN : FB_TAC_DOGFIGHT;
}

/* A fight is over: +1 he died, -1 we died, 0 it fizzled out. */
static void Fb_tactic_done(fb_data_t *d, double outcome)
{
    player_t *e;
    fb_opp_t *o;

    if (!d->tac_active)
	return;
    d->tac_active = false;
    if (!Fb_id_ok(d->tac_opp) || (e = Player_by_id(d->tac_opp)) == NULL
	|| (o = Fb_opp(e)) == NULL)
	return;
    o->tac_n[d->tac] += 1.0;
    o->tac_r[d->tac] += outcome;
    FB_LOG("[fightbot] tactic %s against %s: %+.0f (%.0f fights)\n",
	   d->tac == FB_TAC_RUN ? "run" : "dogfight", e->name, outcome, o->tac_n[d->tac]);
}

/*
 * Learn how each opponent flies: predictions made FB_PRED_T ticks ago
 * with each motion model are compared with where he went.  Only while a
 * fightbot has him in view, as a human would watch him.
 */
static void Fb_update_opponents(void)
{
    int i, k, slot = (int)(FbTick % FB_PRED_T);

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	const fb_snap_t *s;
	fb_opp_t *o;
	double T = FB_PRED_T;

	if (!Fb_id_ok(e->id) || !Fb_player_active(e))
	    continue;
	s = &Hist[HistHead][e->id];
	if (!s->valid || FbTick - SeenTick[e->id] > 1)
	    continue;
	o = Fb_opp(e);
	if (o == NULL)
	    continue;
	if (o->pv_time == FbTick - 1) {
	    o->ax = 0.7 * o->ax + 0.3 * (s->vx - o->pvx);
	    o->ay = 0.7 * o->ay + 0.3 * (s->vy - o->pvy);
	} else
	    o->ax = o->ay = 0.0;
	o->pvx = s->vx;
	o->pvy = s->vy;
	o->pv_time = FbTick;
	if (o->pred_time[slot] == FbTick - FB_PRED_T && o->pred_time[slot] > 0) {
	    for (k = 0; k < FB_NMODELS; k++) {
		double ex = Fb_wdx(s->x - o->pred[slot][k][0]);
		double ey = Fb_wdy(s->y - o->pred[slot][k][1]);
		double e2 = MIN(ex * ex + ey * ey, 1e5);

		o->err[k] = 0.97 * o->err[k] + 0.03 * e2;
	    }
	}
	for (k = 0; k < FB_NMODELS; k++) {
	    o->pred[slot][k][0] = s->x + s->vx * T + 0.5 * fb_model_k[k] * o->ax * T * T;
	    o->pred[slot][k][1] = s->y + s->vy * T + 0.5 * fb_model_k[k] * o->ay * T * T;
	}
	o->pred_time[slot] = FbTick;
    }
}

/* ------------------------------------------------------------------------
 * Perception: what a human pilot in this ship would know
 * ------------------------------------------------------------------------ */

static void Fb_perceive(player_t *pl, fb_data_t *d, int level)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy);
    double rcw = FbW / 256.0, rch = FbH / 256.0;	/* radar resolution */
    bool blind = pl->damaged > 0;
    bool lv = BIT(world->rules->mode, LIMITED_VISIBILITY) ? true : false;
    int i, delay = Fb_delay(level);

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	const fb_snap_t *s;
	double dx, dy, dist;
	bool cansee, sensor_ok;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (Fb_ally(pl, e) || !Fb_player_active(e)) {
	    c->known = c->seen = c->radar = false;
	    continue;
	}
	s = Fb_snap(e->id, delay);
	if (!s->valid) {
	    c->seen = c->radar = false;
	    if (c->known && FbTick - c->last_known > 2)
		c->known = false;
	    continue;
	}
	dx = Fb_wdx(s->x - ox);
	dy = Fb_wdy(s->y - oy);
	dist = LENGTH(dx, dy);
	cansee = pl->visibility[i].canSee;
	sensor_ok = !lv || dist <= pl->sensor_range;
	c->seen = !blind && cansee && sensor_ok
	    && fabs(dx) < FB_VIEW_HW && fabs(dy) < FB_VIEW_HH;
	c->radar = false;
	if (c->seen) {
	    if (c->known && c->last_seen == FbTick - 1) {
		c->ax = 0.6 * c->ax + 0.4 * (s->vx - c->vx);
		c->ay = 0.6 * c->ay + 0.4 * (s->vy - c->vy);
	    } else
		c->ax = c->ay = 0.0;
	    c->x = s->x;
	    c->y = s->y;
	    c->vx = s->vx;
	    c->vy = s->vy;
	    c->dir = s->dir;
	    c->shield = s->shield;
	    c->known = true;
	    c->last_seen = c->last_known = FbTick;
	    SeenTick[e->id] = FbTick;
	} else if (!blind && options.playersOnRadar && cansee && sensor_ok) {
	    /* a dot on the radar: position only */
	    double qx = (floor(s->x / rcw) + 0.5) * rcw;
	    double qy = (floor(s->y / rch) + 0.5) * rch;

	    if (c->known && c->last_seen != FbTick - 1 && c->radar_time > 0
		&& FbTick - c->radar_time <= 3) {
		double n = (double)(FbTick - c->radar_time);

		c->vx = 0.75 * c->vx + 0.25 * Fb_wdx(qx - c->qx) / n;
		c->vy = 0.75 * c->vy + 0.25 * Fb_wdy(qy - c->qy) / n;
	    } else if (!c->known) {
		c->vx = c->vy = 0.0;
	    }
	    c->qx = qx;
	    c->qy = qy;
	    c->radar_time = FbTick;
	    c->x = qx;
	    c->y = qy;
	    c->ax *= 0.5;
	    c->ay *= 0.5;
	    c->shield = false;
	    c->known = true;
	    c->radar = true;
	    c->last_known = FbTick;
	} else if (c->known) {
	    /* out of sight: where he would be now */
	    c->x += c->vx;
	    c->y += c->vy;
	    Fb_wrap_pos(&c->x, &c->y);
	    if (FbTick - c->last_known > FB_MEMORY)
		c->known = false;
	}
    }
}

static inline bool Fb_in_view(double rx, double ry)
{
    return fabs(rx) < FB_VIEW_HW && fabs(ry) < FB_VIEW_HH;
}

/* A missile or mine lying around that is about to go off (it turned white). */
static bool Fb_item_rogue(object_t *o)
{
    int t;

    if (o->type != OBJ_ITEM || o->color != WHITE)
	return false;
    t = ITEM_PTR(o)->item_type;
    return (t == ITEM_MISSILE && options.rogueHeatProb > 0.0)
	|| (t == ITEM_MINE && options.rogueMineProb > 0.0);
}

/* Is this object a danger to us at all? */
static bool Fb_object_hostile(player_t *pl, object_t *o)
{
    if (Fb_item_rogue(o))
	return true;
    if (o->type == OBJ_ASTEROID)
	return options.asteroidCollisionMayKill;
    if (o->type == OBJ_WRECKAGE)
	return options.wreckageCollisionMayKill;
    if (o->type != OBJ_SHOT && o->type != OBJ_CANNON_SHOT
	&& o->type != OBJ_TORPEDO && o->type != OBJ_SMART_SHOT
	&& o->type != OBJ_HEAT_SHOT && o->type != OBJ_MINE
	&& o->type != OBJ_PULSE)
	return false;
    /* our own shots can come back off a wall or be overtaken; mines hurt too */
    if (o->id == pl->id)
	return !options.selfImmunity && o->type != OBJ_PULSE;
    if (o->id != NO_ID && Team_immune(o->id, pl->id))
	return false;
    if (options.teamImmunity && BIT(world->rules->mode, TEAM_PLAY)
	&& o->team == pl->team && o->id != NO_ID)
	return false;
    return true;
}

/*
 * Seen dangers: shots, missiles, mines in view.  With the reaction delay
 * a shot is seen where it was 'delay' ticks ago, and at low levels some
 * are overlooked.  Also notes near misses, who shoots at us and how much
 * the enemies fire.
 */
static void Fb_scan_objects(player_t *pl, fb_data_t *d, int level, fb_ctx_t *cx)
{
    double ox = cx->ox, oy = cx->oy, u = Fb_unskill(level);
    int i, delay = Fb_delay(level), near_miss = 0;
    unsigned overlook = (unsigned)(FB_OVERLOOK * u * 1000.0);
    static int fired[NUM_IDS];
    static unsigned long fired_tick[NUM_IDS];

    cx->nshots = 0;
    d->missile_dist = d->heat_dist = d->mine_dist = 1e9;
    if (pl->damaged > 0)
	return;		/* blinded */
    for (i = 0; i < NumObjs; i++) {
	object_t *o = Obj[i];
	double rx, ry, dvx, dvy, tc, dmin, dist, age = 1e9, r = FB_SHIP_R + 4.0;
	fb_shot_t *sh;

	if (!Fb_object_hostile(pl, o))
	    continue;
	rx = Fb_wdx(Fb_px(o->pos.cx) - ox);
	ry = Fb_wdy(Fb_px(o->pos.cy) - oy);
	if (!Fb_in_view(rx, ry))
	    continue;
	if (o->type == OBJ_SHOT)
	    age = options.shotLife - o->life;
	/* who fires how much (a human sees new shots appear) */
	if (o->type == OBJ_SHOT && age <= 1.0 && Fb_id_ok(o->id) && o->id != pl->id) {
	    if (fired_tick[o->id] != FbTick) {
		fired_tick[o->id] = FbTick;
		fired[o->id] = 0;
	    }
	    fired[o->id]++;
	}
	if (age < delay)
	    continue;	/* not noticed yet */
	if (overlook > 0
	    && ((((uintptr_t)o >> 4) * 2654435761u) >> 7) % 1000 < overlook)
	    continue;
	rx -= o->vel.x * delay;
	ry -= o->vel.y * delay;
	dist = LENGTH(rx, ry);
	if (o->type == OBJ_SMART_SHOT || o->type == OBJ_TORPEDO)
	    d->missile_dist = MIN(d->missile_dist, dist);
	else if (o->type == OBJ_HEAT_SHOT)
	    d->heat_dist = MIN(d->heat_dist, dist);
	else if (o->type == OBJ_MINE)
	    d->mine_dist = MIN(d->mine_dist, dist);
	dvx = o->vel.x - pl->vel.x;
	dvy = o->vel.y - pl->vel.y;
	tc = -(rx * dvx + ry * dvy) / MAX(dvx * dvx + dvy * dvy, 1e-6);
	LIMIT(tc, 0.0, (double)FB_NEAR_H);
	dmin = LENGTH(rx + dvx * tc, ry + dvy * tc);
	if (o->type == OBJ_SHOT && tc > 0.0 && tc < 10.0 && dmin < 40.0) {
	    near_miss++;
	    if (Fb_id_ok(o->id))
		d->threat_time[o->id] = FbTick;
	}
	if (cx->nshots >= FB_MAX_SHOTS)
	    continue;
	if (o->type == OBJ_MINE || o->type == OBJ_ITEM)
	    r = MINE_RANGE * 0.6;
	else if (o->type == OBJ_ASTEROID || o->type == OBJ_WRECKAGE)
	    r = FB_SHIP_R + 4.0 + o->pl_range;
	else if (o->type == OBJ_SMART_SHOT || o->type == OBJ_HEAT_SHOT
		 || o->type == OBJ_TORPEDO)
	    r = FB_SHIP_R + 12.0;
	if (dist > 160.0 + FB_NEAR_H * (2.0 * cx->acc + LENGTH(o->vel.x, o->vel.y)) + r)
	    continue;
	/* our own: only when it may have bounced (it is old enough) */
	if (o->id == pl->id && o->type == OBJ_SHOT && age < 3.0)
	    continue;
	sh = &cx->shots[cx->nshots++];
	sh->t0 = 0.0;
	sh->weight = FB_SHOT_COST;
	sh->r = r;
	{
	    double vx = o->vel.x, vy = o->vel.y;
	    double life = (o->type == OBJ_MINE || o->type == OBJ_ITEM) ? 1e9 : o->life;
	    bool bounce = o->type == OBJ_MINE ? options.minesWallBounce
		: (o->type == OBJ_SHOT || o->type == OBJ_CANNON_SHOT) ? options.shotsWallBounce
		: options.missilesWallBounce;

	    /* homing missiles come at us */
	    if (o->type == OBJ_SMART_SHOT || o->type == OBJ_HEAT_SHOT) {
		double sp = LENGTH(o->vel.x, o->vel.y), l = MAX(dist, 1.0);

		vx = pl->vel.x - rx / l * MAX(sp, 8.0);
		vy = pl->vel.y - ry / l * MAX(sp, 8.0);
	    }
	    Fb_shot_path(sh, ox, oy, rx, ry, vx, vy, life, bounce);
	}
	(void)dmin;
    }
    d->danger = d->danger * 0.95 + near_miss;
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	int n;

	if (!Fb_id_ok(e->id) || e == pl)
	    continue;
	c = &d->c[e->id];
	n = fired_tick[e->id] == FbTick ? fired[e->id] : 0;
	c->fire_rate = 0.95 * c->fire_rate + 0.05 * n;
	c->guns = MAX(c->guns, n);
    }
}

/* ------------------------------------------------------------------------
 * Physics model (as the race bot)
 * ------------------------------------------------------------------------ */

static double Fb_ship_radius(player_t *pl)
{
    double r = SHIP_SZ;
    int i;

    if (pl->ship == NULL)
	return r + 1.0;
    for (i = 0; i < pl->ship->num_points; i++) {
	clpos_t pt = pl->ship->pts[i][0];

	r = MAX(r, LENGTH(pt.cx, pt.cy) / CLICK);
    }
    return r + 1.0;
}

static double Fb_power(int level)
{
    return MAX_PLAYER_POWER * (1.0 - FB_POWER_LOSS * Fb_unskill(level));
}

static double Fb_thrust_acc(player_t *pl)
{
    double power = pl->power > 0.0 ? pl->power : MAX_PLAYER_POWER;
    int a = pl->item[ITEM_AFTERBURNER];

    if (pl->fuel.sum <= 0.0)
	power = MIN_PLAYER_POWER * 0.6;
    else if (a)
	power = AFTER_BURN_POWER(power, a);
    return power / MAX(pl->mass, 1.0);
}

static bool Fb_wall_normal(double x, double y, double *nx, double *ny)
{
    int a = Fb_fine_index(x + FineStep, y), b = Fb_fine_index(x - FineStep, y);
    int c = Fb_fine_index(x, y + FineStep), e = Fb_fine_index(x, y - FineStep);
    double gx, gy, l;

    if (a < 0 || b < 0 || c < 0 || e < 0)
	return false;
    gx = ShipClear[a] - ShipClear[b];
    gy = ShipClear[c] - ShipClear[e];
    l = LENGTH(gx, gy);
    if (l < 1e-6)
	return false;
    *nx = gx / l;
    *ny = gy / l;
    return true;
}

static void Fb_ship_wall(const fb_ctx_t *cx, fb_state_t *s, double px, double py)
{
    double nx, ny, vn, vt, tx, ty, b = options.playerWallBounceBrakeFactor, ch, sp;
    int i = Fb_fine_index(cx->ox + s->x, cx->oy + s->y);

    if (i >= 0 && ShipClear[i] >= cx->wall_r)
	return;
    sp = LENGTH(s->vx, s->vy);
    if (i < 0 || sp > cx->max_bounce) {
	s->crashed = true;
	return;
    }
    if (!Fb_wall_normal(cx->ox + s->x, cx->oy + s->y, &nx, &ny))
	return;
    vn = s->vx * nx + s->vy * ny;
    if (vn >= 0.0)
	return;
    tx = -ny;
    ty = nx;
    vt = s->vx * tx + s->vy * ty;
    switch (options.playerWallBounceType) {
    case 0:
	vt *= b;
	break;
    case 1:
	vt *= MAX(1.0 - options.playerWallFriction, 0.0);
	break;
    case 2:
	vt *= MAX(1.0 - fabs(vn) / MAX(LENGTH(vn, vt), 1e-3)
		  * options.playerWallFriction, 0.0);
	break;
    default:
	ch = MIN(options.playerWallFriction * fabs(vn) * (1.0 + b),
		 (1.0 - b) * fabs(vt));
	vt += vt > 0.0 ? -ch : ch;
	break;
    }
    vn = -vn * b;
    s->vx = vn * nx + vt * tx;
    s->vy = vn * ny + vt * ty;
    s->x = px;
    s->y = py;
    s->hits++;
}

static inline void Fb_gravity(double x, double y, double *gx, double *gy)
{
    int bx = (int)(x / BLOCK_SZ), by = (int)(y / BLOCK_SZ);

    if (FbWrap) {
	bx = (bx % world->x + world->x) % world->x;
	by = (by % world->y + world->y) % world->y;
    }
    LIMIT(bx, 0, world->x - 1);
    LIMIT(by, 0, world->y - 1);
    *gx = world->gravity[bx][by].x;
    *gy = world->gravity[bx][by].y;
}

/*
 * The way of a shot from (x, y) (relative to the ship at ox, oy) for the
 * next ticks: straight, reflected by walls when such objects bounce,
 * ending when it hits one otherwise or its life runs out.
 */
static void Fb_shot_path(fb_shot_t *sh, double ox, double oy, double x, double y,
			 double vx, double vy, double life, bool bounce)
{
    int t, k;

    sh->n = 0;
    sh->px[sh->n] = (float)x;
    sh->py[sh->n] = (float)y;
    sh->n++;
    for (t = 1; t <= FB_NEAR_H && life > 0.0; t++) {
	for (k = 0; k < 2; k++) {
	    double nx, ny, wx, wy, vn;

	    if (FbShotGrav) {
		double gx, gy;

		Fb_gravity(ox + x, oy + y, &gx, &gy);
		vx += gx * 0.5;
		vy += gy * 0.5;
	    }
	    nx = x + vx * 0.5;
	    ny = y + vy * 0.5;

	    if (Fb_ship_clear(ox + nx, oy + ny) >= 2.0) {
		x = nx;
		y = ny;
		continue;
	    }
	    if (!bounce || !Fb_wall_normal(ox + x, oy + y, &wx, &wy)) {
		life = 0.0;
		break;
	    }
	    vn = vx * wx + vy * wy;
	    if (vn < 0.0) {
		vx = (vx - 2.0 * vn * wx) * options.objectWallBounceBrakeFactor;
		vy = (vy - 2.0 * vn * wy) * options.objectWallBounceBrakeFactor;
		life *= options.objectWallBounceLifeFactor;
	    }
	}
	if (life <= 0.0)
	    break;
	life -= 1.0;
	sh->px[sh->n] = (float)x;
	sh->py[sh->n] = (float)y;
	sh->n++;
    }
}

static void Fb_sim_tick(const fb_ctx_t *cx, fb_state_t *s, bool thrust)
{
    double tax = 0.0, tay = 0.0;
    int f;

    if (thrust) {
	tax = cx->acc * tcos(s->dir);
	tay = cx->acc * tsin(s->dir);
    }
    for (f = 0; f < cx->frames; f++) {
	double px = s->x, py = s->y, ax = tax, ay = tay;

	if (cx->grav) {
	    double gx, gy;

	    Fb_gravity(cx->ox + s->x, cx->oy + s->y, &gx, &gy);
	    ax += gx;
	    ay += gy;
	}
	s->vx += ax * cx->dt * 0.5;
	s->vy += ay * cx->dt * 0.5;
	s->vx *= cx->fric_keep;
	s->vy *= cx->fric_keep;
	s->x += (s->vx + options.constantSpeed * tax) * cx->dt;
	s->y += (s->vy + options.constantSpeed * tay) * cx->dt;
	s->vx += ax * cx->dt * 0.5;
	s->vy += ay * cx->dt * 0.5;
	Fb_ship_wall(cx, s, px, py);
	if (s->crashed)
	    return;
    }
}

static double Fb_ray_free(const fb_ctx_t *cx, double x, double y,
			  double ux, double uy, double need, double max)
{
    double t;

    for (t = 0.0; t < max; t += 2.0 * FineStep) {
	if (Fb_ship_clear(cx->ox + x + ux * t, cx->oy + y + uy * t) < need)
	    return t;
    }
    return max;
}

static inline double Fb_cpa2(double ax0, double ay0, double ax1, double ay1,
			     double bx0, double by0, double bx1, double by1)
{
    double rx = bx0 - ax0, ry = by0 - ay0;
    double wx = (bx1 - bx0) - (ax1 - ax0), wy = (by1 - by0) - (ay1 - ay0);
    double ww = wx * wx + wy * wy, t = 0.0;

    if (ww > 1e-9) {
	t = -(rx * wx + ry * wy) / ww;
	LIMIT(t, 0.0, 1.0);
    }
    rx += wx * t;
    ry += wy * t;
    return rx * rx + ry * ry;
}

/*
 * Lead direction for a shot of speed s at a target at (rx, ry) moving at
 * (wvx, wvy) relative to us (the shot inherits our velocity).
 */
static bool Fb_lead(double rx, double ry, double wvx, double wvy, double s,
		    double *dir, double *tof)
{
    double a = wvx * wvx + wvy * wvy - s * s;
    double b = 2.0 * (rx * wvx + ry * wvy);
    double c = rx * rx + ry * ry, t;

    if (ABS(a) < 1e-6)
	t = b < 0.0 ? -c / b : -1.0;
    else {
	double disc = b * b - 4.0 * a * c, t1, t2;

	if (disc < 0.0)
	    return false;
	disc = sqrt(disc);
	t1 = (-b - disc) / (2.0 * a);
	t2 = (-b + disc) / (2.0 * a);
	t = (t1 > 0.0 && (t1 < t2 || t2 <= 0.0)) ? t1 : t2;
    }
    if (t <= 0.0)
	return false;
    *dir = findDir(rx + wvx * t, ry + wvy * t);
    *tof = t;
    return true;
}

/* Target position relative to us at time t, its acceleration lasting 8 ticks. */
static inline void Fb_target_at(double rx, double ry, double wvx, double wvy,
				double ax, double ay, double t, double *x, double *y)
{
    double tc = MIN(t, 8.0), f = 0.5 * tc * tc + tc * (t - tc);

    *x = rx + wvx * t + ax * f;
    *y = ry + wvy * t + ay * f;
}

/*
 * How close a shot fired now in 'dir' from the gun at (gx, gy) passes the
 * target (relative position, velocity relative to us, acceleration).
 */
static double Fb_shot_miss(int dir, double gx, double gy, double rx, double ry,
			   double wvx, double wvy, double ax, double ay,
			   double speed, double life, double *when)
{
    double ux = speed * tcos(dir), uy = speed * tsin(dir), best = 1e9, bt = -1.0;
    double sgx = 0.0, sgy = 0.0;

    /* the shot falls: the target's measured acceleration has that too */
    if (FbShotGrav && (ax != 0.0 || ay != 0.0)) {
	sgx = FbGravX;
	sgy = FbGravY;
    }
    int t;

    for (t = 1; t <= (int)life; t++) {
	double px, py, d2;

	Fb_target_at(rx, ry, wvx, wvy, ax, ay, t, &px, &py);
	d2 = sqr(px - gx - ux * t - 0.5 * sgx * t * t) + sqr(py - gy - uy * t - 0.5 * sgy * t * t);
	if (d2 < best) {
	    best = d2;
	    bt = t;
	} else if (d2 > best * 4.0 + 1e4)
	    break;
    }
    if (bt > 0.0) {
	/* refine between the ticks */
	double t0 = MAX(bt - 1.0, 0.0), t1 = MIN(bt + 1.0, life), k;

	for (k = t0; k <= t1; k += 0.25) {
	    double px, py, d2;

	    Fb_target_at(rx, ry, wvx, wvy, ax, ay, k, &px, &py);
	    d2 = sqr(px - gx - ux * k - 0.5 * sgx * k * k) + sqr(py - gy - uy * k - 0.5 * sgy * k * k);
	    if (d2 < best) {
		best = d2;
		bt = k;
	    }
	}
    }
    if (when)
	*when = bt;
    return sqrt(best);
}

/* ------------------------------------------------------------------------
 * The sampling flight controller
 * ------------------------------------------------------------------------ */

/*
 * The policy after a candidate's own actions: in a fight, hold the target's
 * velocity plus closing to the fighting distance, else point at the lead;
 * otherwise fly at the carrot.
 */
static void Fb_policy(const fb_ctx_t *cx, const fb_state_t *s, double trx, double try_,
		      int *want, bool *thrust)
{
    double ax, ay, amag;

    if (cx->has_target && cx->engage && !cx->run) {
	double rx = trx - s->x, ry = try_ - s->y, dist = LENGTH(rx, ry);
	double close, vdx, vdy, ldir, tof;

	close = (dist - cx->d_pref) * 0.06;
	LIMIT(close, -0.5 * cx->vmax, cx->vmax);
	vdx = cx->tvx + rx / MAX(dist, 1.0) * close;
	vdy = cx->tvy + ry / MAX(dist, 1.0) * close;
	ax = vdx - s->vx;
	ay = vdy - s->vy;
	amag = LENGTH(ax, ay);
	if (amag > 4.0 * cx->acc) {
	    *want = Fb_dir_of(ax, ay);
	    *thrust = ABS(Fb_angle_diff(*want, s->dir)) <= 10;
	    return;
	}
	if (Fb_lead(rx, ry, cx->tvx - s->vx, cx->tvy - s->vy, cx->shot_speed, &ldir, &tof))
	    *want = MOD2((int)(ldir + 0.5), RES);
	else
	    *want = Fb_dir_of(rx, ry);
	*thrust = false;
	return;
    }
    if (cx->have_car || cx->point) {
	double carx = cx->point ? cx->gx : cx->carx, cary = cx->point ? cx->gy : cx->cary;
	double dx = carx - s->x, dy = cary - s->y, dist = LENGTH(dx, dy), vdes;

	if (cx->point)
	    vdes = MIN(cx->vmax, sqrt(cx->acc * MAX(dist - cx->arrive_r * 0.5, 0.0)));
	else
	    vdes = MIN(cx->vmax, sqrt(36.0 + cx->acc * dist));
	if (dist > 1.0) {
	    ax = dx / dist * vdes - s->vx;
	    ay = dy / dist * vdes - s->vy;
	} else {
	    ax = -s->vx;
	    ay = -s->vy;
	}
	amag = LENGTH(ax, ay);
	if (amag < 0.3 * cx->acc) {
	    *want = s->dir;
	    *thrust = false;
	    return;
	}
	*want = Fb_dir_of(ax, ay);
	*thrust = ABS(Fb_angle_diff(*want, s->dir)) <= 10;
	return;
    }
    *want = s->dir;
    *thrust = false;
}

/* Simulate a candidate and return its cost; records the actions taken. */
static double Fb_rollout(const fb_ctx_t *cx, const fb_cand_t *cand, fb_cand_t *rec,
			 fb_flags_t *fl)
{
    fb_state_t s = cx->s0;
    double cost = 0.0, need = cx->ship_r + cx->margin, gmin = 1e9;
    int t, i, hits = 0;

    need = MIN(need, cx->clear0 - 1.0);
    if (fl)
	fl->shot_t = fl->ram_t = -1;

    for (t = 0; t < FB_H; t++) {
	int want;
	bool thr;
	double clear, px0 = s.x, py0 = s.y, tt = t + 1.0, trx = 0.0, try_ = 0.0;

	if (cx->has_target)
	    Fb_target_at(cx->tx, cx->ty, cx->tvx, cx->tvy, cx->tax, cx->tay, tt, &trx, &try_);
	if (t < cand->len) {
	    want = cand->dir[t];
	    thr = cand->thr[t];
	} else
	    Fb_policy(cx, &s, trx, try_, &want, &thr);
	s.dir = Fb_turn_toward(s.dir, want, cx->turn);
	rec->dir[t] = (short)s.dir;
	rec->thr[t] = thr;
	Fb_sim_tick(cx, &s, thr);
	if (thr)
	    cost += cx->thrust_cost;
	/* a margin below the deadly wall speed: blasts and bounces add some */
	{
	    double v2 = s.vx * s.vx + s.vy * s.vy;

	    if (v2 > sqr(cx->vlim))
		cost += sqr(sqrt(v2) - cx->vlim) * 3.0;
	}

	if (s.crashed
	    || Fb_ship_clear(cx->ox + 0.5 * (s.x + px0), cx->oy + 0.5 * (s.y + py0)) < 2.0) {
	    cost += 1e6 * (1.0 + (double)(FB_H - t) / FB_H);
	    return cost;
	}
	if (s.hits > hits) {
	    cost += FB_BOUNCE_COST * (s.hits - hits);
	    hits = s.hits;
	}
	/* wormholes we do not mean to take throw us anywhere */
	for (i = 0; i < NumWorm; i++) {
	    if (WormIn[i] && i != cx->worm_ok
		&& sqr(Fb_wdx(cx->ox + s.x - WormX[i])) + sqr(Fb_wdy(cx->oy + s.y - WormY[i]))
		   < sqr((double)WORMHOLE_RADIUS / CLICK + cx->ship_r + 6.0)) {
		cost += 4000.0 * (1.0 - 0.5 * t / FB_H);
		break;
	    }
	}
	clear = MAX(Fb_ship_clear(cx->ox + s.x, cx->oy + s.y), cx->ship_r);
	if (clear < need + 8.0)
	    cost += sqr(need + 8.0 - clear) * 0.5;

	if (cx->through)
	    gmin = MIN(gmin, LENGTH(cx->gx - s.x, cx->gy - s.y) + t * 2.0);
	if (cx->run_w > 0.0 && cx->field)
	    cost += cx->run_w * MAX(0.0, Fb_field_value(cx->field, cx->ox + s.x, cx->oy + s.y)
				    - cx->goal_slack);

	/* the fight: on the lead early, at the fighting distance */
	if (cx->has_target && cx->engage) {
	    double rx = trx - s.x, ry = try_ - s.y, dist = LENGTH(rx, ry), e;

	    if (t < FB_AIM_H) {
		double ldir, tof;
		int lead, err;

		if (Fb_lead(rx, ry, cx->tvx - s.vx, cx->tvy - s.vy, cx->shot_speed, &ldir, &tof)
		    && tof < cx->shot_life)
		    lead = MOD2((int)(ldir + 0.5), RES);
		else {
		    lead = Fb_dir_of(rx, ry);
		    tof = 1e9;
		}
		err = ABS(Fb_angle_diff(s.dir, lead));
		/* pointing at the lead soon is worth most, being on it more */
		cost += cx->aim_w * (t < 4 ? 10.0 : 3.0) * MIN(err, 32);
		if (t < 4 && tof < cx->shot_life
		    && err <= (FB_SHIP_R + 2.0) / MAX(dist, 1.0) * RES / (2.0 * PI))
		    cost -= cx->aim_w * 350.0 * (4 - t);
	    }
	    e = (dist - cx->d_pref) / cx->d_pref;
	    if (e < 0.0)
		e = 0.0;	/* close is fine: backing off means turning away */
	    cost += cx->band_w * e * e;
	    if (dist < cx->d_min)
		cost += (cx->d_min - dist) * 6.0;
	}

	if (t >= FB_NEAR_H)
	    continue;
	/* shots, checked along the whole tick */
	for (i = 0; i < cx->nshots; i++) {
	    const fb_shot_t *sh = &cx->shots[i];
	    int k = t - (int)sh->t0;

	    if (k < 0 || k + 1 >= sh->n || !(sh->reach & (1u << k)))
		continue;
	    if (Fb_cpa2(px0, py0, s.x, s.y, sh->px[k], sh->py[k], sh->px[k + 1], sh->py[k + 1])
		< sqr(sh->r + cx->margin)) {
		cost += sh->weight * (1.0 - 0.5 * t / FB_NEAR_H);
		if (fl && fl->shot_t < 0 && sh->t0 == 0.0 && sh->weight >= FB_SHOT_COST)
		    fl->shot_t = t;
	    }
	}
	/* ships: collisions kill or bounce; the farther ahead, the wider the berth */
	for (i = 0; i < cx->nen; i++) {
	    const fb_enemy_t *e = &cx->en[i];
	    double ex = e->x + e->vx * tt, ey = e->y + e->vy * tt;
	    double d2 = Fb_cpa2(px0, py0, s.x, s.y, ex - e->vx, ey - e->vy, ex, ey);
	    double hit = cx->ship_r + FB_SHIP_R + 6.0;
	    double may = hit + MIN(0.5 * cx->acc * tt * tt, 20.0);

	    if (d2 < sqr(hit)) {
		/* farther ahead he swerves too: only near collisions are sure */
		cost += cx->ram_cost * (1.0 - 0.5 * t / FB_NEAR_H) * (t >= 3 ? 0.02 : 1.0);
		if (fl && fl->ram_t < 0)
		    fl->ram_t = t;
	    } else if (d2 < sqr(may))
		cost += MIN(cx->ram_cost * 0.15, 500.0) * (1.0 - 0.5 * t / FB_NEAR_H);
	}
    }

    /* terminal cost */
    {
	double v = LENGTH(s.vx, s.vy), dneed, free;

	if (cx->field)
	    cost += MAX(0.0, Fb_field_value(cx->field, cx->ox + s.x, cx->oy + s.y)
			- cx->goal_slack);
	else if (cx->point && cx->through)
	    cost += gmin;	/* passing through it is enough (sooner is better) */
	else if (cx->point) {
	    double gd = LENGTH(cx->gx - s.x, cx->gy - s.y);

	    cost += MAX(0.0, gd - cx->arrive_r);
	    if (gd < cx->arrive_r * 2.0 + 60.0)
		cost += (s.vx * s.vx + s.vy * s.vy) * 4.0;
	}
	if (cx->has_target && cx->engage) {
	    double trx, try_, dist, e;

	    Fb_target_at(cx->tx, cx->ty, cx->tvx, cx->tvy, cx->tax, cx->tay, FB_H,
			 &trx, &try_);
	    dist = LENGTH(trx - s.x, try_ - s.y);
	    e = (dist - cx->d_pref) / cx->d_pref;
	    if (e < 0.0)
		e = 0.0;	/* close is fine: backing off means turning away */
	    cost += cx->band_w * 4.0 * e * e;
	}
	/* still able to stop (or turn) before the next wall? */
	if (v > 2.0) {
	    dneed = v * v / (2.0 * cx->acc) + v * (RES / 2.0) / cx->turn * 0.5;
	    free = Fb_ray_free(cx, s.x, s.y, s.vx / v, s.vy / v, need + 2.0, dneed + 30.0);
	    if (free < dneed)
		cost += (dneed - free) * (v > cx->max_bounce * 0.8 ? 3.0 : 0.6);
	}
    }
    return cost;
}

static void Fb_cand_set(fb_cand_t *c, int dir, bool thr, int len)
{
    int i;

    c->len = len;
    for (i = 0; i < len; i++) {
	c->dir[i] = (short)dir;
	c->thr[i] = thr;
    }
}

/* Physics, enemy ships and virtual shots for this tick's planning. */
static void Fb_ctx_init(player_t *pl, fb_data_t *d, int level, fb_ctx_t *cx)
{
    double f = level / 100.0;
    int i;

    memset(&cx->s0, 0, sizeof(cx->s0));
    cx->pl = pl;
    cx->d = d;
    cx->ox = Fb_px(pl->pos.cx);
    cx->oy = Fb_px(pl->pos.cy);
    cx->s0.vx = pl->vel.x;
    cx->s0.vy = pl->vel.y;
    cx->s0.dir = pl->dir;
    cx->acc = Fb_thrust_acc(pl);
    cx->ship_r = Fb_ship_radius(pl);
    cx->turn = Fb_turn_rate(level);
    cx->frames = MAX(1, MIN((int)(1.0 / timeStep + 0.5), 2));
    cx->dt = 1.0 / cx->frames;
    cx->fric_keep = pow(MAX(1.0 - options.frictionSetting, 0.0), cx->dt);
    cx->grav = world->gravity != NULL && BIT(pl->obj_status, GRAVITY);
    cx->margin = 1.0 + (1.0 - f) * 6.0;
    cx->clear0 = Fb_ship_clear(cx->ox, cx->oy);
    /* here and now the ship fits: a narrow way fits it nose first */
    cx->wall_r = LENGTH(pl->vel.x, pl->vel.y) < 6.0
	? MAX(MIN(cx->ship_r, cx->clear0 - 0.5), 7.0) : cx->ship_r;
    cx->max_bounce = options.maxUnshieldedWallBounceSpeed;
    if (BIT(pl->used, HAS_SHIELD) && pl->fuel.sum > 20.0)
	cx->max_bounce = MAX(cx->max_bounce, options.maxShieldedWallBounceSpeed);
    cx->max_bounce *= 0.92;
    if (BIT(world->rules->mode, CRASH_WITH_PLAYER))
	cx->ram_cost = 5e5;
    else if (BIT(world->rules->mode, BOUNCE_WITH_PLAYER))
	cx->ram_cost = 300.0 + options.playerCollisionFuelDrain * 30.0;	/* only a bump */
    else
	cx->ram_cost = 0.0;
    cx->field = NULL;
    cx->point = false;
    cx->have_car = false;
    cx->has_target = false;
    cx->engage = false;
    cx->run = false;
    cx->through = false;
    cx->run_w = 0.0;
    cx->goal_slack = 0.0;
    cx->vmax = 16.0;
    cx->shot_speed = options.shotSpeed;
    cx->shot_life = options.shotLife;
    cx->nen = 0;

    Fb_scan_objects(pl, d, level, cx);

    /* enemy ships close by, as perceived */
    for (i = 0; i < NumPlayers && cx->nen < FB_MAX_ENEMIES; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	double rx, ry;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->known || (!c->seen && FbTick - c->last_seen > 2))
	    continue;
	rx = Fb_wdx(c->x - cx->ox);
	ry = Fb_wdy(c->y - cx->oy);
	if (LENGTH(rx, ry) > 900.0)
	    continue;
	cx->en[cx->nen].x = rx;
	cx->en[cx->nen].y = ry;
	cx->en[cx->nen].vx = c->vx;
	cx->en[cx->nen].vy = c->vy;
	cx->nen++;
    }
    /* an enemy pointing our way may fire any moment: leave his line */
    for (i = 0; i < NumPlayers && cx->nshots < FB_MAX_SHOTS - 3; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	double rx, ry, dist, w;
	int k;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->seen || c->shield)
	    continue;
	rx = Fb_wdx(c->x - cx->ox);
	ry = Fb_wdy(c->y - cx->oy);
	dist = LENGTH(rx, ry);
	if (dist > Fb_shot_range() * 1.1
	    || ABS(Fb_angle_diff(c->dir, Fb_dir_of(-rx, -ry))) > 12)
	    continue;
	w = FB_VSHOT_COST * Fb_opp_respect(e);
	for (k = 0; k < 3; k++) {
	    fb_shot_t *sh = &cx->shots[cx->nshots++];

	    sh->t0 = 2.0 * k;
	    sh->weight = w;
	    sh->r = FB_SHIP_R + 4.0;
	    Fb_shot_path(sh, cx->ox, cx->oy,
			 rx + c->vx * sh->t0 + FB_GUN_OFF * tcos(c->dir),
			 ry + c->vy * sh->t0 + FB_GUN_OFF * tsin(c->dir),
			 c->vx + options.shotSpeed * tcos(c->dir),
			 c->vy + options.shotSpeed * tsin(c->dir),
			 options.shotLife, options.shotsWallBounce);
	}
    }
}

/*
 * Choose this tick's heading and thrust: sample manoeuvres, simulate them
 * and keep the best.
 */
static fb_flags_t Fb_fly(player_t *pl, fb_data_t *d, int level, fb_ctx_t *cx)
{
    static fb_cand_t cand[FB_MAX_CAND];
    fb_cand_t rec, best_rec;
    fb_flags_t fl = { false, 0, false, -1, -1 };
    double best_cost = 1e30, u = Fb_unskill(level);
    int nc = 0, i, h, nheads, prev_i = -1;
    int specials[12], nspec = 0;

    /*
     * Which shots can meet the ship at all in which tick, whatever it
     * does: the ship gets no farther than its speed and thrust allow.
     */
    for (i = 0; i < cx->nshots; i++) {
	fb_shot_t *sh = &cx->shots[i];
	double v0 = LENGTH(cx->s0.vx, cx->s0.vy);
	int k;

	sh->reach = 0;
	for (k = 0; k + 1 < sh->n && k < 31; k++) {
	    double t = k + sh->t0 + 1.0;
	    double R = v0 * t + 0.5 * cx->acc * t * t + sh->r + cx->margin + 4.0;
	    double ax = sh->px[k], ay = sh->py[k], bx = sh->px[k + 1], by = sh->py[k + 1];
	    double dmin = MIN(sqrt(ax * ax + ay * ay), sqrt(bx * bx + by * by))
		- 0.5 * LENGTH(bx - ax, by - ay);

	    if (dmin < R)
		sh->reach |= 1u << k;
	}
    }

    nheads = level >= 50 ? 16 : 12;
    /*
     * Two ticks in three, with no shot close, the last plan only gets
     * refined: it was the best a tick ago.
     */
    if (d->plan_len > 1 && (FbTick + (unsigned long)pl->id) % 3 != 0) {
	bool near = false;

	for (i = 0; i < cx->nshots && !near; i++) {
	    const fb_shot_t *sh = &cx->shots[i];
	    int k;

	    for (k = 0; k < sh->n && k < 10; k += 3)
		if (sqr(sh->px[k]) + sqr(sh->py[k]) < sqr(300.0))
		    near = true;
	}
	if (!near)
	    nheads = 0;
    }
    specials[nspec++] = pl->dir;
    if (LENGTH(pl->vel.x, pl->vel.y) > 1.0)
	specials[nspec++] = Fb_dir_of(-pl->vel.x, -pl->vel.y);
    if (cx->have_car || cx->point) {
	double rx = cx->point ? cx->gx : cx->carx, ry = cx->point ? cx->gy : cx->cary;
	int dc = Fb_dir_of(rx, ry);

	specials[nspec++] = dc;
	specials[nspec++] = MOD2(dc + RES / 32, RES);
	specials[nspec++] = MOD2(dc - RES / 32, RES);
    }
    if (cx->has_target) {
	double ldir, tof;
	int lead;

	if (Fb_lead(cx->tx, cx->ty, cx->tvx - pl->vel.x, cx->tvy - pl->vel.y,
		    cx->shot_speed, &ldir, &tof))
	    lead = MOD2((int)(ldir + 0.5), RES);
	else
	    lead = Fb_dir_of(cx->tx, cx->ty);
	specials[nspec++] = lead;
	specials[nspec++] = MOD2(lead + 2, RES);
	specials[nspec++] = MOD2(lead - 2, RES);
	/* side steps across the target's line of fire */
	specials[nspec++] = MOD2(Fb_dir_of(cx->tx, cx->ty) + RES / 4, RES);
	specials[nspec++] = MOD2(Fb_dir_of(cx->tx, cx->ty) - RES / 4, RES);
    }
    for (h = 0; h < nheads + nspec; h++) {
	int dir = h < nheads ? (h * RES) / nheads : specials[h - nheads];

	if (nc + 6 > FB_MAX_CAND - 2)
	    break;
	Fb_cand_set(&cand[nc++], dir, true, 2);
	Fb_cand_set(&cand[nc++], dir, true, 6);
	if (h >= nheads || (h & 1) == 0)
	    Fb_cand_set(&cand[nc++], dir, false, 3);
	if (h >= nheads) {
	    Fb_cand_set(&cand[nc++], dir, false, 8);
	    Fb_cand_set(&cand[nc++], dir, true, 12);
	}
    }
    /* pure policy */
    cand[nc++].len = 0;
    /* last plan, shifted by one tick */
    if (d->plan_len > 1) {
	cand[nc].len = d->plan_len - 1;
	for (i = 1; i < d->plan_len; i++) {
	    cand[nc].dir[i - 1] = d->plan_dir[i];
	    cand[nc].thr[i - 1] = d->plan_thr[i];
	}
	prev_i = nc;
	nc++;
    }

    for (i = 0; i < nc; i++) {
	double c = Fb_rollout(cx, &cand[i], &rec, NULL);

	/* lower levels misjudge: now and then a worse plan looks best */
	c += rfrac() * FB_PLAN_NOISE * u;
	if (i == prev_i)
	    c -= 20.0;
	if (c < best_cost) {
	    best_cost = c;
	    best_rec = rec;
	}
    }
    if (best_cost >= 1e30)
	return fl;

    best_rec.len = FB_H;
    Fb_rollout(cx, &best_rec, &rec, &fl);
    d->plan_len = FB_H;
    memcpy(d->plan_dir, best_rec.dir, sizeof(short) * FB_H);
    memcpy(d->plan_thr, best_rec.thr, sizeof(bool) * FB_H);
    fl.valid = true;
    fl.dir = best_rec.dir[0];
    fl.thrust = best_rec.thr[0];
    return fl;
}

/* Free way from (x, y) in direction dir (absolute px), up to max. */
static double Fb_free_ray(double x, double y, int dir, double need, double max)
{
    double t;

    for (t = FineStep; t < max; t += FineStep) {
	if (Fb_ship_clear(x + t * tcos(dir), y + t * tsin(dir)) < need)
	    return t;
    }
    return max;
}

static void Fb_steer(player_t *pl, int want, bool thrust)
{
    if (want != pl->dir) {
	pl->turnspeed = 0;
	pl->turnresistance = 0;
	pl->turnacc = 0;
	pl->turnvel = 0;
	Player_set_float_dir(pl, (double)want);
	pl->wanted_float_dir = pl->float_dir;
	Turn_player(pl, true);
    }
    Thrust(pl, thrust);
}

/* ------------------------------------------------------------------------
 * Tactics
 * ------------------------------------------------------------------------ */

static void Fb_set_mode(fb_data_t *d, int mode)
{
    if (d->mode != mode) {
	d->mode = mode;
	d->mode_since = FbTick;
    }
}

static player_t *Fb_target(fb_data_t *d)
{
    player_t *e;

    if (!Fb_id_ok(d->target_id) || d->target_id == NO_ID)
	return NULL;
    e = Player_by_id(d->target_id);
    if (e == NULL || !Fb_player_active(e))
	return NULL;
    return e;
}

/* The fighting distance against this enemy (px). */
static double Fb_fight_dist(fb_data_t *d, player_t *e)
{
    /* long shots are easy to dodge: the distance does not grow with the range */
    double dp = d->gene[G_DIST] * MIN(Fb_shot_range(), 700.0);

    if (e != NULL)
	dp *= 0.9 + 0.1 * Fb_opp_respect(e);
    LIMIT(dp, 110.0, 700.0);
    return dp;
}

/*
 * Pick the enemy to go for: the war target only, if there is one;
 * otherwise the closest known enemy, attackers and the current target
 * first.  Lower levels reconsider less often.
 */
static void Fb_pick_target(player_t *pl, fb_data_t *d, int level)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), best = 1e30;
    int i, every = 3 + (int)(8.0 * Fb_unskill(level)), best_id = NO_ID;

    if (d->war_id != NO_ID) {
	player_t *w = Fb_id_ok(d->war_id) ? Player_by_id(d->war_id) : NULL;

	if (w != NULL && !Fb_ally(pl, w)) {
	    d->target_id = d->war_id;
	    return;
	}
	if (w == NULL)
	    d->war_id = NO_ID;	/* he left */
    }
    if (FbTick - d->last_pick < (unsigned long)every && Fb_target(d) != NULL
	&& d->c[d->target_id].known)
	return;
    d->last_pick = FbTick;
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	double dist, cost;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->known)
	    continue;
	dist = Fb_dist(ox, oy, c->x, c->y);
	cost = dist;
	if (!Fb_line_free(ox, oy, c->x, c->y))
	    cost *= 1.5;
	if (c->seen)
	    cost *= 0.85;
	if (FbTick - d->threat_time[e->id] < FB_THREAT_TICKS)
	    cost *= 0.55;
	if (e->id == d->target_id)
	    cost *= 0.7;
	if (c->seen && c->shield)
	    cost *= 1.6;	/* fresh spawn shield: come back later */
	if (FbTick - c->last_known > 12)
	    cost += 300.0;
	if (cost < best) {
	    best = cost;
	    best_id = e->id;
	}
    }
    if (best_id != d->target_id && best_id != NO_ID)
	FB_LOG("[fightbot] %s targets %s\n", pl->name, Player_by_id(best_id)->name);
    d->target_id = best_id;
}

/* Somewhere to look for enemies: where one was last seen, else bases and stations. */
static void Fb_pick_search(player_t *pl, fb_data_t *d)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy);
    int tries;

    /* getting unstuck: a fuel station surely reachable from here */
    if (FbTick < d->unstick_until && NumStationF > 0) {
	for (tries = 0; tries < 12; tries++) {
	    int k = (int)(rfrac() * NumStationF);
	    fuel_t *fs = Fuel_by_index(k);
	    double sx = Fb_px(fs->pos.cx), sy = Fb_px(fs->pos.cy);

	    if (!StationF[k].valid || Fb_field_value(&StationF[k], ox, oy) >= 1e5
		|| Fb_dist(ox, oy, sx, sy) < 400.0)
		continue;
	    d->search_x = sx;
	    d->search_y = sy;
	    d->search_valid = true;
	    d->search_time = FbTick;
	    return;
	}
    }
    if (FbTick >= d->unstick_until && Fb_id_ok(d->target_id) && d->target_id != NO_ID
	&& d->c[d->target_id].last_known > 0
	&& FbTick - d->c[d->target_id].last_known < 12 * 12
	&& Fb_dist(ox, oy, d->c[d->target_id].x, d->c[d->target_id].y) > 250.0
	&& !d->search_valid) {
	d->search_x = d->c[d->target_id].x;
	d->search_y = d->c[d->target_id].y;
	d->search_valid = true;
	d->search_time = FbTick;
	return;
    }
    for (tries = 0; tries < 16; tries++) {
	double x, y;
	int nb = Num_bases(), nf = Num_fuels(), k;

	if (nb + nf > 0 && rfrac() < 0.7) {
	    k = (int)(rfrac() * (nb + nf));
	    if (k < nb) {
		x = Fb_px(Base_by_index(k)->pos.cx);
		y = Fb_px(Base_by_index(k)->pos.cy);
	    } else {
		x = Fb_px(Fuel_by_index(k - nb)->pos.cx);
		y = Fb_px(Fuel_by_index(k - nb)->pos.cy);
	    }
	} else {
	    int c = (int)(rfrac() * NavN);

	    if (NavClear[c] < 40.0)
		continue;
	    x = NavRX[c];
	    y = NavRY[c];
	}
	if (Fb_dist(ox, oy, x, y) < 600.0 && tries < 12)
	    continue;
	d->search_x = x;
	d->search_y = y;
	d->search_valid = true;
	d->search_time = FbTick;
	return;
    }
}

/* What an item lying around is worth to this ship now (0: nothing). */
static double Fb_item_value(player_t *pl, int type)
{
    static const double val[NUM_ITEMS] = {
	[ITEM_FUEL] = 0.2, [ITEM_WIDEANGLE] = 1.0, [ITEM_REARSHOT] = 0.6,
	[ITEM_AFTERBURNER] = 0.7, [ITEM_CLOAK] = 0.6, [ITEM_SENSOR] = 0.3,
	[ITEM_TRANSPORTER] = 0.4, [ITEM_TANK] = 0.4, [ITEM_MINE] = 0.5,
	[ITEM_MISSILE] = 0.8, [ITEM_ECM] = 0.6, [ITEM_LASER] = 0.7,
	[ITEM_EMERGENCY_THRUST] = 0.4, [ITEM_TRACTOR_BEAM] = 0.2,
	[ITEM_AUTOPILOT] = 0.0, [ITEM_EMERGENCY_SHIELD] = 0.9,
	[ITEM_DEFLECTOR] = 0.7, [ITEM_HYPERJUMP] = 0.3, [ITEM_PHASING] = 0.6,
	[ITEM_MIRROR] = 0.5, [ITEM_ARMOR] = 0.9
    };
    double v;

    if (type < 0 || type >= NUM_ITEMS)
	return 0.0;
    v = val[type];
    if (type == ITEM_FUEL)
	return pl->fuel.sum < Fb_fuel_ref(pl) * 0.5 ? 1.0 : (pl->fuel.sum < Fb_fuel_ref(pl) * 0.9 ? v : 0.0);
    if (pl->item[type] >= world->items[type].limit)
	return 0.0;
    if ((IsDefensiveItem(type) && CountDefensiveItems(pl) >= options.maxDefensiveItems)
	|| (IsOffensiveItem(type) && CountOffensiveItems(pl) >= options.maxOffensiveItems))
	return 0.0;
    /* the first of a kind helps most */
    if (pl->item[type] > 0)
	v *= 0.6;
    return v;
}

/* The best item in view worth a detour; false if none. */
static bool Fb_pick_item(player_t *pl, fb_data_t *d, double max_dist)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), best = 0.0;
    int i;
    bool found = false;

    if (pl->damaged > 0)
	return false;
    for (i = 0; i < NumObjs; i++) {
	object_t *o = Obj[i];
	double rx, ry, dist, score;

	if (o->type != OBJ_ITEM || Fb_item_rogue(o))
	    continue;
	rx = Fb_wdx(Fb_px(o->pos.cx) - ox);
	ry = Fb_wdy(Fb_px(o->pos.cy) - oy);
	if (!Fb_in_view(rx, ry))
	    continue;
	dist = LENGTH(rx, ry);
	if (dist > max_dist)
	    continue;
	/* only what can be reached straight away; one we failed to get: later */
	if (Fb_segment_clear(ox, oy, ox + rx, oy + ry, FB_SHIP_R * 0.6) < FB_SHIP_R * 0.6)
	    continue;
	if (FbTick < d->item_ban_until
	    && Fb_dist(ox + rx, oy + ry, d->item_ban_x, d->item_ban_y) < 60.0)
	    continue;
	score = Fb_item_value(pl, ITEM_PTR(o)->item_type) * 600.0 - dist;
	if (score > best) {
	    best = score;
	    d->item_x = ox + rx + o->vel.x * 2.0;
	    d->item_y = oy + ry + o->vel.y * 2.0;
	    found = true;
	}
    }
    return found;
}

/* The fuel station with the shortest way there (straight lines as a guess). */
static int Fb_pick_station(player_t *pl)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), best = 1e30;
    int i, bi = -1;

    for (i = 0; i < Num_fuels(); i++) {
	fuel_t *fs = Fuel_by_index(i);
	double dist, c;

	if (fs->fuel < (pl->fuel.sum < 20.0 ? 1.0 : 20.0)
	    || (BIT(world->rules->mode, TEAM_PLAY) && options.teamFuel
		&& fs->team != pl->team))
	    continue;
	dist = Fb_dist(ox, oy, Fb_px(fs->pos.cx), Fb_px(fs->pos.cy));
	if (StationF != NULL && i < NumStationF && StationF[i].valid) {
	    /* the way there; none (walls, wormholes only): not this one */
	    dist = Fb_field_value(&StationF[i], ox, oy);
	    if (dist >= 1e5)
		continue;
	    c = dist;
	} else
	    c = dist * (Fb_line_free(ox, oy, Fb_px(fs->pos.cx), Fb_px(fs->pos.cy)) ? 1.0 : 1.6);
	c -= fs->fuel * 0.1;
	if (c < best) {
	    best = c;
	    bi = i;
	}
    }
    return bi;
}

/*
 * How outgunned the bot is right now: heavy fire, several attackers, an
 * enemy firing much more (wide angle, rear guns), little fuel.
 */
static double Fb_outgunned(player_t *pl, fb_data_t *d, player_t *tgt)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), s = 0.0;
    int i, attackers = 0, eguns = 0;
    int guns = 1 + 2 * pl->item[ITEM_WIDEANGLE] + pl->item[ITEM_REARSHOT];

    s += 0.04 * d->danger;
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->seen || Fb_dist(ox, oy, c->x, c->y) > 800.0)
	    continue;
	if (FbTick - d->threat_time[e->id] < 3 * 12
	    || ABS(Fb_angle_diff(c->dir, Fb_dir_of(Fb_wdx(ox - c->x), Fb_wdy(oy - c->y)))) < 10)
	    attackers++;
	eguns = MAX(eguns, c->guns);
    }
    if (attackers > 1)
	s += 0.8 * (attackers - 1);
    /* his fan of shots is wider than ours */
    if (eguns > guns + 1)
	s += 0.4 * (eguns - guns - 1);
    if (pl->fuel.sum < Fb_fuel_ref(pl) * 0.15)
	s += 0.5;
    if (tgt != NULL)
	s *= sqrt(Fb_opp_respect(tgt));
    return s;
}

/* Decide what to do this tick. */
static void Fb_think(player_t *pl, fb_data_t *d, int level)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), u = Fb_unskill(level);
    player_t *tgt;
    fb_contact_t *c = NULL;
    double dist = 1e9, near_enemy = 1e9, refuel_lo, thr;
    bool war;
    int i;

    Fb_pick_target(pl, d, level);
    tgt = Fb_target(d);
    war = d->war_id != NO_ID && tgt != NULL && tgt->id == d->war_id;
    if (tgt) {
	c = &d->c[tgt->id];
	if (c->known)
	    dist = Fb_dist(ox, oy, c->x, c->y);
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);

	if (e != pl && Fb_id_ok(e->id) && d->c[e->id].seen)
	    near_enemy = MIN(near_enemy, Fb_dist(ox, oy, d->c[e->id].x, d->c[e->id].y));
    }

    /*
     * Standing around is never the plan: not moving for a while without a
     * fight in sight (or refueling), look elsewhere for a few seconds.
     */
    if (LENGTH(pl->vel.x, pl->vel.y) < 1.0 && !Player_is_refueling(pl))
	d->still_ticks++;
    else
	d->still_ticks = 0;
    if (d->still_ticks > 3 * 12
	&& !(tgt && c->seen && Fb_line_free(ox, oy, c->x, c->y))) {
	if (d->mode == FB_ITEM) {
	    d->item_ban_x = d->item_x;
	    d->item_ban_y = d->item_y;
	    d->item_ban_until = FbTick + 15 * 12;
	}
	d->run_phase = 0;
	d->search_valid = false;
	d->unstick_until = FbTick + 5 * 12;
	Fb_pick_search(pl, d);
	d->still_ticks = 0;
	FB_LOG("[fightbot] %s was standing in %s: off to search\n", pl->name,
	       fb_mode_name[d->mode]);
	Fb_set_mode(d, FB_SEARCH);
    }
    if (FbTick < d->unstick_until && d->search_valid
	&& !(tgt && c->seen && Fb_line_free(ox, oy, c->x, c->y))) {
	Fb_set_mode(d, FB_SEARCH);
	return;
    }

    /* fuel: refuel when nobody is close, fight on when someone is */
    refuel_lo = MAX(Fb_fuel_ref(pl) * 0.25, 80.0);
    if (d->mode == FB_REFUEL) {
	/* no way closer in a while: give up on that station */
	if (d->station >= 0 && FbTick - d->mode_since > 15 * 12
	    && Fb_dist(ox, oy, Fb_px(Fuel_by_index(d->station)->pos.cx),
		       Fb_px(Fuel_by_index(d->station)->pos.cy)) > 200.0)
	    d->station = -1;
	if (pl->fuel.sum >= MIN(Fb_fuel_ref(pl) * 0.9, pl->fuel.max - 1.0)
	    || (near_enemy < 600.0 && pl->fuel.sum >= 20.0) || d->station < 0
	    || Fuel_by_index(d->station)->fuel < 5.0)
	    Fb_set_mode(d, FB_HUNT);
	else
	    return;
    } else if (pl->fuel.sum < refuel_lo && (near_enemy > 600.0 || pl->fuel.sum < 20.0)
	       && Num_fuels() > 0) {
	d->station = Fb_pick_station(pl);
	if (d->station >= 0) {
	    Fb_set_mode(d, FB_REFUEL);
	    return;
	}
    }

    /* falling back ends after a while: then attack again */
    if (d->mode == FB_FALLBACK) {
	if (FbTick < d->fallback_until && tgt != NULL && c->known)
	    return;
	d->fallback_cool = FbTick + 3 * 12;
	Fb_set_mode(d, FB_ENGAGE);
    }

    /* an item that takes too long to get: leave it for a while */
    if (d->mode == FB_ITEM && FbTick - d->mode_since > 6 * 12) {
	d->item_ban_x = d->item_x;
	d->item_ban_y = d->item_y;
	d->item_ban_until = FbTick + 15 * 12;
	Fb_set_mode(d, FB_SEARCH);
    }
    /* items in view when no fight is on */
    if (near_enemy > 700.0 && !(war && c && c->known && dist < 900.0)
	&& Fb_pick_item(pl, d, 600.0)) {
	Fb_set_mode(d, FB_ITEM);
	return;
    }

    if (tgt == NULL || !c->known) {
	if (d->mode != FB_SEARCH || !d->search_valid
	    || Fb_dist(ox, oy, d->search_x, d->search_y) < 200.0
	    || FbTick - d->search_time > 20 * 12) {
	    d->search_valid = false;
	    Fb_pick_search(pl, d);
	}
	Fb_set_mode(d, FB_SEARCH);
	return;
    }
    d->search_valid = false;
    if ((c->seen || (c->radar && dist < 900.0))
	&& (dist < 500.0 || Fb_line_free(ox, oy, c->x, c->y))) {
	if (d->mode != FB_ENGAGE && d->mode != FB_RUN)
	    FB_LOG("[fightbot] %s engages %s at %.0f px\n", pl->name, tgt->name, dist);
	/* a new fight: pick the tactic against him */
	if (d->tac_active && (d->tac_opp != tgt->id || FbTick - d->tac_seen > 10 * 12))
	    Fb_tactic_done(d, 0.0);
	if (!d->tac_active) {
	    d->tac = Fb_choose_tactic(tgt);
	    d->tac_opp = tgt->id;
	    d->tac_active = true;
	}
	d->tac_seen = FbTick;
	if (d->tac == FB_TAC_RUN && (d->mode == FB_RUN || dist > 350.0)
	    && Fb_segment_clear(ox, oy, c->x, c->y, FB_SHIP_R) >= FB_SHIP_R) {
	    Fb_set_mode(d, FB_RUN);
	    return;
	}
	d->run_phase = 0;
	Fb_set_mode(d, FB_ENGAGE);
	/* outgunned: fall back for a moment, then come back */
	thr = 0.6 + 1.6 * d->gene[G_AGGR] * (1.0 - 0.5 * u) + (war ? 0.8 : 0.0);
	if (FbTick >= d->fallback_cool && Fb_outgunned(pl, d, tgt) > thr) {
	    d->fallback_until = FbTick
		+ (unsigned long)(d->gene[G_BACK] * (1.0 + 0.8 * u) * (war ? 0.6 : 1.0));
	    Fb_set_mode(d, FB_FALLBACK);
	    FB_LOG("[fightbot] %s falls back from %s\n", pl->name, tgt->name);
	}
	return;
    }
    if (d->mode == FB_RUN && d->run_phase == 2)
	return;		/* extending: he may be out of sight for a moment */
    d->run_phase = 0;
    Fb_set_mode(d, FB_HUNT);
}

/*
 * Attack run: fly past him fast, offset to one side, firing a volley
 * across our way as we pass; then extend and come again.
 */
static void Fb_run_goal(player_t *pl, fb_data_t *d, fb_ctx_t *cx)
{
    double rx = cx->tx, ry = cx->ty, dist = LENGTH(rx, ry), ux, uy, px, py, off = 120.0;

    if (d->run_phase == 2
	&& (dist > 900.0 || FbTick - d->run_since > 40))
	d->run_phase = 0;
    if (d->run_phase == 0) {
	double cr;

	ux = rx / MAX(dist, 1.0);
	uy = ry / MAX(dist, 1.0);
	/* pass on the side we already drift to */
	cr = ux * pl->vel.y - uy * pl->vel.x;
	d->run_side = cr > 0.0 ? 1 : (cr < 0.0 ? -1 : (rfrac() < 0.5 ? -1 : 1));
	d->run_phase = 1;
	d->run_min = dist;
	d->run_since = FbTick;
    }
    if (d->run_phase == 1) {
	double tof = dist / MAX(LENGTH(pl->vel.x - cx->tvx, pl->vel.y - cx->tvy), 8.0);

	tof = MIN(tof, 20.0);
	px = rx + cx->tvx * tof;
	py = ry + cx->tvy * tof;
	ux = px / MAX(LENGTH(px, py), 1.0);
	uy = py / MAX(LENGTH(px, py), 1.0);
	px += -uy * d->run_side * off;
	py += ux * d->run_side * off;
	d->run_ux = ux;
	d->run_uy = uy;
	d->run_gx = cx->ox + px + ux * 700.0;
	d->run_gy = cx->oy + py + uy * 700.0;
	d->run_min = MIN(d->run_min, dist);
	/* passed him: extend */
	if ((d->run_min < 400.0 && dist > d->run_min + 60.0) || FbTick - d->run_since > 150) {
	    d->run_phase = 2;
	    d->run_since = FbTick;
	    {
		double v = LENGTH(pl->vel.x, pl->vel.y);
		double vx = v > 2.0 ? pl->vel.x / v : -rx / MAX(dist, 1.0);
		double vy = v > 2.0 ? pl->vel.y / v : -ry / MAX(dist, 1.0);

		d->run_gx = cx->ox + vx * 900.0;
		d->run_gy = cx->oy + vy * 900.0;
	    }
	}
    }
    cx->run = true;
    cx->engage = true;
    cx->point = true;
    cx->gx = Fb_wdx(d->run_gx - cx->ox);
    cx->gy = Fb_wdy(d->run_gy - cx->oy);
    cx->arrive_r = 0.0;
    cx->vmax = 26.0;
    cx->band_w = 0.0;
    cx->d_min = cx->ship_r * 2.0 + 10.0;
    if (d->run_phase == 2 || dist > 550.0)
	cx->aim_w *= 0.15;
}

/* Fill in goal and combat terms of the planning context. */
static void Fb_set_goal(player_t *pl, fb_data_t *d, int level, fb_ctx_t *cx)
{
    double u = Fb_unskill(level);
    player_t *tgt = Fb_target(d);
    fb_contact_t *c = tgt ? &d->c[tgt->id] : NULL;
    const fb_field_t *fd = NULL;
    double gx = 0.0, gy = 0.0;
    bool goal = false;

    cx->aim_w = 4.0 * d->gene[G_AIM];
    cx->band_w = 20.0;
    cx->vmax = 14.0 + 6.0 * (1.0 - u);
    {
	/* thrust burns fuel: the less there is, the more a tick of it costs */
	double ff = pl->fuel.sum / Fb_fuel_ref(pl), fe = MAX(pl->fuel.sum, 0.0);

	cx->thrust_cost = 40.0 * sqr(1.0 - MIN(ff, 1.0)) + (fe < 600.0 ? (600.0 - fe) * 0.03 : 0.0);
	if (fe < 800.0)
	    cx->vmax *= 0.7 + 0.3 * fe / 800.0;
    }

    if (tgt && c->known && (d->mode == FB_ENGAGE || d->mode == FB_FALLBACK
			    || d->mode == FB_HUNT || d->mode == FB_RUN)) {
	double k = level >= 40 ? Fb_opp_accel_k(tgt) : 0.0;

	cx->has_target = true;
	cx->tx = Fb_wdx(c->x - cx->ox);
	cx->ty = Fb_wdy(c->y - cx->oy);
	cx->tvx = c->vx;
	cx->tvy = c->vy;
	cx->tax = c->seen ? c->ax * k : 0.0;
	cx->tay = c->seen ? c->ay * k : 0.0;
	cx->d_pref = Fb_fight_dist(d, tgt);
	cx->d_min = cx->ship_r * 2.0 + 4.0;	/* the ram cost keeps us off him */
	gx = c->x;
	gy = c->y;
	goal = true;
    }
    switch (d->mode) {
    case FB_ENGAGE:
	if (!goal)
	    break;
	cx->engage = true;
	cx->vmax = 12.0;
	if (!Fb_line_free(cx->ox, cx->oy, gx, gy)) {
	    fd = Fb_goal_field(gx, gy, 24, cx->ox, cx->oy);
	    cx->goal_slack = cx->d_pref;
	}
	break;
    case FB_RUN:
	if (!goal)
	    break;
	Fb_run_goal(pl, d, cx);
	break;
    case FB_FALLBACK:
	if (!goal)
	    break;
	cx->engage = true;
	cx->d_pref *= 2.3;
	cx->aim_w *= 0.4;
	cx->band_w = 30.0;
	cx->vmax = 18.0;
	break;
    case FB_HUNT:
	/* in sight and not far: straight there, no way to work out */
	if (goal && !(Fb_dist(cx->ox, cx->oy, gx, gy) < 1200.0
		      && Fb_segment_clear(cx->ox, cx->oy, gx, gy, FB_SHIP_R) >= FB_SHIP_R)) {
	    fd = Fb_goal_field(gx, gy, 24, cx->ox, cx->oy);
	    cx->goal_slack = cx->has_target ? cx->d_pref * 0.8 : 0.0;
	    cx->run_w = 1.5 / FB_H;
	}
	break;
    case FB_SEARCH:
	if (d->search_valid) {
	    fd = Fb_goal_field(d->search_x, d->search_y, 1000000UL, cx->ox, cx->oy);
	    cx->run_w = 1.5 / FB_H;
	}
	break;
    case FB_REFUEL:
	if (d->station >= 0) {
	    fuel_t *fs = Fuel_by_index(d->station);
	    double sx = Fb_px(fs->pos.cx), sy = Fb_px(fs->pos.cy);

	    if (Fb_dist(cx->ox, cx->oy, sx, sy) < 400.0
		&& Fb_line_free(cx->ox, cx->oy, sx, sy)) {
		cx->point = true;
		cx->gx = Fb_wdx(sx - cx->ox);
		cx->gy = Fb_wdy(sy - cx->oy);
		cx->arrive_r = 40.0;
		cx->vmax = 8.0;
	    } else {
		fd = Fb_station_field(d->station);
		cx->run_w = 1.5 / FB_H;
	    }
	}
	break;
    case FB_ITEM:
	cx->point = true;
	cx->through = true;
	cx->gx = Fb_wdx(d->item_x - cx->ox);
	cx->gy = Fb_wdy(d->item_y - cx->oy);
	cx->arrive_r = 0.0;
	cx->vmax = 12.0;
	break;
    }
    cx->field = fd;
    cx->worm_ok = -1;
    if (fd && !cx->point) {
	double rx, ry;
	int i;

	if (Fb_field_carrot(fd, cx->ox, cx->oy, FB_SHIP_R * 0.5, 12, &rx, &ry)) {
	    cx->carx = Fb_wdx(rx - cx->ox);
	    cx->cary = Fb_wdy(ry - cx->oy);
	    cx->have_car = true;
	    for (i = 0; i < NumWorm; i++)
		if (WormX[i] == rx && WormY[i] == ry)
		    cx->worm_ok = i;
	}
    } else if (!cx->point && cx->has_target && !cx->engage) {
	cx->carx = cx->tx;
	cx->cary = cx->ty;
	cx->have_car = true;
    }
    cx->vlim = options.maxUnshieldedWallBounceSpeed * 0.6;
    /* heat seekers follow the engine: coast */
    if (d->heat_dist < 500.0)
	cx->thrust_cost += 300.0;
    /* lock the target: radar, missiles, ECM reprogramming use it */
    if (tgt && c->known) {
	pl->lock.pl_id = tgt->id;
	SET_BIT(pl->lock.tagged, LOCK_PLAYER);
	pl->lock.distance = Fb_dist(cx->ox, cx->oy, c->x, c->y);
    }
}

/* ------------------------------------------------------------------------
 * Firing
 * ------------------------------------------------------------------------ */

/* Would one of our shots in 'dir' hit a teammate before the target? */
static bool Fb_friendly_in_line(player_t *pl, int dir, double tof)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy);
    int i;

    if (!BIT(world->rules->mode, TEAM_PLAY) || options.teamImmunity)
	return false;
    for (i = 0; i < NumPlayers; i++) {
	player_t *a = Player_by_index(i);
	double rx, ry, w;

	if (a == pl || !Fb_player_active(a) || !Fb_ally(pl, a))
	    continue;
	rx = Fb_wdx(Fb_px(a->pos.cx) - ox);
	ry = Fb_wdy(Fb_px(a->pos.cy) - oy);
	if (Fb_shot_miss(dir, 0.0, 0.0, rx, ry, a->vel.x - pl->vel.x, a->vel.y - pl->vel.y,
			 0.0, 0.0, options.shotSpeed, MIN(tof + 2.0, options.shotLife), &w)
	    < FB_SHIP_R + 8.0)
	    return true;
    }
    return false;
}

/*
 * Fire when a shot of any gun would hit an enemy on his predicted course,
 * with a free line.  At the target also shots covering his dodges, while
 * there are shots to spare.  Returns true if it fires.
 */
static bool Fb_fire(player_t *pl, fb_data_t *d, int level, bool may_drop_shield)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), u = Fb_unskill(level);
    double tol = MAX(FB_SHIP_R + 3.0 - 6.0 * u, 7.0), life = options.shotLife * 0.95;
    int dirs[16], ndirs = 0, i, g, spread = MODS_SPREAD_MAX - Mods_get(pl->mods, ModsSpread);
    bool spare = pl->shots < options.maxPlayerShots / 2;

    if (!BIT(pl->have, HAS_SHOT) || !Can_shoot_normal_shot(pl)
	|| Player_is_phasing(pl) || pl->shots >= options.maxPlayerShots
	|| pl->fuel.sum < 10.0)
	return false;
    FbGravX = FbGravY = 0.0;
    if (FbShotGrav)
	Fb_gravity(ox, oy, &FbGravX, &FbGravY);
    if (BIT(pl->used, HAS_SHIELD) && !may_drop_shield)
	return false;
    if (FbTick - d->aim_time > 10) {
	/* aiming error drifts slowly, like a human hand */
	d->aim_off = (rfrac() - 0.5) * 2.0 * FB_AIM_ERR * u;
	d->aim_time = FbTick;
    }
    {
	int base = MOD2(pl->dir + (int)(d->aim_off + (d->aim_off < 0 ? -0.5 : 0.5)), RES);

	dirs[ndirs++] = base;
	for (i = 0; i < pl->item[ITEM_WIDEANGLE] && ndirs < 12; i++) {
	    dirs[ndirs++] = MOD2(base + (1 + i) * spread, RES);
	    dirs[ndirs++] = MOD2(base - (1 + i) * spread, RES);
	}
	for (i = 0; i < pl->item[ITEM_REARSHOT] && ndirs < 16; i++)
	    dirs[ndirs++] = MOD2(base + RES / 2
				 + ((pl->item[ITEM_REARSHOT] - 1 - 2 * i) * spread) / 2, RES);
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	double rx, ry, wvx, wvy, ax = 0.0, ay = 0.0, k;
	bool is_tgt;

	if (e == pl || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->known || (!c->seen && !c->radar) || c->shield)
	    continue;
	is_tgt = e->id == d->target_id;
	/* at war only the war target, unless someone else is right on us */
	rx = Fb_wdx(c->x - ox);
	ry = Fb_wdy(c->y - oy);
	if (d->war_id != NO_ID && !is_tgt && LENGTH(rx, ry) > 250.0)
	    continue;
	if (LENGTH(rx, ry) > Fb_shot_range() + 200.0)
	    continue;
	wvx = c->vx - pl->vel.x;
	wvy = c->vy - pl->vel.y;
	k = level >= 40 ? Fb_opp_accel_k(e) : 0.0;
	if (c->seen) {
	    ax = c->ax * k;
	    ay = c->ay * k;
	}
	for (g = 0; g < ndirs; g++) {
	    double tof = 0.0, miss, hx, hy, extra = 0.0, gxo, gyo;

	    gxo = FB_GUN_OFF * tcos(dirs[g]);
	    gyo = FB_GUN_OFF * tsin(dirs[g]);
	    miss = Fb_shot_miss(dirs[g], gxo, gyo, rx, ry, wvx, wvy, ax, ay,
				options.shotSpeed, life, &tof);
	    if (tof <= 0.0)
		continue;
	    if (is_tgt && d->mode == FB_RUN && d->run_phase == 1 && LENGTH(rx, ry) < 400.0
		&& pl->shots < options.maxPlayerShots - 1 && !BIT(pl->used, HAS_SHIELD)) {
		/* the volley across our way: he must get through a line of shots */
		extra = 14.0 + 10.0 * d->gene[G_SPREAD];
	    } else if (is_tgt && spare && !BIT(pl->used, HAS_SHIELD)) {
		/* cover his ways to dodge */
		double tc = MIN(tof, 8.0), reach = 0.5 * 1.5 * tc * tc;

		extra = MIN(reach, 120.0) * 0.25 * d->gene[G_SPREAD];
	    }
	    if (c->radar)
		extra = 0.0;
	    if (miss > tol + extra)
		continue;
	    Fb_target_at(rx, ry, c->vx, c->vy, ax, ay, tof, &hx, &hy);
	    if (!Fb_line_free(ox, oy, ox + hx, oy + hy))
		continue;
	    if (Fb_friendly_in_line(pl, dirs[g], tof))
		continue;
	    SET_BIT(pl->used, USES_SHOT);
	    CLR_BIT(pl->used, HAS_SHIELD);
	    d->my_fire = 0.95 * d->my_fire + 0.05 * ndirs;
	    return true;
	}
    }
    d->my_fire *= 0.95;
    return false;
}

/* ------------------------------------------------------------------------
 * Items
 * ------------------------------------------------------------------------ */

/* Another enemy a reprogrammed robot could be sent after (or NO_ID). */
static int Fb_ecm_victim(player_t *pl, fb_data_t *d, player_t *robot)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy), best = 1e30;
    int i, id = NO_ID;

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	fb_contact_t *c;
	double dist;

	if (e == pl || e == robot || !Fb_id_ok(e->id))
	    continue;
	c = &d->c[e->id];
	if (!c->known || !pl->visibility[i].canSee)
	    continue;
	dist = Fb_dist(ox, oy, c->x, c->y);
	if (BIT(world->rules->mode, LIMITED_VISIBILITY) && dist > pl->sensor_range)
	    continue;
	if (dist < best) {
	    best = dist;
	    id = e->id;
	}
    }
    return id;
}

static void Fb_use_items(player_t *pl, fb_data_t *d, int level, const fb_flags_t *fl,
			 bool fired)
{
    double ox = Fb_px(pl->pos.cx), oy = Fb_px(pl->pos.cy);
    player_t *tgt = Fb_target(d);
    fb_contact_t *c = tgt ? &d->c[tgt->id] : NULL;
    double dist = 1e9, err = 64.0;
    bool seen = false, los = false, behind = false;
    double fuel = pl->fuel.sum, fmax = Fb_fuel_ref(pl);

    (void)level;
    if (c && c->known) {
	double rx = Fb_wdx(c->x - ox), ry = Fb_wdy(c->y - oy);

	dist = LENGTH(rx, ry);
	seen = c->seen;
	los = Fb_line_free(ox, oy, c->x, c->y);
	err = ABS(Fb_angle_diff(pl->dir, Fb_dir_of(rx, ry)));
	/* he is behind us and closing in */
	behind = seen && ABS(Fb_angle_diff(pl->dir, Fb_dir_of(rx, ry))) > RES * 3 / 8
	    && (c->vx - pl->vel.x) * -rx + (c->vy - pl->vel.y) * -ry > 0.0;
    }

    /* an unavoidable hit: emergency shield, phasing, hyperjump */
    if (fl->valid && fl->shot_t >= 0 && fl->shot_t <= 1) {
	if (BIT(pl->have, HAS_EMERGENCY_SHIELD) && !BIT(pl->used, HAS_EMERGENCY_SHIELD)
	    && FbTick - d->last_eshield > 12) {
	    Emergency_shield(pl, true);
	    d->last_eshield = FbTick;
	} else if (Player_has_phasing_device(pl) && !Player_is_phasing(pl)
		   && !BIT(pl->used, HAS_EMERGENCY_SHIELD) && FbTick - d->last_phase > 12) {
	    Phasing(pl, true);
	    d->last_phase = FbTick;
	} else if (pl->item[ITEM_HYPERJUMP] > 0 && !BIT(pl->used, HAS_EMERGENCY_SHIELD)
		   && !Player_is_phasing(pl) && FbTick - d->last_hyper > 24
		   && fl->shot_t == 0) {
	    Initiate_hyperjump(pl);
	    d->last_hyper = FbTick;
	}
    }

    /* ECM: homing missiles and mines close by, or an enemy in reach */
    if (pl->item[ITEM_ECM] > 0 && fuel > -ED_ECM + 50.0 && FbTick - d->last_ecm > 18) {
	bool use = d->missile_dist < ECM_DISTANCE * 0.4 || d->mine_dist < ECM_DISTANCE * 0.5
	    || (seen && dist < ECM_DISTANCE * 0.6);

	if (use) {
	    int keep = pl->lock.pl_id, victim = NO_ID;
	    bool locked = BIT(pl->lock.tagged, LOCK_PLAYER) ? true : false;

	    /* a robot in reach gets sent after somebody else */
	    if (options.ecmsReprogramRobots && tgt != NULL && Player_is_robot(tgt)
		&& dist < ECM_DISTANCE)
		victim = Fb_ecm_victim(pl, d, tgt);
	    if (victim != NO_ID) {
		pl->lock.pl_id = victim;
		SET_BIT(pl->lock.tagged, LOCK_PLAYER);
		pl->lock.distance = Fb_dist(ox, oy, d->c[victim].x, d->c[victim].y);
	    } else if (options.ecmsReprogramRobots && tgt != NULL && Player_is_robot(tgt)) {
		CLR_BIT(pl->lock.tagged, LOCK_PLAYER);	/* blind it instead */
	    }
	    Fire_ecm(pl);
	    d->last_ecm = FbTick;
	    pl->lock.pl_id = keep;
	    if (locked)
		SET_BIT(pl->lock.tagged, LOCK_PLAYER);
	}
    }

    /* heat seekers go for the engine: cut thrust is up to the flight, drop a tank */
    if (d->heat_dist < 8 * BLOCK_SZ && pl->fuel.num_tanks > 0 && fuel > fmax * 0.3)
	Tank_handle_detach(pl);

    /* missiles */
    if (pl->item[ITEM_MISSILE] > 0 && tgt && seen && !c->shield
	&& FbTick - d->last_missile > 18 && dist > 200.0 && dist < 1100.0) {
	int type = -1;

	if (options.allowTorpedoes && dist < 450.0 && err <= 3 && los)
	    type = OBJ_TORPEDO;
	else if (options.allowHeatSeekers && Hist[HistHead][tgt->id].thrust && los
		 && dist < 800.0)
	    type = OBJ_HEAT_SHOT;
	else if (options.allowSmartMissiles && (dist > 450.0 || !los))
	    type = OBJ_SMART_SHOT;
	if (type >= 0) {
	    Fire_shot(pl, type, pl->dir);
	    d->last_missile = FbTick;
	}
    }

    /* mines for the one chasing us */
    if (pl->item[ITEM_MINE] > 0 && behind && dist < 400.0
	&& FbTick - d->last_mine > 18) {
	Place_mine(pl);
	d->last_mine = FbTick;
    }

    /* laser pulses when on the lead */
    if (pl->item[ITEM_LASER] > pl->num_pulses && tgt && seen && los && !c->shield
	&& fuel > fmax * 0.2 && dist < options.pulseSpeed * options.pulseLife * 0.9
	&& !BIT(pl->used, HAS_SHIELD)) {
	double ldir, tof;

	if (Fb_lead(Fb_wdx(c->x - ox), Fb_wdy(c->y - oy), c->vx - pl->vel.x,
		    c->vy - pl->vel.y, options.pulseSpeed, &ldir, &tof)
	    && ABS(Fb_angle_diff(pl->dir, MOD2((int)(ldir + 0.5), RES))) <= 1)
	    SET_BIT(pl->used, USES_LASER);
    }

    /* transporter: take his items */
    if (pl->item[ITEM_TRANSPORTER] > 0 && tgt && seen && dist < TRANSPORTER_DISTANCE * 0.9
	&& fuel > -ED_TRANSPORTER + 50.0 && FbTick - d->last_transport > 12) {
	Do_transporter(pl);
	d->last_transport = FbTick;
    }

    /* cloak while hunting and fighting, as long as the fuel lasts */
    if (pl->item[ITEM_CLOAK] > 0) {
	bool want = fuel > fmax * 0.3 && (d->mode == FB_HUNT || d->mode == FB_SEARCH
					   || d->mode == FB_ENGAGE || d->mode == FB_FALLBACK);

	if (fuel < fmax * 0.15)
	    want = false;
	if (want != Player_is_cloaked(pl))
	    Cloak(pl, want);
    }

    /* deflector against shots and missiles close by */
    if (Player_has_deflector(pl)) {
	bool want = (d->danger > 1.0 || d->missile_dist < 300.0 || d->heat_dist < 300.0)
	    && fuel > fmax * 0.2;

	if (want != (BIT(pl->used, USES_DEFLECTOR) ? true : false))
	    Deflector(pl, want);
    }

    /* emergency thrust for long ways and falling back */
    if (Player_has_emergency_thrust(pl)) {
	bool want = d->mode == FB_FALLBACK
	    || ((d->mode == FB_HUNT || d->mode == FB_SEARCH) && dist > 1500.0);

	if (want != Player_uses_emergency_thrust(pl))
	    Emergency_thrust(pl, want);
    }

    /* pressor beam against a ship about to ram us */
    if (Player_has_tractor_beam(pl) && BIT(world->rules->mode, CRASH_WITH_PLAYER)
	&& fl->valid && fl->ram_t >= 0 && fl->ram_t <= 2) {
	pl->tractor_is_pressor = true;
	SET_BIT(pl->used, USES_TRACTOR_BEAM);
    }
    (void)fired;
}

/* ------------------------------------------------------------------------
 * Robot type interface
 * ------------------------------------------------------------------------ */

static void Robot_fight_round_tick(void)
{
    int i;

    NumFightbots = 0;
    for (i = 0; i < NumPlayers; i++) {
	if (Fb_is_fightbot(Player_by_index(i)))
	    NumFightbots++;
    }
    if (NumFightbots == 0)
	return;
    FbTick++;
    FieldBudget = 1;
    Fb_map_update();
    FbShotGrav = options.shotsGravity && world->gravity != NULL;
    Fb_learn_load();
    Fb_record_history();
    Fb_update_opponents();
    if (Learn.dirty && FbTick - LastSave > FB_SAVE_TICKS)
	Fb_learn_save();
}

static void Robot_fight_create(player_t *pl, char *str)
{
    fb_data_t *d;
    int g;

    if (!(d = XMALLOC(fb_data_t, 1))) {
	error("no mem for fightbot robot");
	End_game();
    }
    memset(d, 0, sizeof(*d));
    d->learn = true;
    d->target_id = NO_ID;
    d->war_id = NO_ID;
    d->station = -1;
    d->last_killer = NO_ID;
    d->mode = FB_SEARCH;
    for (g = 0; g < FB_NGENES; g++)
	d->gene[g] = fb_gene_def[g].init;
    if (str != NULL && *str != '\0') {
	int a, b, lvl = 0;

	if (sscanf(str, " %d %d %d", &a, &b, &lvl) == 3 && lvl > 0) {
	    LIMIT(lvl, 1, 100);
	    d->level = lvl;
	}
    }
    pl->turnspeed = 0;
    pl->turnresistance = 0;
    pl->robot_data_ptr->private_data = (void *)d;
}

static void Robot_fight_go_home(player_t *pl)
{
    fb_data_t *d = Fb_data(pl);
    int i;

    /* a new life: the last one ended with a death */
    if (d->ep_active)
	Fb_episode_end(d, true);
    if (!Learn.loaded && world != NULL)
	Fb_learn_load();
    Fb_episode_start(d);
    d->alive_ticks = 0;
    d->plan_len = 0;
    d->danger = 0.0;
    d->my_fire = 0.0;
    d->search_valid = false;
    d->fallback_until = d->fallback_cool = 0;
    d->last_killer = NO_ID;
    d->still_ticks = 0;
    d->push_ticks = d->frozen_ticks = 0;
    d->unstick_until = d->escape_until = 0;
    d->run_phase = 0;
    if (d->destruct) {
	Player_self_destruct(pl, false);
	d->destruct = false;
    }
    if (d->mode == FB_REFUEL || d->mode == FB_FALLBACK || d->mode == FB_ITEM)
	d->mode = FB_SEARCH;
    for (i = 0; i < NUM_IDS; i++)
	d->c[i].known = d->c[i].seen = false;
}

static void Robot_fight_play(player_t *pl)
{
    fb_data_t *d = Fb_data(pl);
    int level = Fb_level(d);
    double u = Fb_unskill(level);
    static fb_ctx_t cx;
    fb_flags_t fl;
    bool spawn_shield, fired;

    CLR_BIT(pl->used, USES_SHOT | USES_LASER | USES_TRACTOR_BEAM | USES_REFUEL);
    /* the start shield (no shields otherwise) lasts until we drop it: keep it */
    spawn_shield = pl->shield_time > 0 && BIT(pl->used, HAS_SHIELD);
    if (!spawn_shield)
	CLR_BIT(pl->used, USES_SHIELD);
    pl->power = Fb_power(level);
    /*
     * Out of fuel with no station to get to, or not getting anywhere for
     * long: start over, as a human would.
     */
    {
	bool want = (pl->fuel.sum < 5.0
		     && (d->mode != FB_REFUEL || d->station < 0 || d->still_ticks > 10 * 12))
	    || d->frozen_ticks > 5 * 12	/* stuck in a wall (broken maps) */
	    || (d->frozen_ticks > 12
		&& Fb_ship_clear(Fb_px(pl->pos.cx), Fb_px(pl->pos.cy)) <= 0.0);

	if (want != d->destruct) {
	    Player_self_destruct(pl, want);
	    d->destruct = want;
	}
    }
    if (!FbMapOk) {
	Thrust(pl, false);
	return;
    }
    d->alive_ticks++;

    Fb_perceive(pl, d, level);
    Fb_ctx_init(pl, d, level, &cx);
    Fb_think(pl, d, level);
    Fb_set_goal(pl, d, level, &cx);
    fl = Fb_fly(pl, d, level, &cx);

    /*
     * Shields (where allowed) against what the best plan cannot avoid; the
     * shield is down again as soon as the bot fires.
     */
    if (!spawn_shield && BIT(pl->have, HAS_SHIELD) && pl->shield_time <= 0
	&& pl->fuel.sum > 5.0 && fl.valid
	&& ((fl.shot_t >= 0 && fl.shot_t <= 1) || (fl.ram_t >= 0 && fl.ram_t <= 1)))
	SET_BIT(pl->used, USES_SHIELD);

    /*
     * Pushing against a wall without getting anywhere (tight spots the
     * model does not see right): turn to the most open way and go.
     */
    if (((fl.valid && fl.thrust) || FbTick < d->escape_until)
	&& LENGTH(pl->vel.x, pl->vel.y) < 1.0) {
	d->push_ticks++;
	d->frozen_ticks++;
    } else if (LENGTH(pl->vel.x, pl->vel.y) >= 2.0)
	d->push_ticks = d->frozen_ticks = 0;
    if (d->push_ticks > 12 && FbTick >= d->escape_until) {
	double best = -1.0;
	int k;

	for (k = 0; k < 32; k++) {
	    int dir = k * RES / 32;
	    double f = Fb_free_ray(cx.ox, cx.oy, dir, 4.0, 300.0);

	    if (f > best) {
		best = f;
		d->escape_dir = dir;
	    }
	}
	d->escape_until = FbTick + 10;
	d->push_ticks = 0;
	FB_LOG("[fightbot] %s stuck: escaping towards %d\n", pl->name, d->escape_dir);
    }
    if (FbTick < d->escape_until) {
	int dir = Fb_turn_toward(pl->dir, d->escape_dir, Fb_turn_rate(level));

	Fb_steer(pl, dir, ABS(Fb_angle_diff(dir, d->escape_dir)) <= 8);
    } else if (fl.valid) {
	int dir = fl.dir;

	/* lower levels steer less exactly: an error that drifts slowly */
	if (FbTick - d->steer_time > 10) {
	    d->steer_off = (rfrac() - 0.5) * 2.0 * FB_STEER_ERR * u;
	    d->steer_time = FbTick;
	}
	dir = MOD2(dir + (int)(d->steer_off + (d->steer_off < 0 ? -0.5 : 0.5)), RES);
	Fb_steer(pl, dir, fl.thrust);
    } else
	Thrust(pl, false);

    /* a deadly shield only goes down for a good shot */
    fired = Fb_fire(pl, d, level, spawn_shield || !(fl.valid && fl.shot_t >= 0 && fl.shot_t <= 1));
    Fb_use_items(pl, d, level, &fl, fired);

    /* refuel at any station in reach */
    if (BIT(pl->have, HAS_REFUEL) && pl->fuel.sum < pl->fuel.max - 1.0) {
	int i, bi = -1;
	double bd = 85.0;

	for (i = 0; i < Num_fuels(); i++) {
	    fuel_t *fs = Fuel_by_index(i);
	    double dist = Wrap_length(pl->pos.cx - fs->pos.cx, pl->pos.cy - fs->pos.cy) / CLICK;

	    if (fs->fuel < 1.0
		|| (BIT(world->rules->mode, TEAM_PLAY) && options.teamFuel
		    && fs->team != pl->team))
		continue;
	    if (dist < bd) {
		bd = dist;
		bi = i;
	    }
	}
	if (bi >= 0) {
	    SET_BIT(pl->used, USES_REFUEL);
	    pl->fs = bi;
	}
    }
}

/*
 * Reprogrammed by an ECM, or at war with a killer: go for him and only
 * him, and search him when he is not in sight.
 */
static void Robot_fight_set_war(player_t *pl, int victim_id)
{
    fb_data_t *d = Fb_data(pl);

    if (victim_id == NO_ID || !Fb_id_ok(victim_id) || victim_id == pl->id) {
	d->war_id = NO_ID;
	return;
    }
    d->war_id = victim_id;
    d->target_id = victim_id;
    d->search_valid = false;
    d->fallback_until = 0;
    if (d->mode == FB_FALLBACK || d->mode == FB_ITEM)
	d->mode = FB_HUNT;
    FB_LOG("[fightbot] %s goes for %s\n", pl->name, Player_by_id(victim_id)
	   ? Player_by_id(victim_id)->name : "?");
}

static int Robot_fight_war_on_player(player_t *pl)
{
    return Fb_data(pl)->war_id;
}

/* /msg <bot> status | level N | learn on|off | forget */
static void Robot_fight_message(player_t *pl, const char *message)
{
    fb_data_t *d = Fb_data(pl);
    int lvl;

    if (message == NULL)
	return;
    if (strstr(message, "status") != NULL) {
	player_t *t = Fb_target(d);

	Set_message_f("Fightbot level %d, %s%s%s%s, fuel %.0f, %d kills %d deaths, "
		      "distance %.0f aggression %.2f, %ld lives learned (%s). [%s]",
		      Fb_level(d), fb_mode_name[d->mode], t ? " " : "",
		      t ? t->name : "", d->war_id != NO_ID ? " (war)" : "",
		      pl->fuel.sum, d->kills, d->deaths, Fb_fight_dist(d, NULL),
		      d->gene[G_AGGR], Learn.episodes, d->learn ? "on" : "off", pl->name);
    } else if (strstr(message, "level ") != NULL
	       && sscanf(strstr(message, "level ") + 6, "%d", &lvl) == 1) {
	d->level = lvl <= 0 ? 0 : MIN(lvl, 100);
	Set_message_f("Skill level set to %d. [%s]", Fb_level(d), pl->name);
    } else if (strstr(message, "learn off") != NULL) {
	d->learn = false;
	Set_message_f("Learning off. [%s]", pl->name);
    } else if (strstr(message, "learn on") != NULL) {
	d->learn = true;
	Set_message_f("Learning on. [%s]", pl->name);
    } else if (strstr(message, "forget") != NULL) {
	if (Learn.loaded) {
	    Fb_learn_reset();
	    Learn.dirty = true;
	    Fb_learn_save();
	}
	Set_message_f("Forgot what I learned about %s. [%s]", world->name, pl->name);
    }
}

static void Robot_fight_destroy(player_t *pl)
{
    if (Learn.dirty)
	Fb_learn_save();
    XFREE(pl->robot_data_ptr->private_data);
}

static void Robot_fight_invite(player_t *pl, player_t *inviter)
{
    fb_data_t *d = Fb_data(pl);

    if (inviter->id == d->war_id || !Player_is_active(inviter)) {
	Refuse_alliance(pl, inviter);
	return;
    }
    Accept_alliance(pl, inviter);
}

static void Robot_fight_game_event(player_t *pl, int event, player_t *other)
{
    fb_data_t *d = Fb_data(pl);
    fb_opp_t *o;

    if (event == ROBOT_KILLED_PLAYER && other != NULL && other != pl) {
	d->kills++;
	d->ep_kills++;
	if (d->tac_active && other->id == d->tac_opp)
	    Fb_tactic_done(d, 1.0);
	if ((o = Fb_opp(other)) != NULL)
	    o->killed += 1.0;
    } else if (event == ROBOT_KILLED_BY) {
	d->deaths++;
	if (other != NULL && other != pl && (o = Fb_opp(other)) != NULL)
	    o->kills_us += 1.0;
	Fb_tactic_done(d, other != NULL && other->id == d->tac_opp ? -1.0 : -0.3);
	Fb_episode_end(d, true);
    }
}

static robot_type_t robot_fight_type = {
    FB_TYPE_NAME,
    Robot_fight_round_tick,
    Robot_fight_create,
    Robot_fight_go_home,
    Robot_fight_play,
    Robot_fight_set_war,
    Robot_fight_war_on_player,
    Robot_fight_message,
    Robot_fight_destroy,
    Robot_fight_invite,
    Robot_fight_game_event
};

int Robot_fight_setup(robot_type_t *type_ptr)
{
    *type_ptr = robot_fight_type;
    return 0;
}

/*
 * The maps fightbots are for: no team ball game and no race.
 */
bool Robot_fight_map(void)
{
    return !Robot_ball_game() && !Robot_race_map();
}
