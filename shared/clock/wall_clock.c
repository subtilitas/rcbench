/**
 * @file
 * @brief The panel's wall clock rules.  See wall_clock.h.
 *
 * SPDX-License-Identifier: MIT
 */

/** @brief localtime_r() is POSIX: newlib and glibc declare it with this,
 *         and its name is the standard's, not ours. */
/* NOLINTNEXTLINE(bugprone-reserved-identifier) */
#define _POSIX_C_SOURCE 200809L

#include "wall_clock.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief POSIX TZ strings, the order of UI_OPTIONS(time_zone).  Daylight saving:
 * the EU (European Union) from the last Sunday of March 01:00 UTC to the
 * last Sunday of October 01:00 UTC; the US (United States) from the second
 * Sunday of March 02:00 local to the first Sunday of November 02:00 local;
 * Sydney from the first Sunday of October 02:00 to the first Sunday of
 * April 03:00 local.  Tokyo has none.
 */
static const char *const k_zones[WALL_ZONE_COUNT] = {
    "UTC0",                           /* UTC          */
    "GMT0BST,M3.5.0/1,M10.5.0",       /* London       */
    "CET-1CEST,M3.5.0,M10.5.0/3",     /* Berlin       */
    "EET-2EEST,M3.5.0/3,M10.5.0/4",   /* Helsinki     */
    "EST5EDT,M3.2.0,M11.1.0",         /* New York     */
    "CST6CDT,M3.2.0,M11.1.0",         /* Chicago      */
    "MST7MDT,M3.2.0,M11.1.0",         /* Denver       */
    "PST8PDT,M3.2.0,M11.1.0",         /* Los Angeles  */
    "JST-9",                          /* Tokyo        */
    "AEST-10AEDT,M10.1.0,M4.1.0/3",   /* Sydney       */
};

bool wall_valid(int64_t unix_s)
{
    return unix_s >= WALL_VALID_FLOOR_S;
}

bool wall_stamp_known(int64_t mtime)
{
    return mtime >= WALL_STAMP_FLOOR_S;
}

const char *wall_zone_posix(int index)
{
    if (index < 0 || index >= WALL_ZONE_COUNT) {
        index = WALL_ZONE_DEFAULT;
    }
    return k_zones[index];
}

int64_t wall_local_s(int64_t unix_s)
{
    if (unix_s < 0) {
        return unix_s;
    }
    const time_t t = (time_t)unix_s;
    struct tm tm;
    if ((int64_t)t != unix_s || localtime_r(&t, &tm) == NULL) {
        return unix_s;
    }
    /* Days since 1970 of the local civil date, H. Hinnant's
     * days_from_civil. */
    const int64_t y = (int64_t)tm.tm_year + 1900 - ((tm.tm_mon < 2) ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t mp = (tm.tm_mon < 2) ? tm.tm_mon + 10 : tm.tm_mon - 2;
    const int64_t doy = (153 * mp + 2) / 5 + tm.tm_mday - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + (int64_t)tm.tm_hour * 3600
           + (int64_t)tm.tm_min * 60 + (int64_t)tm.tm_sec;
}

char *wall_utc_text(char *out, size_t n, int64_t unix_s)
{
    if (out == NULL || n == 0u) {
        return NULL;
    }
    if (unix_s < 0) {
        unix_s = 0;
    }
    /* Days to a civil date (proleptic Gregorian), H. Hinnant's
     * civil_from_days for days of 0 or more. */
    const int64_t days = unix_s / 86400 + 719468;
    const int64_t secs = unix_s % 86400;
    const int64_t era = days / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = (mp < 10) ? mp + 3 : mp - 9;
    const int64_t y = yoe + era * 400 + ((m <= 2) ? 1 : 0);
    snprintf(out, n, "%04" PRId64 "-%02" PRId64 "-%02" PRId64
             " %02" PRId64 ":%02" PRId64 ":%02" PRId64 " UTC", y, m, d,
             secs / 3600, (secs / 60) % 60, secs % 60);
    return out;
}

char *wall_ntp_line(char *out, size_t n, const char *server, int64_t new_us,
                    int64_t old_us)
{
    if (out == NULL || n == 0u) {
        return NULL;
    }
    char when[32];
    (void)wall_utc_text(when, sizeof(when), new_us / 1000000);
    const int64_t off = new_us - old_us;
    /* In milliseconds, rounded half away from zero, the sign kept apart so
     * an offset under 1s still says which way the clock moved. */
    const uint64_t mag_us = (off < 0) ? (uint64_t)0 - (uint64_t)off
                                      : (uint64_t)off;
    const uint64_t mag_ms = (mag_us + 500u) / 1000u;
    snprintf(out, n, "ntp: %s set %s, offset %c%" PRIu64 ".%03" PRIu64
             "s from the old clock", (server != NULL) ? server : "?", when,
             (off < 0) ? '-' : '+', mag_ms / 1000u, mag_ms % 1000u);
    return out;
}

wall_ntp_do_t wall_ntp_step(wall_ntp_t *n, bool joined, bool answered,
                            uint32_t now_ms)
{
    if (n == NULL) {
        return WALL_NTP_NONE;
    }
    switch (n->state) {
    case WALL_NTP_IDLE:
        if (joined) {
            n->state = WALL_NTP_ASKING;
            n->start_ms = now_ms;
            return WALL_NTP_START;
        }
        return WALL_NTP_NONE;
    case WALL_NTP_ASKING:
        if (answered) {
            /* An answer that came as the join ended still set the clock. */
            n->state = joined ? WALL_NTP_DONE : WALL_NTP_IDLE;
            return WALL_NTP_SET;
        }
        if (!joined) {
            n->state = WALL_NTP_IDLE;
            return WALL_NTP_STOP;
        }
        if ((uint32_t)(now_ms - n->start_ms) >= WALL_NTP_TIMEOUT_MS) {
            n->state = WALL_NTP_DONE;
            return WALL_NTP_GAVE_UP;
        }
        return WALL_NTP_NONE;
    case WALL_NTP_DONE:
    default:
        if (!joined) {
            n->state = WALL_NTP_IDLE;
        }
        return WALL_NTP_NONE;
    }
}

bool wall_ntp_holds(const wall_ntp_t *n, bool lingers, uint32_t now_ms)
{
    return n != NULL && lingers && n->state == WALL_NTP_ASKING
           && (uint32_t)(now_ms - n->start_ms) < WALL_NTP_TIMEOUT_MS;
}

wall_ntp_do_t wall_ntp_hold_step(wall_ntp_t *n, bool joined, bool lingers,
                                 bool answered, uint32_t now_ms)
{
    const bool asking = n != NULL && n->state == WALL_NTP_ASKING;
    return wall_ntp_step(n, joined || (lingers && asking), answered, now_ms);
}
