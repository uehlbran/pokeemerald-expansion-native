/*
 * bios_stream.c: the data header and the read/write variants every BIOS
 * decompressor shares.
 *
 * GBATEK "BIOS Decompression Functions"
 * (https://problemkaputt.de/gbatek.htm#biosdecompressionfunctions): each
 * stream starts with a 32-bit header, the type in bits 4-7 and the
 * decompressed size in bits 8-31; "ReadNormal" functions read the source
 * from memory; "Write8bitUnits" ("Wram") functions store bytes, and
 * "Write16bitUnits" ("Vram") functions store 16-bit units, because VRAM
 * ignores 8-bit writes. A 16-bit writer holds each even byte until the odd
 * one arrives; GBATEK's warning that the "Vram" LZ77 "works only with
 * disp=001h..FFFh" is that held byte not being in memory yet, and an odd
 * size leaves the held byte unwritten. Both follow here from writing the
 * same way, not from special cases.
 *
 * The source is read one byte at a time against a bound: the caller's
 * length for the bios_*_bounded functions, none for the game-facing ones,
 * which trust the stream as the hardware does (the header gives no input
 * length). Streams are little-endian, as the GBA is.
 */
#include "bios_stream.h"

#include <stdio.h>

#include "bios_internal.h"

/* Header bit fields (GBATEK). */
#define HEADER_UNIT_MASK 0x0Fu
#define HEADER_TYPE_SHIFT 4u
#define HEADER_TYPE_MASK 0x0Fu
#define HEADER_SIZE_SHIFT 8u
/* A "Vram" writer's unit: 16 bits, two bytes. */
#define WRITE16_UNIT_SIZE 2u

int bios_stream_open(struct bios_source *source, const char *function, const void *src, size_t src_size,
                     unsigned type, struct bios_stream_header *header)
{
    if (src_size < BIOS_STREAM_HEADER_SIZE) {
        return BIOS_ERR_TRUNCATED;
    }
    if (!bios_range_ok(function, "header", src, BIOS_STREAM_HEADER_SIZE, TRANSFER_READ)) {
        return BIOS_ERR_RANGE;
    }
    source->bytes = src;
    source->size = src_size;
    source->position = BIOS_STREAM_HEADER_SIZE;
    uint32_t word = bios_load32(source->bytes);
    header->unit = word & HEADER_UNIT_MASK;
    header->size = word >> HEADER_SIZE_SHIFT;
    return ((word >> HEADER_TYPE_SHIFT) & HEADER_TYPE_MASK) == type ? BIOS_OK : BIOS_ERR_HEADER;
}

bool bios_source_byte(struct bios_source *source, uint8_t *byte)
{
    if (source->position >= source->size) {
        return false;
    }
    *byte = source->bytes[source->position++];
    return true;
}

int bios_sink_open(struct bios_sink *sink, const char *function, void *dest, size_t size,
                   enum bios_write_width width)
{
    if (size != 0 && !bios_range_ok(function, "destination", dest, size, TRANSFER_WRITE)) {
        return BIOS_ERR_RANGE;
    }
    sink->bytes = dest;
    sink->size = size;
    sink->position = 0;
    sink->width = width;
    sink->held = 0;
    return BIOS_OK;
}

bool bios_sink_full(const struct bios_sink *sink)
{
    return sink->position >= sink->size;
}

void bios_sink_put(struct bios_sink *sink, uint8_t byte)
{
    if (sink->width == BIOS_WRITE_8BIT) {
        sink->bytes[sink->position] = byte;
    } else if (sink->position % WRITE16_UNIT_SIZE == 0) {
        sink->held = byte;
    } else {
        const uint8_t unit[WRITE16_UNIT_SIZE] = { sink->held, byte };
        memcpy(sink->bytes + sink->position - 1, unit, sizeof unit);
    }
    sink->position++;
}

uint8_t bios_sink_read_back(const struct bios_sink *sink, size_t distance)
{
    return sink->bytes[sink->position - distance];
}

void bios_stream_report(const char *function, int error)
{
    switch (error) {
    case BIOS_ERR_HEADER:
        fprintf(stderr, "bios: %s: the header names another format; nothing written\n", function);
        break;
    case BIOS_ERR_STREAM:
        fprintf(stderr, "bios: %s: malformed stream; output stopped\n", function);
        break;
    case BIOS_ERR_TRUNCATED:
        fprintf(stderr, "bios: %s: stream ended early; output stopped\n", function);
        break;
    default:
        break;
    }
}
