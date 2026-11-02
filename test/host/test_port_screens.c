/*
 * The programming ports on the screens (protocol 4.26): the view the
 * control task hands them (shared/ui/port_view.c), the OUTPUTS PROG PORT
 * entry (outputs_port_entry.c) and the board view with a port picked
 * (picker_port.c).
 *
 * Under test: the view not known until the page is read, and an ask
 * refused then; an ask numbered 1 to 255 and never 0, a word on an older
 * ask ignored, the line each state gives and none for another port; on
 * OUTPUTS the PIN button's label for each state and its tap opening PICK
 * A PIN on the port, FUNCTION asked on a tap and locked before the read,
 * on an IOmcu older than 4.26, while the port is the tester's, ARMED or
 * held, the SERVO TESTER segment of port 3, the WATCHED BY line; in PICK A
 * PIN the port rows of the list, the port's own pin ticked and a tap on
 * it asking for none, a free pin asked for, another port's, a protocol's
 * and an interface's pin held, the slice of a PWM output and the compare
 * register of another SERVO PWM port refused for a SERVO PWM port and
 * not for a NONE port, the reason lines, every state drawn inside the
 * canvas; the port rules at their edges: no binding, an index past the
 * catalogue, a locked free pin, an interface's pin, each reason line,
 * a function not known, the board view opened on a port OUTPUTS lacks; a
 * NONE port's pin holding no PWM slice for an output on either screen;
 * another port's refused pin held neither as a pin nor as a compare
 * mate.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "link_pages.h"
#include "outputs_screen.h"
#include "outputs_screen_int.h"
#include "outputs_spots.h"
#include "picker_port.h"
#include "picker_screen.h"
#include "sensors_rows.h"
#include "port_view.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_sensors.h"
#include "ui_text.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
/* outputs_spots.h: the PIN button, then the segments under their labels,
 * FUNCTION's face and its list's rows. */
#define PIN_X    (OU_COL_X + OU_COL_W / 2)
#define PIN_Y    OU_PIN_Y
#define SEG_X(i) OU_SEG_X(i)
#define FN_Y     OU_FN_FACE_Y
#define FACE_X   OU_FN_FACE_X
#define FN_ROW_Y(i) OU_FN_ROW_Y(i)
#define TESTER_Y OU_TESTER_Y

/* What the ask hook was handed. */
static struct {
    int      n;
    uint8_t  seq, port;
    prog_link_ask_kind_t kind;
    uint16_t value;
    bool     refuse;
} g_ask;

static bool on_ask(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                   uint16_t value)
{
    if (g_ask.refuse) {
        return false;
    }
    ++g_ask.n;
    g_ask.seq   = seq;
    g_ask.port  = port;
    g_ask.kind  = kind;
    g_ask.value = value;
    return true;
}

/* A 4.26 IOmcu with 4 ports: port 1 SERVO PWM on GP13, port 2 NONE on
 * GP14, port 3 SERVO PWM without a pin, port 4 NONE without a pin. */
static void view_four(bool has_func)
{
    port_view_t v;
    port_view_unknown(&v);
    v.count    = 4u;
    v.has_func = has_func;
    v.pin[0]   = 13u;
    v.pin[1]   = 14u;
    v.func[0]  = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[1]  = (uint8_t)LINK_PG_FN_NONE;
    v.func[2]  = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[3]  = (uint8_t)LINK_PG_FN_NONE;
    if (!has_func) {
        v.func[1] = v.func[3] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    }
    port_view_set(&v);
    const uint8_t held[4] = { 13u, 14u, 0xFFu, 0xFFu };
    outputs_screen_set_prog_ports(4u, held);
    picker_screen_set_prog_ports(4u, held);
}

static void view_unknown(void)
{
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    outputs_screen_set_prog_pin(OUTPUTS_NO_PIN);
    picker_screen_set_prog_ports(0u, NULL);
}

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    settings_set_hold(NULL);
    servo_screen_set_bench_armed(false);
    memset(&g_ask, 0, sizeof(g_ask));
    port_view_set_ask(on_ask);
    /* The last case's ask is done with. */
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    view_four(true);
    outputs_screen()->reset();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outputs_screen_set_binding(&b);
    picker_screen()->reset();
    picker_screen_set_binding(&b);
    picker_screen_set_drawing(NULL, 0u, NULL);
}

static void tap_on(const ui_screen_t *scr, int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr->event(&d);
    scr->event(&u);
}

