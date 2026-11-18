/*
 * The IOmcu's watchdog on its own main loop (loop_watch.h): when the
 * firmware enables the RP2350's watchdog, where it feeds it, and what a
 * restart by it reports.
 *
 * The cases: the first pass enables and every later one feeds; an image on
 * trial leaves the watchdog to the boot ROM until its BUY; the bench
 * console build never enables it; the mark is handed back before the
 * update's restart only when it was written; the fault bit stands from a
 * start after a watchdog restart until a CLEAR is taken, through the real
 * CONTROL page rules; the feed before a flash window; and the period
 * against the longest time between two feeds, with the flash part's
 * datasheet maxima.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <string.h>

#include "greatest.h"

#include "heartbeat.h"
#include "link_dev.h"
#include "link_msg.h"
#include "link_pages.h"
#include "loop_watch.h"
#include "reset_cause.h"
#include "safety_gate.h"

static loop_watch_t w;

/* ------------------------------------------------- enable and feed point */

TEST_CASE(the_first_pass_enables_and_every_later_pass_feeds)
{
    loop_watch_init(&w, false, RESET_CAUSE_POR);
    CHECK(!loop_watch_handover(&w));
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_ENABLE);
    CHECK(loop_watch_handover(&w));
    for (int i = 0; i < 1000; ++i) {
        CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_FEED);
    }
}

/* The boot ROM owns the watchdog for 16.7s after a flash update boot:
 * enabling it would reload the ROM's count, so nothing is done until the
 * BUY, and the pass after the BUY enables. */
TEST_CASE(an_image_on_trial_leaves_the_watchdog_alone_until_its_buy)
{
    loop_watch_init(&w, false, RESET_CAUSE_FLASH_UPDATE);
    for (int i = 0; i < 1000; ++i) {
        CHECK_EQ(loop_watch_pass(&w, true), LOOP_WATCH_NONE);
    }
    CHECK(!loop_watch_handover(&w));
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_ENABLE);
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_FEED);
    /* Enabled, it is fed whatever the trial flag says: nothing disables
     * it, and a pass without a feed is a restart. */
    CHECK_EQ(loop_watch_pass(&w, true), LOOP_WATCH_FEED);
    CHECK(loop_watch_handover(&w));
}

TEST_CASE(the_console_build_never_enables_the_watchdog)
{
    loop_watch_init(&w, true, RESET_CAUSE_POR);
    for (int i = 0; i < 10; ++i) {
        CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_NONE);
        CHECK_EQ(loop_watch_pass(&w, true), LOOP_WATCH_NONE);
    }
    CHECK(!loop_watch_handover(&w));
}

TEST_CASE(a_missing_watch_does_nothing)
{
    loop_watch_init(NULL, false, RESET_CAUSE_LOOP_WATCHDOG);
    CHECK_EQ(loop_watch_pass(NULL, false), LOOP_WATCH_NONE);
    CHECK(!loop_watch_handover(NULL));
    CHECK_EQ(loop_watch_fault(NULL), 0u);
    loop_watch_control(NULL, (uint8_t)LINK_CT_CLEAR, 1u, 0u);
}

/* ------------------------------------------------------- the fault bit */

/* Only a restart by the loop's watchdog sets the bit: the boot ROM's trial
 * timer, a restart asked for and a power-up do not. */
TEST_CASE(only_a_loop_watchdog_restart_sets_the_fault_bit)
{
    for (int c = 0; c < (int)RESET_CAUSE_COUNT; ++c) {
        loop_watch_init(&w, false, (reset_cause_t)c);
        CHECK_EQ(loop_watch_fault(&w),
                 c == (int)RESET_CAUSE_LOOP_WATCHDOG
                     ? (uint16_t)LINK_FAULT_WATCHDOG : 0u);
    }
    CHECK_EQ(LINK_FAULT_WATCHDOG, 0x80u);
    /* The bit stands through every pass: passing the loop is not a CLEAR. */
    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);
    (void)loop_watch_pass(&w, false);
    (void)loop_watch_pass(&w, false);
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);
}

