/**
 * @file
 * @brief The automatic servo test's TXT report, line by line.
 *
 * The lines come in sections, in this order: header, settings, steps,
 * encoder, brown-out, limits and unmeasured notes; servo_report_line() picks
 * one line by its index.
 *
 * Its words are entries of servo_str.c's table, looked up in the language
 * of the table the run was started with (S() below).  A report is UTF-8,
 * as the table's strings are.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_test.h"

#include <stdio.h>
#include <string.h>

/*
 * ------------------------------------------------------------- the
 *        report
 */

/**
 * @brief Cursor that prints one report line, found by counting.
 *
 * The report is written a line at a time as the card takes it, so it is not
 * held anywhere whole.  Each line is found by counting: a cursor walks the
 * report's lines in order and the one asked for is printed.
 */
typedef struct {
    unsigned want;          /**< index of the line asked for              */
    unsigned at;            /**< index of the next line counted           */
    char    *buf;           /**< out: the line                            */
    size_t   n;             /**< size of buf, bytes                       */
    const char *const *text;    /**< the run's language; NULL is English */
} cursor_t;

/** @brief A word or a line format of the report, in the run's language. */
#define S(id) servo_str_in(c->text, SERVO_STR_##id)

/**
 * @brief Count one line, and tell whether it is the one asked for.
 *
 * @param c Cursor. Its count goes up whether or not this is the line.
 * @return true when the line counted is the one asked for.
 */
static bool here(cursor_t *c)
{
    return c->want == c->at++;
}

/**
 * @brief A limit's value, or OFF for 0.
 *
 * @param c Cursor; its text gives the language of OFF.
 * @param b Out: the text.
 * @param n Size of b, bytes.
 * @param a Limit, A; 0 or less is OFF.
 */
static void limit_a(const cursor_t *c, char *b, size_t n, float a)
{
    if (a > 0.0f) {
        snprintf(b, n, "%.2f A", (double)a);
    } else {
        snprintf(b, n, "%s", S(R_OFF));
    }
}

/**
 * @brief The word for one check: NOT CHECKED, NOT MEASURED, FAIL or PASS.
 *
 * @param c        Cursor; its text gives the language.
 * @param checked  true when the check has a limit.
 * @param measured true when a value was measured.
 * @param over     true when the value is over the limit.
 * @return the word, from the run's language table.
 */
static const char *verdict_word(const cursor_t *c, bool checked,
                                bool measured, bool over)
{
    if (!checked) {
        return S(R_NOT_CHECKED);
    }
    if (!measured) {
        return S(R_NOT_MEASURED);
    }
    return over ? S(FAIL) : S(PASS);
}

/**
 * @brief A mean in amperes, or "--" for none.
 *
 * @param b Out: the mean, 3 decimals, or "--".
 * @param n Size of b, bytes.
 * @param m Mean to print; no reading gives "--".
 */
static void amps(char *b, size_t n, const servo_test_mean_t *m)
{
    if (m->n > 0u) {
        snprintf(b, n, "%.3f", (double)(m->sum / (float)m->n));
    } else {
        snprintf(b, n, "--");
    }
}

/**
 * @brief The first and the last brown-out voltage that ran to its end.
 *
 * @param t     Run state.
 * @param first Out: the first such step, or NULL for none. NULL to skip.
 * @param last  Out: the last such step, or NULL for none. NULL to skip.
 */
static void brownout_ran(const servo_test_t *t, const servo_test_step_t **first,
                         const servo_test_step_t **last)
{
    const servo_test_step_t *f = NULL, *l = NULL;
    for (unsigned k = 0; k < t->step_count; ++k) {
        if (t->steps[k].brownout && t->steps[k].done) {
            if (f == NULL) {
                f = &t->steps[k];
            }
            l = &t->steps[k];
        }
    }
    if (first != NULL) {
        *first = f;
    }
    if (last != NULL) {
        *last = l;
    }
}

/**
 * @brief The head of the report: title, result, device and firmware, the
 *        meter and supply, the readings' rates, the counts, the duration and
 *        the rows.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of the head's lines.
 */
