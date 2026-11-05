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
    imu_hc05_init(&c->hc05);
    imu_sensor_init(&c->sensor);
    c->cfg.rate_code = WIT_RATE_50HZ;
}

/**
 * @brief Whether the sensor's stream flows: IMU ON, the link up, a sensor
 *        bound.
 * @param c The controller.
 * @return Whether it does.
 */
static bool stream_up(const imu_ctl_t *c)
{
    return c->cfg.en && imu_hc05_link(&c->hc05) == IMU_LINK_READY
           && c->cfg.addr[0] != '\0';
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
    if (!c->configured) {
        c->configured = true;
        imu_hc05_on(&c->hc05, now_ms);
        return;
    }
    if (was.en && !cfg->en) {
        imu_hc05_off(&c->hc05);
        imu_sensor_off(&c->sensor);
    } else if (!was.en && cfg->en) {
        imu_hc05_on(&c->hc05, now_ms);
    }
    if (strcmp(was.addr, c->cfg.addr) != 0) {
        imu_sensor_rebind(&c->sensor);
    }
}

bool imu_ctl_command(imu_ctl_t *c, const imu_cmd_t *cmd, uint32_t now_ms)
{
    if (c == NULL || cmd == NULL || !c->cfg.en) {
        return false;
    }
    /* An AT operation takes the UART from the sensor: not while the
     * calibration runs or a sensor command still waits to go out. */
    const bool at_free = imu_hc05_ready(&c->hc05)
                         && imu_sensor_quiet(&c->sensor);
    switch (cmd->kind) {
    case IMU_CMD_SCAN:
        return at_free && imu_hc05_scan(&c->hc05, now_ms);
    case IMU_CMD_BIND:
        return at_free && imu_hc05_bind(&c->hc05, cmd->arg, now_ms);
    case IMU_CMD_ANY:
        return at_free && imu_hc05_any(&c->hc05, now_ms);
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

void imu_ctl_feed(imu_ctl_t *c, const uint8_t *b, size_t n, uint32_t now_ms)
{
    if (c == NULL || b == NULL || n == 0u) {
        return;
    }
    if (!imu_hc05_take(&c->hc05, b, n) && c->cfg.en) {
        imu_sensor_feed(&c->sensor, b, n, now_ms);
    }
}

bool imu_ctl_step(imu_ctl_t *c, uint32_t now_ms, hc05_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    out->kind = HC05_ACT_NONE;
    /* The machine first, also when idle: it lowers KEY after an end. */
    if (imu_hc05_act(&c->hc05, now_ms, out)) {
        return true;
    }
    imu_bound_t bound;
    if (imu_hc05_ended(&c->hc05, c->cfg.en, now_ms, &bound)) {
        /* What SETUP keeps now, until the panel's next imu_ctl_config()
         * says the same. */
        memcpy(c->cfg.addr, bound.addr, sizeof(c->cfg.addr));
        memcpy(c->cfg.name, bound.name, sizeof(c->cfg.name));
        imu_sensor_rebind(&c->sensor);
        ++c->bind_seq;
    }
    if (imu_hc05_probe(&c->hc05, c->cfg.en, now_ms)) {
        return imu_hc05_act(&c->hc05, now_ms, out);
    }
    imu_sensor_tick(&c->sensor, now_ms, stream_up(c));
    /* A sensor command over the link: in data mode only, one at a time. */
    uint8_t cmd[WIT_CMD_LEN];
    if (imu_hc05_can_send(&c->hc05)
        && imu_sensor_next(&c->sensor, now_ms, cmd)) {
        imu_hc05_send(cmd, out);
        return true;
    }
    return false;
}

imu_link_t imu_ctl_link(const imu_ctl_t *c)
{
    if (c == NULL || !c->cfg.en) {
        return IMU_LINK_OFF;
    }
    const imu_link_t l = imu_hc05_link(&c->hc05);
    if (l != IMU_LINK_READY) {
        return l;
    }
    if (c->cfg.addr[0] == '\0') {
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
    imu_hc05_view(&c->hc05, out);
    memcpy(out->bound_addr, c->cfg.addr, sizeof(out->bound_addr));
    memcpy(out->bound_name, c->cfg.name, sizeof(out->bound_name));
    imu_sensor_view(&c->sensor, out);
}
