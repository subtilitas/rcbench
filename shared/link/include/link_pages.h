/*
 * The page map: what the panel can ask the coprocessor for, and set.
 *
 * A page is a window of up to LINK_MAX_REGS 16-bit registers with one
 * function.  Adding a capability adds a page, not a message type, which
 * keeps the wire format stable while the bench grows.
 *
 * Pages are numbered with room between the groups.  A page number is part of
 * the contract between two firmwares that are flashed separately and can
 * disagree about their versions, so renumbering an existing page is a
 * breaking change; the gaps let a new page land beside related ones.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_PAGES_H
#define RCBENCH_LINK_PAGES_H

#include <stdbool.h>

#include "link_crc.h"
#include "link_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_PAGE_IDENTITY = 0x00, /**< who and what, read-only      */
    LINK_PAGE_STATUS   = 0x01, /**< how it is, read-only         */
    LINK_PAGE_CONTROL  = 0x10, /**< what to do                   */
    LINK_PAGE_LIMITS   = 0x11, /**< when to stop without asking  */
    LINK_PAGE_FAILSAFE = 0x12, /**< where to go when nobody asks */
    LINK_PAGE_CHANNELS = 0x13, /**< what every output is asked for */
    LINK_PAGE_BENCH    = 0x20, /**< the numbers, read-only       */
    /*
     * 0x21 is reserved and not assigned.  A page number is part of the
     * contract between two firmwares flashed separately; a number that
     * changes meaning lets an old panel write into a page the coprocessor
     * interprets differently, and be acknowledged.
     */
    LINK_PAGE_OUTPUTS  = 0x22, /**< which driver drives what, on which pin */
    LINK_PAGE_CHAN_CFG = 0x23, /**< what each channel is and how it moves  */
    LINK_PAGE_CATALOGUE = 0x24, /**< the board's own pins, read-only       */
    LINK_PAGE_SHAPE     = 0x25, /**< where those pins are, read-only       */
    LINK_PAGE_ARTWORK   = 0x26, /**< what the board's drawing is           */
    LINK_PAGE_ART_DATA  = 0x27, /**< and the drawing itself, a block at a time */
    LINK_PAGE_PADS      = 0x28, /**< the pads that are not pins, read-only  */
    LINK_PAGE_SERVO     = 0x29, /**< the target's frame rate, not kept      */
    LINK_PAGE_SUPPLY    = 0x2A, /**< the PD mini on a PIO UART; wiring kept */
    LINK_PAGE_SENSE     = 0x2B, /**< the current monitors' bus; set-up kept */
    LINK_PAGE_SERVO_SENSE = 0x2C, /**< the servo rail's channels, a move timed */
    LINK_PAGE_TONE      = 0x2D, /**< ESC tones from one motor phase; set-up kept */
    LINK_PAGE_BIND_CFG  = 0x2E, /**< a CHAN_CFG page prepared, not in force  */
    LINK_PAGE_BIND_OUT  = 0x2F, /**< an OUTPUTS page prepared, not in force  */
    LINK_PAGE_BIND      = 0x30, /**< takes the two prepared pages as one     */
    LINK_PAGE_SERVO_WIN = 0x31, /**< the servo rail's last CH1 windows, read-only */
    LINK_PAGE_KST       = 0x32, /**< the KST servo programming port          */
    LINK_PAGE_PROG      = 0x33, /**< the programming ports: pin, pulse, target */
    LINK_PAGE_BUS       = 0x34, /**< a serial bus servo protocol on a programming port */
    LINK_PAGE_BUS_IDS   = 0x35, /**< the IDs the bus's last scan found, read-only */
    /* Frozen: the same numbers, layout and meaning under every major. */
    LINK_PAGE_UPDATE_CTL  = 0xF0, /**< the coprocessor's own firmware update */
    LINK_PAGE_UPDATE_DATA = 0xF1, /**< and the 64 bytes of the block in hand */
} link_page_id_t;

/*
 * The protocol version is register 0 of page 0: the one thing a host must be
 * able to read from a coprocessor whose other pages it might not understand.
 * Bump the major when a register changes meaning or a page is renumbered;
 * bump the minor when a page or a register is added at the end, which an
 * older host can ignore.
 *
 * Only the major has to agree for the link to come up and the bench to arm.
 * The panel reads the minor at link-up and sends nothing to a page the
 * coprocessor's minor does not have: SUPPLY from 4.3, SENSE and
 * SERVO_SENSE from 4.7, TONE from 4.8, and SENSE's output encoder (the
 * ENABLE bit LINK_SN_EN_AS5600 and registers 26 to 30) from 4.9, and
 * BIND_CFG, BIND_OUT and BIND from 4.10, and SERVO_WIN, SENSE's RESETS
 * and a signed CAP_HOLD_MA from 4.11, and KST from 4.12, and PROG, the port
 * fields of KST, CAP_ARM bit 11 and SUPPLY FLAGS bit 11 from 4.13.  From
 * 4.14 the servo programming page (KST, 0x32) plans a write after the
 * command's frame and reports LINK_KST_RESULT_PLANNING, _NO_PATH and _TOO_COMPLEX; nothing is sent
 * differently for it.  LINK_KST_OP_LEAVE and LINK_KS_W_LEAVE_MS go to a
 * coprocessor from 4.15 only.  From 4.16 CHAN_CFG and PROG refuse a slew
 * above 3000, and CHAN_CFG, OUTPUTS, BIND, SERVO and PROG refuse a write that would leave a PWM output with an endpoint longer
 * than its frame (out_pwm_pulse_max()); the panel states a port's range
 * before it switches the port's pulse on, which an older coprocessor takes
 * as well.  From 4.17 the
 * TONE page refuses a busy pin with LINK_NACK_BUSY and keeps BAD_VALUE for
 * a set-up no freed pin makes good; the panel offers the first frame again
 * after a BUSY only.  From 4.18 CONTROL's ARM names the roles it arms
 * (LINK_ARM_THROTTLE, LINK_ARM_SURFACE); the panel writes LINK_ARM_ALL to an
 * older coprocessor, which arms its whole bank on any non-zero ARM.  From
 * 4.19 the SERVO rate reaches the tester's target only (the standard servo
 * ports' surfaces, or the one programming port PROG's TARGET names) and a
 * TARGET change returns it to 0; the panel then counts the rate as 0 after
 * a TARGET change it saw, and as not known after a TARGET write that went
 * unacknowledged.  From 4.20 ARTWORK serves the board's drawing
 * (LINK_ART_DRAWING) with a hash of its payload in registers 7 and 8; the
 * panel reads ARTWORK from 4.20 only and draws an older coprocessor's board
 * from its shape page.  From 4.21 STATUS has 10 registers: registers 8 and
 * 9 count the CAN frames the coprocessor refused to decode; the panel asks
 * an older coprocessor for the 8 it has (LINK_ST_COUNT_V420).  From 4.22
 * UPDATE_CTL and UPDATE_DATA take a new coprocessor image over the link;
 * an older coprocessor refuses both with LINK_NACK_BAD_PAGE, which the panel
 * answers by saying a USB (Universal Serial Bus) load is needed.  From 4.23
 * the BUS page carries a serial bus servo protocol on a programming port;
 * the panel sends nothing to it on an older coprocessor and its SERVO
 * screen's BUS page says the coprocessor has none.  A
 * coprocessor never asks the panel's minor; a page an older panel does not
 * know is a page it never writes.
 *
 * Frozen across majors.  Registers 0 to 6 of IDENTITY (LINK_ID_*) and the
 * two update pages keep their numbers, their layout and their meaning under
 * every later major, and a coprocessor of any later major answers them.  A
 * panel reads them before it compares majors, so a panel and a coprocessor
 * whose majors differ still find out what the other runs and can still move
 * the coprocessor to the panel's own image.  Changing one of them is not a
 * major change; it is a new page.
 */
#define LINK_PROTOCOL_MAJOR 4u
#define LINK_PROTOCOL_MINOR 23u

/** The first minor that serves BIND_CFG, BIND_OUT and BIND. */
#define LINK_MINOR_BIND 10u
/** The first minor that serves SERVO_WIN, counts resets in SENSE register
 *  31 and takes a negative CAP_HOLD_MA. */
#define LINK_MINOR_SERVO_WIN 11u
/** The first minor that serves KST. */
#define LINK_MINOR_KST 12u
/** The first minor that serves PROG, names a programming port in a KST
 *  command and in CAP_ARM, and says in SUPPLY FLAGS that a port holds the
 *  supply. */
#define LINK_MINOR_PROG 13u
/** The first minor whose KST page plans a WRITE and a RESTORE over several
 *  passes: LINK_KST_RESULT_PLANNING while it does, LINK_KST_RESULT_NO_PATH
 *  and LINK_KST_RESULT_TOO_COMPLEX as results.  Minor 13 plans inside the
 *  command's frame and refuses with LINK_KST_REF_PLAN_NO_PATH. */
#define LINK_MINOR_KST_PLAN 14u
/** The first minor whose KST page takes LINK_KST_OP_LEAVE and the register
 *  LINK_KS_W_LEAVE_MS: PROGRAMMING back to PWM with the supply untouched.
 *  Minor 14 refuses the operation with LINK_NACK_BAD_VALUE and the register
 *  with LINK_NACK_READ_ONLY. */
#define LINK_MINOR_KST_LEAVE 15u
/** The first minor that refuses a pulse longer than its frame with
 *  LINK_NACK_BAD_VALUE: an endpoint above out_pwm_pulse_max() of the rate
 *  the PWM output runs at, on CHAN_CFG, OUTPUTS, BIND, SERVO and PROG, and
 *  a stored slot with one starts unbound.  Minor 15 takes the write and
 *  clamps the pulse to the frame's last count, while the page reads back
 *  the endpoint written. */
#define LINK_MINOR_PULSE_FRAME 16u
/** The first minor whose TONE page refuses a tap on a busy pin with
 *  LINK_NACK_BUSY: a pin SUPPLY, SENSE, PROG or an output holds, or a
 *  capture the wiring cannot start for now.  BAD_VALUE is left for a set-up
 *  no freed pin makes good.  Minor 16 and older refuse both with
 *  LINK_NACK_BAD_VALUE. */
#define LINK_MINOR_TONE_BUSY 17u
/** The first minor whose CONTROL ARM arms only the roles it names:
 *  LINK_ARM_THROTTLE, LINK_ARM_SURFACE or both.  It refuses any other
 *  non-zero value but LINK_ARM_ALL with LINK_NACK_BAD_VALUE.  An older minor arms
 *  every output on any non-zero ARM. */
#define LINK_MINOR_ARM_ROLE 18u
/** The first minor whose SERVO rate is the tester's target's alone: the
 *  surfaces of the standard servo ports with them as PROG's TARGET, else
 *  the TARGET port's pin, the slots at their own rates and the other ports
 *  at 50 Hz.  A TARGET change returns the rate to 0.  An older minor runs
 *  every PWM surface and every programming port at the one SERVO rate,
 *  whatever the TARGET. */
#define LINK_MINOR_SERVO_TARGET_RATE 19u
/** The first minor whose ARTWORK page serves the board's drawing
 *  (LINK_ART_DRAWING) and the hash of its payload in LINK_AW_HASH_LO and
 *  LINK_AW_HASH_HI.  An older minor serves a photograph (LINK_ART_RGB565)
 *  in seven registers, which this panel does not read. */
#define LINK_MINOR_DRAWING 20u
/** The first minor whose STATUS page counts the CAN frames it refused to
 *  decode, in LINK_ST_REFUSED_LO and LINK_ST_REFUSED_HI.  An older minor's
 *  page has LINK_ST_COUNT_V420 registers and refuses a longer read with
 *  LINK_NACK_BAD_RANGE. */
#define LINK_MINOR_REFUSED 21u
/** The first minor that serves UPDATE_CTL and UPDATE_DATA.  An older minor
 *  refuses both with LINK_NACK_BAD_PAGE; it takes a new image over USB
 *  only. */
#define LINK_MINOR_UPDATE 22u
/** The first minor that serves BUS.  An older minor refuses it with
 *  LINK_NACK_BAD_PAGE. */
#define LINK_MINOR_BUS 23u

/* ----------------------------------------------------------------- outputs */

/*
 * The three output pages.  Every output protocol has a proportion of travel
 * and a driver that renders it, so that is what the wire carries:
 *
 *   CHANNELS  what each output is asked for, and nothing else.  Hot: written
 *             many times a second, and the only page in the group that is.
 *   CHAN_CFG  what a channel is (throttle or surface), how fast it may move,
 *             and the pulse widths its ends correspond to.
 *   OUTPUTS   which driver renders which channels, on which pin, how often.
 *
 * Eight of each.  At four registers per slot, eight slots fit a page of
 * LINK_MAX_REGS registers; eight is also the number of servo outputs the
 * bench has and the number of channels PPM (pulse-position modulation)
 * carries.  A ninth slot needs a minor version and a second page.
 */

#define LINK_OUT_SLOTS     8u
#define LINK_OUT_CHANNELS  8u

/** A command runs 0..LINK_CH_SPAN across the channel's own travel. */
#define LINK_CH_SPAN    1000u

/* --- what each output is asked for */
#define LINK_CH_COUNT   LINK_OUT_CHANNELS

/* --- what a channel is.  Four registers each, and a channel must be written
 *     whole: min and max are a pair, and a range half-applied is a range
 *     nothing is clamped against. */
enum {
    LINK_CC_ROLE    = 0, /**< 0 throttle, 1 surface                     */
    LINK_CC_SLEW    = 1, /**< span a second, 0 to LINK_CC_SLEW_MAX; 0 means at once */
    LINK_CC_MIN_US  = 2, /**< the pulse a command of 0 renders as       */
    LINK_CC_MAX_US  = 3, /**< and of LINK_CH_SPAN                       */
    LINK_CC_STRIDE  = 4,
};
#define LINK_CC_COUNT  (LINK_OUT_CHANNELS * LINK_CC_STRIDE)

/** The fastest slew a write may name, in thousandths of the span a second:
 *  3000, 300 %/s, the panel's own maximum.  A higher value is refused with
 *  BAD_VALUE. */
#define LINK_CC_SLEW_MAX 3000u

#define LINK_CC_ROLE_THROTTLE 0u
#define LINK_CC_ROLE_SURFACE  1u

/*
 * A channel's range before anything is written, and the widest range the
 * coprocessor accepts for the limits themselves.  The floor is a 760 us tail
 * servo's: its travel ends at 410 us, and a standard servo asked for that
 * buzzes rather than moves.
 *
 * The limits are enforced at the coprocessor, the end holding the wire, so a
 * restarted, reflashed or wrong host cannot drive a servo into its stops.
 *
 * A PWM output's endpoints also fit its frame (since 4.16): neither longer
 * than out_pwm_pulse_max() of the rate the output runs at, one microsecond
 * under the period -- 1784 us at 560 Hz, 2499 us at 400 Hz.  Below 400 Hz
 * the ceiling is the shorter of the two.  A write that would leave a
 * longer one is refused with BAD_VALUE (outputs_pulse_check()).
 */
#define LINK_CC_DEFAULT_MIN 1000u
#define LINK_CC_DEFAULT_MAX 2000u
#define LINK_CC_FLOOR_US     400u
#define LINK_CC_CEILING_US  2500u

