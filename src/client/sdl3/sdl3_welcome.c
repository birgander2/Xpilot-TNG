/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Authentic Welcome Screen matching xpilot-ng-x11 architecture
 */

#include "sdl3_client.h"
#include "meta.h"
#include <ctype.h>
#include <time.h>

enum WelcomeMode {
    ModeWaiting = 0,
    ModeLocalnet,
    ModeInternet,
    ModeStatus,
    ModeQuit
};

static enum WelcomeMode welcome_mode = ModeWaiting;

/* Layout parameters matching xpilot-ng-x11 welcome.c */
typedef struct {
    int form_width;
    int form_height;
    int button_x;
    int max_width;
    int button_height;
    int btn_actual_height;
    int height_per_button;
    int button_y_start;
    int subform_x;
    int subform_y;
    int subform_width;
    int subform_height;
} welcome_layout_t;

/* Local servers data */
typedef struct {
    char server_name[MAX_HOST_LEN];
    char server_addr[MAX_HOST_LEN];
    unsigned int server_version;
    Connect_param_t conpar;
} local_server_t;

static local_server_t local_servers[MAX_LOCAL_SERVERS];
static int local_server_count = 0;
static bool local_scanned = false;
static float max_local_name_width = 120.0f;

/* Internet metaserver data */
static bool internet_fetched = false;
static char internet_err[MSG_LEN] = {0};
static char join_err_msg[MSG_LEN] = {0};
static int  internet_page = 0;
static server_info_t *status_sip = NULL;
#define SERVERS_PER_PAGE 24

/* Button ID definitions for mouse hit-testing */
#define BTN_ID_LOCAL        100
#define BTN_ID_INTERNET     101
#define BTN_ID_QUIT         102
#define BTN_ID_JOIN_LOCAL   200  /* 200 + i */
#define BTN_ID_JOIN_NET     300  /* 300 + i */
#define BTN_ID_STAT_NET     500  /* 500 + i */
#define BTN_ID_PING         400
#define BTN_ID_FIRST_PAGE   401
#define BTN_ID_NEXT_PAGE    402
#define BTN_ID_STATUS_JOIN  403
#define BTN_ID_STATUS_BACK  404

static int pressed_button_id = -1;

/*
 * Calculate layout dimensions according to xpilot-ng-x11 welcome.c:1215-1318
 */
static void Welcome_Calculate_Layout(welcome_layout_t *layout)
{
    layout->form_width = top_width;
    layout->form_height = top_height;
    if (layout->form_width < 400) layout->form_width = 400;
    if (layout->form_width > 1600) layout->form_width = 1600;
    if (layout->form_height < 400) layout->form_height = 400;
    if (layout->form_height > 1400) layout->form_height = 1400;

    layout->button_x = 20;
    layout->max_width = 76;
    layout->button_height = 21;
    layout->btn_actual_height = 41;

    int min_height_needed = 3 * layout->button_height;
    int height_available = layout->form_height;
    int max_height_wanted = 3 * (layout->button_height + 40);
    if (height_available < min_height_needed) height_available = min_height_needed;
    if (height_available > max_height_wanted) height_available = max_height_wanted;

    layout->height_per_button = height_available / 3;
    layout->button_y_start = layout->height_per_button - layout->button_height;

    layout->subform_x = 2 * layout->button_x + layout->max_width;
    layout->subform_y = layout->button_x;
    layout->subform_width = layout->form_width - layout->subform_x - layout->subform_y - 2;
    layout->subform_height = layout->form_height - 2 * layout->subform_y - 2 - layout->button_height;
}

/*
 * Drop shadow text matching ShadowDrawString in xpaint.c
 */
static void ShadowDrawString(const char *str, float x, float y, int fg_col, int bg_col)
{
    SDL3_Draw_Text(str, x + 2.0f, y + 2.0f, bg_col);
    SDL3_Draw_Text(str, x, y, fg_col);
}

static void ShadowDrawStringCentered(const char *str, float x, float y, float w, float h, int fg_col, int bg_col)
{
    if (!str) return;
    float tw = SDL3_Text_Width(str);
    float th = 8.0f;
    float tx = x + (w - tw) * 0.5f;
    float ty = y + (h - th) * 0.5f;
    ShadowDrawString(str, tx, ty, fg_col, bg_col);
}

static void ShadowDrawStringLeft(const char *str, float x, float y, float w, float h, int fg_col, int bg_col)
{
    if (!str) return;
    float th = 8.0f;
    float tx = x + 4.0f;
    float ty = y + (h - th) * 0.5f;
    char buf[128];
    strlcpy(buf, str, sizeof(buf));
    float max_w = w - 8.0f;
    size_t len = strlen(buf);
    while (len > 0 && SDL3_Text_Width(buf) > max_w) {
        buf[--len] = '\0';
    }
    ShadowDrawString(buf, tx, ty, fg_col, bg_col);
}

/*
 * Draw button matching Widget_draw_button and Widget_create_activate in widget.c
 */
