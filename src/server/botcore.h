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
 * Helpers shared by the ballbot, fightbot and racebot drivers: world
 * geometry with wrapping, angles, intercept math, a binary min-heap for
 * the Dijkstra fields, the 1-D distance transform, ship control and the
 * path of the per-map learn files.
 */

#ifndef BOTCORE_H
#define BOTCORE_H

#ifndef XPSERVER_H
# include "xpserver.h"
#endif

/* World size in pixels and wrap mode, set by Bot_geo_init(). */
extern bool	BotWrap;
extern double	BotW, BotH;

void Bot_geo_init(void);

static inline double Bot_px(click_t c)
{
    return (double)c / CLICK;
}

static inline double Bot_wdx(double dx)
{
    if (BotWrap) {
	if (dx > BotW * 0.5)
	    dx -= BotW;
	else if (dx < -BotW * 0.5)
	    dx += BotW;
    }
    return dx;
}

static inline double Bot_wdy(double dy)
{
    if (BotWrap) {
	if (dy > BotH * 0.5)
	    dy -= BotH;
	else if (dy < -BotH * 0.5)
	    dy += BotH;
    }
    return dy;
}

static inline double Bot_dist(double x1, double y1, double x2, double y2)
{
    return LENGTH(Bot_wdx(x2 - x1), Bot_wdy(y2 - y1));
}

static inline int Bot_angle_diff(int a, int b)
{
    int d = MOD2(a - b, RES);

    return d > RES / 2 ? d - RES : d;
}

static inline int Bot_dir_of(double x, double y)
{
    return MOD2((int)(findDir(x, y) + 0.5), RES);
}

static inline int Bot_turn_toward(int dir, int want, int rate)
{
    int d = Bot_angle_diff(want, dir);

    LIMIT(d, -rate, rate);
    return MOD2(dir + d, RES);
}

/*
 * Squared closest distance during one tick between a ship moving from
 * (ax0, ay0) to (ax1, ay1) and an object moving from (bx0, by0) to (bx1, by1).
 * Fast shots would otherwise tunnel through between two samples.
 */
static inline double Bot_cpa2(double ax0, double ay0, double ax1, double ay1,
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

int Bot_turn_rate(int level);
double Bot_unskill(int level);
double Bot_power(int level, double loss);
bool Bot_player_active(player_t *pl);
double Bot_ship_radius(player_t *pl);
void Bot_steer(player_t *pl, int want, bool thrust);
bool Bot_lead(double rx, double ry, double wvx, double wvy, double s,
	      double *dir, double *tof);
void Bot_dt1d(const double *f, double *d, int n, int *v, double *z);
const char *Bot_learn_path(const char *map, const char *subdir, bool mkdirs);

/* Binary min-heap of (cost, nav cell) pairs. */
typedef struct { float f; int32_t c; } bot_heapent_t;
typedef struct {
    bot_heapent_t	*e;
    int			n, cap;
} bot_heap_t;

bool Bot_heap_alloc(bot_heap_t *h, int cap);
void Bot_heap_free(bot_heap_t *h);
void Bot_heap_push(bot_heap_t *h, float f, int32_t c);
bot_heapent_t Bot_heap_pop(bot_heap_t *h);

#endif
