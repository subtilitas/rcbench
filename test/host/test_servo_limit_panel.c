/*
 * What the SERVO screen's limit run decides: START LIMIT RUN on
 * TEST's LIMIT RUN card, what refuses it, its report and every source
 * that aborts it.
 *
 * The screen and its fixtures are servo_rig.h, the bench
 * servo_bench_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "servo_limrun.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "settings.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------------------ the limit run */

/*
 * The LIMIT RUN card, mirrored from servo_limit_panel.c and
 * servo_workbench_int.h: TEST's second card at y 264 (SV_RUN_CARD_Y(1)),
 * its lines from 116px down, 2px under START LIMIT RUN, 20px apart in
 * the 10 x 20 face, their text 10px into the card (x UI_MARGIN + 20); the
 * run's own settings list with LIMIT RUN across row 0 and the mode's note
 * 6px under the top of row 2 (y 260, SV_LIM_NOTE_Y, servo_settings_draw.c).
 */
#define LIM_CARD    1
#define LIM_MODE_Y  LROW_Y(0)
#define LIM_LINE_Y(i) (SV_RUN_CARD_Y(1) + SV_RUN_LINE_DY - 2 + 20 * (i))
#define LIM_NOTE_Y  SV_LIM_NOTE_Y

/* Buffer 0 drawn with TEST open, and whether the LIMIT RUN card's line
 * @p i reads @p text in @p col on the card's fill, and nothing else: from
 * the text's left edge to 10px inside the card's right, clear of its
 * rounded corners beside the last line. */
static bool lim_line_reads(int i, const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    const int y = LIM_LINE_Y(i);
    const gfx_color_t bg = fb[(size_t)y * W + UI_MARGIN + 20u];
    gfx_fill_rect(&want_cv, UI_MARGIN + 20, (int16_t)y, 580, 20, bg);
    gfx_text(&want_cv, UI_MARGIN + 20, y, text, &gfx_font_10x20,
             ui_theme_color(col), 1);
    return same_in(fb, want_fb, UI_MARGIN + 20, y, 580, 20);
}

/* Buffer 0 drawn with the LIMIT RUN settings open, and whether the mode's
 * note reads @p text in @p col on the page's fill, and nothing else. */
static bool lim_note_reads(const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    const int y = LIM_NOTE_Y;
    const gfx_color_t bg = fb[(size_t)y * W + UI_MARGIN + 6];
    gfx_fill_rect(&want_cv, UI_MARGIN + 4, (int16_t)y, 610, 20, bg);
    gfx_text(&want_cv, UI_MARGIN + 10, y, text, &gfx_font_10x20,
             ui_theme_color(col), 1);
    return same_in(fb, want_fb, UI_MARGIN + 4, y, 610, 20);
}

/* Line @p i of the limit run's box on LIVE's left card: its text at the
 * box's left edge on the box's own fill, compared across the box.  It
 * opens LIVE, where the box is; a run started stays on TEST. */
#define LIM_BOX_X (UI_MARGIN + 10)
static bool lim_box_reads(int i, const char *text, ui_color_id_t col)
{
    close_settings();
    scr->render(&cv, 0);
    want_fresh();
    const int y = SV_BOX_LINE_Y(i);
    const gfx_color_t bg = fb[(size_t)y * W + LIM_BOX_X + 4];
    gfx_fill_rect(&want_cv, LIM_BOX_X + 2, (int16_t)y, SV_BOX_W - 4, 20, bg);
    gfx_text(&want_cv, LIM_BOX_X + 10, y, text, &gfx_font_10x20,
             ui_theme_color(col), 1);
    return same_in(fb, want_fb, LIM_BOX_X + 2, y, SV_BOX_W - 4, 20);
}

/* bench_on_ina() with the supply's output on, as the operator leaves it
 * for a limit run, and servo_sim's own stops at 1150 and 1880us. */
static void lim_bench(void)
{
    bench_on_ina();
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_init(&bench.sv, &sc);
    bench.sv.position_us = 1500.0f;
    bench.out = true;
    /* The binding read: one SERVO PWM output on GP2. */
    servo_screen_set_pins(PIN(2), true);
    bench_frames(200u);
}

