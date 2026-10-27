/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Authentic 16-color XPilot Palette & TrueColor Handlers matching ng-x11
 */

#include "sdl3_client.h"
#include <ctype.h>

#define XP_COLOR0  "#000000" /* black */
#define XP_COLOR1  "#FFFFFF" /* white */
#define XP_COLOR2  "#4E7CFF" /* "xpblue" */
#define XP_COLOR3  "#FF3A27" /* "xpred" */
#define XP_COLOR4  "#33BB44" /* "xpgreen" */
#define XP_COLOR5  "#992200"
#define XP_COLOR6  "#BB7700"
#define XP_COLOR7  "#EE9900"
#define XP_COLOR8  "#002299"
#define XP_COLOR9  "#CC4400"
#define XP_COLOR10 "#DD8800"
#define XP_COLOR11 "#FFBB11" /* "xpyellow" */
#define XP_COLOR12 "#9F9F9F"
#define XP_COLOR13 "#5F5F5F"
#define XP_COLOR14 "#DFDFDF"
#define XP_COLOR15 "#202020"

static const SDL_Color default_xpilot_palette[XP_NUM_PALETTE_COLORS] = {
    {   0,   0,   0, 255 }, /* 0: BLACK */
    { 255, 255, 255, 255 }, /* 1: WHITE */
    {  78, 124, 255, 255 }, /* 2: XPBLUE #4E7CFF */
    { 255,  58,  39, 255 }, /* 3: XPRED #FF3A27 */
    {  51, 187,  68, 255 }, /* 4: XPGREEN #33BB44 */
    { 153,  34,   0, 255 }, /* 5: #992200 */
    { 187, 119,   0, 255 }, /* 6: #BB7700 */
    { 238, 153,   0, 255 }, /* 7: #EE9900 */
    {   0,  34, 153, 255 }, /* 8: #002299 */
    { 204,  68,   0, 255 }, /* 9: #CC4400 */
    { 221, 136,   0, 255 }, /* 10: #DD8800 */
    { 255, 187,  17, 255 }, /* 11: XPYELLOW #FFBB11 */
    { 159, 159, 159, 255 }, /* 12: GRAY #9F9F9F */
    {  95,  95,  95, 255 }, /* 13: DARK GRAY #5F5F5F */
    { 223, 223, 223, 255 }, /* 14: LIGHT GRAY #DFDFDF */
    {  32,  32,  32, 255 }, /* 15: CHARCOAL #202020 */
    { 255, 208, 145, 255 }  /* 16: SMARTBOT VERY LIGHT ORANGE #FFD091 */
};

static SDL_Color xpilot_palette[XP_NUM_PALETTE_COLORS] = {
    {   0,   0,   0, 255 }, /* 0: BLACK */
    { 255, 255, 255, 255 }, /* 1: WHITE */
    {  78, 124, 255, 255 }, /* 2: XPBLUE #4E7CFF */
    { 255,  58,  39, 255 }, /* 3: XPRED #FF3A27 */
    {  51, 187,  68, 255 }, /* 4: XPGREEN #33BB44 */
    { 153,  34,   0, 255 }, /* 5: #992200 */
    { 187, 119,   0, 255 }, /* 6: #BB7700 */
    { 238, 153,   0, 255 }, /* 7: #EE9900 */
    {   0,  34, 153, 255 }, /* 8: #002299 */
    { 204,  68,   0, 255 }, /* 9: #CC4400 */
    { 221, 136,   0, 255 }, /* 10: #DD8800 */
    { 255, 187,  17, 255 }, /* 11: XPYELLOW #FFBB11 */
    { 159, 159, 159, 255 }, /* 12: GRAY #9F9F9F */
    {  95,  95,  95, 255 }, /* 13: DARK GRAY #5F5F5F */
    { 223, 223, 223, 255 }, /* 14: LIGHT GRAY #DFDFDF */
    {  32,  32,  32, 255 }, /* 15: CHARCOAL #202020 */
    { 255, 208, 145, 255 }  /* 16: SMARTBOT VERY LIGHT ORANGE #FFD091 */
};

