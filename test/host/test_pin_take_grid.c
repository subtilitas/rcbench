/**
 * @file test_pin_take_grid.c
 * @brief OUTPUTS' grid view and the rbmod carrier: a tap on a cell another
 *        protocol holds asks on a card beside the cell, MOVE PIN writes
 *        once, and the carrier's slice rule refuses with its reason.
 *
 * Cases:
 * - a_tap_on_a_held_cell_asks_beside_it
 * - the_card_never_covers_the_tapped_cell
 * - move_pin_on_the_grid_writes_once
 * - cancel_home_a_press_outside_and_a_failed_read_write_nothing
 * - the_carrier_moves_an_output_between_esc_and_servo
 * - the_carrier_refuses_a_move_that_breaks_slice_2
 * - the_grid_card_fits_in_both_languages
 * - a_refused_tap_on_the_grid_shows_its_own_reason
 * - a_second_tap_on_move_pin_binds_no_cell_under_the_card
 * - port_pwm_off_acts_inside_the_grid_cards_closed_box
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "outputs_screen_rig.h"

#include "pin_take.h"
#include "port_view.h"
#include "ui_board.h"

/* outputs_screen_int.h's, whose layout macros the rig's mirror: which
 * view of OUTPUTS is drawn. */
void outputs_screen_set_board_view(bool board);

#define PICO  ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define RBMOD ((uint16_t)OUTBIND_BOARD_RBMOD)

/** @brief The grid cell of catalogue entry @p i on the Pico header. */
static gfx_rect_t cell_of(uint8_t i)
{
    return gfx_rect_make(OU_CELL_X(i), OU_CELL_Y(i), CELL_W, CELL_H);
}

/** @brief A tap at the center of @p r. */
static void tap_at(gfx_rect_t r) { tap(r.x + r.w / 2, r.y + r.h / 2); }

/** @brief The card's line @p which, in a static buffer. */
static const char *line(pin_take_line_t which)
{
    static char buf[5][128];
    (void)pin_take_line(which, buf[which], sizeof(buf[which]));
    return buf[which];
}

/** @brief Nothing beside the binding: no port, no interface, read. */
static outbind_take_ctx_t bare_ctx(void)
{
    static uint64_t ifc[OUTPUTS_IFACE_COUNT];
    for (int f = 0; f < (int)OUTPUTS_IFACE_COUNT; ++f) {
        ifc[f] = outputs_iface_pins((outputs_iface_t)f);
    }
    return (outbind_take_ctx_t){
        .read = true, .ports = NULL, .pwm_ports = NULL, .port_count = 0u,
        .ifaces = ifc, .iface_count = OUTPUTS_IFACE_COUNT,
    };
}

/** @brief The grid on the Pico header with @p b read and @p proto picked. */
static void grid(const outbind_t *b, const char *proto)
{
    settings_set_store(NULL);
    settings_init();
    ui_text_set_language(UI_LANG_EN);
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
    fresh();
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    outputs_screen_set_binding(b);
    choose_named(proto);
    s_applied = 0;
}

/** @brief The bench of three protocols: GP0 DSHOT600 BIDIR, GP1 MOTOR PWM,
 *  GP2 and GP13 SERVO PWM. */
static outbind_t three(void)
{
    outbind_t b;
    three_protocols(&b);
    return b;
}

TEST_CASE(a_tap_on_a_held_cell_asks_beside_it)
{
    const outbind_t b = three();
    grid(&b, "SERVO PWM");
    tap_pin(1);
    CHECK(pin_take_open_in(PIN_TAKE_GRID));
    CHECK(scr()->covered());
    CHECK_EQ(s_applied, 0);
    CHECK_STR_EQ(reason(), "");
    /* GP1's cell is in column 0: the card over columns 2 and 3. */
    const gfx_rect_t r = pin_take_rect();
    CHECK_EQ(r.x, OU_GRID_X + 2 * (CELL_W + CELL_GAP));
    CHECK_EQ(r.w, 2 * CELL_W + CELL_GAP);
    CHECK_EQ(r.y, OU_GRID_Y);
    CHECK(r.y + r.h + 4 <= OU_FOOT);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_TITLE), "GPIO 1: MOTOR PWM");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "MOTOR PWM KEEPS NO PIN.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_ROLE),
                 "CH1 BECOMES A SERVO CHANNEL: IT CENTERS AT REST.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_LEAD),
                 "THE LEAD ON PAD 2 GETS SERVO PWM FROM THE NEXT ARM.");
    /* The buttons stack in the narrow card, CANCEL at the foot. */
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    const gfx_rect_t cn = pin_take_button(PIN_TAKE_BTN_CANCEL);
    CHECK(mv.y + mv.h <= cn.y);
    CHECK(mv.w >= 240 && cn.w >= 240);
    /* OFF asks nothing and says why. */
    tap_at(cn);
    choose_named("OFF");
    tap_pin(1);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "GPIO 1: MOTOR PWM. PICK A PROTOCOL FIRST");
}

