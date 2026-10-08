/*
 * dma_hooks.h: the two DMA functions expansion's game code calls.
 *
 * Included from include/gba/macro.h under PLATFORM_NATIVE (by the path
 * "platform/dma_hooks.h", so the native build puts the repository root on
 * its quote include path), where DmaSetUnchecked becomes dma_set() and
 * DmaStop becomes dma_stop(): in the native process a register write is a
 * plain store and cannot start a transfer. Kept apart from dma.h so the game
 * sees only these two declarations. Semantics: dma.h.
 */
#ifndef PLATFORM_DMA_HOOKS_H
#define PLATFORM_DMA_HOOKS_H

#include <stdint.h>

/* Writes src, dest and control into DMA channel `channel`'s DMAxSAD,
 * DMAxDAD and DMAxCNT as expansion's DmaSetUnchecked did, then starts the
 * channel if control enables it. Returns 0 or a negative enum dma_error. */
int dma_set(unsigned channel, const void *src, void *dest, uint32_t control);

/* Stops DMA channel `channel` as expansion's DmaStop did. */
void dma_stop(unsigned channel);

#endif /* PLATFORM_DMA_HOOKS_H */
