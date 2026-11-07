/**
 * @file test_setlist.c
 * @brief The settings list (ui_setlist.h) with a host of its own: SETUP's
 *        geometry without headings (64px rows at a 70px pitch, keys of
 *        56 x 48px), the 40px pitch of a heading, a key's step and its
 *        repeat, a drag that scrolls and steps nothing, the keypad and the
 *        host's typed() and range(), a custom row's step, tap and choice
 *        list, RESET of the list's own settings only, SAVE's states, the
 *        leave warning with STAY and DISCARD, and a locked row.
 *
 * Cases:
 * - without_headings_the_rows_are_setups
 * - a_heading_takes_a_40px_pitch
 * - a_key_steps_at_its_release_and_repeats_while_held
 * - a_drag_scrolls_and_steps_nothing
 * - a_tap_on_a_number_opens_the_keypad_through_the_hosts_hooks
 * - a_custom_row_steps_taps_and_offers_its_choices
 * - reset_sets_the_lists_settings_and_no_other
 * - save_is_offered_for_a_change_and_asks_for_the_write
 * - the_warning_holds_an_unsaved_change_and_discard_restores_it
 * - a_locked_row_takes_no_press_and_says_why
 * - a_discard_brings_supplys_limits_back_from_any_list
 * - a_pulse_step_keeps_the_throttle_gap
 * - a_closed_list_asks_for_no_repaint
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_theme.h"

/* --------------------------------------------------------- the custom rows */

static int s_steps;       /* the stepper's value */
static int s_taps;        /* taps on the tapper */
static int s_picked = -1; /* the choice taken */

static void stepper_text(char *out, size_t n) { snprintf(out, n, "%d", s_steps); }
static void stepper_step(int dir) { s_steps += dir; }
static void tapper_tap(void) { ++s_taps; }
static int chooser_count(void) { return 3; }
static const char *chooser_name(int i)
{
    static const char *k[3] = { "ONE", "TWO", "THREE" };
    return (i >= 0 && i < 3) ? k[i] : "";
}
static int chooser_chosen(void) { return s_picked; }
static void chooser_pick(int i) { s_picked = i; }

static const ui_setlist_custom_t k_stepper = {
    .label = TX_SET_SAVE, .help = TX_COUNT, .value_text = stepper_text,
    .step = stepper_step,
};
static const ui_setlist_custom_t k_tapper = {
    .label = TX_SET_RESET, .help = TX_COUNT, .tap = tapper_tap,
};
static const ui_setlist_custom_t k_chooser = {
    .label = TX_SET_STAY, .help = TX_COUNT, .choices = chooser_count,
    .choice = chooser_name, .chosen = chooser_chosen, .pick = chooser_pick,
};

/* ---------------------------------------------------------------- the host */

/** The list: a heading, three numbers, a heading, a number and three custom
 *  rows; SET_MOTOR_KV is none of its rows. */
static const ui_setlist_row_t k_rows[] = {
    UI_SL_ROW_HEADING(TX_MO_SET_PACK, NULL),     /* 0 */
    UI_SL_ROW_SETTING(SET_PACK_CELLS),           /* 1 */
    UI_SL_ROW_SETTING(SET_PACK_MAH),             /* 2 */
    UI_SL_ROW_SETTING(SET_PACK_MOHM),            /* 3 */
    UI_SL_ROW_HEADING(TX_MO_SET_MOTOR, NULL),    /* 4 */
    UI_SL_ROW_SETTING(SET_MOTOR_POLES),          /* 5 */
    UI_SL_ROW_CUSTOM(&k_stepper),                /* 6 */
    UI_SL_ROW_CUSTOM(&k_tapper),                 /* 7 */
    UI_SL_ROW_CUSTOM(&k_chooser),                /* 8 */
};
#define N_ROWS ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

static bool s_headings = true;
static int s_changed;
static int s_changed_all;
static int s_way_out;
static int s_invalidated;
static bool s_lock_cells;

