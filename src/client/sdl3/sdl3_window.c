/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Windowing, Frame Lifecycle, and Main Game Loop
 */

#include "sdl3_client.h"
#include "../playout.h"
#include "tng_app_icon.h"

SDL_Window   *tng_window = NULL;
SDL_Renderer *tng_renderer = NULL;
unsigned      top_width = DEF_TOP_WIDTH;
unsigned      top_height = DEF_TOP_HEIGHT;
bool          show_sidebar = true;
int           playfield_x = PLAYFIELD_BASE_X;
bool          tng_running = true;

int SDL3_Init_Window(int w, int h)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "Failed to initialize SDL3: %s\n", SDL_GetError());
        return -1;
    }

    playfield_x = show_sidebar ? PLAYFIELD_BASE_X : 0;
    if (w >= playfield_x + 200) top_width = w;
    if (h >= 400) top_height = h;
    draw_width = top_width - playfield_x;
    draw_height = top_height;

    Uint32 window_flags = SDL_WINDOW_RESIZABLE;
    if (fullScreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    tng_window = SDL_CreateWindow(
        "XPilot TNG Client",
        top_width,
        top_height,
        window_flags
    );

    if (!tng_window) {
        fprintf(stderr, "Failed to create SDL3 window: %s\n", SDL_GetError());
        return -1;
    }

    tng_set_window_icon(tng_window);

    if (fullScreen) {
        int actual_w = 0, actual_h = 0;
        if (SDL_GetWindowSize(tng_window, &actual_w, &actual_h) && actual_w > 0 && actual_h > 0) {
            top_width = actual_w;
            top_height = actual_h;
            draw_width = top_width - playfield_x;
            draw_height = top_height;
        }
    }

    tng_renderer = SDL_CreateRenderer(tng_window, NULL);
    if (!tng_renderer) {
        fprintf(stderr, "Failed to create SDL3 renderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_SetRenderVSync(tng_renderer, 0);
    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);

    SDL3_Init_Colors();
    SDL3_Init_Font();
    SDL3_Init_Draw();
    Fx_init();
    SDL3_Init_Audio();

    return 0;
}

void SDL3_Cleanup(void)
{
    Config_destroy();
    aboutCleanup();
    Voice_cleanup();
    SDL3_Cleanup_Audio();
    Fx_cleanup();
    SDL3_Cleanup_Draw();
    SDL3_Cleanup_Font();

    if (tng_renderer) {
        SDL_DestroyRenderer(tng_renderer);
        tng_renderer = NULL;
    }

    if (tng_window) {
        SDL_DestroyWindow(tng_window);
        tng_window = NULL;
    }

    SDL_Quit();
}

void Paint_frame(void)
{
    if (!tng_renderer) return;

    /* 1. Setup camera world.x and world.y tracking */
    Paint_frame_start();
    Fx_frame_begin();
    {
        /* view shake (nukes): move the camera, the sidebar stays put */
        int sdx, sdy;
        Fx_shake_offset(&sdx, &sdy);
        world.x += sdx;
        world.y += sdy;
        realWorld.x += sdx;
        realWorld.y += sdy;
    }

    /* 2. Clear entire screen to black */
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL_SetRenderDrawColor(tng_renderer, 0, 0, 0, 255);
    SDL_RenderClear(tng_renderer);

    /* 3. Set Scissor/Clip Rect to Playfield only (x = 258 .. top_width) */
    SDL_Rect playfield_clip = { PLAYFIELD_X, 0, (int)draw_width, (int)draw_height };
    SDL_SetRenderClipRect(tng_renderer, &playfield_clip);

    if (damaged <= 0) {
        /* 3b. Parallax starfield behind the map */
        Fx_render_stars();

        /* 4. Render World Map (Walls, Basen, Tanken, Wurmlöcher) */
        Arc_start();
        Rectangle_start();
        Segment_start();

        Paint_world();

        Segment_end();
        Rectangle_end();

        /* 5. Render Map Objects & Shots (Minen, Trümmer, Schüsse, Items) */
        Rectangle_start();
        Segment_start();

        if (oldServer) {
            Paint_vfuel();
            Paint_vdecor();
            Paint_vcannon();
            Paint_vbase();
        } else {
            Paint_objects();
        }

        Paint_shots();

        Rectangle_end();
        Segment_end();

        /* 5b. Cosmetic effects (explosions, sparks), below ships and HUD */
        Fx_render();

        /* 6. Render Ships and HUD Instruments */
        Rectangle_start();
        Segment_start();

        Paint_ships();
        Fx_render_overlay();    /* nuke flash over the world, below the HUD */
        Paint_meters();
        Paint_HUD();
        Paint_recording();
        Paint_HUD_values();

        Rectangle_end();
        Segment_end();
        Arc_end();

        /* 7. Render In-Game Messages and Score Objects */
        Paint_messages();
        Voice_paint();
        Paint_score_objects();
    } else {
        /* Damaged by ECM flash */
        if (damaged & 1) {
            SDL3_Fill_Rect((float)PLAYFIELD_X, 0.0f, (float)draw_width, (float)draw_height, BLUE);
        }
    }

    /* 8. Render Left Sidebar (256x256 Radar, Buttons, Scoreboard or Config) */
    if (show_sidebar) {
        SDL_SetRenderClipRect(tng_renderer, NULL);
        SDL3_Render_Sidebar();
    }

    /* 9. Render UI Overlays (MENU pulldown, ABOUT window, KEYS/MOTD viewer) */
    SDL3_Render_UI_Overlays();

    /* 10. Present Frame */
    SDL_RenderPresent(tng_renderer);
}

/*
 * Modal "connection lost" dialog drawn in the game window (the system
 * message box is not reliable everywhere).  Returns after OK is clicked,
 * Enter/Space/Esc is pressed or the window is closed.
 */
/*
 * Banner drawn straight onto the window while the server is silent: the
 * normal painting only runs when a frame arrives, so nothing else would show.
 * Redrawn at most every 100 ms.
 */
static void Show_Connection_Warning(int silent)
{
    static Uint64 last = 0;
    const SDL_Color white = { 255, 255, 255, 255 };
    const float pad = 12.0f, h = 40.0f;
    char msg[64];
    int w = 0, oh = 0;
    float tw, bw, bx, by;
    Uint64 now = SDL_GetTicks();

    if (!tng_renderer || now - last < 100)
        return;
    last = now;
    snprintf(msg, sizeof(msg), "No data from server for %d s ...", silent);
    SDL_GetCurrentRenderOutputSize(tng_renderer, &w, &oh);
    tw = SDL3_Text_Width(msg);
    bw = tw + 2 * pad;
    bx = ((float)w - bw) / 2;
    by = (float)oh / 4;
    SDL_FRect box = { bx, by, bw, h };
    SDL_SetRenderDrawColor(tng_renderer, 170, 30, 20, 255);
    SDL_RenderFillRect(tng_renderer, &box);
    SDL_SetRenderDrawColor(tng_renderer, 255, 255, 255, 255);
    SDL_RenderRect(tng_renderer, &box);
    SDL3_Draw_Text_Len_Color(msg, (int)strlen(msg), bx + pad, by + (h - 8.0f) / 2, white);
    SDL_RenderPresent(tng_renderer);
}

static void Show_Connection_Lost(void)
{
    /* a captured pointer would hide the cursor and give no click position */
    Platform_specific_pointer_control_set_state(false);

    static const char *lines[] = {
        "Connection to the server was lost.",
        "No data received for 30 seconds."
    };
    const SDL_Color white = { 255, 255, 255, 255 };
    const float line_h = 20.0f, pad = 24.0f, btn_w = 90.0f, btn_h = 30.0f;
    bool done = false;

    while (!done && tng_renderer) {
        SDL_Event e;
        int w = 0, h = 0;
        float tw = 0.0f, bw, bh, bx, by, btn_x, btn_y;

        SDL_GetCurrentRenderOutputSize(tng_renderer, &w, &h);
        for (int i = 0; i < 2; i++)
            tw = SDL_max(tw, SDL3_Text_Width(lines[i]));
        bw = SDL_max(tw, btn_w) + 2 * pad;
        bh = 2 * line_h + btn_h + 3 * pad;
        bx = ((float)w - bw) / 2;
        by = ((float)h - bh) / 2;
        btn_x = bx + (bw - btn_w) / 2;
        btn_y = by + bh - pad - btn_h;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                done = true;
            else if (e.type == SDL_EVENT_KEY_DOWN
                     && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER
                         || e.key.key == SDLK_SPACE
                         || e.key.key == SDLK_ESCAPE))
                done = true;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_ConvertEventToRenderCoordinates(tng_renderer, &e);
                if (e.button.x >= btn_x && e.button.x <= btn_x + btn_w
                    && e.button.y >= btn_y && e.button.y <= btn_y + btn_h)
                    done = true;
            }
        }

        SDL_SetRenderDrawColor(tng_renderer, 0, 0, 0, 255);
        SDL_RenderClear(tng_renderer);
        SDL_FRect box = { bx, by, bw, bh };
        SDL_SetRenderDrawColor(tng_renderer, 30, 30, 40, 255);
        SDL_RenderFillRect(tng_renderer, &box);
        SDL_SetRenderDrawColor(tng_renderer, 220, 60, 60, 255);
        SDL_RenderRect(tng_renderer, &box);
        for (int i = 0; i < 2; i++)
            SDL3_Draw_Text_Len_Color(lines[i], (int)strlen(lines[i]),
                bx + (bw - SDL3_Text_Width(lines[i])) / 2,
                by + pad + i * line_h, white);
        SDL_FRect btn = { btn_x, btn_y, btn_w, btn_h };
        SDL_SetRenderDrawColor(tng_renderer, 60, 90, 160, 255);
        SDL_RenderFillRect(tng_renderer, &btn);
        SDL_SetRenderDrawColor(tng_renderer, 255, 255, 255, 255);
        SDL_RenderRect(tng_renderer, &btn);
        SDL3_Draw_Text_Len_Color("OK", 2,
            btn_x + (btn_w - SDL3_Text_Width("OK")) / 2,
            btn_y + (btn_h - 8.0f) / 2, white);
        SDL_RenderPresent(tng_renderer);
        SDL_Delay(16);
    }
}

