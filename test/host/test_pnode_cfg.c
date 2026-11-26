/*
 * The power node's own settings on the panel (node_cfg_link.h through
 * pnode_ctl.h) against the scripted host stack and node of pnode_rig.h:
 * CONFIG, NAME, INA228 ADDRESS and the shunt's sync.
 *
 * The failures this is about: a SHUNT write on link before the node's
 * settings are known, which would overwrite a newer node-side value; an
 * older change that wins over a newer one, either way; a node built
 * before CONFIG that loses its link, its shunt or its name over the new
 * writes; a node that takes CONFIG and sends none holding SHUNT for good;
 * a write or a take while ARMED; and a name the panel shows that is not
 * the node's.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pnode_rig.h"

/** @brief NODE SHUNT changed to @p uohm in SETUP now. */
static void set_shunt(rig_t *r, uint16_t uohm)
{
    pnode_cfg_t cfg = r->c.cfg;
    cfg.shunt_uohm = uohm;
    pnode_ctl_config(&r->c, &cfg, r->now);
}

/** @brief NODE SHUNT as the panel started with it: no change seen. */
static void boot_shunt(rig_t *r, uint16_t uohm)
{
    r->c.cfg.shunt_uohm = uohm;
}

/** @brief The view now. */
static pnode_view_t view(const rig_t *r)
{
    pnode_view_t v;
    pnode_ctl_view(&r->c, r->now, &v);
    return v;
}

TEST_CASE(config_is_asked_once_a_link_and_names_the_node)
{
    rig_t r;
    init(&r, true);
    r.info.addr[0] = 0x56u;
    r.info.addr[1] = 0x34u;
    run(&r, 50u);
    CHECK_EQ(r.cfgs, 1u);
    CHECK_EQ(r.cfg_sent, 1u);
    CHECK(ncl_have(&r.c.nc));
    pnode_view_t v = view(&r);
    CHECK(v.have_cfg);
    CHECK_STR_EQ(v.node_name, "rcbench-pwr-3456");
    CHECK_EQ(v.i228_set, RCNODE_I228_AUTO);
    run(&r, 500u);
    CHECK_EQ(r.cfgs, 1u);
    /* SETUP's NAME: written, and the node's answer names it. */
    CHECK_EQ(pnode_ctl_name(&r.c, "XT90 MAIN", false), NCL_REQ_OK);
    run(&r, 10u);
    CHECK_EQ(r.names, 1u);
    v = view(&r);
    CHECK_STR_EQ(v.node_name, "XT90 MAIN");
    /* "" clears it: the default name again. */
    CHECK_EQ(pnode_ctl_name(&r.c, "", false), NCL_REQ_OK);
    run(&r, 10u);
    v = view(&r);
    CHECK_STR_EQ(v.node_name, "rcbench-pwr-3456");
    CHECK_EQ(pnode_ctl_name(&r.c, "lower", false), NCL_REQ_BAD);
    CHECK_EQ(pnode_ctl_name(&r.c, "0123456789ABCDEFG", false), NCL_REQ_BAD);
    /* A new link asks again. */
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    CHECK(!view(&r).have_cfg);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK_EQ(r.cfgs, 2u);
    CHECK(view(&r).have_cfg);
}

TEST_CASE(address_auto_and_fixed_go_to_the_node)
{
    rig_t r;
    up(&r, true);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x40u, false), NCL_REQ_OK);
    run(&r, 10u);
    CHECK_EQ(r.i228s, 1u);
    CHECK_EQ(view(&r).i228_set, 0x40u);
    CHECK_EQ(pnode_ctl_i228(&r.c, RCNODE_I228_AUTO, false), NCL_REQ_OK);
    run(&r, 10u);
    CHECK_EQ(view(&r).i228_set, RCNODE_I228_AUTO);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x50u, false), NCL_REQ_BAD);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x3Fu, false), NCL_REQ_BAD);
    CHECK_EQ(r.i228s, 2u);
}

TEST_CASE(an_older_node_keeps_its_link_and_gets_the_shunt_as_before)
{
    rig_t r;
    init(&r, true);
    r.old_node = true;
    run(&r, 50u);
    CHECK_EQ(r.cfgs, 1u);
    CHECK_EQ(r.c.nc.st, NCL_NONE);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK(!view(&r).have_cfg);
    CHECK_EQ(pnode_ctl_name(&r.c, "X", false), NCL_REQ_UNKNOWN);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x40u, false), NCL_REQ_UNKNOWN);
    /* SHUNT by INFO's rule, as the panel did before CONFIG. */
    set_shunt(&r, 500u);
    run(&r, 20u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 500u);
    run(&r, 500u);
    CHECK_EQ(r.cfgs, 1u);
    CHECK_EQ(r.names, 0u);
    CHECK_EQ(r.c.shunt_refused, 0u);
}

TEST_CASE(shunt_waits_for_config_and_a_silent_node_after_2s)
{
    rig_t r;
    init(&r, true);
    r.cfg_silent = true;
    boot_shunt(&r, 500u);
    run(&r, 50u);
    CHECK_EQ(r.c.nc.st, NCL_WAIT);
    run(&r, NCL_WAIT_MS - 100u);
    CHECK_EQ(r.shunts, 0u);           /* nothing before the settings */
    run(&r, 200u);
    CHECK_EQ(r.c.nc.st, NCL_NONE);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 500u);
}

