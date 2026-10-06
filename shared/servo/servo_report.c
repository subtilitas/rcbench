/*
 * The automatic servo test's words: the CSV header, the phase and abort
 * names, and the TXT report, line by line.
 *
 * Every string the run writes or the SERVO screen shows about it is in
 * k_str or in the report's line formats below, and nowhere else, so a
 * translation replaces this file's tables and nothing more.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_test.h"

#include <stdio.h>
#include <string.h>

static const char *const k_str[SERVO_STR_COUNT] = {
    [SERVO_STR_PHASE_NONE]       = "",
    [SERVO_STR_PHASE_SET]        = "SET",
    [SERVO_STR_PHASE_SETTLE]     = "SETTLE",
    [SERVO_STR_PHASE_IDLE]       = "IDLE",
    [SERVO_STR_PHASE_MOVE]       = "MOVE",
    [SERVO_STR_PHASE_HOLD]       = "HOLD",
    [SERVO_STR_PASS]             = "PASS",
    [SERVO_STR_FAIL]             = "FAIL",
    [SERVO_STR_ABORTED]          = "ABORTED",
    [SERVO_STR_TEST_STEP]        = "STEP",
    [SERVO_STR_TEST_BROWNOUT]    = "BROWN-OUT",
    [SERVO_STR_AB_NONE]          = "",
    [SERVO_STR_AB_STOP]          = "STOP",
    [SERVO_STR_AB_DISARMED]      = "bench disarmed",
    [SERVO_STR_AB_LINK]          = "link lost",
    [SERVO_STR_AB_LEFT]          = "SERVO screen left",
    [SERVO_STR_AB_OPERATOR]      = "stopped by the operator",
    [SERVO_STR_AB_SETTINGS]      = "servo settings changed",
    [SERVO_STR_AB_TOUCH]         = "touch events lost",
    [SERVO_STR_AB_SUPPLY_LOST]   = "supply not answering",
    [SERVO_STR_AB_TRIPPED]       = "supply tripped",
    [SERVO_STR_AB_SUPPLY_OFF]    = "supply output went off",
    [SERVO_STR_AB_STALE]         = "no new supply reading for 1.5 s",
    [SERVO_STR_AB_NOT_ON]        = "supply output did not come on",
    [SERVO_STR_AB_SET_NOT_TAKEN] = "set point not read back in 3 s",
    [SERVO_STR_AB_CAP]           = "step above the voltage cap",
    [SERVO_STR_AB_STALL]         = "above STALL AT for 1 s",
    [SERVO_STR_START_OK]         = "",
    [SERVO_STR_START_NO_STEPS]   = "NO STEP CHOSEN",
    [SERVO_STR_START_ABOVE_CAP]  = "A STEP IS OUTSIDE THE CAPS",
    [SERVO_STR_START_NO_SUPPLY]  = "SUPPLY NOT ANSWERING",
    [SERVO_STR_START_BAD_ENDS]   = "RANGE TOO SMALL",
    [SERVO_STR_START_NOT_ARMED]  = "ARM FIRST",
    [SERVO_STR_START_BUSY]       = "LAST REPORT STILL WRITING",
};

const char *servo_str(servo_str_t id)
{
    if ((unsigned)id >= (unsigned)SERVO_STR_COUNT || k_str[id] == NULL) {
        return "";
    }
    return k_str[id];
}

const char *servo_test_phase_name(servo_test_phase_t ph)
{
    if ((unsigned)ph > (unsigned)SERVO_TEST_PH_HOLD) {
        return "";
    }
    return servo_str((servo_str_t)(SERVO_STR_PHASE_NONE + (int)ph));
}

const char *servo_test_abort_name(servo_test_abort_t why)
{
    if ((unsigned)why >= (unsigned)SERVO_TEST_AB_COUNT) {
        return "";
    }
    return servo_str((servo_str_t)(SERVO_STR_AB_NONE + (int)why));
}

const char *servo_test_verdict_name(servo_test_verdict_t v)
{
    switch (v) {
    case SERVO_TEST_PASS: return servo_str(SERVO_STR_PASS);
    case SERVO_TEST_FAIL: return servo_str(SERVO_STR_FAIL);
    default:              return servo_str(SERVO_STR_ABORTED);
    }
}

const char *servo_test_start_name(servo_test_start_t why)
{
    if ((unsigned)why > (unsigned)SERVO_TEST_START_BAD_ENDS) {
        return "";
    }
    return servo_str((servo_str_t)(SERVO_STR_START_OK + (int)why));
}

const char *servo_test_csv_header(void)
{
    return "time (s);test;step;phase;command (us);position (us);set (V);"
           "voltage (V);limit (A);current (A);power (W);mode;travel (ms)";
}

/* ------------------------------------------------------------- the report */

