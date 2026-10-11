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

/* The link up, CONNECT, the servo read. */
static void connected(void)
{
    link_up();
    CHECK_EQ(kst_panel_editor(), 0);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(M));
}

/* The SERVO tab's line @p i under its rows, "" for none. */
static const char *status(int i)
{
    const char *t = kst_panel_status_line(i);

    return t != NULL ? t : "";
}

/* The tenths of a second the state line counts down with text @p id, -1
 * while it reads something else. */
static int tenths(ui_text_id_t id)
{
    char want[KST_PANEL_LINE];

    for (unsigned t = 0; t <= 99u; ++t) {
        snprintf(want, sizeof(want), ui_tr(id), t / 10u, t % 10u);
        if (strcmp(want, status(0)) == 0) {
            return (int)t;
        }
    }
    return -1;
}

/* MORE, and the entry @p i of its list. */
static void more(int i)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_rect(i));
}

/* Digits, a dot for '.', then OK. */
static void type(const char *t)
{
    static const ui_key_t d[] = { UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3,
                                  UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_7,
                                  UI_KEY_8, UI_KEY_9 };

    key(UI_KEY_CLR);
    for (; *t != '\0'; ++t) {
        key(*t == '.' ? UI_KEY_DOT : d[*t - '0']);
    }
    key(UI_KEY_OK);
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

TEST_CASE(programming_is_locked_until_the_port_is_the_testers_target)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.target = 0u;
        link_up();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_STR_EQ(status(0), TR(KP_NEED_TARGET));
        CHECK_STR_EQ(status(1), TR(KP_NEED_TARGET_2));
        /* No row takes a tap: nothing is asked of the port. */
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 2; ++col) {
                tap(val(PG_SERVO, row, col));
                CHECK_EQ(kst_panel_editor(), 0);
            }
        }
        settle();
        CHECK_EQ(g_rig.starts, 0);
        CHECK(!kst_panel_holds_port());
        /* The model says the same to whoever asks it. */
        CHECK_EQ(kst_prog_connect(M, RIG_PORT), KST_PROG_MSG_NEED_TARGET);
        CHECK_STR_EQ(said(), TR(KP_NEED_TARGET));
        CHECK_EQ(kst_prog_reboot(M, RIG_PORT), KST_PROG_MSG_NEED_TARGET);
        /* Another port as the target is not this one. */
        g_rig.target = 2u + RIG_PORT;
        settle();
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(g_rig.starts, 1);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_PORT);
        CHECK_STR_EQ(said(), TR(KP_R_PORT));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        /* The port as the target: the rows are live. */
        g_rig.target = 1u + RIG_PORT;
        settle();
        snprintf(want, sizeof(want), TR(KP_STATE_PWM_AS_IS), RIG_PORT + 1u);
        CHECK_STR_EQ(status(0), want);
        CHECK_STR_EQ(status(1), "");
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        /* The target moves while the port is held: nothing is locked. */
        g_rig.target = 0u;
        settle();
        snprintf(want, sizeof(want), TR(KP_STATE_PROGRAMMING), RIG_PORT + 1u);
        CHECK_STR_EQ(status(0), want);
        tap(val(PG_SERVO, 1, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_READ);
        tap(val(PG_SERVO, 0, 1));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_STR_EQ(status(0), TR(KP_NEED_TARGET));
    }
}

TEST_CASE(a_port_without_a_pin_is_refused_by_name)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.no_pin = true;
        link_up();
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(g_rig.starts, 1);
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
        CHECK_STR_EQ(said(), TR(KP_R_NO_PIN));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
        /* REBOOT SERVO finds the same before it looks for a supply. */
        tap(val(PG_SERVO, 0, 0));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_PIN);
        tap(kst_panel_close_rect());
        CHECK(!kst_panel_is_open());
    }
}

TEST_CASE(a_port_without_a_servo_says_so_and_gives_the_port_back)
{
    fresh();
    g_rig.sim.powered = false;
    link_up();
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

TEST_CASE(the_programming_voltage_is_typed_inside_1_to_6_v)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        link_up();
        /* 5.0 V and 1000 mA until they are set. */
        snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 5u, 0u,
                 1000u);
        CHECK_STR_EQ(status(0), want);
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 2);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 5u, 0u);
        CHECK_STR_EQ(kst_panel_choice_label(0), want);
        snprintf(want, sizeof(want), TR(KP_SUPPLY_A), 1000u);
        CHECK_STR_EQ(kst_panel_choice_label(1), want);
        tap(kst_panel_choice_rect(0));
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_PROG_VOLTAGE));
        CHECK_NEAR(kst_panel_keypad()->value, 5.0f, 0.001f);
        CHECK_NEAR(kst_panel_keypad()->min,
                   (float)PDMINI_V_MIN_MV / 1000.0f, 0.001f);
        CHECK_NEAR(kst_panel_keypad()->max,
                   (float)KST_PANEL_STD_MV / 1000.0f, 0.001f);
        /* 6.1 V and 0.9 V cannot be entered. */
        type("6.1");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK(kst_panel_keypad()->refused);
        type("0.9");
        CHECK_EQ(kst_panel_editor(), 1);
        CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
        /* The ends can, and neither asks for a hold. */
        type("6.0");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_supply_mv(M), 6000u);
        more(0);
        type("1.0");
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_supply_mv(M), 1000u);
        /* CANCEL keeps what was set. */
        more(0);
        key(UI_KEY_4);
        key(UI_KEY_CANCEL);
        CHECK_EQ(kst_prog_supply_mv(M), 1000u);
        more(0);
        type("4.8");
        snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 4u, 8u,
                 1000u);
        CHECK_STR_EQ(status(0), want);
        CHECK_EQ(g_rig.sup_sets, 0);
        /* CONNECT sets it, and the line shows what the supply reads. */
        g_rig.load_ma = 37u;
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(g_rig.sup_on);
        CHECK_EQ(g_rig.sup_mv, 4800u);
        CHECK_EQ(g_rig.sup_ma, 1000u);
        snprintf(want, sizeof(want), TR(KP_STATE_PROG_V), RIG_PORT + 1u, 4u,
                 80u, 37u);
        CHECK_STR_EQ(status(0), want);
        /* In programming mode MORE is the servo's list. */
        tap(val(PG_SERVO, 2, 1));
        CHECK_EQ(choices(), 3);
        CHECK(choice(TR(KP_VERIFY)) >= 0);
        tap(kst_panel_choice_cancel_rect());
        CHECK_EQ(kst_prog_supply_mv(M), 4800u);
    }
}

