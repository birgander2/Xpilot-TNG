/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Client-side cosmetic effects: fireball explosions and wall sparks.
 *
 * All effects are derived from game state the server already sends
 * (ship positions, wreckage), so no protocol change is needed and the
 * effects also work in recordings. They never influence gameplay.
 *
 * Detection:
 *  - Explosion: a ship vanishes inside the view and wreckage shows up
 *    near its last (extrapolated) position.
 *  - Wall/ship impact: a ship's velocity changes abruptly against its
 *    direction of motion. The impulse direction is the contact normal,
 *    so no wall geometry is needed (works for block and polygon maps).
 *
 * Textures are generated procedurally at startup into one small atlas.
 */

#include "sdl3_client.h"

/* ------------------------------------------------------------------ */
/* Options                                                            */
/* ------------------------------------------------------------------ */

static bool   classicGraphics = false;
static bool   fxExplosions = true;
static bool   fxSparks = true;
static bool   fxContrails = true;
static bool   fxEngineFlame = true;
static bool   fxMineExplosions = true;
static bool   fxMissileTrails = true;
static bool   fxLaserGlow = true;
static bool   fxHyperjump = true;
static bool   fxWormholes = true;
static bool   fxTether = true;
static bool   fxNukeFlash = true;
static bool   fxScreenShake = true;
static double fxAmount = 1.0;

static void fx_clear(void);

static bool Set_classicGraphics(xp_option_t *opt, bool val)
{
    UNUSED_PARAM(opt);
    classicGraphics = val;
    if (val)
        fx_clear();
    return true;
}

static xp_option_t fx_options[] = {
    XP_BOOL_OPTION("classicGraphics", false, &classicGraphics, Set_classicGraphics,
                   XP_OPTFLAG_CONFIG_FX,
                   "Classic look: disable all modern visual effects.\n"),
    XP_BOOL_OPTION("fxExplosions", true, &fxExplosions, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw fireball explosions when ships are destroyed.\n"),
    XP_BOOL_OPTION("fxSparks", true, &fxSparks, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw sparks when ships hit walls or each other.\n"),
    XP_BOOL_OPTION("fxEngineFlame", true, &fxEngineFlame, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw a flickering engine flame while ships thrust.\n"),
    XP_BOOL_OPTION("fxMineExplosions", true, &fxMineExplosions, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Mine explosions: fireball, bright blue for cluster mines,\n"
                   "mushroom cloud for nuclear mines, sized by their strength.\n"),
    XP_BOOL_OPTION("fxMissileTrails", true, &fxMissileTrails, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Smoke trails and exhaust glow behind missiles: smart missiles\n"
                   "yellow with thick white smoke, heat seekers red with dark smoke,\n"
                   "torpedoes blue and almost smokeless.\n"),
    XP_BOOL_OPTION("fxLaserGlow", true, &fxLaserGlow, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw laser pulses as glowing beams with a bright core.\n"),
    XP_BOOL_OPTION("fxHyperjump", true, &fxHyperjump, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Hyperjumps as light streaks: the ship stretches into a streak and\n"
                   "is gone, and shoots in on a streak where it comes out.\n"),
    XP_BOOL_OPTION("fxWormholes", true, &fxWormholes, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Wormholes as glowing tunnels, and a swirling vortex when a ship\n"
                   "goes through.\n"),
    XP_BOOL_OPTION("fxTether", true, &fxTether, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Ball connector as a softly shimmering glowing rope that turns red\n"
                   "the harder it is stretched.\n"),
    XP_BOOL_OPTION("fxNukeFlash", true, &fxNukeFlash, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Brief bright flash of the whole view when a nuke goes off in sight.\n"),
    XP_BOOL_OPTION("fxScreenShake", true, &fxScreenShake, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Shake the view when a nuke goes off anywhere on the map.\n"),
    XP_BOOL_OPTION("fxContrails", true, &fxContrails, NULL,
                   XP_OPTFLAG_CONFIG_FX,
                   "Draw short glowing contrails when ships turn hard at speed.\n"),
    XP_DOUBLE_OPTION("fxAmount", 1.0, 0.0, 2.0, &fxAmount, NULL,
                     XP_OPTFLAG_CONFIG_FX,
                     "Particle amount multiplier for visual effects.\n"),
};

/* True if modern effects are enabled at all. */
static inline bool fx_active(void)
{
    return !classicGraphics && fxAmount > 0.0;
}

void Store_fx_options(void)
{
    STORE_OPTIONS(fx_options);
    Store_terrain_options();
    Store_ship_fx_options();
}

/* True in classic mode: no modern visual effects at all. */
bool Fx_classic(void)
{
    return classicGraphics;
}

/* ------------------------------------------------------------------ */
/* Random numbers (private, does not disturb the game's rfrac())      */
/* ------------------------------------------------------------------ */

static uint32_t fx_seed = 0x9E3779B9u;

static inline float fx_rand(void)
{
    uint32_t x = fx_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    fx_seed = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

static inline float fx_range(float a, float b)
{
    return a + (b - a) * fx_rand();
}

/* random integer in [0, n) */
static inline int fx_rand_int(int n)
{
    int i = (int)(fx_rand() * (float)n);
    return i < n ? i : n - 1;
}

/* ------------------------------------------------------------------ */
/* Procedural texture atlas                                           */
/* ------------------------------------------------------------------ */

#define FX_CELL         128
#define FX_ATLAS_COLS   4
#define FX_ATLAS_ROWS   2

enum {
    CELL_FIRE0  = 0,    /* 0..3: billowy fire puffs */
    CELL_GLOW   = 4,    /* soft gaussian dot */
    CELL_SMOKE0 = 5,    /* 5..6: soft smoke puffs */
    CELL_RING   = 7     /* shockwave ring */
};

static SDL_Texture *fx_atlas;

static float fx_hash2(int x, int y, int seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u
               + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xffffff) / 16777215.0f;
}

static float fx_value_noise(float x, float y, int seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float a = fx_hash2(xi, yi, seed), b = fx_hash2(xi + 1, yi, seed);
    float c = fx_hash2(xi, yi + 1, seed), d = fx_hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

static float fx_fbm(float x, float y, int seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < 4; o++) {
        sum += amp * fx_value_noise(x, y, seed + o * 101);
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

static inline float fx_clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float fx_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fx_smooth(float v)
{
    v = fx_clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

/* Fill one atlas cell; returns luminance and alpha per pixel. */
static void fx_gen_cell(uint8_t *pixels, int pitch, int cell)
{
    int ox = (cell % FX_ATLAS_COLS) * FX_CELL;
    int oy = (cell / FX_ATLAS_COLS) * FX_CELL;

    for (int py = 0; py < FX_CELL; py++) {
        for (int px = 0; px < FX_CELL; px++) {
            float u = ((float)px + 0.5f) / FX_CELL * 2.0f - 1.0f;
            float v = ((float)py + 0.5f) / FX_CELL * 2.0f - 1.0f;
            float r = sqrtf(u * u + v * v);
            float edge = fx_clamp01((1.0f - r) * 4.0f);
            float lum = 1.0f, a = 0.0f;

            if (cell >= CELL_FIRE0 && cell < CELL_FIRE0 + 4) {
                int k = cell - CELL_FIRE0;
                float n  = fx_fbm(u * 2.5f + (float)k * 7.3f, v * 2.5f, 11 + k);
                float n2 = fx_fbm(u * 6.0f, v * 6.0f + (float)k * 3.1f, 31 + k);
                float d = r * (0.7f + 0.65f * (1.0f - n));
                a = fx_smooth(1.0f - d) * (0.6f + 0.4f * n2) * edge;
                lum = fx_clamp01(0.55f + 0.35f * n2 + 0.3f * (1.0f - r));
            } else if (cell == CELL_GLOW) {
                a = expf(-r * r * 5.0f) * edge;
            } else if (cell == CELL_SMOKE0 || cell == CELL_SMOKE0 + 1) {
                int k = cell - CELL_SMOKE0;
                float n  = fx_fbm(u * 1.8f + (float)k * 5.7f, v * 1.8f, 71 + k);
                float n2 = fx_fbm(u * 4.5f, v * 4.5f, 91 + k);
                float d = r * (0.8f + 0.5f * (1.0f - n));
                a = fx_clamp01(1.0f - d);
                a = a * a * (0.55f + 0.45f * n2) * edge;
                lum = 0.75f + 0.25f * n2;
            } else if (cell == CELL_RING) {
                float q = (r - 0.82f) / 0.07f;
                a = (expf(-q * q) + 0.12f * expf(-r * r * 3.0f)) * edge;
            }

            uint8_t *p = pixels + (oy + py) * pitch + (ox + px) * 4;
            uint8_t l = (uint8_t)(fx_clamp01(lum) * 255.0f + 0.5f);
            p[0] = l;
            p[1] = l;
            p[2] = l;
            p[3] = (uint8_t)(fx_clamp01(a) * 255.0f + 0.5f);
        }
    }
}

int Fx_init(void)
{
    const int w = FX_CELL * FX_ATLAS_COLS, h = FX_CELL * FX_ATLAS_ROWS;
    const int pitch = w * 4;
    uint8_t *pixels;

    if (!tng_renderer || fx_atlas)
        return 0;

    pixels = (uint8_t *)calloc((size_t)pitch * (size_t)h, 1);
    if (!pixels)
        return -1;

    for (int c = 0; c < FX_ATLAS_COLS * FX_ATLAS_ROWS; c++)
        fx_gen_cell(pixels, pitch, c);

    fx_atlas = SDL_CreateTexture(tng_renderer, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STATIC, w, h);
    if (fx_atlas) {
        SDL_UpdateTexture(fx_atlas, NULL, pixels, pitch);
        SDL_SetTextureScaleMode(fx_atlas, SDL_SCALEMODE_LINEAR);
    } else {
        warn("FX: could not create effect texture: %s", SDL_GetError());
    }
    free(pixels);

    fx_seed ^= (uint32_t)SDL_GetTicksNS();
    if (!fx_seed)
        fx_seed = 0x9E3779B9u;
    return fx_atlas ? 0 : -1;
}

void Fx_cleanup(void)
{
    if (fx_atlas) {
        SDL_DestroyTexture(fx_atlas);
        fx_atlas = NULL;
    }
    Fx_terrain_cleanup();
    Fx_ship_cleanup();
}

/* ------------------------------------------------------------------ */
/* Particles                                                          */
/* ------------------------------------------------------------------ */

#define FX_BLUE 1           /* particle color variants */
#define FX_SMOKE_LIGHT 2    /* puffs: smart missile */
#define FX_SMOKE_DARK  3    /* puffs: heat seeker */
#define FX_SMOKE_FAINT 4    /* puffs: torpedo */

typedef enum {
    FXK_FIRE,
    FXK_SMOKE,
    FXK_FLASH,
    FXK_RING,
    FXK_SPARK,
    FXK_EMBER,
    FXK_PUFF,       /* light exhaust smoke of a sputtering engine */
    FXK_CLOUD       /* fire-lit smoke of a mushroom cloud */
} fx_kind_t;

typedef struct {
    float   x, y;           /* world pixels, y up */
    float   vx, vy;         /* world pixels per second */
    float   age, life;      /* seconds; age < 0 means delayed start */
    float   size0, size1;   /* half-size in world pixels, start/end */
    float   rot, vrot;      /* radians, radians per second */
    float   drag;           /* velocity decay per second */
    float   gravity;        /* world pixels per second^2 (negative = down) */
    float   seed;           /* per-particle random 0..1 */
    uint8_t kind, cell;
    uint8_t variant;        /* FX_BLUE: bright bluish colors (cluster mines) */
} fx_particle_t;

#define FX_MAX_PARTICLES 3000

static fx_particle_t fx_parts[FX_MAX_PARTICLES];
static int           fx_num_parts;

static fx_particle_t *fx_new(fx_kind_t kind, float x, float y)
{
    fx_particle_t *p;

    if (fx_num_parts >= FX_MAX_PARTICLES)
        return NULL;
    p = &fx_parts[fx_num_parts++];
    memset(p, 0, sizeof(*p));
    p->kind = (uint8_t)kind;
    p->x = x;
    p->y = y;
    p->life = 1.0f;
    p->seed = fx_rand();
    return p;
}

static int fx_count(float base)
{
    float n = base * (float)fxAmount;
    int i = (int)n;
    if (fx_rand() < n - (float)i)
        i++;
    return i;
}

static void fx_random_dir(float *dx, float *dy)
{
    float a = fx_range(0.0f, 2.0f * (float)M_PI);
    *dx = cosf(a);
    *dy = sinf(a);
}

/* vx, vy: velocity of the exploding object in world pixels per second */
static void fx_spawn_explosion(float x, float y, float vx, float vy)
{
    fx_particle_t *p;
    float dx, dy, s;
    int i, n;

    /* initial flash */
    if ((p = fx_new(FXK_FLASH, x, y)) != NULL) {
        p->life = 0.16f;
        p->size0 = 40.0f;
        p->size1 = 75.0f;
        p->cell = CELL_GLOW;
    }

    /* shockwave ring */
    if ((p = fx_new(FXK_RING, x, y)) != NULL) {
        p->vx = vx * 0.5f;
        p->vy = vy * 0.5f;
        p->life = 0.42f;
        p->size0 = 8.0f;
        p->size1 = 105.0f;
        p->cell = CELL_RING;
    }

    /* fireballs, slightly staggered for secondary bursts */
    n = fx_count(10.0f);
    for (i = 0; i < n; i++) {
        fx_random_dir(&dx, &dy);
        s = fx_range(15.0f, 85.0f);
        float off = fx_range(0.0f, 10.0f);
        if ((p = fx_new(FXK_FIRE, x + dx * off, y + dy * off)) == NULL)
            break;
        p->vx = vx * 0.5f + dx * s;
        p->vy = vy * 0.5f + dy * s;
        p->age = -fx_range(0.0f, 0.12f) * (i > 2 ? 1.0f : 0.0f);
        p->life = fx_range(0.45f, 0.85f);
        p->size0 = fx_range(9.0f, 15.0f);
        p->size1 = fx_range(28.0f, 46.0f);
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-1.5f, 1.5f);
        p->drag = 2.2f;
        p->gravity = 12.0f;     /* hot gas rises a little */
        p->cell = (uint8_t)(CELL_FIRE0 + fx_rand_int(4));
    }

    /* smoke, lingers after the fire */
    n = fx_count(8.0f);
    for (i = 0; i < n; i++) {
        fx_random_dir(&dx, &dy);
        s = fx_range(8.0f, 40.0f);
        if ((p = fx_new(FXK_SMOKE, x + dx * 6.0f, y + dy * 6.0f)) == NULL)
            break;
        p->vx = vx * 0.3f + dx * s;
        p->vy = vy * 0.3f + dy * s;
        p->age = -fx_range(0.08f, 0.3f);
        p->life = fx_range(1.4f, 2.4f);
        p->size0 = fx_range(14.0f, 20.0f);
        p->size1 = fx_range(48.0f, 70.0f);
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-0.6f, 0.6f);
        p->drag = 1.2f;
        p->gravity = 6.0f;
        p->cell = (uint8_t)(CELL_SMOKE0 + fx_rand_int(2));
    }

    /* glowing embers */
    n = fx_count(26.0f);
    for (i = 0; i < n; i++) {
        fx_random_dir(&dx, &dy);
        s = fx_range(60.0f, 260.0f);
        if ((p = fx_new(FXK_EMBER, x, y)) == NULL)
            break;
        p->vx = vx * 0.6f + dx * s;
        p->vy = vy * 0.6f + dy * s;
        p->life = fx_range(0.6f, 1.6f);
        p->size0 = fx_range(2.0f, 3.5f);
        p->drag = 1.4f;
        p->gravity = -40.0f;
        p->cell = CELL_GLOW;
    }

    /* fast sparks */
    n = fx_count(16.0f);
    for (i = 0; i < n; i++) {
        fx_random_dir(&dx, &dy);
        s = fx_range(160.0f, 420.0f);
        if ((p = fx_new(FXK_SPARK, x, y)) == NULL)
            break;
        p->vx = vx * 0.6f + dx * s;
        p->vy = vy * 0.6f + dy * s;
        p->life = fx_range(0.25f, 0.55f);
        p->size0 = fx_range(1.4f, 2.2f);
        p->drag = 2.5f;
        p->gravity = -60.0f;
        p->cell = CELL_GLOW;
    }
}

/*
 * (x, y): contact point, (nx, ny): unit normal pointing away from the
 * obstacle, (tx, ty): sliding velocity along the surface (px/s),
 * strength: impact speed in px/s.
 */
/* ---- mine explosions (s: strength, 1 = a normal mine) ---- */

/*
 * Mine: a short, hard burst - flash, shock wave, fireball, sparks, embers.
 * variant FX_BLUE makes it bright bluish (cluster mines).
 */
static void fx_spawn_mine_burst(float x, float y, float s, uint8_t variant)
{
    int first = fx_num_parts;
    fx_particle_t *p;
    float dx, dy;

    if ((p = fx_new(FXK_FLASH, x, y)) != NULL) {
        p->life = 0.12f;
        p->size0 = 45.0f * s;
        p->size1 = 70.0f * s;
        p->cell = CELL_GLOW;
        p->variant = variant;
    }
    if ((p = fx_new(FXK_RING, x, y)) != NULL) {
        p->life = 0.30f;
        p->size0 = 10.0f * s;
        p->size1 = 130.0f * s;
        p->cell = CELL_RING;
        p->variant = variant;
    }
    for (int i = 0, n = fx_count(14.0f * s); i < n; i++) {
        float sp = fx_range(30.0f, 130.0f) * s, off = fx_range(0.0f, 8.0f) * s;
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_FIRE, x + dx * off, y + dy * off)) == NULL)
            break;
        p->vx = dx * sp;
        p->vy = dy * sp;
        p->life = fx_range(0.3f, 0.55f);
        p->size0 = fx_range(8.0f, 14.0f) * s;
        p->size1 = fx_range(26.0f, 40.0f) * s;
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-2.0f, 2.0f);
        p->drag = 3.0f;
        p->gravity = 15.0f;
        p->cell = (uint8_t)(CELL_FIRE0 + fx_rand_int(4));
        p->variant = variant;
    }
    for (int i = 0, n = fx_count(36.0f * s); i < n; i++) {
        float sp = fx_range(200.0f, 520.0f) * s;
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_SPARK, x, y)) == NULL)
            break;
        p->vx = dx * sp;
        p->vy = dy * sp;
        p->life = fx_range(0.2f, 0.5f);
        p->size0 = fx_range(1.4f, 2.2f);
        p->drag = 3.0f;
        p->gravity = -60.0f;
        p->cell = CELL_GLOW;
        p->variant = variant;
    }
    for (int i = 0, n = fx_count(16.0f * s); i < n; i++) {
        float sp = fx_range(60.0f, 220.0f) * s;
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_EMBER, x, y)) == NULL)
            break;
        p->vx = dx * sp;
        p->vy = dy * sp;
        p->life = fx_range(0.5f, 1.2f);
        p->size0 = fx_range(2.0f, 3.2f);
        p->drag = 1.6f;
        p->gravity = -40.0f;
        p->cell = CELL_GLOW;
        p->variant = variant;
    }
    for (int i = 0; i < 4; i++) {
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_SMOKE, x + dx * 6.0f, y + dy * 6.0f)) == NULL)
            break;
        p->vx = dx * 25.0f;
        p->vy = dy * 25.0f;
        p->age = -fx_range(0.1f, 0.2f);
        p->life = fx_range(0.8f, 1.2f);
        p->size0 = 14.0f * s;
        p->size1 = 40.0f * s;
        p->drag = 1.2f;
        p->gravity = 8.0f;
        p->cell = (uint8_t)(CELL_SMOKE0 + (i & 1));
    }

    /* small (split) mines are over sooner, too */
    float lf = 0.6f + 0.4f * fx_clamp(s, 0.0f, 1.0f);
    for (int i = first; i < fx_num_parts; i++) {
        fx_parts[i].life *= lf;
        fx_parts[i].age *= lf;          /* delays (negative ages) as well */
    }
}

