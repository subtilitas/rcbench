/**
 * @file
 * @brief The power node's readings on the panel (pnode_meter.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "pnode_meter.h"

#include <string.h>

/** @brief The INA228's shunt full scales, nV (datasheet table 7-9). @{ */
#define FS_NARROW_NV 40960000u   /**< ±40.96mV, ADCRANGE 1 */
#define FS_WIDE_NV   163840000u  /**< ±163.84mV, ADCRANGE 0 */
/** @} */

_Static_assert(PNODE_STALE_MS == SERVO_GAP_MAX_MS,
               "a reading is stale when a dropout would end the run");
_Static_assert(PNODE_GAP_OPEN_MS < SERVO_GAP_MAX_MS,
               "a dropout opens before it is too long");

void pnode_meter_init(pnode_meter_t *m)
{
    if (m != NULL) {
        memset(m, 0, sizeof(*m));
        servo_gap_init(&m->gap);
    }
}

void pnode_meter_link(pnode_meter_t *m)
{
    if (m != NULL) {
        m->have_seq = false;
        m->cal_ok = false;
        /* Within a run the node may have started again since the RUN
         * write: its next RUN packet says. */
        m->tag_check = m->live && m->r.run && m->tag != 0u;
    }
}

bool pnode_meter_info(pnode_meter_t *m, const rcnode_info_t *i)
{
    if (m == NULL || i == NULL) {
        return false;
    }
    m->cal_ok = false;
    if (i->role != RCNODE_ROLE_POWER || i->adcrange > 1u
        || i->shunt_uohm == 0u) {
        return false;
    }
    /* The range's full scale over the shunt, nV over µΩ, is mA: the
     * maximum current that gives that range, if the driver agrees. */
    const uint32_t fs_nv = (i->adcrange != 0u) ? FS_NARROW_NV : FS_WIDE_NV;
    const uint32_t max_ma = fs_nv / i->shunt_uohm;
    ina228_cal_t cal;
    if (ina228_calibrate(i->shunt_uohm, max_ma, &cal) != INA228_SETUP_OK
        || cal.adcrange != i->adcrange) {
        return false;
    }
    m->cal = cal;
    m->fs_ua = (int32_t)(((uint64_t)fs_nv * 1000u) / i->shunt_uohm);
    m->cal_ok = true;
    return true;
}

/**
 * @brief The run's totals in µC and mJ from the steps counted.
 * @param m The meter.
 */
static void totals_convert(pnode_meter_t *m)
{
    /* Both fit 40 bits by far: 2^39 steps of 390.625µC are 59,000Ah. */
    m->r.charge_uc =
        ina228_charge_uc(&m->cal, (uint64_t)m->q_steps & RCNODE_ACC_MASK);
    m->r.energy_mj =
        ina228_energy_mj(&m->cal, (uint64_t)m->e_steps & RCNODE_ACC_MASK);
}

/**
 * @brief A dropout that ends the run: counted once a run.
 * @param m The meter.
 */
static void gap_error(pnode_meter_t *m)
{
    if (!m->ended) {
        m->ended = true;
        ++m->errors;
    }
}

/**
 * @brief One window: a reading when the INA228 is online and it holds a
 *        sample; the run's peaks; the open dropout closed.
 * @param m      The meter.
 * @param w      The window.
 * @param now_ms Its arrival.
 */
