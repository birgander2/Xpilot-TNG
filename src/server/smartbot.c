/*
 * XPilot NG, a multiplayer space war game.
 *
 * Smart & Learning Robot Driver ("smart")
 *
 * Features:
 * - Adjustable intelligence/skill level via options.smartRobotLevel (1-100)
 *   or per-robot /msg commands.
 * - Exact quadratic ballistic lead prediction (with velocity, wrap).
 * - Multi-object CPA (Closest Point of Approach) threat scanner & potential
 *   field obstacle avoidance.
 * - Fast block-grid A* pathfinding for complex maps, fuel runs, and CTF.
 * - Advanced Capture-the-Flag (CTF) ball catapulting, carrier interception,
 *   and teammate escorting.
 * - Hybrid Online Reinforcement Learning (Tabular Q-Learning + Evolutionary
 *   continuous parameter adaptation) with persistent storage in
 *   "smartbot_brain.dat".
 */

#include "xpserver.h"
#include <sys/stat.h>

#define WITHIN(NOW,THEN,DIFF) (NOW<=THEN && (THEN-NOW)<DIFF)

#define SMART_BRAIN_FILE	"smartbot_brain.dat"
#define SMART_BRAIN_SUBDIR	"xpilot-tng"
#define SMART_BRAIN_MAGIC	0x58505342U /* "XPSB" */
#define SMART_BRAIN_VERSION	1U
#define SMART_BRAIN_SAVE_SECS	300	/* periodic save interval */

/*
 * High-level tactical actions selected by the RL Q-learning controller
 * and utility evaluator.
 */
enum SmartTactic {
    TACTIC_DOGFIGHT_AGGR = 0,	/* Close-range aggressive pursuit & rapid fire */
    TACTIC_SNIPE_KITE    = 1,	/* Keep medium-long distance, precision lead shots */
    TACTIC_EVADE_SURVIVE = 2,	/* Prioritize survival, shields, mines, & escape */
    TACTIC_HARVEST_REFUEL= 3,	/* Collect items or refuel at station */
    TACTIC_CTF_OBJECTIVE = 4,	/* Attack enemy treasure, return ball, or intercept */
    NUM_SMART_TACTICS    = 5
};

/*
 * Discretized RL State Space:
 * - Fuel status:     3 bins (0: critical <25%, 1: normal 25-65%, 2: high >65%)
 * - Enemy distance:  4 bins (0: close <6 blk, 1: med <16 blk, 2: far, 3: none)
 * - Immediate threat:2 bins (0: safe, 1: incoming projectile/wall hazard)
 * - CTF context:     3 bins (0: no ball, 1: carrying ball, 2: enemy has our ball)
 * Total states = 3 * 4 * 2 * 3 = 72 states.
 */
#define RL_FUEL_BINS	3
#define RL_DIST_BINS	4
#define RL_THREAT_BINS	2
#define RL_CTF_BINS	3
#define NUM_RL_STATES	(RL_FUEL_BINS * RL_DIST_BINS * RL_THREAT_BINS * RL_CTF_BINS)

/*
 * Continuous evolutionary policy genes (adapted online via reward feedback).
 */
typedef struct smart_genes {
    double	preferred_combat_dist;	/* Ideal combat distance in blocks (default ~8.5) */
    double	evade_weight;		/* Weight of projectile repulsion (default ~1.4) */
    double	wall_weight;		/* Weight of wall repulsion (default ~1.5) */
    double	aggression_bias;	/* Bias towards attacking vs harvesting (default ~1.0) */
    double	speed_factor;		/* Target flight speed multiplier (default ~1.1) */
} smart_genes_t;

/*
 * Shared persistent RL brain across all smartbots on the server.
 */
typedef struct smart_shared_brain {
    uint32_t		magic;
    uint32_t		version;
    uint64_t		total_episodes;
    uint64_t		total_kills;
    uint64_t		total_deaths;
    double		best_episode_reward;
    double		q_table[NUM_RL_STATES][NUM_SMART_TACTICS];
    smart_genes_t	best_genes;
} smart_shared_brain_t;

static smart_shared_brain_t	SharedBrain;
static bool			BrainInitialized = false;
static bool			BrainDirty = false;
static int			RoundCounter = 0;

/*
 * A* Pathfinding constants (bounded local/global grid search).
 */
#define ASTAR_MAX_NODES		1024
#define ASTAR_MAX_WAYPOINTS	32
#define ASTAR_RECALC_TICKS	18

/*
 * Per-instance private data for each smartbot.
 */
typedef struct robot_smart_data {
    int			robot_lock;
    int			robot_lock_id;
    int			robot_count;
    int			attack;
    int			defense;
    int			custom_level;		/* 0 = use options.smartRobotLevel, else 1..100 */
    bool		learning_enabled;

    double		fuel_l1;
    double		fuel_l2;
    double		fuel_l3;

    int			last_dropped_mine;
    int			last_fired_missile;
    int			last_thrown_ball;

    /* RL & Evolutionary state */
    int			current_state;
    int			current_tactic;
    int			tactic_timer;
    double		accumulated_reward;
    double		episode_reward;
    double		last_score;
    int			last_kills;
    int			last_deaths;
    smart_genes_t	genes;

    /* A* Pathfinding cache & active roaming state */
    clpos_t		waypoints[ASTAR_MAX_WAYPOINTS];
    int			num_waypoints;
    int			cur_waypoint;
    blkpos_t		last_goal_blk;
    int			last_astar_tick;
    clpos_t		patrol_pos;
    int			patrol_timer;

    int			turn_budget;		/* Remaining turn (in dir units) this tick */

    int			threat_timer;		/* Keeps the RL threat bit set for a while */
    int			episode_deaths;		/* Deaths already counted as episodes */
    bool		genes_sampled;
    bool		cut_thrust;		/* Heat seeker nearby: coast this tick */
} robot_smart_data_t;

static double	Visibility_distance;

/*
 * Prototypes for smartbot methods.
 */
static void Robot_smart_round_tick(void);
static void Robot_smart_create(player_t *pl, char *str);
static void Robot_smart_go_home(player_t *pl);
static void Robot_smart_play(player_t *pl);
static void Robot_smart_set_war(player_t *pl, int victim_id);
static int  Robot_smart_war_on_player(player_t *pl);
static void Robot_smart_message(player_t *pl, const char *str);
static void Robot_smart_destroy(player_t *pl);
static void Robot_smart_invite(player_t *pl, player_t *inviter);
int         Robot_smart_setup(robot_type_t *type_ptr);

static robot_type_t robot_smart_type = {
    "smart",
    Robot_smart_round_tick,
    Robot_smart_create,
    Robot_smart_go_home,
    Robot_smart_play,
    Robot_smart_set_war,
    Robot_smart_war_on_player,
    Robot_smart_message,
    Robot_smart_destroy,
    Robot_smart_invite
};

static inline robot_smart_data_t *Robot_smart_get_data(player_t *pl)
{
    return (robot_smart_data_t *)pl->robot_data_ptr->private_data;
}

static inline int Smart_get_effective_level(robot_smart_data_t *my_data)
{
    int lvl = (my_data->custom_level > 0)
	      ? my_data->custom_level
	      : options.smartRobotLevel;
    LIMIT(lvl, 1, 100);
    return lvl;
}

/* =========================================================================
 * Persistent RL Brain Management
 * ========================================================================= */

/*
 * Brain file location: $XDG_DATA_HOME/xpilot-tng/ (default
 * ~/.local/share/xpilot-tng/).  Returns NULL if no home can be determined.
 * If mkdirs is true, the directory is created as needed.
 */
static const char *Smart_brain_path(bool mkdirs)
{
    static char path[PATH_MAX];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char *p;

    if (xdg && xdg[0] == '/')
	snprintf(path, sizeof(path), "%s/" SMART_BRAIN_SUBDIR "/" SMART_BRAIN_FILE, xdg);
    else if (home && home[0])
	snprintf(path, sizeof(path), "%s/.local/share/" SMART_BRAIN_SUBDIR "/" SMART_BRAIN_FILE, home);
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

static void Smart_brain_save(void)
{
    FILE *fp;
    const char *path;
    char tmp[PATH_MAX + 8];
    bool ok;

    if (!BrainInitialized)
	return;

    path = Smart_brain_path(true);
    if (!path)
	path = SMART_BRAIN_FILE;

    /* Write to a temporary file and rename, so a crash never corrupts the brain */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "wb");
    if (!fp)
	return;
    ok = (fwrite(&SharedBrain, sizeof(SharedBrain), 1, fp) == 1);
    if (fclose(fp) != 0)
	ok = false;
    if (ok && rename(tmp, path) == 0)
	BrainDirty = false;
    else
	remove(tmp);
}

static void Smart_brain_init(void)
{
    int s;
    FILE *fp;

    if (BrainInitialized)
	return;

    memset(&SharedBrain, 0, sizeof(SharedBrain));
    SharedBrain.magic = SMART_BRAIN_MAGIC;
    SharedBrain.version = SMART_BRAIN_VERSION;
    SharedBrain.best_episode_reward = -1e9;

    SharedBrain.best_genes.preferred_combat_dist = 8.5;
    SharedBrain.best_genes.evade_weight = 1.45;
    SharedBrain.best_genes.wall_weight = 1.55;
    SharedBrain.best_genes.aggression_bias = 1.05;
    SharedBrain.best_genes.speed_factor = 1.15;

    /* Initialize Q-table with sensible domain priors so bots play smart immediately */
    for (s = 0; s < NUM_RL_STATES; s++) {
	int ctf_bin    = s % RL_CTF_BINS;
	int threat_bin = (s / RL_CTF_BINS) % RL_THREAT_BINS;
	int dist_bin   = (s / (RL_CTF_BINS * RL_THREAT_BINS)) % RL_DIST_BINS;
	int fuel_bin   = s / (RL_CTF_BINS * RL_THREAT_BINS * RL_DIST_BINS);

	SharedBrain.q_table[s][TACTIC_DOGFIGHT_AGGR]  = (dist_bin <= 1 && fuel_bin >= 1) ? 4.0 : 1.0;
	SharedBrain.q_table[s][TACTIC_SNIPE_KITE]     = (dist_bin == 1 || dist_bin == 2) ? 4.5 : 1.5;
	SharedBrain.q_table[s][TACTIC_EVADE_SURVIVE]  = (threat_bin == 1 || fuel_bin == 0) ? 6.0 : 0.5;
	SharedBrain.q_table[s][TACTIC_HARVEST_REFUEL] = (fuel_bin == 0 || dist_bin == 3) ? 5.0 : 2.0;
	SharedBrain.q_table[s][TACTIC_CTF_OBJECTIVE]  = (ctf_bin > 0) ? 7.5 : 1.5;
    }

    {
	const char *path = Smart_brain_path(false);
	fp = path ? fopen(path, "rb") : NULL;
	if (!fp)	/* legacy location: current working directory */
	    fp = fopen(SMART_BRAIN_FILE, "rb");
    }
    if (fp) {
	smart_shared_brain_t loaded;
	if (fread(&loaded, sizeof(loaded), 1, fp) == 1
	    && loaded.magic == SMART_BRAIN_MAGIC
	    && loaded.version == SMART_BRAIN_VERSION) {
	    SharedBrain = loaded;
	    /* Old files blended the -1e9 sentinel into the record; reset it */
	    if (SharedBrain.best_episode_reward < -1e6)
		SharedBrain.best_episode_reward = -1e9;
	    if (options.logRobots)
		xpprintf("%s Smartbot brain loaded (%llu episodes, %llu kills).\n",
			 showtime(),
			 (unsigned long long)SharedBrain.total_episodes,
			 (unsigned long long)SharedBrain.total_kills);
	}
	fclose(fp);
    }

    BrainInitialized = true;
}

