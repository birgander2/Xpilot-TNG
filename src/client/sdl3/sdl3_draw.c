/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Drawing Primitives, Geometry Batching, and Item Icons
 */

#include "sdl3_client.h"

#include "items/itemEnergyPack.xbm"
#include "items/itemWideangleShot.xbm"
#include "items/itemRearShot.xbm"
#include "items/itemAfterburner.xbm"
#include "items/itemCloakingDevice.xbm"
#include "items/itemSensorPack.xbm"
#include "items/itemTransporter.xbm"
#include "items/itemTank.xbm"
#include "items/itemMinePack.xbm"
#include "items/itemRocketPack.xbm"
#include "items/itemEcm.xbm"
#include "items/itemLaser.xbm"
#include "items/itemEmergencyThrust.xbm"
#include "items/itemTractorBeam.xbm"
#include "items/itemAutopilot.xbm"
#include "items/itemEmergencyShield.xbm"
#include "items/itemDeflector.xbm"
#include "items/itemHyperJump.xbm"
#include "items/itemPhasingDevice.xbm"
#include "items/itemMirror.xbm"
#include "items/itemArmor.xbm"

#include "sdl3_bitmaps.h"

/* -------------------------------------------------------------
 * Coordinate Transformations
 * ------------------------------------------------------------- */

float Screen_X(int world_x)
{
    return (float)(PLAYFIELD_X + WINSCALE(X(world_x)));
}

float Screen_Y(int world_y)
{
    return (float)(WINSCALE(Y(world_y)));
}

/* -------------------------------------------------------------
 * Primitives: Lines, Rectangles, Circles, Polygons
 * ------------------------------------------------------------- */

void SDL3_Draw_Line(float x1, float y1, float x2, float y2, int color_idx)
{
    if (!tng_renderer) return;
    SDL3_Set_Draw_Color(color_idx);
    SDL_RenderLine(tng_renderer, x1, y1, x2, y2);
}

void SDL3_Draw_Line_RGB(float x1, float y1, float x2, float y2, Uint32 rgb)
{
    if (!tng_renderer) return;
    SDL3_Set_Draw_Color_RGB(rgb);
    SDL_RenderLine(tng_renderer, x1, y1, x2, y2);
}

void SDL3_Draw_Thick_Line(float x1, float y1, float x2, float y2, float thickness, int color_idx)
{
    if (!tng_renderer) return;
    if (thickness <= 1.5f) {
        SDL3_Draw_Line(x1, y1, x2, y2, color_idx);
        return;
    }

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = hypotf(dx, dy);
    if (len < 1e-3f) return;

    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    SDL_FColor fc = SDL3_Get_FColor(color_idx);
    SDL_Vertex verts[4];
    verts[0].position = (SDL_FPoint){ x1 + nx, y1 + ny };
    verts[1].position = (SDL_FPoint){ x2 + nx, y2 + ny };
    verts[2].position = (SDL_FPoint){ x2 - nx, y2 - ny };
    verts[3].position = (SDL_FPoint){ x1 - nx, y1 - ny };
    for (int i = 0; i < 4; i++) {
        verts[i].color = fc;
        verts[i].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };
    }
    int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(tng_renderer, NULL, verts, 4, indices, 6);
}

void SDL3_Draw_Dashed_Line(float x1, float y1, float x2, float y2, int color_idx, float dash_len, float gap_len)
{
    if (!tng_renderer) return;
    if (dash_len <= 0.0f) dash_len = 4.0f;
    if (gap_len <= 0.0f) gap_len = 4.0f;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float total_len = hypotf(dx, dy);
    if (total_len < 1e-3f) return;

    float ux = dx / total_len;
    float uy = dy / total_len;
    float pos = 0.0f;

    SDL3_Set_Draw_Color(color_idx);
    while (pos < total_len) {
        float end = pos + dash_len;
        if (end > total_len) end = total_len;
        SDL_RenderLine(tng_renderer,
                       x1 + ux * pos, y1 + uy * pos,
                       x1 + ux * end, y1 + uy * end);
        pos += dash_len + gap_len;
    }
}

