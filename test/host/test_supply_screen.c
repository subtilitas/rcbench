/*
 * What the supply screen decides: the switch's hold and tap, the set points
 * and their snapping, what a stop and a touch loss leave behind, and where a
 * run's trace starts and stops.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "supply.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_color_t *fb2;
static gfx_canvas_t cv;
static gfx_canvas_t cv2;
static const ui_screen_t *scr;

static void drain(void)
{
    supply_cmd_t junk;
    while (supply_screen_poll_cmd(&junk)) { }
}

static void fresh(void)
{
    if (fb == NULL) {
        fb  = calloc((size_t)W * H, sizeof(gfx_color_t));
        fb2 = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    memset(fb2, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    gfx_canvas_init(&cv2, fb2, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = supply_screen();
    scr->reset();
    drain();
}

static void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
static void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                ev(x, y, TOUCH_EVENT_UP, 1); }

/*
 * Geometry mirrored from supply_screen.c, screen-local: the router strips the
 * band before a screen sees an event.
 */
#define OUT_X     676          /* OUTPUT ON / OFF, right rail */
#define OUT_Y     318
#define RESET_X   676          /* RESET PEAKS */
#define RESET_Y   402
#define V_DOWN_X  39           /* the set points' fine steps */
#define V_UP_X    519
#define V_ROW_Y   335
#define I_ROW_Y   397
#define TRACK_X   72           /* both tracks: x 72..485 */
#define TRACK_W   414
#define TABLE_X   99           /* the TABLE tab */
#define TABLE_Y   11
#define OFF_X     300          /* the plot, clear of every control */
#define OFF_Y     120

#define TICK_S     0.05f
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static void tick_for(int steps)
{
    for (int i = 0; i < steps; ++i) {
        scr->tick(TICK_S);
    }
}

static void hold_on(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

/* ---------------------------------------------------------------- switch */

TEST_CASE(the_output_comes_on_with_a_hold_and_only_once)
{
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    CHECK(!supply_screen_poll_cmd(&c));       /* half a hold asks nothing */
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
    tick_for(HOLD_TICKS);                     /* still down: no second ON */
    CHECK(!supply_screen_poll_cmd(&c));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(a_short_press_on_output_on_does_nothing)
{
    fresh();
    tap(OUT_X, OUT_Y);
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_finger_that_leaves_the_switch_switches_nothing)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_MOVE, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_MOVE, 1);    /* coming back is not resuming */
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(the_output_goes_off_with_a_tap)
{
    fresh();
    supply_screen_set_output(true);
    supply_cmd_t c;
    tap(OUT_X, OUT_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* A press that slides off before lifting is not a tap. */
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(the_release_that_ends_the_hold_does_not_switch_off)
{
    /* The supply reports the output on while the finger is still down. */
    fresh();
    supply_cmd_t c;
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));

    /* And after the finger has lifted. */
    fresh();
    hold_on();
    CHECK(supply_screen_poll_cmd(&c) && c.on);
    supply_screen_set_output(true);
    CHECK(!supply_screen_poll_cmd(&c));
}

TEST_CASE(an_off_outranks_an_on_in_the_same_poll)
{
    fresh();
    supply_cmd_t c;
    hold_on();                         /* ON posted, not yet collected */
    supply_screen_set_output(true);    /* the supply reports it on */
    tap(OUT_X, OUT_Y);                 /* and the operator turns it off */
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);

    /* And an ON completed after an OFF that is still pending: the output
     * went off under it (a stop) before the OFF was collected. */
    fresh();
    supply_screen_set_output(true);
    tap(OUT_X, OUT_Y);
    supply_screen_set_output(false);
    hold_on();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);
}

TEST_CASE(a_second_contact_cannot_steal_the_release)
{
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 2);
    CHECK(!supply_screen_poll_cmd(NULL));
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(supply_screen_poll_cmd(&c) && c.off);
}

TEST_CASE(a_second_contact_on_another_button_cannot_steal_the_off)
{
    fresh();
    supply_cmd_t c;
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 2);
    ev(RESET_X, RESET_Y, TOUCH_EVENT_UP, 2);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_DOWN, 3);
    ev(V_UP_X, V_ROW_Y, TOUCH_EVENT_UP, 3);
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
    CHECK(!c.reset);

    /* And the buttons answer again once the first contact has lifted. */
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c) && c.reset);
}

TEST_CASE(a_stop_abandons_the_hold_and_drops_a_posted_on)
{
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    supply_screen_cancel_on();
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* An ON already posted and not collected goes too. */
    hold_on();
    supply_screen_cancel_on();
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_stop_while_the_output_reports_off_strands_no_press)
{
    /* The output goes off under a finger on OUTPUT OFF: the press neither
     * switches it back on nor waits to. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    supply_screen_set_output(false);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_touch_loss_drops_an_on_and_keeps_an_off)
{
    supply_cmd_t c;
    fresh();
    hold_on();
    scr->cancel();
    CHECK(!supply_screen_poll_cmd(&c));

    /* A hold under way when the loss lands asks for nothing. */
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->cancel();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(&c));

    /* A press on OUTPUT OFF whose release went missing is an OFF made. */
    fresh();
    supply_screen_set_output(true);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.off);
}

