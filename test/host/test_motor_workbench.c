/*
 * MOTOR & ESC as a workbench: the strip LIVE | PROGRAM and what each task
 * keeps, the footer's SETTINGS and the list it opens (its rows, scroll,
 * keypad, SAVE, RESET, the leave warning and the home tag), the pulse rows'
 * lock and the SAFE rule of the list, the CONTROL card's TARGET row and its
 * lock, the way to OUTPUTS and back; the TARGET text with DShot programming
 * ports beside the slots and alone, in 18 and 26 cells.
 *
 * Driven through the router, in panel coordinates, as the panel drives it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "outputs_screen.h"
#include "touch_feed.h"

#include "gfx.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "port_view.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_footer.h"
#include "ui_keypad.h"
#include "ui_setlist.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"
#include "motor_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
#define P(y) (UI_BAND_H + (y))

/* From motor_spots.h: the strip's tabs, 150px a tab 4px apart from x 9,
 * 40px high from y 8; the CONTROL card's TARGET row; the footer's ARM and
 * SETTINGS; the list's keypad over the body down to the strip's foot
 * (ui_setlist_int.h). */
#define TAB_Y       P(MO_TAB_Y)
#define TAB_X(i)    MO_TAB_X(i)
#define TARGET_X    MO_TARGET_X
#define TARGET_Y    P(MO_TARGET_Y)
#define ARM_X       MO_ARM_X
#define ARM_Y       P(MO_ARM_Y)
#define TRACK_X     MO_TRACK_X
#define TRACK_Y     P(MO_TRACK_Y)
#define KP_AREA     ((gfx_rect_t){ 10, 10, 1004, 482 })
#define SETTINGS_X  MO_SETTINGS_X
#define SETTINGS_Y  P(MO_SETTINGS_Y)
/* The list's strip: RESET in the middle slot, SAVE in the right one, where
 * SETTINGS is on the screen. */
#define RESET_X     512
#define SAVE_X      850
#define STRIP_Y     P(468)
/* LIVE's views, PLOT and TABLE, in the telemetry card. */
#define TABLE_X     MO_TABLE_TAB_X
#define TABLE_Y     P(MO_PANE_Y)

static gfx_color_t *fb;
static gfx_canvas_t cv;

/* ------------------------------------------------------------- the store */

static float s_saved[SETTING_COUNT];
static bool  s_refuse;
static int   s_saves;

static bool mem_load(float *values, int count)
{
    (void)values;
    (void)count;
    return false;
}

static bool mem_save(const float *values, int count)
{
    if (s_refuse) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_saved[i] = values[i];
    }
    ++s_saves;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

static int s_seen_poles;
static int s_seen_any;

static void observer(setting_id_t id)
{
    ++s_seen_any;
    if (id == SET_MOTOR_POLES) {
        ++s_seen_poles;
    }
}

/* -------------------------------------------------------------- the rig */

static motor_cmd_t took(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f, 0u };
    (void)motor_screen_poll_cmd(&c);
    return c;
}

static void drain(void)
{
    while (took().kind != MOTOR_CMD_NONE) { }
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ui_router_tick(0.026f);
        ui_router_render(&cv, i & 1);
    }
}

/* The servo programmer's port as the IOmcu's page gives it. */
static void kst_port(link_kst_state_t st)
{
    kst_prog_feed_t f;
    memset(&f, 0, sizeof(f));
    f.rd.phase = KST_LINK_READY;
    f.rd.known = true;
    f.rd.port = st;
    kst_panel_feed(&f);
}

/* MOTOR & ESC opened from its tile on a SAFE bench, a store that takes a
 * save, nothing on the glass. */
static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    s_refuse = false;
    s_saves = 0;
    settings_set_store(&k_store);
    settings_set_observer(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    kst_panel_init((gfx_rect_t){ 0, UI_BAND_H, W, H - UI_BAND_H });
    kst_port(LINK_KST_PWM);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_screen_set_armed(false);
    motor_screen_set_throttle_pins(3u, true);
    motor_screen_set_throttle_protocol(4u);     /* DSHOT300 */
    drain();
    frames(2);
}

static void home_tag(void)
{
    const gfx_rect_t r = ui_home_tag_rect(ui_router_title(ui_router_current()));
    feed_tap(FEED_LONE, r.x + r.w / 2, r.y + r.h / 2);
}

static void open_settings(void) { feed_tap(FEED_LONE, SETTINGS_X, SETTINGS_Y); }
static void open_live(void)     { feed_tap(FEED_LONE, TAB_X(0), TAB_Y); }
static void open_program(void)  { feed_tap(FEED_LONE, TAB_X(1), TAB_Y); }

