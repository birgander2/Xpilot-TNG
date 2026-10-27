/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Authentic HUD, Dials, Owned Items, Lock, Score Objects, Meters, and Messages (matching ng-x11)
 */

#include "sdl3_client.h"

int hudColor = 2;                       /* Color index for HUD drawing */
static int hudHLineColor = 0;           /* Color index for horiz. HUD line drawing */
static int hudVLineColor = 0;           /* Color index for vert. HUD line drawing */
static int hudItemsColor = 2;           /* Color index for HUD items drawing */
static int hudRadarEnemyColor = 3;      /* Color index for enemy hudradar dots */
static int hudRadarOtherColor = 2;      /* Color index for other hudradar dots */
static int hudLockColor = 0;            /* Color index for lock on HUD drawing */
static int fuelGaugeColor = 0;          /* Color index for fuel gauge drawing */
static int dirPtrColor = 0;             /* Color index for dirptr drawing */
int messagesColor = 12;                 /* Color index for messages */
int oldMessagesColor = 13;              /* Color index for old messages */
static char talkMessagesRGB[16] = "#E6F4FF";   /* talk messages (top), "" = messagesColor */
static char gameMessagesRGB[16] = "#4CFF7A";   /* server messages (bottom), "" = messagesColor */
static bool messageShadow = true;       /* dark shadow behind message text */
static int msgScanBallColor = 3;        /* Color index for ball msg */
static int msgScanSafeColor = 4;        /* Color index for safe msg */
static int msgScanCoverColor = 2;       /* Color index for cover msg */
static int msgScanPopColor = 11;        /* Color index for pop msg */
static int fuelMeterColor = 0;          /* Color index for fuel meter */
static int powerMeterColor = 0;         /* Color index for power meter */
static int turnSpeedMeterColor = 0;     /* Color index for turnspeed meter */
static int packetSizeMeterColor = 0;    /* Color index for packet size meter */
static int packetLossMeterColor = 3;    /* Color index for packet loss meter */
static int packetDropMeterColor = 3;    /* Color index for packet drop meter */
static int packetLagMeterColor = 3;     /* Color index for packet lag meter */
static int temporaryMeterColor = 3;     /* Color index for temporary meter drawing */
static int meterBorderColor = 2;        /* Color index for meter border drawing */
static int scoreObjectColor = 4;        /* Color index for map score objects */

static int meterWidth = 60;
static int meterHeight = 10;

static void Paint_meter(int xoff, int y, const char *title, int val, int max, int meter_color)
{
    const int mw1_4 = meterWidth / 4,
              mw2_4 = meterWidth / 2,
              mw3_4 = 3 * meterWidth / 4,
              mw4_4 = meterWidth,
              BORDER = 5;
    int x, xstr;

    if (xoff >= 0) {
        x = xoff;
        xstr = WINSCALE(x + (int)meterWidth) + BORDER;
    } else {
        x = ext_view_width - ((int)meterWidth - xoff);
        xstr = WINSCALE(x) - (BORDER + (int)SDL3_Text_Width(title));
    }

    Rectangle_add(meter_color,
                  x + 2, y + 2,
                  (int)(((meterWidth - 3) * val) / (max ? max : 1)), meterHeight - 3);

    /* meterBorderColor = 0 obviously means no meter borders are drawn */
    if (meterBorderColor) {
        int color = meterBorderColor;

        SDL3_Draw_Rect((float)(PLAYFIELD_X + WINSCALE(x)),
                       (float)WINSCALE(y),
                       (float)UWINSCALE(meterWidth),
                       (float)UWINSCALE(meterHeight),
                       color);

        /* Paint scale levels */
        Segment_add(color, x,         y - 4, x,         y + meterHeight + 4);
        Segment_add(color, x + mw4_4, y - 4, x + mw4_4, y + meterHeight + 4);
        Segment_add(color, x + mw2_4, y - 3, x + mw2_4, y + meterHeight + 3);
        Segment_add(color, x + mw1_4, y - 1, x + mw1_4, y + meterHeight + 1);
        Segment_add(color, x + mw3_4, y - 1, x + mw3_4, y + meterHeight + 1);
    }

    int text_col = meterBorderColor ? meterBorderColor : meter_color;
    SDL3_Draw_Text(title,
                   (float)(PLAYFIELD_X + xstr),
                   (float)WINSCALE(y),
                   text_col);
}

