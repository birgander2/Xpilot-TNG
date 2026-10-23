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
 * Ballbot: a robot driver ("ball") dedicated to team ball games.
 *
 * Smart robots use this driver instead of the learning smartbot on maps with
 * team play and treasures, unless the option smartBallBots is off.
 *
 *  - Map analysis (once per map): wall clearance on a fine grid (exact
 *    distance transform), a coarse navigation grid and Dijkstra distance
 *    fields to every treasure.  Fields to moving goals are cached.
 *  - Team coordination: a team only uses what its members (humans included)
 *    can see within a human's view range, plus enemy positions on the radar
 *    when playersOnRadar is on.  Roles are assigned from that knowledge.
 *  - Flight: a sampling model predictive controller.  Candidate manoeuvres
 *    are simulated with the server's physics (ship, thrust, turn rate and the
 *    ball on its spring connector) and scored on progress along the distance
 *    field, wall and BallArea clearance, connector strain, ball swing, enemy
 *    shots and enemy ships.  This keeps a towed ball behind the ship.
 *  - Combat: lead aiming with the shot's inherited ship velocity.
 *
 * The skill level (smartRobotLevel) sets turn rate, reaction delay, aiming
 * precision, wall margin and how many manoeuvres are sampled.
 */

#include "xpserver.h"

#define BB_TYPE_NAME		"ball"

#define BB_SHIP_R		16.0	/* collision radius of a ship (px) */
#define BB_TARGET_R		26.0	/* ship shapes reach further: targets kill */
#define BB_NUM_KINDS		2
#define BB_KIND_SHIP		0	/* fields for a ship alone */
#define BB_KIND_BALL		1	/* fields for a ship towing a ball */
#define BB_MAX_TREASURES	16
#define BB_DYN_FIELDS		24
#define BB_DYN_BUDGET		3	/* dynamic field computations per tick */
#define BB_INF			1e30f
#define BB_HMAX			26	/* max rollout horizon (ticks) */
#define BB_MAX_CAND		128
#define BB_MAX_SHOTS		48
#define BB_MAX_ENEMIES		16
#define BB_ROUTES		4
#define BB_ROUTE_PTS		400
#define BB_ROLE_TICKS		6
#define BB_BALL_MEMORY		(5 * 12)	/* ticks a ball sighting is trusted */
#define BB_THIEF_MEMORY		(8 * 12)
#define BB_THROW_TICKS		50	/* longest free flight of a throw */
#define BB_BOUNCE_COST		150.0	/* a wall bounce, besides the speed lost */
#define BB_FREE_VMAX		36.0	/* speed limit flying to a ball */

enum bb_role {
    BB_ROLE_NONE,
    BB_ROLE_CARRY,	/* towing a ball home */
    BB_ROLE_GRAB,	/* fetching the enemy ball */
    BB_ROLE_RETURN,	/* fetching our own loose ball */
    BB_ROLE_HUNT,	/* chasing the thief of our ball */
    BB_ROLE_ESCORT,	/* protecting our carrier */
    BB_ROLE_SUPPORT,	/* covering the grabber at the enemy base */
    BB_ROLE_MID,	/* intercepting in the middle */
    BB_ROLE_DEFEND,	/* guarding our treasure */
    BB_ROLE_FIGHT,	/* no ball game: plain combat */
    BB_NUM_ROLES
};

static const char *bb_role_names[BB_NUM_ROLES] = {
    "none", "carry", "grab", "return", "hunt", "escort", "support",
    "mid", "defend", "fight"
};

/* ------------------------------------------------------------------------
 * Data types
 * ------------------------------------------------------------------------ */

typedef struct {
    float	*dist;		/* cost to the goal per nav cell (px) */
    int32_t	*next;		/* next cell towards the goal, -2 at the goal */
    int		kind;
    int		gcell;
    double	gx, gy;		/* goal (px) */
    unsigned long stamp;	/* tick of computation */
    unsigned long used;		/* tick of last use */
    bool	valid;
} bb_field_t;

typedef struct {
    int		n;
    struct {
	bb_field_t	*via;	/* field to the via point, NULL: direct */
	double		vx, vy;	/* via point (px) */
	double		via_rest; /* remaining cost from the via point */
	double		time;	/* estimated run time (ticks) */
	double		weight;
	int		npts;
	float		px[BB_ROUTE_PTS / 4], py[BB_ROUTE_PTS / 4];
    } v[BB_ROUTES];
    bool	done;
} bb_routes_t;

typedef struct {
    bool	known;		/* position known (seen or on radar) */
    bool	seen;		/* within view range of a teammate now */
    double	x, y, vx, vy;
    unsigned long last_seen;
    unsigned long last_known;
    int		tows;		/* treasure index of a ball it tows (seen), -1 */
} bb_contact_t;

typedef struct {
    bool	known;
    bool	seen;
    bool	home;		/* in its treasure when last seen */
    double	x, y, vx, vy;
    int		owner_id;	/* towing player when seen, NO_ID: loose */
    unsigned long time;
} bb_ballinfo_t;

typedef struct {
    bool		active;
    bb_contact_t	c[NUM_IDS];
    bb_ballinfo_t	ball[BB_MAX_TREASURES];
    bool		ball_away[BB_MAX_TREASURES];
    int			thief_id;		/* enemy towing our ball, NO_ID */
    unsigned long	thief_time;
    int			thief_tr;
} bb_team_t;

typedef struct {
    double	sx, sy, svx, svy;	/* ship, relative to the origin */
    double	bx, by, bvx, bvy;	/* towed ball */
    int		dir;
    int		ship_hits, ball_hits;	/* wall bounces so far */
    bool	crashed;		/* ship too fast against a wall */
    bool	ball_lost;		/* ball too fast against a wall */
} bb_state_t;

typedef struct {
    int		role, role_arg, escort_slot;
    int		level;			/* 0: use options.smartRobotLevel */
    int		plan_len;
    short	plan_dir[BB_HMAX];
    bool	plan_thr[BB_HMAX];
    /* fetching a ball */
    int		grab_tr;
    bool	spot_valid;
    double	spot_x, spot_y, exit_x, exit_y;
    unsigned long spot_time;
    /* towing */
    bool	had_ball;
    int		route_src, route_dst, route_var;
    bool	via_passed;
    bool	route_chosen;
    unsigned long route_time;
    /* aiming */
    double	aim_off;
    unsigned long aim_time;
    bool	threw;			/* released the ball on purpose */
    bool	in_line;		/* ball lined up for a throw */
    unsigned long alive_ticks;
} bb_data_t;

/* What a bot wants this tick; consumed by the flight controller. */
typedef struct {
    bb_field_t	*field;		/* cost-to-go, NULL: straight distance */
    double	gx, gy;		/* goal (px) */
    double	gvx, gvy;	/* goal velocity (moving goals) */
    double	arrive_r;	/* hold position within this radius, 0: none */
    double	vmax;
    player_t	*target;	/* enemy to aim at, NULL */
    double	aim_w;
    double	enemy_w;	/* weight of keeping away from enemy ships */
    double	wall_w;		/* weight of the wall proximity cost */
    double	match_w;	/* weight of matching the goal velocity near it */
    int		score_team;	/* a towed ball scores in this team's targets */
    double	thx, thy;	/* where a throw goes */
    int		hit_tr;		/* ram this treasure's ball (sends it home), -1 */
} bb_goal_t;


typedef struct {
    int		len;
    short	dir[BB_HMAX];
    bool	thr[BB_HMAX];
} bb_cand_t;

typedef struct {
    double	x, y, vx, vy, life;
    double	t0;		/* fired at this tick (virtual shots) */
    double	weight;
} bb_shot_t;

typedef struct {
    double	x, y, vx, vy;
    int		dir;
    bool	seen;
} bb_enemy_t;

/* Other balls: touching one destroys it (treasureCollisionDestroys) */
#define BB_BALL_AVOID	1
#define BB_BALL_HIT	2
#define BB_MAX_BALLS	8
typedef struct {
    double	x, y, vx, vy;
    int		mode;
} bb_oball_t;

typedef struct {
    player_t	*pl;
    bb_data_t	*d;
    const bb_goal_t *g;
    double	ox, oy;			/* origin: ship position (px) */
    bb_state_t	s0;
    bool	carry;
    double	acc, ms, mb, L, k, damp, ratio_max;
    int		turn, H, frames;
    double	dt, margin, a_eff, clear0, alpha;
    double	cx, cy;			/* carrot relative to the origin */
    bool	carrot_is_goal;
    double	ball_r;
    int		nshots;
    bb_shot_t	shots[BB_MAX_SHOTS];
    int		nen;
    bb_enemy_t	en[BB_MAX_ENEMIES];
    int		nballs;
    bb_oball_t	balls[BB_MAX_BALLS];
    int		team;
    double	shot_speed;
    double	tx, ty, tvx, tvy;	/* aim target relative */
    bool	has_target;
    int		lead_dir;
    bool	throw_check;
} bb_ctx_t;

/* ------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------ */

static const poly_t	*BbPdata;
static int		BbNumPolys = -1;
static int		BbMapW = -1, BbMapH = -1;
static bool		BbMapOk;
static bool		BbWrap;
static double		BbW, BbH;
static int		FineW, FineH;
static double		FineCX, FineCY, FineStep;
static float		*ShipClear, *BallClear;
static float		*TargetClear;	/* distance to a ball target: ships crash */
static uint8_t		*Zone;		/* treasure index + 1 of a BallTarget */
static int		NavW, NavH, NavN, NavF;
static double		NavCX, NavCY;
static float		*NavRX, *NavRY, *NavClear;
static uint8_t		*NavEdge[BB_NUM_KINDS];
static float		*NavCost[BB_NUM_KINDS];

typedef struct { float f; int32_t c; } bb_heap_t;
static bb_heap_t	*Heap;
static int		HeapN, HeapCap;

static bb_field_t	*StaticF[BB_MAX_TREASURES][BB_NUM_KINDS];
static bb_field_t	DynF[BB_DYN_FIELDS];
static int		DynBudget;
static bb_routes_t	*Routes[BB_MAX_TREASURES][BB_MAX_TREASURES];

static bb_team_t	BbTeams[MAX_TEAMS];
static unsigned long	BbTick;
static double		BbVisibility = VISIBILITY_DISTANCE;
static int		NumBallbots;

static const int	nb_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int	nb_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

static const double	BbPassR[BB_NUM_KINDS] = { 22.0, 34.0 };
static const double	BbEdgeR[BB_NUM_KINDS] = { 19.0, 28.0 };
static const double	BbPrefR[BB_NUM_KINDS] = { 70.0, 130.0 };
static const double	BbPrefW[BB_NUM_KINDS] = { 1.5, 4.0 };

#ifndef BB_DEBUG
#define BB_DEBUG 0
#endif
#define BB_LOG(...) do { if (BB_DEBUG) { printf("%6lu ", BbTick); printf(__VA_ARGS__); fflush(stdout); } } while (0)

/* ------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------ */

static inline bb_data_t *Bb_data(player_t *pl)
{
    return (bb_data_t *)pl->robot_data_ptr->private_data;
}

static inline bool Bb_is_ballbot(player_t *pl)
{
    const char *name;

    if (!Player_is_robot(pl) || pl->robot_data_ptr == NULL)
	return false;
    name = Robot_type_name(pl);
    return name != NULL && strcmp(name, BB_TYPE_NAME) == 0;
}

static inline double Bb_px(click_t c)
{
    return (double)c / CLICK;
}

static inline double Bb_wdx(double dx)
{
    if (BbWrap) {
	if (dx > BbW * 0.5)
	    dx -= BbW;
	else if (dx < -BbW * 0.5)
	    dx += BbW;
    }
    return dx;
}

static inline double Bb_wdy(double dy)
{
    if (BbWrap) {
	if (dy > BbH * 0.5)
	    dy -= BbH;
	else if (dy < -BbH * 0.5)
	    dy += BbH;
    }
    return dy;
}

static inline double Bb_dist(double x1, double y1, double x2, double y2)
{
    return LENGTH(Bb_wdx(x2 - x1), Bb_wdy(y2 - y1));
}

static inline int Bb_angle_diff(int a, int b)
{
    int d = MOD2(a - b, RES);

    return d > RES / 2 ? d - RES : d;
}

static inline int Bb_dir_of(double x, double y)
{
    return MOD2((int)(findDir(x, y) + 0.5), RES);
}

static int Bb_level(bb_data_t *d)
{
    int lvl = d->level > 0 ? d->level : options.smartRobotLevel;

    LIMIT(lvl, 1, 100);
    return lvl;
}

static int Bb_turn_rate(int level)
{
    return 3 + (level * 9) / 100;	/* same as the smartbot */
}

static bool Bb_player_active(player_t *pl)
{
    return pl != NULL && !Player_is_tank(pl) && Player_is_alive(pl)
	&& !Player_is_paused(pl) && !Player_is_waiting(pl);
}

static int Bb_treasure_index(const treasure_t *tr)
{
    int i;

    for (i = 0; i < Num_treasures() && i < BB_MAX_TREASURES; i++) {
	if (Treasure_by_index(i) == tr)
	    return i;
    }
    return -1;
}

static ballobject_t *Bb_towed_ball(player_t *pl)
{
    int i;

    if (!BIT(pl->have, HAS_BALL))
	return NULL;
    for (i = 0; i < NumObjs; i++) {
	if (Obj[i]->type == OBJ_BALL && Obj[i]->id == pl->id)
	    return BALL_IND(i);
    }
    return NULL;
}

