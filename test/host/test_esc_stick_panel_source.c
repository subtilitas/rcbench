/**
 * @file test_esc_stick_panel_source.c
 * @brief ESC STICK's beep source: the phase tap while its capture runs, the
 * INA3221 channel in the ESC's supply line otherwise, RUN refused with
 * neither; the tap's beeps handed to the run from the readout, and the ends
 * a beep missing or a capture stopping give; the warning and the run's card
 * naming the source; the side column stating a profile's beep lengths.
 *
 * Cases:
 * - run_is_refused_with_no_beep_source
 * - the_tap_counts_when_it_runs_and_the_meter_otherwise
 * - the_readouts_beeps_reach_the_run_oldest_first
 * - beeps_past_the_readout_end_the_run_beep_lost
 * - a_tap_that_stops_ends_the_run_and_draws_its_reason
 * - the_warning_names_the_source_the_run_counts_from
 * - the_side_column_states_a_profiles_beep_lengths
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

/* Where the screen draws the lines these cases read, in body coordinates,
 * mirrored from esc_stick_int.h. */
#define LINE_P     20                    /**< a line's pitch                 */
#define SIDE_IN    688                   /**< the side column's text's left  */
#define SIDE_R     (W - UI_MARGIN)       /**< the side column's right edge   */
#define DEV_TOP    150                   /**< its first device line: HAND_Y
                                              148, 2px under it            */
#define RUN_L0     220                   /**< the run card's first line      */
#define WARN_L0    (PROG_FULL_Y + 68)        /**< the warning's first line       */
#define WARN_P     22                    /**< its lines' pitch               */

/** @brief The tap running, read, with @p n beeps numbered up to @p newest. */
static tone_readout_t tap_running(uint16_t newest, unsigned n)
{
    tone_readout_t t;
    memset(&t, 0, sizeof(t));
    t.state = TONE_STATE_RUNNING;
    t.read = true;
    t.n = (uint8_t)n;
    for (unsigned i = 0u; i < n && i < TONE_LINK_SHOWN; ++i) {
        t.beeps[i].seq = (uint16_t)(newest - i);
        t.beeps[i].start_ms = 10000u + 500u * (newest - i);
        t.beeps[i].len_dms = 1279u;
        t.beeps[i].freq_dhz = 20900u;
    }
    return t;
}

/** @brief Rows @p y0 to @p y0 + 19 of the canvas, which hold one line of
 *         text, as one number.
 *  @param y0 The line's top.
 *  @return The rows' hash. */
static uint32_t line_hash(int y0)
{
    uint32_t h = 2166136261u;
    for (int y = y0; y < y0 + LINE_P; ++y) {
        for (int x = 0; x < W; ++x) {
            h = (h ^ (uint32_t)fb[(size_t)y * W + x]) * 16777619u;
        }
    }
    return h;
}

/** @brief Pixels of colour @p col in the line that starts at @p y0, from
 *         @p x0 to @p x1.
 *  @param y0  The line's top.
 *  @param x0  Its left.
 *  @param x1  Its right, not included.
 *  @param col The theme colour.
 *  @return The pixels. */
static int ink_in(int y0, int x0, int x1, ui_color_id_t col)
{
    int n = 0;
    for (int y = y0; y < y0 + LINE_P; ++y) {
        for (int x = x0; x < x1; ++x) {
            n += fb[(size_t)y * W + x] == ui_theme_color(col);
        }
    }
    return n;
}

/** @brief Pixels of the faint text colour in the side column's line that
 *         starts at @p y0.
 *  @param y0 The line's top.
 *  @return The pixels. */
static int side_faint(int y0)
{
    return ink_in(y0, SIDE_IN, SIDE_R, UI_C_TEXT_FAINT);
}

/** @brief The tap off, as SETUP has it by default. */
static void tap_off(void)
{
    tone_readout_t t;
    memset(&t, 0, sizeof(t));
    esc_stick_panel_tone(&t);
}

/** @brief Hobbywing's page with a change picked and the warning's hold held:
 *         a run, if the bench can carry one. */
static void hold_a_run(void)
{
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);
    hold_for(2.25f);
}

