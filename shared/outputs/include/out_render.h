/**
 * @file
 * @brief The output bank rendered onto pins: which slot and port holds
 *        which pin, in which order they are let go and taken, what each
 *        pass puts on them, and whose telemetry the bench publishes.
 *
 * outputs.h answers what every output should be doing and knows no pin.
 * This module turns that answer into calls on the pin drivers, and holds the
 * rules that order those calls:
 *
 * - every slot that moved is let go before any slot is bound, and every
 *   port that moved before any port is bound, so two outputs may exchange
 *   pins;
 * - a slot that runs on PIO (programmable input/output: DShot, PPM) and is
 *   bound anew first lets go every DShot programming port, so the slots
 *   take the state machines before the ports;
 * - a slot bound as PWM first lets go every SERVO PWM port on its PWM slice,
 *   because the two channels of one slice run at one rate;
 * - a bidirectional DShot slot sends DSHOT_CMD_EDT_ENABLE, with the
 *   telemetry bit set, in place of a stop frame and never in place of a
 *   throttle (dshot_edt_frame() says when);
 * - the published readings are those of the lowest-numbered bound
 *   bidirectional slot (out_render_reporting_slot()).
 *
 * The pins and the clocks are reached through the out_render_hw_*()
 * functions declared below.  This module calls them and does not define
 * them: the IOmcu firmware defines them over its pin drivers
 * (firmware/iomcu/src/outputs_hw.c), and a host test defines fakes.  They
 * are plain functions rather than a table of pointers, so a stack analysis
 * of the image follows every call.
 *
 * Pure C, no pico-sdk.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_OUT_RENDER_H
#define RCBENCH_OUT_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "dshot.h"
#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How often an ESC (electronic speed controller) is told what to do,
 *        in frames a second.
 *
 * The OUTPUTS page's rate field is a bit rate for DShot, not a frame rate, so
 * this is the one output timing the wire does not carry.  1kHz is twenty
 * times the IOmcu's 50Hz sampling, fifty times the panel's 20Hz poll, and
 * inside every ESC's own signal timeout.  For bidirectional DShot it is also
 * the telemetry rate: 1,000 speed readings a second.
 *
 * The rate is held by the IOmcu's main loop, not by a timer: a frame sent
 * more than OUT_RENDER_DSHOT_PERIOD_US - OUT_RENDER_DSHOT_MIN_US (500us)
 * late moves the grid later by the excess, and a pass longer than a period
 * costs the frames due inside it.
 */
#define OUT_RENDER_DSHOT_HZ         1000u

/**
 * @brief The time between two DShot due times of one pin, in microseconds.
 *
 * The due times are a fixed grid: each is the one before it plus this
 * period, not the pass that sent a frame plus this period.  A frame goes out
 * on the first pass at or after its due time, so a late pass delays its own
 * frame and no later one, and the frames average OUT_RENDER_DSHOT_HZ for as
 * long as no frame is late by more than OUT_RENDER_DSHOT_MIN_US.
 */
#define OUT_RENDER_DSHOT_PERIOD_US  (1000000u / OUT_RENDER_DSHOT_HZ)

/**
 * @brief The least time from one DShot frame of a pin to the next due time,
 *        in microseconds: half a period.
 *
 * A frame sent more than this late would otherwise leave its successor due
 * less than this after it, and a frame sent after a stall of a whole period
 * or more would leave every missed due time due at once, a burst of frames
 * one pass apart.  The next due time is then this long after the late frame,
 * and the grid runs on from there.
 *
 * 500us against the longest DShot exchange this bench runs: a bidirectional
 * DShot150 frame of 16 bits at 6.67us is 107us, the ESC's turnaround about
 * 30us and its 21-bit reply at 187.5kbit/s 112us, 249us in all.  On a pin
 * the gap can read shorter than this by the time between the due test and
 * the frame reaching the PIO (programmable input/output) FIFO (first in
 * first out): an interrupt taken in between delays the frame and not the
 * grid.
 */
#define OUT_RENDER_DSHOT_MIN_US     (OUT_RENDER_DSHOT_PERIOD_US / 2u)

/**
 * @brief The most channels one PPM frame carries: the bank's limit for a
 *        PPM slot, held equal to PPM_MAX_CHANNELS by the IOmcu firmware.
 */
