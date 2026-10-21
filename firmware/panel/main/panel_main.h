/**
 * @file panel_main.h
 * @brief What main.c shares with the four files split from it.
 *
 * The four files: the start-up
 * (panel_boot.c), the settings the control task follows
 * (panel_settings.c), the control task (panel_control.c) and the render
 * loop (panel_frame.c).  Everything else the panel's files share is in
 * panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "panel.h"
#include "settings.h"

/* ------------------------------------------------------------ panel_boot.c */

/**
 * @brief Bring the board up and report each step to the splash.
 * @return false when a step the bench cannot run without failed (no
 *         display, no touch, an IOmcu that speaks another protocol); the
 *         splash names it.
 */
bool bring_up(void);

/** @brief The start-up's CAN (Controller Area Network) echo test passed;
 *         until it says otherwise, true. */
extern bool s_bus_ok;
/** @brief touch_init() answered. */
extern bool s_touch_ok;
/** @brief What the start check found, for the band once the splash has
 *         gone. */
extern char s_boot_alert[ALERT_MAX];

/* -------------------------------------------------------- panel_settings.c */

/**
 * @brief The settings model's observer for values: hands the changed one
 *        to the code that follows it.
 * @param id The setting that changed.
 */
void settings_changed(setting_id_t id);

/**
 * @brief The settings model's observer for texts: hands a log folder to
 *        the log writer.
 * @param id The text setting that changed.
 */
void publish_log_folder(setting_text_id_t id);

/** @brief Each value once, at the start-up, whatever the observer was
 *         told. */
void publish_poles(void);
void publish_endpoints(void);
void publish_pdmini(void);
void publish_tone(void);
void publish_log_auto(void);
void publish_hitec_rx(void);

/** @brief AUTO LOGS, for the control task's log_follow_runs()
 *         (main.c). */
extern atomic_bool s_log_auto;

/* --------------------------------------------------------- panel_control.c */

/** @brief Set the heartbeat's pin up as an output, low. */
void heartbeat_init(void);

/** @brief Start the die temperature sensor. */
void tsens_init(void);

/**
 * @brief The control task; app_main creates it on core 1.
 * @param arg Unused.
 */
void control_task(void *arg);

/** @brief Whether a STOP is drawn to press, from the render loop to the
 *         control task's hit test. */
extern atomic_bool s_stop_live;

/* ----------------------------------------------------------- panel_frame.c */

/** @brief The render loop.  Does not return. */
void panel_frame_loop(void);
