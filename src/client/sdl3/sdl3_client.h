/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Header definitions matching xpilot-ng-x11 architecture
 */

#ifndef SDL3_CLIENT_H
#define SDL3_CLIENT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xpclient.h"
#include "paint.h"
#include "netclient.h"
#include "guimap.h"
#include "guiobjects.h"
#include "option.h"
#include "configure.h"
#include "audio.h"

/* Layout dimensions */
#define SIDEBAR_WIDTH       256
#define RADAR_HEIGHT        256
#define BORDER_WIDTH        2
#define PLAYFIELD_X         (SIDEBAR_WIDTH + BORDER_WIDTH) /* 258 */
#define DEF_TOP_WIDTH       (1280 + 2)
#define DEF_TOP_HEIGHT      1024
#define BUTTON_HEIGHT       24

/* Authentic XPilot 16-color indices */
#define XP_COLOR_BLACK        0
#define XP_COLOR_WHITE        1
#define XP_COLOR_BLUE         2
#define XP_COLOR_RED          3
#define XP_COLOR_GREEN        4
#define XP_COLOR_BROWN        5
#define XP_COLOR_GOLD         6
#define XP_COLOR_ORANGE       7
#define XP_COLOR_DARK_BLUE    8
#define XP_COLOR_RUST         9
#define XP_COLOR_DARK_ORANGE 10
#define XP_COLOR_YELLOW      11
#define XP_COLOR_GRAY        12
#define XP_COLOR_DARK_GRAY   13
#define XP_COLOR_LIGHT_GRAY  14
#define XP_COLOR_CHARCOAL    15
#define XP_COLOR_SMARTBOT    16
#define XP_NUM_PALETTE_COLORS 17

/* Global state */
extern SDL_Window   *tng_window;
extern SDL_Renderer *tng_renderer;
extern unsigned      top_width, top_height;
extern unsigned      draw_width, draw_height;
extern Connect_param_t connectParam;
extern xp_args_t     xpArgs;
extern bool          tng_running;

/* Color subsystem & ng-x11 color options */
extern bool fullColor;
extern bool texturedObjects;
extern int  maxColors;
extern int  spark_color[MAX_COLORS];
extern int  wallColor;
extern int  decorColor;
extern int  windowColor;
extern int  buttonColor;
extern int  borderColor;

void SDL3_Init_Colors(void);
void Init_spark_colors(void);
void Colors_init_style_colors(void);
void Store_color_options(void);
void Store_xpaint_options(void);
int  Init_asteroids(void);
void SDL3_Set_Draw_Color(int color_idx);
void SDL3_Set_Draw_Color_RGB(Uint32 rgb);
SDL_Color SDL3_Get_Color(int color_idx);
SDL_Color SDL3_Get_Color_RGB(Uint32 rgb);
SDL_FColor SDL3_Get_FColor(int color_idx);
SDL_FColor SDL3_Get_FColor_RGB(Uint32 rgb);

/* Crisp Bitmap Font subsystem */
int   SDL3_Init_Font(void);
void  SDL3_Cleanup_Font(void);
void  SDL3_Draw_Text(const char *str, float x, float y, int color_idx);
void  SDL3_Draw_Text_Len(const char *str, int len, float x, float y, int color_idx);
float SDL3_Text_Width(const char *str);
float SDL3_Text_Width_Len(int len);

static inline void Check_name_string(other_t *other)
{
    if (other && other->max_chars_in_names != maxCharsInNames) {
        int len;

        strlcpy(other->id_string, other->nick_name, sizeof(other->id_string));
        len = (int)strlen(other->id_string);
        if (maxCharsInNames >= 0 && maxCharsInNames < len)
            other->id_string[maxCharsInNames] = '\0';
        other->name_len = (int)strlen(other->id_string);
        other->name_width = 2 + (int)SDL3_Text_Width(other->id_string);
        other->max_chars_in_names = maxCharsInNames;
    }
}

