/* 
 * XPilot NG, a multiplayer space war game.
 *
 * Copyright (C) 1991-2001 by
 *
 *      Bjørn Stabell        <bjoern@xpilot.org>
 *      Ken Ronny Schouten   <ken@xpilot.org>
 *      Bert Gijsbers        <bert@xpilot.org>
 *      Dick Balaska         <dick@xpilot.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Robot code originally submitted by Maurice Abraham. */

#include "xpserver.h"

#define DEFAULT_ROBOT_TYPE	"default"

/*
 * Array of different robots which are used
 * when we cannot read a robot configuration file.
 *
 * Each robot has a robot driver,
 * a name as seen by the human players,
 * some optional configuration string,
 * a usage count,
 * and a shipshape.
 */
static robot_t DefaultRobots[] = {
    {
	DEFAULT_ROBOT_TYPE,
	"Mad Max",
	"94 20",
	0,
	"(SH: 15,0 7,1 7,2 2,4 -1,11 -3,11 -2,3 -8,6 -8,-6 -2,-3 -3,-11 "
	"-1,-11 2,-4 7,-2 7,-1)"
	"(EN: -8,0)(MG: 15,0)(LL: -8,-6)(RL: -8,6)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Blackie",
	"10 90",
	0,
	"(SH: 15,0 6,2 -2,3 -1,4 -2,5 -10,8 -13,8 -13,1 -15,0 -13,-1 "
	"-13,-8 -10,-8 -2,-5 -1,-4 -2,-3 6,-2)"
	"(EN: -13,0)(MG: 15,0)(LL: -13,8)(RL: -13,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Kryten",
	"70 40",
	0,
	"(SH: 15,0 0,8 -8,0 0,-8)"
	"(EN: 0,0)(MG: 15,0)(LL: 0,8)(RL: 0,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Marvin",
	"30 70",
	0,
	"(SH: 10,0 10,7 5,14 -5,14 -10,7 -10,-7 -5,-14 5,-14 10,-7 10,0 "
	"5,5 2,7 5,0 2,-7 5,-5)"
	"(EN: -10,0)(MG: 10,0)(LL: -10,7)(RL: -10,-7)(MR: 10,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"R2D2",
	"50 60",
	0,
	"(SH: 15,0 14,1 -1,2 -2,9 0,10 -4,10 -7,2 -8,2 -8,-2 -7,-2 -4,-10 "
	"0,-10 -2,-9 -1,-2 14,-1)"
	"(EN: -7,-2)(MG: 15,0)(LL: -8,-2)(RL: -7,-2)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"C3PO",
	"60 50",
	0,
	"(SH: 10,0 0,5 0,15 15,10 0,15 -15,10 0,15 0,5 -7,0 0,-5 0,-15 "
	"-15,-10 0,-15 15,-10 0,-15 0,-5)"
	"(EN: 0,0)(MG: 10,0)(LL: 0,5)(RL: 0,-5)(MR: 10,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"K9",
	"50 50",
	0,
	"(SH: 15,0 15,5 5,5 5,-5 15,-5 15,0 -15,0 -15,5 5,5 5,-5 -15,-5 "
	"-15,-8 -15,8 -15,0)"
	"(EN: 15,0)(MG: 15,0)(LL: 15,0)(RL: 15,0)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Robby",
	"45 55",
	0,
	"(SH: 15,0 0,12 -9,8 -9,-8 0,-12)"
	"(EN: -9,0)(MG: 15,0)(LL: -9,8)(RL: -9,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Mickey",
	"05 95",
	0,
	"(SH: 5,-1 8,-5 7,-9 4,-11 -1,-10 -5,-6 -8,-10 -8,10 -5,6 -1,10 "
	"4,11 7,9 8,5 5,1 0,0)"
	"(EN: -8,0)(MG: 5,-1)(LL: -8,-10)(RL: -8,10)(MR: 5,-1)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Hermes",
	"15 85",
	0,
	"(SH: 10,1 12,8 -11,8 -10,3 -7,0 -5,2 -7,0 -10,-1 -10,-3 -13,-4 "
	"-13,-7 -15,-8 -15,-13 -5,-5 -2,-4 5,-2)"
	"(EN: -15,-10)(MG: 10,1)(LL: -15,-13)(RL: -15,-8)(MR: 10,1)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Pan",
	"60 60",
	0,
	"(SH: 15,-1 15,0 5,0 5,-1 5,9 -15,9 -15,-4 -5,-7 -3,-8 -7,-8 -5,-7 "
	"5,-4 5,-1 -15,-1)"
	"(EN: -15,2)(MG: 15,-1)(LL: -15,-4)(RL: -15,9)(MR: 15,-1)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Azurion",
	"40 30",
	0,
	"(SH: 15,0 0,2 -9,8 -3,0 -9,-8 0,-2)"
	"(EN: -3,0)(MG: 15,0)(LL: -9,8)(RL: -9,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Droidion",
	"60 30",
	0,
	"(SH: 10,0 5,9 -6,9 -11,0 -6,-9 5,-9 10,0)"
	"(EN: -6,0)(MG: 10,0)(LL: -6,9)(RL: -6,-9)(MR: 10,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Terminator",
	"80 40",
	0,
	"(SH: 15,0 0,2 -9,8 -3,0 -9,-8 0,-2)"
	"(EN: -3,0)(MG: 15,0)(LL: -9,8)(RL: -9,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Sniper",
	"30 90",
	0,
	"(SH: 15,0 4,2 -2,8 -4,7 -3,2 -8,5 -8,2 -6,1 -6,-1 -8,-2 -8,-5 "
	"-3,-2 -4,-7 -2,-8 4,-2)"
	"(EN: -6,0)(MG: 15,0)(LL: -8,2)(RL: -8,-2)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Slugger",
	"40 40",
	0,
	"(SH: 15,0 13,1 3,2 -1,8 -3,8 -3,2 -5,2 -8,5 -8,-5 -5,-2 -3,-2 "
	"-3,-8 -1,-8 3,-2 13,-1 15,0)"
	"(EN: -8,0)(MG: 15,0)(LL: -8,-5)(RL: -8,5)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Uzi",
	"95 5",
	0,
	"(SH: 15,3 7,3 7,-8 3,-8 3,1 -2,1 -3,-1 -5,-1 -14,-5 -15,2 -3,4 "
	"-1,8 0,6 13,6 14,8 15,6)"
	"(EN: -14,-1)(MG: 15,3)(LL: -15,2)(RL: -14,-5)(MR: 15,3)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Capone",
	"80 50",
	0,
	"(SH: 14,0 2,2 0,8 1,8 -3,8 -3,2 -8,4 -7,1 -7,-1 -8,-4 -3,-2 "
	"-3,-8 1,-8 0,-8 2,-2)"
	"(EN: -7,0)(MG: 14,0)(LL: -8,-4)(RL: -8,4)(MR: 14,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Tanx",
	"40 70",
	0,
	"(SH: 15,1 2,0 1,-2 6,-3 6,-5 3,-8 -10,-8 -13,-6 -13,-3 -10,-2 "
	"-11,2 -7,2 -7,8 -7,2 1,2 2,1)"
	"(EN: -13,-4)(MG: 15,1)(LL: -13,-6)(RL: -13,-3)(MR: 15,1)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Chrome Star",
	"60 60",
	0,
	"(SH: 10,0 -10,6 2,-10 2,10 -10,-6 10,0)"
	"(EN: -6,0)(MG: 10,0)(LL: -10,6)(RL: -10,-6)(MR: 10,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Bully",
	"80 10",
	0,
	"(SH: 11,0 12,-3 9,-3 8,-2 -5,-5 -9,-11 -14,-14 -5,-3 -5,3 -14,14 "
	"-9,11 -5,5 8,2 9,3 12,3)"
	"(EN: -5,0)(MG: 11,0)(LL: -14,-14)(RL: -14,14)(MR: 11,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Metal Hero",
	"40 45",
	0,
	"(SH: 15,5 12,-2 9,-2 10,-1 -8,-1 -4,-1 1,-3 -13,-9 -9,0 -15,8 "
	"1,3 -4,1 -8,1 -8,-1 -8,1 11,1)"
	"(EN: -9,0)(MG: 15,5)(LL: -13,-9)(RL: -15,8)(MR: 15,5)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Aurora",
	"60 55",
	0,
	"(SH: 15,0 -1,3 -3,5 -3,9 7,10 -12,10 -6,9 -6,4 -8,0 -6,-4 -6,-9 "
	"-12,-10 7,-10 -3,-9 -3,-5 -1,-3)"
	"(EN: -8,0)(MG: 15,0)(LL: -12,10)(RL: -12,-10)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Dalt Wisney",
	"30 75",
	0,
	"(SH: 14,0 7,-4 0,-1 -5,-4 2,-8 0,-10 -14,-10 -5,-7 -14,0 -5,7 "
	"-14,10 0,10 2,8 -5,4 0,1 7,4)"
	"(EN: -14,0)(MG: 14,0)(LL: -14,10)(RL: -14,-10)(MR: 14,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Psycho",
	"65 55",
	0,
	"(SH: 8,0 5,8 3,12 0,15 0,0 -8,3 -8,-3 0,0 0,-15 3,-12 5,-8)"
	"(EN: -8,0)(MG: 8,0)(LL: -8,3)(RL: -8,-3)(MR: 8,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Gorgon",
	"30 40",
	0,
	"(SH: 15,0 5,2 3,8 2,2 -9,2 -10,4 -12,2 -14,4 -14,-4 -12,-2 -10,-4 "
	"-9,-2 2,-2 3,-8 5,-2)"
	"(EN: -14,0)(MG: 15,0)(LL: -14,4)(RL: -14,-4)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Pompel",
	"50 50",
	0,
	"(SH: 15,0 14,4 10,5 5,2 -7,3 -7,6 5,8 -9,8 -9,-8 5,-8 -7,-6 "
	"-7,-3 5,-2 10,-5 14,-4)"
	"(EN: -9,0)(MG: 15,0)(LL: -9,8)(RL: -9,-8)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Pilt",
	"50 50",
	0,
	"(SH: 15,0 13,-2 9,-3 3,-3 -3,-3 -5,-2 -13,-2 -15,-3 -15,3 -13,2 "
	"-5,2 -3,3 -3,-8 -3,8 -3,3 8,3)"
	"(EN: -15,0)(MG: 15,0)(LL: -15,3)(RL: -15,-3)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Sparky",
	"20 40",
	0,
	"(SH:)"
	"(EN: -8,0)(MG: 14,0)(LL: -8,8)(RL: -8,-8)(MR: 14,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Cobra",
	"85 60",
	0,
	"(SH: 8,0 8,-6 6,-8 0,-7 5,-6 -14,-4 5,-2 0,-1 5,0 0,1 5,2 "
	"-14,4 5,6 0,7 6,8 8,6 8,0)"
	"(EN: 5,0)(MG: 8,0)(LL: -14,-4)(RL: -14,4)(MR: 8,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Falcon",
	"70 20",
	0,
	"(SH: 14,2 14,4 2,10 -5,10 -10,8 -12,3 -12,-3 -10,-8 -5,-10 9,-11 "
	"10,-8 7,-8 14,-4 14,-2 4,-2 4,2)"
	"(EN: -12,0)(MG: 14,2)(LL: -12,3)(RL: -12,-3)(MR: 14,2)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Boson",
	"25 35",
	0,
	"(SH: 15,0 10,-5 4,-8 7,-2 7,2 4,8 6,0 4,-8 -10,-8 -10,8 -10,-8 "
	"-15,-7 -15,7 -10,8 4,8 10,5)"
	"(EN: -15,0)(MG: 15,0)(LL: -15,-7)(RL: -15,7)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Blazy",
	"40 40",
	0,
	"(SH: 4,0 2,4 -5,11 10,12 -8,12 -4,6 -2,0 -4,-6 -8,-12 10,-12 "
	"-5,-11 2,-4)"
	"(EN: -2,0)(MG: 4,0)(LL: -8,12)(RL: -8,-12)(MR: 4,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Pixie",
	"15 93",
	0,
	"(SH: 15,0 7,4 11,1 -4,3 3,5 -7,10 -9,2 -9,-2 -7,-10 3,-5 -4,-3 "
	"11,-1 7,-4)"
	"(EN: -9,0)(MG: 15,0)(LL: -9,2)(RL: -9,-2)(MR: 15,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Wimpy",
	"5 98",
	0,
	"(SH: 3,0 6,5 8,10 5,11 1,10 -1,8 -4,9 -8,6 -5,0 -8,-6 -4,-9 "
	"-1,-8 1,-10 5,-11 8,-10 6,-5)"
	"(EN: -5,0)(MG: 3,0)(LL: -8,-6)(RL: -8,6)(MR: 3,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Bonnie",
	"30 40",
	0,
	"(SH: 15,3 5,3 5,1 4,-1 0,-1 -2,-8 -8,-8 -5,3 -6,6 -8,7 -7,8 -4,7 "
	"10,7 12,8 14,8 15,7 15,3)"
	"(EN: -6,0)(MG: 15,3)(LL: -8,7)(RL: -8,-8)(MR: 15,3)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Clyde",
	"40 45",
	0,
	"(SH: 14,0 5,5 6,2 0,2 0,8 -13,8 -13,4 -4,4 -6,0 -4,-4 -13,-4 "
	"-13,-8 0,-8 0,-2 6,-2 5,-5)"
	"(EN: -6,0)(MG: 14,0)(LL: -13,8)(RL: -13,-8)(MR: 14,0)"
    },
    {
	DEFAULT_ROBOT_TYPE,
	"Neuro",
	"70 70",
	0,
	"(SH: 12,-7 12,-12 5,-12 2,-10 1,-5 -9,-4 -11,2 -8,8 -3,11 3,11 "
	"9,8 11,2 13,0 12,-3 12,-7 7,-7)"
	"(EN: -8,2)(MG: 12,-7)(LL: -8,8)(RL: -9,-4)(MR: 12,-7)"
    },
};