TEST_CASE(a_programming_voltage_above_6_v_needs_hv_servo_and_a_hold)
{
    char want[KST_PANEL_LINE];

    fresh();
    g_rig.rail = true;
    kst_panel_set_hv(true);
    link_up();
    more(0);
    CHECK_NEAR(kst_panel_keypad()->max,
               (float)KST_PORT_SUPPLY_MAX_MV / 1000.0f, 0.001f);
    type("8.5");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    /* 6.1 V is the first that asks. */
    type("6.1");
    CHECK_EQ(kst_panel_editor(), 3);
    snprintf(want, sizeof(want), TR(KP_SUPPLY_V), 6u, 1u);
    CHECK_STR_EQ(kst_panel_warning_line(0), want);
    CHECK(warns(TR(SV_HV_1)));
    CHECK(warns(TR(SV_HV_4)));
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    /* 1.9 s is not a hold; CANCEL sets nothing. */
    hold(1.9f);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    if (kst_panel_editor() == 3) {
        tap(kst_panel_cancel_rect());
    }
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    /* The port's highest, held. */
    more(0);
    type("8.4");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PORT_SUPPLY_MAX_MV);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MAX_MV);
    /* HV SERVO off while the port is held changes nothing on the servo;
     * the next CONNECT asks for 5.0 V again. */
    kst_panel_set_hv(false);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    CHECK_EQ(g_rig.sup_mv, KST_PORT_SUPPLY_MAX_MV);

    /* HV SERVO off closes the keypad and the page that offer more. */
    fresh();
    g_rig.rail = true;
    kst_panel_set_hv(true);
    link_up();
    more(0);
    CHECK_EQ(kst_panel_editor(), 1);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_panel_editor(), 0);
    kst_panel_set_hv(true);
    more(0);
    type("7.4");
    CHECK_EQ(kst_panel_editor(), 3);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    more(0);
    CHECK_NEAR(kst_panel_keypad()->max, (float)KST_PANEL_STD_MV / 1000.0f,
               0.001f);
    type("7.4");
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    /* A value inside the standard range stays when HV SERVO goes. */
    more(0);
    type("5.5");
    kst_panel_set_hv(true);
    kst_panel_set_hv(false);
    CHECK_EQ(kst_prog_supply_mv(M), 5500u);
}

TEST_CASE(the_current_limit_is_typed_inside_50_to_3000_ma)
{
    char want[KST_PANEL_LINE];

    fresh();
    g_rig.rail = true;
    link_up();
    more(1);
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_PROG_CURRENT));
    CHECK_NEAR(kst_panel_keypad()->min, (float)PDMINI_I_MIN_MA, 0.001f);
    CHECK_NEAR(kst_panel_keypad()->max, (float)PDMINI_I_MAX_MA, 0.001f);
    type("49");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    type("3001");
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_EQ(kst_prog_supply_ma(M), KST_PROG_SUPPLY_MA);
    type("50");
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_supply_ma(M), 50u);
    more(1);
    type("3000");
    CHECK_EQ(kst_prog_supply_ma(M), 3000u);
    CHECK_EQ(kst_prog_supply_mv(M), KST_PROG_SUPPLY_MV);
    snprintf(want, sizeof(want), TR(KP_STATE_PWM), RIG_PORT + 1u, 5u, 0u,
             3000u);
    CHECK_STR_EQ(status(0), want);
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(g_rig.sup_ma, 3000u);
    CHECK_EQ(g_rig.sup_mv, KST_PROG_SUPPLY_MV);
}

TEST_CASE(a_supply_that_cannot_give_the_voltage_is_refused_by_name)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        /* A 5 V source: 5.0 V out is 500 mV too much. */
        g_rig.vin_mv = 5000u;
        link_up();
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_SUPPLY_RANGE);
        CHECK_STR_EQ(said(), TR(KP_R_SUPPLY_RANGE));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(g_rig.sup_sets, 0);
        /* 4.5 V is what it can give. */
        more(0);
        type("4.5");
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK_EQ(g_rig.sup_mv, 4500u);
    }
}

TEST_CASE(a_supply_that_does_not_switch_on_ends_the_connect_with_its_line)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        g_rig.rail_deaf = true;
        link_up();
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY);
        snprintf(want, sizeof(want), TR(KP_S_SUPPLY),
                 (unsigned)KST_PORT_SUPPLY_MS);
        CHECK_STR_EQ(said(), want);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK(!kst_panel_holds_port());
    }
}

TEST_CASE(reboot_servo_without_a_supply_to_switch_says_so)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        /* No PD mini, the tester on the port. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        link_up();
        CHECK_STR_EQ(kst_panel_label(PG_SERVO, 0, 0), TR(KP_REBOOT));
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 0), TR(KP_REBOOT_GO));
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_SUPPLY);
        CHECK_STR_EQ(said(), TR(KP_R_NO_SUPPLY));
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        /* The same in programming mode: the session stays. */
        tap(val(PG_SERVO, 0, 1));
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_SUPPLY);
        CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
        CHECK(kst_prog_have_servo(M));
        /* The way out is the operator's word. */
        tap(val(PG_SERVO, 0, 1));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns(TR(KP_LEAVE_UNPLUG_1)));
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_LEFT);

        /* A PD mini that is off has nothing to switch either. */
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_REF + LINK_KST_REF_NO_SUPPLY);
        CHECK_STR_EQ(said(), TR(KP_R_NO_SUPPLY));
        CHECK_EQ(g_rig.sup_sets, 0);
    }
}

TEST_CASE(reboot_servo_counts_the_supply_off_and_the_servo_up)
{
    char want[KST_PANEL_LINE];
    int a, b;

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.rail = true;
        g_rig.sup_on = true;
        g_rig.sup_mv = 5000u;
        link_up();
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        /* The page names both times as the port keeps them. */
        snprintf(want, sizeof(want), TR(KP_LEAVE_RAIL_1),
                 (unsigned)(KST_PORT_RAIL_OFF_MS / 1000u),
                 (unsigned)(KST_PORT_RAIL_WAIT_MS / 1000u));
        CHECK(warns(want));
        CHECK(warns(TR(KP_LEAVE_RAIL_2)));
        /* CANCEL and a short hold switch nothing. */
        hold(1.9f);
        if (kst_panel_editor() == 3) {
            tap(kst_panel_cancel_rect());
        }
        settle();
        CHECK_EQ(g_rig.starts, 0);
        CHECK(g_rig.sup_on);
        tap(val(PG_SERVO, 0, 0));
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        rig_frames(300);
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        CHECK(!g_rig.sup_on);
        /* Off: the line counts down from 2.0 s. */
        a = tenths(TX_KP_STATE_RAIL_OFF);
        CHECK(a > 0 && a <= (int)(KST_PORT_RAIL_OFF_MS / 100u));
        rig_frames(500);
        b = tenths(TX_KP_STATE_RAIL_OFF);
        CHECK(b >= 0 && a - b >= 4 && a - b <= 6);
        /* No row takes a tap while the supply is switched. */
        tap(val(PG_SERVO, 0, 0));
        tap(val(PG_SERVO, 0, 1));
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
        /* On: the servo starts, 1.0 s. */
        rig_frames((unsigned)KST_PORT_RAIL_OFF_MS - 600u);
        CHECK(g_rig.sup_on);
        a = tenths(TX_KP_STATE_RAIL_WAIT);
        CHECK(a > 0 && a <= (int)(KST_PORT_RAIL_WAIT_MS / 100u));
        rig_frames(300);
        b = tenths(TX_KP_STATE_RAIL_WAIT);
        CHECK(b >= 0 && a - b >= 2 && a - b <= 4);
        settle();
        CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
        CHECK_EQ(msg(), KST_PROG_MSG_REBOOTED);
        CHECK_STR_EQ(said(), TR(KP_M_REBOOTED));
        CHECK_EQ(tenths(TX_KP_STATE_RAIL_OFF), -1);
        CHECK_EQ(tenths(TX_KP_STATE_RAIL_WAIT), -1);
        CHECK_EQ(g_rig.starts, 1);
    }
}

