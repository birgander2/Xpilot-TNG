/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * World & Map Rendering implementing guimap.h (matching ng-x11)
 */

#include "sdl3_client.h"
#include "sdl3_bitmaps.h"

static int baseNameColor = 2;
static int backgroundPointColor = 2;
static int fuelColor = 3;
static int visibilityBorderColor = 2;

void Gui_paint_walls(int x, int y, int type)
{
    if (!texturedObjects) {
        if (type & BLUE_LEFT)
            Segment_add(wallColor, X(x), Y(y), X(x), Y(y + BLOCK_SZ));
        if (type & BLUE_DOWN)
            Segment_add(wallColor, X(x), Y(y), X(x + BLOCK_SZ), Y(y));
        if (type & BLUE_RIGHT)
            Segment_add(wallColor, X(x + BLOCK_SZ), Y(y), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
        if (type & BLUE_UP)
            Segment_add(wallColor, X(x), Y(y + BLOCK_SZ), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
        if ((type & BLUE_FUEL) == BLUE_FUEL)
            ;
        else if (type & BLUE_OPEN)
            Segment_add(wallColor, X(x), Y(y), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
        else if (type & BLUE_CLOSED)
            Segment_add(wallColor, X(x), Y(y + BLOCK_SZ), X(x + BLOCK_SZ), Y(y));
    } else {
        if (type & BLUE_LEFT)
            Bitmap_paint(0, BM_WALL_LEFT, PLAYFIELD_X + WINSCALE(X(x - 1)), WINSCALE(Y(y + BLOCK_SZ)), 0);
        if (type & BLUE_DOWN)
            Bitmap_paint(0, BM_WALL_BOTTOM, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ - 1)), 0);
        if (type & BLUE_RIGHT)
            Bitmap_paint(0, BM_WALL_RIGHT, PLAYFIELD_X + WINSCALE(X(x + 1)), WINSCALE(Y(y + BLOCK_SZ)), 0);
        if (type & BLUE_UP)
            Bitmap_paint(0, BM_WALL_TOP, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ + 1)), 0);
        if ((type & BLUE_FUEL) == BLUE_FUEL)
            ;
        else if (type & BLUE_OPEN)
            Bitmap_paint(0, BM_WALL_UR, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
        else if (type & BLUE_CLOSED)
            Bitmap_paint(0, BM_WALL_UL, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
    }
}

void Gui_paint_filled_slice(int bl, int tl, int tr, int br, int y)
{
    SDL_FPoint points[5];
    points[0].x = Screen_X(bl);
    points[0].y = Screen_Y(y);
    points[1].x = Screen_X(tl);
    points[1].y = Screen_Y(y + BLOCK_SZ);
    points[2].x = Screen_X(tr);
    points[2].y = Screen_Y(y + BLOCK_SZ);
    points[3].x = Screen_X(br);
    points[3].y = Screen_Y(y);
    points[4] = points[0];

    SDL3_Fill_Polygon(points, 4, wallColor);
}

void Gui_paint_cannon(int x, int y, int type)
{
    if (!texturedObjects) {
        SDL_FPoint points[4];
        switch (type) {
        case SETUP_CANNON_UP:
            points[0].x = Screen_X(x);
            points[0].y = Screen_Y(y);
            points[1].x = Screen_X(x + BLOCK_SZ);
            points[1].y = Screen_Y(y);
            points[2].x = Screen_X(x + BLOCK_SZ / 2);
            points[2].y = Screen_Y(y + BLOCK_SZ / 3);
            break;
        case SETUP_CANNON_DOWN:
            points[0].x = Screen_X(x);
            points[0].y = Screen_Y(y + BLOCK_SZ);
            points[1].x = Screen_X(x + BLOCK_SZ);
            points[1].y = Screen_Y(y + BLOCK_SZ);
            points[2].x = Screen_X(x + BLOCK_SZ / 2);
            points[2].y = Screen_Y(y + 2 * BLOCK_SZ / 3);
            break;
        case SETUP_CANNON_RIGHT:
            points[0].x = Screen_X(x);
            points[0].y = Screen_Y(y);
            points[1].x = Screen_X(x);
            points[1].y = Screen_Y(y + BLOCK_SZ);
            points[2].x = Screen_X(x + BLOCK_SZ / 3);
            points[2].y = Screen_Y(y + BLOCK_SZ / 2);
            break;
        case SETUP_CANNON_LEFT:
            points[0].x = Screen_X(x + BLOCK_SZ);
            points[0].y = Screen_Y(y);
            points[1].x = Screen_X(x + BLOCK_SZ);
            points[1].y = Screen_Y(y + BLOCK_SZ);
            points[2].x = Screen_X(x + 2 * BLOCK_SZ / 3);
            points[2].y = Screen_Y(y + BLOCK_SZ / 2);
            break;
        default:
            return;
        }
        points[3] = points[0];
        SDL3_Draw_Lines(points, 4, WHITE, false);
    } else {
        switch (type) {
        case SETUP_CANNON_UP:
            Bitmap_paint(0, BM_CANNON_DOWN, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        case SETUP_CANNON_DOWN:
            Bitmap_paint(0, BM_CANNON_UP, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ - 1)), 0);
            break;
        case SETUP_CANNON_LEFT:
            Bitmap_paint(0, BM_CANNON_RIGHT, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        case SETUP_CANNON_RIGHT:
            Bitmap_paint(0, BM_CANNON_LEFT, PLAYFIELD_X + WINSCALE(X(x - 1)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        default:
            return;
        }
    }
}

void Gui_paint_fuel(int x, int y, double fuel)
{
    if (fuelColor == BLACK)
        return;

    if (!texturedObjects) {
#define FUEL_BORDER 2
        int size = (int)((BLOCK_SZ - 2 * FUEL_BORDER) * fuel / MAX_STATION_FUEL);
        if (size < 0) size = 0;

        float sx = Screen_X(x + FUEL_BORDER);
        float sy = Screen_Y(y + FUEL_BORDER + size);
        float sw = (float)WINSCALE(BLOCK_SZ - 2 * FUEL_BORDER + 1);
        float sh = (float)WINSCALE(size + 1);

        SDL3_Fill_Rect(sx, sy, sw, sh, fuelColor);

        /* Draw letter 'F' in fuel cells */
        float fx = Screen_X(x + BLOCK_SZ / 2) - 4.0f;
        float fy = Screen_Y(y + BLOCK_SZ / 2) - 4.0f;
        SDL3_Draw_Text("F", fx, fy, WHITE);
    } else {
#define BITMAP_FUEL_BORDER 3
        int fuel_images = 16;
        int image = ABS((loopsSlow + x + x * y) % (fuel_images * 2));
        if (image >= fuel_images)
            image = (2 * fuel_images - 1) - image;

        int size = (int)((BLOCK_SZ - 2 * BITMAP_FUEL_BORDER) * fuel / MAX_STATION_FUEL);

        Bitmap_paint(0, BM_FUELCELL, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);

        if (size > 0) {
            irec_t area;
            area.x = 0;
            area.y = 0;
            area.w = BLOCK_SZ - 2 * BITMAP_FUEL_BORDER;
            area.h = size;
            SDL3_Bitmap_Paint_Area(BM_FUEL, image,
                                   Screen_X(x + BITMAP_FUEL_BORDER),
                                   Screen_Y(y + size + BITMAP_FUEL_BORDER),
                                   &area);
        }
    }
}

void Gui_paint_base(int x, int y, int id, int team, int type)
{
    int color = 0;
    const int BORDER = 4;
    other_t *other = Other_by_id(id);
    homebase_t *base = Homebase_by_id(id);
    bool do_basewarning = false;

    if (baseNameColor) {
        if (!(color = Life_color(other)))
            color = baseNameColor;
    } else {
        color = WHITE;
    }

    if (base != NULL) {
        if (loops < base->appeartime)
            do_basewarning = true;

        if (version < 0x4F12 && do_basewarning) {
            if (baseWarningType & 1) {
                int count = (int)(360 * (base->appeartime - loops) / (3 * (clientFPS > 0 ? clientFPS : 50)));
                LIMIT(count, 0, 360);
                if (count > 0 && (baseWarningType & 1))
                    Gui_paint_appearing(x + BLOCK_SZ / 2, y + BLOCK_SZ / 2, id, count);
            }
        }
    }

    if (do_basewarning && (baseWarningType & 2)) {
        if (loopsSlow & 1) {
            if (color != WHITE)
                color = WHITE;
            else
                color = BLACK;
        }
    }

    if (!texturedObjects) {
        switch (type) {
        case SETUP_BASE_UP:
            Segment_add(color, X(x), Y(y - 1), X(x + BLOCK_SZ), Y(y - 1));
            break;
        case SETUP_BASE_DOWN:
            Segment_add(color, X(x), Y(y + BLOCK_SZ + 1), X(x + BLOCK_SZ), Y(y + BLOCK_SZ + 1));
            break;
        case SETUP_BASE_LEFT:
            Segment_add(color, X(x + BLOCK_SZ + 1), Y(y + BLOCK_SZ), X(x + BLOCK_SZ + 1), Y(y));
            break;
        case SETUP_BASE_RIGHT:
            Segment_add(color, X(x - 1), Y(y + BLOCK_SZ), X(x - 1), Y(y));
            break;
        default:
            return;
        }
    } else {
        switch (type) {
        case SETUP_BASE_UP:
            Bitmap_paint(0, BM_BASE_DOWN, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        case SETUP_BASE_DOWN:
            Bitmap_paint(0, BM_BASE_UP, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ - 1)), 0);
            break;
        case SETUP_BASE_LEFT:
            Bitmap_paint(0, BM_BASE_RIGHT, PLAYFIELD_X + WINSCALE(X(x)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        case SETUP_BASE_RIGHT:
            Bitmap_paint(0, BM_BASE_LEFT, PLAYFIELD_X + WINSCALE(X(x - 1)), WINSCALE(Y(y + BLOCK_SZ)), 0);
            break;
        default:
            return;
        }
    }

    if (!baseNameColor)
        return;

    /* Build base label (team + nick_name + extra info) */
    char label[128] = "";
    char s[4] = "";
    char info[16] = "";

    if (BIT(Setup->mode, TEAM_PLAY)) {
        s[0] = (char)('0' + team);
        if (other) {
            s[1] = ' ';
            s[2] = '\0';
        } else {
            s[1] = '\0';
        }
    }

    if (other) {
        if (other->mychar == ' ' || other->mychar == 'R' || other->mychar == 'S') {
            if (BIT(Setup->mode, LIMITED_LIVES))
                snprintf(info, sizeof(info), " %d", other->life);
        } else {
            snprintf(info, sizeof(info), " %c", other->mychar);
        }
    }

    snprintf(label, sizeof(label), "%s%s%s", s, other ? other->nick_name : "", info);
    if (label[0] == '\0')
        return;

    float tw = SDL3_Text_Width(label);
    float px = Screen_X(x);
    float py = Screen_Y(y);
    float tx = px, ty = py;

    switch (type) {
    case SETUP_BASE_UP:
        tx = px + (float)WINSCALE(BLOCK_SZ / 2) - tw * 0.5f;
        ty = py + (float)BORDER + 2.0f;
        break;
    case SETUP_BASE_DOWN:
        tx = px + (float)WINSCALE(BLOCK_SZ / 2) - tw * 0.5f;
        ty = py - (float)WINSCALE(BLOCK_SZ) - (float)BORDER - 10.0f;
        break;
    case SETUP_BASE_LEFT:
        tx = px + (float)WINSCALE(BLOCK_SZ) + (float)BORDER;
        ty = py - (float)WINSCALE(BLOCK_SZ / 2) - 4.0f;
        break;
    case SETUP_BASE_RIGHT:
        tx = px - (float)BORDER - tw;
        ty = py - (float)WINSCALE(BLOCK_SZ / 2) - 4.0f;
        break;
    default:
        return;
    }

    SDL3_Draw_Text(label, tx, ty, color);
}

void Gui_paint_decor(int x, int y, int xi, int yi, int type, bool last, bool more_y)
{
    SDL_FPoint points[5];
    int mask;
    int fill_top_left = -1, fill_top_right = -1, fill_bottom_left = -1, fill_bottom_right = -1;
    static unsigned char decor[256];
    static int decorReady = 0;

    if (!decorReady) {
        memset(decor, 0, sizeof decor);
        decor[SETUP_DECOR_FILLED] = DECOR_UP | DECOR_LEFT | DECOR_DOWN | DECOR_RIGHT;
        decor[SETUP_DECOR_RU] = DECOR_UP | DECOR_RIGHT | DECOR_CLOSED;
        decor[SETUP_DECOR_RD] = DECOR_DOWN | DECOR_RIGHT | DECOR_OPEN | DECOR_BELOW;
        decor[SETUP_DECOR_LU] = DECOR_UP | DECOR_LEFT | DECOR_OPEN;
        decor[SETUP_DECOR_LD] = DECOR_LEFT | DECOR_DOWN | DECOR_CLOSED | DECOR_BELOW;
        decorReady = 1;
    }

    mask = decor[type];

    if (!(instruments.filledDecor || instruments.texturedDecor)) {
        if (mask & DECOR_LEFT) {
            if ((xi == 0)
                ? (!BIT(Setup->mode, WRAP_PLAY) ||
                   !(decor[Setup->map_data[(Setup->x - 1) * Setup->y + yi]] & DECOR_RIGHT))
                : !(decor[Setup->map_data[(xi - 1) * Setup->y + yi]] & DECOR_RIGHT)) {
                Segment_add(decorColor, X(x), Y(y), X(x), Y(y + BLOCK_SZ));
            }
        }
        if (mask & DECOR_DOWN) {
            if ((yi == 0)
                ? (!BIT(Setup->mode, WRAP_PLAY) ||
                   !(decor[Setup->map_data[xi * Setup->y + Setup->y - 1]] & DECOR_UP))
                : !(decor[Setup->map_data[xi * Setup->y + (yi - 1)]] & DECOR_UP)) {
                Segment_add(decorColor, X(x), Y(y), X(x + BLOCK_SZ), Y(y));
            }
        }
        if (mask & DECOR_RIGHT) {
            if (!instruments.outlineDecor
                || ((xi == Setup->x - 1)
                    ? (!BIT(Setup->mode, WRAP_PLAY)
                       || !(decor[Setup->map_data[yi]] & DECOR_LEFT))
                    : !(decor[Setup->map_data[(xi + 1) * Setup->y + yi]] & DECOR_LEFT))) {
                Segment_add(decorColor, X(x + BLOCK_SZ), Y(y), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
            }
        }
        if (mask & DECOR_UP) {
            if (!instruments.outlineDecor
                || ((yi == Setup->y - 1)
                    ? (!BIT(Setup->mode, WRAP_PLAY)
                       || !(decor[Setup->map_data[xi * Setup->y]] & DECOR_DOWN))
                    : !(decor[Setup->map_data[xi * Setup->y + (yi + 1)]] & DECOR_DOWN))) {
                Segment_add(decorColor, X(x), Y(y + BLOCK_SZ), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
            }
        }
        if (mask & DECOR_OPEN) {
            Segment_add(decorColor, X(x), Y(y), X(x + BLOCK_SZ), Y(y + BLOCK_SZ));
        } else if (mask & DECOR_CLOSED) {
            Segment_add(decorColor, X(x), Y(y + BLOCK_SZ), X(x + BLOCK_SZ), Y(y));
        }
    } else {
        if (mask & DECOR_OPEN) {
            if (mask & DECOR_BELOW) {
                fill_top_left = x + BLOCK_SZ;
                fill_bottom_left = x;
                fill_top_right = fill_bottom_right = -1;
            } else {
                fill_top_right = x + BLOCK_SZ;
                fill_bottom_right = x;
            }
        } else if (mask & DECOR_CLOSED) {
            if (!(mask & DECOR_BELOW)) {
                fill_top_left = x;
                fill_bottom_left = x + BLOCK_SZ;
                fill_top_right = fill_bottom_right = -1;
            } else {
                fill_top_right = x;
                fill_bottom_right = x + BLOCK_SZ;
            }
        }
        if (mask & DECOR_RIGHT)
            fill_top_right = fill_bottom_right = x + BLOCK_SZ;
        if (fill_top_left == -1)
            fill_top_left = fill_bottom_left = x;
        if (fill_top_right == -1 && (last || more_y)) {
            fill_top_right = x + BLOCK_SZ;
            fill_bottom_right = x + BLOCK_SZ;
        }
        if (fill_top_right != -1) {
            points[0].x = Screen_X(fill_bottom_left);
            points[0].y = Screen_Y(y);
            points[1].x = Screen_X(fill_top_left);
            points[1].y = Screen_Y(y + BLOCK_SZ);
            points[2].x = Screen_X(fill_top_right);
            points[2].y = Screen_Y(y + BLOCK_SZ);
            points[3].x = Screen_X(fill_bottom_right);
            points[3].y = Screen_Y(y);
            points[4] = points[0];
            SDL3_Fill_Polygon(points, 4, decorColor);
        }
    }
}

void Gui_paint_decor_dot(int x, int y, int size)
{
    if (!backgroundPointColor)
        return;
    Rectangle_add(backgroundPointColor,
                  X(x + BLOCK_SZ / 2) - (backgroundPointSize >> 1),
                  Y(y + BLOCK_SZ / 2) - (backgroundPointSize >> 1),
                  size, size);
}

void Gui_paint_border(int x, int y, int xi, int yi)
{
    Segment_add(wallColor, X(x), Y(y), X(xi), Y(yi));
}

static void Gui_paint_rectangle(int x, int y, int xi, int yi, int color)
{
    Segment_add(color, X(x), Y(y), X(x), Y(yi));
    Segment_add(color, X(xi), Y(y), X(xi), Y(yi));
    Segment_add(color, X(x), Y(y), X(xi), Y(y));
    Segment_add(color, X(x), Y(yi), X(xi), Y(yi));
}

void Gui_paint_visible_border(int x, int y, int xi, int yi)
{
    if (visibilityBorderColor)
        Gui_paint_rectangle(x, y, xi, yi, visibilityBorderColor);
}

void Gui_paint_hudradar_limit(int x, int y, int xi, int yi)
{
    Gui_paint_rectangle(x, y, xi, yi, BLUE);
}

void Gui_paint_setup_check(int x, int y, bool isNext)
{
    if (!texturedObjects) {
        SDL_FPoint points[5] = {
            { Screen_X(x + (BLOCK_SZ / 2)), Screen_Y(y) },
            { Screen_X(x),                  Screen_Y(y + BLOCK_SZ / 2) },
            { Screen_X(x + BLOCK_SZ / 2),   Screen_Y(y + BLOCK_SZ) },
            { Screen_X(x + BLOCK_SZ),       Screen_Y(y + (BLOCK_SZ / 2)) },
            { Screen_X(x + (BLOCK_SZ / 2)), Screen_Y(y) }
        };
        if (isNext)
            SDL3_Fill_Polygon(points, 4, BLUE);
        else
            SDL3_Draw_Lines(points, 5, BLUE, false);
    } else {
        Bitmap_paint(0, BM_CHECKPOINT,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)),
                     isNext ? 1 : 0);
    }
}

void Gui_paint_setup_acwise_grav(int x, int y)
{
    Arc_add(RED, X(x + 5), Y(y + BLOCK_SZ - 5),
            BLOCK_SZ - 10, BLOCK_SZ - 10, 64 * 150, 64 * 300);
    Segment_add(RED,
                X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ - 5),
                X(x + BLOCK_SZ / 2 + 4), Y(y + BLOCK_SZ - 1));
    Segment_add(RED,
                X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ - 5),
                X(x + BLOCK_SZ / 2 + 4), Y(y + BLOCK_SZ - 9));
}

void Gui_paint_setup_cwise_grav(int x, int y)
{
    Arc_add(RED, X(x + 5), Y(y + BLOCK_SZ - 5),
            BLOCK_SZ - 10, BLOCK_SZ - 10, 64 * 90, 64 * 300);
    Segment_add(RED,
                X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ - 5),
                X(x + BLOCK_SZ / 2 - 4), Y(y + BLOCK_SZ - 1));
    Segment_add(RED,
                X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ - 5),
                X(x + BLOCK_SZ / 2 - 4), Y(y + BLOCK_SZ - 9));
}

