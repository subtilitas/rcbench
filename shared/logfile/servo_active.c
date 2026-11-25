/**
 * @file
 * @brief The active servo.  See servo_active.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_active.h"

#include <string.h>

#include "servo_id.h"

/** @brief The active servo and its changes. */
typedef struct {
    servo_id_t          id;       /**< the id, when from is not NONE */
    servo_active_from_t from;     /**< its source; NONE for none     */
    bool                printed;  /**< a label of it printed         */
    uint32_t            serial;   /**< changes counted               */
} active_t;

/** @brief The one instance, none after power-on. */
static active_t s;

const char *servo_active_id(void)
{
    return (s.from != SERVO_ACTIVE_NONE) ? s.id.text : NULL;
}

servo_active_from_t servo_active_from(void)
{
    return s.from;
}

bool servo_active_set(const char *id, servo_active_from_t from,
                      bool run_active)
{
    servo_id_t p;
    if (run_active || (int)from <= (int)SERVO_ACTIVE_NONE
        || (int)from > (int)SERVO_ACTIVE_PICK
        || !servo_id_parse(id, &p)) {
        return false;
    }
    const bool same = s.from != SERVO_ACTIVE_NONE
                      && strcmp(s.id.text, p.text) == 0;
    if (same && s.from == from) {
        return true;
    }
    if (!same) {
        s.id = p;
        s.printed = false;
    }
    s.from = from;
    ++s.serial;
    return true;
}

bool servo_active_clear(bool run_active)
{
    if (run_active) {
        return false;
    }
    if (s.from != SERVO_ACTIVE_NONE) {
        memset(&s.id, 0, sizeof(s.id));
        s.from = SERVO_ACTIVE_NONE;
        s.printed = false;
        ++s.serial;
    }
    return true;
}

uint32_t servo_active_serial(void)
{
    return s.serial;
}

bool servo_active_printed(void)
{
    return s.from != SERVO_ACTIVE_NONE && s.printed;
}

void servo_active_set_printed(void)
{
    if (s.from != SERVO_ACTIVE_NONE && !s.printed) {
        s.printed = true;
        ++s.serial;
    }
}
