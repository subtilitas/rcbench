/**
 * @file
 * @brief AUTO end points: the outbox, the report RUDDRnnn.TXT, the CSV
 *        RUDDRnnn.CSV and the words a reason and a phase are named by; see
 *        servo_rudder_auto.h.
 *
 * The report is written in the language of the run's table, the CSV in
 * English, as the automatic test's (servo_str.c).
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_auto.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief The CSV's columns: one row a measure. */
#define RAUTO_CSV_HEAD "step;side (us);write;value;measured (deg);" \
                       "goal (deg);measured (mm);goal (mm);within"

/* ------------------------------------------------------------ the outbox */

/**
 * @brief Line @p idx of the CSV: the header, then one row a step.
 * @param r   Run state.
 * @param idx Line, from 0.
 * @param buf Out: the line.
 * @param n   Size of @p buf, bytes.
 * @return false past the last row.
 */
static bool csv_line(const servo_rauto_t *r, unsigned idx, char *buf,
                     size_t n);

servo_test_out_t servo_rauto_peek(servo_rauto_t *r, const char **text)
{
    if (text != NULL) {
        *text = "";
    }
    if (r == NULL || r->state != SERVO_RAUTO_DONE || r->out_done) {
        return SERVO_TEST_OUT_NONE;
    }
    if (!r->out_open) {
        return SERVO_TEST_OUT_OPEN;
    }
    /* The CSV first, then the report: the order the logger writes an
     * automatic test's two files in, one open at a time. */
    if (!r->csv_open) {
        if (csv_line(r, r->out_line, r->scratch, sizeof(r->scratch))) {
            if (text != NULL) {
                *text = r->scratch;
            }
            return SERVO_TEST_OUT_CSV;
        }
        r->csv_open = true;
        r->out_line = 0u;
    }
    if (!r->out_end) {
        if (servo_rauto_report_line(r, r->out_line, r->scratch,
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

void servo_rauto_pop(servo_rauto_t *r)
{
    if (r == NULL || r->state != SERVO_RAUTO_DONE || r->out_done) {
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

bool servo_rauto_drained(const servo_rauto_t *r)
{
    return r == NULL || r->state == SERVO_RAUTO_IDLE
        || (r->state == SERVO_RAUTO_DONE && r->out_done);
}

/* ------------------------------------------------------------ the report */

/** @brief A cursor over the report's lines, as servo_limrun_report.c's. */
typedef struct {
    unsigned want,              /**< index asked for */
             at;                /**< index of the next line */
    char    *buf;               /**< out: the line */
    size_t   n;                 /**< size of buf, bytes */
    const char *const *text;    /**< the language table; NULL for English */
} cur_t;

/** @brief Entry @p id of the cursor's table. */
#define LS(id) servo_str_in(c->text, SERVO_STR_##id)

/**
 * @brief Count one line, and tell whether it is the one asked for.
 * @param c Cursor.
 * @return true when it is.
 */
static bool here(cur_t *c)
{
    return c->want == c->at++;
}

/**
 * @brief An empty line.
 * @param c Cursor.
 * @return true when it is the line asked for.
 */
static bool blank(cur_t *c)
{
    if (here(c)) {
        c->buf[0] = '\0';
        return true;
    }
    return false;
}

/**
 * @brief A table entry as a line.
 * @param c  Cursor.
 * @param id The entry.
 * @return true when it is the line asked for.
 */
static bool text_line(cur_t *c, servo_str_t id)
{
    if (here(c)) {
        snprintf(c->buf, c->n, "%s", servo_str_in(c->text, id));
        return true;
    }
    return false;
}

/**
 * @brief The trailing edge's travel for a deflection: L x sin(theta).
 * @param deg    The deflection, °.
 * @param len_mm The surface's length, mm.
 * @return the travel, mm.
 */
static float mm_of(float deg, float len_mm)
{
    return len_mm * sinf(deg * 3.14159265f / 180.0f);
}

/**
 * @brief The family's name, which stays English.
 * @param f servo_rauto_family_t.
 * @return the name.
 */
static const char *family_name(uint8_t f)
{
    switch (f) {
    case SERVO_RAUTO_FAM_HITEC_D: return "Hitec D";
    case SERVO_RAUTO_FAM_HITEC_C: return "Hitec";
    default:                      return "KST/Chaservo";
    }
}

/**
 * @brief The margin's line: on KST the servo's pulse range as written and
 *        the extended ends by the linear model; on Hitec its lock at 0%.
 * @param r Run state.
 * @param c Cursor, at the line asked for.
 */
static void margin_line(const servo_rauto_t *r, cur_t *c)
{
    const servo_rauto_cfg_t *g = &r->cfg;
    if (g->family != SERVO_RAUTO_FAM_KST) {
        snprintf(c->buf, c->n, "%s", LS(RA_R_MARGIN_HITEC));
        return;
    }
    uint16_t us[2], dus[2];
    servo_rauto_pulse_range(g->margin_pct, us);
    servo_rauto_kst_pulses(g->margin_pct, dus);
    const double k = 1.0 + (double)g->margin_pct / 100.0;
    snprintf(c->buf, c->n, LS(RA_R_MARGIN), (unsigned)g->margin_pct,
             (double)dus[0] / 10.0, (double)dus[1] / 10.0, (unsigned)us[0],
             (double)g->target_deg[0] * k, (unsigned)us[1],
             (double)g->target_deg[1] * k);
}

/**
 * @brief The head: title, result, the servo, what it drives and reads, the
 *        protections, the targets and the settle rule, the duration and the
 *        angle source's gaps.
 * @param r Run state.
 * @param c Cursor.
 * @return true when the line asked for is in the head.
 */
static bool head_lines(const servo_rauto_t *r, cur_t *c)
{
    const servo_rauto_cfg_t *g = &r->cfg;
    if (text_line(c, SERVO_STR_RA_R_TITLE)) {
        return true;
    }
    if (here(c)) {
        const char *v = r->verdict == SERVO_RAUTO_PASS    ? LS(PASS)
                        : r->verdict == SERVO_RAUTO_FAIL  ? LS(FAIL)
                                                          : LS(ABORTED);
        char why[64];
        if (r->why == SERVO_RAUTO_WHY_BINDS) {
            snprintf(why, sizeof(why), LS(RA_BINDS_AT), (double)r->bind_deg);
        } else if (r->why == SERVO_RAUTO_WHY_STOP) {
            snprintf(why, sizeof(why), LS(RA_STOP_AT), (unsigned)r->stop_us);
        } else {
            snprintf(why, sizeof(why), "%s",
                     servo_str_in(c->text, servo_rauto_why_str(r)));
        }
        if (r->why != SERVO_RAUTO_WHY_NONE) {
            snprintf(c->buf, c->n, LS(R_RESULT_WHY), v, why);
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
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_SERVO), g->type,
                 family_name(g->family));
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_OUTPUT), g->output);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_SOURCE), g->source);
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_METER),
                 g->meter[0] != '\0' ? g->meter : "INA3221");
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_PROTECT),
                 (double)SERVO_LIMRUN_HARD_MA / 1000.0,
                 (double)SERVO_LIMRUN_STALL_MA / 1000.0,
                 (unsigned)SERVO_LIMRUN_STALL_MS,
                 (unsigned)SERVO_LIMRUN_WIN_STALE_MS,
                 (unsigned)SERVO_GAP_MAX_MS,
                 (unsigned)(SERVO_GAP_EVERY_MS / 1000u));
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_TARGETS), (double)g->target_deg[0],
                 (double)g->target_deg[1], (double)g->tol_ddeg / 10.0);
        return true;
    }
    if (here(c)) {
        margin_line(r, c);
        return true;
    }
    if (g->len_mm > 0.0f && here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_LENGTH), (double)g->len_mm,
                 (double)mm_of(r->goal_deg[0], g->len_mm),
                 (double)mm_of(r->goal_deg[1], g->len_mm));
        return true;
    }
    if (here(c)) {
        if (g->settle_win_ms > 0u) {
            snprintf(c->buf, c->n, LS(RA_R_SETTLE_IMU),
                     (unsigned)SERVO_RAUTO_STILL_MS,
                     (double)g->settle_band_deg,
                     (double)g->settle_win_ms / 1000.0,
                     (unsigned)servo_rauto_wait_ms(r));
        } else {
            snprintf(c->buf, c->n, LS(RA_R_SETTLE),
                     (unsigned)SERVO_RAUTO_STILL_MS,
                     (unsigned)servo_rauto_wait_ms(r));
        }
        return true;
    }
    if (here(c)) {
        const uint32_t ms = r->end_ms - r->start_ms;
        snprintf(c->buf, c->n, LS(R_DURATION), (unsigned long)(ms / 1000u),
                 (unsigned long)((ms % 1000u) / 100u));
        return true;
    }
    if (r->gap.count > 0u && here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_GAPS), (unsigned long)r->gap.count,
                 (unsigned long)r->gap.longest_ms);
        return true;
    }
    return false;
}

