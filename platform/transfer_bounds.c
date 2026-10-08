#include "transfer_bounds.h"
bool transfer_bounds_ok(uintptr_t address, size_t length, enum transfer_access access) { (void)address; (void)length; (void)access; return true; }
int transfer_bounds_init(void) { return 0; }
void transfer_bounds_shutdown(void) {}
