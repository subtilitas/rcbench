/*
 * The ESC signal-wire session as drawn and as said, in both languages: every stage's
 * page on a canvas with the colour that carries its meaning, every message, refusal
 * and session result in words, the connect page's editors, ESCape32's editors, the
 * card's answers in words and the file list's pages.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"

#include <stdlib.h>

#include "ew_id.h"
#include "ew_session.h"
#include "link_pages.h"
#include "outputs_pages.h"
#include "ui_theme.h"

#include "esc_wire_rig.h"

/** @brief The canvas's width. */
#define W 1024
/** @brief The canvas's height. */
#define H 600

static gfx_color_t *g_fb;
static gfx_canvas_t g_cv;

/** @brief The session drawn on the canvas. */
static void draw(void)
{
    if (g_fb == NULL) {
        g_fb = calloc((size_t)W * H, sizeof(*g_fb));
        REQUIRE_ALLOC(g_fb);
        gfx_canvas_init(&g_cv, g_fb, W, H, W);
    }
    gfx_fill_rect(&g_cv, 0, 0, W, H, 0u);
    esc_wire_pages.render(&g_cv);
}

/**
 * @brief Whether a colour of the theme is in a rectangle of the canvas.
 * @param r   The rectangle.
 * @param col The colour.
 * @return true when a pixel has it.
 */
static bool has(gfx_rect_t r, ui_color_id_t col)
{
    const gfx_color_t want = ui_theme_color(col);

    for (int y = r.y; y < r.y + r.h && y < H; ++y) {
        for (int x = r.x; x < r.x + r.w && x < W; ++x) {
            if (g_fb[(size_t)y * W + (size_t)x] == want) {
                return true;
            }
        }
    }
    return false;
}

/** @brief The band of hold pages' lines and of the session's pages' words. */
static gfx_rect_t lines_band(void)
{
    return (gfx_rect_t){ 0, (int16_t)(k_ew_area.y + 40), W, 420 };
}

/**
 * @brief The session fed by hand: an ESC read and held, in @p state.
 * @param fw     The firmware.
 * @param layout The layout.
 * @param flags  LINK_EW_F_* beside HAVE_READ.
 * @param why    Why it is not written.
 * @param state  A link_esc_state_t.
 */
static void fed(uint8_t fw, uint8_t layout, uint16_t flags, uint8_t why, uint8_t state)
{
    session_up(up_am_rig);
    memset(&g_f, 0, sizeof(g_f));
    g_f.rd.phase = ESC_LINK_READY;
    g_f.rd.known = true;
    g_f.rd.held = true;
    g_f.rd.ours = true;
    g_f.rd.state = state;
    g_f.rd.gpio = 5u;
    g_f.rd.fw = fw;
    g_f.rd.layout = layout;
    g_f.rd.why = why;
    g_f.rd.flags = (uint16_t)(flags | LINK_EW_F_HAVE_READ);
    g_f.image.gen = 1u;
    g_f.image.len = ew_map_len(fw);
    g_hand = true;
    esc_wire_panel_feed(&g_f);
}

TEST_CASE(the_connect_page_draws_its_red_hold_and_dims_what_by_hand_has_not)
{
    session_up(up_am_rig);
    draw();
    CHECK(has(esc_wire_panel_hold_rect(), UI_C_DANGER));
    CHECK(has(lines_band(), UI_C_WARN));
    CHECK(!has(ewp_row_rect(2), UI_C_TEXT_FAINT));
    ewp_s.power = LINK_ESC_BY_HAND;
    draw();
    CHECK(has(ewp_row_rect(2), UI_C_TEXT_FAINT));
    CHECK(has(ewp_row_rect(3), UI_C_TEXT_FAINT));
    CHECK(has(ewp_row_rect(5), UI_C_TEXT_FAINT));
    CHECK(!has(ewp_row_rect(4), UI_C_TEXT_FAINT));
    esc_wire_panel_set_targets(NULL, 0u, true);
    draw();
    /* No pin: the row in the warning colour. */
    CHECK(has(ewp_row_rect(0), UI_C_WARN));
}