/* Nuclear: blinding flash, rising fireball, mushroom cloud, big shock wave. */
static Uint64 fx_flash_ns;          /* start of the nuke flash, 0 = none */
static float  fx_flash_s;

static void fx_spawn_nuke(float x, float y, float s)
{
    const float rise = 75.0f * s;   /* fireball rise speed */
    fx_particle_t *p;
    float dx, dy;

    if (fxNukeFlash) {
        fx_flash_ns = SDL_GetTicksNS();
        fx_flash_s = s;
    }
    if ((p = fx_new(FXK_FLASH, x, y)) != NULL) {
        p->life = 0.35f;
        p->size0 = 110.0f * s;
        p->size1 = 170.0f * s;
        p->cell = CELL_GLOW;
    }
    if ((p = fx_new(FXK_RING, x, y)) != NULL) {
        p->life = 1.1f;
        p->size0 = 20.0f * s;
        p->size1 = 420.0f * s;
        p->cell = CELL_RING;
    }
    for (int i = 0, n = fx_count(20.0f * s); i < n; i++) {      /* fireball, rising as one */
        float off = fx_range(0.0f, 20.0f) * s;
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_FIRE, x + dx * off, y + dy * off)) == NULL)
            break;
        p->vx = dx * fx_range(8.0f, 30.0f) * s;
        p->vy = dy * fx_range(8.0f, 30.0f) * s + rise;
        p->life = fx_range(1.6f, 2.4f);
        p->size0 = fx_range(20.0f, 30.0f) * s;
        p->size1 = fx_range(60.0f, 85.0f) * s;
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-0.8f, 0.8f);
        p->drag = 0.4f;
        p->cell = (uint8_t)(CELL_FIRE0 + fx_rand_int(4));
    }
    int stem = fx_count(18.0f * s);
    for (int i = 0; i < stem; i++) {     /* stem: puffs released one after another */
        if ((p = fx_new(FXK_CLOUD, x + fx_range(-6.0f, 6.0f) * s, y)) == NULL)
            break;
        p->vx = fx_range(-6.0f, 6.0f) * s;
        p->vy = rise * fx_range(0.8f, 1.0f);
        p->age = -(0.1f + 1.3f * (float)i / (float)stem);
        p->life = fx_range(2.2f, 2.8f);
        p->size0 = 14.0f * s;
        p->size1 = fx_range(26.0f, 34.0f) * s;
        p->drag = 0.2f;
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-0.5f, 0.5f);
        p->cell = (uint8_t)(CELL_SMOKE0 + (i & 1));
    }
    for (int i = 0, n = fx_count(22.0f * s); i < n; i++) {   /* cap: billows out on top */
        float a = fx_range(-1.0f, 1.0f), delay = fx_range(0.35f, 0.9f);
        float cy = y + rise * delay + 10.0f * s;
        if ((p = fx_new(FXK_CLOUD, x + a * 15.0f * s, cy + fx_range(-6.0f, 6.0f) * s)) == NULL)
            break;
        p->vx = a * fx_range(55.0f, 95.0f) * s;
        p->vy = rise * 0.7f + fx_range(-8.0f, 8.0f) * s;
        p->age = -delay;
        p->life = fx_range(2.4f, 3.2f);
        p->size0 = 22.0f * s;
        p->size1 = fx_range(55.0f, 75.0f) * s;
        p->drag = 0.9f;
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-0.4f, 0.4f);
        p->cell = (uint8_t)(CELL_SMOKE0 + (i & 1));
    }
    for (int i = 0, n = fx_count(60.0f * s); i < n; i++) {
        float sp = fx_range(80.0f, 380.0f) * s;
        fx_random_dir(&dx, &dy);
        if ((p = fx_new(FXK_EMBER, x, y)) == NULL)
            break;
        p->vx = dx * sp;
        p->vy = dy * sp;
        p->life = fx_range(0.8f, 2.0f);
        p->size0 = fx_range(2.0f, 3.5f);
        p->drag = 1.0f;
        p->gravity = -40.0f;
        p->cell = CELL_GLOW;
    }
}

static void fx_spawn_sparks(float x, float y, float nx, float ny,
                            float tx, float ty, float strength)
{
    fx_particle_t *p;
    float k = fx_clamp01(strength / 600.0f);
    int i, n = fx_count(6.0f + 30.0f * k);

    if ((p = fx_new(FXK_FLASH, x, y)) != NULL) {
        p->life = 0.09f;
        p->size0 = 6.0f + 8.0f * k;
        p->size1 = p->size0 * 1.4f;
        p->cell = CELL_GLOW;
    }

    for (i = 0; i < n; i++) {
        float a = fx_range(-1.3f, 1.3f);          /* +-75 degrees */
        float ca = cosf(a), sa = sinf(a);
        float dx = nx * ca - ny * sa;
        float dy = nx * sa + ny * ca;
        float s = fx_range(60.0f, 260.0f) * (0.5f + k);
        float slide = fx_range(0.2f, 0.6f);

        if ((p = fx_new(FXK_SPARK, x, y)) == NULL)
            break;
        p->vx = dx * s + tx * slide;
        p->vy = dy * s + ty * slide;
        p->life = fx_range(0.22f, 0.55f);
        p->size0 = fx_range(1.2f, 2.0f);
        p->drag = 2.2f;
        p->gravity = -90.0f;
        p->cell = CELL_GLOW;
    }

    n = fx_count(2.0f + 4.0f * k);
    for (i = 0; i < n; i++) {
        float a = fx_range(-1.0f, 1.0f);
        float s = fx_range(20.0f, 90.0f);
        if ((p = fx_new(FXK_EMBER, x, y)) == NULL)
            break;
        p->vx = (nx * cosf(a) - ny * sinf(a)) * s + tx * 0.3f;
        p->vy = (nx * sinf(a) + ny * cosf(a)) * s + ty * 0.3f;
        p->life = fx_range(0.4f, 0.9f);
        p->size0 = fx_range(1.5f, 2.5f);
        p->drag = 1.8f;
        p->gravity = -50.0f;
        p->cell = CELL_GLOW;
    }
}

static int fx_num_hj;

static void fx_clear(void)
{
    fx_num_parts = 0;
    fx_num_hj = 0;
}

static void fx_update(float dt)
{
    int i = 0;

    while (i < fx_num_parts) {
        fx_particle_t *p = &fx_parts[i];

        p->age += dt;
        if (p->age >= p->life) {
            fx_parts[i] = fx_parts[--fx_num_parts];
            continue;
        }
        if (p->age > 0.0f) {
            float damp = expf(-p->drag * dt);
            p->vx *= damp;
            p->vy = p->vy * damp + p->gravity * dt;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            p->rot += p->vrot * dt;
        }
        i++;
    }
}

/* ------------------------------------------------------------------ */
/* World <-> screen                                                   */
/* ------------------------------------------------------------------ */

static float fx_wrap_dx(float d)
{
    if (Setup && BIT(Setup->mode, WRAP_PLAY)) {
        float w = (float)Setup->width;
        if (d > w * 0.5f)
            d -= w;
        else if (d < -w * 0.5f)
            d += w;
    }
    return d;
}

static float fx_wrap_dy(float d)
{
    if (Setup && BIT(Setup->mode, WRAP_PLAY)) {
        float h = (float)Setup->height;
        if (d > h * 0.5f)
            d -= h;
        else if (d < -h * 0.5f)
            d += h;
    }
    return d;
}

/* View-relative offset of a world point from the view center. */
static void fx_view_offset(float wx, float wy, float *dx, float *dy)
{
    *dx = fx_wrap_dx(wx - ((float)world.x + (float)ext_view_width * 0.5f));
    *dy = fx_wrap_dy(wy - ((float)world.y + (float)ext_view_height * 0.5f));
}

/*
 * Inside the area the server sends objects for, at least margin pixels
 * from its edge. The client may draw a wider area (ext_view) than the
 * server covers (active_view); objects leaving the server's area just
 * vanish, which must not look like an explosion.
 */
static bool fx_in_server_view(float x, float y, float margin)
{
    float dx, dy;
    fx_view_offset(x, y, &dx, &dy);
    return fabsf(dx) < (float)active_view_width * 0.5f - margin
        && fabsf(dy) < (float)active_view_height * 0.5f - margin;
}

/* ------------------------------------------------------------------ */
/* Event detection                                                    */
/* ------------------------------------------------------------------ */

/* Contrail: ring buffer of wingtip positions, one point per server frame. */
#define FX_TRAIL_PTS    48
#define FX_TRAIL_LIFE   0.7     /* seconds a contrail point stays visible */

typedef struct {
    float   x, y;           /* world position */
    float   k;              /* intensity 0..1, 0 = invisible joint */
    double  t;              /* creation time, seconds */
} fx_trail_pt_t;

typedef struct {
    fx_trail_pt_t pt[2][FX_TRAIL_PTS];  /* left and right wingtip */
    int     head;           /* index of the newest point */
    int     count;
} fx_trail_t;

#define FX_HIST         9       /* positions for the acceleration fit */

typedef struct {
    int     id;
    float   x, y;           /* last seen world position */
    float   vx, vy;         /* pixels per server tick */
    int     dir;            /* last heading, 0..RES-1 */
    long    loop;           /* server frame of last sighting */
    bool    has_vel;
    Uint64  spark_ns;       /* time of last spark burst (cooldown) */
    float   trail_k;        /* smoothed contrail intensity */
    fx_trail_t trail;
    /* unwrapped position history for a low-noise acceleration estimate */
    float   ux, uy;         /* unwrapped position */
    float   hx[FX_HIST], hy[FX_HIST];   /* [0] newest */
    int     hn;             /* valid history entries */
    long    hdl;            /* frame spacing of the history */
    float   thrust_a;       /* acceleration along the heading, px/tick^2 */
    bool    thrust_valid;
    long    thrust_loop;    /* last frame with a thrust sign */
    int     sputter;        /* frames the flame stays cut out (low fuel) */
    bool    shield;         /* shield up in this frame */
    Uint64  hit_ns;         /* last hit on shield or armor (0 = none) */
    float   flame_k;        /* smoothed flame intensity 0..1 */
} fx_track_t;

