/*
 * The automatic servo test on the INA3221's CH1 windows.
 *
 * Two rigs.  The bench: the modelled INA3221 of fake_ina.h carries
 * servo_sim's current, the coprocessor's schedule and pages turn it into
 * 50 ms windows, the panel's sense_link takes each once, servo_source names
 * the meter, and the run reads what a render loop hands it every 20 ms
 * (sense_chain.h), beside a model of the PD mini read every 104 ms.  The
 * hand: windows and supply readings given at the millisecond a case names.
 *
 * Under test: a full run on each meter, the figures of the one never in
 * the other's file; the meter held from the start to the end of a run, and
 * a run on the INA3221 ended, never moved to the PD mini, when the part
 * stops answering, resets itself, the link goes, each condition of
 * servo_source.h fails, or no window arrives for 500 ms (at 499, 500 and
 * 501 ms); a run on the PD mini untouched by the INA3221 becoming the
 * meter; STALL AT on the window's mean at 1 mA under, at and 1 mA over,
 * its abort at 999, 1000 and 1001 ms and 20 windows in a row; constant
 * current for 999 and 1000 ms on either meter, and a spell of it shorter
 * than that counted and failing nothing; STALL AT under, at and over the
 * current limit and the INA3221's range; every ending with the output
 * asked off once, the servo let go, the report marked, and nothing fed to
 * the run afterwards changing its verdict, its report or its file; every
 * timer across the 2^32 ms tick wrap; a negative current at rest; clipped
 * windows counted and used as the values they are; the CSV read back by
 * the viewer's parser, with and without the encoder's columns, beside
 * files of the 13-column format; and a bench log recorded on 0.14.0,
 * which holds 35 % of its windows, ending a run at its first gap.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "sense_chain.h"
#include "servo_sim.h"
#include "servo_source.h"
#include "servo_test.h"

#define LO_US   1100u
#define HI_US   1900u
#define CENTRE  1500u

/* The run's abort for the condition that took the INA3221 off the rail, as
 * the SERVO screen maps it. */
static servo_test_abort_t ab_of(servo_source_why_t why)
{
    switch (why) {
    case SERVO_SOURCE_WHY_NONE:      return SERVO_TEST_AB_NONE;
    case SERVO_SOURCE_WHY_NO_LINK:   return SERVO_TEST_AB_LINK;
    case SERVO_SOURCE_WHY_OFF:
    case SERVO_SOURCE_WHY_OLD:
    case SERVO_SOURCE_WHY_NOT_HELD:  return SERVO_TEST_AB_INA_SETUP;
    case SERVO_SOURCE_WHY_SILENT:    return SERVO_TEST_AB_INA_SILENT;
    case SERVO_SOURCE_WHY_NO_WINDOW: return SERVO_TEST_AB_INA_NO_WINDOW;
    case SERVO_SOURCE_WHY_RESET:     return SERVO_TEST_AB_INA_RESET;
    default:                         return SERVO_TEST_AB_INA_METER;
    }
}

static servo_test_meter_now_t meter_of(const servo_source_t *s)
{
    servo_test_meter_now_t m;
    memset(&m, 0, sizeof(m));
    m.ina3221 = servo_source_id(s) == SERVO_SOURCE_INA3221;
    m.changes = servo_source_changes(s);
    m.dropped = ab_of(servo_source_dropped(s, &m.dropped_at));
    return m;
}

/* What a run hands over, kept: its report, its CSV, and how often each
 * kind came. */
typedef struct {
    unsigned opens, rows, txt, ends;
    char     report[16384];
    size_t   report_len;
    char    *csv;
    size_t   csv_len, csv_cap;
} out_t;

static void out_reset(out_t *o)
{
    free(o->csv);
    memset(o, 0, sizeof(*o));
}

static void out_drain(out_t *o, servo_test_t *t)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t k = servo_test_peek(t, &text);
        if (k == SERVO_TEST_OUT_NONE) {
            return;
        }
        const size_t n = strlen(text);
        if (k == SERVO_TEST_OUT_OPEN) {
            ++o->opens;
        } else if (k == SERVO_TEST_OUT_END) {
            ++o->ends;
        } else if (k == SERVO_TEST_OUT_TXT) {
            ++o->txt;
            if (o->report_len + n + 2u < sizeof(o->report)) {
                memcpy(o->report + o->report_len, text, n);
                o->report_len += n;
                o->report[o->report_len++] = '\n';
                o->report[o->report_len] = '\0';
            }
        } else {
            ++o->rows;                  /* the header is the first */
            if (o->csv_len + n + 2u > o->csv_cap) {
                o->csv_cap = (o->csv_cap + n + 2u) * 2u;
                o->csv = realloc(o->csv, o->csv_cap);
            }
            memcpy(o->csv + o->csv_len, text, n);
            o->csv_len += n;
            o->csv[o->csv_len++] = '\n';
            o->csv[o->csv_len] = '\0';
        }
        servo_test_pop(t);
    }
}

/* Cell @p idx of CSV row @p line into @p cell; false when the row has
 * fewer. */
static bool cell_of(const char *line, unsigned idx, char *cell, size_t n)
{
    const char *p = line;
    for (unsigned k = 0u; k < idx; ++k) {
        p = strchr(p, ';');
        if (p == NULL || memchr(line, '\n', (size_t)(p - line)) != NULL) {
            return false;
        }
        ++p;
    }
    size_t len = strcspn(p, ";\n");
    if (len >= n) {
        len = n - 1u;
    }
    memcpy(cell, p, len);
    cell[len] = '\0';
    return true;
}

/* Rows of @p csv after the header whose cell @p idx reads @p want. */
static unsigned rows_with(const char *csv, unsigned idx, const char *want)
{
    unsigned n = 0u;
    for (const char *p = strchr(csv, '\n'); p != NULL && p[1] != '\0';
         p = strchr(p + 1, '\n')) {
        char cell[32];
        if (cell_of(p + 1, idx, cell, sizeof(cell))
            && strcmp(cell, want) == 0) {
            ++n;
        }
    }
    return n;
}

/* The CSV's columns, by their place. */
enum {
    COL_TIME = 0, COL_PHASE = 3, COL_VOLTAGE = 7, COL_CURRENT = 9,
    COL_MODE = 11, COL_TRAVEL = 12,
    COL_METER = 13, COL_WINDOW, COL_I_MAX, COL_I_MIN, COL_V_MIN, COL_CLIPPED,
    COLS = 19,
};

/* =============================================================== the hand */

typedef struct {
    servo_test_t t;
    out_t    out;
    uint32_t now;
    bool     armed;
    float    v_max;
    /* The supply as the PD mini reports it. */
    bool     output, online;
    uint8_t  trip;
    float    set_v, set_i;
    bool     take_set, take_on;
    bool     cc;            /* it reports constant current               */
    uint16_t pd_samples;
    bool     pd_on;         /* readings arrive, every 104 ms             */
    uint32_t pd_next;
    float    pd_a;          /* the current it reads                      */
    /* The windows. */
    bool     win_on;        /* they arrive, every 50 ms                  */
    uint32_t win_next;
    uint16_t win_number;
    int      rest_ma;       /* at rest and holding                       */
    int      move_ma;       /* for 200 ms from a command to an end       */
    int      force_ma;      /* every window, when not INT16_MIN          */
    unsigned clipped;       /* samples at an end in every window         */
    uint16_t cmd;
    uint32_t cmd_ms;
    bool     moving;
    /* What the run asked. */
    unsigned offs, releases, commands;
    float    set_v_max;
} hand_t;

static hand_t h;

static void h_fresh(uint32_t tick0)
{
    out_reset(&h.out);
    memset(&h, 0, sizeof(h));
    servo_test_init(&h.t);
    h.now      = tick0;
    h.armed    = true;
    h.v_max    = 20.0f;
    h.online   = true;
    h.take_set = true;
    h.take_on  = true;
    h.pd_on    = true;
    h.win_on   = true;
    h.pd_next  = tick0;
    h.win_next = tick0;
    h.rest_ma  = 20;
    h.move_ma  = 400;
    h.force_ma = INT16_MIN;
    h.pd_a     = 0.005f;
    h.cmd      = CENTRE;
    h.set_v    = 5.0f;
    h.set_i    = 2.0f;
}

/* One step at 4.80 V, two counted moves, STALL AT 1.00 A under a limit of
 * 2.00 A, read by @p ina the INA3221 on 0.1 Ohm or the PD mini. */
static void h_cfg(servo_test_cfg_t *c, bool ina)
{
    memset(c, 0, sizeof(*c));
    c->steps_v[0] = 4.8f;
    c->step_count = 1u;
    c->i_limit    = 2.0f;
    c->centre_us  = CENTRE;
    c->end_lo_us  = LO_US;
    c->end_hi_us  = HI_US;
    c->settle_ms  = 100u;
    c->dwell_ms   = 200u;
    c->by_moves   = true;
    c->moves      = 2u;
    c->stall_a    = 1.0f;
    c->report     = true;
    snprintf(c->dut, sizeof(c->dut), "SERVO");
    snprintf(c->type, sizeof(c->type), "STANDARD PWM");
    snprintf(c->firmware, sizeof(c->firmware), "0.0.0");
    if (ina) {
        servo_test_meter_ina3221(&c->meter, 1000u);
        c->meter_changes = 2u;
    } else {
        servo_test_meter_pdmini(&c->meter);
    }
}

static void h_start(const servo_test_cfg_t *c)
{
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = h.online;
    last.output = h.output;
    CHECK_EQ(servo_test_start(&h.t, c, h.now, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
}

/* A reading of the supply taken at @p at. */
static void h_pd_at(uint32_t at)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.online   = h.online;
    r.output   = h.output;
    r.ok       = true;
    r.trip     = h.trip;
    r.set_v    = h.set_v;
    r.set_i    = h.set_i;
    r.v        = h.output ? h.set_v : 0.0f;
    r.i        = h.output ? h.pd_a : 0.0f;
    r.mode     = !h.output ? 0u : h.cc ? 2u : 1u;
    r.samples  = ++h.pd_samples;
    r.taken_ms = at;
    servo_test_reading(&h.t, &r, 0u);
}

/* A window of @p ma mean taken at @p at: its highest sample 50 mA above
 * the mean, its lowest 5 mA under. */
static void h_win_at(uint32_t at, int ma)
{
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.number   = ++h.win_number;
    w.current  = true;
    w.voltage  = true;
    w.clipped  = (uint8_t)h.clipped;
    w.mean_ma  = (int16_t)ma;
    w.max_ma   = (int16_t)(ma + 50);
    w.min_ma   = (int16_t)(ma - 5);
    w.mean_mv  = (uint16_t)(lroundf(h.set_v * 1000.0f) - 8);
    w.min_mv   = (uint16_t)(w.mean_mv - 200u);
    w.taken_ms = at;
    servo_test_window(&h.t, &w, 0u);
}

/* The current the windows read now. */
static int h_ma(void)
{
    if (h.force_ma != INT16_MIN) {
        return h.force_ma;
    }
    if (!h.output) {
        return 0;
    }
    return (h.moving && (uint32_t)(h.now - h.cmd_ms) < 200u) ? h.move_ma
                                                             : h.rest_ma;
}

/* One pass of the run at @p at, and what it asks done. */
static void h_step_at(uint32_t at)
{
    const servo_test_in_t in = { h.armed, h.v_max };
    servo_test_do_t d;
    servo_test_step(&h.t, at, &in, &d);
    if (d.set && h.take_set) {
        h.set_v = d.set_v;
        h.set_i = d.set_i;
        if (d.set_v > h.set_v_max) {
            h.set_v_max = d.set_v;
        }
    }
    if (d.on && h.take_on) {
        h.output = true;
    }
    if (d.off) {
        ++h.offs;
        h.output = false;
    }
    if (d.command) {
        ++h.commands;
        h.cmd    = d.cmd_us;
        h.cmd_ms = at;
        h.moving = d.cmd_us != CENTRE;
    }
    if (d.release) {
        ++h.releases;
        h.cmd    = CENTRE;
        h.moving = false;
    }
    out_drain(&h.out, &h.t);
}

/* One millisecond: the windows and readings due, and a pass. */
static void h_ms(void)
{
    ++h.now;
    if (h.win_on && (int32_t)(h.now - h.win_next) >= 0) {
        h.win_next += SERVO_TEST_WIN_MS;
        h_win_at(h.now, h_ma());
    }
    if (h.pd_on && (int32_t)(h.now - h.pd_next) >= 0) {
        h.pd_next += 104u;
        h_pd_at(h.now);
    }
    h_step_at(h.now);
}

static void h_run_ms(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; ++k) {
        h_ms();
    }
}

