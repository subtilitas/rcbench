/**
 * @file devupd_client.c
 * @brief The panel's Bluetooth LE client of a device update
 *        (devupd_client.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "devupd_client.h"

#include <string.h>

_Static_assert(BLEOTA_CTRL_MAX <= BLEU_WRITE_MAX,
               "a bleota control write is one bleuart command write");
_Static_assert(BLEOTA_LEN_STATUS <= BLEU_EVT_DATA,
               "a bleota notification is one bleuart event");
_Static_assert(DEVUPD_CHR_DATA < BLEU_CHR_MAX,
               "the profile's entries fit a bleuart profile");

const bleu_profile_t devupd_profile = {
    .svc = bleota_uuid_svc,
    .chr = {
        { .uuid = bleota_uuid_ctrl, .role = BLEU_ROLE_STREAM, .flags = 0u },
        { .uuid = bleota_uuid_ctrl, .role = BLEU_ROLE_CMD,    .flags = 0u },
        { .uuid = bleota_uuid_data, .role = BLEU_ROLE_NONE,   .flags = 0u },
    },
    .chr_n = 3u,
    .conn = {
        .itvl_min = DEVUPD_ITVL_MIN,
        .itvl_max = DEVUPD_ITVL_MAX,
        .latency = 0u,
        .supervision = BLEU_SUPERVISION,
        .scan_itvl = BLEU_CONN_SCAN_ITVL,
        .scan_window = BLEU_CONN_SCAN_WINDOW,
    },
    .adv_svc = NULL,
    .adv_uuid16 = 0u,
};

bool devupd_client_init(devupd_client_t *c, uint8_t owner)
{
    if (c == NULL) {
        return false;
    }
    memset(c, 0, sizeof *c);
    bleu_init(&c->b);
    devupd_init(&c->e);
    return bleu_setup(&c->b, &devupd_profile, owner);
}

bool devupd_client_start(devupd_client_t *c, const bleu_addr_t *addr,
                         const devupd_image_t *im, uint32_t now)
{
    if (c == NULL || addr == NULL || c->on || devupd_busy(&c->e)) {
        return false;
    }
    (void)devupd_start(&c->e, im, now);
    if (!devupd_busy(&c->e)) {
        return true;
    }
    c->up = false;
    c->wr_wait = false;
    c->have_pend = false;
    bleu_bind(&c->b, addr, now);
    bleu_enable(&c->b, true, now);
    c->on = true;
    return true;
}

void devupd_client_abort(devupd_client_t *c, uint32_t now)
{
    if (c != NULL) {
        devupd_abort(&c->e, now);
    }
}

/**
 * @brief Tell the engine what the machine did: the link up or down, a
 *        control write's answer; and switch the machine off once the
 *        transfer ended.
 * @param c   the client
 * @param now the clock
 */
static void follow(devupd_client_t *c, uint32_t now)
{
    if (c->wr_wait && c->b.wr != BLEU_WR_BUSY) {
        c->wr_wait = false;
        const uint16_t st = c->b.wr == BLEU_WR_OK ? 0u
                            : (c->b.wr_status != 0u ? c->b.wr_status
                                                    : BLEU_STATUS_TIMEOUT);
        devupd_written(&c->e, st, now);
    }
    const bool up = bleu_link_up(&c->b)
                    && c->b.chr_val[DEVUPD_CHR_DATA] != 0u;
    if (up != c->up) {
        c->up = up;
        devupd_link(&c->e, up, c->b.mtu, now);
        if (!up) {
            c->have_pend = false;
            c->wr_wait = false;
        }
    }
    if (c->on && !devupd_busy(&c->e)) {
        bleu_enable(&c->b, false, now);
        bleu_bind(&c->b, NULL, now);
        c->on = false;
        c->have_pend = false;
    }
}

void devupd_client_event(devupd_client_t *c, const bleu_evt_t *e,
                         uint32_t now)
{
    if (c == NULL || e == NULL) {
        return;
    }
    const bool note = bleu_event(&c->b, e, now);
    if (note) {
        devupd_notify(&c->e, e->data, e->len, now);
    }
    follow(c, now);
}

/**
 * @brief One engine step: a control write goes to the machine, a chunk is
 *        kept for devupd_client_chunk().
 * @param c   the client
 * @param now the clock
 */
static void pump(devupd_client_t *c, uint32_t now)
{
    devupd_out_t o;
    if (c->have_pend
        || !devupd_step(&c->e, now, bleu_can_send(&c->b), &o)) {
        return;
    }
    if (o.kind == DEVUPD_OUT_CTRL) {
        if (bleu_send(&c->b, o.ctrl, o.len, now)) {
            c->wr_wait = true;
        } else {
            devupd_written(&c->e, BLEU_STATUS_LOST, now);
        }
    } else {
        c->pend = o;
        c->have_pend = true;
    }
}

bool devupd_client_step(devupd_client_t *c, uint32_t now, bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    pump(c, now);
    follow(c, now);
    const bool acted = bleu_step(&c->b, now, out);
    follow(c, now);
    return acted;
}

bool devupd_client_chunk(devupd_client_t *c, uint32_t now, devupd_out_t *out,
                         uint16_t *conn, uint16_t *handle)
{
    if (c == NULL || out == NULL || conn == NULL || handle == NULL
        || !c->up) {
        return false;
    }
    if (!c->have_pend) {
        pump(c, now);
    }
    if (!c->have_pend) {
        return false;
    }
    *out = c->pend;
    c->have_pend = false;
    *conn = c->b.conn;
    *handle = c->b.chr_val[DEVUPD_CHR_DATA];
    return true;
}

void devupd_client_unsend(devupd_client_t *c, const devupd_out_t *out)
{
    if (c != NULL) {
        devupd_unsend(&c->e, out);
    }
}
