/*
 * The first thing to run when two CAN nodes are wired together.
 *
 * Before the page protocol means anything, one question has to be answered:
 * do frames cross this bus intact?  That is not the same question as "does
 * the link work", and answering them together is how a bring-up turns into an
 * afternoon.  A bit timing that is slightly wrong, a missing terminator, a
 * transceiver in the wrong mode and a dispatcher bug all present as "the panel
 * shows no numbers".
 *
 * So this is an echo test and nothing else.  The initiator sends a frame with
 * a sequence number and a payload; the responder sends it straight back; the
 * initiator checks it came back byte for byte.  No page map, no registers, no
 * state machine above it.  If this passes and the link still does not work,
 * the fault is above the wire -- which is worth knowing in itself.
 *
 * THE PAYLOADS ARE NOT ARBITRARY.  CAN stuffs a complementary bit after five
 * of the same polarity, so the patterns that stress a marginal bus are long
 * runs of one level and the transitions either side of them.  A test that only
 * ever sent counting integers would pass on a bus that fails on real traffic,
 * so the patterns cycle through all-dominant, all-recessive, alternating and
 * counting, and the sequence number is checked against the pattern it was sent
 * with.
 *
 * It runs at the lowest arbitration priority there is, on a page the map does
 * not use, so it can be left running while other things happen and can never
 * delay them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_CAN_SELFTEST_H
#define RCBENCH_CAN_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

#include "link_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A page number the map does not use, so a probe cannot be mistaken for
 *  register traffic by anything at either end. */
#define CAN_SELFTEST_PAGE 0x7Eu

/** How many probes before a verdict is worth stating. */
#define CAN_SELFTEST_MIN_PROBES 8u

typedef enum {
    CAN_SELFTEST_RUNNING = 0, /**< too early to say */
    CAN_SELFTEST_OK,
    /** Nothing came back at all. Wiring, bit rate, or a silent far end. */
    CAN_SELFTEST_SILENT,
    /** Frames come back with the wrong contents. Bit timing or termination. */
    CAN_SELFTEST_CORRUPT,
    /** Frames cross, and not all of them. */
    CAN_SELFTEST_LOSSY,
} can_selftest_verdict_t;

typedef struct {
    uint32_t sent;
    uint32_t echoed;      /**< came back, and correct */
    uint32_t timed_out;
    uint32_t corrupt;     /**< came back wrong: the worst of the three */
    uint32_t stale;       /**< an echo of something no longer outstanding */

    uint16_t seq;         /**< next sequence number to send */
    uint16_t outstanding; /**< the one being waited for */
    bool     waiting;
    uint32_t sent_ms;
    uint32_t timeout_ms;

    uint32_t rtt_min_us;
    uint32_t rtt_max_us;
} can_selftest_t;

void can_selftest_init(can_selftest_t *st, uint32_t timeout_ms);

/**
 * Build the next probe, if it is time for one.
 *
 * False while a probe is still outstanding and has not timed out: one frame in
 * flight at a time, so a lost frame is attributed to the probe that was lost
 * rather than to whichever reply happens to arrive next.
 */
bool can_selftest_probe(can_selftest_t *st, uint32_t now_ms,
                        link_can_frame_t *out);

/**
 * Judge a received frame. True if it was this test's business.
 *
 * @p rtt_us is folded in when the frame is the echo being waited for.
 */
bool can_selftest_rx(can_selftest_t *st, const link_can_frame_t *f,
                     uint32_t rtt_us);

/** Give up on an outstanding probe whose time is up. True if one was dropped. */
bool can_selftest_tick(can_selftest_t *st, uint32_t now_ms);

can_selftest_verdict_t can_selftest_verdict(const can_selftest_t *st);
const char *can_selftest_text(can_selftest_verdict_t v);
const char *can_selftest_hint(can_selftest_verdict_t v);

/* ------------------------------------------------------------- responder */

/**
 * The far end's whole job: if this frame is a probe, fill @p out with the
 * echo. True when it did.
 *
 * Stateless on purpose. A responder that counted anything could disagree with
 * the initiator about what happened, and then the bring-up has two stories.
 */
bool can_selftest_echo(const link_can_frame_t *in, link_can_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CAN_SELFTEST_H */
