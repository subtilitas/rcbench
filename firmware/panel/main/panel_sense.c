/**
 * @file panel_sense.c
 * @brief The current monitors' set-up on its way to the IOmcu: SETUP
 *        INTERFACES' INA228, bus and encoder rows and SETUP > SENSORS'
 *        INA3221 parts, published by the settings observer on app_main and
 *        taken by the control task; and the SENSE_MAP page's service there
 *        (sense_map_link.h), with what it tells the operator.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "link_host.h"
#include "link_pages.h"
#include "panel.h"
#include "log_cols.h"
#include "sense_link.h"
#include "sense_map_link.h"
#include "sense_win_link.h"
#include "settings.h"
#include "sensors_rows.h"
#include "settings_sensors.h"
#include "ui_text.h"

/*
 * The set-up as the settings stand, for the control task's SENSE and
 * SENSE_MAP writes (sense_link.h, sense_map_link.h).
 *
 * The values do not fit an atomic word, so the copy is taken under a
 * spinlock both ways: the control task never pairs one edit's shunt with
 * another's address.  Written by the settings observer on app_main, read
 * once a poll by the control task.
 */
static portMUX_TYPE     s_sense_mux = portMUX_INITIALIZER_UNLOCKED;
static sense_setup_t    s_sense_want;
static sense_map_want_t s_map_want;
static uint8_t          s_map_mirror = 0xFFu;

void publish_sense(void)
{
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    const bool    i228 = settings_get_bool(SET_INA228_EN);
    /* The address options run up from 0x40 in steps of one. */
    const uint8_t i228_addr = (uint8_t)(LINK_SN_I228_ADDR_MIN
                              + (unsigned)settings_get_int(SET_INA228_ADDR));
    /* FAST's part on the INA228's address is left out of the map
     * (sense_map_link.h): no channel is read at 1kHz then. */
    const bool fast_out = i228 && m.fast != LINK_SM_FAST_NONE
                          && i228_addr == LINK_SN_I3221_ADDR_MIN + m.fast / 3u;
    const sense_setup_t w = {
        .i228  = i228,
        /* An IOmcu older than 4.25 reads one INA3221: the mirror part
         * (settings_sensors.h). */
        .i3221 = m.one_on,
        .as5600 = settings_get_bool(SET_ENC_EN),
        .sda   = (int8_t)settings_get_int(SET_SENSE_SDA),
        .scl   = (int8_t)settings_get_int(SET_SENSE_SCL),
        .i228_addr   = i228_addr,
        .i228_uohm   = (uint16_t)settings_get_int(SET_INA228_UOHM),
        .i228_max_da = (uint16_t)lrintf(settings_get(SET_INA228_MAX_A) * 10.0f),
        .i3221_addr  = m.one_addr,
        .i3221_dmohm = m.one_dmohm,
        .i3221_ch    = m.one_ch,
        .i3221_parts = m.parts,
        .i3221_unwatched = m.fast == LINK_SM_FAST_NONE || fast_out,
        .i3221_fast_off_ch1 = m.fast != LINK_SM_FAST_NONE && m.fast % 3u != 0u,
    };
    sense_map_want_t mw;
    memcpy(mw.regs, m.regs, sizeof(mw.regs));
    mw.dark      = m.dark;
    mw.i228_addr = w.i228 ? w.i228_addr : 0u;
    mw.nobus     = w.sda < 0 || w.scl < 0;
    taskENTER_CRITICAL(&s_sense_mux);
    s_sense_want = w;
    s_map_want   = mw;
    s_map_mirror = m.mirror;
    taskEXIT_CRITICAL(&s_sense_mux);
    /* And OUTPUTS' WATCHED BY lines: app_main, as the screens. */
    sensors_rows_publish_watch();
    /* And the channels' limits (panel_limit.c). */
    publish_limits();
}

sense_setup_t sense_wanted(void)
{
    taskENTER_CRITICAL(&s_sense_mux);
    const sense_setup_t w = s_sense_want;
    taskEXIT_CRITICAL(&s_sense_mux);
    return w;
}

/* The SENSE_MAP page (4.25); see sense_map_service().  Control task only. */
sense_map_link_t s_sense_map;
/* The parts' states for SETUP > SENSORS, sense_map_link_status()'s word.
 * Control task writes, app_main reads. */
atomic_uint s_sense_parts;

/* The map alert posted and not yet seen taken: its number in the alert
 * slot (0 none) and its event and part.  Control task only. */
static uint32_t s_map_alert_gen;
static uint8_t  s_map_alert_ev;
static uint8_t  s_map_alert_part;
/* The last REFUSED or DARK alert posted, withdrawn once its cause is over
 * (sense_map_link_event_stands()).  Control task only. */
static uint32_t s_map_setup_gen;
static uint8_t  s_map_setup_ev;
static uint8_t  s_map_setup_part;