static void Draw_X11_Button(float x, float y, float w, float h, const char *label, bool pressed)
{
    /* 1px white border */
    SDL3_Draw_Rect(x, y, w + 2.0f, h + 2.0f, XP_COLOR_WHITE);

    /* Background fill: XP_COLOR_BLUE normally, XP_COLOR_BLACK when pressed */
    int bg = pressed ? XP_COLOR_BLACK : XP_COLOR_BLUE;
    SDL3_Fill_Rect(x + 1.0f, y + 1.0f, w, h, bg);

    /* Centered label with drop shadow */
    ShadowDrawStringCentered(label, x + 1.0f, y + 1.0f, w, h, XP_COLOR_WHITE, XP_COLOR_BLACK);
}

/*
 * Draw label matching Widget_create_label and Widget_create_colored_label in widget.c
 */
static void Draw_X11_Label(float x, float y, float w, float h, const char *label, int bg_col, int border_col, bool has_border)
{
    if (has_border) {
        SDL3_Draw_Rect(x, y, w + 2.0f, h + 2.0f, border_col);
        SDL3_Fill_Rect(x + 1.0f, y + 1.0f, w, h, bg_col);
        ShadowDrawStringCentered(label, x + 1.0f, y + 1.0f, w, h, XP_COLOR_WHITE, XP_COLOR_BLACK);
    } else {
        SDL3_Fill_Rect(x, y, w, h, bg_col);
        ShadowDrawStringCentered(label, x, y, w, h, XP_COLOR_WHITE, XP_COLOR_BLACK);
    }
}

static void Draw_X11_Label_Left(float x, float y, float w, float h, const char *label, int bg_col, int border_col, bool has_border)
{
    if (has_border) {
        SDL3_Draw_Rect(x, y, w + 2.0f, h + 2.0f, border_col);
        SDL3_Fill_Rect(x + 1.0f, y + 1.0f, w, h, bg_col);
        ShadowDrawStringLeft(label, x + 1.0f, y + 1.0f, w, h, XP_COLOR_WHITE, XP_COLOR_BLACK);
    } else {
        SDL3_Fill_Rect(x, y, w, h, bg_col);
        ShadowDrawStringLeft(label, x, y, w, h, XP_COLOR_WHITE, XP_COLOR_BLACK);
    }
}

#define NET_COL_VERSION_X   214
#define NET_COL_VERSION_W   110.0f
#define NET_COL_MAP_X       332
#define NET_COL_MAP_W       280.0f
#define NET_COL_SERVER_X    620
#define NET_COL_SERVER_W    260.0f
#define NET_COL_PING_X      888
#define NET_COL_PING_W      44.0f
#define NET_COL_STATUS_X    940
#define NET_COL_STATUS_W    56.0f

/*
 * Try contacting a server to join (matching Internet_server_join_cb in welcome.c)
 */
static bool Try_Join_Server(const char *hostname, const char *ip_str, int port,
                            const Connect_param_t *base_conpar,
                            char *target_server, size_t target_server_size)
{
    Connect_param_t conpar = *base_conpar;
    strlcpy(conpar.server_name, hostname ? hostname : "", sizeof(conpar.server_name));
    strlcpy(conpar.server_addr, (ip_str && ip_str[0]) ? ip_str : (hostname ? hostname : ""), sizeof(conpar.server_addr));
    if (port > 0) {
        conpar.contact_port = port;
    }

    char *server_addr_ptr = conpar.server_addr;
    int result = Contact_servers(1, &server_addr_ptr, 1, 0, 0, NULL,
                                 0, NULL, NULL, NULL, NULL, &conpar);
    if (result) {
        connectParam = conpar;
        strlcpy(target_server, conpar.server_addr, target_server_size);
        join_err_msg[0] = '\0';
        return true;
    }

    printf("Server %s (%s) didn't respond on port %d\n",
           conpar.server_name, conpar.server_addr, conpar.contact_port);
    snprintf(join_err_msg, sizeof(join_err_msg),
             "Server %s (%s:%d) didn't respond.",
             conpar.server_name, conpar.server_addr, conpar.contact_port);
    return false;
}

/*
 * Scan local network for running XPilot servers
 */
