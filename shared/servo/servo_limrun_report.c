/*
 * The limit run's outbox and report, and the words the screen and the
 * report name a verdict, a reason and a phase by: see servo_limrun.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"

#include <stdio.h>

#include "servo_limrun_int.h"

/* ------------------------------------------------------------ the outbox */

servo_test_out_t servo_limrun_peek(servo_limrun_t *r, const char **text)
{
    if (text != NULL) {
        *text = "";
    }
    if (r == NULL || r->state != SERVO_LIMRUN_DONE || r->out_done) {
        return SERVO_TEST_OUT_NONE;
    }
    if (!r->out_open) {
        return SERVO_TEST_OUT_OPEN_TXT;
    }
    if (!r->out_end) {
        if (servo_limrun_report_line(r, r->out_line, r->scratch,
                                     sizeof(r->scratch))) {
            if (text != NULL) {
                *text = r->scratch;
            }
            return SERVO_TEST_OUT_TXT;
        }
        r->out_end = true;
    }
    return SERVO_TEST_OUT_END;
}

void servo_limrun_pop(servo_limrun_t *r)
{
    if (r == NULL || r->state != SERVO_LIMRUN_DONE || r->out_done) {
        return;
    }
    if (!r->out_open) {
        r->out_open = true;
    } else if (!r->out_end) {
        ++r->out_line;
    } else {
        r->out_done = true;
    }
}

bool servo_limrun_drained(const servo_limrun_t *r)
{
    return r == NULL || r->state == SERVO_LIMRUN_IDLE
        || (r->state == SERVO_LIMRUN_DONE && r->out_done);
}

/* ------------------------------------------------------------ the report */

/*
 * One line at a time, found by counting, as the automatic test's report is
 * (servo_report.c): a cursor walks the lines in order and prints the one
 * asked for.
 */
typedef struct {
    unsigned want, at;
    char    *buf;
    size_t   n;
    const char *const *text;
} cur_t;

#define LS(id) servo_str_in(c->text, SERVO_STR_##id)

static bool here(cur_t *c)
{
    return c->want == c->at++;
}

static bool blank(cur_t *c)
{
    if (here(c)) {
        c->buf[0] = '\0';
        return true;
    }
    return false;
}

static bool text_line(cur_t *c, servo_str_t id)
{
    if (here(c)) {
        snprintf(c->buf, c->n, "%s", servo_str_in(c->text, id));
        return true;
    }
    return false;
}

static bool end_lines(const servo_limrun_t *r, cur_t *c,
                      const servo_limrun_side_t *e, uint8_t side)
{
    const char *label = side == SERVO_LIMRUN_LO ? LS(L_R_LOW) : LS(L_R_HIGH);
    if (here(c)) {
        if (!e->done) {
            snprintf(c->buf, c->n, LS(L_R_END_NOT_RUN), label);
        } else if (e->bound) {
            snprintf(c->buf, c->n, LS(L_R_END_BOUND), label,
                     (unsigned)e->bind_us, (unsigned)e->limit_us,
                     (double)e->baseline_a, (double)e->knee_a);
        } else {
            snprintf(c->buf, c->n, LS(L_R_END_FREE), label,
                     (unsigned)e->limit_us, (double)e->baseline_a);
        }
        return true;
    }
    if (r->cfg.enc_on && e->done && here(c)) {
        if (e->enc) {
            snprintf(c->buf, c->n, LS(L_R_ANGLE), (double)e->enc_deg);
        } else {
            snprintf(c->buf, c->n, "%s", LS(L_R_ANGLE_NONE));
        }
        return true;
    }
    return false;
}

