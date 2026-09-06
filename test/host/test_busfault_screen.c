/*
 * The bus-fault screen: the acknowledgement gesture, and the one latch the
 * application reads.
 *
 * What is under test is the gesture, not the words. A screen that says the
 * bench cannot be trusted must not be dismissible by a touch that could have
 * been a sleeve, and it must hand its acknowledgement over exactly once --
 * a second read that still returned true would send the operator back to the
 * menu from wherever they had got to.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "busfault_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_W 800
#define SCREEN_H 480

/* Mirrored from busfault_screen.c: the button across the bottom. */
#define ACK_H 52
#define ACK_Y (SCREEN_H - ACK_H - 18)
#define ACK_X 24

/* A frame at the panel's rate, and enough of them to pass the hold. */
#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static gfx_color_t s_px[SCREEN_W * SCREEN_H];
static gfx_canvas_t s_c;

static const ui_screen_t *scr(void) { return busfault_screen(); }

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    scr()->reset();
    gfx_canvas_init(&s_c, s_px, SCREEN_W, SCREEN_H, SCREEN_W);

    const busfault_report_t r = {
        .verdict = CAN_SELFTEST_CORRUPT,
        .sent = 1284, .echoed = 1197, .corrupt = 71, .lost = 16,
        .bus_errors = 87, .have_remote = true, .remote_up = true,
    };
    busfault_screen_set(&r);
    (void)busfault_screen_take_ack();   /* start from a clean latch */
}

static void down(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr()->event(&e);
}

static void move(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_MOVE,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr()->event(&e);
}

static void up(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_UP,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 0 } };
    scr()->event(&e);
}

static void hold(int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        scr()->tick(TICK_S);
    }
}