static void Scan_Local_Servers(void)
{
    local_server_count = 0;
    max_local_name_width = 120.0f;

    char server_names[MAX_LOCAL_SERVERS * MAX_HOST_LEN];
    char server_addrs[MAX_LOCAL_SERVERS * MAX_HOST_LEN];
    char *name_ptrs[MAX_LOCAL_SERVERS];
    char *addr_ptrs[MAX_LOCAL_SERVERS];
    unsigned int server_versions[MAX_LOCAL_SERVERS];
    int n = 0;

    for (int i = 0; i < MAX_LOCAL_SERVERS; i++) {
        name_ptrs[i] = &server_names[i * MAX_HOST_LEN];
        addr_ptrs[i] = &server_addrs[i * MAX_HOST_LEN];
        name_ptrs[i][0] = '\0';
        addr_ptrs[i][0] = '\0';
    }

    Contact_servers(0, NULL, 0, 2, 0, NULL,
                    MAX_LOCAL_SERVERS, &n,
                    addr_ptrs, name_ptrs, server_versions, &connectParam);

    if (n > MAX_LOCAL_SERVERS)
        n = MAX_LOCAL_SERVERS;

    for (int i = 0; i < n; i++) {
        local_servers[i].conpar = connectParam;
        strlcpy(local_servers[i].server_name, name_ptrs[i], sizeof(local_servers[i].server_name));
        strlcpy(local_servers[i].server_addr, addr_ptrs[i], sizeof(local_servers[i].server_addr));
        local_servers[i].server_version = server_versions[i];

        float tw = SDL3_Text_Width(local_servers[i].server_name);
        if (tw > max_local_name_width)
            max_local_name_width = tw;
    }

    local_server_count = n;
    local_scanned = true;
}

/*
 * Fetch internet servers from metaserver
 */
static void Fetch_Internet_Servers(void)
{
    internet_err[0] = '\0';
    Delete_server_list();

    int res = Get_meta_data(internet_err);
    if (res <= 0) {
        if (!internet_err[0]) {
            strlcpy(internet_err, "Could not contact XPilot meta servers.", sizeof(internet_err));
        }
    } else {
        internet_err[0] = '\0';
        Ping_servers();
        Welcome_sort_server_list();
    }
    internet_fetched = true;
    internet_page = 0;
}

/*
 * Helper for hit-testing rectangles
 */
static bool Point_In_Rect(float px, float py, float x, float y, float w, float h)
{
    return (px >= x && px <= x + w && py >= y && py <= y + h);
}

/*
 * Main Welcome Screen Event Loop & Renderer
 */
