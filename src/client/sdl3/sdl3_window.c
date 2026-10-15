/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Windowing, Frame Lifecycle, and Main Game Loop
 */

#include "sdl3_client.h"

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
    SDL3_Init_Audio();

    return 0;
}

void SDL3_Cleanup(void)
{
    Config_destroy();
    aboutCleanup();
    SDL3_Cleanup_Audio();
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

    /* 2. Clear entire screen to black */
    SDL_SetRenderClipRect(tng_renderer, NULL);
    SDL_SetRenderDrawColor(tng_renderer, 0, 0, 0, 255);
    SDL_RenderClear(tng_renderer);

    /* 3. Set Scissor/Clip Rect to Playfield only (x = 258 .. top_width) */
    SDL_Rect playfield_clip = { PLAYFIELD_X, 0, (int)draw_width, (int)draw_height };
    SDL_SetRenderClipRect(tng_renderer, &playfield_clip);

    if (damaged <= 0) {
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

        /* 6. Render Ships and HUD Instruments */
        Rectangle_start();
        Segment_start();

        Paint_ships();
        Paint_meters();
        Paint_HUD();
        Paint_recording();
        Paint_HUD_values();

        Rectangle_end();
        Segment_end();
        Arc_end();

        /* 7. Render In-Game Messages and Score Objects */
        Paint_messages();
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

void SDL3_Game_Loop(void)
{
    fd_set rfds, tfds;
    int n, netfd, result;
    struct timeval tv;

    tng_running = true;

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

        if (maxMouseTurnsPS > 0) {
            int t = Client_check_pointer_move_interval();
            assert(t > 0);
            if (result <= 1 && t < tv.tv_usec) {
                tv.tv_sec = t / 1000000;
                tv.tv_usec = t % 1000000;
            }
        }

        if ((n = select(netfd + 1, &rfds, NULL, NULL, &tv)) == -1) {
            if (errno == EINTR)
                continue;
            error("Select failed");
            return;
        }

        SDL3_Process_Events();
        if (!tng_running)
            return;

        if (Net_flush() == -1) {
            error("Bad net flush after input");
            return;
        }

        if (FD_ISSET(netfd, &rfds) || result > 1) {
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
