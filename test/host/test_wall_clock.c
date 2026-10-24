/*
 * The panel's wall clock (shared/clock): whether the clock holds a real
 * date, whether a file's FAT stamp says anything, the time zones SETUP
 * offers and their row in the settings model, the console line of an NTP
 * answer, and the one request a join makes.
 *
 * The panel's end -- esp_netif_sntp, setenv("TZ") and the Wi-Fi worker
 * (firmware/panel/components/wifi_access/wifi_ntp.c, main/panel_settings.c)
 * -- is firmware and not in this suite.  The zones are held to the host's
 * C library, which reads POSIX TZ strings as newlib does on the panel.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "settings_rig.h"
#include "wall_clock.h"

/* ------------------------------------------------------------ helpers */

/* Seconds since 1970 of a UTC civil time (days_from_civil). */
static int64_t utc(int y, int mo, int d, int h, int mi, int s)
{
    y -= (mo <= 2) ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

/* Run with TZ set to zone @p index, then back to UTC. */
static void zone(int index)
{
    (void)setenv("TZ", wall_zone_posix(index), 1);
    tzset();
}

/* The local time zone @p index shows at UTC second @p t, as "HH:MM". */
static void local_hm(int index, int64_t t, char *out, size_t n)
{
    zone(index);
    const time_t tt = (time_t)t;
    struct tm tm;
    (void)localtime_r(&tt, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/*
 * What ESP-IDF's get_fattime() (components/fatfs/diskio/diskio.c) stamps
 * at clock reading @p t in zone @p index, read back as storage_list.c's
 * fat_mtime() reads it: localtime, the year clamped to 1980, then mktime.
 */
static int64_t fat_stamp(int index, int64_t t)
{
    zone(index);
    const time_t tt = (time_t)t;
    struct tm tm;
    (void)localtime_r(&tt, &tm);
    const int year = tm.tm_year < 80 ? 0 : tm.tm_year - 80;
    struct tm back = {
        .tm_mday = tm.tm_mday,
        .tm_mon = tm.tm_mon,
        .tm_year = year + 80,
        .tm_sec = (tm.tm_sec >> 1) * 2,
        .tm_min = tm.tm_min,
        .tm_hour = tm.tm_hour,
        .tm_isdst = -1,
    };
    return (int64_t)mktime(&back);
}

/* ------------------------------------------------- the clock's date */

TEST_CASE(a_clock_from_2026_on_holds_a_real_date)
{
    CHECK_EQ(WALL_VALID_FLOOR_S, utc(2026, 1, 1, 0, 0, 0));
    CHECK(wall_valid(WALL_VALID_FLOOR_S));
    CHECK(!wall_valid(WALL_VALID_FLOOR_S - 1));
    CHECK(wall_valid(utc(2026, 10, 24, 14, 3, 12)));
    /* An unset clock counts uptime from 1970: never valid, for 56 years. */
    CHECK(!wall_valid(0));
    CHECK(!wall_valid(-1));
    CHECK(!wall_valid(1 * 86400 + 6 * 3600 + 54 * 60));
    CHECK(!wall_valid(utc(2025, 12, 31, 23, 59, 59)));
    CHECK(wall_valid(utc(2107, 12, 31, 23, 59, 58)));
}

TEST_CASE(a_stamp_from_an_unset_clock_is_not_known_in_any_zone)
{
    CHECK_EQ(WALL_STAMP_FLOOR_S, utc(1981, 1, 1, 12, 0, 0));
    CHECK(!wall_stamp_known(-1));
    CHECK(!wall_stamp_known(0));
    /* The station's file: written 1d 6h 54min after a power-on, which
     * FAT reads back as 1980-01-02 06:54 in every zone. */
    for (int z = 0; z < WALL_ZONE_COUNT; ++z) {
        const int64_t st = fat_stamp(z, 1 * 86400 + 6 * 3600 + 54 * 60);
        if (wall_stamp_known(st)) {
            T_FAIL("zone %d: an unset clock's stamp %lld counts as known", z,
                   (long long)st);
        }
        /* 365 days of uptime is still 1980 in FAT: unknown. */
        const int64_t late = fat_stamp(z, 365LL * 86400);
        if (wall_stamp_known(late)) {
            T_FAIL("zone %d: a year of uptime counts as known", z);
        }
        /* A clock NTP set: the stamp is known, whatever the zone. */
        const int64_t set = fat_stamp(z, utc(2026, 10, 24, 14, 3, 12));
        if (!wall_stamp_known(set)) {
            T_FAIL("zone %d: a real stamp counts as unknown", z);
        }
    }
    /* mktime() reads a local stamp as UTC less the zone's offset.  The
     * last minute of 1980 in UTC-12 and in UTC+14 stays below the floor;
     * the third day of 1981 in UTC+14 and the second's noon in UTC-12 are
     * above it. */
    CHECK(!wall_stamp_known(utc(1980, 12, 31, 23, 58, 0) + 12 * 3600));
    CHECK(!wall_stamp_known(utc(1980, 12, 31, 23, 58, 0) - 14 * 3600));
    CHECK(wall_stamp_known(utc(1981, 1, 3, 0, 0, 0) - 14 * 3600));
    CHECK(wall_stamp_known(utc(1981, 1, 2, 0, 0, 0) + 12 * 3600));
    zone(0);
}

/* ------------------------------------------------------- the zones */

TEST_CASE(the_zones_are_settings_options_in_the_same_order)
{
    fresh_model();
    const setting_def_t *d = settings_def(SET_TIME_ZONE);
    CHECK(d != NULL);
    if (d == NULL) {
        return;
    }
    CHECK_STR_EQ(d->key, "time_zone");
    CHECK(strlen(d->key) <= 15);
    CHECK_EQ(d->cat, SET_CAT_APP);
    CHECK_EQ(d->type, SET_TYPE_ENUM);
    CHECK_EQ(d->option_count, WALL_ZONE_COUNT);
    CHECK_EQ((int)d->def, WALL_ZONE_DEFAULT);
    static const char *const names[WALL_ZONE_COUNT] = {
        "UTC", "LONDON", "BERLIN", "HELSINKI", "NEW YORK", "CHICAGO",
        "DENVER", "LOS ANGELES", "TOKYO", "SYDNEY",
    };
    for (int i = 0; i < WALL_ZONE_COUNT && i < d->option_count; ++i) {
        CHECK_STR_EQ(d->options[i], names[i]);
    }
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), WALL_ZONE_DEFAULT);
    CHECK_STR_EQ(wall_zone_posix(WALL_ZONE_DEFAULT),
                 "CET-1CEST,M3.5.0,M10.5.0/3");
    char buf[24];
    CHECK_STR_EQ(settings_value_text(SET_TIME_ZONE, buf, sizeof(buf)),
                 "BERLIN");
    /* German: the city names as the English list writes them. */
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_setting_label(SET_TIME_ZONE), "Zeitzone");
    CHECK_STR_EQ(ui_setting_option(SET_TIME_ZONE, 8), "TOKYO");
    CHECK_STR_EQ(ui_setting_option(SET_TIME_ZONE, 2), "BERLIN");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(ui_setting_label(SET_TIME_ZONE), "Time zone");
    /* PANEL lists it after Units. */
    setting_id_t ids[32];
    const int n = settings_in_category(SET_CAT_APP, ids, 32);
    int at = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == SET_TIME_ZONE) {
            at = i;
        }
    }
    CHECK(at > 0);
    CHECK_EQ(at > 0 ? ids[at - 1] : SETTING_COUNT, SET_UNITS);
    /* Out of range: Berlin. */
    CHECK_STR_EQ(wall_zone_posix(-1), wall_zone_posix(WALL_ZONE_DEFAULT));
    CHECK_STR_EQ(wall_zone_posix(WALL_ZONE_COUNT),
                 wall_zone_posix(WALL_ZONE_DEFAULT));
}