TEST_CASE(reboot_servo_in_programming_mode_is_the_way_out)
{
    fresh();
    g_rig.rail = true;
    connected();
    CHECK(g_rig.sup_on);
    tap(val(PG_SERVO, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_LEAVE_1)));
    CHECK(warns(TR(KP_LEAVE_RAIL_2)));
    CHECK(!warns(TR(KP_LEAVE_UNPLUG_1)));
    hold(2.1f);
    rig_frames(300);
    CHECK_EQ(kst_prog_state(M), KST_PROG_LEAVING);
    CHECK(tenths(TX_KP_STATE_RAIL_OFF) > 0);
    CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 1), TR(KP_LEAVE));
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_LEFT);
    CHECK_STR_EQ(said(), TR(KP_M_LEFT));
    CHECK(g_rig.sup_on);
    CHECK(!kst_prog_have_servo(M));
    CHECK(!kst_panel_holds_port());
}

TEST_CASE(stop_and_a_lost_link_end_the_supply_cycle_with_their_lines)
{
    /* STOP while the supply is off. */
    fresh();
    g_rig.rail = true;
    g_rig.sup_on = true;
    link_up();
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    rig_frames(500);
    CHECK(tenths(TX_KP_STATE_RAIL_OFF) > 0);
    rig_stop(true);
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PWM);
    CHECK_EQ(msg(), KST_PROG_MSG_SES + KST_SES_ERR_ABORTED);
    CHECK_STR_EQ(said(), TR(KP_S_ABORTED));
    CHECK_EQ(tenths(TX_KP_STATE_RAIL_OFF), -1);
    /* Stopped, the row says why it does nothing. */
    tap(val(PG_SERVO, 0, 0));
    if (kst_panel_editor() == 3) {
        hold(2.1f);
    }
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);

    /* The link goes while the servo starts. */
    fresh();
    g_rig.rail = true;
    g_rig.sup_on = true;
    link_up();
    tap(val(PG_SERVO, 0, 0));
    hold(2.1f);
    rig_frames((unsigned)KST_PORT_RAIL_OFF_MS + 400u);
    CHECK(tenths(TX_KP_STATE_RAIL_WAIT) > 0);
    rig_link_lost();
    CHECK_EQ(kst_prog_state(M), KST_PROG_DOWN);
    CHECK_STR_EQ(status(0), TR(KP_ST_DOWN));
    CHECK_EQ(kst_panel_editor(), 0);
    /* No row asks anything of a link that is down. */
    tap(val(PG_SERVO, 0, 0));
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(a_count_of_1_is_said_in_the_singular)
{
    char want[KST_PANEL_LINE];

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        enable();
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
        show();
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 4, 0), TR(KP_WRITE_ONE));
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_STR_EQ(said(), TR(KP_M_WRITTEN_ONE));
        /* Two are a count. */
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 1u));
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
        show();
        snprintf(want, sizeof(want), TR(KP_WRITE_N), 2u);
        CHECK_STR_EQ(kst_panel_button(PG_SERVO, 4, 0), want);
    }
}

TEST_CASE(connect_reads_the_servo_and_keeps_the_overlay_while_the_port_is_held)
{
    fresh();
    connected();
    /* Two requests: into the mode, then the read. */
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(kst_prog_port(M), RIG_PORT);
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK_STR_EQ(said(), TR(KP_M_READ));
    CHECK(kst_panel_holds_port());
    CHECK(!kst_prog_write_enabled(M));
    CHECK_STR_EQ(kst_panel_button(PG_SERVO, 0, 1), TR(KP_LEAVE));
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
    CHECK(warns(TR(KP_WRITE_BACKUP)));
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
    CHECK(warns(TR(KP_WRITE_BACKUP)));
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
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_READ);
    rig_frames(5);
    CHECK_EQ(g_rig.fc.lists, 0);
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

/* ---------------------------------------------------------- the servo files */

/* The index of a character key of the text keyboard. */
static int tk_key(char ch)
{
    static const char chars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
    const char *p = strchr(chars, ch);

    return p != NULL ? (int)(p - chars) : -1;
}

static void tkey(int key)
{
    tap(ui_textkey_key_rect(kst_panel_textkey(), key));
}

/* Clear the name, type @p s and OK. */
static void type_name(const char *s)
{
    tkey(UI_TK_CLR);
    for (; *s != '\0'; ++s) {
        tkey(tk_key(*s));
    }
    tkey(UI_TK_OK);
}

/* FILES on the SERVO tab, and what the card answers. */
static void files(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 1, 1));
    settle();
}

/* The list's entry of the file @p name, -1 for none. */
static int file_choice(const char *name)
{
    const size_t len = strlen(name);

    for (int i = 0; kst_panel_choice_label(i) != NULL; ++i) {
        const char *l = kst_panel_choice_label(i);

        if (strncmp(l, name, len) == 0 && (l[len] == ' ' || l[len] == '\0')) {
            return i;
        }
    }
    return -1;
}

/* FILES, the file @p name, and what the card answers. */
static void pick(const char *name)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK(file_choice(name) > 0);
    tap(kst_panel_choice_rect(file_choice(name)));
    settle();
}

/* FILES, the entry that saves: the keyboard is open on the preset name. */
static void save_entry(void)
{
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choice(TR(KP_FILE_SAVE)), 0);
    tap(kst_panel_choice_rect(0));
    CHECK_EQ(kst_panel_editor(), 4);
}

/* A file whose image is the bench servo's with @p id at @p raw. */
static fake_file_t *file_with(const char *name, kst_field_id_t id,
                              uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, id, raw));
    return fake_card_put(&g_rig.fc, name, &img);
}

/* The bench servo's image as the other servo type. */
static kst_image_t other_type(void)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    return img;
}

/* The lines of the message shown. */
static int lines(char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE])
{
    return kst_panel_message_lines(kst_prog_message(M), t);
}

