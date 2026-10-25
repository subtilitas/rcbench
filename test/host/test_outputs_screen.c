/**
 * @file test_outputs_screen.c
 * @brief The outputs screen: a protocol list that opens, and a pin grid that ticks.
 *
 * The screen owns no rules -- out_bind does -- so what is under test here is
 * the touching: that a press and a release on the same thing acts once, that
 * a release somewhere else acts not at all, that an open list can be left
 * without choosing, and that a refused pin does not reach the apply seam.
 * The last one matters most: the application writes the wire from that seam,
 * so a call it should not have received is a page write nobody asked for.
 *
 * Cases:
 * - a_pick_calls_the_apply_seam_for_no_entry
 * - the_protocol_list_opens_and_a_choice_closes_it
 * - an_open_list_can_be_left_without_choosing
 * - the_home_tag_closes_the_open_list_first
 * - a_release_away_from_the_press_does_nothing
 * - ticking_a_pin_applies_once_and_unticking_applies_again
 * - a_reserved_pin_never_reaches_the_apply_seam
 * - a_pin_too_many_does_not_apply
 * - the_programming_ports_pin_is_taken_and_takes_no_tick
 * - an_interface_pin_is_taken_named_and_takes_no_tick
 * - a_pwm_pin_on_the_programming_ports_slice_is_not_offered
 * - a_pwm_pin_on_a_bound_pins_compare_register_is_not_offered
 * - nothing_can_be_ticked_while_the_protocol_is_off
 * - a_cancelled_press_applies_nothing
 * - a_release_the_tracker_makes_for_a_jump_picks_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

/* Whether @p s ends in @p tail: "GPIO 1" at the end, not inside "GPIO 17". */
static bool ends_with(const char *s, const char *tail)
{
    const size_t n = strlen(s), t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

/* --------------------------------------------------------------- the list */

TEST_CASE(the_protocol_list_opens_and_a_choice_closes_it)
{
    fresh();
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    /* A pick is not a change of the binding: the seam is not called. */
    CHECK_EQ(s_applied, 0);

    /* And it is shut: a tap where a pin cell is now works as a pin again. */
    tap_pin(0);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, 1);
}

/*
 * A pick in the list writes nothing, whichever entry it is.  It changes
 * what the screen shows and which set the next tick joins; the pages a
 * binding renders to are the same before and after, and a write of them
 * would be 18 exchanges that can fail and change nothing.
 */
TEST_CASE(a_pick_calls_the_apply_seam_for_no_entry)
{
    fresh();
    uint16_t before[LINK_OS_COUNT], after[LINK_OS_COUNT];
    for (int round = 0; round < 2; ++round) {
        for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
            (void)outbind_to_slots(outputs_screen_binding(), before);
            choose_proto(p);
            CHECK_EQ((int)outputs_screen_binding()->proto, p);
            CHECK_EQ(s_applied, 0);
            (void)outbind_to_slots(outputs_screen_binding(), after);
            CHECK(memcmp(before, after, sizeof(before)) == 0);
        }
        if (round == 0) {
            /* With pins bound under three protocols as well. */
            outbind_t b;
            three_protocols(&b);
            outputs_screen_set_binding(&b);
        }
    }
    /* The same entry picked again, and a tick: one call. */
    choose_named("SERVO PWM");
    choose_named("SERVO PWM");
    CHECK_EQ(s_applied, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    tap_pin(4);
    CHECK_EQ(s_applied, 2);
}

TEST_CASE(an_open_list_can_be_left_without_choosing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open */
    tap(700, 400);                                      /* somewhere else */
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    CHECK_EQ(s_applied, was);

    /* The list is closed, so the same tap now reaches the grid under it. */
    tap_pin(0);
    CHECK_EQ(s_applied, was + 1);
}

/*
 * The home tag over the open list (ui_screen_t.back) closes it choosing
 * nothing and keeps the screen; with no list open the tag leaves.
 */
TEST_CASE(the_home_tag_closes_the_open_list_first)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;
    CHECK(!scr()->back());                              /* nothing open */
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open */
    CHECK(scr()->back());
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    CHECK_EQ(s_applied, was);
    tap_pin(0);                                         /* the grid again */
    CHECK_EQ(s_applied, was + 1);
    CHECK(!scr()->back());
}

