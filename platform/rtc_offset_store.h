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
 * with *found 0 and *offset 0; an unreadable or corrupt file is -1 (reported
 * on stderr). */
int rtc_offset_store_load(const char *path, int64_t *offset, int *found);

/* Writes offset to path atomically: a temporary file next to it (path plus
 * ".tmp") is written, synced to the disk, then replaces path (host_file.h),
 * so a failure at any step leaves the previous file intact. The directory is
 * synced after the replacement; if that fails the new file is already in
 * place but -1 is returned. -1 on failure (reported on stderr). */
int rtc_offset_store_save(const char *path, int64_t offset);

/* Prints "rtc: <call> failed: <strerror(errno)>" to stderr. */
void rtc_report_call_failure(const char *call);

#endif /* PLATFORM_RTC_OFFSET_STORE_H */
