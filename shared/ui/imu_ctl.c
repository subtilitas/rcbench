/**
 * @file
 * @brief The Bluetooth IMU's controller (imu_ctl.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_ctl.h"

#include <string.h>

/** @brief SET_IMU_RATE's options as WitMotion codes, in their order. */
static const uint8_t k_rates[] = {
    WIT_RATE_10HZ, WIT_RATE_20HZ, WIT_RATE_50HZ, WIT_RATE_100HZ,
    WIT_RATE_200HZ,
};

uint8_t imu_ctl_rate_code(int option)
{
    if (option < 0 || option >= (int)(sizeof(k_rates) / sizeof(k_rates[0]))) {
        return WIT_RATE_50HZ;
    }
    return k_rates[option];
}

void imu_ctl_init(imu_ctl_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    imu_ble_init(&c->ble);
    imu_sensor_init(&c->sensor);
    c->cfg.rate_code = WIT_RATE_50HZ;
}

/**
 * @brief Whether the sensor's stream flows: IMU ON, a sensor bound, the
 *        link subscribed.
 * @param c The controller.
 * @return Whether it does.
 */
static bool stream_up(const imu_ctl_t *c)
{
    return c->cfg.en && imu_ble_bound(&c->ble) && imu_ble_up(&c->ble);
}

void imu_ctl_config(imu_ctl_t *c, const imu_cfg_t *cfg, uint32_t now_ms)
{
    if (c == NULL || cfg == NULL) {
        return;
    }
    const imu_cfg_t was = c->cfg;
    c->cfg = *cfg;
    c->cfg.addr[IMU_ADDR_MAX - 1u] = '\0';
    c->cfg.name[IMU_NAME_MAX - 1u] = '\0';
    imu_sensor_values(&c->sensor, cfg->rate_code, cfg->six, c->configured);
    imu_sensor_filter(&c->sensor, cfg->filter);
    if (!c->configured || strcmp(was.addr, c->cfg.addr) != 0) {
        (void)imu_ble_set_addr(&c->ble, c->cfg.addr, now_ms);
        imu_sensor_rebind(&c->sensor);
    }
    if (!c->configured || was.en != cfg->en) {
        imu_ble_on(&c->ble, cfg->en, now_ms);
        if (c->configured && !cfg->en) {
            imu_sensor_off(&c->sensor);
        }
    }
    c->configured = true;
}

void imu_ctl_radio(imu_ctl_t *c, imu_radio_t radio, const char *version,
                   uint32_t now_ms)
{
    if (c != NULL) {
        imu_ble_radio(&c->ble, radio, version, now_ms);
    }
}

bool imu_ctl_command(imu_ctl_t *c, const imu_cmd_t *cmd, uint32_t now_ms)
{
    if (c == NULL || cmd == NULL || !c->cfg.en) {
        return false;
    }
    switch (cmd->kind) {
    case IMU_CMD_SCAN:
        return imu_ble_scan(&c->ble, now_ms);
    case IMU_CMD_BIND: {
        imu_bound_t b;
        if (!imu_ble_bind(&c->ble, cmd->arg, now_ms, &b)) {
            return false;
        }
        /* What SETUP keeps now, until the panel's next imu_ctl_config()
         * says the same. */
        memcpy(c->cfg.addr, b.addr, sizeof(c->cfg.addr));
        memcpy(c->cfg.name, b.name, sizeof(c->cfg.name));
        imu_sensor_rebind(&c->sensor);
        ++c->bind_seq;
        return true;
    }
    case IMU_CMD_CALIBRATE:
        return imu_sensor_calibrate(&c->sensor, stream_up(c), now_ms);
    case IMU_CMD_ZERO:
        return imu_sensor_zero(&c->sensor);
    case IMU_CMD_UNZERO:
        imu_sensor_unzero(&c->sensor);
        return true;
    case IMU_CMD_NONE:
    default:
        return false;
    }
}

void imu_ctl_event(imu_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms)
{
    if (c == NULL || e == NULL) {
        return;
    }
    if (imu_ble_event(&c->ble, e, now_ms) && c->cfg.en) {
        imu_sensor_feed(&c->sensor, e->data, e->len, now_ms);
    }
    if (imu_ble_write_failed(&c->ble)) {
        imu_sensor_flush(&c->sensor);
    }
}

bool imu_ctl_step(imu_ctl_t *c, uint32_t now_ms, bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    if (imu_ble_act(&c->ble, now_ms, out)) {
        return true;
    }
    if (imu_ble_write_failed(&c->ble)) {
        imu_sensor_flush(&c->sensor);
    }
    imu_sensor_tick(&c->sensor, now_ms, stream_up(c));
    /* A sensor command on 8841: one write in flight at a time. */
    uint8_t cmd[WIT_CMD_LEN];
    if (imu_ble_can_send(&c->ble)
        && imu_sensor_next(&c->sensor, now_ms, cmd)
        && imu_ble_send(&c->ble, cmd, now_ms)) {
        return imu_ble_act(&c->ble, now_ms, out);
    }
    return false;
}

imu_link_t imu_ctl_link(const imu_ctl_t *c)
{
    if (c == NULL || !c->cfg.en) {
        return IMU_LINK_OFF;
    }
    const imu_link_t l = imu_ble_link(&c->ble);
    if (l != IMU_LINK_READY) {
        return l;
    }
    if (!imu_ble_bound(&c->ble)) {
        return IMU_LINK_READY;
    }
    return imu_sensor_link(&c->sensor);
}

void imu_ctl_view(const imu_ctl_t *c, imu_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (c == NULL) {
        return;
    }
    out->link = imu_ctl_link(c);
    imu_ble_view(&c->ble, out);
    if (imu_ble_bound(&c->ble)) {
        memcpy(out->bound_addr, c->cfg.addr, sizeof(out->bound_addr));
        memcpy(out->bound_name, c->cfg.name, sizeof(out->bound_name));
    }
    imu_sensor_view(&c->sensor, out);
}