/*
 * A cell press applies its binding change on the release, so a press left
 * latched after a lost event is a change waiting for any release that lands
 * on the cell.  The GT911 reuses track ids, so that release need not belong
 * to the same contact.
 */
TEST_CASE(a_cancelled_press_applies_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    int x, y;
    cell_centre(0, &x, &y);
    press_at(x, y);
    scr()->cancel();

    release_at(x, y);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);

    /* And nothing is stuck: a fresh press and release still applies. */
    press_at(x, y);
    release_at(x, y);
    CHECK_EQ(s_applied, was + 1);
}

TEST_CASE(a_release_away_from_the_press_does_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    int x, y;
    cell_centre(0, &x, &y);
    press_at(x, y);
    release_at(x + 400, y);          /* finger slid off the cell */
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);
}

/* A contact the tracker ends for a jump: its release is on the control it
 * pressed and the finger did not make it.  No pin is ticked, no protocol
 * picked and the list is not opened. */
TEST_CASE(a_release_the_tracker_makes_for_a_jump_picks_nothing)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;
    int x, y;
    cell_centre(0, &x, &y);
    feed_reset();
    feed_to_screen(scr());
    /* Off every control: the left column's foot. */
    const int off_x = COL_X + 100, off_y = 400 + UI_BAND_H;

    finger(FEED_LONE, x, y + UI_BAND_H);
    finger(FEED_LONE, off_x, off_y);
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);

    /* The protocol field: not opened, so a pin cell is still a pin cell. */
    finger(FEED_LONE, DD_X + DD_W / 2, DD_Y + DD_H / 2 + UI_BAND_H);
    finger(FEED_LONE, off_x, off_y);
    lift(FEED_LONE);
    feed_tap(FEED_LONE, x, y + UI_BAND_H);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, was + 1);

    /* A row of the open list: not picked. */
    feed_tap(FEED_LONE, DD_X + DD_W / 2, DD_Y + DD_H / 2 + UI_BAND_H);
    finger(FEED_LONE, DD_X + 40,
           DD_Y + 4 + POP_ROW / 2 + UI_BAND_H);         /* row 0: off */
    finger(FEED_LONE, DD_X + 40 + 2 * OU_POP_COL_W,
           DD_Y + 4 + POP_ROW / 2 + UI_BAND_H);            /* off the list */
    lift(FEED_LONE);
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("SERVO PWM"));
}

/* ---------------------------------------------------------------- the pins */

TEST_CASE(ticking_a_pin_applies_once_and_unticking_applies_again)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    tap_pin(4);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ(s_applied, was + 1);
    CHECK_EQ(outbind_chosen(&s_last), 1);

    tap_pin(4);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was + 2);
}

TEST_CASE(a_reserved_pin_never_reaches_the_apply_seam)
{
    fresh();
    choose_named("SERVO PWM");
    const int was = s_applied;

    /* The application writes the wire from that seam, so a call here would
     * be a page write for a pin the far end is going to refuse. */
    tap_pin(3);                      /* heartbeat */
    tap_pin(10);                     /* CAN SCK */
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, was);
}

TEST_CASE(the_programming_ports_pin_is_taken_and_takes_no_tick)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        const int was = s_applied;

        /* No port read: GP13 is a pin like the others. */
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_PROG_PORT));
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(s_applied, was);
        /* The pin beside it is not the port's. */
        CHECK_EQ(outputs_screen_cell(idx(14), NULL, 0), OUTPUTS_CELL_FREE);
        guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        /* The screen left and entered again: the pin is still the port's. */
        scr()->reset();
        outputs_screen_set_apply(on_apply);
        {
            outbind_t b;
            outbind_init(&b);
            outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
            outputs_screen_set_binding(&b);
        }
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0),
                 OUTPUTS_CELL_RESERVED);

        /* A port without a pin, or on a pin this board does not list. */
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(200u);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        choose_named("SERVO PWM");
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);

        /* A slot that holds the pin keeps it -- the IOmcu gives the
         * port no pin then -- and it can be unticked. */
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13), l, sizeof(l)),
                 OUTPUTS_CELL_TICKED);
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(13);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * What SETUP > INTERFACES holds is taken: the sensor bus on Sensor SDA and
 * the SCL after it while the INA228, the INA3221 or the AS5600 is on, the
 * PD mini's TX and RX while it is on, the tap pin while the phase tap is
 * on.  The cell names the function and takes no tick; the pins follow the
 * settings, and a pin set back to -1, a function switched off or a bus
 * moved frees the pin it held.  A protocol that holds the pin already
 * keeps it.
 */
