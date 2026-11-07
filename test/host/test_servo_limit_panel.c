/*
 * What the SERVO screen's limit run decides: START LIMIT RUN on
 * TEST's LIMIT RUN card, what refuses it, its report, every source
 * that aborts it, and the knob moving nothing while it runs.
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
#include "ui_board.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------------------ the limit run */

/*
 * LIMIT RUN's left card, mirrored from servo_limit_card.c: the output and
 * meter line at y 40, 20px tall; the state lines from y 178, each 24px
 * with its text centred in it; all in the 10 x 20 face 10px into the card
 * (x UI_MARGIN + 10), across 600px.
 */
#define LIM_X        (UI_MARGIN + 10)
#define LIM_OUT_Y    40
#define LIM_STATE_Y(i) (178 + 24 * (i))

/* Buffer 0 drawn with LIMIT RUN open, and whether its line @p i -- 0 the
 * output and meter line, 1 on the state lines from the first -- reads
 * @p text in @p col on the card's fill, and nothing else across it. */
static bool lim_line_reads(int i, const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    const int y = (i == 0) ? LIM_OUT_Y : LIM_STATE_Y(i - 1);
    const int h = (i == 0) ? 20 : 24;
    const gfx_color_t bg = fb[(size_t)y * W + LIM_X - 4];
    gfx_fill_rect(&want_cv, LIM_X, (int16_t)y, 600, (int16_t)h, bg);
    gfx_text_in(&want_cv, gfx_rect_make(LIM_X, y, 600, h), text,
                &gfx_font_10x20, ui_theme_color(col), 1, GFX_ALIGN_LEFT);
    return same_in(fb, want_fb, LIM_X, y, 600, h);
}

/* Whether a state line of LIMIT RUN's card reads @p text, in @p col: the
 * line found by its text (servo_screen_limit_line()), then its pixels. */
static bool lim_card_reads(const char *text, ui_color_id_t col)
{
    open_limrun();
    for (int i = 0; i < 8; ++i) {
        char b[96];
        if (strcmp(servo_screen_limit_line(i, b, sizeof(b)), text) == 0) {
            return lim_line_reads(i + 1, text, col);
        }
    }
    return false;
}

/* LIMIT RUN's end cell @p k (0 LOW END, 1 HIGH END), 295 x 96px from x
 * UI_MARGIN + 10 and y 70, 305px apart: whether it reads @p us in the 32 x
 * 44 digits 12px in and 40px down. */
static bool lim_end_reads(int k, unsigned us)
{
    open_limrun();
    scr->render(&cv, 0);
    want_fresh();
    const int x = LIM_X + 305 * k + 12, y = 70 + 40;
    char num[8];
    snprintf(num, sizeof(num), "%u", us);
    const int w = gfx_text_width(&gfx_font_num_32x44, num, 1);
    const gfx_color_t bg = fb[(size_t)(y - 4) * W + x];
    gfx_fill_rect(&want_cv, (int16_t)x, (int16_t)y, (int16_t)w, 44, bg);
    gfx_text(&want_cv, x, y, num, &gfx_font_num_32x44,
             ui_theme_color(UI_C_TEXT), 1);
    return same_in(fb, want_fb, x, y, w, 44);
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
    hold_start_on(1, secs);
}