#define FX_MAX_TRACKS   128
#define FX_MAX_WRECKS   128

static fx_track_t fx_tracks[FX_MAX_TRACKS];
static int        fx_num_tracks;

static float fx_wreck_x[FX_MAX_WRECKS], fx_wreck_y[FX_MAX_WRECKS];
static int   fx_num_wrecks;

/* sparks (debris) of this frame, world coordinates */
#define FX_MAX_SPARKS   1024
static float fx_spark_x[FX_MAX_SPARKS], fx_spark_y[FX_MAX_SPARKS];
static int   fx_num_sparks;

/* x, y: view coordinates as passed to Gui_paint_spark() */
/*
 * Shots of this and the previous frame, world coordinates. A shot that
 * was right at a ship and is gone in the next frame hit it.
 */
#define FX_MAX_SHOTS    1024
static float fx_shot_x[2][FX_MAX_SHOTS], fx_shot_y[2][FX_MAX_SHOTS];
static int   fx_num_shots[2];
static int   fx_shot_cur;                   /* index of this frame's list */
static long  fx_shot_loop[2];

static void fx_add_shot(float x, float y)
{
    int c = fx_shot_cur;
    if (fx_num_shots[c] < FX_MAX_SHOTS) {
        fx_shot_x[c][fx_num_shots[c]] = x;
        fx_shot_y[c][fx_num_shots[c]] = y;
        fx_num_shots[c]++;
    }
}

/* x, y: view coordinates as passed to Gui_paint_fastshot/teamshot() */
void Fx_observe_shot(int x, int y)
{
    fx_add_shot((float)(world.x + x), (float)(world.y + ext_view_height - y));
}

/* x, y: world coordinates as passed to Gui_paint_missile() */
void Fx_observe_missile(int x, int y)
{
    fx_add_shot((float)x, (float)y);
}

/*
 * Missile trails
 *
 * Missiles carry no id, so each frame the trail is continued from the
 * nearest tail of the previous frame with the same length (the length
 * tells the type while the server option distinguishMissiles is on, the
 * default): smart 12, heat 15, torpedo 18.
 */
#define FX_MAX_MTAILS   256
static float fx_mtail_x[2][FX_MAX_MTAILS], fx_mtail_y[2][FX_MAX_MTAILS];
static int   fx_mtail_len[2][FX_MAX_MTAILS];
static int   fx_num_mtails[2];
static long  fx_mtail_loop[2];
static int   fx_mtail_cur;

static void fx_mtails_begin(void)
{
    fx_mtail_cur ^= 1;
    fx_num_mtails[fx_mtail_cur] = 0;
    fx_mtail_loop[fx_mtail_cur] = loops;
}


void Fx_observe_spark(int x, int y)
{
    if (fx_num_sparks < FX_MAX_SPARKS) {
        fx_spark_x[fx_num_sparks] = (float)(world.x + x);
        fx_spark_y[fx_num_sparks] = (float)(world.y + ext_view_height - y);
        fx_num_sparks++;
    }
}

/* Impact thresholds, velocity change in world pixels per second. */
#define FX_IMPACT_MIN_DV    150.0f
#define FX_IMPACT_REL_DV    0.35f
#define FX_IMPACT_MIN_V     60.0f
#define FX_SPARK_COOLDOWN   (120 * SDL_NS_PER_MS)

static float fx_ticks_per_sec(void)
{
    return (Setup && FPS > 0) ? (float)FPS : 50.0f;
}

static void fx_check_impact(fx_track_t *t, float vx, float vy)
{
    float tps = fx_ticks_per_sec();
    float dvx = (vx - t->vx) * tps, dvy = (vy - t->vy) * tps;
    float ox = t->vx * tps, oy = t->vy * tps;
    float dv = sqrtf(dvx * dvx + dvy * dvy);
    float v0 = sqrtf(ox * ox + oy * oy);
    Uint64 now;

    /*
     * Positions arrive as whole pixels, so per-tick velocities carry up
     * to ~1.4 px/tick of rounding noise per frame. Require a velocity
     * change well above that and a moving ship, so hovering ships at
     * high server FPS don't sparkle.
     */
    if (dv < fmaxf(FX_IMPACT_MIN_DV, 3.5f * tps)
        || dv < FX_IMPACT_REL_DV * v0
        || v0 < fmaxf(FX_IMPACT_MIN_V, 2.0f * tps))
        return;
    /* the impulse must oppose the previous motion (bounce, not thrust) */
    if (dvx * ox + dvy * oy > -0.2f * dv * v0)
        return;

    now = SDL_GetTicksNS();
    if (now - t->spark_ns < FX_SPARK_COOLDOWN)
        return;
    t->spark_ns = now;
    if (t->shield)
        t->hit_ns = now;        /* the shield takes the blow */

    float nx = dvx / dv, ny = dvy / dv;
    /* sliding component of the old velocity along the surface */
    float along = ox * -ny + oy * nx;
    float cx = t->x - nx * SHIP_SZ * 0.8f;
    float cy = t->y - ny * SHIP_SZ * 0.8f;

    fx_spawn_sparks(cx, cy, nx, ny, -ny * along, nx * along, dv);
}

/* Wingtip offsets of a ship at heading dir, as drawn by Gui_paint_ship(). */
/* The shipshape Gui_paint_ship() draws for this player. */
static shipshape_t *fx_ship_shape(int id)
{
    shipshape_t *ship;

    if (!instruments.showShipShapes && self && self->id != id)
        return Default_ship();
    if (!instruments.showMyShipShape && self && self->id == id)
        return Default_ship();
    if ((ship = Ship_by_id(id)) == NULL)
        ship = Default_ship();
    return ship;
}

static void fx_ship_tips(int id, int dir, float tip[2][2])
{
    shipshape_t *ship = fx_ship_shape(id);
    int il = 0, ir = 0;
    float best_l = -1e9f, best_r = 1e9f;

    /* at heading 0 the ship points along +x: tips are the outermost
     * points sideways, preferring the rear ones */
    for (int i = 0; i < ship->num_points; i++) {
        position_t p = Ship_get_point_position(ship, i, 0);
        float l = (float)p.y - 0.25f * (float)p.x;
        float r = (float)p.y + 0.25f * (float)p.x;
        if (l > best_l) { best_l = l; il = i; }
        if (r < best_r) { best_r = r; ir = i; }
    }
    position_t pl = Ship_get_point_position(ship, il, dir);
    position_t pr = Ship_get_point_position(ship, ir, dir);
    tip[0][0] = (float)pl.x;
    tip[0][1] = (float)pl.y;
    tip[1][0] = (float)pr.x;
    tip[1][1] = (float)pr.y;
}

static void fx_trail_push(fx_track_t *t, float k, double now)
{
    fx_trail_t *tr = &t->trail;
    float tip[2][2];

    fx_ship_tips(t->id, t->dir, tip);
    tr->head = (tr->head + 1) % FX_TRAIL_PTS;
    if (tr->count < FX_TRAIL_PTS)
        tr->count++;
    for (int s = 0; s < 2; s++) {
        fx_trail_pt_t *p = &tr->pt[s][tr->head];
        p->x = t->x + tip[s][0];
        p->y = t->y + tip[s][1];
        p->k = k;
        p->t = now;
    }
}

/*
 * Contrails appear while a fast ship turns hard. The heading is exact
 * (no rounding noise like the position), so the turn rate is used;
 * the intensity is smoothed to hide single-frame mouse jerks.
 */
static void fx_trail_update(fx_track_t *t, int dir, long dl, bool moving)
{
    fx_trail_t *tr = &t->trail;
    double now = (double)SDL_GetTicksNS() / 1e9;
    float tps = fx_ticks_per_sec();
    float k = 0.0f;

    if (moving && fxContrails && fx_active() && dl > 0) {
        int dd = (dir - t->dir) & (RES - 1);
        if (dd > RES / 2)
            dd -= RES;
        float w = fabsf((float)dd) * 2.0f * (float)M_PI / RES * tps / (float)dl;
        float speed = sqrtf(t->vx * t->vx + t->vy * t->vy) * tps;
        k = fx_smooth((speed - 150.0f) / 200.0f) * fx_smooth((w - 2.0f) / 4.0f);
    }
    t->trail_k += (k - t->trail_k) * (k > t->trail_k ? 0.5f : 0.25f);
    t->dir = dir;

    bool last_visible = tr->count > 0 && tr->pt[0][tr->head].k > 0.0f;
    if (t->trail_k > 0.02f) {
        if (!last_visible)
            fx_trail_push(t, 0.0f, now);    /* invisible joint starts a run */
        fx_trail_push(t, t->trail_k, now);
    } else if (last_visible) {
        fx_trail_push(t, 0.0f, now);        /* invisible joint ends the run */
    }
}

/*
 * Acceleration along the heading from a least squares parabola through
 * the last 9 positions: positions are whole pixels, simple differences
 * are far too noisy to see thrust.
 */
static void fx_accel_update(fx_track_t *t, float dx, float dy, long dl, int dir)
{
    t->ux += dx;
    t->uy += dy;
    if (dl != t->hdl) {
        t->hn = 0;
        t->hdl = dl;
    }
    memmove(&t->hx[1], &t->hx[0], sizeof(float) * (FX_HIST - 1));
    memmove(&t->hy[1], &t->hy[0], sizeof(float) * (FX_HIST - 1));
    t->hx[0] = t->ux;
    t->hy[0] = t->uy;
    if (t->hn < FX_HIST)
        t->hn++;
    if (t->hn < FX_HIST) {
        t->thrust_valid = false;
        return;
    }
    /* second derivative of the fit: 2 * sum((k^2 - 20/3) p_k) / 308, k = -4..4 */
    float ax = 0.0f, ay = 0.0f;
    for (int k = 0; k < FX_HIST; k++) {
        float tk = (float)(k - 4);
        float w = tk * tk - 20.0f / 3.0f;
        ax += w * t->hx[k];
        ay += w * t->hy[k];
    }
    float norm = 2.0f / (308.0f * (float)(dl * dl));
    ax *= norm;
    ay *= norm;
    float ang = (float)dir * 2.0f * (float)M_PI / RES;
    t->thrust_a = ax * cosf(ang) + ay * sinf(ang);
    t->thrust_valid = true;
}

/*
 * Hyperjump: a ship is somewhere else from one frame to the next without
 * blowing up. Own jumps are known from the hyperjump key; for others a
 * jump away from a visible wormhole counts as a hyperjump.
 */
#define FX_MAX_HJ       16
#define FX_HJ_PTS       24
#define FX_HJ_OUT       0.22f   /* departure, seconds */
#define FX_HJ_IN        1.3f    /* arrival incl. afterglow, seconds */

#define FX_JK_HYPER     0
#define FX_JK_WORM      1
#define FX_WT_IN        0.5f    /* wormhole entry, seconds */
#define FX_WT_OUT       1.2f    /* wormhole exit incl. swirling on, seconds */

typedef struct {
    int     kind;                       /* FX_JK_* */
    float   wx, wy, wr;                 /* wormhole center and radius (FX_JK_WORM) */
    int     id;                         /* ship, the afterglow follows it */
    float   x, y;                       /* world position */
    float   hx, hy;                     /* heading, world (y up) */
    float   px[FX_HJ_PTS], py[FX_HJ_PTS]; /* ship outline relative to x, y */
    int     n;
    bool    arrive;
    double  t0;                         /* start, seconds */
} fx_hj_t;

static fx_hj_t fx_hj[FX_MAX_HJ];
static double  fx_hj_key_t = -100.0;    /* own hyperjump key press, seconds */

/* The hyperjump key was pressed (only counts with a hyperjump item left). */
void Fx_hyperjump_key(void)
{
    if (numItems[ITEM_HYPERJUMP] > 0)
        fx_hj_key_t = (double)SDL_GetTicksNS() / 1e9;
}

/*
 * Wormholes of polygon maps, from the TNG map objects. The polygon that
 * marks a wormhole on the map (any style: texture, plain circle) is the
 * smallest one around its center; it is drawn as a tunnel instead.
 */
#define FX_MAX_WORMS    64

typedef struct {
    float   x, y, r;            /* center and radius, world pixels */
    int     type;               /* SETUP_WORM_* */
    int     obj;                /* index into mapobjects */
    int     poly;               /* marking polygon or -1 */
} fx_worm_t;

static fx_worm_t   fx_worms[FX_MAX_WORMS];
static int         fx_num_worms = -1;
static const void *fx_worms_key;
static int         fx_worms_npoly;

static void fx_worms_build(void)
{
    if (fx_num_worms >= 0 && fx_worms_key == (const void *)mapobjects
        && fx_worms_npoly == num_polygons)
        return;
    fx_worms_key = mapobjects;
    fx_worms_npoly = num_polygons;
    fx_num_worms = 0;
    for (int i = 0; i < num_mapobjects && fx_num_worms < FX_MAX_WORMS; i++) {
        int type = mapobjects[i].type;
        if (type != SETUP_WORM_IN && type != SETUP_WORM_NORMAL && type != SETUP_WORM_OUT)
            continue;
        fx_worm_t *w = &fx_worms[fx_num_worms++];
        w->x = (float)(mapobjects[i].bounds.x + BLOCK_SZ / 2);
        w->y = (float)(mapobjects[i].bounds.y + BLOCK_SZ / 2);
        w->r = (float)BLOCK_SZ * 0.5f;
        w->type = type;
        w->obj = i;
        w->poly = -1;
        int best = 1 << 30;
        for (int p = 0; p < num_polygons; p++) {
            const irec_t *b = &polygons[p].bounds;
            if (b->w > 300 || b->h > 300 || b->w * b->h >= best)
                continue;
            /* wormhole polygons are centered on the wormhole */
            float ex = (float)b->x + (float)b->w * 0.5f - w->x;
            float ey = (float)b->y + (float)b->h * 0.5f - w->y;
            float tol = 0.2f * (float)(b->w > b->h ? b->w : b->h) + 4.0f;
            if (fabsf(ex) > tol || fabsf(ey) > tol)
                continue;
            best = b->w * b->h;
            w->poly = p;
        }
        if (w->poly >= 0) {
            const irec_t *b = &polygons[w->poly].bounds;
            w->x = (float)b->x + (float)b->w * 0.5f;
            w->y = (float)b->y + (float)b->h * 0.5f;
            w->r = (float)(b->w > b->h ? b->w : b->h) * 0.5f;
        }
    }
}

static inline bool fx_worms_on(void)
{
    return fxWormholes && fx_active();
}

