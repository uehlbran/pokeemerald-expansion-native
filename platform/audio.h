/*
 * audio.h: the speaker, via SDL.
 *
 * Stands in for the GBA's sound output (GBATEK "GBA Sound Controller"
 * ends in a 2-channel DAC). The sound mixer produces one video frame of
 * signed 16-bit stereo samples per frame and queues them here. This is
 * the only module that calls SDL's audio API; the other modules that use SDL
 * (video, input, frame, frame_pacer, main) name what they use in their own
 * headers. Design:
 * docs/RESEARCH.md "Audio".
 *
 * Threading: single-threaded by design. There is no audio callback; SDL's
 * own audio thread drains a queue that SDL_QueueAudio fills under SDL's
 * lock. No game or platform state is touched from any other thread.
 *
 * The queue is bounded (audio_max_queued_frames). SDL cannot drop the
 * oldest queued audio, so when a block would exceed the bound it is not
 * queued and is counted in audio_dropped_frames().
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sanity ceiling for the requested rate; the output's 32768 Hz (frame.h) is
 * far lower. */
#define AUDIO_MAX_SAMPLE_RATE_HZ 192000u

/* AUDIO_SILENT opens no device: queue calls only count frames (headless
 * gates). */
enum audio_output { AUDIO_DEVICE, AUDIO_SILENT };

/* Returns 0 on success. Paused-at-init protocol: the device starts paused,
 * so queue the first frames, then call audio_set_paused(false). */
int audio_init(uint32_t sample_rate_hz, enum audio_output output);

/* stereo: frames * 2 interleaved int16_t (left, right). Returns 0 on
 * success, including when the block was dropped because the queue is full
 * (that is counted, not an error). Nonzero on a bad argument or SDL error.
 * If the device is running and nothing is queued, one underrun is counted
 * before queuing. */
int audio_queue(const int16_t *stereo, size_t frames);

/* Pause or resume playback. No-op in silent mode. Returns 0 on success. */
int audio_set_paused(bool paused);

/* Discards everything queued and not yet played. No-op in silent mode. */
void audio_clear(void);

/* Safe on partial init, without init, and twice. */
void audio_shutdown(void);

/* Maximum frames the queue may hold: 4 video frames' worth of samples at
 * sample_rate_hz (see audio.c). 0 for a zero rate. */
size_t audio_max_queued_frames(uint32_t sample_rate_hz);

uint32_t audio_queued_bytes(void); /* 0 in silent mode */
uint64_t audio_dropped_frames(void);
uint64_t audio_underruns(void);
uint64_t audio_total_frames(void); /* frames accepted for queuing, silent or not */

#endif /* AUDIO_H */
