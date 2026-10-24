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
 * Racebot: a robot driver ("race") dedicated to race maps (timing mode).
 *
 * Smart robots use this driver on maps with checkpoints when smartBotType
 * is "auto" or "race".
 *
 *  - Map analysis (once per map): wall clearance on a fine grid (exact
 *    distance transform), a navigation grid and a Dijkstra distance field
 *    to every checkpoint disk.  The fields are chained: the value of a
 *    position is its distance to the next checkpoint plus the best possible
 *    rest of the course from there, so passing a checkpoint at a bad spot
 *    costs exactly the distance it wastes.
 *  - Flight: a sampling model predictive controller.  Candidate manoeuvres
 *    are simulated with the server's physics (thrust, turn rate, friction,
 *    gravity, wall bounces) for about three seconds, followed by a racing
 *    policy, and scored on the course value they reach.
 *  - Fuel: the fuel used per course segment is measured; when the fuel left
 *    does not last for the remaining laps the bot stops at the fuel station
 *    with the smallest detour.  Stations on the way are used in passing.
 *  - Defence: enemy shots and ships are part of the flight model, shields
 *    go up against unavoidable hits, rams and hard wall bounces, and the
 *    bot shoots back at players who attacked or pushed it.
 *  - Learning: per map the bot remembers the fuel and time every course
 *    segment takes and its best lap, in $XDG_DATA_HOME/xpilot-tng/racebot/.
 *    From the second race on its fuel planning is exact, so it makes no
 *    needless stops and runs dry no more.
 *
 * The skill level (smartRobotLevel) sets turn rate, reaction delay, aiming
 * precision and how many manoeuvres are sampled.
 */

#include "xpserver.h"
#include <sys/stat.h>

#define RB_TYPE_NAME		"race"

#define RB_SHIP_R		16.0	/* collision radius of a ship (px) */
#define RB_INF			1e30f
#define RB_H			40	/* rollout horizon (ticks) */
#define RB_NEAR_H		20	/* ticks shots and ships are predicted */
#define RB_MAX_CAND		160
#define RB_MAX_SHOTS		48
#define RB_MAX_ENEMIES		16
#define RB_CHAIN		4	/* checkpoints a rollout can pass */
#define RB_DEPTH		3	/* checkpoints beyond the next in the course value */
#define RB_FINISH_V		60.0	/* worth of a tick saved at the finish (px) */
#define RB_RUN_W		(2.0 / RB_H)	/* weight of the course value on the way */
#define RB_KV			1.5	/* worth of terminal speed towards the carrot (ticks) */
#define RB_CORNER_V		9.0	/* policy: speed at the carrot (px/tick) */
#define RB_BRAKE		0.5	/* policy: share of the thrust planned for braking */
#define RB_BOUNCE_COST		80.0	/* a wall bounce, besides the speed lost */
#define RB_HARD_COST		3000.0	/* a bounce only the shield survives */
/*
 * Skill below level 100 (see Rb_unskill(): 0 at level 100, 1 at level 1):
 * a little less thrust and less exact manoeuvres, so lower levels lose
 * time like a weaker human pilot.
 */
#define RB_POWER_LOSS		0.16	/* share of the thrust lost at level 1 */
#define RB_STEER_ERR		5.0	/* heading error at level 1 (dir units) */
#define RB_PLAN_NOISE		600.0	/* noise on plan costs at level 1 (px) */
#define RB_THREAT_TICKS		(6 * 12)
#define RB_STUCK_TICKS		120	/* no progress this long: self-destruct */
#define RB_DETOUR_GAIN		120.0	/* plans gaining less: try the detour */
#define RB_STUCK_GAIN		40.0	/* progress that counts (px) */
#define RB_LEARN_SUBDIR		"xpilot-tng/racebot"
#define RB_LEARN_VERSION	2

/* ------------------------------------------------------------------------
 * Data types
 * ------------------------------------------------------------------------ */

typedef struct {
    float	*dist;		/* cost to the goal per nav cell (px) */
    int32_t	*next;		/* next cell towards the goal, -2 in the goal */
    double	gx, gy;		/* goal (px) */
    bool	valid;
} rb_field_t;

/* What was learned about one course segment (the way to checkpoint i). */
typedef struct {
    double	time;		/* running average of the segment time (ticks) */
    double	best;		/* best segment time (ticks) */
    double	fuel;		/* running average of the fuel used */
    int		runs;
} rb_seg_t;

typedef struct {
    bool	loaded;
    bool	dirty;
    char	map[MAX_CHARS];
    uint32_t	sig;
    int		nchecks;
    double	best_lap;	/* ticks */
    rb_seg_t	*seg;
} rb_learn_t;

typedef struct {
    int		level;		/* 0: use options.smartRobotLevel */
    bool	learn;
    int		plan_len;
    short	plan_dir[RB_H];
    bool	plan_thr[RB_H];
    /* course progress */
    int		last_check, last_round;
    bool	seg_valid;	/* seg_start is the time of a checkpoint pass */
    unsigned long seg_start;
    double	seg_fuel0;
    /* fuel */
    bool	pit;
    int		pit_station;
    double	pit_target;
    double	refuel_ticks;
    /* stuck (e.g. slow in the repelling gravs of a corner) */
    double	stuck_best;	/* best distance to the next checkpoint */
    unsigned long stuck_since;
    int		stuck_check;
    bool	destruct;
    bool	detour;		/* avoiding gravity until the next checkpoint */
    int		slow_ticks;	/* best plans gaining too little near gravity */
    /* combat */
    unsigned long threat_time[NUM_IDS];
    double	aim_off;
    double	steer_off;	/* heading error, drifts (low levels) */
    unsigned long steer_time;
    unsigned long aim_time;
    double	pvx, pvy;	/* velocity last tick */
    unsigned long alive_ticks;
} rb_data_t;

typedef struct {
    double	x, y, vx, vy;
    int		dir;
    int		k;		/* checkpoints passed in the rollout */
    int		hits, hard_hits;
    bool	crashed;
} rb_state_t;

typedef struct {
    int		len;
    short	dir[RB_H];
    bool	thr[RB_H];
} rb_cand_t;

typedef struct {
    double	x, y, vx, vy, life;
    double	t0;		/* fired at this tick (virtual shots) */
    double	weight;
} rb_shot_t;

typedef struct {
    double	x, y, vx, vy;
    int		dir;
} rb_enemy_t;

/* The best plan: this tick's action and what it runs into (for shields). */
typedef struct {
    bool	valid;
    int		dir;
    bool	thrust;
    int		shot_t, ram_t, hard_t;
    double	end_value;	/* course value at the end, -1e9 finished */
    double	progress;	/* course value gained over the horizon */
    double	end_x, end_y, end_speed;	/* where the plan ends (absolute) */
} rb_flags_t;

typedef struct {
    player_t	*pl;
    rb_data_t	*d;
    double	ox, oy;			/* origin: ship position (px) */
    rb_state_t	s0;
    double	acc, max_bounce, fric_keep, dt;
    double	ship_r;			/* the ship shape's reach (px) */
    int		turn, frames;
    bool	grav;
    double	margin, clear0;
    /* race: chained checkpoint targets */
    bool	race;
    int		left;			/* checkpoint passes left in the race */
    int		tgt[RB_CHAIN];
    const rb_field_t *vf[RB_CHAIN];	/* course value after passing k */
    const rb_field_t *pf[RB_CHAIN];	/* racing line after passing k */
    double	check_r2;
    /* point goal (fuel stop) */
    rb_field_t	*pfield;
    double	gx, gy, arrive_r;
    /* policy */
    double	vmax;
    /* threats */
    int		nshots;
    rb_shot_t	shots[RB_MAX_SHOTS];
    int		nen;
    rb_enemy_t	en[RB_MAX_ENEMIES];
    double	ram_cost;		/* touching an enemy ship */
    /* aiming */
    bool	has_target;
    double	tx, ty, tvx, tvy, aim_w;
    int		lead_dir;
} rb_ctx_t;

/* ------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------ */

static const poly_t	*RbPdata;
static int		RbNumPolys = -1;
static int		RbMapW = -1, RbMapH = -1, RbNumChecks = -1;
static bool		RbMapOk;
static bool		RbWrap;
static double		RbW, RbH;
static int		FineW, FineH;
static double		FineCX, FineCY, FineStep;
static float		*ShipClear;
static int		NavW, NavH, NavN, NavF;
static double		NavCX, NavCY;
static float		*NavRX, *NavRY, *NavClear;
static uint8_t		*NavEdge;
static float		*NavCost;
static float		*NavCostG;	/* NavCost, strong gravity costing more */

typedef struct { float f; int32_t c; } rb_heap_t;
static rb_heap_t	*Heap;
static int		HeapN, HeapCap;

/*
 * CheckF[d][i]: cost to pass checkpoint i and then the d checkpoints after
 * it, from anywhere.  Only the deepest ones keep their paths (racing line).
 */
static rb_field_t	*CheckF[RB_DEPTH + 1];
/*
 * The same avoiding strong gravity, computed when needed: for a ship too
 * slow to get through the repelling gravs on the short way.
 */
static rb_field_t	*DetourF[RB_DEPTH + 1];
static rb_field_t	*FuelF;		/* one per fuel station, lazy */
static rb_field_t	PosF;		/* from a robot's position (station choice) */
static int		NumFuelF;
static double		CheckR;		/* checkpoint radius (px) */

static rb_learn_t	Learn;
static unsigned long	RbTick;
static int		NumRacebots;

static const int	nb_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int	nb_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

#define RB_PASS_R	20.0	/* clearance of a nav node */
#define RB_EDGE_R	17.0	/* clearance along a nav edge */
#define RB_PREF_R	45.0	/* below this clearance edges cost more */
#define RB_PREF_W	0.6

#ifndef RB_DEBUG
#define RB_DEBUG 0
#endif
#define RB_LOG(...) do { if (RB_DEBUG) { printf("%6lu ", RbTick); printf(__VA_ARGS__); fflush(stdout); } } while (0)

/* ------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------ */

static inline rb_data_t *Rb_data(player_t *pl)
{
    return (rb_data_t *)pl->robot_data_ptr->private_data;
}

static inline bool Rb_is_racebot(player_t *pl)
{
    const char *name;

    if (!Player_is_robot(pl) || pl->robot_data_ptr == NULL)
	return false;
    name = Robot_type_name(pl);
    return name != NULL && strcmp(name, RB_TYPE_NAME) == 0;
}

static inline bool Rb_ally(player_t *a, player_t *b)
{
    if (a == b)
	return true;
    if (BIT(world->rules->mode, TEAM_PLAY) && a->team == b->team)
	return true;
    return Team_immune(a->id, b->id);
}

static inline double Rb_px(click_t c)
{
    return (double)c / CLICK;
}

static inline double Rb_wdx(double dx)
{
    if (RbWrap) {
	if (dx > RbW * 0.5)
	    dx -= RbW;
	else if (dx < -RbW * 0.5)
	    dx += RbW;
    }
    return dx;
}

