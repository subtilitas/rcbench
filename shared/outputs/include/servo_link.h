/**
 * @file
 * @brief The panel's half of the SERVO screen on the link: a command onto the
 *        IOmcu's CHAN_CFG, CHANNELS, SERVO and PROG pages, the position said
 *        again every SERVO_HOLD_MS (100ms), the release owed while a surface
 *        holds a position, the frame rate and the sweep.
 *
 * Every exchange runs over a link_port_t, so the sequences the panel sends
 * run in the host suite against a modelled bus.  What belongs to the panel
 * -- the output bank, the arming policy, the alerts, the disarm -- is
 * reached through the callbacks of a servo_link_ctx_t.
 *
 * The control task owns the state.  Three groups of calls cross to the
 * task that runs the screens, each over atomics: servo_link_ask_release()
 * and servo_link_ask_hold() in, and the readings from
 * servo_link_rate_shown() to servo_link_sweep_start() out.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_host.h"
#include "link_port.h"
#include "prog_link.h"
#include "servo_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief servo_link_hz_sent() while the rate the PWM target runs at is not
 *  known: perhaps a faster rate. */
#define SERVO_HZ_UNKNOWN 0xFFFFu  /* not known: perhaps a faster rate        */

/** @brief The alerts a sweep's resume can raise, for servo_link_ctx_t.alert. */
typedef enum {
    SERVO_LINK_ALERT_RESUME_OLD = 0, /**< the IOmcu is older than 4.6 */
    SERVO_LINK_ALERT_RESUME_REFUSED, /**< it refused the resume             */
    SERVO_LINK_ALERT_RESUME_UNTIMED, /**< the hold's phase is not known     */
} servo_link_alert_t;

/**
 * @brief What the module reaches of the panel.
 *
 * @p host and @p port carry every exchange (link_write_acked()) and @p port's
 * now_ms is the clock; @p prog is the programming ports' page (prog_link.h).
 * Each callback takes
 * @p ctx.
 */
typedef struct {
    link_host_t       *host;   /**< the link host for every exchange         */
    const link_port_t *port;   /**< the port, and its clock                 */
    prog_link_t       *prog;   /**< the programming ports' state            */
    void              *ctx;    /**< passed to every callback                */
    /** @brief Whether the output bank is armed (outputs_armed()). */
    bool     (*armed)(void *ctx);
    /** @brief A stop applied, or a disarm asked for since the write began. */
    bool     (*stopped)(void *ctx);
    /** Raise @p a for the operator. */
    void     (*alert)(void *ctx, servo_link_alert_t a);
    /** Disarm the bench, and tell the far end while @p link_up. */
    void     (*disarm)(void *ctx, bool link_up);
    /** @brief The arming policy's count of stops (arming_stop_count()). */
    uint32_t (*stop_count)(void *ctx);
    /** @brief Ask the arming policy to arm the surfaces and the programming
     *        ports, and no motor.
     */
    void     (*arm_surfaces)(void *ctx);
} servo_link_ctx_t;

/**
 * @brief Start over with @p ctx: nothing held, no release owed, no binding
 *        read, the rate not known, no sweep.
 *
 * @p ctx is kept and must outlive the module's use.  Before any other call.
 *
 * @param ctx  The panel's callbacks. Kept by pointer, not copied.
 */
void servo_link_init(const servo_link_ctx_t *ctx);

/**
 * @brief An IOmcu of protocol minor @p minor started answering.
 *
 * Whether it has a SERVO rate (4.1), a sweep (4.2) and a resume (4.6); each
 * slot back at its own rate, one exchange; the sweep's state not known until
 * the next command writes a stop.
 *
 * @param minor  The protocol minor the IOmcu reports: 1 for the SERVO rate,
 *               2 for the sweep, 6 for the resume.
 */
void servo_link_came_up(uint16_t minor);

/** @brief The link went down: a sweep or a hold held to be said again is
 *  dropped (servo_cmd_survives_link_loss()). */
void servo_link_lost(void);

/**
 * @brief The binding read back: @p channels the surfaces' channel mask, bit n
 *        channel n, and @p known whether the pages read.
 *
 * An unknown binding is not an empty one: no command reaches the standard
 * outputs and a release stays owed until a read succeeds.  Publishes
 * servo_link_surfaces().
 *
 * @param channels  Surface channel mask: bit n set when channel n is a surface.
 * @param known     true when the pages were read; false marks the binding
 *                  unknown.
 */
void servo_link_binding(uint8_t channels, bool known);

/**
 * @brief A binding was written: the mask is unknown until the next read.
 *
 * Does not publish servo_link_surfaces().
 */
void servo_link_forget_binding(void);

/**
 * @brief Whether a binding has been read since the last servo_link_init(),
 *        servo_link_forget_binding() or unknown servo_link_binding().
 *
 * @return  true when the binding is known.
 */
bool servo_link_binding_known(void);

/**
 * @brief For the screen's SWEEP and its horn: a programming port as target, or
 *        a binding read with a surface in it (servo_link_surfaces()).
 */
void servo_link_surfaces_publish(void);

/**
 * @brief One command from the screen, with the link state @p link_up and the
 *        stop count @p stops its sender had seen.
 *
 * ARM centres the surfaces and asks the policy to arm, unless a stop came in
 * between; DISARM and RELEASE centre them; a sweep waits for an owed release to
 * be paid; a command asked during a pause let go of is not sent.
 *
 * @param sv      The command.
 * @param link_up Whether the link is up now.
 * @param stops   The stop count the sender had seen, from the policy's
 *                stop_count callback.
 */
