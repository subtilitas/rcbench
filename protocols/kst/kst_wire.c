/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_wire.h"

#include <string.h>

/* --- frames --------------------------------------------------------------- */

/* The 10 bits after the start bit: `10 00000000` reads, `00 10000000`
 * writes.  No other prefix is known. */
#define PREFIX_READ  0x200u
#define PREFIX_WRITE 0x080u

static void put_level(kst_frame_t *f, bool high)
{
    if (high) {
        f->level[f->n_half / 8u] |= (uint8_t)(0x80u >> (f->n_half % 8u));
    }
    f->n_half++;
}

/*
 * 27 bits, most significant first, as 54 half-cells.  The first half-cell is
 * the low half of the start bit and is left out; a trailing low is cut, so
 * the sequence ends on the frame's last high.
 */
static void build(uint32_t word, kst_frame_kind_t kind, kst_frame_t *out)
{
    uint8_t last_high = 0;

    memset(out, 0, sizeof(*out));
    out->kind = (uint8_t)kind;
    for (unsigned i = 1; i < 2u * KST_FRAME_BITS; ++i) {
        const bool bit = ((word >> (KST_FRAME_BITS - 1u - i / 2u)) & 1u) != 0u;
        const bool second_half = (i % 2u) != 0u;

        put_level(out, bit == second_half);
        if (bit == second_half) {
            last_high = out->n_half;
        }
    }
    out->n_half = last_high;
}

static uint32_t frame_word(uint32_t prefix, uint8_t reg, uint8_t data)
{
    return ((uint32_t)1 << 26) | (prefix << 16) | ((uint32_t)reg << 8) | data;
}

bool kst_frame_read(uint8_t reg, kst_frame_t *out)
{
    if (out == NULL || reg > KST_REG_MAX) {
        return false;
    }
    build(frame_word(PREFIX_READ, reg, 0), KST_FRAME_READ, out);
    return true;
}

bool kst_frame_write(uint8_t reg, uint8_t value, kst_frame_t *out)
{
    if (out == NULL || reg == 0u || reg > KST_REG_MAX) {
        return false;
    }
    build(frame_word(PREFIX_WRITE, reg, value), KST_FRAME_WRITE, out);
    return true;
}

void kst_frame_sync(kst_frame_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->kind = (uint8_t)KST_FRAME_SYNC;
    for (unsigned i = 0; i < 2u * KST_SYNC_PULSES - 1u; ++i) {
        put_level(out, (i % 2u) == 0u);
    }
}

bool kst_frame_level(const kst_frame_t *frame, unsigned index)
{
    if (frame == NULL || index >= frame->n_half) {
        return false;
    }
    return (frame->level[index / 8u] & (0x80u >> (index % 8u))) != 0u;
}

uint32_t kst_frame_duration_ns(const kst_frame_t *frame)
{
    return frame == NULL ? 0u : (uint32_t)frame->n_half * KST_HALF_CELL_NS;
}

/* --- reply decoder -------------------------------------------------------- */

uint32_t kst_reply_window_ns(kst_expect_t expect)
{
    return expect == KST_EXPECT_WRITE ? KST_WRITE_WINDOW_NS
                                      : KST_READ_WINDOW_NS;
}

/*
 * The glitch rule: a level change counts once the new level has lasted
 * KST_GLITCH_NS.  On timestamps that is a pair of edges closer than that,
 * and both go.  An edge that follows a removed pair is compared with the
 * edge before the pair, so a burst of chatter ends on its last edge.
 */
static unsigned filter(const kst_capture_t *cap, uint32_t *e, uint8_t *removed)
{
    unsigned n = 0;
    unsigned pairs = 0;

    for (unsigned i = 0; i < cap->n_edges; ++i) {
        if (n > 0u && cap->edge_ns[i] - e[n - 1u] < KST_GLITCH_NS) {
            n--;
            pairs++;
        } else {
            e[n++] = cap->edge_ns[i];
        }
    }
    *removed = (uint8_t)(pairs > 255u ? 255u : pairs);
    return n;
}

/* Edges at or before @p t.  The line starts low, so an odd count is high. */
static unsigned edges_upto(const uint32_t *e, unsigned n, uint32_t t)
{
    unsigned count = 0;

    while (count < n && e[count] <= t) {
        count++;
    }
    return count;
}

/* The two halves of the cell whose mid-cell edge is at @p t differ. */
static bool halves_differ(const uint32_t *e, unsigned n, uint32_t t)
{
    const unsigned before = edges_upto(e, n, t - KST_RX_CONFIRM_NS);
    const unsigned after = edges_upto(e, n, t + KST_RX_CONFIRM_NS);

    return ((after - before) % 2u) != 0u;
}

/*
 * Index of the edge after @p last nearest to @p pred and within
 * KST_RX_SEARCH_NS of it; @p n when there is none.
 */
