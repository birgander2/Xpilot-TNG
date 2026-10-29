/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Ballrun records popup: the server's top ten ballruns, shown at the end of
 * a ball game round in which a new record was set.
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

static bool br_active = false;
static unsigned br_seen = 0;    /* serial of the last list shown */
static Uint64 br_opened;

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

static void Br_layout(float *x, float *y, float *w, float *h)
{
    int n = ball_result.ntop > 0 ? ball_result.ntop : 1;

    *w = BR_WIDTH;
    *h = BR_TITLE_H + BR_ROW_H * (1 + n) + BR_FOOTER_H;
    *x = (float)PLAYFIELD_X + ((float)draw_width - *w) / 2.0f;
    *y = ((float)draw_height - *h) / 2.0f;
    if (*x < 0.0f) *x = 0.0f;
    if (*y < 0.0f) *y = 0.0f;
}

/* Close it and let the server know: the next round waits for everybody. */
static void Br_close(void)
{
    br_active = false;
    Net_highscore_done();
}

/* Esc, Enter and Space close it; all other keys go on to the game. */
bool SDL3_BallResult_Handle_Key_Down(SDL_Keycode key)
{
    if (!br_active)
        return false;
    if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER
        || key == SDLK_SPACE) {
        Br_close();
        return true;
    }
    return false;
}

/* A click into the popup closes it, clicks elsewhere go on to the game. */
bool SDL3_BallResult_Handle_Mouse_Down(int mx, int my, int button)
{
    float x, y, w, h;

    (void)button;
    if (!br_active)
        return false;
    Br_layout(&x, &y, &w, &h);
    if (mx < x || mx >= x + w || my < y || my >= y + h)
        return false;
    Br_close();
    return true;
}

void SDL3_BallResult_Render(void)
{
    const ball_result_t *r = &ball_result;
    char buf[128], date[16];
    float x, y, w, h, cy;

    if (!tng_renderer)
        return;
    /* a new list: open */
    if (r->serial != br_seen) {
        br_seen = r->serial;
        br_active = true;
        br_opened = SDL_GetTicks();
    }
    if (!br_active)
        return;
    if (SDL_GetTicks() - br_opened > BR_SHOW_SECS * 1000) {
        Br_close();
        return;
    }

    Br_layout(&x, &y, &w, &h);
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL3_Fill_Rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, borderColor);
    SDL3_Fill_Rect(x, y, w, h, windowColor);

    snprintf(buf, sizeof(buf), "New ballrun record: %.40s", r->map);
    SDL3_Draw_Text(buf, x + BR_PAD, y + 12.0f, XP_COLOR_WHITE);
    cy = y + BR_TITLE_H;

    SDL3_Draw_Text("Top 10 ballruns", x + BR_PAD, cy, XP_COLOR_GOLD);
    cy += BR_ROW_H;
    if (r->ntop == 0)
        SDL3_Draw_Text("no times yet", x + BR_PAD, cy, XP_COLOR_GRAY);
    for (int i = 0; i < r->ntop; i++, cy += BR_ROW_H) {
        const race_result_line_t *l = &r->top[i];
        time_t when = (time_t)l->other;
        struct tm *tm = localtime(&when);
        int color = (l->flags & HIGHSCORE_F_YOU) ? XP_COLOR_YELLOW : XP_COLOR_WHITE;

        if (l->flags & HIGHSCORE_F_NEW)
            color = XP_COLOR_GREEN;
        snprintf(buf, sizeof(buf), "%2d.%s", l->rank,
                 (l->flags & HIGHSCORE_F_NEW) ? "*" : " ");
        SDL3_Draw_Text(buf, x + BR_PAD, cy, color);
        SDL3_Draw_Text_Len(l->name, (int)strnlen(l->name, 20), x + BR_PAD + 34.0f, cy, color);
        Br_format(l->time_ms, buf, sizeof(buf));
        SDL3_Draw_Text(buf, x + w - BR_PAD - 84.0f - SDL3_Text_Width(buf), cy, color);
        if (tm != NULL && strftime(date, sizeof(date), "%d.%m.%y", tm) > 0)
            SDL3_Draw_Text(date, x + w - BR_PAD - SDL3_Text_Width(date), cy, XP_COLOR_GRAY);
    }

    SDL3_Draw_Text("* new record   Esc, Enter or a click closes", x + BR_PAD,
                   y + h - BR_FOOTER_H + 6.0f, XP_COLOR_GREEN);
}
