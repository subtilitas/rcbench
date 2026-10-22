/**
 * @file
 * @brief The TONE page and the phase tap's capture behind it; see iomcu.h
 * for the other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "iomcu.h"

#include "pd_uart.h"
#include "tone_cap.h"
#include "tone_core1.h"

/* ------------------------------------------------------------ the phase tap */

/* The pins the tap may not take for now: the supply's, the programming
 * ports', and the sensor bus's with the ones core 1 has not let go of yet.
 * A write naming one is refused with BUSY; the board's own reserved pins
 * are refused with BAD_VALUE. */
uint64_t tone_taken(void)
{
    return supply_page_pins(&s_supply) | sense_page_held(&s_sense)
           | prog_port_pins(&s_prog);
}

/**
 * @brief Core 1's order from the page, running when the capture does.
 */
static void tone_order(void)
{
    tone_cmd_t cmd;
    tone_page_cmd(&s_tone, tone_cap_running(), &cmd);
    tone_core1_order(&cmd, tone_cap_start_us());
}

/*
 * The capture made to agree with the page: stopped, then started on the
 * page's pin if the tap is enabled and not refused, else the pin left an
 * input with its pull-down on.  False when the tap is enabled and the
 * wiring could not take its pin (no PIO state machine, no DMA channel).
 * Core 1 is told first that nothing runs and given up to
 * TONE_CORE1_WAIT_US to finish the pass it is in, then the capture is torn
 * down, and core 1 is told again once the capture runs: it never reads a
 * ring that is being started over.
 */
static bool s_tone_busy;   /* the last tone_rewire() found core 1 busy */
static uint64_t s_tone_was_us;   /* the capture's start before a rewire */

/**
 * @brief Makes the capture agree with the TONE page: stopped, then started on
 * the page's pin.
 *
 * @param keep_ring  True to recapture the ring with the elapsed time since the
 *                   start saved by the earlier rewire (tone_page_recapture());
 *                   false to start a new capture (tone_page_capture()).
 * @return False when the tap is enabled and the wiring could not take its pin
 *         (no PIO state machine, no DMA channel), or core 1 was still
 *         in a pass
 *         and the old capture runs on. True otherwise.
 */
static bool tone_rewire_as(bool keep_ring)
{
    s_tone_busy = false;
    if (!keep_ring) {
        s_tone_was_us = tone_cap_start_us();
    }
    const bool was_running = tone_cap_pause();
    tone_order();
    if (!tone_core1_quiesce() && was_running) {
        /* Core 1 did not finish its pass in time and may still read the
         * capture: nothing is taken down, the capture runs on as it was,
         * and the caller puts the set-up back. */
        tone_cap_resume();
        tone_order();
        s_tone_busy = true;
        return false;
    }
    tone_cap_stop();
    reserve_held();
    if (!tone_page_wanted(&s_tone)) {
        if (tone_page_pin_free(&s_tone, &s_outputs, tone_taken())) {
            tone_cap_rest(tone_page_pin(&s_tone));
        }
        return true;
    }
    if (s_tone.no_capture || !tone_cap_start(tone_page_pin(&s_tone))) {
        return false;
    }
    if (keep_ring) {
        tone_page_recapture(&s_tone,
                            (uint32_t)((tone_cap_start_us() - s_tone_was_us)
                                       / 1000u));
    } else {
        tone_page_capture(&s_tone);
    }
    tone_order();
    return true;
}

bool tone_rewire(void)
{
    return tone_rewire_as(false);
}

void tone_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    tone_core1_sync(&s_tone);
    tone_page_read(&s_tone, off, n, out);
}

/*
 * The tap's set-up: judged by the page against the bank and the other
 * pages' pins, then the capture started or moved, the pin reserved, the
 * set-up saved and core 1 told.  A change of ENABLE or of the pin, and a
 * write that tries a refused tap again, rewire the capture; the wiring
 * failing there puts the old set-up back and refuses the write with BUSY:
 * core 1 in a pass, or no PIO state machine or DMA channel left by the
 * outputs and the supply, each of which a later write can find free.  Core
 * 1 that did not start at boot frees nothing before a restart: the page
 * refuses that with BAD_VALUE before the wiring is tried
 * (tone_page_no_capture()).  A change
 * of a range only reaches core 1.  Taken
 * armed or not: the tap is an input.
 */
uint8_t tone_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    /* The pins core 1 still holds, as of its last tick. */
    sense_sync();
    tone_core1_sync(&s_tone);
    uint16_t was[LINK_TN_CONFIG_COUNT];
    memcpy(was, s_tone.cfg, sizeof(was));
    const bool was_refused = s_tone.refused;
    const uint16_t gen = s_tone.gen;
    const uint8_t nack = tone_page_write(&s_tone, off, n, in, &s_outputs,
                                         tone_taken());
    if (nack != 0u || s_tone.gen == gen) {
        return nack;
    }
    const bool rewire = was_refused
                        || s_tone.cfg[LINK_TN_ENABLE] != was[LINK_TN_ENABLE]
                        || s_tone.cfg[LINK_TN_PIN] != was[LINK_TN_PIN];
    uint16_t trial[LINK_TN_CONFIG_COUNT];
    memcpy(trial, s_tone.cfg, sizeof(trial));
    bool wired = !rewire || tone_rewire();
    if (wired && rewire && s_supply_attach && !s_pd_open
        && supply_page_enabled(&s_supply)) {
        /* The supply's wiring waits for flash with its UART closed, and
         * attaches once saved: the capture may not take the PIO room that
         * attach needs.  Tried, and let go again. */
        wired = pd_uart_open(supply_page_tx(&s_supply),
                             supply_page_rx(&s_supply),
                             supply_page_uart_baud(&s_supply));
        if (wired) {
            pd_uart_close();
        }
    }
    if (!wired && s_tone_busy) {
        /* Core 1 still in a pass: the capture was not touched and runs as
         * the set-up it had, which the page shows again. */
        tone_page_revert(&s_tone, was, was_refused);
        reserve_held();
        tone_order();
        return LINK_NACK_BUSY;
    }
    if (!wired) {
        /* No state machine or channel for that pin, or none left for a
         * supply waiting to attach: the set-up as it was, and the capture
         * as it was. */
        tone_page_revert(&s_tone, was, was_refused);
        /* The panel was refused and keeps its place: the beeps in the
         * ring stay readable across the restart of the old capture. */
        if (!tone_rewire_as(true)) {
            if (s_tone_busy) {
                /* Core 1 still in a pass: the trial capture runs on, so the
                 * page says what runs and its pin stays reserved. */
                tone_page_revert(&s_tone, trial, false);
                reserve_held();
                tone_order();
                return LINK_NACK_BUSY;
            }
            tone_page_refuse(&s_tone);
            reserve_held();
        }
        return LINK_NACK_BUSY;
    }
    if (!rewire) {
        reserve_held();
        tone_order();
    }
    save_outputs(&s_state);
    return 0u;
}
