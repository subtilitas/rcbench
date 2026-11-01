/**
 * @file link_pages_console.h
 * @brief The CONSOLE page: the bench console's bytes over the link, in debug
 *        builds only.
 *
 * An IOmcu built with IOMCU_CONSOLE has this page; every other IOmcu answers
 * LINK_NACK_BAD_PAGE.  No released image has it, and no capability bit or
 * protocol minor names it: a panel built with RCBENCH_PANEL_CONSOLE reads it
 * and takes the NACK as the answer that there is no console.
 *
 * Read, any window from register 0:
 *
 *   0 LINK_CN_OUT_SEQ  where the window's first byte stands in the output, a
 *                      16-bit count of bytes from the IOmcu's start
 *   1 LINK_CN_OUT_LEN  bytes in the window, 0 to LINK_CN_DATA_BYTES, with
 *                      LINK_CN_OUT_MORE when more wait behind them
 *   2 LINK_CN_DATA     the bytes, low byte of a register first
 *
 * The window stays until it is acknowledged, so a reply lost on the wire costs
 * a second read and no byte.
 *
 * Write:
 *
 *   0 LINK_CN_W_ACK    SEQ + LEN of a window taken; another value is ignored
 *   1 LINK_CN_W_IN     a chunk of a command line: LINK_CN_IN_START,
 *                      LINK_CN_IN_END and the count of bytes, 0 to
 *                      LINK_CN_IN_BYTES, then the bytes.  A line runs when its
 *                      END arrives.  A chunk with no START before it is
 *                      refused LINK_NACK_BAD_VALUE: that is what a restart in
 *                      the middle of a line looks like.  A line not yet run
 *                      refuses the next START with LINK_NACK_BUSY.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_PAGES_CONSOLE_H
#define RCBENCH_LINK_PAGES_CONSOLE_H

#include <stdint.h>

/** @brief The page's number. */
#define LINK_PAGE_CONSOLE 0xE0u

/** @brief The registers of a read. */
enum {
    LINK_CN_OUT_SEQ = 0, /**< the window's place in the output */
    LINK_CN_OUT_LEN = 1, /**< its bytes, and LINK_CN_OUT_MORE  */
    LINK_CN_DATA    = 2, /**< the bytes                        */
    LINK_CN_COUNT   = 32,
};

/** @brief The registers of a write. */
enum {
    LINK_CN_W_ACK = 0, /**< the output taken up to here */
    LINK_CN_W_IN  = 1, /**< a chunk of a line           */
};

/** @brief Bytes in a window: 30 registers. */
#define LINK_CN_DATA_BYTES 60u
/** @brief In LINK_CN_OUT_LEN: more bytes wait behind the window. */
#define LINK_CN_OUT_MORE 0x8000u
/** @brief LINK_CN_OUT_LEN's count. */
#define LINK_CN_OUT_LEN_MASK 0x00FFu
/** @brief In a chunk's first register: the line's first chunk. */
#define LINK_CN_IN_START 0x0100u
/** @brief In a chunk's first register: the line's last chunk. */
#define LINK_CN_IN_END 0x0200u
/** @brief A chunk's count of bytes. */
#define LINK_CN_IN_LEN_MASK 0x00FFu
/** @brief Bytes in a chunk: the three registers a frame has after the first. */
#define LINK_CN_IN_BYTES 6u
/** @brief Registers of a chunk: one CAN frame. */
#define LINK_CN_IN_REGS 4u

#endif /* RCBENCH_LINK_PAGES_CONSOLE_H */