/* An entry wormhole (in or normal) at world position (x, y), or NULL. */
static const fx_worm_t *fx_worm_near(float x, float y)
{
    fx_worms_build();
    for (int i = 0; i < fx_num_worms; i++) {
        const fx_worm_t *w = &fx_worms[i];
        if (w->type == SETUP_WORM_OUT)
            continue;
        float dx = fx_wrap_dx(w->x - x), dy = fx_wrap_dy(w->y - y);
        if (dx * dx + dy * dy < (w->r + 60.0f) * (w->r + 60.0f))
            return w;
    }
    return NULL;
}

/* The wormhole nearest to (x, y) of any type (exit side), or NULL. */
static const fx_worm_t *fx_worm_nearest(float x, float y)
{
    const fx_worm_t *best = NULL;
    float bd = 1e30f;

    fx_worms_build();
    for (int i = 0; i < fx_num_worms; i++) {
        float dx = fx_wrap_dx(fx_worms[i].x - x), dy = fx_wrap_dy(fx_worms[i].y - y);
        float d = dx * dx + dy * dy;
        if (d < bd && d < (fx_worms[i].r + 80.0f) * (fx_worms[i].r + 80.0f)) {
            bd = d;
            best = &fx_worms[i];
        }
    }
    return best;
}

/*
 * What made a ship jump: FX_JK_HYPER, FX_JK_WORM or -1 (unknown, no
 * effect). Own hyperjumps are known from the key.
 */
static int fx_jump_kind(int id, float ox, float oy)
{
    bool own = self && self->id == id;

    if (own) {
        double now = (double)SDL_GetTicksNS() / 1e9;
        bool key = now - fx_hj_key_t < 1.5;
        fx_hj_key_t = -100.0;
        if (key)
            return FX_JK_HYPER;
    }
    if (fx_worm_near(ox, oy))
        return FX_JK_WORM;
    if (own)
        return -1;
    for (int i = 0; i < num_wormholes; i++) {
        float dx = fx_wrap_dx((float)wormhole_ptr[i].x - ox);
        float dy = fx_wrap_dy((float)wormhole_ptr[i].y - oy);
        if (dx * dx + dy * dy < 90.0f * 90.0f)
            return -1;
    }
    return FX_JK_HYPER;
}

static fx_hj_t *fx_hj_add(int kind, int id, float x, float y, int dir, bool arrive)
{
    shipshape_t *ship = fx_ship_shape(id);
    float ang = (float)dir * 2.0f * (float)M_PI / RES;
    fx_hj_t *h;

    if (!(kind == FX_JK_WORM ? fxWormholes : fxHyperjump) || !fx_active()
        || fx_num_hj >= FX_MAX_HJ)
        return NULL;
    h = &fx_hj[fx_num_hj++];
    h->kind = kind;
    h->wx = x;
    h->wy = y;
    h->wr = (float)BLOCK_SZ * 0.5f;
    h->id = id;
    h->x = x;
    h->y = y;
    h->hx = cosf(ang);
    h->hy = sinf(ang);
    h->n = 0;
    for (int i = 0; ship && i < ship->num_points && i < FX_HJ_PTS; i++) {
        position_t pt = Ship_get_point_position(ship, i, dir);
        h->px[i] = (float)pt.x;
        h->py[i] = (float)pt.y;
        h->n++;
    }
    h->arrive = arrive;
    h->t0 = (double)SDL_GetTicksNS() / 1e9;
    return h;
}

static void fx_observe_ship(int id, int x, int y, int dir)
{
    fx_track_t *t = NULL;
    long dl;

    for (int i = 0; i < fx_num_tracks; i++) {
        if (fx_tracks[i].id == id) {
            t = &fx_tracks[i];
            break;
        }
    }

    if (!t) {
        if (fx_num_tracks >= FX_MAX_TRACKS)
            return;
        t = &fx_tracks[fx_num_tracks++];
        memset(t, 0, sizeof(*t));
        t->id = id;
        t->x = (float)x;
        t->y = (float)y;
        t->dir = dir;
        t->loop = loops;
        return;
    }

    dl = loops - t->loop;
    if (dl == 0)
        return;

    float dx = fx_wrap_dx((float)x - t->x);
    float dy = fx_wrap_dy((float)y - t->y);
    bool moving = false;

    /* frame gap, server restart or teleport/wormhole: restart tracking */
    if (dl < 0 || dl > 4 || dx * dx + dy * dy > 120.0f * 120.0f * (float)(dl * dl)) {
        int kind = dl >= 1 && dl <= 4 ? fx_jump_kind(id, t->x, t->y) : -1;
        if (kind == FX_JK_HYPER) {
            fx_hj_add(kind, id, t->x, t->y, t->dir, false);
            fx_hj_add(kind, id, (float)x, (float)y, dir, true);
        } else if (kind == FX_JK_WORM) {
            const fx_worm_t *w = fx_worm_near(t->x, t->y);
            fx_hj_t *h = fx_hj_add(kind, id, t->x, t->y, t->dir, false);
            if (h && w) {
                h->wx = w->x;
                h->wy = w->y;
                h->wr = w->r;
            }
            w = fx_worm_nearest((float)x, (float)y);
            h = fx_hj_add(kind, id, (float)x, (float)y, dir, true);
            if (h && w) {
                h->wx = w->x;
                h->wy = w->y;
                h->wr = w->r;
            }
        }
        t->has_vel = false;
        t->trail.count = 0;
        t->trail_k = 0.0f;
        t->hn = 0;
        t->thrust_valid = false;
    } else {
        fx_accel_update(t, dx, dy, dl, dir);
        float vx = dx / (float)dl, vy = dy / (float)dl;
        if (t->has_vel && fxSparks && fx_active())
            fx_check_impact(t, vx, vy);
        t->vx = vx;
        t->vy = vy;
        t->has_vel = true;
        moving = true;
    }
    t->x = (float)x;
    t->y = (float)y;
    t->loop = loops;
    fx_trail_update(t, dir, dl, moving);
}

static bool fx_wreck_near(float x, float y, float radius)
{
    for (int i = 0; i < fx_num_wrecks; i++) {
        float dx = fx_wrap_dx(fx_wreck_x[i] - x);
        float dy = fx_wrap_dy(fx_wreck_y[i] - y);
        if (dx * dx + dy * dy <= radius * radius)
            return true;
    }
    return false;
}

static fx_track_t *fx_track_by_id(int id);

/*
 * Mines
 *
 * A mine that vanishes inside the view exploded. What it was shows in the
 * same frame: a cluster mine sends a ring of shots, a nuclear mine an
 * order of magnitude more debris (or shots) than a normal one. The debris
 * count, scaled by the share of debris the server sends (sparkProb), gives
 * the strength. (The nuke sound is no help here: the server queues sounds,
 * they may arrive a frame later.)
 */
#define FX_MAX_MINES        256
#define FX_MINE_MATCH       14.0f   /* max mine drift per frame, px */
#define FX_MINE_BURST_R     70.0f   /* count debris/shots this close */
#define FX_MINE_NORMAL_DEBRIS 128.0f    /* full debris of a normal mine */
#define FX_MINE_NUKE_DEBRIS 400.0f  /* normal mines: <~180, nuclear: >~700 */
#define FX_MINE_CLUSTER_SHOTS 8     /* a cluster mine sends ~27 shots */
#define FX_MINE_NUKE_SHOTS  100     /* a nuclear cluster mine sends hundreds */

static float fx_mine_x[2][FX_MAX_MINES], fx_mine_y[2][FX_MAX_MINES];
static int   fx_num_mines[2];
static int   fx_mine_cur;
static long  fx_mine_loop[2];

typedef struct {
    float x, y;
} fx_pending_t;
static fx_pending_t fx_pending[16];
static int          fx_num_pending;

static Uint64 fx_nuke_ns;           /* last nuke event (sound or seen), for dedup */
static Uint64 fx_shake_ns;          /* start of the current shake, 0 = none */

/* A nuke went off somewhere: one shake per nuke, however it was noticed. */
static void fx_nuke_event(void)
{
    Uint64 now = SDL_GetTicksNS();
    if (fx_nuke_ns && now - fx_nuke_ns < 500 * SDL_NS_PER_MS)
        return;
    fx_nuke_ns = now;
    if (fxScreenShake && !Fx_classic())
        fx_shake_ns = now;
}

/* Server sound events; they carry no position. */
void Fx_audio_event(int type, int volume)
{
    UNUSED_PARAM(volume);
    if (type == NUKE_EXPLOSION_SOUND)
        fx_nuke_event();
}

static void fx_mines_begin(void)
{
    int prev = fx_mine_cur, cur = fx_mine_cur ^ 1;
    long dl = loops - fx_mine_loop[prev];

    fx_num_mines[cur] = 0;
    for (int i = 0; i < num_mine && fx_num_mines[cur] < FX_MAX_MINES; i++) {
        fx_mine_x[cur][fx_num_mines[cur]] = (float)mine_ptr[i].x;
        fx_mine_y[cur][fx_num_mines[cur]] = (float)mine_ptr[i].y;
        fx_num_mines[cur]++;
    }
    fx_mine_cur = cur;
    fx_mine_loop[cur] = loops;
    fx_num_pending = 0;

    if (dl < 1 || dl > 3)
        return;
    float r2 = (FX_MINE_MATCH * (float)dl) * (FX_MINE_MATCH * (float)dl);
    for (int k = 0; k < fx_num_mines[prev]; k++) {
        float mx = fx_mine_x[prev][k], my = fx_mine_y[prev][k];
        bool found = false;
        for (int c = 0; !found && c < fx_num_mines[cur]; c++) {
            float dx = fx_wrap_dx(fx_mine_x[cur][c] - mx), dy = fx_wrap_dy(fx_mine_y[cur][c] - my);
            if (dx * dx + dy * dy < r2)
                found = true;
        }
        if (found)
            continue;
        if (fx_in_server_view(mx, my, 20.0f)
            && fx_num_pending < (int)SDL_arraysize(fx_pending))
            fx_pending[fx_num_pending++] = (fx_pending_t){ mx, my };
    }
}

/*
 * Strength of a mine explosion from its share of a full mine. A mine
 * split into n minis has 1/n of the mass and sends 1/n of the debris;
 * ^0.7 keeps the steps clearly visible: 1, 0.62, 0.46, 0.38.
 */
static float fx_mine_strength(float share)
{
    return powf(fx_clamp(share, 0.25f, 1.6f), 0.7f);
}

/* After this frame's debris and shots were seen: what kind of mine was it? */
static void fx_mines_resolve(void)
{
    float send = (float)spark_rand / 128.0f;    /* share of debris the server sends */

    for (int i = 0; i < fx_num_pending; i++) {
        float x = fx_pending[i].x, y = fx_pending[i].y;
        int debris = 0, shots = 0;
        for (int k = 0; k < fx_num_sparks; k++) {
            float dx = fx_wrap_dx(fx_spark_x[k] - x), dy = fx_wrap_dy(fx_spark_y[k] - y);
            if (dx * dx + dy * dy < FX_MINE_BURST_R * FX_MINE_BURST_R)
                debris++;
        }
        for (int k = 0; k < fx_num_shots[fx_shot_cur]; k++) {
            float dx = fx_wrap_dx(fx_shot_x[fx_shot_cur][k] - x);
            float dy = fx_wrap_dy(fx_shot_y[fx_shot_cur][k] - y);
            if (dx * dx + dy * dy < FX_MINE_BURST_R * FX_MINE_BURST_R)
                shots++;
        }
        /*
         * No burst, no explosion: the server sends at most maxVisibleObjects
         * objects per frame, so after a nuke's debris flood mines just drop
         * out for a while. Require some of the debris even a quarter mine
         * leaves (unless the player turned sparks off, then there is none).
         */
        float need = fmaxf(2.0f, 0.3f * FX_MINE_NORMAL_DEBRIS * send / 4.0f);
        if (send >= 0.05f && (float)debris < need && shots < 4)
            continue;

        float full = send > 0.05f ? (float)debris / send : 0.0f;   /* estimated total */
        bool nuke = full > FX_MINE_NUKE_DEBRIS || shots > FX_MINE_NUKE_SHOTS;

        if (nuke)
            fx_nuke_event();
        if (!fxMineExplosions || !fx_active())
            continue;
        if (nuke) {
            float s = full > 0.0f ? sqrtf(full / 3000.0f) : 1.0f;
            fx_spawn_nuke(x, y, fx_clamp(s, 0.7f, 1.3f));
            if (shots >= FX_MINE_CLUSTER_SHOTS)     /* nuclear cluster: plus a blue burst */
                fx_spawn_mine_burst(x, y, 1.4f, FX_BLUE);
        } else if (shots >= FX_MINE_CLUSTER_SHOTS) {
            fx_spawn_mine_burst(x, y, fx_mine_strength((float)shots / 27.0f), FX_BLUE);
        } else {
            float share = full > 0.0f ? full / FX_MINE_NORMAL_DEBRIS : 1.0f;
            fx_spawn_mine_burst(x, y, fx_mine_strength(share), 0);
        }
    }
    fx_num_pending = 0;
}

/*
 * Shake of the view in world pixels, the same for everybody on the map;
 * added to the camera before anything is drawn.
 */
void Fx_shake_offset(int *dx, int *dy)
{
    *dx = *dy = 0;
    if (!fx_shake_ns || !fxScreenShake || Fx_classic())
        return;
    float t = (float)(SDL_GetTicksNS() - fx_shake_ns) / 1e9f;
    const float dur = 1.5f;
    if (t >= dur) {
        fx_shake_ns = 0;
        return;
    }
    float e = 1.0f - t / dur;
    float amp = 16.0f * e * sqrtf(e);       /* ~9 px still after 0.5 s */
    /*
     * Incommensurate sines, rough but without frame-to-frame jitter:
     * a slow 3-4 Hz rumble (the pressure wave) under a fast tremble.
     */
    *dx = (int)lroundf(amp * (0.5f * sinf(t * 22.0f)
                              + 0.3f * sinf(t * 71.0f + 0.4f) + 0.2f * sinf(t * 43.0f + 1.3f)));
    *dy = (int)lroundf(amp * (0.5f * sinf(t * 19.0f + 1.1f)
                              + 0.3f * sinf(t * 59.0f + 0.7f) + 0.2f * sinf(t * 37.0f + 2.1f)));
}

/* Nuke flash over the whole view, drawn after the world. */
void Fx_render_overlay(void)
{
    if (!fx_flash_ns || !tng_renderer)
        return;
    float t = (float)(SDL_GetTicksNS() - fx_flash_ns) / 1e9f;
    if (t >= 0.25f || !fxNukeFlash || Fx_classic()) {
        fx_flash_ns = 0;
        return;
    }
    float e = 1.0f - t / 0.25f;
    float a = 0.35f * fx_clamp(fx_flash_s, 0.0f, 1.0f) * e * e;
    SDL_FRect r = { (float)PLAYFIELD_X, 0.0f, (float)draw_width, (float)draw_height };
    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(tng_renderer, 255, 240, 220, (Uint8)(a * 255.0f));
    SDL_RenderFillRect(tng_renderer, &r);
    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
}