static int Smart_encode_rl_state(player_t *pl, double enemy_dist, bool high_threat)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int fuel_bin, dist_bin, threat_bin, ctf_bin;
    int s;

    if (pl->fuel.sum < my_data->fuel_l2)
	fuel_bin = 0;
    else if (pl->fuel.sum < my_data->fuel_l3 * 2.0)
	fuel_bin = 1;
    else
	fuel_bin = 2;

    if (enemy_dist < 6.0 * BLOCK_SZ)
	dist_bin = 0;
    else if (enemy_dist < 16.0 * BLOCK_SZ)
	dist_bin = 1;
    else if (enemy_dist < Visibility_distance)
	dist_bin = 2;
    else
	dist_bin = 3;

    threat_bin = high_threat ? 1 : 0;

    ctf_bin = 0;
    if (BIT(world->rules->mode, TEAM_PLAY) && Num_treasures() > 0) {
	if (BIT(pl->have, HAS_BALL) || pl->ball != NULL) {
	    ctf_bin = 1;
	} else {
	    int i;
	    for (i = 0; i < Num_treasures(); i++) {
		treasure_t *tr = Treasure_by_index(i);
		if (tr->team == pl->team && !tr->have) {
		    ctf_bin = 2;
		    break;
		}
	    }
	}
    }

    s = ((fuel_bin * RL_DIST_BINS + dist_bin) * RL_THREAT_BINS + threat_bin)
	* RL_CTF_BINS + ctf_bin;
    LIMIT(s, 0, NUM_RL_STATES - 1);
    return s;
}

static void Smart_rl_step(player_t *pl, int next_state)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    double score_delta, reward = 0.0;
    int kills_delta, deaths_delta, a, best_a;
    double max_next_q, td_error;
    const double alpha = 0.12;
    const double gamma = 0.88;
    int eff_level = Smart_get_effective_level(my_data);
    double eps;

    score_delta  = Get_Score(pl) - my_data->last_score;
    kills_delta  = pl->kills - my_data->last_kills;
    deaths_delta = pl->deaths - my_data->last_deaths;

    my_data->last_score  = Get_Score(pl);
    my_data->last_kills  = pl->kills;
    my_data->last_deaths = pl->deaths;

    reward += score_delta * 0.25;
    if (kills_delta > 0) {
	reward += 12.0 * kills_delta;
	SharedBrain.total_kills += kills_delta;
    }
    if (deaths_delta > 0) {
	reward -= 10.0 * deaths_delta;
	SharedBrain.total_deaths += deaths_delta;
    }
    /* Small survival bonus per decision step (about once per second) */
    reward += 0.05;

    my_data->accumulated_reward += reward;
    my_data->episode_reward += reward;

    if (my_data->learning_enabled && my_data->current_state >= 0) {
	max_next_q = SharedBrain.q_table[next_state][0];
	for (a = 1; a < NUM_SMART_TACTICS; a++) {
	    if (SharedBrain.q_table[next_state][a] > max_next_q)
		max_next_q = SharedBrain.q_table[next_state][a];
	}
	td_error = my_data->accumulated_reward
		   + gamma * max_next_q
		   - SharedBrain.q_table[my_data->current_state][my_data->current_tactic];
	SharedBrain.q_table[my_data->current_state][my_data->current_tactic] += alpha * td_error;
    }
    my_data->accumulated_reward = 0.0;
    BrainDirty = true;

    /* Select next tactic using epsilon-greedy scaled by smartRobotLevel */
    eps = my_data->learning_enabled ? 0.02 + (100 - eff_level) * 0.0015 : 0.0;
    best_a = 0;
    for (a = 1; a < NUM_SMART_TACTICS; a++) {
	if (SharedBrain.q_table[next_state][a] > SharedBrain.q_table[next_state][best_a])
	    best_a = a;
    }

    if (rfrac() < eps)
	my_data->current_tactic = (int)(rfrac() * NUM_SMART_TACTICS) % NUM_SMART_TACTICS;
    else
	my_data->current_tactic = best_a;

    my_data->current_state = next_state;
    my_data->tactic_timer = FPS;	/* re-decide about once per second */
}

static void Smart_evolve_genes_on_respawn(player_t *pl)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    bool died = (pl->deaths > my_data->episode_deaths);

    /* An episode ends with a death; round starts and other respawns do not count */
    if (died) {
	my_data->episode_deaths = pl->deaths;
	SharedBrain.total_episodes++;
	BrainDirty = true;

	if (my_data->learning_enabled && SharedBrain.total_episodes > 1) {
	    if (my_data->episode_reward > SharedBrain.best_episode_reward) {
		/* Soft update of best genes towards successful episode genes */
		SharedBrain.best_episode_reward = my_data->episode_reward;
		SharedBrain.best_genes.preferred_combat_dist =
		    0.7 * SharedBrain.best_genes.preferred_combat_dist + 0.3 * my_data->genes.preferred_combat_dist;
		SharedBrain.best_genes.evade_weight =
		    0.7 * SharedBrain.best_genes.evade_weight + 0.3 * my_data->genes.evade_weight;
		SharedBrain.best_genes.wall_weight =
		    0.7 * SharedBrain.best_genes.wall_weight + 0.3 * my_data->genes.wall_weight;
		SharedBrain.best_genes.aggression_bias =
		    0.7 * SharedBrain.best_genes.aggression_bias + 0.3 * my_data->genes.aggression_bias;
		SharedBrain.best_genes.speed_factor =
		    0.7 * SharedBrain.best_genes.speed_factor + 0.3 * my_data->genes.speed_factor;
	    } else {
		/* Let the record fade so a single lucky episode cannot lock the genes forever */
		SharedBrain.best_episode_reward -= 0.02 * ABS(SharedBrain.best_episode_reward);
	    }
	}
    } else if (my_data->genes_sampled) {
	return;		/* same episode continues, keep genes and reward */
    }
    my_data->genes_sampled = true;

    /* Sample slightly mutated genes around SharedBrain.best_genes */
    my_data->genes = SharedBrain.best_genes;
    if (my_data->learning_enabled) {
	my_data->genes.preferred_combat_dist += (rfrac() - 0.5) * 1.5;
	my_data->genes.evade_weight          += (rfrac() - 0.5) * 0.25;
	my_data->genes.wall_weight           += (rfrac() - 0.5) * 0.25;
	my_data->genes.aggression_bias       += (rfrac() - 0.5) * 0.20;
	my_data->genes.speed_factor          += (rfrac() - 0.5) * 0.15;

	LIMIT(my_data->genes.preferred_combat_dist, 4.0, 16.0);
	LIMIT(my_data->genes.evade_weight, 0.6, 3.0);
	LIMIT(my_data->genes.wall_weight, 0.8, 3.0);
	LIMIT(my_data->genes.aggression_bias, 0.4, 2.2);
	LIMIT(my_data->genes.speed_factor, 0.6, 2.0);
    }

    my_data->episode_reward = 0.0;
}

/* =========================================================================
 * Map Geometry, Raycasting & A* Pathfinding
 * ========================================================================= */

static bool Smart_empty_space_for_ball(int bx, int by)
{
    clpos_t pos;
    pos.cx = BLOCK_CENTER(bx);
    pos.cy = BLOCK_CENTER(by);
    return (shape_is_inside(pos.cx, pos.cy, BALL_BIT, NULL, &filled_wire, 0) == NO_GROUP);
}

/*
 * Per-block passability cache.  The block test costs four is_inside() calls and
 * is hit hundreds of times per bot and tick (rays, line of sight, A*).  Results
 * that do not depend on the asking player are kept for one second, so changing
 * targets still show up.  Blocks touching wormholes or targets are never cached.
 */
#define PASS_FREE	1U
#define PASS_SOLID	2U

static uint32_t	*PassCache;
static int	PassCacheW, PassCacheH;

static bool Smart_block_passable(player_t *pl, int bx, int by)
{
    int group, cx, cy, i, j;
    int delta = BLOCK_CLICKS / 4;
    hitmask_t hitmask = NONBALL_BIT;
    uint32_t epoch, *slot;
    bool special = false;

    if (BIT(world->rules->mode, WRAP_PLAY)) {
	bx = WRAP_XBLOCK(bx);
	by = WRAP_YBLOCK(by);
    }
    if (bx < 0 || bx >= world->x || by < 0 || by >= world->y)
	return false;

    if (PassCache == NULL || PassCacheW != world->x || PassCacheH != world->y) {
	free(PassCache);
	PassCache = calloc((size_t)world->x * world->y, sizeof(*PassCache));
	PassCacheW = world->x;
	PassCacheH = world->y;
    }
    slot = PassCache ? &PassCache[(size_t)by * world->x + bx] : NULL;
    epoch = (uint32_t)(frame_loops / FPS) + 1;
    if (slot && (*slot >> 2) == epoch && (*slot & 3U))
	return (*slot & 3U) == PASS_FREE;

    cx = BLOCK_CENTER(bx);
    cy = BLOCK_CENTER(by);

    for (i = -1; i <= 1; i += 2) {
	for (j = -1; j <= 1; j += 2) {
	    clpos_t pos;
	    pos.cx = cx + i * delta;
	    pos.cy = cy + j * delta;
	    pos = World_wrap_clpos(pos);

	    group = is_inside(pos.cx, pos.cy, hitmask, NULL);
	    if (group != NO_GROUP) {
		group_t *gp = groupptr_by_id(group);
		if (gp != NULL && gp->type == WORMHOLE) {
		    special = true;
		    if (gp->mapobj_ind == pl->wormHoleDest)
			group = NO_GROUP;
		} else if (gp != NULL && gp->type == TARGET) {
		    special = true;
		    if (!options.targetTeamCollision && gp->team == pl->team)
			group = NO_GROUP;
		}
	    }
	    if (group != NO_GROUP) {
		if (!special && slot)
		    *slot = (epoch << 2) | PASS_SOLID;
		return false;
	    }
	}
    }
    if (!special && slot)
	*slot = (epoch << 2) | PASS_FREE;
    return true;
}

