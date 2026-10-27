/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Anti-Aliased Monospace TrueType Font Renderer (SDL3_ttf + Fallback)
 */

#include "sdl3_client.h"
#include "SDL_gfxPrimitives_font.h"
#include <SDL3_ttf/SDL_ttf.h>

#define ATLAS_COLS      16
#define ATLAS_ROWS      16
#define ATLAS_CELL_W    16
#define ATLAS_CELL_H    24
#define DEFAULT_PTSIZE  12.0f

static SDL_Texture *font_texture = NULL;
static float        glyph_advance = 8.0f;
static float        font_y_offset = 0.0f;
static float        glyph_w[256];
static float        glyph_h[256];
static bool         ttf_initialized = false;
static SDL_Texture *prop_texture = NULL;
static void         Init_Prop_Font(void);

static TTF_Font *Open_Bundled_Font(float ptsize)
{
    const char *rel_candidates[] = {
        "lib/fonts/IBMPlexMono-Medium.ttf",
        "../lib/fonts/IBMPlexMono-Medium.ttf",
        "lib/fonts/VeraMoBd.ttf",
        "../lib/fonts/VeraMoBd.ttf",
        NULL
    };

    /* 1. Try relative to current working directory */
    for (int i = 0; rel_candidates[i] != NULL; i++) {
        TTF_Font *f = TTF_OpenFont(rel_candidates[i], ptsize);
        if (f) return f;
    }

    /* 2. Try relative to executable directory */
    const char *base_path = SDL_GetBasePath();
    if (base_path) {
        char full_path[1024];
        for (int i = 0; rel_candidates[i] != NULL; i++) {
            snprintf(full_path, sizeof(full_path), "%s%s", base_path, rel_candidates[i]);
            TTF_Font *f = TTF_OpenFont(full_path, ptsize);
            if (f) return f;
        }
    }

    /* 2b. Try the resolved data directory ($XPILOT_DATADIR, lib/, CONF_DATADIR) */
    {
        static const char *const names[] = {
            "fonts/IBMPlexMono-Medium.ttf", "fonts/VeraMoBd.ttf", NULL
        };
        char full_path[PATH_MAX];
        for (int i = 0; names[i] != NULL; i++) {
            snprintf(full_path, sizeof(full_path), "%s%s", Conf_datadir(), names[i]);
            TTF_Font *f = TTF_OpenFont(full_path, ptsize);
            if (f) return f;
        }
    }

    /* 3. Try installed CONF_DATADIR and system fallback paths */
    const char *abs_candidates[] = {
#ifdef CONF_DATADIR
        CONF_DATADIR "fonts/IBMPlexMono-Medium.ttf",
        CONF_DATADIR "fonts/VeraMoBd.ttf",
#endif
        "/usr/share/fonts/truetype/ibm-plex/IBMPlexMono-Medium.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        NULL
    };

    for (int i = 0; abs_candidates[i] != NULL; i++) {
        TTF_Font *f = TTF_OpenFont(abs_candidates[i], ptsize);
        if (f) return f;
    }

    return NULL;
}

static int Init_Fallback_Bitmap_Font(void)
{
    SDL_Surface *surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        fprintf(stderr, "Failed to create fallback font surface: %s\n", SDL_GetError());
        return -1;
    }

    Uint32 *pixels = (Uint32 *)surface->pixels;
    int pitch_pixels = surface->pitch / (int)sizeof(Uint32);
    memset(pixels, 0, (size_t)(surface->pitch * 128));

    for (int c = 0; c < 256; c++) {
        int cell_x = (c % 16) * 8;
        int cell_y = (c / 16) * 8;
        const unsigned char *glyph = &gfxPrimitivesFontdata[c * 8];

        for (int y = 0; y < 8; y++) {
            unsigned char byte = glyph[y];
            for (int x = 0; x < 8; x++) {
                if ((byte >> (7 - x)) & 1) {
                    pixels[(cell_y + y) * pitch_pixels + (cell_x + x)] = 0xFFFFFFFF;
                }
            }
        }
        glyph_w[c] = 8.0f;
        glyph_h[c] = 8.0f;
    }

    glyph_advance = 8.0f;
    font_y_offset = 0.0f;

    font_texture = SDL_CreateTextureFromSurface(tng_renderer, surface);
    SDL_DestroySurface(surface);
    if (!font_texture) return -1;

    SDL_SetTextureScaleMode(font_texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(font_texture, SDL_BLENDMODE_BLEND);
    return 0;
}

