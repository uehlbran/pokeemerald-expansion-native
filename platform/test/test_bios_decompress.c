// deps: bios_stream.c bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of LZ77UnComp (SWI 11h/12h), RLUnComp (SWI 14h/15h) and HuffUnComp
 * (SWI 13h) on hand-built streams, from GBATEK "BIOS Decompression
 * Functions": the header word (type in bits 4-7, size in bits 8-31), LZ77's
 * flag byte (MSB first) and blocks (N+3 bytes from dest-disp-1), the "Vram"
 * variants' 16-bit writes and their disp=0 caveat, run-length flags (bit 7,
 * N-1 or N-3), and the Huffman tree (offsets from (addr AND NOT 1)+2, end
 * flags in bits 7 and 6, bitstream words read from bit 31; the "Huff"
 * example). Write granularity is observed through what a 16-bit writer must
 * do differently: hold an odd last byte, and read a destination byte it has
 * not written yet.
 */
#include <stdint.h>
#include <string.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define STALE 0xEE
#define LZ77_MAX_DISP 0xFFF /* 12 bits */
#define LZ77_MAX_LENGTH 18  /* 4 bits + 3 */
#define RL_MAX_RUN 130      /* 7 bits + 3 */
#define RL_MAX_LITERALS 128 /* 7 bits + 1 */
#define BUILDER_CAPACITY 8192

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

/* ---- A stream builder: header, then LZ77 blocks grouped under flag bytes. */

struct builder {
    _Alignas(4) uint8_t bytes[BUILDER_CAPACITY];
    size_t size;
    size_t flag_at;    /* where the current flag byte is */
    unsigned in_group; /* blocks under it so far, 0-8 */
};

static void put(struct builder *b, uint8_t byte)
{
    if (b->size < BUILDER_CAPACITY)
        b->bytes[b->size++] = byte;
}

static void header(struct builder *b, unsigned type, uint32_t size)
{
    b->size = 0;
    b->in_group = 8;
    put(b, (uint8_t)(type << 4));
    put(b, (uint8_t)size);
    put(b, (uint8_t)(size >> 8));
    put(b, (uint8_t)(size >> 16));
}

static void lz_block(struct builder *b, int compressed)
{
    if (b->in_group == 8) {
        b->flag_at = b->size;
        put(b, 0);
        b->in_group = 0;
    }
    if (compressed)
        b->bytes[b->flag_at] = (uint8_t)(b->bytes[b->flag_at] | (0x80u >> b->in_group));
    b->in_group++;
}

static void lz_literal(struct builder *b, uint8_t byte)
{
    lz_block(b, 0);
    put(b, byte);
}

/* Copy length bytes from dest - disp - 1. */
static void lz_ref(struct builder *b, unsigned length, unsigned disp)
{
    lz_block(b, 1);
    put(b, (uint8_t)(((length - 3) << 4) | (disp >> 8)));
    put(b, (uint8_t)disp);
}

/* ---- LZ77 ---------------------------------------------------------------- */

/* "abcabcabcd": three literals, a 6-byte copy from 3 back (overlapping its
 * own output), a literal. */
static const _Alignas(4) uint8_t lz_abc[] = {
    0x10, 0x0A, 0x00, 0x00,       /* type 1, size 10 */
    0x10,                         /* flags: L L L C L */
    'a',  'b',  'c',  0x30, 0x02, /* length 3+3, disp 2: from dest-3 */
    'd',  0x00,
};

PTEST(lz77_literals_and_overlapping_reference)
{
    setup();
    uint8_t wram[12], vram[12];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, sizeof lz_abc, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, sizeof lz_abc, vram, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(wram, "abcabcabcd", 10);
    PT_ASSERT_MEM_EQ(vram, "abcabcabcd", 10);
    PT_ASSERT_EQ_U32(wram[10], STALE);
    PT_ASSERT_EQ_U32(vram[10], STALE);
    teardown();
}

