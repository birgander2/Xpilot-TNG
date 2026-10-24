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
#define SMART_BRAIN_VERSION	2U	/* 2: team roles & ball event rewards */
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

struct smart_route;

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

    /* Ball delivery state */
    int			throw_side;		/* Swing side (+1/-1) of a throw, 0 = none */
    bool		deliver_retreat;	/* Swinging out for another delivery run */
    clpos_t		deliver_pt;		/* Swing-out point next to the home treasure */
    int			ball_blocked;		/* Ticks the towed ball is behind a wall */
    bool		carrier_aim;		/* Towing with an enemy to shoot at close by */
    int			carrier_aim_dir;	/* Lead direction for that shot */
    bool		had_enemy_ball;		/* Steal of the towed enemy ball rewarded */

    /* Ball run home: one of the precomputed route variants */
    bool		route_valid;		/* route below is chosen */
    struct smart_route	*route;
    treasure_t		*route_from;		/* Enemy treasure the route starts at */
    treasure_t		*route_to;		/* Our treasure it leads to */
    int			route_cur;		/* Next route point */
    int			route_checked_at;	/* robot_count of the last enemy check */
    int			route_lost;		/* Ticks without sight of the next point */

    /* Team play */
    int			role;			/* enum SmartRole */
    int			escort_id;		/* Player to cover as ROLE_ESCORT */
} robot_smart_data_t;

/*
 * Team play roles in ball games, assigned per team about once per second
 * (Smart_assign_roles).  The RL tactic still picks the fighting style; the
 * role decides which objective a bot works on when it is not fighting.
 */
enum SmartRole {
    ROLE_NONE = 0,	/* no ball game: free play */
    ROLE_CARRIER,	/* towing a ball home */
    ROLE_ATTACKER,	/* fetch the enemy ball */
    ROLE_ESCORT,	/* cover a ball carrier or the attacker */
    ROLE_DEFENDER,	/* guard our treasure */
    ROLE_HUNTER		/* get our ball back: chase its carrier or pick it up */
};

#define ROLE_UPDATE_TICKS	12	/* robot ticks between role assignments */

typedef struct smart_team {
    int		enemy_carrier_id;	/* thief of our ball a teammate sees right now, else NO_ID */
} smart_team_t;

/*
 * What a team knows about its treasures is limited like a human team's: it
 * learns that its ball is gone by seeing the empty treasure or the ball
 * anywhere, and remembers that until it sees the ball at home again.
 */
#define MAX_KNOWN_TREASURES	64
static bool	TreasureAwayKnown[MAX_KNOWN_TREASURES];

static smart_team_t	SmartTeams[MAX_TEAMS];

/*
 * Q-learning rewards for ball game events (Robot_smart_game_event).  Scoring
 * a ball wins the round, so it outweighs everything else; bringing our own
 * ball back scores little and is rewarded less than a kill.
 */
#define REWARD_BALL_CASHED	60.0
#define REWARD_BALL_WON		25.0
#define REWARD_BALL_LOST	-20.0
#define REWARD_BALL_SAVED	4.0
#define REWARD_BALL_STOLEN	8.0	/* grabbed an enemy ball (shaping) */
#define REWARD_KILL_CARRIER	15.0	/* extra for killing a ball carrier */
#define REWARD_KILL_AT_HOME	5.0	/* extra for a kill near our treasure */

/*
 * BallArea outlines (treasure boxes), in pixels.  Ships die on contact
 * (treasureCollisionKills), so they are avoided exactly at polygon level with
 * no margin beyond the ship's own size; flying closely past them stays possible.
 */
#define BALLAREA_MAX_VERTS	64

typedef struct smart_ballarea {
    int		num;
    double	x[BALLAREA_MAX_VERTS];
    double	y[BALLAREA_MAX_VERTS];
    double	cx, cy, radius;		/* bounding circle */
} smart_ballarea_t;

static smart_ballarea_t	*BallAreas;
static int		NumBallAreas;
static const poly_t	*BallAreasPdata;
static int		BallAreasNumPolys = -1;

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
static void Robot_smart_game_event(player_t *pl, int event, player_t *other);
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
    Robot_smart_invite,
    Robot_smart_game_event
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

static bool Smart_team_ball_away(int team);
static bool Smart_believes_ball_home(int team, treasure_t *tr);

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
	    if (Smart_team_ball_away(pl->team))
		ctf_bin = 2;
	}
    }

    s = ((fuel_bin * RL_DIST_BINS + dist_bin) * RL_THREAT_BINS + threat_bin)
	* RL_CTF_BINS + ctf_bin;
    LIMIT(s, 0, NUM_RL_STATES - 1);
    return s;
}

/*
 * In ball games the ball matters, not survival or item harvesting: there
 * the RL only chooses between fighting styles and the plain objective, and
 * evading or harvesting only with low fuel.
 */
