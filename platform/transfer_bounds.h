#ifndef TRANSFER_BOUNDS_H
#define TRANSFER_BOUNDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum transfer_access { TRANSFER_READ, TRANSFER_WRITE };
bool transfer_bounds_ok(uintptr_t address, size_t length, enum transfer_access access);
int transfer_bounds_init(void);
void transfer_bounds_shutdown(void);
#endif
