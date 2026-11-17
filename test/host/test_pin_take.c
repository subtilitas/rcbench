/**
 * @file test_pin_take.c
 * @brief OUTPUTS' board view: a tap on a pin another protocol holds asks
 *        to move it, MOVE PIN writes the binding the move gives once, and
 *        everything else writes nothing; a pin the card does not ask about
 *        says why on the reason line.
 *
 * Cases:
 * - a_tap_on_an_esc_pin_under_servo_pwm_asks_and_writes_nothing
 * - move_pin_writes_once_with_the_pin_moved
 * - cancel_home_and_a_tap_outside_write_nothing
 * - the_card_names_the_last_pin_and_the_role_change
 * - the_card_closes_when_the_binding_goes_unread
 * - the_card_closes_on_a_board_change_a_pick_and_a_view_switch
 * - move_pin_after_the_pin_moved_elsewhere_writes_nothing
 * - a_refused_write_leaves_the_pin_with_its_holder
 * - no_card_on_off_and_the_reason_names_the_holder
 * - no_card_for_a_port_or_interface_pin_and_the_reason_says_where
 * - no_card_when_the_taker_cannot_hold_the_pin_and_the_reason_says_why
 * - no_card_on_a_port_entry
 * - lost_touch_events_close_nothing_but_the_press
 * - a_second_finger_takes_the_press_or_closes_the_card
 * - stop_counts_while_the_card_is_open
 * - the_dial_focus_lists_move_and_cancel
 * - the_worst_case_card_fits_in_both_languages
 * - every_take_reason_ends_left_of_the_board
 * - the_tapped_key_stays_visible_on_every_built_in_board
 * - the_card_and_its_buttons_clear_the_key_on_every_drawable_board
 * - each_card_starts_the_dial_on_cancel
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "picker_screen_rig.h"

#include "bind_link.h"
#include "outputs_screen_int.h"
#include "pin_take.h"
#include "port_view.h"
#include "servo_screen.h"
#include "text_mask.h"
#include "ui_board.h"
#include "ui_cats.h"
#include "ui_focus.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_widgets.h"

/** @brief A point off the card: the IN USE column. */
#define USE_X_PROBE 900

/** @brief The screen under test: OUTPUTS with both views. */
static const ui_screen_t *view(void) { return outputs_view_screen(); }

/** @brief One touch event of contact @p id at (@p x, @p y). */
static void ev(touch_event_type_t type, int x, int y, uint8_t id)
{
    touch_event_t e = { type, { id, (int16_t)x, (int16_t)y, 40 }, 0u };
    view()->event(&e);
}

/** @brief A tap at the center of @p r. */
static void tap_at(gfx_rect_t r)
{
    ev(TOUCH_EVENT_DOWN, r.x + r.w / 2, r.y + r.h / 2, 0u);
    ev(TOUCH_EVENT_UP, r.x + r.w / 2, r.y + r.h / 2, 0u);
}

/** @brief The key of GP@p gpio. */
static gfx_rect_t key(uint8_t gpio)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    if (!picker_screen_button_rect(idx(gpio), &r)) {
        T_FAIL("GP%u has no key", (unsigned)gpio);
    }
    return r;
}

/** @brief A tap on GP@p gpio's key. */
static void tap_key(uint8_t gpio) { tap_at(key(gpio)); }

/** @brief A tap on the card's button @p b. */
static void tap_btn(pin_take_btn_t b) { tap_at(pin_take_button(b)); }

/** @brief The ports: 1 SERVO PWM on GP13, 2 NONE on GP14, 3 and 4 none. */
static const uint8_t k_ports[LINK_PG_PORTS_MAX] = { 13u, 14u, 0xFFu, 0xFFu };

/** @brief The IOmcu's four ports, to both views. */
static void four_ports(void)
{
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    v.pin[0] = 13u;
    v.pin[1] = 14u;
    v.func[0] = v.func[2] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[1] = v.func[3] = (uint8_t)LINK_PG_FN_NONE;
    port_view_set(&v);
    outputs_screen_set_prog_ports(4u, k_ports);
    picker_screen_set_prog_ports(4u, k_ports);
}

/** @brief What stands beside the binding, as the board view sees it. */
static outbind_take_ctx_t view_ctx(void)
{
    static uint64_t ifc[OUTPUTS_IFACE_COUNT];
    outputs_iface_read(ifc);
    return (outbind_take_ctx_t){
        .read = true, .ports = k_ports, .pwm_ports = outputs_pwm_pins(k_ports),
        .port_count = LINK_PG_PORTS_MAX, .ifaces = ifc,
        .iface_count = OUTPUTS_IFACE_COUNT,
    };
}

/** @brief SERVO PWM on GP0 to GP2, DSHOT600 on GP6 and GP7, SERVO PWM
 *  selected. */