/* Ships that were tracked but are missing in this frame: did they blow up? */
static void fx_check_vanished(void)
{
    int i = 0;

    while (i < fx_num_tracks) {
        fx_track_t *t = &fx_tracks[i];
        long age = loops - t->loop;

        if (age == 0) {
            i++;
            continue;
        }

        if (age > 0 && age <= 3) {
            float px = t->x, py = t->y;
            if (t->has_vel) {
                px += t->vx * (float)age;
                py += t->vy * (float)age;
            }
            bool in_view = fx_in_server_view(px, py, 30.0f);

            if (in_view && (fx_wreck_near(px, py, 90.0f)
                            || fx_wreck_near(t->x, t->y, 90.0f))) {
                if (fxExplosions && fx_active()) {
                    float tps = fx_ticks_per_sec();
                    float vx = t->has_vel ? t->vx * tps : 0.0f;
                    float vy = t->has_vel ? t->vy * tps : 0.0f;
                    fx_spawn_explosion(px, py, vx, vy);
                }
                fx_tracks[i] = fx_tracks[--fx_num_tracks];
                continue;
            }
            i++;
            continue;
        }

        /* gone for too long (or loop counter reset): forget it */
        fx_tracks[i] = fx_tracks[--fx_num_tracks];
    }
}

/*
 * Called once per received frame, before anything is drawn: reads the
 * ships and wreckage of this frame directly, so effects attached to ships
 * (contrails) are in sync with the ships drawn afterwards.
 */
void Fx_frame_begin(void)
{
    int i;

    fx_num_sparks = 0;
    fx_shot_cur ^= 1;
    fx_num_shots[fx_shot_cur] = 0;
    fx_shot_loop[fx_shot_cur] = loops;
    fx_mtails_begin();
    fx_num_wrecks = 0;
    for (i = 0; i < num_wreckage && fx_num_wrecks < FX_MAX_WRECKS; i++) {
        fx_wreck_x[fx_num_wrecks] = (float)wreckage_ptr[i].x;
        fx_wreck_y[fx_num_wrecks] = (float)wreckage_ptr[i].y;
        fx_num_wrecks++;
    }
    for (i = 0; i < num_ship; i++) {
        fx_observe_ship(ship_ptr[i].id, ship_ptr[i].x, ship_ptr[i].y, ship_ptr[i].dir);
        fx_track_t *t = fx_track_by_id(ship_ptr[i].id);
        if (t)
            t->shield = ship_ptr[i].shield != 0;
    }
    fx_check_vanished();
    fx_mines_begin();
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

static SDL_Vertex fx_verts[FX_MAX_PARTICLES * 4];
static int        fx_idx[FX_MAX_PARTICLES * 6];
static int        fx_nv, fx_ni;

/*
 * Emit a textured quad. (sx, sy): screen center, (ax, ay) and (bx, by):
 * half-axes in screen pixels.
 */
static void fx_quad(float sx, float sy, float ax, float ay, float bx, float by,
                    int cell, SDL_FColor col)
{
    const float iw = 1.0f / (float)(FX_CELL * FX_ATLAS_COLS);
    const float ih = 1.0f / (float)(FX_CELL * FX_ATLAS_ROWS);
    float u0 = (float)((cell % FX_ATLAS_COLS) * FX_CELL) * iw + 0.5f * iw;
    float v0 = (float)((cell / FX_ATLAS_COLS) * FX_CELL) * ih + 0.5f * ih;
    float u1 = u0 + (float)FX_CELL * iw - iw;
    float v1 = v0 + (float)FX_CELL * ih - ih;
    SDL_Vertex *v = &fx_verts[fx_nv];
    int *ix = &fx_idx[fx_ni];

    v[0].position = (SDL_FPoint){ sx - ax - bx, sy - ay - by };
    v[1].position = (SDL_FPoint){ sx + ax - bx, sy + ay - by };
    v[2].position = (SDL_FPoint){ sx + ax + bx, sy + ay + by };
    v[3].position = (SDL_FPoint){ sx - ax + bx, sy - ay + by };
    v[0].tex_coord = (SDL_FPoint){ u0, v0 };
    v[1].tex_coord = (SDL_FPoint){ u1, v0 };
    v[2].tex_coord = (SDL_FPoint){ u1, v1 };
    v[3].tex_coord = (SDL_FPoint){ u0, v1 };
    v[0].color = v[1].color = v[2].color = v[3].color = col;

    ix[0] = fx_nv;
    ix[1] = fx_nv + 1;
    ix[2] = fx_nv + 2;
    ix[3] = fx_nv;
    ix[4] = fx_nv + 2;
    ix[5] = fx_nv + 3;
    fx_nv += 4;
    fx_ni += 6;
}

static SDL_FColor fx_lerp_color(const float (*keys)[4], int n, float t)
{
    /* keys: { t, r, g, b }, ascending t */
    int i;
    for (i = 0; i < n - 1 && t > keys[i + 1][0]; i++)
        ;
    if (i >= n - 1)
        return (SDL_FColor){ keys[n - 1][1], keys[n - 1][2], keys[n - 1][3], 1.0f };
    float f = (t - keys[i][0]) / (keys[i + 1][0] - keys[i][0]);
    f = fx_clamp01(f);
    return (SDL_FColor){
        keys[i][1] + (keys[i + 1][1] - keys[i][1]) * f,
        keys[i][2] + (keys[i + 1][2] - keys[i][2]) * f,
        keys[i][3] + (keys[i + 1][3] - keys[i][3]) * f,
        1.0f
    };
}

/* cluster mines: white-hot to light blue to deep blue */
static const float fire_blue_keys[][4] = {
    { 0.00f, 0.95f, 0.98f, 1.00f },
    { 0.15f, 0.70f, 0.88f, 1.00f },
    { 0.40f, 0.35f, 0.60f, 1.00f },
    { 0.70f, 0.15f, 0.25f, 0.75f },
    { 1.00f, 0.03f, 0.05f, 0.25f },
};

static const float fire_keys[][4] = {
    { 0.00f, 1.00f, 0.95f, 0.80f },
    { 0.15f, 1.00f, 0.75f, 0.30f },
    { 0.40f, 0.95f, 0.40f, 0.08f },
    { 0.70f, 0.55f, 0.12f, 0.02f },
    { 1.00f, 0.15f, 0.02f, 0.00f },
};

/* smoke lit by a nuclear fireball, cooling to grey */
static const float cloud_keys[][4] = {
    { 0.00f, 1.00f, 0.62f, 0.32f },
    { 0.30f, 0.85f, 0.52f, 0.36f },
    { 0.65f, 0.55f, 0.46f, 0.42f },
    { 1.00f, 0.38f, 0.36f, 0.36f },
};

static const float spark_blue_keys[][4] = {
    { 0.00f, 1.00f, 1.00f, 1.00f },
    { 0.40f, 0.60f, 0.80f, 1.00f },
    { 1.00f, 0.30f, 0.40f, 1.00f },
};

static const float spark_keys[][4] = {
    { 0.00f, 1.00f, 1.00f, 0.85f },
    { 0.30f, 1.00f, 0.70f, 0.25f },
    { 1.00f, 0.80f, 0.20f, 0.05f },
};

/* Append one particle to the current batch. */
static void fx_emit(const fx_particle_t *p, float scale)
{
    float t = p->age / p->life;
    float dx, dy, sx, sy, size;
    SDL_FColor col;

    fx_view_offset(p->x, p->y, &dx, &dy);
    if (fabsf(dx) > (float)ext_view_width * 0.5f + 150.0f
        || fabsf(dy) > (float)ext_view_height * 0.5f + 150.0f)
        return;
    sx = (float)PLAYFIELD_X + (dx + (float)ext_view_width * 0.5f) * scale;
    sy = ((float)ext_view_height * 0.5f - dy) * scale;

    switch (p->kind) {
    case FXK_FIRE: {
        float e = 1.0f - t;
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e);
        col = p->variant == FX_BLUE ? fx_lerp_color(fire_blue_keys, 5, t)
                                    : fx_lerp_color(fire_keys, 5, t);
        col.a = fx_clamp01(p->age / 0.05f) * powf(e, 0.8f);
        break;
    }
    case FXK_SMOKE: {
        float e = 1.0f - t;
        float g = 0.20f + 0.10f * p->seed;
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e);
        col = (SDL_FColor){ g, g, g * 0.95f, 0.5f * fx_smooth(t / 0.2f) * powf(e, 1.5f) };
        break;
    }
    case FXK_PUFF: {
        float e = 1.0f - t;
        float g = 0.55f + 0.12f * p->seed, a = 0.7f;
        if (p->variant == FX_SMOKE_LIGHT) {
            g = 0.82f;
            a = 0.55f;
        } else if (p->variant == FX_SMOKE_DARK) {
            g = 0.34f;
            a = 0.55f;
        } else if (p->variant == FX_SMOKE_FAINT) {
            g = 0.5f;
            a = 0.15f;
        }
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e);
        col = (SDL_FColor){ g, g, g, a * fx_smooth(t / 0.1f) * powf(e, 1.3f) };
        break;
    }
    case FXK_FLASH:
        size = p->size0 + (p->size1 - p->size0) * t;
        col = p->variant == FX_BLUE ? (SDL_FColor){ 0.78f, 0.90f, 1.0f, (1.0f - t) * (1.0f - t) }
                                    : (SDL_FColor){ 1.0f, 0.92f, 0.75f, (1.0f - t) * (1.0f - t) };
        break;
    case FXK_RING: {
        float e = 1.0f - t;
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e * e);
        col = p->variant == FX_BLUE ? (SDL_FColor){ 0.60f, 0.80f, 1.0f, 0.55f * e * e }
                                    : (SDL_FColor){ 1.0f, 0.70f, 0.40f, 0.55f * e * e };
        break;
    }
    case FXK_CLOUD: {
        float e = 1.0f - t;
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e);
        col = fx_lerp_color(cloud_keys, 4, t);
        col.a = 0.9f * fx_smooth(t / 0.06f) * powf(e, 1.1f);
        break;
    }
    case FXK_EMBER:
        size = p->size0 * (1.0f - 0.5f * t) * 2.5f;
        col = p->variant == FX_BLUE ? (SDL_FColor){ 0.45f, 0.70f, 1.0f, 1.0f }
                                    : (SDL_FColor){ 1.0f, 0.50f, 0.15f, 1.0f };
        col.a = (1.0f - t) * (0.55f + 0.45f * sinf(p->age * 35.0f + p->seed * 20.0f));
        break;
    case FXK_SPARK: {
        /* stretched glow along the velocity: motion-blurred streak */
        float sp = sqrtf(p->vx * p->vx + p->vy * p->vy);
        float ux = 1.0f, uy = 0.0f;
        if (sp > 0.001f) {
            ux = p->vx / sp;
            uy = -p->vy / sp;           /* screen y points down */
        }
        float len = fmaxf(p->size0 * 2.0f, sp * 0.045f) * scale;
        float wid = p->size0 * 2.5f * scale;
        col = p->variant == FX_BLUE ? fx_lerp_color(spark_blue_keys, 3, t)
                              : fx_lerp_color(spark_keys, 3, t);
        col.a = 1.0f - t;
        fx_quad(sx - ux * len * 0.5f, sy - uy * len * 0.5f,
                ux * len * 0.5f, uy * len * 0.5f,
                -uy * wid, ux * wid, p->cell, col);
        return;
    }
    default:
        return;
    }

    if (col.a <= 0.002f)
        return;

    size *= scale;
    float c = cosf(p->rot) * size, s = sinf(p->rot) * size;
    fx_quad(sx, sy, c, s, -s, c, p->cell, col);
}

static void fx_flush(SDL_BlendMode mode)
{
    if (fx_ni > 0) {
        SDL_SetTextureBlendMode(fx_atlas, mode);
        SDL_RenderGeometry(tng_renderer, fx_atlas, fx_verts, fx_nv, fx_idx, fx_ni);
    }
    fx_nv = 0;
    fx_ni = 0;
}

/*
 * Soft glow along a polyline (screen points): one quad with a soft
 * cross-section per segment plus a glow dot per point, drawn additively
 * right away. closed: also join the last point to the first.
 * width: half-width in screen pixels.
 */
void Fx_draw_polyline_glow(const SDL_FPoint *pts, int n, bool closed,
                           SDL_FColor col, float width)
{
    const float iw = 1.0f / (float)(FX_CELL * FX_ATLAS_COLS);
    const float ih = 1.0f / (float)(FX_CELL * FX_ATLAS_ROWS);
    const float u0 = (float)((CELL_GLOW % FX_ATLAS_COLS) * FX_CELL + 1) * iw;
    const float u1 = (float)((CELL_GLOW % FX_ATLAS_COLS + 1) * FX_CELL - 1) * iw;
    const float vm = ((float)(CELL_GLOW / FX_ATLAS_COLS) + 0.5f) * (float)FX_CELL * ih;

    if (!fx_atlas || !tng_renderer || n < 2 || n > FX_MAX_PARTICLES / 2)
        return;

    fx_nv = 0;
    fx_ni = 0;
    for (int i = 0; i < (closed ? n : n - 1); i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % n];
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f)
            continue;
        float nx = -dy / len * width, ny = dx / len * width;
        SDL_Vertex *v = &fx_verts[fx_nv];
        int *ix = &fx_idx[fx_ni];
        v[0].position = (SDL_FPoint){ a.x + nx, a.y + ny };
        v[1].position = (SDL_FPoint){ b.x + nx, b.y + ny };
        v[2].position = (SDL_FPoint){ b.x - nx, b.y - ny };
        v[3].position = (SDL_FPoint){ a.x - nx, a.y - ny };
        v[0].tex_coord = v[1].tex_coord = (SDL_FPoint){ u0, vm };
        v[2].tex_coord = v[3].tex_coord = (SDL_FPoint){ u1, vm };
        v[0].color = v[1].color = v[2].color = v[3].color = col;
        ix[0] = fx_nv;
        ix[1] = fx_nv + 1;
        ix[2] = fx_nv + 2;
        ix[3] = fx_nv;
        ix[4] = fx_nv + 2;
        ix[5] = fx_nv + 3;
        fx_nv += 4;
        fx_ni += 6;
    }
    /* round off the corners of outlines */
    for (int i = 0; closed && i < n; i++)
        fx_quad(pts[i].x, pts[i].y, width, 0.0f, 0.0f, width, CELL_GLOW, col);
    fx_flush(SDL_BLENDMODE_ADD);
}

/* Soft glow along a closed outline (n unique points). */
void Fx_draw_outline_glow(const SDL_FPoint *pts, int n, SDL_FColor col, float width)
{
    Fx_draw_polyline_glow(pts, n, true, col, width);
}