TEST_CASE(an_interface_pin_is_taken_named_and_takes_no_tick)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        settings_set_store(NULL);
        settings_init();
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        const int was = s_applied;

        /* Nothing on: the default pins hold nothing. */
        for (uint8_t gp = 16u; gp <= 22u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), NULL, 0), OUTPUTS_CELL_FREE);
        }
        CHECK_EQ(outputs_iface_at(16u), OUTPUTS_IFACE_NONE);

        /* The INA228 on: GP16 and GP17, the default Sensor SDA and SCL. */
        settings_set(SET_INA228_EN, 1.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        for (uint8_t gp = 16u; gp <= 17u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), l, sizeof(l)),
                     OUTPUTS_CELL_RESERVED);
            CHECK_STR_EQ(l, TR(OUT_IFACE_SENSE));
            tap_pin(gp);
        }
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(s_applied, was);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        /* The INA3221 or the AS5600 alone holds the bus as well. */
        settings_set(SET_INA228_EN, 0.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_ENC_EN, 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_set(SET_ENC_EN, 0.0f);
        settings_set(SET_INA_FIT(0), 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);

        /* The PD mini on with RX GP18 and TX GP19. */
        settings_set(SET_PDMINI_TX, 19.0f);
        settings_set(SET_PDMINI_RX, 18.0f);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_PDMINI_EN, 1.0f);
        for (uint8_t gp = 18u; gp <= 19u; ++gp) {
            CHECK_EQ(outputs_screen_cell(idx(gp), l, sizeof(l)),
                     OUTPUTS_CELL_RESERVED);
            CHECK_STR_EQ(l, TR(OUT_IFACE_PDMINI));
            tap_pin(gp);
        }
        CHECK_EQ(s_applied, was);
        /* A pin at -1, or both on one pin: the panel writes the page OFF,
         * and neither pin is held. */
        settings_set(SET_PDMINI_TX, -1.0f);
        CHECK_EQ(outputs_screen_cell(idx(19u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_PDMINI_TX, 18.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI), 0ull);
        settings_set(SET_PDMINI_TX, 19.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI),
                 (1ull << 18) | (1ull << 19));

        /* The phase tap on its default GP22. */
        settings_set(SET_TONE_EN, 1.0f);
        CHECK_EQ(outputs_screen_cell(idx(22u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_TAP));

        /* Drawn, inside the canvas, every name in its cell. */
        guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);

        /* The bus moved to GP14: GP16 and GP17 free again, a tick on GP16
         * binds it, and GP14 and GP15 are the bus's.  The frame drawn
         * before is repainted without a touch on this screen. */
        settings_set(SET_SENSE_SDA, 14.0f);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), 15);
        for (int y = 0; y < SCREEN_H; y += 7) {
            for (int x = 0; x < SCREEN_W; x += 13) {
                c.pixels[(size_t)y * c.stride + (size_t)x] =
                    GFX_RGB(255, 0, 255);
            }
        }
        scr()->render(&c, 0);
        bool repainted = true;
        for (int y = 0; y < SCREEN_H; y += 7) {
            for (int x = 0; x < SCREEN_W; x += 13) {
                repainted = repainted
                            && c.pixels[(size_t)y * c.stride + (size_t)x]
                                   != GFX_RGB(255, 0, 255);
            }
        }
        CHECK(repainted);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(14u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_SENSE));
        CHECK_EQ(outputs_screen_cell(idx(15u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
        CHECK_EQ(s_applied, was + 1);

        /* A pin a protocol holds stays the protocol's when the bus is set
         * onto it -- the IOmcu refuses the bus then -- and is taken once
         * it is unticked. */
        settings_set(SET_SENSE_SDA, 16.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_TICKED);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        tap_pin(16u);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);

        /* No sensor bus: -1 frees both pins, whatever is on. */
        settings_set(SET_SENSE_SDA, -1.0f);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE), 0ull);

        /* A programming port's pin stays the port's. */
        settings_set(SET_TONE_PIN, 13.0f);
        outputs_screen_set_prog_pin(13u);
        CHECK_EQ(outputs_screen_cell(idx(13u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_PROG_PORT));
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        CHECK_EQ(outputs_screen_cell(idx(13u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_TAP));
        CHECK_EQ(outputs_screen_cell(idx(22u), NULL, 0), OUTPUTS_CELL_FREE);

        /* An ADC pin, which the TONE page refuses, is no tap pin: the
         * setting takes the default instead, and the ADC pin stays free.
         * A bus or a tap the IOmcu refused holds nothing until it holds it
         * again. */
        settings_set(SET_TONE_PIN, 26.0f);
        CHECK_EQ(settings_get_int(SET_TONE_PIN), 22);
        CHECK_EQ(outputs_screen_cell(idx(26u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_TONE_PIN, 21.0f);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_screen_set_unheld(0u, TONE_LINK_ROWS_TAP);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_screen_set_unheld(0u, 0u);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_set(SET_SENSE_SDA, 16.0f);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_screen_set_unheld(0u, 0u);
        CHECK_EQ(outputs_screen_cell(idx(17u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        /* Every part on refused (the INA3221 alone here): the page
         * enables none, and the bus holds nothing.  One part of two
         * refused leaves the bus held for the other. */
        sensors_rows_set_status((uint16_t)SENS_PART_REFUSED);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE), 0ull);
        CHECK_EQ(outputs_screen_cell(idx(16u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_set(SET_ENC_EN, 1.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        settings_screen_set_unheld(SENSE_LINK_ROW_ENC, 0u);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE), 0ull);
        settings_set(SET_INA228_EN, 1.0f);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        settings_screen_set_unheld(SENSE_LINK_ROW_ENC | SENSE_LINK_ROW_I228,
                                   0u);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE), 0ull);
        settings_screen_set_unheld(0u, 0u);
        sensors_rows_set_status(0u);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        settings_set(SET_INA228_EN, 0.0f);
        settings_set(SET_ENC_EN, 0.0f);
        settings_set(SET_SENSE_SDA, -1.0f);

        /* Read from the IOmcu, a page's pins stand over the settings: a
         * PD mini wiring the page refused holds none, or the pins the page
         * kept; unknown again, the settings' pins. */
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0),
                 OUTPUTS_CELL_RESERVED);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, -1, -1);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI), 0ull);
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(19u), NULL, 0), OUTPUTS_CELL_FREE);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, 20, 21);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI),
                 (1ull << 20) | (1ull << 21));
        CHECK_EQ(outputs_screen_cell(idx(20u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_PDMINI));
        CHECK_EQ(outputs_screen_cell(idx(18u), NULL, 0), OUTPUTS_CELL_FREE);
        /* Pins past the mask hold nothing. */
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, 64, 200);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI), 0ull);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, false, 20, 21);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_PDMINI),
                 (1ull << 18) | (1ull << 19));
        /* A sensor bus moved onto busy pins and refused: the page keeps
         * the old pair, which stays taken, the asked pair free. */
        settings_set(SET_INA228_EN, 1.0f);
        settings_set(SET_SENSE_SDA, 14.0f);
        settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, true, 16, 17);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_SENSE),
                 (1ull << 16) | (1ull << 17));
        CHECK_EQ(outputs_screen_cell(idx(14u), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(16u), l, sizeof(l)),
                 OUTPUTS_CELL_RESERVED);
        CHECK_STR_EQ(l, TR(OUT_IFACE_SENSE));
        settings_screen_set_unheld(0u, 0u);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, false, 16, 17);
        settings_set(SET_INA228_EN, 0.0f);
        settings_set(SET_SENSE_SDA, -1.0f);
        /* A tap relocation refused: the old pin stays the tap's. */
        settings_set(SET_TONE_PIN, 21.0f);
        settings_screen_set_unheld(0u, TONE_LINK_ROWS_TAP);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_TAP, true, 22, -1);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_TAP), 1ull << 22);
        CHECK_EQ(outputs_screen_cell(idx(21u), NULL, 0), OUTPUTS_CELL_FREE);
        settings_screen_set_unheld(0u, 0u);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_TAP, false, -1, -1);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_TAP), 1ull << 21);
        /* A page read that moves a pin's owner and no pin repaints the
         * frame: the PD mini read on GP16 and GP17, where the sensor bus
         * is set. */
        settings_set(SET_INA228_EN, 1.0f);
        settings_set(SET_SENSE_SDA, 16.0f);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, -1, -1);
        scr()->render(&c, 0);
        c.pixels[0] = GFX_RGB(255, 0, 255);
        scr()->render(&c, 0);
        CHECK_EQ(c.pixels[0], GFX_RGB(255, 0, 255));
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, true, -1, -1);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, 16, 17);
        CHECK_EQ(outputs_iface_at(16u), OUTPUTS_IFACE_PDMINI);
        scr()->render(&c, 0);
        CHECK(c.pixels[0] != GFX_RGB(255, 0, 255));
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, false, -1, -1);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, false, -1, -1);
        settings_set(SET_INA228_EN, 0.0f);
        settings_set(SET_SENSE_SDA, -1.0f);
        /* No function, or past the last, takes nothing. */
        outputs_screen_set_iface_held(OUTPUTS_IFACE_NONE, true, 1, 2);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_COUNT, true, 1, 2);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_NONE), 0ull);
        CHECK_EQ(outputs_iface_at(1u), OUTPUTS_IFACE_NONE);

        /* Out of range and none have no pins and no name. */
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_NONE), 0ull);
        CHECK_EQ(outputs_iface_pins(OUTPUTS_IFACE_COUNT), 0ull);
        CHECK_STR_EQ(outputs_iface_name(OUTPUTS_IFACE_NONE), "");
        CHECK_EQ(outputs_iface_at(200u), OUTPUTS_IFACE_NONE);
    }
    settings_init();
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A PWM pin on the programming port's slice is not offered.  The
 * IOmcu's prog_port_slots_check() refuses a PWM slot that shares a
 * slice with a port's pin, and GP28 is slice 6 with GP13.  The cell names
 * the port's pin, a tap writes nothing, and the line under the protocol
 * names both pins.  DShot runs on PIO, not on a slice, and takes the pin.
 */