TEST_CASE(each_zone_shows_its_local_time_across_daylight_saving)
{
    struct {
        int zone;
        int64_t t;
        const char *hm;
    } k[] = {
        /* UTC: never moves. */
        { 0, utc(2026, 7, 1, 12, 0, 0), "12:00" },
        /* Berlin: 01:00 UTC on the last Sundays of March and October. */
        { 2, utc(2026, 3, 29, 0, 59, 0), "01:59" },
        { 2, utc(2026, 3, 29, 1, 0, 0), "03:00" },
        { 2, utc(2026, 10, 25, 0, 59, 0), "02:59" },
        { 2, utc(2026, 10, 25, 1, 0, 0), "02:00" },
        { 2, utc(2026, 10, 24, 14, 3, 0), "16:03" },
        /* London and Helsinki change at the same instant. */
        { 1, utc(2026, 3, 29, 0, 59, 0), "00:59" },
        { 1, utc(2026, 3, 29, 1, 0, 0), "02:00" },
        { 1, utc(2026, 12, 1, 9, 0, 0), "09:00" },
        { 3, utc(2026, 3, 29, 0, 59, 0), "02:59" },
        { 3, utc(2026, 3, 29, 1, 0, 0), "04:00" },
        { 3, utc(2026, 10, 25, 1, 0, 0), "03:00" },
        /* The US: the second Sunday of March, the first of November. */
        { 4, utc(2026, 3, 8, 6, 59, 0), "01:59" },
        { 4, utc(2026, 3, 8, 7, 0, 0), "03:00" },
        { 4, utc(2026, 11, 1, 5, 59, 0), "01:59" },
        { 4, utc(2026, 11, 1, 6, 0, 0), "01:00" },
        { 5, utc(2026, 1, 15, 18, 0, 0), "12:00" },
        { 6, utc(2026, 7, 15, 18, 0, 0), "12:00" },
        { 7, utc(2026, 7, 15, 19, 0, 0), "12:00" },
        { 7, utc(2026, 1, 15, 20, 0, 0), "12:00" },
        /* Tokyo: UTC+9 all year. */
        { 8, utc(2026, 7, 1, 3, 0, 0), "12:00" },
        { 8, utc(2026, 1, 1, 3, 0, 0), "12:00" },
        /* Sydney: summer from October to April. */
        { 9, utc(2026, 1, 15, 1, 0, 0), "12:00" },
        { 9, utc(2026, 7, 15, 2, 0, 0), "12:00" },
        { 9, utc(2026, 4, 4, 15, 59, 0), "02:59" },
        { 9, utc(2026, 4, 4, 16, 0, 0), "02:00" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char hm[16];
        local_hm(k[i].zone, k[i].t, hm, sizeof(hm));
        if (strcmp(hm, k[i].hm) != 0) {
            T_FAIL("case %zu, zone %d: got %s, want %s", i, k[i].zone, hm,
                   k[i].hm);
        }
    }
    zone(0);
}

TEST_CASE(the_zone_survives_a_save_and_a_restart)
{
    fresh_model();
    settings_set_store(&s_mem_store);
    settings_set_observer(observer);
    s_observed = 0;
    settings_set(SET_TIME_ZONE, 7.0f);
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 7);
    CHECK_EQ(s_observed, 1);
    CHECK_EQ(s_last_observed, SET_TIME_ZONE);
    CHECK(settings_dirty());
    CHECK(settings_save());
    CHECK_EQ(s_saved[SET_TIME_ZONE], 7.0f);

    /* The restart: defaults, then the store; the panel then hands the zone
     * to the C library before the card mounts (panel_boot.c). */
    settings_init();
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 7);
    CHECK(!settings_dirty());
    CHECK_STR_EQ(wall_zone_posix(settings_get_int(SET_TIME_ZONE)),
                 "PST8PDT,M3.2.0,M11.1.0");

    /* A stored word off the list lands on it. */
    s_saved[SET_TIME_ZONE] = 15.0f;
    settings_init();
    CHECK(settings_get_int(SET_TIME_ZONE) >= 0);
    CHECK(settings_get_int(SET_TIME_ZONE) < WALL_ZONE_COUNT);
    s_saved[SET_TIME_ZONE] = -3.0f;
    settings_init();
    CHECK(settings_get_int(SET_TIME_ZONE) >= 0);
    CHECK(settings_get_int(SET_TIME_ZONE) < WALL_ZONE_COUNT);

    /* The step wraps SYDNEY -> UTC and back; RESET CATEGORY gives Berlin.
     * Not held while ARMED: it moves no control. */
    settings_set(SET_TIME_ZONE, 9.0f);
    settings_adjust(SET_TIME_ZONE, 1);
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 0);
    settings_adjust(SET_TIME_ZONE, -1);
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), 9);
    CHECK(!settings_safe_only(SET_TIME_ZONE));
    settings_reset(SET_CAT_APP);
    CHECK_EQ(settings_get_int(SET_TIME_ZONE), WALL_ZONE_DEFAULT);
    settings_set_observer(NULL);
    settings_set_store(NULL);
}

