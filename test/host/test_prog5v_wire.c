/*
 * The PROG 5V watch as the panel's control task drives it
 * (firmware/panel/main/panel_sense.c, prog5v_publish_reading()): the rbmod
 * carrier's PROG_SUPPLY page as read (prog_supply_link.h), SENSE_WIN_A with
 * 0x40 CH3 (channel 2) watching the ports' 5V (sense_win_link.h), the watch
 * (prog5v_lost.h), and its trip acted on through error_sites_stop() as
 * bench_error_stop() acts on it.
 *
 * Under test: a collapse under the panel's ON is one error (DISARM of an
 * ARMED bench, the supply off, the PROG_SUPPLY OFF owed) and none again on
 * the same ON; an OFF written and read, and an ON again, is a new ON.  No
 * error for the operator's OFF, which the page reads on until its next
 * read; none while a programming port holds the 5V; none while the
 * heartbeat is withheld (a STOP), which switches the 5V off at the IOmcu.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "error_sites.h"
#include "link_pages.h"
#include "prog5v_lost.h"
#include "prog_supply_link.h"
#include "sense_win_link.h"

/* The poll's period and the windows' voltages, mV. */
#define POLL_MS 50u
#define MV_GOOD 5000u
#define MV_DEAD 100u

static prog_supply_link_t g_ps;
static sense_win_link_t   g_sw;
static prog5v_lost_t      g_p5;
static uint32_t           g_now;
static uint16_t           g_win;     /* the IOmcu's next window number    */
static bool               g_beat;    /* the heartbeat edges               */
static unsigned           g_trips;   /* the watch's trips                 */
static unsigned           g_disarms; /* DISARM requested                  */
static unsigned           g_offs;    /* the supply's output off           */

/**
 * @brief A read of PROG_SUPPLY: the pin on or off.
 * @param on   the pin drives the 5V on
 * @param port a programming port holds it
 */
static void ps_read(bool on, bool port)
{
    uint16_t r[LINK_PS_COUNT] = { 0 };
    r[LINK_PS_FLAGS] = (uint16_t)(LINK_PS_FITTED | (on ? LINK_PS_ON : 0u)
                                  | (port ? LINK_PS_PORT : 0u));
    r[LINK_PS_PIN]   = 21u;
    r[LINK_PS_PANEL] = 1u;
    prog_supply_link_read(&g_ps, r, g_now);
}

/**
 * @brief One poll: SENSE_WIN_A read with the next window at @p mv on CH3,
 *        the watch stepped, and its trip acted on as bench_error_stop()
 *        acts on an ARMED bench with the PD mini on.
 * @param mv the window's mean bus voltage, mV
 */
static void pass(uint16_t mv)
{
    sense_win_link_op_t op;
    if (sense_win_link_next(&g_sw, g_now, &op)) {
        uint16_t r[LINK_SX_COUNT] = { 0 };
        r[LINK_SX_WINDOW] = g_win++;
        r[LINK_SX_FLAGS]  = (uint16_t)(LINK_SX_HAVE | 0x01u);
        r[LINK_SX_E(2, LINK_SX_E_MEAN_MV)] = mv;
        r[LINK_SX_E(2, LINK_SX_E_FLAGS)] =
            (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE);
        sense_win_link_done(&g_sw, SENSE_WIN_LINK_ACK, r);
    }
    prog5v_window_t w = { 0 };
    int16_t ma = 0;
    w.ok = sense_win_link_last(&g_sw, 2u, &w.mv, &ma, &w.window);
    /* prog5v_panel_on(): the page reads on, no port holds it, the beat. */
    const bool on = prog_supply_link_state(&g_ps) == PROG_SUPPLY_ON
                    && !prog_supply_link_port(&g_ps) && g_beat;
    if (prog5v_lost_step(&g_p5, on, prog_supply_link_cuts(&g_ps), g_now,
                         &w)) {
        ++g_trips;
        const error_bench_t b = {
            true, true, false, true, true, LINK_MINOR_PROG_SUPPLY, true,
        };
        const error_act_t a = error_sites_stop(&b);
        g_disarms += a.disarm ? 1u : 0u;
        g_offs    += (a.supply_off || a.off_first) ? 1u : 0u;
        if (a.prog_off) {
            prog_supply_link_cut(&g_ps);
        }
    }
    g_now += POLL_MS;
}

