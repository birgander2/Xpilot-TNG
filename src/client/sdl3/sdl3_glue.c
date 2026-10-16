/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Platform Glue and Hooks for xpclient
 */

#include "sdl3_client.h"

selection_t selection;

void Play_beep(void)
{
    fprintf(stderr, "\aBEEP\n");
}

void Raise_window(void)
{
    if (tng_window) {
        SDL_RaiseWindow(tng_window);
    }
}

#include "sdl3_bitmaps.h"

int Paint_init(void)
{
    Init_spark_colors();
    if (Init_wreckage() == -1)
        return -1;
    if (Init_asteroids() == -1)
        return -1;
    if (Bitmaps_init() == -1)
        return -1;
    return 0;
}

void Paint_cleanup(void)
{
    Bitmaps_cleanup();
}

int Init_playing_windows(void)
{
    return 0;
}

void Platform_specific_cleanup(void)
{
}

bool Set_scaleFactor(xp_option_t *opt, double val)
{
    (void)opt;
    if (val <= 0.0)
        val = 1.0;
    clData.scaleFactor = val;
    clData.scale = 1.0 / val;
    clData.fscale = (float)clData.scale;
    return true;
}

bool Set_altScaleFactor(xp_option_t *opt, double val)
{
    (void)opt;
    if (val <= 0.0)
        val = 1.0;
    clData.altScaleFactor = val;
    return true;
}

void Platform_specific_pointer_control_set_state(bool on)
{
    clData.pointerControl = on;
    if (tng_window) {
        SDL_SetWindowRelativeMouseMode(tng_window, on);
        if (on) {
            SDL_HideCursor();
        } else {
            SDL_ShowCursor();
        }
    }
}

void Platform_specific_talk_set_state(bool on)
{
    clData.talking = on;
    SDL3_Talk_Set_State(on);
}

void Toggle_fullscreen(void)
{
    if (tng_window) {
        Uint32 flags = SDL_GetWindowFlags(tng_window);
        SDL_SetWindowFullscreen(tng_window, !(flags & SDL_WINDOW_FULLSCREEN));
    }
}

void Toggle_radar_and_scorelist(void)
{
    show_sidebar = !show_sidebar;
    playfield_x = show_sidebar ? PLAYFIELD_BASE_X : 0;
    draw_width = top_width - playfield_x;
    Check_view_dimensions();
    Config_resize();
}

void Record_toggle(void) {}

void Add_alert_message(const char *msg, double timeout)
{
    if (msg)
        Add_HUD_message_timed(msg, timeout);
}

void Clear_alert_messages(void)
{
    Del_HUD_message();
}