/* The list dragged by @p dy px: up (negative) scrolls towards its end. */
static void drag_list(int dy)
{
    finger(FEED_LONE, 300, P(240));
    glide(FEED_LONE, 300, P(240 + dy), 6);
    lift(FEED_LONE);
}

/* The list scrolled to its end and back to its top. */
static void to_end(void) { drag_list(-220); drag_list(-220); }
static void to_top(void) { drag_list(200); drag_list(200); }

/* Row @p row's value, tapped where the list's scroll has it. */
static void tap_box(int row)
{
    const gfx_rect_t b = motor_settings_box(row);
    feed_tap(FEED_LONE, b.x + b.w / 2, P(b.y + b.h / 2));
}

/* The row of @p id among the list's settings. */
static int row_of(setting_id_t id)
{
    setting_id_t ids[16];
    const int n = motor_settings_rows(ids, 16);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            return i;
        }
    }
    return -1;
}

/* Whether row @p row's value shows whole in the list's pane. */
static bool shown_whole(int row)
{
    const gfx_rect_t b = motor_settings_box(row);
    const gfx_rect_t pane = ui_setlist_pane(motor_settings_list());
    return b.y >= pane.y && b.y + b.h <= pane.y + pane.h;
}

static void key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, KP_AREA, "", "", 0.0f, 0.0f, 1.0f, 0);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    feed_tap(FEED_LONE, r.x + r.w / 2, P(r.y + r.h / 2));
}

/* Digits typed, then OK. */
static void keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

/* ARM held until it posts; the bench's answer. */
static bool arm(void)
{
    finger(FEED_LONE, ARM_X, ARM_Y);
    for (int i = 0; i < (int)(2.3f / 0.026f); ++i) {
        ui_router_tick(0.026f);
    }
    const motor_cmd_t c = took();
    lift(FEED_LONE);
    if (c.kind != MOTOR_CMD_ARM) {
        return false;
    }
    motor_screen_set_armed(true);
    drain();
    return true;
}

/* ------------------------------------------------------------ the strip */

/*
 * LIVE, then PROGRAM, the plan's order with TEST not drawn, and nothing
 * where a third tab would be: SETTINGS is the footer's.  A tile opens
 * LIVE; the tag leaves from either task and the bench goes to SAFE.
 */
TEST_CASE(the_strip_reads_live_program_and_a_tile_opens_live)
{
    fresh();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    open_program();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    feed_tap(FEED_LONE, TAB_X(2), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    CHECK(!motor_screen_settings_shown());
    open_live();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);

    /* PROGRAM, then away and back by the tile: LIVE. */
    open_program();
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);

    /* The labels: LIVE and PROGRAM; LIVE and PROGRAMMIEREN; the footer's
     * SETTINGS and OPTIONEN. */
    CHECK_STR_EQ(TR(WB_LIVE), "LIVE");
    CHECK_STR_EQ(TR(KP_PROGRAM), "PROGRAM");
    CHECK_STR_EQ(TR(FOOT_SETTINGS), "SETTINGS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(WB_LIVE), "LIVE");
    CHECK_STR_EQ(TR(KP_PROGRAM), "PROGRAMMIEREN");
    CHECK_STR_EQ(TR(FOOT_SETTINGS), "OPTIONEN");
    ui_text_set_language(UI_LANG_EN);
    /* The title keeps its 11 characters. */
    CHECK_STR_EQ(ui_router_title(SCREEN_MOTOR), "MOTOR & ESC");
}

/* LIVE keeps its view across PROGRAM; the home tag leaves from LIVE and
 * from PROGRAM alike, and leaving posts the DISARM it always has. */
TEST_CASE(each_task_keeps_its_view_and_the_tag_leaves_from_either)
{
    fresh();
    feed_tap(FEED_LONE, TABLE_X, TABLE_Y);
    CHECK_EQ(motor_screen_pane(), (int)MOTOR_PANE_TABLE);
    open_program();
    open_live();
    CHECK_EQ(motor_screen_pane(), (int)MOTOR_PANE_TABLE);

    for (int task = 0; task < 2; ++task) {
        fresh();
        if (task == 1) {
            open_program();
        }
        CHECK(arm());
        home_tag();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    }
    /* Switching tasks under an armed bench switches nothing off. */
    fresh();
    CHECK(arm());
    open_program();
    open_live();
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/* A drag on the throttle ends with the task it was on: the second
 * finger's tab tap ends the drag, which commands no more. */
TEST_CASE(a_tab_switch_ends_a_drag_and_the_throttle_is_out_of_reach)
{
    fresh();
    CHECK(arm());
    finger(1, TRACK_X + 100, TRACK_Y);
    glide(1, TRACK_X + 160, TRACK_Y, 8);
    CHECK_EQ(took().kind, MOTOR_CMD_THROTTLE);
    feed_tap(2, TAB_X(1), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    const float at = motor_screen_throttle();
    glide(1, TRACK_X + 260, TRACK_Y, 8);
    lift(1);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), at);
    /* The knob moves the throttle on LIVE only. */
    motor_screen_knob_frame();
    motor_screen_knob(0.1f);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), at);
    open_live();
    motor_screen_knob_frame();
    motor_screen_knob(0.1f);
    CHECK_EQ(took().kind, MOTOR_CMD_THROTTLE);
}

