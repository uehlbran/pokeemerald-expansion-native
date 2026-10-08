# m4a engine: `src/m4a_1.s` in C

The GBA sound driver (MusicPlayer2000, "m4a") is two halves: C in
`src/m4a.c` and ARM/Thumb assembly in `src/m4a_1.s`. This directory is a
C translation of the assembly, so the native build can link the sound
engine without ARM code. `src/m4a.c`, `src/m4a_tables.c` and
`include/gba/m4a_internal.h` stay as upstream has them.

The assembly does two jobs:

- **Mixer.** `SoundMain` runs once a frame. It calls the sequencer and the
  PSG driver, then renders every Direct Sound channel into a ring of
  signed 8-bit PCM. On the GBA, DMA1 and DMA2 feed that ring to sound
  FIFOs A and B.
- **Sequencer.** `MPlayMain` advances the songs. It decodes track
  commands into the `ply_*` handlers, starts notes on channels
  (`ply_note`), runs the LFOs and gate times, and pushes volume and pitch
  changes to the channels.

This is game code translated, not hardware emulated. It follows the
assembly branch for branch and its arithmetic instruction for
instruction. The reference ports' C engines were not used.

## Files

| File | Contents |
|---|---|
| `m4a_engine.h` | Internal header: constants the C header lacks, channel-chain accessors, ARM arithmetic helpers, shared declarations. Includes `m4a_layout.h`. |
| `m4a_layout.h` | The struct-layout contract: `_Static_assert` on every offset and size the assembly hard-codes, in two groups (see below). |
| `m4a_mixer.c` | `SoundMain`, `m4aSoundVSync`, and `SoundMainRAM` up to the samples: frame selection, reverb or clearing, the channel loop, the envelope. |
| `m4a_render.c`, `m4a_render.h` | One channel's samples: the sample loops of `SoundMainRAM`, `SoundMainRAM_Unk1` (reversed and compressed samples) and `SoundMainRAM_Unk2` (DPCM decoding). |
| `m4a_sequencer.c` | `MPlayMain`, `TrackStop`, `RealClearChain`, `ChnVolSetAsm`, `clear_modM`, `umul3232H32`. |
| `m4a_note.c` | `ply_note`: voice selection, channel allocation and stealing, channel start. |
| `m4a_commands.c` | `MPlayJumpTableCopy` and the `ply_*` track commands, with the BIOS-read guard `chk_adr_r2`. |
| `m4a_clear64.c` | `SoundMainBTM`, in its own file (see "Notes for integration"). |
| `../test/test_m4a_*.c` | Unit tests: mixer, render, sequencer (`test_m4a_engine.c`), note, commands. They live with the other platform tests and follow their `// deps:` convention. |
| `test_m4a_support.c/.h`, `test_m4a_c_reference.c/.h` | Test fixtures and the `src/m4a.c` functions the tests need. |
| `Makefile` | Standalone build of the engine and its tests (`BITS=32` or `64`). |
| `difftest/` | Differential test of the C against the original assembly (see "Tests"). |

## Assembly symbol to C

Every global symbol of `src/m4a_1.s` that the game references keeps its
name and its prototype from `include/gba/m4a_internal.h` or
`include/m4a.h`.

