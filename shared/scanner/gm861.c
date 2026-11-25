/**
 * @file
 * @brief The command protocol of a GM861 QR scanner module and its
 *        sequencer (gm861.h).
 *
 * GM861 is a product name of its maker, named here to describe
 * compatibility only.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gm861.h"

#include <string.h>

/** @brief Bytes around an answer's data: head (2), type, length, CRC (2). */
#define ANSWER_FRAME 6u
/** @brief Sends of one request before the pass gives up. */
#define SENDS 2u
/** @brief The factory reset zone (manual 10.5). */
#define ZONE_RESET 0x00D9u
/** @brief The value written to ZONE_RESET for a factory reset. */
#define RESET_VALUE 0x50u

/** @brief The probe's rates in baud, in the order tried; 9600 is the module's default. */
static const uint32_t RATES[GM861_RATES] = {
    9600u, 57600u, 115200u, 38400u, 19200u, 14400u, 4800u, 1200u,
};

/** @brief Symbologies the bench turns off: bit 0 of each zone (manual 10.6). */
static const uint8_t OFF_ZONES[] = {
    0x26u, 0x29u, 0x2Eu, 0x2Fu, 0x30u, 0x31u, 0x32u, 0x33u, 0x36u, 0x39u,
    0x3Cu, 0x40u, 0x43u, 0x46u, 0x49u, 0x4Cu, 0x4Fu, 0x50u, 0x51u, 0x55u,
    0x56u, 0x57u, 0x5Cu, 0x5Fu,
};

uint16_t gm861_crc(const uint8_t *p, size_t n)
{
    uint32_t crc = 0u;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint32_t)p[i] << 8;
        for (unsigned b = 0; b < 8u; ++b) {
            crc <<= 1;
            if ((crc & 0x10000u) != 0u) {
                crc ^= 0x11021u;    /* drop bit 16, apply 0x1021 */
            }
        }
    }
    return (uint16_t)crc;
}

size_t gm861_request(uint8_t out[GM861_REQ_LEN], uint8_t type, uint16_t addr,
                     uint8_t data)
{
    out[0] = 0x7Eu;
    out[1] = 0x00u;
    out[2] = type;
    out[3] = 0x01u;
    out[4] = (uint8_t)(addr >> 8);
    out[5] = (uint8_t)(addr & 0xFFu);
    out[6] = data;
    const uint16_t crc = gm861_crc(&out[2], 5u);
    out[7] = (uint8_t)(crc >> 8);
    out[8] = (uint8_t)(crc & 0xFFu);
    return GM861_REQ_LEN;
}

int gm861_answer_data(const uint8_t *frame, size_t len, const uint8_t **data)
{
    if (frame == NULL || len < ANSWER_FRAME + 1u
        || (frame[0] != 0x02u && frame[0] != 0x03u)
        || frame[1] != 0x00u || frame[2] != 0x00u
        || len != (size_t)frame[3] + ANSWER_FRAME) {
        return -1;
    }
    const uint16_t crc = gm861_crc(&frame[2], len - 4u);
    if (frame[len - 2u] != (uint8_t)(crc >> 8)
        || frame[len - 1u] != (uint8_t)(crc & 0xFFu)) {
        return -1;
    }
    if (data != NULL) {
        *data = &frame[4];
    }
    return (int)frame[3];
}

const uint32_t *gm861_rates(void)
{
    return RATES;
}

