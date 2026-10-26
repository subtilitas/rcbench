/*
 * The modelled ESC (electronic speed controller) protocols
 * (prog_models.h): the four parameter tables no PROGRAM list shows, held
 * sound as data -- every value a programmer could draw is in its range, on
 * its step, and named in every language.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "prog_models.h"
#include "prog_registry.h"
#include "ui_text.h"

/* Four protocols, the ESC firmwares, each under the class and with the
 * parameter count the tables carry.  Servos by Hitec have a programmer of
 * their own (hitec_panel.h) and no table. */
TEST_CASE(the_four_modelled_protocols_are_there)
{
    size_t n = 0;
    const prog_model_t *m = prog_models(&n);
    if (m == NULL || n != 4u) {
        T_FAIL("expected 4 protocols, got %zu", n);
        return;
    }
    static const struct { const char *name; int klass; int count; } k[] = {
        { "BLHeli_S", PROG_MODEL_ESC, 8 },  { "AM32", PROG_MODEL_ESC, 7 },
        { "ESCape32", PROG_MODEL_ESC, 6 },  { "VESC", PROG_MODEL_ESC, 5 },
    };
    for (size_t i = 0; i < n; ++i) {
        CHECK_STR_EQ(m[i].name, k[i].name);
        CHECK_EQ(m[i].klass, k[i].klass);
        CHECK_EQ(m[i].count, k[i].count);
        CHECK(m[i].count <= PROG_MODEL_PARAMS_MAX);
    }
    CHECK(prog_models(NULL) == m);
    /* A firmware by its own name in every language. */
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(prog_model_name(&m[0]), "BLHeli_S");
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(prog_model_name(NULL), "");
}

/* Every parameter of every table: a name, a help line and a group in every
 * language (the first row opens a group), a choice's index and a number's
 * value inside its range and on its step, a switch at 0 or 1. */
TEST_CASE(every_parameter_is_in_range_and_named)
{
    size_t n = 0;
    const prog_model_t *m = prog_models(&n);
    for (size_t i = 0; i < n; ++i) {
        CHECK(m[i].params[0].group != TX_COUNT);
        for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
            ui_text_set_language((ui_lang_t)lang);
            CHECK(ui_tr(m[i].transport)[0] != '\0');
            CHECK(ui_tr(m[i].device)[0] != '\0');
        }
        for (int j = 0; j < m[i].count; ++j) {
            const prog_param_t *p = &m[i].params[j];
            CHECK(p->name != NULL && p->name[0] != '\0');
            for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
                ui_text_set_language((ui_lang_t)lang);
                CHECK(ui_tr(p->help)[0] != '\0');
                CHECK(p->group == TX_COUNT || ui_tr(p->group)[0] != '\0');
            }
            switch (p->kind) {
            case PROG_PARAM_ENUM:
                CHECK(p->choices != NULL);
                CHECK(p->count >= 2);
                CHECK(p->initial >= 0 && p->initial < p->count);
                for (int c = 0; p->choices != NULL && c < p->count; ++c) {
                    CHECK(p->choices[c] != NULL && p->choices[c][0] != '\0');
                }
                break;
            case PROG_PARAM_BOOL:
                CHECK(p->initial == 0 || p->initial == 1);
                break;
            case PROG_PARAM_NUMBER:
                CHECK(p->lo < p->hi);
                CHECK(p->step > 0);
                CHECK_EQ((p->hi - p->lo) % (p->step > 0 ? p->step : 1), 0);
                CHECK(p->initial >= p->lo && p->initial <= p->hi);
                CHECK(p->unit != NULL && p->unit[0] != '\0');
                CHECK(p->decimals == 0 || p->decimals == 1);
                break;
            default:
                T_FAIL("protocol %zu parameter %d: kind %d", i, j,
                       (int)p->kind);
                break;
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* No transport carries them, so no list offers them: neither PROGRAM list
 * has a row named for one of the four protocols. */
TEST_CASE(no_program_list_shows_a_modelled_protocol)
{
    size_t n = 0, row_n = 0;
    const prog_model_t *m = prog_models(&n);
    const prog_reg_row_t *rows = prog_registry(&row_n);
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t r = 0; r < row_n; ++r) {
            const char *name = ui_tr(rows[r].name);
            for (size_t i = 0; i < n; ++i) {
                CHECK(strstr(name, prog_model_name(&m[i])) == NULL);
            }
            CHECK(strstr(name, "BLHeli") == NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_four_modelled_protocols_are_there);
    RUN(every_parameter_is_in_range_and_named);
    RUN(no_program_list_shows_a_modelled_protocol);
    return test_summary("prog_models");
}
