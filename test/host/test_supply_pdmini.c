/**
 * @file test_supply_pdmini.c
 * @brief SUPPLY > SETTINGS' tabs LIMITS | PD MINI: the PD mini's enable, TX,
 *        RX and baud on their own tab.  A change applies at once and is saved
 *        when the bench is idle; no row takes a press while the output is on or
 *        an ON is on its way, while a programming port holds the supply or
 *        while the bench is ARMED, so a mistaken pin cuts no live supply.
 *        LIMITS' reset leaves the wiring.  The stored keys load unchanged.  The
 *        SIMULATED line opens the tab.
 *
 * SPDX-License-Identifier: MIT
 */
#include "supply_screen_rig.h"

/** @brief Times the observer saw each setting move. */
static int s_seen[SETTING_COUNT];

/**
 * @brief Counts a setting's change.
 * @param id the setting
 */
static void seen(setting_id_t id)
{
    ++s_seen[id];
}

/**
 * @brief The wiring changes the observer has seen.
 * @return their count
 */
static int wiring_edits(void)
{
    return s_seen[SET_PDMINI_EN] + s_seen[SET_PDMINI_TX]
           + s_seen[SET_PDMINI_RX] + s_seen[SET_PDMINI_BAUD];
}

static bool s_armed;

/**
 * @brief The bench's ARMED, as settings_set_armed() asks it.
 * @return true while ARMED
 */
static bool armed_now(void)
{
    return s_armed;
}

/** @brief SUPPLY on the PD MINI tab of SETTINGS, on a fresh bench. */
static void on_tab(void)
{
    s_armed = false;
    fresh();
    settings_set_armed(armed_now);
    memset(s_seen, 0, sizeof(s_seen));
    settings_set_observer(seen);
    scr->enter();
    tap(SET_TAB_X, STRIP_Y);
    tap(SU_OV_PDMINI_X, SU_OV_TAB_Y);
    CHECK_EQ(supply_screen_settings_tab(), 1);
}

/** @brief No observer and no ARMED source, for the next case. */
static void off_tab(void)
{
    settings_set_observer(NULL);
    settings_set_armed(NULL);
    s_armed = false;
}

/** @brief The output as a hold on OUTPUT ON leaves it: asked, not yet seen. */
static void hold_output_on(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_screen_output_live());
}

/* --------------------------------------------------------------- the tabs */

/* LIMITS is what SETTINGS opens on.  A tab shows its own rows only: a tap
 * where the other has a row does nothing. */
TEST_CASE(limits_comes_first_and_a_tab_takes_only_its_own_rows)
{
    fresh();
    scr->enter();
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_settings_tab(), 0);
    /* PD mini's value, on LIMITS: CURRENT TRIP's row and its keypad. */
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
    CHECK(supply_screen_keypad()->open);
    key(UI_KEY_CANCEL);
    tap(SU_OV_PDMINI_X, SU_OV_TAB_Y);
    CHECK_EQ(supply_screen_settings_tab(), 1);
    tap(ROW_L_X, VMAX_Y);
    CHECK(!supply_screen_keypad()->open);
    tap(SU_OV_LIMITS_X, SU_OV_TAB_Y);
    CHECK_EQ(supply_screen_settings_tab(), 0);
    tap(ROW_L_X, VMAX_Y);
    CHECK(supply_screen_keypad()->open);
}

/* A press the tracker lost, and a slide off the tab, switch no tab. */
TEST_CASE(a_release_the_finger_did_not_make_switches_no_tab)
{
    fresh();
    scr->enter();
    tap(SET_TAB_X, STRIP_Y);
    ev(SU_OV_PDMINI_X, SU_OV_TAB_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(SU_OV_PDMINI_X, SU_OV_TAB_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_settings_tab(), 0);
    ev(SU_OV_PDMINI_X, SU_OV_TAB_Y, TOUCH_EVENT_DOWN, 2);
    ev(SU_OV_LIMITS_X, SU_OV_TAB_Y + 200, TOUCH_EVENT_UP, 2);
    CHECK_EQ(supply_screen_settings_tab(), 0);
}

/* A reset of LIMITS' category and RESET PD MINI: the wiring stays. */
TEST_CASE(limits_reset_leaves_the_wiring)
{
    fresh();
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 8.0f);
    settings_set(SET_PDMINI_RX, 9.0f);
    settings_set(SET_PDMINI_BAUD, 3.0f);
    settings_set(SET_SUPPLY_V_MAX, 9.0f);
    settings_reset(SET_CAT_SUPPLY);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX),
               settings_def(SET_SUPPLY_V_MAX)->def, 1e-6f);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 3);

    scr->enter();
    supply_screen_set_model(false);
    tap(SET_TAB_X, STRIP_Y);
    tap(SU_MODRESET_X, SU_MODRESET_Y);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.module_reset);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);
    /* PD MINI's tab has no restart where LIMITS has it. */
    tap(SU_OV_PDMINI_X, SU_OV_TAB_Y);
    tap(SU_MODRESET_X, SU_MODRESET_Y);
    CHECK(!supply_screen_poll_cmd(&c));
}

