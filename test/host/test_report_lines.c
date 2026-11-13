/*
 * The lines of the IOmcu's 3s console report (report_lines.h).
 *
 * The expected texts below are what iomcu_report.c printed through
 * IOMCU_SAY() before the report moved to shared/bench, for the same values:
 * its format strings copied verbatim into a host program, run once, and
 * its output pasted here.  docs/Bringup.md quotes these lines and log
 * readers parse them, so a difference is a change of the console format.
 *
 * At the limits: every field at its widest (UINT32_MAX, INT16_MIN, 0xFFFF,
 * a reset text of 143 and a boot text of 111 characters, the longest their
 * buffers hold), a buffer one byte short, a line out of range.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "report_lines.h"

/* A snapshot with every line that can appear, ordinary values. */
static report_snap_t snap_a(void)
{
    report_snap_t s;
    memset(&s, 0, sizeof(s));
    s.store_off = true;
    s.slot = 1;
    s.on_trial = true;
    s.reset = "WHY-TEXT";
    s.boot = "BOOT-TEXT";
    s.can_up = true;
    s.bitrate = 1000000u;
    s.requests = 123456u;
    s.echoes = 7u;
    s.tec = 3u;
    s.rec = 4u;
    s.eflg = 0x15u;
    s.tx_drop = 9u;
    s.txq_max = 11u;
    s.overruns = 2u;
    s.sense_on = true;
    s.sn_flags = 0x0123u;
    s.sn_present = 0x0003u;
    s.i228_id = 0x2260u;
    s.i3221_id = 0x3220u;
    s.sn_errors = 5u;
    s.i228_resets = 1u;
    s.i3221_resets = 2u;
    s.temp_dc = -123;
    s.bn_cv = 1234u;
    s.bn_ca = 567u;
    s.bn_mah = 89u;
    s.bn_dwh = 10u;
    s.bn_flags = 0x1Fu;
    s.ss_window = 20u;
    s.ch1_ma = -45;
    s.ch1_mv = 5012u;
    s.ch_flags = 0x03u;
    s.cap_state = 2u;
    s.cap_seq = 17u;
    s.cap_move = 456u;
    s.cap_arrive = 789u;
    s.map_fit = 0x7u;
    s.map_fast = 1u;
    s.map_flags = 0x0301u;
    for (unsigned c = 0; c < REPORT_MAP_CHANNELS; ++c) {
        const int v = (int)c + 1;
        s.map_ma[c] = (int16_t)((c % 2u) == 0u ? v : -v);
    }
    s.over = 0x005u;
    s.over_count = 3u;
    s.loop.max_us = 1530u;
    s.loop.over = 12u;
    s.loop.sec_max_us[PASS_SEC_CAN] = 61u;
    s.loop.sec_max_us[PASS_SEC_REPORT] = 248u;
    return s;
}

/* Every line of the snapshot in order, the ones that apply joined. */
static size_t report_text(const report_snap_t *s, char *out, size_t cap,
                          unsigned *lines)
{
    size_t at = 0;
    *lines = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < REPORT_LINES; ++i) {
        char buf[REPORT_LINE_MAX];
        const size_t n = report_line(s, i, buf, sizeof(buf));
        if (n == 0u) {
            continue;
        }
        if (n != strlen(buf)) {
            T_FAIL("line %u: returned %zu, holds %zu", i, n, strlen(buf));
        }
        if (at + n < cap) {
            memcpy(out + at, buf, n + 1u);
            at += n;
        }
        ++*lines;
    }
    return at;
}

static void check_lines(const report_snap_t *s, const char *const *want,
                        unsigned count)
{
    unsigned k = 0;
    for (unsigned i = 0; i < REPORT_LINES; ++i) {
        char buf[REPORT_LINE_MAX];
        if (report_line(s, i, buf, sizeof(buf)) == 0u) {
            continue;
        }
        if (k >= count) {
            T_FAIL("line %u is one too many: %s", i, buf);
            return;
        }
        if (strcmp(buf, want[k]) != 0) {
            T_FAIL("line %u:\n        got  %s        want %s", i, buf,
                   want[k]);
        }
        ++k;
    }
    CHECK_EQ(k, count);
}

