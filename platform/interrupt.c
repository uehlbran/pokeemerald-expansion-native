/*
 * interrupt.c: not implemented yet; the tests in test/test_interrupt.c are
 * written first and fail against this empty module.
 */
#include "interrupt.h"

int interrupt_init(interrupt_handler const *table, size_t count)
{
    (void)table;
    (void)count;
    return INTERRUPT_OK;
}

void interrupt_shutdown(void)
{
}

void interrupt_raise(enum gba_irq source)
{
    (void)source;
}

unsigned interrupt_dispatch(void)
{
    return 0;
}

bool interrupt_halted(void)
{
    return false;
}
