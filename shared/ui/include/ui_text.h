/*
 * The interface's words, one table per language.
 *
 * Every string a screen shows in the operator's language has an ID in
 * ui_text.def, which also holds its English.  A language is a table of the
 * same IDs (ui_text_de.c for German); an entry the table leaves NULL shows
 * the English, so a partial table is a working one.  What stays English in
 * every language -- ARM, DISARM, STOP, the screen titles, protocol and mode
 * names, units, and the field's own terms -- is not in the table at all and
 * is written where it is drawn.
 *
 * Text is UTF-8; gfx decodes it and draws one cell per code point
 * (gfx.h).  A translated format string must convert the same arguments in
 * the same order as its English: test_text holds every table to that.
 *
 * The language is SET_LANGUAGE, applied by settings_apply_ui() with the
 * theme, so a change on SETUP repaints every screen in the new language on
 * the next frame.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#include "servo_test.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** In SET_LANGUAGE's order. */
typedef enum {
    UI_LANG_EN = 0,
    UI_LANG_DE,
    UI_LANG_COUNT
} ui_lang_t;

typedef enum {
#define UI_TEXT(id, cells, en) TX_##id,
#include "ui_text.def"
#undef UI_TEXT
    TX_COUNT
} ui_text_id_t;

/**
 * A language.  Every table is indexed by the ID beside it and may be NULL
 * or hold NULL entries, each of which shows the English.
 */
typedef struct {
    const char *const *text;             /**< [TX_COUNT]                  */
    const char *const *setting_label;    /**< [SETTING_COUNT]             */
    const char *const *setting_help;     /**< [SETTING_COUNT]             */
    /** [SETTING_COUNT], each an array of that setting's option_count. */
    const char *const *const *setting_options;
    const char *const *category;         /**< [SET_CAT_COUNT]             */
    const char *const *servo;            /**< [SERVO_STR_COUNT]           */
} ui_language_t;

extern const ui_language_t ui_lang_de;

/** Show @p lang from now on; one out of range is English. */
void ui_text_set_language(ui_lang_t lang);
ui_lang_t ui_text_language(void);

/** @p id in the language showing; "" for an ID out of range. */
const char *ui_tr(ui_text_id_t id);
#define TR(id) ui_tr(TX_##id)

/**
 * Entry @p i of @p set in the language showing, padded with spaces to the
 * cells of the longest entry of @p set, into @p buf.  For a label one of
 * several replaces in place: each lays out the same width, so one repaints
 * over another without a clear.
 */
const char *ui_tr_pad(const ui_text_id_t *set, int count, int i, char *buf,
                      size_t n);

/** @p id in @p lang, falling back to English as ui_tr() does. */
const char *ui_tr_in(ui_lang_t lang, ui_text_id_t id);

/** Whether @p lang's own table has @p id, rather than falling back. */
int ui_text_has(ui_lang_t lang, ui_text_id_t id);

/**
 * The cells @p id may take where it is drawn, from ui_text.def, or 0 when
 * the fit check measures it on a rendered screen instead.
 */
int ui_text_cells(ui_text_id_t id);

/** The ID's name without the TX_ prefix, for the fit check's messages. */
const char *ui_text_name(ui_text_id_t id);

/** The table of @p lang; NULL for English. */
const ui_language_t *ui_text_table(ui_lang_t lang);

/* ---------------------------------------------------- the settings schema */

/*
 * The schema in shared/settings is English and is the fallback.  These
 * return a setting's words in the language showing.
 */
const char *ui_setting_label(setting_id_t id);
const char *ui_setting_help(setting_id_t id);
const char *ui_setting_category(setting_cat_t cat);
/** As settings_value_text(), with ON, OFF and the options translated. */
const char *ui_setting_value(setting_id_t id, char *buf, size_t n);
/** Option @p k of an enum setting; "?" for one out of range. */
const char *ui_setting_option(setting_id_t id, int k);

/* ----------------------------------------------------- the servo test's */

/**
 * The servo test's words for the screen and the report, in the language
 * showing.  servo_str() stays English: it writes the CSV, which tools read.
 */
const char *ui_servo_str(servo_str_t id);

/** The servo table to hand a run for its report: NULL is English. */
const char *const *ui_servo_table(void);

/** ON or OFF in the language showing. */
const char *ui_on_off(int on);

#ifdef GFX_TEXT_TRACE
/** Defined by the fit check's harness: @p id was looked up. */
void ui_text_trace(ui_text_id_t id);
#endif

#ifdef __cplusplus
}
#endif
