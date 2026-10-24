/**
 * @file
 * @brief The string tables and the lookup.  See include/ui_text.h.
 *
 * A lookup is two loads and a test: the language's entry, else the
 * English.  The language is an atomic int, read once per string, so the
 * control task's alerts read it from the other core without a lock: a change
 * lands between two lookups and never inside one.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_text.h"
#include "ui_card_text.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"

/** @brief The screens' strings in English, by TX_ ID. */
static const char *const k_en[TX_COUNT] = {
#define UI_TEXT(id, cells, en, de) [TX_##id] = (en),
#include "ui_text.def"
};

/** @brief The cells each string's field has, 0 where --fit measures it. */
static const unsigned char k_cells[TX_COUNT] = {
#define UI_TEXT(id, cells, en, de) [TX_##id] = (unsigned char)(cells),
#include "ui_text.def"
};

/** @brief The IDs' names, for the host's checks; the panel image carries
 *  none. */
#ifndef ESP_PLATFORM
static const char *const k_names[TX_COUNT] = {
#define UI_TEXT(id, cells, en, de) [TX_##id] = #id,
#include "ui_text.def"
};
#endif

/* ------------------------------------------------- the German column */

/**
 * @brief Picks the German column of an entry of ui_text.def.
 *
 * Each language but English is a block like this one: its tables are
 * ui_text.def's entries with its pick choosing the language's column.  A
 * language added to the table adds a column to every entry, a parameter to
 * every entry macro and a block of its own.
 */
#define DE(en, de) (de)

/** @brief The screens' strings in German, by TX_ ID; NULL shows the
 *  English. */
static const char *const k_de_text[TX_COUNT] = {
#define UI_TEXT(id, cells, en, de) [TX_##id] = DE(en, de),
#include "ui_text.def"
};

/** @brief The settings' labels in German, by setting ID. */
static const char *const k_de_label[SETTING_COUNT] = {
#define UI_SETTING_LABEL(id, en, de) [id] = DE(en, de),
#include "ui_text.def"
};

/** @brief The settings' help lines in German, by setting ID. */
static const char *const k_de_help[SETTING_COUNT] = {
#define UI_SETTING_HELP(id, en, de) [id] = DE(en, de),
#include "ui_text.def"
};

/** @brief The settings categories' names in German. */
static const char *const k_de_category[SET_CAT_COUNT] = {
#define UI_SETTING_CATEGORY(id, en, de) [id] = DE(en, de),
#include "ui_text.def"
};

/** @brief One array k_de_opts_<name> per option list, in German. */
#define UI_OPTIONS(name) static const char *const k_de_opts_##name[] = {
#define UI_OPTION(en, de) DE(en, de),
#define UI_OPTIONS_END(name) };
#include "ui_text.def"

/** @brief The German option lists, in the order of the table, which is
 *  the order settings_option_list() counts. */
static const char *const *const k_de_options[] = {
#define UI_OPTIONS(name) k_de_opts_##name,
#include "ui_text.def"
};

/** @brief The servo test's words in German, by SERVO_STR_ ID. */
static const char *const k_de_servo[SERVO_STR_COUNT] = {
#define UI_SERVO_TEXT(id, en, de) [id] = DE(en, de),
#include "ui_text.def"
};

#undef DE

/** @brief The German tables, as ui_text.h declares them. */
const ui_language_t ui_lang_de = {
    .text          = k_de_text,
    .setting_label = k_de_label,
    .setting_help  = k_de_help,
    .options       = k_de_options,
    .category      = k_de_category,
    .servo         = k_de_servo,
};

static const ui_language_t *const k_langs[UI_LANG_COUNT] = {
    [UI_LANG_EN] = NULL,
    [UI_LANG_DE] = &ui_lang_de,
};

/* Written by the render task, read by it and by the control task. */
static atomic_int s_lang = UI_LANG_EN;

void ui_text_set_language(ui_lang_t lang)
{
    atomic_store(&s_lang, ((unsigned)lang < (unsigned)UI_LANG_COUNT)
                              ? (int)lang : (int)UI_LANG_EN);
}

ui_lang_t ui_text_language(void)
{
    return (ui_lang_t)atomic_load(&s_lang);
}

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
    return ui_tr_in(ui_text_language(), id);
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
#ifndef ESP_PLATFORM
    return ((unsigned)id < (unsigned)TX_COUNT) ? k_names[id] : "";
