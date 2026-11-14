/**
 * @file
 * @brief The panel's stream to the M5Stack Dial (dial_stream.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "dial_stream.h"

#include <string.h>

/** @brief sent[]'s index of a packet kind. */
#define KIND_INDEX(k) ((unsigned)(k) - DK_K_SCREEN)

_Static_assert(DK_K_SCALE - DK_K_SCREEN + 1u == 8u,
               "eight panel kinds, SCREEN to SCALE");

void dial_stream_init(dial_stream_t *st)
{
    if (st != NULL) {
        memset(st, 0, sizeof(*st));
    }
}

void dial_stream_reset(dial_stream_t *st)
{
    if (st == NULL) {
        return;
    }
    st->any = false;
    st->screen_ok = false;
    st->meta_ok = false;
    st->value_ok = false;
    st->scale_ok = false;
    st->nav_ok = 0u;
    st->live_any = false;
    st->state_ok = false;
    st->state_sent = false;
}

/**
 * @brief Clamp @p v into @p lo to @p hi.
 * @param v  The value.
 * @param lo The lowest.
 * @param hi The highest.
 * @param hit Set when @p v was outside.
 * @return The clamped value.
 */
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi, bool *hit)
{
    if (v < lo) {
        *hit = true;
        return lo;
    }
    if (v > hi) {
        *hit = true;
        return hi;
    }
    return v;
}

/**
 * @brief A pulse in its range: 0 none, else DK_US_MIN to DK_US_MAX.
 * @param us  The pulse.
 * @param hit Set when it was outside.
 * @return The pulse.
 */
static uint16_t clamp_us(uint16_t us, bool *hit)
{
    if (us == 0u) {
        return 0u;
    }
    return (uint16_t)clamp32(us, DK_US_MIN, DK_US_MAX, hit);
}

/**
 * @brief The motor readings in range, a reading without its valid bit 0.
 * @param m   The readings.
 * @param hit Set when a field was outside.
 */
static void clamp_motor(dk_live_motor_t *m, bool *hit)
{
    m->flags &= (uint8_t)(DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_FAULT);
    m->valid &= (uint8_t)(DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER
                          | DK_MV_ESC_T | DK_MV_MOT_T);
    m->thr_cpct = (uint16_t)clamp32(m->thr_cpct, 0, DK_THROTTLE_MAX, hit);
    m->esc_c = (int8_t)clamp32(m->esc_c, DK_TEMP_MIN, DK_TEMP_MAX, hit);
    m->mot_c = (int8_t)clamp32(m->mot_c, DK_TEMP_MIN, DK_TEMP_MAX, hit);
    if ((m->valid & DK_MV_RPM) == 0u) { m->rpm = 0u; }
    if ((m->valid & DK_MV_VOLT) == 0u) { m->mv = 0u; }
    if ((m->valid & DK_MV_CURR) == 0u) { m->ca = 0; }
    if ((m->valid & DK_MV_POWER) == 0u) { m->dw = 0u; }
    if ((m->valid & DK_MV_ESC_T) == 0u) { m->esc_c = 0; }
    if ((m->valid & DK_MV_MOT_T) == 0u) { m->mot_c = 0; }
}

/**
 * @brief The servo readings in range, a reading without its flag 0.
 * @param s   The readings.
 * @param hit Set when a field was outside.
 */
static void clamp_servo(dk_live_servo_t *s, bool *hit)
{
    s->flags &= (uint8_t)(DK_LIVE_ARMED | DK_LIVE_DRIVING | DK_LIVE_MEAS
                          | DK_LIVE_FB | DK_LIVE_RAIL);
    s->cmd_us = clamp_us(s->cmd_us, hit);
    s->cmd_ddeg = (int16_t)clamp32(s->cmd_ddeg, -DK_DDEG_MAX, DK_DDEG_MAX,
                                   hit);
    if ((s->flags & DK_LIVE_MEAS) == 0u) {
        s->meas_cdeg = 0;
    }
    s->meas_cdeg = (int16_t)clamp32(s->meas_cdeg, -DK_CDEG_MAX, DK_CDEG_MAX,
                                    hit);
    if ((s->flags & DK_LIVE_FB) != 0u) {
        s->fb_us = (uint16_t)clamp32(s->fb_us, DK_US_MIN, DK_US_MAX, hit);
    } else {
        s->fb_us = 0u;
        s->fb_ca = 0;
    }
    if ((s->flags & DK_LIVE_RAIL) == 0u) {
        s->rail_mv = 0u;
        s->rail_ca = 0;
        s->rail_dw = 0u;
    }
}