/* ---------------------------------------------------------- what a row does */

/* Enable flips and baud steps round its 8 options, each change seen at once
 * by the observer and asked to be saved, nothing asked of the supply. */
TEST_CASE(a_change_applies_at_once_and_is_saved_when_idle)
{
    on_tab();
    CHECK(!settings_save_asked());
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 1);
    CHECK_EQ(s_seen[SET_PDMINI_EN], 1);
    CHECK(settings_save_asked());
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);

    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 7);    /* AUTO */
    tap(SU_PD_PLUS_X, SU_PD_Y(3));                     /* round to the first */
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 0);
    tap(SU_PD_MINUS_X, SU_PD_Y(3));
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 7);
    tap(SU_PD_VALUE_X, SU_PD_Y(3));
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 0);
    CHECK(!supply_screen_keypad()->open);
    CHECK(!supply_screen_poll_cmd(NULL));
    off_tab();
}

/* TX's limits: -1 by its - key, 47 at the top, over the pin RX holds; a release
 * off the key steps nothing. */
TEST_CASE(the_pin_keys_stop_at_the_limits_and_step_over_the_other_pin)
{
    on_tab();
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    tap(SU_PD_MINUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 0);
    tap(SU_PD_MINUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    settings_set(SET_PDMINI_TX, 47.0f);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 47);
    settings_set(SET_PDMINI_TX, 8.0f);
    settings_set(SET_PDMINI_RX, 9.0f);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    tap(SU_PD_MINUS_X, SU_PD_Y(2));
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 8);
    ev(SU_PD_PLUS_X, SU_PD_Y(2), TOUCH_EVENT_DOWN, 1);
    ev(SU_PD_PLUS_X, SU_PD_Y(3) + 200, TOUCH_EVENT_UP, 1);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 8);
    off_tab();
}

/* The pin's keypad: the other pin's GPIO refused with its reason, EN and DE,
 * the keypad open and the pin as it was; 0, the lowest, taken from -1 on a
 * keypad without a sign key. */
TEST_CASE(the_pin_keypad_refuses_the_other_pin_and_takes_0)
{
    static const struct { ui_lang_t lang; const char *why; } k[] = {
        { UI_LANG_EN, "The other PD mini pin holds this GPIO" },
        { UI_LANG_DE, "Diesen GPIO hält der andere PD-mini-Pin" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        on_tab();
        ui_text_set_language(k[i].lang);
        settings_set(SET_PDMINI_RX, 9.0f);
        tap(SU_PD_VALUE_X, SU_PD_Y(1));
        const ui_keypad_t *kp = supply_screen_keypad();
        CHECK(kp->open);
        CHECK(!kp->sign);
        CHECK_NEAR(kp->min, -1.0f, 1e-6f);
        key(UI_KEY_9);
        key(UI_KEY_OK);
        CHECK(kp->open);
        CHECK(kp->refused);
        CHECK_STR_EQ(kp->reason, k[i].why);
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
        key(UI_KEY_CLR);
        key(UI_KEY_0);
        key(UI_KEY_OK);
        CHECK(!kp->open);
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), 0);
        CHECK(settings_save_asked());
        off_tab();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* -------------------------------------------------------------- the locks */

/** @brief A press on every control of every row. */
static void press_everything(void)
{
    for (int r = 0; r < 4; ++r) {
        tap(SU_PD_VALUE_X, SU_PD_Y(r));
        CHECK(!supply_screen_keypad()->open);
        tap(SU_PD_MINUS_X, SU_PD_Y(r));
        tap(SU_PD_PLUS_X, SU_PD_Y(r));
    }
}

/** @brief The wiring holds what lock_case() stored. */
#define WIRING_KEPT()                                       \
    do {                                                    \
        CHECK_EQ(settings_get_int(SET_PDMINI_EN), 1);       \
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);       \
        CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);       \
        CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 3);     \
    } while (0)

