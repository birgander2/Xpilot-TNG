/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * In-Game Talk Window and Message History (matching ng-x11)
 */

#include "sdl3_client.h"
#include "talk.h"
#include "netclient.h"
#include <ctype.h>
#include <string.h>

bool save_talk_str = false;

static char talk_str[MAX_CHARS];
static int  talk_cursor_pos = 0;
static int  history_pos = -1;

extern char *HistoryMsg[];
extern int   maxLinesInHistory;
extern void  Talk_set_state(bool on);

static void Add_msg_to_history(const char *message)
{
    if (!message || message[0] == '\0')
        return;
    if (maxLinesInHistory <= 0)
        return;

    save_talk_str = false;

    char **msg_set = HistoryMsg;
    char *tmp = msg_set[maxLinesInHistory - 1];
    for (int i = maxLinesInHistory - 1; i > 0; i--) {
        msg_set[i] = msg_set[i - 1];
    }
    msg_set[0] = tmp;

    strlcpy(msg_set[0], message, MAX_CHARS);
    history_pos = -1;
}

static const char *Get_msg_from_history(int *pos, const char *message, keys_t direction)
{
    if (maxLinesInHistory <= 0)
        return NULL;

    if (direction != KEY_TALK_CURSOR_UP && direction != KEY_TALK_CURSOR_DOWN)
        return NULL;

    if (save_talk_str) {
        if (message && message[0] != '\0') {
            Add_msg_to_history(message);
        }
        save_talk_str = false;
        return NULL;
    }

    char **msg_set = HistoryMsg;
    for (int i = 0; i < maxLinesInHistory; i++) {
        if (direction == KEY_TALK_CURSOR_UP) {
            (*pos)++;
            if (*pos >= maxLinesInHistory)
                *pos = 0;
        } else if (direction == KEY_TALK_CURSOR_DOWN) {
            (*pos)--;
            if (*pos < 0)
                *pos = maxLinesInHistory - 1;
        }
        if (msg_set[*pos] && msg_set[*pos][0] != '\0')
            return msg_set[*pos];
    }
    return NULL;
}

void SDL3_Talk_Set_State(bool on)
{
    if (on) {
        talk_str[0] = '\0';
        talk_cursor_pos = 0;
        history_pos = -1;
        save_talk_str = false;
        if (tng_window) {
            SDL_StartTextInput(tng_window);
        }
    } else {
        if (tng_window) {
            SDL_StopTextInput(tng_window);
        }
        history_pos = -1;
        save_talk_str = false;
        Key_clear_counts();
    }
}

