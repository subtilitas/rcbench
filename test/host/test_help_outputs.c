/**
 * @file test_help_outputs.c
 * @brief OUTPUTS' screen guide: the board view's two pages on a protocol
 * and on a PROG PORT entry, the grid view's one on a board without a
 * drawing and on the rbmod carrier, none over an open list; every page
 * fits in both languages and themes, SAFE and ARMED.
 *
 * Cases:
 * - every_page_of_outputs_fits
 * - the_board_view_marks_the_column_and_a_key_each_side
 * - the_marked_keys_are_the_outermost_whole_in_each_half
 * - a_prog_port_entry_marks_its_rows
 * - the_grid_view_marks_the_column_and_the_first_cell
 * - an_open_list_leaves_no_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

#include "help_check.h"
#include "out_bind.h"
#include "outputs_screen_int.h"
#include "port_view.h"
#include "ui_board.h"
#include "ui_cats.h"
#include "ui_help.h"
#include "ui_layout.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The screen under test. */
static const ui_screen_t *view(void) { return outputs_view_screen(); }

/** @brief Nothing asked of the IOmcu: every ask taken. */
static bool on_ask(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                   uint16_t value)
{
    (void)seq;
    (void)port;
    (void)kind;
    (void)value;
    return true;
}

/**
 * @brief A visit on @p board with SERVO PWM on GPIO 0 to 2 selected, the
 *        IOmcu's four ports known, port 1 on GPIO 13.
 * @param board The board.
 * @param drawn The IOmcu serves the RP2350-CAN's drawing.
 */
static void visit(uint16_t board, bool drawn)
{
    ui_router_init();
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_board_set(board == OUTBIND_BOARD_RBMOD ? board
                                              : (uint16_t)OUTBIND_BOARD_UNKNOWN);
    ui_cats_force(0);
    view()->reset();
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    v.pin[0] = 13u;
    v.func[0] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    port_view_set(&v);
    port_view_set_ask(on_ask);
    const uint8_t held[4] = { 13u, 0xFFu, 0xFFu, 0xFFu };
    outputs_screen_set_prog_ports(4u, held);
    picker_screen_set_prog_ports(4u, held);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, board);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (uint8_t i = 0; i < 3u; ++i) {
        (void)outbind_toggle(&b, i);
    }
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(&b);
    if (drawn) {
        CHECK(serve_drawing());
    }
    view()->enter();
    outputs_screen_select(b.proto);
    view()->tick(0.016f);
}

/** @brief The board view on SERVO PWM. */
static void pose_board(void) { visit(BOARD, true); }

/** @brief The board view on PROG PORT 1. */
static void pose_board_port(void)
{
    visit(BOARD, true);
    outputs_screen_select_port(0u);
    view()->tick(0.016f);
}

/** @brief The grid view: the board with no drawing and no shape. */
static void pose_grid(void)
{
    uint16_t cat[LINK_CAT_COUNT] = { 0 };
    for (unsigned i = 0; i < 12u && i < LINK_CAT_COUNT; ++i) {
        cat[i] = LINK_CAT_OF(i, i + 1u, LINK_PIN_FREE);
    }
    outbind_forget_learned();
    CHECK(outbind_learn_board(4242u, cat));
    visit(4242u, false);
}

/** @brief The grid view on PROG PORT 1. */
static void pose_grid_port(void)
{
    pose_grid();
    outputs_screen_select_port(0u);
    view()->tick(0.016f);
}

/** @brief The rbmod carrier on SERVO PWM. */
static void pose_rbmod(void) { visit(OUTBIND_BOARD_RBMOD, false); }

/** @brief The rbmod carrier on PROG PORT 1. */
static void pose_rbmod_port(void)
{
    pose_rbmod();
    outputs_screen_select_port(0u);
    view()->tick(0.016f);
}

