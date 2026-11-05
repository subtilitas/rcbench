/**
 * @file
 * @brief The panel's half of the link: poll, match the reply to the question,
 * and escalate when the answers stop.
 *
 * One request outstanding at a time, which is the protocol: the IOmcu
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
 * @brief How long the panel tolerates silence before it escalates: five times
 * the IOmcu's 200ms, so the end holding the outputs gives up first.
 */
#define LINK_HOST_TIMEOUT_MS 1000u

/**
 * @brief How long a numbered request waits for its answer before it is sent
 *        once more: 50ms.
 *
 * The longest answer the IOmcu gives on purpose is a COMMIT or ACTIVATE
 * that writes a 4KiB sector first, about 19.2ms; a store save delays a
 * request by up to 19ms the same way.  The round trip of one exchange is
 * 334 to 1400us.  So an answer not in by 50ms is lost rather than slow, and
 * the repeat, sent at most one 50ms poll after the request before, reaches
 * the IOmcu well inside its LINK_DEV_SILENCE_MS (200ms).
 */
#define LINK_HOST_RETRY_MS 50u

/**
 * @brief The latest a repeat is sent, from the request: the IOmcu's
 *        LINK_DEV_SILENCE_MS.  A caller that comes back to the wait later
 *        than this waits out LINK_HOST_TIMEOUT_MS instead.
 */
#define LINK_HOST_RETRY_LAST_MS 200u

/**
 * @brief The panel's state for one link: the outstanding request, the counters and the reassembly buffer.
 */
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
    /** @brief Requests answered after their repeat was sent, modulo 2^32. */
    uint32_t retried;

    /*
     * The repeat (4.37, LINK_MINOR_REPEAT).  The outstanding request as it
     * was sent, its exchange number, whether it may be repeated and whether
     * the repeat has gone.
     */
    link_msg_t req;      /**< the outstanding request, as built            */
    uint8_t    seq;      /**< its exchange number; 0 unnumbered            */
    uint8_t    next_seq; /**< the number the next request takes, 1 to 7    */
    bool       retry_ok; /**< it is numbered and may be repeated           */
    bool       retry_sent; /**< its repeat has been handed out            */

    /*
     * Reassembly.  A CAN (Controller Area Network) frame carries four
     * registers, so a reply to a 13-register read arrives as four messages,
     * each with its own offset and count.  There is no sequence to follow
     * and no continuation to wait for: the window asked for is known here,
     * and a bit is set as each part of it lands.
     */
    uint16_t acc[LINK_MAX_REGS];
    uint32_t acc_seen;   /**< bit per register within the request's window */

    /**
     * @brief The far end keeps a read's rest for LINK_OP_READ_HELD (4.35,
     *        LINK_MINOR_READ_HELD).
     *
     * Set by the caller from the far end's IDENTITY; false after
     * link_host_init(), and link_read_split() then reads a wide window in one
     * exchange.
     */
    bool     held_reads;

    /**
     * @brief The far end answers a repeat from its copy (4.37,
     *        LINK_MINOR_REPEAT).
     *
     * Set by the caller from the far end's IDENTITY and cleared when the
     * link goes down; false after link_host_init().  While set every request
     * carries an exchange number and link_host_retry() hands out its repeat;
     * while clear requests go unnumbered and none is repeated.
     */
    bool     numbered;
} link_host_t;

/**
 * @brief Set up the panel's link state.
 *
 * @param h The state to set up; NULL is ignored.
 * @param now_ms The current time, in ms; starts the link-quiet clock.
 */
void link_host_init(link_host_t *h, uint32_t now_ms);

/**
 * @brief Build a request.
 *
 * Returns false if the window does not fit a page or a request is already
 * outstanding: one at a time.
 *
 * @param h The panel's link state.
 * @param page Page number to read.
 * @param offset First register within the page.
 * @param count Number of registers, 1 to LINK_MAX_REGS.
 * @param now_ms The current time, in ms; starts the request's timeout.
 * @param out Receives the request to send.
 * @return true when the request was built, else false.
 */
bool link_host_read(link_host_t *h, uint8_t page, uint8_t offset,
                    uint8_t count, uint32_t now_ms, link_msg_t *out);
/**
 * @brief Build a LINK_OP_READ_HELD request: the registers the far end kept
 *        from the read of LINK_READ_REGS_MAX registers before.
 *
 * As link_host_read(); DATA answers it the same way.
 *
 * @param h The panel's link state.
 * @param page Page number to read.
 * @param offset First register kept: the first part's offset plus
 *               LINK_READ_REGS_MAX.
 * @param count Number of registers, 1 to LINK_READ_REGS_MAX.
 * @param now_ms The current time, in ms; starts the request's timeout.
 * @param out Receives the request to send.
 * @return true when the request was built, else false.
 */
