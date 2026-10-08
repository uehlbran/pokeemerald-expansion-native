/* stub: filled in after the tests are committed */
#include "timer.h"

int timer_init(void (*raise)(enum gba_irq)) { (void)raise; return TIMER_OK; }
void timer_shutdown(void) {}
void timer_advance(uint32_t cycles) { (void)cycles; }
