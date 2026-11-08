/**
 * @file test_servo_ready.c
 * @brief RUDDER AUTO's READY card (servo_ready.c,
 *        servo_rudder_auto_ready.c): every text of the start gate
 *        belongs to exactly one row, in both languages; on each bench
 *        state the gate's text is on its row and no row shows another
 *        row's; the hold refuses on the first red row and names it in the
 *        alert band; the rows ARM + START does itself read amber; the
 *        states sit on tags of one left edge; a tap on PORT opens the port
 *        list; every label and state fits its column;
 *        a press on a row ends on its release or a cancel; a tap on a red
 *        ZERO row names itself; the sequence's calls name each step and each
 *        failure and switch every output off.
 *
 * Cases:
 * - every_gate_text_belongs_to_exactly_one_row
 * - on_each_bench_the_gate_text_is_on_its_row_alone
 * - the_hold_refuses_on_the_first_red_row_and_names_it
 * - the_rows_the_hold_does_read_amber_only_while_safe
 * - the_tags_share_the_left_edge_the_widest_state_needs
 * - a_tap_on_port_opens_the_port_list
 * - every_label_and_state_fits_its_column
 * - the_card_takes_the_output_cards_place_on_every_function
 * - every_function_screen_gives_the_footers_middle_button_one_owner
 * - a_refused_hold_has_a_red_row_naming_it
 * - a_press_on_a_row_ends_on_its_release_or_a_cancel
 * - a_tap_on_a_red_zero_row_without_a_reading_names_it
 * - a_tap_on_servo_opens_the_family_list_unless_refused
 * - servo_reads_the_family_the_run_programs
 * - the_sequence_names_its_steps_and_a_start_the_run_refuses
 * - a_refusal_names_its_part_and_switches_every_output_off
 * - a_read_back_that_runs_out_names_its_part_and_switches_off
 * - a_stop_or_a_leave_ends_the_sequence_as_each_must
 * - a_finger_on_a_ready_row_keeps_the_list_shut
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "hitec_wire.h"
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

/** @brief Row @p i of the card as it reads now. */
static rdy_row_t row_of(int i)
{
    rdy_row_t r;
    CHECK(rauto_ready_row(i, &r));
    return r;
}

