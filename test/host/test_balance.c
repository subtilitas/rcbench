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
#include "instruments_spots.h"
#include "ui_band.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/* From instruments_spots.h, mirrored from balance_screen.c. */
#define PAD      UI_MARGIN
#define LCARD_W  (BL_RCARD_X - 8 - PAD)
#define RCARD_X  BL_RCARD_X
#define RCARD_W  (W - RCARD_X - PAD)
#define CTRL_X   BL_RC_X
#define CTRL_W   BL_RC_W
/* Centres of the controls reset() lays out. */
#define BLADE_DN_X BL_DOWN_X
#define BLADE_UP_X BL_UP_X
#define BLADE_Y    BL_BLADES_Y
#define ROTOR0_X   BL_PROP_X
#define ROTOR1_X   BL_EDF_X
#define ROTOR_Y    BL_ROTOR_Y

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
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
            tap(IN_TAB_X(2), IN_TAB_Y);     /* the AIRCRAFT tab */
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

/*
 * The angle (265 deg) and both masses (0.42 g, 0.35 g) are fixed values, so
 * the BALANCE pane carries the MODELLED pill under the readings: 88 px by
 * 20 px at y 378, its fill clear of the dot and the word, and the
 * words "Not measured." beside it.  The other panes carry none.  In German
 * the pill and the words are the German ones, and both fit inside the card.
 */
#define MARK_X  CTRL_X
#define MARK_Y  BL_MARK_Y

TEST_CASE(the_readings_carry_the_modelled_mark)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang ? UI_LANG_DE : UI_LANG_EN);
        fresh();
        CHECK_STR_EQ(balance_screen_model_mark(), TR(BAL_MODELLED));
        CHECK_STR_EQ(balance_screen_model_note(), TR(BAL_MODEL_NOTE));
        CHECK_STR_EQ(TR(BAL_MODELLED), lang ? "SIMULIERT" : "SIMULATED");
        scr->render(&cv, 0);
        CHECK_EQ(fb[(MARK_Y + 1) * W + MARK_X + 60],
                 ui_theme_color(UI_C_PANEL_SUNK));
        CHECK_EQ(fb[(MARK_Y + 10) * W + MARK_X + 12],
                 ui_theme_color(UI_C_WARN));
        /* The words beside the pill, in the dim text colour. */
        int dim = 0;
        for (int y = MARK_Y; y < MARK_Y + 20; ++y) {
            for (int x = MARK_X + 96; x < CTRL_X + CTRL_W; ++x) {
                dim += (fb[(size_t)y * W + x] == ui_theme_color(UI_C_TEXT_DIM));
            }
        }
        CHECK(dim > 20);
        /* Under the rule at y 406. */
        CHECK_EQ(fb[(BL_NOTE_RULE_Y - 1) * W + MARK_X + 60],
                 ui_theme_color(UI_C_PANEL));

        tap(IN_TAB_X(1), IN_TAB_Y);
        CHECK(balance_screen_model_mark() == NULL);
        CHECK(balance_screen_model_note() == NULL);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Both panes draw something, and they draw different things. */
