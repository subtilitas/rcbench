/**
 * @file test_picker_layout.c
 * @brief PICK A PIN at the 1024 x 600 sizes: the keys are 50 x 56 px and the
 *        board with both tiers of them ends above the alert band; the PINS
 *        table holds a protocol's 8 pins there; nothing is drawn under the
 *        alert band in English or German, with no pin bound, at the
 *        fullest tables, with the list open beside 4 ports and with no
 *        board to draw; the keys clear the tables on a board with no end
 *        margin; 4 PROG PORT entries take a second column of 48 px rows.
 *
 * PICK A PIN drives no output and has no stop control: its safe_at is
 * NULL, so none lies under the alert band.
 *
 * Cases:
 * - every_key_is_the_same_finger_sized_key
 * - the_pins_table_holds_eight_pins_above_the_band
 * - nothing_is_drawn_under_the_alert_band
 * - keys_clear_the_tables_on_a_board_with_no_end_margin
 * - the_list_holds_four_ports_in_a_second_column
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

#include "outputs_screen.h"

/** @brief The eighth row of the PINS table: from body y 448, 36 px. */
#define PINS_ROW8_Y 448

/* The pixels from PK_FOOT down that are not the ground: what an alert
 * would cover. */
static unsigned drawn_under_the_band(void)
{
    render();
    unsigned n = 0;
    for (int y = PK_FOOT; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            n += (s_px[(size_t)y * SCREEN_W + (size_t)x] != UI_BG) ? 1u : 0u;
        }
    }
    return n;
}

/* SERVO PWM on @p n pins of the eight below, picked as the Outputs screen
 * brings a pick. */
static void servo_on(unsigned n)
{
    static const uint8_t gp[8] = { 0, 1, 2, 4, 5, 6, 7, 13 };
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (unsigned k = 0; k < n && k < 8u; ++k) {
        CHECK(outbind_toggle(&b, idx(gp[k])));
    }
    picker_screen_set_binding(&b);
    picker_screen_follow(b.proto, true);
}

/*
 * Every key, with the board from its shape and from the IOmcu's drawing:
 * 50 x 56 px, 56 px being UI_BTN_H_TALL; the ones of a tier on one row;
 * the lowest ending at body y 490, above PK_FOOT.
 */
TEST_CASE(every_key_is_the_same_finger_sized_key)
{
    CHECK(scr()->safe_at == NULL);
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        if (pass == 1) { CHECK(serve_drawing()); }
        const outbind_board_t *bd = outbind_board(BOARD);
        int lowest = 0, tops[4] = { -1, -1, -1, -1 };
        unsigned rows = 0;
        for (uint8_t i = 0; i < bd->count; ++i) {
            gfx_rect_t r;
            if (!picker_screen_button_rect(i, &r)) { continue; }
            CHECK_EQ(r.w, 50);
            CHECK_EQ(r.h, UI_BTN_H_TALL);
            lowest = (r.y + r.h > lowest) ? r.y + r.h : lowest;
            bool seen = false;
            for (unsigned k = 0; k < rows; ++k) {
                seen = seen || tops[k] == r.y;
            }
            if (!seen) {
                CHECK(rows < 4u);
                if (rows < 4u) { tops[rows++] = r.y; }
            }
        }
        CHECK_EQ(rows, 4u);                 /* two tiers above, two below */
        CHECK_EQ(lowest, 490);
        CHECK(lowest <= PK_FOOT);
    }
}

/*
 * A protocol's 8 pins: the PINS table lists every one, the eighth from
 * body y 448 in the accent, and nothing runs under the alert band.  With
 * none bound the table says so and nothing runs there either.
 */