static void lim_hold(float secs)
{
    hold_start_on(LIM_CARD, secs);
}

/* TEST open, its LIMIT RUN card in sight. */
static void lim_page(void)
{
    open_test();
}

/* The run to its end, 120s at most. */
static void lim_run_out(void)
{
    for (int i = 0; i < 240 && servo_screen_limit_running(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_limit_running());
    bench_frames(500u);
}

/* START LIMIT RUN starts nothing until the bench is armed and the INA3221
 * is the servo rail's meter, and the page says which is missing; a tap or a
 * hold under 2s starts nothing either.  The PD mini and the model as the
 * meter are refused: the run reads the INA3221 or nothing. */
TEST_CASE(a_limit_run_starts_only_armed_on_the_ina3221)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        /* The INA3221 off in SETUP. */
        bench_fresh();
        bench.out = true;
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_INA_SETUP), UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        CHECK(!servo_screen_testing());
        /* On, with no window yet: the PD mini is the meter. */
        close_settings();
        settings_set(SET_INA_FIT(0), 1.0f);
        meter(SERVO_SOURCE_PDMINI);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_INA_NO_WIN), UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        /* The model as the meter. */
        meter(SERVO_SOURCE_MODEL);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_INA_NO_WIN), UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        /* The INA3221 silent, and the link down. */
        lim_bench();
        servo_screen_source_why(SERVO_SOURCE_WHY_SILENT);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_INA_OFFLINE), UI_C_WARN));
        close_settings();
        servo_screen_source_why(SERVO_SOURCE_WHY_NONE);
        lim_bench();
        servo_screen_set_link(false);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_INA_OFFLINE), UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        servo_screen_set_link(true);
        /* Not armed. */
        lim_bench();
        servo_screen_set_armed(false);
        lim_page();
        CHECK(lim_line_reads(1, ui_servo_str(SERVO_STR_START_NOT_ARMED),
                             UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        /* A port in its programmer's hands renders no pulse. */
        lim_bench();
        servo_screen_set_drive(1u, 13u, false);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_PWM_OFF), UI_C_WARN));
        close_settings();
        servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
        /* Ready: the page says so, and only the whole hold starts it. */
        lim_bench();
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        /* The line names the output and the channel it reads. */
        char meter_line[96];
        snprintf(meter_line, sizeof(meter_line), TR(SV_LIM_LINE),
                 "SERVO PWM GP2", "INA3221 0x40 CH1");
        CHECK(lim_line_reads(0, meter_line, UI_C_TEXT_DIM));
        tap(START_X, START_Y);
        CHECK(!servo_screen_limit_running());
        close_settings();
        lim_hold(1.8f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        CHECK(!servo_screen_testing());
        /* Running, the line names the channel the run reads. */
        lim_page();
        CHECK(lim_line_reads(0, meter_line, UI_C_TEXT_DIM));
        /* START TEST is busy while it runs. */
        hold_start(2.3f);
        CHECK(!servo_screen_testing());
        servo_screen_set_armed(false);
        CHECK(!servo_screen_limit_running());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A limit run starts on a powered servo rail only: the last INA3221
 * window's voltage read and at least SERVO_LIMRUN_RAIL_MIN_MV, 3.0V.  An
 * output that is off reads 0V.  On the bench's own outputs a SERVO PWM
 * output is bound too, as the TARGET row names it. */
TEST_CASE(a_limit_run_needs_a_powered_rail_and_a_bound_output)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int k = 0; k < 4; ++k) {
            lim_bench();
            if (k == 0) {               /* 1mV under the threshold */
                bench.rail_set = true;
                bench.rail_mv = SERVO_LIMRUN_RAIL_MIN_MV - 1u;
            } else if (k == 1) {        /* the output off: 0V */
                bench.out = false;
            } else if (k == 2) {        /* no voltage in the window */
                bench.no_volt = true;
            } else {                    /* 0V from another source */
                bench.rail_set = true;
                bench.rail_mv = 0u;
            }
            bench_frames(200u);
            lim_page();
            CHECK(lim_line_reads(1, TR(SV_LIM_RAIL_LOW), UI_C_WARN));
            close_settings();
            lim_hold(2.3f);
            CHECK(!servo_screen_limit_running());
            close_settings();
        }
        /* At the threshold: ready, and the hold starts it. */
        lim_bench();
        bench.rail_set = true;
        bench.rail_mv = SERVO_LIMRUN_RAIL_MIN_MV;
        bench_frames(200u);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        close_settings();
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        servo_screen_set_armed(false);
        CHECK(!servo_screen_limit_running());
        /* No SERVO PWM output bound, and the binding not read. */
        for (int k = 0; k < 2; ++k) {
            lim_bench();
            servo_screen_set_pins(0u, k == 0);
            bench_frames(100u);
            lim_page();
            CHECK(lim_line_reads(1, k == 0 ? TR(SV_TARGET_NONE)
                                           : TR(SV_TARGET_UNREAD),
                                 UI_C_WARN));
            close_settings();
            lim_hold(2.3f);
            CHECK(!servo_screen_limit_running());
            close_settings();
        }
        /* A binding that names a pin, and the run starts. */
        lim_bench();
        servo_screen_set_pins(PIN(5), true);
        bench_frames(100u);
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        servo_screen_set_armed(false);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The bench's own output is no programming port: SEARCH+PROG is not
 * offered, the LIMIT RUN settings say why, and a tap on LIMIT RUN keeps
 * SEARCH ONLY; the card says nothing of it while SEARCH ONLY is chosen. */
