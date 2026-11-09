/**
 * @file test_servo_ready_tags.c
 * @brief The READY rows' tags (servo_ready.c, k_tags): every text that
 *        can reach a row's state fits the row's 20 cells after its tag, in
 *        English and in German; the table's tags fit, and it holds only
 *        texts longer than a row; a red row shows the tag and keeps the
 *        whole text for the alert band.
 *
 * The texts are enumerated from the tree, not from a list here: every
 * UI_TEXT and servo text that a card's own list gives a row
 * (rauto_row_of(), lim_row_of(), test_row_of(), drive_row_of()), and the
 * tag table itself, its formatted texts at the largest numbers they take.
 * A row's state is measured as servo_ready.c's cut_value() measures it:
 * gfx_text_prefix() at READY_VALUE_CELLS.
 *
 * Cases:
 * - every_text_a_row_shows_fits_after_its_tag
 * - every_tag_fits_and_every_tagged_text_needs_one
 * - a_text_that_fits_shows_itself
 * - a_red_row_shows_its_tag_and_keeps_the_whole_text
 * - every_row_of_every_card_fits_its_cells
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_ready_int.h"
#include "ui_text.h"

/**
 * @brief Whether state @p v fits a row: its first READY_VALUE_CELLS cells
 *        are the whole of it.
 * @param v the state
 * @return true then
 */
static bool fits(const char *v)
{
    return v[gfx_text_prefix(v, READY_VALUE_CELLS)] == '\0';
}

/**
 * @brief Whether a card's own list gives @p t a row: a text that can reach
 *        a row's state.
 * @param t a text in the language showing
 * @return true then
 */
static bool on_a_row(const char *t)
{
    return rauto_row_of(t) != RAR_COUNT || lim_row_of(t) != LRR_COUNT
           || test_row_of(t) != TRR_COUNT || drive_row_of(t) != DRR_COUNT;
}

/**
 * @brief Whether @p t, a text that reaches a row, shows a state that fits;
 *        a failure is named on stdout.
 * @param t the text
 * @param name its ID's name
 * @return true when it fits
 */
static bool shows_fitting(const char *t, const char *name)
{
    char v[64];
    (void)ready_tag(t, v, sizeof(v));
    if (!fits(v)) {
        printf("  %s shows \"%s\": more than %d cells\n", name, v,
               READY_VALUE_CELLS);
        return false;
    }
    return true;
}

/* Every UI_TEXT entry and every servo text, in each language: a text a
 * card's list gives a row shows a state of 20 cells or fewer, its tag or
 * itself.  The cards' lists hold more than a handful, so the walk is no
 * empty one. */
TEST_CASE(every_text_a_row_shows_fits_after_its_tag)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        int seen = 0;
        for (int id = 0; id < (int)TX_COUNT; ++id) {
            const char *t = ui_tr((ui_text_id_t)id);
            if (on_a_row(t)) {
                ++seen;
                CHECK(shows_fitting(t, ui_text_name((ui_text_id_t)id)));
            }
        }
        for (int id = 0; id < (int)SERVO_STR_COUNT; ++id) {
            const char *t = ui_servo_str((servo_str_t)id);
            if (on_a_row(t)) {
                ++seen;
                char name[24];
                snprintf(name, sizeof(name), "SERVO_STR %d", id);
                CHECK(shows_fitting(t, name));
            }
        }
        CHECK(seen >= 40);
    }
    ui_text_set_language(UI_LANG_EN);
}

/**
 * @brief Entry @p t's text or tag in the language showing, a format
 *        filled with the largest numbers it takes.
 * @param t the entry
 * @param tag the tag, else the text
 * @param b receives it
 * @param n its size
 * @return @p b
 */
static const char *entry_text(const rdy_tag_t *t, bool tag, char *b,
                              size_t n)
{
    const unsigned v[2] = { t->max[0], t->max[1] };
    if (t->src == (uint8_t)RDY_TXT_FMT && tag) {
        t->fmt(b, n, v);
    } else if (t->src == (uint8_t)RDY_TXT_FMT) {
        snprintf(b, n, ui_tr((ui_text_id_t)t->full), v[0], v[1]);
    } else if (tag) {
        snprintf(b, n, "%s", ui_tr(t->tag));
    } else if (t->src == (uint8_t)RDY_TXT_SERVO) {
        snprintf(b, n, "%s", ui_servo_str((servo_str_t)t->full));
    } else {
        snprintf(b, n, "%s", ui_tr((ui_text_id_t)t->full));
    }
    return b;
}