static bool header_lines(const servo_test_t *t, cursor_t *c)
{
    const servo_test_cfg_t *g = &t->cfg;
    char *b = c->buf;
    const size_t n = c->n;
    const servo_test_verdict_t v = servo_test_verdict(t);
    if (here(c)) {
        snprintf(b, n, "%s", S(R_TITLE));
        return true;
    }
    if (here(c)) {
        const char *verdict = servo_str_in(c->text,
                                           servo_test_verdict_str(v));
        unsigned unseen = 0u, counted = 0u;
        for (unsigned k = 0; k < t->step_count; ++k) {
            if (!t->steps[k].brownout) {
                unseen  += t->steps[k].no_rise;
                counted += t->steps[k].moves;
            }
        }
        if (v == SERVO_TEST_ABORTED) {
            snprintf(b, n, S(R_RESULT_WHY), verdict,
                     servo_str_in(c->text, servo_test_abort_str(t->why)));
        } else if (v == SERVO_TEST_NOT_MEASURABLE && unseen > 0u) {
            snprintf(b, n, S(R_RESULT_UNSEEN), verdict, unseen, counted);
        } else if (v == SERVO_TEST_NOT_MEASURABLE) {
            /* Every counted move seen: the walk's first voltage saw none. */
            const servo_test_step_t *first = NULL;
            brownout_ran(t, &first, NULL);
            snprintf(b, n, S(R_RESULT_BO_UNSEEN), verdict,
                     (first != NULL) ? (double)first->set_v : 0.0);
        } else {
            snprintf(b, n, S(R_RESULT), verdict);
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_DEVICE), g->dut);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_FIRMWARE), g->firmware);
        return true;
    }
    const bool ina = g->meter.kind == (uint8_t)SERVO_TEST_METER_INA3221;
    if (here(c)) {
        snprintf(b, n, "%s", ina ? S(R_LOG_WIN) : S(R_LOG));
        return true;
    }
    /* The supply, and what measures: a run on the INA3221 is still fed by
     * the PD mini. */
    const char *meter = g->model ? S(R_METER_MODEL)
                        : (g->meter.name[0] != '\0') ? g->meter.name : "--";
    if (here(c)) {
        snprintf(b, n, S(R_SUPPLY), g->model ? S(R_SUPPLY_MODEL)
                                    : ina    ? S(R_SUPPLY_PDMINI) : meter);
        return true;
    }
    if (here(c)) {
        if (ina) {
            snprintf(b, n, S(R_CURRENT_INA), meter,
                     (double)g->meter.shunt_dmohm / 10.0,
                     (double)g->meter.range_ma / 1000.0);
        } else {
            snprintf(b, n, S(R_CURRENT), meter);
        }
        return true;
    }
    if (here(c)) {
        if (ina) {
            snprintf(b, n, S(R_VOLTAGE_INA), meter);
        } else {
            snprintf(b, n, S(R_VOLTAGE), meter);
        }
        return true;
    }
    if (ina && here(c)) {
        snprintf(b, n, S(R_SHUNT),
                 (double)SERVO_TEST_INA3221_END_UV / 1000000.0);
        return true;
    }
    if (!ina && g->ina_why != (uint8_t)SERVO_TEST_INA_NONE
        && g->ina_why < (uint8_t)SERVO_TEST_INA_COUNT && here(c)) {
        snprintf(b, n, S(R_INA_UNUSED),
                 servo_str_in(c->text,
                              (servo_str_t)((int)SERVO_STR_R_INA_OLD
                                            + (int)g->ina_why - 1)));
        return true;
    }
    if (servo_test_negative_at_rest(t) && here(c)) {
        snprintf(b, n, "%s", S(R_NEGATIVE));
        return true;
    }
    if (g->enc_on && here(c)) {
        snprintf(b, n, S(R_ENC_DEVICE), (unsigned)g->enc_centre,
                 (unsigned long)t->enc_reads);
        return true;
    }
    float per_s = 0.0f, module_s = 0.0f;
    uint32_t every = 0u;
    const bool rated = servo_test_rates(t, &per_s, &module_s, &every);
    if (here(c)) {
        if (rated) {
            snprintf(b, n, ina ? S(R_READINGS_WIN) : S(R_READINGS),
                     (double)module_s, (double)per_s);
        } else {
            snprintf(b, n, "%s", S(R_READINGS_FEW));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, ina ? S(R_SKIPPED_WIN) : S(R_SKIPPED),
                 (unsigned long)t->skipped);
        return true;
    }
    if (here(c)) {
        if (rated) {
            snprintf(b, n, ina ? S(R_RESOLUTION_WIN) : S(R_RESOLUTION),
                     (unsigned long)every);
        } else {
            snprintf(b, n, "%s", S(R_RESOLUTION_UNKNOWN));
        }
        return true;
    }
    /* What the meter's readings are worth, where it says. */
    if (g->meter.lag_ms > 0u && here(c)) {
        snprintf(b, n, S(R_LAG), (unsigned)g->meter.lag_ms);
        return true;
    }
    if (g->meter.repeats && here(c)) {
        snprintf(b, n, "%s", S(R_REPEATS));
        return true;
    }
    if (g->meter.upper_bound && here(c)) {
        snprintf(b, n, "%s", S(R_UPPER_BOUND));
        return true;
    }
    if (ina && t->clipped > 0u && here(c)) {
        snprintf(b, n, S(R_CLIPPED), (unsigned long)t->clipped,
                 (double)g->meter.range_ma / 1000.0);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_CC), (unsigned long)t->cc_readings,
                 (unsigned long)t->cc_longest_ms);
        return true;
    }
    if (here(c)) {
        const uint32_t ms = t->end_ms - t->start_ms;
        snprintf(b, n, S(R_DURATION), (unsigned long)(ms / 1000u),
                 (unsigned long)((ms % 1000u) / 100u));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ROWS), (unsigned long)t->rows,
                 (unsigned long)t->rows_lost);
        return true;
    }
    return false;
}