int SDL3_Run_Welcome(char *target_server, size_t target_server_size)
{
    if (!tng_renderer) return -1;

    welcome_mode = ModeWaiting;
    local_scanned = false;
    internet_fetched = false;
    pressed_button_id = -1;

    welcome_layout_t layout;
    bool running = true;
    int return_code = -1;

    while (running) {
        Welcome_Calculate_Layout(&layout);

        float btn_local_x = (float)layout.button_x;
        float btn_local_y = (float)layout.button_y_start;
        float btn_w = (float)layout.max_width;
        float btn_h = (float)layout.btn_actual_height;

        float btn_net_x = (float)layout.button_x;
        float btn_net_y = (float)(layout.button_y_start + layout.height_per_button);

        float btn_quit_x = (float)layout.button_x;
        float btn_quit_y = (float)(layout.button_y_start + 2 * layout.height_per_button);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                return_code = -1;
                break;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode k = event.key.key;
                if (k >= 'A' && k <= 'Z') k = (SDL_Keycode)tolower((char)k);
                if (k == SDLK_ESCAPE || k == 'q') {
                    running = false;
                    return_code = -1;
                    break;
                } else if (k == 'l') {
                    welcome_mode = ModeLocalnet;
                    local_scanned = false;
                } else if (k == 'i') {
                    welcome_mode = ModeInternet;
                    internet_fetched = false;
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    float mx = event.button.x;
                    float my = event.button.y;

                    if (Point_In_Rect(mx, my, btn_local_x, btn_local_y, btn_w + 2.0f, btn_h + 2.0f)) {
                        pressed_button_id = BTN_ID_LOCAL;
                    } else if (Point_In_Rect(mx, my, btn_net_x, btn_net_y, btn_w + 2.0f, btn_h + 2.0f)) {
                        pressed_button_id = BTN_ID_INTERNET;
                    } else if (Point_In_Rect(mx, my, btn_quit_x, btn_quit_y, btn_w + 2.0f, btn_h + 2.0f)) {
                        pressed_button_id = BTN_ID_QUIT;
                    } else if (welcome_mode == ModeLocalnet && local_scanned && local_server_count > 0) {
                        for (int i = 0; i < local_server_count; i++) {
                            float row_y = (float)layout.subform_y + 1.0f + (float)(33 + i * 33);
                            float jbtn_x = (float)(layout.subform_x + 1.0f + 20.0f) + max_local_name_width + 20.0f + 20.0f;
                            float jbtn_w = SDL3_Text_Width("Join game") + 40.0f;
                            float jbtn_h = 23.0f;
                            if (Point_In_Rect(mx, my, jbtn_x, row_y, jbtn_w + 2.0f, jbtn_h + 2.0f)) {
                                pressed_button_id = BTN_ID_JOIN_LOCAL + i;
                                break;
                            }
                        }
                    } else if (welcome_mode == ModeInternet && internet_fetched && server_list && List_size(server_list) > 0) {
                        float row_y = (float)(layout.subform_y + 34);
                        list_iter_t it = List_begin(server_list);
                        int skip = internet_page * SERVERS_PER_PAGE;
                        while (it != List_end(server_list) && skip > 0) {
                            LI_FORWARD(it);
                            skip--;
                        }
                        int idx = 0;
                        while (it != List_end(server_list) && idx < SERVERS_PER_PAGE) {
                            float sbtn_x = (float)(layout.subform_x + NET_COL_SERVER_X);
                            float sbtn_w = NET_COL_SERVER_W;
                            float sbtn_h = 20.0f;
                            float stbtn_x = (float)(layout.subform_x + NET_COL_STATUS_X);
                            float stbtn_w = NET_COL_STATUS_W;
                            if (Point_In_Rect(mx, my, sbtn_x, row_y, sbtn_w + 2.0f, sbtn_h + 2.0f)) {
                                pressed_button_id = BTN_ID_JOIN_NET + idx;
                                break;
                            } else if (Point_In_Rect(mx, my, stbtn_x, row_y, stbtn_w + 2.0f, sbtn_h + 2.0f)) {
                                pressed_button_id = BTN_ID_STAT_NET + idx;
                                break;
                            }
                            row_y += 24.0f;
                            idx++;
                            LI_FORWARD(it);
                        }

                        /* Bottom buttons */
                        float bot_y = (float)(layout.form_height - 35);
                        if (Point_In_Rect(mx, my, 20.0f, bot_y, 120.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_PING;
                        } else if (Point_In_Rect(mx, my, (float)(layout.subform_x + 200), bot_y, 170.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_FIRST_PAGE;
                        } else if (Point_In_Rect(mx, my, (float)(layout.subform_x + 400), bot_y, 170.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_NEXT_PAGE;
                        }
                    } else if (welcome_mode == ModeStatus && status_sip) {
                        float label_x = (float)(layout.subform_x + 10);
                        float label_y = (float)(layout.subform_y + 10);
                        float label_w = 130.0f;
                        float data_w = 264.0f;
                        float label_h = 20.0f;
                        float label_space = 5.0f;
                        float act_x = label_x + label_w + data_w + label_space;
                        float join_y = label_y;
                        float back_y = label_y + label_h + label_space;

                        if (Point_In_Rect(mx, my, act_x, join_y, data_w + 2.0f, label_h + 2.0f)) {
                            pressed_button_id = BTN_ID_STATUS_JOIN;
                        } else if (Point_In_Rect(mx, my, act_x, back_y, data_w + 2.0f, label_h + 2.0f)) {
                            pressed_button_id = BTN_ID_STATUS_BACK;
                        }

                        /* Bottom buttons */
                        float bot_y = (float)(layout.form_height - 35);
                        if (Point_In_Rect(mx, my, 20.0f, bot_y, 120.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_PING;
                        } else if (Point_In_Rect(mx, my, (float)(layout.subform_x + 200), bot_y, 170.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_FIRST_PAGE;
                        } else if (Point_In_Rect(mx, my, (float)(layout.subform_x + 400), bot_y, 170.0f, 28.0f)) {
                            pressed_button_id = BTN_ID_NEXT_PAGE;
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    float mx = event.button.x;
                    float my = event.button.y;

                    if (pressed_button_id == BTN_ID_LOCAL) {
                        if (Point_In_Rect(mx, my, btn_local_x, btn_local_y, btn_w + 2.0f, btn_h + 2.0f)) {
                            welcome_mode = ModeLocalnet;
                            local_scanned = false;
                            join_err_msg[0] = '\0';
                        }
                    } else if (pressed_button_id == BTN_ID_INTERNET) {
                        if (Point_In_Rect(mx, my, btn_net_x, btn_net_y, btn_w + 2.0f, btn_h + 2.0f)) {
                            welcome_mode = ModeInternet;
                            internet_fetched = false;
                            join_err_msg[0] = '\0';
                        }
                    } else if (pressed_button_id == BTN_ID_QUIT) {
                        if (Point_In_Rect(mx, my, btn_quit_x, btn_quit_y, btn_w + 2.0f, btn_h + 2.0f)) {
                            running = false;
                            return_code = -1;
                        }
                    } else if (pressed_button_id >= BTN_ID_JOIN_LOCAL && pressed_button_id < BTN_ID_JOIN_LOCAL + local_server_count) {
                        int idx = pressed_button_id - BTN_ID_JOIN_LOCAL;
                        float row_y = (float)layout.subform_y + 1.0f + (float)(33 + idx * 33);
                        float jbtn_x = (float)(layout.subform_x + 1.0f + 20.0f) + max_local_name_width + 20.0f + 20.0f;
                        float jbtn_w = SDL3_Text_Width("Join game") + 40.0f;
                        float jbtn_h = 23.0f;
                        if (Point_In_Rect(mx, my, jbtn_x, row_y, jbtn_w + 2.0f, jbtn_h + 2.0f)) {
                            if (Try_Join_Server(local_servers[idx].server_name,
                                                local_servers[idx].server_addr,
                                                local_servers[idx].conpar.contact_port,
                                                &local_servers[idx].conpar,
                                                target_server, target_server_size)) {
                                running = false;
                                return_code = 0;
                            }
                        }
                    } else if (pressed_button_id >= BTN_ID_JOIN_NET && pressed_button_id < BTN_ID_JOIN_NET + SERVERS_PER_PAGE) {
                        int target_idx = pressed_button_id - BTN_ID_JOIN_NET;
                        float row_y = (float)(layout.subform_y + 34) + target_idx * 24.0f;
                        float sbtn_x = (float)(layout.subform_x + NET_COL_SERVER_X);
                        float sbtn_w = NET_COL_SERVER_W;
                        float sbtn_h = 20.0f;
                        if (Point_In_Rect(mx, my, sbtn_x, row_y, sbtn_w + 2.0f, sbtn_h + 2.0f)) {
                            list_iter_t it = List_begin(server_list);
                            int skip = internet_page * SERVERS_PER_PAGE + target_idx;
                            while (it != List_end(server_list) && skip > 0) {
                                LI_FORWARD(it);
                                skip--;
                            }
                            if (it != List_end(server_list)) {
                                server_info_t *sip = SI_DATA(it);
                                if (Try_Join_Server(sip->hostname, sip->ip_str, (int)sip->port,
                                                    &connectParam, target_server, target_server_size)) {
                                    running = false;
                                    return_code = 0;
                                } else {
                                    welcome_mode = ModeInternet;
                                }
                            }
                        }
                    } else if (pressed_button_id >= BTN_ID_STAT_NET && pressed_button_id < BTN_ID_STAT_NET + SERVERS_PER_PAGE) {
                        int target_idx = pressed_button_id - BTN_ID_STAT_NET;
                        float row_y = (float)(layout.subform_y + 34) + target_idx * 24.0f;
                        float stbtn_x = (float)(layout.subform_x + NET_COL_STATUS_X);
                        float stbtn_w = NET_COL_STATUS_W;
                        float stbtn_h = 20.0f;
                        if (Point_In_Rect(mx, my, stbtn_x, row_y, stbtn_w + 2.0f, stbtn_h + 2.0f)) {
                            list_iter_t it = List_begin(server_list);
                            int skip = internet_page * SERVERS_PER_PAGE + target_idx;
                            while (it != List_end(server_list) && skip > 0) {
                                LI_FORWARD(it);
                                skip--;
                            }
                            if (it != List_end(server_list)) {
                                status_sip = SI_DATA(it);
                                welcome_mode = ModeStatus;
                                join_err_msg[0] = '\0';
                            }
                        }
                    } else if (pressed_button_id == BTN_ID_STATUS_JOIN && status_sip) {
                        float act_x = (float)(layout.subform_x + 10) + 130.0f + 264.0f + 5.0f;
                        float join_y = (float)(layout.subform_y + 10);
                        if (Point_In_Rect(mx, my, act_x, join_y, 264.0f + 2.0f, 20.0f + 2.0f)) {
                            if (Try_Join_Server(status_sip->hostname, status_sip->ip_str, (int)status_sip->port,
                                                &connectParam, target_server, target_server_size)) {
                                running = false;
                                return_code = 0;
                            } else {
                                welcome_mode = ModeInternet;
                            }
                        }
                    } else if (pressed_button_id == BTN_ID_STATUS_BACK) {
                        float act_x = (float)(layout.subform_x + 10) + 130.0f + 264.0f + 5.0f;
                        float back_y = (float)(layout.subform_y + 10) + 25.0f;
                        if (Point_In_Rect(mx, my, act_x, back_y, 264.0f + 2.0f, 20.0f + 2.0f)) {
                            welcome_mode = ModeInternet;
                            join_err_msg[0] = '\0';
                        }
                    } else if (pressed_button_id == BTN_ID_FIRST_PAGE) {
                        welcome_mode = ModeInternet;
                        internet_page = 0;
                        join_err_msg[0] = '\0';
                    } else if (pressed_button_id == BTN_ID_NEXT_PAGE) {
                        welcome_mode = ModeInternet;
                        join_err_msg[0] = '\0';
                        if (server_list && (internet_page + 1) * SERVERS_PER_PAGE < (int)List_size(server_list)) {
                            internet_page++;
                        }
                    } else if (pressed_button_id == BTN_ID_PING) {
                        welcome_mode = ModeInternet;
                        join_err_msg[0] = '\0';
                        Ping_servers();
                        Welcome_sort_server_list();
                    }

                    pressed_button_id = -1;
                }
            }
        }

        if (!running) break;

        /* Rendering */
        SDL3_Set_Draw_Color(XP_COLOR_BLACK);
        SDL_RenderClear(tng_renderer);

        /* Draw left buttons */
        Draw_X11_Button(btn_local_x, btn_local_y, btn_w, btn_h, "Local", pressed_button_id == BTN_ID_LOCAL);
        Draw_X11_Button(btn_net_x, btn_net_y, btn_w, btn_h, "Internet", pressed_button_id == BTN_ID_INTERNET);
        Draw_X11_Button(btn_quit_x, btn_quit_y, btn_w, btn_h, "Quit", pressed_button_id == BTN_ID_QUIT);

        /* Draw subform rectangle: 1px white border, black interior */
        SDL3_Draw_Rect((float)layout.subform_x, (float)layout.subform_y, (float)layout.subform_width + 2.0f, (float)layout.subform_height + 2.0f, XP_COLOR_WHITE);
        SDL3_Fill_Rect((float)layout.subform_x + 1.0f, (float)layout.subform_y + 1.0f, (float)layout.subform_width, (float)layout.subform_height, XP_COLOR_BLACK);

        /* Render subform content based on mode */
        if (welcome_mode == ModeWaiting) {
            const char *welcome_text = "Welcome to XPilot NG!";
            float bw = 187.0f;
            float bh = 33.0f;
            float bx = (float)layout.subform_x + 1.0f + ((float)layout.subform_width - bw) * 0.5f;
            float by = (float)layout.subform_y + 1.0f + 331.0f;
            Draw_X11_Label(bx, by, bw, bh, welcome_text, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
        } else if (welcome_mode == ModeLocalnet) {
            if (!local_scanned) {
                const char *searching_text = "Searching for XPilot servers on your local network...";
                float bw = SDL3_Text_Width(searching_text) + 40.0f;
                float bh = 33.0f;
                float bx = (float)layout.subform_x + ((float)layout.subform_width - bw) * 0.5f;
                float by = (float)layout.subform_y + ((float)layout.subform_height - bh) * 0.5f;
                Draw_X11_Label(bx, by, bw, bh, searching_text, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);

                SDL_RenderPresent(tng_renderer);
                Scan_Local_Servers();
                continue;
            }

            if (local_server_count == 0) {
                const char *none_text = "No servers were found on your local network.";
                float bw = SDL3_Text_Width(none_text) + 40.0f;
                float bh = 33.0f;
                float bx = (float)layout.subform_x + ((float)layout.subform_width - bw) * 0.5f;
                float by = (float)layout.subform_y + ((float)layout.subform_height - bh) * 0.5f;
                Draw_X11_Label(bx, by, bw, bh, none_text, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
            } else {
                const char *found_text = "The following local XPilot servers were found:";
                int header_w = 362;
                int header_h = 23;
                float header_x = (float)layout.subform_x + 1.0f + 262.0f;
                float header_y = (float)layout.subform_y + 1.0f + 10.0f;
                Draw_X11_Label(header_x, header_y, (float)header_w, (float)header_h, found_text, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);

                for (int i = 0; i < local_server_count; i++) {
                    float row_y = (float)layout.subform_y + 1.0f + (float)(33 + i * 33);
                    float btn_x = (float)(layout.subform_x + 1.0f + 20.0f);
                    float btn_w = max_local_name_width + 20.0f;
                    float btn_h = 23.0f;
                    Draw_X11_Label(btn_x, row_y, btn_w, btn_h, local_servers[i].server_name, XP_COLOR_BLUE, XP_COLOR_BLACK, true);

                    float jbtn_x = btn_x + btn_w + 20.0f;
                    float jbtn_w = 89.0f;
                    float jbtn_h = 23.0f;
                    bool pressed = (pressed_button_id == BTN_ID_JOIN_LOCAL + i);
                    Draw_X11_Button(jbtn_x, row_y, jbtn_w, jbtn_h, "Join game", pressed);
                }
            }
        } else if (welcome_mode == ModeInternet) {
            if (!internet_fetched) {
                const char *dns_text = "Doing a DNS lookup .. ";
                float bw = SDL3_Text_Width(dns_text) + 40.0f;
                float bh = 33.0f;
                float bx = (float)layout.subform_x + ((float)layout.subform_width - bw) * 0.5f;
                float by = (float)layout.subform_y + ((float)layout.subform_height - bh) * 0.5f;
                Draw_X11_Label(bx, by, bw, bh, dns_text, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);

                SDL_RenderPresent(tng_renderer);
                Fetch_Internet_Servers();
                continue;
            }

            if (internet_err[0] != '\0') {
                float bw = SDL3_Text_Width(internet_err) + 40.0f;
                float bh = 33.0f;
                float bx = (float)layout.subform_x + ((float)layout.subform_width - bw) * 0.5f;
                float by = (float)layout.subform_y + ((float)layout.subform_height - bh) * 0.5f;
                Draw_X11_Label(bx, by, bw, bh, internet_err, XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
            } else if (server_list && List_size(server_list) > 0) {
                /* Table Headers */
                float hy = (float)(layout.subform_y + 10);
                float h_h = 20.0f;

                Draw_X11_Label((float)(layout.subform_x + 10), hy, 24.0f, h_h, "Pl", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + 42), hy, 16.0f, h_h, "Q", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + 66), hy, 24.0f, h_h, "Ba", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + 98), hy, 24.0f, h_h, "Tm", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + 130), hy, 32.0f, h_h, "FPS", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + 170), hy, 36.0f, h_h, "Stat", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label_Left((float)(layout.subform_x + NET_COL_VERSION_X), hy, NET_COL_VERSION_W, h_h, "Version", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label_Left((float)(layout.subform_x + NET_COL_MAP_X), hy, NET_COL_MAP_W, h_h, "Map Name", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + NET_COL_SERVER_X), hy, NET_COL_SERVER_W, h_h, "Server", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + NET_COL_PING_X), hy, NET_COL_PING_W, h_h, "Ping", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);
                Draw_X11_Label((float)(layout.subform_x + NET_COL_STATUS_X), hy, NET_COL_STATUS_W, h_h, "Status", XP_COLOR_DARK_BLUE, XP_COLOR_BLACK, false);

                /* Server rows */
                list_iter_t it = List_begin(server_list);
                int skip = internet_page * SERVERS_PER_PAGE;
                while (it != List_end(server_list) && skip > 0) {
                    LI_FORWARD(it);
                    skip--;
                }

                float ry = (float)(layout.subform_y + 34);
                int r_idx = 0;
                while (it != List_end(server_list) && r_idx < SERVERS_PER_PAGE && ry + 22.0f < (float)(layout.subform_y + layout.subform_height)) {
                    server_info_t *sip = SI_DATA(it);

                    ShadowDrawStringCentered(sip->users ? sip->users_str : "", (float)(layout.subform_x + 10), ry, 24.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringCentered(sip->queue ? sip->queue_str : "", (float)(layout.subform_x + 42), ry, 16.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringCentered(sip->bases_str ? sip->bases_str : "", (float)(layout.subform_x + 66), ry, 24.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringCentered(sip->teambases > 0 ? sip->teambases_str : "", (float)(layout.subform_x + 98), ry, 24.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringCentered(sip->fps_str ? sip->fps_str : "", (float)(layout.subform_x + 130), ry, 32.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringCentered((sip->status && strcmp(sip->status, "ok") != 0) ? sip->status : "", (float)(layout.subform_x + 170), ry, 36.0f, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringLeft(sip->version ? sip->version : "", (float)(layout.subform_x + NET_COL_VERSION_X), ry, NET_COL_VERSION_W, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);
                    ShadowDrawStringLeft(sip->mapname ? sip->mapname : "", (float)(layout.subform_x + NET_COL_MAP_X), ry, NET_COL_MAP_W, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);

                    /* Server join button */
                    bool s_pressed = (pressed_button_id == BTN_ID_JOIN_NET + r_idx);
                    Draw_X11_Button((float)(layout.subform_x + NET_COL_SERVER_X), ry, NET_COL_SERVER_W, 20.0f, sip->hostname ? sip->hostname : "", s_pressed);

                    /* Ping */
                    snprintf(sip->pingtime_str, sizeof(sip->pingtime_str), "%4d", sip->pingtime);
                    const char *ping_display = (sip->pingtime == PING_NORESP) ? "None" :
                                               (sip->pingtime == PING_SLOW) ? "Slow" :
                                               (sip->pingtime == PING_UNKNOWN) ? "" : sip->pingtime_str;
                    ShadowDrawStringCentered(ping_display, (float)(layout.subform_x + NET_COL_PING_X), ry, NET_COL_PING_W, 20.0f, XP_COLOR_WHITE, XP_COLOR_BLACK);

                    /* Stat button */
                    bool st_pressed = (pressed_button_id == BTN_ID_STAT_NET + r_idx);
                    Draw_X11_Button((float)(layout.subform_x + NET_COL_STATUS_X), ry, NET_COL_STATUS_W, 20.0f, "Stat", st_pressed);

                    ry += 24.0f;
                    r_idx++;
                    LI_FORWARD(it);
                }

                /* Bottom buttons below subform */
                float bot_y = (float)(layout.form_height - 35);
                Draw_X11_Button(20.0f, bot_y, 120.0f, 28.0f, "Ping Servers", pressed_button_id == BTN_ID_PING);
                Draw_X11_Button((float)(layout.subform_x + 200), bot_y, 170.0f, 28.0f, "<< First Server Page", pressed_button_id == BTN_ID_FIRST_PAGE);
                Draw_X11_Button((float)(layout.subform_x + 400), bot_y, 170.0f, 28.0f, "Next Server Page >>>", pressed_button_id == BTN_ID_NEXT_PAGE);
            }
        } else if (welcome_mode == ModeStatus && status_sip) {
            static const char *status_labels[15] = {
                "Server", "IP:Port", "Version", "Users", "Map name",
                "Map size", "Map author", "Status", "Bases", "Teambases",
                "Free bases", "Queued players", "FPS", "Sound", "Timing"
            };
            char ipport[MAX_HOST_LEN + 16];
            snprintf(ipport, sizeof(ipport), "%s:%u",
                     status_sip->ip_str ? status_sip->ip_str : "", status_sip->port);

            const char *status_vals[15] = {
                status_sip->hostname ? status_sip->hostname : "",
                ipport,
                status_sip->version ? status_sip->version : "",
                status_sip->users ? status_sip->users_str : "",
                status_sip->mapname ? status_sip->mapname : "",
                status_sip->mapsize ? status_sip->mapsize : "",
                status_sip->author ? status_sip->author : "",
                status_sip->status ? status_sip->status : "",
                status_sip->bases_str ? status_sip->bases_str : "",
                status_sip->teambases_str ? status_sip->teambases_str : "",
                status_sip->freebases ? status_sip->freebases : "",
                status_sip->queue_str ? status_sip->queue_str : "",
                status_sip->fps_str ? status_sip->fps_str : "",
                status_sip->sound ? status_sip->sound : "",
                (status_sip->timing && strcmp(status_sip->timing, "0") == 0) ? "Not a race" : "Race!"
            };

            float label_x = (float)(layout.subform_x + 10);
            float label_y = (float)(layout.subform_y + 10);
            float label_w = 130.0f;
            float data_w = 264.0f;
            float label_h = 20.0f;
            float label_space = 5.0f;

            for (int i = 0; i < 15; i++) {
                float yoff = label_y + i * (label_h + label_space);
                Draw_X11_Label(label_x, yoff, label_w, label_h, status_labels[i], XP_COLOR_DARK_BLUE, XP_COLOR_WHITE, true);
                Draw_X11_Label(label_x + label_w, yoff, data_w, label_h, status_vals[i], XP_COLOR_BLACK, XP_COLOR_WHITE, true);
            }

            float act_x = label_x + label_w + data_w + label_space;
            Draw_X11_Button(act_x, label_y, data_w, label_h, "Join This Server", pressed_button_id == BTN_ID_STATUS_JOIN);
            Draw_X11_Button(act_x, label_y + label_h + label_space, data_w, label_h, "Back to List", pressed_button_id == BTN_ID_STATUS_BACK);

            /* Playlist section matching Internet_server_show_cb in welcome.c */
            float py = label_y + 15 * (label_h + label_space);
            int commas = 0;
            if (status_sip->playlist) {
                for (const char *s = status_sip->playlist; (s = strchr(s, ',')) != NULL; s++) {
                    commas++;
                }
            }
            float playlist_h = (float)(commas + 2) * (label_h + label_space) - label_space;
            Draw_X11_Label(label_x, py, label_w, playlist_h, "Playlist", XP_COLOR_DARK_BLUE, XP_COLOR_WHITE, true);

            if (status_sip->users != 0) {
                float player_w = 200.0f;
                float host_w = 400.0f;
                Draw_X11_Label(label_x + label_w, py, player_w, label_h, "Player Name", XP_COLOR_DARK_BLUE, XP_COLOR_WHITE, true);
                Draw_X11_Label(label_x + label_w + player_w, py, host_w, label_h, "Player Host", XP_COLOR_DARK_BLUE, XP_COLOR_WHITE, true);
                py += label_h + label_space;

                if (status_sip->playlist && status_sip->playlist[0]) {
                    char *plist_copy = xp_strdup(status_sip->playlist);
                    if (plist_copy) {
                        for (char *p = my_strtok(plist_copy, ","); p; p = my_strtok(NULL, ",")) {
                            char *eqptr = strchr(p, '=');
                            if (!eqptr) continue;
                            *eqptr = '\0';
                            const char *phost = eqptr + 1;
                            if (py + label_h <= (float)(layout.subform_y + layout.subform_height)) {
                                Draw_X11_Label_Left(label_x + label_w, py, player_w, label_h, p, XP_COLOR_BLACK, XP_COLOR_WHITE, true);
                                Draw_X11_Label_Left(label_x + label_w + player_w, py, host_w, label_h, phost, XP_COLOR_BLACK, XP_COLOR_WHITE, true);
                            }
                            py += label_h + label_space;
                        }
                        free(plist_copy);
                    }
                }
            }

            /* Bottom buttons below subform */
            float bot_y = (float)(layout.form_height - 35);
            Draw_X11_Button(20.0f, bot_y, 120.0f, 28.0f, "Ping Servers", pressed_button_id == BTN_ID_PING);
            Draw_X11_Button((float)(layout.subform_x + 200), bot_y, 170.0f, 28.0f, "<< First Server Page", pressed_button_id == BTN_ID_FIRST_PAGE);
            Draw_X11_Button((float)(layout.subform_x + 400), bot_y, 170.0f, 28.0f, "Next Server Page >>>", pressed_button_id == BTN_ID_NEXT_PAGE);
        }

        if (join_err_msg[0] != '\0') {
            float err_w = SDL3_Text_Width(join_err_msg) + 32.0f;
            float err_h = 26.0f;
            float err_x = (float)layout.subform_x + ((float)layout.subform_width - err_w) * 0.5f;
            float err_y = (float)(layout.subform_y + layout.subform_height - 36);
            Draw_X11_Label(err_x, err_y, err_w, err_h, join_err_msg, XP_COLOR_RED, XP_COLOR_WHITE, true);
        }

        SDL_RenderPresent(tng_renderer);
        SDL_Delay(16);
    }

    return return_code;
}