TEST_CASE(files_lists_the_card_under_the_entry_that_saves)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 1), TR(KP_FILES));
    /* An empty card: the entry that saves, and the message. */
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 1);
    CHECK_EQ(choice(TR(KP_FILE_SAVE)), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NO_FILES);
    CHECK_STR_EQ(said(), TR(KP_M_NO_FILES));
    CHECK_STR_EQ(kst_panel_choice_title(), TR(KP_FILES_TITLE));
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);

    /* The names as stored, each with its servo type. */
    (void)fake_card_put(&g_rig.fc, "KST001", &sv);
    (void)fake_card_put(&g_rig.fc, "WING", &ot);
    fake_card_put(&g_rig.fc, "HAND", &sv)->type = 0;
    files();
    CHECK_EQ(choices(), 4);
    snprintf(want, sizeof(want), "%-16sV%u", "KST001", kst_card_type(&sv));
    CHECK_STR_EQ(kst_panel_choice_label(1), want);
    snprintf(want, sizeof(want), "%-16sV%u", "WING", kst_card_type(&ot));
    CHECK_STR_EQ(kst_panel_choice_label(2), want);
    CHECK_EQ(kst_card_type(&sv) + kst_card_type(&ot), 14);
    /* A file that names no type shows none. */
    CHECK_STR_EQ(kst_panel_choice_label(3), "HAND");
    tap(kst_panel_choice_cancel_rect());
    rig_frames(20);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(more_files_than_the_list_holds_are_shown_in_turn)
{
    const kst_image_t sv = sim_bench_image();
    char title[64];

    fresh();
    connected();
    for (unsigned i = 0; i < 7u; ++i) {
        char name[KST_PROG_NAME_MAX];

        snprintf(name, sizeof(name), "F%u", i);
        (void)fake_card_put(&g_rig.fc, name, &sv);
    }
    files();
    /* The entry that saves, five files, the entry for the next. */
    CHECK_EQ(choices(), 7);
    CHECK_EQ(file_choice("F0"), 1);
    CHECK_EQ(file_choice("F4"), 5);
    CHECK_EQ(choice(TR(KP_FILES_NEXT)), 6);
    /* The last entry ends above CANCEL. */
    CHECK(kst_panel_choice_rect(6).y + kst_panel_choice_rect(6).h
          <= kst_panel_choice_cancel_rect().y);
    tap(kst_panel_choice_rect(6));
    settle();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 4);
    CHECK_EQ(file_choice("F5"), 1);
    CHECK_EQ(file_choice("F6"), 2);
    snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), 6u, 7u, 7u);
    CHECK_STR_EQ(kst_panel_choice_title(), title);
    /* Behind the last file the list starts again. */
    tap(kst_panel_choice_rect(3));
    settle();
    CHECK_EQ(choices(), 7);
    CHECK_EQ(file_choice("F0"), 1);
    snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), 1u, 5u, 7u);
    CHECK_STR_EQ(kst_panel_choice_title(), title);
    /* A file of the second page loads. */
    g_rig.fc.file[6].img.r[KST_REG_DUTY]--;
    g_rig.fc.file[6].img.r[KST_REG_DUTY_COPY]--;
    tap(kst_panel_choice_rect(6));
    settle();
    tap(kst_panel_choice_rect(file_choice("F6")));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_changes(M), 1);
}

TEST_CASE(the_list_waits_for_the_card_and_opens_once)
{
    fresh();
    connected();
    g_rig.fc.silent = true;
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    tick(0.02f);
    show();
    CHECK(kst_prog_card_busy(M));
    CHECK_EQ(kst_panel_editor(), 0);
    /* A second tap asks for no second list. */
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.lists, 1);
    /* A card that does not answer is given up. */
    rig_frames(KST_PROG_CARD_MS + 10u);
    tick(0.02f);
    show();
    CHECK(!kst_prog_card_busy(M));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_STR_EQ(said(), TR(KP_M_CARD_READ));
    CHECK_EQ(kst_panel_editor(), 0);
    /* A list asked for as the link goes opens nothing. */
    g_rig.fc.silent = false;
    tap(val(PG_SERVO, 1, 1));
    kst_link_lost(&g_rig.link);
    settle();
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(a_save_asks_for_the_name_with_the_next_free_one_preset)
{
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    save_entry();
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_TITLE));
    CHECK_STR_EQ(kst_panel_textkey()->text, "KST001");
    tkey(UI_TK_OK);
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    snprintf(want, sizeof(want), TR(KP_M_SAVED), "KST001");
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(g_rig.fc.n, 1);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");

    /* The next preset, and a name typed over it. */
    save_entry();
    CHECK_STR_EQ(kst_panel_textkey()->text, "KST002");
    type_name("WING-L_1");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    snprintf(want, sizeof(want), TR(KP_M_SAVED), "WING-L_1");
    CHECK_STR_EQ(said(), want);
    CHECK_STR_EQ(g_rig.fc.file[1].name, "WING-L_1");

    /* The keyboard takes as many characters as a name has. */
    save_entry();
    tkey(UI_TK_CLR);
    for (unsigned i = 0; i < kst_card_name_max() + 3u; ++i) {
        tkey(tk_key('A'));
    }
    CHECK_EQ(strlen(kst_panel_textkey()->text), kst_card_name_max());
    /* CANCEL saves nothing. */
    tkey(UI_TK_CANCEL);
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(5);
    CHECK_EQ(g_rig.fc.n, 2);
    CHECK_EQ(g_rig.fc.saves, 2);
    /* Nothing of this reached the servo. */
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_name_the_card_refuses_comes_back_to_the_keyboard)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    (void)fake_card_put(&g_rig.fc, "WING", &sv);
    /* A name a file has: no file is replaced, the name is changed. */
    save_entry();
    type_name("WING");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    CHECK_STR_EQ(said(), TR(KP_M_NAME_EXISTS));
    CHECK_EQ(kst_panel_editor(), 4);
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_EXISTS));
    CHECK_STR_EQ(kst_panel_textkey()->text, "WING");
    CHECK_EQ(g_rig.fc.n, 1);
    tkey(tk_key('2'));
    tkey(UI_TK_OK);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_STR_EQ(g_rig.fc.file[1].name, "WING2");

    /* A name with a character no file takes. */
    save_entry();
    type_name("A.B");
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    CHECK_STR_EQ(said(), TR(KP_M_NAME_BAD));
    CHECK_EQ(kst_panel_editor(), 4);
    CHECK_STR_EQ(kst_panel_textkey()->title, TR(KP_NAME_BAD));
    CHECK_STR_EQ(kst_panel_textkey()->text, "A.B");
    tkey(UI_TK_CANCEL);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.n, 2);
}