/* Until the run is in @p phase, at most 20 s. */
static bool h_until(servo_test_phase_t phase)
{
    for (uint32_t k = 0u; k < 20000u && servo_test_running(&h.t); ++k) {
        if (h.t.phase == phase) {
            return true;
        }
        h_ms();
    }
    return servo_test_running(&h.t) && h.t.phase == phase;
}

/* Until the run is over and handed over, at most 60 s. */
static void h_out(void)
{
    for (uint32_t k = 0u; k < 60000u && !servo_test_drained(&h.t); ++k) {
        h_ms();
    }
    CHECK(servo_test_drained(&h.t));
}

/* A run on the INA3221 in IDLE at tick @p tick0, the automatic windows
 * and readings off: the case feeds its own from here. */
static void h_idle_by_hand(uint32_t tick0, const servo_test_cfg_t *c)
{
    h_fresh(tick0);
    h_start(c);
    CHECK(h_until(SERVO_TEST_PH_IDLE));
    h.win_on = false;
    h.pd_on  = false;
}

/* The start tick that puts a run's time @p rel ms after its start 500 ms
 * before the tick wraps. */
static uint32_t wrap_at(uint32_t rel)
{
    return (uint32_t)(0u - 500u - rel);
}

/*
 * The run ended as @p why: the output asked off once and the servo let go
 * once, the verdict ABORTED, the report's first line after the title
 * naming the reason.  Then two seconds of everything a bench can still
 * hand it -- windows of 1.5 A, supply readings in constant current and
 * then not answering, a meter that changed, passes with the bench
 * disarmed -- change neither the reason, the report, the file nor what was
 * asked.
 */
static void h_check_ended(servo_test_abort_t why)
{
    CHECK(!servo_test_running(&h.t));
    CHECK_EQ(h.t.why, why);
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_ABORTED);
    for (uint32_t k = 0u; k < 2000u && !servo_test_drained(&h.t); ++k) {
        ++h.now;
        h_step_at(h.now);
    }
    CHECK(servo_test_drained(&h.t));
    CHECK_EQ(h.offs, 1u);
    CHECK_EQ(h.releases, 1u);
    CHECK(!h.output);
    CHECK_EQ(h.out.opens, 1u);
    CHECK_EQ(h.out.ends, 1u);
    char mark[96];
    snprintf(mark, sizeof(mark), "Result:         ABORTED - %s\n",
             servo_test_abort_name(why));
    CHECK(strlen(servo_test_abort_name(why)) > 0u);
    CHECK(strstr(h.out.report, mark) != NULL);

    const unsigned rows = h.out.rows, txt = h.out.txt;
    const unsigned commands = h.commands;
    const uint32_t end_ms = h.t.end_ms;
    char first[SERVO_TEST_LINE_MAX], line[SERVO_TEST_LINE_MAX];
    CHECK(servo_report_line(&h.t, 1u, first, sizeof(first)));
    h.cc       = true;
    h.force_ma = 1500;
    h.win_on   = true;
    h.pd_on    = true;
    h.win_next = h.now;
    h.pd_next  = h.now;
    h_run_ms(1000u);
    h.online = false;
    h.armed  = false;
    const servo_test_meter_now_t gone = {
        .ina3221 = false, .changes = 9u,
        .dropped = SERVO_TEST_AB_INA_RESET, .dropped_at = 9u,
    };
    servo_test_meter_now(&h.t, &gone, h.now);
    servo_test_abort(&h.t, SERVO_TEST_AB_STOP, h.now);
    h_run_ms(1000u);
    CHECK_EQ(h.t.why, why);
    CHECK_EQ(h.t.end_ms, end_ms);
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_ABORTED);
    CHECK(servo_report_line(&h.t, 1u, line, sizeof(line)));
    CHECK_STR_EQ(line, first);
    CHECK_EQ(h.out.rows, rows);
    CHECK_EQ(h.out.txt, txt);
    CHECK_EQ(h.out.ends, 1u);
    CHECK_EQ(h.offs, 1u);
    CHECK_EQ(h.releases, 1u);
    CHECK_EQ(h.commands, commands);
}

/* ------------------------------------------------- the meter of a run */

/* A run on the INA3221 by hand: every row a window, the figures the
 * windows', the supply's 0.005 A in none of them, and the report naming
 * the meter, the shunt and what the travel times are worth. */
TEST_CASE(a_run_reads_the_windows_and_none_of_the_supplys_readings)
{
    h_fresh(5000u);
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.travel_max_ms = 100u;         /* under every travel time: not checked */
    h_start(&c);
    h_out();
    CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_PASS);
    CHECK_EQ(h.offs, 1u);
    CHECK_EQ(h.releases, 1u);
    const servo_test_step_t *s = &h.t.steps[0];
    CHECK(s->done);
    CHECK_EQ(s->moves, 2u);
    CHECK_EQ(s->travels, 2u);
    /* IDLE's 1000 ms hold 19 to 20 windows: one that began before it is
     * not in it. */
    CHECK(s->idle.n >= 19u && s->idle.n <= 20u);
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.020f, 0.0005f);
    /* Two visits to each end, a window every 50 ms from the arrival. */
    CHECK(s->hold[0].n >= 22u && s->hold[0].n <= 24u);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.020f, 0.0005f);
    /* The voltage is CH1's bus voltage, and its lowest the lowest sample. */
    CHECK_NEAR(s->v.sum / (float)s->v.n, 4.792f, 0.0005f);
    CHECK_NEAR(s->v_min, 4.592f, 0.0005f);
    /* The peak is a 1 ms sample, 50 mA above the highest mean. */
    CHECK_NEAR(s->move_peak_a, 0.450f, 0.0005f);
    CHECK_NEAR(s->move.sum / (float)s->move.n, 0.400f, 0.0005f);
    /* 200 ms of movement: seen in the window after the command and gone
     * in the one after it ends, at most one window and a pass late. */
    CHECK(s->travel_max_ms >= 200u);
    CHECK(s->travel_max_ms <= 200u + 2u * SERVO_TEST_WIN_MS);
    CHECK_EQ(servo_test_travel_window_ms(&h.t), 3050u);

    /* Every row is a window. */
    CHECK_EQ(h.out.rows - 1u, h.t.readings);
    CHECK_EQ(h.t.skipped, 0u);
    CHECK(strncmp(h.out.csv, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "INA3221"), h.out.rows - 1u);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), 0u);
    CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "0.005"), 0u);
    CHECK(rows_with(h.out.csv, COL_CURRENT, "0.020") > 30u);
    CHECK(rows_with(h.out.csv, COL_I_MAX, "0.450") >= 8u);
    CHECK(rows_with(h.out.csv, COL_V_MIN, "4.592") > 30u);
    /* The supply's mode, as its last reading had it: none before the
     * first, OFF until the output is on. */
    CHECK_EQ(rows_with(h.out.csv, COL_MODE, "CV")
                 + rows_with(h.out.csv, COL_MODE, "OFF")
                 + rows_with(h.out.csv, COL_MODE, ""), h.out.rows - 1u);
    CHECK_EQ(rows_with(h.out.csv, COL_MODE, ""), 1u);
    CHECK(rows_with(h.out.csv, COL_MODE, "OFF") <= 3u);

    const char *r = h.out.report;
    CHECK(strstr(r, "Log:            the .CSV with this file's number, one "
                    "row per INA3221 window\n") != NULL);
    CHECK(strstr(r, "Supply:         PD mini\n") != NULL);
    CHECK(strstr(r, "Current:        INA3221 CH1, shunt 100.0 mOhm, range "
                    "1.638 A\n") != NULL);
    CHECK(strstr(r, "Voltage:        INA3221 CH1, load side of the shunt\n")
          != NULL);
    CHECK(strstr(r, "Shunt:          up to 0.164 V lost across it at the "
                    "range") != NULL);
    CHECK(strstr(r, "Readings:       20.0 /s windows closed by the INA3221, "
                    "20.0 /s reached the test\n") != NULL);
    CHECK(strstr(r, "Skipped:        0 windows the INA3221 closed never "
                    "reached the test\n") != NULL);
    CHECK(strstr(r, "Resolution:     one window every 50 ms: a travel "
                    "time is late by up to two windows and the poll that "
                    "reads them\n") != NULL);
    CHECK(strstr(r, "Lag:            about 50 ms") != NULL);
    CHECK(strstr(r, "Repeats:") == NULL);
    CHECK(strstr(r, "Travel times:   an upper bound, not checked against "
                    "the limit\n") != NULL);
    CHECK(strstr(r, "limit 100 ms: upper bound, not checked against the "
                    "limit\n") != NULL);
    CHECK(strstr(r, "Clipped:") == NULL);
    CHECK(strstr(r, "Const. current: 0 supply readings, longest stretch 0 "
                    "ms\n") != NULL);
    CHECK(strstr(r, " 4.80    4.79  4.59   0.020  0.020  0.400  0.450  ")
          != NULL);
    CHECK(strstr(r, "Position: nothing measures the horn; every result is "
                    "the INA3221's current on CH1.\n") != NULL);
    CHECK(strstr(r, "every result is the supply's current") == NULL);
    CHECK(strstr(r, "INA3221:        not used") == NULL);
}

/* The same bench with the run on the PD mini: every row a reading of the
 * supply, no window in the file or in a figure, and the report saying why
 * the INA3221 that SETUP has on was not read. */
TEST_CASE(a_run_on_the_pd_mini_reads_no_window)
{
    static const struct {
        servo_test_ina_t why;
        const char      *text;
    } k_why[] = {
        { SERVO_TEST_INA_OLD, "the coprocessor is older than link protocol "
                              "4.11" },
        { SERVO_TEST_INA_NOT_HELD, "the coprocessor does not hold its "
                                   "set-up" },
        { SERVO_TEST_INA_SILENT, "it does not answer" },
        { SERVO_TEST_INA_NO_WINDOW, "no window with current in the last "
                                    "200 ms" },
        { SERVO_TEST_INA_RESET, "it reset itself" },
        { SERVO_TEST_INA_SETTLING, "it has worked for less than 1 s" },
        { SERVO_TEST_INA_MODEL, "the supply is the panel's model" },
    };
    for (size_t k = 0u; k < sizeof(k_why) / sizeof(k_why[0]); ++k) {
        h_fresh(5000u);
        h.rest_ma = 700;            /* the INA3221 reads another current */
        servo_test_cfg_t c;
        h_cfg(&c, false);
        c.ina_why = (uint8_t)k_why[k].why;
        h_start(&c);
        /* The INA3221 becomes the meter under the run, and stops being it:
         * nothing to a run that reads the PD mini. */
        h_until(SERVO_TEST_PH_IDLE);
        const servo_test_meter_now_t came = { .ina3221 = true,
                                              .changes = 3u };
        servo_test_meter_now(&h.t, &came, h.now);
        h_run_ms(300u);
        const servo_test_meter_now_t went = {
            .ina3221 = false, .changes = 4u,
            .dropped = SERVO_TEST_AB_INA_SILENT, .dropped_at = 4u,
        };
        servo_test_meter_now(&h.t, &went, h.now);
        CHECK(servo_test_running(&h.t));
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        /* 0.005 A at rest and moving alike: no move is seen. */
        CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_NOT_MEASURABLE);
        CHECK_EQ(h.out.rows - 1u, h.t.readings);
        CHECK_EQ(h.t.readings, h.pd_samples);
        CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_WINDOW, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_I_MAX, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_CLIPPED, ""), h.out.rows - 1u);
        CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "0.700"), 0u);
        const servo_test_step_t *s = &h.t.steps[0];
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.005f, 0.0001f);
        CHECK_NEAR(s->v_min, 4.8f, 0.0001f);
        CHECK_EQ(h.t.clipped, 0u);
        char want[160];
        snprintf(want, sizeof(want), "INA3221:        not used: %s\n",
                 k_why[k].text);
        CHECK(strstr(h.out.report, want) != NULL);
        CHECK(strstr(h.out.report, "Supply:         PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Current:        PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Voltage:        PD mini\n") != NULL);
        CHECK(strstr(h.out.report, "Shunt:") == NULL);
        CHECK(strstr(h.out.report, "one row per supply reading") != NULL);
    }
    /* With the INA3221 off in SETUP the line is not written. */
    h_fresh(5000u);
    servo_test_cfg_t c;
    h_cfg(&c, false);
    h_start(&c);
    h_out();
    CHECK(strstr(h.out.report, "INA3221:") == NULL);
    /* A window handed to a run that is not running, and a run without a
     * window, take nothing. */
    servo_test_window(NULL, NULL, 0u);
    servo_test_window(&h.t, NULL, 0u);
    servo_test_meter_now(NULL, NULL, 0u);
    servo_test_meter_now(&h.t, NULL, 0u);
}