/* LIMIT RUN's screen, its card in sight. */
static void lim_page(void)
{
    open_limrun();
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

/* ARM + START starts nothing until the INA3221 is the servo rail's meter,
 * and the card says what is missing; a tap or a hold under 2s starts
 * nothing either.  The PD mini and the model as the meter are refused: the
 * run reads the INA3221 or nothing.  A SAFE bench refuses nothing: the
 * hold arms it. */
TEST_CASE(a_limit_run_starts_only_on_the_ina3221)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        /* The INA3221 off in SETUP, with a SERVO PWM output bound: the
         * TARGET row's words come before the meter's. */
        bench_fresh();
        bench.out = true;
        servo_screen_set_pins(PIN(2), true);
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
        /* Not armed: no reason on the card, and ARM + START arms the
         * bench itself. */
        lim_bench();
        servo_screen_set_armed(false);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_SEARCH_NOTE), UI_C_TEXT_FAINT));
        const unsigned arms = bench.arms;
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        CHECK_EQ(bench.arms, arms + 1u);
        servo_screen_set_armed(false);
        CHECK(!servo_screen_limit_running());
        close_settings();
        /* The port without its pulse on an ARMED bench: PORT PWM ON waits
         * for SAFE, as a pulse comes on only on a bench at rest; PORT PWM
         * OFF asks for the DISARM before TARGET. */
        lim_bench();
        servo_screen_set_drive(1u, 13u, false);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_PWM_SAFE), UI_C_WARN));
        close_settings();
        settings_set(SET_SERVO_PORT_PWM, 0.0f);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_PWM_OFF_ARMED), UI_C_WARN));
        close_settings();
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
        /* Ready: the card names no reason, only what the mode does, and
         * only the whole hold starts it. */
        lim_bench();
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_SEARCH_NOTE), UI_C_TEXT_FAINT));
        /* The line names the output and the channel it reads. */
        char meter_line[96];
        snprintf(meter_line, sizeof(meter_line), TR(SV_LIM_LINE),
                 "SERVO PWM GPIO 2", "INA3221 0x40 CH1");
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
        CHECK(lim_line_reads(1, TR(SV_LIM_SEARCH_NOTE), UI_C_TEXT_FAINT));
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

/* On the rbmod carrier a channel watches a rail, and the bench's outputs
 * are on a rail once one is bound as SERVO PWM: with none bound no channel
 * watches the target, and the line names the TARGET row's words, not an
 * INA3221 that is not set up.  Bound on PWM01 (GPIO 2) the run reads CH2,
 * RAIL A, and starts. */
TEST_CASE(on_the_carrier_no_servo_pwm_output_is_named_as_such)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        lim_bench();
        ui_board_set(OUTBIND_BOARD_RBMOD);
        (void)ui_board_apply_pins(NULL);
        settings_set(SET_SERVO_TARGET, 0.0f);
        /* What the panel does with each binding read (panel_frame.c). */
        servo_screen_set_pins(0u, true);
        (void)settings_sensors_set_tester_rail(ui_board_tester_rail(0u));
        CHECK_EQ(settings_sensors_fast(), LINK_SM_FAST_NONE);
        bench_frames(100u);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_TARGET_NONE), UI_C_WARN));
        close_settings();
        lim_hold(2.3f);
        CHECK(!servo_screen_limit_running());
        close_settings();
        servo_screen_set_pins(PIN(2), true);
        (void)settings_sensors_set_tester_rail(ui_board_tester_rail(PIN(2)));
        CHECK_EQ(settings_sensors_fast(), 1u);
        bench_frames(100u);
        lim_page();
        CHECK(lim_line_reads(1, TR(SV_LIM_SEARCH_NOTE), UI_C_TEXT_FAINT));
        close_settings();
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        servo_screen_set_armed(false);
        (void)settings_sensors_set_tester_rail(LINK_SM_WATCH_NONE);
        ui_board_set(OUTBIND_BOARD_PICO_HEADER);
        (void)ui_board_apply_pins(NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The bench's own output is no programming port: MODE in LIMIT RUN's list
 * is locked with the reason, a step keeps SEARCH ONLY, and the card names
 * no reason, only what SEARCH ONLY does. */
TEST_CASE(programming_is_not_offered_off_the_programming_port)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        lim_bench();
        open_limrun_list();
        CHECK(servo_screen_list()->open);
        sl_tap(LL_MODE);
        CHECK(!servo_screen_limit_prog());
        CHECK(home_tag());
        CHECK(lim_line_reads(1, TR(SV_LIM_SEARCH_NOTE), UI_C_TEXT_FAINT));
        CHECK(lim_line_reads(2, "", UI_C_WARN));
        close_settings();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* What the screen names to the router as a press that stops the limit
 * run: ARM + START's place, STOP RUN while the run is under way.  Nothing
 * once the run has ended. */
TEST_CASE(stop_run_is_named_to_the_router_while_the_run_is_under_way)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    CHECK(scr->safe_at(LIM_START_X, LIM_START_Y));
    tap(LIM_START_X, LIM_START_Y);
    CHECK(!servo_screen_limit_running());
    CHECK(!scr->safe_at(LIM_START_X, LIM_START_Y));
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

/* While the run is under way, STOP RUN in the footer is a tap target of
 * UI_TAP_MIN rows or more, and it ends above the alert band. */
TEST_CASE(the_limit_runs_stops_are_tap_targets_above_the_alert_band)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    CHECK(stop_rows_at(LIM_START_X) >= UI_TAP_MIN);
    CHECK_EQ(stops_under_the_band(), 0);
    tap(LIM_START_X, LIM_START_Y);
    CHECK(!servo_screen_limit_running());
    close_settings();
}