/**
 * @brief The full scales in range: the travel to DK_DDEG_MAX, the pulse
 *        ends both 0 unless both are pulses with the first below the
 *        second.
 * @param c   The full scales.
 * @param hit Set when a field was outside.
 */
static void clamp_scale(dk_scale_t *c, bool *hit)
{
    c->travel = (uint16_t)clamp32(c->travel, 0, DK_DDEG_MAX, hit);
    if (c->us_min == 0u && c->us_max == 0u) {
        return;
    }
    if (c->us_min < DK_US_MIN || c->us_max > DK_US_MAX
        || c->us_min >= c->us_max) {
        c->us_min = 0u;
        c->us_max = 0u;
        *hit = true;
    }
}

bool dial_model_clamp(dial_model_t *m)
{
    bool hit = false;
    if (m == NULL) {
        return false;
    }
    if (m->face != 0u) {
        m->face = (uint8_t)clamp32(m->face, DK_FACE_NAV, DK_FACE_HOME, &hit);
    }
    m->lang = (uint8_t)clamp32(m->lang, DK_LANG_EN, DK_LANG_DE, &hit);
    m->flags &= (uint8_t)(DK_SCR_NAVIGATES | DK_SCR_SOUND);
    m->title[DK_TITLE_MAX] = '\0';
    m->thr_tenths = (uint8_t)clamp32(m->thr_tenths, DK_STEP_MIN, DK_STEP_MAX,
                                     &hit);
    m->srv_tenths = (uint8_t)clamp32(m->srv_tenths, DK_STEP_MIN, DK_STEP_MAX,
                                     &hit);
    if (m->has_value) {
        m->meta.unit[DK_UNIT_MAX] = '\0';
        m->meta.decimals = (uint8_t)clamp32(m->meta.decimals, 0,
                                            DK_DECIMALS_MAX, &hit);
        if (m->meta.max < m->meta.min) {
            m->meta.max = m->meta.min;
            hit = true;
        }
        m->meta.step = clamp32(m->meta.step, 1, INT32_MAX, &hit);
        m->value.flags &= (uint8_t)(DK_VAL_SET | DK_VAL_LIMIT);
    }
    if (m->has_motor) {
        clamp_motor(&m->motor, &hit);
    }
    if (m->has_scale) {
        clamp_scale(&m->scale, &hit);
    }
    if (m->has_servo) {
        clamp_servo(&m->servo, &hit);
    }
    if (m->has_nav) {
        for (unsigned k = 0; k < 3u; ++k) {
            dk_nav_t *n = &m->nav[k];
            n->slot = (uint8_t)k;
            if (n->count == 0u ? n->index != 0u : n->index >= n->count) {
                n->index = 0u;
                n->count = 0u;
                hit = true;
            }
            n->item[DK_ITEM_MAX] = '\0';
        }
    }
    return hit;
}

/**
 * @brief Whether two VALUE_METAs say the same, seq aside.
 * @param a One.  @param b The other.
 * @return true when they do.
 */
static bool meta_eq(const dk_value_meta_t *a, const dk_value_meta_t *b)
{
    return strcmp(a->unit, b->unit) == 0 && a->decimals == b->decimals
           && a->min == b->min && a->max == b->max && a->step == b->step;
}

/**
 * @brief Whether two VALUEs say the same, seq aside.
 * @param a One.  @param b The other.
 * @return true when they do.
 */
static bool value_eq(const dk_value_t *a, const dk_value_t *b)
{
    return a->value == b->value && a->was == b->was && a->flags == b->flags;
}

/**
 * @brief Whether two NAV rows say the same, seq aside.
 * @param a One.  @param b The other.
 * @return true when they do.
 */