PTEST(lz77_game_facing_functions)
{
    setup();
    _Alignas(4) uint8_t wram[12], vram[12];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    uint32_t words[sizeof lz_abc / 4];
    memcpy(words, lz_abc, sizeof words);
    LZ77UnCompWram(words, wram);
    LZ77UnCompVram(words, vram);
    PT_ASSERT_MEM_EQ(wram, "abcabcabcd", 10);
    PT_ASSERT_MEM_EQ(vram, "abcabcabcd", 10);
    PT_ASSERT_EQ_U32(wram[10], STALE);
    teardown();
}

/* 4096 literals, then the farthest (disp FFFh, 4096 back) and longest (18)
 * reference: it copies bytes 0-17. Both variants agree. */
PTEST(lz77_maximum_distance_and_length)
{
    setup();
    static struct builder b;
    static uint8_t wram[4200], vram[4200];
    const uint32_t size = 4096 + LZ77_MAX_LENGTH;
    header(&b, 1, size);
    for (unsigned i = 0; i < 4096; i++)
        lz_literal(&b, (uint8_t)(i * 7 + 3));
    lz_ref(&b, LZ77_MAX_LENGTH, LZ77_MAX_DISP);
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(b.bytes, b.size, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(b.bytes, b.size, vram, BIOS_WRITE_16BIT), BIOS_OK);
    for (unsigned i = 0; i < 4096; i++)
        PT_ASSERT_EQ_U32(wram[i], (uint8_t)(i * 7 + 3));
    PT_ASSERT_MEM_EQ(wram + 4096, wram, LZ77_MAX_LENGTH);
    PT_ASSERT_MEM_EQ(vram, wram, size);
    PT_ASSERT_EQ_U32(wram[size], STALE);
    PT_ASSERT_EQ_U32(vram[size], STALE);
    teardown();
}

/* GBATEK: the Vram function "works only with disp=001h..FFFh, but not with
 * disp=000h": copying from dest-1 at an odd position reads a byte the 16-bit
 * writer still holds, so it reads the destination's old content. */
PTEST(lz77_vram_disp_zero_reads_unwritten_byte)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x10, 0x06, 0x00, 0x00, /* size 6 */
        0x40, 'a',              /* flags: L C */
        0x20, 0x00,             /* length 2+3, disp 0: from dest-1 */
    };
    uint8_t wram[8], vram[8];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, vram, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(wram, "aaaaaa", 6);
    const uint8_t expected_vram[6] = {'a', STALE, STALE, STALE, STALE, STALE};
    PT_ASSERT_MEM_EQ(vram, expected_vram, sizeof expected_vram);
    teardown();
}

/* A 16-bit writer cannot write a single byte: an odd size leaves the last
 * byte unwritten. */
PTEST(lz77_vram_odd_size_leaves_last_byte)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x10, 0x05, 0x00, 0x00, 0x00, 'a', 'b', 'c', 'd', 'e', 0x00, 0x00,
    };
    uint8_t wram[6], vram[6];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, vram, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(wram, "abcde", 5);
    PT_ASSERT_MEM_EQ(vram, "abcd", 4);
    PT_ASSERT_EQ_U32(vram[4], STALE);
    teardown();
}

/* The Vram variant writes pairs from dest, whatever dest's alignment. */
PTEST(lz77_vram_unaligned_destination)
{
    setup();
    uint8_t vram[14];
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, sizeof lz_abc, vram + 1, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(vram + 1, "abcabcabcd", 10);
    PT_ASSERT_EQ_U32(vram[0], STALE);
    PT_ASSERT_EQ_U32(vram[11], STALE);
    teardown();
}

/* Output stops at the declared size, even inside a block. */
PTEST(lz77_stops_at_declared_size_inside_a_block)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x10, 0x04, 0x00, 0x00, /* size 4 */
        0x40, 'a',  0xF0, 0x00, /* length 15+3, disp 0 */
    };
    uint8_t out[8];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, out, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(out, "aaaa", 4);
    PT_ASSERT_EQ_U32(out[4], STALE);
    teardown();
}

