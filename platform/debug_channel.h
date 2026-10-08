/*
 * debug_channel.h: mGBA's debug registers, as expansion's test runner and
 * the hydra that reads its output expect them.
 *
 * Stands in for the three registers mGBA adds to the GBA's address space for
 * a ROM to print through (mGBA src/gba/io.c, src/gba/gba.c GBADebug;
 * include/mgba/internal/gba/io.h; docs/research/test-runner-protocol.md):
 *   STRING  04FFF600h, 256 bytes: the text of the next message;
 *   FLAGS   04FFF700h, 16 bits: bits 0-2 the level, bit 8 "send";
 *   ENABLE  04FFF780h, 16 bits: writing C0DEh turns the registers on, and a
 *           read then returns 1DEAh.
 * Writing FLAGS with bit 8 set logs the buffer (up to 256 bytes, to the
 * first NUL) at level 1 << (FLAGS & 7), masked to 1Fh, then zeroes all 256
 * bytes and clears bit 8. mgba-rom-test prints each message on stdout as
 * "<category>: <message>\n", the category of these being "GBA Debug"; the
 * hydra (tools/mgba-rom-test-hydra) runs it with -l15, so only the levels
 * FATAL, ERROR, WARN and INFO (1, 2, 4, 8) are printed. Expansion's runner
 * sends at MGBA_LOG_INFO (3, so level 8) only.
 *
 * Natively the registers are plain memory in the mapped debug page
 * (gba_memmap.h), so the runner's stores into STRING need nothing; the two
 * accesses with side effects are routed here by guarded modifications of
 * test/test_runner.c (docs/UPSTREAM_MODIFICATIONS.md): the handshake
 * (debug_channel_open, in place of MgbaOpen_'s write and read of ENABLE) and
 * the send (debug_channel_send, in place of the store to FLAGS).
 *
 * Output goes to the stream given to debug_channel_init, one line per send,
 * flushed after every line, so that nothing is lost if the process dies
 * right after (a test that crashes the game; the hydra reads a pipe). Before
 * debug_channel_init, or after debug_channel_shutdown, the registers behave
 * as on a GBA without mGBA: the handshake fails and sends print nothing.
 *
 * Single-threaded: the game's thread only.
 */
#ifndef DEBUG_CHANNEL_H
#define DEBUG_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "gba_memmap.h"

/* mGBA include/mgba/internal/gba/io.h: REG_DEBUG_STRING, REG_DEBUG_FLAGS,
 * REG_DEBUG_ENABLE, inside the mapped debug page. */
#define DEBUG_CHANNEL_STRING_ADDRESS UINT32_C(0x04FFF600)
#define DEBUG_CHANNEL_FLAGS_ADDRESS UINT32_C(0x04FFF700)
#define DEBUG_CHANNEL_ENABLE_ADDRESS UINT32_C(0x04FFF780)

/* mGBA struct GBA: char debugString[0x100]. */
#define DEBUG_CHANNEL_STRING_SIZE 256u

/* mGBA src/gba/io.c: the value that turns the registers on, and what ENABLE
 * reads while they are on. */
#define DEBUG_CHANNEL_ENABLE_REQUEST UINT16_C(0xC0DE)
#define DEBUG_CHANNEL_ENABLE_ACK UINT16_C(0x1DEA)

/* mGBA GBADebugFlags: bits 0-2 the level, bit 8 send. */
#define DEBUG_CHANNEL_FLAGS_LEVEL_MASK UINT16_C(0x0007)
#define DEBUG_CHANNEL_FLAGS_SEND UINT16_C(0x0100)

/* mGBA GBADebug: the mLogLevel is 1 << level, masked to 1Fh (FATAL 01h,
 * ERROR 02h, WARN 04h, INFO 08h, DEBUG 10h; levels 5-7 give 0). */
#define DEBUG_CHANNEL_LOG_LEVEL_MASK 0x1Fu

/* The levels mgba-rom-test prints under the hydra, which passes -l15. */
#define DEBUG_CHANNEL_PRINTED_LEVELS 0x0Fu

/* What precedes each message: mGBA's standard logger prints
 * "<mLogCategoryName>: ", and the debug registers' category is "GBA Debug". */
#define DEBUG_CHANNEL_LINE_PREFIX "GBA Debug: "

enum debug_channel_error {
    DEBUG_CHANNEL_OK = 0,
    DEBUG_CHANNEL_ERR_NO_STREAM = -1, /* debug_channel_init was given NULL */
    DEBUG_CHANNEL_ERR_LAYOUT = -2     /* the debug page is not mapped where gba_memmap.h says */
};

/* Keeps out, where the messages go (the caller owns it and keeps it open
 * until debug_channel_shutdown), and powers the registers on as mGBA does:
 * off until the handshake, the buffer and FLAGS zeroed. Checks the debug
 * page is mapped (gba_memmap). Returns DEBUG_CHANNEL_OK, or an error after a
 * report on stderr, leaving the module uninitialised. */
int debug_channel_init(FILE *out);

/* The handshake MgbaOpen_ does: stores DEBUG_CHANNEL_ENABLE_REQUEST in
 * ENABLE, as the runner's write does, and, if initialised, turns the
 * registers on and leaves DEBUG_CHANNEL_ENABLE_ACK there for the read that
 * follows on mGBA. Returns whether ENABLE then reads the ack: true once
 * initialised, false otherwise (a GBA without mGBA, where the runner exits
 * with 2). */
bool debug_channel_open(void);

/* The store to FLAGS: stores flags there and, if the registers are on and
 * bit 8 is set, does what GBADebug does: prints DEBUG_CHANNEL_LINE_PREFIX,
 * the buffer up to its first NUL or its 256th byte, and a newline, flushed,
 * if the level is one of DEBUG_CHANNEL_PRINTED_LEVELS; zeroes the buffer;
 * clears bit 8 in FLAGS. With bit 8 clear, or before the handshake, only the
 * store happens. A failed write is reported on stderr once and the message
 * dropped (the reader is gone). */
void debug_channel_send(uint16_t flags);

/* Forgets the stream and turns the registers off. Safe at any time, and
 * twice. Does not close the stream. */
void debug_channel_shutdown(void);

#endif /* DEBUG_CHANNEL_H */
