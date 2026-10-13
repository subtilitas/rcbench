/*
 * The pin picker: a board with a button on every pin an output may have.
 *
 * The screen owns no rules -- out_bind does -- so what is under test is the
 * touching and the geometry. The geometry is the part that can be wrong in a
 * way nobody sees: a button wired to the pad next to its own would let an
 * operator press GP4 and bind GP5, and the board would look right.
 *
 * So the tests here do not hard-code where a button is. They ask the board
 * where a pad is, the way the screen does, and press there.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "drawing_fixture.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "touch_feed.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)

static int         s_applied;
static outbind_t   s_last;
static gfx_color_t s_px[800 * 432];

/* firmware/iomcu/src/board_drawing_rp2350_can.c: the drawing the
 * coprocessor serves for this board. */
extern const uint8_t  board_drawing_rp2350_can[];
extern const uint32_t board_drawing_rp2350_can_bytes;

/* Show the served drawing; whether the picker took it. */
static bool serve_drawing(void)
{
    return picker_screen_set_drawing(board_drawing_rp2350_can,
                                     board_drawing_rp2350_can_bytes, NULL);
}

static void on_apply(const outbind_t *b) { ++s_applied; s_last = *b; }

static const ui_screen_t *scr(void) { return picker_screen(); }

static uint8_t idx(uint8_t gpio) { return outbind_index_of(BOARD, gpio); }

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    scr()->reset();
    s_applied = 0;
    picker_screen_set_apply(on_apply);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, 1u);              /* SERVO PWM */
    picker_screen_set_binding(&b);
    picker_screen_set_drawing(NULL, 0u, NULL);  /* the board from its shape */
    scr()->enter();
}

static void render(void)
{
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    scr()->render(&c, 0);
}

/*
 * Where the screen puts a pad, worked out the way the screen does: from the
 * board's own shape. A test that repeated the arithmetic would pass with the
 * screen's copy of it wrong in the same way.
 */
static bool pad_point_on(uint16_t board, uint8_t gpio, int *x, int *y)
{
    const outbind_board_t *bd = outbind_board(board);
    const uint8_t i = outbind_index_of(board, gpio);
    if (bd == NULL || bd->shape == NULL || i >= bd->count) { return false; }
    uint16_t xc, yc;
    if (!outbind_pad_xy(bd->shape, bd->pins[i].pad, &xc, &yc)) { return false; }
    const int bw = 500;
    const int bh = (int)(((uint32_t)bw * bd->shape->height_cmm)
                         / bd->shape->width_cmm);
    const int bx = (SCREEN_W - bw) / 2;
    const int by = (SCREEN_H - bh) / 2;
    /* Rounded to the nearest pixel, each from its own position. */
    *x = bx + (int)(((uint32_t)xc * (uint32_t)bw + bd->shape->width_cmm / 2u)
                    / bd->shape->width_cmm);
    *y = by + (int)(((uint32_t)yc * (uint32_t)bh + bd->shape->height_cmm / 2u)
                    / bd->shape->height_cmm);
    return true;
}

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
        const int bw = 500;
        const int bh = (int)(((uint32_t)bw * bd->shape->height_cmm)
                             / bd->shape->width_cmm);
        const int bx = (SCREEN_W - bw) / 2;
        const int by = (SCREEN_H - bh) / 2;
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
     * a catalogue and a shape and no pad list, which is what a coprocessor
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

    /* What the coprocessor answers with an empty store: OFF, nothing bound. */
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


/* ------------------------------------------------ the settled design */

static void tap(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
}

static void tap_rect(gfx_rect_t r) { tap(r.x + r.w / 2, r.y + r.h / 2); }

/* By name, so an entry added to the catalogue does not retarget a test. */
static uint8_t proto_named(const char *name)
{
    const outbind_proto_t *k = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(k[i].name, name) == 0) { return i; }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0u;
}

/* A pick in the screen's own list: the face, then the row. */
static void pick(const char *name)
{
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
    tap_rect(picker_screen_list_row(proto_named(name)));
    CHECK(!picker_screen_list_open());
}

/* Pose the screen with @p gpios bound to @p proto, on top of what it holds. */
static void bind_more(const char *proto, const uint8_t *gpios, unsigned n,
                      const char *selected)
{
    outbind_t b = *picker_screen_binding();
    outbind_set_proto(&b, proto_named(proto));
    for (unsigned i = 0; i < n; ++i) {
        if (!outbind_toggle(&b, idx(gpios[i]))) {
            T_FAIL("%s refused GP%u", proto, gpios[i]);
        }
    }
    outbind_set_proto(&b, proto_named(selected));
    picker_screen_set_binding(&b);
}