static outbind_t bench(void)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0u)));
    CHECK(outbind_toggle(&b, idx(1u)));
    CHECK(outbind_toggle(&b, idx(2u)));
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(6u)));
    CHECK(outbind_toggle(&b, idx(7u)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    return b;
}

/**
 * @brief The panel's frame after a read: the grid's screen takes the
 *        reading, the board view what came out of it, then the follow.
 * @param b     the binding read, or the last one for a failed read
 * @param state the read's outcome
 */
static void panel_reads(const outbind_t *b, bind_read_t state)
{
    static bind_reading_t r;
    if (state == BIND_READ_OK) {
        uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
        (void)outbind_to_slots(b, slots);
        outbind_to_chan_cfg(b, cc, 1000u, 2000u);
        (void)bind_link_classify(&r, b->board, slots, cc);
    } else {
        r.bind = *b;
        r.state = state;
    }
    outputs_screen_set_reading(&r);
    picker_screen_set_binding(outputs_screen_binding());
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
}

/** @brief Draws a frame into the rig's canvas. */
static void frame(void)
{
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    picker_screen_invalidate();
    outputs_screen_invalidate();
    view()->render(&c, 0);
}

/** @brief Everything back, English, then a visit of @p b on the drawn
 *  board, its selection picked. */
static void visit(const outbind_t *b)
{
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
    ui_cats_force(0);
    view()->reset();
    picker_screen_set_prog_ports(0u, NULL);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    s_applied = 0;
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    four_ports();
    outputs_screen_set_binding(b);
    picker_screen_set_binding(b);
    CHECK(serve_drawing());
    view()->enter();
    outputs_screen_select(b->proto);
    view()->tick(0.016f);
    CHECK(outputs_view_is_board());
    frame();
}

/** @brief The card's line @p which, in a static buffer. */
static const char *line(pin_take_line_t which)
{
    static char buf[5][128];
    (void)pin_take_line(which, buf[which], sizeof(buf[which]));
    return buf[which];
}

/** @brief The same binding as the one the case started from. */
static bool same(const outbind_t *a, const outbind_t *b)
{
    return memcmp(a->pins, b->pins, sizeof(a->pins)) == 0
           && a->board == b->board;
}

/* ------------------------------------------------------------- the card */

TEST_CASE(a_tap_on_an_esc_pin_under_servo_pwm_asks_and_writes_nothing)
{
    const outbind_t b = bench();
    visit(&b);
    CHECK_EQ(outputs_help_count(), 2);
    tap_key(7u);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    CHECK_EQ(pin_take_index(), (int)idx(7u));
    CHECK(view()->covered());
    CHECK_EQ(outputs_help_count(), 0);          /* no guide over the card */
    CHECK_EQ(s_applied, 0);
    CHECK(same(picker_screen_binding(), &b));
    CHECK_STR_EQ(reason(), "");
    /* Over the board, the size of the tallest board's box. */
    const gfx_rect_t r = pin_take_rect();
    CHECK_EQ(r.w, 600);
    CHECK_EQ(r.h, 240);
    CHECK_EQ(r.x, (SCREEN_W - 600) / 2);
    CHECK_EQ(r.y, (PK_FOOT - 240) / 2);
    frame();
    CHECK(text_mask_found(s_px, SCREEN_W, SCREEN_H, r, &gfx_font_16x28,
                          "GPIO 7: DSHOT600"));
}

TEST_CASE(move_pin_writes_once_with_the_pin_moved)
{
    const outbind_t b = bench();
    visit(&b);
    const outbind_take_ctx_t ctx = view_ctx();
    outbind_t want;
    CHECK_EQ(outbind_take_check(&b, idx(7u), proto_named("SERVO PWM"), &ctx,
                                &want, NULL),
             OUTBIND_TAKE_OK);
    tap_key(7u);
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    tap_btn(PIN_TAKE_BTN_MOVE);
    CHECK_EQ(s_applied, 1);
    CHECK(same(&s_last, &want));
    CHECK_EQ(outbind_group_of(&s_last, idx(7u)), proto_named("SERVO PWM"));
    CHECK_EQ(outbind_group_of(&s_last, idx(6u)), proto_named("DSHOT600"));
    CHECK(!pin_take_is_open());
    CHECK(!view()->covered());
    CHECK(same(picker_screen_binding(), &want));
    /* The same spot again is the board under the card: nothing more. */
    tap_at(mv);
    CHECK_EQ(s_applied, 1);
    /* The read-back shows it, and the reverse move is the undo. */
    panel_reads(&want, BIND_READ_OK);
    outputs_screen_select(proto_named("DSHOT600"));
    view()->tick(0.016f);
    frame();
    tap_key(7u);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    tap_btn(PIN_TAKE_BTN_MOVE);
    CHECK_EQ(s_applied, 2);
    CHECK(same(&s_last, &b));
}

TEST_CASE(cancel_home_and_a_tap_outside_write_nothing)
{
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    tap_btn(PIN_TAKE_BTN_CANCEL);
    CHECK(!pin_take_is_open());

    tap_key(7u);
    CHECK(view()->back());                      /* the home tag closes it */
    CHECK(!pin_take_is_open());
    CHECK(!view()->back());                     /* and then leaves */

    /* A press on a free key off the card closes it and binds nothing. */
    tap_key(7u);
    tap_key(4u);
    CHECK(!pin_take_is_open());
    CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(4u)), 0);
    /* A press inside the card but off its buttons does nothing. */
    tap_key(7u);
    const gfx_rect_t r = pin_take_rect();
    tap_at(gfx_rect_make(r.x + 20, r.y + 60, 10, 10));
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    /* Leaving the screen closes it. */
    view()->leave();
    CHECK(!pin_take_is_open());
    CHECK_EQ(s_applied, 0);
    CHECK(same(picker_screen_binding(), &b));
}

