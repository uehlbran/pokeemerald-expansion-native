/*
 * bios_decompress.c: LZ77UnComp (SWI 11h/12h), HuffUnComp (SWI 13h) and
 * RLUnComp (SWI 14h/15h).
 *
 * Stands in for the BIOS's three decompressors (GBATEK "BIOS Decompression
 * Functions", https://problemkaputt.de/gbatek.htm#biosdecompressionfunctions),
 * which expansion uses for its .lz graphics and tilemaps. The header, the
 * source bound and the 8- and 16-bit writers are in bios_stream.c; the
 * formats are here, each as GBATEK describes it:
 *
 * LZ77: flag bytes, each followed by eight blocks, the flags MSB first; a
 * 0 block is one literal byte, a 1 block two bytes, length-3 in bits 4-7 of
 * the first, disp in bits 0-3 of the first and all of the second, copying
 * length bytes from dest-disp-1, one at a time so a copy may overlap its
 * own output.
 *
 * Run-length: a flag byte, bit 7 set for a run of (bits 0-6)+3 copies of
 * the next byte, clear for (bits 0-6)+1 literal bytes.
 *
 * Huffman: a tree-size byte T and a table of 8-bit nodes from the byte
 * after it (the root) up to offset 4 + (T+1)*2, where the bitstream starts
 * as 32-bit words read from bit 31 down. A node's bits 0-5 give its
 * children's position, (address AND NOT 1) + offset*2 + 2 for child 0 and
 * one more for child 1; bit 7 (child 0) and bit 6 (child 1) say the child is
 * data. Decoded units fill 32-bit words from the low bits up.
 *
 * Every decoder stops after exactly the header's size (Huffman: its whole
 * words), even inside a block, and validates the destination for that size
 * first. Where the hardware would read memory outside the stream or the
 * output, these return BIOS_ERR_STREAM instead.
 */
#include "bios.h"
#include "bios_internal.h"
#include "bios_stream.h"

/* LZ77 (GBATEK). */
#define LZ77_BLOCKS_PER_FLAG 8u
#define LZ77_FLAG_COMPRESSED 0x80u
#define LZ77_LENGTH_SHIFT 4u
#define LZ77_MIN_LENGTH 3u
#define LZ77_DISP_HIGH_MASK 0x0Fu

/* Run-length (GBATEK). */
#define RL_FLAG_COMPRESSED 0x80u
#define RL_LENGTH_MASK 0x7Fu
#define RL_MIN_RUN 3u
#define RL_MIN_LITERALS 1u

/* Huffman (GBATEK). */
#define HUFF_TREE_SIZE_OFFSET 4u /* the tree-size byte follows the header */
#define HUFF_ROOT_OFFSET 5u
#define HUFF_NODE_OFFSET_MASK 0x3Fu
#define HUFF_CHILD0_IS_DATA 0x80u
#define HUFF_CHILD1_IS_DATA 0x40u
#define HUFF_WORD_BITS 32u
#define HUFF_WORD_SIZE 4u
#define HUFF_FIRST_BIT_SHIFT 31u

/* ---- LZ77 ---------------------------------------------------------------- */

static int lz77_copy(struct bios_source *in, struct bios_sink *out)
{
    uint8_t first = 0;
    uint8_t second = 0;
    if (!bios_source_byte(in, &first) || !bios_source_byte(in, &second)) {
        return BIOS_ERR_TRUNCATED;
    }
    size_t length = ((unsigned)first >> LZ77_LENGTH_SHIFT) + LZ77_MIN_LENGTH;
    size_t distance = ((((unsigned)first & LZ77_DISP_HIGH_MASK) << BIOS_BITS_PER_BYTE) | second) + 1u;
    if (distance > out->position) {
        return BIOS_ERR_STREAM; /* would read before the start of the output */
    }
    for (size_t i = 0; i < length && !bios_sink_full(out); i++) {
        bios_sink_put(out, bios_sink_read_back(out, distance));
    }
    return BIOS_OK;
}

