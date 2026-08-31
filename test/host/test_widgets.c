/*
 * The reusable instrument widgets: the plot's scales, and the two press
 * contracts that decide whether a finger can spin a motor by accident.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_hero.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
}

/* --------------------------------------------------------------- the ladder */

/*
 * The finer steps are the whole point.  A coarse 1/2/5 ladder puts a 6S pack
 * -- about 25 V, so 28 with headroom -- on a 0-50 V scale and wastes half the
 * plot; this ladder puts it on 0-30.
 */
TEST_CASE(the_scale_ladder_has_its_fine_steps)
{
    static const struct { float in, want; } k[] = {
        { 0.5f,   0.5f },  { 1.0f,   1.0f },  { 1.05f,  1.2f },
        { 1.3f,   1.5f },  { 1.6f,   2.0f },  { 2.1f,   2.5f },
        { 2.6f,   3.0f },  { 3.5f,   4.0f },  { 4.5f,   5.0f },
        { 5.5f,   6.0f },  { 6.5f,   8.0f },  { 8.5f,  10.0f },
        { 28.0f, 30.0f },  { 25.0f, 25.0f },  { 11419.0f, 12000.0f },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const float got = ui_plot_nice_ceil(k[i].in);
        if (fabsf(got - k[i].want) > k[i].want * 1e-4f) {
            T_FAIL("nice_ceil(%g) = %g, want %g",
                   (double)k[i].in, (double)got, (double)k[i].want);
        }
    }
    /* Never zero or negative, whatever it is handed. */
    CHECK(ui_plot_nice_ceil(0.0f) > 0.0f);
    CHECK(ui_plot_nice_ceil(-5.0f) > 0.0f);
}

static const ui_plot_series_t k_series[] = {
    { "VOLT", "V", 0x1234, 2, 5.0f },
    { "CURR", "A", 0x4321, 1, 1.0f },
};

static void push(ui_plot_t *p, float a, float b)
{
    const float v[2] = { a, b };
    ui_plot_push(p, v);
}

TEST_CASE(the_ring_holds_its_history_and_wraps)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    CHECK_EQ(ui_plot_sample(&p, 0, 0), 0.0f);   /* nothing pushed yet */

    for (int i = 0; i < UI_PLOT_HISTORY + 50; ++i) {
        push(&p, (float)i, 0.0f);
    }
    CHECK_EQ(p.filled, UI_PLOT_HISTORY);
    /* Newest is back = 0, and the ring wrapped without shuffling anything. */
    CHECK_EQ(ui_plot_sample(&p, 0, 0), (float)(UI_PLOT_HISTORY + 49));
    CHECK_EQ(ui_plot_sample(&p, 0, 1), (float)(UI_PLOT_HISTORY + 48));
    CHECK_EQ(ui_plot_sample(&p, 0, UI_PLOT_HISTORY - 1), 50.0f);
    /* Past the end reads zero rather than reading somebody else's memory. */
    CHECK_EQ(ui_plot_sample(&p, 0, UI_PLOT_HISTORY), 0.0f);
    CHECK_EQ(ui_plot_sample(&p, 5, 0), 0.0f);
}

/* A non-finite reading must not poison the scale or freeze the loop that owns
 * STOP.  It is recorded as zero: a gap in the trace, not the end of it. */
TEST_CASE(a_non_finite_sample_cannot_poison_the_plot)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    push(&p, 10.0f, 1.0f);
    push(&p, NAN, INFINITY);
    push(&p, 10.0f, 1.0f);

    CHECK_EQ(ui_plot_sample(&p, 0, 1), 0.0f);
    for (int i = 0; i < 200; ++i) {
        ui_plot_update_scales(&p, 100);
    }
    CHECK(isfinite(p.scale[0]));
    CHECK(isfinite(p.scale[1]));
    CHECK(p.scale[0] > 0.0f);
}

/* Grows at once, shrinks on patience: a brief spike must not make the whole
 * plot breathe. */
TEST_CASE(the_scale_grows_at_once_and_shrinks_slowly)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);

    for (int i = 0; i < 20; ++i) { push(&p, 10.0f, 1.0f); }
    ui_plot_update_scales(&p, 100);
    const float settled = p.scale[0];
    CHECK(settled >= 10.0f);

    /* One spike, one update: the scale is already bigger. */
    push(&p, 40.0f, 1.0f);
    ui_plot_update_scales(&p, 100);
    CHECK(p.scale[0] > settled);
    const float grown = p.scale[0];

    /* Push the spike out of the window; the scale must hold for a while. */
    for (int i = 0; i < 200; ++i) { push(&p, 10.0f, 1.0f); }
    ui_plot_update_scales(&p, 100);
    CHECK_EQ(p.scale[0], grown);

    int updates = 1;
    while (p.scale[0] == grown && updates < 500) {
        ui_plot_update_scales(&p, 100);
        ++updates;
    }
    CHECK(p.scale[0] < grown);
    /* About three seconds at 20 Hz, not one update and not a hundred. */
    CHECK(updates >= 40);
    CHECK(updates <= 120);
}

