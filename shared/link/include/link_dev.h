/*
 * The coprocessor's half of the link: answer polls, and fail safe when they
 * stop.
 *
 * Pure C with the clock passed in as a millisecond count.  No timers, no
 * FreeRTOS, no pico-sdk -- which is what lets both watchdogs be driven through
 * their transitions on a laptop, including across the 32-bit wrap that a real
 * one reaches after 49 days and a test reaches in a microsecond.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_DEV_H
#define RCBENCH_LINK_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Silence after which the coprocessor fills failsafe values on its own
 * authority.  IOMCU's ratio is worth copying: the far end gives up in 200 ms
 * and the host escalates at a second, so the processor holding the outputs is
 * always the more suspicious of the two.
 */
#define LINK_DEV_SILENCE_MS 200u

/**
 * One page, as the coprocessor sees it.
 *
 * `read` is not optional: a page nobody can read is a page nobody can check
 * against what they wrote.  `write` being NULL is what makes a page read-only,
 * and asking to write one is answered rather than ignored.
 */
typedef struct {
    uint8_t page;
    uint8_t count;    /**< registers in this page, 1..LINK_MAX_REGS */
    void  (*read)(void *ctx, uint8_t offset, uint8_t count, uint16_t *out);
    /** Returns 0 to accept, or a link_nack_t to refuse with a reason. */
    uint8_t (*write)(void *ctx, uint8_t offset, uint8_t count,
                     const uint16_t *in);
} link_page_t;

typedef struct {
    const link_page_t *pages;
    uint8_t            page_count;
    void              *ctx;

    uint32_t last_request_ms;
    bool     silent;      /**< nothing has arrived for LINK_DEV_SILENCE_MS  */
    bool     failsafe;    /**< latched: only an explicit clear leaves it    */
    uint32_t requests;
} link_dev_t;

void link_dev_init(link_dev_t *d, const link_page_t *pages, uint8_t page_count,
                   void *ctx, uint32_t now_ms);

/**
 * Answer one decoded request.  Writes the reply frame into @p out and returns
 * its length, or 0 if there is nothing to send.
 *
 * Every request gets exactly one reply, including the refusals -- silence is
 * reserved for a coprocessor that is not there, so it must never also mean
 * "I heard you and declined".
 */
/**
 * Decide what to answer, without deciding how to carry it.
 *
 * The whole of the dispatcher's judgement -- which page, whether the range
 * fits, whether the write was accepted, what a NACK should say -- with no
 * transport in it. Two of them exist now: bytes over a UART, and identifiers
 * over CAN. Neither is visible from here, which is what let CAN be added
 * without touching a line of this file's reasoning.
 *
 * False only on a null argument; every real request produces a reply, because
 * a request that gets no answer is indistinguishable from a dead link.
 */
bool link_dev_dispatch(link_dev_t *d, const link_msg_t *req,
                       link_msg_t *reply, uint32_t now_ms);


/**
 * Advance the silence watchdog.  Returns true on the edge where failsafe
 * fires, so the caller can drop the outputs once rather than every tick.
 */
bool link_dev_tick(link_dev_t *d, uint32_t now_ms);

/**
 * Leave failsafe.  Not exposed as "the link came back": recovery of the wire
 * is not consent to spin a propeller, so the host asks for this by writing
 * LINK_CLEAR_MAGIC to the control page.
 */
void link_dev_clear_failsafe(link_dev_t *d, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_DEV_H */
