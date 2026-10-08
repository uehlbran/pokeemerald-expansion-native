/*
 * gcn_multiboot.c: the GameCube multiboot library with no GameCube attached.
 *
 * Stands in for src/libgcnmultiboot.s, which downloads and runs a program
 * from a GameCube over the link port in JOY Bus mode (GBATEK "SIO JOY BUS
 * Mode"). The native build has no link port, so no transfer ever starts, and
 * these functions leave the library where the assembly leaves it without a
 * GameCube: Init clears the progress, Main finds nothing received, so the
 * copyright screen (src/intro.c) sees progress 0 and takes its "no
 * multiboot" path (GameCubeMultiBoot_Quit); the serial interrupt never
 * fires, and ExecuteProgram returns because nothing is ready to boot.
 *
 * The argument is expansion's struct GcmbStruct (include/libgcnmultiboot.h);
 * this file reads it by the assembly's own offsets (GCMB_STRUCT_* in
 * libgcnmultiboot.s) rather than including expansion's header.
 */
#include <stddef.h>
#include <stdint.h>

/* libgcnmultiboot.s: GCMB_STRUCT_MBPROGRESS, the byte the game reads as
 * gcmb_field_2, and MBPROGRESS_NONE, "no image received". */
#define GCMB_STRUCT_MBPROGRESS 0x02u
#define MBPROGRESS_NONE UINT8_C(0x00)

void GameCubeMultiBoot_Init(void *mb)
{
    if (mb != NULL)
        ((volatile uint8_t *)mb)[GCMB_STRUCT_MBPROGRESS] = MBPROGRESS_NONE;
}

void GameCubeMultiBoot_Main(void *mb)
{
    (void)mb;
}

void GameCubeMultiBoot_ExecuteProgram(void *mb)
{
    (void)mb;
}

void GameCubeMultiBoot_HandleSerialInterrupt(void *mb)
{
    (void)mb;
}

void GameCubeMultiBoot_Quit(void)
{
}
