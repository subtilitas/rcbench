/**
 * @file
 * @brief What settings.c and settings_schema.c share: the schema table.
 *
 * Not for any other module; the others read a row through settings_def().
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One row per setting, indexed by setting_id_t
 *         (settings_schema.c).  settings_def() answers the SENSORS rows
 *         from settings_sensors.c in their place. */
extern const setting_def_t settings_schema[SETTING_COUNT];

#ifdef __cplusplus
}
#endif