static char color_names[MAX_COLORS][MAX_COLOR_LEN];
bool fullColor = true;
bool texturedObjects = true;
int  maxColors = MAX_COLORS;
char sparkColors[MSG_LEN] = "5,6,7,3";
int  spark_color[MAX_COLORS];

int  wallColor   = 2;
int  decorColor  = 6;
int  windowColor = 8;
int  buttonColor = 2;
int  borderColor = 1;

static bool Parse_hex_color(const char *str, SDL_Color *out)
{
    if (!str) return false;
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '#') {
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(str + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            out->r = (Uint8)r;
            out->g = (Uint8)g;
            out->b = (Uint8)b;
            out->a = 255;
            return true;
        }
    }
    return false;
}

void SDL3_Init_Colors(void)
{
    for (int i = 0; i < MAX_COLORS; i++) {
        xpilot_palette[i] = default_xpilot_palette[i];
        if (color_names[i][0] != '\0') {
            Parse_hex_color(color_names[i], &xpilot_palette[i]);
        }
    }
    Init_spark_colors();
}

void Init_spark_colors(void)
{
    char buf[MSG_LEN];
    char *src, *dst;
    unsigned col;
    int i;

    if (maxColors <= 0)
        maxColors = MAX_COLORS;

    num_spark_colors = 0;
    for (src = sparkColors; *src && (num_spark_colors < 9); src++) {
        if (isascii((unsigned char)*src) && isdigit((unsigned char)*src)) {
            dst = &buf[0];
            do {
                *dst++ = *src++;
            } while (*src &&
                     isascii((unsigned char)*src) &&
                     isdigit((unsigned char)*src) &&
                     ((size_t)(dst - buf) < (sizeof(buf) - 1)));
            *dst = '\0';
            src--;
            if (sscanf(buf, "%u", &col) == 1) {
                if (col < (unsigned)maxColors)
                    spark_color[num_spark_colors++] = (int)col;
            }
        }
    }
    if (num_spark_colors == 0) {
        if (maxColors <= 8) {
            for (i = 5; i < maxColors; i++)
                spark_color[num_spark_colors++] = i;
        } else {
            for (i = 5; i < 12; i++)
                spark_color[num_spark_colors++] = i;
        }
        spark_color[num_spark_colors++] = RED;
    }
    for (i = num_spark_colors; i < MAX_COLORS; i++)
        spark_color[i] = spark_color[num_spark_colors - 1];
}

void Colors_init_style_colors(void)
{
    for (int i = 0; i < num_polygon_styles; i++) {
        polygon_styles[i].color = fullColor ?
            (unsigned long)polygon_styles[i].rgb : (unsigned long)wallColor;
    }
    for (int i = 0; i < num_edge_styles; i++) {
        edge_styles[i].color = fullColor ?
            (unsigned long)edge_styles[i].rgb : (unsigned long)wallColor;
    }
}

void SDL3_Set_Draw_Color(int color_idx)
{
    if (!tng_renderer)
        return;

    if (color_idx < 0 || color_idx >= XP_NUM_PALETTE_COLORS)
        color_idx = XP_COLOR_WHITE;

    SDL_SetRenderDrawColor(tng_renderer,
                           xpilot_palette[color_idx].r,
                           xpilot_palette[color_idx].g,
                           xpilot_palette[color_idx].b,
                           xpilot_palette[color_idx].a);
}

void SDL3_Set_Draw_Color_RGB(Uint32 rgb)
{
    if (!tng_renderer)
        return;

    Uint8 r = (Uint8)((rgb >> 16) & 0xff);
    Uint8 g = (Uint8)((rgb >> 8) & 0xff);
    Uint8 b = (Uint8)(rgb & 0xff);
    SDL_SetRenderDrawColor(tng_renderer, r, g, b, 255);
}

