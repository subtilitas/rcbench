/**
 * @file test_outputs_view.c
 * @brief OUTPUTS as one screen: which view a board gets, the entry a TARGET
 *        row opens on and the opener the home tag returns to, the PROG PORT
 *        rows in the board view's column, LAST WRITE, UNBIND ALL PINS and
 *        the cat in a protocol's view and a PROG PORT's, in English and
 *        German.
 *
 * Cases:
 * - a_drawn_board_gets_the_board_and_any_other_the_grid
 * - a_view_changed_during_a_visit_drops_the_press_and_keeps_the_selection
 * - a_target_opens_the_board_view_on_its_entry_and_home_returns_to_it
 * - the_port_rows_act_in_the_column
 * - a_locked_row_takes_the_press_and_does_nothing
 * - a_key_asks_for_the_ports_pin_and_the_column_names_it
 * - the_column_holds_every_reason_and_meets_no_key
 * - last_write_shows_the_result_and_leaves_both_cat_spots_free
 * - a_board_with_a_key_there_has_last_write_at_the_foot
 * - the_unbind_key_holds_to_write_no_pin_in_the_board_view
 * - a_cat_is_drawn_in_a_protocols_view_and_a_ports_in_both_languages
 * - a_cat_moves_when_the_entrys_column_covers_it_and_stays_when_it_fits
 * - the_grid_view_draws_no_cat
 * - the_in_use_table_ends_above_the_unbind_key
 * - a_change_the_board_view_shows_repaints_it
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen_rig.h"

#include "guard_canvas.h"
#include "out_bind.h"
#include "outputs_screen_int.h"
#include "port_view.h"
#include "servo_screen.h"
#include "ui_board.h"
#include "ui_cats.h"

static int s_asks;
static uint8_t s_ask_port;
static prog_link_ask_kind_t s_ask_kind;
static uint16_t s_ask_value;

/** @brief The ask seam: counts and keeps the last ask. */
static bool on_ask(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                   uint16_t value)
{
    (void)seq;
    ++s_asks;
    s_ask_port = port;
    s_ask_kind = kind;
    s_ask_value = value;
    return true;
}

/** @brief The screen under test. */
static const ui_screen_t *view(void) { return outputs_view_screen(); }