static int wrap(int *xp, int *yp)
{
    int x = *xp, y = *yp;

    if (x < world.x || x > world.x + ext_view_width) {
        if (x < realWorld.x || x > realWorld.x + ext_view_width)
            return 0;
        *xp += world.x - realWorld.x;
    }
    if (y < world.y || y > world.y + ext_view_height) {
        if (y < realWorld.y || y > realWorld.y + ext_view_height)
            return 0;
        *yp += world.y - realWorld.y;
    }
    return 1;
}

void Paint_score_objects(void)
{
    int i, x, y;

    if (!scoreObjectColor)
        return;

    for (i = 0; i < MAX_SCORE_OBJECTS; i++) {
        score_object_t *sobj = &score_objects[i];
        if (sobj->life_time > 0) {
            if (loopsSlow % 3) {
                x = sobj->x * BLOCK_SZ + BLOCK_SZ / 2;
                y = sobj->y * BLOCK_SZ + BLOCK_SZ / 2;
                if (wrap(&x, &y)) {
                    if (sobj->msg_width == -1)
                        sobj->msg_width = (int)SDL3_Text_Width(sobj->msg);
                    float sx = (float)(PLAYFIELD_X + WINSCALE(X(x)) - sobj->msg_width / 2);
                    float sy = (float)(WINSCALE(Y(y)) - 4);
                    SDL3_Draw_Text(sobj->msg, sx, sy, scoreObjectColor);
                }
            }
            sobj->life_time -= timePerFrame;
            if (sobj->life_time <= 0.0) {
                sobj->life_time = 0.0;
                sobj->hud_msg_len = 0;
            }
        }
    }
}

void Paint_meters(void)
{
    int y = 20, color;
    int fps_val = (int)(FPS > 0 ? FPS : 50);

    if (fuelMeterColor)
        Paint_meter(-10, y += 20, "Fuel",
                    (int)fuelSum, (int)fuelMax, fuelMeterColor);

    if (powerMeterColor)
        color = powerMeterColor;
    else if (controlTime > 0.0)
        color = temporaryMeterColor;
    else
        color = 0;

    if (color)
        Paint_meter(-10, y += 20, "Power",
                    (int)displayedPower, (int)MAX_PLAYER_POWER, color);

    if (turnSpeedMeterColor)
        color = turnSpeedMeterColor;
    else if (controlTime > 0.0)
        color = temporaryMeterColor;
    else
        color = 0;

    if (color)
        Paint_meter(-10, y += 20, "Turnspeed",
                    (int)displayedTurnspeed, (int)MAX_PLAYER_TURNSPEED, color);

    if (controlTime > 0.0) {
        controlTime -= timePerFrame;
        if (controlTime <= 0.0)
            controlTime = 0.0;
    }

    if (packetSizeMeterColor)
        Paint_meter(-10, y += 20, "Packet",
                    (packet_size >= 4096) ? 4096 : packet_size, 4096,
                    packetSizeMeterColor);
    if (packetLossMeterColor)
        Paint_meter(-10, y += 20, "Loss", packet_loss, fps_val,
                    packetLossMeterColor);
    if (packetDropMeterColor)
        Paint_meter(-10, y += 20, "Drop", packet_drop, fps_val,
                    packetDropMeterColor);
    if (packetLagMeterColor)
        Paint_meter(-10, y += 20, "Lag", MIN(packet_lag, 1 * fps_val), 1 * fps_val,
                    packetLagMeterColor);

    if (temporaryMeterColor) {
        if (thrusttime >= 0 && thrusttimemax > 0)
            Paint_meter((ext_view_width - 300) / 2 - 32, 2 * ext_view_height / 3,
                        "Thrust Left",
                        (thrusttime >= thrusttimemax ? thrusttimemax : thrusttime),
                        thrusttimemax, temporaryMeterColor);

        if (shieldtime >= 0 && shieldtimemax > 0)
            Paint_meter((ext_view_width - 300) / 2 - 32, 2 * ext_view_height / 3 + 20,
                        "Shields Left",
                        (shieldtime >= shieldtimemax ? shieldtimemax : shieldtime),
                        shieldtimemax, temporaryMeterColor);

        if (phasingtime >= 0 && phasingtimemax > 0)
            Paint_meter((ext_view_width - 300) / 2 - 32, 2 * ext_view_height / 3 + 40,
                        "Phasing left",
                        (phasingtime >= phasingtimemax ? phasingtimemax : phasingtime),
                        phasingtimemax, temporaryMeterColor);

        if (destruct > 0)
            Paint_meter((ext_view_width - 300) / 2 - 32, 2 * ext_view_height / 3 + 60,
                        "Self destructing", destruct, (int)SELF_DESTRUCT_DELAY,
                        temporaryMeterColor);

        if (shutdown_count >= 0)
            Paint_meter((ext_view_width - 300) / 2 - 32, 2 * ext_view_height / 3 + 80,
                        "SHUTDOWN", shutdown_count, shutdown_delay,
                        temporaryMeterColor);
    }
}

