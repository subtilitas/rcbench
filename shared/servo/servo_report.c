/*
 * The automatic servo test's words: the CSV header, the phase and abort
 * names, and the TXT report, line by line.
 *
 * Every string the run writes or the SERVO screen shows about it is in
 * k_str, and nowhere else, so a translation is another table of the same
 * entries (the panel's German is in shared/ui/ui_text_de.c).  The report is
 * written in the language of the table the run was started with, and the
 * CSV always in English, because tools read it by its words.  A report is
 * UTF-8, as the table's strings are.
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
    [SERVO_STR_NOT_MEASURABLE]   = "NOT MEASURABLE",
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

    /* The report.  The labels pad to column 17, so the values line up. */
    [SERVO_STR_R_TITLE]          = "RCBENCH SERVO TEST REPORT",
    [SERVO_STR_R_RESULT]         = "Result:         %s",
    [SERVO_STR_R_RESULT_WHY]     = "Result:         %s - %s",
    [SERVO_STR_R_RESULT_UNSEEN]  = "Result:         %s - %u of %u counted "
                                   "moves showed no movement in the current",
    [SERVO_STR_R_RESULT_BO_UNSEEN] = "Result:         %s - no movement seen "
                                     "at %.2f V, the brown-out walk's first "
                                     "voltage",
    [SERVO_STR_R_DEVICE]         = "Device:         %s",
    [SERVO_STR_R_FIRMWARE]       = "Firmware:       rcbench %s",
    [SERVO_STR_R_LOG]            = "Log:            the .CSV with this "
                                   "file's number, one row per supply "
                                   "reading",
    [SERVO_STR_R_SUPPLY]         = "Supply:         %s",
    [SERVO_STR_R_SUPPLY_MODEL]   = "the panel's model: every reading is "
                                   "simulated",
    [SERVO_STR_R_READINGS]       = "Readings:       %.1f /s taken by the "
                                   "supply, %.1f /s reached the test",
    [SERVO_STR_R_READINGS_FEW]   = "Readings:       fewer than two",
    [SERVO_STR_R_SKIPPED]        = "Skipped:        %lu readings the supply "
                                   "took never reached the test",
    [SERVO_STR_R_RESOLUTION]     = "Resolution:     one reading every %lu "
                                   "ms: a travel time is late by up to that",
    [SERVO_STR_R_RESOLUTION_UNKNOWN] = "Resolution:     not known",
    [SERVO_STR_R_LAG]            = "Lag:            about %u ms from a change "
                                   "of current to the reading that shows it",
    [SERVO_STR_R_REPEATS]        = "Repeats:        a reading can repeat the "
                                   "last value for several readings",
    [SERVO_STR_R_UPPER_BOUND]    = "Travel times:   an upper bound, not "
                                   "checked against the limit",
    [SERVO_STR_R_DURATION]       = "Duration:       %lu.%01lu s",
    [SERVO_STR_R_ROWS]           = "Log rows:       %lu written, %lu lost to "
                                   "a full queue",
    [SERVO_STR_R_SETTINGS]       = "SETTINGS IN FORCE",
    [SERVO_STR_R_TYPE]           = "Type:           %s, centre %u us, %u-%u "
                                   "us, trim %+d us, reverse %s",
    [SERVO_STR_R_RATE]           = "Frame rate:     %u Hz",
    [SERVO_STR_R_DANGER]         = "Can destroy:    %s",
    [SERVO_STR_R_DANGER_NONE]    = "none in force",
    [SERVO_STR_R_HV]             = "HV servo:       %s",
    [SERVO_STR_R_HV_OFF]         = "OFF, no step above 6.0 V",
    [SERVO_STR_R_HV_ON_RUN]      = "ON, steps above 6.0 V run",
    [SERVO_STR_R_HV_ON_NONE]     = "ON, no step above 6.0 V chosen",
    [SERVO_STR_R_ENDS]           = "Ends:           %u us and %u us (RANGE "
                                   "%u %% of TRAVEL +/-%u deg)",
    [SERVO_STR_R_STEPS]          = "Steps:         ",
    [SERVO_STR_R_STEPS_NONE]     = " none",
    [SERVO_STR_R_BROWNOUT]       = "Brown-out:      from %.2f V down in "
                                   "%.2f V steps to %.2f V",
    [SERVO_STR_R_BROWNOUT_NOT_RUN] = "Brown-out:      not run",
    [SERVO_STR_R_I_LIMIT]        = "Current limit:  %.2f A",
    [SERVO_STR_R_TIMING]         = "Timing:         settle %u ms, idle %u "
                                   "ms, dwell %u ms (hold measured %u ms)",
    [SERVO_STR_R_LEN_MOVES]      = "Length:         %u movements a step",
    [SERVO_STR_R_LEN_TIME]       = "Length:         %u s a step",
    [SERVO_STR_R_LIMITS]         = "Limits:         idle %s, holding %s, "
                                   "travel %s, stall %.2f A",
    [SERVO_STR_R_ON]             = "ON",
    [SERVO_STR_R_OFF]            = "OFF",
    [SERVO_STR_R_PER_STEP]       = "RESULTS PER STEP (currents in A, times "
                                   "in ms)",
    /* Over the columns of the step lines' format in step_lines(). */
    [SERVO_STR_R_COLUMNS]        = "Set V  Meas V  Idle   Thresh Moving "
                                   "Peak   Hold lo Hold hi Travel Longest "
                                   "Moves Late Unseen",
    [SERVO_STR_R_STEP_NOT_RUN]   = "%5.2f  not run",
    [SERVO_STR_R_CUT_SHORT]      = " (cut short)",
    [SERVO_STR_R_NO_STEP]        = "No step ran.",
    [SERVO_STR_R_LATE]           = "Late: moves seen moving that did not "
                                   "arrive within %u ms.",
    [SERVO_STR_R_UNSEEN]         = "Unseen: moves with no movement seen; not "
                                   "timed, not counted late.",
    [SERVO_STR_R_THRESHOLD]      = "Thresh: movement is a reading max(%.3f A, "
                                   "%.0f x idle noise) from the level before "
                                   "the command.",
    [SERVO_STR_R_ARRIVAL]        = "Arrival: after a reading Thresh above "
                                   "the end's holding level, the first back "
                                   "within %.2f A of it.",
    [SERVO_STR_R_BO_HEAD]        = "BROWN-OUT",
    [SERVO_STR_R_BO_NOT_RUN]     = "Not run.",
    [SERVO_STR_R_BO_NOT_REACHED] = "Not reached.",
    [SERVO_STR_R_BO_STOPPED]     = "Moved at %.2f V; no movement seen at "
                                   "%.2f V.",
    [SERVO_STR_R_BO_ALL]         = "Moved at every step down to %.2f V; "
                                   "lower not tested.",
    [SERVO_STR_R_BO_NONE]        = "No movement seen at %.2f V, the first "
                                   "step: not measurable.",
    [SERVO_STR_R_BO_RULE]        = "No movement: no reading of a move more "
                                   "than Thresh, %.3f A at %.2f V, from the "
                                   "level before it.",
    [SERVO_STR_R_LIM_HEAD]       = "AGAINST THE LIMITS PAGE",
    [SERVO_STR_R_LIM_IDLE]       = "Idle current     highest %.3f A, limit "
                                   "%s: %s",
    [SERVO_STR_R_LIM_HOLD]       = "Holding current  highest %.3f A, limit "
                                   "%s: %s",
    [SERVO_STR_R_LIM_TRAVEL]     = "Travel time      longest %lu ms, limit "
                                   "%u ms: %s",
    [SERVO_STR_R_LIM_TRAVEL_OFF] = "Travel time      longest %lu ms, limit "
                                   "OFF: %s",
    [SERVO_STR_R_LIM_TRAVEL_BOUND] = "Travel time      longest %lu ms, limit "
                                     "%u ms: upper bound, not checked "
                                     "against the limit",
    [SERVO_STR_R_LIM_TRAVEL_NONE] = "Travel time      longest --, limit %s: "
                                    "not measured, no move arrived",
    [SERVO_STR_R_LIM_STALL]      = "Stall threshold  highest %.3f A, STALL "
                                   "AT %.2f A: %s",
    [SERVO_STR_R_LIM_LATE]       = "Moves arrived    %u late: %s",
    [SERVO_STR_R_LIM_UNSEEN]     = "Moves seen       %u unseen: %s",
    [SERVO_STR_R_LIM_BO_SEEN]    = "Brown-out start  movement seen at %.2f "
                                   "V: %s",
    [SERVO_STR_R_LIM_BO_UNSEEN]  = "Brown-out start  no movement seen at "
                                   "%.2f V: %s",
    [SERVO_STR_R_NOT_CHECKED]    = "not checked",
    [SERVO_STR_R_NOT_MEASURED]   = "not measured",
    [SERVO_STR_R_UNM_HEAD]       = "NOT MEASURED",
    [SERVO_STR_R_UNM_POSITION]   = "Position: nothing measures the horn; "
                                   "every result is the supply's current.",
    [SERVO_STR_R_UNM_PEAKS]      = "Current peaks between two readings: the "
                                   "supply reports one value a reading.",
    [SERVO_STR_R_UNM_PATH]       = "The command's way from the panel to the "
                                   "pin, inside every travel time.",
};