void Gui_paint_setup_pos_grav(int x, int y)
{
    static const int INSIDE_BL = BLOCK_SZ - 2;
    if (!texturedObjects) {
        Arc_add(RED, X(x + 1), Y(y + BLOCK_SZ - 1),
                INSIDE_BL, INSIDE_BL, 0, 64 * 360);
        Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y + 5),
                    X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ - 5));
        Segment_add(RED, X(x + 5), Y(y + BLOCK_SZ / 2),
                    X(x + BLOCK_SZ - 5), Y(y + BLOCK_SZ / 2));
    } else {
        Bitmap_paint(0, BM_PLUSGRAVITY,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)), 0);
    }
}

void Gui_paint_setup_neg_grav(int x, int y)
{
    static const int INSIDE_BL = BLOCK_SZ - 2;
    if (!texturedObjects) {
        Arc_add(RED, X(x + 1), Y(y + BLOCK_SZ - 1),
                INSIDE_BL, INSIDE_BL, 0, 64 * 360);
        Segment_add(RED, X(x + 5), Y(y + BLOCK_SZ / 2),
                    X(x + BLOCK_SZ - 5), Y(y + BLOCK_SZ / 2));
    } else {
        Bitmap_paint(0, BM_MINUSGRAVITY,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)), 0);
    }
}