/**
 * @brief One register's line: its label, its value before and after, the
 *        side it was found to move.
 * @param r Run state.
 * @param c Cursor.
 * @param g The register.
 * @return true when it is the line asked for.
 */
static bool reg_line(const servo_rauto_t *r, cur_t *c, int g)
{
    if (!here(c)) {
        return false;
    }
    /* The register by its name (servo_limrun_reg_name(): the families are
     * the limit run's), padded to the column of KST's labels; a Hitec
     * servo's CCW stands in REVERSION. */
    const char *col = g == 0 ? LS(L_R_LEFT) : LS(L_R_RIGHT);
    char label[32];
    if (r->cfg.family == SERVO_RAUTO_FAM_KST) {
        snprintf(label, sizeof(label), "%s", col);
    } else {
        char head[28];
        snprintf(head, sizeof(head), "%s:",
                 servo_limrun_reg_name(
                     r->cfg.family,
                     (uint8_t)((unsigned)g | (r->servo.reversion
                                                  ? SERVO_LIMRUN_REG_CCW
                                                  : 0u))));
        const int w = (int)strlen(col) - 1;
        snprintf(label, sizeof(label), "%-*s ", w > 0 ? w : 0, head);
    }
    const servo_str_t side = r->reg_side[g] == 0   ? SERVO_STR_RA_R_SIDE_LO
                             : r->reg_side[g] == 1 ? SERVO_STR_RA_R_SIDE_HI
                                                   : SERVO_STR_L_R_SIDE_UNKNOWN;
    /* The registers as the programming page shows them, the unit named. */
    snprintf(c->buf, c->n, LS(RA_R_REG), label,
             (unsigned)servo_rauto_reg_of(r, g, r->servo.value[g]),
             (unsigned)servo_rauto_reg_of(r, g, r->cur[g]),
             servo_str_in(c->text, servo_rauto_unit(r)),
             servo_str_in(c->text, side));
    return true;
}

