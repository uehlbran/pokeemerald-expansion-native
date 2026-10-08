/* bios_control.c: stub (tests first). */
#include "bios.h"

int bios_init(void (*run_frame)(void)) { (void)run_frame; return BIOS_OK; }
void bios_shutdown(void) {}
void bios_set_reset_target(jmp_buf *target) { (void)target; }
int bios_soft_reset(uint32_t resetFlags) { (void)resetFlags; return BIOS_ERR_NO_RESET_TARGET; }
void SoftReset(uint32_t resetFlags) { (void)resetFlags; }
void SoftResetRom(uint32_t resetFlags) { (void)resetFlags; }
void SoftResetExram(uint32_t resetFlags) { (void)resetFlags; }
void RegisterRamReset(uint32_t resetFlags) { (void)resetFlags; }
void VBlankIntrWait(void) {}
void IntrWait(uint32_t discardOldFlags, uint32_t waitFlags) { (void)discardOldFlags; (void)waitFlags; }
void Halt(void) {}
void Stop(void) {}
int MultiBoot(struct MultiBootParam *mp) { (void)mp; return 0; }
