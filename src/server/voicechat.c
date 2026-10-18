/*
 * XPilot TNG: The Next Generation
 * Voice chat forwarding.
 *
 * The server never decodes audio.  Each Opus frame received from a client
 * is copied into the voice queue of every eligible listener, together with
 * a gain and stereo pan derived from the distance between speaker and
 * listener.  Queued packets are appended to the next frame update.
 */

#include "xpserver.h"

/*
 * Position a player listens from.  Dead, waiting or paused players and
 * spectators hear from the ship they are watching.  Returns false when
 * there is no meaningful position (free spectator).
 */
static bool Voice_listen_pos(player_t *pl, clpos_t *pos)
{
    player_t *eye = pl;

    if (BIT(pl->lock.tagged, LOCK_PLAYER)
	&& (pl->rectype == 2
	    || Player_is_waiting(pl)
	    || Player_is_dead(pl)
	    || Player_is_paused(pl))) {
	int ind = GetInd(pl->lock.pl_id);

	if (ind >= 0)
	    eye = Player_by_index(ind);
    } else if (pl->rectype == 2)
	return false;

    *pos = eye->pos;
    return true;
}

/*
 * Gain (0 = inaudible) and pan for one listener.  A range of 0 or less
 * means unlimited range at full volume.
 */
static int Voice_gain(player_t *speaker, player_t *listener, double range,
		      int *pan)
{
    clpos_t lpos;
    double dx, dy, dist, g;
    int p;

    *pan = VOICE_PAN_CENTER;
    if (range <= 0.0)
	return VOICE_GAIN_MAX;
    if (!Voice_listen_pos(listener, &lpos))
	return VOICE_GAIN_MAX;

    dx = (double)WRAP_DCX(speaker->pos.cx - lpos.cx) / CLICK;
    dy = (double)WRAP_DCY(speaker->pos.cy - lpos.cy) / CLICK;
    dist = LENGTH(dx, dy);
    if (dist >= range)
	return 0;

    /* Smoothstep fade: stays loud nearby, fades gently towards range. */
    g = 1.0 - dist / range;
    g = g * g * (3.0 - 2.0 * g);

    p = VOICE_PAN_CENTER + (int)(MAX(-1.0, MIN(1.0, dx / (range * 0.5)))
				 * 127.0);
    *pan = MAX(1, MIN(255, p));

    return MAX(1, (int)(g * VOICE_GAIN_MAX + 0.5));
}

/*
 * Append one packet to a listener's queue, dropping the oldest
 * packets when the queue is full.
 */
static void Voice_enqueue(connection_t *connp, const unsigned char *pkt,
			  int len)
{
    while (connp->voice_len + len > VOICE_QUEUE_SIZE
	   && connp->voice_len > 0) {
	int first = VOICE_S2C_HEADER + connp->voice_queue[VOICE_S2C_HEADER - 1];

	memmove(connp->voice_queue, connp->voice_queue + first,
		(size_t)(connp->voice_len - first));
	connp->voice_len -= first;
    }
    memcpy(connp->voice_queue + connp->voice_len, pkt, (size_t)len);
    connp->voice_len += len;
}

void Voice_forward(player_t *speaker, unsigned seq, int channel,
		   const unsigned char *data, int len)
{
    unsigned char pkt[VOICE_S2C_HEADER + VOICE_MAX_PAYLOAD];
    double range;
    int i;

    if (speaker == NULL || !options.voiceChat)
	return;
    if (channel == VOICE_CHANNEL_ALL && options.voiceTeamOnly)
	return;
    if (channel == VOICE_CHANNEL_TEAM && !BIT(world->rules->mode, TEAM_PLAY))
	return;

    range = (channel == VOICE_CHANNEL_TEAM)
	? options.voiceTeamRange : options.voiceRange;

    pkt[0] = PKT_VOICE;
    pkt[1] = (unsigned char)((speaker->id >> 8) & 0xFF);
    pkt[2] = (unsigned char)(speaker->id & 0xFF);
    pkt[3] = (unsigned char)((seq >> 8) & 0xFF);
    pkt[4] = (unsigned char)(seq & 0xFF);
    pkt[5] = (unsigned char)channel;
    pkt[8] = (unsigned char)len;
    memcpy(pkt + VOICE_S2C_HEADER, data, (size_t)len);

    for (i = 0; i < spectatorStart + NumSpectators; i++) {
	player_t *pl;
	connection_t *connp;
	int gain, pan;

	if (i >= NumPlayers && i < spectatorStart)
	    continue;
	pl = Player_by_index(i);
	if (pl == speaker)
	    continue;
	connp = pl->conn;
	if (connp == NULL || !connp->voice_on || connp->rectype == 1)
	    continue;
	if (channel == VOICE_CHANNEL_TEAM
	    && !Players_are_teammates(pl, speaker))
	    continue;

	if ((gain = Voice_gain(speaker, pl, range, &pan)) == 0)
	    continue;
	pkt[6] = (unsigned char)gain;
	pkt[7] = (unsigned char)pan;
	Voice_enqueue(connp, pkt, VOICE_S2C_HEADER + len);
    }
}

/*
 * Move as many queued voice packets into the frame update as fit.
 * Called right before Send_end_of_frame(), so game state always
 * has priority over voice.
 */
void Voice_flush(connection_t *connp)
{
    sockbuf_t *w = &connp->w;
    int off = 0;

    while (off < connp->voice_len) {
	int plen = VOICE_S2C_HEADER
	    + connp->voice_queue[off + VOICE_S2C_HEADER - 1];

	if (w->size - w->len - SOCKBUF_WRITE_SPARE < plen)
	    break;
	memcpy(w->buf + w->len, connp->voice_queue + off, (size_t)plen);
	w->len += plen;
	off += plen;
    }
    if (off > 0) {
	memmove(connp->voice_queue, connp->voice_queue + off,
		(size_t)(connp->voice_len - off));
	connp->voice_len -= off;
    }
}

/*
 * Tuner for the voice options: tell all voice clients about the change.
 */
void Voice_options_changed(void)
{
    int i;

    for (i = 0; i < spectatorStart + NumSpectators; i++) {
	player_t *pl;

	if (i >= NumPlayers && i < spectatorStart)
	    continue;
	pl = Player_by_index(i);
	if (pl->conn != NULL && pl->conn->voice_on) {
	    if (!options.voiceChat)
		pl->conn->voice_len = 0;
	    Send_voice_state(pl->conn);
	}
    }
}
