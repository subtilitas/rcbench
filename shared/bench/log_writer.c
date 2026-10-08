/*
 * SPDX-License-Identifier: MIT
 */

#include "log_writer.h"

#include "supply.h"

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
    "esc (C);motor (C);charge (mAh);energy (Wh);"
    "ina voltage (V);ina current (A);esc current (A);window;"
    "ch1 current (A);ch1 max (A);ch1 voltage (V);"
    "ch2 current (A);ch2 max (A);ch2 voltage (V);"
    "ch3 current (A);ch3 max (A);ch3 voltage (V)\n";

/* A supply run's file: what was asked beside what was delivered, and which
 * of the two the supply was holding. */
static const char *const k_supply_header =
    "time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;"
    "charge (mAh);energy (Wh)\n";

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

/*
 * Tell the sink to keep what it has been given.
 *
 * A writer that has already failed is refused before anything is attempted.
 * The file has a hole in it from the row that failed, and a flush that
 * succeeds after that would clear the pending count and report a commit --
 * which is a caller being told an incomplete file is safely on the card.
 * Once failed, always failed; the file is closed and said to be short.
 *
 * Nothing to commit is success, not a transaction: on a card an empty commit
 * costs a directory write and buys nothing.
 */
static bool commit(log_writer_t *w)
{
    if (w->failed) {
        return false;
    }
    if (w->pending == 0u) {
        return true;
    }
    if (w->sink.flush != NULL && !w->sink.flush(w->sink.ctx)) {
        w->failed = true;
        return false;
    }
    w->pending     = 0u;
    w->committed_s = w->last_s;
    return true;
}

