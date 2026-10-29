/**
 * @file
 * @brief The file manager of the Wi-Fi page: its texts, tables, colours and
 *        scripts written into the page. See wa_app.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_app.h"

#include <stdio.h>
#include <string.h>

#include "wa_web_blob.h"

/*
 * Each entry of ui_text.def as a static pair { English, German }: T_<ID>
 * for a screen's text, W_<ID> for the page's.  The pairs the table below
 * does not name are dropped by the compiler.
 */
/** @brief A screen's text as T_<ID>. */
#define UI_TEXT(id, cells, en, de) \
    __attribute__((unused)) static const char *const T_##id[2] = { (en), (de) };
/** @brief A text of the page as W_<ID>. */
#define UI_WEB_TEXT(id, en, de) \
    __attribute__((unused)) static const char *const W_##id[2] = { (en), (de) };
#include "ui_text.def"

/** @brief One text of the app: its key in the data and its entry. */
typedef struct {
    const char *key;        /**< the key app.js reads it by       */
    const char *const *t;   /**< { English, German }; German NULL
                                 for the English                  */
} app_text_t;

/** @brief The texts app.js reads, FILES' own where the panel has one. */
static const app_text_t k_texts[] = {
    { "sd", T_LOG_SD_CARD },           { "root", T_LOG_TAB_ROOT },
    { "prev", T_LOG_PREV },            { "next", T_LOG_NEXT },
    { "rescan", T_LOG_RESCAN },        { "open", T_LOG_OPEN },
    { "info", T_FM_INFO },             { "close", T_FM_CLOSE },
    { "n", T_FM_ENTRIES },             { "one", T_FM_ENTRY_ONE },
    { "of", T_FM_ENTRIES_OF },         { "unl", T_FM_UNLISTED },
    { "reading", T_FM_READING },       { "empty", T_FM_EMPTY },
    { "nofolder", T_FM_NO_FOLDER },    { "hidden", T_FM_HIDDEN },
    { "k.FOLDER", T_FM_KIND_FOLDER },  { "k.RUN", T_FM_KIND_RUN },
    { "k.REPORT", T_FM_KIND_REPORT },  { "k.KST", T_FM_KIND_KST },
    { "k.PROFILE", T_FM_KIND_PROFILE }, { "k.FAULT", T_FM_KIND_FAULT },
    { "k.CSV", T_FM_KIND_CSV },        { "k.JSON", T_FM_KIND_JSON },
    { "k.TEXT", T_FM_KIND_TEXT },      { "k.OTHER", T_FM_KIND_OTHER },
    { "writing", T_FM_WRITING },       { "locked", T_FM_LOCKED },
    { "fixed", T_FM_FIXED },           { "i.file", T_FM_INFO_FILE },
    { "i.name", T_FM_INFO_NAME },      { "i.folder", T_FM_INFO_FOLDER },
    { "i.kind", T_FM_INFO_KIND },      { "i.size", T_FM_INFO_SIZE },
    { "i.time", T_FM_INFO_TIME },      { "i.entries", T_FM_INFO_ENTRIES },
    { "i.rule", T_FM_INFO_RULE },      { "i.count", T_FM_INFO_COUNT },
    { "i.unl", T_FM_INFO_UNLISTED },   { "i.notime", T_FM_INFO_NO_TIME },
    { "i.reading", T_FM_INFO_READING }, { "r.FREE", T_FM_RULE_FREE },
    { "r.FIXED", T_FM_RULE_FIXED },    { "r.LOCKED", T_FM_RULE_LOCKED },
    { "r.FAULT", T_FM_RULE_FAULT },    { "r.BUSY", T_FM_RULE_BUSY },
    { "select", T_FM_SELECT_FIRST },   { "ro", T_ED_READ_ONLY },
    { "line", T_ED_LINE_OF },          { "mixed", T_ED_MIXED_ENDS },
    { "vreading", T_ED_READING },      { "vempty", T_ED_EMPTY },
    { "e.BUSY", W_P_API_BUSY },        { "e.LOCKED", W_P_API_LOCKED },
    { "e.TOO_BIG", W_P_API_TOO_BIG },  { "e.ARG", W_P_API_ARG },
    { "up", W_P_FM_UP },               { "dl", W_P_FM_DOWNLOAD },
    { "wait", W_P_FM_WAIT },           { "noans", W_P_FM_NO_ANSWER },
};

