/*
 * The sensor bus's sampling schedule.  See sense_sched.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_sched.h"

#include <string.h>

/* What a rotation item reads. */
typedef enum {
    ROT_CH2_I = 0,
    ROT_CH3_I,
    ROT_CH1_V,
    ROT_CH2_V,
    ROT_CH3_V,
    ROT_TEMP,
    ROT_DIAG,
    ROT_ENERGY,
    ROT_CHARGE,
    ROT_FLAGS,
} rot_t;

/* The two parts take turns. */
static const uint8_t k_rotation[SENSE_ROTATION] = {
    ROT_CH2_I, ROT_TEMP,   ROT_CH3_I, ROT_DIAG,   ROT_CH1_V,
    ROT_ENERGY, ROT_CH2_V, ROT_CHARGE, ROT_CH3_V, ROT_FLAGS,
};

static void acc_clear(sense_acc_t *a)
{
    memset(a, 0, sizeof(*a));
}

static void acc_current(sense_acc_t *a, sense_value_t v)
{
    if (v.clip == SENSE_CLIP_HIGH) {
        a->clip_hi = true;
        return;
    }
    if (v.clip == SENSE_CLIP_LOW) {
        a->clip_lo = true;
        return;
    }
    if (a->n_i == 0u || v.value < a->i_min) {
        a->i_min = v.value;
    }
    if (a->n_i == 0u || v.value > a->i_max) {
        a->i_max = v.value;
    }
    a->i_sum += v.value;
    ++a->n_i;
}

static void acc_voltage(sense_acc_t *a, int32_t uv)
{
    if (a->n_v == 0u || uv < a->v_min) {
        a->v_min = uv;
    }
    a->v_sum += uv;
    ++a->n_v;
}

static void acc_close(const sense_acc_t *a, uint64_t number, sense_window_t *w)
{
    memset(w, 0, sizeof(*w));
    w->number  = (uint16_t)number;
    w->n_i     = a->n_i;
    w->n_v     = a->n_v;
    w->clip_hi = a->clip_hi;
    w->clip_lo = a->clip_lo;
    if (a->n_i > 0u) {
        w->i_mean_ua = (int32_t)(a->i_sum / (int64_t)a->n_i);
        w->i_min_ua  = a->i_min;
        w->i_max_ua  = a->i_max;
    }
    if (a->n_v > 0u) {
        w->v_mean_uv = (int32_t)(a->v_sum / (int64_t)a->n_v);
        w->v_min_uv  = a->v_min;
    }
}

bool sense_sched_init(sense_sched_t *s, const sense_sched_io_t *io,
                      const sense_sched_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->io  = *io;
    s->cfg = *cfg;
    sense_bus_init(&s->bus, &io->i2c);
    if (cfg->khz == SENSE_KHZ_FAST) {
        s->every_ms = 1u;
    } else if (cfg->khz == SENSE_KHZ_STANDARD) {
        s->every_ms = 4u;
    } else {
        /* Both parts left zeroed, SENSE_PART_UNSET: never addressed. */
        return false;
    }
    if (cfg->ina228_en) {
        s->i228_setup = ina228_init(&s->i228, &s->bus, cfg->ina228_addr,
                                    cfg->ina228_shunt_uohm,
                                    cfg->ina228_max_ma, INA228_ADC_BENCH);
    }
    if (cfg->ina3221_en) {
        s->i3221_setup = ina3221_init(
            &s->i3221, &s->bus, cfg->ina3221_addr, cfg->ina3221_shunt_uohm,
            ina3221_config(cfg->ina3221_channels, 0u, 0u, 0u,
                           INA3221_MODE_CONT));
        if (s->i3221_setup == INA3221_SETUP_OK) {
            /* The step below the top code: a clip is at least this. */
            s->ch_clip_ua = ina3221_current_ua(cfg->ina3221_shunt_uohm,
                                               4094).value;
        }
    }
    return true;
}

bool sense_sched_fast_pair(sense_sched_t *s, bool on)
{
    if (on && s->every_ms != 1u) {
        return false;
    }
    s->fast_pair = on;
    return true;
}

void sense_sched_arm(sense_sched_t *s)
{
    memset(&s->run, 0, sizeof(s->run));
    s->run.clear_owed = true;
}

bool sense_sched_window(const sense_sched_t *s, sense_src_t src,
                        sense_window_t *out)
{
    if (!s->have_last || (unsigned)src >= SENSE_SRC_COUNT) {
        return false;
    }
    *out = s->last[src];
    return true;
}

/* ------------------------------------------------------------ capture */

