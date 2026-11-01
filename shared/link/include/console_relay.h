/**
 * @file console_relay.h
 * @brief The panel's end of the CONSOLE page (link_pages_console.h): the
 *        panel's serial port to and from the IOmcu's bench console.
 *
 * Debug builds only (RCBENCH_PANEL_CONSOLE).  Two sides:
 *
 * - The control task asks console_relay_next() for this pass's exchange, at
 *   most one, makes it, and hands the answer to console_relay_done().  Nothing
 *   here waits.
 * - The console's own task feeds characters with console_relay_key() and takes
 *   lines with console_relay_take().
 *
 * The two sides share one queue in each direction, each with one writer and one
 * reader.
 *
 * Pure logic with no hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_CONSOLE_RELAY_H
#define RCBENCH_CONSOLE_RELAY_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The longest command line, as the IOmcu's console takes it. */
#define CONSOLE_RELAY_LINE_MAX 160u
/** @brief Lines waiting for the link. */
#define CONSOLE_RELAY_LINES 8u
/** @brief Output waiting for the serial port, bytes. */
#define CONSOLE_RELAY_OUT_MAX 4096u
/** @brief The look at the page with nothing known to wait, ms. */
#define CONSOLE_RELAY_POLL_MS 50u
/** @brief The look at a page that was not there, ms. */
#define CONSOLE_RELAY_PROBE_MS 5000u
/** @brief The registers of a poll: the two that say what waits. */
#define CONSOLE_RELAY_POLL_REGS 2u

/** @brief console_relay_done()'s result for an exchange nobody answered. */
#define CONSOLE_RELAY_NO_ANSWER (-1)

/** @brief One exchange of the page. */
typedef struct {
    bool     write;                  /**< a write, otherwise a read */
    uint8_t  off;                    /**< the first register */
    uint8_t  n;                      /**< how many */
    uint16_t regs[LINK_CN_IN_REGS];  /**< a write's values */
} console_relay_op_t;

/** @brief What the relay is doing. */
typedef enum {
    CONSOLE_RELAY_DOWN = 0, /**< no link */
    CONSOLE_RELAY_PROBE,    /**< a link: is the page there */
    CONSOLE_RELAY_NO_PAGE,  /**< a NACK: an IOmcu with no console */
    CONSOLE_RELAY_UP,       /**< the page answers */
} console_relay_state_t;

/** @brief A line waiting. */
typedef struct {
    char    text[CONSOLE_RELAY_LINE_MAX]; /**< the characters */
    uint8_t len;                          /**< how many */
} console_relay_line_t;

/** @brief The relay's state. */
typedef struct {
    /* The console's task writes these and the control task reads them. */
    console_relay_line_t lines[CONSOLE_RELAY_LINES]; /**< the lines' queue */
    atomic_uint lines_in;    /**< lines put, counting on */
    atomic_uint lines_out;   /**< lines taken, counting on */
    char     typing[CONSOLE_RELAY_LINE_MAX]; /**< the line so far */
    uint8_t  typed;          /**< its characters */
    bool     overlong;       /**< it ran past CONSOLE_RELAY_LINE_MAX */
    atomic_uint overlongs;   /**< lines too long, counting on */
    atomic_uint overflows;   /**< lines with the queue full, counting on */
    /* The control task writes these and the console's task reads them. */
    char     out[CONSOLE_RELAY_OUT_MAX]; /**< the output ring */
    atomic_uint out_in;      /**< bytes put, counting on */
    atomic_uint out_out;     /**< bytes taken, counting on */
    /* The control task alone. */
    console_relay_state_t state; /**< what the relay is doing */
    bool     link_up;        /**< the link as last told */
    bool     have_next;      /**< next_seq is known */
    uint16_t next_seq;       /**< the output's next byte */
    uint8_t  sent;           /**< bytes of the line at the queue's head sent */
    uint32_t polled_ms;      /**< the last poll */
    bool     polled_once;    /**< a poll has been made */
    bool     more;           /**< output waits: read a window */
    uint16_t want;           /**< the bytes of the window to read, 0 for all */
    bool     ack_owed;       /**< a window was taken: acknowledge it */
    uint16_t ack;            /**< with this */
    uint8_t  op;             /**< the exchange asked for */
    uint8_t  op_n;           /**< a read's registers, a chunk's bytes */
    unsigned overlongs_said; /**< of overlongs, those said */
    unsigned overflows_said; /**< of overflows, those said */
} console_relay_t;

/**
 * @brief An empty relay.
 * @param r the relay; zeroed first
 */
void console_relay_init(console_relay_t *r);

/**
 * @brief Control task, once a pass: the link's state.  An edge says a line with
 *        the time, and a fall ends what was on its way.
 * @param r      the relay
 * @param up     the link is up
 * @param now_ms the time
 */
void console_relay_link(console_relay_t *r, bool up, uint32_t now_ms);

/**
 * @brief Control task: this pass's exchange.
 * @param r      the relay
 * @param now_ms the time
 * @param op     takes it
 * @return false for none
 */
bool console_relay_next(console_relay_t *r, uint32_t now_ms, console_relay_op_t *op);

/**
 * @brief Control task: how the exchange ended.
 * @param r      the relay
 * @param result 0 for an answer, a link_nack_t, or CONSOLE_RELAY_NO_ANSWER
 * @param regs   a read's registers, otherwise NULL
 */
void console_relay_done(console_relay_t *r, int result, const uint16_t *regs);

/**
 * @brief The console's task: one character from the serial port.
 * @param r  the relay
 * @param ch the character; CR or LF ends the line, and a control character
 *           other than BS and DEL is ignored
 */
void console_relay_key(console_relay_t *r, int ch);

/**
 * @brief The console's task: the output's next whole line, cut at @p room bytes
 *        when it is longer.
 * @param r    the relay
 * @param buf  takes it, with its LF and no terminator
 * @param room its size
 * @return the bytes; 0 for none yet
 */
size_t console_relay_take(console_relay_t *r, char *buf, size_t room);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CONSOLE_RELAY_H */
