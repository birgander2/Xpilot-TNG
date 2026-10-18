/*
 * XPilot TNG: The Next Generation
 * Voice chat protocol constants shared by client and server.
 *
 * Voice is only exchanged with peers speaking protocol >= TNG_VOICE_VERSION
 * and only after the client sent PKT_REQUEST_VOICE.  The server never
 * decodes audio, it only forwards Opus frames and attaches a per-listener
 * gain and stereo pan.
 *
 * Wire formats:
 *   client -> server  PKT_REQUEST_VOICE  %c%c               on
 *   server -> client  PKT_VOICE_STATE    %c%c%hd%hd%c%c%c   flags, range,
 *                                                           team range,
 *                                                           bitrate (kbit/s),
 *                                                           team filter,
 *                                                           all filter
 *                                                           (reliable)
 *   client -> server  PKT_VOICE          %c%hu%c%c + data   seq, channel,
 *                                                           len
 *   server -> client  PKT_VOICE          %c%hd%hu%c%c%c%c + data
 *                                        id, seq, channel, gain, pan, len
 */

#ifndef VOICE_H
#define VOICE_H

#define VOICE_CHANNEL_ALL	0
#define VOICE_CHANNEL_TEAM	1
#define VOICE_NUM_CHANNELS	2

/* PKT_VOICE_STATE flags */
#define VOICE_STATE_ENABLED	(1 << 0)
#define VOICE_STATE_TEAM_ONLY	(1 << 1)
#define VOICE_STATE_TEAM_PLAY	(1 << 2)

/* Playback filter per channel, set by the server. */
#define VOICE_FILTER_OFF	0
#define VOICE_FILTER_RADIO	1
#define VOICE_FILTER_PHONE	2

/* Opus bitrate limits in kbit/s. */
#define VOICE_BITRATE_MIN	6
#define VOICE_BITRATE_MAX	64

#define VOICE_SAMPLE_RATE	48000
#define VOICE_FRAME_MS		20
#define VOICE_FRAME_SAMPLES	(VOICE_SAMPLE_RATE * VOICE_FRAME_MS / 1000)
#define VOICE_MAX_PAYLOAD	200	/* bytes of one Opus frame */

/* Pan is sent as unsigned byte, VOICE_PAN_CENTER means centered. */
#define VOICE_PAN_CENTER	128
#define VOICE_GAIN_MAX		255

/* Size of the server -> client PKT_VOICE header in bytes. */
#define VOICE_S2C_HEADER	9

#endif