TEST_CASE(sync_a_node_change_is_taken_not_overwritten_on_link)
{
    /* The node's SETUP set 500 while the panel kept 200. */
    rig_t r;
    init(&r, true);
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 3u;
    run(&r, 50u);
    CHECK_EQ(r.shunts, 0u);
    pnode_view_t v = view(&r);
    CHECK_EQ(v.adopt_uohm, 500u);
    CHECK_EQ(v.adopt_seq, 1u);
    run(&r, 200u);
    CHECK_EQ(view(&r).adopt_seq, 1u);  /* once a value */
    CHECK_EQ(r.shunts, 0u);
    /* The frame task took it into NODE SHUNT: SHUNT with the same value
     * marks it agreed on the node, and nothing more goes. */
    set_shunt(&r, 500u);
    run(&r, 20u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.cfg.shunt_agreed, 500u);
    CHECK_EQ(r.c.nc.cfg.shunt_agreed, 500u);
    run(&r, 500u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 500u);
}

TEST_CASE(sync_the_node_change_after_a_restart_still_wins_over_an_old_panel)
{
    /* The node changed it before its restart (age not known); the panel
     * holds the value it started with: no change on the panel. */
    rig_t r;
    init(&r, true);
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 750u;
    r.cfg.shunt_agreed = 200u;
    run(&r, 50u);
    CHECK_EQ(r.shunts, 0u);
    CHECK_EQ(view(&r).adopt_uohm, 750u);
}

TEST_CASE(sync_a_panel_change_is_written)
{
    rig_t r;
    up(&r, true);
    set_shunt(&r, 1000u);
    run(&r, 20u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 1000u);
    CHECK_EQ(r.cfg.shunt_agreed, 1000u);
    CHECK_EQ(view(&r).adopt_seq, 0u);
}

TEST_CASE(sync_both_changed_the_later_change_wins)
{
    /* The panel changed at 1000ms, the node 5s before the link at
     * 61,000ms: the node's is the later. */
    rig_t r;
    init(&r, true);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 5u;
    set_shunt(&r, 300u);
    r.now = 61000u;
    run(&r, 50u);
    CHECK_EQ(r.shunts, 0u);
    CHECK_EQ(view(&r).adopt_uohm, 500u);
    /* The panel changed 1s before the link, the node 90s before. */
    init(&r, true);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 90u;
    run(&r, 5u);                      /* linked, not yet asked */
    set_shunt(&r, 300u);
    run(&r, 1000u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 300u);
    CHECK_EQ(view(&r).adopt_seq, 0u);
}

TEST_CASE(nothing_changes_while_armed)
{
    rig_t r;
    init(&r, true);
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 3u;
    r.armed = true;
    run(&r, 200u);
    /* CONFIG is asked (it changes nothing), the take waits. */
    CHECK_EQ(r.cfgs, 1u);
    CHECK_EQ(view(&r).adopt_seq, 0u);
    CHECK_EQ(pnode_ctl_name(&r.c, "X", true), NCL_REQ_ARMED);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x40u, true), NCL_REQ_ARMED);
    /* A name asked as SAFE (the request's own flag) does not go while
     * the bench is ARMED. */
    CHECK_EQ(pnode_ctl_name(&r.c, "LATER", false), NCL_REQ_OK);
    set_shunt(&r, 900u);
    run(&r, 500u);
    CHECK_EQ(r.names, 0u);
    CHECK_EQ(r.shunts, 0u);
    CHECK_EQ(view(&r).adopt_seq, 0u);
    r.armed = false;
    run(&r, 50u);
    CHECK_EQ(r.names, 1u);
    CHECK_STR_EQ(r.cfg.name, "LATER");
    /* Both changed, the panel's later (just now): written at SAFE. */
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 900u);
}

TEST_CASE(a_refused_name_is_counted_for_the_band)
{
    rig_t r;
    up(&r, true);
    CHECK_EQ(pnode_ctl_name(&r.c, "OK NAME", false), NCL_REQ_OK);
    /* The node refuses this one (its store full, say). */
    r.old_node = true;
    run(&r, 10u);
    CHECK_EQ(r.names, 1u);
    CHECK_EQ(view(&r).cfg_refused, 1u);
    run(&r, 100u);
    CHECK_EQ(r.names, 1u);
}

int main(void)
{
    RUN(config_is_asked_once_a_link_and_names_the_node);
    RUN(address_auto_and_fixed_go_to_the_node);
    RUN(an_older_node_keeps_its_link_and_gets_the_shunt_as_before);
    RUN(shunt_waits_for_config_and_a_silent_node_after_2s);
    RUN(sync_a_node_change_is_taken_not_overwritten_on_link);
    RUN(sync_the_node_change_after_a_restart_still_wins_over_an_old_panel);
    RUN(sync_a_panel_change_is_written);
    RUN(sync_both_changed_the_later_change_wins);
    RUN(nothing_changes_while_armed);
    RUN(a_refused_name_is_counted_for_the_band);
    return test_summary("pnode_cfg");
}