TEST_CASE(a_stage_under_way_draws_abort_in_red)
{
    fed(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u, LINK_ESC_POWER_ON);
    g_f.rd.flags = 0u;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_ENTERING);
    draw();
    CHECK(has(esc_wire_panel_action_rect(0), UI_C_DANGER));
    /* BY HAND's two: ABORT red, POWER IS ON not. */
    g_f.rd.state = LINK_ESC_HAND_WAIT;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_HAND);
    draw();
    CHECK(has(esc_wire_panel_action_rect(0), UI_C_DANGER));
    CHECK(!has(esc_wire_panel_action_rect(1), UI_C_DANGER));
    CHECK(has(lines_band(), UI_C_WARN));
    /* A write says so in the warning colour. */
    g_f.rd.state = LINK_ESC_RUN;
    g_f.rd.flags = LINK_EW_F_HAVE_READ | LINK_EW_F_WRITING;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_RUNNING);
    draw();
    CHECK(has(lines_band(), UI_C_WARN));
}

TEST_CASE(the_identity_tab_says_writable_or_read_only_for_every_firmware)
{
    static const struct {
        uint8_t fw;
        uint8_t layout;
    } k_fw[] = { { EW_FW_S, 33u }, { EW_FW_BJ, 208u }, { EW_FW_AM, 4u }, { EW_FW_E32, 17u } };

    for (size_t i = 0u; i < sizeof(k_fw) / sizeof(k_fw[0]); ++i) {
        fed(k_fw[i].fw, k_fw[i].layout, LINK_EW_F_WRITABLE, 0u, LINK_ESC_READY);
        CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
        draw();
        CHECK(has(lines_band(), UI_C_OK));
        CHECK(has(esc_wire_panel_action_rect(2), UI_C_TEXT_FAINT)
              || !has(esc_wire_panel_action_rect(2), UI_C_DANGER));
        fed(k_fw[i].fw, k_fw[i].layout, 0u, EW_WHY_OLD_LAYOUT, LINK_ESC_READY);
        draw();
        CHECK(!has(lines_band(), UI_C_OK));
        CHECK(has(lines_band(), UI_C_WARN));
    }
}

TEST_CASE(an_erased_page_draws_restore_in_red_and_a_change_marks_its_row)
{
    fed(EW_FW_AM, 4u, LINK_EW_F_ERASED, EW_WHY_ERASED, LINK_ESC_READY);
    draw();
    CHECK(has(esc_wire_panel_action_rect(0), UI_C_DANGER));
    fed(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u, LINK_ESC_READY);
    ew_tap(esc_wire_panel_tab_rect(1));
    draw();
    CHECK(!has(ewp_row_rect(0), UI_C_WARN));
    ewp_row_t r;

    CHECK(ewp_row(1, 0, &r));
    ewp_s.draft[r.f->off] = (uint8_t)(ewp_s.prog.f.image.page[r.f->off] + 1u);
    draw();
    CHECK(has(ewp_row_rect(0), UI_C_WARN));
    CHECK(has(esc_wire_panel_action_rect(2), UI_C_DANGER));
}

TEST_CASE(a_layout_with_no_names_draws_its_bytes)
{
    fed(EW_FW_S, 34u, 0u, EW_WHY_OLD_LAYOUT, LINK_ESC_READY);
    ew_tap(esc_wire_panel_tab_rect(1));
    draw();
    CHECK(has(lines_band(), UI_C_TEXT_DIM));
    CHECK(esc_wire_panel_value_rect(0).w == 0);
}

TEST_CASE(every_hold_page_draws_its_title_its_lines_and_a_red_hold)
{
    static const int k_page[] = { EWP_PAGE_NO_BACKUP, EWP_PAGE_LEAVE, EWP_PAGE_RELEASE,
                                  EWP_PAGE_RESTORE };

    fed(EW_FW_AM, 4u, LINK_EW_F_WRITABLE, 0u, LINK_ESC_READY);
    for (size_t i = 0u; i < sizeof(k_page) / sizeof(k_page[0]); ++i) {
        ewp_page_open(k_page[i]);
        ewp_page_add("a line");
        draw();
        CHECK(has(esc_wire_panel_hold_rect(), UI_C_DANGER));
        CHECK(has(lines_band(), UI_C_TEXT));
        CHECK(!has(lines_band(), UI_C_WARN));
    }
    /* A note goes under a page's lines, in its colour. */
    ewp_note("a note", true);
    draw();
    CHECK(has(lines_band(), UI_C_WARN));
}