/** @brief Row @p i's state, an rdy_state_t. */
static uint8_t row_state(int i)
{
    return row_of(i).st;
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

/** @brief The width of state @p v as the card shows it: its first 20
 *         cells, cut after the last whole word in them. */
static int shown_width(const char *v)
{
    size_t cut = gfx_text_prefix(v, READY_VALUE_W / 10);
    if (v[cut] != '\0') {
        size_t sp = cut;
        while (sp > 0u && v[sp] != ' ') {
            --sp;
        }
        cut = (sp > 0u) ? sp : cut;
    }
    char t[64];
    snprintf(t, sizeof(t), "%.*s", (int)cut, v);
    return gfx_text_width(&gfx_font_10x20, t, 1);
}

/* Every row's state sits on a tag 24px high, centred on its row and
 * inside its tap area with its 2px shadow.  The tags share one left edge:
 * the widest state's text, 9px each side and the 14px slant left of the
 * states' right edge.  Drawn, a tag's top row is lit to that right edge,
 * its left column lit, its bottom row dark and ending 14px further left,
 * its slant in the dim edge with the button fill inside and the shadow
 * outside, and its text starts 9px right of the left edge. */
TEST_CASE(the_tags_share_the_left_edge_the_widest_state_needs)
{
    to_auto(false);
    const int n = ready_count();
    CHECK(n > 1);
    const gfx_rect_t t0 = ready_tag_rect(0);
    const int right = t0.x + t0.w;
    int widest = 0;
    for (int i = 0; i < n; ++i) {
        rdy_row_t r;
        CHECK(ready_row(i, &r));
        const int w = shown_width(r.value);
        widest = (w > widest) ? w : widest;
    }
    const int left = right - widest - 2 * READY_TAG_PAD - READY_TAG_SLANT;
    CHECK_EQ(ready_tag_left(), left);
    for (int i = 0; i < n; ++i) {
        const gfx_rect_t t = ready_tag_rect(i);
        const gfx_rect_t a = ready_row_rect(i);
        CHECK_EQ(t.x, left);
        CHECK_EQ(t.x + t.w, right);
        CHECK_EQ(t.h, READY_TAG_H);
        CHECK_EQ(t.y - a.y, a.y + a.h - (t.y + t.h));
        CHECK(t.x >= a.x && right + 2 <= a.x + a.w);
        CHECK(t.y >= a.y && t.y + t.h + 2 <= a.y + a.h);
    }
    CHECK_EQ(READY_TAG_SLANT, 14);  /* round(24 * tan(30 degrees)) */
    CHECK(gfx_rect_empty(ready_tag_rect(n)));

    scr->render(&cv, 0);
    const gfx_rect_t t = ready_tag_rect(RAR_PORT);
    const int y = t.y, b = t.y + t.h - 1, k = READY_TAG_SLANT;
    const gfx_color_t lit = ui_theme_color(UI_C_BEVEL_LIT);
    CHECK_EQ(gfx_pixel_get(&cv, left, y), lit);
    CHECK_EQ(gfx_pixel_get(&cv, right - 1, y), lit);
    CHECK(gfx_pixel_get(&cv, right, y) != lit);
    CHECK_EQ(gfx_pixel_get(&cv, left, y + 12), lit);
    CHECK_EQ(gfx_pixel_get(&cv, left + 1, b),
             ui_theme_color(UI_C_BEVEL_DARK));
    CHECK_EQ(gfx_pixel_get(&cv, right - k - 1, b),
             ui_theme_color(UI_C_BEVEL_DARK));
    /* Row 12 of 24: the slant 7px in, round(14 * 12 / 23). */
    CHECK_EQ(gfx_pixel_get(&cv, right - 8, y + 12), ui_theme_color(UI_C_GRID));
    CHECK_EQ(gfx_pixel_get(&cv, right - 9, y + 12),
             ui_theme_color(UI_C_PANEL_HI));
    CHECK_EQ(gfx_pixel_get(&cv, right - 7, y + 12), ui_theme_color(UI_C_BG));
    CHECK_EQ(gfx_pixel_get(&cv, right + 1, y + 2), ui_theme_color(UI_C_BG));
    CHECK_EQ(gfx_pixel_get(&cv, left + 2, b + 2), ui_theme_color(UI_C_BG));
    CHECK_EQ(gfx_pixel_get(&cv, right + 2, y + 2), ui_theme_color(UI_C_PANEL));
    /* The text from 9px right of the left edge, none before it. */
    rdy_row_t r;
    CHECK(ready_row(RAR_PORT, &r));
    const gfx_color_t ink = (r.st == RDY_BAD) ? ui_theme_color(UI_C_DANGER)
                          : (r.st == RDY_IDLE) ? ui_theme_color(UI_C_TEXT_FAINT)
                          : (r.st == RDY_SEQ || r.st == RDY_WARN)
                              ? ui_theme_color(UI_C_WARN)
                              : ui_theme_color(UI_C_TEXT);
    int first = -1;
    for (int x = left; x < right && first < 0; ++x) {
        for (int yy = y; yy <= b; ++yy) {
            if (gfx_pixel_get(&cv, x, yy) == ink) {
                first = x;
                break;
            }
        }
    }
    CHECK(first >= left + READY_TAG_PAD);
    CHECK(first < left + READY_TAG_PAD + 10);
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

/* Every label fits its 10 cells and every state the hold reads, ARMED,
 * the set and free words fit the 20 the state column has, in both
 * languages. */
TEST_CASE(every_label_and_state_fits_its_column)
{
    static const ui_text_id_t k_lab[] = {
        TX_SVH_PORT, TX_SVH_SERVO, TX_RDY_METER, TX_RDY_ANGLE, TX_RDY_ZERO,
        TX_RDY_RANGE, TX_RDY_PROG, TX_RDY_SUPPLY, TX_RDY_PWM, TX_RDY_RAIL,
        TX_TST_STEPS,
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
        for (int f = 0; f < (int)SERVO_LIMRUN_FAM_COUNT; ++f) {
            CHECK(gfx_text_cells(ui_setting_option(SET_LIMIT_FAMILY, f)) * 10
                  <= READY_VALUE_W);
        }
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

/* RUDDER AUTO shows the card in the whole output card's place and its
 * count of rows; RUDDER and LIVE show their rows over the output card's
 * controls (servo_drive_ready.c); KST shows its own three rows in the
 * whole output card's place (servo_prog_ready.c), with no ARM + START and
 * no refusal of one. */
TEST_CASE(the_card_takes_the_output_cards_place_on_every_function)
{
    to_auto(false);
    CHECK(ready_shown());
    CHECK_EQ(ready_count(), (int)RAR_COUNT);
    rdy_row_t r;
    CHECK(ready_row(0, &r));
    go(SERVO_FN_RUDDER);
    CHECK(!ready_shown());
    CHECK(drive_shown());
    CHECK_EQ(ready_count(), 5);
    CHECK(ready_card_rect().h < 436 - 62);
    go(SERVO_FN_LIVE);
    CHECK(!ready_shown());
    CHECK_EQ(ready_count(), 3);
    go(SERVO_FN_KST);
    CHECK(!ready_shown());
    CHECK(!drive_shown());
    CHECK(prog_ready_shown());
    CHECK_EQ(ready_count(), 3);            /* KST: PORT, supply, SERVO */
    CHECK(ready_row(0, &r));
    CHECK(!ready_row(3, &r));
    char b[64];
    CHECK(ready_refusal(b, sizeof(b)) == NULL);
}

/* Every function screen gives the footer's middle button, where ARM sits,
 * to exactly one owner: ARM on LIVE, SWEEP and RUDDER (servo_drive_ready.c),
 * WRITE on KST and HITEC (servo_prog_ready.c), and ARM + START on RUDDER
 * AUTO, TEST and LIMIT RUN, whose hold is that button.  No press on it is
 * left to the screen's own ARM. */
TEST_CASE(every_function_screen_gives_the_footers_middle_button_one_owner)
{
    for (int fn = SERVO_FN_LIVE; fn < SERVO_FN_COUNT; ++fn) {
        fresh();
        go((servo_fn_t)fn);
        const int owners = (drive_shown() ? 1 : 0)
                           + (prog_ready_shown() ? 1 : 0)
                           + (ready_shown() ? 1 : 0);
        CHECK_EQ(owners, 1);
        CHECK(gfx_rect_contains(ready_hold_rect(), SV_ARM_X, SV_FOOT_Y));
    }
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

/** @brief A touch event at (@p x, @p y). */
static touch_event_t event_at(touch_event_type_t t, int x, int y, uint8_t id)
{
    touch_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = t;
    e.point.id = id;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    return e;
}

/**
 * @brief Whether a DISARM command is waiting for the panel; every command
 *        waiting is taken.
 * @return true when one was
 */
static bool disarm_posted(void)
{
    bool found = false;
    servo_cmd_t c;
    while (servo_screen_take(&c)) {
        found = found || c.kind == SERVO_CMD_DISARM;
    }
    return found;
}

/** @brief A PD mini that answers with its output off, read now. */
static void pd_answers_off(void)
{
    rail_supply(5.0f, 0.0f);
}

/* A press on a row is the card's, whatever its finger does: a second
 * finger and a move change nothing, a cancel drops it with no tap, an UP
 * the tracker marked NO_TAP takes none, and a clean release names the
 * row.  A DOWN off the card, or on a screen without it, is not the
 * card's. */
TEST_CASE(a_press_on_a_row_ends_on_its_release_or_a_cancel)
{
    to_auto(false);
    const int x = SV_RDY_X, y = SV_RDY_ROW_Y(RAR_PWM);
    const uint32_t base = ready_sig();
    char want[128];
    rdy_row_t r;
    CHECK(rauto_ready_row(RAR_PWM, &r));
    CHECK(r.why != NULL);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_PWM), r.why);

    const touch_event_t off = event_at(TOUCH_EVENT_DOWN, 20, 200, 3);
    CHECK(!ready_down(NULL));
    CHECK(!ready_down(&off));
    CHECK_EQ(ready_sig(), base);

    const touch_event_t down = event_at(TOUCH_EVENT_DOWN, x, y, 3);
    CHECK(ready_down(&down));
    const uint32_t pressed = ready_sig();
    CHECK(pressed != base);
    /* A second finger on another row takes nothing. */
    const touch_event_t second = event_at(TOUCH_EVENT_DOWN, x,
                                          SV_RDY_ROW_Y(RAR_ARM), 4);
    CHECK(ready_down(&second));
    CHECK_EQ(ready_sig(), pressed);
    /* A move of the press's finger is the press's; another's is not. */
    const touch_event_t move = event_at(TOUCH_EVENT_MOVE, x, y, 3);
    const touch_event_t other = event_at(TOUCH_EVENT_MOVE, x, y, 9);
    CHECK(ready_rest(&move));
    CHECK(!ready_rest(&other));
    CHECK_EQ(ready_sig(), pressed);
    /* A cancel puts the row back and the release after it taps nothing. */
    ready_cancel();
    CHECK_EQ(ready_sig(), base);
    ready_cancel();
    CHECK_EQ(ready_sig(), base);
    const touch_event_t up = event_at(TOUCH_EVENT_UP, x, y, 3);
    CHECK(!ready_rest(&up));
    CHECK_STR_EQ(alert(), "");
    /* An UP marked NO_TAP ends the press and names nothing. */
    CHECK(ready_down(&down));
    touch_event_t no_tap = up;
    no_tap.flags = TOUCH_FLAG_NO_TAP;
    CHECK(ready_rest(&no_tap));
    CHECK_EQ(ready_sig(), base);
    CHECK_STR_EQ(alert(), "");
    /* A clean release over the row names it. */
    CHECK(ready_down(&down));
    CHECK(ready_rest(&move));
    CHECK(ready_rest(&up));
    CHECK_EQ(ready_sig(), base);
    CHECK_STR_EQ(alert(), want);
    /* No card on RUDDER: the DOWN is the screen's. */
    go(SERVO_FN_RUDDER);
    CHECK(!ready_down(&down));
}

/* A red ZERO row with no angle reading takes no ZERO: its tap names the
 * row in the band and the row stays red.  (ANGLE's and METER's taps leave
 * the screen: test_menu_openers.) */
TEST_CASE(a_tap_on_a_red_zero_row_without_a_reading_names_it)
{
    char want[128];
    rdy_row_t r;
    to_auto(false);
    CHECK_EQ(row_state(RAR_ZERO), (uint8_t)RDY_BAD);
    CHECK(rauto_ready_row(RAR_ZERO, &r));
    CHECK_STR_EQ(r.why, TR(RA_NO_ZERO));
    CHECK(r.fix);
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_ZERO), TR(RA_NO_ZERO));
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_ZERO));
    CHECK_STR_EQ(alert(), want);
    CHECK_EQ(row_state(RAR_ZERO), (uint8_t)RDY_BAD);
}