static ballobject_t *Bb_ball_of(int tr)
{
    int i;

    for (i = 0; i < NumObjs; i++) {
	if (Obj[i]->type == OBJ_BALL
	    && BALL_IND(i)->ball_treasure == Treasure_by_index(tr))
	    return BALL_IND(i);
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * Map analysis: fine clearance grid
 * ------------------------------------------------------------------------ */

static inline int Bb_fine_index(double x, double y)
{
    int ix, iy;

    if (BbWrap) {
	while (x < 0) x += BbW;
	while (x >= BbW) x -= BbW;
	while (y < 0) y += BbH;
	while (y >= BbH) y -= BbH;
    }
    ix = (int)(x / FineCX);
    iy = (int)(y / FineCY);
    if (ix < 0 || ix >= FineW || iy < 0 || iy >= FineH)
	return -1;
    return iy * FineW + ix;
}

static inline float Bb_ship_clear(double x, double y)
{
    int i = Bb_fine_index(x, y);

    return i < 0 ? 0.0f : ShipClear[i];
}

static inline float Bb_ball_clear(double x, double y)
{
    int i = Bb_fine_index(x, y);

    return i < 0 ? 0.0f : BallClear[i];
}

static inline int Bb_zone(double x, double y)
{
    int i = Bb_fine_index(x, y);

    return i < 0 ? 0 : Zone[i];
}

/* Minimum clearance along a segment (absolute px), stops below 'need'. */
static double Bb_segment_clear(const float *grid, double x1, double y1,
			       double x2, double y2, double need)
{
    double dx = Bb_wdx(x2 - x1), dy = Bb_wdy(y2 - y1);
    double len = LENGTH(dx, dy), m = 1e9;
    int i, n = (int)(len / FineStep) + 1;

    for (i = 0; i <= n; i++) {
	double f = (double)i / n;
	int k = Bb_fine_index(x1 + dx * f, y1 + dy * f);
	double c = k < 0 ? 0.0 : grid[k];

	if (c < m) {
	    m = c;
	    if (m < need)
		return m;
	}
    }
    return m;
}

/* 1-D squared distance transform (Felzenszwalb & Huttenlocher). */
static void Bb_dt1d(const double *f, double *d, int n, int *v, double *z)
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
static bool Bb_edt(const uint8_t *obst, float *out)
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

    /* columns */
    for (x = 0; x < FineW; x++) {
	if (BbWrap) {
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
	Bb_dt1d(f, d, n, v, z);
	for (y = 0; y < FineH; y++)
	    tmp[y * FineW + x] = d[y + off];
    }
    /* rows */
    for (y = 0; y < FineH; y++) {
	if (BbWrap) {
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
	Bb_dt1d(f, d, n, v, z);
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

static inline int Bb_nav_cell(double x, double y)
{
    int ix, iy;

    if (BbWrap) {
	while (x < 0) x += BbW;
	while (x >= BbW) x -= BbW;
	while (y < 0) y += BbH;
	while (y >= BbH) y -= BbH;
    }
    ix = (int)(x / NavCX);
    iy = (int)(y / NavCY);
    LIMIT(ix, 0, NavW - 1);
    LIMIT(iy, 0, NavH - 1);
    return iy * NavW + ix;
}

static inline int Bb_nav_nb(int c, int dx, int dy)
{
    int x = c % NavW + dx, y = c / NavW + dy;

    if (BbWrap) {
	x = (x + NavW) % NavW;
	y = (y + NavH) % NavH;
    } else if (x < 0 || x >= NavW || y < 0 || y >= NavH)
	return -1;
    return y * NavW + x;
}

static void Bb_heap_push(float f, int32_t c)
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

static bb_heap_t Bb_heap_pop(void)
{
    bb_heap_t top = Heap[0], last = Heap[--HeapN];
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

static void Bb_field_free(bb_field_t *fd)
{
    if (fd == NULL)
	return;
    free(fd->dist);
    free(fd->next);
    fd->dist = NULL;
    fd->next = NULL;
    fd->valid = false;
}

static void Bb_map_free(void)
{
    int i, j, k;

    free(ShipClear); free(BallClear); free(TargetClear); free(Zone);
    free(NavRX); free(NavRY); free(NavClear); free(Heap);
    ShipClear = BallClear = TargetClear = NULL;
    Zone = NULL;
    NavRX = NavRY = NavClear = NULL;
    Heap = NULL;
    HeapCap = HeapN = 0;
    for (k = 0; k < BB_NUM_KINDS; k++) {
	free(NavEdge[k]);
	free(NavCost[k]);
	NavEdge[k] = NULL;
	NavCost[k] = NULL;
    }
    for (i = 0; i < BB_MAX_TREASURES; i++) {
	for (k = 0; k < BB_NUM_KINDS; k++) {
	    if (StaticF[i][k]) {
		Bb_field_free(StaticF[i][k]);
		free(StaticF[i][k]);
		StaticF[i][k] = NULL;
	    }
	}
	for (j = 0; j < BB_MAX_TREASURES; j++) {
	    if (Routes[i][j]) {
		for (k = 0; k < Routes[i][j]->n; k++) {
		    if (Routes[i][j]->v[k].via) {
			Bb_field_free(Routes[i][j]->v[k].via);
			free(Routes[i][j]->v[k].via);
		    }
		}
		free(Routes[i][j]);
		Routes[i][j] = NULL;
	    }
	}
    }
    for (i = 0; i < BB_DYN_FIELDS; i++)
	Bb_field_free(&DynF[i]);
    BbMapOk = false;
}

/* Nearest treasure of a team to a ball target cell, -1 if none. */
static int Bb_target_treasure(int team, int cx, int cy)
{
    int i, best = -1;
    double bd = 1e30;

    for (i = 0; i < Num_treasures(); i++) {
	treasure_t *t = Treasure_by_index(i);
	double d;

	if (t->team != team)
	    continue;
	d = Wrap_length(t->pos.cx - cx, t->pos.cy - cy);
	if (d < bd) {
	    bd = d;
	    best = i;
	}
    }
    return best;
}

static bool Bb_map_build(void)
{
    long blocks = (long)world->x * world->y;
    int nb = 1, x, y, c, k, i;
    uint8_t *obs_ship, *obs_ball;

    Bb_map_free();
    BbPdata = pdata;
    BbNumPolys = num_polys;
    BbMapW = world->cwidth;
    BbMapH = world->cheight;

    while (blocks / ((long)nb * nb) > 40000)
	nb++;
    BbWrap = BIT(world->rules->mode, WRAP_PLAY) ? true : false;
    BbW = (double)world->cwidth / CLICK;
    BbH = (double)world->cheight / CLICK;
    NavF = 5;
    NavW = MAX(1, (int)(BbW / (BLOCK_SZ * nb) + 0.5));
    NavH = MAX(1, (int)(BbH / (BLOCK_SZ * nb) + 0.5));
    NavN = NavW * NavH;
    NavCX = BbW / NavW;
    NavCY = BbH / NavH;
    FineW = NavW * NavF;
    FineH = NavH * NavF;
    FineCX = NavCX / NavF;
    FineCY = NavCY / NavF;
    FineStep = MIN(FineCX, FineCY);

    ShipClear = malloc(sizeof(float) * (size_t)FineW * FineH);
    BallClear = malloc(sizeof(float) * (size_t)FineW * FineH);
    TargetClear = malloc(sizeof(float) * (size_t)FineW * FineH);
    Zone = calloc((size_t)FineW * FineH, 1);
    obs_ship = malloc((size_t)FineW * FineH);
    obs_ball = malloc((size_t)FineW * FineH);
    NavRX = malloc(sizeof(float) * NavN);
    NavRY = malloc(sizeof(float) * NavN);
    NavClear = malloc(sizeof(float) * NavN);
    HeapCap = NavN * 8 + 64;
    Heap = malloc(sizeof(bb_heap_t) * HeapCap);
    for (k = 0; k < BB_NUM_KINDS; k++) {
	NavEdge[k] = calloc(NavN, 1);
	NavCost[k] = malloc(sizeof(float) * NavN * 8);
    }
    if (!ShipClear || !BallClear || !TargetClear || !Zone || !obs_ship || !obs_ball || !NavRX
	|| !NavRY || !NavClear || !Heap || !NavEdge[0] || !NavEdge[1]
	|| !NavCost[0] || !NavCost[1]) {
	free(obs_ship);
	free(obs_ball);
	Bb_map_free();
	return false;
    }

    /* Rasterize: ships hit walls and BallAreas, balls hit walls only */
    for (y = 0; y < FineH; y++) {
	for (x = 0; x < FineW; x++) {
	    int cx = (int)(((x + 0.5) * FineCX) * CLICK);
	    int cy = (int)(((y + 0.5) * FineCY) * CLICK);
	    int g;

	    i = y * FineW + x;
	    LIMIT(cx, 0, world->cwidth - 1);
	    LIMIT(cy, 0, world->cheight - 1);
	    obs_ship[i] = is_inside(cx, cy, NONBALL_BIT, NULL) != NO_GROUP;
	    obs_ball[i] = 0;
	    g = is_inside(cx, cy, BALL_BIT, NULL);
	    if (g != NO_GROUP) {
		group_t *gp = groupptr_by_id(g);

		if (gp != NULL && gp->type == TREASURE) {
		    int ti = gp->mapobj_ind;

		    obs_ship[i] = 2;	/* a ship touching a target crashes */
		    /* xp2 maps don't link targets to treasures: use the team's */
		    if (ti < 0 || ti >= Num_treasures())
			ti = Bb_target_treasure(gp->team, cx, cy);
		    if (ti >= 0 && ti < 255)
			Zone[i] = (uint8_t)(ti + 1);
		} else
		    obs_ball[i] = 1;
	    }
	}
    }
    /* a ball scores when its edge touches the target: grow by its radius */
    {
	uint8_t *z2 = malloc((size_t)FineW * FineH);
	int r = (int)(options.ballRadius / FineStep + 0.5), dx, dy;

	if (z2 != NULL) {
	    memcpy(z2, Zone, (size_t)FineW * FineH);
	    for (y = 0; y < FineH; y++) {
		for (x = 0; x < FineW; x++) {
		    if (Zone[y * FineW + x] == 0)
			continue;
		    for (dy = -r; dy <= r; dy++) {
			for (dx = -r; dx <= r; dx++) {
			    int xx = x + dx, yy = y + dy;

			    if (dx * dx + dy * dy > r * r)
				continue;
			    if (BbWrap) {
				xx = (xx + FineW) % FineW;
				yy = (yy + FineH) % FineH;
			    } else if (xx < 0 || xx >= FineW || yy < 0 || yy >= FineH)
				continue;
			    if (z2[yy * FineW + xx] == 0 && !obs_ball[yy * FineW + xx])
				z2[yy * FineW + xx] = Zone[y * FineW + x];
			}
		    }
		}
	    }
	    memcpy(Zone, z2, (size_t)FineW * FineH);
	    free(z2);
	}
    }
    if (!Bb_edt(obs_ship, ShipClear) || !Bb_edt(obs_ball, BallClear)) {
	free(obs_ship);
	free(obs_ball);
	Bb_map_free();
	return false;
    }
    for (i = 0; i < FineW * FineH; i++)
	obs_ball[i] = obs_ship[i] == 2;
    if (!Bb_edt(obs_ball, TargetClear)) {
	free(obs_ship);
	free(obs_ball);
	Bb_map_free();
	return false;
    }
    free(obs_ship);
    free(obs_ball);

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
		double sc = MIN(ShipClear[fi], 150.0f) - 0.4 * LENGTH(ox, oy);

		if (sc > best) {
		    best = sc;
		    NavRX[c] = (float)((nx * NavF + fx + 0.5) * FineCX);
		    NavRY[c] = (float)((ny * NavF + fy + 0.5) * FineCY);
		    NavClear[c] = ShipClear[fi];
		}
	    }
	}
    }

    /* Edges per kind with a cost that prefers open space */
    for (k = 0; k < BB_NUM_KINDS; k++) {
	for (c = 0; c < NavN; c++) {
	    int j;

	    if (NavClear[c] < BbPassR[k])
		continue;
	    for (j = 0; j < 8; j++) {
		int n = Bb_nav_nb(c, nb_dx[j], nb_dy[j]);
		double m, len, pen;

		if (n < 0 || NavClear[n] < BbPassR[k])
		    continue;
		m = Bb_segment_clear(ShipClear, NavRX[c], NavRY[c],
				     NavRX[n], NavRY[n], BbEdgeR[k]);
		if (m < BbEdgeR[k])
		    continue;
		len = Bb_dist(NavRX[c], NavRY[c], NavRX[n], NavRY[n]);
		pen = MAX(0.0, (BbPrefR[k] - m) / BbPrefR[k]);
		NavEdge[k][c] |= (uint8_t)(1 << j);
		NavCost[k][c * 8 + j] = (float)(len * (1.0 + BbPrefW[k] * pen * pen));
	    }
	}
    }
    BbMapOk = true;
    BB_LOG("[ballbot] map analysed: fine %dx%d (%.1f px), nav %dx%d\n",
	   FineW, FineH, FineCX, NavW, NavH);
    return true;
}

static bool Bb_map_update(void)
{
    if (BbMapOk && BbPdata == pdata && BbNumPolys == num_polys
	&& BbMapW == world->cwidth && BbMapH == world->cheight)
	return true;
    if (!BbMapOk && BbPdata == pdata && BbNumPolys == num_polys
	&& BbMapW == world->cwidth && BbMapH == world->cheight)
	return false;	/* failed before, don't retry every tick */
    return Bb_map_build();
}

/* Dijkstra from goal (gx, gy) over the nav grid. */
static bool Bb_field_compute(bb_field_t *fd, double gx, double gy, int kind)
{
    int c, r, gcell = Bb_nav_cell(gx, gy), seeded = 0;

    if (fd->dist == NULL) {
	fd->dist = malloc(sizeof(float) * NavN);
	fd->next = malloc(sizeof(int32_t) * NavN);
	if (!fd->dist || !fd->next) {
	    Bb_field_free(fd);
	    return false;
	}
    }
    for (c = 0; c < NavN; c++) {
	fd->dist[c] = BB_INF;
	fd->next[c] = -1;
    }
    HeapN = 0;
    /* Seed open cells near the goal; the goal itself may lie in a BallArea */
    for (r = 1; r <= 6 && seeded == 0; r += (r < 2 ? 1 : 2)) {
	int dx, dy;

	for (dy = -r; dy <= r; dy++) {
	    for (dx = -r; dx <= r; dx++) {
		int n = Bb_nav_nb(gcell, dx, dy);
		double d;

		if (n < 0 || NavClear[n] < BbPassR[kind])
		    continue;
		d = Bb_dist(gx, gy, NavRX[n], NavRY[n]);
		if (d > (r + 0.75) * MAX(NavCX, NavCY))
		    continue;
		if (Bb_segment_clear(BallClear, gx, gy, NavRX[n], NavRY[n], 2.0) < 2.0)
		    continue;
		if (d < fd->dist[n]) {
		    fd->dist[n] = (float)d;
		    fd->next[n] = -2;
		    Bb_heap_push((float)d, n);
		    seeded++;
		}
	    }
	}
    }
    while (HeapN > 0) {
	bb_heap_t e = Bb_heap_pop();
	int j;

	if (e.f > fd->dist[e.c])
	    continue;
	for (j = 0; j < 8; j++) {
	    int n;
	    float nd;

	    if (!(NavEdge[kind][e.c] & (1 << j)))
		continue;
	    n = Bb_nav_nb(e.c, nb_dx[j], nb_dy[j]);
	    nd = e.f + NavCost[kind][e.c * 8 + j];
	    if (nd < fd->dist[n]) {
		fd->dist[n] = nd;
		fd->next[n] = e.c;
		Bb_heap_push(nd, n);
	    }
	}
    }
    fd->kind = kind;
    fd->gcell = gcell;
    fd->gx = gx;
    fd->gy = gy;
    fd->stamp = BbTick;
    fd->used = BbTick;
    fd->valid = true;
    return true;
}

/* Nav cell near (x, y) with the lowest cost to the goal (via straight hop). */
static int Bb_field_best_cell(const bb_field_t *fd, double x, double y, double *val)
{
    int c = Bb_nav_cell(x, y), dx, dy, best = -1;
    double bv = BB_INF;

    for (dy = -1; dy <= 1; dy++) {
	for (dx = -1; dx <= 1; dx++) {
	    int n = Bb_nav_nb(c, dx, dy);
	    double v;

	    if (n < 0 || fd->dist[n] >= BB_INF)
		continue;
	    v = fd->dist[n] + Bb_dist(x, y, NavRX[n], NavRY[n]);
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
static double Bb_field_value(const bb_field_t *fd, double x, double y)
{
    double v;

    if (Bb_field_best_cell(fd, x, y, &v) < 0)
	return 1e5 + Bb_dist(x, y, fd->gx, fd->gy) * 3.0;
    return v;
}

/*
 * Farthest point along the field's path from (x, y) still reachable in a
 * straight line with clearance 'clear' ("string pulling").
 */
static bool Bb_field_carrot(const bb_field_t *fd, double x, double y,
			    double clear, int max_steps, double *cx, double *cy,
			    bool *is_goal)
{
    int cur = Bb_field_best_cell(fd, x, y, NULL), i;

    *is_goal = false;
    if (cur < 0)
	return false;
    *cx = NavRX[cur];
    *cy = NavRY[cur];
    for (i = 0; i < max_steps; i++) {
	int nx = fd->next[cur];

	if (nx == -2) {
	    if (Bb_segment_clear(BallClear, x, y, fd->gx, fd->gy, 2.0) >= 2.0) {
		*cx = fd->gx;
		*cy = fd->gy;
		*is_goal = true;
	    }
	    break;
	}
	if (nx < 0)
	    break;
	if (Bb_segment_clear(ShipClear, x, y, NavRX[nx], NavRY[nx], clear) < clear)
	    break;
	*cx = NavRX[nx];
	*cy = NavRY[nx];
	cur = nx;
    }
    return true;
}

/* Path from (x, y) along the field: nav node positions. */
static int Bb_field_trace(const bb_field_t *fd, double x, double y,
			  float *px, float *py, int max)
{
    int cur = Bb_field_best_cell(fd, x, y, NULL), n = 0;

    while (cur >= 0 && n < max) {
	px[n] = NavRX[cur];
	py[n] = NavRY[cur];
	n++;
	cur = fd->next[cur];
    }
    if (cur == -2 && n < max) {
	px[n] = (float)fd->gx;
	py[n] = (float)fd->gy;
	n++;
    }
    return n;
}

static bb_field_t *Bb_static_field(int tr, int kind)
{
    treasure_t *t;

    if (tr < 0 || tr >= BB_MAX_TREASURES || tr >= Num_treasures() || !BbMapOk)
	return NULL;
    if (StaticF[tr][kind] == NULL) {
	t = Treasure_by_index(tr);
	StaticF[tr][kind] = calloc(1, sizeof(bb_field_t));
	if (StaticF[tr][kind] == NULL)
	    return NULL;
	if (!Bb_field_compute(StaticF[tr][kind], Bb_px(t->pos.cx),
			      Bb_px(t->pos.cy), kind)) {
	    free(StaticF[tr][kind]);
	    StaticF[tr][kind] = NULL;
	    return NULL;
	}
    }
    StaticF[tr][kind]->used = BbTick;
    return StaticF[tr][kind];
}

/* Field to a moving goal, recomputed when older than max_age ticks. */
static bb_field_t *Bb_dyn_field(double gx, double gy, int kind, int max_age)
{
    int gcell, i, lru = 0;
    bb_field_t *near = NULL;
    double near_d = 1e9;

    if (!BbMapOk)
	return NULL;
    gcell = Bb_nav_cell(gx, gy);
    for (i = 0; i < BB_DYN_FIELDS; i++) {
	bb_field_t *fd = &DynF[i];

	if (!fd->valid) {
	    lru = i;
	    continue;
	}
	if (DynF[lru].valid && fd->used < DynF[lru].used)
	    lru = i;
	if (fd->kind != kind)
	    continue;
	if (fd->gcell == gcell) {
	    if (BbTick - fd->stamp <= (unsigned long)max_age || DynBudget <= 0) {
		fd->used = BbTick;
		return fd;
	    }
	    DynBudget--;
	    if (!Bb_field_compute(fd, gx, gy, kind))
		return NULL;
	    return fd;
	} else {
	    double d = Bb_dist(gx, gy, fd->gx, fd->gy);

	    if (d < near_d) {
		near_d = d;
		near = fd;
	    }
	}
    }
    if (DynBudget <= 0) {
	if (near != NULL && near_d < 6 * BLOCK_SZ) {
	    near->used = BbTick;
	    return near;
	}
	return NULL;
    }
    DynBudget--;
    if (!Bb_field_compute(&DynF[lru], gx, gy, kind))
	return NULL;
    return &DynF[lru];
}

/* ------------------------------------------------------------------------
 * Ball run routes: up to four clearly different paths per treasure pair
 * ------------------------------------------------------------------------ */

static double Bb_path_time(const float *px, const float *py, int n)
{
    double t = 0.0, prev = 0.0;
    int i, step = 3;
    bool have_prev = false;

    for (i = step; i < n; i += step) {
	double dx = Bb_wdx(px[i] - px[i - step]), dy = Bb_wdy(py[i] - py[i - step]);
	double len = LENGTH(dx, dy), ang;

	if (len < 1.0)
	    continue;
	t += len / 16.0;
	ang = atan2(dy, dx);
	if (have_prev) {
	    double turn = fabs(remainder(ang - prev, 2 * PI));

	    t += turn * turn * 14.0;
	}
	prev = ang;
	have_prev = true;
    }
    return t;
}

static void Bb_route_store(bb_routes_t *rs, int k, const float *px, const float *py, int n)
{
    int i, m = 0;

    for (i = 0; i < n && m < BB_ROUTE_PTS / 4; i += 4) {
	rs->v[k].px[m] = px[i];
	rs->v[k].py[m] = py[i];
	m++;
    }
    rs->v[k].npts = m;
}

static double Bb_min_dist_to_routes(const bb_routes_t *rs, int nr, double x, double y)
{
    double m = 1e9;
    int k, i;

    for (k = 0; k < nr; k++) {
	for (i = 0; i < rs->v[k].npts; i++) {
	    double d = Bb_dist(x, y, rs->v[k].px[i], rs->v[k].py[i]);

	    if (d < m)
		m = d;
	}
    }
    return m;
}

static bb_routes_t *Bb_routes(int src, int dst)
{
    bb_routes_t *rs;
    bb_field_t *fd, *fs;
    treasure_t *ts;
    static float px[BB_ROUTE_PTS], py[BB_ROUTE_PTS];
    double sx, sy, best, best_time;
    int n, c, k;

    if (src < 0 || dst < 0 || src >= BB_MAX_TREASURES || dst >= BB_MAX_TREASURES)
	return NULL;
    if (Routes[src][dst] != NULL)
	return Routes[src][dst];
    fd = Bb_static_field(dst, BB_KIND_BALL);
    fs = Bb_static_field(src, BB_KIND_BALL);
    if (fd == NULL || fs == NULL)
	return NULL;
    rs = calloc(1, sizeof(*rs));
    if (rs == NULL)
	return NULL;
    Routes[src][dst] = rs;
    ts = Treasure_by_index(src);
    sx = Bb_px(ts->pos.cx);
    sy = Bb_px(ts->pos.cy);
    best = Bb_field_value(fd, sx, sy);

    /* direct route */
    n = Bb_field_trace(fd, sx, sy, px, py, BB_ROUTE_PTS);
    rs->v[0].via = NULL;
    rs->v[0].time = Bb_path_time(px, py, n);
    Bb_route_store(rs, 0, px, py, n);
    rs->n = 1;
    if (best >= 1e5)
	return rs;

    /* via points: in the middle of the run, far from the routes so far */
    for (k = 1; k < BB_ROUTES; k++) {
	int bc = -1, m1, m2;
	double bscore = -1e9;
	bb_field_t *fv;

	for (c = 0; c < NavN; c++) {
	    double a = fs->dist[c], b = fd->dist[c], tot, sep, score;

	    if (a >= BB_INF || b >= BB_INF)
		continue;
	    tot = a + b;
	    if (tot > 1.5 * best || a < 0.3 * tot || a > 0.7 * tot)
		continue;
	    sep = Bb_min_dist_to_routes(rs, rs->n, NavRX[c], NavRY[c]);
	    if (sep < 6 * BLOCK_SZ)
		continue;
	    score = MIN(sep, 15 * BLOCK_SZ) - 0.6 * (tot - best);
	    if (score > bscore) {
		bscore = score;
		bc = c;
	    }
	}
	if (bc < 0)
	    break;
	fv = calloc(1, sizeof(*fv));
	if (fv == NULL || !Bb_field_compute(fv, NavRX[bc], NavRY[bc], BB_KIND_BALL)) {
	    free(fv);
	    break;
	}
	m1 = Bb_field_trace(fv, sx, sy, px, py, BB_ROUTE_PTS);
	m2 = Bb_field_trace(fd, NavRX[bc], NavRY[bc], px + m1, py + m1,
			    BB_ROUTE_PTS - m1);
	rs->v[k].via = fv;
	rs->v[k].vx = NavRX[bc];
	rs->v[k].vy = NavRY[bc];
	rs->v[k].via_rest = fd->dist[bc];
	rs->v[k].time = Bb_path_time(px, py, m1 + m2);
	Bb_route_store(rs, k, px, py, m1 + m2);
	rs->n++;
    }
    best_time = 1e9;
    for (k = 0; k < rs->n; k++)
	best_time = MIN(best_time, rs->v[k].time);
    for (k = 0; k < rs->n; k++) {
	rs->v[k].weight = sqr(best_time / MAX(rs->v[k].time, 1.0));
	BB_LOG("[ballbot] route %d->%d variant %d: time %.0f weight %.2f\n",
	       src, dst, k, rs->v[k].time, rs->v[k].weight);
    }
    rs->done = true;
    return rs;
}

/* ------------------------------------------------------------------------
 * Team knowledge (only what a talking human team could know)
 * ------------------------------------------------------------------------ */

static bool Bb_team_sees(int team, double x, double y)
{
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (pl->team != team || !Bb_player_active(pl))
	    continue;
	if (Bb_dist(Bb_px(pl->pos.cx), Bb_px(pl->pos.cy), x, y) <= BbVisibility)
	    return true;
    }
    return false;
}

static void Bb_update_team(int team)
{
    bb_team_t *tm = &BbTeams[team];
    int i, j;

    /* enemy ships */
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	bb_contact_t *c;
	double x, y;

	if (e->id < 0 || e->id >= NUM_IDS)
	    continue;
	c = &tm->c[e->id];
	if (e->team == team || !Bb_player_active(e) || Player_is_phasing(e)) {
	    c->known = c->seen = false;
	    continue;
	}
	x = Bb_px(e->pos.cx);
	y = Bb_px(e->pos.cy);
	c->seen = Bb_team_sees(team, x, y) && !Player_is_cloaked(e);
	if (c->seen) {
	    c->vx = e->vel.x;
	    c->vy = e->vel.y;
	    c->last_seen = BbTick;
	    c->tows = -1;
	    if (BIT(e->have, HAS_BALL)) {
		ballobject_t *b = Bb_towed_ball(e);

		if (b)
		    c->tows = Bb_treasure_index(b->ball_treasure);
	    }
	} else if (options.playersOnRadar && !Player_is_cloaked(e)) {
	    /* radar: position only, speed from consecutive dots */
	    if (c->known && BbTick - c->last_known == 1) {
		c->vx = 0.7 * c->vx + 0.3 * Bb_wdx(x - c->x);
		c->vy = 0.7 * c->vy + 0.3 * Bb_wdy(y - c->y);
	    }
	} else {
	    if (c->known && BbTick - c->last_known > 3 * 12)
		c->known = false;
	    continue;
	}
	c->known = true;
	c->x = x;
	c->y = y;
	c->last_known = BbTick;
    }

    /* balls */
    for (j = 0; j < Num_treasures() && j < BB_MAX_TREASURES; j++) {
	tm->ball[j].seen = false;
	if (tm->ball[j].known && BbTick - tm->ball[j].time > BB_BALL_MEMORY)
	    tm->ball[j].known = false;
    }
    for (i = 0; i < NumObjs; i++) {
	ballobject_t *b;
	int tr;
	double x, y;

	if (Obj[i]->type != OBJ_BALL)
	    continue;
	b = BALL_IND(i);
	tr = Bb_treasure_index(b->ball_treasure);
	if (tr < 0)
	    continue;
	x = Bb_px(b->pos.cx);
	y = Bb_px(b->pos.cy);
	if (!Bb_team_sees(team, x, y))
	    continue;
	tm->ball[tr].known = tm->ball[tr].seen = true;
	tm->ball[tr].x = x;
	tm->ball[tr].y = y;
	tm->ball[tr].vx = b->vel.x;
	tm->ball[tr].vy = b->vel.y;
	tm->ball[tr].owner_id = b->id;
	tm->ball[tr].home = b->ball_treasure->have;
	tm->ball[tr].time = BbTick;
	if (b->id != NO_ID && b->ball_treasure->team == team) {
	    player_t *o = Player_by_id(b->id);

	    if (o != NULL && o->team != team) {
		tm->thief_id = o->id;
		tm->thief_time = BbTick;
		tm->thief_tr = tr;
	    }
	}
    }

    /* a remembered loose ball that is not where we look any more is gone */
    for (j = 0; j < Num_treasures() && j < BB_MAX_TREASURES; j++) {
	if (tm->ball[j].known && !tm->ball[j].seen && !tm->ball[j].home
	    && Bb_team_sees(team, tm->ball[j].x, tm->ball[j].y))
	    tm->ball[j].known = false;
    }

    /* own balls known to be away: treasure seen empty or ball seen elsewhere */
    for (j = 0; j < Num_treasures() && j < BB_MAX_TREASURES; j++) {
	treasure_t *tr = Treasure_by_index(j);

	if (tr->team != team)
	    continue;
	if (tr->have || tr->empty) {
	    tm->ball_away[j] = false;
	    if (tm->thief_tr == j)
		tm->thief_id = NO_ID;
	    continue;
	}
	if (!tm->ball_away[j]
	    && (Bb_team_sees(team, Bb_px(tr->pos.cx), Bb_px(tr->pos.cy))
		|| (tm->ball[j].seen && !tm->ball[j].home)))
	    tm->ball_away[j] = true;
    }
    if (tm->thief_id != NO_ID) {
	player_t *t = Player_by_id(tm->thief_id);

	/* a dead thief has dropped the ball; seen balls tell the rest */
	if (t == NULL || !Bb_player_active(t)
	    || BbTick - tm->thief_time > BB_THIEF_MEMORY)
	    tm->thief_id = NO_ID;
    }
}

/* Does the team believe the ball of enemy treasure tr is at home? */
static bool Bb_believes_home(int team, int tr)
{
    treasure_t *t = Treasure_by_index(tr);
    bb_team_t *tm = &BbTeams[team];

    if (t->empty)
	return false;
    if (Bb_team_sees(team, Bb_px(t->pos.cx), Bb_px(t->pos.cy)))
	return t->have;
    if (tm->ball[tr].known && !tm->ball[tr].home)
	return false;
    return true;
}

static int Bb_home_treasure(int team, double x, double y)
{
    int i, best = -1;
    double bd = 1e18;

    for (i = 0; i < Num_treasures() && i < BB_MAX_TREASURES; i++) {
	treasure_t *t = Treasure_by_index(i);
	double d;

	if (t->team != team || t->empty)
	    continue;
	d = Bb_dist(x, y, Bb_px(t->pos.cx), Bb_px(t->pos.cy));
	if (d < bd) {
	    bd = d;
	    best = i;
	}
    }
    return best;
}

/* Enemy treasure to attack: nearest one believed to hold its ball. */
static int Bb_enemy_treasure(int team, double x, double y)
{
    int i, best = -1, any = -1;
    double bd = 1e18, ad = 1e18;

    for (i = 0; i < Num_treasures() && i < BB_MAX_TREASURES; i++) {
	treasure_t *t = Treasure_by_index(i);
	double d;

	if (t->team == team || t->empty || t->team == TEAM_NOT_SET)
	    continue;
	d = Bb_dist(x, y, Bb_px(t->pos.cx), Bb_px(t->pos.cy));
	if (d < ad) {
	    ad = d;
	    any = i;
	}
	if (Bb_believes_home(team, i) && d < bd) {
	    bd = d;
	    best = i;
	}
    }
    return best >= 0 ? best : any;
}

bool Robot_ball_game(void)
{
    int i, teams = 0;
    unsigned mask = 0;

    if (!BIT(world->rules->mode, TEAM_PLAY))
	return false;
    for (i = 0; i < Num_treasures(); i++) {
	treasure_t *t = Treasure_by_index(i);

	if (!t->empty && t->team >= 0 && t->team < MAX_TEAMS
	    && !(mask & (1U << t->team))) {
	    mask |= 1U << t->team;
	    teams++;
	}
    }
    return teams >= 2;
}

/* ------------------------------------------------------------------------
 * Roles
 * ------------------------------------------------------------------------ */

typedef struct {
    int		role, arg;
    double	x, y;
} bb_task_t;

/* Seen enemies within range of pl. */
static int Bb_threats(player_t *pl, double range)
{
    bb_team_t *tm = &BbTeams[pl->team];
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy);
    int i, n = 0;

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);

	if (e->team == pl->team || e->id < 0 || e->id >= NUM_IDS)
	    continue;
	if (tm->c[e->id].seen && Bb_dist(x, y, tm->c[e->id].x, tm->c[e->id].y) < range)
	    n++;
    }
    return n;
}

/* Is pl the teammate closest to enemy e (i.e. the one to deal with him)? */
static bool Bb_enemy_is_mine(player_t *pl, player_t *e)
{
    bb_contact_t *c = &BbTeams[pl->team].c[e->id];
    double mine = Bb_dist(Bb_px(pl->pos.cx), Bb_px(pl->pos.cy), c->x, c->y);
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *p = Player_by_index(i);

	if (p == pl || p->team != pl->team || !Bb_player_active(p)
	    || BIT(p->have, HAS_BALL))
	    continue;
	if (Bb_dist(Bb_px(p->pos.cx), Bb_px(p->pos.cy), c->x, c->y) < mine - 50.0)
	    return false;
    }
    return true;
}

