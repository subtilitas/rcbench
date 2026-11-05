/**
 * @file
 * @brief The HC-05 link of the Bluetooth IMU (imu_hc05.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_hc05.h"

#include <string.h>

_Static_assert(HC05_ADDR_MAX <= IMU_ADDR_MAX,
               "an HC-05 address fits the view's");
_Static_assert(HC05_NAME_MAX <= IMU_NAME_MAX, "an HC-05 name fits the view's");
_Static_assert(HC05_VERSION_MAX <= IMU_VERSION_MAX,
               "the HC-05's version fits the view's");
_Static_assert(HC05_FOUND_MAX <= IMU_FOUND_MAX,
               "an HC-05 scan fits the view's list");
_Static_assert(WIT_CMD_LEN <= HC05_OUT_MAX, "a sensor command fits a SEND");

void imu_hc05_init(imu_hc05_t *l)
{
    memset(l, 0, sizeof(*l));
    hc05_init(&l->bt);
}

void imu_hc05_on(imu_hc05_t *l, uint32_t now_ms)
{
    l->probe_at_ms = now_ms;
}

void imu_hc05_off(imu_hc05_t *l)
{
    if (hc05_busy(&l->bt)) {
        hc05_abort(&l->bt);
    }
    l->probed = false;
    l->found_n = 0u;
    l->inq_refused = false;
    l->err = IMU_ERR_NONE;
    l->err_code = 0u;
}

bool imu_hc05_ready(const imu_hc05_t *l)
{
    return l->probed && !hc05_busy(&l->bt);
}

/**
 * @brief Note that an operation started, so its end is read once.
 * @param l       The link.
 * @param started What the start returned.
 * @return @p started.
 */
static bool opened(imu_hc05_t *l, bool started)
{
    if (started) {
        l->op_open = true;
        l->err = IMU_ERR_NONE;
        l->err_code = 0u;
    }
    return started;
}

bool imu_hc05_scan(imu_hc05_t *l, uint32_t now_ms)
{
    l->found_n = 0u;
    l->inq_refused = false;
    return opened(l, hc05_scan(&l->bt, now_ms));
}

bool imu_hc05_bind(imu_hc05_t *l, uint8_t i, uint32_t now_ms)
{
    if (i >= l->found_n) {
        return false;
    }
    return opened(l, hc05_bind(&l->bt, l->found[i].addr, IMU_PIN, now_ms));
}

bool imu_hc05_any(imu_hc05_t *l, uint32_t now_ms)
{
    return opened(l, hc05_any(&l->bt, IMU_PIN, now_ms));
}

bool imu_hc05_take(imu_hc05_t *l, const uint8_t *b, size_t n)
{
    if (!hc05_busy(&l->bt)) {
        return false;
    }
    hc05_feed(&l->bt, b, n);
    return true;
}

bool imu_hc05_act(imu_hc05_t *l, uint32_t now_ms, hc05_act_t *out)
{
    return hc05_step(&l->bt, now_ms, out);
}

/**
 * @brief The module's error as the view's.
 * @param e The hc05 error.
 * @return The same reason as imu_err_t.
 */
static imu_err_t err_of(hc05_err_t e)
{
    switch (e) {
    case HC05_ERR_NO_AT:       return IMU_ERR_NO_AT;
    case HC05_ERR_TIMEOUT:     return IMU_ERR_TIMEOUT;
    case HC05_ERR_REFUSED:     return IMU_ERR_REFUSED;
    case HC05_ERR_INQ_REFUSED: return IMU_ERR_INQ_REFUSED;
    case HC05_ERR_PAIR:        return IMU_ERR_PAIR;
    case HC05_ERR_NONE_FOUND:  return IMU_ERR_NONE_FOUND;
    case HC05_ERR_ARG:         return IMU_ERR_ARG;
    case HC05_ERR_NONE:
    default:                   return IMU_ERR_NONE;
    }
}

/**
 * @brief The scan's devices as the view's: address and name, each cut to
 *        its array and ended, and the RSSI word; the slots past the scan's
 *        count are cleared.
 * @param l The link.
 * @param h The machine whose scan ended DONE.
 */
