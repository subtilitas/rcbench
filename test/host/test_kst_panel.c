/*
 * The KST and Cha servo programmer's overlay (shared/ui/kst_panel.c): what a
 * touch does in each state, what each page asks for, and what the operator
 * is told.  The bench under it is kst_rig.h: the simulated servo, the
 * coprocessor's port, link dispatch and the panel's link module.
 *
 * Not what it looks like; that is the golden images' job.  Every state is
 * rendered here all the same, so a draw that reads past a table falls over
 * under the sanitizers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "kst_limits.h"
#include "kst_panel.h"
#include "kst_reg.h"
#include "kst_rig.h"
#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/* The SERVO screen's left card, as servo_screen.c hands it over. */
static const gfx_rect_t AREA = { 6, 6, 488, 420 };

/* PROGRAM and SETTINGS at the top of the SERVO screen's right card. */
#define PROG_X 632
#define SETB_X 734
#define BTN_Y  24

enum { PG_SERVO = 0, PG_TUNE, PG_PULSE, PG_FAILSAFE, PG_LOCKED };

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;      /* set while a case drives the screen */

#define M (kst_panel_model())

static void show(void)
{
    kst_panel_render(&cv);
}

static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
}

/* The overlay alone, open, the link down. */
static void fresh(void)
{
    canvas();
    scr = NULL;
    rig_fresh();
    kst_panel_init(AREA);
    kst_panel_open();
}

/* The same on the SERVO screen, the overlay closed. */
static void fresh_screen(void)
{
    canvas();
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
    scr = servo_screen();
    scr->reset();
    rig_fresh();
    scr->enter();
}

static void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };

    if (scr != NULL) {
        scr->event(&e);
    } else {
        kst_panel_event(&e);
    }
}

static void tick(float dt)
{
    if (scr != NULL) {
        scr->tick(dt);
    } else {
        kst_panel_tick(dt);
    }
}

static int cx(gfx_rect_t r) { return r.x + r.w / 2; }
static int cy(gfx_rect_t r) { return r.y + r.h / 2; }

static void tap(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    show();
}

static void key(ui_key_t k)
{
    tap(ui_keypad_key_rect(kst_panel_keypad(), k));
}

static void ticks(float secs)
{
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        tick(1.0f / 39.0f);
    }
}

/* Hold a warning page's button for @p secs and let go. */
static void hold(float secs)
{
    const gfx_rect_t r = kst_panel_apply_rect();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(secs);
    show();
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    show();
}

static gfx_rect_t val(int page, int row, int col)
{
    return kst_panel_value_rect(page, row, col);
}

static void settle(void)
{
    CHECK(rig_settle());
    tick(0.02f);
    show();
}

static void link_up(void)
{
    CHECK(rig_link_up(LINK_PROTOCOL_MINOR));
    show();
}

/* The first line of the message shown, "" for none. */
static const char *said(void)
{
    static char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];

    return kst_panel_message_lines(kst_prog_message(M), t) > 0 ? t[0] : "";
}

static unsigned msg(void)
{
    return kst_prog_message(M)->id;
}

static uint16_t draft(kst_field_id_t id)
{
    return kst_field_get(kst_prog_draft(M), id);
}

static uint16_t servo(kst_field_id_t id)
{
    const kst_image_t img = rig_servo();

    return kst_field_get(&img, id);
}

static void type_channel(int ch)
{
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    if (ch >= 10) {
        key(UI_KEY_1);
    }
    key((ui_key_t[]){ UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
                      UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8,
                      UI_KEY_9 }[ch % 10]);
    key(UI_KEY_OK);
}

/* The link up, CH4 typed, CONNECT, the servo read. */
static void connected(void)
{
    link_up();
    type_channel((int)RIG_CH + 1);
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(M));
}

/* ENABLE WRITE TO SERVO through its page. */
static void enable(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK(kst_prog_write_enabled(M));
}

/* A value as a confirmed edit leaves it. */
static void edit(kst_field_id_t id, uint16_t raw)
{
    if (kst_prog_edit(M, id, raw) == KST_PROG_EDIT_SOFT) {
        kst_prog_soft_confirm(M);
    }
    CHECK_EQ(draft(id), raw);
}

/* Three changed values: dead band, boost and soft start. */
static void edits(void)
{
    edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
    edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
    CHECK_EQ(kst_prog_changes(M), 3);
}

/* Any line of the open warning page reads @p s. */
static bool warns(const char *s)
{
    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        if (strcmp(kst_panel_warning_line(i), s) == 0) {
            return true;
        }
    }
    return false;
}

static int choices(void)
{
    int n = 0;

    while (kst_panel_choice_label(n) != NULL) {
        ++n;
    }
    return n;
}

/* The entry of the open list that reads @p s, -1 for none. */
static int choice(const char *s)
{
    for (int i = 0; kst_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(kst_panel_choice_label(i), s) == 0) {
            return i;
        }
    }
    return -1;
}

/* The link goes after the first register of a write of edits(). */
static void cut_write(void)
{
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    for (unsigned i = 0; i < 30000u && g_rig.sim.n_writes == 0u; ++i) {
        rig_frame();
    }
    CHECK(g_rig.sim.n_writes > 0u);
    kst_link_lost(&g_rig.link);
    g_rig.safe = false;
    rig_frames(200);
    show();
}

static void come_back(void)
{
    g_rig.safe = true;
    CHECK(rig_link_up(LINK_PROTOCOL_MINOR));
    tick(0.02f);
    show();
}

/* ------------------------------------------------------------ port and tabs */

