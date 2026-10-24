/**
 * @file test_logview_delete.c
 * @brief Host unit tests for deleting a file in the log viewer.
 *
 * Tested is the question, who may answer it, and what a delete, a cancel, a
 * refusal and a touch loss leave behind.
 *
 * Cases:
 * - delete_asks_first_and_deletes_only_the_file_it_named
 * - cancel_deletes_nothing_and_keeps_the_selection
 * - a_finger_that_slides_off_delete_deletes_nothing
 * - only_the_finger_that_pressed_delete_can_release_it
 * - a_touch_loss_drops_the_press_on_delete
 * - delete_needs_a_selected_file
 * - without_remove_there_is_no_delete
 * - deleting_the_open_file_drops_what_was_read_from_it
 * - deleting_another_file_keeps_the_open_one
 * - a_refused_delete_keeps_the_file_listed
 * - a_rescan_or_leaving_the_screen_closes_the_question
 * - the_question_leaves_no_stale_pixels
 * - the_footer_message_follows_the_language
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

TEST_CASE(delete_asks_first_and_deletes_only_the_file_it_named)
{
    fresh();
    ask_to_delete(0);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* While the question is open, nothing behind it answers: not OPEN, not
     * RESCAN, not a row. */
    tap(OPEN_X, FOOT_CY);
    tap(RESCAN_X, FOOT_CY);
    tap(400, BR_ROW_Y(1));
    draw();
    CHECK_EQ(g_remove_calls, 0);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_analysis() == NULL);

    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");

    /* The list is read again, so the first row is now the second file. */
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 2);
    CHECK_STR_EQ(g_removed_name, "PRUEFUNG.CSV");
}

TEST_CASE(cancel_deletes_nothing_and_keeps_the_selection)
{
    fresh();
    ask_to_delete(2);
    tap(DQ_CANCEL_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* Back on the list with the row still chosen: DELETE asks about it again
     * without a second tap on the row. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "README.CSV");
}

TEST_CASE(a_finger_that_slides_off_delete_deletes_nothing)
{
    fresh();
    ask_to_delete(0);
    send(TOUCH_EVENT_DOWN, DQ_DELETE_X, DQ_CY);
    send(TOUCH_EVENT_MOVE, DQ_DELETE_X, DQ_CY + 120);
    send(TOUCH_EVENT_UP, DQ_DELETE_X, DQ_CY + 120);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* And from CANCEL onto DELETE is neither: the question stays open. */
    send(TOUCH_EVENT_DOWN, DQ_CANCEL_X, DQ_CY);
    send(TOUCH_EVENT_UP, DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
}

TEST_CASE(only_the_finger_that_pressed_delete_can_release_it)
{
    /* The panel reports up to five contacts.  A second finger that lands
     * beside the button, slides onto it and lifts did not press DELETE. */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 2, 400, 160);              /* B, off the buttons */
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);    /* A, on DELETE       */
    send_id(TOUCH_EVENT_MOVE, 2, DQ_DELETE_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 2, DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    /* Nor does a finger landing on CANCEL while A is down take the press. */
    send_id(TOUCH_EVENT_DOWN, 3, DQ_CANCEL_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 3, DQ_CANCEL_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);

    send_id(TOUCH_EVENT_UP, 1, DQ_DELETE_X, DQ_CY);      /* A lifts on DELETE  */
    CHECK_EQ(g_remove_calls, 1);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
}

TEST_CASE(a_touch_loss_drops_the_press_on_delete)
{
    /*
     * Lost events leave this screen's record of the glass stale.  The release
     * of the contact holding DELETE may be one of them, so the question has
     * to take the next fresh press rather than wait for a release that will
     * not come.
     */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);
    log_viewer_screen()->cancel();                 /* its UP was lost      */
    send_id(TOUCH_EVENT_DOWN, 2, DQ_DELETE_X, DQ_CY);
    send_id(TOUCH_EVENT_UP, 2, DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);

    /* And a release of that contact that does arrive after the cancel is not
     * an answer. */
    fresh();
    ask_to_delete(0);
    send_id(TOUCH_EVENT_DOWN, 1, DQ_DELETE_X, DQ_CY);
    log_viewer_screen()->cancel();
    send_id(TOUCH_EVENT_UP, 1, DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 1);
}

TEST_CASE(delete_needs_a_selected_file)
{
    fresh();
    tap(DELETE_X, FOOT_CY);            /* nothing selected */
    tap(DQ_DELETE_X, DQ_CY);           /* lands on the list, not a question */
    draw();
    CHECK_EQ(g_remove_calls, 0);

    g_dir_index = 0;
    log_viewer_refresh();
    ask_to_delete(0);                  /* a folder */
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(without_remove_there_is_no_delete)
{
    fresh();
    log_viewer_io_t io = k_io;
    io.remove = NULL;
    log_viewer_set_io(&io);
    log_viewer_refresh();
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(deleting_the_open_file_drops_what_was_read_from_it)
{
    fresh();
    open_file(0);
    tap(PLOT_X, IM_BTN_CY);
    CHECK(log_viewer_data() != NULL);
    tap(80, FOOT_CY);                  /* FIELDS */
    tap(BACK_X, IM_BTN_CY);            /* the list, row 0 still chosen */

    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
    CHECK(log_viewer_data() == NULL);
    CHECK(log_viewer_analysis() == NULL);
    CHECK_STR_EQ(log_viewer_open_name(), "");
}

TEST_CASE(deleting_another_file_keeps_the_open_one)
{
    fresh();
    open_file(0);
    tap(BACK_X, IM_BTN_CY);
    ask_to_delete(1);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_removed_name, "PRUEFUNG.CSV");
    CHECK(log_viewer_analysis() != NULL);
    CHECK_STR_EQ(log_viewer_open_name(), "BENCH_01.CSV");
}

TEST_CASE(a_refused_delete_keeps_the_file_listed)
{
    fresh();
    g_remove_refuses = true;
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 1);

    /* Still listed and still chosen: DELETE asks about the same file. */
    g_remove_refuses = false;
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_EQ(g_remove_calls, 2);
    CHECK_STR_EQ(g_removed_name, "BENCH_01.CSV");
}