TEST_CASE(the_write_page_draws_check_in_the_warning_colour_and_its_turn_buttons)
{
    session_up(up_am_rig);
    connected();
    ewp_s.draft[0x16] = 0u;
    ew_tap(esc_wire_panel_action_rect(2));
    draw();
    CHECK(has(lines_band(), UI_C_WARN));
    CHECK(has(esc_wire_panel_hold_rect(), UI_C_DANGER));
    ew_tap(esc_wire_panel_cancel_rect());
    const ew_field_t *f;

    for (size_t i = 0u; (f = ew_map_nth(EW_FW_AM, 4u, i)) != NULL; ++i) {
        if (f->access == (uint8_t)EW_ACC_EDIT && (f->flags & EW_FF_WARN) == 0u) {
            ewp_s.draft[f->off] = (uint8_t)(ewp_s.draft[f->off] == f->lo ? f->lo + 1u : f->lo);
        }
    }
    ew_tap(esc_wire_panel_action_rect(2));
    draw();
    /* No hold yet: the line says when. */
    CHECK(!has(esc_wire_panel_action_rect(0), UI_C_DANGER));
    CHECK(has(esc_wire_panel_next_rect(), UI_C_TEXT));
}

TEST_CASE(the_choice_list_and_the_keypad_draw_a_rows_help_line)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(2));
    CHECK_EQ(ewp_s.ch_what, EWP_CH_FIELD);
    draw();
    CHECK(has((gfx_rect_t){ 0, k_ew_area.y, W, 26 }, UI_C_TEXT_DIM));
    ew_tap(esc_wire_panel_cancel_rect());
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK(esc_wire_panel_keypad()->open);
    draw();
    CHECK(has((gfx_rect_t){ 0, k_ew_area.y, W, 26 }, UI_C_TEXT_DIM));
}

TEST_CASE(the_pages_of_a_held_pin_draw_their_ways_out)
{
    fed(EW_FW_AM, 4u, LINK_EW_F_STOPPED, 0u, LINK_ESC_OPEN);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_STOPPED);
    draw();
    CHECK(has(lines_band(), UI_C_WARN));
    CHECK(has(esc_wire_panel_action_rect(1), UI_C_TEXT));
    /* The pin back. */
    fed(EW_FW_AM, 4u, 0u, 0u, LINK_ESC_NOT_BACK);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_NOT_BACK);
    draw();
    CHECK(has(esc_wire_panel_action_rect(0), UI_C_TEXT));
    /* Another writer. */
    fed(EW_FW_AM, 4u, 0u, 0u, LINK_ESC_OPEN);
    g_f.rd.ours = false;
    esc_wire_panel_feed(&g_f);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_OTHER);
    draw();
    CHECK(has(esc_wire_panel_action_rect(0), UI_C_TEXT));
    ew_tap(esc_wire_panel_action_rect(0));
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_RELEASE);
    CHECK_STR_EQ(esc_wire_panel_page_line(0), TR(EW_RELEASE_1));
    /* Held, nothing going on: the hold in red, PIN BACK beside it. */
    fed(EW_FW_AM, 4u, 0u, 0u, LINK_ESC_OPEN);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_PIN_HELD);
    draw();
    CHECK(has(esc_wire_panel_hold_rect(), UI_C_DANGER));
    CHECK(has(esc_wire_panel_action_rect(2), UI_C_TEXT));
}

