/**
 * @file test_outputs_screen_off.c
 * @brief The outputs screen's overview and OFF: every bound pin named, a pick and
 * back, and a binding that did not read.
 *
 * Cases:
 * - the_screen_opens_on_the_overview
 * - the_overview_shows_no_count_and_no_pressed_face
 * - the_overview_with_nothing_bound_writes_nothing
 * - the_overview_with_every_slot_bound_names_every_pin
 * - the_overview_draws_each_bound_pins_protocol_in_full_ink
 * - a_pick_and_off_again_go_to_a_protocol_and_back_to_the_overview
 * - off_shows_the_protocol_of_every_bound_pin
 * - a_tap_on_a_pin_does_nothing_while_off_is_selected
 * - off_stays_until_another_entry_is_picked
 * - a_board_that_answers_during_a_visit_keeps_the_overview
 * - a_board_swapped_during_a_visit_keeps_the_overview
 * - off_with_nothing_bound_is_left_by_a_pick
 * - a_binding_that_did_not_read_is_kept_marked_and_not_edited
 * - a_failed_read_for_another_board_keeps_no_binding
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

/* What the screen prints for every pin of the board, one line a pin: the
 * state of its cell and the line under its name. */
static const char *trace(void)
{
    static char out[2048];
    static const char *const k_state[] = {
        "none", "free", "ticked", "held", "reserved",
    };
    size_t at = 0;
    out[0] = '\0';
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    for (uint8_t i = 0; i < n; ++i) {
        char label[32];
        const outputs_cell_t st = outputs_screen_cell(i, label,
                                                      sizeof(label));
        const int w = snprintf(out + at, sizeof(out) - at, "GP%u %s '%s'\n",
            (unsigned)outbind_pins(OUTBIND_BOARD_PICO_HEADER)[i].gpio,
            k_state[st], label);
        at += (size_t)w;
    }
    return out;
}

static void pose_three_protocols(void)
{
    fresh();
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    /* The board answered and the selection went to OFF; SERVO PWM is
     * picked, as an operator does to edit it. */
    outputs_screen_select((uint8_t)proto_row("SERVO PWM"));
}

/* ---------------------------------------------------------------------- OFF */

/* The pins past GP2 of the bench of three_protocols(): free pins print the
 * pad number on the board, reserved ones what holds them. */
#define TRACE_GP3_TO_GP12                                                     \
    "GP3 reserved 'HEARTBEAT'\n"                                              \
    "GP4 free 'PAD 6'\n"                                                      \
    "GP5 free 'PAD 7'\n"                                                      \
    "GP6 free 'PAD 9'\n"                                                      \
    "GP7 free 'PAD 10'\n"                                                     \
    "GP8 reserved 'CAN INT'\n"                                                \
    "GP9 reserved 'CAN CS'\n"                                                 \
    "GP10 reserved 'CAN SCK'\n"                                               \
    "GP11 reserved 'CAN MOSI'\n"                                              \
    "GP12 reserved 'CAN MISO'\n"

#define TRACE_GP14_TO_GP28                                                    \
    "GP14 free 'PAD 19'\n"                                                    \
    "GP15 free 'PAD 20'\n"                                                    \
    "GP16 free 'PAD 21'\n"                                                    \
    "GP17 free 'PAD 22'\n"                                                    \
    "GP18 free 'PAD 24'\n"                                                    \
    "GP19 free 'PAD 25'\n"                                                    \
    "GP20 free 'PAD 26'\n"                                                    \
    "GP21 free 'PAD 27'\n"                                                    \
    "GP22 free 'PAD 29'\n"                                                    \
    "GP26 free 'PAD 31'\n"                                                    \
    "GP27 free 'PAD 32'\n"                                                    \
    "GP28 free 'PAD 34'\n"

/* OFF selected: all four bound pins name their protocol. */
#define TRACE_OFF                                                             \
    "GP0 held 'DSHOT600 BIDIR'\n"                                             \
    "GP1 held 'MOTOR PWM'\n"                                                  \
    "GP2 held 'SERVO PWM'\n"                                                  \
    TRACE_GP3_TO_GP12                                                         \
    "GP13 held 'SERVO PWM'\n"                                                 \
    TRACE_GP14_TO_GP28