/* SEARCH ONLY on the INA3221: both ends found, the servo let go, the
 * bench SAFE, nothing else asked of it, and LIMITnnn.TXT opened alone
 * with the limit report: both endpoints inside the servo's stops, nothing
 * written. */
TEST_CASE(a_search_only_limit_run_finds_both_ends_and_reports_them)
{
    lim_bench();
    lim_hold(2.3f);
    CHECK(servo_screen_limit_running());
    /* The card while it runs: the search, STOP RUN in the footer. */
    CHECK(lim_card_reads(TR(SV_LIM_TITLE), UI_C_ACCENT));
    lim_run_out();
    CHECK(bench.released);
    CHECK(bench.out);                       /* a search leaves it on */
    CHECK_EQ(bench.disarms, 1u);            /* and the bench SAFE */
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
    /* The card after it: FOUND, and the two endpoints in their cells. */
    char want[64];
    snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
             ui_servo_str(SERVO_STR_L_FOUND));
    CHECK(lim_card_reads(want, UI_C_ACCENT));
    CHECK(lim_end_reads(0, lo));
    CHECK(lim_end_reads(1, hi));
    /* Under the verdict the output they were found on, SERVO PWM GPIO 2.
     * It stays so when the bench is switched to SAFE and another output is
     * bound: the ends belong to the run, not to the target now. */
    CHECK(lim_card_reads("SERVO PWM GPIO 2", UI_C_TEXT_DIM));
    /* And under the output the report's name: until the logger answers,
     * WRITING; with its number, LIMITnnn.TXT -- the limit run's prefix,
     * whatever the arm covers -- and never SERVOnnn.TXT or BENCHnnn.TXT. */
    CHECK(lim_card_reads(TR(SV_WRITING), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);
    CHECK(lim_card_reads("LIMIT012.TXT", UI_C_TEXT_DIM));
    CHECK(!lim_card_reads("SERVO012.TXT", UI_C_TEXT_DIM));
    CHECK(!lim_card_reads("BENCH012.TXT", UI_C_TEXT_DIM));
    servo_screen_test_files(999, true);
    CHECK(lim_card_reads("LIMIT999.TXT", UI_C_TEXT_DIM));
    servo_screen_test_files(-1, false);
    CHECK(lim_card_reads(TR(SV_NOT_RECORDED), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);
    servo_screen_set_armed(false);
    servo_screen_set_pins(PIN(5), true);
    bench_frames(100u);
    CHECK(lim_card_reads("SERVO PWM GPIO 2", UI_C_TEXT_DIM));
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_armed(true);
    bench_frames(100u);
    /* The card names no reason first: it says how the run ended and
     * where the report is. */
    lim_page();
    snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
             ui_servo_str(SERVO_STR_L_FOUND));
    CHECK(lim_line_reads(1, want, UI_C_ACCENT));
    CHECK(lim_card_reads("LIMIT012.TXT", UI_C_TEXT_DIM));
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
    /* The limit run's end left the bench SAFE: TEST's START asks ARM. */
    servo_screen_set_armed(false);
    bench_frames(100u);
    servo_screen_set_armed(true);
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
        open_test();
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
        case 7:                         /* STOP RUN in LIMIT RUN's footer */
            lim_page();
            tap(LIM_START_X, LIM_START_Y);
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
            /* The card says it too, under ABORTED. */
            char want[64];
            snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
                     ui_servo_str(SERVO_STR_ABORTED));
            CHECK(lim_card_reads(want, UI_C_WARN));
            CHECK(lim_card_reads(ui_servo_str(SERVO_STR_AB_WIN_STALE),
                                UI_C_TEXT));
        }
        servo_screen_set_link(true);
    }
}