/*
 * What the SENSE_MAP page said that the operator is told, one event at a
 * time into a free alert slot, the next no sooner than
 * SENSE_MAP_LINK_EVENT_GAP_MS later, and followed until a frame has taken
 * it, as sense_link_alerts() does for SENSE.  A refused or dark map's
 * alert goes once the map no longer has that cause.
 */
static void map_alerts(void)
{
    if (s_map_setup_gen != 0u
        && !sense_map_link_event_stands(&s_sense_map, s_map_setup_ev,
                                        s_map_setup_part)) {
        control_alert_withdraw(s_map_setup_gen);
        if (s_map_alert_gen == s_map_setup_gen) {
            s_map_alert_gen = 0u;
        }
        s_map_setup_gen = 0u;
    }
    snap_lock();
    const bool     slot_free = !s_snap.alert.pending;
    const uint32_t posted    = s_snap.alert.posted;
    const uint32_t taken     = s_snap.alert.taken;
    snap_unlock();
    if (s_map_alert_gen != 0u) {
        if ((int32_t)(taken - s_map_alert_gen) >= 0) {
            s_map_alert_gen = 0u;                    /* shown */
        } else if (posted != s_map_alert_gen) {
            sense_map_link_event_back(&s_sense_map, s_map_alert_ev,
                                      s_map_alert_part);
            s_map_alert_gen = 0u;                    /* replaced unseen */
        } else {
            return;                                  /* not taken yet */
        }
    }
    if (!slot_free) {
        return;
    }
    uint8_t  part = 0u;
    uint16_t id = 0u;
    const uint8_t ev = sense_map_link_event(&s_sense_map, now_ms(), &part,
                                            &id);
    if (ev == 0u) {
        return;
    }
    s_map_alert_ev   = ev;
    s_map_alert_part = part;
    const unsigned addr = LINK_SN_I3221_ADDR_MIN + part;
    char line[ALERT_MAX];
    switch (ev) {
    case SENSE_MAP_LINK_EV_REFUSED:
        snprintf(line, sizeof(line), "%s", TR(ALERT_SENSE_I3221_SETUP));
        break;
    case SENSE_MAP_LINK_EV_DARK:
        snprintf(line, sizeof(line), TR(ALERT_SENSE_DARK), addr);
        break;
    case SENSE_MAP_LINK_EV_WRONG:
        snprintf(line, sizeof(line), TR(ALERT_SENSE_WRONG), addr,
                 (unsigned)id, "INA3221");
        break;
    default:
        snprintf(line, sizeof(line), TR(ALERT_SENSE_SILENT), "INA3221",
                 addr);
        break;
    }
    s_map_alert_gen = control_alert_numbered(line);
    if (ev == SENSE_MAP_LINK_EV_REFUSED || ev == SENSE_MAP_LINK_EV_DARK) {
        s_map_setup_gen  = s_map_alert_gen;
        s_map_setup_ev   = ev;
        s_map_setup_part = part;
    }
}

/* SENSE_WIN_A and SENSE_WIN_B (4.25), for the log.  Control task only. */
static sense_win_link_t s_sense_win;

/* The log's INA3221 columns as the map the IOmcu holds makes them, for the
 * log task's next file: s_cols_map false without a map read (an IOmcu
 * older than 4.25, or not read yet), and CH1 to CH3 then.  Written by the
 * control task, read by the log task, under s_sense_mux. */
static log_cols_t s_cols;
static bool       s_cols_map;

bool sense_log_cols(log_cols_t *out)
{
    taskENTER_CRITICAL(&s_sense_mux);
    const bool map = s_cols_map;
    if (map && out != NULL) {
        *out = s_cols;
    }
    taskEXIT_CRITICAL(&s_sense_mux);
    return map;
}

/* The held map's columns and pages, after every SENSE_MAP exchange: only
 * a map in force, as one derived from SENSE watches nothing and would give
 * a file no INA3221 column, where SERVO_SENSE's CH1 to CH3 read. */
static void publish_held(void)
{
    const bool map = sense_map_link_in_force(&s_sense_map);
    log_cols_t cols;
    log_cols_from_map(map ? s_sense_map.held : NULL, &cols);
    taskENTER_CRITICAL(&s_sense_mux);
    s_cols     = cols;
    s_cols_map = map;
    taskEXIT_CRITICAL(&s_sense_mux);
    sense_win_link_want(&s_sense_win,
                        map ? (uint8_t)s_sense_map.held[LINK_SM_ENABLE] : 0u);
}

void sense_map_came_up(uint16_t minor)
{
    sense_map_want_t w;
    taskENTER_CRITICAL(&s_sense_mux);
    w = s_map_want;
    taskEXIT_CRITICAL(&s_sense_mux);
    sense_map_link_want(&s_sense_map, &w, now_ms());
    sense_map_link_came_up(&s_sense_map, minor, now_ms());
    sense_link_set_map(&s_sense_link, sense_map_link_page(&s_sense_map));
    sense_link_set_map_held(&s_sense_link, false);
    sense_win_link_came_up(&s_sense_win, minor);
    publish_held();
}

