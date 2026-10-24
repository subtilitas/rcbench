/**
 * @file keypad_rows_rig.h
 * @brief One check for every row whose value a tap opens the numeric keypad
 *        on (ui_keypad.h), shared by the test_keypad_rows_*.c suites.
 *
 * A suite names its screen's rows in a table of kpr_row_t, says how a row's
 * keypad is opened and how a key of it is tapped (kpr_screen_t), and hands
 * each row to kpr_row().  That one call holds the row to four things:
 *
 *   1. a tap opens the keypad with the row's title, unit, range and
 *      digits after the point;
 *   2. a value past the range's top (and, where it can be typed, under its
 *      bottom) is refused: the keypad stays open, marked, the entry kept,
 *      and the row keeps its old value;
 *   3. CANCEL closes the keypad and the row keeps its old value;
 *   4. OK on a value inside the range closes it and the row takes the
 *      value, as the keypad opened again shows it.
 *
 * The keypad shows the row's value faint behind the entry
 * (ui_keypad_t.value), so "the row keeps its old value" and "the row takes
 * the value" are read from the keypad opened again: the same reader on
 * every screen.
 *
 * A row that is a setting of the schema takes its expectations from the
 * schema itself (kpr_setting()): the digits from the step, not from the
 * helper the screens open the keypad with.
 *
 * Only static inline helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "settings.h"
#include "ui_keypad.h"
#include "ui_setting_keypad.h"
#include "ui_text.h"

/** @brief How a suite reaches its screen's keypad. */
typedef struct {
    /** @brief The keypad the screen holds, open or closed. */
    const ui_keypad_t *(*keypad)(void);
    /** @brief A tap on key @p k of the open keypad, in the coordinates the
     *         screen takes. */
    void (*key)(ui_key_t k);
    /** @brief A tap that opens the keypad of row @p row of the suite's
     *         table. */
    void (*open)(int row);
} kpr_screen_t;

/** @brief What a row's keypad opens with, and the value OK is tried on. */
typedef struct {
    const char *name;     /**< for a failure's line: the row            */
    const char *title;    /**< the keypad's title                       */
    const char *unit;     /**< its unit                                 */
    float       min;      /**< the range's bottom                       */
    float       max;      /**< its top                                  */
    int         decimals; /**< digits after the point                   */
    /** @brief A value inside the range OK is tried on, on the row's
     *         steps, so the row shows it as typed. */
    float       ok;
} kpr_row_t;

/**
 * @brief @p text typed on the open keypad, key by key, without OK.
 * @param s The screen.
 * @param text Digits, '.' for the point and '-' for the +/- key.
 */
static inline void kpr_type(const kpr_screen_t *s, const char *text)
{
    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        s->key((*p == '-') ? UI_KEY_SIGN
               : (*p == '.') ? UI_KEY_DOT : k_digit[*p - '0']);
    }
}

/**
 * @brief @p v as the keypad takes it: @p decimals digits after the point.
 * @param v The value.
 * @param decimals The digits after the point.
 * @param buf Takes the text.
 * @param n Bytes at @p buf.
 * @return Whether the text fits an entry (UI_KEYPAD_ENTRY_MAX characters).
 */
static inline bool kpr_text(float v, int decimals, char *buf, size_t n)
{
    const int w = snprintf(buf, n, "%.*f", decimals, (double)v);
    return w > 0 && (size_t)w < n && w <= UI_KEYPAD_ENTRY_MAX;
}

/**
 * @brief One unit of the last place @p decimals gives: 1, 0.1, 0.01.
 * @param decimals The digits after the point.
 * @return The unit.
 */
static inline float kpr_unit(int decimals)
{
    float u = 1.0f;
    for (int i = 0; i < decimals; ++i) {
        u *= 0.1f;
    }
    return u;
}

/**
 * @brief The keypad of row @p row checked as it opens: open, its title,
 *        unit, range and digits after the point.
 * @param s The screen.
 * @param e What the row's keypad opens with.
 * @return The value the keypad shows faint: the row's value.
 */
static inline float kpr_opened(const kpr_screen_t *s, const kpr_row_t *e)
{
    const ui_keypad_t *k = s->keypad();
    if (!k->open) {
        T_FAIL("%s: no keypad opened", e->name);
        return NAN;
    }
    if (strcmp(k->title, e->title) != 0) {
        T_FAIL("%s: title \"%s\", want \"%s\"", e->name, k->title, e->title);
    }
    if (strcmp(k->unit, e->unit) != 0) {
        T_FAIL("%s: unit \"%s\", want \"%s\"", e->name, k->unit, e->unit);
    }
    const float tol = kpr_unit(e->decimals) * 0.01f;
    if (fabsf(k->min - e->min) > tol || fabsf(k->max - e->max) > tol) {
        T_FAIL("%s: range %g to %g, want %g to %g", e->name, (double)k->min,
               (double)k->max, (double)e->min, (double)e->max);
    }
    if (k->decimals != e->decimals) {
        T_FAIL("%s: %d decimals, want %d", e->name, k->decimals,
               e->decimals);
    }
    return k->value;
}

/**
 * @brief @p text typed and OK: refused, the keypad open, marked, the entry
 *        kept; then CANCEL closes it.
 * @param s The screen.
 * @param e The row.
 * @param text A value outside the range.
 */
