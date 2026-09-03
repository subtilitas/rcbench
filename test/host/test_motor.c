/*
 * What the bench screen decides: hit regions, the commands it produces, and
 * the two rules that keep a tap from spinning something.
 *
 * Not what it looks like; that is the golden image's job.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "motor_screen.h"
#include "settings.h"
#include "telemetry_sim.h"
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
    scr = motor_screen();
    scr->reset();
    motor_cmd_t junk;
    while (motor_screen_poll_cmd(&junk)) { }
}

/* Screen coordinates: the router strips the band before the screen sees an
 * event, so these are already screen-local. */
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
 * Geometry mirrored from motor_screen.c; if the layout moves these move with
 * it, and the test names say what they aim at.  Screen-local coordinates:
 * the router strips the band before a screen sees an event.
 */
#define UP_Y     24
#define UP_H     242
#define LO_Y     (UP_Y + UP_H + 6)        /* 272 */
#define TITLE_H  20
#define BTN_H    36
#define TRACK_H  40
#define ROW_Y    (LO_Y + TITLE_H + 2)
#define CTRL_Y   (ROW_Y + 42)             /* the throttle track */
#define TRACK_Y  (CTRL_Y + TRACK_H / 2)
/* ARM and RESET PEAKS live in the control panel on the right rail. */
#define ARM_X    676
#define ARM_Y    (LO_Y + TITLE_H + 8 + BTN_H / 2)
#define RESET_X  676
#define RESET_Y  (LO_Y + 154 - 6 - BTN_H / 2)
/* One percentage point at each end of the track. */
#define DOWN_X   33
#define UP_X     519
/* The track itself: x 72..486, so half of it is half the travel. */
#define TRACK_X  72
#define TRACK_W  414

static motor_cmd_t last_cmd(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    while (motor_screen_poll_cmd(&c)) { }
    return c;
}

TEST_CASE(arming_and_disarming_come_from_the_same_button)
{
    fresh();
    motor_screen_set_armed(false);
    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);

    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

/* A press that slid off the button is not a tap on it. */
TEST_CASE(a_press_that_slides_off_arm_does_nothing)
{
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ev(60, 120, TOUCH_EVENT_MOVE, 1);
    ev(60, 120, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

TEST_CASE(a_second_contact_cannot_steal_the_arm_release)
{
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);        /* not ours */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);        /* ours */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

/*
 * Two taps inside a single drain: an ARM landing on top of a DISARM must not
 * re-arm a bench the operator has just watched stop.
 */
TEST_CASE(a_pending_disarm_cannot_be_overwritten_by_an_arm)
{
    fresh();
    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);                 /* posts DISARM */
    motor_screen_set_armed(false);
    tap(ARM_X, ARM_Y);                 /* would post ARM */

    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
}