TEST_CASE(the_card_never_covers_the_tapped_cell)
{
    const uint8_t n = outbind_cell_count(PICO);
    int asked = 0;
    for (uint8_t k = 0; k < n; ++k) {
        const uint8_t i = outbind_cell_index(PICO, k);
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, PICO);
        outbind_set_proto(&b, (uint8_t)proto_row("DSHOT600"));
        if (!outbind_toggle(&b, i)) {
            continue;                   /* reserved */
        }
        grid(&b, "SERVO PWM");
        tap_at(cell_of(i));
        if (!pin_take_open_in(PIN_TAKE_GRID)) {
            T_FAIL("cell %u asked nothing", (unsigned)i);
            continue;
        }
        ++asked;
        const gfx_rect_t r = pin_take_rect(), c = cell_of(i);
        if (r.x < c.x + c.w && c.x < r.x + r.w) {
            T_FAIL("the card covers cell %u", (unsigned)i);
        }
    }
    CHECK(asked > 15);
}

TEST_CASE(move_pin_on_the_grid_writes_once)
{
    const outbind_t b = three();
    grid(&b, "SERVO PWM");
    const outbind_take_ctx_t ctx = bare_ctx();
    outbind_t want;
    CHECK_EQ(outbind_take_check(outputs_screen_binding(), idx(0),
                                (uint8_t)proto_row("SERVO PWM"), &ctx, &want,
                                NULL),
             OUTBIND_TAKE_OK);
    tap_pin(0);
    tap_at(pin_take_button(PIN_TAKE_BTN_MOVE));
    CHECK_EQ(s_applied, 1);
    CHECK(memcmp(s_last.pins, want.pins, sizeof(want.pins)) == 0);
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(0)),
             proto_row("SERVO PWM"));
    CHECK(!pin_take_is_open());
    CHECK(!scr()->covered());
}

TEST_CASE(cancel_home_a_press_outside_and_a_failed_read_write_nothing)
{
    const outbind_t b = three();
    grid(&b, "SERVO PWM");
    tap_pin(1);
    tap_at(pin_take_button(PIN_TAKE_BTN_CANCEL));
    CHECK(!pin_take_is_open());
    tap_pin(1);
    CHECK(scr()->back());
    CHECK(!pin_take_is_open());
    /* A press on another cell closes the card and ticks nothing. */
    tap_pin(1);
    tap_pin(4);
    CHECK(!pin_take_is_open());
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(4)), 0);
    /* Touch events lost mid-press: the press goes, the card stays. */
    tap_pin(1);
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    press_at(mv.x + 10, mv.y + 10);
    scr()->cancel();
    release_at(mv.x + 10, mv.y + 10);
    CHECK(pin_take_open_in(PIN_TAKE_GRID));
    /* A read that failed closes it. */
    bind_reading_t r;
    r.bind = b;
    r.state = BIND_READ_NONE;
    outputs_screen_set_reading(&r);
    CHECK(!pin_take_is_open());
    tap_at(mv);
    /* The board view taking over closes it too. */
    grid(&b, "SERVO PWM");
    tap_pin(1);
    outputs_screen_set_board_view(true);
    CHECK(!pin_take_is_open());
    outputs_screen_set_board_view(false);
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(the_carrier_moves_an_output_between_esc_and_servo)
{
    /* SERVO PWM on PWM01 (GP2) and PWM02 (GP4), DSHOT300 on PWM04 (GP1). */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, (uint8_t)proto_row("SERVO PWM"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 2u)));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 4u)));
    outbind_set_proto(&b, (uint8_t)proto_row("DSHOT300"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 1u)));
    grid(&b, "DSHOT300");
    ui_board_set(RBMOD);
    /* PWM01, the first cell: the card over columns 2 and 3. */
    tap(OU_CELL_X(0) + 60, OU_CELL_Y(0) + 30);
    CHECK(pin_take_open_in(PIN_TAKE_GRID));
    CHECK(pin_take_rect().x > OU_CELL_X(0) + CELL_W);
    CHECK_STR_EQ(line(PIN_TAKE_LINE_TITLE), "GPIO 2: SERVO PWM");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_KEEPS), "SERVO PWM KEEPS 1 PIN.");
    CHECK_STR_EQ(line(PIN_TAKE_LINE_LEAD),
                 "THE LEAD ON PWM01 GETS DSHOT300 FROM THE NEXT ARM.");
    tap_at(pin_take_button(PIN_TAKE_BTN_MOVE));
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, outbind_index_of(RBMOD, 2u)),
             proto_row("DSHOT300"));
    CHECK_EQ(outbind_group_of(&s_last, outbind_index_of(RBMOD, 4u)),
             proto_row("SERVO PWM"));
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

