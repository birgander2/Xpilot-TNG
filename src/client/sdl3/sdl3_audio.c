/*
 * XPilot TNG native audio module using SDL3 audio streams
 */

#include "sdl3_client.h"
#include "audio.h"
#include "caudio.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_AUDIO_CHANNELS 16
#define VOL_THRESHOLD      10

typedef struct {
    Uint8  *buffer;
    int     length;
    float   gain;
    bool    loop;
} sdl3_sample_t;

typedef struct {
    SDL_AudioStream *stream;
    sdl3_sample_t   *sample;
    int              volume;
    long             updated;
    bool             looping;
} sdl3_channel_t;

static SDL_AudioDeviceID audio_device = 0;
static SDL_AudioSpec     device_spec;
static sdl3_channel_t    channels[MAX_AUDIO_CHANNELS];
static int               next_channel = 0;
static bool              audio_initialized = false;
static bool              audio_enabled = false;

static void parse_sample_info(const char *raw_filename, char *clean_path, size_t path_sz,
                              float *gain_out, bool *loop_out)
{
    char tmp[PATH_MAX];
    char *token;

    *gain_out = 1.0f;
    *loop_out = false;

    strlcpy(tmp, raw_filename, sizeof(tmp));
    token = strtok(tmp, ",");
    strlcpy(clean_path, token ? token : raw_filename, path_sz);

    if ((token = strtok(NULL, ",")) != NULL) {
        *gain_out = (float)atof(token);
        if ((token = strtok(NULL, ",")) != NULL) {
            *loop_out = (atoi(token) != 0);
        }
    }
}

static bool resolve_sound_path(const char *input_path, char *resolved, size_t resolved_sz)
{
    if (access(input_path, R_OK) == 0) {
        strlcpy(resolved, input_path, resolved_sz);
        return true;
    }

    const char *base = strrchr(input_path, '/');
    base = base ? (base + 1) : input_path;

    int n = snprintf(resolved, resolved_sz, "%s%s", Conf_sounddir(), base);
    if (n >= 0 && (size_t)n < resolved_sz && access(resolved, R_OK) == 0)
        return true;

    n = snprintf(resolved, resolved_sz, "lib/sound/%s", base);
    if (n >= 0 && (size_t)n < resolved_sz && access(resolved, R_OK) == 0)
        return true;

    n = snprintf(resolved, resolved_sz, "../lib/sound/%s", base);
    if (n >= 0 && (size_t)n < resolved_sz && access(resolved, R_OK) == 0)
        return true;

    return false;
}

static sdl3_sample_t *sample_load(const char *filename)
{
    char clean_path[PATH_MAX];
    char resolved_path[PATH_MAX];
    float gain = 1.0f;
    bool loop = false;

    parse_sample_info(filename, clean_path, sizeof(clean_path), &gain, &loop);
    if (!resolve_sound_path(clean_path, resolved_path, sizeof(resolved_path))) {
        warn("SDL3 Audio: Could not find sound file '%s'", clean_path);
        return NULL;
    }

    SDL_AudioSpec wav_spec;
    Uint8 *wav_buf = NULL;
    Uint32 wav_len = 0;

    if (!SDL_LoadWAV(resolved_path, &wav_spec, &wav_buf, &wav_len)) {
        warn("SDL3 Audio: Failed to load WAV '%s': %s", resolved_path, SDL_GetError());
        return NULL;
    }

    Uint8 *conv_buf = NULL;
    int conv_len = 0;
    if (!SDL_ConvertAudioSamples(&wav_spec, wav_buf, (int)wav_len,
                                 &device_spec, &conv_buf, &conv_len)) {
        warn("SDL3 Audio: Failed to convert WAV '%s': %s", resolved_path, SDL_GetError());
        SDL_free(wav_buf);
        return NULL;
    }
    SDL_free(wav_buf);

    sdl3_sample_t *sample = (sdl3_sample_t *)calloc(1, sizeof(sdl3_sample_t));
    if (!sample) {
        SDL_free(conv_buf);
        return NULL;
    }

    sample->buffer = conv_buf;
    sample->length = conv_len;
    sample->gain   = gain;
    sample->loop   = loop;
    return sample;
}