/* A strip press, a SETTINGS press and a value press lost to a gap in the
 * touch stream: the release on a reused id switches, opens nothing. */
TEST_CASE(a_cancel_drops_the_strips_press_and_the_lists)
{
    fresh();
    finger(FEED_LONE, TAB_X(1), TAB_Y);
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    finger(FEED_LONE, SETTINGS_X, SETTINGS_Y);
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK(!motor_screen_settings_shown());
    open_settings();
    CHECK(motor_screen_settings_shown());
    const gfx_rect_t b = motor_settings_box(0);
    finger(FEED_LONE, b.x + 10, P(b.y + 10));
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    tap_box(0);
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_CELLS);
}

/* ------------------------------------------------------------ the footer */

/*
 * The footer's three slots of 328 x 48px at body y 444, 4px above the
 * alert band: MOTOR & ESC with the throttle's target under it, ARM in the
 * middle, SETTINGS at the right.  The CONTROL card ends 8px over it.
 */
TEST_CASE(the_footer_holds_the_name_arm_and_settings)
{
    fresh();
    const gfx_rect_t hold = ui_footer_rect(UI_FOOTER_HOLD);
    const gfx_rect_t set = ui_footer_rect(UI_FOOTER_SETTINGS);
    CHECK_EQ(hold.x, 348);
    CHECK_EQ(hold.y, 444);
    CHECK_EQ(hold.w, 328);
    CHECK_EQ(hold.h, 48);
    CHECK_EQ(set.x, 686);
    CHECK(gfx_rect_contains(hold, ARM_X, MO_ARM_Y));
    CHECK(gfx_rect_contains(set, SETTINGS_X, MO_SETTINGS_Y));
    CHECK(hold.y + hold.h + 4 <= UI_ALERT_Y - UI_BAND_H);
    /* The line under the name: what the throttle drives. */
    char line[40];
    bool warn = true;
    (void)motor_throttle_target(line, sizeof(line), 4u, 3u, true, 31u);
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 GPIO 1");
    motor_screen_set_throttle_pins(0u, true);
    warn = motor_throttle_target(line, sizeof(line), 4u, 0u, true, 31u);
    CHECK(warn);
    /* SETTINGS opens the list over the body, and covers the screen. */
    open_settings();
    CHECK(motor_screen_settings_shown());
    CHECK(motor_screen()->covered());
    home_tag();
    CHECK(!motor_screen_settings_shown());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/*
 * The list covers DISARM and the throttle, so SETTINGS opens on a SAFE
 * bench with ARM not held: ARMED, a DISARM not yet answered and a held
 * ARM each refuse it with the band's reason.  A bench that leaves SAFE
 * under the list closes it, with what its keypad held typed, and DISARM
 * shows again.
 */
TEST_CASE(settings_open_on_a_safe_bench_only)
{
    fresh();
    CHECK(arm());
    open_settings();
    CHECK(!motor_screen_settings_shown());
    CHECK_STR_EQ(ui_router_alert(), TR(FOOT_SETTINGS_SAFE));
    /* A DISARM asked and not answered. */
    feed_tap(FEED_LONE, ARM_X, ARM_Y);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    open_settings();
    CHECK(!motor_screen_settings_shown());
    motor_screen_set_armed(false);
    frames(1);
    open_settings();
    CHECK(motor_screen_settings_shown());
    home_tag();

    /* ARM held by one finger: a tap on SETTINGS by another opens nothing. */
    fresh();
    finger(1, ARM_X, ARM_Y);
    ui_router_tick(0.5f);
    feed_tap(2, SETTINGS_X, SETTINGS_Y);
    CHECK(!motor_screen_settings_shown());
    lift(1);
    drain();

    /* The bench arms under the keypad of Full pulse: the list closes, and
     * the value stays. */
    fresh();
    open_settings();
    to_end();
    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MAX_US);
    key(UI_KEY_1);
    motor_screen_set_armed(true);
    frames(1);
    CHECK(!motor_screen_settings_shown());
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    CHECK_EQ(settings_get_int(SET_OUT_MAX_US), 2000);
    motor_screen_set_armed(false);
}

