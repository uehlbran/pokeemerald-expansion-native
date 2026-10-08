/*
 * bios_stream.h: the parts of the BIOS decompressors they all share (see
 * bios_stream.c). Internal to bios_decompress.c and bios_unfilter.c; the
 * public interface is bios.h.
 */
#ifndef BIOS_STREAM_H
#define BIOS_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bios.h"

/* GBATEK "BIOS Decompression Functions", data header bits 4-7. */
#define BIOS_STREAM_LZ77 1u
#define BIOS_STREAM_HUFFMAN 2u
#define BIOS_STREAM_RUN_LENGTH 3u
#define BIOS_STREAM_DIFF 8u
#define BIOS_STREAM_HEADER_SIZE 4u
/* The source bound of the game-facing functions: trust the stream. */
#define BIOS_STREAM_UNBOUNDED SIZE_MAX

/* The 32-bit data header. */
struct bios_stream_header {
    unsigned unit;  /* bits 0-3: reserved, or the unit size (Huffman bits, Diff bytes) */
    uint32_t size;  /* bits 8-31: bytes after decompression */
};

/* The compressed bytes, read front to back. */
struct bios_source {
    const uint8_t *bytes;
    size_t size;     /* how many may be read; BIOS_STREAM_UNBOUNDED to trust the stream */
    size_t position; /* the next byte */
};

/* Reads the header of the stream at src: BIOS_OK, BIOS_ERR_RANGE (the
 * header's 4 bytes are not readable; printed), BIOS_ERR_TRUNCATED (src_size
 * < 4) or BIOS_ERR_HEADER (bits 4-7 are not type). Leaves source at the
 * first byte after the header. */
int bios_stream_open(struct bios_source *source, const char *function, const void *src, size_t src_size,
                     unsigned type, struct bios_stream_header *header);

/* The next source byte; false (and no byte) past the source's bound. */
bool bios_source_byte(struct bios_source *source, uint8_t *byte);

/* The output of a byte-oriented decoder, written as the "Wram" (8-bit) or
 * "Vram" (16-bit) variant writes it. */
struct bios_sink {
    uint8_t *bytes;
    size_t size;     /* bytes to produce */
    size_t position; /* bytes produced so far */
    enum bios_write_width width;
    uint8_t held;    /* 16-bit: the even byte, until its partner arrives */
};

/* Starts a sink of size bytes at dest after validating [dest, dest + size)
 * for writing: BIOS_OK or BIOS_ERR_RANGE (printed). Size 0 is BIOS_OK and
 * touches nothing. */
int bios_sink_open(struct bios_sink *sink, const char *function, void *dest, size_t size,
                   enum bios_write_width width);

/* True once size bytes have been produced. */
bool bios_sink_full(const struct bios_sink *sink);

/* Produces the next byte (call only while not full). 8-bit: stored at once.
 * 16-bit: an even byte is held; an odd byte is stored with it as one
 * 16-bit unit at dest + position - 1. So an odd size leaves its last byte
 * unwritten. */
void bios_sink_put(struct bios_sink *sink, uint8_t byte);

/* The destination byte distance bytes behind the next one, read from memory
 * (1 <= distance <= position). For a 16-bit sink at an odd position,
 * distance 1 is the held byte, which memory does not have yet: the old byte
 * is read, as on hardware. */
uint8_t bios_sink_read_back(const struct bios_sink *sink, size_t distance);

/* For the game-facing functions: prints a failed call's error. Range errors
 * were printed with their range when refused, so they are not repeated. */
void bios_stream_report(const char *function, int error);

#endif /* BIOS_STREAM_H */
