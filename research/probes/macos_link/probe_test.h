/* probe_test.h: the test table's entry, 32 bytes at 8-byte alignment, as
 * expansion's struct Test holds pointers and integers (include/test/test.h). */
#ifndef PROBE_TEST_H
#define PROBE_TEST_H
#include <stdint.h>
struct ProbeTest {
    const char *name;
    const char *filename;
    void (*run)(void);
    uint64_t number;
};
#endif