#else
    (void)id;
    return "";
#endif
}

/* ---------------------------------------------------- the settings schema */

const char *ui_setting_label(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return "";
    }
    const ui_language_t *l = ui_text_table(ui_text_language());
    const char *t = (l != NULL) ? entry(l->setting_label, (int)id) : NULL;
    return (t != NULL) ? t : settings_label(id);
}

const char *ui_setting_help(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return "";
    }
    const ui_language_t *l = ui_text_table(ui_text_language());
    const char *t = (l != NULL) ? entry(l->setting_help, (int)id) : NULL;
    return (t != NULL) ? t : settings_help(id);
}

const char *ui_setting_category(setting_cat_t cat)
{
    if ((unsigned)cat >= (unsigned)SET_CAT_COUNT) {
        return "";
    }
    const ui_language_t *l = ui_text_table(ui_text_language());
    const char *t = (l != NULL) ? entry(l->category, (int)cat) : NULL;
    return (t != NULL) ? t : settings_category_name(cat);
}

/**
 * @brief A setting's option list in the language showing.
 * @param id The setting.
 * @return The list, as long as the English one, or NULL for English and
 *         for a setting that is no choice.
 */
static const char *const *lang_options(setting_id_t id)
{
    const ui_language_t *l = ui_text_table(ui_text_language());
    const int list = settings_option_list(id);
    return (l != NULL && l->options != NULL && list >= 0)
               ? l->options[list] : NULL;
}

const char *ui_setting_option(setting_id_t id, int k)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || d->options == NULL || k < 0 || k >= d->option_count) {
        return "?";
    }
    const char *const *opts = lang_options(id);
    return (opts != NULL && opts[k] != NULL) ? opts[k] : d->options[k];
}

const char *ui_on_off(int on)
{
    return on ? TR(ON) : TR(OFF);
}

const char *ui_setting_value(setting_id_t id, char *buf, size_t n)
{
    return ui_setting_value_of(id, settings_get(id), buf, n);
}

const char *ui_setting_value_of(setting_id_t id, float value, char *buf,
                                size_t n)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || buf == NULL || n == 0u) {
        return settings_value_text_of(id, value, buf, n);
    }
    if (d->type == SET_TYPE_BOOL
        || (d->off_below > 0.0f && value == 0.0f)) {
        /* A switch, and a number whose 0 is off: a SENSORS shunt. */
        snprintf(buf, n, "%s", ui_on_off(value != 0.0f));
        return buf;
    }
    if (d->type == SET_TYPE_ENUM) {
        const int i = (int)lrintf(value);
        const char *const *opts = lang_options(id);
        if (opts != NULL && i >= 0 && i < d->option_count
            && opts[i] != NULL) {
            snprintf(buf, n, "%s", opts[i]);
            return buf;
        }
    }
    return settings_value_text_of(id, value, buf, n);
}

/* ----------------------------------------------------- the servo test's */

const char *const *ui_servo_table(void)
{
    const ui_language_t *l = ui_text_table(ui_text_language());
    return (l != NULL) ? l->servo : NULL;
}

const char *ui_servo_str(servo_str_t id)
{
    return servo_str_in(ui_servo_table(), id);
}

/* ------------------------------------------------- the card slot's words */

/* By place, then NONE, NO_FAT, UNREADABLE. */
static const ui_text_id_t k_card_text[UI_CARD_AT_COUNT][3] = {
    [UI_CARD_AT_LOGS]   = { TX_LOG_NO_CARD, TX_LOG_CARD_NO_FAT,
                            TX_LOG_CARD_UNREADABLE },
    [UI_CARD_AT_SPLASH] = { TX_SPLASH_NO_CARD, TX_SPLASH_CARD_NO_FAT,
                            TX_SPLASH_CARD_UNREADABLE },
    [UI_CARD_AT_RUN]    = { TX_ALERT_NO_CARD_RUN, TX_ALERT_CARD_NO_FAT_RUN,
                            TX_ALERT_CARD_UNREADABLE },
    [UI_CARD_AT_TEST]   = { TX_ALERT_CARD_SERVO, TX_ALERT_CARD_SERVO_NO_FAT,
                            TX_ALERT_CARD_SERVO_UNREADABLE },
    [UI_CARD_AT_KP_WRITE] = { TX_KP_WRITE_NO_CARD, TX_KP_WRITE_NO_FAT,
                              TX_KP_WRITE_UNREADABLE },
    [UI_CARD_AT_KP_NO_FILE] = { TX_KP_NO_FILE_1, TX_KP_NO_FILE_1_NO_FAT,
                                TX_KP_NO_FILE_1_UNREADABLE },
    [UI_CARD_AT_KP_NO_BACKUP] = { TX_KP_NO_BACKUP_FILE, TX_KP_NO_BACKUP_NO_FAT,
                                  TX_KP_NO_BACKUP_UNREADABLE },
    [UI_CARD_AT_KP_MSG] = { TX_KP_M_CARD_NONE, TX_KP_M_CARD_NO_FAT,
                            TX_KP_M_CARD_UNREADABLE },
};

