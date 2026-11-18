/**
 * @file
 * @brief The IOmcu's watchdog on its own main loop: when the RP2350's
 *        watchdog is enabled, where it is fed, and what a restart by it
 *        reports.
 *
 * Every fail-safe of the IOmcu is polled from core 0's main loop: the
 * heartbeat monitor, the link's 200ms silence, the outputs' step, the
 * supply's pass.  A PWM slice and a PPM frame on a chained DMA channel
 * repeat without the processor, and the PD (Power Delivery) mini and the
 * programming ports' 5V stay as last commanded.  A core 0 that stops
 * passing the loop -- a fault, a panic(), a loop that does not end --
 * leaves them driving.  The chip's watchdog ends that: it resets the chip
 * LOOP_WATCH_PERIOD_MS after the last feed.  The reset releases every pad,
 * so PWM and PPM stop edging, and the start after it drives the
 * programming ports' 5V low, switches a PD mini left on off
 * (supply_boot_off()) and holds the arm latch until a CLEAR.
 *
 * The decisions are here and host-tested (test_loop_watch); the firmware
 * makes the hardware calls (firmware/iomcu/src/main.c):
 *
 * - The feed points are two: the top of a pass, one feed a pass
 *   (loop_watch_pass()), and right before a flash window
 *   (loop_watch_window()), and nowhere else.  A loop inside a pass that
 *   does not end is a pass that does not end.  The feed before a window
 *   keeps the reset out of an erase or a program: a reset inside one
 *   leaves a store sector or a sector of the update half-written.
 * - The watchdog is enabled at the first pass, after the boot path:
 *   supply_boot_off() takes up to 3,500ms and the boot's store reclaims a
 *   flash window each, before any output can be driven.  The boot path
 *   itself is not watched.
 * - Not while the image runs on trial after an update: the boot ROM owns
 *   the watchdog for 16.7s then, and an image that does not BUY in that
 *   time is replaced by the slot it came from.  Enabling it would reload
 *   the ROM's count.  The first pass after the BUY enables it.  A window
 *   before that pass, the BUY's own included, feeds nothing.
 * - Never in the bench console build (IOMCU_CONSOLE=ON, not released):
 *   its driver test sleeps inside a command.
 * - Core 1 reads the sensor bus and is not watched.
 *
 * The flash part.  Both boards this firmware runs on carry the Waveshare
 * RP2350-CAN module, the rbmod carrier as a plug-in; Waveshare's product
 * page (https://www.waveshare.com/rp2350-can.htm, "What's On Board") names
 * its flash as P25Q32SH-UXH-IR, 4MB.  The part on a given module is not
 * read out by the firmware.  Puya P25Q32SH datasheet V1.8 (2023-10-27),
 * table 5-4, 2.3V to 3.6V, -40 to +85 degrees C:
 *
 *   tSE  sector erase, 4KiB       16ms typical   30ms maximum
 *   tPP  page program, 256 bytes  1.6ms typical  2.5ms maximum
 *
 * One window holds one sector erase (30,000us) or one program: 2 pages of
 * a store record (5,000us) or the 16 pages of the update's 4KiB sector
 * (40,000us).  The SDK's way into and out of the flash's command mode is
 * taken as 2,000us: the 19,178us measured for an erase and a page program
 * on the bring-up module (firmware/iomcu/src/out_store.h, 1 sample) is
 * 1,578us over the two typical figures.  So LOOP_WATCH_WINDOW_MAX_US is
 * 42,000.
 *
 * The longest time between two feeds, in microseconds: the feed before a
 * window, the window, and everything else a pass may hold before the feed
 * at the top of the next pass.
 *
 *   the window's wait for core 1 to stop,
 *   OUT_STORE_LOCKOUT_MS                                         10,000
 *   the window, LOOP_WATCH_WINDOW_MAX_US                         42,000
 *   its wait for core 1 to start again                           10,000
 *   2 TONE rewires, TONE_CORE1_WAIT_US (5,000us) each            10,000
 *   can_drain() before the update's restart, 7 batches of
 *   CAN_TXQ_STUCK_US (5,000us)                                   35,000
 *   the store's line, PICO_STDIO_USB_STDOUT_TIMEOUT_US            1,000
 *   the rest of a pass, measured maximum                            250
 *                                                               -------
 *   LOOP_WATCH_WORST_GAP_US                                     108,250
 *
 * LOOP_WATCH_PERIOD_MS (500ms) is 4.6 times that.  A window may take
 * 433,750us before the watchdog resets inside it, 10 times the part's
 * maximum; the W25Q16JV's 400ms sector erase maximum (datasheet revision
 * I, tSE) is under it as well.  A pass holds 6 windows at most: 2 calls
 * of can_service(), each with one update request of a sector erase and a
 * 4KiB program, the update's owed erase, and the output store's save or
 * reclaim.  No pass with all of them was measured; the longest pass
 * measured outside a flash window is 250us.
 *
 * A restart by this watchdog reads as RESET_CAUSE_LOOP_WATCHDOG
 * (reset_cause.h) and sets STATUS fault bit 7, LINK_FAULT_WATCHDOG, until
 * a CLEAR is taken: the panel shows `IOMCU RESTARTED (WATCHDOG)`.
 *
 * Pure C, no pico-sdk.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_LOOP_WATCH_H
#define RCBENCH_LOOP_WATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "reset_cause.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The watchdog's period: a reset this long after the last feed. */
#define LOOP_WATCH_PERIOD_MS 500u

