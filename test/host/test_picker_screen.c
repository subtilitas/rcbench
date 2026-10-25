/**
 * @file test_picker_screen.c
 * @brief The pin picker: a board with a button on every pin an output may have.
 *
 * The screen owns no rules -- out_bind does -- so what is under test is the
 * touching and the geometry. The geometry is the part that can be wrong in a
 * way nobody sees: a button wired to the pad next to its own would let an
 * operator press GP4 and bind GP5, and the board would look right.
 *
 * So the tests here do not hard-code where a button is. They ask the board
 * where a pad is, the way the screen does, and press there.
 *
 * This suite holds the buttons, the drawing, the ground marks and the follow of
 * the protocol.  The held pins, OFF, the tables and the fit are in
 * test_picker_screen_*.c.
 *
 * Cases:
 * - an_unconfirmed_binding_takes_no_tap
 * - the_picker_follows_the_protocol_chosen_on_the_outputs_screen
 * - a_button_binds_the_pad_it_is_wired_to
 * - every_button_binds_its_own_pin_and_no_other
 * - a_reserved_pin_has_no_button_to_press
 * - a_pin_another_protocol_holds_is_not_this_ones_to_take
 * - a_release_somewhere_else_acts_not_at_all
 * - a_board_that_does_not_say_where_its_pads_are_is_not_drawn
 * - the_coprocessors_drawing_is_used_when_it_agrees_with_the_board
 * - the_buttons_are_where_they_are_with_and_without_the_drawing
 * - a_drawing_that_disagrees_or_does_not_parse_is_not_shown
 * - a_drawing_is_shown_for_its_own_board_only
 * - the_tables_beside_the_board_say_what_the_pins_became
 * - the_drawing_is_asked_for_on_the_way_in
 * - a_binding_for_another_board_asks_for_its_drawing
 * - a_ground_is_marked_and_is_not_a_button
 * - a_board_that_does_not_say_which_pads_are_ground_still_draws
 * - the_marks_do_not_get_in_the_way_of_the_buttons
 * - null_events_are_refused_rather_than_dereferenced
 * - a_cancelled_press_toggles_nothing
 * - a_release_the_tracker_makes_for_a_jump_binds_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

/* The button belonging to a pad is on the pad's own side, straight out from
 * it. Walking away from the board finds it without knowing the tiers. */
static bool button_point_on(uint16_t board, uint8_t gpio, int *bxp, int *byp)
{
    int px, py;
    if (!pad_point_on(board, gpio, &px, &py)) { return false; }
    const int mid = SCREEN_H / 2;
    const int step = (py < mid) ? -1 : 1;
    for (int y = py + step * 20; y > 0 && y < SCREEN_H; y += step * 2) {
        touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)px, (int16_t)y, 40 }, 0u };
        scr()->event(&d);
        touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)px, (int16_t)y, 40 }, 0u };
        const int before = s_applied;
        scr()->event(&u);
        if (s_applied != before) {
            *bxp = px;
            *byp = y;
            return true;              /* that press acted: this is the button */
        }
    }
    return false;
}

static bool button_point(uint8_t gpio, int *bxp, int *byp)
{
    return button_point_on(BOARD, gpio, bxp, byp);
}

TEST_CASE(a_button_binds_the_pad_it_is_wired_to)
{
    /*
     * The whole point of the screen. Press the button above GP4's pad and
     * GP4 is what gets bound -- not the pin next to it, which is what a
     * trace drawn to the wrong pad would give.
     */
    fresh();
    render();
    int bx, by;
    if (!button_point(4u, &bx, &by)) { T_FAIL("GP4 has no button"); }
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(4u)), 1);
    CHECK_EQ(outbind_chosen(&s_last), 1);

    /* And pressing it again gives the pin back. */
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, 2);
    CHECK_EQ(outbind_chosen(&s_last), 0);
}

/*
 * A button press toggles its pin on the release, when the release lands on
 * the cell the press began on.  A press left latched after a lost event
 * turns the next release over that cell into a binding change nobody asked
 * for; the GT911 reuses track ids, so it need not be the same contact.
 */
TEST_CASE(a_cancelled_press_toggles_nothing)
{
    fresh();
    int bx, by;
    CHECK(button_point(idx(4u), &bx, &by));
    const int was = s_applied;

    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    scr()->event(&d);
    scr()->cancel();
    scr()->event(&u);
    CHECK_EQ(s_applied, was);

    /* And nothing is stuck: a fresh press and release still toggles. */
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, was + 1);
}

