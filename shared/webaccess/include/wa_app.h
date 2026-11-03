/**
 * @file
 * @brief The file manager of the Wi-Fi page: the card in two panes, INFO,
 *        a read-only text viewer and the writes, as a script in the page
 *        that calls the file API (wa_api.h).
 *
 * The script and its style sheet are shared/webaccess/web/app.js, hl.js,
 * write.js and app.css, carried as C strings (wa_web_blob.h).  This writes them
 * into the page with what they read from the C side:
 *
 *   - the texts, in the page's language: FILES' own (UI_TEXT entries of
 *     ui_text.def, so a button reads as on the panel) and the page's
 *     (UI_WEB_TEXT);
 *   - the highlighter's tables (text_hl.h): the class names of
 *     TEXT_HL_CLASS_ROWS, the unit table, TEXT_HL_LABEL_CELLS, and from
 *     the panel the verdict words and the span cap its viewer uses;
 *   - a colour per class and the text's background, for the browser's
 *     light and dark schemes, from the panel's light and dark palettes.
 *
 * The data is one JSON (JavaScript Object Notation) element, a script
 * element of type application/json and id fm-data:
 *
 * @verbatim
   {"tx":{"rescan":"RESCAN",...},
    "hl":{"cls":["tx","hd",...],"units":[["V",6],...],"ok":[...],
          "fail":[...],"warn":[...],"cap":128,"label":24}}
   @endverbatim
 *
 * Each verdict word is written as its core (text_hl_word_core()), empty
 * cores left out, so the script compares bytes as text_hl_line() does.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_APP_H
#define RCBENCH_WA_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "text_hl.h"
#include "wa_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Colours of one palette: one per class, then the background. */
#define WA_HL_RGB (TEXT_HL_CLASSES + 1u)
/** @brief The span cap without a wa_hl_t: the panel viewer's
 *         TEXT_VIEW_SPANS, which test_wa_hl holds it to. */
#define WA_HL_SPANS 128u

/** @brief What the panel's viewer classes and colours a line with. */
typedef struct {
    const text_hl_words_t *words; /**< the verdict words; NULL for none  */
    size_t spans;                 /**< spans a line is classed into      */
    uint32_t dark[WA_HL_RGB];     /**< 0xRRGGBB of the dark palette      */
    uint32_t light[WA_HL_RGB];    /**< 0xRRGGBB of the light palette     */
} wa_hl_t;

/**
 * @brief The file manager's style rules, for the page's style element:
 *        the colours of both palettes as CSS (cascading style sheet)
 *        variables, --h-tx and so on by each class's style name and --h-bk
 *        for the background, one rule per class, then app.css.
 * @param o   the output
 * @param hl  the colours; NULL writes app.css alone
 */
void wa_app_style(wa_out_t *o, const wa_hl_t *hl);

/**
 * @brief The file manager's element, its data and its scripts.
 * @param o   the output
 * @param de  German texts
 * @param hl  the words and the span cap; NULL for no words and a cap of
 *            WA_HL_SPANS
 */
void wa_app_body(wa_out_t *o, bool de, const wa_hl_t *hl);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_APP_H */
