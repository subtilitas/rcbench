/*
 * SETUP > SD CARD's two log folders: a row each under the two switches, a
 * tap on it opens the keyboard in folder mode, OK sets the folder as an
 * edit, a refused OK and CANCEL keep the folder in force, SAVE keeps it,
 * and the warning on leaving names both strings.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "card_tree.h"
#include "ui_textkey.h"

/* The middle of string @p id's row on SD CARD's list, panel coordinates. */
static int text_row_y(setting_text_id_t id)
{
    const int row = settings_screen_text_row(SET_CAT_CARD, id);
    return LIST_Y + row * ROW_PITCH + ROW_H / 2;
}

/* SETUP on SD CARD's list, the model at its defaults. */
static void fresh_card(void)
{
    fresh_hub();
    open_category(SET_CAT_CARD);
}

static const ui_textkey_t *kb(void)
{
    return settings_screen_textkey();
}

/* A tap on the keyboard's key @p key, through the router. */
static void key(int k)
{
    const gfx_rect_t r = ui_textkey_key_rect(kb(), k);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/* The index of a character key of the name keyboard. */
static int char_key(char ch)
{
    static const char chars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
    const char *p = strchr(chars, ch);
    return (p != NULL) ? (int)(p - chars) : -1;
}

/* @p text typed after CLR, "/" by the dot key. */
static void type_folder(const char *text)
{
    key(UI_TK_CLR);
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '/') ? UI_TK_DOT : (*p == ' ') ? UI_TK_SPACE
                                                  : char_key(*p));
    }
}

static void open_folder(setting_text_id_t id)
{
    tap(XL, text_row_y(id));
}

/* Line @p i of the warning, or "" past the last. */
static const char *warn_line(int i)
{
    static char line[128];
    if (!settings_screen_warn_line(i, line, sizeof(line))) {
        line[0] = '\0';
    }
    return line;
}

TEST_CASE(sd_card_lists_the_folders_under_the_switches)
{
    fresh_card();
    CHECK_EQ(settings_screen_category(), SET_CAT_CARD);
    CHECK_EQ(settings_screen_rows(SET_CAT_CARD), 4);
    CHECK_EQ(settings_screen_text_row(SET_CAT_CARD, SET_TEXT_LOG_AUTO_DIR), 2);
    CHECK_EQ(settings_screen_text_row(SET_CAT_CARD, SET_TEXT_LOG_USER_DIR), 3);
    CHECK_EQ(settings_screen_text_row(SET_CAT_CARD, SET_TEXT_DUT_NAME), -1);
    CHECK_EQ(settings_screen_text_row(SET_CAT_APP, SET_TEXT_LOG_AUTO_DIR), -1);
    /* The four fit the list: nothing to scroll. */
    int max = -1;
    (void)settings_screen_scroll(&max);
    CHECK_EQ(max, 0);
    CHECK(!kb()->open);
    ui_router_render(&s_c, 0);
}

TEST_CASE(a_tap_on_a_folder_row_opens_the_folder_keyboard_on_it)
{
    fresh_card();
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    CHECK(kb()->open);
    CHECK(kb()->folder);
    CHECK_STR_EQ(kb()->text, CARD_AUTO_LOGS);
    CHECK_STR_EQ(kb()->title, "Auto log folder");
    CHECK_STR_EQ(ui_textkey_key_label(kb(), UI_TK_DOT), "/");
    /* The keyboard covers the list and the strip. */
    CHECK(kb()->area.y + kb()->area.h >= STRIP_Y + STRIP_H - UI_BAND_H);
    CHECK(kb()->area.y + kb()->area.h + UI_BAND_H <= H - 34);
    ui_router_render(&s_c, 0);
    key(UI_TK_CANCEL);
    CHECK(!kb()->open);

    ui_text_set_language(UI_LANG_DE);
    open_folder(SET_TEXT_LOG_USER_DIR);
    CHECK_STR_EQ(kb()->text, CARD_USER_LOGS);
    CHECK_STR_EQ(kb()->title, "Ordner User-Logs");
    key(UI_TK_CANCEL);
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    CHECK_STR_EQ(kb()->title, "Ordner Auto-Logs");
    key(UI_TK_CANCEL);
    ui_text_set_language(UI_LANG_EN);
}