TEST_CASE(without_a_link_nothing_on_the_servo_tab_takes_a_tap)
{
    fresh();
    rig_frame();
    show();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    for (int row = 0; row < 5; ++row) {
        tap(val(PG_SERVO, row, 0));
        tap(val(PG_SERVO, row, 1));
    }
    rig_frames(10);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.starts, 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NONE);
    CHECK(!kst_panel_holds_port());
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(a_coprocessor_without_the_page_leaves_the_programmer_unavailable)
{
    fresh();
    CHECK(rig_link_up((uint16_t)(LINK_MINOR_KST - 1u)));
    show();
    CHECK_EQ(kst_prog_state(M), KST_PROG_NO_PAGE);
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 0, 1));
    rig_frames(50);
    CHECK_EQ(g_rig.starts, 0);
    CHECK(!kst_panel_holds_port());
    /* The other tabs show no values and take no tap. */
    for (int page = PG_TUNE; page <= PG_LOCKED; ++page) {
        tap(kst_panel_tab_rect(page));
        CHECK_EQ(kst_panel_page(), page);
        tap(val(page, 1, 0));
        CHECK_EQ(kst_panel_editor(), 0);
    }
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(the_channel_is_typed_inside_1_to_16)
{
    fresh();
    link_up();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_NEAR(kst_panel_keypad()->min, 1.0f, 0.001f);
    CHECK_NEAR(kst_panel_keypad()->max, 16.0f, 0.001f);
    /* 17 and 0 are not channels: the keypad stays and names the range. */
    key(UI_KEY_1);
    key(UI_KEY_7);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    key(UI_KEY_CLR);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CLR);
    key(UI_KEY_1);
    key(UI_KEY_6);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 0);
    /* CANCEL leaves the channel as it was. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_NEAR(kst_panel_keypad()->value, 16.0f, 0.001f);
    key(UI_KEY_3);
    key(UI_KEY_CANCEL);
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 0, 0));
    CHECK_NEAR(kst_panel_keypad()->value, 16.0f, 0.001f);
    key(UI_KEY_CANCEL);
    /* CH16 is no PWM output of this bench: the port says so. */
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_CHANNEL);
    CHECK_STR_EQ(said(), TR(KP_R_CHANNEL));
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
}

TEST_CASE(a_throttle_channel_is_refused_with_the_reason)
{
    fresh();
    link_up();
    /* CH1, the channel the overlay starts on, is the throttle. */
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(g_rig.starts, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_THROTTLE);
    CHECK_STR_EQ(said(), TR(KP_R_THROTTLE));
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!kst_panel_holds_port());
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(a_channel_without_a_servo_says_so_and_gives_the_output_back)
{
    fresh();
    g_rig.sim.powered = false;
    link_up();
    type_channel((int)RIG_CH + 1);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_NO_SERVO);
    CHECK_STR_EQ(said(), TR(KP_S_NO_SERVO));
    CHECK(!kst_prog_have_servo(M));
    if (kst_prog_state(M) == KST_PROG_PROGRAMMING) {
        /* Nothing to edit, nothing to write; LEAVE is the way out. */
        tap(val(PG_SERVO, 3, 0));
        CHECK_EQ(kst_panel_editor(), 0);
        tap(val(PG_SERVO, 0, 1));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
    }
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
}

TEST_CASE(connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held)
{
    fresh();
    connected();
    /* Two requests: into the mode, then the read. */
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(kst_prog_channel(M), RIG_CH);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_STR_EQ(said(), TR(KP_M_READ));
    CHECK(kst_panel_holds_port());
    CHECK(!kst_prog_write_enabled(M));
    /* The channel is fixed while it is held. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    /* CLOSE asks for the way back to PWM; CANCEL stays. */
    tap(kst_panel_close_rect());
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_1)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
}

TEST_CASE(the_tabs_show_their_fields)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_STR_EQ(kst_panel_keypad()->title, "MAX. DUTY");
    CHECK_STR_EQ(kst_panel_keypad()->unit, "%");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_PULSE));
    CHECK_EQ(kst_panel_page(), PG_PULSE);
    tap(val(PG_PULSE, 1, 1));
    CHECK_STR_EQ(kst_panel_keypad()->title, "PULSE UPPER");
    CHECK_STR_EQ(kst_panel_keypad()->unit, "us");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    CHECK_EQ(kst_panel_page(), PG_FAILSAFE);
    tap(val(PG_FAILSAFE, 1, 1));
    CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_FS_AFTER));
    CHECK_STR_EQ(kst_panel_keypad()->unit, "s");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_LOCKED));
    CHECK_EQ(kst_panel_page(), PG_LOCKED);
    tap(kst_panel_tab_rect(PG_SERVO));
    CHECK_EQ(kst_panel_page(), PG_SERVO);
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_locked_value_takes_no_tap)
{
    const uint32_t seq0 = (fresh(), connected(), kst_prog_message(M)->seq);

    tap(kst_panel_tab_rect(PG_LOCKED));
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 2; ++col) {
            tap(val(PG_LOCKED, row, col));
            CHECK_EQ(kst_panel_editor(), 0);
        }
    }
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(kst_prog_message(M)->seq, seq0);
}

/* ---------------------------------------------------------------- the edits */

TEST_CASE(the_keypads_range_is_the_cores_and_a_value_inside_it_is_an_edit)
{
    uint16_t lo = 0, hi = 0;
    int32_t dlo = 0, dhi = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_BOOST, &lo, &hi));
    CHECK(kst_field_display(KST_F_BOOST, lo, &dlo));
    CHECK(kst_field_display(KST_F_BOOST, hi, &dhi));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 1));
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_NEAR(kst_panel_keypad()->min, (float)(dlo < dhi ? dlo : dhi) / 10.0f,
               0.01f);
    CHECK_NEAR(kst_panel_keypad()->max, (float)(dlo < dhi ? dhi : dlo) / 10.0f,
               0.01f);
    CHECK_NEAR(kst_panel_keypad()->value, 300.0f, 0.01f);
    /* 250.0 us is below what the servo holds: taken without a page. */
    key(UI_KEY_2);
    key(UI_KEY_5);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK(draft(KST_F_BOOST) != servo(KST_F_BOOST));
    /* The servo is untouched, and nothing was sent. */
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(g_rig.starts, 2);
}

TEST_CASE(a_value_beyond_a_hard_limit_cannot_be_entered)
{
    uint16_t lo = 0, hi = 0;
    int32_t top = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_DUTY, &lo, &hi));
    CHECK(kst_field_display(KST_F_DUTY, KST_DUTY_RAW_MAX, &top));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_NEAR(kst_panel_keypad()->max, (float)top, 0.01f);
    /* 100 % and 0 % are past the duty limits: the keypad keeps them. */
    key(UI_KEY_1);
    key(UI_KEY_0);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    key(UI_KEY_CLR);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY));
}

