/**
 * @file test_motor_logname.c
 * @brief RENAME LOG on MOTOR & ESC: the button in the strip row, dimmed
 *        and saying why while ARMED, while ARM is held, while a log is
 *        open, with no log since the start, with AUTO LOGS OFF and with
 *        the log's card gone; the keyboard it opens with the run's plain
 *        name and the label; ARM, the knob and the bench under it; the home
 *        tag and the bench leaving SAFE closing it; OK handing one request
 *        over and the answer coming back as the band's notice.
 *
 * Cases:
 * - the_button_lies_in_the_strip_row_clear_of_the_tabs_and_the_rail
 * - a_dimmed_tap_says_why_and_opens_nothing
 * - a_live_tap_opens_the_keyboard_on_the_label
 * - the_keyboard_covers_arm_and_the_knob
 * - the_home_tag_and_leaving_safe_close_the_keyboard
 * - ok_hands_one_request_over_and_the_answer_is_said
 * - an_empty_ok_gives_the_plain_name_back_or_nothing
 * - a_refused_label_keeps_the_keyboard_open
 * - the_cursor_edits_the_label_where_it_stands
 * - the_settings_list_hides_the_button_and_program_shows_it
 * - closing_the_keyboard_paints_the_screen_as_before
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_rig.h"

#include "fm_io.h"
#include "log_name.h"
#include "ui_textkey.h"
#include "ui_workbench.h"

/** @brief RENAME LOG's middle row: the strip's tabs'. */
#define LOG_Y MO_TAB_Y

/** @brief The log the logger closed last, on the card mounted now. */
static void a_log(const char *name)
{
    motor_screen_set_last_log(name, true, false);
    scr->tick(TICK_S);
}

/**
 * @brief The middle of key @p key of the open keyboard.
 * @param key  the key
 * @param x    takes its column
 * @param y    takes its row
 */
static void key_at(int key, int *x, int *y)
{
    const gfx_rect_t r = ui_textkey_key_rect(motor_screen_logname_keyboard(),
                                             key);
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

/** @brief Tap key @p key of the open keyboard. */
static void tap_key(int key)
{
    int x = 0, y = 0;
    key_at(key, &x, &y);
    tap(x, y);
}

/**
 * @brief Tap the key that shows @p label on the open keyboard.
 * @param label  its label
 * @return false when no key shows it
 */
static bool type_label(const char *label)
{
    const ui_textkey_t *k = motor_screen_logname_keyboard();
    for (int key = 0; key < UI_TK_ALL; ++key) {
        const gfx_rect_t r = ui_textkey_key_rect(k, key);
        if (r.w > 0 && strcmp(ui_textkey_key_label(k, key), label) == 0) {
            tap_key(key);
            return true;
        }
    }
    return false;
}

/** @brief Type @p text, lower-case letters and digits, key by key. */
static void type_text(const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) {
        const char one[2] = { *p, '\0' };
        if (!type_label(one)) {
            T_FAIL("no key types '%c'", *p);
        }
    }
}

/**
 * @brief Pixels of the strip row, x @p x0 to @p x1, that are not the
 *        background.
 * @return the count
 */
static int inked(int x0, int x1)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    int n = 0;
    for (int y = 8; y < 48; ++y) {
        for (int x = x0; x <= x1; ++x) {
            n += (fb[(size_t)y * W + x] != bg) ? 1 : 0;
        }
    }
    return n;
}

/** @brief The band's alert now, "" for none. */
static const char *alert(void)
{
    return (ui_router_alert() != NULL) ? ui_router_alert() : "";
}

TEST_CASE(the_button_lies_in_the_strip_row_clear_of_the_tabs_and_the_rail)
{
    fresh();
    scr->render(&cv, 0);
    /* Drawn in the strip row from x 449 to the left column's edge; the gap
     * from PROGRAM's trough to 2px short of it, and the gap to the rail,
     * stay empty. */
    CHECK(inked(MO_LOGBTN_L, MO_LOGBTN_R) > 200);
    CHECK_EQ(inked(9 + 2 * 154 + 3, MO_LOGBTN_L - 2), 0);
    CHECK_EQ(inked(MO_LOGBTN_R + 1, MO_RC_X - 13), 0);
    CHECK(MO_LOGBTN_R < MO_RC_X);
    /* A tap on PROGRAM's tab is the tab's, not the button's. */
    tap(MO_TAB_X(1), MO_TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!motor_screen_logname_shown());
}

