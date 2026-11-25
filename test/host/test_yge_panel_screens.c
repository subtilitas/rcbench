/*
 * Programming for YGE ESCs as drawn and as said, in both languages: every
 * stage's page on a canvas with the colour that carries its meaning, every
 * tab, editor and page, and every message, refusal, session result, card
 * answer, state and line in words.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>

#include "yge_panel_rig.h"

#include "openyge_file.h"
#include "openyge_session.h"
#include "ui_theme.h"
#include "yge_prog_int.h"

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
    yge_pages.render(&g_cv);
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
            if (y >= 0 && x >= 0 && g_fb[(size_t)y * W + (size_t)x] == want) {
                return true;
            }
        }
    }
    return false;
}

/** @brief The whole session's area. */
static gfx_rect_t all(void)
{
    return k_yg_area;
}

/**
 * @brief What the model was last fed, changed in place: a state with flags.
 * @param state A link_yge_state_t.
 * @param flags LINK_YP_F_*.
 * @param ours  This panel's hold.
 */
static void poke(uint8_t state, uint16_t flags, bool ours)
{
    yge_link_readout_t *rd = &ygp_s.prog.f.rd;

    rd->known = true;
    rd->state = state;
    rd->flags = flags;
    rd->held = state != (uint8_t)LINK_YGE_FREE;
    rd->ours = ours;
    rd->port = 0u;
    rd->gpio = 13u;
    ++ygp_s.rev;
}

TEST_CASE(the_connect_page_draws_its_red_hold_and_dims_what_by_hand_has_not)
{
    session_up();
    draw();
    CHECK(has(yge_panel_hold_rect(), UI_C_DANGER));
    CHECK(has(all(), UI_C_WARN));
    yg_tap(yge_panel_value_rect(YGP_C_POWER));
    draw();
    CHECK(has(yge_panel_choice_rect(0), UI_C_TEXT));
    choose("BY HAND");
    draw();
    CHECK(has(yge_panel_value_rect(YGP_C_VOLT), UI_C_TEXT_FAINT));
    yg_tap(yge_panel_value_rect(YGP_C_BOOT));
    draw();
    CHECK(yge_panel_keypad()->open);
    yg_tap(ui_keypad_key_rect(yge_panel_keypad(), UI_KEY_CANCEL));
    /* A finger on the hold fills it. */
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + 4),
                                   .y = (int16_t)(r.y + 4), .strength = 40 } };

    yge_pages.event(&e);
    yge_pages.tick(1.0f);
    draw();
    e.type = TOUCH_EVENT_UP;
    e.point.x = 0;
    e.point.y = 0;
    yge_pages.event(&e);
    /* An unknown pin names its port in the warning colour. */
    port_view_t v;

    port_view_unknown(&v);
    port_view_set(&v);
    draw();
    char s[24];

    CHECK(yge_panel_target_text(s, sizeof(s)));
}

