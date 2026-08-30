/*
 * What the analyser decides: which state word it shows, and that a failsafe
 * frame is never presented as if it were live.
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

static void push(bool failsafe, bool frame_lost)
{
    sbus_frame_t f;
    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
        f.channel[i] = 1024;
    }
    f.failsafe   = failsafe;
    f.frame_lost = frame_lost;
    sbus_decoder_t d;
    sbus_decoder_reset(&d);
    d.frames = 100;
    uint8_t raw[SBUS_FRAME_BYTES];
    memset(raw, 0, sizeof(raw));
    raw[0] = SBUS_HEADER;
    analyser_screen_push(&f, &d, raw, SBUS_FRAME_BYTES, 10);
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

int main(void)
{
    RUN(a_failsafe_frame_is_not_drawn_like_a_live_one);
    RUN(silence_is_its_own_state);
    RUN(frame_lost_warns_without_claiming_failsafe);
    RUN(both_panes_render_and_differ);
    return test_summary("analyser");
}