/** @brief The tab on a bench wired, nothing seen or asked since. */
static void lock_case(void)
{
    on_tab();
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 8.0f);
    settings_set(SET_PDMINI_RX, 9.0f);
    settings_set(SET_PDMINI_BAUD, 3.0f);
    memset(s_seen, 0, sizeof(s_seen));
}

/* The output on as reported, whichever screen switched it on: no row takes a
 * press, so the control task sees no edit and cuts nothing.  Off again they
 * do. */
TEST_CASE(no_row_changes_while_the_output_is_on)
{
    lock_case();
    supply_screen_set_output(true);
    scr->tick(TICK_S);
    press_everything();
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);
    CHECK(!settings_save_asked());
    supply_screen_set_output(false);
    scr->tick(TICK_S);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    off_tab();
}

/* An ON on its way counts: asked and not collected, and collected and not yet
 * reported. */
TEST_CASE(no_row_changes_while_an_on_is_on_its_way)
{
    lock_case();
    hold_output_on();
    press_everything();
    WIRING_KEPT();
    drain();
    CHECK(supply_screen_output_live());
    press_everything();
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);
    off_tab();
}

/* A programming port on the PD mini holds the supply, the output off between
 * its cycles or not. */
TEST_CASE(no_row_changes_while_a_port_holds_the_supply)
{
    lock_case();
    supply_screen_set_port_held(true);
    press_everything();
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);
    supply_screen_set_port_held(false);
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
    off_tab();
}

/* ARMED: SAFE-only on the settings' own rule, so no path sets them, not a row,
 * not settings_set() and not a reset; SAFE again they move. */
TEST_CASE(no_row_changes_while_the_bench_is_armed)
{
    lock_case();
    s_armed = true;
    press_everything();
    WIRING_KEPT();
    CHECK(settings_safe_only(SET_PDMINI_EN));
    CHECK(settings_safe_only(SET_PDMINI_TX));
    CHECK(settings_safe_only(SET_PDMINI_RX));
    CHECK(settings_safe_only(SET_PDMINI_BAUD));
    CHECK(!settings_safe_only(SET_HITEC_RX));
    settings_set(SET_PDMINI_TX, 20.0f);
    settings_adjust(SET_PDMINI_BAUD, 1);
    settings_reset(SET_CAT_PDMINI);
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);
    s_armed = false;
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    off_tab();
}

/* The lock names itself on the band at the press, each its own words. */
TEST_CASE(a_refused_press_says_why_on_the_band)
{
    lock_case();
    ui_router_set_alert(NULL);
    supply_screen_set_output(true);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_PD_LOCK_ON));
    supply_screen_set_output(false);
    supply_screen_set_port_held(true);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_PD_LOCK_PORT));
    supply_screen_set_port_held(false);
    s_armed = true;
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    CHECK_STR_EQ(ui_router_alert(), TR(MO_SET_SAFE_ONLY));
    ui_router_set_alert(NULL);
    off_tab();
}

/* A keypad open as the output comes on goes, what it holds not taken; an OK in
 * the frame before that tick takes nothing; a press the lock overtakes changes
 * nothing at its release. */
TEST_CASE(a_lock_ends_the_keypad_and_the_press_under_way)
{
    lock_case();
    tap(SU_PD_VALUE_X, SU_PD_Y(1));
    CHECK(supply_screen_keypad()->open);
    key(UI_KEY_2);
    key(UI_KEY_0);
    hold_output_on();
    scr->tick(TICK_S);
    CHECK(!supply_screen_keypad()->open);
    WIRING_KEPT();

    lock_case();
    tap(SU_PD_VALUE_X, SU_PD_Y(1));
    key(UI_KEY_2);
    key(UI_KEY_0);
    supply_screen_set_output(true);
    key(UI_KEY_OK);
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);

    for (int how = 0; how < 3; ++how) {
        lock_case();
        ev(SU_PD_PLUS_X, SU_PD_Y(1), TOUCH_EVENT_DOWN, 1);
        if (how == 0) {
            supply_screen_set_output(true);
        } else if (how == 1) {
            supply_screen_set_port_held(true);
        } else {
            s_armed = true;
        }
        ev(SU_PD_PLUS_X, SU_PD_Y(1), TOUCH_EVENT_UP, 1);
        WIRING_KEPT();
        CHECK_EQ(wiring_edits(), 0);
        off_tab();
    }
}

