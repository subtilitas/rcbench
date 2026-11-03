/**
 * @file test_picker_screen_table.c
 * @brief The pin picker's tables and fit: the pins in use, the pins table, every
 * state inside its canvas, the home tag and the reasons.
 *
 * Cases:
 * - the_in_use_table_lists_every_pin_of_the_fullest_binding
 * - an_interface_pin_is_held_named_in_use_and_takes_no_tap
 * - the_fullest_in_use_table_lists_every_line
 * - the_pins_table_names_each_pins_channel_and_pad
 * - every_state_renders_inside_its_canvas
 * - a_tall_board_is_drawn_to_scale_and_not_stretched
 * - queries_past_the_end_answer_nothing
 * - every_reason_ends_left_of_the_board
 * - the_home_tag_closes_the_pickers_list_first
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

/*
 * The tallest IN USE table a binding gives: OFF selected, so every bound pin
 * is another protocol's; the programming port listed, in 28px pin rows,
 * and then 4 of them on distinct pins, in 20px pin rows; and the 8 slots
 * spread over the 6 protocols that take one channel a pin.  Every pin is listed
 * once under its own protocol, and every line is inside the table and the
 * canvas, clear of every other line, key, marker and text area, and drawn
 * on -- with the IOmcu's drawing and with the board drawn from its
 * shape, in both languages.
 */
