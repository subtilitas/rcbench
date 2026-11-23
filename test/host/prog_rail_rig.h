/*
 * The bench the prog_rail suites run on: the POWER page
 * (shared/outputs/power_page.c), the session's rail (prog_rail.c) and the
 * real SUPPLY page over a PD mini that reads back what it is asked in the
 * same pass, wired as firmware/iomcu/src/iomcu_rsn.c wires them: the
 * session's supply view and request go through prog_rail_view() and
 * prog_rail_set(), the panel's POWER writes through prog_rail_write().
 *
 * For test_prog_rail.c, test_prog_rail_kst.c and test_prog_rail_hitec.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"
#include "power_page.h"
#include "prog_rail.h"
#include "supply_page.h"

/** The rail's voltage the panel set, mV, and its current limit, mA. */
#define RAIL_MV 5000u
#define RAIL_MA 2000u

static power_page_t    g_pw;
static prog_rail_t     g_rail;
static prog_rail_ctx_t g_rctx;
static supply_page_t   g_sp;
static pdmini_t        g_pd;
static outputs_t       g_sout;

/* ------------------------------------------------------------- the pages */

static inline uint16_t pw_reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    power_read(&g_pw, (uint8_t)i, 1u, &v);
    return v;
}

/* PORT_ON as driven. */
static inline uint16_t ports_on(void)
{
    return power_port_on(&g_pw);
}

/* The panel's POWER write of one register, through the session's rules. */
static inline uint8_t pw_write(unsigned r, uint16_t v, uint32_t now)
{
    return prog_rail_write(&g_rail, &g_pw, (uint8_t)r, 1u, &v, &g_rctx, now);
}

/* The PD mini's set point and output, as the SUPPLY page asks them. */
static inline bool pd_wants_on(void)
{
    return g_pd.want_output;
}

/* One pass of the SUPPLY page and the module. */
static inline void sp_pass(bool safe)
{
    supply_page_step(&g_sp, safe, &g_pd);
    g_pd.st.output = g_pd.want_output;
    g_pd.st.set_mv = g_pd.want_mv;
    g_pd.st.set_ma = g_pd.want_ma;
}

/* The panel's SUPPLY OFF, as error_act() has supply_link_cut() write it. */
static inline void sp_panel_off(void)
{
    const uint16_t off = 0u;
    (void)supply_page_write(&g_sp, LINK_SP_OUTPUT, 1u, &off, &g_sout, true);
}

/* The session's supply view and request: iomcu_rsn.c's. */
static inline void rail_view(supply_port_view_t *out)
{
    supply_port_view_t pd;
    supply_page_port_view(&g_sp, &pd);
    prog_rail_view(&g_rail, &g_pw, &pd, out);
}

static inline bool rail_set(bool on, uint16_t mv, uint16_t ma)
{
    return prog_rail_set(&g_rail, &g_pw, &g_sp, on, mv, ma, &g_rctx);
}

/* The bench at its start: the POWER page with or without the switches,
 * everything a RAIL 1 wants, the session on port @p k, the PD mini wired
 * and answering, its output on at RAIL_MV when @p pd_on. */
static inline void rig_start(bool switches, int k, bool pd_on)
{
    static const uint16_t k_wiring[4] = { 1u, 8u, 9u, 1u };
    const uint16_t on[3] = { 1u, RAIL_MV, RAIL_MA };

    power_init(&g_pw, switches, true);
    prog_rail_init(&g_rail);
    prog_rail_port(&g_rail, k);
    memset(&g_rctx, 0, sizeof(g_rctx));
    g_rctx.power.beat_alive  = true;
    g_rctx.power.limits_held = true;
    outputs_init(&g_sout, 0u);
    outputs_reserve_pins(&g_sout, (uint64_t)1u << 3);
    supply_page_init(&g_sp);
    pdmini_init(&g_pd, NULL, 0u);
    g_pd.answered = true;
    g_pd.state_known = true;
    g_pd.st.online = true;
    (void)supply_page_write(&g_sp, LINK_SP_ENABLE, 4u, k_wiring, &g_sout,
                            true);
    if (pd_on) {
        (void)supply_page_write(&g_sp, LINK_SP_OUTPUT, 3u, on, &g_sout,
                                true);
    }
    sp_pass(true);
    sp_pass(true);
}

/* A window of the sensor schedule with every port at @p mv. */
static inline void rig_window(uint16_t number, int32_t mv)
{
    int32_t v[LINK_PW_PORTS];
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) {
        v[k] = mv;
    }
    (void)power_window(&g_pw, 0u, number, v, LINK_PW_PORT_ALL, RAIL_MV);
}
