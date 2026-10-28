/**
 * @file
 * @brief The sensor bus's sampling schedule.
 *
 * See sense_sched.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_sched_int.h"

#include <string.h>

/**
 * @brief What a rotation item reads.
 *
 * An INA3221 item names its part.
 */
typedef enum {
    ROT_NONE = 0,
    ROT_TEMP,
    ROT_DIAG,
    ROT_ENERGY,
    ROT_CHARGE,
    ROT_ADC,        /* the INA228's ADC_CONFIG read back          */
    ROT_FLAGS0,     /* part a's Mask/Enable: ROT_FLAGS0 + a       */
    ROT_CONFIG0 = ROT_FLAGS0 + SENSE_I3221_PARTS,
                    /* part a's Configuration read back: + a      */
} rot_t;

/**
 * @brief The INA228's eight slots where they always were; each INA3221's
 * Mask/Enable and Configuration 20ms apart, part 0's in the slots one part had
 * before; four slots empty.
 */
static const uint8_t k_rotation[SENSE_ROTATION] = {
    ROT_FLAGS0 + 1,  ROT_TEMP,   ROT_NONE,         ROT_DIAG,
    ROT_FLAGS0 + 2,  ROT_ENERGY, ROT_FLAGS0 + 3,   ROT_CHARGE,
    ROT_NONE,        ROT_FLAGS0,
    ROT_CONFIG0 + 1, ROT_ADC,    ROT_NONE,         ROT_DIAG,
    ROT_CONFIG0 + 2, ROT_ENERGY, ROT_CONFIG0 + 3,  ROT_CHARGE,
    ROT_NONE,        ROT_CONFIG0,
};

/**
 * @brief Clear a window accumulator.
 *
 * @param a The accumulator.
 */
static void acc_clear(sense_acc_t *a)
{
    memset(a, 0, sizeof(*a));
}

/**
 * @brief Add one current sample to a window accumulator.
 *
 * @param a The accumulator.
 * @param v The sample.
 */
