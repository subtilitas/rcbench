/**
 * @file kst_port.h
 * @brief The KST programming port and its link page (0x32).
 *
 * A KST or Chaservo servo in programming mode still follows PWM
 * (pulse-width modulation) pulses.  The port takes one output channel out
 * of PWM, hands its pin to the wire driver and runs the session of
 * protocols/kst on it.  A channel is PWM or PROGRAMMING, never both.
 *
 * The way out of PWM, started by the operation ENTER:
 *
 *   STOPPING     KST_PORT_STOP_MS: the slot renders no pulse, so a pulse
 *                that runs ends and no other starts.
 *   LOW          the slot is released and the driver holds the pin low for
 *                KST_PORT_LOW_MS.
 *   PROGRAMMING  the session runs; its entry sequence holds the line low
 *                for another KST_ENTRY_LOW_US.
 *
 * The way back:
 *
 *   - Before the first frame nothing was said to the servo: an abort, a
 *     stop or a failed entry returns the channel to PWM.
 *   - After the first frame the servo may be in programming mode until its
 *     supply was off.  The flag LINK_KS_F_IN_MODE says so and the channel
 *     stays PROGRAMMING until the operation POWER_CYCLED.  With a rail
 *     switch (kst_port_hw_t.rail) the port switches the servo rail off for
 *     KST_PORT_RAIL_OFF_MS and waits KST_PORT_RAIL_WAIT_MS; without one the
 *     operation is the user's word that the servo was without power.
 *
 * While the port holds a channel the caller keeps PWM off its pin:
 * kst_port_hold_mask() names the slot that renders no pulse and
 * kst_port_pin_mask() the slot that is not bound.  Every source of a PWM
 * command (the panel, the sweep, the automatic test, the sync) ends at the
 * slot, so none reaches the pin.
 *
 * A stop is the heartbeat lost, the link silent or the bank disarmed.  On a
 * stop the running operation is aborted: a frame on the wire is finished,
 * which takes at most 12 ms, and no other starts.  The pin is low or an
 * input afterwards.  LINK_KS_F_MUST_READ is set, and until a read of all
 * registers has succeeded the port takes ENTER, READ_ALL, ABORT and
 * POWER_CYCLED only.  The operation ABORT does the same.  While the
 * heartbeat is not trusted or the link is silent no frame starts and every
 * operation but ABORT and POWER_CYCLED is refused.
 *
 * A write to the servo (WRITE, RESTORE, RELEASE) is refused unless the
 * command carries LINK_KST_WRITE_KEY.  The plan is built here, from the
 * image this session read and the staged registers, with the planner and
 * the limits of protocols/kst; the panel sends no register write.
 *
 * Commands: registers 0 to 3 of a write, one frame.  SEQ is LINK_KS_SEQ
 * plus 1; the frame just taken, sent again, is acknowledged and does
 * nothing; any other SEQ is refused with LINK_NACK_BAD_VALUE.  A command
 * that is taken is acknowledged, and what became of it is on the page:
 * LINK_KS_RESULT's high byte says why it started nothing, or 0.
 *
 * Nothing here is kept over a restart of the coprocessor: after one the
 * port is in PWM with LINK_KS_F_IN_MODE clear, whatever mode the servo is
 * in.
 *
 * Pure logic: the pin, the driver and the rail are behind kst_port_hw_t.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_KST_PORT_H
#define RCBENCH_KST_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "kst_session.h"
#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The slot renders no pulse this long before it is released: 2 frames at
 *  40 Hz, the lowest PWM rate. */
#define KST_PORT_STOP_MS 50u
/** The driver holds the pin low this long before the session starts. */
#define KST_PORT_LOW_MS 100u
/** A switched servo rail stays off this long. */
#define KST_PORT_RAIL_OFF_MS 6000u
/** And the servo has this long to start before PWM returns. */
#define KST_PORT_RAIL_WAIT_MS 3000u

