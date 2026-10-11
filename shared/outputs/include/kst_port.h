/**
 * @file kst_port.h
 * @brief The KST programming protocol on a programming port, and its link
 *        page (0x32).
 *
 * Whether a KST or Chaservo servo in programming mode follows PWM
 * (pulse-width modulation) pulses is not measured; the port is built for
 * one that does.  The port takes the pin of one
 * programming port (prog_port.h) from the PWM generator, hands it to the
 * wire driver and runs the session of protocols/kst on it.  A port is in
 * PWM or in PROGRAMMING, never both.  A standard servo port is no
 * programming port: a command that names an output channel is refused.
 *
 * The servo's supply is the PD mini of the SUPPLY page, reached through
 * kst_port_hw_t.supply.  With no PD mini answering the port touches no
 * supply and the user switches the servo's power.
 *
 * The way out of PWM, started by the operation ENTER:
 *
 *   STOPPING     KST_PORT_STOP_MS: the port renders no pulse, so a pulse
 *                that runs ends and no other starts.
 *   LOW          the generator gives up the pin and the driver holds it low
 *                for KST_PORT_LOW_MS at least.  The supply is set here:
 *                  - off: set to the staged LINK_KS_W_SUPPLY_MV and
 *                    LINK_KS_W_SUPPLY_MA and switched on;
 *                  - on at that voltage or under it: left as it is;
 *                  - on above it: lowered to it.
 *                The voltage of an output that is on is never raised.  The
 *                step ends when the PD mini reads on at what was asked,
 *                and, for one switched on here, KST_PORT_RAIL_WAIT_MS
 *                after that.  Not read within KST_PORT_SUPPLY_MS: back to
 *                PWM with LINK_KST_RESULT_SUPPLY, and a supply switched on
 *                here is off again.
 *   PROGRAMMING  the session runs; its entry sequence holds the line low
 *                for another KST_ENTRY_LOW_US.
 *
 * The way back:
 *
 *   - Before the first frame nothing was said to the servo: an abort, a
 *     stop or a failed entry returns the port to PWM, and a supply switched
 *     on on the way in is switched off.
 *   - After the first frame the servo may be in programming mode until its
 *     supply was off.  The flag LINK_KS_F_IN_MODE says so and the port
 *     stays PROGRAMMING until POWER_CYCLED, REBOOT or LEAVE.  With a PD
 *     mini that
 *     answers and is asked on, both switch it off, wait until it reads off,
 *     keep it off for KST_PORT_RAIL_OFF_MS (RAIL_OFF), switch it on at the
 *     set points it had, wait until it reads on and KST_PORT_RAIL_WAIT_MS
 *     more (RAIL_WAIT), and return the port to PWM.  Without one,
 *     POWER_CYCLED is the user's word that the servo was without power and
 *     REBOOT is refused.
 *   - REBOOT is also taken in PWM, while the PD mini is asked on: the same
 *     cycle, with no pulse on the port during it.
 *   - LEAVE returns the port to PWM without a supply cycle, with a PD
 *     mini or without one.  The port goes to LEAVING: the wire driver
 *     keeps the pin low and no frame goes out for the staged
 *     LINK_KS_W_LEAVE_MS (0 to LINK_KST_LEAVE_MS_MAX, 10 000 ms;
 *     LINK_KST_LEAVE_MS_DEFAULT, 100 ms, until written), counted from the
 *     command.  Then the pin is the generator's, the port is in PWM and
 *     LINK_KS_F_IN_MODE is clear.  The supply is not switched and its set
 *     points are not changed: one this port switched on stays on.  LEAVE
 *     is taken from PROGRAMMING with no operation running.  It is refused
 *     with LINK_KST_REF_BUSY while one runs or plans, with
 *     LINK_KST_REF_MUST_READ while LINK_KS_F_MUST_READ is set, with
 *     LINK_KST_REF_LOCKED while the session is locked and with
 *     LINK_KST_REF_LEAVE_MS for a wait above the range: a servo whose
 *     registers are in doubt leaves through a READ_ALL, a RESTORE or a
 *     supply cycle.  The port does not know whether the servo left
 *     programming mode.  The wait a KST servo needs, and whether a written
 *     value acts before the next power-up, are not measured.
 *   - In LEAVING an ABORT returns the port to PROGRAMMING with
 *     KST_SES_ERR_ABORTED and the session as it was.  A stop does the
 *     same and sets LINK_KS_F_MUST_READ, and a supply found off or asked
 *     off ends as it does in PROGRAMMING: RAIL_OFF, then PWM.  Every
 *     other command is refused with LINK_KST_REF_BUSY.
 *   - Leaving programming restores no earlier, higher voltage.
 *
 * A read that does not come within KST_PORT_SUPPLY_MS ends the cycle with
 * LINK_KST_RESULT_SUPPLY.  Not read off: a port that was in PROGRAMMING is
 * there again with LINK_KS_F_IN_MODE kept, because the servo may have had
 * power throughout.  Not read on: the supply is asked off and the port is
 * in PWM.
 *
 * The supply ends off whenever nobody is there to switch it off.  The
 * SUPPLY page switches the PD mini off when the heartbeat is lost or the
 * link is silent.  A port in PROGRAMMING that finds the supply it started
 * under off or asked off (by that, by the panel, by the PD mini's own
 * protection) ends the operation, keeps the supply off for
 * KST_PORT_RAIL_OFF_MS and returns to PWM with LINK_KS_F_IN_MODE clear and
 * the result KST_SES_ERR_ABORTED: a servo without power is in no mode.  In
 * RAIL_OFF the supply is switched on again only while the heartbeat is
 * trusted; in RAIL_WAIT a supply found asked off ends the cycle the same
 * way.  The operation ABORT ends a cycle off as well: in RAIL_OFF nothing
 * is switched on afterwards, in RAIL_WAIT the supply is switched off.
 *
 * KST_PORT_RAIL_OFF_MS and KST_PORT_RAIL_WAIT_MS are the owner's values.
 * The time a KST servo needs without power to leave programming mode, the
 * time it needs to start, and the supply voltage it accepts in programming
 * mode are not measured.
 *
 * While the port is out of PWM the caller keeps the pulse off the pin:
 * kst_port_quiet_mask() names the programming port that renders none and
 * kst_port_pin_mask() the one whose pin is the wire driver's.  The servo
 * tester's commands end at the port's channel, so none reaches the pin.
 *
 * A stop is the heartbeat lost, the link silent or the bank disarmed.  On a
 * stop the running operation is aborted: a frame on the wire is finished,
 * which takes at most 12 ms, and no other starts.  The pin is low or an
 * input afterwards.  LINK_KS_F_MUST_READ is set, and until a read of all
 * registers has succeeded the port takes ENTER, READ_ALL, ABORT,
 * POWER_CYCLED and REBOOT only.  The operation ABORT does the same to an
 * operation of the session.  While
 * the heartbeat is not trusted or the link is silent no frame starts and
 * every operation but ABORT and POWER_CYCLED is refused.  A disarm alone
 * leaves the supply as it is.
 *
 * A write to the servo (WRITE, RESTORE, RELEASE) is refused unless the
 * command carries LINK_KST_WRITE_KEY.  The plan is built here, from the
 * image this session read and the staged registers, with the planner and
 * the limits of protocols/kst; the panel sends no register write.
 *
 * The session plans a WRITE and a RESTORE itself and runs its own plan
 * (kst_session_edit(), kst_session_restore()).  The command's frame checks
 * the request and prepares the job: a target the planner refuses without a
 * search (register 0x00, pairing, a hard rule) is refused there, with
 * LINK_KST_REF_PLAN_R00, _PAIRING or _RULES, and no operation starts.
 * Every other request starts the operation, and each pass then does one
 * slice of the planner, KST_PLAN_SLICE_UNITS (100) to 109 units of work,
 * and nothing else of the session.  While it plans, LINK_KS_F_BUSY is set,
 * LINK_KS_RESULT's low byte is LINK_KST_RESULT_PLANNING and no frame goes
 * out.  The planning ends after KST_PLAN_WORK_CAP (216,000) units at most,
 * 2,161 passes:
 *
 *   - with a plan: the session reads the servo and writes, as before;
 *   - with no order of writes that keeps every image in between valid:
 *     the result LINK_KST_RESULT_NO_PATH, nothing sent;
 *   - with the cap spent before a verdict: LINK_KST_RESULT_TOO_COMPLEX,
 *     nothing sent.  The request is not known to have no plan;
 *   - a RESTORE whose result cannot be checked, without the confirmation:
 *     KST_SES_ERR_UNCONFIRMED, nothing sent.
 *
 * A slice is 892 us or less and the frame's part 64 us on a Cortex-M33 at
 * 150 MHz, computed from the instructions executed (docs/Performance.md);
 * not measured on hardware.  A RELEASE has one write at most and no
 * search.
 *
 * A stop, an ABORT, a POWER_CYCLED, a REBOOT and a supply found off end an
 * operation that plans as they end one that sends: at the next pass, with
 * KST_SES_ERR_ABORTED, nothing sent and the session not locked.  The rest
 * is what the paragraphs above say for PROGRAMMING, LINK_KS_F_MUST_READ
 * included.  A command while it plans is refused with LINK_KST_REF_BUSY.
 *
 * Commands: registers 0 to 3 of a write, one frame.  SEQ is LINK_KS_SEQ
 * plus 1; the frame just taken, sent again, is acknowledged and does
 * nothing; any other SEQ is refused with LINK_NACK_BAD_VALUE.  A command
 * that is taken is acknowledged, and what became of it is on the page:
 * LINK_KS_RESULT's high byte says why it started nothing, or 0.
 *
 * Nothing here is kept over a restart of the coprocessor: after one the
 * port is in PWM with LINK_KS_F_IN_MODE clear, whatever mode the servo is
 * in, and the SUPPLY page starts with the PD mini asked off.
 *
 * Pure logic: the pin, the driver and the supply are behind kst_port_hw_t.
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
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The port renders no pulse this long before the generator gives up the
 *  pin: 2 frames at 40 Hz, the lowest PWM rate. */