static int lz77_uncomp(const char *function, const void *src, size_t src_size, void *dest,
                       enum bios_write_width width)
{
    struct bios_source in;
    struct bios_stream_header header;
    struct bios_sink out;
    int error = bios_stream_open(&in, function, src, src_size, BIOS_STREAM_LZ77, &header);
    if (error == BIOS_OK) {
        error = bios_sink_open(&out, function, dest, header.size, width);
    }
    while (error == BIOS_OK && !bios_sink_full(&out)) {
        uint8_t flags = 0;
        if (!bios_source_byte(&in, &flags)) {
            return BIOS_ERR_TRUNCATED;
        }
        for (unsigned block = 0; block < LZ77_BLOCKS_PER_FLAG && error == BIOS_OK && !bios_sink_full(&out);
             block++) {
            uint8_t literal = 0;
            if (((unsigned)flags << block) & LZ77_FLAG_COMPRESSED) {
                error = lz77_copy(&in, &out);
            } else if (bios_source_byte(&in, &literal)) {
                bios_sink_put(&out, literal);
            } else {
                error = BIOS_ERR_TRUNCATED;
            }
        }
    }
    return error;
}

int bios_lz77_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width)
{
    return lz77_uncomp("bios_lz77_uncomp_bounded", src, src_size, dest, width);
}

void LZ77UnCompWram(const uint32_t *src, void *dest)
{
    bios_stream_report("LZ77UnCompWram",
                       lz77_uncomp("LZ77UnCompWram", src, BIOS_STREAM_UNBOUNDED, dest, BIOS_WRITE_8BIT));
}

void LZ77UnCompVram(const uint32_t *src, void *dest)
{
    bios_stream_report("LZ77UnCompVram",
                       lz77_uncomp("LZ77UnCompVram", src, BIOS_STREAM_UNBOUNDED, dest, BIOS_WRITE_16BIT));
}

/* ---- Run-length ---------------------------------------------------------- */

static int rl_uncomp(const char *function, const void *src, size_t src_size, void *dest,
                     enum bios_write_width width)
{
    struct bios_source in;
    struct bios_stream_header header;
    struct bios_sink out;
    int error = bios_stream_open(&in, function, src, src_size, BIOS_STREAM_RUN_LENGTH, &header);
    if (error == BIOS_OK) {
        error = bios_sink_open(&out, function, dest, header.size, width);
    }
    while (error == BIOS_OK && !bios_sink_full(&out)) {
        uint8_t flag = 0;
        uint8_t value = 0;
        if (!bios_source_byte(&in, &flag)) {
            return BIOS_ERR_TRUNCATED;
        }
        bool run = (flag & RL_FLAG_COMPRESSED) != 0;
        size_t length = (flag & RL_LENGTH_MASK) + (run ? RL_MIN_RUN : RL_MIN_LITERALS);
        if (run && !bios_source_byte(&in, &value)) {
            return BIOS_ERR_TRUNCATED;
        }
        for (size_t i = 0; i < length && !bios_sink_full(&out); i++) {
            if (!run && !bios_source_byte(&in, &value)) {
                return BIOS_ERR_TRUNCATED;
            }
            bios_sink_put(&out, value);
        }
    }
    return error;
}

int bios_rl_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width)
{
    return rl_uncomp("bios_rl_uncomp_bounded", src, src_size, dest, width);
}

void RLUnCompWram(const uint32_t *src, void *dest)
{
    bios_stream_report("RLUnCompWram",
                       rl_uncomp("RLUnCompWram", src, BIOS_STREAM_UNBOUNDED, dest, BIOS_WRITE_8BIT));
}

void RLUnCompVram(const uint32_t *src, void *dest)
{
    bios_stream_report("RLUnCompVram",
                       rl_uncomp("RLUnCompVram", src, BIOS_STREAM_UNBOUNDED, dest, BIOS_WRITE_16BIT));
}

/* ---- Huffman ------------------------------------------------------------- */

struct huffman_tree {
    const uint8_t *bytes; /* the stream; nodes are addressed from its start */
    size_t end;           /* one past the table: where the bitstream starts */
};

static bool supported_unit(unsigned bits)
{
    return bits == 1 || bits == 2 || bits == 4 || bits == 8;
}

