/*
 * The master's cycle on an EX Bus line.  See exbus_sched.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "exbus_sched.h"

#include <string.h>

/* A received byte and the quiet time after it, in byte times: 3. */
_Static_assert(EXBUS_QUIET_BITS % EXBUS_BITS_PER_BYTE == 0u,
               "the quiet time is a whole number of bytes");
#define BYTE_AND_QUIET (1u + EXBUS_QUIET_BITS / EXBUS_BITS_PER_BYTE)

void exbus_cfg_default(exbus_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->baud = EXBUS_BAUD_LS;
    cfg->period_us = EXBUS_PERIOD_DEFAULT_US;
    cfg->window_us = EXBUS_RELEASE_MIN_US;
    cfg->hold_us = EXBUS_HOLD_DEFAULT_US;
    cfg->channels = 16;
    cfg->request_every = 1;
}

/* Bytes of the longest transmission of @p cfg. */
static uint32_t tx_bytes(const exbus_cfg_t *cfg)
{
    return EXBUS_OVERHEAD + 2u * (uint32_t)cfg->channels
           + (cfg->request_every != 0u ? EXBUS_REQUEST_LEN : 0u);
}

uint32_t exbus_cycle_us(const exbus_cfg_t *cfg)
{
    if (cfg == NULL || cfg->channels == 0u || cfg->baud == 0u) {
        return 0;
    }
    return exbus_air_us(tx_bytes(cfg), cfg->baud) + cfg->window_us;
}

exbus_cfg_err_t exbus_cfg_check(const exbus_cfg_t *cfg)
{
    if (cfg == NULL) {
        return EXBUS_CFG_ERR_ARG;
    }
    if (cfg->baud != EXBUS_BAUD_LS && cfg->baud != EXBUS_BAUD_HS) {
        return EXBUS_CFG_ERR_BAUD;
    }
    if (cfg->channels == 0u || cfg->channels > EXBUS_MAX_CHANNELS) {
        return EXBUS_CFG_ERR_CHANNELS;
    }
    if (cfg->window_us < EXBUS_RELEASE_MIN_US
        || cfg->window_us > EXBUS_RELEASE_MAX_US) {
        return EXBUS_CFG_ERR_WINDOW;
    }
    if (cfg->period_us < EXBUS_PERIOD_MIN_US
        || cfg->period_us > EXBUS_PERIOD_MAX_US) {
        return EXBUS_CFG_ERR_PERIOD;
    }
    if (exbus_cycle_us(cfg) + EXBUS_PERIOD_GUARD_US > cfg->period_us) {
        return EXBUS_CFG_ERR_FIT;
    }
    if (cfg->hold_us != 0u && cfg->hold_us < cfg->period_us) {
        return EXBUS_CFG_ERR_HOLD;
    }
    return EXBUS_CFG_OK;
}

exbus_cfg_err_t exbus_sched_init(exbus_sched_t *s, const exbus_driver_t *drv,
                                 const exbus_cfg_t *cfg)
{
    if (s == NULL || drv == NULL || drv->start == NULL || drv->poll == NULL
        || drv->abort == NULL) {
        return EXBUS_CFG_ERR_ARG;
    }
    const exbus_cfg_err_t err = exbus_cfg_check(cfg);

    if (err != EXBUS_CFG_OK) {
        return err;
    }
    memset(s, 0, sizeof(*s));
    s->drv = *drv;
    s->cfg = *cfg;
    s->last_rx = EXBUS_RX_NONE;
    return EXBUS_CFG_OK;
}

bool exbus_sched_set_channels(exbus_sched_t *s, const uint16_t *counts,
                              unsigned n, uint32_t now_us)
{
    if (s == NULL || counts == NULL || n == 0u || n != s->cfg.channels) {
        return false;
    }
    for (unsigned i = 0; i < n; ++i) {
        s->ch[i] = exbus_clamp_counts(counts[i]);
        if (s->ch[i] != counts[i]) {
            s->stats.clamped++;
        }
    }
    s->ch_us = now_us;
    s->has_channels = 1;
    return true;
}

/* The channel values are set and younger than hold_us. */
static bool fresh(const exbus_sched_t *s, uint32_t now_us)
{
    return s->has_channels != 0u
           && (s->cfg.hold_us == 0u || now_us - s->ch_us < s->cfg.hold_us);
}

bool exbus_sched_start(exbus_sched_t *s, uint32_t now_us)
{
    if (s == NULL || !fresh(s, now_us)) {
        return false;
    }
    if (s->running == 0u) {
        s->running = 1;
        s->next_us = now_us;
        s->until_request = 0;
    }
    return true;
}

void exbus_sched_stop(exbus_sched_t *s)
{
    if (s == NULL) {
        return;
    }
    s->running = 0;
    if (s->busy != 0u) {
        s->drv.abort(s->drv.ctx);
        s->busy = 0;
        s->requested = 0;
    }
}

bool exbus_sched_running(const exbus_sched_t *s)
{
    return s != NULL && s->running != 0u;
}

const exbus_reply_t *exbus_sched_reply(const exbus_sched_t *s)
{
    return s != NULL && s->has_reply != 0u ? &s->reply : NULL;
}