TEST_CASE(the_carrier_refuses_a_move_that_breaks_slice_2)
{
    /* MOTOR PWM on PWM02 (GP4), DSHOT600 on PWM03 (GP5): SERVO PWM on
     * PWM03 would put a surface beside a throttle on slice 2. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, (uint8_t)proto_row("MOTOR PWM"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 4u)));
    outbind_set_proto(&b, (uint8_t)proto_row("DSHOT600"));
    CHECK(outbind_toggle(&b, outbind_index_of(RBMOD, 5u)));
    grid(&b, "SERVO PWM");
    ui_board_set(RBMOD);
    tap(OU_CELL_X(2) + 60, OU_CELL_Y(2) + 30);
    CHECK(!pin_take_is_open());
    CHECK_STR_EQ(reason(), "PWM03 GPIO 5: SAME PWM SLICE AS PWM02 GPIO 4");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(),
                 "PWM03 GPIO 5: GLEICHER PWM-SLICE WIE PWM02 GPIO 4");
    ui_text_set_language(UI_LANG_EN);
    /* MOTOR PWM may: a second throttle on the slice. */
    choose_named("MOTOR PWM");
    tap(OU_CELL_X(2) + 60, OU_CELL_Y(2) + 30);
    CHECK(pin_take_open_in(PIN_TAKE_GRID));
    CHECK_EQ(s_applied, 0);
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

/*
 * Every protocol losing GP28 to every other, keeping no pin or one, on the
 * narrow card beside the grid: the lines end above MOVE PIN without a cut
 * in both languages, and the frame stays inside its canvas.
 */
TEST_CASE(the_grid_card_fits_in_both_languages)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    int asked = 0;
    for (uint8_t loser = 1; loser < OUTBIND_PROTOS; ++loser) {
        for (uint8_t taker = 1; taker < OUTBIND_PROTOS; ++taker) {
            for (unsigned keeps = 0; keeps < 2u; ++keeps) {
                if (taker == loser
                    || (keeps > 0u
                        && (outbind_protos()[loser].max_pins <= keeps
                            || outbind_protos()[taker].channels > 1u))) {
                    continue;
                }
                outbind_t b;
                outbind_init(&b);
                outbind_set_board(&b, PICO);
                outbind_set_proto(&b, loser);
                CHECK(outbind_toggle(&b, idx(28)));
                if (keeps > 0u) {
                    CHECK(outbind_toggle(&b, idx(26)));
                }
                grid(&b, outbind_protos()[taker].name);
                tap_pin(28);
                if (!pin_take_open_in(PIN_TAKE_GRID)) {
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
                        T_FAIL("language %d, %s from %s", l,
                               outbind_protos()[taker].name,
                               outbind_protos()[loser].name);
                    }
                    guard_canvas_t g;
                    gfx_canvas_t c;
                    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
                    outputs_screen_invalidate();
                    scr()->render(&c, 0);
                    CHECK_EQ(guard_canvas_touched(&g), 0u);
                }
                ui_text_set_language(UI_LANG_EN);
            }
        }
    }
    CHECK(asked > 40);
}

/* The reason line names the tap made last: the sensor bus pin's line does
 * not stand over the reason of a free cell PPM cannot take. */
TEST_CASE(a_refused_tap_on_the_grid_shows_its_own_reason)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, PICO);
    outbind_set_proto(&b, (uint8_t)proto_row("PPM"));
    CHECK(outbind_toggle(&b, idx(4)));
    grid(&b, "PPM");
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    tap_pin(16);
    CHECK_STR_EQ(reason(), "GPIO 16: SENSOR BUS, SET ON SENSORS");
    tap_pin(6);
    CHECK_STR_EQ(reason(), "PPM USES 1 PIN");
    CHECK_EQ(s_applied, 0);
    settings_init();
}

/* A double tap on MOVE PIN: the first tap moves the pin and closes the
 * card, the second lands on the cell that was under the button and is
 * spent.  400ms later the cell takes a tap. */
