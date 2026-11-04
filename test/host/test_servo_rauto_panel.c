/**
 * @file test_servo_rauto_panel.c
 * @brief RUDDER's AUTO box: AUTO in the ZERO row swaps LENGTH, the drive
 *        buttons and the state lines for the box and MANUAL brings them
 *        back; the box's places; a START that cannot run says why and
 *        drives nothing; the rows show their settings; every string fits
 *        its field in both languages.  The run on a servo is
 *        test_servo_rauto_kst.c's.
 *
 * Cases:
 * - auto_swaps_the_manual_controls_for_the_box_and_manual_brings_them_back
 * - the_box_lies_in_the_left_column_clear_of_the_zero_row_and_the_stop_floor
 * - a_start_that_cannot_run_says_why_and_drives_nothing
 * - every_string_fits
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_rudder_auto.h"
#include "servo_rudder_auto_int.h"
#include "servo_rudder_int.h"

/** @brief The drawn controls' font, 10 x 20. */
#define CELL 10

/** @brief A tap on RUDDER's tab, then AUTO. */
static void to_auto(void)
{
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
}

/** @brief Whether two rectangles share a pixel. */
static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

/** @brief The command waiting, drained. @return its kind */
static servo_cmd_kind_t taken_kind(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_cmd_t next;
    while (servo_screen_take(&next)) {
        c = next;
    }
    return c.kind;
}

/* AUTO shows the box: LENGTH and the three drive buttons have no place,
 * the box's rows do; the readout and the ZERO row stay.  MANUAL, in
 * AUTO's place, brings them back.  Another task and back keeps the view. */
TEST_CASE(auto_swaps_the_manual_controls_for_the_box_and_manual_brings_them_back)
{
    fresh();
    scr->enter();
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    CHECK(!rauto_shown());
    CHECK(rudder_rect(RUD_C_LENGTH).w > 0);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(rauto_shown());
    for (int k = RUD_C_LENGTH; k <= RUD_C_2000; ++k) {
        CHECK_EQ(rudder_rect((rudder_ctl_t)k).w, 0);
    }
    CHECK(rudder_rect(RUD_C_ZERO).w > 0);
    CHECK(rudder_rect(RUD_C_RESET).w > 0);
    for (int k = RA_C_LO; k < RA_C_COUNT; ++k) {
        CHECK(rauto_rect((rauto_ctl_t)k).w > 0);
    }
    scr->render(&cv, 0);
    tap(SV_TAB_LIVE_X, SV_TAB_Y);
    CHECK(!rauto_shown());
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    CHECK(rauto_shown());
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(!rauto_shown());
    CHECK(rudder_rect(RUD_C_1000).w > 0);
    scr->render(&cv, 0);
}

/* The box's controls lie inside x 18 to 518, each at least the tap
 * minimum tall, none on another, on the ZERO row or on the readout; the
 * lines end above the STOP floor's band. */
TEST_CASE(the_box_lies_in_the_left_column_clear_of_the_zero_row_and_the_stop_floor)
{
    fresh();
    scr->enter();
    to_auto();
    const gfx_rect_t keep[] = { rudder_rect(RUD_C_ZERO),
                                rudder_rect(RUD_C_RESET),
                                rudder_rect(RUD_C_AUTO),
                                rudder_readout_rect() };
    for (int k = RA_C_LO; k < RA_C_COUNT; ++k) {
        const gfx_rect_t r = rauto_rect((rauto_ctl_t)k);
        CHECK(r.x >= 18 && r.x + r.w <= 518);
        CHECK(r.h >= UI_TAP_MIN);
        for (int j = k + 1; j < RA_C_COUNT; ++j) {
            CHECK(!overlap(r, rauto_rect((rauto_ctl_t)j)));
        }
        for (size_t j = 0; j < sizeof(keep) / sizeof(keep[0]); ++j) {
            CHECK(!overlap(r, keep[j]));
        }
        CHECK(!overlap(r, rauto_lines_rect()));
    }
    const gfx_rect_t l = rauto_lines_rect();
    CHECK(l.x >= 18 && l.x + l.w <= 518);
    CHECK(l.y + l.h <= UI_BODY_H);
    CHECK_EQ(rauto_rect(RA_C_NONE).w, 0);
    CHECK_EQ(rauto_rect(RA_C_COUNT).w, 0);
}

