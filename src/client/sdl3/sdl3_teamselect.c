/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Team selection dialog (shown on join and on "/team" without a number)
 */

#include "sdl3_client.h"
#include "netclient.h"
#include <stdio.h>
#include <string.h>

#define TS_WIDTH        360
#define TS_ROW_H        28
#define TS_PAD          16
#define TS_HEADER_H     40
#define TS_FOOTER_H     30

typedef struct {
    int team;
    int bases;
    int players;
    bool locked;
} ts_entry_t;

static bool ts_active = false;
static bool ts_auto_done = false;
static ts_entry_t ts_entries[MAX_TEAMS];
static int ts_num_entries = 0;

/* Collect teams that own at least one base, with base and player counts. */
static void TeamSelect_collect(void)
{
    int nbases[MAX_TEAMS] = { 0 }, nplayers[MAX_TEAMS] = { 0 };

    for (int i = 0; i < num_bases; i++) {
        if (bases[i].team < MAX_TEAMS)
            nbases[bases[i].team]++;
    }
    for (int i = 0; i < num_others; i++) {
        if (Others[i].team < MAX_TEAMS)
            nplayers[Others[i].team]++;
    }
    ts_num_entries = 0;
    for (int t = 0; t < MAX_TEAMS; t++) {
        if (nbases[t] > 0) {
            ts_entries[ts_num_entries].team = t;
            ts_entries[ts_num_entries].bases = nbases[t];
            ts_entries[ts_num_entries].players = nplayers[t];
            ts_entries[ts_num_entries].locked =
                BIT(Setup->mode, SETUP_ROBOT_TEAM_LOCKED)
                && t == ((Setup->mode & SETUP_ROBOT_TEAM_MASK)
                         >> SETUP_ROBOT_TEAM_SHIFT);
            ts_num_entries++;
        }
    }
}

static void TeamSelect_layout(float *x, float *y, float *w, float *h)
{
    *w = TS_WIDTH;
    *h = TS_HEADER_H + ts_num_entries * TS_ROW_H + TS_FOOTER_H;
    *x = (float)PLAYFIELD_X + ((float)draw_width - *w) / 2.0f;
    *y = ((float)draw_height - *h) / 2.0f;
    if (*y < 0.0f) *y = 0.0f;
}

/* Close the dialog and give mouse steering back if it was active before. */
static void TeamSelect_close(void)
{
    ts_active = false;
    if (clData.restorePointerControl) {
        Pointer_control_set_state(true);
        clData.restorePointerControl = false;
    }
}

static void TeamSelect_choose(int team)
{
    char cmd[32];

    TeamSelect_close();
    for (int i = 0; i < ts_num_entries; i++) {
        if (ts_entries[i].team == team && ts_entries[i].locked)
            return;
    }
    if (self != NULL && self->team == team)
        return;
    snprintf(cmd, sizeof(cmd), "/team %d", team);
    Net_talk(cmd);
}

void SDL3_TeamSelect_Open(void)
{
    if (!Setup || !BIT(Setup->mode, TEAM_PLAY)) {
        Add_message("No team play going on.");
        return;
    }
    TeamSelect_collect();
    if (ts_num_entries == 0)
        return;
    /* Show the mouse cursor, remember to restore mouse steering on close. */
    if (clData.pointerControl) {
        clData.restorePointerControl = true;
        Pointer_control_set_state(false);
    }
    SDL_ShowCursor();
    ts_active = true;
}

bool SDL3_TeamSelect_Active(void)
{
    return ts_active;
}

bool SDL3_TeamSelect_Handle_Key_Down(SDL_Keycode key)
{
    if (!ts_active)
        return false;

    if (key == SDLK_ESCAPE) {
        TeamSelect_close();
    } else if (key >= SDLK_0 && key <= SDLK_9) {
        for (int i = 0; i < ts_num_entries; i++) {
            if (ts_entries[i].team == key - SDLK_0 && !ts_entries[i].locked) {
                TeamSelect_choose(ts_entries[i].team);
                break;
            }
        }
    }
    return true;
}

bool SDL3_TeamSelect_Handle_Mouse_Down(int mx, int my, int button)
{
    float x, y, w, h;

    if (!ts_active)
        return false;
    if (button != SDL_BUTTON_LEFT)
        return true;

    TeamSelect_layout(&x, &y, &w, &h);
    if (mx < x || mx >= x + w || my < y || my >= y + h) {
        TeamSelect_close();
        return true;
    }
    int row = (int)((my - y - TS_HEADER_H) / TS_ROW_H);
    if (my >= y + TS_HEADER_H && row >= 0 && row < ts_num_entries
        && !ts_entries[row].locked)
        TeamSelect_choose(ts_entries[row].team);
    return true;
}

void SDL3_TeamSelect_Render(void)
{
    if (!tng_renderer)
        return;

    /* Show the dialog once, right after joining a team play server. */
    if (!ts_auto_done && self != NULL && Setup != NULL) {
        ts_auto_done = true;
        if (BIT(Setup->mode, TEAM_PLAY) && connectParam.team == TEAM_NOT_SET) {
            TeamSelect_collect();
            if (ts_num_entries >= 2)
                SDL3_TeamSelect_Open();
        }
    }
    if (!ts_active)
        return;

    TeamSelect_collect();
    if (ts_num_entries == 0) {
        TeamSelect_close();
        return;
    }

    float x, y, w, h, fmx, fmy;
    TeamSelect_layout(&x, &y, &w, &h);
    SDL_GetMouseState(&fmx, &fmy);

    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL3_Fill_Rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, borderColor);
    SDL3_Fill_Rect(x, y, w, h, windowColor);

    SDL3_Draw_Text("Select your team", x + TS_PAD, y + 12.0f, XP_COLOR_WHITE);

    for (int i = 0; i < ts_num_entries; i++) {
        const ts_entry_t *e = &ts_entries[i];
        float ry = y + TS_HEADER_H + (float)i * TS_ROW_H;
        bool hover = fmx >= x && fmx < x + w && fmy >= ry && fmy < ry + TS_ROW_H;
        bool mine = self != NULL && self->team == e->team;
        char label[64];

        hover = hover && !e->locked;
        SDL3_Fill_Rect(x + 4.0f, ry + 1.0f, w - 8.0f, TS_ROW_H - 2.0f,
                       hover ? buttonColor : XP_COLOR_BLACK);
        snprintf(label, sizeof(label), "%d  Team %d", e->team, e->team);
        SDL3_Draw_Text(label, x + TS_PAD, ry + 7.0f, XP_COLOR_WHITE);
        if (e->locked)
            snprintf(label, sizeof(label), "%d/%d  (robot team, locked)",
                     e->players, e->bases);
        else
            snprintf(label, sizeof(label), "%d/%d%s", e->players, e->bases,
                     mine ? "  (you)" : "");
        SDL3_Draw_Text(label, x + w - TS_PAD - SDL3_Text_Width(label),
                       ry + 7.0f, mine ? XP_COLOR_GREEN
                        : e->locked ? XP_COLOR_RED : XP_COLOR_WHITE);
    }

    SDL3_Draw_Text("Click or press a number, Esc to cancel", x + TS_PAD,
                   y + h - TS_FOOTER_H + 8.0f, XP_COLOR_GREEN);
}