TEST_CASE(every_page_of_outputs_fits)
{
    CHECK_EQ(check_every_state(view(), pose_board, "board"), 2);
    CHECK_EQ(check_every_state(view(), pose_board_port, "board port"), 2);
    CHECK_EQ(check_every_state(view(), pose_grid, "grid"), 1);
    CHECK_EQ(check_every_state(view(), pose_grid_port, "grid port"), 1);
    CHECK_EQ(check_every_state(view(), pose_rbmod, "rbmod"), 1);
    CHECK_EQ(check_every_state(view(), pose_rbmod_port, "rbmod port"), 1);
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

TEST_CASE(the_board_view_marks_the_column_and_a_key_each_side)
{
    pose_board();
    REQUIRE(outputs_view_is_board());
    ui_help_page_t p;
    REQUIRE(ui_help_page(view(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_OUT_TITLE_PINS);
    REQUIRE(p.count == 4);
    CHECK_EQ(p.item[0].text, TX_HG_OUT_PROTO);
    CHECK_EQ(p.item[1].text, TX_HG_OUT_LAST);
    CHECK_EQ(p.item[2].text, TX_HG_OUT_PINS);
    CHECK_EQ(p.item[3].text, TX_HG_OUT_KEY);
    const gfx_rect_t left = p.item[3].rect;
    REQUIRE(ui_help_page(view(), false, 1, &p));
    CHECK_EQ(p.title, TX_HG_OUT_TITLE_BOARD);
    CHECK_EQ(p.item[0].text, TX_HG_OUT_INUSE);
    CHECK_EQ(p.item[p.count - 1].text, TX_HG_OUT_KEY);
    /* The two keys stand at the board's two ends. */
    CHECK(left.x + left.w <= ui_help_live_rect(UI_HELP_DOCK_RIGHT).w);
    CHECK(p.item[p.count - 1].rect.x
          >= ui_help_live_rect(UI_HELP_DOCK_LEFT).x);
    CHECK(p.item[p.count - 1].rect.x > left.x);
}

/**
 * @brief Whether key @p k of @p side's page is the outermost key whole in
 *        the live half: none left of it (dock right) or right of it (dock
 *        left), and none at its x lower down.
 * @param side The dock's side.
 * @param k    The marked key.
 * @return True when no other key beats it.
 */
static bool outermost(ui_help_side_t side, gfx_rect_t k)
{
    const gfx_rect_t live = ui_help_live_rect(side);
    int keys = 0;
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        gfx_rect_t r;
        if (!picker_screen_button_rect(i, &r) || r.x < live.x
            || r.x + r.w > live.x + live.w) {
            continue;
        }
        ++keys;
        const bool further = (side == UI_HELP_DOCK_RIGHT) ? r.x < k.x
                                                          : r.x > k.x;
        if (further || (r.x == k.x && r.y > k.y)) {
            return false;
        }
    }
    return keys > 1;
}

TEST_CASE(the_marked_keys_are_the_outermost_whole_in_each_half)
{
    pose_board();
    ui_help_page_t p;
    for (int n = 0; n < 2; ++n) {
        REQUIRE(ui_help_page(view(), false, n, &p));
        REQUIRE(p.item[p.count - 1].text == TX_HG_OUT_KEY);
        CHECK(outermost(p.side, p.item[p.count - 1].rect));
    }
    CHECK(!ui_help_page(view(), false, 2, &p));
    CHECK(!ui_help_page(view(), false, -1, &p));
    CHECK(!outputs_help_page(0, NULL));
    CHECK(!outputs_help_page(2, &p));
    CHECK(outputs_help_page(1, &p));
}

TEST_CASE(a_prog_port_entry_marks_its_rows)
{
    pose_board_port();
    ui_help_page_t p;
    REQUIRE(ui_help_page(view(), false, 0, &p));
    CHECK_EQ(p.title, TX_HG_OUT_TITLE_PORT);
    REQUIRE(p.count == 5);
    CHECK_STR_EQ(p.item[0].label, "PROG PORT 1");
    CHECK_EQ(p.item[1].text, TX_HG_OUT_PIN);
    CHECK_EQ(p.item[2].text, TX_HG_OUT_FUNCTION);
    CHECK_EQ(p.item[3].text, TX_HG_OUT_TESTER);
    CHECK_EQ(p.item[4].text, TX_HG_OUT_PWM);
    CHECK_EQ(p.item[2].rect.x, OB_FN_X);
    CHECK_EQ(p.item[2].rect.y, OB_FN_Y);
    REQUIRE(ui_help_page(view(), false, 1, &p));
    CHECK_EQ(p.item[p.count - 1].text, TX_HG_OUT_KEY_PORT);
    /* The grid view's entry: the PIN button, and on the carrier no
     * tester's rows where the tester cannot take its port. */
    pose_grid_port();
    REQUIRE(ui_help_page(view(), false, 0, &p));
    CHECK_EQ(p.item[1].text, TX_HG_OUT_PIN_GRID);
    CHECK_EQ(p.count, 5);
    outputs_port_rects_t r;
    pose_rbmod_port();
    REQUIRE(outputs_port_rects(&r));
    REQUIRE(ui_help_page(view(), false, 0, &p));
    CHECK_EQ(p.count, (r.tester.w > 0) ? 5 : 3);
    pose_rbmod();
    CHECK(!outputs_port_rects(&r));
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

TEST_CASE(the_grid_view_marks_the_column_and_the_first_cell)
{
    pose_grid();
    REQUIRE(!outputs_view_is_board());
    ui_help_page_t p;
    REQUIRE(ui_help_page(view(), false, 0, &p));
    REQUIRE(p.count == 3);
    CHECK_EQ(p.title, TX_HG_OUT_TITLE_PINS);
    CHECK_EQ(p.item[0].text, TX_HG_OUT_PROTO);
    CHECK_EQ(p.item[1].text, TX_HG_OUT_CELL);
    CHECK_EQ(p.item[2].text, TX_HG_OUT_LAST);
    const gfx_rect_t cell = outputs_grid_rect(OUTPUTS_GRID_CELL);
    CHECK_EQ(p.item[1].rect.x, cell.x);
    CHECK_EQ(p.item[1].rect.w, cell.w);
    /* LAST WRITE ends on the column's foot. */
    const gfx_rect_t last = outputs_grid_rect(OUTPUTS_GRID_LAST);
    CHECK_EQ(last.y + last.h, OU_FOOT);
    CHECK(!ui_help_page(view(), false, 1, &p));
    /* A board without pins has no cell to mark. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, (uint16_t)OUTBIND_BOARD_UNKNOWN);
    outputs_screen_set_binding(&b);
    CHECK_EQ(outputs_grid_rect(OUTPUTS_GRID_CELL).w, 0);
}

TEST_CASE(an_open_list_leaves_no_page)
{
    pose_board();
    CHECK_EQ(ui_help_count(view(), false), 2);
    const gfx_rect_t face = gfx_rect_make(8, 8, 280, 56);
    touch_event_t e = { TOUCH_EVENT_DOWN,
                        { 0, (int16_t)(face.x + 40), (int16_t)(face.y + 20),
                          40 }, 0u };
    view()->event(&e);
    e.type = TOUCH_EVENT_UP;
    view()->event(&e);
    REQUIRE(view()->covered());
    CHECK_EQ(ui_help_count(view(), false), 0);
    ui_help_page_t p;
    CHECK(!ui_help_page(view(), false, 0, &p));
}

int main(void)
{
    RUN(every_page_of_outputs_fits);
    RUN(the_board_view_marks_the_column_and_a_key_each_side);
    RUN(the_marked_keys_are_the_outermost_whole_in_each_half);
    RUN(a_prog_port_entry_marks_its_rows);
    RUN(the_grid_view_marks_the_column_and_the_first_cell);
    RUN(an_open_list_leaves_no_page);
    return test_summary("help_outputs");
}