#define KST_PORT_STOP_MS 50u
/** The driver holds the pin low this long before the session starts. */
#define KST_PORT_LOW_MS 100u
/** The servo's supply stays off this long, counted from the read that
 *  shows it off.  The owner's value; what a KST servo needs is not
 *  measured. */
#define KST_PORT_RAIL_OFF_MS 2000u
/** And the servo has this long to start, counted from the read that shows
 *  the supply on.  The owner's value; not measured. */
#define KST_PORT_RAIL_WAIT_MS 1000u
/** The PD mini has this long to read as asked after a switch. */
#define KST_PORT_SUPPLY_MS 3000u
/** The highest supply voltage ENTER takes: the highest servo rail of the
 *  IO board (hardware/docs/IOBoard.md).  What a KST servo accepts in
 *  programming mode is not measured. */
#define KST_PORT_SUPPLY_MAX_MV 8400u
/** The lowest supply voltage a session runs at: an ENTER whose voltage, or
 *  the lower voltage of an output that is on, is under it is refused with
 *  LINK_KST_REF_SUPPLY_LOW.  A chosen value, not measured: the voltage a
 *  KST or Chaservo servo needs to store a register is not known. */
#define KST_PORT_SUPPLY_MIN_MV 4800u

/** What the port needs from the hardware. */
typedef struct {
    void *ctx;
    /** Programming ports the build has, 1 to LINK_PG_PORTS_MAX. */
    uint8_t ports;
    /** The pin the PWM generator has for @p port, or a value above
     *  OUT_MAX_PIN while it has none. */
    uint8_t (*pin)(void *ctx, uint8_t port);
    /** Take @p pin of @p port from the generator and open the wire driver
     *  on it, which holds it low.  False when the driver gets no resources;
     *  give() returns the pin. */
    bool (*take)(void *ctx, uint8_t port, uint8_t pin);
    /** Close the wire driver and give the pin back to the generator.
     *  Called with the port in LINK_KST_PWM. */
    void (*give)(void *ctx);
    /** What the supply says now (supply_page_port_view()).  NULL, with
     *  supply_set: the build has no supply to switch. */
    void (*supply)(void *ctx, supply_port_view_t *out);
    /** Ask the supply on at @p mv and @p ma, or off (the set points are
     *  then not read).  False when it does not take the request. */
    bool (*supply_set)(void *ctx, bool on, uint16_t mv, uint16_t ma);
    /** The wire driver.  now_us() works while the driver is closed. */
    kst_driver_t driver;
} kst_port_hw_t;

