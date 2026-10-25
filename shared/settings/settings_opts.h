/**
 * @file
 * @brief The option lists of the enum settings, in English.
 *
 * One array per UI_OPTIONS() of ui_text.def, settings_opts_<name>, with
 * its length, settings_opts_n_<name>.  Defined in settings_text.c and bound
 * to their settings by the schema rows of settings.c and
 * settings_sensors.c.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/** @brief settings_opts_n_<name>, the count of a list's UI_OPTION() rows,
 *  and the declaration of settings_opts_<name> with that length. */
#define UI_OPTIONS(name) enum { settings_opts_n_##name = 0
/* A term of the sum, not an expression of its own. */
/* NOLINTNEXTLINE(bugprone-macro-parentheses) */
/// @cond X_MACRO
#define UI_OPTION(en, de) + 1
#define UI_OPTIONS_END(name)                                               \
    };                                                                     \
    extern const char *const settings_opts_##name[settings_opts_n_##name];
#include "ui_text.def"
/// @endcond