TEST_CASE(a_dimmed_tap_says_why_and_opens_nothing)
{
    fresh();
    /* No MOTOR log since the start. */
    settings_set(SET_LOG_AUTO, 1.0f);
    ui_router_set_alert(NULL);
    scr->tick(TICK_S);
    CHECK(!motor_screen_logname_live());
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(!motor_screen_logname_shown());
    CHECK_STR_EQ(alert(), TR(MO_LOG_NONE));
    /* AUTO LOGS OFF says so instead. */
    settings_set(SET_LOG_AUTO, 0.0f);
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(alert(), TR(LOG_AUTO_OFF));
    settings_set(SET_LOG_AUTO, 1.0f);
    /* The log's card gone or swapped since the run. */
    motor_screen_set_last_log("MOTOR042.CSV", false, false);
    tap(MO_LOGBTN_X, LOG_Y);
    char want[UI_ALERT_MAX];
    snprintf(want, sizeof(want), TR(MO_LOG_MISSING), "MOTOR042.CSV");
    CHECK_STR_EQ(alert(), want);
    /* A log open: a new run's, a test's. */
    motor_screen_set_last_log("MOTOR042.CSV", true, true);
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(alert(), TR(FM_GATE_LOGGING));
    /* ARMED. */
    a_log("MOTOR042.CSV");
    CHECK(motor_screen_logname_live());
    motor_screen_set_armed(true);
    scr->tick(TICK_S);
    CHECK(!motor_screen_logname_live());
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(alert(), TR(FM_GATE_ARMED));
    CHECK(!motor_screen_logname_shown());
    /* SAFE with ARM held by another finger. */
    motor_screen_set_armed(false);
    scr->tick(TICK_S);
    CHECK(motor_screen_logname_live());
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 2);
    scr->tick(TICK_S);
    CHECK(!motor_screen_logname_live());
    ui_router_set_alert(NULL);
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(alert(), TR(FM_GATE_ARMED));
    CHECK(!motor_screen_logname_shown());
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    /* A press that slides off the button opens nothing. */
    scr->tick(TICK_S);
    ev(MO_LOGBTN_X, LOG_Y, TOUCH_EVENT_DOWN, 1);
    ev(MO_LOGBTN_X, LOG_Y + 200, TOUCH_EVENT_UP, 1);
    CHECK(!motor_screen_logname_shown());
}

