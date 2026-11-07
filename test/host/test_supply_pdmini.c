/**
 * @file test_supply_pdmini.c
 * @brief SUPPLY's SETTINGS list, the PD mini's rows under their heading:
 *        enable, TX, RX and baud.  A change applies at once and SAVE keeps
 *        it; no row takes a press while the output is on or an ON is on its
 *        way, while a programming port holds the supply or while the bench
 *        is ARMED, so a mistaken pin cuts no live supply, and the band says
 *        why.  The list opens with the output off only and closes when it
 *        goes on.  A reset of the supply's category leaves the wiring.  The
 *        stored keys load unchanged.  The SIMULATED line opens the list at
 *        the PD mini's heading.
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

/** @brief The PD mini's rows, top down. */
static const setting_id_t k_pd[4] = {
    SET_PDMINI_EN, SET_PDMINI_TX, SET_PDMINI_RX, SET_PDMINI_BAUD,
};

/**
 * @brief Taps a part of PD mini row @p r, the row shown first.
 * @param r    0 enable, 1 TX, 2 RX, 3 baud
 * @param part the part
 */
static void pd_tap(int r, ui_sl_part_t part)
{
    const int at = supply_settings_row(k_pd[r]);
    show_row(at);
    const gfx_rect_t b =
        ui_setlist_row_part(supply_settings_list(), at, part);
    tap(b.x + b.w / 2, b.y + b.h / 2);
}

/**
 * @brief Where part @p part of PD mini row @p r is, the row shown first.
 * @param r    the row
 * @param part the part
 * @param x    takes the middle's x
 * @param y    takes the middle's y
 */
static void pd_at(int r, ui_sl_part_t part, int *x, int *y)
{
    const int at = supply_settings_row(k_pd[r]);
    show_row(at);
    const gfx_rect_t b =
        ui_setlist_row_part(supply_settings_list(), at, part);
    *x = b.x + b.w / 2;
    *y = b.y + b.h / 2;
}

#define PD_MINUS(r) pd_tap((r), UI_SL_PART_MINUS)
#define PD_VALUE(r) pd_tap((r), UI_SL_PART_VALUE)
#define PD_PLUS(r)  pd_tap((r), UI_SL_PART_PLUS)

/** @brief SUPPLY's list at the PD mini's heading, on a fresh bench. */
static void on_tab(void)
{
    s_armed = false;
    fresh();
    settings_set_armed(armed_now);
    memset(s_seen, 0, sizeof(s_seen));
    settings_set_observer(seen);
    scr->enter();
    supply_screen_open_pdmini();
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

/* ------------------------------------------------------------ the sections */

/* The footer's SETTINGS opens the list at its top, the limits; the PD mini's
 * opener at its heading, the four rows whole in the pane.  A drag from the
 * top reaches them too. */
TEST_CASE(settings_opens_at_the_limits_and_the_opener_at_the_wiring)
{
    fresh();
    scr->enter();
    open_settings();
    CHECK_EQ(supply_screen_settings_tab(), 0);
    CHECK(scr->back());
    supply_screen_open_pdmini();
    CHECK_EQ(supply_screen_settings_tab(), 1);
    const gfx_rect_t pane = ui_setlist_pane(supply_settings_list());
    for (int r = 0; r < 4; ++r) {
        const gfx_rect_t b = ui_setlist_row_rect(
            supply_settings_list(), supply_settings_row(k_pd[r]));
        CHECK(b.y >= pane.y && b.y + b.h <= pane.y + pane.h);
    }
    CHECK(scr->back());
    open_settings();                          /* the top again */
    CHECK_EQ(supply_screen_settings_tab(), 0);
    for (int k = 0; k < 6; ++k) {
        drag_list(-200);
    }
    CHECK_EQ(supply_screen_settings_tab(), 1);
}

/* A reset of the supply's category and RESET PD MINI leave the wiring;
 * RESET SUPPLY in the strip sets it to its defaults while its rows take a
 * press, and leaves it while a port holds the supply. */
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
    open_settings();
    const int row = supply_settings_modreset_row();
    show_row(row);
    const gfx_rect_t r = ui_setlist_row_rect(supply_settings_list(), row);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.module_reset);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);

    /* RESET SUPPLY: under a port's hold the wiring stays. */
    settings_set(SET_SUPPLY_V_MAX, 9.0f);
    supply_screen_set_port_held(true);
    tap(SU_LIST_RESET_X, SU_LIST_STRIP_Y);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX),
               settings_def(SET_SUPPLY_V_MAX)->def, 1e-6f);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    supply_screen_set_port_held(false);
    tap(SU_LIST_RESET_X, SU_LIST_STRIP_Y);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX),
             (int)settings_def(SET_PDMINI_TX)->def);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN),
             (int)settings_def(SET_PDMINI_EN)->def);
    /* Nothing else: a setting of another screen stays. */
    CHECK(!supply_screen_poll_cmd(&c));
}