static int host_count(void) { return N_ROWS; }
static ui_setlist_row_t host_row(int i)
{
    return ui_setlist_table_row(k_rows, N_ROWS, i);
}
static const char *host_name(void) { return "TEST"; }
static void host_changed(setting_id_t id)
{
    ++s_changed;
    s_changed_all += (id == SETTING_COUNT) ? 1 : 0;
}
static void host_way_out(void) { ++s_way_out; }
static void host_invalidate(void) { ++s_invalidated; }
static const char *host_locked(setting_id_t id)
{
    return (s_lock_cells && id == SET_PACK_CELLS) ? "CELLS LOCKED" : NULL;
}
/* Capacity typed rounds down to 1000mAh. */
static float host_typed(setting_id_t id, float v)
{
    return (id == SET_PACK_MAH) ? floorf(v / 1000.0f) * 1000.0f : v;
}
/* Capacity's keypad takes 1000 to 20000mAh. */
static void host_range(setting_id_t id, float *min, float *max)
{
    if (id == SET_PACK_MAH) {
        *min = 1000.0f;
        *max = 20000.0f;
    }
}

static ui_setlist_host_t s_host = {
    .count = host_count, .row = host_row, .headings = true,
    .name = host_name, .changed = host_changed, .way_out = host_way_out,
    .locked = host_locked, .typed = host_typed, .range = host_range,
    .invalidate = host_invalidate,
};

static ui_setlist_t sl;

/** @brief Fresh settings and an open list; with or without headings. */
static void fresh(bool headings)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    /* SUPPLY's caps, so its limits take the settings as they are. */
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_screen_set_caps(&caps);
    supply_screen_limits_changed();
    s_steps = 0;
    s_taps = 0;
    s_picked = -1;
    s_changed = 0;
    s_changed_all = 0;
    s_way_out = 0;
    s_invalidated = 0;
    s_lock_cells = false;
    s_headings = headings;
    s_host.headings = headings;
    ui_router_set_alert(NULL);
    ui_setlist_init(&sl, &s_host);
    ui_setlist_open(&sl);
}

static void ev(int x, int y, touch_event_type_t t)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = 1, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    ui_setlist_event(&sl, &e);
}
static void tap(int x, int y)
{
    ev(x, y, TOUCH_EVENT_DOWN);
    ev(x, y, TOUCH_EVENT_UP);
}
static void tap_rect(gfx_rect_t r) { tap(r.x + r.w / 2, r.y + r.h / 2); }
static void tap_part(int row, ui_sl_part_t part)
{
    tap_rect(ui_setlist_row_part(&sl, row, part));
}
static void key(ui_key_t k) { tap_rect(ui_keypad_key_rect(&sl.kp, k)); }

/* ------------------------------------------------------------------- cases */

TEST_CASE(without_headings_the_rows_are_setups)
{
    fresh(false);
    for (int i = 0; i < 4; ++i) {
        const gfx_rect_t r = ui_setlist_row_rect(&sl, i);
        CHECK_EQ(r.y, UI_SL_LIST_Y + i * 70);
        CHECK_EQ(r.h, UI_SL_ROW_H);
        CHECK_EQ(UI_SL_ROW_H, 64);
        const gfx_rect_t m = ui_setlist_row_part(&sl, i, UI_SL_PART_MINUS);
        const gfx_rect_t p = ui_setlist_row_part(&sl, i, UI_SL_PART_PLUS);
        CHECK_EQ(m.x, 704);
        CHECK_EQ(p.x, 926);
        CHECK_EQ(m.w, 56);
        CHECK_EQ(p.w, 56);
        /* A row owns the gap under it. */
        CHECK_EQ(ui_setlist_row_at(&sl, 300, r.y + 69), i);
        CHECK_EQ(ui_setlist_row_at(&sl, 300, r.y + 70), i + 1);
    }
    const gfx_rect_t pane = ui_setlist_pane(&sl);
    CHECK_EQ(pane.y, UI_SL_LIST_Y);
    CHECK_EQ(pane.h, 428);
    /* The strip at the footer's columns. */
    static const int k_x[3] = { 10, 348, 686 };
    for (int c = 0; c < 3; ++c) {
        const gfx_rect_t s = ui_setlist_strip_rect(c);
        CHECK_EQ(s.x, k_x[c]);
        CHECK_EQ(s.y, 444);
        CHECK_EQ(s.w, 328);
        CHECK_EQ(s.h, 48);
    }
}

