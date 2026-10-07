/*
 * The SERVO link page at the coprocessor: the surfaces' frame rate and the
 * sweep that drives them through a curve.
 *
 * Register 0 is the frame rate (outputs_servo_rate_check()).  Registers 1 to
 * 4 are a sweep -- curve, speed, amplitude, dwell -- and fit one CAN frame,
 * so a start is whole.  Register 5 is how many ends a sweep started after it
 * reaches before it stops; register 6 reads how many the sweep has reached.
 *
 * A sweep runs only while the bench is armed, and only while the panel keeps
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
 * protocol 4.6) carries that sweep on from there.  Whatever ends the hold
 * -- 0, a disarm, silence -- forgets the phase.  A hold of a sweep that has
 * made its movements keeps the centre it ended on.
 *
 * Every write is judged against the page as a servo_page_step() at the same
 * moment leaves it, so a write served ahead of the pass finds a sweep that
 * has run out already ended.
 *
 * While it runs it commands every channel the bank marks a surface, each
 * pass, and the CHANNELS page is not what drives them.
 *
 * Nothing on the page is kept across a restart.  Host-tested; the
 * coprocessor is wiring.
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

typedef struct {
    uint16_t regs[LINK_SV_COUNT];
    sweep_t  sweep;
    uint32_t heard_ms;   /**< when the sweep was last written          */
    bool     finished;   /**< made its movements; the curve not changed */
    bool     holding;    /**< LINK_SV_HOLD in force                     */
} servo_page_t;

void servo_page_init(servo_page_t *p);

/**
 * A write to the page, validated whole against the bank @p o before any of
 * it is stored.  A stop holds each surface where its output has got to.
 * Refused: off the page (BAD_RANGE); register 6 (READ_ONLY); a frame rate
 * outputs_servo_rate_check() refuses, a sweep sweep_cfg_valid() refuses, or
 * a sweep started or changed by a write that does not carry all four of its
 * registers, or a resume with no phase kept or with a curve that has
 * changed since the hold (BAD_VALUE); a sweep, a hold or a resume while the
 * bank is not armed (NOT_ARMED).
 */
uint8_t servo_page_write(servo_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, outputs_t *o, uint32_t now_ms);

/** The page as a read sees it: the sweep register 0 once the sweep has
 *  stopped, and the ends it reached as of the last servo_page_step(). */
void servo_page_read(servo_page_t *p, uint8_t off, uint8_t n, uint16_t *out);

/**
 * One pass: stop a sweep the bank is no longer armed for, one not written
 * for OUT_DEFAULT_TIMEOUT_MS or one that has made its movements, and
 * command every surface along a sweep that runs.  True while it runs.
 */
bool servo_page_step(servo_page_t *p, outputs_t *o, uint32_t now_ms);

/** The frame rate register, for outputs_slot_rates(). */
uint16_t servo_page_hz(const servo_page_t *p);

/* ------------------------------------------------------- the host's side */

/** What a host writes to carry on a sweep it asked to be resumed. */
typedef enum {
    SERVO_RESUME_CURVE,      /**< the curve whole, as any sweep           */
    SERVO_RESUME_WRITE,      /**< LINK_SV_RESUME alone                    */
    SERVO_RESUME_TOO_OLD,    /**< the curve whole, from its beginning: a
                                  coprocessor older than 4.6 has no resume */
    SERVO_RESUME_REFUSED,    /**< the curve whole, from its beginning: the
                                  resume was refused                      */
} servo_resume_t;

/**
 * The host's choice for a sweep command: @p asked a resume of the sweep
 * held, @p held a hold in force at the far end, @p proto_minor its protocol
 * minor, and @p refused the RESUME just written was refused.  A resume is
 * written only for a hold in force, so a repeat of the resumed sweep is the
 * curve; the two fallbacks start the curve over and are for saying so.
 */
servo_resume_t servo_page_resume_plan(bool asked, bool held,
                                      uint16_t proto_minor, bool refused);

#ifdef __cplusplus
}
#endif
