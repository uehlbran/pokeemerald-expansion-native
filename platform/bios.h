/*
 * bios.h: the GBA BIOS's SWI functions, as C functions.
 *
 * Stands in for the 16 KiB BIOS ROM at 00000000h and the SWI calls into it
 * (GBATEK "BIOS Functions", https://problemkaputt.de/gbatek.htm#biosfunctions).
 * On the GBA, expansion's libagbsyscall stubs load arguments into r0-r3 and
 * execute "svc #n"; the native link substitutes these functions for those
 * stubs: every name, parameter list and return type below is the one
 * include/gba/syscall.h declares (or, for the functions syscall.h does not
 * declare, the name libagbsyscall/libagbsyscall.s exports), so no expansion
 * header changes. platform/test/test_bios.c includes both headers in one
 * translation unit, so the compiler proves the declarations agree.
 *
 * This header does not include expansion's headers (docs/ARCHITECTURE.md).
 * The structs the game passes are only declared here; the implementation
 * reads and writes them at the byte offsets GBATEK gives, and the tests
 * assert that expansion's definitions have those offsets. Include this header
 * before <gba/syscall.h>, or where MODERN is 0: under MODERN, syscall.h makes
 * CpuSet and CpuFastSet function-like macros.
 *
 * The implementation is split by GBATEK's groups:
 *   bios_arith.c       Div, DivArm, Mod, ModArm, Sqrt, ArcTan, ArcTan2
 *   bios_affine.c      BgAffineSet, ObjAffineSet
 *   bios_memory.c      CpuSet, CpuFastSet
 *   bios_decompress.c  LZ77UnComp*, RLUnComp*, HuffUnComp
 *   bios_unfilter.c    BitUnPack, Diff8bitUnFilter*, Diff16bitUnFilter
 *   bios_control.c     resets, halts, the frame hook, MultiBoot
 *   bios_sound.c       SoundBias, MidiKey2Freq
 *   bios_stream.c      the decompressors' header, source bound and writers
 *   bios_internal.c    helpers shared by the files above (bios_internal.h)
 * Written from GBATEK, tonc ("BIOS calls",
 * https://www.coranac.com/tonc/text/swi.htm) and expansion's own m4a.c; no
 * reference port's or emulator's BIOS code was read (docs/C-RULES.md,
 * licensing). Where GBATEK leaves a behaviour open, the function's comment
 * below says what this implementation does; ArcTan's coefficients are the
 * one fact not taken from those sources (bios_arith.c says where from).
 *
 * Not provided: the BIOS sound driver (SWI 1Ah-1Eh, 20h-24h, 28h-2Ah:
 * SoundDriverInit and the rest, MusicPlayer*), which expansion replaces with
 * its own m4a engine and never calls; GetBiosChecksum, HardReset and
 * CustomHalt, which libagbsyscall does not export. A game reference to one of
 * them fails the native link.
 *
 * Validation. Every byte range a call reads or writes whose length comes
 * from the game (a count, a header, an unpack length) is checked with
 * transfer_bounds_ok() (transfer_bounds.h) before any byte is touched; a
 * refused call prints one line naming the function and the range to stderr
 * and does nothing. transfer_bounds_init() must have run (main does it).
 * Alignment is NOT checked: GBATEK requires 4-byte (or 2-byte) aligned
 * addresses for CpuSet, CpuFastSet, BitUnPack and the "Vram" decompressors,
 * and the hardware ignores the low address bits, but expansion compiles the
 * GBA build with -mabi=apcs-gnu, which pads every struct to 4-byte alignment,
 * and the native ABI does not, so native callers legitimately pass 2-byte and
 * 1-byte aligned addresses (CpuFastFill(..., gSaveBlock1Ptr->pcItems)).
 * Masking the low bits natively would write the neighbouring bytes, so every
 * unit is copied with memcpy at the address given.
 *
 * Host assumptions: little-endian, as the GBA is (bios_internal.h asserts
 * it); two's complement int32_t (guaranteed by <stdint.h>). No floating
 * point: the BIOS has none, and every fixed-point value here has an explicit
 * width.
 */
#ifndef BIOS_H
#define BIOS_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

/* The game's structs, defined in expansion's include/gba/types.h,
 * include/gba/multiboot.h, include/gba/m4a_internal.h and src/assertf.c. */
struct BgAffineSrcData;
struct BgAffineDstData;
struct ObjAffineSrcData;
struct MultiBootParam;
struct BitUnPackArgs;
struct WaveData;