uint8_t gm861_zone_want(uint16_t addr, uint8_t found, gm861_mode_t mode)
{
    for (size_t i = 0; i < sizeof(OFF_ZONES); ++i) {
        if (addr == OFF_ZONES[i]) {
            return (uint8_t)(found & ~0x01u);
        }
    }
    switch (addr) {
    case 0x0000u:   /* good-read LED on; read mode */
        return (uint8_t)((found & ~0x83u) | 0x80u
                         | (mode == GM861_MODE_CONTINUOUS ? 0x02u : 0x03u));
    case 0x0003u:   /* a setup code's content is not sent */
        return (uint8_t)(found & ~0x01u);
    case 0x000Du:   /* serial output, UTF-8 output */
        return (uint8_t)((found & ~0x0Fu) | 0x0Cu);
    case 0x0013u:   /* the same code not again within 3.0s */
        return 0x9Eu;
    case 0x003Fu:   /* QR on */
        return (uint8_t)(found | 0x01u);
    case 0x0054u:   /* Data Matrix off, both kinds */
    case GM861_ZONE_CUT:   /* the whole code, nothing cut */
        return (uint8_t)(found & ~0x03u);
    case 0x0060u:   /* plain data, tail CR LF on, nothing added */
        return 0x21u;
    default:
        return found;
    }
}

/** @brief The value read for a held zone. */
static uint8_t zone_now(const gm861_t *g, uint16_t addr)
{
    return addr == GM861_ZONE_CUT ? g->cut : g->zones[addr];
}

/** @brief The first held zone at or after @p from that differs; GM861_NO_ZONE for none. */
static uint16_t next_diff(const gm861_t *g, uint16_t from)
{
    for (uint16_t a = from; a < GM861_ZONES; ++a) {
        if (gm861_zone_want(a, g->zones[a], g->mode) != g->zones[a]) {
            return a;
        }
    }
    if (from <= GM861_ZONE_CUT
        && gm861_zone_want(GM861_ZONE_CUT, g->cut, g->mode) != g->cut) {
        return GM861_ZONE_CUT;
    }
    return GM861_NO_ZONE;
}

/** @brief @p now is at or past @p t, wrap-safe. */
static bool reached(uint32_t now, uint32_t t)
{
    return (int32_t)(now - t) >= 0;
}

/** @brief Put out a request and set when its answer is late. */
static size_t send(gm861_t *g, uint32_t now, uint8_t *tx, uint8_t type,
                   uint16_t addr, uint8_t data, uint8_t expect, bool ack)
{
    g->sent = true;
    g->answered = false;
    g->expect = expect;
    g->ack = ack;
    ++g->tries;
    /* 10 bit times a byte: the request and its answer on the wire. */
    const uint32_t bits = (GM861_REQ_LEN + ANSWER_FRAME + expect) * 10000u;
    const uint32_t baud = RATES[g->rate];
    g->due = now + GM861_ANSWER_MS + (bits + baud - 1u) / baud;
    if (type == GM861_T_WRITE) {
        ++g->writes;
    } else if (type == GM861_T_SAVE) {
        ++g->saves;
    }
    return gm861_request(tx, type, addr, data);
}

/** @brief Start again at step 2. */
static void to_level(gm861_t *g, uint32_t now)
{
    g->phase = GM861_P_LEVEL;
    g->t0 = now;
    g->sent = false;
    g->tries = 0u;
    g->probed = 0u;
    g->passes = 0u;
    g->wrote = 0u;
    g->usb = false;
    g->verify = false;
}

/** @brief Wait GM861_RETRY_MS in @p state, then start again. */
static void to_wait(gm861_t *g, uint32_t now, gm861_state_t state)
{
    g->state = state;
    g->phase = GM861_P_WAIT;
    g->sent = false;
    g->due = now + GM861_RETRY_MS;
}

/** @brief Go to @p phase with a fresh request. */
static void to_phase(gm861_t *g, gm861_phase_t phase)
{
    g->phase = phase;
    g->sent = false;
    g->tries = 0u;
}

/** @brief READY: the first heartbeat GM861_HEARTBEAT_MS from @p now. */
static void to_ready(gm861_t *g, uint32_t now)
{
    g->state = GM861_READY;
    g->phase = GM861_P_BEAT;
    g->sent = false;
    g->missed = 0u;
    g->due = now + GM861_HEARTBEAT_MS;
}

