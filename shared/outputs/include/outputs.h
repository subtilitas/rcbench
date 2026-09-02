/*
 * The intermediary every output goes through: one set of rules for arming,
 * clamping, slew and the silence timeout, with a protocol as a driver behind
 * them.
 *
 *   A channel is a normalised command and a role.  Screens and procedures
 *   write channels and do not know what is on the wire.
 *
 *   A driver declares its shape: how many channels one of it consumes, how
 *   many pins, and whether the pin is also an input.  PPM (pulse-position
 *   modulation) is eight channels on one pin, and bidirectional DShot listens
 *   on the pin it drives.
 *
 *   Arming, clamping, slew and where to go when the link goes quiet are
 *   answered here for every output.  Only turning a command into edges is
 *   per-protocol, on the coprocessor.
 *
 * The unit is a thousandth of the channel's own span.  Microseconds mean
 * nothing to DShot and a DShot code means nothing to a PWM (pulse-width
 * modulation) servo; both have a proportion of travel, and 1000 steps is
 * finer than any of these protocols resolve.
 *
 * Refused and clamped are different answers.  An endpoint outside 500 to
 * 2500 us is refused: it is a configuration mistake, and accepting it leaves
 * the range looking set when it is not.  A command outside its span is
 * clamped: commands arrive many times a second from a host that may be
 * mid-drag, and an output that stops at its stop is what a stop is for.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A command runs 0..OUT_SPAN across the channel's own travel. */
#define OUT_SPAN            1000u

#define OUT_MAX_CHANNELS    16u
#define OUT_MAX_SLOTS        8u

/** What a pulse driver may render, in microseconds.  Endpoints outside this
 *  are refused: no servo made takes them, so they are a typo, not a range. */
#define OUT_FLOOR_US       500u
#define OUT_CEILING_US    2500u

/**
 * What rest means for a channel, which is the one thing a command cannot say.
 *
 * A throttle at rest is stopped, and its slew is asymmetric: going up is
 * ramped and coming down is immediate, because reducing throttle is the safe
 * direction.  A surface at rest is centred, and its slew is symmetric,
 * because for a surface neither direction is the safe one.
 */
typedef enum {
    OUT_ROLE_THROTTLE = 0,
    OUT_ROLE_SURFACE  = 1,
} out_role_t;

typedef enum {
    OUT_DRIVER_NONE = 0,
    OUT_DRIVER_PWM,      /**< one channel, one pin, a pulse per frame      */
    OUT_DRIVER_PPM,      /**< several channels multiplexed onto one pin    */
    OUT_DRIVER_DSHOT,    /**< one channel, one pin, an 11-bit code         */
    OUT_DRIVER_COUNT
} out_driver_t;

/**
 * What a driver is shaped like.
 *
 * The channel count is a range rather than a number because PPM's is chosen
 * when it is configured.
 */
typedef struct {
    const char *name;
    uint8_t     channels_min;
    uint8_t     channels_max;
    uint8_t     pins;
    bool        reads_back;   /**< the pin is not purely an output         */
    bool        pulsed;       /**< renders through the channel's endpoints */
    uint16_t    rate_min_hz;
    uint16_t    rate_max_hz;
} out_driver_def_t;

/** The table itself, so callers can ask rather than assume. */
const out_driver_def_t *out_driver(out_driver_t d);

typedef struct {
    out_role_t role;
    uint16_t   command;      /**< asked for, 0..OUT_SPAN                   */
    uint16_t   actual;       /**< what the slew has reached                */
    uint16_t   rest;         /**< where it goes when it is not driving     */
    uint16_t   slew_per_s;   /**< span units per second; 0 is immediate    */
    uint16_t   min_us;       /**< what a pulse driver renders 0 as         */
    uint16_t   max_us;       /**< and OUT_SPAN                             */
    uint32_t   last_command_ms;
} out_channel_t;

typedef struct {
    out_driver_t driver;
    uint8_t      first_channel;
    uint8_t      channels;
    uint8_t      pin;
    uint16_t     rate_hz;
} out_slot_t;

typedef struct {
    out_channel_t channel[OUT_MAX_CHANNELS];
    out_slot_t    slot[OUT_MAX_SLOTS];
    bool          armed;
    uint32_t      timeout_ms;   /**< silence after which it stops driving  */
    uint32_t      last_step_ms;
} outputs_t;

/** Silence after which an output stops driving. */
#define OUT_DEFAULT_TIMEOUT_MS  500u

void outputs_init(outputs_t *o, uint32_t now_ms);

/**
 * Configure one slot, or clear it with OUT_DRIVER_NONE.
 *
 * Refuses a driver it does not know, a channel count the driver cannot take,
 * a channel range that runs off the end, a rate the driver cannot produce, a
 * pin another slot is already driving, and a channel another slot is already
 * rendering.
 */
bool outputs_configure(outputs_t *o, uint8_t slot, const out_slot_t *cfg);

/** Endpoints, refused rather than clamped.  Inverted pairs are straightened:
 *  an inverted pair is a mistake rather than a range, and it would make the
 *  clamp unsatisfiable. */
bool outputs_set_endpoints(outputs_t *o, uint8_t ch, uint16_t min_us,
                           uint16_t max_us);
bool outputs_set_role(outputs_t *o, uint8_t ch, out_role_t role);
bool outputs_set_slew(outputs_t *o, uint8_t ch, uint16_t per_s);

/** Command a channel.  Clamped, and remembered even while disarmed. */
bool outputs_set(outputs_t *o, uint8_t ch, uint16_t command, uint32_t now_ms);

/** Arming is decided by the end holding the wire, not by the asker.
 *  Disarming goes to rest with no ramp.
 *
 *  Re-asserting the current state is not an event and does nothing, so the
 *  call is safe every pass from a loop that recomputes whether driving is
 *  allowed.  Stamping the clock on every call would hold every channel alive
 *  and defeat the timeout. */
void outputs_arm(outputs_t *o, bool armed, uint32_t now_ms);

/**
 * Has this channel stopped being commanded?
 *
 * Per channel rather than per bank, because the pages that write them are
 * written independently: a servo being dragged does not keep a throttle
 * alive, and a throttle nobody has touched must not stop a servo.
 */
bool outputs_overdue(const outputs_t *o, uint8_t ch, uint32_t now_ms);

/**
 * Say a channel is still being watched without changing what it was asked.
 *
 * A loop that is running with nothing new to say differs from one that has
 * stopped, and the timeout exists to tell those apart.  Re-sending the last
 * command would also overwrite a command that arrived from elsewhere in the
 * same pass.
 */
bool outputs_keepalive(outputs_t *o, uint8_t ch, uint32_t now_ms);

/** Whether the bank is armed, for a screen that shows it. */
bool outputs_armed(const outputs_t *o);

/** Advance slew to @p now_ms, and stop driving if nobody has commanded. */
void outputs_step(outputs_t *o, uint32_t now_ms);

/** Everything to rest, immediately.  The failsafe edge calls this, and it
 *  reaches every output because every output is in this bank. */
void outputs_all_off(outputs_t *o);

uint16_t outputs_actual(const outputs_t *o, uint8_t ch);

/** What a pulse driver should emit for @p ch, in microseconds. */
uint16_t outputs_pulse_us(const outputs_t *o, uint8_t ch);

/** True while the bank is armed and being commanded: what a driver asks
 *  before it puts an edge on a pin. */
bool outputs_driving(const outputs_t *o);

#ifdef __cplusplus
}
#endif
