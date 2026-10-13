/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Bitmap & Texture Management Header
 */

#ifndef SDL3_BITMAPS_H
#define SDL3_BITMAPS_H

#include "sdl3_client.h"
#include "gfx2d.h"

#define BM_HOLDER_FRIEND 0
#define BM_HOLDER_ENEMY  1
#define BM_BALL          2
#define BM_SHIP_SELF     3
#define BM_SHIP_FRIEND   4
#define BM_SHIP_ENEMY    5
#define BM_BULLET        6
#define BM_BULLET_OWN    7
#define BM_BASE_DOWN     8
#define BM_BASE_LEFT     9
#define BM_BASE_UP      10
#define BM_BASE_RIGHT   11
#define BM_FUELCELL     12
#define BM_FUEL         13
#define BM_ALL_ITEMS    14
#define BM_CANNON_DOWN  15
#define BM_CANNON_LEFT  16
#define BM_CANNON_UP    17
#define BM_CANNON_RIGHT 18
#define BM_SPARKS       19
#define BM_PAUSED       20
#define BM_WALL_TOP     21
#define BM_WALL_LEFT    22
#define BM_WALL_BOTTOM  23
#define BM_WALL_RIGHT   24
#define BM_WALL_LU      25
#define BM_WALL_RU      26
#define BM_WALL_LD      27
#define BM_WALL_RD      28
#define BM_WALL_FILLED  29
#define BM_WALL_UR      30
#define BM_WALL_UL      31
#define BM_SCORE_BG     32
#define BM_LOGO         33
#define BM_REFUEL       34
#define BM_WORMHOLE     35
#define BM_MINE_TEAM    36
#define BM_MINE_OTHER   37
#define BM_CONCENTRATOR 38
#define BM_PLUSGRAVITY  39
#define BM_MINUSGRAVITY 40
#define BM_CHECKPOINT   41
#define BM_METER        42
#define BM_ASTEROIDCONC 43
#define BM_BALL_GRAY    44

#define NUM_OBJECT_BITMAPS 45
#define NUM_BITMAPS        45

#define BG_IMAGE_HEIGHT 442
#define LOGO_HEIGHT     223

int          Bitmaps_init(void);
void         Bitmaps_cleanup(void);
int          Bitmap_add(const char *filename, int count, bool scalable);
SDL_Texture *Bitmap_get_texture(int img, int frame, int *out_w, int *out_h);
void         SDL3_Bitmap_Paint(int img, int frame, float x, float y);
void         SDL3_Bitmap_Paint_Blended(int img, int frame, float x, float y, Uint32 rgb);
void         SDL3_Bitmap_Paint_Area(int img, int frame, float x, float y, const irec_t *area);
void         SDL3_Bitmap_Paint_Scaled(int img, int frame, float x, float y, float w, float h);

#define Bitmap_paint(d, img, x, y, frame) SDL3_Bitmap_Paint(img, frame, (float)(x), (float)(y))

#endif /* SDL3_BITMAPS_H */
