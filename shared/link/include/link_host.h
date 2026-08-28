/*
 * The panel's half of the link: poll, match the reply to the question, and
 * escalate when the answers stop.
 *
 * One request outstanding at a time, which is not a simplification but the
 * protocol: the coprocessor never speaks unsolicited, so there is exactly one
 * frame in flight and nothing to arbitrate.
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
 * How long the panel tolerates silence before it escalates.  Five times the
 * coprocessor's own limit, deliberately: by the time the host gives up, the
 * end holding the outputs has already given up four times over.
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

    uint32_t last_reply_ms;
    bool     escalated;

    uint32_t polls;
    uint32_t replies;
    uint32_t mismatches; /**< answers to questions nobody is still asking */
    uint32_t nacks;
    uint32_t timeouts;   /**< requests abandoned at the escalation           */

    /*
     * Reassembly.  A CAN frame carries four registers, so a reply to a
     * thirteen-register read arrives as four messages, each describing itself
     * -- its own offset and its own count.  There is no sequence to follow and
     * no continuation to wait for; the window asked for is known here, and a
     * bit is set as each part of it lands.
     */
    uint16_t acc[LINK_MAX_REGS];
    uint32_t acc_seen;   /**< bit per register within the request's window */
} link_host_t;

void link_host_init(link_host_t *h, uint32_t now_ms);

/**
 * Build a request.  Returns its length on the wire, or 0 if it would not fit
 * or a request is already outstanding -- the caller does not get to have two.
 */
bool link_host_read(link_host_t *h, uint8_t page, uint8_t offset,
                    uint8_t count, link_msg_t *out);
bool link_host_write(link_host_t *h, uint8_t page, uint8_t offset,
                     uint8_t count, const uint16_t *regs, link_msg_t *out);

/**
 * Offer a decoded frame as the answer.  Returns true only if it answers the
 * outstanding request; anything else increments `mismatches` and is dropped.
 *
 * That check is not pedantry.  A reply delayed past a timeout arrives after
 * the host has moved on, and accepting it would attribute one page's registers
 * to another -- silently, and with the CRC entirely happy.
 */
/**
 * Offer one received message against the outstanding request.
 *
 * True when the request is *answered*, which for a read may take several
 * messages: @p whole then holds the reassembled reply. Parts that land
 * without completing it return false and are not a fault -- the caller keeps
 * feeding until it gets true or the request times out.
 *
 * Anything that does not answer the question in flight is counted as a
 * mismatch and refused. An answer to a question nobody is still asking is the
 * failure this exists to catch: on a bus where a late reply can arrive after
 * the panel has moved on, accepting it would attach one page's numbers to
 * another page's request.
 */
bool link_host_accept(link_host_t *h, const link_msg_t *part, uint32_t now_ms,
                      link_msg_t *whole);

/** True on the edge where the host escalates, so it is reported once. */
bool link_host_tick(link_host_t *h, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_HOST_H */