/* A tap on SERVO opens the three families over the left column; a pick
 * is RUDDER AUTO's own and is saved.  Off a PROG PORT and on an ARMED
 * bench the list stays shut and the band names why. */
TEST_CASE(a_tap_on_servo_opens_the_family_list_unless_refused)
{
    to_auto(false);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK_STR_EQ(servo_screen_family_refused(), TR(SVL_PORT_ONLY));

    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_family_choices(), 3);
    CHECK_STR_EQ(servo_screen_family_choice(0), "KST/CHASERVO");
    CHECK_STR_EQ(servo_screen_family_choice(1), "HITEC D SERIES");
    CHECK_STR_EQ(servo_screen_family_choice(2), "HITEC CLASSIC");
    ov_choose(2);
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_HITEC_C);
    CHECK_EQ(servo_screen_limit_family(), SERVO_LIMRUN_FAM_KST);
    CHECK(settings_save_asked());

    to_auto(true);
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    ui_router_set_alert(NULL);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    CHECK_EQ(servo_screen_family_choices(), -1);
    CHECK(!servo_screen_list()->open);
    CHECK_STR_EQ(servo_screen_family_refused(), TR(FOOT_SETTINGS_SAFE));
    CHECK_EQ(servo_screen_rauto_family(), SERVO_RAUTO_FAM_KST);
}

