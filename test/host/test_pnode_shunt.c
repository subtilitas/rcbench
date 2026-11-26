/*
 * The panel's SHUNT write to the power node (pnode_ctl.h) against the
 * scripted host stack and node of pnode_rig.h.
 *
 * The failures this is about: a SHUNT sent when INFO already names the
 * shunt, or while ARMED; the meter's full scale changed under a live run;
 * a timeout or a dropped link taken for the node's refusal, which leaves
 * the meter on the old shunt and tells the operator to update a node that
 * took the write; and a refused write sent again and again.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pnode_rig.h"

/** @brief NODE SHUNT set to @p uohm in SETUP. */
static void set_shunt(rig_t *r, uint16_t uohm)
{
    pnode_cfg_t cfg = r->c.cfg;
    cfg.shunt_uohm = uohm;
    pnode_ctl_config(&r->c, &cfg, r->now);
}

TEST_CASE(shunt_goes_when_info_names_another_and_info_is_read_again)
{
    rig_t r;
    up(&r, true);
    CHECK_EQ(r.shunts, 0u);           /* no NODE SHUNT given: none */
    CHECK_EQ(r.info_reads, 1u);
    set_shunt(&r, 200u);              /* INFO's own: none */
    run(&r, 20u);
    CHECK_EQ(r.shunts, 0u);
    set_shunt(&r, 500u);
    run(&r, 20u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.wr[0], RCNODE_OP_SHUNT);
    CHECK_EQ(r.wr[1], 0xF4u);
    CHECK_EQ(r.wr[2], 0x01u);
    /* INFO again: the shunt the node now holds, and the meter's
     * calibration with it. */
    CHECK_EQ(r.info_reads, 2u);
    CHECK(r.c.info_ok);
    CHECK_EQ(r.c.info_shunt, 500u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 500u);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(v.have_info);
    CHECK_EQ(v.i228_addr, 0x45u);
    CHECK_EQ(v.shunt_uohm, 500u);
    run(&r, 500u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.info_reads, 2u);
    /* A link to a node whose store was erased (its shunt and its agreed
     * value the default) gets it again. */
    r.info.shunt_uohm = 200u;
    r.cfg.shunt_agreed = 200u;
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(r.c.linked);
    CHECK_EQ(r.shunts, 2u);
    CHECK_EQ(r.c.info_shunt, 500u);
}

TEST_CASE(shunt_waits_while_armed_and_goes_at_safe)
{
    rig_t r;
    up(&r, true);
    window(&r, RCNODE_WF_ONLINE);
    r.armed = true;
    run(&r, 5u);
    CHECK_EQ(r.writes, 1u);           /* the RUN marker */
    set_shunt(&r, 1000u);
    run(&r, 200u);
    CHECK_EQ(r.shunts, 0u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 200u);
    r.armed = false;
    run(&r, 20u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 1000u);
}

TEST_CASE(a_node_that_refuses_shunt_is_counted_and_not_asked_again)
{
    rig_t r;
    up(&r, true);
    r.shunt_status = PNODE_ATT_NOT_ALLOWED; /* a node built before SHUNT */
    set_shunt(&r, 500u);
    run(&r, 500u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.shunt_refused, 1u);
    CHECK_EQ(r.info_reads, 1u);
    CHECK(r.c.linked);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.shunt_refused, 1u);
    CHECK_EQ(v.shunt_uohm, 200u);
    /* Another value is another try. */
    set_shunt(&r, 600u);
    run(&r, 20u);
    CHECK_EQ(r.shunts, 2u);
    CHECK_EQ(r.c.shunt_refused, 2u);
}

/* A SHUNT write that times out after the node took it: no refusal, INFO
 * read again, the meter on the new shunt.  One the node did not take goes
 * again after INFO. */
TEST_CASE(a_shunt_write_that_times_out_reads_info_and_goes_again_if_needed)
{
    rig_t r;
    up(&r, true);
    r.shunt_hold = true;
    set_shunt(&r, 500u);
    run(&r, BLEU_WRITE_MS + 50u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.shunt_refused, 0u);
    CHECK_EQ(r.info_reads, 2u);
    CHECK_EQ(r.c.info_shunt, 500u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 500u);
    run(&r, 3u * BLEU_WRITE_MS);
    CHECK_EQ(r.shunts, 1u);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.shunt_refused, 0u);
    /* Not taken: INFO still names 500, so 700 goes again. */
    r.shunt_ignore = true;
    set_shunt(&r, 700u);
    run(&r, BLEU_WRITE_MS + 50u);
    CHECK_EQ(r.c.info_shunt, 500u);
    CHECK_EQ(r.c.shunt_refused, 0u);
    /* A write held when the node answers again ends by its own timeout. */
    r.shunt_hold = false;
    r.shunt_ignore = false;
    run(&r, 2u * BLEU_WRITE_MS);
    CHECK(r.shunts >= 3u);
    CHECK_EQ(r.c.info_shunt, 700u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 700u);
}

/* A SHUNT write ended by the link's drop: no refusal; the next link's
 * INFO names what the node holds. */
TEST_CASE(a_shunt_write_lost_with_the_link_is_no_refusal)
{
    rig_t r;
    up(&r, true);
    r.shunt_hold = true;
    set_shunt(&r, 500u);
    run(&r, 5u);
    CHECK_EQ(r.shunts, 1u);
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x208u;
    pnode_ctl_event(&r.c, &e, r.now);
    CHECK_EQ(r.c.shunt_refused, 0u);
    r.shunt_hold = false;
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(r.c.linked);
    CHECK_EQ(r.c.shunt_refused, 0u);
    CHECK_EQ(r.c.info_shunt, 500u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 500u);
    CHECK_EQ(r.shunts, 1u);
}

/* A SHUNT write taken while the bench went ARMED: INFO is not read again,
 * and the meter keeps its full scale, until the bench is SAFE. */
TEST_CASE(info_after_a_shunt_waits_for_safe)
{
    rig_t r;
    up(&r, true);
    r.shunt_hold = true;
    set_shunt(&r, 500u);
    run(&r, 2u);
    CHECK_EQ(r.shunts, 1u);
    window(&r, RCNODE_WF_ONLINE);
    r.armed = true;
    run(&r, 2u);
    bleu_evt_t e = ev(BLEU_EV_WRITE_DONE, r.shunt_tok);
    pnode_ctl_event(&r.c, &e, r.now);
    r.shunt_hold = false;
    run(&r, 500u);
    CHECK_EQ(r.info_reads, 1u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 200u);
    r.armed = false;
    run(&r, 20u);
    CHECK_EQ(r.info_reads, 2u);
    CHECK_EQ(r.c.info_shunt, 500u);
    CHECK_EQ(r.c.m.cal.shunt_uohm, 500u);
    CHECK_EQ(r.shunts, 1u);
}

int main(void)
{
    RUN(shunt_goes_when_info_names_another_and_info_is_read_again);
    RUN(shunt_waits_while_armed_and_goes_at_safe);
    RUN(a_node_that_refuses_shunt_is_counted_and_not_asked_again);
    RUN(a_shunt_write_that_times_out_reads_info_and_goes_again_if_needed);
    RUN(a_shunt_write_lost_with_the_link_is_no_refusal);
    RUN(info_after_a_shunt_waits_for_safe);
    return test_summary("pnode_shunt");
}