static void Paint_lock(int hud_pos_x, int hud_pos_y)
{
    const int BORDER = 2;
    other_t *target;
    char str[50];
    static int mapdiag = 0;

    if (mapdiag == 0 && Setup) {
        double d = LENGTH(Setup->width, Setup->height);
        mapdiag = (int)d;
    }

    if ((target = Other_by_id(lock_id)) == NULL)
        return;

    if (hudColor) {
        int color = Life_color(target);
        if (!color)
            color = hudColor;

        Check_name_string(target);
        float nw = SDL3_Text_Width(target->id_string);
        SDL3_Draw_Text(target->id_string,
                       (float)(PLAYFIELD_X + WINSCALE(hud_pos_x)) - nw * 0.5f,
                       (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET - BORDER) - 10),
                       color);

        if (BIT(Setup->mode, LIMITED_LIVES))
            snprintf(str, sizeof(str), "%03d", target->life);
        else
            snprintf(str, sizeof(str), "%03d", lock_dist / BLOCK_SZ);

        if (BIT(Setup->mode, LIMITED_LIVES) || lock_dist != 0) {
            int dist_col = (BIT(Setup->mode, LIMITED_LIVES) && target->life == 0) ? RED : hudColor;
            SDL3_Draw_Text(str,
                           (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET + BORDER)),
                           (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET - BORDER) - 10),
                           dist_col);
        }
    }

    if (lock_dist != 0 && hudLockColor) {
        if (lock_dist > WARNING_DISTANCE || (loopsSlow & 1)) {
            int size = MIN(mapdiag / lock_dist, 10);
            if (size == 0)
                size = 1;

            float cx = (float)PLAYFIELD_X + WINSCALE_f((float)(hud_pos_x + MIN_HUD_SIZE * 0.6 * tcos(lock_dir)));
            float cy = WINSCALE_f((float)(hud_pos_y - MIN_HUD_SIZE * 0.6 * tsin(lock_dir)));
            float r  = fmaxf(1.0f, WINSCALE_f((float)size * 0.5f));

            if (self != NULL
                && ((self->team == target->team && BIT(Setup->mode, TEAM_PLAY))
                    || (self->alliance != ' ' && self->alliance == target->alliance))) {
                SDL3_Draw_Circle(cx, cy, r, BLUE);
            } else {
                SDL3_Fill_Circle(cx, cy, r, hudLockColor);
            }
        }
    }
}

static void Paint_hudradar(double hrscale, double xlimit, double ylimit, int sz)
{
    int i;
    double hrw = hrscale * 256.0;
    double hrh = hrscale * (double)RadarHeight;
    double xf = hrw / (double)Setup->width;
    double yf = hrh / (double)Setup->height;
    float radius = fmaxf(1.0f, WINSCALE_f((float)sz * 0.5f));

    for (i = 0; i < num_radar; i++) {
        double dx = (double)radar_ptr[i].x * hrscale - (double)(world.x + ext_view_width * 0.5) * xf;
        double dy = (double)radar_ptr[i].y * hrscale - (double)(world.y + ext_view_height * 0.5) * yf;

        if (dx < -hrw * 0.5)
            dx += hrw;
        else if (dx > hrw * 0.5)
            dx -= hrw;

        if (dy < -hrh * 0.5)
            dy += hrh;
        else if (dy > hrh * 0.5)
            dy -= hrh;

        if (!((dx <= xlimit) && (dx >= -xlimit)
              && (dy <= ylimit) && (dy >= -ylimit))) {

            float cx = (float)PLAYFIELD_X + WINSCALE_f((float)(dx + ext_view_width * 0.5));
            float cy = WINSCALE_f((float)(-dy + ext_view_height * 0.5));

            if (radar_ptr[i].type == RadarEnemy) {
                if (hudRadarEnemyColor >= 1)
                    SDL3_Draw_Circle(cx, cy, radius, hudRadarEnemyColor);
            } else {
                if (hudRadarOtherColor >= 1)
                    SDL3_Draw_Circle(cx, cy, radius, hudRadarOtherColor);
            }
        }
    }
}