/*
 * The report is written a line at a time as the card takes it, so it is
 * not held anywhere whole.  Each line is found by counting: a cursor walks
 * the report's lines in order and the one asked for is printed.
 */
typedef struct {
    unsigned want;
    unsigned at;
    char    *buf;
    size_t   n;
} cursor_t;

static bool here(cursor_t *c)
{
    return c->want == c->at++;
}

/* A limit's value, or OFF for 0. */
static void limit_a(char *b, size_t n, float a)
{
    if (a > 0.0f) {
        snprintf(b, n, "%.2f A", (double)a);
    } else {
        snprintf(b, n, "OFF");
    }
}

static const char *verdict_word(bool checked, bool measured, bool over)
{
    if (!checked) {
        return "not checked";
    }
    if (!measured) {
        return "not measured";
    }
    return over ? servo_str(SERVO_STR_FAIL) : servo_str(SERVO_STR_PASS);
}

/* A mean in amperes, or "--" for none. */
static void amps(char *b, size_t n, const servo_test_mean_t *m)
{
    if (m->n > 0u) {
        snprintf(b, n, "%.3f", (double)(m->sum / (float)m->n));
    } else {
        snprintf(b, n, "--");
    }
}

static bool header_lines(const servo_test_t *t, cursor_t *c)
{
    const servo_test_cfg_t *g = &t->cfg;
    char *b = c->buf;
    const size_t n = c->n;
    const servo_test_verdict_t v = servo_test_verdict(t);
    if (here(c)) {
        snprintf(b, n, "RCBENCH SERVO TEST REPORT");
        return true;
    }
    if (here(c)) {
        if (v == SERVO_TEST_ABORTED) {
            snprintf(b, n, "Result:         %s - %s",
                     servo_test_verdict_name(v), servo_test_abort_name(t->why));
        } else {
            snprintf(b, n, "Result:         %s", servo_test_verdict_name(v));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Device:         %s", g->dut);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Firmware:       rcbench %s", g->firmware);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Log:            the .CSV with this file's number, one "
                       "row per supply reading");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Supply:         %s",
                 g->model ? "the panel's model: every reading is simulated"
                          : "PD mini");
        return true;
    }
    float per_s = 0.0f, module_s = 0.0f;
    uint32_t every = 0u;
    const bool rated = servo_test_rates(t, &per_s, &module_s, &every);
    if (here(c)) {
        if (rated) {
            snprintf(b, n, "Readings:       %.1f /s taken by the supply, "
                           "%.1f /s reached the test", (double)module_s,
                     (double)per_s);
        } else {
            snprintf(b, n, "Readings:       fewer than two");
        }
        return true;
    }
    if (here(c)) {
        if (rated) {
            snprintf(b, n, "Resolution:     one reading every %lu ms: a travel "
                           "time is late by up to that",
                     (unsigned long)every);
        } else {
            snprintf(b, n, "Resolution:     not known");
        }
        return true;
    }
    if (here(c)) {
        const uint32_t ms = t->end_ms - t->start_ms;
        snprintf(b, n, "Duration:       %lu.%01lu s", (unsigned long)(ms / 1000u),
                 (unsigned long)((ms % 1000u) / 100u));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Log rows:       %lu written, %lu lost to a full queue",
                 (unsigned long)t->rows, (unsigned long)t->rows_lost);
        return true;
    }
    return false;
}

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
        snprintf(b, n, "SETTINGS IN FORCE");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Type:           %s, centre %u us, %u-%u us, trim %+d us,"
                       " reverse %s", g->type, (unsigned)g->centre_us,
                 (unsigned)g->min_us, (unsigned)g->max_us, (int)g->trim_us,
                 g->reverse ? "ON" : "OFF");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Frame rate:     %u Hz", (unsigned)g->frame_hz);
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Can destroy:    %s", (g->danger[0] != '\0')
                                                  ? g->danger : "none in force");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "HV servo:       %s", g->hv ? "ON, steps above 6.0 V run"
                                                   : "OFF, no step above 6.0 V");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Ends:           %u us and %u us (RANGE %u %% of TRAVEL "
                       "+/-%u deg)", (unsigned)g->end_lo_us,
                 (unsigned)g->end_hi_us, (unsigned)g->range_pct,
                 (unsigned)g->travel_deg);
        return true;
    }
    if (here(c)) {
        int k = snprintf(b, n, "Steps:         ");
        for (unsigned s = 0; s < g->step_count && k > 0 && (size_t)k < n; ++s) {
            k += snprintf(b + k, n - (size_t)k, " %.2f V", (double)g->steps_v[s]);
        }
        if (g->step_count == 0u && k > 0 && (size_t)k < n) {
            snprintf(b + k, n - (size_t)k, " none");
        }
        return true;
    }
    if (here(c)) {
        if (g->brownout) {
            snprintf(b, n, "Brown-out:      from %.2f V down in %.2f V steps to "
                           "%.2f V", (double)t->bo_start_v,
                     (double)SERVO_TEST_BROWNOUT_STEP_V, (double)t->floor_v);
        } else {
            snprintf(b, n, "Brown-out:      not run");
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Current limit:  %.2f A", (double)g->i_limit);
        return true;
    }
    if (here(c)) {
        const unsigned hold = (g->dwell_ms > SERVO_TEST_HOLD_MIN_MS)
                                  ? g->dwell_ms : SERVO_TEST_HOLD_MIN_MS;
        snprintf(b, n, "Timing:         settle %u ms, idle %u ms, dwell %u ms "
                       "(hold measured %u ms)", (unsigned)g->settle_ms,
                 (unsigned)SERVO_TEST_IDLE_MS, (unsigned)g->dwell_ms, hold);
        return true;
    }
    if (here(c)) {
        if (g->by_moves) {
            snprintf(b, n, "Length:         %u movements a step",
                     (unsigned)g->moves);
        } else {
            snprintf(b, n, "Length:         %u s a step", (unsigned)g->time_s);
        }
        return true;
    }
    if (here(c)) {
        char idle[12], hold[12];
        limit_a(idle, sizeof(idle), g->idle_max_a);
        limit_a(hold, sizeof(hold), g->hold_max_a);
        char travel[12];
        if (g->travel_max_ms > 0u) {
            snprintf(travel, sizeof(travel), "%u ms", (unsigned)g->travel_max_ms);
        } else {
            snprintf(travel, sizeof(travel), "OFF");
        }
        snprintf(b, n, "Limits:         idle %s, holding %s, travel %s, "
                       "stall %.2f A", idle, hold, travel, (double)g->stall_a);
        return true;
    }
    return false;
}

