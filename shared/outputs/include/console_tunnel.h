/**
 * @file console_tunnel.h
 * @brief The IOmcu's end of the CONSOLE page (link_pages_console.h): command
 *        lines off the link into the bench console, the console's output onto
 *        the link.
 *
 * Debug builds only (IOMCU_CONSOLE).  The console and its commands stay as
 * they are: a line from here is the characters a USB host would have typed.
 *
 * Pure logic with no hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_CONSOLE_TUNNEL_H
#define RCBENCH_CONSOLE_TUNNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages_console.h"
#include "page_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A panel that read nothing for this long is not there, ms. */
#define CONSOLE_TUNNEL_IDLE_MS 1000u

/** @brief The tunnel's state. */
typedef struct {
    page_console_t *console;                   /**< the console served */
    uint8_t  win[LINK_CN_DATA_BYTES];          /**< the window */
    uint16_t win_len;                          /**< its bytes */
    uint16_t seq;                              /**< its place in the output */
    char     line[PAGE_CONSOLE_LINE_MAX + 1u]; /**< the line coming in */
    uint16_t line_len;                         /**< its characters */
    bool     line_open;                        /**< a START came, no END yet */
    bool     line_ready;                       /**< its END came: run it */
    bool     read_seen;                        /**< a read since the last pass */
    bool     seen_once;                        /**< a read has ever come */
    uint32_t read_ms;                          /**< the last pass that saw one */
} console_tunnel_t;

/**
 * @brief A tunnel on a console.
 * @param t       the tunnel; zeroed first
 * @param console the console
 */
void console_tunnel_init(console_tunnel_t *t, page_console_t *console);

/**
 * @brief The page's read.
 * @param t   the tunnel
 * @param off the first register
 * @param n   how many
 * @param out takes them
 */
void console_tunnel_read(console_tunnel_t *t, uint8_t off, uint8_t n, uint16_t *out);

/**
 * @brief The page's write.
 * @param t   the tunnel
 * @param off the first register
 * @param n   how many
 * @param in  their values
 * @return 0, or a link_nack_t
 */
uint8_t console_tunnel_write(console_tunnel_t *t, uint8_t off, uint8_t n,
                             const uint16_t *in);

/**
 * @brief Once a pass, outside the link's handlers: runs a line that is complete.
 * @param t      the tunnel
 * @param now_ms the time
 */
void console_tunnel_pass(console_tunnel_t *t, uint32_t now_ms);

/**
 * @brief Whether a panel reads the page now.  While it does, the output leaves at
 *        the window's pace, on USB as well.
 * @param t      the tunnel
 * @param now_ms the time
 * @return true when a read came within CONSOLE_TUNNEL_IDLE_MS
 */
bool console_tunnel_active(const console_tunnel_t *t, uint32_t now_ms);

/**
 * @brief The bytes of output the window takes now.
 * @param t the tunnel
 * @return LINK_CN_DATA_BYTES with the window taken, otherwise 0
 */
size_t console_tunnel_room(const console_tunnel_t *t);

/**
 * @brief Output into the window.
 * @param t   the tunnel
 * @param buf the bytes, a window empty before them
 * @param n   how many, at most console_tunnel_room(); more are ignored
 */
void console_tunnel_put(console_tunnel_t *t, const char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_CONSOLE_TUNNEL_H */
