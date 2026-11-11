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
#include "guard_canvas.h"

#include "busfault_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_W UI_PANEL_W
#define SCREEN_H UI_PANEL_H

/* The button across the bottom. */
#include "busfault_spots.h"

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
        for (int x = BF_ACK_X; x < BF_ACK_X + 420; ++x) {
            sum += (unsigned long)s_px[y * SCREEN_W + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

static int ack_cx(void) { return BF_ACK_X + 300; }
static int ack_cy(void) { return BF_ACK_Y + BF_ACK_H / 2; }

/*
 * This screen keeps its own hold timer rather than a ui_hold_t, so it takes
 * the per-frame credit cap through ui_hold_credit().  Without it a single
 * late frame -- one that dispatched the press it then credits -- would
 * acknowledge a fault from a touch milliseconds old, which is the gesture
 * this screen exists to refuse.
 */
TEST_CASE(one_late_frame_cannot_acknowledge_a_fault)
{
    fresh();
    down(ack_cx(), ack_cy());

    /* A frame longer than the hold itself credits only the cap. */
    scr()->tick(UI_HOLD_S * 1.5f);
    CHECK(!busfault_screen_take_ack());

    /* And it takes the whole eight of them to get there. */
    int frames = 1;
    for (; frames < 64; ++frames) {
        if (busfault_screen_take_ack()) {
            break;
        }
        scr()->tick(UI_HOLD_S * 1.5f);
    }
    CHECK_EQ(frames, (int)(UI_HOLD_S / UI_HOLD_MAX_CREDIT_S));
}

/*
 * The same for the acknowledgement: a screen that says the bench cannot be
 * trusted must not be dismissed by a gesture whose events were lost. The
 * hold completes on the frame timer, so without the cancel it would
 * acknowledge on a contact the screen can no longer account for.
 */
TEST_CASE(a_cancelled_gesture_acknowledges_nothing)
{
    fresh();
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS / 4);

    scr()->cancel();
    hold(HOLD_TICKS * 2);
    CHECK(!busfault_screen_take_ack());

    up(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());
}

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
    move(ack_cx(), BF_ACK_Y - 60);          /* off the button, still down */
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

TEST_CASE(a_press_left_behind_by_leaving_acknowledges_nothing_later)
{
    /* The start-up fault is acknowledged and the screen closes under the
     * finger, so the release goes to the next screen.  The next fault --
     * a lost link -- waits for a new hold. */
    fresh();
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
    /* What ui_router_goto() does on the way out. */
    if (scr()->leave != NULL) {
        scr()->leave();
    }

    /* An IOmcu that answers its status: the silent echo run keeps the
     * hold (busfault_no_iomcu()). */
    const busfault_report_t r = { .verdict = CAN_SELFTEST_SILENT,
                                  .sent = 24, .lost = 23,
                                  .have_remote = true };
    busfault_screen_set(&r);
    hold(HOLD_TICKS);
    CHECK(!busfault_screen_take_ack());

    /* And a new hold still acknowledges it. */
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK(busfault_screen_take_ack());
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

/* The canvas of the two cases below, inside its guard rows and columns. */
static gfx_color_t    s_gpx[GUARD_CANVAS_PIXELS(SCREEN_W, SCREEN_H)];
static guard_canvas_t s_g;
static gfx_canvas_t   s_gc;

/* A frame that painted the whole canvas and nothing beside it, and its sum. */
static uint32_t whole_frame(void)
{
    CHECK_EQ(guard_canvas_touched(&s_g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&s_g), 0u);
    return guard_canvas_sum(&s_g);
}

TEST_CASE(every_verdict_renders)
{
    /*
     * Each verdict draws a different list, and a list drawn from a table
     * indexed by verdict is where an out-of-range read would live.  Every
     * one of them, the running verdict included, paints the whole canvas
     * and no pixel beside it, draws the same picture again off the cached
     * chrome, and draws a picture no other verdict draws.
     */
    static const can_selftest_verdict_t k[] = {
        CAN_SELFTEST_RUNNING, CAN_SELFTEST_OK, CAN_SELFTEST_SILENT,
        CAN_SELFTEST_CORRUPT, CAN_SELFTEST_LOSSY, CAN_SELFTEST_DROPPED,
    };
    uint32_t sum[sizeof(k) / sizeof(k[0])];
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        guard_canvas_init(&s_g, &s_gc, s_gpx, SCREEN_W, SCREEN_H);
        busfault_report_t r = { .verdict = k[i], .sent = 1000 };
        r.have_remote = (i % 2) == 0;
        r.bus_off = (i % 3) == 0;
        r.remote_overflows = (uint16_t)i;
        busfault_screen_set(&r);
        scr()->render(&s_gc, 0);
        sum[i] = whole_frame();
        scr()->render(&s_gc, 1);
        (void)whole_frame();
        scr()->render(&s_gc, 0);  /* and again, off the cached chrome */
        CHECK_EQ(whole_frame(), sum[i]);
        for (size_t j = 0; j < i; ++j) {
            CHECK(sum[i] != sum[j]);
        }
    }
}

TEST_CASE(a_lost_link_renders_every_controller_state)
{
    /*
     * The link-lost screen picks its heading, its explanation and its list
     * from the controller's state, so each of the five has to draw. A state
     * the switch does not cover would fall through to whatever the default
     * arm says, and on this screen that is the text an operator photographs
     * and sends.  Each state paints the whole canvas, no pixel beside it,
     * and a picture of its own.
     */
    static const busfault_bus_t k[] = {
        BUSFAULT_BUS_UNKNOWN, BUSFAULT_BUS_RUNNING, BUSFAULT_BUS_RECOVERING,
        BUSFAULT_BUS_STOPPED, BUSFAULT_BUS_OFF,
    };
    uint32_t sum[sizeof(k) / sizeof(k[0])];
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh();
        guard_canvas_init(&s_g, &s_gc, s_gpx, SCREEN_W, SCREEN_H);
        busfault_report_t r = { .kind = BUSFAULT_LINK_LOST, .bus = k[i] };
        r.down_s     = (uint32_t)(i * 7u);
        r.polls      = 5323;
        r.timeouts   = (uint32_t)i;
        r.recoveries = (uint32_t)(i * 2u);
        r.tx_errors  = (uint32_t)(i * 64u);
        busfault_screen_set(&r);
        scr()->render(&s_gc, 0);
        sum[i] = whole_frame();
        scr()->render(&s_gc, 1);
        (void)whole_frame();
        for (size_t j = 0; j < i; ++j) {
            CHECK(sum[i] != sum[j]);
        }
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

/* The acknowledge button's fill at its left end, clear of the label. */
static gfx_color_t ack_face(void)
{
    scr()->render(&s_c, 0);
    return s_px[ack_cy() * SCREEN_W + BF_ACK_X + 20];
}

/*
 * The hold that acknowledges ends in the 300ms flash ARM gives, by time:
 * white, black and the OK green for 50ms each, twice, then the OK green.
 */
TEST_CASE(the_acknowledgement_flashes_for_300_ms)
{
    fresh();
    down(ack_cx(), ack_cy());
    /* Up to the tick that acknowledges, and no further. */
    bool acked = false;
    for (int i = 0; i < HOLD_TICKS && !acked; ++i) {
        scr()->tick(TICK_S);
        acked = busfault_screen_take_ack();
    }
    CHECK(acked);
    const gfx_color_t ok = ui_theme_color(UI_C_OK);
    /* 'W' white, 'B' black, 'G' the OK green. */
    static const struct { int ms; char want; } k_steps[] = {
        { 0, 'W' }, { 50, 'B' }, { 100, 'G' },
        { 150, 'W' }, { 200, 'B' }, { 250, 'G' }, { 300, 'G' },
    };
    int at = 0;
    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); ++i) {
        for (; at < k_steps[i].ms; at += 25) {
            scr()->tick(0.025f);
        }
        const gfx_color_t want = (k_steps[i].want == 'W') ? GFX_WHITE
                               : (k_steps[i].want == 'B') ? GFX_BLACK : ok;
        CHECK_EQ(ack_face(), want);
    }
}

