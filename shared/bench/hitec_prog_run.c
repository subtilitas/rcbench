/**
 * @file
 * @brief The run seam of the programmer for servos by Hitec: a run's servo
 *        in travel units and its writes of the two end points.  See
 *        hitec_prog_run.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_run.h"

#include <stdio.h>
#include <string.h>

#include "hitec_prog_int.h"

/** @brief The soft rules a run's start hold confirms for the run's edits. */
#define RUN_RULES \
    ((uint16_t)(HITEC_RULE_BIT(HITEC_RULE_END) | HITEC_RULE_BIT(HITEC_RULE_CENTRE)))

void hitec_prog_run_begin(hitec_prog_t *p)
{
    p->run = true;
    p->run_wrote = false;
}

void hitec_prog_run_end(hitec_prog_t *p)
{
    p->run = false;
    p->run_wrote = false;
    (void)hitec_prog_write_enable(p, false);
    /* The draft is the servo read again and every confirmation ends: an
     * edit after the run asks as any edit does. */
    hitec_prog_discard(p);
}

/**
 * @brief The two end point parameters of a series.
 *
 * @param series hitec_family_t.
 * @param side   0 left, 1 right.
 * @return hitec_param_t.
 */
static uint8_t end_id(uint8_t series, unsigned side)
{
    if (series == (uint8_t)HITEC_D) {
        return side == 0u ? (uint8_t)HITEC_P_D_LEFT_END
                          : (uint8_t)HITEC_P_D_RIGHT_END;
    }
    return side == 0u ? (uint8_t)HITEC_P_C_LEFT_END
                      : (uint8_t)HITEC_P_C_RIGHT_END;
}

/**
 * @brief A parameter of the servo read, decoded.
 *
 * @param p     The programmer.
 * @param id    hitec_param_t.
 * @param value Receives the value.
 * @return true when it decodes (HITEC_GET_OK).
 */
static bool get(const hitec_prog_t *p, uint8_t id, int32_t *value)
{
    hitec_value_t v;

    if (hitec_param_get(&p->servo, hitec_prog_layout(p), id, &v)
        != HITEC_GET_OK) {
        return false;
    }
    *value = v.value;
    return true;
}

/**
 * @brief The D series half of hitec_prog_run_servo(): NEUTRAL, the two end
 *        points and the travel the hard rules take.
 *
 * @param p   The programmer.
 * @param end The two end points, registers.
 * @param out The servo.
 * @return true when NEUTRAL decodes and lies between the end points.
 */
static bool d_servo(const hitec_prog_t *p, const int32_t end[2],
                    hitec_run_servo_t *out)
{
    const hitec_param_info_t *info =
        hitec_param(hitec_prog_layout(p), (uint8_t)HITEC_P_D_LEFT_END);
    int32_t mid = 0;
    int32_t a;
    int32_t b;

    if (!get(p, (uint8_t)HITEC_P_D_NEUTRAL, &mid) || !(end[0] < mid)
        || !(mid < end[1])) {
        return false;
    }
    out->mid = (uint16_t)mid;
    out->value[0] = (uint16_t)(mid - end[0]);
    out->value[1] = (uint16_t)(end[1] - mid);
    /* An end point one count from NEUTRAL at the least, and inside the
     * register's range: the narrower side bounds both. */
    a = mid - info->min;
    b = info->max - mid;
    out->lo = 1u;
    out->hi = (uint16_t)(a < b ? a : b);
    return true;
}

bool hitec_prog_run_servo(const hitec_prog_t *p, hitec_run_servo_t *out)
{
    const hitec_image_t *img = hitec_prog_servo(p);
    int32_t end[2];
    int32_t dir = 0;
    uint8_t series;

    memset(out, 0, sizeof(*out));
    if (img == NULL) {
        return false;
    }
    series = img->series;
    out->series = series;
    (void)snprintf(out->backup, sizeof(out->backup), "%s",
                   hitec_prog_backup_name(p));
    for (unsigned s = 0; s < 2u; ++s) {
        if (!hitec_prog_editable(p, end_id(series, s))
            || !get(p, end_id(series, s), &end[s])) {
            return false;
        }
    }
    if (!get(p, series == (uint8_t)HITEC_D ? (uint8_t)HITEC_P_D_DIRECTION
                                           : (uint8_t)HITEC_P_C_DIRECTION,
             &dir)) {
        return false;
    }
    out->cw = dir == (int32_t)HITEC_CW;
    if (series == (uint8_t)HITEC_D) {
        out->ok = d_servo(p, end, out);
        return out->ok;
    }
    /* A classic end point is its raw cell, the range its row takes. */
    out->value[0] = (uint16_t)end[0];
    out->value[1] = (uint16_t)end[1];
    out->lo = (uint16_t)hitec_param(hitec_prog_layout(p), end_id(series, 0u))->min;
    out->hi = (uint16_t)hitec_param(hitec_prog_layout(p), end_id(series, 0u))->max;
    out->ok = true;
    return true;
}

