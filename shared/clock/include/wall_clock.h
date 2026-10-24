/**
 * @file
 * @brief The panel's wall clock: whether it holds a real date, whether a
 *        file's stamp says anything, the time zones SETUP offers, and the
 *        one NTP (Network Time Protocol) request a join makes.
 *
 * The panel board (Waveshare ESP32-P4-WIFI6-Touch-LCD-7B) carries no RTC
 * (real-time clock) chip.  The ESP32-P4's own RTC timer keeps the system
 * time across a software restart; a power-on starts it at 1970-01-01
 * 00:00:00 UTC (coordinated universal time).  An NTP answer on the home
 * network sets it.  Until then the clock is uptime since 1970, and FatFs
 * clamps such a stamp to 1980 (ESP-IDF's get_fattime()).
 *
 * Nothing here reads a clock, a network or a card: every time is the
 * caller's, so the host suite holds every rule.  wall_local_s() reads the
 * C library's time zone, which the panel sets from SETUP.  The panel's end is
 * firmware/panel/components/wifi_access/wifi_ntp.c and
 * firmware/panel/main/panel_settings.c.
 *
 * C and the C library's localtime_r(), no ESP-IDF (Espressif
 * Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WALL_CLOCK_H
#define RCBENCH_WALL_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The first second the clock is taken to hold a real date:
 *        2026-01-01 00:00:00 UTC, in seconds since 1970-01-01 UTC.  An
 *        unset clock counts uptime from 1970 and stays below it for 56
 *        years of uptime.
 */
#define WALL_VALID_FLOOR_S  1767225600LL

/**
 * @brief The first file stamp taken as known: 1981-01-01 12:00:00 UTC.
 *        FAT (File Allocation Table) dates start in 1980, and FatFs stamps
 *        any time before 1980 as 1980.  Read back through mktime() in a
 *        zone from UTC-12 to UTC+14, every 1980 stamp is below this and
 *        every stamp from 1981-01-03 on is above it.
 */
#define WALL_STAMP_FLOOR_S  347198400LL

/** @brief How long a join waits for an NTP answer: 30000ms. */
#define WALL_NTP_TIMEOUT_MS 30000u

/** @brief The NTP server a join asks. */
#define WALL_NTP_SERVER     "pool.ntp.org"

/** @brief The time zone a panel starts with: Berlin (CET/CEST). */
#define WALL_ZONE_DEFAULT   2

/** @brief The number of time zones SETUP offers. */
#define WALL_ZONE_COUNT     10

/**
 * @brief Whether a clock reading holds a real date.
 * @param unix_s Seconds since 1970-01-01 00:00:00 UTC.
 * @return True from WALL_VALID_FLOOR_S on.
 */
bool wall_valid(int64_t unix_s);

/**
 * @brief Whether a file's stamp says when it was written: false for a
 *        stamp FatFs made from an unset clock (any time in 1980) and for
 *        none at all.
 * @param mtime The stamp as mktime() reads FAT's local date and time, or
 *              -1 for no stamp.
 * @return True from WALL_STAMP_FLOOR_S on.
 */
bool wall_stamp_known(int64_t mtime);

/**
 * @brief Time zone @p index as a POSIX TZ string, for setenv("TZ") and
 *        tzset().  The order is the SETUP row's (UI_OPTIONS(time_zone) in
 *        ui_text.def): UTC, London, Berlin, Helsinki, New York, Chicago,
 *        Denver, Los Angeles, Tokyo, Sydney.
 * @param index 0 to WALL_ZONE_COUNT - 1.
 * @return The TZ string; Berlin's for an index out of range.
 */
const char *wall_zone_posix(int index);

/**
 * @brief A time moved by the zone in force (TZ, tzset()) at that time, so
 *        a UTC civil reading of the result shows the local date and time:
 *        what FILES INFO, the Wi-Fi page and the ZIP file write.  The file
 *        stamps FatFs writes are local; mktime() turns them into true
 *        seconds since 1970, and this turns them back.
 * @param unix_s Seconds since 1970-01-01 00:00:00 UTC; a negative time
 *               (no stamp) is returned as it is.
 * @return The local civil time as seconds since 1970 read as UTC; @p
 *         unix_s itself when the C library cannot convert it.
 */
int64_t wall_local_s(int64_t unix_s);

