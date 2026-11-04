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
    hc05_init(&c->bt);
    imu_init(&c->m);
    c->cfg.rate_code = WIT_RATE_50HZ;
}

/**
 * @brief Queue one sensor command for the outbox; a full outbox drops it.
 * @param c   The controller.
 * @param cmd WIT_CMD_LEN bytes.
 * @param len Their count from the encoder: 0 queues nothing.
 */
static void queue(imu_ctl_t *c, const uint8_t cmd[WIT_CMD_LEN], size_t len)
{
    if (len != WIT_CMD_LEN || c->out_n >= IMU_OUTBOX) {
        return;
    }
    const uint8_t at = (uint8_t)((c->out_head + c->out_n) % IMU_OUTBOX);
    memcpy(c->out[at], cmd, WIT_CMD_LEN);
    ++c->out_n;
}

/** @brief Queue the unlock. @param c The controller. */
static void queue_unlock(imu_ctl_t *c)
{
    uint8_t b[WIT_CMD_LEN];
    queue(c, b, wit_cmd_unlock(b));
}

/** @brief Queue SAVE. @param c The controller. */
static void queue_save(imu_ctl_t *c)
{
    uint8_t b[WIT_CMD_LEN];
    queue(c, b, wit_cmd_save(b));
}

/** @brief Queue CALSW @p mode. @param c The controller. @param mode wit_cal_t. */
static void queue_calsw(imu_ctl_t *c, uint8_t mode)
{
    uint8_t b[WIT_CMD_LEN];
    queue(c, b, wit_cmd_calsw(b, mode));
}

/**
 * @brief The end of a calibration: CALSW 0 and SAVE after the unlock.
 * @param c  The controller.
 * @param ok Whether the reading stayed valid through it.
 */
static void cal_end(imu_ctl_t *c, bool ok)
{
    queue_unlock(c);
    queue_calsw(c, WIT_CAL_EXIT);
    queue_save(c);
    c->cal = ok ? IMU_CAL_DONE : IMU_CAL_FAILED;
    c->cal_left_ms = 0u;
}

/**
 * @brief IMU OFF: the operation ended, the calibration ended FAILED, the
 *        reading and the scan forgotten.
 * @param c The controller.
 */
static void switch_off(imu_ctl_t *c)
{
    if (hc05_busy(&c->bt)) {
        hc05_abort(&c->bt);
    }
    if (c->cal == IMU_CAL_RUNNING) {
        cal_end(c, false);
    }
    imu_init(&c->m);
    c->probed = false;
    c->had_frames = false;
    c->found_n = 0u;
    c->inq_refused = false;
    c->err = HC05_ERR_NONE;
    c->err_code = 0u;
}

void imu_ctl_config(imu_ctl_t *c, const imu_cfg_t *cfg, uint32_t now_ms)
{
    if (c == NULL || cfg == NULL) {
        return;
    }
    const imu_cfg_t was = c->cfg;
    c->cfg = *cfg;
    c->cfg.addr[HC05_ADDR_MAX - 1u] = '\0';
    c->cfg.name[HC05_NAME_MAX - 1u] = '\0';
    if (!c->configured) {
        c->configured = true;
        c->probe_at_ms = now_ms;
        return;
    }
    if (was.en && !cfg->en) {
        switch_off(c);
    } else if (!was.en && cfg->en) {
        c->probe_at_ms = now_ms;
    }
    if (strcmp(was.addr, c->cfg.addr) != 0) {
        c->had_frames = false;
    }
    if (was.rate_code != cfg->rate_code) {
        c->rate_due = true;
    }
    if (was.six != cfg->six) {
        c->algo_due = true;
    }
}

/**
 * @brief Note that an operation started, so its end is read once.
 * @param c       The controller.
 * @param started What the start returned.
 * @return @p started.
 */
static bool opened(imu_ctl_t *c, bool started)
{
    if (started) {
        c->op_open = true;
        c->err = HC05_ERR_NONE;
        c->err_code = 0u;
    }
    return started;
}