static void out_tap(int x, int y) { tap_on(outputs_screen(), x, y); }

/* OUTPUTS on port @p n's entry. */
static void out_port(uint8_t n)
{
    outputs_screen_open_on_port(n);
    outputs_screen()->enter();
    CHECK_EQ(outputs_screen_port_shown(), (int)n);
}

/* FUNCTION: the face tapped, and entry @p fn of the list where it opened. */
static void fn_pick(unsigned fn)
{
    out_tap(FACE_X, FN_Y);
    if (outputs_port_fn_open()) {
        out_tap(FACE_X, FN_ROW_Y(fn));
    }
}

static const char *out_reason(void)
{
    static char why[128];
    (void)outputs_screen_port_reason(why, sizeof(why));
    return why;
}

/* OUTPUTS alone, every pixel inside the canvas painted. */
static void render_out(void)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
    outputs_screen()->tick(0.016f);
    outputs_screen()->render(&c, -1);    /* no framebuffer: always drawn */
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
}

static void render_both(void)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
    picker_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
}

/* ------------------------------------------------------------- the view */

TEST_CASE(the_view_is_not_known_until_read_and_takes_no_ask_then)
{
    fresh();
    view_unknown();
    CHECK(!port_view_has(0u));
    CHECK_EQ(port_view_func(0u), PORT_VIEW_FN_UNKNOWN);
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_PIN, 14u));
    CHECK_EQ(g_ask.n, 0);
    view_four(true);
    CHECK(port_view_has(3u));
    CHECK(!port_view_has(4u));
    CHECK_EQ(port_view_func(1u), (uint8_t)LINK_PG_FN_NONE);
    /* No hook, a FUNC the IOmcu does not take, a value no write takes. */
    port_view_set_ask(NULL);
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_PIN, 14u));
    port_view_set_ask(on_ask);
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_FUNC, 2u));
    view_four(false);
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_FUNC, LINK_PG_FN_NONE));
    CHECK(port_view_ask(0u, PROG_LINK_ASK_PIN, 14u));
    g_ask.refuse = true;
    CHECK(!port_view_ask(0u, PROG_LINK_ASK_PIN, 15u));
    /* A revision moves with a change only. */
    const uint32_t rev = port_view_revision();
    view_four(false);
    CHECK_EQ(port_view_revision(), rev);
    view_four(true);
    CHECK(port_view_revision() != rev);
    port_view_unknown(NULL);
    port_view_set(NULL);
}

TEST_CASE(an_ask_is_numbered_past_0_and_its_word_says_how_it_stands)
{
    char line[96];

    fresh();
    for (int i = 0; i < 300; ++i) {
        CHECK(port_view_ask(1u, PROG_LINK_ASK_PIN, 20u));
        CHECK(g_ask.seq != 0u);
    }
    CHECK_EQ(port_view_ask_seq(), g_ask.seq);
    uint8_t port = 9u, kind = 9u;
    CHECK_EQ(port_view_ask_state(&port, &kind), PROG_LINK_ASK_WAITING);
    CHECK_EQ(port, 1u);
    CHECK_EQ(kind, (uint8_t)PROG_LINK_ASK_PIN);
    CHECK(port_view_ask_line(1u, line, sizeof(line)));
    CHECK_STR_EQ(line, TR(OUT_PORT_ASK_WAIT));
    /* Once the control task has judged it, the line names what holds it. */
    static const struct {
        prog_link_wait_t wait;
        ui_text_id_t     text;
    } k_waits[] = {
        { PROG_LINK_WAIT_READ,  TX_OUT_PORT_WAIT_READ },
        { PROG_LINK_WAIT_KST,   TX_OUT_PORT_WAIT_KST },
        { PROG_LINK_WAIT_HITEC, TX_OUT_PORT_WAIT_HITEC },
        { PROG_LINK_WAIT_ESC,   TX_OUT_PORT_WAIT_ESC },
        { PROG_LINK_WAIT_SAFE,  TX_OUT_PORT_WAIT_SAFE },
        { PROG_LINK_WAIT_TEST,  TX_OUT_PORT_WAIT_TEST },
    };
    for (size_t k = 0u; k < sizeof(k_waits) / sizeof(k_waits[0]); ++k) {
        port_view_set_ask_wait(k_waits[k].wait);
        CHECK(port_view_ask_line(1u, line, sizeof(line)));
        CHECK_STR_EQ(line, ui_tr(k_waits[k].text));
    }
    port_view_set_ask_wait(PROG_LINK_WAIT_NONE);
    CHECK(!port_view_ask_line(0u, line, sizeof(line)));
    CHECK_STR_EQ(line, "");
    /* A word on an older ask, or IDLE, is no word on this one. */
    port_view_set_ask_state((uint8_t)(g_ask.seq - 1u), PROG_LINK_ASK_DONE);
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_IDLE);
    CHECK_EQ(port_view_ask_state(NULL, NULL), PROG_LINK_ASK_WAITING);
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_REFUSED);
    CHECK(port_view_ask_line(1u, line, sizeof(line)));
    CHECK_STR_EQ(line, TR(OUT_PORT_ASK_REFUSED));
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_LOST);
    CHECK(port_view_ask_line(1u, line, sizeof(line)));
    CHECK_STR_EQ(line, TR(OUT_PORT_ASK_LOST));
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_DONE);
    CHECK(!port_view_ask_line(1u, line, sizeof(line)));
    CHECK(!port_view_ask_line(1u, NULL, 0u));
}

