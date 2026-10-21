/**
 * @file test_outputs_screen_draw.c
 * @brief The outputs screen's rendering, its read-back of the binding, a change of
 * board, and the fit of its strings.
 *
 * Cases:
 * - the_longest_protocol_name_fits_the_list_and_a_cell
 * - a_selection_from_the_picker_is_a_pick
 * - an_empty_read_back_does_not_clear_the_chosen_protocol
 * - a_protocol_named_from_outside_lands_over_the_one_on_screen
 * - a_second_protocol_survives_the_read_back
 * - a_change_of_board_takes_the_protocol_with_it
 * - a_pick_made_for_one_board_is_not_carried_to_another
 * - every_state_renders_without_reading_off_the_canvas
 * - a_board_that_can_take_nothing_says_why
 * - a_protocol_that_has_all_its_pins_says_so
 * - the_binding_survives_being_set_from_outside
 * - a_protocol_index_from_outside_cannot_run_off_the_table
 * - null_events_are_refused_rather_than_dereferenced
 * - every_two_line_string_of_the_column_breaks_into_two
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

/* The programming port on GP13, for the clash lines. */
static const uint8_t k_gp13[1] = { 13u };

/* --------------------------------------------------------------- rendering */

/* Every protocol with every pin ticked, and its list open over that:
 * each frame paints the whole canvas and no pixel beside it, and each
 * protocol's grid is a picture of its own. */
TEST_CASE(every_state_renders_without_reading_off_the_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    uint32_t sum[OUTBIND_PROTOS];

    fresh();
    for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
        guard_canvas_init(&g, &c, px, SCREEN_W, SCREEN_H);
        choose_proto(p);
        for (uint8_t pin = 0; pin < 29u; ++pin) {
            if (outbind_index_of(OUTBIND_BOARD_PICO_HEADER, pin) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
                tap_pin(pin);
            }
        }
        scr()->render(&c, 0);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        sum[p] = guard_canvas_sum(&g);
        for (int q = 0; q < p; ++q) {
            CHECK(sum[p] != sum[q]);
        }
        /* And with the list open over the top of whatever was chosen. */
        tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
        scr()->render(&c, 1);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
        CHECK(guard_canvas_sum(&g) != sum[p]);
        tap(700, 400);
    }
    for (int r = 0; r <= (int)OUTPUTS_REFUSED; ++r) {
        outputs_screen_set_result((outputs_result_t)r);
        scr()->render(&c, 2);
        CHECK_EQ(guard_canvas_touched(&g), 0u);
        CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    }
}

TEST_CASE(a_board_that_can_take_nothing_says_why)
{
    /*
     * The rules refusing every pin are out_bind's and they are right; what
     * is under test is that the screen states them. A board drawn entirely
     * in grey with nothing beside it reads as a fault, and an operator
     * reported it as one.
     */
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    fresh();
    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    /*
     * Four servo pins, then PPM: eight channels needed against four free,
     * so nothing can be ticked at all. This is the state that was asked
     * about.
     */
    static const uint8_t gp[4] = { 0, 1, 2, 4 };
    for (unsigned i = 0; i < 4u; ++i) {
        tap_pin(gp[i]);
    }
    choose_named("PPM");
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);

    /* And it goes away again when the protocol can take a pin. */
    choose_named("SERVO PWM");
    scr()->render(&c, 0);
    CHECK_EQ(reason_ink(&c), quiet);
}

TEST_CASE(a_protocol_that_has_all_its_pins_says_so)
{
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    fresh();
    choose_named("SERVO PWM");                    /* eight pins */
    scr()->render(&c, 0);
    const unsigned quiet = reason_ink(&c);

    uint8_t taken = 0;
    for (uint8_t g = 0; g < 29u && taken < 8u; ++g) {
        const uint8_t i = outbind_index_of(OUTBIND_BOARD_PICO_HEADER, g);
        if (i < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)
            && outbind_can_add(outputs_screen_binding(), i)) {
            tap_pin(g);
            ++taken;
        }
    }
    CHECK_EQ(taken, 8u);
    scr()->render(&c, 0);
    CHECK(reason_ink(&c) != quiet);
}

