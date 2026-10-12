/**
 * @file prog_port.h
 * @brief The programming ports and their link page (0x33).
 *
 * A programming port is an output of its own kind.  It has its own pin, is
 * no slot of the OUTPUTS page and holds no channel of the CHANNELS page.
 * Its pin does one of two things:
 *
 *   PWM          a servo pulse from the PWM (pulse-width modulation)
 *                generator, rendered while the bank is armed and the port's
 *                bit in LINK_PG_PWM is set.  The pulse is a channel of the
 *                bank (prog_port_channel()), so arming, clamping, slew, the
 *                500 ms command timeout and the endpoints are the bank's.
 *   PROGRAMMING  the wire protocol of the KST page (kst_port.h).
 *
 * The servo tester reaches a port three ways: LINK_PG_CMD is the manual
 * command, LINK_PG_TARGET points the SERVO page's sweep and hold at the
 * port (servo_page_target()), and a capture of the SERVO_SENSE page names
 * the port with LINK_SS_ARM_PORT.
 *
 * Pins.  A port's pin and a slot's pin are never the same: a PIN write
 * refuses a slot's pin, and prog_port_slots_check() refuses a slot on a
 * port's pin.  A port's pin shares no PWM slice and no compare register
 * with a PWM slot's pin either, so neither can change the other's frame
 * rate.  Two ports may share a slice: both run at the one port rate.
 *
 * The model holds LINK_PG_PORTS_MAX ports; a build names how many it has.
 *
 * Pure logic: binding a pin to the generator is the caller's.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_PORT_H
#define RCBENCH_PROG_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The bank channel of port 0; port n has the next n-th.  Above the 8
 *  channels of the CHANNELS page and the bench throttle's channel 8. */
#define PROG_PORT_CHANNEL0 9u
/** A port's frame rate while the SERVO page names none. */
#define PROG_PORT_RATE_HZ 50u
/** Port 0's pin on a board that has stored none: GP13, pad 17 of the
 *  bring-up module.  Its PWM slice partner GP12 is reserved and the pin
 *  with its compare register, GP29, is not on the header. */
#define PROG_PORT_DEFAULT_PIN 13u
/** What prog_port_pin() returns for a port that holds no pin. */
#define PROG_PORT_NO_PIN 0xFFu

/** The registers of one port that are kept in flash: PIN, SLEW, MIN_US,
 *  MAX_US. */
#define PROG_PORT_KEPT 4u
#define PROG_PORT_KEPT_COUNT (LINK_PG_PORTS_MAX * PROG_PORT_KEPT)

typedef struct {
    uint16_t regs[LINK_PG_REGS];
    uint8_t  count;     /**< ports this build has */
    uint8_t  bound;     /**< bit n: the generator has port n's pin */
    uint8_t  blocked;   /**< bit n: the kept pin was taken at start; the
                             port does not hold it */
    uint16_t hz;        /**< the ports' frame rate (prog_port_rate()) */
} prog_port_t;

/** @p count ports, 0 to LINK_PG_PORTS_MAX, none with a pin, at
 *  PROG_PORT_RATE_HZ. */
void prog_port_init(prog_port_t *p, uint8_t count);

/** The kept registers of a board that has stored none: port 0 on
 *  @p pin0 (LINK_PG_PIN_NONE for none), the others without a pin, slew 0,
 *  LINK_CC_DEFAULT_MIN to LINK_CC_DEFAULT_MAX. */
void prog_port_defaults(uint16_t *kept, uint16_t pin0);

/**
 * Take the kept registers at start, after the slots and the other pin
 * holders are restored.  A value no write would take becomes its default.
 * A pin that is taken (a slot's, one in @p taken, reserved in @p o, or
 * beside a PWM slot in the silicon) stays on the page and is not held:
 * LINK_PG_FLAGS says refused until another pin is written.  Sets the role,
 * slew and endpoints of each port's channel in @p o.
 */
void prog_port_restore(prog_port_t *p, const uint16_t *kept, outputs_t *o,
                       uint64_t taken);

/** The kept registers as they stand, PROG_PORT_KEPT_COUNT of them. */
void prog_port_kept(const prog_port_t *p, uint16_t *kept);

/**
 * A write of the page: 0, or the LINK_NACK_* reason with nothing changed.
 *
 * @p taken: the pins other holders have beyond the slots and the bank's
 * reservation.  @p locked: no PIN may change now (the bank is armed or
 * asked to arm, or the KST port is out of PWM).  A PIN change is also
 * refused for a port whose PWM bit is set or that is the TARGET.
 *
 * PWM and TARGET name only ports whose pin the generator has
 * (prog_port_bound()), and not a port whose PIN the same frame changes.
 *
 * No port whose PWM bit the write leaves set may have an endpoint longer
 * than its frame carries: MIN_US and MAX_US at most out_pwm_pulse_max() of
 * the rate prog_port_rate() last named, 1784 us at 560 Hz (BAD_VALUE,
 * protocol 4.16).  This covers a range written to a pulsing port and a
 * pulse switched on under a range that does not fit.
 */
uint8_t prog_port_write(prog_port_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, outputs_t *o, uint64_t taken,
                        bool locked, uint32_t now_ms);

void prog_port_read(const prog_port_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * Undo a write the silicon could not follow: the page as @p was, and each
 * port's channel in @p o with the slew and endpoints of that page.  A
 * command the write carried stays commanded.
 */
void prog_port_revert(prog_port_t *p, const prog_port_t *was, outputs_t *o);

/** Tell the page which ports' pins the generator has, bit n for port n.
 *  A port that loses its pin loses its PWM bit and, if it was the TARGET,
 *  the target returns to the standard servo ports. */
void prog_port_bound(prog_port_t *p, uint8_t mask);

uint8_t prog_port_count(const prog_port_t *p);

/** The pin port @p port holds, or PROG_PORT_NO_PIN. */
uint8_t prog_port_pin(const prog_port_t *p, uint8_t port);

/** Every pin the ports hold, one bit per pin. */
uint64_t prog_port_pins(const prog_port_t *p);

/** Bit n: port n's pulse is switched on. */
uint8_t prog_port_pwm(const prog_port_t *p);

/** The bank channels of the ports whose pulse is switched on, bit c for
 *  channel c: what servo_page_ports() is told. */
uint16_t prog_port_pwm_channels(const prog_port_t *p);

/** The frame rate the ports run at: the SERVO page's, or PROG_PORT_RATE_HZ
 *  while it names none.  A write is judged against it. */
void prog_port_rate(prog_port_t *p, uint16_t hz);

/** 0 for the standard servo ports, 1 + n for port n. */
uint8_t prog_port_target(const prog_port_t *p);

/** The bank channel of @p port. */
uint8_t prog_port_channel(uint8_t port);

/**
 * Whether the OUTPUTS page @p slots may be taken beside the ports: 0, or
 * LINK_NACK_BAD_VALUE for a slot on a port's pin, a PWM slot on a port
 * pin's slice, or a bound slot whose channel range reaches a port's bank
 * channel (PROG_PORT_CHANNEL0 to PROG_PORT_CHANNEL0 + LINK_PG_PORTS_MAX - 1).
 */
uint8_t prog_port_slots_check(const prog_port_t *p, const uint16_t *slots);

/**
 * Unbinds every slot of @p slots whose channel range reaches a port's bank
 * channel, for a stored page read at start.  Returns how many.
 */
unsigned prog_port_slots_free(uint16_t *slots);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_PORT_H */