/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its style sheet name. */
#define APP_CSS(name, css, role) css,

/** @brief Each class's style sheet name, by class. */
static const char *const k_css[TEXT_HL_CLASSES] = {
    TEXT_HL_CLASS_ROWS(APP_CSS)
};

/**
 * @brief A text in a language.
 * @param t   { English, German }
 * @param de  German
 * @return the German, or the English where the German is NULL
 */
static const char *in(const char *const *t, bool de)
{
    return (de && t[1] != NULL) ? t[1] : t[0];
}

/**
 * @brief A JSON string that stays inside a script element: quotes,
 *        backslashes and control bytes escaped, and "<" as < so no
 *        text ends the element.
 * @param o  the output
 * @param s  the text, @p n bytes
 * @param n  its bytes
 */
static void js_str(wa_out_t *o, const char *s, size_t n)
{
    static const char k_hex[] = "0123456789abcdef";
    wa_out_str(o, "\"");
    for (size_t i = 0u; i < n; ++i) {
        const unsigned char b = (unsigned char)s[i];
        if (b == '\\' || b == '"') {
            const char esc[2] = { '\\', (char)b };
            wa_out_raw(o, esc, 2u);
        } else if (b < 0x20u || b == 0x7Fu || b == '<') {
            const char esc[6] = { '\\', 'u', '0', '0', k_hex[b >> 4],
                                  k_hex[b & 0x0Fu] };
            wa_out_raw(o, esc, 6u);
        } else {
            wa_out_raw(o, s + i, 1u);
        }
    }
    wa_out_str(o, "\"");
}

/**
 * @brief Every piece of a file of the blob.
 * @param o       the output
 * @param pieces  the pieces, ended by NULL
 */
static void blob(wa_out_t *o, const char *const *pieces)
{
    for (size_t i = 0u; pieces[i] != NULL; ++i) {
        wa_out_str(o, pieces[i]);
    }
}

/**
 * @brief A colour as "#rrggbb".
 * @param o    the output
 * @param rgb  0xRRGGBB
 */
static void colour(wa_out_t *o, uint32_t rgb)
{
    char hex[8];
    (void)snprintf(hex, sizeof(hex), "#%06lx",
                   (unsigned long)(rgb & 0xFFFFFFu));
    wa_out_str(o, hex);
}

/**
 * @brief The variables of one palette: one per class by its style name,
 *        --h-tx and so on, then --h-bk.
 * @param o    the output
 * @param rgb  WA_HL_RGB colours
 */
static void palette(wa_out_t *o, const uint32_t *rgb)
{
    for (size_t k = 0u; k < TEXT_HL_CLASSES; ++k) {
        wa_out_str(o, "--h-");
        wa_out_str(o, k_css[k]);
        wa_out_str(o, ":");
        colour(o, rgb[k]);
        wa_out_str(o, ";");
    }
    wa_out_str(o, "--h-bk:");
    colour(o, rgb[TEXT_HL_CLASSES]);
}

void wa_app_style(wa_out_t *o, const wa_hl_t *hl)
{
    wa_out_str(o, ":root{color-scheme:light dark");
    if (hl != NULL) {
        wa_out_str(o, ";");
        palette(o, hl->light);
        wa_out_str(o, "}@media (prefers-color-scheme:dark){:root{");
        palette(o, hl->dark);
        wa_out_str(o, "}");
    }
    wa_out_str(o, "}");
    for (size_t k = 0u; k < TEXT_HL_CLASSES; ++k) {
        wa_out_str(o, ".h-");
        wa_out_str(o, k_css[k]);
        wa_out_str(o, "{color:var(--h-");
        wa_out_str(o, k_css[k]);
        wa_out_str(o, ")}");
    }
    blob(o, wa_web_css);
}