#define SMART_ROBOT_TYPE	"smart"
#define BALL_ROBOT_TYPE		"ball"
#define RACE_ROBOT_TYPE		"race"

/*
 * The AIs smart robots can use, selected with the smartBotType option.
 * A new AI needs an entry here, its driver in robot_type_setups[] and an
 * entry in the launcher's list (kSmartBotTypes in launcher_app.cpp).
 */
static const struct {
    const char	*name;			/* value of smartBotType */
    const char	*driver;		/* robot driver, NULL: chosen per map */
    bool	(*usable)(void);	/* can it play this map at all? NULL: always */
    bool	(*fits)(void);		/* is it what "auto" picks here? */
} smart_bot_types[] = {
    { "auto",		NULL,			NULL, NULL },
    { SMART_ROBOT_TYPE,	SMART_ROBOT_TYPE,	NULL, NULL },
    { BALL_ROBOT_TYPE,	BALL_ROBOT_TYPE,	NULL, Robot_ball_game },
    { RACE_ROBOT_TYPE,	RACE_ROBOT_TYPE,	Robot_race_map, Robot_race_map },
};

/* Driver for a new smart robot on the current map. */
static const char *Smart_bot_driver(void)
{
    const char *want = options.smartBotType ? options.smartBotType : "auto";
    static char warned[32];
    int i, found = -1;

    for (i = 0; i < (int)NELEM(smart_bot_types); i++) {
	if (!strcasecmp(want, smart_bot_types[i].name))
	    found = i;
    }
    if (found < 0) {
	if (strncmp(warned, want, sizeof(warned) - 1)) {
	    strlcpy(warned, want, sizeof(warned));
	    warn("Unknown smartBotType \"%s\", using auto.", want);
	}
	found = 0;
    }
    if (smart_bot_types[found].driver != NULL) {
	if (!smart_bot_types[found].usable || smart_bot_types[found].usable())
	    return smart_bot_types[found].driver;
	return SMART_ROBOT_TYPE;
    }
    /* auto: the first specialised AI that fits the map */
    for (i = 1; i < (int)NELEM(smart_bot_types); i++) {
	if (smart_bot_types[i].fits && smart_bot_types[i].fits())
	    return smart_bot_types[i].driver;
    }
    return SMART_ROBOT_TYPE;
}