/* Drawing primitives & coordinate mapping */
int   SDL3_Init_Draw(void);
void  SDL3_Cleanup_Draw(void);
float Screen_X(int world_x);
float Screen_Y(int world_y);
void  SDL3_Draw_Line(float x1, float y1, float x2, float y2, int color_idx);
void  SDL3_Draw_Line_RGB(float x1, float y1, float x2, float y2, Uint32 rgb);
void  SDL3_Draw_Thick_Line(float x1, float y1, float x2, float y2, float thickness, int color_idx);
void  SDL3_Draw_Dashed_Line(float x1, float y1, float x2, float y2, int color_idx, float dash_len, float gap_len);
void  SDL3_Draw_Lines(const SDL_FPoint *points, int count, int color_idx, bool close_loop);
void  SDL3_Draw_Dashed_Lines(const SDL_FPoint *points, int count, int color_idx, float dash_len, float gap_len);
void  SDL3_Draw_Lines_RGB(const SDL_FPoint *points, int count, Uint32 rgb, bool close_loop);
void  SDL3_Draw_Rect(float x, float y, float w, float h, int color_idx);
void  SDL3_Fill_Rect(float x, float y, float w, float h, int color_idx);
void  SDL3_Draw_Circle(float cx, float cy, float radius, int color_idx);
void  SDL3_Draw_Dashed_Circle(float cx, float cy, float radius, int color_idx, int num_dashes);
void  SDL3_Draw_Arc(float cx, float cy, float rx, float ry, int angle1, int angle2, int color_idx);
void  SDL3_Draw_Arc_RGB(float cx, float cy, float rx, float ry, int angle1, int angle2, Uint32 rgb);
void  SDL3_Fill_Circle(float cx, float cy, float radius, int color_idx);
int   SDL3_Triangulate_Polygon(const SDL_FPoint *points, int count, int *out_indices, int max_indices);
void  SDL3_Fill_Polygon(const SDL_FPoint *points, int count, int color_idx);
void  SDL3_Fill_Polygon_RGB(const SDL_FPoint *points, int count, Uint32 rgb);
void  SDL3_Render_Textured_Polygon(const SDL_FPoint *points, int count, SDL_Texture *texture, float origin_x, float origin_y, int tex_w, int tex_h);
void  SDL3_Draw_Item_Icon(int item_type, float x, float y, int color_idx);
void  Gui_paint_item_symbol(int type, int x, int y, int color_idx);
void  Add_HUD_message(const char *message);
void  Del_HUD_message(void);
void  Paint_recording(void);

/* Batching primitives matching paintdata */
void Segment_start(void);
void Segment_end(void);
int  Segment_add(int color, int x1, int y1, int x2, int y2);
void Arc_start(void);
void Arc_end(void);
int  Arc_add(int color, int x, int y, int width, int height, int angle1, int angle2);
int  Arc_add_rgb(unsigned long color, int fallback, int x, int y, int width, int height, int angle1, int angle2);
void Rectangle_start(void);
void Rectangle_end(void);
int  Rectangle_add(int color, int x, int y, int width, int height);

/* Window, lifecycle & game loop */
int  SDL3_Init_Window(int w, int h);
void SDL3_Cleanup(void);
void SDL3_Game_Loop(void);
int  SDL3_Run_Welcome(char *out_server, size_t max_len);

/* Sidebar, Menus & In-game panels */
void SDL3_Render_Sidebar(void);
void SDL3_Render_UI_Overlays(void);
bool SDL3_UI_Handle_Mouse_Down(int x, int y, int button);
bool SDL3_UI_Handle_Mouse_Up(int x, int y, int button);
bool SDL3_UI_Handle_Mouse_Motion(int x, int y);
bool SDL3_UI_Handle_Mouse_Wheel(int x, int y, float wheel_y);
void Paint_clock(bool redraw);
const char *Xp_keysym_to_string(xp_keysym_t ks);

/* Input */
void SDL3_Process_Events(void);

/* Audio */
int  SDL3_Init_Audio(void);
void SDL3_Cleanup_Audio(void);
void SDL3_Play_Sound(int sound_ind, int volume);

#endif /* SDL3_CLIENT_H */
