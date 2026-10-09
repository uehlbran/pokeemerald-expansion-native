/*
 * m4a engine: SoundMainBTM, the translation of src/m4a_1.s SoundMainBTM.
 *
 * It clears 64 bytes (four stm of four zero words). src/m4a.c reaches it
 * as Clear64byte, through gMPlayJumpTable[35], to clear a MusicPlayerInfo
 * (MPlayOpen) or the first 64 bytes of a starting MusicPlayerTrack
 * (MPlayMain), which stop short of its cmdPtr; m4a_layout.h asserts that as
 * M4A_CLEAR_SIZE.
 *
 * This file stands alone because include/gba/m4a_internal.h declares
 * "void SoundMainBTM(void)" while every caller passes the address to
 * clear (Clear64byte calls it as void (*)(void *)). A definition that
 * takes the address cannot see that declaration, so this file does not
 * include the header. The mismatch is upstream's.
 */
#include <string.h>

/* M4A_CLEAR_SIZE in m4a_engine.h, which this file cannot include. */
#define SOUND_MAIN_BTM_CLEAR_SIZE 64

void SoundMainBTM(void *dest);

void SoundMainBTM(void *dest)
{
    memset(dest, 0, SOUND_MAIN_BTM_CLEAR_SIZE);
}
