/*
 * audio_dump.c: the sound output, recorded to a WAV file (audio_dump.h).
 *
 * Written from the RIFF WAVE layout (Microsoft and IBM, "Multimedia
 * Programming Interface and Data Specifications 1.0", 1991: the RIFF chunk,
 * the "fmt " chunk of WAVE_FORMAT_PCM, the "data" chunk; every number
 * little-endian). The samples are encoded byte by byte, so the file is the
 * same on any host.
 */
#include "audio_dump.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host_file.h"

#define MODULE "audio_dump"

/* The chunk and form identifiers, four bytes each, no terminator. */
#define TAG_BYTES 4u
#define TAG_RIFF "RIFF"
#define TAG_WAVE "WAVE"
#define TAG_FMT "fmt "
#define TAG_DATA "data"

/* The RIFF chunk's size counts every byte after its identifier and size. */
#define RIFF_CHUNK_PREFIX_BYTES 8u
/* The "fmt " chunk of uncompressed PCM: format tag 1, 16 bytes of fields. */
#define WAVE_FORMAT_PCM 1u
#define FMT_CHUNK_BYTES 16u
#define CHANNELS 2u
#define BITS_PER_SAMPLE 16u

_Static_assert(AUDIO_DUMP_BYTES_PER_FRAME == CHANNELS * BITS_PER_SAMPLE / 8u, "a frame is two 16-bit samples");
_Static_assert(AUDIO_DUMP_HEADER_BYTES
                   == RIFF_CHUNK_PREFIX_BYTES + TAG_BYTES + RIFF_CHUNK_PREFIX_BYTES + FMT_CHUNK_BYTES
                          + RIFF_CHUNK_PREFIX_BYTES,
               "RIFF, WAVE, the fmt chunk and the data chunk's prefix");

/* Frames encoded per write: a stack buffer of 4 KiB. */
#define FRAMES_PER_WRITE 1024u

#define NO_FILE (-1)

static struct {
    int descriptor; /* NO_FILE: no recording */
    const char *path;
    uint32_t sample_rate_hz;
    uint64_t frames;
    bool failed; /* a write failed: nothing more is appended */
} dump = {.descriptor = NO_FILE};

static uint8_t *put_tag(uint8_t *at, const char *tag)
{
    memcpy(at, tag, TAG_BYTES);
    return at + TAG_BYTES;
}

static uint8_t *put_u16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    return at + sizeof value;
}

static uint8_t *put_u32(uint8_t *at, uint32_t value)
{
    at = put_u16(at, (uint16_t)value);
    return put_u16(at, (uint16_t)(value >> 16));
}

void audio_dump_header(uint8_t out[AUDIO_DUMP_HEADER_BYTES], uint32_t sample_rate_hz, uint64_t frames)
{
    const uint64_t described = frames < AUDIO_DUMP_MAX_FRAMES ? frames : AUDIO_DUMP_MAX_FRAMES;
    const uint32_t data_bytes = (uint32_t)(described * AUDIO_DUMP_BYTES_PER_FRAME);
    uint8_t *at = out;
    at = put_tag(at, TAG_RIFF);
    at = put_u32(at, AUDIO_DUMP_HEADER_BYTES - RIFF_CHUNK_PREFIX_BYTES + data_bytes);
    at = put_tag(at, TAG_WAVE);
    at = put_tag(at, TAG_FMT);
    at = put_u32(at, FMT_CHUNK_BYTES);
    at = put_u16(at, WAVE_FORMAT_PCM);
    at = put_u16(at, CHANNELS);
    at = put_u32(at, sample_rate_hz);
    at = put_u32(at, sample_rate_hz * AUDIO_DUMP_BYTES_PER_FRAME); /* bytes a second */
    at = put_u16(at, AUDIO_DUMP_BYTES_PER_FRAME);                  /* the block: one frame */
    at = put_u16(at, BITS_PER_SAMPLE);
    at = put_tag(at, TAG_DATA);
    (void)put_u32(at, data_bytes);
}

static void forget(void)
{
    dump = (__typeof__(dump)){.descriptor = NO_FILE};
}

int audio_dump_start(const char *path, uint32_t sample_rate_hz)
{
    if (path == NULL || sample_rate_hz == 0 || sample_rate_hz > AUDIO_DUMP_MAX_SAMPLE_RATE_HZ) {
        (void)fprintf(stderr, MODULE ": audio_dump_start: no path, or a rate of %u Hz outside 1..%u\n", sample_rate_hz,
                      AUDIO_DUMP_MAX_SAMPLE_RATE_HZ);
        return -1;
    }
    if (dump.descriptor != NO_FILE) {
        (void)fprintf(stderr, MODULE ": audio_dump_start(%s): %s is already being recorded\n", path, dump.path);
        return -1;
    }
    const int descriptor = host_file_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0) {
        (void)fprintf(stderr, MODULE ": cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }
    uint8_t header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(header, sample_rate_hz, 0);
    if (host_file_write_all(descriptor, header, sizeof header) != 0) {
        (void)fprintf(stderr, MODULE ": cannot write to %s: %s\n", path, strerror(errno));
        (void)close(descriptor); /* already failing */
        return -1;
    }
    dump.descriptor = descriptor;
    dump.path = path;
    dump.sample_rate_hz = sample_rate_hz;
    return 0;
}

int audio_dump_append(const int16_t *stereo, size_t frames)
{
    if (dump.descriptor == NO_FILE || dump.failed || frames == 0)
        return 0;
    if (stereo == NULL) {
        (void)fprintf(stderr, MODULE ": audio_dump_append: no samples\n");
        return -1;
    }
    uint8_t bytes[FRAMES_PER_WRITE * AUDIO_DUMP_BYTES_PER_FRAME];
    for (size_t done = 0; done < frames;) {
        const size_t chunk = frames - done < FRAMES_PER_WRITE ? frames - done : FRAMES_PER_WRITE;
        const int16_t *samples = &stereo[done * CHANNELS];
        for (size_t i = 0; i < chunk * CHANNELS; i++)
            (void)put_u16(&bytes[i * sizeof(int16_t)], (uint16_t)samples[i]);
        if (host_file_write_all(dump.descriptor, bytes, chunk * AUDIO_DUMP_BYTES_PER_FRAME) != 0) {
            (void)fprintf(stderr, MODULE ": cannot write to %s: %s; the recording stops here\n", dump.path,
                          strerror(errno));
            dump.failed = true;
            return -1;
        }
        dump.frames += chunk;
        done += chunk;
    }
    return 0;
}

int audio_dump_finish(void)
{
    if (dump.descriptor == NO_FILE)
        return 0;
    int status = 0;
    uint8_t header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(header, dump.sample_rate_hz, dump.frames);
    if (lseek(dump.descriptor, 0, SEEK_SET) != 0 || host_file_write_all(dump.descriptor, header, sizeof header) != 0) {
        (void)fprintf(stderr, MODULE ": cannot write the header of %s: %s\n", dump.path, strerror(errno));
        status = -1;
    }
    if (close(dump.descriptor) != 0 && status == 0) {
        (void)fprintf(stderr, MODULE ": closing %s failed: %s\n", dump.path, strerror(errno));
        status = -1;
    }
    if (dump.frames > AUDIO_DUMP_MAX_FRAMES)
        (void)fprintf(stderr,
                      MODULE ": %s is longer than a WAV header can describe; it describes the first %u frames\n",
                      dump.path, (unsigned)AUDIO_DUMP_MAX_FRAMES);
    forget();
    return status;
}

void audio_dump_shutdown(void)
{
    (void)audio_dump_finish(); /* reported */
}
