/*
 * The pin picker: a board with a button on every pin an output may have.
 *
 * The screen owns no rules -- out_bind does -- so what is under test is the
 * touching and the geometry. The geometry is the part that can be wrong in a
 * way nobody sees: a button wired to the pad next to its own would let an
 * operator press GP4 and bind GP5, and the picture would look right.
 *
 * So the tests here do not hard-code where a button is. They ask the board
 * where a pad is, the way the screen does, and press there.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "picker_screen.h"
#include "ui_theme.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)

static int         s_applied;
static outbind_t   s_last;
static gfx_color_t s_px[800 * 432];

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
    picker_screen_set_artwork(NULL, 0, 0);  /* the drawn board */
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
static bool pad_point(uint8_t gpio, int *x, int *y)
{
    const outbind_board_t *bd = outbind_board(BOARD);
    const uint8_t i = idx(gpio);
    if (bd == NULL || bd->shape == NULL || i >= bd->count) { return false; }
    uint16_t xc, yc;
    if (!outbind_pad_xy(bd->shape, bd->pins[i].pad, &xc, &yc)) { return false; }
    const int bw = 500;
    const int bh = (int)(((uint32_t)bw * bd->shape->height_cmm)
                         / bd->shape->width_cmm);
    const int bx = (SCREEN_W - bw) / 2;
    const int by = (SCREEN_H - bh) / 2;
    *x = bx + (int)(((uint32_t)xc * (uint32_t)bw) / bd->shape->width_cmm);
    *y = by + (int)(((uint32_t)yc * (uint32_t)bh) / bd->shape->height_cmm);
    return true;
}

/* The button belonging to a pad is on the pad's own side, straight out from
 * it. Walking away from the board finds it without knowing the tiers. */
static bool button_point(uint8_t gpio, int *bxp, int *byp)
{
    int px, py;
    if (!pad_point(gpio, &px, &py)) { return false; }
    const int mid = SCREEN_H / 2;
    const int step = (py < mid) ? -1 : 1;
    for (int y = py + step * 20; y > 0 && y < SCREEN_H; y += step * 2) {
        touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)px, (int16_t)y, 40 } };
        scr()->event(&d);
        touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)px, (int16_t)y, 40 } };
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
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 } };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 } };
    scr()->event(&d);
    scr()->event(&u);
    CHECK_EQ(s_applied, 2);
    CHECK_EQ(outbind_chosen(&s_last), 0);
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
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 } };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)bx, (int16_t)by, 40 } };
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
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)bx, (int16_t)by, 40 } };
    scr()->event(&d);
    touch_event_t u = { TOUCH_EVENT_UP, { 0, SCREEN_W / 2, SCREEN_H / 2, 40 } };
    scr()->event(&u);
    CHECK_EQ(s_applied, before);
}

TEST_CASE(a_board_that_does_not_say_where_its_pads_are_is_not_drawn)
{
    /*
     * A picture drawn from a guessed shape points at the wrong pad as
     * confidently as the right one, so a board with no shape gets no
     * picture. The outputs screen still offers its pins.
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

TEST_CASE(a_photograph_is_used_when_there_is_one_and_not_when_there_is_not)
{
    fresh();
    /* Artwork of no size is no artwork, whatever pointer came with it. */
    static const gfx_color_t px[4] = { 0, 0, 0, 0 };
    picker_screen_set_artwork(px, 0, 0);
    render();
    int bx, by;
    CHECK(button_point(4u, &bx, &by));       /* still usable */

    /* And with one, the buttons are still wired to the same pins: the
     * picture is cropped to the outline, so it changes what is under the
     * pads and not where they are. */
    fresh();
    static gfx_color_t art[500 * 206];
    for (unsigned i = 0; i < 500u * 206u; ++i) { art[i] = 0x1234u; }
    picker_screen_set_artwork(art, 500, 206);
    render();
    CHECK(button_point(4u, &bx, &by));
    CHECK_EQ(outbind_group_of(&s_last, idx(4u)), 1);
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

int main(void)
{
    RUN(a_button_binds_the_pad_it_is_wired_to);
    RUN(every_button_binds_its_own_pin_and_no_other);
    RUN(a_reserved_pin_has_no_button_to_press);
    RUN(a_pin_another_protocol_holds_is_not_this_ones_to_take);
    RUN(a_release_somewhere_else_acts_not_at_all);
    RUN(a_board_that_does_not_say_where_its_pads_are_is_not_drawn);
    RUN(a_photograph_is_used_when_there_is_one_and_not_when_there_is_not);
    RUN(null_events_are_refused_rather_than_dereferenced);
    return test_summary("picker_screen");
}
