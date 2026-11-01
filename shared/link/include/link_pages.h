/**
 * @file
 * @brief The page map: what the panel can ask the IOmcu for, and set.
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

/**
 * @brief The page numbers of the link protocol.
 */
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
     * changes meaning lets an old panel write into a page the IOmcu
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
    LINK_PAGE_HITEC     = 0x34, /**< raw Hitec wire transactions on a port   */
    LINK_PAGE_SENSE_MAP = 0x35, /**< up to 4 INA3221 parts: fitted, shunts,
                                     what each channel watches; kept     */
    LINK_PAGE_SENSE_LIMIT = 0x36, /**< a current limit per channel, kept,
                                       and the windows over them        */
    LINK_PAGE_SENSE_WIN_A = 0x37, /**< channels 0 to 5's last window, read-only */
    LINK_PAGE_SENSE_WIN_B = 0x38, /**< channels 6 to 11's, read-only         */
    LINK_PAGE_ESC_WIRE  = 0x3A, /**< an ESC's settings over its signal wire  */
    LINK_PAGE_ESC_DATA  = 0x3B, /**< its page, as blocks                     */
    LINK_PAGE_PROG_SUPPLY = 0x39, /**< the programming ports' own supply, on
                                       a board that switches one        */
    /* Frozen: the same numbers, layout and meaning under every major. */
    LINK_PAGE_UPDATE_CTL  = 0xF0, /**< the IOmcu's own firmware update */
    LINK_PAGE_UPDATE_DATA = 0xF1, /**< and the 64 bytes of the block in hand */
} link_page_id_t;

/**
 * @brief The protocol version is register 0 of page 0: the one thing a host
 * must be able to read from an IOmcu whose other pages it might not understand.
 *
 * Bump the major when a register changes meaning or a page is renumbered; bump
 * the minor when a page or a register is added at the end, which an older host
 * can ignore.
 *
 * Only the major has to agree for the link to come up and the bench to arm.
 * The panel reads the minor at link-up and sends nothing to a page the
 * IOmcu's minor does not have: SUPPLY from 4.3, SENSE and
 * SERVO_SENSE from 4.7, TONE from 4.8, and SENSE's output encoder (the
 * ENABLE bit LINK_SN_EN_AS5600 and registers 26 to 30) from 4.9, and
 * BIND_CFG, BIND_OUT and BIND from 4.10, and SERVO_WIN, SENSE's RESETS
 * and a signed CAP_HOLD_MA from 4.11, and KST from 4.12, and PROG, the port
 * fields of KST, CAP_ARM bit 11 and SUPPLY FLAGS bit 11 from 4.13.  From
 * 4.14 the servo programming page (KST, 0x32) plans a write after the
 * command's frame and reports LINK_KST_RESULT_PLANNING, _NO_PATH and _TOO_COMPLEX; nothing is sent
 * differently for it.  LINK_KST_OP_LEAVE and LINK_KS_W_LEAVE_MS go to an
 * IOmcu from 4.15 only.  From 4.16 CHAN_CFG and PROG refuse a slew
 * above 3000, and CHAN_CFG, OUTPUTS, BIND, SERVO and PROG refuse a write that would leave a PWM output with an endpoint longer
 * than its frame (out_pwm_pulse_max()); the panel states a port's range
 * before it switches the port's pulse on, which an older IOmcu takes
 * as well.  From 4.17 the
 * TONE page refuses a busy pin with LINK_NACK_BUSY and keeps BAD_VALUE for
 * a set-up no freed pin makes good; the panel offers the first frame again
 * after a BUSY only.  From 4.18 CONTROL's ARM names the roles it arms
 * (LINK_ARM_THROTTLE, LINK_ARM_SURFACE); the panel writes LINK_ARM_ALL to an
 * older IOmcu, which arms its whole bank on any non-zero ARM.  From
 * 4.19 the SERVO rate reaches the tester's target only (the standard servo
 * ports' surfaces, or the one programming port PROG's TARGET names) and a
 * TARGET change returns it to 0; the panel then counts the rate as 0 after
 * a TARGET change it saw, and as not known after a TARGET write that went
 * unacknowledged.  From 4.20 ARTWORK serves the board's drawing
 * (LINK_ART_DRAWING) with a hash of its payload in registers 7 and 8; the
 * panel reads ARTWORK from 4.20 only and draws an older IOmcu's board
 * from its shape page.  From 4.21 STATUS has 10 registers: registers 8 and
 * 9 count the CAN frames the IOmcu refused to decode; the panel asks
 * an older IOmcu for the 8 it has (LINK_ST_COUNT_V420).  From 4.22
 * UPDATE_CTL and UPDATE_DATA take a new IOmcu image over the link;
 * an older IOmcu refuses both with LINK_NACK_BAD_PAGE, which the panel
 * answers by saying a USB (Universal Serial Bus) load is needed.  From
 * 4.23 HITEC (0x34) runs raw Hitec wire transactions on a programming
 * port; an older IOmcu refuses it with LINK_NACK_BAD_PAGE.  From 4.24
 * HITEC takes the receive mode of a D series line in write register 23
 * (LINK_HT_W_RX_MODE); a 4.23 IOmcu refuses that write with
 * LINK_NACK_READ_ONLY and receives with the pull-up, which the panel says
 * on SETUP when the mode asked is PRECHARGE.  From 4.25 SENSE_MAP (0x35)
 * sets up to 4 INA3221 with a shunt and a watch target per channel, and
 * SENSE_WIN_A and SENSE_WIN_B (0x37, 0x38) carry every channel's last
 * window; an older IOmcu refuses the three with LINK_NACK_BAD_PAGE and
 * reads SENSE's one INA3221.  From 4.26 PROG has 28 registers: registers
 * 24 to 27 are each port's FUNC, the function its pin carries while no
 * wire page holds it; the panel reads the 24 an older IOmcu has and offers
 * a port SERVO PWM only.  From 4.27 SENSE_LIMIT (0x36) holds a
 * current limit per channel and flags each channel whose window passed
 * its limit; an older IOmcu refuses it with LINK_NACK_BAD_PAGE.
 * From 4.28 FUNC takes DSHOT150, DSHOT300 and DSHOT600 (plain DShot,
 * the port's channel a throttle) and FLAGS bits 12 to 15 say a port's
 * DShot is bound; an older IOmcu refuses those values with
 * LINK_NACK_BAD_VALUE, and the panel offers them from 4.28 only.  From
 * 4.29 the HITEC page holds SUPPLY (FLAGS bit 11, LINK_SP_PORT) from the
 * OPEN that takes a port to the CLOSE that gives it back; an older IOmcu
 * holds it only while a POWER runs, and the panel reads the bit either way.
 * From 4.30 the KST page takes LINK_KS_W_CMD_LEAVE on a REBOOT from
 * PROGRAMMING: the cycle ends in LEAVING for LINK_KS_W_LEAVE_MS before PWM.
 * An older IOmcu refuses the bit with LINK_NACK_BAD_VALUE; the panel sets it
 * from 4.30 only and sends an older IOmcu the REBOOT without the wait.
 * From 4.31 SUPPLY holds an OFF the panel writes while a programming port
 * holds it back from the module while the port writes to the servo's
 * memory, SUPPLY_PAGE_WRITE_WAIT_MS (10000ms) at most, and refuses the
 * port's own ON after it until the port lets go of SUPPLY; HITEC takes
 * LINK_HT_W_CMD_MORE.  An older IOmcu switches the module off at once and
 * refuses the bit with LINK_NACK_BAD_VALUE; the panel switches a held
 * supply off on an error from 4.31 only, and sets the bit from 4.31 only.
 * From 4.32 PROG_SUPPLY (0x39) switches the programming ports' own supply
 * on a board that has one; an older IOmcu refuses it with
 * LINK_NACK_BAD_PAGE.
 * An IOmcu never asks the panel's minor; a page an older panel does not
 * know is a page it never writes.
 *
 * Frozen across majors.  Registers 0 to 6 of IDENTITY (LINK_ID_*) and the
 * two update pages keep their numbers, their layout and their meaning under
 * every later major, and an IOmcu of any later major answers them.  A
 * panel reads them before it compares majors, so a panel and an IOmcu
 * whose majors differ still find out what the other runs and can still move
 * the IOmcu to the panel's own image.  Changing one of them is not a
 * major change; it is a new page.
 */
#define LINK_PROTOCOL_MAJOR 4u
/** @brief The minor this build speaks: 33, SENSE_MAP's WATCH values of a
 *         board's soldered INA3221. */
#define LINK_PROTOCOL_MINOR 34u

/** @brief The first minor that serves BIND_CFG, BIND_OUT and BIND. */
#define LINK_MINOR_BIND 10u
/**
 * @brief The first minor that serves SERVO_WIN, counts resets in SENSE register
 * 31 and takes a negative CAP_HOLD_MA.
 */
#define LINK_MINOR_SERVO_WIN 11u
/** @brief The first minor that serves KST. */
#define LINK_MINOR_KST 12u
/**
 * @brief The first minor that serves PROG, names a programming port in a KST
 * command and in CAP_ARM, and says in SUPPLY FLAGS that a port holds the
 * supply.
 */
#define LINK_MINOR_PROG 13u
/**
 * @brief The first minor whose KST page plans a WRITE and a RESTORE over
 * several passes: LINK_KST_RESULT_PLANNING while it does,
 * LINK_KST_RESULT_NO_PATH and LINK_KST_RESULT_TOO_COMPLEX as results.
 *
 * Minor 13 plans inside the command's frame and refuses with
 * LINK_KST_REF_PLAN_NO_PATH.
 */
#define LINK_MINOR_KST_PLAN 14u
/**
 * @brief The first minor whose KST page takes LINK_KST_OP_LEAVE and the
 * register LINK_KS_W_LEAVE_MS: PROGRAMMING back to PWM with the supply
 * untouched.
 *
 * Minor 14 refuses the operation with LINK_NACK_BAD_VALUE and the register with
 * LINK_NACK_READ_ONLY.
 */
#define LINK_MINOR_KST_LEAVE 15u
/**
 * @brief The first minor that refuses a pulse longer than its frame with
 * LINK_NACK_BAD_VALUE: an endpoint above out_pwm_pulse_max() of the rate the
 * PWM output runs at, on CHAN_CFG, OUTPUTS, BIND, SERVO and PROG, and a stored
 * slot with one starts unbound.
 *
 * Minor 15 takes the write and clamps the pulse to the frame's last count,
 * while the page reads back the endpoint written.
 */
#define LINK_MINOR_PULSE_FRAME 16u
/**
 * @brief The first minor whose TONE page refuses a tap on a busy pin with
 * LINK_NACK_BUSY: a pin SUPPLY, SENSE, PROG or an output holds, or a capture
 * the wiring cannot start for now.
 *
 * BAD_VALUE is left for a set-up no freed pin makes good.  Minor 16 and older
 * refuse both with LINK_NACK_BAD_VALUE.
 */