TEST_CASE(a_second_tap_on_move_pin_binds_no_cell_under_the_card)
{
    const outbind_t b = three();
    grid(&b, "SERVO PWM");
    tap_pin(0);
    const gfx_rect_t mv = pin_take_button(PIN_TAKE_BTN_MOVE);
    tap(mv.x + 40, mv.y + mv.h / 2);
    CHECK_EQ(s_applied, 1);
    const outbind_t moved = *outputs_screen_binding();
    tap(mv.x + 40, mv.y + mv.h / 2);
    CHECK_EQ(s_applied, 1);
    CHECK(memcmp(outputs_screen_binding()->pins, moved.pins,
                 sizeof(moved.pins)) == 0);
    scr()->tick(0.39f);
    tap(mv.x + 40, mv.y + mv.h / 2);
    CHECK_EQ(s_applied, 1);
    scr()->tick(0.02f);
    tap(mv.x + 40, mv.y + mv.h / 2);
    CHECK_EQ(s_applied, 2);

    /* CANCEL starts the same wait; a cell off the card's box does not
     * wait. */
    grid(&b, "SERVO PWM");
    tap_pin(0);
    const gfx_rect_t cn = pin_take_button(PIN_TAKE_BTN_CANCEL);
    tap(cn.x + 40, cn.y + cn.h / 2);
    CHECK(!pin_take_is_open());
    tap(cn.x + 40, cn.y + cn.h / 2);
    CHECK_EQ(s_applied, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
}

/** @brief One touch event of contact 0 at (@p x, @p y). */
static void ev(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { type, { 0u, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&e);
}

/* PORT PWM OFF is a stop: its press acts inside the box of a card closed
 * less than 400ms ago, and the tap on ON beside it is spent.  The grid's
 * card lies over the cells and a pick of a port's entry ends the wait:
 * the case opens the card over the column with pin_take_tap() itself. */
TEST_CASE(port_pwm_off_acts_inside_the_grid_cards_closed_box)
{
    const outbind_t b = three();
    grid(&b, "SERVO PWM");
    const uint8_t pins[4] = { 13u, OUTPUTS_NO_PIN, OUTPUTS_NO_PIN,
                              OUTPUTS_NO_PIN };
    outputs_screen_set_prog_ports(1u, pins);
    port_view_t v;
    port_view_unknown(&v);
    v.count = 1u;
    v.has_func = true;
    v.pin[0] = 13u;
    v.func[0] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    port_view_set(&v);
    outputs_screen_select_port(0u);
    CHECK_EQ(outputs_screen_port_shown(), 0);
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);

    const outbind_take_ctx_t ctx = bare_ctx();
    const gfx_rect_t box = gfx_rect_make(0, 0, SCREEN_W, OU_FOOT);
    CHECK(pin_take_tap(PIN_TAKE_GRID, &b, idx(1), &ctx, box));
    CHECK(pin_take_open_in(PIN_TAKE_GRID));
    tap_at(pin_take_button(PIN_TAKE_BTN_CANCEL));
    CHECK(!pin_take_is_open());
    touch_event_t down = { TOUCH_EVENT_DOWN,
                           { 0u, (int16_t)OU_SEG_X(1), (int16_t)OU_PWM_Y,
                             40 }, 0u };
    CHECK(pin_take_spends(PIN_TAKE_GRID, &down));

    ev(TOUCH_EVENT_DOWN, OU_SEG_X(1), OU_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    ev(TOUCH_EVENT_UP, OU_SEG_X(1), OU_PWM_Y);
    /* ON starts the pulse and is no stop: its tap in the box is spent. */
    tap(OU_SEG_X(0), OU_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    scr()->tick(0.4f);
    tap(OU_SEG_X(0), OU_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK_EQ(s_applied, 0);

    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    settings_init();
}

int main(void)
{
    RUN(a_tap_on_a_held_cell_asks_beside_it);
    RUN(the_card_never_covers_the_tapped_cell);
    RUN(move_pin_on_the_grid_writes_once);
    RUN(cancel_home_a_press_outside_and_a_failed_read_write_nothing);
    RUN(the_carrier_moves_an_output_between_esc_and_servo);
    RUN(the_carrier_refuses_a_move_that_breaks_slice_2);
    RUN(the_grid_card_fits_in_both_languages);
    RUN(a_refused_tap_on_the_grid_shows_its_own_reason);
    RUN(a_second_tap_on_move_pin_binds_no_cell_under_the_card);
    RUN(port_pwm_off_acts_inside_the_grid_cards_closed_box);
    return test_summary("pin_take_grid");
}
