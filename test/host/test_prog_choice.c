/**
 * @file test_prog_choice.c
 * @brief The programming port chosen on the rbmod carrier (prog_choice.h,
 *        protocol 4.36), and the OUTPUTS PROG PORT entry that offers it.
 *
 * Cases:
 * - each_of_the_four_ports_is_chosen_and_kept_over_a_restart
 * - the_choice_is_locked_while_armed_or_a_programmer_holds_a_port
 * - an_older_iomcu_and_a_board_whose_pins_are_chosen_are_unchanged
 * - the_funcs_follow_the_choice_one_ask_at_a_time
 * - progp04_beside_a_bound_pwm04_is_refused_by_name
 * - no_pulse_without_the_prog_5v
 *
 * A chosen port that is not ready stays the destination: prog_link takes no
 * command for the servo outputs in its place.  The lock under an ARM asked
 * is test_kst_panel_screen's.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen_rig.h"

#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_link.h"
#include "ui_board.h"

#define RBMOD ((uint16_t)OUTBIND_BOARD_RBMOD)

/* The carrier's ports and their GPIO (docs/Carrier.md). */
static const uint8_t k_pins[4] = { 13u, 14u, 15u, 0u };

/* ------------------------------------------------------------ the seams */

/* A settings medium in memory: what a restart loads. */
static float g_kept[SETTING_COUNT];
static bool  g_have_kept;

static bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

static bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    return true;
}

static const settings_store_t k_mem = { mem_load, mem_save, NULL, NULL };

/* The asks prog_choice_tick() makes, as the control task takes them. */
static int     g_asks;
static uint8_t g_ask_port;
static uint16_t g_ask_value;

static bool take_ask(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                     uint16_t value)
{
    (void)seq;
    CHECK_EQ(kind, PROG_LINK_ASK_FUNC);
    ++g_asks;
    g_ask_port = port;
    g_ask_value = value;
    return true;
}

static bool g_holds;

static bool hold_target(setting_id_t id)
{
    return g_holds && id == SET_SERVO_TARGET;
}

/* --------------------------------------------------------------- helpers */

/* The carrier's ports as read: every one on its GPIO, FUNC @p f[n]. */
static void view(const uint8_t *f)
{
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    for (unsigned n = 0; n < 4u; ++n) {
        v.pin[n] = k_pins[n];
        v.func[n] = f[n];
    }
    port_view_set(&v);
}

static void all_none(void)
{
    static const uint8_t none[4] = { LINK_PG_FN_NONE, LINK_PG_FN_NONE,
                                     LINK_PG_FN_NONE, LINK_PG_FN_NONE };
    view(none);
}

/* A carrier of protocol @p minor at rest, nothing bound, PROG 5V on. */
static void carrier(uint16_t minor)
{
    fresh();
    g_have_kept = false;
    settings_set_store(&k_mem);
    settings_init();
    g_holds = false;
    settings_set_hold(hold_target);
    servo_screen_set_bench_armed(false);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outputs_screen_set_binding(&b);
    ui_board_set(RBMOD);
    outputs_screen_set_prog_ports(4u, k_pins);
    all_none();
    prog_choice_reset();
    prog_choice_set_far_minor(minor);
    port_view_set_ask(take_ask);
    /* No ask left waiting from a case before. */
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    g_asks = 0;
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
}

static void done(void)
{
    port_view_set_ask(NULL);
    settings_set_hold(NULL);
    settings_set_store(NULL);
    settings_init();
    prog5v_box_reset();
    port_view_t v;
    port_view_unknown(&v);
    port_view_set(&v);
    outputs_screen_set_prog_ports(OUTPUTS_PORTS_UNKNOWN, NULL);
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    prog_choice_reset();
}

/* Whether a command reaches the servo outputs while the tester wants
 * @p target on the carrier and every port is parked at FUNC NONE: the
 * prog_link the panel runs, at 4.36. */