static bool head_lines(const servo_limrun_t *r, cur_t *c)
{
    const servo_limrun_cfg_t *g = &r->cfg;
    if (text_line(c, SERVO_STR_L_R_TITLE)) {
        return true;
    }
    if (here(c)) {
        const char *v = servo_str_in(c->text,
                                     servo_limrun_verdict_str(r->verdict));
        if (r->why != SERVO_LIMRUN_WHY_NONE) {
            snprintf(c->buf, c->n, LS(R_RESULT_WHY), v,
                     servo_str_in(c->text, servo_limrun_why_str(r)));
        } else {
            snprintf(c->buf, c->n, LS(R_RESULT), v);
        }
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(R_DEVICE), g->dut);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(R_FIRMWARE), g->firmware);
        return true;
    }
    if (text_line(c, g->program      ? SERVO_STR_L_R_MODE_PROGRAM
                     : g->set_pulses ? SERVO_STR_L_R_MODE_PULSES
                                     : SERVO_STR_L_R_MODE_SEARCH)) {
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_OUTPUT), g->output,
                 (unsigned)g->frame_hz);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PULSE), (unsigned)g->centre_us,
                 (unsigned)g->min_us, (unsigned)g->max_us);
        return true;
    }
    if (r->searched && here(c)) {
        /* Where the searches ran: the servo's own pulses once read.  A run
         * that ended before its first search has no such line. */
        snprintf(c->buf, c->n, LS(L_R_SEARCH), (unsigned)r->ctr_us,
                 (unsigned)r->end_us[SERVO_LIMRUN_LO],
                 (unsigned)r->end_us[SERVO_LIMRUN_HI]);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_METER),
                 (double)g->shunt_dmohm / 10.0);
        return true;
    }
    if (here(c)) {
        servo_limit_cfg_t d;
        servo_limit_defaults(&d, g->centre_us, true);
        snprintf(c->buf, c->n, LS(L_R_RULE), (double)d.knee_ratio,
                 (double)d.knee_margin_a);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PROTECT),
                 (double)SERVO_LIMRUN_HARD_MA / 1000.0,
                 (double)SERVO_LIMRUN_STALL_MA / 1000.0,
                 (unsigned)SERVO_LIMRUN_STALL_MS,
                 (unsigned)SERVO_LIMRUN_WIN_STALE_MS,
                 (unsigned)(SERVO_LIMRUN_SEARCH_MS / 1000u));
        return true;
    }
    if (here(c)) {
        const uint32_t ms = r->end_ms - r->start_ms;
        snprintf(c->buf, c->n, LS(R_DURATION), (unsigned long)(ms / 1000u),
                 (unsigned long)((ms % 1000u) / 100u));
        return true;
    }
    if (!g->enc_on && text_line(c, SERVO_STR_L_R_ENC_OFF)) {
        return true;
    }
    if (blank(c) || text_line(c, SERVO_STR_L_R_ENDS_HEAD)) {
        return true;
    }
    return end_lines(r, c, &r->first[SERVO_LIMRUN_LO], SERVO_LIMRUN_LO)
        || end_lines(r, c, &r->first[SERVO_LIMRUN_HI], SERVO_LIMRUN_HI);
}

static servo_str_t side_word(const servo_limrun_t *r, int g)
{
    switch (r->reg_side[g]) {
    case SERVO_LIMRUN_LO: return SERVO_STR_L_R_SIDE_LO;
    case SERVO_LIMRUN_HI: return SERVO_STR_L_R_SIDE_HI;
    default:              return SERVO_STR_L_R_SIDE_UNKNOWN;
    }
}

/* A share of travel, 0.1 us over 0.1 us, as three decimals; "-" for none. */
static void share_text(char *buf, size_t n, int32_t d, int32_t span)
{
    if (d <= 0 || span <= 0) {
        snprintf(buf, n, "-");
    } else {
        snprintf(buf, n, "%.3f", (double)d / (double)span);
    }
}

static void raw_text(char *buf, size_t n, uint16_t v)
{
    if (v == 0u) {
        snprintf(buf, n, "-");
    } else {
        snprintf(buf, n, "%u", (unsigned)v);
    }
}

static bool reg_lines(const servo_limrun_t *r, cur_t *c, int g)
{
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_REG),
                 g == SERVO_LIMRUN_LEFT ? LS(L_R_LEFT) : LS(L_R_RIGHT),
                 (unsigned)r->servo.angle[g], (unsigned)r->cur[g],
                 servo_str_in(c->text, side_word(r, g)));
        return true;
    }
    const int8_t s = r->reg_side[g];
    if (s >= 0 && r->need[s] && here(c)) {
        const int32_t span = span_now(r, (uint8_t)s);
        char f0[12], fb[12], v1[8], f1[12], v2[8];
        share_text(f0, sizeof(f0), r->d0_dus[s], span);
        share_text(fb, sizeof(fb), r->db_dus[s], span);
        raw_text(v1, sizeof(v1), r->v1[s]);
        share_text(f1, sizeof(f1), r->d1_dus[s], span);
        raw_text(v2, sizeof(v2),
                 (r->stage[s] == ST_FINAL || r->stage[s] == ST_DONE)
                     ? r->v2[s] : 0u);
        snprintf(c->buf, c->n, LS(L_R_REG_LIN), f0, fb, v1, f1, v2);
        return true;
    }
    return false;
}

