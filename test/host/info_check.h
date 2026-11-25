/**
 * @file info_check.h
 * @brief A run's CSV file against its `info` cell, for the suites of the six
 *        kinds of file: read with LOGS' reader at this build's column
 *        ceiling and at firmware 0.23.17's (25), and the cell read back
 *        with log_info_read().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "log_info.h"

/** @brief The column ceiling of firmware 0.23.17's LOGS. */
#define INFO_OLD_COLUMNS 25

/**
 * @brief Analyse @p csv with the column ceiling @p ceiling.
 * @param csv     The file's text.
 * @param ceiling log_csv_opts_t.max_columns.
 * @param a       Receives the analysis.
 * @return log_csv_analyse()'s answer.
 */
static inline log_err_t info_analyse(const char *csv, int ceiling,
                                     log_analysis_t *a)
{
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, csv, strlen(csv));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    opts.max_columns = ceiling;
    return log_csv_analyse(&src, &opts, a);
}

/**
 * @brief Read @p csv back: @p columns columns, `info` the last, a text
 *        column with one cell (the first data row's) when @p filled, no
 *        ragged row; refused by a 25-column reader exactly when @p columns
 *        is over 25; and its cell into @p got.
 * @param csv     The file's text.
 * @param columns The columns it must have.
 * @param filled  The first row's cell is not empty.
 * @param got     Receives log_info_read()'s description.
 */
static inline void info_read_back(const char *csv, int columns, bool filled,
                                  log_info_t *got)
{
    static log_analysis_t a;
    CHECK_EQ(info_analyse(csv, 0, &a), LOG_OK);
    CHECK_EQ(a.delimiter, ';');
    CHECK_EQ(a.n_columns, columns);
    CHECK_EQ(a.ragged_rows, 0);
    CHECK(a.row_count > 0);
    if (a.n_columns == columns) {
        const log_column_t *c = &a.columns[columns - 1];
        CHECK_STR_EQ(c->name, LOG_INFO_HEAD);
        CHECK(!c->numeric);
        CHECK_EQ(c->non_empty, filled ? 1 : 0);
    }
    static log_analysis_t old;
    CHECK_EQ(info_analyse(csv, INFO_OLD_COLUMNS, &old),
             columns > INFO_OLD_COLUMNS ? LOG_ERR_TOO_MANY_COLUMNS : LOG_OK);
    if (columns <= INFO_OLD_COLUMNS) {
        CHECK_EQ(old.n_columns, columns);
        CHECK_EQ(old.row_count, a.row_count);
    }
    static log_source_t src;
    static log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, csv, strlen(csv));
    CHECK_EQ(log_info_read(&src, ';', got), filled);
}

/**
 * @brief The longest description: every field at its longest, each value
 *        holding a character the escape rule replaces.
 * @param in Receives it.
 */
static inline void info_longest(log_info_t *in)
{
    memset(in, 0, sizeof(*in));
    snprintf(in->dut, sizeof(in->dut), "%s", "MG90S&METAL=GEAR;NO\"123");
    snprintf(in->note, sizeof(in->note), "%s", "greased, 200g on horn 4");
    snprintf(in->date, sizeof(in->date), "%s", "2026-11-24T10:31");
    snprintf(in->fw, sizeof(in->fw), "%s", "100.200.300-rc9");
    snprintf(in->servo, sizeof(in->servo), "%s", "KST X20-8.4 HV 333H");
    snprintf(in->meter, sizeof(in->meter), "%s", "INA3221 0x41 CH2 XY");
    snprintf(in->angle, sizeof(in->angle), "%s", "IMU X+ YAWS");
}

/**
 * @brief @p got against @p want as written: each field cleaned by the
 *        escape rule.
 * @param got  The description read back.
 * @param want The description written.
 */
static inline void info_same(const log_info_t *got, const log_info_t *want)
{
    char w[LOG_INFO_TEXT_MAX];
    log_info_clean(w, sizeof(w), want->dut);
    CHECK_STR_EQ(got->dut, w);
    log_info_clean(w, sizeof(w), want->note);
    CHECK_STR_EQ(got->note, w);
    log_info_clean(w, sizeof(w), want->date);
    CHECK_STR_EQ(got->date, w);
    log_info_clean(w, sizeof(w), want->fw);
    CHECK_STR_EQ(got->fw, w);
    log_info_clean(w, sizeof(w), want->servo);
    CHECK_STR_EQ(got->servo, w);
    log_info_clean(w, sizeof(w), want->meter);
    CHECK_STR_EQ(got->meter, w);
    log_info_clean(w, sizeof(w), want->angle);
    CHECK_STR_EQ(got->angle, w);
}
