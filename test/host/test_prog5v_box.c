/*
 * The PROG 5V box (shared/ui/prog5v_box.c): the programming ports' own 5V
 * on the rbmod carrier, GPIO 21.
 *
 * Under test: no box without the switch; PROG 5V ON asks ON after a hold of
 * 2,000ms and not after 1,999ms, once, and a release after it asks nothing
 * more; a hold let go early or slid off asks nothing; PROG 5V OFF is a tap
 * that lifts on the button; the button takes no press while the supply is
 * NOT READ or a port holds it, and a port taking it ends a hold; the hold
 * reaches with the read ON and lets go with OFF; an OFF asked stands over
 * an ON in the same poll; leaving the screen that switched it on asks OFF,
 * leaving another does not, nor does leaving while a port holds it; the
 * lines in EN and DE, the reading rounded, a negative current, none; the
 * box drawn inside its rect.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "guard_canvas.h"
#include "prog5v_box.h"
#include "ui_text.h"

enum { ST_UNKNOWN = 1, ST_OFF = 2, ST_ON = 3 };

#define BOX_R ((gfx_rect_t){ 648, 160, 356, PROG5V_BOX_H })

static prog5v_box_t b;

/* The supply read @p state, no port holding it; a fresh box on SERVO. */
static void fresh(uint8_t state)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    prog5v_box_publish(state, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_init(&b, BOX_R, PROG5V_BY_SERVO);
}

/* The button's centre. */
static int bx(void) { const gfx_rect_t r = prog5v_box_button(&b); return r.x + r.w / 2; }
static int by_(void) { const gfx_rect_t r = prog5v_box_button(&b); return r.y + r.h / 2; }

/* Ticks of @p s seconds, @p n of them. */
static void tick_n(int n, float s)
{
    for (int i = 0; i < n; ++i) {
        prog5v_box_tick(&b, s);
    }
}

/* Whether the boxes asked ON (and nothing else) since the last poll. */
static bool asked_on(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.on && !c.off;
}

static bool asked_off(void)
{
    prog5v_cmd_t c;
    return prog5v_box_poll_cmd(&c) && c.off && !c.on;
}

TEST_CASE(no_box_without_the_switch)
{
    fresh(0u);
    CHECK(!prog5v_box_shown());
    CHECK(!prog5v_box_live());
    CHECK(!prog5v_box_press(&b));
    char buf[48];
    CHECK_STR_EQ(prog5v_box_text(0, buf, sizeof(buf)), "");
    CHECK_STR_EQ(prog5v_box_text(0, NULL, 0u), "");
    CHECK(!prog5v_box_press(NULL));
    prog5v_box_init(NULL, BOX_R, PROG5V_BY_SERVO);
    CHECK_EQ(prog5v_box_button(NULL).w, 0);
}

