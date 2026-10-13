/*
 * When the panel may write its own flash.
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

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FLASH_GATE_H */
