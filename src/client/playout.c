/*
 * XPilot TNG - adaptive playout (jitter) buffer for incoming frames.
 *
 * Frames are released on a steady clock derived from the server frame
 * number: release = loops / FPS + base + delay.  'base' is the smallest
 * network delay seen recently, 'delay' follows the largest lateness seen
 * (peak hold) and shrinks slowly, so short network episodes - frames held
 * up and delivered in bursts - are played smoothly at the price of some
 * extra latency until the episode is over.  Frames that are late by more
 * than maxJitterBuffer are shown as soon as they arrive.
 *
 * Only frame datagrams are delayed.  Every other datagram is handed on
 * as soon as it is read, and nothing but frames is ever taken out of the
 * socket ahead of time: the login code waits for data with select() on
 * the real socket, so packets held back here would make it time out.
 * Enabled with the client option maxJitterBuffer (milliseconds, 0 = off).
 */

#include "xpclient.h"
#include "playout.h"

#define PO_QLEN		128
#define PO_PKTSIZE	(CLIENT_RECV_SIZE)
#define PO_WINDOW	500		/* frames over which 'base' is the minimum */
#define PO_DECAY	0.025		/* delay shrinks by this many s per s */

typedef struct {
    double	release;
    int		len;
    char	data[PO_PKTSIZE];
} po_pkt_t;

typedef struct {
    po_pkt_t	pkt[PO_QLEN];
    int		head, count;
} po_queue_t;

static po_queue_t	po_frames;
static double		po_offset[PO_WINDOW];
static int		po_noff, po_ioff;
static double		po_delay, po_last_time, po_last_release;
static long		po_last_loops;
static bool		po_active;

static double Po_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static po_pkt_t *Po_push(po_queue_t *q)
{
    po_pkt_t *p;

    if (q->count == PO_QLEN)
	return NULL;
    p = &q->pkt[(q->head + q->count) % PO_QLEN];
    q->count++;
    return p;
}

static po_pkt_t *Po_head(po_queue_t *q, double now)
{
    if (q->count > 0 && q->pkt[q->head].release <= now)
	return &q->pkt[q->head];
    return NULL;
}

static void Po_pop(po_queue_t *q)
{
    q->head = (q->head + 1) % PO_QLEN;
    q->count--;
}

/* Called when the game loop starts, i.e. after login is complete. */
void Playout_start(void)
{
    po_active = (maxJitterBuffer > 0);
    if (po_active) {
	sockbuf_read_hook = Playout_read;
	printf("Jitter buffer enabled, up to %d ms\n", maxJitterBuffer);
    }
}

bool Playout_active(void)
{
    return po_active;
}

/* Frame datagram arrived at 'now': decide when it is released. */
static double Po_release_time(long loops, double now)
{
    double fps = FPS > 0 ? FPS : 50, ideal = loops / fps;
    double offset = now - ideal, base, excess, cap = maxJitterBuffer * 1e-3;
    int i;

    if (loops < po_last_loops - 100) {
	/* server restarted or frame counter jumped, start over */
	po_noff = po_ioff = 0;
	po_delay = 0;
    }
    po_offset[po_ioff] = offset;
    po_ioff = (po_ioff + 1) % PO_WINDOW;
    if (po_noff < PO_WINDOW)
	po_noff++;
    base = po_offset[0];
    for (i = 1; i < po_noff; i++)
	if (po_offset[i] < base)
	    base = po_offset[i];

    excess = offset - base;
    if (po_last_time > 0 && now > po_last_time)
	po_delay -= (now - po_last_time) * PO_DECAY;
    if (po_delay < 0)
	po_delay = 0;
    if (excess > po_delay)
	po_delay = excess;
    if (po_delay > cap)
	po_delay = cap;
    po_last_time = now;
    po_last_loops = loops;

    return ideal + base + po_delay;
}

/* Replacement for sock_read() when the jitter buffer is on. */
int Playout_read(sock_t *sock, char *buf, int max)
{
    double now = Po_now();
    char tmp[PO_PKTSIZE];
    po_pkt_t *p;
    int len;

    while ((len = sock_read(sock, tmp, sizeof(tmp))) > 0) {
	now = Po_now();
	if (len >= 5 && (u_byte)tmp[0] == PKT_START) {
	    long loops = ((long)(u_byte)tmp[1] << 24) | ((u_byte)tmp[2] << 16)
		| ((u_byte)tmp[3] << 8) | (u_byte)tmp[4];

	    if (loops <= po_last_loops)
		/* duplicate or out of order, the client would drop it too */
		continue;
	    if ((p = Po_push(&po_frames)) != NULL) {
		double t = Po_release_time(loops, now);

		if (t < po_last_release)
		    t = po_last_release;
		po_last_release = p->release = t;
		p->len = len;
		memcpy(p->data, tmp, (size_t)len);
		continue;
	    }
	    /* queue full: hand it on right away */
	}
	len = MIN(len, max);
	memcpy(buf, tmp, (size_t)len);
	return len;
    }

    if ((p = Po_head(&po_frames, now)) == NULL) {
	errno = EAGAIN;
	return -1;
    }
    Po_pop(&po_frames);
    len = MIN(p->len, max);
    memcpy(buf, p->data, (size_t)len);
    return len;
}

bool Playout_due(void)
{
    return po_active && Po_head(&po_frames, Po_now()) != NULL;
}

int Playout_wait_us(int max_us)
{
    int us;

    if (!po_active || po_frames.count == 0)
	return max_us;
    us = (int)((po_frames.pkt[po_frames.head].release - Po_now()) * 1e6);
    return us < 0 ? 0 : us < max_us ? us : max_us;
}