TEST_CASE(the_card_names_the_last_pin_and_the_role_change)
{
    outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_TITLE), "GPIO 7: DSHOT600");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ASK),
                 "REMOVE GPIO 7 FROM DSHOT600 AND GIVE IT TO SERVO PWM?");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "DSHOT600 KEEPS 1 PIN.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ROLE),
                 "CH4 BECOMES A SERVO CHANNEL: IT CENTERS AT REST.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_LEAD),
                 "THE LEAD ON PAD 10 GETS SERVO PWM FROM THE NEXT ARM.");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "DSHOT600 BEHÄLT 1 PIN.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ROLE),
                 "CH4 WIRD ZUM SERVOKANAL: IN RUHE MITTE.");
    ui_text_set_language(UI_LANG_EN);
    tap_btn(PIN_TAKE_BTN_CANCEL);

    /* GP6 back to SERVO PWM's: DSHOT600 keeps no pin. */
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(6u)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    panel_reads(&b, BIND_READ_OK);
    frame();
    tap_key(7u);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "DSHOT600 KEEPS NO PIN.");

    /* MOTOR PWM taking a servo's pin: the channel turns into a motor's.
     * Two servos stay: SERVO PWM keeps 2 pins. */
    tap_btn(PIN_TAKE_BTN_CANCEL);
    outputs_screen_select(proto_named("MOTOR PWM"));
    view()->tick(0.016f);
    frame();
    tap_key(1u);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "SERVO PWM KEEPS 2 PINS.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ROLE),
                 "CH1 BECOMES A MOTOR CHANNEL: IT STOPS AT REST.");

    /* A throttle to a throttle: no role line. */
    tap_btn(PIN_TAKE_BTN_CANCEL);
    outputs_screen_select(proto_named("DSHOT300"));
    view()->tick(0.016f);
    frame();
    tap_key(7u);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ROLE), "");
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(the_card_closes_when_the_binding_goes_unread)
{
    const outbind_t b = bench();
    for (int k = 0; k < 2; ++k) {
        visit(&b);
        tap_key(7u);
        const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
        /* Link lost: no read; a read that failed: the same. */
        panel_reads(&b, (k == 0) ? BIND_READ_NONE : BIND_READ_ODD);
        CHECK(!pin_take_is_open());
        CHECK(!view()->covered());
        tap_at(mv);
        CHECK_EQ(s_applied, 0);
        /* And a binding not read takes no tap that asks. */
        tap_key(7u);
        CHECK(!pin_take_is_open());
    }
}

TEST_CASE(the_card_closes_on_a_board_change_a_pick_and_a_view_switch)
{
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    /* Another write's read-back: GP2 moved, GP7 still DSHOT600's. */
    outbind_t w = b;
    CHECK(outbind_toggle(&w, idx(2u)));
    panel_reads(&w, BIND_READ_OK);
    CHECK(!pin_take_is_open());

    /* A pick in the grid's list, the board view following it. */
    tap_key(7u);
    CHECK(pin_take_is_open());
    outputs_screen_select(proto_named("MOTOR PWM"));
    view()->tick(0.016f);
    CHECK(!pin_take_is_open());

    /* A PROG PORT entry picked. */
    outputs_screen_select(proto_named("SERVO PWM"));
    view()->tick(0.016f);
    frame();
    tap_key(7u);
    CHECK(pin_take_is_open());
    outputs_screen_select_port(1u);
    view()->tick(0.016f);
    CHECK(!pin_take_is_open());

    /* The board swapped for the carrier: the grid, and no card. */
    visit(&b);
    tap_key(7u);
    CHECK(pin_take_is_open());
    ui_board_set((uint16_t)OUTBIND_BOARD_RBMOD);
    view()->tick(0.016f);
    CHECK(!outputs_view_is_board());
    CHECK(!pin_take_is_open());
    CHECK(!view()->covered());
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);

    /* Another board read. */
    visit(&b);
    tap_key(7u);
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)OUTBIND_BOARD_RBMOD);
    panel_reads(&other, BIND_READ_OK);
    CHECK(!pin_take_is_open());

    /* A reset forgets it. */
    visit(&b);
    tap_key(7u);
    view()->reset();
    CHECK(!pin_take_is_open());
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(move_pin_after_the_pin_moved_elsewhere_writes_nothing)
{
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    /* Read back with GP7 gone from DSHOT600 to MOTOR PWM. */
    outbind_t w = b;
    outbind_set_proto(&w, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&w, idx(7u)));
    outbind_set_proto(&w, proto_named("MOTOR PWM"));
    CHECK(outbind_toggle(&w, idx(7u)));
    outbind_set_proto(&w, proto_named("SERVO PWM"));
    panel_reads(&w, BIND_READ_OK);
    CHECK(!pin_take_is_open());
    tap_at(mv);
    CHECK_EQ(s_applied, 0);

    /* The binding as it was, and port 1 moved to GP22 with SERVO PWM:
     * GP7's slice is the port's now, and MOVE PIN, checked again, writes
     * nothing and closes. */
    visit(&b);
    tap_key(7u);
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    v.pin[0] = 22u;
    v.pin[1] = 14u;
    v.func[0] = v.func[2] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[1] = v.func[3] = (uint8_t)LINK_PG_FN_NONE;
    port_view_set(&v);
    static const uint8_t moved[LINK_PG_PORTS_MAX] = { 22u, 14u, 0xFFu,
                                                      0xFFu };
    picker_screen_set_prog_ports(4u, moved);
    CHECK(pin_take_is_open());
    tap_btn(PIN_TAKE_BTN_MOVE);
    CHECK(!pin_take_is_open());
    CHECK_EQ(s_applied, 0);
    CHECK(same(picker_screen_binding(), &b));
    picker_screen_set_prog_ports(4u, k_ports);
}

