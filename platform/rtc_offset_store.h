/*
 * rtc_offset_store.h: the persisted RTC offset, a small text file.
 *
 * Internal to the RTC model (rtc.c). The file holds one decimal integer and a
 * newline: the number of seconds to add to UTC to get the time the game's
 * clock shows. It stands in for the battery-backed
 * counter on the cartridge, which kept the player's setting across power-off.
 */
#ifndef PLATFORM_RTC_OFFSET_STORE_H
#define PLATFORM_RTC_OFFSET_STORE_H

#include <stdint.h>

/* Largest offset magnitude accepted (2^50 s, about 35 million years): far
 * beyond any real setting, small enough that adding it to a clock reading
 * cannot overflow. A file holding more is corrupt. */
#define RTC_OFFSET_LIMIT ((int64_t)1 << 50)

/* Reads *offset from path and sets *found to 1. A missing file is success
 * with *found 0 and *offset 0; an unreadable or corrupt file, or a path too
 * long (this and the save check it against host_file.h's HOST_FILE_PATH_SIZE,
 * leaving room for the temporary file's name), is -1 (reported on stderr). */
int rtc_offset_store_load(const char *path, int64_t *offset, int *found);

/* rtc_offset_store_save's result when the new file is in place but the
 * directory sync after the replacement failed: the offset is what the file
 * says, though not yet known to survive a crash. */
#define RTC_OFFSET_STORE_REPLACED_UNSYNCED 1

/* Writes offset to path atomically: a temporary file next to it (path plus
 * ".tmp") is written, synced to the disk, then replaces path (host_file.h),
 * so a failure at any step leaves the previous file intact. That holds for
 * one process per offset path, as save_file.h says of the save: the temporary
 * name is fixed and nothing locks the path. Returns 0; or -1
 * with the previous file in place; or RTC_OFFSET_STORE_REPLACED_UNSYNCED.
 * Failures are reported on stderr. */
int rtc_offset_store_save(const char *path, int64_t offset);

/* Prints "rtc: <call> failed: <strerror(errno)>" to stderr. */
void rtc_report_call_failure(const char *call);

#endif /* PLATFORM_RTC_OFFSET_STORE_H */
