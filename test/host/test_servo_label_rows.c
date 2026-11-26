/**
 * @file test_servo_label_rows.c
 * @brief The SERVO LABEL rows (servo_label_rows.c): the block on every
 *        SERVO list that carries DUT NAME; SERVO ID's choice list of NONE,
 *        the card's ids and TYPE ID, and its picks; TYPE ID's keyboard,
 *        whose id runs as a scan does; NEW ID's value, request and locks;
 *        ID PREFIX's keyboard.  The bench is servo_bench_rig.h; the card's
 *        side is a fake of servo_label_io_t.
 *
 * Cases:
 * - the_block_is_on_every_list_with_dut_name
 * - servo_id_lists_none_the_ids_and_type_id
 * - a_pick_sets_the_servo_and_none_clears_it
 * - a_pick_during_a_run_is_refused
 * - new_id_shows_the_next_id_and_asks_the_card
 * - new_id_is_locked_with_its_reason
 * - the_prefix_keyboard_takes_17_characters_and_asks_a_save
 * - a_typed_known_id_sets_dut_name
 * - a_typed_unknown_id_is_taken_and_its_run_makes_its_folder
 * - another_id_typed_during_a_run_is_refused
 * - a_malformed_id_is_refused_on_the_keyboard_with_its_reason
 * - a_23_character_id_is_taken_whole
 * - lower_case_comes_out_upper
 * - a_refused_id_stays_in_the_field_as_typed
 * - the_late_new_id_answer_repaints_both_rows
 * - a_scan_with_the_list_open_repaints_servo_id
 * - a_pick_takes_the_id_drawn_after_a_refresh
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "scan_flow.h"
#include "servo_active.h"
#include "servo_label_rows.h"
#include "servo_screen.h"
#include "servo_take.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_textkey.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------------- the card's side */

/** @brief What the fake card holds and was asked. */
static struct {
    bool        card;          /**< a card and a walk read to its end */
    servo_ids_t ids;           /**< its ids                           */
    int         asked;         /**< NEW IDs asked                     */
    bool        busy;          /**< a NEW ID asked, not answered yet  */
    char        prefix[24];    /**< the prefix of the last            */
} g_card;

/** @brief The fake's ids(). */
static bool f_ids(servo_ids_t *out)
{
    *out = g_card.ids;
    return g_card.card;
}

/** @brief The fake's new_id(). */
static bool f_new(const char *prefix)
{
    ++g_card.asked;
    g_card.busy = true;
    snprintf(g_card.prefix, sizeof(g_card.prefix), "%s", prefix);
    return true;
}

/** @brief The fake's new_busy(). */
static bool f_busy(void)
{
    return g_card.busy;
}

/** @brief The fake as the rows' card side. */
static const servo_label_io_t k_io = { .ids = f_ids, .new_id = f_new,
                                       .new_busy = f_busy };

/**
 * @brief The card holds the ids @p ids, @p n of them, highest first.
 */
static void card_with(const char *const *ids, int n)
{
    memset(&g_card, 0, sizeof(g_card));
    g_card.card = true;
    servo_ids_begin(&g_card.ids);
    for (int i = n - 1; i >= 0; --i) {
        servo_ids_visit(&g_card.ids, ids[i], true);
    }
    servo_label_set_io(&k_io);
}

/* ----------------------------------------------------------- the rig */

/** @brief What the look-up answers, and its name. */
static scan_card_t s_look;
static char        s_look_name[SETTINGS_TEXT_MAX];

/** @brief The card's look-up of a typed id. */
static scan_card_t look(const char *id, char *name, size_t n)
{
    (void)id;
    snprintf(name, n, "%s", s_look_name);
    return s_look;
}