/* START on a bench that is not ARMED, then on one ARMED without a KST
 * servo on the programming port: the tap names why on the first line, no
 * hold begins and nothing is driven, also after 2.5s held. */
TEST_CASE(a_start_that_cannot_run_says_why_and_drives_nothing)
{
    char b[96];
    for (int armed = 0; armed < 2; ++armed) {
        if (armed) {
            fresh_armed();
        } else {
            fresh();
        }
        scr->enter();
        to_auto();
        (void)taken_kind();
        const char *why = rauto_blocked();
        CHECK(why != NULL);
        if (!armed) {
            CHECK_STR_EQ(why, ui_servo_str(SERVO_STR_START_NOT_ARMED));
        }
        const gfx_rect_t r = rauto_rect(RA_C_START);
        ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
        frames(2.5f);
        ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
        frames(0.1f);
        CHECK(!servo_screen_rauto_running());
        CHECK_STR_EQ(rauto_line(0, b, sizeof(b)), why);
        CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), "");
        /* The servo a run started now programs. */
        CHECK_STR_EQ(rauto_line(2, b, sizeof(b)), "SERVO KST/CHASERVO");
        CHECK_STR_EQ(rauto_line(3, b, sizeof(b)), "");
        CHECK_STR_EQ(rauto_line(0, NULL, 0), "");
        char keep[4] = "abc";
        CHECK_STR_EQ(rauto_line(0, keep, 0), "");
        CHECK_STR_EQ(keep, "abc");          /* nothing written */
        CHECK_EQ(taken_kind(), SERVO_CMD_NONE);
        scr->render(&cv, 0);
    }
}

/** @brief Whether @p t fits a line of the box, 500px, in the 10 x 20
 *         face. */
static bool line_fits(const char *t)
{
    return gfx_text_width(&gfx_font_10x20, t, 1) <= 500;
}

/** @brief Whether @p t fits a line in the 8 x 16 face the box falls back
 *         to for a longer line. */
static bool line_fits_small(const char *t)
{
    return gfx_text_width(&gfx_font_8x16, t, 1) <= 500;
}

/* Every string the box draws, at its longest, in both languages: the rows
 * at their highest values, START and STOP RUN, AUTO and MANUAL, the step
 * line, the result lines with every reason, the report's line, RESTORE
 * and every reason a START is refused for. */
