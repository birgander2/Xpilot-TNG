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

#include "xpserver.h"
#include "botcore.h"
#include <sys/stat.h>

bool	BotWrap;
double	BotW, BotH;

void Bot_geo_init(void)
{
    BotWrap = BIT(world->rules->mode, WRAP_PLAY) ? true : false;
    BotW = (double)world->cwidth / CLICK;
    BotH = (double)world->cheight / CLICK;
}

int Bot_turn_rate(int level)
{
    return 3 + (level * 9) / 100;	/* same as the other smart robots */
}

/*
 * How far below level 100: 0 at level 100, 1 at level 1.  Rising faster
 * than linear from level 100 down, so middle levels feel it too.
 */
double Bot_unskill(int level)
{
    return pow((100 - level) / 99.0, 0.7);
}

/* Thrust power of the skill level; at level 1 the share 'loss' is missing. */
double Bot_power(int level, double loss)
{
    return MAX_PLAYER_POWER * (1.0 - loss * Bot_unskill(level));
}

bool Bot_player_active(player_t *pl)
{
    return pl != NULL && !Player_is_tank(pl) && Player_is_alive(pl)
	&& !Player_is_paused(pl) && !Player_is_waiting(pl);
}

/* Farthest point of the ship shape from its centre (px), at least SHIP_SZ. */
double Bot_ship_radius(player_t *pl)
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
 * Heading (rate limited, as in the model) and thrust.  Turning can push the
 * ship off a wall, which is a bounce: the shield must be decided before.
 */
void Bot_steer(player_t *pl, int want, bool thrust)
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

/*
 * Lead direction for a shot of speed s at a target at (rx, ry) moving at
 * (wvx, wvy) relative to us (the shot inherits our velocity).
 */
bool Bot_lead(double rx, double ry, double wvx, double wvy, double s,
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

/*
 * 1-D squared Euclidean distance transform (Felzenszwalb/Huttenlocher).
 * v and z are scratch arrays of n and n + 1 elements.
 */
void Bot_dt1d(const double *f, double *d, int n, int *v, double *z)
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
 * <user data dir>/<subdir>/<map name>.txt, the map name reduced to
 * lower case letters, digits, '-' and '_'.  The returned buffer is static.
 */
const char *Bot_learn_path(const char *map, const char *subdir, bool mkdirs)
{
    static char path[PATH_MAX];
    const char *base = Conf_user_data_dir();
    char name[MAX_CHARS], *p;
    int i, n = 0;

    for (i = 0; map[i] != '\0' && n < (int)sizeof(name) - 1; i++) {
	char ch = map[i];

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
    snprintf(path, sizeof(path), "%s/%s/%s.txt", base, subdir, name);
    if (mkdirs) {
	for (p = path + 1; (p = strchr(p, '/')) != NULL; p++) {
	    *p = '\0';
	    mkdir(path, 0755);
	    *p = '/';
	}
    }
    return path;
}

bool Bot_heap_alloc(bot_heap_t *h, int cap)
{
    h->e = malloc(sizeof(bot_heapent_t) * cap);
    h->n = 0;
    h->cap = h->e != NULL ? cap : 0;
    return h->e != NULL;
}

void Bot_heap_free(bot_heap_t *h)
{
    free(h->e);
    h->e = NULL;
    h->n = h->cap = 0;
}

/* Pushes are dropped silently when the heap is full. */
void Bot_heap_push(bot_heap_t *h, float f, int32_t c)
{
    int i;

    if (h->n >= h->cap)
	return;
    i = h->n++;
    while (i > 0) {
	int parent = (i - 1) / 2;

	if (h->e[parent].f <= f)
	    break;
	h->e[i] = h->e[parent];
	i = parent;
    }
    h->e[i].f = f;
    h->e[i].c = c;
}

/* The heap must not be empty. */
bot_heapent_t Bot_heap_pop(bot_heap_t *h)
{
    bot_heapent_t top = h->e[0], last = h->e[--h->n];
    int i = 0;

    while (h->n > 0) {
	int child = 2 * i + 1;

	if (child >= h->n)
	    break;
	if (child + 1 < h->n && h->e[child + 1].f < h->e[child].f)
	    child++;
	if (h->e[child].f >= last.f)
	    break;
	h->e[i] = h->e[child];
	i = child;
    }
    if (h->n > 0)
	h->e[i] = last;
    return top;
}