#define LINK_MINOR_TONE_BUSY 17u
/**
 * @brief The first minor whose CONTROL ARM arms only the roles it names:
 * LINK_ARM_THROTTLE, LINK_ARM_SURFACE or both.
 *
 * It refuses any other non-zero value but LINK_ARM_ALL with
 * LINK_NACK_BAD_VALUE.  An older minor arms every output on any non-zero ARM.
 */
#define LINK_MINOR_ARM_ROLE 18u
/**
 * @brief The first minor whose SERVO rate is the tester's target's alone: the
 * surfaces of the standard servo ports with them as PROG's TARGET, else the
 * TARGET port's pin, the slots at their own rates and the other ports at 50Hz.
 *
 * A TARGET change returns the rate to 0.  An older minor runs every PWM surface
 * and every programming port at the one SERVO rate, whatever the TARGET.
 */
#define LINK_MINOR_SERVO_TARGET_RATE 19u
/**
 * @brief The first minor whose ARTWORK page serves the board's drawing
 * (LINK_ART_DRAWING) and the hash of its payload in LINK_AW_HASH_LO and
 * LINK_AW_HASH_HI.
 *
 * An older minor serves a photograph (LINK_ART_RGB565) in seven registers,
 * which this panel does not read.
 */
#define LINK_MINOR_DRAWING 20u
/**
 * @brief The first minor whose STATUS page counts the CAN frames it refused to
 * decode, in LINK_ST_REFUSED_LO and LINK_ST_REFUSED_HI.
 *
 * An older minor's page has LINK_ST_COUNT_V420 registers and refuses a longer
 * read with LINK_NACK_BAD_RANGE.
 */
#define LINK_MINOR_REFUSED 21u
/**
 * @brief The first minor that serves UPDATE_CTL and UPDATE_DATA.
 *
 * An older minor refuses both with LINK_NACK_BAD_PAGE; it takes a new image
 * over USB only.
 */
#define LINK_MINOR_UPDATE 22u
/**
 * @brief The first minor that serves HITEC.
 *
 * An older minor refuses it with LINK_NACK_BAD_PAGE, and its KST page knows no
 * other holder of the programming port.
 */
#define LINK_MINOR_HITEC 23u
/**
 * @brief The first minor whose HITEC page takes LINK_HT_W_RX_MODE.
 *
 * Minor 23 refuses the write with LINK_NACK_READ_ONLY and receives with the
 * pull-up.
 */
#define LINK_MINOR_HITEC_RX 24u
/**
 * @brief The first minor that serves SENSE_MAP, SENSE_WIN_A and SENSE_WIN_B: up
 * to 4 INA3221 parts, 12 channels.
 *
 * An older minor refuses the three pages with LINK_NACK_BAD_PAGE and reads one
 * INA3221 from SENSE registers 8 to 10.
 */
#define LINK_MINOR_SENSE_MAP 25u
/**
 * @brief The first minor that serves SENSE_LIMIT.
 *
 * An older minor refuses the page with LINK_NACK_BAD_PAGE and checks no limit.
 */
#define LINK_MINOR_SENSE_LIMIT 27u

/**
 * @brief The first minor whose PROG page has a FUNC register per port
 * (LINK_PG_FUNC0, LINK_PG_REGS 28) and the parked flag in FLAGS.
 *
 * An older minor's page has LINK_PG_REGS_V424 registers, refuses a longer read
 * with LINK_NACK_BAD_RANGE and runs every port's pin as SERVO PWM.
 */
#define LINK_MINOR_PROG_FUNC 26u

/**
 * @brief The first minor whose PROG page takes FUNC DSHOT150, DSHOT300 and
 * DSHOT600 (LINK_PG_FN_DSHOT150 on) and sets LINK_PG_F_DSHOT in FLAGS.
 *
 * An older minor refuses those values with LINK_NACK_BAD_VALUE.  4.27 is
 * SENSE_LIMIT's.
 */
#define LINK_MINOR_PROG_DSHOT 28u

/**
 * @brief The first minor whose HITEC page holds SUPPLY for its whole session:
 * LINK_SP_PORT is set from the OPEN that takes a port to the CLOSE that gives
 * it back, a stopped port included.
 *
 * An older minor holds SUPPLY from a POWER's OFF to the end of its listen
 * only, and takes the panel's set points between two POWERs.
 */
#define LINK_MINOR_HITEC_HOLD 29u

/**
 * @brief The first minor whose KST page takes LINK_KS_W_CMD_LEAVE: a REBOOT
 * from PROGRAMMING that ends in LEAVING, the line low for LINK_KS_W_LEAVE_MS
 * after the cycle's KST_PORT_RAIL_WAIT_MS, then PWM.
 *
 * An older minor refuses a command with the bit with LINK_NACK_BAD_VALUE; its
 * REBOOT returns the port to PWM at the end of the cycle.
 */
#define LINK_MINOR_KST_CYCLE_WAIT 30u

/**
 * @brief The first minor whose SUPPLY page holds the panel's OFF of a supply
 * a programming port holds back from the module while the port writes to the
 * servo's memory, SUPPLY_PAGE_WRITE_WAIT_MS at most, and refuses the port's
 * ON after that OFF until the port lets go; and whose HITEC page takes
 * LINK_HT_W_CMD_MORE.
 *
 * An older minor switches the module off at the panel's OFF, whatever the
 * port does, takes the port's ON after it, and refuses a HITEC command with
 * the bit with LINK_NACK_BAD_VALUE.
 */
#define LINK_MINOR_PORT_CUT 31u

/**
 * @brief The first minor with the PROG_SUPPLY page (LINK_PAGE_PROG_SUPPLY):
 * the switch of the programming ports' own supply on a board that has one
 * (the rbmod carrier's GPIO 21, the enable of its 5V buck M7).
 *
 * An older minor refuses the page with LINK_NACK_BAD_PAGE; the panel reads
 * and writes it from 4.32 only.
 */
#define LINK_MINOR_PROG_SUPPLY 32u

/**
 * @brief The first minor whose SENSE_MAP page takes the WATCH values of a
 * board's own supply rails: LINK_SM_WATCH_RAIL_A, LINK_SM_WATCH_RAIL_B and
 * LINK_SM_WATCH_PROG_5V, the three channels the rbmod carrier solders.  On
 * that carrier the page refuses any other value for them, and a map without
 * the carrier's INA3221 (board_fix.h).
 *
 * An older minor refuses a map with those values with LINK_NACK_BAD_VALUE.
 */
#define LINK_MINOR_SENSE_RAILS 33u

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

/** @brief A command runs 0..LINK_CH_SPAN across the channel's own travel. */
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

/**
 * @brief The fastest slew a write may name, in thousandths of the span a
 * second: 3000, 300%/s, the panel's own maximum.
 *
 * A higher value is refused with BAD_VALUE.
 */
#define LINK_CC_SLEW_MAX 3000u

#define LINK_CC_ROLE_THROTTLE 0u
#define LINK_CC_ROLE_SURFACE  1u

/**
 * @brief A channel's range before anything is written, and the widest range the
 * IOmcu accepts for the limits themselves.
 *
 * The floor is a 760us tail servo's: its travel ends at 410us, and a standard
 * servo asked for that buzzes rather than moves.
 *
 * The limits are enforced at the IOmcu, the end holding the wire, so a
 * restarted, reflashed or wrong host cannot drive a servo into its stops.
 *
 * A PWM output's endpoints also fit its frame (since 4.16): neither longer
 * than out_pwm_pulse_max() of the rate the output runs at, one microsecond
 * under the period -- 1784us at 560Hz, 2499us at 400Hz.  Below 400Hz
 * the ceiling is the shorter of the two.  A write that would leave a
 * longer one is refused with BAD_VALUE (outputs_pulse_check()).
 */
#define LINK_CC_DEFAULT_MIN 1000u
#define LINK_CC_DEFAULT_MAX 2000u
#define LINK_CC_FLOOR_US     400u
#define LINK_CC_CEILING_US  2500u

/**
 * @brief The least distance, in microseconds, between a throttle's Idle pulse
 * (CHAN_CFG MIN_US) and its Full pulse (MAX_US).  The panel's keypad for
 * either pulse, the panel's payment of the pair to the IOmcu
 * (outputs_chan_cfg_set_throttle_range()) and the order check ARM waits on
 * hold a pair to it.  The IOmcu's own write check holds each pulse to the
 * floor and ceiling above and straightens an inverted pair
 * (outputs_set_endpoints()); it does not read this gap.
 */
#define LINK_CC_THROTTLE_GAP_US 100u

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
 *     moved some surfaces to 560Hz and not their slice-mates would leave a
 *     slice asked for two rates.  A rate that would do that against the
 *     binding in force -- a surface and a throttle on one slice -- is
 *     refused with BAD_VALUE and nothing changes.  With a programming port
 *     as target (since 4.19) it is that port's pin alone, 0 meaning 50Hz;
 *     the slots run at their own rates.  A port pin that shares its PWM
 *     slice with another port's pin takes only 0 or 50 (BAD_VALUE).  Every
 *     port that is not the target runs at 50Hz, and a TARGET change sets
 *     FRAME_HZ to 0.  An IOmcu before 4.19 runs every surface and
 *     every port at FRAME_HZ, whatever the target.
 *
 *     SWEEP to SWEEP_DWELL_MS are a sweep (servo_sweep.h), four registers so
 *     a start fits one frame.  SWEEP is the curve, 0 stopped, 1 square,
 *     2 sine, 3 triangle; MHZ thousandths of a cycle a second, 50 to 5000;
 *     SPAN command units either side of the centre, 0 to 500; DWELL_MS the
 *     hold at each end, 0 to 5000.  A sweep needs the bench armed
 *     (NOT_ARMED) and stops after 500ms unwritten, on a disarm, or after
 *     SWEEP_MOVES ends (0 for no end), read back in SWEEP_DONE.
 *
 *     SWEEP = 4 (LINK_SV_HOLD), one register, holds every surface where
 *     its output is -- a sweep stopped part way, exactly where it got to,
 *     slew and all -- for as long as it is written, on the same 500ms and
 *     disarm rules.  A sweep that was running when the hold began keeps its
 *     phase: how far into the curve it was, so its point, its dwell and its
 *     movements reached.  One that had made its movements is held at the
 *     centre it ended on.  A write is judged against the page as a pass at
 *     the same moment leaves it: a sweep past its last movement, unwritten
 *     for 500ms or disarmed has ended, even before the pass that ends it.
 *
 *     SWEEP = 5 (LINK_SV_RESUME, since 4.6), one register, carries that
 *     sweep on from the kept phase.  The surfaces are commanded along the
 *     curve again at once and slew there from where they were held, at
 *     their own rate.  Refused with BAD_VALUE when no phase is kept -- no
 *     sweep was running when the hold began, or the hold has ended by a
 *     write of 0, a curve written over it, a disarm, 500ms unwritten or
 *     a restart -- and when
 *     SWEEP_MHZ, SPAN, DWELL_MS or MOVES no longer read what the paused
 *     sweep runs; NOT_ARMED on a disarmed bench.  A 4.5 IOmcu refuses
 *     5 with BAD_VALUE, as any curve not written whole.
 *
 *     Not kept: an IOmcu restart drives every slot at its own rate
 *     again, which is the binding's 50Hz for a servo, and runs no sweep. */
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
/** @brief SWEEP's value for holding the surfaces where their outputs are. */
#define LINK_SV_HOLD 4u
/** @brief SWEEP's value for carrying a held sweep on from its phase (4.6). */
#define LINK_SV_RESUME 5u

