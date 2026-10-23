/**
 * @file
 * @brief Writes a run as CSV rows to a sink, see log_writer.h.
 *
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
static const char *const k_head =
    "time (s);voltage (V);current (A);power (W);rpm (rpm);"
    "esc (C);motor (C);charge (mAh);energy (Wh);"
    "ina voltage (V);ina current (A);esc current (A);window;";
/* SERVO_SENSE's three channels, without SENSE_MAP. */
static const char *const k_ch123 =
    "ch1 current (A);ch1 max (A);ch1 voltage (V);"
    "ch2 current (A);ch2 max (A);ch2 voltage (V);"
    "ch3 current (A);ch3 max (A);ch3 voltage (V);";
static const char *const k_tail = "source\n";

/** @brief The supply figures of a SERVO run's row, by column (log_rail_t). */
enum {
    RAIL_NONE = 0,  /**< a column the bench sample fills */
    RAIL_V,         /**< voltage */
    RAIL_I,         /**< current */
    RAIL_P,         /**< power */
    RAIL_Q,         /**< charge */
    RAIL_E,         /**< energy */
};

/**
 * @brief The last column of both files: what produced the row's values.
 *
 * @param modelled true when the row's values are modelled.
 * @return LOG_WRITER_SRC_MODEL or LOG_WRITER_SRC_MEASURED.
 */
static const char *source_word(bool modelled)
{
    return modelled ? LOG_WRITER_SRC_MODEL : LOG_WRITER_SRC_MEASURED;
}

/* A supply run's file: what was asked beside what was delivered, and which
 * of the two the supply was holding. */
static const char *const k_supply_header =
    "time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;"
    "charge (mAh);energy (Wh);source\n";

/**
 * @brief A short write latches the writer, and the bytes the sink took stay in
 *        the file: nothing takes them back out of a card.
 *
 * Nothing is written after them either, a line feed least of all.  The row they
 * start has no line feed, and that is what the reader drops it by
 * (log_reader_row()): a row cut after "95" of "9554" has its fields and a
 * number that parses, and only the missing terminator tells it from a whole
 * row.
 *
 * @param w The writer.
 * @param s The bytes.
 * @param n The number of bytes.
 * @return true when the sink took all n bytes.
 */
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

/**
 * @brief Tell the sink to keep what it has been given.
 *
 * A writer that has already failed is refused before anything is attempted.
 * The file has a hole in it from the row that failed, and a flush that
 * succeeds after that would clear the pending count and report a commit --
 * which is a caller being told an incomplete file is safely on the card.
 * Once failed, always failed; the file is closed and said to be short.
 *
 * Nothing to commit is success, not a transaction: on a card an empty commit
 * costs a directory write and buys nothing.
 *
 * @param w The writer.
 * @return true on success or when nothing is pending.
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

void log_writer_set_cols(log_writer_t *w, const log_cols_t *cols)
{
    if (w == NULL || w->header_done) {
        return;
    }
    w->by_map = cols != NULL;
    memset(&w->cols, 0, sizeof(w->cols));
    if (cols == NULL) {
        return;
    }
    /* At most LOG_COLS_MAX groups, each on a channel there is. */
    const unsigned n = (cols->n > LOG_COLS_MAX) ? LOG_COLS_MAX : cols->n;
    for (unsigned i = 0u; i < n; ++i) {
        if (cols->ch[i] < BENCH_CHW_CHANNELS) {
            w->cols.ch[w->cols.n] = cols->ch[i];
            memcpy(w->cols.name[w->cols.n], cols->name[i],
                   sizeof(w->cols.name[0]));
            ++w->cols.n;
        }
    }
}

bool log_writer_header(log_writer_t *w)
{
    if (w == NULL || w->header_done) {
        return w != NULL && !w->failed;
    }
    w->header_done = true;   /* set before the write: one attempt per file */
    if (!put(w, k_head, strlen(k_head))) {
        return false;
    }
    if (!w->by_map) {
        return put(w, k_ch123, strlen(k_ch123))
               && put(w, k_tail, strlen(k_tail));
    }
    static const char *const k_what[3] = { "current (A)", "max (A)",
                                           "voltage (V)" };
    for (unsigned i = 0u; i < w->cols.n; ++i) {
        for (unsigned q = 0u; q < 3u; ++q) {
            /* A name of LOG_COL_NAME_MAX - 1 and " voltage (V);" fit. */
            char cell[LOG_COL_NAME_MAX + 16];
            (void)snprintf(cell, sizeof(cell), "%s %s;", w->cols.name[i],
                           k_what[q]);
            if (!put(w, cell, strlen(cell))) {
                return false;
            }
        }
    }
    return put(w, k_tail, strlen(k_tail));
}

/**
 * @brief A reading that is not finite is written as an empty cell, which the
 *        reader counts as absent.
 *
 * "nan" is not a number to the reader, and 0 is a wrong
 * value.
 *
 * @param out      Receives the text.
 * @param cap      The size of out, in bytes.
 * @param v        The reading.
 * @param decimals The digits after the point.
 * @return The number of characters written; 0 for a value that is not finite.
 */
