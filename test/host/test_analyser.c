/*
 * What the analyser decides: which state word it shows, and that a failsafe
 * frame is never presented as if it were live.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "tick_wrap.h"

#include "analyser_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

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
    scr = analyser_screen();
    scr->reset();
}

static int count_of(gfx_color_t c)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == c) { ++n; }
    }
    return n;
}

static void push_at(uint32_t now, uint16_t all, int ch, uint16_t value,
                    bool failsafe, bool frame_lost)
{
    sbus_frame_t f;
    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
        f.channel[i] = all;
    }
    if (ch >= 0) {
        f.channel[ch] = value;
    }
    f.failsafe   = failsafe;
    f.frame_lost = frame_lost;
    sbus_decoder_t d;
    sbus_decoder_reset(&d);
    d.frames = 100;
    uint8_t raw[SBUS_FRAME_BYTES];
    memset(raw, 0, sizeof(raw));
    raw[0] = SBUS_HEADER;
    analyser_screen_push(&f, &d, raw, SBUS_FRAME_BYTES, now);
}

static void push(bool failsafe, bool frame_lost)
{
    push_at(10, 1024, -1, 0, failsafe, frame_lost);
}

/*
 * Pins: a receiver in failsafe sends sixteen well-formed values it generates
 * itself, and the screen draws them in the danger colour, not the live one.
 */
TEST_CASE(a_failsafe_frame_is_not_drawn_like_a_live_one)
{
    fresh();
    push(false, false);
    scr->render(&cv, 0);
    const int live_accent = count_of(ui_theme_color(UI_C_ACCENT));
    const int live_danger = count_of(ui_theme_color(UI_C_DANGER));

    fresh();
    push(true, false);
    scr->render(&cv, 0);
    const int fs_danger = count_of(ui_theme_color(UI_C_DANGER));

    CHECK(live_accent > 0);
    CHECK(fs_danger > live_danger);
    /* The bars themselves change colour, not only a badge. */
    if (fs_danger < 400) {
        T_FAIL("failsafe painted only %d danger pixels; the channels are "
               "still drawn as though they meant something", fs_danger);
    }
}

/* Nothing pushed is not the same as zeros pushed. */
TEST_CASE(silence_is_its_own_state)
{
    fresh();
    scr->render(&cv, 0);
    const int quiet = count_of(ui_theme_color(UI_C_OK));
    CHECK_EQ(quiet, 0);

    push(false, false);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_OK)) > 0);

    analyser_screen_silent(500);
    scr->render(&cv, 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);
}

/* Frame-lost is a third state, and warns rather than alarms: the frame did
 * not arrive whole, which is not the same as the transmitter being gone. */
TEST_CASE(frame_lost_warns_without_claiming_failsafe)
{
    fresh();
    push(false, true);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_WARN)) > 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);
}

/*
 * A cancel abandons a tab press: the panel's record of the glass is stale,
 * and the track id the press owned is one the controller reuses, so a later
 * contact lifting over the tab must not switch the pane.
 */
TEST_CASE(a_cancelled_tab_press_does_not_switch_the_pane)
{
    fresh();
    push(false, false);
    scr->render(&cv, 0);
    gfx_color_t *chan = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(chan, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1, .x = 200, .y = 20, .strength = 40 } };
    scr->event(&e);
    scr->cancel();
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(chan, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And the row is not stuck. */
    e.type = TOUCH_EVENT_DOWN;
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
    scr->render(&cv, 0);
    CHECK(memcmp(chan, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    free(chan);
}

/*
 * A press already drawn is drawn released once it is cancelled.  The screen
 * caches its frame per buffer by revision, so a cancel that cleared the
 * press without moving the revision would leave the tab painted held.
 */
TEST_CASE(a_cancelled_tab_press_already_drawn_is_redrawn_released)
{
    fresh();
    push(false, false);
    scr->render(&cv, 0);
    gfx_color_t *idle = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(idle, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1, .x = 200, .y = 20, .strength = 40 } };
    scr->event(&e);
    /* A tab press alone does not repaint; telemetry arriving under the
     * finger does, which is what paints the press.  Forced here. */
    analyser_invalidate();
    scr->render(&cv, 0);
    /* The press is visible, or this case proves nothing. */
    CHECK(memcmp(idle, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);

    scr->cancel();
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(idle, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(idle);
}

/* Both panes draw, and they draw different things. */
TEST_CASE(both_panes_render_and_differ)
{
    fresh();
    push(false, false);
    scr->render(&cv, 0);
    gfx_color_t *chan = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(chan, fb, (size_t)W * H * sizeof(gfx_color_t));

    const touch_event_t down = { .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1, .x = 200, .y = 20, .strength = 40 } };
    const touch_event_t up = { .type = TOUCH_EVENT_UP,
        .point = { .id = 1, .x = 200, .y = 20, .strength = 40 } };
    scr->event(&down);
    scr->event(&up);
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != chan[i]) { ++differ; }
    }
    if (differ < 3000) {
        T_FAIL("the two panes differ by only %d pixels", differ);
    }
    free(chan);
}