/* --- the SUPPLY page (protocol 4.3): a WeAct PD Power Mini V1 Buck on a
 *     PIO UART the IOmcu runs on two of its pins (shared/bench/
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
 *     1.2s.  That read showing the output off takes it; on, or failed,
 *     refuses it: bit 8 clears and bit 9 sets until the next wiring write.
 *     Such a change comes alone, without OUTPUT to SET_MA, and an ON is
 *     refused with BAD_VALUE while one waits.
 *
 *     OUTPUT to SET_MA are what it is to do, one frame: 1 for the output
 *     on, the set points in mV and mA, clamped to the module's 1 to 20V
 *     and 0.05 to 3A.  An ON is refused with NOT_ARMED without a live
 *     heartbeat, and the output goes off when the heartbeat stops.
 *
 *     Protocol 4.31: an OFF written while a programming port holds the
 *     page (bit 11) and writes to the servo's memory reads 0 at once and
 *     reaches the module when the write ends, 10000ms after the OFF at the
 *     latest; the heartbeat lost or the link silent switches it off at
 *     once.  From such an OFF until the port lets go of the page, the
 *     port's own ON is refused.
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
 *     the set point and 500mV on 2 input reads in a row, held off until
 *     OUTPUT is written 0 (4.5);
 *     the output's voltage and current, the set points read back from it,
 *     the input's state and voltage, and the counts of readings taken and
 *     of transactions that failed, modulo 65536.
 *
 *     The wiring is kept in the IOmcu's flash and driven at boot with
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
/**
 * @brief Protocol 4.13: a programming port holds OUTPUT, SET_MV and SET_MA.
 *
 * A write of them is refused unless it leaves OUTPUT 0 and SET_MV no higher.
 */
#define LINK_SP_PORT         0x800u

/* --- the PROG_SUPPLY page (protocol 4.32): the programming ports' own
 *     supply, on a board that switches one with a pin of the IOmcu (the
 *     rbmod carrier: GPIO 21, the enable of its 5V buck M7).  It follows the
 *     SUPPLY page's rules for a programming port:
 *
 *     OUTPUT  1 switches the supply on, 0 off.  ON is refused with
 *             LINK_NACK_NOT_ARMED without a live heartbeat, and with
 *             LINK_NACK_BAD_VALUE on a board without the switch and while a
 *             programming port holds the supply (FLAGS bit 2).  OFF is
 *             always taken; while a port writes to the servo's memory it is
 *             held back from the pin until the write ends,
 *             SUPPLY_PAGE_WRITE_WAIT_MS at most (FLAGS bit 3), and the
 *             port's own ON is refused after it until the port lets go.
 *             The IOmcu switches the supply off on its own when the
 *             heartbeat or the link goes, and at start.  An OFF waits for
 *             the end of a frame a port has on the wire,
 *             SUPPLY_PAGE_OFF_WAIT_MS at most.  Not kept.
 *     FLAGS   read only: bit 0 the board has the switch, bit 1 the pin
 *             drives the supply on, bit 2 a programming port holds it,
 *             bit 3 the panel's OFF waits for a port's write.
 *     PIN     read only: the GPIO that switches it, LINK_PS_NO_PIN without.
 *     MV      read only: the voltage it gives, in mV, 0 without.
 *     PANEL   1 written alone: the panel knows the page.  Refused with
 *             LINK_NACK_NOT_ARMED without a live heartbeat, LINK_NACK_BAD_VALUE
 *             for any other value.  A programming port gets the supply only
 *             while it reads 1; the heartbeat lost sets it 0.  A panel
 *             before 4.32 never writes it, so its ports never get the
 *             supply. */
enum {
    LINK_PS_OUTPUT = 0,
    LINK_PS_FLAGS  = 1,   /**< read only from here to MV */
    LINK_PS_PIN    = 2,
    LINK_PS_MV     = 3,
    LINK_PS_PANEL  = 4,
    LINK_PS_COUNT  = 5,
};
#define LINK_PS_FITTED 0x01u  /**< the board has the switch               */
#define LINK_PS_ON     0x02u  /**< the pin drives the supply on           */
#define LINK_PS_PORT   0x04u  /**< a programming port holds it            */
#define LINK_PS_HELD   0x08u  /**< the panel's OFF waits for a port write */
#define LINK_PS_NO_PIN 0xFFu  /**< PIN of a board without the switch      */

/* The current monitors' pages (protocol 4.7 to 4.27). */
#include "link_pages_sense.h"
#include "link_pages_esc.h"

/* --- the TONE page (protocol 4.8): the beeps of an ESC (electronic speed
 *     controller), heard on one motor phase.  One GPIO reads the phase
 *     through a series resistor and a zener clamp; a PIO (programmable
 *     input/output) state machine stamps its edges at 26.7ns, and
 *     protocols/phase_tap/tone.c turns them into beeps and their pitch.
 *
 *     ENABLE to F_MAX_HZ are one frame and SPLIT_PCT to register 7 the
 *     next.  ENABLE bit 0 runs the tap.  PIN is the GPIO.  F_MIN_HZ and
 *     F_MAX_HZ bound the tones heard: 50 to 2000Hz, and F_MIN_HZ + 1 to
 *     6900Hz (EVT_FREQ_DHZ holds 0.1Hz in 16 bits, to 6553.5Hz).
 *     SPLIT_PCT is how far a pitch moves, in percent, to start a new beep
 *     without a silence: 0 splits on silence only, 50 at most.  GAP_MS is
 *     the silence that ends a beep, 1 to 100ms and at least the period of
 *     F_MIN_HZ.  MIN_PERIODS is how many tone periods make a beep, 1 to
 *     64.  Register 7 reads 0 and takes only 0.
 *
 *     Refused with BAD_VALUE: a value out of its range, a combination the
 *     detector refuses (tone_init()), and, while the tap is enabled, a pin
 *     past the bank, one the board reserves, or an ADC (analog to digital
 *     converter) pin: GP26 to GP29 on the RP2350A and GP40 to GP47 on the
 *     RP2354B, rated IOVDD + 0.5V and not fault tolerant, and any pin on
 *     an IOmcu whose core 1 did not start at boot: no capture runs
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
 *     detector's 8ms windows counted modulo 65536; WIN_FREQ_DHZ, the last
 *     window's tone in 0.1Hz, 0 with no tone; WIN_PERIODS, the periods in
 *     it.
 *
 *     The IOmcu keeps the last LINK_TN_RING beeps and numbers them
 *     from 1 to 65535, then from 1 again.  BEEP_HEAD is the newest number,
 *     0 before the first beep.  EVT_SEL names the beep that EVT_SEQ to
 *     EVT_FLAGS show.  It is the one writable register after register 7,
 *     it is not kept, and a read does not consume a beep, so a reply lost
 *     on the link loses nothing.  EVT_SEQ equals EVT_SEL while that beep is
 *     in the ring; when it has left the ring, or has not happened yet,
 *     EVT_SEQ and registers 15 to 21 read 0.  EVT_START_MS is the beep's
 *     first rise in ms since the capture started (ENABLE written 1, or PIN
 *     changed while enabled), 32 bit, low register first (49.7 days);
 *     EVT_LEN_DMS its length to its last edge in 0.1ms, to 6553.5ms;
 *     EVT_FREQ_DHZ its mean pitch in 0.1Hz; EVT_BURSTS its bursts, to
 *     65535; EVT_CARRIER_HHZ the carrier it was chopped at in 100Hz
 *     steps, 0 unchopped.  A length, pitch, burst count or carrier past its
 *     register reads as the largest it holds.  EVT_FLAGS bit 0: the beep
 *     began at a pitch change with no silence before it; bit 1: it ended
 *     at one.
 *     LOST counts beeps dropped, modulo 65536: the detector's queue full,
 *     or the hand-over between the cores full.  GLITCHES counts lows the
 *     detector ignored as shorter than 500ns, modulo 65536.  The
 *     capture's 8us hold-off removes every low shorter than 8us before
 *     the detector sees it, so the register reads 0 on the tap.
 *
 *     The capture starts when the tap is enabled or its pin changes, and
 *     then empties the ring; the numbers go on.  Registers 0 to 6 are kept
 *     in the IOmcu's flash beside the SENSE set-up, and the tap starts
 *     at boot.  Nothing else is kept.
 *
 *     While the tap is disabled the pin is an input with its pull-down on,
 *     so a wire connected to nothing reads low.  The panel's read at
 *     20Hz is registers 8 to 23: a request and 4 data frames. */
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
    LINK_TN_EVT_LEN_DMS  = 17,  /**< 0.1ms                               */
    LINK_TN_EVT_FREQ_DHZ = 18,  /**< 0.1Hz                               */
    LINK_TN_EVT_BURSTS   = 19,
    LINK_TN_EVT_CARRIER_HHZ = 20, /**< 100Hz steps; 0 unchopped          */
    LINK_TN_EVT_FLAGS    = 21,
    LINK_TN_LOST         = 22,
    LINK_TN_GLITCHES     = 23,
    LINK_TN_COUNT        = 24,
};
/**
 * @brief The set-up, ENABLE to register 6: what a write may change, and what
 * flash keeps.
 */
#define LINK_TN_CONFIG_COUNT 7u

/** @brief ENABLE's bit. */
#define LINK_TN_EN_TAP   0x01u

/** @brief The ranges a write is held to. */
#define LINK_TN_F_MIN_LO       50u
#define LINK_TN_F_MIN_HI     2000u
#define LINK_TN_F_MAX_HI     6900u
#define LINK_TN_SPLIT_MAX      50u
#define LINK_TN_GAP_MS_MIN      1u
#define LINK_TN_GAP_MS_MAX    100u
#define LINK_TN_PERIODS_MIN     1u
#define LINK_TN_PERIODS_MAX    64u
/** @brief Beeps the IOmcu keeps. */
#define LINK_TN_RING           64u

