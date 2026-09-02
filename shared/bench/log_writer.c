/*
 * SPDX-License-Identifier: MIT
 */

#include "log_writer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Units in the header row, not on a row of their own.  The reader counts a
 * separate units row as data, so a file in that shape reports its first row
 * as unreadable cells.
 */
static const char *const k_header =
    "time (s);voltage (V);current (A);power (W);rpm (rpm);"
    "esc (C);motor (C);charge (mAh);energy (Wh)\n";

static bool put(log_writer_t *w, const char *s, size_t n)
{
    if (w->failed || w->sink.write == NULL) {
        w->failed = true;
        return false;
    }
    if (w->sink.write(w->sink.ctx, s, n) < (int)n) {
        w->failed = true;
        return false;
    }
    return true;
}

void log_writer_init(log_writer_t *w, const log_sink_t *sink)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof(*w));
    if (sink != NULL) {
        w->sink = *sink;
    }
}

bool log_writer_header(log_writer_t *w)
{
    if (w == NULL || w->header_done) {
        return w != NULL && !w->failed;
    }
    w->header_done = true;   /* set before the write: one attempt per file */
    return put(w, k_header, strlen(k_header));
}

/* A reading that is not finite is written as an empty cell, which the reader
 * counts as absent.  "nan" is not a number to the reader, and 0 is a wrong
 * value. */
static int fmt(char *out, size_t cap, float v, int decimals)
{
    if (!isfinite(v)) {
        return 0;
    }
    return snprintf(out, cap, "%.*f", decimals, (double)v);
}

bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b)
{
    if (w == NULL || b == NULL) {
        return false;
    }
    if (!w->header_done && !log_writer_header(w)) {
        return false;
    }

    static const struct { int decimals; size_t offset; } k_cols[] = {
        { 2, offsetof(bench_state_t, voltage) },
        { 2, offsetof(bench_state_t, current) },
        { 0, offsetof(bench_state_t, power) },
        { 0, offsetof(bench_state_t, rpm) },
        { 1, offsetof(bench_state_t, temp_esc) },
        { 1, offsetof(bench_state_t, temp_motor) },
        { 0, offsetof(bench_state_t, charge_mah) },
        { 2, offsetof(bench_state_t, energy_wh) },
    };

    char line[160];
    int n = fmt(line, sizeof(line), t_s, 3);
    if (n <= 0) {
        return false;   /* a row with no time is not a row */
    }
    for (size_t i = 0; i < sizeof(k_cols) / sizeof(k_cols[0]); ++i) {
        if ((size_t)n + 2u >= sizeof(line)) {
            w->failed = true;
            return false;
        }
        line[n++] = LOG_WRITER_SEP;
        float v;
        memcpy(&v, (const char *)b + k_cols[i].offset, sizeof(v));
        n += fmt(line + n, sizeof(line) - (size_t)n, v, k_cols[i].decimals);
    }
    line[n++] = '\n';

    if (!put(w, line, (size_t)n)) {
        return false;
    }
    ++w->rows;
    return true;
}