TEST_CASE(every_button_binds_its_own_pin_and_no_other)
{
    /*
     * Not one pad but all of them: an off-by-one in the tiering would bind a
     * neighbour, and with twenty buttons a row that is easy to miss by eye.
     */
    const outbind_board_t *bd = outbind_board(BOARD);
    for (uint8_t i = 0; i < bd->count; ++i) {
        if (bd->pins[i].reserved) { continue; }
        fresh();
        render();
        int bx, by;
        const uint8_t gp = bd->pins[i].gpio;
        if (!button_point(gp, &bx, &by)) {
            T_FAIL("GP%u has no button", gp);
        }
        if (outbind_group_of(&s_last, i) != 1u) {
            T_FAIL("pressing GP%u's button bound something else", gp);
        }
        if (outbind_chosen(&s_last) != 1u) {
            T_FAIL("pressing GP%u's button bound %u pins", gp,
                   outbind_chosen(&s_last));
        }
    }
}

TEST_CASE(a_reserved_pin_has_no_button_to_press)
{
    /*
     * GP3 carries the heartbeat. It is crossed on the pad, and there is
     * nothing above it to press: a button under a pin that cannot be chosen
     * says it could be.
     */
    fresh();
    render();
    int bx, by;
    CHECK(!button_point(3u, &bx, &by));
    CHECK_EQ(s_applied, 0);
}

TEST_CASE(a_pin_another_protocol_holds_is_not_this_ones_to_take)
{
    fresh();
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));       /* SERVO PWM takes GP4 */
    CHECK_EQ(s_applied, 1);

    outbind_t b = s_last;
    outbind_set_proto(&b, 4u);               /* DSHOT600 */
    picker_screen_set_binding(&b);
    render();

    const int before = s_applied;
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, before);             /* refused, and not applied */
    CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(4u)), 1);
}

TEST_CASE(a_release_somewhere_else_acts_not_at_all)
{
    fresh();
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));
    const int before = s_applied;

    /* Down on the button, up in the middle of the board. */
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    scr()->event(&d);
    touch_event_t u = { TOUCH_EVENT_UP, { 0, SCREEN_W / 2, SCREEN_H / 2, 40 }, 0u };
    scr()->event(&u);
    CHECK_EQ(s_applied, before);
}

/* A contact the tracker ends for a jump: its release is on the button it
 * pressed and the finger did not make it.  The pin stays as it was. */
TEST_CASE(a_release_the_tracker_makes_for_a_jump_binds_nothing)
{
    fresh();
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));
    const int before = s_applied;
    feed_reset();
    feed_to_screen(scr());
    finger(FEED_LONE, bx, by + UI_BAND_H);
    finger(FEED_LONE, SCREEN_W / 2, SCREEN_H / 2 + UI_BAND_H);
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK_EQ(s_applied, before);
    feed_tap(FEED_LONE, bx, by + UI_BAND_H);
    CHECK_EQ(s_applied, before + 1);
}

TEST_CASE(a_board_that_does_not_say_where_its_pads_are_is_not_drawn)
{
    /*
     * A board drawn from a guessed shape points at the wrong pad as
     * confidently as the right one, so a board with no shape gets no
     * drawing. The outputs screen still offers its pins.
     */
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4321u);            /* nothing knows this board */
    picker_screen_set_binding(&b);
    CHECK(!picker_screen_can_draw());
    render();                                /* draws the message, not a board */

    int bx, by;
    CHECK(!button_point(4u, &bx, &by));
    CHECK_EQ(s_applied, 0);

    /* The board this build knows can be drawn. */
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    picker_screen_set_binding(&b);
    CHECK(picker_screen_can_draw());
}