/* And it does not become a permanent block: once drained, ARM works. */
TEST_CASE(the_disarm_latch_clears_when_it_is_read)
{
    fresh();
    motor_screen_set_armed(true);
    tap(ARM_X, ARM_Y);
    (void)last_cmd();

    motor_screen_set_armed(false);
    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

/*
 * The percentage point at each end of the track.  They step the throttle and
 * post it, and they are the reason ui_slider_set() has to re-anchor a drag:
 * they move the value while a finger may be on the track.
 */
TEST_CASE(the_nudges_step_the_throttle_by_one_point)
{
    fresh();
    motor_screen_set_throttle(50.0f);
    (void)last_cmd();

    tap(UP_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 51.0f, 0.01f);
    motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 51.0f, 0.01f);

    tap(DOWN_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 50.0f, 0.01f);
    c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK_NEAR(c.value, 50.0f, 0.01f);

    /* They clamp rather than wrap at the ends of travel. */
    motor_screen_set_throttle(0.0f);
    (void)last_cmd();
    tap(DOWN_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    motor_screen_set_throttle(100.0f);
    (void)last_cmd();
    tap(UP_X, TRACK_Y);
    CHECK_NEAR(motor_screen_throttle(), 100.0f, 0.01f);
}

/*
 * The rated kV comes from the connected ESC when it reports one, and from the
 * setting when it does not.  Neither leaves the field empty: a guessed kV
 * gives a plausible-looking efficiency that is wrong, and the schema
 * therefore defaults to zero rather than to a typical motor.
 */
TEST_CASE(the_rated_kv_prefers_the_esc_over_the_entered_value)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    gfx_color_t *shot = malloc(bytes);
    CHECK(shot != NULL);
    if (shot == NULL) {
        return;
    }

    fresh();
    settings_reset_all();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    CHECK_EQ(settings_get_int(SET_MOTOR_KV), 0);   /* nothing assumed */
    scr->render(&cv, 0);
    memcpy(shot, fb, bytes);

    /* A value the operator entered fills the field. */
    settings_set(SET_MOTOR_KV, 1000.0f);
    motor_invalidate();
    scr->render(&cv, 0);
    CHECK(memcmp(shot, fb, bytes) != 0);
    memcpy(shot, fb, bytes);

    /* And what the ESC reports takes it back off them. */
    motor_screen_set_esc_kv(2000);
    motor_invalidate();
    scr->render(&cv, 0);
    CHECK(memcmp(shot, fb, bytes) != 0);

    /* Zero from the ESC is "it did not say", not "zero". */
    memcpy(shot, fb, bytes);
    motor_screen_set_esc_kv(0);
    motor_invalidate();
    scr->render(&cv, 0);
    CHECK(memcmp(shot, fb, bytes) != 0);

    settings_reset_all();
    free(shot);
}

/*
 * The throttle moves by how far a finger travels, not to where it lands.
 *
 * A press used to command the value under it, so touching the right-hand end
 * of the track asked for full travel in one contact.  The output bank's slew
 * ramps rather than steps, which bounds how fast the motor follows, but the
 * command still went to 100 and nothing had to be confirmed.
 */
TEST_CASE(the_throttle_track_moves_by_how_far_it_is_dragged)
{
    fresh();

    /* A press commands nothing, wherever on the track it lands. */
    ev(TRACK_X + TRACK_W - 4, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
    ev(TRACK_X + TRACK_W - 4, TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);

    /* Half the track's width of drag is half the travel, wherever it began. */
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_DOWN, 1);
    ev(TRACK_X + 10 + TRACK_W / 2, TRACK_Y, TOUCH_EVENT_MOVE, 1);
    const motor_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, MOTOR_CMD_THROTTLE);
    CHECK(c.value > 45.0f && c.value < 55.0f);
    CHECK_NEAR(motor_screen_throttle(), c.value, 0.01f);

    /* And dragging back the same distance returns it. */
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_MOVE, 1);
    ev(TRACK_X + 10, TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_NEAR(motor_screen_throttle(), 0.0f, 0.01f);
}
TEST_CASE(reset_peaks_posts_its_own_command)
{
    fresh();
    tap(RESET_X, RESET_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_RESET_PEAKS);
}

/*
 * Leaving disarms.  Navigating away from an armed bench must not leave a
 * propeller spinning behind a screen that does not show it, and the command
 * carries the disarm rather than the application inferring it from the
 * navigation.
 */
TEST_CASE(leaving_the_screen_disarms)
{
    fresh();
    motor_screen_set_armed(true);
    scr->leave();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

TEST_CASE(the_tabs_switch_panes_and_both_render)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
        motor_screen_push(&b);
    }

    /*
     * Compared pixel for pixel, not by counting non-zero pixels: gfx_clear
     * fills with the background colour rather than with zero, so a non-zero
     * count is the whole canvas (384,000) for any pane.
     */
    scr->render(&cv, 0);
    gfx_color_t *plot = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(plot, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(99, 11);                        /* the TABLE tab */
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != plot[i]) { ++differ; }
    }
    /* Both panes sit in the same card on the same sunken ground, so the
     * difference is their content and not the background; 6000 pixels is
     * sized to that. */
    if (differ < 6000) {
        T_FAIL("the two panes differ by only %d pixels", differ);
    }

    /* And back again lands on what it started as. */
    tap(37, 11);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(plot, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(plot);
}

