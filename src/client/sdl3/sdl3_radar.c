/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Authentic 256xRadarHeight Radar Display (matching ng-x11)
 */

#include "sdl3_client.h"

int wallRadarColor = BLUE;
int decorRadarColor = 6;
int targetRadarColor = 4;

static SDL_Texture *static_radar_texture = NULL;
static SDL_Texture *frame_radar_texture = NULL;
static bool radar_needs_redraw = true;

void Radar_show_target(int x, int y)
{
    (void)x; (void)y;
    radar_needs_redraw = true;
}

void Radar_hide_target(int x, int y)
{
    (void)x; (void)y;
    radar_needs_redraw = true;
}

void Paint_world_radar(void)
{
    radar_needs_redraw = true;
}

void Paint_sliding_radar(void)
{
    radar_needs_redraw = true;
}

static void Compute_radar_bounds(ipos_t *min, ipos_t *max, const irec_t *b)
{
    min->x = (0 - (b->x + b->w)) / Setup->width;
    if (0 > b->x + b->w) min->x++;
    max->x = (0 + Setup->width - b->x) / Setup->width;
    if (0 + Setup->width < b->x) max->x--;
    min->y = (0 - (b->y + b->h)) / Setup->height;
    if (0 > b->y + b->h) min->y++;
    max->y = (0 + Setup->height - b->y) / Setup->height;
    if (0 + Setup->height < b->y) max->y--;
}

static void Paint_radar_block(int xi, int yi, int color)
{
    double xs, ys;
    int xp, yp, xw, yw;
    int rh = (int)RadarHeight;

    SDL3_Set_Draw_Color(color);
    if (Setup->x >= 256) {
        xs = (double)(256 - 1) / (Setup->x - 1);
        ys = (double)(rh - 1) / (Setup->y - 1);
        xp = (int)(xi * xs + 0.5);
        yp = rh - 1 - (int)(yi * ys + 0.5);
        SDL_RenderPoint(tng_renderer, (float)xp, (float)yp);
    } else {
        xs = (double)(Setup->x - 1) / (256 - 1);
        ys = (double)(Setup->y - 1) / (rh - 1);
        xp = (int)((xi - 0.5) / xs) + 1;
        xw = (int)((xi + 0.5) / xs);
        yw = (int)((yi - 0.5) / ys) + 1;
        yp = (int)((yi + 0.5) / ys);
        xw -= xp;
        yw = yp - yw;
        yp = rh - 1 - yp;
        SDL_FRect r = { (float)xp, (float)yp, (float)(xw + 1), (float)(yw + 1) };
        SDL_RenderFillRect(tng_renderer, &r);
    }
}