/* NEUTRAL, PULSE LOWER and PULSE UPPER, 0.1 us, as microseconds. */
static double us_of(uint16_t dus)
{
    return (double)dus / 10.0;
}

static bool prog_lines(const servo_limrun_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_L_R_PROG_HEAD)) {
        return true;
    }
    if (!r->cfg.read) {
        return text_line(c, SERVO_STR_L_R_PROG_OFF);
    }
    if (!r->read_once) {
        return text_line(c, SERVO_STR_L_R_PROG_NO_SERVO);
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_SERVO), r->servo.type,
                 r->servo.reversion ? LS(R_ON) : LS(R_OFF));
        return true;
    }
    const uint16_t *p0 = r->servo.pulse_dus;
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PULSES),
                 us_of(p0[SERVO_LIMRUN_P_NEUTRAL]),
                 us_of(p0[SERVO_LIMRUN_P_LOWER]),
                 us_of(p0[SERVO_LIMRUN_P_UPPER]));
        return true;
    }
    if (r->cfg.set_pulses && here(c)) {
        snprintf(c->buf, c->n, LS(L_R_PULSE_SET),
                 us_of(p0[SERVO_LIMRUN_P_LOWER]),
                 us_of(r->pulse_now[SERVO_LIMRUN_P_LOWER]),
                 us_of(p0[SERVO_LIMRUN_P_UPPER]),
                 us_of(r->pulse_now[SERVO_LIMRUN_P_UPPER]));
        return true;
    }
    if (r->why == SERVO_LIMRUN_WHY_RANGE && here(c)) {
        snprintf(c->buf, c->n, LS(L_R_RANGE),
                 us_of(r->pulse_now[SERVO_LIMRUN_P_LOWER]),
                 us_of(r->pulse_now[SERVO_LIMRUN_P_UPPER]),
                 (unsigned)r->cfg.min_us, (unsigned)r->cfg.max_us);
        return true;
    }
    if (!r->cfg.program) {
        if (text_line(c, SERVO_STR_L_R_PROG_OFF)) {
            return true;
        }
    } else if (r->verdict == SERVO_LIMRUN_FREE) {
        if (text_line(c, SERVO_STR_L_R_PROG_FREE)) {
            return true;
        }
    } else {
        if (reg_lines(r, c, SERVO_LIMRUN_LEFT)
            || reg_lines(r, c, SERVO_LIMRUN_RIGHT)) {
            return true;
        }
        if (here(c)) {
            snprintf(c->buf, c->n, LS(L_R_METHOD),
                     (unsigned)SERVO_LIMRUN_OVERSET_PCT);
            return true;
        }
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(L_R_WRITES), r->writes, r->write_ops,
                 (unsigned)SERVO_LIMRUN_WRITES_MAX, r->sessions);
        return true;
    }
    /* The programmer holds the values before the run from the run's first
     * write on: a run that sent none has no backup. */
    if (r->write_ops > 0u && here(c)) {
        if (r->servo.backup[0] != '\0') {
            snprintf(c->buf, c->n, LS(L_R_BACKUP), r->servo.backup);
        } else {
            snprintf(c->buf, c->n, "%s", LS(L_R_BACKUP_MEM));
        }
        return true;
    }
    if (servo_limrun_restore(r) && text_line(c, SERVO_STR_L_R_RESTORE)) {
        return true;
    }
    if (!r->cfg.program || r->verdict == SERVO_LIMRUN_FREE) {
        return false;
    }
    if (blank(c) || text_line(c, SERVO_STR_L_R_CHECK_HEAD)) {
        return true;
    }
    return end_lines(r, c, &r->after[SERVO_LIMRUN_LO], SERVO_LIMRUN_LO)
        || end_lines(r, c, &r->after[SERVO_LIMRUN_HI], SERVO_LIMRUN_HI);
}

static bool tail_lines(const servo_limrun_t *r, cur_t *c)
{
    if (blank(c)) {
        return true;
    }
    if (r->cfg.program && text_line(c, SERVO_STR_L_R_UNIT)) {
        return true;
    }
    return text_line(c, SERVO_STR_L_R_BENCH);
}

bool servo_limrun_report_line(const servo_limrun_t *r, unsigned idx,
                              char *buf, size_t n)
{
    if (r == NULL || buf == NULL || n == 0u
        || r->state != SERVO_LIMRUN_DONE) {
        return false;
    }
    cur_t c = { idx, 0u, buf, n, r->cfg.text };
    return head_lines(r, &c) || prog_lines(r, &c) || tail_lines(r, &c);
}

