/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Bitmap & Texture Management Implementation
 */

#include "sdl3_bitmaps.h"

typedef struct {
    char         *filename;
    int           count;
    bool          scalable;
    bool          is_opaque;
    bool          loaded;
    xp_picture_t  picture;
    SDL_Texture **textures;
} tng_pixmap_t;

static const struct {
    const char *filename;
    int count;
    bool is_opaque;
} default_object_pixmaps[NUM_OBJECT_BITMAPS] = {
    { "holder1.ppm", 1, false },
    { "holder2.ppm", 1, false },
    { "ball.ppm", 1, false },
    { "ship_red.ppm", 128, false },
    { "ship_blue.ppm", 128, false },
    { "ship_red2.ppm", 128, false },
    { "bullet.ppm", -16, false },
    { "bullet_blue.ppm", -16, false },
    { "base_down.ppm", 1, false },
    { "base_left.ppm", 1, false },
    { "base_up.ppm", 1, false },
    { "base_right.ppm", 1, false },
    { "fuelcell.ppm", 1, false },
    { "fuel2.ppm", -16, false },
    { "allitems.ppm", -30, false },
    { "cannon_down.ppm", 1, false },
    { "cannon_left.ppm", 1, false },
    { "cannon_up.ppm", 1, false },
    { "cannon_right.ppm", 1, false },
    { "sparks.ppm", -8, false },
    { "paused.ppm", -2, false },
    { "wall_top.ppm", 1, true },
    { "wall_left.ppm", 1, true },
    { "wall_bottom.ppm", 1, true },
    { "wall_right.ppm", 1, true },
    { "wall_ul.ppm", 1, true },
    { "wall_ur.ppm", 1, true },
    { "wall_dl.ppm", 1, true },
    { "wall_dr.ppm", 1, true },
    { "wall_fi.ppm", 1, true },
    { "wall_url.ppm", 1, true },
    { "wall_ull.ppm", 1, true },
    { "clouds.ppm", 1, true },
    { "logo.ppm", 1, true },
    { "refuel.ppm", -4, false },
    { "wormhole.ppm", 8, false },
    { "mine_team.ppm", 1, false },
    { "mine_other.ppm", 1, false },
    { "concentrator.ppm", 32, false },
    { "plus.ppm", 1, false },
    { "minus.ppm", 1, false },
    { "checkpoint.ppm", -2, false },
    { "meter.ppm", -2, false },
    { "asteroidconcentrator.ppm", 32, false },
    { "ball_gray16.ppm", -16, false }
};

static tng_pixmap_t *tng_pixmaps = NULL;
static int num_tng_pixmaps = 0;
static int max_tng_pixmaps = 0;

int Bitmaps_init(void)
{
    /* Set up robust texture paths with absolute paths */
    char full_rtp[8192];
    char abs_lib_textures[PATH_MAX] = "";
    if (realpath("lib/textures", abs_lib_textures) == NULL) {
        strlcpy(abs_lib_textures, "/data/Work/Xpilot-5X/Xpilot-TNG/lib/textures", sizeof(abs_lib_textures));
    }

    snprintf(full_rtp, sizeof(full_rtp), "%s:/data/Work/Xpilot-5X/NG/share/xpilot-ng/textures:%s",
             abs_lib_textures, Conf_texturedir());

    if (realTexturePath == NULL) {
        realTexturePath = xp_safe_strdup(full_rtp);
    } else {
        char *comb = (char *)malloc(strlen(full_rtp) + strlen(realTexturePath) + 2);
        if (comb) {
            sprintf(comb, "%s:%s", full_rtp, realTexturePath);
            free(realTexturePath);
            realTexturePath = comb;
        }
    }

    if (texturePath == NULL) {
        texturePath = xp_safe_strdup(realTexturePath);
    }

    /* Free any previous pixmaps array */
    Bitmaps_cleanup();

    num_tng_pixmaps = 0;
    max_tng_pixmaps = NUM_OBJECT_BITMAPS + 32;
    tng_pixmaps = (tng_pixmap_t *)calloc(max_tng_pixmaps, sizeof(tng_pixmap_t));
    if (!tng_pixmaps) {
        fprintf(stderr, "Failed to allocate memory for bitmaps.\n");
        return -1;
    }

    for (int i = 0; i < NUM_OBJECT_BITMAPS; i++) {
        tng_pixmaps[i].filename = xp_safe_strdup(default_object_pixmaps[i].filename);
        tng_pixmaps[i].count = default_object_pixmaps[i].count;
        tng_pixmaps[i].scalable = (i == BM_LOGO || i == BM_SCORE_BG) ? false : true;
        tng_pixmaps[i].is_opaque = default_object_pixmaps[i].is_opaque;
        tng_pixmaps[i].loaded = false;
        tng_pixmaps[i].textures = NULL;
    }
    num_tng_pixmaps = NUM_OBJECT_BITMAPS;

    return 0;
}

