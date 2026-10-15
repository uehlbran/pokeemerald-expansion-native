/*
 * divtest.c: every row of libgcc_division.txt divided with C's / and % on
 * operands the compiler cannot see (volatile), compiled through divfilter.py,
 * so each division is a pen_div.c call. No SIGFPE handler is installed: a
 * hardware division by zero would kill the process on x86_64.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct row {
    const char *kind;
    uint64_t n, d, q, r;
};

static const struct row rows[] = {
#include "cases.h"
};

int main(void)
{
    unsigned same = 0, diff = 0;
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const struct row *t = &rows[i];
        uint64_t q, r;
        if (strcmp(t->kind, "u32") == 0) {
            volatile uint32_t n = (uint32_t)t->n, d = (uint32_t)t->d;
            q = n / d;
            r = n % d;
        } else if (strcmp(t->kind, "s32") == 0) {
            volatile int32_t n = (int32_t)(uint32_t)t->n, d = (int32_t)(uint32_t)t->d;
            q = (uint32_t)(n / d);
            r = (uint32_t)(n % d);
        } else if (strcmp(t->kind, "u64") == 0) {
            volatile uint64_t n = t->n, d = t->d;
            q = n / d;
            r = n % d;
        } else {
            volatile int64_t n = (int64_t)t->n, d = (int64_t)t->d;
            q = (uint64_t)(n / d);
            r = (uint64_t)(n % d);
        }
        if (q == t->q && r == t->r) {
            same++;
        } else {
            diff++;
            printf("DIFF %s %#" PRIx64 " / %#" PRIx64 ": got %#" PRIx64 " %#" PRIx64 ", libgcc %#" PRIx64 " %#" PRIx64 "\n",
                   t->kind, t->n, t->d, q, r, t->q, t->r);
        }
    }
    printf("divtest: rows=%u same=%u diff=%u\n", same + diff, same, diff);
    return diff != 0;
}