TEST_CASE(on_is_a_hold_of_2000ms_not_1999ms)
{
    fresh(ST_OFF);
    CHECK(prog5v_box_shown());
    CHECK(prog5v_box_press(&b));
    /* 7 frames of 250ms and one of 249ms: 1,999ms. */
    tick_n(7, 0.25f);
    prog5v_box_tick(&b, 0.249f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* 1ms more: 2,000ms, ON once. */
    prog5v_box_tick(&b, 0.001f);
    CHECK(asked_on());
    tick_n(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* The release of the press that fired asks nothing, ON read or not. */
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    CHECK(b.hold.flash_ms > 0);               /* reached: the flash */
    prog5v_box_release(&b, bx(), by_(), true);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* A frame counts 250ms at most: one long frame is no hold. */
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    prog5v_box_tick(&b, 5.0f);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

TEST_CASE(a_hold_let_go_or_slid_off_asks_nothing)
{
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    tick_n(7, 0.25f);
    prog5v_box_release(&b, bx(), by_(), true);
    tick_n(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* Slid off the button: over, and back on resumes nothing. */
    CHECK(prog5v_box_press(&b));
    tick_n(4, 0.25f);
    CHECK(!prog5v_box_drag(&b, bx(), by_()));
    CHECK(prog5v_box_drag(&b, bx(), by_() - PROG5V_BOX_H));
    CHECK(!b.pressed);
    tick_n(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* A touch loss drops a hold. */
    CHECK(prog5v_box_press(&b));
    tick_n(7, 0.25f);
    prog5v_box_cancel(&b);
    tick_n(8, 0.25f);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* A hold completed in one frame, a touch loss found in the next before
     * the poll: the ON goes with the gesture. */
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    prog5v_box_cancel(&b);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* An OFF asked stays through a cancel; another screen's ON stays. */
    fresh(ST_ON);
    CHECK(prog5v_box_press(&b));
    prog5v_box_release(&b, bx(), by_(), true);
    prog5v_box_cancel(&b);
    CHECK(asked_off());
    fresh(ST_OFF);
    prog5v_box_t other;
    prog5v_box_init(&other, BOX_R, PROG5V_BY_SUPPLY);
    CHECK(prog5v_box_press(&other));
    for (int i = 0; i < 8; ++i) {
        prog5v_box_tick(&other, 0.25f);
    }
    prog5v_box_cancel(&b);
    CHECK(asked_on());
}

TEST_CASE(off_is_a_tap_that_lifts_on_the_button)
{
    fresh(ST_ON);
    CHECK(prog5v_box_press(&b));
    tick_n(12, 0.25f);                        /* no hold on OFF */
    CHECK(!prog5v_box_poll_cmd(NULL));
    prog5v_box_release(&b, bx(), by_(), true);
    CHECK(asked_off());
    /* Lifted off the button, or an UP that is no tap: nothing. */
    CHECK(prog5v_box_press(&b));
    prog5v_box_release(&b, bx(), by_() + PROG5V_BOX_H, true);
    CHECK(prog5v_box_press(&b));
    prog5v_box_release(&b, bx(), by_(), false);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* A port that took the supply after the last tick: the tap asks
     * nothing, the session's own LEAVE switches it. */
    CHECK(prog5v_box_press(&b));
    prog5v_box_publish(ST_ON, 21u, 1u);
    prog5v_box_release(&b, bx(), by_(), true);
    CHECK(!prog5v_box_poll_cmd(NULL));
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    /* An OFF asked stands over an ON in the same poll. */
    CHECK(prog5v_box_press(&b));
    prog5v_box_release(&b, bx(), by_(), true);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    CHECK(asked_off());
}

TEST_CASE(the_button_is_locked_while_not_read_or_held)
{
    /* NOT READ: no press. */
    fresh(ST_UNKNOWN);
    CHECK(prog5v_box_shown());
    CHECK(!prog5v_box_live());
    CHECK(!prog5v_box_press(&b));
    /* A port holds it, off or on: no press, the line says which port. */
    fresh(ST_OFF);
    prog5v_box_publish(ST_OFF, 21u, 2u);
    CHECK(!prog5v_box_press(&b));
    prog5v_box_publish(ST_ON, 21u, 0u);
    CHECK(!prog5v_box_press(&b));
    char buf[48];
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "HELD BY PROG PORT 1");
    prog5v_box_publish(ST_ON, 21u, 3u);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "HELD BY PROG PORT 4");
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_SOME);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "HELD BY A PROG PORT");
    ui_text_set_language(UI_LANG_DE);
    prog5v_box_publish(ST_ON, 21u, 1u);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)),
                 "BELEGT VON PROG-PORT 2");
    ui_text_set_language(UI_LANG_EN);
    /* A port taking the supply under a hold ends it: no ON. */
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    tick_n(7, 0.25f);
    prog5v_box_publish(ST_OFF, 21u, 0u);
    tick_n(8, 0.25f);
    CHECK(!b.pressed);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* The supply going off under the finger (an error, a lost heartbeat)
     * ends the press of an OFF. */
    fresh(ST_ON);
    CHECK(prog5v_box_press(&b));
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    prog5v_box_release(&b, bx(), by_(), true);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