static const char *reason(void)
{
    static char buf[64];
    (void)picker_screen_reason(buf, sizeof(buf));
    return buf;
}

static const char *count(void)
{
    static char buf[64];
    picker_screen_count(buf, sizeof(buf));
    return buf;
}

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w
           && a.y < b.y + b.h && b.y < a.y + a.h;
}

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
        char who[24];

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

        /* A slot that holds the pin keeps it -- the coprocessor gives the
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

/*
 * Every button against everything else on the screen, with the
 * coprocessor's drawing and with the board drawn from its shape (both 205 px
 * tall): inside the
 * screen, centred on its own pad unless the screen edge stops it, clear of
 * every other button, of every marker inside the board and of the text
 * areas, and a tap on its centre binds its own pin and no other.
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
                || r.y + r.h > SCREEN_H || r.w < 43 || r.h < 40) {
                T_FAIL("pass %d: GP%u's button %d,%d %dx%d", pass, gp, r.x,
                       r.y, r.w, r.h);
            }
            int px = 0, py = 0;
            CHECK(pad_point_on(BOARD, (uint8_t)gp, &px, &py));
            const int mid = r.x + r.w / 2;
            if ((mid - px > 1 || px - mid > 1) && r.x > 2
                && r.x + r.w < SCREEN_W - 2) {
                T_FAIL("pass %d: GP%u's button is off its pad", pass, gp);
            }
            for (uint8_t o = (uint8_t)(i + 1u); o < bd->count; ++o) {
                if (has[o] && overlap(r, btn[o])) {
                    T_FAIL("pass %d: GP%u and GP%u overlap", pass, gp,
                           bd->pins[o].gpio);
                }
            }
            for (uint8_t m = 0; m < pads; ++m) {
                if (mk_has[m] && overlap(r, mk[m])) {
                    T_FAIL("pass %d: GP%u covers pad %u's marker", pass, gp,
                           outbind_pads(BOARD)[m].pad);
                }
            }
            for (int a = 0; a < 3; ++a) {
                if (overlap(r, areas[a])) {
                    T_FAIL("pass %d: GP%u covers text area %d", pass, gp, a);
                }
            }
            if (overlap(r, picker_screen_list_face())) {
                T_FAIL("pass %d: GP%u covers the protocol list", pass, gp);
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
                T_FAIL("pass %d: GP%u's centre bound something else", pass,
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
            const picker_pin_t want_st =
                bd->pins[i].reserved ? PICKER_PIN_RESERVED
                : (g == proto_named("SERVO PWM")) ? PICKER_PIN_TICKED
                : (g != 0u) ? PICKER_PIN_HELD : PICKER_PIN_REFUSED;
            if (st != want_st) {
                T_FAIL("GP%u is %d, want %d", bd->pins[i].gpio, (int)st,
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
            T_FAIL("GP%u is offered under PPM", bd->pins[i].gpio);
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
        char who[24];
        gfx_rect_t r;
        CHECK_EQ(picker_screen_pin(i, who, sizeof(who)), PICKER_PIN_RESERVED);
        CHECK_STR_EQ(who, bd->pins[i].held_by);
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
    char who[24];
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
 * OFF picked on this screen ends when the screen is left, as it does on the
 * Outputs screen.  Kept there, the panel's follow selected it here again
 * every frame, a read-back kept it, and the next visit opened on a board
 * where no key added a pin.
 */
TEST_CASE(off_picked_here_ends_when_the_screen_is_left)
{
    fresh();
    static const uint8_t dshot[2] = { 6, 7 };
    bind_more("DSHOT300", dshot, 2u, "SERVO PWM");
    outputs_screen()->reset();
    outputs_screen_set_binding(picker_screen_binding());
    picker_screen_set_select(outputs_screen_select);

    pick("OFF");
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_REFUSED);

    scr()->leave();
    /* A frame on another screen follows the outputs screen, and a read-back
     * arrives naming the protocol the pins name, as one off the wire does. */
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    outbind_t b = *outputs_screen_binding();
    outbind_set_proto(&b, outbind_wire_proto(&b));
    outputs_screen_set_binding(&b);
    picker_screen_set_binding(outputs_screen_binding());
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());

    scr()->enter();
    CHECK_EQ(outputs_screen_binding()->proto, proto_named("DSHOT300"));
    CHECK_EQ(picker_screen_binding()->proto, proto_named("DSHOT300"));
    CHECK_STR_EQ(reason(), "");
    CHECK_EQ(picker_screen_pin(idx(5u), NULL, 0), PICKER_PIN_FREE);
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(idx(5u), &r));
    tap_rect(r);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_group_of(&s_last, idx(5u)), proto_named("DSHOT300"));

    /* Another protocol picked here is still picked after a leave. */
    pick("MOTOR PWM");
    scr()->leave();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(outputs_screen_binding()->proto, proto_named("MOTOR PWM"));
    CHECK_EQ(picker_screen_binding()->proto, proto_named("MOTOR PWM"));

    /* With no pin bound OFF is what a leave gives, on both screens, as it
     * is on the Outputs screen. */
    fresh();
    outputs_screen()->reset();
    outputs_screen_set_binding(picker_screen_binding());
    picker_screen_set_select(outputs_screen_select);
    pick("OFF");
    scr()->leave();
    CHECK_EQ(outputs_screen_binding()->proto, 0);
    CHECK_EQ(picker_screen_binding()->proto, 0);
    picker_screen_set_select(NULL);
}