/**
 * @brief The programming: the servo's pulses, the registers, the writes,
 *        the backup and RESTORE.
 * @param r Run state.
 * @param c Cursor.
 * @return true when the line asked for is in it.
 */
static bool prog_lines(const servo_rauto_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_RA_R_PROG_HEAD)) {
        return true;
    }
    if (!r->read_once) {
        return text_line(c, SERVO_STR_L_R_PROG_NO_SERVO);
    }
    const uint16_t *p0 = r->servo.pulse_dus;
    if (r->cfg.family == SERVO_RAUTO_FAM_KST) {
        if (here(c)) {
            snprintf(c->buf, c->n, LS(L_R_PULSES), (double)p0[0] / 10.0,
                     (double)p0[1] / 10.0, (double)p0[2] / 10.0);
            return true;
        }
        if (r->pulses_landed && here(c)) {
            snprintf(c->buf, c->n, LS(L_R_PULSE_SET), (double)p0[1] / 10.0,
                     (double)r->pulse_now[1] / 10.0, (double)p0[2] / 10.0,
                     (double)r->pulse_now[2] / 10.0);
            return true;
        }
        uint16_t prof[2];
        if (servo_rauto_widen(r, prof) && here(c)) {
            snprintf(c->buf, c->n, LS(RA_R_WIDEN), (unsigned)r->cfg.min_us,
                     (unsigned)r->cfg.max_us, (unsigned)prof[0],
                     (unsigned)prof[1]);
            return true;
        }
    }
    if (reg_line(r, c, 0) || reg_line(r, c, 1)) {
        return true;
    }
    if (here(c)) {
        snprintf(c->buf, c->n, LS(RA_R_WRITES), (unsigned)r->writes[0],
                 (unsigned)r->writes[1], (unsigned)SERVO_RAUTO_WRITES_MAX,
                 r->sessions);
        return true;
    }
    /* A write cut off on its way counts: it may have landed. */
    if ((r->write_ops > 0u || servo_rauto_restore(r)) && here(c)) {
        if (r->servo.backup[0] != '\0') {
            snprintf(c->buf, c->n, LS(L_R_BACKUP), r->servo.backup);
        } else {
            snprintf(c->buf, c->n, "%s", LS(L_R_BACKUP_MEM));
        }
        return true;
    }
    return servo_rauto_restore(r)
           && text_line(c, r->cfg.family == SERVO_RAUTO_FAM_KST
                               ? SERVO_STR_RA_R_RESTORE
                               : SERVO_STR_L_R_RESTORE_HITEC);
}