TEST_CASE(the_in_use_table_lists_every_pin_of_the_fullest_binding)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    static const struct { const char *proto; uint8_t gp[2]; unsigned n; }
    spread[6] = {
        { "SERVO PWM",      { 0, 1 }, 2u },
        { "MOTOR PWM",      { 2, 4 }, 2u },
        { "DSHOT300",       { 5, 0 }, 1u },
        { "DSHOT600",       { 6, 0 }, 1u },
        { "DSHOT300 BIDIR", { 7, 0 }, 1u },
        { "DSHOT600 BIDIR", { 14, 0 }, 1u },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int pass = 0; pass < 4; ++pass) {
            /* Passes 2 and 3: 4 ports, distinct pins, beside the binding. */
            static const uint8_t four[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u,
                                                             28u };
            const bool ports = pass >= 2;
            fresh();
            if ((pass & 1) != 0) { CHECK(serve_drawing()); }
            picker_screen_set_prog_ports(ports ? 4u : 1u, four);
            for (unsigned g = 0; g < 6u; ++g) {
                bind_more(spread[g].proto, spread[g].gp, spread[g].n, "OFF");
            }
            const outbind_t *b = picker_screen_binding();
            CHECK_EQ(b->proto, 0);
            CHECK_EQ(outbind_chosen_total(b), 8);
            CHECK_EQ(picker_screen_prog_listed(), 13u);

            guard_canvas_t g;
            gfx_canvas_t c;
            guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
            scr()->render(&c, 0);
            if (guard_canvas_touched(&g) != 0u) {
                T_FAIL("language %d, pass %d drew outside", lang, pass);
            }

            picker_use_t line[PICKER_USE_MAX + 1u];
            uint8_t lines = 0;
            while (lines <= PICKER_USE_MAX
                   && picker_screen_in_use(lines, &line[lines])) {
                ++lines;
            }
            /* The port's name, its pins, 6 names, 8 pins: under the
             * bound, nothing dropped. */
            CHECK_EQ(lines, ports ? 19 : 16);
            CHECK(lines < PICKER_USE_MAX);

            /* Every pin bound, and the port's, listed once in its group. */
            unsigned found = 0;
            uint8_t group = 0xFEu;
            for (uint8_t k = 0; k < lines; ++k) {
                if (line[k].kind == PICKER_USE_NAME) {
                    group = line[k].proto;
                    continue;
                }
                CHECK_EQ(line[k].proto, group);
                if (group == PICKER_USE_PROG) {
                    CHECK(line[k].gpio == 13u
                          || (ports && line[k].gpio >= 26u
                              && line[k].gpio <= 28u));
                    CHECK_EQ(line[k].channel, -1);
                } else {
                    CHECK_EQ(outbind_group_of(b, idx(line[k].gpio)), group);
                    CHECK(line[k].channel >= 0);
                }
                found |= 1u << line[k].gpio;
            }
            CHECK_EQ(found, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4)
                            | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 13)
                            | (1u << 14)
                            | (ports ? (1u << 26) | (1u << 27) | (1u << 28)
                                     : 0u));

            const gfx_rect_t area = picker_screen_area(PICKER_AREA_IN_USE);
            const gfx_rect_t other[3] = {
                picker_screen_area(PICKER_AREA_HEAD),
                picker_screen_area(PICKER_AREA_PINS),
                picker_screen_list_face(),
            };
            const outbind_board_t *bd = outbind_board(BOARD);
            for (uint8_t k = 0; k < lines; ++k) {
                const gfx_rect_t r = line[k].rect;
                CHECK_EQ(line[k].compact, ports);
                if (line[k].kind == PICKER_USE_PIN) {
                    CHECK_EQ(r.h, ports ? 20 : 28);
                }
                if (r.x < area.x || r.y < area.y
                    || r.x + r.w > area.x + area.w
                    || r.y + r.h > area.y + area.h
                    || r.x + r.w > SCREEN_W || r.y + r.h > SCREEN_H) {
                    T_FAIL("line %u at %d,%d %dx%d is outside the table", k,
                           r.x, r.y, r.w, r.h);
                }
                for (uint8_t o = (uint8_t)(k + 1u); o < lines; ++o) {
                    if (overlap(r, line[o].rect)) {
                        T_FAIL("lines %u and %u overlap", k, o);
                    }
                }
                for (int a = 0; a < 3; ++a) {
                    if (overlap(r, other[a])) {
                        T_FAIL("line %u covers text area %d", k, a);
                    }
                }
                for (uint8_t i = 0; i < bd->count; ++i) {
                    gfx_rect_t key;
                    if (picker_screen_button_rect(i, &key)
                        && overlap(r, key)) {
                        T_FAIL("line %u covers GP%u's key", k,
                               bd->pins[i].gpio);
                    }
                }
                for (uint8_t m = 0; m < outbind_pad_count(BOARD); ++m) {
                    gfx_rect_t mk;
                    if (picker_screen_marker_rect(m, &mk) && overlap(r, mk)) {
                        T_FAIL("line %u covers pad %u's marker", k,
                               outbind_pads(BOARD)[m].pad);
                    }
                }
                /* Drawn on: something other than the background in it. */
                unsigned ink = 0;
                for (int y = r.y; y < r.y + r.h; ++y) {
                    for (int x = r.x; x < r.x + r.w; ++x) {
                        ink += (c.pixels[(size_t)y * c.stride + (size_t)x]
                                != UI_BG) ? 1u : 0u;
                    }
                }
                if (ink == 0u) {
                    T_FAIL("line %u is not drawn", k);
                }
            }
            picker_screen_set_prog_ports(0u, NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);

    /* A table that fits in 28px pin rows keeps them, with the GPIO in the
     * head face. */
    fresh();
    static const uint8_t servo[2] = { 0, 1 };
    bind_more("SERVO PWM", servo, 2u, "DSHOT300");
    picker_use_t one;
    CHECK(picker_screen_in_use(1u, &one));
    CHECK_EQ(one.kind, PICKER_USE_PIN);
    CHECK(!one.compact);
    CHECK_EQ(one.rect.h, 28);
    CHECK(!picker_screen_in_use(3u, &one));
    CHECK(picker_screen_in_use(2u, NULL));
}

/* Every interface on: the sensor bus on Sensor SDA @p sda, the PD mini on
 * TX 19 and RX 18, the phase tap on GP22. */
static void interfaces_on(int sda)
{
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, (float)sda);
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 19.0f);
    settings_set(SET_PDMINI_RX, 18.0f);
    settings_set(SET_TONE_EN, 1.0f);
}

