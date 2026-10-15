/*
 * audio_dump.h: the sound output, recorded to a WAV file (--audio-dump).
 *
 * Not hardware: a copy of every stereo frame the frame loop sends to the
 * speaker (audio.h), written to a file so that the output can be listened to
 * and measured away from the game, with or without a device (headless and
 * --silent runs record too). The file is a RIFF WAVE file of 16-bit signed
 * PCM, two channels, left first, at the output's rate: the canonical 44-byte
 * header (Microsoft and IBM, "Multimedia Programming Interface and Data
 * Specifications 1.0", 1991, "WAVE Form Type"; the PCM format chunk) and the
 * samples little-endian, whatever the host's byte order. The header is
 * written first with no data and written again with the sizes when the
 * recording is finished, at the game's normal end (main). A crash, or an end
 * that skips the finish, leaves the samples in the file and the header's
 * sizes at 0: `sox --ignore-length` reads such a file. Nothing touches the
 * recording from a fault handler (docs/C-RULES.md, "Concurrency").
 *
 * A failed write (a full disk) never stops the game: it is reported once, the
 * recording takes nothing more, and audio_dump_finish fails, so the process
 * ends with a nonzero status.
 *
 * The file goes through host_file.h (opened as every kept file is); it is a
 * test output, not durable state, so it is written in place, not replaced.
 *
 * Single-threaded: the main thread only.
 */
#ifndef PLATFORM_AUDIO_DUMP_H
#define PLATFORM_AUDIO_DUMP_H

#include <stddef.h>
#include <stdint.h>

/* The canonical PCM header: the RIFF chunk's 12 bytes, the 24-byte "fmt "
 * chunk and the "data" chunk's 8-byte header. */
#define AUDIO_DUMP_HEADER_BYTES 44u

/* Bytes of one stereo frame in the file: two 16-bit samples. */
#define AUDIO_DUMP_BYTES_PER_FRAME 4u

/* The most frames a header can describe: the RIFF chunk's 32-bit size counts
 * the 36 header bytes after it and the data. At 65536 Hz that is over four
 * hours (4.55). */
#define AUDIO_DUMP_MAX_FRAMES ((UINT32_MAX - (AUDIO_DUMP_HEADER_BYTES - 8u)) / AUDIO_DUMP_BYTES_PER_FRAME)

/* The highest rate a header can carry: its bytes-a-second field is 32 bits. */
#define AUDIO_DUMP_MAX_SAMPLE_RATE_HZ (UINT32_MAX / AUDIO_DUMP_BYTES_PER_FRAME)

/* The header of a file holding `frames` stereo frames at sample_rate_hz (at
 * most AUDIO_DUMP_MAX_SAMPLE_RATE_HZ); frames beyond AUDIO_DUMP_MAX_FRAMES
 * are left out of the sizes. Pure. */
void audio_dump_header(uint8_t out[AUDIO_DUMP_HEADER_BYTES], uint32_t sample_rate_hz, uint64_t frames);

/* Creates (or truncates) the file at path and writes the header of an empty
 * recording. path is not copied (main's argv): it names the file in messages
 * until the recording is finished. Returns 0, or -1 (reported) for a NULL
 * path, a rate of 0 or above AUDIO_DUMP_MAX_SAMPLE_RATE_HZ, a recording
 * already started, or a file that cannot be created or written; nothing is
 * then recorded. */
int audio_dump_start(const char *path, uint32_t sample_rate_hz);

/* Appends `frames` stereo frames (frames * 2 interleaved int16_t, left
 * first). Returns 0, also when no recording is started or an earlier write
 * failed (nothing to do), or -1 (reported) for the write that fails, or for
 * no samples with frames > 0. After a failed write the recording takes
 * nothing more; the header describes what was written before it. */
int audio_dump_append(const int16_t *stereo, size_t frames);

/* Writes the header's sizes and closes the file. Returns 0, also when no
 * recording is started, or -1 (reported) if a write failed while recording,
 * or the header could not be written or the file closed. */
int audio_dump_finish(void);

/* audio_dump_finish with the result dropped (a failure is still reported):
 * main's stop step. Safe at any time and twice. */
void audio_dump_shutdown(void);

#endif /* PLATFORM_AUDIO_DUMP_H */
