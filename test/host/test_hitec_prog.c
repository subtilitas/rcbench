/*
 * The Hitec programmer's model (shared/bench/hitec_prog.c) on the whole
 * loopback: CONNECT, the entry and the read of the full set for a D series
 * and a classic servo, READ ALL, LEAVE; every refusal of CONNECT, each
 * with nothing sent; the programming voltage and current at their bounds;
 * a command the control task does not take in time; the ids after a model
 * that starts again; the clock across its wrap.  The rig is
 * hitec_prog_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_prog_rig.h"

/* ------------------------------------------------------------- connect */

TEST_CASE(a_d_series_connect_enters_reads_all_and_names_the_d485hw)
{
    size_t n = 0;
    const uint8_t *list = hitec_d_read_list(&n);

    rig();
    servo_d485();
    CHECK(!hitec_prog_holds_port(model()));
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_OPENING);
    CHECK(hitec_prog_holds_port(model()));
    settled();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_message(model())->arg, 34485u);
    CHECK_EQ(hitec_prog_message(model())->reg, fix_d485()[0x32u / 2u]);
    CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_D485HW);
    CHECK_EQ(n, 21u);
    for (size_t i = 0; i < n; ++i) {
        CHECK_EQ(hitec_image_reg(hitec_prog_servo(model()), list[i]),
                 fix_d485()[list[i] / 2u]);
    }
    /* The port is this panel's, in programming mode, on the tester's pin,
     * the supply on at the programming voltage. */
    CHECK(hitec_prog_holds_port(model()));
    CHECK(hitec_prog_in_mode(model()));
    CHECK(hitec_prog_port_taken(&g_f.rd));
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
    CHECK_EQ(g_hw.takes, 1u);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK_EQ(g_hw.take_family, HITEC_D);
    CHECK_EQ(hitec_prog_pin(model()), PIN);
    CHECK_EQ(hitec_prog_port(model()), CH);
    CHECK(g_hw.v.on);
    CHECK_EQ(g_hw.v.set_mv, HITEC_PROG_SUPPLY_MV);
    CHECK_EQ(g_hw.v.set_ma, HITEC_PROG_SUPPLY_MA);
    /* The entry's POWER with an off time of 0ms: off, and on again. */
    CHECK_EQ(g_hw.offs, 1u);
    CHECK_EQ(g_hw.ons, 1u);
    /* The series stays while the port is held. */
    CHECK(!hitec_prog_series_set(model(), HITEC_CLASSIC));
    CHECK_EQ(hitec_prog_series(model()), HITEC_D);
}

TEST_CASE(a_d_series_servo_without_a_pd_mini_runs_on_the_supply_as_it_is)
{
    rig_at(SUPPLY_NONE, false);
    servo_d485();
    CHECK(!hitec_prog_rail(model()));
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_D485HW);
    CHECK_EQ(g_hw.sets, 0u);
}

TEST_CASE(a_classic_connect_cycles_the_supply_enters_and_reads_100_cells)
{
    /* At a frame of 16ms and at one of 50ms the servo's second `+`, 250ms
     * after its first, is heard. */
    static const unsigned frames[] = { 16u, 50u };

    for (size_t k = 0; k < sizeof(frames) / sizeof(frames[0]); ++k) {
        size_t runs = 0;
        const hitec_range_t *r = hitec_c_read_ranges(&runs);
        unsigned cells = 0;

        rig();
        g_frame_ms = frames[k];
        servo_hs5645();
        CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
        connected();
        CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
        CHECK_EQ(hitec_prog_message(model())->reg, 0x16u);
        CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_HS5645MG);
        CHECK_EQ(g_hw.take_family, HITEC_CLASSIC);
        for (size_t i = 0; i < runs; ++i) {
            for (unsigned c = r[i].first; c < r[i].first + r[i].count;
                 ++c) {
                CHECK_EQ(hitec_image_cell(hitec_prog_servo(model()),
                                          (uint8_t)c),
                         g_drv.eeprom[c]);
                cells++;
            }
        }
        CHECK_EQ(cells, 100u);
        /* One supply cycle: off, then on. */
        CHECK_EQ(g_hw.offs, 1u);
        CHECK_EQ(g_hw.ons, 1u);
        CHECK(g_hw.v.on);
    }
}