static inline void kpr_refused(const kpr_screen_t *s, const kpr_row_t *e,
                               const char *text)
{
    kpr_type(s, text);
    s->key(UI_KEY_OK);
    const ui_keypad_t *k = s->keypad();
    if (!k->open || !k->refused) {
        T_FAIL("%s: %s%s not refused", e->name, text, e->unit);
    } else if (strcmp(k->entry, text) != 0) {
        T_FAIL("%s: entry \"%s\" after the refusal, want \"%s\"", e->name,
               k->entry, text);
    }
    s->key(UI_KEY_CANCEL);
    if (s->keypad()->open) {
        T_FAIL("%s: CANCEL left the keypad open", e->name);
    }
}

/**
 * @brief Row @p row held to the four rules of the file's header.
 * @param s The screen.
 * @param row The row, as s->open() takes it.
 * @param e What its keypad opens with and the value OK is tried on.
 */
static inline void kpr_row(const kpr_screen_t *s, int row, const kpr_row_t *e)
{
    char text[16];
    const float tol = kpr_unit(e->decimals) * 0.5f;

    s->open(row);
    const float old = kpr_opened(s, e);
    if (!s->keypad()->open) {
        return;
    }
    /* Past the top, and under the bottom where that can be typed: an
     * unsigned keypad types nothing below 0. */
    if (kpr_text(e->max + kpr_unit(e->decimals), e->decimals, text,
                 sizeof(text))) {
        kpr_refused(s, e, text);
        s->open(row);
        if (fabsf(kpr_opened(s, e) - old) > tol) {
            T_FAIL("%s: a refused value changed the row", e->name);
        }
    }
    const float below = e->min - kpr_unit(e->decimals);
    if ((below >= 0.0f || s->keypad()->sign)
        && kpr_text(below, e->decimals, text, sizeof(text))) {
        kpr_refused(s, e, text);
        s->open(row);
        if (fabsf(kpr_opened(s, e) - old) > tol) {
            T_FAIL("%s: a refused value changed the row", e->name);
        }
    }
    /* CANCEL on a value typed. */
    if (!kpr_text(e->ok, e->decimals, text, sizeof(text))) {
        T_FAIL("%s: OK's value %g does not fit an entry", e->name,
               (double)e->ok);
        s->key(UI_KEY_CANCEL);
        return;
    }
    kpr_type(s, text);
    s->key(UI_KEY_CANCEL);
    if (s->keypad()->open) {
        T_FAIL("%s: CANCEL left the keypad open", e->name);
    }
    s->open(row);
    if (fabsf(kpr_opened(s, e) - old) > tol) {
        T_FAIL("%s: CANCEL changed the row", e->name);
    }
    /* OK on a value inside the range. */
    kpr_type(s, text);
    s->key(UI_KEY_OK);
    if (s->keypad()->open) {
        T_FAIL("%s: OK on %s%s left the keypad open", e->name, text,
               e->unit);
        s->key(UI_KEY_CANCEL);
        return;
    }
    s->open(row);
    const float now = kpr_opened(s, e);
    if (fabsf(now - e->ok) > tol) {
        T_FAIL("%s: OK on %s%s set %g", e->name, text, e->unit,
               (double)now);
    }
    s->key(UI_KEY_CANCEL);
}

/**
 * @brief The digits after the point a step of @p step has, read from the
 *        step itself: 1 is 0, 0.1 is 1, 0.05 is 2, 0.001 is 3.
 * @param step The step.
 * @return The digits, 0 to 3.
 */
static inline int kpr_digits(float step)
{
    for (int d = 0; d < 3; ++d) {
        const float scaled = step * powf(10.0f, (float)d);
        if (fabsf(scaled - roundf(scaled)) < 1e-3f) {
            return d;
        }
    }
    return 3;
}

/**
 * @brief The value OK is tried on for setting @p id: on its steps, at 0
 *        or above, one ui_setting_keypad_takes() takes and the setting does
 *        not hold now; the highest such value.
 * @param id The setting.
 * @return The value; NAN for none.
 */
static inline float kpr_ok_value(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    const float now = settings_get(id);
    const int steps = (int)floorf((d->max - d->min) / d->step + 1e-3f);
    for (int i = steps; i >= 0; --i) {
        const float v = d->min + (float)i * d->step;
        if (v >= 0.0f && fabsf(v - now) > d->step * 0.5f
            && ui_setting_keypad_takes(id, v)) {
            return v;
        }
    }
    return NAN;
}

/**
 * @brief What the keypad of setting @p id opens with, from the schema: its
 *        unit, its range, its step's digits, and kpr_ok_value().
 * @param id The setting.
 * @param title The title the screen gives it; NULL for the setting's
 *        label (ui_setting_label()).
 * @return The row.
 */
static inline kpr_row_t kpr_setting(setting_id_t id, const char *title)
{
    const setting_def_t *d = settings_def(id);
    const kpr_row_t e = {
        .name = d->key,
        .title = (title != NULL) ? title : ui_setting_label(id),
        .unit = (d->unit != NULL) ? d->unit : "",
        .min = d->min, .max = d->max,
        .decimals = kpr_digits(d->step), .ok = kpr_ok_value(id),
    };
    return e;
}