void Bitmaps_cleanup(void)
{
    if (!tng_pixmaps) return;

    for (int i = 0; i < num_tng_pixmaps; i++) {
        if (tng_pixmaps[i].filename) {
            free(tng_pixmaps[i].filename);
            tng_pixmaps[i].filename = NULL;
        }
        if (tng_pixmaps[i].loaded) {
            int count = ABS(tng_pixmaps[i].count);
            if (tng_pixmaps[i].textures) {
                for (int f = 0; f < count; f++) {
                    if (tng_pixmaps[i].textures[f]) {
                        SDL_DestroyTexture(tng_pixmaps[i].textures[f]);
                    }
                }
                free(tng_pixmaps[i].textures);
                tng_pixmaps[i].textures = NULL;
            }
            if (tng_pixmaps[i].picture.data) {
                for (int f = 0; f < count; f++) {
                    if (tng_pixmaps[i].picture.data[f]) {
                        free(tng_pixmaps[i].picture.data[f]);
                    }
                }
                free(tng_pixmaps[i].picture.data);
                tng_pixmaps[i].picture.data = NULL;
            }
            if (tng_pixmaps[i].picture.bbox) {
                free(tng_pixmaps[i].picture.bbox);
                tng_pixmaps[i].picture.bbox = NULL;
            }
            tng_pixmaps[i].loaded = false;
        }
    }
    free(tng_pixmaps);
    tng_pixmaps = NULL;
    num_tng_pixmaps = 0;
    max_tng_pixmaps = 0;
}

int Bitmap_add(const char *filename, int count, bool scalable)
{
    if (num_tng_pixmaps >= max_tng_pixmaps) {
        int new_max = (max_tng_pixmaps == 0) ? 64 : max_tng_pixmaps * 2;
        tng_pixmap_t *new_arr = (tng_pixmap_t *)realloc(tng_pixmaps, new_max * sizeof(tng_pixmap_t));
        if (!new_arr) {
            fprintf(stderr, "Failed to expand pixmaps array for %s\n", filename);
            return -1;
        }
        tng_pixmaps = new_arr;
        max_tng_pixmaps = new_max;
    }

    int idx = num_tng_pixmaps;
    tng_pixmaps[idx].filename = xp_safe_strdup(filename);
    tng_pixmaps[idx].count = count;
    tng_pixmaps[idx].scalable = scalable;
    tng_pixmaps[idx].is_opaque = true; /* Map wall textures are opaque */
    tng_pixmaps[idx].loaded = false;
    tng_pixmaps[idx].textures = NULL;
    num_tng_pixmaps++;

    return idx;
}