static bool Smart_has_line_of_sight(player_t *pl, clpos_t from, clpos_t to, bool for_ball)
{
    double dx = CLICK_TO_PIXEL(to.cx - from.cx);
    double dy = CLICK_TO_PIXEL(to.cy - from.cy);
    double dist, step, d;
    double ux, uy, px, py;

    dx = WRAP_DX(dx);
    dy = WRAP_DY(dy);
    dist = LENGTH(dx, dy);
    if (dist <= BLOCK_SZ * 0.5)
	return true;

    ux = dx / dist;
    uy = dy / dist;
    px = CLICK_TO_PIXEL(from.cx);
    py = CLICK_TO_PIXEL(from.cy);
    step = BLOCK_SZ * 0.45;

    for (d = step; d < dist - BLOCK_SZ * 0.4; d += step) {
	int bx = (int)((px + ux * d) / BLOCK_SZ);
	int by = (int)((py + uy * d) / BLOCK_SZ);

	if (BIT(world->rules->mode, WRAP_PLAY)) {
	    bx = WRAP_XBLOCK(bx);
	    by = WRAP_YBLOCK(by);
	}
	if (bx < 0 || bx >= world->x || by < 0 || by >= world->y)
	    return false;

	if (for_ball) {
	    if (!Smart_empty_space_for_ball(bx, by))
		return false;
	} else {
	    if (!Smart_block_passable(pl, bx, by))
		return false;
	}
    }
    return true;
}

/*
 * Fast bounded A* pathfinder on block grid.
 * Populates my_data->waypoints[] and returns the next waypoint click position.
 */
typedef struct astar_node {
    int16_t	bx, by;
    int16_t	parent;
    float	g, f;
    bool	closed;
} astar_node_t;

/*
 * A* working storage: a binary heap (lazy deletion) for the open list and a
 * generation-stamped grid mapping blocks to node slots, so neither the open
 * list nor the visited set is ever scanned linearly.
 */
#define ASTAR_HEAP_MAX	(ASTAR_MAX_NODES * 8)

typedef struct astar_heap_entry {
    float	f;
    int16_t	idx;
} astar_heap_entry_t;

static astar_heap_entry_t	AstarHeap[ASTAR_HEAP_MAX];
static int			AstarHeapN;
static uint32_t			*AstarStamp;
static int16_t			*AstarSlot;
static uint32_t			AstarGen;
static int			AstarW, AstarH;

static void Smart_astar_push(float f, int idx)
{
    int i;

    if (AstarHeapN >= ASTAR_HEAP_MAX)
	return;
    i = AstarHeapN++;
    while (i > 0) {
	int parent = (i - 1) / 2;
	if (AstarHeap[parent].f <= f)
	    break;
	AstarHeap[i] = AstarHeap[parent];
	i = parent;
    }
    AstarHeap[i].f = f;
    AstarHeap[i].idx = (int16_t)idx;
}

static int Smart_astar_pop(void)
{
    astar_heap_entry_t last;
    int top = AstarHeap[0].idx;
    int i = 0;

    last = AstarHeap[--AstarHeapN];
    while (AstarHeapN > 0) {
	int child = 2 * i + 1;
	if (child >= AstarHeapN)
	    break;
	if (child + 1 < AstarHeapN && AstarHeap[child + 1].f < AstarHeap[child].f)
	    child++;
	if (AstarHeap[child].f >= last.f)
	    break;
	AstarHeap[i] = AstarHeap[child];
	i = child;
    }
    if (AstarHeapN > 0)
	AstarHeap[i] = last;
    return top;
}

/* Wrap-aware straight-line distance between two blocks. */
static float Smart_astar_h(int bx, int by, blkpos_t goal)
{
    double hdx = goal.bx - bx;
    double hdy = goal.by - by;

    if (BIT(world->rules->mode, WRAP_PLAY)) {
	if (hdx > world->x / 2) hdx -= world->x;
	if (hdx < -world->x / 2) hdx += world->x;
	if (hdy > world->y / 2) hdy -= world->y;
	if (hdy < -world->y / 2) hdy += world->y;
    }
    return (float)LENGTH(hdx, hdy);
}

static bool Smart_astar_prepare(void)
{
    if (AstarStamp == NULL || AstarW != world->x || AstarH != world->y) {
	free(AstarStamp);
	free(AstarSlot);
	AstarStamp = calloc((size_t)world->x * world->y, sizeof(*AstarStamp));
	AstarSlot = malloc((size_t)world->x * world->y * sizeof(*AstarSlot));
	AstarW = world->x;
	AstarH = world->y;
	AstarGen = 0;
	if (AstarStamp == NULL || AstarSlot == NULL) {
	    free(AstarStamp);
	    free(AstarSlot);
	    AstarStamp = NULL;
	    AstarSlot = NULL;
	    AstarW = AstarH = 0;
	    return false;
	}
    }
    if (++AstarGen == 0) {	/* generation wrapped: clear stale stamps */
	memset(AstarStamp, 0, (size_t)AstarW * AstarH * sizeof(*AstarStamp));
	AstarGen = 1;
    }
    AstarHeapN = 0;
    return true;
}

static clpos_t Smart_find_open_corridor_target(player_t *pl)
{
    double px = CLICK_TO_PIXEL(pl->pos.cx);
    double py = CLICK_TO_PIXEL(pl->pos.cy);
    int best_dir = pl->dir;
    double best_score = -1.0;
    int i;

    for (i = 0; i < 16; i++) {
	int ray_dir = MOD2((i * RES) / 16, RES);
	double c = tcos(ray_dir);
	double s = tsin(ray_dir);
	double d, max_d = 0.0;
	int delta = MOD2(ray_dir - pl->dir, RES);
	double forward_bonus;

	if (delta > RES / 2)
	    delta = RES - delta;
	forward_bonus = 1.0 + (1.0 - (double)delta / (RES / 2)) * 0.45;

	for (d = BLOCK_SZ; d <= 16.0 * BLOCK_SZ; d += BLOCK_SZ * 0.75) {
	    int bx = (int)((px + c * d) / BLOCK_SZ);
	    int by = (int)((py + s * d) / BLOCK_SZ);
	    if (!Smart_block_passable(pl, bx, by))
		break;
	    max_d = d;
	}
	if (max_d * forward_bonus > best_score) {
	    best_score = max_d * forward_bonus;
	    best_dir = ray_dir;
	}
    }

    {
	clpos_t target;
	double dist = MAX(best_score, 4.0 * BLOCK_SZ);
	target.cx = pl->pos.cx + PIXEL_TO_CLICK(tcos(best_dir) * dist);
	target.cy = pl->pos.cy + PIXEL_TO_CLICK(tsin(best_dir) * dist);
	return World_wrap_clpos(target);
    }
}