void sense_map_lost(void)
{
    sense_map_link_lost(&s_sense_map);
    sense_win_link_lost(&s_sense_win);
    prog5v_box_reading(false, 0u, 0);
    publish_held();
    sense_link_set_map(&s_sense_link, false);
    sense_link_set_map_held(&s_sense_link, false);
    atomic_store(&s_sense_parts, 0u);
}

/*
 * The SENSE_MAP page, from the poll just before sense_link_service():
 * the map SENSORS asks for written while @p idle, IDs and FLAGS read,
 * SENSE read again after a write was taken, and the parts' states
 * published for SETUP.  An IOmcu older than 4.25 is sent nothing.
 */
void sense_map_service(bool idle)
{
    sense_map_want_t w;
    uint8_t mirror;
    taskENTER_CRITICAL(&s_sense_mux);
    w = s_map_want;
    mirror = s_map_mirror;
    taskEXIT_CRITICAL(&s_sense_mux);
    sense_map_link_want(&s_sense_map, &w, now_ms());
    for (int k = 0; k < 2; ++k) {
        sense_map_link_op_t op;
        /* Pins the bus gets back are SENSE's to write first. */
        const bool go = idle && !sense_map_link_waits_for_pins(&s_sense_map,
                                                               &s_sense_link);
        if (!sense_map_link_next(&s_sense_map, now_ms(), go, &op)) {
            break;
        }
        link_msg_t reply = { 0 };
        const bool answered =
            op.write ? write_regs(&s_host, op.page, op.off, op.n, op.regs,
                                  &reply)
                     : read_regs(&s_host, op.page, op.off, op.n, &reply);
        int result = SENSE_MAP_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : SENSE_MAP_LINK_ACK;
        }
        sense_map_link_done(&s_sense_map, result,
                            (answered && reply.op == LINK_OP_DATA)
                                ? reply.regs : NULL,
                            now_ms());
        if (result == SENSE_MAP_LINK_NO_ANSWER) {
            break;
        }
    }
    sense_link_set_map(&s_sense_link, sense_map_link_page(&s_sense_map));
    sense_link_set_map_held(&s_sense_link, sense_map_link_held(&s_sense_map));
    if (sense_map_link_take_reread(&s_sense_map)) {
        sense_link_reread(&s_sense_link);
    }
    map_alerts();
    publish_held();
    atomic_store(&s_sense_parts,
                 (unsigned)sense_map_link_status(&s_sense_map, &s_sense_link,
                                                 mirror, now_ms()));
}

uint16_t sense_win_take_peaks(int16_t *out)
{
    return sense_win_link_take_peaks(&s_sense_win, out);
}

/*
 * SENSE_WIN_A and SENSE_WIN_B, from the poll on every pass, armed or not:
 * the windows are readings and change nothing.  Each page due is read, two
 * reads a call at most; an unanswered read ends the pass.  An IOmcu older
 * than 4.25 is sent nothing.
 */
void sense_win_service(void)
{
    for (unsigned k = 0u; k < 2u; ++k) {
        sense_win_link_op_t op;
        if (!sense_win_link_next(&s_sense_win, now_ms(), &op)) {
            return;
        }
        link_msg_t reply = { 0 };
        const bool answered = read_regs(&s_host, op.page, 0u, op.n, &reply);
        int result = SENSE_WIN_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : SENSE_WIN_LINK_ACK;
        }
        sense_win_link_done(&s_sense_win, result,
                            (answered && reply.op == LINK_OP_DATA)
                                ? reply.regs : NULL);
        if (result == SENSE_WIN_LINK_NO_ANSWER) {
            return;
        }
    }
}

void prog5v_publish_reading(void)
{
    if (sense_map_link_in_force(&s_sense_map)) {
        const unsigned en = s_sense_map.held[LINK_SM_ENABLE];
        for (unsigned c = 0u; c < LINK_SM_CHANNELS; ++c) {
            uint16_t mv = 0u;
            int16_t  ma = 0;
            if ((en & (1u << (c / 3u))) != 0u
                && s_sense_map.held[LINK_SM_WATCH(c)] == LINK_SM_WATCH_PROG_5V
                && sense_win_link_last(&s_sense_win, c, &mv, &ma)) {
                prog5v_box_reading(true, mv, ma);
                return;
            }
        }
    }
    prog5v_box_reading(false, 0u, 0);
}

void sense_win_attach(bench_state_t *b, bool ring)
{
    if (b == NULL) {
        return;
    }
    (void)sense_win_link_window(&s_sense_win, b->servo_window, &b->chw);
    /* FAST's group from the ring, which has every window of it; the ring
     * is the held map's FAST only once the page holds the map asked. */
    if (ring && sense_map_link_held(&s_sense_map)) {
        sense_win_link_ring(&b->chw, s_sense_map.held[LINK_SM_FAST], b);
    }
}