/* The window's edges against LINK_CT_CLEAR, and the answer: a window that
 * ends one register short or starts one past it is no CLEAR, and a refused
 * frame stored nothing. */
TEST_CASE(a_stored_window_over_clear_takes_the_bit_away)
{
    const uint8_t clr = (uint8_t)LINK_CT_CLEAR;
    CHECK(clr >= 1u);

    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);
    loop_watch_control(&w, 0u, clr, 0u);            /* ends before CLEAR */
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);
    loop_watch_control(&w, (uint8_t)(clr + 1u), 1u, 0u); /* starts past it */
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);
    loop_watch_control(&w, clr, 0u, 0u);            /* no register        */
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);
    loop_watch_control(&w, clr, 1u, LINK_NACK_BAD_VALUE);   /* refused    */
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);
    loop_watch_control(&w, clr, 1u, 0u);            /* CLEAR alone        */
    CHECK_EQ(loop_watch_fault(&w), 0u);

    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);
    loop_watch_control(&w, 0u, (uint8_t)(clr + 1u), 0u);  /* ends on it   */
    CHECK_EQ(loop_watch_fault(&w), 0u);

    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);
    loop_watch_control(&w, (uint8_t)(clr - 1u), 3u, 0u);  /* spans it     */
    CHECK_EQ(loop_watch_fault(&w), 0u);

    /* A window whose end wraps an 8-bit sum still reaches CLEAR. */
    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);
    loop_watch_control(&w, clr, 255u, 0u);
    CHECK_EQ(loop_watch_fault(&w), 0u);
}

/* Through the CONTROL page as the firmware's handler runs it
 * (safety_gate_control_write() and then loop_watch_control()): a CLEAR
 * without its magic and an ARM alone leave the bit, the panel's CLEAR
 * takes it. */
TEST_CASE(the_panels_clear_ends_the_bit_and_no_other_write_does)
{
    static const link_page_t none[1];
    heartbeat_mon_t beat;
    link_dev_t dev;
    uint16_t control[LINK_CT_COUNT];
    memset(control, 0, sizeof(control));
    heartbeat_mon_init(&beat);
    link_dev_init(&dev, none, 0u, NULL, 0u);
    loop_watch_init(&w, false, RESET_CAUSE_LOOP_WATCHDOG);

    /* After a start the arm latch stands: an ARM is refused. */
    uint16_t arm = (uint16_t)LINK_ARM_ALL;
    uint8_t nack = safety_gate_control_write(control, (uint8_t)LINK_CT_ARM,
                                             1u, &arm, &beat, &dev);
    loop_watch_control(&w, (uint8_t)LINK_CT_ARM, 1u, nack);
    CHECK_EQ(nack, LINK_NACK_NOT_ARMED);
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);

    /* A throttle of zero is stored and is no CLEAR. */
    uint16_t zero = 0u;
    nack = safety_gate_control_write(control, (uint8_t)LINK_CT_THROTTLE, 1u,
                                     &zero, &beat, &dev);
    loop_watch_control(&w, (uint8_t)LINK_CT_THROTTLE, 1u, nack);
    CHECK_EQ(nack, 0u);
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);

    /* CLEAR without the magic is refused. */
    uint16_t bad = (uint16_t)(LINK_CLEAR_MAGIC + 1u);
    nack = safety_gate_control_write(control, (uint8_t)LINK_CT_CLEAR, 1u,
                                     &bad, &beat, &dev);
    loop_watch_control(&w, (uint8_t)LINK_CT_CLEAR, 1u, nack);
    CHECK_EQ(nack, LINK_NACK_BAD_VALUE);
    CHECK_EQ(loop_watch_fault(&w), LINK_FAULT_WATCHDOG);

    /* The panel's CLEAR. */
    uint16_t magic = (uint16_t)LINK_CLEAR_MAGIC;
    nack = safety_gate_control_write(control, (uint8_t)LINK_CT_CLEAR, 1u,
                                     &magic, &beat, &dev);
    loop_watch_control(&w, (uint8_t)LINK_CT_CLEAR, 1u, nack);
    CHECK_EQ(nack, 0u);
    CHECK_EQ(loop_watch_fault(&w), 0u);
}