static bool ch_enabled(const sense_sched_t *s, uint8_t ch)
{
    return (ina3221_channels(s->i3221.config) & (1u << (ch - 1u))) != 0u;
}

static bool cap_under_way(const sense_cap_t *c)
{
    return c->state == SENSE_CAP_ARMED || c->state == SENSE_CAP_WAITING
           || c->state == SENSE_CAP_MOVING;
}

bool sense_sched_cap_arm(sense_sched_t *s, const sense_cap_arm_t *levels)
{
    if (ina3221_state(&s->i3221) != SENSE_PART_ONLINE || !ch_enabled(s, 1u)) {
        return false;
    }
    const uint16_t seq = s->cap.seq;
    memset(&s->cap, 0, sizeof(s->cap));
    s->cap.seq   = seq;
    s->cap.arm   = *levels;
    s->cap.state = SENSE_CAP_ARMED;
    return true;
}

static float ua_to_a(int32_t ua)
{
    return (float)ua * 1e-6f;
}

static int32_t a_to_ua(float a)
{
    const float ua = a * 1e6f;
    return (int32_t)((ua >= 0.0f) ? ua + 0.5f : ua - 0.5f);
}

void sense_sched_cap_edge(sense_sched_t *s, uint64_t edge_us)
{
    sense_cap_t *c = &s->cap;
    if (c->state != SENSE_CAP_ARMED) {
        return;
    }
    c->edge_t = (uint32_t)(edge_us / 100u);
    const uint32_t lag_ms = (s->every_ms == 1u) ? SENSE_CAP_LAG_MS_FAST
                                                : SENSE_CAP_LAG_MS_STANDARD;
    const servo_move_cfg_t mc = {
        .cmd_t    = c->edge_t,
        .window_t = servo_move_window_ms(lag_ms) * SENSE_CAP_T_PER_MS,
        .rise_a   = ua_to_a(c->arm.rise_ua),
        .ref_a    = ua_to_a(c->arm.hold_ua),
        .move_a   = ua_to_a(c->arm.move_ua),
        .band_a   = ua_to_a(c->arm.band_ua),
        .settle_n = (uint8_t)SENSE_CAP_SETTLE_N,
        .filter_n = (uint8_t)SENSE_CAP_FILTER_N,
    };
    servo_move_begin(&c->mv, &mc);
    /* The samples taken while armed, oldest first: the filter starts
     * full, and one sample alone does not start a move. */
    const unsigned first = (c->pre_head + SENSE_CAP_FILTER_N - c->pre_n)
                           % SENSE_CAP_FILTER_N;
    for (unsigned k = 0; k < c->pre_n; ++k) {
        const sense_cap_pre_t *p = &c->pre[(first + k) % SENSE_CAP_FILTER_N];
        servo_move_prime(&c->mv, p->a, (servo_move_clip_t)p->clip);
    }
    c->state = SENSE_CAP_WAITING;
}

void sense_sched_cap_disarm(sense_sched_t *s)
{
    s->cap.state = SENSE_CAP_IDLE;
}

/* The capture ends as @p state, with what the move measured. */
static void cap_end(sense_cap_t *c, sense_cap_state_t state)
{
    const servo_move_t *m = &c->mv;
    c->state = state;
    ++c->seq;
    if (state == SENSE_CAP_LOST) {
        return;
    }
    c->move_t   = servo_move_moved(m) ? m->moved_t - m->cfg.cmd_t : 0u;
    c->arrive_t = servo_move_arrived(m) ? m->end_t - m->cfg.cmd_t : 0u;
    c->samples  = m->n;
    c->mean_ua  = (m->n > 0u) ? a_to_ua(m->sum / (float)m->n) : 0;
    c->peak_ua  = a_to_ua(m->peak);
    c->clipped  = m->clipped;
}