int SDL3_Init_Audio(void)
{
    if (audio_initialized)
        return audio_enabled ? 0 : -1;

    audio_initialized = true;
    memset(channels, 0, sizeof(channels));

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            fprintf(stderr, "Warning: Failed to initialize SDL3 audio subsystem: %s\n", SDL_GetError());
            audio_enabled = false;
            return -1;
        }
    }

    device_spec.format   = SDL_AUDIO_S16LE;
    device_spec.channels = 2;
    device_spec.freq     = 44100;

    audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device_spec);
    if (!audio_device) {
        fprintf(stderr, "Warning: Failed to open SDL3 audio device: %s\n", SDL_GetError());
        audio_enabled = false;
        return -1;
    }

    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        channels[i].stream = SDL_CreateAudioStream(&device_spec, &device_spec);
        if (channels[i].stream) {
            SDL_BindAudioStream(audio_device, channels[i].stream);
        }
    }

    SDL_ResumeAudioDevice(audio_device);
    audio_enabled = true;
    printf("SDL3 Audio initialized successfully (%d mixer channels).\n", MAX_AUDIO_CHANNELS);
    return 0;
}

void SDL3_Cleanup_Audio(void)
{
    if (!audio_initialized)
        return;

    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        if (channels[i].stream) {
            SDL_DestroyAudioStream(channels[i].stream);
            channels[i].stream = NULL;
        }
        channels[i].sample  = NULL;
        channels[i].looping = false;
    }

    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    audio_enabled = false;
    audio_initialized = false;
}

int audioDeviceInit(char *display)
{
    (void)display;
    return SDL3_Init_Audio();
}

void audioDevicePlay(char *filename, int type, int volume, void **priv)
{
    (void)type;
    if (!audio_enabled || !audio_device)
        return;

    sdl3_sample_t *sample = (sdl3_sample_t *)(*priv);
    if (!sample) {
        sample = sample_load(filename);
        if (!sample)
            return;
        *priv = sample;
    }

    float gain = sample->gain * ((float)volume / 100.0f);
    if (gain < 0.0f) gain = 0.0f;

    /* If looping sample is already active on a channel, update its volume & timestamp */
    if (sample->loop) {
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
            if (channels[i].looping && channels[i].sample == sample) {
                if (ABS(channels[i].volume - volume) >= VOL_THRESHOLD) {
                    SDL_SetAudioStreamGain(channels[i].stream, gain);
                    channels[i].volume = volume;
                }
                channels[i].updated = loops;
                if (SDL_GetAudioStreamAvailable(channels[i].stream) < sample->length) {
                    SDL_PutAudioStreamData(channels[i].stream, sample->buffer, sample->length);
                }
                return;
            }
        }
    }

    /* Find an idle non-looping channel first, otherwise round-robin a non-looping channel */
    int chosen = -1;
    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        int idx = (next_channel + i) % MAX_AUDIO_CHANNELS;
        if (!channels[idx].looping && channels[idx].stream &&
            SDL_GetAudioStreamAvailable(channels[idx].stream) == 0) {
            chosen = idx;
            break;
        }
    }
    if (chosen == -1) {
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
            int idx = (next_channel + i) % MAX_AUDIO_CHANNELS;
            if (!channels[idx].looping && channels[idx].stream) {
                chosen = idx;
                break;
            }
        }
    }
    if (chosen == -1)
        return;

    next_channel = (chosen + 1) % MAX_AUDIO_CHANNELS;

    sdl3_channel_t *ch = &channels[chosen];
    SDL_ClearAudioStream(ch->stream);
    SDL_SetAudioStreamGain(ch->stream, gain);
    SDL_PutAudioStreamData(ch->stream, sample->buffer, sample->length);
    if (sample->loop) {
        SDL_PutAudioStreamData(ch->stream, sample->buffer, sample->length);
    }

    ch->sample  = sample;
    ch->volume  = volume;
    ch->updated = loops;
    ch->looping = sample->loop;
}

void audioDeviceEvents(void)
{
}

void audioDeviceUpdate(void)
{
    if (!audio_enabled)
        return;

    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        if (channels[i].looping && channels[i].stream) {
            if (!sound || maxVolume <= 0 || channels[i].updated < loops - 1) {
                SDL_ClearAudioStream(channels[i].stream);
                channels[i].looping = false;
                channels[i].sample  = NULL;
            } else if (channels[i].sample &&
                       SDL_GetAudioStreamAvailable(channels[i].stream) < channels[i].sample->length) {
                SDL_PutAudioStreamData(channels[i].stream,
                                       channels[i].sample->buffer,
                                       channels[i].sample->length);
            }
        }
    }
}

void audioDeviceFree(void *priv)
{
    if (!priv)
        return;

    sdl3_sample_t *sample = (sdl3_sample_t *)priv;
    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        if (channels[i].sample == sample) {
            if (channels[i].stream)
                SDL_ClearAudioStream(channels[i].stream);
            channels[i].sample  = NULL;
            channels[i].looping = false;
        }
    }

    if (sample->buffer)
        SDL_free(sample->buffer);
    free(sample);
}

void audioDeviceClose(void)
{
    SDL3_Cleanup_Audio();
}

void SDL3_Play_Sound(int sound_ind, int volume)
{
    Handle_audio(sound_ind, volume);
}