bool SDL3_Talk_Handle_Key_Down(SDL_Keycode key, SDL_Keymod mod)
{
    if (!clData.talking)
        return false;

    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;

    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (talk_str[0] != '\0') {
            Add_msg_to_history(talk_str);
            Net_talk(talk_str);
            talk_str[0] = '\0';
            talk_cursor_pos = 0;
        } else {
            save_talk_str = false;
            history_pos = -1;
        }
        Talk_set_state(false);
        return true;

    case SDLK_ESCAPE:
        talk_str[0] = '\0';
        talk_cursor_pos = 0;
        save_talk_str = false;
        Talk_set_state(false);
        return true;

    case SDLK_BACKSPACE:
        if (talk_cursor_pos > 0) {
            int len = (int)strlen(talk_str);
            memmove(&talk_str[talk_cursor_pos - 1], &talk_str[talk_cursor_pos], len - talk_cursor_pos + 1);
            talk_cursor_pos--;
            save_talk_str = true;
        }
        return true;

    case SDLK_DELETE: {
        int len = (int)strlen(talk_str);
        if (talk_cursor_pos < len) {
            memmove(&talk_str[talk_cursor_pos], &talk_str[talk_cursor_pos + 1], len - talk_cursor_pos);
            save_talk_str = true;
        }
        return true;
    }

    case SDLK_LEFT:
        if (talk_cursor_pos > 0) {
            talk_cursor_pos--;
        }
        return true;

    case SDLK_RIGHT:
        if (talk_cursor_pos < (int)strlen(talk_str)) {
            talk_cursor_pos++;
        }
        return true;

    case SDLK_HOME:
        talk_cursor_pos = 0;
        return true;

    case SDLK_END:
        talk_cursor_pos = (int)strlen(talk_str);
        return true;

    case SDLK_UP: {
        const char *hist = Get_msg_from_history(&history_pos, talk_str, KEY_TALK_CURSOR_UP);
        if (hist && hist[0] != '\0') {
            strlcpy(talk_str, hist, sizeof(talk_str));
            talk_cursor_pos = (int)strlen(talk_str);
        } else {
            talk_str[0] = '\0';
            talk_cursor_pos = 0;
        }
        return true;
    }

    case SDLK_DOWN: {
        const char *hist = Get_msg_from_history(&history_pos, talk_str, KEY_TALK_CURSOR_DOWN);
        if (hist && hist[0] != '\0') {
            strlcpy(talk_str, hist, sizeof(talk_str));
            talk_cursor_pos = (int)strlen(talk_str);
        } else {
            talk_str[0] = '\0';
            talk_cursor_pos = 0;
        }
        return true;
    }

    default:
        break;
    }

    /* Handle Ctrl shortcuts */
    if (ctrl) {
        if (key == SDLK_A) {
            talk_cursor_pos = 0;
            return true;
        } else if (key == SDLK_E) {
            talk_cursor_pos = (int)strlen(talk_str);
            return true;
        } else if (key == SDLK_B) {
            if (talk_cursor_pos > 0) talk_cursor_pos--;
            return true;
        } else if (key == SDLK_F) {
            if (talk_cursor_pos < (int)strlen(talk_str)) talk_cursor_pos++;
            return true;
        } else if (key == SDLK_U) {
            talk_str[0] = '\0';
            talk_cursor_pos = 0;
            save_talk_str = true;
            return true;
        } else if (key == SDLK_K) {
            talk_str[talk_cursor_pos] = '\0';
            save_talk_str = true;
            return true;
        } else if (key == SDLK_W) {
            while (talk_cursor_pos > 0 && talk_str[talk_cursor_pos - 1] == ' ') {
                int len = (int)strlen(talk_str);
                memmove(&talk_str[talk_cursor_pos - 1], &talk_str[talk_cursor_pos], len - talk_cursor_pos + 1);
                talk_cursor_pos--;
            }
            while (talk_cursor_pos > 0 && talk_str[talk_cursor_pos - 1] != ' ') {
                int len = (int)strlen(talk_str);
                memmove(&talk_str[talk_cursor_pos - 1], &talk_str[talk_cursor_pos], len - talk_cursor_pos + 1);
                talk_cursor_pos--;
            }
            save_talk_str = true;
            return true;
        } else if (key == SDLK_V) {
            if (SDL_HasClipboardText()) {
                char *clip = SDL_GetClipboardText();
                if (clip) {
                    for (const char *p = clip; *p && *p != '\n' && *p != '\r'; p++) {
                        if (isprint((unsigned char)*p) && strlen(talk_str) < MAX_CHARS - 1) {
                            int len = (int)strlen(talk_str);
                            memmove(&talk_str[talk_cursor_pos + 1], &talk_str[talk_cursor_pos], len - talk_cursor_pos + 1);
                            talk_str[talk_cursor_pos++] = *p;
                        }
                    }
                    SDL_free(clip);
                    save_talk_str = true;
                }
            }
            return true;
        }
    }

    /* While talking, swallow all other keys so game controls aren't triggered */
    return true;
}

bool SDL3_Talk_Handle_Text_Input(const char *text)
{
    if (!clData.talking || !text)
        return false;

    for (const char *p = text; *p; p++) {
        if (isprint((unsigned char)*p) && strlen(talk_str) < MAX_CHARS - 1) {
            int len = (int)strlen(talk_str);
            memmove(&talk_str[talk_cursor_pos + 1], &talk_str[talk_cursor_pos], len - talk_cursor_pos + 1);
            talk_str[talk_cursor_pos++] = *p;
            save_talk_str = true;
        }
    }
    return true;
}

