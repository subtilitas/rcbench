/**
 * @file
 * @brief The IOmcu's end of the CONSOLE page; see console_tunnel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "console_tunnel.h"

#include <string.h>

#include "link_msg.h"

void console_tunnel_init(console_tunnel_t *t, page_console_t *console)
{
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
        t->console = console;
    }
}

void console_tunnel_read(console_tunnel_t *t, uint8_t off, uint8_t n, uint16_t *out)
{
    const uint16_t more = t->console->used != 0u ? (uint16_t)LINK_CN_OUT_MORE : 0u;

    t->read_seen = true;
    for (unsigned k = 0u; k < n; ++k) {
        const unsigned reg = (unsigned)off + k;

        if (reg == LINK_CN_OUT_SEQ) {
            out[k] = t->seq;
        } else if (reg == LINK_CN_OUT_LEN) {
            out[k] = (uint16_t)(t->win_len | more);
        } else {
            const unsigned at = (reg - LINK_CN_DATA) * 2u;

            out[k] = (uint16_t)(t->win[at] | ((uint16_t)t->win[at + 1u] << 8));
        }
    }
}

/**
 * @brief A chunk of a command line.
 * @param t  the tunnel
 * @param n  its registers
 * @param in its registers' values
 * @return 0, or a link_nack_t
 */
static uint8_t take_chunk(console_tunnel_t *t, uint8_t n, const uint16_t *in)
{
    const unsigned len = in[0] & LINK_CN_IN_LEN_MASK;

    if (len > LINK_CN_IN_BYTES || len > ((unsigned)n - 1u) * 2u) {
        return LINK_NACK_BAD_VALUE;
    }
    if ((in[0] & LINK_CN_IN_START) != 0u) {
        if (t->line_ready) {
            return LINK_NACK_BUSY;
        }
        t->line_open = true;
        t->line_len = 0u;
    }
    if (!t->line_open) {
        return LINK_NACK_BAD_VALUE;
    }
    if ((size_t)t->line_len + len > PAGE_CONSOLE_LINE_MAX) {
        t->line_open = false;
        return LINK_NACK_BAD_RANGE;
    }
    for (unsigned k = 0u; k < len; ++k) {
        t->line[t->line_len++] = (char)(in[1u + k / 2u] >> (8u * (k & 1u)));
    }
    if ((in[0] & LINK_CN_IN_END) != 0u) {
        t->line_open = false;
        t->line_ready = true;
    }
    return 0u;
}

uint8_t console_tunnel_write(console_tunnel_t *t, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    if (off == LINK_CN_W_ACK && n == 1u) {
        if (t->win_len != 0u && in[0] == (uint16_t)(t->seq + t->win_len)) {
            t->seq = in[0];
            t->win_len = 0u;
        }
        return 0u;
    }
    if (off == LINK_CN_W_IN && n >= 1u && n <= LINK_CN_IN_REGS) {
        return take_chunk(t, n, in);
    }
    return LINK_NACK_BAD_RANGE;
}

void console_tunnel_pass(console_tunnel_t *t, uint32_t now_ms)
{
    if (t->read_seen) {
        t->read_seen = false;
        t->seen_once = true;
        t->read_ms = now_ms;
    }
    if (t->line_ready) {
        /* The whole line at once, so a USB host's typing lands before or after it. */
        for (unsigned k = 0u; k < t->line_len; ++k) {
            page_console_key(t->console, (unsigned char)t->line[k]);
        }
        page_console_key(t->console, '\n');
        t->line_ready = false;
    }
}

bool console_tunnel_active(const console_tunnel_t *t, uint32_t now_ms)
{
    return t->seen_once && (uint32_t)(now_ms - t->read_ms) < CONSOLE_TUNNEL_IDLE_MS;
}

size_t console_tunnel_room(const console_tunnel_t *t)
{
    return t->win_len == 0u ? LINK_CN_DATA_BYTES : 0u;
}

void console_tunnel_put(console_tunnel_t *t, const char *buf, size_t n)
{
    if (t->win_len != 0u || n > LINK_CN_DATA_BYTES) {
        return;
    }
    memcpy(t->win, buf, n);
    t->win_len = (uint16_t)n;
}