bool imu_ctl_command(imu_ctl_t *c, const imu_cmd_t *cmd, uint32_t now_ms)
{
    if (c == NULL || cmd == NULL || !c->cfg.en) {
        return false;
    }
    /* An AT operation takes the UART from the sensor: not while the
     * calibration runs or a sensor command still waits to go out. */
    const bool at_free = c->probed && !hc05_busy(&c->bt)
                         && c->cal != IMU_CAL_RUNNING && c->out_n == 0u;
    switch (cmd->kind) {
    case IMU_CMD_SCAN:
        if (!at_free) {
            return false;
        }
        c->found_n = 0u;
        c->inq_refused = false;
        return opened(c, hc05_scan(&c->bt, now_ms));
    case IMU_CMD_BIND:
        if (!at_free || cmd->arg >= c->found_n) {
            return false;
        }
        return opened(c, hc05_bind(&c->bt, c->found[cmd->arg].addr, IMU_PIN,
                                   now_ms));
    case IMU_CMD_ANY:
        if (!at_free) {
            return false;
        }
        return opened(c, hc05_any(&c->bt, IMU_PIN, now_ms));
    case IMU_CMD_CALIBRATE:
        if (imu_ctl_link(c) != IMU_LINK_CONNECTED
            || c->cal == IMU_CAL_RUNNING) {
            return false;
        }
        queue_unlock(c);
        queue_calsw(c, WIT_CAL_ACC);
        c->cal = IMU_CAL_RUNNING;
        c->cal_from_ms = now_ms;
        c->cal_lost = false;
        c->cal_left_ms = IMU_CAL_MS;
        return true;
    case IMU_CMD_ZERO:
        return imu_zero(&c->m);
    case IMU_CMD_UNZERO:
        imu_unzero(&c->m);
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
    if (hc05_busy(&c->bt)) {
        hc05_feed(&c->bt, b, n);
    } else if (c->cfg.en) {
        imu_feed(&c->m, b, n, now_ms);
    }
}

/**
 * @brief Read the end of the operation that ran, once.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void op_ended(imu_ctl_t *c, uint32_t now_ms)
{
    const hc05_t *h = &c->bt;
    if (!c->op_open || hc05_busy(h)
        || (h->state != HC05_ST_DONE && h->state != HC05_ST_FAILED)) {
        return;
    }
    c->op_open = false;
    c->err = h->err;
    c->err_code = h->err_code;
    if (!c->cfg.en) {
        return;               /* ended by IMU OFF: nothing to take */
    }
    if (h->state == HC05_ST_FAILED) {
        /* No AT at any baud, in a probe or later: the module is gone, and
         * the probe goes again IMU_PROBE_RETRY_MS on. */
        if (h->op == HC05_OP_PROBE || h->err == HC05_ERR_NO_AT) {
            c->probed = false;
            c->probe_at_ms = now_ms + IMU_PROBE_RETRY_MS;
        }
        if (h->op == HC05_OP_SCAN) {
            c->inq_refused = h->err == HC05_ERR_INQ_REFUSED;
        }
        return;
    }
    switch (h->op) {
    case HC05_OP_PROBE:
        c->probed = true;
        break;
    case HC05_OP_SCAN:
        c->found_n = (h->found_n > HC05_FOUND_MAX) ? (uint8_t)HC05_FOUND_MAX
                                                   : h->found_n;
        memcpy(c->found, h->found, sizeof(c->found));
        break;
    case HC05_OP_BIND:
    case HC05_OP_ANY: {
        /* The module is set and restarted in data mode: what SETUP keeps
         * now, until the panel's next imu_ctl_config() says the same. */
        const bool any = h->op == HC05_OP_ANY;
        memcpy(c->cfg.addr, any ? IMU_ADDR_ANY : h->target,
               any ? sizeof(IMU_ADDR_ANY) : HC05_ADDR_MAX);
        c->cfg.addr[HC05_ADDR_MAX - 1u] = '\0';
        c->cfg.name[0] = '\0';
        for (uint8_t i = 0; !any && i < c->found_n; ++i) {
            if (strcmp(c->found[i].addr, h->target) == 0) {
                memcpy(c->cfg.name, c->found[i].name, HC05_NAME_MAX);
            }
        }
        c->had_frames = false;
        ++c->bind_seq;
        break;
    }
    case HC05_OP_NONE:
    default:
        break;
    }
}

