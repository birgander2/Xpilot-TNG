/*
 * XPilot TNG: The Next Generation - Modern SDL3 Client
 * Voice chat: microphone capture, Opus codec, push-to-talk and voice
 * activation, per speaker jitter buffer and playback.
 *
 * Audio is captured as 20 ms frames of 48 kHz mono, encoded with Opus and
 * sent to the server, which forwards it together with a gain and pan for
 * this listener.  Every remote speaker gets its own decoder and SDL audio
 * stream, SDL mixes the streams on a private logical playback device.
 */

#include "sdl3_client.h"

#ifdef HAVE_OPUS
#include <opus.h>
#endif

#define VOICE_MAX_SPEAKERS	16
#define VOICE_RING		16	/* jitter buffer slots (320 ms) */
#define VOICE_TARGET_FRAMES	2	/* min. frames buffered and queued */
#define VOICE_IDLE_MS		5000	/* free a silent speaker slot */
#define VOICE_VAD_HANG_FRAMES	20	/* keep sending 400 ms after speech */
#define VOICE_MAX_BACKLOG	4	/* captured frames before dropping */
#define VOICE_HUD_MS		300	/* show speaker this long */
#define VOICE_PAN_DEPTH		0.75f	/* never mute one ear completely */
#define VOICE_SQUELCH_FRAMES	3	/* radio noise tail after a spurt */

/* voiceTeamFilter / voiceAllFilter */
#define VOICE_FILTER_OFF	0
#define VOICE_FILTER_RADIO	1
#define VOICE_FILTER_PHONE	2

/* Options */
static bool voiceChat = true;
static bool voiceActivation = false;
static bool voiceActivationTeam = true;
static int voiceActivationLevel = -40;
static int voiceVolume = 100;
static int voiceMicGain = 100;
static int voiceBitrate = 24;
static int voiceTeamFilter = VOICE_FILTER_RADIO;
static int voiceAllFilter = VOICE_FILTER_OFF;

/* Server state */
static bool server_known;
static int server_flags;
static bool requested;		/* last voice request sent to server */

static bool ptt_down[VOICE_NUM_CHANNELS];
static int tx_channel = -1;	/* channel currently sent on, -1 = none */