TEST_CASE(a_value_past_a_soft_limit_waits_for_a_2_s_hold)
{
    const gfx_rect_t apply = (fresh(), kst_panel_apply_rect());

    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    key(UI_KEY_9);
    key(UI_KEY_7);
    key(UI_KEY_OK);
    /* The page names the change and the reason; nothing is taken yet. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 % -> 97 %");
    CHECK(warns(TR(KP_W_DUTY_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* 1.9 s is not a hold. */
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* A late frame is worth UI_HOLD_MAX_CREDIT_S and no more. */
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    tick(5.0f);
    CHECK_EQ(kst_panel_editor(), 3);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    /* A finger that slides off the button abandons the hold. */
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    ev(apply.x - 4, cy(apply), TOUCH_EVENT_MOVE, 1);
    CHECK(!kst_panel_has_press(1));
    ev(cx(apply), cy(apply), TOUCH_EVENT_MOVE, 1);
    ticks(2.0f);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* CANCEL drops the value. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY));
    /* The hold takes it. */
    tap(val(PG_TUNE, 1, 0));
    key(UI_KEY_9);
    key(UI_KEY_7);
    key(UI_KEY_OK);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK(draft(KST_F_DUTY) != servo(KST_F_DUTY));
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(the_hold_button_fills_through_its_own_revision)
{
    const gfx_rect_t apply = (fresh(), kst_panel_apply_rect());
    uint32_t rev, hold_rev;

    connected();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    rev = kst_panel_revision();
    hold_rev = kst_panel_hold_revision();
    tick(0.1f);
    CHECK_EQ(kst_panel_hold_revision(), hold_rev);
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    rev = kst_panel_revision();
    tick(0.1f);
    tick(0.1f);
    CHECK(kst_panel_hold_revision() != hold_rev);
    CHECK_EQ(kst_panel_revision(), rev);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK(!kst_prog_write_enabled(M));
}

TEST_CASE(stretch_is_chosen_from_the_values_the_core_takes)
{
    uint16_t lo = 0, hi = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_STRETCH, &lo, &hi));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), (int)(hi - lo) + 1);
    /* CANCEL leaves it. */
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* A higher stretch is past a soft limit. */
    tap(val(PG_TUNE, 2, 0));
    tap(kst_panel_choice_rect((int)(servo(KST_F_STRETCH) - lo) + 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_STRETCH_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    hold(2.1f);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK_EQ(draft(KST_F_STRETCH), servo(KST_F_STRETCH) + 1u);
    /* A release off the entry chooses nothing. */
    tap(val(PG_TUNE, 2, 0));
    ev(cx(kst_panel_choice_rect(0)), cy(kst_panel_choice_rect(0)),
       TOUCH_EVENT_DOWN, 1);
    ev(cx(kst_panel_choice_rect(0)), AREA.y + AREA.h - 60, TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
}

TEST_CASE(a_switch_is_toggled_by_its_tap_and_warns_where_the_core_does)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    /* SOFT START off. */
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_SOFT_START_OFF)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_prog_changes(M), 0);
    tap(val(PG_TUNE, 2, 1));
    hold(2.1f);
    CHECK_EQ(kst_prog_changes(M), 1);
    /* Back on is no warning. */
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* REVERSION. */
    tap(kst_panel_tab_rect(PG_PULSE));
    tap(val(PG_PULSE, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_REVERSION_CHANGED)));
    tap(kst_panel_cancel_rect());
    /* MOVE ON SIGNAL LOSS. */
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    tap(val(PG_FAILSAFE, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_ALLOW_UNCONT_ON)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_switch_the_core_refuses_says_which_limit)
{
    fresh();
    connected();
    /* SPEED LIMIT on with SPEED below the least the core takes. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 3, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(M)->arg, KST_H_SPD_SEL_LOW_SPD);
    CHECK_STR_EQ(said(), TR(KP_H_SPD_SEL_LOW_SPD));
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_step_key_acts_on_the_press_and_repeats_while_held)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_DEAD_BAND));
    const gfx_rect_t plus = kst_panel_step_rect(PG_TUNE, 0, true);
    uint16_t lo = 0, hi = 0;
    int steps;

    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK(kst_prog_range(M, KST_F_DEAD_BAND, &lo, &hi));
    /* The press is one step: a wider dead band, no warning. */
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    steps = abs((int)draft(KST_F_DEAD_BAND) - (int)was);
    CHECK_EQ(steps, 1);
    CHECK_EQ(kst_panel_editor(), 0);
    /* Nothing more before 0.45 s. */
    ticks(0.40f);
    CHECK_EQ(abs((int)draft(KST_F_DEAD_BAND) - (int)was), 1);
    /* Then 8 a second: 1 s after the press, 1 + 1 + 4. */
    ticks(0.60f);
    steps = abs((int)draft(KST_F_DEAD_BAND) - (int)was);
    CHECK(steps >= 5 && steps <= 7);
    /* A finger that leaves the key ends the repeat. */
    ev(plus.x + plus.w + 30, cy(plus), TOUCH_EVENT_MOVE, 1);
    CHECK(!kst_panel_has_press(1));
    ticks(1.0f);
    CHECK_EQ(abs((int)draft(KST_F_DEAD_BAND) - (int)was), steps);
    ev(plus.x + plus.w + 30, cy(plus), TOUCH_EVENT_UP, 1);
    /* Held long, the value stops at the end of the core's range. */
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ticks(40.0f);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    show();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(draft(KST_F_DEAD_BAND) == lo || draft(KST_F_DEAD_BAND) == hi);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(the_repeat_speeds_up_after_1_8_s_more)
{
    const gfx_rect_t plus = (fresh(), kst_panel_step_rect(PG_TUNE, 0, true));
    uint16_t at_2s, at_3s;

    connected();
    /* From the narrow end there is room for the count. */
    tap(kst_panel_tab_rect(PG_TUNE));
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ticks(2.25f);
    at_2s = draft(KST_F_DEAD_BAND);
    ticks(1.0f);
    at_3s = draft(KST_F_DEAD_BAND);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    /* 8 Hz for 1.8 s is 15 steps and the press; then 30 a second. */
    CHECK(abs((int)at_2s - (int)servo(KST_F_DEAD_BAND)) >= 15);
    CHECK(abs((int)at_2s - (int)servo(KST_F_DEAD_BAND)) <= 17);
    CHECK(abs((int)at_3s - (int)at_2s) >= 28);
    CHECK(abs((int)at_3s - (int)at_2s) <= 31);
}

TEST_CASE(a_step_past_a_soft_limit_opens_the_page_and_ends_the_repeat)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_DEAD_BAND));
    const gfx_rect_t minus = kst_panel_step_rect(PG_TUNE, 0, false);

    tap(kst_panel_tab_rect(PG_TUNE));
    /* A narrower dead band. */
    ev(cx(minus), cy(minus), TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_DEAD_BAND_NARROWED)));
    CHECK_EQ(draft(KST_F_DEAD_BAND), was);
    /* The finger still on the glass holds nothing. */
    ticks(3.0f);
    ev(cx(minus), cy(minus), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(draft(KST_F_DEAD_BAND), was);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(draft(KST_F_DEAD_BAND) != was);
    /* The repeat runs into the next page the same way. */
    ev(cx(minus), cy(minus), TOUCH_EVENT_DOWN, 1);
    if (kst_panel_editor() == 0) {
        ticks(30.0f);
    }
    ev(cx(minus), cy(minus), TOUCH_EVENT_UP, 1);
    show();
    CHECK(kst_panel_editor() == 3 || kst_prog_changes(M) == 1);
}