/** @brief Sends one touch event at (@p x, @p y). */
static void send(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { type, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    view()->event(&e);
}

/** @brief A press and a release at the centre of @p r. */
static void press(gfx_rect_t r)
{
    send(TOUCH_EVENT_DOWN, r.x + r.w / 2, r.y + r.h / 2);
    send(TOUCH_EVENT_UP, r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief 4 ports: 1 SERVO PWM on GP13, 2 NONE on GP14, 3 and 4 no pin. */
static void four_ports(void)
{
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    v.pin[0] = 13u;
    v.pin[1] = 14u;
    v.func[0] = v.func[2] = (uint8_t)LINK_PG_FN_SERVO_PWM;
    v.func[1] = v.func[3] = (uint8_t)LINK_PG_FN_NONE;
    port_view_set(&v);
    const uint8_t held[4] = { 13u, 14u, 0xFFu, 0xFFu };
    outputs_screen_set_prog_ports(4u, held);
    picker_screen_set_prog_ports(4u, held);
}

/** @brief SERVO PWM on GP0 to GP2, DSHOT300 on GP6, SERVO PWM selected. */
static outbind_t bench(void)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0u)));
    CHECK(outbind_toggle(&b, idx(1u)));
    CHECK(outbind_toggle(&b, idx(2u)));
    outbind_set_proto(&b, proto_named("DSHOT300"));
    CHECK(outbind_toggle(&b, idx(6u)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    return b;
}

/** @brief A learned board with 12 free pins and no shape, or with one. */
static void learn_board(uint16_t id, bool shaped)
{
    uint16_t cat[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { cat[i] = 0u; }
    cat[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    cat[1] = LINK_CAT_OF(1, 20, LINK_PIN_FREE);
    cat[2] = LINK_CAT_OF(2, 21, LINK_PIN_FREE);
    cat[3] = LINK_CAT_OF(3, 40, LINK_PIN_FREE);
    CHECK(outbind_learn_board(id, cat));
    if (shaped) {
        uint16_t sh[LINK_SH_COUNT];
        sh[LINK_SH_WIDTH_CMM]  = 19u * 254u;
        sh[LINK_SH_HEIGHT_CMM] = 1900u;
        sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(LINK_SH_BOTTOM_LEFT, 20);
        sh[LINK_SH_PITCH_CMM]  = 254u;
        sh[LINK_SH_INSET_CMM]  = 100u;
        CHECK(outbind_learn_shape(id, sh));
    }
}

/** @brief The binding the rest of the case works on, read and shown. */
static void hold_binding(const outbind_t *b)
{
    outputs_screen_set_binding(b);
    picker_screen_set_binding(b);
}

/** @brief Everything back to no board, English, nothing asked. */
static void fresh_view(void)
{
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    settings_set_hold(NULL);
    servo_screen_set_bench_armed(false);
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
    outbind_forget_learned();
    ui_cats_force(UI_CAT_RANDOM);
    ui_cats_set_random(NULL);
    ui_cats_seed(1u);
    view()->reset();
    picker_screen_set_prog_ports(0u, NULL);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    s_applied = 0;
    s_asks = 0;
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    port_view_set_ask(on_ask);
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    four_ports();
}

/** @brief The bench on the RP2350-CAN with its drawing, a visit under way on
 *  SERVO PWM. */
static void visit_bench(void)
{
    fresh_view();
    const outbind_t b = bench();
    hold_binding(&b);
    CHECK(serve_drawing());
    view()->enter();
    outputs_screen_select(b.proto);
    view()->tick(0.016f);
    CHECK(outputs_view_is_board());
}

/** @brief The same visit on PROG PORT @p n. */
static void visit_port(uint8_t n)
{
    visit_bench();
    outputs_screen_select_port(n);
    view()->tick(0.016f);
    CHECK_EQ(picker_screen_port_shown(), (int)n);
}

/** @brief Draws a frame into the rig's canvas. */
static void frame(void)
{
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    picker_screen_invalidate();
    outputs_screen_invalidate();
    view()->render(&c, 0);
}

/* The rows of the entry in the column, from the layout. */
#define TESTER_SEG(i)                                                         \
    gfx_rect_make(OB_COL_X + (i) * OB_COL_W / 2, OB_TESTER_Y + 22,            \
                  OB_COL_W / 2, UI_TAP_MIN)
#define PWM_SEG(i)                                                            \
    gfx_rect_make(OB_COL_X + (i) * OB_COL_W / 2, OB_PWM_Y + 22, OB_COL_W / 2, \
                  UI_TAP_MIN)
#define FN_FACE gfx_rect_make(OB_FN_X, OB_FN_Y, OB_FN_W, UI_TAP_MIN)
/* Function @p i's row in the open list, under the face. */
#define FN_ROW(i)                                                             \
    gfx_rect_make(OB_COL_X + 4, OB_FN_Y + UI_TAP_MIN + 8 + (i) * UI_TAP_MIN,  \
                  OB_LIST_W - 8, UI_TAP_MIN)

/* ------------------------------------------------------------ the views */

TEST_CASE(a_drawn_board_gets_the_board_and_any_other_the_grid)
{
    /* No board: the grid, which shows no pins. */
    fresh_view();
    view()->enter();
    CHECK(!outputs_view_is_board());

    /* The RP2350-CAN, from its shape and from its drawing. */
    outbind_t b = bench();
    hold_binding(&b);
    CHECK(outputs_view_is_board());
    CHECK(serve_drawing());
    CHECK(outputs_view_is_board());
    CHECK_STR_EQ(ui_router_title(SCREEN_OUTPUTS), "OUTPUTS");

    /* A learned board with pins and no shape: the grid. */
    fresh_view();
    learn_board(4242u, false);
    outbind_init(&b);
    outbind_set_board(&b, 4242u);
    hold_binding(&b);
    view()->enter();
    CHECK(!outputs_view_is_board());
    /* With a shape: the board. */
    fresh_view();
    learn_board(4243u, true);
    outbind_init(&b);
    outbind_set_board(&b, 4243u);
    hold_binding(&b);
    view()->enter();
    CHECK(outputs_view_is_board());

    /* The carrier has no pin to pick, shape or not: the grid. */
    fresh_view();
    ui_board_set((uint16_t)OUTBIND_BOARD_RBMOD);
    outbind_init(&b);
    outbind_set_board(&b, (uint16_t)OUTBIND_BOARD_RBMOD);
    hold_binding(&b);
    view()->enter();
    CHECK(!outputs_view_is_board());
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

TEST_CASE(a_view_changed_during_a_visit_drops_the_press_and_keeps_the_selection)
{
    visit_bench();
    gfx_rect_t k;
    CHECK(picker_screen_button_rect(idx(4u), &k));
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    /* The board goes: the screen moves to the grid under the finger. */
    outbind_t none;
    outbind_init(&none);
    picker_screen_set_binding(&none);
    view()->tick(0.016f);
    CHECK(!outputs_view_is_board());
    const uint8_t chosen = outputs_screen_binding()->proto;
    /* It answers again, the release lands on the key: no pin ticks. */
    const outbind_t b = bench();
    picker_screen_set_binding(&b);
    view()->tick(0.016f);
    CHECK(outputs_view_is_board());
    send(TOUCH_EVENT_UP, k.x + 10, k.y + 10);
    CHECK_EQ(s_applied, 0);
    CHECK_EQ(outputs_screen_binding()->proto, chosen);

    /* The FUNCTION list shuts with the view that opened it. */
    visit_port(0u);
    press(FN_FACE);
    CHECK(outputs_port_fn_open());
    picker_screen_set_binding(&none);
    view()->tick(0.016f);
    CHECK(!outputs_port_fn_open());
}

TEST_CASE(a_target_opens_the_board_view_on_its_entry_and_home_returns_to_it)
{
    static const ui_screen_id_t k_from[] = { SCREEN_MOTOR, SCREEN_SERVO };
    for (unsigned f = 0; f < 2u; ++f) {
        ui_router_init();
        fresh_view();
        ui_router_goto(SCREEN_OVERVIEW);
        const outbind_t b = bench();
        hold_binding(&b);

        /* On a protocol, before the first frame. */
        ui_router_goto(k_from[f]);
        outputs_screen_open_on(proto_named("DSHOT300"));
        ui_router_goto(SCREEN_OUTPUTS);
        CHECK(outputs_view_is_board());
        CHECK_EQ(picker_screen_binding()->proto, proto_named("DSHOT300"));
        CHECK_EQ(picker_screen_port_shown(), -1);
        CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), k_from[f]);

        /* On a port. */
        ui_router_goto(k_from[f]);
        outputs_screen_open_on_port(1u);
        ui_router_goto(SCREEN_OUTPUTS);
        CHECK_EQ(picker_screen_port_shown(), 1);
        CHECK_EQ(outputs_screen_port_shown(), 1);
        CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), k_from[f]);

        /* Past the list: the overview. */
        ui_router_goto(k_from[f]);
        outputs_screen_open_on_port(9u);
        ui_router_goto(SCREEN_OUTPUTS);
        CHECK_EQ(picker_screen_port_shown(), -1);
        CHECK_EQ(picker_screen_binding()->proto, 0u);
    }
    /* From SETUP: the overview, and home goes back there. */
    ui_router_init();
    fresh_view();
    ui_router_goto(SCREEN_OVERVIEW);
    const outbind_t b = bench();
    hold_binding(&b);
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_OUTPUTS);
    CHECK_EQ(picker_screen_binding()->proto, 0u);
    CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SETUP);
}

