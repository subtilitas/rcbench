/**
 * @file test_help_setup.c
 * @brief SETUP's screen guide: two pages on the tiles, on a list and on
 * SENSORS, none over the keypad; every page fits in both languages and
 * themes, SAFE and ARMED, and marks each tile once.
 *
 * Cases:
 * - every_page_of_setup_fits
 * - the_tiles_pages_mark_every_tile_once
 * - a_list_marks_the_keys_of_a_row_whose_value_opens_the_keypad
 * - the_keypad_leaves_no_page
 * - the_list_draws_nothing_under_the_dock
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#include "help_check.h"
#include "out_bind.h"
#include "sensors_pane.h"
#include "ui_board.h"
#include "ui_help.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The board the next pose takes: a module, or the rbmod carrier. */
static uint16_t s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;

/** @brief SETUP's tiles on top. */
static void pose_hub(void)
{
    fresh();
    ui_board_set(s_board);
    to_overview();
    ui_router_goto(SCREEN_SETUP);
}

/**
 * @brief A tap on the tile of category @p cat.
 * @param cat The category.
 */
static void open_cat(setting_cat_t cat)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    (void)settings_hub_slot(settings_hub_cat_tile(cat), &r);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/** @brief PANEL's list on top. */
static void pose_panel(void)
{
    pose_hub();
    open_cat(SET_CAT_APP);
}

/** @brief SD CARD's list on top. */
static void pose_card(void)
{
    pose_hub();
    open_cat(SET_CAT_CARD);
}

/** @brief SENSORS on top, its first entry selected. */
static void pose_sensors(void)
{
    pose_hub();
    open_cat(SET_CAT_SENSORS);
}

TEST_CASE(every_page_of_setup_fits)
{
    for (int b = 0; b < 2; ++b) {
        s_board = b ? (uint16_t)OUTBIND_BOARD_RBMOD
                    : (uint16_t)OUTBIND_BOARD_UNKNOWN;
        CHECK_EQ(check_every_state(settings_screen(), pose_hub, "hub"), 2);
        CHECK_EQ(check_every_state(settings_screen(), pose_panel, "panel"),
                 2);
        CHECK_EQ(check_every_state(settings_screen(), pose_card, "card"), 2);
        CHECK_EQ(check_every_state(settings_screen(), pose_sensors,
                                   "sensors"), 2);
    }
    s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    ui_board_set(s_board);
}

TEST_CASE(the_tiles_pages_mark_every_tile_once)
{
    for (int b = 0; b < 2; ++b) {
        s_board = b ? (uint16_t)OUTBIND_BOARD_RBMOD
                    : (uint16_t)OUTBIND_BOARD_UNKNOWN;
        pose_hub();
        int marked[SETTINGS_HUB_SLOTS] = { 0 };
        int saves = 0;
        for (int n = 0; n < 2; ++n) {
            ui_help_page_t p;
            REQUIRE(ui_help_page(settings_screen(), false, n, &p));
            for (int i = 0; i < p.count; ++i) {
                gfx_rect_t r = p.item[i].rect;
                saves += r.y > 400;
                for (int k = 0; k < settings_hub_tiles(); ++k) {
                    gfx_rect_t t;
                    REQUIRE(settings_hub_slot(k, &t));
                    marked[k] += r.y == t.y && r.x >= t.x
                                 && r.x < t.x + t.w;
                }
            }
        }
        for (int k = 0; k < settings_hub_tiles(); ++k) {
            if (marked[k] != 1) {
                T_FAIL("board %d: tile %d marked %d times", b, k, marked[k]);
            }
        }
        CHECK_EQ(saves, 1);
    }
    s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    ui_board_set(s_board);
}

