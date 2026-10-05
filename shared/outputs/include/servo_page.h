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
} servo_page_t;

void servo_page_init(servo_page_t *p);

/**
 * A write to the page, validated whole against the bank @p o before any of
 * it is stored.  Refused: off the page (BAD_RANGE); register 6 (READ_ONLY);
 * a frame rate outputs_servo_rate_check() refuses, or a sweep
 * sweep_cfg_valid() refuses (BAD_VALUE); a sweep while the bank is not
 * armed (NOT_ARMED).
 */
uint8_t servo_page_write(servo_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, const outputs_t *o,
                         uint32_t now_ms);

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

#ifdef __cplusplus
}
#endif