enum bios_error {
    BIOS_OK = 0,
    BIOS_ERR_NO_RESET_TARGET = -1, /* SoftReset with no bios_set_reset_target() */
    BIOS_ERR_RANGE = -2,           /* transfer_bounds refused a source or destination range */
    BIOS_ERR_HEADER = -3,          /* a stream's header names another format or unit size */
    BIOS_ERR_TRUNCATED = -4,       /* the stream needs bytes beyond the caller's bound */
    BIOS_ERR_STREAM = -5           /* malformed: a back-reference before the output, a Huffman node outside its tree */
};

/* ---- Control surface for the frame loop ------------------------------- */

/* Starts the module. run_frame is what VBlankIntrWait, IntrWait and Halt
 * call to let one frame pass (main passes the frame loop's step, which
 * renders, runs the timers and dispatches VBlank, whose handler ORs the flag
 * into INTR_CHECK). NULL is allowed: then those calls return at once
 * (tests). Forgets any reset target. Always returns BIOS_OK. */
int bios_init(void (*run_frame)(void));

/* Forgets the frame hook and the reset target. Safe at any time. */
void bios_shutdown(void);

/* The jmp_buf SoftReset longjmps to (with BIOS_RESET_JUMP_VALUE) after
 * clearing what the BIOS clears. main calls setjmp on it before entering
 * AgbMain; setjmp returning BIOS_RESET_JUMP_VALUE means the game reset. NULL
 * forgets it. The buffer must outlive every SoftReset call. */
#define BIOS_RESET_JUMP_VALUE 1
void bios_set_reset_target(jmp_buf *target);

/* SoftReset's documented error path: does what SoftReset does and longjmps
 * to the reset target; with no target it prints a message, changes nothing
 * and returns BIOS_ERR_NO_RESET_TARGET. Never returns BIOS_OK. */
int bios_soft_reset(uint32_t resetFlags);

/* ---- Reset functions (GBATEK "BIOS Reset Functions") ------------------ */

/* RESET_* flags for RegisterRamReset and the SoftReset family; the values
 * are GBATEK's bit numbers and equal include/gba/syscall.h's. */
#define BIOS_RESET_EWRAM UINT32_C(0x01)      /* bit 0: 256K on-board WRAM */
#define BIOS_RESET_IWRAM UINT32_C(0x02)      /* bit 1: 32K on-chip WRAM, except the last 200h bytes */
#define BIOS_RESET_PALETTE UINT32_C(0x04)    /* bit 2 */
#define BIOS_RESET_VRAM UINT32_C(0x08)       /* bit 3 */
#define BIOS_RESET_OAM UINT32_C(0x10)        /* bit 4: zero-filled (does not disable OBJs) */
#define BIOS_RESET_SIO_REGS UINT32_C(0x20)   /* bit 5: SIO registers; RCNT to general-purpose mode */
#define BIOS_RESET_SOUND_REGS UINT32_C(0x40) /* bit 6 */
#define BIOS_RESET_REGS UINT32_C(0x80)       /* bit 7: every other register */

/* libagbsyscall's SoftReset: IME = 0, RegisterRamReset(resetFlags), then SWI
 * 00h, which clears 3007E00h-3007FFFh and restarts the game. Natively the
 * restart is a longjmp to the reset target; with no target it prints and
 * returns to its caller (bios_soft_reset is the path that reports it). */
void SoftReset(uint32_t resetFlags);

/* libagbsyscall's SoftResetRom and SoftResetExram: as SoftReset; on the GBA
 * they first write the re-entry flag at 3007FFAh (0 = restart from ROM, 1 =
 * from EWRAM), which natively means nothing (there is one entry point, the
 * reset target) and is not written. SoftResetExram keeps EWRAM: it clears
 * bit 0 of resetFlags. */
void SoftResetRom(uint32_t resetFlags);
void SoftResetExram(uint32_t resetFlags);

/* SWI 01h. Clears the regions and resets the I/O registers resetFlags names
 * (BIOS_RESET_*), and always sets DISPCNT to 0080h (forced blank), as
 * GBATEK's bug note says. Registers: bit 7 zeroes the LCD, DMA, timer,
 * KEYCNT and IE/IF/WAITCNT/IME registers, but not the read-only VCOUNT,
 * KEYINPUT or DISPSTAT bits 0-2 (writes to them do nothing on hardware;
 * natively the frame loop and input own them), nor POSTFLG/HALTCNT; bit 6
 * zeroes 4000060h-40000A7h except SOUNDBIAS (the BIOS's SoundBias owns it;
 * GBATEK gives no reset value); bit 5 zeroes the SIO registers and sets RCNT
 * to 8000h ("switches to general purpose mode"). GBATEK's "LSBs of SIODATA32
 * are always destroyed" is not reproduced: it names no value. */