TEST_CASE(programming_is_not_offered_off_the_programming_port)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        lim_bench();
        open_run_settings(LIM_CARD);
        CHECK(lim_note_reads(TR(SV_LIM_PROG_PORT), UI_C_TEXT_DIM));
        tap(ROW_L_X, LIM_MODE_Y);
        CHECK(lim_note_reads(TR(SV_LIM_PROG_PORT), UI_C_TEXT_DIM));
        CHECK(home_tag());
        /* The card's state line, and no reason under it. */
        CHECK(lim_line_reads(1, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
        CHECK(lim_line_reads(3, "", UI_C_WARN));
        close_settings();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* What the screen names to the router as a press that stops the limit
 * run: STOP RUN in the box over the dial, and START LIMIT RUN's place on
 * TEST while the run is under way.  Not START TEST, and nothing once the
 * run has ended. */
TEST_CASE(stop_run_is_named_to_the_router_while_the_run_is_under_way)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    close_settings();
    CHECK(scr->safe_at(BOXBTN_X, BOXBTN_Y));                 /* STOP RUN */
    lim_page();
    CHECK(scr->safe_at(START_X, LIM_START_Y));
    CHECK(!scr->safe_at(START_X, START_Y));
    tap(START_X, LIM_START_Y);
    CHECK(!servo_screen_limit_running());
    CHECK(!scr->safe_at(START_X, LIM_START_Y));
    close_settings();
    CHECK(!scr->safe_at(BOXBTN_X, BOXBTN_Y));                /* CLOSE */
}

/* The rows of the body at column @p x that the screen names as a stop. */
static int stop_rows_at(int x)
{
    int n = 0;
    for (int y = 0; y < UI_BODY_H; ++y) {
        n += scr->safe_at(x, y) ? 1 : 0;
    }
    return n;
}

/* The points under the alert band the screen names as a stop, every 2px. */
static int stops_under_the_band(void)
{
    int n = 0;
    for (int y = UI_ALERT_Y - UI_BAND_H; y < UI_BODY_H; y += 2) {
        for (int x = 0; x < W; x += 2) {
            n += scr->safe_at(x, y) ? 1 : 0;
        }
    }
    return n;
}

/* While the run is under way, STOP RUN in LIVE's box and START LIMIT RUN's
 * place on TEST are each a tap target of UI_TAP_MIN rows or more, and
 * both end above the alert band, on LIVE and on TEST. */
TEST_CASE(the_limit_runs_stops_are_tap_targets_above_the_alert_band)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    close_settings();
    CHECK(stop_rows_at(BOXBTN_X) >= UI_TAP_MIN);
    CHECK_EQ(stops_under_the_band(), 0);
    lim_page();
    CHECK(stop_rows_at(START_X) >= UI_TAP_MIN);
    CHECK_EQ(stops_under_the_band(), 0);
    tap(START_X, LIM_START_Y);
    CHECK(!servo_screen_limit_running());
    close_settings();
}

