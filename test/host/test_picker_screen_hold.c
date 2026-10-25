/**
 * @file test_picker_screen_hold.c
 * @brief The pin picker's held pins and its OFF state: the programming ports' pins,
 * clear buttons, a full bench, PPM, a reserved pin, and what every visit opens
 * on.
 *
 * Cases:
 * - a_pwm_pin_the_coprocessor_would_refuse_is_held_and_named
 * - every_visit_opens_on_off_until_a_protocol_is_picked
 * - a_tap_on_every_key_on_off_writes_nothing
 * - a_board_that_answers_during_a_visit_keeps_the_picker_on_off
 * - off_draws_no_pins_table_and_no_pressed_key
 * - the_programming_ports_pin_is_held_and_named
 * - the_picker_holds_every_programming_ports_pin
 * - every_button_is_clear_of_everything_else
 * - a_full_bench_takes_no_ninth_pin_and_says_why
 * - ppm_greys_the_board_and_the_reason_says_why
 * - every_reserved_pin_is_named_and_has_no_button
 * - switching_the_protocol_keeps_the_pins_and_writes_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

static uint8_t s_selected = 0xFFu;

static int     s_selects;

static void on_select(uint8_t proto) { s_selected = proto; ++s_selects; }

/*
 * The programming port's pin is shown and not offered (audit M5).  The pin
 * the port drives is the pin the operator must see: a button that looked
 * free took GP13 from the port into a slot with nothing on the screen to say
 * the port had it.
 */
TEST_CASE(the_programming_ports_pin_is_held_and_named)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        char who[32];

        /* No port read: GP13 is a pin like the others. */
        CHECK_EQ(picker_screen_pin(idx(13u), who, sizeof(who)),
                 PICKER_PIN_FREE);
        CHECK_EQ(picker_screen_prog_listed(), PICKER_NO_PIN);

        picker_screen_set_prog_pin(13u);
        render();
        CHECK_EQ(picker_screen_pin(idx(13u), who, sizeof(who)),
                 PICKER_PIN_HELD);
        CHECK_STR_EQ(who, TR(OUT_PROG_PORT));
        CHECK_EQ(picker_screen_prog_listed(), 13u);
        /* Still drawn, so the lead's pad can be found, and a tap on it
         * changes nothing and writes nothing. */
        gfx_rect_t r;
        CHECK(picker_screen_button_rect(idx(13u), &r));
        const outbind_t before = *picker_screen_binding();
        tap_rect(r);
        CHECK_EQ(s_applied, 0);
        CHECK(memcmp(&before, picker_screen_binding(), sizeof(before)) == 0);
        /* The pin beside it is not the port's. */
        CHECK_EQ(picker_screen_pin(idx(14u), NULL, 0), PICKER_PIN_FREE);

        /* The screen reset: the pin is still the port's. */
        scr()->reset();
        picker_screen_set_apply(on_apply);
        {
            outbind_t b;
            outbind_init(&b);
            outbind_set_board(&b, BOARD);
            outbind_set_proto(&b, proto_named("SERVO PWM"));
            picker_screen_set_binding(&b);
        }
        CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_HELD);

        /* A port without a pin, or on a pin this board does not list. */
        picker_screen_set_prog_pin(PICKER_NO_PIN);
        CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_FREE);
        CHECK_EQ(picker_screen_prog_listed(), PICKER_NO_PIN);
        picker_screen_set_prog_pin(200u);
        CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_FREE);
        CHECK_EQ(picker_screen_prog_listed(), PICKER_NO_PIN);
        picker_screen_set_prog_pin(PICKER_NO_PIN);
        tap_rect(r);
        CHECK_EQ(s_applied, 1);
        CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(13u)),
                 proto_named("SERVO PWM"));

        /* A slot that holds the pin keeps it -- the IOmcu gives the
         * port no pin then -- and it can be unticked, after which the port
         * has it again. */
        picker_screen_set_prog_pin(13u);
        CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_TICKED);
        CHECK_EQ(picker_screen_prog_listed(), PICKER_NO_PIN);
        tap_rect(r);
        CHECK_EQ(s_applied, 2);
        CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 0);
        CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_HELD);
        CHECK_EQ(picker_screen_prog_listed(), 13u);
        tap_rect(r);
        CHECK_EQ(s_applied, 2);
        picker_screen_set_prog_pin(PICKER_NO_PIN);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Whether @p s ends in @p tail: "GPIO 1" at the end, not inside "GPIO 17". */