TEST_CASE(the_binding_survives_being_set_from_outside)
{
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    const int dshot600 = proto_row("DSHOT600");
    outbind_set_proto(&b, (uint8_t)dshot600);
    (void)outbind_toggle(&b, outbind_index_of(OUTBIND_BOARD_PICO_HEADER, 7));
    outputs_screen_set_binding(&b);

    /* What was loaded from storage is what the screen now shows, on the
     * overview the board's first answer opened, and edits once its
     * protocol is picked. */
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_EQ(outputs_screen_cell(idx(7), NULL, 0), OUTPUTS_CELL_HELD);
    choose_named("DSHOT600");
    CHECK_EQ(outputs_screen_binding()->proto, dshot600);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    tap_pin(13);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
}

TEST_CASE(a_protocol_index_from_outside_cannot_run_off_the_table)
{
    /* The binding arrives from the wire and from flash, so the index is not
     * this screen's to trust.  Reading past the protocol table would render
     * a name from whatever followed it. */
    static gfx_color_t px[SCREEN_W * SCREEN_H];
    gfx_canvas_t c = { px, SCREEN_W, SCREEN_H, SCREEN_W, { 0, 0, SCREEN_W, SCREEN_H } };

    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
    b.proto = (uint8_t)(OUTBIND_PROTOS + 40u);
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        b.pins[g] = 0xFFFFFFFFu;
    }
    outputs_screen_set_binding(&b);
    scr()->render(&c, 0);

    /* And it is still usable: the list picks up from OFF rather than wedging. */
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_binding()->proto, 1);
}

TEST_CASE(null_events_are_refused_rather_than_dereferenced)
{
    fresh();
    scr()->event(NULL);
    scr()->enter();
    scr()->leave();
    CHECK(scr()->title != NULL);
}

/*
 * A protocol the operator has just picked survives an empty binding arriving
 * from the far end.
 *
 * A pick writes nothing, but a binding is read at every link-up and after
 * every write.  With nothing bound yet -- every first boot on an unwritten
 * store -- what is read is empty, and an empty binding says nothing about
 * which protocol is being worked in: OFF there is indistinguishable from
 * "nothing is configured".  Letting it land wholesale puts the list back to
 * OFF, and no pin can be ticked, because OFF takes none.
 */
TEST_CASE(an_empty_read_back_does_not_clear_the_chosen_protocol)
{
    fresh();
    const int bidir = proto_row("DSHOT600 BIDIR");

    /* The operator picks a protocol on the screen.  Nothing is ticked yet,
     * which is the only way to reach a first pin: OFF takes none. */
    choose_named("DSHOT600 BIDIR");
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);

    /* A read arrives: the link came up.  With an empty store -- every first
     * boot on this build -- what is read is empty, and an empty binding says
     * nothing about which protocol is being worked in. */
    outbind_t empty;
    outbind_init(&empty);
    outbind_set_board(&empty, OUTBIND_BOARD_PICO_HEADER);
    CHECK_EQ((int)outbind_chosen_total(&empty), 0);
    outputs_screen_set_binding(&empty);

    /* Still the operator's protocol, so a pin can be ticked at all. */
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);
    tap_pin(7);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);

    /* And nothing at all leaves the screen as it was.  The caller passes what
     * the far end gave it, and a link that answered nothing gives NULL. */
    outputs_screen_set_binding(NULL);
    CHECK_EQ((int)outputs_screen_binding()->proto, bidir);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);
}

/*
 * A protocol being started while another is already bound survives the
 * read-back as well.
 *
 * A page carries pins, and the protocol read back out of one is the lowest
 * that holds a pin -- there is nowhere on the page to say which one is being
 * edited.  A protocol just chosen holds no pin yet, so the read-back names
 * the one already bound and, landing whole, moves the screen off the choice
 * within one poll.  A bench wired for an ESC and then for servos meets this
 * on its second protocol.
 */
