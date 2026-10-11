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
    LINK_PAGE_ARTWORK   = 0x26, /**< what a picture of the board is        */
    LINK_PAGE_ART_DATA  = 0x27, /**< and the picture itself, a block at a time */
    LINK_PAGE_PADS      = 0x28, /**< the pads that are not pins, read-only  */
    LINK_PAGE_SERVO     = 0x29, /**< the surfaces' frame rate, not kept     */
    LINK_PAGE_SUPPLY    = 0x2A, /**< the PD mini on a PIO UART; wiring kept */
    LINK_PAGE_SENSE     = 0x2B, /**< the current monitors' bus; set-up kept */
    LINK_PAGE_SERVO_SENSE = 0x2C, /**< the servo rail's channels, a move timed */
    LINK_PAGE_TONE      = 0x2D, /**< ESC tones from one motor phase; set-up kept */
    LINK_PAGE_BIND_CFG  = 0x2E, /**< a CHAN_CFG page prepared, not in force  */
    LINK_PAGE_BIND_OUT  = 0x2F, /**< an OUTPUTS page prepared, not in force  */
    LINK_PAGE_BIND      = 0x30, /**< takes the two prepared pages as one     */
    LINK_PAGE_SERVO_WIN = 0x31, /**< the servo rail's last CH1 windows, read-only */
    LINK_PAGE_KST       = 0x32, /**< the KST servo programming port          */
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
 * and a signed CAP_HOLD_MA from 4.11, and KST from 4.12.  A
 * coprocessor never asks the panel's minor; a
 * page an older panel does not know is a page it never writes.
 */
#define LINK_PROTOCOL_MAJOR 4u
#define LINK_PROTOCOL_MINOR 12u

/** The first minor that serves BIND_CFG, BIND_OUT and BIND. */
#define LINK_MINOR_BIND 10u
/** The first minor that serves SERVO_WIN, counts resets in SENSE register
 *  31 and takes a negative CAP_HOLD_MA. */
#define LINK_MINOR_SERVO_WIN 11u
/** The first minor that serves KST. */
#define LINK_MINOR_KST 12u

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
    LINK_CC_SLEW    = 1, /**< span a second; 0 means at once            */
    LINK_CC_MIN_US  = 2, /**< the pulse a command of 0 renders as       */
    LINK_CC_MAX_US  = 3, /**< and of LINK_CH_SPAN                       */
    LINK_CC_STRIDE  = 4,
};
#define LINK_CC_COUNT  (LINK_OUT_CHANNELS * LINK_CC_STRIDE)

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

/* --- the SERVO screen's page: the surfaces' frame rate (protocol 4.1) and
 *     the sweep that drives them through a curve (4.2).
 *
 *     FRAME_HZ is the frame rate, in Hz, of every PWM output whose first
 *     channel is a surface, or 0 for each slot's own rate from the OUTPUTS
 *     page.  One register for all of them, so a write is whole: a servo
 *     screen that moved some surfaces to 560 Hz and not their slice-mates
 *     would leave a slice asked for two rates.  A rate that would do that
 *     against the binding in force -- a surface and a throttle on one slice
 *     -- is refused with BAD_VALUE and nothing changes.
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
 * (0 to 7). */
#define LINK_SS_ARM            0x80u
#define LINK_SS_ARM_OF(ch, out) \
    ((uint16_t)(LINK_SS_ARM | ((unsigned)(ch) & 0x03u) \
                | (((unsigned)(out) & 0x07u) << 8)))
#define LINK_SS_ARM_CH(r)      ((uint8_t)((r) & 0x03u))
#define LINK_SS_ARM_OUT(r)     ((uint8_t)(((r) >> 8) & 0x07u))
/** Every bit CAP_ARM may carry. */
#define LINK_SS_ARM_BITS       0x0783u
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
 *     past the bank, reserved, bound to an output, held by the SENSE or
 *     SUPPLY page, or an ADC (analog to digital converter) pin: GP26 to
 *     GP29 on the RP2350A and GP40 to GP47 on the RP2354B, rated IOVDD +
 *     0.5 V and not fault tolerant.  The pin is no output's while the tap
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
    LINK_ST_COUNT      = 8,
};

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
     * Edge to the centre of the pad row.  The outline's own edge, not a
     * photograph's: artwork carries its own calibration because a photo is
     * not cropped to the outline.
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
 * A photograph of the board, so the picker shows the thing in the
 * operator's hands rather than an outline of it.
 *
 * Two pages because a page is thirty-two registers and no more.  The
 * picture is two hundred kilobytes, which is three thousand-odd reads
 * however it is cut, so every register the metadata takes out of the data
 * page costs another sixty round trips.  Metadata here, payload next door,
 * and a block stays sixty-two bytes.
 *
 * It is transferred once and kept.  At 1 Mbit/s a block is a write to say
 * which one and a read to fetch it, about 1.4 ms the pair, so the whole
 * picture is roughly ten seconds of link -- fine once for a board that has
 * never been seen, and absurd every time the link comes up.
 *
 * LINK_AW_BLOCKS of zero means this coprocessor carries no picture of
 * itself.  That is the ordinary answer, not a fault: a board is drawn from
 * its shape page when there is no photograph, and used from its catalogue
 * when there is no shape.
 */