static bool step_lines(const servo_test_t *t, cursor_t *c)
{
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "RESULTS PER STEP (currents in A, times in ms)");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Set V  Meas V  Idle   Moving Peak   Hold lo Hold hi "
                       "Travel Longest Moves Late");
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
            snprintf(b, n, "%5.2f  not run", (double)s->set_v);
            return true;
        }
        char vm[16], idle[16], move[16], lo[16], hi[16];
        if (s->v.n > 0u) {
            snprintf(vm, sizeof(vm), "%.2f", (double)(s->v.sum / (float)s->v.n));
        } else {
            snprintf(vm, sizeof(vm), "--");
        }
        amps(idle, sizeof(idle), &s->idle);
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
        snprintf(b, n, "%5.2f  %6s  %-6s %-6s %-6s %-7s %-7s %-6s %-7s %5u %4u%s",
                 (double)s->set_v, vm, idle, move, peak, lo, hi, mean_ms,
                 max_ms, (unsigned)s->moves, (unsigned)s->timeouts,
                 s->done ? "" : " (cut short)");
        return true;
    }
    if (!any && here(c)) {
        snprintf(b, n, "No step ran.");
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Late: moves not back at the holding level within %u ms.",
                 (unsigned)SERVO_TEST_TRAVEL_TIMEOUT_MS);
        return true;
    }
    return false;
}