/*
 * What SENSORS and PD MINI hold is a held key that takes no tap, and IN
 * USE names it after the programming port: each function's name, then its
 * pins.  The pins follow the settings: the bus moved frees its old pins,
 * which then bind.
 */
TEST_CASE(an_interface_pin_is_held_named_in_use_and_takes_no_tap)
{
    char who[32];
    picker_use_t line;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        settings_set_store(NULL);
        settings_init();
        picker_screen_set_prog_ports(0u, NULL);
        fresh();
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_FREE);
        CHECK(!picker_screen_in_use(0u, NULL));

        interfaces_on(16);
        static const struct { uint8_t gp; ui_text_id_t name; } held[5] = {
            { 16u, TX_OUT_IFACE_SENSE }, { 17u, TX_OUT_IFACE_SENSE },
            { 18u, TX_OUT_IFACE_PDMINI }, { 19u, TX_OUT_IFACE_PDMINI },
            { 22u, TX_OUT_IFACE_TAP },
        };
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(picker_screen_pin(idx(held[i].gp), who, sizeof(who)),
                     PICKER_PIN_HELD);
            CHECK_STR_EQ(who, ui_tr(held[i].name));
            gfx_rect_t r;
            CHECK(picker_screen_button_rect(idx(held[i].gp), &r));
            tap_rect(r);
        }
        CHECK_EQ(s_applied, 0);
        CHECK_EQ(outbind_chosen(picker_screen_binding()), 0);
        CHECK_STR_EQ(reason(), "");

        /* IN USE: SENSOR BUS GP16 GP17, PD MINI GP18 GP19, PHASE TAP GP22. */
        static const struct { picker_use_kind_t kind; unsigned group;
                              uint8_t gp; } want[8] = {
            { PICKER_USE_NAME, PICKER_USE_IFACE(OUTPUTS_IFACE_SENSE), 16u },
            { PICKER_USE_PIN,  PICKER_USE_IFACE(OUTPUTS_IFACE_SENSE), 16u },
            { PICKER_USE_PIN,  PICKER_USE_IFACE(OUTPUTS_IFACE_SENSE), 17u },
            { PICKER_USE_NAME, PICKER_USE_IFACE(OUTPUTS_IFACE_PDMINI), 18u },
            { PICKER_USE_PIN,  PICKER_USE_IFACE(OUTPUTS_IFACE_PDMINI), 18u },
            { PICKER_USE_PIN,  PICKER_USE_IFACE(OUTPUTS_IFACE_PDMINI), 19u },
            { PICKER_USE_NAME, PICKER_USE_IFACE(OUTPUTS_IFACE_TAP), 22u },
            { PICKER_USE_PIN,  PICKER_USE_IFACE(OUTPUTS_IFACE_TAP), 22u },
        };
        for (uint8_t k = 0; k < 8u; ++k) {
            CHECK(picker_screen_in_use(k, &line));
            CHECK_EQ(line.kind, want[k].kind);
            CHECK_EQ(line.proto, want[k].group);
            CHECK_EQ(line.gpio, want[k].gp);
            CHECK_EQ(line.channel, -1);
            CHECK(!line.compact);
        }
        CHECK(!picker_screen_in_use(8u, NULL));

        /* After the programming port's lines. */
        picker_screen_set_prog_ports(1u, (const uint8_t[]){ 13u });
        CHECK(picker_screen_in_use(0u, &line));
        CHECK_EQ(line.proto, PICKER_USE_PROG);
        CHECK(picker_screen_in_use(2u, &line));
        CHECK_EQ(line.kind, PICKER_USE_NAME);
        CHECK_EQ(line.proto, PICKER_USE_IFACE(OUTPUTS_IFACE_SENSE));
        picker_screen_set_prog_ports(0u, NULL);

        /* Drawn, the names in the table in red. */
        static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
        guard_canvas_t g;
        gfx_canvas_t c;
        guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK(picker_screen_in_use(0u, &line));
        unsigned red = 0;
        for (int y = line.rect.y; y < line.rect.y + line.rect.h; ++y) {
            for (int x = line.rect.x; x < line.rect.x + line.rect.w; ++x) {
                red += (c.pixels[(size_t)y * c.stride + (size_t)x]
                        == UI_DANGER) ? 1u : 0u;
            }
        }
        CHECK(red > 0u);

        /* The bus moved to GP14: GP16 binds, GP14 is held.  The frame
         * drawn before is repainted without a touch on this screen. */
        settings_set(SET_SENSE_SDA, 14.0f);
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
        /* Unchanged, the frame is not drawn again. */
        c.pixels[0] = GFX_RGB(255, 0, 255);
        scr()->render(&c, 0);
        CHECK_EQ(c.pixels[0], GFX_RGB(255, 0, 255));
        /* A page read that frees the PD mini's pins repaints it too. */
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, -1, -1);
        scr()->render(&c, 0);
        CHECK(c.pixels[0] != GFX_RGB(255, 0, 255));
        CHECK_EQ(picker_screen_pin(idx(18u), NULL, 0), PICKER_PIN_FREE);
        /* So does one that moves a pin's owner and no pin: the PD mini
         * read on GP14 and GP15, where the sensor bus is set. */
        c.pixels[0] = GFX_RGB(255, 0, 255);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, true, -1, -1);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, true, 14, 15);
        CHECK_EQ(picker_screen_pin(idx(14u), who, sizeof(who)),
                 PICKER_PIN_HELD);
        CHECK_STR_EQ(who, TR(OUT_IFACE_PDMINI));
        scr()->render(&c, 0);
        CHECK(c.pixels[0] != GFX_RGB(255, 0, 255));
        outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, false, -1, -1);
        outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, false, -1, -1);
        CHECK_EQ(picker_screen_pin(idx(18u), NULL, 0), PICKER_PIN_HELD);
        CHECK_EQ(picker_screen_pin(idx(14u), who, sizeof(who)),
                 PICKER_PIN_HELD);
        CHECK_STR_EQ(who, TR(OUT_IFACE_SENSE));
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_FREE);
        gfx_rect_t r16;
        CHECK(picker_screen_button_rect(idx(16u), &r16));
        tap_rect(r16);
        CHECK_EQ(s_applied, 1);
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_TICKED);
        /* Set back onto GP16: the protocol keeps it, GP17 is held. */
        settings_set(SET_SENSE_SDA, 16.0f);
        CHECK_EQ(picker_screen_pin(idx(16u), NULL, 0), PICKER_PIN_TICKED);
        CHECK_EQ(picker_screen_pin(idx(17u), NULL, 0), PICKER_PIN_HELD);
        /* Every function off: nothing held, the table empty again. */
        settings_set(SET_INA228_EN, 0.0f);
        settings_set(SET_PDMINI_EN, 0.0f);
        settings_set(SET_TONE_EN, 0.0f);
        CHECK_EQ(picker_screen_pin(idx(17u), NULL, 0), PICKER_PIN_FREE);
        CHECK_EQ(picker_screen_pin(idx(22u), NULL, 0), PICKER_PIN_FREE);
        CHECK(!picker_screen_in_use(0u, NULL));
    }
    settings_init();
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The fullest table: 4 ports, 8 slots over 6 protocols and every
 * interface on lists 27 lines, which the 468px under the heading hold at
 * 16px.  Every line is listed, inside the table and clear of the others,
 * and none is left out; the ports and the interfaces come first.
 */