TEST_CASE(a_second_protocol_survives_the_read_back)
{
    fresh();
    const int dshot = proto_row("DSHOT600");
    const int servo = proto_row("SERVO PWM");

    choose_named("DSHOT600");
    tap_pin(7);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 1);

    /* The second protocol, with no pin of its own yet. */
    choose_named("SERVO PWM");
    CHECK_EQ((int)outputs_screen_binding()->proto, servo);

    /* What the far end answers: the page this binding renders to, read back
     * through the code the panel reads it with, rather than a page written
     * here to say what this test wants said. */
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(outputs_screen_binding(), slots);
    outbind_to_chan_cfg(outputs_screen_binding(), cc, 1000u, 2000u);
    outbind_t back;
    CHECK(outbind_from_slots(&back, OUTBIND_BOARD_PICO_HEADER, slots, cc));
    CHECK_EQ((int)back.proto, dshot);      /* the page can say nothing else */
    outputs_screen_set_binding(&back);

    /* Still the protocol being worked in, so its first pin can be ticked. */
    CHECK_EQ((int)outputs_screen_binding()->proto, servo);
    tap_pin(13);
    CHECK_EQ((int)outbind_chosen(outputs_screen_binding()), 1);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 2);
}

/*
 * A change of board takes the protocol with it.
 *
 * outbind_set_board() clears the selection because a pin index means a
 * different pin -- or no pin -- in another catalogue.  A protocol chosen for
 * the hardware that was there is worth no more than the pins were, and
 * keeping it would let the first tap on the board now in front of the
 * operator bind under it.
 */
TEST_CASE(a_change_of_board_takes_the_protocol_with_it)
{
    fresh();
    choose_named("DSHOT600");
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT600"));

    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    CHECK_EQ((int)other.proto, 0);
    outputs_screen_set_binding(&other);

    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_EQ((int)outbind_chosen_total(outputs_screen_binding()), 0);

    /* Back to the Pico header with pins bound: OFF, not the protocol the
     * pins name and not the one picked before, and a tap writes nothing. */
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);
}

/*
 * A protocol picked while the IOmcu is not there -- the list works
 * with the link down -- is not carried to the board that then answers: the
 * selection goes to OFF, and the next tap writes nothing.  The same for an
 * IOmcu swapped for another board after a pick.
 */
TEST_CASE(a_pick_made_for_one_board_is_not_carried_to_another)
{
    outbind_t b;
    three_protocols(&b);
    bind_reading_t r;
    read_back(&b, &r);

    scr()->reset();
    outputs_screen_set_apply(on_apply);
    s_applied = 0;
    bind_reading_t none;
    (void)bind_link_classify(&none, 0u, NULL, NULL);
    outputs_screen_set_reading(&none);
    scr()->enter();
    choose_named("DSHOT600");
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("DSHOT600"));

    outputs_screen_set_reading(&r);
    CHECK(outputs_screen_editable());
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    CHECK_STR_EQ(reason(), "CHOOSE A PROTOCOL, THEN TAP PINS");
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* Swapped after a pick: by binding, and by a reading with no pin map. */
    choose_named("DSHOT600");
    outbind_t other;
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(OUTBIND_BOARD_PICO_HEADER + 1u));
    outputs_screen_set_binding(&other);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    choose_named("MOTOR PWM");
    bind_reading_t unmapped = r;
    unmapped.state = BIND_READ_NO_BOARD;
    outbind_init(&unmapped.bind);
    outbind_set_board(&unmapped.bind, (uint16_t)0xBEEFu);
    outputs_screen_set_reading(&unmapped);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* The link comes up on another board and its first read fails: the
     * failed read names the board, and the read that follows names the
     * same one.  The pick made before is still not carried. */
    scr()->reset();
    outputs_screen_set_apply(on_apply);
    outputs_screen_set_reading(&none);
    scr()->enter();
    choose_named("DSHOT600");
    bind_reading_t failed;
    (void)bind_link_classify(&failed, OUTBIND_BOARD_PICO_HEADER, NULL, NULL);
    outputs_screen_set_reading(&failed);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    tap_pin(4);
    CHECK_EQ(s_applied, 0);

    /* The same board read again keeps a pick made on it. */
    choose_named("DSHOT600");
    outputs_screen_set_reading(&r);
    CHECK_EQ((int)outputs_screen_binding()->proto, proto_row("DSHOT600"));
    tap_pin(4);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(4)), proto_row("DSHOT600"));
}

