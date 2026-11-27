/*
 * The balance node's own name on the panel (node_cfg_link.h through
 * bal_ctl.h) against the scripted host stack and node of bal_rig.h.
 *
 * The failures this is about: a CONFIG write that crowds the link the Dial
 * shares (closer than BAL_CTL_MS to another write), a node built before
 * CONFIG that loses its link or its reports, a name written while ARMED,
 * and a name shown that is not the node's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bal_rig.h"

/** @brief The view now. */
static bal_view_t view(const brig_t *r)
{
    bal_view_t v;
    bal_ctl_view(&r->c, r->now, &v);
    return v;
}

TEST_CASE(config_goes_once_a_link_paced_after_blades_and_rpm)
{
    brig_t r;
    brig_up(&r);
    brig_run(&r, 1000u);
    CHECK_EQ(r.cfgs, 1u);
    CHECK(r.nw >= 2u);
    /* At least BAL_CTL_MS from each other write. */
    for (unsigned i = 0u; i < r.nw; ++i) {
        const uint32_t d = r.cfg_ms > r.w_ms[i] ? r.cfg_ms - r.w_ms[i]
                                                : r.w_ms[i] - r.cfg_ms;
        CHECK(d >= BAL_CTL_MS);
    }
    bal_view_t v = view(&r);
    CHECK(v.have_cfg);
    CHECK_EQ(rcnode_name_role(v.node_name), RCNODE_ROLE_BALANCE);
    brig_run(&r, 2000u);
    CHECK_EQ(r.cfgs, 1u);
}

TEST_CASE(a_name_goes_while_safe_and_shows)
{
    brig_t r;
    brig_up(&r);
    brig_run(&r, 1000u);
    CHECK_EQ(bal_ctl_name(&r.c, "PROP 13X6", true), NCL_REQ_ARMED);
    CHECK_EQ(bal_ctl_name(&r.c, "bad", false), NCL_REQ_BAD);
    CHECK_EQ(bal_ctl_name(&r.c, "PROP 13X6", false), NCL_REQ_OK);
    r.armed = true;
    brig_run(&r, 500u);
    CHECK_EQ(r.names, 0u);           /* not while ARMED */
    r.armed = false;
    brig_run(&r, 500u);
    CHECK_EQ(r.names, 1u);
    CHECK_STR_EQ(view(&r).node_name, "PROP 13X6");
}

TEST_CASE(an_older_node_keeps_its_link_and_its_reports)
{
    brig_t r;
    brig_init(&r);
    r.old_node = true;
    brig_run(&r, 1000u);
    CHECK_EQ(r.cfgs, 1u);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK(!view(&r).have_cfg);
    CHECK_EQ(bal_ctl_name(&r.c, "X", false), NCL_REQ_UNKNOWN);
    const rcnode_bal_report_t rep = brig_steady(120u, 900u, 9000u);
    brig_report(&r, &rep);
    CHECK_EQ(r.c.reports, 1u);
    CHECK_EQ(r.c.bad, 0u);
    brig_run(&r, 2000u);
    CHECK_EQ(r.cfgs, 1u);
}

TEST_CASE(config_on_stream_is_no_bad_report)
{
    brig_t r;
    brig_up(&r);
    brig_run(&r, 1000u);
    CHECK(view(&r).have_cfg);
    CHECK_EQ(r.c.bad, 0u);
    CHECK_EQ(r.c.reports, 0u);
}

int main(void)
{
    RUN(config_goes_once_a_link_paced_after_blades_and_rpm);
    RUN(a_name_goes_while_safe_and_shows);
    RUN(an_older_node_keeps_its_link_and_its_reports);
    RUN(config_on_stream_is_no_bad_report);
    return test_summary("bal_cfg");
}
