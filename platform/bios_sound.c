/* bios_sound.c: stub (tests first). */
#include "bios.h"

void SoundBiasChange(uint32_t level) { (void)level; }
void SoundBiasReset(void) {}
void SoundBiasSet(void) {}
uint32_t MidiKey2Freq(const struct WaveData *wa, uint8_t mk, uint8_t fp) { (void)wa; (void)mk; (void)fp; return 0; }