static robot_t SmartRobots[] = {
    {
	SMART_ROBOT_TYPE,
	"AlphaPilot",
	"90 80",
	0,
	"(SH: 15,0 6,3 2,8 -6,12 -4,4 -12,6 -9,0 -12,-6 -4,-4 -6,-12 2,-8 6,-3)"
	"(EN: -9,0)(MG: 15,0)(LL: -12,6)(RL: -12,-6)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"DeepThought",
	"75 85",
	0,
	"(SH: 15,0 8,4 0,10 -10,10 -14,4 -10,0 -14,-4 -10,-10 0,-10 8,-4)"
	"(EN: -10,0)(MG: 15,0)(LL: -14,4)(RL: -14,-4)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"SHODAN",
	"95 75",
	0,
	"(SH: 15,0 5,2 10,9 -4,7 -13,12 -8,2 -14,0 -8,-2 -13,-12 -4,-7 10,-9 5,-2)"
	"(EN: -14,0)(MG: 15,0)(LL: -13,12)(RL: -13,-12)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"HAL-9000",
	"80 90",
	0,
	"(SH: 14,0 10,6 2,10 -8,10 -13,5 -13,-5 -8,-10 2,-10 10,-6)"
	"(EN: -13,0)(MG: 14,0)(LL: -8,10)(RL: -8,-10)(MR: 14,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"Cortana",
	"85 80",
	0,
	"(SH: 15,0 4,3 -2,11 -11,8 -7,2 -13,0 -7,-2 -11,-8 -2,-11 4,-3)"
	"(EN: -13,0)(MG: 15,0)(LL: -11,8)(RL: -11,-8)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"Wintermute",
	"88 72",
	0,
	"(SH: 15,0 7,2 3,9 -9,11 -6,3 -14,4 -11,0 -14,-4 -6,-3 -9,-11 3,-9 7,-2)"
	"(EN: -11,0)(MG: 15,0)(LL: -9,11)(RL: -9,-11)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"Siri",
	"82 88",
	0,
	"(SH: 15,0 5,4 0,11 -8,9 -5,3 -13,5 -10,0 -13,-5 -5,-3 -8,-9 0,-11 5,-4)"
	"(EN: -10,0)(MG: 15,0)(LL: -8,9)(RL: -8,-9)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"GLaDOS",
	"70 95",
	0,
	"(SH: 14,0 8,5 0,9 -9,9 -14,3 -10,0 -14,-3 -9,-9 0,-9 8,-5)"
	"(EN: -10,0)(MG: 14,0)(LL: -9,9)(RL: -9,-9)(MR: 14,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"Skynet",
	"96 70",
	0,
	"(SH: 15,0 9,3 4,10 -7,12 -4,4 -14,6 -10,0 -14,-6 -4,-4 -7,-12 4,-10 9,-3)"
	"(EN: -10,0)(MG: 15,0)(LL: -14,6)(RL: -14,-6)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"Ultron",
	"92 78",
	0,
	"(SH: 15,0 6,4 1,10 -10,11 -6,3 -13,4 -9,0 -13,-4 -6,-3 -10,-11 1,-10 6,-4)"
	"(EN: -9,0)(MG: 15,0)(LL: -10,11)(RL: -10,-11)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"DeepMind",
	"86 86",
	0,
	"(SH: 15,0 7,3 2,9 -8,10 -5,2 -12,5 -9,0 -12,-5 -5,-2 -8,-10 2,-9 7,-3)"
	"(EN: -9,0)(MG: 15,0)(LL: -8,10)(RL: -8,-10)(MR: 15,0)"
    },
    {
	SMART_ROBOT_TYPE,
	"OmegaZero",
	"90 85",
	0,
	"(SH: 15,0 8,2 3,11 -9,9 -6,2 -14,3 -11,0 -14,-3 -6,-2 -9,-9 3,-11 8,-2)"
	"(EN: -11,0)(MG: 15,0)(LL: -9,9)(RL: -9,-9)(MR: 15,0)"
    },
};

