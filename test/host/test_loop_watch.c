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
 * CONTROL page rules; and the period against the longest pass.
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

/* ---------------------------------------------------------- the period */

/* The longest pass the code allows, summed from its parts, and the period
 * 1.5 times that at least: the figures the header's comment states. */
TEST_CASE(the_period_covers_the_longest_pass_with_half_of_it_to_spare)
{
    const uint32_t windows = 6u * (19178u + 2u * 10000u);
    CHECK_EQ(windows, 235068u);
    CHECK_EQ(LOOP_WATCH_WORST_PASS_US,
             windows + 2u * 5000u + 35000u + 1000u + 250u);
    CHECK_EQ(LOOP_WATCH_WORST_PASS_US, 281318u);
    CHECK_EQ(LOOP_WATCH_PERIOD_MS, 500u);
    CHECK(LOOP_WATCH_WORST_PASS_US * 3u / 2u < LOOP_WATCH_PERIOD_MS * 1000u);
    /* What one flash window may take over its measured figure. */
    CHECK_EQ(LOOP_WATCH_PERIOD_MS * 1000u - LOOP_WATCH_WORST_PASS_US,
             218682u);
    /* Under the link's silence timeout it is not: the IOmcu's own
     * failsafe (200ms) acts first on a pass that long. */
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
    RUN(the_period_covers_the_longest_pass_with_half_of_it_to_spare);
    return test_summary("loop_watch");
}
