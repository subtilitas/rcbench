/*
 * The TONE link page at the coprocessor: the phase tap that hears an ESC's
 * (electronic speed controller) beeps on one motor phase.  The registers
 * are in link_pages.h (LINK_TN_*).
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

typedef struct {
    uint16_t   cfg[LINK_TN_CONFIG_COUNT];  /**< registers 0 to 6          */
    uint16_t   evt_sel;                    /**< register 13, not kept     */
    uint16_t   gen;        /**< moves with every change of the set-up     */
    uint16_t   cap_gen;    /**< moves each time the capture starts again  */
    bool       refused;    /**< enabled, and the pin could not be taken   */
    /* What core 1 last said, under set-up gen. */
    bool       running, overrun, beep, tone;
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

void tone_page_init(tone_page_t *p);

/** The set-up as a page starts, into @p cfg (LINK_TN_CONFIG_COUNT
 *  registers): the tap off, GP22, 400 to 6500 Hz, 8 %, 3 ms, 3 periods. */
void tone_page_defaults(uint16_t *cfg);

/**
 * A write, validated whole before any of it is stored.  Registers 0 to 7
 * are the set-up and EVT_SEL alone is the beep shown; anything else is
 * read only.  Refused: off the page (BAD_RANGE); a read-only register
 * (READ_ONLY); a value out of its range, register 7 other than 0, a set-up
 * tone_init() refuses, and with the tap enabled a pin past the bank,
 * reserved, bound to an output, in @p taken or an ADC pin (BAD_VALUE).  A
 * write of the set-up in force is taken, unless the tap is refused: then it
 * is tried again.
 *
 * @p taken is the pins another page holds: SUPPLY's, and SENSE's including
 * the ones core 1 has not let go of yet.
 */
uint8_t tone_page_write(tone_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const outputs_t *o,
                        uint64_t taken);

void tone_page_read(const tone_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * The set-up kept in flash, loaded as a write would, at boot.  A set-up
 * that is not valid starts as the defaults.  A valid one that enables the
 * tap on a pin that is no longer free is kept, and the tap does not run:
 * FLAGS says PIN_REFUSED and the pin is not reserved.
 */
void tone_page_restore(tone_page_t *p, const uint16_t *cfg,
                       const outputs_t *o, uint64_t taken);

/** Put back the set-up @p cfg and refusal state a write replaced, after the
 *  wiring could not take it. */
void tone_page_revert(tone_page_t *p, const uint16_t *cfg, bool refused);

/** The wiring could not take the pin of an enabled set-up. */
void tone_page_refuse(tone_page_t *p);

/** The capture starts again (the tap enabled, or its pin moved): the ring
 *  empties and the order carries a new capture generation. */
void tone_page_capture(tone_page_t *p);

/** The capture starts again with the ring kept: a refused write that put
 *  the old set-up back.  The order carries a new capture generation; the
 *  beeps already in the ring stay readable, because the panel was told
 *  nothing restarted.  @p shift_ms, the time from the old capture's start
 *  to the new one's, is added to every later beep's start, so EVT_START_MS
 *  keeps its origin.  A beep under way was cut: FLAGS shows OVERRUN until
 *  the next capture. */
void tone_page_recapture(tone_page_t *p, uint32_t shift_ms);

bool    tone_page_enabled(const tone_page_t *p);   /**< ENABLE bit 0      */
uint8_t tone_page_pin(const tone_page_t *p);

/** Whether the capture is to run: enabled and not refused. */
bool tone_page_wanted(const tone_page_t *p);

/** The pin as a reservation mask; 0 unless the capture is wanted. */
uint64_t tone_page_pins(const tone_page_t *p);

/** Whether the configured pin is free for the tap, as a write of ENABLE
 *  would judge it: the pull-down is put on it only if so. */
bool tone_page_pin_free(const tone_page_t *p, const outputs_t *o,
                        uint64_t taken);

/** LINK_NACK_BAD_VALUE for an OUTPUTS page (@p slots, LINK_OS_COUNT
 *  registers) that binds a slot to the tap's pin; 0 otherwise. */
uint8_t tone_page_slots_check(const tone_page_t *p, const uint16_t *slots);

/** The order for core 1; @p run is whether the capture runs now. */
void tone_page_cmd(const tone_page_t *p, bool run, tone_cmd_t *cmd);

/**
 * What core 1 handed back: its status, and the @p n beeps it finished.
 * Taken when @p st was made under the set-up in force; ignored otherwise.
 */
void tone_page_publish(tone_page_t *p, const tone_status_t *st,
                       const tone_rec_t *rec, size_t n);

/**
 * @p n beeps finished by core 1 under set-up @p gen and capture @p cap_gen,
 * into the ring; dropped when either is not the one in force.
 */
void tone_page_beeps(tone_page_t *p, uint16_t gen, uint16_t cap_gen,
                     const tone_rec_t *rec, size_t n);

/** @p n beeps lost between the cores, added to LOST. */
void tone_page_dropped(tone_page_t *p, uint32_t n);

#ifdef __cplusplus
}
#endif