int		NumRobots = 0;		/* number of currently playing robots. */
int		NumSmartRobots = 0;	/* number of currently playing smart robots. */
static int	MAX_ROBOTS = 0;		/* number of different robot parameters. */
static robot_t	*Robots;		/* array of robot parameters. */
static int	MAX_SMART_ROBOTS = NELEM(SmartRobots);

/*
 * Function prototype for robot type setup routines.
 * Add your own robot type setup routine here.
 */
extern int Robot_default_setup(robot_type_t *type_ptr);
extern int Robot_smart_setup(robot_type_t *type_ptr);
extern int Robot_ball_setup(robot_type_t *type_ptr);
extern int Robot_race_setup(robot_type_t *type_ptr);

/*
 * Array to store function pointers to robot type setup routines.
 * The default robot type should be first.
 * Add your own robot type setup routine here too.
 */
static struct robot_setup {
    int		(*setup_func)(robot_type_t *type_ptr);
} robot_type_setups[] = {
    { Robot_default_setup },
    { Robot_smart_setup },
    { Robot_ball_setup },
    { Robot_race_setup },
};

/*
 * Array of the different robot types available.
 */
static int num_robot_types = NELEM(robot_type_setups);
static robot_type_t robot_types[NELEM(robot_type_setups)];

void Parse_robot_file(void)
{
    if (options.robotFile && *options.robotFile) {
	FILE *fp = fopen(options.robotFile, "r");

	if (fp) {
	    char buf[1024];
	    char name_buf[MAX_NAME_LEN];
	    char ship_buf[2*MSG_LEN];
	    char type_buf[MAX_NAME_LEN];
	    char para_buf[MSG_LEN];
	    int got_name = 0;
	    int num_robs = 0;
	    int max_robs = 0;
	    robot_t *robs = 0;

	    /*
	     * Fill in some default values.
	     */
	    strlcpy(ship_buf, "(15,0)(-9,8)(-9,-8)", sizeof ship_buf);
	    strlcpy(type_buf, DEFAULT_ROBOT_TYPE, sizeof type_buf);
	    strlcpy(para_buf, "", sizeof para_buf);

	    while (fp) {
		int end_of_record = 0;

		if (!fgets(buf, sizeof buf, fp)) {
		    end_of_record = 1;
		    fclose(fp);
		    fp = NULL;
		}
		else if (*buf == '\n')
		    end_of_record = 1;
		else {
		    size_t size = 0;
		    int key = 0;
		    char *dst = 0;

		    if (!strncmp(buf, "name:", 5)) {
			dst = name_buf;
			size = sizeof name_buf;
			key = 1;
			got_name = 1;
		    }
		    else if (!strncmp(buf, "ship:", 5)) {
			dst = ship_buf;
			size = sizeof ship_buf;
			key = 2;
		    }
		    else if (!strncmp(buf, "type:", 5)) {
			dst = type_buf;
			size = sizeof type_buf;
			key = 3;
		    }
		    else if (!strncmp(buf, "para:", 5)) {
			dst = para_buf;
			size = sizeof para_buf;
			key = 4;
		    }
		    if (key > 0) {
			char *ptr = strchr(buf, ':') + 1;

			while (isspace(*ptr))
			    ptr++;
			strlcpy(dst, ptr, size);
			ptr = &dst[strlen(dst)];
			while (--ptr >= dst && isspace(*ptr))
			    *ptr = '\0';
		    }
		}
		if (end_of_record && got_name) {
		    got_name = 0;
		    if (num_robs == max_robs) {
			if (max_robs == 0) {
			    max_robs = 10;
			    robs = XMALLOC(robot_t, max_robs);
			} else {
			    max_robs += 10;
			    robs = XREALLOC(robot_t, robs, max_robs);
			}
			if (!robs) {
			    error("Not enough memory to parse robotsfile");
			    fclose(fp);
			    break;
			}
		    }
		    strlcpy(robs[num_robs].driver, type_buf,
			    sizeof robs[num_robs].driver);
		    strlcpy(robs[num_robs].name, name_buf,
			    sizeof robs[num_robs].name);
		    strlcpy(robs[num_robs].config, para_buf,
			    sizeof robs[num_robs].config);
		    strlcpy(robs[num_robs].shape, ship_buf,
			    sizeof robs[num_robs].shape);
		    robs[num_robs].used = 0;
		    num_robs++;
		}
	    }
	    if (num_robs > 0) {
		Robots = robs;
		MAX_ROBOTS = num_robs;
	    }
	}
    }
    if (MAX_ROBOTS == 0) {
	Robots = &DefaultRobots[0];
	MAX_ROBOTS = NELEM(DefaultRobots);
    }

#ifdef DEVELOPMENT
    if (getenv("XPILOTS_DUMP_ROBOTS_TO_ROBOT_FILE") != NULL) {
	if (options.robotFile && *options.robotFile) {
	    FILE *fp = fopen(options.robotFile, "w");
	    if (fp) {
		int i;
		for (i = 0; i < MAX_ROBOTS; i++) {
		    fprintf(fp,
			    "type:\t%s\n"
			    "para:\t%s\n"
			    "ship:\t%s\n"
			    "name:\t%s\n"
			    "\n",
			    Robots[i].driver,
			    Robots[i].config,
			    Robots[i].shape,
			    Robots[i].name);
		}
		fclose(fp);
	    }
	}
    }
#endif
}