TEST_CASE(a_heading_takes_a_40px_pitch)
{
    fresh(true);
    CHECK_EQ(ui_setlist_row_rect(&sl, 0).y, UI_SL_LIST_Y);
    CHECK_EQ(ui_setlist_row_rect(&sl, 0).h, UI_SL_HEAD_H);
    CHECK_EQ(ui_setlist_row_rect(&sl, 1).y, UI_SL_LIST_Y + 40);
    CHECK_EQ(ui_setlist_row_rect(&sl, 2).y, UI_SL_LIST_Y + 110);
    CHECK_EQ(ui_setlist_row_rect(&sl, 4).y, UI_SL_LIST_Y + 250);
    CHECK_EQ(ui_setlist_row_rect(&sl, 4).h, UI_SL_HEAD_H);
    CHECK_EQ(ui_setlist_row_rect(&sl, 5).y, UI_SL_LIST_Y + 290);
    /* A tap on a heading changes nothing. */
    tap(300, UI_SL_LIST_Y + 10);
    CHECK_EQ(s_changed, 0);
    /* 2 headings and 7 rows: 2 * 40 + 7 * 70 - 6 = 564px over a 428px
     * pane. */
    CHECK_EQ(sl.scroll_max, 564 - 428);
}

TEST_CASE(a_key_steps_at_its_release_and_repeats_while_held)
{
    fresh(true);
    const int mah = settings_get_int(SET_PACK_MAH);
    tap_part(2, UI_SL_PART_PLUS);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), mah + 100);
    CHECK(s_changed > 0);
    tap_part(2, UI_SL_PART_MINUS);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), mah);

    /* Held: nothing before 0.45s, then 8 steps a second. */
    const gfx_rect_t p = ui_setlist_row_part(&sl, 2, UI_SL_PART_PLUS);
    ev(p.x + p.w / 2, p.y + p.h / 2, TOUCH_EVENT_DOWN);
    for (int k = 0; k < 8; ++k) {
        ui_setlist_tick(&sl, 0.05f);
    }
    CHECK_EQ(settings_get_int(SET_PACK_MAH), mah);
    for (int k = 0; k < 21; ++k) {
        ui_setlist_tick(&sl, 0.05f);
    }
    const int steps = (settings_get_int(SET_PACK_MAH) - mah) / 100;
    CHECK(steps >= 7 && steps <= 10);
    ev(p.x + p.w / 2, p.y + p.h / 2, TOUCH_EVENT_UP);
    const int after = (settings_get_int(SET_PACK_MAH) - mah) / 100;
    CHECK(after - steps <= 1);
}

TEST_CASE(a_drag_scrolls_and_steps_nothing)
{
    fresh(true);
    const int cells = settings_get_int(SET_PACK_CELLS);
    const gfx_rect_t p = ui_setlist_row_part(&sl, 1, UI_SL_PART_PLUS);
    const int x = p.x + p.w / 2, y = p.y + p.h / 2;
    ev(x, y, TOUCH_EVENT_DOWN);
    for (int k = 1; k <= 10; ++k) {
        ev(x, y - 10 * k, TOUCH_EVENT_MOVE);
    }
    ev(x, y - 100, TOUCH_EVENT_UP);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), cells);
    CHECK_EQ(sl.scroll, 100);
    CHECK_EQ(ui_setlist_row_rect(&sl, 1).y, UI_SL_LIST_Y + 40 - 100);
    /* Past the end: held at scroll_max; scroll_to clamps the same. */
    ev(300, 400, TOUCH_EVENT_DOWN);
    ev(300, 100, TOUCH_EVENT_MOVE);
    ev(300, 100, TOUCH_EVENT_UP);
    CHECK_EQ(sl.scroll, sl.scroll_max);
    ui_setlist_scroll_to(&sl, 0);
    CHECK_EQ(sl.scroll, 0);
    ui_setlist_scroll_to(&sl, N_ROWS - 1);
    CHECK_EQ(sl.scroll, sl.scroll_max);
    /* Opening again starts at the top. */
    ui_setlist_open(&sl);
    CHECK_EQ(sl.scroll, 0);
}