TEST_CASE(a_stage_under_way_draws_abort_in_red_and_its_words)
{
    static const uint8_t k_ses[] = { OY_ST_WAIT, OY_ST_LISTEN, OY_ST_TAKE,
                                     OY_ST_READ, OY_ST_RECHECK, OY_ST_WRITE,
                                     OY_ST_VERIFY, OY_ST_READY };
    char s[YGP_LINE];

    session_up();
    ygp_s.boot_ms = 20000u;
    yg_hold_ms(2100u);
    bench_run(300u);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_ENTERING);
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (size_t i = 0u; i < sizeof(k_ses); ++i) {
            ygp_s.prog.f.rd.state = LINK_YGE_RUN;
            ygp_s.prog.f.rd.ses_state = k_ses[i];
            ygp_s.prog.f.rd.writing = 3u;
            ygp_session_text(s, sizeof(s));
            draw();
            CHECK(has(yge_panel_action_rect(0), UI_C_DANGER));
        }
        for (uint8_t line = 0u; line < 3u; ++line) {
            ygp_s.prog.f.rd.line = line;
            ygp_s.prog.f.rd.gpio = line == 2u ? 0xFFu : 13u;
            ygp_line_text(s, sizeof(s));
            draw();
        }
        for (uint8_t st = 0u; st <= (uint8_t)LINK_YGE_STATE_COUNT; ++st) {
            ygp_s.prog.f.rd.state = st;
            CHECK(ygp_state_text() != NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ygp_s.prog.f.rd.state = LINK_YGE_RUN;
    ygp_s.prog.f.rd.ses_state = OY_ST_WAIT;
    ygp_s.boot_ms = 5000u;
    ygp_session_text(s, sizeof(s));
    CHECK_STR_EQ(s, "Boot wait 5.0s: the ESC starts up.");
    ygp_s.prog.f.rd.ses_state = OY_ST_WRITE;
    ygp_s.prog.f.rd.writing = 9u;
    ygp_session_text(s, sizeof(s));
    CHECK_STR_EQ(s, "Writing index 9.");
}

TEST_CASE(every_tab_editor_and_page_of_a_session_is_drawn)
{
    session_up();
    connected();
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (int t = 0; t < YGP_TABS; ++t) {
            tab(t);
            draw();
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* IDENTITY: WRITABLE in the ok colour. */
    tab(0);
    draw();
    CHECK(has(all(), UI_C_OK));
    /* A CHECK row's name in the warning colour. */
    tab(1);
    draw();
    CHECK(has(ygp_row_rect(0), UI_C_WARN));
    /* WRITE greyed, then live in red. */
    CHECK(!has(yge_panel_hold_rect(), UI_C_DANGER));
    yg_tap(yge_panel_value_rect(1));
    draw();
    CHECK(yge_panel_keypad()->open);
    kp_type("8");
    draw();
    CHECK(has(yge_panel_hold_rect(), UI_C_DANGER));
    yg_tap(yge_panel_value_rect(0));
    draw();
    CHECK_EQ(ygp_s.ch_what, YGP_CH_FIELD);
    yg_tap(yge_panel_cancel_rect());
    /* The LEAVE and READ pages. */
    yg_tap(yge_panel_action_rect(3));
    draw();
    CHECK(has(yge_panel_hold_rect(), UI_C_DANGER));
    yg_tap(yge_panel_cancel_rect());
    yg_tap(yge_panel_action_rect(0));
    draw();
    CHECK_EQ(yge_panel_page(), YGP_PAGE_READ);
    yg_tap(yge_panel_cancel_rect());
    /* The notes of GOVERNOR & RPM and RAW. */
    tab(4);
    draw();
    tab(6);
    draw();
    /* The card's wait and a write under way. */
    {
        const gfx_rect_t r = yge_panel_hold_rect();
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 1, .x = (int16_t)(r.x + 4),
                                       .y = (int16_t)(r.y + 4),
                                       .strength = 40 } };

        g_card_wait = true;
        yge_pages.event(&e);
        for (int i = 0; i < 9; ++i) {
            yge_pages.tick(0.25f);
        }
        e.type = TOUCH_EVENT_UP;
        yge_pages.event(&e);
        CHECK(yge_prog_card_waits(yge_panel_model()));
        draw();
        CHECK(has(all(), UI_C_WARN));
        g_card_wait = false;
        bench_run(5u);
        ygp_s.prog.f.rd.flags |= LINK_YP_F_WRITING;
        draw();
    }
    for (int i = 0; i < 8000 && yge_prog_busy(yge_panel_model()); ++i) {
        bench_run(5u);
    }
}

TEST_CASE(the_pages_of_a_held_pin_draw_their_ways_out)
{
    session_up();
    connected();
    /* The page's stop: CLEAR STOP and PIN BACK. */
    poke(LINK_YGE_OPEN, LINK_YP_F_STOPPED, true);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_STOPPED);
    draw();
    CHECK(has(yge_panel_action_rect(1), UI_C_TEXT));
    /* The panel's own stop: PIN BACK only, and the note. */
    ygp_s.prog.f.stopped = true;
    draw();
    ygp_s.prog.f.stopped = false;
    /* The pin held after the stop, and with nothing stopped. */
    poke(LINK_YGE_OPEN, 0u, true);
    ygp_s.held_stop = true;
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_PIN_HELD);
    draw();
    ygp_s.held_stop = false;
    draw();
    CHECK(has(yge_panel_hold_rect(), UI_C_DANGER));
    /* Another writer's pin, and its page. */
    poke(LINK_YGE_OPEN, 0u, false);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_OTHER);
    draw();
    yg_tap(yge_panel_action_rect(0));
    poke(LINK_YGE_OPEN, 0u, false);
    draw();
    CHECK_EQ(yge_panel_page(), YGP_PAGE_RELEASE);
    yg_tap(yge_panel_cancel_rect());
    /* A pin not back. */
    poke(LINK_YGE_NOT_BACK, LINK_YP_F_FAULT, true);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_NOT_BACK);
    draw();
    /* Waiting for power. */
    poke(LINK_YGE_HAND_WAIT, LINK_YP_F_BY_HAND, true);
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_HAND);
    draw();
    CHECK(has(yge_panel_action_rect(0), UI_C_DANGER));
}

