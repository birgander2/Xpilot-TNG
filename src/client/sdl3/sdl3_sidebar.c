/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Authentic Left Sidebar (Radar, Buttons, MENU Pulldown, CONFIG/COLORS, ABOUT & Viewer Windows)
 * Matching xpilot-ng-x11 1:1
 */

#include "sdl3_client.h"
#include "sdl3_bitmaps.h"
#include <ctype.h>
#include <time.h>

static int clockColor = 1;
static int scoreColor = 1;
static int scoreSelfColor = 3;
static int scoreInactiveColor = 12;
static int scoreInactiveSelfColor = 12;
static int scoreOwnTeamColor = 4;
static int scoreEnemyTeamColor = 11;

#define BTN_ROW_Y       RADAR_HEIGHT
#define BTN_ROW_H       21
#define SCORE_Y_OFFSET  (BTN_ROW_Y + BTN_ROW_H + 1)
#define SCORE_BORDER    6
#define LINE_HEIGHT     14
#define FONT_HEIGHT     12

static void Shadow_Draw_Text(const char *str, float x, float y, int color_idx)
{
    SDL3_Draw_Text(str, x + 1.0f, y + 1.0f, BLACK);
    SDL3_Draw_Text(str, x, y, color_idx);
}

static void Shadow_Draw_Text_Len(const char *str, int len, float x, float y, int color_idx)
{
    SDL3_Draw_Text_Len(str, len, x + 1.0f, y + 1.0f, BLACK);
    SDL3_Draw_Text_Len(str, len, x, y, color_idx);
}

/* Draw authentic x11 button (Widget_draw_button) */
static void Draw_Widget_Button(float x, float y, float w, float h, bool inverse, bool has_border, const char *label)
{
    int bg = inverse ? BLACK : buttonColor;
    SDL3_Fill_Rect(x, y, w, h, bg);
    if (has_border) {
        SDL3_Draw_Rect(x, y, w, h, borderColor);
    }
    if (label && label[0]) {
        float tw = SDL3_Text_Width(label);
        float tx = x + (w - tw) * 0.5f;
        float ty = y + (h - 10.0f) * 0.5f;
        Shadow_Draw_Text(label, tx, ty, WHITE);
    }
}

/* Draw authentic x11 left/right arrow button (Widget_draw_arrow) */
static void Draw_Widget_Arrow(float x, float y, float w, float h, bool right_arrow, bool active)
{
    int fg = active ? BLACK : buttonColor;
    int bg = active ? buttonColor : BLACK;

    SDL3_Fill_Rect(x, y, w, h, bg);

    float left = w * 0.25f;
    float right = w - left;
    float top = h * 0.25f;
    float bottom = h - top;
    if (right_arrow) {
        float tmp = left;
        left = right;
        right = tmp;
    }

    SDL_FPoint pts[3] = {
        { x + left,  y + h * 0.5f },
        { x + right, y + top },
        { x + right, y + bottom }
    };
    SDL3_Fill_Polygon(pts, 3, fg);
}

/* =========================================================================
 * Item Descriptions for ABOUT Window (matching xinit.c:95-224)
 * ========================================================================= */
static const char *const item_descriptions[NUM_ITEMS] = {
    "Extra energy/fuel",
    "Extra front cannons",
    "Extra rear cannon",
    "Afterburner; makes your engines more powerful",
    "Cloaking device; makes you almost invisible, both on radar and on screen",
    "Sensor; enables you to see cloaked opponents more easily",
    "Transporter; enables you to steal equipment from other players",
    "Tank; makes refueling quicker, increases maximum fuel capacity and can be jettisoned to confuse enemies",
    "Mine; can be dropped as a bomb or as a stationary mine",
    "Rocket; can be utilized as smart missile, heatseeking missile, nuclear missile or just a plain unguided missile (torpedo)",
    "ECM (Electronic Counter Measures); can be used to disturb electronic equipment, for instance can it be used to confuse smart missiles and reprogram robots to seek certain players",
    "Laser; limited range laser beam, costs a lot of fuel, having more laser items increases the range of the laser, they can be irrepairably damaged by ECMs",
    "Emergency Thrust; gives emergency thrust capabilities for a limited period",
    "Tractor Beam; gives mutual attractive force to currently locked on ship, this means the heavier your ship, the less likely you will move when being tractored or using a tractor",
    "Autopilot; when on, the ship will turn and thrust against the direction of travel",
    "EmergencyShield; gives emergency shield capabilities for a limited period",
    "Deflector; pushes hostile objects away from your ship",
    "Hyperjump; enables you to teleport to a random map location",
    "Phasing Device; lets you fly through anything for a limited period",
    "Mirror; reflects laser beams",
    "Armor; absorbs shots in the absence of shields"
};

static const char *Item_get_text(int i)
{
    if (i < 0 || i >= NUM_ITEMS)
        return "";
    return item_descriptions[i];
}

/* =========================================================================
 * Top Button Bar & Pulldown Menu State
 * ========================================================================= */
#define NUM_MENU_ENTRIES 6
static const char *const menu_labels[NUM_MENU_ENTRIES] = {
    "KEYS",
    "CONFIG",
    "COLORS",
    "SCORE",
    "PLAYER",
    "MOTD"
};

static int  top_btn_pressed = -1;  /* 0=QUIT, 1=ABOUT, 2=MENU */
static int  top_btn_hover   = -1;
static bool menu_open       = false;
static bool menu_click_hold = false;
static int  menu_hover_item = -1;

/* =========================================================================
 * ABOUT Window State (matching x11/about.c)
 * ========================================================================= */
#define ABOUT_WINDOW_WIDTH  600
#define ABOUT_WINDOW_HEIGHT 700
#define ABOUT_BORDER        10
#define ABOUT_BTN_BORDER    4
#define NUM_ABOUT_PAGES     5

static bool about_open        = false;
static int  about_page        = 0;
static int  itemsplit         = -1;
static int  about_btn_pressed = -1; /* 0=CLOSE, 1=PREV, 2=NEXT */
static int  about_btn_hover   = -1;

/* Word-wrapped shadow text inside a window [wx .. wx + ww] */
static int Draw_About_Shadow_Text(int wx, int ww, int x_border, int y_start, const char *str)
{
    if (!str || !*str) return y_start;

    int x = x_border;
    int y = y_start;
    int line_step = 14;
    int space_w = (int)SDL3_Text_Width_Len(1);

    while (*str != '\0') {
        char word[256];
        int i = 0;
        while (*str && !isspace((unsigned char)*str) && i < (int)sizeof(word) - 1) {
            word[i++] = *str++;
        }
        word[i] = '\0';

        int word_len = (int)SDL3_Text_Width_Len(i);
        if (x + word_len > ww - ABOUT_BORDER && x > x_border) {
            x = x_border;
            y += line_step;
        }

        if (i > 0) {
            Shadow_Draw_Text(word, (float)(wx + x), (float)y, WHITE);
            x += word_len;
        }

        for (; *str && isspace((unsigned char)*str); str++) {
            if (*str == '\n') {
                x = x_border;
                y += line_step;
            } else {
                x += space_w;
            }
        }
    }

    return y + line_step;
}

static void Draw_About_Item(int type, int cx, int cy)
{
    const int SIZE = ITEM_TRIANGLE_SIZE;
    SDL_FPoint pts[4] = {
        { (float)(cx - SIZE), (float)(cy - SIZE) },
        { (float)cx,          (float)(cy + SIZE) },
        { (float)(cx + SIZE), (float)(cy - SIZE) },
        { (float)(cx - SIZE), (float)(cy - SIZE) }
    };
    SDL3_Draw_Lines(pts, 4, BLUE, false);

    int ix = cx - ITEM_SIZE / 2;
    int iy = cy - SIZE + 2;
    if (texturedObjects) {
        Bitmap_paint(0, BM_ALL_ITEMS, ix, iy, type);
    } else {
        SDL3_Draw_Item_Icon(type, (float)ix, (float)iy, RED);
    }
}

static void Get_About_Rect(int *ax, int *ay, int *aw, int *ah)
{
    *aw = ABOUT_WINDOW_WIDTH;
    *ah = ABOUT_WINDOW_HEIGHT;
    if ((int)top_width < *aw + 20) *aw = (int)top_width - 20;
    if ((int)top_height < *ah + 20) *ah = (int)top_height - 20;
    *ax = ((int)top_width - *aw) / 2;
    *ay = ((int)top_height - *ah) / 2;
    if (*ax < 0) *ax = 0;
    if (*ay < 0) *ay = 0;
}

static void Get_About_Button_Rects(int ax, int ay, int aw, int ah,
                                   SDL_FRect *close_r, SDL_FRect *prev_r, SDL_FRect *next_r)
{
    float bh = (float)(2 * ABOUT_BTN_BORDER + 12);
    float by = (float)(ay + ah - ABOUT_BORDER - (int)bh - 4);

    float close_w = 2.0f * ABOUT_BTN_BORDER + SDL3_Text_Width("CLOSE");
    float prev_w  = 2.0f * ABOUT_BTN_BORDER + SDL3_Text_Width("PREV");
    float next_w  = 2.0f * ABOUT_BTN_BORDER + SDL3_Text_Width("NEXT");

    if (close_r) *close_r = (SDL_FRect){ (float)(ax + ABOUT_BORDER), by, close_w, bh };
    if (prev_r)  *prev_r  = (SDL_FRect){ (float)ax + (aw - prev_w) * 0.5f, by, prev_w, bh };
    if (next_r)  *next_r  = (SDL_FRect){ (float)(ax + aw - ABOUT_BORDER) - next_w, by, next_w, bh };
}