TEST_CASE(a_refused_write_leaves_the_pin_with_its_holder)
{
    /* The IOmcu refuses any slot change on an armed bank: LAST WRITE says
     * REFUSED and the read-back shows the pin with its holder. */
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    tap_btn(PIN_TAKE_BTN_MOVE);
    CHECK_EQ(s_applied, 1);
    outputs_screen_set_result(OUTPUTS_REFUSED);
    panel_reads(&b, BIND_READ_OK);
    CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(7u)),
             proto_named("DSHOT600"));
    CHECK_STR_EQ(outputs_result_text(), TR(OUT_REFUSED));
    CHECK(!pin_take_is_open());
    /* And it asks again on the next tap. */
    frame();
    tap_key(7u);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    CHECK_EQ(s_applied, 1);
}

/* --------------------------------------------------- no card, a reason */

TEST_CASE(no_card_on_off_and_the_reason_names_the_holder)
{
    const outbind_t b = bench();
    visit(&b);
    outputs_screen_select(0u);
    view()->tick(0.016f);
    frame();
    tap_key(7u);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "GPIO 7: DSHOT600. PICK A PROTOCOL FIRST");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "GPIO 7: DSHOT600. WÄHLE ERST EIN PROTOKOLL");
    ui_text_set_language(UI_LANG_EN);
    /* A free key on OFF takes no press, and the line stays. */
    tap_key(4u);
    CHECK_STR_EQ(reason(), "GPIO 7: DSHOT600. PICK A PROTOCOL FIRST");
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(no_card_for_a_port_or_interface_pin_and_the_reason_says_where)
{
    const outbind_t b = bench();
    visit(&b);
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 19.0f);
    settings_set(SET_PDMINI_RX, 18.0f);
    settings_set(SET_TONE_EN, 1.0f);
    frame();
    static const struct { uint8_t gp; const char *en; const char *de; }
    k[] = {
        { 13u, "GPIO 13: PROG PORT 1. FREE IT ON ITS ENTRY",
               "GPIO 13: PROG-PORT 1. GIB IHN IM EINTRAG FREI" },
        { 14u, "GPIO 14: PROG PORT 2. FREE IT ON ITS ENTRY",
               "GPIO 14: PROG-PORT 2. GIB IHN IM EINTRAG FREI" },
        { 16u, "GPIO 16: SENSOR BUS, SET ON SENSORS",
               "GPIO 16: SENSORBUS, IN SENSORS" },
        { 18u, "GPIO 18: PD MINI, SET ON SUPPLY",
               "GPIO 18: PD MINI, IN SUPPLY" },
        { 22u, "GPIO 22: PHASE TAP, SET ON SENSORS",
               "GPIO 22: PHASENABGRIFF, IN SENSORS" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        tap_key(k[i].gp);
        CHECK(!pin_take_is_open());
        CHECK_STR_EQ(reason(), k[i].en);
        ui_text_set_language(UI_LANG_DE);
        CHECK_STR_EQ(reason(), k[i].de);
        ui_text_set_language(UI_LANG_EN);
    }
    /* A tick forgets the line. */
    tap_key(4u);
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(13u)), 0);
}