TEST_CASE(the_pins_table_holds_eight_pins_above_the_band)
{
    fresh();
    servo_on(8u);
    picker_row_t row;
    CHECK(picker_screen_row(7u, &row));
    CHECK(!picker_screen_row(8u, &row));
    CHECK_EQ(drawn_under_the_band(), 0u);
    const gfx_rect_t a = picker_screen_area(PICKER_AREA_PINS);
    CHECK_EQ(a.y + a.h, PK_FOOT);
    unsigned accent = 0;
    for (int y = PINS_ROW8_Y; y < PINS_ROW8_Y + 36; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            accent += (s_px[(size_t)y * SCREEN_W + (size_t)x] == UI_ACCENT)
                          ? 1u : 0u;
        }
    }
    CHECK(accent > 0u);

    servo_on(0u);
    CHECK(!picker_screen_row(0u, &row));
    CHECK_EQ(drawn_under_the_band(), 0u);
}

/*
 * Nothing is drawn from PK_FOOT down, in English and German: no pin
 * bound; 8 pins; OFF with the port on GP13 and pins over five protocols;
 * the list open beside 4 ports, its rows shrunk to end above the band;
 * and a board the screen cannot draw, with its panel of prose.
 */
TEST_CASE(nothing_is_drawn_under_the_alert_band)
{
    static const uint8_t four[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);

        fresh();
        CHECK_EQ(drawn_under_the_band(), 0u);
        servo_on(8u);
        CHECK_EQ(drawn_under_the_band(), 0u);

        fresh();
        CHECK(serve_drawing());
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, BOARD);
        static const struct { const char *proto; uint8_t gp; } k[6] = {
            { "SERVO PWM", 0 },  { "MOTOR PWM", 2 }, { "DSHOT300", 4 },
            { "DSHOT600", 5 },   { "DSHOT300 BIDIR", 6 },
            { "DSHOT600 BIDIR", 7 },
        };
        for (unsigned i = 0; i < 6u; ++i) {
            outbind_set_proto(&b, proto_named(k[i].proto));
            CHECK(outbind_toggle(&b, idx(k[i].gp)));
        }
        outbind_set_proto(&b, 0u);
        picker_screen_set_binding(&b);
        picker_screen_set_prog_pin(13u);
        picker_screen_follow(0u, true);
        CHECK_EQ(drawn_under_the_band(), 0u);

        picker_screen_set_prog_ports(4u, four);
        outputs_screen_set_prog_ports(4u, four);
        CHECK_EQ(outputs_screen_port_entries(), 4u);
        tap_rect(picker_screen_list_face());
        CHECK(picker_screen_list_open());
        const gfx_rect_t last = picker_screen_list_row(
            (uint8_t)(OUTBIND_PROTOS + 3u));
        CHECK(last.y + last.h <= PK_FOOT);
        CHECK_EQ(drawn_under_the_band(), 0u);
        CHECK(scr()->back());
        picker_screen_set_prog_ports(0u, NULL);
        outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
        picker_screen_set_prog_pin(PICKER_NO_PIN);

        outbind_init(&b);
        outbind_set_board(&b, 4321u);        /* nothing knows this board */
        picker_screen_set_binding(&b);
        CHECK(!picker_screen_can_draw());
        CHECK_EQ(drawn_under_the_band(), 0u);
    }
    ui_text_set_language(UI_LANG_EN);
}

/** @brief A board that describes itself, as an IOmcu of another make
 *  would. */
#define LEARNED ((uint16_t)4242)

/*
 * A board that reports its own shape with no margin past its end pads:
 * 20 pads a row at 2.54 mm, an outline of 48.26 x 19.00 mm, so it draws
 * 600 px wide from x 212 and pad 1 sits on its left edge.  Pins on the
 * four corner pads.  Every key stays 8 px clear of the PINS and IN USE
 * tables and above the alert band, as on the boards this build knows.
 */