static void take_win(pnode_meter_t *m, const rcnode_pwr_win_t *w,
                     uint32_t now_ms)
{
    ++m->windows;
    m->have_any = true;
    m->any_ms = now_ms;
    if ((w->flags & RCNODE_WF_ONLINE) == 0u || w->n == 0u) {
        return;
    }
    pnode_reading_t *r = &m->r;
    const bool clip_hi = (w->flags & RCNODE_WF_CLIP_HIGH) != 0u;
    const bool clip_lo = (w->flags & RCNODE_WF_CLIP_LOW) != 0u;
    bool i_ok = true;
    int32_t i_ua = w->i_mean;
    if (w->i_max == RCNODE_I_NONE) {
        /* Every current sample at an end: that end, a bound. */
        i_ok = clip_hi || clip_lo;
        i_ua = clip_hi ? m->fs_ua : -m->fs_ua;
    }
    ++r->win_n;
    ++r->v_n;
    r->v_sum += w->v_mean;
    r->last_v = true;
    r->last_v_steps = w->v_mean;
    r->last_i = i_ok;
    r->last_i_ua = i_ok ? i_ua : 0;
    r->last_clip = clip_hi || clip_lo;
    if (i_ok) {
        ++r->i_n;
        r->i_sum += i_ua;
    }
    if (r->last_clip) {
        ++r->clip_n;
    }
    if (r->run) {
        const int32_t hi = clip_hi ? m->fs_ua : w->i_max;
        if (hi != RCNODE_I_NONE && (!r->i_max_ok || hi > r->i_max_ua)) {
            r->i_max_ua = hi;
            r->i_max_ok = true;
        }
        if (w->v_min != RCNODE_V_NONE
            && (!r->v_min_ok || w->v_min < r->v_min_steps)) {
            r->v_min_steps = w->v_min;
            r->v_min_ok = true;
        }
        if (i_ok) {
            /* 2mV steps x µA is 2nW a unit: / 500,000 gives mW. */
            const int64_t p = ((int64_t)w->v_mean * i_ua) / 500000;
            if (!r->p_max_ok || p > r->p_max_mw) {
                r->p_max_mw = p;
                r->p_max_ok = true;
            }
        }
        r->run_clip = r->run_clip || r->last_clip;
    }
    r->have_win = true;
    r->win_ms = now_ms;
    if (m->live && m->rule && !m->ended
        && servo_gap_back(&m->gap, now_ms) == SERVO_GAP_TOO_LONG) {
        gap_error(m);
    }
}

/**
 * @brief The accumulators: a difference to the last packet, or the counts
 *        from 0 after a restart; only with both CHARGE_OK and ENERGY_OK.
 * @param m      The meter.
 * @param a      The packet.
 * @param now_ms Its arrival.
 */
static void take_acc(pnode_meter_t *m, const rcnode_pwr_acc_t *a,
                     uint32_t now_ms)
{
    const uint8_t both = RCNODE_AF_CHARGE_OK | RCNODE_AF_ENERGY_OK;
    if ((a->flags & both) != both) {
        return;
    }
    if (m->have_acc) {
        if (rcnode_acc_restarted(&m->acc, a)) {
            ++m->restarts;
            m->q_steps += rcnode_sext40(a->charge);
            m->e_steps += (int64_t)(a->energy & RCNODE_ACC_MASK);
        } else {
            m->q_steps += rcnode_charge_delta(m->acc.charge, a->charge);
            m->e_steps += (int64_t)rcnode_energy_delta(m->acc.energy,
                                                       a->energy);
        }
    }
    m->acc = *a;
    m->have_acc = true;
    m->r.acc_ms = now_ms;
    if (m->r.totals_ok) {
        totals_convert(m);
    }
}

/**
 * @brief The node's run extremes: merged under this run's tag only.
 * @param m The meter.
 * @param x The packet.
 */
static void take_run(pnode_meter_t *m, const rcnode_pwr_run_t *x)
{
    pnode_reading_t *r = &m->r;
    if (m->tag_check) {
        m->tag_check = false;
        m->tag_wrong = m->live && r->run && x->tag != m->tag;
    }
    if (!r->run || m->tag == 0u || x->tag != m->tag) {
        return;
    }
    if (x->i_max != RCNODE_I_NONE && (!r->i_max_ok || x->i_max > r->i_max_ua)) {
        r->i_max_ua = x->i_max;
        r->i_max_ok = true;
    }
    if (x->v_min != RCNODE_V_NONE
        && (!r->v_min_ok || x->v_min < r->v_min_steps)) {
        r->v_min_steps = x->v_min;
        r->v_min_ok = true;
    }
    if ((x->flags & RCNODE_RF_CLIP) != 0u) {
        r->run_clip = true;
    }
}