/** @brief The alert band's text, "" for none. */
static const char *alert(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/** @brief The active id, "" for none. */
static const char *active(void)
{
    const char *id = servo_active_id();
    return id != NULL ? id : "";
}

/** @brief A fresh SAFE bench on the hub, no servo active, the hub's list
 *         open, a card with no ids. */
static void fresh_hub(void)
{
    bench_fresh();
    short_runs();
    go(SERVO_FN_HUB);
    servo_screen_set_armed(false);
    bench_frames(100u);
    scan_flow_reset();
    (void)servo_active_clear(false);
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST");
    settings_set_text(SET_TEXT_DUT_NAME, "");
    card_with(NULL, 0);
    ui_router_set_alert(NULL);
    settings_request_save();
    tap_settings();
}

/** @brief The custom row @p i's value as the open list shows it. */
static const char *value_of(int i)
{
    static char buf[64];
    buf[0] = '\0';
    servo_screen_list()->host->row(i).custom->value_text(buf, sizeof(buf));
    return buf;
}

/** @brief The open list's keyboard. */
static const ui_textkey_t *keyboard(void)
{
    return &servo_screen_list()->tk;
}

/** @brief @p text typed on the open list's keyboard, key by key. */
static void type(const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) {
        int k_at = -1;
        if (*p == ' ') {
            k_at = UI_TK_SPACE;
        } else {
            for (int k = 0; k < UI_TEXTKEY_CHARS; ++k) {
                const char *l = ui_textkey_label(k);
                if (l != NULL && l[0] == *p && l[1] == '\0') {
                    k_at = k;
                    break;
                }
            }
        }
        if (k_at < 0) {
            T_FAIL("no key for '%c'", *p);
            return;
        }
        sl_text_key(k_at);
    }
}

/** @brief TYPE ID chosen on the hub's SERVO ID row: the last choice. */
static void type_id_open(void)
{
    sl_choose(HL_SERVO_ID, servo_label_choices() - 1);
}

/* ------------------------------------------------------------ the cases */

TEST_CASE(the_block_is_on_every_list_with_dut_name)
{
    static const struct {
        servo_fn_t fn;
        int        head;
    } lists[] = {
        { SERVO_FN_HUB, HL_LABEL },     { SERVO_FN_LIVE, HL_LABEL },
        { SERVO_FN_TEST, TL_LABEL },    { SERVO_FN_LIMRUN, LL_LABEL },
        { SERVO_FN_RAUTO, RAL_LABEL },
    };
    fresh_hub();
    (void)home_tag();
    for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        settings_request_save();
        go(lists[i].fn);
        tap_settings();
        const ui_setlist_t *sl = servo_screen_list();
        CHECK(sl->open);
        const int h = lists[i].head;
        CHECK_EQ(sl->host->row(h).kind, (uint8_t)UI_SL_HEADING);
        CHECK_EQ(sl->host->row(h).heading, TX_SVL_LABEL);
        CHECK_EQ(sl->host->row(h + 1).kind, (uint8_t)UI_SL_TEXT);
        CHECK_EQ(sl->host->row(h + 1).text, (int8_t)SET_TEXT_SERVO_PREFIX);
        CHECK(sl->host->row(h + 2).custom == &svl_id_row);
        CHECK(sl->host->row(h + 3).custom == &svl_new_row);
        CHECK_STR_EQ(ui_setlist_text_label(SET_TEXT_SERVO_PREFIX),
                     "ID prefix");
        CHECK(home_tag());
    }
}

TEST_CASE(servo_id_lists_none_the_ids_and_type_id)
{
    static const char *const ids[] = {
        "KST-00007", "KST-00006", "B-00005", "KST-00004", "A-00003",
        "KST-00002", "KST-00001",
    };
    fresh_hub();
    /* No id on the card: NONE and TYPE ID. */
    CHECK_EQ(servo_label_choices(), 2);
    CHECK_STR_EQ(svl_id_row.choice(0), "NONE");
    CHECK_STR_EQ(svl_id_row.choice(1), "TYPE ID");
    CHECK_EQ(svl_id_row.chosen(), 0);
    CHECK_STR_EQ(value_of(HL_SERVO_ID), "NONE");
    /* One. */
    card_with(ids, 1);
    CHECK_EQ(servo_label_choices(), 3);
    CHECK_STR_EQ(svl_id_row.choice(1), "KST-00007");
    /* Seven: the 6 highest, then TYPE ID, 8 rows in the pane. */
    card_with(ids, 7);
    CHECK_EQ(servo_label_choices(), 8);
    CHECK_STR_EQ(svl_id_row.choice(1), "KST-00007");
    CHECK_STR_EQ(svl_id_row.choice(6), "KST-00002");
    CHECK_STR_EQ(svl_id_row.choice(7), "TYPE ID");
    sl_tap(HL_SERVO_ID);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK_EQ(sl->choice_row, HL_SERVO_ID);
    const gfx_rect_t pane = ui_setlist_pane(sl);
    for (int k = 0; k < 8; ++k) {
        const gfx_rect_t r = ui_setlist_choice_rect(sl, k);
        CHECK(r.h > 40);
        CHECK(r.y >= pane.y && r.y + r.h <= pane.y + pane.h);
    }
    CHECK_EQ(ui_setlist_choice_rect(sl, 8).h, 0);
    CHECK(ui_setlist_back((ui_setlist_t *)sl));
}