static void Bb_set_role(player_t *pl, int role, int arg)
{
    bb_data_t *d = Bb_data(pl);

    if (d->role == role && d->role_arg == arg)
	return;
    BB_LOG("[ballbot] %s (team %d): %s -> %s\n", pl->name, pl->team,
	   bb_role_names[d->role], bb_role_names[role]);
    if (role != BB_ROLE_CARRY && d->role != BB_ROLE_CARRY)
	d->route_chosen = false;
    d->role = role;
    d->role_arg = arg;
    d->spot_valid = false;
}

static void Bb_assign_roles(int team)
{
    bb_team_t *tm = &BbTeams[team];
    player_t *bots[NUM_IDS];
    bool taken[NUM_IDS];
    bb_task_t tasks[32];
    int nbots = 0, ntasks = 0, nfree, i, j, home, enemy_tr, carrier = NO_ID;
    double hx, hy;
    bool ball_game = Robot_ball_game(), home_threat = false, human_home = false;
    int members = 0;

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (pl->team != team || !Bb_player_active(pl))
	    continue;
	if (BIT(pl->have, HAS_BALL)) {
	    ballobject_t *b = Bb_towed_ball(pl);

	    if (b && b->ball_treasure && b->ball_treasure->team != team)
		carrier = pl->id;
	}
	if (Bb_is_ballbot(pl) && nbots < NUM_IDS)
	    bots[nbots++] = pl;
    }
    if (nbots == 0)
	return;

    home = ball_game ? Bb_home_treasure(team, Bb_px(bots[0]->pos.cx),
					 Bb_px(bots[0]->pos.cy)) : -1;
    if (home < 0) {
	for (i = 0; i < nbots; i++)
	    Bb_set_role(bots[i], BB_ROLE_FIGHT, 0);
	return;
    }
    hx = Bb_px(Treasure_by_index(home)->pos.cx);
    hy = Bb_px(Treasure_by_index(home)->pos.cy);
    enemy_tr = Bb_enemy_treasure(team, hx, hy);

    /* enemies close to our base; a human teammate there defends it */
    for (i = 0; i < NumPlayers; i++) {
	player_t *p = Player_by_index(i);

	if (!Bb_player_active(p) || p->id < 0 || p->id >= NUM_IDS)
	    continue;
	if (p->team != team) {
	    if (tm->c[p->id].known
		&& Bb_dist(tm->c[p->id].x, tm->c[p->id].y, hx, hy) < 1300.0)
		home_threat = true;
	} else {
	    members++;
	    if (!Bb_is_ballbot(p) && !BIT(p->have, HAS_BALL)
		&& Bb_dist(Bb_px(p->pos.cx), Bb_px(p->pos.cy), hx, hy) < 700.0)
		human_home = true;
	}
    }
    (void)members;

    nfree = 0;
    for (i = 0; i < nbots; i++) {
	taken[i] = false;
	if (BIT(bots[i]->have, HAS_BALL)) {
	    Bb_set_role(bots[i], BB_ROLE_CARRY, 0);
	    taken[i] = true;
	} else
	    nfree++;
    }

