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
 * Race results and records.
 *
 * At the end of a race every TNG client gets the standings of the race
 * and two top ten lists, of whole races and of single laps.  The lists
 * hold human players only, the best time of each nick, and are kept per
 * course (map, checkpoints), number of laps, game speed and bot setup in
 * $XDG_DATA_HOME/xpilot-tng/racetimes/ (default ~/.local/share/...).
 *
 * Ball games keep a top ten list of ballruns (the time from lifting the
 * ball out of its treasure until it is cashed), again humans only and one
 * entry per nick, per map (treasures), game speed and bot setup in .../balltimes/.
 * The list pops up at the end of a round, only when it changed, and the
 * next round waits for the popups like the next race.
 *
 * Deathmatch (no race, no treasures) keeps a top ten list of the highest
 * score a human reached before dying, one entry per nick, per map, game
 * speed and bot setup in .../dmscores/.  A player gets it when joining, when
 * pausing and when entering it; everybody is told about a new entry.
 */

#include "xpserver.h"
#include <sys/stat.h>

#define RR_TOP			10
#define RR_SUBDIR		"xpilot-tng/racetimes"
#define BR_SUBDIR		"xpilot-tng/balltimes"
#define RR_VERSION		1
#define DM_SUBDIR		"xpilot-tng/dmscores"

/* kinds of lists (files) */
#define RR_RACE			0
#define RR_BALL			1
#define RR_DM			2

typedef struct {
    double	secs;
    long	when;		/* unix time */
    char	name[MAX_CHARS];
    bool	fresh;		/* entered in this race */
} rr_entry_t;

typedef struct {
    player_t	*pl;
    int		pos;		/* finishing position, 0: did not finish */
    double	total, best;	/* seconds, 0: none */
    int		lap;
} rr_standing_t;

static int	FinishPos[NUM_IDS];	/* by player id, 0: not finished */

/*
 * After a race (or a ball game round with a new ballrun record) the next
 * one waits until every human with a TNG client has closed the popup, at most RR_PAUSE_SECS (the popup closes itself
 * after 30 seconds).
 */
#define RR_PAUSE_SECS		35
static bool	ResultOpen[NUM_IDS];	/* by player id: popup still open */
static long	PauseUntil = -1;	/* frame_loops, -1: no pause */

/* A client closed its result popup. */
void Race_result_done(player_t *pl)
{
    if (pl->id >= 0 && pl->id < NUM_IDS)
	ResultOpen[pl->id] = false;
}

/*
 * Called every frame in race and team mode: true while the next race or
 * round waits for the popups.  The ships stay at their bases, still appearing.
 */
bool Race_records_pause(void)
{
    int i, open = 0;

    if (PauseUntil < 0)
	return false;
    for (i = 0; i < NUM_IDS; i++) {
	player_t *pl;

	if (!ResultOpen[i])
	    continue;
	pl = Player_by_id(i);
	if (pl == NULL || pl->conn == NULL)	/* left */
	    ResultOpen[i] = false;
	else
	    open++;
    }
    if (open == 0 || frame_loops >= PauseUntil) {
	memset(ResultOpen, 0, sizeof(ResultOpen));
	PauseUntil = -1;
	return false;
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (Player_is_appearing(pl) && pl->recovery_count > 0)
	    pl->recovery_count = RECOVERY_DELAY;
    }
    return true;
}

/* A player finished the race in position 'pos' (race.c). */
void Race_note_finish(player_t *pl, int pos)
{
    if (pl->id >= 0 && pl->id < NUM_IDS)
	FinishPos[pl->id] = pos;
}

/* Ballruns of the current round, the best of each human nick. */
typedef struct {
    double	secs;
    char	name[MAX_CHARS];
} br_run_t;

static br_run_t	BallRuns[NUM_IDS];
static int	NumBallRuns;

/* A ball was cashed after 'secs' (treasure.c). */
void Ball_note_run(player_t *pl, double secs)
{
    int i;

    if (pl == NULL || !Player_is_human(pl) || secs <= 0.0)
	return;
    for (i = 0; i < NumBallRuns; i++) {
	if (strcmp(BallRuns[i].name, pl->name) == 0) {
	    if (secs < BallRuns[i].secs)
		BallRuns[i].secs = secs;
	    return;
	}
    }
    if (NumBallRuns >= NUM_IDS)
	return;
    BallRuns[NumBallRuns].secs = secs;
    strlcpy(BallRuns[NumBallRuns].name, pl->name, sizeof(BallRuns[0].name));
    NumBallRuns++;
}

