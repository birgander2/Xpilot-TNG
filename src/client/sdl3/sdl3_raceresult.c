/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Race result popup: own times, the standings of the race and the server's
 * top ten lists (whole race, single lap), shown when a race ends.
 */

#include "sdl3_client.h"
#include "netclient.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RR_WIDTH        660
#define RR_PAD          16
#define RR_ROW_H        18
#define RR_TITLE_H      34
#define RR_FOOTER_H     26
#define RR_MAX_SHOWN    12      /* standings rows shown at most */
#define RR_SHOW_SECS    30      /* closes by itself after this */

static bool rr_active = false;
static unsigned rr_seen = 0;    /* serial of the last result shown */
static Uint64 rr_opened;

/* m:ss.cc, or ss.cc below a minute */
static void Rr_format(int ms, char *buf, size_t size)
{
    int cs = (ms + 5) / 10;

    if (ms <= 0)
        snprintf(buf, size, "-");
    else if (cs >= 6000)
        snprintf(buf, size, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    else
        snprintf(buf, size, "%d.%02d", cs / 100, cs % 100);
}

static int Rr_shown(void)
{
    return race_result.nstand < RR_MAX_SHOWN ? race_result.nstand : RR_MAX_SHOWN;
}

static void Rr_layout(float *x, float *y, float *w, float *h)
{
    int top = race_result.ntotal > race_result.nlap ? race_result.ntotal : race_result.nlap;

    if (top < 1)
        top = 1;
    *w = RR_WIDTH;
    *h = RR_TITLE_H + 2 * RR_ROW_H            /* your result */
        + RR_ROW_H * (2 + Rr_shown())          /* standings with heading */
        + RR_ROW_H * (2 + top)                 /* top lists with heading */
        + RR_FOOTER_H;
    *x = (float)PLAYFIELD_X + ((float)draw_width - *w) / 2.0f;
    *y = ((float)draw_height - *h) / 2.0f;
    if (*x < 0.0f) *x = 0.0f;
    if (*y < 0.0f) *y = 0.0f;
}

/* Close it and let the server know: the next race waits for everybody. */
static void Rr_close(void)
{
    rr_active = false;
    Net_highscore_done();
}

bool SDL3_RaceResult_Active(void)
{
    return rr_active;
}

/* Esc, Enter and Space close it; all other keys go on to the game. */
bool SDL3_RaceResult_Handle_Key_Down(SDL_Keycode key)
{
    if (!rr_active)
        return false;
    if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER
        || key == SDLK_SPACE) {
        Rr_close();
        return true;
    }
    return false;
}

/* A click into the popup closes it, clicks elsewhere go on to the game. */
bool SDL3_RaceResult_Handle_Mouse_Down(int mx, int my, int button)
{
    float x, y, w, h;

    (void)button;
    if (!rr_active)
        return false;
    Rr_layout(&x, &y, &w, &h);
    if (mx < x || mx >= x + w || my < y || my >= y + h)
        return false;
    Rr_close();
    return true;
}

/* One top list in a column: rank, name, time, date. */
static void Rr_draw_top(const char *title, const race_result_line_t *l, int n,
                        float x, float y, float w)
{
    char buf[64], date[16];

    SDL3_Draw_Text(title, x, y, XP_COLOR_GOLD);
    y += RR_ROW_H;
    if (n == 0) {
        SDL3_Draw_Text("no times yet", x, y, XP_COLOR_GRAY);
        return;
    }
    for (int i = 0; i < n; i++, y += RR_ROW_H) {
        time_t when = (time_t)l[i].other;
        struct tm *tm = localtime(&when);
        int color = (l[i].flags & HIGHSCORE_F_YOU) ? XP_COLOR_YELLOW : XP_COLOR_WHITE;

        if (l[i].flags & HIGHSCORE_F_NEW)
            color = XP_COLOR_GREEN;
        snprintf(buf, sizeof(buf), "%2d.%s", l[i].rank,
                 (l[i].flags & HIGHSCORE_F_NEW) ? "*" : " ");
        SDL3_Draw_Text(buf, x, y, color);
        SDL3_Draw_Text_Len(l[i].name, (int)strnlen(l[i].name, 14), x + 34.0f, y, color);
        Rr_format(l[i].time_ms, buf, sizeof(buf));
        SDL3_Draw_Text(buf, x + w - 74.0f - SDL3_Text_Width(buf), y, color);
        if (tm != NULL && strftime(date, sizeof(date), "%d.%m.%y", tm) > 0)
            SDL3_Draw_Text(date, x + w - SDL3_Text_Width(date), y, XP_COLOR_GRAY);
    }
}