/* SEARCH ONLY on the INA3221: both ends found, the servo let go, nothing
 * else asked of the bench, and LIMITnnn.TXT opened alone with the limit
 * report: both endpoints inside the servo's stops, nothing written. */
TEST_CASE(a_search_only_limit_run_finds_both_ends_and_reports_them)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    /* The box over the dial while it runs: the search, then STOP RUN. */
    CHECK(lim_box_reads(0, TR(SV_LIM_TITLE), UI_C_ACCENT));
    lim_run_out();
    CHECK(bench.released);
    CHECK(bench.out);                       /* a search leaves it on */
    CHECK_EQ(bench.opens, 0u);
    CHECK_EQ(bench.opens_txt, 1u);
    CHECK_EQ(bench.csv, 0u);
    CHECK_EQ(bench.ends, 1u);
    CHECK(strstr(bench.report, "RCBENCH SERVO LIMIT REPORT\n") == bench.report);
    CHECK(strstr(bench.report, "FOUND") != NULL);
    CHECK(strstr(bench.report, "search only, nothing written") != NULL);
    CHECK(strstr(bench.report, "INA3221 0x40 CH1, 50ms windows") != NULL);
    CHECK(strstr(bench.report, "Not run: search only.") != NULL);
    CHECK(strstr(bench.report, "RESTORE") == NULL);
    unsigned lo = 0u, hi = 0u;
    const char *l = strstr(bench.report, "Low end:        bound at ");
    const char *h = strstr(bench.report, "High end:       bound at ");
    CHECK(l != NULL && h != NULL);
    if (l != NULL && h != NULL) {
        CHECK(sscanf(strstr(l, "endpoint"), "endpoint %u", &lo) == 1);
        CHECK(sscanf(strstr(h, "endpoint"), "endpoint %u", &hi) == 1);
        /* Inside the stops, by the 25us backoff and a step or two. */
        CHECK(lo > 1150u && lo < 1220u);
        CHECK(hi < 1880u && hi > 1810u);
    }
    /* The box after it: FOUND, and the two endpoints. */
    char want[64], a[16], z[16];
    snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
             ui_servo_str(SERVO_STR_L_FOUND));
    CHECK(lim_box_reads(0, want, UI_C_ACCENT));
    snprintf(a, sizeof(a), "%uus", lo);
    snprintf(z, sizeof(z), "%uus", hi);
    snprintf(want, sizeof(want), TR(SV_LIM_ENDS), a, z);
    CHECK(lim_box_reads(1, want, UI_C_TEXT));
    /* Under them the output they were found on, SERVO PWM GP2.  It stays
     * so when
     * the bench is switched to SAFE and another output is bound before
     * CLOSE: the ends belong to the run, not to the target now. */
    CHECK(lim_box_reads(2, "SERVO PWM GP2", UI_C_TEXT_DIM));
    /* And under the output the report's name: until the logger answers,
     * WRITING; with its number, LIMITnnn.TXT -- the limit run's prefix,
     * whatever the arm covers -- and never SERVOnnn.TXT or BENCHnnn.TXT. */
    CHECK(lim_box_reads(3, TR(SV_WRITING), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);
    CHECK(lim_box_reads(3, "LIMIT012.TXT", UI_C_TEXT_DIM));
    CHECK(!lim_box_reads(3, "SERVO012.TXT", UI_C_TEXT_DIM));
    CHECK(!lim_box_reads(3, "BENCH012.TXT", UI_C_TEXT_DIM));
    servo_screen_test_files(999, true);
    CHECK(lim_box_reads(3, "LIMIT999.TXT", UI_C_TEXT_DIM));
    servo_screen_test_files(-1, false);
    CHECK(lim_box_reads(3, TR(SV_NOT_RECORDED), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);
    servo_screen_set_armed(false);
    servo_screen_set_pins(PIN(5), true);
    bench_frames(100u);
    CHECK(lim_box_reads(2, "SERVO PWM GP2", UI_C_TEXT_DIM));
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_armed(true);
    bench_frames(100u);
    /* The card says how it ended and where the report is, and offers the
     * next run. */
    lim_page();
    CHECK(lim_line_reads(1, TR(SV_HOLD_START), UI_C_TEXT_FAINT));
    snprintf(want, sizeof(want), TR(SV_CARD_LAST),
             ui_servo_str(SERVO_STR_L_FOUND), "LIMIT012.TXT");
    CHECK(lim_line_reads(2, want, UI_C_TEXT_DIM));
}

/* A run's report taken from the outbox but its OPEN or END not answered
 * by the logger yet: no limit run and no automatic test starts, and the
 * page says the last report is still writing.  Answered, the automatic
 * test starts. */
TEST_CASE(no_run_starts_while_the_logger_holds_the_last_report)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    lim_run_out();
    servo_screen_test_logger_busy(true);
    bench_frames(100u);
    lim_hold(2.3f);                       /* TEST stays open */
    CHECK(!servo_screen_limit_running());
    CHECK(lim_line_reads(1, ui_servo_str(SERVO_STR_START_BUSY), UI_C_WARN));
    close_settings();
    hold_start(2.3f);
    CHECK(!servo_screen_testing());
    close_settings();
    servo_screen_test_logger_busy(false);
    bench_frames(100u);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
}