static bool ends_with(const char *s, const char *tail);

/*
 * Every programming port's pin is held as port 1's is, IN USE lists each
 * once under the one PROG PORT name, and a PWM pin is refused beside any of
 * them: GP27 on GP26's slice, GP28 on GP13's.  Pins past the count, past
 * LINK_PG_PORTS_MAX, or PICKER_NO_PIN are no port's.
 */
TEST_CASE(the_picker_holds_every_programming_ports_pin)
{
    static const uint8_t pins[LINK_PG_PORTS_MAX] = { 13u, 26u, PICKER_NO_PIN,
                                                     13u };
    char who[32];
    picker_use_t line;

    fresh();
    picker_screen_set_prog_ports(4u, pins);
    CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_HELD);
    CHECK_EQ(picker_screen_pin(idx(26u), who, sizeof(who)), PICKER_PIN_HELD);
    CHECK_STR_EQ(who, TR(OUT_PROG_PORT));
    CHECK_EQ(picker_screen_pin(idx(27u), who, sizeof(who)), PICKER_PIN_HELD);
    CHECK(ends_with(who, "GPIO 26"));
    CHECK_EQ(picker_screen_pin(idx(28u), who, sizeof(who)), PICKER_PIN_HELD);
    CHECK(ends_with(who, "GPIO 13"));
    CHECK_EQ(picker_screen_prog_listed(), 13u);
    CHECK(picker_screen_in_use(0u, &line));
    CHECK_EQ(line.kind, PICKER_USE_NAME);
    CHECK_EQ(line.proto, PICKER_USE_PROG);
    CHECK(picker_screen_in_use(1u, &line));
    CHECK_EQ(line.gpio, 13u);
    CHECK(picker_screen_in_use(2u, &line));
    CHECK_EQ(line.gpio, 26u);
    CHECK_EQ(line.proto, PICKER_USE_PROG);
    CHECK(!picker_screen_in_use(3u, &line));     /* GP13 once */

    /* A tap on the refused pin writes nothing and says why. */
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(27u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 0);
    CHECK(ends_with(reason(), "GPIO 26"));

    /* One port reported: GP26 and its slice are free again. */
    picker_screen_set_prog_ports(1u, pins);
    CHECK_EQ(picker_screen_pin(idx(26u), NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(picker_screen_pin(idx(27u), NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_HELD);
    /* More than the page holds: the first LINK_PG_PORTS_MAX. */
    picker_screen_set_prog_ports(200u, pins);
    CHECK_EQ(picker_screen_pin(idx(26u), NULL, 0), PICKER_PIN_HELD);
    /* None. */
    picker_screen_set_prog_ports(0u, pins);
    CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(picker_screen_prog_listed(), PICKER_NO_PIN);
    picker_screen_set_prog_ports(4u, NULL);
    CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(picker_screen_pin(idx(28u), NULL, 0), PICKER_PIN_FREE);
}

/* "GP1" at the end of @p s, not inside "GP17". */
static bool ends_with(const char *s, const char *tail)
{
    const size_t n = strlen(s), t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

/*
 * The picker meets the IOmcu's two PWM refusals as OUTPUTS does: a PWM
 * pin on the programming port's slice (GP28 beside GP13, slice 6), and one
 * on the compare register of a pin bound as PWM (GP16 beside GP0).  Each is
 * held with the pin that holds it named, a tap writes nothing, and the line
 * under the protocol list names both pins.  DShot takes either.
 */
TEST_CASE(a_pwm_pin_the_coprocessor_would_refuse_is_held_and_named)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char who[32];
        gfx_rect_t r;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        picker_screen_set_prog_pin(13u);
        CHECK_EQ(picker_screen_pin(idx(28u), who, sizeof(who)),
                 PICKER_PIN_HELD);
        CHECK(ends_with(who, "GPIO 13"));
        CHECK_STR_EQ(reason(), "");
        CHECK(picker_screen_button_rect(idx(28u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 0);
        CHECK(strncmp(reason(), "GPIO 28:", 8) == 0);
        CHECK(ends_with(reason(), "GPIO 13"));
        CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 0);
        pick("MOTOR PWM");
        CHECK_EQ(picker_screen_pin(idx(28u), NULL, 0), PICKER_PIN_HELD);
        pick("DSHOT600");
        CHECK_EQ(picker_screen_pin(idx(28u), NULL, 0), PICKER_PIN_FREE);
        CHECK_STR_EQ(reason(), "");
        picker_screen_set_prog_pin(PICKER_NO_PIN);

        fresh();
        static const uint8_t gp0[1] = { 0 };
        bind_more("SERVO PWM", gp0, 1u, "SERVO PWM");
        CHECK_EQ(picker_screen_pin(idx(16u), who, sizeof(who)),
                 PICKER_PIN_HELD);
        CHECK(ends_with(who, "GPIO 0"));
        CHECK_EQ(picker_screen_pin(idx(17u), NULL, 0), PICKER_PIN_FREE);
        CHECK(picker_screen_button_rect(idx(16u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 0);
        CHECK(strncmp(reason(), "GPIO 16:", 8) == 0);
        CHECK(ends_with(reason(), "GPIO 0"));
        /* A tick that lands ends the answer. */
        CHECK(picker_screen_button_rect(idx(17u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 1);
        CHECK_STR_EQ(reason(), "");
        /* And so does a change of board: the line keeps a catalogue index,
         * which names another pin, or none, on another board. */
        CHECK(picker_screen_button_rect(idx(16u), &r));
        tap_rect(r);
        CHECK(strncmp(reason(), "GPIO 16:", 8) == 0);
        {
            outbind_t b = *picker_screen_binding();
            outbind_t other = b;
            other.board = (uint16_t)(b.board + 1u);
            picker_screen_set_binding(&other);
            picker_screen_set_binding(&b);
            CHECK_STR_EQ(reason(), "");
        }
        pick("MOTOR PWM");
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_HELD);
        pick("DSHOT300");
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_FREE);
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * Every button against everything else on the screen, with the
 * IOmcu's drawing and with the board drawn from its shape (both 240px
 * tall): at least UI_TAP_MIN wide and tall, inside the screen and above
 * the alert band, centred on its own pad unless the screen edge stops it,
 * 8px or more clear of every other button beside it, clear of every
 * marker inside the board and of the text areas, and a tap on its centre
 * binds its own pin and no other.
 */
TEST_CASE(every_button_is_clear_of_everything_else)
{
    for (int pass = 0; pass < 2; ++pass) {
        fresh();
        if (pass == 1) { CHECK(serve_drawing()); }
        render();
        const outbind_board_t *bd = outbind_board(BOARD);
        gfx_rect_t btn[OUTBIND_PINS];
        bool has[OUTBIND_PINS];
        for (uint8_t i = 0; i < bd->count; ++i) {
            has[i] = picker_screen_button_rect(i, &btn[i]);
            CHECK_EQ(has[i], !bd->pins[i].reserved);
        }
        gfx_rect_t mk[LINK_PAD_SLOTS];
        bool mk_has[LINK_PAD_SLOTS];
        const uint8_t pads = outbind_pad_count(BOARD);
        unsigned markers = 0;
        for (uint8_t m = 0; m < pads; ++m) {
            mk_has[m] = picker_screen_marker_rect(m, &mk[m]);
            markers += mk_has[m] ? 1u : 0u;
        }
        CHECK_EQ(markers, 12u);      /* 8 grounds and 4 rails */
        for (uint8_t m = 0; m < pads; ++m) {
            for (uint8_t o = (uint8_t)(m + 1u); o < pads; ++o) {
                if (mk_has[m] && mk_has[o] && overlap(mk[m], mk[o])) {
                    T_FAIL("pass %d: markers of pads %u and %u overlap", pass,
                           outbind_pads(BOARD)[m].pad,
                           outbind_pads(BOARD)[o].pad);
                }
            }
        }
        const gfx_rect_t areas[3] = {
            picker_screen_area(PICKER_AREA_HEAD),
            picker_screen_area(PICKER_AREA_PINS),
            picker_screen_area(PICKER_AREA_IN_USE),
        };
        for (uint8_t i = 0; i < bd->count; ++i) {
            if (!has[i]) { continue; }
            const gfx_rect_t r = btn[i];
            const unsigned gp = bd->pins[i].gpio;
            if (r.x < 0 || r.y < 0 || r.x + r.w > SCREEN_W
                || r.y + r.h > PK_FOOT || r.w < UI_TAP_MIN
                || r.h < UI_TAP_MIN) {
                T_FAIL("pass %d: GPIO %u's button %d,%d %dx%d", pass, gp, r.x,
                       r.y, r.w, r.h);
            }
            int px = 0, py = 0;
            CHECK(pad_point_on(BOARD, (uint8_t)gp, &px, &py));
            const int mid = r.x + r.w / 2;
            if ((mid - px > 1 || px - mid > 1) && r.x > 2
                && r.x + r.w < SCREEN_W - 2) {
                T_FAIL("pass %d: GPIO %u's button is off its pad", pass, gp);
            }
            for (uint8_t o = (uint8_t)(i + 1u); o < bd->count; ++o) {
                if (has[o] && overlap(r, btn[o])) {
                    T_FAIL("pass %d: GPIO %u and GPIO %u overlap", pass, gp,
                           bd->pins[o].gpio);
                }
                /* Beside each other in a tier: 8px apart or more. */
                const gfx_rect_t q = btn[o];
                if (has[o] && r.y < q.y + q.h && q.y < r.y + r.h) {
                    const int apart = (q.x >= r.x) ? q.x - (r.x + r.w)
                                                   : r.x - (q.x + q.w);
                    if (apart < 8) {
                        T_FAIL("pass %d: GPIO %u and GPIO %u are %d px apart",
                               pass, gp, bd->pins[o].gpio, apart);
                    }
                }
            }
            for (uint8_t m = 0; m < pads; ++m) {
                if (mk_has[m] && overlap(r, mk[m])) {
                    T_FAIL("pass %d: GPIO %u covers pad %u's marker", pass, gp,
                           outbind_pads(BOARD)[m].pad);
                }
            }
            for (int a = 0; a < 3; ++a) {
                if (overlap(r, areas[a])) {
                    T_FAIL("pass %d: GPIO %u covers text area %d", pass, gp, a);
                }
            }
            if (overlap(r, picker_screen_list_face())) {
                T_FAIL("pass %d: GPIO %u covers the protocol list", pass, gp);
            }
        }
        /* And each centre binds its own pin, every one in turn. */
        for (uint8_t i = 0; i < bd->count; ++i) {
            if (!has[i]) { continue; }
            const int was = s_applied;
            tap_rect(btn[i]);
            if (s_applied != was + 1
                || outbind_group_of(&s_last, i) != proto_named("SERVO PWM")
                || outbind_chosen(&s_last) != 1u) {
                T_FAIL("pass %d: GPIO %u's centre bound something else", pass,
                       bd->pins[i].gpio);
            }
            tap_rect(btn[i]);                     /* and gives it back */
            CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 0);
        }
    }
}

/* Eight of eight: the count says so, the reason says why, every free pin is
 * grey and a tap on one writes nothing; a pin of the protocol selected can
 * still be given back, and that frees the rest. */
TEST_CASE(a_full_bench_takes_no_ninth_pin_and_says_why)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        static const uint8_t servo[5] = { 0, 1, 2, 4, 5 };
        static const uint8_t dshot[3] = { 6, 7, 14 };
        bind_more("SERVO PWM", servo, 5u, "SERVO PWM");
        bind_more("DSHOT300", dshot, 3u, "SERVO PWM");
        render();

        char want[64];
        snprintf(want, sizeof(want), TR(PK_SLOTS_OF), 8u, 8u);
        CHECK_STR_EQ(count(), want);
        snprintf(want, sizeof(want), TR(OUT_ALL_SLOTS), 8u);
        CHECK_STR_EQ(reason(), want);

        const outbind_board_t *bd = outbind_board(BOARD);
        for (uint8_t i = 0; i < bd->count; ++i) {
            const uint8_t g = outbind_group_of(picker_screen_binding(), i);
            const picker_pin_t st = picker_screen_pin(i, NULL, 0);
            /* GP16 to GP21 share a compare register with the PWM pins
             * GP0 to GP5: held by them before the count refuses them. */
            const bool partner = outbind_pwm_clash(picker_screen_binding(),
                                                   i, PICKER_NO_PIN, NULL)
                                 != OUTBIND_CLASH_NONE;
            const picker_pin_t want_st =
                bd->pins[i].reserved ? PICKER_PIN_RESERVED
                : (g == proto_named("SERVO PWM")) ? PICKER_PIN_TICKED
                : (g != 0u || partner) ? PICKER_PIN_HELD : PICKER_PIN_REFUSED;
            if (st != want_st) {
                T_FAIL("GPIO %u is %d, want %d", bd->pins[i].gpio, (int)st,
                       (int)want_st);
            }
        }

        gfx_rect_t r;
        CHECK(picker_screen_button_rect(idx(15u), &r));
        const outbind_t before = *picker_screen_binding();
        tap_rect(r);
        CHECK_EQ(s_applied, 0);
        CHECK(memcmp(&before, picker_screen_binding(), sizeof(before)) == 0);

        CHECK(picker_screen_button_rect(idx(5u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 1);
        snprintf(want, sizeof(want), TR(PK_SLOTS_OF), 7u, 8u);
        CHECK_STR_EQ(count(), want);
        CHECK_STR_EQ(reason(), "");
        CHECK_EQ(picker_screen_pin(idx(15u), NULL, 0), PICKER_PIN_FREE);
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * PPM carries eight channels on one pin.  With four servo pins bound it
 * cannot have one, so the whole board goes grey -- correct, and on its own
 * indistinguishable from a fault.  The reason line is what tells them apart.
 */
TEST_CASE(ppm_greys_the_board_and_the_reason_says_why)
{
    fresh();
    static const uint8_t servo[4] = { 0, 1, 2, 4 };
    bind_more("SERVO PWM", servo, 4u, "SERVO PWM");
    pick("PPM");
    render();
    char want[64];
    snprintf(want, sizeof(want), TR(OUT_NEEDS_CHANNELS), 8u, 4u);
    CHECK_STR_EQ(reason(), want);
    snprintf(want, sizeof(want), TR(PK_SLOTS_OF), 4u, 8u);
    CHECK_STR_EQ(count(), want);
    const outbind_board_t *bd = outbind_board(BOARD);
    unsigned grey = 0;
    for (uint8_t i = 0; i < bd->count; ++i) {
        const picker_pin_t st = picker_screen_pin(i, NULL, 0);
        if (st == PICKER_PIN_FREE || st == PICKER_PIN_TICKED) {
            T_FAIL("GPIO %u is offered under PPM", bd->pins[i].gpio);
        }
        grey += (st == PICKER_PIN_REFUSED) ? 1u : 0u;
    }
    CHECK_EQ(grey, (unsigned)(bd->count - 4u - 6u));   /* less 4 held, 6 reserved */
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(5u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 0);

    /* With the servo pins given back, PPM takes one pin, and then says it
     * has all it takes. */
    fresh();
    pick("PPM");
    CHECK_STR_EQ(reason(), "");
    CHECK(picker_screen_button_rect(idx(5u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(5u)),
             proto_named("PPM"));
    snprintf(want, sizeof(want), TR(OUT_TAKES_PIN), "PPM", 1u);
    CHECK_STR_EQ(reason(), want);
    CHECK_EQ(picker_screen_pin(idx(6u), NULL, 0), PICKER_PIN_REFUSED);
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_TICKED);
}

/* Every reserved pin: no button, its holder named, and no tap reaches it. */
TEST_CASE(every_reserved_pin_is_named_and_has_no_button)
{
    fresh();
    render();
    const outbind_board_t *bd = outbind_board(BOARD);
    unsigned reserved = 0;
    for (uint8_t i = 0; i < bd->count; ++i) {
        if (!bd->pins[i].reserved) { continue; }
        ++reserved;
        char who[32];
        gfx_rect_t r;
        CHECK_EQ(picker_screen_pin(i, who, sizeof(who)), PICKER_PIN_RESERVED);
        CHECK_STR_EQ(who, ui_pin_holder(bd->pins[i].held_by));
        CHECK(!picker_screen_button_rect(i, &r));
    }
    CHECK_EQ(reserved, 6u);             /* GP3 and GP8 to GP12 */
    CHECK_EQ(picker_screen_pin(bd->count, NULL, 0), PICKER_PIN_NONE);
}

/*
 * The protocol list on the picker: a pick changes which group a tap joins
 * and which table each pin is listed in, keeps every pin where it is, writes
 * nothing, and tells the outputs screen, so the two show one selection.
 */
TEST_CASE(switching_the_protocol_keeps_the_pins_and_writes_nothing)
{
    fresh();
    s_selects = 0;
    s_selected = 0xFFu;
    picker_screen_set_select(on_select);
    static const uint8_t servo[2] = { 0, 1 };
    static const uint8_t dshot[1] = { 6 };
    bind_more("SERVO PWM", servo, 2u, "SERVO PWM");
    bind_more("DSHOT300", dshot, 1u, "SERVO PWM");
    const outbind_t before = *picker_screen_binding();

    pick("DSHOT300");
    CHECK_EQ(s_applied, 0);
    CHECK_EQ(s_selects, 1);
    CHECK_EQ(s_selected, proto_named("DSHOT300"));
    CHECK_EQ(picker_screen_binding()->proto, proto_named("DSHOT300"));
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        CHECK_EQ(outbind_group_of(picker_screen_binding(), i),
                 outbind_group_of(&before, i));
    }
    picker_row_t row;
    CHECK(picker_screen_row(0, &row));
    CHECK_EQ(row.gpio, 6u);
    CHECK(!picker_screen_row(1, &row));
    char who[32];
    CHECK_EQ(picker_screen_pin(idx(0u), who, sizeof(who)), PICKER_PIN_HELD);
    CHECK_STR_EQ(who, "SERVO PWM");
    render();

    /* A tap now joins DSHOT300. */
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(7u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(7u)), proto_named("DSHOT300"));

    /* An open list is left by a tap anywhere else, choosing nothing, and
     * that tap does not reach the button under it. */
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
    tap_rect(r);
    CHECK(!picker_screen_list_open());
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(s_selects, 1);

    /* Wired to the outputs screen as the panel wires it: the pick lands
     * there, and following that screen keeps it. */
    outputs_screen()->reset();
    outputs_screen_set_binding(picker_screen_binding());
    picker_screen_set_select(outputs_screen_select);
    pick("MOTOR PWM");
    CHECK_EQ(outputs_screen_binding()->proto, proto_named("MOTOR PWM"));
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, proto_named("MOTOR PWM"));

    /* OFF edits nothing, and says so. */
    pick("OFF");
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_REFUSED);
    CHECK_EQ(picker_screen_pin(idx(7u), NULL, 0), PICKER_PIN_HELD);
    tap_rect(r);
    CHECK_EQ(s_applied, 1);

    /* The list works while the binding is not confirmed; the pins do not. */
    picker_screen_follow(proto_named("SERVO PWM"), false);
    pick("DSHOT300");
    CHECK_EQ(picker_screen_binding()->proto, proto_named("DSHOT300"));
    CHECK_STR_EQ(reason(), TR(PK_NOT_READ));
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_LOCKED);
    picker_screen_set_select(NULL);
}

/*
 * Every visit opens on the overview, as the Outputs screen's does: OFF on
 * both screens, so the next frame's follow does not put a protocol back.
 * A read-back keeps it, a leave keeps it, and only a pick ends it.
 */
TEST_CASE(every_visit_opens_on_off_until_a_protocol_is_picked)
{
    fresh();
    static const uint8_t dshot[2] = { 6, 7 };
    bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
    outputs_screen()->reset();
    outputs_screen_set_binding(picker_screen_binding());
    picker_screen_set_select(outputs_screen_select);

    pick("DSHOT300");
    scr()->leave();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(outputs_screen_binding()->proto, proto_named("DSHOT300"));

    /* The first frame, before any follow, already shows OFF. */
    scr()->enter();
    CHECK_EQ(picker_screen_binding()->proto, 0);
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_REFUSED);
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(5u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 0);

    /* A read-back naming the protocol the pins name, as one off the wire
     * does: still OFF. */
    outbind_t b = *outputs_screen_binding();
    outbind_set_proto(&b, outbind_wire_proto(&b));
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(outputs_screen_binding());
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, 0);
    tap_rect(r);
    CHECK_EQ(s_applied, 0);

    /* Left on OFF: OFF it stays. */
    scr()->leave();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_EQ(picker_screen_binding()->proto, 0);

    /* A pick here is what makes a key act. */
    scr()->enter();
    pick("DSHOT300");
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_FREE);
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(5u)), proto_named("DSHOT300"));
    picker_screen_set_select(NULL);
}

