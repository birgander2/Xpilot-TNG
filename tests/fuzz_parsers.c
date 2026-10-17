/*
 * Mutation fuzzer for the parsers that handle untrusted network input:
 * Packet_scanf (all format types used by the server) and the ship shape
 * parser.  Build with -DBUILD_TESTS=ON -DSANITIZE=ON and run:
 *
 *     build-asan/fuzz_parsers [iterations] [seed]
 *
 * Any sanitizer report or crash is a bug.  Exit status 0 means no problem.
 */
#include "xpcommon.h"
#include "net.h"
#include "shipshape.h"

bool is_server = true;

static unsigned long long rng_state;

static unsigned rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)(rng_state >> 11);
}

static void mutate(unsigned char *buf, size_t *len, size_t max)
{
    int n = 1 + rnd() % 8;

    while (n-- > 0 && *len > 0) {
	size_t pos = rnd() % *len;

	switch (rnd() % 6) {
	case 0: buf[pos] = rnd(); break;
	case 1: buf[pos] ^= 1 << (rnd() % 8); break;
	case 2: buf[pos] = (rnd() & 1) ? 0xFF : 0x00; break;
	case 3:		/* delete a byte */
	    memmove(buf + pos, buf + pos + 1, *len - pos - 1);
	    (*len)--;
	    break;
	case 4:		/* insert a byte */
	    if (*len < max) {
		memmove(buf + pos + 1, buf + pos, *len - pos);
		buf[pos] = rnd();
		(*len)++;
	    }
	    break;
	case 5:		/* duplicate a chunk */
	    if (*len + 16 < max) {
		size_t l = 1 + rnd() % 16;

		if (pos + l > *len)
		    l = *len - pos;
		memmove(buf + pos + l, buf + pos, *len - pos);
		memcpy(buf + pos, buf + pos + l, l);
		*len += l;
	    }
	    break;
	}
    }
}

/* The formats the server really uses on received packets. */
static const char *const formats[] = {
    "%u", "%s%hu%c", "%s%s%s%d", "%s", "%S", "%d%s", "%ld", "%c%ld%s",
    "%c%c%s", "%c%S", "%c%ld%ld", "%c%ld%hu", "%c%hd", "%c%hd%hd%c%c",
    "%c%s%s", "%c%ld%ld%ld", "%lu%hd%d%u%c",
};

static void fuzz_scanf(unsigned long iterations)
{
    static const unsigned char seed[] = {
	0x1A, 0x2B, 0x3C, 0x4D, 'u', 's', 'e', 'r', 0, 0x12, 0x34, 3,
	'N', 'i', 'c', 'k', 0, 'd', 'p', 'y', 0, 'h', 'o', 's', 't', 0,
	0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF,
    };
    unsigned char data[SERVER_SEND_SIZE];
    size_t len;
    unsigned long i;

    for (i = 0; i < iterations; i++) {
	sockbuf_t sb;
	char s1[MAX_CHARS], s2[MAX_CHARS], s3[MAX_CHARS], big[MSG_LEN];
	char c;
	int d1, d2, rc;
	unsigned u;
	long l1, l2, l3;
	unsigned long ul;
	short sh1, sh2;
	unsigned short us;
	const char *fmt = formats[rnd() % (sizeof(formats) / sizeof(formats[0]))];
	int j;

	if (rnd() % 4 == 0) {	/* sometimes start from random garbage */
	    len = rnd() % 600;
	    for (j = 0; j < (int)len; j++)
		data[j] = rnd();
	} else {
	    len = sizeof(seed);
	    memcpy(data, seed, len);
	    mutate(data, &len, sizeof(data));
	}

	if (Sockbuf_init(&sb, NULL, sizeof(data), SOCKBUF_READ | SOCKBUF_DGRAM) == -1)
	    abort();
	memcpy(sb.buf, data, len);
	sb.len = len;
	sb.ptr = sb.buf;

	/*
	 * Pass destinations of the documented minimum size for every
	 * conversion, no matter which format was chosen.
	 */
	memset(s1, 0, sizeof(s1));
	rc = 0;
	if (!strcmp(fmt, "%u"))
	    rc = Packet_scanf(&sb, fmt, &u);
	else if (!strcmp(fmt, "%s%hu%c"))
	    rc = Packet_scanf(&sb, fmt, s1, &us, &c);
	else if (!strcmp(fmt, "%s%s%s%d"))
	    rc = Packet_scanf(&sb, fmt, s1, s2, s3, &d1);
	else if (!strcmp(fmt, "%s"))
	    rc = Packet_scanf(&sb, fmt, s1);
	else if (!strcmp(fmt, "%S"))
	    rc = Packet_scanf(&sb, fmt, big);
	else if (!strcmp(fmt, "%d%s"))
	    rc = Packet_scanf(&sb, fmt, &d1, s1);
	else if (!strcmp(fmt, "%ld"))
	    rc = Packet_scanf(&sb, fmt, &l1);
	else if (!strcmp(fmt, "%c%ld%s"))
	    rc = Packet_scanf(&sb, fmt, &c, &l1, s1);
	else if (!strcmp(fmt, "%c%c%s"))
	    rc = Packet_scanf(&sb, fmt, &c, &c, s1);
	else if (!strcmp(fmt, "%c%S"))
	    rc = Packet_scanf(&sb, fmt, &c, big);
	else if (!strcmp(fmt, "%c%ld%ld"))
	    rc = Packet_scanf(&sb, fmt, &c, &l1, &l2);
	else if (!strcmp(fmt, "%c%ld%hu"))
	    rc = Packet_scanf(&sb, fmt, &c, &l1, &us);
	else if (!strcmp(fmt, "%c%hd"))
	    rc = Packet_scanf(&sb, fmt, &c, &sh1);
	else if (!strcmp(fmt, "%c%hd%hd%c%c"))
	    rc = Packet_scanf(&sb, fmt, &c, &sh1, &sh2, &c, &c);
	else if (!strcmp(fmt, "%c%s%s"))
	    rc = Packet_scanf(&sb, fmt, &c, s1, s2);
	else if (!strcmp(fmt, "%c%ld%ld%ld"))
	    rc = Packet_scanf(&sb, fmt, &c, &l1, &l2, &l3);
	else
	    rc = Packet_scanf(&sb, fmt, &ul, &sh1, &d1, &u, &c);

	if (rc > 0 && sb.ptr > sb.buf + sb.len) {
	    fprintf(stderr, "Packet_scanf(\"%s\") read past the packet\n", fmt);
	    abort();
	}
	Sockbuf_cleanup(&sb);
    }
}

