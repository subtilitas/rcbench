/**
 * @file test_outputs_layout.c
 * @brief OUTPUTS at the 1024 x 600 sizes: every control a finger tall and
 *        acting to its edges, nothing drawn under the alert band in English
 *        or German at the longest texts, the fullest list and the empty
 *        binding.
 *
 * OUTPUTS drives no output and has no stop control: its safe_at is NULL,
 * so none lies under the alert band.
 *
 * Cases:
 * - a_cell_takes_a_press_to_its_edges_and_none_between
 * - the_protocol_list_rows_are_a_finger_tall_and_end_above_the_band (with
 *   4 PROG PORT entries in a second column)
 * - the_prog_port_entry_controls_are_a_finger_tall
 * - nothing_is_drawn_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

/* A 4.28 IOmcu's ports, as test_outputs_screen_port.c poses them: port 1
 * SERVO PWM on @p pin0, the others NONE; FUNC with the DShot rates. */
static void ports(uint8_t count, uint8_t pin0)
{
    const uint8_t pins[4] = { pin0, 26u, OUTPUTS_NO_PIN, 27u };
    outputs_screen_set_prog_ports(count, pins);
    port_view_t v;
    port_view_unknown(&v);
    v.count     = count;
    v.has_func  = true;
    v.has_dshot = true;
    for (unsigned n = 0; n < 4u; ++n) {
        v.pin[n]  = pins[n];
        v.func[n] = (n == 0u) ? (uint8_t)LINK_PG_FN_SERVO_PWM
                              : (uint8_t)LINK_PG_FN_NONE;
    }
    port_view_set(&v);
}

/* The panel's start for the other suites: the count not known, no pin. */
static void ports_back(void)
{
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    settings_set_hold(NULL);
}

static bool hold_port(setting_id_t id)
{
    return id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM;
}

/* The pixels of the rows from OU_FOOT down that are not the ground: what
 * an alert would cover.  The canvas is the screen's body. */
static unsigned drawn_under_the_band(void)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    unsigned n = 0;
    for (int y = OU_FOOT; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            n += (c.pixels[(size_t)y * c.stride + (size_t)x] != UI_BG) ? 1u
                                                                       : 0u;
        }
    }
    return n;
}

/*
 * A cell is UI_TAP_MIN or more each way and acts on a press at its last
 * row and column; a press in the 6 px between two rows or the 8 px
 * between two columns ticks nothing.  The seventh row ends at OU_FOOT.
 */
TEST_CASE(a_cell_takes_a_press_to_its_edges_and_none_between)
{
    CHECK(OU_CELL_W >= UI_TAP_MIN);
    CHECK(OU_CELL_H >= UI_TAP_MIN);
    CHECK_EQ(OU_CELL_Y(OU_GRID_ROWS - 1) + OU_CELL_H, OU_FOOT);
    CHECK_EQ(OU_CELL_X(21) + OU_CELL_W, SCREEN_W - 8);
    CHECK(scr()->safe_at == NULL);

    fresh();
    choose_named("SERVO PWM");
    const uint8_t gp0 = idx(0u);
    /* The bottom right pixel of GP0's cell ticks it, the top left unticks
     * it. */
    tap(OU_CELL_X(gp0) + OU_CELL_W - 1, OU_CELL_Y(gp0) + OU_CELL_H - 1);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    tap(OU_CELL_X(gp0), OU_CELL_Y(gp0));
    CHECK_EQ(s_applied, 2);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    /* Between GP0 and GP1 under it, and between GP0 and GP7 beside it. */
    tap(OU_CELL_X(gp0) + OU_CELL_W / 2, OU_CELL_Y(gp0) + OU_CELL_H + 2);
    tap(OU_CELL_X(gp0) + OU_CELL_W + 3, OU_CELL_Y(gp0) + OU_CELL_H / 2);
    CHECK_EQ(s_applied, 2);
    /* The last row's bottom pixel: GP6. */
    const uint8_t gp6 = idx(6u);
    CHECK_EQ(gp6 % OU_GRID_ROWS, OU_GRID_ROWS - 1);
    tap(OU_CELL_X(gp6) + 20, OU_FOOT - 1);
    CHECK_EQ(s_applied, 3);
    CHECK_EQ(outputs_screen_cell(gp6, NULL, 0), OUTPUTS_CELL_TICKED);
}

/*
 * The protocol list with the 4 PROG PORT entries of an IOmcu of protocol
 * 4.26 or later: the protocols in a column of UI_TAP_MIN rows, the entries
 * in a second column beside it.  Every row picks its own entry on its top
 * and its bottom pixel row, the rows UI_TAP_MIN apart, and the list with
 * its shadow ends above the alert band, in English and German.
 */