TEST_CASE(run_is_refused_with_no_beep_source)
{
    fresh();
    tap_off();
    descend_to_hobbywing();
    pick_cutoff();
    /* The meter's last window 1500ms old, the supply read off just now. */
    for (uint32_t t = 1500u; t <= 1800u; t += 100u) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.samples = (uint16_t)(t / 100u);
        st.taken_ms = t;
        st.stamped = true;
        st.mode = SUPPLY_MODE_OFF;
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        esc_stick_panel_supply(&st);
    }
    esc_stick_panel_bench(1800u, false, 0u, 0u, false);
    scr->tick(0.02f);
    const uint32_t runs = esc_stick_panel_runs();
    tap(WRITE_X, BTN_CY);
    CHECK_STR_EQ(esc_stick_panel_note(), TR(SP_NO_SOURCE));
    hold_for(2.25f);
    CHECK_EQ(esc_stick_panel_runs(), runs);
    CHECK(!esc_stick_running(esc_stick_panel_stick()));

    /* A window now: RUN is offered again. */
    esc_stick_panel_meter(RIG_METER_CH, 2u, 0, true);
    scr->tick(0.02f);
    CHECK(strcmp(esc_stick_panel_note(), TR(SP_NO_SOURCE)) != 0);
}

TEST_CASE(the_tap_counts_when_it_runs_and_the_meter_otherwise)
{
    fresh();
    tap_off();
    hold_a_run();
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_METER);
    static rig_t r;
    rig_start(&r);
    /* The tap coming up during the run changes nothing: the source is the
     * run's from its start. */
    tone_readout_t t = tap_running(5u, 4u);
    esc_stick_panel_tone(&t);
    for (int i = 0; i < 2000; ++i) {
        rig_step(&r);
    }
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_METER);
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    /* The run's card names the source on its fifth line. */
    draws();
    const uint32_t meter_line = line_hash(RUN_L0 + 4 * LINE_P);
    rig_run(&r, 300000u);
    CHECK_EQ(esc_stick_panel_stick()->phase, ESC_STICK_DONE);

    /* Running at RUN: the tap counts. */
    fresh();
    esc_stick_panel_tone(&t);
    hold_a_run();
    CHECK(esc_stick_running(esc_stick_panel_stick()));
    CHECK_EQ(esc_stick_panel_stick()->src, ESC_STICK_SRC_TAP);
    CHECK_EQ(esc_stick_panel_stick()->tap_seq, 5u);
    draws();
    CHECK(line_hash(RUN_L0 + 4 * LINE_P) != meter_line);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    (void)drain_cmds(MOTOR_CMD_ARM);
    tap_off();
}

TEST_CASE(the_readouts_beeps_reach_the_run_oldest_first)
{
    fresh();
    tone_readout_t t = tap_running(65534u, 2u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->src, ESC_STICK_SRC_TAP);
    CHECK_EQ(run->tap_seq, 65534u);
    /* Three more, across the numbers' wrap: 65535, 1, 2. */
    t.n = 4u;
    t.beeps[0].seq = 2u;
    t.beeps[1].seq = 1u;
    t.beeps[2].seq = 65535u;
    t.beeps[3].seq = 65534u;
    t.beeps[0].start_ms = 13000u;
    t.beeps[1].start_ms = 12500u;
    t.beeps[2].start_ms = 12000u;
    t.beeps[3].start_ms = 11500u;
    esc_stick_panel_tone(&t);
    CHECK(esc_stick_running(run));
    CHECK_EQ(run->tap_seq, 2u);
    CHECK(run->tap_rx);
    CHECK_EQ(run->tap_end, 13000u + 128u);   /* 127.9ms, in whole ms */
    /* The same readout again hands over nothing. */
    esc_stick_panel_tone(&t);
    CHECK(esc_stick_running(run));
    CHECK_EQ(run->tap_seq, 2u);
    tap(WRITE_X, BTN_CY);                    /* ABORT */
    (void)drain_cmds(MOTOR_CMD_ARM);
    tap_off();
}

/* Five beeps between two frames: the readout holds four, and the run never
 * gets the first of them. */