/** @brief Every message says something, in the language set; an error warns. */
static void all_messages(void)
{
    esc_prog_t *m = esc_wire_panel_model();
    char why[EWP_LINE];

    for (int k = (int)ESC_PROG_MSG_ARMED; k <= (int)ESC_PROG_MSG_LOADED; ++k) {
        m->msg = (esc_prog_msg_t)k;
        m->detail = 0u;
        CHECK(ewp_message_text()[0] != '\0');
    }
    for (uint16_t d = 0u; d < 40u; ++d) {
        m->msg = ESC_PROG_MSG_SESSION;
        m->detail = d;
        CHECK(ewp_message_text()[0] != '\0');
        CHECK(ewp_message_warns());
        m->msg = ESC_PROG_MSG_REFUSED;
        CHECK(ewp_message_text()[0] != '\0');
        CHECK(ewp_message_warns());
        m->msg = ESC_PROG_MSG_LOAD_REFUSED;
        CHECK(ewp_message_text()[0] != '\0');
    }
    for (uint8_t w = 0u; w < 24u; ++w) {
        ewp_why_text(w, why, sizeof(why));
        CHECK(why[0] != '\0');
    }
    for (uint8_t s = 0u; s < 12u; ++s) {
        CHECK(ewp_state_text(s)[0] != '\0');
    }
    for (uint8_t f = 0u; f < 6u; ++f) {
        CHECK(ewp_fw_name(f)[0] != '\0');
    }
    for (uint8_t l = 0u; l < 8u; ++l) {
        ewp_s.prog.f.rd.line = l;
        ewp_line_text(why, sizeof(why));
        CHECK(why[0] != '\0');
    }
    m->msg = ESC_PROG_MSG_NONE;
    CHECK_STR_EQ(ewp_message_text(), "");
    CHECK(!ewp_message_warns());
    m->msg = ESC_PROG_MSG_WRITE_DONE;
    CHECK(!ewp_message_warns());
    m->msg = ESC_PROG_MSG_NO_LINK;
    CHECK(ewp_message_warns());
}

TEST_CASE(every_message_refusal_and_result_has_its_words_in_both_languages)
{
    session_up(up_am_rig);
    all_messages();
    ui_text_set_language(UI_LANG_DE);
    all_messages();
    /* The refusal a screen most needs. */
    esc_wire_panel_model()->msg = ESC_PROG_MSG_REFUSED;
    esc_wire_panel_model()->detail = LINK_ESC_REF_RANGE;
    CHECK_STR_EQ(ewp_message_text(), TR(EW_REF_RANGE));
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_connect_rows_open_their_editors_and_take_their_values)
{
    session_up(up_am_rig);
    /* TARGET's list. */
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK_EQ(ewp_s.ch_what, EWP_CH_TARGET);
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), "SLOT 3 GPIO 5");
    CHECK_STR_EQ(esc_wire_panel_choice_label(1), "PORT 1 GPIO 13");
    ew_tap(esc_wire_panel_choice_rect(1));
    CHECK_EQ(ewp_s.target_at, 1u);
    /* POWER's. */
    ew_tap(esc_wire_panel_value_rect(1));
    CHECK_EQ(ewp_s.ch_what, EWP_CH_POWER);
    ew_tap(ewp_choice_cancel_rect());
    CHECK_EQ(ewp_s.ch_what, EWP_CH_NONE);
    /* The numbers. */
    ew_tap(esc_wire_panel_value_rect(3));
    kp_type("0.5");
    CHECK_EQ(ewp_s.ma, 500u);
    ew_tap(esc_wire_panel_value_rect(4));
    kp_type("4");
    CHECK_EQ(ewp_s.hold_ms, 4000u);
    ew_tap(esc_wire_panel_value_rect(5));
    kp_type("2");
    CHECK_EQ(ewp_s.off_ms, 2000u);
    /* A cancel takes nothing. */
    ew_tap(esc_wire_panel_value_rect(5));
    ew_tap(ui_keypad_key_rect(esc_wire_panel_keypad(), UI_KEY_CANCEL));
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_EQ(ewp_s.off_ms, 2000u);
}

/** @brief The slot of a key of ESCape32. */
static size_t slot_of(const char *key)
{
    size_t slot = 0u;

    CHECK(ew_key_find(key, strlen(key), &slot) != NULL);
    return slot;
}