TEST_CASE(a_tap_on_a_number_opens_the_keypad_through_the_hosts_hooks)
{
    fresh(true);
    tap_part(2, UI_SL_PART_VALUE);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), (int)SET_PACK_MAH);
    CHECK(ui_setlist_covered(&sl));
    CHECK_NEAR(sl.kp.min, 1000.0f, 1e-3f);       /* range() */
    CHECK_NEAR(sl.kp.max, 20000.0f, 1e-3f);
    key(UI_KEY_3);
    key(UI_KEY_4);
    key(UI_KEY_5);
    key(UI_KEY_6);
    key(UI_KEY_OK);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), -1);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 3000);  /* typed() */
    /* Outside range(): refused, the keypad stays, the value too. */
    tap_part(2, UI_SL_PART_VALUE);
    key(UI_KEY_5);
    key(UI_KEY_0);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), (int)SET_PACK_MAH);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 3000);
    /* The back way closes it with nothing taken. */
    CHECK(ui_setlist_back(&sl));
    CHECK_EQ(ui_setlist_keypad_setting(&sl), -1);
    CHECK(!ui_setlist_back(&sl));
}

TEST_CASE(a_custom_row_steps_taps_and_offers_its_choices)
{
    fresh(true);
    ui_setlist_scroll_to(&sl, N_ROWS - 1);
    tap_part(6, UI_SL_PART_PLUS);
    tap_part(6, UI_SL_PART_PLUS);
    tap_part(6, UI_SL_PART_MINUS);
    CHECK_EQ(s_steps, 1);
    tap_rect(ui_setlist_row_rect(&sl, 7));
    CHECK_EQ(s_taps, 1);
    tap_rect(ui_setlist_row_rect(&sl, 8));
    CHECK_EQ(sl.choice_row, 8);
    CHECK(ui_setlist_covered(&sl));
    tap_rect(ui_setlist_choice_rect(&sl, 2));
    CHECK_EQ(s_picked, 2);
    CHECK_EQ(sl.choice_row, -1);
    /* The back way closes an open choice list with nothing picked. */
    tap_rect(ui_setlist_row_rect(&sl, 8));
    CHECK(ui_setlist_back(&sl));
    CHECK_EQ(sl.choice_row, -1);
    CHECK_EQ(s_picked, 2);
}

TEST_CASE(reset_sets_the_lists_settings_and_no_other)
{
    fresh(true);
    settings_set(SET_PACK_MAH, 8000.0f);
    settings_set(SET_MOTOR_POLES, 20.0f);
    settings_set(SET_MOTOR_KV, 900.0f);          /* not a row of the list */
    tap_rect(ui_setlist_strip_rect(1));
    CHECK_NEAR(settings_get(SET_PACK_MAH), settings_def(SET_PACK_MAH)->def,
               1e-3f);
    CHECK_NEAR(settings_get(SET_MOTOR_POLES),
               settings_def(SET_MOTOR_POLES)->def, 1e-3f);
    CHECK_EQ(settings_get_int(SET_MOTOR_KV), 900);
    CHECK_EQ(s_changed_all, 1);
}

