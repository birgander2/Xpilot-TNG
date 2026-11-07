/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Pseudo-3D hill landscape inside filled wall polygons.
 *
 * The wall outlines stay exactly as they are; only the inside of filled
 * polygons gets low-poly hills: extra lattice lines plus flat shading.
 *
 * The landscape is a global function of the world position, so the many
 * polygons a map may be built from (joined by invisible internal edges)
 * fit together seamlessly:
 *  - a global, slightly jittered triangle lattice is clipped to each polygon
 *  - height = f(distance to the nearest *visible* wall edge) * noise, so the
 *    visible wall edges lie at height 0 and internal edges are invisible
 *  - each triangle is shaded by its slope against a fixed light
 *
 * Everything is computed once per polygon when it is first drawn and kept
 * in chunks for culling. Purely cosmetic, client side only.
 */

#include "sdl3_client.h"

/* ------------------------------------------------------------------ */
/* Options                                                            */
/* ------------------------------------------------------------------ */

static bool fxTerrain = true;
static bool fxTerrainMesh = true;

static xp_option_t terrain_options[] = {
    XP_BOOL_OPTION("fxTerrain", true, &fxTerrain, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw filled walls as a shaded hill landscape.\n"),
    XP_BOOL_OPTION("fxTerrainMesh", true, &fxTerrainMesh, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw the lattice lines of the hill landscape.\n"),
};

void Store_terrain_options(void)
{
    STORE_OPTIONS(terrain_options);
}

/* ------------------------------------------------------------------ */
/* Parameters (world pixels)                                          */
/* ------------------------------------------------------------------ */

#define TR_GRID         40.0f   /* lattice spacing (fitted to the map size) */
#define TR_JITTER       0.22f   /* lattice jitter, fraction of TR_GRID */
#define TR_HEIGHT       70.0f   /* hill height scale */
#define TR_RISE         80.0f   /* distance over which hills rise from an edge */
#define TR_DMAX         180.0f  /* distances are capped here */
#define TR_NOISE_SCALE  (1.0f / 170.0f)
#define TR_CHUNK        8       /* lattice quads per chunk side */
#define TR_CELL         128.0f  /* edge grid cell size */

/* light from the upper left, slightly above (world: y up, z towards viewer) */
static const float tr_light[3] = { -0.50f, 0.60f, 0.62f };

/* ------------------------------------------------------------------ */
/* Cache                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    float x, y;
    float shade;            /* 1.0 = flat, <1 darker, >1 lighter */
} tr_vert_t;

typedef struct {
    float x1, y1, x2, y2;
} tr_seg_t;

typedef struct {
    float minx, miny, maxx, maxy;
    int   v0, nv;           /* range in tr_poly_t.verts */
    int   s0, ns;           /* range in tr_poly_t.segs */
} tr_chunk_t;

typedef struct {
    bool        built;
    bool        ok;
    tr_chunk_t *chunks;
    int         num_chunks, max_chunks;
    tr_vert_t  *verts;
    int         num_verts, max_verts;
    tr_seg_t   *segs;
    int         num_segs, max_segs;
} tr_poly_t;

static tr_poly_t          *tr_polys;
static int                 tr_num_polys;
static const xp_polygon_t *tr_src;      /* polygons array the cache belongs to */

/* visible wall edges, bucketed in a grid for distance queries */
typedef struct {
    float x1, y1, x2, y2;
} tr_edge_t;

/*
 * The lattice and the noise repeat with the map size, and distances look
 * across the map edge on wrapping maps, so walls split at the map edge
 * show no seam.
 */
static bool  tr_wrap;
static float tr_map_w, tr_map_h;
static int   tr_nx, tr_ny;              /* lattice cells per map width/height */
static float tr_cw, tr_ch;              /* lattice cell size, close to TR_GRID */

static tr_edge_t *tr_edges;
static int        tr_num_edges, tr_max_edges;
static int       *tr_cell_start;        /* gw * gh + 1 */
static int       *tr_cell_items;
static int        tr_gw, tr_gh;
static float      tr_gx0, tr_gy0;

static void tr_free(void)
{
    for (int i = 0; i < tr_num_polys; i++) {
        free(tr_polys[i].chunks);
        free(tr_polys[i].verts);
        free(tr_polys[i].segs);
    }
    free(tr_polys);
    tr_polys = NULL;
    tr_num_polys = 0;
    free(tr_edges);
    tr_edges = NULL;
    tr_num_edges = tr_max_edges = 0;
    free(tr_cell_start);
    free(tr_cell_items);
    tr_cell_start = NULL;
    tr_cell_items = NULL;
    tr_src = NULL;
}

void Fx_terrain_cleanup(void)
{
    tr_free();
}

/* World coordinates of all points of a polygon (points[0] absolute, rest relative). */
static int tr_poly_points(const xp_polygon_t *poly, SDL_FPoint *out)
{
    int x = 0, y = 0;
    for (int p = 0; p < poly->num_points; p++) {
        x += poly->points[p].x;
        y += poly->points[p].y;
        out[p].x = (float)x;
        out[p].y = (float)y;
    }
    return poly->num_points;
}

static bool tr_build_edges(void)
{
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    SDL_FPoint *pts = NULL;
    int max_pts = 0;

    for (int i = 0; i < num_polygons; i++) {
        const xp_polygon_t *poly = &polygons[i];
        int n = poly->num_points;
        if (n < 2)
            continue;
        if (n > max_pts) {
            SDL_FPoint *np = (SDL_FPoint *)realloc(pts, sizeof(*pts) * (size_t)n);
            if (!np) {
                free(pts);
                return false;
            }
            pts = np;
            max_pts = n;
        }
        tr_poly_points(poly, pts);
        for (int j = 0; j < n; j++) {
            /* style 0 marks internal edges between joined polygons */
            if (poly->edge_styles && poly->edge_styles[j] == 0)
                continue;
            int k = (j + 1) % n;
            if (tr_num_edges >= tr_max_edges) {
                int nm = tr_max_edges ? tr_max_edges * 2 : 1024;
                tr_edge_t *ne = (tr_edge_t *)realloc(tr_edges, sizeof(*ne) * (size_t)nm);
                if (!ne) {
                    free(pts);
                    return false;
                }
                tr_edges = ne;
                tr_max_edges = nm;
            }
            tr_edge_t *e = &tr_edges[tr_num_edges++];
            e->x1 = pts[j].x;
            e->y1 = pts[j].y;
            e->x2 = pts[k].x;
            e->y2 = pts[k].y;
            minx = fminf(minx, fminf(e->x1, e->x2));
            maxx = fmaxf(maxx, fmaxf(e->x1, e->x2));
            miny = fminf(miny, fminf(e->y1, e->y2));
            maxy = fmaxf(maxy, fmaxf(e->y1, e->y2));
        }
    }
    free(pts);

    if (tr_num_edges == 0) {
        minx = miny = 0.0f;
        maxx = maxy = 1.0f;
    }
    tr_gx0 = minx - TR_CELL;
    tr_gy0 = miny - TR_CELL;
    tr_gw = (int)((maxx - tr_gx0) / TR_CELL) + 2;
    tr_gh = (int)((maxy - tr_gy0) / TR_CELL) + 2;

    /* counting sort of edges into every cell their bounding box touches */
    size_t ncell = (size_t)tr_gw * (size_t)tr_gh;
    tr_cell_start = (int *)calloc(ncell + 1, sizeof(int));
    if (!tr_cell_start)
        return false;

    for (int pass = 0; pass < 2; pass++) {
        int *fill = NULL;
        if (pass == 1) {
            int total = 0;
            for (size_t c = 0; c < ncell; c++) {
                int cnt = tr_cell_start[c];
                tr_cell_start[c] = total;
                total += cnt;
            }
            tr_cell_start[ncell] = total;
            tr_cell_items = (int *)malloc(sizeof(int) * (size_t)(total > 0 ? total : 1));
            fill = (int *)calloc(ncell, sizeof(int));
            if (!tr_cell_items || !fill) {
                free(fill);
                return false;
            }
        }
        for (int i = 0; i < tr_num_edges; i++) {
            const tr_edge_t *e = &tr_edges[i];
            int cx0 = (int)((fminf(e->x1, e->x2) - tr_gx0) / TR_CELL);
            int cx1 = (int)((fmaxf(e->x1, e->x2) - tr_gx0) / TR_CELL);
            int cy0 = (int)((fminf(e->y1, e->y2) - tr_gy0) / TR_CELL);
            int cy1 = (int)((fmaxf(e->y1, e->y2) - tr_gy0) / TR_CELL);
            for (int cy = cy0; cy <= cy1; cy++) {
                for (int cx = cx0; cx <= cx1; cx++) {
                    size_t c = (size_t)cy * (size_t)tr_gw + (size_t)cx;
                    if (pass == 0)
                        tr_cell_start[c]++;
                    else
                        tr_cell_items[tr_cell_start[c] + fill[c]++] = i;
                }
            }
        }
        free(fill);
    }
    return true;
}

static bool tr_ensure_cache(void)
{
    if (tr_src == polygons && tr_num_polys == num_polygons && tr_polys)
        return true;

    tr_free();
    if (!polygons || num_polygons <= 0)
        return false;
    tr_polys = (tr_poly_t *)calloc((size_t)num_polygons, sizeof(tr_poly_t));
    if (!tr_polys)
        return false;
    tr_num_polys = num_polygons;
    tr_src = polygons;
    tr_wrap = BIT(Setup->mode, WRAP_PLAY) != 0;
    tr_map_w = (float)(Setup->width > 0 ? Setup->width : 1);
    tr_map_h = (float)(Setup->height > 0 ? Setup->height : 1);
    tr_nx = (int)lroundf(tr_map_w / TR_GRID);
    tr_ny = (int)lroundf(tr_map_h / TR_GRID);
    if (tr_nx < 1)
        tr_nx = 1;
    if (tr_ny < 1)
        tr_ny = 1;
    tr_cw = tr_map_w / (float)tr_nx;
    tr_ch = tr_map_h / (float)tr_ny;
    if (!tr_build_edges()) {
        warn("FX: not enough memory for the hill landscape");
        tr_free();
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Height field                                                       */
/* ------------------------------------------------------------------ */

static float tr_seg_dist2(float px, float py, const tr_edge_t *e)
{
    float dx = e->x2 - e->x1, dy = e->y2 - e->y1;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0.0f ? ((px - e->x1) * dx + (py - e->y1) * dy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float qx = e->x1 + t * dx - px, qy = e->y1 + t * dy - py;
    return qx * qx + qy * qy;
}

/* Squared distance to the nearest visible wall edge, at most TR_DMAX^2. */
static float tr_edge_dist2(float x, float y, float best)
{
    int r = (int)ceilf(TR_DMAX / TR_CELL);
    int cx = (int)floorf((x - tr_gx0) / TR_CELL);
    int cy = (int)floorf((y - tr_gy0) / TR_CELL);

    for (int yy = cy - r; yy <= cy + r; yy++) {
        if (yy < 0 || yy >= tr_gh)
            continue;
        for (int xx = cx - r; xx <= cx + r; xx++) {
            if (xx < 0 || xx >= tr_gw)
                continue;
            size_t c = (size_t)yy * (size_t)tr_gw + (size_t)xx;
            for (int k = tr_cell_start[c]; k < tr_cell_start[c + 1]; k++) {
                float d2 = tr_seg_dist2(x, y, &tr_edges[tr_cell_items[k]]);
                if (d2 < best)
                    best = d2;
            }
        }
    }
    return best;
}

/* Distance to the nearest visible wall edge, capped at TR_DMAX. */
static float tr_edge_distance(float x, float y)
{
    float best = tr_edge_dist2(x, y, TR_DMAX * TR_DMAX);

    if (tr_wrap) {
        /* edges on the other side of the map edge */
        const float lox = tr_gx0 - TR_DMAX, hix = tr_gx0 + (float)tr_gw * TR_CELL + TR_DMAX;
        const float loy = tr_gy0 - TR_DMAX, hiy = tr_gy0 + (float)tr_gh * TR_CELL + TR_DMAX;
        for (int sy = -1; sy <= 1; sy++) {
            for (int sx = -1; sx <= 1; sx++) {
                float px = x + (float)sx * tr_map_w, py = y + (float)sy * tr_map_h;
                if ((sx || sy) && px > lox && px < hix && py > loy && py < hiy)
                    best = tr_edge_dist2(px, py, best);
            }
        }
    }
    return sqrtf(best);
}

static inline int tr_mod(int i, int n)
{
    int m = i % n;
    return m < 0 ? m + n : m;
}

static uint32_t tr_hash(int i, int j, uint32_t salt);

/* Value noise 0..1 on a lattice of px * py cells that repeats. */
static float tr_vnoise(float x, float y, int px, int py, uint32_t salt)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
    int x0 = tr_mod(xi, px), x1 = tr_mod(xi + 1, px);
    int y0 = tr_mod(yi, py), y1 = tr_mod(yi + 1, py);
    float a = (float)(tr_hash(x0, y0, salt) & 0xffff) / 65535.0f;
    float b = (float)(tr_hash(x1, y0, salt) & 0xffff) / 65535.0f;
    float c = (float)(tr_hash(x0, y1, salt) & 0xffff) / 65535.0f;
    float d = (float)(tr_hash(x1, y1, salt) & 0xffff) / 65535.0f;
    float top = a + (b - a) * u, bot = c + (d - c) * u;
    return top + (bot - top) * v;
}

/* Fractal noise 0..1 at a world position, repeating with the map size. */
static float tr_fbm(float wx, float wy)
{
    int px = (int)lroundf(tr_map_w * TR_NOISE_SCALE);
    int py = (int)lroundf(tr_map_h * TR_NOISE_SCALE);
    float x, y, sum = 0.0f, amp = 0.5f, norm = 0.0f;

    if (px < 1)
        px = 1;
    if (py < 1)
        py = 1;
    x = wx / tr_map_w * (float)px;
    y = wy / tr_map_h * (float)py;
    for (int o = 0; o < 4; o++) {
        sum += amp * tr_vnoise(x, y, px, py, 4711u + (uint32_t)o * 101u);
        norm += amp;
        x *= 2.0f;
        y *= 2.0f;
        px *= 2;
        py *= 2;
        amp *= 0.5f;
    }
    return sum / norm;
}

static float tr_height(float x, float y)
{
    float d = tr_edge_distance(x, y);
    float rise = 1.0f - expf(-d / TR_RISE);
    float n = tr_fbm(x, y);
    return TR_HEIGHT * rise * (0.45f + 1.1f * n);
}

/* Flat shading of a triangle from its slope; 1.0 for level ground. */
static float tr_shade(const SDL_FPoint *a, const SDL_FPoint *b, const SDL_FPoint *c,
                      float ha, float hb, float hc)
{
    float ux = b->x - a->x, uy = b->y - a->y, uz = hb - ha;
    float vx = c->x - a->x, vy = c->y - a->y, vz = hc - ha;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);

    if (len < 1e-6f)
        return 1.0f;
    if (nz < 0.0f) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    float ll = sqrtf(tr_light[0] * tr_light[0] + tr_light[1] * tr_light[1]
                     + tr_light[2] * tr_light[2]);
    float dot = (nx * tr_light[0] + ny * tr_light[1] + nz * tr_light[2]) / (len * ll);
    float flat = tr_light[2] / ll;
    float s = dot / flat;
    return s < 0.45f ? 0.45f : (s > 1.55f ? 1.55f : s);
}

/* ------------------------------------------------------------------ */
/* Lattice and clipping                                               */
/* ------------------------------------------------------------------ */

static uint32_t tr_hash(int i, int j, uint32_t salt)
{
    uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* Jittered lattice point; the jitter repeats with the map size. */
static SDL_FPoint tr_lattice(int i, int j)
{
    int wi = tr_mod(i, tr_nx), wj = tr_mod(j, tr_ny);
    float jx = ((float)(tr_hash(wi, wj, 1) & 0xffff) / 65535.0f - 0.5f) * 2.0f * TR_JITTER;
    float jy = ((float)(tr_hash(wi, wj, 2) & 0xffff) / 65535.0f - 0.5f) * 2.0f * TR_JITTER;
    return (SDL_FPoint){ ((float)i + jx) * tr_cw, ((float)j + jy) * tr_ch };
}

static bool tr_point_in_poly(const SDL_FPoint *p, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((p[i].y > y) != (p[j].y > y)) {
            float xi = p[j].x + (y - p[j].y) * (p[i].x - p[j].x) / (p[i].y - p[j].y);
            if (x < xi)
                in = !in;
        }
    }
    return in;
}

/*
 * Sutherland-Hodgman: clip a convex polygon against a counter-clockwise
 * triangle. The result is convex again. Returns the number of points in out.
 */
static int tr_clip_tri(const SDL_FPoint *poly, int n, const SDL_FPoint tri[3],
                       SDL_FPoint *buf_a, SDL_FPoint *buf_b, SDL_FPoint **out)
{
    const SDL_FPoint *in = poly;
    SDL_FPoint *dst = buf_a;
    int cnt = n;

    for (int e = 0; e < 3 && cnt > 0; e++) {
        SDL_FPoint a = tri[e], b = tri[(e + 1) % 3];
        float ex = b.x - a.x, ey = b.y - a.y;
        int m = 0;
        for (int i = 0; i < cnt; i++) {
            SDL_FPoint p = in[i], q = in[(i + 1) % cnt];
            float sp = ex * (p.y - a.y) - ey * (p.x - a.x);
            float sq = ex * (q.y - a.y) - ey * (q.x - a.x);
            bool pin = sp >= 0.0f, qin = sq >= 0.0f;
            if (pin)
                dst[m++] = p;
            if (pin != qin) {
                float t = sp / (sp - sq);
                dst[m++] = (SDL_FPoint){ p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t };
            }
        }
        cnt = m;
        in = dst;
        dst = (dst == buf_a) ? buf_b : buf_a;
    }
    *out = (SDL_FPoint *)in;
    return cnt;
}

static float tr_area(const SDL_FPoint *p, int n)
{
    float a = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++)
        a += p[j].x * p[i].y - p[i].x * p[j].y;
    return 0.5f * a;
}

static bool tr_push_vert(tr_poly_t *tp, float x, float y, float shade)
{
    if (tp->num_verts >= tp->max_verts) {
        int nm = tp->max_verts ? tp->max_verts * 2 : 768;
        tr_vert_t *nv = (tr_vert_t *)realloc(tp->verts, sizeof(*nv) * (size_t)nm);
        if (!nv)
            return false;
        tp->verts = nv;
        tp->max_verts = nm;
    }
    tp->verts[tp->num_verts++] = (tr_vert_t){ x, y, shade };
    return true;
}

static bool tr_push_seg(tr_poly_t *tp, SDL_FPoint a, SDL_FPoint b)
{
    if (tp->num_segs >= tp->max_segs) {
        int nm = tp->max_segs ? tp->max_segs * 2 : 512;
        tr_seg_t *ns = (tr_seg_t *)realloc(tp->segs, sizeof(*ns) * (size_t)nm);
        if (!ns)
            return false;
        tp->segs = ns;
        tp->max_segs = nm;
    }
    tp->segs[tp->num_segs++] = (tr_seg_t){ a.x, a.y, b.x, b.y };
    return true;
}

static int tr_cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Add the parts of segment a-b that lie inside the polygon. */
static bool tr_clip_segment(tr_poly_t *tp, const SDL_FPoint *p, int n,
                            SDL_FPoint a, SDL_FPoint b, float *ts)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    int nt = 0;

    ts[nt++] = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float ex = p[i].x - p[j].x, ey = p[i].y - p[j].y;
        float den = dx * ey - dy * ex;
        if (fabsf(den) < 1e-9f)
            continue;
        float wx = p[j].x - a.x, wy = p[j].y - a.y;
        float t = (wx * ey - wy * ex) / den;
        float u = (wx * dy - wy * dx) / den;
        if (t > 0.0f && t < 1.0f && u >= 0.0f && u <= 1.0f)
            ts[nt++] = t;
    }
    ts[nt++] = 1.0f;
    qsort(ts, (size_t)nt, sizeof(float), tr_cmp_float);

    for (int k = 0; k + 1 < nt; k++) {
        float t0 = ts[k], t1 = ts[k + 1];
        if (t1 - t0 < 1e-4f)
            continue;
        float tm = 0.5f * (t0 + t1);
        if (!tr_point_in_poly(p, n, a.x + dx * tm, a.y + dy * tm))
            continue;
        if (!tr_push_seg(tp, (SDL_FPoint){ a.x + dx * t0, a.y + dy * t0 },
                         (SDL_FPoint){ a.x + dx * t1, a.y + dy * t1 }))
            return false;
    }
    return true;
}