TEST_CASE(read_all_reads_the_set_again_and_says_what_it_found)
{
    unsigned done = 0, total = 0;

    rig();
    servo_d485();
    connected();
    g_drv.dreg[0x32] = 7u;
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    prun(30u);
    CHECK(hitec_prog_progress(model(), &done, &total));
    CHECK_EQ(total, 21u);
    CHECK(done < total);
    until(HITEC_PROG_HELD);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_message(model())->reg, 7u);
    CHECK(!hitec_prog_progress(model(), &done, &total));
    /* A second READ ALL while one runs is refused. */
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_BUSY);
    until(HITEC_PROG_HELD);
}

/* --------------------------------------------------------------- leave */

TEST_CASE(leave_restarts_a_d_series_servo_and_gives_the_port_back)
{
    rig();
    servo_d485();
    connected();
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    CHECK_EQ(pstate(), HITEC_PROG_LEAVING);
    CHECK(hitec_prog_holds_port(model()));
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LEFT);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.gives, 1u);
    CHECK(!hitec_prog_holds_port(model()));
    CHECK(!hitec_prog_port_taken(&g_f.rd));
    /* The supply stays on: no off after the entry's. */
    CHECK(g_hw.v.on);
    CHECK_EQ(g_hw.offs, 1u);
    /* And the series can change again. */
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
}

TEST_CASE(leave_cycles_a_classic_servos_supply_and_gives_the_port_back)
{
    rig();
    servo_hs5645();
    CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
    connected();
    CHECK_EQ(g_hw.offs, 1u);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_LEFT);
    CHECK_EQ(g_hw.offs, 2u);
    CHECK_EQ(g_hw.ons, 2u);
    prun(100u);
    CHECK(g_hw.v.on);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
}

/* ----------------------------------------------------------- refusals */

typedef enum {
    R_ARMED = 0,
    R_ARM_ASKED,
    R_STOP,
    R_TARGET,
    R_NO_PIN,
    R_RX_PENDING,
    R_RX_REFUSED,
    R_C_NO_PD,
    R_C_PD_OFFLINE,
    R_CAPS_V,
    R_CAPS_I,
    R_NO_PAGE,
    R_DOWN,
    R_COUNT
} refusal_t;