enum {
    LINK_AW_BLOCKS   = 0, /**< blocks of payload; 0 when there is none    */
    LINK_AW_WIDTH    = 1, /**< pixels                                     */
    LINK_AW_HEIGHT   = 2, /**< pixels                                     */
    LINK_AW_FORMAT   = 3, /**< a link_art_format_t                        */
    LINK_AW_BYTES_LO = 4, /**< payload length, low half                   */
    LINK_AW_BYTES_HI = 5,
    /**
     * link_crc() over the whole payload, seeded zero.  The transfer is long
     * enough that a block lost and silently skipped would otherwise be
     * found by an operator looking at a corrupted board photograph months
     * later, and cached in flash until then.
     */
    LINK_AW_CRC      = 6,
    LINK_AW_COUNT    = 7,
};

typedef enum {
    LINK_ART_NONE   = 0,
    LINK_ART_RGB565 = 1, /**< two bytes a pixel, low byte first           */
} link_art_format_t;

/*
 * The payload, a block at a time: write LINK_AD_BLOCK to say which, then
 * read the page.  Two transactions rather than one because a block that
 * advanced on being read would resend nothing after a reply went missing,
 * and the host would assemble a picture with a hole in it.
 *
 * Bytes little-endian within a register, the low byte first, which is the
 * order an RGB565 framebuffer is already in on both parts.  A final block
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
 * The KST page (0x32, since 4.12): the programming port for KST and
 * Chaservo servos.  One output channel at a time leaves PWM (pulse-width
 * modulation) and carries the servo's programming protocol
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
                              LINK_KS_F_* flags, bits 12-15 the channel */
    LINK_KS_SEQ   = 1,   /**< the SEQ of the last command taken; 0 at start */
    LINK_KS_OP    = 2,   /**< bits 12-15 the last operation started
                              (link_kst_op_t), bits 0-11 the frames it has
                              sent, held at 4095 */
    LINK_KS_RESULT = 3,  /**< low byte the result of that operation
                              (kst_ses_t; 1 while it runs), high byte why
                              the last command taken started nothing
                              (link_kst_refusal_t; 0: it started) */
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
    LINK_KS_W_CMD = 0,   /**< bits 0-3 link_kst_op_t, bit 7 the confirmation
                              of an unchecked restore, bits 8-11 the
                              channel; every other bit 0 */
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
    LINK_KS_W_COUNT = 26,
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
#define LINK_KS_CHANNEL_OF(r) ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_OP_OF(r)      ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_FRAMES_OF(r)  ((uint16_t)((r) & 0xFFFu))
#define LINK_KS_W_CMD_CONFIRM 0x0080u

/** The CRC of @p n page registers, low byte first, seeded 0xFFFF. */
static inline uint16_t link_kst_crc(const uint16_t *regs, unsigned n)
{
    uint16_t crc = 0xFFFFu;

    for (unsigned i = 0; i < n; ++i) {
        const uint8_t b[2] = { (uint8_t)(regs[i] & 0xFFu),
                               (uint8_t)(regs[i] >> 8) };
        crc = link_crc(crc, b, sizeof(b));
    }
    return crc;
}

/** Where the port's channel is. */
typedef enum {
    LINK_KST_PWM = 0,      /**< the channel renders PWM; no port */
    LINK_KST_STOPPING = 1, /**< PWM renders no pulse; the running one ends */
    LINK_KST_LOW = 2,      /**< the pin is the driver's and held low */
    LINK_KST_PROGRAMMING = 3, /**< the pin carries frames and no PWM */
    LINK_KST_RAIL_OFF = 4, /**< the servo rail is switched off */
    LINK_KST_RAIL_WAIT = 5, /**< the rail is on again; the servo starts */
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
    LINK_KS_F_RAIL      = 0x0400u, /**< the hardware switches the servo
                                        rail: a confirmed power cycle is
                                        one the port makes */
    LINK_KS_F_VIEW_BACKUP = 0x0800u, /**< LINK_KS_IMAGE shows the backup */
};

/** The operations of a command. */
typedef enum {
    LINK_KST_OP_NONE = 0,     /**< never written; LINK_KS_OP at start */
    LINK_KST_OP_ENTER = 1,    /**< take the channel and enter programming
                                   mode */
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
                                       port is to switch the rail */
    LINK_KST_OP_COUNT
} link_kst_op_t;

/** Why a command that was taken did nothing: the high byte of
 *  LINK_KS_RESULT. */
typedef enum {
    LINK_KST_REF_NONE = 0,
    LINK_KST_REF_BUSY = 1,       /**< an operation runs or the port moves */
    LINK_KST_REF_CHANNEL = 2,    /**< the channel is on no bound PWM slot,
                                      or is not the one the port holds */
    LINK_KST_REF_THROTTLE = 3,   /**< the channel's role is a throttle */
    LINK_KST_REF_NO_REPLY_PATH = 4, /**< the pin's path carries no reply */
    LINK_KST_REF_UNSAFE = 5,     /**< heartbeat not trusted or link silent */
    LINK_KST_REF_NOT_PROGRAMMING = 6, /**< the port holds no channel */
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
    LINK_KST_REF_PLAN_NO_PATH = 15, /**< no order of writes with valid
                                         images in between */
} link_kst_refusal_t;

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
    LINK_CT_ARM        = 0, /**< non-zero asks for ARMED                    */
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

/** Throttle is hundredths of a percent: 0..10000 inclusive. */
#define LINK_THROTTLE_MAX 10000u

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PAGES_H */
