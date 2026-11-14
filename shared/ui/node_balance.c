/**
 * @file
 * @brief The balance node's readings, captures and solution on the panel
 *        (node_balance.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "node_balance.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "balance_screen.h"
#include "settings.h"
#include "ui_text.h"

/** @brief Degrees to radians. */
#define NB_RAD (3.14159265358979f / 180.0f)

#define NB_GOOD_PCT 90u  /**< GOOD from this quality, % */
#define NB_FAIR_PCT 70u  /**< FAIR from this quality, % */

nb_quality_t nb_quality(uint16_t flags, uint8_t quality)
{
    if ((flags & RCNODE_BF_DC_OK) == 0u) {
        return NB_Q_SENSOR;
    }
    if ((flags & RCNODE_BF_CLIPPED) != 0u) {
        return NB_Q_CLIPPED;
    }
    if ((flags & RCNODE_BF_RPM_RANGE) != 0u) {
        return NB_Q_RPM_RANGE;
    }
    if ((flags & RCNODE_BF_INDEX_LOST) != 0u) {
        return NB_Q_INDEX_LOST;
    }
    if ((flags & RCNODE_BF_VALID) == 0u) {
        return (flags & RCNODE_BF_NO_INDEX) != 0u ? NB_Q_NO_INDEX
                                                  : NB_Q_NO_REV;
    }
    if ((flags & RCNODE_BF_QUALITY) == 0u) {
        return NB_Q_SETTLING;
    }
    if ((flags & RCNODE_BF_NO_INDEX) != 0u) {
        return NB_Q_NO_PHASE;
    }
    if (quality >= NB_GOOD_PCT) {
        return NB_Q_GOOD;
    }
    return quality >= NB_FAIR_PCT ? NB_Q_FAIR : NB_Q_POOR;
}

const char *nb_quality_text(nb_quality_t q)
{
    switch (q) {
    case NB_Q_SENSOR:     return TR(NB_Q_SENSOR);
    case NB_Q_CLIPPED:    return TR(NB_Q_CLIPPED);
    case NB_Q_RPM_RANGE:  return TR(NB_Q_RPM_RANGE);
    case NB_Q_INDEX_LOST: return TR(NB_Q_INDEX_LOST);
    case NB_Q_NO_INDEX:   return TR(NB_Q_NO_INDEX);
    case NB_Q_NO_REV:     return TR(NB_Q_NO_REV);
    case NB_Q_SETTLING:   return TR(NB_Q_SETTLING);
    case NB_Q_NO_PHASE:   return TR(NB_Q_NO_PHASE);
    case NB_Q_GOOD:       return TR(NB_Q_GOOD);
    case NB_Q_FAIR:       return TR(NB_Q_FAIR);
    case NB_Q_POOR:       return TR(NB_Q_POOR);
    case NB_Q_NONE:
    case NB_Q_COUNT:
    default:              return "--";
    }
}

/**
 * @brief Whether a report is fresh: within BAL_STALE_MS of its arrival.
 * @param present Whether a node is on and bound.
 * @param r       The report, NULL none.
 * @param age_ms  Its age.
 * @return true when it may be shown.
 */
static bool fresh(bool present, const rcnode_bal_report_t *r, uint32_t age_ms)
{
    return present && r != NULL && age_ms < BAL_STALE_MS;
}

void nb_read(bool present, const rcnode_bal_report_t *r, uint32_t age_ms,
             nb_reading_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!present) {
        out->live = NB_LIVE_SIMULATED;
        return;
    }
    if (!fresh(present, r, age_ms)) {
        out->live = NB_LIVE_STALE;
        return;
    }
    out->live = NB_LIVE_FRESH;
    out->word = (uint8_t)nb_quality(r->flags, r->quality);
    out->counts = (r->flags & RCNODE_BF_COUNTS) != 0u;
    const bool valid = (r->flags & RCNODE_BF_VALID) != 0u;
    out->valid = valid;
    out->phase = valid && (r->flags & RCNODE_BF_NO_INDEX) == 0u;
    out->mag = out->counts ? (float)r->mag_1x : (float)r->mag_1x / 1000.0f;
    out->deg = (float)(r->phase_1x % RCNODE_PHASE_STEPS) / 10.0f;
    out->rpm = valid ? r->rpm : 0u;
    out->quality = r->quality > 100u ? 100u : r->quality;
}

const char *nb_fmt_mag(const nb_reading_t *r, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return "";
    }
    if (r == NULL || r->live != NB_LIVE_FRESH || !r->valid) {
        snprintf(out, n, "--");
    } else if (r->counts) {
        snprintf(out, n, TR(NB_COUNTS), (unsigned)r->mag);
    } else {
        snprintf(out, n, "%.2fg", (double)r->mag);
    }
    return out;
}