static void Paint_HUD_items(int hud_pos_x, int hud_pos_y)
{
    const int BORDER = 3;
    char str[50];
    int vert_pos, horiz_pos;
    int i, maxWidth = -1;
    int vertSpacing = MAX(ITEM_SIZE, 10) + 1;

    vert_pos = hud_pos_y - hudSize + HUD_OFFSET + BORDER;
    horiz_pos = hud_pos_x - hudSize + HUD_OFFSET - BORDER;

    for (i = 0; i < NUM_ITEMS; i++) {
        int num = numItems[i];

        if (i == ITEM_FUEL)
            continue;

        if (instruments.showItems) {
            lastNumItems[i] = num;
            if (num <= 0)
                num = -1;
        } else {
            if (num != lastNumItems[i]) {
                numItemsTime[i] = (int)(showItemsTime * (double)FPS);
                lastNumItems[i] = num;
            }
            if (numItemsTime[i]-- <= 0) {
                numItemsTime[i] = 0;
                num = -1;
            }
        }

        if (num >= 0) {
            /* Paint item symbol */
            Gui_paint_item_symbol(i, horiz_pos - ITEM_SIZE, vert_pos, hudItemsColor);

            if (i == lose_item) {
                if (lose_item_active != 0) {
                    if (lose_item_active < 0)
                        lose_item_active++;
                    SDL3_Draw_Rect((float)(PLAYFIELD_X + WINSCALE(horiz_pos - ITEM_SIZE - 2)),
                                   (float)WINSCALE(vert_pos - 2),
                                   (float)WINSCALE(ITEM_SIZE + 2),
                                   (float)WINSCALE(ITEM_SIZE + 2),
                                   hudItemsColor);
                }
            }

            /* Paint item count */
            snprintf(str, sizeof(str), "%d", num);
            int width = (int)SDL3_Text_Width(str);
            SDL3_Draw_Text(str,
                           (float)(PLAYFIELD_X + WINSCALE(horiz_pos - ITEM_SIZE - BORDER) - width),
                           (float)(WINSCALE(vert_pos) + 4),
                           hudItemsColor);

            maxWidth = MAX(maxWidth, width + BORDER + ITEM_SIZE);
            vert_pos += vertSpacing;

            if (vert_pos + vertSpacing > hud_pos_y + hudSize - HUD_OFFSET - BORDER) {
                horiz_pos -= maxWidth + 2 * BORDER;
                vert_pos = hud_pos_y - hudSize + HUD_OFFSET + BORDER;
                maxWidth = -1;
            }
        }
    }
}

static bool have_hudmsg = false;
static char hudmsg[MSG_LEN];

void Del_HUD_message(void)
{
    have_hudmsg = false;
}

static Uint64 hudmsg_expires = 0;       /* SDL ticks, 0 = stays until deleted */

void Add_HUD_message_timed(const char *message, double seconds)
{
    if (!message)
        return;

    strlcpy(hudmsg, message, sizeof(hudmsg));
    have_hudmsg = true;
    hudmsg_expires = seconds > 0.0 ? SDL_GetTicks() + (Uint64)(seconds * 1000.0) : 0;
}

void Add_HUD_message(const char *message)
{
    Add_HUD_message_timed(message, 0.0);
}

void Paint_recording(void)
{
}