/**
 * @brief A search row's angle as printed: one that rounds to 0 is +0.
 * @param p The row.
 * @return the angle, °.
 */
static double srow_deg(const servo_rauto_srow_t *p)
{
    return fabs((double)p->deg) < 0.005 ? 0.0 : (double)p->deg;
}

/**
 * @brief One side's end of the limit search: bound with its pulses and
 *        currents, free to the margin's end, or not searched.
 * @param r Run state.
 * @param c Cursor.
 * @param s The side: 0 LOW END, 1 HIGH END.
 * @return true when it is the line asked for.
 */
static bool end_line(const servo_rauto_t *r, cur_t *c, unsigned s)
{
    if (!here(c)) {
        return false;
    }
    const servo_rauto_end_t *e = &r->ends[s];
    const char *head = s == 0u ? LS(L_R_LOW) : LS(L_R_HIGH);
    if (!e->done) {
        snprintf(c->buf, c->n, LS(L_R_END_NOT_RUN), head);
    } else if (e->bound) {
        snprintf(c->buf, c->n, LS(L_R_END_BOUND), head, (unsigned)e->bind_us,
                 (unsigned)e->end_us, (double)e->free_a,
                 (double)e->bound_a);
    } else {
        snprintf(c->buf, c->n, LS(L_R_END_FREE), head, (unsigned)e->end_us,
                 (double)e->free_a);
    }
    return true;
}

/**
 * @brief The limit search: its two ends, then one line a position judged.
 * @param r Run state.
 * @param c Cursor.
 * @return true when the line asked for is in it.
 */
static bool search_lines(const servo_rauto_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_RA_R_SEARCH_HEAD)) {
        return true;
    }
    if (here(c)) {
        servo_limit_cfg_t lc;
        servo_limit_defaults(&lc, r->cfg.center_us, true);
        snprintf(c->buf, c->n, LS(RA_R_SEARCH_RULE), (unsigned)lc.step_us,
                 (unsigned)lc.center_us, (unsigned)lc.settle_ms,
                 (unsigned)lc.measure_ms, (double)lc.knee_ratio,
                 (double)lc.knee_margin_a, (unsigned)lc.backoff_us);
        return true;
    }
    if (end_line(r, c, 0u) || end_line(r, c, 1u)) {
        return true;
    }
    for (unsigned k = 0; k < r->srow_n; ++k) {
        if (!here(c)) {
            continue;
        }
        const servo_rauto_srow_t *p = &r->srow[k];
        char deg[16] = "-";
        if (p->valid) {
            snprintf(deg, sizeof(deg), "%.2f°", srow_deg(p));
        }
        snprintf(c->buf, c->n, LS(RA_R_SEARCH_ROW), (unsigned)p->us,
                 (double)p->ma / 1000.0, deg);
        return true;
    }
    return false;
}