void SDL3_Draw_Dashed_Lines(const SDL_FPoint *points, int count, int color_idx, float dash_len, float gap_len)
{
    if (!tng_renderer || !points || count < 2) return;
    if (dash_len <= 0.0f) dash_len = 4.0f;
    if (gap_len <= 0.0f) gap_len = 4.0f;

    float cycle = dash_len + gap_len;
    float phase = 0.0f;

    SDL3_Set_Draw_Color(color_idx);
    for (int i = 0; i < count - 1; i++) {
        float x1 = points[i].x, y1 = points[i].y;
        float x2 = points[i + 1].x, y2 = points[i + 1].y;
        float dx = x2 - x1;
        float dy = y2 - y1;
        float seg_len = hypotf(dx, dy);
        if (seg_len < 1e-3f) continue;

        float ux = dx / seg_len;
        float uy = dy / seg_len;
        float consumed = 0.0f;

        while (consumed < seg_len) {
            float rem_in_seg = seg_len - consumed;
            if (phase < dash_len) {
                float rem_dash = dash_len - phase;
                float step = (rem_dash < rem_in_seg) ? rem_dash : rem_in_seg;
                SDL_RenderLine(tng_renderer,
                               x1 + ux * consumed, y1 + uy * consumed,
                               x1 + ux * (consumed + step), y1 + uy * (consumed + step));
                consumed += step;
                phase += step;
            } else {
                float rem_gap = cycle - phase;
                float step = (rem_gap < rem_in_seg) ? rem_gap : rem_in_seg;
                consumed += step;
                phase += step;
                if (phase >= cycle)
                    phase -= cycle;
            }
        }
    }
}

static void draw_arc_colored(float cx, float cy, float rx, float ry, int angle1, int angle2, SDL_FColor fc)
{
    if (!tng_renderer || rx <= 0.0f || ry <= 0.0f || angle2 == 0) return;

    float max_r = (rx > ry) ? rx : ry;
    int full_segments = (int)ceilf(max_r * 3.0f);
    if (full_segments < 48) full_segments = 48;
    if (full_segments > 96) full_segments = 96;

    int abs_a2 = abs(angle2);
    bool full_circle = (abs_a2 >= 360 * 64);
    int num_segments = full_segments;
    if (!full_circle) {
        num_segments = (abs_a2 * full_segments) / (360 * 64);
        if (num_segments < 8) num_segments = 8;
        if (num_segments > 96) num_segments = 96;
    }

    float start_rad = (float)angle1 * (float)(M_PI / (180.0 * 64.0));
    float span_rad  = full_circle ? (float)(2.0 * M_PI) : (float)angle2 * (float)(M_PI / (180.0 * 64.0));

    const float half_w  = 0.40f;
    const float feather = 0.85f;

    float rx0 = fmaxf(0.0f, rx - half_w - feather);
    float ry0 = fmaxf(0.0f, ry - half_w - feather);
    float rx1 = fmaxf(0.0f, rx - half_w);
    float ry1 = fmaxf(0.0f, ry - half_w);
    float rx2 = rx + half_w;
    float ry2 = ry + half_w;
    float rx3 = rx + half_w + feather;
    float ry3 = ry + half_w + feather;

    SDL_FColor fc_zero = { fc.r, fc.g, fc.b, 0.0f };

    SDL_Vertex verts[(96 + 1) * 4];
    int indices[96 * 18];

    for (int i = 0; i <= num_segments; i++) {
        int vbase = i * 4;
        if (full_circle && i == num_segments) {
            verts[vbase + 0] = verts[0];
            verts[vbase + 1] = verts[1];
            verts[vbase + 2] = verts[2];
            verts[vbase + 3] = verts[3];
            break;
        }

        float a = start_rad + (span_rad * (float)i) / (float)num_segments;
        float ca = cosf(a);
        float sa = -sinf(a);

        verts[vbase + 0].position = (SDL_FPoint){ cx + ca * rx0, cy + sa * ry0 };
        verts[vbase + 0].color = fc_zero;
        verts[vbase + 0].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

        verts[vbase + 1].position = (SDL_FPoint){ cx + ca * rx1, cy + sa * ry1 };
        verts[vbase + 1].color = fc;
        verts[vbase + 1].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

        verts[vbase + 2].position = (SDL_FPoint){ cx + ca * rx2, cy + sa * ry2 };
        verts[vbase + 2].color = fc;
        verts[vbase + 2].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

        verts[vbase + 3].position = (SDL_FPoint){ cx + ca * rx3, cy + sa * ry3 };
        verts[vbase + 3].color = fc_zero;
        verts[vbase + 3].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };
    }

    int idx = 0;
    for (int i = 0; i < num_segments; i++) {
        int b0 = i * 4;
        int b1 = (i + 1) * 4;
        for (int r = 0; r < 3; r++) {
            indices[idx++] = b0 + r;
            indices[idx++] = b1 + r;
            indices[idx++] = b0 + r + 1;

            indices[idx++] = b0 + r + 1;
            indices[idx++] = b1 + r;
            indices[idx++] = b1 + r + 1;
        }
    }

    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(tng_renderer, NULL, verts, (num_segments + 1) * 4, indices, idx);
}