/* The meter's description: the range from the shunt SETUP holds. */
TEST_CASE(the_ina3221s_range_follows_its_shunt)
{
    servo_test_meter_t m;
    servo_test_meter_ina3221(&m, 1000u);        /* 0.1 Ohm */
    CHECK_EQ(m.kind, SERVO_TEST_METER_INA3221);
    CHECK_STR_EQ(m.name, "INA3221 CH1");
    CHECK_EQ(m.span_ms, 50u);
    CHECK_EQ(m.lag_ms, 50u);
    CHECK(!m.repeats);
    CHECK(m.upper_bound);
    CHECK_EQ(m.range_ma, 1638u);
    servo_test_meter_ina3221(&m, 500u);         /* 0.05 Ohm */
    CHECK_EQ(m.range_ma, 3276u);
    servo_test_meter_ina3221(&m, 50u);          /* 5 mOhm, SETUP's least */
    CHECK_EQ(m.range_ma, 32760u);
    servo_test_meter_ina3221(&m, 10u);          /* held to the field */
    CHECK_EQ(m.range_ma, 65535u);
    servo_test_meter_ina3221(&m, 0u);
    CHECK_EQ(m.range_ma, 0u);
    servo_test_meter_ina3221(NULL, 1000u);
    servo_test_meter_model(&m);
    CHECK_EQ(m.kind, SERVO_TEST_METER_MODEL);
    CHECK_EQ(m.lag_ms, 0u);
    CHECK(!m.repeats);
    CHECK(m.upper_bound);
    servo_test_meter_model(NULL);
    servo_test_meter_pdmini(&m);
    CHECK_EQ(m.kind, SERVO_TEST_METER_PDMINI);
    CHECK_EQ(m.span_ms, 0u);
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_INA3221), "INA3221");
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_PDMINI), "PDMINI");
    CHECK_STR_EQ(servo_test_meter_word(SERVO_TEST_METER_MODEL), "MODEL");
    CHECK_STR_EQ(servo_test_meter_word(99u), "PDMINI");
    CHECK_EQ(SERVO_TEST_WIN_MS, SENSE_WINDOW_MS);
}

/* A window that repeats the last number, one without current samples and
 * one without voltage samples are no reading: no row, no figure, and none
 * of them a window that arrived. */
TEST_CASE(a_window_without_both_quantities_is_no_reading)
{
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_idle_by_hand(5000u, &c);
    const uint32_t readings = h.t.readings;
    const uint32_t last = h.t.last_ms;
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.number   = h.win_number;              /* the last one again */
    w.current  = true;
    w.voltage  = true;
    w.mean_ma  = 900;
    w.taken_ms = h.now + 10u;
    servo_test_window(&h.t, &w, 0u);
    w.number  = (uint16_t)(h.win_number + 1u);
    w.current = false;
    servo_test_window(&h.t, &w, 0u);
    w.current = true;
    w.voltage = false;
    servo_test_window(&h.t, &w, 0u);
    CHECK_EQ(h.t.readings, readings);
    CHECK_EQ(h.t.last_ms, last);
    /* With both it is one, and a number two ahead counts one skipped. */
    w.voltage = true;
    w.number  = (uint16_t)(h.win_number + 2u);
    servo_test_window(&h.t, &w, 0u);
    CHECK_EQ(h.t.readings, readings + 1u);
    CHECK_EQ(h.t.skipped, 1u);
    CHECK_EQ(h.t.last_ms, h.now + 10u);
}

/* ------------------------------------------- no window, and no reading */

/* No window for 500 ms ends a run on the INA3221: a pass 499 ms after the
 * last window leaves it running, one at 500 ms and one at 501 ms end it.
 * The supply's readings go on, and so does nothing else.  At tick 5000 and
 * with the 500 ms across the tick wrap. */
TEST_CASE(no_window_for_500_ms_ends_the_run)
{
    static const uint32_t k_after[] = { 499u, 500u, 501u };
    servo_test_cfg_t c;
    h_cfg(&c, true);
    /* Where IDLE begins, from the start. */
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.t.last_ms - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            const uint32_t last = h.t.last_ms;
            if (i > 0u) {
                /* The 500 ms end on the wrap: tick 0 is the 500th. */
                CHECK_EQ(last, 0u - 500u);
            }
            /* The supply answers throughout; passes run to 498 ms. */
            for (uint32_t at = h.now + 1u; (int32_t)(at - last) < 499;
                 ++at) {
                if ((at - last) % 100u == 0u) {
                    h_pd_at(at);
                }
                h_step_at(at);
                h.now = at;
            }
            CHECK(servo_test_running(&h.t));
            h.now = last + k_after[k];
            h_step_at(h.now);
            if (k_after[k] < SERVO_TEST_WIN_STALE_MS) {
                CHECK(servo_test_running(&h.t));
                CHECK_EQ(h.offs, 0u);
                /* And a window then keeps it running past 500 ms. */
                h_win_at(h.now, 20);
                h.now += 400u;
                h_step_at(h.now);
                CHECK(servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, last + k_after[k]);
            h_check_ended(SERVO_TEST_AB_WIN_STALE);
            CHECK(strstr(h.out.report,
                         "Result:         ABORTED - no INA3221 window for "
                         "0.5 s\n") != NULL);
        }
    }
    /* A run that never has a window ends 500 ms after its start. */
    h_fresh(wrap_at(0u));
    h.win_on = false;
    h_start(&c);
    const uint32_t start = h.now;
    h_run_ms(499u);
    CHECK(servo_test_running(&h.t));
    h_ms();
    CHECK_EQ(h.t.end_ms, start + 500u);
    h_check_ended(SERVO_TEST_AB_WIN_STALE);
}

/* The supply's own silence ends a run on the INA3221 as it ends one on the
 * PD mini: no new reading for more than 1500 ms, the windows arriving
 * throughout.  Running at 1500 ms, ended at 1501, and across the wrap. */
TEST_CASE(no_supply_reading_for_1500_ms_ends_a_run_on_the_ina3221)
{
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_IDLE));
    const uint32_t rel = h.t.sup_ms - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel + 700u) };
    for (size_t i = 0u; i < 2u; ++i) {
        h_fresh(k_tick0[i]);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_IDLE));
        h.pd_on = false;
        const uint32_t heard = h.t.sup_ms;
        while (h.now != heard + SERVO_TEST_STALE_MS) {
            h_ms();
        }
        CHECK(servo_test_running(&h.t));
        h_ms();
        CHECK_EQ(h.t.end_ms, heard + SERVO_TEST_STALE_MS + 1u);
        if (i > 0u) {
            CHECK(heard > 0xFFFFF000u && h.t.end_ms < 2000u);
        }
        h_check_ended(SERVO_TEST_AB_STALE);
    }
}

/* ------------------------------------------------------------- stall */

/* STALL AT is judged on the window's mean, in whole mA: a mean 1 mA under
 * STALL AT and one at it are no stall, one 1 mA over is, whatever the
 * window's highest sample.  Negative means count by their magnitude. */
TEST_CASE(stall_at_is_judged_on_the_windows_mean)
{
    static const struct {
        int  ma;
        bool stall;
    } k_case[] = {
        { 999, false }, { 1000, false }, { 1001, true },
        { -999, false }, { -1000, false }, { -1001, true },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        servo_test_cfg_t c;
        h_cfg(&c, true);
        h_idle_by_hand(5000u, &c);
        /* The highest sample of this window is 50 mA above its mean: over
         * STALL AT in every case, and no part of the rule. */
        h_win_at(h.now + 50u, k_case[k].ma);
        CHECK_EQ(h.t.stalled, k_case[k].stall);
        CHECK_EQ(h.t.stalling, k_case[k].stall);
        CHECK_NEAR(h.t.stall_peak_a, (float)k_case[k].ma / 1000.0f, 1e-6f);
        CHECK(servo_test_running(&h.t));
        /* One window at or under it between two over it starts the second
         * over again. */
        h_win_at(h.now + 100u, 1000);
        CHECK(!h.t.stalling);
    }
    CHECK(!servo_test_over_a(1.0f, 1.0f));
    CHECK(servo_test_over_a(1.001f, 1.0f));
    CHECK(servo_test_over_a(-1.001f, 1.0f));
    CHECK(!servo_test_over_a(-1.0f, 1.0f));
    CHECK(!servo_test_over_a(NAN, 1.0f));
    CHECK(servo_test_over_a(INFINITY, 1.0f));
    CHECK(servo_test_over_a(-INFINITY, 1.0f));
    CHECK(servo_test_over_a(2.0e6f, 1.0f));
    CHECK(!servo_test_over_a(1.0f, INFINITY));
}

/* Windows over STALL AT end the run 1000 ms after the first one began: a
 * window taken 999 ms after that leaves it running, one at 1000 and one at
 * 1001 ms end it, and at 50 ms a window the twentieth in a row does.  At
 * tick 5000 and with the second across the wrap. */
TEST_CASE(a_second_over_stall_at_ends_the_run_on_the_windows)
{
    static const uint32_t k_last[] = { 999u, 1000u, 1001u };
    servo_test_cfg_t c;
    h_cfg(&c, true);
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.now - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            /* The first window over it is taken at began + 50: it began at
             * began.  Nineteen in a row leave the run running. */
            const uint32_t began = h.now;
            for (uint32_t n = 1u; n <= 19u; ++n) {
                const uint32_t at = began + n * SERVO_TEST_WIN_MS;
                h_win_at(at, 1001);
                if (n % 2u == 0u) {
                    h_pd_at(at);
                }
                h_step_at(at);
            }
            CHECK(servo_test_running(&h.t));
            CHECK(h.t.stalled);
            CHECK_EQ(h.t.stall_since_ms, began);
            if (i > 0u) {
                CHECK(began > 0xFFFFFC00u);
            }
            /* The twentieth. */
            const uint32_t at = began + k_last[k];
            h_win_at(at, 1001);
            h.now = at;
            if (k_last[k] < SERVO_TEST_STALL_ABORT_MS) {
                CHECK(servo_test_running(&h.t));
                h_win_at(at + 1u, 1001);
                CHECK(!servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, at);
            h_check_ended(SERVO_TEST_AB_STALL);
            CHECK(strstr(h.out.report, "Stall threshold  highest 1.001 A, "
                                       "STALL AT 1.00 A: FAIL\n") != NULL);
        }
    }
}

/* On the PD mini the second counts from the first reading over STALL AT:
 * a reading 999 ms after it leaves the run running, one at 1000 and one at
 * 1001 ms end it.  At tick 5000 and across the wrap. */
TEST_CASE(a_second_over_stall_at_ends_the_run_on_the_pd_mini)
{
    static const uint32_t k_last[] = { 999u, 1000u, 1001u };
    servo_test_cfg_t c;
    h_cfg(&c, false);
    h_idle_by_hand(5000u, &c);
    const uint32_t rel = h.now - 5000u;
    const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
    for (size_t i = 0u; i < 2u; ++i) {
        for (size_t k = 0u; k < 3u; ++k) {
            h_idle_by_hand(k_tick0[i], &c);
            h.pd_a = 1.001f;
            const uint32_t first = h.now + 10u;
            h_pd_at(first);
            for (uint32_t n = 1u; n <= 9u; ++n) {
                h_pd_at(first + n * 100u);
                h_step_at(first + n * 100u);
            }
            CHECK(servo_test_running(&h.t));
            CHECK(h.t.stalled);
            const uint32_t at = first + k_last[k];
            h_pd_at(at);
            h.now = at;
            if (k_last[k] < SERVO_TEST_STALL_ABORT_MS) {
                CHECK(servo_test_running(&h.t));
                continue;
            }
            CHECK_EQ(h.t.end_ms, at);
            h_check_ended(SERVO_TEST_AB_STALL);
        }
    }
    /* A reading at STALL AT is none over it, and one under it between two
     * over it starts the second again. */
    h_idle_by_hand(5000u, &c);
    h.pd_a = 1.0f;
    h_pd_at(h.now + 10u);
    CHECK(!h.t.stalled);
    CHECK(!h.t.stalling);
    h.pd_a = 1.2f;
    h_pd_at(h.now + 110u);
    h.pd_a = 0.9f;
    h_pd_at(h.now + 610u);
    h.pd_a = 1.2f;
    h_pd_at(h.now + 1110u);
    h_pd_at(h.now + 1210u);
    CHECK(servo_test_running(&h.t));
    CHECK(h.t.stalled);
}

