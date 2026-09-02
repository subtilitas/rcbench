/*
 * What the balance screen decides: both panes draw, the tabs switch them,
 * the blade arithmetic, and no line of guidance runs off its card.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "balance_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

/* Mirrored from balance_screen.c. */
#define PAD      6
#define LCARD_W  494
#define RCARD_X  (PAD + LCARD_W + 8)
#define RCARD_W  (W - RCARD_X - PAD)
#define BODY_Y   44
#define CTRL_X   (RCARD_X + 14)
#define CTRL_W   (RCARD_W - 28)
/* Centres of the controls reset() lays out, from the same arithmetic. */
#define BLADE_DN_X (CTRL_X + CTRL_W - 78 + 17)
#define BLADE_UP_X (CTRL_X + CTRL_W - 34 + 17)
#define BLADE_Y    (BODY_Y + 96 + 14)
#define ROTOR0_X   (CTRL_X + (CTRL_W / 2 - 4) / 2)
#define ROTOR1_X   (CTRL_X + (CTRL_W / 2 + 4) + (CTRL_W / 2 - 4) / 2)
#define ROTOR_Y    (BODY_Y + 34 + 15)

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = balance_screen();
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

/*
 * The guidance is prose in a fixed-width font in a card of known width, and
 * the text renderer clips silently.  Pins: the two columns inside the card's
 * right edge hold no pixel of either text colour, so no line reaches the
 * wall.
 */
TEST_CASE(no_guidance_runs_off_its_card)
{
    for (int pane = 0; pane < 2; ++pane) {
        fresh();
        if (pane == 1) {
            tap(200, 22);                   /* the AIRCRAFT tab */
        }
        scr->render(&cv, 0);

        const gfx_color_t text = ui_theme_color(UI_C_TEXT);
        const gfx_color_t dim  = ui_theme_color(UI_C_TEXT_DIM);
        int touching = 0;
        for (int x = RCARD_X + RCARD_W - 3; x < RCARD_X + RCARD_W - 1; ++x) {
            for (int y = 0; y < H; ++y) {
                const gfx_color_t p = fb[(size_t)y * W + x];
                if (p == text || p == dim) { ++touching; }
            }
        }
        if (touching > 0) {
            T_FAIL("pane %d has %d text pixels against the card edge",
                   pane, touching);
        }
    }
}

/* Both panes draw something, and they draw different things. */
TEST_CASE(both_panes_draw_and_differ)
{
    fresh();
    scr->render(&cv, 0);
    const int rig = lit();
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(200, 22);
    scr->render(&cv, 0);
    const int air = lit();

    CHECK(rig > 20000);
    CHECK(air > 5000);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != first[i]) { ++differ; }
    }
    if (differ < 5000) {
        T_FAIL("the two panes differ by only %d pixels", differ);
    }
    free(first);
}

/*
 * Pins: the rig diagram stays inside its own card, with no pixel in the
 * gutter between the cards.  Hand-placed callout leaders are what a layout
 * change moves.
 */
TEST_CASE(the_diagram_stays_in_its_card)
{
    fresh();
    scr->render(&cv, 0);
    int stray = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = PAD + LCARD_W; x < RCARD_X; ++x) {
            if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) { ++stray; }
        }
    }
    if (stray > 0) {
        T_FAIL("%d pixels of the diagram are in the gutter", stray);
    }
}

/*
 * The blade stepper clamps to 2..6: pressing past either end is a no-op, not
 * a wrap.
 */
TEST_CASE(the_blade_count_clamps_two_to_six)
{
    fresh();
    CHECK_EQ(balance_screen_blades(), 2);

    tap(BLADE_DN_X, BLADE_Y);                 /* already at the floor */
    CHECK_EQ(balance_screen_blades(), 2);

    for (int i = 0; i < 4; ++i) { tap(BLADE_UP_X, BLADE_Y); }
    CHECK_EQ(balance_screen_blades(), 6);
    tap(BLADE_UP_X, BLADE_Y);                 /* past the ceiling */
    CHECK_EQ(balance_screen_blades(), 6);

    tap(BLADE_DN_X, BLADE_Y);
    CHECK_EQ(balance_screen_blades(), 5);
}

/* The rotor type toggles between propeller and ducted fan. */
TEST_CASE(the_rotor_toggles)
{
    fresh();
    CHECK_EQ(balance_screen_rotor(), ROTOR_PROP);
    tap(ROTOR1_X, ROTOR_Y);
    CHECK_EQ(balance_screen_rotor(), ROTOR_EDF);
    tap(ROTOR0_X, ROTOR_Y);
    CHECK_EQ(balance_screen_rotor(), ROTOR_PROP);
}

/*
 * An angle from the index mark is named as the two blades it falls between.
 * Checked directly across every blade count; a golden image pins where the
 * text lands, not what it says.
 */