/* A keypad on a pin goes with the task and the screen; a lost release takes no
 * step. */
TEST_CASE(a_keypad_goes_with_the_task_and_the_screen_and_a_lost_press_steps_nothing)
{
    lock_case();
    tap(SU_PD_VALUE_X, SU_PD_Y(1));
    tap(LIVE_TAB_X, STRIP_Y);
    CHECK(!supply_screen_keypad()->open);
    tap(SET_TAB_X, STRIP_Y);
    CHECK_EQ(supply_screen_settings_tab(), 1);
    tap(SU_PD_VALUE_X, SU_PD_Y(1));
    scr->leave();
    CHECK(!supply_screen_keypad()->open);
    WIRING_KEPT();

    lock_case();
    ev(SU_PD_PLUS_X, SU_PD_Y(1), TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(SU_PD_PLUS_X, SU_PD_Y(1), TOUCH_EVENT_UP, 1);
    WIRING_KEPT();
    off_tab();
}

/* ------------------------------------------------------------- the carrier */

/* The carrier solders TX and RX: no key, no keypad; enable and baud take
 * presses; the stored pins come back off the carrier. */
TEST_CASE(soldered_pins_take_no_press_and_enable_and_baud_stay)
{
    lock_case();
    CHECK(settings_fix(SET_PDMINI_TX, 7));
    CHECK(settings_fix(SET_PDMINI_RX, 6));
    tap(SU_PD_VALUE_X, SU_PD_Y(1));
    CHECK(!supply_screen_keypad()->open);
    tap(SU_PD_PLUS_X, SU_PD_Y(1));
    tap(SU_PD_MINUS_X, SU_PD_Y(2));
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 7);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 6);
    tap(SU_PD_PLUS_X, SU_PD_Y(3));
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 4);
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
    (void)settings_unfix(SET_PDMINI_TX);
    (void)settings_unfix(SET_PDMINI_RX);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);
    off_tab();
}

/* A board found while the tab shows: the rows repaint without a touch. */
TEST_CASE(a_pin_soldered_while_the_tab_shows_repaints_it)
{
    lock_case();
    scr->render(&cv, 0);
    scr->tick(TICK_S);
    scr->render(&cv, 0);
    const size_t n = (size_t)W * H * sizeof(gfx_color_t);
    gfx_color_t *was = malloc(n);
    REQUIRE_ALLOC(was);
    memcpy(was, fb, n);
    CHECK(settings_fix(SET_PDMINI_TX, 7));
    scr->tick(TICK_S);
    scr->render(&cv, 0);
    CHECK(memcmp(was, fb, n) != 0);
    free(was);
    (void)settings_unfix(SET_PDMINI_TX);
    off_tab();
}

/* --------------------------------------------------------------- the store */

static float s_store[SETTING_COUNT];

/**
 * @brief Saves nothing: the case reads back what it stored.
 * @param values unused
 * @param count unused
 * @return true
 */
static bool store_save(const float *values, int count)
{
    (void)values;
    (void)count;
    return true;
}

/**
 * @brief Hands back the values the case stored, as the NVS store would.
 * @param values takes them
 * @param count their number
 * @return true
 */
static bool store_load(float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        values[i] = s_store[i];
    }
    return true;
}

/* A panel in the field: its stored words, by the same keys, load unchanged. */
TEST_CASE(the_stored_keys_are_the_old_ones_and_their_values_load)
{
    CHECK_STR_EQ(settings_def(SET_PDMINI_EN)->key, "pdm_en");
    CHECK_STR_EQ(settings_def(SET_PDMINI_TX)->key, "pdm_tx");
    CHECK_STR_EQ(settings_def(SET_PDMINI_RX)->key, "pdm_rx");
    CHECK_STR_EQ(settings_def(SET_PDMINI_BAUD)->key, "pdm_baud");
    CHECK_STR_EQ(settings_def(SET_HITEC_RX)->key, "hitec_rx");
    CHECK_EQ(settings_def(SET_PDMINI_EN)->cat, SET_CAT_PDMINI);
    CHECK_EQ(settings_def(SET_PDMINI_BAUD)->cat, SET_CAT_PDMINI);
    CHECK_EQ(settings_def(SET_HITEC_RX)->cat, SET_CAT_SERVO);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_store[i] = settings_def((setting_id_t)i)->def;
    }
    s_store[SET_PDMINI_EN] = 1.0f;
    s_store[SET_PDMINI_TX] = 19.0f;
    s_store[SET_PDMINI_RX] = 18.0f;
    s_store[SET_PDMINI_BAUD] = 2.0f;
    s_store[SET_HITEC_RX] = 1.0f;
    static const settings_store_t k_store = {
        .load = store_load, .save = store_save,
    };
    settings_set_store(&k_store);
    settings_init();
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 19);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 18);
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 2);
    CHECK_EQ(settings_get_int(SET_HITEC_RX), 1);
    settings_set_store(NULL);
    settings_init();
}