static bool tr_build_poly(tr_poly_t *tp, const xp_polygon_t *poly)
{
    int n = poly->num_points;
    SDL_FPoint *pts = (SDL_FPoint *)malloc(sizeof(SDL_FPoint) * (size_t)n);
    int *ptri = (int *)malloc(sizeof(int) * (size_t)(n - 2) * 3);
    float *ts = (float *)malloc(sizeof(float) * (size_t)(n + 2));
    SDL_FPoint ca[16], cb[16];
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    int npt = 0;
    bool ok = pts && ptri && ts;

    if (ok) {
        tr_poly_points(poly, pts);
        for (int k = 0; k < n; k++) {
            minx = fminf(minx, pts[k].x);
            maxx = fmaxf(maxx, pts[k].x);
            miny = fminf(miny, pts[k].y);
            maxy = fmaxf(maxy, pts[k].y);
        }
        /*
         * Same triangulation as the plain fill. Clipping these convex
         * triangles against the convex lattice triangles gives convex
         * pieces, which always fan-triangulate correctly (clipping the
         * concave polygon directly leaves bridges that confuse ear clipping).
         */
        npt = SDL3_Triangulate_Polygon(pts, n, ptri, (n - 2) * 3) / 3;
        ok = npt > 0;
    }

    int i0 = (int)floorf(minx / tr_cw) - 1, i1 = (int)ceilf(maxx / tr_cw) + 1;
    int j0 = (int)floorf(miny / tr_ch) - 1, j1 = (int)ceilf(maxy / tr_ch) + 1;

    for (int ci = i0; ok && ci < i1; ci += TR_CHUNK) {
        for (int cj = j0; ok && cj < j1; cj += TR_CHUNK) {
            int v0 = tp->num_verts, s0 = tp->num_segs;

            for (int i = ci; ok && i < ci + TR_CHUNK && i < i1; i++) {
                for (int j = cj; ok && j < cj + TR_CHUNK && j < j1; j++) {
                    SDL_FPoint q00 = tr_lattice(i, j), q10 = tr_lattice(i + 1, j);
                    SDL_FPoint q11 = tr_lattice(i + 1, j + 1), q01 = tr_lattice(i, j + 1);
                    bool diag = tr_hash(tr_mod(i, tr_nx), tr_mod(j, tr_ny), 3) & 1;
                    SDL_FPoint tris[2][3];

                    if (!diag) {
                        tris[0][0] = q00; tris[0][1] = q10; tris[0][2] = q11;
                        tris[1][0] = q00; tris[1][1] = q11; tris[1][2] = q01;
                    } else {
                        tris[0][0] = q00; tris[0][1] = q10; tris[0][2] = q01;
                        tris[1][0] = q10; tris[1][1] = q11; tris[1][2] = q01;
                    }

                    for (int t = 0; ok && t < 2; t++) {
                        const SDL_FPoint *tri = tris[t];
                        float tminx = fminf(tri[0].x, fminf(tri[1].x, tri[2].x));
                        float tmaxx = fmaxf(tri[0].x, fmaxf(tri[1].x, tri[2].x));
                        float tminy = fminf(tri[0].y, fminf(tri[1].y, tri[2].y));
                        float tmaxy = fmaxf(tri[0].y, fmaxf(tri[1].y, tri[2].y));
                        if (tmaxx < minx || tminx > maxx || tmaxy < miny || tminy > maxy)
                            continue;

                        /*
                         * A lattice triangle completely inside gets one flat
                         * shade from its corners, so the polygon's own
                         * triangulation leaves no seams. At the border each
                         * piece is shaded from its own points (height 0 on
                         * the wall edge).
                         */
                        bool whole = tr_point_in_poly(pts, n, tri[0].x, tri[0].y)
                                  && tr_point_in_poly(pts, n, tri[1].x, tri[1].y)
                                  && tr_point_in_poly(pts, n, tri[2].x, tri[2].y);
                        float s_whole = whole
                            ? tr_shade(&tri[0], &tri[1], &tri[2], tr_height(tri[0].x, tri[0].y),
                                       tr_height(tri[1].x, tri[1].y), tr_height(tri[2].x, tri[2].y))
                            : 1.0f;

                        for (int pt = 0; ok && pt < npt; pt++) {
                            SDL_FPoint ptr[3] = { pts[ptri[3 * pt]], pts[ptri[3 * pt + 1]],
                                                  pts[ptri[3 * pt + 2]] };
                            if (fmaxf(ptr[0].x, fmaxf(ptr[1].x, ptr[2].x)) < tminx
                                || fminf(ptr[0].x, fminf(ptr[1].x, ptr[2].x)) > tmaxx
                                || fmaxf(ptr[0].y, fmaxf(ptr[1].y, ptr[2].y)) < tminy
                                || fminf(ptr[0].y, fminf(ptr[1].y, ptr[2].y)) > tmaxy)
                                continue;

                            SDL_FPoint *piece;
                            int m = tr_clip_tri(ptr, 3, tri, ca, cb, &piece);
                            if (m < 3 || fabsf(tr_area(piece, m)) < 0.25f)
                                continue;

                            /* convex: fan from the first point, heights shared */
                            float hp[16];
                            if (!whole)
                                for (int k = 0; k < m; k++)
                                    hp[k] = tr_height(piece[k].x, piece[k].y);
                            for (int k = 1; ok && k + 1 < m; k++) {
                                const SDL_FPoint *a = &piece[0], *b = &piece[k], *c = &piece[k + 1];
                                float s = whole ? s_whole
                                                : tr_shade(a, b, c, hp[0], hp[k], hp[k + 1]);
                                ok = tr_push_vert(tp, a->x, a->y, s)
                                  && tr_push_vert(tp, b->x, b->y, s)
                                  && tr_push_vert(tp, c->x, c->y, s);
                            }
                        }
                    }

                    /* lattice lines: bottom, left and the diagonal of this quad */
                    if (ok)
                        ok = tr_clip_segment(tp, pts, n, q00, q10, ts)
                          && tr_clip_segment(tp, pts, n, q00, q01, ts)
                          && (diag ? tr_clip_segment(tp, pts, n, q10, q01, ts)
                                   : tr_clip_segment(tp, pts, n, q00, q11, ts));
                }
            }

            if (!ok || (tp->num_verts == v0 && tp->num_segs == s0))
                continue;

            if (tp->num_chunks >= tp->max_chunks) {
                int nm = tp->max_chunks ? tp->max_chunks * 2 : 16;
                tr_chunk_t *nc = (tr_chunk_t *)realloc(tp->chunks, sizeof(*nc) * (size_t)nm);
                if (!nc) {
                    ok = false;
                    break;
                }
                tp->chunks = nc;
                tp->max_chunks = nm;
            }
            tr_chunk_t *ch = &tp->chunks[tp->num_chunks++];
            ch->v0 = v0;
            ch->nv = tp->num_verts - v0;
            ch->s0 = s0;
            ch->ns = tp->num_segs - s0;
            ch->minx = ch->miny = 1e30f;
            ch->maxx = ch->maxy = -1e30f;
            for (int k = v0; k < tp->num_verts; k++) {
                ch->minx = fminf(ch->minx, tp->verts[k].x);
                ch->maxx = fmaxf(ch->maxx, tp->verts[k].x);
                ch->miny = fminf(ch->miny, tp->verts[k].y);
                ch->maxy = fmaxf(ch->maxy, tp->verts[k].y);
            }
            for (int k = s0; k < tp->num_segs; k++) {
                const tr_seg_t *sg = &tp->segs[k];
                ch->minx = fminf(ch->minx, fminf(sg->x1, sg->x2));
                ch->maxx = fmaxf(ch->maxx, fmaxf(sg->x1, sg->x2));
                ch->miny = fminf(ch->miny, fminf(sg->y1, sg->y2));
                ch->maxy = fmaxf(ch->maxy, fmaxf(sg->y1, sg->y2));
            }
        }
    }

    free(pts);
    free(ptri);
    free(ts);
    return ok;
}