/* Checkpoints of a race course, treasures of a ball map. */
static uint32_t Rr_course_sig(bool ball)
{
    uint32_t h = 2166136261u;
    int i;

    if (ball) {
	for (i = 0; i < Num_treasures(); i++) {
	    treasure_t *t = Treasure_by_index(i);

	    h = (h ^ (uint32_t)(t->pos.cx / CLICK)) * 16777619u;
	    h = (h ^ (uint32_t)(t->pos.cy / CLICK)) * 16777619u;
	    h = (h ^ (uint32_t)t->team) * 16777619u;
	}
	return h;
    }
    for (i = 0; i < world->NumChecks; i++) {
	check_t *ch = Check_by_index(i);

	h = (h ^ (uint32_t)(ch->pos.cx / CLICK)) * 16777619u;
	h = (h ^ (uint32_t)(ch->pos.cy / CLICK)) * 16777619u;
    }
    return h;
}

/*
 * Bot setup of the lists, from the options: number of ordinary and smart
 * bots (or bots per human, or filling up to that number), the AI and
 * skill of the smart ones.  Empty without bots, so the files of games
 * without bots keep their names.
 */
static const char *Rr_bot_sig(void)
{
    static char sig[MAX_CHARS];
    int plain = MAX(options.maxRobots, 0), smart = MAX(options.maxSmartRobots, 0);
    char *p;

    sig[0] = '\0';
    if (options.botsMatchHumans ? options.botsPerHuman <= 0.0
				: plain + smart == 0)
	return sig;
    snprintf(sig, sizeof(sig), "_bots%d", plain);
    if (smart > 0) {
	const char *type = Robot_smart_driver();
	int level = !strcmp(type, FIGHT_ROBOT_TYPE)
		    ? options.fightRobotLevel : options.smartRobotLevel;

	snprintf(sig + strlen(sig), sizeof(sig) - strlen(sig),
		 "_smart%d_%s_lvl%d", smart, type, level);
    }
    if (options.botsMatchHumans)
	snprintf(sig + strlen(sig), sizeof(sig) - strlen(sig),
		 "_perhuman%g", options.botsPerHuman);
    else if (options.robotsFillTo)
	strlcat(sig, "_fill", sizeof(sig));
    for (p = sig; *p != '\0'; p++)
	if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
	    *p = '_';
    return sig;
}

/*
 * File of the current course, laps and game speed (or of the ball map and
 * game speed); NULL without a home.
 */
static const char *Rr_path(bool mkdirs, int kind)
{
    static char path[PATH_MAX];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char name[MAX_CHARS], *p;
    const char *subdir = kind == RR_DM ? DM_SUBDIR
			 : kind == RR_BALL ? BR_SUBDIR : RR_SUBDIR;
    int i, n = 0;

    for (i = 0; world->name[i] != '\0' && n < (int)sizeof(name) - 1; i++) {
	char ch = world->name[i];

	if (isalnum((unsigned char)ch) || ch == '-' || ch == '_')
	    name[n++] = (char)tolower((unsigned char)ch);
	else if (n > 0 && name[n - 1] != '_')
	    name[n++] = '_';
    }
    name[n] = '\0';
    if (n == 0)
	strlcpy(name, "unnamed", sizeof(name));

    if (xdg && xdg[0] == '/')
	snprintf(path, sizeof(path), "%s/%s/", xdg, subdir);
    else if (home && home[0])
	snprintf(path, sizeof(path), "%s/.local/share/%s/", home, subdir);
    else
	return NULL;
    if (kind == RR_DM)
	snprintf(path + strlen(path), sizeof(path) - strlen(path),
		 "%s_speed%g%s.txt", name, options.gameSpeed, Rr_bot_sig());
    else if (kind == RR_BALL)
	snprintf(path + strlen(path), sizeof(path) - strlen(path),
		 "%s_speed%g%s_%08x.txt", name, options.gameSpeed,
		 Rr_bot_sig(), (unsigned)Rr_course_sig(true));
    else
	snprintf(path + strlen(path), sizeof(path) - strlen(path),
		 "%s_%dlaps_speed%g%s_%08x.txt", name, options.raceLaps,
		 options.gameSpeed, Rr_bot_sig(),
		 (unsigned)Rr_course_sig(false));
    if (mkdirs) {
	for (p = path + 1; (p = strchr(p, '/')) != NULL; p++) {
	    *p = '\0';
	    mkdir(path, 0755);
	    *p = '/';
	}
    }
    return path;
}