static xp_option_t voice_options[] = {
    XP_BOOL_OPTION(
	"voiceChat",
	true,
	&voiceChat,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Enable voice chat with other players (needs a microphone to talk).\n"),

    XP_BOOL_OPTION(
	"voiceActivation",
	false,
	&voiceActivation,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Send voice automatically while you speak instead of only while\n"
	"holding a push-to-talk key (keyVoiceTeam / keyVoiceAll).\n"),

    XP_BOOL_OPTION(
	"voiceActivationTeam",
	true,
	&voiceActivationTeam,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"With voiceActivation, talk on the team channel (if the server has\n"
	"teams) instead of the channel everybody nearby hears.\n"),

    XP_INT_OPTION(
	"voiceActivationLevel",
	-40,
	-80,
	0,
	&voiceActivationLevel,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Microphone level in dB that triggers voiceActivation.\n"
	"Raise it if background noise opens the channel.\n"),

    XP_INT_OPTION(
	"voiceVolume",
	100,
	0,
	200,
	&voiceVolume,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Playback volume of other players' voices (0-200%%).\n"),

    XP_INT_OPTION(
	"voiceMicGain",
	100,
	0,
	400,
	&voiceMicGain,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Amplification of your microphone (0-400%%).\n"),

    XP_INT_OPTION(
	"voiceBitrate",
	24,
	6,
	64,
	&voiceBitrate,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Opus bitrate for your voice in kbit/s.\n"),

    XP_INT_OPTION(
	"voiceTeamFilter",
	VOICE_FILTER_RADIO,
	VOICE_FILTER_OFF,
	VOICE_FILTER_PHONE,
	&voiceTeamFilter,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Sound of voices on the team channel:\n"
	"0 = unfiltered, 1 = radio, 2 = telephone (narrow band).\n"),

    XP_INT_OPTION(
	"voiceAllFilter",
	VOICE_FILTER_OFF,
	VOICE_FILTER_OFF,
	VOICE_FILTER_PHONE,
	&voiceAllFilter,
	NULL,
	XP_OPTFLAG_CONFIG_DEFAULT,
	"Sound of voices on the all channel:\n"
	"0 = unfiltered, 1 = radio, 2 = telephone (narrow band).\n"),
};

void Store_voice_options(void)
{
    STORE_OPTIONS(voice_options);
}

void Voice_server_state(int flags, int range, int team_range)
{
    bool was_enabled = server_known && (server_flags & VOICE_STATE_ENABLED);

    UNUSED_PARAM(range);
    UNUSED_PARAM(team_range);
    server_known = true;
    server_flags = flags;
#ifdef HAVE_OPUS
    if ((flags & VOICE_STATE_ENABLED) && !was_enabled)
	Add_message("Voice chat is available on this server. "
		    "[*Client reply*]");
    else if (!(flags & VOICE_STATE_ENABLED) && was_enabled)
	Add_message("Voice chat was disabled on this server. "
		    "[*Client reply*]");
#else
    UNUSED_PARAM(was_enabled);
#endif
}

void Voice_ptt(int channel, bool down)
{
    if (channel >= 0 && channel < VOICE_NUM_CHANNELS)
	ptt_down[channel] = down;
}

void Voice_release_all(void)
{
    int i;

    for (i = 0; i < VOICE_NUM_CHANNELS; i++)
	ptt_down[i] = false;
}

#ifdef HAVE_OPUS

#define FRAME_BYTES_MONO	(VOICE_FRAME_SAMPLES * 2)
#define FRAME_BYTES_STEREO	(VOICE_FRAME_SAMPLES * 4)

typedef struct {
    bool		valid;
    uint16_t		seq;
    uint8_t		channel, gain, pan, len;
    uint8_t		data[VOICE_MAX_PAYLOAD];
} voice_packet_t;

/* Biquad in direct form I (RBJ audio EQ cookbook). */
typedef struct {
    float		b0, b1, b2, a1, a2;
} biquad_coef_t;

typedef struct {
    float		x1, x2, y1, y2;
} biquad_state_t;

typedef struct {
    biquad_state_t	hp, lp1, lp2;
    uint32_t		rng;
    int			mode;		/* VOICE_FILTER_* of the spurt */
} voice_filter_t;

typedef struct {
    bool		used;
    int			id;
    OpusDecoder		*dec;
    SDL_AudioStream	*stream;
    voice_packet_t	ring[VOICE_RING];
    uint16_t		next_seq;
    bool		playing;
    Uint64		first_rx_ms;	/* first packet of a talk spurt */
    Uint64		last_rx_ms;
    Uint64		last_play_ms;
    int			last_channel;
    int			last_gain, last_pan;	/* for concealed frames */
    voice_filter_t	filter;
    int			squelch_left;	/* noise tail frames still due */
    float		cur_gain;
    float		cur_pan;
} voice_speaker_t;

/*
 * Speaker slots, their decoders and audio streams are created once when
 * playback opens.  Playout runs in SDL's audio thread (stream callback),
 * so it doesn't depend on how fast the main loop renders.  voice_lock
 * protects the slots; it is never held while calling SDL stream
 * functions from the main thread, so it can't deadlock with SDL's locks.
 */
static voice_speaker_t speakers[VOICE_MAX_SPEAKERS];
static SDL_Mutex *voice_lock;

static SDL_AudioDeviceID play_dev;
static bool play_failed;
static int play_target_frames;	/* frames buffered before playout */
static SDL_AudioStream *mic;
static bool mic_failed;
static OpusEncoder *enc;
static uint16_t tx_seq;
static int vad_hang;

static const SDL_AudioSpec mono_spec = {
    SDL_AUDIO_S16, 1, VOICE_SAMPLE_RATE
};
static const SDL_AudioSpec stereo_spec = {
    SDL_AUDIO_S16, 2, VOICE_SAMPLE_RATE
};

static bool Voice_init_sdl_audio(void)
{
    if (SDL_WasInit(SDL_INIT_AUDIO))
	return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
	warn("Voice: cannot initialize SDL audio: %s", SDL_GetError());
	return false;
    }
    return true;
}

static int Voice_ring_count(const voice_speaker_t *sp)
{
    int i, n = 0;

    for (i = 0; i < VOICE_RING; i++)
	if (sp->ring[i].valid)
	    n++;
    return n;
}

static void Voice_ring_clear(voice_speaker_t *sp)
{
    int i;

    for (i = 0; i < VOICE_RING; i++)
	sp->ring[i].valid = false;
}

/* Return a slot to the pool, keeping its decoder and stream. */
static void Voice_reset_speaker(voice_speaker_t *sp)
{
    Voice_ring_clear(sp);
    opus_decoder_ctl(sp->dec, OPUS_RESET_STATE);
    sp->used = false;
    sp->playing = false;
    sp->id = -1;
}

/*
 * Voice filters.  "Radio" is a 400-2800 Hz band pass with soft clipping,
 * a little hiss, a crackle when the channel opens and a short noise tail
 * when it closes.  "Telephone" is a clean 300-3400 Hz band pass.
 */
#define FRAME_KIND_VOICE	0
#define FRAME_KIND_FIRST	1	/* first frame of a talk spurt */
#define FRAME_KIND_TAIL		2	/* squelch noise after the spurt */

static biquad_coef_t radio_hp, radio_lp, phone_hp, phone_lp;
static bool filters_ready;

static biquad_coef_t Biquad_make(bool highpass, float freq, float q)
{
    float w0 = 2.0f * SDL_PI_F * freq / VOICE_SAMPLE_RATE;
    float c = cosf(w0), alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;
    biquad_coef_t k;

    if (highpass) {
	k.b0 = (1.0f + c) / 2.0f;
	k.b1 = -(1.0f + c);
    } else {
	k.b0 = (1.0f - c) / 2.0f;
	k.b1 = 1.0f - c;
    }
    k.b2 = k.b0;
    k.b0 /= a0;
    k.b1 /= a0;
    k.b2 /= a0;
    k.a1 = -2.0f * c / a0;
    k.a2 = (1.0f - alpha) / a0;
    return k;
}

static inline float Biquad_run(const biquad_coef_t *k, biquad_state_t *st,
			       float x)
{
    float y = k->b0 * x + k->b1 * st->x1 + k->b2 * st->x2
	- k->a1 * st->y1 - k->a2 * st->y2;

    st->x2 = st->x1;
    st->x1 = x;
    st->y2 = st->y1;
    st->y1 = y;
    return y;
}

/* White noise in [-1, 1] (xorshift32, runs in the audio thread). */
static inline float Voice_noise(voice_filter_t *f)
{
    f->rng ^= f->rng << 13;
    f->rng ^= f->rng >> 17;
    f->rng ^= f->rng << 5;
    return (float)(int32_t)f->rng / 2147483648.0f;
}

static int Voice_filter_mode(int channel)
{
    int mode = (channel == VOICE_CHANNEL_TEAM)
	? voiceTeamFilter : voiceAllFilter;

    return (mode >= VOICE_FILTER_OFF && mode <= VOICE_FILTER_PHONE)
	? mode : VOICE_FILTER_OFF;
}

static void Voice_filter_start(voice_filter_t *f, int mode)
{
    uint32_t rng = f->rng ? f->rng : 0x9E3779B9u ^ (uint32_t)(uintptr_t)f;

    if (!filters_ready) {
	radio_hp = Biquad_make(true, 400.0f, 0.707f);
	radio_lp = Biquad_make(false, 2800.0f, 0.707f);
	phone_hp = Biquad_make(true, 300.0f, 0.707f);
	phone_lp = Biquad_make(false, 3400.0f, 0.707f);
	filters_ready = true;
    }
    memset(f, 0, sizeof(*f));
    f->rng = rng;
    f->mode = mode;
}

/* Filter one frame in place.  tail_pos counts down the squelch frames. */
static void Voice_filter_frame(voice_filter_t *f, opus_int16 *pcm, int kind,
			       int tail_pos)
{
    const int burst_len = VOICE_SAMPLE_RATE / 100;	/* 10 ms crackle */
    int i;

    if (f->mode == VOICE_FILTER_OFF)
	return;

    for (i = 0; i < VOICE_FRAME_SAMPLES; i++) {
	float x = pcm[i] / 32768.0f, y;

	if (f->mode == VOICE_FILTER_RADIO) {
	    if (kind == FRAME_KIND_TAIL) {
		/* Fade the noise out over the tail frames. */
		float env = ((float)tail_pos
			     - (float)i / VOICE_FRAME_SAMPLES)
		    / VOICE_SQUELCH_FRAMES;

		x = 0.2f * env * Voice_noise(f);
	    } else {
		x += 0.006f * Voice_noise(f);
		if (kind == FRAME_KIND_FIRST && i < burst_len)
		    x += 0.4f * (1.0f - (float)i / burst_len)
			* Voice_noise(f);
	    }
	    y = Biquad_run(&radio_hp, &f->hp, x);
	    y = Biquad_run(&radio_lp, &f->lp1, y);
	    y = Biquad_run(&radio_lp, &f->lp2, y);
	    /*
	     * Soft clipping: gain 1.6 for quiet signals (make-up for the
	     * lost bass), loud ones saturate at 0.64 like an overdriven radio.
	     */
	    y = 1.6f * tanhf(2.5f * y) / 2.5f;
	} else {
	    y = Biquad_run(&phone_hp, &f->hp, x);
	    y = Biquad_run(&phone_lp, &f->lp1, y);
	    y = Biquad_run(&phone_lp, &f->lp2, y);
	    y *= 1.2f;
	}
	pcm[i] = (opus_int16)MAX(-32768.0f, MIN(32767.0f, y * 32767.0f));
    }
}

/*
 * Mono frame to stereo with gain and pan, ramped over the frame so
 * changes in distance don't click.
 */
static void Voice_mix_frame(voice_speaker_t *sp, const opus_int16 *in,
			    int gain, int pan, opus_int16 *out)
{
    float g = (float)gain / VOICE_GAIN_MAX * (float)voiceVolume / 100.0f;
    float p = (float)(pan - VOICE_PAN_CENTER) / 127.0f;
    float g0 = sp->cur_gain, p0 = sp->cur_pan;
    int i;

    for (i = 0; i < VOICE_FRAME_SAMPLES; i++) {
	float t = (float)(i + 1) / VOICE_FRAME_SAMPLES;
	float gi = g0 + (g - g0) * t;
	float pi = p0 + (p - p0) * t;
	float l = in[i] * gi * (1.0f - VOICE_PAN_DEPTH * MAX(0.0f, pi));
	float r = in[i] * gi * (1.0f - VOICE_PAN_DEPTH * MAX(0.0f, -pi));

	out[2 * i] = (opus_int16)MAX(-32768.0f, MIN(32767.0f, l));
	out[2 * i + 1] = (opus_int16)MAX(-32768.0f, MIN(32767.0f, r));
    }
    sp->cur_gain = g;
    sp->cur_pan = p;
}

/*
 * Produce the next 20 ms of a speaker.  Returns false when there is
 * nothing to play (still buffering, or the talk spurt ended).
 */
static bool Voice_next_frame(voice_speaker_t *sp, Uint64 now,
			     opus_int16 *out)
{
    opus_int16 pcm[VOICE_FRAME_SAMPLES];
    voice_packet_t *pkt;
    int gain, pan, n, kind = FRAME_KIND_VOICE;

    if (!sp->playing) {
	int count = Voice_ring_count(sp);

	/* Buffer enough to cover jitter, but don't wait forever. */
	if (count == 0
	    || (count < play_target_frames
		&& now - sp->first_rx_ms
		   < (Uint64)(play_target_frames + 1) * VOICE_FRAME_MS))
	    return false;
	sp->playing = true;
	kind = FRAME_KIND_FIRST;
	/* Start the fade from the first packet's level, not silence. */
	pkt = &sp->ring[sp->next_seq % VOICE_RING];
	if (pkt->valid) {
	    sp->last_gain = pkt->gain;
	    sp->last_pan = pkt->pan;
	    sp->last_channel = pkt->channel;
	    sp->cur_gain = (float)pkt->gain / VOICE_GAIN_MAX
		* (float)voiceVolume / 100.0f;
	    sp->cur_pan = (float)(pkt->pan - VOICE_PAN_CENTER) / 127.0f;
	}
	Voice_filter_start(&sp->filter, Voice_filter_mode(sp->last_channel));
	sp->squelch_left = (sp->filter.mode == VOICE_FILTER_RADIO)
	    ? VOICE_SQUELCH_FRAMES : 0;
    }

    pkt = &sp->ring[sp->next_seq % VOICE_RING];
    if (pkt->valid && pkt->seq == sp->next_seq) {
	n = opus_decode(sp->dec, pkt->data, pkt->len, pcm,
			VOICE_FRAME_SAMPLES, 0);
	gain = sp->last_gain = pkt->gain;
	pan = sp->last_pan = pkt->pan;
	sp->last_channel = pkt->channel;
	pkt->valid = false;
	sp->next_seq++;
    } else if (Voice_ring_count(sp) > 0) {
	/* Lost packet with later ones waiting: conceal it. */
	n = opus_decode(sp->dec, NULL, 0, pcm, VOICE_FRAME_SAMPLES, 0);
	gain = sp->last_gain;
	pan = sp->last_pan;
	sp->next_seq++;
    } else if (sp->squelch_left > 0) {
	/* Radio: the channel closes with a short burst of noise. */
	memset(pcm, 0, sizeof(pcm));
	n = VOICE_FRAME_SAMPLES;
	kind = FRAME_KIND_TAIL;
	gain = sp->last_gain;
	pan = sp->last_pan;
    } else {
	/* Underrun or end of talk spurt: buffer up again. */
	sp->playing = false;
	return false;
    }
    if (n != VOICE_FRAME_SAMPLES)
	memset(pcm, 0, sizeof(pcm));
    Voice_filter_frame(&sp->filter, pcm, kind, sp->squelch_left);
    if (kind == FRAME_KIND_TAIL)
	sp->squelch_left--;
    Voice_mix_frame(sp, pcm, gain, pan, out);
    if (kind != FRAME_KIND_TAIL)
	sp->last_play_ms = now;
    return true;
}

/* SDL audio thread: the device wants more audio from this speaker. */
static void SDLCALL Voice_stream_cb(void *userdata, SDL_AudioStream *stream,
				    int additional, int total)
{
    voice_speaker_t *sp = userdata;
    opus_int16 out[VOICE_FRAME_SAMPLES * 2];
    Uint64 now = SDL_GetTicks();

    UNUSED_PARAM(total);
    SDL_LockMutex(voice_lock);
    while (additional > 0 && sp->used && Voice_next_frame(sp, now, out)) {
	SDL_PutAudioStreamData(stream, out, (int)sizeof(out));
	additional -= (int)sizeof(out);
    }
    SDL_UnlockMutex(voice_lock);
}

static void Voice_close_playback(void)
{
    int i;

    /* Destroying a stream waits for a running callback to finish. */
    for (i = 0; i < VOICE_MAX_SPEAKERS; i++) {
	if (speakers[i].stream)
	    SDL_DestroyAudioStream(speakers[i].stream);
	if (speakers[i].dec)
	    opus_decoder_destroy(speakers[i].dec);
	memset(&speakers[i], 0, sizeof(speakers[i]));
    }
    if (play_dev) {
	SDL_CloseAudioDevice(play_dev);
	play_dev = 0;
    }
    if (voice_lock) {
	SDL_DestroyMutex(voice_lock);
	voice_lock = NULL;
    }
}

static bool Voice_open_playback(void)
{
    SDL_AudioSpec spec;
    int i, err, frames = 0;

    if (play_dev)
	return true;
    if (play_failed || !Voice_init_sdl_audio())
	return false;

    play_failed = true;		/* until everything below worked */
    if ((voice_lock = SDL_CreateMutex()) == NULL) {
	warn("Voice: cannot create mutex: %s", SDL_GetError());
	return false;
    }
    play_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
				   &stereo_spec);
    if (!play_dev) {
	warn("Voice: cannot open playback device: %s", SDL_GetError());
	Voice_close_playback();
	return false;
    }
    for (i = 0; i < VOICE_MAX_SPEAKERS; i++) {
	voice_speaker_t *sp = &speakers[i];

	sp->id = -1;
	sp->dec = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &err);
	sp->stream = SDL_CreateAudioStream(&stereo_spec, &stereo_spec);
	if (!sp->dec || !sp->stream
	    || !SDL_SetAudioStreamGetCallback(sp->stream, Voice_stream_cb, sp)
	    || !SDL_BindAudioStream(play_dev, sp->stream)) {
	    warn("Voice: cannot set up playback stream: %s", SDL_GetError());
	    Voice_close_playback();
	    return false;
	}
    }

    /*
     * Buffer at least one device period plus one frame before a talk
     * spurt starts, so network jitter doesn't cause dropouts.
     */
    play_target_frames = VOICE_TARGET_FRAMES;
    if (SDL_GetAudioDeviceFormat(play_dev, &spec, &frames)
	&& frames > 0 && spec.freq > 0) {
	int buf_ms = frames * 1000 / spec.freq;

	play_target_frames = MAX(play_target_frames,
				 (buf_ms + VOICE_FRAME_MS - 1)
				 / VOICE_FRAME_MS + 1);
    }
    printf("Voice playback: device buffer %d frames, %d ms jitter buffer.\n",
	   frames, play_target_frames * VOICE_FRAME_MS);

    SDL_ResumeAudioDevice(play_dev);
    play_failed = false;
    return true;
}

static bool Voice_open_mic(void)
{
    int err;

    if (mic)
	return true;
    if (mic_failed || !Voice_init_sdl_audio())
	return false;

    mic = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
				    &mono_spec, NULL, NULL);
    if (!mic) {
	warn("Voice: cannot open microphone: %s", SDL_GetError());
	Add_message("Voice chat: no microphone available. [*Client reply*]");
	mic_failed = true;
	return false;
    }
    enc = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
			      &err);
    if (!enc) {
	warn("Voice: cannot create Opus encoder: %s", opus_strerror(err));
	SDL_DestroyAudioStream(mic);
	mic = NULL;
	mic_failed = true;
	return false;
    }
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));
    SDL_ResumeAudioStreamDevice(mic);
    return true;
}