void RegisterRamReset(uint32_t resetFlags);

/* ---- Halt functions (GBATEK "BIOS Halt Functions") -------------------- */

/* SWI 05h: IntrWait(1, V-Blank). The frame boundary: calls the frame hook
 * until INTR_CHECK (3007FF8h) has the V-Blank bit, then clears it. */
void VBlankIntrWait(void);

/* SWI 04h. Sets IME to 1. discardOldFlags 0: returns at once if a waitFlags
 * bit is already set in INTR_CHECK; nonzero: clears those bits first and
 * waits for a new one. Waiting is calling the frame hook until INTR_CHECK &
 * waitFlags is nonzero; then the waitFlags bits are cleared in INTR_CHECK.
 * Like the hardware, it waits forever for an interrupt that never comes.
 * Without a hook, returns at once (after the clearing). */
void IntrWait(uint32_t discardOldFlags, uint32_t waitFlags);

/* SWI 02h. On hardware the CPU sleeps until (IE AND IF) is nonzero; natively
 * one frame passes through the hook (nothing else can raise an interrupt
 * while the game is not running). Without a hook, returns at once. */
void Halt(void);

/* SWI 03h. On hardware: very low power mode until a keypad, Game Pak or SIO
 * interrupt. Natively it returns at once. Expansion's test runner reaches
 * mGBA's exit through "swi 0x3" with the exit code in r0 (inline asm, not
 * this function); milestone 4 routes that to the test-runner shim. */
void Stop(void);

/* ---- Arithmetic (GBATEK "BIOS Arithmetic Functions") ------------------ */

/* SWI 06h returns three registers. */
struct bios_div_result {
    int32_t quotient;      /* r0: Number DIV Denom, truncated toward zero */
    int32_t remainder;     /* r1: Number MOD Denom, the sign of Number */
    uint32_t abs_quotient; /* r3: ABS(Number DIV Denom), unsigned */
};

/* SWI 06h with all three results. GBATEK: -1234, 10 gives -123, -4, +123.
 * INT32_MIN / -1 wraps as the 32-bit hardware does: quotient INT32_MIN,
 * remainder 0, abs_quotient 80000000h. Division by zero hangs the BIOS
 * ("usually gets caught in an endless loop"); here every result is 0 and the
 * first such call prints one line to stderr. */
struct bios_div_result bios_div(int32_t number, int32_t denom);

/* syscall.h's Div: SWI 06h's r0 (the quotient). */
int32_t Div(int32_t num, int32_t denom);
/* libagbsyscall's Mod: SWI 06h's r1 (the remainder). */
int32_t Mod(int32_t num, int32_t denom);
/* SWI 07h: as Div and Mod with the parameters exchanged (r0 = denom). */
int32_t DivArm(int32_t denom, int32_t num);
int32_t ModArm(int32_t denom, int32_t num);

/* SWI 08h: floor of the square root, so Sqrt(2) = 1 and Sqrt(FFFFFFFFh) =
 * FFFFh. */
uint16_t Sqrt(uint32_t num);

/* SWI 09h. tan is 1.1.14 fixed point (4000h = 1.0); the result is an angle
 * in units of 2*pi/10000h, signed, -4000h..4000h (GBATEK: "C000h-4000h"). The
 * BIOS evaluates an odd polynomial in 32-bit arithmetic; it is accurate to
 * about one unit for |tan| <= 1 (|angle| <= pi/4) and, as GBATEK warns,
 * inaccurate beyond: the polynomial diverges and, past |tan| = 1.79, the
 * 32-bit products wrap. Both are reproduced. GBATEK gives no prototype; the
 * argument and result are this 16-bit signed format. */
int16_t ArcTan(int16_t tan);

/* SWI 0Ah. The angle of the vector (x, y), 0000h-FFFFh for 0 <= theta < 2*pi
 * (4000h = pi/2). x and y are any s16 (only their ratio matters). The octant
 * is chosen from the signs and from |x| versus |y| so that ArcTan only ever
 * sees a ratio of magnitude <= 1, where it is accurate. (0, 0) gives 0. */
