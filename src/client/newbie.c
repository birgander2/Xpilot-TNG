/*
 * XPilot TNG: newbie help.
 *
 * Short hints shown above the HUD while the client option 'newbie' is on:
 * what a freshly picked up item does and which keys use it, plus a slowly
 * rotating set of tips about the keys a new player tends to miss.
 */

#include "xpclient.h"

#define NEWBIE_ALERT_TIME	10.0	/* seconds a hint stays visible */
#define NEWBIE_FIRST_TIP_TIME	30.0	/* seconds until the first tip */
#define NEWBIE_TIP_INTERVAL	60.0	/* seconds between tips */
#define NEWBIE_FUEL_PACK_MIN	100.0	/* fuel jump that counts as a pick up */

/* A key option and what it does for this item (label may be NULL). */
typedef struct {
    const char	*option;
    const char	*label;
} newbie_key_t;

typedef struct {
    const char	*name;
    const char	*what;		/* what the item does */
    newbie_key_t keys[3];	/* keys that use it, empty option ends list */
} newbie_item_t;

static const newbie_item_t newbie_items[NUM_ITEMS] = {
    [ITEM_FUEL]		= { "Fuel", "refills your tank, more at fuel stations",
			    {{"keyRefuel", "refuel at station"}} },
    [ITEM_WIDEANGLE]	= { "Wide angle", "extra front cannons fire with your shot", {{NULL}} },
    [ITEM_REARSHOT]	= { "Rear shot", "a cannon shooting backwards", {{NULL}} },
    [ITEM_AFTERBURNER]	= { "Afterburner", "more engine power", {{NULL}} },
    [ITEM_CLOAK]	= { "Cloak", "makes you nearly invisible",
			    {{"keyCloak", NULL}} },
    [ITEM_SENSOR]	= { "Sensor", "shows cloaked ships more clearly", {{NULL}} },
    [ITEM_TRANSPORTER]	= { "Transporter", "steals an item from a nearby ship",
			    {{"keyTransporter", NULL}} },
    [ITEM_TANK]		= { "Tank", "more fuel capacity, faster refuelling",
			    {{"keyTankNext", "next"}, {"keyTankDetach", "drop"}} },
    [ITEM_MINE]		= { "Mine", "bomb or stationary mine",
			    {{"keyDropMine", "drop"}, {"keyDetachMine", "detach"}} },
    [ITEM_MISSILE]	= { "Missile", "smart missile (lock a target first)",
			    {{"keyFireMissile", "smart"}, {"keyFireHeat", "heat"},
			     {"keyFireTorpedo", "torpedo"}} },
    [ITEM_ECM]		= { "ECM", "confuses missiles and robots nearby",
			    {{"keyEcm", NULL}} },
    [ITEM_LASER]	= { "Laser", "costs fuel, more lasers reach farther",
			    {{"keyFireLaser", NULL}} },
    [ITEM_EMERGENCY_THRUST] = { "Emergency thrust", "short burst of strong thrust",
			    {{"keyEmergencyThrust", NULL}} },
    [ITEM_TRACTOR_BEAM]	= { "Tractor beam", "pulls the locked ship towards you",
			    {{"keyTractorBeam", "pull"}, {"keyPressorBeam", "push"}} },
    [ITEM_AUTOPILOT]	= { "Autopilot", "brakes the ship, toggle on or off",
			    {{"keyToggleAutoPilot", NULL}} },
    [ITEM_EMERGENCY_SHIELD] = { "Emergency shield", "short period of invulnerability",
			    {{"keyEmergencyShield", NULL}} },
    [ITEM_DEFLECTOR]	= { "Deflector", "pushes hostile objects away",
			    {{"keyDeflector", NULL}} },
    [ITEM_HYPERJUMP]	= { "Hyperjump", "teleports you to a random place",
			    {{"keyHyperJump", NULL}} },
    [ITEM_PHASING]	= { "Phasing", "fly through walls for a short time",
			    {{"keyPhasing", NULL}} },
    [ITEM_MIRROR]	= { "Mirror", "reflects laser beams", {{NULL}} },
    [ITEM_ARMOR]	= { "Armor", "absorbs a hit when your shield is off", {{NULL}} },
};

/*
 * Copy the keys bound to a key option into buf, separated by '/'.
 * Returns false if the option does not exist or has no key.
 */
static bool Newbie_key_text(const char *option, char *buf, size_t size)
{
    xp_option_t *opt = Find_option(option);
    const char *val;
    size_t n = 0;
    bool in_space = true;

    if (!opt)
	return false;
    val = Option_value_to_string(opt);
    if (!val)
	return false;

    for (; *val && n + 1 < size; val++) {
	if (*val == ' ' || *val == '\t') {
	    in_space = true;
	    continue;
	}
	if (in_space && n > 0 && n + 1 < size)
	    buf[n++] = '/';
	in_space = false;
	buf[n++] = *val;
    }
    buf[n] = '\0';

    return n > 0 && strcasecmp(buf, "none") != 0;
}

/* Append to msg, never overflowing. */
static void Newbie_append(char *msg, size_t size, const char *fmt, ...)
{
    size_t len = strlen(msg);
    va_list ap;

    if (len + 1 >= size)
	return;
    va_start(ap, fmt);
    vsnprintf(msg + len, size - len, fmt, ap);
    va_end(ap);
}

static int Newbie_fps(void)
{
    return Setup ? FPS : 50;
}

/* Frame until which the last hint is shown; tips wait for it to pass. */
static unsigned long newbie_frame = 0;
static unsigned long newbie_busy_until = 0;

