/*
 * signals.c: probe M2's divide error (research/m8-inventory.md, section 3).
 * What si_code macOS x86_64 gives SIGFPE for an integer division by zero and
 * for INT_MIN / -1 at 32 and 64 bits (x86 raises #DE for both), and for a
 * SIGFPE that raise() and kill() send; and the registers the handler sees in
 * uc_mcontext->__ss. Each division is one idiv or div instruction in inline
 * assembly, so that no compiler folds it; the handler records what it saw and
 * leaves by siglongjmp.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

static sigjmp_buf g_back;
static volatile sig_atomic_t g_code;
static volatile uint64_t g_rip, g_rax, g_rdx, g_rcx;
static volatile uintptr_t g_addr;

static void on_fpe(int signal_number, siginfo_t *info, void *context)
{
    (void)signal_number;
    const ucontext_t *uc = context;
    g_code = info->si_code;
    g_addr = (uintptr_t)info->si_addr;
    g_rip = uc->uc_mcontext->__ss.__rip;
    g_rax = uc->uc_mcontext->__ss.__rax;
    g_rdx = uc->uc_mcontext->__ss.__rdx;
    g_rcx = uc->uc_mcontext->__ss.__rcx;
    siglongjmp(g_back, 1);
}

static __attribute__((noinline)) int32_t idiv32(int32_t n, int32_t d)
{
    int32_t q;
    __asm__ volatile("cltd\n\tidivl %2" : "=a"(q) : "a"(n), "c"(d) : "edx", "cc");
    return q;
}

static __attribute__((noinline)) uint32_t div32(uint32_t n, uint32_t d)
{
    uint32_t q;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %2" : "=a"(q) : "a"(n), "c"(d) : "edx", "cc");
    return q;
}

static __attribute__((noinline)) int64_t idiv64(int64_t n, int64_t d)
{
    int64_t q;
    __asm__ volatile("cqto\n\tidivq %2" : "=a"(q) : "a"(n), "c"(d) : "rdx", "cc");
    return q;
}

static void report(const char *what)
{
    printf("%s: si_code %d (FPE_INTDIV %d, FPE_INTOVF %d, FPE_NOOP %d, SI_USER %d), si_addr 0x%llx, "
           "rip 0x%llx, rax 0x%llx, rdx 0x%llx, rcx 0x%llx\n",
           what, (int)g_code, FPE_INTDIV, FPE_INTOVF, 0, SI_USER, (unsigned long long)g_addr,
           (unsigned long long)g_rip, (unsigned long long)g_rax, (unsigned long long)g_rdx,
           (unsigned long long)g_rcx);
}

#define TRY(what, statement)                                                                                           \
    do {                                                                                                               \
        g_code = -1;                                                                                                   \
        if (sigsetjmp(g_back, 1) == 0) {                                                                               \
            statement;                                                                                                 \
            printf("%s: no signal\n", what);                                                                           \
        } else {                                                                                                       \
            report(what);                                                                                              \
        }                                                                                                              \
    } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_fpe;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGFPE, &action, NULL) != 0) {
        perror("sigaction");
        return 1;
    }
    printf("idiv32 at %p, div32 at %p, idiv64 at %p\n", (void *)idiv32, (void *)div32, (void *)idiv64);
    TRY("32-bit 7 / 0 (idivl)", (void)idiv32(7, 0));
    TRY("32-bit INT_MIN / -1 (idivl)", (void)idiv32(INT32_MIN, -1));
    TRY("32-bit unsigned 7 / 0 (divl)", (void)div32(7, 0));
    TRY("64-bit 7 / 0 (idivq)", (void)idiv64(7, 0));
    TRY("64-bit INT64_MIN / -1 (idivq)", (void)idiv64(INT64_MIN, -1));
    TRY("raise(SIGFPE)", (void)raise(SIGFPE));
    TRY("kill(getpid(), SIGFPE)", (void)kill(getpid(), SIGFPE));
    return 0;
}