TEST_CASE(escape32s_rows_edit_by_list_by_off_and_by_a_signed_number)
{
    fed(EW_FW_E32, 17u, LINK_EW_F_WRITABLE, 0u, LINK_ESC_READY);
    ewp_row_t r;

    memset(&r, 0, sizeof(r));
    /* A named key: its list. */
    r.slot = (uint8_t)slot_of("prot_sens");
    r.k = &ew_map_e32[r.slot];
    CHECK(ewp_row_live(&r));
    ewp_edit_open(&r);
    CHECK_EQ(ewp_s.ch_what, EWP_CH_FIELD);
    ewp_choice_done(1);
    CHECK_EQ(ew_e32_get(ewp_s.draft, r.slot), r.k->lo + 1);
    CHECK(ewp_row_changed(&r));
    /* A number with an OFF. */
    r.slot = (uint8_t)slot_of("prot_temp");
    r.k = &ew_map_e32[r.slot];
    ewp_edit_open(&r);
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), "OFF");
    ewp_choice_done(0);
    CHECK_EQ(ew_e32_get(ewp_s.draft, r.slot), 0);
    ewp_edit_open(&r);
    ewp_choice_done(1);
    CHECK(esc_wire_panel_keypad()->open);
    kp_type("60");
    CHECK_EQ(ew_e32_get(ewp_s.draft, r.slot), 60);
    /* A number below zero. */
    r.slot = (uint8_t)slot_of("telem_volt");
    r.k = &ew_map_e32[r.slot];
    ewp_edit_open(&r);
    CHECK(esc_wire_panel_keypad()->open);
    ewp_keypad_done(5.0f);
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_EQ(ew_e32_get(ewp_s.draft, r.slot), 5);
    /* A value its step refuses keeps the keypad open. */
    r.slot = (uint8_t)slot_of("prot_stall");
    r.k = &ew_map_e32[r.slot];
    ewp_edit_open(&r);
    ewp_choice_done(1);
    ewp_keypad_done(15.0f);
    CHECK(esc_wire_panel_keypad()->open);
    ewp_editors_close();
    CHECK(ewp_changed_count() > 0);
    /* The list on the page, by its own walk. */
    ewp_page_write();
    CHECK(esc_wire_panel_page_line(0) != NULL);
}

TEST_CASE(a_byte_field_refuses_a_value_outside_it)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(0));
    CHECK(esc_wire_panel_keypad()->open);
    const uint8_t was = ewp_s.draft[ewp_s.edit.f->off];

    ewp_keypad_done(100000.0f);
    CHECK(esc_wire_panel_keypad()->open);
    CHECK_EQ(ewp_s.draft[ewp_s.edit.f->off], was);
    ewp_editors_close();
    /* BACK closes an editor before anything else. */
    ew_tap(esc_wire_panel_value_rect(0));
    esc_wire_pages.back();
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
}

TEST_CASE(the_cards_answers_have_their_words)
{
    static const struct {
        uint8_t result;
        ui_text_id_t text;
    } k_a[] = {
        { ESC_CARD_DONE_NO_CARD, TX_EW_CARD_NONE }, { ESC_CARD_DONE_FULL, TX_EW_CARD_FULL },
        { ESC_CARD_DONE_DENIED, TX_EW_CARD_DENIED }, { ESC_CARD_DONE_WRITE, TX_EW_CARD_WRITE },
        { ESC_CARD_DONE_MISSING, TX_EW_CARD_MISSING }, { ESC_CARD_DONE_LARGE, TX_EW_CARD_LARGE },
    };

    session_up(up_am_rig);
    connected();
    for (size_t i = 0u; i < sizeof(k_a) / sizeof(k_a[0]); ++i) {
        memset(&g_done, 0, sizeof(g_done));
        g_done.op = ESC_CARD_LOAD;
        g_done.id = ewp_s.card_id;
        g_done.result = k_a[i].result;
        esc_wire_panel_card_done(&g_done);
        CHECK_STR_EQ(esc_wire_panel_message(), ui_tr(k_a[i].text));
        g_done.op = ESC_CARD_LIST;
        esc_wire_panel_card_done(&g_done);
        CHECK_STR_EQ(esc_wire_panel_message(), ui_tr(k_a[i].text));
        g_done.op = ESC_CARD_SAVE;
        esc_wire_panel_card_done(&g_done);
        CHECK_STR_EQ(esc_wire_panel_message(), ui_tr(k_a[i].text));
    }
    /* One no request asked: nothing moves. */
    g_done.op = 200u;
    esc_wire_panel_card_done(&g_done);
    esc_wire_panel_card_done(NULL);
    CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
}