/*
 * On OFF no key acts: every button pressed, the bound ones included, and
 * nothing is applied and nothing is left unsaved.
 */
TEST_CASE(a_tap_on_every_key_on_off_writes_nothing)
{
    fresh();
    static const uint8_t dshot[2] = { 6, 7 };
    bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
    outputs_screen()->reset();
    outputs_screen_set_binding(picker_screen_binding());
    picker_screen_set_select(outputs_screen_select);
    scr()->enter();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    const outbind_t before = *picker_screen_binding();
    render();

    const outbind_board_t *bd = outbind_board(BOARD);
    unsigned pressed = 0, keys = 0;
    for (uint8_t i = 0; i < bd->count; ++i) {
        keys += bd->pins[i].reserved ? 0u : 1u;
        gfx_rect_t r;
        if (!picker_screen_button_rect(i, &r)) { continue; }
        tap_rect(r);
        picker_screen_follow(outputs_screen_binding()->proto,
                             outputs_screen_editable());
        ++pressed;
    }
    CHECK_EQ(pressed, keys);            /* every key, none skipped */
    CHECK_EQ(s_applied, 0);
    CHECK(!settings_dirty());
    for (uint8_t i = 0; i < bd->count; ++i) {
        if (outbind_group_of(picker_screen_binding(), i)
            != outbind_group_of(&before, i)) {
            T_FAIL("a tap on OFF moved GPIO %u", bd->pins[i].gpio);
        }
    }
    CHECK_EQ(picker_screen_binding()->proto, 0);

    pick("SERVO PWM");
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(5u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(5u)), proto_named("SERVO PWM"));
    picker_screen_set_select(NULL);
}

