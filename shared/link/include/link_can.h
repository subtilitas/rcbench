/*
 * The page protocol over CAN (Controller Area Network).
 *
 * `link_msg_t` (an op, a page, an offset, a count and up to a page of
 * registers) carries no transport.  This file maps it onto CAN frames.
 *
 * Three properties of the mapping:
 *
 *   1. A request has no payload.  A read is entirely described by its
 *      identifier, so polling costs one zero-byte frame.
 *
 *   2. Every frame is self-describing, so there is no reassembly in the
 *      transport.  Each frame carries its own offset and count; a reply split
 *      across four frames is four independent messages, a dropped frame
 *      costs one register range rather than a transfer, and no timer waits
 *      for a continuation.
 *
 *   3. There is no frame CRC (cyclic redundancy check) in the payload.  CAN
 *      has a 15-bit CRC, an acknowledge slot and automatic retransmission in
 *      silicon; another two bytes would spend a quarter of an 8-byte payload
 *      on the same check.  End-to-end integrity over something larger than a
 *      page, such as a firmware image, belongs to that transfer.
 *
 * CAN arbitrates by identifier, lowest wins, so priority is a property of
 * the address: a write to the control page wins arbitration against
 * telemetry already in flight, on the wire, with no software involved at
 * either end.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LINK_CAN_H
#define RCBENCH_LINK_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The 29-bit extended identifier, most significant field first:
 *
 *     28..26  priority   3 bits   lower wins arbitration
 *     25..22  op         4 bits   a link_op_t
 *     21..14  page       8 bits   the page map
 *     13..6   offset     8 bits   first register this frame carries
 *      5..0   count      6 bits   registers this frame is about, 0..32
 *
 * Exactly 29 bits: the whole extended identifier.
 */
#define LINK_CAN_PRIO_SHIFT   26
#define LINK_CAN_OP_SHIFT     22
#define LINK_CAN_PAGE_SHIFT   14
#define LINK_CAN_OFFSET_SHIFT 6
#define LINK_CAN_COUNT_SHIFT  0

#define LINK_CAN_PRIO_MASK    0x7u
#define LINK_CAN_OP_MASK      0xFu
#define LINK_CAN_PAGE_MASK    0xFFu
#define LINK_CAN_OFFSET_MASK  0xFFu
#define LINK_CAN_COUNT_MASK   0x3Fu

/** Registers that fit one 8-byte payload. */
#define LINK_CAN_REGS_PER_FRAME 4

/** Frames a full page takes: 32 registers, four to a frame. */
#define LINK_CAN_MAX_FRAMES (LINK_MAX_REGS / LINK_CAN_REGS_PER_FRAME)

/**
 * Arbitration classes.  Three: stopping the bench beats reading it, and
 * reading it beats moving a firmware image.
 */
typedef enum {
    LINK_CAN_PRIO_CONTROL = 0, /**< anything that changes what the bench does */
    LINK_CAN_PRIO_NORMAL  = 1, /**< telemetry, status, identity               */
    LINK_CAN_PRIO_BULK    = 2, /**< transfers that must never delay a stop    */
} link_can_prio_t;

typedef struct {
    uint32_t id;      /**< 29-bit extended identifier */
    uint8_t  dlc;     /**< payload bytes, 0..8 */
    uint8_t  data[8];
} link_can_frame_t;

/**
 * The arbitration class a page and op belong to.
 *
 * Derived rather than chosen per call site, so a new page cannot be added at
 * the wrong priority.  The control, limits and failsafe pages stop a bench,
 * and everything that touches them outranks everything else, their own
 * acknowledgements included.
 */
link_can_prio_t link_can_priority(uint8_t page, uint8_t op);

uint32_t link_can_id(link_can_prio_t prio, uint8_t op, uint8_t page,
                     uint8_t offset, uint8_t count);

/** Split an identifier back into its fields. Any pointer may be NULL. */
void link_can_id_split(uint32_t id, link_can_prio_t *prio, uint8_t *op,
                       uint8_t *page, uint8_t *offset, uint8_t *count);

/**
 * Render a message as one or more CAN frames.
 *
 * Returns how many were written, or 0 if the message is malformed or would
 * not fit.  A READ carries no registers and is always one frame.
 */
size_t link_can_encode(const link_msg_t *msg, link_can_frame_t *out,
                       size_t cap);

/**
 * Turn one received frame back into a message carrying up to four registers.
 *
 * False if the identifier describes something impossible: an unknown op, a
 * count past the end of a page, a payload that disagrees with the count.
 * The layer above accumulates the parts into the transfer it asked for,
 * because only it knows what it asked.
 */
bool link_can_decode(const link_can_frame_t *f, link_msg_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CAN_H */