/** @brief Every message the model says, with every detail it carries. */
static void all_messages(void)
{
    yge_prog_t *p = yge_panel_model();

    for (int m = 0; m <= (int)YGE_PROG_MSG_LOADED; ++m) {
        for (uint16_t d = 0u; d < 40u; ++d) {
            (void)yge_prog_say(p, (yge_prog_msg_t)m, d);
            ygp_s.says_hidden = false;
            const char *s = ygp_message_text();

            CHECK(s != NULL);
            if (m != (int)YGE_PROG_MSG_NONE) {
                CHECK(s[0] != '\0');
            }
            (void)ygp_message_warns();
        }
    }
    for (uint8_t r = 0u; r < 12u; ++r) {
        CHECK(ygp_card_text(r) != NULL);
        CHECK(ygp_ro_text(r) != NULL);
    }
}

TEST_CASE(every_message_refusal_and_result_has_its_words_in_both_languages)
{
    session_up();
    all_messages();
    ui_text_set_language(UI_LANG_DE);
    all_messages();
    ui_text_set_language(UI_LANG_EN);
    yge_prog_t *p = yge_panel_model();

    (void)yge_prog_say(p, YGE_PROG_MSG_REFUSED, LINK_YGE_REF_HELD);
    ygp_s.says_hidden = false;
    CHECK_STR_EQ(ygp_message_text(), TR(YG_REF_HELD));
    (void)yge_prog_say(p, YGE_PROG_MSG_SESSION, OY_SILENT);
    CHECK_STR_EQ(ygp_message_text(), TR(YG_SES_SILENT));
    (void)yge_prog_say(p, YGE_PROG_MSG_BACKUP_RAM, YGE_PROG_CARD_NO_CARD);
    CHECK(strstr(ygp_message_text(), "RAM") != NULL);
    (void)yge_prog_say(p, YGE_PROG_MSG_LOAD_REFUSED, OY_LOAD_TYPE);
    CHECK_STR_EQ(ygp_message_text(),
                 "File not loaded: the file is for another ESC type.");
    CHECK_STR_EQ(ygp_ro_text(OY_REF_RO_FIRMWARE), TR(YG_RO_FIRMWARE));
    CHECK_STR_EQ(ygp_card_text(YGE_CARD_DONE_FULL), TR(YG_CARD_FULL));
    /* A message drawn in the session's footer band. */
    connected();
    (void)yge_prog_say(p, YGE_PROG_MSG_REFUSED, LINK_YGE_REF_ARMED);
    ygp_s.says_hidden = false;
    ++ygp_s.rev;
    draw();
    CHECK(has(all(), UI_C_WARN));
}

TEST_CASE(the_cards_answers_have_their_words)
{
    static const uint8_t k_r[] = {
        YGE_CARD_DONE_NO_CARD, YGE_CARD_DONE_FULL, YGE_CARD_DONE_DENIED,
        YGE_CARD_DONE_WRITE, YGE_CARD_DONE_MISSING, YGE_CARD_DONE_LARGE,
        YGE_CARD_DONE_READ,
    };

    session_up();
    connected();
    g_card_wait = true;
    for (size_t i = 0u; i < sizeof(k_r); ++i) {
        /* A list asked, answered with a refusal. */
        yg_tap(yge_panel_action_rect(1));
        CHECK(yge_panel_card_take(&g_req));
        memset(&g_done, 0, sizeof(g_done));
        g_done.op = YGE_CARD_LIST;
        g_done.id = g_req.id;
        g_done.result = k_r[i];
        yge_panel_card_done(&g_done);
        CHECK_STR_EQ(yge_panel_message(), ygp_card_text(k_r[i]));
        CHECK_EQ(ygp_s.ch_what, YGP_CH_NONE);
    }
    /* An answer to no request moves nothing. */
    memset(&g_done, 0, sizeof(g_done));
    g_done.op = 200u;
    yge_panel_card_done(&g_done);
    yge_panel_card_done(NULL);
    CHECK_EQ(yge_panel_page(), YGP_PAGE_NONE);
    CHECK(!yge_panel_card_take(NULL));
}

int main(void)
{
    RUN(the_connect_page_draws_its_red_hold_and_dims_what_by_hand_has_not);
    RUN(a_stage_under_way_draws_abort_in_red_and_its_words);
    RUN(every_tab_editor_and_page_of_a_session_is_drawn);
    RUN(the_pages_of_a_held_pin_draw_their_ways_out);
    RUN(every_message_refusal_and_result_has_its_words_in_both_languages);
    RUN(the_cards_answers_have_their_words);
    free(g_fb);
    return test_summary("yge_panel_screens");
}