/* On a PROG PORT the SERVO row reads the family RUDDER AUTO's own value
 * holds: KST/CHASERVO, HITEC D SERIES or HITEC CLASSIC.  The HITEC page's
 * series does not move it. */
TEST_CASE(servo_reads_the_family_the_run_programs)
{
    to_auto(false);
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    rdy_row_t r = row_of(RAR_SERVO);
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK_STR_EQ(r.label, TR(SVH_SERVO));
    CHECK_STR_EQ(r.value, TR(SV_LIM_SERVO_KST));
    CHECK(r.fix);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    ov_choose(2);
    CHECK_STR_EQ(row_of(RAR_SERVO).value, "HITEC CLASSIC");
    settings_set(SET_HITEC_SERIES, (float)HITEC_D);
    CHECK_STR_EQ(row_of(RAR_SERVO).value, "HITEC CLASSIC");
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    ov_choose(1);
    r = row_of(RAR_SERVO);
    CHECK_STR_EQ(r.value, "HITEC D SERIES");
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_SERVO));
    ov_choose(0);
    CHECK_STR_EQ(row_of(RAR_SERVO).value, TR(SV_LIM_SERVO_KST));
    settings_cancel_save();
}

/* A sequence on a PROG PORT: PORT PWM asked and read back, ARM asked,
 * ARMED read, START handed to the caller, which refuses it.  Each step
 * is named in the state line and the hold's label, in its number of the
 * four steps; the refusal fails at START with the run's text, in the band,
 * and the bench is asked to DISARM.  An idle sequence has no line, no
 * label and takes no frame; a second begin changes nothing. */
