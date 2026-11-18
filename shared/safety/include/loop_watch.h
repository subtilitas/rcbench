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
 * - The feed point is the top of a pass, one feed a pass, and nowhere
 *   else: a loop inside a pass that does not end is a pass that does not
 *   end.
 * - The watchdog is enabled at the first pass, after the boot path:
 *   supply_boot_off() takes up to 3,500ms and the boot's store reclaims a
 *   flash window each, before any output can be driven.  The boot path
 *   itself is not watched.
 * - Not while the image runs on trial after an update: the boot ROM owns
 *   the watchdog for 16.7s then, and an image that does not BUY in that
 *   time is replaced by the slot it came from.  Enabling it would reload
 *   the ROM's count.  The first pass after the BUY enables it.
 * - Never in the bench console build (IOMCU_CONSOLE=ON, not released):
 *   its driver test sleeps inside a command.
 * - Core 1 reads the sensor bus and is not watched.
 *
 * The period against the longest pass the code allows, in microseconds.
 * A flash window is 19,178us: an erase and a program in one window, 1
 * sample on the bring-up module (firmware/iomcu/src/out_store.h); a window
 * with one of the two is taken at the same figure.  The flash part's
 * datasheet maximum is not checked.  Each window waits up to
 * OUT_STORE_LOCKOUT_MS (10ms) for core 1 to stop and up to 10ms for it to
 * start again.
 *
 *   6 flash windows:  2 calls of can_service(), each one
 *                     update request with a sector erase and a
 *                     4KiB program; the update's owed erase;
 *                     the output store's save or reclaim        115,068
 *   their 12 lock-out waits of 10,000us                         120,000
 *   2 TONE rewires, TONE_CORE1_WAIT_US (5,000us) each            10,000
 *   can_drain() before the update's restart, 7 batches of
 *   CAN_TXQ_STUCK_US (5,000us)                                   35,000
 *   the store's line, PICO_STDIO_USB_STDOUT_TIMEOUT_US            1,000
 *   the rest of a pass, measured maximum                            250
 *                                                               -------
 *   LOOP_WATCH_WORST_PASS_US                                    281,318
 *
 * LOOP_WATCH_PERIOD_MS (500ms) is 1.78 times that.  No pass was measured
 * with all of them in it; the longest pass measured outside a flash window
 * is 250us.  One flash window longer than 218,682us over the figure above
 * restarts the IOmcu; the link's 200ms silence latches its failsafe
 * before that.
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

/** @brief One flash window, us: erase and program, measured once. */
#define LOOP_WATCH_WINDOW_US 19178u
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

/** @brief The longest pass the code allows, us: 281,318. */
#define LOOP_WATCH_WORST_PASS_US                                          \
    (LOOP_WATCH_WINDOWS * (LOOP_WATCH_WINDOW_US + 2u * LOOP_WATCH_LOCKOUT_US) \
     + LOOP_WATCH_TONES * LOOP_WATCH_TONE_US + LOOP_WATCH_DRAIN_US        \
     + LOOP_WATCH_PRINT_US + LOOP_WATCH_PASS_US)

/** @brief What the firmware does with the watchdog at the feed point. */
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
 * @brief The feed point: once at the top of every pass of the main loop.
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