static void acc_current(sense_acc_t *a, sense_value_t v)
{
    if (v.clip == SENSE_CLIP_HIGH) {
        a->clip_hi = true;
        ++a->n_hi;
        return;
    }
    if (v.clip == SENSE_CLIP_LOW) {
        a->clip_lo = true;
        ++a->n_lo;
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

/**
 * @brief Add one bus voltage sample to a window accumulator.
 *
 * @param a The accumulator.
 * @param uv Voltage, in µV.
 */
static void acc_voltage(sense_acc_t *a, int32_t uv)
{
    if (a->n_v == 0u || uv < a->v_min) {
        a->v_min = uv;
    }
    a->v_sum += uv;
    ++a->n_v;
}

/**
 * @brief Close a window: write its totals from an accumulator.
 *
 * @param a The accumulator.
 * @param number Number of the window.
 * @param w Receives the window.
 */
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

/**
 * @brief FAST's window into a ring entry: every sample counted, a clipped one
 * as the end of the range it read.  @p top and @p bottom are the ends of FAST's
 * range.
 *
 * @param top The current at the top code, in µA.
 * @param bottom The current at the bottom code, in µA.
 * @param a The accumulator.
 * @param e Receives the ring entry.
 */
static void ring_close(int32_t top, int32_t bottom, const sense_acc_t *a,
                       sense_ring_win_t *e)
{
    memset(e, 0, sizeof(*e));
    e->closed  = true;
    e->n_clip  = (uint16_t)(a->n_hi + a->n_lo);
    e->n_i     = (uint16_t)(a->n_i + e->n_clip);
    e->n_v     = a->n_v;
    e->clip_hi = a->clip_hi;
    e->clip_lo = a->clip_lo;
    if (e->n_i > 0u) {
        const int64_t sum = a->i_sum + (int64_t)a->n_hi * top
                            + (int64_t)a->n_lo * bottom;
        e->i_mean_ua = (int32_t)(sum / (int64_t)e->n_i);
        /* The ends lie outside every sample with a value. */
        if (a->n_hi > 0u) {
            e->i_max_ua = top;
        } else {
            e->i_max_ua = (a->n_i > 0u) ? a->i_max : bottom;
        }
        if (a->n_lo > 0u) {
            e->i_min_ua = bottom;
        } else {
            e->i_min_ua = (a->n_i > 0u) ? a->i_min : top;
        }
    }
    if (a->n_v > 0u) {
        e->v_mean_uv = (int32_t)(a->v_sum / (int64_t)a->n_v);
        e->v_min_uv  = a->v_min;
    }
}

/**
 * @brief Window @p number has closed with FAST's @p a, empty with no FAST: the
 * ring moves on by as many places as numbers have passed, and the places of
 * skipped numbers read not closed.
 *
 * @param s The schedule.
 * @param number Number of the window.
 * @param a FAST's accumulator, or the empty one with no FAST.
 */
static void ring_push(sense_sched_t *s, uint64_t number, const sense_acc_t *a)
{
    const uint64_t step = s->have_last ? number - s->ring_at : SENSE_WIN_RING;
    for (unsigned k = SENSE_WIN_RING; k-- > 0u;) {
        if (k >= step) {
            s->ring[k] = s->ring[k - step];
        } else {
            memset(&s->ring[k], 0, sizeof(s->ring[k]));
        }
    }
    const uint8_t f = s->cfg.i3221.fast;
    if (f == SENSE_FAST_NONE) {
        sense_acc_t none;
        memset(&none, 0, sizeof(none));
        ring_close(0, 0, &none, &s->ring[0]);
    } else {
        ring_close(s->ch_top_ua[f], s->ch_bottom_ua[f], a, &s->ring[0]);
    }
    s->ring_at = number;
}

/**
 * @brief Part @p a's set-up from the map: its channels with a shunt.
 *
 * @param s The schedule.
 * @param a Part index, 0 to 3.
 */
static void init_part(sense_sched_t *s, unsigned a)
{
    const sense_i3221_cfg_t *m = &s->cfg.i3221;
    uint8_t on = 0u;
    for (unsigned ch = 1u; ch <= INA3221_CHANNELS; ++ch) {
        if (m->shunt_uohm[SENSE_CH(a, ch)] != 0u) {
            on |= (uint8_t)(1u << (ch - 1u));
        }
    }
    s->i3221_setup[a] = ina3221_init_shunts(
        &s->i3221[a], &s->bus, (uint8_t)(INA3221_ADDR_MIN + a),
        &m->shunt_uohm[SENSE_CH(a, 1u)],
        ina3221_config(on, 0u, 0u, 0u, INA3221_MODE_CONT));
    if (s->i3221_setup[a] != INA3221_SETUP_OK) {
        return;
    }
    for (unsigned ch = 1u; ch <= INA3221_CHANNELS; ++ch) {
        const unsigned c = SENSE_CH(a, ch);
        const uint32_t r = s->i3221[a].shunt_uohm[ch - 1u];
        if (r == 0u) {
            continue;
        }
        /* The step below the top code: a clip is at least this. */
        s->ch_clip_ua[c]   = ina3221_current_ua(r, 4094).value;
        s->ch_top_ua[c]    = ina3221_end_ua(r, SENSE_CLIP_HIGH);
        s->ch_bottom_ua[c] = ina3221_end_ua(r, SENSE_CLIP_LOW);
    }
}

/**
 * @brief Whether channel @p c is read: its part set up and the channel on.
 *
 * @param s The schedule.
 * @param c Channel index, 0 to 11.
 * @return true when the channel is read.
 */
static bool ch_read(const sense_sched_t *s, unsigned c)
{
    const unsigned a = c / INA3221_CHANNELS;
    return c < SENSE_CHANNELS && s->i3221[a].part.state != SENSE_PART_UNSET
           && s->i3221[a].shunt_uohm[c % INA3221_CHANNELS] != 0u;
}

void sense_sched_init(sense_sched_t *s, const sense_sched_io_t *io,
                      const sense_sched_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->io  = *io;
    s->cfg = *cfg;
    sense_bus_init(&s->bus, &io->i2c);
    /* A part left zeroed is SENSE_PART_UNSET: never probed, never read. */
    if (cfg->ina228_en) {
        s->i228_setup = ina228_init(&s->i228, &s->bus, cfg->ina228_addr,
                                    cfg->ina228_shunt_uohm,
                                    cfg->ina228_max_ma, INA228_ADC_BENCH);
    }
    if (cfg->as5600_en) {
        as5600_init(&s->enc.dev, &s->bus);
    }
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        if ((cfg->i3221.fitted & (1u << a)) != 0u) {
            init_part(s, a);
        }
    }
    /* FAST on a channel that is not read is none. */
    if (cfg->i3221.fast >= SENSE_CHANNELS || !ch_read(s, cfg->i3221.fast)) {
        s->cfg.i3221.fast = SENSE_FAST_NONE;
    }
    for (unsigned c = 0; c < SENSE_CHANNELS; ++c) {
        if (c != s->cfg.i3221.fast && ch_read(s, c)) {
            s->others[s->n_others++] = (uint8_t)c;
        }
    }
}

const ina3221_t *sense_sched_fast_part(const sense_sched_t *s)
{
    const uint8_t f = s->cfg.i3221.fast;
    return (f == SENSE_FAST_NONE) ? NULL : &s->i3221[f / INA3221_CHANNELS];
}

void sense_sched_arm(sense_sched_t *s)
{
    memset(&s->run, 0, sizeof(s->run));
    s->run.clear_owed = true;
}

bool sense_sched_window(const sense_sched_t *s, unsigned src,
                        sense_window_t *out)
{
    if (!s->have_last || src >= SENSE_SRC_COUNT) {
        return false;
    }
    *out = s->last[src];
    return true;
}

bool sense_sched_ring(const sense_sched_t *s, unsigned back,
                      sense_ring_win_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s->have_last || back >= SENSE_WIN_RING) {
        return false;
    }
    *out = s->ring[back];
    return out->closed;
}