uint8_t pnode_meter_packet(pnode_meter_t *m, const uint8_t *b, size_t n,
                           uint32_t now_ms)
{
    if (m == NULL || !m->cal_ok) {
        return 0u;
    }
    rcnode_pkt_t p;
    if (rcnode_decode(b, n, &p) != RCNODE_OK) {
        ++m->bad;
        return 0u;
    }
    const uint8_t t = p.hdr.type;
    if (t == RCNODE_T_PWR_WIN || t == RCNODE_T_PWR_ACC
        || t == RCNODE_T_PWR_RUN) {
        if (m->have_seq) {
            m->lost += rcnode_seq_lost(m->seq, p.hdr.seq);
        }
        m->seq = p.hdr.seq;
        m->have_seq = true;
    }
    switch (t) {
    case RCNODE_T_PWR_WIN: take_win(m, &p.u.win, now_ms); break;
    case RCNODE_T_PWR_ACC: take_acc(m, &p.u.acc, now_ms); break;
    case RCNODE_T_PWR_RUN: take_run(m, &p.u.run);         break;
    case RCNODE_T_STATUS:
        m->status = p.u.status;
        m->have_status = true;
        break;
    default:
        break;
    }
    return t;
}

/**
 * @brief Whether a reading came within PNODE_GAP_OPEN_MS of @p now_ms.
 * @param m The meter.  @param now_ms The clock.
 * @return true when one did.
 */
static bool recent(const pnode_meter_t *m, uint32_t now_ms)
{
    return m->r.have_win && now_ms - m->r.win_ms <= PNODE_GAP_OPEN_MS;
}

/**
 * @brief A switch to ARMED: the peaks and totals start, the next RUN tag.
 * @param m      The meter.
 * @param now_ms The clock.
 */
static void run_start(pnode_meter_t *m, uint32_t now_ms)
{
    pnode_reading_t *r = &m->r;
    r->run = true;
    r->i_max_ok = false;
    r->v_min_ok = false;
    r->p_max_ok = false;
    r->run_clip = false;
    r->charge_uc = 0;
    r->energy_mj = 0u;
    m->q_steps = 0;
    m->e_steps = 0;
    /* From the last accumulators before the arm, when they are recent, or
     * none this run. */
    r->totals_ok = m->have_acc
                   && now_ms - r->acc_ms <= PNODE_ACC_BASE_MS;
    m->tag = (uint8_t)((m->tag % 255u) + 1u);
}

bool pnode_meter_step(pnode_meter_t *m, uint32_t now_ms, bool live,
                      bool src_node, bool bench_run)
{
    if (m == NULL) {
        return false;
    }
    const bool started = live && !m->live;
    if (started) {
        run_start(m, now_ms);
    }
    if (!live) {
        m->tag_check = false;
        m->tag_wrong = false;
    }
    /* Only a run that reads BENCH counts on the node. */
    const bool rule = src_node && bench_run;
    const bool counting = live && rule;
    if (counting && !(m->live && m->rule)) {
        /* The rule starts with the run, or with NODE chosen inside one. */
        servo_gap_init(&m->gap);
        m->run_ms = now_ms;
        m->ended = false;
    }
    m->live = live;
    m->rule = rule;
    if (counting && !m->ended) {
        if (!m->gap.open && !recent(m, now_ms)) {
            /* Dated from the last reading, not before the rule began: a
             * run that starts without a recent reading opens one at its
             * start. */
            uint32_t from = m->r.have_win ? m->r.win_ms : m->run_ms;
            if ((int32_t)(from - m->run_ms) < 0) {
                from = m->run_ms;
            }
            if (servo_gap_lost(&m->gap, from) == SERVO_GAP_TOO_SOON) {
                gap_error(m);
            }
        }
        if (servo_gap_check(&m->gap, now_ms) == SERVO_GAP_TOO_LONG) {
            gap_error(m);
        }
    }
    return started;
}

bool pnode_arm_refused(bool src_node, bool node_on, const pnode_reading_t *r,
                       uint32_t now_ms)
{
    if (!src_node) {
        return false;
    }
    const bool streams = r != NULL && r->have_win
                         && (int32_t)(now_ms - r->win_ms)
                                <= (int32_t)PNODE_GAP_OPEN_MS;
    return !node_on || !streams;
}