/**
 * @brief The settings lines: the servo, the rate, the danger tag, HV, the
 *        ends, the steps, the brown-out walk, the current limit, the timing,
 *        the length and the limits.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of the settings lines.
 */
static bool settings_lines(const servo_test_t *t, cursor_t *c)
{
    const servo_test_cfg_t *g = &t->cfg;
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_SETTINGS));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_TYPE), g->type, (unsigned)g->centre_us,
                 (unsigned)g->min_us, (unsigned)g->max_us, (int)g->trim_us,
                 g->reverse ? S(R_ON) : S(R_OFF));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_RATE), (unsigned)g->frame_hz);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_DANGER), (g->danger[0] != '\0')
                                        ? g->danger : S(R_DANGER_NONE));
        return true;
    }
    if (here(c)) {
        /* What ran, not only the switch: HV SERVO on with both of its
         * steps off runs nothing above 6.0 V. */
        bool above = false;
        for (unsigned s = 0; s < g->step_count; ++s) {
            above = above || g->steps_v[s] > SERVO_TEST_HV_ABOVE_V + 0.001f;
        }
        snprintf(b, n, S(R_HV), !g->hv  ? S(R_HV_OFF)
                                : above ? S(R_HV_ON_RUN)
                                        : S(R_HV_ON_NONE));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ENDS), (unsigned)g->end_lo_us,
                 (unsigned)g->end_hi_us, (unsigned)g->range_pct,
                 (unsigned)g->travel_deg);
        return true;
    }
    if (here(c)) {
        int k = snprintf(b, n, "%s", S(R_STEPS));
        for (unsigned s = 0; s < g->step_count && k > 0 && (size_t)k < n; ++s) {
            k += snprintf(b + k, n - (size_t)k, " %.2f V", (double)g->steps_v[s]);
        }
        if (g->step_count == 0u && k > 0 && (size_t)k < n) {
            snprintf(b + k, n - (size_t)k, "%s", S(R_STEPS_NONE));
        }
        return true;
    }
    if (here(c)) {
        if (g->brownout) {
            snprintf(b, n, S(R_BROWNOUT), (double)t->bo_start_v,
                     (double)SERVO_TEST_BROWNOUT_STEP_V, (double)t->floor_v);
        } else {
            snprintf(b, n, "%s", S(R_BROWNOUT_NOT_RUN));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_I_LIMIT), (double)g->i_limit);
        return true;
    }
    if (here(c)) {
        const unsigned hold = (g->dwell_ms > SERVO_TEST_HOLD_MIN_MS)
                                  ? g->dwell_ms : SERVO_TEST_HOLD_MIN_MS;
        snprintf(b, n, S(R_TIMING), (unsigned)g->settle_ms,
                 (unsigned)SERVO_TEST_IDLE_MS, (unsigned)g->dwell_ms, hold);
        return true;
    }
    if (here(c)) {
        if (g->by_moves) {
            snprintf(b, n, S(R_LEN_MOVES), (unsigned)g->moves);
        } else {
            snprintf(b, n, S(R_LEN_TIME), (unsigned)g->time_s);
        }
        return true;
    }
    if (here(c)) {
        char idle[24], hold[24];
        limit_a(c, idle, sizeof(idle), g->idle_max_a);
        limit_a(c, hold, sizeof(hold), g->hold_max_a);
        char travel[24];
        if (g->travel_max_ms > 0u) {
            snprintf(travel, sizeof(travel), "%u ms", (unsigned)g->travel_max_ms);
        } else {
            snprintf(travel, sizeof(travel), "%s", S(R_OFF));
        }
        snprintf(b, n, S(R_LIMITS), idle, hold, travel, (double)g->stall_a);
        return true;
    }
    return false;
}

