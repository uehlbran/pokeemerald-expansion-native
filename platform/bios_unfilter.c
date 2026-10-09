/*
 * bios_unfilter.c: BitUnPack (SWI 10h) and the difference unfilters,
 * Diff8bitUnFilterWram/Vram (SWI 16h/17h) and Diff16bitUnFilter (SWI 18h).
 *
 * Stands in for the BIOS functions GBATEK lists with the decompressors but
 * which change the representation rather than the size: BitUnPack widens
 * units ("convert a 1bit monochrome font into 4bit or 8bit GBA tiles", as
 * expansion's src/assertf.c does), and the unfilters turn a stream of
 * differences back into values ("These aren't actually real decompression
 * functions"). GBATEK "BIOS Decompression Functions",
 * https://problemkaputt.de/gbatek.htm#biosdecompressionfunctions.
 *
 * BitUnPack: GBATEK gives the information block and that the offset is added
 * to nonzero units (and to zero units with the flag), and that "Data is
 * written in 32bit units". Not stated, chosen to match how the GBA lays out
 * pixels (the first pixel in the low bits; GBATEK "LCD VRAM Character
 * Data"): units are taken from the low bits of each source byte first and
 * packed into each word from the low bits up. The unit plus offset is cut to
 * the destination width; a trailing partial word is not written.
 *
 * Unfilters: unit n of the output is the sum of input units 0..n, wrapping at
 * the unit width (GBATEK's example: "10 +1 +1 ..." gives "10 11 12 ...").
 * Their input length is known from the header, so the whole input is
 * validated, unlike the decompressors'.
 */
#include <stdio.h>

#include "bios.h"
#include "bios_internal.h"
#include "bios_stream.h"

/* GBATEK BitUnPack information: u16 source length, u8 source width, u8
 * destination width, u32 offset (bits 0-30) and zero-data flag (bit 31). */
#define UNPACK_LENGTH 0u
#define UNPACK_SOURCE_WIDTH 2u
#define UNPACK_DEST_WIDTH 3u
#define UNPACK_OFFSET 4u
#define UNPACK_INFO_SIZE 8u
#define UNPACK_OFFSET_MASK UINT32_C(0x7FFFFFFF)
#define UNPACK_ZERO_DATA_FLAG UINT32_C(0x80000000)
#define UNPACK_WORD_BITS 32u
#define UNPACK_WORD_SIZE 4u

/* GBATEK Diff header bits 0-3: "must be 1 for Diff8bit, 2 for Diff16bit". */
#define DIFF8_UNIT 1u
#define DIFF16_UNIT 2u

/* GBATEK: source widths "only 1,2,4,8 supported", destination widths "only
 * 1,2,4,8,16,32". */
static bool supported_source_width(unsigned bits)
{
    return bits == 1 || bits == 2 || bits == 4 || bits == 8;
}

static bool supported_dest_width(unsigned bits)
{
    return supported_source_width(bits) || bits == 16 || bits == 32;
}

/* The low `bits` bits set; bits 1..32. */
static uint32_t low_mask(unsigned bits)
{
    return bits >= UNPACK_WORD_BITS ? UINT32_MAX : (UINT32_C(1) << bits) - 1u;
}

