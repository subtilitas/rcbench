/**
 * @file
 * @brief The schedule's move capture on the FAST channel, and FAST's history
 * the level before a command is taken from.
 *
 * See sense_sched.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_sched_int.h"

#include <string.h>

bool sense_cap_under_way(const sense_cap_t *c)
{
    return c->state == SENSE_CAP_ARMED || c->state == SENSE_CAP_WAITING
           || c->state == SENSE_CAP_MOVING;
}

bool sense_sched_cap_arm(sense_sched_t *s, const sense_cap_arm_t *levels)
{
    const ina3221_t *d = sense_sched_fast_part(s);
    if (d == NULL || ina3221_state(d) != SENSE_PART_ONLINE) {
        return false;
    }
    const uint16_t seq = s->cap.seq;
    memset(&s->cap, 0, sizeof(s->cap));
    s->cap.seq   = seq;
    s->cap.arm   = *levels;
    s->cap.arm_t = (uint32_t)(s->io.now_us(s->io.ctx) / 100u);
    s->cap.state = SENSE_CAP_ARMED;
    return true;
}

/**
 * @brief Convert µA to A.
 *
 * @param ua Current in µA.
 * @return Current in A.
 */
static float ua_to_a(int32_t ua)
{
    return (float)ua * 1e-6f;
}

/**
 * @brief Convert A to µA, rounded half away from zero.
 *
 * @param a Current in A.
 * @return Current in µA.
 */
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
    /* FAST's level is taken at the first sample past the edge; until then
     * the move judges nothing that reads it (servo_move_sample()). */
    c->rise_owed = (c->arm.rise_ua == SENSE_CAP_RISE_AUTO);
    const servo_move_cfg_t mc = {
        .cmd_t    = c->edge_t,
        .window_t = servo_move_window_ms(SENSE_CAP_LAG_MS) * SENSE_CAP_T_PER_MS,
        .rise_a   = c->rise_owed ? 0.0f : ua_to_a(c->arm.rise_ua),
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

/**
 * @brief The capture ends as @p state, with what the move measured.
 *
 * @param c The capture.
 * @param state The state the capture ends in.
 */
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

/**
 * @brief A clipped FAST sample at the end of the range it is at or past, as the
 * capture's filter takes it; a sample with a value as it is.
 *
 * Only called with a FAST channel.
 *
 * @param s The schedule.
 * @param v The FAST sample.
 * @return The current in µA.
 */
static int32_t fast_ua(const sense_sched_t *s, const sense_value_t *v)
{
    const int32_t clip = s->ch_clip_ua[s->cfg.i3221.fast];
    if (v->clip == SENSE_CLIP_HIGH) {
        return clip;
    }
    if (v->clip == SENSE_CLIP_LOW) {
        return -clip;
    }
    return v->value;
}

/**
 * @brief A FAST sample into the history the level before a command is taken
 * from.
 *
 * @param s The schedule.
 * @param v The FAST sample.
 * @param at_t Time of the sample, in 0.1ms.
 */
static void fast_keep(sense_sched_t *s, const sense_value_t *v, uint32_t at_t)
{
    s->hist[s->hist_head].t  = at_t;
    s->hist[s->hist_head].ua = fast_ua(s, v);
    s->hist_head = (uint8_t)((s->hist_head + 1u) % SENSE_FAST_HISTORY);
    if (s->hist_n < SENSE_FAST_HISTORY) {
        ++s->hist_n;
    }
}

/**
 * @brief The level before the command: the mean of the FAST samples stamped in
 * the SENSE_WINDOW_MS before the edge.
 *
 * False with none.
 *
 * @param s The schedule.
 * @return true when at least one sample lies in the window before the edge;
 *       false otherwise.
 */
static bool take_rise(sense_sched_t *s)
{
    sense_cap_t *c = &s->cap;
    const uint32_t span = SENSE_WINDOW_MS * SENSE_CAP_T_PER_MS;
    int64_t sum = 0;
    uint32_t n = 0u;
    for (unsigned k = 0; k < s->hist_n; ++k) {
        const uint32_t before = c->edge_t - s->hist[k].t;
        if ((int32_t)before > 0 && before <= span) {
            sum += s->hist[k].ua;
            ++n;
        }
    }
    if (n == 0u) {
        return false;
    }
    c->arm.rise_ua    = (int32_t)(sum / (int64_t)n);
    c->mv.cfg.rise_a  = ua_to_a(c->arm.rise_ua);
    c->rise_owed      = false;
    return true;
}

/**
 * @brief A FAST sample, or none, and the clock: both at @p at_t.
 *
 * @param s The schedule.
 * @param v FAST's sample, or NULL when there is none.
 * @param at_t Time of the step, in 0.1ms.
 */
static void cap_step(sense_sched_t *s, const sense_value_t *v, uint32_t at_t)
{
    sense_cap_t *c = &s->cap;
    if (!sense_cap_under_way(c)) {
        return;
    }
    const ina3221_t *d = sense_sched_fast_part(s);
    if (d == NULL || ina3221_state(d) != SENSE_PART_ONLINE) {
        cap_end(c, SENSE_CAP_LOST);
        return;
    }
    servo_move_clip_t clip = SERVO_MOVE_CLIP_NONE;
    float a = 0.0f;
    if (v != NULL) {
        a = ua_to_a(fast_ua(s, v));
        if (v->clip == SENSE_CLIP_HIGH) {
            clip = SERVO_MOVE_CLIP_HIGH;
        } else if (v->clip == SENSE_CLIP_LOW) {
            clip = SERVO_MOVE_CLIP_LOW;
        }
    }
    if (c->state == SENSE_CAP_ARMED) {
        /* No edge in all this time: nothing renders the frame it waits
         * for, and it is not left armed for ever. */
        if ((int32_t)(at_t - c->arm_t)
            >= (int32_t)(SENSE_CAP_EDGE_WAIT_MS * SENSE_CAP_T_PER_MS)) {
            cap_end(c, SENSE_CAP_LOST);
            return;
        }
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
    /* The first sample at or past the edge is the first that can be
     * judged against the level before the command, so the level is taken
     * now, from the samples before the edge. */
    if (c->rise_owed && v != NULL && (int32_t)(at_t - c->edge_t) >= 0
        && !take_rise(s)) {
        cap_end(c, SENSE_CAP_LOST);
        return;
    }
    /* A sample at or past the deadline is the move late or unseen, never
     * its arrival (servo_move_sample()); without one, the clock ends it. */
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

void sense_cap_step(sense_sched_t *s, const sense_value_t *v, uint32_t at_t)
{
    /* The capture on FAST's own time, whatever came after it; then the
     * sample into the history, so the level before a command is taken
     * from samples before it. */
    cap_step(s, v, at_t);
    if (v != NULL) {
        fast_keep(s, v, at_t);
    }
}