const char *nb_fmt_deg(const nb_reading_t *r, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return "";
    }
    if (r == NULL || r->live != NB_LIVE_FRESH || !r->phase) {
        snprintf(out, n, "--");
    } else {
        snprintf(out, n, "%d°", (int)(r->deg + 0.5f) % 360);
    }
    return out;
}

const char *nb_fmt_rpm(const nb_reading_t *r, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return "";
    }
    if (r == NULL || r->live != NB_LIVE_FRESH || r->rpm == 0u) {
        snprintf(out, n, "--");
    } else {
        snprintf(out, n, "%urpm", (unsigned)r->rpm);
    }
    return out;
}

/* ------------------------------------------------------------ captures */

void nb_init(nb_session_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
        s->method = NB_TWO_RUNS;
        s->taking = -1;
    }
}

uint8_t nb_slots(uint8_t m)
{
    switch (m) {
    case NB_TWO_RUNS:  return 2u;
    case NB_FOUR_RUNS: return 4u;
    default:           return 0u;
    }
}

void nb_clear(nb_session_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s->cap, 0, sizeof(s->cap));
    s->taking = -1;
    ++s->rev;
}

void nb_set_method(nb_session_t *s, uint8_t m)
{
    if (s == NULL || m >= NB_METHOD_COUNT || m == s->method) {
        return;
    }
    s->method = m;
    nb_clear(s);
}

nb_take_t nb_take(nb_session_t *s, uint8_t slot, bool is_fresh)
{
    if (s == NULL || slot >= nb_slots(s->method)) {
        return NB_TAKE_SLOT;
    }
    if (s->taking >= 0) {
        return NB_TAKE_BUSY;
    }
    if (!is_fresh) {
        return NB_TAKE_NO_DATA;
    }
    memset(&s->cap[slot], 0, sizeof(s->cap[slot]));
    s->cap[slot].state = NB_CAP_TAKING;
    s->taking = (int8_t)slot;
    ++s->rev;
    return NB_TAKE_STARTED;
}

/**
 * @brief End the running capture.
 * @param s    The session.
 * @param why  NB_FAIL_NONE for DONE, else the reason.
 */
static void finish(nb_session_t *s, nb_fail_t why)
{
    nb_cap_t *c = &s->cap[s->taking];
    if (why != NB_FAIL_NONE) {
        c->state = NB_CAP_FAILED;
        c->fail = (uint8_t)why;
    } else {
        const float k = 1.0f / (float)c->n;
        c->vec.mag = sqrtf(c->sx * c->sx + c->sy * c->sy) * k;
        c->vec.deg = vibe_wrap_deg(atan2f(c->sy, c->sx) / NB_RAD);
        c->mag = c->smag * k;
        c->rpm = c->srpm * k;
        c->state = NB_CAP_DONE;
    }
    s->taking = -1;
    ++s->rev;
}

/**
 * @brief One new report into the running capture.
 * @param s The session.
 * @param r The report.
 */
static void take_report(nb_session_t *s, const rcnode_bal_report_t *r)
{
    nb_cap_t *c = &s->cap[s->taking];
    if ((r->flags & RCNODE_BF_VALID) == 0u) {
        finish(s, NB_FAIL_NO_REV);
        return;
    }
    if (s->method == NB_TWO_RUNS && (r->flags & RCNODE_BF_NO_INDEX) != 0u) {
        finish(s, NB_FAIL_NO_PHASE);
        return;
    }
    if (c->n == 0u) {
        c->rpm0 = r->rpm;
        c->counts = (r->flags & RCNODE_BF_COUNTS) != 0u;
    } else {
        const uint32_t d = r->rpm > c->rpm0 ? (uint32_t)(r->rpm - c->rpm0)
                                            : (uint32_t)(c->rpm0 - r->rpm);
        if (d * 100u > (uint32_t)c->rpm0 * NB_RPM_DRIFT_PCT) {
            finish(s, NB_FAIL_RPM);
            return;
        }
    }
    const float mag = c->counts ? (float)r->mag_1x
                                : (float)r->mag_1x / 1000.0f;
    const float a = (float)(r->phase_1x % RCNODE_PHASE_STEPS) / 10.0f
                    * NB_RAD;
    c->sx += mag * cosf(a);
    c->sy += mag * sinf(a);
    c->smag += mag;
    c->srpm += (float)r->rpm;
    ++c->n;
    ++s->rev;
    if (c->n >= NB_CAP_REPORTS) {
        finish(s, NB_FAIL_NONE);
    }
}