/* ------------------------------------------------- constant current */

/* The supply in constant current for 1000 ms ends a run, whichever meter
 * it reads and whatever STALL AT is: a reading 999 ms after the first in
 * constant current leaves it running, one at 1000 ms ends it.  At tick
 * 5000 and across the wrap. */
TEST_CASE(constant_current_for_a_second_ends_the_run)
{
    static const uint32_t k_last[] = { 999u, 1000u };
    for (unsigned ina = 0u; ina < 2u; ++ina) {
        servo_test_cfg_t c;
        h_cfg(&c, ina != 0u);
        c.stall_a = 5.0f;           /* over every reading */
        h_idle_by_hand(5000u, &c);
        const uint32_t rel = h.now - 5000u;
        const uint32_t k_tick0[] = { 5000u, wrap_at(rel) };
        for (size_t i = 0u; i < 2u; ++i) {
            for (size_t k = 0u; k < 2u; ++k) {
                h_idle_by_hand(k_tick0[i], &c);
                h.cc = true;
                const uint32_t first = h.now + 10u;
                for (uint32_t ms = 0u; ms <= 900u; ms += 50u) {
                    if (ina != 0u) {
                        h_win_at(first + ms, 20);
                    }
                    if (ms % 100u == 0u) {
                        h_pd_at(first + ms);
                    }
                    h_step_at(first + ms);
                }
                CHECK(servo_test_running(&h.t));
                CHECK_EQ(h.t.cc_readings, 10u);
                CHECK_EQ(h.t.cc_longest_ms, 900u);
                if (ina != 0u) {
                    h_win_at(first + 950u, 20);
                }
                const uint32_t at = first + k_last[k];
                h_pd_at(at);
                h.now = at;
                if (k_last[k] < SERVO_TEST_CC_ABORT_MS) {
                    CHECK(servo_test_running(&h.t));
                    CHECK_EQ(h.t.cc_longest_ms, 999u);
                    continue;
                }
                CHECK_EQ(h.t.end_ms, at);
                CHECK(!h.t.stalled);
                h_check_ended(SERVO_TEST_AB_CC);
                CHECK(strstr(h.out.report,
                             "Result:         ABORTED - constant current "
                             "for 1 s\n") != NULL);
                CHECK(strstr(h.out.report, "Const. current: 11 supply "
                                           "readings, longest stretch 1000 "
                                           "ms\n") != NULL);
            }
        }
    }
}

/* A spell of constant current shorter than a second -- an inrush, as the
 * PD mini reports for 0.43 s on a healthy move -- ends nothing and fails
 * nothing: the report counts its readings and gives the longest spell. */
TEST_CASE(a_spell_of_constant_current_is_counted_and_fails_nothing)
{
    for (unsigned ina = 0u; ina < 2u; ++ina) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, ina != 0u);
        /* The PD mini's own current shows its moves on the PD mini's run. */
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_MOVE));
        h.cc = true;
        h_run_ms(430u);
        h.cc = false;
        h_run_ms(300u);
        h.cc = true;
        h_run_ms(120u);
        h.cc = false;
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        CHECK(servo_test_verdict(&h.t) != SERVO_TEST_FAIL);
        CHECK(!h.t.stalled);
        CHECK(h.t.cc_readings >= 5u && h.t.cc_readings <= 7u);
        CHECK(h.t.cc_longest_ms >= 312u && h.t.cc_longest_ms <= 416u);
        char want[96];
        snprintf(want, sizeof(want), "Const. current: %lu supply readings, "
                 "longest stretch %lu ms\n", (unsigned long)h.t.cc_readings,
                 (unsigned long)h.t.cc_longest_ms);
        CHECK(strstr(h.out.report, want) != NULL);
        if (ina != 0u) {
            /* The rows of a run on the INA3221 carry the supply's mode. */
            CHECK(rows_with(h.out.csv, COL_MODE, "CC") >= 6u);
        } else {
            CHECK_EQ(rows_with(h.out.csv, COL_MODE, "CC"), h.t.cc_readings);
        }
    }
}

/* STALL AT against the current limit and the meter's range, in whole mA:
 * at or above the limit no reading passes it, because the supply holds the
 * current; at or above the range none does, because the part reads no
 * more.  The run starts all the same, and its report says so. */
TEST_CASE(a_stall_at_no_reading_can_pass_is_said_and_not_refused)
{
    static const struct {
        float stall, limit;
        bool  ina;
        bool  by_limit, by_range;
    } k_case[] = {
        { 1.99f, 2.00f, false, false, false },
        { 2.00f, 2.00f, false, true,  false },
        { 2.01f, 2.00f, false, true,  false },
        { 3.00f, 2.00f, false, true,  false },
        { 1.63f, 3.00f, true,  false, false },
        { 1.64f, 3.00f, true,  false, true  },
        { 2.00f, 3.00f, true,  false, true  },
        { 2.00f, 2.00f, true,  true,  true  },
        { 1.00f, 2.00f, true,  false, false },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, k_case[k].ina);
        c.stall_a = k_case[k].stall;
        c.i_limit = k_case[k].limit;
        bool by_limit = !k_case[k].by_limit, by_range = !k_case[k].by_range;
        CHECK_EQ(servo_test_stall_unreachable(&c, &by_limit, &by_range),
                 k_case[k].by_limit || k_case[k].by_range);
        CHECK_EQ(by_limit, k_case[k].by_limit);
        CHECK_EQ(by_range, k_case[k].by_range);
        h_start(&c);
        h_out();
        CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
        char want[96];
        snprintf(want, sizeof(want), "STALL AT %.2f A cannot be reached: "
                 "current limit %.2f A\n", (double)k_case[k].stall,
                 (double)k_case[k].limit);
        CHECK_EQ(strstr(h.out.report, want) != NULL, k_case[k].by_limit);
        snprintf(want, sizeof(want), "STALL AT %.2f A cannot be reached: "
                 "INA3221 range 1.638 A\n", (double)k_case[k].stall);
        CHECK_EQ(strstr(h.out.report, want) != NULL, k_case[k].by_range);
        CHECK_EQ(strstr(h.out.report, "cannot be reached") != NULL,
                 k_case[k].by_limit || k_case[k].by_range);
    }
    CHECK(!servo_test_stall_unreachable(NULL, NULL, NULL));

    /* The line the SERVO screen shows under START TEST: the limit where
     * that is a reason, else the range, and nothing while STALL AT can be
     * reached. */
    servo_test_cfg_t c;
    char note[96] = "x";
    h_cfg(&c, true);
    CHECK(!servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "");
    c.stall_a = 1.65f;
    CHECK(servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "STALL AT 1.65 A cannot be reached: INA3221 range "
                       "1.638 A");
    c.stall_a = 2.0f;
    CHECK(servo_test_stall_note(&c, note, sizeof(note)));
    CHECK_STR_EQ(note, "STALL AT 2.00 A cannot be reached: current limit "
                       "2.00 A");
    CHECK(!servo_test_stall_note(&c, NULL, 0u));
    CHECK(!servo_test_stall_note(&c, note, 0u));
}

/* A servo on a stop against a supply that holds its limit, on the
 * INA3221: the windows read the limit, 2.000 A on a 0.05 Ohm shunt.  One
 * step under the limit STALL AT ends the run; at the limit and above it
 * the constant-current rule does, a second after the supply reports it. */
TEST_CASE(a_servo_held_at_the_limit_ends_the_run_on_either_rule)
{
    static const struct {
        float              stall;
        servo_test_abort_t why;
    } k_case[] = {
        { 1.99f, SERVO_TEST_AB_STALL },
        { 2.00f, SERVO_TEST_AB_CC },
        { 2.01f, SERVO_TEST_AB_CC },
        { 3.00f, SERVO_TEST_AB_CC },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, true);
        servo_test_meter_ina3221(&c.meter, 500u);
        c.stall_a = k_case[k].stall;
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        const uint32_t on_stop = h.now;
        h.force_ma = 2000;          /* the limit, and not 1 mA over it */
        h.cc       = true;
        h.pd_a     = 2.0f;
        for (uint32_t ms = 0u; ms < 1200u && servo_test_running(&h.t);
             ++ms) {
            h_ms();
        }
        /* 20 windows from the one that began at the stop; ten readings
         * of the supply, 104 ms apart, after its first there. */
        CHECK(h.t.end_ms - on_stop >= 950u);
        CHECK(h.t.end_ms - on_stop <= 1150u);
        CHECK_EQ(h.t.stalled, k_case[k].why == SERVO_TEST_AB_STALL);
        h_check_ended(k_case[k].why);
    }
}

/* -------------------------------------------------------- every ending */

/* Every way a run on the INA3221 ends, each from a hold at an end with
 * the output on: the output asked off once, the servo let go once, the
 * report ABORTED with the reason, and nothing after it changing either. */
TEST_CASE(every_ending_of_a_run_on_the_ina3221_switches_off_and_is_marked)
{
    /* The caller's: STOP, the link, the screen left, the operator, the
     * settings changed, touch events lost. */
    static const servo_test_abort_t k_callers[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_LEFT,
        SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS, SERVO_TEST_AB_TOUCH,
    };
    servo_test_cfg_t c;
    h_cfg(&c, true);
    for (size_t k = 0u; k < sizeof(k_callers) / sizeof(k_callers[0]); ++k) {
        h_fresh(5000u);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        CHECK(h.output);
        servo_test_abort(&h.t, k_callers[k], h.now);
        h_check_ended(k_callers[k]);
    }

    /* A disarm. */
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.armed = false;
    h_ms();
    h.armed = true;
    h_check_ended(SERVO_TEST_AB_DISARMED);

    /* The supply: not answering, tripped, its output off. */
    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.online = false;
    h_pd_at(h.now);
    h.online = true;
    h_check_ended(SERVO_TEST_AB_SUPPLY_LOST);

    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.output = false;
    h.trip   = 1u;
    h_pd_at(h.now);
    h.trip = 0u;
    h_check_ended(SERVO_TEST_AB_TRIPPED);

    h_fresh(5000u);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.output = false;
    h_pd_at(h.now);
    h_check_ended(SERVO_TEST_AB_SUPPLY_OFF);

    /* A set point not read back, and an output that does not come on: the
     * windows arrive throughout, and the 3000 ms are the supply's. */
    h_fresh(5000u);
    h.take_set = false;
    h.set_v    = 9.0f;
    h_start(&c);
    h_run_ms(3000u);
    CHECK(servo_test_running(&h.t));
    h_ms();
    h_check_ended(SERVO_TEST_AB_SET_NOT_TAKEN);

    h_fresh(5000u);
    h.take_on = false;
    h_start(&c);
    h_run_ms(3001u);
    /* Nothing was switched on, and the off is asked all the same. */
    h_check_ended(SERVO_TEST_AB_NOT_ON);

    /* The cap comes down under the second step. */
    h_fresh(5000u);
    c.steps_v[1] = 6.0f;
    c.step_count = 2u;
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.v_max = 5.5f;
    for (uint32_t k = 0u; k < 20000u && servo_test_running(&h.t); ++k) {
        h_ms();
    }
    CHECK(h.set_v_max <= 4.8f + 0.001f);
    h_check_ended(SERVO_TEST_AB_CAP);
    c.step_count = 1u;

    /* The meter, as servo_test_meter_now() is told: each reason, a change
     * whose reason is not known, and a change count that moved under an
     * INA3221 that is the meter again. */
    static const struct {
        servo_test_meter_now_t now;
        servo_test_abort_t     why;
    } k_meter[] = {
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 3u },
          SERVO_TEST_AB_INA_RESET },
        { { false, 3u, SERVO_TEST_AB_INA_SILENT, 3u },
          SERVO_TEST_AB_INA_SILENT },
        { { false, 3u, SERVO_TEST_AB_INA_NO_WINDOW, 3u },
          SERVO_TEST_AB_INA_NO_WINDOW },
        { { false, 3u, SERVO_TEST_AB_INA_SETUP, 3u },
          SERVO_TEST_AB_INA_SETUP },
        { { false, 3u, SERVO_TEST_AB_LINK, 3u }, SERVO_TEST_AB_LINK },
        /* No drop told, and one from before the run's start. */
        { { false, 3u, SERVO_TEST_AB_NONE, 0u }, SERVO_TEST_AB_INA_METER },
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 1u },
          SERVO_TEST_AB_INA_METER },
        { { false, 3u, SERVO_TEST_AB_INA_RESET, 2u },
          SERVO_TEST_AB_INA_METER },
        /* Gone and back between two answers: the INA3221 again, two
         * changes on. */
        { { true, 4u, SERVO_TEST_AB_INA_SILENT, 3u },
          SERVO_TEST_AB_INA_SILENT },
    };
    for (size_t k = 0u; k < sizeof(k_meter) / sizeof(k_meter[0]); ++k) {
        h_fresh(5000u);
        h_start(&c);
        CHECK(h_until(SERVO_TEST_PH_HOLD));
        /* The meter as it was at the start, with a drop from before it:
         * the run goes on. */
        const servo_test_meter_now_t same = {
            .ina3221 = true, .changes = 2u,
            .dropped = SERVO_TEST_AB_INA_RESET, .dropped_at = 1u,
        };
        servo_test_meter_now(&h.t, &same, h.now);
        CHECK(servo_test_running(&h.t));
        servo_test_meter_now(&h.t, &k_meter[k].now, h.now);
        CHECK_EQ(h.t.end_ms, h.now);
        h_check_ended(k_meter[k].why);
        CHECK_EQ(rows_with(h.out.csv, COL_METER, "PDMINI"), 0u);
    }
    /* The change count across 2^32. */
    h_fresh(5000u);
    c.meter_changes = 0xFFFFFFFFu;
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    const servo_test_meter_now_t wrapped = {
        .ina3221 = false, .changes = 0u,
        .dropped = SERVO_TEST_AB_INA_RESET, .dropped_at = 0u,
    };
    servo_test_meter_now(&h.t, &wrapped, h.now);
    h_check_ended(SERVO_TEST_AB_INA_RESET);
}