/* Each tag fits a row in each language, a format at the largest numbers
 * it takes; each text of the table is longer than a row in a language,
 * so a text that fits is never swapped; a plain text is shown as its tag,
 * and no text is in the table twice. */
TEST_CASE(every_tag_fits_and_every_tagged_text_needs_one)
{
    size_t n = 0;
    const rdy_tag_t *k = ready_tags(&n);
    CHECK(n > 0u);
    char a[128], b[128];
    for (size_t i = 0; i < n; ++i) {
        bool longer = false;
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            ui_text_set_language((ui_lang_t)lang);
            const char *tag = entry_text(&k[i], true, b, sizeof(b));
            if (!fits(tag)) {
                printf("  tag %s: \"%s\" is more than %d cells\n",
                       ui_text_name(k[i].tag), tag, READY_VALUE_CELLS);
            }
            CHECK(fits(tag));
            CHECK(tag[0] != '\0');
            longer |= !fits(entry_text(&k[i], false, a, sizeof(a)));
            if (k[i].src != (uint8_t)RDY_TXT_FMT) {
                const char *full = k[i].src == (uint8_t)RDY_TXT_SERVO
                                       ? ui_servo_str((servo_str_t)k[i].full)
                                       : ui_tr((ui_text_id_t)k[i].full);
                CHECK_STR_EQ(ready_tag(full, a, sizeof(a)), ui_tr(k[i].tag));
                for (size_t j = 0; j < n; ++j) {
                    CHECK(j == i || k[j].src != k[i].src
                          || k[j].full != k[i].full);
                }
            } else {
                /* The tag the row shows is the table's ID, formatted. */
                CHECK(k[i].nums != NULL && k[i].fmt != NULL);
                snprintf(a, sizeof(a), ui_tr(k[i].tag), (unsigned)k[i].max[0],
                         (unsigned)k[i].max[1]);
                CHECK_STR_EQ(tag, a);
            }
        }
        CHECK(longer);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A text the table does not hold is its own state: one that fits, a copy
 * of a table text (texts compare by pointer), NULL as "". */
TEST_CASE(a_text_that_fits_shows_itself)
{
    char v[64];
    CHECK_STR_EQ(ready_tag(ui_servo_str(SERVO_STR_START_NOT_ARMED), v,
                           sizeof(v)),
                 "ARM FIRST");
    CHECK_STR_EQ(ready_tag(TR(SV_TAP_HV), v, sizeof(v)), "TAP: HV WARNING");
    CHECK_STR_EQ(ready_tag("IMU YAW REFUSED: HINGE ON X OR Y", v, sizeof(v)),
                 "IMU YAW REFUSED: HINGE ON X OR Y");
    CHECK_STR_EQ(ready_tag(TR(RA_IMU_YAW), v, sizeof(v)), "YAW REFUSED");
    CHECK_STR_EQ(ready_tag(NULL, v, sizeof(v)), "");
    CHECK_STR_EQ(ready_tag(TR(RA_IMU_YAW), NULL, 0u), "");
    unsigned u[2];
    CHECK(!lim_hitec_nums(TR(SV_LIM_HITEC_RANGE), u));
    CHECK(!lim_hitec_nums(NULL, u));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ready_tag(TR(RA_IMU_YAW), v, sizeof(v)), "GIEREN GESPERRT");
    CHECK_STR_EQ(ready_tag(TR(RA_NO_ZERO), v, sizeof(v)), "NICHT GESETZT");
    ui_text_set_language(UI_LANG_EN);
}