#define ADD_TASK(r, a, tx, ty) do { if (ntasks < 32) { \
	tasks[ntasks].role = (r); tasks[ntasks].arg = (a); \
	tasks[ntasks].x = (tx); tasks[ntasks].y = (ty); ntasks++; } } while (0)

    /* loose balls */
    for (j = 0; j < Num_treasures() && j < BB_MAX_TREASURES; j++) {
	bb_ballinfo_t *bi = &tm->ball[j];
	treasure_t *t = Treasure_by_index(j);

	if (!bi->known || bi->home || bi->owner_id != NO_ID || t->empty)
	    continue;
	if (t->team == team)
	    ADD_TASK(BB_ROLE_RETURN, j, bi->x, bi->y);
	else if (carrier == NO_ID)
	    ADD_TASK(BB_ROLE_GRAB, j, bi->x, bi->y);
    }
    /* our ball stolen */
    {
	bool away = false;

	for (j = 0; j < Num_treasures() && j < BB_MAX_TREASURES; j++) {
	    if (tm->ball_away[j] && !(tm->ball[j].known && tm->ball[j].owner_id == NO_ID
				      && !tm->ball[j].home))
		away = true;
	}
	if (away) {
	    int hunters = nfree >= 3 ? nfree - 1 : nfree;
	    double x = hx, y = hy;

	    if (tm->thief_id != NO_ID) {
		x = tm->c[tm->thief_id].x;
		y = tm->c[tm->thief_id].y;
	    }
	    for (i = 0; i < hunters; i++)
		ADD_TASK(BB_ROLE_HUNT, tm->thief_id, x, y);
	}
    }
    if (carrier != NO_ID) {
	player_t *cp = Player_by_id(carrier);
	double cx = Bb_px(cp->pos.cx), cy = Bb_px(cp->pos.cy);

	ADD_TASK(BB_ROLE_ESCORT, carrier, cx, cy);
	if (!human_home)
	    ADD_TASK(BB_ROLE_DEFEND, home, hx, hy);
	ADD_TASK(BB_ROLE_ESCORT, carrier, cx, cy);
    } else if (enemy_tr >= 0) {
	treasure_t *et = Treasure_by_index(enemy_tr);
	double ex = Bb_px(et->pos.cx), ey = Bb_px(et->pos.cy);

	if (Bb_believes_home(team, enemy_tr))
	    ADD_TASK(BB_ROLE_GRAB, enemy_tr, ex, ey);
	/* attack first unless our base is threatened or the team is small */
	if (home_threat && !human_home)
	    ADD_TASK(BB_ROLE_DEFEND, home, hx, hy);
	ADD_TASK(BB_ROLE_SUPPORT, enemy_tr, ex, ey);
	if (!home_threat && !human_home)
	    ADD_TASK(BB_ROLE_DEFEND, home, hx, hy);
	ADD_TASK(BB_ROLE_MID, enemy_tr, (hx + ex) / 2, (hy + ey) / 2);
    }
    while (ntasks < nbots + 2)
	ADD_TASK(enemy_tr >= 0 ? BB_ROLE_SUPPORT : BB_ROLE_DEFEND,
		 enemy_tr >= 0 ? enemy_tr : home, hx, hy);
#undef ADD_TASK

    /* greedy: tasks in priority order, cheapest free bot each */
    {
	int slot = 0;

	for (j = 0; j < ntasks && nfree > 0; j++) {
	    int bi = -1;
	    double bc = 1e18;

	    for (i = 0; i < nbots; i++) {
		bb_data_t *d = Bb_data(bots[i]);
		double c;

		if (taken[i])
		    continue;
		c = Bb_dist(Bb_px(bots[i]->pos.cx), Bb_px(bots[i]->pos.cy),
			    tasks[j].x, tasks[j].y);
		if (tasks[j].role == BB_ROLE_GRAB) {
		    /* whoever is close and not in a fight takes the ball */
		    c += 350.0 * Bb_threats(bots[i], 300.0);
		    if (d->role == BB_ROLE_GRAB)
			c -= 3 * BLOCK_SZ;
		} else if (d->role == tasks[j].role)
		    c -= 12 * BLOCK_SZ;	/* hysteresis */
		if (c < bc) {
		    bc = c;
		    bi = i;
		}
	    }
	    if (bi < 0)
		break;
	    taken[bi] = true;
	    nfree--;
	    Bb_set_role(bots[bi], tasks[j].role, tasks[j].arg);
	    if (tasks[j].role == BB_ROLE_ESCORT)
		Bb_data(bots[bi])->escort_slot = slot++;
	}
    }
}

/* ------------------------------------------------------------------------
 * Physics model and the sampling flight controller
 * ------------------------------------------------------------------------ */

static bool Bb_lead(double rx, double ry, double wvx, double wvy, double s,
		    double *dir, double *tof);
static player_t *Bb_pick_enemy(player_t *pl, double x, double y, double range,
			       bool seen_only);

/*
 * How close does a shot fired now in direction 'dir' pass a target at
 * relative position (rx, ry) moving with relative velocity (wvx, wvy)
 * (target minus shooter velocity; shots inherit the shooter's velocity)?
 */
