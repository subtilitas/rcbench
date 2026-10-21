/**
 * @file
 * @brief The IOmcu's half of the link: answer polls, and fail safe when they
 * stop.
 *
 * Pure C with the clock passed in as a millisecond count.  No timers, no
 * FreeRTOS, no pico-sdk, so both watchdogs are driven through their transitions
 * on the host, including across the 32-bit wrap at 49.7 days.
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
 * @brief Silence after which the IOmcu fills failsafe values on its own
 * authority.
 *
 * The ratio follows ArduPilot's IOMCU (I/O microcontroller): the IOmcu gives up
 * after 200 ms and the host escalates after 1 s, so the processor holding the
 * outputs is the more suspicious of the two.
 */
#define LINK_DEV_SILENCE_MS 200u

/**
 * @brief One page, as the IOmcu sees it.
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

/**
 * @brief The IOmcu's end of the link: pages, the silence watchdog and the arm latch.
 */
typedef struct {
    const link_page_t *pages;
    uint8_t            page_count;
    void              *ctx;

    uint32_t last_request_ms;
    /**
     * Whether a request has ever arrived.
     *
     * Silence before the first one is not silence: this end is awake a few
     * hundred milliseconds before the panel starts polling, and counting
     * that as a fault lights the panel's indicator from power-on, on every
     * bench, before anything has happened. A warning that is always on is
     * one nobody reads.
     */
    bool     heard;
    bool     silent;      /**< nothing has arrived for LINK_DEV_SILENCE_MS  */
    bool     failsafe;    /**< latched: only an explicit clear leaves it    */
    /**
     * The arm latch: while set, a write of ARM is refused.
     *
     * Set by link_dev_init(), on the silence edge and by
     * link_dev_latch_arm(); cleared by link_dev_clear_failsafe() and by
     * nothing else.  It is not `failsafe`: that flag says the link went
     * silent, gates the supply and is reported on STATUS, and an IOmcu
     * that has just started has had no silence.  What a start and a silence
     * share is that the ARM register the host goes on writing is older than
     * the event, so neither may arm on it.
     */
    bool     arm_latched;
    uint32_t requests;
} link_dev_t;

/**
 * @brief Start with no request heard, out of failsafe and with the arm latch
 * set: the first ARM after a start needs a CLEAR ahead of it.
 *
 * @param d The link state to set up.
 * @param pages The page table; kept, not copied.
 * @param page_count Entries in pages.
 * @param ctx Passed to every page callback.
 * @param now_ms The current time, in ms.
 */
void link_dev_init(link_dev_t *d, const link_page_t *pages, uint8_t page_count,
                   void *ctx, uint32_t now_ms);

/**
 * @brief Decide what to answer, without deciding how to carry it: which page,
 * whether the range fits, whether the write is accepted, and what a NACK
 * (negative acknowledge) says.
 *
 * No transport is visible from here.
 *
 * False only on a null argument.  Every request produces exactly one reply,
 * refusals included: silence means an IOmcu that is not there, and must not
 * also mean a declined request.
 *
 * @param d The link state.
 * @param req The request received.
 * @param reply Receives the answer: DATA, ACK or NACK.
 * @param now_ms The current time, in ms; restarts the silence watchdog.
 * @return true when a reply was built, false on a null argument.
 */
bool link_dev_dispatch(link_dev_t *d, const link_msg_t *req,
                       link_msg_t *reply, uint32_t now_ms);

/**
 * @brief What link_dev_dispatch() answers, without touching the silence
 * watchdog: the same page lookup, range check, read, write and refusals.
 *
 * For a caller on this end that is not the link, such as the bench console
 * (page_console.h): a request from there proves nothing about the panel, so it
 * must not hold the failsafe off.
 *
 * False only on a null argument.
 *
 * @param d The link state.
 * @param req The request to answer.
 * @param reply Receives the answer.
 * @return true when a reply was built, false on a null argument.
 */
bool link_dev_apply(const link_dev_t *d, const link_msg_t *req,
                    link_msg_t *reply);


/**
 * @brief Advance the silence watchdog.
 *
 * Returns true on the edge where failsafe fires, so the caller can drop the
 * outputs once rather than every tick. The edge sets the arm latch too.
 *
 * @param d The link state.
 * @param now_ms The current time, in ms.
 * @return true only on the tick where failsafe fires.
 */
bool link_dev_tick(link_dev_t *d, uint32_t now_ms);

/**
 * @brief Leave failsafe and release the arm latch.
 *
 * Not exposed as "the link came back": recovery of the wire is not consent to
 * spin a propeller, so the host asks for this by writing LINK_CLEAR_MAGIC to
 * the control page.
 *
 * @param d The link state.
 * @param now_ms The current time, in ms; restarts the silence watchdog.
 */
void link_dev_clear_failsafe(link_dev_t *d, uint32_t now_ms);

/**
 * @brief Set the arm latch: a cause other than link silence took the outputs
 * down, and the ARM the host goes on writing does not bring them back.
 *
 * Leaves `failsafe` as it is.
 *
 * @param d The link state.
 */
void link_dev_latch_arm(link_dev_t *d);

/**
 * @brief Whether a write of ARM is refused until the host writes CLEAR.
 *
 * @param d The link state; NULL counts as latched.
 * @return true while the latch is set.
 */
bool link_dev_arm_latched(const link_dev_t *d);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_DEV_H */