/* ------------------------------------------------------------- OUTPUTS */

TEST_CASE(the_entry_says_the_ports_are_not_read_before_the_first_read)
{
    fresh();
    view_unknown();
    out_port(0u);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_NOT_READ));
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 0);
    /* The tester's rows take their settings as before the functions. */
    out_tap(SEG_X(1), TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    render_both();
    settings_set(SET_SERVO_TARGET, 0.0f);
}

TEST_CASE(function_is_asked_on_a_tap_and_its_ask_is_said)
{
    fresh();
    out_port(0u);
    fn_pick(0u);                 /* the one it has */
    CHECK_EQ(g_ask.n, 0);
    fn_pick(1u);                 /* NONE */
    CHECK_EQ(g_ask.n, 1);
    CHECK_EQ(g_ask.port, 0u);
    CHECK_EQ(g_ask.kind, PROG_LINK_ASK_FUNC);
    CHECK_EQ(g_ask.value, (uint16_t)LINK_PG_FN_NONE);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_ASK_WAIT));
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_REFUSED);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_ASK_REFUSED));
    render_both();
    /* Port 2's entry says nothing of port 1's ask. */
    out_port(1u);
    CHECK(strcmp(out_reason(), TR(OUT_PORT_ASK_REFUSED)) != 0);
    fn_pick(0u);                 /* SERVO PWM */
    CHECK_EQ(g_ask.n, 2);
    CHECK_EQ(g_ask.port, 1u);
    CHECK_EQ(g_ask.value, (uint16_t)LINK_PG_FN_SERVO_PWM);
    port_view_set_ask_state(g_ask.seq, PROG_LINK_ASK_DONE);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_TESTER_PWM));
}

TEST_CASE(function_is_locked_where_the_iomcu_would_refuse_it)
{
    fresh();
    /* An IOmcu older than 4.26: SERVO PWM only, and it says so. */
    view_four(false);
    out_port(0u);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_FN_OLD));
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 0);
    view_four(true);
    /* The tester's port. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    CHECK_STR_EQ(out_reason(), "SERVO TESTER DRIVES PROG PORT 1");
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 0);
    settings_set(SET_SERVO_TARGET, 0.0f);
    /* ARMED. */
    servo_screen_set_bench_armed(true);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_LOCKED));
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 0);
    render_both();
    servo_screen_set_bench_armed(false);
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 1);
}

/* 4.28: the list holds the DShot rates; a 4.26 or 4.27 IOmcu's holds
 * SERVO PWM and NONE.  The open list takes every press: one on an entry
 * asks, one beside it shuts it and reaches nothing under it. */