TEST_CASE(no_card_when_the_taker_cannot_hold_the_pin_and_the_reason_says_why)
{
    /* PPM: 8 channels on one pin, and SERVO PWM keeps two of its pins. */
    outbind_t b = bench();
    b.proto = proto_named("PPM");
    visit(&b);
    tap_key(2u);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "NEEDS 8 CHANNELS, 4 FREE");

    /* A PWM taker on a bound PWM pin's compare register: SERVO PWM on
     * GP0, DSHOT600 on GP16. */
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0u)));
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(16u)));
    CHECK(outbind_toggle(&b, idx(28u)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    visit(&b);
    tap_key(16u);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "GPIO 16: SAME PWM CHANNEL AS GPIO 0");
    /* And on PROG PORT 1's slice: GP28 beside GP13. */
    tap_key(28u);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13");
    /* MOTOR PWM is under the same rules. */
    outputs_screen_select(proto_named("MOTOR PWM"));
    view()->tick(0.016f);
    tap_key(16u);
    CHECK_STR_EQ(reason(), "GPIO 16: SAME PWM CHANNEL AS GPIO 0");
    /* A DShot taker is under none: it asks. */
    outputs_screen_select(proto_named("DSHOT300"));
    view()->tick(0.016f);
    tap_key(28u);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(no_card_on_a_port_entry)
{
    const outbind_t b = bench();
    visit(&b);
    outputs_screen_select_port(1u);
    view()->tick(0.016f);
    CHECK_EQ(picker_screen_port_shown(), 1);
    frame();
    tap_key(7u);
    CHECK(!pin_take_is_open());
    CHECK_EQ(s_applied, 0);
}

/* ------------------------------------------------------------- touches */

TEST_CASE(lost_touch_events_close_nothing_but_the_press)
{
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    ev(TOUCH_EVENT_DOWN, mv.x + 10, mv.y + 10, 0u);
    view()->cancel();
    ev(TOUCH_EVENT_UP, mv.x + 10, mv.y + 10, 0u);
    CHECK_EQ(s_applied, 0);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    /* A release the tracker made writes nothing either. */
    ev(TOUCH_EVENT_DOWN, mv.x + 10, mv.y + 10, 0u);
    touch_event_t u = { TOUCH_EVENT_UP, { 0u, (int16_t)(mv.x + 10),
                                          (int16_t)(mv.y + 10), 40 },
                        TOUCH_FLAG_NO_TAP };
    view()->event(&u);
    CHECK_EQ(s_applied, 0);
    /* A finger that slides off MOVE PIN before it lifts: nothing. */
    ev(TOUCH_EVENT_DOWN, mv.x + 10, mv.y + 10, 0u);
    ev(TOUCH_EVENT_MOVE, mv.x + mv.w + 30, mv.y + 10, 0u);
    ev(TOUCH_EVENT_UP, mv.x + mv.w + 30, mv.y + 10, 0u);
    CHECK_EQ(s_applied, 0);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    /* A fresh tap acts. */
    tap_btn(PIN_TAKE_BTN_MOVE);
    CHECK_EQ(s_applied, 1);
}

TEST_CASE(a_second_finger_takes_the_press_or_closes_the_card)
{
    const outbind_t b = bench();
    visit(&b);
    tap_key(7u);
    gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    gfx_rect_t cn = pin_take_button(PIN_TAKE_BTN_CANCEL);
    /* Contact 1 on MOVE PIN, contact 2 on CANCEL: the press is the
     * second's, and the first's lift does nothing. */
    ev(TOUCH_EVENT_DOWN, mv.x + 10, mv.y + 10, 1u);
    ev(TOUCH_EVENT_DOWN, cn.x + 10, cn.y + 10, 2u);
    ev(TOUCH_EVENT_UP, mv.x + 10, mv.y + 10, 1u);
    CHECK_EQ(s_applied, 0);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    ev(TOUCH_EVENT_UP, cn.x + 10, cn.y + 10, 2u);
    CHECK(!pin_take_is_open());

    /* Contact 1 on MOVE PIN, contact 2 off the card: the card closes and
     * the first's lift writes nothing. */
    tap_key(7u);
    mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    ev(TOUCH_EVENT_DOWN, mv.x + 10, mv.y + 10, 1u);
    ev(TOUCH_EVENT_DOWN, USE_X_PROBE, 300, 2u);
    CHECK(!pin_take_is_open());
    ev(TOUCH_EVENT_UP, mv.x + 10, mv.y + 10, 1u);
    ev(TOUCH_EVENT_UP, USE_X_PROBE, 300, 2u);
    CHECK_EQ(s_applied, 0);
    CHECK(same(picker_screen_binding(), &b));
}

TEST_CASE(stop_counts_while_the_card_is_open)
{
    const outbind_t b = bench();
    visit(&b);
    ui_router_init();
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    four_ports();
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(&b);
    CHECK(serve_drawing());
    ui_router_goto(SCREEN_OUTPUTS);
    outputs_screen_select(b.proto);
    ui_router_tick(0.016f);
    CHECK(outputs_view_is_board());
    s_applied = 0;

    const gfx_rect_t k = key(7u);
    touch_event_t e = { TOUCH_EVENT_DOWN,
                        { 1u, (int16_t)(k.x + k.w / 2),
                          (int16_t)(UI_BAND_H + k.y + k.h / 2), 40 }, 0u };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));

    /* Contact 1 down on MOVE PIN, contact 2 on STOP: the stop is taken
     * at its press; MOVE PIN's lift writes nothing. */
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    touch_event_t m = { TOUCH_EVENT_DOWN,
                        { 1u, (int16_t)(mv.x + 10),
                          (int16_t)(UI_BAND_H + mv.y + 10), 40 }, 0u };
    ui_router_event(&m);
    const gfx_rect_t st = ui_band_stop_rect();
    touch_event_t sp = { TOUCH_EVENT_DOWN,
                         { 2u, (int16_t)(st.x + st.w / 2),
                           (int16_t)(st.y + st.h / 2), 40 }, 0u };
    ui_router_event(&sp);
    CHECK(ui_router_take_stop());
    m.type = TOUCH_EVENT_UP;
    ui_router_event(&m);
    sp.type = TOUCH_EVENT_UP;
    ui_router_event(&sp);
    CHECK_EQ(s_applied, 0);
    CHECK(same(picker_screen_binding(), &b));
    ui_router_goto(SCREEN_OVERVIEW);
}

TEST_CASE(the_dial_focus_lists_move_and_cancel)
{
    const outbind_t b = bench();
    visit(&b);
    static ui_focus_list_t f;
    memset(&f, 0, sizeof(f));
    CHECK(picker_screen_focus(&f));
    const int keys = f.count;
    tap_key(7u);
    memset(&f, 0, sizeof(f));
    CHECK(picker_screen_focus(&f));
    CHECK_EQ(f.count, 2);
    CHECK(keys > 2);
    CHECK_STR_EQ(f.item[0].label, "MOVE PIN");
    CHECK_STR_EQ(f.item[1].label, "CANCEL");
    CHECK_EQ(f.start, 1);
    CHECK(gfx_rect_contains(pin_take_button(PIN_TAKE_BTN_MOVE),
                            f.item[0].tap_x, f.item[0].tap_y));
    CHECK(gfx_rect_contains(pin_take_button(PIN_TAKE_BTN_CANCEL),
                            f.item[1].tap_x, f.item[1].tap_y));
}

/* ----------------------------------------------------------------- fit */

/*
 * Every protocol losing GP28, the pin with the longest number, to every
 * other, keeping 0, 1 or 2 pins: the card's lines end above its buttons
 * without a cut, in both languages, and the frame stays inside its canvas.
 * DSHOT600 BIDIR losing to SERVO PWM, or the reverse, gives every line.
 */