/* ------------------------------------------------------------ the list */

/* ESC / BENCH's 8 rows in schema order, under PACK, MOTOR and THROTTLE
 * OUTPUT, the headings EN and DE: 11 rows with the headings. */
TEST_CASE(the_settings_rows_are_esc_bench_under_three_headings)
{
    setting_id_t ids[16];
    setting_id_t esc[16];
    const int n = motor_settings_rows(ids, 16);
    CHECK_EQ(n, 8);
    CHECK_EQ(settings_in_category(SET_CAT_ESC, esc, 16), n);
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(ids[i], esc[i]);
    }
    CHECK_EQ(motor_settings_rows(NULL, 0), 8);
    CHECK_EQ(motor_settings_rows(ids, 3), 8);  /* counts past max */
    static const int head[8] = {
        TX_MO_SET_PACK, TX_MO_SET_PACK, TX_MO_SET_PACK,
        TX_MO_SET_MOTOR, TX_MO_SET_MOTOR,
        TX_MO_SET_THROTTLE, TX_MO_SET_THROTTLE, TX_MO_SET_THROTTLE,
    };
    static const int at[8] = { 1, 2, 3, 5, 6, 8, 9, 10 };
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(motor_settings_heading(i), head[i]);
        CHECK_EQ(motor_settings_list_row(i), at[i]);
    }
    CHECK_EQ(ui_setlist_count(motor_settings_list()), 11);
    CHECK_EQ(motor_settings_heading(-1), -1);
    CHECK_EQ(motor_settings_heading(8), -1);
    const gfx_rect_t none = motor_settings_box(8);
    CHECK_EQ(none.w, 0);
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(TR(MO_SET_PACK), "PACK");
    CHECK_STR_EQ(TR(MO_SET_MOTOR), "MOTOR");
    CHECK_STR_EQ(TR(MO_SET_THROTTLE), "THROTTLE OUTPUT");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(MO_SET_PACK), "AKKU");
    CHECK_STR_EQ(TR(MO_SET_MOTOR), "MOTOR");
    CHECK_STR_EQ(TR(MO_SET_THROTTLE), "GASAUSGANG");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * SETUP's geometry: rows 64px tall at a 70px pitch, a heading 34px at 40,
 * the value between keys of 56 x 48px, the pane from x 10 to 994 and body
 * y 8 to 436.  The first five settings show whole at the top; the pulse
 * rows come into the pane when the list is scrolled to its end.  Each name
 * fits the 668px left of the "-" key in the head face, each help line in
 * the label face, in both languages.
 */