/* ----------------------------------------------------- the PROG PORT rows */

TEST_CASE(the_port_rows_act_in_the_column)
{
    visit_port(0u);
    /* FUNCTION: the face opens the list, an entry asks and shuts it. */
    press(FN_FACE);
    CHECK(outputs_port_fn_open());
    frame();
    press(FN_ROW(1));
    CHECK(!outputs_port_fn_open());
    CHECK_EQ(s_asks, 1);
    CHECK_EQ(s_ask_port, 0u);
    CHECK_EQ(s_ask_kind, PROG_LINK_ASK_FUNC);
    /* A press elsewhere shuts it, spent on that: not the key under it. */
    press(FN_FACE);
    CHECK(outputs_port_fn_open());
    gfx_rect_t k;
    CHECK(picker_screen_button_rect(idx(5u), &k));
    press(k);
    CHECK(!outputs_port_fn_open());
    CHECK_EQ(s_asks, 1);
    /* The home tag shuts it first, then leaves. */
    press(FN_FACE);
    CHECK(view()->back());
    CHECK(!outputs_port_fn_open());
    CHECK(!view()->back());

    /* SERVO TESTER and PORT PWM set their settings from the column. */
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    press(TESTER_SEG(1));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    press(PWM_SEG(0));
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    press(PWM_SEG(1));
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    press(TESTER_SEG(0));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* A press lost halfway acts on nothing. */
    send(TOUCH_EVENT_DOWN, TESTER_SEG(1).x + 4, TESTER_SEG(1).y + 4);
    view()->cancel();
    send(TOUCH_EVENT_UP, TESTER_SEG(1).x + 4, TESTER_SEG(1).y + 4);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    frame();
}

