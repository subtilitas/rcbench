/*
 * The panel's half of the link: poll, match the reply to the question, and
 * escalate when the answers stop.
 *
 * One request outstanding at a time, which is the protocol: the coprocessor
 * transmits only when asked, so there is one request in flight and nothing
 * to arbitrate.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_HOST_H
#define RCBENCH_LINK_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How long the panel tolerates silence before it escalates: five times the
 * coprocessor's 200 ms, so the end holding the outputs gives up first.
 */
#define LINK_HOST_TIMEOUT_MS 1000u

typedef struct {
    /* The outstanding question, kept so an answer to an older one cannot be
     * mistaken for an answer to this one. */
    bool     pending;
    uint8_t  op;
    uint8_t  page;
    uint8_t  offset;
    uint8_t  count;

    uint32_t sent_ms;      /**< when the outstanding request went out */
    uint32_t last_reply_ms;
    bool     escalated;

    uint32_t polls;
    uint32_t replies;
    uint32_t mismatches; /**< answers to questions nobody is still asking */
    uint32_t nacks;
    uint32_t timeouts;   /**< requests abandoned at the escalation           */

    /*
     * Reassembly.  A CAN (Controller Area Network) frame carries four
     * registers, so a reply to a 13-register read arrives as four messages,
     * each with its own offset and count.  There is no sequence to follow
     * and no continuation to wait for: the window asked for is known here,
     * and a bit is set as each part of it lands.
     */
    uint16_t acc[LINK_MAX_REGS];
    uint32_t acc_seen;   /**< bit per register within the request's window */
} link_host_t;

void link_host_init(link_host_t *h, uint32_t now_ms);

/**
 * Build a request.  Returns false if the window does not fit a page or a
 * request is already outstanding: one at a time.
 */
bool link_host_read(link_host_t *h, uint8_t page, uint8_t offset,
                    uint8_t count, uint32_t now_ms, link_msg_t *out);
bool link_host_write(link_host_t *h, uint8_t page, uint8_t offset,
                     uint8_t count, const uint16_t *regs, uint32_t now_ms,
                     link_msg_t *out);

/**
 * Offer one received message against the outstanding request.
 *
 * True when the request is answered, which for a read may take several
 * messages: @p whole then holds the reassembled reply.  Parts that land
 * without completing it return false and are not a fault; the caller keeps
 * feeding until it gets true or the request times out.
 *
 * Anything that does not answer the question in flight is counted as a
 * mismatch and refused.  A late reply can arrive after the panel has moved
 * on, and accepting it would attach one page's registers to another page's
 * request.
 */
bool link_host_accept(link_host_t *h, const link_msg_t *part, uint32_t now_ms,
                      link_msg_t *whole);

/**
 * Time out the outstanding request, and report the link down once.
 *
 * Two clocks: a request is abandoned when it has been outstanding for
 * LINK_HOST_TIMEOUT_MS, every time; the link is reported down when nothing
 * has been answered for LINK_HOST_TIMEOUT_MS, once until the next reply.
 *
 * True if it abandoned a request or escalated; a caller waiting for a reply
 * stops waiting.
 */
bool link_host_tick(link_host_t *h, uint32_t now_ms);

/**
 * Give up on the outstanding request.
 *
 * For a caller that failed to transmit it: nothing is on the wire to wait
 * for, and an outstanding request refuses every later one until its timeout.
 */
void link_host_abandon(link_host_t *h);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_HOST_H */