static void Newbie_show(const char *msg)
{
    Add_newbie_message(msg);
    newbie_busy_until = newbie_frame
	+ (unsigned long)(NEWBIE_ALERT_TIME * Newbie_fps());
}

/* A new item was picked up: say what it does and how to use it. */
static void Newbie_item_picked_up(int item)
{
    const newbie_item_t *it = &newbie_items[item];
    char msg[MSG_LEN], keys[64];
    bool first = true;
    int i;

    if (!it->name)
	return;

    snprintf(msg, sizeof(msg), "%s: %s.", it->name, it->what);
    for (i = 0; i < 3 && it->keys[i].option; i++) {
	if (!Newbie_key_text(it->keys[i].option, keys, sizeof(keys)))
	    continue;
	Newbie_append(msg, sizeof(msg), first ? " Key: " : ", ");
	first = false;
	if (it->keys[i].label)
	    Newbie_append(msg, sizeof(msg), "%s (%s)", keys, it->keys[i].label);
	else
	    Newbie_append(msg, sizeof(msg), "%s", keys);
    }

    Newbie_show(msg);
}

/*
 * Items and fuel are only compared with the previous packet while playing.
 * After (re)joining or respawning the first packet just sets the baseline,
 * so the start items do not trigger hints.
 */
static bool have_old_items = false;
static bool have_old_fuel = false;

void Newbie_status(bool playing)
{
    if (!playing)
	have_old_items = have_old_fuel = false;
}

/* Call with the item counts of a new self packet, before they are stored. */
void Newbie_check_items(const u_byte *newNumItems)
{
    int i;

    if (newbie && have_old_items) {
	for (i = 0; i < NUM_ITEMS; i++) {
	    if (i != ITEM_FUEL && newNumItems[i] > numItems[i])
		Newbie_item_picked_up(i);
	}
    }
    have_old_items = true;
}

/*
 * Call with the fuel sum of a new self packet, before it is stored.
 * A fuel can adds hundreds at once, refuelling at a station only a few
 * units per frame.
 */
void Newbie_check_fuel(double newFuelSum)
{
    if (newbie && have_old_fuel && newFuelSum - fuelSum >= NEWBIE_FUEL_PACK_MIN)
	Newbie_item_picked_up(ITEM_FUEL);
    have_old_fuel = true;
}

/*
 * Tips: "<intro> <option> <label>, ..." where each option contributes
 * its keys; options without a key are left out.  Returns false if no
 * key at all is bound, so the tip is skipped.
 */
static bool Newbie_keys_tip(char *msg, size_t size, const char *intro,
			    const newbie_key_t *list)
{
    char keys[64];
    bool first = true;

    snprintf(msg, size, "%s", intro);
    for (; list->option; list++) {
	if (!Newbie_key_text(list->option, keys, sizeof(keys)))
	    continue;
	Newbie_append(msg, size, first ? " " : ", ");
	first = false;
	Newbie_append(msg, size, "%s = %s", keys, list->label);
    }
    return !first;
}

static bool Newbie_tip(int n, char *msg, size_t size)
{
    switch (n) {
    case 0: {
	static const newbie_key_t k[] = {
	    { "keyTalk", "talk (Enter sends, Escape cancels)" }, { NULL } };
	return Newbie_keys_tip(msg, size, "Chat with other players:", k);
    }
    case 1: {
	static const newbie_key_t k[] = {
	    { "keyExit", "quit" }, { "keyYes", "yes" }, { "keyNo", "no" },
	    { NULL } };
	return Newbie_keys_tip(msg, size, "Leave the game:", k);
    }
    case 2: {
	static const newbie_key_t k[] = {
	    { "keyToggleNuclear", "nuclear" }, { "keyToggleCluster", "cluster" },
	    { "keyToggleMini", "mini" }, { "keyClearModifiers", "clear all" },
	    { NULL } };
	return Newbie_keys_tip(msg, size, "Weapon modifiers for missiles and mines:", k);
    }
    case 3: {
	static const newbie_key_t k[] = {
	    { "keyToggleVelocity", "velocity" }, { "keyToggleSpread", "spread" },
	    { "keyTogglePower", "power" }, { "keyToggleImplosion", "implosion" },
	    { NULL } };
	return Newbie_keys_tip(msg, size, "More weapon modifiers:", k);
    }
    case 4: {
	static const newbie_key_t k[] = {
	    { "keyLockClose", "lock nearest ship" }, { "keyLockNext", "next" },
	    { "keyLockPrev", "previous" }, { NULL } };
	return Newbie_keys_tip(msg, size, "Targeting (needed for smart missiles):", k);
    }
    default:
	return false;
    }
}

#define NEWBIE_NUM_TIPS 5

/* Call once per frame: shows a tip every now and then while idle. */
void Newbie_frame(void)
{
    static unsigned long next_tip = 0;
    static int tip = 0;
    char msg[MSG_LEN];
    int tries;

    newbie_frame++;
    if (!newbie || clData.quitMode || clData.talking)
	return;
    if (next_tip == 0)
	next_tip = (unsigned long)(NEWBIE_FIRST_TIP_TIME * Newbie_fps());
    if (newbie_frame < next_tip || newbie_frame < newbie_busy_until)
	return;

    for (tries = 0; tries < NEWBIE_NUM_TIPS; tries++) {
	bool ok = Newbie_tip(tip, msg, sizeof(msg));

	tip = (tip + 1) % NEWBIE_NUM_TIPS;
	if (ok) {
	    Newbie_show(msg);
	    break;
	}
    }
    next_tip = newbie_frame + (unsigned long)(NEWBIE_TIP_INTERVAL * Newbie_fps());
}