/* Facts under which the INA3221 is the meter, read at @p now
 * (servo_source.h). */
static sense_link_meter_t facts_good(uint32_t now)
{
    const sense_link_meter_t m = {
        .page = true, .wanted = true, .held = true, .setups = 3u,
        .status = true, .status_ms = now,
        .flags = (uint16_t)(LINK_SN_I3221_ONLINE | LINK_SN_I3221_ID_OK
                            | LINK_SN_BUS_OPEN),
        .resets_read = true, .resets = 0u,
        .win = true, .win_valid = true, .win_ms = now,
    };
    return m;
}

/* Each condition of servo_source.h failing under a run on the INA3221, by
 * the meter the real servo_source names from facts written by hand: the
 * run ends in the poll that drops the meter, with that condition's
 * reason.  A reset shows as a condition for one poll only: told to the run
 * three polls later, when the meter is the PD mini for another reason, it
 * is still the reset that ended the run. */
TEST_CASE(each_condition_that_drops_the_meter_ends_the_run)
{
    static const struct {
        servo_source_why_t why;
        servo_test_abort_t ab;
        const char        *text;
    } k_case[] = {
        { SERVO_SOURCE_WHY_OFF, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_OLD, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_NOT_HELD, SERVO_TEST_AB_INA_SETUP,
          "INA3221 set-up not held" },
        { SERVO_SOURCE_WHY_SILENT, SERVO_TEST_AB_INA_SILENT,
          "INA3221 not answering" },
        { SERVO_SOURCE_WHY_NO_WINDOW, SERVO_TEST_AB_INA_NO_WINDOW,
          "INA3221 window without current" },
        { SERVO_SOURCE_WHY_RESET, SERVO_TEST_AB_INA_RESET,
          "INA3221 reset itself" },
        { SERVO_SOURCE_WHY_NO_LINK, SERVO_TEST_AB_LINK, "link lost" },
    };
    for (size_t k = 0u; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        for (unsigned late = 0u; late < 2u; ++late) {
            servo_source_t src;
            servo_source_init(&src);
            /* 1050 ms of good polls: the INA3221 is the meter. */
            uint32_t at = 3900u;
            for (; at <= 4950u; at += 50u) {
                const sense_link_meter_t m = facts_good(at);
                (void)servo_source_step(&src, at, true, 11u, &m);
            }
            CHECK_EQ(servo_source_id(&src), SERVO_SOURCE_INA3221);
            h_fresh(5000u);
            servo_test_cfg_t c;
            h_cfg(&c, true);
            c.meter_changes = servo_source_changes(&src);
            h_start(&c);
            CHECK(h_until(SERVO_TEST_PH_HOLD));
            /* Good polls under the run change nothing. */
            sense_link_meter_t m = facts_good(h.now);
            (void)servo_source_step(&src, h.now, true, 11u, &m);
            servo_test_meter_now_t now = meter_of(&src);
            servo_test_meter_now(&h.t, &now, h.now);
            CHECK(servo_test_running(&h.t));

            /* The poll in which the condition fails. */
            h_run_ms(50u);
            m = facts_good(h.now);
            uint16_t minor = 11u;
            bool up = true;
            switch (k_case[k].why) {
            case SERVO_SOURCE_WHY_OFF:       m.wanted = false; break;
            case SERVO_SOURCE_WHY_OLD:       minor = 10u; break;
            case SERVO_SOURCE_WHY_NOT_HELD:  m.held = false; break;
            case SERVO_SOURCE_WHY_SILENT:    m.flags = LINK_SN_BUS_OPEN;
                                             break;
            case SERVO_SOURCE_WHY_NO_WINDOW: m.win_valid = false; break;
            case SERVO_SOURCE_WHY_RESET:     m.resets = 1u; break;
            default:                         up = false; break;
            }
            CHECK_EQ(servo_source_step(&src, h.now, up, minor, &m),
                     up ? SERVO_SOURCE_PDMINI : SERVO_SOURCE_MODEL);
            CHECK_EQ(servo_source_why(&src), k_case[k].why);
            uint32_t drop_at = 0u;
            CHECK_EQ(servo_source_dropped(&src, &drop_at), k_case[k].why);
            CHECK_EQ(drop_at, servo_source_changes(&src));
            if (late != 0u) {
                /* Three more polls, every condition holding again, before
                 * the run hears of it: the meter is the PD mini while the
                 * INA3221 settles. */
                for (unsigned n = 0u; n < 3u; ++n) {
                    h_run_ms(50u);
                    m = facts_good(h.now);
                    m.resets = (k_case[k].why == SERVO_SOURCE_WHY_RESET)
                                   ? 1u : 0u;
                    (void)servo_source_step(&src, h.now, true, 11u, &m);
                }
                CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SETTLING);
                CHECK_EQ(servo_source_dropped(&src, NULL), k_case[k].why);
                CHECK(servo_test_running(&h.t));
            }
            now = meter_of(&src);
            servo_test_meter_now(&h.t, &now, h.now);
            h_check_ended(k_case[k].ab);
            char want[96];
            snprintf(want, sizeof(want), "Result:         ABORTED - %s\n",
                     k_case[k].text);
            CHECK(strstr(h.out.report, want) != NULL);
        }
    }
    /* A reset found with the part already taken offline, in one read: the
     * first condition that fails is that the part is not online, and the
     * reason kept for the drop is the reset. */
    servo_source_t src;
    servo_source_init(&src);
    for (uint32_t t = 3900u; t <= 4950u; t += 50u) {
        const sense_link_meter_t m = facts_good(t);
        (void)servo_source_step(&src, t, true, 11u, &m);
    }
    sense_link_meter_t off = facts_good(5000u);
    off.flags  = LINK_SN_BUS_OPEN;
    off.resets = 1u;
    CHECK_EQ(servo_source_step(&src, 5000u, true, 11u, &off),
             SERVO_SOURCE_PDMINI);
    CHECK_EQ(servo_source_why(&src), SERVO_SOURCE_WHY_SILENT);
    CHECK_EQ(servo_source_dropped(&src, NULL), SERVO_SOURCE_WHY_RESET);
    /* The count moving later, under the PD mini, is no drop. */
    off.resets = 2u;
    (void)servo_source_step(&src, 5050u, true, 11u, &off);
    uint32_t at = 0u;
    CHECK_EQ(servo_source_dropped(&src, &at), SERVO_SOURCE_WHY_RESET);
    CHECK_EQ(at, 3u);
    CHECK_EQ(servo_source_changes(&src), 3u);

    servo_source_init(&src);
    at = 7u;
    CHECK_EQ(servo_source_dropped(&src, &at), SERVO_SOURCE_WHY_NONE);
    CHECK_EQ(at, 0u);
    CHECK_EQ(servo_source_dropped(NULL, &at), SERVO_SOURCE_WHY_NONE);
}

/* ----------------------------------------------------- signed, clipped */

/* A shunt fitted the other way round: every current negative.  The CSV
 * keeps the sign, the limits and STALL AT compare the magnitude, and the
 * report says the direction. */
TEST_CASE(a_current_negative_at_rest_is_reported_and_judged_by_magnitude)
{
    h_fresh(5000u);
    h.rest_ma = -120;
    h.move_ma = -600;
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.idle_max_a = 0.10f;           /* under 0.120 A: FAIL by magnitude */
    c.hold_max_a = 0.20f;           /* over it: PASS */
    h_start(&c);
    h_out();
    CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
    CHECK(servo_test_negative_at_rest(&h.t));
    CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_FAIL);
    const servo_test_step_t *s = &h.t.steps[0];
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, -0.120f, 0.0005f);
    /* The sample furthest from zero: the window's lowest, 5 mA under. */
    CHECK_NEAR(s->peak_a, -0.605f, 0.0005f);
    CHECK_NEAR(h.t.stall_peak_a, -0.600f, 0.0005f);
    CHECK(!h.t.stalled);
    float a = 0.0f;
    CHECK(servo_test_max_idle(&h.t, &a));
    CHECK_NEAR(a, -0.120f, 0.0005f);
    CHECK(servo_test_max_hold(&h.t, &a));
    CHECK_NEAR(a, -0.120f, 0.0005f);
    CHECK(rows_with(h.out.csv, COL_CURRENT, "-0.120") > 30u);
    CHECK(rows_with(h.out.csv, COL_I_MIN, "-0.605") >= 4u);
    const char *r = h.out.report;
    CHECK(strstr(r, "Current reads negative at rest: shunt direction\n")
          != NULL);
    CHECK(strstr(r, "Idle current     highest -0.120 A, limit 0.10 A: "
                    "FAIL\n") != NULL);
    CHECK(strstr(r, "Holding current  highest -0.120 A, limit 0.20 A: "
                    "PASS\n") != NULL);
    CHECK(strstr(r, "Stall threshold  highest -0.600 A, STALL AT 1.00 A: "
                    "PASS\n") != NULL);

    /* -0.020 A at rest is not said, -0.021 A is; and a stall the other
     * way round ends the run. */
    for (int ma = -21; ma <= -20; ++ma) {
        h_fresh(5000u);
        h.rest_ma = ma;
        h_cfg(&c, true);
        h_start(&c);
        h_out();
        CHECK_EQ(servo_test_negative_at_rest(&h.t), ma == -21);
        CHECK_EQ(strstr(h.out.report, "Current reads negative at rest")
                 != NULL, ma == -21);
    }
    CHECK(!servo_test_negative_at_rest(NULL));
    h_fresh(5000u);
    h_cfg(&c, true);
    h_start(&c);
    CHECK(h_until(SERVO_TEST_PH_HOLD));
    h.force_ma = -1200;
    h_run_ms(1100u);
    h_check_ended(SERVO_TEST_AB_STALL);
}

/* A clipped window is a reading like any other: its mean, its highest
 * sample and its place in the verdict are the values the part gave.  The
 * file carries the count of clipped samples and the report the count of
 * such windows.  How many of a window's samples are clipped decides
 * nothing: 44, 45 and all 50 read alike. */