TEST_CASE(function_list_offers_dshot_from_4_28_and_takes_every_press)
{
    fresh();
    port_view_t v = *port_view_get();
    out_port(1u);
    /* 4.26: two entries; a press where a third would be shuts the list
     * and reaches no row under it. */
    out_tap(FACE_X, FN_Y);
    CHECK(outputs_port_fn_open());
    CHECK(outputs_screen()->covered());
    render_out();
    const int target = settings_get_int(SET_SERVO_TARGET);
    out_tap(SEG_X(1), FN_ROW_Y(2));
    CHECK(!outputs_port_fn_open());
    CHECK_EQ(g_ask.n, 0);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), target);
    /* 4.28: five, DSHOT300 asked as 3. */
    v.has_dshot = true;
    port_view_set(&v);
    fn_pick(3u);
    CHECK_EQ(g_ask.n, 1);
    CHECK_EQ(g_ask.port, 1u);
    CHECK_EQ(g_ask.kind, PROG_LINK_ASK_FUNC);
    CHECK_EQ(g_ask.value, (uint16_t)LINK_PG_FN_DSHOT300);
    CHECK(!outputs_port_fn_open());
    fn_pick(4u);
    CHECK_EQ(g_ask.value, (uint16_t)LINK_PG_FN_DSHOT600);
    fn_pick(2u);
    CHECK_EQ(g_ask.value, (uint16_t)LINK_PG_FN_DSHOT150);
    CHECK_EQ(g_ask.n, 3);
    /* The function it has: no ask. */
    fn_pick(1u);
    CHECK_EQ(g_ask.n, 3);
    /* A second tap on the face shuts it; a press under way that slides
     * off its entry asks nothing. */
    out_tap(FACE_X, FN_Y);
    out_tap(FACE_X, FN_Y);
    CHECK(!outputs_port_fn_open());
    out_tap(FACE_X, FN_Y);
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, FACE_X, FN_ROW_Y(3), 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP, { 0, FACE_X, FN_ROW_Y(4), 40 }, 0u };
    outputs_screen()->event(&d);
    outputs_screen()->event(&u);
    CHECK_EQ(g_ask.n, 3);
    CHECK(outputs_port_fn_open());           /* still open for a choice */
    /* Lost touches shut it. */
    outputs_screen()->cancel();
    CHECK(!outputs_port_fn_open());
    /* The home tag shuts it and is taken; with none open it leaves. */
    out_tap(FACE_X, FN_Y);
    CHECK(outputs_screen()->back());
    CHECK(!outputs_port_fn_open());
    CHECK(!outputs_screen()->back());
    /* A lock while it is open shuts it at the next tick. */
    out_tap(FACE_X, FN_Y);
    servo_screen_set_bench_armed(true);
    outputs_screen()->tick(0.016f);
    CHECK(!outputs_port_fn_open());
    out_tap(FACE_X, FN_Y);
    CHECK(!outputs_port_fn_open());
    render_out();
    servo_screen_set_bench_armed(false);
    /* The face names each function. */
    for (uint8_t fn = 0; fn < (uint8_t)LINK_PG_FN_COUNT; ++fn) {
        v.func[1] = fn;
        port_view_set(&v);
        render_out();
    }
    v.has_dshot = false;
    port_view_set(&v);
}

/* A DShot port is no tester target: SERVO TESTER takes no PORT n there,
 * and the line says why. */
TEST_CASE(a_dshot_port_locks_the_tester_segment)
{
    fresh();
    port_view_t v = *port_view_get();
    v.has_dshot = true;
    v.func[1] = (uint8_t)LINK_PG_FN_DSHOT300;
    port_view_set(&v);
    out_port(1u);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_TESTER_PWM));
    out_tap(SEG_X(1), TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    CHECK(!port_view_pwm(1u));
    /* Under no PWM rule: no pin of it in the PWM pins. */
    const uint8_t pins[LINK_PG_PORTS_MAX] = { 13u, 14u, 15u, 16u };
    CHECK_EQ(outputs_pwm_pins(pins)[1], OUTBIND_NO_GPIO);
    CHECK_EQ(outputs_pwm_pins(pins)[0], 13u);
    CHECK_STR_EQ(port_view_fn_name(port_view_func(1u)), "DSHOT300");
    render_out();
    v.has_dshot = false;
    v.func[1] = (uint8_t)LINK_PG_FN_NONE;
    port_view_set(&v);
}