TEST_CASE(a_stamp_reads_back_in_the_zone_it_was_written_in)
{
    /* FatFs stamps 16:03 local in Berlin on 2026-10-24; mktime() makes
     * that 14:03 UTC; wall_local_s() gives back the 16:03 FILES writes. */
    char b[32];
    zone(2);
    const int64_t st = fat_stamp(2, utc(2026, 10, 24, 14, 3, 12));
    CHECK_EQ(st, utc(2026, 10, 24, 14, 3, 12));
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), wall_local_s(st)),
                 "2026-10-24 16:03:12 UTC");
    /* Winter: one hour. */
    CHECK_EQ(wall_local_s(utc(2026, 12, 1, 9, 0, 0)), utc(2026, 12, 1, 10, 0, 0));
    /* West of Greenwich across midnight and a year end. */
    zone(4);
    CHECK_EQ(wall_local_s(utc(2027, 1, 1, 3, 0, 0)),
             utc(2026, 12, 31, 22, 0, 0));
    /* East across a leap day. */
    zone(9);
    CHECK_EQ(wall_local_s(utc(2028, 2, 28, 14, 30, 0)),
             utc(2028, 2, 29, 1, 30, 0));
    /* UTC moves nothing; no stamp stays no stamp. */
    zone(0);
    CHECK_EQ(wall_local_s(utc(2026, 7, 1, 12, 0, 0)), utc(2026, 7, 1, 12, 0, 0));
    CHECK_EQ(wall_local_s(0), 0);
    CHECK_EQ(wall_local_s(-1), -1);
}

