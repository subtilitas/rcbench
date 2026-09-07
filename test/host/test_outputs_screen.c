/*
 * The outputs screen: a protocol list that opens, and a pin grid that ticks.
 *
 * The screen owns no rules -- out_bind does -- so what is under test here is
 * the touching: that a press and a release on the same thing acts once, that
 * a release somewhere else acts not at all, that an open list can be left
 * without choosing, and that a refused pin does not reach the apply seam.
 * The last one matters most: the application writes the wire from that seam,
 * so a call it should not have received is a page write nobody asked for.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "outputs_screen.h"
#include "ui_theme.h"

/* Geometry the screen draws to; a test that hard-codes it is a test that
 * notices when the layout moves under the hit testing. */
#define COL_X  16
#define DD_X   16
#define DD_Y   44
#define DD_W   208
#define DD_H   54
#define POP_ROW 46
#define GRID_X 236
#define GRID_Y 16
#define CELL_GAP 6
#define CELL_W ((548 - 3 * CELL_GAP) / 4)
#define CELL_H 53
#define GRID_ROWS 7

static int s_applied;
static outbind_t s_last;

static void on_apply(const outbind_t *b)
{
    ++s_applied;
    s_last = *b;
}

static const ui_screen_t *scr(void) { return outputs_screen(); }

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    outputs_screen_set_apply(on_apply);
    scr()->reset();
    outputs_screen_set_apply(on_apply);   /* reset clears the seam */
    {   /* the screen shows nothing until it is told which board answered */
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
        outputs_screen_set_binding(&b);
    }
    s_applied = 0;
}

static void tap(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 } };
    scr()->event(&d);
    scr()->event(&u);
}

static void press_at(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    scr()->event(&d);
}

static void release_at(int x, int y)
{
    touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)x, (int16_t)y, 40 } };
    scr()->event(&u);
}

static void cell_centre(uint8_t gpio, int *x, int *y)
{
    const uint8_t i = outbind_index_of(OUTBIND_BOARD_PICO_HEADER, gpio);
    const int col = i / GRID_ROWS, row = i % GRID_ROWS;
    *x = GRID_X + col * (CELL_W + CELL_GAP) + CELL_W / 2;
    *y = GRID_Y + row * (CELL_H + CELL_GAP) + CELL_H / 2;
}

static void choose_proto(int index)
{
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open  */
    tap(DD_X + 40, DD_Y + 4 + index * POP_ROW + POP_ROW / 2);
}

/* By name, because the row a protocol sits on is the catalogue's business:
 * an entry added between two others must not silently retarget a test at a
 * protocol it was not written for. */
static int proto_row(const char *name)
{
    const outbind_proto_t *k = outbind_protos();
    for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
        if (strcmp(k[i].name, name) == 0) {
            return i;
        }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0;
}

static void choose_named(const char *name) { choose_proto(proto_row(name)); }

static void tap_pin(uint8_t gpio)
{
    int x, y;
    cell_centre(gpio, &x, &y);
    tap(x, y);
}

/* --------------------------------------------------------------- the list */

TEST_CASE(the_protocol_list_opens_and_a_choice_closes_it)
{
    fresh();
    choose_named("SERVO PWM");
    CHECK_EQ(outputs_screen_binding()->proto, 1);
    CHECK_EQ(s_applied, 1);

    /* And it is shut: a tap where a pin cell is now works as a pin again. */
    tap_pin(0);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
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

/* --------------------------------------------------------------- rendering */

TEST_CASE(every_state_renders_without_reading_off_the_canvas)
{
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

    fresh();
    for (int p = 0; p < (int)OUTBIND_PROTOS; ++p) {
        choose_proto(p);
        for (uint8_t g = 0; g < 29u; ++g) {
            if (outbind_index_of(OUTBIND_BOARD_PICO_HEADER, g) < outbind_pin_count(OUTBIND_BOARD_PICO_HEADER)) {
                tap_pin(g);
            }
        }
        scr()->render(&c, 0);
        /* And with the list open over the top of whatever was chosen. */
        tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
        scr()->render(&c, 1);
        tap(700, 400);
    }
    for (int r = 0; r <= (int)OUTPUTS_REFUSED; ++r) {
        outputs_screen_set_result((outputs_result_t)r);
        scr()->render(&c, 2);
    }
}

/* The strip under the protocol where the reason is drawn, as a sum: a line
 * there is ink, no line is the background. */
static unsigned reason_ink(gfx_canvas_t *c)
{
    unsigned long sum = 0;
    for (int y = DD_Y + DD_H + 56; y < DD_Y + DD_H + 76; ++y) {
        for (int x = COL_X; x < COL_X + 210; ++x) {
            sum += (unsigned long)c->pixels[y * c->stride + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

TEST_CASE(a_board_that_can_take_nothing_says_why)
{
    /*
     * The rules refusing every pin are out_bind's and they are right; what
     * is under test is that the screen states them. A board drawn entirely
     * in grey with nothing beside it reads as a fault, and an operator
     * reported it as one.
     */
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

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
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

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
    outbind_set_proto(&b, 4);                     /* DSHOT600 */
    (void)outbind_toggle(&b, outbind_index_of(OUTBIND_BOARD_PICO_HEADER, 7));
    outputs_screen_set_binding(&b);

    /* What was loaded from storage is what the screen now shows and edits. */
    CHECK_EQ(outputs_screen_binding()->proto, 4);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 1);
    tap_pin(13);
    CHECK_EQ(outbind_chosen(outputs_screen_binding()), 2);
}

TEST_CASE(a_protocol_index_from_outside_cannot_run_off_the_table)
{
    /* The binding arrives from the wire and from flash, so the index is not
     * this screen's to trust.  Reading past the protocol table would render
     * a name from whatever followed it. */
    static gfx_color_t px[800 * 432];
    gfx_canvas_t c = { px, 800, 432, 800, { 0, 0, 800, 432 } };

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

int main(void)
{
    RUN(the_protocol_list_opens_and_a_choice_closes_it);
    RUN(an_open_list_can_be_left_without_choosing);
    RUN(a_release_away_from_the_press_does_nothing);
    RUN(ticking_a_pin_applies_once_and_unticking_applies_again);
    RUN(a_reserved_pin_never_reaches_the_apply_seam);
    RUN(a_pin_too_many_does_not_apply);
    RUN(nothing_can_be_ticked_while_the_protocol_is_off);
    RUN(every_state_renders_without_reading_off_the_canvas);
    RUN(a_board_that_can_take_nothing_says_why);
    RUN(a_protocol_that_has_all_its_pins_says_so);
    RUN(the_binding_survives_being_set_from_outside);
    RUN(a_protocol_index_from_outside_cannot_run_off_the_table);
    RUN(null_events_are_refused_rather_than_dereferenced);
    return test_summary("outputs_screen");
}
