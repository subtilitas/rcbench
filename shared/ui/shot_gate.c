/**
 * @file
 * @brief The screenshot popup's gate (shot_gate.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shot_gate.h"

#include <stddef.h>
#include <string.h>

#include "esc_stick_panel.h"
#include "esc_wire_panel.h"
#include "hitec_panel.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "servo_screen.h"
#include "supply_screen.h"

void shot_facts_read(const shot_bench_t *b, shot_facts_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (b == NULL || !b->read) {
        return;
    }
    out->link_known = b->link_up || b->simulated;
    out->safe = !b->armed && !b->arm_wanted;
    out->stop_served = !b->stop_request;
    out->supply_off = !b->supply_on && !b->supply_sample && !b->supply_coming;
    out->prog5v_off = !b->prog5v_on && !b->prog5v_coming;
    out->port_free = !b->hitec_holds && !supply_screen_port_held()
                     && !kst_panel_holds_port() && !hitec_panel_holds_port()
                     && esc_wire_panel_idle();
    out->no_session = !kst_panel_is_open() && !hitec_panel_is_open()
                      && esc_stick_panel_stage() == 0
                      && esc_wire_panel_idle();
    out->no_program_io = !b->card_job_out;
    out->no_motion = !servo_screen_sweeping() && !servo_screen_paused();
    out->no_test = !b->testing && !servo_screen_testing()
                   && !servo_screen_limit_running()
                   && !servo_screen_rauto_running();
    out->no_stick_run = esc_stick_panel_profile() == NULL;
    out->no_update = !b->cu_active && !b->update_busy;
    out->no_fault = b->faults == 0u;
    out->no_lit_fault =
        (b->faults & (uint16_t)~(uint16_t)LINK_FAULT_LINK_SILENT) == 0u;
    out->no_card_fault = !b->fault_log_out;
    out->no_recording = !b->run_open;
}

shot_wait_t shot_gate(const shot_facts_t *f)
{
    if (f == NULL || !f->link_known) {
        return SHOT_WAIT_LINK;
    }
    if (!f->safe) {
        return SHOT_WAIT_ARMED;
    }
    if (!f->stop_served) {
        return SHOT_WAIT_STOP;
    }
    if (!f->supply_off) {
        return SHOT_WAIT_SUPPLY;
    }
    if (!f->prog5v_off) {
        return SHOT_WAIT_PROG5V;
    }
    if (!f->port_free) {
        return SHOT_WAIT_PORT;
    }
    if (!f->no_session) {
        return SHOT_WAIT_SESSION;
    }
    if (!f->no_program_io) {
        return SHOT_WAIT_PROGRAM_IO;
    }
    if (!f->no_motion) {
        return SHOT_WAIT_MOTION;
    }
    if (!f->no_test) {
        return SHOT_WAIT_TEST;
    }
    if (!f->no_stick_run) {
        return SHOT_WAIT_STICK;
    }
    if (!f->no_update) {
        return SHOT_WAIT_UPDATE;
    }
    if (!f->no_fault) {
        return SHOT_WAIT_FAULT;
    }
    if (!f->no_card_fault) {
        return SHOT_WAIT_CARD_FAULT;
    }
    if (!f->no_recording) {
        return SHOT_WAIT_RECORDING;
    }
    return SHOT_IDLE;
}

const char *shot_wait_name(shot_wait_t w)
{
    static const char *const k_names[SHOT_WAIT_COUNT] = {
        [SHOT_IDLE] = "idle",
        [SHOT_WAIT_LINK] = "link",
        [SHOT_WAIT_ARMED] = "armed",
        [SHOT_WAIT_STOP] = "stop",
        [SHOT_WAIT_SUPPLY] = "supply",
        [SHOT_WAIT_PROG5V] = "prog5v",
        [SHOT_WAIT_PORT] = "port",
        [SHOT_WAIT_SESSION] = "session",
        [SHOT_WAIT_PROGRAM_IO] = "program_io",
        [SHOT_WAIT_MOTION] = "motion",
        [SHOT_WAIT_TEST] = "test",
        [SHOT_WAIT_STICK] = "stick",
        [SHOT_WAIT_UPDATE] = "update",
        [SHOT_WAIT_FAULT] = "fault",
        [SHOT_WAIT_CARD_FAULT] = "card_fault",
        [SHOT_WAIT_RECORDING] = "recording",
    };
    if ((unsigned)w >= (unsigned)SHOT_WAIT_COUNT || k_names[w] == NULL) {
        return "?";
    }
    return k_names[w];
}