TEST_CASE(both_panes_draw_and_differ)
{
    fresh();
    scr->render(&cv, 0);
    const int rig = lit();
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(first);
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(IN_TAB_X(1), IN_TAB_Y);
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

/* ------------------------------------------------ the blade count keypad */

/* The keypad's key @p key tapped. */
static void kp_key(ui_key_t key)
{
    const gfx_rect_t r = ui_keypad_key_rect(balance_screen_keypad(), key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* A tap on the blade count: its keypad. */
static void tap_count(void)
{
    const gfx_rect_t r = balance_screen_blades_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* A tap on the count opens the keypad over both cards, above the alert
 * band; the label beside it and the keys open nothing. */
TEST_CASE(a_tap_on_the_blade_count_opens_its_keypad)
{
    fresh();
    tap(CTRL_X + 10, BLADE_Y);                /* the label */
    tap(BLADE_UP_X, BLADE_Y);
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 3);
    tap_count();
    const ui_keypad_t *k = balance_screen_keypad();
    CHECK(k->open);
    CHECK_STR_EQ(k->title, TR(BAL_BLADES));
    CHECK_NEAR(k->min, 2.0f, 1e-6f);
    CHECK_NEAR(k->max, 6.0f, 1e-6f);
    CHECK_EQ(k->decimals, 0);
    CHECK_NEAR(k->value, 3.0f, 1e-6f);
    CHECK(k->area.y + k->area.h <= UI_BODY_H - UI_ALERT_H);
    CHECK(k->area.y >= BL_CARD_Y);
    /* The count sits between its label and the "-" key. */
    const gfx_rect_t v = balance_screen_blades_rect();
    CHECK(v.x + v.w < BLADE_DN_X - 17);
    CHECK(v.x > CTRL_X + 56);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    balance_invalidate();
    scr->render(&cv, 0);
    const gfx_rect_t ok = ui_keypad_key_rect(k, UI_KEY_OK);
    CHECK_EQ(fb[(ok.y + ok.h / 2) * W + ok.x + 4],
             ui_theme_color(UI_C_ACCENT));
}

TEST_CASE(the_blade_keypad_takes_two_to_six_and_refuses_the_rest)
{
    fresh();
    tap_count();
    kp_key(UI_KEY_6);
    kp_key(UI_KEY_OK);
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 6);
    tap_count();
    kp_key(UI_KEY_2);
    kp_key(UI_KEY_OK);
    CHECK_EQ(balance_screen_blades(), 2);
    tap_count();
    kp_key(UI_KEY_7);                         /* above the maximum */
    kp_key(UI_KEY_OK);
    CHECK(balance_screen_keypad()->open);
    CHECK(balance_screen_keypad()->refused);
    CHECK_EQ(balance_screen_blades(), 2);
    kp_key(UI_KEY_CLR);
    kp_key(UI_KEY_1);                         /* under the minimum */
    kp_key(UI_KEY_OK);
    CHECK(balance_screen_keypad()->refused);
    /* A count takes no point: "4.5" is typed as 45, and refused. */
    kp_key(UI_KEY_CLR);
    kp_key(UI_KEY_4);
    kp_key(UI_KEY_DOT);
    kp_key(UI_KEY_5);
    CHECK_STR_EQ(balance_screen_keypad()->entry, "45");
    kp_key(UI_KEY_OK);
    CHECK(balance_screen_keypad()->refused);
    CHECK_EQ(balance_screen_blades(), 2);
    kp_key(UI_KEY_CANCEL);
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 2);
}

/* The count typed is the count the steps reach. */
TEST_CASE(a_typed_count_is_the_count_the_steps_reach)
{
    fresh();
    tap(BLADE_UP_X, BLADE_Y);
    tap(BLADE_UP_X, BLADE_Y);
    const int stepped = balance_screen_blades();
    fresh();
    tap_count();
    kp_key(UI_KEY_4);
    kp_key(UI_KEY_OK);
    CHECK_EQ(balance_screen_blades(), stepped);
    CHECK_EQ(stepped, 4);
}

/* CANCEL, the home tag, leaving and a tab change nothing; the controls
 * under the keypad press nothing while it is open. */
TEST_CASE(the_blade_keypad_cancels_and_covers_what_is_under_it)
{
    fresh();
    tap_count();
    kp_key(UI_KEY_5);
    CHECK(scr->back());                       /* the home tag: CANCEL */
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 2);
    CHECK(!scr->back());                      /* then home */
    tap_count();
    kp_key(UI_KEY_5);
    scr->leave();
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 2);
    tap_count();
    tap(ROTOR1_X, ROTOR_Y);                   /* under it */
    CHECK(balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_rotor(), ROTOR_PROP);
    kp_key(UI_KEY_5);
    tap(IN_TAB_X(1), IN_TAB_Y);               /* RIG's tab, above it */
    CHECK(!balance_screen_keypad()->open);
    CHECK(balance_screen_model_mark() == NULL);
    CHECK_EQ(balance_screen_blades(), 2);
    tap(IN_TAB_X(0), IN_TAB_Y);               /* back to BALANCE */
    tap_count();
    /* A press the touch stream lost is let go; the entry stays. */
    kp_key(UI_KEY_3);
    const gfx_rect_t r = ui_keypad_key_rect(balance_screen_keypad(),
                                            UI_KEY_4);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 2, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    scr->event(&e);
    CHECK(balance_screen_keypad()->pressed >= 0);
    scr->cancel();
    CHECK(balance_screen_keypad()->pressed < 0);
    CHECK_STR_EQ(balance_screen_keypad()->entry, "3");
    /* A press on the count that slides off opens nothing. */
    kp_key(UI_KEY_CANCEL);
    const gfx_rect_t v = balance_screen_blades_rect();
    e = (touch_event_t){ .type = TOUCH_EVENT_DOWN,
                         .point = { .id = 1, .x = (int16_t)(v.x + 4),
                                    .y = (int16_t)(v.y + 4),
                                    .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    e.point.y = (int16_t)(v.y + v.h + 40);
    scr->event(&e);
    CHECK(!balance_screen_keypad()->open);
}

/* STOP in the band works over the keypad; the keypad stays with what was
 * typed and the count as it was. */
TEST_CASE(stop_over_the_blade_keypad_sets_nothing)
{
    ui_router_init();
    ui_router_goto(SCREEN_BALANCE);
    const gfx_rect_t v = balance_screen_blades_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(v.x + v.w / 2),
                                   .y = (int16_t)(UI_BAND_H + v.y + v.h / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(balance_screen_keypad()->open);
    const gfx_rect_t k5 = ui_keypad_key_rect(balance_screen_keypad(),
                                             UI_KEY_5);
    e.type = TOUCH_EVENT_DOWN;
    e.point.x = (int16_t)(k5.x + k5.w / 2);
    e.point.y = (int16_t)(UI_BAND_H + k5.y + k5.h / 2);
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    (void)ui_router_take_stop();
    const gfx_rect_t r = ui_band_stop_rect();
    e.type = TOUCH_EVENT_DOWN;
    e.point.x = (int16_t)(r.x + r.w / 2);
    e.point.y = (int16_t)(r.y + r.h / 2);
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(ui_router_take_stop());
    CHECK(balance_screen_keypad()->open);
    CHECK_STR_EQ(balance_screen_keypad()->entry, "5");
    CHECK_EQ(balance_screen_blades(), 2);
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(!balance_screen_keypad()->open);
    CHECK_EQ(balance_screen_blades(), 2);
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

/* Tab centres, from the tab row reset() lays out: from x 11, three tabs of
 * 150 px, 4 px apart. */
#define TAB0_X   IN_TAB_X(0)
#define TAB1_X   IN_TAB_X(1)
#define TAB2_X   IN_TAB_X(2)
#define TAB_CY   IN_TAB_Y

/*
 * A cancel abandons a tab press: the panel's record of the glass is stale,
 * and the track id the press owned is one the controller reuses, so a later
 * contact lifting over the tab must not switch the pane.
 */
TEST_CASE(a_cancelled_tab_press_does_not_switch_the_pane)
{
    fresh();
    scr->render(&cv, 0);
    const int before = lit();

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = TAB1_X, .y = TAB_CY,
                                   .strength = 40 } };
    scr->event(&e);
    scr->cancel();
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    scr->render(&cv, 0);
    CHECK_EQ(lit(), before);

    /* And the row is not stuck. */
    tap(TAB1_X, TAB_CY);
    scr->render(&cv, 0);
    CHECK(lit() != before);
}


/*
 * A press already drawn is drawn released once it is cancelled.  The screen
 * caches its frame per buffer by revision, so a cancel that cleared the
 * press without moving the revision would leave the tab painted held.
 */
TEST_CASE(a_cancelled_tab_press_already_drawn_is_redrawn_released)
{
    fresh();
    scr->render(&cv, 0);
    static gfx_color_t idle[W * H];
    memcpy(idle, fb, sizeof(idle));

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = TAB1_X, .y = TAB_CY,
                                   .strength = 40 } };
    scr->event(&e);
    /* A tab press alone does not repaint; anything else that does paints
     * the press.  Forced here. */
    balance_invalidate();
    scr->render(&cv, 0);
    /* The press is visible, or this case proves nothing. */
    CHECK(memcmp(idle, fb, sizeof(idle)) != 0);

    scr->cancel();
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(idle, fb, sizeof(idle)), 0);
}