/* Append one wingtip contrail as a triangle strip with a soft cross-section. */
static void fx_emit_trail(const fx_trail_t *tr, int side, float scale, double now)
{
    const float iw = 1.0f / (float)(FX_CELL * FX_ATLAS_COLS);
    const float ih = 1.0f / (float)(FX_CELL * FX_ATLAS_ROWS);
    const float u0 = (float)((CELL_GLOW % FX_ATLAS_COLS) * FX_CELL + 1) * iw;
    const float u1 = (float)((CELL_GLOW % FX_ATLAS_COLS + 1) * FX_CELL - 1) * iw;
    const float vm = ((float)(CELL_GLOW / FX_ATLAS_COLS) + 0.5f) * (float)FX_CELL * ih;
    float sx[FX_TRAIL_PTS], sy[FX_TRAIL_PTS], age[FX_TRAIL_PTS], k[FX_TRAIL_PTS];
    int n = 0;

    /* collect live points, oldest first */
    for (int j = tr->count - 1; j >= 0; j--) {
        const fx_trail_pt_t *p = &tr->pt[side][(tr->head - j + FX_TRAIL_PTS) % FX_TRAIL_PTS];
        float a = (float)((now - p->t) / FX_TRAIL_LIFE);
        float dx, dy;
        if (a >= 1.0f)
            continue;
        fx_view_offset(p->x, p->y, &dx, &dy);
        sx[n] = (float)PLAYFIELD_X + (dx + (float)ext_view_width * 0.5f) * scale;
        sy[n] = ((float)ext_view_height * 0.5f - dy) * scale;
        age[n] = a < 0.0f ? 0.0f : a;
        k[n] = p->k;
        n++;
    }
    if (n < 2)
        return;

    /*
     * Headings come in RES steps and positions in whole pixels, so the
     * raw wingtip path is jagged: smooth it with two [1 2 1] passes,
     * keeping both ends in place.
     */
    for (int pass = 0; pass < 2; pass++) {
        float px = sx[0], py = sy[0];
        for (int j = 1; j < n - 1; j++) {
            float cx = sx[j], cy = sy[j];
            sx[j] = (px + 2.0f * cx + sx[j + 1]) * 0.25f;
            sy[j] = (py + 2.0f * cy + sy[j + 1]) * 0.25f;
            px = cx;
            py = cy;
        }
    }

    if (fx_nv + 2 * n > FX_MAX_PARTICLES * 4 || fx_ni + 6 * n > FX_MAX_PARTICLES * 6)
        fx_flush(SDL_BLENDMODE_ADD);

    int base = fx_nv;
    for (int j = 0; j < n; j++) {
        int a = j > 0 ? j - 1 : j, b = j < n - 1 ? j + 1 : j;
        float tx = sx[b] - sx[a], ty = sy[b] - sy[a];
        float len = sqrtf(tx * tx + ty * ty);
        float nx = 0.0f, ny = 0.0f;
        if (len > 0.001f) {
            nx = -ty / len;
            ny = tx / len;
        }
        /* vortices spread out and fade while they age */
        float w = (1.5f + 3.5f * age[j]) * scale;
        float e = 1.0f - age[j];
        SDL_FColor col = { 0.75f, 0.88f, 1.0f, 0.85f * k[j] * e * sqrtf(e) };
        SDL_Vertex *v = &fx_verts[fx_nv];
        v[0].position = (SDL_FPoint){ sx[j] + nx * w, sy[j] + ny * w };
        v[1].position = (SDL_FPoint){ sx[j] - nx * w, sy[j] - ny * w };
        v[0].tex_coord = (SDL_FPoint){ u0, vm };
        v[1].tex_coord = (SDL_FPoint){ u1, vm };
        v[0].color = v[1].color = col;
        fx_nv += 2;
        if (j > 0) {
            int q = base + 2 * (j - 1);
            int *ix = &fx_idx[fx_ni];
            ix[0] = q;
            ix[1] = q + 1;
            ix[2] = q + 3;
            ix[3] = q;
            ix[4] = q + 3;
            ix[5] = q + 2;
            fx_ni += 6;
        }
    }
}

/*
 * Engine flames
 *
 * Own ship: the thrust key. Other ships: the server's thrust sparks right
 * at the engine (also while hovering against gravity), or a clear
 * acceleration along the heading (also with sparks turned off). A short
 * hold bridges frames without sparks.
 */
#define FX_THRUST_ACCEL     0.12f   /* px/tick^2, well above the fit noise */
#define FX_THRUST_HOLD      3       /* frames */

static fx_track_t *fx_track_by_id(int id)
{
    for (int i = 0; i < fx_num_tracks; i++)
        if (fx_tracks[i].id == id)
            return &fx_tracks[i];
    return NULL;
}

/*
 * Hits on shields and armor. The server shows nothing for them, the shot
 * just disappears: a shot that was right at a ship in the previous frame
 * and has no successor within its flight distance hit it (the ship is
 * still there, so a shield or armor took it). Own ship: the armor count
 * dropping is exact.
 */
#define FX_HIT_NEAR     30.0f   /* shot this close to the ship center */
#define FX_SHOT_STEP    48.0f   /* max shot travel per frame, plus margin */

static void fx_hit_update(void)
{
    static int last_armor = -1;
    int prev = fx_shot_cur ^ 1;
    long dl = loops - fx_shot_loop[prev];
    Uint64 now = SDL_GetTicksNS();

    if (self) {
        int armor = numItems[ITEM_ARMOR];
        if (last_armor >= 0 && armor < last_armor) {
            fx_track_t *t = fx_track_by_id(self->id);
            if (t)
                t->hit_ns = now;
        }
        last_armor = armor;
    }

    if (dl < 1 || dl > 2)
        return;
    float step2 = (FX_SHOT_STEP * (float)dl) * (FX_SHOT_STEP * (float)dl);

    for (int k = 0; k < fx_num_shots[prev]; k++) {
        float px = fx_shot_x[prev][k], py = fx_shot_y[prev][k];
        fx_track_t *hit = NULL;
        float best = FX_HIT_NEAR * FX_HIT_NEAR;

        for (int i = 0; i < fx_num_tracks; i++) {
            fx_track_t *t = &fx_tracks[i];
            if (t->loop != loops)
                continue;       /* gone: blew up, no flash */
            float dx = fx_wrap_dx(px - t->x), dy = fx_wrap_dy(py - t->y);
            float d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2;
                hit = t;
            }
        }
        if (!hit)
            continue;
        /* own ship without shield: the armor count above is exact */
        if (self && hit->id == self->id && !hit->shield)
            continue;

        bool gone = true;
        for (int c = 0; gone && c < fx_num_shots[fx_shot_cur]; c++) {
            float dx = fx_wrap_dx(fx_shot_x[fx_shot_cur][c] - px);
            float dy = fx_wrap_dy(fx_shot_y[fx_shot_cur][c] - py);
            if (dx * dx + dy * dy < step2)
                gone = false;
        }
        if (gone)
            hit->hit_ns = now;
    }
}

/* 0..1: how strongly the shield or armor of ship id flashes right now. */
float Fx_ship_hit_flash(int id)
{
    fx_track_t *t;

    if (Fx_classic() || (t = fx_track_by_id(id)) == NULL || t->hit_ns == 0)
        return 0.0f;
    float age = (float)(SDL_GetTicksNS() - t->hit_ns) / 1e9f;
    if (age >= 0.35f)
        return 0.0f;
    float e = 1.0f - age / 0.35f;
    return e * sqrtf(e);
}

static void fx_flame_update(void)
{
    for (int i = 0; i < fx_num_tracks; i++) {
        fx_track_t *t = &fx_tracks[i];
        bool thrust;

        if (t->loop != loops)
            continue;
        if (self && t->id == self->id && eyesId == self->id) {
            thrust = Key_thrust_held();
        } else {
            position_t e = Ship_get_engine_position(fx_ship_shape(t->id), t->dir);
            float ex = t->x + (float)e.x, ey = t->y + (float)e.y;
            bool sign = t->thrust_valid && t->thrust_a > FX_THRUST_ACCEL;
            for (int k = 0; !sign && k < fx_num_sparks; k++) {
                float dx = fx_wrap_dx(fx_spark_x[k] - ex), dy = fx_wrap_dy(fx_spark_y[k] - ey);
                if (dx * dx + dy * dy < 24.0f * 24.0f)
                    sign = true;
            }
            if (sign)
                t->thrust_loop = loops;
            thrust = loops - t->thrust_loop <= FX_THRUST_HOLD;
        }
        float target = thrust ? 1.0f : 0.0f;
        t->flame_k += (target - t->flame_k) * (thrust ? 0.6f : 0.35f);
    }
}

/* A sputtering engine misfires: a small pop, a few sparks, a puff of smoke. */
static void fx_spawn_backfire(float x, float y, int dir)
{
    float ang = (float)dir * 2.0f * (float)M_PI / RES;
    float bx = -cosf(ang), by = -sinf(ang);     /* backwards, world y up */
    fx_particle_t *p;

    if ((p = fx_new(FXK_FLASH, x, y)) != NULL) {
        p->life = 0.08f;
        p->size0 = 5.0f;
        p->size1 = 9.0f;
        p->cell = CELL_GLOW;
    }
    for (int i = 0, n = 2 + fx_rand_int(2); i < n; i++) {
        float a = fx_range(-0.7f, 0.7f), sp = fx_range(80.0f, 170.0f);
        if ((p = fx_new(FXK_SPARK, x, y)) == NULL)
            break;
        p->vx = (bx * cosf(a) - by * sinf(a)) * sp;
        p->vy = (bx * sinf(a) + by * cosf(a)) * sp;
        p->life = fx_range(0.2f, 0.35f);
        p->size0 = fx_range(1.1f, 1.6f);
        p->drag = 2.5f;
        p->gravity = -60.0f;
        p->cell = CELL_GLOW;
    }
    for (int i = 0, n = 1 + fx_rand_int(2); i < n; i++) {
        if ((p = fx_new(FXK_PUFF, x + bx * 3.0f, y + by * 3.0f)) == NULL)
            break;
        float a = fx_range(-0.4f, 0.4f), sp = fx_range(30.0f, 55.0f);
        p->vx = (bx * cosf(a) - by * sinf(a)) * sp;
        p->vy = (bx * sinf(a) + by * cosf(a)) * sp;
        p->life = fx_range(0.55f, 0.85f);
        p->size0 = 4.0f;
        p->size1 = fx_range(13.0f, 18.0f);
        p->drag = 1.8f;
        p->gravity = 8.0f;
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-1.0f, 1.0f);
        p->cell = (uint8_t)(CELL_SMOKE0 + fx_rand_int(2));
    }
}

/*
 * Draw the flame of ship id, called right before its hull. (x, y): ship
 * world position as passed to Gui_paint_ship(), dir: heading.
 */
void Fx_paint_engine_flame(int id, int x, int y, int dir)
{
    fx_track_t *t;

    if (!fxEngineFlame || Fx_classic() || !fx_atlas || !tng_renderer)
        return;
    if ((t = fx_track_by_id(id)) == NULL || t->flame_k < 0.03f)
        return;

    position_t e = Ship_get_engine_position(fx_ship_shape(id), dir);
    float sc = clData.fscale;
    float sx = (float)PLAYFIELD_X + ((float)(x - world.x) + (float)e.x) * sc;
    float sy = ((float)(world.y + ext_view_height - y) - (float)e.y) * sc;
    float ang = (float)dir * 2.0f * (float)M_PI / RES;
    float bx = -cosf(ang), by = sinf(ang);      /* backwards, screen y down */
    float k = t->flame_k;
    float len = (15.0f + 13.0f * k) * sc;

    /*
     * Low fuel (own ship only, the server sends nobody else's fuel): below
     * the HUD warning level the flame starts to sputter - random cut-outs,
     * more often and longer the emptier the tank, now and then a puff of
     * smoke. With an empty tank the server flies on minimal power.
     */
    float sput = 0.0f;
    if (self && id == self->id && eyesId == self->id && fuelWarning > 0.0) {
        sput = fx_clamp01((float)((fuelWarning - fuelSum) / fuelWarning));
        if (fuelSum <= 0.0)
            len *= 0.6f;
    }
    if (sput > 0.0f) {
        if (t->sputter > 0) {
            t->sputter--;
        } else if (fx_rand() < 0.06f + 0.30f * sput) {
            t->sputter = 1 + fx_rand_int(1 + (int)(3.0f * sput));
            if (sput > 0.3f && fx_rand() < 0.35f + 0.4f * sput)
                fx_spawn_backfire(t->x + (float)e.x, t->y + (float)e.y, dir);
        }
        if (t->sputter > 0)
            k *= 0.12f;                     /* cut out, only a glimmer left */
        else
            len *= 1.0f - 0.35f * sput * fx_rand();   /* uneven */
    }
    float wid = 4.8f * sc;
    static const float spread[3] = { -0.33f, 0.0f, 0.33f };   /* ~ +-19 degrees */

    fx_nv = 0;
    fx_ni = 0;
    /* fanned out plumes: a long orange center, shorter redder sides, each flickering */
    for (int p = 0; p < 3; p++) {
        float a = spread[p] * (0.8f + 0.4f * fx_rand());
        float dx = bx * cosf(a) - by * sinf(a), dy = bx * sinf(a) + by * cosf(a);
        float l = len * (p == 1 ? 1.0f : 0.72f) * (0.8f + 0.4f * fx_rand());
        float w = wid * (p == 1 ? 1.0f : 0.8f);
        SDL_FColor c = p == 1 ? (SDL_FColor){ 1.0f, 0.50f, 0.12f, 0.85f * k }
                              : (SDL_FColor){ 1.0f, 0.32f, 0.07f, 0.70f * k };
        fx_quad(sx + dx * l * 0.42f, sy + dy * l * 0.42f,
                dx * l * 0.62f, dy * l * 0.62f, -dy * w, dx * w, CELL_GLOW, c);
    }
    /* core: blue-white, short */
    float cl = len * 0.42f * (0.85f + 0.3f * fx_rand());
    fx_quad(sx + bx * cl * 0.4f, sy + by * cl * 0.4f,
            bx * cl * 0.6f, by * cl * 0.6f, -by * wid * 0.6f, bx * wid * 0.6f,
            CELL_GLOW, (SDL_FColor){ 0.70f, 0.82f, 1.0f, 0.95f * k });
    /* hot spot at the nozzle */
    fx_quad(sx, sy, wid * 1.1f, 0.0f, 0.0f, wid * 1.1f,
            CELL_GLOW, (SDL_FColor){ 1.0f, 0.95f, 0.8f, 0.75f * k });
    fx_flush(SDL_BLENDMODE_ADD);
}

/*
 * Smoke and exhaust glow of one missile, called as it is drawn.
 * (x, y): world position of its nose, (sx, sy): screen position of its tail.
 */