ui_text_id_t ui_card_text_id(card_state_t state, ui_card_at_t at)
{
    if ((unsigned)at >= (unsigned)UI_CARD_AT_COUNT) {
        return TX_LOG_NO_CARD;
    }
    const unsigned place = (unsigned)at;
    switch (state) {
    case CARD_STATE_NO_FAT:     return k_card_text[place][1];
    case CARD_STATE_UNREADABLE: return k_card_text[place][2];
    case CARD_STATE_NONE:
    case CARD_STATE_MOUNTED:
    default:                    return k_card_text[place][0];
    }
}

const char *ui_card_text(card_state_t state, ui_card_at_t at)
{
    return ui_tr(ui_card_text_id(state, at));
}

/* ---------------------------------------------------------------- units */

/**
 * @brief The unit symbols, as tools/check_units.py's UNITS lists them ("%%",
 *        its printf spelling of "%", as "%"); test_check_units.py holds the
 *        two lists to each other.
 */
static const char *const k_unit_symbols[] = {
    "kbit/s", "Mbit/s", "bit/s", "kB/s", "MB/s", "KB/s", "B/s", "KiB/s",
    "MiB/s", "°/s", "deg/s",
    "mAh", "mWh", "kWh", "Wh", "Ah",
    "GHz", "MHz", "kHz", "Hz",
    "µs", "μs", "us", "ns", "ms", "min", "s", "h",
    "kV", "mV", "µV", "μV", "V", "mA", "µA", "μA", "uA", "nA", "A", "mW",
    "kW", "W",
    "mΩ", "kΩ", "MΩ", "Ω", "mOhm", "kOhm", "Ohm", "ohm",
    "°C", "°F", "°", "%", "deg",
    "KiB", "MiB", "GiB", "kB", "KB", "MB", "GB", "B", "kbps", "Mbps", "bps",
    "px", "dBm", "dB", "rpm", "RPM", "eRPM", "fps", "ppm", "sps",
    "mm", "cm", "µm", "nm", "m", "kg", "g", "Ncm", "Nm", "N",
    "µF", "μF", "uF", "nF", "pF", "mF", "F", "µH", "uH", "mH", "nH", "H", "K",
    "C", "S",
};

/**
 * @brief Whether @p s, up to its end or its "/", is one of the symbols.
 * @param s  the part
 * @param n  its bytes
 * @return 1 for a symbol, else 0
 */
static int is_symbol_part(const char *s, size_t n)
{
    for (size_t k = 0; k < sizeof(k_unit_symbols) / sizeof(k_unit_symbols[0]);
         ++k) {
        if (strlen(k_unit_symbols[k]) == n
            && memcmp(k_unit_symbols[k], s, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int ui_unit_is_symbol(const char *unit)
{
    if (unit == NULL || unit[0] == '\0') {
        return 0;
    }
    if (is_symbol_part(unit, strlen(unit))) {
        return 1;
    }
    /* A compound, every part a symbol: "rpm/V", "%/s". */
    for (const char *p = unit; *p != '\0';) {
        const size_t n = strcspn(p, "/");
        if (n == 0u || !is_symbol_part(p, n)) {
            return 0;
        }
        p += n;
        if (*p == '/') {
            ++p;
            if (*p == '\0') {
                return 0;
            }
        }
    }
    return 1;
}

const char *ui_unit_gap(const char *unit)
{
    return (unit == NULL || unit[0] == '\0' || ui_unit_is_symbol(unit)) ? ""
                                                                         : " ";
}
