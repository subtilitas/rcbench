/**
 * @file test_picker_tags.c
 * @brief OUTPUTS' board view: the tag under a grey key's number that names
 * what holds the pin (picker_screen_pin_tag()), where it is drawn and how
 * the Dial names the key.
 *
 * Cases:
 * - every_held_key_names_its_holder_in_five_cells
 * - each_protocol_gives_its_role_tag
 * - each_port_gives_its_number_and_a_single_port_none
 * - each_interface_gives_its_tag
 * - a_free_ticked_refused_or_clashing_key_has_no_tag
 * - a_reserved_pin_has_no_key_and_no_tag
 * - the_overview_tags_every_bound_pin
 * - an_unread_binding_keeps_its_tags
 * - a_port_entry_tags_other_ports_protocols_and_interfaces
 * - the_tag_and_number_stay_inside_the_key_on_every_built_in_board
 * - the_tag_is_painted_in_both_languages
 * - the_focus_name_carries_the_tag
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "picker_screen_rig.h"
#include "port_view.h"
#include "text_mask.h"
#include "ui_focus.h"

/** @brief The tag of GP@p gpio, in a static buffer; "" for none. */
static const char *tag_of(uint8_t gpio)
{
    static char buf[8];
    (void)picker_screen_pin_tag(idx(gpio), buf, sizeof(buf));
    return buf;
}

/** @brief Whether GP@p gpio's key carries a tag. */
static bool tagged(uint8_t gpio)
{
    char buf[8];
    return picker_screen_pin_tag(idx(gpio), buf, sizeof(buf));
}

/** @brief Every interface on: the sensor bus on GP16 and GP17, the PD mini
 *  on TX 19 and RX 18, the phase tap on GP22. */
static void interfaces_on(void)
{
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 19.0f);
    settings_set(SET_PDMINI_RX, 18.0f);
    settings_set(SET_TONE_EN, 1.0f);
}

/** @brief The ports the IOmcu reports: @p count of them on 13, 26, 27, 28,
 *  each in its PIN register, to both views and the port entries. */