PTEST(lz77_size_zero_writes_nothing)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {0x10, 0x00, 0x00, 0x00};
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, NULL, BIOS_WRITE_8BIT), BIOS_OK);
    teardown();
}

/* The declared size runs past the end of VRAM: refused, nothing written. */
PTEST(lz77_declared_size_beyond_destination_region_refused)
{
    setup();
    struct builder b;
    header(&b, 1, 16);
    for (unsigned i = 0; i < 16; i++)
        lz_literal(&b, 'x');
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 8);
    memset(tail, STALE, 8);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(b.bytes, b.size, tail, BIOS_WRITE_8BIT), BIOS_ERR_RANGE);
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(b.bytes, b.size, tail, BIOS_WRITE_16BIT), BIOS_ERR_RANGE);
    uint32_t words[8] = {0}; /* 26 stream bytes */
    PT_ASSERT(b.size <= sizeof words);
    memcpy(words, b.bytes, b.size);
    LZ77UnCompVram(words, tail);
    LZ77UnCompWram(words, tail);
    for (unsigned i = 0; i < 8; i++)
        PT_ASSERT_EQ_U32(tail[i], STALE);
    teardown();
}

PTEST(lz77_truncated_stream)
{
    setup();
    uint8_t out[12];
    /* Cut inside the reference block's two bytes. */
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, 9, out, BIOS_WRITE_8BIT), BIOS_ERR_TRUNCATED);
    /* No flag byte at all. */
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, 4, out, BIOS_WRITE_8BIT), BIOS_ERR_TRUNCATED);
    /* Not even a header. */
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(lz_abc, 3, out, BIOS_WRITE_8BIT), BIOS_ERR_TRUNCATED);
    teardown();
}

PTEST(lz77_wrong_type_refused)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {0x30, 0x02, 0x00, 0x00, 0x00, 'a', 'b', 0x00};
    uint8_t out[2] = {STALE, STALE};
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, out, BIOS_WRITE_8BIT), BIOS_ERR_HEADER);
    PT_ASSERT_EQ_U32(out[0], STALE);
    teardown();
}

/* A reference reaching before the start of the output. */
PTEST(lz77_reference_before_output_refused)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x10, 0x04, 0x00, 0x00, 0x40, 'a', 0x00, 0x01, /* disp 1: from dest-2, at position 1 */
    };
    uint8_t out[4];
    PT_ASSERT_EQ_INT(bios_lz77_uncomp_bounded(stream, sizeof stream, out, BIOS_WRITE_8BIT), BIOS_ERR_STREAM);
    teardown();
}

/* ---- Run-length ----------------------------------------------------------- */

static const _Alignas(4) uint8_t rl_stream[] = {
    0x30, 0x08, 0x00, 0x00, /* type 3, size 8 */
    0x82, 'A',              /* run of 2+3 */
    0x02, 'B',  'C',  'D',  /* 2+1 literals */
    0x00, 0x00,
};

