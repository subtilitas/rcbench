/**
 * @file page_console.h
 * @brief A bench console: link pages read and written from command lines,
 *        through the page handlers the link uses.
 *
 * A bench tool, not part of a released image: the IOmcu builds it only
 * with the build option IOMCU_CONSOLE (firmware/iomcu/CMakeLists.txt) and
 * reads its lines from the USB (Universal Serial Bus) serial port.
 *
 * Every request goes through link_dev_apply(): the page table, the range
 * checks, the handlers and their refusals are the link's.  Whatever a page
 * refuses to the panel it refuses here: a write of ARM while the arm latch
 * is set, a HITEC or KST transaction without the write key, a supply ON
 * without a heartbeat.  A request from here is not link traffic: it does
 * not hold the link's silence failsafe off (LINK_DEV_SILENCE_MS, 200ms).
 * Every page that needs a trusted heartbeat and a link that is not silent
 * needs a running panel beside the console.
 *
 * Lines end with CR or LF; tokens are separated by spaces; numbers are
 * decimal or 0x hexadecimal.  Commands:
 *
 *   help                          the commands
 *   pages                         each page: number, registers, rw or ro
 *   r <page> <off> [n]            read n registers (1 to 32, default 1)
 *   w <page> <off> <v> [v ...]    write up to 32 registers
 *   hitec [state]                 the HITEC page, decoded
 *   hitec open <port> c|d [od|pp] [pu|pre]
 *                                 OPEN: classic or D series, open drain
 *                                 (default) or push-pull; for the D series
 *                                 pu or pre writes the receive mode,
 *                                 PULL-UP or PRECHARGE, first (register
 *                                 23), and without either the mode stays
 *                                 as the page holds it
 *   hitec close|abort|clear <port>
 *   hitec send <port> [lead=<us>] [gap=<bits>] [window=<us>] [rx=<n>]
 *                     [dec=<dec>] <byte> [<byte> ...]
 *   hitec listen <port> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]
 *   hitec level <port> 0|1 <us>
 *   hitec power <port> mv=<mV> ma=<mA> off=<ms> window=<us> rx=<n>
 *                      [answer=<byte>] [dec=<dec>]
 *   hitec c <port> <cmd> <p1> <p2>          a classic command and its reply
 *   hitec d <port> read <id> <addr>         a D series read and its answer
 *   hitec d <port> write <id> <addr> <value>
 *
 * `more` as the last token of send, listen, level, power, c or d sets
 * LINK_HT_W_CMD_MORE in that command: more steps of this write follow.
 * The writer puts it on every step of a write to the servo's memory but
 * the last; while it stands the SUPPLY page holds the panel's OFF back
 * from the servo's supply, 10000ms after the OFF at most
 * (SUPPLY_PAGE_WRITE_WAIT_MS).
 *
 * <dec> is none, c:<cmd>, d:<addr> or l:<byte> (link_hitec_dec_t and its
 * argument).  The hitec commands stage the HITEC page's registers, name
 * their CRC (cyclic redundancy check) and the next SEQ, and put
 * LINK_HITEC_WRITE_KEY in every command that puts a byte on the wire.  A
 * command that starts something is followed by its report when the port
 * has finished it: `hitec done`, `hitec cap` and one `hitec byte` line per
 * byte received.  A raw `w` of the HITEC page gets none of this.
 *
 * Replies, one line each: `data`, `ack`, `nack`, `err`, `page`, `hitec`.
 *
 * The output is a buffer of PAGE_CONSOLE_OUT_MAX bytes of whole lines;
 * what does not fit is dropped and counted, and a line says how many bytes
 * were lost.  It is taken in whole lines, or with page_console_take_part()
 * as a stream that cuts a line where the caller's room ends.  Lines from
 * outside the console (page_console_say()) go into the same buffer, so
 * they leave in the order they were said and never inside another line.
 *
 * Pure logic with no hardware: the caller feeds characters and takes the
 * output.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PAGE_CONSOLE_H
#define RCBENCH_PAGE_CONSOLE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_dev.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The longest command line, without its end. */
#define PAGE_CONSOLE_LINE_MAX 160u
/** @brief The longest output line, with its LF. */
#define PAGE_CONSOLE_OUT_LINE 200u
/** @brief The output buffer. */
#define PAGE_CONSOLE_OUT_MAX 2048u

/**
 * @brief The console state: the command line being read, the output buffer,
 *        the staged HITEC registers and the work space of the requests.
 */