/**
 * @brief The set-up before anything is written: the tap off, on GP22 (pad 29);
 * 400 to 6500Hz, 8% split, 3ms gap, 3 periods a beep.
 */
#define LINK_TN_DEFAULT_PIN        22u
#define LINK_TN_DEFAULT_F_MIN     400u
#define LINK_TN_DEFAULT_F_MAX    6500u
#define LINK_TN_DEFAULT_SPLIT       8u
#define LINK_TN_DEFAULT_GAP_MS      3u
#define LINK_TN_DEFAULT_PERIODS     3u

/** @brief What the IOmcu says about the tap. */
typedef enum {
    /** @brief The PIO state machine and its DMA ring run on the pin. */
    LINK_TN_RUNNING     = 1u << 0,
    /**
     * @brief The tap is enabled and does not run: the pin is not free (a kept
     * set-up met a binding or a reservation at boot) or no PIO state machine
     * could take it.
     */
    LINK_TN_PIN_REFUSED = 1u << 1,
    /**
     * @brief The capture ring or the state machine's FIFO was overrun since the
     * capture started; the beep under way was cut at its last edge.
     *
     * Cleared when the capture starts again.
     */
    LINK_TN_OVERRUN     = 1u << 2,
    /** @brief A run of bursts is under way. */
    LINK_TN_BEEP        = 1u << 3,
    /** @brief The last window held a tone. */
    LINK_TN_TONE        = 1u << 4,
} link_tone_flag_t;

/** @brief EVT_FLAGS' bits, tone_beep_flag_t's. */
#define LINK_TN_EVT_AFTER_CHANGE  0x01u
#define LINK_TN_EVT_BEFORE_CHANGE 0x02u

#define LINK_OS_RANGE_OF(first, count) \
    ((uint16_t)((((unsigned)(first) & 0xFFu) << 8) | ((unsigned)(count) & 0xFFu)))
#define LINK_OS_FIRST(range) ((uint8_t)((range) >> 8))
#define LINK_OS_CHANNELS(range) ((uint8_t)((range) & 0xFFu))

/**
 * @brief The drivers, numbered on the wire.
 *
 * A contract like a page number: nothing is renumbered, and a new protocol
 * appends.
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

/**
 * @brief A binding taken whole (protocol 4.10).
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
 * IOmcu compares it with the CRC of what it holds prepared and refuses
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
/** @brief Registers of the IDENTITY page. */
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

/**
 * @brief What the IOmcu has fitted, as opposed to what the panel has screens
 * for.
 *
 * The menu marks derive from this bitmap, so a mark disappears when the part is
 * fitted.
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
    LINK_CAP_PROGRAM     = 1u << 8, /**< ESC_WIRE, ESC_DATA (4.34)         */
} link_cap_t;

/* ------------------------------------------------------------------ status */
/** @brief Registers of the STATUS page. */
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
     * @brief Protocol 4.21: CAN frames received and refused since power-up, low
     * half first, modulo 2^32.
     *
     * A frame is refused when its receive buffer holds no frame the link
     * carries (an 11-bit identifier, a remote frame, a DLC above 8:
     * mcp2515_decode_rx()) or when link_can_decode() refuses it (an unknown op,
     * a count past a page, a DLC that disagrees with the count).  Self-test
     * frames are answered, not refused.  0 at power-up and at every restart;
     * nothing clears it over the link.
     */
    LINK_ST_REFUSED_LO = 8,
    LINK_ST_REFUSED_HI = 9,
    LINK_ST_COUNT      = 10,
};
/** @brief The registers a 4.20 or older IOmcu's STATUS page has. */
#define LINK_ST_COUNT_V420 8u

/**
 * @brief What the IOmcu did on its own authority.
 *
 * Sticky until read and explicitly cleared: a fault that lasted 4ms is still
 * the reason the motor stopped.
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
     * @brief The IOmcu's flash store is off for this boot (protocol 4.7): its
     * second core did not register for the flash lock-out, so no erase or
     * program may run.
     *
     * Set-ups written over the link are taken and run from RAM, and are gone at
     * the next restart.  Set while it lasts, which is until a restart.
     */
    LINK_FAULT_STORE_OFF     = 1u << 6,
} link_fault_t;

/**
 * @brief The states of the IOmcu's link end.
 */
typedef enum {
    LINK_STATE_IDLE     = 0, /**< alive, outputs off            */
    LINK_STATE_ARMED    = 1, /**< outputs live                  */
    LINK_STATE_FAILSAFE = 2, /**< acted on its own authority    */
} link_dev_state_t;

/* ------------------------------------------------------------------- bench */
/**
 * @brief The numbers, read-only.
 *
 * Fixed-point rather than floats: a register is 16 bits, and the scale is
 * part of the contract between the two firmwares.
 *
 * The peaks are tracked here rather than on the panel: the IOmcu has
 * the fast samples and the panel sees one poll in fifty of them.
 */
enum {
    LINK_BN_VOLTAGE_CV   = 0,  /**< 10mV steps, 0..655.35V                */
    LINK_BN_CURRENT_CA   = 1,  /**< 10mA steps, 0..655.35A                */
    LINK_BN_POWER_W      = 2,  /**< watts                                  */
    LINK_BN_RPM          = 3,
    LINK_BN_TEMP_ESC_DC  = 4,  /**< 0.1C, signed -- read as int16_t        */
    LINK_BN_TEMP_MOT_DC  = 5,  /**< 0.1C, signed                           */
    LINK_BN_CHARGE_MAH   = 6,  /**< accumulated by the current monitor     */
    LINK_BN_ENERGY_DWH   = 7,  /**< 0.1Wh                                  */
    LINK_BN_VOLT_MIN_CV  = 8,  /**< sag: the lowest the bus went           */
    LINK_BN_CURR_MAX_CA  = 9,
    LINK_BN_POWER_MAX_W  = 10,
    LINK_BN_RPM_MAX      = 11,
    LINK_BN_FLAGS        = 12, /**< a bitmap of link_bench_flag_t          */
    LINK_BN_COUNT        = 13,
};

/** @brief What the IOmcu says about the numbers it is sending. */
typedef enum {
    LINK_BN_VOLTAGE_OK = 1u << 0, /**< a sensor answered; else the field is 0 */
    LINK_BN_CURRENT_OK = 1u << 1,
    LINK_BN_RPM_OK     = 1u << 2,
    /**
     * @brief The ESC's own temperature.
     *
     * Its meaning changed at protocol 3.0: before that it validated both
     * temperature fields, which is why the change is a major and not an
     * added bit.  An IOmcu that sets it and leaves temp_motor at zero
     * would have told a 2.x panel that a motor it cannot measure is at 0C.
     */
    LINK_BN_TEMP_OK    = 1u << 3,
    /**
     * @brief The motor's temperature, which is a different sensor.
     *
     * Separate because the two arrive from different places and one of them
     * usually does not arrive at all: an ESC reports its own temperature over
     * extended DShot telemetry and knows nothing about the motor it is
     * driving.  One flag for both would show a motor at 0C whenever an ESC
     * reported its own.
     */
    LINK_BN_TEMP_MOT_OK = 1u << 4,
    /**
     * @brief Voltage, current and power are the INA228's (protocol 4.7), not
     * the ESC's telemetry, which the SENSE page then carries.
     */
    LINK_BN_SENSED     = 1u << 5,
    /**
     * @brief CHARGE_MAH and ENERGY_DWH are the INA228's accumulators, cleared
     * at this run's arm, with the part answering throughout (protocol 4.7).
     *
     * Clear, the two registers count nothing the panel can use, and it counts
     * its own from the current.
     */
    LINK_BN_TOTALS_OK  = 1u << 6,
    /**
     * @brief The numbers are modelled, not measured.
     *
     * Set by an IOmcu running without a front end and by the panel's own
     * simulator; the panel draws SIMULATION across the screen either way.
     */
    LINK_BN_SIMULATED  = 1u << 7,
} link_bench_flag_t;

/* --------------------------------------------------------------- catalogue */

/**
 * @brief The board's own pins: which GPIOs it brings out, the pad number
 * printed beside each, and what already holds the ones an output may not have.
 *
 * A board describes itself so a panel that has never heard of it can still
 * offer the right pins.  What it cannot do is make a pin safe: the
 * IOmcu reserves its own set at its own end whatever it says here, so
 * a page that lies costs a pin rather than the safety line.
 *
 * One register per pin and no count register.  A count would have to live in
 * the identity page, and lengthening that page breaks every IOmcu
 * built before it: the panel asks for LINK_ID_COUNT registers, and a device
 * whose identity page is shorter refuses the read rather than returning what
 * it has -- which would leave the link down instead of degraded.  Instead a
 * pad number of zero means there is no pin in that slot, since pads are
 * numbered from one.
 *
 * An IOmcu built before this page answers NACK with LINK_NACK_BAD_PAGE,
 * which is the panel's cue to use the catalogue compiled into it.
 */
#define LINK_CAT_PINS  32u
#define LINK_CAT_COUNT LINK_CAT_PINS

/** @brief gpio in 6 bits, pad in 6, and what holds it in 4. */
#define LINK_CAT_OF(gpio, pad, hold) \
    ((uint16_t)((((unsigned)(gpio) & 0x3Fu) << 10) \
                | (((unsigned)(pad) & 0x3Fu) << 4) \
                | ((unsigned)(hold) & 0x0Fu)))
#define LINK_CAT_GPIO(r) ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_CAT_PAD(r)  ((uint8_t)(((r) >> 4) & 0x3Fu))
#define LINK_CAT_HOLD(r) ((uint8_t)((r) & 0x0Fu))

/**
 * @brief What holds a pin the bench may not drive.
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

/**
 * @brief Where the pins are, as opposed to which they are.
 *
 * The catalogue page names a pad and the pin on it; it does not say where
 * that pad sits.  A picture of the board is the one place a pin number has
 * to become a position, and a picture drawn from a guessed shape points at
 * the wrong pad with the same confidence as the right one -- so the board
 * says its shape rather than the panel assuming one.
 *
 * An IOmcu that does not serve this page gets no drawn board.  That is
 * a screen the operator does not get, not a bench that does not work: the
 * outputs grid offers the same pins from the catalogue alone.
 *
 * Two rows on one pitch, which is every 0.1-inch header board.  Pad 1 sits
 * at LINK_SH_CORNER and the numbers run away from it along that edge, then
 * back along the opposite one -- the DIP (dual in-line package) convention,
 * and the one the Pico form factor follows.
 *
 * Hundredths of a millimetre throughout: a register is sixteen bits, which
 * reaches 655.35mm, and 0.01mm is finer than any board is placed to.
 */