uint16_t ArcTan2(int16_t x, int16_t y);

/* ---- Rotation/scaling (GBATEK "BIOS Rotation/Scaling Functions") ------ */

/* SWI 0Eh. For each of count 20-byte source entries (s32 texX, s32 texY,
 * 8 fractional bits; s16 scrX, scrY; s16 sx, sy, 8 fractional bits; u16
 * alpha) writes one 16-byte destination entry (s16 pa, pb, pc, pd; s32 dx,
 * dy, 8 fractional bits):
 *   pa = sx*cos(a), pb = -sx*sin(a), pc = sy*sin(a), pd = sy*cos(a)
 *   dx = texX - (pa*scrX + pb*scrY), dy = texY - (pc*scrX + pd*scrY)
 * (the matrix and its sign convention are tonc's, "BIOS calls", eq. 17.1).
 * The angle uses only alpha's upper 8 bits (GBATEK). sin and cos come from a
 * 256-entry table in 1.14 fixed point; each matrix element is the product
 * shifted right 14 (rounding toward minus infinity), so 0, 90, 180 and 270
 * degrees are exact. dx and dy use those shifted elements, so the screen
 * point (scrX, scrY) maps exactly to (texX, texY). count <= 0 does nothing. */
void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, int32_t count);

/* SWI 0Fh. For each of count source entries (s16 sx, sy; u16 alpha; entries
 * 8 bytes apart, word-aligned as tonc's ObjAffineSource is) writes pa, pb,
 * pc, pd as BgAffineSet computes them, each offset bytes after the last
 * (2 = contiguous, 8 = OAM); the next entry's pa goes 4*offset bytes on.
 * count <= 0 does nothing; a negative offset is refused. */
void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, int32_t count, int32_t offset);

/* ---- Memory copy (GBATEK "BIOS Memory Copy") -------------------------- */

/* The control word of CpuSet and CpuFastSet (GBATEK; equal to syscall.h's
 * CPU_SET_* and CPU_FAST_SET_*). Bits 21-23, 25 and 27-31 are ignored. */
#define BIOS_CPU_SET_COUNT_MASK UINT32_C(0x001FFFFF) /* bits 0-20: unit count */
#define BIOS_CPU_SET_SRC_FIXED UINT32_C(0x01000000)  /* bit 24: fill with the first unit */
#define BIOS_CPU_SET_32BIT UINT32_C(0x04000000)      /* bit 26: 32-bit units (CpuSet only) */

/* SWI 0Bh. Copies or fills count 16-bit (or, with bit 26, 32-bit) units,
 * forward one unit at a time, so an overlapping copy to a higher address
 * repeats the first units, as the hardware's LDRH/STRH loop does. Count 0
 * does nothing. */
void CpuSet(const void *src, void *dest, uint32_t control);

/* SWI 0Ch. Copies or fills 32-bit words in blocks of 8 (32 bytes); the count
 * is rounded up to a multiple of 8 words, and the extra words ARE copied, as
 * on the GBA. A copy moves each 32-byte block through a buffer (LDMIA then
 * STMIA). Bit 26 is ignored. If the rounded range does not fit, nothing is
 * copied. */
void CpuFastSet(const void *src, void *dest, uint32_t control);

/* ---- Decompression (GBATEK "BIOS Decompression Functions") ------------ */

/* Every stream starts with a 32-bit header: bits 4-7 the type (LZ77 1,
 * Huffman 2, run-length 3, diff filter 8), bits 8-31 the decompressed size
 * in bytes. A header naming another type is refused (BIOS_ERR_HEADER):
 * GBATEK says the type "must be" that value and does not say whether the
 * BIOS checks it. The destination range [dest, dest + size) is
 * validated before anything is written; output stops at exactly size bytes,
 * even in the middle of a block. A size of 0 writes nothing.
 *
 * The game-facing functions trust the stream as the hardware does: they read
 * the 4 header bytes through transfer_bounds and then exactly the bytes the
 * stream consumes. The bios_*_bounded variants take the stream's length and
 * return BIOS_ERR_TRUNCATED instead of reading beyond it; the game-facing
 * ones call them with no bound and print any error.
 *
 * "Wram" variants write bytes. "Vram" variants write 16-bit units, never a
 * byte: a byte is held until its partner arrives and the pair is written at
 * dest + 2k. So, as GBATEK warns, an LZ77 back-reference with disp 0 read at
 * an odd output position reads the destination's old byte (the newest byte
 * is still held), and an odd size leaves the last byte unwritten. */
