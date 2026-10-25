/**
 * @file
 * @brief The TONE link page at the IOmcu: the phase tap that hears an ESC's
 *        (electronic speed controller) beeps on one motor phase.
 *
 * The registers are in link_pages.h (LINK_TN_*).
 *
 * The set-up, registers 0 to 6, is refused on a value out of its range, on
 * a combination the detector refuses (tone_init()), and, while the tap is
 * enabled, on a pin past the bank, reserved, bound to an output, held by
 * the SENSE or SUPPLY page, or an ADC (analog to digital converter) pin.
 * The pin it takes is reserved from the outputs for as long as the tap is
 * enabled and runs (tone_page_pins()).  The tap is an input and drives
 * nothing, so a change is taken armed or not.
 *
 * Host-tested.  The page holds the contract and the checks.  Capturing is
 * core 0's wiring (PIO state machine, DMA ring, pull-down) and detecting is
 * core 1's (tone_svc.h): the page turns the set-up into the order core 1
 * runs (tone_page_cmd()), and takes the beeps and status core 1 hands back
 * (tone_page_publish()) into the 64-beep ring and the read-only registers.
 * Until a status under the set-up in force arrives the read-only registers
 * read 0 and the ring is as it was.
 *
 * Beep numbers.  Beeps are numbered from 1 to 65535 and then from 1 again,
 * so that 0 can mean none.  BEEP_HEAD is the newest, 0 before the first.
 * The ring holds the last LINK_TN_RING beeps of the capture running; a
 * capture that starts again empties it and the numbers go on.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"
#include "tone.h"
#include "tone_svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The TONE page state: the set-up, what core 1 last reported, and the
 *        beep ring of the capture.
 */
typedef struct {
    uint16_t   cfg[LINK_TN_CONFIG_COUNT];  /**< registers 0 to 6          */
    uint16_t   evt_sel;                    /**< register 13, not kept     */
    uint16_t   gen;        /**< moves with every change of the set-up     */
    uint16_t   cap_gen;    /**< moves each time the capture starts again  */
    bool       refused;    /**< enabled, and the pin could not be taken   */
    bool       no_capture; /**< no capture runs this boot: core 1 did not
                                start (tone_page_no_capture())            */
    /* What core 1 last said, under set-up gen. */
    bool       running;     /**< core 1's last status: an order with run was taken */
    bool       overrun;     /**< core 1's last status: a lap or a FIFO overflow */
    bool       beep;        /**< core 1's last status: a run of bursts is under way */
    bool       tone;        /**< core 1's last status: the last window held a tone */
    uint16_t   window, win_freq_dhz, win_periods;
    uint32_t   lost, glitches;   /**< counted across set-ups              */
    uint32_t   seen_lost, seen_glitches;   /**< the status's own, last    */
    /* The beeps of this capture. */
    tone_rec_t ring[LINK_TN_RING];
    uint64_t   n;          /**< beeps taken since boot; 64 bit: no wrap   */
    uint64_t   first;      /**< the first still valid, as a count         */
    uint32_t   ms_shift;   /**< added to each start: a restart that kept
                                the ring keeps the time base too          */
    bool       broke;      /**< such a restart cut this capture: OVERRUN  */
    tone_t     scratch;    /**< tone_init()'s verdict on a set-up         */
} tone_page_t;

/**
 * @brief Set the page to its start: the defaults, and the first beep number 1.
 *
 * Nothing is done when @p p is NULL.
 *
 * @param p The page state, overwritten.
 */
void tone_page_init(tone_page_t *p);

/**
 * @brief The set-up as a page starts, into @p cfg (LINK_TN_CONFIG_COUNT
 *        registers): the tap off, GP22, 400 to 6500Hz, 8%, 3ms, 3 periods.
 *
 * @param cfg Receives LINK_TN_CONFIG_COUNT registers. Nothing is done when
 *        NULL.
 */
void tone_page_defaults(uint16_t *cfg);

