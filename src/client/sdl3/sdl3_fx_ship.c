/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Ship hull effects: glossy hull with rim-lit outline, and team halo.
 *
 * The shipshape is never changed: the outline is still exactly the
 * collision shape, only its brightness follows the light. The hull is
 * filled with a dome-shaped light texture that stays in screen
 * orientation, so the highlight stays put while the ship turns. That
 * works for any outline, convex or not, and at any size.
 * Purely cosmetic, client side only.
 */

#include "sdl3_client.h"

static bool fxShipHull = true;
static bool fxShipHalo = true;
static bool fxShield = true;

static xp_option_t ship_fx_options[] = {
    XP_BOOL_OPTION("fxShipHull", true, &fxShipHull, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw ships with a glossy hull and a rim-lit outline; phased ships\n"
                   "half transparent, cloaked ones as running glowing dashes.\n"),
    XP_BOOL_OPTION("fxShipHalo", true, &fxShipHalo, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Soft glow around ships in team play: blue for your own team.\n"),
    XP_BOOL_OPTION("fxShield", true, &fxShield, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Shield as a pale dashed hexagon that flashes on hits and wall\n"
                   "contact; armor shows it briefly when hit. Deflector as turning,\n"
                   "softly glowing arcs.\n"),
};

void Store_ship_fx_options(void)
{
    STORE_OPTIONS(ship_fx_options);
}

/* light from the upper left of the screen (x right, y down, z to the viewer) */
static const float sh_light[3] = { -0.50f, -0.60f, 0.62f };

#define SH_MAX_PTS      64
#define SH_DOME         128     /* dome texture size */

static SDL_Texture *sh_dome;    /* diffuse light of a flattened dome, luminance */
static SDL_Texture *sh_spec;    /* its highlight, white with alpha */
static bool         sh_tex_failed;

static inline float sh_clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void Fx_ship_cleanup(void)
{
    if (sh_dome) {
        SDL_DestroyTexture(sh_dome);
        sh_dome = NULL;
    }
    if (sh_spec) {
        SDL_DestroyTexture(sh_spec);
        sh_spec = NULL;
    }
    sh_tex_failed = false;
}

static bool sh_make_textures(void)
{
    static Uint8 dome[SH_DOME * SH_DOME * 4], spec[SH_DOME * SH_DOME * 4];
    float ll = sqrtf(sh_light[0] * sh_light[0] + sh_light[1] * sh_light[1]
                     + sh_light[2] * sh_light[2]);
    float lx = sh_light[0] / ll, ly = sh_light[1] / ll, lz = sh_light[2] / ll;
    /* half vector between light and viewer for the highlight */
    float hx = lx, hy = ly, hz = lz + 1.0f;
    float hl = sqrtf(hx * hx + hy * hy + hz * hz);

    if (sh_dome)
        return true;
    if (sh_tex_failed || !tng_renderer)
        return false;

    hx /= hl;
    hy /= hl;
    hz /= hl;
    for (int y = 0; y < SH_DOME; y++) {
        for (int x = 0; x < SH_DOME; x++) {
            float u = ((float)x + 0.5f) / SH_DOME * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / SH_DOME * 2.0f - 1.0f;
            float r = sqrtf(u * u + v * v);
            if (r > 0.999f) {
                u *= 0.999f / r;
                v *= 0.999f / r;
                r = 0.999f;
            }
            /* a flattened dome: a hull, not a ball */
            float nx = u * 0.8f, ny = v * 0.8f, nz = sqrtf(1.0f - r * r) + 0.35f;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= nl;
            ny /= nl;
            nz /= nl;
            float diff = sh_clamp01(nx * lx + ny * ly + nz * lz);
            float lum = sh_clamp01(0.10f + 0.85f * diff * diff);
            float hi = powf(sh_clamp01(nx * hx + ny * hy + nz * hz), 40.0f);

            Uint8 *o = &dome[(y * SH_DOME + x) * 4];
            o[0] = o[1] = o[2] = (Uint8)(lum * 255.0f + 0.5f);
            o[3] = 255;
            o = &spec[(y * SH_DOME + x) * 4];
            o[0] = o[1] = o[2] = 255;
            o[3] = (Uint8)(sh_clamp01(hi * 0.9f) * 255.0f + 0.5f);
        }
    }

    sh_dome = SDL_CreateTexture(tng_renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, SH_DOME, SH_DOME);
    sh_spec = SDL_CreateTexture(tng_renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, SH_DOME, SH_DOME);
    if (!sh_dome || !sh_spec) {
        warn("FX: no ship hull textures: %s", SDL_GetError());
        Fx_ship_cleanup();
        sh_tex_failed = true;
        return false;
    }
    SDL_UpdateTexture(sh_dome, NULL, dome, SH_DOME * 4);
    SDL_UpdateTexture(sh_spec, NULL, spec, SH_DOME * 4);
    SDL_SetTextureScaleMode(sh_dome, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureScaleMode(sh_spec, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(sh_dome, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(sh_spec, SDL_BLENDMODE_ADD);
    return true;
}

/*
 * pts: screen outline of the ship (n unique points), (cx, cy): screen
 * position of the ship center, color: ship palette color,
 * halo_color: palette color for the team halo, 0 for none,
 * fill: draw the hull (false while the ship is shown solid, e.g. locked).
 * Returns true if the outline was drawn here (rim-lit), so the caller
 * must not draw its plain outline.
 */
static bool sh_paint_hull(const SDL_FPoint *pts, int n, float cx, float cy,
                          int color, int halo_color, bool fill, float alpha)
{
    SDL_Vertex v[SH_MAX_PTS * 3];
    int idx[SH_MAX_PTS * 3];

    if (Fx_classic() || !tng_renderer || n < 3 || n > SH_MAX_PTS)
        return false;

    if (fxShipHalo && halo_color > 0) {
        SDL_FColor hc = SDL3_Get_FColor(halo_color);
        hc.a = 0.32f * alpha;
        Fx_draw_outline_glow(pts, n, hc, 6.0f * clData.fscale);
    }

    if (!fxShipHull || !fill || !sh_make_textures())
        return false;

    SDL_FColor base = SDL3_Get_FColor(color);
    float rmax = 1.0f, area2 = 0.0f;
    for (int i = 0; i < n; i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % n];
        rmax = fmaxf(rmax, sqrtf((a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy)));
        area2 += a.x * b.y - b.x * a.y;
    }

    /* hull: dome light mapped in screen orientation over the ship */
    int ni = SDL3_Triangulate_Polygon(pts, n, idx, SH_MAX_PTS * 3);
    if (ni >= 3) {
        const SDL_FColor tint = { base.r * 0.62f, base.g * 0.62f, base.b * 0.62f, alpha };
        const float inv = 0.5f / rmax;
        for (int k = 0; k < ni; k++) {
            SDL_FPoint q = pts[idx[k]];
            v[k].position = q;
            v[k].color = tint;
            v[k].tex_coord = (SDL_FPoint){ 0.5f + (q.x - cx) * inv, 0.5f + (q.y - cy) * inv };
        }
        SDL_RenderGeometry(tng_renderer, sh_dome, v, ni, NULL, 0);
        for (int k = 0; k < ni; k++)
            v[k].color = (SDL_FColor){ 1.0f, 1.0f, 1.0f, 0.85f * alpha };
        SDL_RenderGeometry(tng_renderer, sh_spec, v, ni, NULL, 0);
    }

    /* outline, lit by the same light: full color towards it, dim away from it */
    float lx = sh_light[0], ly = sh_light[1], ll = sqrtf(lx * lx + ly * ly);
    lx /= ll;
    ly /= ll;
    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < n; i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % n];
        float ex = b.x - a.x, ey = b.y - a.y, el = sqrtf(ex * ex + ey * ey);
        if (el < 1e-4f)
            continue;
        /* outward normal; screen y points down */
        float nx = ey / el, ny = -ex / el;
        if (area2 < 0.0f) {
            nx = -nx;
            ny = -ny;
        }
        float k = 0.35f + 0.65f * (0.5f + 0.5f * (nx * lx + ny * ly));
        SDL_SetRenderDrawColor(tng_renderer, (Uint8)(sh_clamp01(base.r * k) * 255.0f),
                               (Uint8)(sh_clamp01(base.g * k) * 255.0f),
                               (Uint8)(sh_clamp01(base.b * k) * 255.0f),
                               (Uint8)(sh_clamp01(alpha) * 255.0f));
        SDL_RenderLine(tng_renderer, a.x, a.y, b.x, b.y);
    }
    return true;
}

bool Fx_paint_ship_hull(const SDL_FPoint *pts, int n, float cx, float cy,
                        int color, int halo_color, bool fill)
{
    return sh_paint_hull(pts, n, cx, cy, color, halo_color, fill, 1.0f);
}

/*
 * Phased or cloaked ship (screen outline pts, n unique points, center cx, cy).
 * Phased: the normal ship, mostly transparent (30 %). Cloaked: a faint
 * hull (25 %) and soft glowing dashes slowly running along the outline.
 * Returns false if the classic look should be drawn instead.
 */
bool Fx_paint_ship_ghost(const SDL_FPoint *pts, int n, float cx, float cy,
                         int color, bool cloak)
{
    if (!fxShipHull || Fx_classic() || !tng_renderer || n < 3 || n > SH_MAX_PTS)
        return false;

    if (!cloak)
        return sh_paint_hull(pts, n, cx, cy, color, 0, true, 0.3f);

    sh_paint_hull(pts, n, cx, cy, color, 0, true, 0.25f);

    /* running dashes: walk the outline, dash 5 px, gap 4 px, moving along */
    SDL_FColor fc = SDL3_Get_FColor(color);
    const float sc = clData.fscale;
    const float dash = 5.0f * sc, gap = 4.0f * sc, period = dash + gap;
    float t = (float)(SDL_GetTicksNS() % (100ull * SDL_NS_PER_SECOND)) / 1e9f;
    float pos = -fmodf(t * 18.0f * sc, period);        /* phase along the outline */
    SDL_FPoint seg[16];

    for (int i = 0; i < n; i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % n];
        float ex = b.x - a.x, ey = b.y - a.y, el = sqrtf(ex * ex + ey * ey);
        if (el < 0.01f) {
            continue;
        }
        /* dashes overlapping this edge, edge-local start s0 = pos */
        for (float s0 = pos; s0 < el; s0 += period) {
            float d0 = fmaxf(s0, 0.0f), d1 = fminf(s0 + dash, el);
            if (d1 <= d0)
                continue;
            seg[0] = (SDL_FPoint){ a.x + ex * d0 / el, a.y + ey * d0 / el };
            seg[1] = (SDL_FPoint){ a.x + ex * d1 / el, a.y + ey * d1 / el };
            Fx_draw_polyline_glow(seg, 2, false, (SDL_FColor){ fc.r, fc.g, fc.b, 0.22f }, 4.0f * sc);
            SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(tng_renderer, (Uint8)(fc.r * 255.0f), (Uint8)(fc.g * 255.0f),
                                   (Uint8)(fc.b * 255.0f), 100);
            SDL_RenderLine(tng_renderer, seg[0].x, seg[0].y, seg[1].x, seg[1].y);
        }
        /* carry the dash phase over the corner */
        pos = fmodf(pos - el, period);
        if (pos > 0.0f)
            pos -= period;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Shield                                                             */
/* ------------------------------------------------------------------ */

bool Fx_shield_enabled(void)
{
    return fxShield && !Fx_classic();
}

/* Pointy-top hexagon, circumradius r. */
static void sh_hexagon(float cx, float cy, float r, SDL_FPoint out[6])
{
    for (int i = 0; i < 6; i++) {
        float a = (float)M_PI / 6.0f + (float)i * (float)M_PI / 3.0f;
        out[i] = (SDL_FPoint){ cx + r * cosf(a), cy + r * sinf(a) };
    }
}

/* Three dashes per edge, open at the corners. */
static void sh_dashed_hexagon(const SDL_FPoint h[6], SDL_Color c)
{
    static const float dash[3][2] = { { 0.06f, 0.28f }, { 0.39f, 0.61f }, { 0.72f, 0.94f } };

    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(tng_renderer, c.r, c.g, c.b, c.a);
    for (int i = 0; i < 6; i++) {
        SDL_FPoint a = h[i], b = h[(i + 1) % 6];
        for (int d = 0; d < 3; d++) {
            float t0 = dash[d][0], t1 = dash[d][1];
            SDL_RenderLine(tng_renderer, a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0,
                           a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1);
        }
    }
}

/*
 * Shield around a ship at screen (cx, cy). radius: radius of the classic
 * shield circle in screen pixels. shield: shield or emergency shield up
 * (both look the same), flash: 0..1 hit flash; without shield a flash
 * means the armor took a hit and shows the hexagon briefly.
 */
void Fx_paint_shield(float cx, float cy, float radius, int color,
                     bool shield, float flash)
{
    SDL_FPoint h[6];
    SDL_FColor fc;
    float r = radius * 1.1f;
    float alpha;

    if (!tng_renderer || radius < 1.0f || (!shield && flash <= 0.0f))
        return;

    fc = SDL3_Get_FColor(color);
    alpha = shield ? 0.32f + 0.68f * flash : 0.9f * flash;
    sh_hexagon(cx, cy, r, h);

    if (flash > 0.0f) {
        SDL_FColor gc = { fc.r, fc.g, fc.b, 0.5f * flash };
        Fx_draw_outline_glow(h, 6, gc, 5.0f * clData.fscale);
    }

    SDL_Color c = { (Uint8)(fc.r * 255.0f), (Uint8)(fc.g * 255.0f), (Uint8)(fc.b * 255.0f),
                    (Uint8)(sh_clamp01(alpha) * 255.0f) };
    sh_dashed_hexagon(h, c);
}

/*
 * Deflector: eight arc pieces with a soft glow that turn slowly around the
 * ship, their color drifting smoothly between the classic red and blue.
 * radius: radius of the classic deflector circle in screen pixels.
 */
void Fx_paint_deflector(float cx, float cy, float radius)
{
    const int arcs = 8, steps = 4;
    float t = (float)(SDL_GetTicksNS() % (1000ull * SDL_NS_PER_SECOND)) / 1e9f;
    float m = 0.5f + 0.5f * sinf(t * 5.0f);
    float r = radius * 1.15f;      /* clear of the shield hexagon */
    float rot = t * 2.2f;

    if (!tng_renderer || radius < 1.0f)
        return;

    SDL_FColor a = SDL3_Get_FColor(RED), b = SDL3_Get_FColor(BLUE);
    SDL_FColor c = { a.r + (b.r - a.r) * m, a.g + (b.g - a.g) * m, a.b + (b.b - a.b) * m, 0.55f };
    SDL_Color lc = { (Uint8)(c.r * 255.0f), (Uint8)(c.g * 255.0f), (Uint8)(c.b * 255.0f), 196 };

    for (int i = 0; i < arcs; i++) {
        SDL_FPoint p[steps + 1];
        float a0 = rot + (float)i * 2.0f * (float)M_PI / (float)arcs;
        float span = 2.0f * (float)M_PI / (float)arcs * 0.55f;
        for (int k = 0; k <= steps; k++) {
            float ang = a0 + span * (float)k / (float)steps;
            p[k] = (SDL_FPoint){ cx + r * cosf(ang), cy + r * sinf(ang) };
        }
        Fx_draw_polyline_glow(p, steps + 1, false, c, 2.4f * clData.fscale);
        SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(tng_renderer, lc.r, lc.g, lc.b, lc.a);
        SDL_RenderLines(tng_renderer, p, steps + 1);
    }
}