TEST_CASE(the_fullest_in_use_table_lists_every_line)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    static const uint8_t four[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    static const struct { const char *proto; uint8_t gp[2]; unsigned n; }
    spread[6] = {
        { "SERVO PWM",      { 0, 1 }, 2u },
        { "MOTOR PWM",      { 2, 4 }, 2u },
        { "DSHOT300",       { 5, 0 }, 1u },
        { "DSHOT600",       { 6, 0 }, 1u },
        { "DSHOT300 BIDIR", { 7, 0 }, 1u },
        { "DSHOT600 BIDIR", { 14, 0 }, 1u },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        settings_set_store(NULL);
        settings_init();
        fresh();
        picker_screen_set_prog_ports(4u, four);
        for (unsigned k = 0; k < 6u; ++k) {
            bind_more(spread[k].proto, spread[k].gp, spread[k].n, "OFF");
        }
        interfaces_on(16);

        picker_use_t line[PICKER_USE_MAX + 1u];
        uint8_t lines = 0;
        while (lines <= PICKER_USE_MAX
               && picker_screen_in_use(lines, &line[lines])) {
            ++lines;
        }
        if (lines < 2u) {
            T_FAIL("the table lists %u lines", lines);
            return;
        }
        const picker_use_t *last = &line[lines - 1u];
        CHECK(last->kind != PICKER_USE_MORE);
        /* 5 port lines, 8 interface lines, 14 protocol lines. */
        CHECK_EQ(lines, 27u);
        CHECK(lines <= PICKER_USE_MAX);
        CHECK_EQ(line[5].proto, PICKER_USE_IFACE(OUTPUTS_IFACE_SENSE));
        CHECK_EQ(line[12].proto, PICKER_USE_IFACE(OUTPUTS_IFACE_TAP));

        const gfx_rect_t area = picker_screen_area(PICKER_AREA_IN_USE);
        for (uint8_t k = 0; k < lines; ++k) {
            const gfx_rect_t r = line[k].rect;
            CHECK_EQ(r.h, 16);
            if (r.x < area.x || r.y < area.y || r.x + r.w > area.x + area.w
                || r.y + r.h > area.y + area.h) {
                T_FAIL("line %u at %d,%d %dx%d is outside the table", k,
                       r.x, r.y, r.w, r.h);
            }
            for (uint8_t o = (uint8_t)(k + 1u); o < lines; ++o) {
                if (overlap(r, line[o].rect)) {
                    T_FAIL("lines %u and %u overlap", k, o);
                }
            }
        }
        guard_canvas_t g;
        gfx_canvas_t c;
        guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        unsigned ink = 0;
        for (int y = last->rect.y; y < last->rect.y + last->rect.h; ++y) {
            for (int x = last->rect.x; x < last->rect.x + last->rect.w; ++x) {
                ink += (c.pixels[(size_t)y * c.stride + (size_t)x] != UI_BG)
                           ? 1u : 0u;
            }
        }
        CHECK(ink > 0u);
        picker_screen_set_prog_ports(0u, NULL);
    }
    settings_init();
    ui_text_set_language(UI_LANG_EN);
}