/** @brief The flash part's sector erase, us: P25Q32SH tSE maximum. */
#define LOOP_WATCH_ERASE_MAX_US 30000u
/** @brief The flash part's page program, us: P25Q32SH tPP maximum. */
#define LOOP_WATCH_PAGE_MAX_US 2500u
/** @brief The pages of the longest program in one window: the update's
 *         4KiB sector, 16 pages of 256 bytes. */
#define LOOP_WATCH_PROGRAM_PAGES 16u
/** @brief The SDK's way into and out of the flash's command mode around
 *         one erase or program, us: taken, not from a datasheet. */
#define LOOP_WATCH_WINDOW_ENTRY_US 2000u
/** @brief The longest flash window, us: the 16-page program, 42,000. */
#define LOOP_WATCH_WINDOW_MAX_US                                          \
    (LOOP_WATCH_PROGRAM_PAGES * LOOP_WATCH_PAGE_MAX_US                    \
     + LOOP_WATCH_WINDOW_ENTRY_US)
/** @brief The flash windows one pass can hold. */
#define LOOP_WATCH_WINDOWS 6u
/** @brief A window's wait for core 1, us, on the way in and on the way
 *         out: OUT_STORE_LOCKOUT_MS. */
#define LOOP_WATCH_LOCKOUT_US 10000u
/** @brief A TONE rewire's wait for core 1, us: TONE_CORE1_WAIT_US. */
#define LOOP_WATCH_TONE_US 5000u
/** @brief The TONE rewires one pass can hold: one a can_service() call. */
#define LOOP_WATCH_TONES 2u
/** @brief can_drain() before the update's restart, us: 7 batches of
 *         CAN_TXQ_STUCK_US. */
#define LOOP_WATCH_DRAIN_US 35000u
/** @brief The store's line waiting for the USB host, us:
 *         PICO_STDIO_USB_STDOUT_TIMEOUT_US. */
#define LOOP_WATCH_PRINT_US 1000u
/** @brief The longest pass measured outside a flash window, us. */
#define LOOP_WATCH_PASS_US 250u

/** @brief What a pass may hold outside its flash windows, us: 46,250. */
#define LOOP_WATCH_REST_US                                                \
    (LOOP_WATCH_TONES * LOOP_WATCH_TONE_US + LOOP_WATCH_DRAIN_US          \
     + LOOP_WATCH_PRINT_US + LOOP_WATCH_PASS_US)

/** @brief One window with its two waits for core 1, us: 62,000. */
#define LOOP_WATCH_WINDOW_SPAN_US                                         \
    (LOOP_WATCH_WINDOW_MAX_US + 2u * LOOP_WATCH_LOCKOUT_US)