static int fmt(char *out, size_t cap, float v, int decimals)
{
    if (!isfinite(v)) {
        return 0;
    }
    return snprintf(out, cap, "%.*f", decimals, (double)v);
}

/**
 * @brief The end of every row, whichever kind: the line goes out, the counts
 *        move, and the commit comes on rows or on the run's own clock,
 *        whichever is first.
 *
 * The clock bound is what holds when rows arrive slower than 20 Hz -- the far
 * end answering every other poll, say -- where waiting for
 * LOG_WRITER_FLUSH_ROWS would leave more than a second of run at risk.
 *
 * @param w    The writer.
 * @param t_s  The time of the row, in s.
 * @param line The row text.
 * @param n    The length of line, in bytes.
 * @return true when the row is written.
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

/**
 * @brief One cell of a row: the separator, then the value if @p present.
 *
 * False once the row no longer fits, checked before anything is written past
 * what the last cell left: snprintf answers the length it wanted, not the one
 * it had, so an offset taken from it unchecked points past the line.
 *
 * @param line     The row text.
 * @param cap      The size of line, in bytes.
 * @param n        The length of the row so far; updated.
 * @param present  true when v is written.
 * @param v        The value.
 * @param decimals The digits after the point.
 * @return true when the cell fits.
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

/**
 * @brief Appends one text cell to a row.
 *
 * @param line The row text.
 * @param cap  The size of line, in bytes.
 * @param n    The length of the row so far; updated.
 * @param text The text.
 * @return true when the cell fits.
 */
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
                s->energy_wh, 2)
        && text_cell(line, cap, &n, source_word(s->modelled));
    if (!fits || (size_t)n + 2u >= cap) {
        w->failed = true;
        return false;
    }
    return finish_row(w, t_s, line, n);
}

/**
 * @brief The cell of a supply figure on a SERVO run's row (RAIL_*), from
 *        @p r: false when @p r has no reading or total of it.
 *
 * @param r     The supply's reading and totals, or NULL.
 * @param which RAIL_V to RAIL_E.
 * @param v     Receives the figure when true.
 * @return true when the figure is a reading or a counted total.
 */
static bool rail_figure(const log_rail_t *r, unsigned which, float *v)
{
    if (r == NULL) {
        return false;
    }
    const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    switch (which) {
    case RAIL_V:
        *v = r->v;
        return (r->ok & SUPPLY_OK_VOLTAGE) != 0u;
    case RAIL_I:
        *v = r->i;
        return (r->ok & SUPPLY_OK_CURRENT) != 0u;
    case RAIL_P:
        *v = r->p;
        return (r->ok & both) == both;
    case RAIL_Q:
        *v = r->charge_mah;
        return (r->counted & BENCH_COUNTED_CHARGE) != 0u;
    case RAIL_E:
    default:
        *v = r->energy_wh;
        return (r->counted & BENCH_COUNTED_ENERGY) != 0u;
    }
}

/**
 * @brief One bench row, a motor run's with @p servo false and a SERVO run's
 *        with it true: then the five columns k_cols marks are @p rail's.
 *
 * @param w     The writer.
 * @param t_s   The time of the row, in s.
 * @param b     The bench sample.
 * @param servo true for a SERVO run's row.
 * @param rail  The supply's figures on a SERVO run's row, or NULL.
 * @return true when the row is written.
 */