/* Ball and deathmatch files hold 'T' lines only. */
static void Rr_load(int kind, rr_entry_t *total, int *nt, rr_entry_t *lap, int *nl)
{
    const char *path = Rr_path(false, kind);
    char line[256];
    FILE *fp;

    *nt = *nl = 0;
    if (path == NULL || (fp = fopen(path, "r")) == NULL)
	return;
    while (fgets(line, sizeof(line), fp) != NULL) {
	char tag, *name;
	double secs;
	long when;
	int off = 0;
	rr_entry_t *e;

	if (sscanf(line, "%c %lf %ld %n", &tag, &secs, &when, &off) != 3 || off == 0)
	    continue;
	if (tag == 'T' && *nt < RR_TOP)
	    e = &total[(*nt)++];
	else if (tag == 'L' && *nl < RR_TOP)
	    e = &lap[(*nl)++];
	else
	    continue;
	name = line + off;
	name[strcspn(name, "\r\n")] = '\0';
	e->secs = secs;
	e->when = when;
	e->fresh = false;
	strlcpy(e->name, name, sizeof(e->name));
    }
    fclose(fp);
}

static void Rr_save(int kind, const rr_entry_t *total, int nt,
		    const rr_entry_t *lap, int nl)
{
    const char *path = Rr_path(true, kind);
    char tmp[PATH_MAX + 8];
    FILE *fp;
    bool ok = true;
    int i;

    if (path == NULL)
	return;
    /* write a temporary file and rename it, a crash never leaves half a file */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if ((fp = fopen(tmp, "w")) == NULL)
	return;
    if (kind == RR_DM) {
	if (fprintf(fp, "# dmscores %d: %s, game speed %g\n", RR_VERSION,
		    world->name, options.gameSpeed) < 0)
	    ok = false;
    } else if (kind == RR_BALL) {
	if (fprintf(fp, "# balltimes %d: %s, game speed %g\n", RR_VERSION,
		    world->name, options.gameSpeed) < 0)
	    ok = false;
    } else if (fprintf(fp, "# racetimes %d: %s, %d laps, game speed %g\n",
		       RR_VERSION, world->name, options.raceLaps,
		       options.gameSpeed) < 0)
	ok = false;
    for (i = 0; i < nt && ok; i++)
	ok = fprintf(fp, "T %.3f %ld %s\n", total[i].secs, total[i].when, total[i].name) >= 0;
    for (i = 0; i < nl && ok; i++)
	ok = fprintf(fp, "L %.3f %ld %s\n", lap[i].secs, lap[i].when, lap[i].name) >= 0;
    if (fclose(fp) != 0)
	ok = false;
    if (!ok || rename(tmp, path) != 0)
	remove(tmp);
}

/*
 * Enter a time (or, with 'higher', a score) into a top list (sorted, best
 * first), one entry per nick.  Returns true when the list changed.
 */
static bool Rr_enter(rr_entry_t *list, int *n, const char *name, double secs,
		     long when, bool higher)
{
    int i, at;

    for (i = 0; i < *n; i++) {
	if (strcmp(list[i].name, name) == 0) {
	    if (higher ? list[i].secs >= secs : list[i].secs <= secs)
		return false;	/* has a better one */
	    memmove(&list[i], &list[i + 1], sizeof(rr_entry_t) * (*n - i - 1));
	    (*n)--;
	    break;
	}
    }
    for (at = 0; at < *n && (higher ? list[at].secs >= secs
				    : list[at].secs <= secs); at++)
	;
    if (at >= RR_TOP)
	return false;
    if (*n == RR_TOP)
	(*n)--;
    memmove(&list[at + 1], &list[at], sizeof(rr_entry_t) * (*n - at));
    list[at].secs = secs;
    list[at].when = when;
    list[at].fresh = true;
    strlcpy(list[at].name, name, sizeof(list[at].name));
    (*n)++;
    return true;
}