/*
 * Pins: the history trace changes when one channel steps.  A run of flat
 * frames, then frames with channel 5 stepped, and the rendered trace differs.
 */
TEST_CASE(a_moved_channel_changes_the_trace)
{
    fresh();
    uint32_t t = 10;
    for (int i = 0; i < 40; ++i) {
        push_at(t, 1024, -1, 0, false, false);
        t += 20;
    }
    scr->render(&cv, 0);
    gfx_color_t *flat = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(flat, fb, (size_t)W * H * sizeof(gfx_color_t));

    for (int i = 0; i < 40; ++i) {
        push_at(t, 1024, 5, 1800, false, false);   /* channel 5 steps up */
        t += 20;
    }
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != flat[i]) { ++differ; }
    }
    free(flat);
    if (differ < 200) {
        T_FAIL("a stepped channel moved only %d pixels of trace", differ);
    }
}

/* A live frame after silence returns the state to live: the state is
 * current, not sticky. */
TEST_CASE(a_frame_after_silence_is_live_again)
{
    fresh();
    push(false, false);
    scr->render(&cv, 0);
    const int live = count_of(ui_theme_color(UI_C_OK));
    CHECK(live > 0);

    analyser_screen_silent(2000);
    scr->render(&cv, 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);

    push_at(3000, 1024, -1, 0, false, false);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_OK)) > 0);
}

/* A sum of the frame buffer: two renders with the same sum are taken as
 * the same picture. */
static uint32_t picture(void)
{
    uint32_t sum = 2166136261u;
    for (int i = 0; i < W * H; ++i) {
        sum = (sum ^ fb[i]) * 16777619u;
    }
    return sum;
}

/* Frames @p every ms apart, the first one that long after @p t0, for
 * @p ms; the picture then. */
static uint32_t stream(uint32_t t0, uint32_t every, uint32_t ms)
{
    fresh();
    for (uint32_t t = every; t <= ms; t += every) {
        push_at(t0 + t, 1024, -1, 0, false, false);
    }
    scr->render(&cv, 0);
    return picture();
}

/*
 * RATE is the frames of a whole second.  A stream of 100 frames a second
 * shows the same picture 1250 ms and 2500 ms after its start whether it
 * began at tick 0 or 500 ms or 1500 ms before the 2^32 ms wrap, and a
 * stream of 50 frames a second shows another: the trace is as long as the
 * screen keeps it in all of them, so the pictures differ in the rate alone.
 */
static uint32_t g_rate_100[2];

static void a_stream_shows_its_rate(uint32_t t0)
{
    static const uint32_t k_at[2] = { 1250u, 2500u };
    for (unsigned i = 0; i < 2u; ++i) {
        const uint32_t at_100 = stream(t0, 10u, k_at[i]);
        const uint32_t at_50  = stream(t0, 20u, 2u * k_at[i]);
        CHECK(at_100 != at_50);
        if (t0 == 0u) {
            g_rate_100[i] = at_100;
        } else {
            CHECK_EQ(at_100, g_rate_100[i]);
        }
    }
}

TEST_CASE(the_rate_is_counted_per_second_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(a_stream_shows_its_rate, 500u);
    at_tick_0_and_before_the_wrap(a_stream_shows_its_rate, 1500u);
}

int main(void)
{
    RUN(a_failsafe_frame_is_not_drawn_like_a_live_one);
    RUN(silence_is_its_own_state);
    RUN(frame_lost_warns_without_claiming_failsafe);
    RUN(both_panes_render_and_differ);
    RUN(a_moved_channel_changes_the_trace);
    RUN(a_frame_after_silence_is_live_again);
    RUN(a_cancelled_tab_press_does_not_switch_the_pane);
    RUN(a_cancelled_tab_press_already_drawn_is_redrawn_released);
    RUN(the_rate_is_counted_per_second_across_the_tick_wrap);
    return test_summary("analyser");
}