TEST_CASE(the_worst_case_card_fits_in_both_languages)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    static const uint8_t more[2] = { 26u, 27u };
    int asked = 0;
    for (uint8_t loser = 1; loser < OUTBIND_PROTOS; ++loser) {
        for (uint8_t taker = 1; taker < OUTBIND_PROTOS; ++taker) {
            for (unsigned keeps = 0; keeps < 3u; ++keeps) {
                if (taker == loser
                    || (keeps > 0u
                        && (outbind_protos()[loser].max_pins <= keeps
                            || outbind_protos()[taker].channels > 1u))) {
                    continue;
                }
                outbind_t b;
                outbind_init(&b);
                outbind_set_board(&b, BOARD);
                outbind_set_proto(&b, loser);
                CHECK(outbind_toggle(&b, idx(28u)));
                for (unsigned k = 0; k < keeps; ++k) {
                    CHECK(outbind_toggle(&b, idx(more[k])));
                }
                outbind_set_proto(&b, taker);
                visit(&b);
                picker_screen_set_prog_ports(0u, NULL);
                frame();
                tap_key(28u);
                if (!pin_take_open_in(PIN_TAKE_BOARD)) {
                    T_FAIL("%s from %s asked nothing",
                           outbind_protos()[taker].name,
                           outbind_protos()[loser].name);
                    continue;
                }
                ++asked;
                for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
                    ui_text_set_language((ui_lang_t)l);
                    pin_take_layout_t lay;
                    CHECK(pin_take_layout(&lay));
                    if (lay.cut || lay.text_end > lay.buttons_y - 4) {
                        T_FAIL("language %d, %s from %s: ends %d, buttons "
                               "%d, cut %d", l, outbind_protos()[taker].name,
                               outbind_protos()[loser].name, lay.text_end,
                               lay.buttons_y, (int)lay.cut);
                    }
                    guard_canvas_t g;
                    gfx_canvas_t c;
                    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
                    picker_screen_invalidate();
                    view()->render(&c, 0);
                    CHECK_EQ(guard_canvas_touched(&g), 0u);
                }
                ui_text_set_language(UI_LANG_EN);
            }
        }
    }
    CHECK(asked > 40);
    /* The worst German case keeps its 10 x 20 question over 8 x 16
     * role and lead lines. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("DSHOT600 BIDIR"));
    CHECK(outbind_toggle(&b, idx(28u)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    visit(&b);
    picker_screen_set_prog_ports(0u, NULL);
    frame();
    tap_key(28u);
    ui_text_set_language(UI_LANG_DE);
    pin_take_layout_t lay;
    CHECK(pin_take_layout(&lay));
    CHECK_EQ(lay.title_h, 28);
    CHECK_EQ(lay.body_h, 20);
    CHECK_EQ(lay.small_h, 16);
    ui_text_set_language(UI_LANG_EN);
}

/**
 * @brief Whether @p text, wrapped as the reason line is, keeps its first
 *        line inside @p first px and its second inside @p second px.
 * @param text   the line
 * @param first  the first line's room, px
 * @param second the second line's room, px
 * @return true when both lines end inside their room
 */
