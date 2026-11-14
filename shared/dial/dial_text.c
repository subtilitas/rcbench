/**
 * @file
 * @brief The Dial faces' words by language, from ui_text.def.  See
 *        include/dial_text.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial_text.h"

#include <stddef.h>

#include "dialknob.h"

/** @brief The English column. */
static const char *const k_en[DIAL_TEXT_COUNT] = {
/// @cond X_MACRO
#define UI_DIAL_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
/// @endcond
};

/** @brief The German column; NULL shows the English. */
static const char *const k_de[DIAL_TEXT_COUNT] = {
/// @cond X_MACRO
#define UI_DIAL_TEXT(id, en, de) [id] = (de),
#include "ui_text.def"
/// @endcond
};

/** @brief The IDs' names. */
static const char *const k_name[DIAL_TEXT_COUNT] = {
/// @cond X_MACRO
#define UI_DIAL_TEXT(id, en, de) [id] = #id,
#include "ui_text.def"
/// @endcond
};

const char *dial_text(uint8_t lang, dial_text_id_t id)
{
    if ((int)id < 0 || id >= DIAL_TEXT_COUNT) {
        return "";
    }
    if (lang == DK_LANG_DE && k_de[id] != NULL) {
        return k_de[id];
    }
    return k_en[id];
}

const char *dial_text_name(dial_text_id_t id)
{
    if ((int)id < 0 || id >= DIAL_TEXT_COUNT) {
        return "";
    }
    return k_name[id];
}
