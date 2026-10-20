/*
 * The programming ports on the screens (protocol 4.26): the view the
 * control task hands them (shared/ui/port_view.c), the OUTPUTS PROG PORT
 * entry (outputs_port_entry.c) and PICK A PIN with a port picked
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
 * canvas.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "link_pages.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "port_view.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define COL_X 16
#define DD_X  16
#define DD_Y  44
#define DD_W  266
#define DD_H  54
/* outputs_port_entry.c: the PIN button, then the segments under their
 * labels, two of 128 px 4 px apart from x 19. */
#define PIN_X    (COL_X + 133)
#define PIN_Y    (106 + 20)
#define SEG_X(i) (19 + 64 + (i) * 132)
#define FN_Y     (154 + 20 + 18)
#define TESTER_Y (242 + 20 + 18)

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

static const char *out_reason(void)
{
    static char why[128];
    (void)outputs_screen_port_reason(why, sizeof(why));
    return why;
}

static void render_both(void)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    guard_canvas_init(&g, &c, px, 800, 432);
    outputs_screen()->render(&c, 0);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
    guard_canvas_init(&g, &c, px, 800, 432);
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
    out_tap(SEG_X(1), FN_Y);
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
    out_tap(SEG_X(0), FN_Y);                 /* the one it has */
    CHECK_EQ(g_ask.n, 0);
    out_tap(SEG_X(1), FN_Y);                 /* NONE */
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
    out_tap(SEG_X(0), FN_Y);                 /* SERVO PWM */
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
    out_tap(SEG_X(1), FN_Y);
    CHECK_EQ(g_ask.n, 0);
    view_four(true);
    /* The tester's port. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    CHECK_STR_EQ(out_reason(), "SERVO TESTER DRIVES PROG PORT 1");
    out_tap(SEG_X(1), FN_Y);
    CHECK_EQ(g_ask.n, 0);
    settings_set(SET_SERVO_TARGET, 0.0f);
    /* ARMED. */
    servo_screen_set_bench_armed(true);
    CHECK_STR_EQ(out_reason(), TR(OUT_PORT_LOCKED));
    out_tap(SEG_X(1), FN_Y);
    CHECK_EQ(g_ask.n, 0);
    render_both();
    servo_screen_set_bench_armed(false);
    out_tap(SEG_X(1), FN_Y);
    CHECK_EQ(g_ask.n, 1);
}

TEST_CASE(the_pin_button_names_the_pin_and_opens_pick_a_pin_on_the_port)
{
    fresh();
    ui_router_goto(SCREEN_OUTPUTS);
    out_port(2u);                             /* no pin */
    render_both();
    out_tap(PIN_X, PIN_Y);                    /* the router enters it */
    CHECK_EQ(picker_screen_port_shown(), 2);
    CHECK_EQ(outputs_screen_port_shown(), 2);
    /* Once: the next visit opens on OFF. */
    picker_screen()->leave();
    picker_screen()->enter();
    CHECK_EQ(picker_screen_port_shown(), -1);
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
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 432)];
    guard_canvas_t g;
    gfx_canvas_t c;
    out_port(0u);
    guard_canvas_init(&g, &c, px, 800, 432);
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
    outputs_screen_set_port_watch(0u, NULL);
    outputs_screen_set_port_watch(LINK_PG_PORTS_MAX, "X");
}

/* --------------------------------------------------------- PICK A PIN */

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

/* PICK A PIN on port @p n, picked from its list. */
static void pk_port(uint8_t n)
{
    picker_screen()->enter();
    const gfx_rect_t face = picker_screen_list_face();
    tap_on(picker_screen(), face.x + 20, face.y + face.h / 2);
    CHECK(picker_screen_list_open());
    const gfx_rect_t row =
        picker_screen_list_row((uint8_t)(OUTBIND_PROTOS + n));
    CHECK(row.y + row.h <= 432);
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
    picker_screen_count(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "FUNCTION: SERVO PWM");
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
    CHECK_STR_EQ(holder, "SLICE OF GP4");
    pk_tap_pin(5u);
    CHECK_EQ(g_ask.n, 0);
    CHECK(picker_screen_reason(buf, sizeof(buf)));
    CHECK_STR_EQ(buf, "GP5: SHARES PWM SLICE WITH OUTPUT GP4");
    /* GP28 shares a compare register with GP12: free while no SERVO PWM
     * port holds GP12, held once port 1 does. */
    CHECK_EQ(pin_of(28u, NULL, 0), PICKER_PIN_FREE);
    port_view_t v = *port_view_get();
    v.pin[0] = 12u;
    port_view_set(&v);
    CHECK_EQ(pin_of(28u, holder, sizeof(holder)), PICKER_PIN_HELD);
    CHECK_STR_EQ(holder, "PWM CH OF GP12");
    render_both();
    /* Port 4, NONE: both are free. */
    pk_port(3u);
    CHECK_EQ(pin_of(5u, NULL, 0), PICKER_PIN_FREE);
    CHECK_EQ(pin_of(28u, NULL, 0), PICKER_PIN_FREE);
    picker_screen_count(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "FUNCTION: NONE");
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

int main(void)
{
    RUN(the_view_is_not_known_until_read_and_takes_no_ask_then);
    RUN(an_ask_is_numbered_past_0_and_its_word_says_how_it_stands);
    RUN(the_entry_says_the_ports_are_not_read_before_the_first_read);
    RUN(function_is_asked_on_a_tap_and_its_ask_is_said);
    RUN(function_is_locked_where_the_iomcu_would_refuse_it);
    RUN(the_pin_button_names_the_pin_and_opens_pick_a_pin_on_the_port);
    RUN(the_tester_takes_port_3_and_the_watch_line_follows_the_map);
    RUN(a_port_picked_ticks_its_pin_and_asks_for_another);
    RUN(a_servo_pwm_port_is_refused_the_pwm_rules_and_a_none_port_not);
    RUN(a_port_picked_before_the_read_or_on_an_unread_binding_is_locked);
    return test_summary("port_screens");
}
