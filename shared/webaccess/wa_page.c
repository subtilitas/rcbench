/**
 * @file
 * @brief The Wi-Fi page: the code form, the file manager and the upload
 *        answers in English and German. See wa_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_page.h"

#include <string.h>

#include "wa_app.h"
#include "wa_guard.h"
#include "wa_zip.h"

/** @brief The page's texts, P_<ID> for each UI_WEB_TEXT() of ui_text.def. */
typedef enum {
/** @brief One identifier per UI_WEB_TEXT() entry. */
#define UI_WEB_TEXT(id, en, de) id,
#include "ui_text.def"
    P_COUNT
} p_text_t;

/** @brief The page's texts in English. */
static const char *const k_en[P_COUNT] = {
/** @brief One English entry per identifier. */
#define UI_WEB_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
};

#ifndef WA_PAGE_DE
/**
 * @brief The German entry of text @p id as the table gives it.
 *
 * test_wa_page_de defines it before it includes this file, to blank one
 * entry.
 */
#define WA_PAGE_DE(id, de) (de)
#endif

/** @brief The page's texts in German; NULL where the table has no German. */
static const char *const k_de[P_COUNT] = {
/** @brief One German entry per identifier. */
#define UI_WEB_TEXT(id, en, de) [id] = WA_PAGE_DE(id, de),
#include "ui_text.def"
};

/**
 * @brief One text of the page in the page's language.
 *
 * @param de German page.
 * @param t  The text.
 * @return The German entry, or the English where the German is NULL.
 */
static const char *pt(bool de, p_text_t t)
{
    return (de && k_de[t] != NULL) ? k_de[t] : k_en[t];
}

/**
 * @brief The page's head and its first heading.
 * @param o    the output
 * @param pi   the page's name and language
 * @param app  with the file manager's style rules
 */
static void head(wa_out_t *o, const wa_page_info_t *pi, bool app)
{
    wa_out_str(o, "<!doctype html><html lang=\"");
    wa_out_str(o, pi->de ? "de" : "en");
    wa_out_str(o, "\"><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" "
                  "content=\"width=device-width,initial-scale=1\"><title>");
    wa_out_html(o, pi->name);
    /* The page's colour tokens, the panel's theme colours (ui_theme.c):
     * page PANEL, text TEXT, dim TEXT_DIM, faint TEXT_FAINT, line EDGE,
     * highlight ACCENT, link POWER, warn WARN; the selection is ACCENT at
     * 20% and the shade behind a box black at 53%.  The light theme's, and
     * the dark theme's when the browser asks for a dark scheme.  Every
     * colour of the page is one of them (app.css); test_wa_hl holds them
     * to ui_theme. */
    wa_out_str(o, "</title><style>"
                  ":root{color-scheme:light dark;--pg-bg:#fcfdff;"
                  "--pg-fg:#0a121c;--pg-dim:#46586c;--pg-faint:#788a9e;"
                  "--pg-ln:#aabacc;--pg-hi:#00c4e8;--pg-ac:#0084ba;"
                  "--pg-warn:#f6a800;--pg-sel:#00c4e833;--pg-shade:#0008}"
                  "@media (prefers-color-scheme:dark){:root{"
                  "--pg-bg:#0e1621;--pg-fg:#e8f8ff;--pg-dim:#84acca;"
                  "--pg-faint:#486a86;--pg-ln:#3a749c;--pg-hi:#00f0ff;"
                  "--pg-ac:#00d0ff;--pg-warn:#ffba00;--pg-sel:#00f0ff33}}"
                  "body{font-family:sans-serif;margin:16px;max-width:72em;"
                  "background:var(--pg-bg);color:var(--pg-fg)}"
                  "table{border-collapse:collapse;width:100%}"
                  "td,th{padding:4px 8px;text-align:left;"
                  "border-bottom:1px solid var(--pg-ln)}"
                  "h3{margin-bottom:4px;word-break:break-all}"
                  "a{color:var(--pg-ac);word-break:break-all}");
    if (app) {
        wa_app_style(o, pi->hl);
    }
    wa_out_str(o, "</style></head><body><h1>");
    wa_out_html(o, pi->name);
    wa_out_str(o, "</h1>");
}

void wa_page_code(wa_out_t *o, const wa_page_info_t *pi, bool wrong)
{
    head(o, pi, false);
    wa_out_str(o, "<h2>");
    wa_out_str(o, pt(pi->de, P_CODE));
    wa_out_str(o, "</h2><p>");
    wa_out_str(o, pt(pi->de, P_CODE_NOTE));
    wa_out_str(o, "</p>");
    if (wrong) {
        wa_out_str(o, "<p><b>");
        wa_out_str(o, pt(pi->de, P_CODE_WRONG));
        wa_out_str(o, "</b></p>");
    }
    wa_out_str(o, "<form method=\"post\" action=\"" WA_URL_CODE "\">"
                  "<input name=\"code\" maxlength=\"63\" size=\"24\" "
                  "autocomplete=\"off\" autocapitalize=\"none\" "
                  "spellcheck=\"false\" autofocus> <button>");
    wa_out_str(o, pt(pi->de, P_OK));
    wa_out_str(o, "</button></form></body></html>");
}