enum {
    LINK_SH_WIDTH_CMM  = 0, /**< outline, along the pad rows              */
    LINK_SH_HEIGHT_CMM = 1, /**< outline, across them                     */
    LINK_SH_LAYOUT     = 2, /**< (corner << 8) | pads in one row          */
    LINK_SH_PITCH_CMM  = 3, /**< centre to centre, 254 for 0.1 inch       */
    /**
     * @brief Edge to the centre of the pad row, from the outline's own edge.
     *
     * A board drawing (LINK_ART_DRAWING) places its pads on the same grid.
     */
    LINK_SH_INSET_CMM  = 4,
    LINK_SH_COUNT      = 5,
};

#define LINK_SH_LAYOUT_OF(corner, per_side) \
    ((uint16_t)((((unsigned)(corner) & 0xFFu) << 8) \
                | ((unsigned)(per_side) & 0xFFu)))
#define LINK_SH_CORNER(r)   ((uint8_t)(((r) >> 8) & 0xFFu))
#define LINK_SH_PER_SIDE(r) ((uint8_t)((r) & 0xFFu))

/**
 * @brief Which corner of the outline pad 1 sits at, seen with the board as
 * drawn.
 */
typedef enum {
    LINK_SH_BOTTOM_LEFT  = 0,
    LINK_SH_BOTTOM_RIGHT = 1,
    LINK_SH_TOP_LEFT     = 2,
    LINK_SH_TOP_RIGHT    = 3,
} link_shape_corner_t;

/* ----------------------------------------------------------------- artwork */

/**
 * @brief The board's drawing: its outline, its parts as flat shapes, and every
 * pad with what it is, so the picker shows the board in the operator's hands
 * and not only its outline.
 *
 * The IOmcu carries the drawing of its own board; a new board brings its
 * drawing in its own firmware image, and the panel needs no change for it.
 *
 * Two pages because a page is thirty-two registers and no more.  Metadata
 * here, payload next door, and a block stays sixty-two bytes.  The drawing
 * of the RP2350-CAN is under 2kB, at most 33 blocks: at 1Mbit/s a block
 * is a write to say which one and a read to fetch it, about 1.4ms the pair.
 *
 * LINK_AW_BLOCKS of zero means this IOmcu carries no drawing of
 * itself.  That is the ordinary answer, not a fault: the board is then drawn
 * from its shape page, and used from its catalogue when there is no shape.
 *
 * The panel keeps a drawing in its flash under the IOmcu's board ID
 * together with every register of this page from LINK_AW_WIDTH on, and
 * shows the kept one only while all of them still match: an IOmcu
 * flashed with another drawing for the same board ID fetches the new one.
 */
enum {
    LINK_AW_BLOCKS   = 0, /**< blocks of payload; 0 when there is none    */
    LINK_AW_WIDTH    = 1, /**< the outline along the pad rows, 0.01mm     */
    LINK_AW_HEIGHT   = 2, /**< the outline across them, 0.01mm            */
    LINK_AW_FORMAT   = 3, /**< a link_art_format_t                        */
    LINK_AW_BYTES_LO = 4, /**< payload length, low half                   */
    LINK_AW_BYTES_HI = 5,
    /**
     * @brief link_crc() over the whole payload, seeded zero.
     *
     * A block lost and silently skipped would otherwise be cached in flash and
     * shown until somebody wired a lead by it.
     */
    LINK_AW_CRC      = 6,
    /**
     * @brief link_fnv1a32() over the whole payload, low half then high half.
     *
     * The content key of the panel's cache, next to the CRC: two drawings that
     * share a sixteen-bit CRC and a length differ here.
     */
    LINK_AW_HASH_LO  = 7,
    LINK_AW_HASH_HI  = 8,
    LINK_AW_COUNT    = 9,
};

/**
 * @brief The formats an ARTWORK drawing can have.
 */
typedef enum {
    LINK_ART_NONE    = 0,
    /**
     * @brief A photograph, two bytes a pixel.
     *
     * Minor 19 and older serve it; this panel does not read it.
     */
    LINK_ART_RGB565  = 1,
    /** @brief The board drawing laid out below (LINK_DRAW_*). */
    LINK_ART_DRAWING = 2,
} link_art_format_t;

/**
 * @brief The board drawing, LINK_ART_DRAWING.
 *
 * Little-endian, 0.01mm throughout, origin the top-left corner of the outline
 * seen from the component side, x along the pad rows, y across them.  Every pad
 * ring and every shape lies inside the outline; a payload with anything else is
 * refused whole.
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
/**
 * @brief A pad that is a GPIO, the fourth kind next to link_pad_kind_t's three.
 */
#define LINK_DRAW_PAD_GPIO     4u
/** @brief The pad's ring is square: pad 1 of a header, by convention. */
#define LINK_DRAW_PAD_SQUARE   0x01u

/**
 * @brief The kinds of shape record a drawing holds.
 */
typedef enum {
    LINK_DRAW_RECT  = 1,
    LINK_DRAW_RRECT = 2,
    LINK_DRAW_DISC  = 3,
    LINK_DRAW_LINE  = 4,
} link_draw_shape_kind_t;

/**
 * @brief The payload, a block at a time: write LINK_AD_BLOCK to say which, then
 * read the page.
 *
 * Two transactions rather than one because a block that advanced on being read
 * would resend nothing after a reply went missing, and the host would assemble
 * a drawing with a hole in it.
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

/**
 * @brief The pads that are not pins: grounds, rails, and the few that are
 * neither.
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
 * An IOmcu that does not serve this page has its grounds and rails
 * unmarked, which is where the picker started. It is a lead placed by
 * reading the board instead of the screen, not a bench that does not work.
 */
#define LINK_PAD_SLOTS 32u
#define LINK_PAD_COUNT LINK_PAD_SLOTS

/**
 * @brief pad in 6 bits, what it is in 2, and its rail in 8 tenths of a volt.
 */
#define LINK_PAD_OF(pad, kind, dv) \
    ((uint16_t)((((unsigned)(pad) & 0x3Fu) << 10) \
                | (((unsigned)(kind) & 0x03u) << 8) \
                | ((unsigned)(dv) & 0xFFu)))
#define LINK_PAD_NUM(r)  ((uint8_t)(((r) >> 10) & 0x3Fu))
#define LINK_PAD_KIND(r) ((uint8_t)(((r) >> 8) & 0x03u))
#define LINK_PAD_DV(r)   ((uint8_t)((r) & 0xFFu))

/**
 * @brief What a pad that is not a pin is.
 *
 * Tenths of a volt reach 25.5V, which covers every rail a logic board
 * brings to a header. Zero on a power pad means the rail is not a fixed
 * voltage -- an input like VSYS follows whatever feeds it -- and the screen
 * says so rather than printing a number that is only sometimes true.
 */
typedef enum {
    LINK_PAD_NONE   = 0, /**< no pad in this slot; the list ends here      */
    LINK_PAD_GROUND = 1, /**< 0V, and what a servo lead returns through    */
    LINK_PAD_POWER  = 2, /**< a rail, at LINK_PAD_DV tenths of a volt      */
    LINK_PAD_OTHER  = 3, /**< neither: a RUN or an enable                  */
} link_pad_kind_t;

/* --------------------------------------------------------------------- KST */
/**
 * @brief The servo programming page (KST, 0x32, since 4.12): the programming
 * port for KST and Chaservo servos.
 *
 * One programming port (the PROG page) at a time leaves PWM (pulse-width
 * modulation) and carries the servo's programming protocol (protocols/kst).
 * The session runs at the IOmcu: the panel writes an operation and the images
 * it needs, and reads the state, the result and the servo's 32 registers.
 * shared/outputs/kst_port.h holds the rules.
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

/** @brief What a write of the page takes. */
enum {
    LINK_KS_W_CMD = 0,   /**< bits 0-3 link_kst_op_t, bit 5
                              LINK_KS_W_CMD_LEAVE (4.30), bit 6
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
                                   before the port is in PWM, and (4.30) a
                                   REBOOT with LINK_KS_W_CMD_LEAVE after its
                                   cycle, in ms, 0 to
                                   LINK_KST_LEAVE_MS_MAX;
                                   LINK_KST_LEAVE_MS_DEFAULT until written.
                                   Taken as written: a LEAVE with a value
                                   above the range is refused.  Not covered
                                   by the CRC */
    LINK_KS_W_COUNT = 29,
};

/** @brief Registers of the command, from LINK_KS_W_CMD: one frame. */
#define LINK_KS_W_CMD_FRAME 4u
/** @brief The staged registers a command's CRC covers, from LINK_KS_W_IMAGE. */
#define LINK_KS_W_STAGED 21u
/** @brief Registers a servo image takes on the page. */
#define LINK_KS_IMAGE_REGS 16u

/**
 * @brief LINK_KS_W_KEY of an operation that writes to the servo.
 *
 * The panel sends it only while its switch ENABLE WRITE TO SERVO is on.
 */
#define LINK_KST_WRITE_KEY 0x57A5u

#define LINK_KS_STATE_OF(r)   ((uint8_t)((r) & 0x7u))
#define LINK_KS_PORT_OF(r)    ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_OP_OF(r)      ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_KS_FRAMES_OF(r)  ((uint16_t)((r) & 0xFFFu))
#define LINK_KS_W_CMD_CONFIRM 0x0080u
/**
 * @brief Bits 8-11 of the command name a programming port (4.13).
 *
 * Every command but ABORT carries it; one without it names an output channel,
 * which is no programming port, and is refused with LINK_KST_REF_PORT.
 */
#define LINK_KS_W_CMD_PORT    0x0040u
/**
 * @brief Bit 5 of a REBOOT from PROGRAMMING (4.30): the cycle ends in LEAVING.
 *
 * After KST_PORT_RAIL_WAIT_MS with the supply read on, the line stays low and
 * no frame goes out for LINK_KS_W_LEAVE_MS more, as read at the command; then
 * the port is in PWM.  The bit on any other operation is refused with
 * LINK_NACK_BAD_VALUE, a REBOOT with it in PWM with LINK_KST_REF_NOT_PROGRAMMING
 * and one with LINK_KS_W_LEAVE_MS above LINK_KST_LEAVE_MS_MAX with
 * LINK_KST_REF_LEAVE_MS.
 */
#define LINK_KS_W_CMD_LEAVE   0x0020u
#define LINK_KS_W_CMD_OF(op, port) \
    ((uint16_t)((unsigned)(op) | LINK_KS_W_CMD_PORT \
                | (((unsigned)(port) & 0xFu) << 8)))
/**
 * @brief LINK_KS_RESULT's low byte when the supply did not switch in time.
 *
 * Above every kst_ses_t.
 */
