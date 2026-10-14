/*
 * The application slots, as the installer and the start-up need them.
 *
 * The flash holds two application slots, ota_0 and ota_1, and otadata, which
 * names the one the bootloader starts.  An update writes the slot that is
 * not running (fw_ota_flash()), checks it and names it in otadata.  The
 * bootloader is built with rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE):
 * a new build's first start is a trial, and a start that does not reach
 * fw_ota_boot_check() with the display and touch up goes back to the build
 * before it at the next restart.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_OTA_H
#define RCBENCH_FW_OTA_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"
#include "fw_install.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes of the slot an update writes; 0 when the table has no second slot. */
uint32_t fw_ota_slot_bytes(void);

/** The running build: version and ELF (executable and linkable format)
 *  SHA-256, as fw_offer() compares them.  False when its version does not
 *  parse. */
bool fw_ota_running(fw_image_info_t *out);

/** The slot that is not running, for fw_install_begin(). */
const fw_flash_t *fw_ota_flash(void);

/**
 * Before the restart into a written slot: what the next start reports when
 * that build does not stay.  @p version is the written build's.  Kept in
 * memory a software restart keeps and a power cut clears.
 */
void fw_ota_note_restart(const char *version);

typedef enum {
    FW_BOOT_NORMAL = 0,
    FW_BOOT_UPDATED,      /**< this build's first start, now kept           */
    FW_BOOT_ROLLED_BACK,  /**< the written build did not start; this one runs */
} fw_boot_t;

/**
 * Once, after the display and touch came up (@p ui_up), or did not.
 *
 * On a build's first start after an update: @p ui_up keeps it
 * (FW_BOOT_UPDATED), otherwise the panel restarts into the build before it
 * and this does not return.  After a start that went back:
 * FW_BOOT_ROLLED_BACK with the written build's version in @p other, when
 * fw_ota_note_restart() was called before it and no power cut came between.
 */
fw_boot_t fw_ota_boot_check(bool ui_up, char *other, uint32_t other_cap);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_OTA_H */