typedef struct {
    kst_port_hw_t hw;
    kst_session_t session;
    kst_image_t work;    /**< the staged image of a command: not on the
                              stack */
    uint16_t staged[LINK_KS_W_STAGED];
    uint16_t last_cmd[LINK_KS_W_CMD_FRAME];
    uint32_t t0_ms;
    uint32_t t1_ms;
    uint16_t seq;
    uint16_t frames;
    uint16_t supply_mv;  /**< staged: what the next ENTER asks */
    uint16_t supply_ma;
    uint16_t want_mv;    /**< what the running ENTER asks */
    uint16_t want_ma;
    uint16_t back_mv;    /**< what RAIL_WAIT switches on at */
    uint16_t back_ma;
    uint16_t leave_ms;   /**< staged: how long the next LEAVE waits */
    uint16_t wait_ms;    /**< how long the running LEAVE waits */
    uint8_t  state;      /**< link_kst_state_t */
    uint8_t  port;
    uint8_t  pin;
    uint8_t  op;         /**< link_kst_op_t of the last operation started */
    uint8_t  result;     /**< kst_ses_t of that operation, or a
                              LINK_KST_RESULT_* */
    uint8_t  refusal;    /**< link_kst_refusal_t of the last command */
    uint8_t  fail_reg;
    uint8_t  steps_done;
    uint8_t  seen_step;
    uint8_t  busy;       /**< an operation runs */
    uint8_t  running;    /**< it is the session's */
    uint8_t  leaving;    /**< why PROGRAMMING ends when the session has */
    uint8_t  framed;     /**< a frame went out since the servo had no power */
    uint8_t  must_read;
    uint8_t  view;
    uint8_t  safe;
    uint8_t  armed;       /**< the bank is armed: no ENTER is taken */
    uint8_t  has_pin;    /**< the pin is the wire driver's */
    uint8_t  sup;        /**< where the wait for the supply is */
    uint8_t  on_here;    /**< this ENTER switched the supply on */
    uint8_t  watch;      /**< the session started under a supply that was
                              read on */
    uint8_t  again;      /**< RAIL_OFF switches the supply on afterwards */
    uint8_t  from_pwm;   /**< the cycle started in PWM */
} kst_port_t;