TEST_CASE(the_lines_read_as_before)
{
    static const char *const a[] = {
        "rcbench-iomcu: flash store OFF -- core 1 did not register for the flash lock-out; set-ups run from RAM and are lost at restart\n",
        "rcbench-iomcu: running from slot B, on trial until the panel buys it\n",
        "rcbench-iomcu: reset WHY-TEXT\n",
        "rcbench-iomcu: BOOT-TEXT\n",
        "rcbench-iomcu: CAN up, 1000000 bit/s, 123456 requests served, 7 self-test echoes, tx_err 3 rx_err 4 eflg 0x15 tx_drop 9 txq_max 11\n",
        "rcbench-iomcu: CAN receive buffers overran 2 time(s) -- frames arrived with nowhere to put them; not a bus fault\n",
        "rcbench-iomcu: sense flags 0x0123 present 0x0003 ids 0x2260 0x3220 errors 5 resets 1 2 | bench 1234 cV 567 cA 89 mAh 10 dWh flags 0x1F | temp -123 dC\n",
        "rcbench-iomcu: sense window 20 CH1 -45 mA 5012 mV ch_flags 0x03 | capture state 2 seq 17 move 456 arrive 789\n",
        "rcbench-iomcu: sense map fit 0x7 fast 1 flags 0x0301 | mA 1 -2 3 -4 5 -6 7 -8 9 -10 11 -12 | over 0x005 count 3\n",
        "rcbench-iomcu: loop max 1530 us, over 500us 12, can max 61 us, report max 248 us\n",
    };
    report_snap_t s = snap_a();
    check_lines(&s, a, 10u);

    /* CAN down, no current monitor, slot A not on trial, the store on. */
    static const char *const b[] = {
        "rcbench-iomcu: running from slot A\n",
        "rcbench-iomcu: reset WHY-TEXT\n",
        "rcbench-iomcu: BOOT-TEXT\n",
        "rcbench-iomcu: CAN did not answer on SPI -- module fitted? wiring on GP9-12?\n",
        "rcbench-iomcu: loop max 1530 us, over 500us 12, can max 61 us, report max 248 us\n",
    };
    s = snap_a();
    s.store_off = false;
    s.slot = 0;
    s.on_trial = false;
    s.can_up = false;
    s.sense_on = false;
    check_lines(&s, b, 5u);

    /* No partition table, CAN up without an overrun. */
    static const char *const c[] = {
        "rcbench-iomcu: no partition table -- firmware updates over CAN need one USB load of rcbench-iomcu-install.uf2\n",
        "rcbench-iomcu: reset WHY-TEXT\n",
        "rcbench-iomcu: BOOT-TEXT\n",
        "rcbench-iomcu: CAN up, 1000000 bit/s, 123456 requests served, 7 self-test echoes, tx_err 3 rx_err 4 eflg 0x15 tx_drop 9 txq_max 11\n",
        "rcbench-iomcu: loop max 1530 us, over 500us 12, can max 61 us, report max 248 us\n",
    };
    s = snap_a();
    s.store_off = false;
    s.slot = -1;
    s.overruns = 0u;
    s.sense_on = false;
    check_lines(&s, c, 5u);
}

TEST_CASE(the_loop_line_reads_its_four_figures)
{
    report_snap_t s;
    memset(&s, 0, sizeof(s));
    s.loop.max_us = 1u;
    s.loop.over = 2u;
    s.loop.sec_max_us[PASS_SEC_CAN] = 3u;
    s.loop.sec_max_us[PASS_SEC_REPORT] = 4u;
    char buf[REPORT_LINE_MAX];
    CHECK(report_line(&s, REPORT_L_LOOP, buf, sizeof(buf)) > 0u);
    CHECK_STR_EQ(buf, "rcbench-iomcu: loop max 1 us, over 500us 2, can max "
                      "3 us, report max 4 us\n");
    /* It is the last line, and it always applies. */
    CHECK_EQ(REPORT_L_LOOP, REPORT_LINES - 1);
}