const char *servo_str_in(const char *const *table, servo_str_t id)
{
    if ((unsigned)id >= (unsigned)SERVO_STR_COUNT) {
        return "";
    }
    if (table != NULL && table[id] != NULL) {
        return table[id];
    }
    return (k_str[id] != NULL) ? k_str[id] : "";
}

const char *servo_str(servo_str_t id)
{
    return servo_str_in(NULL, id);
}

servo_str_t servo_test_phase_str(servo_test_phase_t ph)
{
    if ((unsigned)ph > (unsigned)SERVO_TEST_PH_HOLD) {
        return SERVO_STR_PHASE_NONE;
    }
    return (servo_str_t)(SERVO_STR_PHASE_NONE + (int)ph);
}

servo_str_t servo_test_abort_str(servo_test_abort_t why)
{
    if ((unsigned)why >= (unsigned)SERVO_TEST_AB_COUNT) {
        return SERVO_STR_AB_NONE;
    }
    return (servo_str_t)(SERVO_STR_AB_NONE + (int)why);
}

servo_str_t servo_test_verdict_str(servo_test_verdict_t v)
{
    switch (v) {
    case SERVO_TEST_PASS: return SERVO_STR_PASS;
    case SERVO_TEST_FAIL: return SERVO_STR_FAIL;
    case SERVO_TEST_NOT_MEASURABLE: return SERVO_STR_NOT_MEASURABLE;
    default:              return SERVO_STR_ABORTED;
    }
}

