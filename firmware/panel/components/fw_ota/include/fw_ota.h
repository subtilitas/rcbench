/**
 * @file fw_ota.h
 * @brief The application slots, as the installer and the start-up need
 *        them.
 *
 * The flash holds two application slots, ota_0 and ota_1, and otadata, which
 * names the one the bootloader starts.  An update writes the slot that is
 * not running (fw_ota_flash()), checks it and names it in otadata.  The
 * bootloader is built with rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE):
 * a new build's first start is a trial, and a start that resets before
 * fw_ota_boot_check() keeps it goes back to the build before it.  When the
 * panel calls it to keep the build is shared/fwupdate/fw_trial.h's rule
 * (firmware/panel/main/panel_trial.c).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_OTA_H
#define RCBENCH_FW_OTA_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_boot.h"
#include "fw_image.h"
#include "fw_install.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The slot an update writes.
 * @return Its bytes; 0 when the table has no second slot.
 */
uint32_t fw_ota_slot_bytes(void);

/**
 * @brief The running build: version and ELF (executable and linkable
 *        format) SHA-256, as fw_offer() compares them.
 * @param out Where they go.
 * @return False when its version does not parse.
 */
bool fw_ota_running(fw_image_info_t *out);

/**
 * @brief The slot that is not running, for fw_install_begin().
 * @return Its writer.
 */
const fw_flash_t *fw_ota_flash(void);

/**
 * @brief Before the restart into a written slot: what the next start
 *        reports when that build does not stay.  Written to NVS
 *        (non-volatile storage) and committed, so a restart, a panic and a
 *        power cut keep it; a refused write is a console warning, and that
 *        start then reports nothing.
 * @param version The written build's version.
 */
void fw_ota_note_restart(const char *version);

/**
 * @brief Whether the running build is on its first start after an update
 *        (pending verify), and fw_ota_boot_check() still settles it.
 * @return True on a trial start.
 */
bool fw_ota_trial_pending(void);

/**
 * @brief Once per start: with @p ui_up false at the display or touch failing, or
 * with @p ui_up true once the start is known to work (fw_trial.h).
 *
 * On a build's first start after an update: @p ui_up keeps it
 * (FW_BOOT_UPDATED, or FW_BOOT_UNCONFIRMED when otadata could not be
 * written), otherwise the panel restarts into the build before it and this
 * does not return (fw_boot_decide()).  After a start that went back:
 * FW_BOOT_ROLLED_BACK with the written build's version in @p other, when
 * fw_ota_note_restart() was called before it.  The note is erased after
 * every outcome but a trial that goes back (fw_boot_settle()).  An install
 * from a build that kept the note in LP (low-power) RAM leaves no NVS
 * note: that start reads none, as a panel that never installed does.
 *
 * Says one console line at INFO, before a trial that goes back restarts
 * and otherwise before it returns:
 * `start check: note <version|none|unreadable>, last invalid slot
 * <label|none>, result <NORMAL|UPDATED|ROLLED_BACK|UNCONFIRMED|GOING_BACK>`.
 *
 * @param ui_up     Whether the start is kept (true) or goes back (false).
 * @param other     Where the written build's version goes; may be NULL
 *                  with @p other_cap 0.
 * @param other_cap Bytes at @p other.
 * @return What the check found.
 */
fw_boot_t fw_ota_boot_check(bool ui_up, char *other, uint32_t other_cap);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_OTA_H */