TEST_CASE(the_list_is_setups_and_scrolls_to_the_pulse_rows)
{
    fresh();
    open_settings();
    const ui_setlist_t *sl = motor_settings_list();
    const gfx_rect_t pane = ui_setlist_pane(sl);
    CHECK_EQ(pane.x, 10);
    CHECK_EQ(pane.w, 984);
    CHECK_EQ(pane.y, 8);
    CHECK_EQ(pane.y + pane.h, 436);
    CHECK_EQ(ui_setlist_row_rect(sl, 0).h, 34);
    CHECK_EQ(ui_setlist_row_rect(sl, 1).y, 8 + 40);
    CHECK_EQ(ui_setlist_row_rect(sl, 2).y - ui_setlist_row_rect(sl, 1).y, 70);
    CHECK_EQ(ui_setlist_row_rect(sl, 1).h, 64);
    const gfx_rect_t minus = ui_setlist_row_part(sl, 1, UI_SL_PART_MINUS);
    CHECK_EQ(minus.x, 704);
    CHECK_EQ(minus.w, 56);
    for (int i = 0; i < 5; ++i) {
        CHECK(shown_whole(i));
    }
    CHECK(!shown_whole(row_of(SET_OUT_MAX_US)));
    to_end();
    CHECK(shown_whole(row_of(SET_OUT_MIN_US)));
    CHECK(shown_whole(row_of(SET_OUT_MAX_US)));
    CHECK(shown_whole(row_of(SET_OUT_RAMP)));
    CHECK(sl->scroll == sl->scroll_max && sl->scroll_max > 0);
    to_top();
    CHECK_EQ(sl->scroll, 0);
    /* Closed and opened again: at its top. */
    to_end();
    home_tag();
    open_settings();
    CHECK_EQ(sl->scroll, 0);

    setting_id_t ids[16];
    const int n = motor_settings_rows(ids, 16);
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(gfx_text_width(&gfx_font_10x20, TR(MO_SET_SAFE_ONLY), 1) <= 668);
        for (int i = 0; i < n; ++i) {
            CHECK(gfx_text_width(&gfx_font_20x36, ui_setting_label(ids[i]), 1)
                  <= 668);
            CHECK(gfx_text_width(&gfx_font_10x20, ui_setting_help(ids[i]), 1)
                  <= 668);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A typed value lands on its step inside its range and is in force at
 * once; SAVE asks for the write and the bench's next idle frame takes it.
 * Out of the range the keypad refuses it and stays; CANCEL changes
 * nothing.  Motor poles, the IOmcu's copy, notifies once.
 */
TEST_CASE(a_typed_value_lands_on_its_step_and_save_asks_the_write)
{
    fresh();
    open_settings();
    settings_set_observer(observer);
    s_seen_poles = 0;

    tap_box(row_of(SET_PACK_MAH));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_MAH);
    keys("4321");
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 4300);
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());
    feed_tap(FEED_LONE, SAVE_X, STRIP_Y);
    CHECK(settings_save_asked());
    CHECK_EQ(s_saves, 0);
    CHECK(!settings_save_tick(false));        /* not on a busy bench */
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_saves, 1);
    CHECK_EQ(s_saved[SET_PACK_MAH], 4300.0f);
    CHECK(!settings_dirty());

    /* The range's ends are taken; past them the keypad stays open. */
    tap_box(row_of(SET_PACK_MAH));
    keys("30000");
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 30000);
    tap_box(row_of(SET_PACK_MAH));
    keys("30001");
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_MAH);
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 30000);
    key(UI_KEY_CANCEL);
    CHECK_EQ(motor_screen_keypad_setting(), -1);

    /* An odd pole count (21) lands on an even one, and the IOmcu is told once;
     * the same value again tells it nothing. */
    tap_box(row_of(SET_MOTOR_POLES));
    keys("21");
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES) % 2, 0);
    CHECK_EQ(s_seen_poles, 1);
    const int poles = settings_get_int(SET_MOTOR_POLES);
    char again[8];
    snprintf(again, sizeof(again), "%d", poles);
    tap_box(row_of(SET_MOTOR_POLES));
    keys(again);
    CHECK_EQ(s_seen_poles, 1);

    /* CANCEL: nothing changes. */
    tap_box(row_of(SET_PACK_CELLS));
    key(UI_KEY_4);
    key(UI_KEY_CANCEL);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    settings_set_observer(NULL);
}

/*
 * One store: the write SAVE asks for on the list takes what SETUP left
 * unsaved with it, and SETUP then holds nothing.  A refused write leaves
 * the set unsaved; the list's SAVE reads NOT SAVED and SETUP's warning
 * names ESC / BENCH's row.
 */
TEST_CASE(the_lists_save_takes_setups_pending_changes)
{
    fresh();
    ui_router_goto(SCREEN_SETUP);
    settings_set(SET_DIM_AFTER, 30.0f);
    ui_router_goto(SCREEN_MOTOR);
    CHECK(settings_dirty());

    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    feed_tap(FEED_LONE, SAVE_X, STRIP_Y);
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_saved[SET_PACK_CELLS], 4.0f);
    CHECK_EQ(s_saved[SET_DIM_AFTER], 30.0f);
    CHECK(!settings_dirty());
    home_tag();
    CHECK(!motor_screen_settings_shown());

    ui_router_goto(SCREEN_SETUP);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    fresh();
    s_refuse = true;
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    feed_tap(FEED_LONE, SAVE_X, STRIP_Y);
    CHECK(!settings_save_tick(true));
    CHECK(settings_save_failed());
    CHECK(settings_dirty());
    ui_router_goto(SCREEN_SETUP);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    char line[96];
    CHECK(settings_screen_warn_line(0, line, sizeof(line)));
    CHECK(strstr(line, "Cells") != NULL);
    s_refuse = false;
}

/* RESET MOTOR & ESC sets the list's 8 rows to their defaults and leaves
 * every other setting as it is. */
TEST_CASE(reset_sets_the_lists_rows_only)
{
    fresh();
    settings_set(SET_DIM_AFTER, 30.0f);
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    tap_box(row_of(SET_MOTOR_POLES));
    keys("20");
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
    feed_tap(FEED_LONE, RESET_X, STRIP_Y);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), 14);
    CHECK_EQ(settings_get_int(SET_DIM_AFTER), 30);
}

/*
 * Idle pulse and Full pulse are locked while the bench is not SAFE; the
 * list itself opens only on a SAFE bench, so inside it they take a value
 * and the pack's rows do as well.
 */