/*
 * A protocol somebody names still lands, over the one on screen.
 *
 * The rule turns on what a page could have said.  A caller naming a protocol
 * the pins do not name is not a page -- it is the screen being posed, or told
 * what to show at start-up -- and what it names is the choice.
 */
TEST_CASE(a_protocol_named_from_outside_lands_over_the_one_on_screen)
{
    fresh();
    const int ppm = proto_row("PPM");
    choose_named("DSHOT600");
    tap_pin(7);

    /* PPM with the DShot pin still bound: no page renders to this, because
     * the protocol a page names is the lowest one holding a pin. */
    outbind_t b = *outputs_screen_binding();
    outbind_set_proto(&b, (uint8_t)ppm);
    CHECK(b.proto != outbind_wire_proto(&b));
    outputs_screen_set_binding(&b);

    CHECK_EQ(outputs_screen_binding()->proto, ppm);
}

/* ------------------------------------------------------------------ the fit */

/*
 * The longest protocol names inside what they are drawn in: the closed list
 * in the head face (16 px a cell) between its 10 px inset and the chevron's
 * column, and the line under a pin in the label face (8 px a cell) inside
 * the cell.  One cell more than the longest name does not fit either.
 */
TEST_CASE(the_longest_protocol_name_fits_the_list_and_a_cell)
{
    int longest = 0;
    for (unsigned i = 0; i < OUTBIND_PROTOS; ++i) {
        const int n = (int)strlen(outbind_protos()[i].name);
        if (n > longest) {
            longest = n;
        }
    }
    CHECK_EQ(longest, 14);                      /* "DSHOT600 BIDIR" */
    const int head = gfx_text_width(UI_FONT_HEAD, "DSHOT600 BIDIR", 1);
    const int lab  = gfx_text_width(UI_FONT_LABEL, "DSHOT600 BIDIR", 1);
    CHECK_EQ(head, 224);
    CHECK_EQ(lab, 112);
    /* The list: text from x + 10, the chevron's column from x + w - 28. */
    CHECK(10 + head <= DD_W - 28);
    CHECK(10 + head + 16 > DD_W - 28);
    /* A cell: text from x + 5, the border at x + w - 1. */
    CHECK(5 + lab <= CELL_W - 1);
    CHECK(5 + lab + 8 > CELL_W - 1);
    /* And the grid ends inside the 800 px screen with its 16 px margin. */
    CHECK_EQ(GRID_X + 4 * CELL_W + 3 * CELL_GAP, SCREEN_W - 16);
    CHECK(DD_X + DD_W < GRID_X);
}

/* The two entry points the pin picker uses: a selection from outside goes
 * through the same checks as a pick, and the reason by itself takes no
 * missing binding or buffer. */