/* ---------------------------------------------------------- what a row does */

/* Enable flips and baud steps round its 8 options, each change seen at once
 * by the observer; SAVE asks for the write; nothing asked of the supply. */
TEST_CASE(a_change_applies_at_once_and_save_keeps_it)
{
    on_tab();
    CHECK(!settings_save_asked());
    PD_PLUS(0);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 1);
    CHECK_EQ(s_seen[SET_PDMINI_EN], 1);
    CHECK(!settings_save_asked());
    PD_MINUS(0);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);

    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 7);    /* AUTO */
    PD_PLUS(3);                                        /* round to the first */
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 0);
    PD_MINUS(3);
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 7);
    PD_VALUE(3);                                       /* a choice: no keypad */
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 7);
    CHECK_EQ(supply_screen_keypad_setting(), -1);
    PD_PLUS(3);
    list_save();
    CHECK(settings_save_asked());
    CHECK(!supply_screen_poll_cmd(NULL));
    off_tab();
}

/* TX's limits: -1 by its - key, 47 at the top, over the pin RX holds; a release
 * off the key steps nothing. */
TEST_CASE(the_pin_keys_stop_at_the_limits_and_step_over_the_other_pin)
{
    on_tab();
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    PD_MINUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    PD_PLUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 0);
    PD_MINUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    settings_set(SET_PDMINI_TX, 47.0f);
    PD_PLUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 47);
    settings_set(SET_PDMINI_TX, 8.0f);
    settings_set(SET_PDMINI_RX, 9.0f);
    PD_PLUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    PD_MINUS(2);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 8);
    int x, y;
    pd_at(2, UI_SL_PART_PLUS, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x - 200, y, TOUCH_EVENT_MOVE, 1);
    ev(x - 200, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 8);
    off_tab();
}

/* The pin's keypad: the other pin's GPIO refused with its reason, EN and DE,
 * the keypad open and the pin as it was; 0, the lowest, taken from -1 on a
 * keypad without a sign key. */