TEST_CASE(neutral_steps_through_its_page)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_NEUTRAL));
    const gfx_rect_t plus = kst_panel_step_rect(PG_PULSE, 0, true);

    tap(kst_panel_tab_rect(PG_PULSE));
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    if (kst_panel_editor() == 3) {
        CHECK(warns(TR(KP_W_NEUTRAL_MOVED)));
        hold(2.1f);
    }
    CHECK(draft(KST_F_NEUTRAL) != was);
    CHECK_EQ(kst_prog_changes(M), 1);
}

/* ------------------------------------------------------- the switch, a write */

TEST_CASE(enable_write_is_off_until_its_page_is_held)
{
    fresh();
    connected();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_ENABLE_1)));
    CHECK(!kst_prog_write_enabled(M));
    hold(1.9f);
    CHECK(!kst_prog_write_enabled(M));
    tap(kst_panel_cancel_rect());
    CHECK(!kst_prog_write_enabled(M));
    enable();
    /* Off again is one tap. */
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_write_enabled(M));
    /* And it goes off at each entry of the screen. */
    enable();
    kst_panel_screen_entry();
    CHECK(!kst_prog_write_enabled(M));
    /* And when the link goes. */
    enable();
    rig_link_lost();
    CHECK(!kst_prog_write_enabled(M));
}

TEST_CASE(a_write_needs_changes_the_switch_and_a_hold)
{
    fresh();
    connected();
    /* No changes: the button takes no tap. */
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    edits();
    /* The switch is off: said, no page. */
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    CHECK_STR_EQ(said(), TR(KP_M_NEED_ENABLE));
    enable();
    CHECK_EQ(kst_prog_changes(M), 3);
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    /* The first write of a session asks for a free horn. */
    CHECK(warns(TR(KP_HORN_FREE)));
    CHECK(warns(TR(KP_BENCH_FIRST)));
    CHECK(warns(TR(KP_WRITE_NO_CARD)));
    hold(1.9f);
    rig_frames(50);
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_message(M)->arg, 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(servo(KST_F_DEAD_BAND), draft(KST_F_DEAD_BAND));
    CHECK_EQ(servo(KST_F_BOOST), draft(KST_F_BOOST));
    CHECK_EQ(servo(KST_F_SOFT_START), draft(KST_F_SOFT_START));
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
    /* The second write of the session does not ask again. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns(TR(KP_HORN_FREE)));
    CHECK(warns(TR(KP_WRITE_1)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_prog_changes(M), 1);
}

TEST_CASE(with_a_card_the_values_before_the_write_go_to_it_first)
{
    fresh();
    g_rig.card = true;
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns(TR(KP_WRITE_CARD)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(g_rig.file_writes >= 1u);
}

TEST_CASE(a_card_that_takes_no_file_stops_the_write)
{
    fresh();
    g_rig.card = true;
    g_rig.card_fails = true;
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STORE_FAILED);
    CHECK_STR_EQ(said(), TR(KP_M_STORE_FAILED));
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
}

TEST_CASE(a_write_the_servo_does_not_take_names_the_register)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    int n;

    fresh();
    connected();
    enable();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
    sim_add_fault(&g_rig.sim, SIM_WRITE, 0x03, 0, 100, SIM_F_WRITE_IGNORE, 0);
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_WRITE_FAILED);
    n = kst_panel_message_lines(kst_prog_message(M), t);
    CHECK_EQ(n, 2);
    CHECK_STR_EQ(t[0], TR(KP_S_WRITE_FAILED));
    CHECK(strstr(t[1], "03") != NULL);
    CHECK_EQ(kst_prog_changes(M), 1);
}

TEST_CASE(port_is_abort_while_an_operation_runs)
{
    fresh();
    link_up();
    type_channel((int)RIG_CH + 1);
    tap(val(PG_SERVO, 0, 1));
    rig_frames(40);
    show();
    CHECK(kst_prog_state(M) == KST_PROG_ENTERING
          || kst_prog_state(M) == KST_PROG_WORKING);
    CHECK(kst_panel_holds_port());
    /* CLOSE does nothing while the port moves. */
    tap(kst_panel_close_rect());
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 0);
    /* READ ALL takes no tap; PORT aborts. */
    tap(val(PG_SERVO, 1, 0));
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK(g_rig.aborts >= 1u);
    CHECK(msg() != KST_PROG_MSG_NONE);
    CHECK(said()[0] != '\0');
    CHECK(!kst_prog_have_servo(M) || kst_prog_in_doubt(M));
}

TEST_CASE(stop_aborts_a_write_and_the_servo_must_be_read)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    for (unsigned i = 0; i < 30000u && g_rig.sim.n_writes == 0u; ++i) {
        rig_frame();
    }
    rig_stop(true);
    settle();
    CHECK(!kst_prog_write_enabled(M));
    CHECK(kst_prog_in_doubt(M) || kst_prog_state(M) != KST_PROG_PROGRAMMING);
    CHECK(said()[0] != '\0');
    /* While STOP is latched a write is not offered. */
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    rig_stop(false);
    settle();
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