TEST_CASE(the_pulse_rows_take_a_value_in_the_list)
{
    fresh();
    open_settings();
    to_end();
    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MAX_US);
    keys("1900");
    CHECK_EQ(settings_get_int(SET_OUT_MAX_US), 1900);
    tap_box(row_of(SET_OUT_MIN_US));
    keys("1100");
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), 1100);
}

/* ------------------------------------------------------- the TARGET row */

/* Locked while the bench is not SAFE or ARM is held, and while the servo
 * programmer holds its port; a tap while locked opens nothing. */
TEST_CASE(the_target_row_locks_for_each_condition)
{
    fresh();
    CHECK(!motor_screen_target_locked());

    /* ARM held, not yet asked. */
    finger(FEED_LONE, ARM_X, ARM_Y);
    ui_router_tick(0.5f);
    CHECK(motor_screen_target_locked());
    feed_tap(1, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    /* Asked, not yet answered. */
    for (int i = 0; i < 80; ++i) {
        ui_router_tick(0.026f);
    }
    CHECK_EQ(took().kind, MOTOR_CMD_ARM);
    lift(FEED_LONE);
    CHECK(motor_screen_target_locked());
    /* ARMED. */
    motor_screen_set_armed(true);
    CHECK(motor_screen_target_locked());
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    /* DISARM asked, the bench still armed. */
    feed_tap(FEED_LONE, ARM_X, ARM_Y);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    CHECK(motor_screen_target_locked());
    /* SAFE again. */
    motor_screen_set_armed(false);
    CHECK(!motor_screen_target_locked());

    /* The programmer holds its port. */
    kst_port(LINK_KST_PROGRAMMING);
    CHECK(motor_screen_target_locked());
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    kst_port(LINK_KST_PWM);
    CHECK(!motor_screen_target_locked());

    /* A press that slides off the row opens nothing. */
    finger(FEED_LONE, TARGET_X, TARGET_Y);
    glide(FEED_LONE, TARGET_X, TARGET_Y + 60, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/*
 * A tap on the row opens OUTPUTS; its home tag comes back to MOTOR & ESC
 * on the task the row was tapped on.  OUTPUTS from SETUP's tile goes back
 * to SETUP, and a screen between the two makes the next entry a tile's,
 * which opens LIVE.
 */
TEST_CASE(the_target_row_opens_outputs_and_the_tag_comes_back)
{
    for (int task = 0; task < 2; ++task) {
        fresh();
        if (task == 1) {
            open_program();
        }
        feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
        CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
        CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_MOTOR);
        /* Leaving switched the bench to SAFE as every way out does. */
        CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
        home_tag();
        CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
        CHECK_EQ(motor_screen_task(), task == 0 ? (int)UI_WB_LIVE
                                                : (int)UI_WB_PROGRAM);
    }

    /* OUTPUTS from SETUP's tile: back to SETUP. */
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_OUTPUTS);
    CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SETUP);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* Opened from PROGRAM, then a route elsewhere: the next entry opens
     * LIVE. */
    fresh();
    open_program();
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
}

/*
 * OUTPUTS opens on the protocol the throttle's pins are bound as; on the
 * overview (OFF) while they are bound as several or none is known.  Once:
 * the same screen from SETUP's tile opens on the overview, and so does the
 * next row tap after the protocol is no longer known.
 */
TEST_CASE(the_target_row_opens_outputs_on_the_throttle_protocol)
{
    const outbind_proto_t *k = outbind_protos();
    for (uint8_t p = 1; p < OUTBIND_PROTOS; ++p) {
        if (k[p].role != OUT_ROLE_THROTTLE) {
            continue;
        }
        fresh();
        motor_screen_set_throttle_protocol(p);
        feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
        CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
        CHECK_EQ((int)outputs_screen_binding()->proto, (int)p);
        CHECK_EQ(outputs_screen_port_shown(), -1);
        home_tag();
        CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
        /* From SETUP's tile: the overview. */
        ui_router_goto(SCREEN_SETUP);
        ui_router_goto(SCREEN_OUTPUTS);
        CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    }
    static const uint8_t none[] = { 0u, MOTOR_PROTO_MIXED, OUTBIND_PROTOS };
    for (size_t i = 0; i < sizeof(none); ++i) {
        fresh();
        motor_screen_set_throttle_protocol(none[i]);
        feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
        CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
        CHECK_EQ((int)outputs_screen_binding()->proto, 0);
        CHECK_EQ(outputs_screen_port_shown(), -1);
    }
}

/* ------------------------------------------------------- the home tag */

