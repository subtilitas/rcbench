/**
 * @file test_servo_hub.c
 * @brief The SERVO hub through the router: its eight tiles in SETUP's grid,
 * their names and lines in both languages, each tile's function screen, a
 * grey tile's reason, the footer with no ARM and the profile's list, the
 * hub as a SAFE bench, and a programmer's home tag back to the hub.
 *
 * Cases:
 * - the_tiles_take_setups_grid_in_the_order_of_use
 * - every_tile_name_and_line_fits_31_cells_in_both_languages
 * - each_tile_opens_its_function_screen_under_its_title
 * - a_grey_tile_names_its_reason_and_opens_nothing
 * - the_hub_footer_holds_no_arm_and_settings_opens_the_profile
 * - the_hub_keeps_the_bench_safe
 * - a_programmers_home_tag_goes_back_to_the_hub
 * - hitecs_series_row_reaches_the_programmer
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"
#include "hitec_panel.h"
#include "hitec_prog.h"
#include "ui_footer.h"
#include "ui_setlist.h"

/** @brief Tile @p i's middle, in panel coordinates. */
static void tap_tile(int i)
{
    tap(SV_HUB_TILE_X(i), UI_BAND_H + SV_HUB_TILE_Y(i));
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief The hub on top, an IOmcu that reports SERVO PWM, nothing waiting. */
static void on_hub(void)
{
    on_screen(SCREEN_SERVO);
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = LINK_CAP_SERVO_PWM;
    ui_router_set_status(&st);
    ui_router_set_alert(NULL);
    drain_servo();
}

/* Eight tiles of 328 x 130px, SETUP's slots, LIVE first: x 10, 348 and
 * 686, y 10, 150 and 290 in the body; the ninth slot is no tile, and the
 * third row ends above the footer at y 444. */
TEST_CASE(the_tiles_take_setups_grid_in_the_order_of_use)
{
    on_hub();
    CHECK_EQ(SERVO_HUB_TILES, 8);
    for (int i = 0; i < SERVO_HUB_TILES; ++i) {
        const gfx_rect_t r = servo_hub_tile_rect(i);
        gfx_rect_t slot;
        CHECK(settings_hub_slot(i, &slot));
        CHECK_EQ(r.x, 10 + 338 * (i % 3));
        CHECK_EQ(r.y, 10 + 140 * (i / 3));
        CHECK_EQ(r.w, 328);
        CHECK_EQ(r.h, 130);
        CHECK_EQ(r.x, slot.x);
        CHECK_EQ(r.y, slot.y);
        CHECK_EQ(r.w, slot.w);
        CHECK_EQ(r.h, slot.h);
        CHECK(r.y + r.h <= UI_FOOTER_Y);
        CHECK(gfx_rect_contains(r, SV_HUB_TILE_X(i), SV_HUB_TILE_Y(i)));
    }
    CHECK_EQ(servo_hub_tile_rect(SERVO_HUB_TILES).w, 0);
    CHECK_EQ(servo_hub_tile_rect(-1).w, 0);
}

/* Each tile's name and line, as the hub draws them, take at most 31 cells
 * of 10 x 20 in English and German: the 328px tile less its margins. */
TEST_CASE(every_tile_name_and_line_fits_31_cells_in_both_languages)
{
    static const ui_text_id_t k_name[SERVO_HUB_TILES] = {
        TX_SVH_LIVE,   TX_SVH_SWEEP,  TX_SVH_TEST,  TX_SVH_LIMRUN,
        TX_SVH_RUDDER, TX_SVH_RAUTO, TX_SVH_KST,   TX_SVH_HITEC,
    };
    static const ui_text_id_t k_line[SERVO_HUB_TILES] = {
        TX_SVH_LIVE_LINE,   TX_SVH_SWEEP_LINE,  TX_SVH_TEST_LINE,
        TX_SVH_LIMRUN_LINE, TX_SVH_RUDDER_LINE, TX_SVH_RAUTO_LINE,
        TX_SVH_KST_LINE,    TX_SVH_HITEC_LINE,
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < SERVO_HUB_TILES; ++i) {
            const char *name = ui_tr(k_name[i]);
            const char *line = ui_tr(k_line[i]);
            CHECK(name[0] != '\0');
            CHECK(line[0] != '\0');
            CHECK(gfx_text_cells(name) <= 31);
            CHECK(gfx_text_cells(line) <= 31);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A tap on each tile opens its function's screen through the router, its
 * title the function's name, KST's with SERVO before it; KST opens its
 * session at once, and the hub closes it. */
TEST_CASE(each_tile_opens_its_function_screen_under_its_title)
{
    static const char *const k_title[SERVO_HUB_TILES] = {
        "LIVE", "SWEEP", "TEST", "LIMIT RUN", "RUDDER", "RUDDER AUTO",
        "SERVO > KST", "HITEC",
    };
    on_hub();
    for (int i = 0; i < SERVO_HUB_TILES; ++i) {
        const servo_fn_t fn = (servo_fn_t)(i + 1);
        ui_router_goto(SCREEN_SERVO);
        CHECK_EQ(servo_screen_fn(), SERVO_FN_HUB);
        tap_tile(i);
        CHECK_EQ(ui_router_current(), servo_fn_screen_id(fn));
        CHECK_EQ(servo_screen_fn(), fn);
        CHECK(servo_screen_owns(ui_router_current()));
        CHECK_EQ(servo_fn_of_screen(ui_router_current()), fn);
        CHECK_STR_EQ(ui_router_title(ui_router_current()), k_title[i]);
        if (fn == SERVO_FN_KST) {
            CHECK(kst_panel_is_open());
        }
        ui_router_goto(SCREEN_SERVO);
        CHECK_EQ(servo_screen_fn(), SERVO_FN_HUB);
        CHECK(!kst_panel_is_open());
    }
}

/* With SERVO PWM not reported KST's tile is grey: a tap names the reason
 * the hub gives for it in the alert band and stays on the hub. */
TEST_CASE(a_grey_tile_names_its_reason_and_opens_nothing)
{
    on_hub();
    char why[64];
    CHECK(!servo_hub_tile_refused(6, why, sizeof(why)));
    CHECK_STR_EQ(why, "");
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = 0u;
    ui_router_set_status(&st);
    CHECK(servo_hub_tile_refused(6, why, sizeof(why)));
    CHECK(why[0] != '\0');
    /* The other functions need no capability. */
    for (int i = 0; i < 6; ++i) {
        CHECK(!servo_hub_tile_refused(i, NULL, 0u));
    }
    tap_tile(6);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK(!kst_panel_is_open());
    CHECK(alert()[0] != '\0');
    CHECK_STR_EQ(alert(), why);
}

/* The hub's footer: SERVO with the port line, an empty middle slot, and
 * SETTINGS.  A tap where ARM sits on a function screen posts nothing;
 * SETTINGS opens the profile's list: OUTPUT, its 9 rows, DUT and its 2. */
TEST_CASE(the_hub_footer_holds_no_arm_and_settings_opens_the_profile)
{
    on_hub();
    char line[48];
    (void)servo_screen_port_line(line, sizeof(line));
    CHECK(line[0] != '\0');
    const gfx_rect_t hold = ui_footer_rect(UI_FOOTER_HOLD);
    touch(hold.x + hold.w / 2, UI_BAND_H + hold.y + hold.h / 2,
          TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        ui_router_tick(0.026f);                     /* 2.6s held */
    }
    touch(hold.x + hold.w / 2, UI_BAND_H + hold.y + hold.h / 2,
          TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);

    tap(SV_FOOT_SET_X, UI_BAND_H + SV_FOOT_Y);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    CHECK_EQ(ui_setlist_count(sl), 13);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
}

/* The hub drives nothing: an armed function screen left for the hub
 * posts DISARM, as every leave does. */
TEST_CASE(the_hub_keeps_the_bench_safe)
{
    on_hub();
    tap_tile(0);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIVE);
    drain_servo();                      /* the hub's leave */
    const gfx_rect_t hold = ui_footer_rect(UI_FOOTER_HOLD);
    touch(hold.x + hold.w / 2, UI_BAND_H + hold.y + hold.h / 2,
          TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        ui_router_tick(0.026f);
    }
    touch(hold.x + hold.w / 2, UI_BAND_H + hold.y + hold.h / 2,
          TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_took().kind, SERVO_CMD_ARM);
    servo_screen_set_armed(true);
    drain_servo();
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(servo_took().kind, SERVO_CMD_DISARM);
    servo_screen_set_armed(false);
    CHECK_EQ(servo_screen_fn(), SERVO_FN_HUB);
}

/* KST's session has no CLOSE: one tap on the home tag ends the session and
 * the router takes the screen to the hub, not to the menu. */
TEST_CASE(a_programmers_home_tag_goes_back_to_the_hub)
{
    on_hub();
    tap_tile(6);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_KST);
    CHECK(kst_panel_is_open());
    const gfx_rect_t r = ui_home_tag_rect(ui_router_title(SCREEN_SERVO_KST));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(!kst_panel_is_open());
    ui_router_tick(0.026f);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_fn(), SERVO_FN_HUB);
}