/*
 * The screen skips painting what has not changed, which keeps it off the
 * PSRAM (pseudo-static random-access memory) bus the LCD (liquid-crystal
 * display) is scanning out of.  The failure mode that buys is a stale
 * framebuffer: the panel alternates between two, so a buffer whose last paint
 * was a sample ago still needs one even when the other is current.
 *
 * Both counters are checked, because they are bumped by different things
 * (new numbers and a touch), and either one forgetting which buffer it drew
 * into leaves half the frames showing the previous value.
 */
TEST_CASE(each_framebuffer_is_updated_independently)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    gfx_color_t *other = malloc((size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, other, W, H, W);

    /* Both buffers start current and identical. */
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 20.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* New numbers, painted into buffer 1 only.  Buffer 0 is then a sample
     * behind, and the next render into it has to notice. */
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 95.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    scr->render(&cv1, 1);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* Same again for a control: the ARM button changes with no new sample. */
    motor_screen_set_armed(true);
    scr->render(&cv1, 1);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And a frame with nothing new must leave a correct buffer alone: checked
     * by rendering twice and requiring the second to be a no-op on
     * already-correct pixels. */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(other);
}

/*
 * Redrawing state B on top of state A must give the same pixels as drawing B
 * onto a buffer that never saw A.  Anything drawn outside the region it
 * clears violates that, and with alternating framebuffers the leftovers show
 * up as flicker rather than as an obviously stale pixel.
 */
