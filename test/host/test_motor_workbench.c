/*
 * MOTOR & ESC as a workbench: the strip LIVE | PROGRAM | SETTINGS and what
 * each task keeps, SETTINGS' rows, their keypad and the write when idle beside
 * SETUP's SAVE, the pulse rows' lock, the CONTROL card's TARGET row and its
 * lock, the way to OUTPUTS and back, and the home tag's levels; the
 * TARGET text with DShot programming ports beside the slots and alone,
 * in 18 and 26 cells.
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
#include "ui_keypad.h"
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
 * 40px high from y 8; the CONTROL card's TARGET row and ARM; the keypad's
 * area over the page and the strip. */
#define TAB_Y       P(MO_TAB_Y)
#define TAB_X(i)    MO_TAB_X(i)
#define TARGET_X    MO_TARGET_X
#define TARGET_Y    P(MO_TARGET_Y)
#define ARM_X       MO_ARM_X
#define ARM_Y       P(MO_ARM_Y)
#define TRACK_X     MO_TRACK_X
#define TRACK_Y     P(MO_TRACK_Y)
#define KP_AREA     ((gfx_rect_t){ UI_MARGIN, 3, 620, UI_BODY_H - 3 - UI_MARGIN })
/* SETTINGS' save line, 4px above the alert band (motor_settings_page.c). */
#define SAVE_LINE_Y MO_SAVE_LINE_Y
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
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
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

static void open_settings(void) { feed_tap(FEED_LONE, TAB_X(2), TAB_Y); }
static void open_live(void)     { feed_tap(FEED_LONE, TAB_X(0), TAB_Y); }

/* Row @p row's value box, tapped. */
static void tap_box(int row)
{
    const gfx_rect_t b = motor_settings_box(row);
    feed_tap(FEED_LONE, b.x + b.w / 2, P(b.y + b.h / 2));
}

/* The row of @p id on SETTINGS. */
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
 * LIVE, PROGRAM, then SETTINGS, the plan's order with TEST not drawn, and
 * nothing where a fourth tab would be.  A tile opens LIVE; the tag leaves
 * from any task and the bench goes to SAFE.
 */
TEST_CASE(the_strip_reads_live_program_settings_and_a_tile_opens_live)
{
    fresh();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    feed_tap(FEED_LONE, TAB_X(1), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_PROGRAM);
    open_settings();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_SETTINGS);
    feed_tap(FEED_LONE, TAB_X(3), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_SETTINGS);
    open_live();
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);

    /* SETTINGS, then away and back by the tile: LIVE. */
    open_settings();
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);

    /* The labels: LIVE, PROGRAM and SETTINGS; LIVE, PROGRAMMIEREN and
     * OPTIONEN. */
    CHECK_STR_EQ(TR(WB_LIVE), "LIVE");
    CHECK_STR_EQ(TR(KP_PROGRAM), "PROGRAM");
    CHECK_STR_EQ(TR(SUP_SETTINGS), "SETTINGS");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(TR(WB_LIVE), "LIVE");
    CHECK_STR_EQ(TR(KP_PROGRAM), "PROGRAMMIEREN");
    CHECK_STR_EQ(TR(SUP_SETTINGS), "OPTIONEN");
    ui_text_set_language(UI_LANG_EN);
    /* The title keeps its 11 characters. */
    CHECK_STR_EQ(ui_router_title(SCREEN_MOTOR), "MOTOR & ESC");
}

/* LIVE keeps its view across SETTINGS; the home tag leaves from LIVE and
 * from SETTINGS alike, and leaving posts the DISARM it always has. */