static bool Smart_tactic_allowed(int state, int tactic)
{
    int fuel_bin = state / (RL_CTF_BINS * RL_THREAT_BINS * RL_DIST_BINS);

    if (!BIT(world->rules->mode, TEAM_PLAY) || Num_treasures() <= 0)
	return true;
    if (tactic == TACTIC_EVADE_SURVIVE || tactic == TACTIC_HARVEST_REFUEL)
	return fuel_bin == 0;
    return true;
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

    /*
     * Ball scores are rewarded through Robot_smart_game_event(); cap the
     * plain score signal so its large ball swings are not counted twice.
     */
    {
	double score_reward = score_delta * 0.25;
	LIMIT(score_reward, -3.0, 3.0);
	reward += score_reward;
    }
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
	max_next_q = -1e30;
	for (a = 0; a < NUM_SMART_TACTICS; a++) {
	    if (Smart_tactic_allowed(next_state, a)
		&& SharedBrain.q_table[next_state][a] > max_next_q)
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
    best_a = -1;
    for (a = 0; a < NUM_SMART_TACTICS; a++) {
	if (Smart_tactic_allowed(next_state, a)
	    && (best_a < 0
		|| SharedBrain.q_table[next_state][a] > SharedBrain.q_table[next_state][best_a]))
	    best_a = a;
    }

    if (rfrac() < eps) {
	do
	    a = (int)(rfrac() * NUM_SMART_TACTICS) % NUM_SMART_TACTICS;
	while (!Smart_tactic_allowed(next_state, a));
	my_data->current_tactic = a;
    } else
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
    int group;
    group_t *gp;

    pos.cx = BLOCK_CENTER(bx);
    pos.cy = BLOCK_CENTER(by);
    group = shape_is_inside(pos.cx, pos.cy, BALL_BIT, NULL, &filled_wire, 0);
    if (group == NO_GROUP)
	return true;
    /* Ball targets are where the ball should go, not obstacles */
    gp = groupptr_by_id(group);
    return gp != NULL && gp->type == TREASURE;
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

/*
 * Block passability.  BallAreas count as solid for path planning only when
 * ballarea_solid is set; near-field avoidance handles them exactly instead
 * (Smart_avoid_ballareas), so blocks touching them are never cached.
 */
static bool Smart_block_test(player_t *pl, int bx, int by, bool ballarea_solid)
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
		} else if (gp != NULL && gp->type == TREASURE && gp->hitmask == BALL_BIT) {
		    special = true;
		    if (!ballarea_solid)
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

static bool Smart_block_passable(player_t *pl, int bx, int by)
{
    return Smart_block_test(pl, bx, by, true);
}

static int Smart_max_turn_per_tick(int eff_level);

/* (Re)build the BallArea outline list when the map's polygons change. */
static void Smart_ballareas_update(void)
{
    int i, k;

    if (BallAreasPdata == pdata && BallAreasNumPolys == num_polys)
	return;

    free(BallAreas);
    BallAreas = NULL;
    NumBallAreas = 0;
    BallAreasPdata = pdata;
    BallAreasNumPolys = num_polys;

    for (i = 0; i < num_polys; i++) {
	group_t *gp = groupptr_by_id(pdata[i].group);
	smart_ballarea_t *ba, *tmp;
	int *edges;
	double x, y, minx, maxx, miny, maxy;

	if (gp == NULL || gp->type != TREASURE || gp->hitmask != BALL_BIT)
	    continue;
	if (pdata[i].num_points < 3 || pdata[i].num_points > BALLAREA_MAX_VERTS)
	    continue;
	tmp = realloc(BallAreas, (size_t)(NumBallAreas + 1) * sizeof(*BallAreas));
	if (tmp == NULL)
	    return;
	BallAreas = tmp;
	ba = &BallAreas[NumBallAreas++];

	edges = edgeptr + pdata[i].edges;
	x = CLICK_TO_PIXEL(pdata[i].pos.cx);
	y = CLICK_TO_PIXEL(pdata[i].pos.cy);
	minx = maxx = x;
	miny = maxy = y;
	ba->num = pdata[i].num_points;
	for (k = 0; k < ba->num; k++) {
	    ba->x[k] = x;
	    ba->y[k] = y;
	    minx = MIN(minx, x);
	    maxx = MAX(maxx, x);
	    miny = MIN(miny, y);
	    maxy = MAX(maxy, y);
	    x += CLICK_TO_PIXEL(edges[2 * k]);
	    y += CLICK_TO_PIXEL(edges[2 * k + 1]);
	}
	ba->cx = (minx + maxx) / 2.0;
	ba->cy = (miny + maxy) / 2.0;
	ba->radius = LENGTH(maxx - minx, maxy - miny) / 2.0;
    }
}

/*
 * Signed distance (pixels) from (px, py) to a BallArea outline, negative
 * inside.  (*nx, *ny) is the unit normal pointing away from the box.
 */
static double Smart_ballarea_dist(const smart_ballarea_t *ba, double px, double py,
				  double *nx, double *ny)
{
    double best = 1e19, qbx = 0.0, qby = 0.0;
    bool inside = false;
    int k, n = ba->num;

    for (k = 0; k < n; k++) {
	double ax = WRAP_DX(ba->x[k] - px);
	double ay = WRAP_DY(ba->y[k] - py);
	double cx = WRAP_DX(ba->x[(k + 1) % n] - px);
	double cy = WRAP_DY(ba->y[(k + 1) % n] - py);
	double ex = cx - ax, ey = cy - ay;
	double len2 = ex * ex + ey * ey;
	double t = (len2 > 0.0) ? -(ax * ex + ay * ey) / len2 : 0.0;
	double qx, qy, d2;

	LIMIT(t, 0.0, 1.0);
	qx = ax + ex * t;
	qy = ay + ey * t;
	d2 = qx * qx + qy * qy;
	if (d2 < best) {
	    best = d2;
	    qbx = qx;
	    qby = qy;
	}
	/* Crossing test of the ray from the point along +x */
	if ((ay > 0.0) != (cy > 0.0) && ax - ay * ex / ey > 0.0)
	    inside = !inside;
    }
    best = sqrt(best);
    if (best > 1e-6) {
	*nx = -qbx / best;
	*ny = -qby / best;
    } else {
	*nx = 1.0;
	*ny = 0.0;
    }
    if (inside) {
	*nx = -*nx;
	*ny = -*ny;
	return -best;
    }
    return best;
}

/* Smallest distance from a point to any BallArea outline. */
static double Smart_ballarea_clearance(clpos_t pos)
{
    double px = CLICK_TO_PIXEL(pos.cx), py = CLICK_TO_PIXEL(pos.cy);
    double best = 1e19, nx, ny;
    int i;

    Smart_ballareas_update();
    for (i = 0; i < NumBallAreas; i++)
	best = MIN(best, Smart_ballarea_dist(&BallAreas[i], px, py, &nx, &ny));
    return best;
}

/* Can a ship hover at pos without touching walls or a BallArea? */
static bool Smart_spot_is_free(player_t *pl, clpos_t pos)
{
    blkpos_t blk = Clpos_to_blkpos(pos);

    return Smart_block_test(pl, blk.bx, blk.by, false)
	&& Smart_ballarea_clearance(pos) > SHIP_SZ + 4.0;
}

/*
 * Limit the desired velocity so the ship can always stop before touching a
 * BallArea.  Only the velocity component towards a box is limited, so close
 * fly-bys stay possible.  Returns true if the ship must brake right now.
 */
static bool Smart_avoid_ballareas(player_t *pl, double *des_vx, double *des_vy)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    double px = CLICK_TO_PIXEL(pl->pos.cx);
    double py = CLICK_TO_PIXEL(pl->pos.cy);
    /* Assume only part of full thrust for braking; a towed ball adds its mass. */
    double acc = 0.5 * MAX_PLAYER_POWER
		 / MAX(pl->mass + (BIT(pl->have, HAS_BALL) ? options.ballMass : 0.0), 1.0);
    /*
     * Time to turn the ship around before braking thrust takes effect.
     * Bots act once per game time unit, which is also the unit of velocity.
     */
    double react = (RES / 2.0) / Smart_max_turn_per_tick(Smart_get_effective_level(my_data))
		   + 1.0;
    double reach = 4.0 * BLOCK_SZ + pl->velocity * (react + 10.0);
    bool urgent = false;
    int i;

    Smart_ballareas_update();
    for (i = 0; i < NumBallAreas; i++) {
	const smart_ballarea_t *ba = &BallAreas[i];
	double nx, ny, dist, clear, v_allow, approach;

	if (LENGTH(WRAP_DX(ba->cx - px), WRAP_DY(ba->cy - py)) - ba->radius > reach)
	    continue;

	dist = Smart_ballarea_dist(ba, px, py, &nx, &ny);
	clear = dist - SHIP_SZ;
	if (clear > 0.0)
	    v_allow = acc * (sqrt(react * react + 2.0 * clear / acc) - react);
	else
	    v_allow = 0.25 * clear;	/* touching: push out */

	approach = -(*des_vx * nx + *des_vy * ny);
	if (approach > v_allow) {
	    *des_vx += nx * (approach - v_allow);
	    *des_vy += ny * (approach - v_allow);
	}
	approach = -(pl->vel.x * nx + pl->vel.y * ny);
	if (approach > 0.5 && approach > 0.8 * v_allow)
	    urgent = true;
    }
    return urgent;
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

/*
 * While towing a ball, blocks near enemies the bot sees cost extra, so it
 * avoids guarded ways.  Extra costs are never negative, so the A* heuristic
 * stays admissible.
 */
#define ROUTE_DANGER_RADIUS	6.0f	/* blocks */
#define ROUTE_DANGER		3.0f	/* extra cost factor right at an enemy */
#define ROUTE_MAX_DANGERS	8

static bool Smart_detect_ship(player_t *pl, player_t *ship);

typedef struct smart_route_ctx {
    int		num_dangers;
    float	dbx[ROUTE_MAX_DANGERS];
    float	dby[ROUTE_MAX_DANGERS];
} smart_route_ctx_t;

static void Smart_route_ctx_init(player_t *pl, smart_route_ctx_t *rc)
{
    int i;

    rc->num_dangers = 0;
    if (!BIT(pl->have, HAS_BALL))
	return;
    for (i = 0; i < NumPlayers && rc->num_dangers < ROUTE_MAX_DANGERS; i++) {
	player_t *ship = Player_by_index(i);

	if (ship->id == pl->id || !Player_is_active(ship) || Team_immune(pl->id, ship->id)
	    || !Smart_detect_ship(pl, ship))
	    continue;
	rc->dbx[rc->num_dangers] = (float)CLICK_TO_PIXEL(ship->pos.cx) / BLOCK_SZ;
	rc->dby[rc->num_dangers] = (float)CLICK_TO_PIXEL(ship->pos.cy) / BLOCK_SZ;
	rc->num_dangers++;
    }
}

/* Cost factor (>= 1) for entering block (bx, by). */
static float Smart_route_cost(const smart_route_ctx_t *rc, int bx, int by)
{
    float extra = 0.0f;
    int i;

    for (i = 0; i < rc->num_dangers; i++) {
	float dx = bx + 0.5f - rc->dbx[i];
	float dy = by + 0.5f - rc->dby[i];
	float d;

	if (BIT(world->rules->mode, WRAP_PLAY)) {
	    if (dx > world->x / 2) dx -= world->x;
	    if (dx < -world->x / 2) dx += world->x;
	    if (dy > world->y / 2) dy -= world->y;
	    if (dy < -world->y / 2) dy += world->y;
	}
	d = sqrtf(dx * dx + dy * dy);
	if (d < ROUTE_DANGER_RADIUS)
	    extra += ROUTE_DANGER * (1.0f - d / ROUTE_DANGER_RADIUS);
    }
    return 1.0f + extra;
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
    smart_route_ctx_t route;

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
    Smart_route_ctx_init(pl, &route);

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

	    ng = nodes[cur].g + cost8[dir] * Smart_route_cost(&route, nbx, nby);
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
 * Race Maps: Checkpoint Distance Fields
 * ========================================================================= */

/*
 * The bounded A* gets lost on long winding race tracks.  Race maps get one
 * distance field (in blocks) per checkpoint instead, computed once over the
 * whole map with the checkpoint's disk as the goal.  Blocks next to walls
 * and in strong gravity cost more, so the way keeps to the middle of the
 * track, where the wall repulsion does not fight it.
 */
static float	**RaceDist;
static uint8_t	*RaceClear;	/* blocks to the nearest wall block, up to 3 */
static int	RaceDistN, RaceDistW, RaceDistH;
static const void *RaceDistPdata;

typedef struct { float f; int c; } race_heap_t;

static void Smart_race_free(void)
{
    int i;

    for (i = 0; i < RaceDistN; i++)
	free(RaceDist[i]);
    free(RaceDist);
    free(RaceClear);
    RaceDist = NULL;
    RaceClear = NULL;
    RaceDistN = 0;
}

static const float *Smart_race_field(player_t *pl, int ci)
{
    static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    static const float cost8[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };
    int w = world->x, h = world->y, n = w * h, hn = 0, cap = n * 8 + 16, i;
    double r = options.checkpointRadius - 0.5;
    race_heap_t *heap;
    float *dist;
    blkpos_t cb;

    if (RaceDistPdata != pdata || RaceDistW != w || RaceDistH != h
	|| RaceDistN != world->NumChecks) {
	Smart_race_free();
	RaceDist = calloc(MAX(world->NumChecks, 1), sizeof(float *));
	if (RaceDist == NULL)
	    return NULL;
	RaceDistN = world->NumChecks;
	RaceDistW = w;
	RaceDistH = h;
	RaceDistPdata = pdata;
	/* wall distance of every block: paths keep to the middle of the track */
	RaceClear = malloc((size_t)n);
	if (RaceClear == NULL)
	    return NULL;
	for (i = 0; i < n; i++)
	    RaceClear[i] = Smart_block_passable(pl, i % w, i / w) ? 3 : 0;
	{
	    int pass, x, y, k;

	    for (pass = 1; pass <= 2; pass++) {
		for (i = 0; i < n; i++) {
		    if (RaceClear[i] <= pass)
			continue;
		    for (k = 0; k < 8; k++) {
			x = i % w + dx8[k];
			y = i / w + dy8[k];
			if (x >= 0 && x < w && y >= 0 && y < h
			    && RaceClear[y * w + x] == pass - 1) {
			    RaceClear[i] = (uint8_t)pass;
			    break;
			}
		    }
		}
	    }
	}
    }
    if (ci < 0 || ci >= RaceDistN)
	return NULL;
    if (RaceDist[ci] != NULL)
	return RaceDist[ci];

    dist = malloc(sizeof(float) * n);
    heap = malloc(sizeof(race_heap_t) * cap);
    if (dist == NULL || heap == NULL) {
	free(dist);
	free(heap);
	return NULL;
    }
    for (i = 0; i < n; i++)
	dist[i] = 1e30f;
    cb = Clpos_to_blkpos(Check_by_index(ci)->pos);

    /* the disk is the goal; blocks with a free centre in it are seeds */
    {
	int ri = (int)r + 1, bx, by;

	for (by = cb.by - ri; by <= cb.by + ri; by++) {
	    for (bx = cb.bx - ri; bx <= cb.bx + ri; bx++) {
		int x = bx, y = by;

		if (BIT(world->rules->mode, WRAP_PLAY)) {
		    x = WRAP_XBLOCK(x);
		    y = WRAP_YBLOCK(y);
		}
		if (x < 0 || x >= w || y < 0 || y >= h
		    || (bx - cb.bx) * (bx - cb.bx) + (by - cb.by) * (by - cb.by) > r * r
		    || !Smart_block_passable(pl, x, y))
		    continue;
		dist[y * w + x] = 0.0f;
		heap[hn].f = 0.0f;
		heap[hn].c = y * w + x;
		hn++;	/* all seeds equal: already a valid heap */
	    }
	}
    }
    /* Dijkstra */
    while (hn > 0) {
	race_heap_t top = heap[0], last = heap[--hn];
	int k = 0, d;

	while (hn > 0) {
	    int ch = 2 * k + 1;

	    if (ch >= hn)
		break;
	    if (ch + 1 < hn && heap[ch + 1].f < heap[ch].f)
		ch++;
	    if (heap[ch].f >= last.f)
		break;
	    heap[k] = heap[ch];
	    k = ch;
	}
	if (hn > 0)
	    heap[k] = last;
	if (top.f > dist[top.c])
	    continue;
	for (d = 0; d < 8; d++) {
	    static const float wall_pen[4] = { 1.0f, 3.0f, 1.6f, 1.0f };
	    int x = top.c % w + dx8[d], y = top.c / w + dy8[d], nc, j;
	    float nd;

	    if (BIT(world->rules->mode, WRAP_PLAY)) {
		x = WRAP_XBLOCK(x);
		y = WRAP_YBLOCK(y);
	    }
	    if (x < 0 || x >= w || y < 0 || y >= h)
		continue;
	    nc = y * w + x;
	    nd = top.f + cost8[d] * wall_pen[RaceClear[nc]];
	    /* strong gravity (bumpers in corners) is no place to fly through */
	    if (world->gravity != NULL) {
		double g = LENGTH(world->gravity[x][y].x, world->gravity[x][y].y);

		if (g > 0.8)
		    nd += cost8[d] * (float)MIN(g * 2.0, 12.0);
	    }
	    if (nd >= dist[nc] || !Smart_block_passable(pl, x, y))
		continue;
	    /* no diagonal corner cutting through walls */
	    if (d >= 4 && (!Smart_block_passable(pl, top.c % w + dx8[d], top.c / w)
			   || !Smart_block_passable(pl, top.c % w, top.c / w + dy8[d])))
		continue;
	    if (hn >= cap)
		continue;
	    dist[nc] = nd;
	    j = hn++;
	    while (j > 0 && heap[(j - 1) / 2].f > nd) {
		heap[j] = heap[(j - 1) / 2];
		j = (j - 1) / 2;
	    }
	    heap[j].f = nd;
	    heap[j].c = nc;
	}
    }
    free(heap);
    RaceDist[ci] = dist;
    return dist;
}

/* Is the straight way free of walls and of strong gravity? */
static bool Smart_race_line_ok(player_t *pl, clpos_t to)
{
    double dx = WRAP_DX(CLICK_TO_PIXEL(to.cx - pl->pos.cx));
    double dy = WRAP_DY(CLICK_TO_PIXEL(to.cy - pl->pos.cy));
    double len = LENGTH(dx, dy), d;

    if (!Smart_has_line_of_sight(pl, pl->pos, to, false))
	return false;
    if (world->gravity == NULL)
	return true;
    for (d = BLOCK_SZ; d < len; d += BLOCK_SZ * 0.5) {
	clpos_t p;
	vector_t g;

	p.cx = pl->pos.cx + PIXEL_TO_CLICK(dx * d / len);
	p.cy = pl->pos.cy + PIXEL_TO_CLICK(dy * d / len);
	p = World_wrap_clpos(p);
	g = World_gravity(p);
	if (LENGTH(g.x, g.y) > 0.8)
	    return false;
    }
    return true;
}

/*
 * Where to fly for the next checkpoint: the farthest point visible from
 * here along the way down its distance field.
 */
static clpos_t Smart_race_waypoint(player_t *pl)
{
    static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    const float *dist = Smart_race_field(pl, pl->check);
    clpos_t goal = Check_by_index(pl->check)->pos, wp;
    blkpos_t b = Clpos_to_blkpos(pl->pos);
    int w = world->x, h = world->y, cur, step, d;

    if (dist == NULL)
	return Smart_find_next_waypoint(pl, goal);
    cur = b.by * w + b.bx;
    if (dist[cur] >= 1e29f) {
	/* inside a wall block (ship overlaps it): start from the best neighbour */
	for (d = 0; d < 8; d++) {
	    int x = b.bx + dx8[d], y = b.by + dy8[d];

	    if (x >= 0 && x < w && y >= 0 && y < h && dist[y * w + x] < dist[cur])
		cur = y * w + x;
	}
	if (dist[cur] >= 1e29f)
	    return Smart_find_next_waypoint(pl, goal);
    }
    if (dist[cur] <= 0.0f)
	return goal;
    wp.cx = BLOCK_CENTER(cur % w);
    wp.cy = BLOCK_CENTER(cur / w);
    for (step = 0; step < 16 && dist[cur] > 0.0f; step++) {
	int next = cur;
	clpos_t p;

	for (d = 0; d < 8; d++) {
	    int x = cur % w + dx8[d], y = cur / w + dy8[d];

	    if (BIT(world->rules->mode, WRAP_PLAY)) {
		x = WRAP_XBLOCK(x);
		y = WRAP_YBLOCK(y);
	    }
	    if (x >= 0 && x < w && y >= 0 && y < h && dist[y * w + x] < dist[next])
		next = y * w + x;
	}
	if (next == cur)
	    break;
	p.cx = BLOCK_CENTER(next % w);
	p.cy = BLOCK_CENTER(next / w);
	if (step > 0 && !Smart_race_line_ok(pl, p))
	    break;
	wp = p;
	cur = next;
    }
    return wp;
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

	    /* BallAreas are handled exactly by Smart_avoid_ballareas() */
	    if (!Smart_block_test(pl, bx, by, false)) {
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

		/* Shield against a hard wall hit; walls are no RL "threat" */
		if (d < SHIP_SZ * 2.2 && vel_proj > 2.0
		    && options.maxShieldedWallBounceSpeed > options.maxUnshieldedWallBounceSpeed)
		    *need_shield_now = true;
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
 * Ball Towing Flight Controller
 * ========================================================================= */

/* Point at dist pixels from pos in direction dir (dir units). */
static clpos_t Smart_offset_pos(clpos_t pos, int dir, double dist)
{
    pos.cx += PIXEL_TO_CLICK(tcos(MOD2(dir, RES)) * dist);
    pos.cy += PIXEL_TO_CLICK(tsin(MOD2(dir, RES)) * dist);
    return World_wrap_clpos(pos);
}

/* The ball we are towing, or NULL. */
static ballobject_t *Smart_carried_ball(player_t *pl)
{
    int i;

    if (!BIT(pl->have, HAS_BALL))
	return NULL;
    for (i = 0; i < NumObjs; i++) {
	if (Obj[i]->type == OBJ_BALL && Obj[i]->id == pl->id)
	    return BALL_PTR(Obj[i]);
    }
    return NULL;
}

/*
 * Velocity correction keeping the towed ball off walls: rays from the ball
 * along its motion; the strongest needed braking (with the pair's real
 * deceleration acc) is returned.
 */
static vector_t Smart_ball_wall_brake(ballobject_t *ball, double acc)
{
    vector_t brake = { 0.0, 0.0 };
    double bx = CLICK_TO_PIXEL(ball->pos.cx);
    double by = CLICK_TO_PIXEL(ball->pos.cy);
    double v = LENGTH(ball->vel.x, ball->vel.y);
    double reach = v * v / (2.0 * acc) + v * 2.0 + BLOCK_SZ;
    double worst = 0.0;
    int i;

    LIMIT(reach, 2.0 * BLOCK_SZ, 12.0 * BLOCK_SZ);
    for (i = 0; i < 16; i++) {
	int ray_dir = (i * RES) / 16;
	double c = tcos(ray_dir), s = tsin(ray_dir);
	double vel_proj = ball->vel.x * c + ball->vel.y * s;
	double d;

	if (vel_proj <= 0.5)
	    continue;
	for (d = BLOCK_SZ * 0.45; d <= reach; d += BLOCK_SZ * 0.45) {
	    int x = (int)floor((bx + c * d) / BLOCK_SZ);
	    int y = (int)floor((by + s * d) / BLOCK_SZ);
	    bool blocked;

	    if (BIT(world->rules->mode, WRAP_PLAY)) {
		x = WRAP_XBLOCK(x);
		y = WRAP_YBLOCK(y);
	    }
	    blocked = (x < 0 || x >= world->x || y < 0 || y >= world->y
		       || !Smart_empty_space_for_ball(x, y));
	    if (blocked) {
		double room = MAX(d - 0.5 * BLOCK_SZ - options.ballRadius, 0.0);
		double excess = vel_proj - sqrt(2.0 * acc * room);

		if (excess > worst) {
		    worst = excess;
		    brake.x = -c * excess;
		    brake.y = -s * excess;
		}
		break;
	    }
	}
    }
    return brake;
}

/* Is the ship out of the released ball's path? */
static bool Smart_clear_of_ball_path(player_t *pl, ballobject_t *ball)
{
    double v = LENGTH(ball->vel.x, ball->vel.y);
    double rx = WRAP_DX(CLICK_TO_PIXEL(pl->pos.cx - ball->pos.cx));
    double ry = WRAP_DY(CLICK_TO_PIXEL(pl->pos.cy - ball->pos.cy));
    double ux, uy;

    if (v < 1.0)
	return false;
    ux = ball->vel.x / v;
    uy = ball->vel.y / v;
    return rx * ux + ry * uy < 0.0
	|| fabs(rx * uy - ry * ux) > SHIP_SZ + options.ballRadius + 8.0;
}

/*
 * Flight controller while towing a ball.  The ball is heavier than the ship
 * and hangs on a stiff, barely damped spring, so steering the ship alone
 * makes the pair jerk and oscillate.  Instead:
 * - the pair's centre of mass is flown, with the acceleration the pair
 *   really has (thrust / total mass) and slowing down before turns,
 * - the ship pulls away whenever the connector compresses, damping the
 *   spring oscillation (it can only thrust forward, so only this half),
 * - the pair's orientation is held: normally the ship leads in the direction
 *   of travel.  With throw_at set it swings out beside the ball's line to
 *   the target, so the ball can be released past the ship.
 */
static void Smart_fly_with_ball(player_t *pl, ballobject_t *ball, clpos_t target_pos,
				double desired_speed, vector_t repulse_vec,
				const clpos_t *throw_at)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    int eff_level = Smart_get_effective_level(my_data);
    double ms = MAX(pl->mass, 1.0), mb = MAX(ball->mass, 1.0), m = ms + mb;
    double acc = MAX_PLAYER_POWER / m;
    double rx = WRAP_DX(CLICK_TO_PIXEL(pl->pos.cx - ball->pos.cx));
    double ry = WRAP_DY(CLICK_TO_PIXEL(pl->pos.cy - ball->pos.cy));
    double len = MAX(LENGTH(rx, ry), 1.0);
    double dx = rx / len, dy = ry / len;	/* ball -> ship */
    double px = -dy, py = dx;			/* D turned +90 degrees */
    double vcx = (ms * pl->vel.x + mb * ball->vel.x) / m;
    double vcy = (ms * pl->vel.y + mb * ball->vel.y) / m;
    double vrx = pl->vel.x - ball->vel.x, vry = pl->vel.y - ball->vel.y;
    double v_along = vrx * dx + vry * dy, v_tan = vrx * px + vry * py;
    double tx, ty, dist, speed, des_x = 0.0, des_y = 0.0;
    double ux, uy, ulen, phi, phi_t = 0.0, v_t_des, f_tan;
    double fx, fy, sx, sy, proj, ex, ey, e_par, des_len, cl_x, cl_y;
    bool emergency;
    vector_t brake;

    speed = desired_speed * my_data->genes.speed_factor;
    LIMIT(speed, 4.0, 14.0 + 14.0 * eff_level / 100.0);

    if (throw_at != NULL) {
	/* Fly the centre of mass parallel to the ball's line to the target */
	tx = WRAP_DX(CLICK_TO_PIXEL(throw_at->cx - ball->pos.cx));
	ty = WRAP_DY(CLICK_TO_PIXEL(throw_at->cy - ball->pos.cy));
	dist = LENGTH(tx, ty);
    } else {
	double v_end = 3.0;

	/* Target relative to the centre of mass */
	tx = WRAP_DX(CLICK_TO_PIXEL(target_pos.cx - pl->pos.cx)) + rx * mb / m;
	ty = WRAP_DY(CLICK_TO_PIXEL(target_pos.cy - pl->pos.cy)) + ry * mb / m;
	dist = LENGTH(tx, ty);

	/*
	 * Following a path: plan the speed backwards over all remaining
	 * waypoints, so straight runs are flown fast and the pair still has
	 * room to slow down for every later turn.
	 */
	if (my_data->cur_waypoint < my_data->num_waypoints
	    && my_data->waypoints[my_data->cur_waypoint].cx == target_pos.cx
	    && my_data->waypoints[my_data->cur_waypoint].cy == target_pos.cy) {
	    double in_x = tx, in_y = ty;	/* leg arriving at waypoint k */
	    int k;

	    v_end = 0.5 * speed;		/* end of the planned path */
	    for (k = my_data->num_waypoints - 1; k > my_data->cur_waypoint; k--) {
		clpos_t a = my_data->waypoints[k - 1], b = my_data->waypoints[k];
		double lx = WRAP_DX(CLICK_TO_PIXEL(b.cx - a.cx));
		double ly = WRAP_DY(CLICK_TO_PIXEL(b.cy - a.cy));

		v_end = sqrt(v_end * v_end + 1.2 * acc * LENGTH(lx, ly));
		/* Speed allowed at waypoint k - 1, turning onto this leg */
		if (k - 1 == my_data->cur_waypoint) {
		    in_x = tx;
		    in_y = ty;
		} else {
		    clpos_t z = my_data->waypoints[k - 2];
		    in_x = WRAP_DX(CLICK_TO_PIXEL(a.cx - z.cx));
		    in_y = WRAP_DY(CLICK_TO_PIXEL(a.cy - z.cy));
		}
		if (LENGTH(in_x, in_y) > 1.0 && LENGTH(lx, ly) > 1.0) {
		    double c = (in_x * lx + in_y * ly) / (LENGTH(in_x, in_y) * LENGTH(lx, ly));
		    v_end = MIN(v_end, MAX(3.0, speed * (1.0 + c) / 2.0));
		}
	    }
	}
	speed = MIN(speed, sqrt(v_end * v_end + 1.2 * acc * dist));
    }
    if (dist > 1.0) {
	des_x = tx / dist * speed;
	des_y = ty / dist * speed;
    }

    /* Keep ship and ball off walls, ship off BallAreas */
    des_x += 0.5 * repulse_vec.x;
    des_y += 0.5 * repulse_vec.y;
    brake = Smart_ball_wall_brake(ball, 0.6 * acc);
    des_x += brake.x;
    des_y += brake.y;
    cl_x = des_x;
    cl_y = des_y;
    Smart_avoid_ballareas(pl, &des_x, &des_y);
    emergency = LENGTH(brake.x, brake.y) > 0.5
		|| LENGTH(des_x - cl_x, des_y - cl_y) > 0.5;

    /*
     * Centre of mass velocity control.  Braking means turning towards the
     * ball, so a small overspeed along the track is simply coasted out.
     */
    ex = des_x - vcx;
    ey = des_y - vcy;
    des_len = LENGTH(des_x, des_y);
    if (des_len > 0.5) {
	e_par = (ex * des_x + ey * des_y) / des_len;
	if (e_par < 0.0) {
	    if (-e_par < 0.25 * des_len + 1.5) {
		ex -= e_par * des_x / des_len;
		ey -= e_par * des_y / des_len;
	    } else
		emergency = true;
	}
    }
    fx = m * 0.6 * ex;
    fy = m * 0.6 * ey;

    /* Orientation control: angle of ball->ship relative to the reference */
    if (throw_at != NULL) {
	ux = tx;
	uy = ty;
    } else {
	ux = des_x;
	uy = des_y;
    }
    ulen = LENGTH(ux, uy);
    if (ulen > 0.5) {
	ux /= ulen;
	uy /= ulen;
	phi = atan2(ux * dy - uy * dx, ux * dx + uy * dy);
	if (throw_at != NULL) {
	    if (my_data->throw_side == 0) {
		clpos_t side = Smart_offset_pos(ball->pos,
						(int)(findDir(ux, uy) + 0.5) + RES / 4, len);
		my_data->throw_side = (phi > 0.2 || (phi > -0.2 && Smart_spot_is_free(pl, side)))
				      ? 1 : -1;
	    }
	    phi_t = my_data->throw_side * PI / 2.0;
	}
	phi = phi_t - phi;
	if (phi > PI)
	    phi -= 2.0 * PI;
	else if (phi < -PI)
	    phi += 2.0 * PI;
	v_t_des = 0.8 * phi * len;
	LIMIT(v_t_des, -8.0, 8.0);
	f_tan = ms * 0.5 * (v_t_des - v_tan);
	fx += px * f_tan;
	fy += py * f_tan;
    }

    /*
     * Keep the ball behind: unless we must brake, never push towards the
     * ball.  A ship that ended up behind it swings around it via the
     * orientation control instead of ramming it.
     */
    if (throw_at == NULL && !emergency) {
	double f_par = fx * dx + fy * dy;

	if (f_par < 0.0) {
	    fx -= f_par * dx;
	    fy -= f_par * dy;
	}
    }

    /* Damp the spring: pull away while the connector is being compressed */
    if (v_along < 0.0) {
	double mu = ms * mb / m;
	double omega = sqrt(options.ballConnectorSpringConstant
			    / MAX(options.ballConnectorLength, 1.0) / mu);
	double c_d = 2.0 * 0.6 * omega * ms * (0.4 + 0.6 * eff_level / 100.0);

	fx += dx * -c_d * v_along;
	fy += dy * -c_d * v_along;
    }

    /* Emergency: the ship itself approaching a BallArea too fast */
    sx = pl->vel.x;
    sy = pl->vel.y;
    if (Smart_avoid_ballareas(pl, &sx, &sy)) {
	fx += ms * 2.0 * (sx - pl->vel.x);
	fy += ms * 2.0 * (sy - pl->vel.y);
	emergency = true;
    }

    /*
     * Defend the ball: while the pair coasts (little thrust needed), turn
     * to an enemy close by so the cannon can fire, as human carriers do.
     * Flying has priority again as soon as real thrust is needed.
     */
    if (my_data->carrier_aim && throw_at == NULL && !emergency
	&& LENGTH(fx, fy) < 0.35 * MAX_PLAYER_POWER) {
	Smart_steer_to_dir(pl, my_data->carrier_aim_dir);
	Thrust(pl, false);
	return;
    }

    if (LENGTH(fx, fy) > 0.5 * MIN_PLAYER_POWER)
	Smart_steer_to_dir(pl, MOD2((int)(findDir(fx, fy) + 0.5), RES));

    proj = fx * tcos(pl->dir) + fy * tsin(pl->dir);
    if (proj > 0.5 * MIN_PLAYER_POWER && !my_data->cut_thrust) {
	pl->power = proj;
	LIMIT(pl->power, MIN_PLAYER_POWER, MAX_PLAYER_POWER);
	Thrust(pl, true);
    } else
	Thrust(pl, false);
}

/* =========================================================================
 * Desired Velocity Vector Flight Controller (PD Thrust & Direction)
 * ========================================================================= */

static void Smart_fly_towards(player_t *pl, clpos_t target_pos, double desired_speed,
			      vector_t repulse_vec, int *aim_override_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    ballobject_t *towed = Smart_carried_ball(pl);
    double dx = WRAP_DX(CLICK_TO_PIXEL(target_pos.cx - pl->pos.cx));
    double dy = WRAP_DY(CLICK_TO_PIXEL(target_pos.cy - pl->pos.cy));
    double dist = LENGTH(dx, dy);
    double des_vx = 0.0, des_vy = 0.0;
    double err_vx, err_vy, err_mag;
    int thrust_dir;
    bool must_brake;

    if (towed != NULL) {
	Smart_fly_with_ball(pl, towed, target_pos, desired_speed, repulse_vec, NULL);
	return;
    }

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

    /* Never approach a deadly BallArea faster than we can stop */
    must_brake = Smart_avoid_ballareas(pl, &des_vx, &des_vy);

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
	if (must_brake || repulse_mag > 10.0 || pl->velocity < 3.5
	    || (err_mag > 9.0 && (my_data->robot_count & 1))) {
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
	    pl->power = (err_mag > 5.0 || must_brake) ? MAX_PLAYER_POWER : (MAX_PLAYER_POWER * 0.75);
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

/*
 * Will the released ball hit the treasure's ball target on its current
 * course?  Balls fly through BallAreas, so only walls matter.  Low-skill
 * bots accept a sloppier course.
 */
static bool Smart_ball_on_target(player_t *pl, ballobject_t *ball, treasure_t *tr)
{
    int eff_level = Smart_get_effective_level(Robot_smart_get_data(pl));
    double rx = WRAP_DX(CLICK_TO_PIXEL(tr->pos.cx - ball->pos.cx));
    double ry = WRAP_DY(CLICK_TO_PIXEL(tr->pos.cy - ball->pos.cy));
    double vx = ball->vel.x, vy = ball->vel.y;
    double v2 = vx * vx + vy * vy;
    double t, miss, tol;

    if (v2 < sqr(4.0) || LENGTH(rx, ry) > 14.0 * BLOCK_SZ)
	return false;
    t = (rx * vx + ry * vy) / v2;	/* time of closest approach */
    if (t <= 0.0)
	return false;
    miss = LENGTH(rx - vx * t, ry - vy * t);
    tol = options.ballRadius + 8.0 + (100 - eff_level) * 0.2;
    return miss < tol && Smart_has_line_of_sight(pl, ball->pos, tr->pos, true);
}

/*
 * Point to swing out to after a delivery run without release: off to the
 * side of the treasure so the next run comes in straight, with the ball
 * having a clear line to the target.
 */
static clpos_t Smart_pick_swing_out_point(player_t *pl, treasure_t *tr)
{
    static const int offs[] = { RES / 4, -RES / 4, RES / 6, -RES / 6,
				RES / 3, -RES / 3, 0 };
    int base = (int)(Wrap_cfindDir(pl->pos.cx - tr->pos.cx, pl->pos.cy - tr->pos.cy) + 0.5);
    double dist;
    int k;

    for (dist = 8.0 * BLOCK_SZ; dist >= 5.0 * BLOCK_SZ; dist -= 3.0 * BLOCK_SZ) {
	for (k = 0; k < (int)NELEM(offs); k++) {
	    clpos_t p = Smart_offset_pos(tr->pos, base + offs[k], dist);
	    if (Smart_spot_is_free(pl, p)
		&& Smart_has_line_of_sight(pl, p, tr->pos, true))
		return p;
	}
    }
    return Smart_offset_pos(tr->pos, base, 6.0 * BLOCK_SZ);
}

/*
 * Switch the connector on for a deliberate grab only: the ball must be
 * within reach with a clear line for it, so it is never grabbed through a
 * wall (ship and ball then end up stuck on both sides of it).
 */
static bool Smart_use_connector(player_t *pl, clpos_t ball_pos)
{
    double d = Wrap_length(ball_pos.cx - pl->pos.cx, ball_pos.cy - pl->pos.cy) / CLICK;

    if (d > options.ballConnectorLength
	|| !Smart_has_line_of_sight(pl, pl->pos, ball_pos, true))
	return false;
    SET_BIT(pl->used, USES_CONNECTOR);
    return true;
}

/* Position of the ball resting in (or loose from) treasure tr, if any. */
static bool Smart_free_ball_pos(treasure_t *tr, clpos_t *pos)
{
    int i;

    for (i = 0; i < NumObjs; i++) {
	if (Obj[i]->type == OBJ_BALL && Obj[i]->id == NO_ID
	    && BALL_PTR(Obj[i])->ball_treasure == tr) {
	    *pos = Obj[i]->pos;
	    return true;
	}
    }
    return false;
}

/* =========================================================================
 * Ball Run Routes
 * ========================================================================= */

/*
 * For each pair "enemy treasure -> our treasure" four clearly different
 * routes are computed once per map.  A route's quality is its estimated
 * flight time with a towed ball: corners count heavily, because the heavy
 * pair has to slow down for them.  Before grabbing the ball, a bot picks
 * one at random, weighted by quality and avoiding routes with enemies on
 * them; on the way, route points are pushed away from enemies and the
 * local path finder steers around them.
 *
 * Routes are searched on a grid of half blocks with real wall clearance.
 * Variants come from penalising a corridor around every route found;
 * routes running mostly inside the corridor of an accepted one are dropped.
 */
#define SR_VARIANTS		4
#define SR_MAX_POINTS		48
#define SR_MAX_TREASURES	8
#define SR_CELL			(BLOCK_CLICKS / 2)	/* grid cell size in clicks */
#define SR_CLEARANCE		20.0	/* px from walls */
#define SR_CORRIDOR		8	/* cells around a route */
#define SR_PENALTY		2.0f	/* extra cost factor inside earlier corridors */
#define SR_MAX_OVERLAP		0.4	/* share of a route inside accepted corridors */
#define SR_MAX_TRIES		24
#define SR_SPEED		22.0	/* towing speed for the time estimate */
#define SR_SIMPLIFY		20.0	/* px tolerance when straightening */

typedef struct smart_route {
    int		num;
    clpos_t	pts[SR_MAX_POINTS];
    double	time;		/* estimated flight time incl. corners */
    double	length;		/* px */
    int		turns;		/* corners of more than 25 degrees */
} smart_route_t;

typedef struct smart_route_set {
    bool		computed;
    int			num;
    smart_route_t	r[SR_VARIANTS];
} smart_route_set_t;

static smart_route_set_t	RouteSets[SR_MAX_TREASURES][SR_MAX_TREASURES];
static const poly_t		*RouteSetsPdata;
static int			RouteSetsNumPolys = -1;

static int	SrW, SrH;
static uint8_t	*SrFree;
static float	*SrPenalty;
static uint8_t	*SrCorr;
static float	*SrDist;
static int32_t	*SrPrev;
static float	*SrWall;
typedef struct sr_heap_entry { float f; int32_t c; } sr_heap_entry_t;
static sr_heap_entry_t	*SrHeap;
static int		SrHeapN, SrHeapCap;

static void Smart_sr_push(float f, int32_t c)
{
    int i;

    if (SrHeapN >= SrHeapCap)
	return;
    i = SrHeapN++;
    while (i > 0) {
	int parent = (i - 1) / 2;
	if (SrHeap[parent].f <= f)
	    break;
	SrHeap[i] = SrHeap[parent];
	i = parent;
    }
    SrHeap[i].f = f;
    SrHeap[i].c = c;
}

static sr_heap_entry_t Smart_sr_pop(void)
{
    sr_heap_entry_t top = SrHeap[0], last = SrHeap[--SrHeapN];
    int i = 0;

    while (SrHeapN > 0) {
	int child = 2 * i + 1;
	if (child >= SrHeapN)
	    break;
	if (child + 1 < SrHeapN && SrHeap[child + 1].f < SrHeap[child].f)
	    child++;
	if (SrHeap[child].f >= last.f)
	    break;
	SrHeap[i] = SrHeap[child];
	i = child;
    }
    if (SrHeapN > 0)
	SrHeap[i] = last;
    return top;
}

static clpos_t Smart_sr_pos(int c)
{
    clpos_t pos;

    pos.cx = (c % SrW) * SR_CELL + SR_CELL / 2;
    pos.cy = (c / SrW) * SR_CELL + SR_CELL / 2;
    return pos;
}

static int Smart_sr_cell(clpos_t pos)
{
    int x = pos.cx / SR_CELL, y = pos.cy / SR_CELL;

    LIMIT(x, 0, SrW - 1);
    LIMIT(y, 0, SrH - 1);
    return y * SrW + x;
}

static bool Smart_sr_neighbour(int c, int dx, int dy, int *n)
{
    int x = c % SrW + dx, y = c / SrW + dy;

    if (BIT(world->rules->mode, WRAP_PLAY)) {
	x = (x + SrW) % SrW;
	y = (y + SrH) % SrH;
    } else if (x < 0 || x >= SrW || y < 0 || y >= SrH)
	return false;
    *n = y * SrW + x;
    return true;
}

static const int	sr_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int	sr_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

/* Grid of free cells (with wall clearance); false if it cannot be built. */
static bool Smart_sr_build_grid(void)
{
    float cell_px = (float)SR_CELL / CLICK;
    int cells, c, k;

    SrW = world->cwidth / SR_CELL;
    SrH = world->cheight / SR_CELL;
    cells = SrW * SrH;
    free(SrFree); free(SrPenalty); free(SrCorr); free(SrDist);
    free(SrPrev); free(SrWall); free(SrHeap);
    SrFree = malloc((size_t)cells);
    SrPenalty = malloc((size_t)cells * sizeof(*SrPenalty));
    SrCorr = malloc((size_t)cells);
    SrDist = malloc((size_t)cells * sizeof(*SrDist));
    SrPrev = malloc((size_t)cells * sizeof(*SrPrev));
    SrWall = malloc((size_t)cells * sizeof(*SrWall));
    SrHeapCap = cells * 8;
    SrHeap = malloc((size_t)SrHeapCap * sizeof(*SrHeap));
    if (!SrFree || !SrPenalty || !SrCorr || !SrDist || !SrPrev || !SrWall || !SrHeap)
	return false;

    /* Distance to the nearest wall (BallAreas count as walls) */
    SrHeapN = 0;
    for (c = 0; c < cells; c++) {
	clpos_t pos = Smart_sr_pos(c);

	if (is_inside(pos.cx, pos.cy, NONBALL_BIT, NULL) != NO_GROUP) {
	    SrWall[c] = 0.0f;
	    Smart_sr_push(0.0f, c);
	} else
	    SrWall[c] = 1e30f;
    }
    while (SrHeapN > 0) {
	sr_heap_entry_t e = Smart_sr_pop();

	if (e.f > SrWall[e.c] || e.f > 2.0f * SR_CLEARANCE)
	    continue;
	for (k = 0; k < 8; k++) {
	    int n;
	    float d = e.f + cell_px * (k < 4 ? 1.0f : 1.414f);

	    if (Smart_sr_neighbour(e.c, sr_dx[k], sr_dy[k], &n) && d < SrWall[n]) {
		SrWall[n] = d;
		Smart_sr_push(d, n);
	    }
	}
    }
    for (c = 0; c < cells; c++)
	SrFree[c] = SrWall[c] > SR_CLEARANCE;
    return true;
}

/* Cheapest grid path from near 'from' to near 'to'; returns its length. */
static int Smart_sr_search(clpos_t from, clpos_t to, int *path, int max_len)
{
    float cell_px = (float)SR_CELL / CLICK;
    int cells = SrW * SrH, c, k, goal = -1, len = 0, r;
    int fc = Smart_sr_cell(from), tc = Smart_sr_cell(to);
    int src_r = (int)(0.9 * options.ballConnectorLength / cell_px);
    int dst_r = (int)(3.0 * BLOCK_SZ / cell_px);

    for (c = 0; c < cells; c++)
	SrDist[c] = 1e30f;
    SrHeapN = 0;
    for (r = -src_r; r <= src_r; r++) {
	for (k = -src_r; k <= src_r; k++) {
	    int n = fc;

	    if (k * k + r * r > src_r * src_r)
		continue;
	    if (Smart_sr_neighbour(fc, k, r, &n) && SrFree[n]) {
		SrDist[n] = 0.0f;
		SrPrev[n] = -1;
		Smart_sr_push(0.0f, n);
	    }
	}
    }
    while (SrHeapN > 0) {
	sr_heap_entry_t e = Smart_sr_pop();
	int ex = e.c % SrW - tc % SrW, ey = e.c / SrW - tc / SrW;

	if (e.f > SrDist[e.c])
	    continue;
	if (BIT(world->rules->mode, WRAP_PLAY)) {
	    if (ex > SrW / 2) ex -= SrW;
	    if (ex < -SrW / 2) ex += SrW;
	    if (ey > SrH / 2) ey -= SrH;
	    if (ey < -SrH / 2) ey += SrH;
	}
	if (ex * ex + ey * ey <= dst_r * dst_r) {
	    goal = e.c;
	    break;
	}
	for (k = 0; k < 8; k++) {
	    int n, a, b;
	    float d;

	    if (!Smart_sr_neighbour(e.c, sr_dx[k], sr_dy[k], &n) || !SrFree[n])
		continue;
	    if (k >= 4 && (!Smart_sr_neighbour(e.c, sr_dx[k], 0, &a) || !SrFree[a]
			   || !Smart_sr_neighbour(e.c, 0, sr_dy[k], &b) || !SrFree[b]))
		continue;
	    d = e.f + (k < 4 ? 1.0f : 1.414f) * (1.0f + SrPenalty[n]);
	    if (d < SrDist[n]) {
		SrDist[n] = d;
		SrPrev[n] = e.c;
		Smart_sr_push(d, n);
	    }
	}
    }
    if (goal < 0)
	return 0;
    for (c = goal; c >= 0 && len < max_len; c = SrPrev[c])
	path[len++] = c;
    for (k = 0; k < len / 2; k++) {
	int t = path[k];
	path[k] = path[len - 1 - k];
	path[len - 1 - k] = t;
    }
    return len;
}

/* Douglas-Peucker on unwrapped pixel points; marks kept points. */
static void Smart_sr_simplify(const double *x, const double *y, int a, int b,
			      double tol, uint8_t *keep)
{
    double dx = x[b] - x[a], dy = y[b] - y[a], l = MAX(LENGTH(dx, dy), 1e-9);
    double best = 0.0;
    int i, bi = -1;

    for (i = a + 1; i < b; i++) {
	double d = fabs(dx * (y[a] - y[i]) - (x[a] - x[i]) * dy) / l;
	if (d > best) {
	    best = d;
	    bi = i;
	}
    }
    if (bi < 0 || best <= tol)
	return;
    keep[bi] = 1;
    Smart_sr_simplify(x, y, a, bi, tol, keep);
    Smart_sr_simplify(x, y, bi, b, tol, keep);
}

/* Turn a grid path into a straightened route and rate it. */
static bool Smart_sr_make_route(const int *path, int len, smart_route_t *r)
{
    static double x[4096], y[4096];
    static uint8_t keep[4096];
    double acc = 0.6 * MAX_PLAYER_POWER / MAX(options.shipMass + options.ballMass, 1.0);
    double tol = SR_SIMPLIFY, pdx = 0.0, pdy = 0.0, plen = 0.0;
    int i, n;

    if (len < 2 || len > 4096)
	return false;
    for (i = 0; i < len; i++) {
	clpos_t pos = Smart_sr_pos(path[i]);
	x[i] = CLICK_TO_PIXEL(pos.cx);
	y[i] = CLICK_TO_PIXEL(pos.cy);
	if (i > 0) {	/* unwrap across the map edge */
	    x[i] = x[i - 1] + WRAP_DX(x[i] - x[i - 1]);
	    y[i] = y[i - 1] + WRAP_DY(y[i] - y[i - 1]);
	}
    }
    do {
	memset(keep, 0, (size_t)len);
	keep[0] = keep[len - 1] = 1;
	Smart_sr_simplify(x, y, 0, len - 1, tol, keep);
	for (n = 0, i = 0; i < len; i++)
	    n += keep[i];
	tol *= 1.5;
    } while (n > SR_MAX_POINTS);

    r->num = 0;
    r->time = r->length = 0.0;
    r->turns = 0;
    for (i = 0; i < len; i++) {
	double dx, dy, l;

	if (!keep[i])
	    continue;
	if (r->num > 0) {
	    clpos_t prev = r->pts[r->num - 1];
	    double px = x[i] - WRAP_DX(x[i] - CLICK_TO_PIXEL(prev.cx));
	    double py = y[i] - WRAP_DY(y[i] - CLICK_TO_PIXEL(prev.cy));

	    dx = x[i] - px;
	    dy = y[i] - py;
	    l = LENGTH(dx, dy);
	    r->length += l;
	    r->time += l / SR_SPEED;
	    if (plen > 1.0 && l > 1.0) {
		double c = (dx * pdx + dy * pdy) / (l * plen);
		double vc = MAX(3.0, SR_SPEED * (1.0 + c) / 2.0);

		r->time += (SR_SPEED - vc) * (SR_SPEED - vc) / (acc * SR_SPEED);
		if (c < cos(25.0 * PI / 180.0))
		    r->turns++;
	    }
	    pdx = dx;
	    pdy = dy;
	    plen = l;
	}
	r->pts[r->num].cx = PIXEL_TO_CLICK(x[i]);
	r->pts[r->num].cy = PIXEL_TO_CLICK(y[i]);
	r->pts[r->num] = World_wrap_clpos(r->pts[r->num]);
	r->num++;
    }
    return r->num >= 2;
}

/* Compute the route variants from treasure 'from' to treasure 'to'. */
static void Smart_sr_compute(treasure_t *from, treasure_t *to, smart_route_set_t *set)
{
    static int path[8192];
    int cells = SrW * SrH, tries, k, len;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    set->computed = true;
    set->num = 0;
    memset(SrPenalty, 0, (size_t)cells * sizeof(*SrPenalty));
    memset(SrCorr, 0, (size_t)cells);

    for (tries = 0; tries < SR_MAX_TRIES && set->num < SR_VARIANTS; tries++) {
	int inside = 0;

	len = Smart_sr_search(from->pos, to->pos, path, (int)NELEM(path));
	if (len < 2)
	    break;
	for (k = 0; k < len; k++)
	    inside += SrCorr[path[k]];
	/* Penalise the corridor around this route for the next search */
	for (k = 0; k < len; k++) {
	    int r, q, n;
	    for (r = -SR_CORRIDOR; r <= SR_CORRIDOR; r++) {
		for (q = -SR_CORRIDOR; q <= SR_CORRIDOR; q++) {
		    if (Smart_sr_neighbour(path[k], q, r, &n) && SrPenalty[n] < 8.0f * SR_PENALTY)
			SrPenalty[n] += SR_PENALTY / (2 * SR_CORRIDOR + 1);
		}
	    }
	}
	if (set->num > 0 && inside > SR_MAX_OVERLAP * len)
	    continue;		/* not clearly different from an accepted route */
	if (!Smart_sr_make_route(path, len, &set->r[set->num]))
	    continue;
	for (k = 0; k < len; k++) {
	    int r, q, n;
	    for (r = -SR_CORRIDOR; r <= SR_CORRIDOR; r++) {
		for (q = -SR_CORRIDOR; q <= SR_CORRIDOR; q++) {
		    if (Smart_sr_neighbour(path[k], q, r, &n))
			SrCorr[n] = 1;
		}
	    }
	}
	set->num++;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (options.logRobots) {
	xpprintf("%s Smartbot routes team %d -> %d: %d variants in %.0f ms\n",
		 showtime(), from->team, to->team, set->num,
		 (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
	for (k = 0; k < set->num; k++)
	    xpprintf("%s Smartbot route team %d -> %d #%d: %.0f px, %d turns, time %.1f\n",
		     showtime(), from->team, to->team, k + 1, set->r[k].length,
		     set->r[k].turns, set->r[k].time);
    }
}

static int Smart_treasure_index(treasure_t *tr)
{
    int i;

    for (i = 0; i < Num_treasures() && i < SR_MAX_TREASURES; i++) {
	if (Treasure_by_index(i) == tr)
	    return i;
    }
    return -1;
}

/* Route variants from 'from' to 'to', computed on first use; NULL if none. */
static smart_route_set_t *Smart_route_set(treasure_t *from, treasure_t *to)
{
    int fi = Smart_treasure_index(from), ti = Smart_treasure_index(to);
    smart_route_set_t *set;

    if (fi < 0 || ti < 0)
	return NULL;
    if (RouteSetsPdata != pdata || RouteSetsNumPolys != num_polys) {
	memset(RouteSets, 0, sizeof(RouteSets));
	RouteSetsPdata = pdata;
	RouteSetsNumPolys = num_polys;
	if (!Smart_sr_build_grid()) {
	    RouteSetsNumPolys = -1;
	    return NULL;
	}
    }
    set = &RouteSets[fi][ti];
    if (!set->computed)
	Smart_sr_compute(from, to, set);
    return set->num > 0 ? set : NULL;
}

/* Is a visible enemy within 4 blocks of the route from point 'from' on? */
static bool Smart_route_guarded(player_t *pl, const smart_route_t *r, int from)
{
    int i, k, s;

    for (i = 0; i < NumPlayers; i++) {
	player_t *ship = Player_by_index(i);

	if (ship->id == pl->id || !Player_is_active(ship) || Team_immune(pl->id, ship->id)
	    || !Smart_detect_ship(pl, ship))
	    continue;
	for (k = MAX(from, 0); k < r->num - 1; k++) {
	    clpos_t a = r->pts[k], b = r->pts[k + 1];
	    double l = Wrap_length(b.cx - a.cx, b.cy - a.cy) / CLICK;
	    int steps = MAX(1, (int)(l / (2.0 * BLOCK_SZ)));

	    for (s = 0; s <= steps; s++) {
		clpos_t p;

		p.cx = a.cx + (click_t)((double)WRAP_DCX(b.cx - a.cx) * s / steps);
		p.cy = a.cy + (click_t)((double)WRAP_DCY(b.cy - a.cy) * s / steps);
		p = World_wrap_clpos(p);
		if (Wrap_length(p.cx - ship->pos.cx, p.cy - ship->pos.cy) / CLICK
		    < 4.0 * BLOCK_SZ)
		    return true;
	    }
	}
    }
    return false;
}

/*
 * Choose the route for the next ball run: at random, weighted by quality
 * (estimated flight time incl. corners), routes with enemies on them only
 * as a last resort.
 */
static void Smart_choose_route(player_t *pl, treasure_t *from, treasure_t *to)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    smart_route_set_t *set = Smart_route_set(from, to);
    double w[SR_VARIANTS], sum = 0.0, best = 1e30, pick;
    int k;

    my_data->route_valid = false;
    my_data->route_checked_at = my_data->robot_count;
    if (set == NULL)
	return;
    for (k = 0; k < set->num; k++)
	best = MIN(best, set->r[k].time);
    for (k = 0; k < set->num; k++) {
	w[k] = sqr(best / set->r[k].time);	/* (best time / time)^2 */
	if (Smart_route_guarded(pl, &set->r[k], 0))
	    w[k] *= 0.05;
	sum += w[k];
    }
    pick = rfrac() * sum;
    for (k = 0; k < set->num - 1 && pick > w[k]; k++)
	pick -= w[k];

    my_data->route = &set->r[k];
    my_data->route_from = from;
    my_data->route_to = to;
    my_data->route_cur = 0;
    my_data->route_lost = 0;
    my_data->route_valid = true;
}

/* Direction (dir units) in which the chosen route leaves the ball at bpos. */
static int Smart_route_exit_dir(const smart_route_t *r, clpos_t bpos)
{
    int k;

    for (k = 0; k < r->num - 1; k++) {
	if (Wrap_length(r->pts[k].cx - bpos.cx, r->pts[k].cy - bpos.cy) / CLICK
	    >= 1.5 * BLOCK_SZ)
	    break;
    }
    return (int)(Wrap_cfindDir(r->pts[k].cx - bpos.cx, r->pts[k].cy - bpos.cy) + 0.5);
}

/* A route point pushed away from enemies close to it, if there is room. */
static clpos_t Smart_route_nudge(player_t *pl, clpos_t pt)
{
    double px = 0.0, py = 0.0;
    clpos_t moved;
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *ship = Player_by_index(i);
	double dx, dy, d;

	if (ship->id == pl->id || !Player_is_active(ship) || Team_immune(pl->id, ship->id)
	    || !Smart_detect_ship(pl, ship))
	    continue;
	dx = WRAP_DX(CLICK_TO_PIXEL(pt.cx - ship->pos.cx));
	dy = WRAP_DY(CLICK_TO_PIXEL(pt.cy - ship->pos.cy));
	d = MAX(LENGTH(dx, dy), 1.0);
	if (d < 5.0 * BLOCK_SZ) {
	    px += dx / d * (5.0 * BLOCK_SZ - d) * 0.5;
	    py += dy / d * (5.0 * BLOCK_SZ - d) * 0.5;
	}
    }
    {
	double l = LENGTH(px, py);
	if (l < 1.0)
	    return pt;
	if (l > 2.5 * BLOCK_SZ) {
	    px *= 2.5 * BLOCK_SZ / l;
	    py *= 2.5 * BLOCK_SZ / l;
	}
    }
    moved.cx = pt.cx + PIXEL_TO_CLICK(px);
    moved.cy = pt.cy + PIXEL_TO_CLICK(py);
    moved = World_wrap_clpos(moved);
    return Smart_spot_is_free(pl, moved) ? moved : pt;
}

/*
 * Follow the chosen route while towing.  With a clear line to the next
 * point the remaining points go to the towing controller (which plans the
 * speed for the corners ahead); otherwise the local path finder (with its
 * enemy penalty) leads there.  Returns false when there is no route (left).
 */
static bool Smart_follow_route(player_t *pl, vector_t repulse_vec)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    const smart_route_t *r = my_data->route;
    clpos_t target;
    int k, n;

    while (my_data->route_cur < r->num - 1) {
	clpos_t cur = r->pts[my_data->route_cur], next = r->pts[my_data->route_cur + 1];

	if (Wrap_length(cur.cx - pl->pos.cx, cur.cy - pl->pos.cy) / CLICK < 2.5 * BLOCK_SZ
	    || (Smart_has_line_of_sight(pl, pl->pos, next, false)
		&& Smart_has_line_of_sight(pl, pl->pos, next, true)))
	    my_data->route_cur++;
	else
	    break;
    }
    if (my_data->route_cur == r->num - 1) {
	clpos_t last = r->pts[my_data->route_cur];
	if (Wrap_length(last.cx - pl->pos.cx, last.cy - pl->pos.cy) / CLICK < 2.5 * BLOCK_SZ) {
	    my_data->route_valid = false;	/* done; the throwing run takes over */
	    return false;
	}
    }

    target = Smart_route_nudge(pl, r->pts[my_data->route_cur]);
    if (Smart_has_line_of_sight(pl, pl->pos, target, false)) {
	my_data->route_lost = 0;
	my_data->waypoints[0] = target;
	for (n = 1, k = my_data->route_cur + 1; k < r->num && n < ASTAR_MAX_WAYPOINTS; k++, n++)
	    my_data->waypoints[n] = r->pts[k];
	my_data->num_waypoints = n;
	my_data->cur_waypoint = 0;
	/* Not an A* path: keep Smart_find_next_waypoint from reusing it */
	my_data->last_astar_tick = my_data->robot_count + 10 * ASTAR_RECALC_TICKS;
	Smart_fly_towards(pl, target, 24.0, repulse_vec, NULL);
    } else {
	if (++my_data->route_lost > 40) {
	    my_data->route_valid = false;	/* lost it: plain path finding home */
	    return false;
	}
	Smart_fly_towards(pl, Smart_find_next_waypoint(pl, target), 24.0, repulse_vec, NULL);
    }
    return true;
}

/*
 * Where to fly to grab the enemy ball: the connector arms anywhere within
 * ballConnectorLength, so hover well inside that range on our side of the
 * ball instead of flying into its deadly BallArea.
 */
static clpos_t Smart_pick_pickup_spot(player_t *pl, treasure_t *tr)
{
    static const int offs[] = { 0, 8, -8, 16, -16, 24, -24, 32, -32, 48, -48, 64 };
    clpos_t bpos = tr->pos;
    double r = 0.55 * options.ballConnectorLength;
    int base, i, k;

    for (i = 0; i < NumObjs; i++) {
	if (Obj[i]->type == OBJ_BALL && Obj[i]->id == NO_ID
	    && BALL_PTR(Obj[i])->ball_treasure == tr) {
	    bpos = Obj[i]->pos;
	    break;
	}
    }

    base = (int)(Wrap_cfindDir(pl->pos.cx - bpos.cx, pl->pos.cy - bpos.cy) + 0.5);
    {
	robot_smart_data_t *my_data = Robot_smart_get_data(pl);

	/* Grab from the side where the chosen route leaves */
	if (my_data->route_valid && my_data->route_from == tr)
	    base = Smart_route_exit_dir(my_data->route, bpos);
    }
    for (k = 0; k < (int)NELEM(offs); k++) {
	clpos_t p = Smart_offset_pos(bpos, base + offs[k] * RES / 128, r);
	if (Smart_spot_is_free(pl, p))
	    return p;
    }
    return Smart_offset_pos(bpos, base, r);
}

static bool Smart_handle_ctf(player_t *pl, vector_t repulse_vec,
			     int *aim_dir, bool *has_aim_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
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
	} else if (Team_by_index(tr->team) != NULL
		   && Team_by_index(tr->team)->NumMembers > 0
		   && Smart_believes_ball_home(pl->team, tr)) {
	    if (d < enemy_tr_dist) {
		enemy_tr_dist = d;
		enemy_tr = i;
	    }
	}
    }

    /*
     * Case 0: Connector is armed (we came within ballConnectorLength) but the
     * ball is not attached yet.  It attaches once we are a connector length
     * away again, so back off the way we came instead of closing in.
     */
    if (pl->ball != NULL && !BIT(pl->have, HAS_BALL)) {
	ballobject_t *ball = pl->ball;
	double ax = WRAP_DX(CLICK_TO_PIXEL(pl->pos.cx - ball->pos.cx));
	double ay = WRAP_DY(CLICK_TO_PIXEL(pl->pos.cy - ball->pos.cy));
	double alen = MAX(LENGTH(ax, ay), 1.0);
	double pull = 2.5 * options.ballConnectorLength;
	clpos_t away;

	/*
	 * Keep the connector only with a clear line to the ball; otherwise it
	 * drops (it was armed through a wall) and the approach goes on below.
	 */
	if (Smart_has_line_of_sight(pl, pl->pos, ball->pos, true)) {
	    if (my_data->route_valid && my_data->route_from == ball->ball_treasure)
		/* Pull out along the chosen route */
		away = Smart_offset_pos(ball->pos,
					Smart_route_exit_dir(my_data->route, ball->pos), pull);
	    else {
		away.cx = ball->pos.cx + PIXEL_TO_CLICK(ax / alen * pull);
		away.cy = ball->pos.cy + PIXEL_TO_CLICK(ay / alen * pull);
		away = World_wrap_clpos(away);
	    }
	    SET_BIT(pl->used, USES_CONNECTOR);
	    Smart_fly_towards(pl, away, 10.0, repulse_vec, NULL);
	    return true;
	}
    }

    /* Case 1: We are carrying a ball -> Navigate home & release it on target */
    if (BIT(pl->have, HAS_BALL) && home_tr != NO_IND) {
	treasure_t *htr = Treasure_by_index(home_tr);
	ballobject_t *ball = Smart_carried_ball(pl);

	/* Just stole the enemy ball: a small reward on the way to the big one */
	if (ball != NULL && ball->ball_treasure != NULL
	    && ball->ball_treasure->team != pl->team && !my_data->had_enemy_ball) {
	    my_data->accumulated_reward += REWARD_BALL_STOLEN;
	    my_data->had_enemy_ball = true;
	}

	/* Ball stuck behind a wall: let go, it is grabbed again properly */
	if (ball != NULL && !Smart_has_line_of_sight(pl, pl->pos, ball->pos, true)) {
	    if (++my_data->ball_blocked > 12) {
		Detach_ball(pl, NULL);
		my_data->ball_blocked = 0;
		return true;
	    }
	} else
	    my_data->ball_blocked = 0;

	if (ball != NULL && Smart_ball_on_target(pl, ball, htr)
	    && Smart_clear_of_ball_path(pl, ball)) {
	    Detach_ball(pl, NULL);
	    CLR_BIT(pl->used, USES_CONNECTOR);
	    my_data->last_thrown_ball = my_data->robot_count;
	    my_data->throw_side = 0;
	    my_data->deliver_retreat = false;
	    /* Small shaping only: the real reward comes with the score */
	    my_data->accumulated_reward += 2.0;
	    return true;
	}

	/* Too close without a release: swing out for another straight run */
	if (!my_data->deliver_retreat && home_tr_dist < 2.5 * BLOCK_SZ) {
	    my_data->deliver_pt = Smart_pick_swing_out_point(pl, htr);
	    my_data->deliver_retreat = true;
	}
	if (my_data->deliver_retreat) {
	    double d = Wrap_length(my_data->deliver_pt.cx - pl->pos.cx,
				   my_data->deliver_pt.cy - pl->pos.cy) / CLICK;
	    if (home_tr_dist > 7.0 * BLOCK_SZ || d < 1.5 * BLOCK_SZ)
		my_data->deliver_retreat = false;
	    else {
		clpos_t wp = Smart_find_next_waypoint(pl, my_data->deliver_pt);
		my_data->throw_side = 0;
		Smart_fly_towards(pl, wp, 12.0, repulse_vec, NULL);
		return true;
	    }
	}

	/*
	 * Throwing run once the ball has a clear line to the target: swing the
	 * ship out beside the ball's line and release when the ball is on course.
	 */
	if (ball != NULL
	    && home_tr_dist < 10.0 * BLOCK_SZ
	    && Smart_has_line_of_sight(pl, ball->pos, htr->pos, true)) {
	    Smart_fly_with_ball(pl, ball, htr->pos, 12.0, repulse_vec, &htr->pos);
	    return true;
	}
	my_data->throw_side = 0;

	/* The route chosen before the grab, as long as it fits */
	if (ball != NULL && my_data->route_valid && my_data->route_to == htr
	    && my_data->route_from == ball->ball_treasure
	    && Smart_follow_route(pl, repulse_vec))
	    return true;

	{
	    clpos_t wp = Smart_find_next_waypoint(pl, htr->pos);
	    /* Speed is planned along the path by the towing controller */
	    Smart_fly_towards(pl, wp, 24.0, repulse_vec, has_aim_dir ? aim_dir : NULL);
	    return true;
	}
    }

    /* Case 2: Check for loose enemy balls or enemy carriers to intercept */
    if (!WITHIN(my_data->robot_count, my_data->last_thrown_ball, 3 * FPS)) {
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
	    ballobject_t *ball = BALL_IND(loose_ball_i);
	    clpos_t spot = ball->ball_treasure != NULL
			   ? Smart_pick_pickup_spot(pl, ball->ball_treasure) : ball->pos;
	    clpos_t wp = Smart_find_next_waypoint(pl, spot);
	    double d = Wrap_length(spot.cx - pl->pos.cx, spot.cy - pl->pos.cy) / CLICK;

	    my_data->route_valid = false;	/* loose in the field: no base route */
	    Smart_use_connector(pl, ball->pos);
	    Smart_fly_towards(pl, wp, (d < 4.0 * BLOCK_SZ) ? 9.0 : 15.0,
			      repulse_vec, has_aim_dir ? aim_dir : NULL);
	    return true;
	}
    }

    /* Case 3: Go steal enemy treasure */
    if (enemy_tr != NO_IND) {
	treasure_t *etr = Treasure_by_index(enemy_tr);
	treasure_t *htr = (home_tr != NO_IND) ? Treasure_by_index(home_tr) : NULL;
	clpos_t spot, wp, bpos;
	double d;

	/*
	 * Choose the route home before the grab; until the ball is ours,
	 * choose again when an enemy shows up on it.
	 */
	if (htr != NULL
	    && (!my_data->route_valid || my_data->route_from != etr || my_data->route_to != htr))
	    Smart_choose_route(pl, etr, htr);
	else if (htr != NULL && ABS(my_data->route_checked_at - my_data->robot_count) > 12) {
	    my_data->route_checked_at = my_data->robot_count;
	    if (Smart_route_guarded(pl, my_data->route, 0))
		Smart_choose_route(pl, etr, htr);
	}

	spot = Smart_pick_pickup_spot(pl, etr);
	wp = Smart_find_next_waypoint(pl, spot);
	d = Wrap_length(spot.cx - pl->pos.cx, spot.cy - pl->pos.cy) / CLICK;

	/* Grab only at the spot, so the ball comes out on the route side */
	if (d < 1.5 * BLOCK_SZ && Smart_free_ball_pos(etr, &bpos))
	    Smart_use_connector(pl, bpos);
	Smart_fly_towards(pl, wp, (d < 4.0 * BLOCK_SZ) ? 9.0 : 16.0,
			  repulse_vec, has_aim_dir ? aim_dir : NULL);
	return true;
    }

    return false;
}

/* =========================================================================
 * Team Play: Role Assignment & Role Objectives
 * ========================================================================= */

static smart_team_t *Smart_team(int team)
{
    if (team < 0 || team >= MAX_TEAMS)
	return NULL;
    return &SmartTeams[team];
}

static bool Smart_is_smartbot(player_t *pl)
{
    const char *name = Robot_type_name(pl);

    return name != NULL && strcmp(name, robot_smart_type.name) == 0;
}

/* Does a living teammate see pos (within a human's view range)? */
static bool Smart_team_sees(int team, clpos_t pos)
{
    int i;

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl_i = Player_by_index(i);

	if (pl_i->team != team || Player_is_tank(pl_i) || !Player_is_alive(pl_i)
	    || Player_is_paused(pl_i) || Player_is_waiting(pl_i))
	    continue;
	if (Wrap_length(pl_i->pos.cx - pos.cx, pl_i->pos.cy - pos.cy) / CLICK
	    <= Visibility_distance)
	    return true;
    }
    return false;
}

/*
 * Does a team believe the ball of treasure tr is at home?  A team that
 * sees the treasure knows; otherwise it assumes the ball is still there
 * (own and enemy treasures alike; for the own ones see TreasureAwayKnown).
 */
static bool Smart_believes_ball_home(int team, treasure_t *tr)
{
    return Smart_team_sees(team, tr->pos) ? tr->have : true;
}

/* Does the team know that (one of) its own balls is away from its treasure? */
static bool Smart_team_ball_away(int team)
{
    int i;

    for (i = 0; i < Num_treasures() && i < MAX_KNOWN_TREASURES; i++) {
	if (TreasureAwayKnown[i] && Treasure_by_index(i)->team == team)
	    return true;
    }
    return false;
}

/*
 * Update what the teams know: who has stolen their ball (only while a
 * teammate sees the thief) and whether their own balls are away.  A ball
 * is seen anywhere within view range of a living teammate, loose or towed.
 */
static void Smart_update_team_knowledge(void)
{
    int i, j, t;

    for (t = 0; t < MAX_TEAMS; t++)
	SmartTeams[t].enemy_carrier_id = NO_ID;

    for (i = 0; i < NumObjs; i++) {
	ballobject_t *ball;
	player_t *owner;
	int bt;

	if (Obj[i]->type != OBJ_BALL)
	    continue;
	ball = BALL_IND(i);
	if (ball->id == NO_ID || ball->ball_treasure == NULL)
	    continue;
	bt = ball->ball_treasure->team;
	owner = Player_by_id(ball->id);
	if (owner == NULL || owner->team == bt || Smart_team(bt) == NULL)
	    continue;
	/* A teammate has to see the thief; cloaking hides him as for humans */
	for (j = 0; j < NumPlayers; j++) {
	    player_t *mate = Player_by_index(j);

	    if (mate->team == bt && !Player_is_tank(mate) && Player_is_alive(mate)
		&& !Player_is_paused(mate) && Smart_detect_ship(mate, owner)) {
		SmartTeams[bt].enemy_carrier_id = owner->id;
		break;
	    }
	}
    }

    for (i = 0; i < Num_treasures() && i < MAX_KNOWN_TREASURES; i++) {
	treasure_t *tr = Treasure_by_index(i);

	if (tr->have || tr->empty) {
	    TreasureAwayKnown[i] = false;
	    continue;
	}
	if (TreasureAwayKnown[i])
	    continue;
	if (Smart_team_sees(tr->team, tr->pos)) {
	    TreasureAwayKnown[i] = true;
	    continue;
	}
	for (j = 0; j < NumObjs; j++) {
	    if (Obj[j]->type == OBJ_BALL && BALL_IND(j)->ball_treasure == tr
		&& Smart_team_sees(tr->team, Obj[j]->pos)) {
		TreasureAwayKnown[i] = true;
		break;
	    }
	}
    }
}

/* Nearest treasure of a team to pos, NULL if the team has none. */
static treasure_t *Smart_team_treasure(int team, clpos_t pos, bool need_ball_home)
{
    treasure_t *best = NULL;
    double best_d = 1e19;
    int i;

    for (i = 0; i < Num_treasures(); i++) {
	treasure_t *tr = Treasure_by_index(i);
	double d;

	if (tr->team != team || (need_ball_home && !tr->have))
	    continue;
	d = Wrap_length(tr->pos.cx - pos.cx, tr->pos.cy - pos.cy);
	if (d < best_d) {
	    best_d = d;
	    best = tr;
	}
    }
    return best;
}

/* Nearest enemy treasure with its ball at home, NULL if none. */
static treasure_t *Smart_enemy_treasure(int team, clpos_t pos)
{
    treasure_t *best = NULL;
    double best_d = 1e19;
    int i;

    for (i = 0; i < Num_treasures(); i++) {
	treasure_t *tr = Treasure_by_index(i);
	double d;

	if (tr->team == team || Team_by_index(tr->team) == NULL
	    || !Smart_believes_ball_home(team, tr)
	    || Team_by_index(tr->team)->NumMembers <= 0)
	    continue;
	d = Wrap_length(tr->pos.cx - pos.cx, tr->pos.cy - pos.cy);
	if (d < best_d) {
	    best_d = d;
	    best = tr;
	}
    }
    return best;
}

/*
 * Pick the bot from pool[] (NULL entries are taken) closest to pos.  Bots
 * already holding the role get a bonus, so roles do not flap back and forth.
 */
static int Smart_pick_closest(player_t **pool, int n, clpos_t pos, int role)
{
    double best_d = 1e19;
    int i, best = -1;

    for (i = 0; i < n; i++) {
	double d;

	if (pool[i] == NULL)
	    continue;
	d = Wrap_length(pool[i]->pos.cx - pos.cx, pool[i]->pos.cy - pos.cy);
	if (Robot_smart_get_data(pool[i])->role == role)
	    d *= 0.7;
	if (d < best_d) {
	    best_d = d;
	    best = i;
	}
    }
    return best;
}

static void Smart_set_role(player_t **pool, int idx, int role, int escort_id)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pool[idx]);

    my_data->role = role;
    my_data->escort_id = escort_id;
    pool[idx] = NULL;
}