TEST_CASE(the_coprocessors_drawing_is_used_when_it_agrees_with_the_board)
{
    fresh();
    /* No bytes is no drawing, whatever pointer came with them. */
    const char *why = NULL;
    CHECK(!picker_screen_set_drawing(board_drawing_rp2350_can, 0u, &why));
    CHECK(why != NULL && why[0] != '\0');
    CHECK(!picker_screen_has_drawing());
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));       /* still usable */

    /* The served drawing agrees with the catalogue and is drawn. */
    fresh();
    CHECK(picker_screen_set_drawing(board_drawing_rp2350_can,
                                    board_drawing_rp2350_can_bytes, &why));
    CHECK_STR_EQ(why, "");
    CHECK(picker_screen_has_drawing());
    CHECK(picker_screen_can_draw());
    render();
    /* The buttons are wired to the same pins: the drawing's pads are the
     * shape's grid, to the hundredth of a millimetre. */
    CHECK(button_point(4u, &bx, &by));
    CHECK_EQ(outbind_group_of(&s_last, idx(4u)), 1);
}

TEST_CASE(the_buttons_are_where_they_are_with_and_without_the_drawing)
{
    const outbind_board_t *bd = outbind_board(BOARD);
    gfx_rect_t plain[OUTBIND_PINS];
    bool has[OUTBIND_PINS];
    fresh();
    for (uint8_t i = 0; i < bd->count; ++i) {
        has[i] = picker_screen_button_rect(i, &plain[i]);
    }
    fresh();
    CHECK(serve_drawing());
    for (uint8_t i = 0; i < bd->count; ++i) {
        gfx_rect_t r;
        CHECK_EQ(picker_screen_button_rect(i, &r), has[i]);
        if (has[i]) {
            CHECK_EQ(r.x, plain[i].x);
            CHECK_EQ(r.y, plain[i].y);
            CHECK_EQ(r.w, plain[i].w);
            CHECK_EQ(r.h, plain[i].h);
        }
    }
}

/* A copy of the served drawing to spoil. */
static uint8_t s_spoilt[LINK_DRAW_MAX_BYTES];

static uint8_t *spoil_pad(uint8_t pad)
{
    memcpy(s_spoilt, board_drawing_rp2350_can, board_drawing_rp2350_can_bytes);
    for (unsigned i = 0; i < s_spoilt[1]; ++i) {
        uint8_t *p = fx_pad_at(s_spoilt, i);
        if (p[0] == pad) { return p; }
    }
    return NULL;
}

TEST_CASE(a_drawing_that_disagrees_or_does_not_parse_is_not_shown)
{
    /*
     * Each refused whole, the board then drawn from its shape -- never a
     * drawing whose pads say one pin while the buttons bind another.
     */
    const char *why = NULL;

    /* GP2's pad drawn as GP3. */
    fresh();
    uint8_t *p = spoil_pad(4u);
    CHECK(p != NULL);
    p[2] = 3u;
    CHECK(!picker_screen_set_drawing(s_spoilt, board_drawing_rp2350_can_bytes,
                                     &why));
    CHECK_STR_EQ(why, "a pin's pad is not that GPIO in the drawing");
    CHECK(!picker_screen_has_drawing());
    CHECK(picker_screen_can_draw());          /* from its shape */
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));

    /* Truncated by one byte. */
    fresh();
    CHECK(!picker_screen_set_drawing(board_drawing_rp2350_can,
                                     board_drawing_rp2350_can_bytes - 1u,
                                     &why));
    CHECK(why[0] != '\0');
    CHECK(!picker_screen_has_drawing());

    /* A shape past the outline. */
    fresh();
    (void)spoil_pad(1u);
    uint8_t *sh = fx_shape_at(s_spoilt, s_spoilt[1], 0);
    fx_put16(sh + 4, 0xFFF0u);
    CHECK(!picker_screen_set_drawing(s_spoilt, board_drawing_rp2350_can_bytes,
                                     &why));
    CHECK(why[0] != '\0');
    CHECK(!picker_screen_has_drawing());

    /* A good drawing after a refused one is shown, and a refused one after
     * a good one takes it away. */
    CHECK(serve_drawing());
    CHECK(picker_screen_has_drawing());
    CHECK(!picker_screen_set_drawing(s_spoilt, board_drawing_rp2350_can_bytes,
                                     NULL));
    CHECK(!picker_screen_has_drawing());
}

