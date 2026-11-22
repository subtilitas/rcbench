/**
 * @file panel_sbk.h
 * @brief The settings backup on the panel: the render loop's side and the
 *        logger task's side of its card requests (panel_sbk.c).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

/**
 * @brief The render loop's frame: the driver fed (sbk_tick()), a request
 *        handed to the logger, an answer handed to the driver.
 * @param committed The settings were written to NVS (non-volatile storage)
 *                  this frame: settings_save_tick() answered true.
 * @param armed     The bench is ARMED, an arm waits or an ARM is queued
 *                  (arm_live_or_coming()), read after the router's tick:
 *                  a load whose answer arrives then is refused.
 */
void panel_sbk_frame(bool committed, bool armed);

/**
 * @brief Run the request handed over, on the logger task; nothing when none
 *        waits.
 */
void panel_sbk_card_serve(void);

/**
 * @brief Whether a request is out: handed over and not answered.
 * @return true while one is.
 */
bool panel_sbk_busy(void);