/*
 * A board that answers during a visit, on a panel that booted with the
 * IOmcu off, or one swapped for another: the picker stays on OFF.
 * It takes what the Outputs screen made of the reading, as the panel's
 * loop hands it, and follows its selection.
 */
TEST_CASE(a_board_that_answers_during_a_visit_keeps_the_picker_on_off)
{
    static const uint16_t k_before[2] = {
        OUTBIND_BOARD_UNKNOWN, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u),
    };
    for (unsigned n = 0; n < 2u; ++n) {
        fresh();
        static const uint8_t dshot[2] = { 6, 7 };
        bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
        bind_reading_t ok;
        {
            const outbind_t full = *picker_screen_binding();
            uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
            (void)outbind_to_slots(&full, slots);
            outbind_to_chan_cfg(&full, cc, 1000u, 2000u);
            CHECK_EQ(bind_link_classify(&ok, BOARD, slots, cc),
                     BIND_READ_OK);
        }

        outputs_screen()->reset();
        bind_reading_t none;
        (void)bind_link_classify(&none, k_before[n], NULL, NULL);
        outputs_screen_set_reading(&none);
        picker_screen_set_binding(outputs_screen_binding());
        picker_screen_set_select(outputs_screen_select);
        scr()->enter();
        /* A protocol picked before the board answers, on the Outputs
         * screen's list: it is not carried to the board. */
        outputs_screen_select(proto_named("DSHOT300"));
        picker_screen_follow(outputs_screen_binding()->proto,
                             outputs_screen_editable());
        CHECK_EQ(picker_screen_binding()->proto, proto_named("DSHOT300"));

        outputs_screen_set_reading(&ok);
        picker_screen_set_binding(outputs_screen_binding());
        picker_screen_follow(outputs_screen_binding()->proto,
                             outputs_screen_editable());
        CHECK(picker_screen_editable());
        CHECK_EQ(picker_screen_binding()->board, BOARD);
        CHECK_EQ(picker_screen_binding()->proto, 0);
        render();
        gfx_rect_t r;
        CHECK(picker_screen_button_rect(idx(5u), &r));
        tap_rect(r);
        CHECK(picker_screen_button_rect(idx(6u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 0);

        /* Picked here, then the IOmcu is swapped for another board
         * and back: OFF again, and a key writes nothing. */
        pick("SERVO PWM");
        outbind_t gone;
        outbind_init(&gone);
        outbind_set_board(&gone, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
        outputs_screen_set_binding(&gone);
        picker_screen_set_binding(outputs_screen_binding());
        outputs_screen_set_reading(&ok);
        picker_screen_set_binding(outputs_screen_binding());
        picker_screen_follow(outputs_screen_binding()->proto,
                             outputs_screen_editable());
        CHECK_EQ(picker_screen_binding()->proto, 0);
        render();
        CHECK(picker_screen_button_rect(idx(5u), &r));
        tap_rect(r);
        CHECK_EQ(s_applied, 0);
        picker_screen_set_select(NULL);
    }
}

/* Whether any pixel of @p ink lies in the PINS table left of the board. */
static bool pins_table_inked(gfx_color_t ink)
{
    for (int y = 128; y < SCREEN_H - 24; ++y) {
        for (int x = 6; x < 6 + 128; ++x) {
            if (s_px[y * SCREEN_W + x] == ink) {
                return true;
            }
        }
    }
    return false;
}

/*
 * OFF holds no pin, so the PINS table is not drawn, and a press on a key
 * draws no pressed face, as on the Outputs screen.  The slot count and the
 * IN USE table stay: they describe the overview.  A protocol picked brings
 * the table and the pressed face back.
 */
TEST_CASE(off_draws_no_pins_table_and_no_pressed_key)
{
    static gfx_color_t was[SCREEN_W * SCREEN_H];
    fresh();
    static const uint8_t dshot[2] = { 6, 7 };
    bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
    scr()->enter();                             /* OFF */
    picker_screen_invalidate();
    render();
    CHECK(!pins_table_inked(UI_ACCENT));
    CHECK(!pins_table_inked(UI_TEXT_FAINT));
    CHECK(strstr(count(), "OF 8") != NULL);
    memcpy(was, s_px, sizeof(was));

    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(5u), &r));
    touch_event_t d = { TOUCH_EVENT_DOWN,
                        { 0, (int16_t)(r.x + r.w / 2),
                          (int16_t)(r.y + r.h / 2), 40 }, 0u };
    touch_event_t u = d;
    u.type = TOUCH_EVENT_UP;
    scr()->event(&d);
    picker_screen_invalidate();
    render();
    CHECK(memcmp(was, s_px, sizeof(was)) == 0);
    scr()->event(&u);
    CHECK_EQ(s_applied, 0);

    picker_screen_follow(proto_named("SERVO PWM"), true);
    picker_screen_invalidate();
    render();
    CHECK(pins_table_inked(UI_ACCENT));
    memcpy(was, s_px, sizeof(was));
    scr()->event(&d);
    picker_screen_invalidate();
    render();
    CHECK(memcmp(was, s_px, sizeof(was)) != 0);
    scr()->event(&u);
    CHECK_EQ(s_applied, 1);
}

int main(void)
{
    RUN(a_pwm_pin_the_coprocessor_would_refuse_is_held_and_named);
    RUN(every_visit_opens_on_off_until_a_protocol_is_picked);
    RUN(a_tap_on_every_key_on_off_writes_nothing);
    RUN(a_board_that_answers_during_a_visit_keeps_the_picker_on_off);
    RUN(off_draws_no_pins_table_and_no_pressed_key);
    RUN(the_programming_ports_pin_is_held_and_named);
    RUN(the_picker_holds_every_programming_ports_pin);
    RUN(every_button_is_clear_of_everything_else);
    RUN(a_full_bench_takes_no_ninth_pin_and_says_why);
    RUN(ppm_greys_the_board_and_the_reason_says_why);
    RUN(every_reserved_pin_is_named_and_has_no_button);
    RUN(switching_the_protocol_keeps_the_pins_and_writes_nothing);
    return test_summary("picker_screen_hold");
}
