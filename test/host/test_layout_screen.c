/**
 * @file test_layout_screen.c
 * @brief LAYOUT on the rbmod carrier and on a board without
 *        soldered pins.
 *
 * Cases:
 * - j1_and_j2_follow_the_connectors_position_order
 * - a_servo_output_shows_the_protocol_bound_on_it
 * - an_output_reads_not_read_until_the_binding_is_read
 * - a_board_without_soldered_pins_has_no_positions
 * - the_picture_repaints_when_the_binding_moves
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

#include "layout_screen.h"
#include "ui_board.h"

#define RBMOD ((uint16_t)OUTBIND_BOARD_RBMOD)

/* OUTPUTS holding the carrier's binding with nothing bound, read. */
static void fresh_carrier(void)
{
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outputs_screen_set_binding(&b);
    ui_board_set(RBMOD);
    layout_screen()->reset();
}

/* The screen drawn into framebuffer @p fb of a canvas with guards: every
 * pixel painted, none outside it.  Returns whether it painted at all. */
static bool draw_guarded(int fb)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, UI_BODY_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, SCREEN_W, UI_BODY_H);
    layout_screen()->render(&c, fb);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    return guard_canvas_unpainted(&g) == 0u;
}

TEST_CASE(j1_and_j2_follow_the_connectors_position_order)
{
    fresh_carrier();
    static const char *const j1[4] = { "PROGP04", "PROGP01", "PROGP02",
                                       "PROGP03" };
    static const uint8_t j1_gpio[4] = { 0u, 13u, 14u, 15u };
    static const char *const j2[4] = { "PWM03", "PWM02", "PWM01", "PWM04" };
    static const uint8_t j2_gpio[4] = { 5u, 4u, 2u, 1u };
    static const char *const j2_rail[4] = {
        "VCC_SERVOPORT_B", "VCC_SERVOPORT_B", "VCC_SERVOPORT_A",
        "VCC_SERVOPORT_A" };
    layout_pos_t p;
    for (int n = 0; n < 4; ++n) {
        CHECK(layout_screen_pos(0, n, &p));
        CHECK_STR_EQ(p.role, j1[n]);
        CHECK_EQ(p.gpio, j1_gpio[n]);
        CHECK_STR_EQ(p.rail, "VCC_PROGPORT");
        CHECK_STR_EQ(p.state, "PROGRAMMING");
        CHECK(layout_screen_pos(1, n, &p));
        CHECK_STR_EQ(p.role, j2[n]);
        CHECK_EQ(p.gpio, j2_gpio[n]);
        CHECK_STR_EQ(p.rail, j2_rail[n]);
        CHECK_STR_EQ(p.state, "OFF");
    }
    /* Past the last position and the last connector: nothing. */
    CHECK(!layout_screen_pos(0, 4, &p));
    CHECK(!layout_screen_pos(0, -1, &p));
    CHECK(!layout_screen_pos(2, 0, &p));
    CHECK(!layout_screen_pos(-1, 0, &p));
    CHECK(!layout_screen_pos(0, 0, NULL));
    CHECK(draw_guarded(-1));
    ui_text_set_language(UI_LANG_DE);
    CHECK(layout_screen_pos(0, 0, &p));
    CHECK_STR_EQ(p.state, "PROGRAMMIEREN");
    layout_screen_invalidate();
    CHECK(draw_guarded(-1));
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_servo_output_shows_the_protocol_bound_on_it)
{
    fresh_carrier();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, (uint8_t)proto_row("SERVO PWM"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 2u)));
    outbind_set_proto(&b, (uint8_t)proto_row("DSHOT600 BIDIR"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 1u)));
    outputs_screen_set_binding(&b);
    layout_pos_t p;
    CHECK(layout_screen_pos(1, 2, &p));          /* J2 3: PWM01, GPIO 2 */
    CHECK_STR_EQ(p.state, "SERVO PWM");
    CHECK(layout_screen_pos(1, 3, &p));          /* J2 4: PWM04, GPIO 1 */
    CHECK_STR_EQ(p.state, "DSHOT600 BIDIR");
    CHECK(layout_screen_pos(1, 0, &p));
    CHECK_STR_EQ(p.state, "OFF");
    CHECK(draw_guarded(-1));
}

TEST_CASE(an_output_reads_not_read_until_the_binding_is_read)
{
    fresh_carrier();
    bind_reading_t r;
    memset(&r, 0, sizeof(r));
    r.state = BIND_READ_NONE;
    outbind_init(&r.bind);
    outbind_set_board(&r.bind, RBMOD);
    outputs_screen_set_reading(&r);
    layout_pos_t p;
    CHECK(layout_screen_pos(1, 0, &p));
    CHECK_STR_EQ(p.state, "NOT READ");
    /* A binding of another board is not this board's either. */
    fresh();
    ui_board_set(RBMOD);
    CHECK(layout_screen_pos(1, 0, &p));
    CHECK_STR_EQ(p.state, "NOT READ");
    CHECK(draw_guarded(-1));
}

TEST_CASE(a_board_without_soldered_pins_has_no_positions)
{
    fresh();
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    layout_screen()->reset();
    layout_pos_t p;
    CHECK(!layout_screen_pos(0, 0, &p));
    CHECK(draw_guarded(-1));
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    CHECK(!layout_screen_pos(1, 0, &p));
    layout_screen_invalidate();
    CHECK(draw_guarded(-1));
    CHECK_STR_EQ(layout_screen()->title, "LAYOUT");
}

TEST_CASE(the_picture_repaints_when_the_binding_moves)
{
    fresh_carrier();
    layout_screen()->enter();
    CHECK(draw_guarded(0));
    /* Framebuffer 0 holds the picture: the next frame leaves it. */
    CHECK(!draw_guarded(0));
    /* Framebuffer 1 does not. */
    CHECK(draw_guarded(1));
    /* A protocol bound on PWM02 moves it, in every framebuffer. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, (uint8_t)proto_row("SERVO PWM"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 4u)));
    outputs_screen_set_binding(&b);
    CHECK(draw_guarded(0));
    CHECK(draw_guarded(1));
    CHECK(!draw_guarded(1));
    /* As does a change of board. */
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    CHECK(draw_guarded(0));
    /* And an invalidation, as a change of language or theme makes. */
    layout_screen_invalidate();
    CHECK(draw_guarded(0));
    /* A framebuffer index out of range is drawn and never cached. */
    CHECK(draw_guarded(3));
    CHECK(draw_guarded(3));
}

int main(void)
{
    RUN(j1_and_j2_follow_the_connectors_position_order);
    RUN(a_servo_output_shows_the_protocol_bound_on_it);
    RUN(an_output_reads_not_read_until_the_binding_is_read);
    RUN(a_board_without_soldered_pins_has_no_positions);
    RUN(the_picture_repaints_when_the_binding_moves);
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    return test_summary("layout_screen");
}