/*
 * All three panes draw, each a different picture.  The aircraft pane is the
 * third tab, not the second, and holds the most drawing code on the screen.
 */
TEST_CASE(each_pane_draws_its_own_diagram)
{
    uint32_t seen[3] = { 0u, 0u, 0u };
    const int tabs[] = { TAB0_X, TAB1_X, TAB2_X };
    for (int i = 0; i < 3; ++i) {
        fresh();
        if (i > 0) { tap(tabs[i], TAB_CY); }
        scr->render(&cv, 0);
        const int drew = lit();
        if (drew < 5000) {
            T_FAIL("pane %d drew only %d pixels", i, drew);
        }
        /* Not the same picture as another pane's: the cards fill the same
         * area on every pane, so the count of lit pixels cannot tell. */
        uint32_t h = 2166136261u;
        for (int k = 0; k < W * H; ++k) {
            h = (h ^ fb[k]) * 16777619u;
        }
        seen[i] = h;
        for (int j = 0; j < i; ++j) {
            CHECK(seen[j] != h);
        }
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
    REQUIRE_ALLOC(prop);
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
    RUN(the_readings_carry_the_modelled_mark);
    RUN(both_panes_draw_and_differ);
    RUN(the_diagram_stays_in_its_card);
    RUN(the_blade_count_clamps_two_to_six);
    RUN(the_rotor_toggles);
    RUN(an_angle_names_the_blades_it_falls_between);
    RUN(a_negative_angle_does_not_index_before_the_first_blade);
    RUN(each_pane_draws_its_own_diagram);
    RUN(a_cancelled_tab_press_does_not_switch_the_pane);
    RUN(a_cancelled_tab_press_already_drawn_is_redrawn_released);
    RUN(both_rotor_types_draw_on_the_measure_pane);
    RUN(every_blade_count_draws);
    RUN(a_tap_on_the_blade_count_opens_its_keypad);
    RUN(the_blade_keypad_takes_two_to_six_and_refuses_the_rest);
    RUN(a_typed_count_is_the_count_the_steps_reach);
    RUN(the_blade_keypad_cancels_and_covers_what_is_under_it);
    RUN(stop_over_the_blade_keypad_sets_nothing);
    return test_summary("balance");
}