/*
 * First time initialization of all the robot stuff.
 */
void Robot_init(void)
{
    int i, result, n;

    /*
     * For each robot driver call its initialization function.
     * If this function returns 0 then remember this robot driver.
     */

    n = 0;
    for (i = 0; i < num_robot_types; i++) {
	memset(&robot_types[n], 0, sizeof(robot_type_t));
	result = (*robot_type_setups[i].setup_func)(&robot_types[n]);
	if (result == 0)
	    n++;
    }
    num_robot_types = n;

    Parse_robot_file();

    if (options.robotTeam < 0 || options.robotTeam >= MAX_TEAMS)
	options.robotTeam = 0;
}


static void Robot_talks(enum robot_talk_t says_what,
			char *robot_name, const char *other_name)
{
    /*
     * Insert your own witty messages here and remove the silly ones.
     */

    static const char *enter_msgs[] = {
	"%s just can't stand you anymore.",
	"%s has come to give you a hard time.",
	"%s is looking for trouble.",
	"%s has a very loose trigger finger.",
	"Have fear, %s is here.",
	"Prepare to die by the hands of %s.",
	"%s is untouchable.",
	"%s is in a gruesome mood.",
	"%s is in a killing mood.",
	"%s wants you for dessert.",
	"%s vows to torment you in this life and the next.",
	"%s has no sense of humour.",
	"%s is back from the Sirius wars, and he's in a violent mood.",
    };
    static const char *leave_msgs[] = {
	"That's it, I've had enough. :(   I'm outta here. [%s]",
	"Later people.  It's been fun. [%s]",
	"Gotta go, ... er ... this ... er ... lab is closing. [%s]",
	"Er...  Oh!  It it really that late?  I gotta go. [%s].",
	"I'm signing off now.  Bye! [%s]",
	"Gotta go...  Er... I have some work to be done. [%s]",
	"This sucks! :(  I'm going back to cyberspace. [%s]",
	"I've taken enough beating for today. [%s]",
	"Oh man.  Playing with humans sucks. [%s]",
	"You can't beat us robots; we always return, stronger than ever! [%s]",
	"Geez, this just isn't my lucky day.  See ya some other time. [%s]",
	"Wow, this game is just killing me. :( [%s]",
	"I'll be back when you stop cheating! :-( [%s]",
    };
    static const char *kill_msgs[] = {
	"Have some %s.  Have some! [%s]",
	"You want some more %s? [%s]",
	"%s lost his stuff again.  That's just tooooooo bad. :) [%s]",
	"%s: did you like that one? [%s]",
	"Face it %s, you just can't compete with me. [%s]",
	"%s, my grandmother plays better than you. [%s]",
	"Hey %s, go play chess instead. [%s]",
	"I think Darwin would've said you're too unfit to survive %s :) [%s]",
	"Oh my, what colourful explosions you make %s. :) [%s]",
	"Hey %s, maybe its time you upgraded that old 386. [%s]",
    };
    static const char *war_msgs[] = {
	"UNBELIEVABLE, me shot down by %s?!?!  This means war [%s]",
	"People like %s just piss me off. [%s]",
	"Nice %s.  But now its my turn. [%s]",
	"Red alert... target: %s. [%s]",
	"$%#^@#$^#$%  That's the last time you do that %s! [%s]",
	"Enough's enough!  It's only room enough for one of us %s here. [%s]",
	"I'm sorry %s, but you must... DIE!!!!! [%s]",
	"Jihad!  Die %s!  Die! [%s]",
	"%s will be assimilated [%s]",
    };

    static int next_msg = -1;
    const char **msgsp;
    int two, i, n;

    if (options.robotsTalk != true && says_what != ROBOT_TALK_ENTER)
	return;

    switch (says_what) {
    case ROBOT_TALK_ENTER:
	msgsp = enter_msgs;
	n = NELEM(enter_msgs);
	two = 1;
	break;
    case ROBOT_TALK_LEAVE:
	msgsp = leave_msgs;
	n = NELEM(leave_msgs);
	two = 1;
	break;
    case ROBOT_TALK_KILL:
	msgsp = kill_msgs;
	n = NELEM(kill_msgs);
	two = 2;
	break;
    case ROBOT_TALK_WAR:
	msgsp = war_msgs;
	n = NELEM(war_msgs);
	two = 2;
	break;
    default:
	return;
    }

    if (next_msg == -1)
	next_msg = (int)(rfrac() * 997);

    if (++next_msg > 997)
	next_msg = 0;

    i = next_msg % n;
    if (two == 2)
	Set_message_f(msgsp[i], other_name, robot_name);
    else
	Set_message_f(msgsp[i], robot_name);
}