/**
 * @brief A build's own console command: a line none of the commands here took.
 * @param ctx the context given to page_console_set_ext()
 * @param tok the line's tokens
 * @param n   how many, 1 or more
 * @return true when the line was its own; false gives `err unknown command`
 */
typedef bool (*page_console_ext_fn)(void *ctx, char **tok, unsigned n);

/** @brief One console: its line, its output and its work space. */
typedef struct {
    const link_dev_t *dev;
    page_console_ext_fn ext; /**< a build's own commands, or NULL */
    void    *ext_ctx;        /**< and its context */
    char     line[PAGE_CONSOLE_LINE_MAX + 1u]; /**< the line so far */
    uint16_t len;
    uint8_t  overlong;   /**< the line ran past PAGE_CONSOLE_LINE_MAX */
    char     out[PAGE_CONSOLE_OUT_MAX];  /**< the output ring */
    uint16_t head;       /**< the next byte to take */
    uint16_t used;                      /**< bytes in the ring */
    uint32_t dropped;                   /**< bytes dropped from the output ring */
    uint16_t staged[LINK_HT_W_STAGED];  /**< the registers staged for the HITEC page */
    uint8_t  watch;      /**< a HITEC operation runs: report its end */
    uint8_t  more;       /**< the line's step carries LINK_HT_W_CMD_MORE */
    /* Work space, here rather than on the caller's stack. */
    char       fmt[PAGE_CONSOLE_OUT_LINE];
    link_msg_t req;
    link_msg_t reply;
    uint16_t   regs[LINK_HT_COUNT];
    uint16_t   data[3][16];
} page_console_t;

/**
 * @brief An empty console on the pages of @p dev.
 *
 * @param c   The console; zeroed first. A NULL c is ignored.
 * @param dev The link device whose pages the console reads and writes.
 */
void page_console_init(page_console_t *c, const link_dev_t *dev);

/**
 * @brief Hand lines no command here takes to @p fn.
 * @param c   the console
 * @param fn  the handler, NULL for none
 * @param ctx its context
 */
void page_console_set_ext(page_console_t *c, page_console_ext_fn fn, void *ctx);

/**
 * @brief One character in.
 *
 * CR or LF runs the line.
 *
 * @param c  The console.
 * @param ch The character; a value below 0 is ignored, and so is a control
 *           character other than CR, LF, BS and DEL.
 */
void page_console_key(page_console_t *c, int ch);

/**
 * @brief Once a pass: the report of a HITEC operation that has ended.
 *
 * @param c The console.
 */
void page_console_poll(page_console_t *c);

/**
 * @brief One line of output from outside the console, formatted as printf()
 *        formats: the LF is added, and one the text ends with is not doubled.
 *
 * At most PAGE_CONSOLE_OUT_LINE bytes with the LF; a longer text is cut.
 *
 * @param c   The console.
 * @param fmt The printf format.
 * @param ... The arguments of @p fmt.
 */
void page_console_say(page_console_t *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief page_console_say() with a va_list.
 *
 * @param c   The console.
 * @param fmt The printf format.
 * @param ap  The argument list, started by the caller.
 */
void page_console_vsay(page_console_t *c, const char *fmt, va_list ap)
    __attribute__((format(printf, 2, 0)));

/**
 * @brief Whole lines of output, at most @p room bytes; the bytes copied.
 *
 * A line longer than @p room does not leave: a caller whose room can be smaller
 * than PAGE_CONSOLE_OUT_LINE takes with page_console_take_part().
 *
 * @param c    The console.
 * @param buf  Receives the bytes.
 * @param room Size of buf in bytes.
 * @return The number of bytes copied, whole lines only; 0 for a NULL c or buf.
 */
size_t page_console_take(page_console_t *c, char *buf, size_t room);

/**
 * @brief The first @p room bytes of output, a line cut where the room ends: a
 *        line longer than @p room leaves in pieces, and the rest of it comes
 *        first in the next take.
 *
 * Any @p room from 1 byte on moves the output.  A caller that takes in parts
 * keeps to this function: page_console_take() leaves a part-taken line where it
 * is until the rest of it fits.
 *
 * @param c    The console.
 * @param buf  Receives the bytes.
 * @param room Size of buf in bytes.
 * @return The number of bytes copied; 0 for a NULL c or buf.
 */
size_t page_console_take_part(page_console_t *c, char *buf, size_t room);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PAGE_CONSOLE_H */