TEST_CASE(beeps_past_the_readout_end_the_run_beep_lost)
{
    fresh();
    tone_readout_t t = tap_running(10u, 4u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK_EQ(run->src, ESC_STICK_SRC_TAP);
    t = tap_running(15u, 4u);
    esc_stick_panel_tone(&t);
    CHECK(!esc_stick_running(run));
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_LOST);
    CHECK(!esc_stick_out(run)->supply_on);
    CHECK(!esc_stick_out(run)->arm);

    /* The tap's own lost count moving ends it the same way. */
    fresh();
    t = tap_running(10u, 4u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    t.lost = 1u;
    esc_stick_panel_tone(&t);
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_LOST);
    /* The result in red, with the reason and its help. */
    scr->tick(0.02f);
    draws();
    bool red = false, green = true;
    esc_stick_panel_lights(&red, &green);
    CHECK(red && !green);
    tap_off();
}

TEST_CASE(a_tap_that_stops_ends_the_run_and_draws_its_reason)
{
    fresh();
    tone_readout_t t = tap_running(3u, 3u);
    esc_stick_panel_tone(&t);
    hold_a_run();
    const esc_stick_t *run = esc_stick_panel_stick();
    CHECK(esc_stick_running(run));
    t.state = TONE_STATE_WAITING;            /* its reads gone stale */
    t.read = false;
    esc_stick_panel_tone(&t);
    CHECK(!esc_stick_running(run));
    CHECK_EQ(run->reason, ESC_STICK_R_TAP_STOP);
    scr->tick(0.02f);
    draws();
    bool red = false, green = true;
    esc_stick_panel_lights(&red, &green);
    CHECK(red && !green);
    tap_off();
}

/* The warning's last line names where the run counts the beeps: the tap
 * while its capture runs, the INA3221 channel once it stops; the four
 * lines above it stay as they are. */
TEST_CASE(the_warning_names_the_source_the_run_counts_from)
{
    fresh();
    tone_readout_t t = tap_running(3u, 3u);
    esc_stick_panel_tone(&t);
    descend_to_hobbywing();
    pick_cutoff();
    tap(WRITE_X, BTN_CY);                    /* the warning */
    draws();
    const int y5 = WARN_L0 + 4 * WARN_P;
    CHECK(ink_in(WARN_L0, 0, W, UI_C_TEXT) > 0);
    CHECK(ink_in(y5, 0, W, UI_C_TEXT) > 0);
    const uint32_t above = line_hash(WARN_L0);
    const uint32_t tap_line = line_hash(y5);
    esc_stick_panel_meter(RIG_METER_CH, 2u, 0, true);
    tap_off();
    scr->tick(0.02f);
    draws();
    CHECK_EQ(line_hash(WARN_L0), above);
    CHECK(ink_in(y5, 0, W, UI_C_TEXT) > 0);
    CHECK(line_hash(y5) != tap_line);
}

/* A profile that states its beep lengths (the FlyDragon's measured 127.9ms
 * and 383.7ms) shows BEEP MIN and LONG, then GAP MIN, on the two lines
 * under the supply and entry lines; one that states none leaves them out,
 * and the picked item's values start there. */
TEST_CASE(the_side_column_states_a_profiles_beep_lengths)
{
    fresh();
    ui_text_set_language(UI_LANG_EN);
    open_profile("flycolor-flydragon-lite-plus");
    const esc_profile_t *p = esc_stick_panel_page();
    CHECK(p != NULL && p->manual_count == 0u);
    draws();
    /* Three lines of how the menu works, two of the supply and entry. */
    const int y0 = DEV_TOP + 5 * LINE_P;
    CHECK(side_faint(y0) > 0);
    CHECK(side_faint(y0 + LINE_P) > 0);

    fresh();
    descend_to_hobbywing();
    p = esc_stick_panel_page();
    CHECK(p != NULL && p->manual_count == 0u && p->beep_ms == 0u);
    draws();
    CHECK_EQ(side_faint(y0), 0);
}

int main(void)
{
    RUN(run_is_refused_with_no_beep_source);
    RUN(the_tap_counts_when_it_runs_and_the_meter_otherwise);
    RUN(the_readouts_beeps_reach_the_run_oldest_first);
    RUN(beeps_past_the_readout_end_the_run_beep_lost);
    RUN(a_tap_that_stops_ends_the_run_and_draws_its_reason);
    RUN(the_warning_names_the_source_the_run_counts_from);
    RUN(the_side_column_states_a_profiles_beep_lengths);
    return test_summary("esc_stick_panel_source");
}
