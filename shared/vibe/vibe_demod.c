/**
 * @file
 * @brief The balance node's synchronous demodulator (vibe_demod.h).
 *
 * The per-sample work is in demodulate(): for each sample one phase
 * addition, and for each harmonic one multiply for the harmonic's phase,
 * two table reads, two 32-bit products into 64-bit sums and two 32-bit
 * sums.  Everything else runs once per revolution or once per window.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vibe_demod.h"

#include <string.h>

/** @brief log2 of the cosine table's length. */
#define TAB_BITS 10u
/** @brief The cosine table's length. */
#define TAB_N (1u << TAB_BITS)
/** @brief The mask of a table index. */
#define TAB_MASK (TAB_N - 1u)
/** @brief A quarter turn in table entries. */
#define TAB_QUARTER (TAB_N / 4u)
/** @brief The shift from a 32-bit turn to a table index. */
#define TAB_SHIFT (32u - TAB_BITS)
/** @brief Half a table step, so the index rounds to the nearest entry. */
#define TAB_ROUND (1u << (TAB_SHIFT - 1u))

/** @brief cos(2 pi i / TAB_N) x 32,767, filled once by tab_init(). */
static int16_t s_cos[TAB_N];
/** @brief s_cos holds. */
static bool s_tab_ok;

/** @brief CORDIC steps. */
#define CORDIC_N 30
/** @brief atan(2^-i) as a fraction of a turn x 2^32. */
static const uint32_t k_atan[CORDIC_N] = {
    536870912u, 316933406u, 167458907u, 85004756u, 42667331u, 21354465u,
    10679838u, 5340245u, 2670163u, 1335087u, 667544u, 333772u, 166886u,
    83443u, 41722u, 20861u, 10430u, 5215u, 2608u, 1304u, 652u, 326u, 163u,
    81u, 41u, 20u, 10u, 5u, 3u, 1u,
};
/** @brief 1 / (the CORDIC's gain over CORDIC_N steps) x 2^30. */
#define CORDIC_INV_GAIN_Q30 652032874

/** @brief Half a turn as a 32-bit fraction. */
#define HALF_TURN 0x80000000u

/** @brief The period tolerance of a counted revolution: 1/2^3. */
#define TOL_SHIFT 3u