/* --- which driver drives what.  Four registers a slot, written whole for the
 *     same reason: a driver claimed on a pin the rest of the write has not
 *     arrived to name yet is a driver on the wrong pin. */
enum {
    LINK_OS_DRIVER  = 0, /**< link_out_driver_t                          */
    LINK_OS_PIN     = 1, /**< which pin it drives                        */
    /*
     * First channel and count in one register: the run of channels this slot
     * renders.  PPM is eight channels on one pin, so a slot cannot be assumed
     * to be one channel.
     */
    LINK_OS_RANGE   = 2, /**< (first << 8) | count                       */
    LINK_OS_RATE_HZ = 3, /**< frames a second, or kbit/s for DShot       */
    LINK_OS_STRIDE  = 4,
};
#define LINK_OS_COUNT  (LINK_OUT_SLOTS * LINK_OS_STRIDE)

/* --- the SERVO screen's page: the target's frame rate (protocol 4.1) and
 *     the sweep that drives it through a curve (4.2).
 *
 *     FRAME_HZ is the frame rate, in Hz, of the tester's target (PROG's
 *     LINK_PG_TARGET), or 0 for its own rate.  With the standard servo
 *     ports as target it is every PWM output whose first channel is a
 *     surface, 0 meaning each slot's own rate from the OUTPUTS page.  One
 *     register for all of them, so a write is whole: a servo screen that
 *     moved some surfaces to 560 Hz and not their slice-mates would leave a
 *     slice asked for two rates.  A rate that would do that against the
 *     binding in force -- a surface and a throttle on one slice -- is
 *     refused with BAD_VALUE and nothing changes.  With a programming port
 *     as target (since 4.19) it is that port's pin alone, 0 meaning 50 Hz;
 *     the slots run at their own rates.  A port pin that shares its PWM
 *     slice with another port's pin takes only 0 or 50 (BAD_VALUE).  Every
 *     port that is not the target runs at 50 Hz, and a TARGET change sets
 *     FRAME_HZ to 0.  A coprocessor before 4.19 runs every surface and
 *     every port at FRAME_HZ, whatever the target.
 *
 *     SWEEP to SWEEP_DWELL_MS are a sweep (servo_sweep.h), four registers so
 *     a start fits one frame.  SWEEP is the curve, 0 stopped, 1 square,
 *     2 sine, 3 triangle; MHZ thousandths of a cycle a second, 50 to 5000;
 *     SPAN command units either side of the centre, 0 to 500; DWELL_MS the
 *     hold at each end, 0 to 5000.  A sweep needs the bench armed
 *     (NOT_ARMED) and stops after 500 ms unwritten, on a disarm, or after
 *     SWEEP_MOVES ends (0 for no end), read back in SWEEP_DONE.
 *
 *     SWEEP = 4 (LINK_SV_HOLD), one register, holds every surface where
 *     its output is -- a sweep stopped part way, exactly where it got to,
 *     slew and all -- for as long as it is written, on the same 500 ms and
 *     disarm rules.  A sweep that was running when the hold began keeps its
 *     phase: how far into the curve it was, so its point, its dwell and its
 *     movements reached.  One that had made its movements is held at the
 *     centre it ended on.  A write is judged against the page as a pass at
 *     the same moment leaves it: a sweep past its last movement, unwritten
 *     for 500 ms or disarmed has ended, even before the pass that ends it.
 *
 *     SWEEP = 5 (LINK_SV_RESUME, since 4.6), one register, carries that
 *     sweep on from the kept phase.  The surfaces are commanded along the
 *     curve again at once and slew there from where they were held, at
 *     their own rate.  Refused with BAD_VALUE when no phase is kept -- no
 *     sweep was running when the hold began, or the hold has ended by a
 *     write of 0, a curve written over it, a disarm, 500 ms unwritten or
 *     a restart -- and when
 *     SWEEP_MHZ, SPAN, DWELL_MS or MOVES no longer read what the paused
 *     sweep runs; NOT_ARMED on a disarmed bench.  A 4.5 coprocessor refuses
 *     5 with BAD_VALUE, as any curve not written whole.
 *
 *     Not kept: a coprocessor restart drives every slot at its own rate
 *     again, which is the binding's 50 Hz for a servo, and runs no sweep. */
enum {
    LINK_SV_FRAME_HZ       = 0,
    LINK_SV_SWEEP          = 1,
    LINK_SV_SWEEP_MHZ      = 2,
    LINK_SV_SWEEP_SPAN     = 3,
    LINK_SV_SWEEP_DWELL_MS = 4,
    LINK_SV_SWEEP_MOVES    = 5,
    LINK_SV_SWEEP_DONE     = 6,   /**< read only */
    LINK_SV_COUNT          = 7,
};
/** SWEEP's value for holding the surfaces where their outputs are. */
#define LINK_SV_HOLD 4u
/** SWEEP's value for carrying a held sweep on from its phase (4.6). */
#define LINK_SV_RESUME 5u

/* --- the SUPPLY page (protocol 4.3): a WeAct PD Power Mini V1 Buck on a
 *     PIO UART the coprocessor runs on two of its pins (shared/bench/
 *     pdmini.h).
 *
 *     ENABLE to BAUD are its wiring, one frame: 1 to drive it, the GPIO the
 *     UART transmits on (to the module's DM) and receives on (its DP), and
 *     the module's UART Baudrate setting, 0 to 6 for 9600 to 460800 baud,
 *     or 7 (protocol 4.4) to find it: the UART steps to the next rate
 *     after every WHO_AM_I without a valid answer, until a module has
 *     answered once, and BAUD_FOUND says which rate is in use.
 *
 *     RESET, written alone as 1 with OUTPUT 0, restarts the module
 *     (SYSTEM_RESET) once it has answered and its output reads off; it
 *     reads 0.
 *     Refused with BAD_VALUE: a pin that is reserved, bound to an output or
 *     the other pin, a baud out of range, or any change while the output is
 *     asked on or may be on (FLAGS bit 6).  The pins it takes are no
 *     output's while it holds them.
 *
 *     Protocol 4.5: a change while a module has answered is acknowledged
 *     and waits, ENABLE to BAUD reading the wiring in force and FLAGS bit 8
 *     set, for a state read of the module sent after it -- at most about
 *     1.2 s.  That read showing the output off takes it; on, or failed,
 *     refuses it: bit 8 clears and bit 9 sets until the next wiring write.
 *     Such a change comes alone, without OUTPUT to SET_MA, and an ON is
 *     refused with BAD_VALUE while one waits.
 *
 *     OUTPUT to SET_MA are what it is to do, one frame: 1 for the output
 *     on, the set points in mV and mA, clamped to the module's 1 to 20 V
 *     and 0.05 to 3 A.  An ON is refused with NOT_ARMED without a live
 *     heartbeat, and the output goes off when the heartbeat stops.
 *
 *     FLAGS onwards are read only, what the module last said: bit 0
 *     online, bit 1 output on, bits 3..2 the mode (0 normal, 1 constant
 *     current, 2 overcurrent), bit 4 an output that would not switch,
 *     bit 5 set points that would not take, bit 6 an output that is on or
 *     may be -- read on, an ON not yet confirmed, or an OFF owed to a
 *     module that stopped answering, bit 7 an output the module switched
 *     off by itself, held off until OUTPUT is written 0, bit 8 a wiring
 *     change waiting for its state read and bit 9 one refused by it
 *     (4.5), bit 10 an output switched off because its input read under
 *     the set point and 500 mV on 2 input reads in a row, held off until
 *     OUTPUT is written 0 (4.5);
 *     the output's voltage and current, the set points read back from it,
 *     the input's state and voltage, and the counts of readings taken and
 *     of transactions that failed, modulo 65536.
 *
 *     The wiring is kept in the coprocessor's flash and driven at boot with
 *     the output off, so a module left on is switched off after a restart;
 *     the command is not kept. */
enum {
    LINK_SP_ENABLE    = 0,
    LINK_SP_TX_PIN    = 1,
    LINK_SP_RX_PIN    = 2,
    LINK_SP_BAUD      = 3,
    LINK_SP_OUTPUT    = 4,
    LINK_SP_SET_MV    = 5,
    LINK_SP_SET_MA    = 6,
    LINK_SP_FLAGS     = 7,   /**< read only from here */
    LINK_SP_V_MV      = 8,
    LINK_SP_I_MA      = 9,
    LINK_SP_SET_MV_RB = 10,
    LINK_SP_SET_MA_RB = 11,
    LINK_SP_IN_STATE  = 12,
    LINK_SP_VIN_MV    = 13,
    LINK_SP_SAMPLES   = 14,
    LINK_SP_ERRORS    = 15,
    LINK_SP_BAUD_FOUND = 16,  /**< protocol 4.4: the rate in use, 0..6, 7 none */
    LINK_SP_RESET     = 17,   /**< protocol 4.4: write 1 to restart the module */
    LINK_SP_COUNT     = 18,
};
#define LINK_SP_ONLINE  0x01u
#define LINK_SP_ON      0x02u
#define LINK_SP_MODE(f) (((f) >> 2) & 3u)
#define LINK_SP_STUCK   0x10u
#define LINK_SP_SET_STUCK 0x20u
#define LINK_SP_LIVE      0x40u
#define LINK_SP_TRIPPED   0x80u
#define LINK_SP_WIRE_WAIT    0x100u  /**< protocol 4.5 */
#define LINK_SP_WIRE_REFUSED 0x200u  /**< protocol 4.5 */
#define LINK_SP_SAGGED       0x400u  /**< protocol 4.5 */
/** Protocol 4.13: a programming port holds OUTPUT, SET_MV and SET_MA.  A
 *  write of them is refused unless it leaves OUTPUT 0 and SET_MV no
 *  higher. */
#define LINK_SP_PORT         0x800u

/* --- the SENSE page (protocol 4.7): two I2C (Inter-Integrated Circuit)
 *     current monitors on one bus the coprocessor runs on two of its pins,
 *     a TI INA228 in the ESC's power path and a TI INA3221 on the servo
 *     rail.
 *
 *     ENABLE to KHZ are the bus, one frame: ENABLE bit 0 the INA228, bit 1
 *     the INA3221, bit 2 the AS5600 output encoder (protocol 4.9); the GPIO for SDA and the one for SCL; the clock, 400 kHz
 *     and nothing else (LINK_SN_KHZ_BUS): the schedule's 1 ms tick does not
 *     fit a slower bus.  The RP2350 has its I2C function at pin mod 4 -- 0 I2C0 SDA,
 *     1 I2C0 SCL, 2 I2C1 SDA, 3 I2C1 SCL -- so SDA is a GPIO whose number
 *     mod 4 is 0 or 2 and SCL is the one after it: one I2C block's pair.
 *
 *     I228_ADDR to register 7 are the INA228, one frame: its address, 0x40
 *     to 0x4F; its shunt in micro-ohms, 50 to 20000; the current its range
 *     is set for, in 0.1 A, 1.0 to 300.0 A, the bench's design maximum.
 *     That current chooses ADCRANGE only: 1
 *     while the shunt's voltage at it is at most 40.96 mV, 0 up to
 *     163.84 mV, and past that the write is refused.  CURRENT_LSB is the
 *     shunt ADC's step divided by the shunt at that range, so CURRENT and
 *     VSHUNT clip together, and SHUNT_CAL is 4096 at either range.  A
 *     shunt whose full scale at the chosen range passes 2000 A is refused
 *     too: below 81.92 micro-ohms only ADCRANGE 1 is taken.  The rule is
 *     the driver's, ina228_calibrate() in shared/sense.
 *
 *     I3221_ADDR to register 11 are the INA3221, one frame: its address,
 *     0x40 to 0x43; its shunt in 0.1 milliohm, 50 to 10000 (5 mOhm to
 *     1 Ohm); the channels read, bits 0..2 for CH1 to CH3, at least one
 *     while the part is enabled.  The part's full scale is 163.8 mV across
 *     the shunt: 1.638 A at the DAOKAI module's 0.1 Ohm, 32.76 A at 5 mOhm,
 *     which is the most SERVO_SENSE's signed mA carry.
 *
 *     Registers 7 and 11 are reserved: they read 0, and a write of anything
 *     else is refused with BAD_VALUE.  They keep each part's frame four
 *     registers long for a later minor to give them a meaning.
 *
 *     Refused with BAD_VALUE: a value out of its range, SDA and SCL not one
 *     I2C block's pair (link_sn_pins_pair()) whatever ENABLE holds, and
 *     while a part is enabled a pin past the bank, reserved, bound to an
 *     output or held by the SUPPLY page, the two parts on one address while
 *     both are enabled, and any change while the bank is armed -- the bus
 *     opened again stops the readings for some milliseconds mid-run.  One
 *     frame that is not a pair is taken: ENABLE 0 with SDA or SCL 0, which
 *     is how a panel writes pins that are not set.  The pins are no
 *     output's while a part is enabled.
 *
 *     FLAGS onwards are read only: FLAGS (link_sense_flag_t); PRESENT, bit
 *     n for address 0x40 + n answering the last scan; the ID the INA228
 *     gave (DEVICE_ID) and the one the INA3221 gave (die ID); transactions
 *     failed, modulo 65536; the INA228's die temperature in 0.1 C, signed,
 *     and its DIAG_ALRT as read; its CHARGE in 0.01 mAh, signed 32 bit, and
 *     its ENERGY in 0.01 Wh, 32 bit, each low register first and counted
 *     since the run's arm; and the ESC's own telemetry voltage (10 mV) and
 *     current (10 mA), ESC_FLAGS bit 0 and bit 1 saying each is valid --
 *     here because BENCH carries the INA228's while it answers.
 *
 *     The output encoder (protocol 4.9) is an ams OSRAM AS5600 magnetic
 *     angle sensor on the same bus, at its fixed address 0x36, on the horn
 *     shaft of the servo under test.  ENABLE bit 2 turns it on and has no
 *     parameter of its own: the bus pins are the INA parts', and the angle
 *     comes back as the 12-bit count, 0 to 4095 for one turn (0.0879 degrees
 *     a count), with no centre applied -- the panel keeps that.  The
 *     coprocessor reads it at 500 Hz.  A bit 2 written to a 4.8 or older
 *     coprocessor is not sent: the panel gates it on minor >= 9.
 *
 *     AS5600_FLAGS (link_sense_enc_flag_t) say whether it answers and what
 *     its magnet is doing.  AS5600_ANGLE is RAW ANGLE as last read, 0 until
 *     the first read of a set-up.  It is a position only while ENC_ONLINE,
 *     ENC_VALID and ENC_MD are all set: with MD clear the part sees no
 *     magnet and the count means nothing.  With ML or MH set beside MD the
 *     count is a position whose noise the datasheet does not specify.  AS5600_MAGNITUDE is the part's CORDIC
 *     (coordinate rotation digital computer) magnitude, read at 20 Hz, 0
 *     until read; it is a measure of the field.  AS5600_SAMPLES counts the
 *     angle reads, modulo 65536: two reads of the page with the same count
 *     are one sample.  AS5600_STILL_MS is how long the angle has stayed
 *     within 12 counts (1.05 degrees) of an anchor -- the angle at the last
 *     time it left that band -- in milliseconds, saturating at 65535, 0 with
 *     no sample and 0 while MD is clear: without a magnet the count is not
 *     a position, and the anchor starts afresh when MD returns.  A move that ends at time t_end, read at time t_read, shows
 *     STILL_MS = t_read - t_end to within the 2 ms sample interval, so the
 *     panel times the end of a move to the coprocessor's resolution
 *     whatever its own polling interval.
 *
 *     RESETS (register 31, protocol 4.11) counts the times a current
 *     monitor was found running on its power-on set-up and not the one
 *     written to it: the INA228 in the low byte, the INA3221 in the high
 *     byte, each modulo 256, since the set-up in force was taken.  The
 *     coprocessor reads the INA228's ADC_CONFIG and the INA3221's
 *     Configuration back every 40 ms; a part found reset loses ONLINE and
 *     is set up again 1000 ms on.  The count shows a reset that was found
 *     and repaired between two reads of FLAGS.  Before 4.11 the register
 *     is reserved and reads 0.
 *
 *     ENABLE to register 11 are kept in the coprocessor's flash beside the
 *     bindings and the supply's wiring, and the bus is opened at boot. */
