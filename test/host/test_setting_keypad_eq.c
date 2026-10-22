/**
 * @file test_setting_keypad_eq.c
 * @brief Every value the "-" and "+" keys reach on a row that opens a
 *        setting's keypad, typed on the keypad: the stored float equals the
 *        stepped one bit for bit.
 *
 * For each number of SETUP's PANEL, INTERFACES and SENSORS lists and of
 * ESC STICK's TIMING (Sensor SCL has no keys), the walk starts at the
 * range's minimum and steps "+" (settings_adjust()) to the maximum.  Each
 * value it reaches is written as "%.*f" with settings_decimals() digits,
 * typed key by key on a keypad opened by ui_setting_keypad_open() with the
 * setting first moved to the other end of its range, and taken with OK
 * through ui_setting_keypad_ok().  -1, a pin not set, is skipped: the
 * keypad of a pin has no sign.  188,891 values; the case runs in 0.7 s
 * on the host suite's Debug build.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "settings.h"
#include "ui_keypad.h"
#include "ui_setting_keypad.h"

static const gfx_rect_t k_area = { 8, 8, 784, 386 };

/**
 * @brief One key pressed and released over its middle.
 * @param k The keypad.
 * @param key The key.
 * @param out Where OK writes the value, or NULL.
 * @return What the release came to.
 */
static ui_keypad_result_t press(ui_keypad_t *k, ui_key_t key, float *out)
{
    const gfx_rect_t r = ui_keypad_key_rect(k, key);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    (void)ui_keypad_event(k, &e, out);
    e.type = TOUCH_EVENT_UP;
    return ui_keypad_event(k, &e, out);
}

/**
 * @brief @p text typed key by key, then OK.
 * @param k The keypad, open.
 * @param text Digits and at most one point.
 * @param out The value OK hands back.
 * @return What OK came to.
 */
static ui_keypad_result_t type_ok(ui_keypad_t *k, const char *text,
                                  float *out)
{
    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        (void)press(k, (*p == '.') ? UI_KEY_DOT : k_digit[*p - '0'], NULL);
    }
    return press(k, UI_KEY_OK, out);
}

/**
 * @brief Whether a setting is a row these keypads serve.
 * @param id The setting.
 * @return True for a keyed number of PANEL, INTERFACES, SENSORS or STICK.
 */
static bool served(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || !ui_setting_keypad_offered(id)) {
        return false;
    }
    return d->cat == SET_CAT_APP || d->cat == SET_CAT_IFACE
           || d->cat == SET_CAT_SENSORS || d->cat == SET_CAT_STICK;
}

TEST_CASE(every_stepped_value_typed_is_the_stepped_value)
{
    settings_set_store(NULL);
    settings_init();
    ui_keypad_t k;
    memset(&k, 0, sizeof(k));
    long checked = 0;
    int bad = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (!served(id)) {
            continue;
        }
        const setting_def_t *d = settings_def(id);
        const int dec = settings_decimals(id);
        CHECK(!ui_setting_keypad_signed(id));     /* no signed setting */
        settings_set(id, d->min);
        float prev = NAN;
        for (int n = 0; n < 200000; ++n) {
            const float stepped = settings_get(id);
            if (stepped == prev) {
                break;                            /* the maximum */
            }
            prev = stepped;
            if (stepped < 0.0f) {
                settings_adjust(id, 1);           /* -1: not typed */
                continue;
            }
            char txt[32];
            snprintf(txt, sizeof(txt), "%.*f", dec, (double)stepped);
            if (strlen(txt) > UI_KEYPAD_ENTRY_MAX) {
                T_FAIL("%s: %s is longer than the entry", d->key, txt);
                ++bad;
            }
            settings_set(id, (stepped == d->max) ? d->min : d->max);
            ui_setting_keypad_open(&k, k_area, id);
            float v = 0.0f;
            const ui_keypad_result_t r = type_ok(&k, txt, &v);
            const ui_setkey_result_t t = (r == UI_KEYPAD_OK)
                                             ? ui_setting_keypad_ok(&k, id, v)
                                             : UI_SETKEY_REFUSED;
            const float typed = settings_get(id);
            if (t != UI_SETKEY_SET
                || memcmp(&typed, &stepped, sizeof(typed)) != 0) {
                if (bad < 20) {
                    T_FAIL("%s: stepped %.9g, typed '%s' -> %.9g", d->key,
                           (double)stepped, txt, (double)typed);
                }
                ++bad;
                settings_set(id, stepped);
            }
            ++checked;
            settings_adjust(id, 1);
        }
    }
    printf("      %ld values, %d mismatches\n", checked, bad);
    /* 12 SENSORS LIMIT rows of 3,278 values each (0 to 32760 mA in 10 mA
     * steps and 32767) are 39,336 of them. */
    CHECK_EQ(checked, 188891);
    CHECK_EQ(bad, 0);
}

int main(void)
{
    RUN(every_stepped_value_typed_is_the_stepped_value);
    return test_summary("setting_keypad_eq");
}