| `src/m4a_1.s` | C | File | Linkage |
|---|---|---|---|
| `umul3232H32` | `umul3232H32` | m4a_sequencer.c | global |
| `SoundMain` | `SoundMain` | m4a_mixer.c | global |
| `SoundMainRAM` (frame, reverb, clear, channel loop, envelope) | `SoundMainRAM_Mix`, `SelectFrame`, `ApplyReverb`, `ClearFrame`, `MixChannel`, `StartChannel`, `StepEnvelope` | m4a_mixer.c | static |
| `SoundMainRAM` (sample loops from `_081DD03A`) | `M4aRenderChannel`, `MixFixedRate`, `MixForward`, `PlayFixedRate`, `PlayResampled` | m4a_render.c | `M4aRenderChannel` internal, rest static |
| `SoundMainRAM_Unk1` | `PlaySpecial`, `MixForward`, `MixBackward` | m4a_render.c | static |
| `SoundMainRAM_Unk2` | `DpcmSample`, `DecodeDpcmBlock` | m4a_render.c | static |
| `sDecodingBuffer` (.bss) | `sDecodingBuffer` | m4a_render.c | static |
| `SoundMainBTM` | `SoundMainBTM` | m4a_clear64.c | global |
| `RealClearChain` | `RealClearChain` | m4a_sequencer.c | global |
| `MPlayJumpTableCopy` | `MPlayJumpTableCopy` | m4a_commands.c | global |
| `chk_adr_r2` | `IsReadAllowed` | m4a_commands.c | static |
| `ld_r3_tp_adr_i` | `ReadCheckedByte` | m4a_commands.c | static |
| `ld_r3_tp_adr_i_unchecked` | `ReadByte` | m4a_commands.c | static |
| `ply_goto_1`, `ldrb_r3_r2` | `JumpToAddressAt` | m4a_commands.c | static |
| `ply_fine`, `ply_goto`, `ply_patt`, `ply_pend`, `ply_rept`, `ply_prio`, `ply_tempo`, `ply_keysh`, `ply_voice`, `ply_vol`, `ply_pan`, `ply_bend`, `ply_bendr`, `ply_lfodl`, `ply_modt`, `ply_tune`, `ply_port`, `ply_lfos`, `ply_mod`, `ply_endtie` | same names | m4a_commands.c | global |
| `m4aSoundVSync` | `m4aSoundVSync` | m4a_mixer.c | global |
| `MPlayMain` | `MPlayMain`, `RunPlayer`, `Tick`, `TickTrack`, `RunCommand`, `StepLfo`, `CountDownGateTimes`, `UpdateVolumeAndPitch` | m4a_sequencer.c | `MPlayMain` global, rest static |
| `TrackStop` | `TrackStop` | m4a_sequencer.c | global |
| `ChnVolSetAsm` | `ChnVolSetAsm` | m4a_sequencer.c | global (engine-internal) |
| `ply_note` | `ply_note`, `SelectVoice`, `FindCgbChannel`, `FindDirectChannel`, `LinkChannel` | m4a_note.c | `ply_note` global, rest static |
| `clear_modM` | `clear_modM` | m4a_sequencer.c | global (engine-internal) |
| `call_r3` | C calls through the function pointers | | |

Four global symbols of the assembly are not exported: `SoundMainRAM`,
`SoundMainRAM_Unk1`, `SoundMainRAM_Unk2` and `ld_r3_tp_adr_i`. Nothing
references them. `include/gba/m4a_internal.h` declares
`extern char SoundMainRAM[]`, which no code uses. A C function of that
name could not coexist with that declaration in the same file.

The engine calls into `src/m4a.c`: `MidiKeyToFreq`, `TrkVolPitSet`,
`FadeOutBody`, `ClearChain` and `Clear64byte`. It also uses
`src/m4a_tables.c`'s `gClockTable`, `gDeltaEncodingTable` and
`gMPlayJumpTableTemplate`. `m4a_engine.h` declares the five that the C
headers do not.

## The PCM ring

`SoundInfo.pcmBuffer` is `2 * PCM_DMA_BUF_SIZE` (2 x 1584) bytes of
signed 8-bit samples:

- The first 1584 bytes are the right channel. DMA1 copies them to FIFO A,
  which `SoundInit` routes to the right output.
- The second 1584 bytes are the left channel. DMA2 copies them to
  FIFO B, routed to the left output.

Each half is a ring of `pcmDmaPeriod` frames of `pcmSamplesPerVBlank`
samples:

- `SampleFreqSet` sets `pcmSamplesPerVBlank` from
  `gPcmSamplesPerVBlankTable` and `pcmDmaPeriod` to
  `1584 / pcmSamplesPerVBlank`.
- The sample rate is `pcmFreq = (597275 * pcmSamplesPerVBlank + 5000) / 10000` Hz,
  that many samples per GBA frame at 59.7275 Hz.
- The game's `m4aSoundInit` selects 13379 Hz: 224 samples per frame, a
  ring of 7 frames (1568 of the 1584 bytes used).

Which frame `SoundMain` writes depends on `pcmDmaCounter`, which
`m4aSoundVSync` counts down once a frame from `pcmDmaPeriod` to 1:

- With the counter above 1, it mixes frame `pcmDmaPeriod - counter + 1`.
- With the counter at 0 or 1, it mixes frame 0.