/*
 * Over the list's keypad the tag closes it, typed digits with it; then it
 * closes the list; the next tag leaves.  With a change unsaved the tag
 * opens the leave warning: STAY keeps the list, DISCARD puts the value
 * back and closes it, SAVE asks for the write and closes it.
 */
TEST_CASE(the_home_tag_closes_the_keypad_then_the_list_then_leaves)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    key(UI_KEY_4);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK(motor_screen_settings_shown());
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    home_tag();
    CHECK(!motor_screen_settings_shown());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* A change unsaved: the warning holds the list. */
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    home_tag();
    CHECK(motor_screen_settings_shown());
    CHECK(motor_settings_list()->warn);
    char line[96];
    CHECK(ui_setlist_warn_line(motor_settings_list(), 0, line, sizeof(line)));
    CHECK(strstr(line, "Cells") != NULL);
    feed_tap(FEED_LONE, 174, STRIP_Y);                 /* STAY */
    CHECK(!motor_settings_list()->warn);
    CHECK(motor_screen_settings_shown());
    home_tag();
    CHECK(motor_settings_list()->warn);
    feed_tap(FEED_LONE, RESET_X, STRIP_Y);             /* DISCARD */
    CHECK(!motor_screen_settings_shown());
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    CHECK(!settings_dirty());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    home_tag();
    feed_tap(FEED_LONE, SAVE_X, STRIP_Y);              /* SAVE */
    CHECK(!motor_screen_settings_shown());
    CHECK(settings_save_asked());
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
    /* A save asked holds nothing. */
    open_settings();
    home_tag();
    CHECK(!motor_screen_settings_shown());
}

/* SAVE repaints when the store's state moves with no touch: a save taken
 * outside the screen turns WHEN IDLE into SAVED. */
TEST_CASE(the_save_button_follows_the_store)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("5");
    feed_tap(FEED_LONE, SAVE_X, STRIP_Y);
    frames(2);
    gfx_color_t before[24 * 120];
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 120; ++x) {
            before[y * 120 + x] = fb[(size_t)(STRIP_Y - 12 + y) * W + SAVE_X - 60 + x];
        }
    }
    CHECK(settings_save_tick(true));
    frames(2);
    int differ = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 120; ++x) {
            differ += before[y * 120 + x]
                      != fb[(size_t)(STRIP_Y - 12 + y) * W + SAVE_X - 60 + x];
        }
    }
    CHECK(differ > 0);
}

/* DSHOT300 is entry 4 of the outputs catalogue, DSHOT600 entry 5. */
#define PROTO_DSHOT300 4u
#define PROTO_DSHOT600 5u

/* A 4.28 view: port 0 SERVO PWM on GP13, port 1 on GP14 as @p fn1, port 2
 * on GP15 as @p fn2, port 3 NONE; @p refused as FLAGS has it. */
static void view_ports(uint8_t fn1, uint8_t fn2, uint8_t refused)
{
    port_view_t v;
    port_view_unknown(&v);
    v.count     = 4u;
    v.has_func  = true;
    v.has_dshot = true;
    v.pin[0] = 13u; v.func[0] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.pin[1] = 14u; v.func[1] = fn1;
    v.pin[2] = 15u; v.func[2] = fn2;
    v.pin[3] = PORT_VIEW_NO_PIN; v.func[3] = (uint8_t)LINK_PG_FN_NONE;
    v.refused = refused;
    port_view_set(&v);
    /* As the panel hands OUTPUTS the view. */
    outputs_screen_set_prog_ports(4u, v.pin);
}

