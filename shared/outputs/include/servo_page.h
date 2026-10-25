/**
 * @file
 * @brief The SERVO link page at the IOmcu: the frame rate of the tester's
 *        target and the sweep that drives it through a curve.
 *
 * Register 0 is the target's frame rate (protocol 4.19).  With the standard
 * servo ports as target it is every PWM slot whose first channel is a
 * surface (outputs_servo_rate_check()); with a programming port it is that
 * port's pin alone, and the slots run at their own rates.  The other
 * programming ports run at PROG_PORT_RATE_HZ, 50Hz.  A change of target
 * returns the register to 0.  Registers 1 to
 * 4 are a sweep -- curve, speed, amplitude, dwell -- and fit one CAN frame,
 * so a start is whole.  Register 5 is how many ends a sweep started after it
 * reaches before it stops; register 6 reads how many the sweep has reached.
 *
 * A sweep runs only while the bench is armed for the surfaces
 * (OUT_ROLE_SURFACE), and only while the panel keeps
 * writing it: one that hears nothing for OUT_DEFAULT_TIMEOUT_MS stops, the
 * rule a channel command already keeps, so a panel that goes quiet leaves no
 * servo moving.  A write that repeats the sweep in force keeps it going
 * without restarting the curve; one that changes it starts the new curve
 * from its beginning.  A disarm stops it.  A sweep that has made its
 * movements stays stopped while the same curve is written again, so a
 * panel still repeating it does not start it over; 0 or another curve
 * clears that.
 *
 * A hold (LINK_SV_HOLD) freezes the surfaces where their outputs are, and a
 * sweep running when it began keeps its phase; a resume (LINK_SV_RESUME,
 * protocol 4.6) carries that sweep on from there.  Whatever ends the hold -- 0,
 * a curve written over it, a disarm, silence -- forgets the phase.  A hold of a
 * sweep that has made its movements keeps the centre it ended on.
 *
 * Every write is judged against the page as a servo_page_step() at the same
 * moment leaves it, so a write served ahead of the pass finds a sweep that
 * has run out already ended.
 *
 * While it runs it commands every channel the bank marks a surface, each
 * pass, and the CHANNELS page is not what drives them.
 *
 * Nothing on the page is kept across a restart.  Host-tested; the
 * IOmcu is wiring.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"
#include "servo_sweep.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The SERVO page state: its registers, the sweep, and the hold. */
typedef struct {
    uint16_t regs[LINK_SV_COUNT];  /**< registers 0 to 6, LINK_SV_* */
    sweep_t  sweep;                /**< the sweep in progress, if any */
    uint32_t heard_ms;   /**< when the sweep was last written          */
    bool     finished;   /**< made its movements; the curve not changed */
    bool     holding;    /**< LINK_SV_HOLD in force                     */
    uint8_t  target_first; /**< the channels commanded, when target_n   */
    uint8_t  target_n;     /**< is not 0; else the standard servo ports */
    uint16_t ports;        /**< bank channels pulsing, not slots          */
    uint16_t sliced;       /**< bank channels held at PROG_PORT_RATE_HZ   */
} servo_page_t;

/**
 * @brief Set the page to its start: no sweep, no hold, the standard servo
 *        ports as target.
 *
 * Nothing is done when @p p is NULL.
 *
 * @param p The page state, overwritten.
 */
void servo_page_init(servo_page_t *p);

/**
 * @brief A write to the page, validated whole against the bank @p o before any
 *        of it is stored.
 *
 * A stop holds each surface where its output has got to.  Refused: off the page
 * (BAD_RANGE); register 6 (READ_ONLY); a frame rate the target cannot take
 * (BAD_VALUE): for the standard servo ports one outputs_servo_rate_check()
 * refuses or one under which a bound PWM output would carry a pulse longer than
 * its frame (outputs_pulse_check(), protocol 4.16); for a programming port one
 * outside the PWM driver's 40 to 560Hz, one other than PROG_PORT_RATE_HZ for a
 * port servo_page_ports() names as sharing its slice, or one whose frame is
 * shorter than the endpoint of a port it names as pulsing (protocol 4.19).
 * Refused too: a sweep sweep_cfg_valid() refuses, or a sweep started or changed
 * by a write that does not carry all four of its registers, or a resume with no
 * phase kept or with a curve that has changed since the hold (BAD_VALUE); a
 * sweep, a hold or a resume while the bank is not armed for the surfaces, an
 * arm for the throttle alone among them (NOT_ARMED).
 *
 * @param p The page state, changed only when the write is taken.
 * @param off First register, 0 to LINK_SV_COUNT - 1.
 * @param n Number of registers to write. 0 is taken with no change.
 * @param in The @p n new register values.
 * @param o The outputs bank, judged and driven by the sweep and the hold.
 * @param now_ms Current time in milliseconds, on the same clock as the passes.
 * @return 0 when taken. LINK_NACK_BAD_RANGE for a NULL pointer or a range past
 *         LINK_SV_COUNT. LINK_NACK_READ_ONLY for a range that reaches
 *         register 6. LINK_NACK_BAD_VALUE and LINK_NACK_NOT_ARMED as described
 *         above.
 */