/* A CH1 sample, or none, and the clock: both at @p at_t. */
static void cap_step(sense_sched_t *s, const sense_value_t *v, uint32_t at_t)
{
    sense_cap_t *c = &s->cap;
    if (!cap_under_way(c)) {
        return;
    }
    if (ina3221_state(&s->i3221) != SENSE_PART_ONLINE) {
        cap_end(c, SENSE_CAP_LOST);
        return;
    }
    servo_move_clip_t clip = SERVO_MOVE_CLIP_NONE;
    float a = 0.0f;
    if (v != NULL) {
        a = ua_to_a(v->value);
        if (v->clip == SENSE_CLIP_HIGH) {
            clip = SERVO_MOVE_CLIP_HIGH;
            a    = ua_to_a(s->ch_clip_ua);
        } else if (v->clip == SENSE_CLIP_LOW) {
            clip = SERVO_MOVE_CLIP_LOW;
            a    = -ua_to_a(s->ch_clip_ua);
        }
    }
    if (c->state == SENSE_CAP_ARMED) {
        if (v != NULL) {
            c->pre[c->pre_head].a    = a;
            c->pre[c->pre_head].clip = (int8_t)clip;
            c->pre_head = (uint8_t)((c->pre_head + 1u) % SENSE_CAP_FILTER_N);
            if (c->pre_n < SENSE_CAP_FILTER_N) {
                ++c->pre_n;
            }
        }
        return;
    }
    if (v != NULL) {
        servo_move_sample(&c->mv, at_t, a, clip);
    }
    switch (servo_move_tick(&c->mv, at_t)) {
    case SERVO_MOVE_MOVING:  c->state = SENSE_CAP_MOVING;        break;
    case SERVO_MOVE_ARRIVED: cap_end(c, SENSE_CAP_ARRIVED);      break;
    case SERVO_MOVE_SETTLED: cap_end(c, SENSE_CAP_SETTLED);      break;
    case SERVO_MOVE_LATE:    cap_end(c, SENSE_CAP_LATE);         break;
    case SERVO_MOVE_UNSEEN:  cap_end(c, SENSE_CAP_UNSEEN);       break;
    default:                                                     break;
    }
}

/* -------------------------------------------------------------- reads */

/* An INA228 transaction's result: the first failure after the clear ends
 * the run's totals. */
static bool i228_ok(sense_sched_t *s, sense_err_t e)
{
    if (e != SENSE_OK) {
        s->run.totals_ok = false;
        return false;
    }
    return true;
}

/* INA3221 channel @p ch's current into its window; true with a sample. */
static bool read_ch_current(sense_sched_t *s, uint8_t ch, sense_value_t *v)
{
    if (!ch_enabled(s, ch)
        || ina3221_read_current(&s->i3221, ch, v) != SENSE_OK) {
        return false;
    }
    acc_current(&s->acc[SENSE_SRC_CH1 + ch - 1u], *v);
    return true;
}

static void read_ch_bus(sense_sched_t *s, uint8_t ch)
{
    int32_t mv = 0;
    if (ch_enabled(s, ch) && ina3221_read_bus(&s->i3221, ch, &mv) == SENSE_OK) {
        acc_voltage(&s->acc[SENSE_SRC_CH1 + ch - 1u], mv * 1000);
    }
}

static void read_i228_current(sense_sched_t *s)
{
    sense_value_t v = { 0, SENSE_CLIP_NONE };
    if (!i228_ok(s, ina228_read_current(&s->i228, &v))) {
        return;
    }
    acc_current(&s->acc[SENSE_SRC_INA228], v);
    sense_run_t *r = &s->run;
    if (v.clip != SENSE_CLIP_NONE) {
        r->i_clipped = true;
        return;
    }
    if (!r->have_i || v.value > r->i_max_ua) {
        r->i_max_ua = v.value;
        r->have_i   = true;
    }
    if (s->have_vbus) {
        const int64_t p = (int64_t)s->vbus_uv * v.value / 1000000;
        if (!r->have_p || p > r->p_max_uw) {
            r->p_max_uw = p;
            r->have_p   = true;
        }
    }
}

static void read_i228_vbus(sense_sched_t *s)
{
    int32_t uv = 0;
    /* A failed read leaves no voltage for the current after it. */
    s->have_vbus = false;
    if (!i228_ok(s, ina228_read_vbus(&s->i228, &uv))) {
        return;
    }
    acc_voltage(&s->acc[SENSE_SRC_INA228], uv);
    s->vbus_uv   = uv;
    s->have_vbus = true;
    sense_run_t *r = &s->run;
    if (!r->have_v || uv < r->v_min_uv) {
        r->v_min_uv = uv;
        r->have_v   = true;
    }
}

/* The INA228's slot @p n: CURRENT on even ones, VBUS on odd ones. */
static void read_i228(sense_sched_t *s, uint32_t n)
{
    if ((n & 1u) == 0u) {
        read_i228_current(s);
    } else {
        read_i228_vbus(s);
    }
}