TEST_CASE(the_target_names_the_dshot_ports_the_throttle_drives)
{
    char line[64];
    const uint8_t none = (uint8_t)LINK_PG_FN_NONE;
    const uint8_t d300 = (uint8_t)LINK_PG_FN_DSHOT300;
    const uint8_t d600 = (uint8_t)LINK_PG_FN_DSHOT600;
    ui_text_set_language(UI_LANG_EN);
    /* No DShot port: as before. */
    view_ports(none, none, 0u);
    CHECK_EQ(motor_dshot_ports(), 0u);
    CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    /* A port alone: in full where it fits, PORT n and its pin where that
     * fits, PORT n where neither does. */
    view_ports(d300, none, 0u);
    CHECK_EQ(motor_dshot_ports(), 2u);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 29u));
    CHECK_STR_EQ(line, "DSHOT300 PROG PORT 2 GPIO 14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 28u));
    CHECK_STR_EQ(line, "DSHOT300 PROG PORT 2 GPIO 14");
    /* Shorter: PORT n and its pin, as in the CONTROL card's 25 cells. */
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 27u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2 GPIO 14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 25u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2 GPIO 14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 23u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2 GPIO 14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 22u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2");
    /* Slots of the same rate first, then the port. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 +1");
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 26u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 PORT 2");
    /* In the CONTROL card's 25 cells both are named. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 25u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 PORT 2");
    /* Another rate: MIXED. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT600, 1u,
                                 true, 25u));
    CHECK_STR_EQ(line, "MIXED GPIO 0 PORT 2");
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT600, 1u,
                                 true, 18u));
    CHECK_STR_EQ(line, "MIXED GPIO 0 +1");
    /* Slot pins at a rate no protocol has, beside a port: MIXED too. */
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 1u, true, 25u));
    CHECK_STR_EQ(line, "MIXED GPIO 0 PORT 2");
    view_ports(d300, d600, 0u);
    CHECK_EQ(motor_dshot_ports(), 6u);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "MIXED PORT 2 +1");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 19u));
    CHECK_STR_EQ(line, "MIXED PORT 2 PORT 3");
    ui_text_set_language(UI_LANG_DE);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "GEMISCHT PORT 2 +1");
    view_ports(d300, none, 0u);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 28u));
    CHECK_STR_EQ(line, "DSHOT300 PROG-PORT 2 GPIO 14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 25u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2 GPIO 14");
    ui_text_set_language(UI_LANG_EN);
    /* Eight slot pins and two ports in 18 cells: the count holds both. */
    view_ports(d300, d300, 0u);
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0xFFu,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 GPIO 0 +9");
    /* A refused port drives nothing and is not named. */
    view_ports(d300, d600, 0x4u);
    CHECK_EQ(motor_dshot_ports(), 2u);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2");
    view_ports(none, d600, 0x4u);
    CHECK(motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    /* The binding not read still says so. */
    view_ports(d300, none, 0u);
    CHECK(motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                false, 18u));
    /* Nothing known about the ports: none named. */
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    CHECK_EQ(motor_dshot_ports(), 0u);
}

/* A DShot port the throttle drives alone opens its PROG PORT entry; with
 * slot pins beside it, or two ports, as without. */
TEST_CASE(the_target_row_opens_the_entry_of_a_lone_dshot_port)
{
    const uint8_t none = (uint8_t)LINK_PG_FN_NONE;
    const uint8_t d300 = (uint8_t)LINK_PG_FN_DSHOT300;
    fresh();
    view_ports(d300, none, 0u);
    motor_screen_set_throttle_pins(0u, true);
    motor_screen_set_throttle_protocol(0u);
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), 1);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    /* A slot pin beside it: the slot's protocol. */
    motor_screen_set_throttle_pins(1u, true);
    motor_screen_set_throttle_protocol(PROTO_DSHOT300);
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    CHECK_EQ((int)outputs_screen_binding()->proto, (int)PROTO_DSHOT300);
    home_tag();
    /* Two ports and no slot: the overview. */
    view_ports(d300, d300, 0u);
    motor_screen_set_throttle_pins(0u, true);
    motor_screen_set_throttle_protocol(0u);
    feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    CHECK_EQ((int)outputs_screen_binding()->proto, 0);
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
}

int main(void)
{
    RUN(the_strip_reads_live_program_and_a_tile_opens_live);
    RUN(each_task_keeps_its_view_and_the_tag_leaves_from_either);
    RUN(a_tab_switch_ends_a_drag_and_the_throttle_is_out_of_reach);
    RUN(a_cancel_drops_the_strips_press_and_the_lists);
    RUN(the_footer_holds_the_name_arm_and_settings);
    RUN(settings_open_on_a_safe_bench_only);
    RUN(the_settings_rows_are_esc_bench_under_three_headings);
    RUN(the_list_is_setups_and_scrolls_to_the_pulse_rows);
    RUN(a_typed_value_lands_on_its_step_and_save_asks_the_write);
    RUN(the_lists_save_takes_setups_pending_changes);
    RUN(reset_sets_the_lists_rows_only);
    RUN(the_pulse_rows_take_a_value_in_the_list);
    RUN(the_target_row_locks_for_each_condition);
    RUN(the_target_row_opens_outputs_and_the_tag_comes_back);
    RUN(the_target_row_opens_outputs_on_the_throttle_protocol);
    RUN(the_home_tag_closes_the_keypad_then_the_list_then_leaves);
    RUN(the_save_button_follows_the_store);
    RUN(the_target_names_the_dshot_ports_the_throttle_drives);
    RUN(the_target_row_opens_the_entry_of_a_lone_dshot_port);
    free(fb);
    return test_summary("motor_workbench");
}