/* Find or allocate the slot of a speaker.  Called with voice_lock held. */
static voice_speaker_t *Voice_speaker(int id)
{
    voice_speaker_t *free_slot = NULL;
    int i;

    for (i = 0; i < VOICE_MAX_SPEAKERS; i++) {
	if (speakers[i].used && speakers[i].id == id)
	    return &speakers[i];
	if (!speakers[i].used && free_slot == NULL)
	    free_slot = &speakers[i];
    }
    if (free_slot) {
	free_slot->used = true;
	free_slot->id = id;
	free_slot->playing = false;
	free_slot->last_rx_ms = 0;
	free_slot->last_play_ms = 0;
    }
    return free_slot;
}

void Voice_received(int id, unsigned seq, int channel, int gain, int pan,
		    const unsigned char *data, int len)
{
    voice_speaker_t *sp;
    voice_packet_t *pkt;
    other_t *other;
    int16_t ahead;
    Uint64 now = SDL_GetTicks();

    if (!voiceChat || len <= 0 || len > VOICE_MAX_PAYLOAD)
	return;
    if ((other = Other_by_id(id)) != NULL && other->ignorelevel > 0)
	return;
    if (!Voice_open_playback())
	return;

    SDL_LockMutex(voice_lock);
    if ((sp = Voice_speaker(id)) == NULL) {
	SDL_UnlockMutex(voice_lock);
	return;
    }
    if (!sp->playing && Voice_ring_count(sp) == 0) {
	/* Start of a talk spurt. */
	sp->next_seq = (uint16_t)seq;
	sp->first_rx_ms = now;
    }
    ahead = (int16_t)((uint16_t)seq - sp->next_seq);
    if (ahead >= 0) {
	if (ahead >= VOICE_RING) {
	    /* Far ahead (long stall): restart from this packet. */
	    Voice_ring_clear(sp);
	    sp->next_seq = (uint16_t)seq;
	    sp->playing = false;
	    sp->first_rx_ms = now;
	}
	pkt = &sp->ring[seq % VOICE_RING];
	pkt->valid = true;
	pkt->seq = (uint16_t)seq;
	pkt->channel = (uint8_t)channel;
	pkt->gain = (uint8_t)gain;
	pkt->pan = (uint8_t)pan;
	pkt->len = (uint8_t)len;
	memcpy(pkt->data, data, (size_t)len);
	sp->last_rx_ms = now;
    }
    /* else: too late, that frame was already concealed */
    SDL_UnlockMutex(voice_lock);
}