void SDL3_Game_Loop(void)
{
    fd_set rfds, tfds;
    int n, netfd, result;
    struct timeval tv;

    tng_running = true;
    Playout_start();

    if ((result = Net_input()) == -1) {
        error("Bad server input");
        return;
    }

    SDL3_Process_Events();
    if (!tng_running)
        return;

    if (Net_flush() == -1)
        return;

    if ((netfd = Net_fd()) == -1) {
        error("Bad socket filedescriptor");
        return;
    }

    Net_key_change();
    FD_ZERO(&rfds);
    FD_SET(netfd, &rfds);

    for (tfds = rfds; tng_running; rfds = tfds) {
        tv.tv_sec = 0;
        tv.tv_usec = (result > 1) ? 0 : 2000; /* poll SDL input every 2 ms when idle */
        tv.tv_usec = Playout_wait_us((int)tv.tv_usec);

        if (maxMouseTurnsPS > 0) {
            int t = Client_check_pointer_move_interval();
            assert(t > 0);
            if (result <= 1 && t < tv.tv_usec) {
                tv.tv_sec = t / 1000000;
                tv.tv_usec = t % 1000000;
            }
        }

        if ((n = select(netfd + 1, &rfds, NULL, NULL, &tv)) == -1) {
            if (sock_last_error() == EINTR)
                continue;
            error("Select failed");
            return;
        }

        SDL3_Process_Events();
        if (!tng_running)
            return;

        Voice_update();
        Key_turn_tick();

        {
            /* Server silent: warn after 10 s, give up after 30 s. */
            static bool warned = false;
            int silent = Net_idle_check();

            if (silent >= 30) {
                error("No data from server for %d seconds, connection lost",
                      silent);
                Show_Connection_Lost();
                return;
            }
            if (silent >= 10 && !warned) {
                Add_message("Connection to server lost? No data received. "
                            "[*Client reply*]");
                warned = true;
            } else if (silent < 10)
                warned = false;
            if (silent >= 10)
                Show_Connection_Warning(silent);
        }

        if (Net_flush() == -1) {
            error("Bad net flush after input");
            return;
        }

        if (FD_ISSET(netfd, &rfds) || result > 1 || Playout_due()) {
            struct timeval tv1, tv2;

            gettimeofday(&tv1, NULL);

            if ((result = Net_input()) == -1) {
                warn("Bad net input.  Have a nice day!");
                return;
            }

            if (result > 0) {
                SDL3_Process_Events();
                if (!tng_running)
                    return;

                if (Net_flush() == -1) {
                    error("Bad net flush");
                    return;
                }
            }

            if (newSecond) {
                gettimeofday(&tv2, NULL);
                clData.clientLag = 1e-3 * timeval_sub(&tv2, &tv1);
            }
        }
    }
}