void Paint_HUD(void)
{
    const int BORDER = 3;
    char str[50];
    int hud_pos_x, hud_pos_y, size;
    int did_fuel = 0;
    int i, j;
    static char autopilot[] = "Autopilot";

    /*
     * Show speed pointer
     */
    if (ptr_move_fact != 0.0
        && selfVisible
        && (selfVel.x != 0 || selfVel.y != 0))
        Segment_add(hudColor,
                    ext_view_width / 2,
                    ext_view_height / 2,
                    (int)(ext_view_width / 2 - ptr_move_fact * selfVel.x),
                    (int)(ext_view_height / 2 + ptr_move_fact * selfVel.y));

    if (selfVisible && dirPtrColor)
        Segment_add(dirPtrColor,
                    (int)(ext_view_width / 2 + (100 - 15) * tcos(heading)),
                    (int)(ext_view_height / 2 - (100 - 15) * tsin(heading)),
                    (int)(ext_view_width / 2 + 100 * tcos(heading)),
                    (int)(ext_view_height / 2 - 100 * tsin(heading)));

    if ((hudRadarEnemyColor || hudRadarOtherColor) && Setup && Setup->width > 0) {
        double hudRadarMapScale = (double)Setup->width / (double)256;
        Paint_hudradar(
            hudRadarScale,
            hudRadarLimit * (active_view_width / 2) * hudRadarScale / hudRadarMapScale,
            hudRadarLimit * (active_view_width / 2) * hudRadarScale / hudRadarMapScale,
            hudRadarDotSize);

        Paint_hudradar(hudRadarMapScale,
                       (double)active_view_width / 2,
                       (double)active_view_height / 2,
                       SHIP_SZ);
    }

    /* message scan hack by mara and jpv */
    if (Bms_test_state(BmsBall) && msgScanBallColor)
        Arc_add(msgScanBallColor, ext_view_width / 2 - 5,
                ext_view_height / 2 - 5, 10, 10, 0, 64 * 360);
    if (Bms_test_state(BmsCover) && msgScanCoverColor)
        Arc_add(msgScanCoverColor, ext_view_width / 2 - 4,
                ext_view_height / 2 - 4, 8, 8, 0, 64 * 360);

    /*
     * Display the HUD
     */
    hud_pos_x = (int)(ext_view_width / 2 - hud_move_fact * selfVel.x);
    hud_pos_y = (int)(ext_view_height / 2 + hud_move_fact * selfVel.y);

    /* HUD frame lines (dashed) */
    float d1 = (float)MAX(1, WINSCALE(3));
    float d2 = (float)MAX(1, WINSCALE(8));
    if (hudHLineColor) {
        SDL3_Draw_Dashed_Line((float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize)),
                              (float)WINSCALE(hud_pos_y - hudSize + HUD_OFFSET),
                              (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize)),
                              (float)WINSCALE(hud_pos_y - hudSize + HUD_OFFSET),
                              hudHLineColor, d1, d2);
        SDL3_Draw_Dashed_Line((float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize)),
                              (float)WINSCALE(hud_pos_y + hudSize - HUD_OFFSET),
                              (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize)),
                              (float)WINSCALE(hud_pos_y + hudSize - HUD_OFFSET),
                              hudHLineColor, d1, d2);
    }
    if (hudVLineColor) {
        SDL3_Draw_Dashed_Line((float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize + HUD_OFFSET)),
                              (float)WINSCALE(hud_pos_y - hudSize),
                              (float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize + HUD_OFFSET)),
                              (float)WINSCALE(hud_pos_y + hudSize),
                              hudVLineColor, d1, d2);
        SDL3_Draw_Dashed_Line((float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET)),
                              (float)WINSCALE(hud_pos_y - hudSize),
                              (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET)),
                              (float)WINSCALE(hud_pos_y + hudSize),
                              hudVLineColor, d1, d2);
    }

    if (hudItemsColor)
        Paint_HUD_items(hud_pos_x, hud_pos_y);

    /* Fuel notify, HUD meter on */
    if (hudColor && (fuelTime > 0.0 || fuelSum < fuelNotify)) {
        did_fuel = 1;
        snprintf(str, sizeof(str), "%04d", (int)fuelSum);
        SDL3_Draw_Text(str,
                       (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET + BORDER)),
                       (float)(WINSCALE(hud_pos_y + hudSize - HUD_OFFSET + BORDER) + 2),
                       hudColor);
        if (numItems[ITEM_TANK]) {
            if (fuelCurrent == 0)
                strlcpy(str, "M ", sizeof(str));
            else
                snprintf(str, sizeof(str), "T%d", fuelCurrent);
            SDL3_Draw_Text(str,
                           (float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET + BORDER)),
                           (float)(WINSCALE(hud_pos_y + hudSize - HUD_OFFSET + BORDER) + 14),
                           hudColor);
        }
    }

    /* Update the lock display */
    Paint_lock(hud_pos_x, hud_pos_y);

    /* Draw "alert" messages */
    if (have_hudmsg && hudmsg_expires && SDL_GetTicks() >= hudmsg_expires)
        have_hudmsg = false;
    if (have_hudmsg) {
        float width = SDL3_Text_Width(hudmsg);
        SDL3_Draw_Text(hudmsg,
                       (float)(PLAYFIELD_X + WINSCALE(hud_pos_x)) - width * 0.5f,
                       (float)(WINSCALE(hud_pos_y - hudSize - BORDER) - 10),
                       WHITE);
    }

    /* Draw last score on hud if it has a message attached to it */
    if (hudColor) {
        for (i = 0, j = 0; i < MAX_SCORE_OBJECTS; i++) {
            score_object_t *sobj = &score_objects[(i + score_object) % MAX_SCORE_OBJECTS];
            if (sobj->hud_msg_len > 0) {
                if (sobj->hud_msg_width == -1)
                    sobj->hud_msg_width = (int)SDL3_Text_Width(sobj->hud_msg);
                if (j == 0 &&
                    sobj->hud_msg_width > WINSCALE(2 * hudSize - HUD_OFFSET * 2) &&
                    (did_fuel || hudVLineColor))
                    ++j;
                SDL3_Draw_Text(sobj->hud_msg,
                               (float)(PLAYFIELD_X + WINSCALE(hud_pos_x) - sobj->hud_msg_width / 2),
                               (float)(WINSCALE(hud_pos_y + hudSize - HUD_OFFSET + BORDER) + 2 + j * 12),
                               hudColor);
                j++;
            }
        }

        if (time_left > 0) {
            snprintf(str, sizeof(str), "%3d:%02d", (int)(time_left / 60), (int)(time_left % 60));
            size = (int)SDL3_Text_Width(str);
            SDL3_Draw_Text(str,
                           (float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize + HUD_OFFSET - BORDER) - size),
                           (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET - BORDER) - 10),
                           hudColor);
        }

        /* Update the modifiers */
        if (mods[0] != '\0') {
            int modw = (int)SDL3_Text_Width(mods);
            SDL3_Draw_Text(mods,
                           (float)(PLAYFIELD_X + WINSCALE(hud_pos_x - hudSize + HUD_OFFSET - BORDER) - modw),
                           (float)(WINSCALE(hud_pos_y + hudSize - HUD_OFFSET + BORDER) + 2),
                           hudColor);
        }

        if (autopilotLight) {
            float text_width = SDL3_Text_Width(autopilot);
            SDL3_Draw_Text(autopilot,
                           (float)(PLAYFIELD_X + WINSCALE(hud_pos_x)) - text_width * 0.5f,
                           (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET - BORDER) - 22),
                           hudColor);
        }
    }

    if (fuelTime > 0.0) {
        fuelTime -= timePerFrame;
        if (fuelTime <= 0.0)
            fuelTime = 0.0;
    }

    /* draw fuel gauge */
    if (fuelGaugeColor && fuelMax > 0.0 &&
        ((fuelTime > 0.0)
         || (fuelSum < fuelNotify
             && ((fuelSum < fuelCritical && (loopsSlow % 4) < 2)
                 || (fuelSum < fuelWarning
                     && fuelSum > fuelCritical
                     && (loopsSlow % 8) < 4)
                 || (fuelSum > fuelWarning))))) {

        SDL3_Draw_Rect((float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET + FUEL_GAUGE_OFFSET) - 1),
                       (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET + FUEL_GAUGE_OFFSET) - 1),
                       (float)(UWINSCALE(HUD_OFFSET - (2 * FUEL_GAUGE_OFFSET)) + 3),
                       (float)(UWINSCALE(HUD_FUEL_GAUGE_SIZE) + 3),
                       fuelGaugeColor);

        size = (int)((HUD_FUEL_GAUGE_SIZE * fuelSum) / fuelMax);
        SDL3_Fill_Rect((float)(PLAYFIELD_X + WINSCALE(hud_pos_x + hudSize - HUD_OFFSET + FUEL_GAUGE_OFFSET) + 1),
                       (float)(WINSCALE(hud_pos_y - hudSize + HUD_OFFSET + FUEL_GAUGE_OFFSET + HUD_FUEL_GAUGE_SIZE - size) + 1),
                       (float)UWINSCALE(HUD_OFFSET - (2 * FUEL_GAUGE_OFFSET)),
                       (float)UWINSCALE(size),
                       fuelGaugeColor);
    }
}