void Gui_paint_setup_up_grav(int x, int y)
{
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y), X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ));
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ), X(x + BLOCK_SZ / 2 - 10), Y(y + BLOCK_SZ - 10));
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ), X(x + BLOCK_SZ / 2 + 10), Y(y + BLOCK_SZ - 10));
}

void Gui_paint_setup_down_grav(int x, int y)
{
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y), X(x + BLOCK_SZ / 2), Y(y + BLOCK_SZ));
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y), X(x + BLOCK_SZ / 2 - 10), Y(y + 10));
    Segment_add(RED, X(x + BLOCK_SZ / 2), Y(y), X(x + BLOCK_SZ / 2 + 10), Y(y + 10));
}

void Gui_paint_setup_right_grav(int x, int y)
{
    Segment_add(RED, X(x), Y(y + BLOCK_SZ / 2), X(x + BLOCK_SZ), Y(y + BLOCK_SZ / 2));
    Segment_add(RED, X(x + BLOCK_SZ), Y(y + BLOCK_SZ / 2), X(x + BLOCK_SZ - 10), Y(y + BLOCK_SZ / 2 + 10));
    Segment_add(RED, X(x + BLOCK_SZ), Y(y + BLOCK_SZ / 2), X(x + BLOCK_SZ - 10), Y(y + BLOCK_SZ / 2 - 10));
}

