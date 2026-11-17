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
#include "fw_run.h"

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
 * @brief Note an update run before a restart (fw_run_begin()): the next
 *        start running @p expect finds the run with FW_RUN_OPENS
 *        openings.  Written to NVS beside the restart note, key
 *        `run_note`, and committed.  Says one console line:
 *        `run note: <version>, 2 openings`, or a warning when no note was
 *        written.  The C6 radio's update calls it on success with the
 *        running version.  app_main's task only.
 * @param expect The version the next start is expected to run.
 * @return False when the text or NVS refused it.
 */
bool fw_ota_run_begin(const char *expect);

/**
 * @brief After the install's last step: note the run when the image is
 *        written whole and checked (fw_run_after_install()), before
 *        fw_ota_note_restart() and the restart.  Any other status writes
 *        and says nothing.  app_main's task only.
 * @param st      The install's last fw_install_step() status.
 * @param version The written build's version.
 * @return True when the run was noted.
 */
bool fw_ota_run_installed(fw_install_status_t st, const char *version);

/**
 * @brief Whether an update run stands in this start: the run note named
 *        the running build and this start spent one of its openings
 *        (fw_run_settle(), run by fw_ota_boot_check() once the start is
 *        settled), and fw_ota_run_end() has not ended it.  False before
 *        the start check.  app_main's task only.
 * @return True while the run stands.
 */
bool fw_ota_run_active(void);

/**
 * @brief Whether this start has read its run note (fw_run_settle()): at
 *        once on a start that is no trial, when the trial is kept on one
 *        that is (up to FW_TRIAL_BOUND_MS, 30s, after the start).  Until
 *        then fw_ota_run_active() is false although a run may stand, and
 *        the start-up check waits.  app_main's task only.
 * @return True once the note was read.
 */
bool fw_ota_run_settled(void);

/**
 * @brief End the update run: fw_ota_run_active() is false for the rest of
 *        this start, and the note is erased (fw_run_end()).  A refused
 *        erase is a console warning; the note left then spends its
 *        openings at the next starts.  app_main's task only.
 */
void fw_ota_run_end(void);

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
 * Once the start is settled, the run note is read once (fw_run_settle()
 * with the running version): see fw_ota_run_active().  It says a line
 * `run note: ...` when it found an entry.
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