/**
 * @brief The steps table: one line per characterisation step, then the
 *        column notes and the threshold, arrival and late rules.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of these.
 */
static bool step_lines(const servo_test_t *t, cursor_t *c)
{
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_PER_STEP));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_COLUMNS));
        return true;
    }
    bool any = false;
    for (unsigned k = 0; k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (s->brownout) {
            continue;
        }
        any = true;
        if (!here(c)) {
            continue;
        }
        if (!s->begun) {
            snprintf(b, n, S(R_STEP_NOT_RUN), (double)s->set_v);
            return true;
        }
        char vm[16], vlow[16], idle[16], thr[16], move[16], lo[16], hi[16];
        if (s->v.n > 0u) {
            snprintf(vm, sizeof(vm), "%.2f", (double)(s->v.sum / (float)s->v.n));
            snprintf(vlow, sizeof(vlow), "%.2f", (double)s->v_min);
        } else {
            snprintf(vm, sizeof(vm), "--");
            snprintf(vlow, sizeof(vlow), "--");
        }
        amps(idle, sizeof(idle), &s->idle);
        if (s->move_a > 0.0f) {
            snprintf(thr, sizeof(thr), "%.3f", (double)s->move_a);
        } else {
            snprintf(thr, sizeof(thr), "--");
        }
        amps(move, sizeof(move), &s->move);
        amps(lo, sizeof(lo), &s->hold[0]);
        amps(hi, sizeof(hi), &s->hold[1]);
        char peak[16];
        if (s->move.n > 0u) {
            snprintf(peak, sizeof(peak), "%.3f", (double)s->move_peak_a);
        } else {
            snprintf(peak, sizeof(peak), "--");
        }
        char mean_ms[12], max_ms[12];
        if (s->travels > 0u) {
            snprintf(mean_ms, sizeof(mean_ms), "%lu",
                     (unsigned long)(s->travel_sum_ms / s->travels));
            snprintf(max_ms, sizeof(max_ms), "%lu",
                     (unsigned long)s->travel_max_ms);
        } else {
            snprintf(mean_ms, sizeof(mean_ms), "--");
            snprintf(max_ms, sizeof(max_ms), "--");
        }
        /* A step none of whose moves showed movement is not measurable. */
        const bool none = s->moves > 0u && s->no_rise == s->moves;
        snprintf(b, n,
                 "%5.2f  %6s  %-6s %-6s %-6s %-6s %-6s %-7s %-7s %-6s %-7s %5u "
                 "%4u %6u%s%s%s",
                 (double)s->set_v, vm, vlow, idle, thr, move, peak, lo, hi,
                 mean_ms,
                 max_ms, (unsigned)s->moves, (unsigned)s->timeouts,
                 (unsigned)s->no_rise, s->done ? "" : S(R_CUT_SHORT),
                 none ? " " : "", none ? S(NOT_MEASURABLE) : "");
        return true;
    }
    if (!any && here(c)) {
        snprintf(b, n, "%s", S(R_NO_STEP));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_THRESHOLD), (double)SERVO_TEST_MOVE_MIN_A,
                 (double)SERVO_TEST_NOISE_K);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ARRIVAL), (double)SERVO_TEST_BAND_A);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_LATE), (unsigned)servo_test_travel_window_ms(t));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_UNSEEN));
        return true;
    }
    return false;
}

