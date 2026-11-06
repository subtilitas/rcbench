/**
 * @file test_servo_rauto_panel.c
 * @brief RUDDER's AUTO box: AUTO in the ZERO row swaps LENGTH, the drive
 *        buttons and the state lines for the box and MANUAL brings them
 *        back; the box's places; a START that cannot run says why and
 *        drives nothing; the rows show their settings; every string fits
 *        its field in both languages, the lines wrapped beside the side
 *        view; the targets the side view draws.  The run on a servo is
 *        test_servo_rauto_kst.c's.
 *
 * Cases:
 * - auto_swaps_the_manual_controls_for_the_box_and_manual_brings_them_back
 * - the_box_lies_in_the_left_column_clear_of_the_zero_row_and_the_stop_floor
 * - a_start_that_cannot_run_says_why_and_drives_nothing
 * - every_string_fits
 * - the_targets_take_the_direction_the_run_measured
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
    /* START and the lines left of the side view, which no control of the
     * box covers. */
    const gfx_rect_t fl = rudder_flap_rect();
    CHECK(rauto_rect(RA_C_START).x + rauto_rect(RA_C_START).w < fl.x);
    CHECK(l.x + l.w < fl.x);
    for (int k = RA_C_LO; k < RA_C_COUNT; ++k) {
        CHECK(!overlap(rauto_rect((rauto_ctl_t)k), fl));
    }
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

/** @brief The longest a candidate list below holds. */
#define CAND_MAX 128

/** @brief Strings one of the box's lines can show, at their longest. */
typedef struct {
    char t[CAND_MAX][96]; /**< the strings  */
    int  n;               /**< their count  */
} cands_t;

/**
 * @brief @p t added to @p c, once.
 * @param c the list
 * @param t the string
 */
static void cand(cands_t *c, const char *t)
{
    for (int i = 0; i < c->n; ++i) {
        if (strcmp(c->t[i], t) == 0) {
            return;
        }
    }
    if (c->n < CAND_MAX) {
        snprintf(c->t[c->n++], sizeof(c->t[0]), "%s", t);
    }
}

/**
 * @brief The first line's strings: the step, the goals and every reason a
 *        START is refused for.
 * @param c the list
 */
static void first_lines(cands_t *c)
{
    char t[96];
    for (int ph = SERVO_RAUTO_PH_READ; ph <= SERVO_RAUTO_PH_CHECK; ++ph) {
        snprintf(t, sizeof(t), TR(RA_STEP),
                 ui_servo_str(servo_rauto_phase_str((servo_rauto_phase_t)ph)),
                 2000u, 8u, 8u, 17u);
        cand(c, t);
    }
    snprintf(t, sizeof(t), TR(RA_GOALS), 99.0, 99.0);
    cand(c, t);
    static const ui_text_id_t k_why[] = {
        TX_RA_NO_ANGLE, TX_RA_NO_ZERO, TX_RA_RANGE, TX_RA_PROG_PORT,
        TX_RA_REFUSED, TX_SV_LIM_PWM_OFF, TX_SV_LIM_PWM_SAFE,
        TX_SV_LIM_INA_SETUP, TX_SV_LIM_INA_OFFLINE, TX_SV_LIM_INA_NO_WIN,
        TX_SV_LIM_RAIL_LOW,
        TX_SV_LIM_PROG_PORT, TX_SV_LIM_PROG_BUSY, TX_SV_LIM_PROG_PD_OFF,
        TX_SV_LIM_PROG_LOW, TX_SV_LIM_PROG_RAIL, TX_SV_LIM_PROG_NO_PD,
        TX_HP_NEED_PD_C,
    };
    for (size_t k = 0; k < sizeof(k_why) / sizeof(k_why[0]); ++k) {
        cand(c, ui_tr(k_why[k]));
    }
    static const servo_str_t k_start[] = {
        SERVO_STR_START_NOT_ARMED, SERVO_STR_START_TEST_RUNS,
        SERVO_STR_START_LIMIT_RUNS, SERVO_STR_START_AUTO_RUNS,
        SERVO_STR_START_BUSY, SERVO_STR_START_PROG_5V,
    };
    for (size_t k = 0; k < sizeof(k_start) / sizeof(k_start[0]); ++k) {
        cand(c, ui_servo_str(k_start[k]));
    }
}

/**
 * @brief The second line's strings: PASS with its values, FAIL and
 *        ABORTED with every reason.
 * @param c the list
 */
static void second_lines(cands_t *c)
{
    char t[96], w[64];
    snprintf(t, sizeof(t), TR(RA_SET), 65535u, 65535u, 65535u, 65535u);
    cand(c, t);
    static servo_rauto_t r;
    for (int why = 1; why < (int)SERVO_RAUTO_WHY_COUNT; ++why) {
        for (int ab = 0; ab < (int)SERVO_TEST_AB_COUNT; ++ab) {
            memset(&r, 0, sizeof(r));
            r.why = (servo_rauto_why_t)why;
            r.test_why = (servo_test_abort_t)ab;
            if (why == SERVO_RAUTO_WHY_BINDS) {
                /* The IMU's widest, in hundredths. */
                snprintf(w, sizeof(w), TR(RA_BINDS_AT), "-180.00");
            } else {
                snprintf(w, sizeof(w), "%s",
                         ui_servo_str(servo_rauto_why_str(&r)));
            }
            snprintf(t, sizeof(t), TR(RA_LAST), ui_servo_str(SERVO_STR_FAIL),
                     w);
            cand(c, t);
        }
    }
}

