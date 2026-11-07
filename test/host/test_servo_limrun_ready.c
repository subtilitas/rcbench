/**
 * @file test_servo_limrun_ready.c
 * @brief LIMIT RUN's READY card (servo_ready.c, servo_limit_ready.c) and
 *        its list (servo_lists.c): every text of the start gate belongs to
 *        exactly one row, in both languages; on each bench state the
 *        gate's text is on its row and no row shows another row's; a row
 *        is red whenever one of its texts refuses the hold; the rows ARM +
 *        START does itself read amber only while SAFE, a dead rail among
 *        them where the hold's supply feeds it; a tap on the grey hold
 *        names the first red row; SERVO off a PROG PORT names why; the
 *        list holds MODE, SERVO, SERVO PULSES and the programmer's rows.
 *
 * Cases:
 * - every_gate_text_belongs_to_exactly_one_row
 * - on_each_bench_the_gate_text_is_on_its_row_alone
 * - a_refused_hold_has_a_red_row_naming_it
 * - the_rows_the_hold_does_read_amber_only_while_safe
 * - a_tap_on_the_grey_hold_names_the_first_red_row
 * - servo_off_a_prog_port_names_why_and_port_opens_the_list
 * - the_card_and_the_hold_take_the_footer_and_the_output_cards_place
 * - the_list_holds_mode_servo_pulses_and_the_programmers_rows
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_ready_int.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_widgets.h"

/** @brief A text the gate returns, and the row it belongs to. */
typedef struct {
    int          kind;  /**< 0 a TX_ text, 1 a SERVO_STR_ text */
    int          id;    /**< its ID                             */
    limrun_row_t row;   /**< its row                            */
} gate_text_t;

/** @brief Every fixed text servo_screen_limit_blocked() can return, listed here on its
 *         own, with the row it belongs to.  The Hitec range's text is
 *         formatted and is held apart (servo_limit_hitec's suite). */
static const gate_text_t k_gate[] = {
    { 1, SERVO_STR_START_PROG_5V, LRR_SUPPLY },
    { 1, SERVO_STR_START_NOT_ARMED, LRR_ARM },
    { 1, SERVO_STR_START_TEST_RUNS, LRR_ARM },
    { 1, SERVO_STR_START_AUTO_RUNS, LRR_ARM },
    { 1, SERVO_STR_START_BUSY, LRR_ARM },
    { 0, TX_SV_TARGET_NONE, LRR_PORT },
    { 0, TX_SV_TARGET_UNREAD, LRR_PORT },
    { 0, TX_SV_LIM_PROG_PORT, LRR_PORT },
    { 0, TX_SV_LIM_INA_SETUP, LRR_METER },
    { 0, TX_SV_LIM_INA_OFFLINE, LRR_METER },
    { 0, TX_SV_LIM_INA_NO_WIN, LRR_METER },
    { 0, TX_SV_LIM_RAIL_LOW, LRR_RAIL },
    { 0, TX_SV_LIM_PROG_RAIL, LRR_RAIL },
    { 0, TX_SV_LIM_REFUSED, LRR_RANGE },
    { 0, TX_OUT_PORT_HELD, LRR_PROG },
    { 0, TX_SV_LIM_PROG_BUSY, LRR_PROG },
    { 0, TX_HP_NEED_PD_C, LRR_SUPPLY },
    { 0, TX_SV_LIM_PROG_PD_OFF, LRR_SUPPLY },
    { 0, TX_SV_LIM_PROG_LOW, LRR_SUPPLY },
    { 0, TX_SV_LIM_PROG_NO_PD, LRR_SUPPLY },
    { 0, TX_SV_TAP_HV, LRR_SUPPLY },
    { 0, TX_SV_LIM_PWM_SAFE, LRR_PWM },
    { 0, TX_SV_LIM_PWM_OFF, LRR_PWM },
    { 0, TX_SV_LIM_PWM_OFF_ARMED, LRR_PWM },
};
#define N_GATE ((int)(sizeof(k_gate) / sizeof(k_gate[0])))