/* Each refusal of CONNECT: its message, and nothing sent or taken. */
TEST_CASE(every_refusal_of_connect_sends_nothing)
{
    static const unsigned want[R_COUNT] = {
        [R_ARMED]        = HITEC_PROG_MSG_NEED_DISARM,
        [R_ARM_ASKED]    = HITEC_PROG_MSG_NEED_DISARM,
        [R_STOP]         = HITEC_PROG_MSG_STOP_LATCHED,
        [R_TARGET]       = HITEC_PROG_MSG_NEED_TARGET,
        [R_NO_PIN]       = HITEC_PROG_MSG_NO_PIN,
        [R_RX_PENDING]   = HITEC_PROG_MSG_RX_PENDING,
        [R_RX_REFUSED]   = HITEC_PROG_MSG_RX_PENDING,
        [R_C_NO_PD]      = HITEC_PROG_MSG_NEED_PD_C,
        [R_C_PD_OFFLINE] = HITEC_PROG_MSG_NEED_PD_C,
        [R_CAPS_V]       = HITEC_PROG_MSG_SUPPLY_CAPS,
        [R_CAPS_I]       = HITEC_PROG_MSG_SUPPLY_CAPS,
        [R_NO_PAGE]      = HITEC_PROG_MSG_NO_PAGE,
        [R_DOWN]         = HITEC_PROG_MSG_DOWN,
    };

    for (int r = 0; r < R_COUNT; ++r) {
        const supply_kind_t supply = r == R_C_NO_PD        ? SUPPLY_NONE
                                     : r == R_C_PD_OFFLINE ? SUPPLY_OFFLINE
                                                           : SUPPLY_ON;
        unsigned cmds;

        rig_at(supply, false);
        servo_d485();
        switch (r) {
        case R_ARMED:
            g_armed = true;
            break;
        case R_ARM_ASKED:
            hitec_prog_bench_armed(model(), true);
            break;
        case R_STOP:
            g_stopped = true;
            break;
        case R_TARGET:
            g_target = 0u;
            break;
        case R_NO_PIN:
            g_pin_known = false;
            break;
        case R_RX_PENDING:
            g_rx_mark = (uint8_t)HITEC_RX_LINK_PENDING;
            break;
        case R_RX_REFUSED:
            g_rx_mark = (uint8_t)HITEC_RX_LINK_REFUSED;
            break;
        case R_C_NO_PD:
        case R_C_PD_OFFLINE:
            CHECK(hitec_prog_series_set(model(), HITEC_CLASSIC));
            break;
        case R_CAPS_V:
            /* 1mV under the programming voltage. */
            hitec_prog_supply_caps(model(), HITEC_PROG_SUPPLY_MV - 1u,
                                   3000u);
            break;
        case R_CAPS_I:
            hitec_prog_supply_caps(model(), 6000u,
                                   HITEC_PROG_SUPPLY_MA - 1u);
            break;
        case R_NO_PAGE:
            hitec_link_lost(&g_l);
            hitec_link_came_up(&g_l, LINK_MINOR_HITEC - 1u);
            break;
        default:
            hitec_link_lost(&g_l);
            g_link = false;
            break;
        }
        prun(50u);
        cmds = g_sent;
        CHECK_EQ(hitec_prog_connect(model()), want[r]);
        CHECK_EQ(msg(), want[r]);
        prun(200u);
        CHECK_EQ(g_cmds, 0u);
        CHECK_EQ(g_hw.takes, 0u);
        CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
        CHECK(!hitec_prog_holds_port(model()));
        CHECK_EQ(g_q_n, 0u);
        if (r == R_NO_PAGE || r == R_DOWN) {
            /* No status read either. */
            CHECK_EQ(g_sent, cmds);
        }
    }
}

TEST_CASE(connect_takes_a_supply_at_its_limits_exactly)
{
    rig();
    servo_d485();
    hitec_prog_supply_caps(model(), HITEC_PROG_SUPPLY_MV,
                           HITEC_PROG_SUPPLY_MA);
    connected();
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
}

TEST_CASE(the_programming_voltage_and_current_hold_to_the_pages_bounds)
{
    rig();
    hitec_prog_supply_set(model(), 6000u, 3000u);
    CHECK_EQ(hitec_prog_mv(model()), 6000u);
    CHECK_EQ(hitec_prog_ma(model()), 3000u);
    CHECK(!hitec_prog_mv_capped(model()));
    hitec_prog_supply_set(model(), 6001u, 3001u);
    CHECK_EQ(hitec_prog_mv(model()), 6000u);
    CHECK_EQ(hitec_prog_ma(model()), 3000u);
    CHECK(hitec_prog_mv_capped(model()));
    hitec_prog_supply_set(model(), 4800u, 50u);
    CHECK_EQ(hitec_prog_mv(model()), 4800u);
    CHECK_EQ(hitec_prog_ma(model()), 50u);
    hitec_prog_supply_set(model(), 4799u, 49u);
    CHECK_EQ(hitec_prog_mv(model()), 4800u);
    CHECK_EQ(hitec_prog_ma(model()), 50u);
    CHECK(!hitec_prog_mv_capped(model()));
    hitec_prog_supply_set(model(), 0u, 0u);
    CHECK_EQ(hitec_prog_mv(model()), 4800u);
    CHECK_EQ(hitec_prog_ma(model()), 50u);

    /* An 8.4V setting runs the session at 6.0V. */
    hitec_prog_supply_set(model(), 8400u, 1000u);
    servo_d485();
    connected();
    CHECK_EQ(g_hw.v.set_mv, 6000u);
    CHECK_EQ(g_hw.v.set_ma, 1000u);
}

