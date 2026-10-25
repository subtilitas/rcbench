/**
 * @file
 * @brief Stick programming: the beep detector.
 *
 * It turns the supply readings into pulses and the pulses into groups.  See
 * esc_stick.h, and esc_stick.c for the rules that make a count safe.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick.h"
#include "esc_stick_int.h"

#include <string.h>

void esc_det_init(esc_det_t *d, const esc_stick_timing_t *t,
                  uint8_t long_equals_short)
{
    if (d == NULL || t == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->t = *t;
    d->long_equals_short = long_equals_short;
}

/* Forget any group under way, as if the line had been silent. */
void esc_det_drop_group(esc_det_t *d)
{
    d->high = false;
    d->group_open = false;
    d->group_bad = false;
    d->shorts = 0u;
    d->longs = 0u;
    d->n_high = 0u;
    d->n_low = 0u;
    d->stuck_told = false;
}

/* Floor only: no pulses, no groups. */
void esc_det_floor_only(esc_det_t *d)
{
    d->counting = false;
    esc_det_drop_group(d);
}

void esc_det_count(esc_det_t *d)
{
    /* Quiet since the last pulse ended, when one did and nothing has risen
     * since: then the gap already heard counts toward the quiet. */
    const bool quiet = d->counting && !d->high && d->fell;
    const uint32_t from = d->fall_ms;
    d->counting = true;
    esc_det_drop_group(d);
    d->wait_quiet = true;
    d->quiet_known = quiet;
    d->quiet_from = from;
    d->busy = false;
}

int32_t esc_det_floor_ma(const esc_det_t *d)
{
    return d->floor_x16 / 16;
}

/**
 * @brief How many readings a span of @p ms must hold, at the longest interval
 *        seen that was not late.  0 while no interval is known.
 *
 * @param d   The beep detector.
 * @param ms  Span, ms.
 * @return The readings the span must hold, or 0 while no interval is known.
 */
static uint32_t readings_in(const esc_det_t *d, uint32_t ms)
{
    return (d->iv_max_ms == 0u) ? 0u : ms / d->iv_max_ms;
}

/**
 * @brief Close the pulse that has just fallen and judge its length.
 *
 * Counts the pulse as a short or a long beep, or marks the group bad when it
 * is no beep.
 *
 * @param d     The beep detector.
 * @param t_ms  Time of the first low reading after the pulse, ms.
 */
static void close_pulse(esc_det_t *d, uint32_t t_ms)
{
    d->high = false;
    d->fell = true;
    d->fall_ms = t_ms;
    d->n_low = 1u;
    /*
     * The beep began after the reading before the rise and ended before this
     * one: between the two spans lies the true length.  The shorter span is
     * never longer than the beep, so a pulse is judged too long on it alone.
     */
    const uint32_t lo = d->last_high_ms - d->rise_ms;
    const uint32_t hi = t_ms - d->before_rise_ms;
    const uint32_t est = (lo + hi) / 2u;
    if (d->n_high < readings_in(d, d->t.beep_min_ms)
        || lo > d->t.long_max_ms) {
        d->group_bad = true;               /* shorter or longer than a beep */
    } else if (est >= d->t.long_ms) {
        if (d->long_equals_short == 0u || d->shorts > 0u) {
            d->group_bad = true;           /* no long here, or after a short */
        } else if (d->longs < 255u) {
            d->longs++;
        }
    } else if (d->shorts < 255u) {
        d->shorts++;
    }
}

esc_det_event_t esc_det_reading(esc_det_t *d, uint32_t t_ms, int32_t ma,
                                bool late)
{
    if (d == NULL) {
        return ESC_DET_NONE;
    }
    if (d->have_prev && !late) {
        const uint32_t iv = t_ms - d->prev_ms;
        if (iv > d->iv_max_ms) {
            d->iv_max_ms = iv;
        }
    }
    const uint32_t before = d->have_prev ? d->prev_ms : t_ms;
    d->have_prev = true;
    d->prev_ms = t_ms;

    const int32_t x16 = ma * 16;
    if (!d->seeded) {
        d->seeded = true;
        d->floor_x16 = x16;
    }
    if (!d->counting) {
        /* The floor while the ESC starts: the lowest current seen, which a
         * beep cannot raise. */
        if (x16 < d->floor_x16) {
            d->floor_x16 = x16;
        }
        return ESC_DET_NONE;
    }

    if (late && (d->group_open || d->high)) {
        d->group_bad = true;
    }
    const int32_t on  = d->floor_x16 + (int32_t)d->t.threshold_ma * 16;
    const int32_t off = on - (int32_t)d->t.hysteresis_ma * 16;

    if (d->wait_quiet) {
        /* Anything over the release, or a late reading that may hide a
         * beep, is not quiet. */
        if (x16 > off || late) {
            d->quiet_known = false;
            if (x16 > off && !d->busy) {
                d->busy = true;
                d->busy_from = t_ms;
            } else if (x16 > off && !d->stuck_told
                       && t_ms - d->busy_from > 2u * d->t.long_max_ms) {
                d->stuck_told = true;
                return ESC_DET_STUCK;
            }
            return ESC_DET_NONE;
        }
        d->busy = false;
        d->floor_x16 += (x16 - d->floor_x16) / 16;
        if (!d->quiet_known) {
            d->quiet_known = true;
            d->quiet_from = t_ms;
        }
        if (t_ms - d->quiet_from >= d->t.group_gap_ms) {
            d->wait_quiet = false;
        }
        return ESC_DET_NONE;
    }

    if (d->high) {
        if (x16 < off) {
            close_pulse(d, t_ms);
            return ESC_DET_PULSE;
        }
        d->n_high++;
        d->last_high_ms = t_ms;
        if (!d->stuck_told && t_ms - d->rise_ms > 2u * d->t.long_max_ms) {
            d->stuck_told = true;
            return ESC_DET_STUCK;
        }
        return ESC_DET_NONE;
    }
    if (x16 > on) {
        if (d->group_open) {
            if (d->n_low < readings_in(d, d->t.gap_min_ms)) {
                d->group_bad = true;       /* a gap shorter than any gap */
            }
        } else {
            d->group_open = true;
            d->group_bad = late;
            d->shorts = 0u;
            d->longs = 0u;
        }
        d->high = true;
        d->rise_ms = t_ms;
        d->last_high_ms = t_ms;
        d->before_rise_ms = before;
        d->n_high = 1u;
        d->stuck_told = false;
        return ESC_DET_NONE;
    }
    /* Quiet: the floor follows the idle current, slowly. */
    d->floor_x16 += (x16 - d->floor_x16) / 16;
    d->n_low++;
    if (d->group_open && t_ms - d->fall_ms >= d->t.group_gap_ms) {
        d->group_open = false;
        const uint32_t n = (uint32_t)d->longs * d->long_equals_short
                           + d->shorts;
        d->count = (uint8_t)((n > 255u) ? 255u : n);
        d->valid = !d->group_bad && n > 0u && n <= 255u;
        d->shorts = 0u;
        d->longs = 0u;
        d->group_bad = false;
        return ESC_DET_GROUP;
    }
    return ESC_DET_NONE;
}
