// deps: bios_arith.c bios_affine.c bios_memory.c bios_decompress.c bios_unfilter.c bios_control.c bios_sound.c bios_stream.c bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * The BIOS module against expansion's view of it.
 *
 * bios.h and expansion's include/gba/syscall.h (pulled in by gba/gba.h)
 * declare the same functions; with both in this translation unit, any
 * difference in a parameter or return type is a compile error ("conflicting
 * types"), so this file compiling is the signature check. The struct
 * layouts the BIOS functions read and write are GBATEK's ("BIOS
 * Rotation/Scaling Functions", "BIOS Decompression Functions": BitUnPack)
 * and must be expansion's; asserted here at compile time. Then one call
 * through each syscall.h name, linked against the platform's definitions,
 * proves the link substitutes them for libagbsyscall.
 */
#include "bios.h"

#include <gba/m4a_internal.h> /* gba/gba.h: types.h, multiboot.h, syscall.h, io_reg.h */

#include <stddef.h>
#include <string.h>

#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

/* src/assertf.c declares BitUnPack and its argument struct itself; this is
 * its declaration, verbatim, so the compiler checks bios.h's against it. */
struct BitUnPackArgs
{
    u16 compressedSize;
    u8 compressedBits;
    u8 decompressedBits;
    u32 dataOffset:31;
    u32 offsetZeros:1;
};
extern void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *);

/* syscall.h's RESET_* and CPU_SET_* are the BIOS's bits. */
_Static_assert(RESET_EWRAM == BIOS_RESET_EWRAM && RESET_IWRAM == BIOS_RESET_IWRAM
                   && RESET_PALETTE == BIOS_RESET_PALETTE && RESET_VRAM == BIOS_RESET_VRAM
                   && RESET_OAM == BIOS_RESET_OAM && RESET_SIO_REGS == BIOS_RESET_SIO_REGS
                   && RESET_SOUND_REGS == BIOS_RESET_SOUND_REGS && RESET_REGS == BIOS_RESET_REGS,
               "RESET_* flags");
_Static_assert(CPU_SET_SRC_FIXED == BIOS_CPU_SET_SRC_FIXED && CPU_SET_32BIT == BIOS_CPU_SET_32BIT
                   && CPU_FAST_SET_SRC_FIXED == BIOS_CPU_SET_SRC_FIXED,
               "CpuSet control bits");

/* GBATEK BgAffineSet source: s32, s32, s16 x5 (u16 last), 20 bytes each
 * (word-aligned entries). */
_Static_assert(offsetof(struct BgAffineSrcData, texX) == 0, "BgAffineSrcData.texX");
_Static_assert(offsetof(struct BgAffineSrcData, texY) == 4, "BgAffineSrcData.texY");
_Static_assert(offsetof(struct BgAffineSrcData, scrX) == 8, "BgAffineSrcData.scrX");
_Static_assert(offsetof(struct BgAffineSrcData, scrY) == 10, "BgAffineSrcData.scrY");
_Static_assert(offsetof(struct BgAffineSrcData, sx) == 12, "BgAffineSrcData.sx");
_Static_assert(offsetof(struct BgAffineSrcData, sy) == 14, "BgAffineSrcData.sy");
_Static_assert(offsetof(struct BgAffineSrcData, alpha) == 16, "BgAffineSrcData.alpha");
_Static_assert(sizeof(struct BgAffineSrcData) == 20, "BgAffineSrcData size");
/* GBATEK BgAffineSet destination: s16 x4, s32 x2, 16 bytes each. */
_Static_assert(offsetof(struct BgAffineDstData, pa) == 0, "BgAffineDstData.pa");
_Static_assert(offsetof(struct BgAffineDstData, pb) == 2, "BgAffineDstData.pb");
_Static_assert(offsetof(struct BgAffineDstData, pc) == 4, "BgAffineDstData.pc");
_Static_assert(offsetof(struct BgAffineDstData, pd) == 6, "BgAffineDstData.pd");
_Static_assert(offsetof(struct BgAffineDstData, dx) == 8, "BgAffineDstData.dx");
_Static_assert(offsetof(struct BgAffineDstData, dy) == 12, "BgAffineDstData.dy");
_Static_assert(sizeof(struct BgAffineDstData) == 16, "BgAffineDstData size");
/* GBATEK ObjAffineSet source: s16, s16, u16. Expansion's struct is 6 bytes
 * (no padding), so an array of them is not the BIOS's 8-byte stride; every
 * caller in expansion passes count 1. */
_Static_assert(offsetof(struct ObjAffineSrcData, xScale) == 0, "ObjAffineSrcData.xScale");
_Static_assert(offsetof(struct ObjAffineSrcData, yScale) == 2, "ObjAffineSrcData.yScale");
_Static_assert(offsetof(struct ObjAffineSrcData, rotation) == 4, "ObjAffineSrcData.rotation");
/* GBATEK BitUnPack info: u16 length, u8 source width, u8 destination width,
 * u32 offset (bits 0-30) and zero flag (bit 31). */
