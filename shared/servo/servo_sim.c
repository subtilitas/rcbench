#include "servo_sim.h"

#include <string.h>

void servo_sim_defaults(servo_sim_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->stop_lo_us     = 1150;
    cfg->stop_hi_us     = 1880;
    cfg->free_a         = 0.12f;
    cfg->stall_a        = 2.4f;
    cfg->bind_us        = 45;
    cfg->travel_a       = 0.95f;  /* about 40% of stall, while moving */
    cfg->slew_us_per_ms = 1.2f;   /* ~0.17 s for 60 degrees */
    cfg->noise_a        = 0.02f;
}

void servo_sim_init(servo_sim_t *s, const servo_sim_cfg_t *cfg)
{
    if (s == NULL || cfg == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->cfg  = *cfg;
    s->seed = 0x5A17C0DEu;
    s->position_us = (float)((cfg->stop_lo_us + cfg->stop_hi_us) / 2u);
}

/* Deterministic, so a failing case is a failing case again next time. */
static float noise(servo_sim_t *s)
{
    s->seed = s->seed * 1664525u + 1013904223u;
    const float unit = (float)((s->seed >> 8) & 0xFFFFu) / 65535.0f; /* 0..1 */
    return (unit - 0.5f) * s->cfg.noise_a;
}

float servo_sim_step(servo_sim_t *s, uint16_t cmd_us, uint32_t now_ms)
{
    if (s == NULL) {
        return 0.0f;
    }
    if (!s->started) {
        s->started = true;
        s->last_ms = now_ms;
    }

    const uint32_t dt_ms = (uint32_t)(now_ms - s->last_ms);
    s->last_ms = now_ms;

    /*
     * The horn travels toward the command at a finite rate, and it cannot go
     * past the stop however hard it is asked to -- so the target it actually
     * tracks is the command clamped to the linkage's range.  Clamping here
     * rather than leaving the horn to chase an impossible number is what
     * separates "still travelling" from "arrived and pushing", and those two
     * draw very different currents.
     */
    float target = (float)cmd_us;
    if (target > (float)s->cfg.stop_hi_us) {
        target = (float)s->cfg.stop_hi_us;
    } else if (target < (float)s->cfg.stop_lo_us) {
        target = (float)s->cfg.stop_lo_us;
    }
    const float reach  = s->cfg.slew_us_per_ms * (float)dt_ms;
    if (target > s->position_us) {
        s->position_us += reach;
        if (s->position_us > target) {
            s->position_us = target;
        }
    } else {
        s->position_us -= reach;
        if (s->position_us < target) {
            s->position_us = target;
        }
    }

    /*
     * How far past its stop the servo is being asked to go.  The horn cannot
     * actually get there -- it is against something -- but the *command* is
     * what sets how hard it pushes, which is why this is measured from cmd_us
     * and not from position_us.  A servo told to go somewhere it cannot reach
     * keeps trying, and that is the whole phenomenon being searched for.
     */
    float over = 0.0f;
    if (cmd_us > s->cfg.stop_hi_us) {
        over = (float)(cmd_us - s->cfg.stop_hi_us);
    } else if (cmd_us < s->cfg.stop_lo_us) {
        over = (float)(s->cfg.stop_lo_us - cmd_us);
    }

    const bool arrived = (s->position_us == target);

    float amps;
    if (!arrived) {
        /*
         * Still moving, so it is not pushing on anything yet whatever it has
         * been told.  A finder that reads here reads travel current and calls
         * it a baseline -- or calls it a bind.
         */
        amps = s->cfg.travel_a;
    } else if (over > 0.0f) {
        float t = over / (float)(s->cfg.bind_us == 0 ? 1 : s->cfg.bind_us);
        if (t > 1.0f) {
            t = 1.0f;
        }
        amps = s->cfg.free_a + (s->cfg.stall_a - s->cfg.free_a) * t;
    } else {
        amps = s->cfg.free_a;
    }

    /* A tight spot costs extra to hold, whether or not anything is bound. */
    if (arrived && s->cfg.stiff_hi_us > s->cfg.stiff_lo_us
        && cmd_us >= s->cfg.stiff_lo_us && cmd_us <= s->cfg.stiff_hi_us) {
        amps += s->cfg.stiff_a;
    }

    amps += noise(s);
    return amps < 0.0f ? 0.0f : amps;
}