/** @brief Function @p fn shown on a fresh bench, SAFE or ARMED. */
static void to_fn(servo_fn_t fn, bool armed)
{
    if (armed) {
        fresh_armed();
    } else {
        fresh();
    }
    scr->enter();
    go(fn);
    ui_router_set_alert(NULL);
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/* RUDDER AUTO on OUTPUTS: PORT is red with NO PROG PORT, its row keeps
 * the whole text, and the refusal and a tap name the whole text in the
 * band.  A report on its way: ARM reads WRITING REPORT on RUDDER AUTO,
 * LIMIT RUN and TEST, the whole text in `why`.  In both languages. */
TEST_CASE(a_red_row_shows_its_tag_and_keeps_the_whole_text)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        to_fn(SERVO_FN_RAUTO, false);
        rdy_row_t r;
        CHECK(rauto_ready_row(RAR_PORT, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK(r.why == TR(RA_PROG_PORT));
        CHECK_STR_EQ(r.value, TR(PRR_NO_PORT));
        char want[128], got[128];
        snprintf(want, sizeof(want), TR(RDY_BAND), TR(SVH_PORT),
                 TR(RA_PROG_PORT));
        CHECK_STR_EQ(ready_refusal(got, sizeof(got)), want);
        settings_set(SET_SERVO_TARGET, 1.0f);
        servo_screen_set_drive(1u, 13u, true);
        servo_screen_test_logger_busy(true);
        CHECK(rauto_ready_row(RAR_ARM, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK(r.why == ui_servo_str(SERVO_STR_START_BUSY));
        CHECK_STR_EQ(r.value, TR(RDY_TAG_BUSY));
        snprintf(want, sizeof(want), TR(RDY_BAND), "ARM", r.why);
        tap(SV_RDY_X, SV_RDY_ROW_Y(RAR_ARM));
        CHECK_STR_EQ(alert(), want);
        go(SERVO_FN_LIMRUN);
        CHECK(lim_ready_row(LRR_ARM, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK(r.why == ui_servo_str(SERVO_STR_START_BUSY));
        CHECK_STR_EQ(r.value, TR(RDY_TAG_BUSY));
        go(SERVO_FN_TEST);
        CHECK(test_ready_row(TRR_ARM, &r));
        CHECK_EQ(r.st, (uint8_t)RDY_BAD);
        CHECK(r.why == ui_servo_str(SERVO_STR_START_BUSY));
        CHECK_STR_EQ(r.value, TR(RDY_TAG_BUSY));
        servo_screen_test_logger_busy(false);
    }
    ui_text_set_language(UI_LANG_EN);
}

/**
 * @brief Whether every row of the card shown fits its cells, and every
 *        red row shows its text's state (ready_tag()).  The port line is
 *        the PORT button's line of up to 25 cells and no gate text: a PORT row
 *        that shows it whole is left to the button's own checks.
 * @return false on one that does not; it is named on stdout
 */
static bool rows_fit(void)
{
    bool ok = true;
    char line[64];
    (void)servo_screen_port_line(line, sizeof(line));
    for (int i = 0; i < ready_count(); ++i) {
        rdy_row_t r;
        if (!ready_row(i, &r)) {
            return false;
        }
        if (strcmp(r.value, line) == 0) {
            continue;
        }
        char v[64];
        if (r.st == (uint8_t)RDY_BAD && r.why != NULL
            && strcmp(ready_tag(r.why, v, sizeof(v)), r.value) != 0) {
            printf("  fn %d row %d: red \"%s\" is not its tag \"%s\"\n",
                   (int)servo_screen_fn(), i, r.value, v);
            ok = false;
        }
        if (!fits(r.value)) {
            printf("  fn %d row %d: \"%s\" is more than %d cells\n",
                   (int)servo_screen_fn(), i, r.value, READY_VALUE_CELLS);
            ok = false;
        }
    }
    return ok;
}

/* Every function with READY rows, on the benches the cards meet most: a
 * fresh SAFE bench on OUTPUTS, ARMED, a PROG PORT with its pulse off and
 * on, a report on its way, no link.  Every row fits its 20 cells and a
 * red row shows its text's state, in both languages. */
TEST_CASE(every_row_of_every_card_fits_its_cells)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int fn = SERVO_FN_LIVE; fn < SERVO_FN_COUNT; ++fn) {
            to_fn((servo_fn_t)fn, false);
            CHECK(rows_fit());
            to_fn((servo_fn_t)fn, true);
            CHECK(rows_fit());
            to_fn((servo_fn_t)fn, false);
            settings_set(SET_SERVO_TARGET, 1.0f);
            servo_screen_set_drive(1u, 13u, false);
            CHECK(rows_fit());
            servo_screen_set_drive(1u, 13u, true);
            CHECK(rows_fit());
            servo_screen_test_logger_busy(true);
            CHECK(rows_fit());
            servo_screen_test_logger_busy(false);
            servo_screen_set_armed(true);
            servo_screen_set_drive(1u, 13u, false);
            CHECK(rows_fit());
            servo_screen_set_link(false);
            CHECK(rows_fit());
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_text_a_row_shows_fits_after_its_tag);
    RUN(every_tag_fits_and_every_tagged_text_needs_one);
    RUN(a_text_that_fits_shows_itself);
    RUN(a_red_row_shows_its_tag_and_keeps_the_whole_text);
    RUN(every_row_of_every_card_fits_its_cells);
    return test_summary("servo_ready_tags");
}