TEST_CASE(the_pin_row_only_names_the_pin_and_the_board_view_takes_the_port)
{
    fresh();
    ui_router_goto(SCREEN_OUTPUTS);
    out_port(2u);                             /* no pin */
    render_both();
    /* The grid view's PIN row takes no press: no ask, no other screen. */
    out_tap(PIN_X, PIN_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(g_ask.n, 0);
    CHECK_EQ(outputs_screen_port_shown(), 2);
    /* A workbench's ask opens the board view on the port, once. */
    outputs_screen_open_on_port(2u);
    outputs_view_screen()->leave();
    outputs_view_screen()->enter();
    CHECK(outputs_view_is_board());
    CHECK_EQ(picker_screen_port_shown(), 2);
    CHECK_EQ(outputs_screen_port_shown(), 2);
    outputs_view_screen()->leave();
    outputs_view_screen()->enter();
    CHECK_EQ(picker_screen_port_shown(), -1);
    CHECK_EQ(outputs_screen_port_shown(), -1);
    /* A refused pin and an unknown view draw inside the canvas. */
    port_view_t v = *port_view_get();
    v.pin[2] = 40u;
    v.refused = 4u;
    port_view_set(&v);
    out_port(2u);
    render_both();
    view_unknown();
    render_both();
}

TEST_CASE(the_tester_takes_port_3_and_the_watch_line_follows_the_map)
{
    fresh();
    out_port(2u);
    out_tap(SEG_X(1), TESTER_Y);              /* PORT 3 */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 3);
    /* Port 1's entry: neither segment selected, and why. */
    out_port(0u);
    CHECK_STR_EQ(out_reason(), "SERVO TESTER DRIVES PROG PORT 3");
    out_tap(SEG_X(0), TESTER_Y);              /* OUTPUTS */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* A NONE port is no target. */
    out_port(1u);
    out_tap(SEG_X(1), TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* The watch line repaints with the map. */
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    out_port(0u);
    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
    outputs_screen()->tick(0.02f);
    outputs_screen()->render(&c, 0);
    const uint32_t before = guard_canvas_sum(&g);
    outputs_screen_set_port_watch(0u, "INA3221 0x41 CH2");
    outputs_screen()->render(&c, 0);
    const uint32_t watched = guard_canvas_sum(&g);
    CHECK(watched != before);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    outputs_screen_set_port_watch(0u, "INA3221 0x41 CH2");
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_sum(&g), watched);
    /* From the SENSORS rows: 0x41 CH2 on PORT 1 writes that line; with
     * its part not fitted, or its shunt off, none.  Each against the line
     * set by hand with the same rows. */
    settings_set(SET_INA_W(4), (float)SENS_W_PORT_1);
    sensors_rows_publish_watch();
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_sum(&g), before);
    settings_set(SET_INA_FIT(1), 1.0f);
    CHECK_EQ(sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PORT(0)), 4);
    outputs_screen_set_port_watch(0u, "INA3221 0x41 CH2");
    outputs_screen()->render(&c, 0);
    const uint32_t by_hand = guard_canvas_sum(&g);
    outputs_screen_set_port_watch(0u, "");
    outputs_screen()->render(&c, 0);
    const uint32_t none = guard_canvas_sum(&g);
    CHECK(by_hand != none);
    sensors_rows_publish_watch();
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_sum(&g), by_hand);
    settings_set(SET_INA_SH(4), 0.0f);
    CHECK_EQ(sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PORT(0)), -1);
    sensors_rows_publish_watch();
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_sum(&g), none);
    char name[12];
    CHECK_STR_EQ(sensors_rows_channel(11, name, sizeof(name)), "0x43 CH3");
    CHECK_STR_EQ(sensors_rows_channel(12, name, sizeof(name)), "");
    CHECK_STR_EQ(sensors_rows_channel(-1, name, sizeof(name)), "");
    CHECK_STR_EQ(sensors_rows_channel(0, NULL, 0u), "");
    /* The meter's name in the reports: the channel watching the tester's
     * target, PORT 1 now, or the part alone with none. */
    char meter[20];
    settings_set(SET_SERVO_TARGET, 1.0f);
    CHECK_STR_EQ(sensors_rows_meter(meter, sizeof(meter)), "INA3221");
    settings_set(SET_INA_SH(4), 100.0f);
    CHECK_STR_EQ(sensors_rows_meter(meter, sizeof(meter)),
                 "INA3221 0x41 CH2");
    CHECK_STR_EQ(sensors_rows_meter(NULL, 0u), "");
    CHECK_STR_EQ(sensors_rows_short(meter, sizeof(meter)), "INA 0x41 CH2");
    CHECK_STR_EQ(sensors_rows_short(NULL, 0u), "");
    settings_set(SET_SERVO_TARGET, 0.0f);
    settings_set(SET_INA_FIT(1), 0.0f);
    settings_set(SET_INA_SH(4), 100.0f);
    settings_set(SET_INA_W(4), 0.0f);
    outputs_screen_set_port_watch(0u, NULL);
    outputs_screen_set_port_watch(LINK_PG_PORTS_MAX, "X");
}

/* --------------------------------------------------------- the board view */

