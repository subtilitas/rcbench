/*
 * Which meter measures the servo rail.  See servo_source.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_source.h"

#include <stddef.h>
#include <string.h>

void servo_source_init(servo_source_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->id  = SERVO_SOURCE_MODEL;
        s->why = SERVO_SOURCE_WHY_NO_LINK;
    }
}

/* The first of conditions 1 to 5 that fails, or NONE. */
static servo_source_why_t judge(uint32_t now_ms, uint16_t minor,
                                const sense_link_meter_t *m)
{
    if (!m->wanted) {
        return SERVO_SOURCE_WHY_OFF;
    }
    if (minor < SENSE_LINK_WIN_MINOR) {
        return SERVO_SOURCE_WHY_OLD;
    }
    if (!m->held) {
        return SERVO_SOURCE_WHY_NOT_HELD;
    }
    const uint16_t need = (uint16_t)(LINK_SN_I3221_ONLINE
                                     | LINK_SN_I3221_ID_OK);
    if (!m->status || !m->resets_read
        || (uint32_t)(now_ms - m->status_ms) >= SERVO_SOURCE_FRESH_MS
        || (m->flags & need) != need
        || (m->flags & LINK_SN_BUS_STUCK) != 0u) {
        return SERVO_SOURCE_WHY_SILENT;
    }
    if (!m->win || !m->win_valid
        || (uint32_t)(now_ms - m->win_ms) >= SERVO_SOURCE_FRESH_MS) {
        return SERVO_SOURCE_WHY_NO_WINDOW;
    }
    return SERVO_SOURCE_WHY_NONE;
}

static servo_source_id_t step(servo_source_t *s, uint32_t now_ms,
                              bool link_up, uint16_t minor,
                              const sense_link_meter_t *m)
{
    s->reset_step = false;
    if (!link_up || m == NULL) {
        /* Nothing answers: what was said about the coprocessor that did,
         * and not yet shown, goes with it. */
        s->up           = false;
        s->good         = false;
        s->resets_known = false;
        s->old_told     = false;
        s->events       = 0u;
        s->id           = SERVO_SOURCE_MODEL;
        s->why          = SERVO_SOURCE_WHY_NO_LINK;
        return s->id;
    }
    s->up = true;

    /* The older coprocessor, once per link and per switching on. */
    const bool old = m->wanted && m->page && minor < SENSE_LINK_WIN_MINOR;
    if (old && !s->old_told) {
        s->events |= SERVO_SOURCE_EV_OLD;
    }
    if (!old) {
        s->events &= ~(uint32_t)SERVO_SOURCE_EV_OLD;
    }
    s->old_told = old;

    servo_source_why_t why = judge(now_ms, minor, m);

    /* Condition 6, from every read that carries the count: a reset found
     * and repaired between two reads shows nowhere else.  A set-up taken
     * starts the count again at the coprocessor, and here. */
    if (!m->status || !m->resets_read || m->setups != s->setups) {
        s->resets_known = false;
        s->setups       = m->setups;
    }
    if (m->status && m->resets_read) {
        if (s->resets_known && m->resets != s->resets) {
            s->events |= SERVO_SOURCE_EV_RESET;
            s->reset_step = true;
            if (why == SERVO_SOURCE_WHY_NONE) {
                why = SERVO_SOURCE_WHY_RESET;
            }
        }
        s->resets_known = true;
        s->resets       = m->resets;
    }

    if (why != SERVO_SOURCE_WHY_NONE) {
        s->good = false;
        s->id   = SERVO_SOURCE_PDMINI;
        s->why  = why;
        return s->id;
    }
    if (!s->good) {
        s->good    = true;
        s->good_ms = now_ms;
    }
    /* Condition 7.  Once the INA3221 is the meter the clock is not looked
     * at again, so it cannot wrap under a meter that stays. */
    if (s->id != SERVO_SOURCE_INA3221
        && (uint32_t)(now_ms - s->good_ms) < SERVO_SOURCE_SETTLE_MS) {
        s->id  = SERVO_SOURCE_PDMINI;
        s->why = SERVO_SOURCE_WHY_SETTLING;
        return s->id;
    }
    s->id  = SERVO_SOURCE_INA3221;
    s->why = SERVO_SOURCE_WHY_NONE;
    return s->id;
}

servo_source_id_t servo_source_step(servo_source_t *s, uint32_t now_ms,
                                    bool link_up, uint16_t minor,
                                    const sense_link_meter_t *m)
{
    if (s == NULL) {
        return SERVO_SOURCE_MODEL;
    }
    const servo_source_id_t was = s->id;
    if (step(s, now_ms, link_up, minor, m) != was) {
        ++s->changes;
        if (was == SERVO_SOURCE_INA3221) {
            s->dropped    = s->reset_step ? SERVO_SOURCE_WHY_RESET : s->why;
            s->dropped_at = s->changes;
        }
    }
    return s->id;
}

servo_source_why_t servo_source_dropped(const servo_source_t *s, uint32_t *at)
{
    if (s == NULL) {
        return SERVO_SOURCE_WHY_NONE;
    }
    if (at != NULL) {
        *at = s->dropped_at;
    }
    return s->dropped;
}

uint32_t servo_source_changes(const servo_source_t *s)
{
    return (s != NULL) ? s->changes : 0u;
}

servo_source_id_t servo_source_id(const servo_source_t *s)
{
    return (s != NULL) ? s->id : SERVO_SOURCE_MODEL;
}

servo_source_why_t servo_source_why(const servo_source_t *s)
{
    return (s != NULL) ? s->why : SERVO_SOURCE_WHY_NO_LINK;
}

uint32_t servo_source_event(servo_source_t *s, uint32_t now_ms)
{
    static const uint32_t k_order[] = { SERVO_SOURCE_EV_OLD,
                                        SERVO_SOURCE_EV_RESET };
    if (s == NULL || s->events == 0u
        || (s->event_given
            && (uint32_t)(now_ms - s->event_ms)
                   < SERVO_SOURCE_EVENT_GAP_MS)) {
        return 0u;
    }
    for (size_t i = 0u; i < sizeof(k_order) / sizeof(k_order[0]); ++i) {
        if ((s->events & k_order[i]) != 0u) {
            s->events &= ~k_order[i];
            s->event_given = true;
            s->event_ms    = now_ms;
            return k_order[i];
        }
    }
    s->events = 0u;                 /* no bit this build knows */
    return 0u;
}

void servo_source_event_back(servo_source_t *s, uint32_t ev)
{
    if (s == NULL || !s->up) {
        return;
    }
    s->events |= ev & (uint32_t)(SERVO_SOURCE_EV_OLD
                                 | SERVO_SOURCE_EV_RESET);
}