TEST_CASE(the_protocol_list_rows_are_a_finger_tall_and_end_above_the_band)
{
    CHECK_EQ(OU_POP_ROW, UI_TAP_MIN);
    /* The protocols' column, its 4 px margins and its 4 px shadow. */
    CHECK(OU_DD_Y + 8 + (int)OUTBIND_PROTOS * OU_POP_ROW + 4 <= OU_FOOT);
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        fresh();
        ports(4u, 13u);
        CHECK_EQ(outputs_screen_port_entries(), 4u);
        for (int n = 0; n < 4; ++n) {
            const int top = OU_POP_ROW_Y(n) - OU_POP_ROW / 2;
            for (int edge = 0; edge < 2; ++edge) {
                choose_named("SERVO PWM");
                CHECK_EQ(outputs_screen_port_shown(), -1);
                tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
                CHECK_EQ(drawn_under_the_band(), 0u);
                tap(OU_POP_X(1), top + edge * (OU_POP_ROW - 1));
                CHECK_EQ(outputs_screen_port_shown(), n);
            }
        }
        /* The protocols' last row, DSHOT600 BIDIR, on its bottom row. */
        tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
        tap(OU_POP_X(0),
            OU_POP_ROW_Y((int)OUTBIND_PROTOS - 1) + OU_POP_ROW / 2 - 1);
        CHECK_EQ(outputs_screen_port_shown(), -1);
        CHECK_EQ((int)outputs_screen_binding()->proto,
                 proto_row("DSHOT600 BIDIR"));
        CHECK_EQ(s_applied, 0);
        ports_back();
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A PROG PORT entry: PIN, FUNCTION's face, its list's 5 rows and both
 * segment rows are UI_TAP_MIN tall; the FUNCTION list's last row picks on
 * its bottom pixel, which is above the alert band.
 */
TEST_CASE(the_prog_port_entry_controls_are_a_finger_tall)
{
    fresh();
    settings_set_store(NULL);
    settings_init();
    settings_set_hold(NULL);
    servo_screen_set_bench_armed(false);
    ports(1u, 13u);
    outputs_screen_open_on_port(0u);
    scr()->enter();
    CHECK_EQ(outputs_screen_port_shown(), 0);

    /* SERVO TESTER's segments take a press on their top and bottom
     * rows. */
    tap(OU_SEG_X(1), OU_TESTER_Y - UI_TAP_MIN / 2);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    tap(OU_SEG_X(0), OU_TESTER_Y + UI_TAP_MIN / 2 - 1);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);

    /* FUNCTION's face opens its list on its bottom row; the fifth row,
     * DSHOT600, takes a press on its bottom row, above OU_FOOT, and the
     * list shuts. */
    tap(OU_FN_FACE_X, OU_FN_FACE_Y + UI_TAP_MIN / 2 - 1);
    CHECK(scr()->back());                   /* it was open: the tag shuts it */
    tap(OU_FN_FACE_X, OU_FN_FACE_Y + UI_TAP_MIN / 2 - 1);
    const int last = OU_FN_ROW_Y(4) + UI_TAP_MIN / 2 - 1;
    CHECK(last < OU_FOOT);
    tap(OU_FN_FACE_X, last);
    CHECK(!scr()->back());                  /* the pick shut it */
    ports_back();
}

/*
 * Nothing is drawn from OU_FOOT down, in English and German: a protocol
 * with its pins, the binding the pages do not describe with UNBIND ALL
 * PINS, its reason and NO LINK on two lines; the list open with one and
 * with four PROG PORT entries; a PROG PORT entry with its FUNCTION list
 * open, and held by the servo programmer with its reason on three lines;
 * and the empty binding.
 */
TEST_CASE(nothing_is_drawn_under_the_alert_band)
{
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        settings_set_store(NULL);
        settings_init();
        settings_set_hold(NULL);
        servo_screen_set_bench_armed(false);

        fresh();                                     /* nothing bound */
        CHECK_EQ(drawn_under_the_band(), 0u);
        choose_named("SERVO PWM");
        for (unsigned g = 0; g < 8u; ++g) {
            const uint8_t gp[8] = { 0, 1, 2, 4, 5, 6, 7, 13 };
            int x, y;
            cell_centre(gp[g], &x, &y);
            tap(x, y);
        }
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 8);
        CHECK_EQ(drawn_under_the_band(), 0u);

        static bind_reading_t r;
        outbind_init(&r.bind);
        outbind_set_board(&r.bind, OUTBIND_BOARD_PICO_HEADER);
        r.state = BIND_READ_ODD;
        outputs_screen_set_reading(&r);
        outputs_screen_set_result(OUTPUTS_NO_LINK);
        CHECK_EQ(drawn_under_the_band(), 0u);

        for (uint8_t count = 0u; count <= 4u; ++count) {
            fresh();
            ports(count, 13u);
            tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);       /* the list */
            CHECK_EQ(drawn_under_the_band(), 0u);
            tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);       /* shut */
        }

        fresh();
        ports(1u, 13u);
        outputs_screen_set_port_watch(0u, "");
        outputs_screen_open_on_port(0u);
        scr()->enter();
        tap(OU_FN_FACE_X, OU_FN_FACE_Y);         /* FUNCTION's list */
        CHECK_EQ(drawn_under_the_band(), 0u);
        CHECK(scr()->back());

        settings_set_hold(hold_port);
        scr()->tick(0.0f);
        char why[128];
        CHECK(outputs_screen_port_reason(why, sizeof(why)));
        CHECK_STR_EQ(why, TR(OUT_PORT_HELD));
        CHECK_EQ(drawn_under_the_band(), 0u);
        ports_back();
    }
    ui_text_set_language(UI_LANG_EN);
    settings_init();
}

int main(void)
{
    RUN(a_cell_takes_a_press_to_its_edges_and_none_between);
    RUN(the_protocol_list_rows_are_a_finger_tall_and_end_above_the_band);
    RUN(the_prog_port_entry_controls_are_a_finger_tall);
    RUN(nothing_is_drawn_under_the_alert_band);
    return test_summary("outputs_layout");
}