TEST_CASE(leaving_the_screen_that_switched_it_on_switches_it_off)
{
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    CHECK(asked_on());
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    prog5v_box_release(&b, bx(), by_(), true);
    /* SUPPLY left: not its. */
    prog5v_box_left(PROG5V_BY_SUPPLY);
    prog5v_box_left(PROG5V_BY_NONE);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* SERVO left while a port holds it: the port's. */
    prog5v_box_publish(ST_ON, 21u, 0u);
    prog5v_box_left(PROG5V_BY_SERVO);
    CHECK(!prog5v_box_poll_cmd(NULL));
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_left(PROG5V_BY_SERVO);
    CHECK(asked_off());
    /* Once left, a second leave asks nothing. */
    prog5v_box_left(PROG5V_BY_SERVO);
    CHECK(!prog5v_box_poll_cmd(NULL));
    /* An ON collected and on its way, still read OFF: left, it is asked
     * off all the same. */
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    CHECK(asked_on());
    prog5v_box_left(PROG5V_BY_SERVO);
    CHECK(asked_off());
    /* An ON asked and not yet read is left as well. */
    fresh(ST_OFF);
    prog5v_box_init(&b, BOX_R, PROG5V_BY_SUPPLY);
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    prog5v_box_left(PROG5V_BY_SUPPLY);
    CHECK(asked_off());
    /* Read OFF since (a programmer's session ended it): nobody's. */
    fresh(ST_OFF);
    CHECK(prog5v_box_press(&b));
    tick_n(8, 0.25f);
    CHECK(asked_on());
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    prog5v_box_left(PROG5V_BY_SERVO);
    CHECK(!prog5v_box_poll_cmd(NULL));
}

TEST_CASE(the_lines_name_the_pin_and_the_reading)
{
    char buf[48];
    fresh(ST_OFF);
    CHECK_STR_EQ(prog5v_box_text(0, buf, sizeof(buf)),
                 "PROG PORTS 5V, GPIO 21: OFF");
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "--V  --A");
    prog5v_box_reading(true, 5024u, 120);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "5.02V  0.120A");
    prog5v_box_reading(true, 4995u, -3);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "5.00V  -0.003A");
    prog5v_box_reading(true, 0u, 0);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "0.00V  0.000A");
    prog5v_box_reading(true, 32767u, 32767);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "32.77V  32.767A");
    prog5v_box_reading(false, 5000u, 1);
    CHECK_STR_EQ(prog5v_box_text(1, buf, sizeof(buf)), "--V  --A");
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    CHECK_STR_EQ(prog5v_box_text(0, buf, sizeof(buf)),
                 "PROG PORTS 5V, GPIO 21: ON");
    prog5v_box_publish(ST_UNKNOWN, 21u, (uint8_t)PROG5V_HELD_NONE);
    CHECK_STR_EQ(prog5v_box_text(0, buf, sizeof(buf)),
                 "PROG PORTS 5V, GPIO 21: NOT READ");
    ui_text_set_language(UI_LANG_DE);
    prog5v_box_publish(ST_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    CHECK_STR_EQ(prog5v_box_text(0, buf, sizeof(buf)),
                 "PROG-PORTS 5V, GPIO 21: AUS");
    CHECK_STR_EQ(TR(P5_ON), "PROG 5V EIN");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(TR(P5_ON), "PROG 5V ON");
    CHECK_STR_EQ(TR(P5_OFF), "PROG 5V OFF");
}

TEST_CASE(the_box_is_drawn_inside_its_rect)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(1024, 600)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, 1024, 600);
    fresh(ST_OFF);
    prog5v_box_reading(true, 5010u, 80);
    prog5v_box_render(&b, &c);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    const uint32_t off = guard_canvas_sum(&g);
    prog5v_box_publish(ST_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    prog5v_box_tick(&b, 0.02f);
    prog5v_box_render(&b, &c);
    CHECK(guard_canvas_sum(&g) != off);
    /* The width the lines and the button need. */
    CHECK(BOX_R.w >= PROG5V_BOX_MIN_W);
    prog5v_box_render(NULL, &c);
    prog5v_box_render(&b, NULL);
}

int main(void)
{
    RUN(no_box_without_the_switch);
    RUN(on_is_a_hold_of_2000ms_not_1999ms);
    RUN(a_hold_let_go_or_slid_off_asks_nothing);
    RUN(off_is_a_tap_that_lifts_on_the_button);
    RUN(the_button_is_locked_while_not_read_or_held);
    RUN(leaving_the_screen_that_switched_it_on_switches_it_off);
    RUN(the_lines_name_the_pin_and_the_reading);
    RUN(the_box_is_drawn_inside_its_rect);
    return test_summary("prog5v_box");
}