bool SDL3_Parse_Hex_Color(const char *str, SDL_Color *out)
{
    return Parse_hex_color(str, out);
}

SDL_Color SDL3_Get_Color(int color_idx)
{
    if (color_idx < 0 || color_idx >= XP_NUM_PALETTE_COLORS)
        color_idx = XP_COLOR_WHITE;

    return xpilot_palette[color_idx];
}

SDL_Color SDL3_Get_Color_RGB(Uint32 rgb)
{
    SDL_Color c;
    c.r = (Uint8)((rgb >> 16) & 0xff);
    c.g = (Uint8)((rgb >> 8) & 0xff);
    c.b = (Uint8)(rgb & 0xff);
    c.a = 255;
    return c;
}

SDL_FColor SDL3_Get_FColor(int color_idx)
{
    SDL_Color c = SDL3_Get_Color(color_idx);
    SDL_FColor fc = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f };
    return fc;
}

SDL_FColor SDL3_Get_FColor_RGB(Uint32 rgb)
{
    SDL_FColor fc = {
        ((rgb >> 16) & 0xff) / 255.0f,
        ((rgb >> 8) & 0xff) / 255.0f,
        (rgb & 0xff) / 255.0f,
        1.0f
    };
    return fc;
}

static bool Set_sparkColors(xp_option_t *opt, const char *val)
{
    UNUSED_PARAM(opt);
    strlcpy(sparkColors, val, sizeof(sparkColors));
    Init_spark_colors();
    return true;
}

static bool Set_maxColors(xp_option_t *opt, int val)
{
    UNUSED_PARAM(opt);
    if (val == 4 || val == 8)
        maxColors = val;
    else
        maxColors = MAX_COLORS;
    return true;
}

static bool Set_color(xp_option_t *opt, const char *val)
{
    char *buf = (char *)Option_get_private_data(opt);
    if (!buf || !val) return false;
    strlcpy(buf, val, MAX_COLOR_LEN);
    int idx = (int)((buf - &color_names[0][0]) / MAX_COLOR_LEN);
    if (idx >= 0 && idx < MAX_COLORS) {
        Parse_hex_color(buf, &xpilot_palette[idx]);
    }
    return true;
}

static bool Set_fullColor(xp_option_t *opt, bool val)
{
    UNUSED_PARAM(opt);
    fullColor = val;
    if (!fullColor)
        texturedObjects = false;
    Colors_init_style_colors();
    scoresChanged = true;
    return true;
}

static bool Set_texturedObjects(xp_option_t *opt, bool val)
{
    UNUSED_PARAM(opt);
    texturedObjects = fullColor ? val : false;
    return true;
}