static bool bench_row(log_writer_t *w, float t_s, const bench_state_t *b,
                      bool servo, const log_rail_t *rail)
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
    /*
     * On a SERVO run's row voltage, current, power, charge and energy are the
     * supply's (rail, RAIL_*), at the decimals of a supply file: a servo's
     * idle current is tens of mA and its power under a watt.
     */
    static const struct {
        int decimals; size_t offset; uint16_t flag; uint8_t counted;
        uint8_t rail; int rail_decimals;
    } k_cols[] = {
        { 2, offsetof(bench_state_t, voltage),    LINK_BN_VOLTAGE_OK, 0u,
             RAIL_V, 2 },
        { 2, offsetof(bench_state_t, current),    LINK_BN_CURRENT_OK, 0u,
             RAIL_I, 3 },
        { 0, offsetof(bench_state_t, power),
             (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK), 0u,
             RAIL_P, 2 },
        { 0, offsetof(bench_state_t, rpm),        LINK_BN_RPM_OK, 0u,
             RAIL_NONE, 0 },
        { 1, offsetof(bench_state_t, temp_esc),   LINK_BN_TEMP_OK, 0u,
             RAIL_NONE, 0 },
        { 1, offsetof(bench_state_t, temp_motor), LINK_BN_TEMP_MOT_OK, 0u,
             RAIL_NONE, 0 },
        { 0, offsetof(bench_state_t, charge_mah), 0u, BENCH_COUNTED_CHARGE,
             RAIL_Q, 0 },
        { 2, offsetof(bench_state_t, energy_wh),  0u, BENCH_COUNTED_ENERGY,
             RAIL_E, 2 },
    };

    /* In the writer, not on the stack: 23 cells, every one checked
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
        float v;
        if (servo && k_cols[i].rail != RAIL_NONE) {
            if (rail_figure(rail, k_cols[i].rail, &v)) {
                n += fmt(line + n, cap - (size_t)n, v,
                         k_cols[i].rail_decimals);
            }
            continue;               /* no supply figure: an empty cell */
        }
        const uint16_t need = k_cols[i].flag;
        if (need != 0u && (b->flags & need) != need) {
            continue;               /* nothing measured it: an empty cell */
        }
        if (k_cols[i].counted != 0u
            && (b->counted & k_cols[i].counted) == 0u) {
            continue;               /* nothing counted it: an empty cell */
        }
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
     * readings of, and for the quantity a window holds no sample of.  A
     * row with no new window has these cells empty.
     */
    const bool win = b->servo_new;
    fits = fits && cell(line, cap, &n, win, (float)b->servo_window, 0);
    /* With the map, each watched target's channel from the same window of
     * SENSE_WIN_A or SENSE_WIN_B; without, SERVO_SENSE's three. */
    const bool same = win && b->chw.window == b->servo_window;
    const unsigned groups = w->by_map ? w->cols.n : 3u;
    for (unsigned k = 0u; k < groups; ++k) {
        bool i;
        bool v;
        float mean;
        float max;
        float min;
        if (w->by_map) {
            const unsigned c = w->cols.ch[k];
            const uint16_t bit = (uint16_t)(1u << c);
            const bool ch = same && (b->chw.ok & bit) != 0u;
            i    = ch && (b->chw.no_current & bit) == 0u;
            v    = ch && (b->chw.no_voltage & bit) == 0u;
            mean = ch ? (float)b->chw.mean_ma[c] : 0.0f;
            max  = ch ? (float)b->chw.max_ma[c] : 0.0f;
            min  = ch ? (float)b->chw.min_mv[c] : 0.0f;
        } else {
            const bool ch = win && (b->servo_ok & (1u << k)) != 0u;
            i    = ch && (b->servo_no_current & (1u << k)) == 0u;
            v    = ch && (b->servo_no_voltage & (1u << k)) == 0u;
            mean = (float)b->servo_mean_ma[k];
            max  = (float)b->servo_max_ma[k];
            min  = (float)b->servo_min_mv[k];
        }
        fits = fits
               && cell(line, cap, &n, i, mean / 1000.0f, 3)
               && cell(line, cap, &n, i, max / 1000.0f, 3)
               && cell(line, cap, &n, v, min / 1000.0f, 3);
    }
    fits = fits && text_cell(line, cap, &n,
                             source_word(bench_state_simulated(b)));
    if (!fits || (size_t)n + 2u >= cap) {
        w->failed = true;
        return false;
    }
    return finish_row(w, t_s, line, n);
}

bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b)
{
    return bench_row(w, t_s, b, false, NULL);
}

bool log_writer_servo_row(log_writer_t *w, float t_s, const bench_state_t *b,
                          const log_rail_t *rail)
{
    return bench_row(w, t_s, b, true, rail);
}

void log_rail_take(log_rail_t *out, const supply_state_t *s,
                   bool row_modelled)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s == NULL) {
        return;
    }
    if (s->modelled && !row_modelled) {
        out->withheld = true;
        return;
    }
    out->v  = s->v;
    out->i  = s->i;
    out->p  = s->p;
    out->ok = (uint8_t)(s->ok & (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT));
}

void log_rail_count(bench_totals_t *t, log_rail_t *r, float dt_s,
                    bool driving)
{
    if (t == NULL || r == NULL) {
        return;
    }
    if (r->withheld) {
        r->charge_mah = 0.0f;
        r->energy_wh  = 0.0f;
        r->counted    = 0u;
        return;
    }
    float dt = dt_s;
    if (!(dt > 0.0f) || !driving) {
        dt = 0.0f;              /* negative, zero or NaN counts nothing */
    } else if (dt > BENCH_TOTALS_MAX_STEP_S) {
        dt = BENCH_TOTALS_MAX_STEP_S;
    }
    if (driving) {
        const uint8_t both = (uint8_t)(SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
        t->run_s += dt;
        if ((r->ok & SUPPLY_OK_CURRENT) != 0u) {
            t->mah += r->i * dt * (1000.0f / 3600.0f);
            t->counted |= BENCH_COUNTED_CHARGE;
        }
        if ((r->ok & both) == both) {
            t->wh += r->p * dt * (1.0f / 3600.0f);
            t->counted |= BENCH_COUNTED_ENERGY;
        }
    }
    r->charge_mah = t->mah;
    r->energy_wh  = t->wh;
    r->counted    = t->counted;
}