/**
 * @brief A UTC time as "YYYY-MM-DD HH:MM:SS UTC".
 * @param out The buffer.
 * @param n   Its size in bytes; 24 holds every year up to 9999.
 * @param unix_s Seconds since 1970-01-01 00:00:00 UTC, 0 or more; a
 *               negative time is written as 1970-01-01 00:00:00.
 * @return @p out, or NULL when @p out is NULL or @p n is 0.
 */
char *wall_utc_text(char *out, size_t n, int64_t unix_s);

/**
 * @brief The console line of an NTP answer: the server, the time set and
 *        how far it moved the clock, e.g.
 *        "ntp: pool.ntp.org set 2026-10-24 14:03:12 UTC, offset
 *        +1792739352.345s from the old clock".
 * @param out    The buffer.
 * @param n      Its size in bytes; 128 holds a 32-character server name
 *               and a 12-digit offset.
 * @param server The server asked.
 * @param new_us The time the answer set, microseconds since 1970 UTC.
 * @param old_us What the clock read at that moment before the answer.
 * @return @p out, or NULL when @p out is NULL or @p n is 0.
 */
char *wall_ntp_line(char *out, size_t n, const char *server, int64_t new_us,
                    int64_t old_us);

/** @brief Where a join's NTP request stands. */
typedef enum {
    WALL_NTP_IDLE = 0,  /**< not joined, or joined and not yet asked   */
    WALL_NTP_ASKING,    /**< the request runs                           */
    WALL_NTP_DONE,      /**< answered or given up; nothing until a new
                             join                                       */
} wall_ntp_state_t;

/** @brief What wall_ntp_step() asks of the caller. */
typedef enum {
    WALL_NTP_NONE = 0,  /**< nothing                                    */
    WALL_NTP_START,     /**< start the request                          */
    WALL_NTP_STOP,      /**< the join ended unanswered: stop it         */
    WALL_NTP_SET,       /**< answered: log the line, stop the request   */
    WALL_NTP_GAVE_UP,   /**< WALL_NTP_TIMEOUT_MS unanswered: log, stop  */
} wall_ntp_do_t;

/** @brief One join's request: at most one per join. */
typedef struct {
    wall_ntp_state_t state;     /**< where it stands                    */
    uint32_t         start_ms;  /**< the millisecond clock at START     */
} wall_ntp_t;

/**
 * @brief Move the request on.  Joined: one START, then SET on an answer
 *        or GAVE_UP after WALL_NTP_TIMEOUT_MS.  A join that ends while it
 *        asks: STOP.  The next join asks again.
 * @param n        The request; zeroed is IDLE.
 * @param joined   The station holds an address on the home network.
 * @param answered An answer has set the clock since START.
 * @param now_ms   A millisecond clock; its wrap is taken.
 * @return What the caller does now.
 */
wall_ntp_do_t wall_ntp_step(wall_ntp_t *n, bool joined, bool answered,
                            uint32_t now_ms);

/**
 * @brief Whether the radio stays up past its session's end for this
 *        request: the session lingers (wa_lingers() in wa_session.h) and
 *        the request asks, less than WALL_NTP_TIMEOUT_MS after its START.
 *        An answer at 29.9s ends the hold with it; the time-out at 30s ends
 *        it unanswered.
 * @param n       The request.
 * @param lingers The session ended and lets the radio stay up.
 * @param now_ms  The millisecond clock of wall_ntp_step(); its wrap is
 *                taken.
 * @return True while the radio waits for this request.
 */
bool wall_ntp_holds(const wall_ntp_t *n, bool lingers, uint32_t now_ms);

/**
 * @brief wall_ntp_step() across a session's end that lingers: a request
 *        that asks counts the lingering station as joined, so it ends with
 *        its answer (SET) or after WALL_NTP_TIMEOUT_MS (GAVE_UP), not with
 *        the session (STOP).  A lingering station starts no request.
 * @param n        The request; zeroed is IDLE.
 * @param joined   The session holds an address on the home network.
 * @param lingers  The session ended and lets the radio stay up.
 * @param answered An answer has set the clock since START.
 * @param now_ms   A millisecond clock; its wrap is taken.
 * @return What the caller does now.
 */
wall_ntp_do_t wall_ntp_hold_step(wall_ntp_t *n, bool joined, bool lingers,
                                 bool answered, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WALL_CLOCK_H */
