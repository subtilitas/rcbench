/*
 * SETUP on a board that solders its pins, the rbmod carrier
 * (shared/settings settings_fix(), shared/ui ui_board.c).
 *
 * Under test: a fixed setting reads its fixed value and keeps its stored
 * one, which comes back when it is let go; the interface pins follow the
 * board the IOmcu reports, once per change of board, and each one that
 * moves is announced; on the carrier the Sensor SDA row has no keys, opens
 * no keypad and says it is soldered; the PD mini's one-pin rule reads the
 * pins in force; the carrier adds a LAYOUT tile, and OUTPUTS' tile opens
 * the one screen on either.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "ui_board.h"
#include "ui_screen.h"
#include "ui_setting_keypad.h"

#define RBMOD  ((uint16_t)OUTBIND_BOARD_RBMOD)
#define MODULE ((uint16_t)OUTBIND_BOARD_PICO_HEADER)

/* Each setting the apply announced, counted. */
static int s_moved[SETTING_COUNT];

static void moved(setting_id_t id)
{
    if (id >= 0 && id < SETTING_COUNT) {
        ++s_moved[id];
    }
}

/* The model at its defaults, on the module, every pin let go. */
static void fresh_board(void)
{
    fresh_model();
    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
    memset(s_moved, 0, sizeof(s_moved));
}

TEST_CASE(a_fixed_setting_reads_its_pin_and_keeps_its_own)
{
    fresh_board();
    settings_set(SET_TONE_PIN, 14.0f);
    CHECK_EQ(settings_get_int(SET_TONE_PIN), 14);
    CHECK(!settings_fixed(SET_TONE_PIN));
    const bool differs = settings_differs(SET_TONE_PIN);

    CHECK(settings_fix(SET_TONE_PIN, 22));
    CHECK(settings_fixed(SET_TONE_PIN));
    CHECK_EQ(settings_get_int(SET_TONE_PIN), 22);
    /* What is unsaved is the stored value's business, fixed or not. */
    CHECK(settings_differs(SET_TONE_PIN) == differs);
    /* Fixed again at the same pin: nothing moves. */
    CHECK(!settings_fix(SET_TONE_PIN, 22));
    /* A value set under it is the stored one's, and does not show. */
    settings_set(SET_TONE_PIN, 13.0f);
    CHECK_EQ(settings_get_int(SET_TONE_PIN), 22);
    settings_set(SET_TONE_PIN, 14.0f);

    CHECK(settings_unfix(SET_TONE_PIN));
    CHECK(!settings_fixed(SET_TONE_PIN));
    CHECK_EQ(settings_get_int(SET_TONE_PIN), 14);
    CHECK(!settings_unfix(SET_TONE_PIN));
    /* A fix at the value it holds is held and moves nothing. */
    CHECK(!settings_fix(SET_TONE_PIN, 14));
    CHECK(settings_fixed(SET_TONE_PIN));
    CHECK(settings_unfix(SET_TONE_PIN) == false);

    CHECK(!settings_fix((setting_id_t)-1, 3));
    CHECK(!settings_fix(SETTING_COUNT, 3));
    CHECK(!settings_unfix(SETTING_COUNT));
    CHECK(!settings_fixed(SETTING_COUNT));
}

TEST_CASE(the_interface_pins_follow_the_board_once_per_change)
{
    fresh_board();
    settings_set(SET_PDMINI_TX, 7.0f);    /* already the carrier's TX */
    const int sda = settings_get_int(SET_SENSE_SDA);

    ui_board_set(RBMOD);
    CHECK(ui_board_apply_pins(moved));
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(settings_get_int(SET_TONE_PIN), 22);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 7);
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), 6);
    /* Announced where the value moved: SDA 16 and SCL 17 and the tap's 22
     * are the defaults, TX was 7; RX moved from -1. */
    CHECK_EQ(s_moved[SET_SENSE_SDA], (sda == 16) ? 0 : 1);
    CHECK_EQ(s_moved[SET_PDMINI_TX], 0);
    CHECK_EQ(s_moved[SET_PDMINI_RX], 1);
    CHECK(settings_fixed(SET_PDMINI_RX));

    /* The same board again: nothing to do. */
    CHECK(!ui_board_apply_pins(moved));
    CHECK_EQ(s_moved[SET_PDMINI_RX], 1);

    /* Back on the module: every pin its own again, announced. */
    ui_board_set(MODULE);
    CHECK(ui_board_apply_pins(moved));
    CHECK_EQ(settings_get_int(SET_PDMINI_RX), -1);
    CHECK_EQ(s_moved[SET_PDMINI_RX], 2);
    CHECK(!settings_fixed(SET_SENSE_SDA));
    /* And no board at all lets them go as well. */
    ui_board_set(RBMOD);
    (void)ui_board_apply_pins(NULL);
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    CHECK(ui_board_apply_pins(NULL));
    CHECK(!settings_fixed(SET_PDMINI_TX));
    CHECK(ui_board_roles() == NULL);
    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
}