void nb_feed(nb_session_t *s, bool present, const rcnode_bal_report_t *r,
             uint32_t age_ms)
{
    if (s == NULL) {
        return;
    }
    const bool ok = fresh(present, r, age_ms);
    bool is_new = false;
    if (r != NULL) {
        is_new = !s->have_last || r->hdr.seq != s->last_seq
                 || r->hdr.t_node != s->last_t;
        s->have_last = true;
        s->last_seq = r->hdr.seq;
        s->last_t = r->hdr.t_node;
    }
    if (s->taking < 0) {
        return;
    }
    if (!ok) {
        finish(s, NB_FAIL_STALE);
    } else if (is_new) {
        take_report(s, r);
    }
}

bool nb_ready(const nb_session_t *s)
{
    if (s == NULL || s->taking >= 0) {
        return false;
    }
    const uint8_t n = nb_slots(s->method);
    for (uint8_t i = 0; i < n; ++i) {
        if (s->cap[i].state != NB_CAP_DONE) {
            return false;
        }
    }
    return n > 0u;
}

bool nb_solve(const nb_session_t *s, float trial_g, vibe_solution_t *out)
{
    if (out == NULL || !nb_ready(s)) {
        return false;
    }
    if (s->method == NB_TWO_RUNS) {
        (void)vibe_solve_two_run(s->cap[0].vec, s->cap[1].vec, trial_g,
                                 0.0f, out);
    } else {
        (void)vibe_solve_four_run(s->cap[0].mag, s->cap[1].mag,
                                  s->cap[2].mag, s->cap[3].mag, trial_g,
                                  out);
    }
    return true;
}

const char *nb_csv_header(void)
{
    return "method,trial_g,unit,"
           "run0_mag,run0_deg,run0_rpm,run1_mag,run1_deg,run1_rpm,"
           "run2_mag,run2_rpm,run3_mag,run3_rpm,"
           "status,add_g,where_deg,blades,blade_lo,blade_hi\n";
}

/** @brief The status words of the CSV, by vibe_solve_status_t. */
static const char *const k_status[] = {
    "OK", "WEAK", "NO_EFFECT", "INCONSISTENT", "BAD_INPUT",
};

size_t nb_csv_row(const nb_session_t *s, float trial_g,
                  const vibe_solution_t *sol, int blades, char *out,
                  size_t n)
{
    if (out == NULL || n == 0u || sol == NULL || !nb_ready(s)) {
        return 0u;
    }
    const nb_cap_t *c = s->cap;
    const bool two = s->method == NB_TWO_RUNS;
    const bool corr = sol->status == VIBE_SOLVE_OK
                      || sol->status == VIBE_SOLVE_WEAK;
    int lo = 0, hi = 0;
    if (corr) {
        balance_nearest_blades(sol->deg, blades, &lo, &hi);
    }
    const unsigned st = (unsigned)sol->status
                                < sizeof(k_status) / sizeof(k_status[0])
                            ? (unsigned)sol->status : 4u;
    int len;
    if (two) {
        len = snprintf(out, n,
                       "two,%.2f,%s,%.3f,%.1f,%.0f,%.3f,%.1f,%.0f,,,,,"
                       "%s,%.2f,%.1f,%d,%d,%d\n",
                       (double)trial_g, c[0].counts ? "counts" : "g",
                       (double)c[0].vec.mag, (double)c[0].vec.deg,
                       (double)c[0].rpm, (double)c[1].vec.mag,
                       (double)c[1].vec.deg, (double)c[1].rpm,
                       k_status[st], corr ? (double)sol->mass : 0.0,
                       corr ? (double)sol->deg : 0.0, blades, lo, hi);
    } else {
        len = snprintf(out, n,
                       "four,%.2f,%s,%.3f,,%.0f,%.3f,,%.0f,%.3f,%.0f,"
                       "%.3f,%.0f,%s,%.2f,%.1f,%d,%d,%d\n",
                       (double)trial_g, c[0].counts ? "counts" : "g",
                       (double)c[0].mag, (double)c[0].rpm,
                       (double)c[1].mag, (double)c[1].rpm,
                       (double)c[2].mag, (double)c[2].rpm,
                       (double)c[3].mag, (double)c[3].rpm,
                       k_status[st], corr ? (double)sol->mass : 0.0,
                       corr ? (double)sol->deg : 0.0, blades, lo, hi);
    }
    if (len <= 0 || (size_t)len >= n) {
        out[0] = '\0';
        return 0u;
    }
    return (size_t)len;
}

/* ------------------------------------------- the panel's one instance */

