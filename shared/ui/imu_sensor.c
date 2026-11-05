/**
 * @file
 * @brief The sensor side of the Bluetooth IMU (imu_sensor.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_sensor.h"

#include <string.h>

void imu_sensor_init(imu_sensor_t *s)
{
    memset(s, 0, sizeof(*s));
    imu_init(&s->m);
    s->rate_code = WIT_RATE_50HZ;
}

/**
 * @brief Queue one sensor command for the outbox; a full outbox drops it.
 * @param s   The sensor.
 * @param cmd WIT_CMD_LEN bytes.
 * @param len Their count from the encoder: 0 queues nothing.
 */
static void queue(imu_sensor_t *s, const uint8_t cmd[WIT_CMD_LEN], size_t len)
{
    if (len != WIT_CMD_LEN || s->out_n >= IMU_OUTBOX) {
        return;
    }
    const uint8_t at = (uint8_t)((s->out_head + s->out_n) % IMU_OUTBOX);
    memcpy(s->out[at], cmd, WIT_CMD_LEN);
    ++s->out_n;
}

/** @brief Queue the unlock. @param s The sensor. */
static void queue_unlock(imu_sensor_t *s)
{
    uint8_t b[WIT_CMD_LEN] = { 0 };
    queue(s, b, wit_cmd_unlock(b));
}

/** @brief Queue SAVE. @param s The sensor. */
static void queue_save(imu_sensor_t *s)
{
    uint8_t b[WIT_CMD_LEN] = { 0 };
    queue(s, b, wit_cmd_save(b));
}

/** @brief Queue CALSW @p mode. @param s The sensor. @param mode wit_cal_t. */
static void queue_calsw(imu_sensor_t *s, uint8_t mode)
{
    uint8_t b[WIT_CMD_LEN] = { 0 };
    queue(s, b, wit_cmd_calsw(b, mode));
}

/**
 * @brief The end of a calibration: CALSW 0 and SAVE after the unlock.
 * @param s  The sensor.
 * @param ok Whether the reading stayed valid through it.
 */
static void cal_end(imu_sensor_t *s, bool ok)
{
    queue_unlock(s);
    queue_calsw(s, WIT_CAL_EXIT);
    queue_save(s);
    s->cal = ok ? IMU_CAL_DONE : IMU_CAL_FAILED;
    s->cal_left_ms = 0u;
}

void imu_sensor_values(imu_sensor_t *s, uint8_t rate_code, bool six,
                       bool send)
{
    if (send && s->rate_code != rate_code) {
        s->rate_due = true;
    }
    if (send && s->six != six) {
        s->algo_due = true;
    }
    s->rate_code = rate_code;
    s->six = six;
}

void imu_sensor_off(imu_sensor_t *s)
{
    if (s->cal == IMU_CAL_RUNNING) {
        cal_end(s, false);
    }
    if (s->m.r.zeroed) {
        ++s->zero_gen;        /* the zero goes with the reading */
    }
    imu_init(&s->m);
    s->angle_seen = false;
    s->had_frames = false;
}

void imu_sensor_rebind(imu_sensor_t *s)
{
    s->had_frames = false;
}

void imu_sensor_feed(imu_sensor_t *s, const uint8_t *b, size_t n,
                     uint32_t now_ms)
{
    const uint32_t before = s->m.r.taken_ms;
    imu_feed(&s->m, b, n, now_ms);
    if (s->m.r.taken_ms != before) {
        /* An angle after the stale point: the reading was stale for a
         * while, which the frame task may not have seen. */
        if (s->angle_seen && s->m.r.taken_ms - before >= IMU_STALE_MS) {
            ++s->gaps;
            s->gap_at_ms = before + IMU_STALE_MS;
            if (s->cal == IMU_CAL_RUNNING) {
                s->cal_lost = true;   /* a stretch the tick missed */
            }
        }
        s->angle_seen = true;
    }
}