static clpos_t Smart_find_next_waypoint(player_t *pl, clpos_t goal_pos)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    blkpos_t start_blk = Clpos_to_blkpos(pl->pos);
    blkpos_t goal_blk  = Clpos_to_blkpos(goal_pos);
    astar_node_t nodes[ASTAR_MAX_NODES];
    int num_nodes = 0, best_idx = 0, max_expand;
    float best_h;
    int i, dir;
    static const int dx8[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
    static const int dy8[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };
    static const float cost8[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };

    /* If direct line of sight is clear, fly directly to goal_pos */
    if (Smart_has_line_of_sight(pl, pl->pos, goal_pos, false)) {
	my_data->num_waypoints = 0;
	return goal_pos;
    }

    /* Check if cached path is still valid (allow moving goals to shift up to 3 blocks) */
    if (my_data->num_waypoints > 0
	&& my_data->cur_waypoint < my_data->num_waypoints
	&& ABS(my_data->last_goal_blk.bx - goal_blk.bx) <= 3
	&& ABS(my_data->last_goal_blk.by - goal_blk.by) <= 3
	&& ABS(my_data->last_astar_tick - my_data->robot_count) < ASTAR_RECALC_TICKS) {

	/* String-pulling: advance to furthest visible waypoint along cached path */
	while (my_data->cur_waypoint + 1 < my_data->num_waypoints) {
	    clpos_t wp = my_data->waypoints[my_data->cur_waypoint];
	    clpos_t next_wp = my_data->waypoints[my_data->cur_waypoint + 1];
	    double d = Wrap_length(wp.cx - pl->pos.cx, wp.cy - pl->pos.cy) / CLICK;
	    if (d < BLOCK_SZ * 2.5 || Smart_has_line_of_sight(pl, pl->pos, next_wp, false))
		my_data->cur_waypoint++;
	    else
		break;
	}
	return my_data->waypoints[my_data->cur_waypoint];
    }

    my_data->last_astar_tick = my_data->robot_count;
    my_data->last_goal_blk = goal_blk;

    /* Scale A* search budget by skill level */
    max_expand = 250 + (eff_level * (ASTAR_MAX_NODES - 260)) / 100;
    LIMIT(max_expand, 250, ASTAR_MAX_NODES - 8);

    if (!Smart_astar_prepare())
	return Smart_find_open_corridor_target(pl);

    nodes[0].bx = start_blk.bx;
    nodes[0].by = start_blk.by;
    nodes[0].parent = -1;
    nodes[0].g = 0.0f;
    best_h = Smart_astar_h(start_blk.bx, start_blk.by, goal_blk);
    nodes[0].f = best_h;
    nodes[0].closed = false;
    num_nodes = 1;
    AstarStamp[(size_t)start_blk.by * world->x + start_blk.bx] = AstarGen;
    AstarSlot[(size_t)start_blk.by * world->x + start_blk.bx] = 0;
    Smart_astar_push(best_h, 0);

    while (AstarHeapN > 0 && num_nodes < max_expand) {
	int cur = Smart_astar_pop();

	if (nodes[cur].closed)
	    continue;

	if (nodes[cur].bx == goal_blk.bx && nodes[cur].by == goal_blk.by) {
	    best_idx = cur;
	    break;
	}
	nodes[cur].closed = true;

	for (dir = 0; dir < 8; dir++) {
	    int nbx = nodes[cur].bx + dx8[dir];
	    int nby = nodes[cur].by + dy8[dir];
	    size_t cell;
	    float ng, nh, nf;

	    if (BIT(world->rules->mode, WRAP_PLAY)) {
		nbx = WRAP_XBLOCK(nbx);
		nby = WRAP_YBLOCK(nby);
	    }
	    if (nbx < 0 || nbx >= world->x || nby < 0 || nby >= world->y)
		continue;
	    cell = (size_t)nby * world->x + nbx;

	    /* Avoid cutting diagonal corners through walls */
	    if (dir >= 4) {
		if (!Smart_block_passable(pl, nodes[cur].bx + dx8[dir], nodes[cur].by)
		    || !Smart_block_passable(pl, nodes[cur].bx, nodes[cur].by + dy8[dir]))
		    continue;
	    }

	    ng = nodes[cur].g + cost8[dir];
	    if (AstarStamp[cell] == AstarGen) {
		int existing = AstarSlot[cell];
		if (ng < nodes[existing].g) {
		    nh = nodes[existing].f - nodes[existing].g;
		    nodes[existing].g = ng;
		    nodes[existing].f = ng + nh;
		    nodes[existing].parent = cur;
		    nodes[existing].closed = false;
		    Smart_astar_push(nodes[existing].f, existing);
		}
		continue;
	    }

	    if (!(nbx == goal_blk.bx && nby == goal_blk.by)
		&& !Smart_block_passable(pl, nbx, nby))
		continue;

	    if (num_nodes >= max_expand)
		break;

	    nh = Smart_astar_h(nbx, nby, goal_blk);
	    nf = ng + nh;
	    nodes[num_nodes].bx = nbx;
	    nodes[num_nodes].by = nby;
	    nodes[num_nodes].parent = cur;
	    nodes[num_nodes].g = ng;
	    nodes[num_nodes].f = nf;
	    nodes[num_nodes].closed = false;
	    AstarStamp[cell] = AstarGen;
	    AstarSlot[cell] = (int16_t)num_nodes;
	    Smart_astar_push(nf, num_nodes);

	    if (nh < best_h && ng >= 2.0f) {
		best_h = nh;
		best_idx = num_nodes;
	    }
	    num_nodes++;
	}
    }

    /* If no node reduced heuristic distance, pick furthest explored node to escape pocket */
    if (best_idx == 0 && num_nodes > 1) {
	float max_g = -1.0f;
	for (i = 1; i < num_nodes; i++) {
	    if (nodes[i].g > max_g) {
		max_g = nodes[i].g;
		best_idx = i;
	    }
	}
    }

    /* Reconstruct path from best_idx back to start */
    if (best_idx > 0) {
	int path_indices[ASTAR_MAX_NODES];
	int len = 0, curr = best_idx;

	while (curr >= 0 && len < ASTAR_MAX_NODES) {
	    path_indices[len++] = curr;
	    curr = nodes[curr].parent;
	}
	my_data->num_waypoints = 0;
	if (len > 1) {
	    int step = MAX(1, (len - 1) / (ASTAR_MAX_WAYPOINTS - 1));
	    for (i = len - 2; i >= 0 && my_data->num_waypoints < ASTAR_MAX_WAYPOINTS; i -= step) {
		int idx = path_indices[i];
		my_data->waypoints[my_data->num_waypoints].cx = BLOCK_CENTER(nodes[idx].bx);
		my_data->waypoints[my_data->num_waypoints].cy = BLOCK_CENTER(nodes[idx].by);
		my_data->num_waypoints++;
	    }
	    my_data->cur_waypoint = 0;
	    /* String-pulling: jump immediately to furthest waypoint with clear LOS */
	    while (my_data->cur_waypoint + 1 < my_data->num_waypoints) {
		clpos_t next_wp = my_data->waypoints[my_data->cur_waypoint + 1];
		if (Smart_has_line_of_sight(pl, pl->pos, next_wp, false))
			my_data->cur_waypoint++;
		else
			break;
	    }
	    return my_data->waypoints[my_data->cur_waypoint];
	}
    }

    return Smart_find_open_corridor_target(pl);
}

/* =========================================================================
 * Quadratic Ballistic Lead Solver & Precision Aiming
 * ========================================================================= */

/*
 * Computes exact intercept direction to hit 'target' with a projectile of speed
 * 'proj_speed' fired from 'pl'. Accounts for relative velocity and skill-level
 * jitter; gravity is not modelled.
 */
static bool Smart_compute_lead_dir(player_t *pl, clpos_t target_pos, vector_t target_vel,
				   double proj_speed, int *out_dir, double *out_intercept_time)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    double rx = CLICK_TO_PIXEL(target_pos.cx - pl->pos.cx);
    double ry = CLICK_TO_PIXEL(target_pos.cy - pl->pos.cy);
    double vx, vy, a, b, c, disc, t = 0.0;
    double aim_x, aim_y, angle_deg, jitter_range;
    int dir;

    rx = WRAP_DX(rx);
    ry = WRAP_DY(ry);

    /* At low skill levels, scale down velocity compensation */
    {
	double lead_accuracy = 0.25 + 0.75 * (eff_level / 100.0);
	vx = (target_vel.x - pl->vel.x) * lead_accuracy;
	vy = (target_vel.y - pl->vel.y) * lead_accuracy;
    }

    /* Solve ||r + v * t||^2 = (proj_speed * t)^2 */
    a = vx * vx + vy * vy - proj_speed * proj_speed;
    b = 2.0 * (rx * vx + ry * vy);
    c = rx * rx + ry * ry;

    if (ABS(a) < 1e-6) {
	if (ABS(b) > 1e-6)
	    t = -c / b;
    } else {
	disc = b * b - 4.0 * a * c;
	if (disc >= 0.0) {
	    double sqrt_d = sqrt(disc);
	    double t1 = (-b - sqrt_d) / (2.0 * a);
	    double t2 = (-b + sqrt_d) / (2.0 * a);
	    if (t1 > 0.0 && t2 > 0.0)
		t = MIN(t1, t2);
	    else if (t1 > 0.0)
		t = t1;
	    else if (t2 > 0.0)
		t = t2;
	}
    }

    if (t <= 0.0 || t > options.shotLife) {
	t = LENGTH(rx, ry) / MAX(proj_speed, 1.0);
    }
    if (out_intercept_time)
	*out_intercept_time = t;

    aim_x = rx + vx * t;
    aim_y = ry + vy * t;

    /* Check if predicted intercept point hits a wall */
    if (eff_level >= 50) {
	clpos_t intercept_cl;
	intercept_cl.cx = pl->pos.cx + PIXEL_TO_CLICK(aim_x);
	intercept_cl.cy = pl->pos.cy + PIXEL_TO_CLICK(aim_y);
	intercept_cl = World_wrap_clpos(intercept_cl);
	if (!Smart_has_line_of_sight(pl, pl->pos, intercept_cl, false))
	    return false;
    }

    angle_deg = findDir(aim_x, aim_y);

    /* Add skill-based aiming error for levels < 95 */
    if (eff_level < 95) {
	jitter_range = (100 - eff_level) * 0.16; /* in RES units (128 = full circle) */
	angle_deg += (rfrac() - 0.5) * 2.0 * jitter_range;
    }

    dir = MOD2((int)(angle_deg + 0.5), RES);
    *out_dir = dir;
    return true;
}

/*
 * Maximum turn per tick (in dir units, RES = full circle) for a skill level.
 * Bots turn at a bounded rate like a human with a mouse; the budget is shared
 * by all steering calls of one tick, so aiming and flying cannot both turn
 * the ship by the full amount.
 */
static int Smart_max_turn_per_tick(int eff_level)
{
    return 3 + (eff_level * 9) / 100;
}

/* Absolute angular difference of two directions, 0 .. RES/2. */
static int Smart_dir_error(int a, int b)
{
    int d = MOD2(a - b, RES);

    return (d > RES / 2) ? RES - d : d;
}

/*
 * Rate-limited ship orientation controller.
 */
static void Smart_steer_to_dir(player_t *pl, int desired_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int delta = MOD2(desired_dir - pl->dir, RES);
    int step;

    if (delta > RES / 2)
	delta -= RES;
    step = delta;
    LIMIT(step, -my_data->turn_budget, my_data->turn_budget);
    if (step == 0)
	return;
    my_data->turn_budget -= ABS(step);

    /*
     * Immediately rotate via Turn_player(pl, true) so polygon wall collision
     * and turn-push are applied and pl->dir is up-to-date on this frame.
     */
    pl->turnspeed = 0;
    pl->turnresistance = 0;
    pl->turnacc = 0;
    pl->turnvel = 0;
    Player_set_float_dir(pl, (double)MOD2(pl->dir + step, RES));
    pl->wanted_float_dir = pl->float_dir;
    Turn_player(pl, true);
}

/* =========================================================================
 * Multi-Ray Wall Avoidance & CPA Projectile Threat Scanner
 * ========================================================================= */