/**
 * @brief A list of verdict words as a JSON array of their cores.
 * @param o     the output
 * @param list  the words; NULL entries skipped
 * @param n     their count
 */
static void words(wa_out_t *o, const char *const *list, size_t n)
{
    bool first = true;
    wa_out_str(o, "[");
    for (size_t i = 0u; list != NULL && i < n; ++i) {
        size_t len = 0u;
        const char *w = (list[i] != NULL)
                            ? text_hl_word_core(list[i], &len) : "";
        if (len == 0u) {
            continue;
        }
        wa_out_str(o, first ? "" : ",");
        js_str(o, w, len);
        first = false;
    }
    wa_out_str(o, "]");
}

/**
 * @brief The highlighter's tables as a JSON object.
 * @param o   the output
 * @param hl  the words and the cap, or NULL
 */
static void tables(wa_out_t *o, const wa_hl_t *hl)
{
    wa_out_str(o, "{\"cls\":[");
    for (size_t k = 0u; k < TEXT_HL_CLASSES; ++k) {
        wa_out_str(o, (k == 0u) ? "" : ",");
        js_str(o, k_css[k], strlen(k_css[k]));
    }
    wa_out_str(o, "],\"units\":[");
    for (size_t k = 0u; k < TEXT_HL_UNIT_COUNT; ++k) {
        wa_out_str(o, (k == 0u) ? "[" : ",[");
        js_str(o, text_hl_units[k].unit, strlen(text_hl_units[k].unit));
        wa_out_str(o, ",");
        wa_out_u64(o, (uint64_t)text_hl_units[k].cls);
        wa_out_str(o, "]");
    }
    const text_hl_words_t *w = (hl != NULL) ? hl->words : NULL;
    wa_out_str(o, "],\"ok\":");
    words(o, (w != NULL) ? w->ok : NULL, (w != NULL) ? w->ok_n : 0u);
    wa_out_str(o, ",\"fail\":");
    words(o, (w != NULL) ? w->fail : NULL, (w != NULL) ? w->fail_n : 0u);
    wa_out_str(o, ",\"warn\":");
    words(o, (w != NULL) ? w->warn : NULL, (w != NULL) ? w->warn_n : 0u);
    wa_out_str(o, ",\"cap\":");
    wa_out_u64(o, (hl != NULL) ? hl->spans : WA_HL_SPANS);
    wa_out_str(o, ",\"label\":");
    wa_out_u64(o, TEXT_HL_LABEL_CELLS);
    wa_out_str(o, "}");
}

void wa_app_body(wa_out_t *o, bool de, const wa_hl_t *hl)
{
    wa_out_str(o, "<div id=\"fm\" class=\"fm\"></div><noscript><p>");
    wa_out_html(o, in(W_P_FM_NOSCRIPT, de));
    wa_out_str(o, "</p></noscript>"
                  "<script type=\"application/json\" id=\"fm-data\">"
                  "{\"tx\":{");
    for (size_t i = 0u; i < sizeof(k_texts) / sizeof(k_texts[0]); ++i) {
        const char *t = in(k_texts[i].t, de);
        wa_out_str(o, (i == 0u) ? "" : ",");
        js_str(o, k_texts[i].key, strlen(k_texts[i].key));
        wa_out_str(o, ":");
        js_str(o, t, strlen(t));
    }
    wa_out_str(o, "},\"hl\":");
    tables(o, hl);
    wa_out_str(o, "}</script><script>");
    blob(o, wa_web_hl_js);
    wa_out_str(o, "</script><script>");
    blob(o, wa_web_app_js);
    wa_out_str(o, "</script>");
}
