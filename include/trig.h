#ifndef GUARD_TRIG_H
#define GUARD_TRIG_H

extern const s16 gSineTable[];

s16 Sin(s16 index, s16 amplitude);
s16 Cos(s16 index, s16 amplitude);
s16 Sin2(u16 angle);
s16 Cos2(u16 angle);

#if PLATFORM_NATIVE
// gSineTable has 320 entries; a few animations read index 320, one past the end.
// The GBA reads the s16 at 0x08DE3B78 (gSineTable + 640 in pokeemerald.elf),
// the low half of a code address in a jump table: bytes 1e 04, 0x041E.
// Natively that is whatever follows the table, which differs by build and width.
#define SINE_TABLE_LENGTH       320
#define SINE_TABLE_GBA_READ_END 0x041E
#endif

#endif // GUARD_TRIG_H