/**
 * @brief The reading's age, the calibration's clock and the commands due
 *        on a connected sensor.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void data_side(imu_ctl_t *c, uint32_t now_ms)
{
    imu_tick(&c->m, now_ms);
    if (c->m.r.valid) {
        c->had_frames = true;
    }
    if (c->cal == IMU_CAL_RUNNING) {
        if (!c->m.r.valid) {
            c->cal_lost = true;
        }
        const uint32_t held = now_ms - c->cal_from_ms;
        if (held >= IMU_CAL_MS) {
            cal_end(c, !c->cal_lost);
        } else {
            c->cal_left_ms = IMU_CAL_MS - held;
        }
    }
    if (imu_ctl_link(c) == IMU_LINK_CONNECTED && (c->rate_due || c->algo_due)) {
        uint8_t b[WIT_CMD_LEN];
        queue_unlock(c);
        if (c->rate_due) {
            queue(c, b, wit_cmd_rate(b, c->cfg.rate_code));
        }
        if (c->algo_due) {
            queue(c, b, wit_cmd_axis6(b, c->cfg.six));
        }
        queue_save(c);
        c->rate_due = false;
        c->algo_due = false;
    }
}

bool imu_ctl_step(imu_ctl_t *c, uint32_t now_ms, hc05_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    out->kind = HC05_ACT_NONE;
    /* The machine first, also when idle: it lowers KEY after an end. */
    if (hc05_step(&c->bt, now_ms, out)) {
        return true;
    }
    op_ended(c, now_ms);
    if (c->cfg.en && !c->probed && !hc05_busy(&c->bt)
        && (int32_t)(now_ms - c->probe_at_ms) >= 0
        && opened(c, hc05_probe(&c->bt, now_ms))) {
        return hc05_step(&c->bt, now_ms, out);
    }
    data_side(c, now_ms);
    /* A sensor command over the link: in data mode only, one at a time. */
    if (c->out_n > 0u && !hc05_busy(&c->bt) && !c->bt.key
        && (!c->out_sent || now_ms - c->out_ms >= IMU_CMD_GAP_MS)) {
        out->kind = HC05_ACT_SEND;
        memcpy(out->data, c->out[c->out_head], WIT_CMD_LEN);
        out->len = WIT_CMD_LEN;
        c->out_head = (uint8_t)((c->out_head + 1u) % IMU_OUTBOX);
        --c->out_n;
        c->out_ms = now_ms;
        c->out_sent = true;
        return true;
    }
    return false;
}

imu_link_t imu_ctl_link(const imu_ctl_t *c)
{
    if (c == NULL || !c->cfg.en) {
        return IMU_LINK_OFF;
    }
    if (hc05_busy(&c->bt)) {
        if (c->bt.op == HC05_OP_SCAN) {
            return IMU_LINK_SCANNING;
        }
        if (c->bt.op == HC05_OP_BIND || c->bt.op == HC05_OP_ANY) {
            return IMU_LINK_BINDING;
        }
    }
    if (!c->probed) {
        return IMU_LINK_NO_MODULE;
    }
    if (c->cfg.addr[0] == '\0') {
        return IMU_LINK_READY;
    }
    if (c->m.r.valid) {
        return IMU_LINK_CONNECTED;
    }
    return c->had_frames ? IMU_LINK_NO_FRAMES : IMU_LINK_BOUND;
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
    if (c->probed) {
        memcpy(out->version, c->bt.version, sizeof(out->version));
        out->version[HC05_VERSION_MAX - 1u] = '\0';
    }
    memcpy(out->bound_addr, c->cfg.addr, sizeof(out->bound_addr));
    memcpy(out->bound_name, c->cfg.name, sizeof(out->bound_name));
    out->found_n = c->found_n;
    memcpy(out->found, c->found, sizeof(out->found));
    out->err = c->err;
    out->err_code = c->err_code;
    out->inq_refused = c->inq_refused;
    out->cal = c->cal;
    out->cal_left_ms = (c->cal == IMU_CAL_RUNNING) ? c->cal_left_ms : 0u;
    out->r = c->m.r;
}

void imu_handout_reset(imu_handout_t *h)
{
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
    }
}

bool imu_handout_due(imu_handout_t *h, const imu_reading_t *r)
{
    if (h == NULL || r == NULL) {
        return false;
    }
    if (h->given && h->was_valid == r->valid
        && (!r->valid || r->taken_ms == h->last_taken)) {
        return false;
    }
    h->given = true;
    h->was_valid = r->valid;
    if (r->valid) {
        h->last_taken = r->taken_ms;
    }
    return true;
}