static void Render_About_Window(void)
{
    if (!about_open || !tng_renderer) return;

    int ax, ay, aw, ah;
    Get_About_Rect(&ax, &ay, &aw, &ah);

    SDL_SetRenderClipRect(tng_renderer, NULL);
    /* Outer 2px border + windowColor background */
    SDL3_Fill_Rect((float)(ax - 2), (float)(ay - 2), (float)(aw + 4), (float)(ah + 4), borderColor);
    SDL3_Fill_Rect((float)ax, (float)ay, (float)aw, (float)ah, windowColor);

    SDL_Rect clip = { ax, ay, aw, ah };
    SDL_SetRenderClipRect(tng_renderer, &clip);

    SDL_FRect close_r, prev_r, next_r;
    Get_About_Button_Rects(ax, ay, aw, ah, &close_r, &prev_r, &next_r);

    switch (about_page) {
    case 0:
        Draw_About_Shadow_Text(ax, aw, ABOUT_BORDER, ay + ABOUT_BORDER,
            "ABOUT XPILOT\n"
            "\n"
            "The game was conceived in its original form at the "
            "University of Tromso (Norway) by Ken Ronny Schouten and "
            "Bjorn Stabell during the fall of 1991, but much of the game today "
            "is the result of hard efforts by Bert Gijsbers of the "
            "molecular cytology lab at the University of Amsterdam "
            "(The Netherlands).  "
            "Bert joined the team in the spring of 1993.\n"
            "\n"
            "Dick Balaska (Connecticut, USA) ported XPilot to Windows 95 and NT "
            "in the summer of 1996.\n"
            "\n"
            "A large number of features have been contributed by XPilot fans from "
            "all around the world.  See the CREDITS file for details.\n"
            "\n"
            "For more information, "
            "read the XPilot FAQ (Frequently Asked Questions).\n"
            "\n\n"
            "Good luck as a future xpilot,\n"
            "Bjorn Stabell, Ken Ronny Schouten, Bert Gijsbers & Dick Balaska");
        break;

    case 1:
        Draw_About_Shadow_Text(ax, aw, ABOUT_BORDER, ay + ABOUT_BORDER,
            "ABOUT XPILOT NG\n"
            "\n"
            "XPilot NG is an improved version of XPilot.\n\n"
            "For more info visit http://xpilot.sourceforge.net/ or\n"
            "read the man pages xpilot-ng-x11(6) and xpilot-ng-server(6).\n\n"
            "You can report any bug you find to <" PACKAGE_BUGREPORT ">.");
        break;

    case 2:
        Draw_About_Shadow_Text(ax, aw, ABOUT_BORDER, ay + ABOUT_BORDER,
            "GAME OBJECTIVE\n"
            "\n"
            "XPilot is a multi-player 2D space game.  "
            "Some features are borrowed from classics like the Atari coin-ups "
            "Asteroids and Gravitar, and the home-computer games "
            "Thrust (Commdore 64) and Gravity Force, but XPilot has many "
            "new features as well.\n"
            "\n"
            "The primary goal of the game is to collect points and increase "
            "your rating by destroying enemy fighters and cannons.  "
            "You are equipped with a machine gun when you start the game, "
            "but after a while you should have managed to collect some other "
            "fancy equipment.\n"
            "\n"
            "Another important task is to refuel your ship.  This is "
            "vital because your engine, radar, weapons and shields all "
            "require fuel.  Some even work better the more fuel you "
            "have aboard (mainly the radar).\n"
            "\n"
            "Optional modes include variations on this game play: "
            "you can play together in teams, you can disable shields "
            "(and all other equipment if you like), "
            "you can race against time and fellow players, and much much more.");
        break;

    case 3:
    case 4: {
        int y, old_y, box_start, box_end, first, last;
        if (about_page == 4 && itemsplit == -1) {
            itemsplit = 11;
        }
        if (about_page == 3) {
            y = Draw_About_Shadow_Text(ax, aw, ABOUT_BORDER, ay + ABOUT_BORDER,
                "BONUS ITEMS\n"
                "\n"
                "Scattered around the world you might find some "
                "of these red triangle objects.  They are "
                "well worth picking up since they either improve "
                "on the equipment you have, or they give you "
                "new equipment.  If a fighter explodes, some of "
                "its equipment might be found among the debris.");
            first = 0;
            last = (itemsplit == -1) ? (NUM_ITEMS - 1) : itemsplit;
        } else {
            y = Draw_About_Shadow_Text(ax, aw, ABOUT_BORDER, ay + ABOUT_BORDER,
                "BONUS ITEMS CONTINUED\n");
            first = itemsplit + 1;
            last = NUM_ITEMS - 1;
        }

        y += ABOUT_BORDER;
        box_start = y;
        y += ABOUT_BORDER / 2;

        for (int i = first; i <= last; i++) {
            y += ABOUT_BORDER / 2;
            old_y = y;
            y = Draw_About_Shadow_Text(ax, aw,
                                       5 * ABOUT_BORDER + 2 * ITEM_SIZE,
                                       old_y, Item_get_text(i));
            if (y - old_y < 2 * ITEM_TRIANGLE_SIZE)
                y = old_y + 2 * ITEM_TRIANGLE_SIZE;
            box_end = y + ABOUT_BORDER / 2;
            if (i == last)
                box_end += ABOUT_BORDER / 2;

            if (about_page == 3 && itemsplit == -1 &&
                box_end >= (int)(close_r.y - ABOUT_BORDER)) {
                itemsplit = i - 1;
                SDL3_Fill_Rect((float)(ax + ABOUT_BORDER), (float)box_start,
                               (float)(aw - 2 * ABOUT_BORDER),
                               (float)(box_end - box_start), windowColor);
                break;
            }

            /* Paint the item on the left side in a black box */
            SDL3_Fill_Rect((float)(ax + ABOUT_BORDER), (float)box_start,
                           (float)(2 * ITEM_SIZE + 2 * ABOUT_BORDER),
                           (float)(box_end - box_start), BLACK);
            Draw_About_Item(i, ax + 2 * ABOUT_BORDER + ITEM_SIZE,
                            old_y + ITEM_TRIANGLE_SIZE);

            y = box_end;
            box_start = box_end;
        }
        if (about_page == 3 && itemsplit == -1)
            itemsplit = NUM_ITEMS - 1;
        break;
    }
    default:
        break;
    }

    /* Bottom buttons: CLOSE, PREV, NEXT */
    Draw_Widget_Button(close_r.x, close_r.y, close_r.w, close_r.h,
                       (about_btn_pressed == 0 && about_btn_hover == 0), false, "CLOSE");
    Draw_Widget_Button(prev_r.x, prev_r.y, prev_r.w, prev_r.h,
                       (about_btn_pressed == 1 && about_btn_hover == 1), false, "PREV");
    Draw_Widget_Button(next_r.x, next_r.y, next_r.w, next_r.h,
                       (about_btn_pressed == 2 && about_btn_hover == 2), false, "NEXT");

    SDL_SetRenderClipRect(tng_renderer, NULL);
}

/* =========================================================================
 * Scrollable Viewer Popup Window (KEYS & MOTD, matching x11/widget.c)
 * ========================================================================= */
#define MAX_MOTD_SIZE (30 * 1024)

int keys_viewer = 0;
int motd_viewer = 0;

typedef struct {
    const char *txt;
    int         len;
    int         txt_width;
} viewer_line_t;

typedef struct {
    bool           active;
    char           title[128];
    const char    *save_filename;
    char          *buf;
    int            len;
    bool           owns_buf;
    viewer_line_t *lines;
    int            num_lines;
    int            max_width;
    int            visible_x;
    int            visible_y;
    int            real_width;
    int            real_height;
    bool           dragging_vert;
    bool           dragging_hori;
    int            btn_pressed; /* 0=SAVE, 1=CLOSE */
    int            btn_hover;
} sdl3_viewer_t;

static sdl3_viewer_t active_viewer = { 0 };
static char         *motd_buf      = NULL;
static size_t        motd_size     = 0;

static void Viewer_Get_Layout(int *vx, int *vy, int *vw, int *vh,
                              int *text_w, int *text_h,
                              SDL_FRect *hori_r, SDL_FRect *vert_r,
                              SDL_FRect *save_r, SDL_FRect *close_r)
{
    int w = (int)(2 * top_width / 3);
    int h = (int)(4 * top_height / 5);
    if (w < 420) w = 420;
    if (h < 300) h = 300;
    if (w > (int)top_width - 20) w = (int)top_width - 20;
    if (h > (int)top_height - 20) h = (int)top_height - 20;

    int x = ((int)top_width - w) / 2;
    int y = ((int)top_height - h) / 2;

    int save_w = 16 + (int)SDL3_Text_Width("SAVE");
    int save_h = 6 + FONT_HEIGHT;
    int close_w = 16 + (int)SDL3_Text_Width("CLOSE");
    int close_h = 6 + FONT_HEIGHT;
    int close_y_offset = 5;
    int close_spaced_height = close_h + 2 * close_y_offset;
    int hori_slider_h = 20;
    int vert_slider_w = 20;
    int viewer_w = w - vert_slider_w;
    int viewer_h = h - close_spaced_height - hori_slider_h;

    if (vx) *vx = x;
    if (vy) *vy = y;
    if (vw) *vw = w;
    if (vh) *vh = h;
    if (text_w) *text_w = viewer_w;
    if (text_h) *text_h = viewer_h;

    if (hori_r) *hori_r = (SDL_FRect){ (float)x, (float)(y + viewer_h), (float)viewer_w, (float)hori_slider_h };
    if (vert_r) *vert_r = (SDL_FRect){ (float)(x + viewer_w), (float)y, (float)vert_slider_w, (float)viewer_h };
    if (save_r) *save_r = (SDL_FRect){ (float)(x + w / 3 - save_w / 2), (float)(y + viewer_h + hori_slider_h + close_y_offset), (float)save_w, (float)save_h };
    if (close_r) *close_r = (SDL_FRect){ (float)(x + 2 * w / 3 - close_w / 2), (float)(y + viewer_h + hori_slider_h + close_y_offset), (float)close_w, (float)close_h };
}