void Gui_paint_setup_left_grav(int x, int y)
{
    Segment_add(RED, X(x), Y(y + BLOCK_SZ / 2), X(x + BLOCK_SZ), Y(y + BLOCK_SZ / 2));
    Segment_add(RED, X(x), Y(y + BLOCK_SZ / 2), X(x + 10), Y(y + BLOCK_SZ / 2 + 10));
    Segment_add(RED, X(x), Y(y + BLOCK_SZ / 2), X(x + 10), Y(y + BLOCK_SZ / 2 - 10));
}

void Gui_paint_setup_worm(int x, int y)
{
    int wormDrawCount = loopsSlow & 7;
    if (!texturedObjects) {
        static const int INSIDE_BL = BLOCK_SZ - 2;
        static int wormOffset[8][3] = {
            { 10, 10, 10 },
            { 5, 10, 10 },
            { 0, 10, 10 },
            { 0, 5, 10 },
            { 0, 0, 10 },
            { 5, 0, 10 },
            { 10, 0, 10 },
            { 10, 5, 10 }
        };
#define _O wormOffset[wormDrawCount]
        Arc_add(RED, X(x) + 0, Y(y + BLOCK_SZ) + 0,
                INSIDE_BL - 0, INSIDE_BL - 0, 0, 64 * 360);
        Arc_add(RED, X(x) + _O[0], Y(y + BLOCK_SZ) + _O[1],
                INSIDE_BL - _O[2], INSIDE_BL - _O[2], 0, 64 * 360);
        Arc_add(RED, X(x) + _O[0] * 2, Y(y + BLOCK_SZ) + _O[1] * 2,
                INSIDE_BL - _O[2] * 2, INSIDE_BL - _O[2] * 2, 0, 64 * 360);
#undef _O
    } else {
        Bitmap_paint(0, BM_WORMHOLE,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)), wormDrawCount);
    }
}