That is always the frame after the one the DMA is playing. When the
counter runs out, `m4aSoundVSync` restarts both DMAs at the start of the
ring. With reverb on, the frame is not cleared first. Instead each byte
becomes `(this frame's left + right + the next frame's left + right) *
reverb >> 9`, rounded toward zero, with the next frame wrapping to frame
0 from the last one. The engine mixes channels into the bytes modulo 256,
without saturation, exactly as the GBA does. Converting the ring to the
host's audio format is the platform's job.

## The struct-layout contract

The assembly addresses `SoundInfo`, `SoundChannel`, `CgbChannel`,
`MusicPlayerInfo`, `MusicPlayerTrack`, `ToneData` and `WaveData` by the
numeric offsets in `constants/m4a_constants.inc`. The C uses the structs
in `include/gba/m4a_internal.h`. `m4a_layout.h` asserts, with
`_Static_assert` and `offsetof`, every offset and size the assembly
depends on, in two groups. The first holds on every target: offsets
before any pointer, and the relations the C itself relies on. The second
is the assembly's exact layout, which the C structs have only where
pointers are 4 bytes (`#if UINTPTR_MAX == UINT32_MAX`); everything in it
is flagged 64-bit. It also asserts a few contracts the layout implies:

- `SoundMainRAM` reads `masterVolume` as `[soundInfo, #o_SoundChannel_release]`,
  which works because both offsets are 7.
- `Clear64byte` clears 64 bytes: all of a `MusicPlayerInfo`, and a
  starting `MusicPlayerTrack` up to its `cmdPtr`.
- `sound/music_player_table.inc` reserves `TRACK_SIZE` 0x50 per track.
- A key-split voice keeps its table's address in `ToneData.attack..release`
  as `.4byte`; the engine reads those four bytes as a little-endian address.

A track's channel chain mixes `SoundChannel` and `CgbChannel` nodes. The
offsets the two structs share are asserted in `m4a_engine.h`. A node of
unknown kind is accessed only through byte reads and writes and
`memcpy`, so no object is read through the wrong struct type. Once the
node's type byte shows its kind, the code uses the right struct.

## How ARM-specific code became C

| Assembly | C |
|---|---|
| Four output samples rotated through one register (`add r6, r1, r6, ror 8`), with bits 16-23 of the product cleared | Each sample adds bits 8-15 of `volume * sample` to its own output byte, modulo 256. |
| The top two bits of the output pointer count the four samples of a word (`adds r5, r5, #0x40000000; bcc`) | A per-sample loop. The pointer-tag trick needs PCM addresses below 0x40000000, which the C does not. |
| Signed registers, `asr`, `ldrsb`, condition flags after `subs`/`adds` | `u32` two's-complement values with `AsrU32`, `SignExtend8`, `AsS32` and `LowByteS8`, and 64-bit comparisons that give the flag conditions (`CountRunsOut`, `SumIsPositive`). No signed overflow, no shift of a negative value, no implementation-defined conversion. |
| `umull` (`umul3232H32`) | `(u32)(((u64)a * b) >> 32)` |
| One 32-bit word over `SoundChannel.xpi` and `xpc` (the DPCM cache key) | Composed from the two 16-bit fields. |
| `currentPointer` holding an integer sample index for compressed samples | Stored and loaded as a `uintptr_t` with `memcpy`. |
| Address arithmetic over the `WaveData` header (reversed playback reads the byte before sample 0) | Sample reads index the `WaveData` object as bytes, with the index wrapping as the assembly's address does. |
| Pointers read from song data (`GOTO`, `PATT`, `REPT`) | Four little-endian bytes composed into a `u32` and converted through `uintptr_t`. |
| `cmp` of two track pointers (priority tie-break) | `uintptr_t` comparison. |

Every `SampleFreqSet` configuration writes its frame in whole 4-sample
words. So the per-sample loops produce exactly the bytes the word loops
do. `SelectFrame` rejects any other frame size.

## Departures from the assembly

All of them concern state the game never produces. Each one replaces an
out-of-bounds read or write with a defined result, and each has a unit
test.