/** Prepare a port in LINK_KST_PWM.  False when @p hw names no port, lacks
 *  a function other than the supply's two, or has one of those two. */
bool kst_port_init(kst_port_t *p, const kst_port_hw_t *hw);

/**
 * A write of the page: 0, or the LINK_NACK_* reason with nothing changed.
 *
 * A window that starts in registers 0 to 3 is a command and has to be
 * exactly those 4.  Registers 4 to 28 are taken in any windows; 29 and up
 * are LINK_NACK_READ_ONLY.
 */
uint8_t kst_port_write(kst_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in, uint32_t now_ms);

/** A read of the page, LINK_KS_*. */
void kst_port_read(const kst_port_t *p, uint8_t off, uint8_t n,
                   uint16_t *out);

/**
 * One pass.  @p safe: the heartbeat is trusted and the link is not silent.
 * @p stop: the bank was switched off or disarmed since the last pass.
 * Steps the session and the port's timers; a pass every 1 ms or faster
 * keeps the session's pacing.  A pass does one slice of planning at most
 * and never a whole search.
 */
void kst_port_step(kst_port_t *p, bool safe, bool stop, uint32_t now_ms);

/** Where the port is. */
link_kst_state_t kst_port_state(const kst_port_t *p);

/** Bit n: programming port n renders no PWM pulse.  The held port in every
 *  state but LINK_KST_PWM. */
uint8_t kst_port_quiet_mask(const kst_port_t *p);

/** An operation of the session runs: a frame may be on the wire.  False
 *  from the pass in which an operation, or its cut, has ended. */
bool kst_port_frame_out(const kst_port_t *p);

/** Whether the bank is armed.  An ENTER is refused with LINK_KST_REF_BUSY
 *  while it is; the caller arms no output while kst_port_quiet_mask() is
 *  not 0. */
void kst_port_bench_armed(kst_port_t *p, bool armed);

/** Bit n: programming port n's pin is the wire driver's, not the PWM
 *  generator's.  The held port from LINK_KST_LOW until it is back in
 *  LINK_KST_PWM; never for a REBOOT taken in PWM. */
uint8_t kst_port_pin_mask(const kst_port_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_KST_PORT_H */