/**
 * @brief The steps: one line a measure.
 * @param r Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of them.
 */
static bool step_lines(const servo_rauto_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_RA_R_STEPS_HEAD)) {
        return true;
    }
    if (r->step_n == 0u) {
        return text_line(c, SERVO_STR_RA_R_NO_STEPS);
    }
    for (unsigned k = 0; k < r->step_n; ++k) {
        if (!here(c)) {
            continue;
        }
        const servo_rauto_step_t *p = &r->steps[k];
        char v[8];
        if (p->value != 0u) {
            snprintf(v, sizeof(v), "%u",
                     (unsigned)servo_rauto_reg_of(r, p->reg, p->value));
        } else {
            snprintf(v, sizeof(v), "-");
        }
        snprintf(c->buf, c->n, LS(RA_R_STEP), p->side == 0u ? 1000u : 2000u,
                 (unsigned)p->n, v, (double)p->measured_deg,
                 (double)p->goal_deg,
                 p->within ? LS(RA_R_IN) : LS(RA_R_OUT));
        return true;
    }
    return false;
}

/**
 * @brief The pulse of a verify measure.
 * @param r Run state.
 * @param k The measure, 0 to SERVO_RAUTO_VERIFY_N - 1.
 * @return 1000, 2000, or PULSE CENTER, us.
 */
static unsigned verify_us(const servo_rauto_t *r, unsigned k)
{
    return k == 0u ? 1000u : k == 1u ? 2000u : (unsigned)r->cfg.center_us;
}

/**
 * @brief The goal of a verify measure: the side's, 0 for the center.
 * @param r Run state.
 * @param k The measure.
 * @return The goal, °.
 */
static float verify_goal(const servo_rauto_t *r, unsigned k)
{
    return k < 2u ? r->goal_deg[k] : 0.0f;
}

/**
 * @brief A verify measure as printed to 0.01°: one that rounds to 0 is 0,
 *        not -0.00.
 * @param r Run state.
 * @param k The measure.
 * @return The angle, °.
 */
static double verify_deg(const servo_rauto_t *r, unsigned k)
{
    const double v = (double)r->vdeg[k];
    return fabs(v) < 0.005 ? 0.0 : v;
}

/**
 * @brief The verify pass: one line a measure taken, or why there is none.
 * @param r Run state.
 * @param c Cursor.
 * @return true when the line asked for is one of them.
 */
static bool verify_lines(const servo_rauto_t *r, cur_t *c)
{
    if (blank(c) || text_line(c, SERVO_STR_RA_R_VERIFY_HEAD)) {
        return true;
    }
    if (!r->programmed) {
        return text_line(c, SERVO_STR_RA_R_VERIFY_NO_WRITE);
    }
    if (r->vn == 0u) {
        return text_line(c, SERVO_STR_RA_R_VERIFY_NONE);
    }
    for (unsigned k = 0; k < r->vn; ++k) {
        if (here(c)) {
            snprintf(c->buf, c->n, LS(RA_R_VERIFY), verify_us(r, k),
                     verify_deg(r, k), (double)verify_goal(r, k),
                     r->vwithin[k] ? LS(RA_R_IN) : LS(RA_R_OUT));
            return true;
        }
    }
    return false;
}