static void tr_build_one(int i)
{
    tr_poly_t *tp = &tr_polys[i];
    if (!tp->built) {
        tp->built = true;
        tp->ok = polygons[i].num_points >= 3 && tr_build_poly(tp, &polygons[i]);
    }
}

/*
 * Build every polygon that can show hills right away (first frame of a
 * map), so a large wall coming into view later causes no hitch.
 */
static void tr_build_all(void)
{
    for (int i = 0; i < tr_num_polys; i++) {
        const xp_polygon_t *poly = &polygons[i];
        polygon_style_t style = polygon_styles[poly->style];
        if (BIT(style.flags, STYLE_INVISIBLE)
            || !BIT(style.flags, STYLE_FILLED | STYLE_TEXTURED))
            continue;
        tr_build_one(i);
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static SDL_Vertex *tr_vbuf;
static int         tr_vcap;
static int        *tr_ibuf;
static int         tr_icap;

static bool tr_reserve(int nv, int ni)
{
    if (nv > tr_vcap) {
        int c = tr_vcap ? tr_vcap : 4096;
        while (c < nv)
            c *= 2;
        SDL_Vertex *v = (SDL_Vertex *)realloc(tr_vbuf, sizeof(*v) * (size_t)c);
        if (!v)
            return false;
        tr_vbuf = v;
        tr_vcap = c;
    }
    if (ni > tr_icap) {
        int c = tr_icap ? tr_icap : 4096;
        while (c < ni)
            c *= 2;
        int *ix = (int *)realloc(tr_ibuf, sizeof(*ix) * (size_t)c);
        if (!ix)
            return false;
        tr_ibuf = ix;
        tr_icap = c;
    }
    return true;
}

static inline float tr_clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Append the visible facets with shade in [lo, hi), colored by fn. */
static int tr_emit_facets(const tr_poly_t *tp, float bx, float by, float sc,
                          float vx0, float vx1, float vy0, float vy1,
                          float lo, float hi, const SDL_FColor *base)
{
    int nv = 0;

    for (int c = 0; c < tp->num_chunks; c++) {
        const tr_chunk_t *ch = &tp->chunks[c];
        if (ch->maxx < vx0 || ch->minx > vx1 || ch->maxy < vy0 || ch->miny > vy1)
            continue;
        if (!tr_reserve(nv + ch->nv, 0))
            return nv;
        for (int k = 0; k + 2 < ch->nv; k += 3) {
            float sh = tp->verts[ch->v0 + k].shade;
            if (sh < lo || sh >= hi)
                continue;
            SDL_FColor col;
            if (base)       /* plain fill: the shaded fill color */
                col = (SDL_FColor){ tr_clamp01(base->r * sh), tr_clamp01(base->g * sh),
                                    tr_clamp01(base->b * sh), 1.0f };
            else if (sh < 1.0f)     /* texture, MOD pass: darken */
                col = (SDL_FColor){ sh, sh, sh, 1.0f };
            else                    /* texture, ADD pass: a little light */
                col = (SDL_FColor){ (sh - 1.0f) * 0.45f, (sh - 1.0f) * 0.42f,
                                    (sh - 1.0f) * 0.38f, 1.0f };
            for (int q = 0; q < 3; q++) {
                const tr_vert_t *v = &tp->verts[ch->v0 + k + q];
                SDL_Vertex *o = &tr_vbuf[nv++];
                o->position.x = bx + v->x * sc;
                o->position.y = by - v->y * sc;
                o->color = col;
                o->tex_coord = (SDL_FPoint){ 0.0f, 0.0f };
            }
        }
    }
    return nv;
}

/*
 * Draw the landscape of polygon i. xoff, yoff: map wrap copy as in
 * Gui_paint_polygon(). base: the plain fill color, which the facets
 * replace; NULL for a textured polygon, which only gets light and shadow
 * on top so the texture stays visible.
 */
static bool tr_paint(int i, int xoff, int yoff, const SDL_FColor *base)
{
    tr_poly_t *tp;

    if (!fxTerrain || Fx_classic() || !tng_renderer || i < 0 || i >= num_polygons)
        return false;
    if (tr_src != polygons || tr_num_polys != num_polygons || !tr_polys) {
        if (!tr_ensure_cache())
            return false;
        tr_build_all();
    }

    tp = &tr_polys[i];
    tr_build_one(i);
    if (!tp->ok)
        return false;

    const float ox = (float)xoff * (float)Setup->width;
    const float oy = (float)yoff * (float)Setup->height;
    const float sc = clData.fscale;
    const float vx0 = (float)world.x - ox, vx1 = vx0 + (float)ext_view_width;
    const float vy0 = (float)world.y - oy, vy1 = vy0 + (float)ext_view_height;
    const float bx = (float)PLAYFIELD_X - ((float)world.x - ox) * sc;
    const float by = ((float)world.y + (float)ext_view_height - oy) * sc;

    /* facets */
    int nv;
    if (base) {
        nv = tr_emit_facets(tp, bx, by, sc, vx0, vx1, vy0, vy1, -1e9f, 1e9f, base);
        if (nv >= 3)
            SDL_RenderGeometry(tng_renderer, NULL, tr_vbuf, nv, NULL, 0);
    } else {
        nv = tr_emit_facets(tp, bx, by, sc, vx0, vx1, vy0, vy1, -1e9f, 0.999f, NULL);
        if (nv >= 3) {
            SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_MOD);
            SDL_RenderGeometry(tng_renderer, NULL, tr_vbuf, nv, NULL, 0);
        }
        nv = tr_emit_facets(tp, bx, by, sc, vx0, vx1, vy0, vy1, 1.001f, 1e9f, NULL);
        if (nv >= 3) {
            SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_ADD);
            SDL_RenderGeometry(tng_renderer, NULL, tr_vbuf, nv, NULL, 0);
        }
    }
    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);

    /* lattice lines as thin quads: a lighter shade of the fill, or white over textures */
    if (fxTerrainMesh) {
        SDL_FColor lc = base
            ? (SDL_FColor){ tr_clamp01(base->r * 1.7f + 0.04f), tr_clamp01(base->g * 1.7f + 0.04f),
                            tr_clamp01(base->b * 1.7f + 0.04f), 0.30f }
            : (SDL_FColor){ 1.0f, 1.0f, 1.0f, 0.14f };
        float hw = 0.5f;
        int ni = 0;
        nv = 0;
        for (int c = 0; c < tp->num_chunks; c++) {
            const tr_chunk_t *ch = &tp->chunks[c];
            if (ch->ns == 0 || ch->maxx < vx0 || ch->minx > vx1
                || ch->maxy < vy0 || ch->miny > vy1)
                continue;
            if (!tr_reserve(nv + 4 * ch->ns, ni + 6 * ch->ns))
                return true;
            for (int k = 0; k < ch->ns; k++) {
                const tr_seg_t *s = &tp->segs[ch->s0 + k];
                float x1 = bx + s->x1 * sc, y1 = by - s->y1 * sc;
                float x2 = bx + s->x2 * sc, y2 = by - s->y2 * sc;
                float dx = x2 - x1, dy = y2 - y1;
                float len = sqrtf(dx * dx + dy * dy);
                if (len < 0.01f)
                    continue;
                float px = -dy / len * hw, py = dx / len * hw;
                SDL_Vertex *o = &tr_vbuf[nv];
                o[0].position = (SDL_FPoint){ x1 + px, y1 + py };
                o[1].position = (SDL_FPoint){ x2 + px, y2 + py };
                o[2].position = (SDL_FPoint){ x2 - px, y2 - py };
                o[3].position = (SDL_FPoint){ x1 - px, y1 - py };
                for (int q = 0; q < 4; q++) {
                    o[q].color = lc;
                    o[q].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };
                }
                int *ix = &tr_ibuf[ni];
                ix[0] = nv;
                ix[1] = nv + 1;
                ix[2] = nv + 2;
                ix[3] = nv;
                ix[4] = nv + 2;
                ix[5] = nv + 3;
                nv += 4;
                ni += 6;
            }
        }
        if (ni > 0)
            SDL_RenderGeometry(tng_renderer, NULL, tr_vbuf, nv, tr_ibuf, ni);
    }
    return true;
}

/* Hills over a plain filled polygon, replacing the fill color. */
bool Fx_terrain_paint(int i, int xoff, int yoff, SDL_FColor base)
{
    return tr_paint(i, xoff, yoff, &base);
}

/* Light and shadow of the hills over a textured polygon. */
bool Fx_terrain_paint_textured(int i, int xoff, int yoff)
{
    return tr_paint(i, xoff, yoff, NULL);
}