void Gui_paint_setup_item_concentrator(int x, int y)
{
    static struct concentrator_triangle {
        int radius;
        int displ;
        int dir_off;
        int rot_speed;
        int rot_dir;
    } tris[] = {
        {  7, 3, 0, 3, 0 },
        { 11, 5, 3, 3, 0 },
        { 14, 8, 5, 3, 0 },
    };
    static unsigned rot_dir;
    static long concentratorloop;
    unsigned rdir, tdir;
    int cx, cy;
    size_t i;
    SDL_FPoint pts[4];

    if (!texturedObjects) {
        if (concentratorloop != loops) {
            concentratorloop = loops;
            rot_dir += 5;
            for (i = 0; i < NELEM(tris); i++)
                tris[i].rot_dir += tris[i].rot_speed;
        }
        for (i = 0; i < NELEM(tris); i++) {
            rdir = MOD2(rot_dir + tris[i].dir_off, RES);
            cx = (int)(X(x + BLOCK_SZ / 2) + tris[i].displ * tcos(rdir));
            cy = (int)(Y(y + BLOCK_SZ / 2) + tris[i].displ * tsin(rdir));
            tdir = MOD2(tris[i].rot_dir, RES);
            pts[0].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(tris[i].radius * tcos(tdir))));
            pts[0].y = (float)(WINSCALE(cy + (int)(tris[i].radius * tsin(tdir))));
            pts[1].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(tris[i].radius * tcos(MOD2(tdir + RES / 3, RES)))));
            pts[1].y = (float)(WINSCALE(cy + (int)(tris[i].radius * tsin(MOD2(tdir + RES / 3, RES)))));
            pts[2].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(tris[i].radius * tcos(MOD2(tdir + 2 * RES / 3, RES)))));
            pts[2].y = (float)(WINSCALE(cy + (int)(tris[i].radius * tsin(MOD2(tdir + 2 * RES / 3, RES)))));
            pts[3] = pts[0];
            SDL3_Draw_Lines(pts, 4, RED, false);
        }
    } else {
        Bitmap_paint(0, BM_CONCENTRATOR,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)),
                     (loopsSlow + (x + x * y)) % 32);
    }
}