TEST_CASE(a_list_answer_under_a_page_stays_out_and_the_list_has_pages)
{
    session_up(up_am_rig);
    connected();
    /* 13 files: one page and a NEXT. */
    for (int i = 0; i < 13; ++i) {
        snprintf(g_names[i], sizeof(g_names[i]), "ESCW%03d.TXT", i + 1);
        snprintf(g_texts[i], sizeof(g_texts[i]), "x\n");
    }
    g_card_files = 13;
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    CHECK_EQ(ewp_s.ch_what, EWP_CH_FILES);
    CHECK_STR_EQ(esc_wire_panel_choice_label(0), TR(EW_FILE_SAVE));
    CHECK_STR_EQ(esc_wire_panel_choice_label(13), TR(EW_FILES_NEXT));
    ew_tap(esc_wire_panel_choice_rect(13));
    bench_run(10u);
    CHECK_EQ(ewp_s.files_first, ESC_CARD_FILES);
    CHECK_EQ(ewp_s.ch_what, EWP_CH_FILES);
    ew_tap(ewp_choice_cancel_rect());
    /* A list asked, and a page opened before it answers. */
    g_card_wait = true;
    ew_tap(esc_wire_panel_action_rect(1));
    ewp_page_open(EWP_PAGE_LEAVE);
    g_card_wait = false;
    bench_run(10u);
    CHECK_EQ(ewp_s.ch_what, EWP_CH_NONE);
    CHECK_STR_EQ(esc_wire_panel_message(), TR(EW_CARD_LATE));
}

TEST_CASE(save_by_hand_saves_once_and_a_card_refusal_is_said)
{
    session_up(up_am_rig);
    connected();
    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    ew_tap(esc_wire_panel_choice_rect(0));
    bench_run(10u);
    CHECK_EQ(g_card_files, 1);
    CHECK_EQ(esc_prog_backup_state(esc_wire_panel_model()), ESC_PROG_BACKUP_SETTLED);
    CHECK(strstr(esc_wire_panel_message(), "ESCW") != NULL);
    /* No read copy: the reason, not a save. */
    esc_wire_panel_model()->f.image.gen = 0u;
    ewp_file_chosen(-1);
    CHECK(ewp_s.card_id != 0u);
}

TEST_CASE(a_file_that_is_refused_says_why_and_leaves_the_draft)
{
    session_up(up_am_rig);
    connected();
    snprintf(g_names[0], sizeof(g_names[0]), "ESCW001.TXT");
    snprintf(g_texts[0], sizeof(g_texts[0]), "not a settings file\n");
    g_card_files = 1;
    const uint8_t was = ewp_s.draft[0x11];

    ew_tap(esc_wire_panel_action_rect(1));
    bench_run(10u);
    ew_tap(esc_wire_panel_choice_rect(1));
    bench_run(10u);
    CHECK_EQ(ewp_s.draft[0x11], was);
    CHECK_EQ(esc_prog_message(esc_wire_panel_model(), NULL), ESC_PROG_MSG_FILE_REFUSED);
    CHECK(esc_wire_panel_message()[0] != '\0');
}

TEST_CASE(the_page_set_tells_its_host_what_it_holds)
{
    session_up(up_am_rig);
    CHECK_EQ(esc_wire_pages.pin(), (uint8_t)PROG_REG_NO_PIN);
    esc_wire_pages.armed(true);
    CHECK(ewp_s.armed);
    esc_wire_pages.armed(false);
    esc_wire_pages.entry();
    const uint32_t rev = ewp_s.rev;

    connected();
    CHECK(ewp_s.rev != rev);
    CHECK_EQ(esc_wire_pages.revision(), ewp_s.rev + ewp_s.kp.revision);
    CHECK_EQ(esc_wire_pages.pin(), 5u);
    CHECK(esc_wire_pages.holds_port());
    /* Left with a pin held, it stays open; a press and an editor go. */
    ew_tap(esc_wire_panel_tab_rect(1));
    ew_tap(esc_wire_panel_value_rect(0));
    esc_wire_pages.leave();
    CHECK(!esc_wire_panel_keypad()->open);
    CHECK(esc_wire_pages.is_open());
}

TEST_CASE(a_finger_that_leaves_the_hold_abandons_it)
{
    session_up(up_am_rig);
    const gfx_rect_t r = esc_wire_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + 5), .y = (int16_t)(r.y + 5),
                                   .strength = 40 } };

    esc_wire_pages.event(&e);
    CHECK(esc_wire_pages.has_press(1u));
    bench_run(500u);
    const uint32_t hold = esc_wire_pages.hold_revision();

    e.type = TOUCH_EVENT_MOVE;
    e.point.y = (int16_t)(r.y - 80);
    esc_wire_pages.event(&e);
    CHECK(!esc_wire_pages.has_press(1u));
    bench_run(3000u);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_CONNECT);
    CHECK_EQ(esc_wire_pages.hold_revision(), hold);
}