TEST_CASE(the_sequence_names_its_steps_and_a_start_the_run_refuses)
{
    char b[96], want[128], text[96];
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        to_auto(false);
        settings_set(SET_SERVO_TARGET, 1.0f);
        settings_set(SET_SERVO_PORT_PWM, 0.0f);
        servo_screen_set_drive(1u, 13u, false);
        servo_screen_clock(1000u);
        CHECK(!svs_seq_frame(NULL));
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), "");
        CHECK_STR_EQ(svs_seq_label(b, sizeof(b)), "");
        CHECK_STR_EQ(svs_seq_line(NULL, sizeof(b)), "");
        CHECK_STR_EQ(svs_seq_label(b, 0u), "");
        CHECK(svs_seq_failure() == NULL);

        CHECK(svs_seq_begin());
        CHECK(!svs_seq_begin());
        CHECK(svs_seq_busy());
        snprintf(want, sizeof(want), TR(SQ_LINE), 1u, 4u, TR(RDY_PWM));
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), want);
        snprintf(want, sizeof(want), "%s ...", TR(RDY_PWM));
        CHECK_STR_EQ(svs_seq_label(b, sizeof(b)), want);

        /* PORT PWM goes on and is saved; the pulse is not read back yet. */
        CHECK(!svs_seq_frame(NULL));
        CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
        snprintf(want, sizeof(want), TR(SQ_LINE), 2u, 4u, TR(SQ_READBACK));
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), want);
        snprintf(want, sizeof(want), "%s ...", TR(SQ_READBACK));
        CHECK_STR_EQ(svs_seq_label(b, sizeof(b)), want);
        CHECK(!svs_seq_frame(NULL));
        CHECK(svs_seq_busy());
        /* The pulse reads back: ARM is asked, once. */
        servo_screen_set_drive(1u, 13u, true);
        CHECK(!svs_seq_frame(NULL));
        snprintf(want, sizeof(want), TR(SQ_LINE), 3u, 4u, "ARM");
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), want);
        CHECK_STR_EQ(svs_seq_label(b, sizeof(b)), "ARM ...");
        servo_cmd_t c;
        bool arm = false;
        while (servo_screen_take(&c)) {
            arm = arm || c.kind == SERVO_CMD_ARM;
        }
        CHECK(arm);
        /* ARMED reads: the caller is told to start. */
        servo_screen_set_armed(true);
        CHECK(svs_seq_frame(NULL));
        snprintf(want, sizeof(want), TR(SQ_LINE), 4u, 4u, "START");
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), want);
        CHECK_STR_EQ(svs_seq_label(b, sizeof(b)), "START ...");
        /* The run refuses its start: no text of its own, then its text. */
        svs_seq_started(false, NULL);
        CHECK(!svs_seq_busy());
        snprintf(text, sizeof(text), TR(SQ_FAILED), "START", TR(RA_REFUSED));
        CHECK_STR_EQ(svs_seq_failure(), text);
        CHECK_STR_EQ(alert(), text);
        CHECK(disarm_posted());
        CHECK_STR_EQ(svs_seq_line(b, sizeof(b)), "");

        servo_screen_set_armed(false);
        CHECK(svs_seq_begin());
        CHECK(svs_seq_failure() == NULL);
        CHECK(!svs_seq_frame(NULL));
        servo_screen_set_drive(1u, 13u, true);
        (void)svs_seq_frame(NULL);
        servo_screen_set_armed(true);
        CHECK(svs_seq_frame(NULL));
        svs_seq_started(false, TR(RA_NO_ZERO));
        snprintf(text, sizeof(text), TR(SQ_FAILED), "START", TR(RA_NO_ZERO));
        CHECK_STR_EQ(svs_seq_failure(), text);
        CHECK(disarm_posted());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A check of the gate that refuses on the way fails the step with the part
 * its row names: PORT PWM's row PORT PWM, ARM's row ARM, the supply row the
 * PD mini where it feeds the servo; a row no part belongs to names the step
 * under way.  Each failure's line carries the refusal, and DISARM goes out
 * and, with a PD mini fed, its output off. */
