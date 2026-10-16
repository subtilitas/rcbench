/*
 * What the programmer decides: that you say what you are programming before
 * anything is read, that stepping up a level cannot leave a stale connection
 * behind, and that a stepper stops rather than wraps.  The ESC STICK class
 * is in test_esc_stick_panel.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "programmer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "touch_feed.h"

#include "programmer_rig.h"

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
    const int on_device = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_list = lit();

    tap(BACK_X, BACK_Y);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    const int at_class = lit();

    CHECK(on_device > 0 && at_list > 0 && at_class > 0);
    if (at_class == at_list || at_list == on_device) {
        T_FAIL("levels drew the same: %d, %d, %d",
               at_class, at_list, on_device);
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
    REQUIRE_ALLOC(first);
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
    REQUIRE_ALLOC(bottom);
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

/* The MODELLED pill on the device line, mirrored as the rest are: 18 px
 * in, 60 to 80 px down; its dot at (30, 70), and at (38, 62) its fill,
 * clear of the dot's ring and of the word from x 42. */
#define MARK_DOT_X   30
#define MARK_DOT_Y   70
#define MARK_FILL_X  38
#define MARK_FILL_Y  62

static int ok_pixels(void)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_OK)) { ++n; }
    }
    return n;
}

/*
 * What a modelled session shows at one step: the mark and the sentence,
 * the pill drawn where a connection's dot would be, its dot amber while
 * the session is open and faint while none is, and nothing in the OK
 * colour, which on every other screen means hardware answered.
 */
static void shows_a_model(const char *step, bool open)
{
    const char *mark = programmer_screen_mark();
    const char *note = programmer_screen_note();
    if (mark == NULL || strcmp(mark, TR(PG_MODELLED)) != 0) {
        T_FAIL("%s: the mark is %s", step, mark ? mark : "missing");
    }
    if (note == NULL || strcmp(note, TR(PG_MODEL_NOTE)) != 0) {
        T_FAIL("%s: the sentence is %s", step, note ? note : "missing");
    }
    CHECK_EQ(programmer_screen_connected(), open);

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    programmer_invalidate();
    scr->render(&cv, 0);
    if (ok_pixels() != 0) {
        T_FAIL("%s: %d pixels in the OK colour", step, ok_pixels());
    }
    CHECK_EQ(fb[MARK_FILL_Y * W + MARK_FILL_X],
             ui_theme_color(UI_C_PANEL_SUNK));
    CHECK_EQ(fb[MARK_DOT_Y * W + MARK_DOT_X],
             open ? ui_theme_color(UI_C_WARN)
                  : ui_theme_color(UI_C_TEXT_FAINT));
}

/*
 * ESC and SERVO are a model with no transport: CONNECT, READ and WRITE
 * change the screen's own copy and nothing goes on a wire.  Every step of
 * a session carries the mark and the sentence and never the OK colour, in
 * every protocol of the two classes and in both languages.
 */
TEST_CASE(a_modelled_session_is_marked_at_every_step)
{
    const int klass[] = { 0, 0, 0, 0, 1 };
    const int slot[]  = { 0, 1, 2, 3, 0 };
    for (int lang = 0; lang < 2; ++lang) {
        for (int p = 0; p < (int)(sizeof(klass) / sizeof(klass[0])); ++p) {
            fresh();
            ui_text_set_language(lang ? UI_LANG_DE : UI_LANG_EN);
            CHECK(programmer_screen_mark() == NULL);   /* the class picker */
            tap(TILE_CX(klass[p]), TILE_CY);
            CHECK(programmer_screen_mark() != NULL);   /* the protocol list */
            CHECK(programmer_screen_note() == NULL);
            tap(ROW_CX, ROW_CY(slot[p]));
            shows_a_model("before CONNECT", false);
            CHECK_STR_EQ(programmer_screen_device_line(),
                         TR(PG_NOTHING_ANSWERED));

            tap(CONNECT_X, CONNECT_Y);
            shows_a_model("CONNECT", true);

            tap(STEP_UP_X, STEP_CY(1));
            tap(STEP_UP_X, STEP_CY(1));
            CHECK(programmer_screen_dirty() > 0);
            tap(READ_X, BTN_CY);
            CHECK_EQ(programmer_screen_dirty(), 0);
            shows_a_model("READ", true);

            tap(STEP_UP_X, STEP_CY(1));
            tap(STEP_UP_X, STEP_CY(1));
            CHECK(programmer_screen_dirty() > 0);
            tap(WRITE_X, BTN_CY);
            CHECK_EQ(programmer_screen_dirty(), 0);
            shows_a_model("WRITE", true);

            tap(CONNECT_X, CONNECT_Y);              /* DISCONNECT */
            shows_a_model("DISCONNECT", false);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The two modelled classes carry the mark on their tiles; ESC STICK, which
 * drives a real ESC through the bench, does not, and has no mark on its
 * pages. */
TEST_CASE(only_the_modelled_classes_carry_the_mark)
{
    fresh();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    scr->render(&cv, 0);
    /* The pill's fill at the same place on each tile: 20 px into the
     * 88 px pill centred on the 244 px tile, 2 px under its top edge,
     * clear of the dot's ring and of the word. */
    for (int i = 0; i < 3; ++i) {
        const int x = 18 + i * 260 + (244 - 88) / 2 + 20;
        const gfx_color_t px = fb[110 * W + x];
        if (i < 2) {
            CHECK_EQ(px, ui_theme_color(UI_C_PANEL_SUNK));
        } else {
            CHECK_EQ(px, ui_theme_color(UI_C_PANEL_HI));
        }
    }
    tap(TILE_CX(2), TILE_CY);
    CHECK(programmer_screen_mark() == NULL);
    CHECK(programmer_screen_note() == NULL);
    CHECK(programmer_screen_device_line() == NULL);
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
    RUN(a_modelled_session_is_marked_at_every_step);
    RUN(only_the_modelled_classes_carry_the_mark);
    RUN(every_protocol_draws_its_parameters);
    return test_summary("programmer");
}