TEST_CASE(a_drawing_is_shown_for_its_own_board_only)
{
    /* A board this build does not know gets no drawing, whatever is
     * served. */
    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4321u);
    picker_screen_set_binding(&b);
    const char *why = NULL;
    CHECK(!serve_drawing());
    CHECK(!picker_screen_set_drawing(board_drawing_rp2350_can,
                                     board_drawing_rp2350_can_bytes, &why));
    CHECK_STR_EQ(why, "a board this build does not know");
    CHECK(!picker_screen_has_drawing());
    CHECK(!picker_screen_can_draw());

    /* Taken for the board it describes, and dropped when the binding moves
     * to another board: a drawing is never shown for a board it was not
     * checked against. */
    fresh();
    CHECK(serve_drawing());
    outbind_init(&b);
    outbind_set_board(&b, 4321u);
    picker_screen_set_binding(&b);
    CHECK(!picker_screen_has_drawing());
    render();
}

TEST_CASE(the_tables_beside_the_board_say_what_the_pins_became)
{
    /*
     * The columns are what the board on its own cannot give: which lead is
     * channel 0. Rendered with pins bound on two protocols, so both the "this
     * one" and the "somebody else" paths draw.
     */
    fresh();
    outbind_t b = *picker_screen_binding();
    outbind_set_proto(&b, 1u);                     /* SERVO PWM */
    CHECK(outbind_toggle(&b, idx(0u)));
    CHECK(outbind_toggle(&b, idx(1u)));
    outbind_set_proto(&b, 4u);                     /* DSHOT600 */
    CHECK(outbind_toggle(&b, idx(6u)));
    outbind_set_proto(&b, 1u);
    picker_screen_set_binding(&b);
    render();

    /* The pins are held where the tables say they are: two here, one there. */
    CHECK_EQ(outbind_chosen(picker_screen_binding()), 2);
    CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 3);
    CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(6u)), 4);

    /* And the same board seen from the other protocol, which swaps which
     * column each pin is drawn in. */
    outbind_set_proto(&b, 4u);
    picker_screen_set_binding(&b);
    render();
    CHECK_EQ(outbind_chosen(picker_screen_binding()), 1);

    /* A protocol holding nothing draws the empty column rather than a gap. */
    outbind_set_proto(&b, 2u);                     /* PPM */
    picker_screen_set_binding(&b);
    render();
    CHECK_EQ(outbind_chosen(picker_screen_binding()), 0);
}

static int  s_asked;

static uint16_t s_asked_board;

static void drawing_source(uint16_t board)
{
    ++s_asked;
    s_asked_board = board;
    (void)picker_screen_set_drawing(NULL, 0u, NULL);
}

TEST_CASE(the_drawing_is_asked_for_on_the_way_in)
{
    /*
     * Asked on entry rather than held, because one that arrived since the
     * last visit should be the one shown -- as should none, for a board
     * that has been swapped.
     */
    fresh();
    s_asked = 0;
    picker_screen_set_drawing_source(drawing_source);
    outbind_t b = *picker_screen_binding();
    picker_screen_set_binding(&b);

    scr()->enter();
    CHECK_EQ(s_asked, 1);
    CHECK_EQ(s_asked_board, BOARD);
    render();

    scr()->leave();
    scr()->enter();
    CHECK_EQ(s_asked, 2);        /* asked again, not remembered */
    picker_screen_set_drawing_source(NULL);
}

TEST_CASE(a_binding_for_another_board_asks_for_its_drawing)
{
    /*
     * A link-up can publish the drawing before the binding that names its
     * board is read; the picker refuses it then, as it names the board
     * before.  The binding naming the new board asks again, so an open
     * picker does not stay on the outline until it is re-entered.
     */
    fresh();
    s_asked = 0;
    picker_screen_set_drawing_source(drawing_source);
    outbind_t b = *picker_screen_binding();
    picker_screen_set_binding(&b);
    CHECK_EQ(s_asked, 0);              /* the same board: nothing to ask */

    outbind_t other = b;
    other.board = (uint16_t)(BOARD + 1u);
    picker_screen_set_binding(&other);
    CHECK_EQ(s_asked, 1);
    CHECK_EQ(s_asked_board, (uint16_t)(BOARD + 1u));

    picker_screen_set_binding(&b);
    CHECK_EQ(s_asked, 2);
    CHECK_EQ(s_asked_board, BOARD);

    picker_screen_set_drawing_source(NULL);
    picker_screen_set_binding(&other);  /* no source: nothing is called */
    CHECK_EQ(s_asked, 2);
    picker_screen_set_binding(&b);
}

