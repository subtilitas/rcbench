/**
 * @file
 * @brief The Wi-Fi page: the code form, the card listing and the upload
 *        answers in English and German. See wa_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_page.h"

#include <string.h>

#include "wa_guard.h"
#include "wa_zip.h"
#include "wall_clock.h"

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

static void head(wa_out_t *o, const wa_page_info_t *pi)
{
    wa_out_str(o, "<!doctype html><html lang=\"");
    wa_out_str(o, pi->de ? "de" : "en");
    wa_out_str(o, "\"><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" "
                  "content=\"width=device-width,initial-scale=1\"><title>");
    wa_out_html(o, pi->name);
    wa_out_str(o, "</title><style>"
                  "body{font-family:sans-serif;margin:16px;max-width:60em}"
                  "table{border-collapse:collapse;width:100%}"
                  "td,th{padding:4px 8px;text-align:left;"
                  "border-bottom:1px solid #ccc}"
                  "td.n{text-align:right;font-variant-numeric:tabular-nums}"
                  "h3{margin-bottom:4px;word-break:break-all}"
                  "a{word-break:break-all}"
                  "</style></head><body><h1>");
    wa_out_html(o, pi->name);
    wa_out_str(o, "</h1>");
}

void wa_page_code(wa_out_t *o, const wa_page_info_t *pi, bool wrong)
{
    head(o, pi);
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
                  "<input name=\"code\" maxlength=\"8\" size=\"10\" "
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

/* The folder heading and the table head of a folder's files. */
static void folder(wa_out_t *o, const wa_page_info_t *pi, const char *path)
{
    wa_out_str(o, "<h3>/");
    wa_out_html(o, path);
    wa_out_str(o, "</h3><table><tr><th>");
    wa_out_str(o, pt(pi->de, P_NAME));
    wa_out_str(o, "</th><th>");
    wa_out_str(o, pt(pi->de, P_BYTES));
    wa_out_str(o, "</th><th>");
    wa_out_str(o, pt(pi->de, P_MODIFIED));
    wa_out_str(o, "</th></tr>");
}

static void files(wa_out_t *o, const wa_page_info_t *pi, const wa_fs_t *fs)
{
    wa_out_str(o, "<h2>");
    wa_out_str(o, pt(pi->de, P_FILES));
    wa_out_str(o, "</h2>");
    wa_zip_plan_t plan;
    const wa_zip_err_t pe = wa_zip_plan(fs, &plan);
    if (pe != WA_ZIP_OK) {
        wa_out_str(o, "<p>");
        wa_out_str(o, pt(pi->de, P_NO_CARD));
        wa_out_str(o, "</p>");
        return;
    }
    if (plan.files == 0u) {
        wa_out_str(o, "<p>");
        wa_out_str(o, pt(pi->de, P_EMPTY));
        wa_out_str(o, "</p>");
    } else {
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

    static wa_walk_t w;   /* 1.6kB: off the calling task's stack */
    wa_walk_begin(&w, fs);
    bool open = false;
    for (;;) {
        const wa_walk_ev_t ev = wa_walk_next(&w);
        if (ev == WA_WALK_DIR) {
            if (open) {
                wa_out_str(o, "</table>");
            }
            folder(o, pi, w.path);
            open = true;
        } else if (ev == WA_WALK_FILE) {
            wa_out_str(o, "<tr><td><a href=\"" WA_URL_FILE);
            wa_out_url(o, w.path);
            wa_out_str(o, "\" download>");
            wa_out_html(o, w.ent.name);
            wa_out_str(o, "</a></td><td class=\"n\">");
            wa_out_u64(o, w.ent.size);
            wa_out_str(o, "</td><td>");
            /* A 1980 stamp is an unset clock's (wall_clock.h): an empty
             * cell rather than a date nobody wrote the file on. */
            if (wall_stamp_known(w.ent.mtime)) {
                wa_out_time(o, wall_local_s(w.ent.mtime));
            }
            wa_out_str(o, "</td></tr>");
        } else {
            if (open) {
                wa_out_str(o, "</table>");
            }
            if (ev != WA_WALK_END) {
                wa_out_str(o, "<p>");
                wa_out_str(o, pt(pi->de, P_NO_CARD));
                wa_out_str(o, "</p>");
            }
            break;
        }
        if (o->failed) {
            wa_walk_end(&w);   /* the browser went away */
            return;
        }
    }
    if (w.left_out > 0u) {
        wa_out_str(o, "<p>");
        wa_out_u64(o, w.left_out);
        wa_out_str(o, pt(pi->de, P_LEFT_OUT));
        wa_out_str(o, "</p>");
    }
}

void wa_page_main(wa_out_t *o, const wa_page_info_t *pi, const wa_fs_t *fs)
{
    head(o, pi);
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
