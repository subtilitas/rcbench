/**
 * @file
 * @brief The screenshot popup's gate (shot_gate.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "shot_gate.h"

#include <stddef.h>

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
