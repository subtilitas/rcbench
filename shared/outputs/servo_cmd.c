/**
 * @file
 * @brief The SERVO screen's command and the link's reports on it: the state an
 * acknowledged frame rate is shown in, and the rules a kept command and a
 * pause follow (servo_cmd.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_cmd.h"

#include <stddef.h>

#include "link_pages.h"

servo_rate_state_t servo_rate_in_force(uint16_t minor)
{
    return (minor >= LINK_MINOR_SERVO_TARGET_RATE)
               ? SERVO_RATE_IN_FORCE
               : SERVO_RATE_IN_FORCE_SURFACES;
}

uint16_t servo_cmd_pause_root(const servo_cmd_t *c)
{
    if (c == NULL) {
        return 0u;
    }
    return (c->from_pause != 0u) ? c->from_pause : c->pause_seq;
}

bool servo_cmd_stale(servo_pause_end_t *e, const servo_cmd_t *c)
{
    if (e == NULL || c == NULL || !e->on) {
        return false;
    }
    const bool drive = c->kind == SERVO_CMD_POSITION
                       || c->kind == SERVO_CMD_CENTRE
                       || c->kind == SERVO_CMD_SWEEP
                       || c->kind == SERVO_CMD_HOLD;
    if (!drive) {
        return false;
    }
    if (c->from_pause != 0u && c->from_pause == e->pause_seq) {
        return true;
    }
    e->on = false;              /* from after it: nothing of it is queued */
    return false;
}

bool servo_cmd_survives_link_loss(const servo_cmd_t *c)
{
    return c != NULL && c->kind != SERVO_CMD_SWEEP
           && c->kind != SERVO_CMD_HOLD;
}
