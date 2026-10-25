/**
 * @file
 * @brief The schema's words in English: the settings' labels, help lines
 *        and option lists, from the one text table, ui_text.def
 *        (shared/text).
 *
 * The categories' names are settings.c's.  ui_text.c gives every
 * language's.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings.h"
#include "settings_opts.h"
#include "settings_sensors.h"

#include <stddef.h>

/* The schema rows of settings_sensors.c give these lists' lengths as
 * numbers. */
_Static_assert((int)settings_opts_n_watch == (int)SENS_W_COUNT,
               "a channel watches one of SENS_W_COUNT targets");
_Static_assert(settings_opts_n_old_addr == 4 && settings_opts_n_old_ch == 2,
               "the kept INA3221 rows name 4 addresses and 2 channel sets");

/** @brief settings_opts_<name>: each option list's English. */
/// @cond X_MACRO
#define UI_OPTIONS(name) const char *const settings_opts_##name[] = {
#define UI_OPTION(en, de) (en),
#define UI_OPTIONS_END(name) };
#include "ui_text.def"
/// @endcond

/** @brief The settings' labels in English, by setting. */
static const char *const k_labels[SETTING_COUNT] = {
/// @cond X_MACRO
#define UI_SETTING_LABEL(id, en, de) [id] = (en),
#include "ui_text.def"
/// @endcond
};

/** @brief The settings' help lines in English, by setting. */
static const char *const k_helps[SETTING_COUNT] = {
/// @cond X_MACRO
#define UI_SETTING_HELP(id, en, de) [id] = (en),
#include "ui_text.def"
/// @endcond
};

/** @brief The option lists in the order of ui_text.def's UI_OPTIONS(),
 *  which a language's options follow. */
static const char *const *const k_lists[] = {
/// @cond X_MACRO
#define UI_OPTIONS(name) settings_opts_##name,
#include "ui_text.def"
/// @endcond
};

const char *settings_label(setting_id_t id)
{
    return (id >= 0 && id < SETTING_COUNT && k_labels[id] != NULL)
               ? k_labels[id] : "";
}

const char *settings_help(setting_id_t id)
{
    return (id >= 0 && id < SETTING_COUNT && k_helps[id] != NULL)
               ? k_helps[id] : "";
}

int settings_option_list(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || d->options == NULL) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(k_lists) / sizeof(k_lists[0]); ++i) {
        if (k_lists[i] == d->options) {
            return (int)i;
        }
    }
    return -1;
}
