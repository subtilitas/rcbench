/*
 * The DShot frame schedule of shared/outputs/out_render.c, over a fake of
 * the out_render_hw_*() calls that stamps every frame with the clock.
 *
 * Under test: the due times are a grid OUT_RENDER_DSHOT_PERIOD_US (1000us)
 * apart.  Passes that land on the due times send frames exactly 1000us
 * apart, and passes at any other step send each frame on the first pass at
 * or after its due time, 1kHz on average.  A pass late by up to
 * OUT_RENDER_DSHOT_MIN_US (500us) moves no later frame; one later than that
 * puts the next due time 500us after its frame.  A stall of several periods
 * sends one frame, not one for every missed due time, and the grid runs on
 * 500us after it.  A send that returns late (an interrupt between the due
 * test and the FIFO write) moves no due time.  All of it holds across the
 * wrap of the 32-bit microsecond clock, and on a bidirectional slot, whose
 * wake pass takes one due time.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "dshot.h"
#include "link_pages.h"
#include "out_render.h"
#include "outputs.h"

/* ------------------------------------------------------------ the fake */

/** @brief The most frames one case records. */
#define SENT_MAX 4096u

static uint32_t g_us, g_ms;
/* The clock of every frame sent, in order. */
static uint32_t g_sent[SENT_MAX];
static unsigned g_n;
/* How far the clock moves inside a send: an interrupt taken between the
 * due test and the frame reaching the FIFO. */
static uint32_t g_send_takes_us;
/* Bidirectional pins at their low rest, bit n for GPn. */
static uint64_t g_low;

bool out_render_hw_bind(const out_slot_t *s)
{
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        g_low |= 1ull << s->pin;
    }
    return true;
}

void out_render_hw_release(const out_slot_t *s) { (void)s; }

bool out_render_hw_park(uint8_t pin)
{
    (void)pin;
    return true;
}

void out_render_hw_unpark(uint8_t pin) { (void)pin; }

void out_render_hw_pwm(uint8_t pin, uint16_t pulse_us)
{
    (void)pin;
    (void)pulse_us;
}

bool out_render_hw_pwm_stamped(uint8_t pin, uint16_t pulse_us,
                               uint64_t *at_us)
{
    (void)pin;
    (void)pulse_us;
    *at_us = g_us;
    return true;
}

void out_render_hw_ppm(uint8_t pin, const uint16_t *us, uint8_t n)
{
    (void)pin;
    (void)us;
    (void)n;
}

void out_render_hw_ppm_stop(uint8_t pin) { (void)pin; }

bool out_render_hw_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *t)
{
    (void)pin;
    (void)edt;
    (void)t;
    return false;
}

void out_render_hw_dshot_stop(uint8_t pin)
{
    (void)pin;
}

bool out_render_hw_dshot_wake(uint8_t pin)
{
    const bool low = ((g_low >> pin) & 1u) != 0u;
    g_low &= ~(1ull << pin);
    return low;
}

void out_render_hw_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    (void)pin;
    (void)value;
    (void)telemetry;
    if (g_n < SENT_MAX) {
        g_sent[g_n++] = g_us;
    }
    g_us += g_send_takes_us;
}

uint32_t out_render_hw_us(void) { return g_us; }

uint32_t out_render_hw_ms(void) { return g_ms; }

/* ---------------------------------------------------------- the helpers */

static out_render_t g_r;
static outputs_t    g_o;
static uint16_t     g_rate[OUT_MAX_SLOTS];

/*
 * Slot 0 as @p driver at 300kbit/s on GP20, bound and armed with the clock
 * at @p t0: the disarmed pass at @p t0 makes it due at @p t0, and nothing
 * is sent yet.
 */
static void armed_at(out_driver_t driver, uint32_t t0)
{
    g_us = t0;
    g_ms = 1000u;
    g_n = 0u;
    g_send_takes_us = 0u;
    g_low = 0u;
    out_render_init(&g_r);
    outputs_init(&g_o, g_ms);
    memset(g_rate, 0, sizeof(g_rate));
    const out_slot_t s = { driver, 0u, 1u, 20u, 300u };
    CHECK(outputs_configure(&g_o, 0u, &s));
    g_rate[0] = 300u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    out_render_service(&g_r, &g_o);          /* disarmed: due from now */
    outputs_arm(&g_o, true, g_ms);
    CHECK_EQ(g_n, 0u);
}