/* -------------------------------------------------------------- reads */

/**
 * @brief An INA228 transaction's result: the first failure after the clear ends
 * the run's totals.
 *
 * @param s The schedule.
 * @param e The result of the transaction.
 * @return true when the transaction succeeded.
 */
static bool i228_ok(sense_sched_t *s, sense_err_t e)
{
    if (e != SENSE_OK) {
        s->run.totals_ok = false;
        return false;
    }
    return true;
}

static void roll_windows(sense_sched_t *s, uint64_t now_ms);

/**
 * @brief A read is done: its time, µs, with the windows rolled to it, so what
 * it read goes into the window it was read in.
 *
 * The time of a read that went through is the end of its transaction, from
 * the io's done_us() when it has one; any other is now_us().
 *
 * @param s The schedule.
 * @param ok True when the read just made went through.
 * @return The time of the read, in µs.
 */
static uint64_t stamp_read(sense_sched_t *s, bool ok)
{
    const uint64_t us = (ok && s->io.done_us != NULL)
                        ? s->io.done_us(s->io.ctx)
                        : s->io.now_us(s->io.ctx);
    roll_windows(s, us / 1000u);
    return us;
}

/**
 * @brief stamp_read() of a read that went through.
 *
 * @param s The schedule.
 * @return The time of the read, in µs.
 */
static uint64_t stamp(sense_sched_t *s)
{
    return stamp_read(s, true);
}

/**
 * @brief Channel @p c's current into its window; true with a sample.  @p at_us,
 * when given, is when the read was done, sample or not.
 *
 * @param s The schedule.
 * @param c Channel index, 0 to 11.
 * @param v Receives the current.
 * @param at_us Receives the time of the read, in µs; may be NULL.
 * @return true with a sample.
 */
static bool read_ch_current(sense_sched_t *s, unsigned c, sense_value_t *v,
                            uint64_t *at_us)
{
    ina3221_t *d = &s->i3221[c / INA3221_CHANNELS];
    const bool ok = ch_read(s, c)
                    && ina3221_read_current(d,
                                            (uint8_t)(c % INA3221_CHANNELS + 1u),
                                            v) == SENSE_OK;
    const uint64_t us = stamp_read(s, ok);
    if (at_us != NULL) {
        *at_us = us;
    }
    if (ok) {
        acc_current(&s->acc[c], *v);
    }
    return ok;
}

/**
 * @brief Read channel c's bus voltage into its window.
 *
 * @param s The schedule.
 * @param c Channel index, 0 to 11.
 */