TEST_CASE(every_failure_of_the_card_has_its_line)
{
    static const struct {
        uint8_t result;
        unsigned msg;
        ui_text_id_t text;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE, TX_KP_M_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL, TX_KP_M_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE,
          TX_KP_M_CARD_WRITE },
    };
    const kst_image_t sv = sim_bench_image();

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        /* A save. */
        for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            g_rig.fc.fails = KST_PROG_CARD_OK;
            save_entry();
            g_rig.fc.fails = k[i].result;
            tkey(UI_TK_OK);
            settle();
            CHECK_EQ(msg(), k[i].msg);
            CHECK_STR_EQ(said(), ui_tr(k[i].text));
            CHECK_EQ(kst_panel_editor(), 0);
            CHECK(!kst_prog_backed_up(M));
        }
        /* The list. */
        g_rig.fc.fails = KST_PROG_CARD_READ_ERR;
        files();
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
        CHECK_STR_EQ(said(), TR(KP_M_CARD_READ));
        CHECK_EQ(kst_panel_editor(), 0);
        /* A file that went between the list and the tap. */
        g_rig.fc.fails = KST_PROG_CARD_OK;
        (void)fake_card_put(&g_rig.fc, "GONE", &sv);
        files();
        CHECK_EQ(kst_panel_editor(), 2);
        g_rig.fc.n = 0;
        tap(kst_panel_choice_rect(1));
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
        /* No card in the slot. */
        g_rig.card = false;
        rig_frame();
        tap(val(PG_SERVO, 1, 1));
        CHECK_EQ(msg(), KST_PROG_MSG_CARD_NONE);
        CHECK_STR_EQ(said(), TR(KP_M_CARD_NONE));
        CHECK_EQ(kst_panel_editor(), 0);
        CHECK(!kst_prog_card_busy(M));
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_becomes_the_pending_values_and_the_ordinary_write_sends_them)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];

    fresh();
    connected();
    (void)file_with("LOW", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 1u));
    pick("LOW");
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(lines(t), 2);
    snprintf(want, sizeof(want), TR(KP_M_LOADED_ONE), "LOW");
    CHECK_STR_EQ(t[0], want);
    CHECK_STR_EQ(t[1], TR(KP_EDIT_NOTE_2));
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY) - 1u);
    CHECK_EQ(kst_prog_changed(M), KST_FIELD_BIT(KST_F_DUTY));
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    /* The values are written as edits are: the switch, the page, the
     * hold. */
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);
    enable();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_HORN_FREE)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo(KST_F_DUTY), draft(KST_F_DUTY));
    CHECK_EQ(servo(KST_F_DUTY), sim_bench_image().r[KST_REG_DUTY] - 1u);
    CHECK_EQ(g_rig.sim.n_bad_writes, 0);
}

TEST_CASE(an_edited_file_is_named_as_one_and_its_locked_values_are_counted)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];
    fake_file_t *f;

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        kst_image_t ot = other_type();

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        f = file_with("ED", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 1u));
        f->kind = KST_PROG_FILE_EDITED;
        pick("ED");
        CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[1], TR(KP_FILE_EDITED));

        /* No type and another 20k value: loaded, the type stays the
         * servo's and is counted as locked, not written. */
        CHECK(kst_field_edit(&ot, KST_F_DUTY,
                             (uint16_t)(servo(KST_F_DUTY) - 2u)));
        f = fake_card_put(&g_rig.fc, "UN", &ot);
        f->kind = KST_PROG_FILE_UNTYPED;
        f->type = 0;
        pick("UN");
        CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
        CHECK_EQ(lines(t), 3);
        snprintf(want, sizeof(want), TR(KP_M_LOADED_ONE), "UN");
        CHECK_STR_EQ(t[0], want);
        CHECK_STR_EQ(t[1], TR(KP_FILE_UNTYPED));
        snprintf(want, sizeof(want), "%s", TR(KP_FILE_STAY_ONE));
        CHECK_STR_EQ(t[2], want);
        CHECK_EQ(kst_prog_load_skipped(M), KST_FIELD_BIT(KST_F_20K_SEL));
        CHECK_EQ(draft(KST_F_20K_SEL), servo(KST_F_20K_SEL));
        CHECK_EQ(kst_prog_changed(M), KST_FIELD_BIT(KST_F_DUTY));
        /* The LOCKED tab draws with it. */
        tap(kst_panel_tab_rect(PG_LOCKED));
        show();
        tap(kst_panel_tab_rect(PG_SERVO));
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The servo is of the other type when @p servo_other, the file of the type
 * that is not the servo's. */
static void type_mismatch(bool servo_other, ui_lang_t lang)
{
    char want[KST_PANEL_LINE];
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    const unsigned t_file = kst_card_type(servo_other ? &sv : &ot);
    const unsigned t_servo = kst_card_type(servo_other ? &ot : &sv);

    fresh();
    ui_text_set_language(lang);
    if (servo_other) {
        memcpy(g_rig.sim.reg, ot.r, sizeof(ot.r));
    }
    connected();
    (void)fake_card_put(&g_rig.fc, "X", servo_other ? &sv : &ot);
    pick("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    snprintf(want, sizeof(want), TR(KP_M_LOAD_TYPE), t_file, t_servo);
    CHECK_STR_EQ(said(), want);
    CHECK_EQ(t_file + t_servo, 14);
    if (lang == UI_LANG_EN) {
        CHECK_STR_EQ(said(), t_file == 6u
                                 ? "File is V6, servo is V8. Not loaded."
                                 : "File is V8, servo is V6. Not loaded.");
    } else {
        CHECK(strstr(said(), t_file == 6u ? "V6" : "V8") != NULL);
        CHECK(strstr(said(), t_servo == 6u ? "V6" : "V8") != NULL);
        CHECK(strcmp(TR(KP_M_LOAD_TYPE),
                     ui_tr_in(UI_LANG_EN, TX_KP_M_LOAD_TYPE)) != 0);
    }
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_of_the_other_servo_type_is_denied_naming_both)
{
    type_mismatch(false, UI_LANG_EN);
    type_mismatch(true, UI_LANG_EN);
    type_mismatch(false, UI_LANG_DE);
    type_mismatch(true, UI_LANG_DE);
}

TEST_CASE(the_servo_type_is_shown_once_the_servo_is_read)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    char want[24];

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        for (int other = 0; other < 2; ++other) {
            const unsigned type = kst_card_type(other != 0 ? &ot : &sv);

            fresh();
            ui_text_set_language((ui_lang_t)lang);
            if (other != 0) {
                memcpy(g_rig.sim.reg, ot.r, sizeof(ot.r));
            }
            link_up();
            /* Nothing read: no type. */
            CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 0), "SERVO");
            tap(val(PG_SERVO, 0, 1));
            settle();
            snprintf(want, sizeof(want), TR(KP_SERVO_V), type);
            CHECK_STR_EQ(kst_panel_label(PG_SERVO, 1, 0), want);
            CHECK(strstr(want, type == 6u ? "V6" : "V8") != NULL);
            /* The 20k field is the type, and takes no tap. */
            tap(kst_panel_tab_rect(PG_LOCKED));
            CHECK_STR_EQ(kst_panel_label(PG_LOCKED, 0, 0), TR(KP_TYPE));
            tap(val(PG_LOCKED, 0, 0));
            CHECK_EQ(kst_panel_editor(), 0);
            show();
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_that_does_not_read_says_why)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];
    fake_file_t *f;

    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        f = file_with("X", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 1u));
        f->result = KST_PROG_CARD_BAD;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
        CHECK_EQ(lines(t), 1);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_BAD));
        f->line = 7;
        pick("X");
        snprintf(want, sizeof(want), TR(KP_M_LOAD_LINE), 7u);
        CHECK_STR_EQ(said(), want);
        /* A wrong checksum: damaged, and how it loads all the same. */
        f->result = KST_PROG_CARD_CHECKSUM;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_CHECKSUM);
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_CHECKSUM));
        CHECK_STR_EQ(t[1], TR(KP_M_LOAD_CHECKSUM_2));
        f->result = KST_PROG_CARD_NEWER;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_NEWER);
        CHECK_STR_EQ(said(), TR(KP_M_LOAD_NEWER));
        /* A value past a hard limit: its line and its field. */
        f->result = KST_PROG_CARD_VALUE;
        f->field = KST_F_BOOST;
        f->line = 5;
        pick("X");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_VALUE);
        snprintf(want, sizeof(want), TR(KP_M_LOAD_VALUE), 5u,
                 kst_panel_label(PG_TUNE, 1, 1));
        CHECK_EQ(lines(t), 1);
        CHECK_STR_EQ(t[0], want);
        CHECK(strstr(t[0], "BOOST") != NULL);
        f->field = 0xFFu;
        pick("X");
        snprintf(want, sizeof(want), TR(KP_M_LOAD_VALUE_ANY), 5u);
        CHECK_STR_EQ(said(), want);
        CHECK_EQ(kst_prog_changes(M), 0);
        CHECK_EQ(kst_panel_editor(), 0);

        /* A value the hard limits of the programmer refuse: the rule's
         * line. */
        (void)file_with("HARD", KST_F_DUTY, 0xFFu);
        pick("HARD");
        CHECK_EQ(msg(), KST_PROG_MSG_LOAD_REFUSED);
        CHECK_EQ(lines(t), 2);
        CHECK_STR_EQ(t[0], TR(KP_M_LOAD_REFUSED));
        CHECK_STR_EQ(t[1], kst_panel_rule_text(KST_H_DUTY_HIGH));
        CHECK_EQ(kst_prog_changes(M), 0);
        rig_frames(50);
        CHECK_EQ(g_rig.sim.n_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_file_past_a_soft_limit_waits_for_a_2_s_hold)
{
    char want[KST_PANEL_LINE];
    fake_file_t *f;

    fresh();
    connected();
    f = file_with("UP", KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 1u));
    f->kind = KST_PROG_FILE_EDITED;
    pick("UP");
    /* The page of a soft limit, with the file's name and kind. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    snprintf(want, sizeof(want), TR(KP_FILE_DIFF_ONE), "UP");
    CHECK_STR_EQ(kst_panel_warning_line(0), want);
    CHECK(warns(TR(KP_FILE_EDITED)));
    CHECK(warns(kst_panel_rule_text(KST_S_DUTY_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* Short of 2 s nothing is taken. */
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* CANCEL leaves the draft as it was. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_soft_is_load(M));
    CHECK_EQ(kst_prog_changes(M), 0);

    pick("UP");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY) + 1u);
    /* Pending, not written: the write still needs its switch. */
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_ENABLE);

    /* The page goes with the link, and the file with it. */
    kst_prog_discard(M);
    pick("UP");
    CHECK_EQ(kst_panel_editor(), 3);
    rig_link_lost();
    tick(0.02f);
    show();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(!kst_prog_soft_is_load(M));
}