PTEST(rl_runs_and_literals)
{
    setup();
    uint8_t wram[10], vram[10];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(rl_stream, sizeof rl_stream, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(rl_stream, sizeof rl_stream, vram, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(wram, "AAAAABCD", 8);
    PT_ASSERT_MEM_EQ(vram, "AAAAABCD", 8);
    PT_ASSERT_EQ_U32(wram[8], STALE);
    PT_ASSERT_EQ_U32(vram[8], STALE);

    uint32_t words[sizeof rl_stream / 4];
    memcpy(words, rl_stream, sizeof words);
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    RLUnCompWram(words, wram);
    RLUnCompVram(words, vram);
    PT_ASSERT_MEM_EQ(wram, "AAAAABCD", 8);
    PT_ASSERT_MEM_EQ(vram, "AAAAABCD", 8);
    teardown();
}

/* The longest run (flag FFh: 7Fh+3) and the longest literal block (flag
 * 7Fh: 7Fh+1); each source byte is consumed once. */
PTEST(rl_maximum_lengths)
{
    setup();
    struct builder b;
    header(&b, 3, RL_MAX_RUN + RL_MAX_LITERALS);
    put(&b, 0xFF);
    put(&b, 'r');
    put(&b, 0x7F);
    for (unsigned i = 0; i < RL_MAX_LITERALS; i++)
        put(&b, (uint8_t)i);
    uint8_t out[RL_MAX_RUN + RL_MAX_LITERALS + 2];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(b.bytes, b.size, out, BIOS_WRITE_16BIT), BIOS_OK);
    for (unsigned i = 0; i < RL_MAX_RUN; i++)
        PT_ASSERT_EQ_U32(out[i], 'r');
    for (unsigned i = 0; i < RL_MAX_LITERALS; i++)
        PT_ASSERT_EQ_U32(out[RL_MAX_RUN + i], i);
    PT_ASSERT_EQ_U32(out[RL_MAX_RUN + RL_MAX_LITERALS], STALE);
    teardown();
}

PTEST(rl_vram_odd_size_and_stop_inside_a_run)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {0x30, 0x03, 0x00, 0x00, 0xFF, 'z', 0x00, 0x00};
    uint8_t wram[6], vram[6];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(stream, sizeof stream, wram, BIOS_WRITE_8BIT), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(stream, sizeof stream, vram, BIOS_WRITE_16BIT), BIOS_OK);
    PT_ASSERT_MEM_EQ(wram, "zzz", 3);
    PT_ASSERT_EQ_U32(wram[3], STALE);
    PT_ASSERT_MEM_EQ(vram, "zz", 2);
    PT_ASSERT_EQ_U32(vram[2], STALE);
    teardown();
}

PTEST(rl_errors)
{
    setup();
    uint8_t out[8];
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(rl_stream, 8, out, BIOS_WRITE_8BIT), BIOS_ERR_TRUNCATED);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(lz_abc, sizeof lz_abc, out, BIOS_WRITE_8BIT), BIOS_ERR_HEADER);
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 4);
    memset(tail, STALE, 4);
    PT_ASSERT_EQ_INT(bios_rl_uncomp_bounded(rl_stream, sizeof rl_stream, tail, BIOS_WRITE_16BIT), BIOS_ERR_RANGE);
    PT_ASSERT_EQ_U32(tail[0], STALE);
    teardown();
}

/* ---- Huffman ------------------------------------------------------------- */

/* GBATEK's example: "Huff" in 6 bits, 10-11-0-0, root.0 = data "f", root.1 a
 * node whose children are "H" and "u". */
static const _Alignas(4) uint8_t huff_8bit[] = {
    0x28, 0x04, 0x00, 0x00, /* 8-bit units, type 2, size 4 */
    0x03,                   /* tree size: (3+1)*2 bytes from here to the bitstream */
    0x80,                   /* root: node0 is data; children at 4+0*2+2 = 6, 7 */
    'f',                    /* 6: data */
    0xC0,                   /* 7: both children data, at 6+0*2+2 = 8, 9 */
    'H',  'u',  0x00, 0x00, /* 8, 9; padding to the word */
    0x00, 0x00, 0x00, 0xB0, /* bits 1 0 1 1 0 0 from bit 31 */
};

/* Four leaves 1, 2, 3, 4 with codes 00, 01, 10, 11. */
#define HUFF_4BIT_TREE                                                       \
    0x03,                      /* tree size 3 */                             \
        0x00,                  /* root: children at 6 (A) and 7 (B) */       \
        0xC0,                  /* 6, A: data children at 6+0*2+2 = 8, 9 */   \
        0xC1,                  /* 7, B: data children at 6+1*2+2 = 10, 11 */ \
        0x01, 0x02, 0x03, 0x04 /* 8-11 */

