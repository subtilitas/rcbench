/*
 * What the programmer decides: that you say what you are programming before
 * anything is read, that stepping up a level cannot leave a stale connection
 * behind, and that a stepper stops rather than wraps.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "programmer_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* Mirrored from programmer_screen.c; if the layout moves these move with it. */
#define TILE_CX(i) (18 + (i) * 390 + 187)
#define TILE_CY    180
#define BACK_X     66
#define BACK_Y     27
#define ROW_CX     400
#define ROW_CY(n)  (64 + (n) * 68 + 29)
#define CONNECT_X  698
#define CONNECT_Y  70
#define STEP_DN_X  653
#define STEP_UP_X  765
#define STEP_CY(i) (132 + (i) * 30 + 10)
#define READ_X     516
#define WRITE_X    698
#define BTN_CY     407
#define PAGE_UP_X  723
#define PAGE_DN_X  761
#define PAGE_CY    116

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = programmer_screen();
    scr->reset();
}

static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

static int lit(void)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != ui_theme_color(UI_C_BG)) { ++n; }
    }
    return n;
}

/* ESC (electronic speed controller) then BLHeli_S then CONNECT: the whole
 * descent. */
static void descend_to_blheli(void)
{
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    tap(CONNECT_X, CONNECT_Y);
}

/*
 * Pins: the protocol rows accept a press before the page is rendered.  Hit
 * rectangles are laid out independently of drawing, so the press here comes
 * with no render at all.
 */
TEST_CASE(the_protocol_list_is_pressable_before_it_is_painted)
{
    fresh();
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(1));                 /* AM32, never rendered */
    tap(CONNECT_X, CONNECT_Y);
    CHECK(programmer_screen_connected());
    CHECK_EQ(programmer_screen_protocol(), 1);
}

/* A class shows its own protocols and no others. */
TEST_CASE(a_class_shows_only_its_own_protocols)
{
    fresh();
    tap(TILE_CX(1), TILE_CY);               /* SERVO: one protocol */
    /* The fourth row does not exist in this class, so pressing where it
     * would be in the other one must do nothing. */
    tap(ROW_CX, ROW_CY(3));
    CHECK(!programmer_screen_connected());
    tap(CONNECT_X, CONNECT_Y);
    CHECK(!programmer_screen_connected());  /* still on the list */

    tap(ROW_CX, ROW_CY(0));                 /* Hitec, the only servo one */
    CHECK_EQ(programmer_screen_protocol(), 4);
}

/*
 * The reason for the hierarchy: the device that answered a one-wire
 * bootloader is not the device that answers a CLI (command-line interface),
 * and the screen must never show one identity above the other's parameters.
 * Stepping back up drops the connection.
 */
TEST_CASE(stepping_back_up_drops_the_connection)
{
    fresh();
    descend_to_blheli();
    CHECK(programmer_screen_connected());

    tap(BACK_X, BACK_Y);                    /* to the protocol list */
    CHECK(!programmer_screen_connected());

    tap(ROW_CX, ROW_CY(2));                 /* AM32 */
    CHECK_EQ(programmer_screen_protocol(), 2);
    CHECK(!programmer_screen_connected());
}

/* Back is a step up the hierarchy, not out of the screen: two presses reach
 * the class picker rather than doing nothing the second time. */
TEST_CASE(back_climbs_one_level_at_a_time)
{
    fresh();
    descend_to_blheli();
    scr->render(&cv, 0);
    const int at_device = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_list = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_class = lit();

    CHECK(at_device > 0 && at_list > 0 && at_class > 0);
    if (at_class == at_list || at_list == at_device) {
        T_FAIL("levels drew the same: %d, %d, %d",
               at_class, at_list, at_device);
    }
    /*
     * And from the top it does nothing rather than leaving the screen.
     *
     * Asserted by descending again rather than by comparing pixels: pressing
     * BACK at the class picker changes nothing, so the redraw gate paints
     * nothing, and a test that cleared the buffer first measures its own
     * memset.
     */
    tap(BACK_X, BACK_Y);
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    tap(CONNECT_X, CONNECT_Y);
    CHECK(programmer_screen_connected());
}

/* Each protocol brings its own parameters and its own defaults. */
TEST_CASE(each_protocol_starts_from_its_own_defaults)
{
    fresh();
    descend_to_blheli();
    CHECK_EQ(programmer_screen_value(2), 0);   /* 24 kHz */
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_value(2), 1);
    /* And that shows as an unwritten change rather than blending in. */
    CHECK_EQ(programmer_screen_dirty(), 1);

    tap(BACK_X, BACK_Y);
    tap(ROW_CX, ROW_CY(1));                     /* AM32 */
    tap(CONNECT_X, CONNECT_Y);
    /* AM32's third row starts at 48 kHz on its own account, not because
     * BLHeli_S was left on index one. */
    CHECK_EQ(programmer_screen_value(2), 1);
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_value(2), 2);
}

/*
 * Clamped, not wrapped.  A parameter that rolls from its last value round to
 * its first is set to the wrong end by one press too many, and on an ESC the
 * wrong end is a direction.
 */