/*
 * Assign roles to the smartbots of every team:
 * - bots towing a ball are carriers,
 * - our ball stolen or loose: everybody hunts it, but with three or more
 *   bots one keeps attacking the enemy ball,
 * - otherwise: attacker, then (from three bots on) a defender, then
 *   escorts (at most two) for the carrier or attacker; further bots attack.
 */
static void Smart_assign_roles(void)
{
    player_t *bots[MAX_TEAMS][NUM_IDS];
    int nbots[MAX_TEAMS];
    int carrier_of_enemy_ball[MAX_TEAMS];
    int t, i;
    bool ball_game = BIT(world->rules->mode, TEAM_PLAY) && Num_treasures() > 0;

    for (t = 0; t < MAX_TEAMS; t++) {
	nbots[t] = 0;
	carrier_of_enemy_ball[t] = NO_ID;
    }

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl_i = Player_by_index(i);

	if (!Smart_is_smartbot(pl_i))
	    continue;
	if (!ball_game || Smart_team(pl_i->team) == NULL) {
	    Robot_smart_get_data(pl_i)->role = ROLE_NONE;
	    continue;
	}
	if (Player_is_alive(pl_i) && nbots[pl_i->team] < NUM_IDS)
	    bots[pl_i->team][nbots[pl_i->team]++] = pl_i;
    }
    if (!ball_game)
	return;

    Smart_update_team_knowledge();

    /* Precompute the ball run routes early instead of at the first grab */
    for (i = 0; i < Num_treasures(); i++) {
	int j;

	for (j = 0; j < Num_treasures(); j++) {
	    treasure_t *a = Treasure_by_index(i), *b = Treasure_by_index(j);

	    if (a->team != b->team && !a->empty && !b->empty)
		(void)Smart_route_set(a, b);
	}
    }

    /* Which teammate tows an enemy ball? (a team always knows its own members) */
    for (i = 0; i < NumObjs; i++) {
	ballobject_t *ball;
	player_t *owner;

	if (Obj[i]->type != OBJ_BALL)
	    continue;
	ball = BALL_IND(i);
	if (ball->id == NO_ID || ball->ball_treasure == NULL)
	    continue;
	owner = Player_by_id(ball->id);
	if (owner == NULL || Smart_team(owner->team) == NULL)
	    continue;
	if (owner->team != ball->ball_treasure->team)
	    carrier_of_enemy_ball[owner->team] = owner->id;
    }

    for (t = 0; t < MAX_TEAMS; t++) {
	player_t **pool = bots[t];
	int n = nbots[t], left, k;
	treasure_t *home, *enemy_tr;
	bool our_ball_home = !Smart_team_ball_away(t);
	int escort_target = carrier_of_enemy_ball[t];
	clpos_t ref;

	if (n == 0)
	    continue;
	ref = pool[0]->pos;

	/* Carriers keep towing */
	for (k = 0; k < n; k++) {
	    if (BIT(pool[k]->have, HAS_BALL))
		Smart_set_role(pool, k, ROLE_CARRIER, NO_ID);
	}
	left = 0;
	for (k = 0; k < n; k++)
	    if (pool[k] != NULL)
		left++;

	home = Smart_team_treasure(t, ref, false);
	enemy_tr = NULL;
	for (k = 0; k < n && enemy_tr == NULL; k++) {
	    if (pool[k] != NULL)
		enemy_tr = Smart_enemy_treasure(t, pool[k]->pos);
	}

	if (!our_ball_home) {
	    if (left >= 3 && enemy_tr != NULL && escort_target == NO_ID) {
		k = Smart_pick_closest(pool, n, enemy_tr->pos, ROLE_ATTACKER);
		if (k >= 0) {
		    Smart_set_role(pool, k, ROLE_ATTACKER, NO_ID);
		    left--;
		}
	    }
	    for (k = 0; k < n; k++) {
		if (pool[k] != NULL)
		    Smart_set_role(pool, k, ROLE_HUNTER, NO_ID);
	    }
	    continue;
	}

	/* Attacker, unless a teammate already tows the enemy ball */
	if (escort_target == NO_ID && enemy_tr != NULL && left > 0) {
	    k = Smart_pick_closest(pool, n, enemy_tr->pos, ROLE_ATTACKER);
	    escort_target = pool[k]->id;
	    Smart_set_role(pool, k, ROLE_ATTACKER, NO_ID);
	    left--;
	}
	/* Defender only with enough bots: stealing the ball comes first */
	if (home != NULL && left >= 2) {
	    k = Smart_pick_closest(pool, n, home->pos, ROLE_DEFENDER);
	    Smart_set_role(pool, k, ROLE_DEFENDER, NO_ID);
	    left--;
	}
	/* Up to two escorts */
	if (escort_target != NO_ID) {
	    player_t *target = Player_by_id(escort_target);
	    int escorts = 0;

	    while (target != NULL && left > 0 && escorts < 2) {
		k = Smart_pick_closest(pool, n, target->pos, ROLE_ESCORT);
		Smart_set_role(pool, k, ROLE_ESCORT, escort_target);
		left--;
		escorts++;
	    }
	}
	/* The rest attack */
	for (k = 0; k < n; k++) {
	    if (pool[k] != NULL)
		Smart_set_role(pool, k, enemy_tr != NULL ? ROLE_ATTACKER : ROLE_DEFENDER,
			       NO_ID);
	}
    }
}