TEST_CASE(a_pick_sets_the_servo_and_none_clears_it)
{
    static const char *const ids[] = { "KST-00002", "KST-00001" };
    fresh_hub();
    card_with(ids, 2);
    sl_choose(HL_SERVO_ID, 2);
    CHECK_STR_EQ(active(), "KST-00001");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_PICK);
    CHECK_EQ(svl_id_row.chosen(), 2);
    CHECK_STR_EQ(value_of(HL_SERVO_ID), "KST-00001");
    sl_choose(HL_SERVO_ID, 0);
    CHECK_STR_EQ(active(), "");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_NONE);
    /* An active id the list does not hold: none of the rows. */
    CHECK(servo_active_set("Z-00009", SERVO_ACTIVE_SCAN, false));
    CHECK_EQ(svl_id_row.chosen(), -1);
}

TEST_CASE(a_pick_during_a_run_is_refused)
{
    static const char *const ids[] = { "KST-00002", "KST-00001" };
    fresh_hub();
    card_with(ids, 2);
    CHECK(servo_active_set("KST-00002", SERVO_ACTIVE_PICK, false));
    servo_screen_set_armed(true);
    CHECK(servo_screen_run_active());
    CHECK_STR_EQ(svl_id_row.locked(), ui_tr(TX_SVL_RUN_ACTIVE));
    sl_tap(HL_SERVO_ID);
    CHECK_EQ(servo_screen_list()->choice_row, -1);
    /* The pick hook asked anyway: refused, the servo kept. */
    svl_id_row.pick(2);
    CHECK_STR_EQ(active(), "KST-00002");
    svl_id_row.pick(0);
    CHECK_STR_EQ(active(), "KST-00002");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_PICK);
    servo_screen_set_armed(false);
}

TEST_CASE(new_id_shows_the_next_id_and_asks_the_card)
{
    static const char *const ids[] = { "B-0000Z" };
    fresh_hub();
    CHECK_STR_EQ(value_of(HL_NEW_ID), "NEXT KST-00000");
    card_with(ids, 1);
    CHECK_STR_EQ(value_of(HL_NEW_ID), "NEXT KST-00010");
    CHECK(svl_new_row.locked() == NULL);
    sl_tap(HL_NEW_ID);
    CHECK_EQ(g_card.asked, 1);
    CHECK_STR_EQ(g_card.prefix, "KST");
}

TEST_CASE(new_id_is_locked_with_its_reason)
{
    static const char *const top[] = { "KST-ZZZZZ" };
    fresh_hub();
    /* No card. */
    g_card.card = false;
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_NO_CARD));
    CHECK_STR_EQ(value_of(HL_NEW_ID), "");
    sl_tap(HL_NEW_ID);
    CHECK_EQ(g_card.asked, 0);
    /* No io at all reads as no card. */
    servo_label_set_io(NULL);
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_NO_CARD));
    /* ZZZZZ on the card. */
    card_with(top, 1);
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_NO_FREE_ID));
    /* A prefix that starts with ".", and one with a character the id
     * does not take. */
    card_with(NULL, 0);
    settings_set_text(SET_TEXT_SERVO_PREFIX, ".KST");
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_PREFIX_DOT));
    settings_set_text(SET_TEXT_SERVO_PREFIX, "K#T");
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_PREFIX_BAD));
    CHECK_STR_EQ(value_of(HL_NEW_ID), "");
    /* A run. */
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST");
    servo_screen_set_armed(true);
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_RUN_ACTIVE));
    svl_new_row.tap();
    CHECK_EQ(g_card.asked, 0);
    servo_screen_set_armed(false);
    CHECK(svl_new_row.locked() == NULL);
}