void SDL3_Draw_Dashed_Circle(float cx, float cy, float radius, int color_idx, int num_dashes)
{
    if (!tng_renderer || radius <= 0.0f) return;
    if (num_dashes < 4) num_dashes = 12;

    SDL_FColor fc = SDL3_Get_FColor(color_idx);
    int dash_angle = (360 * 64) / (num_dashes * 2);
    if (dash_angle < 1) dash_angle = 1;

    for (int i = 0; i < num_dashes; i++) {
        int a1 = (i * 2) * dash_angle;
        draw_arc_colored(cx, cy, radius, radius, a1, dash_angle, fc);
    }
}

void SDL3_Draw_Arc(float cx, float cy, float rx, float ry, int angle1, int angle2, int color_idx)
{
    if (!tng_renderer) return;
    draw_arc_colored(cx, cy, rx, ry, angle1, angle2, SDL3_Get_FColor(color_idx));
}

void SDL3_Draw_Arc_RGB(float cx, float cy, float rx, float ry, int angle1, int angle2, Uint32 rgb)
{
    if (!tng_renderer) return;
    draw_arc_colored(cx, cy, rx, ry, angle1, angle2, SDL3_Get_FColor_RGB(rgb));
}

void SDL3_Draw_Lines(const SDL_FPoint *points, int count, int color_idx, bool close_loop)
{
    if (!tng_renderer || !points || count < 2) return;
    SDL3_Set_Draw_Color(color_idx);
    SDL_RenderLines(tng_renderer, points, count);
    if (close_loop && count >= 3) {
        SDL_RenderLine(tng_renderer, points[count - 1].x, points[count - 1].y,
                       points[0].x, points[0].y);
    }
}

void SDL3_Draw_Lines_RGB(const SDL_FPoint *points, int count, Uint32 rgb, bool close_loop)
{
    if (!tng_renderer || !points || count < 2) return;
    SDL3_Set_Draw_Color_RGB(rgb);
    SDL_RenderLines(tng_renderer, points, count);
    if (close_loop && count >= 3) {
        SDL_RenderLine(tng_renderer, points[count - 1].x, points[count - 1].y,
                       points[0].x, points[0].y);
    }
}

void SDL3_Draw_Rect(float x, float y, float w, float h, int color_idx)
{
    if (!tng_renderer) return;
    SDL3_Set_Draw_Color(color_idx);
    SDL_FRect r = { x, y, w, h };
    SDL_RenderRect(tng_renderer, &r);
}

void SDL3_Fill_Rect(float x, float y, float w, float h, int color_idx)
{
    if (!tng_renderer) return;
    SDL3_Set_Draw_Color(color_idx);
    SDL_FRect r = { x, y, w, h };
    SDL_RenderFillRect(tng_renderer, &r);
}