/*
 * SERVO PWM selected: its two pins are ticked and show their pad.  GP17 and
 * GP18 share a compare register with the PWM pins GP1 and GP2, so they are
 * held by them; GP16 is GP0's partner, and GP0 is DShot, which drives no
 * compare register.
 */
#define TRACE_SERVO                                                           \
    "GP0 held 'DSHOT600 BIDIR'\n"                                             \
    "GP1 held 'MOTOR PWM'\n"                                                  \
    "GP2 ticked 'PAD 4'\n"                                                    \
    TRACE_GP3_TO_GP12                                                         \
    "GP13 ticked 'PAD 17'\n"                                                  \
    "GP14 free 'PAD 19'\n"                                                    \
    "GP15 free 'PAD 20'\n"                                                    \
    "GP16 free 'PAD 21'\n"                                                    \
    "GP17 held 'PWM CH OF GP1'\n"                                  \
    "GP18 held 'PWM CH OF GP2'\n"                                  \
    "GP19 free 'PAD 25'\n"                                                    \
    "GP20 free 'PAD 26'\n"                                                    \
    "GP21 free 'PAD 27'\n"                                                    \
    "GP22 free 'PAD 29'\n"                                                    \
    "GP26 free 'PAD 31'\n"                                                    \
    "GP27 free 'PAD 32'\n"                                                    \
    "GP28 free 'PAD 34'\n"

/*
 * Every visit opens on the overview: OFF selected, every bound pin naming
 * its protocol, the line under the list saying what to do.  Entering calls
 * the apply seam for nothing.  A pick then selects as it always does.
 */
TEST_CASE(the_screen_opens_on_the_overview)
{
    pose_three_protocols();
    choose_named("SERVO PWM");          /* a pick made before this visit */
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "PROTOKOLL WÄHLEN, DANN PIN TIPPEN");
    ui_text_set_language(UI_LANG_EN);

    /* SERVO PWM picked: its own pins show their pad, the others' their
     * protocol, and nothing is written. */
    choose_named("SERVO PWM");
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("SERVO PWM"));
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 0);
}

/* No pin bound: the overview shows every pin free or reserved, and no tap
 * on any of them writes. */
TEST_CASE(the_overview_with_nothing_bound_writes_nothing)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;

    fresh();
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    for (uint8_t i = 0; i < n; ++i) {
        const outputs_cell_t st = outputs_screen_cell(i, NULL, 0);
        CHECK(st == OUTPUTS_CELL_FREE || st == OUTPUTS_CELL_RESERVED);
    }
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    for (uint8_t gp = 0; gp < 29u; ++gp) {
        if (idx(gp) < n) {
            tap_pin(gp);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);
    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
}

/* All 8 slots bound over six protocols, as a reading gives them. */
static void every_slot_bound(outbind_t *b)
{
    static const struct { const char *proto; uint8_t gp; } k[8] = {
        { "SERVO PWM", 2 },       { "SERVO PWM", 4 },
        { "MOTOR PWM", 5 },       { "DSHOT600", 6 },
        { "DSHOT600", 7 },        { "DSHOT300", 14 },
        { "DSHOT600 BIDIR", 15 }, { "DSHOT300 BIDIR", 16 },
    };
    outbind_init(b);
    outbind_set_board(b, OUTBIND_BOARD_PICO_HEADER);
    for (unsigned i = 0; i < 8u; ++i) {
        outbind_set_proto(b, (uint8_t)proto_row(k[i].proto));
        CHECK(outbind_toggle(b, idx(k[i].gp)));
    }
    CHECK_EQ((int)outbind_chosen_total(b), (int)LINK_OUT_SLOTS);
}

/*
 * Every slot bound: the overview names the protocol of each of the 8 pins,
 * its line says to choose a protocol rather than that the slots are full,
 * and no tap changes the binding or writes.
 */
