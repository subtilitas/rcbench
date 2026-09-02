/*
 * The CAN (Controller Area Network) echo self-test: the first check when two
 * nodes are wired together.
 *
 * It answers one question: do frames cross this bus intact?  The initiator
 * sends a frame with a sequence number and a payload, the responder sends it
 * back unchanged, and the initiator compares it byte for byte.  No page map,
 * no registers, no state machine above it.  A passing test with a link that
 * does not work places the fault above the wire.  A wrong bit timing, a
 * missing terminator, a transceiver in the wrong mode and a dispatcher bug
 * all present as a panel that shows no numbers; this test separates the
 * first three from the fourth.
 *
 * The payloads exercise bit stuffing.  CAN stuffs a complementary bit after
 * five bits of the same polarity, so a marginal bus fails on long runs of one
 * level.  The first two payload bytes carry the sequence number; the other
 * six carry one of four patterns selected by it (0x00, 0xFF, 0x55, 0xAA),
 * and the echo is checked against the pattern it was sent with.
 *
 * The test runs at the lowest arbitration priority on a page the map does
 * not use, so it can run beside normal traffic without delaying it.
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
    /** Frames cross, and not all of them, and the bus reported errors while
     *  they went missing. Electrical: timing, termination, length. */
    CAN_SELFTEST_LOSSY,
    /**
     * Frames went missing with no bus error.
     *
     * A frame corrupted on the wire produces a bit, stuff, form or CRC
     * (cyclic redundancy check) error at some controller.  None did, so the
     * frames arrived intact and a receive buffer overran because its owner
     * did not read in time.  Not a wiring fault.
     */
    CAN_SELFTEST_DROPPED,
} can_selftest_verdict_t;

typedef struct {
    uint32_t sent;
    uint32_t echoed;      /**< came back, and correct */
    uint32_t timed_out;
    uint32_t corrupt;     /**< came back wrong: the worst of the three */
    uint32_t stale;       /**< an echo of something not outstanding */

    uint16_t seq;         /**< next sequence number to send */
    uint16_t outstanding; /**< the one being waited for */
    bool     waiting;
    uint32_t sent_ms;
    uint32_t timeout_ms;

    uint32_t rtt_min_us;
    uint32_t rtt_max_us;

    /**
     * Bus errors counted by the local controller, filled in by the caller
     * before asking for a verdict.  This is what separates a wire fault
     * (LOSSY) from a software one (DROPPED).  Left at zero, loss is reported
     * as DROPPED.
     */
    uint32_t bus_errors;
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

/* ----------------------------------------------- the far end's own numbers */

/*
 * The coprocessor's view of the bus, carried across the bus in one 8-byte
 * frame on the self-test page at the lowest priority, so one console shows
 * both ends of a fault.
 *
 * It is polled: the coprocessor transmits only in answer to a request.
 */
typedef struct {
    bool     up;          /**< the controller answered on SPI (Serial
                               Peripheral Interface) at boot */
    uint8_t  tx_errors;   /**< transmit error counter; 128 is error-passive */
    uint8_t  rx_errors;   /**< receive error counter */
    uint8_t  flags;       /**< the controller's error flag register */
    uint16_t echoes;      /**< probes it has answered, wrapping */
    uint16_t overflows;   /**< frames that arrived with nowhere to put them */
} can_remote_status_t;

/** Build the request: one frame, no payload. */
bool can_selftest_status_request(link_can_frame_t *out);

/** The far end's answer. True if @p in was a status request. */
bool can_selftest_status_reply(const link_can_frame_t *in,
                               const can_remote_status_t *st,
                               link_can_frame_t *out);

/** Read an answer. True if @p in was one. */
bool can_selftest_status_parse(const link_can_frame_t *in,
                               can_remote_status_t *out);

/**
 * Frames the far end answered that this end never heard.
 *
 * @p before and @p after are the far end's echo counter sampled either side
 * of the measurement; @p heard is everything this end received in between,
 * late and altered echoes included, since those crossed the return path too.
 *
 * Only the difference of the two samples is meaningful.  The far end's
 * counter runs from its own boot, and a coprocessor powered through several
 * panel reboots is tens of thousands ahead of the panel's count.  The counter
 * is 16 bits and wraps; unsigned subtraction handles that.
 *
 * Returns how many went missing, or 0.  A probe in flight across either
 * boundary is not a fault, so a difference of up to 2 is not reported.
 */
uint16_t can_selftest_return_loss(uint16_t before, uint16_t after,
                                  uint32_t heard);

/* ------------------------------------------------------------- responder */

/**
 * The responder: if @p in is a probe, fill @p out with the echo.  True when
 * it did.  Stateless, so the responder cannot disagree with the initiator
 * about what happened.
 */
bool can_selftest_echo(const link_can_frame_t *in, link_can_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CAN_SELFTEST_H */