void SDL3_Draw_Circle(float cx, float cy, float radius, int color_idx)
{
    if (!tng_renderer || radius <= 0.0f) return;
    draw_arc_colored(cx, cy, radius, radius, 0, 360 * 64, SDL3_Get_FColor(color_idx));
}

void SDL3_Fill_Circle(float cx, float cy, float radius, int color_idx)
{
    if (!tng_renderer || radius <= 0.0f) return;

    SDL_FColor fc = SDL3_Get_FColor(color_idx);
    SDL_FColor fc_zero = { fc.r, fc.g, fc.b, 0.0f };

    const int num_segments = 64;
    float r_core = fmaxf(0.0f, radius - 0.5f);
    float r_edge = radius + 0.5f;

    SDL_Vertex vertices[1 + num_segments * 2];
    int indices[num_segments * 9];

    vertices[0].position = (SDL_FPoint){ cx, cy };
    vertices[0].color = fc;
    vertices[0].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

    int idx = 0;
    for (int i = 0; i < num_segments; i++) {
        float angle = (float)(i * (2.0 * M_PI / num_segments));
        float ca = cosf(angle);
        float sa = sinf(angle);
        int next = (i + 1 == num_segments) ? 0 : (i + 1);

        int v_core = 1 + i;
        int v_edge = 1 + num_segments + i;
        int v_core_next = 1 + next;
        int v_edge_next = 1 + num_segments + next;

        vertices[v_core].position = (SDL_FPoint){ cx + ca * r_core, cy + sa * r_core };
        vertices[v_core].color = fc;
        vertices[v_core].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

        vertices[v_edge].position = (SDL_FPoint){ cx + ca * r_edge, cy + sa * r_edge };
        vertices[v_edge].color = fc_zero;
        vertices[v_edge].tex_coord = (SDL_FPoint){ 0.0f, 0.0f };

        indices[idx++] = 0;
        indices[idx++] = v_core;
        indices[idx++] = v_core_next;

        indices[idx++] = v_core;
        indices[idx++] = v_edge;
        indices[idx++] = v_core_next;

        indices[idx++] = v_core_next;
        indices[idx++] = v_edge;
        indices[idx++] = v_edge_next;
    }

    SDL_SetRenderDrawBlendMode(tng_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(tng_renderer, NULL, vertices, 1 + num_segments * 2, indices, idx);
}

static float polygon_signed_area(const SDL_FPoint *pts, int n)
{
    float area = 0.0f;
    for (int i = 0; i < n; i++) {
        int next = (i + 1 == n) ? 0 : i + 1;
        area += pts[i].x * pts[next].y - pts[next].x * pts[i].y;
    }
    return area * 0.5f;
}

static bool is_point_in_triangle(SDL_FPoint p, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c)
{
    float cp1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    float cp2 = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
    float cp3 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);

    bool has_neg = (cp1 < -1e-5f) || (cp2 < -1e-5f) || (cp3 < -1e-5f);
    bool has_pos = (cp1 > 1e-5f)  || (cp2 > 1e-5f)  || (cp3 > 1e-5f);

    return !(has_neg && has_pos);
}