uint16_t hitec_run_to_reg(const hitec_run_servo_t *sv, uint8_t side,
                          uint16_t travel)
{
    int32_t r;

    if (sv->series != (uint8_t)HITEC_D) {
        return travel;
    }
    r = side == 0u ? (int32_t)sv->mid - travel : (int32_t)sv->mid + travel;
    return (uint16_t)(r < 0 ? 0 : r > 0xFFFF ? 0xFFFF : r);
}

uint16_t hitec_run_to_travel(const hitec_run_servo_t *sv, uint8_t side,
                             uint16_t reg)
{
    int32_t t;

    if (sv->series != (uint8_t)HITEC_D) {
        return reg;
    }
    t = side == 0u ? (int32_t)sv->mid - reg : (int32_t)reg - sv->mid;
    return (uint16_t)(t < 0 ? 0 : t);
}

/**
 * @brief One end point into the draft, the run's consent taken for the
 *        rules a run confirms.
 *
 * @param p   The programmer.
 * @param id  hitec_param_t.
 * @param reg The register or cell.
 * @return true when the draft holds it.
 */
static bool run_edit(hitec_prog_t *p, uint8_t id, uint16_t reg)
{
    const hitec_prog_edit_t e = hitec_prog_edit(p, id, (int32_t)reg);

    if (e != HITEC_PROG_EDIT_CHECK) {
        return e == HITEC_PROG_EDIT_OK;
    }
    if ((hitec_prog_pending(p, NULL, NULL) & (uint16_t)~RUN_RULES) != 0u) {
        /* A rule the run's start does not confirm: to the run the value is
         * not writable. */
        hitec_prog_cancel(p);
        (void)hpg_say2(p, HITEC_PROG_MSG_EDIT_REFUSED,
                       (unsigned)HITEC_SET_ERR_RO, 0u);
        return false;
    }
    /* The run's start, held 2s, is the consent CHECK THIS CHANGE asks
     * for. */
    return hitec_prog_confirm(p) == HITEC_PROG_EDIT_OK;
}

hitec_prog_msg_id_t hitec_prog_run_write(hitec_prog_t *p, uint16_t left,
                                         uint16_t right)
{
    const uint16_t want[2] = { left, right };
    hitec_run_servo_t sv;
    hitec_prog_msg_id_t m;

    if (!p->run) {
        /* Only a run's start switches its writes on. */
        return hpg_say2(p, HITEC_PROG_MSG_WRITE_OFF, 0u, 0u);
    }
    if (!hitec_prog_run_servo(p, &sv)) {
        return hpg_say2(p, HITEC_PROG_MSG_NOT_READ, 0u, 0u);
    }
    for (unsigned s = 0; s < 2u; ++s) {
        if (want[s] < sv.lo || want[s] > sv.hi) {
            /* Refused, not clamped: the run asked for a value the servo
             * cannot take. */
            return hpg_say2(p, HITEC_PROG_MSG_EDIT_REFUSED,
                            (unsigned)HITEC_SET_ERR_RANGE, 0u);
        }
    }
    m = hitec_prog_write_enable(p, true);
    if (m != HITEC_PROG_MSG_NONE) {
        return m;
    }
    /* From here every way out but the write on its way switches the write
     * off again: a run that gives up holds no enabled write. */
    hitec_prog_discard(p);
    for (unsigned s = 0; s < 2u; ++s) {
        if (!run_edit(p, end_id(sv.series, s),
                      hitec_run_to_reg(&sv, (uint8_t)s, want[s]))) {
            /* The edit said why. */
            hitec_prog_discard(p);
            (void)hitec_prog_write_enable(p, false);
            return (hitec_prog_msg_id_t)hitec_prog_message(p)->id;
        }
    }
    m = hitec_prog_write(p);
    if (m != HITEC_PROG_MSG_NONE) {
        hitec_prog_discard(p);
        (void)hitec_prog_write_enable(p, false);
    }
    return m;
}

bool hitec_prog_run_values(const hitec_prog_t *p, uint16_t value[2])
{
    const unsigned id = hitec_prog_message(p)->id;
    hitec_run_servo_t sv;

    /* The servo read is the write's read-back while its message stands:
     * WRITTEN, or VERIFY_FAIL with the values the servo holds instead. */
    if (!p->run_wrote
        || (id != (unsigned)HITEC_PROG_MSG_WRITTEN
            && id != (unsigned)HITEC_PROG_MSG_VERIFY_FAIL)
        || !hitec_prog_run_servo(p, &sv)) {
        return false;
    }
    value[0] = sv.value[0];
    value[1] = sv.value[1];
    return true;
}
