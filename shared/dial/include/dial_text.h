/**
 * @file
 * @brief The Dial faces' own words in English and German, read from the
 *        UI_DIAL_TEXT() entries of the interface's one text table
 *        (shared/text/include/ui_text.def).
 *
 * The panel's language reaches the Dial in the SCREEN packet; the faces
 * draw their words in it.  What stays English in every language (SAFE,
 * ARMED, DISARM, the screen names, the units) is written where it is
 * drawn.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The faces' words, DIAL_<ID> for each UI_DIAL_TEXT() entry. */
typedef enum {
/// @cond X_MACRO
#define UI_DIAL_TEXT(id, en, de) id,
#include "ui_text.def"
/// @endcond
    DIAL_TEXT_COUNT
} dial_text_id_t;

/**
 * @brief A word in a language.
 * @param lang DK_LANG_EN or DK_LANG_DE; another is English
 * @param id   the word
 * @return the text; "" for an ID out of range
 */
const char *dial_text(uint8_t lang, dial_text_id_t id);

/**
 * @brief The ID's name, "DIAL_LINK_LOST", for a test's message.
 * @param id the word
 * @return the name; "" for an ID out of range
 */
const char *dial_text_name(dial_text_id_t id);

#ifdef __cplusplus
}
#endif