void Paint_messages(void)
{
    int i, x, y, top_y, bot_y;
    size_t len;
    const int BORDER = 10,
              SPACING = 12;
    message_t *msg;
    int msg_color;
    bool plain;                 /* normal or old message, not a scan or help one */
    bool old;

    top_y = BORDER;
    bot_y = WINSCALE(ext_view_height) - SPACING - BORDER;

    for (i = 0; i < 2 * maxMessages; i++) {
        if (i < maxMessages)
            msg = TalkMsg[i];
        else
            msg = GameMsg[i - maxMessages];
        if (!msg || msg->len == 0)
            continue;

        if (msg->lifeTime > MSG_FLASH_TIME || !selection.draw.state) {
            if ((msg->lifeTime -= timePerFrame) <= 0.0) {
                msg->txt[0] = '\0';
                msg->len = 0;
                msg->lifeTime = 0.0;
                continue;
            }
        }

        plain = true;
        old = msg->lifeTime <= MSG_FLASH_TIME;
        if (old)
            msg_color = oldMessagesColor;
        else {
            /* If paused, don't bother to paint messages in msgScan* colors. */
            if (self && strchr("P", self->mychar))
                msg_color = messagesColor;
            else {
                switch (msg->bmsinfo) {
                case BmsBall:   msg_color = msgScanBallColor;   plain = false; break;
                case BmsSafe:   msg_color = msgScanSafeColor;   plain = false; break;
                case BmsCover:  msg_color = msgScanCoverColor;  plain = false; break;
                case BmsPop:    msg_color = msgScanPopColor;    plain = false; break;
                default:        msg_color = messagesColor;      break;
                }
            }
        }

        if (newbie && msg->txt[0] != '\0') {
            const char *help = "[*Newbie help*]";
            size_t sz = strlen(msg->txt);
            size_t hsz = strlen(help);

            if (sz > hsz && !strcmp(help, &msg->txt[sz - hsz])) {
                msg_color = WHITE;
                plain = false;
            }
        }

        if (msg_color == 0)
            continue;

        if (i < maxMessages) {
            x = PLAYFIELD_X + BORDER;
            y = top_y;
            top_y += SPACING;
        } else {
            if (!instruments.showMessages)
                continue;
            x = PLAYFIELD_X + BORDER;
            y = bot_y;
            bot_y -= SPACING;
        }

        len = (size_t)(charsPerSecond * (MSG_LIFE_TIME - msg->lifeTime));
        len = MIN(msg->len, len);

        char tmp[MSG_LEN];
        if (len >= sizeof(tmp))
            len = sizeof(tmp) - 1;
        memcpy(tmp, msg->txt, len);
        tmp[len] = '\0';

        SDL_Color col = SDL3_Get_Color(msg_color);
        if (plain) {
            /* talk messages (top) and server messages (bottom) have their own colors */
            SDL_Color rgb;

            if (SDL3_Parse_Hex_Color(i < maxMessages ? talkMessagesRGB : gameMessagesRGB, &rgb))
                col = rgb;
            if (old)
                col.a = (Uint8)(col.a * 0.6);   /* fade instead of turning dark gray */
        }
        if (messageShadow) {
            SDL_Color shadow = { 0, 0, 0, (Uint8)(col.a * 0.85) };

            SDL3_Draw_Text_Len_Color(tmp, (int)len, (float)(x + 1), (float)(y + 1), shadow);
        }
        SDL3_Draw_Text_Len_Color(tmp, (int)len, (float)x, (float)y, col);
    }
}