/* An idle bench must not draw its own noise at full height. */
TEST_CASE(the_scale_never_falls_below_the_series_floor)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    for (int i = 0; i < 500; ++i) {
        push(&p, 0.01f, 0.0f);
        ui_plot_update_scales(&p, 100);
    }
    CHECK(p.scale[0] >= 5.0f);
    CHECK(p.scale[1] >= 1.0f);
}

/* Tap to focus, tap again to hide, tap again to bring it back focused. */
TEST_CASE(a_legend_tap_cycles_focus_then_hidden)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    CHECK_EQ(p.focus, -1);

    ui_plot_touch_series(&p, 0);
    CHECK_EQ(p.focus, 0);
    CHECK(!p.hidden[0]);

    ui_plot_touch_series(&p, 0);
    CHECK(p.hidden[0]);
    CHECK_EQ(p.focus, -1);

    ui_plot_touch_series(&p, 0);
    CHECK(!p.hidden[0]);
    CHECK_EQ(p.focus, 0);

    /* Focusing another series does not hide the first. */
    ui_plot_touch_series(&p, 1);
    CHECK_EQ(p.focus, 1);
    CHECK(!p.hidden[0]);

    ui_plot_touch_series(&p, 99);   /* out of range is ignored */
    CHECK_EQ(p.focus, 1);
}

TEST_CASE(map_y_is_the_right_way_up_and_clamps)
{
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    p.scale[0] = 10.0f;

    /* Zero at the bottom, full scale at the top. */
    CHECK_EQ(ui_plot_map_y(&p, 0, 0.0f, 100, 200), 100 + 199);
    CHECK_EQ(ui_plot_map_y(&p, 0, 10.0f, 100, 200), 100);
    /* Beyond the scale in either direction stays inside the box. */
    CHECK_EQ(ui_plot_map_y(&p, 0, 99.0f, 100, 200), 100);
    CHECK_EQ(ui_plot_map_y(&p, 0, -5.0f, 100, 200), 100 + 199);
}

TEST_CASE(the_plot_and_hero_render_without_running_off_the_canvas)
{
    canvas();
    ui_plot_t p;
    ui_plot_init(&p, k_series, 2, 24.0f);
    for (int i = 0; i < 300; ++i) {
        push(&p, 20.0f + (float)(i % 7), (float)(i % 30));
    }
    ui_plot_update_scales(&p, 700);
    ui_plot_touch_series(&p, 1);

    const gfx_rect_t body = { 40, 10, 700, 220 };
    ui_plot_render(&p, &cv, body);
    ui_plot_render_legend(&p, &cv, (gfx_rect_t){ 8, 10, 700, 16 });

    const ui_hero_def_t def = { "VOLT", "V", 0x1234, 2 };
    ui_hero_render(&cv, (gfx_rect_t){ 40, 250, 180, 80 }, &def, 20.71f, 24.32f);
    ui_hero_render(&cv, (gfx_rect_t){ 240, 250, 180, 80 }, &def, NAN, NAN);

    /* Something was drawn, and nothing outside the boxes was. */
    int lit = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != 0) { ++lit; }
    }
    CHECK(lit > 5000);
    for (int x = 0; x < W; ++x) {
        CHECK_EQ(fb[(size_t)(H - 1) * W + x], 0);
    }
}

/* ---------------------------------------------------------------- the slider */

static ui_slider_t sl;
static const float k_presets[] = { 0.0f, 25.0f, 50.0f, 100.0f };
static const char *const k_labels[] = { "0", "25", "50", "100" };

static void fresh_slider(void)
{
    ui_slider_init(&sl, (gfx_rect_t){ 100, 100, 400, 40 }, 0.0f, 100.0f,
                   0x1234);
    ui_slider_set_presets(&sl, k_presets, k_labels, 4,
                          (gfx_rect_t){ 100, 150, 400, 36 });
}

static bool ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    return ui_slider_event(&sl, &e);
}

TEST_CASE(a_press_on_the_track_sets_the_value)
{
    fresh_slider();
    CHECK(ev(300, 120, TOUCH_EVENT_DOWN, 1));
    CHECK(sl.value > 49.0f && sl.value < 51.0f);
    (void)ev(300, 120, TOUCH_EVENT_UP, 1);
}

/*
 * A press that began on the track owns the value until it is released.  A
 * finger sliding off the bottom while pushing the throttle up must not hand
 * the throttle to whatever is underneath.
 */