/* ------------------------------------------------- the flash windows */

/* The feed before a flash window: only a watchdog this firmware enabled is
 * fed, and a window never enables one. */
TEST_CASE(a_flash_window_feeds_an_enabled_watchdog_and_enables_none)
{
    /* Before the first pass: the boot's store reclaims. */
    loop_watch_init(&w, false, RESET_CAUSE_POR);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
    CHECK(!loop_watch_handover(&w));
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_ENABLE);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);

    /* On trial: the BUY's own window leaves the boot ROM's count alone. */
    loop_watch_init(&w, false, RESET_CAUSE_FLASH_UPDATE);
    CHECK_EQ(loop_watch_pass(&w, true), LOOP_WATCH_NONE);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_ENABLE);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);

    /* The console build. */
    loop_watch_init(&w, true, RESET_CAUSE_POR);
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_NONE);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
    CHECK_EQ(loop_watch_window(NULL), LOOP_WATCH_NONE);
}

/** @brief The feeds of one model run: their count and the longest time
 *         between two of them. */
typedef struct {
    uint32_t now_us;   /**< the model's clock                      */
    uint32_t last_us;  /**< the last feed, or the enable           */
    uint32_t gap_us;   /**< the longest time between two feeds     */
    unsigned feeds;    /**< feeds and enables counted              */
} feeds_t;

/** @brief Counts @p act as a feed when it is one. */
static void fed(feeds_t *f, loop_watch_act_t act)
{
    if (act == LOOP_WATCH_NONE) {
        return;
    }
    const uint32_t gap = f->now_us - f->last_us;
    if (gap > f->gap_us) {
        f->gap_us = gap;
    }
    f->last_us = f->now_us;
    ++f->feeds;
}

/**
 * @brief One pass as the main loop runs it: the feed at its top, then
 *        @p windows flash windows, each fed before its wait for core 1 and
 *        lasting @p window_us with a full wait either side, then
 *        everything else a pass may hold (LOOP_WATCH_REST_US).
 */
static void model_pass(feeds_t *f, unsigned windows, uint32_t window_us)
{
    fed(f, loop_watch_pass(&w, false));
    for (unsigned i = 0; i < windows; ++i) {
        fed(f, loop_watch_window(&w));
        f->now_us += window_us + 2u * LOOP_WATCH_LOCKOUT_US;
    }
    f->now_us += LOOP_WATCH_REST_US;
}

/* Passes with 0 to 6 windows of the flash part's datasheet maximum each,
 * back to back: every window is fed, and the time between two feeds never
 * passes the period, and never the sum the header states.  6 is the most
 * a pass holds (LOOP_WATCH_WINDOWS_MAX). */
TEST_CASE(windows_at_the_datasheet_maximum_never_reach_the_period)
{
    CHECK_EQ(LOOP_WATCH_WINDOWS_MAX, 6u);
    for (unsigned n = 0; n <= LOOP_WATCH_WINDOWS_MAX; ++n) {
        loop_watch_init(&w, false, RESET_CAUSE_POR);
        feeds_t f = { 0u, 0u, 0u, 0u };
        for (int pass = 0; pass < 4; ++pass) {
            model_pass(&f, n, LOOP_WATCH_WINDOW_MAX_US);
        }
        /* The feed that closes the last pass. */
        fed(&f, loop_watch_pass(&w, false));
        CHECK_EQ(f.feeds, 4u * (1u + n) + 1u);
        CHECK(f.gap_us < LOOP_WATCH_PERIOD_MS * 1000u);
        CHECK(f.gap_us <= LOOP_WATCH_WORST_GAP_US);
        if (n == 0u) {
            CHECK_EQ(f.gap_us, LOOP_WATCH_REST_US);
        } else {
            CHECK_EQ(f.gap_us, LOOP_WATCH_WORST_GAP_US);
        }
    }
}