static double Bb_shot_miss(int dir, double rx, double ry, double wvx, double wvy,
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

static double Bb_thrust_acc(player_t *pl)
{
    double power = MAX_PLAYER_POWER;
    int a = pl->item[ITEM_AFTERBURNER];

    if (pl->fuel.sum <= 0.0)
	power = MIN_PLAYER_POWER * 0.6;
    else if (a)
	power = AFTER_BURN_POWER(power, a);
    return power / MAX(pl->mass, 1.0);
}

static inline int Bb_turn_toward(int dir, int want, int rate)
{
    int d = Bb_angle_diff(want, dir);

    LIMIT(d, -rate, rate);
    return MOD2(dir + d, RES);
}

/* Outward wall normal at (x, y) (absolute) from a clearance grid. */
static bool Bb_wall_normal(const float *grid, double x, double y,
			   double *nx, double *ny)
{
    int a = Bb_fine_index(x + FineStep, y), b = Bb_fine_index(x - FineStep, y);
    int c = Bb_fine_index(x, y + FineStep), e = Bb_fine_index(x, y - FineStep);
    double gx, gy, l;

    if (a < 0 || b < 0 || c < 0 || e < 0)
	return false;
    gx = grid[a] - grid[b];
    gy = grid[c] - grid[e];
    l = LENGTH(gx, gy);
    if (l < 1e-6)
	return false;
    *nx = gx / l;
    *ny = gy / l;
    return true;
}

/*
 * Wall contact of the ship: walls are not deadly below
 * maxUnshieldedWallBounceSpeed, the ship bounces (playerWallBounceType 3).
 * Moves the ship back to (px, py) when it bounced.
 */
static void Bb_ship_wall(const bb_ctx_t *cx, bb_state_t *s, double px, double py)
{
    double nx, ny, vn, vt, tx, ty, b = options.playerWallBounceBrakeFactor, ch;

    int i = Bb_fine_index(cx->ox + s->sx, cx->oy + s->sy);

    if (i >= 0 && ShipClear[i] >= BB_SHIP_R)
	return;
    if (i < 0 || TargetClear[i] < BB_TARGET_R
	|| LENGTH(s->svx, s->svy) > options.maxUnshieldedWallBounceSpeed) {
	s->crashed = true;
	return;
    }
    if (!Bb_wall_normal(ShipClear, cx->ox + s->sx, cx->oy + s->sy, &nx, &ny))
	return;
    vn = s->svx * nx + s->svy * ny;
    if (vn >= 0.0)
	return;
    tx = -ny;
    ty = nx;
    vt = s->svx * tx + s->svy * ty;
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
    s->svx = vn * nx + vt * tx;
    s->svy = vn * ny + vt * ty;
    s->sx = px;
    s->sy = py;
    s->ship_hits++;
}

/* Wall contact of a towed ball: it bounces, or is lost when too fast. */
static void Bb_ball_wall(const bb_ctx_t *cx, bb_state_t *s, double px, double py)
{
    double nx, ny, vn, f = options.objectWallBounceBrakeFactor;

    if (Bb_ball_clear(cx->ox + s->bx, cx->oy + s->by) >= cx->ball_r)
	return;
    if (!Bb_wall_normal(BallClear, cx->ox + s->bx, cx->oy + s->by, &nx, &ny))
	return;
    vn = s->bvx * nx + s->bvy * ny;
    if (vn >= 0.0)
	return;
    if (LENGTH(s->bvx, s->bvy) > options.maxObjectWallBounceSpeed) {
	s->ball_lost = true;
	return;
    }
    s->bvx = (s->bvx - 2.0 * vn * nx) * f;
    s->bvy = (s->bvy - 2.0 * vn * ny) * f;
    s->bx = px;
    s->by = py;
    s->ball_hits++;
}

/* One robot tick of the model. */
static void Bb_sim_tick(const bb_ctx_t *cx, bb_state_t *s, bool thrust)
{
    double ax = 0.0, ay = 0.0;
    int f;

    if (thrust) {
	ax = cx->acc * tcos(s->dir);
	ay = cx->acc * tsin(s->dir);
    }
    /* constantSpeed (alias oldThrust) moves a thrusting ship extra */
    if (!cx->carry) {
	double px = s->sx, py = s->sy;

	s->sx += s->svx + (0.5 + options.constantSpeed) * ax;
	s->sy += s->svy + (0.5 + options.constantSpeed) * ay;
	s->svx += ax;
	s->svy += ay;
	Bb_ship_wall(cx, s, px, py);
	return;
    }
    for (f = 0; f < cx->frames; f++) {
	double psx = s->sx, psy = s->sy, pbx = s->bx, pby = s->by;
	double dx = s->sx - s->bx, dy = s->sy - s->by;
	double len = LENGTH(dx, dy), ux = 0.0, uy = 0.0, force, damping;

	if (len > 0.0) {
	    ux = dx / len;
	    uy = dy / len;
	}
	force = cx->k * (1.0 - len / cx->L);
	damping = -cx->damp * ((s->svx - s->bvx) * ux + (s->svy - s->bvy) * uy);
	s->svx += ux * (force + damping) / cx->ms * cx->dt;
	s->svy += uy * (force + damping) / cx->ms * cx->dt;
	s->bvx -= ux * (force + damping) / cx->mb * cx->dt;
	s->bvy -= uy * (force + damping) / cx->mb * cx->dt;
	s->bx += s->bvx * cx->dt;
	s->by += s->bvy * cx->dt;
	s->svx += ax * cx->dt * 0.5;
	s->svy += ay * cx->dt * 0.5;
	s->sx += (s->svx + options.constantSpeed * ax) * cx->dt;
	s->sy += (s->svy + options.constantSpeed * ay) * cx->dt;
	s->svx += ax * cx->dt * 0.5;
	s->svy += ay * cx->dt * 0.5;
	Bb_ship_wall(cx, s, psx, psy);
	Bb_ball_wall(cx, s, pbx, pby);
    }
}

/*
 * Towing: always pull, never push.  The rope (ball -> ship) is turned to
 * point along the wanted acceleration of the pair (ball trailing when
 * speeding up, swung ahead for braking); off-rope thrust gives the torque.
 * A cascade sets the rope's angular speed from the angle error and the
 * thrust angle from the angular speed error.  Gains were tuned offline on
 * a copy of the model.
 */
static void Bb_tow_action(const bb_ctx_t *cx, const bb_state_t *s,
			  double vdx, double vdy, double vx, double vy,
			  int *want, bool *thrust)
{
    double rx = s->sx - s->bx, ry = s->sy - s->by, rl = LENGTH(rx, ry);
    double th, thd, err, om, adx, ady, A, wd, sphi, phi, along;
    bool on;

    if (rl < 1.0) {
	*want = s->dir;
	*thrust = false;
	return;
    }
    th = atan2(ry, rx);
    om = (rx * (s->svy - s->bvy) - ry * (s->svx - s->bvx)) / (rl * rl);
    adx = (vdx - vx) * 0.12;
    ady = (vdy - vy) * 0.12;
    if (LENGTH(adx, ady) > 0.5)
	thd = atan2(ady, adx);
    else
	thd = LENGTH(vx, vy) > 1.0 ? atan2(vy, vx) : th;
    err = remainder(thd - th, 2.0 * PI);
    A = cx->alpha * 0.955;
    wd = MIN(sqrt(2.0 * A * 0.493 * fabs(err)), 0.211);
    if (err < 0.0)
	wd = -wd;
    sphi = (wd - om) / 2.76 / A;
    LIMIT(sphi, -1.0, 1.0);
    phi = asin(sphi);
    LIMIT(phi, -1.521, 1.521);
    along = adx * cos(th) + ady * sin(th);
    on = fabs(phi) > 0.171 || (fabs(err) < 1.494 && along > 0.5);
    if (fabs(phi) <= 0.171)
	phi = 0.0;
    *want = MOD2((int)((th + phi) * RES / (2.0 * PI) + RES + 0.5), RES);
    *thrust = on && ABS(Bb_angle_diff(*want, s->dir)) <= 8;
}

/* Built-in policy used after a candidate's own actions run out. */
static void Bb_default_action(const bb_ctx_t *cx, const bb_state_t *s,
			      int *want, bool *thrust)
{
    double px = s->sx, py = s->sy, vx = s->svx, vy = s->svy, acc = cx->acc;
    double dx, dy, dist, vdes, ax, ay, amag;
    int w;

    if (cx->carry) {
	double mt = cx->ms + cx->mb;

	px = (s->sx * cx->ms + s->bx * cx->mb) / mt;
	py = (s->sy * cx->ms + s->by * cx->mb) / mt;
	vx = (s->svx * cx->ms + s->bvx * cx->mb) / mt;
	vy = (s->svy * cx->ms + s->bvy * cx->mb) / mt;
	acc = cx->a_eff;
    }
    dx = cx->cx - px;
    dy = cx->cy - py;
    dist = LENGTH(dx, dy);
    vdes = cx->g->vmax;
    if (cx->carrot_is_goal && cx->g->arrive_r > 0.0)
	vdes = MIN(vdes, sqrt(2.0 * acc * 0.6 * MAX(dist - cx->g->arrive_r * 0.5, 0.0)));
    if (dist > 1.0) {
	ax = dx / dist * vdes - vx;
	ay = dy / dist * vdes - vy;
    } else {
	ax = -vx;
	ay = -vy;
    }
    if (cx->carrot_is_goal) {
	ax += cx->g->gvx;
	ay += cx->g->gvy;
    }
    if (cx->carry) {
	Bb_tow_action(cx, s, ax + vx, ay + vy, vx, vy, want, thrust);
	return;
    }
    amag = LENGTH(ax, ay);
    if (amag < 0.4) {
	*want = s->dir;
	*thrust = false;
	return;
    }
    w = Bb_dir_of(ax, ay);
    *want = w;
    *thrust = ABS(Bb_angle_diff(w, s->dir)) <= 10;
}

/* Free distance along (ux, uy) from (x, y) (relative), up to max. */
static double Bb_ray_free(const bb_ctx_t *cx, double x, double y,
			  double ux, double uy, double need, double max)
{
    double t;

    for (t = 0.0; t < max; t += 2.0 * FineStep) {
	if (Bb_ship_clear(cx->ox + x + ux * t, cx->oy + y + uy * t) < need)
	    return t;
    }
    return max;
}

/*
 * Squared closest distance during one tick between a ship moving from
 * (ax0, ay0) to (ax1, ay1) and an object moving from (bx0, by0) to (bx1, by1).
 * Fast shots would otherwise tunnel through between two samples.
 */
static inline double Bb_cpa2(double ax0, double ay0, double ax1, double ay1,
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

/* Can the ship coast on (absolute) without hitting a target before it turns? */
static bool Bb_coast_safe(double sx, double sy, double svx, double svy)
{
    int t;

    for (t = 0; t <= 8; t++) {
	int i = Bb_fine_index(sx + svx * t, sy + svy * t);

	if (i < 0 || TargetClear[i] < BB_TARGET_R)
	    return false;
    }
    return true;
}

/*
 * Would a ball released at (x, y) (absolute) with velocity (vx, vy) fly
 * into a target of 'team' before hitting a wall?  Returns the ticks or 0.
 */
static int Bb_throw_ticks(double x, double y, double vx, double vy,
			  double sx, double sy, double svx, double svy,
			  int team, int max_t)
{
    double v = LENGTH(vx, vy), rx, ry;
    int t;

    if (team < 0 || v < 2.0)
	return 0;
    if (!Bb_coast_safe(sx, sy, svx, svy))
	return 0;
    /* the released ball must not hit our own ship (sends it home) */
    rx = Bb_wdx(sx - x);
    ry = Bb_wdy(sy - y);
    if ((rx * vx + ry * vy) / v > 0.0
	&& fabs(rx * vy - ry * vx) / v < BB_SHIP_R + options.ballRadius + 8.0)
	return 0;
    for (t = 1; t <= max_t; t++) {
	double qx = x + vx * t, qy = y + vy * t;
	int z;

	if (Bb_ball_clear(qx, qy) < options.ballRadius + 1.0)
	    return 0;
	z = Bb_zone(qx, qy);
	if (z > 0 && z - 1 < Num_treasures()
	    && Treasure_by_index(z - 1)->team == team)
	    return t;
    }
    return 0;
}

/*
 * How close does a free ball on this course pass the goal? -1 when it moves
 * away or a wall is in between. Steers the towing towards a throw.
 */
static double Bb_throw_miss(double x, double y, double vx, double vy,
			    double gx, double gy)
{
    double rx = Bb_wdx(gx - x), ry = Bb_wdy(gy - y), v2 = vx * vx + vy * vy, t;

    if (v2 < 4.0)
	return -1.0;
    t = (rx * vx + ry * vy) / v2;
    if (t <= 0.0
	|| Bb_segment_clear(BallClear, x, y, gx, gy, options.ballRadius)
	   < options.ballRadius)
	return -1.0;
    return LENGTH(rx - vx * t, ry - vy * t);
}

/* Simulate a candidate and return its cost; records the actions taken. */
static double Bb_rollout(const bb_ctx_t *cx, const bb_cand_t *cand, bb_cand_t *rec)
{
    bb_state_t s = cx->s0;
    const bb_goal_t *g = cx->g;
    double cost = 0.0, need = BB_SHIP_R + cx->margin;
    int t, i, hits = 0, bhits = 0;
    bool ended = false;

    /* starting close to a wall (e.g. on a base): don't count it as a crash */
    need = MIN(need, cx->clear0 - 1.0);

    for (t = 0; t < cx->H; t++) {
	int want;
	bool thr;
	double clear, px0 = s.sx, py0 = s.sy;

	if (t < cand->len) {
	    want = cand->dir[t];
	    thr = cand->thr[t];
	} else
	    Bb_default_action(cx, &s, &want, &thr);
	s.dir = Bb_turn_toward(s.dir, want, cx->turn);
	rec->dir[t] = (short)s.dir;
	rec->thr[t] = thr;
	Bb_sim_tick(cx, &s, thr);

	/* pointing at the target now is worth more than later */
	if (cx->has_target && g->aim_w > 0.0 && t < 4) {
	    double tt = t, miss = Bb_shot_miss(s.dir, cx->tx + cx->tvx * tt - px0,
					       cx->ty + cx->tvy * tt - py0,
					       cx->tvx - s.svx, cx->tvy - s.svy,
					       cx->shot_speed, options.shotLife * 0.9, NULL);
	    int err = ABS(Bb_angle_diff(s.dir, cx->lead_dir));

	    /* turning towards the lead counts too, not only being on it */
	    cost += g->aim_w * 12.0 * MIN(err, 32);
	    if (miss < BB_SHIP_R + 4.0)
		cost -= g->aim_w * 400.0 * (4 - t);
	}

	/*
	 * Ship against walls and BallAreas.  Slow enough it only bounces,
	 * which costs speed (simulated) and a little extra.  The mid point
	 * catches a fast ship passing through a thin wall in the model.
	 */
	if (s.crashed
	    || Bb_ship_clear(cx->ox + 0.5 * (s.sx + px0),
			     cx->oy + 0.5 * (s.sy + py0)) < 2.0) {
	    cost += 1e6 * (1.0 + (double)(cx->H - t) / cx->H);
	    ended = true;
	    break;
	}
	if (s.ship_hits > hits) {
	    cost += BB_BOUNCE_COST;
	    hits = s.ship_hits;
	}
	clear = MAX(Bb_ship_clear(cx->ox + s.sx, cx->oy + s.sy), BB_SHIP_R);
	if (clear < need + 22.0)
	    cost += sqr(need + 22.0 - clear) * 0.8 * g->wall_w;
	/* speed limit as a running cost: no "accelerate now, brake later" */
	{
	    double vx = s.svx, vy = s.svy, v;

	    if (cx->carry) {
		vx = (s.svx * cx->ms + s.bvx * cx->mb) / (cx->ms + cx->mb);
		vy = (s.svy * cx->ms + s.bvy * cx->mb) / (cx->ms + cx->mb);
	    }
	    v = LENGTH(vx, vy);
	    if (v > g->vmax)
		cost += sqr(v - g->vmax) * 6.0;
	}

	if (cx->carry) {
	    double len = LENGTH(s.sx - s.bx, s.sy - s.by);
	    double ratio = 1.0 - len / cx->L;
	    int z;

	    if (ABS(ratio) > cx->ratio_max * 0.9) {
		cost += 2e5 * (1.0 + (double)(cx->H - t) / cx->H);
		ended = true;
		break;
	    }
	    if (s.ball_lost) {
		cost += 4e5;
		ended = true;
		break;
	    }
	    if (s.ball_hits > bhits) {
		cost += BB_BOUNCE_COST * 0.5;
		bhits = s.ball_hits;
	    }
	    /*
	     * Releasing here would send the ball into our target: a throw.
	     * Faster than towing it in and it frees us, so worth more.
	     */
	    if (cx->throw_check && (t & 1)) {
		int tt = Bb_throw_ticks(cx->ox + s.bx, cx->oy + s.by, s.bvx, s.bvy,
					cx->ox + s.sx, cx->oy + s.sy, s.svx, s.svy,
					g->score_team, BB_THROW_TICKS);
		double miss;

		if (tt > 0)
		    return cost - 2e6 * (1.0 - (t + tt) / 120.0);
		/* in sight of the goal: swing the ball onto a course to it */
		miss = Bb_throw_miss(cx->ox + s.bx, cx->oy + s.by, s.bvx, s.bvy,
				     g->thx, g->thy);
		if (miss >= 0.0 && miss < 250.0)
		    cost -= 3e4 * (1.0 - miss / 250.0);
	    }
	    z = Bb_zone(cx->ox + s.bx, cx->oy + s.by);
	    if (z > 0 && g->score_team >= 0 && z - 1 < Num_treasures()
		&& Treasure_by_index(z - 1)->team == g->score_team
		&& Bb_coast_safe(cx->ox + s.sx, cx->oy + s.sy, s.svx, s.svy)) {
		cost -= 3e6 * (1.0 - 0.02 * t);
		return cost;
	    }
	}

	/* visible enemy shots, checked along the whole tick */
	for (i = 0; i < cx->nshots; i++) {
	    const bb_shot_t *sh = &cx->shots[i];
	    double t0 = t, t1 = t + 1.0;

	    if (t0 < sh->t0 || t0 - sh->t0 > sh->life)
		continue;
	    t0 -= sh->t0;
	    t1 -= sh->t0;
	    if (Bb_cpa2(px0, py0, s.sx, s.sy, sh->x + sh->vx * t0, sh->y + sh->vy * t0,
			sh->x + sh->vx * t1, sh->y + sh->vy * t1)
		< sqr(BB_SHIP_R + 4.0 + cx->margin)) {
		cost += sh->weight * (1.0 - 0.5 * t / cx->H);
		break;
	    }
	}
	/* enemy ships: collisions and keeping distance */
	for (i = 0; i < cx->nen; i++) {
	    const bb_enemy_t *e = &cx->en[i];
	    double tt = t + 1.0, ex = e->x + e->vx * tt, ey = e->y + e->vy * tt;
	    double d = LENGTH(s.sx - ex, s.sy - ey);

	    /* crashing into a ship kills (allowPlayerCrashes) */
	    if (Bb_cpa2(px0, py0, s.sx, s.sy, ex - e->vx, ey - e->vy, ex, ey)
		< sqr(2.0 * BB_SHIP_R + 8.0))
		cost += 5e5 * (1.0 - 0.5 * t / cx->H);
	    else if (g->enemy_w > 0.0 && d < 250.0)
		cost += g->enemy_w * (250.0 - d) * 0.15;
	    /*
	     * An enemy touching our towed ball sends it home, and they try:
	     * assume he accelerates straight at where the ball is now.
	     */
	    if (cx->carry) {
		double ux = cx->s0.bx - e->x, uy = cx->s0.by - e->y, ul = LENGTH(ux, uy);
		double pa = MIN(0.5 * cx->acc * tt * tt, ul), db;

		if (ul > 1.0) {
		    ex += ux / ul * pa;
		    ey += uy / ul * pa;
		}
		db = LENGTH(s.bx - ex, s.by - ey);
		if (db < BB_SHIP_R + cx->ball_r + 8.0)
		    cost += 1e5 * (1.0 - 0.5 * t / cx->H);
		else if (db < 160.0)
		    cost += (160.0 - db) * 30.0;
	    }
	}
	/* other balls: ram the one we want, never touch the others */
	for (i = 0; i < cx->nballs; i++) {
	    const bb_oball_t *b = &cx->balls[i];
	    double tt = t + 1.0;
	    double d = LENGTH(s.sx - (b->x + b->vx * tt), s.sy - (b->y + b->vy * tt));

	    if (b->mode == BB_BALL_HIT && d < BB_SHIP_R + BALL_RADIUS - 3.0)
		return cost - 3e4 * (1.0 - 0.02 * t);
	    if (b->mode == BB_BALL_AVOID && d < BB_SHIP_R + BALL_RADIUS + 6.0) {
		cost += 2e5;
		ended = true;
		break;
	    }
	}
	if (ended)
	    break;
    }
    if (ended)
	return cost;

    /* terminal cost */
    {
	double px = s.sx, py = s.sy, vx = s.svx, vy = s.svy, acc = cx->acc;
	double ctg, v, dneed, free, gd;

	/*
	 * Towing: the centre of mass is what has to get there.  A swinging
	 * ball or one that leads is no cost by itself, only the time lost.
	 */
	if (cx->carry) {
	    double mt = cx->ms + cx->mb;

	    px = (s.sx * cx->ms + s.bx * cx->mb) / mt;
	    py = (s.sy * cx->ms + s.by * cx->mb) / mt;
	    vx = (s.svx * cx->ms + s.bvx * cx->mb) / mt;
	    vy = (s.svy * cx->ms + s.bvy * cx->mb) / mt;
	    acc = cx->a_eff;
	}
	if (g->field)
	    ctg = Bb_field_value(g->field, cx->ox + px, cx->oy + py);
	else
	    ctg = Bb_dist(cx->ox + px, cx->oy + py, g->gx, g->gy);
	gd = Bb_dist(cx->ox + px, cx->oy + py, g->gx, g->gy);
	/* close to the target the ball itself has to get in: straight distance */
	if (cx->carry && g->score_team >= 0) {
	    double qx = cx->ox + s.bx, qy = cx->oy + s.by;
	    double de = Bb_dist(qx, qy, g->gx, g->gy);

	    if (de < 600.0 && de < ctg
		&& Bb_segment_clear(BallClear, qx, qy, g->gx, g->gy, cx->ball_r)
		   >= cx->ball_r)
		ctg = de;
	}
	if (g->arrive_r > 0.0)
	    ctg = MAX(0.0, ctg - g->arrive_r);
	cost += ctg * 1.0;

	/* hold position: match the goal's speed near it */
	if (g->arrive_r > 0.0 && gd < g->arrive_r * 1.5 + 60.0)
	    cost += (sqr(vx - g->gvx) + sqr(vy - g->gvy)) * g->match_w;
	/* still able to stop (or turn) before the next wall? */
	v = LENGTH(vx, vy);
	if (v > 2.0) {
	    dneed = v * v / (2.0 * acc) + v * (RES / 2.0) / cx->turn * 0.6;
	    free = Bb_ray_free(cx, s.sx, s.sy, vx / v, vy / v, need + 4.0, dneed + 40.0);
	    if (free < dneed)
		cost += (dneed - free) * (cx->carry ? 0.5 : 6.0);
	}
	if (v > g->vmax)
	    cost += sqr(v - g->vmax) * 20.0;

	/* aiming at the target */
	if (cx->has_target && g->aim_w > 0.0) {
	    double tt = cx->H, rx = cx->tx + cx->tvx * tt - s.sx;
	    double ry = cx->ty + cx->tvy * tt - s.sy, dist = LENGTH(rx, ry);

	    if (dist < 900.0) {
		double tof = dist / MAX(cx->shot_speed, 1.0);
		int lead = Bb_dir_of(rx + (cx->tvx - vx) * tof, ry + (cx->tvy - vy) * tof);
		double err = ABS(Bb_angle_diff(lead, s.dir)) * (360.0 / RES);

		cost += g->aim_w * err * 1.5;
	    }
	}
	/* staying in the line of fire of seen enemies */
	for (i = 0; i < cx->nen; i++) {
	    const bb_enemy_t *e = &cx->en[i];
	    double ex = e->x + e->vx * cx->H, ey = e->y + e->vy * cx->H;
	    double d = LENGTH(s.sx - ex, s.sy - ey);

	    if (!e->seen || d > 700.0)
		continue;
	    if (ABS(Bb_angle_diff(Bb_dir_of(s.sx - ex, s.sy - ey), e->dir)) < 5)
		cost += (700.0 - d) * 0.5;
	}
    }
    return cost;
}

static void Bb_cand_set(bb_cand_t *c, int dir, bool thr, int len)
{
    int i;

    c->len = len;
    for (i = 0; i < len; i++) {
	c->dir[i] = (short)dir;
	c->thr[i] = thr;
    }
}

/* Choose and apply this tick's heading and thrust. */
static void Bb_fly(player_t *pl, bb_data_t *d, const bb_goal_t *g, int level)
{
    static bb_ctx_t cx;
    static bb_cand_t cand[BB_MAX_CAND];
    bb_cand_t rec, best_rec;
    ballobject_t *ball = Bb_towed_ball(pl);
    double best_cost = 1e30;
    int nc = 0, i, h, nheads, want, prev_dir = -1;
    int specials[12], nspec = 0;
    double f = level / 100.0;
    bool is_goal;

    memset(&cx.s0, 0, sizeof(cx.s0));
    cx.pl = pl;
    cx.d = d;
    cx.g = g;
    cx.ox = Bb_px(pl->pos.cx);
    cx.oy = Bb_px(pl->pos.cy);
    cx.s0.svx = pl->vel.x;
    cx.s0.svy = pl->vel.y;
    cx.s0.dir = pl->dir;
    cx.acc = Bb_thrust_acc(pl);
    cx.ms = MAX(pl->mass, 1.0);
    cx.turn = Bb_turn_rate(level);
    cx.frames = MAX(1, (int)(1.0 / timeStep + 0.5));
    cx.dt = 1.0 / cx.frames;
    cx.margin = 2.0 + (1.0 - f) * 6.0;
    cx.clear0 = Bb_ship_clear(cx.ox, cx.oy);
    cx.carry = ball != NULL;
    cx.ball_r = options.ballRadius;
    cx.shot_speed = options.shotSpeed;
    cx.H = cx.carry ? 22 : 16;
    if (cx.carry) {
	cx.mb = MAX(ball->mass, 1.0);
	cx.L = options.ballConnectorLength;
	cx.k = options.ballConnectorSpringConstant;
	cx.damp = options.ballConnectorDamping;
	cx.ratio_max = options.maxBallConnectorRatio;
	cx.s0.bx = Bb_wdx(Bb_px(ball->pos.cx) - cx.ox);
	cx.s0.by = Bb_wdy(Bb_px(ball->pos.cy) - cx.oy);
	cx.s0.bvx = ball->vel.x;
	cx.s0.bvy = ball->vel.y;
	cx.a_eff = cx.acc * cx.ms / (cx.ms + cx.mb);
	{
	    /* angular acceleration of the pair for full thrust across the rope */
	    double rs = cx.L * cx.mb / (cx.ms + cx.mb), rb = cx.L - rs;

	    cx.alpha = cx.acc * cx.ms * rs / (cx.ms * rs * rs + cx.mb * rb * rb);
	}
	cx.throw_check = g->score_team >= 0
	    && Bb_dist(cx.ox + cx.s0.bx, cx.oy + cx.s0.by, g->thx, g->thy) < 900.0;
    } else
	cx.a_eff = cx.acc;

    /* carrot along the field (or the goal itself) */
    {
	double ax = cx.ox, ay = cx.oy, carx, cary;

	if (cx.carry) {
	    ax = (cx.ox * cx.ms + (cx.ox + cx.s0.bx) * cx.mb) / (cx.ms + cx.mb);
	    ay = (cx.oy * cx.ms + (cx.oy + cx.s0.by) * cx.mb) / (cx.ms + cx.mb);
	}
	if (g->field && Bb_field_carrot(g->field, ax, ay, BB_SHIP_R + 10.0, 8,
					&carx, &cary, &is_goal)) {
	    cx.cx = Bb_wdx(carx - cx.ox);
	    cx.cy = Bb_wdy(cary - cx.oy);
	    cx.carrot_is_goal = is_goal
		|| Bb_dist(carx, cary, g->gx, g->gy) < 2.0 * MAX(NavCX, NavCY);
	} else {
	    cx.cx = Bb_wdx(g->gx - cx.ox);
	    cx.cy = Bb_wdy(g->gy - cx.oy);
	    cx.carrot_is_goal = true;
	}
    }

    /* threats: enemy shots in view and known enemy ships */
    cx.nshots = 0;
    for (i = 0; i < NumObjs && cx.nshots < BB_MAX_SHOTS; i++) {
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
	rx = Bb_wdx(Bb_px(o->pos.cx) - cx.ox);
	ry = Bb_wdy(Bb_px(o->pos.cy) - cx.oy);
	if (LENGTH(rx, ry) > BbVisibility)
	    continue;
	/* closest approach to a ship that keeps its speed */
	dvx = o->vel.x - pl->vel.x;
	dvy = o->vel.y - pl->vel.y;
	tc = -(rx * dvx + ry * dvy) / MAX(dvx * dvx + dvy * dvy, 1e-6);
	LIMIT(tc, 0.0, (double)cx.H);
	dmin = LENGTH(rx + dvx * tc, ry + dvy * tc);
	if (dmin > 160.0 + cx.H * 2.0 * cx.acc)
	    continue;
	cx.shots[cx.nshots].x = rx;
	cx.shots[cx.nshots].y = ry;
	cx.shots[cx.nshots].vx = o->vel.x;
	cx.shots[cx.nshots].vy = o->vel.y;
	cx.shots[cx.nshots].life = o->life;
	cx.shots[cx.nshots].t0 = 0.0;
	cx.shots[cx.nshots].weight = 6e5;
	cx.nshots++;
    }
    cx.nen = 0;
    cx.has_target = false;
    {
	bb_team_t *tm = &BbTeams[pl->team];
	int delay = (int)((1.0 - f) * 5.0 + 0.5);

	for (i = 0; i < NumPlayers && cx.nen < BB_MAX_ENEMIES; i++) {
	    player_t *e = Player_by_index(i);
	    bb_contact_t *c;
	    double rx, ry;

	    if (e->team == pl->team || e->id < 0 || e->id >= NUM_IDS)
		continue;
	    c = &tm->c[e->id];
	    if (!c->known)
		continue;
	    rx = Bb_wdx(c->x - c->vx * delay - cx.ox);
	    ry = Bb_wdy(c->y - c->vy * delay - cx.oy);
	    if (LENGTH(rx, ry) > 900.0)
		continue;
	    cx.en[cx.nen].x = rx;
	    cx.en[cx.nen].y = ry;
	    cx.en[cx.nen].vx = c->vx;
	    cx.en[cx.nen].vy = c->vy;
	    cx.en[cx.nen].dir = e->dir;
	    cx.en[cx.nen].seen = c->seen;
	    cx.nen++;
	    if (g->target == e) {
		cx.has_target = true;
		cx.tx = rx;
		cx.ty = ry;
		cx.tvx = c->vx;
		cx.tvy = c->vy;
	    }
	}
    }

    /*
     * Virtual shots: a close enemy pointing our way may fire any moment.
     * Leaving his line of fire early is the only defence at short range.
     */
    for (i = 0; i < cx.nen && cx.nshots < BB_MAX_SHOTS - 3; i++) {
	const bb_enemy_t *e = &cx.en[i];
	double dist = LENGTH(e->x, e->y);
	int k, toward = Bb_dir_of(-e->x, -e->y);

	if (!e->seen || dist > 700.0 || ABS(Bb_angle_diff(e->dir, toward)) > 16)
	    continue;
	for (k = 0; k < 3; k++) {
	    bb_shot_t *sh = &cx.shots[cx.nshots++];

	    sh->t0 = 2.0 * k;
	    sh->x = e->x + e->vx * sh->t0;
	    sh->y = e->y + e->vy * sh->t0;
	    sh->vx = e->vx + options.shotSpeed * tcos(e->dir);
	    sh->vy = e->vy + options.shotSpeed * tsin(e->dir);
	    sh->life = options.shotLife;
	    sh->weight = 3e3;
	}
    }

    /* other balls in view */
    cx.nballs = 0;
    cx.team = pl->team;
    for (i = 0; i < NumObjs && cx.nballs < BB_MAX_BALLS; i++) {
	ballobject_t *b;
	double rx, ry;
	int tr, mode;

	if (Obj[i]->type != OBJ_BALL)
	    continue;
	b = BALL_IND(i);
	if (b->id == pl->id || b->ball_treasure == NULL)
	    continue;
	rx = Bb_wdx(Bb_px(b->pos.cx) - cx.ox);
	ry = Bb_wdy(Bb_px(b->pos.cy) - cx.oy);
	if (LENGTH(rx, ry) > MIN(BbVisibility, 700.0))
	    continue;
	tr = Bb_treasure_index(b->ball_treasure);
	mode = BB_BALL_AVOID;
	if (tr >= 0 && tr == g->hit_tr)
	    mode = BB_BALL_HIT;
	else if (b->ball_treasure->team == pl->team && !b->ball_treasure->have
		 && !cx.carry) {
	    player_t *o = b->id != NO_ID ? Player_by_id(b->id) : NULL;

	    /* our ball loose or with the enemy: touching sends it home */
	    if (o == NULL || o->team != pl->team)
		mode = BB_BALL_HIT;
	}
	cx.balls[cx.nballs].x = rx;
	cx.balls[cx.nballs].y = ry;
	cx.balls[cx.nballs].vx = b->vel.x;
	cx.balls[cx.nballs].vy = b->vel.y;
	cx.balls[cx.nballs].mode = mode;
	cx.nballs++;
    }

    /* candidates: headings x {short thrust, short coast, long thrust} */
    nheads = level >= 50 ? 16 : 8;
    specials[nspec++] = pl->dir;
    specials[nspec++] = Bb_dir_of(cx.cx, cx.cy);
    if (LENGTH(pl->vel.x, pl->vel.y) > 1.0)
	specials[nspec++] = Bb_dir_of(-pl->vel.x, -pl->vel.y);
    if (cx.carry) {
	int rope = Bb_dir_of(-cx.s0.bx, -cx.s0.by);

	specials[nspec++] = rope;
	specials[nspec++] = MOD2(rope + RES / 16, RES);
	specials[nspec++] = MOD2(rope - RES / 16, RES);
	specials[nspec++] = MOD2(rope + RES / 8, RES);
	specials[nspec++] = MOD2(rope - RES / 8, RES);
	specials[nspec++] = MOD2(rope + RES / 2, RES);
    }
    if (cx.has_target) {
	double ldir, tof;

	if (Bb_lead(cx.tx, cx.ty, cx.tvx - pl->vel.x, cx.tvy - pl->vel.y,
		    cx.shot_speed, &ldir, &tof))
	    cx.lead_dir = MOD2((int)(ldir + 0.5), RES);
	else
	    cx.lead_dir = Bb_dir_of(cx.tx, cx.ty);
	specials[nspec++] = cx.lead_dir;
    }
    for (h = 0; h < nheads + nspec; h++) {
	int dir = h < nheads ? (h * RES) / nheads : specials[h - nheads];

	if (nc + 5 > BB_MAX_CAND - 1)
	    break;
	Bb_cand_set(&cand[nc++], dir, true, 2);
	Bb_cand_set(&cand[nc++], dir, false, 2);
	Bb_cand_set(&cand[nc++], dir, true, 5);
	if (h >= nheads) {
	    /* special headings (aim, rope, brake) are worth holding longer */
	    Bb_cand_set(&cand[nc++], dir, false, 5);
	    Bb_cand_set(&cand[nc++], dir, false, 9);
	}
    }
    /* last plan, shifted by one tick */
    if (d->plan_len > 1) {
	cand[nc].len = d->plan_len - 1;
	for (i = 1; i < d->plan_len; i++) {
	    cand[nc].dir[i - 1] = d->plan_dir[i];
	    cand[nc].thr[i - 1] = d->plan_thr[i];
	}
	prev_dir = cand[nc].dir[0];
	nc++;
    }

    for (i = 0; i < nc; i++) {
	double c = Bb_rollout(&cx, &cand[i], &rec);

	/* a little inertia against dithering between equal plans */
	if (i == nc - 1 && prev_dir >= 0)
	    c -= 30.0;
	if (c < best_cost) {
	    best_cost = c;
	    best_rec = rec;
	}
    }
    if (best_cost >= 1e30)
	return;

    d->plan_len = cx.H;
    memcpy(d->plan_dir, best_rec.dir, sizeof(short) * cx.H);
    memcpy(d->plan_thr, best_rec.thr, sizeof(bool) * cx.H);

    /* apply: heading (rate limited, as in the model) and thrust */
    want = best_rec.dir[0];
    if (want != pl->dir) {
	pl->turnspeed = 0;
	pl->turnresistance = 0;
	pl->turnacc = 0;
	pl->turnvel = 0;
	Player_set_float_dir(pl, (double)want);
	pl->wanted_float_dir = pl->float_dir;
	Turn_player(pl, true);
    }
    pl->power = MAX_PLAYER_POWER;
    Thrust(pl, best_rec.thr[0]);
}

/* ------------------------------------------------------------------------
 * Combat
 * ------------------------------------------------------------------------ */

/* Lead direction for a shot that inherits the shooter's velocity. */
static bool Bb_lead(double rx, double ry, double wvx, double wvy, double s,
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

static void Bb_fire(player_t *pl, bb_data_t *d, int level)
{
    bb_team_t *tm = &BbTeams[pl->team];
    double ox = Bb_px(pl->pos.cx), oy = Bb_px(pl->pos.cy), f = level / 100.0;
    int i;

    if (!Can_shoot_normal_shot(pl) || Player_is_phasing(pl))
	return;
    if (BbTick - d->aim_time > 12) {
	/* aiming error drifts slowly, like a human hand */
	d->aim_off = (rfrac() - 0.5) * 2.0 * (1.0 - f) * 4.0;
	d->aim_time = BbTick;
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	bb_contact_t *c;
	double rx, ry, dist, tof = 0.0, tol, hx, hy;

	if (e->team == pl->team && BIT(world->rules->mode, TEAM_PLAY))
	    continue;
	if (e == pl || e->id < 0 || e->id >= NUM_IDS || !Bb_player_active(e))
	    continue;
	c = &tm->c[e->id];
	if (!c->seen)
	    continue;
	rx = Bb_wdx(c->x - ox);
	ry = Bb_wdy(c->y - oy);
	dist = LENGTH(rx, ry);
	if (dist > BbVisibility * 0.9)
	    continue;
	{
	    /* aiming error drifts slowly, like a human hand */
	    int fdir = MOD2(pl->dir + (int)(d->aim_off + (d->aim_off < 0 ? -0.5 : 0.5)), RES);
	    double miss = Bb_shot_miss(fdir, rx, ry, c->vx - pl->vel.x, c->vy - pl->vel.y,
				       options.shotSpeed, options.shotLife * 0.9, &tof);

	    tol = BB_SHIP_R + 4.0 - (1.0 - f) * 6.0;
	    if (miss > tol)
		continue;
	}
	/* walls (and BallAreas) stop shots */
	hx = ox + rx + c->vx * tof;
	hy = oy + ry + c->vy * tof;
	if (Bb_segment_clear(ShipClear, ox, oy, hx, hy, 1.0) < 1.0)
	    continue;
	SET_BIT(pl->used, USES_SHOT);
	return;
    }
}

/* Best enemy to fight near (x, y) within range, preferring seen ones. */
static player_t *Bb_pick_enemy(player_t *pl, double x, double y, double range,
			       bool seen_only)
{
    bb_team_t *tm = &BbTeams[pl->team];
    player_t *best = NULL;
    double bd = range;
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	bb_contact_t *c;
	double dd;

	if (e->team == pl->team || e->id < 0 || e->id >= NUM_IDS)
	    continue;
	c = &tm->c[e->id];
	if (!c->known || (seen_only && !c->seen))
	    continue;
	dd = Bb_dist(x, y, c->x, c->y) + (c->seen ? 0.0 : 200.0);
	if (dd < bd) {
	    bd = dd;
	    best = e;
	}
    }
    return best;
}

/* Goal to fight enemy e: close in to the engagement distance. */
static void Bb_goal_engage(player_t *pl, bb_goal_t *g, player_t *e, double range)
{
    bb_contact_t *c = &BbTeams[pl->team].c[e->id];
    double ox = Bb_px(pl->pos.cx), oy = Bb_px(pl->pos.cy);
    double dist = Bb_dist(ox, oy, c->x, c->y), lead = MIN(dist / 20.0, 15.0);

    g->gx = c->x + c->vx * lead;
    g->gy = c->y + c->vy * lead;
    g->gvx = c->vx;
    g->gvy = c->vy;
    g->arrive_r = range;
    g->target = e;
    g->aim_w = 1.0;
    g->vmax = 18.0;
    g->field = Bb_segment_clear(ShipClear, ox, oy, g->gx, g->gy, BB_SHIP_R + 8.0)
	>= BB_SHIP_R + 8.0 ? NULL : Bb_dyn_field(g->gx, g->gy, BB_KIND_SHIP, 8);
}

/* ------------------------------------------------------------------------
 * Behaviours
 * ------------------------------------------------------------------------ */

static void Bb_goal_point(bb_goal_t *g, double x, double y, double arrive_r,
			  double vmax, int kind)
{
    g->gx = x;
    g->gy = y;
    g->gvx = g->gvy = 0.0;
    g->arrive_r = arrive_r;
    g->vmax = vmax;
    g->field = Bb_dyn_field(x, y, kind, 24);
}

/* Open point near (x, y): the nav node with the best clearance nearby. */
static void Bb_open_point(double *x, double *y, double need)
{
    int c = Bb_nav_cell(*x, *y), dx, dy, best = -1;
    double bd = 1e18;

    if (Bb_ship_clear(*x, *y) >= need)
	return;
    for (dy = -3; dy <= 3; dy++) {
	for (dx = -3; dx <= 3; dx++) {
	    int n = Bb_nav_nb(c, dx, dy);
	    double d;

	    if (n < 0 || NavClear[n] < need)
		continue;
	    d = Bb_dist(*x, *y, NavRX[n], NavRY[n]);
	    if (d < bd) {
		bd = d;
		best = n;
	    }
	}
    }
    if (best >= 0) {
	*x = NavRX[best];
	*y = NavRY[best];
    }
}

/* Direction a ball run from (bx, by) to treasure dst starts with. */
static void Bb_exit_dir(int dst, const bb_field_t *via, double bx, double by,
			double *ex, double *ey)
{
    float px[12], py[12];
    const bb_field_t *fd = via ? via : Bb_static_field(dst, BB_KIND_BALL);
    int n;
    double dx, dy, l;

    *ex = 1.0;
    *ey = 0.0;
    if (fd == NULL)
	return;
    n = Bb_field_trace(fd, bx, by, px, py, 12);
    if (n < 2)
	return;
    dx = Bb_wdx(px[n - 1] - bx);
    dy = Bb_wdy(py[n - 1] - by);
    l = LENGTH(dx, dy);
    if (l > 1.0) {
	*ex = dx / l;
	*ey = dy / l;
    }
}

/* Is any known enemy close to the remaining part of a route? */
static bool Bb_route_guarded(int team, const bb_routes_t *rs, int k, double from_val,
			     const bb_field_t *fd)
{
    bb_team_t *tm = &BbTeams[team];
    int i, j;

    for (i = 0; i < NumPlayers; i++) {
	player_t *e = Player_by_index(i);
	bb_contact_t *c;

	if (e->team == team || e->id < 0 || e->id >= NUM_IDS)
	    continue;
	c = &tm->c[e->id];
	if (!c->known)
	    continue;
	for (j = 0; j < rs->v[k].npts; j++) {
	    if (Bb_field_value(fd, rs->v[k].px[j], rs->v[k].py[j]) > from_val)
		continue;
	    if (Bb_dist(c->x, c->y, rs->v[k].px[j], rs->v[k].py[j]) < 300.0)
		return true;
	}
    }
    return false;
}

/* Pick a ball run route: weighted by quality, avoiding guarded ones. */
static void Bb_choose_route(player_t *pl, bb_data_t *d, int src, int dst,
			    double x, double y, bool reroute)
{
    bb_routes_t *rs = Bb_routes(src, dst);
    bb_field_t *fd = Bb_static_field(dst, BB_KIND_BALL);
    double w[BB_ROUTES], sum = 0.0, r, here;
    int k, old_var = d->route_var;

    d->route_src = src;
    d->route_dst = dst;
    d->route_var = 0;
    d->via_passed = false;
    d->route_chosen = true;
    d->route_time = BbTick;
    if (rs == NULL || fd == NULL || rs->n <= 1)
	return;
    here = Bb_field_value(fd, x, y);
    for (k = 0; k < rs->n; k++) {
	w[k] = rs->v[k].weight;
	if (k > 0 && rs->v[k].via_rest > here - 4 * BLOCK_SZ)
	    w[k] = 0.0;		/* via point no longer ahead */
	else if (k > 0 && reroute
		 && Bb_field_value(rs->v[k].via, x, y) + rs->v[k].via_rest
		    > 1.25 * here + 8 * BLOCK_SZ)
	    w[k] = 0.0;		/* too much of a detour from here */
	else if (Bb_route_guarded(pl->team, rs, k, here, fd))
	    w[k] *= reroute ? 0.0 : 0.05;
	sum += w[k];
    }
    if (sum <= 0.0) {
	if (reroute)
	    d->route_var = old_var;	/* nothing better: keep going */
	return;
    }
    r = rfrac() * sum;
    for (k = 0; k < rs->n; k++) {
	r -= w[k];
	if (r <= 0.0)
	    break;
    }
    d->route_var = MIN(k, rs->n - 1);
    BB_LOG("[ballbot] %s takes route %d of %d (%d->%d)\n", pl->name,
	   d->route_var, rs->n, src, dst);
}

/*
 * The best line for a throw into target G: a direction u (outwards from G)
 * with a long free straight run for the ball and room for the ship beside
 * it, preferring the side the ball comes from.
 */
static bool Bb_throw_line(double gx, double gy, double bx, double by,
			  double len, double *ux, double *uy)
{
    double fx = Bb_wdx(bx - gx), fy = Bb_wdy(by - gy), fl = LENGTH(fx, fy);
    double best = -1e9;
    int a;

    if (fl > 1.0) {
	fx /= fl;
	fy /= fl;
    }
    for (a = 0; a < 48; a++) {
	double ang = a * 2.0 * PI / 48.0, cx = cos(ang), cy = sin(ang), r, score;
	double room = 1e9;
	bool ok = true;

	for (r = 30.0; r <= len; r += FineStep) {
	    double qx = gx + cx * r, qy = gy + cy * r;

	    if (Bb_ball_clear(qx, qy) < options.ballRadius + 3.0) {
		ok = false;
		break;
	    }
	    if (r > 120.0)
		room = MIN(room, Bb_ship_clear(qx, qy));
	}
	if (!ok || room < BB_SHIP_R + 30.0)
	    continue;
	score = 200.0 * (cx * fx + cy * fy) + MIN(room, 150.0);
	if (score > best) {
	    best = score;
	    *ux = cx;
	    *uy = cy;
	}
    }
    return best > -1e8;
}

/* Towing a ball home. */
static void Bb_behave_carry(player_t *pl, bb_data_t *d, bb_goal_t *g,
			    ballobject_t *ball)
{
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy);
    double bx = Bb_px(ball->pos.cx), by = Bb_px(ball->pos.cy);
    int src = Bb_treasure_index(ball->ball_treasure);
    int dst = Bb_home_treasure(pl->team, x, y);
    bb_field_t *fd = Bb_static_field(dst, BB_KIND_BALL);
    bb_routes_t *rs;
    treasure_t *home;

    if (fd == NULL)
	return;
    home = Treasure_by_index(dst);
    if (!d->had_ball) {
	if (ball->ball_treasure->team == pl->team) {
	    d->route_src = src;
	    d->route_dst = dst;
	    d->route_var = 0;
	    d->route_chosen = true;
	} else if (!d->route_chosen || d->route_src != src || d->route_dst != dst)
	    Bb_choose_route(pl, d, src, dst, x, y, false);
	d->had_ball = true;
	d->threw = false;
	d->in_line = false;
    } else if (BbTick - d->route_time > 25 && d->route_src >= 0) {
	/* switch when enemies show up on the way and another route is free */
	rs = Routes[d->route_src][dst];
	if (rs && d->route_var < rs->n && !d->via_passed
	    && Bb_route_guarded(pl->team, rs, d->route_var,
				Bb_field_value(fd, x, y), fd))
	    Bb_choose_route(pl, d, d->route_src, dst, x, y, true);
	d->route_time = BbTick;
    }

    g->field = fd;
    g->gx = Bb_px(home->pos.cx);
    g->gy = Bb_px(home->pos.cy);
    rs = (d->route_src >= 0 && d->route_src < BB_MAX_TREASURES)
	? Routes[d->route_src][dst] : NULL;
    if (rs && d->route_var > 0 && d->route_var < rs->n && !d->via_passed) {
	double vx = rs->v[d->route_var].vx, vy = rs->v[d->route_var].vy;

	if (Bb_dist(x, y, vx, vy) < 5 * BLOCK_SZ
	    || Bb_field_value(fd, x, y) < rs->v[d->route_var].via_rest + 2 * BLOCK_SZ)
	    d->via_passed = true;
	else
	    g->field = rs->v[d->route_var].via;
    }
    g->vmax = 25.0 + 4.0 * Bb_level(d) / 100.0;
    /* their ships respawn at their base: get away from it quickly */
    if (Bb_dist(x, y, Bb_px(ball->ball_treasure->pos.cx),
		Bb_px(ball->ball_treasure->pos.cy)) < 1200.0
	&& ball->ball_treasure->team != pl->team)
	g->vmax += 4.0;
    /*
     * Line up for a throw: fly the ball to a gate on a free line into the
     * target, arriving with its velocity along the line.
     */
    if (ball->ball_treasure->team != pl->team) {
	double gd = Bb_dist(bx, by, g->gx, g->gy), ux, uy, R = 300.0;

	if (gd > 1100.0)
	    d->in_line = false;
	else if (Bb_throw_line(g->gx, g->gy, bx, by, R + 60.0, &ux, &uy)) {
	    double rx = Bb_wdx(bx - g->gx), ry = Bb_wdy(by - g->gy);
	    double along = rx * ux + ry * uy, lat = fabs(rx * uy - ry * ux);
	    double vin = -(ball->vel.x * ux + ball->vel.y * uy);

	    if (along > 0.0 && along < R + 80.0 && lat < 50.0 && vin > 0.0)
		d->in_line = true;
	    else if (along < 0.0 || lat > 150.0)
		d->in_line = false;
	    if (!d->in_line) {
		double px = g->gx + ux * R, py = g->gy + uy * R, vg = 12.0;
		double hx = g->gx, hy = g->gy;

		Bb_goal_point(g, px, py, 40.0, g->vmax, BB_KIND_BALL);
		g->gvx = -ux * vg;
		g->gvy = -uy * vg;
		g->match_w = 2.0;
		/* throws still aim at the real target */
		g->thx = hx;
		g->thy = hy;
	    }
	}
    }
    g->enemy_w = 1.0;
    g->score_team = pl->team;
    if (g->thx == 0.0 && g->thy == 0.0) {
	g->thx = g->gx;
	g->thy = g->gy;
    }
    /* self defence: turning while coasting does not disturb the ball */
    g->target = Bb_pick_enemy(pl, x, y, 450.0, true);
    g->aim_w = g->target ? 0.25 : 0.0;

    /* Throw: release when the free ball will fly into our target */
    {
	int t = Bb_throw_ticks(bx, by, ball->vel.x, ball->vel.y, x, y,
			       pl->vel.x, pl->vel.y, pl->team,
			       BB_THROW_TICKS);

	if (t > 0) {
	    BB_LOG("[ballbot] %s throws the ball (%d ticks)\n", pl->name, t);
	    Detach_ball(pl, NULL);
	    d->threw = true;
	    return;
	}
    }
}

/* Choose where to stand for connecting to the ball at (bx, by). */
static bool Bb_pick_spot(player_t *pl, bb_data_t *d, double bx, double by)
{
    double L = options.ballConnectorLength, best = -1e9;
    int a, r;

    d->spot_valid = false;
    for (a = 0; a < 32; a++) {
	double ang = a * 2.0 * PI / 32.0, ux = cos(ang), uy = sin(ang);

	for (r = 0; r < 4; r++) {
	    double rad = L * (0.5 + 0.13 * r), x = bx + ux * rad, y = by + uy * rad;
	    double clear = Bb_ship_clear(x, y), score;

	    if (clear < BB_SHIP_R + 14.0)
		continue;
	    if (Bb_segment_clear(BallClear, bx, by, x, y, 2.0) < 2.0)
		continue;
	    /* room to manoeuvre matters more than the exact direction */
	    score = 80.0 * (ux * d->exit_x + uy * d->exit_y)
		+ MIN(clear, 70.0) * 3.0 - r * 4.0;
	    if (score > best) {
		best = score;
		d->spot_x = x;
		d->spot_y = y;
		d->spot_valid = true;
	    }
	}
    }
    (void)pl;
    d->spot_time = BbTick;
    return d->spot_valid;
}

/* Fetch a ball: own loose ball (return) or an enemy ball. */
static void Bb_behave_fetch(player_t *pl, bb_data_t *d, bb_goal_t *g, int tr)
{
    bb_team_t *tm = &BbTeams[pl->team];
    treasure_t *t = Treasure_by_index(tr);
    ballobject_t *ball = Bb_ball_of(tr);
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy), bx, by, dist;
    double L = options.ballConnectorLength;
    int dst = Bb_home_treasure(pl->team, x, y);
    bool armed = pl->ball != NULL;

    if (tm->ball[tr].known && !tm->ball[tr].home) {
	bx = tm->ball[tr].x;
	by = tm->ball[tr].y;
    } else {
	bx = Bb_px(t->pos.cx);
	by = Bb_px(t->pos.cy);
    }
    /* close enough to see the actual ball */
    if (ball != NULL && Bb_dist(x, y, Bb_px(ball->pos.cx), Bb_px(ball->pos.cy)) < BbVisibility) {
	bx = Bb_px(ball->pos.cx);
	by = Bb_px(ball->pos.cy);
    }
    dist = Bb_dist(x, y, bx, by);

    if (!d->spot_valid || BbTick - d->spot_time > 24 || d->grab_tr != tr) {
	const bb_field_t *via = NULL;

	d->grab_tr = tr;
	if (t->team != pl->team) {
	    /* plan the run before the pickup, start in its direction */
	    if (!d->route_chosen || d->route_src != tr || d->route_dst != dst)
		Bb_choose_route(pl, d, tr, dst, bx, by, false);
	    if (Routes[tr][dst] && d->route_var > 0)
		via = Routes[tr][dst]->v[d->route_var].via;
	}
	Bb_exit_dir(dst, via, bx, by, &d->exit_x, &d->exit_y);
	Bb_pick_spot(pl, d, bx, by);
    }

    g->enemy_w = 0.5;
    g->vmax = 20.0;
    /*
     * The ball run comes first: fight only an enemy right on us on the way
     * in, and not one a teammate is closer to. Close to the ball we grab.
     */
    if (!armed && dist > 0.8 * L) {
	player_t *e = Bb_pick_enemy(pl, x, y, 220.0, true);

	if (e != NULL && dist > 400.0 && Bb_enemy_is_mine(pl, e)) {
	    Bb_goal_engage(pl, g, e, 200.0);
	    return;
	}
    }
    /* the server attaches the ball at L only while the connector is held */
    if (ball != NULL && ball->id == NO_ID && !(t->have && t->team == pl->team)
	&& (armed || (dist < L - 2.0
		      && Bb_segment_clear(BallClear, x, y, Bb_px(ball->pos.cx),
					  Bb_px(ball->pos.cy), 2.0) >= 2.0))) {
	SET_BIT(pl->used, USES_CONNECTOR);
	armed = true;
    }
    /*
     * Fly-by pickup of a ball at rest: come from behind along a line beside
     * the ball in the exit direction, connect in passing and attach with
     * speed.  The pair leaves the base already moving.
     */
    if (ball != NULL && ball->id == NO_ID
	&& LENGTH(ball->vel.x, ball->vel.y) < 1.0) {
	double ex = d->exit_x, ey = d->exit_y, nx, ny;
	double off = 105.0, back = 80.0, vp = 20.0, best = -1e9, side = 1.0;
	double e0x = ex, e0y = ey;
	int k, a;

	/* the pass line closest to the exit direction that is free */
	for (a = 0; a < 32; a++) {
	    double ang = a * 2.0 * PI / 32.0, px = cos(ang), py = sin(ang);
	    double dot = px * e0x + py * e0y;

	    if (dot < 0.3)
		continue;
	    for (k = 0; k < 2; k++) {
		double sg = k ? -1.0 : 1.0;
		double ax = bx - px * back - py * off * sg, ay = by - py * back + px * off * sg;
		double fx = bx + px * (L + 150.0) - py * off * sg;
		double fy = by + py * (L + 150.0) + px * off * sg;

		if (Bb_segment_clear(ShipClear, ax, ay, fx, fy, BB_SHIP_R + 6.0)
		    < BB_SHIP_R + 6.0)
		    continue;
		if (dot > best) {
		    best = dot;
		    ex = px;
		    ey = py;
		    side = sg;
		}
	    }
	}
	nx = -ey;
	ny = ex;
	if (best > -1e8) {
	    double rx = Bb_wdx(x - bx), ry = Bb_wdy(y - by);
	    double along = rx * ex + ry * ey, lat = (rx * nx + ry * ny) * side;
	    double vin = pl->vel.x * ex + pl->vel.y * ey;

	    if (armed || (along < 0.0 && fabs(lat - off) < 50.0 && vin > 0.5 * vp)) {
		/* on the line: pass the ball, the connector is held near it */
		Bb_goal_point(g, bx + ex * (L + 250.0) + nx * off * side,
			      by + ey * (L + 250.0) + ny * off * side, 0.0, vp,
			      BB_KIND_SHIP);
		g->field = NULL;
		g->wall_w = 0.3;
		return;
	    }
	    Bb_goal_point(g, bx - ex * back + nx * off * side,
			  by - ey * back + ny * off * side, 30.0, BB_FREE_VMAX, BB_KIND_SHIP);
	    g->gvx = ex * vp;
	    g->gvy = ey * vp;
	    g->match_w = 1.0;
	    g->wall_w = 0.5;
	    g->target = Bb_pick_enemy(pl, x, y, 600.0, true);
	    g->aim_w = g->target ? 0.3 : 0.0;
	    return;
	}
    }
    if (armed) {
	/* pull out along the exit direction; the ball attaches at L */
	Bb_goal_point(g, bx + d->exit_x * (L + 250.0), by + d->exit_y * (L + 250.0),
		      0.0, 12.0, BB_KIND_SHIP);
	g->field = NULL;
	return;
    }
    if (d->spot_valid) {
	double sd = Bb_dist(x, y, d->spot_x, d->spot_y);

	Bb_goal_point(g, d->spot_x, d->spot_y, 0.0, BB_FREE_VMAX, BB_KIND_SHIP);
	/* arrive moving out along the exit direction: a fly-by pickup */
	if (sd < 250.0) {
	    g->wall_w = 0.2;
	    /* leaving along the exit matters only when we come from behind */
	    g->match_w = (d->spot_x - x) * d->exit_x + (d->spot_y - y) * d->exit_y
			 > 0.0 ? 4.0 : 0.3;
	    g->arrive_r = 8.0;
	    g->gvx = d->exit_x * 5.0;
	    g->gvy = d->exit_y * 5.0;
	    g->vmax = MAX(7.0, sd / 10.0);
	}
    } else
	Bb_goal_point(g, bx, by, L * 0.7, 16.0, BB_KIND_SHIP);
    /* defenders near the ball */
    g->target = Bb_pick_enemy(pl, x, y, 600.0, true);
    g->aim_w = g->target ? 0.3 : 0.0;
}

