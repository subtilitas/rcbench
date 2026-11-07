/**
 * @file test_servo_ready.c
 * @brief RUDDER AUTO's READY card (servo_ready.c,
 *        servo_rudder_auto_ready.c): every text of the start gate
 *        belongs to exactly one row, in both languages; on each bench
 *        state the gate's text is on its row and no row shows another
 *        row's; the hold refuses on the first red row and names it in the
 *        alert band; the rows ARM + START does itself read amber; a tap on
 *        PORT opens the port list; every label and state fits its column.
 *
 * Cases:
 * - every_gate_text_belongs_to_exactly_one_row
 * - on_each_bench_the_gate_text_is_on_its_row_alone
 * - the_hold_refuses_on_the_first_red_row_and_names_it
 * - the_rows_the_hold_does_read_amber_only_while_safe
 * - a_tap_on_port_opens_the_port_list
 * - every_label_and_state_fits_its_column
 * - the_card_takes_the_output_cards_place_on_rudder_auto_only
 * - a_refused_hold_has_a_red_row_naming_it
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_rudder_auto_int.h"
#include "servo_ready_int.h"
#include "servo_rudder_int.h"
#include "ui_text.h"
#include "ui_widgets.h"

/** @brief A text the gate returns, and the row it belongs to. */
typedef struct {
    int         kind;  /**< 0 a TX_ text, 1 a SERVO_STR_ text */
    int         id;    /**< its ID                             */
    rauto_row_t row;   /**< its row                            */
} gate_text_t;

/** @brief Every text rauto_blocked() can return, listed here on its own,
 *         with the row it belongs to. */
static const gate_text_t k_gate[] = {
    { 1, SERVO_STR_START_PROG_5V, RAR_SUPPLY },
    { 1, SERVO_STR_START_NOT_ARMED, RAR_ARM },
    { 1, SERVO_STR_START_TEST_RUNS, RAR_ARM },
    { 1, SERVO_STR_START_LIMIT_RUNS, RAR_ARM },
    { 1, SERVO_STR_START_BUSY, RAR_ARM },
    { 0, TX_RA_PROG_PORT, RAR_PORT },
    { 0, TX_SV_LIM_PROG_PORT, RAR_PORT },
    { 0, TX_SV_LIM_INA_SETUP, RAR_METER },
    { 0, TX_SV_LIM_INA_OFFLINE, RAR_METER },
    { 0, TX_SV_LIM_INA_NO_WIN, RAR_METER },
    { 0, TX_OUT_PORT_HELD, RAR_PROG },
    { 0, TX_SV_LIM_PROG_BUSY, RAR_PROG },
    { 0, TX_SV_LIM_PWM_SAFE, RAR_PWM },
    { 0, TX_SV_LIM_PWM_OFF, RAR_PWM },
    { 0, TX_SV_LIM_PWM_OFF_ARMED, RAR_PWM },
    { 0, TX_RA_RANGE, RAR_RANGE },
    { 0, TX_HP_NEED_PD_C, RAR_SUPPLY },
    { 0, TX_SV_LIM_PROG_PD_OFF, RAR_SUPPLY },
    { 0, TX_SV_LIM_PROG_LOW, RAR_SUPPLY },
    { 0, TX_SV_LIM_PROG_RAIL, RAR_SUPPLY },
    { 0, TX_SV_LIM_PROG_NO_PD, RAR_SUPPLY },
    { 0, TX_SV_TAP_HV, RAR_SUPPLY },
    { 0, TX_RA_NO_ANGLE, RAR_ANGLE },
    { 0, TX_RA_NO_ZERO, RAR_ZERO },
};
#define N_GATE ((int)(sizeof(k_gate) / sizeof(k_gate[0])))

/** @brief Gate text @p g in the language showing. */
static const char *gate_str(const gate_text_t *g)
{
    return g->kind == 0 ? ui_tr((ui_text_id_t)g->id)
                        : ui_servo_str((servo_str_t)g->id);
}

