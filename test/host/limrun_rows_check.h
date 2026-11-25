/**
 * @file limrun_rows_check.h
 * @brief LIMITnnn.CSV against the run that wrote it, for
 *        test_servo_limrun_rows.c and test_servo_limrun_hitec.c: the file read
 *        back with LOGS' reader (log_csv.h) and the rows' shape checked
 *        search by search.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "servo_limit.h"
#include "servo_limrun.h"

/**
 * @brief Read @p csv, LIMITnnn.CSV as the outbox handed it out, with LOGS'
 *        reader, and hold every row to the run's own: 11 columns by header
 *        name and unit, no time axis, one row a judged position, the
 *        numbers to the written precision, an endpoint on each BOUND row
 *        only.
 * @param r   The run that wrote it.
 * @param csv The file's text.
 */
static inline void rows_read_back(const servo_limrun_t *r, const char *csv)
{
    static const char *const k_name[11] = {
        "row", "search", "side", "pulse", "current", "voltage", "angle",
        "state", "free", "threshold", "end point",
    };
    static const char *const k_unit[11] = {
        "", "", "", "us", "A", "V", "deg", "", "A", "A", "us",
    };
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, csv, strlen(csv));
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, NULL, &a), LOG_OK);
    CHECK_EQ(a.delimiter, ';');
    CHECK_EQ(a.n_columns, 11);
    CHECK_EQ(a.row_count, (int)r->rows_n);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK(!a.cut_row);
    CHECK_EQ(a.time_index, -1);          /* `row` counts: no time axis */
    for (int i = 0; i < 11 && i < a.n_columns; ++i) {
        CHECK_STR_EQ(a.columns[i].name, k_name[i]);
        CHECK_STR_EQ(a.columns[i].unit, k_unit[i]);
    }
    CHECK(!a.columns[2].numeric);        /* LOW, HIGH */
    CHECK(!a.columns[7].numeric);        /* BASE, FREE, BOUND */
    unsigned bound = 0u;
    unsigned base = 0u;
    unsigned angles = 0u;
    for (unsigned k = 0u; k < r->rows_n; ++k) {
        bound += r->rows[k].state == SERVO_LIMRUN_ROW_BOUND ? 1u : 0u;
        base += r->rows[k].state == SERVO_LIMRUN_ROW_BASE ? 1u : 0u;
        angles += r->rows[k].angle_ok ? 1u : 0u;
    }
    CHECK_EQ(a.columns[10].non_empty, (int)bound);
    CHECK_EQ(a.columns[9].non_empty, (int)(r->rows_n - base));
    CHECK_EQ(a.columns[6].non_empty, (int)angles);

    static const int k_cols[2][4] = { { 1, 3, 4, 5 }, { 8, 9, 10, 6 } };
    log_data_t d;
    CHECK_EQ(log_csv_build(&src, &a, k_cols[0], 4, &d), LOG_OK);
    CHECK_EQ(d.count, (int)r->rows_n);
    for (unsigned k = 0u; k < r->rows_n && (int)k < d.count; ++k) {
        const servo_limrun_row_t *w = &r->rows[k];
        CHECK_NEAR(d.value[0][k], (float)w->search, 1e-6);
        CHECK_NEAR(d.value[1][k], (float)w->pulse_us, 1e-6);
        CHECK_NEAR(d.value[2][k], (float)w->ma / 1000.0f, 1e-6);
        CHECK_NEAR(d.value[3][k], (float)(w->mv / 10u) / 100.0f, 1e-6);
    }
    log_data_free(&d);
    CHECK_EQ(log_csv_build(&src, &a, k_cols[1], 1, &d), LOG_OK);
    for (unsigned k = 0u; k < r->rows_n && (int)k < d.count; ++k) {
        CHECK_NEAR(d.value[0][k], (float)r->rows[k].free_ma / 1000.0f, 1e-6);
    }
    log_data_free(&d);
}

/**
 * @brief Hold the rows to the searches they came from: a search's rows in
 *        one stretch, one side and one search number each, from the center
 *        outward in 10us steps (the last step shorter at the travel's end),
 *        the first SERVO_LIMIT_BASELINE_STEPS BASE, a BOUND row only last,
 *        its threshold under its current and its endpoint 25us inside the
 *        last free row.
 * @param r      The run.
 * @param center The pulse every search starts at, us.
 * @return The number of searches the rows hold.
 */
static inline unsigned rows_shape(const servo_limrun_t *r, uint16_t center)
{
    unsigned searches = 0u;
    unsigned at = 0u;            /* the row in its search */
    for (unsigned k = 0u; k < r->rows_n; ++k) {
        const servo_limrun_row_t *w = &r->rows[k];
        const bool first = k == 0u || r->rows[k - 1u].state
                                          == SERVO_LIMRUN_ROW_BOUND
                           || w->pulse_us == center;
        if (first) {
            ++searches;
            at = 0u;
            CHECK_EQ(w->pulse_us, center);
        } else {
            const servo_limrun_row_t *p = &r->rows[k - 1u];
            CHECK_EQ(w->search, p->search);
            CHECK_EQ(w->side, p->side);
            const int step = (int)w->pulse_us - (int)p->pulse_us;
            CHECK(w->side == SERVO_LIMRUN_HI ? (step > 0 && step <= 10)
                                              : (step < 0 && step >= -10));
            CHECK(p->state != SERVO_LIMRUN_ROW_BOUND);
        }
        CHECK(w->search >= 1u && w->search <= 3u);
        CHECK_EQ(w->state == SERVO_LIMRUN_ROW_BASE,
                 at < SERVO_LIMIT_BASELINE_STEPS);
        CHECK_EQ(w->thr_ma == 0u, w->state == SERVO_LIMRUN_ROW_BASE);
        if (w->state == SERVO_LIMRUN_ROW_FREE) {
            CHECK(w->ma <= w->thr_ma);
            CHECK_EQ(w->end_us, 0u);
        }
        if (w->state == SERVO_LIMRUN_ROW_BOUND) {
            CHECK(w->ma >= w->thr_ma);   /* above it before the rounding */
            const int last_free = (int)r->rows[k - 1u].pulse_us;
            int end = w->side == SERVO_LIMRUN_HI ? last_free - 25
                                                 : last_free + 25;
            if (w->side == SERVO_LIMRUN_HI ? end < (int)center
                                           : end > (int)center) {
                end = (int)center;   /* servo_limit.c keeps it on its side */
            }
            CHECK_EQ(w->end_us, (uint16_t)end);
        }
        ++at;
    }
    return searches;
}
