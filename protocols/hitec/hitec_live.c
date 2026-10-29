/**
 * @file
 * @brief Hitec live loop: the D series target mapping, the pass's command,
 *        its judge and FIND STOP.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_live.h"

#include <string.h>

/* --- mapping -------------------------------------------------------------- */

/**
 * @brief True for stored positions the mapping runs through.
 *
 * @param e  The stored positions.
 * @return true when left < mid < right <= HITEC_D_POS_MAX.
 */
static bool ends_ok(const hitec_live_ends_t *e)
{
    return e != NULL && e->left < e->mid && e->mid < e->right
           && e->right <= HITEC_D_POS_MAX;
}

bool hitec_live_d_ends(const hitec_image_t *img, hitec_live_ends_t *out)
{
    static const uint8_t regs[] = { 0xB2u, 0xC2u, 0xB0u, 0x5Eu };

    if (img == NULL || out == NULL || img->series != HITEC_D) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        if (!hitec_image_known(img, regs[i])) {
            return false;
        }
    }
    out->left = hitec_image_reg(img, 0xB2u);
    out->mid = hitec_image_reg(img, 0xC2u);
    out->right = hitec_image_reg(img, 0xB0u);
    return hitec_image_reg(img, 0x5Eu) == 0u && ends_ok(out);
}

/**
 * @brief @p num / @p den, rounded half up; both not negative, @p den not 0.
 *
 * @param num  Numerator.
 * @param den  Denominator.
 * @return The quotient.
 */
static uint32_t div_round(uint32_t num, uint32_t den)
{
    return (num + den / 2u) / den;
}

/**
 * @brief @p v clamped to @p lo to @p hi.
 *
 * @param v   Value.
 * @param lo  Lowest.
 * @param hi  Highest.
 * @return The value.
 */
static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

uint16_t hitec_live_d_pos(const hitec_live_ends_t *e, uint16_t t)
{
    int32_t pos;

    if (!ends_ok(e)) {
        return 0u;
    }
    if (t > HITEC_D_TARGET_MAX) {
        t = HITEC_D_TARGET_MAX;
    }
    if (t < HITEC_LIVE_D_T_MID) {
        pos = (int32_t)e->mid
              - (int32_t)div_round((HITEC_LIVE_D_T_MID - t)
                                       * (uint32_t)(e->mid - e->left),
                                   HITEC_LIVE_D_T_HALF);
    } else {
        pos = (int32_t)e->mid
              + (int32_t)div_round((t - HITEC_LIVE_D_T_MID)
                                       * (uint32_t)(e->right - e->mid),
                                   HITEC_LIVE_D_T_HALF);
    }
    return (uint16_t)clamp(pos, 0, (int32_t)HITEC_D_POS_MAX);
}

uint16_t hitec_live_d_target(const hitec_live_ends_t *e, uint16_t pos)
{
    int32_t t;

    if (!ends_ok(e)) {
        return HITEC_LIVE_D_T_MID;
    }
    if (pos < e->mid) {
        t = (int32_t)HITEC_LIVE_D_T_MID
            - (int32_t)div_round((uint32_t)(e->mid - pos)
                                     * HITEC_LIVE_D_T_HALF,
                                 (uint32_t)(e->mid - e->left));
    } else {
        t = (int32_t)HITEC_LIVE_D_T_MID
            + (int32_t)div_round((uint32_t)(pos - e->mid)
                                     * HITEC_LIVE_D_T_HALF,
                                 (uint32_t)(e->right - e->mid));
    }
    return (uint16_t)clamp(t, 0, (int32_t)HITEC_D_TARGET_MAX);
}

void hitec_live_d_reach(const hitec_live_ends_t *e, uint16_t *lo,
                        uint16_t *hi)
{
    *lo = hitec_live_d_pos(e, 0u);
    *hi = hitec_live_d_pos(e, HITEC_D_TARGET_MAX);
}

uint16_t hitec_live_d_us_target(uint16_t us)
{
    const int32_t t = ((int32_t)us - (int32_t)HITEC_LIVE_D_US_ZERO)
                      * (int32_t)HITEC_LIVE_D_T_PER_US;

    return (uint16_t)clamp(t, 0, (int32_t)HITEC_D_TARGET_MAX);
}

uint16_t hitec_live_d_share_pm(uint16_t us)
{
    const uint16_t t = hitec_live_d_us_target(us);
    const uint32_t d = t < HITEC_LIVE_D_T_MID ? HITEC_LIVE_D_T_MID - t
                                              : t - HITEC_LIVE_D_T_MID;

    return (uint16_t)div_round(d * 1000u, HITEC_LIVE_D_T_HALF);
}