/** @brief A start-up test on which nothing answered: no echo and no status
 *         answer either. */
static const busfault_report_t k_noio = {
    .kind = BUSFAULT_SELFTEST, .verdict = CAN_SELFTEST_SILENT,
    .sent = 1310, .lost = 1310, .tx_errors = 128, .bus_off = true,
};

/*
 * A bus on which no IOmcu answered is the panel on its own: a tap on the
 * button leaves for the simulation, on the press's own release over the
 * button.  The press alone leaves nothing, a release off the button
 * nothing, and a press held past the hold time still leaves on its release
 * only.
 */
TEST_CASE(a_tap_leaves_a_bus_on_which_no_iomcu_answered)
{
    fresh();
    busfault_screen_set(&k_noio);
    down(ack_cx(), ack_cy());
    scr()->tick(TICK_S);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    up(ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);

    fresh();
    busfault_screen_set(&k_noio);
    down(ack_cx(), ack_cy());
    move(ack_cx(), BF_ACK_Y - 40);
    up(ack_cx(), BF_ACK_Y - 40);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);

    fresh();
    busfault_screen_set(&k_noio);
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    up(ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
}

/*
 * Only the silent start-up test with no answer to either status request
 * takes the tap.  A silent echo run from an IOmcu that answered its status
 * before the run or after it, frames altered or lost, and a link that
 * stopped are faults, and keep the 2s hold.
 */
