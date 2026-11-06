/**
 * @file
 * @brief What crosses the link, independent of the transport: an op, a page, an
 * offset, a count and up to a page of registers.
 *
 * The dispatcher and the poller work on this struct; link_can.h maps it onto
 * CAN (Controller Area Network) frames.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LINK_MSG_H
#define RCBENCH_LINK_MSG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Registers in a page, and therefore in the largest transfer. */
#define LINK_MAX_REGS 32

/**
 * @brief The operation a link message carries.
 */
typedef enum {
    LINK_OP_READ  = 0x01, /**< host asks for COUNT registers from PAGE:OFFS  */
    LINK_OP_WRITE = 0x02, /**< host sets them                                */
    LINK_OP_DATA  = 0x03, /**< IOmcu answers a read                    */
    LINK_OP_ACK   = 0x04, /**< IOmcu accepted a write                  */
    LINK_OP_NACK  = 0x05, /**< IOmcu refused it; regs[0] says why      */
    /**
     * @brief Host asks for COUNT registers from PAGE:OFFS as the IOmcu kept
     *        them at the read of LINK_READ_REGS_MAX registers before.
     *
     * The second part of a split read, from 4.35 (LINK_MINOR_READ_HELD).
     * Answered with DATA, or with a NACK of LINK_NACK_BUSY when nothing is
     * kept for PAGE:OFFS.  An older IOmcu drops the frame unanswered, so a
     * panel sends it only to a far end of 4.35 or later.
     */
    LINK_OP_READ_HELD = 0x06,
} link_op_t;

/**
 * @brief The most registers one read asks for in a split read: a reply of 4
 *        frames.
 *
 * docs/Link.md gives the reason: 4 frames of 13 bytes fit the 64-byte receive
 * FIFO (first in, first out buffer) the panel's TWAI controller is taken to
 * have.  An IOmcu of 4.35 or later answers a read of exactly this many
 * registers and keeps the up to LINK_READ_REGS_MAX registers after them, read
 * in the same call, for one LINK_OP_READ_HELD.
 */
#define LINK_READ_REGS_MAX 16u

/** @brief Reasons a NACK (negative acknowledge) carries. */
typedef enum {
    LINK_NACK_BAD_PAGE   = 1,
    LINK_NACK_BAD_RANGE  = 2, /**< offset + count runs off the end of a page */
    LINK_NACK_READ_ONLY  = 3,
    LINK_NACK_BAD_VALUE  = 4,
    LINK_NACK_NOT_ARMED  = 5,
    /**
     * @brief The value is good and something else holds what it names: a pin
     * another page or an output has, or a resource the wiring needs.
     *
     * Freed, the same write is taken.  TONE only, since 4.17
     * (LINK_MINOR_TONE_BUSY); an older IOmcu says BAD_VALUE.
     */
    LINK_NACK_BUSY       = 6,
} link_nack_t;

/**
 * @brief The highest exchange number: 1 to LINK_SEQ_MAX number exchanges,
 *        0 is a message without one.
 *
 * A panel numbers its requests from 4.37 (LINK_MINOR_REPEAT) on an IOmcu
 * that reports 4.37 or later, and a repeat of a request carries the number
 * of the request it repeats.  The IOmcu answers a repeat from its copy of
 * the first answer and does not act on it again (link_dev_dispatch()); a
 * reply carries the number of the request it answers.
 */
#define LINK_SEQ_MAX 7u

/**
 * @brief One link message: the op, the register window and its values.
 */
typedef struct {
    uint8_t  op;     /**< a link_op_t                                       */
    uint8_t  page;                      /**< the page the message is for */
    uint8_t  offset; /**< first register within the page                    */
    uint8_t  count;  /**< registers meant, 0..LINK_MAX_REGS                 */
    uint16_t regs[LINK_MAX_REGS];       /**< the values, count of them */
    /** @brief The exchange number, 1 to LINK_SEQ_MAX; 0 none. */
    uint8_t  seq;
} link_msg_t;

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_MSG_H */