static void read_ch_bus(sense_sched_t *s, unsigned c)
{
    int32_t mv = 0;
    if (ch_read(s, c)
        && ina3221_read_bus(&s->i3221[c / INA3221_CHANNELS],
                            (uint8_t)(c % INA3221_CHANNELS + 1u),
                            &mv) == SENSE_OK) {
        (void)stamp(s);
        acc_voltage(&s->acc[c], mv * 1000);
    }
}

/**
 * @brief The next channel beside FAST, in turn.
 *
 * @param s The schedule.
 */
static void read_other(sense_sched_t *s)
{
    if (s->n_others == 0u) {
        return;
    }
    const unsigned c = s->others[s->other_at];
    s->other_at = (uint8_t)((s->other_at + 1u) % s->n_others);
    sense_value_t v = { 0, SENSE_CLIP_NONE };
    (void)read_ch_current(s, c, &v, NULL);
}

/**
 * @brief The next channel's bus voltage: every channel in turn, one that is not
 * read leaving its turn empty, so each is read every SENSE_CHANNELS even ticks.
 *
 * @param s The schedule.
 */
static void read_volt(sense_sched_t *s)
{
    const unsigned c = s->volt_at;
    s->volt_at = (uint8_t)((s->volt_at + 1u) % SENSE_CHANNELS);
    read_ch_bus(s, c);
}

/**
 * @brief Read the INA228's current into its window and the run's peaks.
 *
 * @param s The schedule.
 */