/* -------------------------------------------------------------- the words */

servo_str_t servo_limrun_verdict_str(servo_limrun_verdict_t v)
{
    switch (v) {
    case SERVO_LIMRUN_FOUND:   return SERVO_STR_L_FOUND;
    case SERVO_LIMRUN_FREE:    return SERVO_STR_L_FREE;
    case SERVO_LIMRUN_PASS:    return SERVO_STR_PASS;
    case SERVO_LIMRUN_FAIL:    return SERVO_STR_FAIL;
    default:                   return SERVO_STR_ABORTED;
    }
}

servo_str_t servo_limrun_why_str(const servo_limrun_t *r)
{
    if (r == NULL) {
        return SERVO_STR_AB_NONE;
    }
    switch (r->why) {
    case SERVO_LIMRUN_WHY_TEST:           return servo_test_abort_str(r->test_why);
    case SERVO_LIMRUN_WHY_HARD:           return SERVO_STR_L_WHY_HARD;
    case SERVO_LIMRUN_WHY_CLIPPED:        return SERVO_STR_L_WHY_CLIPPED;
    case SERVO_LIMRUN_WHY_STALL:          return SERVO_STR_L_WHY_STALL;
    case SERVO_LIMRUN_WHY_SEARCH_LONG:    return SERVO_STR_L_WHY_SEARCH_LONG;
    case SERVO_LIMRUN_WHY_SUPPLY_CHANGED: return SERVO_STR_L_WHY_SUPPLY;
    case SERVO_LIMRUN_WHY_BENCH:          return SERVO_STR_L_WHY_BENCH;
    case SERVO_LIMRUN_WHY_NOT_KST:        return SERVO_STR_L_WHY_NOT_KST;
    case SERVO_LIMRUN_WHY_PROG:           return SERVO_STR_L_WHY_PROG;
    case SERVO_LIMRUN_WHY_WRITE:          return SERVO_STR_L_WHY_WRITE;
    case SERVO_LIMRUN_WHY_OTHER_VALUES:   return SERVO_STR_L_WHY_OTHER;
    case SERVO_LIMRUN_WHY_MAPPING:        return SERVO_STR_L_WHY_MAPPING;
    case SERVO_LIMRUN_WHY_SMALLEST:       return SERVO_STR_L_WHY_SMALLEST;
    case SERVO_LIMRUN_WHY_WRITE_CAP:      return SERVO_STR_L_WHY_CAP;
    case SERVO_LIMRUN_WHY_STILL_BINDS:    return SERVO_STR_L_WHY_STILL;
    case SERVO_LIMRUN_WHY_NO_VOLTAGE:     return SERVO_STR_L_WHY_NO_VOLTAGE;
    case SERVO_LIMRUN_WHY_NO_STOP:        return SERVO_STR_L_WHY_NO_STOP;
    case SERVO_LIMRUN_WHY_BEFORE_NEUTRAL: return SERVO_STR_L_WHY_BEFORE_NEUTRAL;
    case SERVO_LIMRUN_WHY_NEUTRAL:        return SERVO_STR_L_WHY_NEUTRAL;
    case SERVO_LIMRUN_WHY_RANGE:          return SERVO_STR_L_WHY_RANGE;
    default:                              return SERVO_STR_AB_NONE;
    }
}

servo_str_t servo_limrun_phase_str(servo_limrun_phase_t ph)
{
    switch (ph) {
    case SERVO_LIMRUN_PH_SEARCH:  return SERVO_STR_L_PH_SEARCH;
    case SERVO_LIMRUN_PH_SAFE:    return SERVO_STR_L_PH_SAFE;
    case SERVO_LIMRUN_PH_CONNECT: return SERVO_STR_L_PH_CONNECT;
    case SERVO_LIMRUN_PH_WRITE:   return SERVO_STR_L_PH_WRITE;
    case SERVO_LIMRUN_PH_LEAVE:   return SERVO_STR_L_PH_LEAVE;
    case SERVO_LIMRUN_PH_ARM:     return SERVO_STR_L_PH_ARM;
    case SERVO_LIMRUN_PH_PROBE:   return SERVO_STR_L_PH_PROBE;
    case SERVO_LIMRUN_PH_CHECK:   return SERVO_STR_L_PH_CHECK;
    default:                      return SERVO_STR_PHASE_NONE;
    }
}
