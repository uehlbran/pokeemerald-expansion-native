/* ucontext_plain.c: probe M2: <ucontext.h> without _XOPEN_SOURCE, as
 * platform/host_linux.c includes it. */
#include <ucontext.h>
int main(void)
{
    return 0;
}
