/**
 * @file test_outputs_screen_carrier.c
 * @brief OUTPUTS on a board that solders its pins, the rbmod carrier.
 *
 * Cases:
 * - the_grid_is_pwm01_to_pwm04_named_by_their_role
 * - a_tap_gives_a_servo_output_the_protocol
 * - a_throttle_beside_a_surface_on_slice_2_is_refused_by_name
 * - a_prog_port_entry_names_its_pin_and_offers_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

#include "ui_board.h"

#define RBMOD ((uint16_t)OUTBIND_BOARD_RBMOD)

/* The carrier's binding with nothing bound, as a read hands it over. */
static void fresh_carrier(void)
{
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outputs_screen_set_binding(&b);
    ui_board_set(RBMOD);
}

/* Centre of the grid cell at position @p pos (PWM01 is 0). */
static void cell_at_pos(int pos, int *x, int *y)
{
    const int col = pos / GRID_ROWS, row = pos % GRID_ROWS;
    *x = GRID_X + col * (CELL_W + CELL_GAP) + CELL_W / 2;
    *y = GRID_Y + row * (CELL_H + ROW_GAP) + CELL_H / 2;
}

static void tap_pos(int pos)
{
    int x, y;
    cell_at_pos(pos, &x, &y);
    tap(x, y);
}

static uint8_t at(uint8_t gpio) { return outbind_index_of(RBMOD, gpio); }

/* The screen drawn into a canvas with guards: every pixel painted, none
 * outside it. */
static void draw_guarded(void)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
}

TEST_CASE(the_grid_is_pwm01_to_pwm04_named_by_their_role)
{
    fresh_carrier();
    char label[32];
    CHECK_EQ(outputs_screen_cell(at(2u), label, sizeof(label)),
             OUTPUTS_CELL_FREE);
    CHECK_STR_EQ(label, "GPIO 2");
    CHECK_EQ(outputs_screen_cell(at(1u), label, sizeof(label)),
             OUTPUTS_CELL_FREE);
    CHECK_STR_EQ(label, "GPIO 1");
    char name[8];
    CHECK(outputs_role_name(RBMOD, 2u, name, sizeof(name)));
    CHECK_STR_EQ(name, "PWM01");
    CHECK(outputs_role_name(RBMOD, 1u, name, sizeof(name)));
    CHECK_STR_EQ(name, "PWM04");
    CHECK(outputs_role_name(RBMOD, 15u, name, sizeof(name)));
    CHECK_STR_EQ(name, "PROGP03");
    CHECK(outputs_role_name(RBMOD, 0u, name, sizeof(name)));
    CHECK_STR_EQ(name, "PROGP04");
    CHECK(!outputs_role_name(RBMOD, 22u, name, sizeof(name)));
    CHECK_STR_EQ(name, "");
    CHECK(!outputs_role_name(OUTBIND_BOARD_PICO_HEADER, 2u, name,
                             sizeof(name)));
    CHECK(!outputs_role_name(RBMOD, 2u, NULL, 8u));
    /* Drawn whole, and nothing outside the canvas. */
    draw_guarded();
}

TEST_CASE(a_tap_gives_a_servo_output_the_protocol)
{
    fresh_carrier();
    choose_named("SERVO PWM");
    tap_pos(0);                      /* PWM01, GP2 */
    tap_pos(3);                      /* PWM04, GP1 */
    CHECK_EQ(s_applied, 2);
    const uint8_t g = (uint8_t)proto_row("SERVO PWM");
    CHECK_EQ(s_last.pins[g], ((uint32_t)1u << at(2u)) | ((uint32_t)1u << at(1u)));
    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&s_last, regs), 2u);
    CHECK_EQ(regs[LINK_OS_PIN], 2u);
    CHECK_EQ(regs[LINK_OS_STRIDE + LINK_OS_PIN], 1u);
    /* A tap past the fourth cell is no cell. */
    tap_pos(4);
    CHECK_EQ(s_applied, 2);
    /* The four outputs are all a protocol can have here. */
    tap_pos(1);
    tap_pos(2);
    CHECK_EQ(s_applied, 4);
    CHECK_STR_EQ(reason(), "SERVO PWM TAKES 4 PINS");
}

TEST_CASE(a_throttle_beside_a_surface_on_slice_2_is_refused_by_name)
{
    fresh_carrier();
    choose_named("SERVO PWM");
    tap_pos(1);                      /* PWM02, GP4 */
    CHECK_EQ(s_applied, 1);
    choose_named("MOTOR PWM");
    char label[32];
    CHECK_EQ(outputs_screen_cell(at(5u), label, sizeof(label)),
             OUTPUTS_CELL_HELD);
    CHECK_STR_EQ(label, "SLICE OF PWM02");
    tap_pos(2);                      /* PWM03, GP5 */
    CHECK_EQ(s_applied, 1);
    CHECK_STR_EQ(reason(), "PWM03 GPIO 5: SAME PWM SLICE AS PWM02 GPIO 4");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(),
                 "PWM03 GPIO 5: GLEICHER PWM-SLICE WIE PWM02 GPIO 4");
    ui_text_set_language(UI_LANG_EN);
    /* PWM01 is another slice: taken. */
    tap_pos(0);
    CHECK_EQ(s_applied, 2);
}

TEST_CASE(a_prog_port_entry_names_its_pin_and_offers_nothing)
{
    fresh_carrier();
    static const uint8_t pins[4] = { 13u, 14u, 15u, 0u };
    outputs_screen_set_prog_ports(4u, pins);
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    for (unsigned n = 0; n < 4u; ++n) {
        v.pin[n]  = pins[n];
        v.func[n] = (uint8_t)LINK_PG_FN_NONE;
    }
    port_view_set(&v);
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
    tap(OU_POP_X(1), OU_POP_ROW_Y(2));       /* PROG PORT 3 */
    CHECK_EQ(outputs_screen_port_shown(), 2);
    CHECK_STR_EQ(reason(),
                 "KST AND HITEC PROGRAMMERS ONLY. SIGNALS: PWM01 TO PWM04");
    /* PIN takes no tap: the screen stays. */
    tap(COL_X + 40, OU_PIN_Y);
    CHECK_EQ(outputs_screen_port_shown(), 2);
    draw_guarded();

    /* Back on the module the entry is the module's again. */
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    CHECK(strcmp(reason(),
                 "KST AND HITEC PROGRAMMERS ONLY. SIGNALS: PWM01 TO PWM04")
          != 0);
    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
}

int main(void)
{
    RUN(the_grid_is_pwm01_to_pwm04_named_by_their_role);
    RUN(a_tap_gives_a_servo_output_the_protocol);
    RUN(a_throttle_beside_a_surface_on_slice_2_is_refused_by_name);
    RUN(a_prog_port_entry_names_its_pin_and_offers_nothing);
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    return test_summary("outputs_screen_carrier");
}