/**
 * @brief The third line's strings: the servo, the report, RESTORE.
 * @param c the list
 */
static void third_lines(cands_t *c)
{
    char t[96];
    snprintf(t, sizeof(t), TR(RA_SERVO), TR(SV_LIM_SERVO_KST));
    cand(c, t);
    for (int k = 0; k < 2; ++k) {
        snprintf(t, sizeof(t), TR(RA_SERVO_HITEC),
                 ui_setting_option(SET_HITEC_SERIES, k));
        cand(c, t);
    }
    snprintf(t, sizeof(t), TR(RA_FILE), "RUDDR999.TXT");
    cand(c, t);
    snprintf(t, sizeof(t), TR(RA_FILE), TR(SV_NOT_RECORDED));
    cand(c, t);
    snprintf(t, sizeof(t), TR(SV_LIM_RESTORE), TR(KP_PROGRAM));
    cand(c, t);
    snprintf(t, sizeof(t), TR(SV_LIM_RESTORE_HITEC), "HITEC999.TXT");
    cand(c, t);
    cand(c, TR(SV_LIM_NO_BACKUP));
    cand(c, TR(SV_USER_LOGS_OFF));
}

/* Every string the box draws, at its longest, in both languages: the rows
 * at their highest values, START and STOP RUN, AUTO and MANUAL; and every
 * first line with every second and every third, wrapped beside the side
 * view, above the alert band in one face or the other. */
TEST_CASE(every_string_fits)
{
    const gfx_font_t *f = &gfx_font_10x20;
    char t[128];
    static cands_t l0, l1, l2;
    int small = 0;
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
        memset(&l0, 0, sizeof(l0));
        memset(&l1, 0, sizeof(l1));
        memset(&l2, 0, sizeof(l2));
        first_lines(&l0);
        second_lines(&l1);
        third_lines(&l2);
        CHECK(l0.n < CAND_MAX && l1.n < CAND_MAX && l2.n < CAND_MAX);
        for (int i = 0; i < l0.n; ++i) {
            for (int j = 0; j < l1.n; ++j) {
                for (int k = 0; k < l2.n; ++k) {
                    const char *const three[RA_LINES] = { l0.t[i], l1.t[j],
                                                          l2.t[k] };
                    int pitch = 0;
                    const gfx_font_t *face = rauto_lines_face(three, &pitch);
                    if (face == NULL) {
                        T_FAIL("lang %d: \"%s\" / \"%s\" / \"%s\" past "
                               "the alert band", lang, l0.t[i], l1.t[j],
                               l2.t[k]);
                        return;
                    }
                    small += face == &gfx_font_8x16;
                    CHECK_EQ(pitch, face == &gfx_font_8x16 ? 16 : 20);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* The longest take the small face, the German Hitec refusal of 78
     * cells among them. */
    CHECK(small > 0);
    /* One short line alone takes the label face; NULL takes none. */
    const char *const one[RA_LINES] = { "ARM FIRST", "", "" };
    int pitch = 0;
    CHECK(rauto_lines_face(one, &pitch) == &gfx_font_10x20);
    CHECK_EQ(pitch, 20);
    CHECK(rauto_lines_face(one, NULL) == &gfx_font_10x20);
    CHECK(rauto_lines_face(NULL, &pitch) == NULL);
    /* Lines past any room: none fits. */
    static char big[RA_LINES][96];
    for (int i = 0; i < RA_LINES; ++i) {
        memset(big[i], 'M', 95);
        for (int k = 9; k < 95; k += 10) {
            big[i][k] = ' ';
        }
        big[i][95] = '\0';
    }
    const char *const huge[RA_LINES] = { big[0], big[1], big[2] };
    CHECK(rauto_lines_face(huge, &pitch) == NULL);
}

/* The targets as the side view draws them: 1000us negative and 2000us
 * positive before a run measured them; a side the run measured the other
 * way round is drawn that way; NULL takes nothing. */
TEST_CASE(the_targets_take_the_direction_the_run_measured)
{
    fresh();
    scr->enter();
    to_auto();
    settings_set(SET_RUDDER_DEFL_LO, 15.0f);
    settings_set(SET_RUDDER_DEFL_HI, 25.5f);
    float d[2] = { 0.0f, 0.0f };
    CHECK(rauto_targets(d));
    CHECK_NEAR(d[0], -15.0f, 1e-4f);
    CHECK_NEAR(d[1], 25.5f, 1e-4f);
    CHECK(!rauto_targets(NULL));
}

int main(void)
{
    RUN(auto_swaps_the_manual_controls_for_the_box_and_manual_brings_them_back);
    RUN(the_box_lies_in_the_left_column_clear_of_the_zero_row_and_the_stop_floor);
    RUN(a_start_that_cannot_run_says_why_and_drives_nothing);
    RUN(every_string_fits);
    RUN(the_targets_take_the_direction_the_run_measured);
    return test_summary("servo_rauto_panel");
}