/* One pass with the clock at @p t. */
static void pass_at(uint32_t t)
{
    g_us = t;
    out_render_service(&g_r, &g_o);
}

/* Passes every @p step us from the clock's current value, @p n of them. */
static void passes(uint32_t step, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        pass_at(g_us + step);
    }
}

/* The time from frame @p k - 1 to frame @p k, wrap-safe. */
static uint32_t gap(unsigned k)
{
    return g_sent[k] - g_sent[k - 1u];
}

/* ----------------------------------------------------------- the cases */

/*
 * Passes on the due times: every frame is 1000us after the one before.
 * Passes every 100us land on them too.  Passes every 37us, which never
 * land on one, send frame k less than 37us after t0 + k x 1000us, for
 * 1,000 frames: none is lost or gained.
 */
TEST_CASE(on_time_passes_send_exactly_one_period_apart)
{
    armed_at(OUT_DRIVER_DSHOT, 1000000u);
    pass_at(1000000u);
    for (unsigned k = 1; k <= 20u; ++k) {
        pass_at(1000000u + k * OUT_RENDER_DSHOT_PERIOD_US);
    }
    CHECK_EQ(g_n, 21u);
    for (unsigned k = 1; k < g_n; ++k) {
        CHECK_EQ(gap(k), OUT_RENDER_DSHOT_PERIOD_US);
    }

    armed_at(OUT_DRIVER_DSHOT, 1000000u);
    pass_at(1000000u);
    passes(100u, 200u);
    CHECK_EQ(g_n, 21u);
    for (unsigned k = 1; k < g_n; ++k) {
        CHECK_EQ(gap(k), OUT_RENDER_DSHOT_PERIOD_US);
    }

    armed_at(OUT_DRIVER_DSHOT, 1000000u);
    pass_at(1000000u);
    while (g_n < 1000u) {
        passes(37u, 1u);
    }
    for (unsigned k = 0; k < g_n; ++k) {
        const uint32_t late = g_sent[k] - (1000000u + k * 1000u);
        CHECK(late < 37u);
    }
}

/*
 * A pass 300us late sends its frame then; the next frame keeps its due
 * time, 700us after it, and the one after is a period on.  500us late is
 * the most the grid takes: the next frame is 500us on.  501us late puts the
 * next due time 500us after the late frame, and the grid runs on from
 * there.
 */
TEST_CASE(a_late_pass_moves_no_later_frame)
{
    const uint32_t t0 = 2000000u;
    armed_at(OUT_DRIVER_DSHOT, t0);
    pass_at(t0);
    pass_at(t0 + 1300u);                     /* due at t0 + 1000 */
    pass_at(t0 + 1999u);
    CHECK_EQ(g_n, 2u);
    pass_at(t0 + 2000u);
    pass_at(t0 + 3000u);
    CHECK_EQ(g_n, 4u);
    CHECK_EQ(gap(2), 700u);
    CHECK_EQ(gap(3), 1000u);

    pass_at(t0 + 4500u);                     /* due at t0 + 4000 */
    pass_at(t0 + 5000u);
    CHECK_EQ(g_n, 6u);
    CHECK_EQ(gap(5), OUT_RENDER_DSHOT_MIN_US);

    pass_at(t0 + 6501u);                     /* due at t0 + 6000 */
    pass_at(t0 + 7000u);
    CHECK_EQ(g_n, 7u);
    pass_at(t0 + 7001u);
    CHECK_EQ(g_n, 8u);
    CHECK_EQ(gap(7), OUT_RENDER_DSHOT_MIN_US);
    pass_at(t0 + 8000u);
    CHECK_EQ(g_n, 8u);
    pass_at(t0 + 8001u);
    CHECK_EQ(g_n, 9u);
}

/*
 * No pass for 5.25 periods: the pass after it sends one frame, not the five
 * it missed.  Passes every 10us after it send nothing for 500us, then one
 * frame, and from there one a period.
 */
TEST_CASE(a_stall_sends_one_frame_and_the_grid_runs_on)
{
    const uint32_t t0 = 3000000u;
    armed_at(OUT_DRIVER_DSHOT, t0);
    pass_at(t0);
    pass_at(t0 + 5250u);
    CHECK_EQ(g_n, 2u);
    passes(10u, 49u);                        /* to t0 + 5740 */
    CHECK_EQ(g_n, 2u);
    passes(10u, 1u);                         /* t0 + 5750 */
    CHECK_EQ(g_n, 3u);
    CHECK_EQ(gap(2), OUT_RENDER_DSHOT_MIN_US);
    passes(10u, 300u);
    CHECK_EQ(g_n, 6u);
    for (unsigned k = 3; k < g_n; ++k) {
        CHECK_EQ(gap(k), OUT_RENDER_DSHOT_PERIOD_US);
    }
}

