#include "throttle.h"

#include <string.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void throttle_init(throttle_t *t, const throttle_cfg_t *cfg, uint32_t now_ms)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->cfg = (cfg != NULL) ? *cfg : (throttle_cfg_t)THROTTLE_CFG_DEFAULT();
    if (!(t->cfg.ramp_pct_per_s > 0.0f)) {
        t->cfg.ramp_pct_per_s = 55.0f;
    }
    t->last_cmd_ms = now_ms;
}

bool throttle_set_rate(throttle_t *t, float pct_per_s)
{
    if (t == NULL || !(pct_per_s > 0.0f)) {
        return false;
    }
    t->cfg.ramp_pct_per_s = pct_per_s;
    return true;
}

void throttle_arm(throttle_t *t, bool armed, uint32_t now_ms)
{
    if (t == NULL) {
        return;
    }
    if (!armed) {
        /*
         * Straight to idle, no ramp.  A slew limit on the way down is a slew
         * limit on stopping, and the whole reason a stop exists is that
         * somebody wants it to have happened already.
         */
        t->actual = 0.0f;
    }
    t->armed = armed;
    t->last_cmd_ms = now_ms;
}

void throttle_set(throttle_t *t, float pct, uint32_t now_ms)
{
    if (t == NULL) {
        return;
    }
    /* Remembered while disarmed, so arming does not lose where the slider was
     * -- but never emitted; throttle_step decides that. */
    t->command = clampf(pct, 0.0f, 100.0f);
    t->last_cmd_ms = now_ms;
}

void throttle_keepalive(throttle_t *t, uint32_t now_ms)
{
    if (t != NULL) {
        t->last_cmd_ms = now_ms;
    }
}

bool throttle_overdue(const throttle_t *t, uint32_t now_ms)
{
    if (t == NULL) {
        return true;
    }
    /* Wrap-safe: a bench that stops failing safe after 49 days of uptime
     * would be a memorable bug. */
    return (uint32_t)(now_ms - t->last_cmd_ms) >= t->cfg.command_timeout_ms;
}

float throttle_step(throttle_t *t, float dt_s)
{
    if (t == NULL) {
        return 0.0f;
    }
    if (!t->armed) {
        t->actual = 0.0f;
        return 0.0f;
    }
    if (!(dt_s > 0.0f)) {
        return t->actual;
    }

    const float step = t->cfg.ramp_pct_per_s * dt_s;
    if (t->command > t->actual) {
        t->actual += step;
        if (t->actual > t->command) {
            t->actual = t->command;
        }
    } else if (t->command < t->actual) {
        /* Down is not ramped either: reducing throttle is the safe direction
         * and making it slow helps nobody. */
        t->actual = t->command;
    }
    return t->actual;
}