TEST_CASE(a_live_tap_opens_the_keyboard_on_the_label)
{
    fresh();
    a_log("MOTOR042_kv1200.CSV");
    CHECK(motor_screen_logname_live());
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(motor_screen_logname_shown());
    const ui_textkey_t *k = motor_screen_logname_keyboard();
    char title[64];
    snprintf(title, sizeof(title), TR(MO_TK_LOG), "MOTOR042.CSV");
    CHECK_STR_EQ(k->title, title);
    CHECK_STR_EQ(k->text, "kv1200");
    CHECK(k->file);
    CHECK_EQ(k->max_len, (int)LOG_LABEL_MAX);
    /* A plain name opens an empty field. */
    fresh();
    a_log("MOTOR007.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(motor_screen_logname_shown());
    CHECK_STR_EQ(motor_screen_logname_keyboard()->text, "");
    /* The field takes 46 bytes and no more. */
    for (int i = 0; i < (int)LOG_LABEL_MAX + 3; ++i) {
        type_text("k");
    }
    CHECK_EQ(motor_screen_logname_keyboard()->len, (int)LOG_LABEL_MAX);
}

TEST_CASE(the_keyboard_covers_arm_and_the_knob)
{
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(motor_screen_logname_shown());
    CHECK(scr->covered());
    CHECK(!scr->safe_at(ARM_X, ARM_Y));
    /* The knob moves nothing. */
    motor_screen_knob(0.25f);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    CHECK(motor_screen_logname_shown());
    /* ARM is under the keyboard: a hold there is a key's press, and arms
     * nothing. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    tick_for(2);
    CHECK(last_cmd().kind != MOTOR_CMD_ARM);
    CHECK(motor_screen_logname_live());
}

TEST_CASE(the_home_tag_and_leaving_safe_close_the_keyboard)
{
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(motor_screen_logname_shown());
    /* The tag closes the keyboard and stays on the screen; the next one
     * leaves. */
    CHECK(scr->back());
    CHECK(!motor_screen_logname_shown());
    CHECK(!scr->back());
    CHECK(!motor_screen_log_take((char[8]){ 0 }, 8u));
    /* ARMED from elsewhere while it is open: it closes. */
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    type_text("kv");
    motor_screen_set_armed(true);
    scr->tick(TICK_S);
    CHECK(!motor_screen_logname_shown());
    char label[LOG_LABELLED_SIZE];
    CHECK(!motor_screen_log_take(label, sizeof(label)));
    /* A way off the screen closes it too. */
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    scr->leave();
    CHECK(!motor_screen_logname_shown());
    /* CANCEL closes it, nothing asked. */
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    type_text("kv");
    tap_key(UI_TK_CANCEL);
    CHECK(!motor_screen_logname_shown());
    CHECK(!motor_screen_log_take(label, sizeof(label)));
}

TEST_CASE(ok_hands_one_request_over_and_the_answer_is_said)
{
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    type_text("kv1200");
    tap_key(UI_TK_OK);
    CHECK(!motor_screen_logname_shown());
    char label[LOG_LABELLED_SIZE];
    CHECK(motor_screen_log_take(label, sizeof(label)));
    CHECK_STR_EQ(label, "kv1200");
    CHECK(!motor_screen_log_take(label, sizeof(label)));   /* once */
    /* While the logger renames, a tap says the card is busy. */
    scr->tick(TICK_S);
    CHECK(!motor_screen_logname_live());
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(alert(), TR(MO_LOG_BUSY));
    /* Renamed: the notice names the new file. */
    motor_screen_log_done(FM_OK, "MOTOR042_kv1200.CSV");
    char want[UI_ALERT_MAX];
    snprintf(want, sizeof(want), TR(MO_LOG_RENAMED), "MOTOR042_kv1200.CSV");
    CHECK_STR_EQ(alert(), want);
    /* Pressed again: the label it carries now. */
    a_log("MOTOR042_kv1200.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK_STR_EQ(motor_screen_logname_keyboard()->text, "kv1200");
    /* OK on the label it has asks nothing. */
    tap_key(UI_TK_OK);
    CHECK(!motor_screen_logname_shown());
    CHECK(!motor_screen_log_take(label, sizeof(label)));
    /* The refusals, each in its words. */
    tap(MO_LOGBTN_X, LOG_Y);
    type_text("x");
    tap_key(UI_TK_OK);
    CHECK(motor_screen_log_take(label, sizeof(label)));
    CHECK_STR_EQ(label, "kv1200x");
    motor_screen_log_done(FM_ERR_EXISTS, "MOTOR042_kv1200x.CSV");
    snprintf(want, sizeof(want), TR(FM_E_EXISTS), "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), want);
    motor_screen_log_done(FM_ERR_CARD_GONE, "MOTOR042_kv1200x.CSV");
    snprintf(want, sizeof(want), TR(MO_LOG_MISSING), "MOTOR042_kv1200.CSV");
    CHECK_STR_EQ(alert(), want);
    motor_screen_log_done(FM_ERR_NOT_FOUND, "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), want);
    motor_screen_log_done(FM_ERR_BUSY, "");
    CHECK_STR_EQ(alert(), TR(MO_LOG_BUSY));
    motor_screen_log_done(FM_ERR_FULL, "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), TR(FM_E_FULL));
    motor_screen_log_done(FM_ERR_DENIED, "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), TR(FM_E_DENIED));
    motor_screen_log_done(FM_ERR_ARMED, "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), TR(FM_GATE_ARMED));
    motor_screen_log_done(FM_ERR_LOGGING, "MOTOR042_kv1200x.CSV");
    CHECK_STR_EQ(alert(), TR(FM_GATE_LOGGING));
    scr->tick(TICK_S);
    CHECK(motor_screen_logname_live());
}

TEST_CASE(an_empty_ok_gives_the_plain_name_back_or_nothing)
{
    /* A labelled log: CLR and OK ask for the plain name. */
    fresh();
    a_log("MOTOR042_kv1200.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    tap_key(UI_TK_CLR);
    CHECK_EQ(motor_screen_logname_keyboard()->len, 0);
    tap_key(UI_TK_OK);
    CHECK(!motor_screen_logname_shown());
    char label[LOG_LABELLED_SIZE] = "x";
    CHECK(motor_screen_log_take(label, sizeof(label)));
    CHECK_STR_EQ(label, "");
    /* A plain one: OK on the empty field closes and asks nothing. */
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    tap_key(UI_TK_OK);
    CHECK(!motor_screen_logname_shown());
    CHECK(!motor_screen_log_take(label, sizeof(label)));
    /* A press of OK that slides off it is no OK. */
    fresh();
    a_log("MOTOR042_kv.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    tap_key(UI_TK_CLR);
    int x = 0, y = 0;
    key_at(UI_TK_OK, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y - 200, TOUCH_EVENT_UP, 1);
    CHECK(motor_screen_logname_shown());
    /* Nor is a release by another contact. */
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 2);
    CHECK(motor_screen_logname_shown());
    CHECK(!motor_screen_log_take(label, sizeof(label)));
}

TEST_CASE(the_cursor_edits_the_label_where_it_stands)
{
    fresh();
    a_log("MOTOR042_kv1200.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    const ui_textkey_t *k = motor_screen_logname_keyboard();
    /* The cursor starts at the label's end; <- and -> are in the bottom
     * row. */
    CHECK_EQ(k->cur, 6);
    CHECK(ui_textkey_key_rect(k, UI_TK_LEFT).w > 0);
    CHECK(ui_textkey_key_rect(k, UI_TK_RIGHT).w > 0);
    CHECK_STR_EQ(ui_textkey_key_label(k, UI_TK_LEFT), "<-");
    CHECK_STR_EQ(ui_textkey_key_label(k, UI_TK_RIGHT), "->");
    /* Two to the left, a key types there. */
    tap_key(UI_TK_LEFT);
    tap_key(UI_TK_LEFT);
    CHECK_EQ(k->cur, 4);
    type_text("x");
    CHECK_STR_EQ(k->text, "kv12x00");
    CHECK_EQ(k->cur, 5);
    /* DEL takes the character before the cursor. */
    tap_key(UI_TK_DEL);
    CHECK_STR_EQ(k->text, "kv1200");
    /* A tap at the field's left edge puts the cursor at 0. */
    const gfx_rect_t f = ui_textkey_field_rect(k);
    CHECK(f.w > 0);
    tap(f.x + UI_TEXTKEY_FIELD_IN, f.y + f.h / 2);
    CHECK_EQ(k->cur, 0);
    type_text("a");
    CHECK_STR_EQ(k->text, "akv1200");
    /* -> past the end stays at the end; a tap right of the text too. */
    for (int i = 0; i < 10; ++i) {
        tap_key(UI_TK_RIGHT);
    }
    CHECK_EQ(k->cur, k->len);
    tap_key(UI_TK_LEFT);
    tap(f.x + f.w - UI_TEXTKEY_FIELD_IN - 1, f.y + f.h / 2);
    CHECK_EQ(k->cur, k->len);
    /* At 46 bytes a key mid-label is refused, cursor and text kept. */
    tap_key(UI_TK_CLR);
    for (int i = 0; i < (int)LOG_LABEL_MAX; ++i) {
        type_text("k");
    }
    tap_key(UI_TK_LEFT);
    const int at = k->cur;
    type_text("z");
    CHECK_EQ(k->len, (int)LOG_LABEL_MAX);
    CHECK_EQ(k->cur, at);
    CHECK(strchr(k->text, 'z') == NULL);
    /* OK asks for the label as edited. */
    tap_key(UI_TK_CLR);
    type_text("kv");
    tap_key(UI_TK_LEFT);
    type_text("1");
    tap_key(UI_TK_OK);
    char label[LOG_LABELLED_SIZE];
    CHECK(motor_screen_log_take(label, sizeof(label)));
    CHECK_STR_EQ(label, "k1v");
}

TEST_CASE(a_refused_label_keeps_the_keyboard_open)
{
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_LOGBTN_X, LOG_Y);
    type_text("a");
    /* "*" from the marks layer: a FAT name cannot hold it. */
    tap_key(UI_TK_LAYER);
    CHECK(type_label("*"));
    tap_key(UI_TK_LAYER);
    type_text("b");
    CHECK_STR_EQ(motor_screen_logname_keyboard()->text, "a*b");
    tap_key(UI_TK_OK);
    CHECK(motor_screen_logname_shown());
    CHECK(motor_screen_logname_keyboard()->refused);
    char label[LOG_LABELLED_SIZE];
    CHECK(!motor_screen_log_take(label, sizeof(label)));
    /* A full stop at the label's end too. */
    tap_key(UI_TK_CLR);
    type_text("kv");
    tap_key(UI_TK_LAYER);
    CHECK(type_label("."));
    tap_key(UI_TK_LAYER);
    tap_key(UI_TK_OK);
    CHECK(motor_screen_logname_shown());
    CHECK(!motor_screen_log_take(label, sizeof(label)));
    /* Dots and separators inside it are taken. */
    type_text("2");
    tap_key(UI_TK_LAYER);
    CHECK(type_label("_"));
    tap_key(UI_TK_LAYER);
    type_text("a");
    tap_key(UI_TK_OK);
    CHECK(!motor_screen_logname_shown());
    CHECK(motor_screen_log_take(label, sizeof(label)));
    CHECK_STR_EQ(label, "kv.2_a");
}

TEST_CASE(the_settings_list_hides_the_button_and_program_shows_it)
{
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_SETTINGS_X, MO_SETTINGS_Y);
    CHECK(motor_screen_settings_shown());
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(!motor_screen_logname_shown());
    /* PROGRAM's list: the button is there and live. */
    fresh();
    a_log("MOTOR042.CSV");
    tap(MO_TAB_X(1), MO_TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    tap(MO_LOGBTN_X, LOG_Y);
    CHECK(motor_screen_logname_shown());
    /* A lost touch drops the keyboard's press, not the keyboard. */
    int x = 0, y = 0;
    key_at(UI_TK_OK, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK(motor_screen_logname_shown());
}

TEST_CASE(closing_the_keyboard_paints_the_screen_as_before)
{
    fresh();
    a_log("MOTOR042.CSV");
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    tap(MO_LOGBTN_X, LOG_Y);
    scr->tick(TICK_S);
    scr->render(&cv, 0);
    /* The keyboard over the footer: ARM's slot is not drawn. */
    CHECK(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    tap_key(UI_TK_CANCEL);
    scr->tick(TICK_S);
    scr->render(&cv, 0);
    /* The footer with ARM and TARGET, the strip and RENAME LOG: as they
     * were before it opened. */
    CHECK(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);
    free(before);
}

int main(void)
{
    settings_init();
    RUN(the_button_lies_in_the_strip_row_clear_of_the_tabs_and_the_rail);
    RUN(a_dimmed_tap_says_why_and_opens_nothing);
    RUN(a_live_tap_opens_the_keyboard_on_the_label);
    RUN(the_keyboard_covers_arm_and_the_knob);
    RUN(the_home_tag_and_leaving_safe_close_the_keyboard);
    RUN(ok_hands_one_request_over_and_the_answer_is_said);
    RUN(an_empty_ok_gives_the_plain_name_back_or_nothing);
    RUN(a_refused_label_keeps_the_keyboard_open);
    RUN(the_cursor_edits_the_label_where_it_stands);
    RUN(the_settings_list_hides_the_button_and_program_shows_it);
    RUN(closing_the_keyboard_paints_the_screen_as_before);
    free(fb);
    return test_summary("motor_logname");
}