/* Values the bench servo's are above, far from, or other than. */
static void lower_many(kst_image_t *img)
{
    const uint16_t duty = (uint16_t)(kst_field_get(img, KST_F_DUTY) - 10u);

    CHECK(kst_field_set(img, KST_F_DUTY, duty));
    CHECK(kst_field_set(img, KST_F_DUTY_COPY, duty));
    CHECK(kst_field_set(img, KST_F_BOOST,
                        (uint16_t)(kst_field_get(img, KST_F_BOOST) - 4u)));
    CHECK(kst_field_set(img, KST_F_DEAD_BAND,
                        (uint16_t)(kst_field_get(img, KST_F_DEAD_BAND) + 4u)));
    CHECK(kst_field_set(img, KST_F_LEFT_ANGLE,
                        (uint16_t)(kst_field_get(img, KST_F_LEFT_ANGLE) - 5u)));
    CHECK(kst_field_set(img, KST_F_REVERSION,
                        1u - kst_field_get(img, KST_F_REVERSION)));
    CHECK(kst_field_set(img, KST_F_NEUTRAL,
                        (uint16_t)(kst_field_get(img, KST_F_NEUTRAL) + 300u)));
    CHECK(kst_field_set(img, KST_F_PULSE_LOWER,
                        (uint16_t)(kst_field_get(img, KST_F_PULSE_LOWER)
                                   + 1200u)));
}

TEST_CASE(a_file_with_more_warnings_than_lines_counts_the_rest)
{
    char more[KST_PANEL_LINE];
    const kst_image_t sv = sim_bench_image();
    kst_image_t img = sv;
    kst_rules_t soft;
    unsigned texts = 0, n = 0;
    int16_t seen[KST_RULE_COUNT];

    fresh();
    lower_many(&img);
    memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    connected();
    (void)fake_card_put(&g_rig.fc, "MANY", &sv);
    pick("MANY");
    CHECK_EQ(kst_panel_editor(), 3);
    soft = kst_prog_soft_rules(M);
    /* The texts the rules have between them, two rules to a text where
     * the text is the same. */
    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        bool twice = false;

        if ((soft & KST_RULE_BIT(rule)) == 0u) {
            continue;
        }
        for (unsigned i = 0; i < texts; ++i) {
            twice = twice || strcmp(kst_panel_rule_text((unsigned)seen[i]),
                                    kst_panel_rule_text(rule)) == 0;
        }
        if (!twice) {
            seen[texts++] = (int16_t)rule;
        }
    }
    CHECK(texts > 6u);
    while (kst_panel_warning_line((int)n) != NULL) {
        ++n;
    }
    /* The file, five warnings and the count of the rest. */
    CHECK_EQ(n, 7);
    snprintf(more, sizeof(more), TR(KP_CHECK_MORE), texts - 5u);
    CHECK_STR_EQ(kst_panel_warning_line(6), more);
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK(warns(kst_panel_rule_text((unsigned)seen[i])));
    }
    CHECK(!warns(kst_panel_rule_text((unsigned)seen[5])));
    /* CANCEL leaves the servo and the draft as they are. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_file_that_holds_the_servos_values_changes_nothing)
{
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char want[KST_PANEL_LINE];
    const kst_image_t sv = sim_bench_image();
    kst_image_t pair = sv;

    fresh();
    connected();
    (void)fake_card_put(&g_rig.fc, "SAME", &sv);
    pick("SAME");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(lines(t), 1);
    snprintf(want, sizeof(want), TR(KP_M_LOAD_SAME), "SAME");
    CHECK_STR_EQ(t[0], want);
    /* Only a locked value differs: the pairing value. */
    pair.r[KST_REG_NODE_ADDR] ^= 0x22u;
    (void)fake_card_put(&g_rig.fc, "PAIR", &pair);
    pick("PAIR");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(lines(t), 2);
    snprintf(want, sizeof(want), "%s", TR(KP_FILE_STAY_ONE));
    CHECK_STR_EQ(t[1], want);
    CHECK_EQ(kst_prog_changes(M), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_load_waits_for_a_servo_that_is_read)
{
    const kst_image_t sv = sim_bench_image();

    /* No servo connected: FILES takes no tap. */
    fresh();
    link_up();
    (void)fake_card_put(&g_rig.fc, "F", &sv);
    tap(val(PG_SERVO, 1, 1));
    rig_frames(5);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(g_rig.fc.lists, 0);

    /* A servo that fails the fingerprint: listed, never loaded onto. */
    fresh();
    g_rig.sim.reg[0x06] = 0x05u;
    link_up();
    tap(val(PG_SERVO, 0, 1));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    (void)fake_card_put(&g_rig.fc, "F", &sv);
    pick("F");
    CHECK_EQ(msg(), KST_PROG_MSG_READ_ONLY);
    CHECK_EQ(g_rig.fc.loads, 0);
    CHECK_EQ(kst_prog_changes(M), 0);
}