void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *info)
{
    if (!bios_range_ok("BitUnPack", "unpack information", info, UNPACK_INFO_SIZE, TRANSFER_READ)) {
        return;
    }
    const uint8_t *args = (const uint8_t *)info;
    size_t length = bios_load16(args + UNPACK_LENGTH);
    unsigned from = args[UNPACK_SOURCE_WIDTH];
    unsigned to = args[UNPACK_DEST_WIDTH];
    uint32_t offset_word = bios_load32(args + UNPACK_OFFSET);
    if (!supported_source_width(from) || !supported_dest_width(to)) {
        (void)fprintf(stderr, "bios: BitUnPack: unsupported widths %u to %u bits; nothing written\n", from, to);
        return;
    }
    /* length <= FFFFh, so the bit count fits easily. */
    size_t words = length * (BIOS_BITS_PER_BYTE / from) * to / UNPACK_WORD_BITS;
    if (words == 0 || !bios_range_ok("BitUnPack", "source", src, length, TRANSFER_READ)
        || !bios_range_ok("BitUnPack", "destination", dest, words * UNPACK_WORD_SIZE, TRANSFER_WRITE)) {
        return;
    }
    const uint8_t *in = src;
    uint8_t *out = dest;
    uint32_t offset = offset_word & UNPACK_OFFSET_MASK;
    bool offset_zeros = (offset_word & UNPACK_ZERO_DATA_FLAG) != 0;
    uint32_t output = 0;
    unsigned filled = 0;
    size_t written = 0;
    for (size_t i = 0; i < length && written < words; i++) {
        for (unsigned bit = 0; bit < BIOS_BITS_PER_BYTE && written < words; bit += from) {
            uint32_t unit = ((uint32_t)in[i] >> bit) & low_mask(from);
            if (unit != 0 || offset_zeros) {
                unit += offset;
            }
            output |= (unit & low_mask(to)) << filled;
            filled += to;
            if (filled == UNPACK_WORD_BITS) {
                bios_store32(out + written * UNPACK_WORD_SIZE, output);
                written++;
                output = 0;
                filled = 0;
            }
        }
    }
}

/* Reads a Diff stream's header, validates its input (header and units)
 * and starts the output; BIOS_OK or an error, printed. */
static int diff_open(const char *function, const void *src, unsigned unit, void *dest, enum bios_write_width width,
                     struct bios_stream_header *header, struct bios_sink *out)
{
    struct bios_source in;
    int error = bios_stream_open(&in, function, src, BIOS_STREAM_UNBOUNDED, BIOS_STREAM_DIFF, header);
    if (error == BIOS_OK && header->unit != unit) {
        error = BIOS_ERR_HEADER;
    }
    if (error == BIOS_OK && header->size != 0
        && !bios_range_ok(function, "source", src, BIOS_STREAM_HEADER_SIZE + (size_t)header->size, TRANSFER_READ)) {
        error = BIOS_ERR_RANGE;
    }
    if (error == BIOS_OK) {
        error = bios_sink_open(out, function, dest, header->size, width);
    }
    bios_stream_report(function, error);
    return error;
}

static void diff8_unfilter(const char *function, const void *src, void *dest, enum bios_write_width width)
{
    struct bios_stream_header header;
    struct bios_sink out;
    if (diff_open(function, src, DIFF8_UNIT, dest, width, &header, &out) != BIOS_OK) {
        return;
    }
    const uint8_t *units = (const uint8_t *)src + BIOS_STREAM_HEADER_SIZE;
    uint8_t sum = 0;
    for (size_t i = 0; !bios_sink_full(&out); i++) {
        sum = (uint8_t)(sum + units[i]);
        bios_sink_put(&out, sum);
    }
}

void Diff8bitUnFilterWram(const void *src, void *dest)
{
    diff8_unfilter("Diff8bitUnFilterWram", src, dest, BIOS_WRITE_8BIT);
}

void Diff8bitUnFilterVram(const void *src, void *dest)
{
    diff8_unfilter("Diff8bitUnFilterVram", src, dest, BIOS_WRITE_16BIT);
}

/* 16-bit units, written as 16-bit units: an odd size leaves its last byte
 * unwritten, as the "Vram" writers do. */
void Diff16bitUnFilter(const void *src, void *dest)
{
    struct bios_stream_header header;
    struct bios_sink out;
    if (diff_open("Diff16bitUnFilter", src, DIFF16_UNIT, dest, BIOS_WRITE_16BIT, &header, &out) != BIOS_OK) {
        return;
    }
    const uint8_t *units = (const uint8_t *)src + BIOS_STREAM_HEADER_SIZE;
    uint16_t sum = 0;
    for (size_t i = 0; i < header.size / DIFF16_UNIT; i++) {
        sum = (uint16_t)(sum + bios_load16(units + i * DIFF16_UNIT));
        bios_sink_put(&out, (uint8_t)(sum & UINT8_MAX));
        bios_sink_put(&out, (uint8_t)(sum >> BIOS_BITS_PER_BYTE));
    }
}