static void Viewer_Calculate_Text(void)
{
    if (active_viewer.lines) {
        free(active_viewer.lines);
        active_viewer.lines = NULL;
    }
    active_viewer.num_lines = 0;
    active_viewer.max_width = 0;

    if (!active_viewer.buf || active_viewer.len <= 0)
        return;

    int count = 0;
    for (int i = 0; i < active_viewer.len; i++) {
        if (active_viewer.buf[i] == '\n')
            count++;
    }
    if (active_viewer.len > 0 && active_viewer.buf[active_viewer.len - 1] != '\n')
        count++;

    if (count <= 0)
        return;

    active_viewer.lines = (viewer_line_t *)calloc((size_t)count, sizeof(viewer_line_t));
    if (!active_viewer.lines)
        return;

    active_viewer.num_lines = count;
    int i = 0;
    for (int c = 0; c < count; c++, i++) {
        active_viewer.lines[c].txt = &active_viewer.buf[i];
        while (i < active_viewer.len && active_viewer.buf[i] != '\n') {
            i++;
        }
        active_viewer.lines[c].len = (int)(&active_viewer.buf[i] - active_viewer.lines[c].txt);
        active_viewer.lines[c].txt_width = (int)SDL3_Text_Width_Len(active_viewer.lines[c].len);
        if (active_viewer.lines[c].txt_width > active_viewer.max_width)
            active_viewer.max_width = active_viewer.lines[c].txt_width;
    }

    int text_w = 0, text_h = 0;
    Viewer_Get_Layout(NULL, NULL, NULL, NULL, &text_w, &text_h, NULL, NULL, NULL, NULL);

    int new_w = 40 + active_viewer.max_width;
    int new_h = 40 + active_viewer.num_lines * LINE_HEIGHT;
    if (new_w < text_w) new_w = text_w;
    if (new_h < text_h) new_h = text_h;

    active_viewer.real_width = new_w;
    active_viewer.real_height = new_h;
    if (active_viewer.visible_x > new_w - text_w)
        active_viewer.visible_x = new_w - text_w;
    if (active_viewer.visible_x < 0)
        active_viewer.visible_x = 0;
    if (active_viewer.visible_y > new_h - text_h)
        active_viewer.visible_y = new_h - text_h;
    if (active_viewer.visible_y < 0)
        active_viewer.visible_y = 0;
}

static void Viewer_Open(char *buf, int len, bool owns_buf, const char *title, const char *save_filename)
{
    if (active_viewer.owns_buf && active_viewer.buf && active_viewer.buf != buf) {
        free(active_viewer.buf);
    }
    active_viewer.buf = buf;
    active_viewer.len = len;
    active_viewer.owns_buf = owns_buf;
    strlcpy(active_viewer.title, title ? title : "XPilot Viewer", sizeof(active_viewer.title));
    active_viewer.save_filename = save_filename ? save_filename : "xpilot.motd";
    active_viewer.visible_x = 0;
    active_viewer.visible_y = 0;
    active_viewer.dragging_vert = false;
    active_viewer.dragging_hori = false;
    active_viewer.btn_pressed = -1;
    active_viewer.btn_hover = -1;
    active_viewer.active = true;

    Viewer_Calculate_Text();
}

static void Viewer_Close(void)
{
    active_viewer.active = false;
    active_viewer.dragging_vert = false;
    active_viewer.dragging_hori = false;
    keys_viewer = 0;
    motd_viewer = 0;
}

static void Render_Viewer_Window(void)
{
    if (!active_viewer.active || !tng_renderer) return;

    int vx, vy, vw, vh, text_w, text_h;
    SDL_FRect hori_r, vert_r, save_r, close_r;
    Viewer_Get_Layout(&vx, &vy, &vw, &vh, &text_w, &text_h, &hori_r, &vert_r, &save_r, &close_r);

    if (active_viewer.real_width < text_w) active_viewer.real_width = text_w;
    if (active_viewer.real_height < text_h) active_viewer.real_height = text_h;

    SDL_SetRenderClipRect(tng_renderer, NULL);
    /* Outer 2px border + windowColor background */
    SDL3_Fill_Rect((float)(vx - 2), (float)(vy - 2), (float)(vw + 4), (float)(vh + 4), borderColor);
    SDL3_Fill_Rect((float)vx, (float)vy, (float)vw, (float)vh, windowColor);

    /* Clip to text area */
    SDL_Rect text_clip = { vx, vy, text_w, text_h };
    SDL_SetRenderClipRect(tng_renderer, &text_clip);

    int start = (active_viewer.visible_y - 20) / LINE_HEIGHT;
    int end   = (active_viewer.visible_y + text_h - 20) / LINE_HEIGHT + 1;
    if (start < 0) start = 0;
    if (end > active_viewer.num_lines) end = active_viewer.num_lines;

    for (int i = start; i < end; i++) {
        float lx = (float)(vx + 20 - active_viewer.visible_x);
        float ly = (float)(vy + 20 + i * LINE_HEIGHT - active_viewer.visible_y);
        Shadow_Draw_Text_Len(active_viewer.lines[i].txt, active_viewer.lines[i].len, lx, ly, WHITE);
    }

    SDL_SetRenderClipRect(tng_renderer, NULL);

    /* Draw Horizontal Slider (matching Widget_draw_slider) */
    {
        float unit = hori_r.h;
        SDL3_Fill_Rect(hori_r.x, hori_r.y, hori_r.w, hori_r.h, BLACK);
        SDL3_Draw_Rect(hori_r.x, hori_r.y, hori_r.w, hori_r.h, borderColor);

        SDL_FPoint lpts[3] = {
            { hori_r.x + unit * 0.25f, hori_r.y + unit * 0.5f },
            { hori_r.x + unit * 0.75f, hori_r.y + unit * 0.25f },
            { hori_r.x + unit * 0.75f, hori_r.y + unit * 0.75f }
        };
        SDL3_Fill_Polygon(lpts, 3, buttonColor);

        SDL_FPoint rpts[3] = {
            { hori_r.x + hori_r.w - unit * 0.25f, hori_r.y + unit * 0.5f },
            { hori_r.x + hori_r.w - unit * 0.75f, hori_r.y + unit * 0.25f },
            { hori_r.x + hori_r.w - unit * 0.75f, hori_r.y + unit * 0.75f }
        };
        SDL3_Fill_Polygon(rpts, 3, buttonColor);

        float block_offset = unit;
        float block_max_size = hori_r.w - 2.0f * block_offset;
        if (block_max_size > 0.0f && active_viewer.real_width > 0) {
            float block_size = block_max_size * (float)text_w / (float)active_viewer.real_width;
            float block_start = block_max_size * (float)active_viewer.visible_x / (float)active_viewer.real_width;
            SDL3_Fill_Rect(hori_r.x + block_offset + block_start, hori_r.y + unit * 0.25f,
                           block_size, unit * 0.5f, buttonColor);
        }
    }

    /* Draw Vertical Slider (matching Widget_draw_slider) */
    {
        float unit = vert_r.w;
        SDL3_Fill_Rect(vert_r.x, vert_r.y, vert_r.w, vert_r.h, BLACK);
        SDL3_Draw_Rect(vert_r.x, vert_r.y, vert_r.w, vert_r.h, borderColor);

        SDL_FPoint upts[3] = {
            { vert_r.x + unit * 0.5f,  vert_r.y + unit * 0.25f },
            { vert_r.x + unit * 0.25f, vert_r.y + unit * 0.75f },
            { vert_r.x + unit * 0.75f, vert_r.y + unit * 0.75f }
        };
        SDL3_Fill_Polygon(upts, 3, buttonColor);

        SDL_FPoint dpts[3] = {
            { vert_r.x + unit * 0.5f,  vert_r.y + vert_r.h - unit * 0.25f },
            { vert_r.x + unit * 0.25f, vert_r.y + vert_r.h - unit * 0.75f },
            { vert_r.x + unit * 0.75f, vert_r.y + vert_r.h - unit * 0.75f }
        };
        SDL3_Fill_Polygon(dpts, 3, buttonColor);

        float block_offset = unit;
        float block_max_size = vert_r.h - 2.0f * block_offset;
        if (block_max_size > 0.0f && active_viewer.real_height > 0) {
            float block_size = block_max_size * (float)text_h / (float)active_viewer.real_height;
            float block_start = block_max_size * (float)active_viewer.visible_y / (float)active_viewer.real_height;
            SDL3_Fill_Rect(vert_r.x + unit * 0.25f, vert_r.y + block_offset + block_start,
                           unit * 0.5f, block_size, buttonColor);
        }
    }

    /* Draw SAVE and CLOSE buttons */
    Draw_Widget_Button(save_r.x, save_r.y, save_r.w, save_r.h,
                       (active_viewer.btn_pressed == 0 && active_viewer.btn_hover == 0), true, "SAVE");
    Draw_Widget_Button(close_r.x, close_r.y, close_r.w, close_r.h,
                       (active_viewer.btn_pressed == 1 && active_viewer.btn_hover == 1), true, "CLOSE");
}

static void Open_Keys_Viewer(void)
{
    size_t bufsize = (size_t)num_keydefs * 64 + 1024;
    char *buf = (char *)calloc(bufsize, 1);
    if (!buf) return;

    int maxkeylen = 0;
    for (int i = 0; i < num_keydefs; i++) {
        const char *kstr = Xp_keysym_to_string(keydefs[i].keysym);
        if (kstr) {
            int len = (int)strlen(kstr);
            if (len > maxkeylen) maxkeylen = len;
        }
    }

    size_t used = 0;
    for (int i = 0; i < num_keydefs; i++) {
        const char *kstr = Xp_keysym_to_string(keydefs[i].keysym);
        const char *help = Get_keyHelpString(keydefs[i].key);
        if (!kstr || !help) continue;

        size_t needed = (size_t)maxkeylen + strlen(help) + 8;
        if (used + needed >= bufsize) {
            bufsize += 4096;
            char *nb = (char *)realloc(buf, bufsize);
            if (!nb) break;
            buf = nb;
        }
        int n = snprintf(buf + used, bufsize - used, "%-*s  %s\n", maxkeylen, kstr, help);
        if (n > 0) used += (size_t)n;
    }

    keys_viewer = 1;
    motd_viewer = 0;
    Viewer_Open(buf, (int)used, true, "XPilot - key reference", "xpilot.keys");
}

static void Open_Motd_Viewer(void)
{
    Net_ask_for_motd(0, MAX_MOTD_SIZE);
    Net_flush();
    if (motd_buf && motd_size > 0) {
        char title[128];
        snprintf(title, sizeof(title), "XPilot motd from %s", servername);
        motd_viewer = 1;
        keys_viewer = 0;
        Viewer_Open(motd_buf, (int)motd_size, false, title, "xpilot.motd");
    }
}