_Static_assert(offsetof(struct BitUnPackArgs, compressedBits) == 2, "BitUnPackArgs.compressedBits");
_Static_assert(offsetof(struct BitUnPackArgs, decompressedBits) == 3, "BitUnPackArgs.decompressedBits");
_Static_assert(sizeof(struct BitUnPackArgs) == 8, "BitUnPackArgs size");
/* GBATEK MidiKey2Freq reads the wave's frequency word at byte 4 (the
 * BIOS-reading formula reads [wa + 4]). */
_Static_assert(offsetof(struct WaveData, freq) == 4, "WaveData.freq");

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    PT_ASSERT_EQ_INT(bios_init(NULL), BIOS_OK);
}

static void teardown(void)
{
    bios_shutdown();
    transfer_bounds_shutdown();
}

/* The bits of BitUnPackArgs' bitfield word, as the BIOS reads them. */
PTEST(bit_unpack_args_bitfield_is_gbatek_word)
{
    struct BitUnPackArgs args = { .dataOffset = 0x12345, .offsetZeros = 1 };
    uint32_t word = 0;
    memcpy(&word, (const unsigned char *)&args + 4, sizeof word);
    PT_ASSERT_EQ_U32(word, UINT32_C(0x80012345));
}

/* One call through each syscall.h name resolves to the platform's code. */
PTEST(syscall_h_names_link_to_the_platform)
{
    setup();
    PT_ASSERT_EQ_INT(Div(-1234, 10), -123);
    PT_ASSERT_EQ_U32(Sqrt(144), 12);
    PT_ASSERT_EQ_U32(ArcTan2(0, 0x100), 0x4000);

    u16 source[4] = { 1, 2, 3, 4 };
    u16 copy[4] = { 0 };
    CpuSet(source, copy, CPU_SET_16BIT | 4);
    PT_ASSERT_MEM_EQ(copy, source, sizeof copy);

    u32 words[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    u32 fast[8] = { 0 };
    CpuFastSet(words, fast, 8);
    PT_ASSERT_MEM_EQ(fast, words, sizeof fast);

    struct BgAffineSrcData bg_src = { .texX = 0x8000, .texY = 0x8000, .scrX = 120, .scrY = 80,
                                      .sx = 0x100, .sy = 0x100, .alpha = 0 };
    struct BgAffineDstData bg_dst = { 0 };
    BgAffineSet(&bg_src, &bg_dst, 1);
    PT_ASSERT_EQ_INT(bg_dst.pa, 0x100);
    PT_ASSERT_EQ_INT(bg_dst.dx, 0x8000 - 0x100 * 120);

    struct ObjAffineSrcData obj_src = { .xScale = 0x100, .yScale = 0x200, .rotation = 0 };
    s16 matrix[4] = { 0 };
    ObjAffineSet(&obj_src, matrix, 1, 2);
    PT_ASSERT_EQ_INT(matrix[0], 0x100);
    PT_ASSERT_EQ_INT(matrix[3], 0x200);

    /* LZ77: header type 1, size 2; flag 00h, then the literals "ab". */
    static const u32 lz77[] = { 0x00000210, 0x00626100 };
    u8 lz_out[2] = { 0 };
    LZ77UnCompWram(lz77, lz_out);
    PT_ASSERT_MEM_EQ(lz_out, "ab", 2);
    u16 lz_vram[1] = { 0 };
    LZ77UnCompVram(lz77, lz_vram);
    PT_ASSERT_MEM_EQ(lz_vram, "ab", 2);

    /* Run-length: 3 x 'z'; header type 3, size 3; flag 80h = run of 0+3. */
    static const u32 rl[] = { 0x00000330, 0x00007A80 };
    u8 rl_out[3] = { 0 };
    RLUnCompWram(rl, rl_out);
    PT_ASSERT_MEM_EQ(rl_out, "zzz", 3);
    u16 rl_vram[2] = { 0 };
    RLUnCompVram(rl, rl_vram);
    PT_ASSERT_MEM_EQ(rl_vram, "zz", 2); /* the odd last byte is held, not written */

    PT_ASSERT_EQ_INT(MultiBoot(NULL), 1);

    RegisterRamReset(0);
    PT_ASSERT_EQ_U32(REG_DISPCNT, 0x0080); /* forced blank, whatever the flags */
    VBlankIntrWait();                       /* no hook: returns at once */
    teardown();
}

PTEST(bit_unpack_as_assertf_calls_it)
{
    setup();
    /* assertf.c unpacks its 1bpp font to 4bpp: 1 bit to 4, offset 0. */
    static const u8 glyph[2] = { 0x81, 0x0F };
    static const struct BitUnPackArgs args = { .compressedSize = 2, .compressedBits = 1,
                                               .decompressedBits = 4, .dataOffset = 0,
                                               .offsetZeros = 0 };
    u32 tiles[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
    BitUnPack(glyph, tiles, &args);
    PT_ASSERT_EQ_U32(tiles[0], UINT32_C(0x10000001));
    PT_ASSERT_EQ_U32(tiles[1], UINT32_C(0x00001111));
    teardown();
}

PTEST_MAIN()
