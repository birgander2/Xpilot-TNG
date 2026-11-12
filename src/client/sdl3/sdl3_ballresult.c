/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Ballrun records popup: the server's top ten ballruns, shown at the end of
 * a ball game round in which a new record was set.  The deathmatch highscores
 * (top ten scores) use the same popup, on joining, pausing and new entries.
 */

#include "sdl3_client.h"
#include "netclient.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BR_WIDTH        420
#define BR_PAD          16
#define BR_ROW_H        18
#define BR_TITLE_H      34
#define BR_FOOTER_H     26
#define BR_SHOW_SECS    30      /* closes by itself after this */
#define BR_JOIN_SECS    10      /* deathmatch list shown on joining */

/* Two popups: the ballrun records and the deathmatch highscores. */
typedef struct {
    const ball_result_t *res;
    bool        dm;
    bool        active;
    unsigned    seen;           /* serial of the last list shown */
    Uint64      opened;
} br_popup_t;

static br_popup_t br_popups[2] = {
    { &ball_result, false, false, 0, 0 },
    { &dm_result,   true,  false, 0, 0 },
};

/* m:ss.cc, or ss.cc below a minute */
static void Br_format(int ms, char *buf, size_t size)
{
    int cs = (ms + 5) / 10;

    if (ms <= 0)
        snprintf(buf, size, "-");
    else if (cs >= 6000)
        snprintf(buf, size, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    else
        snprintf(buf, size, "%d.%02d", cs / 100, cs % 100);
}

static void Br_layout(const br_popup_t *p, float *x, float *y, float *w, float *h)
{
    int n = p->res->ntop > 0 ? p->res->ntop : 1;

    *w = BR_WIDTH;
    *h = BR_TITLE_H + BR_ROW_H * (1 + n) + BR_FOOTER_H;
    *x = (float)PLAYFIELD_X + ((float)draw_width - *w) / 2.0f;
    *y = ((float)draw_height - *h) / 2.0f;
    if (*x < 0.0f) *x = 0.0f;
    if (*y < 0.0f) *y = 0.0f;
}

/* Close it and let the server know: the next round waits for everybody. */
static void Br_close(br_popup_t *p)
{
    p->active = false;
    if (!p->dm)
        Net_highscore_done();
}

/* Esc, Enter and Space close it; all other keys go on to the game. */
bool SDL3_BallResult_Handle_Key_Down(SDL_Keycode key)
{
    bool handled = false;

    if (key != SDLK_ESCAPE && key != SDLK_RETURN && key != SDLK_KP_ENTER
        && key != SDLK_SPACE)
        return false;
    for (int i = 0; i < 2; i++) {
        if (br_popups[i].active) {
            Br_close(&br_popups[i]);
            handled = true;
        }
    }
    return handled;
}

/* A click into a popup closes it, clicks elsewhere go on to the game. */
bool SDL3_BallResult_Handle_Mouse_Down(int mx, int my, int button)
{
    (void)button;
    for (int i = 0; i < 2; i++) {
        float x, y, w, h;

        if (!br_popups[i].active)
            continue;
        Br_layout(&br_popups[i], &x, &y, &w, &h);
        if (mx < x || mx >= x + w || my < y || my >= y + h)
            continue;
        Br_close(&br_popups[i]);
        return true;
    }
    return false;
}

static void Br_render(br_popup_t *p)
{
    const ball_result_t *r = p->res;
    char buf[128], date[16];
    float x, y, w, h, cy;

    /* a new list: open */
    if (r->serial != p->seen) {
        p->seen = r->serial;
        p->active = true;
        p->opened = SDL_GetTicks();
        if (p->dm) {            /* only one popup at a time */
            br_popups[0].active = false;
        } else {
            br_popups[1].active = false;
        }
    }
    if (!p->active)
        return;
    if (SDL_GetTicks() - p->opened >
        (Uint64)((p->dm && r->reason == HIGHSCORE_DM_JOIN ? BR_JOIN_SECS : BR_SHOW_SECS) * 1000)) {
        Br_close(p);
        return;
    }

    Br_layout(p, &x, &y, &w, &h);
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL3_Fill_Rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, borderColor);
    SDL3_Fill_Rect(x, y, w, h, windowColor);

    if (p->dm)
        snprintf(buf, sizeof(buf), "%s: %.40s",
                 r->reason == HIGHSCORE_DM_RECORD ? "New highscore" : "Highscores", r->map);
    else
        snprintf(buf, sizeof(buf), "New ballrun record: %.40s", r->map);
    SDL3_Draw_Text(buf, x + BR_PAD, y + 12.0f, XP_COLOR_WHITE);
    cy = y + BR_TITLE_H;

    SDL3_Draw_Text(p->dm ? "Top 10 scores" : "Top 10 ballruns", x + BR_PAD, cy, XP_COLOR_GOLD);
    cy += BR_ROW_H;
    if (r->ntop == 0)
        SDL3_Draw_Text(p->dm ? "no scores yet" : "no times yet", x + BR_PAD, cy, XP_COLOR_GRAY);
    for (int i = 0; i < r->ntop; i++, cy += BR_ROW_H) {
        const race_result_line_t *l = &r->top[i];
        time_t when = (time_t)l->other;
        struct tm *tm = localtime(&when);
        int color = (l->flags & HIGHSCORE_F_YOU) ? XP_COLOR_YELLOW : XP_COLOR_WHITE;

        bool fresh = (l->flags & HIGHSCORE_F_NEW) != 0;
        int dcolor = XP_COLOR_GRAY;

        if (fresh) {            /* solid green bar, dark text */
            SDL3_Fill_Rect(x + 4.0f, cy - 2.0f, w - 8.0f, (float)BR_ROW_H, XP_COLOR_GREEN);
            color = dcolor = XP_COLOR_BLACK;
        }
        snprintf(buf, sizeof(buf), "%2d.%s", l->rank,
                 (l->flags & HIGHSCORE_F_NEW) ? "*" : " ");
        SDL3_Draw_Text(buf, x + BR_PAD, cy, color);
        SDL3_Draw_Text_Len(l->name, (int)strnlen(l->name, 20), x + BR_PAD + 34.0f, cy, color);
        if (p->dm)
            snprintf(buf, sizeof(buf), "%d.%d", l->time_ms / 10, l->time_ms % 10);
        else
            Br_format(l->time_ms, buf, sizeof(buf));
        SDL3_Draw_Text(buf, x + w - BR_PAD - 84.0f - SDL3_Text_Width(buf), cy, color);
        if (tm != NULL && strftime(date, sizeof(date), "%d.%m.%y", tm) > 0)
            SDL3_Draw_Text(date, x + w - BR_PAD - SDL3_Text_Width(date), cy, dcolor);
    }

    SDL3_Draw_Text(p->dm ? "* new highscore   Esc, Enter or a click closes"
                         : "* new record   Esc, Enter or a click closes",
                   x + BR_PAD, y + h - BR_FOOTER_H + 6.0f, XP_COLOR_GREEN);
}

void SDL3_BallResult_Render(void)
{
    if (!tng_renderer)
        return;
    Br_render(&br_popups[0]);
    Br_render(&br_popups[1]);
}