TEST_CASE(keys_clear_the_tables_on_a_board_with_no_end_margin)
{
    uint16_t cat[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { cat[i] = 0u; }
    cat[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    cat[1] = LINK_CAT_OF(1, 20, LINK_PIN_FREE);
    cat[2] = LINK_CAT_OF(2, 21, LINK_PIN_FREE);
    cat[3] = LINK_CAT_OF(3, 40, LINK_PIN_FREE);
    CHECK(outbind_learn_board(LEARNED, cat));
    uint16_t sh[LINK_SH_COUNT];
    sh[LINK_SH_WIDTH_CMM]  = 19u * 254u;
    sh[LINK_SH_HEIGHT_CMM] = 1900u;
    sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(LINK_SH_BOTTOM_LEFT, 20);
    sh[LINK_SH_PITCH_CMM]  = 254u;
    sh[LINK_SH_INSET_CMM]  = 100u;
    CHECK(outbind_learn_shape(LEARNED, sh));

    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, LEARNED);
        outbind_set_proto(&b, proto_named("SERVO PWM"));
        if (pass == 1) {              /* the PINS table full of this board */
            for (uint8_t i = 0; i < 4u; ++i) {
                CHECK(outbind_toggle(&b, i));
            }
        }
        picker_screen_set_binding(&b);
        picker_screen_follow(b.proto, true);
        CHECK(picker_screen_can_draw());
        const gfx_rect_t pins = picker_screen_area(PICKER_AREA_PINS);
        const gfx_rect_t use = picker_screen_area(PICKER_AREA_IN_USE);
        int lo = SCREEN_W, hi = 0;
        for (uint8_t i = 0; i < 4u; ++i) {
            gfx_rect_t r;
            CHECK(picker_screen_button_rect(i, &r));
            lo = (r.x < lo) ? r.x : lo;
            hi = (r.x + r.w > hi) ? r.x + r.w : hi;
            CHECK(r.y + r.h <= PK_FOOT);
            CHECK(r.x >= pins.x + pins.w + 8);
            CHECK(r.x + r.w + 8 <= use.x);
        }
        CHECK_EQ(lo, 187);                 /* pad 1 on the board's edge */
        CHECK_EQ(hi, 837);
        CHECK_EQ(drawn_under_the_band(), 0u);
    }
    outbind_forget_learned();
}

/*
 * With 4 PROG PORT entries (an IOmcu of protocol 4.26 or later) the list
 * holds the protocols in one column and the entries in a second, every row
 * UI_TAP_MIN tall, the rows apart and the list above the alert band, in
 * English and German; the last entry's bottom row picks it.
 */
TEST_CASE(the_list_holds_four_ports_in_a_second_column)
{
    static const uint8_t four[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        fresh();
        picker_screen_set_prog_ports(4u, four);
        outputs_screen_set_prog_ports(4u, four);
        tap_rect(picker_screen_list_face());
        CHECK(picker_screen_list_open());
        const uint8_t rows = (uint8_t)(OUTBIND_PROTOS + 4u);
        for (uint8_t i = 0; i < rows; ++i) {
            const gfx_rect_t r = picker_screen_list_row(i);
            CHECK(r.h >= UI_TAP_MIN);
            CHECK(r.y + r.h + 8 <= PK_FOOT);
            for (uint8_t o = (uint8_t)(i + 1u); o < rows; ++o) {
                gfx_rect_t both;
                CHECK(!gfx_rect_intersect(r, picker_screen_list_row(o),
                                          &both));
            }
        }
        /* The entries beside the protocols, from the same top. */
        CHECK_EQ(picker_screen_list_row(OUTBIND_PROTOS).y,
                 picker_screen_list_row(0).y);
        CHECK(picker_screen_list_row(OUTBIND_PROTOS).x
              > picker_screen_list_row(0).x);
        CHECK_EQ(drawn_under_the_band(), 0u);
        const gfx_rect_t last = picker_screen_list_row(
            (uint8_t)(OUTBIND_PROTOS + 3u));
        tap(last.x + 20, last.y + last.h - 1);
        CHECK_EQ(picker_screen_port_shown(), 3);
        picker_screen_set_prog_ports(0u, NULL);
        outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_key_is_the_same_finger_sized_key);
    RUN(the_pins_table_holds_eight_pins_above_the_band);
    RUN(nothing_is_drawn_under_the_alert_band);
    RUN(keys_clear_the_tables_on_a_board_with_no_end_margin);
    RUN(the_list_holds_four_ports_in_a_second_column);
    return test_summary("picker_layout");
}