TEST_CASE(a_redraw_leaves_no_stale_pixels)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    /* State A: a loaded bench, armed, throttle high. */
    for (int i = 0; i < 300; ++i) {
        telemetry_sim_step(&sim, 95.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    motor_screen_set_armed(true);
    motor_screen_set_throttle(95.0f);
    scr->render(&cv, 0);

    /* Then state B on top of it. */
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 5.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    motor_screen_set_armed(false);
    motor_screen_set_throttle(5.0f);
    scr->render(&cv, 0);
    gfx_color_t *over = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(over, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same state B, onto a buffer that never saw A. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    motor_invalidate();
    scr->render(&cv, 0);

    CHECK_EQ(memcmp(over, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(over);
}

/*
 * RGB565 components, for comparing a button's fill without pinning it.
 * Scaled to 8 bits, because red carries 5 bits and green 6 and the raw
 * fields are not comparable with each other.
 */
static int red_of(gfx_color_t c)   { return (int)(((c >> 11) & 0x1f) << 3); }
static int green_of(gfx_color_t c) { return (int)(((c >> 5) & 0x3f) << 2); }
static gfx_color_t arm_px(void)
{
    /* Inside the ARM button, clear of its rounded corner, its hairline and
     * the centred label. */
    return fb[(size_t)(ARM_Y + 8) * W + (ARM_X - 90)];
}

/*
 * ARM says what the release will do: green while it will arm, fading to the
 * danger red over ARM_FADE_S while the finger stays down, and one flash as
 * the arm takes effect.  Each is its own animation, and the button carries
 * its own revision so a frame of either repaints 180 x 28 px rather than the
 * 800 x 90 control row.
 */
TEST_CASE(the_arm_button_fades_while_held_and_flashes_on_arming)
{
    fresh();
    scr->render(&cv, 0);
    const gfx_color_t idle = arm_px();
    CHECK(green_of(idle) > red_of(idle));   /* the OK green */

    /* Down on ARM, before the fade has run. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    scr->render(&cv, 0);
    const gfx_color_t held0 = arm_px();

    /* Held for the whole fade. */
    for (int i = 0; i < 20; ++i) {
        scr->tick(0.05f);
    }
    scr->render(&cv, 0);
    const gfx_color_t held1 = arm_px();
    if (!(red_of(held1) > red_of(held0) && red_of(held1) > green_of(held1))) {
        T_FAIL("held ARM went %04x -> %04x, which is not a fade to red",
               held0, held1);
    }

    /* Arming flashes it, and the flash decays. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    motor_screen_set_armed(true);
    scr->render(&cv, 0);
    const gfx_color_t flash = arm_px();
    for (int i = 0; i < 10; ++i) {
        scr->tick(0.05f);
    }
    scr->render(&cv, 0);
    const gfx_color_t settled = arm_px();
    if (!(red_of(flash) >= red_of(settled)
          && green_of(flash) > green_of(settled))) {
        T_FAIL("arming went %04x -> %04x, which is not a flash that decays",
               flash, settled);
    }

    /* And the fade does not run once armed: DISARM is not about to arm. */
    for (int i = 0; i < 20; ++i) {
        scr->tick(0.05f);
    }
    scr->render(&cv, 0);
    CHECK_EQ(arm_px(), settled);
}

/*
 * A drag moves the throttle and nothing else, which is its own repaint path:
 * the readout's box and the slider, without the row's buttons.  The thumb
 * and its shadow stand proud of the track, so a path that clears the track
 * and not ui_slider_painted_rect() leaves a thumb behind at every position
 * the finger passed through.  The other stale-pixel case changes the armed
 * state, which takes the whole row and never exercises this.
 */
TEST_CASE(dragging_the_throttle_leaves_no_stale_pixels)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 40.0f, 0.05f, &b);
        motor_screen_push(&b);
    }

    /* Both framebuffers settled, the way the panel leaves them. */
    motor_screen_set_throttle(0.0f);
    scr->render(&cv, 0);
    scr->render(&cv, 1);

    /* The finger travels the whole track, a frame per step. */
    for (int v = 0; v <= 100; v += 5) {
        motor_screen_set_throttle((float)v);
        scr->render(&cv, (v / 5) & 1);
    }
    const int last_buf = (100 / 5) & 1;
    gfx_color_t *dragged = malloc(bytes);
    CHECK(dragged != NULL);
    if (dragged == NULL) {
        return;
    }
    memcpy(dragged, fb, bytes);

    /* The same value, onto a buffer that never saw the drag. */
    memset(fb, 0, bytes);
    motor_invalidate();
    scr->render(&cv, last_buf);

    if (memcmp(dragged, fb, bytes) != 0) {
        long differ = 0;
        for (long i = 0; i < (long)W * H; ++i) {
            if (dragged[i] != fb[i]) {
                ++differ;
            }
        }
        T_FAIL("a drag left %ld px that a fresh render does not draw", differ);
    }
    free(dragged);
}

/*
 * Before a single poll has answered there is nothing to show, and 0.00 V is
 * a wrong reading; the heroes print a placeholder, so the screen differs
 * visibly from one holding real numbers.
 */
TEST_CASE(an_unanswered_bench_does_not_show_numbers)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *blank = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(blank, fb, (size_t)W * H * sizeof(gfx_color_t));

    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
    motor_screen_push(&b);
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != blank[i]) { ++differ; }
    }
    CHECK(differ > 1000);
    free(blank);
}

int main(void)
{
    RUN(arming_and_disarming_come_from_the_same_button);
    RUN(a_press_that_slides_off_arm_does_nothing);
    RUN(a_second_contact_cannot_steal_the_arm_release);
    RUN(a_pending_disarm_cannot_be_overwritten_by_an_arm);
    RUN(the_disarm_latch_clears_when_it_is_read);
    RUN(the_nudges_step_the_throttle_by_one_point);
    RUN(the_rated_kv_prefers_the_esc_over_the_entered_value);
    RUN(the_throttle_track_moves_by_how_far_it_is_dragged);
    RUN(reset_peaks_posts_its_own_command);
    RUN(leaving_the_screen_disarms);
    RUN(the_tabs_switch_panes_and_both_render);
    RUN(each_framebuffer_is_updated_independently);
    RUN(a_redraw_leaves_no_stale_pixels);
    RUN(dragging_the_throttle_leaves_no_stale_pixels);
    RUN(the_arm_button_fades_while_held_and_flashes_on_arming);
    RUN(an_unanswered_bench_does_not_show_numbers);
    return test_summary("motor");
}