enum {
    LINK_SN_ENABLE          = 0,
    LINK_SN_SDA_PIN         = 1,
    LINK_SN_SCL_PIN         = 2,
    LINK_SN_KHZ             = 3,
    LINK_SN_I228_ADDR       = 4,
    LINK_SN_I228_SHUNT_UOHM = 5,
    LINK_SN_I228_MAX_DA     = 6,
    LINK_SN_RESERVED_7      = 7,   /**< reads 0; written 0                  */
    LINK_SN_I3221_ADDR      = 8,
    LINK_SN_I3221_SHUNT_DMOHM = 9,
    LINK_SN_I3221_CHANNELS  = 10,
    LINK_SN_RESERVED_11     = 11,  /**< reads 0; written 0                  */
    LINK_SN_FLAGS           = 12,  /**< read only from here                 */
    LINK_SN_PRESENT         = 13,
    LINK_SN_I228_ID         = 14,
    LINK_SN_I3221_ID        = 15,
    LINK_SN_ERRORS          = 16,
    LINK_SN_I228_TEMP_DC    = 17,  /**< 0.1 C, signed -- read as int16_t    */
    LINK_SN_I228_DIAG       = 18,
    LINK_SN_I228_CHARGE_LO  = 19,  /**< 0.01 mAh, int32_t, low half first   */
    LINK_SN_I228_CHARGE_HI  = 20,
    LINK_SN_I228_ENERGY_LO  = 21,  /**< 0.01 Wh, uint32_t, low half first   */
    LINK_SN_I228_ENERGY_HI  = 22,
    LINK_SN_ESC_VOLTAGE_CV  = 23,  /**< 10 mV steps                         */
    LINK_SN_ESC_CURRENT_CA  = 24,  /**< 10 mA steps                         */
    LINK_SN_ESC_FLAGS       = 25,
    LINK_SN_AS5600_FLAGS    = 26,  /**< protocol 4.9, from here             */
    LINK_SN_AS5600_ANGLE    = 27,  /**< RAW ANGLE, 0 to 4095                */
    LINK_SN_AS5600_MAGNITUDE = 28, /**< 12 bits; 0 until read               */
    LINK_SN_AS5600_SAMPLES  = 29,  /**< angle reads, modulo 65536           */
    LINK_SN_AS5600_STILL_MS = 30,  /**< within 12 counts of its anchor, ms  */
    LINK_SN_RESETS          = 31,  /**< protocol 4.11; reads 0 before it    */
    LINK_SN_COUNT           = 32,
};
/** The registers a 4.8 coprocessor's page has. */
#define LINK_SN_COUNT_V48 26u
/** The set-up, ENABLE to register 11: what a write may change, and what
 *  flash keeps. */
#define LINK_SN_CONFIG_COUNT 12u

/* ENABLE's bits. */
#define LINK_SN_EN_I228   0x01u
#define LINK_SN_EN_I3221  0x02u
#define LINK_SN_EN_AS5600 0x04u     /**< protocol 4.9 */
/** Every bit ENABLE takes. */
#define LINK_SN_EN_ALL    (LINK_SN_EN_I228 | LINK_SN_EN_I3221 | LINK_SN_EN_AS5600)

/* The ranges a write is held to. */
/** The bus clock, the one KHZ takes: 400 kHz, the parts' fast mode. */
#define LINK_SN_KHZ_BUS          400u
#define LINK_SN_I228_ADDR_MIN    0x40u
#define LINK_SN_I228_ADDR_MAX    0x4Fu
#define LINK_SN_I228_UOHM_MIN      50u
#define LINK_SN_I228_UOHM_MAX   20000u
#define LINK_SN_I228_DA_MIN        10u
#define LINK_SN_I228_DA_MAX      3000u
#define LINK_SN_I3221_ADDR_MIN   0x40u
#define LINK_SN_I3221_ADDR_MAX   0x43u
#define LINK_SN_I3221_DMOHM_MIN    50u
#define LINK_SN_I3221_DMOHM_MAX 10000u
#define LINK_SN_I3221_CH_ALL     0x07u

/**
 * Whether @p sda and @p scl are one I2C block's pair: SDA a GPIO whose
 * number mod 4 is 0 or 2, SCL the GPIO after it.  The one rule for the
 * page, the bus that opens the pins, the panel's writes and the values
 * SETUP offers.  It does not say the pins are free, or that the board has
 * them.
 */
static inline bool link_sn_pins_pair(unsigned sda, unsigned scl)
{
    return (sda % 2u) == 0u && scl == sda + 1u;
}

/** What the coprocessor says about the bus and the two parts. */
typedef enum {
    LINK_SN_I228_ONLINE    = 1u << 0, /**< answering, identity good, in use */
    /** Its last identity read was an INA228's.  With ONLINE clear, it has
     *  stopped answering since. */
    LINK_SN_I228_ID_OK     = 1u << 1,
    /** Something answers at its address with another identity, and is not
     *  used. */
    LINK_SN_I228_ID_WRONG  = 1u << 2,
    /** A current read at the end of its range, in the last 50 ms window
     *  or since the run's arm: BENCH's current and power, or their peaks,
     *  are then bounds, not values. */
    LINK_SN_I228_CLIPPED   = 1u << 3,
    LINK_SN_I3221_ONLINE   = 1u << 4,
    LINK_SN_I3221_ID_OK    = 1u << 5,
    LINK_SN_I3221_ID_WRONG = 1u << 6,
    LINK_SN_BUS_OPEN       = 1u << 8, /**< the I2C block runs on its pins   */
    LINK_SN_BUS_STUCK      = 1u << 9, /**< SDA held low, being clocked free */
} link_sense_flag_t;

/* RESETS' two counts, each modulo 256. */
#define LINK_SN_RESETS_I228(r)  ((uint8_t)((r) & 0xFFu))
#define LINK_SN_RESETS_I3221(r) ((uint8_t)(((r) >> 8) & 0xFFu))

/** AS5600_FLAGS' bits (protocol 4.9). */
typedef enum {
    /** Answering at 0x36 with a STATUS an AS5600 can give. */
    LINK_SN_ENC_ONLINE = 1u << 0,
    /** STATUS MD: a magnet is detected.  Clear, AS5600_ANGLE is no
     *  position. */
    LINK_SN_ENC_MD     = 1u << 1,
    /** STATUS ML: the field is too weak. */
    LINK_SN_ENC_ML     = 1u << 2,
    /** STATUS MH: the field is too strong. */
    LINK_SN_ENC_MH     = 1u << 3,
    /** Something answers at 0x36 with a STATUS no AS5600 gives, ML and MH
     *  both set; not used. */
    LINK_SN_ENC_WRONG  = 1u << 4,
    /** AS5600_ANGLE and bits 1 to 3 hold a reading of this set-up. */
    LINK_SN_ENC_VALID  = 1u << 5,
} link_sense_enc_flag_t;

/* ESC_FLAGS' bits. */
#define LINK_SN_ESC_VOLTAGE_OK 0x01u
#define LINK_SN_ESC_CURRENT_OK 0x02u

/* --- the SERVO_SENSE page (protocol 4.7): the INA3221's three channels,
 *     and a move timed on the coprocessor's clock.
 *
 *     Registers 0 to 11, four a channel from CH1: the mean and the highest
 *     current in mA, signed, and the mean and the lowest bus voltage in mV,
 *     at the load side of the shunt, over the last 50 ms window.  WINDOW
 *     numbers the windows, modulo 65536; a read does not end one, so a
 *     reply lost on the link loses nothing.  CH_FLAGS bits 0..2 say a
 *     channel's window holds readings; bits 4..6 that one of them read the
 *     top of the range, 163.8 mV across the shunt, which makes that
 *     channel's mean and highest current lower bounds and not values; bit 7
 *     the same of the capture's move.
 *
 *     CAP_ARM to CAP_BAND_MA are a capture, one frame: CAP_ARM with bit 7
 *     set, the INA3221 channel in bits 0..1 (1: CH2 and CH3 are refused,
 *     LINK_SS_CAP_CH) and in bits 8..10 the
 *     output channel (0 to 7) whose next changed command starts the timing;
 *     the holding level the move ends at in mA, signed, -32768 to 32767
 *     (protocol 4.11; before it 0 to 32767, and 32768 to 65535 refused):
 *     a holding level near 0 A reads below zero by the part's offset; the
 *     distance from
 *     the level before the command that counts as movement, and the band
 *     around the holding level that counts as arrival, each 1 to 32767 mA.
 *     An arm is the whole frame from CAP_ARM; an arm restarts a capture
 *     already running.  CAP_ARM written 0 disarms and is never refused,
 *     alone or at the head of the frame, whose other registers are then not
 *     stored.  Refused with BAD_VALUE: any other write that is not the
 *     whole frame, CAP_ARM bits outside the three fields, a value out of
 *     range, an INA3221 channel other than CH1 or one SENSE does not read,
 *     and an output channel
 *     that is not a surface rendered by a PWM slot; with NOT_ARMED on a
 *     disarmed bank.  A bank that stops driving ends a capture that has not
 *     finished: CAP_ARM reads 0 and CAP_STATE idle.
 *
 *     CAP_STATE onwards are read only: the state (link_cap_state_t);
 *     captures finished, modulo 65536; the time from the PWM frame that
 *     carries the new pulse to the movement and to the arrival, in 0.1 ms,
 *     6553.5 ms at most, resolved to CH1's 1 ms sample interval; the
 *     highest and the mean filtered current of the move in mA, signed,
 *     over CAP_SAMPLES samples.  A capture ends arrived, at a stop, late
 *     (movement and no arrival within 3000 ms plus the meter's lag),
 *     unseen (no movement in that time) or lost (the INA3221 stopped
 *     answering, or no PWM edge came within 3000 ms of the arm).
 *
 *     Not kept: a coprocessor restart reads 0 throughout. */
enum {
    LINK_SS_CH_MEAN_MA  = 0,   /**< channel n's at LINK_SS_CH_STRIDE * (n-1) */
    LINK_SS_CH_MAX_MA   = 1,
    LINK_SS_CH_MEAN_MV  = 2,
    LINK_SS_CH_MIN_MV   = 3,
    LINK_SS_CH_STRIDE   = 4,
};
#define LINK_SS_CHANNELS 3u
enum {
    LINK_SS_WINDOW       = 12,
    LINK_SS_CH_FLAGS     = 13,
    LINK_SS_CAP_ARM      = 14,
    LINK_SS_CAP_HOLD_MA  = 15,
    LINK_SS_CAP_MOVE_MA  = 16,
    LINK_SS_CAP_BAND_MA  = 17,
    LINK_SS_CAP_STATE    = 18,  /**< read only from here */
    LINK_SS_CAP_SEQ      = 19,
    LINK_SS_CAP_MOVE_T   = 20,  /**< 0.1 ms              */
    LINK_SS_CAP_ARRIVE_T = 21,  /**< 0.1 ms              */
    LINK_SS_CAP_PEAK_MA  = 22,
    LINK_SS_CAP_MEAN_MA  = 23,
    LINK_SS_CAP_SAMPLES  = 24,
    LINK_SS_COUNT        = 25,
};
/** The registers a capture is armed with, from LINK_SS_CAP_ARM. */
#define LINK_SS_CAP_FRAME 4u

/* CH_FLAGS' bits, for INA3221 channel 1 to 3. */
#define LINK_SS_CH_VALID(ch)   ((uint16_t)(1u << ((unsigned)(ch) - 1u)))
#define LINK_SS_CH_CLIPPED(ch) ((uint16_t)(1u << ((unsigned)(ch) + 3u)))
#define LINK_SS_CAP_CLIPPED    0x80u

/* CAP_ARM: bit 7, the INA3221 channel (1 to 3) and the output channel
 * (0 to 7).  With bit 11 (4.13) the number in bits 8 to 10 is a programming
 * port of the PROG page, 0 to 3. */
#define LINK_SS_ARM            0x80u
#define LINK_SS_ARM_OF(ch, out) \
    ((uint16_t)(LINK_SS_ARM | ((unsigned)(ch) & 0x03u) \
                | (((unsigned)(out) & 0x07u) << 8)))
#define LINK_SS_ARM_CH(r)      ((uint8_t)((r) & 0x03u))
#define LINK_SS_ARM_OUT(r)     ((uint8_t)(((r) >> 8) & 0x07u))
/** Every bit CAP_ARM may carry. */
#define LINK_SS_ARM_BITS       0x0F83u
/** Bits 8 to 10 name a programming port, not an output channel. */
#define LINK_SS_ARM_PORT       0x0800u
/** The one INA3221 channel a capture is taken on; CH2 and CH3 are refused
 *  with BAD_VALUE, the field kept for a channel read fast enough later. */
#define LINK_SS_CAP_CH         1u

typedef enum {
    LINK_CAP_IDLE      = 0,
    LINK_CAP_ARMED     = 1, /**< waiting for the command to change         */
    LINK_CAP_WAIT_MOVE = 2, /**< the new pulse is out; no movement yet     */
    LINK_CAP_MOVING    = 3,
    LINK_CAP_ARRIVED   = 4, /**< back within the band of the holding level */
    LINK_CAP_AT_STOP   = 5, /**< settled, pushing on an end stop           */
    LINK_CAP_LATE      = 6, /**< movement, and no arrival within 3000 ms
                                 plus the meter's lag                      */
    LINK_CAP_UNSEEN    = 7, /**< no movement within 3000 ms plus the
                                 meter's lag: neither timed nor late       */
    LINK_CAP_LOST      = 8, /**< the INA3221 stopped answering, or no PWM
                                 edge came within 3000 ms of the arm       */
} link_cap_state_t;