int Handle_motd(long off, char *buf, int len, long filesize)
{
    static const char no_motd_msg[] = "\nThis server has no MOTD.\n\n";

    if (!motd_buf) {
        motd_size = (size_t)MIN(filesize, MAX_MOTD_SIZE);
        size_t alloc_sz = MAX(motd_size, sizeof(no_motd_msg)) + 1;
        motd_buf = (char *)malloc(alloc_sz);
        if (!motd_buf) {
            error("No memory for MOTD");
            return -1;
        }
        memset(motd_buf, ' ', motd_size);
        for (size_t i = 39; i < motd_size; i += 40)
            motd_buf[i] = '\n';
        motd_buf[motd_size] = '\0';
    } else if (filesize < (long)motd_size) {
        motd_size = (size_t)filesize;
        motd_buf[motd_size] = '\0';
    }

    if (off < (long)motd_size && len > 0) {
        if (off + len > (long)motd_size)
            len = (int)motd_size - (int)off;
        memcpy(motd_buf + off, buf, (size_t)len);
    } else if (len == 0 && off > 0) {
        return 0;
    }

    if (motd_size == 0) {
        strcpy(motd_buf, no_motd_msg);
        motd_size = strlen(motd_buf);
    }

    int cur_len = (off || len) ? (int)(off + len) : (int)strlen(motd_buf);
    if ((size_t)cur_len < motd_size)
        cur_len = (int)motd_size;

    char title[128];
    snprintf(title, sizeof(title), "XPilot motd from %s", servername);

    if (!active_viewer.active || motd_viewer == 0) {
        motd_viewer = 1;
        keys_viewer = 0;
        Viewer_Open(motd_buf, cur_len, false, title, "xpilot.motd");
    } else if (motd_viewer != 0) {
        active_viewer.buf = motd_buf;
        active_viewer.len = cur_len;
        Viewer_Calculate_Text();
    }

    return 0;
}

void aboutCleanup(void)
{
    if (active_viewer.lines) {
        free(active_viewer.lines);
        active_viewer.lines = NULL;
    }
    if (active_viewer.owns_buf && active_viewer.buf) {
        free(active_viewer.buf);
        active_viewer.buf = NULL;
    }
    if (motd_buf) {
        free(motd_buf);
        motd_buf = NULL;
    }
}

/* =========================================================================
 * Real-Time Sidebar Configuration (CONFIG & COLORS, matching x11/configure.c)
 * ========================================================================= */
#define CFG_SPACE         6
#define CFG_TEXT_SPACE    3
#define CFG_BUTTON_SPACE  3
#define CFG_ENTRY_HEIGHT  16
#define CFG_ARROW_W       16
#define CFG_ARROW_H       16
#define CFG_BOOL_W        30
#define CFG_INT_W         44
#define CFG_DOUBLE_W      44

typedef struct {
    xp_option_t *opt;     /* NULL for Save Configuration button */
    int          page;
    SDL_FRect    label_r;
    SDL_FRect    val_r;   /* Bool button, Int/Double/Color value box, or Save button */
    SDL_FRect    left_r;  /* Left arrow '<' */
    SDL_FRect    right_r; /* Right arrow '>' */
} cfg_entry_t;

static int  *default_option_indices = NULL;
static int   num_default_options    = 0;
static int   max_default_options    = 0;

static int  *color_option_indices   = NULL;
static int   num_color_options      = 0;
static int   max_color_options      = 0;

static bool         config_mapped      = false;
static int          config_what        = CONFIG_NONE;
static int          config_page        = 0;
static int          config_max_pages   = 0;
static cfg_entry_t *cfg_entries        = NULL;
static int          num_cfg_entries    = 0;
static int          cfg_pressed_entry  = -1;
static int          cfg_pressed_part   = 0;  /* 1=val/bool/save, 2=left, 3=right */
static int          cfg_nav_pressed    = -1; /* 0=CLOSE, 1=PREV, 2=NEXT */
static int          cfg_nav_hover      = -1;
static char         cfg_save_label[32] = "Save Configuration";

static int Nelem_config_creator(void)
{
    if (config_what == CONFIG_DEFAULT)
        return num_default_options + 1;
    if (config_what == CONFIG_COLORS)
        return num_color_options + 1;
    return 0;
}

static xp_option_t *Config_creator_option(int i)
{
    int ind = -1;
    if (config_what == CONFIG_DEFAULT && i >= 0 && i < num_default_options)
        ind = default_option_indices[i];
    if (config_what == CONFIG_COLORS && i >= 0 && i < num_color_options)
        ind = color_option_indices[i];
    return Option_by_index(ind);
}

void Config_init(void)
{
    num_default_options = 0;
    num_color_options = 0;
    for (int i = 0; i < num_options; i++) {
        xp_option_t *opt = Option_by_index(i);
        if (Option_get_flags(opt) & XP_OPTFLAG_CONFIG_COLORS) {
            STORE(int, color_option_indices, num_color_options, max_color_options, i);
        } else if (Option_get_flags(opt) & XP_OPTFLAG_CONFIG_DEFAULT) {
            STORE(int, default_option_indices, num_default_options, max_default_options, i);
        }
    }
}

static void Get_Config_Nav_Rects(SDL_FRect *close_r, SDL_FRect *prev_r, SDL_FRect *next_r)
{
    float config_y = (float)SCORE_Y_OFFSET;
    float config_h = (float)((int)top_height - SCORE_Y_OFFSET);
    float by = config_y + config_h - CFG_SPACE - CFG_ENTRY_HEIGHT;

    float close_w = 2.0f * CFG_BUTTON_SPACE + SDL3_Text_Width("CLOSE");
    float prev_w  = 2.0f * CFG_BUTTON_SPACE + SDL3_Text_Width("PREV");
    float next_w  = 2.0f * CFG_BUTTON_SPACE + SDL3_Text_Width("NEXT");

    if (close_r) *close_r = (SDL_FRect){ (float)CFG_SPACE, by, close_w, (float)CFG_ENTRY_HEIGHT };
    if (prev_r)  *prev_r  = (SDL_FRect){ (SIDEBAR_WIDTH - prev_w) * 0.5f, by, prev_w, (float)CFG_ENTRY_HEIGHT };
    if (next_r)  *next_r  = (SDL_FRect){ (float)(SIDEBAR_WIDTH - CFG_SPACE) - next_w, by, next_w, (float)CFG_ENTRY_HEIGHT };
}

static void Layout_Config_Pages(void)
{
    if (cfg_entries) {
        free(cfg_entries);
        cfg_entries = NULL;
    }
    num_cfg_entries = Nelem_config_creator();
    if (num_cfg_entries <= 0) {
        config_max_pages = 0;
        return;
    }

    cfg_entries = (cfg_entry_t *)calloc((size_t)num_cfg_entries, sizeof(cfg_entry_t));
    if (!cfg_entries) {
        num_cfg_entries = 0;
        config_max_pages = 0;
        return;
    }

    int config_height = (int)top_height - SCORE_Y_OFFSET;
    int page = 0;
    int height = CFG_SPACE;

    for (int i = 0; i < num_cfg_entries; i++) {
        xp_option_t *opt = Config_creator_option(i);
        cfg_entry_t *e = &cfg_entries[i];
        e->opt = opt;

        if (opt == NULL) {
            /* Save Configuration button */
            int space = config_height - (height + 2 * CFG_ENTRY_HEIGHT + 2 * CFG_SPACE);
            if (space < 0 && height > CFG_SPACE) {
                page++;
                height = CFG_SPACE;
                space = config_height - (height + 2 * CFG_ENTRY_HEIGHT + 2 * CFG_SPACE);
            }
            if (space < 0) space = 0;
            float w = 2.0f * CFG_BUTTON_SPACE + SDL3_Text_Width(cfg_save_label);
            e->page = page;
            e->val_r = (SDL_FRect){
                (SIDEBAR_WIDTH - w) * 0.5f,
                (float)(SCORE_Y_OFFSET + height + space / 2),
                w,
                (float)CFG_ENTRY_HEIGHT
            };
            height += CFG_ENTRY_HEIGHT + CFG_SPACE + space;
            continue;
        }

        const char *name = Option_get_name(opt);
        int label_w = (int)SDL3_Text_Width(name) + 2 * CFG_TEXT_SPACE;
        int offset = 0;
        if (Option_get_type(opt) == xp_bool_option) {
            offset = SIDEBAR_WIDTH - (CFG_SPACE + CFG_BOOL_W);
        } else {
            offset = SIDEBAR_WIDTH - (CFG_SPACE + 2 * CFG_ARROW_W + CFG_INT_W);
        }

        bool two_lines = (CFG_SPACE + label_w > offset);
        int req_h = (two_lines ? 3 : 2) * CFG_ENTRY_HEIGHT + 2 * CFG_SPACE;
        if (height + req_h >= config_height && height > CFG_SPACE) {
            page++;
            height = CFG_SPACE;
        }

        e->page = page;
        e->label_r = (SDL_FRect){
            (float)CFG_SPACE,
            (float)(SCORE_Y_OFFSET + height),
            (float)label_w,
            (float)CFG_ENTRY_HEIGHT
        };

        if (two_lines) {
            height += CFG_ENTRY_HEIGHT;
        }

        float vy = (float)(SCORE_Y_OFFSET + height);
        if (Option_get_type(opt) == xp_bool_option) {
            e->val_r = (SDL_FRect){ (float)offset, vy, (float)CFG_BOOL_W, (float)CFG_ENTRY_HEIGHT };
        } else {
            e->val_r   = (SDL_FRect){ (float)offset, vy, (float)CFG_INT_W, (float)CFG_ENTRY_HEIGHT };
            e->left_r  = (SDL_FRect){ (float)(offset + CFG_INT_W), vy, (float)CFG_ARROW_W, (float)CFG_ARROW_H };
            e->right_r = (SDL_FRect){ (float)(offset + CFG_INT_W + CFG_ARROW_W), vy, (float)CFG_ARROW_W, (float)CFG_ARROW_H };
        }

        height += CFG_ENTRY_HEIGHT + CFG_SPACE;
    }

    config_max_pages = page + 1;
    if (config_page >= config_max_pages)
        config_page = 0;
}