TEST_CASE(a_locked_row_takes_the_press_and_does_nothing)
{
    visit_port(1u);                       /* NONE: no tester target */
    press(TESTER_SEG(1));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* PORT PWM only on the target port's entry. */
    const bool was = settings_get_bool(SET_SERVO_PORT_PWM);
    press(PWM_SEG(was ? 1 : 0));
    CHECK_EQ(settings_get_bool(SET_SERVO_PORT_PWM), was);
    /* The bench ARMED locks SERVO TESTER and FUNCTION on any port. */
    visit_port(0u);
    servo_screen_set_bench_armed(true);
    view()->tick(0.016f);
    press(TESTER_SEG(1));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    press(FN_FACE);
    CHECK(!outputs_port_fn_open());
    frame();
    servo_screen_set_bench_armed(false);
}

TEST_CASE(a_key_asks_for_the_ports_pin_and_the_column_names_it)
{
    visit_port(2u);                       /* no pin */
    gfx_rect_t k;
    CHECK(picker_screen_button_rect(idx(15u), &k));
    press(k);
    CHECK_EQ(s_asks, 1);
    CHECK_EQ(s_ask_port, 2u);
    CHECK_EQ(s_ask_kind, PROG_LINK_ASK_PIN);
    CHECK_EQ(s_ask_value, 15u);
    /* The column's row reads the view the drive path reads, in EN and DE. */
    port_view_t v = *port_view_get();
    v.pin[2] = 15u;
    port_view_set(&v);
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    frame();
    /* Its own key frees it. */
    press(k);
    CHECK_EQ(s_asks, 2);
    CHECK_EQ(s_ask_value, LINK_PG_PIN_NONE);
    /* Every state of the row draws: refused, none, not known. */
    v = *port_view_get();
    v.refused = 4u;
    port_view_set(&v);
    frame();
    v.pin[2] = PORT_VIEW_NO_PIN;
    v.refused = 0u;
    port_view_set(&v);
    frame();
    port_view_unknown(&v);
    port_view_set(&v);
    frame();
}