int SDL3_Init_Font(void)
{
    if (font_texture || !tng_renderer)
        return 0;

    if (!ttf_initialized) {
        if (TTF_Init()) {
            ttf_initialized = true;
        }
    }
    Init_Prop_Font();

    TTF_Font *ttf_font = ttf_initialized ? Open_Bundled_Font(DEFAULT_PTSIZE) : NULL;
    if (!ttf_font) {
        fprintf(stderr, "Warning: Could not load bundled TTF font, using fallback 8x8 bitmap font.\n");
        return Init_Fallback_Bitmap_Font();
    }

    TTF_SetFontHinting(ttf_font, TTF_HINTING_LIGHT_SUBPIXEL);

    int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 8;
    if (TTF_GetGlyphMetrics(ttf_font, 'M', &minx, &maxx, &miny, &maxy, &adv) && adv > 0) {
        glyph_advance = (float)adv;
    } else {
        glyph_advance = 8.0f;
    }

    /*
     * Align uppercase cap-height to match the classic 10px UI text box:
     * ascent - maxy gives the internal top leading above capital 'M'.
     */
    int ascent = TTF_GetFontAscent(ttf_font);
    int top_leading = ascent - maxy;
    font_y_offset = (top_leading > 1) ? (float)(top_leading - 1) : 0.0f;

    int atlas_w = ATLAS_COLS * ATLAS_CELL_W;
    int atlas_h = ATLAS_ROWS * ATLAS_CELL_H;
    SDL_Surface *atlas = SDL_CreateSurface(atlas_w, atlas_h, SDL_PIXELFORMAT_RGBA32);
    if (!atlas) {
        TTF_CloseFont(ttf_font);
        return Init_Fallback_Bitmap_Font();
    }

    SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
    memset(atlas->pixels, 0, (size_t)(atlas->pitch * atlas_h));

    SDL_Color white = { 255, 255, 255, 255 };

    for (int c = 0; c < 256; c++) {
        glyph_w[c] = 0.0f;
        glyph_h[c] = 0.0f;

        if (c < 32 || (c >= 127 && c < 160))
            continue;

        SDL_Surface *gsurf = TTF_RenderGlyph_Blended(ttf_font, (Uint32)c, white);
        if (!gsurf)
            continue;

        SDL_SetSurfaceBlendMode(gsurf, SDL_BLENDMODE_NONE);

        int cell_x = (c % ATLAS_COLS) * ATLAS_CELL_W;
        int cell_y = (c / ATLAS_COLS) * ATLAS_CELL_H;
        int copy_w = (gsurf->w < ATLAS_CELL_W) ? gsurf->w : ATLAS_CELL_W;
        int copy_h = (gsurf->h < ATLAS_CELL_H) ? gsurf->h : ATLAS_CELL_H;

        SDL_Rect src_rect = { 0, 0, copy_w, copy_h };
        SDL_Rect dst_rect = { cell_x, cell_y, copy_w, copy_h };
        SDL_BlitSurface(gsurf, &src_rect, atlas, &dst_rect);

        glyph_w[c] = (float)copy_w;
        glyph_h[c] = (float)copy_h;

        SDL_DestroySurface(gsurf);
    }

    TTF_CloseFont(ttf_font);

    font_texture = SDL_CreateTextureFromSurface(tng_renderer, atlas);
    SDL_DestroySurface(atlas);

    if (!font_texture) {
        fprintf(stderr, "Failed to create TTF font atlas texture: %s\n", SDL_GetError());
        return Init_Fallback_Bitmap_Font();
    }

    SDL_SetTextureScaleMode(font_texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(font_texture, SDL_BLENDMODE_BLEND);
    return 0;
}

void SDL3_Cleanup_Font(void)
{
    if (font_texture) {
        SDL_DestroyTexture(font_texture);
        font_texture = NULL;
    }
    if (prop_texture) {
        SDL_DestroyTexture(prop_texture);
        prop_texture = NULL;
    }
    if (ttf_initialized) {
        TTF_Quit();
        ttf_initialized = false;
    }
}

float SDL3_Text_Width_Len(int len)
{
    if (len <= 0) return 0.0f;
    return (float)len * glyph_advance;
}

float SDL3_Text_Width(const char *str)
{
    if (!str) return 0.0f;
    return (float)strlen(str) * glyph_advance;
}

void SDL3_Draw_Text_Len(const char *str, int len, float x, float y, int color_idx)
{
    SDL3_Draw_Text_Len_Color(str, len, x, y, SDL3_Get_Color(color_idx));
}

void SDL3_Draw_Text_Len_Color(const char *str, int len, float x, float y, SDL_Color col)
{
    if (!str || len <= 0 || !tng_renderer || !font_texture)
        return;

    SDL_SetTextureColorMod(font_texture, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(font_texture, col.a);

    float cell_step_x = (glyph_w['M'] > 8.0f || glyph_h['M'] > 8.0f) ? (float)ATLAS_CELL_W : 8.0f;
    float cell_step_y = (glyph_w['M'] > 8.0f || glyph_h['M'] > 8.0f) ? (float)ATLAS_CELL_H : 8.0f;

    float cur_x = SDL_roundf(x);
    float draw_y = SDL_roundf(y - font_y_offset);

    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\n' || c == '\r') {
            break;
        }

        float gw = glyph_w[c];
        float gh = glyph_h[c];
        if (gw > 0.0f && gh > 0.0f) {
            SDL_FRect src = {
                (float)(c % ATLAS_COLS) * cell_step_x,
                (float)(c / ATLAS_COLS) * cell_step_y,
                gw,
                gh
            };
            SDL_FRect dst = {
                cur_x,
                draw_y,
                gw,
                gh
            };
            SDL_RenderTexture(tng_renderer, font_texture, &src, &dst);
        }
        cur_x += glyph_advance;
    }
}