#define LINK_KST_RESULT_SUPPLY 32u
/**
 * @brief 4.14: a WRITE or a RESTORE has no order of writes in which every image
 * in between is valid.
 *
 * Nothing was sent.
 */
#define LINK_KST_RESULT_NO_PATH 33u
/**
 * @brief 4.14: the planner spent its work (KST_PLAN_WORK_CAP units) before it
 * had a plan or knew there is none.
 *
 * Nothing was sent.
 */
#define LINK_KST_RESULT_TOO_COMPLEX 34u
/**
 * @brief 4.14: LINK_KS_RESULT's low byte while a WRITE or a RESTORE plans:
 * LINK_KS_F_BUSY is set and no frame has gone out.
 *
 * 1 follows when the plan is there.  Never the result of an operation that has
 * ended.
 */
#define LINK_KST_RESULT_PLANNING 35u

/**
 * @brief 4.15: the longest wait LINK_KS_W_LEAVE_MS takes for a LEAVE, in ms.
 */
#define LINK_KST_LEAVE_MS_MAX 10000u
/**
 * @brief 4.15: the wait of a LEAVE while LINK_KS_W_LEAVE_MS was never written,
 * in ms.
 *
 * The wait a KST servo needs is not measured.
 */
#define LINK_KST_LEAVE_MS_DEFAULT 100u

/**
 * @brief The CRC of @p n page registers, low byte first, seeded 0xFFFF.
 * @param regs the page registers
 * @param n the number of registers
 * @return the CRC
 */
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

/** @brief Where the programming port is. */
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
                                untouched; PWM follows.  4.30: also a REBOOT
                                with LINK_KS_W_CMD_LEAVE after its cycle */
} link_kst_state_t;

/**
 * @brief Bits of the KST flags register.
 */
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

/** @brief The operations of a command. */
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
                                   PROGRAMMING or from PWM; with
                                   LINK_KS_W_CMD_LEAVE (4.30) a wait in
                                   LEAVING follows */
    LINK_KST_OP_LEAVE = 10,   /**< 4.15: from PROGRAMMING back to PWM after
                                   LINK_KS_W_LEAVE_MS with the line low; the
                                   supply is not switched and not read */
    LINK_KST_OP_COUNT
} link_kst_op_t;

/**
 * @brief Why a command that was taken did nothing: the high byte of
 * LINK_KS_RESULT.
 */
typedef enum {
    LINK_KST_REF_NONE = 0,
    LINK_KST_REF_BUSY = 1,       /**< an operation runs or the port moves;
                                      the bank is armed; or (4.23) the HITEC
                                      page holds the port */
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
    LINK_KST_REF_LEAVE_MS = 19,  /**< 4.15: a LEAVE, and (4.30) a REBOOT
                                      with LINK_KS_W_CMD_LEAVE, with
                                      LINK_KS_W_LEAVE_MS above
                                      LINK_KST_LEAVE_MS_MAX */
    LINK_KST_REF_LOCKED = 20,    /**< 4.15: a LEAVE while LINK_KS_F_LOCKED
                                      is set: a write left the servo in
                                      doubt */
    LINK_KST_REF_SUPPLY_LOW = 21, /**< 4.15: an ENTER whose supply voltage,
                                       or the lower one of a PD mini output
                                       that is on, is under
                                       KST_PORT_SUPPLY_MIN_MV */
} link_kst_refusal_t;

/* -------------------------------------------------------------------- PROG */
/**
 * @brief The PROG page (0x33, since 4.13): the programming ports.
 *
 * A programming port is an output of its own kind with its own pin.  It is no
 * slot of the OUTPUTS page and no channel of the CHANNELS page.  While no wire
 * page (KST, HITEC) holds it, its pin carries the port's function, FUNC (from
 * 4.26; SERVO PWM on an older IOmcu): a servo pulse from the PWM (pulse-width
 * modulation) generator, switched by its bit in LINK_PG_PWM, or nothing, the
 * pin parked as an input with its pull-down.  A wire page that holds the port
 * has the pin, never both.  shared/outputs/prog_port.h holds the rules.
 *
 * Registers 0 to 3 are the page's own; LINK_PG_STRIDE registers per port
 * follow from LINK_PG_PORT0, then one FUNC register per port from
 * LINK_PG_FUNC0.  PIN, SLEW, MIN_US, MAX_US and FUNC are kept in flash;
 * PWM, TARGET and CMD are not.
 */
enum {
    LINK_PG_COUNT  = 0,  /**< read only: the ports this build has */
    LINK_PG_FLAGS  = 1,  /**< read only: bit n, port n's pin is bound to
                              the PWM generator; bit 4 + n (from 4.26),
                              port n's pin is parked (FUNC NONE); bit
                              8 + n, port n's pin is refused */
    LINK_PG_PWM    = 2,  /**< bit n: port n renders its pulse while the
                              bank is armed */
    LINK_PG_TARGET = 3,  /**< what the SERVO page's rate, sweep and hold
                              command: 0 the standard servo ports, 1 + n
                              port n; a change sets the SERVO rate to 0 */
    LINK_PG_PORT0  = 4,
    LINK_PG_REGS_V424 = 24, /**< the page of an IOmcu older than 4.26 */
    LINK_PG_FUNC0  = 24, /**< from 4.26: port n's FUNC is LINK_PG_FUNC0 + n,
                              a link_pg_func_t */
    LINK_PG_REGS   = 28,
};
/**
 * @brief Register offsets of the PROG page: per-port fields and the function of
 * each port.
 */
enum {
    LINK_PG_PIN    = 0,  /**< per port: the IOmcu GPIO, or
                              LINK_PG_PIN_NONE */
    LINK_PG_CMD    = 1,  /**< the command, 0 to 1000 */
    LINK_PG_SLEW   = 2,  /**< as LINK_CC_SLEW */
    LINK_PG_MIN_US = 3,  /**< as LINK_CC_MIN_US */
    LINK_PG_MAX_US = 4,  /**< as LINK_CC_MAX_US */
    LINK_PG_STRIDE = 5,
};
/**
 * @brief A port's function: what its pin carries while no wire page holds it.
 *
 * DSHOT150 to DSHOT600 from 4.28 (LINK_MINOR_PROG_DSHOT); an older IOmcu
 * refuses them with LINK_NACK_BAD_VALUE.
 */
typedef enum {
    LINK_PG_FN_SERVO_PWM = 0, /**< the servo pulse; a PWM pin only */
    LINK_PG_FN_NONE      = 1, /**< no driver: an input with its pull-down;
                                   any free header pin */
    LINK_PG_FN_DSHOT150  = 2, /**< plain DShot at 150kbit/s: the port's
                                   channel a throttle, a PIO state machine
                                   of its own; any free header pin */
    LINK_PG_FN_DSHOT300  = 3, /**< the same at 300kbit/s */
    LINK_PG_FN_DSHOT600  = 4, /**< the same at 600kbit/s */
    LINK_PG_FN_COUNT     = 5,
} link_pg_func_t;
/** @brief The functions an IOmcu of 4.26 and 4.27 takes. */
#define LINK_PG_FN_COUNT_V426 2u
/** @brief Whether @p fn is one of the DShot functions. */
#define LINK_PG_FN_IS_DSHOT(fn) \
    ((unsigned)(fn) - (unsigned)LINK_PG_FN_DSHOT150 \
     <= (unsigned)LINK_PG_FN_DSHOT600 - (unsigned)LINK_PG_FN_DSHOT150)
/** @brief A DShot function's bit rate in kbit/s, 0 for another function. */
#define LINK_PG_FN_KBIT(fn) \
    ((unsigned)(fn) == (unsigned)LINK_PG_FN_DSHOT150 ? 150u \
     : (unsigned)(fn) == (unsigned)LINK_PG_FN_DSHOT300 ? 300u \
     : (unsigned)(fn) == (unsigned)LINK_PG_FN_DSHOT600 ? 600u : 0u)
/** @brief Ports the page has room for. */
#define LINK_PG_PORTS_MAX 4u
#define LINK_PG_PIN_NONE  0xFFFFu
#define LINK_PG_REG(port, r) \
    ((uint8_t)(LINK_PG_PORT0 + (unsigned)(port) * LINK_PG_STRIDE + (r)))
#define LINK_PG_FUNC(port) ((uint8_t)(LINK_PG_FUNC0 + (unsigned)(port)))
#define LINK_PG_F_BOUND(port)   ((uint16_t)(1u << (unsigned)(port)))
#define LINK_PG_F_PARKED(port)  ((uint16_t)(0x10u << (unsigned)(port)))
#define LINK_PG_F_REFUSED(port) ((uint16_t)(0x100u << (unsigned)(port)))
/** @brief From 4.28: port n's DShot driver holds its pin. */
#define LINK_PG_F_DSHOT(port)   ((uint16_t)(0x1000u << (unsigned)(port)))

/* ------------------------------------------------------------------- HITEC */
/**
 * @brief The HITEC page (0x34, since 4.23): raw transactions of the Hitec servo
 * wire (protocols/hitec) on a programming port.
 *
 * The page holds no Hitec session: the panel, or a bench console, writes one
 * transaction at a time and reads what came back.  shared/outputs/hitec_port.h
 * holds the rules.
 *
 * A read and a write see different registers.  A read gives the 32
 * registers LINK_HT_*: 16 fixed ones and 16 that LINK_HT_W_VIEW selects.
 * A write takes LINK_HT_W_*: the command in registers 0 to 3, one frame
 * taken whole or not at all, and the staged registers 4 to 21, written in
 * any windows before the command that names their CRC (cyclic redundancy
 * check, link_kst_crc()).  Nothing is kept in flash.
 *
 * From 4.29 (LINK_MINOR_HITEC_HOLD) a held port holds SUPPLY as well, from
 * its OPEN to its CLOSE: SUPPLY sets LINK_SP_PORT and takes an OFF of the
 * panel's and no other change of OUTPUT, SET_MV or SET_MA.  From 4.31
 * (LINK_MINOR_PORT_CUT) the writer marks the steps of a write that more
 * steps follow (LINK_HT_W_CMD_MORE), and SUPPLY holds the panel's OFF back
 * from the module until the write ends.
 */
