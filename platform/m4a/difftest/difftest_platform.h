/*
 * Differential test of the m4a engine against the original assembly.
 *
 * The same driver (difftest.c) is built twice: for ARM with expansion's
 * src/m4a_1.s and run under qemu-arm, and natively with the C translation.
 * Both print a digest of the engine's state after every frame of randomly
 * generated scenarios; the digests must be identical.
 *
 * This header is the only target-specific part: the GBA pages the engine
 * addresses directly (IWRAM for SOUND_INFO_PTR, the I/O registers).
 */
#ifndef GUARD_PLATFORM_M4A_DIFFTEST_PLATFORM_H
#define GUARD_PLATFORM_M4A_DIFFTEST_PLATFORM_H

/* Maps the IWRAM page holding SOUND_INFO_PTR and the I/O page; returns 0 on success. */
int DifftestMapGbaPages(void);
void DifftestUnmapGbaPages(void);

#endif // GUARD_PLATFORM_M4A_DIFFTEST_PLATFORM_H