/* ------------------------------------------------- link loss and the journal */

TEST_CASE(a_link_lost_in_a_write_leaves_a_servo_nobody_should_use)
{
    const kst_image_t before = sim_bench_image();
    kst_image_t now;
    int at;

    fresh();
    g_rig.card = true;
    cut_write();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_STR_EQ(said(), TR(KP_M_JOURNAL));
    CHECK(kst_prog_journal(M));
    CHECK(!kst_prog_write_enabled(M));
    CHECK_EQ(kst_panel_editor(), 0);
    /* The values before the write are on the card. */
    CHECK(g_rig.file_len > 0u);

    come_back();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_in_doubt(M));
    CHECK(kst_panel_holds_port());
    /* Nothing is edited and nothing stored until the servo is read. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    /* LEAVE says what leaving costs. */
    tap(val(PG_SERVO, 0, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_DOUBT_1)));
    tap(kst_panel_cancel_rect());

    /* READ ALL compares the servo with the values before the write. */
    tap(val(PG_SERVO, 1, 0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_DIFF);
    CHECK_STR_EQ(said(), TR(KP_M_INCONSISTENT));
    now = rig_servo();
    CHECK(memcmp(now.r, before.r, sizeof(now.r)) != 0);

    /* RESTORE needs the switch, and says so. */
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    at = choice(TR(KP_FROM_SESSION));
    CHECK(at >= 0);
    CHECK(choice(TR(KP_FORGET)) >= 0);
    tap(kst_panel_choice_rect(at));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    enable();
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    CHECK_STR_EQ(said(), TR(KP_M_RESTORED));
    now = rig_servo();
    CHECK(memcmp(now.r, before.r, sizeof(now.r)) == 0);
    CHECK(!kst_prog_in_doubt(M));
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

TEST_CASE(the_record_of_an_interrupted_write_is_discarded_only_by_its_entry)
{
    int at;

    fresh();
    cut_write();
    come_back();
    tap(val(PG_SERVO, 1, 0));
    settle();
    CHECK(M->journal);
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
    CHECK(M->journal);
    tap(val(PG_SERVO, 2, 0));
    at = choice(TR(KP_FORGET));
    CHECK(at >= 0);
    tap(kst_panel_choice_rect(at));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!M->journal);
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK_STR_EQ(said(), TR(KP_M_FORGOTTEN));
}

TEST_CASE(a_coprocessor_that_restarts_ends_the_session_and_says_so)
{
    fresh();
    connected();
    enable();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    /* The coprocessor starts again: the port is back in PWM. */
    rig_link_lost();
    CHECK_EQ(kst_panel_editor(), 0);
    rig_port_fresh();
    come_back();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK(!kst_prog_write_enabled(M));
    CHECK(!kst_prog_have_servo(M));
    CHECK(!kst_panel_holds_port());
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(an_open_page_goes_when_the_link_goes)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    rig_link_lost();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    /* A list too. */
    come_back();
    if (kst_prog_state(M) == KST_PROG_PROGRAMMING) {
        tap(val(PG_SERVO, 1, 0));
        settle();
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(kst_panel_editor(), 2);
        rig_link_lost();
        CHECK_EQ(kst_panel_editor(), 0);
    }
}

/* ------------------------------------------------------- backup and restore */

TEST_CASE(store_keeps_a_backup_and_says_where)
{
    fresh();
    connected();
    /* Without a card it is kept in memory. */
    tap(val(PG_SERVO, 1, 1));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STORED_RAM);
    CHECK_STR_EQ(said(), TR(KP_M_STORED_RAM));
    CHECK(kst_prog_have_stored(M));
    CHECK_EQ(g_rig.file_writes, 0);
    /* With one it goes to KST.TXT. */
    g_rig.card = true;
    rig_frame();
    tap(val(PG_SERVO, 1, 1));
    settle();
    rig_frames(5);
    CHECK_EQ(msg(), KST_PROG_MSG_STORED);
    CHECK_STR_EQ(said(), TR(KP_M_STORED));
    CHECK(g_rig.file_len > 0u);
    /* A card that fails leaves the backup in memory, and that is said. */
    g_rig.card_fails = true;
    tap(val(PG_SERVO, 1, 1));
    settle();
    rig_frames(5);
    CHECK_EQ(msg(), KST_PROG_MSG_STORED_RAM);
    CHECK(kst_prog_have_stored(M));
    /* The servo's own values are no restore. */
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(a_stored_backup_is_written_back_as_an_edit_of_the_editable_fields)
{
    char file[KST_PROG_FILE_MAX];
    size_t len;
    const kst_image_t before = sim_bench_image();
    kst_image_t now;
    int at;

    fresh();
    g_rig.card = true;
    connected();
    tap(val(PG_SERVO, 1, 1));
    settle();
    rig_frames(5);
    len = g_rig.file_len;
    memcpy(file, g_rig.file, len + 1u);

    /* A panel that starts later, the servo changed in between. */
    fresh();
    g_rig.card = true;
    CHECK(kst_panel_file_load(file, len));
    CHECK(kst_prog_have_stored(M));
    g_rig.sim.reg[0x03] = (uint8_t)(g_rig.sim.reg[0x03] - 4u);
    connected();
    now = rig_servo();
    CHECK(memcmp(now.r, before.r, sizeof(now.r)) != 0);
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    at = choice(TR(KP_FROM_STORED));
    CHECK(at >= 0);
    CHECK(choice(TR(KP_FROM_SESSION)) < 0);
    tap(kst_panel_choice_cancel_rect());
    enable();
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_STORED))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_STORED)));
    CHECK(warns(TR(KP_HORN_FREE)));
    CHECK(warns(TR(KP_WRITE_CARD)));
    hold(2.1f);
    settle();
    CHECK(msg() == KST_PROG_MSG_RESTORED || msg() == KST_PROG_MSG_WRITTEN);
    now = rig_servo();
    CHECK(memcmp(now.r, before.r, sizeof(now.r)) == 0);
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