void pnode_meter_reading(const pnode_meter_t *m, pnode_reading_t *out)
{
    if (out == NULL) {
        return;
    }
    if (m == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = m->r;
}

/**
 * @brief The mean of a sum over the windows since the last take.
 * @param n_was The count at the last take.  @param n_now The count now.
 * @param s_was The sum at the last take.   @param s_now The sum now.
 * @param out   Receives the mean.
 * @return false when no window came.
 */
static bool mean_since(uint32_t n_was, uint32_t n_now, int64_t s_was,
                       int64_t s_now, float *out)
{
    const uint32_t dn = n_now - n_was;
    if (dn == 0u) {
        return false;
    }
    *out = (float)(s_now - s_was) / (float)dn;
    return true;
}

bool pnode_reading_bench(const pnode_reading_t *was,
                         const pnode_reading_t *now, uint32_t now_ms,
                         bench_state_t *b)
{
    if (now == NULL || b == NULL) {
        return false;
    }
    /* BENCH's own figures are the ESC's while no INA228 is the source. */
    if ((b->flags & (uint16_t)LINK_BN_SENSED) == 0u
        && (b->flags & (uint16_t)LINK_BN_SIMULATED) == 0u) {
        bench_state_set_esc(b, (b->flags & LINK_BN_VOLTAGE_OK) != 0u,
                            b->voltage, (b->flags & LINK_BN_CURRENT_OK) != 0u,
                            b->current, false);
    }
    const uint16_t ok = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                                   | LINK_BN_TOTALS_OK);
    b->flags = (uint16_t)((b->flags & (uint16_t)~ok) | LINK_BN_SENSED);
    b->from_node = true;
    b->voltage = 0.0f;
    b->current = 0.0f;
    b->power = 0.0f;
    const bool fresh = now->have_win
                       && (int32_t)(now_ms - now->win_ms)
                              <= (int32_t)PNODE_STALE_MS;
    if (fresh) {
        float v = 0.0f;
        float i = 0.0f;
        bool v_ok = was != NULL
                    && mean_since(was->v_n, now->v_n, was->v_sum, now->v_sum,
                                  &v);
        bool i_ok = was != NULL
                    && mean_since(was->i_n, now->i_n, was->i_sum, now->i_sum,
                                  &i);
        if (!v_ok && now->last_v) {
            v = (float)now->last_v_steps;
            v_ok = true;
        }
        if (!i_ok && now->last_i) {
            i = (float)now->last_i_ua;
            i_ok = true;
        }
        if (v_ok) {
            b->voltage = v * ((float)RCNODE_V_STEP_UV * 1e-6f);
            b->flags |= (uint16_t)LINK_BN_VOLTAGE_OK;
        }
        if (i_ok) {
            b->current = i * 1e-6f;
            b->flags |= (uint16_t)LINK_BN_CURRENT_OK;
        }
        if (v_ok && i_ok) {
            b->power = b->voltage * b->current;
        }
    }
    const bool run = now->run;
    b->voltage_min = (run && now->v_min_ok)
                         ? (float)now->v_min_steps
                               * ((float)RCNODE_V_STEP_UV * 1e-6f)
                         : b->voltage;
    b->current_max = (run && now->i_max_ok) ? (float)now->i_max_ua * 1e-6f
                                            : b->current;
    b->power_max = (run && now->p_max_ok) ? (float)now->p_max_mw * 1e-3f
                                          : b->power;
    const bool clip_new = was != NULL && now->clip_n != was->clip_n;
    b->clipped = (fresh && (clip_new || now->last_clip))
                 || (run && now->run_clip);
    b->charge_mah = 0.0f;
    b->energy_wh = 0.0f;
    if (now->totals_ok
        && (int32_t)(now_ms - now->acc_ms) <= (int32_t)PNODE_ACC_STALE_MS) {
        /* 1mAh is 3.6C; 1Wh is 3600 J. */
        b->charge_mah = (float)now->charge_uc / 3.6e6f;
        b->energy_wh = (float)now->energy_mj / 3.6e6f;
        b->flags |= (uint16_t)LINK_BN_TOTALS_OK;
    }
    return fresh;
}