/* SERIES stepped in HITEC's SETTINGS list reaches the programmer at once:
 * the next CONNECT uses CLASSIC, not the D series the screen opened
 * with, and RESET puts D back. */
TEST_CASE(hitecs_series_row_reaches_the_programmer)
{
    on_hub();
    tap_tile(7);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_HITEC);
    CHECK_EQ(hitec_prog_series(hitec_panel_model()), (uint8_t)HITEC_D);
    tap(SV_FOOT_SET_X, UI_BAND_H + SV_FOOT_Y);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    const gfx_rect_t minus = ui_setlist_row_part(sl, 0, UI_SL_PART_MINUS);
    tap(minus.x + minus.w / 2, UI_BAND_H + minus.y + minus.h / 2);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_CLASSIC);
    CHECK_EQ(hitec_prog_series(hitec_panel_model()), (uint8_t)HITEC_CLASSIC);
    const gfx_rect_t reset = ui_setlist_strip_rect(1);
    tap(reset.x + reset.w / 2, UI_BAND_H + reset.y + reset.h / 2);
    CHECK_EQ(settings_get_int(SET_HITEC_SERIES), (int)HITEC_D);
    CHECK_EQ(hitec_prog_series(hitec_panel_model()), (uint8_t)HITEC_D);
    settings_cancel_save();
}

int main(void)
{
    RUN(the_tiles_take_setups_grid_in_the_order_of_use);
    RUN(every_tile_name_and_line_fits_31_cells_in_both_languages);
    RUN(each_tile_opens_its_function_screen_under_its_title);
    RUN(a_grey_tile_names_its_reason_and_opens_nothing);
    RUN(the_hub_footer_holds_no_arm_and_settings_opens_the_profile);
    RUN(the_hub_keeps_the_bench_safe);
    RUN(a_programmers_home_tag_goes_back_to_the_hub);
    RUN(hitecs_series_row_reaches_the_programmer);
    return test_summary("servo_hub");
}