TEST_CASE(a_pwm_pin_on_the_programming_ports_slice_is_not_offered)
{
    static const char *const pwm[2] = { "SERVO PWM", "MOTOR PWM" };
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (unsigned k = 0; k < 2u; ++k) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            outputs_screen_set_prog_pin(13u);
            choose_named(pwm[k]);
            const int was = s_applied;

            CHECK_EQ(outputs_screen_cell(idx(28), l, sizeof(l)),
                     OUTPUTS_CELL_HELD);
            CHECK(ends_with(l, "GPIO 13"));
            /* Slice 7 is not the port's. */
            CHECK_EQ(outputs_screen_cell(idx(14), NULL, 0),
                     OUTPUTS_CELL_FREE);
            /* The cell says it before a tap; the line answers the tap. */
            CHECK_STR_EQ(reason(), "");
            tap_pin(28);
            CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
            CHECK_EQ(s_applied, was);
            CHECK(strncmp(reason(), "GPIO 28:", 8) == 0);
            CHECK(ends_with(reason(), "GPIO 13"));
            /* Another pick ends the answer. */
            choose_named(pwm[k]);
            CHECK_STR_EQ(reason(), "");

            /* So does a change of board: the line keeps a catalogue index,
             * which names another pin, or none, on another board. */
            tap_pin(28);
            CHECK(strncmp(reason(), "GPIO 28:", 8) == 0);
            outbind_t b = *outputs_screen_binding();
            outbind_t other = b;
            other.board = (uint16_t)(b.board + 1u);
            outputs_screen_set_binding(&other);
            outputs_screen_set_binding(&b);
            CHECK_STR_EQ(reason(), "");
        }

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        outputs_screen_set_prog_pin(13u);
        choose_named("DSHOT600");
        CHECK_EQ(outputs_screen_cell(idx(28), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");
        tap_pin(28);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
        CHECK_EQ(s_applied, 1);

        /* No port pin: GP28 is a PWM pin like the others. */
        fresh();
        outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
        choose_named("SERVO PWM");
        CHECK_EQ(outputs_screen_cell(idx(28), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");
    }
    outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    ui_text_set_language(UI_LANG_EN);
}

/*
 * GP0 and GP16 are one compare register on two pads, and the IOmcu's
 * out_pwm_bind() refuses the second pin of the pair.  Beside a pin bound as
 * PWM in either PWM protocol, its partner is not offered to a PWM protocol;
 * the cell names the bound pin, and a refused tap names both under the
 * protocol.  Giving the bound pin back offers the partner again.
 */
TEST_CASE(a_pwm_pin_on_a_bound_pins_compare_register_is_not_offered)
{
    char l[32];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        choose_named("SERVO PWM");
        tap_pin(0);
        tap_pin(1);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
        const int was = s_applied;

        CHECK_EQ(outputs_screen_cell(idx(16), l, sizeof(l)),
                 OUTPUTS_CELL_HELD);
        CHECK(ends_with(l, "GPIO 0"));
        CHECK_EQ(outputs_screen_cell(idx(17), l, sizeof(l)),
                 OUTPUTS_CELL_HELD);
        CHECK(ends_with(l, "GPIO 1"));
        /* The other channel of the slice is a register of its own. */
        CHECK_EQ(outputs_screen_cell(idx(18), NULL, 0), OUTPUTS_CELL_FREE);

        /* The line names the pin tapped, not the lowest one refused. */
        CHECK_STR_EQ(reason(), "");
        tap_pin(17);
        CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
        CHECK_EQ(s_applied, was);
        CHECK(strncmp(reason(), "GPIO 17:", 8) == 0);
        CHECK(ends_with(reason(), "GPIO 1"));
        tap_pin(16);
        CHECK_EQ(s_applied, was);
        CHECK(strncmp(reason(), "GPIO 16:", 8) == 0);
        CHECK(ends_with(reason(), "GPIO 0"));
        /* A tick that lands ends it. */
        tap_pin(4);
        CHECK_EQ(s_applied, was + 1);
        CHECK_STR_EQ(reason(), "");

        /* The other PWM protocol meets the same register. */
        choose_named("MOTOR PWM");
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_HELD);
        tap_pin(16);
        CHECK_EQ(s_applied, was + 1);

        /* DShot does not drive a compare register. */
        choose_named("DSHOT300");
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "");

        choose_named("SERVO PWM");
        tap_pin(0);
        CHECK_EQ(outputs_screen_cell(idx(16), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_EQ(outputs_screen_cell(idx(17), NULL, 0), OUTPUTS_CELL_HELD);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_pin_too_many_does_not_apply)
{
    fresh();
    choose_named("PPM");             /* one pin */
    const int was = s_applied;

    tap_pin(0);
    CHECK_EQ(s_applied, was + 1);
    tap_pin(1);                      /* refused, and silently doing it would
                                      * write a page that drops the second */
    CHECK_EQ(s_applied, was + 1);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
}

TEST_CASE(nothing_can_be_ticked_while_the_protocol_is_off)
{
    fresh();
    tap_pin(0);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 0);
    CHECK_EQ(s_applied, 0);
}

int main(void)
{
    RUN(a_pick_calls_the_apply_seam_for_no_entry);
    RUN(the_protocol_list_opens_and_a_choice_closes_it);
    RUN(an_open_list_can_be_left_without_choosing);
    RUN(the_home_tag_closes_the_open_list_first);
    RUN(a_release_away_from_the_press_does_nothing);
    RUN(ticking_a_pin_applies_once_and_unticking_applies_again);
    RUN(a_reserved_pin_never_reaches_the_apply_seam);
    RUN(a_pin_too_many_does_not_apply);
    RUN(the_programming_ports_pin_is_taken_and_takes_no_tick);
    RUN(an_interface_pin_is_taken_named_and_takes_no_tick);
    RUN(a_pwm_pin_on_the_programming_ports_slice_is_not_offered);
    RUN(a_pwm_pin_on_a_bound_pins_compare_register_is_not_offered);
    RUN(nothing_can_be_ticked_while_the_protocol_is_off);
    RUN(a_cancelled_press_applies_nothing);
    RUN(a_release_the_tracker_makes_for_a_jump_picks_nothing);
    return test_summary("outputs_screen");
}