/* While the automatic test runs, START LIMIT RUN is grey and the page names
 * the test as the reason; only after the test has ended and the logger still
 * holds its report is the reason the report that is still writing. */
TEST_CASE(a_running_automatic_test_is_the_reason_the_limit_run_cannot_start)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        lim_bench();
        hold_start(2.3f);
        CHECK(servo_screen_testing());
        lim_page();
        CHECK(lim_line_reads(1, ui_servo_str(SERVO_STR_START_TEST_RUNS),
                             UI_C_WARN));
        CHECK(strcmp(ui_servo_str(SERVO_STR_START_TEST_RUNS),
                     ui_servo_str(SERVO_STR_START_BUSY)) != 0);
        close_settings();
        /* STOP TEST ends it; the logger still holds its report. */
        CHECK(scr->safe_at(BOXBTN_X, BOXBTN_Y));
        tap(BOXBTN_X, BOXBTN_Y);
        servo_screen_test_logger_busy(true);
        bench_frames(100u);
        CHECK(!servo_screen_testing());
        lim_page();
        CHECK(lim_line_reads(1, ui_servo_str(SERVO_STR_START_BUSY),
                             UI_C_WARN));
        close_settings();
        servo_screen_test_logger_busy(false);
        servo_screen_set_armed(false);
        bench_frames(100u);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A limit run started with no supply reading at all: a PD mini heard
 * during the run, its output on or off, beside the rail the run started
 * on, ends it as a changed supply, with the output asked off. */
TEST_CASE(a_pd_mini_heard_first_after_the_start_ends_a_limit_run)
{
    for (int on = 1; on >= 0; --on) {
        s_bench_no_first = true;
        s_bench_no_supply = true;
        lim_bench();
        s_bench_no_first = false;
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        bench_frames(1000u);
        CHECK(servo_screen_limit_running());
        s_bench_no_supply = false;
        bench.online = true;
        bench.out = on != 0;
        bench.rail_set = true;
        bench.rail_mv = (unsigned)lroundf(supply_screen_set_v() * 1000.0f);
        bench_frames(500u);
        CHECK(!servo_screen_limit_running());
        lim_run_out();
        CHECK(strstr(bench.report, "ABORTED") != NULL);
        CHECK(strstr(bench.report, "supply or rail voltage changed") != NULL);
        CHECK(bench.released);
    }
}

/* Each end of a limit run's watch, at a search: ABORTED, the output asked
 * off, the servo let go, and the report naming the reason.  A reset of the
 * screen, as at the panel's start, takes the run with it and asks
 * nothing, as it does for a test. */