TEST_CASE(the_column_holds_every_reason_and_meets_no_key)
{
    static const ui_text_id_t k_why[] = {
        TX_OUT_PORT_HELD,    TX_OUT_PORT_NOT_READ,  TX_OUT_PORT_LOCKED,
        TX_OUT_PORT_TESTER_PWM, TX_OUT_PORT_FN_OLD, TX_OUT_PORT_PWM_NEEDS,
        TX_OUT_PORT_ASK_WAIT, TX_OUT_PORT_ASK_REFUSED, TX_OUT_PORT_ASK_LOST,
        TX_OUT_PORT_TESTER_ON, TX_PK_PORT_SLICE_WHY, TX_OUT_COMPARE_WHY,
    };
    const gfx_rect_t first = gfx_rect_make(OB_COL_X, OB_WHY_Y, OB_COL_W, 16);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (unsigned i = 0; i < sizeof(k_why) / sizeof(k_why[0]); ++i) {
            char buf[96];
            snprintf(buf, sizeof(buf), ui_tr(k_why[i]), 28u, 13u);
            /* Five lines of 8 x 16 hold what the label face cannot. */
            CHECK(outputs_wrap(NULL, first, OB_WHY_FINE, buf, 9, OU_FONT_FINE,
                               0, false) <= 5);
        }
        /* And every row's words the column's width. */
        char buf[96];
        snprintf(buf, sizeof(buf), ui_tr(TX_OUT_PORT_WATCHED),
                 "INA3221 0x41 CH2");
        const gfx_rect_t watch = gfx_rect_make(OB_COL_X, 0, OB_COL_W, 20);
        CHECK(outputs_wrap(NULL, watch, OU_LINE, buf, 9, OU_FONT_LABEL, 0,
                           false) <= 3);
        CHECK(outputs_wrap(NULL, watch, OU_LINE, ui_tr(TX_OUT_PORT_WATCH_NONE),
                           9, OU_FONT_LABEL, 0, false) <= 3);
        snprintf(buf, sizeof(buf), ui_tr(TX_OUT_PORT_PIN), 28u);
        CHECK(gfx_text_width(UI_FONT_BODY, buf, 1) <= OB_COL_W);
        CHECK(gfx_text_width(OU_FONT_LABEL, ui_tr(TX_OUT_PORT_FUNCTION), 1)
              <= OB_FN_X - OB_COL_X);
    }
    ui_text_set_language(UI_LANG_EN);

    /* Nothing in the column stands under a key, on the RP2350-CAN or on a
     * learned board with its keys from x 187. */
    const gfx_rect_t col = gfx_rect_make(OB_COL_X, OB_PIN_Y, OB_COL_W,
                                         OU_FOOT - OB_PIN_Y);
    const gfx_rect_t face = FN_FACE;
    visit_port(0u);
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        gfx_rect_t k;
        if (picker_screen_button_rect(i, &k)) {
            CHECK(!overlap(col, k));
            CHECK(!overlap(face, k));
        }
    }
    fresh_view();
    learn_board(4242u, true);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4242u);
    hold_binding(&b);
    view()->enter();
    CHECK(outputs_view_is_board());
    for (uint8_t i = 0; i < 4u; ++i) {
        gfx_rect_t k;
        CHECK(picker_screen_button_rect(i, &k));
        CHECK(!overlap(col, k));
    }
}

/* ------------------------------------------------- LAST WRITE and UNBIND */

TEST_CASE(last_write_shows_the_result_and_leaves_both_cat_spots_free)
{
    visit_bench();
    const gfx_rect_t r = picker_screen_last_rect();
    CHECK_EQ(r.y + r.h <= 64, true);       /* in the strip above the board */
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        gfx_rect_t k;
        if (picker_screen_button_rect(i, &k)) {
            CHECK(!overlap(r, k));
        }
    }
    CHECK(!overlap(r, picker_screen_list_face()));
    /* Every result draws in a face that holds it, in both languages. */
    static const outputs_result_t k_res[] = { OUTPUTS_IDLE, OUTPUTS_OK,
                                              OUTPUTS_NO_LINK,
                                              OUTPUTS_REFUSED };
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (unsigned i = 0; i < 4u; ++i) {
            outputs_screen_set_result(k_res[i]);
            CHECK(gfx_text_width(OU_FONT_FINE, outputs_result_text(), 1)
                  <= r.w);
            frame();
        }
        CHECK(outputs_wrap(NULL, gfx_rect_make(r.x, 0, r.w, 16), 16,
                           ui_tr(TX_OUT_LAST_WRITE), 9, OU_FONT_FINE, 0, false)
              <= 2);
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_board_with_a_key_there_has_last_write_at_the_foot)
{
    /* Upper-row pins all along the top edge, every position a key. */
    fresh_view();
    uint16_t cat[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { cat[i] = 0u; }
    for (unsigned i = 0; i < 12u && i < LINK_CAT_COUNT; ++i) {
        cat[i] = LINK_CAT_OF(i, 29u + i, LINK_PIN_FREE);
    }
    CHECK(outbind_learn_board(4244u, cat));
    uint16_t sh[LINK_SH_COUNT];
    sh[LINK_SH_WIDTH_CMM]  = 19u * 254u;
    sh[LINK_SH_HEIGHT_CMM] = 1900u;
    sh[LINK_SH_LAYOUT]     = LINK_SH_LAYOUT_OF(LINK_SH_BOTTOM_LEFT, 20);
    sh[LINK_SH_PITCH_CMM]  = 254u;
    sh[LINK_SH_INSET_CMM]  = 100u;
    CHECK(outbind_learn_shape(4244u, sh));
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4244u);
    hold_binding(&b);
    view()->enter();
    const gfx_rect_t r = picker_screen_last_rect();
    CHECK_EQ(r.x, OB_CLR_X);
    CHECK_EQ(r.y + r.h, OU_FOOT);
    for (uint8_t i = 0; i < 12u; ++i) {
        gfx_rect_t k;
        if (picker_screen_button_rect(i, &k)) {
            CHECK(!overlap(r, k));
        }
    }
    frame();
}

