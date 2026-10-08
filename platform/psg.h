/*
 * psg.h: the GBA's four Game Boy sound channels (the PSG), synthesised from
 * the sound registers the game writes.
 *
 * Stands in for GBATEK "GBA Sound Controller": "GBA Sound Channel 1 - Tone &
 * Sweep", "Channel 2 - Tone", "Channel 3 - Wave Output", "Channel 4 - Noise"
 * and the PSG half of "GBA Sound Control Registers"
 * (https://problemkaputt.de/gbatek.htm#gbasoundcontroller). On the GBA these
 * circuits run by themselves once the game has written NR10..NR52, SOUNDCNT_L,
 * SOUNDCNT_H and wave RAM. Here the game's writes are plain stores into the
 * I/O region (gba_memmap.h), and psg_generate() reads that state, acts on it
 * the way the hardware would have, and produces PCM for the audio module.
 * How the register state is interpreted, the timing model and the output
 * scale: psg.c. The Direct Sound FIFOs are not here; the m4a mixer produces
 * that half of the output.
 *
 * Not thread-safe, by design: call it from the thread that runs the game and
 * writes the registers (docs/DECISIONS.md, "Single thread").
 */
#ifndef PSG_H
#define PSG_H

#include <stddef.h>
#include <stdint.h>

#include "gba_timing.h"

/* Highest output rate psg_init() accepts: the system clock itself (GBATEK
 * "LCD Dimensions and Timings", System Clock, 16*1024*1024 Hz; gba_timing.h).
 * Above it consecutive output samples would repeat the same hardware state. */
#define PSG_MAX_OUTPUT_RATE_HZ GBA_CPU_HZ

/* Largest magnitude psg_generate() writes: four channels at level 15 with
 * every volume at maximum. The derivation is in psg.c ("OUTPUT SCALE"); it
 * leaves room for the audio module to add the Direct Sound FIFOs at the same
 * scale without overflowing int16_t. */
#define PSG_MAX_OUTPUT_LEVEL 15360

enum psg_error {
    PSG_OK = 0,
    PSG_ERR_OUTPUT_RATE = -1 /* rate is 0 or above PSG_MAX_OUTPUT_RATE_HZ */
};

/* Resets the module to its power-on state (every channel off, the PSG
 * powered down until it sees SOUNDCNT_X's master enable set, wave RAM banks
 * zero) and sets the rate psg_generate() produces samples at. Returns PSG_OK,
 * or PSG_ERR_OUTPUT_RATE and leaves the module uninitialised. Calling it
 * again re-initialises. Does not touch the I/O region. */
int psg_init(uint32_t output_sample_rate_hz);

/* Returns the module to its uninitialised state. Owns no resources, so it is
 * safe at any time: before init, after a failed init, and more than once. */
void psg_shutdown(void);

/* Reads the sound registers from the I/O region (NR10..NR52, SOUNDCNT_L/H/X,
 * wave RAM), acts on the trigger bits written since the last call, advances
 * the frame sequencer and the four channels by `frames` output samples, and
 * writes `frames` stereo frames to stereo_out as interleaved signed 16-bit
 * samples, left first. Afterwards SOUNDCNT_X's read-only bits 0-3 hold each
 * channel's on flag, as the hardware keeps them.
 *
 * Trigger bits: on the GBA, NRx4 bit 7 ("Initial", write-only) restarts the
 * channel once per write and never reads back as 1. Plain memory keeps the
 * written 1, so this module consumes it: it acts on the bit once and clears
 * it in the I/O region, after which the register reads as on hardware and the
 * game's next write of the bit is a new trigger. See psg.c, "TRIGGERS".
 *
 * frames == 0 still applies the registers and updates the status bits, and
 * writes nothing. Returns the number of frames written: `frames`, or 0 if the
 * module is not initialised or stereo_out is NULL while frames > 0. */
size_t psg_generate(int16_t *stereo_out, size_t frames);

#endif /* PSG_H */