/**
 * @brief A step's mean end angle at @p end and its error against the
 *        commanded one, both on the circle (servo_test_enc_end()), signed.
 *
 * @param t   Run state.
 * @param s   Step.
 * @param end 0 for the low end, 1 for the high end.
 * @param b   Out: the mean angle, degrees, or "--" for none.
 * @param eb  Out: the error, degrees, or "--" for none.
 * @param n   Size of b and eb, bytes.
 */
static void deg_end(const servo_test_t *t, const servo_test_step_t *s,
                    unsigned end, char *b, char *eb, size_t n)
{
    float deg = 0.0f, err = 0.0f;
    if (servo_test_enc_end(t, s, end, &deg, &err)) {
        snprintf(b, n, "%+.2f", (double)deg);
        snprintf(eb, n, "%+.2f", (double)err);
    } else {
        snprintf(b, n, "--");
        snprintf(eb, n, "--");
    }
}

/**
 * @brief The encoder lines: one line per characterisation step with its
 *        end angles, then the counts and the rules.  Nothing when the encoder
 *        is off.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of these.
 */
static bool encoder_lines(const servo_test_t *t, cursor_t *c)
{
    const servo_test_cfg_t *g = &t->cfg;
    if (!g->enc_on) {
        return false;
    }
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_ENC_HEAD));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_ENC_COLUMNS));
        return true;
    }
    bool any = false;
    for (unsigned k = 0; k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (s->brownout) {
            continue;
        }
        any = true;
        if (!here(c)) {
            continue;
        }
        if (!s->begun) {
            snprintf(b, n, S(R_STEP_NOT_RUN), (double)s->set_v);
            return true;
        }
        char lo[16], elo[16], hi[16], ehi[16], mean_ms[12], max_ms[12];
        deg_end(t, s, 0u, lo, elo, sizeof(lo));
        deg_end(t, s, 1u, hi, ehi, sizeof(hi));
        if (s->enc_travels > 0u) {
            snprintf(mean_ms, sizeof(mean_ms), "%lu",
                     (unsigned long)(s->enc_travel_sum_ms / s->enc_travels));
            snprintf(max_ms, sizeof(max_ms), "%lu",
                     (unsigned long)s->enc_travel_max_ms);
        } else {
            snprintf(mean_ms, sizeof(mean_ms), "--");
            snprintf(max_ms, sizeof(max_ms), "--");
        }
        snprintf(b, n, "%5.2f  %-7s  %-7s  %-7s  %-7s  %-6s %-7s %5u %7u %4u",
                 (double)s->set_v, lo, elo, hi, ehi, mean_ms, max_ms,
                 (unsigned)s->enc_moves, (unsigned)s->enc_unmoved,
                 (unsigned)s->enc_late);
        return true;
    }
    if (!any && here(c)) {
        snprintf(b, n, "%s", S(R_NO_STEP));
        return true;
    }
    if (t->enc_reads == 0u && here(c)) {
        snprintf(b, n, "%s", S(R_ENC_NONE));
        return true;
    }
    if (t->enc_no_magnet > 0u && here(c)) {
        snprintf(b, n, S(R_ENC_NO_MAGNET), (unsigned long)t->enc_no_magnet);
        return true;
    }
    if ((t->enc_weak > 0u || t->enc_strong > 0u) && here(c)) {
        snprintf(b, n, S(R_ENC_FIELD), (unsigned long)t->enc_weak,
                 (unsigned long)t->enc_strong);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ENC_CMD), (double)g->enc_cmd_deg[0],
                 (double)g->enc_cmd_deg[1]);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_ENC_END));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ENC_SETTLED), (double)SERVO_TEST_ENC_TOL_DEG,
                 (unsigned)SERVO_TEST_ENC_HOLD_MS);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_ENC_TRAVEL));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ENC_UNMOVED), (double)SERVO_TEST_ENC_MOVED_DEG);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_ENC_LATE), (unsigned)SERVO_TEST_ENC_HOLD_MS);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_ENC_DEADBAND));
        return true;
    }
    return false;
}