/* One window more than a pass holds is a fault: it gets no feed, and
 * neither does any window after it until the top of the next pass.  A
 * loop that keeps opening windows runs into the period; a pass that comes
 * back to its top is fed there and counts from 0 again. */
TEST_CASE(a_window_past_the_legal_count_is_not_fed_and_the_period_runs_out)
{
    loop_watch_init(&w, false, RESET_CAUSE_POR);
    feeds_t f = { 0u, 0u, 0u, 0u };
    fed(&f, loop_watch_pass(&w, false));               /* the enable */
    for (unsigned i = 0; i < LOOP_WATCH_WINDOWS_MAX; ++i) {
        CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);
        fed(&f, LOOP_WATCH_FEED);
        f.now_us += LOOP_WATCH_WINDOW_SPAN_US;
    }
    CHECK_EQ(f.feeds, 1u + LOOP_WATCH_WINDOWS_MAX);
    CHECK(f.gap_us < LOOP_WATCH_PERIOD_MS * 1000u);

    /* The seventh window and every one after it: no feed.  The loop never
     * comes back to the top of a pass, and the model's clock passes the
     * period since the last feed. */
    unsigned unfed = 0u;
    while (f.now_us - f.last_us < LOOP_WATCH_PERIOD_MS * 1000u) {
        CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
        fed(&f, LOOP_WATCH_NONE);
        f.now_us += LOOP_WATCH_WINDOW_SPAN_US;
        ++unfed;
        CHECK(unfed < 100u);
    }
    CHECK_EQ(f.feeds, 1u + LOOP_WATCH_WINDOWS_MAX);
    CHECK(f.now_us - f.last_us >= LOOP_WATCH_PERIOD_MS * 1000u);
    /* The sixth window itself and 8 more of the longest kind: the reset
     * comes 500ms after the sixth window's feed. */
    CHECK_EQ(unfed, 8u);

    /* A pass that does come back: fed at its top, and its windows are
     * counted from 0. */
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_FEED);
    for (unsigned i = 0; i < LOOP_WATCH_WINDOWS_MAX; ++i) {
        CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);
    }
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);

    /* A watch that is not enabled counts nothing: the boot's windows and a
     * trial's do not use up the first pass's. */
    loop_watch_init(&w, false, RESET_CAUSE_POR);
    for (unsigned i = 0; i < 3u * LOOP_WATCH_WINDOWS_MAX; ++i) {
        CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
    }
    CHECK_EQ(loop_watch_pass(&w, false), LOOP_WATCH_ENABLE);
    for (unsigned i = 0; i < LOOP_WATCH_WINDOWS_MAX; ++i) {
        CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_FEED);
    }
    CHECK_EQ(loop_watch_window(&w), LOOP_WATCH_NONE);
}

/* A pass without a window has its one feed, at its top. */
TEST_CASE(a_pass_without_a_window_has_its_single_feed)
{
    loop_watch_init(&w, false, RESET_CAUSE_POR);
    feeds_t f = { 0u, 0u, 0u, 0u };
    model_pass(&f, 0u, 0u);
    CHECK_EQ(f.feeds, 1u);                     /* the enable */
    for (int pass = 0; pass < 10; ++pass) {
        const unsigned before = f.feeds;
        model_pass(&f, 0u, 0u);
        CHECK_EQ(f.feeds, before + 1u);
    }
    CHECK_EQ(f.gap_us, LOOP_WATCH_REST_US);
}

/* Without the feed before a window, 6 windows at the maximum and the rest
 * of a pass come to 418,250us: under the period by 81,750us, and over it
 * with 8.  The feed is what makes the count of windows not matter. */
