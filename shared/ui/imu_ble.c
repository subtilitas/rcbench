/**
 * @file
 * @brief The Bluetooth LE link of the IMU (imu_ble.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_ble.h"

#include <stdio.h>
#include <string.h>

_Static_assert(BLEU_ADDR_TEXT_MAX <= IMU_ADDR_MAX,
               "an address with its type fits the view's");
_Static_assert(BLEU_NAME_MAX <= IMU_NAME_MAX, "a BLE name fits the view's");
_Static_assert(BLEU_FOUND_MAX <= IMU_FOUND_MAX,
               "a BLE scan fits the view's list");
_Static_assert(WIT_CMD_LEN <= BLEU_WRITE_MAX, "a sensor command fits a write");

void imu_ble_init(imu_ble_t *l)
{
    memset(l, 0, sizeof(*l));
    bleu_init(&l->b);
}

/**
 * @brief Run the central while IMU is ON and the radio is synced.
 * @param l      The link.
 * @param now_ms The clock.
 */
static void apply(imu_ble_t *l, uint32_t now_ms)
{
    bleu_enable(&l->b, l->en && l->radio == IMU_RADIO_READY, now_ms);
}

void imu_ble_radio(imu_ble_t *l, imu_radio_t radio, const char *version,
                   uint32_t now_ms)
{
    l->radio = radio;
    memset(l->version, 0, sizeof(l->version));
    if (version != NULL) {
        snprintf(l->version, sizeof(l->version), "%s", version);
    }
    apply(l, now_ms);
}

void imu_ble_on(imu_ble_t *l, bool on, uint32_t now_ms)
{
    l->en = on;
    apply(l, now_ms);
    if (!on) {
        l->scan_open = false;
        l->found_n = 0u;
        l->hidden = 0u;
        l->op_err = IMU_ERR_NONE;
        l->op_code = 0u;
        l->wr_failed = false;
    }
}

bool imu_ble_set_addr(imu_ble_t *l, const char *text, uint32_t now_ms)
{
    bleu_addr_t a;
    if (text != NULL && bleu_addr_parse(text, &a)) {
        bleu_bind(&l->b, &a, now_ms);
    } else {
        bleu_bind(&l->b, NULL, now_ms);
    }
    return l->b.bound;
}

bool imu_ble_bound(const imu_ble_t *l)
{
    return l->b.bound;
}

bool imu_ble_ready(const imu_ble_t *l)
{
    return l->en && l->radio == IMU_RADIO_READY && !l->b.scanning;
}

bool imu_ble_scan(imu_ble_t *l, uint32_t now_ms)
{
    if (!imu_ble_ready(l)) {
        return false;
    }
    l->found_n = 0u;
    l->hidden = 0u;
    l->op_err = IMU_ERR_NONE;
    l->op_code = 0u;
    l->scan_open = bleu_scan(&l->b, now_ms);
    return l->scan_open;
}