static void Bb_behave_escort(player_t *pl, bb_data_t *d, bb_goal_t *g);

/* Our living carrier of an enemy ball, NULL if none. */
static player_t *Bb_team_carrier(int team)
{
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *p = Player_by_index(i);
	ballobject_t *b;

	if (p->team != team || !Bb_player_active(p) || !BIT(p->have, HAS_BALL))
	    continue;
	b = Bb_towed_ball(p);
	if (b && b->ball_treasure && b->ball_treasure->team != team)
	    return p;
    }
    return NULL;
}

/* Guard position between a treasure and the main threat. */
static void Bb_behave_guard(player_t *pl, bb_data_t *d, bb_goal_t *g, int tr,
			    double radius, double engage)
{
    bb_team_t *tm = &BbTeams[pl->team];
    treasure_t *t = Treasure_by_index(tr);
    double tx = Bb_px(t->pos.cx), ty = Bb_px(t->pos.cy), ux = 0.0, uy = 0.0;
    double gx, gy;
    player_t *threat = Bb_pick_enemy(pl, tx, ty, engage, false);
    int other;

    (void)d;
    if (threat != NULL) {
	bb_contact_t *c = &tm->c[threat->id];
	double dx = Bb_wdx(c->x - tx), dy = Bb_wdy(c->y - ty), l = LENGTH(dx, dy);

	if (c->seen && l < engage * 0.6) {
	    Bb_goal_engage(pl, g, threat, 220.0);
	    return;
	}
	if (l > 1.0) {
	    ux = dx / l;
	    uy = dy / l;
	}
	g->target = threat;
	g->aim_w = 0.6;
    }
    if (ux == 0.0 && uy == 0.0) {
	/* towards the enemy base along the ball run path */
	other = Bb_enemy_treasure(pl->team, tx, ty);
	if (other >= 0)
	    Bb_exit_dir(other, NULL, tx, ty, &ux, &uy);
	else
	    ux = 1.0;
    }
    gx = tx + ux * radius;
    gy = ty + uy * radius;
    Bb_open_point(&gx, &gy, BB_SHIP_R + 30.0);
    Bb_goal_point(g, gx, gy, 40.0, 16.0, BB_KIND_SHIP);
}