int Config(bool doit, int what)
{
    if (!doit) {
        config_mapped = false;
        config_what = CONFIG_NONE;
        scoresChanged = true;
        return 0;
    }

    if (num_default_options == 0 && num_color_options == 0) {
        Config_init();
    }

    if (config_what != what || !cfg_entries) {
        config_what = what;
        config_page = 0;
        strlcpy(cfg_save_label, "Save Configuration", sizeof(cfg_save_label));
        Layout_Config_Pages();
    }
    config_mapped = true;
    return 1;
}

void Config_destroy(void)
{
    if (cfg_entries) {
        free(cfg_entries);
        cfg_entries = NULL;
    }
    num_cfg_entries = 0;
    config_mapped = false;
}

void Config_resize(void)
{
    if (config_mapped) {
        Layout_Config_Pages();
    }
}

void Config_redraw(void)
{
    /* Rendered every frame in SDL3_Render_Sidebar when config_mapped is true */
}

static void Render_Config_Panel(void)
{
    if (!tng_renderer) return;

    SDL_Rect score_rect = { 0, SCORE_Y_OFFSET, SIDEBAR_WIDTH, (int)(top_height - SCORE_Y_OFFSET) };
    SDL_SetRenderClipRect(tng_renderer, &score_rect);

    /* Fill config form background with windowColor (matching Widget_create_form in configure.c) */
    SDL3_Fill_Rect(0.0f, (float)SCORE_Y_OFFSET, (float)SIDEBAR_WIDTH,
                   (float)(top_height - SCORE_Y_OFFSET), windowColor);

    for (int i = 0; i < num_cfg_entries; i++) {
        cfg_entry_t *e = &cfg_entries[i];
        if (e->page != config_page)
            continue;

        xp_option_t *opt = e->opt;
        if (opt == NULL) {
            /* Save Configuration button */
            bool active = (cfg_pressed_entry == i && cfg_pressed_part == 1);
            Draw_Widget_Button(e->val_r.x, e->val_r.y, e->val_r.w, e->val_r.h,
                               active, false, cfg_save_label);
            continue;
        }

        /* Draw label */
        SDL3_Fill_Rect(e->label_r.x, e->label_r.y, e->label_r.w, e->label_r.h, windowColor);
        Shadow_Draw_Text(Option_get_name(opt),
                         e->label_r.x + CFG_TEXT_SPACE,
                         e->label_r.y + 3.0f, WHITE);

        if (Option_get_flags(opt) & XP_OPTFLAG_CONFIG_COLORS) {
            int cval = *opt->int_ptr;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", cval);
            SDL3_Fill_Rect(e->val_r.x, e->val_r.y, e->val_r.w, e->val_r.h, cval);
            int text_col = (cval == WHITE) ? BLACK : WHITE;
            float tw = SDL3_Text_Width(buf);
            SDL3_Draw_Text(buf,
                           e->val_r.x + (e->val_r.w - tw) * 0.5f,
                           e->val_r.y + 3.0f, text_col);

            Draw_Widget_Arrow(e->left_r.x, e->left_r.y, e->left_r.w, e->left_r.h,
                              false, (cfg_pressed_entry == i && cfg_pressed_part == 2));
            Draw_Widget_Arrow(e->right_r.x, e->right_r.y, e->right_r.w, e->right_r.h,
                              true, (cfg_pressed_entry == i && cfg_pressed_part == 3));
        } else {
            switch (Option_get_type(opt)) {
            case xp_bool_option: {
                bool bval = *opt->bool_ptr;
                bool active = (cfg_pressed_entry == i && cfg_pressed_part == 1);
                Draw_Widget_Button(e->val_r.x, e->val_r.y, e->val_r.w, e->val_r.h,
                                   active, false, bval ? "Yes" : "No");
                break;
            }
            case xp_int_option: {
                int ival = *opt->int_ptr;
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", ival);
                SDL3_Fill_Rect(e->val_r.x, e->val_r.y, e->val_r.w, e->val_r.h, BLACK);
                float tw = SDL3_Text_Width(buf);
                SDL3_Draw_Text(buf,
                               e->val_r.x + (e->val_r.w - tw) * 0.5f,
                               e->val_r.y + 3.0f, WHITE);

                Draw_Widget_Arrow(e->left_r.x, e->left_r.y, e->left_r.w, e->left_r.h,
                                  false, (cfg_pressed_entry == i && cfg_pressed_part == 2));
                Draw_Widget_Arrow(e->right_r.x, e->right_r.y, e->right_r.w, e->right_r.h,
                                  true, (cfg_pressed_entry == i && cfg_pressed_part == 3));
                break;
            }
            case xp_double_option: {
                double dval = *opt->dbl_ptr;
                char buf[16];
                if (dval < 10.0)
                    snprintf(buf, sizeof(buf), "%.2f", dval);
                else if (dval < 100.0)
                    snprintf(buf, sizeof(buf), "%.1f", dval);
                else
                    snprintf(buf, sizeof(buf), "%d", (int)dval);

                SDL3_Fill_Rect(e->val_r.x, e->val_r.y, e->val_r.w, e->val_r.h, BLACK);
                float tw = SDL3_Text_Width(buf);
                SDL3_Draw_Text(buf,
                               e->val_r.x + (e->val_r.w - tw) * 0.5f,
                               e->val_r.y + 3.0f, WHITE);

                Draw_Widget_Arrow(e->left_r.x, e->left_r.y, e->left_r.w, e->left_r.h,
                                  false, (cfg_pressed_entry == i && cfg_pressed_part == 2));
                Draw_Widget_Arrow(e->right_r.x, e->right_r.y, e->right_r.w, e->right_r.h,
                                  true, (cfg_pressed_entry == i && cfg_pressed_part == 3));
                break;
            }
            default:
                break;
            }
        }
    }

    /* Bottom navigation buttons: CLOSE, PREV, NEXT */
    SDL_FRect close_r, prev_r, next_r;
    Get_Config_Nav_Rects(&close_r, &prev_r, &next_r);
    Draw_Widget_Button(close_r.x, close_r.y, close_r.w, close_r.h,
                       (cfg_nav_pressed == 0 && cfg_nav_hover == 0), false, "CLOSE");
    Draw_Widget_Button(prev_r.x, prev_r.y, prev_r.w, prev_r.h,
                       (cfg_nav_pressed == 1 && cfg_nav_hover == 1), false, "PREV");
    Draw_Widget_Button(next_r.x, next_r.y, next_r.w, next_r.h,
                       (cfg_nav_pressed == 2 && cfg_nav_hover == 2), false, "NEXT");

    SDL_SetRenderClipRect(tng_renderer, NULL);
}

static void Execute_Config_Entry_Action(int idx, int part)
{
    if (idx < 0 || idx >= num_cfg_entries) return;
    cfg_entry_t *e = &cfg_entries[idx];
    xp_option_t *opt = e->opt;

    if (opt == NULL && part == 1) {
        /* Save Configuration */
        char path[PATH_MAX + 1];
        Xpilotrc_get_filename(path, sizeof(path));
        int retval = Xpilotrc_write(path);
        if (retval < 0) {
            strlcpy(cfg_save_label, "Saving failed...", sizeof(cfg_save_label));
        } else {
            strlcpy(cfg_save_label, "Save Configuration", sizeof(cfg_save_label));
            Add_HUD_message("Configuration saved to .xpilotrc");
        }
        return;
    }

    if (!opt) return;

    if (Option_get_flags(opt) & XP_OPTFLAG_CONFIG_COLORS) {
        if (part == 2 || part == 3) {
            int cval = *opt->int_ptr;
            LIMIT(cval, opt->int_minval, opt->int_maxval);
            if (part == 3) cval++;
            else cval--;
            LIMIT(cval, opt->int_minval, opt->int_maxval);
            if (cval != *opt->int_ptr) {
                Set_int_option(opt, cval, xp_option_origin_config);
                Colors_init_style_colors();
                UpdateRadar = true;
                scoresChanged = true;
            }
        }
        return;
    }

    switch (Option_get_type(opt)) {
    case xp_bool_option:
        if (part == 1) {
            bool nval = !(*opt->bool_ptr);
            Set_bool_option(opt, nval, xp_option_origin_config);
        }
        break;

    case xp_int_option:
        if (part == 2 || part == 3) {
            int cur = *opt->int_ptr;
            int ival = cur;
            LIMIT(ival, opt->int_minval, opt->int_maxval);
            if (part == 3) {
                ival = (int)(cur * 1.05 + 0.5);
                if (ival == cur) ival++;
            } else {
                ival = (int)(cur * 0.95);
                if (ival == cur) ival--;
            }
            LIMIT(ival, opt->int_minval, opt->int_maxval);
            if (ival != cur) {
                Set_int_option(opt, ival, xp_option_origin_config);
            }
        }
        break;

    case xp_double_option:
        if (part == 2 || part == 3) {
            double cur = *opt->dbl_ptr;
            double dval = cur;
            double dmin_val = opt->dbl_minval;
            double dmax_val = opt->dbl_maxval;
            LIMIT(dval, dmin_val, dmax_val);
            double delta = dmax_val - dmin_val;
            double dmin, offset, newoffset;
            if (dval >= 0.0) {
                dmin = (dmin_val < 0.0) ? 0.0 : dmin_val;
                offset = dval - dmin;
            } else {
                dmin = (dmax_val > 0.0) ? 0.0 : dmax_val;
                offset = -dval + dmin;
            }
            if ((part == 3) == (dval >= 0.0)) {
                newoffset = offset * 1.05;
                if (newoffset - offset < delta / 100.0)
                    newoffset = offset + delta / 100.0;
            } else {
                newoffset = offset * 0.95;
                if (newoffset - offset > -delta / 100.0)
                    newoffset = offset - delta / 100.0;
                if (newoffset < 0.0 && offset > 0.0)
                    newoffset = 0.0;
            }
            if (dval >= 0.0)
                dval = dmin + newoffset;
            else
                dval = dmin - newoffset;
            LIMIT(dval, dmin_val, dmax_val);
            if (dval != cur) {
                Set_double_option(opt, dval, xp_option_origin_config);
            }
        }
        break;

    default:
        break;
    }
}

/* =========================================================================
 * Scoreboard & Sidebar Rendering
 * ========================================================================= */