TEST_CASE(save_is_offered_for_a_change_and_asks_for_the_write)
{
    fresh(true);
    CHECK(!ui_setlist_save_offered());
    tap_rect(ui_setlist_strip_rect(2));          /* SAVED: inert */
    CHECK(!settings_save_asked());
    tap_part(1, UI_SL_PART_PLUS);
    CHECK(settings_dirty());
    CHECK(ui_setlist_save_offered());
    CHECK(!settings_save_asked());               /* no write by itself */
    tap_rect(ui_setlist_strip_rect(2));
    CHECK(settings_save_asked());
    CHECK(!ui_setlist_save_offered());           /* WHEN IDLE */
    settings_cancel_save();
}

TEST_CASE(the_warning_holds_an_unsaved_change_and_discard_restores_it)
{
    fresh(true);
    CHECK(!ui_setlist_warn(&sl));                /* nothing to keep */
    const int cells = settings_get_int(SET_PACK_CELLS);
    tap_part(1, UI_SL_PART_PLUS);
    CHECK(ui_setlist_warn(&sl));
    CHECK(sl.warn);
    char line[96];
    CHECK(ui_setlist_warn_line(&sl, 0, line, sizeof(line)));
    CHECK(line[0] != '\0');
    tap_rect(ui_setlist_strip_rect(0));          /* STAY */
    CHECK(!sl.warn);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), cells + 1);
    CHECK_EQ(s_way_out, 0);
    CHECK(ui_setlist_warn(&sl));
    tap_rect(ui_setlist_strip_rect(1));          /* DISCARD */
    CHECK(!sl.warn);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), cells);
    CHECK_EQ(s_way_out, 1);
    CHECK(s_changed_all > 0);
    /* SAVE on the warning asks for the write and goes the way out. */
    tap_part(1, UI_SL_PART_PLUS);
    CHECK(ui_setlist_warn(&sl));
    tap_rect(ui_setlist_strip_rect(2));
    CHECK(settings_save_asked());
    CHECK_EQ(s_way_out, 2);
    /* A save asked for holds nothing. */
    CHECK(!ui_setlist_warn(&sl));
    settings_cancel_save();
}

/*
 * SUPPLY's cap edited and left unsaved, then DISCARD in another list:
 * the store and SUPPLY's limits, which the control task's levels are
 * published from, both go back to the stored 21.00V.
 */
TEST_CASE(a_discard_brings_supplys_limits_back_from_any_list)
{
    fresh(true);
    supply_screen_limits_changed();
    CHECK_NEAR(supply_screen_limits().v_max, 21.0f, 1e-4f);
    settings_set(SET_SUPPLY_V_MAX, 10.0f);
    supply_screen_limits_changed();
    CHECK_NEAR(supply_screen_limits().v_max, 10.0f, 1e-4f);
    CHECK(ui_setlist_warn(&sl));
    tap_rect(ui_setlist_strip_rect(1));          /* DISCARD */
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK_NEAR(supply_screen_limits().v_max, 21.0f, 1e-4f);
}

/** The throttle pair's list: Idle pulse, then Full pulse. */
static const ui_setlist_row_t k_pulse_rows[] = {
    UI_SL_ROW_SETTING(SET_OUT_MIN_US),
    UI_SL_ROW_SETTING(SET_OUT_MAX_US),
};
static int pulse_count(void) { return 2; }
static ui_setlist_row_t pulse_row(int i)
{
    return ui_setlist_table_row(k_pulse_rows, 2, i);
}

/*
 * At 1400/1500us a "+" on Idle pulse or a "-" on Full pulse would close
 * the 100us gap the keypad and the IOmcu hold: the pair stays.  From
 * 1380/1500us a held "+" on Idle pulse stops at 1400us.
 */