bool servo_rauto_report_line(const servo_rauto_t *r, unsigned idx, char *buf,
                             size_t n)
{
    if (r == NULL || buf == NULL || n == 0u
        || r->state != SERVO_RAUTO_DONE) {
        return false;
    }
    cur_t c = { idx, 0u, buf, n, r->cfg.text };
    return head_lines(r, &c) || prog_lines(r, &c) || search_lines(r, &c)
        || step_lines(r, &c) || verify_lines(r, &c) || blank(&c)
        || text_line(&c, SERVO_STR_RA_R_MODEL);
}

static bool csv_line(const servo_rauto_t *r, unsigned idx, char *buf,
                     size_t n)
{
    if (idx == 0u) {
        snprintf(buf, n, "%s", RAUTO_CSV_HEAD);
        return true;
    }
    if (idx > r->step_n + r->vn) {
        /* The search's positions last, "S" in write, the current in mA in
         * value and the angle in measured. */
        const unsigned k = idx - 1u - r->step_n - r->vn;
        if (k >= r->srow_n) {
            return false;
        }
        const servo_rauto_srow_t *p = &r->srow[k];
        char deg[16] = "";
        if (p->valid) {
            snprintf(deg, sizeof(deg), "%.2f", srow_deg(p));
        }
        snprintf(buf, n, "%u;%u;S;%u;%s;;;;", idx, (unsigned)p->us,
                 (unsigned)p->ma, deg);
        return true;
    }
    if (idx > r->step_n) {
        /* The verify pass's measures after the steps, "V" in write. */
        const unsigned k = idx - 1u - r->step_n;
        char mm[16] = "", gm[16] = "";
        if (r->cfg.len_mm > 0.0f) {
            snprintf(mm, sizeof(mm), "%.2f",
                     (double)mm_of((float)verify_deg(r, k), r->cfg.len_mm));
            snprintf(gm, sizeof(gm), "%.2f",
                     (double)mm_of(verify_goal(r, k), r->cfg.len_mm));
        }
        snprintf(buf, n, "%u;%u;V;;%.2f;%.2f;%s;%s;%u", idx,
                 verify_us(r, k), verify_deg(r, k),
                 (double)verify_goal(r, k), mm, gm,
                 r->vwithin[k] ? 1u : 0u);
        return true;
    }
    const servo_rauto_step_t *p = &r->steps[idx - 1u];
    char v[8] = "", mm[16] = "", gm[16] = "";
    if (p->value != 0u) {
        snprintf(v, sizeof(v), "%u",
                 (unsigned)servo_rauto_reg_of(r, p->reg, p->value));
    }
    if (r->cfg.len_mm > 0.0f) {
        snprintf(mm, sizeof(mm), "%.2f",
                 (double)mm_of(p->measured_deg, r->cfg.len_mm));
        snprintf(gm, sizeof(gm), "%.2f",
                 (double)mm_of(p->goal_deg, r->cfg.len_mm));
    }
    snprintf(buf, n, "%u;%u;%u;%s;%.2f;%.2f;%s;%s;%u", idx,
             p->side == 0u ? 1000u : 2000u, (unsigned)p->n, v,
             (double)p->measured_deg, (double)p->goal_deg, mm, gm,
             p->within ? 1u : 0u);
    return true;
}

/* -------------------------------------------------------------- the words */

bool servo_rauto_why_error(servo_rauto_why_t why, servo_test_abort_t test_why)
{
    switch (why) {
    case SERVO_RAUTO_WHY_TEST:  return servo_test_abort_error(test_why);
    case SERVO_RAUTO_WHY_HARD:
    case SERVO_RAUTO_WHY_STALL:
    case SERVO_RAUTO_WHY_BINDS:
    case SERVO_RAUTO_WHY_STOP:
    case SERVO_RAUTO_WHY_ANGLE: return true;
    default:                    return false;
    }
}