/*
 * Ram a ball: our own ball, loose or towed by the enemy, goes home when any
 * ship touches it (treasureCollisionDestroys).
 */
static bool Bb_behave_ram(player_t *pl, bb_data_t *d, bb_goal_t *g, int tr)
{
    bb_ballinfo_t *bi = &BbTeams[pl->team].ball[tr];
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy), dist, lead;

    (void)d;
    if (!bi->known)
	return false;
    dist = Bb_dist(x, y, bi->x, bi->y);
    lead = MIN(dist / 22.0, 20.0);
    Bb_goal_point(g, bi->x + bi->vx * lead, bi->y + bi->vy * lead, 0.0, 24.0,
		  BB_KIND_SHIP);
    if (dist < 400.0)
	g->field = NULL;
    g->hit_tr = tr;
    g->enemy_w = 0.0;
    return true;
}

/* Chase the thief of our ball, or wait for him at his home. */
static void Bb_behave_hunt(player_t *pl, bb_data_t *d, bb_goal_t *g)
{
    bb_team_t *tm = &BbTeams[pl->team];
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy);
    player_t *thief = tm->thief_id != NO_ID ? Player_by_id(tm->thief_id) : NULL;

    if (thief != NULL && tm->c[thief->id].known) {
	bb_contact_t *c = &tm->c[thief->id];
	double dist = Bb_dist(x, y, c->x, c->y);
	int their = Bb_home_treasure(thief->team, c->x, c->y);

	if (dist < 900.0 || their < 0) {
	    /* ram the towed ball when we see it, shoot the thief anyway */
	    if (tm->thief_tr >= 0 && tm->ball[tm->thief_tr].seen
		&& Bb_behave_ram(pl, d, g, tm->thief_tr)) {
		g->target = thief;
		g->aim_w = 0.3;
		return;
	    }
	    Bb_goal_engage(pl, g, thief, 120.0);
	    g->vmax = 22.0;
	    return;
	}
	/* intercept on his way home */
	{
	    static float px[BB_ROUTE_PTS], py[BB_ROUTE_PTS];
	    bb_field_t *fd = Bb_static_field(their, BB_KIND_BALL);
	    double tlen = 0.0, tv = MAX(LENGTH(c->vx, c->vy), 8.0);
	    int n, i;

	    if (fd != NULL) {
		n = Bb_field_trace(fd, c->x, c->y, px, py, BB_ROUTE_PTS);
		for (i = 1; i < n; i++) {
		    tlen += Bb_dist(px[i - 1], py[i - 1], px[i], py[i]);
		    if (Bb_dist(x, y, px[i], py[i]) / 18.0 <= tlen / tv)
			break;
		}
		if (n > 0) {
		    i = MIN(i, n - 1);
		    Bb_goal_point(g, px[i], py[i], 0.0, 24.0, BB_KIND_SHIP);
		    g->target = thief;
		    g->aim_w = 0.5;
		    return;
		}
	    }
	}
    }
    /* thief unknown: guard his target, he has to come there */
    {
	int their = -1, i;

	for (i = 0; i < Num_treasures() && i < BB_MAX_TREASURES; i++) {
	    treasure_t *t = Treasure_by_index(i);

	    if (t->team != pl->team && !t->empty) {
		their = i;
		break;
	    }
	}
	if (thief != NULL)
	    their = Bb_home_treasure(thief->team, x, y);
	if (their >= 0)
	    Bb_behave_guard(pl, d, g, their, 260.0, 1200.0);
    }
}

