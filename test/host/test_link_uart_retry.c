/**
 * @file
 * @brief The repeat and the timeout over a modelled UART (universal
 *        asynchronous receiver-transmitter): the node's link_host_retry()
 *        against the IOmcu's link_uart_dev.c and link_dev_dispatch()
 *        (uart_link_rig.h).
 *
 * The node polls with the 50ms repeat and the 1000ms timeout of the CAN
 * (Controller Area Network) link, unchanged (LINK_HOST_RETRY_MS,
 * LINK_HOST_TIMEOUT_MS).  The cases hold them against what the UART adds:
 * a reader that stalls for a 42ms flash window while the DMA (direct memory
 * access) fills the ring, delay on the line, lost answer bytes, noise that
 * laps the ring, and the 32-bit ms wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "uart_link_rig.h"

/** @brief The longest flash window of the IOmcu's core 0, ms. */
#define FLASH_STALL_MS 42u

/**
 * @brief Stall the IOmcu's reader from now for @p ms.
 * @param ms the stall
 */
static void stall_from_now(uint32_t ms)
{
    rig.stall_from = rig.t;
    rig.stall_to = rig.t + ms * 1000u;
}

/* A request that arrives at the start of a 42ms flash window waits in the
 * ring and is answered after it, before the 50ms repeat is due: no repeat,
 * no failsafe. */
TEST_CASE(the_answer_waits_out_a_42ms_stall_without_a_repeat)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    stall_from_now(FLASH_STALL_MS);
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(9u, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK(ended - start >= FLASH_STALL_MS);
    CHECK(ended - start < LINK_HOST_RETRY_MS);
    CHECK_EQ(rig.host.retried, 0u);
    CHECK_EQ(rig.served, 1u);
    CHECK_EQ(rig.rd.laps, 0u);
    CHECK_EQ(rig.refused, 0u);
    CHECK(!rig.dev.failsafe);
}

/* The stall with 5ms of line delay each way: the answer reaches the node
 * after its repeat went out.  The first answer is taken, the repeat is
 * answered from the copy, and the write acts once. */
TEST_CASE(a_stall_and_delay_past_50ms_act_once)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.up.delay_us = 5000u;
    rig.down.delay_us = 5000u;
    /* The window opens as the request arrives. */
    rig.stall_from = rig.t + 5000u;
    rig.stall_to = rig.stall_from + FLASH_STALL_MS * 1000u;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(3u, &got, &ended));
    CHECK_EQ(got.op, LINK_OP_ACK);
    CHECK(ended - start > LINK_HOST_RETRY_MS);
    CHECK(ended - start <= 5u + FLASH_STALL_MS + 5u + 1u);
    CHECK_EQ(rig.host.retried, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    rig_run(20000u);
    CHECK_EQ(rig.served, 2u);
    CHECK_EQ(rig.dev.repeats, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
}

/* Line delay alone: an answer in by 49ms needs no repeat; one after 50ms
 * gets the repeat, and the first answer is still the one taken. */
TEST_CASE(an_answer_by_49ms_needs_no_repeat_and_one_after_50ms_gets_one)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.down.delay_us = 48000u;
    uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(4u, &got, &ended));
    CHECK_EQ(ended - start, 48u);
    CHECK_EQ(rig.host.retried, 0u);

    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.down.delay_us = 50500u;
    start = rig_ms();
    CHECK(rig_write_control(4u, &got, &ended));
    CHECK_EQ(ended - start, 50u);
    CHECK_EQ(rig.host.retried, 1u);
    rig_run(60000u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(rig.dev.repeats, 1u);
}

/* The answer's bytes lost on the line: the repeat at 50ms is answered from
 * the copy within the millisecond. */
TEST_CASE(a_lost_answer_is_recovered_by_the_repeat_at_50ms)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.down.lose_from = rig.down.n;
    rig.down.lose_to = rig.down.n + 11u;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(77u, &got, &ended));
    CHECK_EQ(got.regs[0], 77u);
    CHECK_EQ(ended - start, LINK_HOST_RETRY_MS);
    CHECK_EQ(rig.host.retried, 1u);
    CHECK_EQ(rig.host.timeouts, 0u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(rig.dev.repeats, 1u);
    CHECK_EQ(rig.nrefused, 0u);
    CHECK(!rig.dev.failsafe);
}

/* Every answer lost: the wait ends at the 1000ms timeout, and the IOmcu,
 * hearing nothing after the repeat, is in failsafe 200ms after it. */