int SDL3_Triangulate_Polygon(const SDL_FPoint *points, int count, int *out_indices, int max_indices)
{
    if (count < 3 || !points || !out_indices) return 0;
    if (count == 3) {
        if (max_indices >= 3) {
            out_indices[0] = 0;
            out_indices[1] = 1;
            out_indices[2] = 2;
            return 3;
        }
        return 0;
    }

    int *V = (int *)malloc(sizeof(int) * count);
    if (!V) return 0;

    float area = polygon_signed_area(points, count);
    bool ccw = (area > 0.0f);

    for (int i = 0; i < count; i++) {
        V[i] = i;
    }

    int remaining = count;
    int out_idx = 0;
    int count_fails = 0;
    int curr = 0;

    while (remaining > 2 && count_fails < remaining * 2 && (out_idx + 3 <= max_indices)) {
        int prev = (curr + remaining - 1) % remaining;
        int next = (curr + 1) % remaining;

        SDL_FPoint a = points[V[prev]];
        SDL_FPoint b = points[V[curr]];
        SDL_FPoint c = points[V[next]];

        float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        bool is_convex = ccw ? (cross > 1e-5f) : (cross < -1e-5f);

        bool is_ear = is_convex;
        if (is_ear) {
            for (int j = 0; j < remaining; j++) {
                if (j == prev || j == curr || j == next) continue;
                if (is_point_in_triangle(points[V[j]], a, b, c)) {
                    is_ear = false;
                    break;
                }
            }
        }

        if (is_ear) {
            out_indices[out_idx++] = V[prev];
            out_indices[out_idx++] = V[curr];
            out_indices[out_idx++] = V[next];

            for (int k = curr; k < remaining - 1; k++) {
                V[k] = V[k + 1];
            }
            remaining--;
            curr %= remaining;
            count_fails = 0;
        } else {
            curr = (curr + 1) % remaining;
            count_fails++;
        }
    }

    /* Fallback if any vertices remain: simple fan */
    if (remaining > 2 && out_idx + (remaining - 2) * 3 <= max_indices) {
        for (int i = 1; i < remaining - 1; i++) {
            out_indices[out_idx++] = V[0];
            out_indices[out_idx++] = V[i];
            out_indices[out_idx++] = V[i + 1];
        }
    }

    free(V);
    return out_idx;
}

void SDL3_Fill_Polygon(const SDL_FPoint *points, int count, int color_idx)
{
    if (!tng_renderer || !points || count < 3) return;

    SDL_FColor fc = SDL3_Get_FColor(color_idx);
    SDL_Vertex *vertices = (SDL_Vertex *)malloc(sizeof(SDL_Vertex) * count);
    int max_indices = (count - 2) * 3;
    int *indices = (int *)malloc(sizeof(int) * max_indices);

    if (!vertices || !indices) {
        if (vertices) free(vertices);
        if (indices) free(indices);
        return;
    }

    for (int i = 0; i < count; i++) {
        vertices[i].position = points[i];
        vertices[i].color = fc;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }

    int num_indices = SDL3_Triangulate_Polygon(points, count, indices, max_indices);
    if (num_indices >= 3) {
        SDL_RenderGeometry(tng_renderer, NULL, vertices, count, indices, num_indices);
    }

    free(vertices);
    free(indices);
}

void SDL3_Fill_Polygon_RGB(const SDL_FPoint *points, int count, Uint32 rgb)
{
    if (!tng_renderer || !points || count < 3) return;

    SDL_FColor fc = SDL3_Get_FColor_RGB(rgb);
    SDL_Vertex *vertices = (SDL_Vertex *)malloc(sizeof(SDL_Vertex) * count);
    int max_indices = (count - 2) * 3;
    int *indices = (int *)malloc(sizeof(int) * max_indices);

    if (!vertices || !indices) {
        if (vertices) free(vertices);
        if (indices) free(indices);
        return;
    }

    for (int i = 0; i < count; i++) {
        vertices[i].position = points[i];
        vertices[i].color = fc;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }

    int num_indices = SDL3_Triangulate_Polygon(points, count, indices, max_indices);
    if (num_indices >= 3) {
        SDL_RenderGeometry(tng_renderer, NULL, vertices, count, indices, num_indices);
    }

    free(vertices);
    free(indices);
}

