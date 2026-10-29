/**
 * @file
 * @brief The file manager of the Wi-Fi page as C strings: app.css, hl.js
 *        and app.js of shared/webaccess/web, which tools/gen_web_blob.py
 *        writes into wa_web_blob.c.  Each is an array of pieces in order,
 *        ended by NULL; wa_app.c writes them into the page.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_WEB_BLOB_H
#define RCBENCH_WA_WEB_BLOB_H

/** @brief app.css: the file manager's style sheet. */
extern const char *const wa_web_css[];
/** @brief hl.js: the viewer's highlighter (text_hl.c's rules). */
extern const char *const wa_web_hl_js[];
/** @brief app.js: the two panes, INFO and the viewer. */
extern const char *const wa_web_app_js[];

#endif /* RCBENCH_WA_WEB_BLOB_H */
