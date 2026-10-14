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

enum transfer_bounds_error { TRANSFER_BOUNDS_OK = 0, TRANSFER_BOUNDS_ERR_STACK = -1 };

/* True if every byte of [address, address + length) may be accessed that way.
 * length 0 is false. Wraparound of address + length is false. Before
 * transfer_bounds_init() (or after shutdown) it is always false. */
bool transfer_bounds_ok(uintptr_t address, size_t length, enum transfer_access access);

/* Records the image and stack bounds (host_image). Call once, on the main
 * thread, before any transfer. Returns TRANSFER_BOUNDS_OK, or an error from
 * transfer_bounds_error after printing which lookup failed. Calling it again
 * shuts down first, so a failed re-init leaves every query false.
 * Threading: the state is written only here and in shutdown, on the main
 * thread, before any query; queries only read it. */
int transfer_bounds_init(void);

/* Forgets the bounds. Safe without init and safe to call twice. */
void transfer_bounds_shutdown(void);

#endif /* TRANSFER_BOUNDS_H */