static unsigned find_mid_edge(const uint32_t *e, unsigned n, unsigned last,
                              uint32_t pred)
{
    unsigned best = n;
    uint32_t best_off = KST_RX_SEARCH_NS + 1u;

    for (unsigned j = last + 1u; j < n; ++j) {
        const uint32_t off = e[j] > pred ? e[j] - pred : pred - e[j];

        if (off < best_off) {
            best = j;
            best_off = off;
        }
    }
    return best;
}

static kst_rx_t track(const uint32_t *e, unsigned n, uint32_t window_ns,
                      kst_expect_t expect, kst_reply_t *out)
{
    unsigned idx = 0;
    unsigned bits = 0;
    uint32_t end;

    if (!halves_differ(e, n, e[0])) {
        return KST_RX_CELL;
    }
    for (unsigned k = 1; k < KST_REPLY_BITS; ++k) {
        const uint32_t pred = e[idx] + KST_SERVO_CELL_NS;
        const unsigned j = find_mid_edge(e, n, idx, pred);

        if (j == n) {
            /* Nothing at or after the search window: the reply stopped.
             * Edges there, none of them mid-cell: a cell is broken. */
            return edges_upto(e, n, pred - KST_RX_SEARCH_NS - 1u) == n
                       ? KST_RX_LENGTH : KST_RX_CELL;
        }
        if (!halves_differ(e, n, e[j])) {
            return KST_RX_CELL;
        }
        /* The first edge rises and edges alternate: an even index rises. */
        bits = (bits << 1) | ((j % 2u) == 0u ? 1u : 0u);
        idx = j;
    }

    /* The reply ends one half-cell after the last mid-cell edge.  After a
     * last bit 1 the line is high there and falls when the servo lets go;
     * that edge carries no data, only a deadline. */
    end = e[idx] + KST_SERVO_HALF_CELL_NS;
    if ((bits & 1u) != 0u) {
        if (idx + 1u >= n || e[idx + 1u] > end + KST_RX_FINAL_EDGE_MAX_NS) {
            return KST_RX_FINAL_EDGE;
        }
        idx++;
        end = e[idx];
    }
    if (idx + 1u < n && e[idx + 1u] < end + KST_RX_QUIET_AFTER_NS) {
        return KST_RX_LENGTH;
    }
    if (window_ns < end + KST_RX_QUIET_AFTER_NS) {
        return KST_RX_WINDOW;
    }

    {
        const unsigned last_mid = (bits & 1u) != 0u ? idx - 1u : idx;
        const uint32_t span = e[last_mid] - e[0];
        const uint32_t halves = 2u * (KST_REPLY_BITS - 1u);

        out->half_cell_ns = (span + halves / 2u) / halves;
        if (span < halves * KST_RX_HALF_CELL_MIN_NS
            || span > halves * KST_RX_HALF_CELL_MAX_NS) {
            return KST_RX_RATE;
        }
    }

    out->flags = (uint8_t)((bits >> 8) & 3u);
    out->value = (uint8_t)(bits & 0xFFu);
    if (expect == KST_EXPECT_SYNC) {
        if (out->flags != 2u || out->value != 0u) {
            return KST_RX_FLAGS;
        }
    } else if (out->flags != 3u) {
        return KST_RX_FLAGS;
    }
    return KST_RX_OK;
}

static kst_rx_t decode(const kst_capture_t *cap, kst_expect_t expect,
                       kst_reply_t *out)
{
    uint32_t e[KST_CAP_MAX_EDGES];
    unsigned n;
    const uint32_t accept_min = expect == KST_EXPECT_WRITE
        ? KST_WRITE_ACCEPT_MIN_NS : KST_READ_ACCEPT_MIN_NS;
    const uint32_t accept_max = expect == KST_EXPECT_WRITE
        ? KST_WRITE_ACCEPT_MAX_NS : KST_READ_ACCEPT_MAX_NS;

    if (cap == NULL || cap->n_edges > KST_CAP_MAX_EDGES) {
        return KST_RX_CAPTURE;
    }
    if (cap->overflow != 0u) {
        return KST_RX_OVERFLOW;
    }
    for (unsigned i = 1; i < cap->n_edges; ++i) {
        if (cap->edge_ns[i] < cap->edge_ns[i - 1u]) {
            return KST_RX_CAPTURE;
        }
    }
    n = filter(cap, e, &out->glitches);

    if (cap->level0 != 0u) {
        return KST_RX_START_LEVEL;
    }
    if (n == 0u) {
        return KST_RX_NO_REPLY;
    }
    if (e[0] < accept_min) {
        return KST_RX_QUIET;
    }
    if (e[0] > accept_max) {
        return KST_RX_LATE;
    }
    out->delay_ns = e[0];
    return track(e, n, cap->window_ns, expect, out);
}

kst_rx_t kst_reply_decode(const kst_capture_t *cap, kst_expect_t expect,
                          kst_reply_t *out)
{
    kst_reply_t scratch;
    kst_rx_t rx;

    if (out == NULL) {
        out = &scratch;
    }
    memset(out, 0, sizeof(*out));
    rx = decode(cap, expect, out);
    out->result = (uint8_t)rx;
    return rx;
}