TEST_CASE(the_prefix_keyboard_takes_17_characters_and_asks_a_save)
{
    fresh_hub();
    settings_set_text(SET_TEXT_SERVO_PREFIX, "");
    sl_tap(HL_PREFIX);
    CHECK(keyboard()->open);
    CHECK(!keyboard()->folder);
    CHECK_EQ(keyboard()->max_len, 17);
    type(" KST X10 HV WING LEFT");
    CHECK_EQ(keyboard()->len, 17);
    sl_text_key(UI_TK_OK);
    CHECK(!keyboard()->open);
    /* The 17 characters " KST X10 HV WING ": the spaces at the ends
     * dropped, the ones inside kept. */
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "KST X10 HV WING");
    settings_set_text(SET_TEXT_SERVO_PREFIX, "");
    sl_tap(HL_PREFIX);
    type("KST X10 ");
    sl_text_key(UI_TK_OK);
    CHECK_STR_EQ(settings_text(SET_TEXT_SERVO_PREFIX), "KST X10");
    CHECK(settings_save_asked());
}

TEST_CASE(a_typed_known_id_sets_dut_name)
{
    fresh_hub();
    s_look = SCAN_CARD_KNOWN;
    snprintf(s_look_name, sizeof(s_look_name), "%s", "LEFT AILERON");
    scan_flow_set_look(look);
    type_id_open();
    CHECK(keyboard()->open);
    CHECK(keyboard()->servo_id);
    CHECK_STR_EQ(keyboard()->text, "KST-");
    type("0000A");
    sl_text_key(UI_TK_OK);
    CHECK(!keyboard()->open);
    CHECK_STR_EQ(active(), "KST-0000A");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_TYPED);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SET);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "LEFT AILERON");
    char want[96];
    snprintf(want, sizeof(want), ui_tr(TX_SCAN_SET_NAME), "KST-0000A",
             "LEFT AILERON");
    CHECK_STR_EQ(alert(), want);
    /* The band's chip says how: typed. */
    ui_bench_status_t st = *ui_router_status();
    scan_flow_status(&st);
    CHECK_EQ(st.servo_from, (uint8_t)SERVO_ACTIVE_TYPED);
    /* The same id typed again at once: SAME, not a scanner's repeat. */
    type_id_open();
    type("0000A");
    sl_text_key(UI_TK_OK);
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_SAME);
    scan_flow_set_look(NULL);
}

/* The fake card of servo_take(): every folder made is counted. */
static char s_made[4][40];
static int  s_n_made;
static bool t_mounted(void *ctx) { (void)ctx; return true; }
static bool t_make_dir(void *ctx, const char *dir)
{
    (void)ctx;
    if (s_n_made < 4) {
        snprintf(s_made[s_n_made++], sizeof(s_made[0]), "%s", dir);
    }
    return true;
}
static bool t_walk(void *ctx, const char *dir, log_take_visit_fn visit,
                   void *visit_ctx)
{
    (void)ctx;
    (void)dir;
    (void)visit;
    (void)visit_ctx;
    return true;
}
static bool t_exists(void *ctx, const char *dir, const char *name)
{
    (void)ctx;
    (void)dir;
    (void)name;
    return false;
}
static bool t_create(void *ctx, const char *dir, const char *name, int n)
{
    (void)ctx;
    (void)dir;
    (void)name;
    (void)n;
    return true;
}

TEST_CASE(a_typed_unknown_id_is_taken_and_its_run_makes_its_folder)
{
    fresh_hub();
    s_look = SCAN_CARD_NEW;
    s_look_name[0] = '\0';
    scan_flow_set_look(look);
    type_id_open();
    type("0000B");
    sl_text_key(UI_TK_OK);
    CHECK_STR_EQ(active(), "KST-0000B");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_TYPED);
    char want[96];
    snprintf(want, sizeof(want), ui_tr(TX_SCAN_NEW), "KST-0000B");
    CHECK_STR_EQ(alert(), want);
    /* The first run of it makes its folder. */
    servo_series_t series;
    servo_series_init(&series);
    const log_take_io_t io = { NULL, t_mounted, t_make_dir, t_walk,
                               t_exists, t_create };
    s_n_made = 0;
    int number = -1;
    CHECK_EQ(servo_take(&series, &io, servo_active_id(), SERVO_KIND_LIMIT,
                        false, &number),
             LOG_TAKE_OK);
    CHECK_EQ(number, 0);
    CHECK(s_n_made >= 2);
    CHECK_STR_EQ(s_made[s_n_made - 1], "Servos/KST-0000B");
    scan_flow_set_look(NULL);
}