static inline double Rb_wdy(double dy)
{
    if (RbWrap) {
	if (dy > RbH * 0.5)
	    dy -= RbH;
	else if (dy < -RbH * 0.5)
	    dy += RbH;
    }
    return dy;
}

static inline double Rb_dist(double x1, double y1, double x2, double y2)
{
    return LENGTH(Rb_wdx(x2 - x1), Rb_wdy(y2 - y1));
}

static inline int Rb_angle_diff(int a, int b)
{
    int d = MOD2(a - b, RES);

    return d > RES / 2 ? d - RES : d;
}

static inline int Rb_dir_of(double x, double y)
{
    return MOD2((int)(findDir(x, y) + 0.5), RES);
}

static int Rb_level(rb_data_t *d)
{
    int lvl = d->level > 0 ? d->level : options.smartRobotLevel;

    LIMIT(lvl, 1, 100);
    return lvl;
}

static int Rb_turn_rate(int level)
{
    return 3 + (level * 9) / 100;	/* same as the other smart robots */
}

static bool Rb_player_active(player_t *pl)
{
    return pl != NULL && !Player_is_tank(pl) && Player_is_alive(pl)
	&& !Player_is_paused(pl) && !Player_is_waiting(pl);
}

static inline int Rb_turn_toward(int dir, int want, int rate)
{
    int d = Rb_angle_diff(want, dir);

    LIMIT(d, -rate, rate);
    return MOD2(dir + d, RES);
}

/* ------------------------------------------------------------------------
 * Map analysis: fine clearance grid
 * ------------------------------------------------------------------------ */

static inline int Rb_fine_index(double x, double y)
{
    int ix, iy;

    if (RbWrap) {
	while (x < 0) x += RbW;
	while (x >= RbW) x -= RbW;
	while (y < 0) y += RbH;
	while (y >= RbH) y -= RbH;
    }
    ix = (int)(x / FineCX);
    iy = (int)(y / FineCY);
    if (x < 0 || y < 0 || ix >= FineW || iy >= FineH)
	return -1;
    return iy * FineW + ix;
}

static inline float Rb_ship_clear(double x, double y)
{
    int i = Rb_fine_index(x, y);

    return i < 0 ? 0.0f : ShipClear[i];
}

/* Minimum clearance along a segment (absolute px), stops below 'need'. */
static double Rb_segment_clear(double x1, double y1, double x2, double y2, double need)
{
    double dx = Rb_wdx(x2 - x1), dy = Rb_wdy(y2 - y1);
    double len = LENGTH(dx, dy), m = 1e9;
    int i, n = (int)(len / FineStep) + 1;

    for (i = 0; i <= n; i++) {
	double f = (double)i / n;
	int k = Rb_fine_index(x1 + dx * f, y1 + dy * f);
	double c = k < 0 ? 0.0 : ShipClear[k];

	if (c < m) {
	    m = c;
	    if (m < need)
		return m;
	}
    }
    return m;
}

/* 1-D squared distance transform (Felzenszwalb & Huttenlocher). */
static void Rb_dt1d(const double *f, double *d, int n, int *v, double *z)
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

/*
 * Exact Euclidean clearance (px) of every fine cell to the nearest obstacle
 * cell.  Lines are tripled on wrapping maps; outside counts as obstacle
 * otherwise.
 */