TEST_CASE(every_string_fits)
{
    const gfx_font_t *f = &gfx_font_10x20;
    char t[128], w[64];
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        const gfx_rect_t half = rauto_rect(RA_C_LO);
        CHECK(ui_text_fits(f, "2000us 90.0° 500.0mm", half.w, half.h, 8));
        snprintf(t, sizeof(t), TR(RA_MARGIN), 50u);
        CHECK(ui_text_fits(f, t, half.w, half.h, 8));
        snprintf(t, sizeof(t), TR(RA_TOL), 5.0);
        CHECK(ui_text_fits(f, t, half.w, half.h, 8));
        const gfx_rect_t st = rauto_rect(RA_C_START);
        CHECK(ui_text_fits(f, TR(RA_START), st.w, st.h, 8));
        CHECK(ui_text_fits(f, TR(SV_LIM_STOP), st.w, st.h, 8));
        CHECK(ui_text_fits(f, TR(RA_AUTO), 160, 48, 8));
        CHECK(ui_text_fits(f, TR(RA_MANUAL), 160, 48, 8));
        for (int ph = SERVO_RAUTO_PH_READ; ph <= SERVO_RAUTO_PH_CHECK; ++ph) {
            snprintf(t, sizeof(t), TR(RA_STEP),
                     ui_servo_str(servo_rauto_phase_str(
                         (servo_rauto_phase_t)ph)),
                     2000u, 8u, 8u, 17u);
            CHECK(line_fits(t));
        }
        snprintf(t, sizeof(t), TR(RA_SET), 65535u, 65535u, 65535u, 65535u);
        CHECK(line_fits(t));
        snprintf(t, sizeof(t), TR(RA_GOALS), 99.0, 99.0);
        CHECK(line_fits(t));
        static servo_rauto_t r;
        for (int why = 1; why < (int)SERVO_RAUTO_WHY_COUNT; ++why) {
            for (int ab = 0; ab < (int)SERVO_TEST_AB_COUNT; ++ab) {
                memset(&r, 0, sizeof(r));
                r.why = (servo_rauto_why_t)why;
                r.test_why = (servo_test_abort_t)ab;
                if (why == SERVO_RAUTO_WHY_BINDS) {
                    snprintf(w, sizeof(w), TR(RA_BINDS_AT), -89.9);
                } else {
                    snprintf(w, sizeof(w), "%s",
                             ui_servo_str(servo_rauto_why_str(&r)));
                }
                snprintf(t, sizeof(t), TR(RA_LAST),
                         ui_servo_str(SERVO_STR_ABORTED), w);
                CHECK(line_fits(t));
                snprintf(t, sizeof(t), TR(RA_LAST),
                         ui_servo_str(SERVO_STR_FAIL), w);
                CHECK(line_fits(t));
            }
        }
        snprintf(t, sizeof(t), TR(RA_FILE), "RUDDR999.TXT");
        CHECK(line_fits(t));
        snprintf(t, sizeof(t), TR(RA_FILE), TR(SV_NOT_RECORDED));
        CHECK(line_fits(t));
        snprintf(t, sizeof(t), TR(SV_LIM_RESTORE), TR(KP_PROGRAM));
        CHECK(line_fits(t));
        static const ui_text_id_t k_why[] = {
            TX_RA_NO_ANGLE, TX_RA_NO_ZERO, TX_RA_RANGE, TX_RA_PROG_PORT,
            TX_RA_REFUSED, TX_SV_LIM_PWM_OFF,
        };
        for (size_t k = 0; k < sizeof(k_why) / sizeof(k_why[0]); ++k) {
            CHECK(line_fits(ui_tr(k_why[k])));
        }
        /* The limit run's refusals, some past 50 cells: the 8 x 16 face. */
        static const ui_text_id_t k_lim[] = {
            TX_SV_LIM_PROG_PORT, TX_SV_LIM_PROG_BUSY, TX_SV_LIM_PROG_PD_OFF,
            TX_SV_LIM_PROG_LOW, TX_SV_LIM_PROG_RAIL, TX_SV_LIM_PROG_NO_PD,
        };
        for (size_t k = 0; k < sizeof(k_lim) / sizeof(k_lim[0]); ++k) {
            CHECK(line_fits_small(ui_tr(k_lim[k])));
        }
        static const servo_str_t k_start[] = {
            SERVO_STR_START_NOT_ARMED, SERVO_STR_START_TEST_RUNS,
            SERVO_STR_START_LIMIT_RUNS, SERVO_STR_START_AUTO_RUNS,
            SERVO_STR_START_BUSY, SERVO_STR_START_PROG_5V,
        };
        for (size_t k = 0; k < sizeof(k_start) / sizeof(k_start[0]); ++k) {
            CHECK(line_fits(ui_servo_str(k_start[k])));
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(auto_swaps_the_manual_controls_for_the_box_and_manual_brings_them_back);
    RUN(the_box_lies_in_the_left_column_clear_of_the_zero_row_and_the_stop_floor);
    RUN(a_start_that_cannot_run_says_why_and_drives_nothing);
    RUN(every_string_fits);
    return test_summary("servo_rauto_panel");
}