/* Every field at its widest. */
static report_snap_t snap_widest(char *why, char *boot)
{
    memset(why, 'R', 143);
    why[143] = '\0';
    memset(boot, 'B', 111);
    boot[111] = '\0';
    report_snap_t s;
    memset(&s, 0xFF, sizeof(s));
    s.store_off = true;
    s.slot = 1;
    s.on_trial = true;
    s.reset = why;
    s.boot = boot;
    s.can_up = true;
    s.bitrate = 1000000u;       /* IOMCU_CAN_BITRATE, the one rate */
    s.sense_on = true;
    s.temp_dc = INT16_MIN;
    s.ch1_ma = INT16_MIN;
    for (unsigned c = 0; c < REPORT_MAP_CHANNELS; ++c) {
        s.map_ma[c] = INT16_MIN;
    }
    return s;
}

TEST_CASE(every_line_fits_at_its_widest)
{
    char why[144], boot[112];
    report_snap_t s = snap_widest(why, boot);
    for (int up = 0; up < 2; ++up) {
        s.can_up = up != 0;
        for (unsigned i = 0; i < REPORT_LINES; ++i) {
            char buf[REPORT_LINE_MAX + 64u];
            const size_t n = report_line(&s, i, buf, sizeof(buf));
            if (n + 1u > REPORT_LINE_MAX) {
                T_FAIL("line %u takes %zu bytes", i, n + 1u);
            }
        }
    }
    /* A buffer one byte short of a line cuts it and says what it holds. */
    char buf[REPORT_LINE_MAX];
    const size_t n = report_line(&s, REPORT_L_SENSE_MAP, buf, sizeof(buf));
    char small[REPORT_LINE_MAX];
    CHECK_EQ(report_line(&s, REPORT_L_SENSE_MAP, small, n), n - 1u);
    CHECK_EQ(strlen(small), n - 1u);
    CHECK(strncmp(small, buf, n - 1u) == 0);
}

TEST_CASE(the_whole_report_is_at_most_report_bytes_max)
{
    char why[144], boot[112];
    report_snap_t s = snap_widest(why, boot);
    static char out[4096];
    unsigned lines;
    CHECK_EQ(report_text(&s, out, sizeof(out), &lines), REPORT_BYTES_MAX);
    CHECK_EQ(lines, 10);
    s.can_up = false;
    CHECK(report_text(&s, out, sizeof(out), &lines) < REPORT_BYTES_MAX);
    CHECK_EQ(lines, 9);
}

TEST_CASE(a_line_out_of_range_or_without_a_buffer_is_empty)
{
    report_snap_t s = snap_a();
    char buf[REPORT_LINE_MAX];
    strcpy(buf, "x");
    CHECK_EQ(report_line(&s, REPORT_LINES, buf, sizeof(buf)), 0);
    CHECK_STR_EQ(buf, "");
    CHECK_EQ(report_line(NULL, REPORT_L_RESET, buf, sizeof(buf)), 0);
    CHECK_EQ(report_line(&s, REPORT_L_RESET, NULL, sizeof(buf)), 0);
    CHECK_EQ(report_line(&s, REPORT_L_RESET, buf, 0u), 0);
    /* No text reads as an empty one. */
    s.reset = NULL;
    s.boot = NULL;
    CHECK(report_line(&s, REPORT_L_RESET, buf, sizeof(buf)) > 0u);
    CHECK_STR_EQ(buf, "rcbench-iomcu: reset \n");
    CHECK(report_line(&s, REPORT_L_BOOT, buf, sizeof(buf)) > 0u);
    CHECK_STR_EQ(buf, "rcbench-iomcu: \n");
}

int main(void)
{
    RUN(the_lines_read_as_before);
    RUN(the_loop_line_reads_its_four_figures);
    RUN(every_line_fits_at_its_widest);
    RUN(the_whole_report_is_at_most_report_bytes_max);
    RUN(a_line_out_of_range_or_without_a_buffer_is_empty);
    return test_summary("report_lines");
}
