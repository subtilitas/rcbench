#include "servo_sync.h"

#include <string.h>

static inline bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

void servo_sync_defaults(servo_sync_cfg_t *cfg, uint16_t centre_us)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->centre_us     = centre_us;
    cfg->deflection_us = 300;
    /*
     * Forty microseconds each way is about a degree, which is far more
     * disagreement than an installation anybody would fly -- and the scan
     * narrows fast, so a window wider than it needs to be costs one round
     * rather than proportional time.
     */
    cfg->window_us     = 40;
    cfg->points        = 7;
    cfg->rounds        = 3;
    cfg->settle_ms     = 120;
    /* A standard servo is about 1.2 us/ms unloaded; assume rather less. */
    cfg->slew_us_per_ms = 0.8f;
    cfg->measure_ms    = 100;
    cfg->min_depth_a   = 0.08f;
    cfg->hard_limit_a  = 4.0f;   /* two servos, so higher than one servo's */
}

/* Where in its window the scan's @p i-th point sits. */
static int32_t point_us(const servo_sync_t *sy, uint8_t i)
{
    const uint8_t n = sy->cfg.points;
    if (n <= 1) {
        return sy->win_centre_us;
    }
    /* Spread across the full window, ends included, so the first round can
     * reach the edge of the range it claims to cover. */
    const int32_t span = sy->win_half_us * 2;
    return sy->win_centre_us - sy->win_half_us
           + (span * (int32_t)i) / (int32_t)(n - 1);
}

/* Where the servos should be for the stage being measured. */
static void aim(servo_sync_t *sy)
{
    const int32_t centre = (int32_t)sy->cfg.centre_us;
    const int32_t defl   = (int32_t)sy->cfg.deflection_us;
    const int32_t under  = point_us(sy, sy->point);
    const int32_t was_a  = (int32_t)sy->cmd_a_us;
    const int32_t was_b  = (int32_t)sy->cmd_b_us;

    switch (sy->stage) {
    case SERVO_SYNC_STAGE_CENTRE:
        sy->cmd_a_us = (uint16_t)centre;
        sy->cmd_b_us = (uint16_t)(centre + under);
        break;
    case SERVO_SYNC_STAGE_HIGH:
        sy->cmd_a_us = (uint16_t)(centre + defl);
        /* The trim found at centre is already applied; what is being searched
         * here is the throw on top of it, which is what a travel adjustment
         * is. Searching the two together would be searching a plane for a
         * point that two lines already give. */
        sy->cmd_b_us = (uint16_t)(centre + sy->trim_us + under);
        break;
    case SERVO_SYNC_STAGE_LOW:
    default:
        sy->cmd_a_us = (uint16_t)(centre - defl);
        sy->cmd_b_us = (uint16_t)(centre + sy->trim_us - under);
        break;
    }

    /* How long the larger of the two moves needs, added to the fixed wait. */
    int32_t da = (int32_t)sy->cmd_a_us - was_a;
    int32_t db = (int32_t)sy->cmd_b_us - was_b;
    if (da < 0) {
        da = -da;
    }
    if (db < 0) {
        db = -db;
    }
    const int32_t moved = (da > db) ? da : db;
    const float   slew  = (sy->cfg.slew_us_per_ms > 0.0f)
                              ? sy->cfg.slew_us_per_ms
                              : 1.0f;
    sy->travel_ms = (uint32_t)((float)moved / slew);
}

void servo_sync_init(servo_sync_t *sy, const servo_sync_cfg_t *cfg)
{
    if (sy == NULL || cfg == NULL) {
        return;
    }
    memset(sy, 0, sizeof(*sy));
    sy->cfg   = *cfg;
    if (sy->cfg.points > SERVO_SYNC_MAX_POINTS) {
        sy->cfg.points = SERVO_SYNC_MAX_POINTS;
    }
    if (sy->cfg.points < 3) {
        sy->cfg.points = 3;   /* two points have no interior minimum */
    }
    sy->state         = SERVO_SYNC_RUNNING;
    sy->stage         = SERVO_SYNC_STAGE_CENTRE;
    sy->win_centre_us = 0;
    sy->win_half_us   = sy->cfg.window_us;
    /* The high and low stages search a throw rather than an offset, so their
     * window is centred on the deflection itself. Set when they start. */
    sy->travel_hi_us  = (int16_t)cfg->deflection_us;
    sy->travel_lo_us  = (int16_t)cfg->deflection_us;
    aim(sy);
}

static void finish(servo_sync_t *sy, servo_sync_state_t state,
                   servo_sync_fault_t fault)
{
    sy->state    = state;
    sy->fault    = fault;
    sy->cmd_a_us = sy->cfg.centre_us;
    sy->cmd_b_us = (uint16_t)((int32_t)sy->cfg.centre_us + sy->trim_us);
}