/** @brief @p s seconds of frames of 0.25s, as the hold counts them. */
static void run_for(float seconds)
{
    for (float t = 0.0f; t < seconds; t += 0.25f) {
        view()->tick(0.25f);
    }
}

/** @brief Reads pages no binding describes, on the bench's board. */
static void read_odd(void)
{
    static bind_reading_t r;
    outbind_init(&r.bind);
    outbind_set_board(&r.bind, BOARD);
    r.state = BIND_READ_ODD;
    outputs_screen_set_reading(&r);
    view()->tick(0.016f);
}

TEST_CASE(the_unbind_key_holds_to_write_no_pin_in_the_board_view)
{
    visit_bench();
    CHECK(!outputs_unbind_offered());
    /* Not offered: a press there is nothing. */
    const gfx_rect_t k = gfx_rect_make(OB_CLR_X, OB_CLR_Y, OB_CLR_W, OB_CLR_H);
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    run_for(3.0f);
    send(TOUCH_EVENT_UP, k.x + 10, k.y + 10);
    CHECK_EQ(s_applied, 0);

    read_odd();
    CHECK(outputs_unbind_offered());
    CHECK_EQ(outputs_unbind_rect().x, k.x);
    CHECK_STR_EQ(reason(), ui_tr(TX_OUT_NOT_MAPPED));
    frame();
    /* It is not over a key, and the table stands above it. */
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        gfx_rect_t bk;
        if (picker_screen_button_rect(i, &bk)) {
            CHECK(!overlap(k, bk));
        }
    }
    /* Released early: nothing. */
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    run_for(0.5f);
    send(TOUCH_EVENT_UP, k.x + 10, k.y + 10);
    run_for(3.0f);
    CHECK_EQ(s_applied, 0);
    /* Slid off: nothing. */
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    run_for(0.5f);
    send(TOUCH_EVENT_MOVE, k.x - 200, k.y - 200);
    run_for(0.5f);
    run_for(3.0f);
    CHECK_EQ(s_applied, 0);
    send(TOUCH_EVENT_UP, k.x - 200, k.y - 200);
    /* Lost touch: nothing. */
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    view()->cancel();
    run_for(3.0f);
    CHECK_EQ(s_applied, 0);
    /* The list open over it: a press shuts the list and holds nothing. */
    press(picker_screen_list_face());
    CHECK(picker_screen_list_open());
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    CHECK(!picker_screen_list_open());
    run_for(3.0f);
    send(TOUCH_EVENT_UP, k.x + 10, k.y + 10);
    CHECK_EQ(s_applied, 0);
    /* Held for 2s: a binding with no pin, once. */
    send(TOUCH_EVENT_DOWN, k.x + 10, k.y + 10);
    run_for(1.0f);
    frame();
    CHECK_EQ(s_applied, 0);
    run_for(1.25f);
    CHECK_EQ(s_applied, 1);
    CHECK_EQ(outbind_chosen_total(&s_last), 0u);
    send(TOUCH_EVENT_UP, k.x + 10, k.y + 10);
    /* A PROG PORT entry offers none: it writes no binding. */
    read_odd();
    outputs_screen_select_port(0u);
    view()->tick(0.016f);
    CHECK(!outputs_unbind_offered());
}

TEST_CASE(the_in_use_table_ends_above_the_unbind_key)
{
    visit_bench();
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    static const struct { const char *proto; uint8_t gp; } k[8] = {
        { "SERVO PWM", 0 },      { "SERVO PWM", 4 },
        { "MOTOR PWM", 5 },      { "MOTOR PWM", 7 },
        { "DSHOT300", 6 },       { "DSHOT600", 14 },
        { "DSHOT300 BIDIR", 1 }, { "DSHOT600 BIDIR", 2 },
    };
    for (unsigned i = 0; i < 8u; ++i) {
        outbind_set_proto(&b, proto_named(k[i].proto));
        CHECK(outbind_toggle(&b, idx(k[i].gp)));
    }
    outbind_set_proto(&b, 0u);
    hold_binding(&b);
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_set(SET_PDMINI_EN, 1.0f);
    settings_set(SET_PDMINI_TX, 19.0f);
    settings_set(SET_PDMINI_RX, 18.0f);
    settings_set(SET_TONE_EN, 1.0f);
    read_odd();
    /* The reading keeps the binding confirmed on the same board. */
    CHECK(outputs_unbind_offered());
    picker_use_t u;
    int bottom = 0;
    for (uint8_t i = 0; picker_screen_in_use(i, &u); ++i) {
        bottom = u.rect.y + u.rect.h;
    }
    CHECK(bottom > 0);
    CHECK(bottom <= OB_CLR_Y - 8);
    frame();
}