/*
 * Work on the bot's role objective.  Returns false if the bot should fight
 * (or do something else) instead.  With the CTF tactic picked by the RL the
 * bot sticks to its objective; otherwise it fights enemies that matter for
 * its role.
 */
static bool Smart_play_role(player_t *pl, player_t *enemy, double enemy_dist,
			    bool enemy_los, vector_t repulse_vec,
			    int *aim_dir, bool *has_aim_dir)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    bool focus = (my_data->current_tactic == TACTIC_CTF_OBJECTIVE);
    smart_team_t *team = Smart_team(pl->team);

    switch (my_data->role) {
    case ROLE_CARRIER:
	return Smart_handle_ctf(pl, repulse_vec, aim_dir, has_aim_dir);

    case ROLE_ATTACKER:
	/*
	 * Stealing the ball comes first: only fight enemies right in our way
	 * (never with the CTF tactic).
	 */
	if (!focus && enemy != NULL && enemy_los && enemy_dist < 6.0 * BLOCK_SZ)
	    return false;
	return Smart_handle_ctf(pl, repulse_vec, aim_dir, has_aim_dir);

    case ROLE_HUNTER: {
	player_t *carrier = (team != NULL && team->enemy_carrier_id != NO_ID)
			    ? Player_by_id(team->enemy_carrier_id) : NULL;
	treasure_t *home;

	if (carrier != NULL && Player_is_alive(carrier)) {
	    double d = Wrap_length(carrier->pos.cx - pl->pos.cx,
				   carrier->pos.cy - pl->pos.cy) / CLICK;
	    double lead_t = MIN(d / 20.0, 15.0);
	    clpos_t aim, wp;

	    /* In sight: the combat code presses him and shoots */
	    if (enemy == carrier && enemy_los && d < 18.0 * BLOCK_SZ)
		return false;
	    aim.cx = carrier->pos.cx + PIXEL_TO_CLICK(carrier->vel.x * lead_t);
	    aim.cy = carrier->pos.cy + PIXEL_TO_CLICK(carrier->vel.y * lead_t);
	    wp = Smart_find_next_waypoint(pl, World_wrap_clpos(aim));
	    Smart_fly_towards(pl, wp, 22.0, repulse_vec, *has_aim_dir ? aim_dir : NULL);
	    return true;
	}
	/* Our ball lies loose somewhere: pick it up and bring it home */
	home = Smart_team_treasure(pl->team, pl->pos, false);
	if (home != NULL && Smart_team_ball_away(pl->team)) {
	    int i;

	    for (i = 0; i < NumObjs; i++) {
		if (Obj[i]->type == OBJ_BALL && Obj[i]->id == NO_ID
		    && BALL_PTR(Obj[i])->ball_treasure == home) {
		    clpos_t spot = Smart_pick_pickup_spot(pl, home);
		    clpos_t wp = Smart_find_next_waypoint(pl, spot);
		    double d = Wrap_length(spot.cx - pl->pos.cx,
					   spot.cy - pl->pos.cy) / CLICK;

		    Smart_use_connector(pl, Obj[i]->pos);
		    Smart_fly_towards(pl, wp, (d < 4.0 * BLOCK_SZ) ? 9.0 : 18.0,
				      repulse_vec, *has_aim_dir ? aim_dir : NULL);
		    return true;
		}
	    }
	}
	return false;
    }

    case ROLE_ESCORT: {
	player_t *target = Player_by_id(my_data->escort_id);
	double tspeed, d, speed, side = (pl->id & 1) ? 1.0 : -1.0;
	clpos_t spot, wp;

	if (target == NULL || !Player_is_alive(target))
	    return false;
	/* Fight enemies approaching the one we cover: that is the job */
	if (enemy != NULL
	    && Wrap_length(enemy->pos.cx - target->pos.cx,
			   enemy->pos.cy - target->pos.cy) / CLICK < 18.0 * BLOCK_SZ)
	    return false;

	/* Fly ahead and to the side, away from a towed ball trailing behind */
	tspeed = LENGTH(target->vel.x, target->vel.y);
	if (tspeed > 3.0) {
	    int vdir = (int)(findDir(target->vel.x, target->vel.y) + 0.5);
	    spot = Smart_offset_pos(target->pos, vdir, 3.5 * BLOCK_SZ);
	    spot = Smart_offset_pos(spot, vdir + (int)side * RES / 4, 2.0 * BLOCK_SZ);
	} else
	    spot = Smart_offset_pos(target->pos, (int)(side * RES / 4), 3.0 * BLOCK_SZ);

	d = Wrap_length(spot.cx - pl->pos.cx, spot.cy - pl->pos.cy) / CLICK;
	speed = tspeed + d / BLOCK_SZ * 1.5;
	LIMIT(speed, 6.0, 22.0);
	wp = Smart_find_next_waypoint(pl, spot);
	Smart_fly_towards(pl, wp, speed, repulse_vec, *has_aim_dir ? aim_dir : NULL);
	return true;
    }

    case ROLE_DEFENDER: {
	treasure_t *home = Smart_team_treasure(pl->team, pl->pos, false);
	int base, k;
	clpos_t spot, wp;
	double d;

	if (home == NULL)
	    return false;
	/* Fight enemies near our treasure: that is the job */
	if (enemy != NULL
	    && Wrap_length(enemy->pos.cx - home->pos.cx,
			   enemy->pos.cy - home->pos.cy) / CLICK < 16.0 * BLOCK_SZ)
	    return false;

	/* Slowly circle the treasure */
	base = (RoundCounter / 4 + pl->id * 37) % RES;
	spot = Smart_offset_pos(home->pos, base, 4.0 * BLOCK_SZ);
	for (k = 1; k < 8 && !Smart_spot_is_free(pl, spot); k++)
	    spot = Smart_offset_pos(home->pos, base + k * RES / 8, 4.0 * BLOCK_SZ);

	d = Wrap_length(spot.cx - pl->pos.cx, spot.cy - pl->pos.cy) / CLICK;
	wp = Smart_find_next_waypoint(pl, spot);
	Smart_fly_towards(pl, wp, MIN(16.0, 3.0 + d / BLOCK_SZ * 1.5),
			  repulse_vec, *has_aim_dir ? aim_dir : NULL);
	return true;
    }

    default:
	/* No ball game role: the old behaviour */
	if (focus || BIT(pl->have, HAS_BALL) || pl->ball != NULL)
	    return Smart_handle_ctf(pl, repulse_vec, aim_dir, has_aim_dir);
	return false;
    }
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

    if (RoundCounter % ROLE_UPDATE_TICKS == 0)
	Smart_assign_roles();
}