static const char *const shapes[] = {
    "(SH: 15,0 -9,8 -9,-8)(MG: 15,0)(LG: -9,8)(RG: -9,-8)(EN: -9,0)",
    "(SH: 15,0 -9,8 -9,-8)(LL: -9,8)(RL: -9,-8)(MR: 15,0)"
    "(NM: Test)(AU: Me)",
    "(shape: 15,0 -9,8 -9,-8)(mainGun: 15,0)(leftGun: -9,8 -9,8)"
    "(rightGun: -9,-8)(leftRearGun: 1,1)(rightRearGun: 1,-1)",
    "(SH: 15,0 -10,10 -10,-10 15,0 15,0)",
};

static void fuzz_shapes(unsigned long iterations)
{
    unsigned char buf[2 * MSG_LEN + 64];
    unsigned long i;

    for (i = 0; i < iterations; i++) {
	size_t len;
	const char *seed = shapes[rnd() % (sizeof(shapes) / sizeof(shapes[0]))];
	shipshape_t *ship;

	len = strlen(seed);
	memcpy(buf, seed, len);
	if (rnd() % 3 == 0) {
	    /* Structured: one keyword with a long, often unterminated argument. */
	    static const char *const keys[] = {
		"SH", "shape", "MG", "LG", "RG", "LL", "RL", "EN", "MR", "NM",
		"AU", "LR", "RR", "xx",
	    };
	    size_t want = rnd() % (2 * MSG_LEN - 1);

	    len = (size_t)snprintf((char *)buf, sizeof(buf), "(%s:",
				   keys[rnd() % (sizeof(keys) / sizeof(keys[0]))]);
	    while (len < want) {
		len += (size_t)snprintf((char *)buf + len, sizeof(buf) - len,
					" %d,%d", (int)(rnd() % 40) - 20,
					(int)(rnd() % 40) - 20);
	    }
	    if (len > 2 * MSG_LEN - 1)
		len = 2 * MSG_LEN - 1;
	    if (rnd() & 1)
		buf[len - 1] = ')';
	}
	mutate(buf, &len, 2 * MSG_LEN - 1);	/* the longest the server accepts */
	buf[len] = '\0';
	/* Embedded NULs just shorten the string, as on the wire. */
	ship = Parse_shape_str((char *)buf);
	if (ship != NULL)
	    Free_ship_shape(ship);
    }
}

int main(int argc, char **argv)
{
    unsigned long iterations = (argc > 1) ? strtoul(argv[1], NULL, 0) : 200000;

    rng_state = ((argc > 2) ? strtoull(argv[2], NULL, 0) : 88172645463325252ULL) | 1;
    fuzz_scanf(iterations);
    fuzz_shapes(iterations);
    printf("fuzz_parsers: %lu iterations each, no problems found\n", iterations);
    return 0;
}