/* ---------------------------------------------------------------- the cat */

/** @brief Whether the frame drawn shows a cat, one of the 8 poses, standing above
 *  the alert band. */
static bool cat_seen(void)
{
    frame();
    gfx_rect_t box;
    int pose;
    if (!picker_screen_cat(&box, &pose)) {
        return false;
    }
    CHECK(pose >= 0 && pose < UI_CAT_POSES);
    CHECK(box.y + box.h <= OU_FOOT);
    return true;
}

TEST_CASE(a_cat_is_drawn_in_a_protocols_view_and_a_ports_in_both_languages)
{
    static const ui_lang_t k_lang[] = { UI_LANG_EN, UI_LANG_DE };
    for (unsigned l = 0; l < 2u; ++l) {
        for (uint32_t seed = 1u; seed <= 64u; ++seed) {
            visit_bench();
            ui_text_set_language(k_lang[l]);
            ui_cats_seed(seed);
            view()->enter();
            outputs_screen_select(proto_named("SERVO PWM"));
            view()->tick(0.016f);
            if (!cat_seen()) {
                T_FAIL("no cat on SERVO PWM, seed %u", (unsigned)seed);
            }

            /* The rig's PROG PORT from its TARGET row, the tester on it. */
            visit_bench();
            ui_text_set_language(k_lang[l]);
            settings_set(SET_SERVO_TARGET, 1.0f);
            settings_set(SET_SERVO_PORT_PWM, 1.0f);
            ui_cats_seed(seed);
            outputs_screen_open_on_port(0u);
            view()->enter();
            view()->tick(0.016f);
            CHECK_EQ(picker_screen_port_shown(), 0);
            if (!cat_seen()) {
                T_FAIL("no cat on PROG PORT 1, seed %u", (unsigned)seed);
            }
            gfx_rect_t box;
            CHECK(picker_screen_cat(&box, NULL));
            /* It stands right of the keys, in the free right column. */
            CHECK(box.x >= OB_LIST_W);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* Every pose that takes the right spot fits the port view: no size grew. */
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        CHECK(ui_cat_poses[p].art.w <= 180);
        CHECK(ui_cat_poses[p].art.h <= 175);
    }
}

TEST_CASE(a_cat_moves_when_the_entrys_column_covers_it_and_stays_when_it_fits)
{
    /* Forced to the sleeping pose, which only the left spot takes: OFF shows
     * it, the entry's column covers that spot and hides it. */
    int sleep = -1;
    for (int p = 0; p < UI_CAT_POSES; ++p) {
        if (strcmp(ui_cat_poses[p].name, "sleep_grey") == 0) { sleep = p; }
    }
    CHECK(sleep >= 0);
    visit_bench();
    ui_cats_force(sleep);
    view()->enter();
    view()->tick(0.016f);
    CHECK(cat_seen());
    outputs_screen_select_port(0u);
    view()->tick(0.016f);
    CHECK(!cat_seen());
    ui_cats_force(UI_CAT_RANDOM);

    /* Random: every roll that picked the left spot on OFF has a cat on the
     * right after PROG PORT is picked. */
    int moved = 0, kept = 0;
    for (uint32_t seed = 1u; seed <= 64u; ++seed) {
        visit_bench();
        ui_cats_seed(seed);
        view()->enter();
        view()->tick(0.016f);
        CHECK(cat_seen());
        gfx_rect_t was;
        int pose_was;
        CHECK(picker_screen_cat(&was, &pose_was));
        outputs_screen_select(proto_named("SERVO PWM"));
        view()->tick(0.016f);
        /* SERVO PWM's table of 3 rows leaves both spots free: same cat. */
        CHECK(cat_seen());
        gfx_rect_t same;
        int pose_same;
        CHECK(picker_screen_cat(&same, &pose_same));
        CHECK_EQ(pose_same, pose_was);
        CHECK_EQ(same.x, was.x);

        outputs_screen_select_port(0u);
        view()->tick(0.016f);
        CHECK(cat_seen());
        gfx_rect_t now;
        int pose_now;
        CHECK(picker_screen_cat(&now, &pose_now));
        CHECK(now.x >= OB_LIST_W);
        if (pose_now == pose_was) { ++kept; } else { ++moved; }
    }
    CHECK(moved > 0);
    CHECK(kept > 0);
}

TEST_CASE(the_grid_view_draws_no_cat)
{
    fresh_view();
    learn_board(4242u, false);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, 4242u);
    hold_binding(&b);
    view()->enter();
    CHECK(!outputs_view_is_board());
    frame();
    CHECK(!picker_screen_cat(NULL, NULL));

    fresh_view();
    ui_board_set((uint16_t)OUTBIND_BOARD_RBMOD);
    outbind_init(&b);
    outbind_set_board(&b, (uint16_t)OUTBIND_BOARD_RBMOD);
    hold_binding(&b);
    view()->enter();
    CHECK(!outputs_view_is_board());
    frame();
    CHECK(!picker_screen_cat(NULL, NULL));
    ui_board_set((uint16_t)OUTBIND_BOARD_UNKNOWN);
}