void SDL3_Render_Textured_Polygon(const SDL_FPoint *points, int count, SDL_Texture *texture, float origin_x, float origin_y, int tex_w, int tex_h)
{
    if (!tng_renderer || !points || count < 3) return;

    if (!texture || tex_w <= 0 || tex_h <= 0) {
        SDL3_Fill_Polygon(points, count, XP_COLOR_BLUE);
        return;
    }

    SDL_Vertex *vertices = (SDL_Vertex *)malloc(sizeof(SDL_Vertex) * count);
    int max_indices = (count - 2) * 3;
    int *indices = (int *)malloc(sizeof(int) * max_indices);

    if (!vertices || !indices) {
        if (vertices) free(vertices);
        if (indices) free(indices);
        return;
    }

    for (int i = 0; i < count; i++) {
        vertices[i].position = points[i];
        vertices[i].color = (SDL_FColor){ 1.0f, 1.0f, 1.0f, 1.0f };
        vertices[i].tex_coord.x = (points[i].x - origin_x) / (float)tex_w;
        vertices[i].tex_coord.y = (points[i].y - origin_y) / (float)tex_h;
    }

    int num_indices = SDL3_Triangulate_Polygon(points, count, indices, max_indices);
    if (num_indices >= 3) {
        SDL_RenderGeometry(tng_renderer, texture, vertices, count, indices, num_indices);
    }

    free(vertices);
    free(indices);
}

/* -------------------------------------------------------------
 * 16x16 XBM Item Icon Textures
 * ------------------------------------------------------------- */

static const unsigned char *item_xbm_bits[NUM_ITEMS] = {
    itemEnergyPack_bits,        /* 0: ITEM_FUEL */
    itemWideangleShot_bits,     /* 1: ITEM_WIDEANGLE */
    itemRearShot_bits,          /* 2: ITEM_REARSHOT */
    itemAfterburner_bits,       /* 3: ITEM_AFTERBURNER */
    itemCloakingDevice_bits,    /* 4: ITEM_CLOAK */
    itemSensorPack_bits,        /* 5: ITEM_SENSOR */
    itemTransporter_bits,       /* 6: ITEM_TRANSPORTER */
    itemTank_bits,              /* 7: ITEM_TANK */
    itemMinePack_bits,          /* 8: ITEM_MINE */
    itemRocketPack_bits,        /* 9: ITEM_MISSILE */
    itemEcm_bits,               /* 10: ITEM_ECM */
    itemLaser_bits,             /* 11: ITEM_LASER */
    itemEmergencyThrust_bits,   /* 12: ITEM_EMERGENCY_THRUST */
    itemTractorBeam_bits,       /* 13: ITEM_TRACTOR_BEAM */
    itemAutopilot_bits,         /* 14: ITEM_AUTOPILOT */
    itemEmergencyShield_bits,   /* 15: ITEM_EMERGENCY_SHIELD */
    itemDeflector_bits,         /* 16: ITEM_DEFLECTOR */
    itemHyperJump_bits,         /* 17: ITEM_HYPERJUMP */
    itemPhasingDevice_bits,     /* 18: ITEM_PHASING */
    itemMirror_bits,            /* 19: ITEM_MIRROR */
    itemArmor_bits              /* 20: ITEM_ARMOR */
};

static SDL_Texture *item_textures[NUM_ITEMS] = { NULL };

int SDL3_Init_Draw(void)
{
    if (!tng_renderer) return -1;

    for (int i = 0; i < NUM_ITEMS; i++) {
        SDL_Surface *surf = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
        if (!surf) continue;

        memset(surf->pixels, 0, surf->pitch * 16);
        Uint32 *pixels = (Uint32 *)surf->pixels;
        int pitch_pixels = surf->pitch / sizeof(Uint32);
        const unsigned char *bits = item_xbm_bits[i];

        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                int byte_idx = y * 2 + (x / 8);
                int bit = (bits[byte_idx] >> (x % 8)) & 1;
                if (bit) {
                    pixels[y * pitch_pixels + x] = 0xFFFFFFFF;
                }
            }
        }

        item_textures[i] = SDL_CreateTextureFromSurface(tng_renderer, surf);
        SDL_DestroySurface(surf);
        if (item_textures[i]) {
            SDL_SetTextureScaleMode(item_textures[i], SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(item_textures[i], SDL_BLENDMODE_BLEND);
        }
    }
    return 0;
}

void SDL3_Cleanup_Draw(void)
{
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (item_textures[i]) {
            SDL_DestroyTexture(item_textures[i]);
            item_textures[i] = NULL;
        }
    }
}