/* The next bit of the bitstream, bit 31 of each word first. */
static int next_bit(struct bios_source *in, uint32_t *word, unsigned *bits_left, unsigned *bit)
{
    if (*bits_left == 0) {
        *word = 0;
        for (unsigned i = 0; i < HUFF_WORD_SIZE; i++) {
            uint8_t byte = 0;
            if (!bios_source_byte(in, &byte)) {
                return BIOS_ERR_TRUNCATED;
            }
            *word |= (uint32_t)byte << (i * BIOS_BITS_PER_BYTE);
        }
        *bits_left = HUFF_WORD_BITS;
    }
    *bit = (unsigned)(*word >> HUFF_FIRST_BIT_SHIFT);
    *word <<= 1;
    (*bits_left)--;
    return BIOS_OK;
}

/* Walks from the root to a data node; *unit is its data byte. */
static int decode_unit(const struct huffman_tree *tree, struct bios_source *in, uint32_t *word,
                       unsigned *bits_left, uint8_t *unit)
{
    size_t position = HUFF_ROOT_OFFSET;
    for (;;) {
        unsigned bit = 0;
        int error = next_bit(in, word, bits_left, &bit);
        if (error != BIOS_OK) {
            return error;
        }
        uint8_t node = tree->bytes[position];
        size_t child = (position & ~(size_t)1) + (size_t)(node & HUFF_NODE_OFFSET_MASK) * 2u + 2u + bit;
        if (child >= tree->end) {
            return BIOS_ERR_STREAM;
        }
        if (node & (bit != 0 ? HUFF_CHILD1_IS_DATA : HUFF_CHILD0_IS_DATA)) {
            *unit = tree->bytes[child];
            return BIOS_OK;
        }
        position = child;
    }
}

static int huff_uncomp(const char *function, const void *src, size_t src_size, void *dest)
{
    struct bios_source in;
    struct bios_stream_header header;
    struct bios_sink out;
    uint8_t tree_size = 0;
    int error = bios_stream_open(&in, function, src, src_size, BIOS_STREAM_HUFFMAN, &header);
    if (error != BIOS_OK) {
        return error;
    }
    if (!supported_unit(header.unit)) {
        return BIOS_ERR_HEADER;
    }
    if (!bios_source_byte(&in, &tree_size)) {
        return BIOS_ERR_TRUNCATED;
    }
    struct huffman_tree tree = { in.bytes, HUFF_TREE_SIZE_OFFSET + ((size_t)tree_size + 1u) * 2u };
    if (tree.end > in.size) {
        return BIOS_ERR_TRUNCATED;
    }
    /* GBATEK: "Data is written in units of 32bits": the size is rounded up. */
    size_t words = ((size_t)header.size + HUFF_WORD_SIZE - 1u) / HUFF_WORD_SIZE;
    error = bios_sink_open(&out, function, dest, words * HUFF_WORD_SIZE, BIOS_WRITE_8BIT);
    in.position = tree.end;
    uint32_t bitstream = 0;
    unsigned bits_left = 0;
    uint32_t mask = (UINT32_C(1) << header.unit) - 1u;
    for (size_t w = 0; w < words && error == BIOS_OK; w++) {
        uint32_t output = 0;
        for (unsigned shift = 0; shift < HUFF_WORD_BITS && error == BIOS_OK; shift += header.unit) {
            uint8_t unit = 0;
            error = decode_unit(&tree, &in, &bitstream, &bits_left, &unit);
            output |= (unit & mask) << shift;
        }
        if (error == BIOS_OK) {
            bios_store32(out.bytes + w * HUFF_WORD_SIZE, output);
        }
    }
    return error;
}

int bios_huff_uncomp_bounded(const void *src, size_t src_size, void *dest)
{
    return huff_uncomp("bios_huff_uncomp_bounded", src, src_size, dest);
}

void HuffUnComp(const uint32_t *src, void *dest)
{
    bios_stream_report("HuffUnComp", huff_uncomp("HuffUnComp", src, BIOS_STREAM_UNBOUNDED, dest));
}