TEST_CASE(a_ground_is_marked_and_is_not_a_button)
{
    /*
     * A servo lead needs a ground, so the screen says where one is. What it
     * must not do is offer it: a ground is not a pin an output may have, and
     * a press there binds nothing.
     */
    fresh();
    render();

    const outbind_pad_t *pads = outbind_pads(BOARD);
    const uint8_t n = outbind_pad_count(BOARD);
    if (pads == NULL || n == 0u) {
        T_FAIL("the board describes no grounds or rails");
        return;
    }
    const outbind_board_t *bd = outbind_board(BOARD);
    if (bd == NULL || bd->shape == NULL) {
        T_FAIL("the board this build knows has no shape");
        return;
    }

    unsigned pressed = 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint16_t xc, yc;
        if (!outbind_pad_xy(bd->shape, pads[i].pad, &xc, &yc)) { continue; }
        int bw = 0, bh = 0;
        picker_board_fit(bd->shape->width_cmm, bd->shape->height_cmm, &bw,
                         &bh);
        const int bx = (SCREEN_W - bw) / 2;
        const int by = (PK_FOOT - bh) / 2;
        const int px = bx + (int)(((uint32_t)xc * (uint32_t)bw)
                                 / bd->shape->width_cmm);
        const int py = by + (int)(((uint32_t)yc * (uint32_t)bh)
                                  / bd->shape->height_cmm);
        touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)px, (int16_t)py, 40 }, 0u };
        touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)px, (int16_t)py, 40 }, 0u };
        scr()->event(&d);
        scr()->event(&u);
        ++pressed;
    }
    CHECK(pressed > 0);
    CHECK_EQ(s_applied, 0);          /* none of them bound anything */
    CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 0);
}

TEST_CASE(a_board_that_does_not_say_which_pads_are_ground_still_draws)
{
    /*
     * The marks are the last thing to arrive and the first a board may not
     * have. Without them the screen is what it was: a board, its pins, and a
     * lead placed by reading the outline.
     *
     * The board this build knows does say, so this learns one that does not:
     * a catalogue and a shape and no pad list, which is what an IOmcu
     * built before the page answers with.
     */
    const uint16_t bare = 7777u;
    uint16_t cat[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { cat[i] = 0u; }
    cat[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    cat[1] = LINK_CAT_OF(1, 2, LINK_PIN_FREE);
    cat[2] = LINK_CAT_OF(4, 6, LINK_PIN_FREE);
    CHECK(outbind_learn_board(bare, cat));
    uint16_t sh[LINK_SH_COUNT];
    outbind_shape_to_regs(outbind_board(BOARD), sh);
    CHECK(outbind_learn_shape(bare, sh));
    CHECK(outbind_pads(bare) == NULL);          /* the case under test */

    fresh();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, bare);
    outbind_set_proto(&b, 1u);
    picker_screen_set_binding(&b);
    CHECK(picker_screen_can_draw());
    render();

    int bx, by;
    CHECK(button_point_on(bare, 4u, &bx, &by));
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, outbind_index_of(bare, 4u)), 1);
    outbind_forget_learned();
}

TEST_CASE(the_marks_do_not_get_in_the_way_of_the_buttons)
{
    /*
     * With the marks drawn, every pin the board brings out is still bound by
     * its own button: a chip drawn over a trace must not move where a button
     * is or which pin it belongs to.
     */
    const outbind_board_t *bd = outbind_board(BOARD);
    CHECK(outbind_pads(BOARD) != NULL);
    for (uint8_t i = 0; i < bd->count; ++i) {
        if (bd->pins[i].reserved) { continue; }
        fresh();
        render();
        int bx, by;
        const uint8_t gp = bd->pins[i].gpio;
        if (!button_point(gp, &bx, &by)) {
            T_FAIL("GP%u has no button with the marks drawn", gp);
            return;
        }
        if (outbind_group_of(&s_last, i) != 1u
            || outbind_chosen(&s_last) != 1u) {
            T_FAIL("GP%u's button bound something else with the marks drawn",
                   gp);
            return;
        }
    }
}

TEST_CASE(null_events_are_refused_rather_than_dereferenced)
{
    fresh();
    scr()->event(NULL);
    picker_screen_set_binding(NULL);
    scr()->enter();
    scr()->leave();
    render();
    CHECK_EQ(s_applied, 0);
}