/* --- the SERVO_WIN page (protocol 4.11): the INA3221's CH1, the servo
 *     under test, as its last four 50 ms windows.  Read only: every write
 *     is refused with READ_ONLY.
 *
 *     SERVO_SENSE shows the last window alone, so a host whose reads lie
 *     further apart than 50 ms misses windows.  Here a host takes each
 *     window number once, and misses none while two of its reads lie at
 *     most 200 ms apart.
 *
 *     WINDOW is the number of the newest complete window, modulo 65536,
 *     the number SERVO_SENSE's WINDOW shows.  FLAGS bit 0 (LINK_SW_HAVE)
 *     says a window has closed under the set-up in force, so WINDOW is a
 *     number; bit 7 (LINK_SW_CAP_CLIPPED) is SERVO_SENSE's capture bit.
 *     CAP_STATE and CAP_SEQ read as SERVO_SENSE's registers 18 and 19, so
 *     one read shows the windows and whether a capture has ended.
 *
 *     Then four entries of six registers, newest first: entry k, from
 *     register 4 + 6 k, is window number WINDOW - k.  Mean, highest and
 *     lowest current in mA, signed; mean and lowest bus voltage in mV, at
 *     the load side of the shunt; and the entry's flags.  The schedule
 *     reads CH1's current every 1 ms and its bus voltage every 20 ms: a
 *     window holds up to 50 current samples and 2 or 3 voltage samples.
 *
 *     An entry's flags: bit 15 (LINK_SW_E_CLOSED) a window closed with
 *     this number -- clear for a number the coprocessor skipped because
 *     its tick ran late by more than a window, and for a number before
 *     the first window, and the entry then reads 0 throughout; bit 8
 *     (LINK_SW_E_CURRENT) the window holds current samples; bit 9
 *     (LINK_SW_E_VOLTAGE) it holds voltage samples; bit 10
 *     (LINK_SW_E_CLIP_HI) a current sample read the top of the range,
 *     163.8 mV across the shunt; bit 11 (LINK_SW_E_CLIP_LO) one read the
 *     bottom; bits 0..7 the number of samples that did, held at 255.
 *
 *     A clipped sample counts in an entry as the reading it is: the end
 *     of the range, 1.638 A or -1.6384 A on the 0.1 Ohm shunt, in the
 *     mean, the highest and the lowest.  SERVO_SENSE's registers 0 to 11
 *     leave a clipped sample out of their figures, so the two pages
 *     differ for a window with a clipped sample, and only for such a
 *     window.
 *
 *     A window being filled when the INA3221 is found reset (SENSE's
 *     RESETS) is emptied: its entry is closed and holds no samples.
 *
 *     Not kept, and cleared by a SENSE set-up taken. */
enum {
    LINK_SW_WINDOW    = 0,
    LINK_SW_FLAGS     = 1,
    LINK_SW_CAP_STATE = 2,   /**< link_cap_state_t                       */
    LINK_SW_CAP_SEQ   = 3,
    LINK_SW_ENTRIES   = 4,   /**< entry k's registers start at 4 + 6 k   */
    LINK_SW_COUNT     = 28,
};
/** The windows the page carries. */
#define LINK_SW_RING 4u
/* One entry's registers. */
enum {
    LINK_SW_E_MEAN_MA = 0,   /**< signed -- read as int16_t              */
    LINK_SW_E_MAX_MA  = 1,   /**< signed                                 */
    LINK_SW_E_MIN_MA  = 2,   /**< signed                                 */
    LINK_SW_E_MEAN_MV = 3,
    LINK_SW_E_MIN_MV  = 4,
    LINK_SW_E_FLAGS   = 5,
    LINK_SW_E_STRIDE  = 6,
};
/** Register @p r of the entry @p k windows before the newest. */
#define LINK_SW_ENTRY(k, r) \
    ((unsigned)LINK_SW_ENTRIES + (unsigned)(k) * LINK_SW_E_STRIDE + (unsigned)(r))

/* FLAGS' bits. */
#define LINK_SW_HAVE        0x01u
#define LINK_SW_CAP_CLIPPED 0x80u

/* An entry's flags. */
#define LINK_SW_E_CLIPPED(f) ((uint8_t)((f) & 0xFFu))
#define LINK_SW_E_CLIPPED_MAX 255u
#define LINK_SW_E_CURRENT   0x0100u
#define LINK_SW_E_VOLTAGE   0x0200u
#define LINK_SW_E_CLIP_HI   0x0400u
#define LINK_SW_E_CLIP_LO   0x0800u
#define LINK_SW_E_CLOSED    0x8000u

/* --- the TONE page (protocol 4.8): the beeps of an ESC (electronic speed
 *     controller), heard on one motor phase.  One GPIO reads the phase
 *     through a series resistor and a zener clamp; a PIO (programmable
 *     input/output) state machine stamps its edges at 26.7 ns, and
 *     protocols/phase_tap/tone.c turns them into beeps and their pitch.
 *
 *     ENABLE to F_MAX_HZ are one frame and SPLIT_PCT to register 7 the
 *     next.  ENABLE bit 0 runs the tap.  PIN is the GPIO.  F_MIN_HZ and
 *     F_MAX_HZ bound the tones heard: 50 to 2000 Hz, and F_MIN_HZ + 1 to
 *     6900 Hz (EVT_FREQ_DHZ holds 0.1 Hz in 16 bits, to 6553.5 Hz).
 *     SPLIT_PCT is how far a pitch moves, in percent, to start a new beep
 *     without a silence: 0 splits on silence only, 50 at most.  GAP_MS is
 *     the silence that ends a beep, 1 to 100 ms and at least the period of
 *     F_MIN_HZ.  MIN_PERIODS is how many tone periods make a beep, 1 to
 *     64.  Register 7 reads 0 and takes only 0.
 *
 *     Refused with BAD_VALUE: a value out of its range, a combination the
 *     detector refuses (tone_init()), and, while the tap is enabled, a pin
 *     past the bank, one the board reserves, or an ADC (analog to digital
 *     converter) pin: GP26 to GP29 on the RP2350A and GP40 to GP47 on the
 *     RP2354B, rated IOVDD + 0.5 V and not fault tolerant, and any pin on
 *     a coprocessor whose core 1 did not start at boot: no capture runs
 *     before a restart.  Refused with
 *     BUSY (since 4.17, BAD_VALUE before): while the tap is enabled, a pin
 *     bound to an output or held by the SENSE, SUPPLY or PROG page, and a
 *     capture the wiring cannot start for now -- core 1 in a pass, or no
 *     PIO state machine or DMA channel left.  Freed, the same write is
 *     taken.  The pin is no output's while the tap
 *     is enabled, and an OUTPUTS write that binds it is refused, as are a
 *     SENSE or SUPPLY write that takes it.  A change is taken armed or
 *     not: the tap is an input and drives nothing.
 *
 *     FLAGS onwards are read only: FLAGS (link_tone_flag_t); WINDOW, the
 *     detector's 8 ms windows counted modulo 65536; WIN_FREQ_DHZ, the last
 *     window's tone in 0.1 Hz, 0 with no tone; WIN_PERIODS, the periods in
 *     it.
 *
 *     The coprocessor keeps the last LINK_TN_RING beeps and numbers them
 *     from 1 to 65535, then from 1 again.  BEEP_HEAD is the newest number,
 *     0 before the first beep.  EVT_SEL names the beep that EVT_SEQ to
 *     EVT_FLAGS show.  It is the one writable register after register 7,
 *     it is not kept, and a read does not consume a beep, so a reply lost
 *     on the link loses nothing.  EVT_SEQ equals EVT_SEL while that beep is
 *     in the ring; when it has left the ring, or has not happened yet,
 *     EVT_SEQ and registers 15 to 21 read 0.  EVT_START_MS is the beep's
 *     first rise in ms since the capture started (ENABLE written 1, or PIN
 *     changed while enabled), 32 bit, low register first (49.7 days);
 *     EVT_LEN_DMS its length to its last edge in 0.1 ms, to 6553.5 ms;
 *     EVT_FREQ_DHZ its mean pitch in 0.1 Hz; EVT_BURSTS its bursts, to
 *     65535; EVT_CARRIER_HHZ the carrier it was chopped at in 100 Hz
 *     steps, 0 unchopped.  A length, pitch, burst count or carrier past its
 *     register reads as the largest it holds.  EVT_FLAGS bit 0: the beep
 *     began at a pitch change with no silence before it; bit 1: it ended
 *     at one.
 *     LOST counts beeps dropped, modulo 65536: the detector's queue full,
 *     or the hand-over between the cores full.  GLITCHES counts lows the
 *     detector ignored as shorter than 500 ns, modulo 65536.  The
 *     capture's 8 us hold-off removes every low shorter than 8 us before
 *     the detector sees it, so the register reads 0 on the tap.
 *
 *     The capture starts when the tap is enabled or its pin changes, and
 *     then empties the ring; the numbers go on.  Registers 0 to 6 are kept
 *     in the coprocessor's flash beside the SENSE set-up, and the tap starts
 *     at boot.  Nothing else is kept.
 *
 *     While the tap is disabled the pin is an input with its pull-down on,
 *     so a wire connected to nothing reads low.  The panel's read at
 *     20 Hz is registers 8 to 23: a request and 4 data frames. */
enum {
    LINK_TN_ENABLE       = 0,
    LINK_TN_PIN          = 1,
    LINK_TN_F_MIN_HZ     = 2,
    LINK_TN_F_MAX_HZ     = 3,
    LINK_TN_SPLIT_PCT    = 4,
    LINK_TN_GAP_MS       = 5,
    LINK_TN_MIN_PERIODS  = 6,
    LINK_TN_RESERVED_7   = 7,   /**< reads 0; written 0                  */
    LINK_TN_FLAGS        = 8,   /**< read only from here                 */
    LINK_TN_WINDOW       = 9,
    LINK_TN_WIN_FREQ_DHZ = 10,
    LINK_TN_WIN_PERIODS  = 11,
    LINK_TN_BEEP_HEAD    = 12,
    LINK_TN_EVT_SEL      = 13,  /**< writable, not kept                  */
    LINK_TN_EVT_SEQ      = 14,
    LINK_TN_EVT_START_LO = 15,  /**< ms since ENABLE, uint32_t, low first*/
    LINK_TN_EVT_START_HI = 16,
    LINK_TN_EVT_LEN_DMS  = 17,  /**< 0.1 ms                              */
    LINK_TN_EVT_FREQ_DHZ = 18,  /**< 0.1 Hz                              */
    LINK_TN_EVT_BURSTS   = 19,
    LINK_TN_EVT_CARRIER_HHZ = 20, /**< 100 Hz steps; 0 unchopped         */
    LINK_TN_EVT_FLAGS    = 21,
    LINK_TN_LOST         = 22,
    LINK_TN_GLITCHES     = 23,
    LINK_TN_COUNT        = 24,
};
/** The set-up, ENABLE to register 6: what a write may change, and what
 *  flash keeps. */
#define LINK_TN_CONFIG_COUNT 7u

/* ENABLE's bit. */
#define LINK_TN_EN_TAP   0x01u

/* The ranges a write is held to. */
#define LINK_TN_F_MIN_LO       50u
#define LINK_TN_F_MIN_HI     2000u
#define LINK_TN_F_MAX_HI     6900u
#define LINK_TN_SPLIT_MAX      50u
#define LINK_TN_GAP_MS_MIN      1u
#define LINK_TN_GAP_MS_MAX    100u
#define LINK_TN_PERIODS_MIN     1u
#define LINK_TN_PERIODS_MAX    64u
/** Beeps the coprocessor keeps. */
#define LINK_TN_RING           64u

/** The set-up before anything is written: the tap off, on GP22 (pad 29);
 *  400 to 6500 Hz, 8 % split, 3 ms gap, 3 periods a beep. */
#define LINK_TN_DEFAULT_PIN        22u
#define LINK_TN_DEFAULT_F_MIN     400u
#define LINK_TN_DEFAULT_F_MAX    6500u
#define LINK_TN_DEFAULT_SPLIT       8u
#define LINK_TN_DEFAULT_GAP_MS      3u
#define LINK_TN_DEFAULT_PERIODS     3u

/** What the coprocessor says about the tap. */
typedef enum {
    /** The PIO state machine and its DMA ring run on the pin. */
    LINK_TN_RUNNING     = 1u << 0,
    /** The tap is enabled and does not run: the pin is not free (a kept
     *  set-up met a binding or a reservation at boot) or no PIO state
     *  machine could take it. */
    LINK_TN_PIN_REFUSED = 1u << 1,
    /** The capture ring or the state machine's FIFO was overrun since the
     *  capture started; the beep under way was cut at its last edge.
     *  Cleared when the capture starts again. */
    LINK_TN_OVERRUN     = 1u << 2,
    /** A run of bursts is under way. */
    LINK_TN_BEEP        = 1u << 3,
    /** The last window held a tone. */
    LINK_TN_TONE        = 1u << 4,
} link_tone_flag_t;

/** EVT_FLAGS' bits, tone_beep_flag_t's. */
#define LINK_TN_EVT_AFTER_CHANGE  0x01u
#define LINK_TN_EVT_BEFORE_CHANGE 0x02u

#define LINK_OS_RANGE_OF(first, count) \
    ((uint16_t)((((unsigned)(first) & 0xFFu) << 8) | ((unsigned)(count) & 0xFFu)))
#define LINK_OS_FIRST(range) ((uint8_t)((range) >> 8))
#define LINK_OS_CHANNELS(range) ((uint8_t)((range) & 0xFFu))

/*
 * The drivers, numbered on the wire.  A contract like a page number: nothing
 * is renumbered, and a new protocol appends.
 */
typedef enum {
    LINK_DRIVER_NONE        = 0,
    LINK_DRIVER_PWM         = 1,
    LINK_DRIVER_PPM         = 2,
    LINK_DRIVER_DSHOT       = 3,
    /*
     * Bidirectional DShot is its own driver rather than a flag on the one
     * above.  It inverts the line and the checksum, so an ESC (electronic
     * speed controller) set up for one protocol ignores the other entirely;
     * a slot that could be switched between them by a flag would look like
     * one output with a setting instead of two incompatible wires.
     */
    LINK_DRIVER_DSHOT_BIDIR = 4,
} link_out_driver_t;

/* -------------------------------------------------------------------- bind */

/*
 * A binding taken whole (protocol 4.10).
 *
 * A binding is two pages of 32 registers, 16 frames, and a write to CHAN_CFG
 * or OUTPUTS is in force frame by frame: a sequence that stops part way
 * leaves the first frames of one binding and the rest of another.  BIND_CFG
 * and BIND_OUT hold a CHAN_CFG and an OUTPUTS page that are not in force.
 * They take the registers and the windows of the pages they prepare, check
 * each frame by those pages' value rules, and change no output.
 *
 * BIND has one register.  A write of LINK_BD_COMMIT carries the CRC (cyclic
 * redundancy check, out_stage_crc()) of the 64 prepared registers.  The
 * coprocessor compares it with the CRC of what it holds prepared and refuses
 * a difference with BAD_VALUE; on a match it judges the two pages by every
 * rule a CHAN_CFG and an OUTPUTS write is judged by, CHAN_CFG first, and
 * puts both in force or neither.  The register reads the CRC of what is
 * prepared.
 *
 * What is prepared starts as the pages in force at boot and is not kept in
 * flash.  CHAN_CFG and OUTPUTS stay writable, one entry a frame.
 */
enum {
    LINK_BD_COMMIT = 0, /**< write: the prepared pages' CRC; read: the same */
    LINK_BD_COUNT  = 1,
};

