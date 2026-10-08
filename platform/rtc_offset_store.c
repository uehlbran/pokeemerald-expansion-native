/*
 * rtc_offset_store.c: persistence of the RTC offset (see the header).
 *
 * Not hardware: the S-3511A kept its time in a battery-backed counter, and
 * this file is what stands in for that battery. It is written atomically (a
 * temporary file, fsync, rename) so a crash while saving cannot leave a
 * truncated or half-written offset, which would read back as corrupt.
 */
#include "rtc_offset_store.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEMP_SUFFIX ".tmp"
#define FILE_MODE   0644
/* "-" + 16 digits + newline fits in 32; the longest valid file is shorter. */
#define FILE_BUFFER_SIZE 32
#define DECIMAL_BASE     10

void rtc_report_call_failure(const char *call)
{
    int saved = errno;
    fprintf(stderr, "rtc: %s failed: %s\n", call, strerror(saved));
}

static void report_corrupt(const char *path)
{
    fprintf(stderr, "rtc: offset file %s is corrupt (expected one decimal integer and a newline)\n", path);
}

/* Strict parse of "[-]digits\n" with nothing after, within +-RTC_OFFSET_LIMIT. */
static int parse_offset(const char *text, size_t length, int64_t *offset)
{
    size_t i = 0;
    int negative = 0;
    int64_t magnitude = 0;

    if (i < length && text[i] == '-') {
        negative = 1;
        i++;
    }
    if (i >= length || text[i] < '0' || text[i] > '9')
        return -1;
    for (; i < length && text[i] >= '0' && text[i] <= '9'; i++) {
        magnitude = magnitude * DECIMAL_BASE + (text[i] - '0');
        if (magnitude > RTC_OFFSET_LIMIT)
            return -1;
    }
    if (i + 1 != length || text[i] != '\n')
        return -1;
    *offset = negative ? -magnitude : magnitude;
    return 0;
}

int rtc_offset_store_load(const char *path, int64_t *offset, int *found)
{
    char text[FILE_BUFFER_SIZE];
    ssize_t length;
    int64_t parsed;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        if (errno == ENOENT) {
            *offset = 0;
            *found = 0;
            return 0;
        }
        rtc_report_call_failure("open (offset file)");
        return -1;
    }
    do {
        length = read(fd, text, sizeof text);
    } while (length < 0 && errno == EINTR);
    if (length < 0) {
        rtc_report_call_failure("read (offset file)");
        close(fd);
        return -1;
    }
    if (close(fd) != 0) {
        rtc_report_call_failure("close (offset file)");
        return -1;
    }
    if (parse_offset(text, (size_t)length, &parsed) != 0) {
        report_corrupt(path);
        return -1;
    }
    *offset = parsed;
    *found = 1;
    return 0;
}

static int write_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t n = write(fd, data, length);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rtc_report_call_failure("write (offset file)");
            return -1;
        }
        data += n;
        length -= (size_t)n;
    }
    return 0;
}

static void discard_temp(const char *temp_path)
{
    if (unlink(temp_path) != 0)
        rtc_report_call_failure("unlink (temporary offset file)");
}

/* fsync of the directory holding path, so the rename itself is durable. */
static int sync_directory(const char *path)
{
    char dir[PATH_MAX];
    char *slash;
    int fd;

    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (slash == NULL)
        snprintf(dir, sizeof dir, ".");
    else if (slash == dir)
        slash[1] = '\0';
    else
        *slash = '\0';
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        rtc_report_call_failure("open (offset file directory)");
        return -1;
    }
    if (fsync(fd) != 0) {
        rtc_report_call_failure("fsync (offset file directory)");
        close(fd);
        return -1;
    }
    if (close(fd) != 0) {
        rtc_report_call_failure("close (offset file directory)");
        return -1;
    }
    return 0;
}

int rtc_offset_store_save(const char *path, int64_t offset)
{
    char temp_path[PATH_MAX + sizeof TEMP_SUFFIX];
    char text[FILE_BUFFER_SIZE];
    int text_length;
    int fd;

    if (strlen(path) >= PATH_MAX) {
        fprintf(stderr, "rtc: offset file path is longer than %d bytes\n", PATH_MAX - 1);
        return -1;
    }
    snprintf(temp_path, sizeof temp_path, "%s%s", path, TEMP_SUFFIX);
    text_length = snprintf(text, sizeof text, "%lld\n", (long long)offset);
    if (text_length < 0 || (size_t)text_length >= sizeof text) {
        fprintf(stderr, "rtc: offset does not fit the file format\n");
        return -1;
    }

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, FILE_MODE);
    if (fd < 0) {
        rtc_report_call_failure("open (temporary offset file)");
        return -1;
    }
    if (write_all(fd, text, (size_t)text_length) != 0) {
        close(fd);
        discard_temp(temp_path);
        return -1;
    }
    if (fsync(fd) != 0) {
        rtc_report_call_failure("fsync (temporary offset file)");
        close(fd);
        discard_temp(temp_path);
        return -1;
    }
    if (close(fd) != 0) {
        rtc_report_call_failure("close (temporary offset file)");
        discard_temp(temp_path);
        return -1;
    }
    if (rename(temp_path, path) != 0) {
        rtc_report_call_failure("rename (offset file)");
        discard_temp(temp_path);
        return -1;
    }
    return sync_directory(path);
}
