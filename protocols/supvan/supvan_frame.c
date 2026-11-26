/**
 * @file
 * @brief Supvan print protocol frames: the command, the answer joiner,
 *        the status and the material record (supvan.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "supvan.h"

/** @brief Byte offsets in answers. @{ */
#define ST_AT    14u /**< status registers */
#define MAT_AT   22u /**< material record */
#define MAT_TYPE 39u /**< label type */
#define MAT_LEFT 43u /**< labels left */
/** @} */

void sv_cmd_build(uint8_t out[SV_CMD_LEN], uint8_t cmd, uint16_t p0,
                  uint16_t p1)
{
    static const uint8_t head[7] = { SV_MAGIC0, SV_MAGIC1, 0x0Cu, 0x00u,
                                     0x10u, 0x01u, 0xAAu };
    memcpy(out, head, sizeof head);
    out[SV_CMD_AT] = cmd;
    out[10] = 0x00u;
    out[11] = 0x01u;
    out[12] = (uint8_t)p0;
    out[13] = (uint8_t)(p0 >> 8);
    out[14] = (uint8_t)p1;
    out[15] = (uint8_t)(p1 >> 8);
    uint16_t sum = 0u;
    for (unsigned i = 10u; i < SV_CMD_LEN; ++i) {
        sum = (uint16_t)(sum + out[i]);
    }
    out[8] = (uint8_t)sum;
    out[9] = (uint8_t)(sum >> 8);
}

void sv_rx_reset(sv_rx_t *rx)
{
    rx->n = 0u;
    rx->done = 0u;
}

/**
 * @brief Drop the first @p k bytes held.
 * @param rx the joiner
 * @param k  bytes, at most rx->n
 */
static void drop(sv_rx_t *rx, uint16_t k)
{
    memmove(rx->buf, rx->buf + k, (size_t)(rx->n - k));
    rx->n = (uint16_t)(rx->n - k);
}

/**
 * @brief Drop held bytes until they start a frame that can be whole.
 * @param rx the joiner
 * @return the frame's length once its header is in, else 0
 */
static uint16_t resync(sv_rx_t *rx)
{
    while (rx->n > 0u) {
        if (rx->buf[0] != SV_MAGIC0) {
            drop(rx, 1u);
            continue;
        }
        if (rx->n < 2u) {
            return 0u;
        }
        if (rx->buf[1] != SV_MAGIC1) {
            drop(rx, 1u);
            continue;
        }
        if (rx->n < SV_HEAD) {
            return 0u;
        }
        const uint32_t total =
            (uint32_t)(rx->buf[2] | (rx->buf[3] << 8)) + SV_HEAD;
        if (total < SV_RX_MIN || total > SV_RX_MAX) {
            drop(rx, 1u);
            continue;
        }
        return (uint16_t)total;
    }
    return 0u;
}

size_t sv_rx_feed(sv_rx_t *rx, const uint8_t *in, size_t n,
                  const uint8_t **frame, uint16_t *len)
{
    if (frame != NULL) {
        *frame = NULL;
    }
    if (len != NULL) {
        *len = 0u;
    }
    if (rx->done != 0u) {
        drop(rx, rx->done);
        rx->done = 0u;
    }
    size_t used = 0u;
    for (;;) {
        const uint16_t total = resync(rx);
        if (total != 0u && rx->n >= total) {
            rx->done = total;
            if (frame != NULL) {
                *frame = rx->buf;
            }
            if (len != NULL) {
                *len = total;
            }
            return used;
        }
        if (used >= n) {
            return used;
        }
        /* Take what completes the frame, or one byte while its header is
         * not in: no more than 3 bytes are then held, so a resync never
         * needs more room than the buffer has. */
        size_t take = (total != 0u) ? (size_t)(total - rx->n) : 1u;
        if (take > n - used) {
            take = n - used;
        }
        memcpy(rx->buf + rx->n, in + used, take);
        rx->n = (uint16_t)(rx->n + take);
        used += take;
    }
}

bool sv_status_parse(const uint8_t *f, size_t n, sv_status_t *st)
{
    if (f == NULL || n < SV_STATUS_MIN || f[0] != SV_MAGIC0
        || f[1] != SV_MAGIC1 || f[SV_CMD_AT] != SV_C_INQUIRY) {
        return false;
    }
    const uint8_t a = f[ST_AT];
    const uint8_t b = f[ST_AT + 1u];
    const uint8_t c = f[ST_AT + 2u];
    const uint8_t d = f[ST_AT + 3u];
    st->buf_full = (a & 0x01u) != 0u;
    st->label_rw = (a & 0x02u) != 0u;
    st->label_end = (a & 0x04u) != 0u;
    st->label_mode = (a & 0x08u) != 0u;
    st->low_battery = (a & 0x40u) != 0u;
    st->busy = (b & 0x04u) != 0u;
    st->head_hot = (b & 0x08u) != 0u;
    st->cover_open = (c & 0x08u) != 0u;
    st->usb_power = (c & 0x10u) != 0u;
    st->printing = (c & 0x40u) != 0u;
    st->no_label = (d & 0x01u) != 0u;
    st->count = (uint16_t)(f[ST_AT + 4u] | (f[ST_AT + 5u] << 8));
    return true;
}

sv_err_t sv_status_error(const sv_status_t *st)
{
    if (st->cover_open) {
        return SV_ERR_COVER;
    }
    if (st->no_label || st->label_end) {
        return SV_ERR_NO_LABEL;
    }
    if (st->label_rw || st->label_mode) {
        return SV_ERR_LABEL;
    }
    if (st->low_battery) {
        return SV_ERR_BATTERY;
    }
    if (st->head_hot) {
        return SV_ERR_HOT;
    }
    return SV_OK;
}

bool sv_material_parse(const uint8_t *f, size_t n, sv_material_t *m)
{
    if (f == NULL || n < SV_MAT_MIN || f[0] != SV_MAGIC0
        || f[1] != SV_MAGIC1 || f[SV_CMD_AT] != SV_C_MATERIAL) {
        return false;
    }
    memcpy(m->uid, f + MAT_AT, SV_UID_LEN);
    m->type = f[MAT_TYPE];
    m->w_mm = f[MAT_TYPE + 1u];
    m->h_mm = f[MAT_TYPE + 2u];
    m->gap_mm = f[MAT_TYPE + 3u];
    m->left = (uint32_t)f[MAT_LEFT] | ((uint32_t)f[MAT_LEFT + 1u] << 8)
              | ((uint32_t)f[MAT_LEFT + 2u] << 16)
              | ((uint32_t)f[MAT_LEFT + 3u] << 24);
    return true;
}

sv_err_t sv_roll_check(const sv_material_t *m)
{
    if (m->w_mm == 0u || m->h_mm == 0u) {
        return SV_ERR_NO_LABEL;
    }
    if (m->w_mm < SV_ROLL_W_MIN || m->h_mm < SV_ROLL_H_MIN) {
        return SV_ERR_SMALL;
    }
    if (m->w_mm > SV_ROLL_W_MAX || m->h_mm > SV_ROLL_H_MAX) {
        return SV_ERR_LARGE;
    }
    return SV_OK;
}