/* ------------------------------------------------- the console line */

TEST_CASE(utc_text_writes_civil_dates)
{
    char b[32];
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), 0), "1970-01-01 00:00:00 UTC");
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), -5), "1970-01-01 00:00:00 UTC");
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), utc(2026, 10, 24, 14, 3, 12)),
                 "2026-10-24 14:03:12 UTC");
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), utc(2028, 2, 29, 23, 59, 59)),
                 "2028-02-29 23:59:59 UTC");
    CHECK_STR_EQ(wall_utc_text(b, sizeof(b), utc(2100, 3, 1, 0, 0, 0)),
                 "2100-03-01 00:00:00 UTC");
    /* A short buffer is cut, never overrun. */
    char s[11];
    CHECK_STR_EQ(wall_utc_text(s, sizeof(s), utc(2026, 10, 24, 0, 0, 0)),
                 "2026-10-24");
    CHECK(wall_utc_text(NULL, 8, 0) == NULL);
    CHECK(wall_utc_text(b, 0, 0) == NULL);
}

TEST_CASE(the_ntp_line_names_server_time_and_offset)
{
    char b[128];
    const int64_t now = utc(2026, 10, 24, 14, 3, 12) * 1000000LL + 345000;
    /* From an unset clock: 1d 6h 54min of uptime. */
    const int64_t old = (1LL * 86400 + 6 * 3600 + 54 * 60) * 1000000LL;
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), "pool.ntp.org", now, old),
                 "ntp: pool.ntp.org set 2026-10-24 14:03:12 UTC, offset "
                 "+1792739352.345s from the old clock");
    /* A clock 1.5s fast moves back. */
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), "pool.ntp.org", now,
                               now + 1500000),
                 "ntp: pool.ntp.org set 2026-10-24 14:03:12 UTC, offset "
                 "-1.500s from the old clock");
    /* Under a millisecond each way, rounded half away from zero. */
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), "x", now, now - 499),
                 "ntp: x set 2026-10-24 14:03:12 UTC, offset +0.000s from "
                 "the old clock");
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), "x", now, now - 500),
                 "ntp: x set 2026-10-24 14:03:12 UTC, offset +0.001s from "
                 "the old clock");
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), "x", now, now + 500),
                 "ntp: x set 2026-10-24 14:03:12 UTC, offset -0.001s from "
                 "the old clock");
    CHECK_STR_EQ(wall_ntp_line(b, sizeof(b), NULL, now, now),
                 "ntp: ? set 2026-10-24 14:03:12 UTC, offset +0.000s from "
                 "the old clock");
    /* The longest: a 32-character name and a 12-digit offset fit 128. */
    char big[128];
    (void)wall_ntp_line(big, sizeof(big), "abcdefghijklmnopqrstuvwxyz012345",
                        now, now - 999999999999LL * 1000000LL);
    CHECK(strstr(big, "+999999999999.000s from the old clock") != NULL);
    CHECK(wall_ntp_line(NULL, 8, "x", now, old) == NULL);
    CHECK(wall_ntp_line(b, 0, "x", now, old) == NULL);
}

/* --------------------------------------------- one request a join */