/* OK sets the folder in force, unsaved; SAVE keeps it. */
TEST_CASE(ok_sets_the_folder_and_save_keeps_it)
{
    fresh_card();
    settings_set_store(&s_text_store);
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("RUNS/A-1");
    CHECK_STR_EQ(kb()->text, "RUNS/A-1");
    key(UI_TK_OK);
    CHECK(!kb()->open);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), "RUNS/A-1");
    CHECK_STR_EQ(settings_stored_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_tick(true));
    CHECK_STR_EQ(settings_stored_text(SET_TEXT_LOG_AUTO_DIR), "RUNS/A-1");
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    settings_set_store(NULL);
}

/* A refused OK leaves the keyboard open and the folder in force as it
 * was; the edge of each limit is taken. */
TEST_CASE(a_refused_ok_keeps_the_folder_in_force)
{
    fresh_card();
    open_folder(SET_TEXT_LOG_USER_DIR);
    type_folder("BENCHLOGS");
    key(UI_TK_OK);
    CHECK(kb()->open);
    CHECK(kb()->refused);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_LONG);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    CHECK(!settings_dirty());
    ui_router_render(&s_c, 0);
    key(UI_TK_DEL);
    key(UI_TK_OK);
    CHECK(!kb()->open);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "BENCHLOG");

    open_folder(SET_TEXT_LOG_USER_DIR);
    type_folder("BENCH/KST");
    key(UI_TK_OK);
    CHECK(kb()->open);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_BENCH);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "BENCHLOG");
    key(char_key('X'));
    key(UI_TK_OK);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "BENCH/KSTX");

    open_folder(SET_TEXT_LOG_USER_DIR);
    type_folder("A/B/C");
    key(UI_TK_OK);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_DEEP);
    type_folder("MY LOGS");
    key(UI_TK_OK);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_CHAR);
    type_folder("LOGS.OLD");
    CHECK_STR_EQ(kb()->text, "LOGS/OLD");     /* the dot key types "/" */
    type_folder("AUX/LOGS");
    key(UI_TK_OK);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_RESERVED);
    type_folder("");
    key(UI_TK_OK);
    CHECK_EQ(kb()->why, (uint8_t)CARD_FOLDER_EMPTY);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "BENCH/KSTX");
    key(UI_TK_CANCEL);
}

/* CANCEL and the home tag drop what was typed; the home tag stays on the
 * list.  A route the application takes closes it the same way. */
TEST_CASE(cancel_and_the_home_tag_keep_the_folder)
{
    fresh_card();
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("OTHER");
    key(UI_TK_CANCEL);
    CHECK(!kb()->open);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK(!settings_dirty());

    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("OTHER");
    tap(home_x(), home_y());
    CHECK(!kb()->open);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_CARD);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);

    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("OTHER");
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(!kb()->open);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
}

/* While the keyboard is open nothing under it presses: the switch's "+"
 * is under its title row, and where SAVE is the keyboard has its OK. */
TEST_CASE(nothing_under_the_keyboard_presses)
{
    fresh_card();
    settings_set(SET_LOG_AUTO, 0.0f);
    open_folder(SET_TEXT_LOG_USER_DIR);
    tap(XP, LIST_Y + ROW_H / 2);
    CHECK(!settings_get_bool(SET_LOG_AUTO));
    CHECK(kb()->open);
    const gfx_rect_t ok = ui_textkey_key_rect(kb(), UI_TK_OK);
    /* 60 px inside SAVE's right end. */
    const int on_save = SAVE_X + HUB_COL_W - 60;
    CHECK(gfx_rect_contains(ok, on_save, SAVE_Y + SAVE_H / 2 - UI_BAND_H));
    tap(on_save, SAVE_Y + SAVE_H / 2);
    CHECK(!kb()->open);                       /* the keyboard's OK */
    CHECK(!settings_save_asked());
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
}

/* A press on a folder row that becomes a scroll opens nothing, and a
 * release off the row neither. */