void SDL3_Draw_Text(const char *str, float x, float y, int color_idx)
{
    if (!str) return;
    SDL3_Draw_Text_Len(str, (int)strlen(str), x, y, color_idx);
}

/* ---- Proportional UI font (talk window etc.) ---- */

#define PROP_PTSIZE     13.0f
#define PROP_CELL       28

static float        prop_adv[256];
static float        prop_w[256];
static float        prop_h[256];
static float        prop_height = 0.0f;

static TTF_Font *Open_Font_Named(const char *name, float ptsize)
{
    char path[PATH_MAX];
    TTF_Font *f;

    snprintf(path, sizeof(path), "lib/fonts/%s", name);
    if ((f = TTF_OpenFont(path, ptsize))) return f;
    snprintf(path, sizeof(path), "../lib/fonts/%s", name);
    if ((f = TTF_OpenFont(path, ptsize))) return f;

    const char *base_path = SDL_GetBasePath();
    if (base_path) {
        snprintf(path, sizeof(path), "%slib/fonts/%s", base_path, name);
        if ((f = TTF_OpenFont(path, ptsize))) return f;
        snprintf(path, sizeof(path), "%s../lib/fonts/%s", base_path, name);
        if ((f = TTF_OpenFont(path, ptsize))) return f;
    }

    snprintf(path, sizeof(path), "%sfonts/%s", Conf_datadir(), name);
    if ((f = TTF_OpenFont(path, ptsize))) return f;

#ifdef CONF_DATADIR
    snprintf(path, sizeof(path), CONF_DATADIR "fonts/%s", name);
    if ((f = TTF_OpenFont(path, ptsize))) return f;
#endif
    snprintf(path, sizeof(path), "/usr/share/fonts/truetype/dejavu/%s", name);
    return TTF_OpenFont(path, ptsize);
}