static void read_i228_current(sense_sched_t *s)
{
    sense_value_t v = { 0, SENSE_CLIP_NONE };
    if (!i228_ok(s, ina228_read_current(&s->i228, &v))) {
        return;
    }
    (void)stamp(s);
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

/**
 * @brief Read the INA228's bus voltage into its window and the run's minimum.
 *
 * @param s The schedule.
 */
static void read_i228_vbus(sense_sched_t *s)
{
    int32_t uv = 0;
    /* A failed read leaves no voltage for the current after it. */
    s->have_vbus = false;
    if (!i228_ok(s, ina228_read_vbus(&s->i228, &uv))) {
        return;
    }
    (void)stamp(s);
    acc_voltage(&s->acc[SENSE_SRC_INA228], uv);
    s->vbus_uv   = uv;
    s->have_vbus = true;
    sense_run_t *r = &s->run;
    if (!r->have_v || uv < r->v_min_uv) {
        r->v_min_uv = uv;
        r->have_v   = true;
    }
}

/**
 * @brief The INA228's slot @p n: CURRENT on even ones, VBUS on odd ones.
 *
 * @param s The schedule.
 * @param n The slot number.
 */
static void read_i228(sense_sched_t *s, uint32_t n)
{
    if ((n & 1u) == 0u) {
        read_i228_current(s);
    } else {
        read_i228_vbus(s);
    }
}

/**
 * @brief The INA228's ADC_CONFIG read back.
 *
 * A part found reset is offline from here.  The read-back is stamped as any
 * read: the windows are rolled to the time it was done, and the one being
 * filled then is emptied -- it holds samples at the reset range.  The voltage
 * of the last slot pairs with no current, and the totals are no longer the
 * run's.
 *
 * @param s The schedule.
 */
static void verify_i228(sense_sched_t *s)
{
    bool lost = false;
    (void)i228_ok(s, ina228_verify(&s->i228, &lost));
    if (!lost) {
        return;
    }
    (void)stamp(s);
    ++s->i228_resets;
    acc_clear(&s->acc[SENSE_SRC_INA228]);
    s->have_vbus     = false;
    s->run.totals_ok = false;
}

/**
 * @brief Part @p a's Configuration read back.
 *
 * A part found reset is offline from here, which ends a capture on it as lost
 * (sense_cap_step()); the windows being filled from it at the time the
 * read-back was done are emptied.
 *
 * @param s The schedule.
 * @param a Part index, 0 to 3.
 */
static void verify_i3221(sense_sched_t *s, unsigned a)
{
    bool lost = false;
    (void)ina3221_verify(&s->i3221[a], &lost);
    if (!lost) {
        return;
    }
    (void)stamp(s);
    ++s->i3221_resets[a];
    for (unsigned ch = 1u; ch <= INA3221_CHANNELS; ++ch) {
        acc_clear(&s->acc[SENSE_CH(a, ch)]);
    }
}

/**
 * @brief Read part a's Mask/Enable flags; kept when the read succeeds.
 *
 * @param s The schedule.
 * @param a Part index, 0 to 3.
 */
static void read_flags(sense_sched_t *s, unsigned a)
{
    if (ina3221_read_flags(&s->i3221[a], &s->flags[a]) == SENSE_OK) {
        s->have_flags |= (uint8_t)(1u << a);
    }
}

/**
 * @brief The next rotation item.
 *
 * @param s The schedule.
 */
static void read_rotation(sense_sched_t *s)
{
    const unsigned item = k_rotation[s->rot % SENSE_ROTATION];
    ++s->rot;
    if (item >= ROT_CONFIG0) {
        verify_i3221(s, item - ROT_CONFIG0);
        return;
    }
    if (item >= ROT_FLAGS0) {
        read_flags(s, item - ROT_FLAGS0);
        return;
    }
    switch ((rot_t)item) {
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
    case ROT_ADC:
        verify_i228(s);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ encoder */

uint16_t sense_sched_enc_still_ms(const sense_sched_t *s)
{
    if (!s->enc.anchored) {
        return 0u;
    }
    const uint64_t now_ms = s->io.now_us(s->io.ctx) / 1000u;
    const uint64_t d = now_ms - s->enc.anchor_ms;
    return (d > 65535u) ? 65535u : (uint16_t)d;
}

/**
 * @brief An angle sample at @p at_ms: the anchor moves when the angle has left
 * its tolerance.
 *
 * @param e The encoder state.
 * @param raw The angle, 0 to 4095 counts.
 * @param at_ms The time of the sample, in ms.
 */
static void enc_anchor(sense_enc_t *e, uint16_t raw, uint64_t at_ms)
{
    int d = as5600_delta(raw, e->anchor);
    if (d < 0) {
        d = -d;
    }
    if (!e->anchored || d > (int)SENSE_ENC_STILL_TOL) {
        e->anchor    = raw;
        e->anchor_ms = at_ms;
        e->anchored  = true;
    }
}

/**
 * @brief The encoder's slot: STATUS and RAW ANGLE, and every
 * SENSE_ENC_MAG_EVERY-th time the field's AGC and MAGNITUDE as well, in place
 * of STATUS.
 *
 * The angle is read in every slot, 500Hz.  Each register is a transaction of
 * its own (as5600.h).
 *
 * @param s The schedule.
 */
static void read_enc(sense_sched_t *s)
{
    sense_enc_t *e = &s->enc;
    if (as5600_state(&e->dev) != SENSE_PART_ONLINE) {
        /* Offline: the next reading starts the still time afresh, and the
         * angle of before the outage is not shown as a reading again. */
        e->anchored   = false;
        e->have_angle = false;
        return;
    }
    const uint32_t slot = e->slots++;
    const bool field = (slot % SENSE_ENC_MAG_EVERY) == SENSE_ENC_MAG_EVERY - 1u;
    uint8_t status = e->status;
    uint16_t raw = 0u;
    /* STATUS and RAW ANGLE are one sample: a STATUS that answers does not
     * clear the failures of the RAW ANGLE reads before it. */
    const uint8_t fails = e->dev.part.fails;
    if (!field && as5600_read_status(&e->dev, &status) != SENSE_OK) {
        return;
    }
    if (!field && !as5600_status_possible(status)) {
        /* ML and MH together: a device answering 0xFF, not an AS5600, and
         * the RAW ANGLE that comes with it is no position.  A failed
         * sample like any other, so the part goes offline after
         * SENSE_FAILS of them. */
        sense_part_sample_failed(&e->dev.part, fails);
        if (as5600_state(&e->dev) != SENSE_PART_ONLINE) {
            e->have_angle = false;
        }
        return;
    }
    if (as5600_read_raw(&e->dev, &raw) != SENSE_OK) {
        sense_part_sample_failed(&e->dev.part, fails);
        if (as5600_state(&e->dev) != SENSE_PART_ONLINE) {
            e->have_angle = false;
        }
        return;
    }
    const uint64_t at_ms = stamp(s) / 1000u;
    e->status     = status;
    e->raw        = raw;
    e->have_angle = true;
    ++e->samples;
    if (as5600_md(status)) {
        enc_anchor(e, raw, at_ms);
    } else {
        /* No magnet: RAW ANGLE is not a position (as5600.h), and nothing
         * holds still.  The next sample with MD set starts afresh. */
        e->anchored = false;
    }
    if (field) {
        uint8_t agc = 0u;
        uint16_t mag = 0u;
        if (as5600_read_agc(&e->dev, &agc) == SENSE_OK
            && as5600_read_mag(&e->dev, &mag) == SENSE_OK) {
            e->agc       = agc;
            e->magnitude = mag;
            e->have_mag  = true;
        }
    }
}

/**
 * @brief The windows: the one being filled closes when the clock passes its
 * end, and the next one starts empty.
 *
 * In 64 bits, so the numbering runs on past the 32-bit millisecond count's wrap
 * at 49.7 days.
 *
 * @param s The schedule.
 * @param now_ms The current time, in ms.
 */
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
    const uint8_t f = s->cfg.i3221.fast;
    ring_push(s, s->win, &s->acc[(f == SENSE_FAST_NONE) ? 0u : f]);
    for (unsigned k = 0; k < SENSE_SRC_COUNT; ++k) {
        acc_close(&s->acc[k], s->win, &s->last[k]);
        acc_clear(&s->acc[k]);
    }
    /* Which parts were online as these windows closed: what a limit
     * judges them by (sense_svc). */
    s->last_online = 0u;
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        if (ina3221_state(&s->i3221[a]) == SENSE_PART_ONLINE) {
            s->last_online = (uint8_t)(s->last_online | (1u << a));
        }
    }
    s->have_last = true;
    s->win       = win;
}

/**
 * @brief The totals: cleared once the arm owes it.
 *
 * @param s The schedule.
 */
static void totals(sense_sched_t *s)
{
    sense_run_t *r = &s->run;
    if (!r->clear_owed) {
        return;
    }
    if (ina228_state(&s->i228) != SENSE_PART_ONLINE) {
        r->clear_owed = false;          /* no totals for this run */
        return;
    }
    if (ina228_clear_totals(&s->i228) == SENSE_OK) {
        r->clear_owed = false;
        r->totals_ok  = true;
        r->energy_mj  = 0u;
        r->charge_uc  = 0;
    }
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
    /* One INA3221 probe a tick at most: 455µs of bus each. */
    bool probed = false;
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        ina3221_t *d = &s->i3221[a];
        if (d->part.state == SENSE_PART_UNSET) {
            continue;
        }
        if (sense_part_probe_due(&d->part, now_ms32)) {
            if (probed) {
                continue;
            }
            probed = true;
        }
        (void)ina3221_step(d, now_ms32);
    }
    (void)ina228_step(&s->i228, now_ms32);
    if (s->cfg.as5600_en) {
        (void)as5600_step(&s->enc.dev, now_ms32);
    }
    totals(s);

    /* FAST, one channel beside it, the INA228, and every second tick a
     * voltage and the rotation.  Each read is stamped when it is done and
     * goes into the window of that time: a recovery, a probe, the clear or
     * the reads before it can take the clock past a window's end. */
    const uint32_t tick = s->ticks++;
    const uint8_t f = s->cfg.i3221.fast;
    sense_value_t fv = { 0, SENSE_CLIP_NONE };
    uint64_t fast_us = 0u;
    bool have_fast = false;
    if (f != SENSE_FAST_NONE) {
        have_fast = read_ch_current(s, f, &fv, &fast_us);
    }
    read_other(s);
    read_i228(s, tick);
    if ((tick & 1u) == 0u) {
        read_volt(s);
        read_rotation(s);
    } else if (s->cfg.as5600_en) {
        read_enc(s);
    }
    /* The die temperature and DIAG are of the part online: once it is
     * offline, the last ones are no reading of now.  Checked every tick,
     * as a failed CURRENT or VBUS read takes the part offline as well. */
    if (ina228_state(&s->i228) != SENSE_PART_ONLINE) {
        s->have_temp = false;
        s->have_diag = false;
    }
    if (f != SENSE_FAST_NONE) {
        sense_cap_step(s, have_fast ? &fv : NULL,
                       (uint32_t)(fast_us / 100u));
    }
}