static void Bb_behave_escort(player_t *pl, bb_data_t *d, bb_goal_t *g)
{
    player_t *c = Player_by_id(d->role_arg);
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy), cx, cy, ax, ay;
    double ux = 0.0, uy = 0.0, l;
    player_t *e;
    int src;

    if (c == NULL || !Bb_player_active(c)) {
	Bb_behave_guard(pl, d, g, Bb_home_treasure(pl->team, x, y), 200.0, 900.0);
	return;
    }
    cx = Bb_px(c->pos.cx);
    cy = Bb_px(c->pos.cy);
    /* enemies near the carrier first */
    e = Bb_pick_enemy(pl, cx, cy, 700.0, true);
    if (e != NULL) {
	Bb_goal_engage(pl, g, e, 200.0);
	g->vmax = 22.0;
	return;
    }
    /*
     * Cover the carrier's back: pursuers come from the enemy base, so stay
     * between him and the nearest known enemy (or the enemy base).
     */
    e = Bb_pick_enemy(pl, cx, cy, 2000.0, false);
    if (e != NULL) {
	bb_contact_t *ct = &BbTeams[pl->team].c[e->id];

	ux = Bb_wdx(ct->x - cx);
	uy = Bb_wdy(ct->y - cy);
    } else if ((src = Bb_enemy_treasure(pl->team, cx, cy)) >= 0) {
	treasure_t *t = Treasure_by_index(src);

	ux = Bb_wdx(Bb_px(t->pos.cx) - cx);
	uy = Bb_wdy(Bb_px(t->pos.cy) - cy);
    }
    l = LENGTH(ux, uy);
    if (l > 1.0) {
	ux /= l;
	uy /= l;
    }
    ax = cx + ux * 180.0 + (d->escort_slot % 2 ? -uy : uy) * 60.0;
    ay = cy + uy * 180.0 + (d->escort_slot % 2 ? ux : -ux) * 60.0;
    Bb_open_point(&ax, &ay, BB_SHIP_R + 20.0);
    Bb_goal_point(g, ax, ay, 50.0, 22.0, BB_KIND_SHIP);
    g->gvx = c->vel.x;
    g->gvy = c->vel.y;
}

static void Bb_behave_mid(player_t *pl, bb_data_t *d, bb_goal_t *g)
{
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy);
    int home = Bb_home_treasure(pl->team, x, y);
    int enemy = d->role_arg;
    bb_field_t *fe = Bb_static_field(enemy, BB_KIND_SHIP);
    player_t *e;

    if (home < 0 || fe == NULL) {
	Bb_behave_guard(pl, d, g, home, 220.0, 900.0);
	return;
    }
    {
	static float px[BB_ROUTE_PTS], py[BB_ROUTE_PTS];
	treasure_t *h = Treasure_by_index(home);
	int n = Bb_field_trace(fe, Bb_px(h->pos.cx), Bb_px(h->pos.cy), px, py,
			       BB_ROUTE_PTS);
	double mx, my;

	if (n <= 0) {
	    Bb_behave_guard(pl, d, g, home, 220.0, 900.0);
	    return;
	}
	mx = px[n * 2 / 5];
	my = py[n * 2 / 5];
	e = Bb_pick_enemy(pl, mx, my, 700.0, true);
	if (e != NULL) {
	    Bb_goal_engage(pl, g, e, 230.0);
	    return;
	}
	Bb_goal_point(g, mx, my, 80.0, 18.0, BB_KIND_SHIP);
    }
}

static void Bb_behave_support(player_t *pl, bb_data_t *d, bb_goal_t *g)
{
    int tr = d->role_arg;
    treasure_t *t;
    double tx, ty, x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy), ux, uy, gx, gy;
    player_t *e;

    if (tr < 0 || tr >= Num_treasures()) {
	Bb_behave_guard(pl, d, g, Bb_home_treasure(pl->team, x, y), 220.0, 900.0);
	return;
    }
    t = Treasure_by_index(tr);
    tx = Bb_px(t->pos.cx);
    ty = Bb_px(t->pos.cy);
    e = Bb_pick_enemy(pl, tx, ty, 800.0, true);
    if (e != NULL) {
	Bb_goal_engage(pl, g, e, 230.0);
	return;
    }
    Bb_exit_dir(Bb_home_treasure(pl->team, x, y), NULL, tx, ty, &ux, &uy);
    /* off to the side of the exit, covering the grabber */
    gx = tx + (ux * 0.7 - uy * 0.7) * 280.0;
    gy = ty + (uy * 0.7 + ux * 0.7) * 280.0;
    Bb_open_point(&gx, &gy, BB_SHIP_R + 30.0);
    Bb_goal_point(g, gx, gy, 60.0, 18.0, BB_KIND_SHIP);
}

static void Bb_behave_fight(player_t *pl, bb_data_t *d, bb_goal_t *g)
{
    double x = Bb_px(pl->pos.cx), y = Bb_px(pl->pos.cy);
    player_t *e = Bb_pick_enemy(pl, x, y, 1e9, false);

    (void)d;
    if (e != NULL)
	Bb_goal_engage(pl, g, e, 240.0);
    else
	Bb_goal_point(g, x, y, 100.0, 10.0, BB_KIND_SHIP);
}

/* ------------------------------------------------------------------------
 * Robot type interface
 * ------------------------------------------------------------------------ */

static void Robot_ball_round_tick(void)
{
    int i, t;
    bool active[MAX_TEAMS];

    NumBallbots = 0;
    memset(active, 0, sizeof(active));
    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (Bb_is_ballbot(pl)) {
	    NumBallbots++;
	    if (pl->team < MAX_TEAMS)
		active[pl->team] = true;
	}
    }
    if (NumBallbots == 0)
	return;

    BbTick++;
    DynBudget = BB_DYN_BUDGET;
    BbVisibility = 256.0 + ((VISIBILITY_DISTANCE - 256.0) * (NUM_IDS - NumRobots)) / NUM_IDS;
    if (!Bb_map_update())
	return;

    for (t = 0; t < MAX_TEAMS; t++) {
	BbTeams[t].active = active[t];
	if (!active[t])
	    continue;
	Bb_update_team(t);
	if (BbTick % BB_ROLE_TICKS == (unsigned long)t % BB_ROLE_TICKS)
	    Bb_assign_roles(t);
    }
}

static void Robot_ball_create(player_t *pl, char *str)
{
    bb_data_t *d;

    if (!(d = XMALLOC(bb_data_t, 1))) {
	error("no mem for ball robot");
	End_game();
    }
    memset(d, 0, sizeof(*d));
    d->role = BB_ROLE_NONE;
    d->grab_tr = -1;
    d->route_src = d->route_dst = -1;
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

static void Robot_ball_go_home(player_t *pl)
{
    bb_data_t *d = Bb_data(pl);

    if (d->alive_ticks > 0)
	BB_LOG("[ballbot] %s died: role %s ball %d life %lu\n", pl->name,
	       bb_role_names[d->role], d->had_ball, d->alive_ticks);
    d->alive_ticks = 0;
    d->plan_len = 0;
    d->spot_valid = false;
    d->had_ball = false;
    d->route_chosen = false;
}

static void Robot_ball_play(player_t *pl)
{
    bb_data_t *d = Bb_data(pl);
    bb_goal_t g;
    ballobject_t *ball;
    int level = Bb_level(d);

    CLR_BIT(pl->used, USES_SHOT | USES_SHIELD | USES_LASER | USES_CONNECTOR);
    if (pl->fuel.sum <= (BIT(world->rules->mode, TIMING) ? 0.0 : 5.0))
	Player_self_destruct(pl, true);
    else
	Player_self_destruct(pl, false);
    if (pl->damaged > 0 || !BbMapOk) {
	Thrust(pl, false);
	return;
    }

    memset(&g, 0, sizeof(g));
    g.score_team = -1;
    g.hit_tr = -1;
    g.vmax = 20.0;
    g.enemy_w = 0.3;
    g.wall_w = 1.0;
    g.match_w = 4.0;
    g.gx = Bb_px(pl->pos.cx);
    g.gy = Bb_px(pl->pos.cy);

    d->alive_ticks++;
    ball = Bb_towed_ball(pl);
    if (ball == NULL && d->had_ball) {
	if (!d->threw)
	    BB_LOG("[ballbot] %s lost the ball\n", pl->name);
	d->had_ball = false;
	d->route_chosen = false;
    }
    if (ball != NULL)
	Bb_behave_carry(pl, d, &g, ball);
    else {
	switch (d->role) {
	case BB_ROLE_GRAB:
	    if (d->role_arg >= 0 && d->role_arg < Num_treasures())
		Bb_behave_fetch(pl, d, &g, d->role_arg);
	    break;
	case BB_ROLE_RETURN:
	    if (d->role_arg >= 0 && d->role_arg < Num_treasures())
		Bb_behave_ram(pl, d, &g, d->role_arg);
	    break;
	case BB_ROLE_HUNT:
	    Bb_behave_hunt(pl, d, &g);
	    break;
	case BB_ROLE_ESCORT:
	    Bb_behave_escort(pl, d, &g);
	    break;
	case BB_ROLE_SUPPORT:
	    Bb_behave_support(pl, d, &g);
	    break;
	case BB_ROLE_MID:
	    Bb_behave_mid(pl, d, &g);
	    break;
	case BB_ROLE_DEFEND:
	    {
		/* nobody near our base but our carrier is under way: help him */
		player_t *c = Bb_team_carrier(pl->team);
		treasure_t *t = Treasure_by_index(d->role_arg);

		if (c != NULL && c != pl
		    && Bb_pick_enemy(pl, Bb_px(t->pos.cx), Bb_px(t->pos.cy),
				     1100.0, false) == NULL) {
		    int arg = d->role_arg;

		    d->role_arg = c->id;
		    Bb_behave_escort(pl, d, &g);
		    d->role_arg = arg;
		} else
		    Bb_behave_guard(pl, d, &g, d->role_arg, 200.0, 1000.0);
	    }
	    break;
	case BB_ROLE_FIGHT:
	default:
	    Bb_behave_fight(pl, d, &g);
	    break;
	}
    }
    /* opportunistic aiming at the nearest enemy in view */
    if (g.target == NULL) {
	g.target = Bb_pick_enemy(pl, Bb_px(pl->pos.cx), Bb_px(pl->pos.cy), 600.0, true);
	if (g.target != NULL)
	    g.aim_w = ball != NULL ? 0.3 : 0.6;
    }
    /* a thrown ball: just keep flying sensibly this tick */
    if (ball != NULL && Bb_towed_ball(pl) == NULL)
	g.score_team = -1;

    Bb_fly(pl, d, &g, level);
    Bb_fire(pl, d, level);
}

static void Robot_ball_set_war(player_t *pl, int victim_id)
{
    (void)pl;
    (void)victim_id;
}

static int Robot_ball_war_on_player(player_t *pl)
{
    (void)pl;
    return NO_ID;
}

/* /msg <bot> status: role and level; /msg <bot> level N */
static void Robot_ball_message(player_t *pl, const char *message)
{
    bb_data_t *d = Bb_data(pl);
    int lvl;

    if (message == NULL)
	return;
    if (strstr(message, "status") != NULL)
	Set_message_f("Ballbot level %d, role %s. [%s]", Bb_level(d),
		      bb_role_names[d->role], pl->name);
    else if (strstr(message, "level ") != NULL
	     && sscanf(strstr(message, "level ") + 6, "%d", &lvl) == 1) {
	d->level = lvl <= 0 ? 0 : MIN(lvl, 100);
	Set_message_f("Skill level set to %d. [%s]", Bb_level(d), pl->name);
    }
}

static void Robot_ball_destroy(player_t *pl)
{
    XFREE(pl->robot_data_ptr->private_data);
}

static void Robot_ball_invite(player_t *pl, player_t *inviter)
{
    if (!Player_is_active(inviter)) {
	Refuse_alliance(pl, inviter);
	return;
    }
    Accept_alliance(pl, inviter);
}

static void Robot_ball_game_event(player_t *pl, int event, player_t *other)
{
    if (event == ROBOT_BALL_CASHED || event == ROBOT_BALL_SAVED)
	Bb_data(pl)->threw = true;	/* not a lost ball */
    if (event == ROBOT_BALL_CASHED)
	BB_LOG("[ballbot] EVENT cashed %s team %d\n", pl->name, pl->team);
    else if (event == ROBOT_BALL_LOST)
	BB_LOG("[ballbot] EVENT lost team %d tick %lu\n", pl->team, BbTick);
    else if (event == ROBOT_BALL_SAVED)
	BB_LOG("[ballbot] EVENT saved %s team %d\n", pl->name, pl->team);
    else if (event == ROBOT_KILLED_PLAYER && other)
	BB_LOG("[ballbot] EVENT kill %s team %d -> %s\n", pl->name, pl->team,
	       other->name);
}

static robot_type_t robot_ball_type = {
    BB_TYPE_NAME,
    Robot_ball_round_tick,
    Robot_ball_create,
    Robot_ball_go_home,
    Robot_ball_play,
    Robot_ball_set_war,
    Robot_ball_war_on_player,
    Robot_ball_message,
    Robot_ball_destroy,
    Robot_ball_invite,
    Robot_ball_game_event
};

int Robot_ball_setup(robot_type_t *type_ptr)
{
    *type_ptr = robot_ball_type;
    return 0;
}
