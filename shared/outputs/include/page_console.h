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
 * not hold the link's silence failsafe off (LINK_DEV_SILENCE_MS, 200 ms).
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
 * The output is a buffer of PAGE_CONSOLE_OUT_MAX bytes taken in whole
 * lines; what does not fit is dropped and counted, and a line says how
 * many bytes were lost.
 *
 * Pure logic with no hardware: the caller feeds characters and takes the
 * output.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PAGE_CONSOLE_H
#define RCBENCH_PAGE_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_dev.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The longest command line, without its end. */
#define PAGE_CONSOLE_LINE_MAX 160u
/** The longest output line, with its LF. */
#define PAGE_CONSOLE_OUT_LINE 200u
/** The output buffer. */
#define PAGE_CONSOLE_OUT_MAX 2048u

typedef struct {
    const link_dev_t *dev;
    char     line[PAGE_CONSOLE_LINE_MAX + 1u];
    uint16_t len;
    uint8_t  overlong;   /**< the line ran past PAGE_CONSOLE_LINE_MAX */
    char     out[PAGE_CONSOLE_OUT_MAX];
    uint16_t head;       /**< the next byte to take */
    uint16_t used;
    uint32_t dropped;
    uint16_t staged[LINK_HT_W_STAGED];
    uint8_t  watch;      /**< a HITEC operation runs: report its end */
    /* Work space, here rather than on the caller's stack. */
    char       fmt[PAGE_CONSOLE_OUT_LINE];
    link_msg_t req;
    link_msg_t reply;
    uint16_t   regs[LINK_HT_COUNT];
    uint16_t   data[3][16];
} page_console_t;

/** An empty console on the pages of @p dev. */
void page_console_init(page_console_t *c, const link_dev_t *dev);

/** One character in.  CR or LF runs the line. */
void page_console_key(page_console_t *c, int ch);

/** Once a pass: the report of a HITEC operation that has ended. */
void page_console_poll(page_console_t *c);

/** Whole lines of output, at most @p room bytes; the bytes copied. */
size_t page_console_take(page_console_t *c, char *buf, size_t room);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PAGE_CONSOLE_H */