static int Rr_standing_cmp(const void *va, const void *vb)
{
    const rr_standing_t *a = va, *b = vb;

    if ((a->pos > 0) != (b->pos > 0))
	return a->pos > 0 ? -1 : 1;
    if (a->pos > 0)
	return a->pos - b->pos;
    /* not finished: the farther the better */
    if (a->lap != b->lap)
	return b->lap - a->lap;
    return b->pl->check - a->pl->check;
}

static int Rr_ms(double secs)
{
    return (int)(secs * 1000.0 + 0.5);
}

/* The race is over: update the records and show everybody the results. */
void Race_records_finish(void)
{
    static rr_standing_t st[NUM_IDS];
    static rr_entry_t total[RR_TOP], lap[RR_TOP];
    int i, j, ns = 0, nt, nl;
    bool changed = false;
    long now = (long)time(NULL);

    if (!options.highscores || world->NumChecks <= 0 || FPS <= 0) {
	memset(FinishPos, 0, sizeof(FinishPos));
	return;
    }

    /* who raced: everybody who crossed the start line */
    for (i = 0; i < NumPlayers && ns < NUM_IDS; i++) {
	player_t *pl = Player_by_index(i);
	rr_standing_t *s;

	if (Player_is_tank(pl) || Player_is_paused(pl) || pl->id < 0
	    || pl->id >= NUM_IDS || pl->round <= 0)
	    continue;
	s = &st[ns++];
	s->pl = pl;
	s->pos = FinishPos[pl->id];
	s->total = s->pos > 0 ? (double)pl->time / FPS : 0.0;
	s->best = pl->best_lap > 0 ? (double)pl->best_lap / FPS : 0.0;
	s->lap = MIN(pl->round, options.raceLaps);
    }
    memset(FinishPos, 0, sizeof(FinishPos));
    if (ns == 0)
	return;
    qsort(st, ns, sizeof(st[0]), Rr_standing_cmp);

    /* the top lists: humans only */
    Rr_load(RR_RACE, total, &nt, lap, &nl);
    for (i = 0; i < ns; i++) {
	if (!Player_is_human(st[i].pl))
	    continue;
	if (st[i].total > 0.0)
	    changed |= Rr_enter(total, &nt, st[i].pl->name, st[i].total, now, false);
	if (st[i].best > 0.0)
	    changed |= Rr_enter(lap, &nl, st[i].pl->name, st[i].best, now, false);
    }
    if (changed)
	Rr_save(RR_RACE, total, nt, lap, nl);

    /* the popup for every TNG client, players and spectators */
    for (j = 0; j < spectatorStart + NumSpectators; j++) {
	player_t *to;
	connection_t *conn;

	if (j == NumPlayers) {
	    if (NumSpectators == 0)
		break;
	    j = spectatorStart;
	}
	to = Player_by_index(j);
	if ((conn = to->conn) == NULL || !FEATURE(conn, F_HIGHSCORE))
	    continue;
	/* players (not spectators) hold the next race until they close it */
	if (j < NumPlayers && to->id >= 0 && to->id < NUM_IDS && !Player_is_paused(to))
	    ResultOpen[to->id] = true;
	Send_highscore_head(conn, world->name, options.raceLaps,
			      (int)(options.gameSpeed * 10.0 + 0.5));
	for (i = 0; i < ns; i++) {
	    int flags = 0;

	    if (Player_is_robot(st[i].pl))
		flags |= HIGHSCORE_F_ROBOT;
	    if (st[i].pos > 0)
		flags |= HIGHSCORE_F_FINISHED;
	    if (st[i].pl == to)
		flags |= HIGHSCORE_F_YOU;
	    Send_highscore_line(conn, HIGHSCORE_STANDING, st[i].pos, flags,
				  Rr_ms(st[i].total), Rr_ms(st[i].best), st[i].lap,
				  st[i].pl->name);
	}
	for (i = 0; i < nt; i++)
	    Send_highscore_line(conn, HIGHSCORE_TOP_TOTAL, i + 1,
				  (total[i].fresh ? HIGHSCORE_F_NEW : 0)
				  | (strcmp(total[i].name, to->name) == 0
				     ? HIGHSCORE_F_YOU : 0),
				  Rr_ms(total[i].secs), (int)total[i].when, 0,
				  total[i].name);
	for (i = 0; i < nl; i++)
	    Send_highscore_line(conn, HIGHSCORE_TOP_LAP, i + 1,
				  (lap[i].fresh ? HIGHSCORE_F_NEW : 0)
				  | (strcmp(lap[i].name, to->name) == 0
				     ? HIGHSCORE_F_YOU : 0),
				  Rr_ms(lap[i].secs), (int)lap[i].when, 0,
				  lap[i].name);
	Send_highscore_end(conn);
    }
    for (i = 0; i < NUM_IDS; i++) {
	if (ResultOpen[i]) {
	    PauseUntil = frame_loops + (long)RR_PAUSE_SECS * FPS;
	    Set_message("The next race starts when everybody closed the results. "
			"[*Server notice*]");
	    break;
	}
    }
}