/*
 * Which channel to send the current frame on, -1 for none.
 */
static int Voice_tx_channel(float level_db)
{
    bool team_play = (server_flags & VOICE_STATE_TEAM_PLAY) != 0;
    bool team_only = (server_flags & VOICE_STATE_TEAM_ONLY) != 0;
    int ch = -1;

    if (ptt_down[VOICE_CHANNEL_TEAM])
	ch = team_play ? VOICE_CHANNEL_TEAM : VOICE_CHANNEL_ALL;
    else if (ptt_down[VOICE_CHANNEL_ALL])
	ch = VOICE_CHANNEL_ALL;
    else if (voiceActivation) {
	if (level_db > (float)voiceActivationLevel)
	    vad_hang = VOICE_VAD_HANG_FRAMES;
	if (vad_hang > 0) {
	    vad_hang--;
	    ch = (voiceActivationTeam && team_play)
		? VOICE_CHANNEL_TEAM : VOICE_CHANNEL_ALL;
	}
    }
    if (ch == VOICE_CHANNEL_ALL && team_only)
	ch = team_play ? VOICE_CHANNEL_TEAM : -1;
    return ch;
}

static void Voice_capture(void)
{
    opus_int16 pcm[VOICE_FRAME_SAMPLES];
    unsigned char data[VOICE_MAX_PAYLOAD];
    bool want_mic = voiceActivation
	|| ptt_down[VOICE_CHANNEL_TEAM] || ptt_down[VOICE_CHANNEL_ALL];

    if (!want_mic && !mic) {
	tx_channel = -1;
	return;
    }
    if (!Voice_open_mic()) {
	tx_channel = -1;
	return;
    }

    /* Don't let a stalled client send stale audio. */
    while (SDL_GetAudioStreamAvailable(mic)
	   > VOICE_MAX_BACKLOG * FRAME_BYTES_MONO)
	SDL_GetAudioStreamData(mic, pcm, FRAME_BYTES_MONO);

    while (SDL_GetAudioStreamAvailable(mic) >= FRAME_BYTES_MONO) {
	double sum = 0.0;
	float level_db;
	int i, ch, n;

	if (SDL_GetAudioStreamData(mic, pcm, FRAME_BYTES_MONO)
	    != FRAME_BYTES_MONO)
	    break;
	for (i = 0; i < VOICE_FRAME_SAMPLES; i++) {
	    int s = pcm[i] * voiceMicGain / 100;

	    pcm[i] = (opus_int16)MAX(-32768, MIN(32767, s));
	    sum += (double)pcm[i] * pcm[i];
	}
	level_db = (float)(10.0 * log10(sum / VOICE_FRAME_SAMPLES
					/ (32768.0 * 32768.0) + 1e-12));

	ch = Voice_tx_channel(level_db);
	if (ch < 0) {
	    tx_channel = -1;
	    continue;
	}
	if (tx_channel < 0) {
	    /* New talk spurt: fresh encoder state, current bitrate. */
	    opus_encoder_ctl(enc, OPUS_RESET_STATE);
	    opus_encoder_ctl(enc, OPUS_SET_BITRATE(voiceBitrate * 1000));
	}
	tx_channel = ch;

	n = opus_encode(enc, pcm, VOICE_FRAME_SAMPLES, data, sizeof(data));
	if (n > 0)
	    Send_voice(tx_seq++, ch, data, n);
    }
}