static void Smart_scan_environment(player_t *pl,
				   vector_t *repulse_vec,
				   bool *need_shield_now,
				   bool *high_threat,
				   int *best_item_i,
				   double *best_item_score)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    int i, j, obj_count;
    object_t **obj_list;
    const int max_objs = 800;
    long killing_shots;
    double px = CLICK_TO_PIXEL(pl->pos.cx);
    double py = CLICK_TO_PIXEL(pl->pos.cy);
    double lookahead_dist;
    int num_rays = (eff_level >= 50) ? 16 : 8;

    repulse_vec->x = 0.0;
    repulse_vec->y = 0.0;
    my_data->cut_thrust = false;
    *need_shield_now = false;
    *high_threat = false;
    *best_item_i = NO_IND;
    *best_item_score = 0.0;

    /*
     * 1. Velocity-Aware Multi-Ray Wall Avoidance
     * Only repulse from walls we are moving towards (vel_proj > 0.5) or touching,
     * so stationary or corridor-flying ships never get trapped in potential wells.
     */
    lookahead_dist = MAX(2.5 * BLOCK_SZ, pl->velocity * 10.0);
    LIMIT(lookahead_dist, 2.5 * BLOCK_SZ, 10.0 * BLOCK_SZ);

    for (i = 0; i < num_rays; i++) {
	int ray_dir = MOD2((i * RES) / num_rays, RES);
	double c = tcos(ray_dir);
	double s = tsin(ray_dir);
	double vel_proj = pl->vel.x * c + pl->vel.y * s;
	double max_ray_d = (vel_proj > 0.5) ? lookahead_dist : (BLOCK_SZ * 0.9);
	double d;

	for (d = BLOCK_SZ * 0.45; d <= max_ray_d; d += BLOCK_SZ * 0.45) {
	    int bx = (int)((px + c * d) / BLOCK_SZ);
	    int by = (int)((py + s * d) / BLOCK_SZ);

	    if (!Smart_block_passable(pl, bx, by)) {
		if (vel_proj > 0.5) {
		    double t_impact = d / MAX(vel_proj, 1.0);
		    if (t_impact < 10.0) {
			double weight = my_data->genes.wall_weight * (10.0 - t_impact) * 1.4;
			/* Brake against wall normal + slide tangentially */
			double tx = -s;
			double ty = c;
			if (tx * pl->vel.x + ty * pl->vel.y < 0.0) {
			    tx = -tx;
			    ty = -ty;
			}
			repulse_vec->x += (-c * 0.85 + tx * 0.55) * weight;
			repulse_vec->y += (-s * 0.85 + ty * 0.55) * weight;
		    }
		} else if (d < BLOCK_SZ * 0.85) {
		    repulse_vec->x -= c * 4.0;
		    repulse_vec->y -= s * 4.0;
		}

		if (d < SHIP_SZ * 2.2 && vel_proj > 2.0) {
		    *high_threat = true;
		    if (options.maxShieldedWallBounceSpeed > options.maxUnshieldedWallBounceSpeed)
			*need_shield_now = true;
		}
		break;
	    }
	}
    }

    /* Compensate strong local gravity */
    {
	vector_t grav = World_gravity(pl->pos);
	repulse_vec->x -= grav.x * 5.0;
	repulse_vec->y -= grav.y * 5.0;
    }

    /* 2. CPA (Closest Point of Approach) Object, Item & Projectile Scanner */
    killing_shots = KILLING_SHOTS;
    if (options.treasureCollisionMayKill)
	killing_shots |= OBJ_BALL_BIT;
    if (options.wreckageCollisionMayKill)
	killing_shots |= OBJ_WRECKAGE_BIT;
    if (options.asteroidCollisionMayKill)
	killing_shots |= OBJ_ASTEROID_BIT;

    if (NumObjs >= options.cellGetObjectsThreshold)
	Cell_get_objects(pl->pos, (int)(Visibility_distance / BLOCK_SZ),
			 max_objs, &obj_list, &obj_count);
    else {
	obj_list = Obj;
	obj_count = NumObjs;
    }

    for (j = 0; j < obj_count; j++) {
	object_t *obj = obj_list[j];
	double rx, ry, rvx, rvy, dist_sq, dist;

	if (obj->type == OBJ_SPARK || obj->type == OBJ_DEBRIS)
	    continue;

	if (obj->type == OBJ_BALL
	    && !WITHIN(my_data->robot_count, my_data->last_thrown_ball, 3 * FPS))
	    SET_BIT(pl->used, USES_CONNECTOR);

	rx = WRAP_DX(CLICK_TO_PIXEL(obj->pos.cx - pl->pos.cx));
	ry = WRAP_DY(CLICK_TO_PIXEL(obj->pos.cy - pl->pos.cy));
	dist_sq = rx * rx + ry * ry;

	/* Evaluate beneficial items within view range only */
	if (obj->type == OBJ_ITEM) {
	    itemobject_t *item = ITEM_PTR(obj);
	    int itype = item->item_type;
	    double val = 2.0;

	    if (dist_sq > sqr(Visibility_distance))
		continue;
	    dist = sqrt(dist_sq);

	    if (itype == ITEM_AUTOPILOT || itype == ITEM_PHASING)
		continue;
	    if (!options.smartRobotsLaser && itype == ITEM_LASER)
		continue;
	    if (pl->item[itype] >= world->items[itype].limit)
		continue;
	    if ((IsDefensiveItem(itype) && CountDefensiveItems(pl) >= options.maxDefensiveItems)
		|| (IsOffensiveItem(itype) && CountOffensiveItems(pl) >= options.maxOffensiveItems))
		continue;

	    if (itype == ITEM_FUEL) {
		if (pl->fuel.sum >= pl->fuel.max * 0.90)
		    continue;
		val = (pl->fuel.sum < my_data->fuel_l3) ? 6.0 : 2.5;
	    } else if (itype == ITEM_EMERGENCY_SHIELD || itype == ITEM_ARMOR || itype == ITEM_DEFLECTOR) {
		val = BIT(pl->have, HAS_SHIELD) ? 3.5 : 6.5;
	    } else if (itype == ITEM_WIDEANGLE || itype == ITEM_REARSHOT
		       || itype == ITEM_AFTERBURNER || itype == ITEM_EMERGENCY_THRUST) {
		val = 5.0;
	    } else if (itype == ITEM_MISSILE || itype == ITEM_MINE
		       || itype == ITEM_LASER || itype == ITEM_ECM || itype == ITEM_CLOAK) {
		val = 4.2;
	    }

	    /* Only pay for the line-of-sight test if the item could still win */
	    {
		double base = (val * 220.0) / (dist + 40.0);
		if (base * 1.75 > *best_item_score) {
		    double score = base * (Smart_has_line_of_sight(pl, pl->pos, obj->pos, false)
					   ? 1.75 : 1.0);
		    if (score > *best_item_score) {
			*best_item_score = score;
			*best_item_i = j;
		    }
		}
	    }
	    continue;
	}

	if (dist_sq > sqr(Visibility_distance))
	    continue;
	dist = sqrt(dist_sq);

	if (!BIT(OBJ_TYPEBIT(obj->type), killing_shots))
	    continue;
	if (Team_immune(obj->id, pl->id))
	    continue;
	if (obj->id == pl->id && options.selfImmunity)
	    continue;
	if (obj->type == OBJ_PULSE) {
	    pulseobject_t *pulse = PULSE_PTR(obj);
	    if (pulse->id == pl->id && !pulse->pulse_refl)
		continue;
	}

	/* Compute Time to Closest Approach (TCA) and Closest Point of Approach (CPA) */
	rvx = obj->vel.x - pl->vel.x;
	rvy = obj->vel.y - pl->vel.y;
	{
	    double v_sq = rvx * rvx + rvy * rvy;
	    double tca = (v_sq > 0.01) ? (-(rx * rvx + ry * rvy) / v_sq) : 0.0;
	    double horizon = 12.0 + 28.0 * (eff_level / 100.0);
	    double cpa_x, cpa_y, cpa_dist, danger_radius;

	    if (tca < 0.0)
		tca = 0.0;
	    if (tca > horizon)
		continue;

	    cpa_x = rx + rvx * tca;
	    cpa_y = ry + rvy * tca;
	    cpa_dist = LENGTH(cpa_x, cpa_y);
	    danger_radius = SHIP_SZ * 2.4 + obj->pl_range;

	    if (cpa_dist < danger_radius * 2.2) {
		/* Steer perpendicular to incoming projectile velocity */
		double perp_x = -rvy;
		double perp_y = rvx;
		double plen = LENGTH(perp_x, perp_y);
		double weight = my_data->genes.evade_weight * 28.0 / (tca + 1.5);

		*high_threat = true;
		if (plen > 0.1) {
		    /* Pick side that moves us away from cpa_x, cpa_y */
		    if (perp_x * cpa_x + perp_y * cpa_y > 0.0) {
			perp_x = -perp_x;
			perp_y = -perp_y;
		    }
		    repulse_vec->x += (perp_x / plen) * weight;
		    repulse_vec->y += (perp_y / plen) * weight;
		} else if (dist > 0.1) {
		    repulse_vec->x -= (rx / dist) * weight;
		    repulse_vec->y -= (ry / dist) * weight;
		}
	    }

	    /* Precision Shield Activation (only raise shields right before impact) */
	    if ((tca <= 2.2 && cpa_dist <= SHIP_SZ + obj->pl_range + 22.0)
		|| dist <= SHIP_SZ + obj->pl_range + 24.0) {
		*need_shield_now = true;
	    }

	    /* Smart countermeasures */
	    if (obj->type == OBJ_SMART_SHOT && dist < ECM_DISTANCE / 3)
		Fire_ecm(pl);
	    else if (obj->type == OBJ_MINE && dist < ECM_DISTANCE / 2)
		Fire_ecm(pl);
	    else if (obj->type == OBJ_HEAT_SHOT && dist < 8 * BLOCK_SZ) {
		my_data->cut_thrust = true;	/* enforced at the end of Smart_fly_towards */
		if (pl->fuel.num_tanks > 0 && pl->fuel.sum > my_data->fuel_l1)
		    Tank_handle_detach(pl);
	    }
	}
    }

    /* Convert best_item_i from obj_list[] index to global Obj[] index */
    if (*best_item_i >= 0 && obj_list != Obj) {
	int target_id = obj_list[*best_item_i]->id;
	*best_item_i = NO_IND;
	for (j = 0; j < NumObjs; j++) {
	    if (Obj[j]->id == target_id) {
		*best_item_i = j;
		break;
	    }
	}
    }
}

/* =========================================================================
 * Desired Velocity Vector Flight Controller (PD Thrust & Direction)
 * ========================================================================= */