/*
 * A send that returns 80us after its due test, the time an interrupt takes
 * between the clock read and the FIFO write: the next frame's due time is
 * the grid's, not the send's return plus a period.  On a pin the frame
 * after such a one goes out 80us sooner after it, which is a gap under
 * 1000us that no pass was early for.
 */
TEST_CASE(a_slow_send_moves_no_due_time)
{
    const uint32_t t0 = 4000000u;
    armed_at(OUT_DRIVER_DSHOT, t0);
    g_send_takes_us = 80u;
    pass_at(t0);
    CHECK_EQ(g_us, t0 + 80u);
    pass_at(t0 + 999u);
    CHECK_EQ(g_n, 1u);
    pass_at(t0 + 1000u);
    CHECK_EQ(g_n, 2u);
    CHECK_EQ(gap(1), OUT_RENDER_DSHOT_PERIOD_US);
}

/*
 * Across the wrap of the 32-bit microsecond clock: due times every 1000us
 * through 0xFFFFFFFF to 0, passes every 50us, late passes and a stall all
 * keep the same spacing as anywhere else.
 */
TEST_CASE(the_schedule_holds_across_the_clock_wrap)
{
    const uint32_t t0 = 0xFFFFFFFFu - 5500u;  /* wraps inside frame 6 */
    armed_at(OUT_DRIVER_DSHOT, t0);
    pass_at(t0);
    passes(50u, 220u);                       /* 11ms, through the wrap */
    CHECK_EQ(g_n, 12u);
    for (unsigned k = 1; k < g_n; ++k) {
        CHECK_EQ(gap(k), OUT_RENDER_DSHOT_PERIOD_US);
    }
    CHECK(g_sent[g_n - 1u] < 0x10000u);      /* the clock did wrap */

    /* Late across the wrap: due 100us before it, sent 300us late, after
     * it; the next frame keeps its due time. */
    const uint32_t t1 = 0xFFFFFFFFu - 1099u;
    armed_at(OUT_DRIVER_DSHOT, t1);
    pass_at(t1);
    pass_at(t1 + 1300u);
    pass_at(t1 + 1999u);
    CHECK_EQ(g_n, 2u);
    pass_at(t1 + 2000u);
    CHECK_EQ(g_n, 3u);
    CHECK_EQ(gap(2), 700u);

    /* A stall across the wrap: one frame, then 500us. */
    const uint32_t t2 = 0xFFFFFFFFu - 2000u;
    armed_at(OUT_DRIVER_DSHOT, t2);
    pass_at(t2);
    pass_at(t2 + 4100u);
    pass_at(t2 + 4599u);
    CHECK_EQ(g_n, 2u);
    pass_at(t2 + 4600u);
    CHECK_EQ(g_n, 3u);
    CHECK_EQ(gap(2), OUT_RENDER_DSHOT_MIN_US);
}

/*
 * A bidirectional slot: the wake pass takes the first due time and sends
 * nothing, and the frames after it keep the grid as a plain slot's do,
 * late passes included.
 */
TEST_CASE(a_bidirectional_slot_keeps_the_same_grid)
{
    const uint32_t t0 = 5000000u;
    armed_at(OUT_DRIVER_DSHOT_BIDIR, t0);
    pass_at(t0);                             /* the wake */
    CHECK_EQ(g_n, 0u);
    pass_at(t0 + 999u);
    CHECK_EQ(g_n, 0u);
    pass_at(t0 + 1000u);
    pass_at(t0 + 2200u);
    pass_at(t0 + 3000u);
    CHECK_EQ(g_n, 3u);
    CHECK_EQ(g_sent[0], t0 + 1000u);
    CHECK_EQ(gap(1), 1200u);
    CHECK_EQ(gap(2), 800u);
}

int main(void)
{
    RUN(on_time_passes_send_exactly_one_period_apart);
    RUN(a_late_pass_moves_no_later_frame);
    RUN(a_stall_sends_one_frame_and_the_grid_runs_on);
    RUN(a_slow_send_moves_no_due_time);
    RUN(the_schedule_holds_across_the_clock_wrap);
    RUN(a_bidirectional_slot_keeps_the_same_grid);
    return test_summary("dshot_cadence");
}