TEST_CASE(the_output_pages_name_what_can_be_programmed)
{
    static uint16_t slots[(size_t)LINK_OUT_SLOTS * LINK_OS_STRIDE];
    static uint16_t cfg[LINK_CC_COUNT];

    session_up(up_am_rig);
    for (size_t i = 0u; i < LINK_OUT_SLOTS; ++i) {
        slots[i * LINK_OS_STRIDE + LINK_OS_DRIVER] = LINK_DRIVER_NONE;
    }
    esc_wire_panel_read_binding(slots, cfg, true);
    CHECK_EQ(ewp_s.target_count, 0u);
    CHECK(ewp_s.targets_known);
    /* A slot with a throttle channel: a target on its pin.  A surface's is not. */
    slots[2u * LINK_OS_STRIDE + LINK_OS_DRIVER] = LINK_DRIVER_DSHOT;
    slots[2u * LINK_OS_STRIDE + LINK_OS_PIN] = 9u;
    slots[2u * LINK_OS_STRIDE + LINK_OS_RANGE] = (uint16_t)LINK_OS_RANGE_OF(1u, 1u);
    cfg[1u * LINK_CC_STRIDE + LINK_CC_ROLE] = LINK_CC_ROLE_THROTTLE;
    slots[3u * LINK_OS_STRIDE + LINK_OS_DRIVER] = LINK_DRIVER_PWM;
    slots[3u * LINK_OS_STRIDE + LINK_OS_PIN] = 10u;
    slots[3u * LINK_OS_STRIDE + LINK_OS_RANGE] = (uint16_t)LINK_OS_RANGE_OF(2u, 1u);
    cfg[2u * LINK_CC_STRIDE + LINK_CC_ROLE] = LINK_CC_ROLE_SURFACE;
    esc_wire_panel_read_binding(slots, cfg, true);
    CHECK_EQ(ewp_s.target_count, 1u);
    CHECK_EQ(ewp_s.targets[0].target, 2u);
    CHECK_EQ(ewp_s.targets[0].gpio, 9u);
    esc_wire_panel_read_binding(NULL, NULL, false);
    CHECK(!ewp_s.targets_known);
    /* No binding read: no pin named as if it were. */
    char s[40];

    CHECK(esc_wire_panel_target_text(s, sizeof(s)));
    CHECK_STR_EQ(s, TR(EW_NOT_READ_YET));
}

int main(void)
{
    RUN(the_connect_page_draws_its_red_hold_and_dims_what_by_hand_has_not);
    RUN(a_stage_under_way_draws_abort_in_red);
    RUN(the_identity_tab_says_writable_or_read_only_for_every_firmware);
    RUN(an_erased_page_draws_restore_in_red_and_a_change_marks_its_row);
    RUN(a_layout_with_no_names_draws_its_bytes);
    RUN(every_hold_page_draws_its_title_its_lines_and_a_red_hold);
    RUN(the_write_page_draws_check_in_the_warning_colour_and_its_turn_buttons);
    RUN(the_choice_list_and_the_keypad_draw_a_rows_help_line);
    RUN(the_pages_of_a_held_pin_draw_their_ways_out);
    RUN(every_message_refusal_and_result_has_its_words_in_both_languages);
    RUN(the_connect_rows_open_their_editors_and_take_their_values);
    RUN(escape32s_rows_edit_by_list_by_off_and_by_a_signed_number);
    RUN(a_byte_field_refuses_a_value_outside_it);
    RUN(the_cards_answers_have_their_words);
    RUN(a_list_answer_under_a_page_stays_out_and_the_list_has_pages);
    RUN(save_by_hand_saves_once_and_a_card_refusal_is_said);
    RUN(a_file_that_is_refused_says_why_and_leaves_the_draft);
    RUN(the_page_set_tells_its_host_what_it_holds);
    RUN(a_finger_that_leaves_the_hold_abandons_it);
    RUN(the_output_pages_name_what_can_be_programmed);
    free(g_fb);
    return test_summary("esc_wire_screens");
}