uint8_t servo_page_write(servo_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, outputs_t *o, uint32_t now_ms);

/**
 * @brief The page as a read sees it: the sweep register 0 once the sweep has
 *        stopped, and the ends it reached as of the last servo_page_step().
 *
 * Nothing is done when @p p or @p out is NULL, or when off and n reach past
 * LINK_SV_COUNT.
 *
 * @param p The page state. The sweep register is cleared when the sweep has
 *        stopped.
 * @param off First register, 0 to LINK_SV_COUNT - 1.
 * @param n Number of registers to read.
 * @param out Receives the @p n register values.
 */
void servo_page_read(servo_page_t *p, uint8_t off, uint8_t n, uint16_t *out);

/**
 * @brief One pass: stop a sweep the bank's surfaces are no longer armed for,
 *        one not written for OUT_DEFAULT_TIMEOUT_MS or one that has made its
 *        movements, and command every surface along a sweep that runs.
 *
 * True while it runs.
 *
 * @param p The page state. Nothing is done when NULL.
 * @param o The outputs bank whose surfaces are commanded.
 * @param now_ms Current time in milliseconds.
 * @return true while the sweep runs, false when it has stopped or @p p or @p o
 *         is NULL.
 */
bool servo_page_step(servo_page_t *p, outputs_t *o, uint32_t now_ms);

/**
 * @brief The programming ports as a rate for a port target is judged, bit n for
 *        bank channel n: @p channels the ports whose pulse is on, whose
 *        endpoints the rate must fit, and @p sliced the ports whose pin shares
 *        a PWM slice with another port's pin, which run at PROG_PORT_RATE_HZ
 *        only.
 *
 * Both 0 after servo_page_init().
 *
 * @param p The page state. Nothing is done when NULL.
 * @param channels Bit n set for a bank channel whose pulse is on.
 * @param sliced Bit n set for a bank channel that runs at PROG_PORT_RATE_HZ.
 */
void servo_page_ports(servo_page_t *p, uint16_t channels, uint16_t sliced);

/**
 * @brief The frame rate register: the target's rate, 0 for its own.
 *
 * @param p The page state.
 * @return The frame rate in Hz from register 0, or 0 for NULL.
 */
uint16_t servo_page_hz(const servo_page_t *p);

/**
 * @brief The rate the slots take, for outputs_slot_rates() and the OUTPUTS and
 *        CHAN_CFG checks: the register with the standard servo ports as target,
 *        0 with a programming port.
 *
 * @param p The page state.
 * @return The frame rate in Hz when the target is the standard servo ports,
 *         otherwise 0. 0 for NULL.
 */
uint16_t servo_page_slots_hz(const servo_page_t *p);

/**
 * @brief Name the channels the sweep and the hold command: @p count channels of
 *        the bank from @p first, or with @p count 0 the standard servo ports
 *        (channels 0 to LINK_OUT_CHANNELS - 1), which is what a page starts
 *        with.
 *
 * A programming port is such a range of 1 channel.
 *
 * A change ends a running or held sweep as a SWEEP of 0 does: the surfaces
 * it moved stay where their outputs are.  It also returns the frame rate
 * register to 0, so a change of target never raises a rate under pulses
 * meant for a slower one (protocol 4.19).  False, with nothing changed, for
 * a range that runs off the bank.
 *
 * @param p The page state.
 * @param o The outputs bank, for the surfaces a running sweep stops on.
 * @param first First channel of the target.
 * @param count Number of channels; 0 selects the standard servo ports.
 * @return true when the target is in force, including when it is unchanged.
 *         false for NULL @p p or @p o, or a range past OUT_MAX_CHANNELS.
 */
bool servo_page_target(servo_page_t *p, outputs_t *o, uint8_t first,
                       uint8_t count);

/* ------------------------------------------------------- the host's side */