TEST_CASE(the_overview_with_every_slot_bound_names_every_pin)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;

    fresh();
    outbind_t b;
    every_slot_bound(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    const uint8_t n = outbind_pin_count(OUTBIND_BOARD_PICO_HEADER);
    unsigned named = 0;
    for (uint8_t i = 0; i < n; ++i) {
        char l[32];
        const outputs_cell_t st = outputs_screen_cell(i, l, sizeof(l));
        const uint8_t held = outbind_group_of(&b, i);
        if (held != 0u) {
            CHECK_EQ(st, OUTPUTS_CELL_HELD);
            CHECK_STR_EQ(l, outbind_protos()[held].name);
            ++named;
        } else {
            CHECK(st == OUTPUTS_CELL_FREE || st == OUTPUTS_CELL_RESERVED);
        }
    }
    CHECK_EQ(named, (unsigned)LINK_OUT_SLOTS);
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));

    const outbind_t before = *outputs_screen_binding();
    for (uint8_t gp = 0; gp < 29u; ++gp) {
        if (idx(gp) < n) {
            tap_pin(gp);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK(memcmp(&before, outputs_screen_binding(), sizeof(before)) == 0);
    guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
    scr()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);

    /* A protocol picked: the slots are full, and the line says so. */
    choose_named("SERVO PWM");
    CHECK_STR_EQ(reason(), "ALL 8 SLOTS IN USE");
    CHECK_EQ(s_applied, 0);
}

/* Whether the line under GP@p gpio's name has a pixel of @p ink. */
static bool label_inked(const gfx_canvas_t *c, uint8_t gpio, gfx_color_t ink)
{
    int x, y;
    cell_centre(gpio, &x, &y);
    const int x0 = x - CELL_W / 2 + OU_CELL_PAD;
    const int y0 = y - CELL_H / 2 + OU_CELL_LABEL_Y;
    for (int yy = y0; yy < y0 + 16; ++yy) {
        for (int xx = x0; xx < x0 + 144; ++xx) {
            if (c->pixels[yy * c->stride + xx] == ink) {
                return true;
            }
        }
    }
    return false;
}

/* Whether any pixel of @p ink lies in the row the pin count is drawn on. */
static bool count_row_inked(const gfx_canvas_t *c, gfx_color_t ink)
{
    const int y0 = DD_Y + DD_H + 8;
    for (int yy = y0; yy < y0 + 20; ++yy) {
        for (int xx = DD_X; xx < DD_X + DD_W; ++xx) {
            if (c->pixels[yy * c->stride + xx] == ink) {
                return true;
            }
        }
    }
    return false;
}

/*
 * OFF takes no pin, so the overview shows no pin count, and a press on a
 * pin draws no pressed face: the cell does not look as if it would act.
 * A protocol picked brings both back.
 */
TEST_CASE(the_overview_shows_no_count_and_no_pressed_face)
{
    static gfx_color_t px[SCREEN_W * SCREEN_H], was[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    pose_three_protocols();
    scr()->enter();
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(!count_row_inked(&c, UI_TEXT_DIM));
    memcpy(was, px, sizeof(px));

    int x, y;
    cell_centre(4, &x, &y);                     /* a free pin */
    press_at(x, y);
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(memcmp(was, px, sizeof(px)) == 0);
    release_at(x, y);
    CHECK_EQ(s_applied, 0);

    choose_named("SERVO PWM");
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(count_row_inked(&c, UI_TEXT_DIM));
    memcpy(was, px, sizeof(px));
    press_at(x, y);
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    CHECK(memcmp(was, px, sizeof(px)) != 0);
    release_at(x, y);
    CHECK_EQ(s_applied, 1);
}

/*
 * The overview draws the protocol under each bound pin in full ink, apart
 * from the pad numbers of free pins.  With a protocol picked, the pins of
 * the others are dimmed as before, and so is the overview while the
 * binding is not confirmed.
 */
TEST_CASE(the_overview_draws_each_bound_pins_protocol_in_full_ink)
{
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    pose_three_protocols();
    scr()->enter();
    scr()->render(&c, 0);
    CHECK(label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 13, UI_TEXT));
    CHECK(!label_inked(&c, 4, UI_TEXT));        /* free: PAD 6, faint */

    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    CHECK(!label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 0, UI_TEXT_DIM));

    choose_named("OFF");
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->render(&c, 0);
    CHECK(!label_inked(&c, 0, UI_TEXT));
    CHECK(label_inked(&c, 0, UI_TEXT_DIM));
}