static void take_found(imu_hc05_t *l, const hc05_t *h)
{
    memset(l->found, 0, sizeof(l->found));
    for (unsigned i = 0; i < HC05_FOUND_MAX; ++i) {
        memcpy(l->found[i].addr, h->found[i].addr, HC05_ADDR_MAX - 1u);
        memcpy(l->found[i].name, h->found[i].name, HC05_NAME_MAX - 1u);
        l->found[i].rssi = h->found[i].rssi;
    }
    l->found_n = (h->found_n > HC05_FOUND_MAX) ? (uint8_t)HC05_FOUND_MAX
                                               : h->found_n;
}

/**
 * @brief What a bind or ANY that ended DONE leaves SETUP to keep: the
 *        address, and the name of the scan's device of that address.
 * @param l     The link.
 * @param h     The machine.
 * @param bound Takes them.
 */
static void take_bound(const imu_hc05_t *l, const hc05_t *h,
                       imu_bound_t *bound)
{
    const bool any = h->op == HC05_OP_ANY;
    memset(bound, 0, sizeof(*bound));
    memcpy(bound->addr, any ? IMU_ADDR_ANY : h->target,
           any ? sizeof(IMU_ADDR_ANY) : HC05_ADDR_MAX - 1u);
    for (uint8_t i = 0; !any && i < l->found_n; ++i) {
        if (strcmp(l->found[i].addr, h->target) == 0) {
            memcpy(bound->name, l->found[i].name, IMU_NAME_MAX - 1u);
        }
    }
}

bool imu_hc05_ended(imu_hc05_t *l, bool en, uint32_t now_ms,
                    imu_bound_t *bound)
{
    const hc05_t *h = &l->bt;
    if (!l->op_open || hc05_busy(h)
        || (h->state != HC05_ST_DONE && h->state != HC05_ST_FAILED)) {
        return false;
    }
    l->op_open = false;
    l->err = err_of(h->err);
    l->err_code = h->err_code;
    if (!en) {
        return false;          /* ended by IMU OFF: nothing to take */
    }
    if (h->state == HC05_ST_FAILED) {
        /* No AT at any baud, in a probe or later: the module is gone, and
         * the probe goes again IMU_PROBE_RETRY_MS on. */
        if (h->op == HC05_OP_PROBE || h->err == HC05_ERR_NO_AT) {
            l->probed = false;
            l->probe_at_ms = now_ms + IMU_PROBE_RETRY_MS;
        }
        if (h->op == HC05_OP_SCAN) {
            l->inq_refused = h->err == HC05_ERR_INQ_REFUSED;
        }
        return false;
    }
    switch (h->op) {
    case HC05_OP_PROBE:
        l->probed = true;
        return false;
    case HC05_OP_SCAN:
        take_found(l, h);
        return false;
    case HC05_OP_BIND:
    case HC05_OP_ANY:
        /* The module is set and restarted in data mode. */
        take_bound(l, h, bound);
        return true;
    case HC05_OP_NONE:
    default:
        return false;
    }
}

bool imu_hc05_probe(imu_hc05_t *l, bool en, uint32_t now_ms)
{
    return en && !l->probed && !hc05_busy(&l->bt)
           && (int32_t)(now_ms - l->probe_at_ms) >= 0
           && opened(l, hc05_probe(&l->bt, now_ms));
}

imu_link_t imu_hc05_link(const imu_hc05_t *l)
{
    if (hc05_busy(&l->bt)) {
        if (l->bt.op == HC05_OP_SCAN) {
            return IMU_LINK_SCANNING;
        }
        if (l->bt.op == HC05_OP_BIND || l->bt.op == HC05_OP_ANY) {
            return IMU_LINK_BINDING;
        }
    }
    return l->probed ? IMU_LINK_READY : IMU_LINK_NO_MODULE;
}

bool imu_hc05_can_send(const imu_hc05_t *l)
{
    return !hc05_busy(&l->bt) && !l->bt.key;
}

void imu_hc05_send(const uint8_t cmd[WIT_CMD_LEN], hc05_act_t *out)
{
    out->kind = HC05_ACT_SEND;
    memcpy(out->data, cmd, WIT_CMD_LEN);
    out->len = WIT_CMD_LEN;
}

void imu_hc05_view(const imu_hc05_t *l, imu_view_t *out)
{
    memset(out->version, 0, sizeof(out->version));
    if (l->probed) {
        memcpy(out->version, l->bt.version, HC05_VERSION_MAX - 1u);
    }
    out->found_n = l->found_n;
    memcpy(out->found, l->found, sizeof(out->found));
    out->err = l->err;
    out->err_code = l->err_code;
    out->inq_refused = l->inq_refused;
}
