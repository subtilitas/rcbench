/**
 * @file
 * @brief A transmit queue for an MCP2515-type CAN (Controller Area Network)
 *        controller: replies are queued whole and leave three frames at a
 *        time through the three transmit buffers, and nothing waits.
 *
 * This file has no hardware access and no global state.  A driver
 * implements can_txq_ops_t: READ STATUS, a load of one transmit buffer, one
 * RTS (request to send) for several buffers, and an abort.  Every function
 * of one queue is called from one context.
 *
 * - can_txq_put() takes a reply whole or not at all.  A reply that does
 *   not fit adds its frames to the dropped count: a part of a reply never
 *   completes the panel's window, so sending it only spends bus time.
 * - can_txq_pump() reads the controller's status once.  While a buffer
 *   still holds a requested frame it loads nothing.  With all three empty
 *   it loads up to three queued frames, oldest first, into TXB0, TXB1 and
 *   TXB2 with priorities 3, 2 and 1, and requests them with one RTS.  The
 *   MCP2515 sends the requested buffer of the highest priority first, so
 *   the frames leave in the order they were put.
 * - A batch still requested CAN_TXQ_STUCK_US after its RTS is aborted: the
 *   queue empties, and every queued frame and every frame of the batch
 *   whose buffer was still requested at that status read adds to the
 *   dropped count; a frame of the batch that had left does not.  A frame
 *   already in transmission at the abort can still complete; it is counted
 *   as dropped all the same.
 *
 * Times are a 32-bit microsecond clock the caller passes in; every
 * comparison is a wrap-safe difference.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frames the queue holds, a batch on the wire not counted: two
 *        replies of 32 registers, 8 frames each, so a reply and its repeat
 *        fit.  16 frames of 16 bytes are 256 bytes.
 */
#define CAN_TXQ_DEPTH 16u

/** @brief Transmit buffers a batch fills at most: TXB0, TXB1 and TXB2. */
#define CAN_TXQ_BUFFERS 3u

/**
 * @brief A batch still requested this long after its RTS is aborted, in us.
 *
 * A batch of 3 extended frames of 8 bytes takes 393us to 465us of bus at
 * 1Mbit/s, so 5000us is more than 10 batches, and a tenth of the panel's
 * 50ms repeat (LINK_HOST_RETRY_MS).
 */
#define CAN_TXQ_STUCK_US 5000u

/** @brief The controller, as the queue drives it. */
typedef struct {
    /**
     * @brief READ STATUS: the TXREQ bits of the three buffers
     *        (MCP2515_STATUS_TXREQ) among others.
     */
    uint8_t (*status)(void *ctx);
    /**
     * @brief Writes frame @p f into transmit buffer @p buf (0 to 2) with
     *        priority @p txp (0 to 3) and TXREQ clear.
     */
    void (*load)(void *ctx, uint8_t buf, uint8_t txp,
                 const link_can_frame_t *f);
    /** @brief One RTS for the buffers in @p mask, bit 0 TXB0 to bit 2 TXB2. */
    void (*request)(void *ctx, uint8_t mask);
    /** @brief Clears TXREQ of all three buffers. */
    void (*abort)(void *ctx);
    void *ctx; /**< handed to each function */
} can_txq_ops_t;

/** @brief One queue. */
typedef struct {
    const can_txq_ops_t *ops;            /**< the controller             */
    link_can_frame_t frames[CAN_TXQ_DEPTH]; /**< the ring of queued frames */
    uint8_t  head;     /**< the oldest queued frame's place in the ring    */
    uint8_t  count;    /**< frames queued                                  */
    uint8_t  on_wire;  /**< frames of the batch requested last, until a
                            status shows every TXREQ clear                 */
    uint8_t  peak;     /**< the most frames queued since the last read     */
    bool     timed;    /**< since_us holds the last RTS or abort           */
    uint32_t since_us; /**< when the last RTS or abort was made, us        */
    uint32_t dropped;  /**< frames refused or aborted, modulo 2^32         */
} can_txq_t;

/**
 * @brief Empties the queue and clears every count.
 *
 * @param q    The queue.
 * @param ops  The controller; kept, not copied.
 */
void can_txq_init(can_txq_t *q, const can_txq_ops_t *ops);

/**
 * @brief Queues the frames of one reply, all of them or none.
 *
 * Refused whole, each frame adding 1 to the dropped count, when the queue
 * has fewer than @p n free places, when @p frames is NULL, or when a frame
 * has a DLC above 8 or an identifier wider than 29 bits.  No frame is sent
 * here; can_txq_pump() sends them.
 *
 * @param q       The queue.
 * @param frames  The frames, in the order they are to leave.
 * @param n       How many; 0 queues nothing and succeeds.
 * @return true when every frame is queued.
 */
bool can_txq_put(can_txq_t *q, const link_can_frame_t *frames, size_t n);

/**
 * @brief One look at the controller: the next batch loaded and requested
 *        when the last has left, or a stuck batch aborted.
 *
 * At most one status read, CAN_TXQ_BUFFERS loads and one request, or one
 * status read and one abort.  None at all with nothing queued and no batch
 * on the wire.  Never waits.
 *
 * @param q       The queue.
 * @param now_us  The clock, us.
 */
void can_txq_pump(can_txq_t *q, uint32_t now_us);

/**
 * @brief Whether frames are queued or the last batch has not been seen to
 *        leave.
 *
 * @param q  The queue.
 * @return true until a can_txq_pump() finds every TXREQ clear with nothing
 *         left to load.
 */
bool can_txq_busy(const can_txq_t *q);

/**
 * @brief Reads the most frames queued since the last read, and starts the
 *        next reading from the frames queued now.
 *
 * @param q  The queue.
 * @return The deepest queue since the last read, in frames.
 */
uint8_t can_txq_take_peak(can_txq_t *q);

#ifdef __cplusplus
}
#endif
