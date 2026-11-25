/**
 * @file
 * @brief The limit run's judged positions: one row of LIMITnnn.CSV each, the
 *        pulse, the current, the rail voltage and the angle of every position
 *        a search judged, and how the search judged it.  See servo_limrun.h,
 *        SERVO_LIMRUN_CSV_HEAD and servo_limrun_csv_line().
 *
 * The CSV is written in English, as the automatic test's (servo_str.c), with
 * `;` between cells and a decimal point, as every bench CSV.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"

#include <stdio.h>

#include "servo_limrun_int.h"

/**
 * @brief A current in A as whole mA, held to 0 to 65535mA.
 * @param a Current, A.
 * @return The current, mA.
 */
static uint16_t ma_of(double a)
{
    const double ma = a * 1000.0 + 0.5;
    return (uint16_t)(ma > 65535.0 ? 65535.0 : ma < 0.0 ? 0.0 : ma);
}

/**
 * @brief The search a phase is: 1 the first, 2 one after a write, 3 the last
 *        check.
 * @param ph The phase the search runs in.
 * @return 1, 2 or 3.
 */
static uint8_t search_of(servo_limrun_phase_t ph)
{
    switch (ph) {
    case SERVO_LIMRUN_PH_PROBE: return 2u;
    case SERVO_LIMRUN_PH_CHECK: return 3u;
    default:                    return 1u;
    }
}

void limrun_row_take(servo_limrun_t *r, uint16_t pulse_us, uint16_t mv)
{
    const servo_limit_t *lf = &r->lf;
    const bool base = r->judged < SERVO_LIMIT_BASELINE_STEPS;
    if (r->judged < UINT8_MAX) {
        ++r->judged;
    }
    if (r->rows_n >= SERVO_LIMRUN_ROWS_MAX) {
        ++r->rows_past;
        return;
    }
    servo_limrun_row_t *w = &r->rows[r->rows_n++];
    const double free_a = (double)lf->baseline_a;
    /* The bound test of servo_limit.c's judge(): above both the ratio and
     * the margin, so the larger of the two is the threshold. */
    const double by_ratio = free_a * (double)lf->cfg.knee_ratio;
    const double by_margin = free_a + (double)lf->cfg.knee_margin_a;
    w->pulse_us = pulse_us;
    w->ma       = ma_of(lf->samples > 0u ? lf->sum_a / (double)lf->samples
                                         : 0.0);
    w->mv       = mv;
    w->free_ma  = ma_of(free_a);
    w->thr_ma   = base ? 0u
                       : ma_of(by_ratio > by_margin ? by_ratio : by_margin);
    w->state    = (uint8_t)(lf->state == SERVO_LIMIT_FOUND
                                ? SERVO_LIMRUN_ROW_BOUND
                            : base ? SERVO_LIMRUN_ROW_BASE
                                   : SERVO_LIMRUN_ROW_FREE);
    w->end_us   = lf->state == SERVO_LIMIT_FOUND ? lf->limit_us : 0u;
    w->search   = search_of(r->phase);
    w->side     = r->side;
    w->angle_ok = r->enc_valid;
    w->deg      = r->enc_valid ? r->enc_deg : 0.0f;
}

bool servo_limrun_csv_line(const servo_limrun_t *r, unsigned idx, char *buf,
                           size_t n)
{
    if (r == NULL || buf == NULL || n == 0u
        || r->state != SERVO_LIMRUN_DONE) {
        return false;
    }
    if (idx == 0u) {
        snprintf(buf, n, "%s", SERVO_LIMRUN_CSV_HEAD);
        return true;
    }
    if (idx > r->rows_n) {
        return false;
    }
    static const char *const k_state[] = { "BASE", "FREE", "BOUND" };
    const servo_limrun_row_t *w = &r->rows[idx - 1u];
    char deg[16] = "";
    char thr[16] = "";
    char end[8] = "";
    if (w->angle_ok) {
        snprintf(deg, sizeof(deg), "%.2f", (double)w->deg);
    }
    if (w->state != SERVO_LIMRUN_ROW_BASE) {
        snprintf(thr, sizeof(thr), "%.3f", (double)w->thr_ma / 1000.0);
    }
    if (w->state == SERVO_LIMRUN_ROW_BOUND) {
        snprintf(end, sizeof(end), "%u", (unsigned)w->end_us);
    }
    snprintf(buf, n, "%u;%u;%s;%u;%.3f;%.2f;%s;%s;%.3f;%s;%s", idx,
             (unsigned)w->search, w->side == SERVO_LIMRUN_HI ? "HIGH" : "LOW",
             (unsigned)w->pulse_us, (double)w->ma / 1000.0,
             (double)w->mv / 1000.0, deg,
             k_state[w->state <= SERVO_LIMRUN_ROW_BOUND ? w->state : 0u],
             (double)w->free_ma / 1000.0, thr, end);
    return true;
}