PTEST(huffman_8bit_gbatek_example)
{
    setup();
    uint8_t out[8];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(huff_8bit, sizeof huff_8bit, out), BIOS_OK);
    PT_ASSERT_MEM_EQ(out, "Huff", 4);
    PT_ASSERT_EQ_U32(out[4], STALE);

    uint32_t words[sizeof huff_8bit / 4];
    memcpy(words, huff_8bit, sizeof words);
    memset(out, STALE, sizeof out);
    HuffUnComp(words, out);
    PT_ASSERT_MEM_EQ(out, "Huff", 4);
    teardown();
}

/* Nibbles fill each word from the low bits: 1, 2, 3, 4 -> bytes 21h, 43h.
 * 24 nibbles need 48 bits, so the bitstream crosses into a second word. */
PTEST(huffman_4bit_across_words)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x24,           0x0C, 0x00, 0x00,       /* 4-bit units, type 2, size 12 */
        HUFF_4BIT_TREE, 0x1B, 0x1B, 0x1B, 0x1B, /* 00 01 10 11 x 4, from bit 31 */
        0x00,           0x00, 0x1B, 0x1B,       /* 00 01 10 11 x 2, then unused bits */
    };
    uint8_t out[16];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(stream, sizeof stream, out), BIOS_OK);
    const uint8_t expected[12] = {0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x21, 0x43};
    PT_ASSERT_MEM_EQ(out, expected, sizeof expected);
    PT_ASSERT_EQ_U32(out[12], STALE);
    teardown();
}

/* Output is whole words: size 5 writes 8 bytes. */
PTEST(huffman_size_rounds_up_to_words)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x28, 0x05, 0x00, 0x00, 0x03, 0x80, 'f',  0xC0,
        'H',  'u',  0x00, 0x00, 0x00, 0x00, 0xC0, 0xB2, /* 10 11 0 0 10 11 0 0: "HuffHuff" */
    };
    uint8_t out[10];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(stream, sizeof stream, out), BIOS_OK);
    PT_ASSERT_MEM_EQ(out, "HuffHuff", 8);
    PT_ASSERT_EQ_U32(out[8], STALE);
    teardown();
}

/* GBATEK: a data node's upper bits "should be zero"; they are ignored. */
PTEST(huffman_data_upper_bits_ignored)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x24, 0x01, 0x00, 0x00, 0x03, 0x00, 0xC0, 0xC1,
        0xF1, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x10, /* 00 01: units 1, 2 */
    };
    uint8_t out[4];
    memset(out, STALE, sizeof out);
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(stream, sizeof stream, out), BIOS_OK);
    PT_ASSERT_EQ_U32(out[0], 0x21);
    teardown();
}

PTEST(huffman_errors)
{
    setup();
    uint8_t out[8];
    /* A child offset past the tree table. */
    static const _Alignas(4) uint8_t outside[] = {
        0x28, 0x04, 0x00, 0x00, 0x03, 0x3F, 'f', 0xC0, 'H', 'u', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(outside, sizeof outside, out), BIOS_ERR_STREAM);
    /* Unit size 3 cannot fill a 32-bit word. */
    static const _Alignas(4) uint8_t odd_units[] = {
        0x23, 0x04, 0x00, 0x00, 0x03, 0x80, 'f', 0xC0, 'H', 'u', 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0,
    };
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(odd_units, sizeof odd_units, out), BIOS_ERR_HEADER);
    /* LZ77 data is not Huffman data. */
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(lz_abc, sizeof lz_abc, out), BIOS_ERR_HEADER);
    /* The bitstream word is missing. */
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(huff_8bit, 12, out), BIOS_ERR_TRUNCATED);
    /* The tree table is cut short. */
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(huff_8bit, 7, out), BIOS_ERR_TRUNCATED);
    /* Output past the end of VRAM. */
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 2);
    PT_ASSERT_EQ_INT(bios_huff_uncomp_bounded(huff_8bit, sizeof huff_8bit, tail), BIOS_ERR_RANGE);
    teardown();
}

PTEST_MAIN()
