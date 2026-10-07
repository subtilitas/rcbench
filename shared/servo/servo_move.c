/*
 * One servo move judged from its current.  See servo_move.h for the rules.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_move.h"

#include <math.h>
#include <string.h>

float servo_move_noise_a(uint32_t n, float sum, float sum_sq)
{
    float var = 0.0f;
    if (n > 0u) {
        const float mean = sum / (float)n;
        var = sum_sq / (float)n - mean * mean;
    }
    return (var > 0.0f) ? sqrtf(var) : 0.0f;
}

float servo_move_threshold_a(float noise_a)
{
    const float k = SERVO_MOVE_NOISE_K * noise_a;
    return (k > SERVO_MOVE_MIN_A) ? k : SERVO_MOVE_MIN_A;
}

/*
 * The lag is added to the window rather than the window's last part left
 * unjudged: a servo that arrives at 2900 ms shows it on the PD mini at
 * about 3200 ms, and waiting for that reading times the move.  A move that
 * never arrives is still late, only 300 ms later; a window left unjudged
 * would need the same wait to tell the two apart.
 */
uint32_t servo_move_window_ms(uint32_t lag_ms)
{
    return SERVO_MOVE_TIMEOUT_MS + lag_ms;
}

void servo_move_begin(servo_move_t *m, const servo_move_cfg_t *cfg)
{
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    if (m->cfg.settle_n < 2u) {
        m->cfg.settle_n = 2u;
    }
    if (m->cfg.filter_n < 1u) {
        m->cfg.filter_n = 1u;
    } else if (m->cfg.filter_n > SERVO_MOVE_FILTER_MAX) {
        m->cfg.filter_n = (uint8_t)SERVO_MOVE_FILTER_MAX;
    }
    m->state = SERVO_MOVE_WAITING;
}

static bool under_way(const servo_move_t *m)
{
    return m->state == SERVO_MOVE_WAITING || m->state == SERVO_MOVE_MOVING;
}

/* A move under way whose window has run out at @p now is over: late with
 * movement, unseen without, ended at @p now.  A @p now before the command
 * decides nothing.  Whether it ran out. */
static bool expired(servo_move_t *m, uint32_t now)
{
    const uint32_t since = now - m->cfg.cmd_t;
    if ((int32_t)since < 0 || since < m->cfg.window_t) {
        return false;
    }
    m->state = (m->state == SERVO_MOVE_MOVING) ? SERVO_MOVE_LATE
                                               : SERVO_MOVE_UNSEEN;
    m->end_t = now;
    return true;
}

/* A sample into the filter's window of the newest filter_n. */
static void filter_push(servo_move_t *m, float a, servo_move_clip_t clip)
{
    const uint8_t len = m->cfg.filter_n;
    m->fbuf[m->f_head]  = a;
    m->fclip[m->f_head] = (int8_t)clip;
    m->f_head = (uint8_t)((m->f_head + 1u) % len);
    if (m->f_n < len) {
        ++m->f_n;
    }
}

void servo_move_prime(servo_move_t *m, float a, servo_move_clip_t clip)
{
    if (under_way(m) && m->cfg.filter_n > 1u) {
        filter_push(m, a, clip);
    }
}

/* The filtered sample, from the newest filter_n samples: false when the
 * window holds clips at both ends and so no bound. */
static bool filtered(servo_move_t *m, float a, servo_move_clip_t clip,
                     float *out, servo_move_clip_t *out_clip)
{
    filter_push(m, a, clip);
    float sum = 0.0f;
    bool hi = false;
    bool lo = false;
    for (unsigned k = 0; k < m->f_n; ++k) {
        sum += m->fbuf[k];
        hi = hi || m->fclip[k] > 0;
        lo = lo || m->fclip[k] < 0;
    }
    if (hi && lo) {
        return false;
    }
    *out = sum / (float)m->f_n;
    *out_clip = hi ? SERVO_MOVE_CLIP_HIGH
                   : (lo ? SERVO_MOVE_CLIP_LOW : SERVO_MOVE_CLIP_NONE);
    return true;
}

/* The samples in a row near the level with @p i added: their count, or 1
 * when @p i spreads them wider than the band and starts a new run. */
static uint8_t run_with(const servo_move_t *m, float i, float *lo, float *hi)
{
    if (m->run_n > 0u) {
        const float l = (i < m->run_lo) ? i : m->run_lo;
        const float h = (i > m->run_hi) ? i : m->run_hi;
        if (h - l <= m->cfg.band_a) {
            *lo = l;
            *hi = h;
            return (m->run_n < UINT8_MAX) ? (uint8_t)(m->run_n + 1u)
                                          : (uint8_t)UINT8_MAX;
        }
    }
    *lo = i;
    *hi = i;
    return 1u;
}