static void Robot_create(bool want_smart)
{
    player_t *robot;
    robot_t *rob;
    robot_t *pool = want_smart ? SmartRobots : Robots;
    int pool_size = want_smart ? MAX_SMART_ROBOTS : MAX_ROBOTS;
    team_t *teamp = NULL;
    int i, num, most_used, least_used;
    robot_data_t *data, *new_data;
    robot_type_t *rob_type;

    if (peek_ID() == 0 || pool_size <= 0)
	return;

    if ((new_data = XMALLOC(robot_data_t, 1)) == NULL) {
	error("malloc robot_data");
	return;
    }
    new_data->private_data = NULL;
    new_data->is_smart = want_smart;

    most_used = 0;
    for (i = 0; i < pool_size; i++) {
	if (pool[i].used > pool[most_used].used)
	    most_used = i;
    }
    for (i = 0; i < NumPlayers; i++) {
	player_t *pl_i = Player_by_index(i);

	if (Player_is_robot(pl_i)) {
	    data = (robot_data_t *)pl_i->robot_data_ptr;
	    if (data->is_smart == want_smart && data->robots_ind < pool_size) {
		if (pool[data->robots_ind].used < pool[most_used].used)
		    pool[data->robots_ind].used = pool[most_used].used;
	    }
	}
    }
    least_used = 0;
    for (i = 0; i < pool_size; i++) {
	if (pool[i].used < pool[least_used].used)
	    least_used = i;
    }
    num = (int)(rfrac() * pool_size);
    while (pool[num].used > pool[least_used].used) {
	if (++num >= pool_size)
	    num = 0;
    }
    rob = &pool[num];
    rob->used++;
    new_data->robots_ind = num;
    new_data->robot_types_ind = 0;
    for (i = 1; i < num_robot_types; i++) {
	if (!strcmp(robot_types[i].name, rob->driver))
	    new_data->robot_types_ind = i;
    }
    /* Smart robots use the AI selected with smartBotType */
    if (want_smart) {
	const char *driver = Smart_bot_driver();

	for (i = 1; i < num_robot_types; i++) {
	    if (!strcmp(robot_types[i].name, driver))
		new_data->robot_types_ind = i;
	}
    }
    rob_type = &robot_types[new_data->robot_types_ind];

    Init_player(NumPlayers,
		options.allowShipShapes ? Parse_shape_str(rob->shape) : NULL,
		PL_TYPE_ROBOT);

    robot = Player_by_index(NumPlayers);
    robot->robot_data_ptr = new_data;
    if (want_smart) {
	robot->pl_type_mychar = 'S';
	Player_set_mychar(robot, 'S');
    }

    strlcpy(robot->name, rob->name, MAX_CHARS);
    strlcpy(robot->username, options.robotUserName, MAX_CHARS);
    strlcpy(robot->hostname, options.robotHostName, MAX_CHARS);
    if (strcmp(rob_type->name, DEFAULT_ROBOT_TYPE))
	snprintf(robot->hostname, MAX_CHARS, "%sbot.from.hell", rob_type->name);

    robot->turnspeed = MAX_PLAYER_TURNSPEED;
    robot->turnspeed_s = MAX_PLAYER_TURNSPEED;
    robot->turnresistance = 0.12;
    robot->turnresistance_s = 0.12;
    robot->power = MAX_PLAYER_POWER;
    robot->power_s = MAX_PLAYER_POWER;
    robot->check = 0;
    if (BIT(world->rules->mode, TEAM_PLAY)) {
	robot->team = Pick_team(PL_TYPE_ROBOT);
	teamp = Team_by_index(robot->team);
	assert(teamp); /* if teamplay, can't have TEAM_NOT_SET */
	teamp->NumMembers++;
	teamp->NumRobots++;
    }

    Pick_startpos(robot);

    (*rob_type->robot_create)(robot, rob->config);

    Go_home(robot);

    request_ID();
    NumPlayers++;
    NumRobots++;
    if (want_smart)
	NumSmartRobots++;

    Rank_get_saved_score(robot);

    for (i = 0; i < NumPlayers - 1; i++) {
	player_t *pl_i = Player_by_index(i);

	if (pl_i->conn != NULL) {
	    Send_player(pl_i->conn, robot->id);
	    Send_base(pl_i->conn, robot->id, robot->home_base->ind);
	}
    }

    Robot_talks(ROBOT_TALK_ENTER, robot->name, "");

    if (options.logRobots)
	xpprintf("%s %s (%d, %s) starts at startpos %d.\n",
		 showtime(), robot->name, NumPlayers, robot->username,
		 robot->home_base->ind);

    xpprintf("[join] %s|%s|bot|%d|%d\n",
	     robot->name,
	     robot->hostname[0] ? robot->hostname : "bot",
	     robot->team,
	     robot->id);
    fflush(stdout);

    if (NumPlayers == 1) {
	if (options.maxRoundTime > 0)
	    roundtime = options.maxRoundTime * FPS;
	else
	    roundtime = -1;
	Set_message_f("Player entered. Delaying 0 seconds until next %s.",
		      (BIT(world->rules->mode, TIMING) ? "race" : "round"));
    }

    updateScores = true;
}


void Robot_destroy(player_t *pl)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    if (pl->robot_data_ptr->is_smart && NumSmartRobots > 0)
	NumSmartRobots--;

    (*rob_type->robot_destroy)(pl);
    XFREE(pl->robot_data_ptr);
}

void Robot_delete_type(bool want_smart, bool kicked)
{
    int i;
    player_t *low_pl = NULL;
    double low_score = (double)LONG_MAX;

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl_i = Player_by_index(i);

	if (!Player_is_robot(pl_i))
	    continue;
	if (pl_i->robot_data_ptr->is_smart != want_smart)
	    continue;

	if (Get_Score(pl_i) < low_score) {
	    low_pl = pl_i;
	    low_score = Get_Score(low_pl);
	}
    }
    if (low_pl)
	Robot_delete(low_pl, kicked);
}