void Paint_clock(bool redraw)
{
    (void)redraw;
    if (!tng_renderer || !clockColor) return;

    time_t cur_t = time(NULL);
    struct tm *m = localtime(&cur_t);
    char buf[32];
    if (m) {
        int hour = m->tm_hour;
        int minute = m->tm_min;
        if (!instruments.clockAMPM) {
            snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        } else {
            char tmpchar = 'A';
            if (hour > 12) {
                tmpchar = 'P';
                hour %= 12;
            }
            snprintf(buf, sizeof(buf), "%2d:%02d%cM", hour, minute, tmpchar);
        }
    } else {
        snprintf(buf, sizeof(buf), "00:00");
    }

    float width = SDL3_Text_Width(buf);
    float bx = 256.0f - width - 6.0f;
    float by = (float)SCORE_Y_OFFSET;

    SDL3_Fill_Rect(bx, by, width + 6.0f, 15.0f, windowColor);
    Shadow_Draw_Text(buf, bx + 3.0f, by + 3.0f, clockColor);
}

static void Paint_score_background(void)
{
    if (!tng_renderer) return;

    float play_h = (float)(top_height - SCORE_Y_OFFSET);
    int bgh = 0, lh = 0;
    SDL_Texture *bg_tex = Bitmap_get_texture(BM_SCORE_BG, 0, NULL, &bgh);
    SDL_Texture *logo_tex = Bitmap_get_texture(BM_LOGO, 0, NULL, &lh);

    if (fullColor && bg_tex != NULL && logo_tex != NULL && bgh > 0 && lh > 0) {
        Bitmap_paint(0, BM_SCORE_BG, 0, SCORE_Y_OFFSET, 0);
        if (play_h > (float)(bgh + lh)) {
            SDL3_Fill_Rect(0.0f, (float)(SCORE_Y_OFFSET + bgh), 256.0f, play_h - (float)(bgh + lh), BLACK);
        }
        Bitmap_paint(0, BM_LOGO, 0, (int)top_height - lh, 0);
    } else {
        SDL3_Fill_Rect(0.0f, (float)SCORE_Y_OFFSET, 256.0f, play_h, windowColor);
    }
}

void Paint_score_start(void)
{
    if (!tng_renderer) return;

    /* Scissor clip to scoreboard area below buttons */
    SDL_Rect score_rect = { 0, SCORE_Y_OFFSET, SIDEBAR_WIDTH, (int)(top_height - SCORE_Y_OFFSET) };
    SDL_SetRenderClipRect(tng_renderer, &score_rect);

    /* Draw background textures (clouds.ppm & logo.ppm) */
    Paint_score_background();

    /* Draw Scoreboard Column Header (authentic NG format) */
    char headingStr[MSG_LEN];
    if (showUserName) {
        strlcpy(headingStr, "NICK=USER@HOST", sizeof(headingStr));
    } else if (BIT(Setup->mode, TEAM_PLAY)) {
        strlcpy(headingStr, "     SCORE   NAME     LIFE", sizeof(headingStr));
    } else {
        strlcpy(headingStr, "  ", sizeof(headingStr));
        if (BIT(Setup->mode, TIMING))
            strlcat(headingStr, "LAP ", sizeof(headingStr));
        strlcpy(headingStr, " AL ", sizeof(headingStr));
        strlcat(headingStr, "  SCORE  ", sizeof(headingStr));
        if (BIT(Setup->mode, LIMITED_LIVES))
            strlcat(headingStr, "LIFE", sizeof(headingStr));
        strlcat(headingStr, " NAME", sizeof(headingStr));
    }

    float hy = (float)(SCORE_Y_OFFSET + SCORE_BORDER);
    Shadow_Draw_Text(headingStr, (float)SCORE_BORDER, hy, scoreColor);

    /* Horizontal separator below header */
    SDL3_Draw_Line((float)SCORE_BORDER, hy + 11.0f, (float)(SIDEBAR_WIDTH - SCORE_BORDER), hy + 11.0f, scoreColor);

    /* Clock in header row */
    Paint_clock(true);
}

void Paint_score_entry(int entry_num, other_t *other, bool is_team)
{
    if (!other || !tng_renderer) return;

    float ey = (float)(SCORE_Y_OFFSET + 2 * SCORE_BORDER + LINE_HEIGHT + entry_num * LINE_HEIGHT);
    if (ey > (float)(top_height - 12)) return;

    char raceStr[16] = "";
    char teamStr[4] = "  ";
    char lifeStr[16] = "";
    char scoreStr[16] = "";
    char label[MSG_LEN];

    if (showUserName) {
        snprintf(label, sizeof(label), "%s=%s@%s",
                 other->nick_name, other->user_name, other->host_name);
    } else {
        if (BIT(Setup->mode, TIMING)) {
            strlcpy(raceStr, "   ", sizeof(raceStr));
            if ((other->mychar == ' ' || other->mychar == 'R' || other->mychar == 'S')
                && other->round + other->check > 0) {
                if (other->round > 99)
                    snprintf(raceStr, sizeof(raceStr), "%3d", other->round);
                else
                    snprintf(raceStr, sizeof(raceStr), "%d.%c", other->round, other->check + 'a');
            }
        }
        if (BIT(Setup->mode, TEAM_PLAY)) {
            teamStr[0] = (char)(other->team + '0');
            teamStr[1] = ' ';
            teamStr[2] = '\0';
        } else {
            snprintf(teamStr, sizeof(teamStr), "%c ", other->alliance ? other->alliance : ' ');
        }

        if (BIT(Setup->mode, LIMITED_LIVES)) {
            snprintf(lifeStr, sizeof(lifeStr), " %3d", other->life);
        }

        if (Using_score_decimals()) {
            snprintf(scoreStr, sizeof(scoreStr), "%*.*f",
                     9 - showScoreDecimals, showScoreDecimals, other->score);
        } else {
            double score = other->score;
            int sc = (int)(score >= 0.0 ? score + 0.5 : score - 0.5);
            snprintf(scoreStr, sizeof(scoreStr), "%6d", sc);
        }

        if (BIT(Setup->mode, TEAM_PLAY)) {
            snprintf(label, sizeof(label), "%c %s  %-18s%s",
                     other->mychar, scoreStr, other->nick_name, lifeStr);
        } else {
            snprintf(label, sizeof(label), "%c %s%s%s%s  %s",
                     other->mychar, raceStr, teamStr, scoreStr, lifeStr, other->nick_name);
        }
    }

    int color;
    if (!is_team && strchr("DPW", other->mychar)) {
        if (self && other->id == self->id)
            color = scoreInactiveSelfColor;
        else
            color = scoreInactiveColor;
        SDL3_Draw_Text(label, (float)SCORE_BORDER, ey, color);
    } else {
        if (!is_team) {
            if (self && other->id == self->id)
                color = scoreSelfColor;
            else
                color = scoreColor;
        } else {
            color = Team_color(other->team);
            if (!color) {
                if (self && other->team == self->team)
                    color = scoreOwnTeamColor;
                else
                    color = scoreEnemyTeamColor;
            }
        }
        Shadow_Draw_Text(label, (float)SCORE_BORDER, ey, color);
    }

    /* Underline the teams */
    if (is_team) {
        int ucol = (windowColor != WHITE) ? WHITE : BLACK;
        SDL3_Draw_Line((float)SCORE_BORDER, ey + 11.0f,
                       (float)(SIDEBAR_WIDTH - SCORE_BORDER), ey + 11.0f, ucol);
    }
}

void SDL3_Render_Sidebar(void)
{
    if (!tng_renderer) return;

    /* 0. Fill sidebar background entirely with solid black */
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL3_Fill_Rect(0.0f, 0.0f, (float)SIDEBAR_WIDTH, (float)top_height, BLACK);

    /* 1. Radar is rendered in (0, 0, 256, RadarHeight) */
    Paint_radar();

    /* 2. Reset clip rect for buttons & separators */
    SDL_SetRenderClipRect(tng_renderer, NULL);

    /* 3. Draw vertical separator dividing Sidebar and Playfield */
    SDL3_Draw_Line((float)SIDEBAR_WIDTH, 0.0f, (float)SIDEBAR_WIDTH, (float)top_height, borderColor);
    SDL3_Draw_Line((float)(SIDEBAR_WIDTH + 1), 0.0f, (float)(SIDEBAR_WIDTH + 1), (float)top_height, borderColor);

    /* 4. Draw 3 Control Buttons: QUIT, ABOUT, MENU with authentic x11 styling */
    float by = (float)BTN_ROW_Y;
    float bh = (float)BTN_ROW_H;

    Draw_Widget_Button(0.0f, by, 84.0f, bh,
                       (top_btn_pressed == 0 && top_btn_hover == 0), true, "QUIT");
    Draw_Widget_Button(85.0f, by, 84.0f, bh,
                       (top_btn_pressed == 1 && top_btn_hover == 1), true, "ABOUT");
    Draw_Widget_Button(170.0f, by, 86.0f, bh,
                       false, true, "MENU");

    /* 5. Draw either CONFIG/COLORS panel or Player Scoreboard */
    if (config_mapped) {
        Render_Config_Panel();
    } else {
        scoresChanged = true;
        players_exposed = true;
        Paint_score_table();
    }
}

void SDL3_Render_UI_Overlays(void)
{
    if (!tng_renderer) return;

    SDL_SetRenderClipRect(tng_renderer, NULL);

    /* 1. MENU Pulldown if open */
    if (menu_open) {
        float px = 170.0f;
        float py = (float)(BTN_ROW_Y + BTN_ROW_H);
        float pw = 86.0f;
        float eh = (float)BTN_ROW_H;
        float ph = NUM_MENU_ENTRIES * (eh + 1.0f) + 1.0f;

        SDL3_Fill_Rect(px - 1.0f, py, pw + 2.0f, ph, borderColor);
        for (int i = 0; i < NUM_MENU_ENTRIES; i++) {
            float ey = py + 1.0f + i * (eh + 1.0f);
            Draw_Widget_Button(px, ey, pw, eh, (menu_hover_item == i), false, menu_labels[i]);
        }
    }

    /* 2. ABOUT Window if open */
    if (about_open) {
        Render_About_Window();
    }

    /* 3. KEYS / MOTD Scrollable Viewer if open */
    if (active_viewer.active) {
        Render_Viewer_Window();
    }

    /* 4. Talk / Message Window if open */
    if (clData.talking) {
        SDL3_Render_Talk_Window();
    }

    /* 5. Team selection dialog */
    SDL3_TeamSelect_Render();
}