#define OUT_RENDER_PPM_MAX          8u

/**
 * @brief Render state of one slot or port: its DShot schedule, extended
 *        telemetry and the last PWM pulse.
 */
typedef struct {
    bool        bound;     /**< the driver holds the pin */
    uint32_t    next_us;   /**< DShot only: when the next frame is due */
    /**
     * @brief Extended telemetry, which an ESC sends only after it has been
     *        asked: when to ask, when to stop, and whether replies read as
     *        extended frames (dshot_edt_*()).  Bidirectional slots only.
     */
    dshot_edt_t edt;
    bool        pwm_have;  /**< PWM only: a pulse was rendered since the bind */
    uint16_t    pwm_last;  /**< PWM only: the pulse rendered on the last pass */
} out_render_slot_t;

/**
 * @brief A programming port: a pin of its own on the same generator,
 *        rendered from one channel of the bank.
 *
 * @c as is the port as a slot of one PWM or DShot channel, so the rendering
 * and the capture's edge are the slots' code.
 */
typedef struct {
    bool              bound;   /**< the pin is held: bound, or parked */
    bool              parked;  /**< FUNC NONE: an input with its pull-down */
    out_slot_t        as;      /**< driver PWM, or DSHOT for a DShot port */
    out_render_slot_t st;      /**< the port's render state */
} out_render_port_t;

/**
 * @brief The last readings one ESC gave, with the clock each arrived on.
 *
 * Each kind keeps its own clock: an ESC interleaves the extended frames
 * between eRPM ones, so temperature arrives far less often than speed.
 */
typedef struct {
    bool     have_erpm;                       /**< a speed reading arrived */
    uint32_t erpm;                            /**< electrical rpm */
    uint32_t erpm_ms;                         /**< its arrival, in ms */
    bool     have_edt[DSHOT_TELEM_KINDS];     /**< a reading of each kind arrived */
    uint16_t edt[DSHOT_TELEM_KINDS];          /**< each kind's payload */
    uint32_t edt_ms[DSHOT_TELEM_KINDS];       /**< each kind's arrival, in ms */
} out_render_telem_t;

/** @brief Everything the renderer keeps between passes. */
typedef struct {
    out_slot_t         shadow[OUT_MAX_SLOTS];   /**< each slot as bound */
    out_render_slot_t  slot[OUT_MAX_SLOTS];     /**< each slot's render state */
    out_render_port_t  port[LINK_PG_PORTS_MAX]; /**< the programming ports */
    uint8_t            port_drive;  /**< ports that render their pulse, bit n */
    int                watch_ch;    /**< channel watched for an edge, -1 none */
    bool               edge_ready;  /**< an edge was stamped and not taken */
    uint64_t           edge_us;     /**< the stamped edge, in microseconds */
    /**
     * @brief Per slot, not per bank: one set shared between bidirectional
     *        slots would publish a speed from one motor beside a voltage from
     *        another.
     */
    out_render_telem_t telem[OUT_MAX_SLOTS];
    bool               was_driving; /**< the bank drove on the last pass */
} out_render_t;

/**
 * @name Pin and clock calls
 * Declared here, defined by the caller of this module.
 * @{
 */

/**
 * @brief Bind @p s->pin to the driver @p s->driver names, at @p s->rate_hz
 *        (kbit/s for DShot, frames a second for PWM and PPM).
 * @param s The slot or port, driver not OUT_DRIVER_NONE.
 * @return true when the driver took the pin.
 */
bool out_render_hw_bind(const out_slot_t *s);

/**
 * @brief Release @p s->pin from the driver @p s->driver names.
 * @param s The slot or port as it was bound.
 */
void out_render_hw_release(const out_slot_t *s);

/**
 * @brief Hold @p pin as an input with its pull-down.
 * @param pin The GPIO.
 * @return true when the pin is held.
 */
bool out_render_hw_park(uint8_t pin);

/**
 * @brief Let a parked pin go.
 * @param pin The GPIO.
 */
void out_render_hw_unpark(uint8_t pin);

/**
 * @brief Write a PWM pulse; 0 stops the pin edging after the pulse in
 *        progress.
 * @param pin The GPIO, bound as PWM.
 * @param pulse_us The pulse width, in microseconds.
 */