/** @brief The zones are read: write what differs, save, or be READY. */
static void zones_read(gm861_t *g, uint32_t now)
{
    if (!g->verify) {
        memcpy(g->found, g->zones, sizeof(g->found));
        g->found_cut = g->cut;
        g->usb = (g->zones[0x0D] & 0x03u) == 0x01u
                 || (g->zones[0x0D] & 0x03u) == 0x03u;
    }
    const uint16_t first = next_diff(g, 0u);
    if (first == GM861_NO_ZONE) {
        if (g->wrote > 0u) {
            to_phase(g, GM861_P_SAVE);
        } else {
            to_ready(g, now);
        }
        return;
    }
    if (g->passes >= SENDS) {
        g->bad_zone = first;
        g->state = GM861_NO_ANSWER;
        g->phase = GM861_P_HELD;
        g->sent = false;
        return;
    }
    ++g->passes;
    g->state = g->usb ? GM861_USB_OUTPUT : GM861_SETTING;
    g->at = first;
    to_phase(g, GM861_P_WRITE);
}

/** @brief The answer due has arrived: move on. */
static void answered(gm861_t *g, uint32_t now)
{
    switch (g->phase) {
    case GM861_P_PROBE:
        g->state = GM861_SETTING;
        g->verify = false;
        to_phase(g, GM861_P_READ);
        break;
    case GM861_P_READ:
        memcpy(g->zones, g->answer, GM861_ZONES);
        to_phase(g, GM861_P_READ_CUT);
        break;
    case GM861_P_READ_CUT:
        g->cut = g->answer[0];
        zones_read(g, now);
        break;
    case GM861_P_WRITE:
        ++g->wrote;
        g->at = next_diff(g, (uint16_t)(g->at + 1u));
        if (g->at == GM861_NO_ZONE) {
            g->verify = true;
            to_phase(g, GM861_P_READ);
        } else {
            to_phase(g, GM861_P_WRITE);
        }
        break;
    case GM861_P_SAVE:
        to_ready(g, now);
        break;
    case GM861_P_RESET:
        to_level(g, now);
        break;
    default:   /* GM861_P_BEAT */
        g->missed = 0u;
        g->sent = false;
        g->due = g->t0 + GM861_HEARTBEAT_MS;
        break;
    }
}

/** @brief The answer due is late. */
static void late(gm861_t *g, uint32_t now)
{
    g->sent = false;
    switch (g->phase) {
    case GM861_P_PROBE:
        g->rate = (uint8_t)((g->rate + 1u) % GM861_RATES);
        g->tries = 0u;
        if (++g->probed >= GM861_RATES) {
            to_wait(g, now, g->state == GM861_LOST ? GM861_LOST
                                                   : GM861_NO_ANSWER);
        }
        break;
    case GM861_P_BEAT:
        g->due = g->t0 + GM861_HEARTBEAT_MS;
        if (++g->missed >= GM861_LOST_BEATS) {
            g->state = GM861_LOST;
            to_level(g, now);
        }
        break;
    case GM861_P_RESET:
        to_level(g, now);
        break;
    default:   /* read, write, save: one more send, then give up */
        if (g->tries >= SENDS) {
            to_wait(g, now, GM861_NO_ANSWER);
        }
        break;
    }
}

/** @brief The request of the phase, sent fresh. */
static size_t request(gm861_t *g, uint32_t now, uint8_t *tx)
{
    switch (g->phase) {
    case GM861_P_PROBE:
        return send(g, now, tx, GM861_T_READ, 0x0000u, 1u, 1u, false);
    case GM861_P_READ:
        return send(g, now, tx, GM861_T_READ, 0x0000u, GM861_ZONES,
                    GM861_ZONES, false);
    case GM861_P_READ_CUT:
        return send(g, now, tx, GM861_T_READ, GM861_ZONE_CUT, 1u, 1u, false);
    case GM861_P_WRITE:
        return send(g, now, tx, GM861_T_WRITE, g->at,
                    gm861_zone_want(g->at, zone_now(g, g->at), g->mode),
                    1u, true);
    case GM861_P_SAVE:
        return send(g, now, tx, GM861_T_SAVE, 0x0000u, 0u, 1u, true);
    case GM861_P_RESET:
        return send(g, now, tx, GM861_T_WRITE, ZONE_RESET, RESET_VALUE, 1u,
                    true);
    default:   /* GM861_P_BEAT */
        if (!reached(now, g->due)) {
            return 0u;
        }
        g->t0 = now;
        return send(g, now, tx, GM861_T_HEARTBEAT, 0x0000u, 0u, 1u, true);
    }
}