TEST_CASE(a_lost_repeat_ends_at_the_1000ms_timeout)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    rig.down.lose_from = rig.down.n;
    rig.down.lose_to = RIG_NONE - 1u;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!rig_write_control(5u, &got, &ended));
    CHECK_EQ(ended - start, LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(rig.host.timeouts, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(rig.served, 2u);
    CHECK(rig.dev.failsafe);
    CHECK_EQ(rig.dev.last_request_ms, start + LINK_HOST_RETRY_MS);
}

/* The repeat and the timeout across the 32-bit ms wrap. */
TEST_CASE(the_repeat_and_the_timeout_hold_across_the_wrap)
{
    rig_fresh(0xFFFFFFFFu - 20u, LINK_UART_DMA_RELOAD);
    rig.down.lose_from = rig.down.n;
    rig.down.lose_to = rig.down.n + 11u;
    uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(6u, &got, &ended));
    CHECK_EQ((uint32_t)(ended - start), LINK_HOST_RETRY_MS);
    CHECK(ended < start);
    CHECK_EQ(rig.host.retried, 1u);

    rig_fresh(0xFFFFFFFFu - 500u, LINK_UART_DMA_RELOAD);
    rig.down.lose_from = rig.down.n;
    rig.down.lose_to = RIG_NONE - 1u;
    start = rig_ms();
    CHECK(!rig_write_control(7u, &got, &ended));
    CHECK_EQ((uint32_t)(ended - start), LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(rig.host.timeouts, 1u);
}

/* Noise behind the request while the reader stalls laps the ring: 2000
 * bytes of 0x55 and a 0x00.  The request is lost and counted as one lap,
 * nothing as refused; the repeat at 50ms is the request the IOmcu acts
 * on. */
TEST_CASE(noise_that_laps_the_ring_in_a_stall_is_one_lap_and_the_repeat_acts)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    stall_from_now(FLASH_STALL_MS);
    rig.noise_at = rig.t + 200u;
    rig.noise_n = 2000u;
    rig.noise_b = 0x55u;
    rig.noise_delim = true;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(8u, &got, &ended));
    CHECK_EQ(rig.rd.laps, 1u);
    CHECK_EQ(rig.refused, 0u);
    CHECK_EQ(rig.served, 1u);
    CHECK_EQ(rig.host.retried, 1u);
    CHECK_EQ(g_far.control_writes, 1u);
    CHECK_EQ(g_far.control[1], 8u);
    CHECK_EQ(ended - start, LINK_HOST_RETRY_MS);
}

/* The same noise without a 0x00 at its end: the reader drops bytes up to
 * the next 0x00, which ends the repeat, so the repeat is lost as well and
 * the wait ends at the 1000ms timeout.  Nothing acts. */
TEST_CASE(noise_without_a_delimiter_costs_the_repeat_too)
{
    rig_fresh(1000u, LINK_UART_DMA_RELOAD);
    stall_from_now(FLASH_STALL_MS);
    rig.noise_at = rig.t + 200u;
    rig.noise_n = 2000u;
    rig.noise_b = 0x55u;
    rig.noise_delim = false;
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(!rig_write_control(8u, &got, &ended));
    CHECK_EQ(ended - start, LINK_HOST_TIMEOUT_MS);
    CHECK_EQ(rig.rd.laps, 1u);
    CHECK_EQ(rig.refused, 0u);
    CHECK_EQ(rig.served, 0u);
    CHECK_EQ(g_far.control_writes, 0u);
    /* The next request after it is taken. */
    rig.host.numbered = true;
    CHECK(rig_write_control(9u, &got, &ended));
    CHECK_EQ(g_far.control_writes, 1u);
}

/* A DMA that re-triggers every 1024 bytes, the ring's size: 201 exchanges
 * after a stall run the count across 4 reloads, and no byte is lost. */
TEST_CASE(a_short_reload_counts_every_byte_across_the_stall)
{
    rig_fresh(1000u, LINK_UART_RING_BYTES);
    stall_from_now(FLASH_STALL_MS);
    const uint32_t start = rig_ms();
    link_msg_t got;
    uint32_t ended = 0u;
    CHECK(rig_write_control(10u, &got, &ended));
    CHECK(ended - start < LINK_HOST_RETRY_MS);
    for (unsigned k = 0; k < 200u; ++k) {
        CHECK(rig_write_control((uint16_t)k, &got, &ended));
    }
    CHECK_EQ(rig.rd.head, rig.written);
    CHECK(rig.written > 2u * LINK_UART_RING_BYTES);
    CHECK_EQ(rig.rd.laps, 0u);
    CHECK_EQ(rig.host.retried, 0u);
    CHECK_EQ(g_far.control_writes, 201u);
}

int main(void)
{
    RUN(the_answer_waits_out_a_42ms_stall_without_a_repeat);
    RUN(a_stall_and_delay_past_50ms_act_once);
    RUN(an_answer_by_49ms_needs_no_repeat_and_one_after_50ms_gets_one);
    RUN(a_lost_answer_is_recovered_by_the_repeat_at_50ms);
    RUN(a_lost_repeat_ends_at_the_1000ms_timeout);
    RUN(the_repeat_and_the_timeout_hold_across_the_wrap);
    RUN(noise_that_laps_the_ring_in_a_stall_is_one_lap_and_the_repeat_acts);
    RUN(noise_without_a_delimiter_costs_the_repeat_too);
    RUN(a_short_reload_counts_every_byte_across_the_stall);
    return test_summary("link_uart_retry");
}