/*
 * A ball game round is over: enter its ballruns into the top list and,
 * only when the list changed, show it to every TNG client.
 */
void Ball_records_finish(void)
{
    static rr_entry_t top[RR_TOP], unused[RR_TOP];
    int i, j, nt, nu;
    bool changed = false;
    long now = (long)time(NULL);

    if (!options.highscores)
	NumBallRuns = 0;
    if (NumBallRuns == 0)
	return;
    Rr_load(RR_BALL, top, &nt, unused, &nu);
    for (i = 0; i < NumBallRuns; i++)
	changed |= Rr_enter(top, &nt, BallRuns[i].name, BallRuns[i].secs, now, false);
    NumBallRuns = 0;
    if (!changed)
	return;
    Rr_save(RR_BALL, top, nt, unused, 0);

    for (j = 0; j < spectatorStart + NumSpectators; j++) {
	player_t *to;
	connection_t *conn;

	if (j == NumPlayers) {
	    if (NumSpectators == 0)
		break;
	    j = spectatorStart;
	}
	to = Player_by_index(j);
	if ((conn = to->conn) == NULL || !FEATURE(conn, F_HIGHSCORE))
	    continue;
	/* players (not spectators) hold the next round until they close it */
	if (j < NumPlayers && to->id >= 0 && to->id < NUM_IDS && !Player_is_paused(to))
	    ResultOpen[to->id] = true;
	Send_highscore_line(conn, HIGHSCORE_BALL_HEAD, 0, 0, 0,
			      (int)(options.gameSpeed * 10.0 + 0.5), 0,
			      world->name);
	for (i = 0; i < nt; i++)
	    Send_highscore_line(conn, HIGHSCORE_BALL_TOP, i + 1,
				  (top[i].fresh ? HIGHSCORE_F_NEW : 0)
				  | (strcmp(top[i].name, to->name) == 0
				     ? HIGHSCORE_F_YOU : 0),
				  Rr_ms(top[i].secs), (int)top[i].when, 0,
				  top[i].name);
	Send_highscore_line(conn, HIGHSCORE_BALL_END, 0, 0, 0, 0, 0, "");
    }
    for (i = 0; i < NUM_IDS; i++) {
	if (ResultOpen[i]) {
	    PauseUntil = frame_loops + (long)RR_PAUSE_SECS * FPS;
	    Set_message("The next round starts when everybody closed the records. "
			"[*Server notice*]");
	    break;
	}
    }
}

/*
 * Deathmatch highscores: the highest score a human had before dying.
 */
static bool	DmTrack[NUM_IDS];	/* by player id: peak is followed */
static double	DmPeak[NUM_IDS];	/* highest score of this life */
static rr_entry_t DmTop[RR_TOP];	/* the list of the current map, cached */
static int	DmN;
static char	DmLoadedPath[PATH_MAX];

static bool Dm_active(void)
{
    return options.highscores && !BIT(world->rules->mode, TIMING)
	   && Num_treasures() == 0;
}