TEST_CASE(a_drag_that_leaves_the_track_keeps_the_value)
{
    fresh_slider();
    (void)ev(150, 120, TOUCH_EVENT_DOWN, 1);
    CHECK(ev(400, 400, TOUCH_EVENT_MOVE, 1));   /* far below the track */
    CHECK(sl.value > 74.0f && sl.value < 76.0f);
    (void)ev(400, 400, TOUCH_EVENT_UP, 1);
    CHECK(!sl.dragging);
}

/* And a press that started somewhere else never becomes a drag, however far
 * it travels across the track on its way. */
TEST_CASE(a_press_from_outside_never_becomes_a_drag)
{
    fresh_slider();
    ui_slider_set(&sl, 10.0f);
    (void)ev(700, 400, TOUCH_EVENT_DOWN, 1);
    CHECK(!ev(300, 120, TOUCH_EVENT_MOVE, 1));
    CHECK(!ev(300, 120, TOUCH_EVENT_UP, 1));
    CHECK_EQ(sl.value, 10.0f);
}

TEST_CASE(a_preset_sets_its_value_and_a_slip_does_not)
{
    fresh_slider();
    ui_slider_set(&sl, 10.0f);

    const gfx_rect_t r = sl.presets[2];   /* the 50 */
    (void)ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(ev(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1));
    CHECK_EQ(sl.value, 50.0f);

    /*
     * Down on one preset, up somewhere else: nothing.  The preset pressed
     * here is the 0, not the 50 -- an earlier version pressed the one whose
     * value the slider already held, so it passed just as happily against a
     * slider that fired its preset wherever the release landed.
     */
    const gfx_rect_t zero = sl.presets[0];
    (void)ev(zero.x + zero.w / 2, zero.y + zero.h / 2, TOUCH_EVENT_DOWN, 1);
    CHECK(!ev(700, 400, TOUCH_EVENT_UP, 1));
    CHECK_EQ(sl.value, 50.0f);
}

TEST_CASE(a_second_contact_cannot_steal_the_slider)
{
    fresh_slider();
    (void)ev(150, 120, TOUCH_EVENT_DOWN, 1);
    const float held = sl.value;
    CHECK(!ev(450, 120, TOUCH_EVENT_MOVE, 2));   /* another finger */
    CHECK_EQ(sl.value, held);
    CHECK(ev(450, 120, TOUCH_EVENT_MOVE, 1));    /* ours */
    CHECK(sl.value > held);
}

/* ------------------------------------------------------------------ the tabs */

TEST_CASE(tabs_select_and_a_slip_does_not)
{
    ui_tabs_t t;
    static const char *const labels[] = { "PLOT", "TABLE", "RAW" };
    ui_tabs_init(&t, labels, 3, (gfx_rect_t){ 10, 10, 400, 30 });
    CHECK_EQ(t.selected, 0);

    const touch_event_t down = { .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1, .x = (int16_t)(t.rect[2].x + 5),
                   .y = 20, .strength = 1 } };
    const touch_event_t up = { .type = TOUCH_EVENT_UP,
        .point = { .id = 1, .x = (int16_t)(t.rect[2].x + 5),
                   .y = 20, .strength = 1 } };
    CHECK(!ui_tabs_event(&t, &down));
    CHECK(ui_tabs_event(&t, &up));
    CHECK_EQ(t.selected, 2);

    /* Re-selecting the same tab is not a change. */
    CHECK(!ui_tabs_event(&t, &down));
    CHECK(!ui_tabs_event(&t, &up));

    /* Down on a tab, up elsewhere: no change. */
    const touch_event_t away = { .type = TOUCH_EVENT_UP,
        .point = { .id = 1, .x = 700, .y = 400, .strength = 1 } };
    const touch_event_t down0 = { .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1, .x = (int16_t)(t.rect[0].x + 5),
                   .y = 20, .strength = 1 } };
    CHECK(!ui_tabs_event(&t, &down0));
    CHECK(!ui_tabs_event(&t, &away));
    CHECK_EQ(t.selected, 2);

    canvas();
    ui_tabs_render(&t, &cv);
}

/*
 * Every drawing primitive the benches are assembled from, rendered once into a
 * canvas that is bigger than the boxes they are given.
 *
 * Not a golden image -- that is render_ui.py's job on whole screens.  This is
 * the cheaper question a golden cannot answer: does any of them write outside
 * the rectangle it was handed?  A widget that overruns its box by a few pixels
 * leaves stale pixels behind on redraw, and with two alternating framebuffers
 * that presents as flicker rather than as an obviously wrong pixel.
 */