TEST_CASE(another_id_typed_during_a_run_is_refused)
{
    fresh_hub();
    CHECK(servo_active_set("KST-00001", SERVO_ACTIVE_PICK, false));
    type_id_open();
    CHECK(keyboard()->open);
    /* A run starts while the keyboard is open: ARMED from SERVO. */
    servo_screen_set_armed(true);
    ui_bench_status_t st = *ui_router_status();
    st.armed = true;
    st.armed_roles = UI_ARMED_SERVO;
    ui_router_set_status(&st);
    type("00002");
    sl_text_key(UI_TK_OK);
    CHECK_STR_EQ(active(), "KST-00001");
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_REFUSED_RUN);
    char want[128];
    snprintf(want, sizeof(want), ui_tr(TX_SCAN_RUN), "KST-00001");
    CHECK_STR_EQ(alert(), want);
    st.armed = false;
    st.armed_roles = 0u;
    ui_router_set_status(&st);
    servo_screen_set_armed(false);
}

TEST_CASE(a_malformed_id_is_refused_on_the_keyboard_with_its_reason)
{
    static const struct {
        const char *text;      /**< typed after a CLR */
        servo_id_err_t why;    /**< the rule broken    */
    } bad[] = {
        { "KST-0000", SERVO_ID_SERIAL },
        { "-00001", SERVO_ID_EMPTY },
        { " KST-00001", SERVO_ID_EDGE },
        { ".KST-00001", SERVO_ID_EDGE },
    };
    fresh_hub();
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        type_id_open();
        sl_text_key(UI_TK_CLR);
        type(bad[i].text);
        sl_text_key(UI_TK_OK);
        CHECK(keyboard()->open);
        CHECK(keyboard()->refused);
        CHECK_EQ(keyboard()->why, (uint8_t)bad[i].why);
        CHECK_STR_EQ(active(), "");
        sl_text_key(UI_TK_CANCEL);
        CHECK(!keyboard()->open);
    }
    /* A character no key types, from a prefix written on a computer:
     * refused, nothing trimmed or dropped. */
    settings_set_text(SET_TEXT_SERVO_PREFIX, "K#T");
    type_id_open();
    type("00001");
    sl_text_key(UI_TK_OK);
    CHECK(keyboard()->refused);
    CHECK_EQ(keyboard()->why, (uint8_t)SERVO_ID_CHAR);
    CHECK_STR_EQ(keyboard()->text, "K#T-00001");
    sl_text_key(UI_TK_CANCEL);
    CHECK_STR_EQ(active(), "");
}

TEST_CASE(a_23_character_id_is_taken_whole)
{
    fresh_hub();
    settings_set_text(SET_TEXT_SERVO_PREFIX, "KST X10 HV WING L");
    type_id_open();
    CHECK_STR_EQ(keyboard()->text, "KST X10 HV WING L-");
    type("ZZZZZ");
    CHECK_EQ(keyboard()->len, 23);
    type("Z");
    CHECK_EQ(keyboard()->len, 23);
    sl_text_key(UI_TK_OK);
    CHECK_STR_EQ(active(), "KST X10 HV WING L-ZZZZZ");
    CHECK_EQ(strlen(active()), 23u);
}

TEST_CASE(lower_case_comes_out_upper)
{
    fresh_hub();
    /* A prefix written in lower case on a computer. */
    settings_set_text(SET_TEXT_SERVO_PREFIX, "kst_x10");
    type_id_open();
    CHECK_STR_EQ(keyboard()->text, "kst_x10-");
    type("0000A");
    sl_text_key(UI_TK_OK);
    CHECK(!keyboard()->open);
    CHECK_STR_EQ(active(), "KST_X10-0000A");
    CHECK_EQ(servo_active_from(), SERVO_ACTIVE_TYPED);
}

TEST_CASE(a_refused_id_stays_in_the_field_as_typed)
{
    fresh_hub();
    settings_set_text(SET_TEXT_SERVO_PREFIX, "kst");
    type_id_open();
    type("0000");
    sl_text_key(UI_TK_OK);
    CHECK(keyboard()->refused);
    CHECK_STR_EQ(keyboard()->text, "kst-0000");
    sl_text_key(UI_TK_CANCEL);
}

/* NEW ID asked: the row is locked until the card answers; the answer,
 * frames later, makes the servo active and both rows repaint. */
