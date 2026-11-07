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

static bool   fxExplosions = true;
static bool   fxSparks = true;
static double fxAmount = 1.0;

static xp_option_t fx_options[] = {
    XP_BOOL_OPTION("fxExplosions", true, &fxExplosions, NULL,
                   XP_OPTFLAG_CONFIG_DEFAULT,
                   "Draw fireball explosions when ships are destroyed.\n"),
    XP_BOOL_OPTION("fxSparks", true, &fxSparks, NULL,
                   XP_OPTFLAG_CONFIG_DEFAULT,
                   "Draw sparks when ships hit walls or each other.\n"),
    XP_DOUBLE_OPTION("fxAmount", 1.0, 0.0, 2.0, &fxAmount, NULL,
                     XP_OPTFLAG_CONFIG_DEFAULT,
                     "Particle amount multiplier for visual effects.\n"),
};

void Store_fx_options(void)
{
    STORE_OPTIONS(fx_options);
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
}

/* ------------------------------------------------------------------ */
/* Particles                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    FXK_FIRE,
    FXK_SMOKE,
    FXK_FLASH,
    FXK_RING,
    FXK_SPARK,
    FXK_EMBER
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

/* ------------------------------------------------------------------ */
/* Event detection                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int     id;
    float   x, y;           /* last seen world position */
    float   vx, vy;         /* pixels per server tick */
    long    loop;           /* server frame of last sighting */
    bool    has_vel;
    Uint64  spark_ns;       /* time of last spark burst (cooldown) */
} fx_track_t;

#define FX_MAX_TRACKS   128
#define FX_MAX_WRECKS   128

static fx_track_t fx_tracks[FX_MAX_TRACKS];
static int        fx_num_tracks;

static float fx_wreck_x[FX_MAX_WRECKS], fx_wreck_y[FX_MAX_WRECKS];
static int   fx_num_wrecks;

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

    float nx = dvx / dv, ny = dvy / dv;
    /* sliding component of the old velocity along the surface */
    float along = ox * -ny + oy * nx;
    float cx = t->x - nx * SHIP_SZ * 0.8f;
    float cy = t->y - ny * SHIP_SZ * 0.8f;

    fx_spawn_sparks(cx, cy, nx, ny, -ny * along, nx * along, dv);
}

void Fx_observe_ship(int id, int x, int y)
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
        t->loop = loops;
        return;
    }

    dl = loops - t->loop;
    if (dl == 0)
        return;

    float dx = fx_wrap_dx((float)x - t->x);
    float dy = fx_wrap_dy((float)y - t->y);

    /* frame gap, server restart or teleport/wormhole: restart tracking */
    if (dl < 0 || dl > 4 || dx * dx + dy * dy > 120.0f * 120.0f * (float)(dl * dl)) {
        t->has_vel = false;
    } else {
        float vx = dx / (float)dl, vy = dy / (float)dl;
        if (t->has_vel && fxSparks && fxAmount > 0.0)
            fx_check_impact(t, vx, vy);
        t->vx = vx;
        t->vy = vy;
        t->has_vel = true;
    }
    t->x = (float)x;
    t->y = (float)y;
    t->loop = loops;
}

void Fx_observe_wreck(int x, int y)
{
    if (fx_num_wrecks < FX_MAX_WRECKS) {
        fx_wreck_x[fx_num_wrecks] = (float)x;
        fx_wreck_y[fx_num_wrecks] = (float)y;
        fx_num_wrecks++;
    }
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

void Fx_end_frame(void)
{
    int i = 0;

    while (i < fx_num_tracks) {
        fx_track_t *t = &fx_tracks[i];
        long age = loops - t->loop;

        if (age == 0) {
            i++;
            continue;
        }

        /* ship vanished: check whether it blew up */
        if (age > 0 && age <= 3) {
            float px = t->x, py = t->y, dx, dy;
            if (t->has_vel) {
                px += t->vx * (float)age;
                py += t->vy * (float)age;
            }
            fx_view_offset(px, py, &dx, &dy);
            bool in_view = fabsf(dx) < (float)ext_view_width * 0.5f - 30.0f
                        && fabsf(dy) < (float)ext_view_height * 0.5f - 30.0f;

            if (in_view && (fx_wreck_near(px, py, 90.0f)
                            || fx_wreck_near(t->x, t->y, 90.0f))) {
                if (fxExplosions && fxAmount > 0.0) {
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

    fx_num_wrecks = 0;
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

static const float fire_keys[][4] = {
    { 0.00f, 1.00f, 0.95f, 0.80f },
    { 0.15f, 1.00f, 0.75f, 0.30f },
    { 0.40f, 0.95f, 0.40f, 0.08f },
    { 0.70f, 0.55f, 0.12f, 0.02f },
    { 1.00f, 0.15f, 0.02f, 0.00f },
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
        col = fx_lerp_color(fire_keys, 5, t);
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
    case FXK_FLASH:
        size = p->size0 + (p->size1 - p->size0) * t;
        col = (SDL_FColor){ 1.0f, 0.92f, 0.75f, (1.0f - t) * (1.0f - t) };
        break;
    case FXK_RING: {
        float e = 1.0f - t;
        size = p->size0 + (p->size1 - p->size0) * (1.0f - e * e * e);
        col = (SDL_FColor){ 1.0f, 0.70f, 0.40f, 0.55f * e * e };
        break;
    }
    case FXK_EMBER:
        size = p->size0 * (1.0f - 0.5f * t) * 2.5f;
        col = (SDL_FColor){ 1.0f, 0.50f, 0.15f,
                            (1.0f - t) * (0.55f + 0.45f * sinf(p->age * 35.0f + p->seed * 20.0f)) };
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
        col = fx_lerp_color(spark_keys, 3, t);
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

void Fx_render(void)
{
    static Uint64 last_ns;
    Uint64 now = SDL_GetTicksNS();
    float dt = last_ns ? (float)(now - last_ns) / 1e9f : 0.0f;
    float scale = clData.fscale;
    int i;

    last_ns = now;
    if (dt > 0.1f)
        dt = 0.1f;
    fx_update(dt);

    if (!fx_atlas || !tng_renderer || fx_num_parts == 0)
        return;

    /* pass 1: smoke, normal alpha blending */
    for (i = 0; i < fx_num_parts; i++)
        if (fx_parts[i].kind == FXK_SMOKE && fx_parts[i].age > 0.0f)
            fx_emit(&fx_parts[i], scale);
    fx_flush(SDL_BLENDMODE_BLEND);

    /* pass 2: everything glowing, additive */
    for (i = 0; i < fx_num_parts; i++)
        if (fx_parts[i].kind != FXK_SMOKE && fx_parts[i].age > 0.0f)
            fx_emit(&fx_parts[i], scale);
    fx_flush(SDL_BLENDMODE_ADD);
}
