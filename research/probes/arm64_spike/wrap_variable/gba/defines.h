/* Probe only (task i): expansion's gba/defines.h, then its fixed GBA addresses
   redefined as offsets from a run-time base (the regions mmapped
   wherever mmap puts them). No expansion file changes. */
#include <stdint.h>
#include_next "gba/defines.h"
#ifndef PEN_WRAP_DEFINES
#define PEN_WRAP_DEFINES
extern uintptr_t pen_gba_base;
#undef EWRAM_START
#define EWRAM_START (pen_gba_base + 0x02000000)
#undef IWRAM_START
#define IWRAM_START (pen_gba_base + 0x03000000)
#undef PLTT
#define PLTT (pen_gba_base + 0x05000000)
#undef VRAM
#define VRAM (pen_gba_base + 0x06000000)
#undef OAM
#define OAM (pen_gba_base + 0x07000000)
#undef SOUND_INFO_PTR
#define SOUND_INFO_PTR (*(struct SoundInfo **)((char *)pen_gba_base + 0x03007FF0))
#undef INTR_CHECK
#define INTR_CHECK (*(u16 *)((char *)pen_gba_base + 0x03007FF8))
#undef INTR_VECTOR
#define INTR_VECTOR (*(u32 *)((char *)pen_gba_base + 0x03007FFC))
#endif