static bool wraps_inside(const char *text, int first, int second)
{
    static gfx_color_t px[720 * 48];
    gfx_canvas_t c = { px, 720, 48, 720, { 0, 0, 720, 48 } };
    memset(px, 0, sizeof(px));
    (void)outputs_wrap(&c, gfx_rect_make(0, 0, first, 20), 22, text, 2,
                       &gfx_font_10x20, GFX_RGB(255, 255, 255), false);
    for (int y = 0; y < 48; ++y) {
        const int room = (y < 22) ? first : second;
        for (int x = room; x < 720; ++x) {
            if (px[y * 720 + x] != 0) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Every reason a tap gives, with the longest pin, port and protocol names:
 * on the board view 35 cells beside the keys and 17 beside the board, on
 * the grid two lines of 30.
 */
TEST_CASE(every_take_reason_ends_left_of_the_board)
{
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        const ui_lang_t g = (ui_lang_t)l;
        char s[6][96];
        snprintf(s[0], sizeof(s[0]), ui_tr_in(g, TX_PK_TAKE_WHY_PORT), 28u,
                 (g == UI_LANG_EN) ? "PROG PORT 4" : "PROG-PORT 4");
        snprintf(s[1], sizeof(s[1]), ui_tr_in(g, TX_PK_TAKE_WHY_SENSE), 28u);
        snprintf(s[2], sizeof(s[2]), ui_tr_in(g, TX_PK_TAKE_WHY_PDMINI), 28u);
        snprintf(s[3], sizeof(s[3]), ui_tr_in(g, TX_PK_TAKE_WHY_TAP), 28u);
        snprintf(s[4], sizeof(s[4]), ui_tr_in(g, TX_PK_TAKE_WHY_OFF), 28u,
                 "DSHOT600 BIDIR");
        snprintf(s[5], sizeof(s[5]), ui_tr_in(g, TX_OUT_OUT_SLICE_WHY),
                 "PWM03", 5u, "PWM02", 4u);
        for (int i = 0; i < 6; ++i) {
            if (i < 5 && !wraps_inside(s[i], 350, 170)) {
                T_FAIL("board, language %d: \"%s\"", l, s[i]);
            }
            if (!wraps_inside(s[i], 300, 300)) {
                T_FAIL("grid, language %d: \"%s\"", l, s[i]);
            }
        }
    }
}

/*
 * Every key of the board, drawn from its shape and from the IOmcu's
 * drawing, asked about: the key stays on top of the card with an accent
 * edge, and the frame stays inside its canvas.
 */
TEST_CASE(the_tapped_key_stays_visible_on_every_built_in_board)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    for (int pass = 0; pass < 2; ++pass) {
        const uint8_t n = outbind_pin_count(BOARD);
        int asked = 0;
        for (uint8_t i = 0; i < n; ++i) {
            gfx_rect_t r;
            outbind_t b;
            outbind_init(&b);
            outbind_set_board(&b, BOARD);
            outbind_set_proto(&b, proto_named("DSHOT600"));
            if (!outbind_toggle(&b, i)) {
                continue;
            }
            outbind_set_proto(&b, proto_named("SERVO PWM"));
            visit(&b);
            picker_screen_set_prog_ports(0u, NULL);
            if (pass == 0) {
                (void)picker_screen_set_drawing(NULL, 0u, NULL);
            }
            frame();
            if (!picker_screen_button_rect(i, &r)) {
                continue;               /* a pin with no key */
            }
            tap_at(r);
            if (!pin_take_open_in(PIN_TAKE_BOARD)) {
                T_FAIL("pass %d: GP%u asked nothing", pass,
                       (unsigned)outbind_pins(BOARD)[i].gpio);
                continue;
            }
            ++asked;
            guard_canvas_t g;
            gfx_canvas_t c;
            guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
            picker_screen_invalidate();
            view()->render(&c, 0);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
            const gfx_color_t top = c.pixels[(size_t)r.y * c.stride
                                             + (size_t)(r.x + r.w / 2)];
            if (top != UI_ACCENT) {
                T_FAIL("pass %d: GP%u's key is not on top", pass,
                       (unsigned)outbind_pins(BOARD)[i].gpio);
            }
        }
        if (asked < 15) {
            T_FAIL("pass %d: %d keys asked", pass, asked);
        }
    }
}

/**
 * @brief A learned board of @p per_side pads a row, @p w x @p h in 0.01mm
 *        at 0.1in pitch from @p corner, GPIO n on pad (n + 7) mod pads + 1:
 *        GPIO 0 on pad 8.  Shown in the board view, nothing bound but
 *        GPIO @p gpio to DSHOT600, SERVO PWM picked.
 * @return false when the board or its shape is refused
 */
static bool learned_visit(uint16_t w, uint16_t h, uint8_t per_side,
                          uint8_t corner, uint8_t gpio, uint8_t *pins)
{
    const uint16_t id = 4300u;
    const unsigned pads = 2u * per_side;
    const unsigned n = (pads < 24u) ? pads : 24u;
    uint16_t cat[LINK_CAT_COUNT];
    memset(cat, 0, sizeof(cat));
    for (unsigned i = 0; i < n && i < LINK_CAT_COUNT; ++i) {
        cat[i] = LINK_CAT_OF(i, (i + 7u) % pads + 1u, LINK_PIN_FREE);
    }
    *pins = (uint8_t)n;
    outbind_forget_learned();
    if (!outbind_learn_board(id, cat)) {
        return false;
    }
    uint16_t sh[LINK_SH_COUNT];
    sh[LINK_SH_WIDTH_CMM]  = w;
    sh[LINK_SH_HEIGHT_CMM] = h;
    sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(corner, per_side);
    sh[LINK_SH_PITCH_CMM]  = 254u;
    sh[LINK_SH_INSET_CMM]  = 100u;
    if (!outbind_learn_shape(id, sh)) {
        return false;
    }
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, id);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    if (!outbind_toggle(&b, outbind_index_of(id, gpio))) {
        return false;
    }
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    view()->reset();
    picker_screen_set_prog_ports(0u, NULL);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    s_applied = 0;
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(&b);
    (void)picker_screen_set_drawing(NULL, 0u, NULL);
    view()->enter();
    outputs_screen_select(b.proto);
    view()->tick(0.016f);
    frame();
    return outputs_view_is_board();
}

/** @brief Whether @p a and @p b share a pixel. */
static bool meet(gfx_rect_t a, gfx_rect_t b)
{
    return gfx_rect_intersect(a, b, NULL);
}

/*
 * Every board the board view draws -- the RP2350-CAN from its drawing and
 * from its shape, and learned shapes from a 1mm to a 60mm outline, 10 and
 * 20 pads a row, from two corners -- asked about each key: the card stands
 * clear of the key, inside the body, and a tap on the key a second time
 * closes the card without writing.  Among them the 60 x 12mm board with
 * GPIO 0 on pad 8, whose key lay under MOVE PIN in the tallest board's box.
 */
