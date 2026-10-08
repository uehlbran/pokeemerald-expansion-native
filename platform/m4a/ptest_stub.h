/*
 * Minimal private stand-in for platform/test/ptest.h, which another worker
 * is writing in parallel. It implements exactly the agreed API so the m4a
 * tests build before that header lands; the orchestrator deletes this file
 * at merge and the tests switch to "../test/ptest.h".
 *
 * API: PTEST(name) { ... } registers a test; PT_ASSERT, PT_ASSERT_EQ_U32,
 * PT_ASSERT_EQ_INT, PT_ASSERT_EQ_PTR, PT_ASSERT_EQ_STR, PT_ASSERT_MEM_EQ and
 * PT_FAIL report file, line and values and end the current test;
 * PTEST_MAIN() defines main, runs every test and returns non-zero if any
 * failed.
 */
#ifndef GUARD_PLATFORM_M4A_PTEST_STUB_H
#define GUARD_PLATFORM_M4A_PTEST_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*PtestBody)(void);

struct PtestCase
{
    const char *name;
    PtestBody body;
};

/* Upper bound on registered tests; registering more is reported as a failure. */
#define PTEST_MAX_CASES 512

static struct PtestCase sPtestCases[PTEST_MAX_CASES];
static size_t sPtestCount;
static int sPtestRegistrationOverflow;
static int sPtestCurrentFailed;

static inline void PtestRegister(const char *name, PtestBody body)
{
    if (sPtestCount >= PTEST_MAX_CASES)
    {
        sPtestRegistrationOverflow = 1;
        return;
    }
    sPtestCases[sPtestCount].name = name;
    sPtestCases[sPtestCount].body = body;
    sPtestCount++;
}

#define PTEST(name)                                                        \
    static void ptest_body_##name(void);                                   \
    __attribute__((constructor)) static void ptest_register_##name(void)   \
    {                                                                      \
        PtestRegister(#name, ptest_body_##name);                           \
    }                                                                      \
    static void ptest_body_##name(void)

#define PT_FAIL(message)                                                   \
    do {                                                                   \
        fprintf(stderr, "%s:%d: failure: %s\n", __FILE__, __LINE__,        \
                (message));                                                \
        sPtestCurrentFailed = 1;                                           \
        return;                                                            \
    } while (0)

#define PT_ASSERT(cond)                                                    \
    do {                                                                   \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,     \
                    __LINE__, #cond);                                      \
            sPtestCurrentFailed = 1;                                       \
            return;                                                        \
        }                                                                  \
    } while (0)

#define PT_ASSERT_EQ_U32(actual, expected)                                 \
    do {                                                                   \
        uint32_t ptActual_ = (uint32_t)(actual);                           \
        uint32_t ptExpected_ = (uint32_t)(expected);                       \
        if (ptActual_ != ptExpected_)                                      \
        {                                                                  \
            fprintf(stderr, "%s:%d: %s == %s failed: 0x%08x != 0x%08x\n",  \
                    __FILE__, __LINE__, #actual, #expected,                \
                    (unsigned)ptActual_, (unsigned)ptExpected_);           \
            sPtestCurrentFailed = 1;                                       \
            return;                                                        \
        }                                                                  \
    } while (0)

#define PT_ASSERT_EQ_INT(actual, expected)                                 \
    do {                                                                   \
        long long ptActual_ = (long long)(actual);                         \
        long long ptExpected_ = (long long)(expected);                     \
        if (ptActual_ != ptExpected_)                                      \
        {                                                                  \
            fprintf(stderr, "%s:%d: %s == %s failed: %lld != %lld\n",      \
                    __FILE__, __LINE__, #actual, #expected,                \
                    ptActual_, ptExpected_);                               \
            sPtestCurrentFailed = 1;                                       \
            return;                                                        \
        }                                                                  \
    } while (0)

#define PT_ASSERT_EQ_PTR(actual, expected)                                 \
    do {                                                                   \
        const void *ptActual_ = (const void *)(actual);                    \
        const void *ptExpected_ = (const void *)(expected);                \
        if (ptActual_ != ptExpected_)                                      \
        {                                                                  \
            fprintf(stderr, "%s:%d: %s == %s failed: %p != %p\n",          \
                    __FILE__, __LINE__, #actual, #expected,                \
                    ptActual_, ptExpected_);                               \
            sPtestCurrentFailed = 1;                                       \
            return;                                                        \
        }                                                                  \
    } while (0)

#define PT_ASSERT_EQ_STR(actual, expected)                                 \
    do {                                                                   \
        const char *ptActual_ = (actual);                                  \
        const char *ptExpected_ = (expected);                              \
        if (ptActual_ == NULL || ptExpected_ == NULL                       \
            || strcmp(ptActual_, ptExpected_) != 0)                        \
        {                                                                  \
            fprintf(stderr, "%s:%d: %s == %s failed: \"%s\" != \"%s\"\n",  \
                    __FILE__, __LINE__, #actual, #expected,                \
                    ptActual_ ? ptActual_ : "(null)",                      \
                    ptExpected_ ? ptExpected_ : "(null)");                 \
            sPtestCurrentFailed = 1;                                       \
            return;                                                        \
        }                                                                  \
    } while (0)

#define PT_ASSERT_MEM_EQ(actual, expected, nbytes)                         \
    do {                                                                   \
        const unsigned char *ptActual_ = (const unsigned char *)(actual);  \
        const unsigned char *ptExpected_ =                                 \
            (const unsigned char *)(expected);                             \
        size_t ptCount_ = (size_t)(nbytes);                                \
        for (size_t ptI_ = 0; ptI_ < ptCount_; ptI_++)                     \
        {                                                                  \
            if (ptActual_[ptI_] != ptExpected_[ptI_])                      \
            {                                                              \
                fprintf(stderr, "%s:%d: %s == %s failed at byte %zu: "     \
                        "0x%02x != 0x%02x\n", __FILE__, __LINE__,          \
                        #actual, #expected, ptI_,                          \
                        (unsigned)ptActual_[ptI_],                         \
                        (unsigned)ptExpected_[ptI_]);                      \
                sPtestCurrentFailed = 1;                                   \
                return;                                                    \
            }                                                              \
        }                                                                  \
    } while (0)

static inline int PtestRunAll(void)
{
    size_t failures = 0;

    if (sPtestRegistrationOverflow)
    {
        fprintf(stderr, "ptest: more than %d tests registered\n", PTEST_MAX_CASES);
        return 1;
    }
    for (size_t i = 0; i < sPtestCount; i++)
    {
        sPtestCurrentFailed = 0;
        sPtestCases[i].body();
        printf("%s %s\n", sPtestCurrentFailed ? "FAIL" : "ok  ", sPtestCases[i].name);
        if (sPtestCurrentFailed)
            failures++;
    }
    printf("%zu tests, %zu failed\n", sPtestCount, failures);
    return failures == 0 && sPtestCount > 0 ? 0 : 1;
}

#define PTEST_MAIN()            \
    int main(void)              \
    {                           \
        return PtestRunAll();   \
    }

#endif /* GUARD_PLATFORM_M4A_PTEST_STUB_H */
