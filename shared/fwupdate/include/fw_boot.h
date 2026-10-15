/*
 * The first start after an update: keep the new build, or go back.
 *
 * The bootloader is built with rollback: a build's first start is a trial
 * (pending verify), and a restart while it still is starts the build
 * before it.  fw_boot_decide() is the order of the calls that settle the
 * trial; the calls themselves are the panel's (fw_ota.c), so the host
 * suite runs the order against fakes.
 *
 * A failed start is never kept.  When marking it invalid returns -- the
 * call restarts on success -- the panel restarts anyway, which leaves the
 * trial unconfirmed and so goes back too.  Marking a good start valid can
 * fail: the build then runs this once, and the next start goes back.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_BOOT_H
#define RCBENCH_FW_BOOT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FW_BOOT_NORMAL = 0,
    FW_BOOT_UPDATED,      /**< this build's first start, now kept            */
    FW_BOOT_ROLLED_BACK,  /**< the written build did not start; this one runs */
    FW_BOOT_UNCONFIRMED,  /**< first start not kept: the next start goes back */
} fw_boot_t;

typedef struct {
    /** True when the running build is on its first start (a trial). */
    bool (*pending)(void *ctx);
    /** Mark the running build invalid and restart; returns only on failure. */
    void (*mark_invalid)(void *ctx);
    /** Keep the running build; false when otadata could not be written. */
    bool (*mark_valid)(void *ctx);
    /** Restart; does not return on the panel. */
    void (*restart)(void *ctx);
    /** True when a slot is marked invalid by a start that went back. */
    bool (*last_invalid)(void *ctx);
    void *ctx;
} fw_boot_ops_t;

/**
 * Settle the start: @p ui_up is whether the display and touch came up,
 * @p noted whether a restart into a written build was noted before this
 * start (fw_ota_note_restart()).
 *
 * A trial with @p ui_up false is marked invalid; mark_valid is never called
 * after that, whatever mark_invalid and restart return.  Without a trial,
 * FW_BOOT_ROLLED_BACK needs both @p noted and a slot marked invalid.
 */
fw_boot_t fw_boot_decide(const fw_boot_ops_t *ops, bool ui_up, bool noted);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_BOOT_H */