void SDL3_Draw_Item_Icon(int item_type, float x, float y, int color_idx)
{
    if (item_type < 0 || item_type >= NUM_ITEMS || !item_textures[item_type] || !tng_renderer)
        return;

    SDL_Color col = SDL3_Get_Color(color_idx);
    SDL_SetTextureColorMod(item_textures[item_type], col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(item_textures[item_type], col.a);

    SDL_FRect dst = { x, y, (float)ITEM_SIZE, (float)ITEM_SIZE };
    SDL_RenderTexture(tng_renderer, item_textures[item_type], NULL, &dst);
}

void Gui_paint_item_symbol(int type, int x, int y, int color_idx)
{
    if (type < 0 || type >= NUM_ITEMS)
        return;

    int sx = PLAYFIELD_X + WINSCALE(x);
    int sy = WINSCALE(y);
    if (texturedObjects) {
        Bitmap_paint(0, BM_ALL_ITEMS, sx, sy, type);
    } else {
        SDL3_Draw_Item_Icon(type, (float)sx, (float)sy, color_idx);
    }
}

/* -------------------------------------------------------------
 * Segment, Arc and Rectangle Batching (paintdata.h)
 * ------------------------------------------------------------- */

typedef struct {
    float x1, y1, x2, y2;
} segment_entry_t;

typedef struct {
    segment_entry_t *entries;
    int count;
    int capacity;
} segment_batch_t;

static segment_batch_t seg_batches[XP_NUM_PALETTE_COLORS];

void Segment_start(void)
{
    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        seg_batches[i].count = 0;
    }
}

int Segment_add(int color, int x1, int y1, int x2, int y2)
{
    if (color < 0 || color >= XP_NUM_PALETTE_COLORS) color = XP_COLOR_WHITE;

    segment_batch_t *b = &seg_batches[color];
    if (b->count >= b->capacity) {
        int new_cap = (b->capacity == 0) ? 512 : b->capacity * 2;
        segment_entry_t *new_entries = (segment_entry_t *)realloc(b->entries, sizeof(segment_entry_t) * new_cap);
        if (!new_entries) return -1;
        b->entries = new_entries;
        b->capacity = new_cap;
    }

    b->entries[b->count].x1 = (float)(PLAYFIELD_X + WINSCALE(x1));
    b->entries[b->count].y1 = (float)(WINSCALE(y1));
    b->entries[b->count].x2 = (float)(PLAYFIELD_X + WINSCALE(x2));
    b->entries[b->count].y2 = (float)(WINSCALE(y2));
    b->count++;
    return 0;
}

void Segment_end(void)
{
    if (!tng_renderer) return;

    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        segment_batch_t *b = &seg_batches[i];
        if (b->count > 0) {
            SDL3_Set_Draw_Color(i);
            for (int j = 0; j < b->count; j++) {
                SDL_RenderLine(tng_renderer,
                               b->entries[j].x1, b->entries[j].y1,
                               b->entries[j].x2, b->entries[j].y2);
            }
            b->count = 0;
        }
    }
}

/* Arcs */
typedef struct {
    float cx, cy, rx, ry;
    int a1, a2;
    bool is_rgb;
    Uint32 rgb;
} arc_entry_t;

typedef struct {
    arc_entry_t *entries;
    int count;
    int capacity;
} arc_batch_t;

static arc_batch_t arc_batches[XP_NUM_PALETTE_COLORS];

void Arc_start(void)
{
    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        arc_batches[i].count = 0;
    }
}

