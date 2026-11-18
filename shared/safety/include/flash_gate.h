/**
 * @file
 * @brief When the panel may write its own flash, and when the IOmcu may not
 *        write its own.
 *
 * CONFIG_SPI_FLASH_AUTO_SUSPEND is off on the panel, so an erase or a write
 * of its main flash disables the cache and stalls both cores for its length:
 * code that is not in IRAM (instruction RAM) runs nowhere.  The control task
 * is not in IRAM.  It beats the safety line, whose ceiling is
 * HEARTBEAT_MAX_GAP_MS (150ms), and it carries STOP, OUTPUT OFF and the
 * supply's trips.  One 4kB sector erase is tens of milliseconds; a bench
 * that is armed, or whose supply is live, must not wait for one.
 *
 * So a flash write starts only on a bench that is disarmed with the supply
 * off.  Every writer of the panel's flash asks this one rule: the settings
 * save before it commits a page of NVS (non-volatile storage), and the board
 * drawing's keeper before each sector.  A write that is not allowed waits;
 * arming and the supply are never refused for one.
 *
 * The IOmcu's rule is flash_gate_busy(), for its output store and its
 * firmware update alike.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FLASH_GATE_H
#define RCBENCH_FLASH_GATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether a flash operation may start now.
 *
 * True only when both @p armed and @p supply are false.
 *
 * @param armed true when the bank is armed, an arm is waiting for the line
 *        to settle (arming_link_needed()), or an ARM is queued and not yet
 *        taken (flash_gate_arm()): a stall inside the settle can cost the
 *        far end's trust in the heartbeat and so the arm.
 * @param supply true when the supply's output is on, or an ON for it is queued
 *        and not yet taken.
 * @return true when a flash operation may start.
 */
bool flash_gate_open(bool armed, bool supply);

/**
 * @brief flash_gate_open()'s @p armed for a writer on another task than the
 *        one that arms.
 *
 * The panel's screens queue an ARM for the control task.  The sender counts
 * the ARM in @p arms_sent before it queues it.  The control task counts it
 * in @p arms_taken once it has dealt with it, taken, refused or lost, and
 * only after it has published what became of it (@p armed, @p arm_waits).
 * A reader that loads @p arms_taken before @p armed and @p arm_waits never
 * finds both saying nothing is coming while an arm is under way.
 *
 * The counters are free-running; only their equality is read, so a wrap of
 * the 32-bit count changes nothing.
 *
 * @param armed      true when the bank is armed.
 * @param arm_waits  true when an arm is waiting for the line to settle.
 * @param arms_sent  ARMs counted when queued.
 * @param arms_taken ARMs counted when dealt with.
 * @return true when armed, an arm waits, or an ARM is queued and not yet
 *         dealt with.
 */
bool flash_gate_arm(bool armed, bool arm_waits, unsigned arms_sent,
                    unsigned arms_taken);

/**
 * @brief Whether the IOmcu's flash may not be written now, for the store and
 *        for the firmware update alike.
 *
 * The bank driving: a window stops this core and steps no output.  The supply
 * asked on or perhaps on: its UART replies would overrun the 8-byte FIFO in a
 * 19ms window, and an OFF asked over the link would wait for it.  A KST
 * session: a flash write stalls the pass that steps it.
 *
 * False only when all four arguments are false.
 *
 * @param driving true when the bank is driving (outputs_driving()).
 * @param supply_on true when the SUPPLY page's output register is non-zero.
 * @param pd_on true when the PD mini is open and may be on
 *        (pdmini_may_be_on()).
 * @param kst_session true when the KST port is out of PWM.
 * @return true when the flash may not be written.
 */
bool flash_gate_busy(bool driving, bool supply_on, bool pd_on,
                     bool kst_session);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FLASH_GATE_H */