/**
 * @brief The brown-out lines: the walk's result, then the threshold rule.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of these.
 */
static bool brownout_lines(const servo_test_t *t, cursor_t *c)
{
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_BO_HEAD));
        return true;
    }
    if (here(c)) {
        float moved = 0.0f;
        bool stopped = false;
        const bool any = servo_test_brownout(t, &moved, &stopped);
        const servo_test_step_t *first = NULL, *last = NULL;
        brownout_ran(t, &first, &last);
        if (!t->cfg.brownout) {
            snprintf(b, n, "%s", S(R_BO_NOT_RUN));
        } else if (first == NULL || last == NULL) {
            snprintf(b, n, "%s", S(R_BO_NOT_REACHED));
        } else if (any && stopped) {
            snprintf(b, n, S(R_BO_STOPPED), (double)moved, (double)last->set_v);
        } else if (any) {
            snprintf(b, n, S(R_BO_ALL), (double)moved);
        } else {
            snprintf(b, n, S(R_BO_NONE), (double)first->set_v);
        }
        return true;
    }
    /* The threshold where the walk ended: the last voltage it ran. */
    const servo_test_step_t *last = NULL;
    brownout_ran(t, NULL, &last);
    if (last != NULL && here(c)) {
        snprintf(b, n, S(R_BO_RULE), (double)last->move_a,
                 (double)last->set_v);
        return true;
    }
    return false;
}

/**
 * @brief STALL AT against the current limit, or against the meter's range.
 *
 * @param c        Cursor; buf receives the line.
 * @param g        Configuration of the run.
 * @param by_limit true for the current limit, false for the meter's range.
 */
static void stall_line(const cursor_t *c, const servo_test_cfg_t *g,
                       bool by_limit)
{
    if (by_limit) {
        snprintf(c->buf, c->n, S(R_LIM_STALL_LIMIT), (double)g->stall_a,
                 (double)g->i_limit);
    } else {
        snprintf(c->buf, c->n, S(R_LIM_STALL_RANGE), (double)g->stall_a,
                 (double)g->meter.range_ma / 1000.0);
    }
}

bool servo_test_stall_note(const servo_test_cfg_t *cfg, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return false;
    }
    buf[0] = '\0';
    bool by_limit = false;
    if (!servo_test_stall_unreachable(cfg, &by_limit, NULL)) {
        return false;
    }
    const cursor_t c = { 0u, 0u, buf, n, cfg->text };
    stall_line(&c, cfg, by_limit);
    return true;
}

/**
 * @brief The limits lines: the idle, hold, travel, stall, late and unseen
 *        checks against their limits, and the first brown-out voltage.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of these.
 */