void SDL3_RaceResult_Render(void)
{
    const race_result_t *r = &race_result;
    char buf[128], t1[24], t2[24];
    float x, y, w, h, cy, col;

    if (!tng_renderer)
        return;
    /* a new result: open */
    if (r->serial != rr_seen) {
        rr_seen = r->serial;
        rr_active = true;
        rr_opened = SDL_GetTicks();
    }
    if (!rr_active)
        return;
    if (SDL_GetTicks() - rr_opened > RR_SHOW_SECS * 1000) {
        Rr_close();
        return;
    }

    Rr_layout(&x, &y, &w, &h);
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL3_Fill_Rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, borderColor);
    SDL3_Fill_Rect(x, y, w, h, windowColor);

    snprintf(buf, sizeof(buf), "Race finished: %.60s, %d lap%s", r->map, r->laps,
             r->laps == 1 ? "" : "s");
    SDL3_Draw_Text(buf, x + RR_PAD, y + 12.0f, XP_COLOR_WHITE);
    cy = y + RR_TITLE_H;

    /* your result */
    {
        const race_result_line_t *me = NULL;

        for (int i = 0; i < r->nstand; i++) {
            if (r->stand[i].flags & HIGHSCORE_F_YOU)
                me = &r->stand[i];
        }
        if (me == NULL)
            snprintf(buf, sizeof(buf), "You did not race.");
        else if (me->flags & HIGHSCORE_F_FINISHED) {
            Rr_format(me->time_ms, t1, sizeof(t1));
            Rr_format(me->other, t2, sizeof(t2));
            snprintf(buf, sizeof(buf), "You finished %d. in %s   best lap %s",
                     me->rank, t1, t2);
        } else {
            Rr_format(me->other, t2, sizeof(t2));
            snprintf(buf, sizeof(buf), "You did not finish (lap %d)   best lap %s",
                     me->lap, t2);
        }
        SDL3_Draw_Text(buf, x + RR_PAD, cy, XP_COLOR_YELLOW);
        cy += 2 * RR_ROW_H;
    }

    /* standings of this race */
    SDL3_Draw_Text("Pos  Name", x + RR_PAD, cy, XP_COLOR_GOLD);
    SDL3_Draw_Text("Race", x + w - RR_PAD - 230.0f, cy, XP_COLOR_GOLD);
    SDL3_Draw_Text("Best lap", x + w - RR_PAD - 130.0f, cy, XP_COLOR_GOLD);
    cy += RR_ROW_H;
    for (int i = 0; i < Rr_shown(); i++, cy += RR_ROW_H) {
        const race_result_line_t *l = &r->stand[i];
        int color = (l->flags & HIGHSCORE_F_YOU) ? XP_COLOR_YELLOW
            : (l->flags & HIGHSCORE_F_ROBOT) ? XP_COLOR_LIGHT_GRAY : XP_COLOR_WHITE;

        if (l->flags & HIGHSCORE_F_FINISHED)
            snprintf(buf, sizeof(buf), "%2d.", l->rank);
        else
            snprintf(buf, sizeof(buf), "  -");
        SDL3_Draw_Text(buf, x + RR_PAD, cy, color);
        snprintf(buf, sizeof(buf), "%.24s%s", l->name,
                 (l->flags & HIGHSCORE_F_ROBOT) ? " (bot)" : "");
        SDL3_Draw_Text(buf, x + RR_PAD + 40.0f, cy, color);
        if (l->flags & HIGHSCORE_F_FINISHED)
            Rr_format(l->time_ms, t1, sizeof(t1));
        else
            snprintf(t1, sizeof(t1), "lap %d", l->lap);
        SDL3_Draw_Text(t1, x + w - RR_PAD - 230.0f, cy, color);
        Rr_format(l->other, t2, sizeof(t2));
        SDL3_Draw_Text(t2, x + w - RR_PAD - 130.0f, cy, color);
    }
    cy += RR_ROW_H;

    /* top lists side by side */
    col = (w - 3 * RR_PAD) / 2.0f;
    Rr_draw_top("Top 10 races", r->total, r->ntotal, x + RR_PAD, cy, col);
    Rr_draw_top("Top 10 laps", r->lap, r->nlap, x + 2 * RR_PAD + col, cy, col);

    SDL3_Draw_Text("* new record   Esc, Enter or a click closes", x + RR_PAD,
                   y + h - RR_FOOTER_H + 6.0f, XP_COLOR_GREEN);
}