void Robot_delete(player_t *pl, bool kicked)
{
    int i;

    if (pl == NULL) {
	player_t *low_pl = NULL;
	double low_score = (double)LONG_MAX;

	/*
	 * Find the robot with the lowest score.
	 */
	for (i = 0; i < NumPlayers; i++) {
	    player_t *pl_i = Player_by_index(i);

	    if (!Player_is_robot(pl_i))
		continue;

	    if (Get_Score(pl_i) < low_score) {
		low_pl = pl_i;
		low_score = Get_Score(low_pl);
	    }
	}
	if (low_pl)
	    pl = low_pl;
    }

    if (pl) {
	if (kicked)
	    Set_message_f("%s upset the gods and was kicked out of the game.",
			  pl->name);
	Delete_player(pl);
    }
}

/*
 * Ask a robot for an alliance
 */
void Robot_invite(player_t *pl, player_t *inviter)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    (*rob_type->robot_invite)(pl, inviter);
}

/*
 * Tell a robot about a game event; pl may be any player, other is the
 * second player involved (or NULL).
 */
void Robot_game_event(player_t *pl, int event, player_t *other)
{
    robot_type_t *rob_type;

    if (pl == NULL || !Player_is_robot(pl) || pl->robot_data_ptr == NULL)
	return;
    rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];
    if (rob_type->robot_game_event != NULL)
	(*rob_type->robot_game_event)(pl, event, other);
}

/*
 * Name of the robot type driving this robot, NULL for non-robots.
 */
const char *Robot_type_name(player_t *pl)
{
    if (pl == NULL || !Player_is_robot(pl) || pl->robot_data_ptr == NULL)
	return NULL;
    return robot_types[pl->robot_data_ptr->robot_types_ind].name;
}

/*
 * Turn on a war lock.
 */
static void Robot_set_war(player_t *pl, int victim_id)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    (*rob_type->robot_set_war)(pl, victim_id);
}


/*
 * Turn off a war lock.
 * The only time when this can be called is if
 * a player a robot has war on leaves the game.
 */
void Robot_reset_war(player_t *pl)
{
    Robot_set_war(pl, NO_ID);
}


/*
 * Someone has programmed a robot (using ECM) to seek some player.
 */
void Robot_program(player_t *pl, int victim_id)
{
    Robot_set_war(pl, victim_id);
}


/*
 * Return the id of the player this robot has war on.
 * If the robot is not in peace mode then return -1.
 */
int Robot_war_on_player(player_t *pl)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    return (*rob_type->robot_war_on_player)(pl);
}


/*
 * A robot has killed someone.
 * Or a robot has been killed by someone.
 * Maybe this is enough reason for the killed robot to change
 * its behavior with respect to the player it has been killed by.
 */
void Robot_war(player_t *pl, player_t *kp)
{
    if (kp->id == pl->id)
	return;

    if (Player_is_robot(kp)) {
	Robot_talks(ROBOT_TALK_KILL, kp->name, pl->name);
	Robot_set_war(kp, NO_ID);
    }

    if (Player_is_robot(pl)
	&& rfrac() * 100.0 < Get_Score(kp) - Get_Score(pl)
	&& !Players_are_teammates(pl, kp)
	&& !Players_are_allies(pl, kp)) {

	Robot_talks(ROBOT_TALK_WAR, pl->name, kp->name);

	if (Robot_war_on_player(pl) != kp->id) {
	    sound_play_all(DECLARE_WAR_SOUND);
	    Robot_set_war(pl, kp->id);
	}
    }
}


/*
 * A robot starts on its homebase.
 */
void Robot_go_home(player_t *pl)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    (*rob_type->robot_go_home)(pl);
}


/*
 * Someone sends a message to a robot.
 * The format of the message is: "This is the real message [receiver]:[sender]"
 */
void Robot_message(player_t *pl, const char *message)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    (*rob_type->robot_message)(pl, message);
}


/*
 * A robot plays this frame.
 */
static void Robot_play(player_t *pl)
{
    robot_type_t *rob_type = &robot_types[pl->robot_data_ptr->robot_types_ind];

    (*rob_type->robot_play)(pl);
}

/*
 * Check if robot is still considered good enough to continue playing.
 * Return false if robot continues playing,
 * return true if robot leaves the game.
 */
static bool Robot_check_leave(player_t *pl)
{
    bool leave = false;

    if (!options.robotsLeave)
	return false;

    if (options.robotLeaveLife > 0
	&& pl->pl_deaths_since_join >= options.robotLeaveLife) {
	Set_message_f("%s retired.", pl->name);
	leave = true;
    }

    if (leave) {
	Robot_talks(ROBOT_TALK_LEAVE, pl->name, "");
	Robot_delete(pl, false);
	return true;
    }

    return false;
}


/*
 * On each round we call the robot type round ticker.
 */
static void Robot_round_tick(void)
{
    int i;

    if (NumRobots > 0) {
	for (i = 0; i < num_robot_types; i++)
	    (*robot_types[i].robot_round_tick)();
    }
}


/*
 * Update tanks here.
 */
static void Tank_play(player_t *pl)
{
    int		t = frame_loops % (TANK_NOTHRUST_TIME + TANK_THRUST_TIME);

    if (t == 0)
	Thrust(pl, true);
    else if (t == TANK_THRUST_TIME)
	Thrust(pl, false);
}


static bool robot_prefill;

/*
 * botsMatchHumans: keep NumRobots at botsPerHuman * (number of active
 * human players). maxRobots and maxSmartRobots only give the ratio of
 * normal to smart robots. Returns after at most one create/delete.
 *
 * robotsFillTo: keep humans + NumRobots at maxRobots + maxSmartRobots, each
 * type with its own share of that.
 */
