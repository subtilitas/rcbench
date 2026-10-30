/**
 * @file panel_limit.c
 * @brief The INA3221 channels' current limits on their way to the IOmcu's
 *        SENSE_LIMIT page.
 *
 * Protocol 4.27, sense_limit_link.h: SETUP > SENSORS' LIMIT rows,
 * published by the settings observer on app_main and taken by the control
 * task, the page's service there, and the band alert when a channel's
 * window passed its limit.  A limit passed is an error of the bench as
 * well: the bench SAFE and the supply off (bench_error_stop()), and one
 * console line a channel (log_trip()).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "link_host.h"
#include "link_pages.h"
#include "panel.h"
#include "sense_limit_link.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_sensors.h"

/**
 * @brief Guards the limits and what each channel watches as the settings
 *        stand, both ways: written by the settings observer on app_main,
 *        read once a poll by the control task.
 */
static portMUX_TYPE s_limit_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Each channel's limit in mA, 0 for none; under s_limit_mux. */
static uint16_t     s_limit_want[LINK_SL_KEPT];
/** @brief What each channel watches, SENS_W_* options; under s_limit_mux. */
static uint8_t      s_limit_watch[LINK_SL_KEPT];

/** @brief The page's service; control task only. */
static sense_limit_link_t s_sense_limit;

/** @brief The console's tag. */
static const char *TAG = "rcbench";

/** @brief Each channel's highest window current read up to the last read
 *         of OVER, for the console line of a trip; control task only. */
static int16_t  s_trip_peak[LINK_SL_KEPT];
/** @brief Bit c: s_trip_peak[c] holds a window; control task only. */
static uint16_t s_trip_peaks;

/** @brief SETUP > SENSORS' LIMIT marks, sensors_rows_set_limits()'s word.
 *         Control task writes, app_main reads. */
atomic_uint s_sense_limits;

void publish_limits(void)
{
    uint16_t ma[LINK_SL_KEPT];
    uint8_t  watch[LINK_SL_KEPT];
    for (int c = 0; c < (int)LINK_SL_KEPT; ++c) {
        ma[c]    = settings_sensors_limit_ma(c);
        watch[c] = (uint8_t)settings_get_int(SET_INA_W(c));
    }
    taskENTER_CRITICAL(&s_limit_mux);
    memcpy(s_limit_want, ma, sizeof(ma));
    memcpy(s_limit_watch, watch, sizeof(watch));
    taskEXIT_CRITICAL(&s_limit_mux);
}

void sense_limit_came_up(uint16_t minor)
{
    sense_limit_link_came_up(&s_sense_limit, minor, now_ms());
}

void sense_limit_lost(void)
{
    sense_limit_link_lost(&s_sense_limit);
    atomic_store(&s_sense_limits, 0u);
}

/** @brief The next alert into the band, when the band's slot is free. */
static void limit_alerts(void)
{
    snap_lock();
    const bool slot_free = !s_snap.alert.pending;
    snap_unlock();
    if (!slot_free) {
        return;
    }
    unsigned c = 0u;
    uint16_t ma = 0u;
    const sense_limit_link_ev_t ev =
        sense_limit_link_event(&s_sense_limit, now_ms(), &c, &ma);
    if (ev == SENSE_LIMIT_LINK_EV_NONE) {
        return;
    }
    int watch = 0;
    if (c < LINK_SL_KEPT) {
        taskENTER_CRITICAL(&s_limit_mux);
        watch = s_limit_watch[c];
        taskEXIT_CRITICAL(&s_limit_mux);
    }
    char line[ALERT_MAX];
    (void)sensors_rows_alert(line, sizeof(line), ev, c, ma, watch);
    (void)control_alert_numbered(line);
}

/**
 * @brief One console line for each channel of a trip: the part, channel,
 *        what it watches, the highest current read and the limit held.
 * @param trip The channels, bit c channel c (sense_limit_link_take_trip()).
 */
static void log_trip(uint16_t trip)
{
    uint8_t watch[LINK_SL_KEPT];
    taskENTER_CRITICAL(&s_limit_mux);
    memcpy(watch, s_limit_watch, sizeof(watch));
    taskEXIT_CRITICAL(&s_limit_mux);
    for (unsigned c = 0u; c < LINK_SL_KEPT; ++c) {
        if ((trip & (1u << c)) == 0u) {
            continue;
        }
        char line[96];
        ESP_LOGI(TAG, "%s",
                 sensors_rows_trip_line(
                     line, sizeof(line), c, watch[c],
                     (s_trip_peaks & (1u << c)) != 0u, s_trip_peak[c],
                     sense_limit_link_held_ma(&s_sense_limit, c)));
    }
}

/*
 * SENSE_LIMIT, from the poll on every pass, armed or not: the page takes
 * a limit in any bank state.  A limit passed is an error: the bench SAFE
 * and the supply off at the read that shows it, every channel at once, and
 * the band's alert in its turn.  Two exchanges a call at most; an
 * unanswered one ends the pass.
 */
void sense_limit_service(void)
{
    uint16_t want[LINK_SL_KEPT];
    taskENTER_CRITICAL(&s_limit_mux);
    memcpy(want, s_limit_want, sizeof(want));
    taskEXIT_CRITICAL(&s_limit_mux);
    sense_limit_link_want(&s_sense_limit, want, now_ms());
    for (int k = 0; k < 2; ++k) {
        sense_limit_link_op_t op;
        if (!sense_limit_link_next(&s_sense_limit, now_ms(), &op)) {
            break;
        }
        link_msg_t reply = { 0 };
        const bool answered =
            op.write ? write_regs(&s_host, LINK_PAGE_SENSE_LIMIT, op.off,
                                  op.n, op.regs, &reply)
                     : read_regs(&s_host, LINK_PAGE_SENSE_LIMIT, op.off,
                                 op.n, &reply);
        int result = SENSE_LIMIT_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : SENSE_LIMIT_LINK_ACK;
        }
        if (answered && reply.op == LINK_OP_DATA
            && (op.kind == SENSE_LIMIT_LINK_OP_OVER
                || op.kind == SENSE_LIMIT_LINK_OP_READ)) {
            /* The windows OVER judged since its last read, as far as the
             * panel read them, for the console line of a trip. */
            s_trip_peaks = sense_win_take_peaks(s_trip_peak);
        }
        sense_limit_link_done(&s_sense_limit, result,
                              (answered && reply.op == LINK_OP_DATA)
                                  ? reply.regs : NULL,
                              now_ms());
        if (result == SENSE_LIMIT_LINK_NO_ANSWER) {
            break;
        }
    }
    const uint16_t trip = sense_limit_link_take_trip(&s_sense_limit);
    if (trip != 0u) {
        bench_error_stop();
        log_trip(trip);
    }
    limit_alerts();
    atomic_store(&s_sense_limits,
                 (unsigned)(sense_limit_link_unheld(&s_sense_limit)
                            | ((unsigned)sense_limit_link_status(
                                   &s_sense_limit) << 12)));
}
