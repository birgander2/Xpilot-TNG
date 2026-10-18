/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Input Handling: Keyboard, Mouse Steering, and Window Resizing
 */

#include "sdl3_client.h"

extern void Keyboard_button_pressed(xp_keysym_t ks);
extern void Keyboard_button_released(xp_keysym_t ks);

typedef struct {
    const char *name;
    SDL_Keycode key;
} sdlkey_mapping_t;

static const sdlkey_mapping_t key_mappings[] = {
    { "BackSpace",      SDLK_BACKSPACE },
    { "Tab",            SDLK_TAB },
    { "Return",         SDLK_RETURN },
    { "Enter",          SDLK_RETURN },
    { "Pause",          SDLK_PAUSE },
    { "Scroll_Lock",    SDLK_SCROLLLOCK },
    { "Print",          SDLK_PRINTSCREEN },
    { "Escape",         SDLK_ESCAPE },
    { "Delete",         SDLK_DELETE },
    { "Home",           SDLK_HOME },
    { "Left",           SDLK_LEFT },
    { "Up",             SDLK_UP },
    { "Right",          SDLK_RIGHT },
    { "Down",           SDLK_DOWN },
    { "Page_Up",        SDLK_PAGEUP },
    { "Page_Down",      SDLK_PAGEDOWN },
    { "Prior",          SDLK_PAGEUP },
    { "Next",           SDLK_PAGEDOWN },
    { "End",            SDLK_END },
    { "Insert",         SDLK_INSERT },
    { "Num_Lock",       SDLK_NUMLOCKCLEAR },
    { "KP_Enter",       SDLK_KP_ENTER },
    { "KP_Return",      SDLK_KP_ENTER },
    { "KP_Multiply",    SDLK_KP_MULTIPLY },
    { "KP_Add",         SDLK_KP_PLUS },
    { "KP_Subtract",    SDLK_KP_MINUS },
    { "KP_Decimal",     SDLK_KP_PERIOD },
    { "KP_Divide",      SDLK_KP_DIVIDE },
    { "KP_Insert",      SDLK_KP_0 },
    { "KP_Delete",      SDLK_KP_PERIOD },
    { "KP_0",           SDLK_KP_0 },
    { "KP_1",           SDLK_KP_1 },
    { "KP_2",           SDLK_KP_2 },
    { "KP_3",           SDLK_KP_3 },
    { "KP_4",           SDLK_KP_4 },
    { "KP_5",           SDLK_KP_5 },
    { "KP_6",           SDLK_KP_6 },
    { "KP_7",           SDLK_KP_7 },
    { "KP_8",           SDLK_KP_8 },
    { "KP_9",           SDLK_KP_9 },
    { "F1",             SDLK_F1 },
    { "F2",             SDLK_F2 },
    { "F3",             SDLK_F3 },
    { "F4",             SDLK_F4 },
    { "F5",             SDLK_F5 },
    { "F6",             SDLK_F6 },
    { "F7",             SDLK_F7 },
    { "F8",             SDLK_F8 },
    { "F9",             SDLK_F9 },
    { "F10",            SDLK_F10 },
    { "F11",            SDLK_F11 },
    { "F12",            SDLK_F12 },
    { "Shift_L",        SDLK_LSHIFT },
    { "Shift",          SDLK_LSHIFT },
    { "Shift_R",        SDLK_RSHIFT },
    { "Control_L",      SDLK_LCTRL },
    { "Control",        SDLK_LCTRL },
    { "Control_R",      SDLK_RCTRL },
    { "Alt_L",          SDLK_LALT },
    { "Alt",            SDLK_LALT },
    { "Alt_R",          SDLK_RALT },
    { "Caps_Lock",      SDLK_CAPSLOCK },
    { "space",          SDLK_SPACE },
    { "Space",          SDLK_SPACE },
    { "apostrophe",     SDLK_APOSTROPHE },
    { "quoteright",     SDLK_APOSTROPHE },
    { "comma",          SDLK_COMMA },
    { "plus",           SDLK_PLUS },
    { "minus",          SDLK_MINUS },
    { "period",         SDLK_PERIOD },
    { "slash",          SDLK_SLASH },
    { "semicolon",      SDLK_SEMICOLON },
    { "equal",          SDLK_EQUALS },
    { "bracketleft",    SDLK_LEFTBRACKET },
    { "backslash",      SDLK_BACKSLASH },
    { "bracketright",   SDLK_RIGHTBRACKET },
    { "grave",          SDLK_GRAVE },
    { "quoteleft",      SDLK_GRAVE },
    { NULL,             SDLK_UNKNOWN }
};