static void Smart_fly_towards(player_t *pl, clpos_t target_pos, double desired_speed,
			      vector_t repulse_vec, int *aim_override_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    double dx = WRAP_DX(CLICK_TO_PIXEL(target_pos.cx - pl->pos.cx));
    double dy = WRAP_DY(CLICK_TO_PIXEL(target_pos.cy - pl->pos.cy));
    double dist = LENGTH(dx, dy);
    double des_vx = 0.0, des_vy = 0.0;
    double err_vx, err_vy, err_mag;
    int thrust_dir;

    desired_speed *= my_data->genes.speed_factor;
    LIMIT(desired_speed, 6.0, 38.0);

    if (dist > 1.0) {
	double speed = desired_speed;
	/* Only slow down on approach when actively refueling at a fuel station */
	if (BIT(pl->used, USES_REFUEL) && dist < 3.0 * BLOCK_SZ)
	    speed = MIN(desired_speed, 2.0 + (dist / (3.0 * BLOCK_SZ)) * desired_speed);
	des_vx = (dx / dist) * speed;
	des_vy = (dy / dist) * speed;
    }

    /* Combine goal velocity with obstacle/projectile repulsion */
    des_vx += repulse_vec.x;
    des_vy += repulse_vec.y;

    /* Compute velocity error vector (desired_vel - current_vel) */
    err_vx = des_vx - pl->vel.x;
    err_vy = des_vy - pl->vel.y;
    err_mag = LENGTH(err_vx, err_vy);

    thrust_dir = MOD2((int)(findDir(err_vx, err_vy) + 0.5), RES);

    if (aim_override_dir != NULL && BIT(pl->used, HAS_SHOT | HAS_LASER)) {
	/*
	 * On a firing frame: if we don't have an urgent wall/projectile dodge
	 * and aren't stalled, point at the ballistic lead angle to fire!
	 * On non-firing frames (or when accelerating/dodging), steer in thrust_dir!
	 */
	double repulse_mag = LENGTH(repulse_vec.x, repulse_vec.y);
	if (repulse_mag > 10.0 || pl->velocity < 3.5 || (err_mag > 9.0 && (my_data->robot_count & 1))) {
	    CLR_BIT(pl->used, HAS_SHOT | HAS_LASER);
	    Smart_steer_to_dir(pl, thrust_dir);
	    pl->power = MAX_PLAYER_POWER;
	    Thrust(pl, err_mag > 1.5);
	} else {
	    int delta_thrust = MOD2(thrust_dir - *aim_override_dir, RES);
	    Smart_steer_to_dir(pl, *aim_override_dir);
	    pl->power = MAX_PLAYER_POWER;
	    if ((delta_thrust < RES / 4 || delta_thrust > 3 * RES / 4) && err_mag > 1.5)
		Thrust(pl, true);
	    else
		Thrust(pl, false);
	}
    } else {
	Smart_steer_to_dir(pl, thrust_dir);
	if (err_mag > 1.2) {
	    int delta = MOD2(thrust_dir - pl->dir, RES);
	    pl->power = (err_mag > 5.0) ? MAX_PLAYER_POWER : (MAX_PLAYER_POWER * 0.75);
	    Thrust(pl, (delta < RES / 4 || delta > 3 * RES / 4));
	} else {
	    Thrust(pl, false);
	}
    }

    /* Heat seekers home in on exhaust: coast instead of thrusting */
    if (my_data->cut_thrust)
	Thrust(pl, false);
}

/* =========================================================================
 * Combat & Weapon Selection
 * ========================================================================= */

static bool Smart_detect_ship(player_t *pl, player_t *ship)
{
    double distance;

    if (!Player_is_alive(ship) || Player_is_phasing(ship))
	return false;

    distance = Wrap_length(ship->pos.cx - pl->pos.cx,
			   ship->pos.cy - pl->pos.cy) / CLICK;

    /* Bots see no further than a human's view range, even without limited visibility */
    if (pl->visibility[GetInd(ship->id)].canSee)
	return (distance <= Visibility_distance);

    if (distance > Visibility_distance)
	return false;

    if ((Player_is_thrusting(ship) && options.cloakedExhaust)
	|| BIT(ship->used, HAS_SHOT)
	|| BIT(ship->used, HAS_LASER)
	|| Player_is_refueling(ship)
	|| Player_is_repairing(ship)
	|| Player_uses_connector(ship)
	|| Player_uses_tractor_beam(ship)
	|| BIT(ship->have, HAS_BALL))
	return true;

    return false;
}

static void Smart_engage_enemy(player_t *pl, player_t *enemy, double enemy_dist,
			       bool has_los, bool urgent_dodge,
			       int *out_aim_dir, bool *has_aim_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    int lead_dir;
    int aim_tol = 2 + (100 - eff_level) / 25;	/* firing tolerance in dir units */
    double intercept_t = 0.0;

    *has_aim_dir = false;

    /* Lock onto target enemy for radar, missiles, and tractor beams */
    pl->lock.pl_id = enemy->id;
    SET_BIT(pl->lock.tagged, LOCK_PLAYER);
    pl->lock.distance = enemy_dist;

    /* ECM if enemy is very close */
    if (pl->item[ITEM_ECM] > 0 && enemy_dist < ECM_DISTANCE / 4)
	Fire_ecm(pl);

    /* Steal item if in transporter range */
    if (pl->item[ITEM_TRANSPORTER] > 0
	&& enemy_dist < TRANSPORTER_DISTANCE
	&& pl->fuel.sum > -ED_TRANSPORTER)
	Do_transporter(pl);

    /* Missile Firing (Smart missiles can curve around obstacles; Heat/Torpedo when closer) */
    if (pl->item[ITEM_MISSILE] > 0
	&& !WITHIN(my_data->robot_count, my_data->last_fired_missile, 12)
	&& (has_los || options.allowSmartMissiles)
	&& enemy_dist < Visibility_distance * 0.95) {
	int mtype = options.allowSmartMissiles ? OBJ_SMART_SHOT : OBJ_HEAT_SHOT;
	Fire_shot(pl, mtype, pl->dir);
	my_data->last_fired_missile = my_data->robot_count;
    }

    /* Tactical Mine Dropping when pursued or in close quarters */
    if (pl->item[ITEM_MINE] > 0
	&& enemy_dist < 7.0 * BLOCK_SZ
	&& !WITHIN(my_data->robot_count, my_data->last_dropped_mine, 14)
	&& pl->fuel.sum > my_data->fuel_l2) {
	Place_moving_mine(pl);
	my_data->last_dropped_mine = my_data->robot_count;
    }

    if (!has_los)
	return;

    /* Precision Laser Pulse */
    if (options.smartRobotsLaser
	&& pl->item[ITEM_LASER] > pl->num_pulses
	&& pl->fuel.sum + ED_LASER > my_data->fuel_l3
	&& enemy_dist < options.pulseSpeed * options.pulseLife) {
	if (Smart_compute_lead_dir(pl, enemy->pos, enemy->vel, options.pulseSpeed,
				   &lead_dir, &intercept_t)) {
	    *out_aim_dir = lead_dir;
	    *has_aim_dir = true;
	    if (!urgent_dodge)
		Smart_steer_to_dir(pl, lead_dir);
	    if ((my_data->robot_count & 1) == 0
		&& Smart_dir_error(pl->dir, lead_dir) <= aim_tol)
		SET_BIT(pl->used, HAS_LASER);
	    return;
	}
    }

    /*
     * Primary Ballistic Cannon Lead Shot
     * Alternate firing frames (cadence >= 2) so odd frames can thrust in any direction
     * while even frames snap to the ballistic lead angle and fire!
     */
    if (enemy_dist < options.shotSpeed * options.shotLife * 0.95) {
	if (Smart_compute_lead_dir(pl, enemy->pos, enemy->vel, options.shotSpeed,
				   &lead_dir, &intercept_t)) {
	    int fire_cadence = (eff_level >= 70) ? 2 : (eff_level >= 40 ? 3 : 4);
	    *out_aim_dir = lead_dir;
	    *has_aim_dir = true;
	    if (!urgent_dodge)
		Smart_steer_to_dir(pl, lead_dir);
	    if ((my_data->robot_count % fire_cadence) == 0
		&& Smart_dir_error(pl->dir, lead_dir) <= aim_tol)
		SET_BIT(pl->used, HAS_SHOT);
	}
    }
}

/* =========================================================================
 * Capture The Flag (CTF) Specialist Handler
 * ========================================================================= */

static bool Smart_handle_ctf(player_t *pl, vector_t repulse_vec,
			     int *aim_dir, bool *has_aim_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    int i;
    int home_tr = NO_IND, enemy_tr = NO_IND;
    double home_tr_dist = 1e19, enemy_tr_dist = 1e19;

    if (!BIT(world->rules->mode, TEAM_PLAY)
	|| Num_treasures() <= 0
	|| world->teams[pl->team].NumTreasures <= 0)
	return false;

    for (i = 0; i < Num_treasures(); i++) {
	treasure_t *tr = Treasure_by_index(i);
	double d = Wrap_length(tr->pos.cx - pl->pos.cx, tr->pos.cy - pl->pos.cy) / CLICK;

	if (tr->team == pl->team) {
	    if (d < home_tr_dist) {
		home_tr_dist = d;
		home_tr = i;
	    }
	} else if (Team_by_index(tr->team)->NumMembers > 0 && tr->have) {
	    if (d < enemy_tr_dist) {
		enemy_tr_dist = d;
		enemy_tr = i;
	    }
	}
    }

    /* Case 1: We are carrying a ball -> Navigate home & catapult ball into treasure */
    if ((BIT(pl->have, HAS_BALL) || pl->ball != NULL) && home_tr != NO_IND) {
	treasure_t *htr = Treasure_by_index(home_tr);
	ballobject_t *ball = pl->ball;

	if (ball == NULL) {
	    for (i = 0; i < NumObjs; i++) {
		if (Obj[i]->type == OBJ_BALL && Obj[i]->id == pl->id) {
		    ball = BALL_PTR(Obj[i]);
		    break;
		}
	    }
	}

	if (ball != NULL && eff_level >= 50) {
	    double bspeed = LENGTH(ball->vel.x, ball->vel.y);
	    int bdir = MOD2((int)(findDir(ball->vel.x, ball->vel.y) + 0.5), RES);
	    int tdir = MOD2((int)(Wrap_cfindDir(htr->pos.cx - ball->pos.cx,
						htr->pos.cy - ball->pos.cy) + 0.5), RES);
	    int delta_dir = MOD2(tdir - bdir, RES);

	    if ((delta_dir <= 3 || delta_dir >= RES - 3)
		&& bspeed > 7.5
		&& home_tr_dist < 14.0 * BLOCK_SZ
		&& Smart_has_line_of_sight(pl, ball->pos, htr->pos, true)) {
		Detach_ball(pl, NULL);
		CLR_BIT(pl->used, USES_CONNECTOR);
		my_data->last_thrown_ball = my_data->robot_count;
		my_data->accumulated_reward += 6.0;
		return true;
	    }
	}

	{
	    clpos_t wp = Smart_find_next_waypoint(pl, htr->pos);
	    Smart_fly_towards(pl, wp, 14.0, repulse_vec, has_aim_dir ? aim_dir : NULL);
	    return true;
	}
    }

    /* Case 2: Check for loose enemy balls or enemy carriers to intercept */
    {
	int loose_ball_i = NO_IND;
	double loose_ball_dist = 1e19;

	for (i = 0; i < NumObjs; i++) {
	    if (Obj[i]->type == OBJ_BALL) {
		ballobject_t *ball = BALL_IND(i);
		bool ours = (ball->ball_treasure != NULL
			     && ball->ball_treasure->team == pl->team);
		if (!ours && ball->id == NO_ID) {
		    double d = Wrap_length(ball->pos.cx - pl->pos.cx,
					   ball->pos.cy - pl->pos.cy) / CLICK;
		    if (d < loose_ball_dist && d <= Visibility_distance) {
			loose_ball_dist = d;
			loose_ball_i = i;
		    }
		}
	    }
	}

	if (loose_ball_i != NO_IND && loose_ball_dist < enemy_tr_dist) {
	    clpos_t wp = Smart_find_next_waypoint(pl, Obj[loose_ball_i]->pos);
	    SET_BIT(pl->used, USES_CONNECTOR);
	    Smart_fly_towards(pl, wp, 15.0, repulse_vec, has_aim_dir ? aim_dir : NULL);
	    return true;
	}
    }

    /* Case 3: Go steal enemy treasure */
    if (enemy_tr != NO_IND) {
	treasure_t *etr = Treasure_by_index(enemy_tr);
	clpos_t wp = Smart_find_next_waypoint(pl, etr->pos);
	SET_BIT(pl->used, USES_CONNECTOR);
	Smart_fly_towards(pl, wp, 16.0, repulse_vec, has_aim_dir ? aim_dir : NULL);
	return true;
    }

    return false;
}

