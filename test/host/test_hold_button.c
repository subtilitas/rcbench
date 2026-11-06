/*
 * The hold's flash as ui_hold_tick() starts it, and the hold button that
 * draws it (ui_hold_button(), ui_hold_ghost()).
 *
 * Every hold flashes on the frame it fires, 300ms in phases of 50ms,
 * without a ui_hold_reached(); a reach inside the flash restarts nothing
 * and a press gets one flash; a spent press keeps its flash.  The button
 * fades from its base to its target, draws the phases on its whole body,
 * enabled while it flashes, and its ghost where it was drawn.  The phases'
 * timing is test_widgets.c's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 400
#define H 240

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
}

/* @p seconds of the hold in frames of half the most one frame credits;
 * whether it fired. */
static bool hold_advance(ui_hold_t *h, float seconds)
{
    const float step = UI_HOLD_MAX_CREDIT_S / 2.0f;
    bool fired = false;
    for (float t = 0.0f; t + step <= seconds + step / 2.0f; t += step) {
        if (ui_hold_tick(h, step)) {
            fired = true;
        }
    }
    return fired;
}

/* Every hold flashes as it fires, with no ui_hold_reached(): the firing
 * starts the 300ms flash, which the steps then spend. */
TEST_CASE(every_hold_flashes_as_it_fires_without_a_reach)
{
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_TEST);
    CHECK(!hold_advance(&h, UI_HOLD_S / 2.0f));
    CHECK_EQ(h.flash_ms, 0);
    CHECK(!ui_hold_flashing(&h));
    CHECK(hold_advance(&h, UI_HOLD_S / 2.0f));
    CHECK_EQ(h.flash_ms, UI_HOLD_FLASH_MS);
    CHECK(ui_hold_flashing(&h));
    /* The frame it fires on draws white: its first step spends nothing. */
    ui_hold_flash_step(&h, 1.0f / 60.0f);
    CHECK_EQ(ui_hold_flash(UI_OK, h.flash_ms), GFX_WHITE);
    int frames = 0;
    while (ui_hold_flashing(&h) && frames < 100) {
        ui_hold_flash_step(&h, 1.0f / 60.0f);
        ++frames;
    }
    CHECK_EQ(frames, 18);
    /* A press that is spent keeps its flash: the page that closed on it
     * still shows the firing. */
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_TEST);
    CHECK(hold_advance(&h, UI_HOLD_S));
    ui_hold_spend(&h);
    CHECK(!h.down);
    CHECK(!h.fired);
    CHECK_EQ(h.flash_ms, UI_HOLD_FLASH_MS);
    CHECK(!ui_hold_end(&h, NULL, gfx_rect_make(0, 0, 1, 1)));
}

/* A reach inside the flash restarts nothing; a reach after it, for the
 * same press, flashes nothing more; a state that arrives with no press,
 * or again after it went away, flashes once. */
TEST_CASE(a_reach_flashes_once_a_press_and_restarts_nothing)
{
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    CHECK(hold_advance(&h, UI_HOLD_S));
    ui_hold_flash_step(&h, 0.0f);
    ui_hold_flash_step(&h, 0.06f);          /* into the black phase */
    const int mid = h.flash_ms;
    CHECK(mid < UI_HOLD_FLASH_MS && mid > 0);
    ui_hold_reached(&h);
    CHECK_EQ(h.flash_ms, mid);
    CHECK(h.held_s == 0.0f);                /* the fade ends */
    for (int i = 0; i < 40; ++i) {
        ui_hold_flash_step(&h, 1.0f / 60.0f);
    }
    CHECK_EQ(h.flash_ms, 0);
    ui_hold_reached(&h);                    /* the answer, late */
    CHECK_EQ(h.flash_ms, 0);
    CHECK(ui_hold_end(&h, NULL, gfx_rect_make(0, 0, 1, 1)));

    /* Arrived from elsewhere: no press, one flash. */
    ui_hold_reset(&h);
    ui_hold_reached(&h);
    CHECK_EQ(h.flash_ms, UI_HOLD_FLASH_MS);
    for (int i = 0; i < 40; ++i) {
        ui_hold_flash_step(&h, 1.0f / 60.0f);
    }
    ui_hold_reached(&h);
    CHECK_EQ(h.flash_ms, 0);
    /* Gone, and back: it flashes again. */
    CHECK(!ui_hold_left(&h));
    ui_hold_reached(&h);
    CHECK_EQ(h.flash_ms, UI_HOLD_FLASH_MS);
    /* A fresh press flashes as it fires, whatever came before. */
    ui_hold_flash_step(&h, 1.0f);
    ui_hold_flash_step(&h, 1.0f);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    for (int i = 0; i < 20 && ui_hold_flashing(&h); ++i) {
        ui_hold_flash_step(&h, 1.0f);
    }
    CHECK(hold_advance(&h, UI_HOLD_S));
    CHECK_EQ(h.flash_ms, UI_HOLD_FLASH_MS);
}