/* ---------------------------------------------------------------- identity */
enum {
    LINK_ID_PROTOCOL_MAJOR = 0,
    LINK_ID_PROTOCOL_MINOR = 1,
    LINK_ID_FIRMWARE_MAJOR = 2,
    LINK_ID_FIRMWARE_MINOR = 3,
    LINK_ID_FIRMWARE_PATCH = 4,
    LINK_ID_HARDWARE       = 5, /**< board revision                        */
    LINK_ID_CAPABILITIES   = 6, /**< bitmap: which optional pages are real */
    LINK_ID_COUNT          = 7,
};

/*
 * What the coprocessor has fitted, as opposed to what the panel has screens
 * for.  The menu marks derive from this bitmap, so a mark disappears when the
 * part is fitted.
 *
 * Absent is not forbidden.  A screen whose capability is missing opens and
 * runs from the model, marked MODELLED.
 */
typedef enum {
    LINK_CAP_ESC_DRIVE   = 1u << 0, /**< a signal line out to an ESC       */
    LINK_CAP_ESC_TELEM   = 1u << 1, /**< telemetry back from one           */
    LINK_CAP_SERVO_PWM   = 1u << 2, /**< pulses out to a servo             */
    LINK_CAP_SERVO_SENSE = 1u << 3, /**< current, per output               */
    LINK_CAP_PACK_SENSE  = 1u << 4, /**< pack volts and amps               */
    LINK_CAP_RECEIVER    = 1u << 5, /**< a receiver bus decoded in PIO     */
    LINK_CAP_VIBRATION   = 1u << 6, /**< accelerometer and an index pulse  */
    LINK_CAP_CELLS       = 1u << 7, /**< a cell monitor on the balance lead*/
    LINK_CAP_PROGRAM     = 1u << 8, /**< one-wire and text-CLI programming */
} link_cap_t;

/* ------------------------------------------------------------------ status */
enum {
    LINK_ST_STATE      = 0, /**< a link_dev_state_t                      */
    LINK_ST_FAULTS     = 1, /**< a bitmap of link_fault_t                */
    LINK_ST_UPTIME_MS_LO = 2,
    LINK_ST_UPTIME_MS_HI = 3,
    LINK_ST_FRAMES_LO  = 4, /**< frames accepted, for a link that is only */
    LINK_ST_FRAMES_HI  = 5, /**<   sometimes wrong                        */
    LINK_ST_CRC_ERRORS = 6,
    LINK_ST_RESYNCS    = 7,
    /**
     * Protocol 4.21: CAN frames received and refused since power-up, low
     * half first, modulo 2^32.  A frame is refused when its receive buffer
     * holds no frame the link carries (an 11-bit identifier, a remote frame,
     * a DLC above 8: mcp2515_decode_rx()) or when link_can_decode() refuses
     * it (an unknown op, a count past a page, a DLC that disagrees with the
     * count).  Self-test frames are answered, not refused.  0 at power-up
     * and at every restart; nothing clears it over the link.
     */
    LINK_ST_REFUSED_LO = 8,
    LINK_ST_REFUSED_HI = 9,
    LINK_ST_COUNT      = 10,
};
/** The registers a 4.20 or older coprocessor's STATUS page has. */
#define LINK_ST_COUNT_V420 8u

/**
 * What the coprocessor did on its own authority.  Sticky until read and
 * explicitly cleared: a fault that lasted 4 ms is still the reason the motor
 * stopped.
 */
typedef enum {
    LINK_FAULT_NONE          = 0,
    LINK_FAULT_LINK_SILENT   = 1u << 0,
    LINK_FAULT_OVERCURRENT   = 1u << 1,
    LINK_FAULT_OVERTEMP      = 1u << 2,
    LINK_FAULT_STALL         = 1u << 3,
    LINK_FAULT_HEARTBEAT     = 1u << 4, /**< the safety line stopped edging */
    LINK_FAULT_VERSION       = 1u << 5, /**< the two ends disagree          */
    /**
     * The coprocessor's flash store is off for this boot (protocol 4.7):
     * its second core did not register for the flash lock-out, so no
     * erase or program may run.  Set-ups written over the link are taken
     * and run from RAM, and are gone at the next restart.  Set while it
     * lasts, which is until a restart.
     */
    LINK_FAULT_STORE_OFF     = 1u << 6,
} link_fault_t;

typedef enum {
    LINK_STATE_IDLE     = 0, /**< alive, outputs off            */
    LINK_STATE_ARMED    = 1, /**< outputs live                  */
    LINK_STATE_FAILSAFE = 2, /**< acted on its own authority    */
} link_dev_state_t;

/* ------------------------------------------------------------------- bench */
/*
 * The numbers, read-only.
 *
 * Fixed-point rather than floats: a register is 16 bits, and the scale is
 * part of the contract between the two firmwares.
 *
 * The peaks are tracked here rather than on the panel: the coprocessor has
 * the fast samples and the panel sees one poll in fifty of them.
 */
enum {
    LINK_BN_VOLTAGE_CV   = 0,  /**< 10 mV steps, 0..655.35 V              */
    LINK_BN_CURRENT_CA   = 1,  /**< 10 mA steps, 0..655.35 A              */
    LINK_BN_POWER_W      = 2,  /**< watts                                  */
    LINK_BN_RPM          = 3,
    LINK_BN_TEMP_ESC_DC  = 4,  /**< 0.1 C, signed -- read as int16_t       */
    LINK_BN_TEMP_MOT_DC  = 5,  /**< 0.1 C, signed                          */
    LINK_BN_CHARGE_MAH   = 6,  /**< accumulated by the current monitor     */
    LINK_BN_ENERGY_DWH   = 7,  /**< 0.1 Wh                                 */
    LINK_BN_VOLT_MIN_CV  = 8,  /**< sag: the lowest the bus went           */
    LINK_BN_CURR_MAX_CA  = 9,
    LINK_BN_POWER_MAX_W  = 10,
    LINK_BN_RPM_MAX      = 11,
    LINK_BN_FLAGS        = 12, /**< a bitmap of link_bench_flag_t          */
    LINK_BN_COUNT        = 13,
};

/** What the coprocessor says about the numbers it is sending. */
typedef enum {
    LINK_BN_VOLTAGE_OK = 1u << 0, /**< a sensor answered; else the field is 0 */
    LINK_BN_CURRENT_OK = 1u << 1,
    LINK_BN_RPM_OK     = 1u << 2,
    /**
     * The ESC's own temperature.
     *
     * Its meaning changed at protocol 3.0: before that it validated both
     * temperature fields, which is why the change is a major and not an
     * added bit.  A coprocessor that sets it and leaves temp_motor at zero
     * would have told a 2.x panel that a motor it cannot measure is at 0 C.
     */
    LINK_BN_TEMP_OK    = 1u << 3,
    /**
     * The motor's temperature, which is a different sensor.
     *
     * Separate because the two arrive from different places and one of them
     * usually does not arrive at all: an ESC reports its own temperature over
     * extended DShot telemetry and knows nothing about the motor it is
     * driving.  One flag for both would show a motor at 0 C whenever an ESC
     * reported its own.
     */
    LINK_BN_TEMP_MOT_OK = 1u << 4,
    /**
     * Voltage, current and power are the INA228's (protocol 4.7), not the
     * ESC's telemetry, which the SENSE page then carries.
     */
    LINK_BN_SENSED     = 1u << 5,
    /**
     * CHARGE_MAH and ENERGY_DWH are the INA228's accumulators, cleared at
     * this run's arm, with the part answering throughout (protocol 4.7).
     * Clear, the two registers count nothing the panel can use, and it
     * counts its own from the current.
     */
    LINK_BN_TOTALS_OK  = 1u << 6,
    /**
     * The numbers are modelled, not measured.  Set by a coprocessor running
     * without a front end and by the panel's own simulator; the panel draws
     * SIMULATION across the screen either way.
     */
    LINK_BN_SIMULATED  = 1u << 7,
} link_bench_flag_t;

/* --------------------------------------------------------------- catalogue */

/*
 * The board's own pins: which GPIOs it brings out, the pad number printed
 * beside each, and what already holds the ones an output may not have.
 *
 * A board describes itself so a panel that has never heard of it can still
 * offer the right pins.  What it cannot do is make a pin safe: the
 * coprocessor reserves its own set at its own end whatever it says here, so
 * a page that lies costs a pin rather than the safety line.
 *
 * One register per pin and no count register.  A count would have to live in
 * the identity page, and lengthening that page breaks every coprocessor
 * built before it: the panel asks for LINK_ID_COUNT registers, and a device
 * whose identity page is shorter refuses the read rather than returning what
 * it has -- which would leave the link down instead of degraded.  Instead a
 * pad number of zero means there is no pin in that slot, since pads are
 * numbered from one.
 *
 * A coprocessor built before this page answers NACK with LINK_NACK_BAD_PAGE,
 * which is the panel's cue to use the catalogue compiled into it.
 */
#define LINK_CAT_PINS  32u
#define LINK_CAT_COUNT LINK_CAT_PINS

/** gpio in 6 bits, pad in 6, and what holds it in 4. */
#define LINK_CAT_OF(gpio, pad, hold) \
    ((uint16_t)((((unsigned)(gpio) & 0x3Fu) << 10) \
                | (((unsigned)(pad) & 0x3Fu) << 4) \
                | ((unsigned)(hold) & 0x0Fu)))
#define LINK_CAT_GPIO(r) ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_CAT_PAD(r)  ((uint8_t)(((r) >> 4) & 0x3Fu))
#define LINK_CAT_HOLD(r) ((uint8_t)((r) & 0x0Fu))

/**
 * What holds a pin the bench may not drive.
 *
 * A code rather than a name: a name is a string and a register is sixteen
 * bits.  The panel prints its own words for these, so a board it knows shows
 * the exact signal -- "CAN CS" rather than "CAN" -- and a board it has only
 * been told about shows the group.
 */
typedef enum {
    LINK_PIN_FREE      = 0,  /**< an output may have it                    */
    LINK_PIN_HEARTBEAT = 1,  /**< the safety line                          */
    LINK_PIN_CAN       = 2,  /**< the link to the panel                    */
    LINK_PIN_FLASH     = 3,
    LINK_PIN_DEBUG     = 4,
    LINK_PIN_SENSOR    = 5,
    LINK_PIN_OTHER     = 15, /**< spoken for, and this build has no word   */
} link_pin_hold_t;

/* ------------------------------------------------------------------- shape */

/*
 * Where the pins are, as opposed to which they are.
 *
 * The catalogue page names a pad and the pin on it; it does not say where
 * that pad sits.  A picture of the board is the one place a pin number has
 * to become a position, and a picture drawn from a guessed shape points at
 * the wrong pad with the same confidence as the right one -- so the board
 * says its shape rather than the panel assuming one.
 *
 * A coprocessor that does not serve this page gets no drawn board.  That is
 * a screen the operator does not get, not a bench that does not work: the
 * outputs grid offers the same pins from the catalogue alone.
 *
 * Two rows on one pitch, which is every 0.1-inch header board.  Pad 1 sits
 * at LINK_SH_CORNER and the numbers run away from it along that edge, then
 * back along the opposite one -- the DIP (dual in-line package) convention,
 * and the one the Pico form factor follows.
 *
 * Hundredths of a millimetre throughout: a register is sixteen bits, which
 * reaches 655.35 mm, and 0.01 mm is finer than any board is placed to.
 */
enum {
    LINK_SH_WIDTH_CMM  = 0, /**< outline, along the pad rows              */
    LINK_SH_HEIGHT_CMM = 1, /**< outline, across them                     */
    LINK_SH_LAYOUT     = 2, /**< (corner << 8) | pads in one row          */
    LINK_SH_PITCH_CMM  = 3, /**< centre to centre, 254 for 0.1 inch       */
    /**
     * Edge to the centre of the pad row, from the outline's own edge.  A
     * board drawing (LINK_ART_DRAWING) places its pads on the same grid.
     */
    LINK_SH_INSET_CMM  = 4,
    LINK_SH_COUNT      = 5,
};

#define LINK_SH_LAYOUT_OF(corner, per_side) \
    ((uint16_t)((((unsigned)(corner) & 0xFFu) << 8) \
                | ((unsigned)(per_side) & 0xFFu)))
#define LINK_SH_CORNER(r)   ((uint8_t)(((r) >> 8) & 0xFFu))
#define LINK_SH_PER_SIDE(r) ((uint8_t)((r) & 0xFFu))

/** Which corner of the outline pad 1 sits at, seen with the board as drawn. */
typedef enum {
    LINK_SH_BOTTOM_LEFT  = 0,
    LINK_SH_BOTTOM_RIGHT = 1,
    LINK_SH_TOP_LEFT     = 2,
    LINK_SH_TOP_RIGHT    = 3,
} link_shape_corner_t;

/* ----------------------------------------------------------------- artwork */

/*
 * The board's drawing: its outline, its parts as flat shapes, and every pad
 * with what it is, so the picker shows the board in the operator's hands and
 * not only its outline.  The coprocessor carries the drawing of its own
 * board; a new board brings its drawing in its own firmware image, and the
 * panel needs no change for it.
 *
 * Two pages because a page is thirty-two registers and no more.  Metadata
 * here, payload next door, and a block stays sixty-two bytes.  The drawing
 * of the RP2350-CAN is under 2 kB, at most 33 blocks: at 1 Mbit/s a block
 * is a write to say which one and a read to fetch it, about 1.4 ms the pair.
 *
 * LINK_AW_BLOCKS of zero means this coprocessor carries no drawing of
 * itself.  That is the ordinary answer, not a fault: the board is then drawn
 * from its shape page, and used from its catalogue when there is no shape.
 *
 * The panel keeps a drawing in its flash under the coprocessor's board ID
 * together with every register of this page from LINK_AW_WIDTH on, and
 * shows the kept one only while all of them still match: a coprocessor
 * flashed with another drawing for the same board ID fetches the new one.
 */
enum {
    LINK_AW_BLOCKS   = 0, /**< blocks of payload; 0 when there is none    */
    LINK_AW_WIDTH    = 1, /**< the outline along the pad rows, 0.01 mm    */
    LINK_AW_HEIGHT   = 2, /**< the outline across them, 0.01 mm           */
    LINK_AW_FORMAT   = 3, /**< a link_art_format_t                        */
    LINK_AW_BYTES_LO = 4, /**< payload length, low half                   */
    LINK_AW_BYTES_HI = 5,
    /**
     * link_crc() over the whole payload, seeded zero.  A block lost and
     * silently skipped would otherwise be cached in flash and shown until
     * somebody wired a lead by it.
     */
    LINK_AW_CRC      = 6,
    /**
     * link_fnv1a32() over the whole payload, low half then high half.  The
     * content key of the panel's cache, next to the CRC: two drawings that
     * share a sixteen-bit CRC and a length differ here.
     */
    LINK_AW_HASH_LO  = 7,
    LINK_AW_HASH_HI  = 8,
    LINK_AW_COUNT    = 9,
};

typedef enum {
    LINK_ART_NONE    = 0,
    /** A photograph, two bytes a pixel.  Minor 19 and older serve it; this
     *  panel does not read it. */
    LINK_ART_RGB565  = 1,
    /** The board drawing laid out below (LINK_DRAW_*). */
    LINK_ART_DRAWING = 2,
} link_art_format_t;

