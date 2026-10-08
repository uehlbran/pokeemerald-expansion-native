/*
 * transfer_bounds.h: may the game transfer this memory?
 *
 * Stands in for the GBA bus decoder as DMA and the BIOS copy calls meet it.
 * Used by the DMA and BIOS copy modules before they touch a range a game
 * supplied. See transfer_bounds.c for the rules and their sources.
 */
#ifndef TRANSFER_BOUNDS_H
#define TRANSFER_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum transfer_access { TRANSFER_READ, TRANSFER_WRITE };

/* True if every byte of [address, address + length) may be accessed that way.
 * length 0 is false. Wraparound of address + length is false. Before
 * transfer_bounds_init() (or after shutdown) it is always false. */
bool transfer_bounds_ok(uintptr_t address, size_t length, enum transfer_access access);

/* Records the executable image and the main thread's stack bounds; call once
 * from the main thread before any transfer; returns 0 or a negative error
 * code and prints which lookup failed. Calling it again re-records. */
int transfer_bounds_init(void);

/* Forgets the bounds. Safe without init and safe to call twice. */
void transfer_bounds_shutdown(void);

#endif /* TRANSFER_BOUNDS_H */