static bool command_taken(uint8_t target)
{
    uint16_t page[LINK_PG_REGS];
    memset(page, 0, sizeof(page));
    page[LINK_PG_COUNT] = 4u;
    for (unsigned n = 0; n < 4u; ++n) {
        page[LINK_PG_REG(n, LINK_PG_PIN)] = k_pins[n];
        page[LINK_PG_FUNC(n)] = (uint16_t)LINK_PG_FN_NONE;
        page[LINK_PG_FLAGS] |= LINK_PG_F_PARKED(n);
    }
    prog_link_t pl;
    prog_link_init(&pl);
    prog_link_came_up(&pl, LINK_MINOR_CARRIER_PWM);
    prog_link_read(&pl, page, 0u);
    prog_link_want(&pl, target, prog_choice_want_pwm());
    CHECK_EQ(prog_link_wanted(&pl), 0u);
    CHECK_EQ(prog_link_drive(&pl).target, target);
    return prog_link_command(&pl, false);
}

/* ----------------------------------------------------------------- cases */

TEST_CASE(each_of_the_four_ports_is_chosen_and_kept_over_a_restart)
{
    char line[64];

    carrier(LINK_MINOR_CARRIER_PWM);
    CHECK(prog_choice_offered());
    CHECK_EQ(prog_choice_port(), 0u);
    for (uint8_t port = 1u; port <= 4u; ++port) {
        CHECK(prog_choice_pick(port));
        CHECK_EQ(settings_get_int(SET_SERVO_TARGET), port);
        CHECK(settings_save_asked());
        CHECK(settings_save());
        /* A restart: the values from the medium, the far end anew. */
        settings_set(SET_SERVO_TARGET, 0.0f);
        settings_init();
        prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
        CHECK_EQ(prog_choice_port(), port);
        prog_choice_entry(port, line, sizeof(line));
        char want[32];
        snprintf(want, sizeof(want), "PROGP%02u  GPIO %u", (unsigned)port,
                 (unsigned)k_pins[port - 1u]);
        CHECK_STR_EQ(line, want);
    }
    /* OUTPUTS again, and past the last port nothing. */
    CHECK(prog_choice_pick(0u));
    CHECK_EQ(prog_choice_port(), 0u);
    CHECK(!prog_choice_pick(5u));
    CHECK_EQ(prog_choice_port(), 0u);
    done();
}

TEST_CASE(the_choice_is_locked_while_armed_or_a_programmer_holds_a_port)
{
    char line[64];

    carrier(LINK_MINOR_CARRIER_PWM);
    CHECK(prog_choice_pick(2u));
    CHECK(!prog_choice_lock_line(line, sizeof(line)));
    CHECK_STR_EQ(line, "");

    servo_screen_set_bench_armed(true);
    CHECK(prog_choice_locked());
    CHECK(!prog_choice_pick(3u));
    CHECK_EQ(prog_choice_port(), 2u);
    CHECK(prog_choice_lock_line(line, sizeof(line)));
    CHECK_STR_EQ(line, TR(OUT_PORT_LOCKED));
    servo_screen_set_bench_armed(false);

    g_holds = true;
    CHECK(prog_choice_locked());
    CHECK(!prog_choice_pick(1u));
    CHECK_EQ(prog_choice_port(), 2u);
    CHECK(prog_choice_lock_line(line, sizeof(line)));
    CHECK_STR_EQ(line, TR(OUT_PORT_HELD));
    g_holds = false;

    CHECK(prog_choice_pick(1u));
    CHECK_EQ(prog_choice_port(), 1u);
    done();
}