/* The colour of the body of a @p r drawn on the canvas, clear of the
 * label and the rounded corners. */
static gfx_color_t body_px(gfx_rect_t r)
{
    return fb[(r.y + 6) * W + r.x + 10];
}

/* The hold button: base, the fade, the flash's phases on the button whole,
 * settled on the target; drawn enabled while it flashes; and its ghost
 * where it was drawn. */
TEST_CASE(the_hold_button_fades_and_flashes_where_it_is_drawn)
{
    canvas();
    ui_theme_set(UI_THEME_DARK);
    const gfx_rect_t r = { 100, 100, 200, 60 };
    const gfx_color_t base = UI_OK, target = UI_DANGER;
    ui_hold_t h;
    ui_hold_reset(&h);
    ui_hold_button(&cv, r, "GO", base, target, &h, false, true,
                   UI_FONT_LABEL);
    CHECK_EQ(body_px(r), base);
    ui_hold_begin(&h, TX_HOLD_HINT_ARM);
    CHECK(!hold_advance(&h, UI_HOLD_S / 2.0f));
    ui_hold_button(&cv, r, "GO", base, target, &h, false, true,
                   UI_FONT_LABEL);
    CHECK_EQ(body_px(r), ui_hold_fill(base, target, h.held_s));
    CHECK(body_px(r) != base && body_px(r) != target);
    CHECK(hold_advance(&h, UI_HOLD_S / 2.0f));
    /* The state it asked for can refuse the next press: drawn disabled,
     * the flash still shows. */
    static const char k_want[] = "WWWBBBSSSWWWBBBSSS";
    char seen[sizeof(k_want)];
    ui_hold_flash_step(&h, 0.0f);
    for (int i = 0; i < 18; ++i) {
        ui_hold_button(&cv, r, "GO", base, target, &h, false, false,
                       UI_FONT_LABEL);
        const gfx_color_t px = body_px(r);
        seen[i] = (px == GFX_WHITE) ? 'W' : (px == GFX_BLACK) ? 'B'
                : (px == target) ? 'S' : '?';
        ui_hold_flash_step(&h, 1.0f / 60.0f);
    }
    seen[18] = '\0';
    CHECK_STR_EQ(seen, k_want);
    CHECK(!ui_hold_flashing(&h));
    ui_hold_button(&cv, r, "GO", base, target, &h, false, false,
                   UI_FONT_LABEL);
    CHECK_EQ(body_px(r), ui_button_off_fill());

    /* The ghost: nothing without a flash, the button where it was drawn
     * while one runs. */
    canvas();
    ui_hold_ghost(&cv, &h);
    CHECK_EQ(body_px(r), 0);
    CHECK(ui_hold_end(&h, NULL, r));
    ui_hold_reached(&h);
    CHECK(!ui_hold_flashing(&h));           /* this press had its flash */
    CHECK(!ui_hold_left(&h));
    ui_hold_reached(&h);
    ui_hold_ghost(&cv, &h);
    CHECK_EQ(body_px(r), GFX_WHITE);
    ui_hold_ghost(NULL, &h);
    ui_hold_ghost(&cv, NULL);
    ui_hold_t never;
    ui_hold_reset(&never);
    never.drawn_f = NULL;
    ui_hold_reached(&never);
    canvas();
    ui_hold_ghost(&cv, &never);             /* never drawn: nothing */
    CHECK_EQ(body_px(r), 0);
    CHECK_EQ(ui_hold_button_fill(base, target, NULL), base);
    CHECK(!ui_hold_flashing(NULL));
}

int main(void)
{
    RUN(every_hold_flashes_as_it_fires_without_a_reach);
    RUN(a_reach_flashes_once_a_press_and_restarts_nothing);
    RUN(the_hold_button_fades_and_flashes_where_it_is_drawn);
    return test_summary("hold_button");
}