/* --- the pass ------------------------------------------------------------- */

uint16_t hitec_live_slew(uint16_t from, uint16_t to)
{
    if (to > from && (unsigned)(to - from) > HITEC_LIVE_SLEW) {
        return (uint16_t)(from + HITEC_LIVE_SLEW);
    }
    if (from > to && (unsigned)(from - to) > HITEC_LIVE_SLEW) {
        return (uint16_t)(from - HITEC_LIVE_SLEW);
    }
    return to;
}

bool hitec_live_d_command(const hitec_live_ends_t *e, uint16_t last_p,
                          uint16_t want, hitec_live_cmd_t *out)
{
    uint16_t w = want;

    if (!ends_ok(e) || out == NULL) {
        return false;
    }
    hitec_live_d_reach(e, &out->lo, &out->hi);
    if (w < out->lo) {
        w = out->lo;
    } else if (w > out->hi) {
        w = out->hi;
    }
    out->clamped = w != want ? 1u : 0u;
    out->p = hitec_live_slew(last_p, w);
    out->target = hitec_live_d_target(e, out->p);
    return true;
}

uint16_t hitec_live_stall_ma(uint16_t limit_ma)
{
    return (uint16_t)((uint32_t)limit_ma * HITEC_LIVE_STALL_PCT / 100u);
}

void hitec_live_judge_init(hitec_live_judge_t *j, uint16_t mid,
                           uint16_t start_p, uint16_t limit_ma)
{
    if (j == NULL) {
        return;
    }
    memset(j, 0, sizeof(*j));
    j->mid = mid;
    j->stall_ma = hitec_live_stall_ma(limit_ma);
    j->last_p = start_p;
    j->last_free = start_p;
}

/**
 * @brief |@p a - @p b|.
 *
 * @param a  Value.
 * @param b  Value.
 * @return The distance.
 */