TEST_CASE(an_angle_names_the_blades_it_falls_between)
{
    int lo, hi;

    /* 265 degrees on a six-blade rotor: blades at 0,60,..,300, so it is
     * between the fifth (240) and the sixth (300). */
    balance_nearest_blades(265.0f, 6, &lo, &hi);
    CHECK_EQ(lo, 5);
    CHECK_EQ(hi, 6);

    /* Three blades at 0,120,240: 265 is past the third, wrapping to the
     * first. */
    balance_nearest_blades(265.0f, 3, &lo, &hi);
    CHECK_EQ(lo, 3);
    CHECK_EQ(hi, 1);

    /* A correction past the last blade wraps to "between N and 1". */
    balance_nearest_blades(359.0f, 4, &lo, &hi);
    CHECK_EQ(lo, 4);
    CHECK_EQ(hi, 1);

    /* Right at the index mark is between the first blade and the second. */
    balance_nearest_blades(0.0f, 5, &lo, &hi);
    CHECK_EQ(lo, 1);
    CHECK_EQ(hi, 2);

    /* Both are always a real blade, 1..count, for every count and a spread
     * of angles: no zero, no off-by-one past the end. */
    for (int blades = 2; blades <= 6; ++blades) {
        for (int a = 0; a < 360; a += 7) {
            balance_nearest_blades((float)a, blades, &lo, &hi);
            CHECK(lo >= 1 && lo <= blades);
            CHECK(hi >= 1 && hi <= blades);
            CHECK(lo != hi);              /* two distinct blades */
        }
    }
}

/* A negative angle -- which a sensor could hand back -- is floored rather than
 * indexing before the first blade. */
TEST_CASE(a_negative_angle_does_not_index_before_the_first_blade)
{
    int lo, hi;
    balance_nearest_blades(-30.0f, 4, &lo, &hi);
    CHECK_EQ(lo, 1);
    CHECK_EQ(hi, 2);
}

/* Tab centres, from the tab bar reset() lays out: start 9, width 390, three
 * tabs of 130 each. */
#define TAB0_X   74
#define TAB1_X   204
#define TAB2_X   334
#define TAB_CY   22

/*
 * All three panes draw, each a different picture.  The aircraft pane is the
 * third tab, not the second, and holds the most drawing code on the screen.
 */
TEST_CASE(each_pane_draws_its_own_diagram)
{
    int prev = -1;
    const int tabs[] = { TAB0_X, TAB1_X, TAB2_X };
    for (int i = 0; i < 3; ++i) {
        fresh();
        if (i > 0) { tap(tabs[i], TAB_CY); }
        scr->render(&cv, 0);
        const int drew = lit();
        if (drew < 5000) {
            T_FAIL("pane %d drew only %d pixels", i, drew);
        }
        CHECK(drew != prev);            /* not the same picture as the last */
        prev = drew;
    }
}

/*
 * The measure pane draws the rotor face-on.  A ducted fan is a different
 * picture from a propeller (a duct and a hub angle rather than a blade), and
 * the EDF (electric ducted fan) branch is separate code, so both render.
 */
TEST_CASE(both_rotor_types_draw_on_the_measure_pane)
{
    fresh();
    scr->render(&cv, 0);
    CHECK(lit() > 5000);
    gfx_color_t *prop = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(prop, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(ROTOR1_X, ROTOR_Y);             /* to EDF */
    CHECK_EQ(balance_screen_rotor(), ROTOR_EDF);
    scr->render(&cv, 0);
    CHECK(lit() > 5000);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != prop[i]) { ++differ; }
    }
    free(prop);
    if (differ < 500) {
        T_FAIL("the duct changed only %d pixels from the propeller", differ);
    }
}

/*
 * Every blade count from 2 to 6 draws a rotor.  The blade loop is
 * count-driven, so running the whole range exercises the placement
 * arithmetic.
 */
TEST_CASE(every_blade_count_draws)
{
    fresh();
    int last = 0;
    for (int b = 2; b <= 6; ++b) {
        while (balance_screen_blades() < b) { tap(BLADE_UP_X, BLADE_Y); }
        scr->render(&cv, 0);
        const int ink = lit();
        CHECK(ink > 5000);
        last = ink;
    }
    CHECK(last > 0);
}

int main(void)
{
    RUN(no_guidance_runs_off_its_card);
    RUN(both_panes_draw_and_differ);
    RUN(the_diagram_stays_in_its_card);
    RUN(the_blade_count_clamps_two_to_six);
    RUN(the_rotor_toggles);
    RUN(an_angle_names_the_blades_it_falls_between);
    RUN(a_negative_angle_does_not_index_before_the_first_blade);
    RUN(each_pane_draws_its_own_diagram);
    RUN(both_rotor_types_draw_on_the_measure_pane);
    RUN(every_blade_count_draws);
    return test_summary("balance");
}