void Fx_paint_missile(int x, int y, int len, int dir, float sx, float sy)
{
    int cur = fx_mtail_cur, prev = cur ^ 1;
    float ang = (float)dir * 2.0f * (float)M_PI / RES;
    float tx = (float)x - cosf(ang) * (float)len, ty = (float)y - sinf(ang) * (float)len;
    int variant, n;
    SDL_FColor fc;
    float flen;

    if (!fxMissileTrails || !fx_active())
        return;

    if (fx_num_mtails[cur] < FX_MAX_MTAILS) {
        fx_mtail_x[cur][fx_num_mtails[cur]] = tx;
        fx_mtail_y[cur][fx_num_mtails[cur]] = ty;
        fx_mtail_len[cur][fx_num_mtails[cur]] = len;
        fx_num_mtails[cur]++;
    }

    if (len == 12) {                    /* smart missile: yellow, thick white smoke */
        variant = FX_SMOKE_LIGHT;
        n = 3;
        fc = (SDL_FColor){ 1.0f, 0.90f, 0.30f, 0.9f };
        flen = 1.1f;
    } else if (len == 18) {             /* torpedo: blue, almost no smoke */
        variant = FX_SMOKE_FAINT;
        n = 1;
        fc = (SDL_FColor){ 0.40f, 0.65f, 1.0f, 0.9f };
        flen = 1.1f;
    } else {                            /* heat seeker (or undistinguished) */
        variant = FX_SMOKE_DARK;
        n = 3;
        fc = (SDL_FColor){ 1.0f, 0.32f, 0.06f, 0.95f };
        flen = 1.4f;
    }

    /* continue from last frame's tail so the trail has no gaps */
    float px = tx, py = ty;
    long dl = loops - fx_mtail_loop[prev];
    if (dl >= 1 && dl <= 3) {
        float best = (90.0f * (float)dl) * (90.0f * (float)dl);
        for (int i = 0; i < fx_num_mtails[prev]; i++) {
            if (fx_mtail_len[prev][i] != len)
                continue;
            float dx = fx_wrap_dx(fx_mtail_x[prev][i] - tx), dy = fx_wrap_dy(fx_mtail_y[prev][i] - ty);
            float d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2;
                px = tx + dx;
                py = ty + dy;
            }
        }
    }

    float bvx = -cosf(ang) * 40.0f, bvy = -sinf(ang) * 40.0f;
    float jit = variant == FX_SMOKE_LIGHT ? 12.0f : 7.0f;
    for (int i = 0, m = fx_count((float)n); i < m; i++) {
        float f = (float)(i + 1) / (float)m;
        fx_particle_t *p = fx_new(FXK_PUFF, px + (tx - px) * f, py + (ty - py) * f);
        if (!p)
            break;
        p->vx = bvx + fx_range(-jit, jit);
        p->vy = bvy + fx_range(-jit, jit);
        p->age = (1.0f - f) * (float)timePerFrame;     /* older the further back */
        p->life = fx_range(0.7f, 1.1f);
        p->size0 = 2.5f;
        p->size1 = variant == FX_SMOKE_LIGHT ? fx_range(11.0f, 15.0f) : fx_range(7.0f, 10.0f);
        p->rot = fx_range(0.0f, 6.283f);
        p->vrot = fx_range(-1.0f, 1.0f);
        p->drag = 1.5f;
        p->gravity = 6.0f;
        p->cell = (uint8_t)(CELL_SMOKE0 + fx_rand_int(2));
        p->variant = (uint8_t)variant;
    }

    /* exhaust glow at the tail */
    if (!fx_atlas || !tng_renderer)
        return;
    float sc = clData.fscale;
    float bx = -cosf(ang), by = sinf(ang);          /* backwards on screen */
    float l = 7.0f * flen * sc * (0.85f + 0.3f * fx_rand()), w = 2.4f * sc;
    fx_nv = 0;
    fx_ni = 0;
    fx_quad(sx + bx * l * 0.5f, sy + by * l * 0.5f, bx * l * 0.6f, by * l * 0.6f,
            -by * w, bx * w, CELL_GLOW, fc);
    fx_quad(sx, sy, w * 1.2f, 0.0f, 0.0f, w * 1.2f, CELL_GLOW,
            (SDL_FColor){ 1.0f, 1.0f, 0.9f, 0.8f });
    fx_flush(SDL_BLENDMODE_ADD);
}

/*
 * Soft capsule from a to b (screen pixels) with the glow cell's
 * cross-section: a stretched middle plus two half-dot caps, so the
 * ends round off without the overlap brightening them.
 * w: half-width in screen pixels; color fades from ca (a) to cb (b).
 */
static void fx_capsule(float ax, float ay, float bx, float by, float w,
                       SDL_FColor ca, SDL_FColor cb)
{
    const float iw = 1.0f / (float)(FX_CELL * FX_ATLAS_COLS);
    const float ih = 1.0f / (float)(FX_CELL * FX_ATLAS_ROWS);
    const float ul = (float)((CELL_GLOW % FX_ATLAS_COLS) * FX_CELL + 1) * iw;
    const float ur = (float)((CELL_GLOW % FX_ATLAS_COLS + 1) * FX_CELL - 1) * iw;
    const float um = (ul + ur) * 0.5f;
    const float vt = (float)((CELL_GLOW / FX_ATLAS_COLS) * FX_CELL + 1) * ih;
    const float vb = (float)((CELL_GLOW / FX_ATLAS_COLS + 1) * FX_CELL - 1) * ih;
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);

    if (len < 0.01f) {
        dx = 1.0f;
        dy = 0.0f;
    } else {
        dx /= len;
        dy /= len;
    }
    float tx = dx * w, ty = dy * w;             /* along */
    float nx = -dy * w, ny = dx * w;            /* across */
    /* columns: tail cap edge, tail, head, head cap edge */
    float px[4] = { ax - tx, ax, bx, bx + tx };
    float py[4] = { ay - ty, ay, by, by + ty };
    float uu[4] = { ul, um, um, ur };
    SDL_Vertex *v = &fx_verts[fx_nv];
    int *ix = &fx_idx[fx_ni];

    for (int i = 0; i < 4; i++) {
        v[2 * i].position = (SDL_FPoint){ px[i] + nx, py[i] + ny };
        v[2 * i].tex_coord = (SDL_FPoint){ uu[i], vt };
        v[2 * i + 1].position = (SDL_FPoint){ px[i] - nx, py[i] - ny };
        v[2 * i + 1].tex_coord = (SDL_FPoint){ uu[i], vb };
        v[2 * i].color = v[2 * i + 1].color = i < 2 ? ca : cb;
    }
    for (int i = 0; i < 3; i++) {
        int b = fx_nv + 2 * i;
        ix[6 * i + 0] = b;
        ix[6 * i + 1] = b + 2;
        ix[6 * i + 2] = b + 3;
        ix[6 * i + 3] = b;
        ix[6 * i + 4] = b + 3;
        ix[6 * i + 5] = b + 1;
    }
    fx_nv += 8;
    fx_ni += 18;
}

/*
 * Laser pulse as a glowing beam: wide soft halo in the beam color, a
 * mostly colored core and a thin white-hot center. Team/own pulses get
 * a more saturated blue than the palette so the color stays readable.
 * Returns false when the classic line should be drawn instead.
 */
bool Fx_paint_laser(int color, float x1, float y1, float x2, float y2)
{
    SDL_FColor c;
    float sc = clData.fscale, mix = 0.8f;

    if (!fxLaserGlow || !fx_active() || !fx_atlas || !tng_renderer)
        return false;

    if (color == BLUE)
        c = (SDL_FColor){ 0.25f, 0.45f, 1.0f, 1.0f };
    else if (color == RED) {
        c = (SDL_FColor){ 1.0f, 0.08f, 0.04f, 1.0f };
        mix = 0.9f;                     /* white washes red out to pink */
    }
    else
        c = SDL3_Get_FColor(color);

    SDL_FColor core = { c.r * mix + 1.0f - mix, c.g * mix + 1.0f - mix,
                        c.b * mix + 1.0f - mix, 1.0f };
    SDL_FColor halo = { c.r, c.g, c.b, 0.9f };

    fx_nv = 0;
    fx_ni = 0;
    SDL_FColor white = { 1.0f, 1.0f, 1.0f, 0.55f };
    fx_capsule(x1, y1, x2, y2, 8.0f * sc, halo, halo);
    fx_capsule(x1, y1, x2, y2, 2.2f * sc, core, core);
    fx_capsule(x1, y1, x2, y2, 1.1f * sc, white, white);
    fx_flush(SDL_BLENDMODE_ADD);
    return true;
}