TEST_CASE(a_refusal_names_its_part_and_switches_every_output_off)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        const struct {
            const char *why;
            const char *part;
            bool        pd;
        } k_case[] = {
            { TR(SV_LIM_PWM_OFF_ARMED), TR(RDY_PWM), false },
            { ui_servo_str(SERVO_STR_START_BUSY), "ARM", false },
            { TR(RA_NO_ZERO), TR(RDY_PWM), false },
            { TR(SV_LIM_PROG_PD_OFF), "PD MINI", true },
        };
        for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
            char want[128];
            to_auto(false);
            settings_set(SET_SERVO_TARGET, 1.0f);
            servo_screen_set_drive(1u, 13u, false);
            servo_screen_clock(500u);
            if (k_case[k].pd) {
                pd_answers_off();
            }
            CHECK(svs_seq_begin());
            CHECK_EQ(svs_seq_need().pd_mini, k_case[k].pd);
            CHECK(!svs_seq_frame(k_case[k].why));
            CHECK(!svs_seq_busy());
            snprintf(want, sizeof(want), TR(SQ_FAILED), k_case[k].part,
                     k_case[k].why);
            CHECK_STR_EQ(svs_seq_failure(), want);
            CHECK_STR_EQ(alert(), want);
            CHECK(disarm_posted());
            supply_cmd_t sc;
            const bool off = supply_cmd(&sc) && sc.off;
            CHECK_EQ(off, k_case[k].pd);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A read-back is judged at 1000ms: at 999ms it waits, at 1000ms it fails
 * naming the part that did not read back, PORT PWM or the PD mini, with the
 * cause NO READ-BACK IN 1000ms and DISARM out; the PD mini's ON is asked
 * once and its OFF with the failure. */
TEST_CASE(a_read_back_that_runs_out_names_its_part_and_switches_off)
{
    char want[128], cause[64];
    snprintf(cause, sizeof(cause), TR(SQ_NO_READBACK), 1000u);

    to_auto(false);
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, false);
    servo_screen_clock(2000u);
    CHECK(svs_seq_begin());
    CHECK(!svs_seq_frame(NULL));
    servo_screen_clock(2999u);
    CHECK(!svs_seq_frame(NULL));
    CHECK(svs_seq_busy());
    CHECK(svs_seq_failure() == NULL);
    servo_screen_clock(3000u);
    CHECK(!svs_seq_frame(NULL));
    CHECK(!svs_seq_busy());
    snprintf(want, sizeof(want), TR(SQ_FAILED), TR(RDY_PWM), cause);
    CHECK_STR_EQ(svs_seq_failure(), want);
    CHECK_STR_EQ(alert(), want);
    CHECK(disarm_posted());

    to_auto(false);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    pd_answers_off();
    servo_screen_clock(5000u);
    CHECK(svs_seq_begin());
    CHECK(svs_seq_need().pd_mini);
    CHECK(!svs_seq_frame(NULL));
    supply_cmd_t sc;
    CHECK(supply_cmd(&sc));
    CHECK(sc.on);
    servo_screen_clock(5999u);
    CHECK(!svs_seq_frame(NULL));
    CHECK(svs_seq_busy());
    servo_screen_clock(6000u);
    CHECK(!svs_seq_frame(NULL));
    CHECK(!svs_seq_busy());
    snprintf(want, sizeof(want), TR(SQ_FAILED), "PD MINI", cause);
    CHECK_STR_EQ(svs_seq_failure(), want);
    CHECK(disarm_posted());
    CHECK(supply_cmd(&sc));
    CHECK(sc.off);
}