static void Render_static_radar_old(void)
{
    int i, xi, yi, xm, ym, xp, yp = 0;
    int xmoff, xioff;
    int type, vis;
    double damage;
    double xs, ys;
    int rh = (int)RadarHeight;
    uint8_t visible[256];
    uint8_t visibleColor[256];

    memset(visible, 0, sizeof visible);
    visible[SETUP_FILLED] = 1;
    visible[SETUP_FILLED_NO_DRAW] = 1;
    visible[SETUP_REC_LU] = 1;
    visible[SETUP_REC_RU] = 1;
    visible[SETUP_REC_LD] = 1;
    visible[SETUP_REC_RD] = 1;
    visible[SETUP_FUEL] = 1;
    for (i = 0; i < 10; i++)
        visible[SETUP_TARGET + i] = 1;
    for (i = BLUE_BIT; i < (int)sizeof visible; i++)
        visible[i] = 1;
    if (instruments.showDecor) {
        visible[SETUP_DECOR_FILLED] = 1;
        visible[SETUP_DECOR_LU] = 1;
        visible[SETUP_DECOR_RU] = 1;
        visible[SETUP_DECOR_LD] = 1;
        visible[SETUP_DECOR_RD] = 1;
    }

    memset(visibleColor, 0, sizeof visibleColor);
    visibleColor[SETUP_FILLED] =
        visibleColor[SETUP_FILLED_NO_DRAW] =
        visibleColor[SETUP_REC_LU] =
        visibleColor[SETUP_REC_RU] =
        visibleColor[SETUP_REC_LD] =
        visibleColor[SETUP_REC_RD] =
        visibleColor[SETUP_FUEL] = (uint8_t)wallRadarColor;
    for (i = 0; i < 10; i++)
        visibleColor[SETUP_TARGET + i] = (uint8_t)targetRadarColor;
    for (i = BLUE_BIT; i < (int)sizeof visible; i++)
        visibleColor[i] = (uint8_t)wallRadarColor;
    if (instruments.showDecor) {
        visibleColor[SETUP_DECOR_FILLED] =
            visibleColor[SETUP_DECOR_LU] =
            visibleColor[SETUP_DECOR_RU] =
            visibleColor[SETUP_DECOR_LD] =
            visibleColor[SETUP_DECOR_RD] = (uint8_t)decorRadarColor;
    }

    if (Setup->x >= 256) {
        xs = (double)(256 - 1) / (Setup->x - 1);
        ys = (double)(rh - 1) / (Setup->y - 1);
        for (xi = 0; xi < Setup->x; xi++) {
            xp = (int)(xi * xs + 0.5);
            xioff = xi * Setup->y;
            for (yi = 0; yi < Setup->y; yi++) {
                type = Setup->map_data[xioff + yi];
                if (type >= SETUP_TARGET && type < SETUP_TARGET + 10)
                    vis = (Target_alive(xi, yi, &damage) == 0);
                else
                    vis = visible[type];
                if (vis) {
                    yp = rh - 1 - (int)(yi * ys + 0.5);
                    SDL3_Set_Draw_Color(visibleColor[type]);
                    SDL_RenderPoint(tng_renderer, (float)xp, (float)yp);
                }
            }
        }
    } else {
        xs = (double)(Setup->x - 1) / (256 - 1);
        ys = (double)(Setup->y - 1) / (rh - 1);
        for (xi = 0; xi < 256; xi++) {
            xm = (int)(xi * xs + 0.5);
            xmoff = xm * Setup->y;
            xp = xi;
            for (yi = 0; yi < rh; yi++) {
                ym = (int)(yi * ys + 0.5);
                type = Setup->map_data[xmoff + ym];
                vis = visible[type];
                if (type >= SETUP_TARGET && type < SETUP_TARGET + 10)
                    vis = (Target_alive(xm, ym, &damage) == 0);
                if (vis) {
                    yp = rh - 1 - yi;
                    SDL3_Set_Draw_Color(visibleColor[type]);
                    SDL_RenderPoint(tng_renderer, (float)xp, (float)yp);
                }
            }
        }
    }

    for (i = 0;; i++) {
        int dead_time;
        double targ_damage;
        if (Target_by_index(i, &xi, &yi, &dead_time, &targ_damage) == -1)
            break;
        if (dead_time)
            continue;
        Paint_radar_block(xi, yi, targetRadarColor);
    }
}

static void Render_static_radar_new(void)
{
    int rh = (int)RadarHeight;

    for (int i = 0; i < num_polygons; i++) {
        if (BIT(polygon_styles[polygons[i].style].flags, STYLE_INVISIBLE_RADAR))
            continue;
        int npts = polygons[i].num_points;
        if (npts < 3) continue;

        SDL_FPoint *pts = (SDL_FPoint *)malloc(sizeof(SDL_FPoint) * npts);
        if (!pts) continue;

        ipos_t min, max;
        Compute_radar_bounds(&min, &max, &polygons[i].bounds);

        Uint32 col = polygon_styles[polygons[i].style].color ?
                     (Uint32)polygon_styles[polygons[i].style].color :
                     (Uint32)polygon_styles[polygons[i].style].rgb;

        for (int xoff = min.x; xoff <= max.x; xoff++) {
            for (int yoff = min.y; yoff <= max.y; yoff++) {
                int x = xoff * Setup->width;
                int y = yoff * Setup->height;
                for (int j = 0; j < npts; j++) {
                    x += polygons[i].points[j].x;
                    y += polygons[i].points[j].y;
                    /* no integer division: snapping to whole radar pixels collapses
                     * the vertices of thin walls and breaks the triangulation */
                    pts[j].x = (float)x * 256.0f / (float)Setup->width;
                    pts[j].y = (float)rh - (float)y * (float)rh / (float)Setup->height;
                }
                if (fullColor && col) {
                    SDL3_Fill_Polygon_RGB(pts, npts, col);
                } else {
                    SDL3_Fill_Polygon(pts, npts, wallRadarColor);
                }
            }
        }
        free(pts);
    }
}