TEST_CASE(a_selection_from_the_picker_is_a_pick)
{
    fresh();
    outputs_screen_select((uint8_t)proto_row("DSHOT300"));
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT300"));
    CHECK_EQ(s_applied, 0);
    outputs_screen_select((uint8_t)OUTBIND_PROTOS);
    CHECK_EQ(outputs_screen_binding()->proto, proto_row("DSHOT300"));
    outputs_screen_select(0u);
    CHECK_STR_EQ(reason(), TR(OUT_OFF_VIEW));
    CHECK_EQ(s_applied, 0);

    char buf[8] = "x";
    CHECK(!outputs_reason_of(NULL, buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_reason_of(outputs_screen_binding(), NULL, 0));

    /* The PWM clash lines take no missing binding, buffer or pin either. */
    strcpy(buf, "x");
    CHECK(!outputs_clash_reason(NULL, 0, k_gp13, 1u, buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_clash_reason(outputs_screen_binding(), -1, k_gp13, 1u, buf,
                                sizeof(buf)));
    CHECK(!outputs_clash_reason(outputs_screen_binding(),
                                (int)OUTBIND_PINS, k_gp13, 1u, buf,
                                sizeof(buf)));
    CHECK(!outputs_clash_reason(outputs_screen_binding(), 0, k_gp13, 1u,
                                NULL, 0));
    strcpy(buf, "x");
    CHECK(!outputs_clash_label(outputs_screen_binding(), 0u, k_gp13, 1u, buf,
                               sizeof(buf)));
    CHECK_STR_EQ(buf, "");
    CHECK(!outputs_clash_label(outputs_screen_binding(), 0u, k_gp13, 1u,
                               NULL, 0));
}

/*
 * The column's lines that break onto a second line where they need it: the
 * reason under the protocol (266 px), the label of the button that unbinds
 * every pin (250 px inside it) and the last write in the head face
 * (266 px).  ui_text.def holds each to both lines' cells; each has to break
 * into the two, in every language, a pin number at its widest.
 */
TEST_CASE(every_two_line_string_of_the_column_breaks_into_two)
{
    static const struct {
        ui_text_id_t id;
        int w;
        bool head;
    } k[] = {
        { TX_OUT_OFF_VIEW, 266, false },    { TX_OUT_NOT_READ, 266, false },
        { TX_OUT_NOT_MAPPED, 266, false },  { TX_OUT_SLICE_WHY, 266, false },
        { TX_OUT_COMPARE_WHY, 266, false }, { TX_OUT_HOLD_UNBIND, 250, false },
        { TX_OUT_WRITTEN, 266, true },      { TX_OUT_REFUSED, 266, true },
        { TX_OUT_NOT_WRITTEN, 266, true },  { TX_OUT_NO_LINK, 266, true },
        { TX_OUT_PORT_HELD, 266, false },   { TX_OUT_PORT_LOCKED, 266, false },
        { TX_OUT_PORT_PWM_NEEDS, 266, false },
        { TX_OUT_PORT_NOT_READ, 266, false },
        { TX_OUT_PORT_FN_OLD, 266, false },
        { TX_OUT_PORT_ASK_WAIT, 266, false },
        { TX_OUT_PORT_ASK_REFUSED, 266, false },
        { TX_OUT_PORT_ASK_LOST, 266, false },
        { TX_OUT_PORT_TESTER_PWM, 266, false },
        { TX_OUT_PORT_TESTER_ON, 266, false },
    };
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            char s[96];
            snprintf(s, sizeof(s), ui_tr_in((ui_lang_t)l, k[i].id), 28u,
                     13u);
            const int n = outputs_wrap(NULL, gfx_rect_make(0, 0, k[i].w, 16),
                                       18, s, 3,
                                       k[i].head ? UI_FONT_HEAD
                                                 : UI_FONT_LABEL,
                                       0, false);
            if (n > 2) {
                T_FAIL("%s in language %d takes %d lines",
                       ui_text_name(k[i].id), l, n);
            }
        }
    }
}

int main(void)
{
    RUN(the_longest_protocol_name_fits_the_list_and_a_cell);
    RUN(a_selection_from_the_picker_is_a_pick);
    RUN(an_empty_read_back_does_not_clear_the_chosen_protocol);
    RUN(a_protocol_named_from_outside_lands_over_the_one_on_screen);
    RUN(a_second_protocol_survives_the_read_back);
    RUN(a_change_of_board_takes_the_protocol_with_it);
    RUN(a_pick_made_for_one_board_is_not_carried_to_another);
    RUN(every_state_renders_without_reading_off_the_canvas);
    RUN(a_board_that_can_take_nothing_says_why);
    RUN(a_protocol_that_has_all_its_pins_says_so);
    RUN(the_binding_survives_being_set_from_outside);
    RUN(a_protocol_index_from_outside_cannot_run_off_the_table);
    RUN(null_events_are_refused_rather_than_dereferenced);
    RUN(every_two_line_string_of_the_column_breaks_into_two);
    return test_summary("outputs_screen_draw");
}