TEST_CASE(every_other_report_keeps_the_hold)
{
    CHECK(busfault_no_iomcu(&k_noio));
    CHECK(!busfault_no_iomcu(NULL));
    busfault_report_t k[6];
    for (size_t i = 0; i < 6u; ++i) {
        k[i] = k_noio;
    }
    k[0].have_remote = true;
    k[1].verdict = CAN_SELFTEST_CORRUPT;
    k[2].verdict = CAN_SELFTEST_LOSSY;
    k[3].verdict = CAN_SELFTEST_DROPPED;
    k[4].kind = BUSFAULT_LINK_LOST;
    /* The IOmcu answered the status request before the echo run and was
     * silent after it: it went quiet, which is a fault. */
    k[5].remote_seen = true;
    for (size_t i = 0; i < 6u; ++i) {
        CHECK(!busfault_no_iomcu(&k[i]));
        fresh();
        busfault_screen_set(&k[i]);
        down(ack_cx(), ack_cy());
        up(ack_cx(), ack_cy());
        CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
        down(ack_cx(), ack_cy());
        hold(HOLD_TICKS);
        CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
    }
}

/*
 * The IOmcu answered the status request before the echo run, then nothing
 * came back and the status request after it went unanswered: an IOmcu that
 * went quiet.  The screen names the silent bus as a fault, red, and keeps
 * the 2s hold.
 */
TEST_CASE(an_iomcu_that_answered_before_the_run_keeps_the_hold)
{
    busfault_report_t quiet = k_noio;
    quiet.remote_seen = true;
    CHECK(!busfault_no_iomcu(&quiet));
    CHECK_STR_EQ(busfault_headline(&quiet), TR(CAN_SILENT));
    fresh();
    busfault_screen_set(&quiet);
    (void)heading_face();
    CHECK_EQ(s_px[4 * SCREEN_W + SCREEN_W - 4], ui_theme_color(UI_C_DANGER));
    down(ack_cx(), ack_cy());
    up(ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_SIMULATION);
}

/* RECONNECT, once the link answers, is the hold on every report. */
TEST_CASE(reconnect_stays_a_hold_where_no_iomcu_answered)
{
    fresh();
    busfault_screen_set(&k_noio);
    busfault_screen_link(true, 0u);
    busfault_screen_link(true, BUSFAULT_BACK_MS);
    CHECK(busfault_screen_link_back());
    down(ack_cx(), ack_cy());
    up(ack_cx(), ack_cy());
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_NONE);
    down(ack_cx(), ack_cy());
    hold(HOLD_TICKS);
    CHECK_EQ(busfault_screen_take_exit(), BUSFAULT_EXIT_RECONNECT);
}

/*
 * The screen with no IOmcu says so: its own heading, which the splash takes
 * too, and an amber bar where a fault has its red one.
 */
TEST_CASE(no_iomcu_has_its_own_heading_and_an_amber_bar)
{
    busfault_report_t answered = k_noio;
    answered.have_remote = true;
    CHECK_STR_EQ(busfault_headline(&k_noio), TR(BF_NOIO_TEXT));
    CHECK_STR_EQ(busfault_headline(&answered), TR(CAN_SILENT));
    CHECK_STR_EQ(busfault_headline(NULL), TR(CAN_RUNNING));

    fresh();
    busfault_screen_set(&k_noio);
    const unsigned noio = heading_face();
    CHECK_EQ(s_px[4 * SCREEN_W + SCREEN_W - 4], ui_theme_color(UI_C_WARN));
    busfault_screen_set(&answered);
    CHECK(heading_face() != noio);
    CHECK_EQ(s_px[4 * SCREEN_W + SCREEN_W - 4], ui_theme_color(UI_C_DANGER));
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
    RUN(a_press_left_behind_by_leaving_acknowledges_nothing_later);
    RUN(the_report_is_kept_and_handed_back);
    RUN(every_verdict_renders);
    RUN(a_lost_link_renders_every_controller_state);
    RUN(a_lost_link_is_acknowledged_the_same_way);
    RUN(the_two_faults_do_not_share_a_heading);
    RUN(the_button_fills_towards_the_colour_it_settles_on);
    RUN(the_acknowledgement_flashes_for_300_ms);
    RUN(one_late_frame_cannot_acknowledge_a_fault);
    RUN(a_cancelled_gesture_acknowledges_nothing);
    RUN(a_tap_leaves_a_bus_on_which_no_iomcu_answered);
    RUN(every_other_report_keeps_the_hold);
    RUN(an_iomcu_that_answered_before_the_run_keeps_the_hold);
    RUN(reconnect_stays_a_hold_where_no_iomcu_answered);
    RUN(no_iomcu_has_its_own_heading_and_an_amber_bar);
    return test_summary("busfault_screen");
}
