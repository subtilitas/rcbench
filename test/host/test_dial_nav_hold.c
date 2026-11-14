/**
 * @file test_dial_nav_hold.c
 * @brief The M5Stack Dial on OUTPUTS' board view (ui_focus.h): the focus
 *        walks the list's face, CLEAR ALL and the pins, a SELECT opens and
 *        picks in the protocol list, and a SELECT on CLEAR ALL, a 2s hold,
 *        completes nothing however long the bench runs after it.
 *
 * Cases:
 * - the_board_view_moves_wraps_and_picks
 * - a_select_on_a_hold_button_completes_nothing
 * - a_press_lands_on_the_focused_item
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

#include "outputs_screen_int.h"
#include "ui_board.h"
#include "ui_focus.h"

/** @brief A tick of @p seconds in steps of 0.25s, as frames give them. */
static void run_for(float seconds)
{
    for (float t = 0.0f; t < seconds; t += 0.25f) {
        ui_router_tick(0.25f);
    }
}

/** @brief OUTPUTS on the RP2350-CAN's drawing, SERVO PWM picked, pages no
 *         binding describes read: CLEAR ALL offered; the Dial linked. */
static void visit(void)
{
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
    ui_router_init();
    s_applied = 0;
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(&b);
    CHECK(serve_drawing());
    ui_router_goto(SCREEN_OUTPUTS);
    outputs_screen_select(b.proto);
    ui_router_tick(0.016f);
    CHECK(outputs_view_is_board());
    static bind_reading_t r;
    outbind_init(&r.bind);
    outbind_set_board(&r.bind, BOARD);
    r.state = BIND_READ_ODD;
    outputs_screen_set_reading(&r);
    ui_router_tick(0.016f);
    CHECK(outputs_unbind_offered());
    ui_focus_set_link(true, false);
}

/**
 * @brief Turn until the focused item's label is @p label.
 * @param label The label.
 * @return true when found within one turn round the list.
 */
static bool focus_on(const char *label)
{
    const int n = ui_focus_count();
    for (int i = 0; i <= n; ++i) {
        if (strcmp(ui_focus_label(ui_focus_index()), label) == 0) {
            return true;
        }
        (void)ui_focus_turn(1);
    }
    return false;
}

TEST_CASE(the_board_view_moves_wraps_and_picks)
{
    visit();
    CHECK(ui_focus_shown());
    const int n = ui_focus_count();
    /* The face, CLEAR ALL and a button a pin of the board. */
    CHECK(n > 10);
    CHECK_EQ(ui_focus_index(), 0);
    CHECK_STR_EQ(ui_focus_label(0), "SERVO PWM");
    CHECK_STR_EQ(ui_focus_label(1), TR(HG_OUT_UNBIND_NAME));
    CHECK(ui_focus_turn(-1));
    CHECK_EQ(ui_focus_index(), n - 1);
    CHECK(ui_focus_turn(1));
    CHECK_EQ(ui_focus_index(), 0);
    /* SELECT on the face opens the list, on the protocol picked. */
    CHECK(ui_focus_press());
    CHECK(picker_screen_list_open());
    CHECK_EQ(ui_focus_index(), (int)proto_named("SERVO PWM"));
    CHECK(focus_on("DSHOT300"));
    CHECK(ui_focus_press());
    CHECK(!picker_screen_list_open());
    CHECK_STR_EQ(ui_focus_label(ui_focus_index()), "DSHOT300");
    /* Again, and HOLD closes the list without a pick. */
    CHECK(ui_focus_press());
    CHECK(picker_screen_list_open());
    CHECK(ui_focus_hold());
    CHECK(!picker_screen_list_open());
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_STR_EQ(ui_focus_label(ui_focus_index()), "DSHOT300");
}

TEST_CASE(a_select_on_a_hold_button_completes_nothing)
{
    visit();
    CHECK(focus_on(TR(HG_OUT_UNBIND_NAME)));
    CHECK(ui_focus_press());
    run_for(3.0f);
    CHECK_EQ(s_applied, 0);
    /* Ten presses with the bench running between them: still nothing. */
    for (int i = 0; i < 10; ++i) {
        CHECK(ui_focus_press());
        run_for(1.0f);
    }
    CHECK_EQ(s_applied, 0);
    /* The same key held by a finger for 2.25s clears, so the rig can
     * complete the hold the Dial cannot. */
    const gfx_rect_t k = outputs_unbind_rect();
    touch_event_t e = { TOUCH_EVENT_DOWN,
                        { 1, (int16_t)(k.x + k.w / 2),
                          (int16_t)(UI_BAND_H + k.y + k.h / 2), 40 }, 0u };
    ui_router_event(&e);
    run_for(2.25f);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK_EQ(s_applied, 1);
}

/** @brief Every item of the list on top: its tap point on it, in sight. */
static void every_target_lands(void)
{
    const int n = ui_focus_count();
    CHECK(n > 0);
    for (int i = 0; i < n; ++i) {
        ui_focus_target_t t;
        CHECK(ui_focus_target(&t));
        CHECK(gfx_rect_contains(t.rect, t.x, t.y));
        CHECK(gfx_rect_contains(t.seen, t.x, t.y));
        CHECK(ui_focus_turn(1));
    }
}

TEST_CASE(a_press_lands_on_the_focused_item)
{
    visit();
    every_target_lands();
    CHECK_EQ(ui_focus_index(), 0);
    CHECK(ui_focus_press());
    CHECK(picker_screen_list_open());
    every_target_lands();
}

int main(void)
{
    RUN(the_board_view_moves_wraps_and_picks);
    RUN(a_select_on_a_hold_button_completes_nothing);
    RUN(a_press_lands_on_the_focused_item);
    return test_summary("dial_nav_hold");
}