TEST_CASE(a_clipped_window_is_the_value_it_is_and_is_counted)
{
    static const unsigned k_clipped[] = { 1u, 44u, 45u, 50u };
    for (size_t k = 0u; k < sizeof(k_clipped) / sizeof(k_clipped[0]); ++k) {
        for (int over = 0; over <= 1; ++over) {
            h_fresh(5000u);
            servo_test_cfg_t c;
            h_cfg(&c, true);
            /* The windows' mean while clipped is 1.588 A: over a STALL
             * AT of 1.58 A and under one of 1.59 A. */
            c.stall_a = over ? 1.58f : 1.59f;
            h_start(&c);
            CHECK(h_until(SERVO_TEST_PH_HOLD));
            h.clipped  = k_clipped[k];
            h.force_ma = 1588;      /* its highest sample 1.638 A */
            h_run_ms(400u);
            const uint32_t clipped = h.t.clipped;
            CHECK(clipped >= 7u && clipped <= 9u);
            CHECK_EQ(h.t.stalled, over != 0);
            h.clipped  = 0u;
            h.force_ma = INT16_MIN;
            h_out();
            CHECK_EQ(h.t.why, SERVO_TEST_AB_NONE);
            CHECK_EQ(h.t.stalled, over != 0);
            if (over != 0) {
                CHECK_EQ(servo_test_verdict(&h.t), SERVO_TEST_FAIL);
            }
            CHECK_EQ(h.t.clipped, clipped);
            char cell[8], want[128];
            snprintf(cell, sizeof(cell), "%u", k_clipped[k]);
            CHECK_EQ(rows_with(h.out.csv, COL_CLIPPED, cell), clipped);
            CHECK_EQ(rows_with(h.out.csv, COL_I_MAX, "1.638"), clipped);
            CHECK_EQ(rows_with(h.out.csv, COL_CURRENT, "1.588"), clipped);
            snprintf(want, sizeof(want), "Clipped:        %lu windows hold "
                     "a sample at an end of the range, 1.638 A",
                     (unsigned long)clipped);
            CHECK(strstr(h.out.report, want) != NULL);
            CHECK(strstr(h.out.report, "Stall threshold  highest 1.588 A")
                  != NULL);
        }
    }
}

/* --------------------------------------------------------- the file */

/* The run's CSV read by the log viewer's parser: 19 columns on either
 * meter and 21 with the encoder's, none ragged, the time column found and
 * rising.  Files of the format before the meter's columns, 13 wide, read
 * as they did. */
TEST_CASE(the_csv_reads_back_in_the_viewer_old_and_new)
{
    for (unsigned variant = 0u; variant < 3u; ++variant) {
        h_fresh(5000u);
        servo_test_cfg_t c;
        h_cfg(&c, variant != 1u);
        c.enc_on = variant == 2u;
        h_start(&c);
        h_out();
        const char *head = (variant == 2u) ? servo_test_csv_header_enc()
                                           : servo_test_csv_header();
        CHECK(strlen(head) < SERVO_TEST_LINE_MAX);
        CHECK(strncmp(h.out.csv, head, strlen(head)) == 0);
        CHECK_EQ(h.out.csv[strlen(head)], '\n');
        log_source_t src;
        log_mem_ctx_t ctx;
        log_source_memory(&src, &ctx, h.out.csv, h.out.csv_len);
        log_csv_opts_t opts;
        log_csv_opts_default(&opts);
        log_analysis_t an;
        CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
        const int enc = (variant == 2u) ? 2 : 0;
        CHECK_EQ(an.n_columns, (int)COLS + enc);
        CHECK_EQ(an.delimiter, ';');
        CHECK_EQ(an.ragged_rows, 0);
        CHECK_EQ(an.time_index, 0);
        CHECK_EQ(an.row_count, (int)h.out.rows - 1);
        CHECK(an.columns[COL_TIME].monotonic);
        CHECK(an.columns[COL_CURRENT].numeric);
        CHECK(an.columns[COL_VOLTAGE].numeric);
        CHECK_STR_EQ(an.columns[COL_METER + enc].name, "meter");
        CHECK_STR_EQ(an.columns[COL_V_MIN + enc].name, "voltage min");
        CHECK_STR_EQ(an.columns[COL_V_MIN + enc].unit, "V");
        if (variant != 1u) {
            CHECK(an.columns[COL_WINDOW + enc].numeric);
            CHECK(an.columns[COL_I_MAX + enc].numeric);
            CHECK(an.columns[COL_I_MIN + enc].numeric);
            CHECK(an.columns[COL_V_MIN + enc].numeric);
            CHECK(an.columns[COL_CLIPPED + enc].numeric);
        }
    }
    /* The 13 columns come first, in the order they had. */
    CHECK_STR_EQ(servo_test_csv_header(),
                 "time (s);test;step;phase;command (us);position (us);"
                 "set (V);voltage (V);limit (A);current (A);power (W);"
                 "mode;travel (ms);meter;window;current max (A);"
                 "current min (A);voltage min (V);clipped");
    CHECK_EQ(strlen(servo_test_csv_header_enc()), 219u);
    CHECK(strstr(servo_test_csv_header_enc(),
                 ";travel (ms);angle (deg);travel angle (ms);meter;window;")
          != NULL);

    static const struct {
        const char *name;
        int         rows;
    } k_old[] = {
        { "servo-mg90s.csv", 870 },
        { "servo-1102hb.csv", 869 },
        { "servo-ms24-pdmini.csv", 1734 },
    };
    for (size_t k = 0u; k < sizeof(k_old) / sizeof(k_old[0]); ++k) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, k_old[k].name);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL);
        if (f == NULL) {
            continue;
        }
        static char text[200000];
        const size_t n = fread(text, 1u, sizeof(text) - 1u, f);
        fclose(f);
        text[n] = '\0';
        log_source_t src;
        log_mem_ctx_t ctx;
        log_source_memory(&src, &ctx, text, n);
        log_csv_opts_t opts;
        log_csv_opts_default(&opts);
        log_analysis_t an;
        CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
        CHECK_EQ(an.n_columns, 13);
        CHECK_EQ(an.ragged_rows, 0);
        CHECK_EQ(an.time_index, 0);
        CHECK_EQ(an.row_count, k_old[k].rows);
        CHECK(an.columns[COL_CURRENT].numeric);
    }
}

/* ---------------------------------------------- a recorded bench log */

/*
 * A bench log recorded on 0.14.0 beside a servo test of an MS24 digital
 * servo (fixtures/servo-ms24-windows.csv): 1310 rows for the 3817 windows
 * the INA3221 closed, since that firmware wrote a row for 35 % of them.
 * Its windows, each at 50 ms a number from the first, are fed to a run on
 * the INA3221 with a supply that answers throughout.
 *
 * The run reads every window the file holds up to its first gap of ten
 * numbers or more, counts the numbers missing between them as skipped,
 * and ends 500 ms after the last window before that gap: a file with its
 * windows missing cannot feed a run.  The 0.14.0 log has no lowest
 * sample, no lowest voltage and no clip count: the mean stands in for the
 * first two.
 */
TEST_CASE(a_recorded_log_with_windows_missing_ends_the_run_at_its_first_gap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/servo-ms24-windows.csv", FIXTURE_DIR);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    static struct {
        unsigned number;
        int      mean_ma, max_ma, mv;
    } rec[1400];
    unsigned n = 0u;
    char line[512];
    CHECK(fgets(line, sizeof(line), f) != NULL);            /* the header */
    CHECK(strstr(line, ";window;ch1 current (A);ch1 max (A);ch1 voltage "
                       "(V);") != NULL);
    while (fgets(line, sizeof(line), f) != NULL && n < 1400u) {
        char cell[4][32];
        bool ok = true;
        for (unsigned k = 0u; k < 4u; ++k) {
            ok = ok && cell_of(line, 12u + k, cell[k], sizeof(cell[k]));
        }
        if (!ok || cell[0][0] == '\0' || cell[1][0] == '\0') {
            continue;                   /* a row without a window */
        }
        rec[n].number  = (unsigned)strtoul(cell[0], NULL, 10);
        rec[n].mean_ma = (int)lround(strtod(cell[1], NULL) * 1000.0);
        rec[n].max_ma  = (int)lround(strtod(cell[2], NULL) * 1000.0);
        rec[n].mv      = (int)lround(strtod(cell[3], NULL) * 1000.0);
        ++n;
    }
    fclose(f);
    CHECK_EQ(n, 1309u);
    CHECK_EQ(rec[0].number, 19750u);
    CHECK_EQ(rec[n - 1u].number, 23566u);

    /* The first gap of ten numbers or more, and what is missing before. */
    unsigned upto = 0u, missing = 0u;
    for (unsigned k = 1u; k < n; ++k) {
        const unsigned step = rec[k].number - rec[k - 1u].number;
        if (step >= 10u) {
            upto = k;
            break;
        }
        missing += step - 1u;
    }
    CHECK(upto > 20u);
    CHECK(missing > 0u);

    h_fresh(5000u);
    h.win_on = false;
    servo_test_cfg_t c;
    h_cfg(&c, true);
    c.stall_a  = 3.0f;              /* as the recorded run's LIMITS page */
    c.by_moves = false;             /* and its 60 s a step */
    c.time_s   = 60u;
    h_start(&c);
    const uint32_t t0 = h.now;
    unsigned next = 0u;
    while (servo_test_running(&h.t) && (uint32_t)(h.now - t0) < 60000u) {
        ++h.now;
        while (next < n
               && (rec[next].number - rec[0].number + 1u) * SERVO_TEST_WIN_MS
                      <= (uint32_t)(h.now - t0)) {
            servo_test_win_t w;
            memset(&w, 0, sizeof(w));
            w.number   = (uint16_t)rec[next].number;
            w.current  = true;
            w.voltage  = true;
            w.mean_ma  = (int16_t)rec[next].mean_ma;
            w.max_ma   = (int16_t)rec[next].max_ma;
            w.min_ma   = (int16_t)rec[next].mean_ma;
            w.mean_mv  = (uint16_t)rec[next].mv;
            w.min_mv   = (uint16_t)rec[next].mv;
            w.taken_ms = h.now;
            servo_test_window(&h.t, &w, 0u);
            ++next;
        }
        if ((int32_t)(h.now - h.pd_next) >= 0) {
            h.pd_next += 104u;
            h_pd_at(h.now);
        }
        h_step_at(h.now);
    }
    const uint32_t last = t0 + (rec[upto - 1u].number - rec[0].number + 1u)
                                   * SERVO_TEST_WIN_MS;
    CHECK_EQ(h.t.end_ms, last + SERVO_TEST_WIN_STALE_MS);
    CHECK_EQ(h.t.readings, upto);
    CHECK_EQ(h.t.skipped, missing);
    CHECK_EQ(h.t.samples, (uint16_t)rec[upto - 1u].number);
    CHECK_EQ(next, upto);
    h.win_on = true;
    h.win_next = h.now;
    h.win_number = (uint16_t)rec[n - 1u].number;
    h_check_ended(SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(h.out.rows - 1u, upto);
    CHECK_EQ(rows_with(h.out.csv, COL_METER, "INA3221"), upto);
    /* The first row is the file's first window. */
    char want[96];
    snprintf(want, sizeof(want), ";INA3221;19750;%.3f;%.3f;%.3f;0\n",
             (double)rec[0].max_ma / 1000.0, (double)rec[0].mean_ma / 1000.0,
             (double)rec[0].mv / 1000.0);
    CHECK(strstr(h.out.csv, want) != NULL);
    snprintf(want, sizeof(want), "Skipped:        %u windows the INA3221 "
             "closed never reached the test\n", missing);
    CHECK(strstr(h.out.report, want) != NULL);
}

/* ============================================================== the bench */

#define PASS_MS   5u            /* the control task's pass                */
#define FRAME_MS  20u           /* the render loop's frame                */
#define PD_MS     104u          /* the PD mini's reading                  */
#define SOURCE_OHM 0.2          /* the supply and its leads               */
#define Q_LEN     64u

typedef struct {
    bool                   is_win;
    servo_test_win_t       win;
    servo_test_reading_t   pd;
    servo_test_meter_now_t meter;
} item_t;