/** @brief Gate text @p g in the language showing. */
static const char *gate_str(const gate_text_t *g)
{
    return g->kind == 0 ? ui_tr((ui_text_id_t)g->id)
                        : ui_servo_str((servo_str_t)g->id);
}

/** @brief LIMIT RUN shown on a fresh bench, SAFE or ARMED. */
static void to_limrun(bool armed)
{
    if (armed) {
        fresh_armed();
    } else {
        fresh();
    }
    scr->enter();
    go(SERVO_FN_LIMRUN);
    ui_router_set_alert(NULL);
}

/** @brief The bench a run may start on: a SERVO PWM output bound, the
 *         INA3221 the meter with a 6.0V window, the PD mini's output on. */
static void to_ready(bool armed)
{
    to_limrun(armed);
    servo_screen_set_pins((uint64_t)1u << 2, true);
    settings_set(SET_INA_FIT(0), 1.0f);
    meter(SERVO_SOURCE_INA3221);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = true;
    st.set_v = supply_screen_set_v();
    st.v = st.set_v;
    st.samples = 1u;
    servo_screen_supply(&st);
    rail_window(100, 6000u);
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
    rdy_row_t r[LRR_COUNT];
    for (int i = 0; i < (int)LRR_COUNT; ++i) {
        if (!lim_ready_row(i, &r[i])) {
            return false;
        }
        if (r[i].why != NULL && lim_row_of(r[i].why) != (limrun_row_t)i) {
            return false;
        }
    }
    const char *g = servo_screen_limit_blocked();
    if (g == NULL) {
        return true;
    }
    if (lim_row_of(g) == LRR_COUNT) {
        return false;
    }
    int on = 0;
    for (int i = 0; i < (int)LRR_COUNT; ++i) {
        on += (r[i].why == g);
    }
    /* Its row shows it, or a text of the same row that refuses the hold
     * where the gate's own is one the hold does. */
    const rdy_row_t *mine = &r[lim_row_of(g)];
    if (mine->why == g) {
        return on == 1;
    }
    return on == 0 && lim_seq_fixes(g) && mine->st == (uint8_t)RDY_BAD;
}

/**
 * @brief A hold refused is named on a red row: the row of
 *        lim_hold_blocked()'s text reads red with that text, and every row
 *        with a text that refuses reads red, but the rows that wait on a
 *        red PORT.
 * @return false on a refusal no red row names
 */
static bool refusal_on_a_red_row(void)
{
    limrun_row_t row = LRR_COUNT;
    const char *hb = lim_hold_blocked(&row);
    rdy_row_t r;
    if (hb != NULL) {
        if (!lim_ready_row((int)row, &r) || r.st != (uint8_t)RDY_BAD
            || r.why != hb) {
            return false;
        }
    }
    rdy_row_t port;
    (void)lim_ready_row(LRR_PORT, &port);
    for (int i = 0; i < (int)LRR_COUNT; ++i) {
        (void)lim_ready_row(i, &r);
        if (r.why != NULL && !lim_seq_fixes(r.why)
            && r.st != (uint8_t)RDY_BAD
            && !(r.st == (uint8_t)RDY_IDLE && port.st == (uint8_t)RDY_BAD)) {
            return false;
        }
    }
    return true;
}

/** @brief The benches the cases walk, each checked by @p ok.
 *  @param ok the check  @return false on the first bench that fails it */