SDL_Texture *Bitmap_get_texture(int img, int frame, int *out_w, int *out_h)
{
    if (img < 0 || img >= num_tng_pixmaps || !tng_renderer) {
        return NULL;
    }

    tng_pixmap_t *pm = &tng_pixmaps[img];

    if (!pm->loaded) {
        if (Picture_init(&pm->picture, pm->filename, pm->count) != 0) {
            /* Try with lowercase/fallback if error */
            return NULL;
        }
        int count = ABS(pm->count);
        pm->textures = (SDL_Texture **)calloc(count, sizeof(SDL_Texture *));
        pm->loaded = true;
    }

    int total_frames = ABS(pm->count);
    if (frame < 0 || frame >= total_frames) {
        frame = 0;
    }

    if (!pm->textures[frame]) {
        int w = (int)pm->picture.width;
        int h = (int)pm->picture.height;
        if (w <= 0 || h <= 0) return NULL;

        SDL_Surface *surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
        if (!surf) return NULL;

        Uint32 *pixels = (Uint32 *)surf->pixels;
        int pitch_words = surf->pitch / 4;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                RGB_COLOR c = Picture_get_pixel(&pm->picture, frame, x, y);
                Uint8 r = (Uint8)RED_VALUE(c);
                Uint8 g = (Uint8)GREEN_VALUE(c);
                Uint8 b = (Uint8)BLUE_VALUE(c);
                Uint8 a = 255;

                if (!pm->is_opaque && c == 0) {
                    a = 0; /* Color 0 is transparent key for sprites */
                }

                pixels[y * pitch_words + x] = ((Uint32)a << 24) |
                                              ((Uint32)r << 16) |
                                              ((Uint32)g << 8)  |
                                              ((Uint32)b << 0);
            }
        }

        SDL_Texture *tex = SDL_CreateTextureFromSurface(tng_renderer, surf);
        SDL_DestroySurface(surf);

        if (tex) {
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(tex, pm->is_opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
            pm->textures[frame] = tex;
        }
    }

    if (out_w) *out_w = (int)pm->picture.width;
    if (out_h) *out_h = (int)pm->picture.height;

    return pm->textures[frame];
}

void SDL3_Bitmap_Paint(int img, int frame, float x, float y)
{
    int w = 0, h = 0;
    SDL_Texture *tex = Bitmap_get_texture(img, frame, &w, &h);
    if (!tex || !tng_renderer) return;

    float dw = tng_pixmaps[img].scalable ? (float)UWINSCALE(w) : (float)w;
    float dh = tng_pixmaps[img].scalable ? (float)UWINSCALE(h) : (float)h;
    SDL_FRect dst = { x, y, dw, dh };
    SDL_RenderTexture(tng_renderer, tex, NULL, &dst);
}

void SDL3_Bitmap_Paint_Blended(int img, int frame, float x, float y, Uint32 rgb)
{
    int w = 0, h = 0;
    SDL_Texture *tex = Bitmap_get_texture(img, frame, &w, &h);
    if (!tex || !tng_renderer) return;

    Uint8 r = (Uint8)((rgb >> 16) & 0xff);
    Uint8 g = (Uint8)((rgb >> 8) & 0xff);
    Uint8 b = (Uint8)(rgb & 0xff);
    SDL_SetTextureColorMod(tex, r, g, b);

    float dw = tng_pixmaps[img].scalable ? (float)UWINSCALE(w) : (float)w;
    float dh = tng_pixmaps[img].scalable ? (float)UWINSCALE(h) : (float)h;
    SDL_FRect dst = { x, y, dw, dh };
    SDL_RenderTexture(tng_renderer, tex, NULL, &dst);

    SDL_SetTextureColorMod(tex, 255, 255, 255);
}

void SDL3_Bitmap_Paint_Area(int img, int frame, float x, float y, const irec_t *area)
{
    int w = 0, h = 0;
    SDL_Texture *tex = Bitmap_get_texture(img, frame, &w, &h);
    if (!tex || !tng_renderer || !area) return;

    SDL_FRect src = { (float)area->x, (float)area->y, (float)area->w, (float)area->h };
    float dw = tng_pixmaps[img].scalable ? (float)UWINSCALE(area->w) : (float)area->w;
    float dh = tng_pixmaps[img].scalable ? (float)UWINSCALE(area->h) : (float)area->h;
    SDL_FRect dst = { x, y, dw, dh };
    SDL_RenderTexture(tng_renderer, tex, &src, &dst);
}

void SDL3_Bitmap_Paint_Scaled(int img, int frame, float x, float y, float w, float h)
{
    SDL_Texture *tex = Bitmap_get_texture(img, frame, NULL, NULL);
    if (!tex || !tng_renderer) return;

    SDL_FRect dst = { x, y, w, h };
    SDL_RenderTexture(tng_renderer, tex, NULL, &dst);
}