/**
 * @brief A write, validated whole before any of it is stored.
 *
 * Registers 0 to 7 are the set-up and EVT_SEL alone is the beep shown; anything
 * else is read only.  Refused: off the page (BAD_RANGE); a read-only register
 * (READ_ONLY); a value out of its range, register 7 other than 0, a set-up
 * tone_init() refuses, and with the tap enabled a pin past the bank, an ADC pin
 * or one the board reserves (BAD_VALUE); with the tap enabled on a page that
 * runs no capture this boot (tone_page_no_capture(), BAD_VALUE: no freed pin
 * makes it good before a restart); with the tap enabled a pin in @p taken or
 * bound to an output (BUSY, since 4.17: freed, the same write is taken).  A
 * write of the set-up in force is taken, unless the tap is refused: then it is
 * tried again.
 *
 * @p taken is the pins another page holds: SUPPLY's, PROG's, and SENSE's
 * including the ones core 1 has not let go of yet.  Every reserved pin
 * outside it is judged a board pin.
 *
 * @param p The page state, changed only when the write is taken.
 * @param off First register, 0 to LINK_TN_COUNT - 1.
 * @param n Number of registers to write. 0 is taken with no change.
 * @param in The @p n new register values.
 * @param o The outputs bank, for the pin's reservation.
 * @param taken Mask of the pins other pages hold, bit n for GPn.
 * @return 0 when taken. LINK_NACK_BAD_RANGE for a NULL pointer or a range past
 *         LINK_TN_COUNT. LINK_NACK_READ_ONLY for a range that reaches
 *         register 8 (FLAGS) or above. LINK_NACK_BAD_VALUE for the refusals
 *         above. LINK_NACK_BUSY for a pin in @p taken or bound to an output.
 */
uint8_t tone_page_write(tone_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const outputs_t *o,
                        uint64_t taken);

/**
 * @brief A TONE page read.
 *
 * Nothing is done when @p p or @p out is NULL, or when off and n reach past
 * LINK_TN_COUNT.
 *
 * @param p The page state, not changed.
 * @param off First register, 0 to LINK_TN_COUNT - 1.
 * @param n Number of registers to read.
 * @param out Receives the @p n register values.
 */