TEST_CASE(the_pin_keypad_refuses_the_other_pin_and_takes_0)
{
    static const struct { ui_lang_t lang; const char *why; } k[] = {
        { UI_LANG_EN, "GPIO already used by the other PD mini pin" },
        { UI_LANG_DE, "GPIO vom anderen PD-mini-Pin belegt" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        on_tab();
        ui_text_set_language(k[i].lang);
        settings_set(SET_PDMINI_RX, 9.0f);
        PD_VALUE(1);
        const ui_keypad_t *kp = supply_settings_keypad();
        CHECK(kp->open);
        CHECK(!kp->sign);
        CHECK_NEAR(kp->min, -1.0f, 1e-6f);
        list_key(UI_KEY_9);
        list_key(UI_KEY_OK);
        CHECK(kp->open);
        CHECK(kp->refused);
        CHECK_STR_EQ(kp->reason, k[i].why);
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
        list_key(UI_KEY_CLR);
        list_key(UI_KEY_0);
        list_key(UI_KEY_OK);
        CHECK(!kp->open);
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), 0);
        off_tab();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* -------------------------------------------------------------- the locks */

/** @brief A press on every control of every row. */
static void press_everything(void)
{
    for (int r = 0; r < 4; ++r) {
        PD_VALUE(r);
        CHECK(!supply_settings_keypad()->open);
        PD_MINUS(r);
        PD_PLUS(r);
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

/** @brief The list on a bench wired, nothing seen or asked since. */
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
 * press before the next tick, which closes the list, so the control task sees
 * no edit and cuts nothing.  The list does not open again until the output is
 * off; then the rows take presses. */
TEST_CASE(no_row_changes_while_the_output_is_on)
{
    lock_case();
    supply_screen_set_output(true);
    press_everything();
    WIRING_KEPT();
    scr->tick(TICK_S);
    CHECK(!supply_screen_settings_shown());
    supply_screen_open_pdmini();
    CHECK(!supply_screen_settings_shown());
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);
    CHECK(!settings_save_asked());
    supply_screen_set_output(false);
    scr->tick(TICK_S);
    supply_screen_open_pdmini();
    PD_PLUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    off_tab();
}

/* An ON on its way counts: asked and not collected, and collected and not yet
 * reported.  The list does not open over either. */
TEST_CASE(no_row_changes_while_an_on_is_on_its_way)
{
    lock_case();
    list_save();
    CHECK(scr->back());                       /* LIVE: OUTPUT ON in reach */
    CHECK(!supply_screen_settings_shown());
    hold_output_on();
    supply_screen_open_pdmini();
    CHECK(!supply_screen_settings_shown());
    drain();
    CHECK(supply_screen_output_live());
    open_settings();
    CHECK(!supply_screen_settings_shown());
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
    PD_PLUS(0);
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
    PD_PLUS(1);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 10);
    off_tab();
}

/* The lock names itself on the band at the press, each its own words. */
TEST_CASE(a_refused_press_says_why_on_the_band)
{
    lock_case();
    ui_router_set_alert(NULL);
    supply_screen_set_output(true);
    PD_PLUS(1);
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_PD_LOCK_ON));
    supply_screen_set_output(false);
    supply_screen_set_port_held(true);
    PD_PLUS(1);
    CHECK_STR_EQ(ui_router_alert(), TR(SUP_PD_LOCK_PORT));
    supply_screen_set_port_held(false);
    s_armed = true;
    PD_PLUS(1);
    CHECK_STR_EQ(ui_router_alert(), TR(MO_SET_SAFE_ONLY));
    ui_router_set_alert(NULL);
    off_tab();
}

/* A keypad open as the output comes on goes, what it holds not taken, and the
 * list with it; an OK in the frame before that tick takes nothing; a press the
 * lock overtakes changes nothing at its release. */
TEST_CASE(a_lock_ends_the_keypad_and_the_press_under_way)
{
    lock_case();
    PD_VALUE(1);
    CHECK(supply_settings_keypad()->open);
    list_key(UI_KEY_2);
    list_key(UI_KEY_0);
    supply_screen_set_output(true);
    scr->tick(TICK_S);
    CHECK(!supply_settings_keypad()->open);
    CHECK(!supply_screen_settings_shown());
    WIRING_KEPT();

    lock_case();
    PD_VALUE(1);
    list_key(UI_KEY_2);
    list_key(UI_KEY_0);
    supply_screen_set_output(true);
    list_key(UI_KEY_OK);
    WIRING_KEPT();
    CHECK_EQ(wiring_edits(), 0);

    for (int how = 0; how < 3; ++how) {
        lock_case();
        int x, y;
        pd_at(1, UI_SL_PART_PLUS, &x, &y);
        ev(x, y, TOUCH_EVENT_DOWN, 1);
        if (how == 0) {
            supply_screen_set_output(true);
        } else if (how == 1) {
            supply_screen_set_port_held(true);
        } else {
            s_armed = true;
        }
        ev(x, y, TOUCH_EVENT_UP, 1);
        WIRING_KEPT();
        CHECK_EQ(wiring_edits(), 0);
        off_tab();
    }
}

/* A keypad on a pin goes with the home tag and the screen; a lost release
 * takes no step. */
