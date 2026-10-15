/* exepath.c: probe M2: what _NSGetExecutablePath gives for each way build.sh
 * starts this program (a relative path, a symbolic link, another working
 * directory), and what realpath makes of it. */
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char path[PATH_MAX];
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) {
        printf("_NSGetExecutablePath: needs %u bytes\n", size);
        return 1;
    }
    char resolved[PATH_MAX];
    printf("_NSGetExecutablePath: %s\nrealpath: %s\n", path, realpath(path, resolved) ? resolved : "(failed)");
    return 0;
}