/** What the port needs from the hardware. */
typedef struct {
    void *ctx;
    /** True when the silicon renders PWM on @p slot. */
    bool (*bound)(void *ctx, uint8_t slot);
    /** True when the path from @p pin to the connector carries a reply
     *  back to the pin. */
    bool (*reply_path)(void *ctx, uint8_t pin);
    /** Release @p slot from PWM and open the wire driver on @p pin, which
     *  it holds low.  False when the driver gets no resources; the slot is
     *  then unbound and give() binds it again. */
    bool (*take)(void *ctx, uint8_t slot, uint8_t pin);
    /** Close the wire driver and bind the slots again.  Called with the
     *  port in LINK_KST_PWM. */
    void (*give)(void *ctx);
    /** Switch the servo rail.  NULL: the hardware has no switch. */
    void (*rail)(void *ctx, bool on);
    /** The wire driver.  now_us() works while the driver is closed. */
    kst_driver_t driver;
} kst_port_hw_t;

typedef struct {
    kst_port_hw_t hw;
    kst_session_t session;
    kst_plan_t plan;     /**< work space of a command: not on the stack */
    kst_image_t work;    /**< the staged image of that command */
    uint16_t staged[LINK_KS_W_STAGED];
    uint16_t last_cmd[LINK_KS_W_CMD_FRAME];
    uint32_t t0_ms;
    uint16_t seq;
    uint16_t frames;
    uint8_t  state;      /**< link_kst_state_t */
    uint8_t  slot;
    uint8_t  channel;
    uint8_t  pin;
    uint8_t  op;         /**< link_kst_op_t of the last operation started */
    uint8_t  result;     /**< kst_ses_t of that operation */
    uint8_t  refusal;    /**< link_kst_refusal_t of the last command */
    uint8_t  fail_reg;
    uint8_t  steps_done;
    uint8_t  seen_step;
    uint8_t  plan_n;
    uint8_t  plan_reg[KST_PLAN_MAX_STEPS];
    uint8_t  busy;       /**< an operation runs */
    uint8_t  running;    /**< it is the session's */
    uint8_t  leaving;    /**< a power cycle waits for the session to end */
    uint8_t  framed;     /**< a frame went out since the servo had no power */
    uint8_t  must_read;
    uint8_t  view;
    uint8_t  safe;
} kst_port_t;

/** Prepare a port in LINK_KST_PWM.  False when @p hw lacks a function
 *  other than rail. */
bool kst_port_init(kst_port_t *p, const kst_port_hw_t *hw);

/**
 * A write of the page: 0, or the LINK_NACK_* reason with nothing changed.
 *
 * A window that starts in registers 0 to 3 is a command and has to be
 * exactly those 4.  Registers 4 to 25 are taken in any windows; 26 and up
 * are LINK_NACK_READ_ONLY.  @p o tells ENTER which slot renders the channel
 * and what the channel's role is.
 */
uint8_t kst_port_write(kst_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, const outputs_t *o,
                       uint32_t now_ms);

/** A read of the page, LINK_KS_*. */
void kst_port_read(const kst_port_t *p, uint8_t off, uint8_t n,
                   uint16_t *out);

/**
 * One pass.  @p safe: the heartbeat is trusted and the link is not silent.
 * @p stop: the bank was switched off or disarmed since the last pass.
 * Steps the session and the port's timers; a pass every 1 ms or faster
 * keeps the session's pacing.
 */
void kst_port_step(kst_port_t *p, bool safe, bool stop, uint32_t now_ms);

/** Where the port's channel is. */
link_kst_state_t kst_port_state(const kst_port_t *p);

/** Bit n: slot n renders no PWM pulse.  The port's slot in every state but
 *  LINK_KST_PWM. */
uint8_t kst_port_hold_mask(const kst_port_t *p);

/** Bit n: slot n is not bound to PWM, its pin is the wire driver's.  The
 *  port's slot from LINK_KST_LOW on. */
uint8_t kst_port_pin_mask(const kst_port_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_KST_PORT_H */