static void Render_static_radar_world(void)
{
    if (!static_radar_texture) {
        static_radar_texture = SDL_CreateTexture(tng_renderer,
                                                 SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET,
                                                 256, RADAR_HEIGHT);
    }
    if (!static_radar_texture) return;

    SDL_SetRenderTarget(tng_renderer, static_radar_texture);
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL_SetRenderDrawColor(tng_renderer, 0, 0, 0, 255);
    SDL_RenderClear(tng_renderer);

    if (oldServer) {
        Render_static_radar_old();
    } else {
        Render_static_radar_new();
    }

    radar_needs_redraw = false;
}

void Paint_radar(void)
{
    if (!Setup || Setup->width <= 0 || Setup->height <= 0 || !tng_renderer)
        return;

    int rh = (int)RadarHeight;
    if (rh <= 0 || rh > RADAR_HEIGHT)
        rh = RADAR_HEIGHT;

    /* 1. Ensure static world texture is up to date */
    if (!static_radar_texture || radar_needs_redraw) {
        Render_static_radar_world();
    }

    /* 2. Prepare dynamic frame texture */
    if (!frame_radar_texture) {
        frame_radar_texture = SDL_CreateTexture(tng_renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                256, RADAR_HEIGHT);
    }
    if (!frame_radar_texture) return;

    SDL_SetRenderTarget(tng_renderer, frame_radar_texture);
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL_SetRenderDrawColor(tng_renderer, 0, 0, 0, 255);
    SDL_RenderClear(tng_renderer);

    if (static_radar_texture) {
        SDL_RenderTexture(tng_renderer, static_radar_texture, NULL, NULL);
    }

    const double xf = 256.0 / (double)Setup->width;
    const double yf = (double)rh / (double)Setup->height;

    /* 3. Draw Checkpoints */
    if (BIT(Setup->mode, TIMING)) {
        int cx, cy;
        if (oldServer) {
            Check_pos_by_index(nextCheckPoint, &cx, &cy);
            cx = (int)(cx * BLOCK_SZ * xf + 0.5);
            cy = rh - (int)(cy * BLOCK_SZ * yf + 0.5) + DSIZE - 1;
        } else if (nextCheckPoint >= 0 && nextCheckPoint < num_checks) {
            irec_t b = checks[nextCheckPoint].bounds;
            cx = (int)(b.x * xf + 0.5);
            cy = rh - (int)(b.y * yf + 0.5) + DSIZE - 1;
        } else {
            cx = cy = 0;
        }
        if (cx <= 0) cx += 256;
        if (cy <= 0) cy += rh;

        SDL_FPoint d[5] = {
            { (float)cx,         (float)cy },
            { (float)(cx + DSIZE), (float)(cy - DSIZE) },
            { (float)cx,         (float)(cy - 2 * DSIZE) },
            { (float)(cx - DSIZE), (float)(cy - DSIZE) },
            { (float)cx,         (float)cy }
        };
        SDL3_Draw_Lines(d, 5, WHITE, false);
    }

    /* 4. Draw Self Heading Vector */
    if (selfVisible != 0 && loops % 16 < 13) {
        int x = (int)(selfPos.x * xf + 0.5);
        int y = rh - (int)(selfPos.y * yf + 0.5) - 1;
        if (x <= 0) x += 256;
        if (y <= 0) y += rh;

        int x_1 = (int)(x + 8 * tcos(heading));
        int y_1 = (int)(y - 8 * tsin(heading));
        SDL3_Draw_Line((float)x, (float)y, (float)x_1, (float)y_1, WHITE);
        if (BIT(Setup->mode, WRAP_PLAY)) {
            int xw = x_1 - (x_1 + 256) % 256;
            int yw = y_1 - (y_1 + rh) % rh;
            if (xw != 0)
                SDL3_Draw_Line((float)(x - xw), (float)y, (float)(x_1 - xw), (float)y_1, WHITE);
            if (yw != 0) {
                SDL3_Draw_Line((float)x, (float)(y - yw), (float)x_1, (float)(y_1 - yw), WHITE);
                if (xw != 0)
                    SDL3_Draw_Line((float)(x - xw), (float)(y - yw), (float)(x_1 - xw), (float)(y_1 - yw), WHITE);
            }
        }
    }

    /* 5. Draw Server Radar Objects (radar_ptr) */
    for (int i = 0; i < num_radar; i++) {
        int rs = radar_ptr[i].size;
        int s = (rs <= 0) ? 1 : rs;
        int col = WHITE;
        if (radar_ptr[i].type == RadarFriend) {
            col = (maxColors > 4) ? 4 : RED;
        }
        int x = radar_ptr[i].x - s / 2;
        int y = rh - radar_ptr[i].y - 1 - s / 2;
        if (x <= 0) x += 256;
        if (y <= 0) y += rh;

        SDL3_Fill_Rect((float)x, (float)y, (float)s, (float)s, col);
        if (BIT(Setup->mode, WRAP_PLAY)) {
            int xw = (x < 0) ? -256 : (x + s >= 256) ? 256 : 0;
            int yw = (y < 0) ? -rh : (y + s >= rh) ? rh : 0;
            if (xw != 0)
                SDL3_Fill_Rect((float)(x - xw), (float)y, (float)s, (float)s, col);
            if (yw != 0) {
                SDL3_Fill_Rect((float)x, (float)(y - yw), (float)s, (float)s, col);
                if (xw != 0)
                    SDL3_Fill_Rect((float)(x - xw), (float)(y - yw), (float)s, (float)s, col);
            }
        }
    }
    if (num_radar) {
        RELEASE(radar_ptr, num_radar, max_radar);
    }

    /* 6. Render to Window (Sliding or Static) */
    SDL_SetRenderTarget(tng_renderer, NULL);
    SDL_Rect radar_rect = { 0, 0, 256, rh };
    SDL_SetRenderClipRect(tng_renderer, &radar_rect);

    if (instruments.slidingRadar && BIT(Setup->mode, WRAP_PLAY)) {
        double xp = (double)(selfPos.x * 256) / (double)Setup->width;
        double yp = (double)(selfPos.y * rh) / (double)Setup->height;
        double xo = 128.0;
        double yo = (double)rh / 2.0;
        int x = (int)(xp - xo + 0.5);
        int y = (int)(yp - yo + 0.5);
        if (x <= 0) x += 256;
        if (y <= 0) y += rh;
        int w1 = x;
        int h1 = y;
        int h2 = rh - h1;
        int w2 = 256 - w1;

        SDL_FRect sr1 = { 0.0f, (float)h2, (float)w1, (float)h1 };
        SDL_FRect dr1 = { (float)w2, 0.0f, (float)w1, (float)h1 };
        SDL_RenderTexture(tng_renderer, frame_radar_texture, &sr1, &dr1);

        if (w2 > 0) {
            SDL_FRect sr2 = { (float)w1, (float)h2, (float)w2, (float)h1 };
            SDL_FRect dr2 = { 0.0f, 0.0f, (float)w2, (float)h1 };
            SDL_RenderTexture(tng_renderer, frame_radar_texture, &sr2, &dr2);
        }
        if (h2 > 0) {
            SDL_FRect sr3 = { 0.0f, 0.0f, (float)w1, (float)h2 };
            SDL_FRect dr3 = { (float)w2, (float)h1, (float)w1, (float)h2 };
            SDL_RenderTexture(tng_renderer, frame_radar_texture, &sr3, &dr3);
        }
        if (w2 > 0 && h2 > 0) {
            SDL_FRect sr4 = { (float)w1, 0.0f, (float)w2, (float)h2 };
            SDL_FRect dr4 = { 0.0f, (float)h1, (float)w2, (float)h2 };
            SDL_RenderTexture(tng_renderer, frame_radar_texture, &sr4, &dr4);
        }
    } else {
        SDL_FRect sr = { 0.0f, 0.0f, 256.0f, (float)rh };
        SDL_FRect dr = { 0.0f, 0.0f, 256.0f, (float)rh };
        SDL_RenderTexture(tng_renderer, frame_radar_texture, &sr, &dr);
    }

    SDL3_Draw_Rect(0.0f, 0.0f, 256.0f, (float)rh, borderColor);
}

static xp_option_t radar_opts[] = {
    COLOR_INDEX_OPTION("wallRadarColor", BLUE, &wallRadarColor, "Which color number to use for drawing walls on the radar.\n"),
    COLOR_INDEX_OPTION("decorRadarColor", 6, &decorRadarColor, "Which color number to use for drawing decorations on the radar.\n"),
    COLOR_INDEX_OPTION("targetRadarColor", 4, &targetRadarColor, "Which color number to use for drawing targets on the radar.\n"),
};

void Store_paintradar_options(void)
{
    STORE_OPTIONS(radar_opts);
}