TEST_CASE(an_open_the_control_task_finds_armed_is_answered_busy)
{
    rig();
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    /* Armed between the model's check and the control task's pass. */
    g_armed = true;
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_BUSY);
    CHECK_EQ(g_cmds, 0u);
    CHECK_EQ(g_hw.takes, 0u);
}

/* ------------------------------------------------------ the command path */

TEST_CASE(a_command_not_taken_in_2s_is_given_up_and_never_carried_out)
{
    rig();
    g_q_hold = true;
    CHECK_EQ(hitec_prog_connect(model()), HITEC_PROG_MSG_NONE);
    prun(HITEC_PROG_START_MS - 20u);
    CHECK_EQ(pstate(), HITEC_PROG_OPENING);
    prun(40u);
    CHECK_EQ(pstate(), HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_TIMEOUT);
    /* The queue lets it through now: its pass is gone. */
    g_q_hold = false;
    prun(200u);
    CHECK_EQ(g_cmds, 0u);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    /* And the next CONNECT runs. */
    servo_d485();
    connected();
}

TEST_CASE(a_model_that_starts_again_numbers_past_the_last_answer)
{
    hitec_prog_cmd_t c;
    uint32_t last;

    rig();
    servo_d485();
    connected();
    last = g_ans_id;
    CHECK(last > 1u);
    /* The render loop starts again; the control task keeps its last
     * answer and outcome. */
    hitec_prog_init(&g_p);
    hitec_prog_gate_set(&g_p, &g_gate);
    frame();
    CHECK_EQ(pstate(), HITEC_PROG_OTHER);
    CHECK_EQ(hitec_prog_release(model()), HITEC_PROG_MSG_NONE);
    CHECK(hitec_prog_take(&g_p, &c));
    CHECK(c.id > last);
    g_q[g_q_n++] = c;
    until(HITEC_PROG_PWM);
    CHECK_EQ(msg(), HITEC_PROG_MSG_RELEASED);
}

TEST_CASE(a_session_runs_across_the_clocks_wrap)
{
    /* The millisecond clock passes 2^32 during the entry. */
    rig_from(0xFFFFFFFFu - 2400u, SUPPLY_ON, false);
    servo_d485();
    connected();
    CHECK(g_ms < 0x10000000u);
    CHECK_EQ(msg(), HITEC_PROG_MSG_READ);
    CHECK_EQ(hitec_prog_model(model())->model, HITEC_MODEL_D485HW);
    CHECK_EQ(hitec_prog_leave(model()), HITEC_PROG_MSG_NONE);
    until(HITEC_PROG_PWM);
}

int main(void)
{
    RUN(a_d_series_connect_enters_reads_all_and_names_the_d485hw);
    RUN(a_d_series_servo_without_a_pd_mini_runs_on_the_supply_as_it_is);
    RUN(a_classic_connect_cycles_the_supply_enters_and_reads_100_cells);
    RUN(read_all_reads_the_set_again_and_says_what_it_found);
    RUN(leave_restarts_a_d_series_servo_and_gives_the_port_back);
    RUN(leave_cycles_a_classic_servos_supply_and_gives_the_port_back);
    RUN(every_refusal_of_connect_sends_nothing);
    RUN(connect_takes_a_supply_at_its_limits_exactly);
    RUN(the_programming_voltage_and_current_hold_to_the_pages_bounds);
    RUN(an_open_the_control_task_finds_armed_is_answered_busy);
    RUN(a_command_not_taken_in_2s_is_given_up_and_never_carried_out);
    RUN(a_model_that_starts_again_numbers_past_the_last_answer);
    RUN(a_session_runs_across_the_clocks_wrap);
    return test_summary("hitec_prog");
}
