/* bios_arith.c: stub (tests first). */
#include "bios.h"

struct bios_div_result bios_div(int32_t number, int32_t denom)
{
    (void)number;
    (void)denom;
    struct bios_div_result r = { 0, 0, 0 };
    return r;
}
int32_t Div(int32_t num, int32_t denom) { (void)num; (void)denom; return 0; }
int32_t Mod(int32_t num, int32_t denom) { (void)num; (void)denom; return 0; }
int32_t DivArm(int32_t denom, int32_t num) { (void)num; (void)denom; return 0; }
int32_t ModArm(int32_t denom, int32_t num) { (void)num; (void)denom; return 0; }
uint16_t Sqrt(uint32_t num) { (void)num; return 0; }
int16_t ArcTan(int16_t tan) { (void)tan; return 0; }
uint16_t ArcTan2(int16_t x, int16_t y) { (void)x; (void)y; return 0; }