static bool nav_eq(const dk_nav_t *a, const dk_nav_t *b)
{
    return a->index == b->index && a->count == b->count
           && a->slot == b->slot && strcmp(a->item, b->item) == 0;
}

/**
 * @brief Whether two SCALEs say the same, seq aside.
 * @param a One.  @param b The other.
 * @return true when they do.
 */
static bool scale_eq(const dk_scale_t *a, const dk_scale_t *b)
{
    return a->rpm == b->rpm && a->mv == b->mv && a->ca == b->ca
           && a->travel == b->travel && a->us_min == b->us_min
           && a->us_max == b->us_max && a->rail_mv == b->rail_mv
           && a->rail_ca == b->rail_ca;
}

/**
 * @brief Number one packet and count it.
 * @param st     The scheduler.
 * @param out    The packet, kind in byte 0, written.
 * @param len    Its length; 0 for one the codec refused.
 * @param now_ms The clock.
 * @return @p len.
 */
static size_t sent(dial_stream_t *st, uint8_t *out, size_t len,
                   uint32_t now_ms)
{
    if (len == 0u) {
        return 0u;
    }
    ++st->sent[KIND_INDEX(out[0])];
    st->any = true;
    st->t_any = now_ms;
    return len;
}

/**
 * @brief SCREEN, when due.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_screen(dial_stream_t *st, const dial_model_t *m,
                          uint32_t now_ms, uint8_t *out)
{
    dk_screen_t s;
    memset(&s, 0, sizeof(s));
    s.face = m->face;
    s.lang = m->lang;
    s.flags = m->flags;
    (void)dk_text_copy(s.title, sizeof(s.title), m->title);
    if (st->screen_ok && s.face == st->screen.face
        && s.lang == st->screen.lang && s.flags == st->screen.flags
        && strcmp(s.title, st->screen.title) == 0
        && now_ms - st->t_screen < DK_SCREEN_MS) {
        return 0u;
    }
    /* A face entered sends its SCALE again. */
    if (!st->screen_ok || s.face != st->screen.face) {
        st->scale_ok = false;
    }
    st->screen = s;
    st->screen_ok = true;
    st->t_screen = now_ms;
    /* A new face's value and rows go again after it. */
    st->meta_ok = false;
    st->value_ok = false;
    st->nav_ok = 0u;
    s.seq = ++st->seq;
    return sent(st, out, dk_screen_encode(&s, out), now_ms);
}

/**
 * @brief VALUE_META or VALUE, when due.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_value(dial_stream_t *st, const dial_model_t *m,
                         uint32_t now_ms, uint8_t *out)
{
    if (!m->has_value) {
        return 0u;
    }
    dk_value_meta_t mt = m->meta;
    mt.seq = 0u;
    if (!st->meta_ok || !meta_eq(&mt, &st->meta)) {
        st->meta = mt;
        st->meta_ok = true;
        st->value_ok = false;
        mt.seq = ++st->seq;
        return sent(st, out, dk_value_meta_encode(&mt, out), now_ms);
    }
    dk_value_t v = m->value;
    v.seq = 0u;
    if (!st->value_ok || !value_eq(&v, &st->value)) {
        st->value = v;
        st->value_ok = true;
        v.seq = ++st->seq;
        return sent(st, out, dk_value_encode(&v, out), now_ms);
    }
    return 0u;
}

/**
 * @brief SCALE, when due: the first since the face or the reset, else a
 *        change no sooner than DK_STREAM_MS after the last, so a peak that
 *        climbs every sample does not crowd LIVE out.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_scale(dial_stream_t *st, const dial_model_t *m,
                         uint32_t now_ms, uint8_t *out)
{
    if (!m->has_scale) {
        return 0u;
    }
    dk_scale_t c = m->scale;
    c.seq = 0u;
    if (st->scale_ok
        && (scale_eq(&c, &st->scale) || now_ms - st->t_scale < DK_STREAM_MS)) {
        return 0u;
    }
    st->scale = c;
    st->scale_ok = true;
    st->t_scale = now_ms;
    c.seq = ++st->seq;
    return sent(st, out, dk_scale_encode(&c, out), now_ms);
}

/**
 * @brief One NAV row, when one of the three changed.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_nav(dial_stream_t *st, const dial_model_t *m,
                       uint32_t now_ms, uint8_t *out)
{
    if (!m->has_nav) {
        return 0u;
    }
    /* The focused row first: the Dial empties the others on its change. */
    static const uint8_t order[3] = { DK_SLOT_FOCUS, DK_SLOT_PREV,
                                      DK_SLOT_NEXT };
    for (unsigned k = 0; k < 3u; ++k) {
        const uint8_t slot = order[k];
        dk_nav_t n = m->nav[slot];
        n.seq = 0u;
        n.slot = slot;
        if ((st->nav_ok & (1u << slot)) != 0u
            && nav_eq(&n, &st->nav[slot])) {
            continue;
        }
        if (slot == DK_SLOT_FOCUS) {
            st->nav_ok = 0u;
        }
        st->nav[slot] = n;
        st->nav_ok |= (uint8_t)(1u << slot);
        n.seq = ++st->seq;
        return sent(st, out, dk_nav_encode(&n, out), now_ms);
    }
    return 0u;
}