/* An end by STOP or the operator fails the step under way as STOP with
 * DISARM out; a leave ends the sequence and leaves the outputs to the
 * leave, naming nothing.  An end with none under way keeps the last
 * failure, except a leave, which clears it for the screen entered again. */
TEST_CASE(a_stop_or_a_leave_ends_the_sequence_as_each_must)
{
    char want[128];
    to_auto(false);
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, false);
    servo_screen_clock(100u);
    CHECK(svs_seq_begin());
    svs_seq_end(SERVO_TEST_AB_OPERATOR);
    CHECK(!svs_seq_busy());
    snprintf(want, sizeof(want), TR(SQ_FAILED), TR(RDY_PWM), "STOP");
    CHECK_STR_EQ(svs_seq_failure(), want);
    CHECK_STR_EQ(alert(), want);
    CHECK(disarm_posted());
    /* Nothing under way: the failure stays for the operator to read. */
    svs_seq_end(SERVO_TEST_AB_OPERATOR);
    CHECK_STR_EQ(svs_seq_failure(), want);
    /* A leave clears it, with or without a sequence. */
    svs_seq_end(SERVO_TEST_AB_LEFT);
    CHECK(svs_seq_failure() == NULL);

    CHECK(svs_seq_begin());
    (void)svs_seq_frame(NULL);
    svs_seq_end(SERVO_TEST_AB_LEFT);
    CHECK(!svs_seq_busy());
    CHECK(svs_seq_failure() == NULL);
    CHECK(!disarm_posted());
    CHECK(svs_seq_begin());
    CHECK(svs_seq_failure() == NULL);
}

/* A finger on a READY row holds SERVO's lists shut: SETTINGS tapped with a
 * second finger opens none, and the row's release still reaches the card.
 * Once it is up, SETTINGS opens the list. */
TEST_CASE(a_finger_on_a_ready_row_keeps_the_list_shut)
{
    to_auto(false);
    const int x = SV_RDY_X, y = SV_RDY_ROW_Y(RAR_PWM);
    ev(x, y, TOUCH_EVENT_DOWN, 3);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 4);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 4);
    CHECK(!servo_screen_list()->open);
    ev(x, y, TOUCH_EVENT_UP, 3);
    tap_settings();
    CHECK(servo_screen_list()->open);
}

int main(void)
{
    RUN(every_gate_text_belongs_to_exactly_one_row);
    RUN(on_each_bench_the_gate_text_is_on_its_row_alone);
    RUN(the_hold_refuses_on_the_first_red_row_and_names_it);
    RUN(the_rows_the_hold_does_read_amber_only_while_safe);
    RUN(the_tags_share_the_left_edge_the_widest_state_needs);
    RUN(a_tap_on_port_opens_the_port_list);
    RUN(every_label_and_state_fits_its_column);
    RUN(the_card_takes_the_output_cards_place_on_every_function);
    RUN(every_function_screen_gives_the_footers_middle_button_one_owner);
    RUN(a_refused_hold_has_a_red_row_naming_it);
    RUN(a_press_on_a_row_ends_on_its_release_or_a_cancel);
    RUN(a_tap_on_a_red_zero_row_without_a_reading_names_it);
    RUN(a_tap_on_servo_opens_the_family_list_unless_refused);
    RUN(servo_reads_the_family_the_run_programs);
    RUN(the_sequence_names_its_steps_and_a_start_the_run_refuses);
    RUN(a_refusal_names_its_part_and_switches_every_output_off);
    RUN(a_read_back_that_runs_out_names_its_part_and_switches_off);
    RUN(a_stop_or_a_leave_ends_the_sequence_as_each_must);
    RUN(a_finger_on_a_ready_row_keeps_the_list_shut);
    return test_summary("servo_ready");
}