/** @brief RUDDER AUTO shown on a fresh bench, SAFE or ARMED. */
static void to_auto(bool armed)
{
    if (armed) {
        fresh_armed();
    } else {
        fresh();
    }
    scr->enter();
    go(SERVO_FN_RAUTO);
    ui_router_set_alert(NULL);
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/**
 * @brief The card against the gate: every row's text is its own, and the
 *        gate's text is on the row it belongs to.
 * @return false on a row that shows another row's text, or a gate text
 *         on no row
 */
static bool card_holds_the_gate(void)
{
    rdy_row_t r[RAR_COUNT];
    for (int i = 0; i < (int)RAR_COUNT; ++i) {
        if (!rauto_ready_row(i, &r[i])) {
            return false;
        }
        if (r[i].why != NULL && rauto_row_of(r[i].why) != (rauto_row_t)i) {
            return false;
        }
    }
    const char *g = rauto_blocked();
    if (g == NULL) {
        return true;
    }
    int on = 0;
    for (int i = 0; i < (int)RAR_COUNT; ++i) {
        on += (r[i].why == g);
    }
    /* Its row shows it, or a text of the same row that refuses the hold
     * where the gate's own is one the hold does. */
    const rdy_row_t *mine = &r[rauto_row_of(g)];
    if (mine->why == g) {
        return on == 1;
    }
    return on == 0 && rauto_seq_fixes(g) && mine->st == (uint8_t)RDY_BAD;
}

/**
 * @brief A hold refused is named on a red row: the row of
 *        rauto_hold_blocked()'s text reads red with that text, and every
 *        row with a text that refuses reads red, but the rows that wait on
 *        a red PORT.
 * @return false on a refusal no red row names
 */
static bool refusal_on_a_red_row(void)
{
    rauto_row_t row = RAR_COUNT;
    const char *hb = rauto_hold_blocked(&row);
    rdy_row_t r;
    if (hb != NULL) {
        if (!rauto_ready_row((int)row, &r) || r.st != (uint8_t)RDY_BAD
            || r.why != hb) {
            return false;
        }
    }
    rdy_row_t port;
    (void)rauto_ready_row(RAR_PORT, &port);
    for (int i = 0; i < (int)RAR_COUNT; ++i) {
        (void)rauto_ready_row(i, &r);
        if (r.why != NULL && !rauto_seq_fixes(r.why)
            && r.st != (uint8_t)RDY_BAD
            && !(r.st == (uint8_t)RDY_IDLE && port.st == (uint8_t)RDY_BAD)) {
            return false;
        }
    }
    return true;
}

/* Each of the gate's texts is one row's, the same row in English and in
 * German; no text of the list is two rows', and a text the gate does not
 * return is no row's. */
TEST_CASE(every_gate_text_belongs_to_exactly_one_row)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int k = 0; k < N_GATE; ++k) {
            const char *t = gate_str(&k_gate[k]);
            CHECK_EQ(rauto_row_of(t), k_gate[k].row);
            /* No other text of the list shares its string. */
            for (int j = 0; j < N_GATE; ++j) {
                if (j != k) {
                    CHECK(gate_str(&k_gate[j]) != t);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(rauto_row_of(NULL), RAR_COUNT);
    CHECK_EQ(rauto_row_of(TR(RA_GOALS)), RAR_COUNT);
    CHECK_EQ(rauto_row_of("AUTO PROGRAMS A SERVO ON THE PROG PORT"),
             RAR_COUNT);                         /* a copy is no row's */
}

/* A SAFE bench on OUTPUTS; ARMED; a PROG PORT SAFE with its pulse off and
 * on; ARMED with it off; no link: on each the gate's text is on its row
 * and every row shows only its own. */
TEST_CASE(on_each_bench_the_gate_text_is_on_its_row_alone)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        to_auto(false);
        CHECK(card_holds_the_gate());
        to_auto(true);
        CHECK(card_holds_the_gate());
        to_auto(false);
        settings_set(SET_SERVO_TARGET, 1.0f);
        servo_screen_set_drive(1u, 13u, false);
        CHECK(rauto_blocked() != NULL);
        CHECK(card_holds_the_gate());
        servo_screen_set_drive(1u, 13u, true);
        CHECK(card_holds_the_gate());
        servo_screen_set_armed(true);
        servo_screen_set_drive(1u, 13u, false);
        CHECK(card_holds_the_gate());
        servo_screen_set_link(false);
        CHECK(card_holds_the_gate());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* On OUTPUTS the PORT row is red: the hold refuses, its fill stays grey,
 * 2.5s held start nothing, and a tap names PORT and its text in the
 * band; the ARM row reads amber. */
TEST_CASE(the_hold_refuses_on_the_first_red_row_and_names_it)
{
    to_auto(false);
    rauto_row_t row = RAR_COUNT;
    CHECK_STR_EQ(rauto_hold_blocked(&row), TR(RA_PROG_PORT));
    CHECK_EQ(row, RAR_PORT);
    char want[128], got[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(SVH_PORT),
             TR(RA_PROG_PORT));
    CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    frames(2.5f);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    CHECK(!svs_seq_busy());
    CHECK(!servo_screen_rauto_running());
    /* A tap on the grey hold hands ready_refusal()'s line to the band as
     * a notice (ui_action_refused()). */
    tap(SV_RA_START_X, SV_RA_START_Y);
    CHECK(!svs_seq_busy());
    rdy_row_t r;
    CHECK(rauto_ready_row(RAR_PORT, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(r.fix);
    /* The rows that wait on the port read grey. */
    CHECK(rauto_ready_row(RAR_PWM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_IDLE);
    CHECK_STR_EQ(r.value, "--");
    /* An amber row takes no tap; a grey one names its own text. */
    ui_router_set_alert(NULL);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_ARM));
    CHECK_STR_EQ(alert(), "");
    CHECK(r.why != NULL);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_PWM), r.why);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_PWM));
    CHECK_STR_EQ(alert(), want);
    scr->render(&cv, 0);
}