TEST_CASE(without_the_window_feed_the_count_of_windows_would_matter)
{
    const uint32_t six = LOOP_WATCH_WINDOWS_MAX * LOOP_WATCH_WINDOW_SPAN_US
                         + LOOP_WATCH_REST_US;
    CHECK_EQ(six, 418250u);
    CHECK(six < LOOP_WATCH_PERIOD_MS * 1000u);
    CHECK(8u * LOOP_WATCH_WINDOW_SPAN_US + LOOP_WATCH_REST_US
          > LOOP_WATCH_PERIOD_MS * 1000u);
}

/* ---------------------------------------------------------- the period */

/* The flash part's maxima (Puya P25Q32SH V1.8, table 5-4), the longest
 * window and the longest time between two feeds, as the header's comment
 * states them. */
TEST_CASE(the_period_is_over_twice_the_longest_time_between_feeds)
{
    CHECK_EQ(LOOP_WATCH_ERASE_MAX_US, 30000u);
    CHECK_EQ(LOOP_WATCH_PAGE_MAX_US, 2500u);
    /* The update's sector: 4096 bytes in pages of 256. */
    CHECK_EQ(LOOP_WATCH_PROGRAM_PAGES, 4096u / 256u);
    CHECK_EQ(LOOP_WATCH_WINDOW_MAX_US, 16u * 2500u + 2000u);
    CHECK(LOOP_WATCH_ERASE_MAX_US + LOOP_WATCH_WINDOW_ENTRY_US
          <= LOOP_WATCH_WINDOW_MAX_US);
    /* The window measured on the bring-up module is under the maximum. */
    CHECK(19178u < LOOP_WATCH_ERASE_MAX_US + LOOP_WATCH_PAGE_MAX_US
                       + LOOP_WATCH_WINDOW_ENTRY_US);
    CHECK_EQ(LOOP_WATCH_REST_US, 2u * 5000u + 35000u + 1000u + 250u);
    CHECK_EQ(LOOP_WATCH_WINDOW_SPAN_US, 62000u);
    CHECK_EQ(LOOP_WATCH_WORST_GAP_US, 108250u);
    CHECK_EQ(LOOP_WATCH_PERIOD_MS, 500u);
    CHECK(2u * LOOP_WATCH_WORST_GAP_US < LOOP_WATCH_PERIOD_MS * 1000u);
    /* What one window may take before the reset falls into it. */
    CHECK_EQ(LOOP_WATCH_PERIOD_MS * 1000u - 2u * LOOP_WATCH_LOCKOUT_US
                 - LOOP_WATCH_REST_US,
             433750u);
    /* A W25Q16JV's 400ms sector erase stays under that as well. */
    CHECK(400000u < 433750u);
    /* The link's silence timeout (200ms) is shorter than the period: the
     * IOmcu's own failsafe acts first on a pass that long. */
    CHECK(LOOP_WATCH_PERIOD_MS > LINK_DEV_SILENCE_MS);
    /* The RP2350's watchdog counts 24 bits of microseconds. */
    CHECK(LOOP_WATCH_PERIOD_MS * 1000u <= 0x00FFFFFFu);
}

int main(void)
{
    RUN(the_first_pass_enables_and_every_later_pass_feeds);
    RUN(an_image_on_trial_leaves_the_watchdog_alone_until_its_buy);
    RUN(the_console_build_never_enables_the_watchdog);
    RUN(a_missing_watch_does_nothing);
    RUN(only_a_loop_watchdog_restart_sets_the_fault_bit);
    RUN(a_stored_window_over_clear_takes_the_bit_away);
    RUN(the_panels_clear_ends_the_bit_and_no_other_write_does);
    RUN(a_flash_window_feeds_an_enabled_watchdog_and_enables_none);
    RUN(windows_at_the_datasheet_maximum_never_reach_the_period);
    RUN(a_window_past_the_legal_count_is_not_fed_and_the_period_runs_out);
    RUN(a_pass_without_a_window_has_its_single_feed);
    RUN(without_the_window_feed_the_count_of_windows_would_matter);
    RUN(the_period_is_over_twice_the_longest_time_between_feeds);
    return test_summary("loop_watch");
}