static bool on_each_bench(bool (*ok)(void))
{
    bool all = true;
    to_limrun(false);
    all = all && ok();
    to_limrun(true);
    all = all && ok();
    to_ready(false);
    all = all && ok();
    to_ready(true);
    all = all && ok();
    /* A PROG PORT, its pulse off and on, SAFE and ARMED. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, false);
    all = all && servo_screen_limit_blocked() != NULL && ok();
    servo_screen_set_drive(1u, 13u, true);
    all = all && ok();
    servo_screen_set_armed(false);
    servo_screen_set_drive(1u, 13u, false);
    all = all && ok();
    /* A report on its way, the rail dead, the link down. */
    servo_screen_test_logger_busy(true);
    all = all && ok();
    servo_screen_test_logger_busy(false);
    rail_window(0, 0u);
    all = all && ok();
    servo_screen_set_link(false);
    all = all && ok();
    return all;
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
            CHECK_EQ(lim_row_of(t), k_gate[k].row);
            for (int j = 0; j < N_GATE; ++j) {
                if (j != k) {
                    CHECK(gate_str(&k_gate[j]) != t);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(lim_row_of(NULL), LRR_COUNT);
    CHECK_EQ(lim_row_of(TR(SV_LIM_SEARCH_NOTE)), LRR_COUNT);
    CHECK_EQ(lim_row_of("INA3221 OFFLINE"), LRR_COUNT); /* a copy: none */
    /* What a failed step names for each row's texts. */
    CHECK_EQ(lim_kind_of(TR(SV_LIM_RAIL_LOW)), RDY_K_SUPPLY);
    CHECK_EQ(lim_kind_of(TR(SV_LIM_PROG_RAIL)), RDY_K_SUPPLY);
    CHECK_EQ(lim_kind_of(TR(SV_LIM_PROG_PD_OFF)), RDY_K_SUPPLY);
    CHECK_EQ(lim_kind_of(TR(SV_LIM_PWM_SAFE)), RDY_K_PWM);
    CHECK_EQ(lim_kind_of(ui_servo_str(SERVO_STR_START_BUSY)), RDY_K_ARM);
    CHECK_EQ(lim_kind_of(TR(SV_LIM_INA_OFFLINE)), RDY_K_OTHER);
}

/* On a SAFE and an ARMED bench, unready and ready, on a PROG PORT with
 * its pulse off and on, with a report on its way, a dead rail and no
 * link: the gate's text is on its row and every row shows only its own. */
TEST_CASE(on_each_bench_the_gate_text_is_on_its_row_alone)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(on_each_bench(card_holds_the_gate));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* On the same benches the hold refused is named on a red row, and a row
 * any of whose texts refuses reads red: REPORT STILL BEING WRITTEN on ARM
 * is red, not hidden behind START ARMS. */
TEST_CASE(a_refused_hold_has_a_red_row_naming_it)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(on_each_bench(refusal_on_a_red_row));
        to_ready(false);
        servo_screen_test_logger_busy(true);
        CHECK_STR_EQ(lim_hold_blocked(NULL),
                     ui_servo_str(SERVO_STR_START_BUSY));
        rdy_row_t r;
        CHECK(lim_ready_row(LRR_ARM, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK_STR_EQ(r.why, ui_servo_str(SERVO_STR_START_BUSY));
        servo_screen_test_logger_busy(false);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* NOT ARMED is the hold's on any bench.  A rail under 3.0V is the hold's
 * only while SAFE and where the hold's supply feeds it: the PD mini off
 * on the outputs' rail; with its output already on, a rail that reads
 * dead refuses.  ARMED it refuses. */
TEST_CASE(the_rows_the_hold_does_read_amber_only_while_safe)
{
    to_ready(false);
    rdy_row_t r;
    CHECK(lim_ready_row(LRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ARM));
    CHECK(lim_hold_blocked(NULL) == NULL);
    /* The PD mini's output off, the rail dead: RAIL amber, the hold
     * offered, the PD mini the part it switches. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = false;
    st.set_v = supply_screen_set_v();
    st.samples = 2u;
    servo_screen_supply(&st);
    rail_window(0, 0u);
    CHECK(lim_ready_row(LRR_RAIL, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_SEQ);
    CHECK_STR_EQ(r.why, TR(SV_LIM_RAIL_LOW));
    CHECK_STR_EQ(r.value, TR(RDY_SEQ_ON));
    CHECK(lim_hold_blocked(NULL) == NULL);
    CHECK(svs_seq_need().pd_mini);
    /* The PD mini off on a rail fed from elsewhere, on OUTPUTS: nothing
     * refuses, and the hold leaves the PD mini off. */
    rail_window(100, 6000u);
    CHECK(lim_hold_blocked(NULL) == NULL);
    CHECK(!svs_seq_need().pd_mini);
    /* With its output on the rail reads dead: a broken feed, red. */
    st.output = true;
    st.samples = 3u;
    servo_screen_supply(&st);
    rail_window(0, 0u);
    CHECK(lim_ready_row(LRR_RAIL, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK_STR_EQ(lim_hold_blocked(NULL), TR(SV_LIM_RAIL_LOW));
    /* ARMED: the PD mini off and the dead rail refuse. */
    st.output = false;
    st.samples = 4u;
    servo_screen_supply(&st);
    servo_screen_set_armed(true);
    CHECK(!lim_seq_fixes(TR(SV_LIM_RAIL_LOW)));
    CHECK(lim_seq_fixes(ui_servo_str(SERVO_STR_START_NOT_ARMED)));
    CHECK(lim_ready_row(LRR_RAIL, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_BAD);
    CHECK(lim_ready_row(LRR_ARM, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_LIVE);
    CHECK_STR_EQ(r.value, "ARMED");
    CHECK(!lim_seq_fixes(NULL));
    CHECK(!lim_ready_row(LRR_COUNT, &r));
    CHECK(!lim_ready_row(-1, &r));
    CHECK(!lim_ready_row(0, NULL));
}

/* With the INA3221 not set up the METER row is red: the hold refuses, 2.5s
 * held start nothing, its refusal names METER and its text, and the
 * card's first state line says it. */
TEST_CASE(a_tap_on_the_grey_hold_names_the_first_red_row)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        to_limrun(false);
        servo_screen_set_pins((uint64_t)1u << 2, true);
        limrun_row_t row = LRR_COUNT;
        CHECK_STR_EQ(lim_hold_blocked(&row), TR(SV_LIM_INA_SETUP));
        CHECK_EQ(row, LRR_METER);
        char want[128], got[128], line[96];
        snprintf(want, sizeof(want), TR(RDY_BAND), TR(RDY_METER),
                 TR(SV_LIM_INA_SETUP));
        CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
        CHECK_STR_EQ(servo_screen_limit_line(0, line, sizeof(line)),
                     TR(SV_LIM_INA_SETUP));
        ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_DOWN, 1);
        frames(2.5f);
        ev(SV_HOLD_X, SV_HOLD_Y, TOUCH_EVENT_UP, 1);
        CHECK(!svs_seq_busy());
        CHECK(!servo_screen_limit_running());
        /* A tap on it hands ready_refusal()'s line to the band as a
         * notice (ui_action_refused(), test_hold_flash's router case). */
        tap(SV_HOLD_X, SV_HOLD_Y);
        CHECK(!svs_seq_busy());
        /* The rows behind a red PORT read grey; METER is red with a fix. */
        rdy_row_t r;
        CHECK(lim_ready_row(LRR_METER, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK(r.fix);
        scr->render(&cv, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* On OUTPUTS SERVO reads SEARCH ONLY in grey and a tap says it is a PROG
 * PORT's; PORT opens the choice of OUTPUTS and the four PROG PORTs. */
TEST_CASE(servo_off_a_prog_port_names_why_and_port_opens_the_list)
{
    to_ready(false);
    rdy_row_t r;
    CHECK(lim_ready_row(LRR_SERVO, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_IDLE);
    CHECK_STR_EQ(r.value, TR(SV_LIM_SEARCH));
    tap(SV_RDY_X, SV_RDY_ROW_Y(LRR_SERVO));
    char want[128];
    snprintf(want, sizeof(want), TR(RDY_BAND), TR(SVH_SERVO),
             TR(SVL_PORT_ONLY));
    CHECK_STR_EQ(alert(), want);
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_port_choices(), -1);
    tap(SV_RDY_X, SV_RDY_ROW_Y(LRR_PORT));
    CHECK_EQ(servo_screen_port_choices(), 5);
    scr->render(&cv, 0);
}

/* LIMIT RUN shows the card, its 9 rows, and ARM + START in the footer's
 * middle slot, which it names to the router as a stop only while a run is
 * under way.  Every label and state fits its column in both languages. */
TEST_CASE(the_card_and_the_hold_take_the_footer_and_the_output_cards_place)
{
    to_limrun(false);
    CHECK(ready_shown());
    CHECK_EQ(ready_count(), (int)LRR_COUNT);
    const gfx_rect_t h = ready_hold_rect();
    CHECK(gfx_rect_contains(h, SV_HOLD_X, SV_HOLD_Y));
    CHECK(!ready_hold_stops());
    CHECK(!scr->safe_at(SV_HOLD_X, SV_HOLD_Y));
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < (int)LRR_COUNT; ++i) {
            rdy_row_t r;
            CHECK(lim_ready_row(i, &r));
            CHECK(gfx_text_cells(r.label) * 10 <= READY_LABEL_W);
        }
        CHECK(gfx_text_cells(TR(SV_LIM_SEARCH)) * 10 <= READY_VALUE_W);
        CHECK(gfx_text_cells(TR(RDY_RAIL)) * 10 <= READY_LABEL_W);
        CHECK(gfx_text_cells(TR(LR_LOW_END)) * 10 <= 295 - 24);
        CHECK(gfx_text_cells(TR(LR_HIGH_END)) * 10 <= 295 - 24);
    }
    ui_text_set_language(UI_LANG_EN);
    go(SERVO_FN_TEST);
    CHECK(!ready_shown());
    go(SERVO_FN_LIMRUN);
    CHECK(ready_shown());
}

/* LIMIT RUN's list: MODE, SERVO, SERVO PULSES, PROG VOLTAGE, PROG CURRENT
 * and PROG WAIT.  On OUTPUTS MODE, SERVO and SERVO PULSES are locked with
 * the reason; on a PROG PORT behind a PD mini a tap on MODE switches it to
 * SEARCH + PROGRAM. */
TEST_CASE(the_list_holds_mode_servo_pulses_and_the_programmers_rows)
{
    to_ready(false);
    settings_request_save();        /* no leave question on the tag */
    tap_settings();
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(sl->host->count(), 6);
    CHECK_EQ(sl->host->row(0).kind, (int)UI_SL_CUSTOM);
    CHECK_EQ(sl->host->row(1).kind, (int)UI_SL_CUSTOM);
    CHECK_EQ(sl->host->row(2).kind, (int)UI_SL_CUSTOM);
    CHECK_EQ(sl->host->row(3).id, SET_SERVO_PROG_V);
    CHECK_EQ(sl->host->row(4).id, SET_SERVO_PROG_I);
    CHECK_EQ(sl->host->row(5).id, SET_SERVO_PROG_WAIT);
    CHECK_STR_EQ(sl->host->row(0).custom->locked(), TR(SVL_PORT_ONLY));
    sl_tap(LL_MODE);
    CHECK(!servo_screen_limit_prog());
    scr->render(&cv, 0);
    CHECK(home_tag());
    /* A PROG PORT behind a PD mini: a tap on MODE switches it. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    tap_settings();
    CHECK(servo_screen_list()->open);
    CHECK(sl->host->row(0).custom->locked() == NULL);
    sl_tap(LL_MODE);
    CHECK(servo_screen_limit_prog());
    scr->render(&cv, 0);
    /* The list closes as the bench arms under it. */
    servo_screen_set_armed(true);
    scr->tick(0.02f);
    CHECK(!servo_screen_list()->open);
}

int main(void)
{
    RUN(every_gate_text_belongs_to_exactly_one_row);
    RUN(on_each_bench_the_gate_text_is_on_its_row_alone);
    RUN(a_refused_hold_has_a_red_row_naming_it);
    RUN(the_rows_the_hold_does_read_amber_only_while_safe);
    RUN(a_tap_on_the_grey_hold_names_the_first_red_row);
    RUN(servo_off_a_prog_port_names_why_and_port_opens_the_list);
    RUN(the_card_and_the_hold_take_the_footer_and_the_output_cards_place);
    RUN(the_list_holds_mode_servo_pulses_and_the_programmers_rows);
    return test_summary("servo_limrun_ready");
}