/* USER LOGS off: the limit run sends the logger nothing, and the card
 * names USER LOGS OFF for its report. */
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
    CHECK(lim_card_reads(TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
    servo_screen_test_files(12, true);      /* an answer to an older run */
    CHECK(lim_card_reads(TR(SV_USER_LOGS_OFF), UI_C_TEXT_DIM));
}

/* The pulse the bench drives, every 20ms for @p n frames, into @p trace;
 * with @p turn a knob turn before each frame, +0.2 and -0.1 of the span in
 * turn. */
static void lim_trace(bool turn, uint16_t *trace, size_t n)
{
    for (size_t k = 0u; k < n; ++k) {
        if (turn) {
            servo_screen_knob_frame();
            servo_screen_knob((k % 2u == 0u) ? 0.2f : -0.1f);
        }
        bench_frames(20u);
        trace[k] = bench.cmd;
    }
}

/* The limit run owns the horn: a knob turn on LIVE, ARMED, while it runs
 * posts nothing, and the run drives the same pulses as without the turns.
 * Before the run the same turn moves the horn. */
TEST_CASE(the_knob_moves_nothing_while_a_limit_run_drives_the_horn)
{
    enum { N = 150 };                       /* 3s of frames */
    static uint16_t still[N], turned[N];
    for (int turn = 0; turn < 2; ++turn) {
        lim_bench();
        close_settings();
        const uint16_t was = bench.cmd;
        servo_screen_knob_frame();
        servo_screen_knob(0.2f);
        bench_frames(20u);
        CHECK(bench.cmd != was);            /* LIVE, ARMED: the knob moves it */
        lim_bench();
        lim_hold(2.3f);
        CHECK(servo_screen_limit_running());
        close_settings();
        CHECK(lim_card_reads(TR(SV_LIM_TITLE), UI_C_ACCENT));
        lim_trace(turn != 0, turn ? turned : still, N);
        CHECK(servo_screen_limit_running());
    }
    unsigned moved = 0u;
    for (int k = 0; k < N; ++k) {
        CHECK_EQ(turned[k], still[k]);
        moved += (still[k] != still[0]) ? 1u : 0u;
    }
    CHECK(moved > 0u);                      /* the search moved it */
}

int main(void)
{
    RUN(a_limit_run_starts_only_on_the_ina3221);
    RUN(a_limit_run_needs_a_powered_rail_and_a_bound_output);
    RUN(on_the_carrier_no_servo_pwm_output_is_named_as_such);
    RUN(programming_is_not_offered_off_the_programming_port);
    RUN(stop_run_is_named_to_the_router_while_the_run_is_under_way);
    RUN(the_limit_runs_stops_are_tap_targets_above_the_alert_band);
    RUN(a_search_only_limit_run_finds_both_ends_and_reports_them);
    RUN(no_run_starts_while_the_logger_holds_the_last_report);
    RUN(a_running_automatic_test_is_the_reason_the_limit_run_cannot_start);
    RUN(a_pd_mini_heard_first_after_the_start_ends_a_limit_run);
    RUN(every_abort_source_ends_a_limit_run_with_the_output_off);
    RUN(a_limit_run_with_user_logs_off_writes_no_report);
    RUN(the_knob_moves_nothing_while_a_limit_run_drives_the_horn);
    return test_summary("servo_limit_panel");
}