TEST_CASE(a_stepper_stops_at_its_ends)
{
    fresh();
    descend_to_blheli();

    /* Row 0 is an enum of three; row 3 is a number bounded 25..150 by 25.
     * Both are steppers to the finger and both must stop. */
    for (int i = 0; i < 12; ++i) { tap(STEP_DN_X, STEP_CY(0)); }
    CHECK_EQ(programmer_screen_value(0), 0);
    for (int i = 0; i < 12; ++i) { tap(STEP_UP_X, STEP_CY(0)); }
    CHECK_EQ(programmer_screen_value(0), 2);

    for (int i = 0; i < 20; ++i) { tap(STEP_UP_X, STEP_CY(3)); }
    CHECK_EQ(programmer_screen_value(3), 150);
    for (int i = 0; i < 20; ++i) { tap(STEP_DN_X, STEP_CY(3)); }
    CHECK_EQ(programmer_screen_value(3), 25);
}

/* Nothing is editable until something has answered. */
TEST_CASE(no_device_means_no_editing)
{
    fresh();
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    const int before = programmer_screen_value(0);
    tap(STEP_UP_X, STEP_CY(0));
    CHECK_EQ(programmer_screen_value(0), before);

    tap(CONNECT_X, CONNECT_Y);
    tap(STEP_UP_X, STEP_CY(0));
    CHECK(programmer_screen_value(0) != before);
}

/* Eight parameters in six rows: the other two have to be reachable. */
TEST_CASE(paging_reaches_the_rows_that_do_not_fit)
{
    fresh();
    descend_to_blheli();
    scr->render(&cv, 0);
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(PAGE_DN_X, PAGE_CY);
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != first[i]) { ++differ; }
    }
    if (differ < 2000) {
        T_FAIL("paging changed only %d pixels", differ);
    }

    for (int i = 0; i < 10; ++i) { tap(PAGE_DN_X, PAGE_CY); }
    scr->render(&cv, 0);
    gfx_color_t *bottom = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(bottom, fb, (size_t)W * H * sizeof(gfx_color_t));
    tap(PAGE_DN_X, PAGE_CY);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(bottom, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(first);
    free(bottom);
}

/* Every protocol reaches its parameters and draws them. */
TEST_CASE(every_protocol_draws_its_parameters)
{
    /* Four ESC protocols and one servo; BLHeli_32 is not one of them, see
     * docs/BLHeli32.md. */
    const int klass[] = { 0, 0, 0, 0, 1 };
    const int slot[]  = { 0, 1, 2, 3, 0 };
    for (int p = 0; p < (int)(sizeof(klass) / sizeof(klass[0])); ++p) {
        fresh();
        tap(TILE_CX(klass[p]), TILE_CY);
        tap(ROW_CX, ROW_CY(slot[p]));
        tap(CONNECT_X, CONNECT_Y);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        scr->render(&cv, 0);
        CHECK_EQ(programmer_screen_protocol(), p);
        if (lit() < 20000) {
            T_FAIL("protocol %d drew only %d pixels", p, lit());
        }
    }
}


/*
 * Until a read or a write happens, a staged edit and the device's value
 * disagree, and the screen shows the difference: an edited value must not
 * look like a value read off the hardware.
 */
TEST_CASE(a_change_is_unwritten_until_it_is_written)
{
    fresh();
    descend_to_blheli();
    CHECK_EQ(programmer_screen_dirty(), 0);

    tap(STEP_UP_X, STEP_CY(1));
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(programmer_screen_dirty(), 2);

    /* Writing makes the device agree with the screen. */
    tap(WRITE_X, BTN_CY);                   /* WRITE */
    CHECK_EQ(programmer_screen_dirty(), 0);

    /* Reading throws staged edits away, which is what reading means. */
    const int was = programmer_screen_value(1);
    tap(STEP_UP_X, STEP_CY(1));
    CHECK_EQ(programmer_screen_dirty(), 1);
    tap(READ_X, BTN_CY);                    /* READ */
    CHECK_EQ(programmer_screen_dirty(), 0);
    CHECK_EQ(programmer_screen_value(1), was);
}

/* Three kinds of parameter, and the stepper is the same gesture for all of
 * them: a switch has two values, a choice has its list, a number has its
 * range.  The widget differs; the way you move it does not. */
TEST_CASE(every_kind_steps)
{
    fresh();
    descend_to_blheli();
    /* 0 enum, 3 number, 5 boolean. */
    const int rows[3] = { 0, 3, 5 };
    for (int k = 0; k < 3; ++k) {
        const int before = programmer_screen_value(rows[k]);
        tap(STEP_UP_X, STEP_CY(rows[k]));
        if (programmer_screen_value(rows[k]) == before) {
            T_FAIL("row %d did not step from %d", rows[k], before);
        }
    }
}

int main(void)
{
    RUN(the_protocol_list_is_pressable_before_it_is_painted);
    RUN(a_class_shows_only_its_own_protocols);
    RUN(stepping_back_up_drops_the_connection);
    RUN(back_climbs_one_level_at_a_time);
    RUN(each_protocol_starts_from_its_own_defaults);
    RUN(a_stepper_stops_at_its_ends);
    RUN(no_device_means_no_editing);
    RUN(paging_reaches_the_rows_that_do_not_fit);
    RUN(a_change_is_unwritten_until_it_is_written);
    RUN(every_kind_steps);
    RUN(every_protocol_draws_its_parameters);
    return test_summary("programmer");
}