/* The PINS table: one line a pin of the protocol selected, in catalogue
 * order, with the channel the page gives it and the pad printed on the
 * board beside it. */
TEST_CASE(the_pins_table_names_each_pins_channel_and_pad)
{
    fresh();
    static const uint8_t servo[3] = { 2, 13, 27 };
    static const uint8_t dshot[2] = { 0, 1 };
    bind_more("SERVO PWM", servo, 3u, "SERVO PWM");
    bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
    render();
    const outbind_pin_t *pins = outbind_pins(BOARD);
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(picker_screen_binding(), regs);
    picker_row_t row;
    for (uint8_t k = 0; k < 3u; ++k) {
        CHECK(picker_screen_row(k, &row));
        CHECK_EQ(row.gpio, servo[k]);
        CHECK_EQ(row.index, idx(servo[k]));
        CHECK_EQ(row.pad, pins[idx(servo[k])].pad);
        int ch = -1;
        for (uint8_t sl = 0; sl < used; ++sl) {
            const uint16_t *r = &regs[(size_t)sl * LINK_OS_STRIDE];
            if (r[LINK_OS_PIN] == servo[k]) { ch = LINK_OS_FIRST(r[LINK_OS_RANGE]); }
        }
        CHECK_EQ(row.channel, ch);
    }
    CHECK_EQ(row.channel, 4);            /* GP27, after GP0, 1, 2 and 13 */
    CHECK(!picker_screen_row(3u, &row));
}