servo_str_t servo_rauto_why_str(const servo_rauto_t *r)
{
    if (r == NULL) {
        return SERVO_STR_AB_NONE;
    }
    switch (r->why) {
    case SERVO_RAUTO_WHY_TEST:      return servo_test_abort_str(r->test_why);
    case SERVO_RAUTO_WHY_HARD:      return SERVO_STR_L_WHY_HARD;
    case SERVO_RAUTO_WHY_STALL:     return SERVO_STR_L_WHY_STALL;
    case SERVO_RAUTO_WHY_BINDS:     return SERVO_STR_RA_WHY_BINDS;
    case SERVO_RAUTO_WHY_NOT_STILL: return SERVO_STR_RA_WHY_NOT_STILL;
    case SERVO_RAUTO_WHY_ANGLE:     return SERVO_STR_RA_WHY_ANGLE;
    case SERVO_RAUTO_WHY_NO_SERVO:  return SERVO_STR_RA_WHY_NO_SERVO;
    case SERVO_RAUTO_WHY_PROG:      return SERVO_STR_L_WHY_PROG;
    case SERVO_RAUTO_WHY_WRITE:     return SERVO_STR_L_WHY_WRITE;
    case SERVO_RAUTO_WHY_MAPPING:   return SERVO_STR_RA_WHY_MAPPING;
    case SERVO_RAUTO_WHY_LIMIT:     return SERVO_STR_RA_WHY_LIMIT;
    case SERVO_RAUTO_WHY_WRITE_CAP: return SERVO_STR_RA_WHY_WRITE_CAP;
    case SERVO_RAUTO_WHY_BENCH:     return SERVO_STR_L_WHY_BENCH;
    case SERVO_RAUTO_WHY_SUPPLY:    return SERVO_STR_L_WHY_SUPPLY;
    case SERVO_RAUTO_WHY_CAP:       return SERVO_STR_RA_WHY_CAP;
    case SERVO_RAUTO_WHY_NO_EFFECT: return r->side == 0u
                                               ? SERVO_STR_RA_WHY_NO_EFFECT_LO
                                               : SERVO_STR_RA_WHY_NO_EFFECT_HI;
    case SERVO_RAUTO_WHY_STOP:      return SERVO_STR_RA_WHY_STOP;
    case SERVO_RAUTO_WHY_VERIFY:    return r->vbad == 0u
                                               ? SERVO_STR_RA_WHY_VERIFY_LO
                                           : r->vbad == 1u
                                               ? SERVO_STR_RA_WHY_VERIFY_HI
                                               : SERVO_STR_RA_WHY_VERIFY_MID;
    default:                        return SERVO_STR_AB_NONE;
    }
}

servo_str_t servo_rauto_phase_str(servo_rauto_phase_t ph)
{
    switch (ph) {
    case SERVO_RAUTO_PH_READ:    return SERVO_STR_RA_PH_READ;
    case SERVO_RAUTO_PH_MOVE:    return SERVO_STR_PHASE_MOVE;
    case SERVO_RAUTO_PH_SETTLE:  return SERVO_STR_PHASE_SETTLE;
    case SERVO_RAUTO_PH_SAFE:    return SERVO_STR_L_PH_SAFE;
    case SERVO_RAUTO_PH_CONNECT: return SERVO_STR_L_PH_CONNECT;
    case SERVO_RAUTO_PH_WRITE:   return SERVO_STR_L_PH_WRITE;
    case SERVO_RAUTO_PH_LEAVE:   return SERVO_STR_L_PH_LEAVE;
    case SERVO_RAUTO_PH_ARM:     return SERVO_STR_L_PH_ARM;
    case SERVO_RAUTO_PH_CHECK:   return SERVO_STR_L_PH_CHECK;
    case SERVO_RAUTO_PH_VERIFY:  return SERVO_STR_RA_PH_VERIFY;
    case SERVO_RAUTO_PH_APPROACH: return SERVO_STR_RA_PH_APPROACH;
    case SERVO_RAUTO_PH_SEARCH:  return SERVO_STR_L_PH_SEARCH;
    default:                     return SERVO_STR_PHASE_NONE;
    }
}