bool log_writer_commit(log_writer_t *w)
{
    if (w == NULL) {
        return false;
    }
    return commit(w);
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

/*
 * The end of every row, whichever kind: the line goes out, the counts move,
 * and the commit comes on rows or on the run's own clock, whichever is first.
 * The clock bound is what holds when rows arrive slower than 20 Hz -- the far
 * end answering every other poll, say -- where waiting for
 * LOG_WRITER_FLUSH_ROWS would leave more than a second of run at risk.
 */
static bool finish_row(log_writer_t *w, float t_s, char *line, int n)
{
    line[n++] = '\n';
    if (!put(w, line, (size_t)n)) {
        return false;
    }
    ++w->rows;
    ++w->pending;
    w->last_s = t_s;
    if (w->pending >= LOG_WRITER_FLUSH_ROWS
        || (t_s - w->committed_s) >= LOG_WRITER_FLUSH_S) {
        return commit(w);
    }
    return true;
}

/*
 * One cell of a row: the separator, then the value if @p present.  False
 * once the row no longer fits, checked before anything is written past what
 * the last cell left: snprintf answers the length it wanted, not the one it
 * had, so an offset taken from it unchecked points past the line.
 */
static bool cell(char *line, size_t cap, int *n, bool present, float v,
                 int decimals)
{
    if (*n < 0 || (size_t)*n + 2u >= cap) {
        return false;
    }
    line[(*n)++] = LOG_WRITER_SEP;
    if (present) {
        const int k = fmt(line + *n, cap - (size_t)*n, v, decimals);
        if (k < 0 || (size_t)*n + (size_t)k >= cap) {
            return false;
        }
        *n += k;
    }
    return true;
}

static bool text_cell(char *line, size_t cap, int *n, const char *text)
{
    if (*n < 0 || (size_t)*n + 2u >= cap) {
        return false;
    }
    line[(*n)++] = LOG_WRITER_SEP;
    const int k = snprintf(line + *n, cap - (size_t)*n, "%s", text);
    if (k < 0 || (size_t)*n + (size_t)k >= cap) {
        return false;
    }
    *n += k;
    return true;
}

bool log_writer_supply_row(log_writer_t *w, float t_s,
                           const supply_state_t *s)
{
    if (w == NULL || s == NULL) {
        return false;
    }
    if (!w->header_done) {
        w->header_done = true;   /* one attempt per file, as the bench's */
        if (!put(w, k_supply_header, strlen(k_supply_header))) {
            return false;
        }
    }
    if (w->failed) {
        return false;
    }
    char line[160];
    int n = fmt(line, sizeof(line), t_s, 3);
    if (n <= 0) {
        return false;   /* a row with no time is not a row */
    }
    const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    const bool v_ok = (s->ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = (s->ok & SUPPLY_OK_CURRENT) != 0u;
    const char *mode = (s->mode == SUPPLY_MODE_CC) ? "CC"
                       : (s->mode == SUPPLY_MODE_CV) ? "CV" : "OFF";
    /* What was asked is always known; what arrived only when it did.  A row
     * that does not fit -- a value no supply gives, from a driver that has
     * gone wrong -- fails the log as an oversized bench row does. */
    const size_t cap = sizeof(line);
    const bool fits =
        cell(line, cap, &n, true, s->set_v, 2)
        && cell(line, cap, &n, v_ok, s->v, 2)
        && cell(line, cap, &n, true, s->set_i, 2)
        && cell(line, cap, &n, i_ok, s->i, 3)
        && cell(line, cap, &n, (s->ok & both) == both, s->p, 2)
        && text_cell(line, cap, &n, s->online ? mode : "")
        && cell(line, cap, &n, (s->counted & BENCH_COUNTED_CHARGE) != 0u,
                s->charge_mah, 0)
        && cell(line, cap, &n, (s->counted & BENCH_COUNTED_ENERGY) != 0u,
                s->energy_wh, 2);
    if (!fits || (size_t)n + 2u >= cap) {
        w->failed = true;
        return false;
    }
    return finish_row(w, t_s, line, n);
}

bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b)
{
    if (w == NULL || b == NULL) {
        return false;
    }
    if (!w->header_done && !log_writer_header(w)) {
        return false;
    }

    /*
     * Each measured column carries the flag that says whether anything
     * measured it.  A field with no flag set is written empty rather than as
     * a number: log_parse_with() refuses an empty cell instead of guessing,
     * so an empty column reads as absent, while a 0 reads as a measurement.
     * The bench has quantities nothing measures -- a motor temperature has no
     * sensor at all -- and a run's permanent record must not report them as
     * zero any more than the screen may draw them as zero.
     *
     * Power carries both halves because it is their product.  Charge and
     * energy carry what the panel counted (bench_state_t.counted): a run
     * with no current to count has no consumption to record, and its record
     * must not show 0 mAh that nothing counted.
     */
    static const struct {
        int decimals; size_t offset; uint16_t flag; uint8_t counted;
    } k_cols[] = {
        { 2, offsetof(bench_state_t, voltage),    LINK_BN_VOLTAGE_OK, 0u },
        { 2, offsetof(bench_state_t, current),    LINK_BN_CURRENT_OK, 0u },
        { 0, offsetof(bench_state_t, power),
             (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK), 0u },
        { 0, offsetof(bench_state_t, rpm),        LINK_BN_RPM_OK, 0u },
        { 1, offsetof(bench_state_t, temp_esc),   LINK_BN_TEMP_OK, 0u },
        { 1, offsetof(bench_state_t, temp_motor), LINK_BN_TEMP_MOT_OK, 0u },
        { 0, offsetof(bench_state_t, charge_mah), 0u, BENCH_COUNTED_CHARGE },
        { 2, offsetof(bench_state_t, energy_wh),  0u, BENCH_COUNTED_ENERGY },
    };

    /* In the writer, not on the stack: 22 cells, every one checked
     * against the end. */
    char *line = w->line;
    const size_t cap = sizeof(w->line);
    int n = fmt(line, cap, t_s, 3);
    if (n <= 0) {
        return false;   /* a row with no time is not a row */
    }
    for (size_t i = 0; i < sizeof(k_cols) / sizeof(k_cols[0]); ++i) {
        if ((size_t)n + 2u >= cap) {
            w->failed = true;
            return false;
        }
        line[n++] = LOG_WRITER_SEP;
        const uint16_t need = k_cols[i].flag;
        if (need != 0u && (b->flags & need) != need) {
            continue;               /* nothing measured it: an empty cell */
        }
        if (k_cols[i].counted != 0u
            && (b->counted & k_cols[i].counted) == 0u) {
            continue;               /* nothing counted it: an empty cell */
        }
        float v;
        memcpy(&v, (const char *)b + k_cols[i].offset, sizeof(v));
        n += fmt(line + n, cap - (size_t)n, v, k_cols[i].decimals);
    }
    /*
     * Where the voltage and current came from, beside them: the INA228's
     * own, empty while it is not BENCH's source, and the ESC's own current
     * whichever page carried it, so a run with both says what the ESC
     * claimed against what was measured.
     */
    float ina_v = 0.0f;
    float ina_i = 0.0f;
    float esc_i = 0.0f;
    const bool have_v   = bench_state_ina_voltage(b, &ina_v);
    const bool have_i   = bench_state_ina_current(b, &ina_i);
    const bool have_esc = bench_state_esc_current(b, &esc_i);
    bool fits = cell(line, cap, &n, have_v, ina_v, 2)
                && cell(line, cap, &n, have_i, ina_i, 2)
                && cell(line, cap, &n, have_esc, esc_i, 2);
    /*
     * The INA3221's window, on the row that first carries it and on no
     * other: its number, then per channel the mean and highest current and
     * the lowest bus voltage, empty for a channel the window has no
     * readings of.  A row with no new window has these cells empty.
     */
    const bool win = b->servo_new;
    fits = fits && cell(line, cap, &n, win, (float)b->servo_window, 0);
    for (unsigned c = 0u; c < 3u; ++c) {
        const bool ch = win && (b->servo_ok & (1u << c)) != 0u;
        fits = fits
               && cell(line, cap, &n, ch, (float)b->servo_mean_ma[c] / 1000.0f, 3)
               && cell(line, cap, &n, ch, (float)b->servo_max_ma[c] / 1000.0f, 3)
               && cell(line, cap, &n, ch, (float)b->servo_min_mv[c] / 1000.0f, 3);
    }
    if (!fits || (size_t)n + 2u >= cap) {
        w->failed = true;
        return false;
    }
    return finish_row(w, t_s, line, n);
}