const exbus_capture_t *exbus_sched_capture(const exbus_sched_t *s)
{
    return s != NULL ? &s->cap : NULL;
}

exbus_rx_t exbus_sched_last_rx(const exbus_sched_t *s)
{
    return s != NULL ? (exbus_rx_t)s->last_rx : EXBUS_RX_NONE;
}

/* The reply in the capture of the transaction that ended. */
static unsigned take_reply(exbus_sched_t *s, uint32_t now_us)
{
    const exbus_capture_t *c = &s->cap;
    uint8_t bytes[EXBUS_RX_MAX] = { 0 };
    exbus_reply_t r;
    unsigned n = c->n > EXBUS_RX_MAX ? EXBUS_RX_MAX : c->n;
    bool framing = false;

    for (unsigned i = 0; i < n; ++i) {
        bytes[i] = c->bytes[i].value;
        if ((c->bytes[i].flags & EXBUS_RXB_FRAMING) != 0u) {
            framing = true;
        }
    }
    exbus_rx_t rx = exbus_reply_parse(bytes, n, s->packet_id, &r);

    if (rx == EXBUS_RX_OK && c->bytes[r.skipped].at_us > s->cfg.window_us) {
        rx = EXBUS_RX_LATE;
    } else if (rx != EXBUS_RX_OK && rx != EXBUS_RX_NONE && framing) {
        rx = EXBUS_RX_FRAMING;
    }
    s->last_rx = (uint8_t)rx;
    if (rx == EXBUS_RX_NONE) {
        s->stats.no_reply++;
        return EXBUS_EV_NO_REPLY;
    }
    if (rx != EXBUS_RX_OK) {
        s->stats.bad[rx]++;
        return EXBUS_EV_BAD_REPLY;
    }
    s->reply = r;
    s->reply_us = now_us;
    s->has_reply = 1;
    s->stats.replies++;
    if (r.block_len == 0u) {
        s->stats.empty++;
    }
    return EXBUS_EV_REPLY;
}

/* The transaction ended with s->cap filled. */
static unsigned finish(exbus_sched_t *s, uint32_t now_us)
{
    s->busy = 0;
    if (s->cap.overflow != 0u) {
        s->stats.overflow++;
    }
    if (s->requested != 0u) {
        s->requested = 0;
        return take_reply(s, now_us);
    }
    if (s->cap.n != 0u) {
        s->stats.unsolicited++;
        return EXBUS_EV_UNSOLICITED;
    }
    return 0;
}

/* Lay out and start the next cycle. */
static unsigned begin(exbus_sched_t *s, uint32_t now_us)
{
    const bool request = s->cfg.request_every != 0u && s->until_request == 0u;
    const uint8_t id = s->next_id;
    size_t len = exbus_build_channels(s->ch, s->cfg.channels, id, !request,
                                      s->tx, NULL);

    if (request) {
        len += exbus_build_request(id, s->tx + len);
    }
    s->tx_len = (uint8_t)len;
    if (!s->drv.start(s->drv.ctx, s->tx, (unsigned)len, s->cfg.window_us)) {
        s->stats.refused++;
        return EXBUS_EV_REFUSED;
    }
    s->busy = 1;
    s->requested = request ? 1u : 0u;
    s->packet_id = id;
    s->next_id = (uint8_t)(id + 1u);
    s->started_us = now_us;
    s->budget_us = exbus_air_us((uint32_t)len + (EXBUS_RX_MAX + 1u)
                                                * BYTE_AND_QUIET,
                                s->cfg.baud) + s->cfg.window_us;
    if (s->cfg.request_every != 0u) {
        s->until_request = request ? (uint8_t)(s->cfg.request_every - 1u)
                                   : (uint8_t)(s->until_request - 1u);
    }
    memset(&s->cap, 0, sizeof(s->cap));
    s->stats.cycles++;
    if (request) {
        s->stats.requests++;
    }
    if (now_us - s->next_us >= s->cfg.period_us) {
        s->stats.late++;
        s->next_us = now_us;
    }
    s->next_us += s->cfg.period_us;
    return EXBUS_EV_SENT;
}

unsigned exbus_sched_step(exbus_sched_t *s, uint32_t now_us)
{
    unsigned ev = 0;

    if (s == NULL) {
        return 0;
    }
    if (s->busy != 0u) {
        if (s->drv.poll(s->drv.ctx, &s->cap)) {
            ev |= finish(s, now_us);
        } else if (now_us - s->started_us
                   >= s->budget_us + EXBUS_DRIVER_GRACE_US) {
            s->drv.abort(s->drv.ctx);
            s->busy = 0;
            s->requested = 0;
            memset(&s->cap, 0, sizeof(s->cap));
            s->cap.overflow = 1;
            s->stats.cut++;
            ev |= EXBUS_EV_CUT;
        } else {
            return ev;
        }
    }
    if (s->running == 0u) {
        return ev;
    }
    if (!fresh(s, now_us)) {
        s->running = 0;
        s->stats.stale++;
        return ev | EXBUS_EV_STALE;
    }
    if ((int32_t)(now_us - s->next_us) >= 0) {
        ev |= begin(s, now_us);
    }
    return ev;
}