bool imu_ble_bind(imu_ble_t *l, uint8_t i, uint32_t now_ms,
                  imu_bound_t *out)
{
    if (i >= l->found_n || l->b.scanning) {
        return false;
    }
    const imu_dev_t *d = &l->found[i];
    char text[IMU_ADDR_MAX + 2u];
    snprintf(text, sizeof(text), "%s %c", d->addr,
             d->addr_type == BLEU_ADDR_RANDOM ? 'R' : 'P');
    bleu_addr_t a;
    if (!bleu_addr_parse(text, &a)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    (void)bleu_addr_format(&a, true, out->addr, sizeof(out->addr));
    memcpy(out->name, d->name, IMU_NAME_MAX - 1u);
    bleu_bind(&l->b, &a, now_ms);
    return true;
}

/**
 * @brief A scan's end, read once: its sensors as the view's devices, the
 *        hidden count, and an error for a scan that failed or listed none.
 * @param l The link.
 */
static void take_scan(imu_ble_t *l)
{
    if (!l->scan_open || l->b.scanning) {
        return;
    }
    l->scan_open = false;
    const bleu_list_t *s = &l->b.list;
    memset(l->found, 0, sizeof(l->found));
    l->found_n = (s->found_n > BLEU_FOUND_MAX) ? (uint8_t)BLEU_FOUND_MAX
                                               : s->found_n;
    for (uint8_t i = 0; i < l->found_n; ++i) {
        const bleu_dev_t *d = &s->found[i];
        imu_dev_t *o = &l->found[i];
        (void)bleu_addr_format(&d->addr, false, o->addr, sizeof(o->addr));
        o->addr_type = d->addr.type;
        memcpy(o->name, d->name, BLEU_NAME_MAX - 1u);
        o->rssi = d->rssi;
        o->flags = (uint8_t)(((d->flags & BLEU_DEV_18F0) != 0u
                                  ? IMU_DEV_UUID_18F0 : 0u)
                             | ((d->flags & BLEU_DEV_ISSC) != 0u
                                  ? IMU_DEV_ISSC : 0u));
    }
    l->hidden = s->hidden;
    if (l->b.scan_status != 0u) {
        l->op_err = IMU_ERR_SCAN;
        l->op_code = l->b.scan_status;
    } else if (l->found_n == 0u) {
        l->op_err = IMU_ERR_NONE_FOUND;
    }
}

/**
 * @brief A command write that went from in flight to failed: noted once
 *        for the sensor side and shown.
 * @param l The link.
 */
static void follow_write(imu_ble_t *l)
{
    if (l->wr_was == BLEU_WR_BUSY && l->b.wr == BLEU_WR_FAILED) {
        l->wr_failed = true;
        l->op_err = IMU_ERR_WRITE;
        l->op_code = l->b.wr_status;
    }
    l->wr_was = l->b.wr;
}

bool imu_ble_event(imu_ble_t *l, const bleu_evt_t *e, uint32_t now_ms)
{
    const bool stream = bleu_event(&l->b, e, now_ms);
    take_scan(l);
    follow_write(l);
    return stream;
}

bool imu_ble_act(imu_ble_t *l, uint32_t now_ms, bleu_act_t *out)
{
    const bool acted = bleu_step(&l->b, now_ms, out);
    take_scan(l);
    follow_write(l);
    return acted;
}

bool imu_ble_write_failed(imu_ble_t *l)
{
    const bool was = l->wr_failed;
    l->wr_failed = false;
    return was;
}

imu_link_t imu_ble_link(const imu_ble_t *l)
{
    switch (l->radio) {
    case IMU_RADIO_NO_LINK: return IMU_LINK_C6_FAILED;
    case IMU_RADIO_NO_SYNC: return IMU_LINK_NO_BT;
    case IMU_RADIO_READY:   break;
    case IMU_RADIO_OFF:
    case IMU_RADIO_STARTING:
    default:                return IMU_LINK_STARTING;
    }
    if (l->b.scanning) {
        return IMU_LINK_SCANNING;
    }
    if (!l->b.bound || l->b.st == BLEU_ST_UP) {
        return IMU_LINK_READY;
    }
    return l->b.st == BLEU_ST_NOT_WIT ? IMU_LINK_BOUND : IMU_LINK_CONNECTING;
}

bool imu_ble_up(const imu_ble_t *l)
{
    return bleu_link_up(&l->b);
}

bool imu_ble_can_send(const imu_ble_t *l)
{
    return bleu_can_send(&l->b);
}

bool imu_ble_send(imu_ble_t *l, const uint8_t cmd[WIT_CMD_LEN],
                  uint32_t now_ms)
{
    return bleu_send(&l->b, cmd, WIT_CMD_LEN, now_ms);
}

/**
 * @brief The link's own error: none while up or unbound, NOT_WIT for a
 *        device without the service, CONNECT for every other end.
 * @param l    The link.
 * @param code Takes the status that ended it.
 * @return The error.
 */
static imu_err_t link_err(const imu_ble_t *l, uint16_t *code)
{
    const bleu_t *b = &l->b;
    *code = 0u;
    if (!b->bound || b->st == BLEU_ST_UP || b->err == BLEU_ERR_NONE) {
        return IMU_ERR_NONE;
    }
    *code = (b->err == BLEU_ERR_LOST) ? b->reason : b->err_status;
    return (b->err == BLEU_ERR_NOT_WIT) ? IMU_ERR_NOT_WIT : IMU_ERR_CONNECT;
}

void imu_ble_view(const imu_ble_t *l, imu_view_t *out)
{
    memcpy(out->version, l->version, sizeof(out->version));
    out->found_n = l->found_n;
    memcpy(out->found, l->found, sizeof(out->found));
    out->hidden = l->hidden;
    if (l->op_err != IMU_ERR_NONE) {
        out->err = l->op_err;
        out->err_code = l->op_code;
    } else {
        out->err = link_err(l, &out->err_code);
    }
}