enum {
    LINK_HT_STATE  = 0,  /**< bits 0-2 link_hitec_state_t, bits 3-11 the
                              LINK_HT_F_* flags, bits 12-15 the port */
    LINK_HT_SEQ    = 1,  /**< the SEQ of the last command taken; 0 at start */
    LINK_HT_OP     = 2,  /**< bits 12-15 the last operation started
                              (link_hitec_op_t), bits 0-11 the transactions
                              started since the port was taken, held at
                              4095 */
    LINK_HT_RESULT = 3,  /**< low byte link_hitec_result_t of that
                              operation, high byte why the last command
                              taken started nothing
                              (link_hitec_refusal_t; 0: it started) */
    LINK_HT_CAP    = 4,  /**< the last capture: bits 0-4 the bytes held,
                              bits 8-11 LINK_HT_CAP_*, bits 12-13 the
                              decoder (link_hitec_dec_t) */
    LINK_HT_DEC    = 5,  /**< low byte the decoder's hitec_rx_t, high byte
                              the reply's id (D series) */
    LINK_HT_VALUE  = 6,  /**< the decoded value */
    LINK_HT_VIEW   = 7,  /**< what registers 16 to 31 show
                              (link_hitec_view_t) */
    LINK_HT_BYTES  = 8,  /**< 8 registers: byte 2 k of the capture in the
                              low byte of register 8 + k, 2 k + 1 in the
                              high byte; 0 past the bytes held */
    LINK_HT_DATA   = 16, /**< 16 registers, as LINK_HT_W_VIEW selects */
    LINK_HT_COUNT  = 32,
};

/** @brief What registers 16 to 31 show. */
typedef enum {
    /**
     * @brief The start bits of bytes 0 to 7, ns from the receiver's start, 32
     * bits each, low half first; 0 past the bytes held.
     */
    LINK_HITEC_VIEW_TIMES_LO = 0,
    /** @brief Bytes 8 to 15, the same way. */
    LINK_HITEC_VIEW_TIMES_HI = 1,
    /** @brief The run: LINK_HT_I_*. */
    LINK_HITEC_VIEW_RUN = 2,
    LINK_HITEC_VIEW_COUNT
} link_hitec_view_t;

/** @brief Registers 16 and up in LINK_HITEC_VIEW_RUN. */
enum {
    LINK_HT_I_DELAY_LO = 16, /**< the decoder's delay_ns, low half */
    LINK_HT_I_DELAY_HI = 17,
    LINK_HT_I_LEAD_LO  = 18, /**< lead's first edge to the release, ns */
    LINK_HT_I_LEAD_HI  = 19,
    LINK_HT_I_T_LO     = 20, /**< start to the capture, us */
    LINK_HT_I_T_HI     = 21,
    LINK_HT_I_SPAN_LO  = 22, /**< the longest the run could take, us */
    LINK_HT_I_SPAN_HI  = 23,
    LINK_HT_I_BIT_NS   = 24, /**< the bit time of the open line, ns; 0
                                  while none is open */
    LINK_HT_I_LINE     = 25, /**< the open line, as LINK_HT_W_LINE, and
                                  LINK_HT_LINE_PRECHARGE */
    LINK_HT_I_PIN      = 26, /**< the port's pin, 0xFFFF while none */
    LINK_HT_I_WINDOW_LO = 27, /**< the window the capture asked, us */
    LINK_HT_I_WINDOW_HI = 28,
    LINK_HT_I_FRAMING  = 29, /**< bit k: byte k's stop bit was sampled at
                                  the start level */
};

/** @brief What a write of the page takes. */
enum {
    LINK_HT_W_CMD  = 0,  /**< bits 0-3 link_hitec_op_t, bit 4
                              LINK_HT_W_CMD_MORE (4.31), bits 8-11 the
                              programming port; every other bit 0 */
    LINK_HT_W_SEQ  = 1,  /**< LINK_HT_SEQ + 1 modulo 65536 */
    LINK_HT_W_KEY  = 2,  /**< LINK_HITEC_WRITE_KEY for an operation that
                              puts a byte on the wire, else 0 */
    LINK_HT_W_CRC  = 3,  /**< link_kst_crc() of the LINK_HT_W_STAGED
                              registers from LINK_HT_W_LINE, for every
                              operation but ABORT, CLOSE and CLEAR */
    LINK_HT_W_LINE = 4,  /**< bit 0 the family (hitec_family_t), bit 4 the
                              drive (hitec_drive_t): OPEN */
    LINK_HT_W_NTX  = 5,  /**< low byte the bytes to send, 1 to 8; high
                              byte the idle bit times between two, 0 to 6:
                              SEND */
    LINK_HT_W_TX   = 6,  /**< 4 registers: byte 2 k in the low byte of
                              register 6 + k, 2 k + 1 in the high byte */
    LINK_HT_W_LEAD_LO   = 10, /**< SEND: the lead, us, 0 to 4 000 000 */
    LINK_HT_W_LEAD_HI   = 11,
    LINK_HT_W_WINDOW_LO = 12, /**< SEND, LISTEN, POWER: the window, us */
    LINK_HT_W_WINDOW_HI = 13,
    LINK_HT_W_RX   = 14, /**< low byte the bytes to receive, 0 to 16; bit 8
                              answer the first byte (LISTEN, POWER) */
    LINK_HT_W_ANSWER = 15, /**< low byte the answer; bit 8 the level of a
                                LEVEL */
    LINK_HT_W_LEVEL_LO = 16, /**< LEVEL: how long, us, 1 to 4 000 000 */
    LINK_HT_W_LEVEL_HI = 17,
    LINK_HT_W_DECODE = 18,   /**< bits 0-1 link_hitec_dec_t, bits 8-15 its
                                  argument: the classic command whose reply
                                  is expected, the D series register read,
                                  or the byte a listen wants */
    LINK_HT_W_SUPPLY_MV = 19, /**< POWER: the supply voltage, mV,
                                   LINK_HITEC_SUPPLY_MIN_MV to
                                   LINK_HITEC_SUPPLY_MAX_MV */
    LINK_HT_W_SUPPLY_MA = 20, /**< POWER: its current limit, mA, 1 to the
                                   PD mini's 3000 */
    LINK_HT_W_OFF_MS = 21,    /**< POWER: how long the supply stays off,
                                   ms, 0 to LINK_HITEC_OFF_MS_MAX */
    LINK_HT_W_VIEW = 22,      /**< what LINK_HT_DATA shows
                                   (link_hitec_view_t).  Not staged: in
                                   force when written */
    LINK_HT_W_RX_MODE = 23,   /**< (4.24) how a D series line receives
                                   (link_hitec_rx_t), for every OPEN from
                                   the next on; a classic OPEN ignores it.
                                   Not staged; LINK_HITEC_RX_PULL_UP at
                                   start */
    LINK_HT_W_COUNT = 24,
};

/** @brief LINK_HT_W_RX_MODE. */
typedef enum {
    /**
     * @brief Released for the answer; a pull-up on the board lifts the line.
     */
    LINK_HITEC_RX_PULL_UP = 0,
    /**
     * @brief No pull-up: the IOmcu precharges the line and samples it
     * (protocols/hitec/include/hitec_pre.h).
     */
    LINK_HITEC_RX_PRECHARGE = 1,
    LINK_HITEC_RX_COUNT
} link_hitec_rx_t;

/** @brief LINK_HT_I_LINE: the open line receives by PRECHARGE. */
#define LINK_HT_LINE_PRECHARGE 0x0100u

/** @brief Registers of the command, from LINK_HT_W_CMD: one frame. */
#define LINK_HT_W_CMD_FRAME 4u
/** @brief The staged registers a command's CRC covers, from LINK_HT_W_LINE. */
#define LINK_HT_W_STAGED 18u

/**
 * @brief LINK_HT_W_KEY of an operation that puts a byte on the wire: every
 * SEND, and a LISTEN or a POWER that answers.
 */
#define LINK_HITEC_WRITE_KEY 0x48A5u
/**
 * @brief The supply voltage a POWER takes: until a session knows the servo
 * model, 4800 to 6000mV.
 */
#define LINK_HITEC_SUPPLY_MIN_MV 4800u
#define LINK_HITEC_SUPPLY_MAX_MV 6000u
/** @brief The longest off time of a POWER, ms. */
#define LINK_HITEC_OFF_MS_MAX 10000u

#define LINK_HT_STATE_OF(r) ((uint8_t)((r) & 0x7u))
#define LINK_HT_PORT_OF(r)  ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_HT_OP_OF(r)    ((uint8_t)(((r) >> 12) & 0xFu))
#define LINK_HT_W_CMD_OF(op, port) \
    ((uint16_t)((unsigned)(op) | (((unsigned)(port) & 0xFu) << 8)))
#define LINK_HT_LINE_OF(family, drive) \
    ((uint16_t)(((unsigned)(family) & 1u) | (((unsigned)(drive) & 1u) << 4)))

/** @brief Where the port is. */
typedef enum {
    LINK_HITEC_FREE = 0,     /**< the port's pin is the PWM generator's */
    LINK_HITEC_STOPPING = 1, /**< PWM renders no pulse; the running one
                                  ends */
    LINK_HITEC_OPEN = 2,     /**< the pin is the wire driver's, released */
    LINK_HITEC_BUSY = 3,     /**< a transaction runs */
    LINK_HITEC_POWER_OFF = 4, /**< a POWER: the supply is switched off;
                                   the ON and its listen follow */
} link_hitec_state_t;

/**
 * @brief Bits of the HITEC flags register.
 */
enum {
    LINK_HT_F_STOPPED = 0x0008u, /**< a stop cut the port; no transaction
                                      until CLEAR */
    LINK_HT_F_RAIL    = 0x0010u, /**< a PD mini answers: POWER is taken */
    LINK_HT_F_CAPTURE = 0x0020u, /**< the page holds a capture */
    LINK_HT_F_KEY     = 0x0040u, /**< the last transaction put a byte on
                                      the wire */
    LINK_HT_F_PRECHARGE = 0x0080u, /**< (4.24) LINK_HT_W_RX_MODE holds
                                        LINK_HITEC_RX_PRECHARGE */
    LINK_HT_F_MORE = 0x0100u,      /**< (4.31) the last command taken
                                        carried LINK_HT_W_CMD_MORE and
                                        started its transaction */
};

/**
 * @brief Bit 4 of a SEND, LISTEN, LEVEL or POWER command (4.31): more steps
 *        of the write this transaction belongs to follow it.
 *
 * The writer sets it on every step of a write to the servo's memory but
 * the last.  While it stands (LINK_HT_F_MORE), while a SEND whose frame
 * writes runs and for HITEC_PORT_WRITE_TAIL_MS after it, the SUPPLY page
 * counts a write as running (LINK_MINOR_PORT_CUT).  The page tells a
 * writing frame from its bytes, with or without the bit; a read and a
 * LISTEN write nothing.  Any other
 * command, one refused included, and a stop clear it.  A 4.30 IOmcu
 * refuses a command with the bit with LINK_NACK_BAD_VALUE; on OPEN, CLOSE,
 * ABORT and CLEAR it is refused with LINK_NACK_BAD_VALUE as well.
 */
#define LINK_HT_W_CMD_MORE 0x0010u