/** @brief What a host writes to carry on a sweep it asked to be resumed. */
typedef enum {
    SERVO_RESUME_CURVE,      /**< the curve whole, as any sweep           */
    SERVO_RESUME_WRITE,      /**< LINK_SV_RESUME alone                    */
    SERVO_RESUME_TOO_OLD,    /**< the curve whole, from its beginning: an
                                  IOmcu older than 4.6 has no resume */
    SERVO_RESUME_REFUSED,    /**< the curve whole, from its beginning: the
                                  resume was refused                      */
    SERVO_RESUME_UNTIMED,    /**< the curve whole, from its beginning: the
                                  HOLD went unanswered, so whether the far
                                  end holds is not known here             */
} servo_resume_t;

/**
 * @brief The host's choice for a sweep command: @p asked a resume of the sweep
 *        held, @p held a hold at the far end, @p timed its phase known here
 *        (false for a HOLD that went unanswered and was not yet acknowledged,
 *        which may or may not have reached the far end), @p proto_minor its
 *        protocol minor, and @p refused the RESUME just written was refused.
 *
 * A resume is written only for a hold in force, so a repeat of the resumed
 * sweep is the curve; the two fallbacks start the curve over -- a stop, then
 * the curve whole, since a RESUME whose acknowledgement was lost leaves the far
 * end running and a repeat of its curve would carry it on -- and are for
 * saying so.
 *
 * @param asked Whether a resume of the held sweep was asked.
 * @param held Whether a hold is in force at the far end.
 * @param timed Whether the phase of the hold is known on the host.
 * @param proto_minor The IOmcu's protocol minor. Below 6 has no resume.
 * @param refused Whether the RESUME just written was refused.
 * @return SERVO_RESUME_CURVE when nothing is asked or held,
 *         SERVO_RESUME_TOO_OLD below minor 6, SERVO_RESUME_UNTIMED when the
 *         phase is unknown, SERVO_RESUME_REFUSED when @p refused, and
 *         SERVO_RESUME_WRITE otherwise.
 */
servo_resume_t servo_page_resume_plan(bool asked, bool held, bool timed,
                                      uint16_t proto_minor, bool refused);

/**
 * @brief The far end's sweep phase as the host times it, from the
 *        acknowledgements of the writes that start, hold and resume it: when
 *        the curve's phase 0 is on the host's clock, and the phase a hold kept.
 *
 * The far end times each from when the write reached it; the reply's delay is
 * in every acknowledgement alike, so it cancels in the differences.
 */
typedef struct {
    uint32_t start_ms;   /**< phase 0 of the curve, on the host's clock   */
    uint32_t kept_ms;    /**< how far into the curve a hold kept it       */
    bool     kept;       /**< a hold of a running sweep, not yet resumed  */
} servo_phase_t;

/**
 * @brief A sweep started, from its beginning, when acknowledged at @p ack_ms.
 *
 * Nothing is done when @p ph is NULL.
 *
 * @param ph The phase record, set to the start and no kept phase.
 * @param ack_ms Host time of the acknowledgement, in milliseconds.
 */
void servo_phase_started(servo_phase_t *ph, uint32_t ack_ms);

/**
 * @brief A running sweep held at @p ack_ms: its phase is kept; returned.
 *
 * @param ph The phase record. Nothing is changed when NULL.
 * @param ack_ms Host time of the hold's acknowledgement, in milliseconds.
 * @return The kept phase in milliseconds: @p ack_ms minus the start. 0 for
 *         NULL.
 */
uint32_t servo_phase_held(servo_phase_t *ph, uint32_t ack_ms);

/**
 * @brief The sweep stopped, or the far end restarted: nothing is held to
 *        resume.
 *
 * Nothing is done when @p ph is NULL.
 *
 * @param ph The phase record. Its kept phase is cleared.
 */
void servo_phase_stopped(servo_phase_t *ph);

/**
 * @brief Whether a hold of a running sweep stands to be resumed.
 *
 * @param ph The phase record.
 * @return true when a phase is kept, false otherwise or for NULL.
 */
bool servo_phase_resumable(const servo_phase_t *ph);

/**
 * @brief A held sweep resumed at @p ack_ms: its phase 0 moves on by the hold,
 *        into @p start_ms.
 *
 * False, and nothing changes, with no phase kept.
 *
 * @param ph The phase record. Its kept phase is cleared on success.
 * @param ack_ms Host time of the resume's acknowledgement, in milliseconds.
 * @param start_ms Receives the new phase 0 on the host's clock. May be NULL.
 * @return true when a phase was kept and resumed, false otherwise or for NULL.
 */
bool servo_phase_resumed(servo_phase_t *ph, uint32_t ack_ms,
                         uint32_t *start_ms);

#ifdef __cplusplus
}
#endif
