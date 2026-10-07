/*
 * The string tables and the lookup.  See include/ui_text.h.
 *
 * A lookup is two loads and a test: the language's entry, else the
 * English.  The language is a pointer read once per string, so the control
 * task's alerts read it from the other core without a lock: a change lands
 * between two lookups and never inside one.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_text.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"

static const char *const k_en[TX_COUNT] = {
#define UI_TEXT(id, cells, en) [TX_##id] = (en),
#include "ui_text.def"
#undef UI_TEXT
};

static const unsigned char k_cells[TX_COUNT] = {
#define UI_TEXT(id, cells, en) [TX_##id] = (unsigned char)(cells),
#include "ui_text.def"
#undef UI_TEXT
};

static const char *const k_names[TX_COUNT] = {
#define UI_TEXT(id, cells, en) [TX_##id] = #id,
#include "ui_text.def"
#undef UI_TEXT
};

static const ui_language_t *const k_langs[UI_LANG_COUNT] = {
    [UI_LANG_EN] = NULL,
    [UI_LANG_DE] = &ui_lang_de,
};

static ui_lang_t s_lang = UI_LANG_EN;

void ui_text_set_language(ui_lang_t lang)
{
    s_lang = ((unsigned)lang < (unsigned)UI_LANG_COUNT) ? lang : UI_LANG_EN;
}

ui_lang_t ui_text_language(void) { return s_lang; }

const ui_language_t *ui_text_table(ui_lang_t lang)
{
    return ((unsigned)lang < (unsigned)UI_LANG_COUNT) ? k_langs[lang] : NULL;
}

/* @p table's entry @p i, or NULL when it has none. */
static const char *entry(const char *const *table, int i)
{
    return (table != NULL) ? table[i] : NULL;
}

const char *ui_tr_in(ui_lang_t lang, ui_text_id_t id)
{
    if ((unsigned)id >= (unsigned)TX_COUNT) {
        return "";
    }
    const ui_language_t *l = ui_text_table(lang);
    const char *t = (l != NULL) ? entry(l->text, (int)id) : NULL;
    return (t != NULL) ? t : k_en[id];
}

const char *ui_tr(ui_text_id_t id)
{
#ifdef GFX_TEXT_TRACE
    ui_text_trace(id);
#endif
    return ui_tr_in(s_lang, id);
}

const char *ui_tr_pad(const ui_text_id_t *set, int count, int i, char *buf,
                      size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (set == NULL || count <= 0) {
        return buf;
    }
    int cells = 0;
    for (int k = 0; k < count; ++k) {
        const int w = gfx_text_cells(ui_tr(set[k]));
        cells = (w > cells) ? w : cells;
    }
    const char *t = ui_tr(set[(i >= 0 && i < count) ? i : 0]);
    /* The width is in bytes, so the pad is the cells short of the longest
     * on top of the string's own bytes. */
    snprintf(buf, n, "%-*s", (int)strlen(t) + cells - gfx_text_cells(t), t);
    return buf;
}

int ui_text_has(ui_lang_t lang, ui_text_id_t id)
{
    if ((unsigned)id >= (unsigned)TX_COUNT) {
        return 0;
    }
    const ui_language_t *l = ui_text_table(lang);
    return (l == NULL) || entry(l->text, (int)id) != NULL;
}

int ui_text_cells(ui_text_id_t id)
{
    return ((unsigned)id < (unsigned)TX_COUNT) ? (int)k_cells[id] : 0;
}

const char *ui_text_name(ui_text_id_t id)
{
    return ((unsigned)id < (unsigned)TX_COUNT) ? k_names[id] : "";
}

/* ---------------------------------------------------- the settings schema */

const char *ui_setting_label(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return "";
    }
    const ui_language_t *l = ui_text_table(s_lang);
    const char *t = (l != NULL) ? entry(l->setting_label, (int)id) : NULL;
    return (t != NULL) ? t : d->label;
}

const char *ui_setting_help(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return "";
    }
    const ui_language_t *l = ui_text_table(s_lang);
    const char *t = (l != NULL) ? entry(l->setting_help, (int)id) : NULL;
    return (t != NULL) ? t : d->help;
}

const char *ui_setting_category(setting_cat_t cat)
{
    if ((unsigned)cat >= (unsigned)SET_CAT_COUNT) {
        return "";
    }
    const ui_language_t *l = ui_text_table(s_lang);
    const char *t = (l != NULL) ? entry(l->category, (int)cat) : NULL;
    return (t != NULL) ? t : settings_category_name(cat);
}

const char *ui_setting_option(setting_id_t id, int k)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || d->options == NULL || k < 0 || k >= d->option_count) {
        return "?";
    }
    const ui_language_t *l = ui_text_table(s_lang);
    const char *const *opts = (l != NULL && l->setting_options != NULL)
                                  ? l->setting_options[id] : NULL;
    return (opts != NULL && opts[k] != NULL) ? opts[k] : d->options[k];
}

const char *ui_on_off(int on)
{
    return on ? TR(ON) : TR(OFF);
}

const char *ui_setting_value(setting_id_t id, char *buf, size_t n)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || buf == NULL || n == 0u) {
        return settings_value_text(id, buf, n);
    }
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", ui_on_off(settings_get_bool(id)));
        return buf;
    }
    if (d->type == SET_TYPE_ENUM) {
        const int i = settings_get_int(id);
        const ui_language_t *l = ui_text_table(s_lang);
        const char *const *opts = (l != NULL && l->setting_options != NULL)
                                      ? l->setting_options[id] : NULL;
        if (opts != NULL && i >= 0 && i < d->option_count
            && opts[i] != NULL) {
            snprintf(buf, n, "%s", opts[i]);
            return buf;
        }
    }
    return settings_value_text(id, buf, n);
}

/* ----------------------------------------------------- the servo test's */

const char *const *ui_servo_table(void)
{
    const ui_language_t *l = ui_text_table(s_lang);
    return (l != NULL) ? l->servo : NULL;
}

const char *ui_servo_str(servo_str_t id)
{
    return servo_str_in(ui_servo_table(), id);
}
