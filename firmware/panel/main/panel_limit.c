/*
 * The INA3221 channels' current limits on their way to the IOmcu's
 * SENSE_LIMIT page (protocol 4.27, sense_limit_link.h): SETUP > SENSORS'
 * LIMIT rows, published by the settings observer on app_main and taken by
 * the control task, the page's service there, and the band alert when a
 * channel's window passed its limit.  An alert only: nothing here stops or
 * switches anything.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "link_host.h"
#include "link_pages.h"
#include "panel.h"
#include "sense_limit_link.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_sensors.h"

/* The limits and what each channel watches as the settings stand, under
 * s_limit_mux both ways: written by the settings observer on app_main,
 * read once a poll by the control task. */
static portMUX_TYPE s_limit_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t     s_limit_want[LINK_SL_KEPT];
static uint8_t      s_limit_watch[LINK_SL_KEPT];  /* SENS_W_* options */

/* The page's service; control task only. */
static sense_limit_link_t s_sense_limit;

/* SETUP > SENSORS' LIMIT marks, sensors_rows_set_limits()'s word.
 * Control task writes, app_main reads. */
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

/* The next alert into the band, when the band's slot is free. */
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

/*
 * SENSE_LIMIT, from the poll on every pass, armed or not: the page takes
 * a limit in any bank state, and a limit passed is an alert only.  Two
 * exchanges a call at most; an unanswered one ends the pass.
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
        sense_limit_link_done(&s_sense_limit, result,
                              (answered && reply.op == LINK_OP_DATA)
                                  ? reply.regs : NULL,
                              now_ms());
        if (result == SENSE_LIMIT_LINK_NO_ANSWER) {
            break;
        }
    }
    limit_alerts();
    atomic_store(&s_sense_limits,
                 (unsigned)(sense_limit_link_unheld(&s_sense_limit)
                            | ((unsigned)sense_limit_link_status(
                                   &s_sense_limit) << 12)));
}