/* The red bar across the top, where the fault names itself. */
static unsigned heading_face(void)
{
    scr()->render(&s_c, 0);
    unsigned long sum = 0;
    for (int y = 10; y < 50; ++y) {
        for (int x = ACK_X; x < ACK_X + 420; ++x) {
            sum += (unsigned long)s_px[y * SCREEN_W + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

static int ack_cx(void) { return ACK_X + 300; }
static int ack_cy(void) { return ACK_Y + ACK_H / 2; }

TEST_CASE(a_tap_acknowledges_nothing)
{
    /* The whole point of the hold: a touch that could have been a sleeve
     * must not clear a screen that says the bench cannot be trusted. */
    fresh();
    down(ack_cx(), ack_cy());
    scr()->tick(TICK_S);
    up(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(half_the_hold_acknowledges_nothing)
{
    fresh();
    down(ack_cx(), ack_cy());
    hold((int)(UI_HOLD_S / 2.0f / TICK_S));
    CHECK(!busfault_screen_take_ack());
    up(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(two_seconds_of_contact_acknowledges)
{
    fresh();
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
}

TEST_CASE(the_acknowledgement_is_handed_over_once)
{
    /* A latch that stayed set would send the operator back to the menu from
     * wherever they had got to. */
    fresh();
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
    CHECK(!busfault_screen_take_ack());

    /* And holding on past the fade does not produce a second one. */
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(a_press_that_slides_off_the_button_abandons_the_hold)
{
    fresh();
    down(ack_cx(), ack_cy());
    hold((int)(UI_HOLD_S / 2.0f / TICK_S));
    move(ack_cx(), ACK_Y - 60);          /* off the button, still down */
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(a_press_outside_the_button_is_not_a_hold)
{
    fresh();
    down(400, 200);                      /* on the text, not the control */
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(a_second_finger_does_not_end_the_first_ones_hold)
{
    /*
     * The GT911 reports up to five contacts. A palm landing on the panel
     * mid-hold sends its own UP, and the release is matched by track id for
     * exactly that reason.
     */
    fresh();
    down(ack_cx(), ack_cy());
    hold((int)(UI_HOLD_S / 2.0f / TICK_S));

    touch_event_t other = { .type = TOUCH_EVENT_UP,
                            .point = { .id = 4, .x = (int16_t)ack_cx(),
                                       .y = (int16_t)ack_cy(),
                                       .strength = 0 } };
    scr()->event(&other);

    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
}

TEST_CASE(a_new_report_restarts_the_gesture)
{
    /* Setting a report is the screen being told about a fault. A hold left
     * part-finished from a previous one must not shorten this one. */
    fresh();
    down(ack_cx(), ack_cy());
    hold((int)(UI_HOLD_S / 2.0f / TICK_S));

    const busfault_report_t r = { .verdict = CAN_SELFTEST_SILENT,
                                  .sent = 1310, .lost = 1310 };
    busfault_screen_set(&r);
    hold((int)(UI_HOLD_S / 2.0f / TICK_S) + 2);
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(the_report_is_kept_and_handed_back)
{
    fresh();
    const busfault_report_t *r = busfault_screen_report();
    CHECK(r != NULL);
    CHECK_EQ(r->verdict, CAN_SELFTEST_CORRUPT);
    CHECK_EQ(r->corrupt, 71u);

    busfault_screen_set(NULL);
    CHECK(busfault_screen_report() == NULL);
}

TEST_CASE(every_verdict_renders)
{
    /*
     * Each verdict draws a different list, and a list drawn from a table
     * indexed by verdict is where an out-of-range read would live. Rendering
     * every one of them, the running verdict included, is what the sanitiser
     * build is for.
     */
    static const can_selftest_verdict_t k[] = {
        CAN_SELFTEST_RUNNING, CAN_SELFTEST_OK, CAN_SELFTEST_SILENT,
        CAN_SELFTEST_CORRUPT, CAN_SELFTEST_LOSSY, CAN_SELFTEST_DROPPED,
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        busfault_report_t r = { .verdict = k[i], .sent = 1000 };
        r.have_remote = (i % 2) == 0;
        r.bus_off = (i % 3) == 0;
        r.remote_overflows = (uint16_t)i;
        busfault_screen_set(&r);
        scr()->render(&s_c, 0);
        scr()->render(&s_c, 1);
        scr()->render(&s_c, 0);   /* and again, off the cached chrome */
    }
}

TEST_CASE(a_lost_link_renders_every_controller_state)
{
    /*
     * The link-lost screen picks its heading, its explanation and its list
     * from the controller's state, so each of the five has to draw. A state
     * the switch does not cover would fall through to whatever the default
     * arm says, and on this screen that is the text an operator photographs
     * and sends.
     */
    static const busfault_bus_t k[] = {
        BUSFAULT_BUS_UNKNOWN, BUSFAULT_BUS_RUNNING, BUSFAULT_BUS_RECOVERING,
        BUSFAULT_BUS_STOPPED, BUSFAULT_BUS_OFF,
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        busfault_report_t r = { .kind = BUSFAULT_LINK_LOST, .bus = k[i] };
        r.down_s     = (uint32_t)(i * 7u);
        r.polls      = 5323;
        r.timeouts   = (uint32_t)i;
        r.recoveries = (uint32_t)(i * 2u);
        r.tx_errors  = (uint32_t)(i * 64u);
        busfault_screen_set(&r);
        scr()->render(&s_c, 0);
        scr()->render(&s_c, 1);
    }
}

TEST_CASE(a_lost_link_is_acknowledged_the_same_way)
{
    /* One gesture, whichever fault brought the screen up: an operator does
     * not learn two ways out of the same screen. */
    fresh();
    const busfault_report_t r = { .kind = BUSFAULT_LINK_LOST,
                                  .bus = BUSFAULT_BUS_OFF, .down_s = 12 };
    busfault_screen_set(&r);

    down(ack_cx(), ack_cy());
    hold((int)(UI_HOLD_S / 2.0f / TICK_S));
    CHECK(!busfault_screen_take_ack());
    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
    CHECK(!busfault_screen_take_ack());
}

TEST_CASE(the_two_faults_do_not_share_a_heading)
{
    /*
     * A start-up test that failed and a link that stopped are different
     * things to be told, and the screen is the only place either is said.
     * Compared as pixels, because what is under test is what reaches the
     * panel rather than which branch ran.
     */
    fresh();
    busfault_report_t a = { .kind = BUSFAULT_SELFTEST,
                            .verdict = CAN_SELFTEST_SILENT, .sent = 1000 };
    busfault_screen_set(&a);
    const unsigned selftest = heading_face();

    busfault_report_t b = { .kind = BUSFAULT_LINK_LOST,
                            .bus = BUSFAULT_BUS_OFF, .down_s = 12 };
    busfault_screen_set(&b);
    CHECK(heading_face() != selftest);
}

TEST_CASE(the_button_fills_towards_the_colour_it_settles_on)
{
    /*
     * The fade is the feedback: without it a two-second hold is a button
     * that does nothing for two seconds. Held longer, it is further along.
     */
    const gfx_color_t base   = ui_theme_color(UI_C_DANGER);
    const gfx_color_t target = ui_theme_color(UI_C_OK);

    CHECK_EQ(ui_hold_fill(base, target, 0.0f), base);
    CHECK_EQ(ui_hold_fill(base, target, UI_HOLD_S), target);
    CHECK_EQ(ui_hold_fill(base, target, UI_HOLD_S * 2.0f), target);

    const gfx_color_t quarter = ui_hold_fill(base, target, UI_HOLD_S * 0.25f);
    const gfx_color_t half    = ui_hold_fill(base, target, UI_HOLD_S * 0.5f);
    CHECK(quarter != base);
    CHECK(half != quarter);
    CHECK(half != target);
}

int main(void)
{
    RUN(a_tap_acknowledges_nothing);
    RUN(half_the_hold_acknowledges_nothing);
    RUN(two_seconds_of_contact_acknowledges);
    RUN(the_acknowledgement_is_handed_over_once);
    RUN(a_press_that_slides_off_the_button_abandons_the_hold);
    RUN(a_press_outside_the_button_is_not_a_hold);
    RUN(a_second_finger_does_not_end_the_first_ones_hold);
    RUN(a_new_report_restarts_the_gesture);
    RUN(the_report_is_kept_and_handed_back);
    RUN(every_verdict_renders);
    RUN(a_lost_link_renders_every_controller_state);
    RUN(a_lost_link_is_acknowledged_the_same_way);
    RUN(the_two_faults_do_not_share_a_heading);
    RUN(the_button_fills_towards_the_colour_it_settles_on);
    return test_summary("busfault_screen");
}
