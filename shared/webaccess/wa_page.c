/*
 * The Wi-Fi page. See wa_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_page.h"

#include <string.h>

#include "wa_zip.h"

/* The page's texts, English and German. */
typedef enum {
    P_FIRMWARE = 0,
    P_INSTALLED,
    P_FW_NOTE,
    P_UPLOAD,
    P_TOO_BIG,
    P_BROKE,
    P_REFUSED,
    P_FILES,
    P_ALL,
    P_ALL_SIZE_1,
    P_ALL_SIZE_2,
    P_ALL_SIZE_3,
    P_NO_CARD,
    P_EMPTY,
    P_LEFT_OUT,
    P_NAME,
    P_BYTES,
    P_MODIFIED,
    P_CODE,
    P_CODE_NOTE,
    P_CODE_WRONG,
    P_OK,
    P_UP_OK_1,
    P_UP_OK_2,
    P_UP_SAME,
    P_UP_EMPTY,
    P_UP_TOO_BIG,
    P_UP_SHORT,
    P_UP_NOT_IMAGE,
    P_UP_CHIP,
    P_UP_PROJECT,
    P_UP_VERSION,
    P_UP_NO_HASH,
    P_UP_LENGTH,
    P_UP_HASH,
    P_UP_BUSY,
    P_UP_MEMORY,
    P_UP_OTHER,
    P_COUNT
} p_text_t;

static const char *const k_en[P_COUNT] = {
    [P_FIRMWARE]     = "Panel firmware",
    [P_INSTALLED]    = "Installed: ",
    /* One text in three literals: the initialisers are designated, so no
     * comma is missing. */
    /* NOLINTNEXTLINE(bugprone-suspicious-missing-comma) */
    [P_FW_NOTE]      = "Upload a panel image, rcbench-panel-vX.Y.Z.bin. The "
                       "panel checks it and asks. It installs only after "
                       "INSTALL is held for 2 s on the panel.",
    [P_UPLOAD]       = "Upload",
    [P_TOO_BIG]      = "The file is larger than the panel's slot.",
    [P_BROKE]        = "The upload broke off.",
    [P_REFUSED]      = "The panel did not take the upload: HTTP ",
    [P_FILES]        = "Files on the card",
    [P_ALL]          = "Download all as one ZIP file",
    [P_ALL_SIZE_1]   = " files, ",
    [P_ALL_SIZE_2]   = " bytes. The ZIP file is ",
    [P_ALL_SIZE_3]   = " bytes.",
    [P_NO_CARD]      = "No card, or the card cannot be read.",
    [P_EMPTY]        = "The card holds no files.",
    [P_LEFT_OUT]     = " entries are not listed: a name over 255 characters, "
                       "a path over 384 bytes, or a folder deeper than 16.",
    [P_NAME]         = "Name",
    [P_BYTES]        = "Bytes",
    [P_MODIFIED]     = "Modified",
    [P_CODE]         = "Access code",
    [P_CODE_NOTE]    = "Enter the code the panel's Wi-Fi screen shows.",
    [P_CODE_WRONG]   = "Wrong code.",
    [P_OK]           = "OK",
    [P_UP_OK_1]      = "Received and checked: panel firmware ",
    [P_UP_OK_2]      = ". To install, hold INSTALL on the panel for 2 s.",
    [P_UP_SAME]      = ". This is the build the panel runs; the panel "
                       "offers no install.",
    [P_UP_EMPTY]     = "The file is empty.",
    [P_UP_TOO_BIG]   = "The file is larger than the panel's slot.",
    [P_UP_SHORT]     = "The file is cut short.",
    [P_UP_NOT_IMAGE] = "This is not a panel firmware image.",
    [P_UP_CHIP]      = "The image is built for another chip.",
    [P_UP_PROJECT]   = "The image is another project's.",
    [P_UP_VERSION]   = "The image carries no release number.",
    [P_UP_NO_HASH]   = "The image is built without its SHA-256.",
    [P_UP_LENGTH]    = "The file is longer than the image it holds.",
    [P_UP_HASH]      = "The SHA-256 does not match: the file is damaged.",
    [P_UP_BUSY]      = "An image waits on the panel. CANCEL it there first.",
    [P_UP_MEMORY]    = "The panel has no memory for the image.",
    [P_UP_OTHER]     = "The panel refused the image.",
};

