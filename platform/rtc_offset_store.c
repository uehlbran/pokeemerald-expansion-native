/*
 * rtc_offset_store.c: persistence of the RTC offset (see the header).
 *
 * Not hardware: the S-3511A kept its time in a battery-backed counter, and
 * this file is what stands in for that battery. It is written atomically (a
 * temporary file, synced to the disk, replacing the old one; host_file.h) so
 * a crash while saving cannot leave a truncated or half-written offset,
 * which would read back as corrupt.
 */
#include "rtc_offset_store.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host_file.h"

#define TEMP_SUFFIX ".tmp"
/* "-" + 16 digits + newline fits in 32; the longest valid file is shorter. */
#define FILE_BUFFER_SIZE 32
#define DECIMAL_BASE 10

void rtc_report_call_failure(const char *call)
{
    int saved = errno;
    (void)fprintf(stderr, "rtc: %s failed: %s\n", call, strerror(saved));
}

static void report_corrupt(const char *path)
{
    (void)fprintf(stderr, "rtc: offset file %s is corrupt (expected one decimal integer and a newline)\n", path);
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
    int fd = host_file_open(path, O_RDONLY);

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
        (void)close(fd); /* already failing */
        return -1;
    }
    if (close(fd) != 0) {
        rtc_report_call_failure("close (offset file)");
        return -1;
    }
    if ((size_t)length == sizeof text) { /* longer than any valid file: trailing data unseen */
        report_corrupt(path);
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

static void discard_temp(const char *temp_path)
{
    if (unlink(temp_path) != 0)
        rtc_report_call_failure("unlink (temporary offset file)");
}

int rtc_offset_store_save(const char *path, int64_t offset)
{
    char temp_path[PATH_MAX + sizeof TEMP_SUFFIX];
    char text[FILE_BUFFER_SIZE];
    int text_length;
    int fd;

    if (strlen(path) >= PATH_MAX) {
        (void)fprintf(stderr, "rtc: offset file path is longer than %d bytes\n", PATH_MAX - 1);
        return -1;
    }
    (void)snprintf(temp_path, sizeof temp_path, "%s%s", path, TEMP_SUFFIX); /* sized for it */
    text_length = snprintf(text, sizeof text, "%lld\n", (long long)offset);
    if (text_length < 0 || (size_t)text_length >= sizeof text) {
        (void)fprintf(stderr, "rtc: offset does not fit the file format\n");
        return -1;
    }

    fd = host_file_open(temp_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        rtc_report_call_failure("open (temporary offset file)");
        return -1;
    }
    if (host_file_write_all(fd, text, (size_t)text_length) != 0) {
        rtc_report_call_failure("write (offset file)");
        (void)close(fd); /* already failing */
        discard_temp(temp_path);
        return -1;
    }
    if (host_file_sync(fd) != 0) {
        rtc_report_call_failure("fsync (temporary offset file)");
        (void)close(fd); /* already failing */
        discard_temp(temp_path);
        return -1;
    }
    if (close(fd) != 0) {
        rtc_report_call_failure("close (temporary offset file)");
        discard_temp(temp_path);
        return -1;
    }
    if (host_file_replace(temp_path, path) != 0) {
        rtc_report_call_failure("rename (offset file)");
        discard_temp(temp_path);
        return -1;
    }
    /* The directory holding path, so the replacement itself is durable. */
    if (host_file_sync_directory_of(path) != 0) {
        rtc_report_call_failure("fsync (offset file directory)");
        return -1;
    }
    return 0;
}