static void Robot_smart_game_event(player_t *pl, int event, player_t *other)
{
    robot_smart_data_t *my_data = Robot_smart_get_data(pl);
    double reward = 0.0;

    switch (event) {
    case ROBOT_BALL_CASHED:	reward = REWARD_BALL_CASHED;	break;
    case ROBOT_BALL_WON:	reward = REWARD_BALL_WON;	break;
    case ROBOT_BALL_LOST:	reward = REWARD_BALL_LOST;	break;
    case ROBOT_BALL_SAVED:	reward = REWARD_BALL_SAVED;	break;
    case ROBOT_KILLED_PLAYER: {
	treasure_t *home;

	/* The kill itself counts in Smart_rl_step; ball games add context */
	if (other == NULL || !BIT(world->rules->mode, TEAM_PLAY)
	    || Num_treasures() <= 0 || other->team == pl->team)
	    return;
	if (BIT(other->have, HAS_BALL))
	    reward += REWARD_KILL_CARRIER;
	home = Smart_team_treasure(pl->team, other->pos, false);
	if (home != NULL
	    && Wrap_length(other->pos.cx - home->pos.cx, other->pos.cy - home->pos.cy) / CLICK
	       < 16.0 * BLOCK_SZ)
	    reward += REWARD_KILL_AT_HOME;
	if (reward == 0.0)
	    return;
	break;
    }
    default:			return;
    }
    my_data->accumulated_reward += reward;
    my_data->episode_reward += reward;
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
    my_data->role = ROLE_NONE;
    my_data->escort_id = NO_ID;
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
    my_data->route_valid = false;
    my_data->ball_blocked = 0;
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
    {
	ballobject_t *towed = Smart_carried_ball(pl);

	if (towed == NULL || towed->ball_treasure == NULL
	    || towed->ball_treasure->team == pl->team)
	    my_data->had_enemy_ball = false;
    }

    /*
     * Default equipment state per tick (shields stay OFF unless needed to
     * save fuel!).  The connector is only switched on for a deliberate grab
     * (Smart_use_connector); a towed ball stays attached without it.
     */
    CLR_BIT(pl->used, USES_SHOT | USES_SHIELD | USES_LASER | USES_CONNECTOR);

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
	/* A teammate in sight of the thief of our ball tells the others */
	bool our_ball_thief = Smart_team(pl->team) != NULL
			      && ship->id == SmartTeams[pl->team].enemy_carrier_id;

	if (ship->id == pl->id || !Player_is_active(ship) || Team_immune(pl->id, ship->id))
	    continue;
	if (!our_ball_thief && !Smart_detect_ship(pl, ship))
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
	if (our_ball_thief && my_data->role == ROLE_HUNTER)
	    priority_dist *= 0.2;
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

    /*
     * Engage enemy with ballistic lead prediction if in range.  While towing
     * a ball the ship's heading belongs to the towing controller, which turns
     * to the enemy only while the pair coasts (carrier_aim).
     */
    if (best_enemy != NULL) {
	Smart_engage_enemy(pl, best_enemy, best_enemy_dist, best_enemy_los,
			   LENGTH(repulse_vec.x, repulse_vec.y) > 10.0
			   || BIT(pl->have, HAS_BALL),
			   &aim_dir, &has_aim_dir);
    }
    my_data->carrier_aim = BIT(pl->have, HAS_BALL) && has_aim_dir
			   && best_enemy_dist < 12.0 * BLOCK_SZ;
    my_data->carrier_aim_dir = aim_dir;

    /* Raise shield only if we did not just fire and a threat is imminent */
    if (need_shield_now && BIT(pl->have, HAS_SHIELD) && !BIT(pl->used, HAS_SHOT | HAS_LASER)) {
	SET_BIT(pl->used, HAS_SHIELD);
    }
    if (!options.allowShields && options.playerStartsShielded && BIT(pl->have, HAS_SHIELD)) {
	SET_BIT(pl->used, HAS_SHIELD);
    }

    /* Execute movement based on Race mode, CTF, Fuel need, Item collection, or Combat Hunt */
    if (BIT(world->rules->mode, TIMING) && world->NumChecks > 0) {
	clpos_t wp = Smart_race_waypoint(pl);
	vector_t grav = World_gravity(pl->pos);

	/*
	 * The scan steers against local gravity.  Against the repelling
	 * bumpers in race track corners that steers into them and traps the
	 * ship; the race way avoids gravity instead.
	 */
	repulse_vec.x += grav.x * 5.0;
	repulse_vec.y += grav.y * 5.0;
	Smart_fly_towards(pl, wp, 24.0, repulse_vec, has_aim_dir ? &aim_dir : NULL);
	return;
    }

    /* Towing (or about to tow) a ball takes priority over everything else */
    if ((BIT(pl->have, HAS_BALL) || pl->ball != NULL)
	&& Smart_handle_ctf(pl, repulse_vec, &aim_dir, &has_aim_dir))
	return;

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

    /* Ball game role objectives (or the plain CTF objective without roles) */
    if (Smart_play_role(pl, best_enemy, best_enemy_dist, best_enemy_los,
			repulse_vec, &aim_dir, &has_aim_dir))
	return;

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
	/* The thief of our ball (or a carrier near the one we cover): press him */
	if (BIT(best_enemy->have, HAS_BALL)
	    && (my_data->role == ROLE_HUNTER || my_data->role == ROLE_ESCORT
		|| my_data->role == ROLE_DEFENDER))
	    pref_dist = 3.0 * BLOCK_SZ;

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