static uint16_t dist(uint16_t a, uint16_t b)
{
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

/**
 * @brief The lag judge: counts the pass toward a bind or starts again.
 *
 * @param j  Judge.
 * @param o  The pass.
 * @return true when the count binds.
 */
static bool lag_binds(hitec_live_judge_t *j, const hitec_live_obs_t *o)
{
    const bool outward = dist(o->p, j->mid) >= dist(j->last_p, j->mid);

    if (dist(o->pos, o->p) > HITEC_LIVE_LAG_BIND && outward) {
        if (j->over < HITEC_LIVE_LAG_PASSES) {
            ++j->over;
        }
    } else {
        j->over = 0;
    }
    return j->over >= HITEC_LIVE_LAG_PASSES;
}

/**
 * @brief The current judge: a run at or above the stall level.
 *
 * @param j  Judge.
 * @param o  The pass.
 * @return true when the run has lasted HITEC_LIVE_STALL_MS.
 */
static bool current_binds(hitec_live_judge_t *j, const hitec_live_obs_t *o)
{
    if (o->has_ma == 0u) {
        return j->i_on != 0u && o->now_ms - j->t_over >= HITEC_LIVE_STALL_MS;
    }
    if (o->ma < j->stall_ma) {
        j->i_on = 0;
        return false;
    }
    if (j->i_on == 0u) {
        j->i_on = 1;
        j->t_over = o->now_ms;
    }
    return o->now_ms - j->t_over >= HITEC_LIVE_STALL_MS;
}

/**
 * @brief The AS5600 judge.
 *
 * @param j  Judge.
 * @param o  The pass.
 * @return true when the angle moved less than half of the change.
 */
static bool enc_binds(hitec_live_judge_t *j, const hitec_live_obs_t *o)
{
    const uint32_t a = o->angle % HITEC_LIVE_ENC_COUNTS;
    uint32_t cmd_cdeg;
    uint32_t ang_cdeg;
    uint32_t da;

    if (o->has_angle == 0u) {
        j->enc_on = 0;
        return false;
    }
    if (j->enc_on == 0u) {
        j->enc_on = 1;
        j->enc_p = o->p;
        j->enc_a = (uint16_t)a;
        return false;
    }
    cmd_cdeg = (uint32_t)dist(j->last_p, j->enc_p) * HITEC_LIVE_D_SPAN_CDEG
               / HITEC_D_POS_MAX;
    if (cmd_cdeg < HITEC_LIVE_ENC_MIN_CDEG) {
        return false;
    }
    /* The shorter way round the turn. */
    da = (a + HITEC_LIVE_ENC_COUNTS - j->enc_a) % HITEC_LIVE_ENC_COUNTS;
    if (da > HITEC_LIVE_ENC_COUNTS / 2u) {
        da = HITEC_LIVE_ENC_COUNTS - da;
    }
    ang_cdeg = da * 36000u / HITEC_LIVE_ENC_COUNTS;
    if (ang_cdeg * 2u < cmd_cdeg) {
        return true;
    }
    j->enc_p = o->p;
    j->enc_a = (uint16_t)a;
    return false;
}

hitec_live_verdict_t hitec_live_judge(hitec_live_judge_t *j,
                                      const hitec_live_obs_t *o)
{
    hitec_live_verdict_t v = HITEC_LIVE_FOLLOWING;
    bool lag;
    bool cur;
    bool enc;

    if (j == NULL || o == NULL) {
        return HITEC_LIVE_FOLLOWING;
    }
    /* Every judge sees every pass, whichever binds first. */
    lag = lag_binds(j, o);
    cur = current_binds(j, o);
    enc = enc_binds(j, o);
    if (lag) {
        v = HITEC_LIVE_BOUND_LAG;
    } else if (cur) {
        v = HITEC_LIVE_BOUND_CURRENT;
    } else if (enc) {
        v = HITEC_LIVE_BOUND_ENC;
    } else if (dist(o->pos, o->p) <= HITEC_LIVE_LAG_FREE) {
        v = HITEC_LIVE_FREE;
        j->last_free = o->pos;
    }
    j->last_p = o->p;
    return v;
}

uint16_t hitec_live_back(const hitec_live_judge_t *j)
{
    if (j == NULL) {
        return 0u;
    }
    if (dist(j->last_free, j->mid) <= HITEC_LIVE_BACK) {
        return j->mid;
    }
    return j->last_free < j->mid
               ? (uint16_t)(j->last_free + HITEC_LIVE_BACK)
               : (uint16_t)(j->last_free - HITEC_LIVE_BACK);
}

/* --- FIND STOP ------------------------------------------------------------ */

bool hitec_live_find_start(hitec_live_find_t *f, const hitec_live_ends_t *e,
                           uint8_t side, uint16_t limit_ma, uint32_t now_ms)
{
    uint16_t lo;
    uint16_t hi;

    if (f == NULL || !ends_ok(e) || side > HITEC_FIND_RIGHT) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    hitec_live_d_reach(e, &lo, &hi);
    hitec_live_judge_init(&f->j, e->mid, e->mid, limit_ma);
    f->t0 = now_ms;
    f->p = e->mid;
    f->edge = side == HITEC_FIND_LEFT ? lo : hi;
    f->side = side;
    f->state = HITEC_FIND_RUN;
    return true;
}

/**
 * @brief The next position outward from @p p, up to @p edge.
 *
 * @param f  Search.
 * @return The position.
 */
static uint16_t find_step(const hitec_live_find_t *f)
{
    if (f->side == HITEC_FIND_LEFT) {
        return (unsigned)(f->p - f->edge) > HITEC_LIVE_FIND_STEP
                   ? (uint16_t)(f->p - HITEC_LIVE_FIND_STEP)
                   : f->edge;
    }
    return (unsigned)(f->edge - f->p) > HITEC_LIVE_FIND_STEP
               ? (uint16_t)(f->p + HITEC_LIVE_FIND_STEP)
               : f->edge;
}

uint8_t hitec_live_find_pass(hitec_live_find_t *f, const hitec_live_obs_t *o)
{
    hitec_live_obs_t pass;
    hitec_live_verdict_t v;

    if (f == NULL || o == NULL) {
        return HITEC_FIND_ABORTED;
    }
    if (f->state != HITEC_FIND_RUN) {
        return f->state;
    }
    pass = *o;
    pass.p = f->p;
    v = hitec_live_judge(&f->j, &pass);
    if (v >= HITEC_LIVE_BOUND_LAG) {
        f->stop = f->j.last_free;
        f->end = hitec_live_back(&f->j);
        f->why = (uint8_t)v;
        f->state = HITEC_FIND_FOUND;
        return f->state;
    }
    if (++f->passes >= HITEC_LIVE_FIND_PASSES) {
        if (f->p == f->edge) {
            f->stop = f->edge;
            f->state = HITEC_FIND_NONE;
            return f->state;
        }
        f->p = find_step(f);
        f->passes = 0;
    }
    if (o->now_ms - f->t0 >= HITEC_LIVE_FIND_MS) {
        f->state = HITEC_FIND_TIMEOUT;
    }
    return f->state;
}

void hitec_live_find_abort(hitec_live_find_t *f)
{
    if (f != NULL && f->state == HITEC_FIND_RUN) {
        f->state = HITEC_FIND_ABORTED;
    }
}