void gm861_init(gm861_t *g, gm861_mode_t mode)
{
    if (g == NULL) {
        return;
    }
    memset(g, 0, sizeof(*g));
    g->mode = mode;
    g->state = GM861_NOT_CONNECTED;
    g->phase = GM861_P_START;
    g->bad_zone = GM861_NO_ZONE;
}

void gm861_stop(gm861_t *g)
{
    if (g == NULL) {
        return;
    }
    g->state = GM861_OFF;
    g->sent = false;
}

void gm861_recheck(gm861_t *g)
{
    if (g == NULL || g->state == GM861_OFF) {
        return;
    }
    g->bad_zone = GM861_NO_ZONE;
    g->kick = true;
}

void gm861_set_mode(gm861_t *g, gm861_mode_t mode)
{
    if (g == NULL) {
        return;
    }
    g->mode = mode;
    if (g->state == GM861_READY) {
        gm861_recheck(g);
    }
}

void gm861_factory_reset(gm861_t *g)
{
    if (g == NULL || g->state == GM861_OFF) {
        return;
    }
    g->kick = false;
    g->bad_zone = GM861_NO_ZONE;
    g->state = GM861_SETTING;
    to_phase(g, GM861_P_RESET);
}

size_t gm861_step(gm861_t *g, uint32_t now_ms, bool rx_high, uint8_t *tx,
                  size_t tx_max)
{
    if (g == NULL || tx == NULL || tx_max < GM861_REQ_LEN
        || g->state == GM861_OFF) {
        return 0u;
    }
    if (!g->started) {
        g->started = true;
        g->t0 = now_ms;
    }
    if (g->kick) {
        g->kick = false;
        to_level(g, now_ms);
    }
    if (g->sent) {
        if (g->answered) {
            answered(g, now_ms);
        } else if (reached(now_ms, g->due)) {
            late(g, now_ms);
        } else {
            return 0u;
        }
    }
    switch (g->phase) {
    case GM861_P_START:
        if (reached(now_ms, g->t0 + GM861_START_MS)) {
            to_level(g, now_ms);
        }
        return 0u;
    case GM861_P_WAIT:
        if (reached(now_ms, g->due)) {
            to_level(g, now_ms);
        }
        return 0u;
    case GM861_P_HELD:
        return 0u;
    case GM861_P_LEVEL:
        if (!rx_high) {
            if (reached(now_ms, g->t0 + GM861_LEVEL_MS)) {
                to_wait(g, now_ms, GM861_NOT_CONNECTED);
            }
            return 0u;
        }
        to_phase(g, GM861_P_PROBE);
        g->probed = 0u;
        break;
    default:
        break;
    }
    return request(g, now_ms, tx);
}

void gm861_answer(gm861_t *g, const uint8_t *frame, size_t len)
{
    const uint8_t *data = NULL;
    if (g == NULL || !g->sent || g->answered) {
        return;
    }
    const int n = gm861_answer_data(frame, len, &data);
    if (n != (int)g->expect || (g->ack && data[0] != 0x00u)) {
        return;
    }
    memcpy(g->answer, data, (size_t)n);
    g->answer_len = (uint8_t)n;
    g->answered = true;
}

gm861_state_t gm861_state(const gm861_t *g)
{
    return g == NULL ? GM861_OFF : g->state;
}

uint32_t gm861_baud(const gm861_t *g)
{
    return g == NULL ? RATES[0] : RATES[g->rate];
}

uint16_t gm861_bad_zone(const gm861_t *g)
{
    return g->bad_zone;
}

const uint8_t *gm861_found(const gm861_t *g, uint8_t *cut)
{
    if (cut != NULL) {
        *cut = g->found_cut;
    }
    return g->found;
}