void out_render_hw_pwm(uint8_t pin, uint16_t pulse_us);

/**
 * @brief Write a PWM pulse and stamp the start of the first frame that
 *        carries it.
 * @param pin The GPIO, bound as PWM.
 * @param pulse_us The pulse width, in microseconds, not 0.
 * @param at_us Receives the stamp, in microseconds.
 * @return true when the stamp was taken.
 */
bool out_render_hw_pwm_stamped(uint8_t pin, uint16_t pulse_us,
                               uint64_t *at_us);

/**
 * @brief Write a PPM frame's channel widths.
 * @param pin The GPIO, bound as PPM.
 * @param us The @p n widths, in microseconds.
 * @param n The number of channels, at most OUT_RENDER_PPM_MAX.
 */
void out_render_hw_ppm(uint8_t pin, const uint16_t *us, uint8_t n);

/**
 * @brief Stop a PPM pin.
 * @param pin The GPIO, bound as PPM.
 */
void out_render_hw_ppm_stop(uint8_t pin);

/**
 * @brief Take the reply to the last DShot frame, once.
 * @param pin The GPIO, bound as bidirectional DShot.
 * @param edt true to read the reply as extended telemetry where it can be.
 * @param t Receives the reply.
 * @return true when a reply was taken.
 */
bool out_render_hw_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *t);

/**
 * @brief Stop sending DShot frames: the pin is driven low until the next run.
 * @param pin The GPIO, bound as DShot.
 */
void out_render_hw_dshot_stop(uint8_t pin);

/**
 * @brief Release a bidirectional pin from its low rest to its high idle.
 * @param pin The GPIO, bound as DShot.
 * @return true when the pin was at its low rest and is released now; no
 *         frame goes out on this pass then.
 */
bool out_render_hw_dshot_wake(uint8_t pin);

/**
 * @brief Send one DShot frame.
 * @param pin The GPIO, bound as DShot.
 * @param value The 11-bit value: a throttle or a command.
 * @param telemetry The frame's telemetry bit.
 */
void out_render_hw_dshot_send(uint8_t pin, uint16_t value, bool telemetry);

/**
 * @brief The microsecond clock, wrapping at 2^32us (71.6 minutes).
 * @return The time, in microseconds.
 */
uint32_t out_render_hw_us(void);

/**
 * @brief The millisecond clock, wrapping at 2^32ms (49.7 days).
 * @return The time, in milliseconds.
 */
uint32_t out_render_hw_ms(void);

/** @} */

/**
 * @brief Forget every binding: nothing bound, no port held, no telemetry,
 *        no watch.
 *
 * Calls no pin function.
 *
 * @param r The renderer.
 */
void out_render_init(out_render_t *r);

/**
 * @brief Make the slot bindings match the bank's slot table, each slot at
 *        the rate in @p rate_hz, binding only the slots in @p may_bind.
 *
 * A slot whose driver, channels, pin and rate are unchanged and that is
 * bound, and that is in @p may_bind, is left alone.  Every other slot is
 * released (when bound) before any slot is bound, and its telemetry and
 * extended-telemetry ask are forgotten.  A slot outside @p may_bind stays
 * unbound.  When a slot bound anew runs on PIO, every DShot port is let go
 * before the first bind; when a slot bound anew is PWM, every SERVO PWM port
 * on its slice is let go right before its bind.  A slot whose bind fails is
 * left unbound.  Does nothing when an argument is NULL.
 *
 * @param r The renderer.
 * @param o The output bank.
 * @param rate_hz OUT_MAX_SLOTS entries, from outputs_slot_rates().
 * @param may_bind Bit n set when slot n may be bound.
 */
void out_render_apply(out_render_t *r, const outputs_t *o,
                      const uint16_t *rate_hz, uint8_t may_bind);

/**
 * @brief Bind one slot an apply left out, as the bank has it, and move
 *        nothing else.
 *
 * @param r The renderer.
 * @param o The output bank.
 * @param rate_hz OUT_MAX_SLOTS entries, from outputs_slot_rates().
 * @param slot The slot.
 * @return true when the slot is bound (already, or now); false for a bad
 *         argument, a slot without a driver or a bind that failed.
 */
bool out_render_return_slot(out_render_t *r, const outputs_t *o,
                            const uint16_t *rate_hz, uint8_t slot);

