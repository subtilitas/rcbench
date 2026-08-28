/*
 * The page protocol over CAN.
 *
 * The link began as a byte stream over RS485 because that is what the board
 * had.  CAN removes the reason most of that framing existed: it arbitrates
 * instead of taking turns, so there is no direction line, no turnaround to
 * wait out, no baud floor set by an RC one-shot, and no window in which a
 * transceiver is still holding the bus.  Three of the things this project has
 * spent effort on stop being problems rather than getting solved.
 *
 * What survives untouched is the part that matters: `link_msg_t` -- an op, a
 * page, an offset, a count and some registers -- never knew what carried it.
 * This is how it is carried.  There was a byte transport over RS485 before
 * it, and the dispatcher above never knew the difference -- which is what let
 * that one be deleted rather than adapted.
 *
 * THREE THINGS THIS MAPPING DOES DIFFERENTLY, and why:
 *
 *   1. **The request has no payload at all.**  A read is entirely described by
 *      its identifier, so polling costs one zero-byte frame.  The identifier
 *      is arbitrated anyway; putting the question in it is free.
 *
 *   2. **Every frame is self-describing, so there is no reassembly.**  Each
 *      carries its own offset and its own count, which means a reply split
 *      across four frames is four independent messages rather than a sequence
 *      with state behind it.  A dropped frame costs one register range and not
 *      a whole transfer, and there is no timer waiting for a continuation that
 *      is never coming.
 *
 *   3. **The frame CRC is gone.**  CAN has a fifteen-bit CRC, an acknowledge
 *      slot and automatic retransmission in silicon.  Carrying another two
 *      bytes would spend a quarter of an eight-byte payload duplicating what
 *      the controller already did.  End-to-end integrity over something larger
 *      than a page -- a firmware image -- belongs to that transfer and not to
 *      the transport.
 *
 * AND ONE THING IT GAINS.  CAN arbitrates by identifier, lowest wins, so
 * priority is a property of the address rather than of a scheduler.  A write
 * to the control page outranks every telemetry read on the bus by
 * construction: it wins arbitration against traffic already in flight, on the
 * wire, with no software involved at either end.  That is a guarantee RS485
 * cannot make at any baud rate.
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
 *     21..14  page       8 bits   the page map, unchanged
 *     13..6   offset     8 bits   first register this frame carries
 *      5..0   count      6 bits   registers this frame is about, 0..32
 *
 * Exactly 29 bits, which is the whole extended identifier and not a
 * coincidence -- the fields are the ones link_msg_t already had.
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
 * Arbitration classes.
 *
 * Three, because a bus wants few enough that the ordering is arguable from
 * first principles: stopping the bench beats reading it, and reading it beats
 * moving a firmware image that has all day.
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
 * the wrong priority by whoever adds it: the control and failsafe pages are
 * what stop a bench, and everything that touches them outranks everything
 * else including its own acknowledgement.
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
 * not fit. A READ carries no registers and is always exactly one frame.
 */
size_t link_can_encode(const link_msg_t *msg, link_can_frame_t *out,
                       size_t cap);

/**
 * Turn one received frame back into a message carrying up to four registers.
 *
 * False if the identifier describes something impossible -- an unknown op, a
 * count past the end of a page, a payload that disagrees with the count. The
 * layer above accumulates these into the transfer it asked for; that is its
 * job and not the transport's, because only it knows what it asked.
 */
bool link_can_decode(const link_can_frame_t *f, link_msg_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CAN_H */