/* --------------------------------------------------------------- repaint */

TEST_CASE(a_change_the_board_view_shows_repaints_it)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    guard_canvas_t g;
    gfx_canvas_t c;
    visit_port(0u);
    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
    view()->render(&c, 0);
    const uint32_t plain = guard_canvas_sum(&g);
    /* Nothing changed: nothing drawn, whatever the frame buffer holds. */
    view()->render(&c, 0);
    CHECK_EQ(guard_canvas_sum(&g), plain);
    /* A watch label set from the sensor map repaints the column. */
    outputs_screen_set_port_watch(0u, "INA3221 0x41 CH2");
    view()->render(&c, 0);
    CHECK(guard_canvas_sum(&g) != plain);
    /* And the tester's setting, set from SERVO, at the next tick. */
    const uint32_t watched = guard_canvas_sum(&g);
    settings_set(SET_SERVO_TARGET, 1.0f);
    view()->tick(0.016f);
    view()->render(&c, 0);
    CHECK(guard_canvas_sum(&g) != watched);
    CHECK_EQ(guard_canvas_touched(&g), 0u);
    outputs_screen_set_port_watch(0u, "");

    /* The last write repaints a protocol's view. */
    visit_bench();
    outputs_screen_set_result(OUTPUTS_OK);
    view()->render(&c, 0);
    const uint32_t ok = guard_canvas_sum(&g);
    outputs_screen_set_result(OUTPUTS_REFUSED);
    view()->render(&c, 0);
    CHECK(guard_canvas_sum(&g) != ok);
}

int main(void)
{
    RUN(a_drawn_board_gets_the_board_and_any_other_the_grid);
    RUN(a_view_changed_during_a_visit_drops_the_press_and_keeps_the_selection);
    RUN(a_target_opens_the_board_view_on_its_entry_and_home_returns_to_it);
    RUN(the_port_rows_act_in_the_column);
    RUN(a_locked_row_takes_the_press_and_does_nothing);
    RUN(a_key_asks_for_the_ports_pin_and_the_column_names_it);
    RUN(the_column_holds_every_reason_and_meets_no_key);
    RUN(last_write_shows_the_result_and_leaves_both_cat_spots_free);
    RUN(a_board_with_a_key_there_has_last_write_at_the_foot);
    RUN(the_unbind_key_holds_to_write_no_pin_in_the_board_view);
    RUN(the_in_use_table_ends_above_the_unbind_key);
    RUN(a_cat_is_drawn_in_a_protocols_view_and_a_ports_in_both_languages);
    RUN(a_cat_moves_when_the_entrys_column_covers_it_and_stays_when_it_fits);
    RUN(the_grid_view_draws_no_cat);
    RUN(a_change_the_board_view_shows_repaints_it);
    return test_summary("outputs_view");
}