static const char *const k_de[P_COUNT] = {
    [P_FIRMWARE]     = "Panel-Firmware",
    [P_INSTALLED]    = "Installiert: ",
    /* NOLINTNEXTLINE(bugprone-suspicious-missing-comma) */
    [P_FW_NOTE]      = "Ein Panel-Image hochladen, rcbench-panel-vX.Y.Z.bin. "
                       "Das Panel prüft es und fragt. Es installiert erst, "
                       "wenn INSTALL am Panel 2 s gehalten wird.",
    [P_UPLOAD]       = "Hochladen",
    [P_TOO_BIG]      = "Die Datei ist größer als der Slot des Panels.",
    [P_BROKE]        = "Der Upload brach ab.",
    [P_REFUSED]      = "Das Panel hat den Upload nicht angenommen: HTTP ",
    [P_FILES]        = "Dateien auf der Karte",
    [P_ALL]          = "Alles als eine ZIP-Datei herunterladen",
    [P_ALL_SIZE_1]   = " Dateien, ",
    [P_ALL_SIZE_2]   = " Bytes. Die ZIP-Datei hat ",
    [P_ALL_SIZE_3]   = " Bytes.",
    [P_NO_CARD]      = "Keine Karte, oder die Karte ist nicht lesbar.",
    [P_EMPTY]        = "Auf der Karte sind keine Dateien.",
    [P_LEFT_OUT]     = " Einträge fehlen in der Liste: ein Name über 255 "
                       "Zeichen, ein Pfad über 384 Bytes oder ein Ordner "
                       "tiefer als 16.",
    [P_NAME]         = "Name",
    [P_BYTES]        = "Bytes",
    [P_MODIFIED]     = "Geändert",
    [P_CODE]         = "Zugangscode",
    [P_CODE_NOTE]    = "Den Code eingeben, den der WLAN-Bildschirm des "
                       "Panels zeigt.",
    [P_CODE_WRONG]   = "Falscher Code.",
    [P_OK]           = "OK",
    [P_UP_OK_1]      = "Empfangen und geprüft: Panel-Firmware ",
    [P_UP_OK_2]      = ". Zum Installieren INSTALL am Panel 2 s halten.",
    [P_UP_SAME]      = ". Das ist der Build, der auf dem Panel läuft; das "
                       "Panel bietet keine Installation an.",
    [P_UP_EMPTY]     = "Die Datei ist leer.",
    [P_UP_TOO_BIG]   = "Die Datei ist größer als der Slot des Panels.",
    [P_UP_SHORT]     = "Die Datei ist abgeschnitten.",
    [P_UP_NOT_IMAGE] = "Das ist kein Panel-Firmware-Image.",
    [P_UP_CHIP]      = "Das Image ist für einen anderen Chip gebaut.",
    [P_UP_PROJECT]   = "Das Image gehört zu einem anderen Projekt.",
    [P_UP_VERSION]   = "Das Image trägt keine Release-Nummer.",
    [P_UP_NO_HASH]   = "Das Image ist ohne seinen SHA-256 gebaut.",
    [P_UP_LENGTH]    = "Die Datei ist länger als das Image darin.",
    [P_UP_HASH]      = "Der SHA-256 stimmt nicht: die Datei ist beschädigt.",
    [P_UP_BUSY]      = "Am Panel wartet ein Image. Erst dort ABBRECHEN.",
    [P_UP_MEMORY]    = "Das Panel hat keinen Speicher für das Image.",
    [P_UP_OTHER]     = "Das Panel hat das Image abgelehnt.",
};

static const char *pt(bool de, p_text_t t)
{
    return de ? k_de[t] : k_en[t];
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

    static wa_walk_t w;   /* 1.6 kB: off the calling task's stack */
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
            wa_out_time(o, w.ent.mtime);
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