TEST_CASE(a_file_that_is_not_readable_is_said_and_not_used)
{
    fresh();
    CHECK(!kst_panel_file_load("KST 1\n00=zz\n", 12));
    CHECK(!kst_prog_have_stored(M));
    CHECK_EQ(msg(), KST_PROG_MSG_FILE_BAD);
    CHECK_STR_EQ(said(), TR(KP_M_FILE_BAD));
}

TEST_CASE(more_holds_verify_discard_and_release_pairing)
{
    fresh();
    connected();
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 3);
    /* VERIFY. */
    tap(kst_panel_choice_rect(0));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_VERIFIED);
    CHECK_STR_EQ(said(), TR(KP_M_VERIFIED));
    /* A servo that changed under the session. */
    g_rig.sim.reg[0x03] = (uint8_t)(g_rig.sim.reg[0x03] - 1u);
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(0));
    settle();
    CHECK(msg() != KST_PROG_MSG_VERIFIED);
    CHECK(said()[0] != '\0');
    tap(val(PG_SERVO, 1, 0));
    settle();
    /* DISCARD EDITS. */
    edits();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(1));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* RELEASE PAIRING needs the switch. */
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    enable();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    tap(kst_panel_cancel_rect());
    CHECK(g_rig.sim.reg[KST_REG_NODE_ADDR] != 0u);
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(2));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_STR_EQ(said(), TR(KP_M_RELEASED));
    CHECK_EQ(g_rig.sim.reg[KST_REG_NODE_ADDR], 0);
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

/* ---------------------------------------------------------------- read only */

TEST_CASE(a_servo_that_fails_the_fingerprint_is_shown_and_never_written)
{
    fresh();
    g_rig.sim.reg[0x06] = 0x05u;
    link_up();
    type_channel((int)RIG_CH + 1);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK(kst_prog_have_servo(M));
    CHECK(!kst_prog_editable(M));
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_STR_EQ(said(), TR(KP_M_READ_ONLY));
    /* The switch stays off. */
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_write_enabled(M));
    /* No field takes a tap. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(kst_panel_step_rect(PG_TUNE, 0, true));
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* Reading and the backup stay. */
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 1));
    settle();
    CHECK(kst_prog_have_stored(M));
    tap(val(PG_SERVO, 1, 0));
    settle();
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

/* -------------------------------------------------------------------- leave */

TEST_CASE(leave_asks_for_the_power_cycle_and_gives_the_output_back)
{
    fresh();
    connected();
    edits();
    tap(val(PG_SERVO, 0, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    /* No rail the port switches: the servo is unplugged by hand. */
    CHECK(warns(TR(KP_LEAVE_CHANGES)));
    CHECK(warns(TR(KP_LEAVE_UNPLUG_1)));
    CHECK(!warns(TR(KP_LEAVE_RAIL_2)));
    hold(1.9f);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_STR_EQ(said(), TR(KP_M_LEFT));
    CHECK(!kst_panel_holds_port());
    CHECK(!kst_prog_have_servo(M));
    CHECK_EQ(g_rig.sim.n_writes, 0);
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
}

TEST_CASE(a_port_with_a_rail_says_that_it_switches_the_supply)
{
    kst_prog_feed_t f;

    fresh();
    connected();
    rig_feed_now(&f);
    f.rd.flags |= LINK_KS_F_RAIL;
    kst_panel_feed(&f);
    tap(val(PG_SERVO, 0, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_RAIL_2)));
    CHECK(!warns(TR(KP_LEAVE_UNPLUG_1)));
    tap(kst_panel_cancel_rect());
}

/* ----------------------------------------------------------------- presses */

TEST_CASE(one_press_at_a_time_and_a_lost_touch_stream_drops_it)
{
    const gfx_rect_t port = (fresh(), val(PG_SERVO, 0, 1));
    const gfx_rect_t ch = val(PG_SERVO, 0, 0);

    link_up();
    /* A second finger is not a press. */
    ev(cx(port), cy(port), TOUCH_EVENT_DOWN, 1);
    CHECK(kst_panel_has_press(1));
    ev(cx(ch), cy(ch), TOUCH_EVENT_DOWN, 2);
    ev(cx(ch), cy(ch), TOUCH_EVENT_UP, 2);
    CHECK(!kst_panel_has_press(2));
    CHECK_EQ(kst_panel_editor(), 0);
    /* The stream lost events: the press is dropped and its release acts on
     * nothing. */
    kst_panel_cancel();
    CHECK(!kst_panel_has_press(1));
    ev(cx(port), cy(port), TOUCH_EVENT_UP, 1);
    rig_frames(20);
    CHECK_EQ(g_rig.starts, 0);
    /* A release off the control is no tap. */
    ev(cx(port), cy(port), TOUCH_EVENT_DOWN, 1);
    ev(cx(port), cy(port) + 60, TOUCH_EVENT_UP, 1);
    rig_frames(20);
    CHECK_EQ(g_rig.starts, 0);
    /* A press beside every control is none. */
    ev(AREA.x + 4, AREA.y + AREA.h - 4, TOUCH_EVENT_DOWN, 1);
    CHECK(!kst_panel_has_press(1));
    /* A closed overlay takes nothing. */
    tap(kst_panel_close_rect());
    CHECK(!kst_panel_is_open());
    tap(port);
    rig_frames(20);
    CHECK_EQ(g_rig.starts, 0);
    kst_panel_event(NULL);
}

TEST_CASE(leaving_the_screen_closes_the_pages_and_keeps_the_port)
{
    fresh();
    connected();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    kst_panel_leave();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_panel_is_open());
    CHECK(kst_panel_holds_port());
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    /* A soft limit left open is a value not taken. */
    kst_panel_open();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    kst_panel_leave();
    kst_panel_open();
    CHECK_EQ(kst_prog_changes(M), 0);
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    tap(kst_panel_cancel_rect());
}

TEST_CASE(the_revision_moves_with_what_is_shown_and_rests_with_it)
{
    uint32_t rev;

    fresh();
    connected();
    rig_frames(20);
    rev = kst_panel_revision();
    /* Nothing happens: nothing to draw. */
    rig_frames(500);
    kst_panel_tick(0.02f);
    CHECK_EQ(kst_panel_revision(), rev);
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    rig_frame();
    CHECK(kst_panel_revision() != rev);
    rev = kst_panel_revision();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK(kst_panel_revision() != rev);
    rev = kst_panel_revision();
    kst_panel_open();
    CHECK_EQ(kst_panel_revision(), rev);
}