TEST_CASE(an_older_iomcu_and_a_board_whose_pins_are_chosen_are_unchanged)
{
    /* A carrier of 4.35: FUNC held at NONE there, nothing offered, and the
     * IOmcu's TARGET stays 0 whatever the setting says. */
    carrier(LINK_MINOR_CARRIER_PWM - 1u);
    CHECK(prog_choice_carrier());
    CHECK(!prog_choice_offered());
    CHECK(!prog_choice_pick(1u));
    settings_set(SET_SERVO_TARGET, 2.0f);
    CHECK_EQ(prog_choice_want_target(), 0u);
    CHECK(!prog_choice_want_pwm());
    prog_choice_tick();
    CHECK_EQ(g_asks, 0);
    CHECK_EQ(prog_choice_text(TX_KP_NEED_TARGET_2), (int)TX_PC_OLD_IOMCU);
    prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
    CHECK_EQ(prog_choice_text(TX_KP_NEED_TARGET_2), (int)TX_PC_NEED_PORT_2);
    CHECK_EQ(prog_choice_text(TX_KP_NEED_TARGET), (int)TX_KP_NEED_TARGET);

    /* The module: the settings pass through, no ask, no refusal, no 5V. */
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    CHECK(!prog_choice_carrier());
    CHECK(!prog_choice_offered());
    settings_set(SET_SERVO_TARGET, 3.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    CHECK_EQ(prog_choice_want_target(), 3u);
    CHECK(prog_choice_want_pwm());
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    CHECK(!prog_choice_want_pwm());
    CHECK(!prog_choice_needs_5v());
    CHECK(!prog_choice_refusal(4u, NULL, 0u));
    prog_choice_tick();
    CHECK_EQ(g_asks, 0);
    CHECK_EQ(prog_choice_text(TX_KP_NEED_TARGET_2), (int)TX_KP_NEED_TARGET_2);
    done();
}

TEST_CASE(the_funcs_follow_the_choice_one_ask_at_a_time)
{
    static const uint8_t on1[4] = { LINK_PG_FN_SERVO_PWM, LINK_PG_FN_NONE,
                                    LINK_PG_FN_NONE, LINK_PG_FN_NONE };
    static const uint8_t on3[4] = { LINK_PG_FN_NONE, LINK_PG_FN_NONE,
                                    LINK_PG_FN_SERVO_PWM, LINK_PG_FN_NONE };

    carrier(LINK_MINOR_CARRIER_PWM);
    view(on1);
    CHECK(prog_choice_pick(3u));
    /* The port stays the destination while its SERVO PWM is asked for: the
     * IOmcu's TARGET waits (the port is parked, not bound) and the tester
     * takes no command but a release, so nothing reaches the outputs. */
    CHECK_EQ(prog_choice_want_target(), 3u);
    CHECK(!command_taken(prog_choice_want_target()));
    /* PROGP01 back to NONE first, so no two ports hold a slice. */
    prog_choice_tick();
    CHECK_EQ(g_asks, 1);
    CHECK_EQ(g_ask_port, 0u);
    CHECK_EQ(g_ask_value, LINK_PG_FN_NONE);
    prog_choice_tick();                       /* it waits: no second ask */
    CHECK_EQ(g_asks, 1);
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    all_none();
    prog_choice_tick();
    CHECK_EQ(g_asks, 2);
    CHECK_EQ(g_ask_port, 2u);
    CHECK_EQ(g_ask_value, LINK_PG_FN_SERVO_PWM);
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_DONE);
    view(on3);
    CHECK_EQ(prog_choice_want_target(), 3u);
    prog_choice_tick();
    CHECK_EQ(g_asks, 2);                      /* all in line: nothing */

    /* A refused ask is not made again until the ports change. */
    CHECK(prog_choice_pick(2u));
    prog_choice_tick();
    CHECK_EQ(g_asks, 3);
    CHECK_EQ(g_ask_port, 2u);
    port_view_set_ask_state(port_view_ask_seq(), PROG_LINK_ASK_REFUSED);
    prog_choice_tick();
    prog_choice_tick();
    CHECK_EQ(g_asks, 3);
    view(on3);                                /* the same read */
    prog_choice_tick();
    CHECK_EQ(g_asks, 3);
    all_none();                               /* the ports moved */
    prog_choice_tick();
    CHECK_EQ(g_asks, 4);
    CHECK_EQ(g_ask_port, 1u);
    CHECK_EQ(g_ask_value, LINK_PG_FN_SERVO_PWM);
    done();
}