TEST_CASE(a_list_marks_the_keys_of_a_row_whose_value_opens_the_keypad)
{
    s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    pose_panel();
    ui_help_page_t p;
    REQUIRE(ui_help_page(settings_screen(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_SET_TITLE_VALUES);
    /* Theme, the first row, is a choice; Brightness, the second, opens the
     * keypad: "-", its value, "+" and SAVE. */
    REQUIRE(p.count == 4);
    CHECK_STR_EQ(p.item[0].label, "-");
    CHECK_EQ(p.item[1].text, TX_HG_SET_VALUE);
    CHECK_STR_EQ(p.item[2].label, "+");
    CHECK_EQ(p.item[3].text, TX_HG_SET_SAVE);
    const gfx_rect_t minus = settings_screen_row_part(1, SETTINGS_ROW_MINUS);
    CHECK_EQ(p.item[0].rect.x, minus.x);
    CHECK_EQ(p.item[0].rect.y, minus.y);
    /* SD CARD has no row with a keypad: its first row with keys, a switch,
     * with "<" and ">". */
    pose_card();
    REQUIRE(ui_help_page(settings_screen(), false, 1, &p));
    REQUIRE(p.count == 3);
    CHECK_STR_EQ(p.item[0].label, "<");
    CHECK_STR_EQ(p.item[1].label, ">");
    /* The first page marks the row that leads, whole in the pane. */
    REQUIRE(ui_help_page(settings_screen(), false, 0, &p));
    CHECK_EQ(p.item[0].text, TX_HG_SET_ROW);
    CHECK(p.item[0].rect.y >= settings_screen_list_rect().y);
    /* SENSORS adds its selected entry, named as its card names it. */
    pose_sensors();
    REQUIRE(ui_help_page(settings_screen(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_SET_TITLE_SENSORS);
    char name[40];
    CHECK_STR_EQ(p.item[0].label,
                 sensors_pane_name(sensors_pane_selected(), name,
                                   sizeof(name)));
}

TEST_CASE(the_keypad_leaves_no_page)
{
    s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    pose_panel();
    CHECK_EQ(ui_help_count(settings_screen(), false), 2);
    const gfx_rect_t v = settings_screen_row_part(1, SETTINGS_ROW_VALUE);
    tap(v.x + v.w / 2, UI_BAND_H + v.y + v.h / 2);
    REQUIRE(settings_screen_keypad()->open);
    CHECK_EQ(ui_help_count(settings_screen(), false), 0);
    CHECK(!ui_router_guide_shown());
    ui_help_page_t p;
    CHECK(!ui_help_page(settings_screen(), false, 0, &p));
}

/* The dock's half holds the dock and nothing of the list: the list clips
 * its rows inside the router's clip, not in place of it. */
TEST_CASE(the_list_draws_nothing_under_the_dock)
{
    static gfx_color_t alone[(size_t)W * H];
    s_board = (uint16_t)OUTBIND_BOARD_UNKNOWN;
    for (int page = 0; page < 2; ++page) {
        pose_panel();
        const gfx_rect_t g = ui_band_guide_rect();
        tap(g.x + g.w / 2, g.y + g.h / 2);
        REQUIRE(ui_router_guide_open());
        ui_help_page_t p;
        if (page == 1) {
            REQUIRE(ui_help_page(settings_screen(), false, 0, &p));
            const gfx_rect_t b = ui_help_button_rect(p.side, UI_HELP_HIT_NEXT);
            tap(b.x + 10, b.y + 10);
        }
        REQUIRE(ui_help_page(settings_screen(), false, page, &p));
        for (int f = 0; f < 4; ++f) {
            /* From frame 2 on the list repaints on its own, as a save
             * taken by the store makes it. */
            if (f >= 2) {
                settings_screen_invalidate();
            }
            ui_router_tick(0.05f);
            ui_router_render(&cv, f & 1);
        }
        gfx_canvas_t c2;
        gfx_canvas_init(&c2, alone, W, H, W);
        gfx_clear(&c2, 0);
        ui_help_render_dock(&c2, &p, page, 2, false, UI_HELP_HIT_NONE);
        const gfx_rect_t d = ui_help_dock_rect(p.side);
        int diff = 0;
        for (int y = UI_BAND_H; y < UI_ALERT_Y; ++y) {
            for (int x = d.x - 8; x < d.x + d.w + 8; ++x) {
                diff += fb[(size_t)y * W + x] != alone[(size_t)y * W + x];
            }
        }
        if (diff != 0) {
            T_FAIL("page %d: %d pixels of the dock's half differ", page + 1,
                   diff);
        }
    }
}

int main(void)
{
    RUN(every_page_of_setup_fits);
    RUN(the_tiles_pages_mark_every_tile_once);
    RUN(a_list_marks_the_keys_of_a_row_whose_value_opens_the_keypad);
    RUN(the_keypad_leaves_no_page);
    RUN(the_list_draws_nothing_under_the_dock);
    return test_summary("help_setup");
}