TEST_CASE(a_join_asks_once_and_logs_the_answer)
{
    wall_ntp_t n;
    memset(&n, 0, sizeof(n));
    CHECK_EQ(wall_ntp_step(&n, false, false, 0u), WALL_NTP_NONE);
    CHECK_EQ(n.state, WALL_NTP_IDLE);
    CHECK_EQ(wall_ntp_step(&n, true, false, 100u), WALL_NTP_START);
    CHECK_EQ(n.state, WALL_NTP_ASKING);
    CHECK_EQ(wall_ntp_step(&n, true, false, 200u), WALL_NTP_NONE);
    CHECK_EQ(wall_ntp_step(&n, true, true, 450u), WALL_NTP_SET);
    CHECK_EQ(n.state, WALL_NTP_DONE);
    /* No second request in the same join, answered or not. */
    CHECK_EQ(wall_ntp_step(&n, true, true, 500u), WALL_NTP_NONE);
    CHECK_EQ(wall_ntp_step(&n, true, false, 600000u), WALL_NTP_NONE);
    /* The join ends; the next one asks again. */
    CHECK_EQ(wall_ntp_step(&n, false, false, 700000u), WALL_NTP_NONE);
    CHECK_EQ(n.state, WALL_NTP_IDLE);
    CHECK_EQ(wall_ntp_step(&n, true, false, 800000u), WALL_NTP_START);
    CHECK_EQ(wall_ntp_step(NULL, true, true, 0u), WALL_NTP_NONE);
}

TEST_CASE(an_unanswered_request_gives_up_at_30000ms_across_the_wrap)
{
    wall_ntp_t n;
    memset(&n, 0, sizeof(n));
    const uint32_t t0 = 0xFFFFF000u;   /* 4096ms before the wrap */
    CHECK_EQ(wall_ntp_step(&n, true, false, t0), WALL_NTP_START);
    CHECK_EQ(wall_ntp_step(&n, true, false, t0 + 4096u), WALL_NTP_NONE);
    CHECK_EQ(wall_ntp_step(&n, true, false, t0 + WALL_NTP_TIMEOUT_MS - 1u),
             WALL_NTP_NONE);
    CHECK_EQ(n.state, WALL_NTP_ASKING);
    CHECK_EQ(wall_ntp_step(&n, true, false, t0 + WALL_NTP_TIMEOUT_MS),
             WALL_NTP_GAVE_UP);
    CHECK_EQ(n.state, WALL_NTP_DONE);
    /* A late answer in the same join changes nothing more. */
    CHECK_EQ(wall_ntp_step(&n, true, true, t0 + 40000u), WALL_NTP_NONE);
}

TEST_CASE(a_join_that_ends_first_stops_the_request)
{
    wall_ntp_t n;
    memset(&n, 0, sizeof(n));
    CHECK_EQ(wall_ntp_step(&n, true, false, 10u), WALL_NTP_START);
    /* The release check ended and took the radio: STOP, nothing logged as
     * set. */
    CHECK_EQ(wall_ntp_step(&n, false, false, 8010u), WALL_NTP_STOP);
    CHECK_EQ(n.state, WALL_NTP_IDLE);
    /* An answer that came as the radio went still set the clock: SET,
     * and the next join asks again. */
    CHECK_EQ(wall_ntp_step(&n, true, false, 9000u), WALL_NTP_START);
    CHECK_EQ(wall_ntp_step(&n, false, true, 9300u), WALL_NTP_SET);
    CHECK_EQ(n.state, WALL_NTP_IDLE);
    CHECK_EQ(wall_ntp_step(&n, true, false, 9400u), WALL_NTP_START);
}

int main(void)
{
    RUN(a_clock_from_2026_on_holds_a_real_date);
    RUN(a_stamp_from_an_unset_clock_is_not_known_in_any_zone);
    RUN(the_zones_are_settings_options_in_the_same_order);
    RUN(each_zone_shows_its_local_time_across_daylight_saving);
    RUN(the_zone_survives_a_save_and_a_restart);
    RUN(a_stamp_reads_back_in_the_zone_it_was_written_in);
    RUN(utc_text_writes_civil_dates);
    RUN(the_ntp_line_names_server_time_and_offset);
    RUN(a_join_asks_once_and_logs_the_answer);
    RUN(an_unanswered_request_gives_up_at_30000ms_across_the_wrap);
    RUN(a_join_that_ends_first_stops_the_request);
    return test_summary("wall_clock");
}