/**
 * @brief Make the programming ports' bindings match @p pin, @p rate_hz,
 *        @p parked and @p dshot.
 *
 * A port whose pin, function or rate moved is let go (unparked or released)
 * before any port is bound; a port that did not move is left running.  A
 * port with a pin above OUT_MAX_PIN holds none.  Does nothing when @p pin or
 * @p rate_hz is NULL.
 *
 * @param r The renderer.
 * @param pin LINK_PG_PORTS_MAX pins, one per port.
 * @param rate_hz LINK_PG_PORTS_MAX rates: kbit/s for DShot, frames a second
 *        for PWM.
 * @param parked Bit n set when port n is parked (FUNC NONE).
 * @param dshot Bit n set when port n runs DShot.
 * @param channel0 The bank channel of port 0; port n renders channel0 + n.
 */
void out_render_ports(out_render_t *r, const uint8_t *pin,
                      const uint16_t *rate_hz, uint8_t parked, uint8_t dshot,
                      uint8_t channel0);

/**
 * @brief Render the bank onto every bound slot and port for one pass.
 *
 * Every slot and port is judged against one outputs_driving() answer.  A
 * slot drives when the arm covers the role of one of its channels.  At the
 * end of a pass that follows a driving pass and does not drive, every
 * telemetry reading is forgotten.  Does nothing when @p o is NULL.
 *
 * @param r The renderer.
 * @param o The output bank.
 */
void out_render_service(out_render_t *r, const outputs_t *o);

/**
 * @brief Whether slot @p slot is bound to its driver.
 *
 * @param r The renderer.
 * @param slot The slot.
 * @return true when the slot is bound; false for a slot out of range.
 */
bool out_render_bound(const out_render_t *r, uint8_t slot);

/**
 * @brief The ports whose function holds their pin, bit n for port n.
 *
 * @param r The renderer.
 * @return Bit n set when port n is bound or parked.
 */
uint8_t out_render_ports_bound(const out_render_t *r);

/**
 * @brief Which bound PWM ports render their channel's pulse while the bank
 *        drives, bit n for port n; the others are written 0.
 *
 * @param r The renderer.
 * @param ports Bit n set for port n.
 */
void out_render_ports_drive(out_render_t *r, uint8_t ports);

/**
 * @brief Watch channel @p ch for a capture's edge; a negative @p ch stops
 *        watching.
 *
 * The first later pass that renders a non-zero pulse other than the last
 * one on @p ch's PWM slot or port writes it stamped and ends the watch.
 *
 * @param r The renderer.
 * @param ch The channel, or a negative value.
 */
void out_render_watch(out_render_t *r, int ch);

/**
 * @brief The edge stamped since the last call, once.
 *
 * @param r The renderer.
 * @param us Receives the edge, in microseconds.
 * @return true when an edge was stamped since the last call.
 */
bool out_render_edge(out_render_t *r, uint64_t *us);

/**
 * @brief The slot whose readings the bench publishes: the lowest-numbered
 *        bound bidirectional DShot slot.
 *
 * @param r The renderer.
 * @return The slot, or -1 when no bidirectional slot is bound.
 */
int out_render_reporting_slot(const out_render_t *r);

/**
 * @brief The reporting slot's last electrical rpm and its age.
 *
 * @param r The renderer.
 * @param erpm Receives the electrical rpm.
 * @param age_ms Receives the time since it arrived, in milliseconds,
 *        wrap-safe.
 * @return false when no slot reports, an argument is NULL or no speed
 *         reading has arrived.
 */
bool out_render_erpm(const out_render_t *r, uint32_t *erpm, uint32_t *age_ms);

/**
 * @brief The reporting slot's last extended-telemetry reading of @p kind
 *        and its age.
 *
 * @param r The renderer.
 * @param kind The telemetry frame type.
 * @param value Receives the payload, as the ESC sent it.
 * @param age_ms Receives the time since it arrived, in milliseconds,
 *        wrap-safe.
 * @return false when no slot reports, an argument is NULL or out of range,
 *         or no reading of @p kind has arrived.
 */
bool out_render_edt(const out_render_t *r, dshot_telem_kind_t kind,
                    uint16_t *value, uint32_t *age_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_RENDER_H */