TEST_CASE(each_task_keeps_its_view_and_the_tag_leaves_from_either)
{
    fresh();
    feed_tap(FEED_LONE, TABLE_X, TABLE_Y);
    CHECK_EQ(motor_screen_pane(), (int)MOTOR_PANE_TABLE);
    open_settings();
    open_live();
    CHECK_EQ(motor_screen_pane(), (int)MOTOR_PANE_TABLE);

    for (int task = 0; task < 2; ++task) {
        fresh();
        if (task == 1) {
            open_settings();
        }
        CHECK(arm());
        home_tag();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    }
    /* Switching tasks under an armed bench switches nothing off. */
    fresh();
    CHECK(arm());
    open_settings();
    open_live();
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/* A drag on the throttle and a step press end with the task they were
 * on: the second finger's tab tap ends the drag, which commands no more. */
TEST_CASE(a_tab_switch_ends_a_drag_and_the_throttle_is_out_of_reach)
{
    fresh();
    CHECK(arm());
    finger(1, TRACK_X + 100, TRACK_Y);
    glide(1, TRACK_X + 160, TRACK_Y, 8);
    CHECK_EQ(took().kind, MOTOR_CMD_THROTTLE);
    feed_tap(2, TAB_X(2), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_SETTINGS);
    const float at = motor_screen_throttle();
    glide(1, TRACK_X + 260, TRACK_Y, 8);
    lift(1);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
    CHECK_EQ(motor_screen_throttle(), at);
    /* The track's place on SETTINGS is the page's: no throttle. */
    finger(FEED_LONE, TRACK_X + 20, TRACK_Y);
    glide(FEED_LONE, TRACK_X + 200, TRACK_Y, 8);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, MOTOR_CMD_NONE);
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

/* A strip press and a value press lost to a gap in the touch stream: the
 * release on a reused id switches nothing and opens nothing. */
TEST_CASE(a_cancel_drops_the_strips_press_and_the_pages)
{
    fresh();
    finger(FEED_LONE, TAB_X(2), TAB_Y);
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_LIVE);
    open_settings();
    const gfx_rect_t b = motor_settings_box(0);
    finger(FEED_LONE, b.x + 10, P(b.y + 10));
    ui_router_cancel_gestures();
    lift(FEED_LONE);
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    tap_box(0);
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_CELLS);
}

/* ------------------------------------------------------------ SETTINGS */

/* ESC / BENCH's 8 rows in schema order, under PACK, MOTOR and THROTTLE
 * OUTPUT, the headings EN and DE. */
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
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(motor_settings_heading(i), head[i]);
    }
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
 * Every row fits its column in both languages: the label 16 cells of 10px
 * left of the 120px box, the help (or the lock's reason) 36 cells of 8px
 * under them, the value with its unit the box's 11 at the setting's minimum
 * and maximum.  The boxes are a finger tall, stand apart, inside the page,
 * above the save line, which ends above the alert band.
 */
TEST_CASE(every_row_fits_its_columns_in_both_languages)
{
    setting_id_t ids[16];
    const int n = motor_settings_rows(ids, 16);
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(strlen(TR(MO_SET_SAFE_ONLY)) <= 36u);
        for (int i = 0; i < n; ++i) {
            const setting_def_t *d = settings_def(ids[i]);
            CHECK(strlen(ui_setting_label(ids[i])) <= 16u);
            CHECK(strlen(ui_setting_help(ids[i])) <= 36u);
            const float ends[2] = { d->min, d->max };
            for (int e = 0; e < 2; ++e) {
                char v[24], line[40];
                (void)ui_setting_value_of(ids[i], ends[e], v, sizeof(v));
                snprintf(line, sizeof(line), "%s %s", v, d->unit);
                CHECK(strlen(line) <= 11u);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    for (int i = 0; i < n; ++i) {
        const gfx_rect_t a = motor_settings_box(i);
        CHECK(a.x >= UI_MARGIN + 10 + 16 * 10 && a.x + a.w <= UI_MARGIN + 620);
        CHECK(a.w >= 11 * 10 && a.h >= UI_TAP_MIN);
        /* The box and its help line under it end over the save line. */
        CHECK(a.y >= 56 && a.y + a.h + 2 + 16 <= SAVE_LINE_Y);
        for (int k = 0; k < i; ++k) {
            const gfx_rect_t b = motor_settings_box(k);
            const bool apart = a.x >= b.x + b.w + 8 || b.x >= a.x + a.w + 8
                               || a.y >= b.y + b.h + 4 || b.y >= a.y + a.h + 4;
            CHECK(apart);
        }
    }
    CHECK(SAVE_LINE_Y + 20 <= UI_ALERT_Y - UI_BAND_H - 4);
}

/*
 * A typed value lands on its step inside its range, in force at once,
 * and asks for a save; the bench's next idle frame writes it.  Out of the
 * range the keypad refuses it and stays; CANCEL changes nothing.  Motor
 * poles, the IOmcu's copy, notifies once.
 */
TEST_CASE(a_typed_value_lands_on_its_step_and_asks_a_save)
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
    tap_box(row_of(SET_PACK_MAH));
    keys("0");
    CHECK_EQ(settings_get_int(SET_PACK_MAH), 0);

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

    /* CANCEL: nothing changes and nothing is asked. */
    CHECK(settings_save_tick(true));
    const int saves = s_saves;
    tap_box(row_of(SET_PACK_CELLS));
    key(UI_KEY_4);
    key(UI_KEY_CANCEL);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    CHECK(!settings_save_asked());
    CHECK(!settings_save_tick(true));
    CHECK_EQ(s_saves, saves);
    settings_set_observer(NULL);
}

/*
 * One store: the write a change on SETTINGS asks for takes what SETUP left
 * unsaved with it.  SETUP then holds nothing.  A refused write leaves the
 * set unsaved, and SETUP's warning names ESC / BENCH's row with its own.
 * SETUP keeps SAVE and the warning for its own lists.
 */
TEST_CASE(the_idle_write_takes_setups_pending_changes)
{
    fresh();
    /* A change on SETUP, not saved, carried out by a route the warning
     * does not hold (STOP's, the application's). */
    ui_router_goto(SCREEN_SETUP);
    settings_set(SET_DIM_AFTER, 30.0f);
    ui_router_goto(SCREEN_MOTOR);
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());

    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_saved[SET_PACK_CELLS], 4.0f);
    CHECK_EQ(s_saved[SET_DIM_AFTER], 30.0f);
    CHECK(!settings_dirty());

    /* SETUP holds nothing now: its home tag leaves. */
    ui_router_goto(SCREEN_SETUP);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* A write refused: the values stay unsaved, and SETUP's warning lists
     * the change made on MOTOR & ESC. */
    fresh();
    s_refuse = true;
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("4");
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

    /* A change on SETUP's own list still asks SAVE and warns. */
    fresh();
    ui_router_goto(SCREEN_SETUP);
    settings_set(SET_DIM_AFTER, 30.0f);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK(settings_screen_warning());
    CHECK(!settings_save_asked());
}