TEST_CASE(the_card_and_its_buttons_clear_the_key_on_every_drawable_board)
{
    static const uint16_t heights[] = { 100u, 600u, 1200u, 2100u, 3000u,
                                        6000u };
    static const uint16_t widths[] = { 6000u, 12000u };
    static const uint8_t rows[] = { 10u, 20u };
    static const uint8_t corners[] = { LINK_SH_BOTTOM_LEFT,
                                       LINK_SH_TOP_RIGHT };
    int asked = 0;
    for (size_t hi = 0; hi < sizeof(heights) / sizeof(heights[0]); ++hi) {
        for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
            for (size_t ri = 0; ri < sizeof(rows) / sizeof(rows[0]); ++ri) {
                for (size_t ci = 0; ci < 2u; ++ci) {
                    uint8_t n = 0u;
                    for (uint8_t g = 0; g < 24u; ++g) {
                        if (!learned_visit(widths[wi], heights[hi], rows[ri],
                                           corners[ci], g, &n)) {
                            break;
                        }
                        if (g >= n) {
                            break;
                        }
                        gfx_rect_t k;
                        if (!picker_screen_button_rect(
                                outbind_index_of(4300u, g), &k)) {
                            continue;
                        }
                        tap_at(k);
                        if (!pin_take_open_in(PIN_TAKE_BOARD)) {
                            /* At 5px a mm keys overlap: the press went to
                             * the key drawn over this one. */
                            continue;
                        }
                        ++asked;
                        const gfx_rect_t r = pin_take_rect();
                        if (meet(k, pin_take_button(PIN_TAKE_BTN_MOVE))
                            || meet(k, pin_take_button(PIN_TAKE_BTN_CANCEL))
                            || meet(k, r) || r.y < 0
                            || r.y + r.h + 4 > PK_FOOT) {
                            T_FAIL("%ux%u, %u a row, corner %u: GPIO %u's "
                                   "key at (%d,%d) meets the card at "
                                   "(%d,%d)", widths[wi], heights[hi],
                                   rows[ri], corners[ci], g, k.x, k.y, r.x,
                                   r.y);
                        }
                        tap_at(k);          /* a press off the card */
                        CHECK(!pin_take_is_open());
                        CHECK_EQ(s_applied, 0);
                    }
                }
            }
        }
    }
    CHECK(asked > 500);

    /* The 60 x 12mm board, 20 pads a row: GPIO 0 on pad 8 has its key
     * inside the tallest board's box, and a second tap on it is a press
     * off the card, not MOVE PIN. */
    uint8_t n = 0u;
    CHECK(learned_visit(6000u, 1200u, 20u, LINK_SH_BOTTOM_LEFT, 0u, &n));
    gfx_rect_t k;
    CHECK(picker_screen_button_rect(outbind_index_of(4300u, 0u), &k));
    CHECK(meet(k, gfx_rect_make((SCREEN_W - 600) / 2, (PK_FOOT - 240) / 2,
                                600, 240)));
    tap_at(k);
    CHECK(pin_take_open_in(PIN_TAKE_BOARD));
    CHECK(!meet(k, pin_take_rect()));
    tap_at(k);
    CHECK(!pin_take_is_open());
    CHECK_EQ(s_applied, 0);
    outbind_forget_learned();
}

/*
 * The Dial on MOVE PIN when CANCEL is touched: the next card starts on
 * CANCEL again, through the router and the focus layer.
 */
TEST_CASE(each_card_starts_the_dial_on_cancel)
{
    const outbind_t b = bench();
    visit(&b);
    ui_router_init();
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    four_ports();
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(&b);
    CHECK(serve_drawing());
    ui_router_goto(SCREEN_OUTPUTS);
    outputs_screen_select(b.proto);
    ui_router_tick(0.016f);
    ui_focus_set_link(true, false);
    s_applied = 0;

    for (int round = 0; round < 3; ++round) {
        const gfx_rect_t k = key(7u);
        touch_event_t e = { TOUCH_EVENT_DOWN,
                            { 1u, (int16_t)(k.x + k.w / 2),
                              (int16_t)(UI_BAND_H + k.y + k.h / 2), 40 },
                            0u };
        ui_router_event(&e);
        e.type = TOUCH_EVENT_UP;
        ui_router_event(&e);
        ui_router_tick(0.016f);
        CHECK(pin_take_open_in(PIN_TAKE_BOARD));
        CHECK_EQ(ui_focus_count(), 2);
        CHECK_STR_EQ(ui_focus_label(ui_focus_index()), "CANCEL");
        CHECK(ui_focus_turn(1));
        CHECK_STR_EQ(ui_focus_label(ui_focus_index()), "MOVE PIN");
        /* CANCEL by touch, the Dial left on MOVE PIN. */
        const gfx_rect_t cn = pin_take_button(PIN_TAKE_BTN_CANCEL);
        touch_event_t c = { TOUCH_EVENT_DOWN,
                            { 1u, (int16_t)(cn.x + cn.w / 2),
                              (int16_t)(UI_BAND_H + cn.y + cn.h / 2), 40 },
                            0u };
        ui_router_event(&c);
        c.type = TOUCH_EVENT_UP;
        ui_router_event(&c);
        ui_router_tick(0.016f);
        CHECK(!pin_take_is_open());
        CHECK(ui_focus_count() > 2);
    }
    CHECK_EQ(s_applied, 0);
    ui_focus_set_link(false, false);
    ui_router_goto(SCREEN_OVERVIEW);
}

int main(void)
{
    RUN(a_tap_on_an_esc_pin_under_servo_pwm_asks_and_writes_nothing);
    RUN(move_pin_writes_once_with_the_pin_moved);
    RUN(cancel_home_and_a_tap_outside_write_nothing);
    RUN(the_card_names_the_last_pin_and_the_role_change);
    RUN(the_card_closes_when_the_binding_goes_unread);
    RUN(the_card_closes_on_a_board_change_a_pick_and_a_view_switch);
    RUN(move_pin_after_the_pin_moved_elsewhere_writes_nothing);
    RUN(a_refused_write_leaves_the_pin_with_its_holder);
    RUN(no_card_on_off_and_the_reason_names_the_holder);
    RUN(no_card_for_a_port_or_interface_pin_and_the_reason_says_where);
    RUN(no_card_when_the_taker_cannot_hold_the_pin_and_the_reason_says_why);
    RUN(no_card_on_a_port_entry);
    RUN(lost_touch_events_close_nothing_but_the_press);
    RUN(a_second_finger_takes_the_press_or_closes_the_card);
    RUN(stop_counts_while_the_card_is_open);
    RUN(the_dial_focus_lists_move_and_cancel);
    RUN(the_worst_case_card_fits_in_both_languages);
    RUN(every_take_reason_ends_left_of_the_board);
    RUN(the_tapped_key_stays_visible_on_every_built_in_board);
    RUN(the_card_and_its_buttons_clear_the_key_on_every_drawable_board);
    RUN(each_card_starts_the_dial_on_cancel);
    return test_summary("pin_take");
}
