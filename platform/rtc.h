/*
 * rtc.h: the S-3511A real-time clock on the Game Pak, as the game sees it.
 *
 * Public interface of rtc.c; see the header comment there for what hardware
 * this stands in for and for the design decisions. Every function returns 0
 * on success and -1 on failure (not initialised, invalid argument, host or
 * file error); failures of C library calls are also reported on stderr,
 * naming the module and the call. Single-threaded: call from the main thread.
 *
 * All register values are BCD exactly as the chip delivers them
 * (GBATEK "GBA Cart Real-Time Clock (RTC)",
 * https://problemkaputt.de/gbatek.htm#gbacartrealtimeclockrtc).
 */
#ifndef PLATFORM_RTC_H
#define PLATFORM_RTC_H

#include <stdint.h>

/* Status register bits as expansion's struct SiiRtcInfo.status carries them
 * (include/constants/siirtc.h SIIRTCINFO_*), which is not the chip's own bit
 * order (src/siirtc.c converts between the two on the GPIO path). */
#define RTC_STATUS_INTFE 0x01u  /* frequency interrupt enable, read/write */
#define RTC_STATUS_INTME 0x02u  /* per-minute interrupt enable, read/write */
#define RTC_STATUS_INTAE 0x04u  /* alarm interrupt enable, read/write */
#define RTC_STATUS_24HOUR 0x40u /* 0: 12-hour mode, 1: 24-hour mode, read/write */
#define RTC_STATUS_POWER 0x80u  /* power-off flag, read only, cleared by a read */

/* Hour register, GBA cartridge layout (GBATEK: the GBA moved the AM/PM flag
 * from the NDS's bit 6 to bit 7). Bits 0-5 hold the hour in BCD (bits 4-5
 * the tens): 00h..23h in 24-hour mode, 00h..11h in 12-hour mode. Bit 7 is
 * the PM flag. In 24-hour mode the flag is read only and set for 12h..23h
 * (src/siirtc.c masks it off, & 0x7F, after a read); in 12-hour mode it is
 * read/write. Bit 6 is always zero. */
#define RTC_HOUR_PM 0x80u
#define RTC_HOUR_MASK 0x3Fu

/* One field per register of the datetime command, in the order the chip
 * transfers them; same order as the first seven bytes of struct SiiRtcInfo. */
struct rtc_datetime {
    uint8_t year_bcd;        /* 00h..99h = 2000..2099 */
    uint8_t month_bcd;       /* 01h..12h */
    uint8_t day_bcd;         /* 01h..28h/29h/30h/31h */
    uint8_t day_of_week_bcd; /* 00h..06h, 0 = Sunday; computed, see rtc.c */
    uint8_t hour_bcd;        /* see RTC_HOUR_* above */
    uint8_t minute_bcd;      /* 00h..59h */
    uint8_t second_bcd;      /* 00h..59h (bit 7 is the chip's test-mode flag, never set) */
};

/* Seconds since the Unix epoch, UTC. Injectable so tests control time. */
typedef int64_t (*rtc_time_source)(void);

/* Starts the clock. source NULL means the system clock. offset_file_path NULL
 * means no persistence; otherwise it names the file holding the offset (one
 * decimal integer and a newline): a missing file is offset zero, a corrupt
 * one is an error. Calling it again while initialised replaces the state. */
int rtc_init(rtc_time_source source, const char *offset_file_path);

/* As rtc_init, but a first run (no offset file, or none given) starts with
 * offset zero, so the chip reads the source's UTC: independent of the host's
 * time zone. For a deterministic source (--rtc-fixed). */
int rtc_init_utc(rtc_time_source source, const char *offset_file_path);

/* Releases the module. Safe without a prior successful rtc_init. */
void rtc_shutdown(void);

/* Status byte in RTC_STATUS_* layout. Reading clears the power-off flag. */
int rtc_get_status(uint8_t *status);
/* Writes the read/write bits; RTC_STATUS_POWER is ignored (read only); any
 * other bit is an error. */
int rtc_set_status(uint8_t status);

int rtc_get_datetime(struct rtc_datetime *out);
/* Rejects invalid BCD and out-of-range fields without changing anything.
 * day_of_week_bcd is validated (00h..06h) but the stored value is computed
 * from the date. Persists the offset; on failure the offset on disk and in
 * the chip are unchanged. (A failed directory sync after the file was
 * replaced is not a failure here: the new offset is on disk and in the chip,
 * and the failure is reported on stderr.) */
int rtc_set_datetime(const struct rtc_datetime *in);

/* hour, minute, second only; the other fields are left untouched in out. */
int rtc_get_time(struct rtc_datetime *out);
int rtc_set_time(const struct rtc_datetime *in);

/* The chip's force-reset: status becomes 00h (12-hour mode, expansion's
 * SiiRtcReset follows with SiiRtcSetStatus(24-hour)), the time becomes
 * 2000-01-01 00:00:00. Persists the offset. */
int rtc_reset(void);

/* Alarm: hour and minute BCD are validated and stored; no alarm ever fires
 * and no interrupt is raised, since expansion never calls it (SiiRtcSetAlarm
 * is static UNUSED in src/siirtc.c). Only the hour and minute fields are
 * used; the PM flag is derived from the hour, as the original code does. */
int rtc_set_alarm(const struct rtc_datetime *in);
/* Reads the stored alarm back (hour with its PM flag, minute); other fields 0. */
int rtc_get_alarm(struct rtc_datetime *out);

#endif /* PLATFORM_RTC_H */