/* =========================================================================
 * UI Mouse Event Handling
 * ========================================================================= */
static bool Point_In_Rect(int x, int y, SDL_FRect r)
{
    return (x >= (int)r.x && x < (int)(r.x + r.w) &&
            y >= (int)r.y && y < (int)(r.y + r.h));
}

static void Trigger_Menu_Entry(int item)
{
    switch (item) {
    case 0: /* KEYS */
        Open_Keys_Viewer();
        break;
    case 1: /* CONFIG */
        Config(true, CONFIG_DEFAULT);
        break;
    case 2: /* COLORS */
        Config(true, CONFIG_COLORS);
        break;
    case 3: /* SCORE */
        Config(false, CONFIG_NONE);
        if (showUserName) {
            showUserName = false;
            scoresChanged = true;
        }
        break;
    case 4: /* PLAYER */
        Config(false, CONFIG_NONE);
        if (!showUserName) {
            showUserName = true;
            scoresChanged = true;
        }
        break;
    case 5: /* MOTD */
        Open_Motd_Viewer();
        break;
    default:
        break;
    }
}

static void Handle_Viewer_Slider_Click(int mx, int my, bool is_vert, bool is_motion)
{
    int text_w = 0, text_h = 0;
    SDL_FRect hori_r, vert_r;
    Viewer_Get_Layout(NULL, NULL, NULL, NULL, &text_w, &text_h, &hori_r, &vert_r, NULL, NULL);

    if (is_vert) {
        float unit = vert_r.w;
        float block_offset = unit;
        float block_max_size = vert_r.h - 2.0f * block_offset;
        if (block_max_size <= 0.0f || active_viewer.real_height <= 0) return;

        float block_size = block_max_size * (float)text_h / (float)active_viewer.real_height;
        float block_start = block_offset + block_max_size * (float)active_viewer.visible_y / (float)active_viewer.real_height;
        float rel_y = (float)my - vert_r.y;
        float block_new_start = block_start;

        if (!is_motion && rel_y < unit)
            block_new_start = block_start - block_size;
        else if (!is_motion && rel_y >= block_offset + block_max_size)
            block_new_start = block_start + block_size;
        else
            block_new_start = rel_y - block_size * 0.5f;

        if (block_new_start < block_offset)
            block_new_start = block_offset;
        if (block_new_start + block_size > block_offset + block_max_size)
            block_new_start = block_offset + block_max_size - block_size;

        active_viewer.visible_y = (int)(active_viewer.real_height * (block_new_start - block_offset) / block_max_size);
    } else {
        float unit = hori_r.h;
        float block_offset = unit;
        float block_max_size = hori_r.w - 2.0f * block_offset;
        if (block_max_size <= 0.0f || active_viewer.real_width <= 0) return;

        float block_size = block_max_size * (float)text_w / (float)active_viewer.real_width;
        float block_start = block_offset + block_max_size * (float)active_viewer.visible_x / (float)active_viewer.real_width;
        float rel_x = (float)mx - hori_r.x;
        float block_new_start = block_start;

        if (!is_motion && rel_x < unit)
            block_new_start = block_start - block_size;
        else if (!is_motion && rel_x >= block_offset + block_max_size)
            block_new_start = block_start + block_size;
        else
            block_new_start = rel_x - block_size * 0.5f;

        if (block_new_start < block_offset)
            block_new_start = block_offset;
        if (block_new_start + block_size > block_offset + block_max_size)
            block_new_start = block_offset + block_max_size - block_size;

        active_viewer.visible_x = (int)(active_viewer.real_width * (block_new_start - block_offset) / block_max_size);
    }
}

bool SDL3_UI_Handle_Mouse_Down(int x, int y, int button)
{
    if (button != SDL_BUTTON_LEFT)
        return false;

    /* 1. If MENU pulldown is open, check if clicked inside pulldown */
    if (menu_open) {
        float px = 170.0f;
        float py = (float)(BTN_ROW_Y + BTN_ROW_H);
        float pw = 86.0f;
        float eh = (float)BTN_ROW_H;
        float ph = NUM_MENU_ENTRIES * (eh + 1.0f) + 1.0f;

        if (x >= (int)px && x < (int)(px + pw) && y >= (int)py && y < (int)(py + ph)) {
            int idx = (int)((y - (int)py - 1) / ((int)eh + 1));
            if (idx >= 0 && idx < NUM_MENU_ENTRIES) {
                menu_hover_item = idx;
                menu_click_hold = true;
                return true;
            }
        }
        /* If clicked on the MENU button itself while open, close it */
        if (x >= 170 && x < SIDEBAR_WIDTH && y >= BTN_ROW_Y && y < BTN_ROW_Y + BTN_ROW_H) {
            menu_open = false;
            menu_click_hold = false;
            menu_hover_item = -1;
            return true;
        }
        /* Clicked outside pulldown: close pulldown and continue processing click */
        menu_open = false;
        menu_click_hold = false;
        menu_hover_item = -1;
    }

    /* 2. Check Viewer Window (KEYS / MOTD) if active */
    if (active_viewer.active) {
        int vx, vy, vw, vh;
        SDL_FRect hori_r, vert_r, save_r, close_r;
        Viewer_Get_Layout(&vx, &vy, &vw, &vh, NULL, NULL, &hori_r, &vert_r, &save_r, &close_r);

        if (x >= vx - 2 && x <= vx + vw + 2 && y >= vy - 2 && y <= vy + vh + 2) {
            if (Point_In_Rect(x, y, save_r)) {
                active_viewer.btn_pressed = 0;
                active_viewer.btn_hover = 0;
            } else if (Point_In_Rect(x, y, close_r)) {
                active_viewer.btn_pressed = 1;
                active_viewer.btn_hover = 1;
            } else if (Point_In_Rect(x, y, vert_r)) {
                active_viewer.dragging_vert = true;
                Handle_Viewer_Slider_Click(x, y, true, false);
            } else if (Point_In_Rect(x, y, hori_r)) {
                active_viewer.dragging_hori = true;
                Handle_Viewer_Slider_Click(x, y, false, false);
            }
            return true;
        }
    }

    /* 3. Check ABOUT Window if open */
    if (about_open) {
        int ax, ay, aw, ah;
        Get_About_Rect(&ax, &ay, &aw, &ah);
        if (x >= ax - 2 && x <= ax + aw + 2 && y >= ay - 2 && y <= ay + ah + 2) {
            SDL_FRect close_r, prev_r, next_r;
            Get_About_Button_Rects(ax, ay, aw, ah, &close_r, &prev_r, &next_r);
            if (Point_In_Rect(x, y, close_r)) {
                about_btn_pressed = 0;
                about_btn_hover = 0;
            } else if (Point_In_Rect(x, y, prev_r)) {
                about_btn_pressed = 1;
                about_btn_hover = 1;
            } else if (Point_In_Rect(x, y, next_r)) {
                about_btn_pressed = 2;
                about_btn_hover = 2;
            }
            return true;
        }
    }

    /* 4. Check Sidebar (x < SIDEBAR_WIDTH) */
    if (show_sidebar && x < SIDEBAR_WIDTH) {
        /* Top 3 buttons: QUIT, ABOUT, MENU */
        if (y >= BTN_ROW_Y && y < BTN_ROW_Y + BTN_ROW_H) {
            if (x < 84) {
                top_btn_pressed = 0;
                top_btn_hover = 0;
            } else if (x >= 85 && x < 169) {
                top_btn_pressed = 1;
                top_btn_hover = 1;
            } else if (x >= 170) {
                top_btn_pressed = 2;
                top_btn_hover = 2;
                menu_open = true;
                menu_click_hold = true;
                menu_hover_item = -1;
            }
            return true;
        }

        /* CONFIG / COLORS panel if mapped */
        if (config_mapped && y >= SCORE_Y_OFFSET) {
            SDL_FRect close_r, prev_r, next_r;
            Get_Config_Nav_Rects(&close_r, &prev_r, &next_r);
            if (Point_In_Rect(x, y, close_r)) {
                cfg_nav_pressed = 0;
                cfg_nav_hover = 0;
                return true;
            }
            if (Point_In_Rect(x, y, prev_r)) {
                cfg_nav_pressed = 1;
                cfg_nav_hover = 1;
                return true;
            }
            if (Point_In_Rect(x, y, next_r)) {
                cfg_nav_pressed = 2;
                cfg_nav_hover = 2;
                return true;
            }

            for (int i = 0; i < num_cfg_entries; i++) {
                cfg_entry_t *e = &cfg_entries[i];
                if (e->page != config_page) continue;

                if (e->opt == NULL || Option_get_type(e->opt) == xp_bool_option) {
                    if (Point_In_Rect(x, y, e->val_r)) {
                        cfg_pressed_entry = i;
                        cfg_pressed_part = 1;
                        return true;
                    }
                } else {
                    if (Point_In_Rect(x, y, e->left_r)) {
                        cfg_pressed_entry = i;
                        cfg_pressed_part = 2;
                        return true;
                    }
                    if (Point_In_Rect(x, y, e->right_r)) {
                        cfg_pressed_entry = i;
                        cfg_pressed_part = 3;
                        return true;
                    }
                }
            }
            return true;
        }

        return true;
    }

    return false;
}