/**
 * @brief @p n polls at @p mv.
 * @param n  the polls
 * @param mv the windows' mean bus voltage, mV
 */
static void passes(int n, uint16_t mv)
{
    for (int i = 0; i < n; ++i) {
        pass(mv);
    }
}

/** @brief The carrier's link up, PROG 5V read on, the heartbeat edging. */
static void up(void)
{
    g_now = 1000u;
    g_win = 1u;
    g_beat = true;
    g_trips = g_disarms = g_offs = 0u;
    prog_supply_link_init(&g_ps, true);
    sense_win_link_init(&g_sw);
    sense_win_link_came_up(&g_sw, LINK_MINOR_SENSE_MAP);
    sense_win_link_want(&g_sw, 0x01u);
    prog5v_lost_init(&g_p5);
    ps_read(true, false);
}

TEST_CASE(a_collapse_under_the_panels_on_is_one_error)
{
    up();
    passes(20, MV_GOOD);
    passes(20, MV_DEAD);
    CHECK_EQ(g_trips, 1u);
    CHECK_EQ(g_disarms, 1u);
    CHECK_EQ(g_offs, 1u);
    uint8_t  off = 0u;
    uint16_t v   = 1u;
    CHECK(prog_supply_link_next(&g_ps, &off, &v));
    CHECK_EQ(off, LINK_PS_OUTPUT);
    CHECK_EQ(v, 0u);
    /* The OFF written and read off; the operator's ON again: a new ON with
     * its own 500ms, and its own error. */
    prog_supply_link_written(&g_ps, off, v, PROG_SUPPLY_LINK_ACK);
    ps_read(false, false);
    pass(0u);
    ps_read(true, false);
    passes(9, MV_DEAD);                       /* under 500ms: nothing */
    CHECK_EQ(g_trips, 1u);
    passes(3, MV_DEAD);
    CHECK_EQ(g_trips, 2u);
}

TEST_CASE(the_operators_off_is_no_error)
{
    up();
    passes(20, MV_GOOD);
    prog_supply_link_ask_off(&g_ps);
    prog_supply_link_written(&g_ps, LINK_PS_OUTPUT, 0u, PROG_SUPPLY_LINK_ACK);
    CHECK_EQ(prog_supply_link_state(&g_ps), PROG_SUPPLY_ON);
    passes(4, 0u);                            /* 200ms to the next read */
    ps_read(false, false);
    passes(20, 0u);
    CHECK_EQ(g_trips, 0u);
    CHECK_EQ(g_disarms, 0u);
}

TEST_CASE(a_port_holding_the_5v_is_not_watched)
{
    up();
    ps_read(true, true);
    passes(20, MV_GOOD);
    passes(20, MV_DEAD);                      /* a session's power cycle */
    CHECK_EQ(g_trips, 0u);
}

/* A STOP withholds the heartbeat; the IOmcu switches the 5V off before the
 * page reads so. */
TEST_CASE(a_withheld_heartbeat_is_no_error)
{
    up();
    passes(20, MV_GOOD);
    g_beat = false;
    passes(4, 0u);
    ps_read(false, false);
    passes(20, 0u);
    CHECK_EQ(g_trips, 0u);
}

int main(void)
{
    RUN(a_collapse_under_the_panels_on_is_one_error);
    RUN(the_operators_off_is_no_error);
    RUN(a_port_holding_the_5v_is_not_watched);
    RUN(a_withheld_heartbeat_is_no_error);
    return test_summary("prog5v_wire");
}