/*
 * The picker can add a pin after the operator has chosen a protocol on the
 * outputs screen, with nothing bound anywhere.
 *
 * The protocol belongs to the outputs screen: this screen has no control for
 * one and only reads it to know which group a tap joins.  So the panel hands
 * the picker what the outputs screen reconciled, not what came off the wire --
 * an empty page renders as OFF, and a picker left on OFF refuses every pin.
 *
 * This walks the sequence firmware/panel/main/main.c performs, which is not
 * itself in this suite.
 */
TEST_CASE(the_picker_follows_the_protocol_chosen_on_the_outputs_screen)
{
    fresh();

    /* What the operator picked, on the other screen. */
    outbind_t picked;
    outbind_init(&picked);
    outbind_set_board(&picked, BOARD);
    outbind_set_proto(&picked, 4u);              /* DSHOT600 */
    outputs_screen_set_binding(&picked);

    /* What the IOmcu answers with an empty store: OFF, nothing bound. */
    outbind_t empty;
    outbind_init(&empty);
    outbind_set_board(&empty, BOARD);
    CHECK_EQ((int)outbind_chosen_total(&empty), 0);
    CHECK_EQ((int)empty.proto, 0);

    /* main.c: the outputs screen first, then the picker from its result. */
    outputs_screen_set_binding(&empty);
    picker_screen_set_binding(outputs_screen_binding());

    CHECK_EQ((int)picker_screen_binding()->proto, 4);
    outbind_t b = *picker_screen_binding();
    CHECK(outbind_can_add(&b, idx(4u)));
}

/*
 * A binding the outputs screen holds unconfirmed -- a read failed, or the
 * pages describe no binding -- is not edited here either: every button is
 * drawn as not offered, NOT READ stands in the left column, and a tap
 * changes nothing and calls nothing.  The press that was down when the
 * state changed does not act on its release.
 */
TEST_CASE(an_unconfirmed_binding_takes_no_tap)
{
    fresh();
    render();
    int bx, by;
    if (!button_point(4u, &bx, &by)) { T_FAIL("GP4 has no button"); }
    CHECK_EQ(s_applied, 1);
    CHECK(picker_screen_editable());
    const outbind_t before = *picker_screen_binding();

    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 }, 0u };
    scr()->event(&d);
    picker_screen_follow(1u, false);
    scr()->event(&u);
    CHECK(!picker_screen_editable());
    CHECK_EQ(s_applied, 1);

    render();                                   /* NOT READ, buttons grey */
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, 1);
    CHECK(memcmp(&before, picker_screen_binding(), sizeof(before)) == 0);

    /* Told the same again: nothing to repaint, nothing changes. */
    picker_screen_follow(1u, false);
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, 1);

    /* Confirmed again: the same tap acts. */
    picker_screen_follow(1u, true);
    render();
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, 2);
}

int main(void)
{
    RUN(an_unconfirmed_binding_takes_no_tap);
    RUN(the_picker_follows_the_protocol_chosen_on_the_outputs_screen);
    RUN(a_button_binds_the_pad_it_is_wired_to);
    RUN(every_button_binds_its_own_pin_and_no_other);
    RUN(a_reserved_pin_has_no_button_to_press);
    RUN(a_pin_another_protocol_holds_is_not_this_ones_to_take);
    RUN(a_release_somewhere_else_acts_not_at_all);
    RUN(a_board_that_does_not_say_where_its_pads_are_is_not_drawn);
    RUN(the_coprocessors_drawing_is_used_when_it_agrees_with_the_board);
    RUN(the_buttons_are_where_they_are_with_and_without_the_drawing);
    RUN(a_drawing_that_disagrees_or_does_not_parse_is_not_shown);
    RUN(a_drawing_is_shown_for_its_own_board_only);
    RUN(the_tables_beside_the_board_say_what_the_pins_became);
    RUN(the_drawing_is_asked_for_on_the_way_in);
    RUN(a_binding_for_another_board_asks_for_its_drawing);
    RUN(a_ground_is_marked_and_is_not_a_button);
    RUN(a_board_that_does_not_say_which_pads_are_ground_still_draws);
    RUN(the_marks_do_not_get_in_the_way_of_the_buttons);
    RUN(null_events_are_refused_rather_than_dereferenced);
    RUN(a_cancelled_press_toggles_nothing);
    RUN(a_release_the_tracker_makes_for_a_jump_binds_nothing);
    return test_summary("picker_screen");
}