/* =========================================================================
 * Lifecycle & Main Play Loop
 * ========================================================================= */

int Robot_smart_setup(robot_type_t *type_ptr)
{
    Smart_brain_init();
    *type_ptr = robot_smart_type;
    return 0;
}

static void Robot_smart_round_tick(void)
{
    double min_visibility = 256.0;

    Visibility_distance = min_visibility
	+ (((VISIBILITY_DISTANCE - min_visibility)
	    * (NUM_IDS - NumRobots)) / NUM_IDS);

    if (++RoundCounter % (SMART_BRAIN_SAVE_SECS * FPS) == 0 && BrainDirty)
	Smart_brain_save();
}

static void Robot_smart_create(player_t *pl, char *str)
{
    robot_smart_data_t *my_data;

    Smart_brain_init();

    if (!(my_data = XMALLOC(robot_smart_data_t, 1))) {
	error("no mem for smart robot");
	End_game();
    }
    memset(my_data, 0, sizeof(*my_data));

    my_data->attack = 85;
    my_data->defense = 80;
    my_data->custom_level = 0; /* follow options.smartRobotLevel */
    my_data->learning_enabled = true;

    if (str != NULL && *str != '\0') {
	int a = 85, d = 80, lvl = 0;
	int n = sscanf(str, " %d %d %d", &a, &d, &lvl);
	if (n >= 2) {
	    my_data->attack = a;
	    my_data->defense = d;
	}
	if (n >= 3 && lvl > 0) {
	    LIMIT(lvl, 1, 100);
	    my_data->custom_level = lvl;
	}
    }
    LIMIT(my_data->attack, 1, 99);
    LIMIT(my_data->defense, 1, 99);

    my_data->fuel_l3 = 160.0;
    my_data->fuel_l2 = 100.0;
    my_data->fuel_l1 = 45.0;

    my_data->robot_lock = LOCK_NONE;
    my_data->robot_lock_id = NO_ID;
    my_data->current_state = -1;
    my_data->current_tactic = TACTIC_DOGFIGHT_AGGR;
    my_data->genes = SharedBrain.best_genes;

    pl->turnspeed = 0;
    pl->turnresistance = 0;

    pl->robot_data_ptr->private_data = (void *)my_data;
}

static void Smart_pick_new_patrol_target(player_t *pl)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);

    if (Num_bases() > 0 && rfrac() < 0.70) {
	int b_ind = (int)(rfrac() * Num_bases()) % Num_bases();
	my_data->patrol_pos = Base_by_index(b_ind)->pos;
    } else if (Num_fuels() > 0 && rfrac() < 0.50) {
	int f_ind = (int)(rfrac() * Num_fuels()) % Num_fuels();
	my_data->patrol_pos = Fuel_by_index(f_ind)->pos;
    } else {
	my_data->patrol_pos.cx = (int)(rfrac() * world->cwidth);
	my_data->patrol_pos.cy = (int)(rfrac() * world->cheight);
    }
    my_data->patrol_timer = 140 + (int)(rfrac() * 100);
}

static void Robot_smart_go_home(player_t *pl)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);

    my_data->num_waypoints = 0;
    my_data->cur_waypoint = 0;
    Smart_pick_new_patrol_target(pl);
    Smart_evolve_genes_on_respawn(pl);
    my_data->tactic_timer = 0;	/* choose a fresh tactic after respawn */
}

static void Robot_smart_set_war(player_t *pl, int victim_id)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);

    if (victim_id == NO_ID)
	CLR_BIT(my_data->robot_lock, LOCK_PLAYER);
    else {
	my_data->robot_lock_id = victim_id;
	SET_BIT(my_data->robot_lock, LOCK_PLAYER);
    }
}

static int Robot_smart_war_on_player(player_t *pl)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);

    if (BIT(my_data->robot_lock, LOCK_PLAYER))
	return my_data->robot_lock_id;
    return NO_ID;
}

/*
 * Interactive chat commands for inspecting and controlling Smartbots:
 * - /msg <BotName> status  -> reports skill level, RL episodes, kills/deaths, active tactic
 * - /msg <BotName> level N -> sets individual skill level (1-100, or 0 for global)
 * - /msg <BotName> save    -> saves RL brain to smartbot_brain.dat
 * - /msg <BotName> learn [on|off] -> toggles RL learning
 */
static void Robot_smart_message(player_t *pl, const char *message)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    const char *cmd = message;
    int new_lvl;

    if (cmd == NULL)
	return;

    if (strstr(cmd, "status") != NULL) {
	static const char *tactic_names[] = {
	    "Dogfight", "Snipe/Kite", "Evade", "Harvest/Fuel", "CTF"
	};
	Set_message_f("Lvl:%d(%s) Tactic:%s Ep:%llu K/D:%d/%d Dist:%.1f [%s]",
		      Smart_get_effective_level(my_data),
		      my_data->learning_enabled ? "RL:ON" : "RL:OFF",
		      tactic_names[my_data->current_tactic % NUM_SMART_TACTICS],
		      (unsigned long long)SharedBrain.total_episodes,
		      pl->kills, pl->deaths,
		      my_data->genes.preferred_combat_dist,
		      pl->name);
    } else if (strstr(cmd, "level ") != NULL) {
	const char *p = strstr(cmd, "level ") + 6;
	if (sscanf(p, "%d", &new_lvl) == 1) {
	    if (new_lvl <= 0) {
		my_data->custom_level = 0;
		Set_message_f("Using global smartRobotLevel (%d). [%s]",
			      options.smartRobotLevel, pl->name);
	    } else {
		LIMIT(new_lvl, 1, 100);
		my_data->custom_level = new_lvl;
		Set_message_f("Skill level set to %d. [%s]", new_lvl, pl->name);
	    }
	}
    } else if (strstr(cmd, "save") != NULL) {
	Smart_brain_save();
	Set_message_f("Brain saved to %s (%llu episodes). [%s]",
		      Smart_brain_path(false) ? Smart_brain_path(false) : SMART_BRAIN_FILE,
		      (unsigned long long)SharedBrain.total_episodes,
		      pl->name);
    } else if (strstr(cmd, "learn off") != NULL) {
	my_data->learning_enabled = false;
	Set_message_f("Online learning disabled (pure exploitation). [%s]", pl->name);
    } else if (strstr(cmd, "learn on") != NULL) {
	my_data->learning_enabled = true;
	Set_message_f("Online learning enabled. [%s]", pl->name);
    }
}

static void Robot_smart_destroy(player_t *pl)
{
    if (BrainDirty)
	Smart_brain_save();
    XFREE(pl->robot_data_ptr->private_data);
}

static void Robot_smart_invite(player_t *pl, player_t *inviter)
{
    int war_id = Robot_smart_war_on_player(pl);

    if (inviter->id == war_id || !Player_is_active(inviter)) {
	Refuse_alliance(pl, inviter);
	return;
    }
    Accept_alliance(pl, inviter);
}