typedef struct {
    servo_test_t   t;
    out_t          out;
    servo_source_t src;
    servo_sim_t    servo;
    bool     link_up;
    bool     armed;
    /* The PD mini. */
    bool     output;
    float    set_v, set_i;
    bool     limits;        /* it holds its current limit                */
    bool     in_cc;
    uint16_t pd_samples;
    uint32_t pd_next;
    float    pd_gain;       /* its reading, of the rail's current        */
    double   sign;          /* the shunt's direction: 1 or -1            */
    uint16_t cmd;
    double   amps;          /* through the shunt now                     */
    /* From the control task to the render loop. */
    item_t   q[Q_LEN];
    unsigned q_n;
    unsigned q_lost;
    unsigned passes;
    unsigned wins_taken;
    unsigned offs, releases;
} bench_t;

static bench_t b;

static uint32_t b_now(void) { return chain_now(); }

/* The rail for one millisecond: the servo, the supply's limit, and what
 * the INA3221 has across its shunt and at its load side. */
static void b_rail_ms(void)
{
    double amps = 0.0;
    if (b.output) {
        amps = (double)servo_sim_step(&b.servo, b.cmd, b_now());
    }
    b.in_cc = false;
    if (b.limits && amps > (double)b.set_i) {
        amps    = (double)b.set_i;
        b.in_cc = true;
    }
    b.amps = amps;
    ch.i3221->amps[0]  = amps * b.sign;
    ch.i3221->volts[0] = b.output ? (double)b.set_v - amps * SOURCE_OHM
                                  : 0.0;
    chain_far(1u);
}

static void b_push(const item_t *it)
{
    if (b.q_n >= Q_LEN) {
        ++b.q_lost;
        memmove(&b.q[0], &b.q[1], (Q_LEN - 1u) * sizeof(b.q[0]));
        --b.q_n;
    }
    b.q[b.q_n++] = *it;
}

/* One pass of the control task: the poll, the meter, the windows it
 * brought and the supply's reading when one is due. */
static void b_pass(void)
{
    const uint64_t began = ch.us;
    if (b.link_up) {
        (void)chain_poll();
    }
    sense_link_meter_t m;
    sense_link_meter(&ch.sl, &m);
    (void)servo_source_step(&b.src, b_now(), b.link_up, ch.minor,
                            b.link_up ? &m : NULL);
    item_t it;
    memset(&it, 0, sizeof(it));
    it.meter = meter_of(&b.src);
    sense_link_win_t w;
    while (b.link_up && sense_link_take_win(&ch.sl, &w)) {
        ++b.wins_taken;
        it.is_win       = true;
        it.win.number   = w.number;
        it.win.current  = w.current;
        it.win.voltage  = w.voltage;
        it.win.clipped  = w.clipped;
        it.win.mean_ma  = w.mean_ma;
        it.win.max_ma   = w.max_ma;
        it.win.min_ma   = w.min_ma;
        it.win.mean_mv  = w.mean_mv;
        it.win.min_mv   = w.min_mv;
        it.win.taken_ms = w.taken_ms;
        b_push(&it);
    }
    if ((int32_t)(b_now() - b.pd_next) >= 0) {
        b.pd_next += PD_MS;
        it.is_win = false;
        memset(&it.pd, 0, sizeof(it.pd));
        it.pd.online   = true;
        it.pd.output   = b.output;
        it.pd.ok       = true;
        it.pd.set_v    = b.set_v;
        it.pd.set_i    = b.set_i;
        it.pd.v        = b.output ? b.set_v : 0.0f;
        it.pd.i        = (float)b.amps * b.pd_gain;
        it.pd.mode     = !b.output ? 0u : b.in_cc ? 2u : 1u;
        it.pd.samples  = ++b.pd_samples;
        it.pd.taken_ms = b_now();
        b_push(&it);
    }
    /* The rest of the pass, a millisecond of the rail at a time. */
    while ((ch.us - began) / 1000u < PASS_MS) {
        b_rail_ms();
    }
    ++b.passes;
}

/* One frame of the render loop: the meter, then what the queue holds in
 * the order it came, each with the meter of its poll, and the run's pass. */
static void b_frame(void)
{
    for (unsigned k = 0u; k < FRAME_MS / PASS_MS; ++k) {
        b_pass();
    }
    const servo_test_meter_now_t snap = meter_of(&b.src);
    servo_test_meter_now(&b.t, &snap, b_now());
    for (unsigned k = 0u; k < b.q_n; ++k) {
        servo_test_meter_now(&b.t, &b.q[k].meter, b_now());
        if (b.q[k].is_win) {
            servo_test_window(&b.t, &b.q[k].win, 0u);
        } else {
            servo_test_reading(&b.t, &b.q[k].pd, 0u);
        }
    }
    b.q_n = 0u;
    const servo_test_in_t in = { b.armed, 20.0f };
    servo_test_do_t d;
    servo_test_step(&b.t, b_now(), &in, &d);
    if (d.set) {
        b.set_v = d.set_v;
        b.set_i = d.set_i;
    }
    if (d.on) {
        b.output = true;
    }
    if (d.off) {
        ++b.offs;
        b.output = false;
    }
    if (d.command) {
        b.cmd = d.cmd_us;
    }
    if (d.release) {
        ++b.releases;
        b.cmd = CENTRE;
    }
    out_drain(&b.out, &b.t);
}

static void b_frames_ms(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k += FRAME_MS) {
        b_frame();
    }
}

/* A bench at tick @p tick0 with a 4.11 coprocessor, the INA3221 on CH1 on
 * 0.1 Ohm, a servo free between its ends and the link up for long enough
 * that the INA3221 is the rail's meter. */
static void b_fresh(uint32_t tick0)
{
    out_reset(&b.out);
    memset(&b, 0, sizeof(b));
    servo_test_init(&b.t);
    chain_start(LINK_MINOR_SERVO_WIN, tick0 - 2000u, 1u);
    ch.flat = true;
    ch.i3221->amps[0] = 0.0;
    servo_source_init(&b.src);
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_init(&b.servo, &sc);
    b.servo.position_us = (float)CENTRE;
    b.armed   = true;
    b.set_v   = 5.0f;
    b.set_i   = 2.0f;
    b.pd_gain = 1.0f;
    b.sign    = 1.0;
    b.cmd     = CENTRE;
    b.link_up = true;
    b.pd_next = b_now();
    chain_link_up();
    b_frames_ms(2000u);
}

/* Two steps and four counted moves each, as test_servo_test's. */
static void b_cfg(servo_test_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->steps_v[0] = 4.8f;
    c->steps_v[1] = 6.0f;
    c->step_count = 2u;
    c->i_limit    = 3.0f;
    c->centre_us  = CENTRE;
    c->end_lo_us  = LO_US;
    c->end_hi_us  = HI_US;
    c->settle_ms  = 500u;
    c->dwell_ms   = 200u;
    c->by_moves   = true;
    c->moves      = 4u;
    c->stall_a    = 1.5f;
    c->report     = true;
    snprintf(c->dut, sizeof(c->dut), "DS3218");
    snprintf(c->type, sizeof(c->type), "STANDARD PWM");
    snprintf(c->firmware, sizeof(c->firmware), "0.0.0");
}

/* Start a run on the meter servo_source names, as the SERVO screen does. */
static void b_start(servo_test_cfg_t *c)
{
    if (servo_source_id(&b.src) == SERVO_SOURCE_INA3221) {
        servo_test_meter_ina3221(&c->meter, 1000u);
        c->meter_changes = servo_source_changes(&b.src);
    } else {
        servo_test_meter_pdmini(&c->meter);
        c->ina_why = (uint8_t)SERVO_TEST_INA_SETTLING;
    }
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    last.output = b.output;
    CHECK_EQ(servo_test_start(&b.t, c, b_now(), &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
}

static void b_out(void)
{
    for (uint32_t k = 0u; k < 120000u && !servo_test_drained(&b.t);
         k += FRAME_MS) {
        b_frame();
    }
    CHECK(servo_test_drained(&b.t));
}

static bool b_until(servo_test_phase_t phase)
{
    for (uint32_t k = 0u; k < 30000u && servo_test_running(&b.t);
         k += FRAME_MS) {
        if (b.t.phase == phase) {
            return true;
        }
        b_frame();
    }
    return false;
}

/* The run on the bench ended as @p why, and two more seconds of the bench
 * change nothing of it. */
static void b_check_ended(servo_test_abort_t why)
{
    CHECK(!servo_test_running(&b.t));
    CHECK_EQ(b.t.why, why);
    b_out();
    CHECK_EQ(servo_test_verdict(&b.t), SERVO_TEST_ABORTED);
    CHECK_EQ(b.offs, 1u);
    CHECK_EQ(b.releases, 1u);
    CHECK(!b.output);
    char mark[96];
    snprintf(mark, sizeof(mark), "Result:         ABORTED - %s\n",
             servo_test_abort_name(why));
    CHECK(strstr(b.out.report, mark) != NULL);
    const unsigned rows = b.out.rows, txt = b.out.txt;
    b_frames_ms(2000u);
    CHECK_EQ(b.t.why, why);
    CHECK_EQ(b.out.rows, rows);
    CHECK_EQ(b.out.txt, txt);
    CHECK_EQ(b.offs, 1u);
    CHECK_EQ(b.releases, 1u);
    /* The run never read the other meter. */
    CHECK_EQ(rows_with(b.out.csv, COL_METER, "PDMINI"), 0u);
    CHECK_EQ(rows_with(b.out.csv, COL_METER, "INA3221"), rows - 1u);
}

/* A full run through the chain: the modelled servo's current at the
 * INA3221, its windows through the schedule, the page and the link, and
 * the run's figures from them.  The PD mini beside it reads a quarter of
 * the current, as a slow meter does of a short peak, and none of that is
 * in the run.  At tick 5000 and with the tick wrapping during the run. */
TEST_CASE(a_full_run_on_the_ina3221_through_the_chain)
{
    static const uint32_t k_tick0[] = { 5000u, 0xFFFFE000u };
    for (size_t i = 0u; i < 2u; ++i) {
        b_fresh(k_tick0[i]);
        CHECK_EQ(servo_source_id(&b.src), SERVO_SOURCE_INA3221);
        b.pd_gain = 0.25f;
        servo_test_cfg_t c;
        b_cfg(&c);
        c.idle_max_a = 0.3f;
        c.hold_max_a = 0.3f;
        c.travel_max_ms = 100u;     /* not checked: an upper bound */
        b_start(&c);
        CHECK_EQ(b.t.cfg.meter.kind, SERVO_TEST_METER_INA3221);
        const uint32_t began = b_now();
        b_out();
        if (i > 0u) {
            CHECK(began >= 0xFFFFE000u && b_now() < began);
        }
        CHECK_EQ(b.t.why, SERVO_TEST_AB_NONE);
        CHECK_EQ(servo_test_verdict(&b.t), SERVO_TEST_PASS);
        CHECK_EQ(b.offs, 1u);
        CHECK_EQ(b.releases, 1u);
        CHECK(!b.output);
        CHECK_EQ(b.q_lost, 0u);
        CHECK_EQ(b.t.skipped, 0u);
        CHECK_EQ(sense_link_win_lost(&ch.sl), 0u);
        CHECK_EQ(b.t.clipped, 0u);
        for (unsigned k = 0u; k < 2u; ++k) {
            const servo_test_step_t *s = &b.t.steps[k];
            CHECK(s->done);
            CHECK_EQ(s->moves, 4u);
            CHECK_EQ(s->travels, 4u);
            CHECK_EQ(s->timeouts, 0u);
            CHECK_EQ(s->no_rise, 0u);
            /* The model's 0.12 A free and 0.95 A travelling, in windows
             * that lie wholly in their phase: nearer than the PD mini's
             * rig holds them (0.03 A). */
            CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.005f);
            CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.12f, 0.005f);
            CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.12f, 0.005f);
            /* IDLE's 1000 ms hold 19 to 20 windows, and each of the three
             * 600 ms holds at an end 11 to 12: the first window of a phase
             * began before it and is left out. */
            CHECK(s->idle.n >= 19u && s->idle.n <= 20u);
            CHECK(s->hold[0].n >= 33u && s->hold[0].n <= 36u);
            CHECK(s->hold[1].n >= 33u && s->hold[1].n <= 36u);
            /* The peak is a 1 ms sample of the travelling current. */
            CHECK_NEAR(s->move_peak_a, 0.96f, 0.012f);
            /* The servo's 667 ms end to end, late by the rest of the
             * window the arrival falls in, the whole window after it, and
             * the poll that reads that one; never early. */
            CHECK(s->travel_max_ms >= 667u);
            CHECK(s->travel_max_ms <= 667u + 2u * SERVO_TEST_WIN_MS
                                          + SENSE_LINK_WIN_MS + PASS_MS);
            /* The bus voltage at the load side: the set point less the
             * source's drop, lowest while the servo travels. */
            CHECK_NEAR(s->v.sum / (float)s->v.n, s->set_v - 0.10f, 0.03f);
            CHECK_NEAR(s->v_min, s->set_v - 0.95f * (float)SOURCE_OHM,
                       0.03f);
        }
        /* One row a window, every window the link took. */
        CHECK_EQ(b.out.rows - 1u, b.t.readings);
        CHECK_EQ(rows_with(b.out.csv, COL_METER, "INA3221"),
                 b.out.rows - 1u);
        float per_s = 0.0f, mod_s = 0.0f;
        uint32_t every = 0u;
        CHECK(servo_test_rates(&b.t, &per_s, &mod_s, &every));
        CHECK_NEAR(per_s, 20.0f, 0.1f);
        CHECK_NEAR(mod_s, 20.0f, 0.1f);
        CHECK_EQ(every, 50u);
        CHECK(strstr(b.out.report, "Current:        INA3221 CH1, shunt "
                                   "100.0 mOhm, range 1.638 A\n") != NULL);
        CHECK(strstr(b.out.report, "Result:         PASS\n") != NULL);
        CHECK(strstr(b.out.report, "STALL AT 1.50 A cannot be reached")
              == NULL);
    }
}

