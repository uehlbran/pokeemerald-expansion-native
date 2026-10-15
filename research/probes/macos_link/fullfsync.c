/*
 * fullfsync.c: probe M2's save durability question (docs/C-RULES.md, "Save
 * writes are crash-safe"): on the runner's disk, how long fsync and
 * fcntl(F_FULLFSYNC) take for a 128 KiB file (the save's size), and whether
 * F_FULLFSYNC works on a directory's descriptor after a rename into it.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SAVE_BYTES (128 * 1024)

static double milliseconds_since(struct timespec start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start.tv_sec) * 1000.0 + (double)(now.tv_nsec - start.tv_nsec) / 1e6;
}

static void timed(const char *what, int descriptor, int full)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    const int status = full ? fcntl(descriptor, F_FULLFSYNC) : fsync(descriptor);
    const int error = errno;
    printf("%s: status %d (%s), %.2f ms\n", what, status, status == 0 ? "ok" : strerror(error),
           milliseconds_since(start));
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    char temporary[1024], final[1024];
    snprintf(temporary, sizeof temporary, "%s/probe.sav.tmp", argv[1]);
    snprintf(final, sizeof final, "%s/probe.sav", argv[1]);
    static unsigned char bytes[SAVE_BYTES];
    for (int round = 0; round < 3; round++) {
        memset(bytes, round + 1, sizeof bytes);
        const int file = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (file < 0 || write(file, bytes, sizeof bytes) != (ssize_t)sizeof bytes)
            return 1;
        timed("fsync of the 128 KiB file", file, 0);
        if (write(file, bytes, 1) != 1)
            return 1;
        timed("F_FULLFSYNC of the 128 KiB file", file, 1);
        close(file);
        if (rename(temporary, final) != 0)
            return 1;
        const int directory = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0)
            return 1;
        timed("fsync of the directory", directory, 0);
        timed("F_FULLFSYNC of the directory", directory, 1);
        close(directory);
    }
    return 0;
}
