/**
 * @file
 * @brief The PASS and PASS_STAT pages of board 3.
 *
 * See pass_page.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pass_page.h"

#include <string.h>

#include "link_msg.h"

_Static_assert(LINK_PA_CHANNELS == PASS_CHANNELS,
               "the pages and the decoder have 9 channels");

/**
 * @brief A count as a register: at most 65535.
 *
 * @param v  The count.
 * @return   The register.
 */
static uint16_t sat16(uint32_t v)
{
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

/**
 * @brief A pin's microseconds as a register.
 *
 * @param p  The pin.
 * @return   Its width, at most 65535.
 */
static uint16_t width(const pass_pin_t *p)
{
    return sat16(p->width_us);
}

/**
 * @brief Every input's glitches, modulo 2^32.
 *
 * @param d  The decoder.
 * @return   The sum.
 */
static uint32_t glitch_sum(const pass_dec_t *d)
{
    uint32_t s = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        s += d->pin[PASS_IN_BIT(k)].glitches;
    }
    return s;
}

/**
 * @brief PASS_STAT from the decoder and the zeros.
 *
 * @param p  The pages.
 * @param d  The decoder.
 */
static void stat_publish(pass_page_t *p, const pass_dec_t *d)
{
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        const pass_pin_t *in = &d->pin[PASS_IN_BIT(k)];
        p->stat[LINK_PT_PERIOD(k)] = sat16(in->period_us);
        p->stat[LINK_PT_MISSED(k)] = sat16(in->missed - p->base_missed[k]);
        p->stat[LINK_PT_FRAMES(k)] =
            (uint16_t)((in->frames - p->base_frames[k]) & 0xFFFFu);
    }
    p->stat[LINK_PT_RESET]    = 0u;
    p->stat[LINK_PT_GLITCHES] = sat16(glitch_sum(d) - p->base_glitches);
}

void pass_page_init(pass_page_t *p, bool capture, uint32_t now_ms)
{
    memset(p, 0, sizeof(*p));
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        p->source[k] = (uint16_t)LINK_PA_SRC_BENCH;
        p->pass[LINK_PA_SOURCE(k)] = (uint16_t)LINK_PA_SRC_BENCH;
    }
    p->capture = capture;
    p->win_start_ms = now_ms;
    p->pass[LINK_PA_FLAGS] = capture ? 0u : LINK_PA_F_NO_CAPTURE;
}

/**
 * @brief Whether a write covers registers at or past @p first.
 *
 * @param off    First register.
 * @param n      Registers.
 * @param first  The first read-only register.
 * @return       true when it does.
 */
static bool past(uint8_t off, uint8_t n, unsigned first)
{
    return (unsigned)off + n > first;
}

uint8_t pass_page_write(pass_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const pass_ctx_t *c)
{
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + n > (unsigned)LINK_PA_COUNT) {
        return (uint8_t)LINK_NACK_BAD_RANGE;
    }
    if (past(off, n, LINK_PA_CHANNELS)) {
        return (uint8_t)LINK_NACK_READ_ONLY;
    }
    uint16_t want[LINK_PA_CHANNELS];
    memcpy(want, p->source, sizeof(want));
    for (unsigned i = 0; i < n; ++i) {
        if (in[i] > (uint16_t)LINK_PA_SRC_BENCH) {
            return (uint8_t)LINK_NACK_BAD_VALUE;
        }
        want[off + i] = in[i];
    }
    bool change = false;
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        if (want[k] == p->source[k]) {
            continue;
        }
        change = true;
        if (want[k] == (uint16_t)LINK_PA_SRC_PASS && !p->capture) {
            return (uint8_t)LINK_NACK_BAD_VALUE;
        }
    }
    if (!change) {
        return 0u;
    }
    if (c->armed) {
        return (uint8_t)LINK_NACK_BUSY;
    }
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        if (p->source[k] == (uint16_t)LINK_PA_SRC_BENCH
            && want[k] != (uint16_t)LINK_PA_SRC_BENCH
            && (c->busy & (1u << k)) != 0u) {
            return (uint8_t)LINK_NACK_BUSY;
        }
    }
    memcpy(p->source, want, sizeof(want));
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        p->pass[LINK_PA_SOURCE(k)] = want[k];
    }
    return 0u;
}

void pass_page_read(const pass_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    for (unsigned i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        out[i] = (r < (unsigned)LINK_PA_COUNT) ? p->pass[r] : 0u;
    }
}