static void ports(uint8_t count)
{
    static const uint8_t pins[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    port_view_t v;
    port_view_unknown(&v);
    v.count = count;
    v.has_func = true;
    for (uint8_t n = 0; n < count; ++n) {
        v.pin[n] = pins[n];
        v.func[n] = (uint8_t)LINK_PG_FN_NONE;
    }
    port_view_set(&v);
    outputs_screen_set_prog_ports(count, pins);
    picker_screen_set_prog_ports(count, pins);
}

/** @brief No interface, no port, English; the picker on SERVO PWM. */
static void clean(void)
{
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    picker_screen_set_prog_ports(0u, NULL);
    outputs_screen_select(0u);
    fresh();
}

/** @brief The tag protocol @p name gives: its role word. */
static const char *role_of(const char *name)
{
    if (strcmp(name, "SERVO PWM") == 0) { return ui_tr(TX_PK_TAG_SERVO); }
    if (strcmp(name, "PPM") == 0)       { return ui_tr(TX_PK_TAG_PPM); }
    return ui_tr(TX_PK_TAG_ESC);
}

/* A grey key with a holder names it in at most 5 cells of 8 x 16, in both
 * languages, for every protocol, port and interface; a held key without
 * a tag is a PWM clash. */
TEST_CASE(every_held_key_names_its_holder_in_five_cells)
{
    const outbind_proto_t *k = outbind_protos();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (uint8_t p = 1; p < OUTBIND_PROTOS; ++p) {
            clean();
            ui_text_set_language((ui_lang_t)lang);
            interfaces_on();
            ports(4u);
            const uint8_t gp = 0u;
            bind_more(k[p].name, &gp, 1u, (p == 1u) ? "MOTOR PWM"
                                                    : "SERVO PWM");
            const uint8_t n = outbind_pin_count(BOARD);
            unsigned held = 0u, tags = 0u;
            for (uint8_t i = 0; i < n; ++i) {
                char who[32], tag[8];
                const picker_pin_t st = picker_screen_pin(i, who, sizeof(who));
                const bool has = picker_screen_pin_tag(i, tag, sizeof(tag));
                if (st != PICKER_PIN_HELD) {
                    CHECK(!has);
                    continue;
                }
                ++held;
                if (!has) {
                    continue;   /* a PWM clash */
                }
                ++tags;
                if (gfx_text_cells(tag) > 5) {
                    T_FAIL("language %d: \"%s\" is %d cells", lang, tag,
                           gfx_text_cells(tag));
                }
                CHECK(tag[0] != '\0');
            }
            /* GP0, 4 ports, 5 interface pins. */
            CHECK_EQ(tags, 10u);
            CHECK(held >= tags);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Each of the 7 protocols names its pin by its role in another protocol's
 * view: SERVO for SERVO PWM, PPM for PPM, ESC for MOTOR PWM and the four
 * DShot entries. */
TEST_CASE(each_protocol_gives_its_role_tag)
{
    const outbind_proto_t *k = outbind_protos();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (uint8_t p = 1; p < OUTBIND_PROTOS; ++p) {
            clean();
            ui_text_set_language((ui_lang_t)lang);
            const uint8_t gp = 7u;
            const char *other = (p == 1u) ? "MOTOR PWM" : "SERVO PWM";
            bind_more(k[p].name, &gp, 1u, other);
            CHECK_EQ(picker_screen_pin(idx(7u), NULL, 0), PICKER_PIN_HELD);
            if (strcmp(tag_of(7u), role_of(k[p].name)) != 0) {
                T_FAIL("language %d: %s tags \"%s\"", lang, k[p].name,
                       tag_of(7u));
            }
        }
    }
    CHECK_STR_EQ(role_of("DSHOT600 BIDIR"), "ESC");
    ui_text_set_language(UI_LANG_EN);
}

/* A bench with 4 ports numbers each port's pin, PORT1 to PORT4; a bench
 * with one names it PORT, with no number. */
TEST_CASE(each_port_gives_its_number_and_a_single_port_none)
{
    static const uint8_t gp[4] = { 13u, 26u, 27u, 28u };
    clean();
    ports(4u);
    for (uint8_t n = 0; n < 4u; ++n) {
        char want[8];
        snprintf(want, sizeof(want), "PORT%u", (unsigned)n + 1u);
        CHECK_EQ(picker_screen_pin(idx(gp[n]), NULL, 0), PICKER_PIN_HELD);
        CHECK_STR_EQ(tag_of(gp[n]), want);
    }
    clean();
    ports(1u);
    CHECK_STR_EQ(tag_of(13u), "PORT");
    CHECK(!tagged(26u));
    /* The port count not known yet: one port, PORT. */
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    CHECK_STR_EQ(tag_of(13u), "PORT");
}

/* SENSOR BUS is BUS, PD MINI is PD, PHASE TAP is TAP, in German PHASE. */
TEST_CASE(each_interface_gives_its_tag)
{
    static const struct { uint8_t gp; const char *en, *de; } want[5] = {
        { 16u, "BUS", "BUS" }, { 17u, "BUS", "BUS" },
        { 18u, "PD", "PD" },   { 19u, "PD", "PD" },
        { 22u, "TAP", "PHASE" },
    };
    for (int lang = 0; lang < 2; ++lang) {
        clean();
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(!tagged(16u));
        interfaces_on();
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(picker_screen_pin(idx(want[i].gp), NULL, 0),
                     PICKER_PIN_HELD);
            CHECK_STR_EQ(tag_of(want[i].gp), lang == 0 ? want[i].en
                                                       : want[i].de);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Grey without a tag means "can't add": a free key, a ticked one, a
 * refused one (SERVO PWM full) and one a PWM clash holds carry none. */
TEST_CASE(a_free_ticked_refused_or_clashing_key_has_no_tag)
{
    clean();
    CHECK_EQ(picker_screen_pin(idx(0u), NULL, 0), PICKER_PIN_FREE);
    CHECK(!tagged(0u));
    char buf[8] = "x";
    CHECK(!picker_screen_pin_tag(idx(0u), buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "");

    static const uint8_t eight[8] = { 0u, 1u, 2u, 4u, 5u, 6u, 7u, 14u };
    bind_more("SERVO PWM", eight, 8u, "SERVO PWM");
    CHECK_EQ(picker_screen_pin(idx(0u), NULL, 0), PICKER_PIN_TICKED);
    CHECK(!tagged(0u));
    CHECK_EQ(picker_screen_pin(idx(15u), NULL, 0), PICKER_PIN_REFUSED);
    CHECK(!tagged(15u));

    /* GP16 shares GP0's compare register: held for the clash, no tag. */
    clean();
    const uint8_t gp0 = 0u;
    bind_more("SERVO PWM", &gp0, 1u, "SERVO PWM");
    char who[32];
    CHECK_EQ(picker_screen_pin(idx(16u), who, sizeof(who)), PICKER_PIN_HELD);
    CHECK(who[0] != '\0');
    CHECK(!tagged(16u));

    /* No room, no tag and nothing written. */
    CHECK(!picker_screen_pin_tag(idx(0u), NULL, 8u));
    CHECK(!picker_screen_pin_tag(idx(0u), buf, 0u));
    CHECK(!picker_screen_pin_tag(0xFFu, buf, sizeof(buf)));
}

/* A pin the IOmcu reserves (GP3, the heartbeat) has no key and no tag. */
TEST_CASE(a_reserved_pin_has_no_key_and_no_tag)
{
    clean();
    CHECK_EQ(picker_screen_pin(idx(3u), NULL, 0), PICKER_PIN_RESERVED);
    CHECK(!picker_screen_button_rect(idx(3u), NULL));
    CHECK(!tagged(3u));
}

/* OFF selected: every bound pin is another protocol's and carries its
 * role. */
TEST_CASE(the_overview_tags_every_bound_pin)
{
    clean();
    static const uint8_t two[2] = { 0u, 1u };
    const uint8_t m = 2u, d = 5u;
    bind_more("SERVO PWM", two, 2u, "OFF");
    bind_more("MOTOR PWM", &m, 1u, "OFF");
    bind_more("DSHOT600 BIDIR", &d, 1u, "OFF");
    CHECK_EQ(picker_screen_binding()->proto, 0);
    CHECK_STR_EQ(tag_of(0u), "SERVO");
    CHECK_STR_EQ(tag_of(1u), "SERVO");
    CHECK_STR_EQ(tag_of(2u), "ESC");
    CHECK_STR_EQ(tag_of(5u), "ESC");
    CHECK(!tagged(4u));
}

/* A binding no read confirmed: the holders' keys keep their tags, the
 * others are locked without one. */
TEST_CASE(an_unread_binding_keeps_its_tags)
{
    clean();
    const uint8_t gp = 6u;
    bind_more("DSHOT300", &gp, 1u, "SERVO PWM");
    picker_screen_follow(proto_named("SERVO PWM"), false);
    CHECK(!picker_screen_editable());
    CHECK_STR_EQ(tag_of(6u), "ESC");
    CHECK_EQ(picker_screen_pin(idx(0u), NULL, 0), PICKER_PIN_LOCKED);
    CHECK(!tagged(0u));
    picker_screen_follow(proto_named("SERVO PWM"), true);
    CHECK_STR_EQ(tag_of(6u), "ESC");
}

/* On PROG PORT 1's entry: the other ports' pins, a protocol's and an
 * interface's are tagged; the port's own pin is ticked, without one. */
TEST_CASE(a_port_entry_tags_other_ports_protocols_and_interfaces)
{
    clean();
    interfaces_on();
    ports(4u);
    const uint8_t gp = 7u;
    bind_more("PPM", &gp, 1u, "SERVO PWM");
    outputs_screen_select_port(0u);
    picker_screen_follow(0u, true);
    CHECK_EQ(picker_screen_port_shown(), 0);
    CHECK_EQ(picker_screen_pin(idx(13u), NULL, 0), PICKER_PIN_TICKED);
    CHECK(!tagged(13u));
    CHECK_STR_EQ(tag_of(26u), "PORT2");
    CHECK_STR_EQ(tag_of(27u), "PORT3");
    CHECK_STR_EQ(tag_of(28u), "PORT4");
    CHECK_STR_EQ(tag_of(7u), "PPM");
    CHECK_STR_EQ(tag_of(16u), "BUS");
    CHECK_STR_EQ(tag_of(18u), "PD");
    CHECK_STR_EQ(tag_of(22u), "TAP");
    CHECK(!tagged(0u));

    /* PROG PORT 3's entry: port 1's pin is PORT1. */
    outputs_screen_select_port(2u);
    picker_screen_follow(0u, true);
    CHECK_EQ(picker_screen_port_shown(), 2);
    CHECK_STR_EQ(tag_of(13u), "PORT1");
    CHECK(!tagged(27u));
    outputs_screen_select(0u);
}

/**
 * @brief Checks every key of the board shown: a tagged key draws its number
 *        in 16 x 28 in its upper 34px and its tag in 8 x 16 below that,
 *        each on the key's face and clear of its edge and chamfer; every
 *        other key its number in 20 x 36.
 * @param what the pose, for a failure's message
 * @return the keys with a tag
 */
static unsigned keys_hold_their_text(const char *what)
{
    render();
    unsigned tags = 0u;
    const outbind_pin_t *pins = outbind_pins(picker_screen_binding()->board);
    const uint8_t n = outbind_pin_count(picker_screen_binding()->board);
    for (uint8_t i = 0; i < n; ++i) {
        gfx_rect_t r;
        if (!picker_screen_button_rect(i, &r)) {
            continue;
        }
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= SCREEN_W
              && r.y + r.h <= PK_FOOT);
        char num[6], tag[8];
        snprintf(num, sizeof(num), "%u", (unsigned)pins[i].gpio);
        const bool has = picker_screen_pin_tag(i, tag, sizeof(tag));
        if (!has) {
            if (!text_mask_found(s_px, SCREEN_W, SCREEN_H, r,
                                 &gfx_font_20x36, num)) {
                T_FAIL("%s: GP%s untagged is not in 20 x 36", what, num);
            }
            continue;
        }
        ++tags;
        const gfx_rect_t top = gfx_rect_make(r.x + 1, r.y + 1, r.w - 2, 34);
        const gfx_rect_t low = gfx_rect_make(r.x + 3, r.y + 33, r.w - 6,
                                             r.h - 35);
        if (!text_mask_found(s_px, SCREEN_W, SCREEN_H, top,
                             &gfx_font_16x28, num)) {
            T_FAIL("%s: GP%s's number is not in its key's top", what, num);
        }
        if (!text_mask_found(s_px, SCREEN_W, SCREEN_H, low, &gfx_font_8x16,
                             tag)) {
            T_FAIL("%s: GP%s's tag \"%s\" is not under its number", what,
                   num, tag);
        }
        CHECK(!text_mask_found(s_px, SCREEN_W, SCREEN_H, r, &gfx_font_20x36,
                               num));
    }
    return tags;
}

/* Every key of every board the view draws is 50 x 56px, and on each a
 * tagged key holds its number and its tag on its face: the RP2350-CAN
 * from its shape and from its drawing, and a learned board with a shape,
 * on OFF with every holder there is, 4 ports and every interface. */
TEST_CASE(the_tag_and_number_stay_inside_the_key_on_every_built_in_board)
{
    for (int pass = 0; pass < 2; ++pass) {
        clean();
        if (pass == 1) {
            CHECK(serve_drawing());
        }
        interfaces_on();
        ports(4u);
        static const uint8_t s2[2] = { 0u, 1u };
        const uint8_t m = 2u, d = 5u, b = 6u;
        bind_more("SERVO PWM", s2, 2u, "OFF");
        bind_more("MOTOR PWM", &m, 1u, "OFF");
        bind_more("DSHOT300", &d, 1u, "OFF");
        bind_more("DSHOT600 BIDIR", &b, 1u, "OFF");
        const uint8_t n = outbind_pin_count(BOARD);
        for (uint8_t i = 0; i < n; ++i) {
            gfx_rect_t r;
            if (picker_screen_button_rect(i, &r)) {
                CHECK_EQ(r.w, 50);
                CHECK_EQ(r.h, 56);
            }
        }
        /* 5 bound pins, 4 ports, 5 interface pins. */
        CHECK_EQ(keys_hold_their_text(pass ? "drawing" : "shape"), 14u);
        /* And the PPM pin. */
        clean();
        const uint8_t p = 28u;
        bind_more("PPM", &p, 1u, "OFF");
        CHECK_EQ(keys_hold_their_text("ppm"), 1u);
    }

    /* A learned board with a shape: 4 pins along one row. */
    clean();
    const uint16_t learned = 4245u;
    uint16_t cat[LINK_CAT_COUNT];
    memset(cat, 0, sizeof(cat));
    cat[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    cat[1] = LINK_CAT_OF(1, 2, LINK_PIN_FREE);
    cat[2] = LINK_CAT_OF(2, 3, LINK_PIN_FREE);
    cat[3] = LINK_CAT_OF(3, 4, LINK_PIN_FREE);
    CHECK(outbind_learn_board(learned, cat));
    uint16_t sh[LINK_SH_COUNT];
    memset(sh, 0, sizeof(sh));
    sh[LINK_SH_WIDTH_CMM]  = 19u * 254u;
    sh[LINK_SH_HEIGHT_CMM] = 1900u;
    sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(LINK_SH_BOTTOM_LEFT, 20);
    sh[LINK_SH_PITCH_CMM]  = 254u;
    sh[LINK_SH_INSET_CMM]  = 100u;
    CHECK(outbind_learn_shape(learned, sh));
    outbind_t lb;
    outbind_init(&lb);
    outbind_set_board(&lb, learned);
    outbind_set_proto(&lb, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&lb, 0u));
    CHECK(outbind_toggle(&lb, 3u));
    outbind_set_proto(&lb, 0u);
    picker_screen_set_binding(&lb);
    picker_screen_follow(0u, true);
    CHECK(picker_screen_can_draw());
    CHECK_EQ(keys_hold_their_text("learned"), 2u);
    outbind_forget_learned();
}

/* Every tag, in English and German, is drawn on its key: the 3 role words,
 * PORT1 to PORT4, BUS, PD and the phase tap's. */
TEST_CASE(the_tag_is_painted_in_both_languages)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        clean();
        ui_text_set_language((ui_lang_t)lang);
        interfaces_on();
        ports(4u);
        const uint8_t s0 = 0u, m = 2u, ppm = 7u;
        bind_more("SERVO PWM", &s0, 1u, "OFF");
        bind_more("MOTOR PWM", &m, 1u, "OFF");
        char lang_name[16];
        snprintf(lang_name, sizeof(lang_name), "language %d", lang);
        CHECK_EQ(keys_hold_their_text(lang_name), 11u);
        clean();
        ui_text_set_language((ui_lang_t)lang);
        bind_more("PPM", &ppm, 1u, "OFF");
        CHECK_EQ(keys_hold_their_text(lang_name), 1u);
        CHECK_STR_EQ(tag_of(7u), ui_tr(TX_PK_TAG_PPM));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The Dial names a tagged key "GPIO 7 ESC", every other "GPIO 0", each
 * within the 15 bytes its face shows. */
TEST_CASE(the_focus_name_carries_the_tag)
{
    static ui_focus_list_t list;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        clean();
        ui_text_set_language((ui_lang_t)lang);
        interfaces_on();
        const uint8_t gp = 7u;
        bind_more("DSHOT600", &gp, 1u, "SERVO PWM");
        memset(&list, 0, sizeof(list));
        CHECK(picker_screen_focus(&list));
        bool seven = false, zero = false, tap = false;
        for (int k = 0; k < list.count; ++k) {
            const char *l = list.item[k].label;
            CHECK(strlen(l) <= 15u);
            seven = seven || strcmp(l, "GPIO 7 ESC") == 0;
            zero = zero || strcmp(l, "GPIO 0") == 0;
            char want[24];
            snprintf(want, sizeof(want), "GPIO 22 %s", ui_tr(TX_PK_TAG_TAP));
            tap = tap || strcmp(l, want) == 0;
        }
        CHECK(seven);
        CHECK(zero);
        CHECK(tap);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_held_key_names_its_holder_in_five_cells);
    RUN(each_protocol_gives_its_role_tag);
    RUN(each_port_gives_its_number_and_a_single_port_none);
    RUN(each_interface_gives_its_tag);
    RUN(a_free_ticked_refused_or_clashing_key_has_no_tag);
    RUN(a_reserved_pin_has_no_key_and_no_tag);
    RUN(the_overview_tags_every_bound_pin);
    RUN(an_unread_binding_keeps_its_tags);
    RUN(a_port_entry_tags_other_ports_protocols_and_interfaces);
    RUN(the_tag_and_number_stay_inside_the_key_on_every_built_in_board);
    RUN(the_tag_is_painted_in_both_languages);
    RUN(the_focus_name_carries_the_tag);
    return test_summary("picker_tags");
}
