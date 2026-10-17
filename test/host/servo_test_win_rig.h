/*
 * The rig of the automatic servo test on the INA3221's CH1 windows:
 * test_servo_test_win.c, test_servo_test_win_read.c and
 * test_servo_test_win_chain.c.
 *
 * Shared: the abort and the meter as the SERVO screen maps them, what a
 * run hands over (report, CSV, counts) and the CSV's columns.  The hand:
 * windows and supply readings given at the millisecond a case names, with
 * helpers that run a run to a phase and check how it ended.  Every
 * function is static inline and `h` is the hand's one instance.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "sense_chain.h"
#include "servo_sim.h"
#include "servo_source.h"
#include "servo_test.h"

#define LO_US   1100u
#define HI_US   1900u
#define CENTRE  1500u

/* The run's abort for the condition that took the INA3221 off the rail, as
 * the SERVO screen maps it. */
static inline servo_test_abort_t ab_of(servo_source_why_t why)
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

static inline servo_test_meter_now_t meter_of(const servo_source_t *s)
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

static inline void out_reset(out_t *o)
{
    free(o->csv);
    memset(o, 0, sizeof(*o));
}

static inline void out_drain(out_t *o, servo_test_t *t)
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
                REQUIRE_REALLOC(o->csv, o->csv_cap);
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
static inline bool cell_of(const char *line, unsigned idx, char *cell, size_t n)
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
static inline unsigned rows_with(const char *csv, unsigned idx, const char *want)
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

static inline void h_fresh(uint32_t tick0)
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
static inline void h_cfg(servo_test_cfg_t *c, bool ina)
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

static inline void h_start(const servo_test_cfg_t *c)
{
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = h.online;
    last.output = h.output;
    CHECK_EQ(servo_test_start(&h.t, c, h.now, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
}

/* A reading of the supply taken at @p at. */
static inline void h_pd_at(uint32_t at)
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
static inline void h_win_at(uint32_t at, int ma)
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
static inline int h_ma(void)
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
static inline void h_step_at(uint32_t at)
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
static inline void h_ms(void)
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

static inline void h_run_ms(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; ++k) {
        h_ms();
    }
}

/* Until the run is in @p phase, at most 20 s. */
static inline bool h_until(servo_test_phase_t phase)
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
static inline void h_out(void)
{
    for (uint32_t k = 0u; k < 60000u && !servo_test_drained(&h.t); ++k) {
        h_ms();
    }
    CHECK(servo_test_drained(&h.t));
}

/* A run on the INA3221 in IDLE at tick @p tick0, the automatic windows
 * and readings off: the case feeds its own from here. */
static inline void h_idle_by_hand(uint32_t tick0, const servo_test_cfg_t *c)
{
    h_fresh(tick0);
    h_start(c);
    CHECK(h_until(SERVO_TEST_PH_IDLE));
    h.win_on = false;
    h.pd_on  = false;
}

/* The start tick that puts a run's time @p rel ms after its start 500 ms
 * before the tick wraps. */
static inline uint32_t wrap_at(uint32_t rel)
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
static inline void h_check_ended(servo_test_abort_t why)
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