static void Robot_match_humans(bool can_spawn_more, int num_playing_ships,
			       int num_any_ships, double *create_delay)
{
    int i, humans = 0, target;
    int num_default_robots = NumRobots - NumSmartRobots;
    int weight_default = MAX(options.maxRobots, 0);
    int weight_smart = MAX(options.maxSmartRobots, 0);
    bool want_smart, fill = !options.botsMatchHumans && options.robotsFillTo;

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (Player_is_human(pl) && !Player_is_paused(pl) && pl->rectype != 2)
	    humans++;
    }
    if (fill)
	target = MAX(weight_default + weight_smart - humans, 0);
    else
	target = (int)(humans * MAX(options.botsPerHuman, 0.0) + 0.5);

    if (NumRobots < target && can_spawn_more) {
	*create_delay += timeStep;
	if (*create_delay < ROBOT_CREATE_DELAY && !robot_prefill)
	    return;
	*create_delay = 0;
	if (fill && num_default_robots >= weight_default)
	    want_smart = weight_smart > NumSmartRobots;
	else if (fill && NumSmartRobots >= weight_smart)
	    want_smart = false;
	else if (weight_default + weight_smart > 0)
	    want_smart = (int)(randomMT() % (weight_default + weight_smart))
			 >= weight_default;
	else
	    want_smart = false;
	/* Fall back to the other type if its pool is exhausted. */
	if (want_smart && NumSmartRobots >= MAX_SMART_ROBOTS)
	    want_smart = false;
	else if (!want_smart && num_default_robots >= MAX_ROBOTS)
	    want_smart = true;
	Robot_create(want_smart);
	return;
    }
    *create_delay = 0;

    if (NumRobots <= 0)
	return;
    if ((!options.baselessPausing && num_playing_ships > Num_bases())
	|| num_any_ships > NUM_IDS) {
	Robot_delete(NULL, false);
    } else if (NumRobots > target) {
	if (fill)	/* the type that is fuller compared to its share */
	    want_smart = (double)NumSmartRobots * MAX(weight_default, 1)
			 >= (double)num_default_robots * MAX(weight_smart, 1)
			 && NumSmartRobots > 0;
	else	/* Remove a type with probability proportional to its share. */
	    want_smart = (int)(randomMT() % NumRobots) < NumSmartRobots;
	Robot_delete_type(want_smart, false);
    }
}

/*
 * While no human is connected the world is frozen and Robot_update() is
 * not called, so robots would only trickle in after the first human joined
 * (with idleRun they trickle in with ROBOT_CREATE_DELAY).  Create all
 * missing robots at once while no human plays instead, so a round started
 * by the first human has them all (limited lives makes latecomers wait).
 */
void Robot_prefill(void)
{
    int i, n = MAX_ROBOTS + MAX_SMART_ROBOTS;

    robot_prefill = true;
    for (i = 0; i < n; i++) {
	int before = NumRobots;

	Robot_update(false);
	if (NumRobots <= before)
	    break;
    }
    robot_prefill = false;
}

/*
 * Update robots. If 'tick' is true, robot AI routines will be called.
 */
void Robot_update(bool tick)
{
    int i;
    static double new_robot_delay;
    int num_playing_ships, num_any_ships;
    int num_default_robots = NumRobots - NumSmartRobots;
    int total_max_robots = options.maxRobots + options.maxSmartRobots;
    bool need_smart = false;
    bool need_default = false;
    bool can_spawn_more;

    num_any_ships = NumPlayers + login_in_progress;
    num_playing_ships = num_any_ships - NumPseudoPlayers;

    can_spawn_more = (options.baselessPausing || num_playing_ships < Num_bases())
	&& num_any_ships < NUM_IDS
	&& !(BIT(world->rules->mode, TEAM_PLAY)
	     && options.restrictRobots
	     && world->teams[options.robotTeam].NumMembers >=
		world->teams[options.robotTeam].NumBases);

    if (robot_prefill)
	new_robot_delay = ROBOT_CREATE_DELAY;

    if (options.botsMatchHumans || options.robotsFillTo) {
	Robot_match_humans(can_spawn_more, num_playing_ships, num_any_ships,
			   &new_robot_delay);
    } else {
	    /* maxRobots / maxSmartRobots count robots only, not humans */
	    if (NumSmartRobots < MAX_SMART_ROBOTS
		&& (NumSmartRobots < options.minSmartRobots
		    || NumSmartRobots < options.maxSmartRobots))
		need_smart = true;

	    if (num_default_robots < MAX_ROBOTS
		&& (num_default_robots < options.minRobots
		    || num_default_robots < options.maxRobots))
		need_default = true;

	    if (can_spawn_more && (need_smart || need_default)) {
		new_robot_delay += timeStep;
		if (new_robot_delay >= ROBOT_CREATE_DELAY) {
		    if (need_smart && (!need_default || NumSmartRobots <= num_default_robots))
			Robot_create(true);
		    else
			Robot_create(false);
		    new_robot_delay = 0;
		}
	    }
	    else {
		new_robot_delay = 0;
		if (NumRobots > 0) {
		    if ((!options.baselessPausing 
			 && num_playing_ships > Num_bases())
			|| (num_any_ships > NUM_IDS)) {
			Robot_delete(NULL, false);
		    }
		    else if (num_default_robots > options.maxRobots
			     && num_default_robots > options.minRobots) {
			Robot_delete_type(false, false);
		    }
		    else if (NumSmartRobots > options.maxSmartRobots
			     && NumSmartRobots > options.minSmartRobots) {
			Robot_delete_type(true, false);
		    }
		    else if (NumRobots > total_max_robots) {
			if (num_default_robots > options.minRobots)
			    Robot_delete_type(false, false);
			else if (NumSmartRobots > options.minSmartRobots)
			    Robot_delete_type(true, false);
		    }
		}
	    }
    }

    if (!tick)
	return;

    if (NumRobots <= 0 && NumPseudoPlayers <= 0)
	return;

    Robot_round_tick();

    for (i = 0; i < NumPlayers; i++) {
	player_t *pl = Player_by_index(i);

	if (Player_is_tank(pl)) {
	    Tank_play(pl);
	    continue;
	}

	if (!Player_is_robot(pl))
	    /* Ignore non-robots. */
	    continue;

	if (!Player_is_alive(pl)) {
	    if (Robot_check_leave(pl))
		i--;
	    continue;
	}

	/*
	 * Let the robot code control this robot.
	 */
	Robot_play(pl);
    }
}