/* Start the next stage, or finish if that was the last. */
static void next_stage(servo_sync_t *sy)
{
    sy->round = 0;
    sy->point = 0;

    switch (sy->stage) {
    case SERVO_SYNC_STAGE_CENTRE:
        sy->stage         = SERVO_SYNC_STAGE_HIGH;
        sy->win_centre_us = (int32_t)sy->cfg.deflection_us;
        sy->win_half_us   = sy->cfg.window_us;
        break;
    case SERVO_SYNC_STAGE_HIGH:
        sy->stage         = SERVO_SYNC_STAGE_LOW;
        sy->win_centre_us = (int32_t)sy->cfg.deflection_us;
        sy->win_half_us   = sy->cfg.window_us;
        break;
    default:
        finish(sy, SERVO_SYNC_DONE, SERVO_SYNC_FAULT_NONE);
        return;
    }
    aim(sy);
}

/* A whole round of points has been measured; narrow, or take the answer. */
static void close_round(servo_sync_t *sy)
{
    uint8_t best = 0;
    float lo = sy->scan_a[0];
    float hi = sy->scan_a[0];
    for (uint8_t i = 1; i < sy->cfg.points; ++i) {
        if (sy->scan_a[i] < lo) {
            lo   = sy->scan_a[i];
            best = i;
        }
        if (sy->scan_a[i] > hi) {
            hi = sy->scan_a[i];
        }
    }

    /*
     * The depth test is applied on the *first* round only, where the window
     * is widest and a real disagreement therefore shows its largest spread.
     * Later rounds are narrow by construction and would fail a depth test for
     * the good reason that they are already near the bottom.
     */
    if (sy->round == 0) {
        sy->best_a[sy->stage]  = lo;
        sy->worst_a[sy->stage] = hi;
        if (hi - lo < sy->cfg.min_depth_a) {
            finish(sy, SERVO_SYNC_FAULT, SERVO_SYNC_FAULT_NO_MINIMUM);
            return;
        }
    } else if (lo < sy->best_a[sy->stage]) {
        sy->best_a[sy->stage] = lo;
    }

    const int32_t at = point_us(sy, best);
    ++sy->round;

    if (sy->round >= sy->cfg.rounds) {
        switch (sy->stage) {
        case SERVO_SYNC_STAGE_CENTRE: sy->trim_us      = (int16_t)at; break;
        case SERVO_SYNC_STAGE_HIGH:   sy->travel_hi_us = (int16_t)at; break;
        default:                      sy->travel_lo_us = (int16_t)at; break;
        }
        next_stage(sy);
        return;
    }

    /*
     * Narrow around the best point.  The new half-window is one step of the
     * round just finished, so the next round covers the interval between the
     * winner's neighbours -- which is exactly the region the minimum can be
     * in, and no more.
     */
    sy->win_centre_us = at;
    sy->win_half_us   = (sy->win_half_us * 2) / (int32_t)(sy->cfg.points - 1);
    if (sy->win_half_us < 1) {
        sy->win_half_us = 1;
    }
    sy->point = 0;
    aim(sy);
}

void servo_sync_step(servo_sync_t *sy, float total_a, uint32_t now_ms,
                     uint16_t *cmd_a, uint16_t *cmd_b)
{
    if (sy == NULL) {
        return;
    }
    if (sy->state == SERVO_SYNC_RUNNING) {
        if (!sy->started) {
            sy->started        = true;
            sy->phase_start_ms = now_ms;
            sy->measuring      = false;
        }

        /* Two servos fighting is exactly the condition that draws a lot of
         * current, so the ceiling is checked every sample here for the same
         * reason it is in the limit search. */
        if (total_a >= sy->cfg.hard_limit_a) {
            finish(sy, SERVO_SYNC_FAULT, SERVO_SYNC_FAULT_OVERCURRENT);
        } else if (!sy->measuring) {
            if (elapsed(now_ms, sy->phase_start_ms,
                        (uint32_t)sy->cfg.settle_ms + sy->travel_ms)) {
                sy->measuring      = true;
                sy->phase_start_ms = now_ms;
                sy->sum_a          = 0.0;
                sy->samples        = 0;
            }
        } else {
            sy->sum_a += (double)total_a;
            ++sy->samples;
            if (elapsed(now_ms, sy->phase_start_ms, sy->cfg.measure_ms)
                && sy->samples > 0) {
                sy->scan_a[sy->point] =
                    (float)(sy->sum_a / (double)sy->samples);
                sy->measuring      = false;
                sy->phase_start_ms = now_ms;
                ++sy->point;
                if (sy->point >= sy->cfg.points) {
                    close_round(sy);
                } else {
                    aim(sy);
                }
            }
        }
    }

    if (cmd_a != NULL) {
        *cmd_a = sy->cmd_a_us;
    }
    if (cmd_b != NULL) {
        *cmd_b = sy->cmd_b_us;
    }
}

float servo_sync_progress(const servo_sync_t *sy)
{
    if (sy == NULL) {
        return 0.0f;
    }
    if (sy->state != SERVO_SYNC_RUNNING) {
        return 1.0f;
    }
    const uint32_t per   = (uint32_t)sy->cfg.rounds * sy->cfg.points;
    const uint32_t total = per * (uint32_t)SERVO_SYNC_STAGE_COUNT;
    if (total == 0) {
        return 0.0f;
    }
    const uint32_t done = (uint32_t)sy->stage * per
                          + (uint32_t)sy->round * sy->cfg.points
                          + sy->point;
    return (float)done / (float)total;
}