TEST_CASE(the_pd_minis_one_pin_rule_reads_the_pins_in_force)
{
    fresh_board();
    settings_set(SET_PDMINI_TX, 9.0f);
    settings_set(SET_PDMINI_RX, 10.0f);
    settings_set(SET_PDMINI_TX, 10.0f);
    CHECK(settings_pdmini_same_pin() == (settings_get_int(SET_PDMINI_TX)
                                         == 10));
    ui_board_set(RBMOD);
    (void)ui_board_apply_pins(NULL);
    CHECK(!settings_pdmini_same_pin());
    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
}

/* The list row of @p id in its open list. */
static int list_row(setting_id_t id)
{
    const setting_cat_t cat = settings_def(id)->cat;
    setting_id_t ids[64];
    const int n = settings_in_category(cat, ids, 64);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            return settings_screen_row_of(cat, i);
        }
    }
    return -1;
}

TEST_CASE(a_soldered_pin_row_has_no_keys_and_says_so)
{
    fresh_hub();
    ui_board_set(RBMOD);
    (void)ui_board_apply_pins(NULL);
    open_sensor(SET_SENSE_SDA);
    const int row = list_row(SET_SENSE_SDA);
    CHECK(row >= 0);
    const int before = (int)lrintf(settings_get(SET_SENSE_SDA));
    const gfx_rect_t pl = settings_screen_row_part(row, SETTINGS_ROW_PLUS);
    tap(pl.x + pl.w / 2, UI_BAND_H + pl.y + pl.h / 2);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), before);
    CHECK(!settings_differs(SET_SENSE_SDA));
    CHECK(!ui_setting_keypad_offered(SET_SENSE_SDA));
    CHECK(!ui_setting_keypad_offered(SET_PDMINI_TX));
    CHECK_STR_EQ(settings_screen_help(SET_TONE_PIN),
                 "Soldered on this board. Not a setting here.");
    ui_router_render(&s_c, 0);

    /* On the module the row steps and the keypad opens. */
    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
    CHECK(ui_setting_keypad_offered(SET_SENSE_SDA));
    CHECK(strcmp(settings_screen_help(SET_TONE_PIN),
                 "Soldered on this board. Not a setting here.") != 0);
    tap(pl.x + pl.w / 2, UI_BAND_H + pl.y + pl.h / 2);
    CHECK(settings_get_int(SET_SENSE_SDA) != before);
}

TEST_CASE(the_carrier_adds_a_layout_tile_and_keeps_outputs)
{
    fresh_hub();
    ui_board_set(RBMOD);
    const int t = settings_hub_tiles() - 1;
    CHECK_EQ(settings_hub_tile_screen(t), SCREEN_LAYOUT);
    CHECK_EQ(settings_hub_tile_screen(OUTPUTS_TILE), SCREEN_OUTPUTS);
    CHECK_EQ(settings_hub_tile_screen(0), SCREEN_SETUP);
    CHECK_EQ(settings_hub_tile_screen(-1), SCREEN_COUNT);
    CHECK_EQ(settings_hub_tile_screen(settings_hub_tiles()), SCREEN_COUNT);
    CHECK(settings_screen_opens(SCREEN_LAYOUT));
    ui_router_render(&s_c, 0);
    tap(tile_x(t), tile_y(t));
    CHECK_EQ(ui_router_current(), SCREEN_LAYOUT);
    CHECK_EQ(ui_router_parent(SCREEN_LAYOUT), SCREEN_SETUP);
    ui_router_goto(SCREEN_SETUP);
    tap(tile_x(OUTPUTS_TILE), tile_y(OUTPUTS_TILE));
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    ui_router_goto(SCREEN_SETUP);

    /* The module: one tile fewer, LAYOUT's slot opening nothing. */
    ui_board_set(MODULE);
    CHECK_EQ(settings_hub_tiles(), t);
    CHECK_EQ(settings_hub_tile_screen(t), SCREEN_COUNT);
    tap(tile_x(t), tile_y(t));
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    tap(tile_x(OUTPUTS_TILE), tile_y(OUTPUTS_TILE));
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
}

int main(void)
{
    RUN(a_fixed_setting_reads_its_pin_and_keeps_its_own);
    RUN(the_interface_pins_follow_the_board_once_per_change);
    RUN(the_pd_minis_one_pin_rule_reads_the_pins_in_force);
    RUN(a_soldered_pin_row_has_no_keys_and_says_so);
    RUN(the_carrier_adds_a_layout_tile_and_keeps_outputs);
    return test_summary("settings_carrier");
}
