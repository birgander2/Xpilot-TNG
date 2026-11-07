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

static xp_option_t ship_fx_options[] = {
    XP_BOOL_OPTION("fxShipHull", true, &fxShipHull, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw ships with a glossy hull and a rim-lit outline.\n"),
    XP_BOOL_OPTION("fxShipHalo", true, &fxShipHalo, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Soft glow around ships in team play: blue for your own team.\n"),
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
bool Fx_paint_ship_hull(const SDL_FPoint *pts, int n, float cx, float cy,
                        int color, int halo_color, bool fill)
{
    SDL_Vertex v[SH_MAX_PTS * 3];
    int idx[SH_MAX_PTS * 3];

    if (Fx_classic() || !tng_renderer || n < 3 || n > SH_MAX_PTS)
        return false;

    if (fxShipHalo && halo_color > 0) {
        SDL_FColor hc = SDL3_Get_FColor(halo_color);
        hc.a = 0.32f;
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
        const SDL_FColor tint = { base.r * 0.62f, base.g * 0.62f, base.b * 0.62f, 1.0f };
        const float inv = 0.5f / rmax;
        for (int k = 0; k < ni; k++) {
            SDL_FPoint q = pts[idx[k]];
            v[k].position = q;
            v[k].color = tint;
            v[k].tex_coord = (SDL_FPoint){ 0.5f + (q.x - cx) * inv, 0.5f + (q.y - cy) * inv };
        }
        SDL_RenderGeometry(tng_renderer, sh_dome, v, ni, NULL, 0);
        for (int k = 0; k < ni; k++)
            v[k].color = (SDL_FColor){ 1.0f, 1.0f, 1.0f, 0.85f };
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
                               (Uint8)(sh_clamp01(base.b * k) * 255.0f), 255);
        SDL_RenderLine(tng_renderer, a.x, a.y, b.x, b.y);
    }
    return true;
}