/* ------------------------------------------- the file before the first write */

TEST_CASE(the_first_write_saves_the_servo_to_a_file_without_asking)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    hold(2.1f);
    /* No name is asked for. */
    CHECK_EQ(kst_panel_editor(), 0);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK_EQ(g_rig.starts_at_save, 2);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    CHECK(memcmp(g_rig.fc.file[0].img.r, sv.r, sizeof(sv.r)) == 0);
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
    show();
    /* The second write of the session takes no file. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns(TR(KP_WRITE_BACKUP)));
    CHECK(warns(TR(KP_WRITE_CARD)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
}

TEST_CASE(a_first_write_whose_file_fails_is_not_sent)
{
    static const struct {
        uint8_t result;
        unsigned msg;
        ui_text_id_t text;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE, TX_KP_M_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL, TX_KP_M_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE,
          TX_KP_M_CARD_WRITE },
    };
    const unsigned starts = 2u;

    fresh();
    connected();
    enable();
    edits();
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_rig.fc.fails = k[i].result;
        if (!kst_prog_write_enabled(M)) {
            enable();
        }
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_STR_EQ(said(), ui_tr(k[i].text));
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK_EQ(g_rig.starts, starts);
        CHECK_EQ(kst_prog_changes(M), 3);
        CHECK(!kst_prog_journal(M));
    }
    /* The card takes the file: the same write goes. */
    g_rig.fc.fails = KST_PROG_CARD_OK;
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.n, 1);
}

/* ------------------------------------------------------------- with no card */

/* The three lines of a write that goes without a file. */
static bool warns_no_file(void)
{
    return warns(TR(KP_NO_FILE_1)) && warns(TR(KP_NO_FILE_2))
        && warns(TR(KP_NO_FILE_3));
}

static void card_out(void)
{
    g_rig.card = false;
    rig_frame();
}

static void card_in(void)
{
    g_rig.card = true;
    rig_frame();
}

TEST_CASE(a_first_write_with_no_card_is_sent_after_its_page_says_so)
{
    for (int lang = UI_LANG_EN; lang <= UI_LANG_DE; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        card_out();
        enable();
        edits();
        /* A value past a soft limit, held for at the edit. */
        edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) + 2u));
        tap(val(PG_SERVO, 4, 0));
        /* The page opens; nothing is refused. */
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(msg() != KST_PROG_MSG_CARD_NONE);
        CHECK(warns_no_file());
        CHECK(warns(TR(KP_HORN_FREE)));
        CHECK(warns(TR(KP_WRITE_2)));
        CHECK(!warns(TR(KP_WRITE_BACKUP)));
        show();
        /* The one hold of the page, let go early: nothing is sent. */
        hold(1.9f);
        CHECK_EQ(kst_panel_editor(), 3);
        rig_frames(50);
        CHECK_EQ(g_rig.starts, 2);
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK(!kst_prog_no_file(M));
        CHECK_EQ(kst_prog_changes(M), 4);
        /* Held: the write goes, and no second hold is asked for. */
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK_EQ(kst_prog_changes(M), 0);
        CHECK_EQ(g_rig.fc.saves, 0);
        CHECK_EQ(g_rig.fc.n, 0);
        CHECK(kst_prog_no_file(M));
        CHECK(!kst_prog_backed_up(M));
        /* The SERVO tab's line. */
        tap(kst_panel_tab_rect(PG_SERVO));
        show();
        ui_text_set_language(UI_LANG_EN);
    }
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_1), TR(KP_NO_FILE_1))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_2), TR(KP_NO_FILE_2))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_FILE_3), TR(KP_NO_FILE_3))
          != 0);
    CHECK(strcmp(ui_tr_in(UI_LANG_DE, TX_KP_NO_BACKUP_FILE),
                 TR(KP_NO_BACKUP_FILE)) != 0);
}

TEST_CASE(a_card_put_in_between_two_writes_takes_the_file_at_the_second)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    card_out();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns_no_file());
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_no_file(M));

    card_in();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns_no_file());
    CHECK(!warns(TR(KP_NO_FILE_1)));
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK_STR_EQ(g_rig.fc.file[0].name, "KST001");
    /* The servo as the session found it, not as the first write left it. */
    CHECK(memcmp(g_rig.fc.file[0].img.r, sv.r, sizeof(sv.r)) == 0);
    CHECK(!kst_prog_no_file(M));
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
    show();
}

TEST_CASE(a_card_pulled_under_the_write_page_brings_the_page_back)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_WRITE_BACKUP)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    /* The page promised a file; the card leaves before the hold ends. */
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns_no_file());
    CHECK(!warns(TR(KP_WRITE_BACKUP)));
    rig_frames(50);
    CHECK_EQ(g_rig.starts, 2);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(g_rig.fc.saves, 0);
    /* CANCEL sends nothing either. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    /* The page's own hold does. */
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns_no_file());
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));
}

TEST_CASE(a_card_pulled_after_the_backup_file_leaves_the_next_write_alone)
{
    fresh();
    connected();
    enable();
    edits();
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);

    card_out();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    /* The file is saved: only the record of the write stays in memory. */
    CHECK(!warns(TR(KP_NO_FILE_1)));
    CHECK(warns(TR(KP_WRITE_NO_CARD)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_rig.fc.saves, 1);
    CHECK(!kst_prog_no_file(M));
    CHECK_STR_EQ(kst_prog_backup_name(M), "KST001");
}

TEST_CASE(a_restore_with_no_card_shows_its_warnings_and_the_missing_file)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    fresh();
    connected();
    card_out();
    enable();
    /* Max. Duty lowered: the session's backup raises it past a soft
     * limit. */
    edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 2u));
    tap(val(PG_SERVO, 4, 0));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    /* The soft limit's page first, as with a card. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_CHECK_1)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    hold(2.1f);
    /* Then the restore's, with the lines of the missing file. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(warns_no_file());
    show();
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    rig_frames(50);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) == 0);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));
}

TEST_CASE(a_restore_and_a_release_whose_card_is_pulled_come_back_too)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t now;

    /* RELEASE PAIRING as the session's first write. */
    fresh();
    connected();
    enable();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_RELEASE_1)));
    CHECK(warns_no_file());
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    show();
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_rig.fc.saves, 0);
    CHECK(kst_prog_no_file(M));

    /* The session's restore after a write that had no card, the card
     * back in for its page and out again under the hold. */
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    card_in();
    tap(val(PG_SERVO, 2, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(!warns(TR(KP_NO_FILE_1)));
    card_out();
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_FROM_SESSION)));
    CHECK(warns_no_file());
    rig_frames(50);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) != 0);
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = rig_servo();
    CHECK(memcmp(now.r, sv.r, sizeof(now.r)) == 0);
}

static void other_20k(kst_image_t *img)
{
    CHECK(kst_field_set(img, KST_F_20K_SEL,
                        1u - kst_field_get(img, KST_F_20K_SEL)));
}