/*
 * A protocol picked and OFF picked again: back on the overview, where the
 * pin just ticked names its protocol and a tap on it writes nothing.  The
 * same protocol picked again takes the tap.
 */
TEST_CASE(a_pick_and_off_again_go_to_a_protocol_and_back_to_the_overview)
{
    pose_three_protocols();
    scr()->enter();
    choose_named("SERVO PWM");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outputs_screen_cell(idx(4), NULL, 0), OUTPUTS_CELL_TICKED);

    choose_named("OFF");
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 1);
    char l[32];
    CHECK_EQ(outputs_screen_cell(idx(4), l, sizeof(l)), OUTPUTS_CELL_HELD);
    CHECK_STR_EQ(l, "SERVO PWM");
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    tap_pin(4);
    tap_pin(5);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(4)),
             proto_row("SERVO PWM"));
    CHECK_EQ(outbind_group_of(outputs_screen_binding(), idx(5)), 0);

    choose_named("SERVO PWM");
    tap_pin(4);                                 /* unticked */
    CHECK_EQ(s_applied, 2);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), 0);
}

/*
 * OFF selected, three protocols bound: every bound pin prints its
 * protocol's name, every free pin its pad, every reserved pin its holder.
 * The whole board, as a text trace of what is drawn under each pin.
 */
TEST_CASE(off_shows_the_protocol_of_every_bound_pin)
{
    pose_three_protocols();
    choose_named("OFF");
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");

    /* The same pins under SERVO PWM: its own two show their pad. */
    choose_named("SERVO PWM");
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(s_applied, 0);
}

/*
 * With OFF selected no pin can be ticked or unticked: a tap changes nothing
 * and calls nothing, and the line under the protocol says what OFF is for.
 */
TEST_CASE(a_tap_on_a_pin_does_nothing_while_off_is_selected)
{
    pose_three_protocols();
    choose_named("OFF");
    const outbind_t before = *outputs_screen_binding();
    for (uint8_t g = 0; g < 29u; ++g) {
        if (idx(g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
            tap_pin(g);
        }
    }
    CHECK_EQ(s_applied, 0);
    CHECK(memcmp(&before, outputs_screen_binding(), sizeof(before)) == 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "PROTOKOLL WÄHLEN, DANN PIN TIPPEN");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * OFF is a pick like any other.  It stays across a reading of the same
 * pages, a second one, a repaint, a link that goes and comes back, a
 * reading of other pins, a leave and a change of board, until another
 * entry is picked.
 */
TEST_CASE(off_stays_until_another_entry_is_picked)
{
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    pose_three_protocols();
    scr()->enter();
    choose_named("OFF");
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    CHECK_EQ((int)r.bind.proto, proto_row("SERVO PWM"));   /* the page's */

    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_binding(&r.bind);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);

    /* Repaints, in every framebuffer. */
    outputs_screen_invalidate();
    scr()->render(&c, 0);
    scr()->render(&c, 1);
    scr()->cancel();
    scr()->render(&c, 0);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    /* The link goes: nothing read.  It comes back: the link-up read. */
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(trace(), TRACE_OFF);
    CHECK(outputs_screen_editable());

    /* A reading with other pins: still OFF, showing them. */
    outbind_t fewer = b;
    outbind_set_proto(&fewer, (uint8_t)proto_row("SERVO PWM"));
    (void)outbind_toggle(&fewer, idx(13));
    read_back(&fewer, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_FREE);
    CHECK_EQ(outputs_screen_cell(idx(2), NULL, 0), OUTPUTS_CELL_HELD);

    /* Another entry is picked: that one is kept from here on. */
    choose_named("MOTOR PWM");
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("MOTOR PWM"));

    /* OFF again, and the screen is left: the selection stays OFF, and the
     * next entry opens on the overview as every entry does. */
    choose_named("OFF");
    scr()->leave();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    scr()->enter();
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    /* A picked protocol other than OFF outlasts the visit; the next entry
     * opens on the overview. */
    choose_named("PPM");
    scr()->leave();
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ(s_applied, 0);

    /* A protocol named from outside lands, as a pick does. */
    choose_named("OFF");
    outbind_t named = r.bind;
    outbind_set_proto(&named, (uint8_t)proto_row("PPM"));
    outputs_screen_set_binding(&named);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("PPM"));

    /* A change of board keeps OFF: no protocol was picked for the board
     * that answers now.  Out to another board and back, by binding and by
     * reading, a board with no pin map included. */
    choose_named("OFF");
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    bind_reading_t unmapped = r;
    unmapped.state = BIND_READ_NO_BOARD;
    outbind_init(&unmapped.bind);
    outbind_set_board(&unmapped.bind, (uint16_t)0xBEEFu);
    outputs_screen_set_reading(&unmapped);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(13);
    tap_pin(2);
    CHECK_EQ(s_applied, 0);
}