TEST_CASE(no_widget_draws_outside_the_box_it_was_given)
{
    canvas();

    /* A margin of untouched canvas all the way round. */
    const int M = 40;
    const gfx_rect_t box = { M, M, W - 2 * M, H - 2 * M };

    ui_panel(&cv, (gfx_rect_t){ box.x, box.y, 300, 90 }, "PANEL", 0x1234);
    ui_panel_header(&cv, (gfx_rect_t){ box.x + 320, box.y, 300, 30 },
                    "HEADER", 0x4321);
    ui_button(&cv, (gfx_rect_t){ box.x, box.y + 100, 120, 40 }, "GO",
              ui_theme_color(UI_C_OK), false, true);
    ui_button(&cv, (gfx_rect_t){ box.x + 130, box.y + 100, 120, 40 }, "DOWN",
              ui_theme_color(UI_C_OK), true, true);
    ui_button(&cv, (gfx_rect_t){ box.x + 260, box.y + 100, 120, 40 }, "OFF",
              ui_theme_color(UI_C_OK), false, false);
    ui_pill(&cv, (gfx_rect_t){ box.x, box.y + 150, 140, 26 }, "LINK",
            ui_theme_color(UI_C_OK), ui_theme_color(UI_C_PANEL));
    ui_bar(&cv, (gfx_rect_t){ box.x, box.y + 190, 300, 20 }, 0.62f, 0.81f,
           0x1234);
    ui_bar(&cv, (gfx_rect_t){ box.x, box.y + 220, 300, 20 }, -1.0f, 9.0f,
           0x1234);   /* out of range both ways */
    ui_value(&cv, (gfx_rect_t){ box.x, box.y + 250, 200, 50 }, "24.32", "V",
             0x1234);
    ui_chevron_left(&cv, box.x + 240, box.y + 270, 14, 0x1234);
    ui_rule(&cv, box.x, box.y + 310, 400, ui_theme_color(UI_C_EDGE));

    fresh_slider();
    sl.track   = (gfx_rect_t){ box.x, box.y + 320, 400, 34 };
    ui_slider_set_presets(&sl, k_presets, k_labels, 4,
                          (gfx_rect_t){ box.x, box.y + 360, 400, 30 });
    ui_slider_set(&sl, 64.0f);
    ui_slider_render(&sl, &cv);
    /* And with the knob at each end, where an off-by-one escapes the track. */
    ui_slider_set(&sl, 0.0f);
    ui_slider_render(&sl, &cv);
    ui_slider_set(&sl, 100.0f);
    ui_slider_render(&sl, &cv);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool inside = gfx_rect_contains(box, x, y);
            if (!inside && fb[(size_t)y * W + x] != 0) {
                T_FAIL("a widget drew at (%d,%d), outside every box", x, y);
                return;
            }
        }
    }
}

/* The two formatters the readouts depend on. */
TEST_CASE(the_formatters_round_and_pad_as_the_readouts_expect)
{
    char b[32];

    ui_fmt(b, sizeof(b), 24.318f, 2);  CHECK_STR_EQ(b, "24.32");
    ui_fmt(b, sizeof(b), 24.318f, 0);  CHECK_STR_EQ(b, "24");
    ui_fmt(b, sizeof(b), 0.0f, 1);     CHECK_STR_EQ(b, "0.0");
    ui_fmt(b, sizeof(b), -3.14f, 1);   CHECK_STR_EQ(b, "-3.1");

    ui_clock(b, sizeof(b), 0);         CHECK_STR_EQ(b, "00:00");
    ui_clock(b, sizeof(b), 257);       CHECK_STR_EQ(b, "04:17");
    ui_clock(b, sizeof(b), 3599);      CHECK_STR_EQ(b, "59:59");
}

int main(void)
{
    RUN(the_scale_ladder_has_its_fine_steps);
    RUN(the_ring_holds_its_history_and_wraps);
    RUN(a_non_finite_sample_cannot_poison_the_plot);
    RUN(the_scale_grows_at_once_and_shrinks_slowly);
    RUN(the_scale_never_falls_below_the_series_floor);
    RUN(a_legend_tap_cycles_focus_then_hidden);
    RUN(map_y_is_the_right_way_up_and_clamps);
    RUN(the_plot_and_hero_render_without_running_off_the_canvas);
    RUN(a_press_on_the_track_sets_the_value);
    RUN(a_drag_that_leaves_the_track_keeps_the_value);
    RUN(a_press_from_outside_never_becomes_a_drag);
    RUN(a_preset_sets_its_value_and_a_slip_does_not);
    RUN(a_second_contact_cannot_steal_the_slider);
    RUN(tabs_select_and_a_slip_does_not);
    RUN(no_widget_draws_outside_the_box_it_was_given);
    RUN(the_formatters_round_and_pad_as_the_readouts_expect);
    return test_summary("widgets");
}