static float Talk_box_height(void)
{
    return SDL_ceilf(SDL3_Prop_Text_Height()) + 10.0f;
}

bool SDL3_Talk_Handle_Mouse_Down(int x, int y, int button)
{
    if (!clData.talking)
        return false;

    (void)button;

    float tw_x = (float)(PLAYFIELD_X + 50);
    float tw_w = (float)(draw_width - 100);
    if (tw_w < 200.0f) tw_w = 200.0f;
    float tw_h = Talk_box_height();
    float tw_y = (float)((int)(draw_height * 3 / 4 - tw_h / 2));

    if (x >= (int)tw_x && x <= (int)(tw_x + tw_w) &&
        y >= (int)tw_y && y <= (int)(tw_y + tw_h)) {
        /* Click inside talk box: position cursor */
        const char *prompt = "Talk: ";
        float text_start_x = tw_x + 8.0f + SDL3_Prop_Text_Width(prompt);
        float click_rel = (float)x - text_start_x;
        talk_cursor_pos = (click_rel <= 0.0f) ? 0 : SDL3_Prop_Index_At(talk_str, click_rel);
        return true;
    }

    /* Click outside talk box: close talk */
    Talk_set_state(false);
    return true;
}

void SDL3_Render_Talk_Window(void)
{
    if (!clData.talking || !tng_renderer)
        return;

    float tw_x = (float)(PLAYFIELD_X + 50);
    float tw_w = (float)(draw_width - 100);
    if (tw_w < 200.0f) tw_w = 200.0f;
    float tw_h = Talk_box_height();
    float tw_y = (float)((int)(draw_height * 3 / 4 - tw_h / 2));

    /* 1. Black background */
    SDL3_Fill_Rect(tw_x, tw_y, tw_w, tw_h, BLACK);

    /* 2. White 2px border (matching ng-x11 TALK_OUTSIDE_BORDER) */
    SDL3_Draw_Rect(tw_x, tw_y, tw_w, tw_h, WHITE);
    SDL3_Draw_Rect(tw_x + 1.0f, tw_y + 1.0f, tw_w - 2.0f, tw_h - 2.0f, WHITE);

    /* 3. "Talk: " prompt in green */
    const char *prompt = "Talk: ";
    float prompt_w = SDL3_Prop_Text_Width(prompt);
    float text_h = SDL3_Prop_Text_Height();
    float text_y = tw_y + SDL_floorf((tw_h - text_h) * 0.5f);
    SDL3_Prop_Draw_Text(prompt, tw_x + 8.0f, text_y, 4 /* XPGREEN */);

    /* 4. Clip area for talk message so it never overflows box */
    SDL_Rect prev_clip;
    bool had_clip = SDL_GetRenderClipRect(tng_renderer, &prev_clip);

    SDL_Rect text_clip = {
        .x = (int)(tw_x + 8.0f + prompt_w),
        .y = (int)tw_y,
        .w = (int)(tw_w - 16.0f - prompt_w),
        .h = (int)tw_h
    };
    if (text_clip.w < 10) text_clip.w = 10;
    SDL_SetRenderClipRect(tng_renderer, &text_clip);

    /* Scroll offset if text length exceeds visible box */
    float cursor_rel_x = SDL3_Prop_Text_Width_Len(talk_str, talk_cursor_pos);
    float visible_w = (float)text_clip.w - 10.0f;
    float scroll_offset = 0.0f;
    if (cursor_rel_x > visible_w) {
        scroll_offset = cursor_rel_x - visible_w;
    }

    float msg_x = (float)text_clip.x - scroll_offset;
    SDL3_Prop_Draw_Text(talk_str, msg_x, text_y, WHITE);

    /* 5. Blinking cursor */
    bool cursor_visible = ((SDL_GetTicks() / 400) % 2 == 0);
    if (cursor_visible) {
        float cx = msg_x + cursor_rel_x;
        SDL3_Fill_Rect(cx, text_y + 2.0f, 2.0f, text_h - 4.0f, WHITE);
    }

    /* Restore previous clip rect */
    SDL_SetRenderClipRect(tng_renderer, had_clip ? &prev_clip : NULL);
}
