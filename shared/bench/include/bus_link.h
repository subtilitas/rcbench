/*
 * The panel's half of the BUS (0x34) link page, protocol 4.23: a serial
 * bus servo protocol on a programming port (shared/outputs/bus_port.h).
 *
 * The session with the servos runs at the coprocessor.  The panel sends one
 * command at a time and reads back where the bus is, how the operation
 * ended, the IDs a scan found, the bytes a read gave and the live block of
 * the servo it watches.  A coprocessor whose protocol minor is below
 * BUS_LINK_MINOR has no page, and nothing is sent to it.
 *
 * One command at a time, bus_link_send():
 *
 *   1. for a WRITE and an EEPROM write the staged registers 4 to 13 are
 *      written, 3 frames of 4, 4 and 2 registers;
 *   2. the command, registers 0 to 3, one frame, with SEQ one above the
 *      page's.  A command without an answer is sent again as it was, up to
 *      BUS_LINK_RETRIES times; the page takes it once.  A command refused
 *      with LINK_NACK_BAD_VALUE is sent once more after a read of the page,
 *      with its SEQ: the page may have counted on;
 *   3. the page is read at once: the readout's answer names the command's
 *      id and the SEQ it went with.  The page's LINK_BS_SEQ at that SEQ says
 *      the status that follows is the command's.
 *
 * bus_link_stop() is for STOP: a command whose frame has not gone is
 * dropped, and the command STOP goes to a bus the page last read open or
 * opening.
 *
 * The page is read every BUS_LINK_POLL_MS while the bus is not closed or an
 * operation runs, and every BUS_LINK_IDLE_MS otherwise.  The IDs a scan
 * found, on the BUS_IDS page (0x35), are read whenever a read of the page shows a
 * scan with another count found, or a scan done that was not read.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The protocol minor that has the BUS page. */
#define BUS_LINK_MINOR LINK_MINOR_BUS

/** The page is read this long after the last read while the bus is not
 *  closed or an operation runs. */
#define BUS_LINK_POLL_MS 50u
/** And this long after it while the bus is closed. */
#define BUS_LINK_IDLE_MS 500u
/** Exchanges without an answer, in a row, after which a command is given
 *  up. */
#define BUS_LINK_RETRIES 3u

#define BUS_LINK_NO_ANSWER (-1)
#define BUS_LINK_ACK       0

/** The registers of one read of the page's status. */
#define BUS_LINK_STATUS_N ((unsigned)LINK_BS_STATUS_REGS)
/** And of the IDs a scan found. */
#define BUS_LINK_IDS_N ((unsigned)LINK_BI_COUNT)

/** A command as the screen asks for it. */
typedef struct {
    link_bus_op_t op;
    uint8_t  port;             /**< the programming port                */
    uint8_t  proto;            /**< LINK_BUS_PROTO_*                    */
    bool     sts;              /**< the series bit of the command       */
    uint16_t arg;              /**< LINK_BS_W_ARG                       */
    uint16_t aux;              /**< READ: the bytes                     */
    uint8_t  n;                /**< WRITE, EEPROM: the bytes, 1 to 16   */
    uint8_t  data[LINK_BS_BYTES];
    bool     key;              /**< EEPROM: the warning's hold was held */
    uint32_t id;               /**< the caller's number of the command  */
} bus_link_cmd_t;

/** What became of the last command. */
typedef enum {
    BUS_LINK_ANS_NONE = 0,
    BUS_LINK_ANS_TAKEN,        /**< the page acknowledged it            */
    BUS_LINK_ANS_NACK,         /**< the page refused a frame of it      */
    BUS_LINK_ANS_NO_ANSWER,    /**< BUS_LINK_RETRIES exchanges unanswered */
    BUS_LINK_ANS_DROPPED,      /**< bus_link_stop() before it went      */
    BUS_LINK_ANS_DOWN,         /**< the link went, or there is no page  */
} bus_link_ans_t;

/** The exchange the caller makes. */
typedef struct {
    bool     write;
    uint8_t  page;
    uint8_t  off, n;
    uint16_t regs[4];
} bus_link_xfer_t;

/** The screen's copy: a plain value the control task fills. */
typedef struct {
    bool     up;               /**< a coprocessor answers               */
    bool     page;             /**< and has the page                    */
    bool     known;            /**< status was read since it came up    */
    bool     sending;          /**< a command is on its way             */
    uint16_t status[BUS_LINK_STATUS_N];
    bool     ids_known;
    uint16_t ids[BUS_LINK_IDS_N];
    uint32_t ans_id;           /**< the command the answer is for       */
    uint8_t  ans;              /**< bus_link_ans_t                      */
    uint8_t  ans_nack;         /**< BUS_LINK_ANS_NACK: link_nack_t      */
    uint16_t ans_seq;          /**< BUS_LINK_ANS_TAKEN: the SEQ it went
                                    with                                */
    uint32_t answers;          /**< answers given, wrapping             */
} bus_link_readout_t;

typedef struct {
    bool     up;
    bool     page;
    uint16_t minor;
    bool     known;
    uint8_t  pending;          /**< the exchange in flight              */
    uint8_t  tries;
    uint16_t status[BUS_LINK_STATUS_N];
    bool     poll_now;
    bool     polled;
    uint32_t status_ms;
    bool     ids_known;
    bool     ids_due;
    uint16_t ids_seq;          /**< LINK_BS_SEQ the IDs were read at    */
    uint8_t  ids_found;        /**< the count found they were read at   */
    uint16_t ids[BUS_LINK_IDS_N];

    bool     active;
    bus_link_cmd_t cmd;
    uint8_t  stage_at;         /**< staged windows written              */
    uint16_t staged[LINK_BS_W_STAGED];
    bool     framed;           /**< frame holds the command as it goes  */
    bool     sent;             /**< the frame went at least once        */
    bool     reseq;            /**< sent again after a BAD_VALUE        */
    uint16_t frame[LINK_BS_W_CMD_FRAME];
    bool     stop_due;

    uint32_t ans_id;
    uint8_t  ans;
    uint8_t  ans_nack;
    uint16_t ans_seq;
    uint32_t answers;
} bus_link_t;

void bus_link_init(bus_link_t *k);

/** A coprocessor answered with protocol minor @p minor. */
void bus_link_came_up(bus_link_t *k, uint16_t minor);

/** The link went: a command on its way ends as BUS_LINK_ANS_DOWN. */
void bus_link_lost(bus_link_t *k);

bool bus_link_has_page(const bus_link_t *k);

/** Send @p c.  False, with nothing sent, while no page answers or a
 *  command is on its way, and for an operation of none or beyond the
 *  last. */
bool bus_link_send(bus_link_t *k, const bus_link_cmd_t *c);

/** STOP: see the file's comment. */
void bus_link_stop(bus_link_t *k);

/** The next exchange owed at @p now_ms; false for none. */
bool bus_link_next(bus_link_t *k, uint32_t now_ms, bus_link_xfer_t *x);

/** How it went: BUS_LINK_ACK with the registers read (NULL for a
 *  write), a NACK reason, or BUS_LINK_NO_ANSWER. */
void bus_link_done(bus_link_t *k, int result, const uint16_t *regs);

void bus_link_readout(const bus_link_t *k, bus_link_readout_t *r);

/** Whether the bus holds a port by the last read, or a command that may
 *  open it is on its way: the bench takes no arm then. */
bool bus_link_holds(const bus_link_t *k);

#ifdef __cplusplus
}
#endif