void Paint_HUD_values(void)
{
    static char buf[32], buf2[32];

    if (!hudColor)
        return;

    snprintf(buf, sizeof(buf), "FPS    : %.3f", clientFPS);
    snprintf(buf2, sizeof(buf2), "CL.LAG : %.1f ms", clData.clientLag);

    float w1 = SDL3_Text_Width(buf);
    float w2 = SDL3_Text_Width(buf2);
    float wmax = MAX(w1, w2);

    float x = (float)(PLAYFIELD_X + WINSCALE(ext_view_width) - 10) - wmax;
    SDL3_Draw_Text(buf, x, 200.0f, hudColor);
    SDL3_Draw_Text(buf2, x, 220.0f, hudColor);
}

static xp_option_t hud_options[] = {
    COLOR_INDEX_OPTION("hudColor", 2, &hudColor, "Which color number to use for drawing the HUD.\n"),
    COLOR_INDEX_OPTION("hudHLineColor", 0, &hudHLineColor, "Which color number to use for drawing the horizontal lines in the HUD.\n"),
    COLOR_INDEX_OPTION("hudVLineColor", 0, &hudVLineColor, "Which color number to use for drawing the vertical lines in the HUD.\n"),
    COLOR_INDEX_OPTION("hudItemsColor", 2, &hudItemsColor, "Which color number to use for drawing owned items on the HUD.\n"),
    COLOR_INDEX_OPTION("hudRadarEnemyColor", 3, &hudRadarEnemyColor, "Which color number to use for drawing hudradar dots that represent enemy ships.\n"),
    COLOR_INDEX_OPTION("hudRadarOtherColor", 2, &hudRadarOtherColor, "Which color number to use for drawing hudradar dots that represent friendly ships or other objects.\n"),
    COLOR_INDEX_OPTION("hudLockColor", 0, &hudLockColor, "Which color number to use for drawing the lock on the HUD.\n"),
    COLOR_INDEX_OPTION("fuelGaugeColor", 0, &fuelGaugeColor, "Which color number to use for drawing the fuel gauge.\n"),
    COLOR_INDEX_OPTION("dirPtrColor", 0, &dirPtrColor, "Which color number to use for drawing the direction pointer hack.\n"),
    COLOR_INDEX_OPTION("messagesColor", 12, &messagesColor, "Which color number to use for drawing messages.\n"),
    COLOR_INDEX_OPTION("oldMessagesColor", 13, &oldMessagesColor, "Which color number to use for drawing old messages.\n"),
    XP_STRING_OPTION("talkMessagesRGB", "#E6F4FF", talkMessagesRGB, sizeof(talkMessagesRGB), NULL, NULL, NULL,
                     XP_OPTFLAG_CONFIG_DEFAULT,
                     "Color of talk messages (top of the screen) as #RRGGBB.\n"
                     "Empty: use messagesColor.\n"),
    XP_STRING_OPTION("gameMessagesRGB", "#4CFF7A", gameMessagesRGB, sizeof(gameMessagesRGB), NULL, NULL, NULL,
                     XP_OPTFLAG_CONFIG_DEFAULT,
                     "Color of server messages (bottom of the screen) as #RRGGBB.\n"
                     "Empty: use messagesColor.\n"),
    XP_BOOL_OPTION("messageShadow", true, &messageShadow, NULL, XP_OPTFLAG_CONFIG_DEFAULT,
                   "Draw a dark shadow behind message text so that it stays readable\n"
                   "on bright backgrounds.\n"),
    COLOR_INDEX_OPTION("msgScanBallColor", 3, &msgScanBallColor, "Which color number to use for drawing ball message warning.\n"),
    COLOR_INDEX_OPTION("msgScanSafeColor", 4, &msgScanSafeColor, "Which color number to use for drawing safe message.\n"),
    COLOR_INDEX_OPTION("msgScanCoverColor", 2, &msgScanCoverColor, "Which color number to use for drawing cover message.\n"),
    COLOR_INDEX_OPTION("msgScanPopColor", 11, &msgScanPopColor, "Which color number to use for drawing pop message.\n"),
    COLOR_INDEX_OPTION("fuelMeterColor", 0, &fuelMeterColor, "Which color number to use for drawing the fuel meter.\n"),
    COLOR_INDEX_OPTION("powerMeterColor", 0, &powerMeterColor, "Which color number to use for drawing the power meter.\n"),
    COLOR_INDEX_OPTION("turnSpeedMeterColor", 0, &turnSpeedMeterColor, "Which color number to use for drawing the turn speed meter.\n"),
    COLOR_INDEX_OPTION("packetSizeMeterColor", 0, &packetSizeMeterColor, "Which color number to use for drawing the packet size meter.\n"),
    COLOR_INDEX_OPTION("packetLossMeterColor", 3, &packetLossMeterColor, "Which color number to use for drawing the packet loss meter.\n"),
    COLOR_INDEX_OPTION("packetDropMeterColor", 3, &packetDropMeterColor, "Which color number to use for drawing the packet drop meter.\n"),
    COLOR_INDEX_OPTION("packetLagMeterColor", 3, &packetLagMeterColor, "Which color number to use for drawing the packet lag meter.\n"),
    COLOR_INDEX_OPTION("temporaryMeterColor", 3, &temporaryMeterColor, "Which color number to use for drawing temporary meters.\n"),
    COLOR_INDEX_OPTION("meterBorderColor", 2, &meterBorderColor, "Which color number to use for drawing borders of meters.\n"),
    COLOR_INDEX_OPTION("scoreObjectColor", 4, &scoreObjectColor, "Which color number to use for drawing score objects.\n"),
};

void Store_hud_options(void)
{
    STORE_OPTIONS(hud_options);
}