/*
 * The tallest IN USE table a binding gives: OFF selected, so every bound pin
 * is another protocol's; the programming port listed; and the 8 slots spread
 * over the 6 protocols that take one channel a pin.  Every pin is listed
 * once under its own protocol, and every line is inside the table and the
 * canvas, clear of every other line, key, marker and text area, and drawn
 * on -- with the coprocessor's drawing and with the board drawn from its
 * shape, in both languages.
 */
TEST_CASE(the_in_use_table_lists_every_pin_of_the_fullest_binding)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
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
        for (int pass = 0; pass < 2; ++pass) {
            fresh();
            if (pass == 1) { CHECK(serve_drawing()); }
            picker_screen_set_prog_pin(13u);
            for (unsigned g = 0; g < 6u; ++g) {
                bind_more(spread[g].proto, spread[g].gp, spread[g].n, "OFF");
            }
            const outbind_t *b = picker_screen_binding();
            CHECK_EQ(b->proto, 0);
            CHECK_EQ(outbind_chosen_total(b), 8);
            CHECK_EQ(picker_screen_prog_listed(), 13u);

            guard_canvas_t g;
            gfx_canvas_t c;
            guard_canvas_init(&g, &c, px, 800, 432);
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
            CHECK_EQ(lines, 16);      /* the port's 2, 6 names, 8 pins */

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
                    CHECK_EQ(line[k].gpio, 13u);
                    CHECK_EQ(line[k].channel, -1);
                } else {
                    CHECK_EQ(outbind_group_of(b, idx(line[k].gpio)), group);
                    CHECK(line[k].channel >= 0);
                }
                found |= 1u << line[k].gpio;
            }
            CHECK_EQ(found, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4)
                            | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 13)
                            | (1u << 14));

            const gfx_rect_t area = picker_screen_area(PICKER_AREA_IN_USE);
            const gfx_rect_t other[3] = {
                picker_screen_area(PICKER_AREA_HEAD),
                picker_screen_area(PICKER_AREA_PINS),
                picker_screen_list_face(),
            };
            const outbind_board_t *bd = outbind_board(BOARD);
            for (uint8_t k = 0; k < lines; ++k) {
                const gfx_rect_t r = line[k].rect;
                CHECK(line[k].compact);
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
            picker_screen_set_prog_pin(PICKER_NO_PIN);
        }
    }
    ui_text_set_language(UI_LANG_EN);

    /* A table that fits in 28 px pin rows keeps them, with the GPIO in the
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
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
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
            guard_canvas_init(&g, &c, px, 800, 432);
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

int main(void)
{
    RUN(an_unconfirmed_binding_takes_no_tap);
    RUN(off_picked_here_ends_when_the_screen_is_left);
    RUN(the_in_use_table_lists_every_pin_of_the_fullest_binding);
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
    RUN(a_ground_is_marked_and_is_not_a_button);
    RUN(a_board_that_does_not_say_which_pads_are_ground_still_draws);
    RUN(the_marks_do_not_get_in_the_way_of_the_buttons);
    RUN(null_events_are_refused_rather_than_dereferenced);
    RUN(a_cancelled_press_toggles_nothing);
    RUN(a_release_the_tracker_makes_for_a_jump_binds_nothing);
    RUN(the_programming_ports_pin_is_held_and_named);
    RUN(every_button_is_clear_of_everything_else);
    RUN(a_full_bench_takes_no_ninth_pin_and_says_why);
    RUN(ppm_greys_the_board_and_the_reason_says_why);
    RUN(every_reserved_pin_is_named_and_has_no_button);
    RUN(switching_the_protocol_keeps_the_pins_and_writes_nothing);
    RUN(the_pins_table_names_each_pins_channel_and_pad);
    RUN(every_state_renders_inside_its_canvas);
    RUN(queries_past_the_end_answer_nothing);
    return test_summary("picker_screen");
}