TEST_CASE(the_late_new_id_answer_repaints_both_rows)
{
    fresh_hub();
    bench_frames(40u);
    sl_tap(HL_NEW_ID);
    CHECK_EQ(g_card.asked, 1);
    bench_frames(40u);
    CHECK_STR_EQ(svl_new_row.locked(), ui_tr(TX_SVL_NEW_BUSY));
    svl_new_row.tap();
    CHECK_EQ(g_card.asked, 1);
    const uint32_t before = servo_label_repaints();
    /* The answer: KST-00000 made, its folder on the card, it active. */
    g_card.busy = false;
    servo_ids_visit(&g_card.ids, "KST-00000", true);
    CHECK(servo_active_set("KST-00000", SERVO_ACTIVE_NEW, false));
    bench_frames(40u);
    CHECK(servo_label_repaints() > before);
    CHECK_STR_EQ(value_of(HL_SERVO_ID), "KST-00000");
    CHECK_STR_EQ(value_of(HL_NEW_ID), "NEXT KST-00001");
    CHECK(svl_new_row.locked() == NULL);
}

TEST_CASE(a_scan_with_the_list_open_repaints_servo_id)
{
    fresh_hub();
    bench_frames(40u);
    const uint32_t before = servo_label_repaints();
    ui_bench_status_t st = *ui_router_status();
    st.armed = false;
    st.armed_roles = 0u;
    ui_router_set_status(&st);
    CHECK(scan_flow_typed("KST-0000S"));
    bench_frames(40u);
    CHECK(servo_label_repaints() > before);
    CHECK_STR_EQ(value_of(HL_SERVO_ID), "KST-0000S");
}

/* The card's walk refreshes while SERVO ID's list is open: a new id on top
 * moves every id down one.  A tap takes the id drawn under the finger; an
 * id drawn that the card holds no more is refused. */
TEST_CASE(a_pick_takes_the_id_drawn_after_a_refresh)
{
    static const char *const ids[] = { "KST-00006", "KST-00005",
                                       "KST-00004", "KST-00003",
                                       "KST-00002", "KST-00001" };
    fresh_hub();
    card_with(ids, 6);
    sl_tap(HL_SERVO_ID);
    CHECK_EQ(servo_screen_list()->choice_row, HL_SERVO_ID);
    CHECK_STR_EQ(svl_id_row.choice(2), "KST-00005");
    servo_ids_visit(&g_card.ids, "KST-00007", true);
    bench_frames(40u);
    CHECK_STR_EQ(svl_id_row.choice(2), "KST-00005");
    gfx_rect_t r = ui_setlist_choice_rect(servo_screen_list(), 2);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_STR_EQ(active(), "KST-00005");
    /* KST-00001 is drawn last; two more ids push it off the 6 newest. */
    sl_tap(HL_SERVO_ID);
    CHECK_STR_EQ(svl_id_row.choice(6), "KST-00002");
    servo_ids_visit(&g_card.ids, "KST-00008", true);
    r = ui_setlist_choice_rect(servo_screen_list(), 6);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK_STR_EQ(active(), "KST-00005");
}

int main(void)
{
    RUN(the_block_is_on_every_list_with_dut_name);
    RUN(servo_id_lists_none_the_ids_and_type_id);
    RUN(a_pick_sets_the_servo_and_none_clears_it);
    RUN(a_pick_during_a_run_is_refused);
    RUN(new_id_shows_the_next_id_and_asks_the_card);
    RUN(new_id_is_locked_with_its_reason);
    RUN(the_prefix_keyboard_takes_17_characters_and_asks_a_save);
    RUN(a_typed_known_id_sets_dut_name);
    RUN(a_typed_unknown_id_is_taken_and_its_run_makes_its_folder);
    RUN(another_id_typed_during_a_run_is_refused);
    RUN(a_malformed_id_is_refused_on_the_keyboard_with_its_reason);
    RUN(a_23_character_id_is_taken_whole);
    RUN(lower_case_comes_out_upper);
    RUN(a_refused_id_stays_in_the_field_as_typed);
    RUN(the_late_new_id_answer_repaints_both_rows);
    RUN(a_scan_with_the_list_open_repaints_servo_id);
    RUN(a_pick_takes_the_id_drawn_after_a_refresh);
    return test_summary("servo_label_rows");
}
