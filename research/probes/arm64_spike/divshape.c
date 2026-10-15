/* divshape.c: the code clang emits for / and % with a variable divisor on
   AArch64, plain and with the divide-by-zero check trapping (option (c) of
   research/m8-inventory.md 3). Compiled only, never run. */
int sdiv32(int a, int b) { return a / b; }
int smod32(int a, int b) { return a % b; }
unsigned udivmod32(unsigned a, unsigned b, unsigned *r) { *r = a % b; return a / b; }
int two(int a, int b, int c) { return a / b + a / c; }
