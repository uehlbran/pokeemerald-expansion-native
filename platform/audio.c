/*
 * audio.c: the speaker, via SDL2's queue API.
 *
 * Real hardware: the sound mixer's output is a continuous stream at the PWM
 * stage's sampling rate (32768 Hz by default, GBATEK SOUNDBIAS), consumed in
 * real time; frame.h says what rate the platform opens the device at.
 * Here: a push model, SDL_OpenAudioDevice (not the legacy SDL_OpenAudio)
 * with no callback, and SDL_QueueAudio once per frame, as the reference
 * ports do (docs/research/); the ports' pitfalls avoided here are the
 * unbounded queue, unchecked calls and missing cleanup.
 *
 * Format: signed 16-bit native-endian stereo. allowed_changes = 0 makes SDL
 * convert internally so the obtained spec equals the request; the obtained
 * spec is still compared and a mismatch is an error.
 *
 * Queue bound: the GBA frame period is 280896 cycles at 16777216 Hz
 * (59.7275 Hz, GBATEK "LCD Dimensions and Timings"). Four frame periods
 * (67 ms) is the most audio allowed to wait: enough to absorb a late frame
 * or two, small enough that the sound does not lag the picture visibly.
 *
 * SDL docs: https://wiki.libsdl.org/SDL2/CategoryAudio
 */
#include "audio.h"

#include <stdio.h>

#include <SDL.h>

#include "gba_timing.h"

#define MODULE "audio"

#define CHANNELS 2u
#define BYTES_PER_SAMPLE_FRAME ((size_t)CHANNELS * sizeof(int16_t))
/* SDL wants a power of two. 512 frames is 11 ms at 44.1 kHz. */
#define DEVICE_BUFFER_SAMPLES 512u

#define MAX_QUEUED_VIDEO_FRAMES 4u

static struct {
    bool initialised;
    bool silent;
    bool paused;
    uint32_t sample_rate_hz;
    SDL_AudioDeviceID device;
    bool owns_audio_subsystem;
    uint64_t dropped_frames;
    uint64_t underruns;
    uint64_t total_frames;
} state;

static int sdl_error(const char *call)
{
    (void)fprintf(stderr, MODULE ": %s failed: %s\n", call, SDL_GetError());
    return -1;
}

size_t audio_max_queued_frames(uint32_t sample_rate_hz)
{
    /* Frames per video frame, rounded up, times the allowed video frames. */
    uint64_t per_video_frame = ((uint64_t)sample_rate_hz * GBA_CYCLES_PER_FRAME + GBA_CPU_HZ - 1u) / GBA_CPU_HZ;
    return (size_t)(per_video_frame * MAX_QUEUED_VIDEO_FRAMES);
}

static int open_device(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = (int)state.sample_rate_hz;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)CHANNELS;
    want.samples = (Uint16)DEVICE_BUFFER_SAMPLES;
    want.callback = NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return sdl_error("SDL_InitSubSystem(SDL_INIT_AUDIO)");
    state.owns_audio_subsystem = true;
    state.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (state.device == 0)
        return sdl_error("SDL_OpenAudioDevice");
    if (have.freq != want.freq || have.format != want.format || have.channels != want.channels) {
        (void)fprintf(stderr, MODULE ": SDL_OpenAudioDevice returned a different spec than requested\n");
        return -1;
    }
    return 0; /* SDL opens devices paused */
}

int audio_init(uint32_t sample_rate_hz, enum audio_output output)
{
    if (sample_rate_hz == 0u || sample_rate_hz > AUDIO_MAX_SAMPLE_RATE_HZ) {
        (void)fprintf(stderr, MODULE ": audio_init: sample rate %u Hz out of range 1..%u\n", sample_rate_hz,
                      AUDIO_MAX_SAMPLE_RATE_HZ);
        return -1;
    }
    if (state.initialised) {
        (void)fprintf(stderr, MODULE ": audio_init: already initialised\n");
        return -1;
    }
    state.sample_rate_hz = sample_rate_hz;
    state.silent = (output == AUDIO_SILENT);
    state.paused = true;
    state.dropped_frames = 0;
    state.underruns = 0;
    state.total_frames = 0;
    state.initialised = true;
    if (!state.silent && open_device() != 0) {
        audio_shutdown();
        return -1;
    }
    return 0;
}

int audio_set_paused(bool paused)
{
    if (!state.initialised) {
        (void)fprintf(stderr, MODULE ": audio_set_paused: not initialised\n");
        return -1;
    }
    state.paused = paused;
    if (state.silent)
        return 0;
    SDL_PauseAudioDevice(state.device, paused ? 1 : 0); /* returns void */
    return 0;
}

int audio_queue(const int16_t *stereo, size_t frames)
{
    if (!state.initialised || (stereo == NULL && frames != 0u)) {
        (void)fprintf(stderr, MODULE ": audio_queue: not initialised or no samples\n");
        return -1;
    }
    if (frames == 0u)
        return 0;
    size_t max_frames = audio_max_queued_frames(state.sample_rate_hz);
    if (frames > max_frames) { /* also keeps the byte count inside Uint32 */
        state.dropped_frames += frames;
        return 0;
    }
    if (state.silent) {
        state.total_frames += frames;
        return 0;
    }
    uint32_t queued = SDL_GetQueuedAudioSize(state.device);
    if (queued == 0u && !state.paused)
        state.underruns++;
    if ((size_t)queued / BYTES_PER_SAMPLE_FRAME + frames > max_frames) {
        state.dropped_frames += frames;
        return 0;
    }
    if (SDL_QueueAudio(state.device, stereo, (Uint32)(frames * BYTES_PER_SAMPLE_FRAME)) != 0)
        return sdl_error("SDL_QueueAudio");
    state.total_frames += frames;
    return 0;
}

void audio_clear(void)
{
    if (state.initialised && !state.silent)
        SDL_ClearQueuedAudio(state.device);
}

void audio_shutdown(void)
{
    if (state.device != 0)
        SDL_CloseAudioDevice(state.device);
    if (state.owns_audio_subsystem)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    state.device = 0;
    state.owns_audio_subsystem = false;
    state.initialised = false;
    state.silent = false;
    state.paused = true;
}

uint32_t audio_queued_bytes(void)
{
    if (!state.initialised || state.silent)
        return 0;
    return SDL_GetQueuedAudioSize(state.device);
}

uint64_t audio_dropped_frames(void)
{
    return state.dropped_frames;
}
uint64_t audio_underruns(void)
{
    return state.underruns;
}
uint64_t audio_total_frames(void)
{
    return state.total_frames;
}