/* Every state the screen can be in, in both languages, drawn inside the
 * canvas it was given. */
TEST_CASE(every_state_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int state = 0; state < 7; ++state) {
            fresh();
            static const uint8_t servo[5] = { 0, 1, 2, 4, 5 };
            static const uint8_t dshot[3] = { 6, 7, 14 };
            switch (state) {
            case 1: bind_more("SERVO PWM", servo, 5u, "SERVO PWM");
                    bind_more("DSHOT300", dshot, 3u, "SERVO PWM"); break;
            case 2: picker_screen_set_prog_pin(13u); break;
            case 3: tap_rect(picker_screen_list_face()); break;
            case 4: picker_screen_follow(1u, false); break;
            case 5: bind_more("SERVO PWM", servo, 4u, "PPM"); break;
            case 6: pick("OFF"); break;
            default: break;
            }
            guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
            scr()->render(&c, 0);
            if (guard_canvas_touched(&g) != 0u) {
                T_FAIL("state %d, language %d drew outside", state, lang);
            }
            picker_screen_set_prog_pin(PICKER_NO_PIN);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Asked about what is not there, the queries answer nothing; a key held
 * down is drawn pressed and acts on nothing when the finger leaves it. */
TEST_CASE(a_tall_board_is_drawn_to_scale_and_not_stretched)
{
    int w = 0, h = 0;
    /* A 3:1 outline fits at full width. */
    picker_board_fit(6000u, 2000u, &w, &h);
    CHECK_EQ(w, 600);
    CHECK_EQ(h, 200);
    /* Exactly at the cap: 600 x 240. */
    picker_board_fit(5000u, 2000u, &w, &h);
    CHECK_EQ(w, 600);
    CHECK_EQ(h, 240);
    /* The Pico outline, 51.00 x 21.00mm: 247px tall at full width, so
     * 240 high and 582 wide. */
    picker_board_fit(5100u, 2100u, &w, &h);
    CHECK_EQ(w, 582);
    CHECK_EQ(h, 240);
    /* Taller than the cap: the width shrinks with the height. */
    picker_board_fit(5000u, 5000u, &w, &h);
    CHECK_EQ(w, 240);
    CHECK_EQ(h, 240);
    picker_board_fit(2100u, 5100u, &w, &h);
    CHECK_EQ(h, 240);
    CHECK_EQ(w, 98);
    /* The aspect holds to a pixel of rounding. */
    const long lhs = (long)w * 5100L, rhs = (long)h * 2100L;
    CHECK(lhs <= rhs && rhs - lhs < 5100L);
    /* A very tall outline still draws one pixel wide. */
    picker_board_fit(1u, 60000u, &w, &h);
    CHECK_EQ(h, 240);
    CHECK_EQ(w, 1);
    /* No outline: the full box. */
    picker_board_fit(0u, 0u, &w, &h);
    CHECK_EQ(w, 600);
    CHECK_EQ(h, 240);
}

TEST_CASE(queries_past_the_end_answer_nothing)
{
    fresh();
    gfx_rect_t r;
    CHECK(!picker_screen_button_rect(outbind_pin_count(BOARD), &r));
    CHECK(!picker_screen_marker_rect(outbind_pad_count(BOARD), &r));
    picker_row_t row;
    CHECK(!picker_screen_row(200u, &row));
    picker_screen_count(NULL, 0);
    CHECK(!picker_screen_reason(NULL, 0));
    char buf[8] = "x";
    picker_screen_count(buf, 0);
    CHECK_STR_EQ(buf, "x");

    CHECK(picker_screen_button_rect(idx(5u), &r));
    const int x = r.x + r.w / 2, y = r.y + r.h / 2;
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t m = { TOUCH_EVENT_MOVE, { 0, (int16_t)x, (int16_t)(y + 1), 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, SCREEN_W / 2, SCREEN_H / 2, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&m);
    render();                                  /* the key drawn pressed */
    scr()->event(&u);
    CHECK_EQ(s_applied, 0);
}

/*
 * The home tag over the open list (ui_screen_t.back) closes it choosing
 * nothing and keeps the screen; with no list open the tag leaves.
 */
TEST_CASE(the_home_tag_closes_the_pickers_list_first)
{
    fresh();
    CHECK(!scr()->back());                       /* nothing open */
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
    CHECK(scr()->back());
    CHECK(!picker_screen_list_open());
    CHECK_EQ(s_applied, 0);
    /* A button acts again: the list no longer takes the tap. */
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(7u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK(!scr()->back());
}

/*
 * The reason under the protocol list: 35 cells of 10 x 20 under the
 * count, and what does not fit there on a second line of at most 17
 * cells, which ends at x 178, left of the widest board (x 212).  Every
 * reason the picker shows, in every language, the longest protocol name
 * and two-digit numbers in it.
 */
TEST_CASE(every_reason_ends_left_of_the_board)
{
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        char s[9][96];
        const ui_lang_t g = (ui_lang_t)l;
        snprintf(s[0], sizeof(s[0]), "%s", ui_tr_in(g, TX_PK_NOT_READ));
        snprintf(s[1], sizeof(s[1]), "%s", ui_tr_in(g, TX_OUT_OFF_VIEW));
        snprintf(s[2], sizeof(s[2]), ui_tr_in(g, TX_OUT_TAKES_PIN),
                 "DSHOT600 BIDIR", 1u);
        snprintf(s[3], sizeof(s[3]), ui_tr_in(g, TX_OUT_TAKES_PINS),
                 "DSHOT600 BIDIR", 8u);
        snprintf(s[4], sizeof(s[4]), ui_tr_in(g, TX_OUT_ALL_SLOTS), 8u);
        snprintf(s[5], sizeof(s[5]), ui_tr_in(g, TX_OUT_NEEDS_CHANNEL), 1u,
                 0u);
        snprintf(s[6], sizeof(s[6]), ui_tr_in(g, TX_OUT_NEEDS_CHANNELS), 16u,
                 15u);
        snprintf(s[7], sizeof(s[7]), ui_tr_in(g, TX_OUT_SLICE_WHY), 28u,
                 13u);
        snprintf(s[8], sizeof(s[8]), ui_tr_in(g, TX_OUT_COMPARE_WHY), 28u,
                 13u);
        for (int i = 0; i < 9; ++i) {
            const char *rest = "";
            if (gfx_text_cells(s[i]) > 35) {
                const size_t fit = gfx_text_prefix(s[i], 35);
                size_t cut = fit;
                while (cut > 0u && s[i][cut] != ' ') {
                    --cut;
                }
                rest = s[i] + ((cut > 0u) ? cut : fit);
                while (*rest == ' ') {
                    ++rest;
                }
            }
            if (gfx_text_cells(rest) > 17) {
                T_FAIL("language %d: \"%s\" leaves \"%s\"", l, s[i], rest);
            }
        }
    }
}

int main(void)
{
    RUN(the_in_use_table_lists_every_pin_of_the_fullest_binding);
    RUN(an_interface_pin_is_held_named_in_use_and_takes_no_tap);
    RUN(the_fullest_in_use_table_lists_every_line);
    RUN(the_pins_table_names_each_pins_channel_and_pad);
    RUN(every_state_renders_inside_its_canvas);
    RUN(a_tall_board_is_drawn_to_scale_and_not_stretched);
    RUN(queries_past_the_end_answer_nothing);
    RUN(every_reason_ends_left_of_the_board);
    RUN(the_home_tag_closes_the_pickers_list_first);
    return test_summary("picker_screen_table");
}