void imu_sensor_tick(imu_sensor_t *s, uint32_t now_ms, bool link_up)
{
    imu_tick(&s->m, now_ms);
    if (s->m.r.valid) {
        s->had_frames = true;
    }
    if (s->cal == IMU_CAL_RUNNING) {
        if (!s->m.r.valid) {
            s->cal_lost = true;
        }
        const uint32_t held = now_ms - s->cal_from_ms;
        if (held >= IMU_CAL_MS) {
            cal_end(s, !s->cal_lost);
        } else {
            s->cal_left_ms = IMU_CAL_MS - held;
        }
    }
    /* Unlock, the writes due and SAVE go as one sequence, once all of it
     * fits: a change that comes while the outbox is full waits, and the
     * write that goes carries SETUP's latest value. */
    const unsigned need = 2u + (s->rate_due ? 1u : 0u) + (s->algo_due ? 1u : 0u);
    /* Not while the calibration runs: its CALSW 0 and SAVE go first. */
    if (link_up && s->m.r.valid && (s->rate_due || s->algo_due)
        && s->cal != IMU_CAL_RUNNING && IMU_OUTBOX - s->out_n >= need) {
        uint8_t b[WIT_CMD_LEN] = { 0 };
        queue_unlock(s);
        if (s->rate_due) {
            queue(s, b, wit_cmd_rate(b, s->rate_code));
        }
        if (s->algo_due) {
            queue(s, b, wit_cmd_axis6(b, s->six));
        }
        queue_save(s);
        s->rate_due = false;
        s->algo_due = false;
    }
}

bool imu_sensor_next(imu_sensor_t *s, uint32_t now_ms,
                     uint8_t out[WIT_CMD_LEN])
{
    if (s->out_n == 0u
        || (s->out_sent && now_ms - s->out_ms < IMU_CMD_GAP_MS)) {
        return false;
    }
    memcpy(out, s->out[s->out_head], WIT_CMD_LEN);
    s->out_head = (uint8_t)((s->out_head + 1u) % IMU_OUTBOX);
    --s->out_n;
    s->out_ms = now_ms;
    s->out_sent = true;
    return true;
}

bool imu_sensor_calibrate(imu_sensor_t *s, bool link_up, uint32_t now_ms)
{
    if (!link_up || !s->m.r.valid || s->cal == IMU_CAL_RUNNING) {
        return false;
    }
    queue_unlock(s);
    queue_calsw(s, WIT_CAL_ACC);
    s->cal = IMU_CAL_RUNNING;
    s->cal_from_ms = now_ms;
    s->cal_lost = false;
    s->cal_left_ms = IMU_CAL_MS;
    return true;
}

bool imu_sensor_zero(imu_sensor_t *s)
{
    if (!imu_zero(&s->m)) {
        return false;
    }
    ++s->zero_gen;
    return true;
}

void imu_sensor_unzero(imu_sensor_t *s)
{
    imu_unzero(&s->m);
    ++s->zero_gen;
}

bool imu_sensor_quiet(const imu_sensor_t *s)
{
    return s->cal != IMU_CAL_RUNNING && s->out_n == 0u;
}

void imu_sensor_flush(imu_sensor_t *s)
{
    s->out_n = 0u;
    s->out_head = 0u;
}

imu_link_t imu_sensor_link(const imu_sensor_t *s)
{
    if (s->m.r.valid) {
        return IMU_LINK_CONNECTED;
    }
    return s->had_frames ? IMU_LINK_NO_FRAMES : IMU_LINK_BOUND;
}

void imu_sensor_view(const imu_sensor_t *s, imu_view_t *out)
{
    out->cal = s->cal;
    out->cal_left_ms = (s->cal == IMU_CAL_RUNNING) ? s->cal_left_ms : 0u;
    out->r = s->m.r;
}

void imu_handout_reset(imu_handout_t *h)
{
    if (h != NULL) {
        memset(h, 0, sizeof(*h));
    }
}

imu_hand_t imu_handout_next(imu_handout_t *h, const imu_reading_t *r,
                            uint32_t gaps)
{
    if (h == NULL || r == NULL) {
        return IMU_HAND_NONE;
    }
    const bool gap = h->given && h->gaps != gaps;
    h->gaps = gaps;
    if (gap && h->was_valid && r->valid) {
        h->was_valid = false;
        return IMU_HAND_GAP;
    }
    if (h->given && h->was_valid == r->valid
        && (!r->valid || r->taken_ms == h->last_taken)) {
        return IMU_HAND_NONE;
    }
    h->given = true;
    h->was_valid = r->valid;
    if (r->valid) {
        h->last_taken = r->taken_ms;
    }
    return IMU_HAND_READING;
}