static void Init_Prop_Font(void)
{
    if (prop_texture || !ttf_initialized)
        return;

    TTF_Font *font = Open_Font_Named("DejaVuSans.ttf", PROP_PTSIZE);
    if (!font)
        return;

    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    prop_height = (float)TTF_GetFontHeight(font);

    SDL_Surface *atlas = SDL_CreateSurface(ATLAS_COLS * PROP_CELL, ATLAS_ROWS * PROP_CELL,
                                           SDL_PIXELFORMAT_RGBA32);
    if (!atlas) {
        TTF_CloseFont(font);
        return;
    }
    SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
    memset(atlas->pixels, 0, (size_t)(atlas->pitch * atlas->h));

    SDL_Color white = { 255, 255, 255, 255 };
    for (int c = 0; c < 256; c++) {
        prop_adv[c] = prop_w[c] = prop_h[c] = 0.0f;
        if (c < 32 || (c >= 127 && c < 160))
            continue;

        int adv = 0;
        if (TTF_GetGlyphMetrics(font, (Uint32)c, NULL, NULL, NULL, NULL, &adv))
            prop_adv[c] = (float)adv;

        SDL_Surface *g = TTF_RenderGlyph_Blended(font, (Uint32)c, white);
        if (!g)
            continue;
        SDL_SetSurfaceBlendMode(g, SDL_BLENDMODE_NONE);

        int cw = (g->w < PROP_CELL) ? g->w : PROP_CELL;
        int ch = (g->h < PROP_CELL) ? g->h : PROP_CELL;
        SDL_Rect src = { 0, 0, cw, ch };
        SDL_Rect dst = { (c % ATLAS_COLS) * PROP_CELL, (c / ATLAS_COLS) * PROP_CELL, cw, ch };
        SDL_BlitSurface(g, &src, atlas, &dst);
        prop_w[c] = (float)cw;
        prop_h[c] = (float)ch;
        SDL_DestroySurface(g);
    }
    TTF_CloseFont(font);

    prop_texture = SDL_CreateTextureFromSurface(tng_renderer, atlas);
    SDL_DestroySurface(atlas);
    if (prop_texture) {
        SDL_SetTextureScaleMode(prop_texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(prop_texture, SDL_BLENDMODE_BLEND);
    }
}

float SDL3_Prop_Text_Height(void)
{
    return prop_texture ? prop_height : 10.0f;
}

float SDL3_Prop_Text_Width_Len(const char *str, int len)
{
    if (!str || len <= 0) return 0.0f;
    if (!prop_texture) return SDL3_Text_Width_Len(len);
    float w = 0.0f;
    for (int i = 0; i < len && str[i]; i++)
        w += prop_adv[(unsigned char)str[i]];
    return w;
}

float SDL3_Prop_Text_Width(const char *str)
{
    return str ? SDL3_Prop_Text_Width_Len(str, (int)strlen(str)) : 0.0f;
}

/* Character index closest to horizontal offset rel_x from the string start. */
int SDL3_Prop_Index_At(const char *str, float rel_x)
{
    int len = str ? (int)strlen(str) : 0;
    float x = 0.0f;
    for (int i = 0; i < len; i++) {
        float a = prop_texture ? prop_adv[(unsigned char)str[i]] : SDL3_Text_Width_Len(1);
        if (rel_x < x + a * 0.5f)
            return i;
        x += a;
    }
    return len;
}

/* y is the top of the text line (full font height). */
void SDL3_Prop_Draw_Text(const char *str, float x, float y, int color_idx)
{
    if (!str || !tng_renderer) return;
    if (!prop_texture) {
        SDL3_Draw_Text(str, x, y, color_idx);
        return;
    }

    SDL_Color col = SDL3_Get_Color(color_idx);
    SDL_SetTextureColorMod(prop_texture, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(prop_texture, col.a);

    float cur_x = x;
    float draw_y = SDL_roundf(y);
    for (const char *p = str; *p && *p != '\n' && *p != '\r'; p++) {
        unsigned char c = (unsigned char)*p;
        if (prop_w[c] > 0.0f && prop_h[c] > 0.0f) {
            SDL_FRect src = { (float)(c % ATLAS_COLS) * PROP_CELL,
                              (float)(c / ATLAS_COLS) * PROP_CELL, prop_w[c], prop_h[c] };
            SDL_FRect dst = { SDL_roundf(cur_x), draw_y, prop_w[c], prop_h[c] };
            SDL_RenderTexture(tng_renderer, prop_texture, &src, &dst);
        }
        cur_x += prop_adv[c];
    }
}
