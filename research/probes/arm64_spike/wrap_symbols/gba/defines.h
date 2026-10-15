/* Probe only (task i): expansion's gba/defines.h, then its fixed GBA addresses
   redefined as link-time symbols of a slid image (the regions as zerofill
   sections the loader places with the image). No expansion file changes. */
#include <stdint.h>
#include_next "gba/defines.h"
#ifndef PEN_WRAP_DEFINES
#define PEN_WRAP_DEFINES
extern char pen_gba_ewram[], pen_gba_iwram[], pen_gba_pltt[], pen_gba_vram[], pen_gba_oam[];
#undef EWRAM_START
#define EWRAM_START ((uintptr_t)pen_gba_ewram)
#undef IWRAM_START
#define IWRAM_START ((uintptr_t)pen_gba_iwram)
#undef PLTT
#define PLTT ((uintptr_t)pen_gba_pltt)
#undef VRAM
#define VRAM ((uintptr_t)pen_gba_vram)
#undef OAM
#define OAM ((uintptr_t)pen_gba_oam)
#undef SOUND_INFO_PTR
#define SOUND_INFO_PTR (*(struct SoundInfo **)(pen_gba_iwram + 0x7FF0))
#undef INTR_CHECK
#define INTR_CHECK (*(u16 *)(pen_gba_iwram + 0x7FF8))
#undef INTR_VECTOR
#define INTR_VECTOR (*(u32 *)(pen_gba_iwram + 0x7FFC))
#endif