void Voice_update(void)
{
    bool want;
    Uint64 now;
    int i;

    if (version < TNG_VOICE_VERSION)
	return;

    want = voiceChat;
    if (want != requested) {
	if (Send_voice_request(want) == -1)
	    return;
	requested = want;
	if (!want) {
	    server_known = false;
	    tx_channel = -1;
	}
    }
    if (!want)
	return;

    if (server_known && (server_flags & VOICE_STATE_ENABLED))
	Voice_capture();
    else
	tx_channel = -1;

    /* Playout happens in the audio thread, only recycle idle slots here. */
    if (!play_dev)
	return;
    now = SDL_GetTicks();
    SDL_LockMutex(voice_lock);
    for (i = 0; i < VOICE_MAX_SPEAKERS; i++) {
	voice_speaker_t *sp = &speakers[i];

	if (sp->used && !sp->playing && Voice_ring_count(sp) == 0
	    && now - sp->last_rx_ms > VOICE_IDLE_MS)
	    Voice_reset_speaker(sp);
    }
    SDL_UnlockMutex(voice_lock);
}

void Voice_paint(void)
{
    char buf[MAX_CHARS + 16];
    float x_right = (float)(PLAYFIELD_X + WINSCALE(ext_view_width) - 10);
    float y = (float)draw_height - 30.0f;
    Uint64 now;
    int i;

    if (!voiceChat || version < TNG_VOICE_VERSION)
	return;

    if (tx_channel >= 0) {
	snprintf(buf, sizeof(buf), "VOICE %s",
		 tx_channel == VOICE_CHANNEL_TEAM ? "TEAM" : "ALL");
	SDL3_Draw_Text(buf, x_right - SDL3_Text_Width(buf), y,
		       XP_COLOR_RED);
	y -= 18.0f;
    }

    if (!play_dev)
	return;
    now = SDL_GetTicks();
    for (i = 0; i < VOICE_MAX_SPEAKERS; i++) {
	voice_speaker_t *sp = &speakers[i];
	other_t *other;
	int id, channel;
	bool talking;

	SDL_LockMutex(voice_lock);
	talking = sp->used && now - sp->last_play_ms <= VOICE_HUD_MS;
	id = sp->id;
	channel = sp->last_channel;
	SDL_UnlockMutex(voice_lock);
	if (!talking)
	    continue;

	other = Other_by_id(id);
	snprintf(buf, sizeof(buf), "%s%s",
		 channel == VOICE_CHANNEL_TEAM ? "[T] " : "",
		 other ? other->nick_name : "?");
	SDL3_Draw_Text(buf, x_right - SDL3_Text_Width(buf), y,
		       channel == VOICE_CHANNEL_TEAM
		       ? XP_COLOR_GREEN : XP_COLOR_WHITE);
	y -= 18.0f;
    }
}

void Voice_cleanup(void)
{
    Voice_close_playback();
    if (mic) {
	SDL_DestroyAudioStream(mic);
	mic = NULL;
    }
    if (enc) {
	opus_encoder_destroy(enc);
	enc = NULL;
    }
}

#else /* HAVE_OPUS */

/* Built without Opus: never request voice, so nothing is ever received. */
void Voice_received(int id, unsigned seq, int channel, int gain, int pan,
		    const unsigned char *data, int len)
{
    UNUSED_PARAM(id); UNUSED_PARAM(seq); UNUSED_PARAM(channel);
    UNUSED_PARAM(gain); UNUSED_PARAM(pan); UNUSED_PARAM(data);
    UNUSED_PARAM(len);
}

void Voice_update(void)
{
    UNUSED_PARAM(requested);
    UNUSED_PARAM(tx_channel);
}

void Voice_paint(void) {}
void Voice_cleanup(void) {}

#endif /* HAVE_OPUS */