uint8_t pass_stat_write(pass_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const pass_dec_t *d)
{
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + n > (unsigned)LINK_PT_COUNT) {
        return (uint8_t)LINK_NACK_BAD_RANGE;
    }
    if (off != (uint8_t)LINK_PT_RESET || n != 1u) {
        return (uint8_t)LINK_NACK_READ_ONLY;
    }
    if (in[0] > 1u) {
        return (uint8_t)LINK_NACK_BAD_VALUE;
    }
    if (in[0] == 1u) {
        for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
            p->base_missed[k] = d->pin[PASS_IN_BIT(k)].missed;
            p->base_frames[k] = d->pin[PASS_IN_BIT(k)].frames;
        }
        p->base_glitches = glitch_sum(d);
        stat_publish(p, d);
    }
    return 0u;
}

void pass_stat_read(const pass_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out)
{
    for (unsigned i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        out[i] = (r < (unsigned)LINK_PT_COUNT) ? p->stat[r] : 0u;
    }
}

void pass_page_flag(pass_page_t *p, uint16_t flags)
{
    p->acc |= flags;
}

bool pass_page_tick(pass_page_t *p, const pass_dec_t *d, uint32_t now_ms)
{
    const uint32_t since = now_ms - p->win_start_ms;
    if (since < LINK_PA_WINDOW_MS) {
        return false;
    }
    p->win_start_ms = (since >= 2u * LINK_PA_WINDOW_MS)
                          ? now_ms : p->win_start_ms + LINK_PA_WINDOW_MS;
    ++p->window;
    uint16_t lost = 0u;
    uint16_t glitch = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        const pass_pin_t *in  = &d->pin[(size_t)PASS_IN_BIT(k)];
        const pass_pin_t *out = &d->pin[(size_t)PASS_OUT_BIT(k)];
        p->pass[LINK_PA_IN_US(k)]  = width(in);
        p->pass[LINK_PA_OUT_US(k)] = width(out);
        if (in->lost) {
            lost |= (uint16_t)(1u << k);
        }
        if (in->glitches != p->glitch_seen[k]) {
            glitch |= (uint16_t)(1u << k);
            p->glitch_seen[k] = in->glitches;
        }
    }
    p->pass[LINK_PA_LOST]   = lost;
    p->pass[LINK_PA_WINDOW] = p->window;
    p->pass[LINK_PA_FLAGS]  = (uint16_t)(p->acc
                              | (p->capture ? 0u : LINK_PA_F_NO_CAPTURE));
    p->pass[LINK_PA_GLITCH] = glitch;
    p->acc = 0u;
    stat_publish(p, d);
    return true;
}

uint16_t pass_page_mirror(const pass_page_t *p, const outputs_t *o)
{
    uint16_t m = 0u;
    if (o == NULL || !p->capture) {
        return 0u;
    }
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        /* Each channel by its own role: arming surfaces never passes a
         * receiver's throttle pulse to an ESC. */
        if (p->source[k] == (uint16_t)LINK_PA_SRC_PASS
            && outputs_channel_driving(o, (uint8_t)k)) {
            m |= (uint16_t)(1u << k);
        }
    }
    return m;
}

uint16_t pass_page_held(const pass_page_t *p)
{
    uint16_t m = 0u;
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        if (p->source[k] != (uint16_t)LINK_PA_SRC_BENCH) {
            m |= (uint16_t)(1u << k);
        }
    }
    return m;
}

uint16_t pass_page_active(const pass_page_t *p)
{
    uint16_t m = 0u;
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        if (p->source[k] == (uint16_t)LINK_PA_SRC_PASS) {
            m |= (uint16_t)(1u << k);
        }
    }
    return m;
}

uint16_t pass_page_force_off(pass_page_t *p, uint16_t mask, uint16_t busy)
{
    uint16_t off = 0u;
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        const uint16_t b = (uint16_t)(1u << k);
        if ((mask & b) == 0u) {
            continue;
        }
        if (p->source[k] == (uint16_t)LINK_PA_SRC_BENCH && (busy & b) != 0u) {
            continue;
        }
        p->source[k] = (uint16_t)LINK_PA_SRC_OFF;
        p->pass[LINK_PA_SOURCE(k)] = (uint16_t)LINK_PA_SRC_OFF;
        off |= b;
    }
    return off;
}

bool pass_page_session(const kst_port_t *kst, const hitec_port_t *hitec,
                       const esc_port_t *esc)
{
    /* The quiet masks cover a session before its wire driver takes the
     * pin; the pin masks a driver that still holds it; esc_port_pins() a
     * line on a slot, which esc_port_pin_mask() leaves out. */
    return (kst_port_quiet_mask(kst) | kst_port_pin_mask(kst)
            | hitec_port_quiet_mask(hitec) | hitec_port_pin_mask(hitec)
            | esc_port_pin_mask(esc)) != 0u
           || esc_port_supply_hold(esc) || esc_port_pins(esc) != 0u;
}