/* The same bench before the INA3221 has been the meter for 1000 ms: the
 * run starts on the PD mini, reads it every 104 ms to its end, and the
 * INA3221 becoming the meter under it changes nothing. */
TEST_CASE(a_full_run_on_the_pd_mini_beside_a_working_ina3221)
{
    b_fresh(5000u);
    /* A part that reset: the meter is the PD mini for the next second. */
    fake_reset3221(ch.i3221);
    for (unsigned k = 0u; k < 50u
                          && servo_source_id(&b.src) == SERVO_SOURCE_INA3221;
         ++k) {
        b_frame();
    }
    CHECK_EQ(servo_source_id(&b.src), SERVO_SOURCE_PDMINI);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK_EQ(b.t.cfg.meter.kind, SERVO_TEST_METER_PDMINI);
    b_frames_ms(4000u);
    CHECK_EQ(servo_source_id(&b.src), SERVO_SOURCE_INA3221);
    CHECK(servo_test_running(&b.t));
    b_out();
    CHECK_EQ(b.t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&b.t), SERVO_TEST_PASS);
    CHECK_EQ(b.out.rows - 1u, b.t.readings);
    CHECK_EQ(rows_with(b.out.csv, COL_METER, "PDMINI"), b.out.rows - 1u);
    CHECK_EQ(rows_with(b.out.csv, COL_WINDOW, ""), b.out.rows - 1u);
    float per_s = 0.0f;
    uint32_t every = 0u;
    CHECK(servo_test_rates(&b.t, &per_s, NULL, &every));
    CHECK(every >= 103u && every <= 105u);
    for (unsigned k = 0u; k < 2u; ++k) {
        const servo_test_step_t *s = &b.t.steps[k];
        CHECK_EQ(s->travels, 4u);
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.03f);
        /* Late by up to a reading and a frame. */
        CHECK(s->travel_max_ms >= 667u);
        CHECK(s->travel_max_ms <= 667u + PD_MS + FRAME_MS + 10u);
    }
    CHECK(strstr(b.out.report, "Current:        PD mini\n") != NULL);
    CHECK(strstr(b.out.report, "INA3221:        not used: it has worked "
                               "for less than 1 s\n") != NULL);
}

/* The part stops answering in the middle of a run on it: the run ends
 * ABORTED and never reads the PD mini, whose readings arrive throughout. */
TEST_CASE(the_meter_failing_mid_run_never_switches_meter)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    const uint32_t failed = b_now();
    ch.i3221->present = false;
    for (unsigned k = 0u; k < 100u && servo_test_running(&b.t); ++k) {
        b_frame();
    }
    CHECK(!servo_test_running(&b.t));
    /* Within a window, a poll and a frame of the part going: sooner than
     * the 500 ms the run's own rule takes. */
    CHECK(b.t.end_ms - failed <= 250u);
    CHECK(b.t.why == SERVO_TEST_AB_INA_SILENT
          || b.t.why == SERVO_TEST_AB_INA_NO_WINDOW);
    CHECK_EQ(servo_source_id(&b.src), SERVO_SOURCE_PDMINI);
    CHECK(b.pd_samples > 20u);
    b_check_ended(b.t.why);
    /* The part back and the meter again: the run stays ended. */
    ch.i3221->present = true;
    b_frames_ms(3000u);
    CHECK_EQ(servo_test_verdict(&b.t), SERVO_TEST_ABORTED);
    CHECK(!b.output);
}

/* The part resets itself during a run: the coprocessor counts it in
 * RESETS, the meter drops for that poll, and the run ends with that
 * reason although the render loop looks 20 ms later. */
TEST_CASE(a_part_that_resets_itself_ends_the_run)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    fake_reset3221(ch.i3221);
    for (unsigned k = 0u; k < 100u && servo_test_running(&b.t); ++k) {
        b_frame();
    }
    b_check_ended(SERVO_TEST_AB_INA_RESET);
    CHECK(strstr(b.out.report, "Result:         ABORTED - INA3221 reset "
                               "itself\n") != NULL);
}

/* The link goes during a run on the INA3221: the meter is the model, and
 * the run ends as link lost. */
TEST_CASE(the_link_going_ends_a_run_on_the_ina3221)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    b.link_up = false;
    sense_link_lost(&ch.sl);
    b_frame();
    CHECK_EQ(servo_source_id(&b.src), SERVO_SOURCE_MODEL);
    b_check_ended(SERVO_TEST_AB_LINK);
}

/* A servo on a stop, on a bench whose supply limit is above what it
 * draws there.  The model's 2.4 A is over the 0.1 Ohm shunt's 1.638 A:
 * every sample clips and the windows read 1.638 A.  With STALL AT under
 * that the run ends on STALL AT.  With STALL AT at its default, 2.00 A,
 * no reading passes it: the run goes on to its end, counts the clipped
 * windows and says STALL AT cannot be reached -- the shunt decides what
 * this meter can show. */
TEST_CASE(a_servo_on_a_stop_reads_the_range_and_is_judged_on_that)
{
    b_fresh(5000u);
    b.servo.cfg.stop_hi_us = 1850u;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    c.stall_a = 1.5f;
    b_start(&c);
    b_out();
    CHECK(b.t.clipped >= 19u);
    CHECK(b.t.stalled);
    b_check_ended(SERVO_TEST_AB_STALL);
    CHECK(strstr(b.out.report, "Stall threshold  highest 1.638 A, STALL AT "
                               "1.50 A: FAIL\n") != NULL);
    CHECK(strstr(b.out.report, "Clipped:        ") != NULL);
    CHECK(rows_with(b.out.csv, COL_CLIPPED, "50") >= 15u);
    CHECK(rows_with(b.out.csv, COL_CURRENT, "1.638") >= 15u);

    b_fresh(5000u);
    b.servo.cfg.stop_hi_us = 1850u;
    b_cfg(&c);
    c.step_count = 1u;
    c.stall_a = 2.0f;
    b_start(&c);
    b_out();
    CHECK_EQ(b.t.why, SERVO_TEST_AB_NONE);
    CHECK(!b.t.stalled);
    CHECK(b.t.clipped > 40u);
    CHECK_EQ(b.t.cc_readings, 0u);
    CHECK_NEAR(b.t.stall_peak_a, 1.638f, 0.001f);
    CHECK(strstr(b.out.report, "Stall threshold  highest 1.638 A, STALL AT "
                               "2.00 A: PASS\n") != NULL);
    CHECK(strstr(b.out.report, "STALL AT 2.00 A cannot be reached: INA3221 "
                               "range 1.638 A\n") != NULL);
    char want[96];
    snprintf(want, sizeof(want), "Clipped:        %lu windows hold a sample "
             "at an end of the range, 1.638 A", (unsigned long)b.t.clipped);
    CHECK(strstr(b.out.report, want) != NULL);
}

/* The same servo against a supply that holds 1.00 A: the INA3221 reads
 * the limit, under STALL AT, and the supply's constant current ends the
 * run a second after it begins. */
TEST_CASE(a_servo_on_a_stop_against_the_limit_ends_on_constant_current)
{
    b_fresh(5000u);
    b.servo.cfg.stop_hi_us = 1850u;
    b.limits = true;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    c.i_limit = 1.0f;
    c.stall_a = 2.0f;
    b_start(&c);
    b_out();
    CHECK(!b.t.stalled);
    CHECK_EQ(b.t.clipped, 0u);
    CHECK(b.t.cc_longest_ms >= 1000u && b.t.cc_longest_ms < 1000u + PD_MS);
    CHECK_NEAR(b.t.stall_peak_a, 1.0f, 0.002f);
    b_check_ended(SERVO_TEST_AB_CC);
    CHECK(strstr(b.out.report, "STALL AT 2.00 A cannot be reached: current "
                               "limit 1.00 A\n") != NULL);
    CHECK(strstr(b.out.report, "STALL AT 2.00 A cannot be reached: INA3221 "
                               "range 1.638 A\n") != NULL);
}

/* A shunt wired the other way round, through the chain: the part's signed
 * windows reach the run signed. */
TEST_CASE(a_reversed_shunt_reads_negative_through_the_chain)
{
    b_fresh(5000u);
    b.sign = -1.0;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    b_start(&c);
    b_out();
    CHECK_EQ(b.t.why, SERVO_TEST_AB_NONE);
    CHECK(servo_test_negative_at_rest(&b.t));
    const servo_test_step_t *s = &b.t.steps[0];
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, -0.12f, 0.005f);
    CHECK_NEAR(s->peak_a, -0.96f, 0.012f);
    CHECK(strstr(b.out.report, "Current reads negative at rest: shunt "
                               "direction\n") != NULL);
    CHECK(strstr(b.out.csv, ";-0.1") != NULL);
}

int main(void)
{
    RUN(a_run_reads_the_windows_and_none_of_the_supplys_readings);
    RUN(a_run_on_the_pd_mini_reads_no_window);
    RUN(the_ina3221s_range_follows_its_shunt);
    RUN(a_window_without_both_quantities_is_no_reading);
    RUN(no_window_for_500_ms_ends_the_run);
    RUN(no_supply_reading_for_1500_ms_ends_a_run_on_the_ina3221);
    RUN(stall_at_is_judged_on_the_windows_mean);
    RUN(a_second_over_stall_at_ends_the_run_on_the_windows);
    RUN(a_second_over_stall_at_ends_the_run_on_the_pd_mini);
    RUN(constant_current_for_a_second_ends_the_run);
    RUN(a_spell_of_constant_current_is_counted_and_fails_nothing);
    RUN(a_stall_at_no_reading_can_pass_is_said_and_not_refused);
    RUN(a_servo_held_at_the_limit_ends_the_run_on_either_rule);
    RUN(every_ending_of_a_run_on_the_ina3221_switches_off_and_is_marked);
    RUN(each_condition_that_drops_the_meter_ends_the_run);
    RUN(a_current_negative_at_rest_is_reported_and_judged_by_magnitude);
    RUN(a_clipped_window_is_the_value_it_is_and_is_counted);
    RUN(the_csv_reads_back_in_the_viewer_old_and_new);
    RUN(a_recorded_log_with_windows_missing_ends_the_run_at_its_first_gap);
    RUN(a_full_run_on_the_ina3221_through_the_chain);
    RUN(a_full_run_on_the_pd_mini_beside_a_working_ina3221);
    RUN(the_meter_failing_mid_run_never_switches_meter);
    RUN(a_part_that_resets_itself_ends_the_run);
    RUN(the_link_going_ends_a_run_on_the_ina3221);
    RUN(a_servo_on_a_stop_reads_the_range_and_is_judged_on_that);
    RUN(a_servo_on_a_stop_against_the_limit_ends_on_constant_current);
    RUN(a_reversed_shunt_reads_negative_through_the_chain);
    out_reset(&h.out);
    out_reset(&b.out);
    return test_summary("servo_test_win");
}