/*
 * The panel boots with the IOmcu off: the screen knows no board, and
 * opens on the overview.  The IOmcu comes up and its first reading
 * names a board and a binding.  Nobody picked a protocol, so a tap still
 * writes nothing -- the overview must not hand the next tap the lowest
 * protocol the reading holds.
 */
TEST_CASE(a_board_that_answers_during_a_visit_keeps_the_overview)
{
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);

    /* No board yet: the read the panel posts while the link is down. */
    scr()->reset();
    outputs_screen_set_apply(on_apply);
    s_applied = 0;
    bind_reading_t none;
    (void)bind_link_classify(&none, 0u, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->board,
             (int)OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    for (uint8_t gp = 0; gp < 30u; ++gp) {
        if (idx(gp) < OUTBIND_PINS) { tap_pin(gp); }
    }
    CHECK_EQ(s_applied, 0);

    /* A pick is what makes a pin tickable. */
    choose_named("SERVO PWM");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), proto_row("SERVO PWM"));
}

/* An IOmcu swapped for another board type while the screen is open:
 * the overview stays, and a tap writes nothing. */
TEST_CASE(a_board_swapped_during_a_visit_keeps_the_overview)
{
    fresh();
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    scr()->enter();
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);

    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->board,
             (int)OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    tap_pin(13);
    CHECK_EQ(s_applied, 0);
    CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_HELD);
}

/* OFF with nothing bound is where a bench starts: a reading does not move
 * it, and the first pick is what makes a pin tickable. */
TEST_CASE(off_with_nothing_bound_is_left_by_a_pick)
{
    fresh();
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(4);
    CHECK_EQ(s_applied, 0);
    choose_named("SERVO PWM");
    CHECK_STR_EQ(reason(), "");
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
}

/* ------------------------------------------------- a binding not confirmed */

/*
 * A reading that is not a binding leaves the last one on the screen, takes
 * back an edit nobody confirmed, says why, and refuses every tap on a pin.
 * The next reading that is a binding ends that.
 */