static void Robot_smart_play(player_t *pl)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    vector_t repulse_vec;
    bool need_shield_now = false, high_threat = false;
    int best_item_i = NO_IND;
    double best_item_score = 0.0;
    player_t *best_enemy = NULL;
    double best_enemy_dist = 1e19;
    bool best_enemy_los = false;
    int aim_dir = pl->dir;
    bool has_aim_dir = false;
    int i, rl_state;

    if (my_data->robot_count <= 0)
	my_data->robot_count = 2000 + (int)(rfrac() * 64);
    my_data->robot_count--;
    my_data->turn_budget = Smart_max_turn_per_tick(Smart_get_effective_level(my_data));

    /* Default equipment state per tick (shields stay OFF unless needed to save fuel!) */
    CLR_BIT(pl->used, USES_SHOT | USES_SHIELD | USES_LASER);

    if (BIT(pl->have, HAS_EMERGENCY_SHIELD) && !BIT(pl->used, HAS_EMERGENCY_SHIELD))
	Emergency_shield(pl, true);
    if (Player_has_emergency_thrust(pl) && !Player_uses_emergency_thrust(pl))
	Emergency_thrust(pl, true);
    if (Player_has_deflector(pl) && !BIT(world->rules->mode, TIMING))
	Deflector(pl, true);
    if (Player_has_cloaking_device(pl) && pl->fuel.sum > my_data->fuel_l3)
	SET_BIT(pl->used, USES_CLOAKING_DEVICE);
    else
	CLR_BIT(pl->used, USES_CLOAKING_DEVICE);

    if (pl->fuel.sum <= (BIT(world->rules->mode, TIMING) ? 0.0 : 5.0))
	Player_self_destruct(pl, true);
    else
	Player_self_destruct(pl, false);

    /* Blinded by ECM: raise shields and wait */
    if (pl->damaged > 0) {
	if (BIT(pl->have, HAS_SHIELD))
	    SET_BIT(pl->used, HAS_SHIELD);
	return;
    }

    /* Take off cleanly from home base in base direction before turning */
    if (pl->home_base != NULL
	&& QUICK_LENGTH(pl->pos.cx - pl->home_base->pos.cx,
			pl->pos.cy - pl->home_base->pos.cy) < BLOCK_CLICKS / 2) {
	if (!options.allowShields && options.playerStartsShielded && BIT(pl->have, HAS_SHIELD))
	    SET_BIT(pl->used, HAS_SHIELD);
	Smart_steer_to_dir(pl, pl->home_base->dir);
	pl->power = MAX_PLAYER_POWER;
	Thrust(pl, true);
	return;
    }

    /* Opportunistic refueling when passing near a fuel station */
    if (pl->fuel.sum < pl->fuel.max * 0.85) {
	for (i = 0; i < Num_fuels(); i++) {
	    fuel_t *fs = Fuel_by_index(i);
	    if (BIT(world->rules->mode, TEAM_PLAY) && options.teamFuel && fs->team != pl->team)
		continue;
	    if (Wrap_length(pl->pos.cx - fs->pos.cx, pl->pos.cy - fs->pos.cy) <= 90.0 * CLICK
		&& fs->fuel > REFUEL_RATE * timeStep) {
		pl->fs = i;
		SET_BIT(pl->used, USES_REFUEL);
		break;
	    } else {
		CLR_BIT(pl->used, USES_REFUEL);
	    }
	}
    } else {
	CLR_BIT(pl->used, USES_REFUEL);
    }

    /* Scan walls, gravity, projectiles (CPA), and items */
    Smart_scan_environment(pl, &repulse_vec, &need_shield_now, &high_threat,
			   &best_item_i, &best_item_score);

    /* Scan for closest / highest-priority enemy ship */
    for (i = 0; i < NumPlayers; i++) {
	player_t *ship = Player_by_index(i);
	double d, priority_dist;
	bool los;

	if (ship->id == pl->id || !Player_is_active(ship) || Team_immune(pl->id, ship->id))
	    continue;
	if (!Smart_detect_ship(pl, ship))
	    continue;

	d = Wrap_length(ship->pos.cx - pl->pos.cx, ship->pos.cy - pl->pos.cy) / CLICK;
	if (d < 3.2 * SHIP_SZ)
	    need_shield_now = true;

	priority_dist = d;
	/* Prefer enemies in direct line of sight, ball carriers, and war targets */
	los = Smart_has_line_of_sight(pl, pl->pos, ship->pos, false);
	if (los)
	    priority_dist *= 0.55;
	if (BIT(ship->have, HAS_BALL) || ship->ball != NULL)
	    priority_dist *= 0.45;
	if (BIT(my_data->robot_lock, LOCK_PLAYER) && ship->id == my_data->robot_lock_id)
	    priority_dist *= 0.60;

	if (priority_dist < best_enemy_dist) {
	    best_enemy_dist = d;
	    best_enemy = ship;
	    best_enemy_los = los;
	}
    }

    /* Update RL State & Select Tactic periodically */
    if (high_threat)
	my_data->threat_timer = FPS / 2;
    else if (my_data->threat_timer > 0)
	my_data->threat_timer--;
    rl_state = Smart_encode_rl_state(pl, best_enemy ? best_enemy_dist : 1e19,
				     my_data->threat_timer > 0);
    if (--my_data->tactic_timer <= 0) {
	Smart_rl_step(pl, rl_state);
    }

    /* Engage enemy with ballistic lead prediction if in range */
    if (best_enemy != NULL) {
	Smart_engage_enemy(pl, best_enemy, best_enemy_dist, best_enemy_los,
			   LENGTH(repulse_vec.x, repulse_vec.y) > 10.0,
			   &aim_dir, &has_aim_dir);
    }

    /* Raise shield only if we did not just fire and a threat is imminent */
    if (need_shield_now && BIT(pl->have, HAS_SHIELD) && !BIT(pl->used, HAS_SHOT | HAS_LASER)) {
	SET_BIT(pl->used, HAS_SHIELD);
    }
    if (!options.allowShields && options.playerStartsShielded && BIT(pl->have, HAS_SHIELD)) {
	SET_BIT(pl->used, HAS_SHIELD);
    }

    /* Execute movement based on Race mode, CTF, Fuel need, Item collection, or Combat Hunt */
    if (BIT(world->rules->mode, TIMING) && world->NumChecks > 0) {
	clpos_t wp = Smart_find_next_waypoint(pl, Check_by_index(pl->check)->pos);
	Smart_fly_towards(pl, wp, 24.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	return;
    }

    /* Critical fuel -> navigate to nearest fuel pack or fuel station */
    if (pl->fuel.sum < my_data->fuel_l2 || my_data->current_tactic == TACTIC_HARVEST_REFUEL) {
	if (best_item_i >= 0) {
	    clpos_t wp = Smart_find_next_waypoint(pl, Obj[best_item_i]->pos);
	    Smart_fly_towards(pl, wp, 18.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	    return;
	}
	if (pl->fuel.sum < my_data->fuel_l3 && Num_fuels() > 0) {
	    int best_fs = NO_IND;
	    double best_fs_d = 1e19;
	    for (i = 0; i < Num_fuels(); i++) {
		fuel_t *fs = Fuel_by_index(i);
		double d;
		if (fs->fuel < 100.0)
		    continue;
		if (BIT(world->rules->mode, TEAM_PLAY) && options.teamFuel && fs->team != pl->team)
		    continue;
		d = Wrap_length(fs->pos.cx - pl->pos.cx, fs->pos.cy - pl->pos.cy) / CLICK;
		if (d < best_fs_d) {
		    best_fs_d = d;
		    best_fs = i;
		}
	    }
	    if (best_fs != NO_IND) {
		clpos_t wp = Smart_find_next_waypoint(pl, Fuel_by_index(best_fs)->pos);
		pl->fs = best_fs;
		SET_BIT(pl->used, USES_REFUEL);
		Smart_fly_towards(pl, wp, 14.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
		return;
	    }
	}
    }

    /* Evade tactic: break contact and run away from the closest enemy */
    if (my_data->current_tactic == TACTIC_EVADE_SURVIVE && best_enemy != NULL) {
	double ax = WRAP_DX(CLICK_TO_PIXEL(pl->pos.cx - best_enemy->pos.cx));
	double ay = WRAP_DY(CLICK_TO_PIXEL(pl->pos.cy - best_enemy->pos.cy));
	double alen = MAX(LENGTH(ax, ay), 1.0);
	clpos_t flee, wp;

	flee.cx = pl->pos.cx + PIXEL_TO_CLICK(ax / alen * 10.0 * BLOCK_SZ);
	flee.cy = pl->pos.cy + PIXEL_TO_CLICK(ay / alen * 10.0 * BLOCK_SZ);
	flee = World_wrap_clpos(flee);
	wp = Smart_find_next_waypoint(pl, flee);
	Smart_fly_towards(pl, wp, 22.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	return;
    }

    /* Capture the Flag objectives */
    if (my_data->current_tactic == TACTIC_CTF_OBJECTIVE
	|| BIT(pl->have, HAS_BALL)
	|| pl->ball != NULL) {
	if (Smart_handle_ctf(pl, repulse_vec, &aim_dir, &has_aim_dir))
	    return;
    }

    /* Proactive item collection when no enemy is in immediate close quarters or item is very valuable */
    if (best_item_i >= 0
	&& (best_enemy == NULL
	    || best_enemy_dist > 14.0 * BLOCK_SZ
	    || !best_enemy_los
	    || best_item_score > 2.0 * my_data->genes.aggression_bias)) {
	clpos_t wp = Smart_find_next_waypoint(pl, Obj[best_item_i]->pos);
	Smart_fly_towards(pl, wp, 18.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	return;
    }

    /* Active Enemy Hunting & Combat Maneuvering */
    if (best_enemy != NULL) {
	double pref_dist = my_data->genes.preferred_combat_dist * BLOCK_SZ;
	if (my_data->current_tactic == TACTIC_SNIPE_KITE)
	    pref_dist *= 1.35;
	else if (my_data->current_tactic == TACTIC_DOGFIGHT_AGGR)
	    pref_dist *= 0.60;

	if (best_enemy_dist > pref_dist * 1.20
	    || !best_enemy_los) {
	    /* Hunt enemy across the map */
	    clpos_t wp = Smart_find_next_waypoint(pl, best_enemy->pos);
	    Smart_fly_towards(pl, wp, 20.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	    return;
	} else {
	    /* High-speed Orbit / Strafe at preferred combat distance */
	    double rx = WRAP_DX(CLICK_TO_PIXEL(pl->pos.cx - best_enemy->pos.cx));
	    double ry = WRAP_DY(CLICK_TO_PIXEL(pl->pos.cy - best_enemy->pos.cy));
	    double rlen = MAX(LENGTH(rx, ry), 1.0);
	    double ux = rx / rlen;
	    double uy = ry / rlen;
	    double strafe_sign = ((pl->id & 1) == 0) ? 1.0 : -1.0;
	    double radial_err = pref_dist - rlen;
	    clpos_t orbit_target;

	    orbit_target.cx = pl->pos.cx + PIXEL_TO_CLICK(ux * radial_err + (-uy * strafe_sign * 5.0 * BLOCK_SZ));
	    orbit_target.cy = pl->pos.cy + PIXEL_TO_CLICK(uy * radial_err + ( ux * strafe_sign * 5.0 * BLOCK_SZ));
	    orbit_target = World_wrap_clpos(orbit_target);

	    Smart_fly_towards(pl, orbit_target, 16.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	    return;
	}
    }

    /* Fallback: CTF or Active Map Roaming Patrol */
    if (Smart_handle_ctf(pl, repulse_vec, &aim_dir, &has_aim_dir))
	return;

    if (--my_data->patrol_timer <= 0
	|| Wrap_length(my_data->patrol_pos.cx - pl->pos.cx,
		       my_data->patrol_pos.cy - pl->pos.cy) < 6.0 * BLOCK_CLICKS) {
	Smart_pick_new_patrol_target(pl);
    }
    {
	clpos_t wp = Smart_find_next_waypoint(pl, my_data->patrol_pos);
	Smart_fly_towards(pl, wp, 18.0, repulse_vec, NULL);
    }
}