bool SDL3_UI_Handle_Mouse_Motion(int x, int y)
{
    bool handled = false;

    /* 1. MENU pulldown hover */
    if (menu_open) {
        float px = 170.0f;
        float py = (float)(BTN_ROW_Y + BTN_ROW_H);
        float pw = 86.0f;
        float eh = (float)BTN_ROW_H;
        float ph = NUM_MENU_ENTRIES * (eh + 1.0f) + 1.0f;

        if (x >= (int)px && x < (int)(px + pw) && y >= (int)py && y < (int)(py + ph)) {
            int idx = (int)((y - (int)py - 1) / ((int)eh + 1));
            menu_hover_item = (idx >= 0 && idx < NUM_MENU_ENTRIES) ? idx : -1;
        } else {
            menu_hover_item = -1;
        }
        handled = true;
    }

    /* 2. Top buttons hover while pressed */
    if (top_btn_pressed != -1) {
        top_btn_hover = -1;
        if (y >= BTN_ROW_Y && y < BTN_ROW_Y + BTN_ROW_H) {
            if (top_btn_pressed == 0 && x >= 0 && x < 84) top_btn_hover = 0;
            else if (top_btn_pressed == 1 && x >= 85 && x < 169) top_btn_hover = 1;
            else if (top_btn_pressed == 2 && x >= 170 && x < SIDEBAR_WIDTH) top_btn_hover = 2;
        }
        handled = true;
    }

    /* 3. Viewer sliders & buttons */
    if (active_viewer.active) {
        if (active_viewer.dragging_vert) {
            Handle_Viewer_Slider_Click(x, y, true, true);
            return true;
        }
        if (active_viewer.dragging_hori) {
            Handle_Viewer_Slider_Click(x, y, false, true);
            return true;
        }
        if (active_viewer.btn_pressed != -1) {
            SDL_FRect save_r, close_r;
            Viewer_Get_Layout(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &save_r, &close_r);
            active_viewer.btn_hover = -1;
            if (active_viewer.btn_pressed == 0 && Point_In_Rect(x, y, save_r))
                active_viewer.btn_hover = 0;
            else if (active_viewer.btn_pressed == 1 && Point_In_Rect(x, y, close_r))
                active_viewer.btn_hover = 1;
            handled = true;
        }
    }

    /* 4. ABOUT buttons hover while pressed */
    if (about_open && about_btn_pressed != -1) {
        int ax, ay, aw, ah;
        Get_About_Rect(&ax, &ay, &aw, &ah);
        SDL_FRect close_r, prev_r, next_r;
        Get_About_Button_Rects(ax, ay, aw, ah, &close_r, &prev_r, &next_r);
        about_btn_hover = -1;
        if (about_btn_pressed == 0 && Point_In_Rect(x, y, close_r)) about_btn_hover = 0;
        else if (about_btn_pressed == 1 && Point_In_Rect(x, y, prev_r)) about_btn_hover = 1;
        else if (about_btn_pressed == 2 && Point_In_Rect(x, y, next_r)) about_btn_hover = 2;
        handled = true;
    }

    /* 5. CONFIG nav buttons hover while pressed */
    if (config_mapped && cfg_nav_pressed != -1) {
        SDL_FRect close_r, prev_r, next_r;
        Get_Config_Nav_Rects(&close_r, &prev_r, &next_r);
        cfg_nav_hover = -1;
        if (cfg_nav_pressed == 0 && Point_In_Rect(x, y, close_r)) cfg_nav_hover = 0;
        else if (cfg_nav_pressed == 1 && Point_In_Rect(x, y, prev_r)) cfg_nav_hover = 1;
        else if (cfg_nav_pressed == 2 && Point_In_Rect(x, y, next_r)) cfg_nav_hover = 2;
        handled = true;
    }

    return handled;
}

bool SDL3_UI_Handle_Mouse_Up(int x, int y, int button)
{
    if (button != SDL_BUTTON_LEFT)
        return false;

    bool handled = false;

    /* 1. MENU pulldown release */
    if (menu_open && menu_click_hold) {
        menu_click_hold = false;
        top_btn_pressed = -1;
        top_btn_hover = -1;
        if (menu_hover_item >= 0 && menu_hover_item < NUM_MENU_ENTRIES) {
            int chosen = menu_hover_item;
            menu_open = false;
            menu_hover_item = -1;
            Trigger_Menu_Entry(chosen);
            return true;
        }
        /* If released outside the MENU button, close the menu; if released on the MENU button, keep it open for click-click UX */
        if (!(x >= 170 && x < SIDEBAR_WIDTH && y >= BTN_ROW_Y && y < BTN_ROW_Y + BTN_ROW_H)) {
            menu_open = false;
            menu_hover_item = -1;
        }
        return true;
    }

    /* 2. Top buttons release (QUIT, ABOUT) */
    if (top_btn_pressed != -1) {
        int btn = top_btn_pressed;
        int hov = top_btn_hover;
        top_btn_pressed = -1;
        top_btn_hover = -1;
        if (btn == hov) {
            if (btn == 0) {
                tng_running = false;
            } else if (btn == 1) {
                about_open = true;
            }
        }
        return true;
    }

    /* 3. Viewer release */
    if (active_viewer.active) {
        if (active_viewer.dragging_vert || active_viewer.dragging_hori) {
            active_viewer.dragging_vert = false;
            active_viewer.dragging_hori = false;
            return true;
        }
        if (active_viewer.btn_pressed != -1) {
            int btn = active_viewer.btn_pressed;
            int hov = active_viewer.btn_hover;
            active_viewer.btn_pressed = -1;
            active_viewer.btn_hover = -1;
            if (btn == hov) {
                if (btn == 0) {
                    /* SAVE (matching Widget_viewer_save_callback) */
                    FILE *fp = fopen(active_viewer.save_filename, "w");
                    if (fp && active_viewer.buf && active_viewer.len > 0) {
                        fwrite(active_viewer.buf, 1, (size_t)active_viewer.len, fp);
                        fclose(fp);
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Saved to %s", active_viewer.save_filename);
                        Add_HUD_message(msg);
                    }
                } else if (btn == 1) {
                    Viewer_Close();
                }
            }
            return true;
        }
    }

    /* 4. ABOUT release (matching About(Window w) in x11/about.c) */
    if (about_open && about_btn_pressed != -1) {
        int btn = about_btn_pressed;
        int hov = about_btn_hover;
        about_btn_pressed = -1;
        about_btn_hover = -1;
        if (btn == hov) {
            if (btn == 0) {
                about_page = 0;
                about_open = false;
            } else if (btn == 1) {
                /* PREV */
                about_page--;
                if (about_page == 4 && itemsplit >= NUM_ITEMS - 1)
                    about_page--;
                if (about_page <= -1) {
                    about_page = NUM_ABOUT_PAGES - 1;
                    if (about_page == 4 && itemsplit >= NUM_ITEMS - 1)
                        about_page--;
                }
            } else if (btn == 2) {
                /* NEXT */
                about_page++;
                if (about_page == 4 && itemsplit >= NUM_ITEMS - 1)
                    about_page++;
                if (about_page >= NUM_ABOUT_PAGES)
                    about_page = 0;
            }
        }
        return true;
    }

    /* 5. CONFIG / COLORS release */
    if (config_mapped) {
        if (cfg_nav_pressed != -1) {
            int btn = cfg_nav_pressed;
            int hov = cfg_nav_hover;
            cfg_nav_pressed = -1;
            cfg_nav_hover = -1;
            if (btn == hov) {
                if (btn == 0) {
                    Config(false, CONFIG_NONE);
                } else if (btn == 1 && config_max_pages > 1) {
                    config_page = (config_page - 1 + config_max_pages) % config_max_pages;
                } else if (btn == 2 && config_max_pages > 1) {
                    config_page = (config_page + 1) % config_max_pages;
                }
            }
            return true;
        }
        if (cfg_pressed_entry != -1) {
            int idx = cfg_pressed_entry;
            int part = cfg_pressed_part;
            cfg_pressed_entry = -1;
            cfg_pressed_part = 0;
            cfg_entry_t *e = &cfg_entries[idx];
            if (e->page == config_page) {
                if ((part == 1 && Point_In_Rect(x, y, e->val_r)) ||
                    (part == 2 && Point_In_Rect(x, y, e->left_r)) ||
                    (part == 3 && Point_In_Rect(x, y, e->right_r))) {
                    Execute_Config_Entry_Action(idx, part);
                }
            }
            return true;
        }
    }

    return handled;
}

bool SDL3_UI_Handle_Mouse_Wheel(int x, int y, float wheel_y)
{
    if (active_viewer.active) {
        int vx, vy, vw, vh, text_w, text_h;
        Viewer_Get_Layout(&vx, &vy, &vw, &vh, &text_w, &text_h, NULL, NULL, NULL, NULL);
        if (x >= vx && x <= vx + vw && y >= vy && y <= vy + vh) {
            int step = 3 * LINE_HEIGHT;
            active_viewer.visible_y -= (int)(wheel_y * step);
            int max_y = active_viewer.real_height - text_h;
            if (max_y < 0) max_y = 0;
            if (active_viewer.visible_y < 0) active_viewer.visible_y = 0;
            if (active_viewer.visible_y > max_y) active_viewer.visible_y = max_y;
            return true;
        }
    }
    if (show_sidebar && config_mapped && x < SIDEBAR_WIDTH && y >= SCORE_Y_OFFSET && config_max_pages > 1) {
        if (wheel_y < 0) {
            config_page = (config_page + 1) % config_max_pages;
        } else if (wheel_y > 0) {
            config_page = (config_page - 1 + config_max_pages) % config_max_pages;
        }
        return true;
    }
    return false;
}

static xp_option_t xpaint_options[] = {
    COLOR_INDEX_OPTION("clockColor", 1, &clockColor, "Which color number to use for drawing the clock.\n"),
    COLOR_INDEX_OPTION("scoreColor", 1, &scoreColor, "Which color number to use for drawing score list entries.\n"),
    COLOR_INDEX_OPTION("scoreSelfColor", 3, &scoreSelfColor, "Which color number to use for drawing your own score.\n"),
    COLOR_INDEX_OPTION("scoreInactiveColor", 12, &scoreInactiveColor, "Which color number to use for drawing inactive players's scores.\n"),
    COLOR_INDEX_OPTION("scoreInactiveSelfColor", 12, &scoreInactiveSelfColor, "Which color number to use for drawing your score when inactive.\n"),
    COLOR_INDEX_OPTION("scoreOwnTeamColor", 4, &scoreOwnTeamColor, "Which color number to use for drawing your own team score.\n"),
    COLOR_INDEX_OPTION("scoreEnemyTeamColor", 11, &scoreEnemyTeamColor, "Which color number to use for drawing enemy team score.\n"),
};

void Store_xpaint_options(void)
{
    STORE_OPTIONS(xpaint_options);
}