static float fx_smooth01(float x)
{
    x = fx_clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

/*
 * Ship outline as a glowing line, stretched by st: forward (nose pulled
 * out) or, with back, the whole ship pulled out behind its fixed nose.
 */
static void fx_hj_outline(const fx_hj_t *h, float sx, float sy, float scale,
                          float st, float a, float wide, bool back)
{
    SDL_FPoint pts[FX_HJ_PTS];

    float nose = -1e9f;

    if (h->n < 2 || a <= 0.0f)
        return;
    for (int i = 0; i < h->n; i++)
        nose = fmaxf(nose, h->px[i] * h->hx + h->py[i] * h->hy);
    for (int i = 0; i < h->n; i++) {
        float along = h->px[i] * h->hx + h->py[i] * h->hy;
        float qx = h->px[i] - along * h->hx, qy = h->py[i] - along * h->hy;
        if (back)
            along = nose + (along - nose) * st;     /* pulled out behind the nose */
        else if (along > 0.0f)
            along *= st;
        pts[i].x = sx + (qx + along * h->hx) * scale;
        pts[i].y = sy - (qy + along * h->hy) * scale;
    }
    Fx_draw_polyline_glow(pts, h->n, true, (SDL_FColor){ 0.45f, 0.75f, 1.0f, 0.8f * a },
                          3.0f * wide * scale);
    Fx_draw_polyline_glow(pts, h->n, true, (SDL_FColor){ 1.0f, 1.0f, 1.0f, a },
                          1.2f * scale);
}

/* Light streak along the heading from back (dim) to front (bright), screen offsets. */
static void fx_hj_streak(float sx, float sy, float hx, float hy, float back, float front,
                         float scale, float a)
{
    float ax = sx - hx * back * scale, ay = sy + hy * back * scale;
    float bx = sx + hx * front * scale, by = sy - hy * front * scale;
    SDL_FColor c0 = { 0.45f, 0.75f, 1.0f, 0.1f * a }, c1 = { 0.45f, 0.75f, 1.0f, 0.9f * a };
    SDL_FColor w0 = { 1.0f, 1.0f, 1.0f, 0.1f * a }, w1 = { 1.0f, 1.0f, 1.0f, 0.9f * a };

    fx_capsule(ax, ay, bx, by, 6.0f * scale, c0, c1);
    fx_capsule(ax, ay, bx, by, 2.0f * scale, w0, w1);
}

/* Arrival: follow the ship so the afterglow stays on it. */
static void fx_hj_follow(fx_hj_t *h)
{
    fx_track_t *t = fx_track_by_id(h->id);
    shipshape_t *ship;

    if (!t || loops - t->loop > 4 || (ship = fx_ship_shape(h->id)) == NULL)
        return;
    h->x = t->x;
    h->y = t->y;
    h->n = 0;
    for (int i = 0; i < ship->num_points && i < FX_HJ_PTS; i++) {
        position_t pt = Ship_get_point_position(ship, i, t->dir);
        h->px[i] = (float)pt.x;
        h->py[i] = (float)pt.y;
        h->n++;
    }
}

/*
 * Ball connector as a glowing rope: a matt blue glow with a thin light
 * core, drawn in short pieces that tremble a tiny bit, so the rope
 * shimmers. Stretched beyond its rest length (ballConnectorLength,
 * 120 by default; the client is not told) it turns red, fully at the
 * maximum stretch (maxBallConnectorRatio 0.3). ball: (x1, y1) is the
 * ball, which gets a soft halo. Screen coordinates; returns false when
 * the classic line should be drawn instead.
 */
bool Fx_paint_tether(float x1, float y1, float x2, float y2, bool ball)
{
    const float rest = 120.0f, maxr = 0.3f;
    float sc = clData.fscale;
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    double now = (double)SDL_GetTicksNS() / 1e9;

    if (!fxTether || !fx_active() || !fx_atlas || !tng_renderer || len < 1.0f)
        return false;

    float st = fx_clamp01((len / sc - rest) / (rest * maxr));
    SDL_FColor blue = { 0x4E / 255.0f, 0x7C / 255.0f, 1.0f, 1.0f };
    SDL_FColor red = { 1.0f, 0.23f, 0.15f, 1.0f };
    SDL_FColor c = {
        blue.r + (red.r - blue.r) * st, blue.g + (red.g - blue.g) * st,
        blue.b + (red.b - blue.b) * st, 1.0f
    };
    SDL_FColor halo = { c.r, c.g, c.b, 0.24f };
    SDL_FColor core = { 0.5f + 0.5f * c.r, 0.5f + 0.5f * c.g, 0.5f + 0.5f * c.b, 0.45f };
    float ux = dx / len, uy = dy / len;
    /* leave the ball's rim free */
    float ax = x1 + ux * (ball ? 10.0f * sc : 0.0f), ay = y1 + uy * (ball ? 10.0f * sc : 0.0f);
    float bx = x2, by = y2;
    int n = (int)(len / (6.0f * sc));
    float ph = (float)fmod(now * 90.0, 2.0 * M_PI);
    float px = ax, py = ay;

    if (n < 2)
        n = 2;
    if (n > 64)
        n = 64;
    fx_nv = 0;
    fx_ni = 0;
    if (ball)
        fx_quad(x1, y1, 31.0f * sc, 0.0f, 0.0f, 31.0f * sc, CELL_GLOW,
                (SDL_FColor){ c.r, c.g, c.b, 0.3f });
    for (int i = 1; i <= n; i++) {
        float u = (float)i / (float)n;
        float amp = 0.25f * sc * sinf(u * (float)M_PI) * sinf(ph + u * 40.0f);
        float x = ax + (bx - ax) * u - uy * amp, y = ay + (by - ay) * u + ux * amp;
        fx_capsule(px, py, x, y, 9.0f * sc, halo, halo);
        fx_capsule(px, py, x, y, 2.0f * sc, core, core);
        px = x;
        py = y;
    }
    fx_flush(SDL_BLENDMODE_ADD);
    return true;
}

/* ------------------------------------------------------------------ */
/* Wormholes                                                          */
/* ------------------------------------------------------------------ */

static const SDL_FColor FX_WORM_RED = { 1.0f, 0.25f, 0.12f, 1.0f };
static const SDL_FColor FX_WORM_ORANGE = { 1.0f, 0.55f, 0.18f, 1.0f };

static SDL_FColor fx_alpha(SDL_FColor c, float a)
{
    c.a = fx_clamp01(a);
    return c;
}

/*
 * Tunnel: glowing rings that keep moving into the depth and shrink
 * towards a slowly circling vanishing point; out: rings come out instead
 * (exit-only wormholes). (sx, sy), r: screen pixels.
 */
static void fx_tunnel(float sx, float sy, float r, bool out)
{
    double now = (double)SDL_GetTicksNS() / 1e9;
    float ph = (float)fmod(now * M_PI, 4.0 * M_PI);
    float fr = (float)fmod(now * 2.0, 1.0);

    if (out)
        fr = 1.0f - fr;                     /* exits: rings come out */
    float ang = ph * 0.5f, thick = sqrtf(fmaxf(r / 18.0f, 1.0f));
    SDL_FPoint pts[49];

    if (!fx_atlas || !tng_renderer || r < 2.0f)
        return;
    fx_nv = 0;
    fx_ni = 0;
    fx_quad(sx, sy, r * 1.3f, 0.0f, 0.0f, r * 1.3f, CELL_GLOW, fx_alpha(FX_WORM_RED, 0.3f));
    fx_flush(SDL_BLENDMODE_ADD);
    for (int k = 0; k < 6; k++) {
        float z = ((float)k + fr) / 6.0f;          /* 0 outer .. 1 deep */
        float rr = r * (1.0f - z) * (1.0f - z) + 1.0f;
        float off = r * 0.25f * z;
        float cx = sx + off * cosf(ang), cy = sy + off * sinf(ang);
        int n = (int)(12.0f + rr * 0.5f);
        if (n > 48)
            n = 48;
        for (int j = 0; j <= n; j++) {
            float a = (float)j * 2.0f * (float)M_PI / (float)n;
            pts[j].x = cx + rr * cosf(a);
            pts[j].y = cy + rr * sinf(a);
        }
        float al = (0.25f + 0.75f * z) * fx_clamp01(z * 6.0f);
        Fx_draw_polyline_glow(pts, n + 1, false,
                              fx_alpha(z < 0.5f ? FX_WORM_RED : FX_WORM_ORANGE, al),
                              (1.6f + 1.4f * (1.0f - z)) * thick);
    }
    float vx = sx + r * 0.25f * cosf(ang), vy = sy + r * 0.25f * sinf(ang);
    float vr = r * 0.08f + 2.5f;
    fx_nv = 0;
    fx_ni = 0;
    fx_quad(vx, vy, vr * 2.0f, 0.0f, 0.0f, vr * 2.0f, CELL_GLOW,
            (SDL_FColor){ 1.0f, 0.85f, 0.5f, 1.0f });
    fx_flush(SDL_BLENDMODE_ADD);
}

/* Block maps: wormhole at world center (x, y). False in classic mode. */
bool Fx_paint_wormhole_at(int x, int y, int r)
{
    if (!fx_worms_on())
        return false;
    fx_tunnel(Screen_X(x), Screen_Y(y), (float)r * clData.fscale, false);
    return true;
}

/* Polygon maps: map object obj at world corner (x, y), wrap included. */
void Fx_paint_wormhole_obj(int obj, int x, int y)
{
    if (!fx_worms_on())
        return;
    fx_worms_build();
    for (int i = 0; i < fx_num_worms; i++) {
        const fx_worm_t *w = &fx_worms[i];
        if (w->obj != obj)
            continue;
        if (w->type == SETUP_WORM_OUT && w->poly < 0)
            return;                 /* unmarked exits stay invisible */
        int cx = x + (int)(w->x - (float)mapobjects[obj].bounds.x);
        int cy = y + (int)(w->y - (float)mapobjects[obj].bounds.y);
        fx_tunnel(Screen_X(cx), Screen_Y(cy), w->r * clData.fscale,
                  w->type == SETUP_WORM_OUT);
        return;
    }
}

/* True if polygon i marks a wormhole that is drawn as a tunnel instead. */
bool Fx_wormhole_polygon(int i)
{
    if (!fx_worms_on() || num_mapobjects == 0)
        return false;
    fx_worms_build();
    for (int k = 0; k < fx_num_worms; k++)
        if (fx_worms[k].poly == i)
            return true;
    return false;
}

/* Swirling streaks, inwards (entry) or unwinding outwards (exit). */
static void fx_worm_vortex(float sx, float sy, float wr, float t, bool out, float scale)
{
    unsigned seed = 99;
    float rs = fmaxf(wr, 17.5f) / 17.5f;

    fx_nv = 0;
    fx_ni = 0;
    for (int i = 0; i < 30; i++) {
        seed = seed * 1103515245u + 12345u;
        float a0 = (float)((seed >> 8) & 0xffff) / 65535.0f * 6.283f;
        seed = seed * 1103515245u + 12345u;
        float r0 = (14.0f + (float)((seed >> 8) & 0xffff) / 65535.0f * 30.0f) * rs * scale;
        float ph = out ? t / 1.1f : 1.0f - t / 0.4f;
        if (ph < 0.0f || ph > 1.05f)
            continue;
        float d = out ? -0.14f : 0.14f;
        float r = r0 * ph, ang = a0 + (1.0f - ph) * 4.5f;
        float r2 = r0 * (ph + d), ang2 = a0 + (1.0f - ph - d) * 4.5f;
        float al = out ? (1.0f - ph * 0.6f) * (1.0f - fx_smooth01((ph - 0.6f) / 0.45f))
                       : 0.4f + 0.6f * (1.0f - ph);
        SDL_FColor c = i % 3 ? FX_WORM_RED : FX_WORM_ORANGE;
        fx_capsule(sx + r2 * cosf(ang2), sy + r2 * sinf(ang2),
                   sx + r * cosf(ang), sy + r * sinf(ang), 3.1f * scale,
                   fx_alpha(c, 0.0f), fx_alpha(c, 0.9f * al));
    }
    fx_flush(SDL_BLENDMODE_ADD);
}

/* Ship outline glowing red-orange, turned by rot and scaled by size. */
static void fx_worm_ghost(const fx_hj_t *h, float sx, float sy, float scale,
                          float rot, float size, float a, float wide)
{
    SDL_FPoint pts[FX_HJ_PTS + 1];
    float c = cosf(rot), s = sinf(rot);

    if (h->n < 2 || a <= 0.0f)
        return;
    for (int i = 0; i < h->n; i++) {
        float px = (h->px[i] * c - h->py[i] * s) * size;
        float py = (h->px[i] * s + h->py[i] * c) * size;
        pts[i].x = sx + px * scale;
        pts[i].y = sy - py * scale;
    }
    Fx_draw_polyline_glow(pts, h->n, true, fx_alpha(FX_WORM_ORANGE, 0.8f * a),
                          3.0f * wide * scale);
    Fx_draw_polyline_glow(pts, h->n, true, (SDL_FColor){ 1.0f, 0.9f, 0.8f, a },
                          1.2f * scale);
}

static void fx_worm_flash(float sx, float sy, float scale, float f)
{
    if (f < 0.01f)
        return;
    fx_nv = 0;
    fx_ni = 0;
    fx_quad(sx, sy, 22.0f * scale, 0.0f, 0.0f, 22.0f * scale, CELL_GLOW,
            fx_alpha(FX_WORM_ORANGE, 0.8f * f));
    fx_quad(sx, sy, 7.0f * scale, 0.0f, 0.0f, 7.0f * scale, CELL_GLOW,
            (SDL_FColor){ 1.0f, 1.0f, 1.0f, f });
    fx_flush(SDL_BLENDMODE_ADD);
}

static void fx_world_to_screen(float wx, float wy, float scale, float *sx, float *sy)
{
    float dx, dy;

    fx_view_offset(wx, wy, &dx, &dy);
    *sx = (float)PLAYFIELD_X + (dx + (float)ext_view_width * 0.5f) * scale;
    *sy = ((float)ext_view_height * 0.5f - dy) * scale;
}

/* Going through a wormhole: pulled into the vortex, or spun out of it. */
static void fx_worm_render(fx_hj_t *h, float t, float scale)
{
    float wsx, wsy;

    fx_world_to_screen(h->wx, h->wy, scale, &wsx, &wsy);
    if (!h->arrive) {
        float k = 1.0f - fx_smooth01(t / 0.35f);
        float shx, shy;
        fx_worm_vortex(wsx, wsy, h->wr, t, false, scale);
        fx_world_to_screen(h->x, h->y, scale, &shx, &shy);
        if (k > 0.03f)
            fx_worm_ghost(h, wsx + (shx - wsx) * k * k, wsy + (shy - wsy) * k * k, scale,
                          6.0f * (1.0f - k) * (1.0f - k), 0.3f + 0.7f * k,
                          fminf(1.0f, k * 1.5f), 1.0f + (1.0f - k));
        fx_worm_flash(wsx, wsy, scale, expf(-(t - 0.36f) * (t - 0.36f) / 0.003f));
    } else {
        float shx, shy;
        fx_worm_flash(wsx, wsy, scale, expf(-t / 0.08f));
        fx_worm_vortex(wsx, wsy, h->wr, t, true, scale);
        fx_hj_follow(h);
        fx_world_to_screen(h->x, h->y, scale, &shx, &shy);
        fx_worm_ghost(h, shx, shy, scale, 0.0f, 1.0f, 1.0f - fx_smooth01(t / 0.9f), 2.0f);
    }
}

static void fx_hj_render(float scale, double now)
{
    int i = 0;

    while (i < fx_num_hj) {
        fx_hj_t *h = &fx_hj[i];
        float t = (float)(now - h->t0), dx, dy;

        float life = h->kind == FX_JK_WORM ? (h->arrive ? FX_WT_OUT : FX_WT_IN)
                                           : (h->arrive ? FX_HJ_IN : FX_HJ_OUT);
        if (t > life) {
            fx_hj[i] = fx_hj[--fx_num_hj];
            continue;
        }
        i++;
        if (t < 0.0f)
            t = 0.0f;
        if (h->kind == FX_JK_WORM) {
            fx_world_to_screen(h->wx, h->wy, scale, &dx, &dy);
            if (dx > -300.0f && dy > -300.0f
                && dx < (float)ext_view_width * scale + 300.0f + (float)PLAYFIELD_X
                && dy < (float)ext_view_height * scale + 300.0f)
                fx_worm_render(h, t, scale);
            continue;
        }
        if (h->arrive)
            fx_hj_follow(h);
        fx_view_offset(h->x, h->y, &dx, &dy);
        float sx = (float)PLAYFIELD_X + (dx + (float)ext_view_width * 0.5f) * scale;
        float sy = ((float)ext_view_height * 0.5f - dy) * scale;
        if (fabsf(dx) > (float)ext_view_width * 0.5f + 200.0f
            || fabsf(dy) > (float)ext_view_height * 0.5f + 200.0f)
            continue;

        if (!h->arrive) {
            /* stretches into a streak along the heading and is gone */
            float st = 1.0f + 9.0f * fx_smooth01(t / 0.12f);
            float a = 1.0f - fx_smooth01((t - 0.08f) / 0.12f);
            fx_hj_outline(h, sx, sy, scale, st > 3.0f ? 3.0f : st, a, 1.0f, false);
            fx_nv = 0;
            fx_ni = 0;
            if (a > 0.0f)
                fx_hj_streak(sx, sy, h->hx, h->hy, 9.0f, 14.0f * st, scale, a);
            if (t > 0.1f) {
                float f = expf(-(t - 0.14f) * (t - 0.14f) / 0.003f);
                float fx = sx + h->hx * 140.0f * scale, fy = sy - h->hy * 140.0f * scale;
                fx_quad(fx, fy, 14.0f * scale, 0.0f, 0.0f, 14.0f * scale, CELL_GLOW,
                        (SDL_FColor){ 0.45f, 0.75f, 1.0f, f });
                fx_quad(fx, fy, 5.0f * scale, 0.0f, 0.0f, 5.0f * scale, CELL_GLOW,
                        (SDL_FColor){ 1.0f, 1.0f, 1.0f, f });
            }
            fx_flush(SDL_BLENDMODE_ADD);
        } else {
            /*
             * shoots in on a long streak from behind that shrinks into the
             * stretched ship, a flash on arrival, then the outline glows
             * on for a second
             */
            float k = fx_smooth01(t / 0.25f);
            float a = 1.0f - fx_smooth01((t - 0.25f) / 1.0f);
            fx_nv = 0;
            fx_ni = 0;
            if (k < 1.0f)
                fx_hj_streak(sx, sy, h->hx, h->hy, 360.0f * (1.0f - k), 14.0f, scale, 1.0f);
            float f = expf(-(t - 0.22f) * (t - 0.22f) / 0.004f);
            float g = 0.5f * a * fx_smooth01(t / 0.15f) + 0.6f * f;
            fx_quad(sx, sy, 30.0f * scale, 0.0f, 0.0f, 30.0f * scale, CELL_GLOW,
                    (SDL_FColor){ 0.45f, 0.75f, 1.0f, fx_clamp01(g) });
            fx_quad(sx, sy, 7.0f * scale, 0.0f, 0.0f, 7.0f * scale, CELL_GLOW,
                    (SDL_FColor){ 1.0f, 1.0f, 1.0f, 0.8f * f });
            fx_flush(SDL_BLENDMODE_ADD);
            fx_hj_outline(h, sx, sy, scale, 1.0f + 5.0f * (1.0f - k), a, 2.2f, true);
            fx_hj_outline(h, sx, sy, scale, 1.0f + 5.0f * (1.0f - k), a, 1.0f, true);
        }
    }
}

void Fx_render(void)
{
    static Uint64 last_ns;
    Uint64 now = SDL_GetTicksNS();
    float dt = last_ns ? (float)(now - last_ns) / 1e9f : 0.0f;
    float scale = clData.fscale;
    int i;

    last_ns = now;
    if (!fx_active()) {
        fx_clear();
        return;
    }
    if (dt > 0.1f)
        dt = 0.1f;
    fx_update(dt);
    fx_flame_update();
    fx_hit_update();
    fx_mines_resolve();

    if (!fx_atlas || !tng_renderer)
        return;

    /* pass 0: contrails, additive */
    if (fxContrails) {
        for (i = 0; i < fx_num_tracks; i++)
            if (fx_tracks[i].trail.count >= 2)
                for (int side = 0; side < 2; side++)
                    fx_emit_trail(&fx_tracks[i].trail, side, scale, (double)now / 1e9);
        fx_flush(SDL_BLENDMODE_ADD);
    }

    /* hyperjump streaks, additive */
    if (fx_num_hj > 0)
        fx_hj_render(scale, (double)now / 1e9);

    if (fx_num_parts == 0)
        return;

    /* pass 1: smoke, normal alpha blending */
    for (i = 0; i < fx_num_parts; i++)
        if ((fx_parts[i].kind == FXK_SMOKE || fx_parts[i].kind == FXK_PUFF
             || fx_parts[i].kind == FXK_CLOUD)
            && fx_parts[i].age > 0.0f)
            fx_emit(&fx_parts[i], scale);
    fx_flush(SDL_BLENDMODE_BLEND);

    /* pass 2: everything glowing, additive */
    for (i = 0; i < fx_num_parts; i++)
        if (fx_parts[i].kind != FXK_SMOKE && fx_parts[i].kind != FXK_PUFF
            && fx_parts[i].kind != FXK_CLOUD
            && fx_parts[i].age > 0.0f)
            fx_emit(&fx_parts[i], scale);
    fx_flush(SDL_BLENDMODE_ADD);
}