/**
 * @brief LIVE_MOTOR or LIVE_SERVO, at DIAL_LIVE_MS.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_live(dial_stream_t *st, const dial_model_t *m,
                        uint32_t now_ms, uint8_t *out)
{
    const bool motor = m->face == DK_FACE_MOTOR && m->has_motor;
    const bool servo = m->face == DK_FACE_SERVO && m->has_servo;
    if ((!motor && !servo)
        || (st->live_any && now_ms - st->t_live < DIAL_LIVE_MS)) {
        return 0u;
    }
    st->live_any = true;
    st->t_live = now_ms;
    if (motor) {
        dk_live_motor_t lm = m->motor;
        lm.seq = ++st->seq;
        return sent(st, out, dk_live_motor_encode(&lm, out), now_ms);
    }
    dk_live_servo_t ls = m->servo;
    ls.seq = ++st->seq;
    return sent(st, out, dk_live_servo_encode(&ls, out), now_ms);
}

/**
 * @brief STATE, on a change, DK_STREAM_MS without any packet, or
 *        DIAL_STATE_MS after the last STATE.
 * @param st The scheduler.  @param m The model.  @param now_ms The clock.
 * @param degraded The link is DEGRADED.  @param out The packet.
 * @return Its length, 0 when not due.
 */
static size_t next_state(dial_stream_t *st, const dial_model_t *m,
                         uint32_t now_ms, bool degraded, uint8_t *out)
{
    dk_state_t s;
    memset(&s, 0, sizeof(s));
    s.thr_step = m->thr_tenths;
    s.srv_step = m->srv_tenths;
    s.flags = (uint8_t)((m->armed ? DK_STA_ARMED : 0u)
                        | (degraded ? DK_STA_DEGRADED : 0u));
    if (st->state_ok && s.thr_step == st->state.thr_step
        && s.srv_step == st->state.srv_step && s.flags == st->state.flags
        && st->any && now_ms - st->t_any < DK_STREAM_MS
        && now_ms - st->state_ms < DIAL_STATE_MS) {
        return 0u;
    }
    st->state = s;
    st->state_ok = true;
    st->state_ms = now_ms;
    st->state_sent = true;
    s.panel_ms = now_ms;
    s.seq = ++st->seq;
    return sent(st, out, dk_state_encode(&s, out), now_ms);
}

size_t dial_stream_next(dial_stream_t *st, const dial_model_t *model,
                        uint32_t now_ms, bool degraded, uint8_t *out)
{
    if (st == NULL || model == NULL || out == NULL || model->face == 0u) {
        return 0u;
    }
    dial_model_t m = *model;
    if (dial_model_clamp(&m)) {
        ++st->clamped;
    }
    size_t n = next_screen(st, &m, now_ms, out);
    if (n == 0u) {
        n = next_value(st, &m, now_ms, out);
    }
    if (n == 0u) {
        n = next_scale(st, &m, now_ms, out);
    }
    if (n == 0u) {
        n = next_nav(st, &m, now_ms, out);
    }
    if (n == 0u) {
        n = next_live(st, &m, now_ms, out);
    }
    if (n == 0u) {
        n = next_state(st, &m, now_ms, degraded, out);
    }
    return n;
}