/* -------------------------------------------------------- the SIMULATED line */

/* On the model it opens PD MINI: from LIVE, from LIMITS, over a keypad, which
 * goes.  Not on a slide off, a lost release, or on the PD mini. */
TEST_CASE(the_simulated_line_opens_the_pd_mini_tab)
{
    fresh();
    scr->enter();
    supply_screen_set_model(true);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(supply_screen_settings_tab(), 1);

    /* Above the 40px button: the target is a finger tall. */
    tap(SU_OV_LIMITS_X, SU_OV_TAB_Y);
    tap(SU_MODEL_X, 2);
    CHECK_EQ(supply_screen_settings_tab(), 1);

    tap(LIVE_TAB_X, STRIP_Y);
    tap(CARD_X, CARD_V_Y);
    CHECK(supply_screen_keypad()->open);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK(!supply_screen_keypad()->open);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);

    tap(LIVE_TAB_X, STRIP_Y);
    ev(SU_MODEL_X, SU_MODEL_Y, TOUCH_EVENT_DOWN, 1);
    ev(SU_MODEL_X, SU_MODEL_Y + 200, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    ev(SU_MODEL_X, SU_MODEL_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(SU_MODEL_X, SU_MODEL_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);

    supply_screen_set_model(false);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
}

/* A question on a live output is answered first; then the tab opens with its
 * rows locked. */
TEST_CASE(the_simulated_line_waits_for_a_question)
{
    fresh();
    scr->enter();
    supply_screen_set_model(true);
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(DISCARD_X, ASK_Y);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap(SU_PD_VALUE_X, SU_PD_Y(0));
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
}

/* The source names the tab, EN and DE.  A cold render of the tab, free and
 * locked, paints its value boxes inside the page. */
TEST_CASE(the_line_names_the_tab_and_the_tab_draws_in_both_languages)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        scr->enter();
        supply_screen_set_model(true);
        char line[80];
        supply_screen_header_text(line, sizeof(line));
        CHECK(strstr(line, "PD MINI") != NULL);
        tap(SU_MODEL_X, SU_MODEL_Y);
        scr->render(&cv, 0);
        supply_screen_set_output(true);
        scr->tick(TICK_S);
        scr->render(&cv, 0);
        supply_screen_set_output(false);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(limits_comes_first_and_a_tab_takes_only_its_own_rows);
    RUN(a_release_the_finger_did_not_make_switches_no_tab);
    RUN(limits_reset_leaves_the_wiring);
    RUN(a_change_applies_at_once_and_is_saved_when_idle);
    RUN(the_pin_keys_stop_at_the_limits_and_step_over_the_other_pin);
    RUN(the_pin_keypad_refuses_the_other_pin_and_takes_0);
    RUN(no_row_changes_while_the_output_is_on);
    RUN(no_row_changes_while_an_on_is_on_its_way);
    RUN(no_row_changes_while_a_port_holds_the_supply);
    RUN(no_row_changes_while_the_bench_is_armed);
    RUN(a_refused_press_says_why_on_the_band);
    RUN(a_lock_ends_the_keypad_and_the_press_under_way);
    RUN(a_keypad_goes_with_the_task_and_the_screen_and_a_lost_press_steps_nothing);
    RUN(soldered_pins_take_no_press_and_enable_and_baud_stay);
    RUN(a_pin_soldered_while_the_tab_shows_repaints_it);
    RUN(the_stored_keys_are_the_old_ones_and_their_values_load);
    RUN(the_simulated_line_opens_the_pd_mini_tab);
    RUN(the_simulated_line_waits_for_a_question);
    RUN(the_line_names_the_tab_and_the_tab_draws_in_both_languages);
    free(fb);
    free(fb2);
    return test_summary("supply_pdmini");
}