void Gui_paint_setup_asteroid_concentrator(int x, int y)
{
    static struct concentrator_square {
        int size;
        int displ;
        int dir_off;
        int rot_speed;
        int rot_dir;
    } sqrs[] = {
        {  8, 3, 0, 3, 0 },
        { 12, 5, 3, 3, 0 },
        { 16, 8, 5, 3, 0 },
    };
    static unsigned rot_dir;
    static long concentratorloop;
    unsigned rdir, tdir;
    int cx, cy;
    size_t i;
    SDL_FPoint pts[5];

    if (!texturedObjects) {
        if (concentratorloop != loops) {
            concentratorloop = loops;
            rot_dir += 5;
            for (i = 0; i < NELEM(sqrs); i++)
                sqrs[i].rot_dir += sqrs[i].rot_speed;
        }
        for (i = 0; i < NELEM(sqrs); i++) {
            rdir = MOD2(rot_dir + sqrs[i].dir_off, RES);
            cx = (int)(X(x + BLOCK_SZ / 2) + sqrs[i].displ * tcos(rdir));
            cy = (int)(Y(y + BLOCK_SZ / 2) + sqrs[i].displ * tsin(rdir));
            tdir = MOD2(sqrs[i].rot_dir, RES);
            pts[0].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(sqrs[i].size * tcos(tdir))));
            pts[0].y = (float)(WINSCALE(cy + (int)(sqrs[i].size * tsin(tdir))));
            pts[1].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(sqrs[i].size * tcos(MOD2(tdir + RES / 4, RES)))));
            pts[1].y = (float)(WINSCALE(cy + (int)(sqrs[i].size * tsin(MOD2(tdir + RES / 4, RES)))));
            pts[2].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(sqrs[i].size * tcos(MOD2(tdir + 2 * RES / 4, RES)))));
            pts[2].y = (float)(WINSCALE(cy + (int)(sqrs[i].size * tsin(MOD2(tdir + 2 * RES / 4, RES)))));
            pts[3].x = (float)(PLAYFIELD_X + WINSCALE(cx + (int)(sqrs[i].size * tcos(MOD2(tdir + 3 * RES / 4, RES)))));
            pts[3].y = (float)(WINSCALE(cy + (int)(sqrs[i].size * tsin(MOD2(tdir + 3 * RES / 4, RES)))));
            pts[4] = pts[0];
            SDL3_Draw_Lines(pts, 5, RED, false);
        }
    } else {
        Bitmap_paint(0, BM_ASTEROIDCONC,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)),
                     (loopsSlow + (x + x * y)) % 32);
    }
}