/*
 * Idle pulse and Full pulse take a value on a SAFE bench only: ARMED,
 * arming, and a DISARM not yet answered lock them, and a keypad open for
 * one closes when the bench leaves SAFE.  The pack's rows stay open.
 */
TEST_CASE(the_pulse_rows_change_on_a_safe_bench_only)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MAX_US);
    key(UI_KEY_CANCEL);

    CHECK(arm());
    for (int r = 0; r < 2; ++r) {
        const setting_id_t id = (r == 0) ? SET_OUT_MIN_US : SET_OUT_MAX_US;
        tap_box(row_of(id));
        CHECK_EQ(motor_screen_keypad_setting(), -1);
    }
    tap_box(row_of(SET_OUT_RAMP));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_RAMP);
    key(UI_KEY_CANCEL);
    tap_box(row_of(SET_PACK_MAH));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_MAH);
    key(UI_KEY_CANCEL);

    /* A DISARM asked and not answered: still locked. */
    finger(FEED_LONE, ARM_X, ARM_Y);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
    tap_box(row_of(SET_OUT_MIN_US));
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    motor_screen_set_armed(false);
    tap_box(row_of(SET_OUT_MIN_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MIN_US);

    /* The bench arms under the keypad of Full pulse: it closes and the
     * value stays. */
    key(UI_KEY_CANCEL);
    tap_box(row_of(SET_OUT_MAX_US));
    key(UI_KEY_1);
    motor_screen_set_armed(true);
    frames(1);
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    CHECK_EQ(settings_get_int(SET_OUT_MAX_US), 2000);
    motor_screen_set_armed(false);
}

/*
 * ARM held locks the pulse rows too.  An endpoint edit waits 300ms before
 * it goes to the IOmcu and the switch to ARMED sends no CHAN_CFG, so an OK
 * under a held ARM could be overtaken by the arm.  OK on an open keypad
 * while ARM is down changes nothing, the next frame closes the keypad, and
 * a pulse box takes no tap until ARM is let go.
 */
TEST_CASE(the_pulse_rows_lock_while_arm_is_held)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_OUT_MIN_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MIN_US);
    key(UI_KEY_1);
    key(UI_KEY_2);
    key(UI_KEY_0);
    key(UI_KEY_0);
    finger(1, ARM_X, ARM_Y);                  /* ARM down, second finger */
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), 1000);
    CHECK(!settings_save_asked());

    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    tap_box(row_of(SET_PACK_CELLS));          /* the pack is not locked */
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_CELLS);
    key(UI_KEY_CANCEL);

    /* A keypad open for an endpoint when ARM goes down closes at the next
     * frame. */
    lift(1);
    drain();
    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MAX_US);
    finger(1, ARM_X, ARM_Y);
    frames(1);
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    lift(1);
    drain();
    frames(1);
    tap_box(row_of(SET_OUT_MAX_US));
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_OUT_MAX_US);
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
            open_settings();
        }
        feed_tap(FEED_LONE, TARGET_X, TARGET_Y);
        CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
        CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_MOTOR);
        /* Leaving switched the bench to SAFE as every way out does. */
        CHECK_EQ(took().kind, MOTOR_CMD_DISARM);
        home_tag();
        CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
        CHECK_EQ(motor_screen_task(), task == 0 ? (int)UI_WB_LIVE
                                                : (int)UI_WB_SETTINGS);
    }

    /* OUTPUTS from SETUP's tile: back to SETUP. */
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_OUTPUTS);
    CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SETUP);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* Opened from SETTINGS, then a route elsewhere: the next entry opens
     * LIVE. */
    fresh();
    open_settings();
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