TEST_CASE(progp04_beside_a_bound_pwm04_is_refused_by_name)
{
    static const uint8_t on4[4] = { LINK_PG_FN_NONE, LINK_PG_FN_NONE,
                                    LINK_PG_FN_NONE, LINK_PG_FN_SERVO_PWM };
    char line[64];

    carrier(LINK_MINOR_CARRIER_PWM);
    CHECK(!prog_choice_refusal(4u, line, sizeof(line)));
    CHECK(prog_choice_pick(4u));
    view(on4);
    CHECK_EQ(prog_choice_want_target(), 4u);

    /* PWM04 (GPIO 1) bound as a servo output: slice 0 is the binding's. */
    choose_named("SERVO PWM");
    const int pos = 3;                       /* PWM04 (PWM01 is 0) */
    tap(GRID_X + (pos / GRID_ROWS) * (CELL_W + CELL_GAP) + CELL_W / 2,
        GRID_Y + (pos % GRID_ROWS) * (CELL_H + ROW_GAP) + CELL_H / 2);
    CHECK_EQ(s_applied, 1);
    CHECK(prog_choice_refusal(4u, line, sizeof(line)));
    CHECK_STR_EQ(line, "PROGP04 SHARES PWM SLICE 0 WITH PWM04 (GPIO 1)");
    ui_text_set_language(UI_LANG_DE);
    CHECK(prog_choice_refusal(4u, line, sizeof(line)));
    CHECK_STR_EQ(line, "PROGP04 TEILT PWM-SLICE 0 MIT PWM04 (GPIO 1)");
    ui_text_set_language(UI_LANG_EN);
    /* The other ports are free; PROGP04 takes no choice, has no TARGET,
     * and is asked back to NONE. */
    for (uint8_t port = 1u; port <= 3u; ++port) {
        CHECK(!prog_choice_refusal(port, NULL, 0u));
    }
    CHECK(!prog_choice_pick(4u));
    /* PROGP04 stays the destination, without a pulse: no command reaches
     * the outputs in its place. */
    CHECK_EQ(prog_choice_want_target(), 4u);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    CHECK(!prog_choice_want_pwm());
    CHECK(!command_taken(prog_choice_want_target()));
    prog_choice_tick();
    CHECK_EQ(g_asks, 1);
    CHECK_EQ(g_ask_port, 3u);
    CHECK_EQ(g_ask_value, LINK_PG_FN_NONE);
    /* The binding is left as it was. */
    CHECK_EQ(s_applied, 1);
    done();
}

TEST_CASE(no_pulse_without_the_prog_5v)
{
    static const uint8_t on2[4] = { LINK_PG_FN_NONE, LINK_PG_FN_SERVO_PWM,
                                    LINK_PG_FN_NONE, LINK_PG_FN_NONE };

    carrier(LINK_MINOR_CARRIER_PWM);
    CHECK(prog_choice_pick(2u));
    view(on2);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    CHECK(!prog_choice_needs_5v());
    CHECK(prog_choice_want_pwm());
    /* PROG 5V off, by hand or by a cut: the pulse is not asked for. */
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    CHECK(prog_choice_needs_5v());
    CHECK(!prog_choice_want_pwm());
    CHECK_EQ(prog_choice_want_target(), 2u);   /* the port stays chosen */
    prog5v_box_publish(1u, 21u, PROG5V_HELD_NONE);      /* not read */
    CHECK(!prog_choice_want_pwm());
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
    CHECK(prog_choice_want_pwm());
    /* PORT PWM OFF is OFF whatever the 5V. */
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    CHECK(!prog_choice_want_pwm());
    /* PROGP02's entry names the box with PORT PWM OFF too: the tap on ON
     * is refused there and the line says why. */
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);
    tap(OU_POP_X(1), OU_POP_ROW_Y(1));       /* PROG PORT 2 */
    CHECK_EQ(outputs_screen_port_shown(), 1);
    CHECK_STR_EQ(reason(), TR(PC_NEED_5V));
    prog5v_box_publish(3u, 21u, PROG5V_HELD_NONE);
    /* OUTPUTS as the target: no 5V is needed. */
    CHECK(prog_choice_pick(0u));
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    CHECK(!prog_choice_needs_5v());
    done();
}

int main(void)
{
    RUN(each_of_the_four_ports_is_chosen_and_kept_over_a_restart);
    RUN(the_choice_is_locked_while_armed_or_a_programmer_holds_a_port);
    RUN(an_older_iomcu_and_a_board_whose_pins_are_chosen_are_unchanged);
    RUN(the_funcs_follow_the_choice_one_ask_at_a_time);
    RUN(progp04_beside_a_bound_pwm04_is_refused_by_name);
    RUN(no_pulse_without_the_prog_5v);
    return test_summary("prog_choice");
}
