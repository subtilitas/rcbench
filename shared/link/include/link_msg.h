/*
 * What crosses the link, independent of the transport: an op, a page, an
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

/** Registers in a page, and therefore in the largest transfer. */
#define LINK_MAX_REGS 32

typedef enum {
    LINK_OP_READ  = 0x01, /**< host asks for COUNT registers from PAGE:OFFS  */
    LINK_OP_WRITE = 0x02, /**< host sets them                                */
    LINK_OP_DATA  = 0x03, /**< coprocessor answers a read                    */
    LINK_OP_ACK   = 0x04, /**< coprocessor accepted a write                  */
    LINK_OP_NACK  = 0x05, /**< coprocessor refused it; regs[0] says why      */
} link_op_t;

/** Reasons a NACK (negative acknowledge) carries. */
typedef enum {
    LINK_NACK_BAD_PAGE   = 1,
    LINK_NACK_BAD_RANGE  = 2, /**< offset + count runs off the end of a page */
    LINK_NACK_READ_ONLY  = 3,
    LINK_NACK_BAD_VALUE  = 4,
    LINK_NACK_NOT_ARMED  = 5,
} link_nack_t;

typedef struct {
    uint8_t  op;     /**< a link_op_t                                       */
    uint8_t  page;
    uint8_t  offset; /**< first register within the page                    */
    uint8_t  count;  /**< registers meant, 0..LINK_MAX_REGS                 */
    uint16_t regs[LINK_MAX_REGS];
} link_msg_t;

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_MSG_H */