int Arc_add(int color, int x, int y, int width, int height, int angle1, int angle2)
{
    if (color < 0 || color >= XP_NUM_PALETTE_COLORS) color = XP_COLOR_WHITE;

    arc_batch_t *b = &arc_batches[color];
    if (b->count >= b->capacity) {
        int new_cap = (b->capacity == 0) ? 64 : b->capacity * 2;
        arc_entry_t *new_entries = (arc_entry_t *)realloc(b->entries, sizeof(arc_entry_t) * new_cap);
        if (!new_entries) return -1;
        b->entries = new_entries;
        b->capacity = new_cap;
    }

    float sx1 = (float)PLAYFIELD_X + WINSCALE_f((float)x);
    float sy1 = WINSCALE_f((float)y);
    float w = fmaxf(1.0f, WINSCALE_f((float)width));
    float h = fmaxf(1.0f, WINSCALE_f((float)height));
    b->entries[b->count].cx = sx1 + w * 0.5f;
    b->entries[b->count].cy = sy1 + h * 0.5f;
    b->entries[b->count].rx = w * 0.5f;
    b->entries[b->count].ry = h * 0.5f;
    b->entries[b->count].a1 = angle1;
    b->entries[b->count].a2 = angle2;
    b->entries[b->count].is_rgb = false;
    b->entries[b->count].rgb = 0;
    b->count++;
    return 0;
}

int Arc_add_rgb(unsigned long color, int fallback, int x, int y, int width, int height, int angle1, int angle2)
{
    if (fallback < 0 || fallback >= XP_NUM_PALETTE_COLORS) fallback = XP_COLOR_WHITE;
    if (Arc_add(fallback, x, y, width, height, angle1, angle2) == -1)
        return -1;
    if (fullColor) {
        arc_batch_t *b = &arc_batches[fallback];
        b->entries[b->count - 1].is_rgb = true;
        b->entries[b->count - 1].rgb = (Uint32)color;
    }
    return 0;
}

void Arc_end(void)
{
    if (!tng_renderer) return;

    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        arc_batch_t *b = &arc_batches[i];
        if (b->count > 0) {
            for (int j = 0; j < b->count; j++) {
                arc_entry_t *e = &b->entries[j];
                if (e->is_rgb) {
                    SDL3_Draw_Arc_RGB(e->cx, e->cy, e->rx, e->ry, e->a1, e->a2, e->rgb);
                } else {
                    SDL3_Draw_Arc(e->cx, e->cy, e->rx, e->ry, e->a1, e->a2, i);
                }
            }
            b->count = 0;
        }
    }
}

/* Rectangles */
typedef struct {
    float x, y, w, h;
} rect_entry_t;

typedef struct {
    rect_entry_t *entries;
    int count;
    int capacity;
} rect_batch_t;

static rect_batch_t rect_batches[XP_NUM_PALETTE_COLORS];

void Rectangle_start(void)
{
    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        rect_batches[i].count = 0;
    }
}

int Rectangle_add(int color, int x, int y, int width, int height)
{
    if (color < 0 || color >= XP_NUM_PALETTE_COLORS) color = XP_COLOR_WHITE;

    rect_batch_t *b = &rect_batches[color];
    if (b->count >= b->capacity) {
        int new_cap = (b->capacity == 0) ? 64 : b->capacity * 2;
        rect_entry_t *new_entries = (rect_entry_t *)realloc(b->entries, sizeof(rect_entry_t) * new_cap);
        if (!new_entries) return -1;
        b->entries = new_entries;
        b->capacity = new_cap;
    }

    float w = (float)WINSCALE(width);
    float h = (float)WINSCALE(height);
    if (width > 0 && w < 1.0f) w = 1.0f;
    if (height > 0 && h < 1.0f) h = 1.0f;

    b->entries[b->count].x = (float)(PLAYFIELD_X + WINSCALE(x));
    b->entries[b->count].y = (float)(WINSCALE(y));
    b->entries[b->count].w = w;
    b->entries[b->count].h = h;
    b->count++;
    return 0;
}

void Rectangle_end(void)
{
    if (!tng_renderer) return;

    for (int i = 0; i < XP_NUM_PALETTE_COLORS; i++) {
        rect_batch_t *b = &rect_batches[i];
        if (b->count > 0) {
            SDL3_Set_Draw_Color(i);
            for (int j = 0; j < b->count; j++) {
                SDL_FRect r = { b->entries[j].x, b->entries[j].y, b->entries[j].w, b->entries[j].h };
                SDL_RenderFillRect(tng_renderer, &r);
            }
            b->count = 0;
        }
    }
}