static bool Rb_edt(const uint8_t *obst, float *out)
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
	if (RbWrap) {
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
	Rb_dt1d(f, d, n, v, z);
	for (y = 0; y < FineH; y++)
	    tmp[y * FineW + x] = d[y + off];
    }
    for (y = 0; y < FineH; y++) {
	if (RbWrap) {
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
	Rb_dt1d(f, d, n, v, z);
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

static inline int Rb_nav_cell(double x, double y)
{
    int ix, iy;

    if (RbWrap) {
	while (x < 0) x += RbW;
	while (x >= RbW) x -= RbW;
	while (y < 0) y += RbH;
	while (y >= RbH) y -= RbH;
    }
    ix = (int)(x / NavCX);
    iy = (int)(y / NavCY);
    LIMIT(ix, 0, NavW - 1);
    LIMIT(iy, 0, NavH - 1);
    return iy * NavW + ix;
}

static inline int Rb_nav_nb(int c, int dx, int dy)
{
    int x = c % NavW + dx, y = c / NavW + dy;

    if (RbWrap) {
	x = (x + NavW) % NavW;
	y = (y + NavH) % NavH;
    } else if (x < 0 || x >= NavW || y < 0 || y >= NavH)
	return -1;
    return y * NavW + x;
}

static void Rb_heap_push(float f, int32_t c)
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

static rb_heap_t Rb_heap_pop(void)
{
    rb_heap_t top = Heap[0], last = Heap[--HeapN];
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

static void Rb_field_free(rb_field_t *fd)
{
    if (fd == NULL)
	return;
    free(fd->dist);
    free(fd->next);
    fd->dist = NULL;
    fd->next = NULL;
    fd->valid = false;
}

static void Rb_learn_save(void);

static void Rb_map_free(void)
{
    int i, d;

    free(ShipClear); free(NavRX); free(NavRY); free(NavClear); free(Heap);
    free(NavEdge); free(NavCost); free(NavCostG);
    ShipClear = NULL;
    NavRX = NavRY = NavClear = NULL;
    NavEdge = NULL;
    NavCost = NavCostG = NULL;
    Heap = NULL;
    HeapCap = HeapN = 0;
    for (d = 0; d <= RB_DEPTH; d++) {
	if (CheckF[d]) {
	    for (i = 0; i < RbNumChecks; i++)
		Rb_field_free(&CheckF[d][i]);
	    free(CheckF[d]);
	    CheckF[d] = NULL;
	}
	if (DetourF[d]) {
	    for (i = 0; i < RbNumChecks; i++)
		Rb_field_free(&DetourF[d][i]);
	    free(DetourF[d]);
	    DetourF[d] = NULL;
	}
    }
    if (FuelF) {
	for (i = 0; i < NumFuelF; i++)
	    Rb_field_free(&FuelF[i]);
	free(FuelF);
	FuelF = NULL;
    }
    Rb_field_free(&PosF);
    NumFuelF = 0;
    RbMapOk = false;
}

/*
 * Dijkstra over the nav grid from the goal (gx, gy): every open node within
 * seed_r counts as being there, so a checkpoint disk is one goal.  With
 * 'then' the goal nodes start with its values: the way on from there.
 */
static bool Rb_field_compute(rb_field_t *fd, double gx, double gy, double seed_r,
			     const rb_field_t *then, const float *cost)
{
    int c, r, gcell = Rb_nav_cell(gx, gy), seeded = 0;

    if (cost == NULL)
	cost = NavCost;
    if (fd->dist == NULL || fd->next == NULL) {
	Rb_field_free(fd);
	fd->dist = malloc(sizeof(float) * NavN);
	fd->next = malloc(sizeof(int32_t) * NavN);
	if (!fd->dist || !fd->next) {
	    Rb_field_free(fd);
	    return false;
	}
    }
    for (c = 0; c < NavN; c++) {
	fd->dist[c] = RB_INF;
	fd->next[c] = -1;
    }
    HeapN = 0;
    if (seed_r > 0.0) {
	int rx = (int)(seed_r / NavCX) + 1, ry = (int)(seed_r / NavCY) + 1, dx, dy;

	for (dy = -ry; dy <= ry; dy++) {
	    for (dx = -rx; dx <= rx; dx++) {
		int n = Rb_nav_nb(gcell, dx, dy);
		float v0;

		if (n < 0 || NavClear[n] < RB_PASS_R
		    || Rb_dist(gx, gy, NavRX[n], NavRY[n]) > seed_r)
		    continue;
		v0 = then ? then->dist[n] : 0.0f;
		if (v0 >= RB_INF)
		    continue;
		fd->dist[n] = v0;
		fd->next[n] = -2;
		Rb_heap_push(v0, n);
		seeded++;
	    }
	}
    }
    /* small goals (fuel stations): the open nodes nearest to it */
    for (r = 1; r <= 6 && seeded == 0; r++) {
	int dx, dy;

	for (dy = -r; dy <= r; dy++) {
	    for (dx = -r; dx <= r; dx++) {
		int n = Rb_nav_nb(gcell, dx, dy);
		double d;

		if (n < 0 || NavClear[n] < RB_PASS_R)
		    continue;
		d = Rb_dist(gx, gy, NavRX[n], NavRY[n]);
		if (d > (r + 0.75) * MAX(NavCX, NavCY))
		    continue;
		if (d < fd->dist[n]) {
		    fd->dist[n] = (float)d;
		    fd->next[n] = -2;
		    Rb_heap_push((float)d, n);
		    seeded++;
		}
	    }
	}
    }
    while (HeapN > 0) {
	rb_heap_t e = Rb_heap_pop();
	int j;

	if (e.f > fd->dist[e.c])
	    continue;
	for (j = 0; j < 8; j++) {
	    int n;
	    float nd;

	    if (!(NavEdge[e.c] & (1 << j)))
		continue;
	    n = Rb_nav_nb(e.c, nb_dx[j], nb_dy[j]);
	    nd = e.f + cost[e.c * 8 + j];
	    if (nd < fd->dist[n]) {
		fd->dist[n] = nd;
		fd->next[n] = e.c;
		Rb_heap_push(nd, n);
	    }
	}
    }
    fd->gx = gx;
    fd->gy = gy;
    fd->valid = true;
    return true;
}

/* Nav cell near (x, y) with the lowest cost to the goal (via straight hop). */
static int Rb_field_best_cell(const rb_field_t *fd, double x, double y, double *val)
{
    int c = Rb_nav_cell(x, y), dx, dy, best = -1;
    double bv = RB_INF;

    for (dy = -1; dy <= 1; dy++) {
	for (dx = -1; dx <= 1; dx++) {
	    int n = Rb_nav_nb(c, dx, dy);
	    double v;

	    if (n < 0 || fd->dist[n] >= RB_INF)
		continue;
	    v = fd->dist[n] + Rb_dist(x, y, NavRX[n], NavRY[n]);
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
static double Rb_field_value(const rb_field_t *fd, double x, double y)
{
    double v;

    if (Rb_field_best_cell(fd, x, y, &v) < 0)
	return 1e5 + Rb_dist(x, y, fd->gx, fd->gy) * 3.0;
    return v;
}

/*
 * Farthest point along the field's path from (x, y) still reachable in a
 * straight line with clearance 'clear' ("string pulling").
 */
static bool Rb_field_carrot(const rb_field_t *fd, double x, double y,
			    double clear, int max_steps, double *cx, double *cy,
			    bool *is_goal)
{
    int cur = Rb_field_best_cell(fd, x, y, NULL), i;

    *is_goal = false;
    if (cur < 0)
	return false;
    *cx = NavRX[cur];
    *cy = NavRY[cur];
    if (fd->next[cur] == -2)
	*is_goal = true;
    for (i = 0; i < max_steps; i++) {
	int nx = fd->next[cur];

	if (nx == -2) {
	    *is_goal = true;
	    break;
	}
	if (nx < 0)
	    break;
	if (Rb_segment_clear(x, y, NavRX[nx], NavRY[nx], clear) < clear)
	    break;
	*cx = NavRX[nx];
	*cy = NavRY[nx];
	cur = nx;
    }
    return true;
}

static uint32_t Rb_course_sig(void)
{
    uint32_t h = 2166136261u;
    int i;

    for (i = 0; i < world->NumChecks; i++) {
	check_t *ch = Check_by_index(i);

	h = (h ^ (uint32_t)(ch->pos.cx / CLICK)) * 16777619u;
	h = (h ^ (uint32_t)(ch->pos.cy / CLICK)) * 16777619u;
    }
    return h;
}

static void Rb_learn_load(void);

static bool Rb_map_build(void)
{
    long blocks = (long)world->x * world->y;
    int nb = 1, x, y, c, i, j, d;
    uint8_t *obs;

    if (Learn.dirty)
	Rb_learn_save();
    Rb_map_free();
    RbPdata = pdata;
    RbNumPolys = num_polys;
    RbMapW = world->cwidth;
    RbMapH = world->cheight;
    RbNumChecks = world->NumChecks;

    while (blocks / ((long)nb * nb) > 90000)
	nb++;
    RbWrap = BIT(world->rules->mode, WRAP_PLAY) ? true : false;
    RbW = (double)world->cwidth / CLICK;
    RbH = (double)world->cheight / CLICK;
    NavF = 5;
    NavW = MAX(1, (int)(RbW / (BLOCK_SZ * nb) + 0.5));
    NavH = MAX(1, (int)(RbH / (BLOCK_SZ * nb) + 0.5));
    NavN = NavW * NavH;
    NavCX = RbW / NavW;
    NavCY = RbH / NavH;
    FineW = NavW * NavF;
    FineH = NavH * NavF;
    FineCX = NavCX / NavF;
    FineCY = NavCY / NavF;
    FineStep = MIN(FineCX, FineCY);
    CheckR = options.checkpointRadius * BLOCK_SZ;

    ShipClear = malloc(sizeof(float) * (size_t)FineW * FineH);
    obs = malloc((size_t)FineW * FineH);
    NavRX = malloc(sizeof(float) * NavN);
    NavRY = malloc(sizeof(float) * NavN);
    NavClear = malloc(sizeof(float) * NavN);
    HeapCap = NavN * 8 + 64;
    Heap = malloc(sizeof(rb_heap_t) * HeapCap);
    NavEdge = calloc(NavN, 1);
    NavCost = malloc(sizeof(float) * NavN * 8);
    NavCostG = malloc(sizeof(float) * NavN * 8);
    for (d = 0; d <= RB_DEPTH; d++) {
	CheckF[d] = calloc(MAX(RbNumChecks, 1), sizeof(rb_field_t));
	DetourF[d] = calloc(MAX(RbNumChecks, 1), sizeof(rb_field_t));
    }
    NumFuelF = Num_fuels();
    FuelF = calloc(MAX(NumFuelF, 1), sizeof(rb_field_t));
    if (!ShipClear || !obs || !NavRX || !NavRY || !NavClear || !Heap
	|| !NavEdge || !NavCost || !NavCostG || !CheckF[0] || !CheckF[1] || !CheckF[2]
	|| !CheckF[RB_DEPTH] || !DetourF[0] || !DetourF[1] || !DetourF[2]
	|| !DetourF[RB_DEPTH] || !FuelF) {
	free(obs);
	Rb_map_free();
	return false;
    }

    /* Rasterize the walls ships hit */
    for (y = 0; y < FineH; y++) {
	for (x = 0; x < FineW; x++) {
	    int cx = (int)(((x + 0.5) * FineCX) * CLICK);
	    int cy = (int)(((y + 0.5) * FineCY) * CLICK);

	    LIMIT(cx, 0, world->cwidth - 1);
	    LIMIT(cy, 0, world->cheight - 1);
	    obs[y * FineW + x] = is_inside(cx, cy, NONBALL_BIT, NULL) != NO_GROUP;
	}
    }
    /* thin walls fall between the cell centres: walk the edges too */
    for (i = 0; i < num_polys; i++) {
	const poly_t *p = &pdata[i];
	group_t *gp = groupptr_by_id(p->group);
	const int *e = edgeptr + p->edges;
	double ex = Rb_px(p->pos.cx), ey = Rb_px(p->pos.cy);

	if (p->is_decor || (gp != NULL && (gp->hitmask & NONBALL_BIT)))
	    continue;
	for (j = 0; j < p->num_points; j++) {
	    double dx = Rb_px(e[2 * j]), dy = Rb_px(e[2 * j + 1]);
	    int k, n = (int)(LENGTH(dx, dy) / (0.5 * FineStep)) + 1;

	    for (k = 0; k <= n; k++) {
		int fi = Rb_fine_index(ex + dx * k / n, ey + dy * k / n);

		if (fi >= 0)
		    obs[fi] = 1;
	    }
	    ex += dx;
	    ey += dy;
	}
    }
    if (!Rb_edt(obs, ShipClear)) {
	free(obs);
	Rb_map_free();
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
	if (NavClear[c] < RB_PASS_R)
	    continue;
	for (j = 0; j < 8; j++) {
	    int n = Rb_nav_nb(c, nb_dx[j], nb_dy[j]);
	    double m, len, pen;

	    if (n < 0 || NavClear[n] < RB_PASS_R)
		continue;
	    m = Rb_segment_clear(NavRX[c], NavRY[c], NavRX[n], NavRY[n], RB_EDGE_R);
	    if (m < RB_EDGE_R)
		continue;
	    len = Rb_dist(NavRX[c], NavRY[c], NavRX[n], NavRY[n]);
	    pen = MAX(0.0, (RB_PREF_R - m) / RB_PREF_R);
	    NavEdge[c] |= (uint8_t)(1 << j);
	    NavCost[c * 8 + j] = (float)(len * (1.0 + RB_PREF_W * pen * pen));
	    NavCostG[c * 8 + j] = NavCost[c * 8 + j];
	    if (world->gravity != NULL) {
		int bx = (int)(NavRX[n] / BLOCK_SZ), by = (int)(NavRY[n] / BLOCK_SZ);
		double g;

		LIMIT(bx, 0, world->x - 1);
		LIMIT(by, 0, world->y - 1);
		g = LENGTH(world->gravity[bx][by].x, world->gravity[bx][by].y);
		if (g > 0.5)
		    NavCostG[c * 8 + j] *= (float)(1.0 + 2.0 * MIN(g, 8.0));
	    }
	}
    }

    /*
     * Course values: to checkpoint i, then on through the next d ones.  The
     * disk of i starts with the value of going on from each of its points,
     * so passing it is continuous and the paths show the racing line.
     */
    for (d = 0; d <= RB_DEPTH; d++) {
	for (i = 0; i < RbNumChecks; i++) {
	    check_t *ch = Check_by_index(i);
	    const rb_field_t *then = d > 0 ? &CheckF[d - 1][(i + 1) % RbNumChecks] : NULL;

	    if (!Rb_field_compute(&CheckF[d][i], Rb_px(ch->pos.cx), Rb_px(ch->pos.cy),
				  MAX(CheckR - 12.0, 8.0), then, NULL)) {
		Rb_map_free();
		return false;
	    }
	}
    }
    for (d = 0; d < RB_DEPTH; d++) {
	for (i = 0; i < RbNumChecks; i++) {
	    free(CheckF[d][i].next);
	    CheckF[d][i].next = NULL;
	}
    }
    RbMapOk = true;
    Rb_learn_load();
    RB_LOG("[racebot] map analysed: fine %dx%d (%.1f px), nav %dx%d, %d checks, "
	   "radius %.0f\n", FineW, FineH, FineCX, NavW, NavH, RbNumChecks, CheckR);
    return true;
}

static bool Rb_map_update(void)
{
    bool same = RbPdata == pdata && RbNumPolys == num_polys
	&& RbMapW == world->cwidth && RbMapH == world->cheight
	&& RbNumChecks == world->NumChecks;

    if (same)
	return RbMapOk;	/* failed before: don't retry every tick */
    return Rb_map_build();
}

static rb_field_t *Rb_fuel_field(int fs)
{
    fuel_t *f;

    if (fs < 0 || fs >= NumFuelF || !RbMapOk)
	return NULL;
    if (!FuelF[fs].valid) {
	f = Fuel_by_index(fs);
	if (!Rb_field_compute(&FuelF[fs], Rb_px(f->pos.cx), Rb_px(f->pos.cy), 0.0, NULL, NULL))
	    return NULL;
    }
    return &FuelF[fs];
}

/* Course value avoiding strong gravity (see DetourF), NULL on failure. */
static const rb_field_t *Rb_detour_field(int d, int i)
{
    rb_field_t *fd;

    if (!RbMapOk || d < 0 || d > RB_DEPTH || i < 0 || i >= RbNumChecks)
	return NULL;
    fd = &DetourF[d][i];
    if (!fd->valid) {
	check_t *ch = Check_by_index(i);
	const rb_field_t *then = NULL;

	if (d > 0 && (then = Rb_detour_field(d - 1, (i + 1) % RbNumChecks)) == NULL)
	    return NULL;
	if (!Rb_field_compute(fd, Rb_px(ch->pos.cx), Rb_px(ch->pos.cy),
			      MAX(CheckR - 12.0, 8.0), then, NavCostG))
	    return NULL;
	if (d < RB_DEPTH) {
	    free(fd->next);	/* only the deepest give the line */
	    fd->next = NULL;
	}
    }
    return fd;
}

bool Robot_race_map(void)
{
    return BIT(world->rules->mode, TIMING) && world->NumChecks > 0
	&& !options.ballrace;
}

/* ------------------------------------------------------------------------
 * Learning: per map and segment, kept in a small text file
 * ------------------------------------------------------------------------ */

static const char *Rb_learn_path(bool mkdirs)
{
    static char path[PATH_MAX];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
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

    if (xdg && xdg[0] == '/')
	snprintf(path, sizeof(path), "%s/" RB_LEARN_SUBDIR "/%s.txt", xdg, name);
    else if (home && home[0])
	snprintf(path, sizeof(path), "%s/.local/share/" RB_LEARN_SUBDIR "/%s.txt",
		 home, name);
    else
	return NULL;
    if (mkdirs) {
	for (p = path + 1; (p = strchr(p, '/')) != NULL; p++) {
	    *p = '\0';
	    mkdir(path, 0755);
	    *p = '/';
	}
    }
    return path;
}

static void Rb_learn_reset(void)
{
    int i;

    Learn.best_lap = 0.0;
    for (i = 0; i < Learn.nchecks; i++) {
	Learn.seg[i].time = 0.0;
	Learn.seg[i].best = 0.0;
	Learn.seg[i].fuel = -1.0;
	Learn.seg[i].runs = 0;
    }
}

static void Rb_learn_load(void)
{
    const char *path;
    FILE *fp;
    char line[256];

    free(Learn.seg);
    memset(&Learn, 0, sizeof(Learn));
    strlcpy(Learn.map, world->name, sizeof(Learn.map));
    Learn.sig = Rb_course_sig();
    Learn.nchecks = world->NumChecks;
    Learn.seg = calloc(MAX(Learn.nchecks, 1), sizeof(rb_seg_t));
    if (Learn.seg == NULL)
	return;
    Learn.loaded = true;
    Rb_learn_reset();

    path = Rb_learn_path(false);
    if (path == NULL || (fp = fopen(path, "r")) == NULL)
	return;
    if (fgets(line, sizeof(line), fp) != NULL) {
	int ver, n;
	unsigned int sig;
	double best;

	if (sscanf(line, "racebot %d %u %d %lf", &ver, &sig, &n, &best) == 4
	    && ver == RB_LEARN_VERSION && sig == Learn.sig && n == Learn.nchecks) {
	    Learn.best_lap = best;
	    while (fgets(line, sizeof(line), fp) != NULL) {
		int i, runs;
		double t, b, f;

		if (sscanf(line, "%d %lf %lf %lf %d", &i, &t, &b, &f, &runs) != 5
		    || i < 0 || i >= n)
		    continue;
		Learn.seg[i].time = t;
		Learn.seg[i].best = b;
		Learn.seg[i].fuel = f;
		Learn.seg[i].runs = runs;
	    }
	    RB_LOG("[racebot] learned data loaded from %s\n", path);
	}
    }
    fclose(fp);
}

static void Rb_learn_save(void)
{
    const char *path;
    char tmp[PATH_MAX + 8];
    FILE *fp;
    bool ok = true;
    int i;

    if (!Learn.loaded || Learn.seg == NULL)
	return;
    path = Rb_learn_path(true);
    if (path == NULL)
	return;
    /* write a temporary file and rename it, a crash never leaves half a file */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if ((fp = fopen(tmp, "w")) == NULL)
	return;
    if (fprintf(fp, "racebot %d %u %d %.1f\n", RB_LEARN_VERSION, Learn.sig,
		Learn.nchecks, Learn.best_lap) < 0)
	ok = false;
    for (i = 0; i < Learn.nchecks && ok; i++) {
	const rb_seg_t *s = &Learn.seg[i];

	if (fprintf(fp, "%d %.1f %.1f %.2f %d\n", i, s->time, s->best, s->fuel,
		    s->runs) < 0)
	    ok = false;
    }
    if (fclose(fp) != 0)
	ok = false;
    if (ok && rename(tmp, path) == 0)
	Learn.dirty = false;
    else
	remove(tmp);
}

/* A checkpoint was passed: learn from the segment that led to it. */
static void Rb_segment_done(player_t *pl, rb_data_t *d, int seg)
{
    rb_seg_t *s;
    double t, fuel;

    if (!Learn.loaded || !d->learn || !d->seg_valid || seg < 0 || seg >= Learn.nchecks)
	return;
    s = &Learn.seg[seg];
    t = (double)(RbTick - d->seg_start);
    fuel = d->seg_fuel0 - pl->fuel.sum;
    /* refuelling hides the use, a hit or a ram is no regular use */
    if (d->refuel_ticks <= 0.0 && fuel >= 0.0
	&& (s->fuel < 0.0 || fuel < 3.0 * s->fuel + 5.0))
	s->fuel = s->fuel < 0.0 ? fuel : 0.7 * s->fuel + 0.3 * fuel;
    if (s->runs == 0) {
	s->time = t;
	s->best = t;
    } else {
	s->time = 0.8 * s->time + 0.2 * t;
	s->best = MIN(s->best, t);
    }
    s->runs++;
    Learn.dirty = true;
    RB_LOG("[racebot] %s seg %d: %.0f ticks (avg %.1f best %.0f) fuel %.2f\n",
	   pl->name, seg, t, s->time, s->best, fuel);
}

static void Rb_segment_start(rb_data_t *d)
{
    d->seg_start = RbTick;
    d->refuel_ticks = 0.0;
}

/* ------------------------------------------------------------------------
 * Race state
 * ------------------------------------------------------------------------ */

/* Checkpoint passes still needed to finish the race. */
static int Rb_passes_left(player_t *pl)
{
    int n = world->NumChecks, done;

    /* the round counts up at checkpoint 0, after the lap's last one */
    if (pl->round <= 0)
	done = 0;
    else
	done = (pl->round - 1) * n + (pl->check == 0 ? n : pl->check);
    return MAX(options.raceLaps * n + 1 - done, 0);
}

/* Fuel needed for the rest of the race, from what the segments used. */
static double Rb_fuel_needed(player_t *pl)
{
    int left = Rb_passes_left(pl), n = world->NumChecks, i;
    double need = 0.0;

    for (i = 0; i < left; i++) {
	int seg = (pl->check + i) % n;
	double f = Learn.loaded ? Learn.seg[seg].fuel : -1.0;

	if (f < 0.0) {
	    /*
	     * Not measured yet: thrusting all the way at a typical racing
	     * speed.  Too low a guess is no harm, the first lap measures it.
	     */
	    check_t *a = Check_by_index((seg + n - 1) % n);
	    double len = Rb_field_value(&CheckF[0][seg], Rb_px(a->pos.cx), Rb_px(a->pos.cy));

	    f = MIN(len, 1e4) / 35.0 * MAX_PLAYER_POWER * 0.0008 + 0.5;
	}
	need += f;
    }
    return need;
}

/* ------------------------------------------------------------------------
 * Physics model and the sampling flight controller
 * ------------------------------------------------------------------------ */

/* Farthest point of the ship shape from its centre (px), at least SHIP_SZ. */
static double Rb_ship_radius(player_t *pl)
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

/*
 * How far below level 100: 0 at level 100, 1 at level 1.  Rising faster
 * than linear from level 100 down, so middle levels feel it too.
 */
static double Rb_unskill(int level)
{
    return pow((100 - level) / 99.0, 0.7);
}

/* Thrust power of the skill level; the model uses what the ship gets. */
static double Rb_power(int level)
{
    return MAX_PLAYER_POWER * (1.0 - RB_POWER_LOSS * Rb_unskill(level));
}

static double Rb_thrust_acc(player_t *pl)
{
    double power = pl->power > 0.0 ? pl->power : MAX_PLAYER_POWER;
    int a = pl->item[ITEM_AFTERBURNER];

    if (pl->fuel.sum <= 0.0)
	power = MIN_PLAYER_POWER * 0.6;
    else if (a)
	power = AFTER_BURN_POWER(power, a);
    return power / MAX(pl->mass, 1.0);
}

/* Outward wall normal at (x, y) (absolute) from the clearance grid. */
static bool Rb_wall_normal(double x, double y, double *nx, double *ny)
{
    int a = Rb_fine_index(x + FineStep, y), b = Rb_fine_index(x - FineStep, y);
    int c = Rb_fine_index(x, y + FineStep), e = Rb_fine_index(x, y - FineStep);
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

/*
 * Wall contact: below the bounce speed limit the ship bounces as in the
 * server (walls.c), above it crashes.  Moves the ship back to (px, py).
 */
static void Rb_ship_wall(const rb_ctx_t *cx, rb_state_t *s, double px, double py)
{
    double nx, ny, vn, vt, tx, ty, b = options.playerWallBounceBrakeFactor, ch, sp;
    int i = Rb_fine_index(cx->ox + s->x, cx->oy + s->y);

    if (i >= 0 && ShipClear[i] >= cx->ship_r)
	return;
    sp = LENGTH(s->vx, s->vy);
    if (i < 0 || sp > cx->max_bounce) {
	s->crashed = true;
	return;
    }
    if (!Rb_wall_normal(cx->ox + s->x, cx->oy + s->y, &nx, &ny))
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
    if (sp > options.maxUnshieldedWallBounceSpeed)
	s->hard_hits++;
}

static inline void Rb_gravity(double x, double y, double *gx, double *gy)
{
    int bx = (int)(x / BLOCK_SZ), by = (int)(y / BLOCK_SZ);

    if (RbWrap) {
	bx = (bx % world->x + world->x) % world->x;
	by = (by % world->y + world->y) % world->y;
    }
    LIMIT(bx, 0, world->x - 1);
    LIMIT(by, 0, world->y - 1);
    *gx = world->gravity[bx][by].x;
    *gy = world->gravity[bx][by].y;
}

/* One robot tick of the model, frame by frame as the server moves ships. */
static void Rb_sim_tick(const rb_ctx_t *cx, rb_state_t *s, bool thrust)
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

	    Rb_gravity(cx->ox + s->x, cx->oy + s->y, &gx, &gy);
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
	Rb_ship_wall(cx, s, px, py);
	if (s->crashed)
	    return;
    }
}

/* Field the rollout state steers for: the next checkpoint or the station. */
static inline const rb_field_t *Rb_cur_field(const rb_ctx_t *cx, const rb_state_t *s)
{
    if (!cx->race)
	return cx->pfield;
    return cx->pf[MIN(s->k, RB_CHAIN - 1)];
}

/*
 * Racing policy used after a candidate's own actions run out: fly at the
 * carrot (the farthest visible point on the way), as fast as still allows
 * slowing down to the corner speed there.
 */
static void Rb_default_action(const rb_ctx_t *cx, const rb_state_t *s,
			      double carx, double cary, bool car_goal,
			      int *want, bool *thrust)
{
    double dx = carx - s->x, dy = cary - s->y, dist = LENGTH(dx, dy);
    double vdes, ax, ay, amag;
    int w;

    if (car_goal && cx->race && s->k + 1 < cx->left)
	vdes = cx->vmax;	/* a checkpoint to pass through */
    else if (car_goal && !cx->race)
	vdes = MIN(cx->vmax, sqrt(2.0 * cx->acc * 0.5 * MAX(dist - cx->arrive_r * 0.5, 0.0)));
    else
	vdes = MIN(cx->vmax, sqrt(RB_CORNER_V * RB_CORNER_V + 2.0 * cx->acc * RB_BRAKE * dist));
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
    w = Rb_dir_of(ax, ay);
    *want = w;
    *thrust = ABS(Rb_angle_diff(w, s->dir)) <= 10;
}

/* Free distance along (ux, uy) from (x, y) (relative), up to max. */
static double Rb_ray_free(const rb_ctx_t *cx, double x, double y,
			  double ux, double uy, double need, double max)
{
    double t;

    for (t = 0.0; t < max; t += 2.0 * FineStep) {
	if (Rb_ship_clear(cx->ox + x + ux * t, cx->oy + y + uy * t) < need)
	    return t;
    }
    return max;
}

/* Squared closest distance during one tick between two moving points. */
static inline double Rb_cpa2(double ax0, double ay0, double ax1, double ay1,
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

static double Rb_shot_miss(int dir, double rx, double ry, double wvx, double wvy,
			   double speed, double life, double *when)
{
    double ux = speed * tcos(dir) - wvx, uy = speed * tsin(dir) - wvy;
    double uu = ux * ux + uy * uy, t;

    if (uu < 1e-6)
	return 1e9;
    t = (rx * ux + ry * uy) / uu;
    if (when)
	*when = t;
    if (t < 0.0 || t > life)
	return 1e9;
    return LENGTH(rx - ux * t, ry - uy * t);
}

/* Inside the disk of checkpoint 'ci' (relative position)? */
static inline bool Rb_in_check(const rb_ctx_t *cx, int ci, double x, double y)
{
    check_t *ch = Check_by_index(ci);
    double dx = Rb_wdx(cx->ox + x - Rb_px(ch->pos.cx));
    double dy = Rb_wdy(cx->oy + y - Rb_px(ch->pos.cy));

    return dx * dx + dy * dy < cx->check_r2;
}

/* Simulate a candidate and return its cost; records the actions taken. */
static double Rb_rollout(const rb_ctx_t *cx, const rb_cand_t *cand, rb_cand_t *rec,
			 rb_flags_t *fl)
{
    rb_state_t s = cx->s0;
    double cost = 0.0, need = cx->ship_r + cx->margin;
    double carx = 0.0, cary = 0.0;
    bool car_goal = false, have_car = false;
    int t, i, hits = 0, hard = 0, car_k = -1;

    need = MIN(need, cx->clear0 - 1.0);
    if (fl)
	fl->shot_t = fl->ram_t = fl->hard_t = -1;
    if (fl)
	fl->end_value = 1e9;

    for (t = 0; t < RB_H; t++) {
	int want;
	bool thr;
	double clear, px0 = s.x, py0 = s.y;

	if (t < cand->len) {
	    want = cand->dir[t];
	    thr = cand->thr[t];
	} else {
	    /* the carrot moves on every few ticks or after a checkpoint */
	    if (!have_car || (t & 3) == 0 || car_k != s.k) {
		const rb_field_t *fd = Rb_cur_field(cx, &s);
		double ax = cx->ox + s.x, ay = cx->oy + s.y, rx, ry;

		if (fd && Rb_field_carrot(fd, ax, ay, RB_SHIP_R + 6.0, 10, &rx, &ry,
					  &car_goal)) {
		    carx = Rb_wdx(rx - cx->ox);
		    cary = Rb_wdy(ry - cx->oy);
		} else if (!cx->race) {
		    carx = Rb_wdx(cx->gx - cx->ox);
		    cary = Rb_wdy(cx->gy - cx->oy);
		    car_goal = true;
		}
		have_car = true;
		car_k = s.k;
	    }
	    Rb_default_action(cx, &s, carx, cary, car_goal, &want, &thr);
	}
	s.dir = Rb_turn_toward(s.dir, want, cx->turn);
	rec->dir[t] = (short)s.dir;
	rec->thr[t] = thr;
	Rb_sim_tick(cx, &s, thr);

	if (s.crashed
	    || Rb_ship_clear(cx->ox + 0.5 * (s.x + px0), cx->oy + 0.5 * (s.y + py0)) < 2.0) {
	    cost += 1e6 * (1.0 + (double)(RB_H - t) / RB_H);
	    return cost;
	}
	if (s.hits > hits) {
	    cost += RB_BOUNCE_COST * (s.hits - hits);
	    hits = s.hits;
	}
	if (s.hard_hits > hard) {
	    /* only survivable with the shield up: costs fuel and risk */
	    cost += RB_HARD_COST * (s.hard_hits - hard);
	    hard = s.hard_hits;
	    if (fl && fl->hard_t < 0)
		fl->hard_t = t;
	}
	clear = MAX(Rb_ship_clear(cx->ox + s.x, cx->oy + s.y), cx->ship_r);
	if (clear < need + 8.0)
	    cost += sqr(need + 8.0 - clear) * 0.5;

	/* checkpoints */
	if (cx->race) {
	    while (s.k < RB_CHAIN && s.k < cx->left
		   && (Rb_in_check(cx, cx->tgt[s.k], s.x, s.y)
		       || Rb_in_check(cx, cx->tgt[s.k], 0.5 * (s.x + px0),
				      0.5 * (s.y + py0)))) {
		s.k++;
		if (s.k >= cx->left) {
		    if (fl)
			fl->end_value = -1e9;
		    return cost - (RB_H - t) * RB_FINISH_V;
		}
	    }
	    /* rare: more checkpoints than the chain within the horizon */
	    if (s.k >= RB_CHAIN) {
		if (fl)
		    fl->end_value = -1e9;
		return cost - (RB_H - t) * RB_FINISH_V;
	    }
	}
	/*
	 * Progress made early counts: without this, plans that dawdle and
	 * reach the same point at the horizon look just as good.
	 */
	if (cx->race)
	    cost += RB_RUN_W * Rb_field_value(cx->vf[s.k], cx->ox + s.x, cx->oy + s.y);
	else if (cx->pfield)
	    cost += RB_RUN_W * MAX(0.0, Rb_field_value(cx->pfield, cx->ox + s.x,
							cx->oy + s.y) - cx->arrive_r);

	/* aiming at an attacker: pointing at him early is worth most */
	if (cx->has_target && t < 4) {
	    int err = ABS(Rb_angle_diff(s.dir, cx->lead_dir));

	    cost += cx->aim_w * 10.0 * MIN(err, 32);
	}

	if (t >= RB_NEAR_H)
	    continue;
	/* enemy shots, checked along the whole tick */
	for (i = 0; i < cx->nshots; i++) {
	    const rb_shot_t *sh = &cx->shots[i];
	    double t0 = t, t1 = t + 1.0;

	    if (t0 < sh->t0 || t0 - sh->t0 > sh->life)
		continue;
	    t0 -= sh->t0;
	    t1 -= sh->t0;
	    if (Rb_cpa2(px0, py0, s.x, s.y, sh->x + sh->vx * t0, sh->y + sh->vy * t0,
			sh->x + sh->vx * t1, sh->y + sh->vy * t1)
		< sqr(RB_SHIP_R + 4.0 + cx->margin)) {
		cost += sh->weight * (1.0 - 0.5 * t / RB_NEAR_H);
		if (fl && fl->shot_t < 0 && sh->t0 == 0.0)
		    fl->shot_t = t;
		break;
	    }
	}
	/*
	 * Enemy ships: collisions kill or cost fuel.  They may speed up or
	 * turn any moment, so the farther ahead, the wider the berth.
	 */
	for (i = 0; i < cx->nen; i++) {
	    const rb_enemy_t *e = &cx->en[i];
	    double tt = t + 1.0, ex = e->x + e->vx * tt, ey = e->y + e->vy * tt;
	    double d2 = Rb_cpa2(px0, py0, s.x, s.y, ex - e->vx, ey - e->vy, ex, ey);
	    double hit = cx->ship_r + RB_SHIP_R + 8.0;
	    double may = hit + MIN(0.5 * cx->acc * tt * tt, 45.0);

	    if (d2 < sqr(hit)) {
		cost += cx->ram_cost * (1.0 - 0.5 * t / RB_NEAR_H);
		if (fl && fl->ram_t < 0)
		    fl->ram_t = t;
	    } else if (d2 < sqr(may))
		cost += cx->ram_cost * 0.15 * (1.0 - 0.5 * t / RB_NEAR_H);
	    else if (d2 < sqr(may + 50.0))
		cost += (may + 50.0 - sqrt(d2)) * 2.0;
	}
    }

    /* terminal cost: the course value reached */
    {
	const rb_field_t *fd = Rb_cur_field(cx, &s);
	double v = LENGTH(s.vx, s.vy), ctg, dneed, free;

	if (cx->race)
	{
	    ctg = Rb_field_value(cx->vf[MIN(s.k, RB_CHAIN - 1)], cx->ox + s.x, cx->oy + s.y);
	    if (fl) {
		fl->end_value = ctg;
		fl->end_x = cx->ox + s.x;
		fl->end_y = cx->oy + s.y;
		fl->end_speed = v;
	    }
	}
	else {
	    double gd;

	    ctg = fd ? Rb_field_value(fd, cx->ox + s.x, cx->oy + s.y)
		     : Rb_dist(cx->ox + s.x, cx->oy + s.y, cx->gx, cx->gy);
	    ctg = MAX(0.0, ctg - cx->arrive_r);
	    gd = Rb_dist(cx->ox + s.x, cx->oy + s.y, cx->gx, cx->gy);
	    if (gd < cx->arrive_r * 2.0 + 60.0)
		cost += (s.vx * s.vx + s.vy * s.vy) * 6.0;
	}
	cost += ctg;
	/* speed towards the carrot is distance about to be made good */
	if (cx->race && have_car && v > 0.5) {
	    double dx = carx - s.x, dy = cary - s.y, dl = LENGTH(dx, dy);

	    if (dl > 1.0)
		cost -= RB_KV * (s.vx * dx + s.vy * dy) / dl;
	}
	/* still able to stop (or turn) before the next wall? */
	if (v > 2.0) {
	    dneed = v * v / (2.0 * cx->acc) + v * (RES / 2.0) / cx->turn * 0.5;
	    free = Rb_ray_free(cx, s.x, s.y, s.vx / v, s.vy / v, need + 2.0, dneed + 30.0);
	    if (free < dneed && v > cx->max_bounce * 0.8)
		cost += (dneed - free) * 2.0;
	}
    }
    return cost;
}

static void Rb_cand_set(rb_cand_t *c, int dir, bool thr, int len)
{
    int i;

    c->len = len;
    for (i = 0; i < len; i++) {
	c->dir[i] = (short)dir;
	c->thr[i] = thr;
    }
}

static bool Rb_lead(double rx, double ry, double wvx, double wvy, double s,
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

static bool Rb_is_threat(const rb_data_t *d, const player_t *e)
{
    return e->id >= 0 && e->id < NUM_IDS && d->threat_time[e->id] > 0
	&& RbTick - d->threat_time[e->id] < RB_THREAT_TICKS;
}

/*
 * Choose this tick's heading and thrust.  'pit' >= 0 flies to that fuel
 * station instead of racing.
 */
static rb_flags_t Rb_fly(player_t *pl, rb_data_t *d, int level, int pit,
			 player_t *target)
{
    static rb_ctx_t cx;
    static rb_cand_t cand[RB_MAX_CAND];
    rb_cand_t rec, best_rec;
    rb_flags_t fl = { false, 0, false, -1, -1, -1, 1e9, 0.0, 0.0, 0.0, 99.0 };
    double best_cost = 1e30, f = level / 100.0;
    int nc = 0, i, h, nheads, prev_i = -1;
    int specials[8], nspec = 0;
    double vis = VISIBILITY_DISTANCE;

    memset(&cx.s0, 0, sizeof(cx.s0));
    cx.pl = pl;
    cx.d = d;
    cx.ox = Rb_px(pl->pos.cx);
    cx.oy = Rb_px(pl->pos.cy);
    cx.s0.vx = pl->vel.x;
    cx.s0.vy = pl->vel.y;
    cx.s0.dir = pl->dir;
    cx.acc = Rb_thrust_acc(pl);
    cx.ship_r = Rb_ship_radius(pl);
    cx.turn = Rb_turn_rate(level);
    cx.frames = MAX(1, (int)(1.0 / timeStep + 0.5));
    cx.dt = 1.0 / cx.frames;
    cx.fric_keep = pow(MAX(1.0 - options.frictionSetting, 0.0), cx.dt);
    cx.grav = world->gravity != NULL && BIT(pl->obj_status, GRAVITY);
    cx.margin = 1.0 + (1.0 - f) * 6.0;
    cx.clear0 = Rb_ship_clear(cx.ox, cx.oy);
    cx.max_bounce = options.maxUnshieldedWallBounceSpeed;
    if (BIT(pl->have, HAS_SHIELD) && pl->fuel.sum > 20.0)
	cx.max_bounce = MAX(cx.max_bounce, options.maxShieldedWallBounceSpeed);
    cx.max_bounce *= 0.92;
    /* ships crash (deadly without a shield) or bounce, which costs fuel */
    if (BIT(world->rules->mode, CRASH_WITH_PLAYER))
	cx.ram_cost = 5e5;
    else if (BIT(world->rules->mode, BOUNCE_WITH_PLAYER))
	cx.ram_cost = 5e3 + options.playerCollisionFuelDrain * 300.0;
    else
	cx.ram_cost = 0.0;

    cx.race = pit < 0;
    cx.pfield = NULL;
    cx.arrive_r = 0.0;
    if (cx.race) {
	cx.left = Rb_passes_left(pl);
	cx.check_r2 = sqr(MAX(CheckR - 4.0, 4.0));
	for (i = 0; i < RB_CHAIN; i++)
	    cx.tgt[i] = (pl->check + i) % world->NumChecks;
	/*
	 * After passing k checkpoints the value looks RB_DEPTH - k further
	 * (as far as the race goes): the value stays continuous at a pass.
	 */
	for (i = 0; i < RB_CHAIN; i++) {
	    int dep = MIN(RB_DEPTH - i, cx.left - 1 - i);

	    dep = MAX(dep, 0);
	    cx.vf[i] = &CheckF[dep][cx.tgt[i]];
	    cx.pf[i] = &CheckF[RB_DEPTH][cx.tgt[i]];
	    /* too slow for the gravs on the short way: around them */
	    if (d->detour && i < 2) {
		const rb_field_t *v = Rb_detour_field(dep, cx.tgt[i]);
		const rb_field_t *l = Rb_detour_field(RB_DEPTH, cx.tgt[i]);

		if (v != NULL && l != NULL) {
		    cx.vf[i] = v;
		    cx.pf[i] = l;
		}
	    }
	}
    } else {
	fuel_t *fs = Fuel_by_index(pit);

	cx.pfield = Rb_fuel_field(pit);
	cx.gx = Rb_px(fs->pos.cx);
	cx.gy = Rb_px(fs->pos.cy);
	cx.arrive_r = 25.0;
    }
    cx.vmax = cx.race ? 80.0 : 22.0;

    /* enemy shots in view */
    cx.nshots = 0;
    for (i = 0; i < NumObjs && cx.nshots < RB_MAX_SHOTS; i++) {
	object_t *o = Obj[i];
	double rx, ry, dvx, dvy, tc, dmin;

	if (o->type != OBJ_SHOT && o->type != OBJ_CANNON_SHOT
	    && o->type != OBJ_TORPEDO && o->type != OBJ_SMART_SHOT
	    && o->type != OBJ_HEAT_SHOT && o->type != OBJ_MINE)
	    continue;
	if (o->id == pl->id
	    || (options.teamImmunity && o->team == pl->team
		&& BIT(world->rules->mode, TEAM_PLAY)))
	    continue;
	rx = Rb_wdx(Rb_px(o->pos.cx) - cx.ox);
	ry = Rb_wdy(Rb_px(o->pos.cy) - cx.oy);
	if (LENGTH(rx, ry) > vis)
	    continue;
	dvx = o->vel.x - pl->vel.x;
	dvy = o->vel.y - pl->vel.y;
	tc = -(rx * dvx + ry * dvy) / MAX(dvx * dvx + dvy * dvy, 1e-6);
	LIMIT(tc, 0.0, (double)RB_NEAR_H);
	dmin = LENGTH(rx + dvx * tc, ry + dvy * tc);
	if (dmin > 160.0 + RB_NEAR_H * 2.0 * cx.acc)
	    continue;
	cx.shots[cx.nshots].x = rx;
	cx.shots[cx.nshots].y = ry;
	cx.shots[cx.nshots].vx = o->vel.x;
	cx.shots[cx.nshots].vy = o->vel.y;
	cx.shots[cx.nshots].life = o->life;
	cx.shots[cx.nshots].t0 = 0.0;
	cx.shots[cx.nshots].weight = 4e4;
	cx.nshots++;
    }

    /* enemy ships close by; the reaction delay depends on the level */
    cx.nen = 0;
    cx.has_target = false;
    {
	int delay = (int)((1.0 - f) * 4.0 + 0.5);

	for (i = 0; i < NumPlayers && cx.nen < RB_MAX_ENEMIES; i++) {
	    player_t *e = Player_by_index(i);
	    double rx, ry;

	    if (Rb_ally(e, pl) || !Rb_player_active(e))
		continue;
	    rx = Rb_wdx(Rb_px(e->pos.cx) - e->vel.x * delay - cx.ox);
	    ry = Rb_wdy(Rb_px(e->pos.cy) - e->vel.y * delay - cx.oy);
	    if (LENGTH(rx, ry) > 900.0)
		continue;
	    cx.en[cx.nen].x = rx;
	    cx.en[cx.nen].y = ry;
	    cx.en[cx.nen].vx = e->vel.x;
	    cx.en[cx.nen].vy = e->vel.y;
	    cx.en[cx.nen].dir = e->dir;
	    cx.nen++;
	    if (e == target) {
		cx.has_target = true;
		cx.tx = rx;
		cx.ty = ry;
		cx.tvx = e->vel.x;
		cx.tvy = e->vel.y;
	    }
	}
    }
    /* an attacker pointing our way may fire any moment: leave his line */
    for (i = 0; i < NumPlayers && cx.nshots < RB_MAX_SHOTS - 3; i++) {
	player_t *e = Player_by_index(i);
	double rx, ry, dist;
	int k;

	if (Rb_ally(e, pl) || !Rb_player_active(e) || !Rb_is_threat(d, e))
	    continue;
	rx = Rb_wdx(Rb_px(e->pos.cx) - cx.ox);
	ry = Rb_wdy(Rb_px(e->pos.cy) - cx.oy);
	dist = LENGTH(rx, ry);
	if (dist > 700.0 || ABS(Rb_angle_diff(e->dir, Rb_dir_of(-rx, -ry))) > 12)
	    continue;
	for (k = 0; k < 3; k++) {
	    rb_shot_t *sh = &cx.shots[cx.nshots++];

	    sh->t0 = 2.0 * k;
	    sh->x = rx + e->vel.x * sh->t0;
	    sh->y = ry + e->vel.y * sh->t0;
	    sh->vx = e->vel.x + options.shotSpeed * tcos(e->dir);
	    sh->vy = e->vel.y + options.shotSpeed * tsin(e->dir);
	    sh->life = options.shotLife;
	    sh->weight = 1.5e3;
	}
    }
    cx.aim_w = 0.0;
    if (cx.has_target) {
	double ldir, tof;

	cx.aim_w = 0.25;
	if (Rb_lead(cx.tx, cx.ty, cx.tvx - pl->vel.x, cx.tvy - pl->vel.y,
		    options.shotSpeed, &ldir, &tof))
	    cx.lead_dir = MOD2((int)(ldir + 0.5), RES);
	else
	    cx.lead_dir = Rb_dir_of(cx.tx, cx.ty);
    }

    /* candidates: headings x {short thrust, short coast, long thrust} */
    nheads = level >= 50 ? 32 : 16;
    specials[nspec++] = pl->dir;
    if (LENGTH(pl->vel.x, pl->vel.y) > 1.0)
	specials[nspec++] = Rb_dir_of(-pl->vel.x, -pl->vel.y);
    {
	const rb_field_t *fd = cx.race ? cx.pf[0] : cx.pfield;
	double rx, ry;
	bool g;

	if (fd && Rb_field_carrot(fd, cx.ox, cx.oy, RB_SHIP_R + 6.0, 10, &rx, &ry, &g)) {
	    int dc = Rb_dir_of(Rb_wdx(rx - cx.ox), Rb_wdy(ry - cx.oy));

	    specials[nspec++] = dc;
	    specials[nspec++] = MOD2(dc + RES / 32, RES);
	    specials[nspec++] = MOD2(dc - RES / 32, RES);
	}
    }
    if (cx.has_target)
	specials[nspec++] = cx.lead_dir;
    for (h = 0; h < nheads + nspec; h++) {
	int dir = h < nheads ? (h * RES) / nheads : specials[h - nheads];

	if (nc + 6 > RB_MAX_CAND - 1)
	    break;
	Rb_cand_set(&cand[nc++], dir, true, 2);
	Rb_cand_set(&cand[nc++], dir, true, 6);
	if (h >= nheads || (h & 1) == 0) {
	    Rb_cand_set(&cand[nc++], dir, false, 2);
	    Rb_cand_set(&cand[nc++], dir, true, 12);
	}
	if (h >= nheads) {
	    Rb_cand_set(&cand[nc++], dir, false, 6);
	    Rb_cand_set(&cand[nc++], dir, true, 20);
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
	double c = Rb_rollout(&cx, &cand[i], &rec, NULL);

	/* lower levels misjudge: now and then a worse plan looks best */
	c += rfrac() * RB_PLAN_NOISE * Rb_unskill(level);

	/* a little inertia against dithering between equal plans */
	if (i == prev_i)
	    c -= 20.0;
	if (c < best_cost) {
	    best_cost = c;
	    best_rec = rec;
	}
    }
    if (best_cost >= 1e30)
	return fl;

    best_rec.len = RB_H;
    Rb_rollout(&cx, &best_rec, &rec, &fl);
    if (cx.race)
	fl.progress = Rb_field_value(cx.vf[0], cx.ox, cx.oy) - fl.end_value;
    d->plan_len = RB_H;
    memcpy(d->plan_dir, best_rec.dir, sizeof(short) * RB_H);
    memcpy(d->plan_thr, best_rec.thr, sizeof(bool) * RB_H);

    fl.valid = true;
    fl.dir = best_rec.dir[0];
    fl.thrust = best_rec.thr[0];
    return fl;
}

/*
 * Heading (rate limited, as in the model) and thrust.  Turning can push the
 * ship off a wall, which is a bounce: the shield must be decided before.
 */
static void Rb_steer(player_t *pl, int want, bool thrust)
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
 * Combat
 * ------------------------------------------------------------------------ */

/* Who shoots at us or pushes us?  Remembered as threats for a while. */
static void Rb_update_threats(player_t *pl, rb_data_t *d)
{
    double ox = Rb_px(pl->pos.cx), oy = Rb_px(pl->pos.cy);
    int i;

    for (i = 0; i < NumObjs; i++) {
	object_t *o = Obj[i];
	player_t *e;
	double rx, ry, dvx, dvy, tc;

	if (o->type != OBJ_SHOT && o->type != OBJ_TORPEDO
	    && o->type != OBJ_SMART_SHOT && o->type != OBJ_HEAT_SHOT
	    && o->type != OBJ_MINE)
	    continue;
	if (o->id == pl->id || o->id == NO_ID)
	    continue;
	e = Player_by_id(o->id);
	if (e == NULL || Rb_ally(e, pl))
	    continue;
	rx = Rb_wdx(Rb_px(o->pos.cx) - ox);
	ry = Rb_wdy(Rb_px(o->pos.cy) - oy);
	if (LENGTH(rx, ry) > 600.0)
	    continue;
	dvx = o->vel.x - pl->vel.x;
	dvy = o->vel.y - pl->vel.y;
	tc = -(rx * dvx + ry * dvy) / MAX(dvx * dvx + dvy * dvy, 1e-6);
	if (tc > 0.0 && tc < 30.0 && LENGTH(rx + dvx * tc, ry + dvy * tc) < 50.0) {
	    if (RB_DEBUG && RbTick - d->threat_time[e->id] > RB_THREAT_TICKS)
		RB_LOG("[racebot] %s attacked by %s\n", pl->name, e->name);
	    d->threat_time[e->id] = RbTick;
	}
    }
    /* pushed: our velocity changed without our doing, an enemy is touching */
    {
	double dvx = pl->vel.x - d->pvx, dvy = pl->vel.y - d->pvy;
	bool bumped = LENGTH(dvx, dvy) > Rb_thrust_acc(pl) * 1.5 + 1.0;

	for (i = 0; i < NumPlayers; i++) {
	    player_t *e = Player_by_index(i);
	    double dist;

	    if (Rb_ally(e, pl) || !Rb_player_active(e))
		continue;
	    dist = Rb_dist(ox, oy, Rb_px(e->pos.cx), Rb_px(e->pos.cy));
	    if (dist < 3.0 * RB_SHIP_R + 10.0 && (bumped || d->alive_ticks < 2))
		d->threat_time[e->id] = RbTick;
	}
    }
    d->pvx = pl->vel.x;
    d->pvy = pl->vel.y;
}

/* The closest threat we can see, for aiming and shooting back. */
static player_t *Rb_pick_target(player_t *pl, rb_data_t *d)
{
    double ox = Rb_px(pl->pos.cx), oy = Rb_px(pl->pos.cy), bd = 700.0;
    player_t *best = NULL;
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	double dist;

	if (Rb_ally(e, pl) || !Rb_player_active(e) || !Rb_is_threat(d, e))
	    continue;
	dist = Rb_dist(ox, oy, Rb_px(e->pos.cx), Rb_px(e->pos.cy));
	if (dist < bd) {
	    bd = dist;
	    best = e;
	}
    }
    return best;
}

/* Shoot at a threat when the shot would hit (walls stop shots). */
static void Rb_fire(player_t *pl, rb_data_t *d, int level)
{
    double ox = Rb_px(pl->pos.cx), oy = Rb_px(pl->pos.cy), f = level / 100.0;
    int i;

    if (!BIT(pl->have, HAS_SHOT) || !Can_shoot_normal_shot(pl)
	|| Player_is_phasing(pl) || BIT(pl->used, USES_SHIELD)
	|| pl->fuel.sum < 30.0)
	return;
    if (RbTick - d->aim_time > 12) {
	/* aiming error drifts slowly, like a human hand */
	d->aim_off = (rfrac() - 0.5) * 2.0 * (1.0 - f) * 4.0;
	d->aim_time = RbTick;
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	double rx, ry, tof = 0.0, tol, miss, hx, hy;
	int fdir;

	if (Rb_ally(e, pl) || !Rb_player_active(e) || !Rb_is_threat(d, e))
	    continue;
	rx = Rb_wdx(Rb_px(e->pos.cx) - ox);
	ry = Rb_wdy(Rb_px(e->pos.cy) - oy);
	if (LENGTH(rx, ry) > VISIBILITY_DISTANCE * 0.8)
	    continue;
	fdir = MOD2(pl->dir + (int)(d->aim_off + (d->aim_off < 0 ? -0.5 : 0.5)), RES);
	miss = Rb_shot_miss(fdir, rx, ry, e->vel.x - pl->vel.x, e->vel.y - pl->vel.y,
			    options.shotSpeed, options.shotLife * 0.9, &tof);
	tol = RB_SHIP_R + 4.0 - (1.0 - f) * 6.0;
	if (miss > tol)
	    continue;
	hx = ox + rx + e->vel.x * tof;
	hy = oy + ry + e->vel.y * tof;
	if (Rb_segment_clear(ox, oy, hx, hy, 1.0) < 1.0)
	    continue;
	SET_BIT(pl->used, USES_SHOT);
	RB_LOG("[racebot] %s fires at %s\n", pl->name, e->name);
	return;
    }
}

/* ------------------------------------------------------------------------
 * Fuel stops
 * ------------------------------------------------------------------------ */

/*
 * Station with the smallest detour that still has fuel, -1 if none.  One
 * field from our position gives the way to all of them (edges cost the
 * same both ways).
 */
static int Rb_pick_station(player_t *pl, int exclude)
{
    double ox = Rb_px(pl->pos.cx), oy = Rb_px(pl->pos.cy), best = 1e30;
    const rb_field_t *cf = &CheckF[0][pl->check];
    double here = Rb_field_value(cf, ox, oy);
    int i, j, bi = -1;

    if (!Rb_field_compute(&PosF, ox, oy, 0.0, NULL, NULL))
	return -1;
    for (i = 0; i < Num_fuels(); i++) {
	fuel_t *fs = Fuel_by_index(i);
	double sx = Rb_px(fs->pos.cx), sy = Rb_px(fs->pos.cy), to, c;

	if (i == exclude || fs->fuel < 20.0
	    || (BIT(world->rules->mode, TEAM_PLAY) && options.teamFuel
		&& fs->team != pl->team))
	    continue;
	to = Rb_field_value(&PosF, sx, sy);
	if (to >= 1e5)
	    continue;
	c = to + Rb_field_value(cf, sx, sy) - here - fs->fuel * 0.2;
	/* taken: another ship stands there or a racebot is on its way */
	for (j = 0; j < NumPlayers; j++) {
	    player_t *o = Player_by_index(j);

	    if (o == pl || !Rb_player_active(o))
		continue;
	    if (Rb_dist(sx, sy, Rb_px(o->pos.cx), Rb_px(o->pos.cy)) < 80.0
		|| (Rb_is_racebot(o) && Rb_data(o)->pit && Rb_data(o)->pit_station == i))
		c += 1000.0;
	}
	if (c < best) {
	    best = c;
	    bi = i;
	}
    }
    return bi;
}

/* Use any station in reach while passing it. */
static void Rb_refuel_in_passing(player_t *pl, rb_data_t *d)
{
    int i, bi = -1;
    double bd = 85.0;

    if (!BIT(pl->have, HAS_REFUEL) || pl->fuel.sum >= pl->fuel.max - 1.0)
	return;
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
	d->refuel_ticks += 1.0;
    } else
	CLR_BIT(pl->used, USES_REFUEL);
}

/* Decide about a fuel stop; returns the station to fly to or -1. */
static int Rb_fuel_plan(player_t *pl, rb_data_t *d)
{
    double need, have = pl->fuel.sum;

    if (Num_fuels() == 0 || !BIT(pl->have, HAS_REFUEL))
	return -1;
    need = Rb_fuel_needed(pl);
    if (!d->pit) {
	if (have >= need * 1.1 + 10.0)
	    return -1;
	d->pit = true;
	d->pit_station = Rb_pick_station(pl, -1);
	d->pit_target = MIN(pl->fuel.max - 2.0, need * 1.3 + 30.0);
	RB_LOG("[racebot] %s fuel stop: have %.0f need %.0f -> station %d\n",
	       pl->name, have, need, d->pit_station);
    }
    if (have >= d->pit_target || have >= pl->fuel.max - 2.0) {
	d->pit = false;
	return -1;
    }
    if (d->pit_station >= 0 && Fuel_by_index(d->pit_station)->fuel < 5.0)
	d->pit_station = Rb_pick_station(pl, d->pit_station);
    if (d->pit_station < 0) {
	d->pit = false;
	return -1;
    }
    return d->pit_station;
}

/* ------------------------------------------------------------------------
 * Robot type interface
 * ------------------------------------------------------------------------ */

static void Robot_race_round_tick(void)
{
    int i;

    NumRacebots = 0;
    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (!Rb_is_racebot(pl))
	    continue;
	NumRacebots++;
	/* best lap (frames), also of the last lap when the robot is done */
	if (Learn.loaded && pl->best_lap > 0
	    && (Learn.best_lap <= 0.0 || pl->best_lap * timeStep < Learn.best_lap - 0.5)) {
	    Learn.best_lap = pl->best_lap * timeStep;
	    Learn.dirty = true;
	}
    }
    if (NumRacebots == 0)
	return;
    RbTick++;
    if (Robot_race_map())
	Rb_map_update();
}

static void Robot_race_create(player_t *pl, char *str)
{
    rb_data_t *d;

    if (!(d = XMALLOC(rb_data_t, 1))) {
	error("no mem for race robot");
	End_game();
    }
    memset(d, 0, sizeof(*d));
    d->learn = true;
    d->last_check = -1;
    d->stuck_check = -1;
    d->pit_station = -1;
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

static void Robot_race_go_home(player_t *pl)
{
    rb_data_t *d = Rb_data(pl);

    if (d->alive_ticks > 0)
	RB_LOG("[racebot] %s back at base after %lu ticks (check %d round %d)\n",
	       pl->name, d->alive_ticks, pl->check, pl->round);
    /* the time from the base to the next checkpoint teaches nothing */
    d->alive_ticks = 0;
    d->plan_len = 0;
    d->seg_valid = false;
    d->pit = false;
    d->destruct = false;
    d->detour = false;
    d->slow_ticks = 0;
    d->stuck_check = -1;
}

/* Keeps track of checkpoint passes, laps and the learning that goes with them. */
static void Rb_track_progress(player_t *pl, rb_data_t *d)
{
    if (d->last_check < 0 || d->alive_ticks <= 1) {
	d->last_check = pl->check;
	d->last_round = pl->round;
	d->seg_fuel0 = pl->fuel.sum;
	return;
    }
    if (pl->check == d->last_check && pl->round == d->last_round)
	return;
    /* passed checkpoint d->last_check */
    if (pl->round != d->last_round) {
	/* a lap starts (or the race): a good moment to save what was learned */
	if (pl->round > 1)
	    RB_LOG("[racebot] %s LAP %d: %.0f ticks\n", pl->name, pl->round - 1,
		   pl->last_lap_time * timeStep);
	if (Learn.dirty)
	    Rb_learn_save();
    }
    if (d->seg_valid)
	Rb_segment_done(pl, d, d->last_check);
    d->last_check = pl->check;
    d->last_round = pl->round;
    d->seg_valid = pl->round > 0;
    d->detour = false;
    d->slow_ticks = 0;
    d->seg_fuel0 = pl->fuel.sum;
    Rb_segment_start(d);
}

/*
 * Hanging somewhere without getting closer to the next checkpoint, which
 * a slow ship among repelling gravs can do: self-destruct and start over,
 * as a human would.  Standing at a fuel station is no hanging.
 */
static void Rb_check_stuck(player_t *pl, rb_data_t *d, int pit)
{
    double v = Rb_field_value(&CheckF[0][pl->check], Rb_px(pl->pos.cx), Rb_px(pl->pos.cy));

    if (pl->check != d->stuck_check || pit >= 0 || Player_is_refueling(pl)
	|| v < d->stuck_best - RB_STUCK_GAIN) {
	d->stuck_check = pl->check;
	d->stuck_best = v;
	d->stuck_since = RbTick;
	d->destruct = false;
	return;
    }
    if (!d->destruct && RbTick - d->stuck_since > RB_STUCK_TICKS) {
	d->destruct = true;
	RB_LOG("[racebot] %s is stuck at %.0f,%.0f: self-destruct\n", pl->name,
	       Rb_px(pl->pos.cx), Rb_px(pl->pos.cy));
    }
}

/* Strongest gravity within 'range' px of the ship. */
static double Rb_gravity_near(player_t *pl, double range)
{
    int bx0 = (int)(Rb_px(pl->pos.cx) / BLOCK_SZ), by0 = (int)(Rb_px(pl->pos.cy) / BLOCK_SZ);
    int r = (int)(range / BLOCK_SZ) + 1, bx, by;
    double g = 0.0;

    if (world->gravity == NULL)
	return 0.0;
    for (by = MAX(by0 - r, 0); by <= MIN(by0 + r, world->y - 1); by++) {
	for (bx = MAX(bx0 - r, 0); bx <= MIN(bx0 + r, world->x - 1); bx++)
	    g = MAX(g, LENGTH(world->gravity[bx][by].x, world->gravity[bx][by].y));
    }
    return g;
}

/*
 * Does the ship get through the gravity ahead at its speed?  The flight
 * model knows: when the best plan ends slow in strong gravity, or gains
 * little while strong gravity is near, the ship would hang there.  Then
 * take the way around it up to the next checkpoint.  With speed the plans
 * get through, and the bot keeps the short way.
 */
static void Rb_check_detour(player_t *pl, rb_data_t *d, const rb_flags_t *fl)
{
    double acc = Rb_thrust_acc(pl), g_end = 0.0;

    if (d->detour || !fl->valid)
	return;
    if (world->gravity != NULL && fl->end_value > -1e8) {
	int bx = (int)(fl->end_x / BLOCK_SZ), by = (int)(fl->end_y / BLOCK_SZ);

	LIMIT(bx, 0, world->x - 1);
	LIMIT(by, 0, world->y - 1);
	g_end = LENGTH(world->gravity[bx][by].x, world->gravity[bx][by].y);
    }
    if ((g_end > 0.7 * acc && fl->end_speed < 10.0)
	|| (fl->progress < RB_DETOUR_GAIN && Rb_gravity_near(pl, 300.0) > 0.7 * acc))
	d->slow_ticks++;
    else
	d->slow_ticks = 0;
    if (d->slow_ticks >= 3) {
	d->detour = true;
	d->plan_len = 0;
	RB_LOG("[racebot] %s too slow for the gravity at %.0f,%.0f: detour\n",
	       pl->name, Rb_px(pl->pos.cx), Rb_px(pl->pos.cy));
    }
}

static void Robot_race_play(player_t *pl)
{
    rb_data_t *d = Rb_data(pl);
    int level = Rb_level(d), pit = -1;
    player_t *target;
    rb_flags_t fl;

    CLR_BIT(pl->used, USES_SHOT | USES_SHIELD | USES_LASER | USES_CONNECTOR);
    Player_self_destruct(pl, d->destruct);
    pl->power = Rb_power(level);	/* before planning: the model uses it */
    if (pl->damaged > 0 || !Robot_race_map() || !RbMapOk
	|| BIT(pl->pl_status, FINISH | RACE_OVER)) {
	Thrust(pl, false);
	return;
    }
    d->alive_ticks++;
    Rb_track_progress(pl, d);
    Rb_update_threats(pl, d);

    pit = Rb_fuel_plan(pl, d);
    Rb_check_stuck(pl, d, pit);
    target = Rb_pick_target(pl, d);
    fl = Rb_fly(pl, d, level, pit, target);
    if (pit < 0)
	Rb_check_detour(pl, d, &fl);
    /*
     * Shield against what the best plan cannot avoid, and whenever the
     * model may be wrong in a deadly way: a wall close ahead at a speed
     * only a shield survives, or a ship closing in fast (ships swerve).
     */
    if (BIT(pl->have, HAS_SHIELD) && pl->fuel.sum > 5.0) {
	double v = LENGTH(pl->vel.x, pl->vel.y), x = Rb_px(pl->pos.cx), y = Rb_px(pl->pos.cy);
	double r = Rb_ship_radius(pl);
	bool danger = (fl.shot_t >= 0 && fl.shot_t <= 2) || (fl.ram_t >= 0 && fl.ram_t <= 1)
	    || (fl.hard_t >= 0 && fl.hard_t <= 2);
	int i;

	if (!danger && v > options.maxUnshieldedWallBounceSpeed * 0.9) {
	    double ux = pl->vel.x / v, uy = pl->vel.y / v, t;

	    danger = Rb_ship_clear(x, y) < r + 6.0;	/* turning may touch it */
	    for (t = 0.0; t <= 2.0 * v && !danger; t += FineStep)
		danger = Rb_ship_clear(x + ux * t, y + uy * t) < r + 3.0;
	}
	for (i = 0; i < NumPlayers && !danger && BIT(world->rules->mode, CRASH_WITH_PLAYER); i++) {
	    player_t *e = Player_by_index(i);
	    double rx, ry, rvx, rvy;

	    if (Rb_ally(e, pl) || !Rb_player_active(e))
		continue;
	    rx = Rb_wdx(Rb_px(e->pos.cx) - x);
	    ry = Rb_wdy(Rb_px(e->pos.cy) - y);
	    rvx = e->vel.x - pl->vel.x;
	    rvy = e->vel.y - pl->vel.y;
	    danger = rx * rvx + ry * rvy < 0.0
		&& LENGTH(rx, ry) < 2.0 * r + 6.0 + 1.5 * LENGTH(rvx, rvy);
	}
	if (danger)
	    SET_BIT(pl->used, USES_SHIELD);
    }
    if (fl.valid) {
	int dir = fl.dir;

	/*
	 * Lower levels steer less exactly: an error that drifts slowly.  Not
	 * in strong gravity, where it would only get the ship stuck.
	 */
	if (RbTick - d->steer_time > 10) {
	    d->steer_off = (rfrac() - 0.5) * 2.0 * RB_STEER_ERR * Rb_unskill(level);
	    d->steer_time = RbTick;
	}
	if (Rb_gravity_near(pl, 250.0) < 0.5 * Rb_thrust_acc(pl))
	    dir = MOD2(dir + (int)(d->steer_off + (d->steer_off < 0 ? -0.5 : 0.5)), RES);
	Rb_steer(pl, dir, fl.thrust);
    }
    else
	Thrust(pl, false);
    if (pit >= 0) {
	fuel_t *fs = Fuel_by_index(pit);

	if (Wrap_length(pl->pos.cx - fs->pos.cx, pl->pos.cy - fs->pos.cy) / CLICK < 85.0) {
	    SET_BIT(pl->used, USES_REFUEL);
	    pl->fs = pit;
	    d->refuel_ticks += 1.0;
	} else
	    Rb_refuel_in_passing(pl, d);
    } else
	Rb_refuel_in_passing(pl, d);
    Rb_fire(pl, d, level);
}

static void Robot_race_set_war(player_t *pl, int victim_id)
{
    rb_data_t *d = Rb_data(pl);

    /* an enemy who killed us is a threat when we meet him again */
    if (victim_id >= 0 && victim_id < NUM_IDS)
	d->threat_time[victim_id] = RbTick;
}

static int Robot_race_war_on_player(player_t *pl)
{
    (void)pl;
    return NO_ID;
}

/* /msg <bot> status | level N | learn on|off | forget */
static void Robot_race_message(player_t *pl, const char *message)
{
    rb_data_t *d = Rb_data(pl);
    int lvl;

    if (message == NULL)
	return;
    if (strstr(message, "status") != NULL) {
	/* ticks of game time: gameSpeed of them per second */
	Set_message_f("Racebot level %d, lap %d/%d, checkpoint %d, fuel %.0f "
		      "(needs %.0f)%s, best lap %.2fs, learning %s. [%s]",
		      Rb_level(d), MAX(pl->round, 0), options.raceLaps, pl->check,
		      pl->fuel.sum, RbMapOk ? Rb_fuel_needed(pl) : 0.0,
		      d->pit ? ", fuel stop" : "",
		      Learn.best_lap / MAX(options.gameSpeed, 1e-3),
		      d->learn ? "on" : "off", pl->name);
    } else if (strstr(message, "level ") != NULL
	       && sscanf(strstr(message, "level ") + 6, "%d", &lvl) == 1) {
	d->level = lvl <= 0 ? 0 : MIN(lvl, 100);
	Set_message_f("Skill level set to %d. [%s]", Rb_level(d), pl->name);
    } else if (strstr(message, "learn off") != NULL) {
	d->learn = false;
	Set_message_f("Learning off. [%s]", pl->name);
    } else if (strstr(message, "learn on") != NULL) {
	d->learn = true;
	Set_message_f("Learning on. [%s]", pl->name);
    } else if (strstr(message, "forget") != NULL) {
	if (Learn.loaded) {
	    Rb_learn_reset();
	    Learn.dirty = true;
	    Rb_learn_save();
	}
	Set_message_f("Forgot what I learned about %s. [%s]", world->name, pl->name);
    }
}

static void Robot_race_destroy(player_t *pl)
{
    if (Learn.dirty)
	Rb_learn_save();
    XFREE(pl->robot_data_ptr->private_data);
}

static void Robot_race_invite(player_t *pl, player_t *inviter)
{
    if (!Player_is_active(inviter)) {
	Refuse_alliance(pl, inviter);
	return;
    }
    Accept_alliance(pl, inviter);
}

static void Robot_race_game_event(player_t *pl, int event, player_t *other)
{
    (void)pl;
    (void)event;
    (void)other;
}

static robot_type_t robot_race_type = {
    RB_TYPE_NAME,
    Robot_race_round_tick,
    Robot_race_create,
    Robot_race_go_home,
    Robot_race_play,
    Robot_race_set_war,
    Robot_race_war_on_player,
    Robot_race_message,
    Robot_race_destroy,
    Robot_race_invite,
    Robot_race_game_event
};

int Robot_race_setup(robot_type_t *type_ptr)
{
    *type_ptr = robot_race_type;
    return 0;
}