/* NOT ARMED is the hold's on any bench; PORT PWM OFF and the PD mini off
 * only while SAFE: ARMED they refuse.  The PD mini off is the hold's only
 * on a rail the INA3221 reads dead (under 3.0V). */
TEST_CASE(the_rows_the_hold_does_read_amber_only_while_safe)
{
    to_auto(false);
    rail_window(0, 0u);
    CHECK(rauto_seq_fixes(ui_servo_str(SERVO_STR_START_NOT_ARMED)));
    CHECK(rauto_seq_fixes(TR(SV_LIM_PROG_PD_OFF)));
    CHECK(rauto_seq_fixes(TR(SV_LIM_PWM_SAFE)));
    CHECK(!rauto_seq_fixes(TR(RA_NO_ZERO)));
    CHECK(!rauto_seq_fixes(TR(SV_LIM_PWM_OFF_ARMED)));
    CHECK(!rauto_seq_fixes(NULL));
    rdy_row_t r;
    CHECK(rauto_ready_row(RAR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ARM));
    CHECK_STR_EQ(r.label, "ARM");
    servo_screen_set_armed(true);
    CHECK(rauto_seq_fixes(ui_servo_str(SERVO_STR_START_NOT_ARMED)));
    CHECK(!rauto_seq_fixes(TR(SV_LIM_PROG_PD_OFF)));
    CHECK(!rauto_seq_fixes(TR(SV_LIM_PWM_SAFE)));
    CHECK(!rauto_seq_fixes(ui_servo_str(SERVO_STR_START_PROG_5V)));
    CHECK(rauto_ready_row(RAR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    CHECK_STR_EQ(r.value, "ARMED");
    CHECK(!rauto_ready_row(RAR_COUNT, &r));
    CHECK(!rauto_ready_row(-1, &r));
    CHECK(!rauto_ready_row(0, NULL));
}

/* A tap on the PORT row opens the choice of OUTPUTS and the four PROG
 * PORTs, as the PORT button does. */
TEST_CASE(a_tap_on_port_opens_the_port_list)
{
    to_auto(false);
    CHECK_EQ(servo_screen_port_choices(), -1);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_PORT));
    CHECK_EQ(servo_screen_port_choices(), 5);
    scr->render(&cv, 0);
}

/* Every label fits its 13 cells and every state the hold reads, ARMED,
 * the set and free words fit the 20 the state column has, in both
 * languages. */
