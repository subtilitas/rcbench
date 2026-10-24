/**
 * @file panel_trial.h
 * @brief A new build's first start, kept once the panel works as a whole.
 *
 * The bootloader starts a new build once on trial; a panic, a watchdog
 * reset, a restart or a power cut before the build is kept starts the
 * build before it (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).  The steps the
 * keep waits for and its bound are shared/fwupdate/fw_trial.h's; this is
 * where the panel reports them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Settle the start's check at the display and touch step.
 *
 * Without the display or touch (@p ui_up false) a trial start goes back to
 * the build before it here and does not return.  A start that is no trial
 * is settled here too: the band's line for a build that went back.  A
 * trial start with both up stays on trial until panel_trial_frame() keeps
 * it.
 *
 * @param ui_up Whether the display and touch came up.
 */
void panel_trial_begin(bool ui_up);

/**
 * @brief Record start-up steps (FW_TRIAL_CARD, FW_TRIAL_SETTINGS) from
 *        app_main.
 * @param steps One or more FW_TRIAL_* steps.
 */
void panel_trial_mark(uint32_t steps);

/**
 * @brief Once per pass of the screen loop, after the flip: keep the build
 *        when fw_trial_poll() says so.
 * @param flushed Whether this pass's frame reached the display.
 * @param quiet   Whether the bench can take a flash write now
 *                (flash_gate_open(), as the settings save asks).
 */
void panel_trial_frame(bool flushed, bool quiet);

/** @brief The control task finished one pass; set by that task. */
extern atomic_bool s_trial_control_ran;
/** @brief The logger task finished one pass; set by that task. */
extern atomic_bool s_trial_logger_ran;
