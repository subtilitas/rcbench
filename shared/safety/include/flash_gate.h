/*
 * When the panel may write its own flash, and when the IOmcu may not
 * write its own.
 *
 * CONFIG_SPI_FLASH_AUTO_SUSPEND is off on the panel, so an erase or a write
 * of its main flash disables the cache and stalls both cores for its length:
 * code that is not in IRAM (instruction RAM) runs nowhere.  The control task
 * is not in IRAM.  It beats the safety line, whose ceiling is
 * HEARTBEAT_MAX_GAP_MS (150 ms), and it carries STOP, OUTPUT OFF and the
 * supply's trips.  One 4 kB sector erase is tens of milliseconds; a bench
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
 * Whether a flash operation may start now.
 *
 * @p armed   the bank is armed, or an arm is waiting for the line to settle
 *            (arming_link_needed()): a stall inside the settle can cost the
 *            far end's trust in the heartbeat and so the arm.
 * @p supply  the supply's output is on, or an ON for it is queued and not
 *            yet taken.
 *
 * True only when both are false.
 */
bool flash_gate_open(bool armed, bool supply);

/**
 * Whether the IOmcu's flash may not be written now, for the store and
 * for the firmware update alike.  The bank driving: a window stops this core
 * and steps no output.  The supply asked on or perhaps on: its UART replies
 * would overrun the 8-byte FIFO in a 19 ms window, and an OFF asked over the
 * link would wait for it.  A KST session: a flash write stalls the pass that
 * steps it.
 *
 * @p driving      the bank is driving (outputs_driving()).
 * @p supply_on    the SUPPLY page's output register is non-zero.
 * @p pd_on        the PD mini is open and may be on (pdmini_may_be_on()).
 * @p kst_session  the KST port is out of PWM.
 *
 * False only when all four are false.
 */
bool flash_gate_busy(bool driving, bool supply_on, bool pd_on,
                     bool kst_session);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FLASH_GATE_H */