/* ---------------------------------------------------------------- the texts */

static bool lines_ok(unsigned id, unsigned arg, uint8_t reg)
{
    const kst_prog_msg_t m = { .id = (uint16_t)id, .level = KST_PROG_WARN,
                               .reg = reg, .arg = (uint16_t)arg, .seq = 1 };
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const int n = kst_panel_message_lines(&m, t);
    bool ok = n >= 1 && n <= KST_PANEL_MSG_LINES;

    for (int i = 0; i < n; ++i) {
        /* 58 cells fit the card. */
        ok = ok && t[i][0] != '\0' && strlen(t[i]) <= 62u;
        ok = ok && strchr(t[i], '%') == NULL;
    }
    return ok;
}

/* Every id of @p from .. @p from + n - 1 has a text of its own kind. */
static bool family_ok(unsigned from, unsigned n)
{
    bool ok = true;

    for (unsigned i = 0; i < n; ++i) {
        char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
        char unknown[KST_PANEL_LINE];
        const kst_prog_msg_t m = { .id = (uint16_t)(from + i), .reg = 0xFFu };

        snprintf(unknown, sizeof(unknown), TR(KP_M_UNKNOWN), from + i);
        ok = ok && lines_ok(from + i, 3, 0xFFu) && lines_ok(from + i, 3, 0x1Fu);
        (void)kst_panel_message_lines(&m, t);
        ok = ok && strcmp(t[0], unknown) != 0;
    }
    return ok;
}

static void texts_in(ui_lang_t lang)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    kst_prog_msg_t m = { .id = KST_PROG_MSG_NONE };

    ui_text_set_language(lang);
    CHECK_EQ(kst_panel_message_lines(&m, t), 0);
    /* The fixed messages, the answers of start, the outcome kinds, the
     * port's refusals and the session's results. */
    CHECK(family_ok(1u, (unsigned)KST_PROG_MSG_TIMEOUT));
    CHECK(family_ok(KST_PROG_MSG_START, (unsigned)KST_LINK_START_NO_IMAGE + 1u));
    CHECK(family_ok(KST_PROG_MSG_OUT, (unsigned)KST_LINK_OUT_DROPPED + 1u));
    CHECK(family_ok(KST_PROG_MSG_REF, (unsigned)LINK_KST_REF_PLAN_NO_PATH + 1u));
    CHECK(family_ok(KST_PROG_MSG_SES, (unsigned)KST_SES_ERR_ABORTED + 1u));
    /* A hard limit's message is the rule's line. */
    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        m.id = KST_PROG_MSG_EDIT_HARD;
        m.arg = (uint16_t)rule;
        CHECK(kst_panel_rule_text(rule)[0] != '\0');
        CHECK(strlen(kst_panel_rule_text(rule)) <= 62u);
        CHECK_EQ(kst_panel_message_lines(&m, t), 1);
        CHECK_STR_EQ(t[0], kst_panel_rule_text(rule));
        m.id = KST_PROG_MSG_RESTORE_REFUSED;
        CHECK_EQ(kst_panel_message_lines(&m, t), 2);
        CHECK_STR_EQ(t[1], kst_panel_rule_text(rule));
    }
    CHECK_STR_EQ(kst_panel_rule_text((unsigned)KST_RULE_COUNT),
                 TR(KP_M_OUTSIDE));
    /* An id nobody gave a text is still a line. */
    m.id = 0x1Fu;
    m.arg = 0;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK(strstr(t[0], "31") != NULL);
    m.id = KST_PROG_MSG_END + 5u;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    /* A servo nobody should use is said with the result. */
    m.id = KST_PROG_MSG_SES + KST_SES_ERR_TORN;
    m.reg = 0x03u;
    m.arg = 2;
    CHECK_EQ(kst_panel_message_lines(&m, t), 3);
    CHECK_STR_EQ(t[1], TR(KP_M_DO_NOT_USE));
    CHECK(strstr(t[2], "03") != NULL);
    m.id = KST_PROG_MSG_SES + KST_SES_ERR_LOCKED;
    m.reg = 0xFFu;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK_STR_EQ(t[1], TR(KP_S_LOCKED_2));
    /* Locked values that differ from a backup are counted. */
    m.id = KST_PROG_MSG_NO_RESTORE;
    m.arg = 2;
    CHECK_EQ(kst_panel_message_lines(&m, t), 2);
    CHECK(strstr(t[1], "2") != NULL);
    m.arg = 0;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
}

TEST_CASE(every_message_refusal_result_and_rule_has_a_line_in_english)
{
    fresh();
    texts_in(UI_LANG_EN);
}

