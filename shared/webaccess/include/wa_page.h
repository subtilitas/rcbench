/**
 * @file
 * @brief The Wi-Fi page: one HTML (hypertext markup language) page with no
 *        scripts or styles from elsewhere, so it works on a phone with no
 *        other connection.
 *
 * Two parts.  Panel firmware: a file picker and an upload with progress,
 * then the panel's answer as text; the page never starts the write, the
 * operator does on the panel with a 2s hold.  Card files: one link for the
 * whole card as a ZIP file with its size given before the click, and the
 * file manager (wa_app.h), which shows the card in two panes through the
 * file API, each file a download and a text file viewed read-only.
 *
 * With the access code on, a browser without the code gets only the code
 * form.  Texts in English or German, as the panel's language setting.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_PAGE_H
#define RCBENCH_WA_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "fw_image.h"
#include "wa_app.h"
#include "wa_text.h"
#include "wa_walk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The paths the page uses. */
#define WA_URL_CODE    "/code"
#define WA_URL_UPDATE  "/update"
#define WA_URL_FILE    "/card/"
#define WA_URL_ZIP     "/card.zip"
/** The cookie that carries the access code. */
#define WA_COOKIE      "rcbench_code"

typedef struct {
    const char *name;      /**< "rcbench-1A2B"                        */
    const char *version;   /**< the panel firmware running            */
    uint32_t    slot;      /**< the largest image the panel takes     */
    bool        de;        /**< German texts                          */
    const wa_hl_t *hl;     /**< the viewer's words and colours; NULL
                                for none                              */
} wa_page_info_t;

/** The code form; @p wrong adds that the last code was wrong. */
void wa_page_code(wa_out_t *o, const wa_page_info_t *pi, bool wrong);

/**
 * @brief The whole page: the firmware upload, the ZIP link and the file
 *        manager (wa_app.h).
 * @param o   the output
 * @param pi  the page's name, version, slot, language and highlighter
 * @param fs  the card, walked once for the ZIP file's sizes
 */
void wa_page_main(wa_out_t *o, const wa_page_info_t *pi, const wa_fs_t *fs);

/**
 * The text an upload's response carries: @p e as fw_upload_end() or
 * fw_upload_begin() gave it, and for FW_OK the version received in
 * @p info, which @p same says is the build that runs.
 */
void wa_page_upload_result(wa_out_t *o, bool de, fw_err_t e,
                           const fw_image_info_t *info, bool same);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_PAGE_H */
