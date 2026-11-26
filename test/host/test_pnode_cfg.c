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

TEST_CASE(shunt_waits_for_config_asked_twice_then_a_silent_node)
{
    rig_t r;
    init(&r, true);
    r.cfg_silent = true;
    boot_shunt(&r, 500u);
    run(&r, 50u);
    CHECK_EQ(r.c.nc.st, NCL_WAIT);
    run(&r, NCL_WAIT_MS + 100u);
    CHECK_EQ(r.cfgs, 2u);             /* asked once more */
    CHECK_EQ(r.shunts, 0u);           /* nothing before the settings */
    run(&r, NCL_WAIT_MS - 300u);
    CHECK_EQ(r.shunts, 0u);
    run(&r, 400u);
    CHECK_EQ(r.c.nc.st, NCL_NONE);
    CHECK_EQ(r.cfgs, 2u);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.info_shunt, 500u);
}

TEST_CASE(a_lost_config_is_asked_again_and_the_node_change_kept)
{
    /* The node's SETUP set 500; its answer to the first ask is lost. */
    rig_t r;
    init(&r, true);
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 3u;
    r.cfg_mute = 1u;
    run(&r, NCL_WAIT_MS + 200u);
    CHECK_EQ(r.cfgs, 2u);
    CHECK(ncl_have(&r.c.nc));
    CHECK_EQ(r.shunts, 0u);           /* not overwritten */
    CHECK_EQ(view(&r).adopt_uohm, 500u);
    CHECK_EQ(view(&r).adopt_seq, 1u);
}

TEST_CASE(a_late_config_after_the_fallback_decides_again)
{
    /* The node takes both asks and sends nothing in time: INFO's rule
     * writes the panel's 200.  A CONFIG sent before that write arrives
     * late: the node's change of 500, 3s old.  It is decided again,
     * though 200 is the value already sent. */
    rig_t r;
    init(&r, true);
    r.cfg_silent = true;
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 3u;
    run(&r, 2u * NCL_WAIT_MS + 300u);
    CHECK_EQ(r.c.nc.st, NCL_NONE);
    CHECK_EQ(r.shunts, 1u);
    CHECK_EQ(r.c.shunt_sent, 200u);
    rcnode_config_t late;
    memset(&late, 0, sizeof late);
    late.flags = RCNODE_CF_SHUNT;
    late.shunt_uohm = 500u;
    late.shunt_agreed = 200u;
    late.shunt_age_s = 3u;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_config(&late, b, sizeof b);
    notify(&r, b, n);
    run(&r, 20u);
    CHECK_EQ(view(&r).adopt_uohm, 500u);
    CHECK_EQ(view(&r).adopt_seq, 1u);
}

TEST_CASE(the_same_node_value_is_taken_again_after_a_panel_change)
{
    /* The node sets 500 and the panel takes it; the user sets 300 on the
     * panel, which is written; the node sets 500 again and links anew:
     * taken again, not left at 300 against the node's 500. */
    rig_t r;
    init(&r, true);
    boot_shunt(&r, 200u);
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_agreed = 200u;
    r.cfg.shunt_age_s = 3u;
    run(&r, 50u);
    CHECK_EQ(view(&r).adopt_seq, 1u);
    set_shunt(&r, 500u);              /* the frame task's take */
    run(&r, 50u);
    CHECK_EQ(r.cfg.shunt_agreed, 500u);
    set_shunt(&r, 300u);
    run(&r, 50u);
    CHECK_EQ(r.info.shunt_uohm, 300u);
    CHECK_EQ(r.cfg.shunt_agreed, 300u);
    /* The node's SETUP sets 500 again, 2s before the new link. */
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    r.cfg_on = false;
    r.info.shunt_uohm = 500u;
    r.cfg.shunt_age_s = 2u;
    r.now += 60000u;                  /* the panel's change, a minute old */
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK_EQ(view(&r).adopt_uohm, 500u);
    CHECK_EQ(view(&r).adopt_seq, 2u);
}

TEST_CASE(a_config_between_windows_counts_no_lost_packet)
{
    rig_t r;
    up(&r, true);
    window(&r, RCNODE_WF_ONLINE);
    window(&r, RCNODE_WF_ONLINE);
    /* The node's CONFIG carries its own seq, not STREAM's. */
    r.cfg.hdr.seq = 77u;
    push_cfg(&r);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(r.c.m.lost, 0u);
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

TEST_CASE(a_refused_request_and_a_dropped_write_are_counted_for_the_band)
{
    rig_t r;
    init(&r, true);                   /* no link, no CONFIG yet */
    CHECK_EQ(pnode_ctl_name(&r.c, "X", false), NCL_REQ_UNKNOWN);
    pnode_view_t v = view(&r);
    CHECK_EQ(v.cfg_req_no, 1u);
    CHECK_EQ(v.cfg_req_why, NCL_REQ_UNKNOWN);
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_i228(&r.c, 0x40u, true), NCL_REQ_ARMED);
    CHECK_EQ(view(&r).cfg_req_no, 2u);
    CHECK_EQ(view(&r).cfg_req_why, NCL_REQ_ARMED);
    /* Asked while SAFE, held while ARMED, then the link drops: not sent
     * on the next link, and counted. */
    CHECK_EQ(pnode_ctl_name(&r.c, "LATER", false), NCL_REQ_OK);
    r.armed = true;
    run(&r, 50u);
    CHECK_EQ(r.names, 0u);
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    r.cfg_on = false;
    r.armed = false;
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(view(&r).have_cfg);
    CHECK_EQ(r.names, 0u);
    v = view(&r);
    CHECK_EQ(v.cfg_dropped, 1u);
    CHECK_EQ(v.cfg_req_no, 2u);       /* kept across the link */
}

int main(void)
{
    RUN(config_is_asked_once_a_link_and_names_the_node);
    RUN(address_auto_and_fixed_go_to_the_node);
    RUN(an_older_node_keeps_its_link_and_gets_the_shunt_as_before);
    RUN(shunt_waits_for_config_asked_twice_then_a_silent_node);
    RUN(a_lost_config_is_asked_again_and_the_node_change_kept);
    RUN(a_late_config_after_the_fallback_decides_again);
    RUN(the_same_node_value_is_taken_again_after_a_panel_change);
    RUN(a_config_between_windows_counts_no_lost_packet);
    RUN(a_refused_request_and_a_dropped_write_are_counted_for_the_band);
    RUN(sync_a_node_change_is_taken_not_overwritten_on_link);
    RUN(sync_the_node_change_after_a_restart_still_wins_over_an_old_panel);
    RUN(sync_a_panel_change_is_written);
    RUN(sync_both_changed_the_later_change_wins);
    RUN(nothing_changes_while_armed);
    RUN(a_refused_name_is_counted_for_the_band);
    return test_summary("pnode_cfg");
}