xp_keysym_t String_to_xp_keysym(char *name)
{
    if (!name || !name[0]) return (xp_keysym_t)SDLK_UNKNOWN;

    for (const sdlkey_mapping_t *m = key_mappings; m->name != NULL; m++) {
        if (!strcasecmp(name, m->name)) {
            return (xp_keysym_t)m->key;
        }
    }

    if (strlen(name) == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return (xp_keysym_t)(SDLK_A + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return (xp_keysym_t)(SDLK_A + (c - 'A'));
        if (c >= '0' && c <= '9') return (xp_keysym_t)(SDLK_0 + (c - '0'));
        return (xp_keysym_t)c;
    }

    SDL_Keycode code = SDL_GetKeyFromName(name);
    if (code != SDLK_UNKNOWN) {
        return (xp_keysym_t)code;
    }

    return (xp_keysym_t)SDLK_UNKNOWN;
}

const char *Xp_keysym_to_string(xp_keysym_t ks)
{
    static char single_buf[4];
    SDL_Keycode kc = (SDL_Keycode)ks;

    if (kc >= SDLK_A && kc <= SDLK_Z) {
        single_buf[0] = (char)('a' + (kc - SDLK_A));
        single_buf[1] = '\0';
        return single_buf;
    }
    if (kc >= SDLK_0 && kc <= SDLK_9) {
        single_buf[0] = (char)('0' + (kc - SDLK_0));
        single_buf[1] = '\0';
        return single_buf;
    }

    for (const sdlkey_mapping_t *m = key_mappings; m->name != NULL; m++) {
        if (m->key == kc) {
            return m->name;
        }
    }

    const char *name = SDL_GetKeyName(kc);
    if (name && name[0])
        return name;

    return NULL;
}

void SDL3_Process_Events(void)
{
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        switch (evt.type) {
        case SDL_EVENT_QUIT:
            tng_running = false;
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            Voice_release_all();
            if (clData.pointerControl) {
                clData.restorePointerControl = true;
                Pointer_control_set_state(false);
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (clData.restorePointerControl) {
                clData.restorePointerControl = false;
                Pointer_control_set_state(true);
            }
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (clData.talking) {
                SDL3_Talk_Handle_Text_Input(evt.text.text);
            }
            break;

        case SDL_EVENT_KEY_DOWN:
            if (SDL3_TeamSelect_Handle_Key_Down(evt.key.key)) {
                break;
            }
            if (clData.talking) {
                if (SDL3_Talk_Handle_Key_Down(evt.key.key, evt.key.mod)) {
                    break;
                }
            }
            if (!evt.key.repeat) {
                Keyboard_button_pressed((xp_keysym_t)evt.key.key);
            }
            break;

        case SDL_EVENT_KEY_UP:
            Keyboard_button_released((xp_keysym_t)evt.key.key);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int x = (int)evt.button.x;
            int y = (int)evt.button.y;

            if (SDL3_TeamSelect_Handle_Mouse_Down(x, y, evt.button.button)) {
                break;
            }

            if (clData.talking) {
                if (SDL3_Talk_Handle_Mouse_Down(x, y, evt.button.button)) {
                    break;
                }
            }

            if (!clData.pointerControl) {
                /* Mouse steering is currently OFF: check UI / Sidebar / Popups first */
                if (!SDL3_UI_Handle_Mouse_Down(x, y, evt.button.button)) {
                    /* Clicked in Playfield outside popups: Left-click enables mouse steering (just like ng-x11) */
                    if (evt.button.button == SDL_BUTTON_LEFT && x >= PLAYFIELD_X) {
                        Pointer_control_set_state(true);
                    }
                }
            } else {
                /* Mouse steering is ON: trigger configured pointer buttons */
                int btn = 0;
                switch (evt.button.button) {
                case SDL_BUTTON_LEFT:   btn = 1; break;
                case SDL_BUTTON_MIDDLE: btn = 2; break;
                case SDL_BUTTON_RIGHT:  btn = 3; break;
                case SDL_BUTTON_X1:     btn = 4; break;
                case SDL_BUTTON_X2:     btn = 5; break;
                }
                if (btn > 0) {
                    Pointer_button_pressed(btn);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int x = (int)evt.button.x;
            int y = (int)evt.button.y;

            if (!clData.pointerControl) {
                SDL3_UI_Handle_Mouse_Up(x, y, evt.button.button);
            } else {
                int btn = 0;
                switch (evt.button.button) {
                case SDL_BUTTON_LEFT:   btn = 1; break;
                case SDL_BUTTON_MIDDLE: btn = 2; break;
                case SDL_BUTTON_RIGHT:  btn = 3; break;
                case SDL_BUTTON_X1:     btn = 4; break;
                case SDL_BUTTON_X2:     btn = 5; break;
                }
                if (btn > 0) {
                    Pointer_button_released(btn);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
            if (!clData.pointerControl) {
                SDL3_UI_Handle_Mouse_Motion((int)evt.motion.x, (int)evt.motion.y);
            } else {
                if ((int)evt.motion.xrel != 0) {
                    Client_pointer_move((int)evt.motion.xrel);
                    Net_flush();
                }
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (!clData.pointerControl) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);
                SDL3_UI_Handle_Mouse_Wheel((int)mx, (int)my, evt.wheel.y);
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED: {
            int new_w = evt.window.data1;
            int new_h = evt.window.data2;
            if (new_w >= PLAYFIELD_X + 200) top_width = new_w;
            if (new_h >= 400) top_height = new_h;
            draw_width = top_width - PLAYFIELD_X;
            draw_height = top_height;
            Check_view_dimensions();
            Config_resize();
            break;
        }

        default:
            break;
        }
    }
}