static void firmware(wa_out_t *o, const wa_page_info_t *pi)
{
    wa_out_str(o, "<h2>");
    wa_out_str(o, pt(pi->de, P_FIRMWARE));
    wa_out_str(o, "</h2><p>");
    wa_out_str(o, pt(pi->de, P_INSTALLED));
    wa_out_html(o, pi->version);
    wa_out_str(o, "</p><p>");
    wa_out_str(o, pt(pi->de, P_FW_NOTE));
    wa_out_str(o, "</p><form id=\"f\"><input type=\"file\" id=\"i\" "
                  "accept=\".bin\" required> <button>");
    wa_out_str(o, pt(pi->de, P_UPLOAD));
    wa_out_str(o, "</button></form>"
                  "<p><progress id=\"p\" max=\"100\" value=\"0\" hidden>"
                  "</progress></p><p id=\"r\"></p><script>"
                  "document.getElementById('f').onsubmit=function(e){"
                  "e.preventDefault();"
                  "var f=document.getElementById('i').files[0],"
                  "p=document.getElementById('p'),"
                  "r=document.getElementById('r');"
                  "if(!f)return;if(f.size>");
    wa_out_u64(o, pi->slot);
    wa_out_str(o, "){r.textContent='");
    wa_out_js(o, pt(pi->de, P_TOO_BIG));
    wa_out_str(o, "';return;}"
                  "var x=new XMLHttpRequest();x.open('POST','" WA_URL_UPDATE
                  "');x.setRequestHeader('" WA_UPLOAD_HDR "','" WA_UPLOAD_VAL
                  "');p.hidden=false;p.value=0;r.textContent='';"
                  "x.upload.onprogress=function(v){if(v.lengthComputable)"
                  "p.value=Math.floor(v.loaded*100/v.total);};"
                  "x.onload=function(){var t=x.responseText;p.hidden=true;"
                  "p.value=0;if(t===''&&x.status!==200)t='");
    wa_out_js(o, pt(pi->de, P_REFUSED));
    wa_out_str(o, "'+x.status+'.';r.textContent=t;};"
                  "x.onerror=function(){p.hidden=true;p.value=0;"
                  "r.textContent='");
    wa_out_js(o, pt(pi->de, P_BROKE));
    wa_out_str(o, "';};x.send(f);};</script>");
}

/**
 * @brief The card's part: the ZIP link with its sizes, then the file
 *        manager (wa_app.h).  The ZIP's sizes take one walk of the card;
 *        the file manager reads its folders a page at a time.
 * @param o   the output
 * @param pi  the page's language and highlighter
 * @param fs  the card
 */
static void files(wa_out_t *o, const wa_page_info_t *pi, const wa_fs_t *fs)
{
    wa_out_str(o, "<h2>");
    wa_out_str(o, pt(pi->de, P_FILES));
    wa_out_str(o, "</h2>");
    /* No card: no ZIP link; the panes say so, and RESCAN reads the card
     * once it is in. */
    wa_zip_plan_t plan;
    const bool card = (wa_zip_plan(fs, &plan) == WA_ZIP_OK);
    if (card && plan.files == 0u) {
        wa_out_str(o, "<p>");
        wa_out_str(o, pt(pi->de, P_EMPTY));
        wa_out_str(o, "</p>");
    } else if (card) {
        wa_out_str(o, "<p><a href=\"" WA_URL_ZIP "\" download>");
        wa_out_str(o, pt(pi->de, P_ALL));
        wa_out_str(o, "</a><br>");
        wa_out_u64(o, plan.files);
        wa_out_str(o, pt(pi->de, P_ALL_SIZE_1));
        wa_out_u64(o, plan.file_bytes);
        wa_out_str(o, pt(pi->de, P_ALL_SIZE_2));
        wa_out_u64(o, plan.bytes);
        wa_out_str(o, pt(pi->de, P_ALL_SIZE_3));
        wa_out_str(o, "</p>");
    }
    wa_app_body(o, pi->de, pi->hl);
}

void wa_page_main(wa_out_t *o, const wa_page_info_t *pi, const wa_fs_t *fs)
{
    head(o, pi, true);
    firmware(o, pi);
    files(o, pi, fs);
    wa_out_str(o, "</body></html>");
}

void wa_page_upload_result(wa_out_t *o, bool de, fw_err_t e,
                           const fw_image_info_t *info, bool same)
{
    p_text_t t;
    switch (e) {
    case FW_OK:
        wa_out_str(o, pt(de, P_UP_OK_1));
        if (info != NULL) {
            char v[FW_TEXT_MAX + 1u];
            memcpy(v, info->version_text, FW_TEXT_MAX);
            v[FW_TEXT_MAX] = '\0';
            wa_out_str(o, v);
        }
        wa_out_str(o, pt(de, same ? P_UP_SAME : P_UP_OK_2));
        return;
    case FW_ERR_EMPTY:     t = P_UP_EMPTY;     break;
    case FW_ERR_TOO_BIG:   t = P_UP_TOO_BIG;   break;
    case FW_ERR_SHORT:     t = P_UP_SHORT;     break;
    case FW_ERR_NOT_IMAGE:
    case FW_ERR_NO_DESC:   t = P_UP_NOT_IMAGE; break;
    case FW_ERR_CHIP:      t = P_UP_CHIP;      break;
    case FW_ERR_PROJECT:   t = P_UP_PROJECT;   break;
    case FW_ERR_VERSION:   t = P_UP_VERSION;   break;
    case FW_ERR_NO_HASH:   t = P_UP_NO_HASH;   break;
    case FW_ERR_LENGTH:    t = P_UP_LENGTH;    break;
    case FW_ERR_HASH:      t = P_UP_HASH;      break;
    case FW_ERR_READ:      t = P_BROKE;        break;
    case FW_ERR_BUSY:      t = P_UP_BUSY;      break;
    case FW_ERR_MEMORY:    t = P_UP_MEMORY;    break;
    case FW_ERR_RUNNING:
    case FW_ERR_FLASH:
    case FW_ERR_COUNT:
    default:               t = P_UP_OTHER;     break;
    }
    wa_out_str(o, pt(de, t));
}
