/**
 * @file
 * @brief The QR scanner's port (scan_port.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "scan_port.h"

#include <string.h>

void scan_port_init(scan_port_t *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    scan_line_init(&p->line);
    gm861_stop(&p->g);
}

void scan_port_start(scan_port_t *p, gm861_mode_t mode)
{
    if (p == NULL) {
        return;
    }
    scan_line_init(&p->line);
    gm861_init(&p->g, mode);
    p->on = true;
    p->baud = 0u;
    p->discard = false;
}

void scan_port_stop(scan_port_t *p)
{
    if (p == NULL) {
        return;
    }
    gm861_stop(&p->g);
    p->on = false;
    p->baud = 0u;
}

void scan_port_set_mode(scan_port_t *p, gm861_mode_t mode)
{
    if (p != NULL && p->on) {
        gm861_set_mode(&p->g, mode);
    }
}

/**
 * @brief Queue one line, the oldest dropped when full.
 * @param p    The port.
 * @param kind scan_ev_kind_t.
 * @param text The text, NUL ended, at most SCAN_LINE_MAX bytes.
 * @param now  The clock.
 */
static void push(scan_port_t *p, uint8_t kind, const char *text, uint32_t now)
{
    if (p->q_n == SCAN_PORT_QUEUE) {
        p->q_head = (uint8_t)((p->q_head + 1u) % SCAN_PORT_QUEUE);
        --p->q_n;
        ++p->drops;
    }
    scan_event_t *e = &p->q[(p->q_head + p->q_n) % SCAN_PORT_QUEUE];
    ++p->q_n;
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->at_ms = now;
    strncpy(e->text, text, SCAN_LINE_MAX);
}

/**
 * @brief One byte through the framer, and what ends with it on its way.
 * @param p   The port.
 * @param b   The byte.
 * @param now The clock.
 */
static void feed(scan_port_t *p, uint8_t b, uint32_t now)
{
    const scan_out_t o = scan_line_feed(&p->line, b, now);
    if (o == SCAN_OUT_FRAME) {
        size_t len = 0u;
        const uint8_t *f = scan_line_frame(&p->line, &len);
        gm861_answer(&p->g, f, len);
    } else if (o == SCAN_OUT_LINE || o == SCAN_OUT_LONG) {
        push(p, o == SCAN_OUT_LINE ? SCAN_EV_LINE : SCAN_EV_LONG,
             scan_line_text(&p->line), now);
    }
}

/**
 * @brief The framer back between lines with its counts kept: scan_line.h
 *        has no call that drops the open line alone.
 * @param p The port.
 */
static void drop_open(scan_port_t *p)
{
    const scan_counts_t kept = scan_line_counts(&p->line);
    scan_line_init(&p->line);
    p->line.counts = kept;
}

/**
 * @brief End the discard once SCAN_GAP_MS passed since the loss or the last
 *        byte dropped.
 * @param p   The port.
 * @param now The clock.
 */
static void quiet_check(scan_port_t *p, uint32_t now)
{
    if (p->discard && (uint32_t)(now - p->quiet_from) >= SCAN_GAP_MS) {
        p->discard = false;
    }
}

void scan_port_rx(scan_port_t *p, const uint8_t *b, size_t n, uint32_t now_ms)
{
    if (p == NULL || !p->on || b == NULL) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        quiet_check(p, now_ms);
        if (p->discard) {
            ++p->discarded;
            p->quiet_from = now_ms;
        } else {
            feed(p, b[i], now_ms);
        }
    }
}

void scan_port_flush(scan_port_t *p, uint32_t now_ms)
{
    if (p == NULL || !p->on) {
        return;
    }
    drop_open(p);
    p->discard = true;
    p->quiet_from = now_ms;
    ++p->flushes;
}

void scan_port_error(scan_port_t *p, uint32_t now_ms)
{
    if (p == NULL || !p->on) {
        return;
    }
    ++p->errors;
    scan_port_flush(p, now_ms);
}

size_t scan_port_step(scan_port_t *p, uint32_t now_ms, bool rx_high,
                      uint8_t *tx, size_t tx_max, uint32_t *reopen)
{
    if (reopen != NULL) {
        *reopen = 0u;
    }
    if (p == NULL || !p->on) {
        return 0u;
    }
    quiet_check(p, now_ms);
    (void)scan_line_idle(&p->line, now_ms);
    const size_t n = gm861_step(&p->g, now_ms, rx_high, tx, tx_max);
    const uint32_t want = gm861_baud(&p->g);
    if (want != p->baud) {
        /* The caller re-opens and flushes the UART: what is open was
         * read at the old rate. */
        drop_open(p);
        p->baud = want;
        if (reopen != NULL) {
            *reopen = want;
        }
    }
    return n;
}

bool scan_port_take(scan_port_t *p, scan_event_t *out)
{
    if (p == NULL || out == NULL || p->q_n == 0u) {
        return false;
    }
    *out = p->q[p->q_head];
    p->q_head = (uint8_t)((p->q_head + 1u) % SCAN_PORT_QUEUE);
    --p->q_n;
    return true;
}

bool scan_port_inject(scan_port_t *p, const char *text, uint32_t now_ms)
{
    if (p == NULL || text == NULL) {
        return false;
    }
    const size_t n = strlen(text);
    if (n == 0u || n > SCAN_LINE_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x20u || c > 0x7Eu) {
            return false;
        }
    }
    push(p, SCAN_EV_LINE, text, now_ms);
    return true;
}