static bool limit_lines(const servo_test_t *t, cursor_t *c)
{
    const servo_test_cfg_t *g = &t->cfg;
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "%s", S(R_LIM_HEAD));
        return true;
    }
    float a = 0.0f;
    if (here(c)) {
        const bool m = servo_test_max_idle(t, &a);
        char lim[24];
        limit_a(c, lim, sizeof(lim), g->idle_max_a);
        snprintf(b, n, S(R_LIM_IDLE), (double)a, lim,
                 verdict_word(c, g->idle_max_a > 0.0f, m,
                              servo_test_over_a(a, g->idle_max_a)));
        return true;
    }
    if (here(c)) {
        const bool m = servo_test_max_hold(t, &a);
        char lim[24];
        limit_a(c, lim, sizeof(lim), g->hold_max_a);
        snprintf(b, n, S(R_LIM_HOLD), (double)a, lim,
                 verdict_word(c, g->hold_max_a > 0.0f, m,
                              servo_test_over_a(a, g->hold_max_a)));
        return true;
    }
    if (here(c)) {
        uint32_t ms = 0u;
        const bool m = servo_test_max_travel(t, &ms);
        if (!m) {
            /* No move arrived: there is no longest travel time, not even
             * an upper bound. */
            char lim[24];
            if (g->travel_max_ms > 0u) {
                snprintf(lim, sizeof(lim), "%u ms", (unsigned)g->travel_max_ms);
            } else {
                snprintf(lim, sizeof(lim), "%s", S(R_OFF));
            }
            snprintf(b, n, S(R_LIM_TRAVEL_NONE), lim);
        } else if (g->travel_max_ms > 0u && g->meter.upper_bound) {
            snprintf(b, n, S(R_LIM_TRAVEL_BOUND), (unsigned long)ms,
                     (unsigned)g->travel_max_ms);
        } else if (g->travel_max_ms > 0u) {
            snprintf(b, n, S(R_LIM_TRAVEL), (unsigned long)ms,
                     (unsigned)g->travel_max_ms,
                     verdict_word(c, true, true, ms > g->travel_max_ms));
        } else {
            snprintf(b, n, S(R_LIM_TRAVEL_OFF), (unsigned long)ms,
                     verdict_word(c, false, true, false));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_LIM_STALL), (double)t->stall_peak_a,
                 (double)g->stall_a, t->stalled ? S(FAIL) : S(PASS));
        return true;
    }
    /* A STALL AT no reading can pass, said beside its line. */
    bool by_limit = false, by_range = false;
    (void)servo_test_stall_unreachable(g, &by_limit, &by_range);
    if (by_limit && here(c)) {
        stall_line(c, g, true);
        return true;
    }
    if (by_range && here(c)) {
        stall_line(c, g, false);
        return true;
    }
    if (here(c)) {
        unsigned late = 0u;
        for (unsigned k = 0; k < t->step_count; ++k) {
            if (!t->steps[k].brownout) {
                late += t->steps[k].timeouts;
            }
        }
        snprintf(b, n, S(R_LIM_LATE), late, (late > 0u) ? S(FAIL) : S(PASS));
        return true;
    }
    if (here(c)) {
        unsigned unseen = 0u;
        for (unsigned k = 0; k < t->step_count; ++k) {
            if (!t->steps[k].brownout) {
                unseen += t->steps[k].no_rise;
            }
        }
        snprintf(b, n, S(R_LIM_UNSEEN), unseen,
                 (unseen > 0u) ? S(NOT_MEASURABLE) : S(PASS));
        return true;
    }
    /* The walk's first voltage, where it ran: no movement there measured
     * nothing, and the run says so. */
    const servo_test_step_t *first = NULL;
    brownout_ran(t, &first, NULL);
    if (first != NULL && here(c)) {
        if (first->moved) {
            snprintf(b, n, S(R_LIM_BO_SEEN), (double)first->set_v, S(PASS));
        } else {
            snprintf(b, n, S(R_LIM_BO_UNSEEN), (double)first->set_v,
                     S(NOT_MEASURABLE));
        }
        return true;
    }
    return false;
}

/**
 * @brief The unmeasured notes: the heading and the lines that say what the
 *        run did not measure.
 *
 * @param t Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of these.
 */
static bool unmeasured_lines(const servo_test_t *t, cursor_t *c)
{
    const bool ina = t->cfg.meter.kind == (uint8_t)SERVO_TEST_METER_INA3221;
    const servo_str_t k_lines[] = {
        SERVO_STR_COUNT,            /* the blank line before the heading */
        SERVO_STR_R_UNM_HEAD,
        t->cfg.enc_on ? SERVO_STR_R_UNM_POSITION_ENC
        : ina         ? SERVO_STR_R_UNM_POSITION_INA
                      : SERVO_STR_R_UNM_POSITION,
        ina ? SERVO_STR_R_UNM_PEAKS_INA : SERVO_STR_R_UNM_PEAKS,
        SERVO_STR_R_UNM_PATH,
    };
    for (size_t k = 0; k < sizeof(k_lines) / sizeof(k_lines[0]); ++k) {
        if (here(c)) {
            snprintf(c->buf, c->n, "%s", servo_str_in(c->text, k_lines[k]));
            return true;
        }
    }
    return false;
}

bool servo_report_line(const servo_test_t *t, unsigned idx, char *buf,
                       size_t n)
{
    if (t == NULL || buf == NULL || n == 0u) {
        return false;
    }
    cursor_t c = { idx, 0u, buf, n, t->cfg.text };
    return header_lines(t, &c) || settings_lines(t, &c) || step_lines(t, &c)
           || encoder_lines(t, &c) || brownout_lines(t, &c)
           || limit_lines(t, &c) || unmeasured_lines(t, &c);
}