static bool Dm_player(player_t *pl)
{
    return Dm_active() && Player_is_human(pl) && pl->id >= 0
	   && pl->id < NUM_IDS && DmTrack[pl->id];
}

/* The score of a player changed (score.c). */
void Dm_note_score(player_t *pl)
{
    if (pl->id >= 0 && pl->id < NUM_IDS && DmTrack[pl->id]
	&& pl->score > DmPeak[pl->id])
	DmPeak[pl->id] = pl->score;
}

static void Dm_load(void)
{
    static rr_entry_t unused[RR_TOP];
    const char *path = Rr_path(false, RR_DM);
    int nu;

    if (path == NULL || strcmp(path, DmLoadedPath) == 0)
	return;
    strlcpy(DmLoadedPath, path, sizeof(DmLoadedPath));
    Rr_load(RR_DM, DmTop, &DmN, unused, &nu);
}

/* The top list for one player (also a paused one). */
static void Dm_send(player_t *to, int reason)
{
    connection_t *conn = to->conn;
    int i;

    if (conn == NULL || !FEATURE(conn, F_HIGHSCORE))
	return;
    Dm_load();
    Send_highscore_line(conn, HIGHSCORE_DM_HEAD, 0, 0, 0,
			(int)(options.gameSpeed * 10.0 + 0.5), reason,
			world->name);
    for (i = 0; i < DmN; i++)
	Send_highscore_line(conn, HIGHSCORE_DM_TOP, i + 1,
			    (reason == HIGHSCORE_DM_RECORD && DmTop[i].fresh
			     ? HIGHSCORE_F_NEW : 0)
			    | (strcmp(DmTop[i].name, to->name) == 0
			       ? HIGHSCORE_F_YOU : 0),
			    (int)(DmTop[i].secs * 10.0 + 0.5), (int)DmTop[i].when, 0,
			    DmTop[i].name);
    Send_highscore_line(conn, HIGHSCORE_DM_END, 0, 0, 0, 0, 0, "");
}

/* A player pauses (event.c). */
void Dm_records_pause(player_t *pl)
{
    if (Dm_player(pl))
	Dm_send(pl, HIGHSCORE_DM_PAUSE);
}

/* A player entered the map (netserver.c). */
void Dm_records_join(player_t *pl)
{
    if (!Dm_active() || !Player_is_human(pl) || pl->id < 0 || pl->id >= NUM_IDS)
	return;
    DmTrack[pl->id] = true;
    DmPeak[pl->id] = pl->score;
    Dm_send(pl, HIGHSCORE_DM_JOIN);
}

/* Scores go with one decimal, sent as tenths (time_ms).
 * Enter the peak of a player; with 'announce' tell him and everybody else. */
static void Dm_enter(player_t *pl, double peak, bool announce)
{
    int i, rank = 0;

    peak = floor(peak * 10.0 + 0.5) / 10.0;
    if (peak < 0.1)
	return;
    Dm_load();
    for (i = 0; i < DmN; i++)
	DmTop[i].fresh = false;
    if (!Rr_enter(DmTop, &DmN, pl->name, peak, (long)time(NULL), true))
	return;
    Rr_save(RR_DM, DmTop, DmN, NULL, 0);
    if (!announce)
	return;
    for (i = 0; i < DmN; i++) {
	if (strcmp(DmTop[i].name, pl->name) == 0)
	    rank = i + 1;
    }
    Set_message_f("%c%s reached %.1f points, rank %d in the deathmatch "
		  "highscores! [*Highscore*]", MSG_ALERT_MARK, pl->name,
		  peak, rank);
    Dm_send(pl, HIGHSCORE_DM_RECORD);
}

/* A player is killed: his score drops now, so the peak counts (player.c). */
void Dm_records_death(player_t *pl)
{
    double peak;

    if (!Dm_player(pl))
	return;
    peak = DmPeak[pl->id];
    DmPeak[pl->id] = pl->score;
    Dm_enter(pl, peak, true);
}

/* A player leaves: the peak of the current life counts (player.c). */
void Dm_records_leave(player_t *pl)
{
    if (!Dm_player(pl))
	return;
    Dm_enter(pl, DmPeak[pl->id], false);
    DmTrack[pl->id] = false;
}