/* The next rotation item. */
static void read_rotation(sense_sched_t *s)
{
    const rot_t item = (rot_t)k_rotation[s->rot % SENSE_ROTATION];
    ++s->rot;
    switch (item) {
    case ROT_CH2_I:
    case ROT_CH3_I:
        if (!s->fast_pair) {
            sense_value_t v = { 0, SENSE_CLIP_NONE };
            (void)read_ch_current(s, (item == ROT_CH2_I) ? 2u : 3u, &v);
        }
        break;
    case ROT_CH1_V: read_ch_bus(s, 1u); break;
    case ROT_CH2_V: read_ch_bus(s, 2u); break;
    case ROT_CH3_V: read_ch_bus(s, 3u); break;
    case ROT_TEMP:
        s->have_temp = i228_ok(s, ina228_read_dietemp(&s->i228, &s->temp_mdegc))
                       || s->have_temp;
        break;
    case ROT_DIAG:
        s->have_diag = i228_ok(s, ina228_read_diag(&s->i228, &s->diag))
                       || s->have_diag;
        break;
    case ROT_ENERGY:
        (void)i228_ok(s, ina228_read_energy(&s->i228, &s->run.energy_mj));
        break;
    case ROT_CHARGE:
        (void)i228_ok(s, ina228_read_charge(&s->i228, &s->run.charge_uc));
        break;
    default:
        s->have_flags = ina3221_read_flags(&s->i3221, &s->flags) == SENSE_OK
                        || s->have_flags;
        break;
    }
}

/* The windows: the one being filled closes when the clock passes its
 * end, and the next one starts empty.  In 64 bits, so the numbering runs
 * on past the 32-bit millisecond count's wrap at 49.7 days. */
static void roll_windows(sense_sched_t *s, uint64_t now_ms)
{
    if (!s->started) {
        s->started = true;
        s->t0_ms   = now_ms;
        s->win     = 0u;
    }
    const uint64_t win = (now_ms - s->t0_ms) / SENSE_WINDOW_MS;
    if (win == s->win) {
        return;
    }
    for (unsigned k = 0; k < SENSE_SRC_COUNT; ++k) {
        acc_close(&s->acc[k], s->win, &s->last[k]);
        acc_clear(&s->acc[k]);
    }
    s->have_last = true;
    s->win       = win;
}

/* The totals: cleared once the arm owes it.  Whether the clear's writes
 * were sent. */
static bool totals(sense_sched_t *s)
{
    sense_run_t *r = &s->run;
    if (!r->clear_owed) {
        return false;
    }
    if (ina228_state(&s->i228) != SENSE_PART_ONLINE) {
        r->clear_owed = false;          /* no totals for this run */
        return false;
    }
    if (ina228_clear_totals(&s->i228) == SENSE_OK) {
        r->clear_owed = false;
        r->totals_ok  = true;
        r->energy_mj  = 0u;
        r->charge_uc  = 0;
    }
    return true;
}

void sense_sched_tick(sense_sched_t *s)
{
    const uint64_t us = s->io.now_us(s->io.ctx);
    const uint64_t now_ms = us / 1000u;
    /* The bus and the parts count retries in 32 bits, modulo. */
    const uint32_t now_ms32 = (uint32_t)now_ms;
    roll_windows(s, now_ms);

    if (sense_bus_recovery_due(&s->bus, now_ms32)) {
        if (s->io.recover != NULL) {
            s->io.recover(s->io.ctx);
        }
        sense_bus_recovered(&s->bus, now_ms32);
    }
    (void)ina3221_step(&s->i3221, now_ms32);
    (void)ina228_step(&s->i228, now_ms32);
    const bool cleared = totals(s);

    const uint32_t tick = s->ticks++;
    sense_value_t ch1 = { 0, SENSE_CLIP_NONE };
    bool have_ch1 = false;
    if (s->every_ms == 1u) {
        /* 400 kHz: CH1, the pair, the INA228, and every second tick the
         * rotation. */
        have_ch1 = read_ch_current(s, 1u, &ch1);
        if (s->fast_pair) {
            sense_value_t v = { 0, SENSE_CLIP_NONE };
            (void)read_ch_current(s, 2u, &v);
            (void)read_ch_current(s, 3u, &v);
        }
        read_i228(s, tick);
        if ((tick & 1u) == 0u) {
            read_rotation(s);
        }
    } else if (!cleared) {
        /* 100 kHz: one read a tick, in turn CH1, the INA228, the rotation
         * twice; none beside the clear's writes. */
        switch (tick % 4u) {
        case 0u: have_ch1 = read_ch_current(s, 1u, &ch1); break;
        case 1u: read_i228(s, tick / 4u);                break;
        default: read_rotation(s);                       break;
        }
    }
    /* A sample is stamped when its read is done. */
    const uint32_t at_t = (uint32_t)(s->io.now_us(s->io.ctx) / 100u);
    cap_step(s, have_ch1 ? &ch1 : NULL, at_t);
}