TEST_CASE(a_servo_that_is_not_the_one_of_the_interrupted_write_is_named)
{
    char file[KST_PROG_FILE_MAX];
    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    char second[KST_PANEL_LINE];
    size_t len;
    kst_image_t img;

    fresh();
    g_rig.card = true;
    cut_write();
    len = g_rig.file_len;
    memcpy(file, g_rig.file, len + 1u);

    /* A panel that starts later: CHANNEL stands on the write's output,
     * and the message names it. */
    fresh();
    g_rig.card = true;
    CHECK(kst_panel_file_load(file, len));
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL);
    CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
    snprintf(second, sizeof(second), TR(KP_M_JOURNAL_2), (unsigned)RIG_PORT + 1u);
    CHECK_STR_EQ(t[1], second);
    img = rig_servo();
    other_20k(&img);
    memcpy(g_rig.sim.reg, img.r, sizeof(img.r));
    link_up();
    tap(val(PG_SERVO, 0, 1));           /* CONNECT */
    settle();
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    CHECK_EQ(kst_prog_port(M), RIG_PORT);

    /* Another servo is on it. */
    CHECK_EQ(msg(), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_STR_EQ(said(), TR(KP_M_JOURNAL_OTHER));
    CHECK_EQ(kst_panel_message_lines(kst_prog_message(M), t), 2);
    snprintf(second, sizeof(second), TR(KP_M_JOURNAL_OTHER_2),
             (unsigned)RIG_PORT + 1u);
    CHECK_STR_EQ(t[1], second);
    show();
    /* Nothing is edited, and the switch does not open a write. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    tap(kst_panel_tab_rect(PG_SERVO));
    /* RESTORE holds the one entry that discards the record. */
    tap(val(PG_SERVO, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 1);
    CHECK(choice(TR(KP_FORGET)) == 0);
    tap(kst_panel_choice_rect(0));
    CHECK_EQ(msg(), KST_PROG_MSG_FORGOTTEN);
    CHECK(!M->journal);
    CHECK(kst_prog_editable(M));
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(a_file_that_is_not_readable_is_said_and_not_used)
{
    fresh();
    CHECK(!kst_panel_file_load("KST 1\n00=zz\n", 12));
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
    save_entry();
    tkey(UI_TK_OK);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(g_rig.fc.n, 1);
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
    CHECK(family_ok(1u, (unsigned)KST_PROG_MSG_REBOOTED));
    CHECK(family_ok(KST_PROG_MSG_START, (unsigned)KST_LINK_START_NO_IMAGE + 1u));
    CHECK(family_ok(KST_PROG_MSG_OUT, (unsigned)KST_LINK_OUT_DROPPED + 1u));
    CHECK(family_ok(KST_PROG_MSG_REF, (unsigned)LINK_KST_REF_PORT + 1u));
    CHECK(family_ok(KST_PROG_MSG_REF + LINK_KST_REF_UNSAFE,
                    (unsigned)LINK_KST_REF_NO_SUPPLY + 1u
                        - (unsigned)LINK_KST_REF_UNSAFE));
    /* 3 and 4 are refusals no port gives: they are a line all the same. */
    for (unsigned gap = 3u; gap < (unsigned)LINK_KST_REF_UNSAFE; ++gap) {
        m.id = (uint16_t)(KST_PROG_MSG_REF + gap);
        CHECK_EQ(kst_panel_message_lines(&m, t), 1);
        CHECK(t[0][0] != '\0');
    }
    CHECK(family_ok(KST_PROG_MSG_SES + LINK_KST_RESULT_SUPPLY, 1u));
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
    m.id = 0x3Fu;
    m.arg = 0;
    CHECK_EQ(kst_panel_message_lines(&m, t), 1);
    CHECK(strstr(t[0], "63") != NULL);
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
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
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
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
}

TEST_CASE(the_programmer_stays_on_the_screen_while_the_port_is_held)
{
    servo_cmd_t junk;

    fresh_screen();
    link_up();
    screen_tap(PROG_X, BTN_Y);
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
    RUN(programming_is_locked_until_the_port_is_the_testers_target);
    RUN(a_port_without_a_pin_is_refused_by_name);
    RUN(a_port_without_a_servo_says_so_and_gives_the_port_back);
    RUN(the_programming_voltage_is_typed_inside_1_to_6_v);
    RUN(a_programming_voltage_above_6_v_needs_hv_servo_and_a_hold);
    RUN(the_current_limit_is_typed_inside_50_to_3000_ma);
    RUN(a_supply_that_cannot_give_the_voltage_is_refused_by_name);
    RUN(a_supply_that_does_not_switch_on_ends_the_connect_with_its_line);
    RUN(reboot_servo_without_a_supply_to_switch_says_so);
    RUN(reboot_servo_counts_the_supply_off_and_the_servo_up);
    RUN(reboot_servo_in_programming_mode_is_the_way_out);
    RUN(stop_and_a_lost_link_end_the_supply_cycle_with_their_lines);
    RUN(a_count_of_1_is_said_in_the_singular);
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
    RUN(files_lists_the_card_under_the_entry_that_saves);
    RUN(more_files_than_the_list_holds_are_shown_in_turn);
    RUN(the_list_waits_for_the_card_and_opens_once);
    RUN(a_save_asks_for_the_name_with_the_next_free_one_preset);
    RUN(a_name_the_card_refuses_comes_back_to_the_keyboard);
    RUN(every_failure_of_the_card_has_its_line);
    RUN(a_file_becomes_the_pending_values_and_the_ordinary_write_sends_them);
    RUN(an_edited_file_is_named_as_one_and_its_locked_values_are_counted);
    RUN(a_file_of_the_other_servo_type_is_denied_naming_both);
    RUN(the_servo_type_is_shown_once_the_servo_is_read);
    RUN(a_file_that_does_not_read_says_why);
    RUN(a_file_past_a_soft_limit_waits_for_a_2_s_hold);
    RUN(a_file_with_more_warnings_than_lines_counts_the_rest);
    RUN(a_file_that_holds_the_servos_values_changes_nothing);
    RUN(a_load_waits_for_a_servo_that_is_read);
    RUN(the_first_write_saves_the_servo_to_a_file_without_asking);
    RUN(a_first_write_whose_file_fails_is_not_sent);
    RUN(a_first_write_with_no_card_is_sent_after_its_page_says_so);
    RUN(a_card_put_in_between_two_writes_takes_the_file_at_the_second);
    RUN(a_card_pulled_under_the_write_page_brings_the_page_back);
    RUN(a_card_pulled_after_the_backup_file_leaves_the_next_write_alone);
    RUN(a_restore_with_no_card_shows_its_warnings_and_the_missing_file);
    RUN(a_restore_and_a_release_whose_card_is_pulled_come_back_too);
    RUN(a_servo_that_is_not_the_one_of_the_interrupted_write_is_named);
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
    RUN(the_programmer_stays_on_the_screen_while_the_port_is_held);
    RUN(a_lost_touch_stream_on_the_screen_drops_the_programmers_hold);
    return test_summary("kst_panel");
}