TEST_CASE(a_drag_from_a_folder_row_opens_nothing)
{
    fresh_card();
    const int y = text_row_y(SET_TEXT_LOG_AUTO_DIR);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = XL, .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    e.point.y = (int16_t)(y - 20);
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(!kb()->open);

    /* Within the slop, but released on the row below. */
    e = (touch_event_t){ .type = TOUCH_EVENT_DOWN,
                         .point = { .id = 1, .x = XL, .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    e.point.y = (int16_t)text_row_y(SET_TEXT_LOG_USER_DIR);
    ui_router_event(&e);
    CHECK(!kb()->open);
}

/* Both kinds may name one folder. */
TEST_CASE(both_folders_can_be_one_folder)
{
    fresh_card();
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("LOGS");
    key(UI_TK_OK);
    open_folder(SET_TEXT_LOG_USER_DIR);
    type_folder("LOGS");
    key(UI_TK_OK);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), "LOGS");
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "LOGS");
}

/* A folder in force in lower case (a store written elsewhere) is handed
 * back in upper case by OK. */
TEST_CASE(lower_case_in_force_is_stored_upper_case)
{
    fresh_card();
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "my/logs");
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    CHECK_STR_EQ(kb()->text, "my/logs");
    key(UI_TK_OK);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), "MY/LOGS");
}

/* The warning names both strings with their stored and their new text;
 * DISCARD puts both back. */
TEST_CASE(the_leave_warning_names_both_folders)
{
    fresh_card();
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("RUNS");
    key(UI_TK_OK);
    open_folder(SET_TEXT_LOG_USER_DIR);
    type_folder("TESTS/A");
    key(UI_TK_OK);
    to_hub();
    tap(home_x(), home_y());
    CHECK(settings_screen_warning());
    CHECK_STR_EQ(warn_line(0), "Auto log folder: BENCH/LOGS -> RUNS");
    CHECK_STR_EQ(warn_line(1), "User log folder: USER/LOGS -> TESTS/A");
    CHECK_STR_EQ(warn_line(2), "");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(warn_line(0), "Ordner Auto-Logs: BENCH/LOGS -> RUNS");
    CHECK_STR_EQ(warn_line(1), "Ordner User-Logs: USER/LOGS -> TESTS/A");
    ui_text_set_language(UI_LANG_EN);
    ui_router_render(&s_c, 0);
    /* DISCARD, under the strip's second column. */
    tap(RESET_X + 128, STRIP_Y + STRIP_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    CHECK(!settings_dirty());
}

/* RESET CATEGORY puts the folders back with the switches. */
TEST_CASE(reset_category_puts_the_folders_back)
{
    fresh_card();
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "RUNS");
    settings_set_text(SET_TEXT_LOG_USER_DIR, "TESTS");
    settings_set_text(SET_TEXT_DUT_NAME, "X8");
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "X8");
}

/* The row draws its folder: a longer one repaints the row. */
TEST_CASE(the_row_shows_the_folder_in_force)
{
    fresh_card();
    ui_router_render(&s_c, 0);
    gfx_color_t *before = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, s_fb, (size_t)W * H * sizeof(gfx_color_t));
    open_folder(SET_TEXT_LOG_AUTO_DIR);
    type_folder("ABCDEFGH/12345678");
    key(UI_TK_OK);
    ui_router_render(&s_c, 0);
    const int y0 = text_row_y(SET_TEXT_LOG_AUTO_DIR) - ROW_H / 2;
    CHECK(memcmp(before + (size_t)y0 * W, s_fb + (size_t)y0 * W,
                 (size_t)ROW_H * W * sizeof(gfx_color_t)) != 0);
    free(before);
}

int main(void)
{
    RUN(sd_card_lists_the_folders_under_the_switches);
    RUN(a_tap_on_a_folder_row_opens_the_folder_keyboard_on_it);
    RUN(ok_sets_the_folder_and_save_keeps_it);
    RUN(a_refused_ok_keeps_the_folder_in_force);
    RUN(cancel_and_the_home_tag_keep_the_folder);
    RUN(nothing_under_the_keyboard_presses);
    RUN(a_drag_from_a_folder_row_opens_nothing);
    RUN(both_folders_can_be_one_folder);
    RUN(lower_case_in_force_is_stored_upper_case);
    RUN(the_leave_warning_names_both_folders);
    RUN(reset_category_puts_the_folders_back);
    RUN(the_row_shows_the_folder_in_force);
    return test_summary("settings_folders");
}