/** @brief The longest time between two feeds, us: 108,250. */
#define LOOP_WATCH_WORST_GAP_US                                           \
    (LOOP_WATCH_WINDOW_SPAN_US + LOOP_WATCH_REST_US)

/** @brief What the firmware does with the watchdog at a feed point. */
typedef enum {
    LOOP_WATCH_NONE = 0, /**< nothing: the watchdog is not this firmware's */
    LOOP_WATCH_ENABLE,   /**< write RESET_WD_LOOP_MARK to SCRATCH0 and
                              enable the watchdog at LOOP_WATCH_PERIOD_MS  */
    LOOP_WATCH_FEED,     /**< reload the watchdog                          */
} loop_watch_act_t;

/** @brief The watch's state; loop_watch_init() fills it. */
typedef struct {
    bool console;   /**< the bench console build: never enabled           */
    bool enabled;   /**< the firmware enabled the watchdog this boot      */
    bool restarted; /**< this start followed a restart by the watchdog,
                         and no CLEAR has been taken since                */
} loop_watch_t;

/**
 * @brief The watch at boot.
 *
 * @param w        The watch; nothing happens for NULL.
 * @param console  The build is the bench console's (IOMCU_CONSOLE=ON).
 * @param cause    Why the IOmcu started (reset_cause_decode()).
 */
void loop_watch_init(loop_watch_t *w, bool console, reset_cause_t cause);

/**
 * @brief The feed point of a pass: once at the top of every pass of the
 *        main loop.
 *
 * NONE in the console build and while the image waits for its BUY with the
 * watchdog not yet enabled; ENABLE once, at the first pass that is neither;
 * FEED at every pass after it.  A watchdog that is enabled is fed whatever
 * @p on_trial says: nothing here disables it.
 *
 * @param w         The watch; NONE for NULL.
 * @param on_trial  The running image waits for its BUY: the boot ROM owns
 *                  the watchdog.
 * @return What the firmware does now.
 */
loop_watch_act_t loop_watch_pass(loop_watch_t *w, bool on_trial);

/**
 * @brief The feed point right before a flash window: an erase or a
 *        program, with interrupts off and core 1 parked for its length.
 *
 * FEED while the watchdog is enabled: the window starts with the whole
 * period ahead of it.  NONE otherwise, and never ENABLE: before the first
 * pass (the boot's reclaims), on trial (the BUY's window) and in the
 * console build the watchdog is not this firmware's.
 *
 * @param w  The watch; NONE for NULL.
 * @return LOOP_WATCH_FEED or LOOP_WATCH_NONE.
 */
loop_watch_act_t loop_watch_window(const loop_watch_t *w);

/**
 * @brief Whether SCRATCH0 is zeroed before a restart the firmware asks for
 *        (the update's reboot()): the restart after it is not the
 *        watchdog's.
 *
 * @param w  The watch.
 * @return True when the firmware enabled the watchdog this boot.
 */
bool loop_watch_handover(const loop_watch_t *w);

/**
 * @brief The STATUS fault bit this watch owns.
 *
 * @param w  The watch.
 * @return LINK_FAULT_WATCHDOG from a start after a restart by the watchdog
 *         until loop_watch_control() sees a CLEAR taken; else 0.
 */
uint16_t loop_watch_fault(const loop_watch_t *w);

/**
 * @brief A write to the CONTROL page, after its answer is known: a window
 *        that holds CLEAR and was stored takes the fault bit away.
 *
 * The page refuses a CLEAR without its magic and stores nothing of a
 * refused frame (link_control_write()), so a stored window that reaches
 * LINK_CT_CLEAR is a CLEAR the operator's hold of ARM or STOP sent.
 *
 * @param w     The watch; nothing happens for NULL.
 * @param off   The window's first register.
 * @param n     Its registers.
 * @param nack  The page's answer: 0 when the window was stored.
 */
void loop_watch_control(loop_watch_t *w, uint8_t off, uint8_t n,
                        uint8_t nack);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LOOP_WATCH_H */