TEST_CASE(leaving_keeps_the_output_and_drops_the_hold)
{
    fresh();
    supply_screen_set_output(true);
    scr->leave();
    CHECK(!supply_screen_poll_cmd(NULL));

    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    tick_for(HOLD_TICKS);
    CHECK(!supply_screen_poll_cmd(NULL));

    /* And the switch comes back unfaded: the screen on return draws as one
     * that was never pressed. */
    fresh();
    scr->leave();
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    fresh();
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    scr->leave();
    scr->render(&cv, 0);
    const int same = memcmp(fb, was, (size_t)W * H * sizeof(gfx_color_t));
    free(was);
    CHECK_EQ(same, 0);
}

TEST_CASE(reset_peaks_posts_its_own_command)
{
    fresh();
    supply_cmd_t c;
    tap(RESET_X, RESET_Y);
    CHECK(supply_screen_poll_cmd(&c));
    CHECK(c.reset);
    CHECK(!c.on && !c.off);

    /* Slid off before the release: nothing. */
    ev(RESET_X, RESET_Y, TOUCH_EVENT_DOWN, 1);
    ev(OFF_X, OFF_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_screen_poll_cmd(&c));
}

/* ------------------------------------------------------------ set points */

TEST_CASE(the_fine_steps_move_a_set_point_by_a_tenth)
{
    fresh();
    CHECK_NEAR(supply_screen_set_v(), 6.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_UP_X, V_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);

    tap(V_DOWN_X, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_v(), 6.1f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 1.9f, 1e-4f);

    /* Down past the bottom of the range stops at it. */
    for (int k = 0; k < 40; ++k) {
        tap(V_DOWN_X, I_ROW_Y);
    }
    CHECK_NEAR(supply_screen_set_i(), 0.5f, 1e-4f);

    /* A set point is a level, not a command: nothing is posted. */
    CHECK(!supply_screen_poll_cmd(NULL));
}

TEST_CASE(a_tap_on_a_track_sets_a_value_the_supply_takes)
{
    fresh();
    tap(TRACK_X + TRACK_W / 2, V_ROW_Y);
    /* About the middle of 3.3 to 21 V, on a 20 mV step above 3.3 V. */
    const float v = supply_screen_set_v();
    CHECK(v > 11.5f && v < 12.7f);
    const float steps = (v - 3.3f) / 0.02f;
    CHECK_NEAR(steps, roundf(steps), 1e-2f);

    /* The current track the same, on its 50 mA step. */
    tap(TRACK_X + TRACK_W - 1, I_ROW_Y);
    CHECK_NEAR(supply_screen_set_i(), 5.0f, 1e-4f);
}

TEST_CASE(new_caps_pull_the_set_points_into_range)
{
    fresh();
    const supply_caps_t narrow = { 5.0f, 5.0f, 0.0f, 0.5f, 3.0f, 0.05f };
    supply_screen_set_caps(&narrow);
    CHECK_NEAR(supply_screen_set_v(), 5.0f, 1e-4f);
    CHECK_NEAR(supply_screen_set_i(), 2.0f, 1e-4f);
    const supply_caps_t low = { 3.3f, 21.0f, 0.02f, 0.5f, 1.0f, 0.05f };
    supply_screen_set_caps(&low);
    CHECK_NEAR(supply_screen_set_i(), 1.0f, 1e-4f);
    supply_screen_set_caps(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));     /* caps are not a command */
}

/* ------------------------------------------------------------------ plot */

static supply_state_t reading(float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.v = v; st.i = i; st.p = v * i;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.online = true;
    st.mode = SUPPLY_MODE_CV;
    return st;
}

/* A whole frame drawn from nothing, so two screens can be compared by their
 * pixels: the trace, the readouts and the panel's title tag. */
static void render_both(void)
{
    scr->render(&cv, 0);
    supply_invalidate();
    scr->render(&cv, 0);
}