servo_move_state_t servo_move_sample(servo_move_t *m, uint32_t at, float a,
                                     servo_move_clip_t clip)
{
    if (!under_way(m)) {
        return m->state;
    }
    float i = a;
    servo_move_clip_t c = clip;
    bool valued = true;
    if (m->cfg.filter_n > 1u) {
        valued = filtered(m, a, clip, &i, &c);
    }
    /* A sample taken before the command is not the move; one at or past
     * the window's end is the move late or unseen, never its arrival. */
    if ((int32_t)(at - m->cfg.cmd_t) < 0 || expired(m, at)) {
        return m->state;
    }
    if (!valued) {
        m->run_n   = 0u;
        m->clipped = true;
        return m->state;
    }
    /*
     * What the sample says, against the level before the command (rise),
     * and the destination's (ref).  A clip says only what its bound says:
     * past the end of the range by an unknown amount, it is never near.
     */
    const float move_a = m->cfg.move_a;
    const float band_a = m->cfg.band_a;
    const float rise   = m->cfg.rise_a;
    const float ref    = m->cfg.ref_a;
    bool away;
    bool above;
    bool below;
    bool near;
    if (c == SERVO_MOVE_CLIP_NONE) {
        away  = fabsf(i - rise) > move_a;
        above = i > ref + move_a;
        below = i < ref - band_a;
        near  = fabsf(i - ref) <= band_a;
    } else if (c == SERVO_MOVE_CLIP_HIGH) {
        away  = i - rise > move_a;
        above = i > ref + move_a;
        below = false;
        near  = false;
    } else {
        away  = rise - i > move_a;
        above = false;
        below = i < ref - band_a;
        near  = false;
    }
    if (away && m->state == SERVO_MOVE_WAITING) {
        m->state   = SERVO_MOVE_MOVING;
        m->moved_t = at;
    }
    const bool rose = m->state == SERVO_MOVE_MOVING;

    /*
     * A servo moving draws more than it holds with, so the move is under
     * way once a sample, after movement, lies above the destination's
     * holding level, and has arrived when one falls back to it.  In that
     * order: the ends' holding levels can differ by more than the band,
     * and a sample still at the start end's level, or one passing the
     * destination's level on its way up, is not the move arriving.
     *
     * A destination held harder than the servo moves -- an end pushing on
     * a stop -- is never passed on the way up, or only by the first sample
     * of the acceleration, after which the moving current lies below the
     * level again.  There the move has arrived when, after movement,
     * settle_n samples in a row lie within the band of that level and of
     * each other: settled, at the first of them.  A current climbing
     * through the level steps more than the band between two samples
     * unless it climbs slower than the band a sample, and a moving current
     * within the band of the level cannot be told from the servo there.
     *
     * A sample above the level by the threshold is the move under way,
     * whatever the band says, so a band wider than the threshold times no
     * move early: the first sample back under the threshold is the
     * arrival.
     */
    float lo = 0.0f;
    float hi = 0.0f;
    const uint8_t run = (rose && near) ? run_with(m, i, &lo, &hi) : 0u;
    if (rose && above) {
        m->left = true;
    } else if (m->left && near) {
        m->state = SERVO_MOVE_ARRIVED;
        m->end_t = at;
        return m->state;
    } else if (m->left && below) {
        /* Back below the level without settling at it: the servo moves at
         * less than it holds there, and the settled rule decides. */
        m->left = false;
    } else if (!m->left && run >= m->cfg.settle_n) {
        m->state = SERVO_MOVE_SETTLED;
        m->end_t = m->run_t;   /* settle_n is 2 or more: the run goes on */
        return m->state;
    }
    if (run == 1u) {
        m->run_t = at;
    }
    m->run_n  = run;
    m->run_lo = lo;
    m->run_hi = hi;
    if (c == SERVO_MOVE_CLIP_NONE) {
        /* The peak from the first valued sample, so a move whose samples
         * all lie below 0 A reports the highest of them. */
        if (m->n == 0u || i > m->peak) {
            m->peak = i;
        }
        m->sum += i;
        ++m->n;
    } else {
        m->clipped = true;
    }
    return m->state;
}

servo_move_state_t servo_move_tick(servo_move_t *m, uint32_t now)
{
    if (under_way(m)) {
        (void)expired(m, now);
    }
    return m->state;
}

bool servo_move_over(const servo_move_t *m)
{
    return m->state != SERVO_MOVE_IDLE && !under_way(m);
}

bool servo_move_arrived(const servo_move_t *m)
{
    return m->state == SERVO_MOVE_ARRIVED || m->state == SERVO_MOVE_SETTLED;
}

bool servo_move_moved(const servo_move_t *m)
{
    return m->state == SERVO_MOVE_MOVING || m->state == SERVO_MOVE_LATE
           || servo_move_arrived(m);
}