static picker_pin_t pin_of(uint8_t gpio, char *holder, size_t cap)
{
    return picker_screen_pin(outbind_index_of(BOARD, gpio), holder, cap);
}

static void pk_tap_pin(uint8_t gpio)
{
    gfx_rect_t r;
    CHECK(picker_screen_button_rect(outbind_index_of(BOARD, gpio), &r));
    tap_on(picker_screen(), r.x + r.w / 2, r.y + r.h / 2);
}

/* the board view on port @p n, picked from its list. */
static void pk_port(uint8_t n)
{
    picker_screen()->enter();
    const gfx_rect_t face = picker_screen_list_face();
    tap_on(picker_screen(), face.x + 20, face.y + face.h / 2);
    CHECK(picker_screen_list_open());
    const gfx_rect_t row =
        picker_screen_list_row((uint8_t)(OUTBIND_PROTOS + n));
    CHECK(row.y + row.h <= UI_BODY_H);
    tap_on(picker_screen(), row.x + 20, row.y + row.h / 2);
    CHECK_EQ(picker_screen_port_shown(), (int)n);
    CHECK_EQ(outputs_screen_port_shown(), (int)n);
}

TEST_CASE(a_port_picked_ticks_its_pin_and_asks_for_another)
{
    char holder[32];
    char buf[96];

    fresh();
    pk_port(0u);
    CHECK_EQ(pin_of(13u, NULL, 0), PICKER_PIN_TICKED);
    CHECK_EQ(pin_of(14u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "PROG PORT 2");
    CHECK_EQ(pin_of(20u, NULL, 0), PICKER_PIN_FREE);
    CHECK_STR_EQ(port_view_fn_name(port_view_func(2u)), "SERVO PWM");
    /* The FUNCTION face says it; no count line beside it. */
    picker_screen_count(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "");
    pk_tap_pin(20u);
    CHECK_EQ(g_ask.n, 1);
    CHECK_EQ(g_ask.port, 0u);
    CHECK_EQ(g_ask.kind, PROG_LINK_ASK_PIN);
    CHECK_EQ(g_ask.value, 20u);
    CHECK(picker_screen_reason(buf, sizeof(buf)));
    CHECK_STR_EQ(buf, TR(OUT_PORT_ASK_WAIT));
    pk_tap_pin(13u);                         /* its own: none */
    CHECK_EQ(g_ask.n, 2);
    CHECK_EQ(g_ask.value, LINK_PG_PIN_NONE);
    pk_tap_pin(14u);                         /* another port's */
    CHECK_EQ(g_ask.n, 2);
    render_both();
    /* A protocol picked after it leaves the port. */
    picker_screen_follow(1u, true);
    outputs_screen_select(1u);
    picker_screen_follow(1u, true);
    CHECK_EQ(picker_screen_port_shown(), -1);
}

TEST_CASE(a_servo_pwm_port_is_refused_the_pwm_rules_and_a_none_port_not)
{
    char holder[32];
    char buf[96];

    fresh();
    /* GP4 bound as SERVO PWM: GP5 shares its slice. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, 1u);
    CHECK(outbind_toggle(&b, outbind_index_of(BOARD, 4u)));
    outbind_set_proto(&b, 0u);
    picker_screen_set_binding(&b);
    outputs_screen_set_binding(&b);
    pk_port(2u);                             /* SERVO PWM, no pin */
    CHECK_EQ(pin_of(4u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "SERVO PWM");
    CHECK_EQ(pin_of(5u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "SLICE OF GPIO 4");
    pk_tap_pin(5u);
    CHECK_EQ(g_ask.n, 0);
    CHECK(picker_screen_reason(buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "GPIO 5: SHARES PWM SLICE WITH OUTPUT GPIO 4");
    /* GP28 shares a compare register with GP12: free while no SERVO PWM
     * port holds GP12, held once port 1 does. */
    CHECK_EQ(pin_of(28u, NULL, 0), PICKER_PIN_FREE);
    port_view_t v = *port_view_get();
    v.pin[0] = 12u;
    port_view_set(&v);
    CHECK_EQ(pin_of(28u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "PWM CH OF GPIO 12");
    render_both();
    /* Port 4, NONE: both are free. */
    pk_port(3u);
    CHECK_EQ(pin_of(5u, NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(pin_of(28u, NULL, 0), PICKER_PIN_FREE);
    CHECK_STR_EQ(port_view_fn_name(port_view_func(1u)), "NONE");
    pk_tap_pin(5u);
    CHECK_EQ(g_ask.n, 1);
    CHECK_EQ(g_ask.port, 3u);
}

TEST_CASE(a_port_picked_before_the_read_or_on_an_unread_binding_is_locked)
{
    char buf[96];

    fresh();
    pk_port(1u);
    view_unknown();
    CHECK_EQ(pin_of(20u, NULL, 0), PICKER_PIN_LOCKED);
    CHECK(picker_screen_reason(buf, sizeof(buf)));
    CHECK_STR_EQ(buf, TR(OUT_PORT_NOT_READ));
    pk_tap_pin(20u);
    CHECK_EQ(g_ask.n, 0);
    render_both();
    view_four(true);
    picker_screen_follow(0u, false);         /* the binding not read */
    CHECK_EQ(pin_of(20u, NULL, 0), PICKER_PIN_LOCKED);
    /* Another's pin stays held, as a protocol's does. */
    CHECK_EQ(pin_of(14u, NULL, 0), PICKER_PIN_HELD);
    CHECK(picker_screen_reason(buf, sizeof(buf)));
    CHECK_STR_EQ(buf, TR(PK_NOT_READ));
    pk_tap_pin(20u);
    CHECK_EQ(g_ask.n, 0);
}

TEST_CASE(the_port_rules_hold_at_their_edges)
{
    char holder[32];
    char buf[96];
    outbind_t b;

    fresh();
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    const uint8_t n = outbind_pin_count(BOARD);
    const uint8_t gp20 = outbind_index_of(BOARD, 20u);
    /* No binding, or an index past the catalogue: no pin, no tap. */
    CHECK_EQ(picker_port_pin(NULL, gp20, 0u, false, NULL, 0), PICKER_PIN_NONE);
    CHECK_EQ(picker_port_pin(&b, n, 0u, false, holder, sizeof(holder)),
             PICKER_PIN_NONE);
    CHECK_STR_EQ(holder, "");
    CHECK(!picker_port_tap(NULL, gp20, 0u, PICKER_PIN_FREE));
    CHECK(!picker_port_tap(&b, n, 0u, PICKER_PIN_FREE));
    CHECK(!picker_port_tap(&b, gp20, 0u, PICKER_PIN_HELD));
    CHECK(!picker_port_clashes(NULL, gp20, 0u));
    CHECK(!picker_port_clashes(&b, n, 0u));
    CHECK_EQ(g_ask.n, 0);
    /* A free pin while the bench locks the port. */
    CHECK_EQ(picker_port_pin(&b, gp20, 2u, true, NULL, 0), PICKER_PIN_LOCKED);
    /* An interface's pin is held under its name. */
    outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, true, 20, 21);
    CHECK_EQ(picker_port_pin(&b, gp20, 2u, false, holder, sizeof(holder)),
             PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, outputs_iface_name(OUTPUTS_IFACE_SENSE));
    outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, true, -1, -1);
    /* The reason line: no buffer, no refused pin, one past the
     * catalogue, one that breaks no rule, and the compare register. */
    CHECK(!picker_port_reason(&b, 2u, -1, NULL, 0));
    CHECK(!picker_port_reason(&b, 2u, -1, buf, sizeof(buf)));
    CHECK(!picker_port_reason(NULL, 2u, 0, buf, sizeof(buf)));
    CHECK(!picker_port_reason(&b, 2u, (int)n, buf, sizeof(buf)));
    CHECK(!picker_port_reason(&b, 2u, (int)gp20, buf, sizeof(buf)));
    port_view_t v = *port_view_get();
    v.pin[0] = 12u;
    port_view_set(&v);
    CHECK(picker_port_reason(&b, 2u, (int)outbind_index_of(BOARD, 28u), buf,
                             sizeof(buf)));
    CHECK_STR_EQ(buf, "GPIO 28: SAME PWM CHANNEL AS GPIO 12");
    /* A port not known has no function to name. */
    view_unknown();
    CHECK_EQ(port_view_func(2u), PORT_VIEW_FN_UNKNOWN);
    /* No row past the port's one pin, nor one with no pin. */
    view_four(true);
    picker_row_t row;
    CHECK(picker_port_row(&b, 0u, NULL) == false);
    CHECK(!picker_port_row(&b, 1u, &row));
    CHECK(!picker_port_own_pin(&b, gp20, -1));
    CHECK(!picker_port_own_pin(&b, gp20, 4));
    /* OUTPUTS opened on a port it does not have: on the protocols. */
    outputs_screen_open_on_port(9u);
    outputs_view_screen()->enter();
    CHECK_EQ(picker_screen_port_shown(), -1);
    /* The entry's guards: no buffer, no port shown, no canvas. */
    outputs_port_name(0u, NULL, 0);
    outputs_screen()->reset();
    CHECK(!outputs_screen_port_reason(NULL, 0));
    CHECK(!outputs_screen_port_reason(buf, sizeof(buf)));
    outputs_port_draw(NULL, picker_screen_list_face(), false, false);
}

TEST_CASE(a_none_ports_pin_holds_no_pwm_slice_for_the_outputs)
{
    char label[32];

    fresh();
    /* SERVO PWM being edited on both screens.  Port 2 is NONE on GP14:
     * GP15 shares its slice and is free, and GP14 itself stays taken. */
    outputs_screen_select(1u);
    picker_screen_follow(1u, true);
    const uint8_t gp15 = outbind_index_of(BOARD, 15u);
    CHECK(outputs_screen_cell(gp15, NULL, 0) != OUTPUTS_CELL_HELD);
    CHECK_EQ(pin_of(15u, NULL, 0), PICKER_PIN_FREE);
    CHECK(pin_of(14u, NULL, 0) != PICKER_PIN_FREE);
    /* Port 2 as SERVO PWM: GP15 is held again. */
    port_view_t v = *port_view_get();
    v.func[1] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    port_view_set(&v);
    CHECK_EQ(outputs_screen_cell(gp15, label, sizeof(label)),
             OUTPUTS_CELL_HELD);
    CHECK_STR_EQ(label, "SLICE OF GPIO 14");
    CHECK_EQ(pin_of(15u, NULL, 0), PICKER_PIN_HELD);
    /* And a function not known counts as SERVO PWM. */
    v.func[1] = PORT_VIEW_FN_UNKNOWN;
    port_view_set(&v);
    CHECK_EQ(pin_of(15u, NULL, 0), PICKER_PIN_HELD);
}

TEST_CASE(another_ports_refused_pin_is_not_held)
{
    char holder[32];

    fresh();
    /* Port 2's GP14 refused by the IOmcu: shown on port 2, held by none,
     * so port 3 may take it. */
    port_view_t v = *port_view_get();
    v.refused = 0x2u;
    port_view_set(&v);
    pk_port(2u);
    CHECK_EQ(pin_of(14u, NULL, 0), PICKER_PIN_FREE);
    v.refused = 0u;
    port_view_set(&v);
    CHECK_EQ(pin_of(14u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "PROG PORT 2");
    /* Port 1 as SERVO PWM on GP12, refused: its compare mate GP28 is free
     * to port 3; held once the IOmcu holds GP12. */
    v.pin[0]  = 12u;
    v.refused = 0x1u;
    port_view_set(&v);
    CHECK_EQ(pin_of(28u, NULL, 0), PICKER_PIN_FREE);
    v.refused = 0u;
    port_view_set(&v);
    CHECK_EQ(pin_of(28u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "PWM CH OF GPIO 12");
}

int main(void)
{
    RUN(the_view_is_not_known_until_read_and_takes_no_ask_then);
    RUN(an_ask_is_numbered_past_0_and_its_word_says_how_it_stands);
    RUN(the_entry_says_the_ports_are_not_read_before_the_first_read);
    RUN(function_is_asked_on_a_tap_and_its_ask_is_said);
    RUN(function_is_locked_where_the_iomcu_would_refuse_it);
    RUN(function_list_offers_dshot_from_4_28_and_takes_every_press);
    RUN(a_dshot_port_locks_the_tester_segment);
    RUN(the_pin_row_only_names_the_pin_and_the_board_view_takes_the_port);
    RUN(the_tester_takes_port_3_and_the_watch_line_follows_the_map);
    RUN(a_port_picked_ticks_its_pin_and_asks_for_another);
    RUN(a_servo_pwm_port_is_refused_the_pwm_rules_and_a_none_port_not);
    RUN(a_port_picked_before_the_read_or_on_an_unread_binding_is_locked);
    RUN(the_port_rules_hold_at_their_edges);
    RUN(a_none_ports_pin_holds_no_pwm_slice_for_the_outputs);
    RUN(another_ports_refused_pin_is_not_held);
    return test_summary("port_screens");
}
