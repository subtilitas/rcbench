/*
 * The rules for asking a motor to spin.
 *
 * Inherited from the predecessor's motor_ramp, where it was the pure half of
 * a driver that also owned an LEDC channel.  Here it has no output at all: it
 * produces a number that becomes a register write, and the pin it eventually
 * reaches is on the other processor.
 *
 * Three rules, and all three exist because a bench that spins when nobody
 * asked is the failure that matters:
 *
 *   Throttle set while disarmed is remembered and not emitted.
 *   Disarming drops to idle with no ramp -- a slew limit on the way down is
 *   a slew limit on stopping.
 *   And it refuses to hold a throttle nobody is watching: if the loop that
 *   owns it goes quiet, it disarms itself.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    ramp_pct_per_s;    /**< slew limit on the way up            */
    uint32_t command_timeout_ms;/**< silence after which it disarms      */
} throttle_cfg_t;

#define THROTTLE_CFG_DEFAULT()        \
    (throttle_cfg_t) {                \
        .ramp_pct_per_s = 55.0f,      \
        .command_timeout_ms = 500,    \
    }

typedef struct {
    throttle_cfg_t cfg;
    bool     armed;
    float    command;      /**< what was asked for, 0..100               */
    float    actual;       /**< what the ramp has reached, 0..100        */
    uint32_t last_cmd_ms;
} throttle_t;

void  throttle_init(throttle_t *t, const throttle_cfg_t *cfg, uint32_t now_ms);
bool  throttle_set_rate(throttle_t *t, float pct_per_s);
void  throttle_arm(throttle_t *t, bool armed, uint32_t now_ms);
void  throttle_set(throttle_t *t, float pct, uint32_t now_ms);
void  throttle_keepalive(throttle_t *t, uint32_t now_ms);
bool  throttle_overdue(const throttle_t *t, uint32_t now_ms);

/** Advance the ramp by @p dt_s and return what should be emitted, 0..100. */
float throttle_step(throttle_t *t, float dt_s);

#ifdef __cplusplus
}
#endif