TEST_CASE(a_pulse_step_keeps_the_throttle_gap)
{
    fresh(false);
    const ui_setlist_host_t host = {
        .count = pulse_count, .row = pulse_row, .name = host_name,
    };
    ui_setlist_init(&sl, &host);
    ui_setlist_open(&sl);
    settings_set(SET_OUT_MAX_US, 1500.0f);
    settings_set(SET_OUT_MIN_US, 1400.0f);
    tap_part(0, UI_SL_PART_PLUS);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), 1400);
    tap_part(1, UI_SL_PART_MINUS);
    CHECK_EQ(settings_get_int(SET_OUT_MAX_US), 1500);
    tap_part(1, UI_SL_PART_PLUS);
    CHECK_EQ(settings_get_int(SET_OUT_MAX_US), 1510);
    settings_set(SET_OUT_MAX_US, 1500.0f);
    settings_set(SET_OUT_MIN_US, 1380.0f);
    const gfx_rect_t p = ui_setlist_row_part(&sl, 0, UI_SL_PART_PLUS);
    ev(p.x + p.w / 2, p.y + p.h / 2, TOUCH_EVENT_DOWN);
    for (int k = 0; k < 40; ++k) {
        ui_setlist_tick(&sl, 0.05f);
    }
    ev(p.x + p.w / 2, p.y + p.h / 2, TOUCH_EVENT_UP);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), 1400);
    CHECK(settings_get_int(SET_OUT_MIN_US) + (int)LINK_CC_THROTTLE_GAP_US
          <= settings_get_int(SET_OUT_MAX_US));
    ui_setlist_init(&sl, &s_host);
}

/*
 * A list closed with a change unsaved: a save asked for after the close,
 * and frames after it, ask the host for no repaint -- nothing of the list
 * is on screen to show it.
 */
TEST_CASE(a_closed_list_asks_for_no_repaint)
{
    fresh(true);
    tap_part(1, UI_SL_PART_PLUS);
    ui_setlist_close(&sl);
    settings_request_save();
    const int before = s_invalidated;
    for (int k = 0; k < 10; ++k) {
        ui_setlist_tick(&sl, 0.025f);
    }
    CHECK_EQ(s_invalidated, before);
    settings_cancel_save();
}

TEST_CASE(a_locked_row_takes_no_press_and_says_why)
{
    fresh(true);
    s_lock_cells = true;
    const int cells = settings_get_int(SET_PACK_CELLS);
    tap_part(1, UI_SL_PART_PLUS);
    tap_part(1, UI_SL_PART_MINUS);
    tap_part(1, UI_SL_PART_VALUE);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), cells);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), -1);
    CHECK_STR_EQ(ui_router_alert() != NULL ? ui_router_alert() : "", "CELLS LOCKED");
    /* A keypad open when the lock comes closes at the next tick. */
    s_lock_cells = false;
    ui_router_set_alert(NULL);
    tap_part(1, UI_SL_PART_VALUE);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), (int)SET_PACK_CELLS);
    s_lock_cells = true;
    ui_setlist_tick(&sl, 0.05f);
    CHECK_EQ(ui_setlist_keypad_setting(&sl), -1);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), cells);
    ui_router_set_alert(NULL);
}

int main(void)
{
    RUN(without_headings_the_rows_are_setups);
    RUN(a_heading_takes_a_40px_pitch);
    RUN(a_key_steps_at_its_release_and_repeats_while_held);
    RUN(a_drag_scrolls_and_steps_nothing);
    RUN(a_tap_on_a_number_opens_the_keypad_through_the_hosts_hooks);
    RUN(a_custom_row_steps_taps_and_offers_its_choices);
    RUN(reset_sets_the_lists_settings_and_no_other);
    RUN(save_is_offered_for_a_change_and_asks_for_the_write);
    RUN(the_warning_holds_an_unsaved_change_and_discard_restores_it);
    RUN(a_locked_row_takes_no_press_and_says_why);
    RUN(a_discard_brings_supplys_limits_back_from_any_list);
    RUN(a_pulse_step_keeps_the_throttle_gap);
    RUN(a_closed_list_asks_for_no_repaint);
    return test_summary("setlist");
}