| Situation | Assembly | C |
|---|---|---|
| A compressed sample read one past either end. Reversed playback reaching sample 0 reads "sample -1". Forward playback of a sample whose size is a multiple of 64 reads the block after the last. | Decodes whatever lies there. Block 0x3FFFFFF is 2 GiB past the wave. | Reads the sample as 0 and leaves the decoding cache unchanged. |
| A data byte before any running status (track data never written by mid2agb) | Waits `gClockTable[byte - 0x80]`, up to 128 bytes before the table. | Ends the track, as `FINE` does. |
| `maxChans` above 12 (`m4aSoundMode`'s 4-bit field allows 15) | Mixes and allocates "channels" 12-14 out of `pcmBuffer`. | Uses the 12 channels `SoundInfo` holds. A value of 0 still processes channel 0, as in the assembly. |
| A PSG voice naming channel 5-7 (`type & 7`) | Indexes past `gCgbChans[4]`. | Plays nothing. |
| `ply_note` called with a note command beyond `gClockTable` | Reads past the table. | Plays nothing. `MPlayMain` only passes 0-48. |
| A frame outside the ring, or a frame size that is not a multiple of 4 | Mixes or clears outside `pcmBuffer`. | Leaves the ring untouched, but still runs the callbacks and releases the lock. |
| `SoundMain` or `m4aSoundVSync` before `SoundInit` (`SOUND_INFO_PTR` is 0) | Reads `ident` from the BIOS at address 0, never `ID_NUMBER`, and returns. | Tests for NULL and returns. |
| A key above 255 after adding `keyM` | Passes the full register to `MidiKeyToFreq` and `MidiKeyToCgbFreq`. | Passes the `u8` the prototypes take. Unreachable: voicegroup keys are below 128 and `keyM` is at most 127. |
| A BIOS-region argument refused by `chk_adr_r2` | Loads the byte, then replaces it with 0. | Does not load it, which gives the same result. |
| The volume and pitch pass in `MPlayMain` | Continues to the next track on the flags of `adds r5, #0x50`, true only below 0x80000000. | Always continues: the result on the GBA, on any host. |

## Flagged 64-bit

This milestone is 32-bit. The engine and its unit tests also build and
pass with `BITS=64`, on data laid out as C structs. The game's assembled
sound data keeps the 4-byte layout, so on 64-bit the C structs and that
data disagree; the 64-bit milestone must reconcile them. It must revisit
the following.

- **Struct layout.** Every offset after the first pointer in each struct,
  and every struct size, depends on the pointer size. `m4a_layout.h`
  marks each one "flagged 64-bit". This includes the `TRACK_SIZE` 0x50 in
  `sound/music_player_table.inc` and the voicegroup stride of 12.
- **`Clear64byte`.** It clears 64 bytes, which assumes
  `sizeof(MusicPlayerInfo) == 64` and `offsetof(MusicPlayerTrack, cmdPtr) == 64`.
- **Key-split voices.** They store a 4-byte table address in
  `ToneData.attack..release` (`voice_keysplit`). `KeySplitTable` reads it
  as a 32-bit address (`m4a_note.c`).
- **Song data.** It holds 4-byte addresses (`GOTO`, `PATT`, `REPT`).
  `JumpToAddressAt` converts the 32-bit value through `uintptr_t`
  (`m4a_commands.c`). On a 64-bit host both hold only for data linked
  below 4 GiB, which the non-PIE build at 0x40000000 provides.
- **Compressed samples.** `currentPointer` holds a sample index for them,
  stored as `uintptr_t` (`m4a_render.c`).
- **Track tie-break.** Ties between equal priorities compare track
  addresses (`TrackAddress`, `m4a_note.c`).
- **BIOS-read guard.** `IsReadAllowed` (`chk_adr_r2`, `m4a_commands.c`)
  compares addresses as integers.
- **`ply_port`.** It forms the register address through `uintptr_t`, as
  does the `VCOUNT` read in `m4a_mixer.c`. Neither depends on pointer
  size; they are listed for completeness.

## Notes for integration

- **Leave `src/m4a_1.s` out of the native build** and link these files
  instead. `platform/Makefile` already puts `m4a/m4a_*.c` into
  `libplatform.a` and runs `test/test_m4a_*.c` with the other platform
  tests. The engine's calls into `src/m4a.c` and `src/m4a_tables.c` are
  resolved by the game link.
- **`SoundMainBTM`'s prototype.** `include/gba/m4a_internal.h` declares
  `void SoundMainBTM(void)`, but every caller passes the address to clear
  (`Clear64byte` calls it as `void (*)(void *)`). The definition takes the
  address, so `m4a_clear64.c` does not include the header. The mismatch
  is upstream's; no call site is affected.
- **Channel-stealing ties follow the linker.** Ties between channels of
  equal priority go to the track at the higher address, as on the GBA.
  To get the GBA's choices the native build must place the track arrays
  in the GBA's order: `gMPlayTrack_BGM`, `_SE1`, `_SE2`, `_SE3` from
  `sound/music_player_table.inc`, and `gPokemonCryTracks` from `src/m4a.c`.
- **DMA register writes are plain stores.** `m4aSoundVSync` writes
  `REG_DMA1CNT`/`REG_DMA2CNT` to restart the FIFO DMAs, and `ply_port`
  writes sound registers, as plain stores to the I/O page. A platform
  DMA model that acts on the `DMA_START_NOW` write would copy 4 words
  from the ring to the FIFO register. That is harmless, but it is not
  needed.
- **`src/m4a.c` concerns the native build will meet** (not changed here):
  - `SampleFreqSet` busy-waits for `VCOUNT` to leave and then reach 159.
    With `VCOUNT` reading 160 natively, it never returns unless the
    platform or a guarded change handles it.
  - `SoundInit` writes `(s32)soundInfo->pcmBuffer` to `REG_DMA1SAD`, a
    pointer cast to a 32-bit integer.
  - `MAX_LINES` and `NUM_MUSIC_PLAYERS` are the addresses of
    linker-defined symbols.
  - `MusicPlayerJumpTableCopy` contains `swi 0x2A`.
  - `TrkVolPitSet` shifts the signed `keyShift` left, which UBSan reports
    for negative values.

## Tests

```
make -C platform test [BITS=64]        # every platform test, the 131 m4a ones included
make -C platform/m4a test [BITS=64]    # the m4a tests alone, five binaries
make -C platform/m4a/difftest run      # needs arm-none-eabi-gcc (newlib) and qemu-arm
```

**Build flags.** The tests build as `platform/Makefile` builds every
test: `-m32` or `-m64`, `-std=gnu17 -Wall -Wextra -Wshadow
-Wstrict-prototypes -Wconversion -Werror`, under AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer, linked non-PIE with
`gba_regions.ld`. Expansion's headers are system headers, but they come
in with `-idirafter` instead of `-isystem`: `-isystem` searches them
before libc's, and expansion's `include/strings.h` then shadows the
`<strings.h>` that glibc's `<string.h>` includes.

**Fixtures.** The GBA regions (IWRAM with `SOUND_INFO_PTR`, the I/O
registers) exist at their GBA addresses because `gba_regions.ld` puts
them there, as for every platform test. The fixture maps one more page
below 0x02000000 for the BIOS-read guard, and the tests link
`src/m4a_tables.c` for the engine's tables. `src/m4a.c` cannot be
built standalone (it includes `global.h` and an ARM `swi`), so the tests
supply its functions: `MidiKeyToFreq` and `TrkVolPitSet` transcribed in
`test_m4a_c_reference.c`, the others recording their calls.

**Expected values.** Expected values come from the assembly's arithmetic
worked by hand (the numbers are in the comments), or from small
reference models written independently of the engine: a floor-division
interpolating player, a DPCM decoder written from the format, and 64-bit
products.

**The differential test** builds one driver twice:

- for ARM, with the original `src/m4a_1.s`, as a bare-metal newlib
  program run under `qemu-arm` with the IWRAM and I/O pages linked at
  their GBA addresses;
- natively (32-bit), with this C, under the sanitizers.

It runs 300 random mixer scenarios and 60 random sequencer scenarios.
The mixer scenarios cover every channel type, envelope phase, reverb,
frame position and step up to 2^32. The sequencer scenarios run two
chained players on random songs with notes, ties, rhythm and key-split
voices, PSG voices, LFOs, `PATT`, `REPT`, `GOTO`, `PORT` and running
status. The driver prints the engine's whole state after every frame.
The two outputs are identical over all 11400 frames. The scenarios stay
inside the behaviour both sides define, so they avoid the departures
listed above.