TEST_CASE(every_abort_source_ends_a_limit_run_with_the_output_off)
{
    for (int how = 0; how < 9; ++how) {
        lim_bench();
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        bench_frames(3000u);
        CHECK(servo_screen_limit_running());
        bench.released = false;
        switch (how) {
        case 0:                         /* the meter to the PD mini */
            bench.ina = false;
            meter(SERVO_SOURCE_PDMINI);
            break;
        case 1:                         /* the meter to the model */
            bench.ina = false;
            meter(SERVO_SOURCE_MODEL);
            break;
        case 2:                         /* STOP and DISARM: the bench SAFE */
            servo_screen_set_armed(false);
            break;
        case 3:                         /* the link */
            servo_screen_set_link(false);
            break;
        case 4:                         /* the screen left */
            scr->leave();
            break;
        case 5:                         /* no window for 200ms */
            bench.ina = false;
            bench_frames(160u);
            CHECK(servo_screen_limit_running());
            bench_frames(60u);
            break;
        case 6: {                       /* the supply's set point moved */
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = true;
            st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = true;
            st.mode = SUPPLY_MODE_CV;
            st.set_v = supply_screen_set_v() + 0.5f;
            st.set_i = supply_screen_set_i();
            st.v = st.set_v;
            st.samples = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            servo_screen_supply(&st);
            break;
        }
        case 7:                         /* STOP RUN on the LIMIT RUN card */
            lim_page();
            tap(START_X, LIM_START_Y);
            close_settings();
            break;
        default:                        /* the screen reset under it */
            scr->reset();
            break;
        }
        CHECK(!servo_screen_limit_running());
        bench_frames(500u);
        if (how != 8) {                 /* a reset asks nothing, as for a test */
            CHECK(bench.released);
            CHECK(!bench.out);
            CHECK(strstr(bench.report, "ABORTED") != NULL);
        }
        if (how == 5) {
            CHECK(strstr(bench.report, "no INA3221 window for 0.2s")
                  != NULL);
            /* The box says it too, under ABORTED. */
            char want[64];
            snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
                     ui_servo_str(SERVO_STR_ABORTED));
            CHECK(lim_box_reads(0, want, UI_C_WARN));
            CHECK(lim_box_reads(1, ui_servo_str(SERVO_STR_AB_WIN_STALE),
                                UI_C_TEXT));
        }
        servo_screen_set_link(true);
    }
}


/* USER LOGS off: the limit run sends the logger nothing, and the box and
 * the LIMIT RUN card's LAST line name USER LOGS OFF for its report. */
TEST_CASE(a_limit_run_with_user_logs_off_writes_no_report)
{
    lim_bench();
    settings_set(SET_LOG_USER, 0.0f);
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    lim_run_out();
    CHECK_EQ(bench.opens, 0u);
    CHECK_EQ(bench.opens_txt, 0u);
    CHECK_EQ(bench.txt, 0u);
    CHECK_EQ(bench.ends, 0u);
    CHECK(lim_box_reads(3, TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);      /* an answer to an older run */
    CHECK(lim_box_reads(3, TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
    lim_page();
    char want[64];
    snprintf(want, sizeof(want), TR(SV_CARD_LAST),
             ui_servo_str(SERVO_STR_L_FOUND), TR(SV_USER_LOGS_OFF));
    CHECK(lim_line_reads(2, want, UI_C_TEXT_DIM));
}

int main(void)
{
    RUN(a_limit_run_starts_only_armed_on_the_ina3221);
    RUN(a_limit_run_needs_a_powered_rail_and_a_bound_output);
    RUN(programming_is_not_offered_off_the_programming_port);
    RUN(stop_run_is_named_to_the_router_while_the_run_is_under_way);
    RUN(the_limit_runs_stops_are_tap_targets_above_the_alert_band);
    RUN(a_search_only_limit_run_finds_both_ends_and_reports_them);
    RUN(no_run_starts_while_the_logger_holds_the_last_report);
    RUN(a_running_automatic_test_is_the_reason_the_limit_run_cannot_start);
    RUN(a_pd_mini_heard_first_after_the_start_ends_a_limit_run);
    RUN(every_abort_source_ends_a_limit_run_with_the_output_off);
    RUN(a_limit_run_with_user_logs_off_writes_no_report);
    return test_summary("servo_limit_panel");
}