TEST_CASE(a_run_is_traced_from_the_switch_on_and_held_after_it)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);               /* off: no trace */
    render_both();

    /* The reference: a screen that only ever saw the run. */
    supply_screen_set_output(true);
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);              /* after the run: readouts only */
    render_both();
    gfx_color_t *want = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(want, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same run with off-time samples before it on a used screen. */
    fresh();
    for (int k = 0; k < 50; ++k) {
        st = reading(9.0f, 3.0f);
        supply_screen_push(&st);
    }
    supply_screen_set_output(true);        /* a run before it, */
    for (int k = 0; k < 40; ++k) {
        st = reading(9.0f, 3.0f);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);       /* held, */
    supply_screen_set_output(true);        /* and cleared by the next */
    for (int k = 0; k < 30; ++k) {
        st = reading(6.0f, 1.0f + 0.02f * (float)k);
        st.output = true;
        supply_screen_push(&st);
    }
    supply_screen_set_output(false);
    st = reading(0.0f, 0.0f);
    supply_screen_push(&st);
    render_both();
    const int same = memcmp(fb, want, (size_t)W * H * sizeof(gfx_color_t));
    free(want);
    CHECK_EQ(same, 0);
}

/* ---------------------------------------------------------------- redraw */

/*
 * Drawing state B over state A must give the pixels B gives on a buffer that
 * never saw A; with two framebuffers, anything left over shows as flicker.
 */
static void check_redraw(void (*to_b)(void))
{
    fresh();
    supply_state_t st = reading(6.0f, 1.2f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    to_b();
    for (int k = 0; k < 8; ++k) {          /* past any flash */
        scr->render(&cv, 0);
    }
    supply_invalidate();
    scr->render(&cv2, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

static void b_output_on(void)
{
    supply_screen_set_output(true);
    supply_state_t st = reading(5.95f, 1.8f);
    st.output = true;
    st.mode = SUPPLY_MODE_CC;
    supply_screen_push(&st);
}
static void b_set_point(void) { tap(V_UP_X, V_ROW_Y); drain(); }
static void b_table(void)     { tap(TABLE_X, TABLE_Y); }
static void b_offline(void)
{
    supply_state_t st = reading(0.0f, 0.0f);
    st.online = false;
    st.ok = 0u;
    supply_screen_set_model(true);
    supply_screen_push(&st);
}
static void b_table_then_set(void)
{
    tap(TABLE_X, TABLE_Y);
    scr->render(&cv, 0);
    tap(V_DOWN_X, I_ROW_Y);
    drain();
}
static void b_hold_half(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 2);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

TEST_CASE(a_redraw_leaves_no_stale_pixels)
{
    check_redraw(b_output_on);
    check_redraw(b_set_point);
    check_redraw(b_table);
    check_redraw(b_offline);
    check_redraw(b_table_then_set);
    check_redraw(b_hold_half);
}

TEST_CASE(each_framebuffer_is_updated_independently)
{
    fresh();
    supply_state_t st = reading(6.0f, 1.0f);
    supply_screen_push(&st);
    scr->render(&cv, 0);
    scr->render(&cv2, 1);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A run, a set point and the switch, painted into buffer 1 first. */
    b_output_on();
    tap(V_UP_X, V_ROW_Y);
    drain();
    for (int k = 0; k < 8; ++k) {
        scr->render(&cv2, 1);
        scr->render(&cv, 0);
    }
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A frame with nothing new leaves a correct buffer correct. */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, fb2, (size_t)W * H * sizeof(gfx_color_t)), 0);
}

TEST_CASE(a_screen_without_a_sample_draws)
{
    /* No sample yet, no caps from a driver: the screen draws the model's
     * defaults and nothing reads uninitialised. */
    fresh();
    scr->render(&cv, 0);
    supply_screen_push(NULL);
    scr->event(NULL);
    CHECK(!supply_screen_poll_cmd(NULL));
}

int main(void)
{
    RUN(the_output_comes_on_with_a_hold_and_only_once);
    RUN(a_short_press_on_output_on_does_nothing);
    RUN(a_finger_that_leaves_the_switch_switches_nothing);
    RUN(the_output_goes_off_with_a_tap);
    RUN(the_release_that_ends_the_hold_does_not_switch_off);
    RUN(an_off_outranks_an_on_in_the_same_poll);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(a_second_contact_on_another_button_cannot_steal_the_off);
    RUN(a_stop_abandons_the_hold_and_drops_a_posted_on);
    RUN(a_stop_while_the_output_reports_off_strands_no_press);
    RUN(a_touch_loss_drops_an_on_and_keeps_an_off);
    RUN(leaving_keeps_the_output_and_drops_the_hold);
    RUN(reset_peaks_posts_its_own_command);
    RUN(the_fine_steps_move_a_set_point_by_a_tenth);
    RUN(a_tap_on_a_track_sets_a_value_the_supply_takes);
    RUN(new_caps_pull_the_set_points_into_range);
    RUN(a_run_is_traced_from_the_switch_on_and_held_after_it);
    RUN(a_redraw_leaves_no_stale_pixels);
    RUN(each_framebuffer_is_updated_independently);
    RUN(a_screen_without_a_sample_draws);
    free(fb);
    free(fb2);
    return test_summary("supply_screen");
}