TEST_CASE(a_rescan_or_leaving_the_screen_closes_the_question)
{
    /* A re-read can be of another card, where the same name is another
     * file. */
    fresh();
    ask_to_delete(0);
    log_viewer_refresh();
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);

    ask_to_delete(0);
    screen()->enter();
    tap(DQ_DELETE_X, DQ_CY);
    draw();
    CHECK_EQ(g_remove_calls, 0);
}

TEST_CASE(the_question_leaves_no_stale_pixels)
{
    /* The question replaces the list; drawn over a frame of the list, it must
     * leave nothing of it behind, and the list drawn back must too. */
    if (s_fb_b == NULL) {
        s_fb_b = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_t cb;

    fresh();
    tap(400, BR_ROW_Y(3));
    screen()->render(&s_c, 0);
    tap(DELETE_X, FOOT_CY);
    screen()->render(&s_c, 0);

    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cb, s_fb_b, W, H, W);
    reset_screen();
    tap(400, BR_ROW_Y(3));
    tap(DELETE_X, FOOT_CY);
    screen()->render(&cb, 0);

    int count = 0;
    int at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("question over list: %d stale pixel(s), first at (%d,%d)",
               count, at % W, at / W);
    }

    tap(DQ_CANCEL_X, DQ_CY);
    screen()->render(&s_c, 0);
    memset(s_fb_b, 0, (size_t)W * H * sizeof(gfx_color_t));
    reset_screen();
    tap(400, BR_ROW_Y(3));
    screen()->render(&cb, 0);
    at = first_diff(s_fb, s_fb_b, &count);
    if (at >= 0) {
        T_FAIL("list over question: %d stale pixel(s), first at (%d,%d)",
               count, at % W, at / W);
    }
}

/**
 * @brief The footer message in @p lang for @p id with @p name.
 * @param lang  the language
 * @param id    the text, a format of one name
 * @param name  the name
 * @return the message, in a buffer the next call overwrites
 */
static const char *line_in(ui_lang_t lang, ui_text_id_t id, const char *name)
{
    static char out[160];
    snprintf(out, sizeof(out), ui_tr_in(lang, id), name);
    return out;
}

/*
 * The footer message is worded when it is read, so a change of language
 * rewords a message already standing: a refused delete said in English, a
 * delete said in German, and the reader's verdict on a file.
 */
TEST_CASE(the_footer_message_follows_the_language)
{
    fresh();
    g_remove_refuses = true;
    ask_to_delete(0);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(strcmp(ui_tr_in(UI_LANG_EN, TX_LOG_CANNOT_DELETE),
                 ui_tr_in(UI_LANG_DE, TX_LOG_CANNOT_DELETE)) != 0);
    CHECK_STR_EQ(log_viewer_message(),
                 line_in(UI_LANG_EN, TX_LOG_CANNOT_DELETE, "BENCH_01.CSV"));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(log_viewer_message(),
                 line_in(UI_LANG_DE, TX_LOG_CANNOT_DELETE, "BENCH_01.CSV"));
    draw();

    /* Said in German, read in English. */
    g_remove_refuses = false;
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(log_viewer_message(),
                 line_in(UI_LANG_DE, TX_LOG_DELETED, "BENCH_01.CSV"));
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(log_viewer_message(),
                 line_in(UI_LANG_EN, TX_LOG_DELETED, "BENCH_01.CSV"));
    draw();

    /* The reader's verdict on a header with no row under it. */
    fresh();
    snprintf(g_long, sizeof(g_long), "%s", "t,v\n");
    g_removed[0] = g_removed[1] = g_removed[2] = true;
    log_viewer_refresh();
    open_file(5);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_message(),
                 ui_tr_in(UI_LANG_EN, TX_LOG_ERR_NO_ROWS));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(log_viewer_message(),
                 ui_tr_in(UI_LANG_DE, TX_LOG_ERR_NO_ROWS));
    draw();
    ui_text_set_language(UI_LANG_EN);
    g_long[0] = '\0';
}

int main(void)
{
    RUN(delete_asks_first_and_deletes_only_the_file_it_named);
    RUN(cancel_deletes_nothing_and_keeps_the_selection);
    RUN(a_finger_that_slides_off_delete_deletes_nothing);
    RUN(only_the_finger_that_pressed_delete_can_release_it);
    RUN(a_touch_loss_drops_the_press_on_delete);
    RUN(delete_needs_a_selected_file);
    RUN(without_remove_there_is_no_delete);
    RUN(deleting_the_open_file_drops_what_was_read_from_it);
    RUN(deleting_another_file_keeps_the_open_one);
    RUN(a_refused_delete_keeps_the_file_listed);
    RUN(a_rescan_or_leaving_the_screen_closes_the_question);
    RUN(the_question_leaves_no_stale_pixels);
    RUN(the_footer_message_follows_the_language);
    return test_summary("logview_delete");
}