TEST_CASE(every_label_and_state_fits_its_column)
{
    static const ui_text_id_t k_lab[] = {
        TX_SVH_PORT, TX_SVH_SERVO, TX_RDY_METER, TX_RDY_ANGLE, TX_RDY_ZERO,
        TX_RDY_RANGE, TX_RDY_PROG, TX_RDY_SUPPLY, TX_RDY_PWM,
    };
    static const ui_text_id_t k_val[] = {
        TX_RDY_SEQ_ON, TX_RDY_SEQ_ARM, TX_RDY_ARMING, TX_RDY_SET,
        TX_RDY_FREE, TX_SV_LIM_SERVO_KST,
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t k = 0; k < sizeof(k_lab) / sizeof(k_lab[0]); ++k) {
            CHECK(gfx_text_cells(ui_tr(k_lab[k])) * 10 <= READY_LABEL_W);
        }
        for (size_t k = 0; k < sizeof(k_val) / sizeof(k_val[0]); ++k) {
            CHECK(gfx_text_cells(ui_tr(k_val[k])) * 10 <= READY_VALUE_W);
        }
        char t[48];
        snprintf(t, sizeof(t), "%s %s", TR(SV_LIM_SERVO_HITEC),
                 ui_setting_option(SET_HITEC_SERIES, 1));
        CHECK(gfx_text_cells(t) * 10 <= READY_VALUE_W);
        CHECK(gfx_text_cells(TR(RDY_TITLE)) * 10 <= READY_LABEL_W);
    }
    ui_text_set_language(UI_LANG_EN);
    /* The rows and the rail line inside the output card. */
    const gfx_rect_t c = ready_card_rect();
    for (int i = 0; i < READY_ROWS_MAX; ++i) {
        const gfx_rect_t r = ready_row_rect(i);
        CHECK(r.x >= c.x && r.x + r.w <= c.x + c.w);
        CHECK(r.y >= c.y && r.y + r.h <= ready_rail_rect().y);
    }
    CHECK_EQ(ready_row_rect(READY_ROWS_MAX).w, 0);
    CHECK_EQ(ready_row_rect(-1).w, 0);
    const gfx_rect_t rl = ready_rail_rect();
    CHECK(rl.y + rl.h <= c.y + c.h);
}

/* RUDDER AUTO shows the card and its count of rows; RUDDER and LIVE have
 * none and keep the output card. */
TEST_CASE(the_card_takes_the_output_cards_place_on_rudder_auto_only)
{
    to_auto(false);
    CHECK(ready_shown());
    CHECK_EQ(ready_count(), (int)RAR_COUNT);
    rdy_row_t r;
    CHECK(ready_row(0, &r));
    go(SERVO_FN_RUDDER);
    CHECK(!ready_shown());
    CHECK_EQ(ready_count(), 0);
    CHECK(!ready_row(0, &r));
    char b[64];
    CHECK(ready_refusal(b, sizeof(b)) == NULL);
    go(SERVO_FN_LIVE);
    CHECK(!ready_shown());
}

/* On every bench of the cases above, and with a report still on its way
 * behind an amber ARM, the hold refused is named on a red row: REPORT
 * STILL BEING WRITTEN on ARM, red, not hidden behind START ARMS. */
TEST_CASE(a_refused_hold_has_a_red_row_naming_it)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        to_auto(false);
        CHECK(refusal_on_a_red_row());
        to_auto(true);
        CHECK(refusal_on_a_red_row());
        to_auto(false);
        settings_set(SET_SERVO_TARGET, 1.0f);
        servo_screen_set_drive(1u, 13u, false);
        CHECK(refusal_on_a_red_row());
        servo_screen_set_drive(1u, 13u, true);
        CHECK(refusal_on_a_red_row());
        servo_screen_test_logger_busy(true);
        CHECK_STR_EQ(rauto_hold_blocked(NULL),
                     ui_servo_str(SERVO_STR_START_BUSY));
        rdy_row_t r;
        CHECK(rauto_ready_row(RAR_ARM, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK_STR_EQ(r.why, ui_servo_str(SERVO_STR_START_BUSY));
        CHECK(refusal_on_a_red_row());
        CHECK(card_holds_the_gate());
        servo_screen_test_logger_busy(false);
        servo_screen_set_armed(true);
        servo_screen_set_drive(1u, 13u, false);
        CHECK(refusal_on_a_red_row());
        servo_screen_set_link(false);
        CHECK(refusal_on_a_red_row());
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_gate_text_belongs_to_exactly_one_row);
    RUN(on_each_bench_the_gate_text_is_on_its_row_alone);
    RUN(the_hold_refuses_on_the_first_red_row_and_names_it);
    RUN(the_rows_the_hold_does_read_amber_only_while_safe);
    RUN(a_tap_on_port_opens_the_port_list);
    RUN(every_label_and_state_fits_its_column);
    RUN(the_card_takes_the_output_cards_place_on_rudder_auto_only);
    RUN(a_refused_hold_has_a_red_row_naming_it);
    return test_summary("servo_ready");
}
