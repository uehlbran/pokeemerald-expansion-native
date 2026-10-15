/* ucontext_xopen.c: probe M2's evidence on ucontext under the platform's flags
 * (-std=gnu17 -Wall -Wextra -Werror): with _XOPEN_SOURCE, which macOS's
 * <ucontext.h> requires, a switch to a stack of the caller's and back. */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <ucontext.h>

static ucontext_t caller, callee;
static _Alignas(16) unsigned char stack[64 * 1024];

static void entry(void)
{
    puts("on the new stack");
}

int main(void)
{
    if (getcontext(&callee) != 0)
        return 1;
    callee.uc_stack.ss_sp = stack;
    callee.uc_stack.ss_size = sizeof stack;
    callee.uc_link = &caller;
    makecontext(&callee, entry, 0);
    if (swapcontext(&caller, &callee) != 0)
        return 1;
    puts("back");
    return 0;
}