TEST_CASE(a_binding_that_did_not_read_is_kept_marked_and_not_edited)
{
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    pose_three_protocols();
    CHECK(outputs_screen_editable());
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_OK);
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    /* An edit, and then the read that should have confirmed it fails. */
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);
    bind_reading_t none;
    (void)bind_link_classify(&none, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&none);

    CHECK(!outputs_screen_editable());
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_NONE);
    CHECK_STR_EQ(trace(), TRACE_SERVO);         /* GP4 is free again */
    CHECK_STR_EQ(reason(), "ASSIGNMENT NOT READ. EDITING LOCKED.");
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT.");
    ui_text_set_language(UI_LANG_EN);

    /* Ticked, free, held or reserved: no tap acts. */
    for (uint8_t g = 0; g < 29u; ++g) {
        if (idx(g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
            tap_pin(g);
        }
    }
    CHECK_EQ(s_applied, 1);
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    /* A press that began while the binding was confirmed does not act on
     * its release either. */
    bind_reading_t ok;
    outbind_t b;
    three_protocols(&b);
    read_back(&b, &ok);
    outputs_screen_set_reading(&ok);
    int x, y;
    cell_centre(4, &x, &y);
    press_at(x, y);
    outputs_screen_set_reading(&none);
    release_at(x, y);
    CHECK_EQ(s_applied, 1);

    /* The list still opens and a pick still changes what is shown. */
    choose_named("MOTOR PWM");
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outputs_screen_cell(idx(1), NULL, 0), OUTPUTS_CELL_TICKED);
    CHECK_STR_EQ(reason(), "ASSIGNMENT NOT READ. EDITING LOCKED.");
    choose_named("SERVO PWM");

    /* Pages no binding describes: the same, with its own reason. */
    bind_reading_t odd = ok;
    odd.state = BIND_READ_ODD;
    outbind_init(&odd.bind);
    outbind_set_board(&odd.bind, OUTBIND_BOARD_PICO_HEADER);
    outputs_screen_set_reading(&odd);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    CHECK_STR_EQ(reason(), "NO VALID ASSIGNMENT IN THE PAGES");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(reason(), "KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES");
    ui_text_set_language(UI_LANG_EN);
    tap_pin(4);
    tap_pin(2);
    CHECK_EQ(s_applied, 1);
    scr()->render(&c, 0);
    scr()->render(&c, 1);

    /* A read succeeds: the binding it gives, and edits again. */
    outputs_screen_set_reading(&ok);
    CHECK(outputs_screen_editable());
    CHECK_STR_EQ(reason(), "");
    CHECK_STR_EQ(trace(), TRACE_SERVO);
    tap_pin(4);
    CHECK_EQ(s_applied, 2);
    outputs_screen_set_reading(NULL);
    CHECK(outputs_screen_editable());
}

/* A reading that is not a binding, for another board than the one shown:
 * nothing read earlier is a binding of it, so nothing is kept. */
TEST_CASE(a_failed_read_for_another_board_keeps_no_binding)
{
    pose_three_protocols();
    bind_reading_t none;
    (void)bind_link_classify(&none, (uint16_t)0x7777u, NULL, NULL);
    outputs_screen_set_reading(&none);
    CHECK_EQ(outputs_screen_binding()->board, 0x7777u);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 0);
    CHECK(!outputs_screen_editable());
    CHECK_EQ(outputs_screen_cell(0, NULL, 0), OUTPUTS_CELL_NONE);

    /* A board this build has no pin map for shows no pins and takes no
     * edit; the reading for a known board ends that. */
    pose_three_protocols();
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(outputs_screen_binding(), slots);
    outbind_to_chan_cfg(outputs_screen_binding(), cc, 1000u, 2000u);
    bind_reading_t r;
    CHECK_EQ(bind_link_classify(&r, (uint16_t)0x7777u, slots, cc),
             BIND_READ_NO_BOARD);
    outputs_screen_set_reading(&r);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_NO_BOARD);
    CHECK(!outputs_screen_editable());
    CHECK_EQ(outbind_pin_count(outputs_screen_binding()->board), 0);
    outbind_t b;
    three_protocols(&b);
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);  /* another board */
    CHECK_STR_EQ(trace(), TRACE_OFF);
}

int main(void)
{
    RUN(the_screen_opens_on_the_overview);
    RUN(the_overview_shows_no_count_and_no_pressed_face);
    RUN(the_overview_with_nothing_bound_writes_nothing);
    RUN(the_overview_with_every_slot_bound_names_every_pin);
    RUN(the_overview_draws_each_bound_pins_protocol_in_full_ink);
    RUN(a_pick_and_off_again_go_to_a_protocol_and_back_to_the_overview);
    RUN(off_shows_the_protocol_of_every_bound_pin);
    RUN(a_tap_on_a_pin_does_nothing_while_off_is_selected);
    RUN(off_stays_until_another_entry_is_picked);
    RUN(a_board_that_answers_during_a_visit_keeps_the_overview);
    RUN(a_board_swapped_during_a_visit_keeps_the_overview);
    RUN(off_with_nothing_bound_is_left_by_a_pick);
    RUN(a_binding_that_did_not_read_is_kept_marked_and_not_edited);
    RUN(a_failed_read_for_another_board_keeps_no_binding);
    return test_summary("outputs_screen_off");
}