/* Over SETTINGS' keypad the tag closes it, typed digits with it, and the
 * page stays; the next tag leaves. */
TEST_CASE(the_home_tag_closes_the_keypad_then_leaves)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    key(UI_KEY_4);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(motor_screen_keypad_setting(), -1);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    CHECK(!settings_save_asked());
    /* Where OK was is the page now: nothing is typed. */
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 6);
    home_tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* The strip under the keypad takes no tap. */
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    feed_tap(FEED_LONE, TAB_X(0), TAB_Y);
    CHECK_EQ(motor_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_CELLS);
    /* ARM on the rail does, under the keypad. */
    CHECK(arm());
    CHECK_EQ(motor_screen_keypad_setting(), (int)SET_PACK_CELLS);
}

/* The page repaints when the store's state moves with no touch: a save
 * taken outside the screen turns SAVE PENDING into SAVED. */
TEST_CASE(the_save_line_follows_the_store)
{
    fresh();
    open_settings();
    tap_box(row_of(SET_PACK_CELLS));
    keys("5");
    frames(2);
    gfx_color_t before[24 * 120];
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 120; ++x) {
            before[y * 120 + x] = fb[(size_t)P(SAVE_LINE_Y + y) * W + UI_MARGIN + 10 + x];
        }
    }
    CHECK(settings_save_tick(true));
    frames(2);
    int differ = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 120; ++x) {
            differ += before[y * 120 + x] != fb[(size_t)P(SAVE_LINE_Y + y) * W + UI_MARGIN + 10 + x];
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
    /* A port alone: in full where it fits, PORT n where it does not. */
    view_ports(d300, none, 0u);
    CHECK_EQ(motor_dshot_ports(), 2u);
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 26u));
    CHECK_STR_EQ(line, "DSHOT300 PROG PORT 2 GP14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 25u));
    CHECK_STR_EQ(line, "DSHOT300 PROG PORT 2 GP14");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 24u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2");
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 PORT 2");
    /* Slots of the same rate first, then the port. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 GP0 +1");
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 26u));
    CHECK_STR_EQ(line, "DSHOT300 GP0 PORT 2");
    /* In the CONTROL card's 25 cells both are named. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 1u,
                                 true, 25u));
    CHECK_STR_EQ(line, "DSHOT300 GP0 PORT 2");
    /* Another rate: MIXED. */
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT600, 1u,
                                 true, 18u));
    CHECK_STR_EQ(line, "MIXED GP0 PORT 2");
    /* Slot pins at a rate no protocol has, beside a port: MIXED too. */
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 1u, true, 18u));
    CHECK_STR_EQ(line, "MIXED GP0 PORT 2");
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
    CHECK(!motor_throttle_target(line, sizeof(line), 0u, 0u, true, 26u));
    CHECK_STR_EQ(line, "DSHOT300 PROG-PORT 2 GP14");
    ui_text_set_language(UI_LANG_EN);
    /* Eight slot pins and two ports in 18 cells: the count holds both. */
    view_ports(d300, d300, 0u);
    CHECK(!motor_throttle_target(line, sizeof(line), PROTO_DSHOT300, 0xFFu,
                                 true, 18u));
    CHECK_STR_EQ(line, "DSHOT300 GP0 +9");
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
    RUN(the_strip_reads_live_program_settings_and_a_tile_opens_live);
    RUN(each_task_keeps_its_view_and_the_tag_leaves_from_either);
    RUN(a_tab_switch_ends_a_drag_and_the_throttle_is_out_of_reach);
    RUN(a_cancel_drops_the_strips_press_and_the_pages);
    RUN(the_settings_rows_are_esc_bench_under_three_headings);
    RUN(every_row_fits_its_columns_in_both_languages);
    RUN(a_typed_value_lands_on_its_step_and_asks_a_save);
    RUN(the_idle_write_takes_setups_pending_changes);
    RUN(the_pulse_rows_change_on_a_safe_bench_only);
    RUN(the_pulse_rows_lock_while_arm_is_held);
    RUN(the_target_row_locks_for_each_condition);
    RUN(the_target_row_opens_outputs_and_the_tag_comes_back);
    RUN(the_target_row_opens_outputs_on_the_throttle_protocol);
    RUN(the_home_tag_closes_the_keypad_then_leaves);
    RUN(the_save_line_follows_the_store);
    RUN(the_target_names_the_dshot_ports_the_throttle_drives);
    RUN(the_target_row_opens_the_entry_of_a_lone_dshot_port);
    free(fb);
    return test_summary("motor_workbench");
}
