/**
 * @file panel_devupd.h
 * @brief The wireless devices' update on the panel: the images this panel
 *        carries, their check, the rows of SETUP > UPDATES' devices' page
 *        (DIAL FIRMWARE, POWER NODE FIRMWARE, BALANCE NODE FIRMWARE), and
 *        the transfer over Bluetooth LE as a client of the `blec` task
 *        (shared/devupd).
 *
 * The images: the M5Stack Dial's, the power node's and the balance node's,
 * each embedded at build time (main/CMakeLists.txt) and checked once in
 * the `blec` task after the start: an application of the device's chip and
 * project that fits its slot, with its appended SHA-256 right, and the
 * device's role in its head (bleota_head_check()); then the SHA-256 of the
 * whole image that BEGIN carries.  A row offers its hold only once its
 * image passed.
 *
 * An update: the 2s hold on a row asks the `blec` task to start it for
 * that device (devupd_gate_ask()).  The task holds the device's own link
 * ended (panel_dial_blec_hold(), panel_pnode_blec_hold(),
 * panel_bal_blec_hold()), connects the update client to the bound device
 * once that link is down, and runs the transfer; the device asks for its
 * confirmation before it erases anything: a press of the Dial's screen
 * button, a node's BOOT key held 1s.  At the end, installed or not, the
 * client lets go of the device and its own client connects again from
 * SETUP's values; a node's hold counts no loss, so the band shows no
 * node lost.  The device's version comes back from its own client: the
 * Dial's INFO, a node's STATUS.  One update runs at a time.  ARM is
 * refused from the hold to the end (panel_devupd_busy(), a
 * devupd_gate_t), and the `blec` task refuses the start when the bench is
 * ARMED or an arm waits, at the take and again before the update client
 * connects.  A link that ends after END leaves the install's result to
 * the version the device reports once it is back (devupd_settle()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleuart.h"

/** @brief Chunks written in one poll of the `blec` task at most. */
#define PANEL_DEVUPD_CHUNKS 4

/** @brief Set up the client; before panel_blec_start(). */
void panel_devupd_start(void);

/**
 * @brief The `blec` task's turn, first: the images' check, a start a row
 *        asked for, the device's hold, the end.
 * @param now The clock, ms.
 */
void panel_devupd_blec_poll(uint32_t now);

/**
 * @brief An event of the host stack the hub routes to the update client.
 * @param e   The event.
 * @param now The clock, ms.
 */
void panel_devupd_blec_event(const bleu_evt_t *e, uint32_t now);

/**
 * @brief The client's next action for the hub.
 * @param now The clock, ms.
 * @param out Receives it.
 * @return true when @p out holds one.
 */
bool panel_devupd_blec_step(uint32_t now, bleu_act_t *out);

/**
 * @brief The chunks of this poll to DATA, at most PANEL_DEVUPD_CHUNKS; a
 *        chunk NimBLE has no buffer for goes again at the next poll.
 * @param now The clock, ms.
 */
void panel_devupd_blec_chunks(uint32_t now);

/**
 * @brief The `blec` task's turn, last: what the frame task shows.
 * @param now The clock, ms.
 */
void panel_devupd_blec_publish(uint32_t now);

/**
 * @brief SETUP > UPDATES' device rows and the band's lines of an update,
 *        once a frame on app_main after c6_service().
 * @param armed ARMED, or an arm waiting.
 */
void devupd_service(bool armed);

/**
 * @brief Whether a device update runs or was asked for: ARM is refused and
 *        the C6 update waits.  Any task.
 * @return true from the hold to the update's end.
 */
bool panel_devupd_busy(void);