static xp_option_t color_options[] = {
    XP_BOOL_OPTION("fullColor", true, &fullColor, Set_fullColor,
                   XP_OPTFLAG_CONFIG_DEFAULT,
                   "Whether to use colors as close as possible to the specified ones.\n"),
    XP_BOOL_OPTION("texturedObjects", true, &texturedObjects, Set_texturedObjects,
                   XP_OPTFLAG_CONFIG_DEFAULT,
                   "Whether to draw certain game objects with textures.\n"),
    XP_INT_OPTION("maxColors", MAX_COLORS, 4, MAX_COLORS, &maxColors, Set_maxColors,
                  XP_OPTFLAG_DEFAULT,
                  "The number of colors to use.\n"),
    XP_STRING_OPTION("color0",  XP_COLOR0,  color_names[0],  MAX_COLOR_LEN, Set_color, color_names[0],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the first color.\n"),
    XP_STRING_OPTION("color1",  XP_COLOR1,  color_names[1],  MAX_COLOR_LEN, Set_color, color_names[1],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the second color.\n"),
    XP_STRING_OPTION("color2",  XP_COLOR2,  color_names[2],  MAX_COLOR_LEN, Set_color, color_names[2],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the third color.\n"),
    XP_STRING_OPTION("color3",  XP_COLOR3,  color_names[3],  MAX_COLOR_LEN, Set_color, color_names[3],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the fourth color.\n"),
    XP_STRING_OPTION("color4",  XP_COLOR4,  color_names[4],  MAX_COLOR_LEN, Set_color, color_names[4],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the fifth color.\n"),
    XP_STRING_OPTION("color5",  XP_COLOR5,  color_names[5],  MAX_COLOR_LEN, Set_color, color_names[5],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the sixth color.\n"),
    XP_STRING_OPTION("color6",  XP_COLOR6,  color_names[6],  MAX_COLOR_LEN, Set_color, color_names[6],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the seventh color.\n"),
    XP_STRING_OPTION("color7",  XP_COLOR7,  color_names[7],  MAX_COLOR_LEN, Set_color, color_names[7],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the eighth color.\n"),
    XP_STRING_OPTION("color8",  XP_COLOR8,  color_names[8],  MAX_COLOR_LEN, Set_color, color_names[8],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the ninth color.\n"),
    XP_STRING_OPTION("color9",  XP_COLOR9,  color_names[9],  MAX_COLOR_LEN, Set_color, color_names[9],  NULL, XP_OPTFLAG_DEFAULT, "The color value for the tenth color.\n"),
    XP_STRING_OPTION("color10", XP_COLOR10, color_names[10], MAX_COLOR_LEN, Set_color, color_names[10], NULL, XP_OPTFLAG_DEFAULT, "The color value for the eleventh color.\n"),
    XP_STRING_OPTION("color11", XP_COLOR11, color_names[11], MAX_COLOR_LEN, Set_color, color_names[11], NULL, XP_OPTFLAG_DEFAULT, "The color value for the twelfth color.\n"),
    XP_STRING_OPTION("color12", XP_COLOR12, color_names[12], MAX_COLOR_LEN, Set_color, color_names[12], NULL, XP_OPTFLAG_DEFAULT, "The color value for the thirteenth color.\n"),
    XP_STRING_OPTION("color13", XP_COLOR13, color_names[13], MAX_COLOR_LEN, Set_color, color_names[13], NULL, XP_OPTFLAG_DEFAULT, "The color value for the fourteenth color.\n"),
    XP_STRING_OPTION("color14", XP_COLOR14, color_names[14], MAX_COLOR_LEN, Set_color, color_names[14], NULL, XP_OPTFLAG_DEFAULT, "The color value for the fifteenth color.\n"),
    XP_STRING_OPTION("color15", XP_COLOR15, color_names[15], MAX_COLOR_LEN, Set_color, color_names[15], NULL, XP_OPTFLAG_DEFAULT, "The color value for the sixteenth color.\n"),
    XP_STRING_OPTION("sparkColors", "5,6,7,3", sparkColors, sizeof(sparkColors),
                     Set_sparkColors, NULL, NULL, XP_OPTFLAG_DEFAULT,
                     "Which color numbers to use for spark and debris particles.\n"),
    COLOR_INDEX_OPTION("wallColor",   2, &wallColor,   "Which color number to use for drawing walls.\n"),
    COLOR_INDEX_OPTION("decorColor",  6, &decorColor,  "Which color number to use for drawing decorations.\n"),
    COLOR_INDEX_OPTION("windowColor", 8, &windowColor, "Which color number to use for drawing windows.\n"),
    COLOR_INDEX_OPTION("buttonColor", 2, &buttonColor, "Which color number to use for drawing buttons.\n"),
    COLOR_INDEX_OPTION("borderColor", 1, &borderColor, "Which color number to use for drawing borders.\n"),
};

void Store_color_options(void)
{
    STORE_OPTIONS(color_options);
}