/*
 * The board drawing, LINK_ART_DRAWING.  Little-endian, 0.01 mm throughout,
 * origin the top-left corner of the outline seen from the component side,
 * x along the pad rows, y across them.  Every pad ring and every shape lies
 * inside the outline; a payload with anything else is refused whole.
 *
 * Header, LINK_DRAW_HEAD_BYTES:
 *   0  u8   LINK_DRAW_VERSION
 *   1  u8   pads, 1 to LINK_DRAW_MAX_PADS
 *   2  u16  shapes, 0 to LINK_DRAW_MAX_SHAPES
 *   4  u16  outline width, the same as LINK_AW_WIDTH
 *   6  u16  outline height, the same as LINK_AW_HEIGHT
 *   8  u16  board colour, RGB565
 *   10 u16  pad ring colour, RGB565
 *   12 u8   corner radius of the outline
 *   13 u8   ring radius
 *   14 u8   hole radius, below the ring radius
 *   15 u8   0
 * Then the pads, LINK_DRAW_PAD_BYTES each, in ascending pad order:
 *   0  u8   pad, 1 to 63
 *   1  u8   kind: LINK_PAD_GROUND, LINK_PAD_POWER, LINK_PAD_OTHER or
 *           LINK_DRAW_PAD_GPIO
 *   2  u8   the GPIO of a LINK_DRAW_PAD_GPIO pad, the rail of a POWER pad in
 *           tenths of a volt (0: not a fixed voltage), else 0
 *   3  u8   flags: LINK_DRAW_PAD_SQUARE
 *   4  u16  x of the pad centre
 *   6  u16  y of the pad centre
 * Then the shapes, LINK_DRAW_SHAPE_BYTES each, drawn in order over the board
 * colour and under the rings:
 *   0  u8   kind: a link_draw_shape_kind_t
 *   1  u8   corner radius of an RRECT, width of a LINE, else 0
 *   2  u16  colour, RGB565
 *   4  u16  a   RECT, RRECT: left x;  DISC: centre x;  LINE: x0
 *   6  u16  b   RECT, RRECT: top y;   DISC: centre y;  LINE: y0
 *   8  u16  c   RECT, RRECT: width;   DISC: radius;    LINE: x1
 *   10 u16  d   RECT, RRECT: height;  DISC: 0;         LINE: y1
 * The length is exactly the header, the pads and the shapes.
 */
#define LINK_DRAW_VERSION      1u
#define LINK_DRAW_HEAD_BYTES   16u
#define LINK_DRAW_PAD_BYTES    8u
#define LINK_DRAW_SHAPE_BYTES  12u
#define LINK_DRAW_MAX_PADS     64u
#define LINK_DRAW_MAX_SHAPES   400u
#define LINK_DRAW_MAX_BYTES    (LINK_DRAW_HEAD_BYTES \
                                + LINK_DRAW_MAX_PADS * LINK_DRAW_PAD_BYTES \
                                + LINK_DRAW_MAX_SHAPES * LINK_DRAW_SHAPE_BYTES)
/** A pad that is a GPIO, the fourth kind next to link_pad_kind_t's three. */
#define LINK_DRAW_PAD_GPIO     4u
/** The pad's ring is square: pad 1 of a header, by convention. */
#define LINK_DRAW_PAD_SQUARE   0x01u

typedef enum {
    LINK_DRAW_RECT  = 1,
    LINK_DRAW_RRECT = 2,
    LINK_DRAW_DISC  = 3,
    LINK_DRAW_LINE  = 4,
} link_draw_shape_kind_t;

/*
 * The payload, a block at a time: write LINK_AD_BLOCK to say which, then
 * read the page.  Two transactions rather than one because a block that
 * advanced on being read would resend nothing after a reply went missing,
 * and the host would assemble a drawing with a hole in it.
 *
 * Bytes little-endian within a register, the low byte first.  A final block
 * carrying an odd count leaves the high byte of its last register unused.
 */
enum {
    LINK_AD_BLOCK = 0,            /**< read and write: which block follows */
    LINK_AD_DATA  = 1,            /**< the payload starts here             */
    LINK_AD_COUNT = LINK_MAX_REGS,
};
#define LINK_AD_WORDS ((unsigned)(LINK_AD_COUNT - LINK_AD_DATA))
#define LINK_AD_BYTES (LINK_AD_WORDS * 2u)

/* -------------------------------------------------------------------- pads */

/*
 * The pads that are not pins: grounds, rails, and the few that are neither.
 *
 * A servo lead has three wires and the catalogue describes one of them. An
 * operator who has found where the signal goes still has to find a ground for
 * the return and a rail for the positive, and counting pads on a board to do
 * it is how a lead goes to the wrong place.
 *
 * Separate from the catalogue because they do not fit in it: a page is
 * thirty-two registers, the catalogue is one per pin an output may have, and
 * a forty-pad board has more pads than that between the two of them.
 *
 * A coprocessor that does not serve this page has its grounds and rails
 * unmarked, which is where the picker started. It is a lead placed by
 * reading the board instead of the screen, not a bench that does not work.
 */
#define LINK_PAD_SLOTS 32u
#define LINK_PAD_COUNT LINK_PAD_SLOTS

/** pad in 6 bits, what it is in 2, and its rail in 8 tenths of a volt. */
#define LINK_PAD_OF(pad, kind, dv) \
    ((uint16_t)((((unsigned)(pad) & 0x3Fu) << 10) \
                | (((unsigned)(kind) & 0x03u) << 8) \
                | ((unsigned)(dv) & 0xFFu)))
#define LINK_PAD_NUM(r)  ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_PAD_KIND(r) ((uint8_t)(((r) >> 8) & 0x03u))
#define LINK_PAD_DV(r)   ((uint8_t)((r) & 0xFFu))

/**
 * What a pad that is not a pin is.
 *
 * Tenths of a volt reach 25.5 V, which covers every rail a logic board
 * brings to a header. Zero on a power pad means the rail is not a fixed
 * voltage -- an input like VSYS follows whatever feeds it -- and the screen
 * says so rather than printing a number that is only sometimes true.
 */
typedef enum {
    LINK_PAD_NONE   = 0, /**< no pad in this slot; the list ends here      */
    LINK_PAD_GROUND = 1, /**< 0 V, and what a servo lead returns through   */
    LINK_PAD_POWER  = 2, /**< a rail, at LINK_PAD_DV tenths of a volt      */
    LINK_PAD_OTHER  = 3, /**< neither: a RUN or an enable                  */
} link_pad_kind_t;

/* --------------------------------------------------------------------- KST */
/*
 * The servo programming page (KST, 0x32, since 4.12): the programming
 * port for KST and Chaservo servos.  One programming port (the PROG page) at a time leaves
 * PWM (pulse-width modulation) and carries the servo's programming protocol
 * (protocols/kst).  The session runs at the coprocessor: the panel writes
 * an operation and the images it needs, and reads the state, the result
 * and the servo's 32 registers.  shared/outputs/kst_port.h holds the
 * rules.
 *
 * A read and a write of the page see different registers.  A read gives
 * the 32 registers LINK_KS_*.  A write takes LINK_KS_W_*: the command in
 * registers 0 to 3, which is one frame and taken whole or not at all, and
 * the staged registers 4 to 24, written in any windows before the command
 * that names their CRC (cyclic redundancy check).  Nothing is kept in
 * flash.
 */
enum {
    LINK_KS_STATE = 0,   /**< bits 0-2 link_kst_state_t, bits 3-11 the
                              LINK_KS_F_* flags, bits 12-15 the port */
    LINK_KS_SEQ   = 1,   /**< the SEQ of the last command taken; 0 at start */
    LINK_KS_OP    = 2,   /**< bits 12-15 the last operation started
                              (link_kst_op_t), bits 0-11 the frames it has
                              sent, held at 4095 */
    LINK_KS_RESULT = 3,  /**< low byte the result of that operation
                              (kst_ses_t or a LINK_KST_RESULT_*; 1 while it
                              runs, LINK_KST_RESULT_PLANNING while it
                              plans), high byte why the last command taken
                              started nothing (link_kst_refusal_t; 0: it
                              started) */
    LINK_KS_FAIL  = 4,   /**< low byte the register the result names, 0xFF
                              none; high byte the writes of the plan done */
    LINK_KS_DIFF_LO = 5, /**< bit n: register n differs from what the
                              operation expected; registers 0 to 15 */
    LINK_KS_DIFF_HI = 6, /**< registers 16 to 31 */
    LINK_KS_BAD_LO  = 7, /**< bit n: register n has no 3 equal reads */
    LINK_KS_BAD_HI  = 8,
    LINK_KS_FP_RULES = 9,    /**< bit n: fingerprint rule n is broken */
    LINK_KS_FP_REGS_LO = 10, /**< bit n: register n deviates from the
                                  fingerprint */
    LINK_KS_FP_REGS_HI = 11,
    LINK_KS_HALF_MIN_NS = 12, /**< the servo's half-cell as measured on its
                                   replies since the port took the channel,
                                   ns; 0 before the first reply */
    LINK_KS_HALF_MAX_NS = 13,
    LINK_KS_DELAY_MIN_US = 14, /**< last edge of a read frame to the reply's
                                    first edge, us; 0 before the first */
    LINK_KS_DELAY_MAX_US = 15,
    LINK_KS_IMAGE = 16,  /**< 16 registers: servo register 2 k in the low
                              byte of register 16 + k, 2 k + 1 in the high
                              byte.  The image last read, or the backup
                              (LINK_KS_W_VIEW); 0 while there is none */
    LINK_KS_COUNT = 32,
};

/** What a write of the page takes. */
enum {
    LINK_KS_W_CMD = 0,   /**< bits 0-3 link_kst_op_t, bit 6
                              LINK_KS_W_CMD_PORT, bit 7 the confirmation
                              of an unchecked restore, bits 8-11 the
                              programming port; every other bit 0 */
    LINK_KS_W_SEQ = 1,   /**< LINK_KS_SEQ + 1 modulo 65536 */
    LINK_KS_W_KEY = 2,   /**< LINK_KST_WRITE_KEY for an operation that
                              writes to the servo, else 0 */
    LINK_KS_W_CRC = 3,   /**< link_kst_crc() of registers 4 to 24 for a
                              write, a restore, a release and a verify,
                              else not looked at */
    LINK_KS_W_IMAGE = 4, /**< 16 registers, packed as LINK_KS_IMAGE: the
                              target of a write, the backup of a restore,
                              the image a verify expects */
    LINK_KS_W_UNLOCK = 20,    /**< 4 registers, low word first: the fields
                                   an edit may change beyond the editable
                                   class, bit n for kst_field_id_t n */
    LINK_KS_W_START_CRC = 24, /**< link_kst_crc() of the 16 registers of
                                   the image the panel planned from, as
                                   LINK_KS_IMAGE gave them */
    LINK_KS_W_VIEW = 25,      /**< what LINK_KS_IMAGE shows: 0 the image
                                   last read, 1 the backup.  Not staged:
                                   in force when written */
    LINK_KS_W_SUPPLY_MV = 26, /**< 4.13: the supply voltage of an ENTER,
                                   in mV.  Not covered by the CRC */
    LINK_KS_W_SUPPLY_MA = 27, /**< 4.13: and its current limit, in mA */
    LINK_KS_W_LEAVE_MS = 28,  /**< 4.15: how long a LEAVE keeps the line low
                                   before the port is in PWM, in ms, 0 to
                                   LINK_KST_LEAVE_MS_MAX;
                                   LINK_KST_LEAVE_MS_DEFAULT until written.
                                   Taken as written: a LEAVE with a value
                                   above the range is refused.  Not covered
                                   by the CRC */
    LINK_KS_W_COUNT = 29,
};

/** Registers of the command, from LINK_KS_W_CMD: one frame. */
#define LINK_KS_W_CMD_FRAME 4u
/** The staged registers a command's CRC covers, from LINK_KS_W_IMAGE. */
#define LINK_KS_W_STAGED 21u
/** Registers a servo image takes on the page. */
#define LINK_KS_IMAGE_REGS 16u

/** LINK_KS_W_KEY of an operation that writes to the servo.  The panel
 *  sends it only while its switch ENABLE WRITE TO SERVO is on. */
#define LINK_KST_WRITE_KEY 0x57A5u

#define LINK_KS_STATE_OF(r)   ((uint8_t)((r) & 0x7u))
#define LINK_KS_PORT_OF(r)    ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_OP_OF(r)      ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_FRAMES_OF(r)  ((uint16_t)((r) & 0xFFFu))
#define LINK_KS_W_CMD_CONFIRM 0x0080u
/** Bits 8-11 of the command name a programming port (4.13).  Every command
 *  but ABORT carries it; one without it names an output channel, which is
 *  no programming port, and is refused with LINK_KST_REF_PORT. */
#define LINK_KS_W_CMD_PORT    0x0040u
#define LINK_KS_W_CMD_OF(op, port) \
    ((uint16_t)((unsigned)(op) | LINK_KS_W_CMD_PORT \
                | (((unsigned)(port) & 0xFu) << 8)))
/** LINK_KS_RESULT's low byte when the supply did not switch in time.
 *  Above every kst_ses_t. */
#define LINK_KST_RESULT_SUPPLY 32u
/** 4.14: a WRITE or a RESTORE has no order of writes in which every image
 *  in between is valid.  Nothing was sent. */
#define LINK_KST_RESULT_NO_PATH 33u
/** 4.14: the planner spent its work (KST_PLAN_WORK_CAP units) before it
 *  had a plan or knew there is none.  Nothing was sent. */
#define LINK_KST_RESULT_TOO_COMPLEX 34u
/** 4.14: LINK_KS_RESULT's low byte while a WRITE or a RESTORE plans:
 *  LINK_KS_F_BUSY is set and no frame has gone out.  1 follows when the
 *  plan is there.  Never the result of an operation that has ended. */
#define LINK_KST_RESULT_PLANNING 35u

/** 4.15: the longest wait LINK_KS_W_LEAVE_MS takes for a LEAVE, in ms. */
#define LINK_KST_LEAVE_MS_MAX 10000u
/** 4.15: the wait of a LEAVE while LINK_KS_W_LEAVE_MS was never written,
 *  in ms.  The wait a KST servo needs is not measured. */
#define LINK_KST_LEAVE_MS_DEFAULT 100u

/** The CRC of @p n page registers, low byte first, seeded 0xFFFF. */
static inline uint16_t link_kst_crc(const uint16_t *regs, unsigned n)
{
    uint16_t crc = 0xFFFFu;

    for (unsigned i = 0; i < n; ++i) {
        const uint8_t pair[2] = { (uint8_t)(regs[i] & 0xFFu),
                                  (uint8_t)(regs[i] >> 8) };
        crc = link_crc(crc, pair, sizeof(pair));
    }
    return crc;
}

/** Where the programming port is. */
typedef enum {
    LINK_KST_PWM = 0,      /**< the port's pin is the PWM generator's */
    LINK_KST_STOPPING = 1, /**< PWM renders no pulse; the running one ends */
    LINK_KST_LOW = 2,      /**< the pin is the driver's and held low; the
                                supply is set */
    LINK_KST_PROGRAMMING = 3, /**< the pin carries frames and no PWM */
    LINK_KST_RAIL_OFF = 4, /**< the servo's supply is switched off */
    LINK_KST_RAIL_WAIT = 5, /**< the supply is on again; the servo starts */
    LINK_KST_LEAVING = 6,  /**< 4.15: a LEAVE waits: the pin is the driver's
                                and low, no frame goes out, the supply is
                                untouched; PWM follows */
} link_kst_state_t;