servo_str_t servo_test_start_str(servo_test_start_t why)
{
    if ((unsigned)why > (unsigned)SERVO_TEST_START_BAD_ENDS) {
        return SERVO_STR_START_OK;
    }
    return (servo_str_t)(SERVO_STR_START_OK + (int)why);
}

const char *servo_test_phase_name(servo_test_phase_t ph)
{
    return servo_str(servo_test_phase_str(ph));
}

const char *servo_test_abort_name(servo_test_abort_t why)
{
    return servo_str(servo_test_abort_str(why));
}

const char *servo_test_verdict_name(servo_test_verdict_t v)
{
    return servo_str(servo_test_verdict_str(v));
}

const char *servo_test_start_name(servo_test_start_t why)
{
    return servo_str(servo_test_start_str(why));
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
    const char *const *text;    /* the run's language; NULL is English */
} cursor_t;

/* A word or a line format of the report, in the run's language. */
#define S(id) servo_str_in(c->text, SERVO_STR_##id)

static bool here(cursor_t *c)
{
    return c->want == c->at++;
}

/* A limit's value, or OFF for 0. */
static void limit_a(const cursor_t *c, char *b, size_t n, float a)
{
    if (a > 0.0f) {
        snprintf(b, n, "%.2f A", (double)a);
    } else {
        snprintf(b, n, "%s", S(R_OFF));
    }
}

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

/* A mean in amperes, or "--" for none. */
static void amps(char *b, size_t n, const servo_test_mean_t *m)
{
    if (m->n > 0u) {
        snprintf(b, n, "%.3f", (double)(m->sum / (float)m->n));
    } else {
        snprintf(b, n, "--");
    }
}

/* The first and the last brown-out voltage that ran to its end; NULL for
 * none. */
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
    if (here(c)) {
        snprintf(b, n, "%s", S(R_LOG));
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_SUPPLY), g->model ? S(R_SUPPLY_MODEL)
                                    : (g->meter.name[0] != '\0')
                                          ? g->meter.name : "--");
        return true;
    }
    float per_s = 0.0f, module_s = 0.0f;
    uint32_t every = 0u;
    const bool rated = servo_test_rates(t, &per_s, &module_s, &every);
    if (here(c)) {
        if (rated) {
            snprintf(b, n, S(R_READINGS), (double)module_s, (double)per_s);
        } else {
            snprintf(b, n, "%s", S(R_READINGS_FEW));
        }
        return true;
    }
    if (here(c)) {
        snprintf(b, n, S(R_SKIPPED), (unsigned long)t->skipped);
        return true;
    }
    if (here(c)) {
        if (rated) {
            snprintf(b, n, S(R_RESOLUTION), (unsigned long)every);
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
        char vm[16], idle[16], thr[16], move[16], lo[16], hi[16];
        if (s->v.n > 0u) {
            snprintf(vm, sizeof(vm), "%.2f", (double)(s->v.sum / (float)s->v.n));
        } else {
            snprintf(vm, sizeof(vm), "--");
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
                 "%5.2f  %6s  %-6s %-6s %-6s %-6s %-7s %-7s %-6s %-7s %5u %4u "
                 "%6u%s%s%s",
                 (double)s->set_v, vm, idle, thr, move, peak, lo, hi, mean_ms,
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
                 verdict_word(c, g->idle_max_a > 0.0f, m, a > g->idle_max_a));
        return true;
    }
    if (here(c)) {
        const bool m = servo_test_max_hold(t, &a);
        char lim[24];
        limit_a(c, lim, sizeof(lim), g->hold_max_a);
        snprintf(b, n, S(R_LIM_HOLD), (double)a, lim,
                 verdict_word(c, g->hold_max_a > 0.0f, m, a > g->hold_max_a));
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

static bool unmeasured_lines(cursor_t *c)
{
    const servo_str_t k_lines[] = {
        SERVO_STR_COUNT,            /* the blank line before the heading */
        SERVO_STR_R_UNM_HEAD,
        SERVO_STR_R_UNM_POSITION,
        SERVO_STR_R_UNM_PEAKS,
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
           || brownout_lines(t, &c) || limit_lines(t, &c)
           || unmeasured_lines(&c);
}