/** @brief LINK_HT_CAP bits 8-11, as hitec_capture_t has them. */
enum {
    LINK_HT_CAP_FULL     = 0x0100u,
    LINK_HT_CAP_ANSWERED = 0x0200u,
    LINK_HT_CAP_OVERFLOW = 0x0400u,
    LINK_HT_CAP_ABORTED  = 0x0800u,
};

/** @brief How the page decodes a capture. */
typedef enum {
    LINK_HITEC_DEC_NONE = 0,
    LINK_HITEC_DEC_C = 1,       /**< hitec_c_decode() */
    LINK_HITEC_DEC_D = 2,       /**< hitec_d_decode() */
    LINK_HITEC_DEC_LISTEN = 3,  /**< hitec_listen_decode() */
} link_hitec_dec_t;

/** @brief The operations of a command. */
typedef enum {
    LINK_HITEC_OP_NONE = 0,   /**< never written; LINK_HT_OP at start */
    LINK_HITEC_OP_OPEN = 1,   /**< take the port's pin from PWM and open
                                   the line on it, released */
    LINK_HITEC_OP_CLOSE = 2,  /**< cut what runs, give the pin back */
    LINK_HITEC_OP_SEND = 3,   /**< bytes out, then the receiver; needs the
                                   key */
    LINK_HITEC_OP_LISTEN = 4, /**< the receiver; the key with an answer */
    LINK_HITEC_OP_LEVEL = 5,  /**< hold the line, then release it */
    LINK_HITEC_OP_ABORT = 6,  /**< cut the running transaction or POWER */
    LINK_HITEC_OP_POWER = 7,  /**< the supply off, on, and a listen from
                                   the ON; the key with an answer */
    LINK_HITEC_OP_CLEAR = 8,  /**< clear LINK_HT_F_STOPPED */
    LINK_HITEC_OP_COUNT
} link_hitec_op_t;

/** @brief LINK_HT_RESULT's low byte. */
typedef enum {
    LINK_HITEC_RESULT_NONE = 0,    /**< no operation since the start */
    LINK_HITEC_RESULT_RUNNING = 1, /**< the operation runs */
    LINK_HITEC_RESULT_DONE = 2,    /**< it ended; a transaction's capture
                                        is on the page */
    LINK_HITEC_RESULT_SUPPLY = 3,  /**< POWER: the supply did not read off
                                        within LINK_HITEC_SUPPLY_MS, or
                                        refused the ON */
    LINK_HITEC_RESULT_START = 4,   /**< the driver did not take the
                                        transaction */
    LINK_HITEC_RESULT_NO_LINE = 5, /**< OPEN: the driver got no state
                                        machine, program space or DMA
                                        channel; the port is free */
    LINK_HITEC_RESULT_CUT = 6,     /**< an ABORT, a CLOSE or a stop ended
                                        it; a transaction's capture holds
                                        the bytes so far, marked aborted */
} link_hitec_result_t;

/** @brief The time a POWER waits for the supply to read off, ms. */
#define LINK_HITEC_SUPPLY_MS 3000u

/**
 * @brief Why a command that was taken did nothing: LINK_HT_RESULT's high byte.
 */
typedef enum {
    LINK_HITEC_REF_NONE = 0,
    LINK_HITEC_REF_BUSY = 1,      /**< an operation runs, or the port is
                                       being taken */
    LINK_HITEC_REF_PORT = 2,      /**< no such programming port, or not
                                       the one held */
    LINK_HITEC_REF_HELD = 3,      /**< the KST page holds the port */
    LINK_HITEC_REF_ARMED = 4,     /**< the bank is armed */
    LINK_HITEC_REF_UNSAFE = 5,    /**< heartbeat not trusted or link
                                       silent */
    LINK_HITEC_REF_NOT_OPEN = 6,  /**< the port is not held */
    LINK_HITEC_REF_NO_KEY = 7,    /**< a byte on the wire without the key */
    LINK_HITEC_REF_STAGED = 8,    /**< the staged registers do not have the
                                       CRC the command names */
    LINK_HITEC_REF_STOPPED = 9,   /**< a stop since: CLEAR first */
    LINK_HITEC_REF_NO_PIN = 10,   /**< the port has no bound pin */
    LINK_HITEC_REF_RANGE = 11,    /**< the staged transaction is not one
                                       the line runs */
    LINK_HITEC_REF_SUPPLY_RANGE = 12, /**< POWER: a set point or an off
                                           time out of range */
    LINK_HITEC_REF_NO_SUPPLY = 13, /**< POWER: no PD mini answers */
} link_hitec_refusal_t;

/* ----------------------------------------------------------------- control */
/**
 * @brief ARM, THROTTLE and MOTOR_POLES are registers 0 to 2 so that the write
 * that arms carries all three in one CAN (Controller Area Network) frame.
 *
 * The count is the divisor the far end starts sampling with; a run begun on a
 * stale one puts a wrong speed into the bench page's sticky rpm_max, and a
 * frame is all-or-nothing at the far end, so the run starts on the count that
 * was sent or does not start.  CLEAR sits behind them: the far end checks ARM
 * against its failsafe before it applies a CLEAR from the same frame, so the
 * clear is its own transaction ahead of the arm.
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

/** @brief The registers the frame that arms carries, from LINK_CT_ARM. */
#define LINK_CT_ARM_FRAME 3u

/**
 * @brief Even, and between these; zero is the fourth legal value, meaning
 * unknown.
 */
#define LINK_POLES_MIN  2u
#define LINK_POLES_MAX 42u

/**
 * @brief Leaving failsafe takes this value written to LINK_CT_CLEAR, not any
 * write and not the link coming back: a link that recovers must not re-arm a
 * bench on its own.
 */
#define LINK_CLEAR_MAGIC 0x5AFEu

/**
 * @brief What a non-zero ARM arms, from LINK_MINOR_ARM_ROLE.
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
/** @brief Both roles an ARM can name: throttle and surface outputs. */
#define LINK_ARM_ROLES     (LINK_ARM_THROTTLE | LINK_ARM_SURFACE)

/** @brief Throttle is hundredths of a percent: 0..10000 inclusive. */
#define LINK_THROTTLE_MAX 10000u

/* ----------------------------------------------------------------- update */

/**
 * @brief The IOmcu's firmware, written over the link into the flash slot it is
 * not running from.
 *
 * Frozen: see LINK_PROTOCOL_MAJOR.  The registers are versioned by
 * LINK_UPD_VERSION in register 0, which only grows, and only by registers or
 * commands added at the end.
 *
 * The flash holds two equal slots, A and B, behind the boot ROM's partition
 * table.  The image goes to the slot that is not running, in 64-byte blocks
 * (LINK_UPD_BLOCK_BYTES), strictly in order, each staged in UPDATE_DATA and
 * then committed with its CRC-16 (cyclic redundancy check, link_crc() seeded
 * LINK_CRC_INIT over the block's bytes).  The IOmcu writes a 4KiB
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
 * boot ROM starts it under a 16.7s watchdog; it stays only when the panel
 * reads its identity over the link and writes BUY.  Without the BUY the
 * watchdog restarts the IOmcu into the slot it came from.
 *
 * A session runs from BEGIN to ACTIVATE, ABORT, a failure or a restart.
 * While one runs, and while a BUY is pending, the IOmcu refuses ARM
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
 * IOmcu erases that slot's first sector: at the BUY when the gate is
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
    LINK_UPD_SLOT_SECTORS = 4, /**< 4KiB sectors in one slot; 0 when none    */
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

/**
 * @brief The update registers' own version; it grows when a register or command
 * is added.
 */
#define LINK_UPD_PROTOCOL 1u

/** @brief Bytes in a block, and registers in UPDATE_DATA. */
#define LINK_UPD_BLOCK_BYTES 64u
#define LINK_UPD_DATA_COUNT  32u
/** @brief Bytes in a flash sector, the unit of an erase. */
#define LINK_UPD_SECTOR_BYTES 4096u
/** @brief UPDATE_DATA registers FINISH takes the expected SHA-256 from. */
#define LINK_UPD_EXPECT_REGS 16u

/** @brief LINK_UPD_SLOT: the running slot in bits 0 to 3, and two flags. */
#define LINK_UPD_SLOT_A      0x0000u
#define LINK_UPD_SLOT_B      0x0001u
/**
 * @brief No slot to write: no partition table, or not the one this build has.
 */
#define LINK_UPD_SLOT_NONE   0x000Fu
#define LINK_UPD_SLOT_MASK   0x000Fu
/** @brief The running image is on trial and waits for BUY. */
#define LINK_UPD_SLOT_TRIAL  0x0100u
/** @brief The other slot's first sector still waits for its erase. */
#define LINK_UPD_SLOT_OTHER  0x0200u

/**
 * @brief The states of an update session on the IOmcu.
 */
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

/**
 * @brief The commands the UPDATE_CTL page takes.
 */
typedef enum {
    LINK_UPD_CMD_NONE         = 0,
    /**
     * @brief SHA-256 of the running slot's first ARG0 | ARG1 << 16 bytes, with
     * the image's TBYB flag read as clear (link_update_image.h).
     *
     * Idle only.
     */
    LINK_UPD_CMD_HASH_RUNNING = 1,
    /**
     * @brief A session for an image of ARG0 | ARG1 << 16 bytes: 1 to the slot.
     */
    LINK_UPD_CMD_BEGIN        = 2,
    /**
     * @brief UPDATE_DATA is block ARG0; ARG1 is its CRC-16.
     *
     * The last block is the image's remainder, its CRC over those bytes only.
     * The block before LINK_UPD_NEXT_BLOCK again is acknowledged and not
     * written.
     */
    LINK_UPD_CMD_COMMIT       = 3,
    /** @brief UPDATE_DATA registers 0 to 15 hold the expected SHA-256. */
    LINK_UPD_CMD_FINISH       = 4,
    /** @brief Write the first sector and restart into the new slot. */
    LINK_UPD_CMD_ACTIVATE     = 5,
    /** @brief End the session; the slot is left without an image. */
    LINK_UPD_CMD_ABORT        = 6,
    /**
     * @brief Keep the image on trial.
     *
     * Acknowledged with nothing to do when no image is on trial.
     */
    LINK_UPD_CMD_BUY          = 7,
} link_upd_cmd_t;

/**
 * @brief The reasons an update command or session was refused or failed.
 */
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

/**
 * @brief What the UPDATE_CTL digest register holds.
 */
typedef enum {
    LINK_UPD_DIGEST_NONE    = 0,
    LINK_UPD_DIGEST_RUNNING = 1, /**< HASH_RUNNING's, over SIZE bytes        */
    LINK_UPD_DIGEST_IMAGE   = 2, /**< FINISH's, over the image written       */
} link_upd_digest_t;

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_PAGES_H */