enum {
    LINK_KS_F_BUSY      = 0x0008u, /**< an operation runs */
    LINK_KS_F_IN_MODE   = 0x0010u, /**< a frame was sent since the servo
                                        was last without power: it may be
                                        in programming mode */
    LINK_KS_F_MUST_READ = 0x0020u, /**< a stop or an abort cut the
                                        session: read all before anything
                                        else */
    LINK_KS_F_IMAGE     = 0x0040u, /**< an image was read */
    LINK_KS_F_BACKUP    = 0x0080u, /**< the session holds a backup */
    LINK_KS_F_FP_OK     = 0x0100u, /**< the image has the known layout */
    LINK_KS_F_LOCKED    = 0x0200u, /**< only a restore is accepted */
    LINK_KS_F_RAIL      = 0x0400u, /**< a PD mini answers: the port
                                        switches the servo's supply, and a
                                        power cycle is one the port makes */
    LINK_KS_F_VIEW_BACKUP = 0x0800u, /**< LINK_KS_IMAGE shows the backup */
};

/** The operations of a command. */
typedef enum {
    LINK_KST_OP_NONE = 0,     /**< never written; LINK_KS_OP at start */
    LINK_KST_OP_ENTER = 1,    /**< take the port's pin out of PWM, set the
                                   supply and enter programming mode */
    LINK_KST_OP_READ_ALL = 2,
    LINK_KST_OP_WRITE = 3,    /**< an edit from the image read to the staged
                                   image; needs the key */
    LINK_KST_OP_RESTORE = 4,  /**< back to the staged backup; needs the key */
    LINK_KST_OP_RELEASE = 5,  /**< release pairing; needs the key */
    LINK_KST_OP_VERIFY = 6,   /**< read all and compare with the staged
                                   image */
    LINK_KST_OP_ABORT = 7,    /**< ends the running operation; never the
                                   operation LINK_KS_OP names */
    LINK_KST_OP_POWER_CYCLED = 8, /**< the servo was without power, or the
                                       port is to switch the supply */
    LINK_KST_OP_REBOOT = 9,   /**< 4.13: switch the supply off and on, from
                                   PROGRAMMING or from PWM */
    LINK_KST_OP_LEAVE = 10,   /**< 4.15: from PROGRAMMING back to PWM after
                                   LINK_KS_W_LEAVE_MS with the line low; the
                                   supply is not switched and not read */
    LINK_KST_OP_COUNT
} link_kst_op_t;

/** Why a command that was taken did nothing: the high byte of
 *  LINK_KS_RESULT. */
typedef enum {
    LINK_KST_REF_NONE = 0,
    LINK_KST_REF_BUSY = 1,       /**< an operation runs or the port moves */
    LINK_KST_REF_PORT = 2,       /**< no such programming port, a command
                                      without LINK_KS_W_CMD_PORT, or not
                                      the port that is held.  3 and 4 are
                                      not used */
    LINK_KST_REF_UNSAFE = 5,     /**< heartbeat not trusted or link silent */
    LINK_KST_REF_NOT_PROGRAMMING = 6, /**< the port is in PWM */
    LINK_KST_REF_NO_ENABLE = 7,  /**< a write without the key */
    LINK_KST_REF_STAGED = 8,     /**< the staged registers do not have the
                                      CRC the command names */
    LINK_KST_REF_START = 9,      /**< the image read is not the one the
                                      panel planned from */
    LINK_KST_REF_MUST_READ = 10, /**< after a stop or an abort: read all
                                      first */
    LINK_KST_REF_NO_IMAGE = 11,  /**< no image read to plan from */
    LINK_KST_REF_PLAN_RULES = 12,   /**< the target breaks a hard rule */
    LINK_KST_REF_PLAN_R00 = 13,     /**< the target differs in register 0 */
    LINK_KST_REF_PLAN_PAIRING = 14, /**< an edit differs in register 0x1D */
    LINK_KST_REF_PLAN_NO_PATH = 15, /**< 4.12 and 4.13 only: no order of
                                         writes with valid images in
                                         between.  From 4.14 that is the
                                         result LINK_KST_RESULT_NO_PATH */
    LINK_KST_REF_NO_PIN = 16,    /**< 4.13: the port has no bound pin */
    LINK_KST_REF_SUPPLY_RANGE = 17, /**< 4.13: the staged supply voltage or
                                         current is not one the PD mini
                                         gives a servo */
    LINK_KST_REF_NO_SUPPLY = 18, /**< 4.13: a REBOOT with no PD mini
                                      answering, or from PWM with its
                                      output off */
    LINK_KST_REF_LEAVE_MS = 19,  /**< 4.15: a LEAVE with LINK_KS_W_LEAVE_MS
                                      above LINK_KST_LEAVE_MS_MAX */
    LINK_KST_REF_LOCKED = 20,    /**< 4.15: a LEAVE while LINK_KS_F_LOCKED
                                      is set: a write left the servo in
                                      doubt */
    LINK_KST_REF_SUPPLY_LOW = 21, /**< 4.15: an ENTER whose supply voltage,
                                       or the lower one of a PD mini output
                                       that is on, is under
                                       KST_PORT_SUPPLY_MIN_MV */
} link_kst_refusal_t;

/* -------------------------------------------------------------------- PROG */
/*
 * The PROG page (0x33, since 4.13): the programming ports.  A programming
 * port is an output of its own kind with its own pin.  It is no slot of the
 * OUTPUTS page and no channel of the CHANNELS page.  It renders a servo
 * pulse from the PWM (pulse-width modulation) generator, switched by its
 * bit in LINK_PG_PWM, or carries the programming protocol of the KST page,
 * never both.  shared/outputs/prog_port.h holds the rules.
 *
 * Registers 0 to 3 are the page's own; LINK_PG_STRIDE registers per port
 * follow from LINK_PG_PORT0.  PIN, SLEW, MIN_US and MAX_US are kept in
 * flash; PWM, TARGET and CMD are not.
 */
enum {
    LINK_PG_COUNT  = 0,  /**< read only: the ports this build has */
    LINK_PG_FLAGS  = 1,  /**< read only: bit n, port n's pin is bound to
                              the PWM generator; bit 8 + n, port n's pin
                              is refused */
    LINK_PG_PWM    = 2,  /**< bit n: port n renders its pulse while the
                              bank is armed */
    LINK_PG_TARGET = 3,  /**< what the SERVO page's rate, sweep and hold
                              command: 0 the standard servo ports, 1 + n
                              port n; a change sets the SERVO rate to 0 */
    LINK_PG_PORT0  = 4,
    LINK_PG_REGS   = 24,
};
enum {
    LINK_PG_PIN    = 0,  /**< per port: the coprocessor GPIO, or
                              LINK_PG_PIN_NONE */
    LINK_PG_CMD    = 1,  /**< the command, 0 to 1000 */
    LINK_PG_SLEW   = 2,  /**< as LINK_CC_SLEW */
    LINK_PG_MIN_US = 3,  /**< as LINK_CC_MIN_US */
    LINK_PG_MAX_US = 4,  /**< as LINK_CC_MAX_US */
    LINK_PG_STRIDE = 5,
};
/** Ports the page has room for. */
#define LINK_PG_PORTS_MAX 4u
#define LINK_PG_PIN_NONE  0xFFFFu
#define LINK_PG_REG(port, r) \
    ((uint8_t)(LINK_PG_PORT0 + (unsigned)(port) * LINK_PG_STRIDE + (r)))
#define LINK_PG_F_BOUND(port)   ((uint16_t)(1u << (unsigned)(port)))
#define LINK_PG_F_REFUSED(port) ((uint16_t)(0x100u << (unsigned)(port)))

/* --------------------------------------------------------------------- BUS */
/*
 * The BUS page (0x34, since 4.23): a serial bus servo protocol on a
 * programming port.  One programming port (the PROG page) at a time leaves
 * PWM (pulse-width modulation) and carries the bus: half duplex on the
 * port's pin.  The session runs at the coprocessor; the panel writes one
 * operation at a time and reads the state, the result, the IDs a scan
 * found (on the BUS_IDS page), the bytes a read gave and the live block of
 * the servo it watches.  shared/outputs/bus_port.h holds the rules.  Protocol 0 is Feetech SCS
 * and STS (protocols/feetech); every other protocol number is refused.
 *
 * A read and a write of the page see different registers.  A read gives
 * LINK_BS_*.  A write takes LINK_BS_W_*: the command in registers 0 to 3,
 * which is one CAN frame and taken whole or not at all, and the staged
 * registers 4 to 13, written in any windows before the command that names
 * their CRC (cyclic redundancy check).  Nothing is kept in flash.
 */
enum {
    LINK_BS_STATE = 0,   /**< bits 0-1 link_bus_state_t, bits 2-7 the
                              LINK_BS_F_* flags, bits 8-11 the port,
                              bits 12-15 the protocol */
    LINK_BS_SEQ   = 1,   /**< the SEQ of the last command taken; 0 at
                              start */
    LINK_BS_OP    = 2,   /**< low byte the last operation started
                              (link_bus_op_t), high byte the baud index
                              in force */
    LINK_BS_RESULT = 3,  /**< low byte the result of that operation
                              (feetech_res_t: 0 done, 1 while it runs),
                              high byte why the last command taken started
                              nothing (link_bus_refusal_t; 0: it started) */
    LINK_BS_REPLY = 4,   /**< an operation's or the watch's: low byte
                              the error byte of the last good
                              reply, high byte its ID, 0xFF none */
    LINK_BS_SCAN  = 5,   /**< low byte the ID the scan asks next, high byte
                              the IDs found */
    LINK_BS_RANGE = 6,   /**< low byte the first ID of the last scan, high
                              byte the last */
    LINK_BS_WATCH = 7,   /**< low byte the ID watched, 0xFF none; high byte
                              the result of its last read */
    LINK_BS_WATCH_OK = 8,  /**< reads of the watched servo that decoded,
                                wrapping */
    LINK_BS_WATCH_BAD = 9, /**< and that did not, wrapping */
    LINK_BS_LIVE  = 10,  /**< 8 registers: the live block of the watched
                              servo, byte 2 k in the low byte of register
                              10 + k, 2 k + 1 in the high byte; as the
                              servo sent it; empty after a read that
                              failed */
    LINK_BS_COUNTS = 18, /**< low byte the bytes of LINK_BS_LIVE, high byte
                              the bytes of LINK_BS_DATA */
    LINK_BS_AT    = 19,  /**< low byte the address of the last read or
                              EEPROM write, high byte the IDs whose scan
                              answer did not decode */
    LINK_BS_DATA  = 20,  /**< 8 registers: the bytes the last read gave, or
                              an EEPROM write read back, packed as
                              LINK_BS_LIVE */
    LINK_BS_PIN   = 28,  /**< the coprocessor GPIO the bus drives, or
                              LINK_PG_PIN_NONE while it is closed */
    LINK_BS_COUNT = 29,
    LINK_BS_STATUS_REGS = LINK_BS_COUNT, /**< registers 0 to 28: one read */
};

/*
 * The BUS_IDS page (0x35, since 4.23), read-only: the IDs the BUS page's
 * last scan found.  Bit n of register k: ID 16 k + n answered.  The bits of
 * IDs 254 and 255 are always 0.
 */
enum {
    LINK_BI_IDS   = 0,   /**< 16 registers */
    LINK_BI_COUNT = 16,
};

/** What a write of the page takes. */
enum {
    LINK_BS_W_CMD = 0,   /**< bits 0-3 link_bus_op_t, bit 4 the series
                              (0 SCS, 1 STS), bits 8-11 the programming
                              port, bits 12-15 the protocol */
    LINK_BS_W_SEQ = 1,   /**< LINK_BS_SEQ + 1 modulo 65536 */
    LINK_BS_W_ARG = 2,   /**< OPEN and BAUD: the baud index; SCAN: low byte
                              the first ID, high byte the last; READ,
                              WRITE, EEPROM: low byte the ID, high byte
                              the address; WATCH: the ID, 0xFF none */
    LINK_BS_W_AUX = 3,   /**< READ: the bytes, 1 to 16; WRITE and EEPROM:
                              link_kst_crc() of registers 4 to 13; else
                              0 */
    LINK_BS_W_N   = 4,   /**< staged: the bytes of a WRITE or an EEPROM
                              write, 1 to 16 */
    LINK_BS_W_KEY = 5,   /**< staged: LINK_BUS_WRITE_KEY for an EEPROM
                              write, else 0 */
    LINK_BS_W_DATA = 6,  /**< staged: 8 registers, the bytes packed as
                              LINK_BS_LIVE */
    LINK_BS_W_COUNT = 14,
};

/** Registers of the command, from LINK_BS_W_CMD: one frame. */
#define LINK_BS_W_CMD_FRAME 4u
/** The staged registers the CRC covers, from LINK_BS_W_N. */
#define LINK_BS_W_STAGED 10u
/** Bytes a read or a write carries, and the live block's room. */
#define LINK_BS_BYTES 16u
/** The page registers that hold LINK_BS_BYTES bytes. */
#define LINK_BS_BYTE_REGS 8u

/** LINK_BS_W_KEY of an EEPROM write.  The panel sends it only after the
 *  2 s hold on the write's warning. */
#define LINK_BUS_WRITE_KEY 0x57A5u

/** The protocols. */
#define LINK_BUS_PROTO_FEETECH 0u

#define LINK_BS_STATE_OF(r)  ((uint8_t)((r) & 0x3u))
#define LINK_BS_PORT_OF(r)   ((uint8_t)(((r) >> 8) & 0xFu))
#define LINK_BS_PROTO_OF(r)  ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_BS_W_CMD_STS    0x0010u
#define LINK_BS_W_CMD_OF(op, port, proto, sts) \
    ((uint16_t)((unsigned)(op) | ((sts) ? LINK_BS_W_CMD_STS : 0u) \
                | (((unsigned)(port) & 0xFu) << 8) \
                | (((unsigned)(proto) & 0xFu) << 12)))

/** Where the bus is. */
typedef enum {
    LINK_BUS_CLOSED = 0,   /**< the port's pin is the PWM generator's */
    LINK_BUS_OPENING = 1,  /**< PWM renders no pulse on the port; the
                                running one ends */
    LINK_BUS_OPEN = 2,     /**< the pin carries the bus and no PWM */
    LINK_BUS_CLOSING = 3,  /**< torque off and the lock are sent; the pin
                                goes back to the PWM generator */
} link_bus_state_t;

enum {
    LINK_BS_F_BUSY     = 0x0004u, /**< an operation or a stop runs */
    LINK_BS_F_WATCH    = 0x0008u, /**< a servo is watched */
    LINK_BS_F_UNLOCKED = 0x0010u, /**< a servo's lock flag is 0 by this
                                       session and its lock is owed */
    LINK_BS_F_SAFE     = 0x0020u, /**< the heartbeat is trusted and the
                                       link is not silent */
};