TEST_CASE(every_message_refusal_result_and_rule_has_a_line_in_german)
{
    fresh();
    texts_in(UI_LANG_DE);
    /* And it is German. */
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_M_READ),
                 ui_tr_in(UI_LANG_EN, TX_KP_M_READ)) != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_HORN_FREE),
                 ui_tr_in(UI_LANG_EN, TX_KP_HORN_FREE)) != 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(every_state_is_drawn_in_both_languages)
{
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.card = true;
        cut_write();
        come_back();
        for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
            tap(kst_panel_tab_rect(page));
        }
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 1, 0));
        settle();
        tap(val(PG_SERVO, 1, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_LOCKED);
        rig_stop(true);
        rig_frames(5);
        show();
        rig_stop(false);
        rig_frames(5);
        tap(val(PG_SERVO, 2, 0));
        show();
        tap(kst_panel_choice_cancel_rect());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* --------------------------------------------------------- the SERVO screen */

static void screen_tap(int x, int y)
{
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    scr->tick(0.02f);
    scr->render(&cv, 0);
}

TEST_CASE(program_opens_the_programmer_over_the_left_card_and_closes_it)
{
    fresh_screen();
    link_up();
    CHECK(!kst_panel_is_open());
    screen_tap(PROG_X, BTN_Y);
    CHECK(kst_panel_is_open());
    /* The overlay takes the taps on the left card. */
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    screen_tap(PROG_X, BTN_Y);
    CHECK(!kst_panel_is_open());
    /* SETTINGS takes its place. */
    screen_tap(PROG_X, BTN_Y);
    CHECK(kst_panel_is_open());
    screen_tap(SETB_X, BTN_Y);
    CHECK(!kst_panel_is_open());
    screen_tap(SETB_X, BTN_Y);
    /* And PROGRAM takes the settings' place. */
    screen_tap(SETB_X, BTN_Y);
    screen_tap(PROG_X, BTN_Y);
    CHECK(kst_panel_is_open());
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
}

TEST_CASE(the_programmer_stays_on_the_screen_while_the_port_holds_a_channel)
{
    servo_cmd_t junk;

    fresh_screen();
    link_up();
    screen_tap(PROG_X, BTN_Y);
    type_channel((int)RIG_CH + 1);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK(kst_panel_holds_port());
    /* SETTINGS does not cover it. */
    ev(SETB_X, BTN_Y, TOUCH_EVENT_DOWN, 1);
    ev(SETB_X, BTN_Y, TOUCH_EVENT_UP, 1);
    CHECK(kst_panel_is_open());
    /* PROGRAM asks for the way back and closes nothing. */
    screen_tap(PROG_X, BTN_Y);
    CHECK(kst_panel_is_open());
    CHECK_EQ(kst_panel_editor(), 3);
    tap(kst_panel_cancel_rect());
    /* The switch goes off when the screen is entered again, and the
     * overlay is there again. */
    enable();
    scr->leave();
    CHECK(!kst_panel_is_open());
    CHECK(kst_prog_write_enabled(M));
    scr->enter();
    CHECK(!kst_prog_write_enabled(M));
    scr->tick(0.02f);
    CHECK(kst_panel_is_open());
    scr->render(&cv, 0);
    scr->render(&cv, 1);
    /* Leaving programming gives the tester its screen back. */
    tap(val(PG_SERVO, 0, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    screen_tap(PROG_X, BTN_Y);
    CHECK(!kst_panel_is_open());
    /* The tester's controls answer: ARM is asked for. */
    while (servo_screen_take(&junk)) { }
    screen_tap(SETB_X, BTN_Y);
    scr->render(&cv, 0);
    screen_tap(SETB_X, BTN_Y);
    CHECK(!kst_panel_is_open());
}

TEST_CASE(a_lost_touch_stream_on_the_screen_drops_the_programmers_hold)
{
    const gfx_rect_t apply = (fresh_screen(), kst_panel_apply_rect());

    link_up();
    screen_tap(PROG_X, BTN_Y);
    type_channel((int)RIG_CH + 1);
    tap(val(PG_SERVO, 0, 1));
    settle();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    scr->render(&cv, 0);
    scr->cancel();
    ticks(2.0f);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK(!kst_prog_write_enabled(M));
}

int main(void)
{
    RUN(without_a_link_nothing_on_the_servo_tab_takes_a_tap);
    RUN(a_coprocessor_without_the_page_leaves_the_programmer_unavailable);
    RUN(the_channel_is_typed_inside_1_to_16);
    RUN(a_throttle_channel_is_refused_with_the_reason);
    RUN(a_channel_without_a_servo_says_so_and_gives_the_output_back);
    RUN(connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held);
    RUN(the_tabs_show_their_fields);
    RUN(a_locked_value_takes_no_tap);
    RUN(the_keypads_range_is_the_cores_and_a_value_inside_it_is_an_edit);
    RUN(a_value_beyond_a_hard_limit_cannot_be_entered);
    RUN(a_value_past_a_soft_limit_waits_for_a_2_s_hold);
    RUN(the_hold_button_fills_through_its_own_revision);
    RUN(stretch_is_chosen_from_the_values_the_core_takes);
    RUN(a_switch_is_toggled_by_its_tap_and_warns_where_the_core_does);
    RUN(a_switch_the_core_refuses_says_which_limit);
    RUN(a_step_key_acts_on_the_press_and_repeats_while_held);
    RUN(the_repeat_speeds_up_after_1_8_s_more);
    RUN(a_step_past_a_soft_limit_opens_the_page_and_ends_the_repeat);
    RUN(neutral_steps_through_its_page);
    RUN(enable_write_is_off_until_its_page_is_held);
    RUN(a_write_needs_changes_the_switch_and_a_hold);
    RUN(with_a_card_the_values_before_the_write_go_to_it_first);
    RUN(a_card_that_takes_no_file_stops_the_write);
    RUN(a_write_the_servo_does_not_take_names_the_register);
    RUN(port_is_abort_while_an_operation_runs);
    RUN(stop_aborts_a_write_and_the_servo_must_be_read);
    RUN(a_link_lost_in_a_write_leaves_a_servo_nobody_should_use);
    RUN(the_record_of_an_interrupted_write_is_discarded_only_by_its_entry);
    RUN(a_coprocessor_that_restarts_ends_the_session_and_says_so);
    RUN(an_open_page_goes_when_the_link_goes);
    RUN(store_keeps_a_backup_and_says_where);
    RUN(a_stored_backup_is_written_back_as_an_edit_of_the_editable_fields);
    RUN(a_file_that_is_not_readable_is_said_and_not_used);
    RUN(more_holds_verify_discard_and_release_pairing);
    RUN(a_servo_that_fails_the_fingerprint_is_shown_and_never_written);
    RUN(leave_asks_for_the_power_cycle_and_gives_the_output_back);
    RUN(a_port_with_a_rail_says_that_it_switches_the_supply);
    RUN(one_press_at_a_time_and_a_lost_touch_stream_drops_it);
    RUN(leaving_the_screen_closes_the_pages_and_keeps_the_port);
    RUN(the_revision_moves_with_what_is_shown_and_rests_with_it);
    RUN(every_message_refusal_result_and_rule_has_a_line_in_english);
    RUN(every_message_refusal_result_and_rule_has_a_line_in_german);
    RUN(every_state_is_drawn_in_both_languages);
    RUN(program_opens_the_programmer_over_the_left_card_and_closes_it);
    RUN(the_programmer_stays_on_the_screen_while_the_port_holds_a_channel);
    RUN(a_lost_touch_stream_on_the_screen_drops_the_programmers_hold);
    return test_summary("kst_panel");
}