static bool brownout_lines(const servo_test_t *t, cursor_t *c)
{
    char *b = c->buf;
    const size_t n = c->n;
    if (here(c)) {
        b[0] = '\0';
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "BROWN-OUT");
        return true;
    }
    if (here(c)) {
        float moved = 0.0f;
        bool stopped = false;
        const bool any = servo_test_brownout(t, &moved, &stopped);
        float first = 0.0f;
        bool ran = false;
        float last = 0.0f;
        for (unsigned k = 0; k < t->step_count; ++k) {
            if (t->steps[k].brownout && t->steps[k].done) {
                if (!ran) {
                    first = t->steps[k].set_v;
                }
                last = t->steps[k].set_v;
                ran = true;
            }
        }
        if (!t->cfg.brownout) {
            snprintf(b, n, "Not run.");
        } else if (!ran) {
            snprintf(b, n, "Not reached.");
        } else if (any && stopped) {
            snprintf(b, n, "Moved at %.2f V; no movement at %.2f V.",
                     (double)moved, (double)last);
        } else if (any) {
            snprintf(b, n, "Moved at every step down to %.2f V; lower not "
                           "tested.", (double)moved);
        } else {
            snprintf(b, n, "No movement at %.2f V, the first step.",
                     (double)first);
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "No movement: no reading of a move %.2f A away from "
                       "the level before it.", (double)SERVO_TEST_MOVE_A);
        return true;
    }
    return false;
}

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
        snprintf(b, n, "AGAINST THE LIMITS PAGE");
        return true;
    }
    float a = 0.0f;
    if (here(c)) {
        const bool m = servo_test_max_idle(t, &a);
        char lim[12];
        limit_a(lim, sizeof(lim), g->idle_max_a);
        snprintf(b, n, "Idle current     highest %.3f A, limit %s: %s",
                 (double)a, lim,
                 verdict_word(g->idle_max_a > 0.0f, m, a > g->idle_max_a));
        return true;
    }
    if (here(c)) {
        const bool m = servo_test_max_hold(t, &a);
        char lim[12];
        limit_a(lim, sizeof(lim), g->hold_max_a);
        snprintf(b, n, "Holding current  highest %.3f A, limit %s: %s",
                 (double)a, lim,
                 verdict_word(g->hold_max_a > 0.0f, m, a > g->hold_max_a));
        return true;
    }
    if (here(c)) {
        uint32_t ms = 0u;
        const bool m = servo_test_max_travel(t, &ms);
        if (g->travel_max_ms > 0u) {
            snprintf(b, n, "Travel time      longest %lu ms, limit %u ms: %s",
                     (unsigned long)ms, (unsigned)g->travel_max_ms,
                     verdict_word(true, m, ms > g->travel_max_ms));
        } else {
            snprintf(b, n, "Travel time      longest %lu ms, limit OFF: %s",
                     (unsigned long)ms, verdict_word(false, m, false));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, "Stall threshold  highest %.3f A, STALL AT %.2f A: %s",
                 (double)t->stall_peak_a, (double)g->stall_a,
                 t->stalled ? servo_str(SERVO_STR_FAIL)
                            : servo_str(SERVO_STR_PASS));
        return true;
    }
    if (here(c)) {
        unsigned late = 0u;
        for (unsigned k = 0; k < t->step_count; ++k) {
            if (!t->steps[k].brownout) {
                late += t->steps[k].timeouts;
            }
        }
        snprintf(b, n, "Moves arrived    %u late: %s", late,
                 (late > 0u) ? servo_str(SERVO_STR_FAIL)
                             : servo_str(SERVO_STR_PASS));
        return true;
    }
    return false;
}

static bool unmeasured_lines(cursor_t *c)
{
    static const char *const k_lines[] = {
        "",
        "NOT MEASURED",
        "Position: nothing measures the horn; every result is the supply's "
        "current.",
        "Current peaks between two readings: the supply reports one value a "
        "reading.",
        "The command's way from the panel to the pin, inside every travel "
        "time.",
    };
    for (size_t k = 0; k < sizeof(k_lines) / sizeof(k_lines[0]); ++k) {
        if (here(c)) {
            snprintf(c->buf, c->n, "%s", k_lines[k]);
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
    cursor_t c = { idx, 0u, buf, n };
    return header_lines(t, &c) || settings_lines(t, &c) || step_lines(t, &c)
           || brownout_lines(t, &c) || limit_lines(t, &c)
           || unmeasured_lines(&c);
}