/** The operations of a command. */
typedef enum {
    LINK_BUS_OP_NONE = 0,     /**< never written; LINK_BS_OP at start */
    LINK_BUS_OP_OPEN = 1,     /**< take the port's pin out of PWM at the
                                   baud index */
    LINK_BUS_OP_CLOSE = 2,    /**< stop, then give the pin back */
    LINK_BUS_OP_SCAN = 3,     /**< PING every ID of a range */
    LINK_BUS_OP_READ = 4,     /**< 1 to 16 bytes from one servo */
    LINK_BUS_OP_WRITE = 5,    /**< 1 to 16 bytes to the RAM of one servo,
                                   the lock flag excepted */
    LINK_BUS_OP_EEPROM = 6,   /**< an EEPROM write: unlock, write, lock,
                                   read back; needs the key */
    LINK_BUS_OP_WATCH = 7,    /**< read one servo's live block every 50 ms
                                   between operations */
    LINK_BUS_OP_TORQUE_OFF = 8, /**< torque off to every servo */
    LINK_BUS_OP_STOP = 9,     /**< end the operation and the watch, torque
                                   off to every servo, the lock owed */
    LINK_BUS_OP_BAUD = 10,    /**< the line to another baud index */
    LINK_BUS_OP_COUNT
} link_bus_op_t;

/** Why a command that was taken did nothing: the high byte of
 *  LINK_BS_RESULT. */
typedef enum {
    LINK_BUS_REF_NONE = 0,
    LINK_BUS_REF_BUSY = 1,     /**< an operation runs, or the bus opens or
                                    closes */
    LINK_BUS_REF_PORT = 2,     /**< no such programming port, or not the
                                    port that is open */
    LINK_BUS_REF_PROTOCOL = 3, /**< a protocol this build does not have */
    LINK_BUS_REF_UNSAFE = 4,   /**< heartbeat not trusted or link silent */
    LINK_BUS_REF_ARMED = 5,    /**< an OPEN while the bank is armed */
    LINK_BUS_REF_NOT_OPEN = 6, /**< the bus is closed */
    LINK_BUS_REF_NO_PIN = 7,   /**< the port has no bound pin, or the KST
                                    page holds it */
    LINK_BUS_REF_NO_KEY = 8,   /**< an EEPROM write without the key */
    LINK_BUS_REF_RANGE = 9,    /**< an ID, address, count or baud index the
                                    protocol does not take */
    LINK_BUS_REF_EEPROM = 10,  /**< a WRITE to the EEPROM area or the lock
                                    flag, or an EEPROM write outside it */
    LINK_BUS_REF_STAGED = 11,  /**< the staged registers do not have the
                                    CRC the command names */
} link_bus_refusal_t;

/* ----------------------------------------------------------------- control */
/*
 * ARM, THROTTLE and MOTOR_POLES are registers 0 to 2 so that the write that
 * arms carries all three in one CAN (Controller Area Network) frame.  The
 * count is the divisor the far end starts sampling with; a run begun on a
 * stale one puts a wrong speed into the bench page's sticky rpm_max, and a
 * frame is all-or-nothing at the far end, so the run starts on the count
 * that was sent or does not start.  CLEAR sits behind them: the far end
 * checks ARM against its failsafe before it applies a CLEAR from the same
 * frame, so the clear is its own transaction ahead of the arm.
 */
enum {
    LINK_CT_ARM        = 0, /**< 0 disarms; LINK_ARM_* asks for ARMED       */
    LINK_CT_THROTTLE   = 1, /**< 0..10000, hundredths of a percent          */
    /*
     * The magnet count of the motor under test, so an electrical speed can
     * be turned into a mechanical one.  It is the one number bidirectional
     * DShot cannot carry: an ESC (electronic speed controller) reports
     * electrical periods and has no idea what it is bolted to.  Zero means
     * nobody has said, and rpm (revolutions per minute) is then not reported
     * rather than reported wrong.
     */
    LINK_CT_MOTOR_POLES = 2,
    LINK_CT_CLEAR      = 3, /**< write LINK_CLEAR_MAGIC to leave FAILSAFE   */
    LINK_CT_COUNT      = 4,
};

/** The registers the frame that arms carries, from LINK_CT_ARM. */
#define LINK_CT_ARM_FRAME 3u

/** Even, and between these; zero is the fourth legal value, meaning unknown. */
#define LINK_POLES_MIN  2u
#define LINK_POLES_MAX 42u

/**
 * Leaving failsafe takes this value written to LINK_CT_CLEAR, not any write
 * and not the link coming back: a link that recovers must not re-arm a bench
 * on its own.
 */
#define LINK_CLEAR_MAGIC 0x5AFEu

/*
 * What a non-zero ARM arms, from LINK_MINOR_ARM_ROLE.
 *
 * An arm is limited to the roles it names.  A channel of another role stays
 * at no pulse, and a programming port is a surface.  LINK_ARM_ALL is every
 * role, which is what a panel older than LINK_MINOR_ARM_ROLE writes as 1.
 * Any other value is refused with LINK_NACK_BAD_VALUE, ahead of the failsafe
 * check, so a refused value never latches anything.
 *
 * The roles of an arm change only through a disarm: a non-zero ARM that
 * differs from the non-zero ARM in force is refused with LINK_NACK_BAD_VALUE.
 * Writing the value in force again is not a change.
 */
#define LINK_ARM_ALL       0x0001u /**< every role                          */
#define LINK_ARM_THROTTLE  0x0002u /**< outputs bound as a motor            */
#define LINK_ARM_SURFACE   0x0004u /**< outputs bound as a servo, and ports */
#define LINK_ARM_ROLES     (LINK_ARM_THROTTLE | LINK_ARM_SURFACE)

/** Throttle is hundredths of a percent: 0..10000 inclusive. */
#define LINK_THROTTLE_MAX 10000u

/* ----------------------------------------------------------------- update */

/*
 * The coprocessor's firmware, written over the link into the flash slot it
 * is not running from.  Frozen: see LINK_PROTOCOL_MAJOR.  The registers are
 * versioned by LINK_UPD_VERSION in register 0, which only grows, and only by
 * registers or commands added at the end.
 *
 * The flash holds two equal slots, A and B, behind the boot ROM's partition
 * table.  The image goes to the slot that is not running, in 64-byte blocks
 * (LINK_UPD_BLOCK_BYTES), strictly in order, each staged in UPDATE_DATA and
 * then committed with its CRC-16 (cyclic redundancy check, link_crc() seeded
 * LINK_CRC_INIT over the block's bytes).  The coprocessor writes a 4 KiB
 * sector once its 64 blocks are in, and acknowledges the COMMIT that filled
 * it only once the sector is erased and programmed.  The slot's first sector
 * is erased at BEGIN and written last, at ACTIVATE, so a slot written in part
 * holds no image the boot ROM would start.
 *
 * Once every block is in, FINISH compares the SHA-256 (Secure Hash
 * Algorithm) of the slot with the digest the panel staged in UPDATE_DATA
 * registers 0 to 15.  ACTIVATE writes the first sector, reads the whole
 * sector back and restarts into the new slot as a flash update boot.  The
 * image the panel sends carries the TBYB (try before you buy) flag, so the
 * boot ROM starts it under a 16.7 s watchdog; it stays only when the panel
 * reads its identity over the link and writes BUY.  Without the BUY the
 * watchdog restarts the coprocessor into the slot it came from.
 *
 * A session runs from BEGIN to ACTIVATE, ABORT, a failure or a restart.
 * While one runs, and while a BUY is pending, the coprocessor refuses ARM
 * with LINK_NACK_NOT_ARMED.  BEGIN, COMMIT and ACTIVATE need the store's
 * gate open -- the bank not driving, the supply's output off, no servo
 * programming session -- and refuse with LINK_NACK_BUSY while it is shut.
 * Five seconds (LINK_UPD_SILENCE_MS) without a request to UPDATE_CTL or
 * UPDATE_DATA ends a session with LINK_UPD_ERR_LINK; the panel's ordinary
 * polling does not keep one alive.  A restart ends it as well; the panel
 * then starts again from block 0.  A panel that finds a session it did not
 * start -- its own, from before it restarted -- ends it with ABORT.
 *
 * After a BUY the slot the image came from must hold no image the boot ROM
 * could start, or a later normal boot may choose between two.  The
 * coprocessor erases that slot's first sector: at the BUY when the gate is
 * open, and otherwise, or after an erase that failed, once a second while
 * the gate is open until the erase holds.  It owes the same erase after any
 * start that finds the other slot's first sector written outside a session
 * or a trial.  LINK_UPD_SLOT_OTHER says the erase is owed.  The BUY is
 * acknowledged when the boot ROM took it, owed erase or not: the bought
 * image is the one a normal boot prefers either way.
 *
 * A command is one frame: a write of 1 to 4 registers starting at
 * LINK_UPD_CMD, the arguments after it.  Any other write to UPDATE_CTL is
 * refused with LINK_NACK_READ_ONLY.  A command that does not fit the state
 * is refused with LINK_NACK_BAD_VALUE and the reason in LINK_UPD_ERROR.
 */
enum {
    LINK_UPD_VERSION     = 0,  /**< LINK_UPD_PROTOCOL; read-only            */
    LINK_UPD_STATE       = 1,  /**< a link_upd_state_t                       */
    LINK_UPD_ERROR       = 2,  /**< a link_upd_err_t: why the last refusal   */
    LINK_UPD_SLOT        = 3,  /**< LINK_UPD_SLOT_* bits                     */
    LINK_UPD_SLOT_SECTORS = 4, /**< 4 KiB sectors in one slot; 0 when none   */
    LINK_UPD_NEXT_BLOCK  = 5,  /**< the block COMMIT takes next              */
    LINK_UPD_SIZE_LO     = 6,  /**< bytes of the session's image, or of the  */
    LINK_UPD_SIZE_HI     = 7,  /**<   running slot hashed by HASH_RUNNING     */
    LINK_UPD_CMD         = 8,  /**< a link_upd_cmd_t; reads the last taken   */
    LINK_UPD_ARG0        = 9,
    LINK_UPD_ARG1        = 10,
    LINK_UPD_ARG2        = 11,
    LINK_UPD_DIGEST_OF   = 12, /**< a link_upd_digest_t: what DIGEST holds   */
    /* 13 to 15 read 0. */
    LINK_UPD_DIGEST      = 16, /**< 16 registers, 32 bytes, low byte first   */
    LINK_UPD_COUNT       = 32,
};

/** The update registers' own version; it grows when a register or command
 *  is added. */
#define LINK_UPD_PROTOCOL 1u

/** Bytes in a block, and registers in UPDATE_DATA. */
#define LINK_UPD_BLOCK_BYTES 64u
#define LINK_UPD_DATA_COUNT  32u
/** Bytes in a flash sector, the unit of an erase. */
#define LINK_UPD_SECTOR_BYTES 4096u
/** UPDATE_DATA registers FINISH takes the expected SHA-256 from. */
#define LINK_UPD_EXPECT_REGS 16u

/** LINK_UPD_SLOT: the running slot in bits 0 to 3, and two flags. */
#define LINK_UPD_SLOT_A      0x0000u
#define LINK_UPD_SLOT_B      0x0001u
/** No slot to write: no partition table, or not the one this build has. */
#define LINK_UPD_SLOT_NONE   0x000Fu
#define LINK_UPD_SLOT_MASK   0x000Fu
/** The running image is on trial and waits for BUY. */
#define LINK_UPD_SLOT_TRIAL  0x0100u
/** The other slot's first sector still waits for its erase. */
#define LINK_UPD_SLOT_OTHER  0x0200u

typedef enum {
    LINK_UPD_IDLE      = 0, /**< no session                                 */
    LINK_UPD_HASHING   = 1, /**< HASH_RUNNING works through the slot        */
    LINK_UPD_RECEIVING = 2, /**< BEGIN taken; blocks go in                  */
    LINK_UPD_RECEIVED  = 3, /**< every block is in                          */
    LINK_UPD_VERIFYING = 4, /**< FINISH hashes the slot                     */
    LINK_UPD_VERIFIED  = 5, /**< the digest matched; ACTIVATE may go        */
    LINK_UPD_FAILED    = 6, /**< LINK_UPD_ERROR says why; BEGIN starts over */
    LINK_UPD_ACTIVATED = 7, /**< the first sector is in; restart follows    */
} link_upd_state_t;

typedef enum {
    LINK_UPD_CMD_NONE         = 0,
    /** SHA-256 of the running slot's first ARG0 | ARG1 << 16 bytes, with the
     *  image's TBYB flag read as clear (link_update_image.h).  Idle only. */
    LINK_UPD_CMD_HASH_RUNNING = 1,
    /** A session for an image of ARG0 | ARG1 << 16 bytes: 1 to the slot. */
    LINK_UPD_CMD_BEGIN        = 2,
    /** UPDATE_DATA is block ARG0; ARG1 is its CRC-16.  The last block is
     *  the image's remainder, its CRC over those bytes only.  The block
     *  before LINK_UPD_NEXT_BLOCK again is acknowledged and not written. */
    LINK_UPD_CMD_COMMIT       = 3,
    /** UPDATE_DATA registers 0 to 15 hold the expected SHA-256. */
    LINK_UPD_CMD_FINISH       = 4,
    /** Write the first sector and restart into the new slot. */
    LINK_UPD_CMD_ACTIVATE     = 5,
    /** End the session; the slot is left without an image. */
    LINK_UPD_CMD_ABORT        = 6,
    /** Keep the image on trial.  Acknowledged with nothing to do when no
     *  image is on trial. */
    LINK_UPD_CMD_BUY          = 7,
} link_upd_cmd_t;

typedef enum {
    LINK_UPD_ERR_NONE     = 0,
    LINK_UPD_ERR_NO_SLOTS = 1,  /**< no partition table to write into        */
    LINK_UPD_ERR_GATE     = 2,  /**< the store's gate is shut; try again     */
    LINK_UPD_ERR_SIZE     = 3,  /**< 0, or larger than a slot                */
    LINK_UPD_ERR_ORDER    = 4,  /**< not the block expected next             */
    LINK_UPD_ERR_CRC      = 5,  /**< the staged block fails its CRC          */
    LINK_UPD_ERR_FLASH    = 6,  /**< an erase, a program or a read-back      */
    LINK_UPD_ERR_DIGEST   = 7,  /**< the slot's SHA-256 is not the one sent  */
    LINK_UPD_ERR_STATE    = 8,  /**< the command does not fit the state      */
    LINK_UPD_ERR_TRIAL    = 9,  /**< an image on trial: BUY it first         */
    LINK_UPD_ERR_LINK     = 10, /**< the link went silent in a session       */
    LINK_UPD_ERR_BUY      = 11, /**< the boot ROM refused the BUY            */
    LINK_UPD_ERR_COMMAND  = 12, /**< no such command                         */
} link_upd_err_t;

typedef enum {
    LINK_UPD_DIGEST_NONE    = 0,
    LINK_UPD_DIGEST_RUNNING = 1, /**< HASH_RUNNING's, over SIZE bytes        */
    LINK_UPD_DIGEST_IMAGE   = 2, /**< FINISH's, over the image written       */
} link_upd_digest_t;

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PAGES_H */