void Gui_paint_setup_target(int x, int y, int team, double damage, bool own)
{
    int size, a1, a2, b1, b2, color;
    char s[2];

    color = own ? BLUE : RED;
    if (BIT(Setup->mode, TEAM_PLAY)) {
        int team_color = Team_color(team);
        if (team_color)
            color = team_color;
    }

    a1 = X(x);
    b1 = Y(y + BLOCK_SZ);
    a2 = a1 + BLOCK_SZ;
    b2 = b1 + BLOCK_SZ;
    Segment_add(color, a1, b1, a1, b2);
    Segment_add(color, a2, b1, a2, b2);
    Segment_add(color, a1, b1, a2, b1);
    Segment_add(color, a1, b2, a2, b2);

    SDL3_Draw_Rect(Screen_X(x + (BLOCK_SZ + 2) / 4),
                   Screen_Y(y + 3 * BLOCK_SZ / 4),
                   (float)WINSCALE(BLOCK_SZ / 2),
                   (float)WINSCALE(BLOCK_SZ / 2),
                   color);

    if (BIT(Setup->mode, TEAM_PLAY)) {
        s[0] = (char)('0' + team);
        s[1] = '\0';
        float tw = SDL3_Text_Width(s);
        SDL3_Draw_Text(s,
                       Screen_X(x + BLOCK_SZ / 2) - tw * 0.5f,
                       Screen_Y(y + BLOCK_SZ / 2) - 4.0f,
                       color);
    }

    if (damage != TARGET_DAMAGE) {
        size = (int)((damage * BLOCK_SZ) / (TARGET_DAMAGE * 2));
        a1 = x + size;
        a2 = y + size;
        b1 = x + (BLOCK_SZ - size);
        b2 = y + (BLOCK_SZ - size);

        Segment_add(RED, X(a1), Y(a2), X(b1), Y(b2));
        Segment_add(RED, X(a1), Y(b2), X(b1), Y(a2));
    }
}

void Gui_paint_setup_treasure(int x, int y, int team, bool own)
{
    int color = own ? BLUE : RED;

    if (BIT(Setup->mode, TEAM_PLAY)) {
        int team_color = Team_color(team);
        if (team_color)
            color = team_color;
    }

    if (!texturedObjects) {
        Segment_add(color, X(x), Y(y), X(x), Y(y + BLOCK_SZ / 2));
        Segment_add(color, X(x + BLOCK_SZ), Y(y), X(x + BLOCK_SZ), Y(y + BLOCK_SZ / 2));
        Segment_add(color, X(x), Y(y), X(x + BLOCK_SZ), Y(y));
        Arc_add(color, X(x), Y(y + BLOCK_SZ), BLOCK_SZ, BLOCK_SZ, 0, 64 * 180);

        if (BIT(Setup->mode, TEAM_PLAY)) {
            char s[2] = { (char)('0' + team), '\0' };
            float tw = SDL3_Text_Width(s);
            SDL3_Draw_Text(s,
                           Screen_X(x + BLOCK_SZ / 2) - tw * 0.5f,
                           Screen_Y(y + 2 * BALL_RADIUS) - 4.0f,
                           color);
        }
    } else {
        int type = own ? BM_HOLDER_FRIEND : BM_HOLDER_ENEMY;
        Bitmap_paint(0, type,
                     PLAYFIELD_X + WINSCALE(X(x)),
                     WINSCALE(Y(y + BLOCK_SZ)), 0);

        if (BIT(Setup->mode, TEAM_PLAY)) {
            char s[2] = { (char)('0' + team), '\0' };
            float tw = SDL3_Text_Width(s);
            SDL3_Draw_Text(s,
                           Screen_X(x + BLOCK_SZ / 2) - tw * 0.5f,
                           Screen_Y(y + BALL_RADIUS + 5) - 4.0f,
                           color);
        }
    }
}