TEST_CASE(a_keypad_goes_with_the_tag_and_the_screen_and_a_lost_press_steps_nothing)
{
    lock_case();
    PD_VALUE(1);
    CHECK(scr->back());
    CHECK(!supply_settings_keypad()->open);
    CHECK_EQ(supply_screen_settings_tab(), 1);
    PD_VALUE(1);
    scr->leave();
    CHECK(!supply_settings_keypad()->open);
    CHECK(!supply_screen_settings_shown());
    WIRING_KEPT();

    lock_case();
    int x, y;
    pd_at(1, UI_SL_PART_PLUS, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(x, y, TOUCH_EVENT_UP, 1);
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
    PD_VALUE(1);
    CHECK(!supply_settings_keypad()->open);
    PD_PLUS(1);
    PD_MINUS(2);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 7);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 6);
    PD_PLUS(3);
    CHECK_EQ(settings_get_int(SET_PDMINI_BAUD), 4);
    PD_PLUS(0);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
    (void)settings_unfix(SET_PDMINI_TX);
    (void)settings_unfix(SET_PDMINI_RX);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 9);
    off_tab();
}

/* A board found while the list shows: the rows repaint without a touch. */
TEST_CASE(a_pin_soldered_while_the_list_shows_repaints_it)
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

/* On the model it opens the list at the PD mini's heading: from LIVE, over a
 * set point's keypad, which goes, and from the row above the 40px button.
 * Not on a slide off, a lost release, or on the PD mini. */
TEST_CASE(the_simulated_line_opens_the_list_at_the_pd_mini)
{
    fresh();
    scr->enter();
    supply_screen_set_model(true);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(supply_screen_settings_tab(), 1);
    CHECK(scr->back());

    /* Above the 40px button: the target is a finger tall. */
    tap(SU_MODEL_X, 2);
    CHECK_EQ(supply_screen_settings_tab(), 1);
    CHECK(scr->back());

    tap(CARD_X, CARD_V_Y);
    CHECK(supply_screen_keypad()->open);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK(!supply_screen_keypad()->open);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK(scr->back());

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

/* A question on a live output is answered first; with the output still on
 * the line opens nothing, and the band says why. */
TEST_CASE(the_simulated_line_opens_nothing_on_a_live_output)
{
    fresh();
    scr->enter();
    supply_screen_set_model(true);
    supply_screen_set_output(true);
    tap(V_UP_X, V_ROW_Y);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    tap(DISCARD_X, ASK_Y);
    ui_router_set_alert(NULL);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK_STR_EQ(ui_router_alert(), TR(FOOT_SETTINGS_ON));
    ui_router_set_alert(NULL);
    supply_screen_set_output(false);
    tap(SU_MODEL_X, SU_MODEL_Y);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    PD_VALUE(0);
    CHECK_EQ(settings_get_int(SET_PDMINI_EN), 0);
}

/* The source names the PD mini, EN and DE.  A cold render of the list, and
 * of LIVE after the output closed it, in both languages. */
TEST_CASE(the_line_names_the_pd_mini_and_the_list_draws_in_both_languages)
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
        CHECK(!supply_screen_settings_shown());
        supply_screen_set_output(false);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(settings_opens_at_the_limits_and_the_opener_at_the_wiring);
    RUN(limits_reset_leaves_the_wiring);
    RUN(a_change_applies_at_once_and_save_keeps_it);
    RUN(the_pin_keys_stop_at_the_limits_and_step_over_the_other_pin);
    RUN(the_pin_keypad_refuses_the_other_pin_and_takes_0);
    RUN(no_row_changes_while_the_output_is_on);
    RUN(no_row_changes_while_an_on_is_on_its_way);
    RUN(no_row_changes_while_a_port_holds_the_supply);
    RUN(no_row_changes_while_the_bench_is_armed);
    RUN(a_refused_press_says_why_on_the_band);
    RUN(a_lock_ends_the_keypad_and_the_press_under_way);
    RUN(a_keypad_goes_with_the_tag_and_the_screen_and_a_lost_press_steps_nothing);
    RUN(soldered_pins_take_no_press_and_enable_and_baud_stay);
    RUN(a_pin_soldered_while_the_list_shows_repaints_it);
    RUN(the_stored_keys_are_the_old_ones_and_their_values_load);
    RUN(the_simulated_line_opens_the_list_at_the_pd_mini);
    RUN(the_simulated_line_opens_nothing_on_a_live_output);
    RUN(the_line_names_the_pd_mini_and_the_list_draws_in_both_languages);
    free(fb);
    free(fb2);
    return test_summary("supply_pdmini");
}