void servo_link_apply(const servo_cmd_t sv, bool link_up, uint32_t stops);

/**
 * @brief Stop holding the servo.
 *
 * A position written or held leaves a release owed.
 */
void servo_link_let_go(void);

/**
 * @brief Every pass: settle a rate not known while a release is owed or the
 *        bank is armed, pay an owed release, and say a held position again once
 *        SERVO_HOLD_MS (100ms) has passed.
 *
 * Nothing while @p link_up is false.
 *
 * @param link_up  Whether the link is up now.
 */
void servo_link_service(bool link_up);

/**
 * @brief Whether the rate the surfaces run at is known, after one more reset
 *        when it is not.  Asked before every arm.
 *
 * @return  true when the rate is known.
 */
bool servo_link_rate_settled(void);

/**
 * @brief What a rate reset left behind: @p page whether the IOmcu has the SERVO
 *        page, @p landed whether the reset was acknowledged.
 *
 * For the reset a binding's own sequence sends (bind_link_write()) and a TARGET
 * change (prog_link_rate()).
 *
 * @param page    true when the IOmcu has the SERVO page.
 * @param landed  true when the reset was acknowledged.
 */
void servo_link_rate_reset_note(bool page, bool landed);

/**
 * @brief The SERVO screen's half of the programming ports' service, after the
 *        page's read: a position still out is let go first while the bench is
 *        at rest and a target change is pending, then at most one write towards
 *        what the screen wants (prog_link_next()), a pulse that comes on
 *        preceded by the range the screen names; then what a TARGET change
 *        means for the rate (prog_link_rate()).
 */
void servo_link_prog_service(void);

/**
 * @brief Whether a release is owed: a surface may still hold a position.
 *
 * @return  true when a release is owed.
 */
bool servo_link_release_owed(void);

/**
 * @brief The rate the IOmcu acknowledged, 0 for each slot's own, or
 *        SERVO_HZ_UNKNOWN.
 *
 * @return  The acknowledged frame rate in Hz, 0, or SERVO_HZ_UNKNOWN.
 */
uint16_t servo_link_hz_sent(void);

/**
 * @brief A release the screen asked for through servo_link_ask_release():
 *        taken, it drops the position held and owes the release.
 *
 * True when one was asked for.
 *
 * @return  true when a release was asked for, and this call took it.
 */
bool servo_link_take_release(void);

/**
 * @brief From the screens' task: a release, or a disarm with one, asked for.
 *        A sweep being written gives way to it.
 */
void servo_link_ask_release(void);

/**
 * @brief From the screens' task: a HOLD, or a command that ends a sweep, asked
 *        for.
 *
 * A sweep being written gives way to it; cleared when
 * servo_link_apply() takes the next command.
 */
void servo_link_ask_hold(void);

/**
 * @brief The rate's state and its Hz, as (servo_rate_state_t << 16) | Hz.
 *
 * @return  The packed state and rate.
 */
unsigned servo_link_rate_shown(void);

/**
 * @brief Whether the IOmcu can sweep: protocol 4.2 or later.
 *
 * @return  true when it can.
 */
bool servo_link_sweep_able(void);

/**
 * @brief What servo_link_surfaces_publish() last published.
 *
 * @return  true when there is something to move.
 */
bool servo_link_surfaces(void);

/**
 * @brief A sweep found no surface to sweep, once.
 *
 * @return  true once for each refusal; the flag is cleared by the call.
 */
bool servo_link_take_sweep_refused(void);

/**
 * @brief The far end let go of a hold, once, with its pause in @p pause_seq.
 *
 * @param pause_seq  Receives the sequence number of the pause.
 * @return           true once for each release; false when none is pending.
 */
bool servo_link_take_hold_lost(uint16_t *pause_seq);

/**
 * @brief The far end took the HOLD of pause @p pause_seq and kept its curve
 *        @p kept_ms in, once.
 *
 * @param pause_seq  Receives the sequence number of the pause.
 * @param kept_ms    Receives the phase the curve kept, in milliseconds.
 * @return           true once for each hold; false when none is pending.
 */
bool servo_link_take_sweep_held(uint16_t *pause_seq, uint32_t *kept_ms);

/**
 * @brief The far end started a sweep, once.
 *
 * Its details: servo_link_sweep_start().
 *
 * @return  true once for each start.
 */
bool servo_link_take_sweep_start(void);

/** @brief The last sweep start the far end acknowledged. */
typedef struct {
    uint16_t           start_seq;  /**< the command it acknowledges       */
    uint32_t           start_ms;   /**< the curve's phase 0, on the clock  */
    servo_sweep_from_t from;       /**< where the sweep started from       */
    uint32_t           frozen_ms;  /**< when it froze, for FROZEN; when the
                                        resume was taken, for RESUMED      */
} servo_link_start_t;

/**
 * @brief Read the last sweep start the far end acknowledged.
 *
 * @return  The start sequence, phase 0 on the clock, the origin and the
 *          freeze or resume time, each read from its own atomic.
 */
servo_link_start_t servo_link_sweep_start(void);

#ifdef __cplusplus
}
#endif