uint32_t vibe_isqrt64(uint64_t v)
{
    uint64_t r = 0;
    uint64_t bit = 1ull << 62;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

/**
 * @brief |v| as an unsigned value, INT64_MIN included.
 *
 * @param v  The value.
 * @return   Its magnitude.
 */
static uint64_t abs64(int64_t v)
{
    return (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
}

uint32_t vibe_atan2(int64_t x, int64_t y, uint64_t *mag)
{
    const uint64_t ax = abs64(x);
    const uint64_t ay = abs64(y);
    const uint64_t m = (ax > ay) ? ax : ay;
    if (m == 0u) {
        if (mag != NULL) {
            *mag = 0u;
        }
        return 0u;
    }
    /* To 28 or 29 bits: the vector's growth by the gain (1.65) along the
     * 45 degree worst case (1.41) stays inside an int32, and a short
     * vector keeps the steps' resolution. */
    unsigned down = 0u;
    unsigned up = 0u;
    while ((m >> down) >= (1ull << 29)) {
        ++down;
    }
    while ((m << up) < (1ull << 28)) {
        ++up;
    }
    int32_t xi = (int32_t)((ax >> down) << up);
    int32_t yi = (int32_t)((ay >> down) << up);
    if (x < 0) {
        xi = -xi;
    }
    if (y < 0) {
        yi = -yi;
    }
    uint32_t ang = 0u;
    if (xi < 0) {
        xi = -xi;
        yi = -yi;
        ang = HALF_TURN;
    }
    for (int i = 0; i < CORDIC_N; ++i) {
        const int32_t dx = yi >> i;
        const int32_t dy = xi >> i;
        if (yi > 0) {
            xi += dx;
            yi -= dy;
            ang += k_atan[i];
        } else {
            xi -= dx;
            yi += dy;
            ang -= k_atan[i];
        }
    }
    if (mag != NULL) {
        const uint64_t g = ((uint64_t)(uint32_t)xi * (uint64_t)CORDIC_INV_GAIN_Q30
                            + (1ull << 29)) >> 30;
        *mag = (g << down) >> up;
    }
    return ang;
}

void vibe_cossin(uint32_t turn, int32_t *c, int32_t *s)
{
    bool neg = false;
    /* Into -90 to +90 degrees, where the rotation converges. */
    if (turn > 0x40000000u && turn < 0xC0000000u) {
        turn -= HALF_TURN;
        neg = true;
    }
    int32_t z = (int32_t)turn;
    int32_t x = CORDIC_INV_GAIN_Q30;
    int32_t y = 0;
    for (int i = 0; i < CORDIC_N; ++i) {
        const int32_t dx = y >> i;
        const int32_t dy = x >> i;
        if (z >= 0) {
            x -= dx;
            y += dy;
            z -= (int32_t)k_atan[i];
        } else {
            x += dx;
            y -= dy;
            z += (int32_t)k_atan[i];
        }
    }
    if (c != NULL) {
        *c = neg ? -x : x;
    }
    if (s != NULL) {
        *s = neg ? -y : y;
    }
}

/** @brief Fills the cosine table, once. */
static void tab_init(void)
{
    if (s_tab_ok) {
        return;
    }
    for (uint32_t i = 0; i < TAB_N; ++i) {
        int32_t c;
        vibe_cossin(i << TAB_SHIFT, &c, NULL);
        int32_t v = (c + (1 << 14)) >> 15;
        if (v > 32767) {
            v = 32767;
        } else if (v < -32767) {
            v = -32767;
        }
        s_cos[i] = (int16_t)v;
    }
    s_tab_ok = true;
}

void vibe_cfg_default(vibe_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->fs_hz = 10000u;
    cfg->window = 2500u;
    cfg->ug_per_count_q16 = 150311927u;
    cfg->edge_shift_q8 = 0;
    cfg->blades = 0u;
    cfg->adc_max = 4095u;
    cfg->dc_min = 1321u;
    cfg->dc_max = 3038u;
}

/**
 * @brief Clears the window's sums and flags; the next window starts.
 *
 * @param d  The state.
 */
static void win_reset(vibe_demod_t *d)
{
    memset(&d->w, 0, sizeof(d->w));
    d->sd = 0;
    d->sdd = 0u;
    d->nd = 0u;
    d->clipped = false;
    d->edge_seen = false;
    d->edge_dropped = false;
    d->lost = false;
    d->blade_alias = false;
    d->restarted = false;
    d->revs = 0u;
    d->rejected = 0u;
    d->t_sum_q8 = 0u;
    d->t_min_q8 = UINT32_MAX;
    d->t_max_q8 = 0u;
    d->win_left = d->cfg.window;
}

/**
 * @brief Forgets the period lock and the quality history.
 *
 * @param d  The state.
 */
static void lock_reset(vibe_demod_t *d)
{
    d->ref_ok = false;
    d->cand_n = 0u;
    d->ring_n = 0u;
    d->ring_at = 0u;
}

/**
 * @brief Opens a no-index unit at @p t0 with the rpm set now.
 *
 * @param d   The state.
 * @param t0  Its start, Q8.
 */
static void nx_open(vibe_demod_t *d, uint32_t t0)
{
    const uint64_t turn = ((uint64_t)600u * d->cfg.fs_hz * 256u
                           + d->rpm_x10 / 2u) / d->rpm_x10;
    if (turn < (uint64_t)VIBE_REV_MIN * 256u
            || turn > (uint64_t)VIBE_REV_MAX * 256u) {
        d->rpm_bad = true;
        d->open = false;
        return;
    }
    d->rpm_bad = false;
    uint32_t g = (uint32_t)(((uint64_t)VIBE_UNIT_MIN * 256u + turn - 1u) / turn);
    if (g == 0u) {
        g = 1u;
    }
    while (g > 1u && (uint64_t)g * turn > (uint64_t)VIBE_REV_MAX * 256u) {
        --g;
    }
    d->u_turns = g;
    d->t0_q8 = t0;
    d->vend_q8 = t0 + (uint32_t)((uint64_t)g * turn);
    d->open = true;
}

void vibe_demod_init(vibe_demod_t *d, const vibe_cfg_t *cfg,
                     vibe_report_fn fn, void *ctx)
{
    if (d == NULL) {
        return;
    }
    tab_init();
    memset(d, 0, sizeof(*d));
    if (cfg != NULL) {
        d->cfg = *cfg;
    } else {
        vibe_cfg_default(&d->cfg);
    }
    if (d->cfg.fs_hz < 1000u) {
        d->cfg.fs_hz = 1000u;
    } else if (d->cfg.fs_hz > 100000u) {
        d->cfg.fs_hz = 100000u;
    }
    if (d->cfg.window < 256u) {
        d->cfg.window = 256u;
    } else if (d->cfg.window > 65535u) {
        d->cfg.window = 65535u;
    }
    if (d->cfg.blades > VIBE_BLADES_MAX) {
        d->cfg.blades = VIBE_BLADES_MAX;
    }
    d->fn = fn;
    d->ctx = ctx;
    d->dc_ref = ((int32_t)d->cfg.dc_min + (int32_t)d->cfg.dc_max) / 2;
    lock_reset(d);
    win_reset(d);
}

/**
 * @brief Clears the window's blade-pass sums after a change of blade count.
 *
 * @param d  The state.
 */
static void blade_clear(vibe_demod_t *d)
{
    vibe_sums_t *w = &d->w;
    w->xc[VIBE_H_BLADE] = 0;
    w->xs[VIBE_H_BLADE] = 0;
    w->c[VIBE_H_BLADE] = 0;
    w->s[VIBE_H_BLADE] = 0;
    w->pw[VIBE_H_BLADE] = 0u;
    w->x_blade = 0;
    w->n_blade = 0u;
    w->units_blade = 0u;
    d->blade_alias = false;
    d->restarted = true;
}

void vibe_demod_set_blades(vibe_demod_t *d, uint8_t blades)
{
    if (d == NULL) {
        return;
    }
    const uint8_t b = (blades > VIBE_BLADES_MAX) ? (uint8_t)VIBE_BLADES_MAX
                                                 : blades;
    if (b != d->cfg.blades) {
        d->cfg.blades = b;
        blade_clear(d);
    }
}

void vibe_demod_set_rpm(vibe_demod_t *d, uint32_t rpm_x10)
{
    if (d == NULL || rpm_x10 == d->rpm_x10) {
        return;
    }
    const bool was = d->rpm_x10 != 0u;
    d->rpm_x10 = rpm_x10;
    if (was != (rpm_x10 != 0u)) {
        d->open = false;
        d->nbuf = 0u;
        d->qn = 0u;
        d->have_last = false;
        d->rpm_bad = false;
        lock_reset(d);
        /* The window keeps its rms and DC, not the other mode's sums. */
        memset(&d->w, 0, sizeof(d->w));
        d->revs = 0u;
        d->rejected = 0u;
        d->t_sum_q8 = 0u;
        d->t_min_q8 = UINT32_MAX;
        d->t_max_q8 = 0u;
        d->blade_alias = false;
        d->restarted = true;
    }
    if (rpm_x10 != 0u && !d->open) {
        d->nbuf = 0u;
        nx_open(d, d->pos_q8);
    }
}

uint32_t vibe_demod_pos_q8(const vibe_demod_t *d)
{
    return (d != NULL) ? d->pos_q8 : 0u;
}

/**
 * @brief Demodulates a unit's samples and adds them to the window.
 *
 * @param d      The state.
 * @param k      Samples: buf[0] to buf[k - 1].
 * @param d0_q8  buf[0]'s distance after the unit's start, Q8, 0 to 255.
 * @param t_q8   The unit's length, Q8.
 * @param turns  Turns of the rotor in it.
 */
static void demodulate(vibe_demod_t *d, uint32_t k, uint32_t d0_q8,
                       uint32_t t_q8, uint32_t turns)
{
    /* One sample's turn, x 2^32; under a turn, as t_q8 > turns x 256. */
    const uint64_t step64 = ((uint64_t)turns << 40) / t_q8;
    const uint32_t step = (uint32_t)step64;
    const uint32_t hb = d->cfg.blades;
    /* The blade-pass under half the sample rate: under half a turn of
     * the blade phase a sample. */
    const bool blade_ok = hb != 0u && (uint64_t)hb * step64 < (1ull << 31);
    if (hb != 0u && !blade_ok) {
        d->blade_alias = true;
    }
    const uint32_t mul[VIBE_H_COUNT] = { 1u, 2u, hb };
    const unsigned nh = blade_ok ? 3u : 2u;

    int64_t xc[VIBE_H_COUNT] = { 0 };
    int64_t xs[VIBE_H_COUNT] = { 0 };
    int32_t cs[VIBE_H_COUNT] = { 0 };
    int32_t sn[VIBE_H_COUNT] = { 0 };
    int32_t sx = 0;
    uint32_t ph = (uint32_t)(((uint64_t)d0_q8 * step64) >> 8);
    for (uint32_t j = 0; j < k; ++j) {
        const int32_t v = d->buf[j];
        sx += v;
        for (unsigned h = 0; h < nh; ++h) {
            const uint32_t idx = (ph * mul[h] + TAB_ROUND) >> TAB_SHIFT;
            const int32_t c = s_cos[idx & TAB_MASK];
            const int32_t s = s_cos[(idx - TAB_QUARTER) & TAB_MASK];
            xc[h] += (int64_t)(v * c);
            xs[h] += (int64_t)(v * s);
            cs[h] += c;
            sn[h] += s;
        }
        ph += step;
    }

    vibe_sums_t *w = &d->w;
    w->x += sx;
    w->n += k;
    if (blade_ok) {
        w->x_blade += sx;
        w->n_blade += k;
    }
    const bool nx = d->rpm_x10 != 0u;
    if (nx) {
        ++w->units;
        if (blade_ok) {
            ++w->units_blade;
        }
    }
    for (unsigned h = 0; h < nh; ++h) {
        w->xc[h] += xc[h];
        w->xs[h] += xs[h];
        w->c[h] += cs[h];
        w->s[h] += sn[h];
        /* The unit's own vector with its DC taken out, counts x 256:
         * amplitude = 2 sum / (k 2^15), x 2^8. */
        const int64_t ip = xc[h] - ((int64_t)sx * cs[h]) / (int64_t)k;
        const int64_t qp = xs[h] - ((int64_t)sx * sn[h]) / (int64_t)k;
        const int64_t iv = ip / ((int64_t)k * 64);
        const int64_t qv = qp / ((int64_t)k * 64);
        if (nx) {
            w->pw[h] += (uint64_t)(iv * iv + qv * qv);
        }
        if (h == VIBE_H_1X) {
            vibe_vec_t *r = &d->ring[d->ring_at];
            r->i_q8 = (int32_t)iv;
            r->q_q8 = (int32_t)qv;
            r->mag_q8 = vibe_isqrt64((uint64_t)(iv * iv + qv * qv));
            d->ring_at = (d->ring_at + 1u) % VIBE_QUAL_REVS;
            if (d->ring_n < VIBE_QUAL_REVS) {
                ++d->ring_n;
            }
        }
    }
}

/**
 * @brief Whether a revolution's period counts, and the lock it updates.
 *
 * @param d     The state.
 * @param t_q8  The period, Q8.
 * @return      true when it counts.
 */
static bool period_ok(vibe_demod_t *d, uint32_t t_q8)
{
    if (d->ref_ok) {
        const uint32_t diff = (t_q8 > d->ref_q8) ? t_q8 - d->ref_q8
                                                 : d->ref_q8 - t_q8;
        if (diff <= (d->ref_q8 >> TOL_SHIFT)) {
            d->ref_q8 = t_q8;
            d->cand_n = 0u;
            return true;
        }
    }
    if (d->cand_n != 0u) {
        const uint32_t diff = (t_q8 > d->cand_q8) ? t_q8 - d->cand_q8
                                                  : d->cand_q8 - t_q8;
        d->cand_n = (diff <= (d->cand_q8 >> TOL_SHIFT)) ? d->cand_n + 1u : 1u;
    } else {
        d->cand_n = 1u;
    }
    d->cand_q8 = t_q8;
    if (d->cand_n >= (d->ref_ok ? 3u : 2u)) {
        d->ref_q8 = t_q8;
        d->ref_ok = true;
        d->cand_n = 0u;
        return true;
    }
    return false;
}

/**
 * @brief Closes the open unit at @p t1 with the first @p k buffered samples.
 *
 * @param d      The state.
 * @param k      The unit's samples, from buf[0].
 * @param t1     Its end, Q8.
 * @param turns  Turns in it: 1 in index mode.
 */
static void close_unit(vibe_demod_t *d, uint32_t k, uint32_t t1,
                       uint32_t turns)
{
    const bool index = d->rpm_x10 == 0u;
    const uint32_t t_q8 = t1 - d->t0_q8;
    const uint32_t b0 = d->pos_q8 - (d->nbuf << 8);
    const int32_t d0 = (int32_t)(b0 - d->t0_q8);
    const bool in_range = t_q8 >= (VIBE_REV_MIN << 8) * turns
                          && t_q8 <= (VIBE_REV_MAX << 8);
    const bool complete = in_range && d0 >= 0 && d0 < 256 && k != 0u
                          && k == ((t_q8 - (uint32_t)d0 + 255u) >> 8);
    if (index) {
        if (!complete) {
            ++d->rejected;
            d->cand_n = 0u;
            return;
        }
        if (!period_ok(d, t_q8)) {
            ++d->rejected;
            return;
        }
        d->t_sum_q8 += t_q8;
        if (t_q8 < d->t_min_q8) {
            d->t_min_q8 = t_q8;
        }
        if (t_q8 > d->t_max_q8) {
            d->t_max_q8 = t_q8;
        }
    } else if (!complete) {
        return;
    }
    ++d->revs;
    demodulate(d, k, (uint32_t)d0, t_q8, turns);
}

/**
 * @brief How many buffered samples lie before @p e.
 *
 * @param d  The state.
 * @param e  A position, Q8.
 * @return   0 to nbuf.
 */
static uint32_t count_before(const vibe_demod_t *d, uint32_t e)
{
    const uint32_t b0 = d->pos_q8 - (d->nbuf << 8);
    const int32_t diff = (int32_t)(e - b0);
    if (diff <= 0) {
        return 0u;
    }
    const uint32_t k = ((uint32_t)diff + 255u) >> 8;
    return (k > d->nbuf) ? d->nbuf : k;
}

/**
 * @brief Drops the first @p k buffered samples.
 *
 * @param d  The state.
 * @param k  0 to nbuf.
 */
static void drop_front(vibe_demod_t *d, uint32_t k)
{
    if (k == 0u) {
        return;
    }
    d->nbuf -= k;
    if (d->nbuf != 0u) {
        memmove(d->buf, d->buf + k, d->nbuf * sizeof(d->buf[0]));
    }
}

/**
 * @brief Takes an index edge: closes the open revolution, opens the next.
 *
 * @param d  The state.
 * @param e  The edge, Q8, shifted.
 */
static void take_edge(vibe_demod_t *d, uint32_t e)
{
    const uint32_t k = count_before(d, e);
    if (d->open) {
        close_unit(d, k, e, 1u);
    }
    drop_front(d, k);
    d->t0_q8 = e;
    d->open = true;
}

/**
 * @brief The next position at which a unit ends, if one is known.
 *
 * @param d  The state.
 * @param e  The position, Q8.
 * @return   true when there is one.
 */
static bool next_event(const vibe_demod_t *d, uint32_t *e)
{
    if (d->rpm_x10 != 0u) {
        if (!d->open) {
            return false;
        }
        *e = d->vend_q8;
        return true;
    }
    if (d->qn == 0u) {
        return false;
    }
    *e = d->q[0];
    return true;
}

/**
 * @brief Ends the unit at the next event and opens the next one.
 *
 * @param d  The state.
 */
static void run_event(vibe_demod_t *d)
{
    if (d->rpm_x10 != 0u) {
        const uint32_t e = d->vend_q8;
        const uint32_t k = count_before(d, e);
        close_unit(d, k, e, d->u_turns);
        drop_front(d, k);
        nx_open(d, e);
        return;
    }
    const uint32_t e = d->q[0];
    --d->qn;
    memmove(d->q, d->q + 1, d->qn * sizeof(d->q[0]));
    take_edge(d, e);
}

void vibe_demod_edge(vibe_demod_t *d, uint32_t stamp_q8)
{
    if (d == NULL) {
        return;
    }
    d->edge_seen = true;
    if (d->rpm_x10 != 0u) {
        return;
    }
    const uint32_t e = stamp_q8 + (uint32_t)d->cfg.edge_shift_q8;
    if (d->have_last && (int32_t)(e - d->last_q8) <= 0) {
        d->edge_dropped = true;
        return;
    }
    if (d->qn == 0u && (int32_t)(e - d->pos_q8) <= 0) {
        d->have_last = true;
        d->last_q8 = e;
        take_edge(d, e);
        return;
    }
    if (d->qn == VIBE_EDGE_QUEUE) {
        d->edge_dropped = true;
        return;
    }
    d->have_last = true;
    d->last_q8 = e;
    d->q[d->qn++] = e;
}

/**
 * @brief Amplitude of a harmonic from coherent sums, counts x 2^16.
 *
 * @param xc     Sum of x cos.
 * @param xs     Sum of x sin.
 * @param c      Sum of cos.
 * @param s      Sum of sin.
 * @param x      Sum of x.
 * @param n      Samples.
 * @param phase  Its phase, a fraction of a turn x 2^32.
 * @return       The amplitude.
 */
static uint64_t coherent(int64_t xc, int64_t xs, int64_t c, int64_t s,
                         int64_t x, uint32_t n, uint32_t *phase)
{
    const int64_t ip = xc - (x * c) / (int64_t)n;
    const int64_t qp = xs - (x * s) / (int64_t)n;
    uint64_t mag;
    *phase = vibe_atan2(ip, qp, &mag);
    /* amplitude = 2 |z| / (n 2^15) counts, x 2^16 = 4 |z| / n. */
    return (mag * 4u) / n;
}

/**
 * @brief An amplitude in counts x 2^16 as ug, saturated.
 *
 * @param d        The state.
 * @param amp_q16  The amplitude.
 * @return         ug.
 */
static uint32_t to_ug(const vibe_demod_t *d, uint64_t amp_q16)
{
    if (amp_q16 >= (1ull << 32)) {
        return UINT32_MAX;
    }
    const uint64_t v = (amp_q16 * d->cfg.ug_per_count_q16 + (1ull << 31)) >> 32;
    return (v > UINT32_MAX) ? UINT32_MAX : (uint32_t)v;
}

/**
 * @brief A fraction of a turn as 0.01 degree, 0 to 35,999.
 *
 * @param turn  The angle x 2^32.
 * @return      The angle in 0.01deg.
 */
static uint16_t to_cdeg(uint32_t turn)
{
    const uint32_t v = (uint32_t)(((uint64_t)turn * 36000u + (1ull << 31)) >> 32);
    return (uint16_t)((v >= 36000u) ? v - 36000u : v);
}

/**
 * @brief Saturates a count to a byte.
 *
 * @param v  The count.
 * @return   0 to 255.
 */
static uint8_t sat8(uint32_t v)
{
    return (v > 255u) ? (uint8_t)255u : (uint8_t)v;
}

/**
 * @brief Writes the window's report, hands it on and starts the next window.
 *
 * @param d  The state.
 */
static void close_window(vibe_demod_t *d)
{
    vibe_report_t r;
    memset(&r, 0, sizeof(r));
    const vibe_sums_t *w = &d->w;
    const bool nx = d->rpm_x10 != 0u;
    r.seq = d->seq++;
    r.end_sample = d->samples;
    r.revs = sat8(d->revs);
    r.rejected = sat8(d->rejected);
    uint16_t f = 0u;
    if (d->edge_seen) {
        f |= VIBE_F_INDEX_SEEN;
    }
    if (d->lost) {
        f |= VIBE_F_INDEX_LOST;
    }
    if (d->clipped) {
        f |= VIBE_F_CLIPPED;
    }
    if (d->edge_dropped) {
        f |= VIBE_F_EDGE_DROPPED;
    }
    if (d->restarted) {
        f |= VIBE_F_RESTARTED;
    }
    if (nx) {
        f |= VIBE_F_NO_INDEX;
        if (d->rpm_bad) {
            f |= VIBE_F_RPM_RANGE;
        }
        r.rpm_x10 = d->rpm_x10;
    } else if (d->revs != 0u && d->rejected == 0u && !d->lost
               && !d->edge_dropped) {
        f |= VIBE_F_INDEX_STEADY;
    }
    if (d->cfg.blades != 0u && !d->blade_alias && w->n_blade != 0u) {
        f |= VIBE_F_BLADE_OK;
    }

    if (w->n != 0u) {
        f |= VIBE_F_VALID;
        for (unsigned h = 0; h < VIBE_H_COUNT; ++h) {
            const bool blade = h == VIBE_H_BLADE;
            uint64_t amp = 0u;
            uint32_t ph = 0u;
            if (nx) {
                const uint32_t u = blade ? w->units_blade : w->units;
                if (u != 0u) {
                    amp = (uint64_t)vibe_isqrt64((w->pw[h] / u) << 16);
                }
            } else if (!blade || w->n_blade != 0u) {
                amp = coherent(w->xc[h], w->xs[h], w->c[h], w->s[h],
                               blade ? w->x_blade : w->x,
                               blade ? w->n_blade : w->n, &ph);
            }
            r.mag_ug[h] = to_ug(d, amp);
            r.phase_cdeg[h] = nx ? 0u : to_cdeg(ph);
        }
        if (!nx && d->revs != 0u && d->t_sum_q8 != 0u) {
            /* rpm = 60 fs / period, the period in Q8 samples. */
            const uint64_t num = (uint64_t)600u * d->cfg.fs_hz * 256u * d->revs;
            r.rpm_x10 = (uint32_t)((num + d->t_sum_q8 / 2u) / d->t_sum_q8);
            const uint64_t mean = d->t_sum_q8 / d->revs;
            const uint64_t sp = ((uint64_t)(d->t_max_q8 - d->t_min_q8) * 1000u
                                 + mean / 2u) / mean;
            r.spread_pm = (uint16_t)((sp > 65535u) ? 65535u : sp);
        }
    }

    if (d->ring_n >= 2u) {
        int64_t si = 0;
        int64_t sq = 0;
        uint64_t sm = 0u;
        for (uint32_t i = 0; i < d->ring_n; ++i) {
            si += d->ring[i].i_q8;
            sq += d->ring[i].q_q8;
            sm += d->ring[i].mag_q8;
        }
        if (sm != 0u) {
            const uint64_t len = vibe_isqrt64((uint64_t)(si * si + sq * sq));
            const uint64_t pct = (len * 100u + sm / 2u) / sm;
            r.quality_pct = (uint8_t)((pct > 100u) ? 100u : pct);
        }
        r.quality_revs = (uint8_t)d->ring_n;
        f |= VIBE_F_QUALITY;
    }

    if (d->nd != 0u) {
        /* The mean and the variance after the reference, x 2^16. */
        const int64_t mean_q16 = (d->sd * 65536) / (int64_t)d->nd;
        const uint64_t edd_q16 = (d->sdd << 16) / d->nd;
        const uint64_t m2 = (uint64_t)((mean_q16 * mean_q16) >> 16);
        const uint64_t var_q16 = (edd_q16 > m2) ? edd_q16 - m2 : 0u;
        r.rms_ug = to_ug(d, (uint64_t)vibe_isqrt64(var_q16 << 16));
        const int64_t dc_q16 = (int64_t)d->dc_ref * 65536 + mean_q16;
        const int64_t dc16 = (dc_q16 + 2048) >> 12;
        r.dc_x16 = (uint16_t)((dc16 < 0) ? 0 : (dc16 > 65535) ? 65535 : dc16);
        if (r.dc_x16 >= (uint32_t)d->cfg.dc_min * 16u
                && r.dc_x16 <= (uint32_t)d->cfg.dc_max * 16u) {
            f |= VIBE_F_DC_OK;
        }
        d->dc_ref = (int32_t)((dc_q16 + 32768) >> 16);
    }
    r.flags = f;
    win_reset(d);
    if (d->fn != NULL) {
        d->fn(&r, d->ctx);
    }
}

/**
 * @brief Appends samples to the open unit and to the window's sums.
 *
 * @param d  The state.
 * @param x  The samples.
 * @param m  How many; fit the buffer and the window.
 */
static void append(vibe_demod_t *d, const int16_t *x, uint32_t m)
{
    const int32_t ref = d->dc_ref;
    const int32_t top = d->cfg.adc_max;
    int64_t sd = 0;
    uint64_t sdd = 0u;
    bool clip = false;
    int16_t *dst = d->buf + d->nbuf;
    for (uint32_t i = 0; i < m; ++i) {
        const int32_t v = x[i];
        dst[i] = x[i];
        const int32_t dv = v - ref;
        sd += dv;
        sdd += (uint64_t)((int64_t)dv * dv);
        clip = clip || v <= 0 || v >= top;
    }
    d->sd += sd;
    d->sdd += sdd;
    d->nd += m;
    d->clipped = d->clipped || clip;
    d->nbuf += m;
    d->pos_q8 += m << 8;
    d->samples += m;
    d->win_left -= m;
}

void vibe_demod_push(vibe_demod_t *d, const int16_t *x, size_t n)
{
    if (d == NULL || x == NULL) {
        return;
    }
    size_t i = 0;
    while (i < n) {
        uint32_t e;
        while (next_event(d, &e) && (int32_t)(e - d->pos_q8) <= 0) {
            run_event(d);
        }
        uint32_t m = (n - i > d->win_left) ? d->win_left : (uint32_t)(n - i);
        if (next_event(d, &e)) {
            const uint32_t upto = ((e - d->pos_q8) + 255u) >> 8;
            if (m > upto) {
                m = upto;
            }
        }
        if (d->nbuf == VIBE_REV_MAX) {
            /* No edge in a whole buffer: the index is gone. */
            if (d->rpm_x10 == 0u) {
                d->lost = true;
                d->open = false;
                lock_reset(d);
                /* The last edge can lie any time back: the next one is
                 * not held to it (a wrap after 2^23 samples would read
                 * it as earlier). */
                if (d->qn == 0u) {
                    d->have_last = false;
                }
            }
            d->nbuf = 0u;
        }
        if (m > VIBE_REV_MAX - d->nbuf) {
            m = VIBE_REV_MAX - d->nbuf;
        }
        append(d, x + i, m);
        i += m;
        if (d->win_left == 0u) {
            close_window(d);
        }
    }
}

void vibe_demod_feed(vibe_demod_t *d, const int16_t *x, size_t n,
                     const uint32_t *edges, size_t ne)
{
    if (d == NULL || x == NULL) {
        return;
    }
    size_t i = 0;
    for (size_t k = 0; edges != NULL && k < ne; ++k) {
        const uint32_t e = edges[k] + (uint32_t)d->cfg.edge_shift_q8;
        const int32_t diff = (int32_t)(e - d->pos_q8);
        if (diff > 0 && d->rpm_x10 == 0u) {
            size_t upto = ((uint32_t)diff + 255u) >> 8;
            if (upto > n - i) {
                upto = n - i;
            }
            vibe_demod_push(d, x + i, upto);
            i += upto;
        }
        vibe_demod_edge(d, edges[k]);
    }
    vibe_demod_push(d, x + i, n - i);
}
