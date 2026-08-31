/*
 * What the analyser decides: which state word it shows, and that a failsafe
 * frame is never presented as if it were live.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

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
 * The whole reason this screen exists.  A receiver in failsafe sends sixteen
 * well-formed values that it made up, and a bench that draws them the same
 * colour as live ones is helping somebody trust invented data.
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
    /* And the bars themselves change colour, not merely a badge somewhere. */
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
 * The question the screen is built to answer -- "I moved that, which channel
 * was it?" -- lives in the history.  Push a run of flat frames, then one where
 * a single channel has stepped, and the trace has to change: a screen that
 * showed only the present value could not tell the moved channel from the
 * still ones.
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

/* A live frame after silence returns to live -- the state is current, not
 * sticky. */
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

int main(void)
{
    RUN(a_failsafe_frame_is_not_drawn_like_a_live_one);
    RUN(silence_is_its_own_state);
    RUN(frame_lost_warns_without_claiming_failsafe);
    RUN(both_panes_render_and_differ);
    RUN(a_moved_channel_changes_the_trace);
    RUN(a_frame_after_silence_is_live_again);
    return test_summary("analyser");
}