void Gui_paint_polygon(int i, int xoff, int yoff)
{
    if (i < 0 || i >= num_polygons) return;

    xp_polygon_t *poly = &polygons[i];
    if (poly->num_points < 3) return;

    polygon_style_t style = polygon_styles[poly->style];
    if (BIT(style.flags, STYLE_INVISIBLE)) return;

    SDL_FPoint *pts = (SDL_FPoint *)malloc(sizeof(SDL_FPoint) * (poly->num_points + 1));
    if (!pts) return;

    int x = xoff * Setup->width;
    int y = yoff * Setup->height;

    for (int p = 0; p < poly->num_points; p++) {
        x += poly->points[p].x;
        y += poly->points[p].y;
        pts[p].x = Screen_X(x);
        pts[p].y = Screen_Y(y);
    }
    pts[poly->num_points] = pts[0];

    bool textured = instruments.texturedWalls && fullColor;
    bool filled = instruments.filledWorld;
    bool did_fill = false;

    if ((filled || textured) && BIT(style.flags, STYLE_TEXTURED | STYLE_FILLED)) {
        if (textured && BIT(style.flags, STYLE_TEXTURED)) {
            int w = 0, h = 0;
            SDL_Texture *tex = Bitmap_get_texture(NUM_BITMAPS + style.texture, 0, &w, &h);
            if (tex != NULL && w > 0 && h > 0) {
                float ox = Screen_X(poly->bounds.x + xoff * Setup->width);
                float oy = Screen_Y(poly->bounds.y + poly->bounds.h + yoff * Setup->height);
                SDL3_Render_Textured_Polygon(pts, poly->num_points, tex, ox, oy, w, h);
                did_fill = true;
            }
        }
        if (!did_fill) {
            if (fullColor) {
                Uint32 rgb = style.color ? (Uint32)style.color : (Uint32)style.rgb;
                if (!rgb) rgb = 0x4E7CFF;
                SDL3_Fill_Polygon_RGB(pts, poly->num_points, rgb);
            } else {
                SDL3_Fill_Polygon(pts, poly->num_points, wallColor);
            }
            did_fill = true;
        }
    }

    /* Edge Rendering matching guimap.c */
    int sindex = style.def_edge_style;
    if (poly->edge_styles == NULL) {
        int width = (sindex >= 0 && sindex < num_edge_styles) ? edge_styles[sindex].width : 0;
        if (width != -1 || !did_fill) {
            if (fullColor) {
                Uint32 ecol = (sindex >= 0 && sindex < num_edge_styles && edge_styles[sindex].color) ?
                              (Uint32)edge_styles[sindex].color :
                              (sindex >= 0 && sindex < num_edge_styles && edge_styles[sindex].rgb) ?
                              (Uint32)edge_styles[sindex].rgb : 0x4E7CFF;
                SDL3_Draw_Lines_RGB(pts, poly->num_points + 1, ecol, false);
            } else {
                SDL3_Draw_Lines(pts, poly->num_points + 1, wallColor, false);
            }
        }
    } else {
        /* Style 0 means internal edges which are never shown */
        for (int j = 0; j < poly->num_points; j++) {
            int es = poly->edge_styles[j];
            if (es > 0 && es < num_edge_styles) {
                if (edge_styles[es].width != -1 || !did_fill) {
                    int jnext = j + 1;
                    if (fullColor) {
                        Uint32 ecol = edge_styles[es].color ? (Uint32)edge_styles[es].color : (Uint32)edge_styles[es].rgb;
                        if (!ecol) ecol = 0x4E7CFF;
                        SDL3_Draw_Line_RGB(pts[j].x, pts[j].y, pts[jnext].x, pts[jnext].y, ecol);
                    } else {
                        SDL3_Draw_Line(pts[j].x, pts[j].y, pts[jnext].x, pts[jnext].y, wallColor);
                    }
                }
            }
        }
    }

    free(pts);
}

xp_option_t guimap_options[] = {
    COLOR_INDEX_OPTION("baseNameColor", 2, &baseNameColor, "Which color number to use for drawing names of bases.\n"),
    COLOR_INDEX_OPTION("backgroundPointColor", 2, &backgroundPointColor, "Which color number to use for drawing background points.\n"),
    COLOR_INDEX_OPTION("fuelColor", 3, &fuelColor, "Which color number to use for drawing fuel stations.\n"),
    COLOR_INDEX_OPTION("visibilityBorderColor", 2, &visibilityBorderColor, "Which color number to use for drawing the visibility border.\n"),
};

void Store_guimap_options(void)
{
    STORE_OPTIONS(guimap_options);
}