enum bios_write_width {
    BIOS_WRITE_8BIT, /* the "Wram" variants */
    BIOS_WRITE_16BIT /* the "Vram" variants */
};

/* SWI 11h/12h: LZ77. A back-reference reaching before dest is
 * BIOS_ERR_STREAM (the hardware would read whatever memory precedes it). */
int bios_lz77_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width);
void LZ77UnCompWram(const uint32_t *src, void *dest);
void LZ77UnCompVram(const uint32_t *src, void *dest);

/* SWI 14h/15h: run-length. */
int bios_rl_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width);
void RLUnCompWram(const uint32_t *src, void *dest);
void RLUnCompVram(const uint32_t *src, void *dest);

/* SWI 13h: Huffman. Header bits 0-3 are the data unit size, 1, 2, 4 or 8
 * bits (others are BIOS_ERR_HEADER). Output is written in 32-bit words,
 * first unit in the low bits, so size is rounded up to whole words (GBATEK:
 * "Data is written in units of 32bits"); the destination range is checked
 * for the rounded size. A data node's bits above the unit size are ignored
 * (GBATEK: "should be zero"). Child offsets are relative to the stream start,
 * assuming it is even, as GBATEK requires it to be word-aligned; a child
 * outside the tree table is BIOS_ERR_STREAM. */
int bios_huff_uncomp_bounded(const void *src, size_t src_size, void *dest);
void HuffUnComp(const uint32_t *src, void *dest);

/* SWI 10h. Unpack information (GBATEK): u16 source length in bytes; u8
 * source unit width, 1, 2, 4 or 8; u8 destination unit width, 1, 2, 4, 8, 16
 * or 32; u32 data offset in bits 0-30 and the zero-data flag in bit 31.
 * Source units are taken from each byte's low bits first and packed into
 * 32-bit words from the low bits up. The offset is added to every nonzero
 * unit, and to zero units too if the flag is set; the sum is cut to the
 * destination width (GBATEK: it "should not exceed" it). Only whole words
 * are written; a trailing partial word is dropped. An unsupported width
 * prints and writes nothing. Declared as src/assertf.c declares it. */
void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *info);

/* SWI 16h/17h/18h: undo a difference filter. Header bits 0-3 are the unit
 * size (1 for the 8-bit functions, 2 for the 16-bit one), bits 4-7 type 8.
 * Output unit n is the sum of input units 0..n, wrapping at the unit width.
 * The input is 4 + size bytes, validated with the output. Diff16bitUnFilter
 * writes 16-bit units; an odd size leaves the last byte unwritten. */
void Diff8bitUnFilterWram(const void *src, void *dest);
void Diff8bitUnFilterVram(const void *src, void *dest);
void Diff16bitUnFilter(const void *src, void *dest);

/* ---- Sound (GBATEK "BIOS Sound Functions") ---------------------------- */

/* SWI 19h through libagbsyscall's three entries. Sets SOUNDBIAS (4000088h)
 * bits 0-9 to 000h (level 0) or 200h (any other level) and keeps bits 10-15.
 * The hardware steps there with short delays to avoid a click; natively the
 * level is set at once. */
void SoundBiasChange(uint32_t level);
void SoundBiasReset(void);
void SoundBiasSet(void);

/* SWI 1Fh. The playback rate of the wave wa (its 32-bit freq at byte 4) at
 * MIDI key mk plus fp/256 halftones:
 *   freq * 2^((mk + fp/256 - 180) / 12)
 * so key 168 halves freq, which is GBATEK's BIOS-reading formula. Computed as
 * the GBA sound library computes it (expansion's src/m4a.c MidiKeyToFreq):
 * the 2^(n/12) factor from a 12-entry table in 1.31 fixed point shifted by
 * the octave, interpolated linearly between keys mk and mk+1 by fp/256, then
 * freq times it divided by 2^32. Keys above 178 are treated as 178 + 255/256
 * (the factor for key 180 does not fit in 32 bits). Prints and returns 0 if
 * transfer_bounds refuses the wave's first 8 bytes. */
uint32_t MidiKey2Freq(const struct WaveData *wa, uint8_t mk, uint8_t fp);

/* ---- Multi Boot (GBATEK "BIOS Multi Boot") ---------------------------- */

/* SWI 25h. There is no link port natively, so the transfer fails: returns 1
 * ("1=failed") without touching mp. */
int MultiBoot(struct MultiBootParam *mp);

#endif /* BIOS_H */