bool link_host_read_held(link_host_t *h, uint8_t page, uint8_t offset,
                         uint8_t count, uint32_t now_ms, link_msg_t *out);
/**
 * @brief Build a write request.
 *
 * @param h The panel's link state.
 * @param page Page number to write.
 * @param offset First register within the page.
 * @param count Number of registers, 1 to LINK_MAX_REGS.
 * @param regs The count values to write.
 * @param now_ms The current time, in ms; starts the request's timeout.
 * @param out Receives the request to send.
 * @return true when the request was built, else false.
 */
bool link_host_write(link_host_t *h, uint8_t page, uint8_t offset,
                     uint8_t count, const uint16_t *regs, uint32_t now_ms,
                     link_msg_t *out);

/**
 * @brief Offer one received message against the outstanding request.
 *
 * True when the request is answered, which for a read may take several
 * messages: @p whole then holds the reassembled reply.  Parts that land
 * without completing it return false and are not a fault; the caller keeps
 * feeding until it gets true or the request times out.
 *
 * Anything that does not answer the question in flight is counted as a
 * mismatch and refused, an answer of another exchange number among them:
 * the late answer of a request already answered through its repeat is not
 * taken a second time, even by a request for the same window.  A late
 * reply can arrive after the panel has moved on, and accepting it would
 * attach one page's registers to another page's request.
 *
 * @param h The panel's link state.
 * @param part The received message.
 * @param now_ms The current time, in ms.
 * @param whole Receives the reassembled reply when the request is answered.
 * @return true when the request is answered, else false.
 */
bool link_host_accept(link_host_t *h, const link_msg_t *part, uint32_t now_ms,
                      link_msg_t *whole);

/**
 * @brief Time out the outstanding request, and report the link down once.
 *
 * Two clocks: a request is abandoned when it has been outstanding for
 * LINK_HOST_TIMEOUT_MS, every time; the link is reported down when nothing
 * has been answered for LINK_HOST_TIMEOUT_MS, once until the next reply.
 *
 * True if it abandoned a request or escalated; a caller waiting for a reply
 * stops waiting.
 *
 * @param h The panel's link state.
 * @param now_ms The current time, in ms.
 * @return true if it abandoned a request or escalated; a caller waiting for a
 *       reply stops waiting.
 */
bool link_host_tick(link_host_t *h, uint32_t now_ms);

/**
 * @brief The repeat of the outstanding request, once, when its answer is due.
 *
 * True, with the request as it was first built in @p out, when a numbered
 * request has been outstanding for LINK_HOST_RETRY_MS and less than
 * LINK_HOST_RETRY_LAST_MS and has not been repeated or cancelled: the caller
 * sends @p out.  The request's own timeout still runs from the first send,
 * so a repeat that is lost as well ends at LINK_HOST_TIMEOUT_MS as an
 * unrepeated one does.  A request answered after its repeat counts in
 * `retried`.  An unnumbered request, an ACTIVATE (the IOmcu restarts 20ms
 * after it, and a repeat could reach the new image) and a request whose
 * repeat link_host_retry_cancel() took back are never repeated.
 *
 * @param h The panel's link state.
 * @param now_ms The current time, in ms.
 * @param out Receives the repeat.
 * @return true when the repeat is to be sent now, else false.
 */
bool link_host_retry(link_host_t *h, uint32_t now_ms, link_msg_t *out);

/**
 * @brief Take back the outstanding request's repeat: it waits for its answer
 *        to LINK_HOST_TIMEOUT_MS as an unnumbered one does.
 *
 * For a caller whose bench stopped while it waited: a request from before
 * the stop, an ARM among them, is not sent again after it.
 *
 * @param h The panel's link state; NULL is ignored.
 */
void link_host_retry_cancel(link_host_t *h);

/**
 * @brief Give up on the outstanding request.
 *
 * For a caller that failed to transmit it: nothing is on the wire to wait
 * for, and an outstanding request refuses every later one until its timeout.
 *
 * @param h The panel's link state.
 */
void link_host_abandon(link_host_t *h);

/**
 * @brief Whether a request is outstanding.
 *
 * A caller waiting for an answer needs this rather than link_host_tick()'s
 * return, which is true for the link going quiet as well as for this
 * request's timeout: only one of the two releases the slot, and a wait that
 * ended on the other one left the host unable to send anything again.
 *
 * @param h The panel's link state.
 * @return true while a request is outstanding.
 */
bool link_host_pending(const link_host_t *h);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_HOST_H */