/** @brief The panel's instance: UI task only. */
static struct {
    nb_reading_t    live;       /**< the reading shown */
    nb_session_t    ses;        /**< the captures */
    bool            init;       /**< ses was set up */
    uint32_t        rev;        /**< the reading's own revision */
    uint32_t        res_rev;    /**< the solution's and the row's revision */
    bool            solved;     /**< sol holds the ready captures' solution */
    vibe_solution_t sol;        /**< that solution */
    float           trial_g;    /**< its trial mass */
    bool            csv_due;    /**< its CSV row is not taken yet */
    uint8_t         csv;        /**< nb_csv_t of the last row taken */
    uint32_t        csv_seq;    /**< that row's number */
} s_nb; /**< the panel's instance: UI task only */

/** @brief Set up on first use. */
static void nb_once(void)
{
    if (!s_nb.init) {
        nb_init(&s_nb.ses);
        s_nb.init = true;
    }
}

void node_balance_reset(void)
{
    memset(&s_nb, 0, sizeof(s_nb));
    nb_once();
}

/**
 * @brief Whether two readings show the same.
 * @param a One.  @param b The other.
 * @return true when every member is equal.
 */
static bool same_reading(const nb_reading_t *a, const nb_reading_t *b)
{
    return a->live == b->live && a->word == b->word && a->counts == b->counts
           && a->valid == b->valid && a->phase == b->phase
           && a->mag == b->mag && a->deg == b->deg && a->rpm == b->rpm
           && a->quality == b->quality;
}

/**
 * @brief The solution kept in step with the captures and the trial mass:
 *        solved again, with a new CSV row due, when the captures became
 *        ready or SET_BAL_TRIAL differs from the solution's trial mass;
 *        dropped when the captures are no longer ready.
 */
static void refresh(void)
{
    nb_once();
    const bool ready = nb_ready(&s_nb.ses);
    const float trial = settings_get(SET_BAL_TRIAL);
    if (ready && (!s_nb.solved || trial != s_nb.trial_g)) {
        s_nb.trial_g = trial;
        s_nb.solved = nb_solve(&s_nb.ses, trial, &s_nb.sol);
        s_nb.csv_due = s_nb.solved;
        s_nb.csv = NB_CSV_NONE;
        ++s_nb.res_rev;
    } else if (!ready && s_nb.solved) {
        s_nb.solved = false;
        s_nb.csv_due = false;
        s_nb.csv = NB_CSV_NONE;
        ++s_nb.res_rev;
    }
}

void node_balance_feed(bool present, const rcnode_bal_report_t *r,
                       uint32_t age_ms)
{
    nb_once();
    nb_reading_t next;
    nb_read(present, r, age_ms, &next);
    if (!same_reading(&next, &s_nb.live)) {
        s_nb.live = next;
        ++s_nb.rev;
    }
    nb_feed(&s_nb.ses, present, r, age_ms);
    refresh();
}

const nb_reading_t *node_balance_live(void)
{
    return &s_nb.live;
}

nb_session_t *node_balance_session(void)
{
    nb_once();
    return &s_nb.ses;
}

uint32_t node_balance_rev(void)
{
    return s_nb.rev + s_nb.ses.rev + s_nb.res_rev;
}

uint32_t node_balance_result_rev(void)
{
    return s_nb.ses.rev + s_nb.res_rev;
}

bool node_balance_result(vibe_solution_t *out, float *trial_g)
{
    /* A run taken again, CLEAR or a new trial mass since the last frame. */
    refresh();
    if (!s_nb.solved) {
        return false;
    }
    if (out != NULL) {
        *out = s_nb.sol;
    }
    if (trial_g != NULL) {
        *trial_g = s_nb.trial_g;
    }
    return true;
}

size_t node_balance_take_csv(int blades, char *out, size_t n)
{
    refresh();
    if (!s_nb.csv_due) {
        return 0u;
    }
    const size_t len = nb_csv_row(&s_nb.ses, s_nb.trial_g, &s_nb.sol,
                                  blades, out, n);
    if (len > 0u) {
        s_nb.csv_due = false;
        s_nb.csv = NB_CSV_WAITING;
        ++s_nb.csv_seq;
        ++s_nb.res_rev;
    }
    return len;
}

uint32_t node_balance_csv_seq(void)
{
    return s_nb.csv_seq;
}

void node_balance_csv_done(uint32_t seq, bool ok)
{
    if (seq != s_nb.csv_seq || s_nb.csv != NB_CSV_WAITING) {
        return;
    }
    s_nb.csv = ok ? NB_CSV_WRITTEN : NB_CSV_FAILED;
    ++s_nb.res_rev;
}

nb_csv_t node_balance_csv_state(void)
{
    return (nb_csv_t)s_nb.csv;
}