void tone_page_read(const tone_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * @brief The set-up kept in flash, loaded as a write would, at boot.
 *
 * A set-up that is not valid starts as the defaults.  A valid one that enables
 * the tap on a pin that is no longer free is kept, and the tap does not run:
 * FLAGS says PIN_REFUSED and the pin is not reserved.
 *
 * Nothing is done when @p p, @p cfg or @p o is NULL.
 *
 * @param p The page state, overwritten.
 * @param cfg The kept set-up, LINK_TN_CONFIG_COUNT registers.
 * @param o The outputs bank, for the pin's reservation.
 * @param taken Mask of the pins other pages hold, as tone_page_write()
 *        takes it.
 */
void tone_page_restore(tone_page_t *p, const uint16_t *cfg,
                       const outputs_t *o, uint64_t taken);

/**
 * @brief Put back the set-up @p cfg and refusal state a write replaced, after
 *        the wiring could not take it.
 *
 * Nothing is done when @p p or @p cfg is NULL.
 *
 * @param p The page state, overwritten.
 * @param cfg The set-up to put back, LINK_TN_CONFIG_COUNT registers.
 * @param refused The refusal state to put back.
 */
void tone_page_revert(tone_page_t *p, const uint16_t *cfg, bool refused);

/**
 * @brief The wiring could not take the pin of an enabled set-up.
 *
 * Nothing changes unless the set-up is enabled.
 *
 * @param p The page state.
 */
void tone_page_refuse(tone_page_t *p);

/**
 * @brief No capture runs this boot (core 1 did not start): an enabled set-up
 *        is refused as tone_page_refuse() does, and every later write that
 *        enables the tap is refused with LINK_NACK_BAD_VALUE until the IOmcu
 *        restarts.  A write with ENABLE clear is taken.
 *
 * @param p The page state.
 */
void tone_page_no_capture(tone_page_t *p);

/**
 * @brief The capture starts again (the tap enabled, or its pin moved): the
 *        ring empties and the order carries a new capture generation.
 *
 * @param p The page state.
 */
void tone_page_capture(tone_page_t *p);

/**
 * @brief The capture starts again with the ring kept: a refused write that put
 *        the old set-up back.
 *
 * The order carries a new capture generation; the beeps already in the ring
 * stay readable, because the panel was told nothing restarted.  @p shift_ms,
 * the time from the old capture's start to the new one's, is added to every
 * later beep's start, so EVT_START_MS keeps its origin.  A beep under way was
 * cut: FLAGS shows OVERRUN until the next capture.
 *
 * @param p The page state.
 * @param shift_ms Milliseconds from the old capture's start to the new one's.
 */
void tone_page_recapture(tone_page_t *p, uint32_t shift_ms);

/**
 * @brief Whether ENABLE bit 0 (the tap) is set.
 *
 * @param p The page state.
 * @return true when enabled, false when not, or when @p p is NULL.
 */
bool tone_page_enabled(const tone_page_t *p);

/**
 * @brief The configured pin, from register LINK_TN_PIN.
 *
 * @param p The page state.
 * @return The pin number, 0 for NULL.
 */
uint8_t tone_page_pin(const tone_page_t *p);

/**
 * @brief Whether the capture is to run: enabled and not refused.
 *
 * @param p The page state.
 * @return true when the tap is enabled and not refused, false otherwise.
 */
bool tone_page_wanted(const tone_page_t *p);

/**
 * @brief The pin as a reservation mask; 0 unless the capture is wanted.
 *
 * @param p The page state.
 * @return Bit n set for the configured pin n, or 0.
 */
uint64_t tone_page_pins(const tone_page_t *p);

/**
 * @brief Whether the configured pin is free for the tap, as a write of ENABLE
 *        would judge it: the pull-down is put on it only if so.
 *
 * @param p The page state.
 * @param o The outputs bank.
 * @param taken Mask of the pins other pages hold.
 * @return true when the pin is free, false when it is not or @p p or @p o is
 *         NULL.
 */
bool tone_page_pin_free(const tone_page_t *p, const outputs_t *o,
                        uint64_t taken);

/**
 * @brief LINK_NACK_BAD_VALUE for an OUTPUTS page (slots, LINK_OS_COUNT
 *        registers) that binds a slot to the tap's pin; 0 otherwise.
 *
 * @param p The page state.
 * @param slots The OUTPUTS page, LINK_OS_COUNT registers.
 * @return LINK_NACK_BAD_VALUE when a slot with a driver binds the pin held by
 *         tone_page_pins(), 0 otherwise. LINK_NACK_BAD_VALUE for a NULL
 *         @p slots.
 */
uint8_t tone_page_slots_check(const tone_page_t *p, const uint16_t *slots);

/**
 * @brief The order for core 1; @p run is whether the capture runs now.
 *
 * Nothing is done when @p p or @p cmd is NULL.
 *
 * @param p The page state.
 * @param run Whether the capture is asked to run now.
 * @param cmd Receives the order. Set to all zero, then filled in.
 */
void tone_page_cmd(const tone_page_t *p, bool run, tone_cmd_t *cmd);

/**
 * @brief What core 1 handed back: its status, and the @p n beeps it finished.
 *
 * Taken when @p st was made under the set-up in force; ignored otherwise.
 *
 * @param p The page state.
 * @param st The status core 1 returned.
 * @param rec The beeps core 1 finished, @p n of them.
 * @param n Number of beeps in @p rec.
 */
void tone_page_publish(tone_page_t *p, const tone_status_t *st,
                       const tone_rec_t *rec, size_t n);

/**
 * @brief @p n beeps finished by core 1 under set-up @p gen and capture
 *        @p cap_gen, into the ring; dropped when the capture is not the one in
 *        force.
 *
 * A beep finished under an earlier set-up of the same capture is taken: a
 * change of the range keeps the ring and the numbering, and a beep the
 * detector finished before it took the change is a beep that happened.
 *
 * @param p The page state.
 * @param gen The set-up generation the beeps finished under. Not checked.
 * @param cap_gen The capture generation the beeps belong to.
 * @param rec The beeps, @p n of them.
 * @param n Number of beeps in @p rec.
 */
void tone_page_beeps(tone_page_t *p, uint16_t gen, uint16_t cap_gen,
                     const tone_rec_t *rec, size_t n);

/**
 * @brief @p n beeps lost between the cores, added to LOST.
 *
 * @param p The page state. Nothing is done when NULL.
 * @param n Number of beeps lost.
 */
void tone_page_dropped(tone_page_t *p, uint32_t n);

#ifdef __cplusplus
}
#endif
