/*
 * SETUP > WIRELESS > BALANCE NODE from the glass to the link: SCAN FOR NODE
 * and BOUND NODE tapped through the setlist's touch path, SEARCH's found
 * list and its bind question (wl_search.h), the pane's queue, the panel's
 * frame and `blec` glue (a model of panel_bal.c's order), the controller
 * (bal_ctl.h) and a node that answers at once (wl_bind_rig.h).
 *
 * The failures this is about: a bind the controller refuses without a word
 * on the band, a bind that a settings post read before the frame kept it
 * undoes, and a scan tapped or asked while one runs, or while an arm
 * waits, that does nothing and says nothing.
 *
 * Cases:
 * - a_pick_binds_keeps_and_links_the_node
 * - a_bind_the_controller_refuses_says_so
 * - a_settings_post_between_bind_and_write_back_keeps_the_bind
 * - a_scan_tap_while_a_scan_runs_says_so
 * - a_scan_the_controller_refuses_says_so
 * - an_unbind_forgets_the_node_and_a_pick_binds_it_again
 *
 * SPDX-License-Identifier: MIT
 */
#include "wl_bind_rig.h"

#include "bal_ctl.h"
#include "wireless_bal.h"

/** @brief The frame task's period, ms: 60Hz rounded down. */
#define FRAME_MS 16u
/** @brief The `blec` task's poll, ms (panel_blec.c's BLEC_POLL_MS). */
#define BLEC_MS 5u
/** @brief The queue between the frame and `blec` (panel_bal.c's). */
#define Q_LEN 8u
/** @brief A finger's tap, DOWN to UP, ms. */
#define TAP_MS 80u
/** @brief The node's address text and name (wbr_peer_init()). @{ */
#define NODE_TEXT "7C:DF:A1:12:34:01 P"
#define NODE_NAME "rcbench-bal-3401"
/** @} */
/** @brief The entry under test. */
#define ENTRY WL_ENTRY_BAL

/** @brief What the frame posts to `blec` (panel_bal.c's bal_msg_t). */
typedef struct {
    bool        is_cfg;
    bal_cfg_t cfg;
    bal_cmd_t cmd;
} msg_t;

/** @brief The rig: both tasks, the controller and the node. */
static struct {
    uint32_t     now;
    bool         armed;          /* the bench ARMED, as the frame reads it */
    bool         live;           /* ARMED or an arm waiting (s_live) */
    bal_ctl_t  c;
    wbr_peer_t   peer;
    msg_t        q[Q_LEN];
    unsigned     q_n;
    unsigned     q_drops;
    bal_view_t pub_v;          /* what `blec` published */
    uint32_t     pub_seq;
    uint32_t     kept_seq;       /* the binds the frame kept in SETUP */
    unsigned     bound_lines;    /* "node: bound ..." console lines */
    unsigned     refused_lines;  /* bind refusals on the console */
    unsigned     unbound_lines;  /* "node: unbound ..." console lines */
    unsigned     scan_lines;     /* "scan for 10000ms" console lines */
    unsigned     scan_no_lines;  /* scan refusals on the console */
} g;

/* ---------------------------------------------- the `blec` task's side */

/** @brief The node's events to the controller. */
static void sink(const bleu_evt_t *e)
{
    bal_ctl_event(&g.c, e, g.now);
}

/** @brief One command of SETUP, with its console line (panel_bal.c's
 *         run_command()). */
static void run_command(const bal_cmd_t *cmd)
{
    if (cmd->kind == BAL_CMD_SCAN) {
        if (bal_ctl_scan(&g.c, g.live, g.now) == BAL_SCAN_STARTED) {
            ++g.scan_lines;
        } else {
            ++g.scan_no_lines;
        }
    } else if (cmd->kind == BAL_CMD_BIND) {
        if (bal_ctl_bind(&g.c, cmd->arg, g.now)) {
            ++g.bound_lines;
        } else {
            ++g.refused_lines;
        }
    } else if (cmd->kind == BAL_CMD_UNBIND) {
        if (bal_ctl_unbind(&g.c, g.now)) {
            ++g.unbound_lines;
        }
    }
}

/** @brief One poll of the `blec` task: the queue, the radio, the node's
 *         advertising, the controller's actions answered, the view
 *         published (panel_bal.c's blec_poll, step and publish). */
static void blec(void)
{
    while (g.q_n > 0u) {
        const msg_t m = g.q[0];
        --g.q_n;
        memmove(&g.q[0], &g.q[1], g.q_n * sizeof g.q[0]);
        if (m.is_cfg) {
            bal_ctl_config(&g.c, &m.cfg, g.now);
        } else {
            run_command(&m.cmd);
        }
    }
    bal_ctl_radio(&g.c, IMU_RADIO_READY, g.now);
    wbr_peer_adv(&g.peer, g.now);
    bleu_act_t a;
    for (int i = 0; i < 64 && bal_ctl_step(&g.c, g.now, g.live, &a); ++i) {
        wbr_peer_act(&g.peer, &a, g.now);
    }
    bal_ctl_view(&g.c, g.now, &g.pub_v);
    g.pub_seq = g.c.bind_seq;
}

/* ------------------------------------------------ the frame task's side */

/** @brief Post @p m to `blec`; a full queue drops it. */
static void send(const msg_t *m)
{
    if (g.q_n < Q_LEN) {
        g.q[g.q_n++] = *m;
    } else {
        ++g.q_drops;
    }
}

/** @brief SETUP's values to `blec` (panel_bal.c's post_cfg()). */
static void post_cfg(void)
{
    msg_t m;
    memset(&m, 0, sizeof m);
    m.is_cfg = true;
    m.cfg.en = settings_get_bool(SET_BNODE_EN);
    snprintf(m.cfg.addr, sizeof m.cfg.addr, "%s",
             settings_text(SET_TEXT_BNODE_ADDR));
    snprintf(m.cfg.name, sizeof m.cfg.name, "%s",
             settings_text(SET_TEXT_BNODE_NAME));
    m.cfg.bind_seq = g.kept_seq;
    send(&m);
}

/** @brief The settings observer: the node's values to `blec`. */
static void on_setting(setting_id_t id)
{
    if (id == SET_BNODE_EN) {
        post_cfg();
    }
}

/** @brief The text observer: the node's address and name to `blec`. */
static void on_text(setting_text_id_t id)
{
    if (id == SET_TEXT_BNODE_ADDR || id == SET_TEXT_BNODE_NAME) {
        post_cfg();
    }
}

/** @brief Whether Bluetooth can scan (panel_bal.c's bt_ready()). */
static bool bt_ready(uint8_t link)
{
    return link != BAL_LINK_OFF && link != BAL_LINK_STARTING
           && link != BAL_LINK_NO_BT && link != BAL_LINK_C6_FAILED;
}

/** @brief One frame's node glue (panel_bal.c's panel_bal_frame()). */
static void frame(void)
{
    const bal_view_t seen = g.pub_v;
    const uint32_t seq = g.pub_seq;
    wireless_pane_set_bal(&seen);
    if (seq != g.kept_seq) {
        g.kept_seq = seq;
        settings_set_text(SET_TEXT_BNODE_ADDR, seen.bound_addr);
        settings_set_text(SET_TEXT_BNODE_NAME, seen.bound_name);
        settings_request_save();
    }
    msg_t m;
    memset(&m, 0, sizeof m);
    while (wireless_pane_take_bal(&m.cmd)) {
        if (m.cmd.kind == BAL_CMD_SCAN && g.armed) {
            ui_router_set_alert(TR(BNODE_SCAN_ARMED));
            continue;
        }
        if (m.cmd.kind == BAL_CMD_SCAN && !bt_ready(seen.link)) {
            ui_router_set_alert(TR(BNODE_SCAN_OFF));
            continue;
        }
        send(&m);
    }
}

/** @brief The clock on by @p ms: `blec` every BLEC_MS, a frame every
 *         FRAME_MS, 2ms after the poll. */
static void run(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++g.now;
        if (g.now % BLEC_MS == 0u) {
            blec();
        }
        if (g.now % FRAME_MS == 2u) {
            frame();
        }
    }
}

/* ------------------------------------------------------------- the glass */

/** @brief A tap of TAP_MS at (@p x, @p y), the clock running under it. */
static void tap_at(int x, int y)
{
    wbr_down(x, y);
    run(TAP_MS);
    wbr_up(x, y);
}

/** @brief A tap in the middle of @p r, panel coordinates. */
static void tap_rect(gfx_rect_t r)
{
    tap_at(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief A tap on the row of @p c, SEARCH left by its home tag first. */
static void tap_row(const ui_setlist_custom_t *c)
{
    if (ui_router_current() == SCREEN_WL_SEARCH) {
        int hx, hy;
        wbr_home_at(&hx, &hy);
        tap_at(hx, hy);
    }
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    int x, y;
    wbr_row_mid(ENTRY, c, &x, &y);
    tap_at(x, y);
}

/** @brief SEARCH's found node @p k tapped, then the question's BIND. */
static void pick(int k)
{
    gfx_rect_t r;
    CHECK(wbr_found(k, &r));
    tap_rect(r);
    CHECK(wl_search_asking());
    tap_rect(wbr_part(WLS_PART_BIND));
    CHECK(!wl_search_asking());
}

/** @brief The rows under test. @{ */
#define SCAN_ROW (&wireless_bal_scan_row)
#define BIND_ROW (&wireless_bal_bind_row)
/** @} */

/** @brief The bench at 0ms: SETUP > WIRELESS > BALANCE NODE open through its
 *         tile and entry, BALANCE NODE ON, Bluetooth synced, nothing bound,
 *         the node advertising when a scan runs. */
static void start(void)
{
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    memset(&g, 0, sizeof g);
    settings_set_observer(on_setting);
    settings_set_text_observer(on_text);
    wireless_pane_set_bal(NULL);
    bal_cmd_t c;
    while (wireless_pane_take_bal(&c)) { }
    wbr_peer_init(&g.peer, RCNODE_ROLE_BALANCE, sink);
    bal_ctl_init(&g.c);
    /* BALANCE NODE ON: the settings observer posts it, as the switch would. */
    open_wireless(SET_BNODE_EN);
    run(20u);
    CHECK_EQ(wireless_pane_selected(), ENTRY);
    CHECK(wbr_row(ENTRY, BIND_ROW) >= 0);
}

/** @brief SCAN FOR NODE tapped and the clock run until it reads FOUND 1,
 *         at most 12s. */
static void scan(void)
{
    tap_row(SCAN_ROW);
    char want[32];
    snprintf(want, sizeof want, TR(DIAL_SCAN_FOUND), 1u);
    for (unsigned k = 0; k < 12000u / FRAME_MS
                         && strcmp(wbr_value(SCAN_ROW), want) != 0; ++k) {
        run(FRAME_MS);
    }
    CHECK_STR_EQ(wbr_value(SCAN_ROW), want);
}

/** @brief The controller linked to the node with its INFO taken. */
static bool linked(void)
{
    return g.c.linked && g.c.info_ok && g.c.b.bound;
}

/* ---------------------------------------------------------------- cases */

/* A scan, then BOUND NODE's SEARCH and a pick there: bound, kept in SETUP
 * with its name, linked; a second later still so. */
TEST_CASE(a_pick_binds_keeps_and_links_the_node)
{
    start();
    CHECK_STR_EQ(wbr_value(BIND_ROW), TR(DIAL_BIND_NONE));
    scan();
    CHECK_EQ(g.scan_lines, 1u);
    tap_row(BIND_ROW);
    pick(0);
    run(200u);
    CHECK_EQ(g.bound_lines, 1u);
    CHECK_EQ(g.refused_lines, 0u);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), NODE_TEXT);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_NAME), NODE_NAME);
    CHECK_STR_EQ(wbr_value(BIND_ROW), NODE_NAME);
    CHECK(linked());
    run(1000u);
    CHECK(linked());
    CHECK_STR_EQ(g.c.cfg.addr, NODE_TEXT);
    CHECK_EQ(g.q_drops, 0u);
}

/* The question open on FOUND 1 and a scan started in the controller
 * before the BIND reaches it, which the screen's view does not show yet:
 * the controller refuses the bind, and the band says so instead of
 * nothing.  The same with no scan running and the list emptied under the
 * BIND: the list changed. */
TEST_CASE(a_bind_the_controller_refuses_says_so)
{
    start();
    scan();
    tap_row(BIND_ROW);
    gfx_rect_t r;
    CHECK(wbr_found(0, &r));
    tap_rect(r);
    CHECK(wl_search_asking());
    const gfx_rect_t b = wbr_part(WLS_PART_BIND);
    CHECK_EQ(bal_ctl_scan(&g.c, false, g.now), BAL_SCAN_STARTED);
    wbr_down(b.x + b.w / 2, b.y + b.h / 2);
    wbr_up(b.x + b.w / 2, b.y + b.h / 2);
    const uint32_t n0 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.refused_lines, 1u);
    CHECK_EQ(g.bound_lines, 0u);
    CHECK_EQ(g.c.bind_refused, 1u);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(wbr_band(), TR(NODE_BIND_SCANNING));
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "");
    run(BLEU_SCAN_MS + 500u);
    CHECK(wbr_found(0, &r));
    tap_rect(r);
    wbr_down(b.x + b.w / 2, b.y + b.h / 2);
    wbr_up(b.x + b.w / 2, b.y + b.h / 2);
    g.c.found_n = 0u;
    const uint32_t n1 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.refused_lines, 2u);
    CHECK(ui_router_alert_number() != n1);
    CHECK_STR_EQ(wbr_band(), TR(NODE_BIND_GONE));
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "");
}

/* A settings post between the BIND and the frame's write-back (a value
 * changed in the frame the bind ran): it carries the old, empty address.
 * The bind stands, the link stays and SETUP keeps the address. */
TEST_CASE(a_settings_post_between_bind_and_write_back_keeps_the_bind)
{
    start();
    scan();
    gfx_rect_t r;
    CHECK(wbr_found(0, &r));
    tap_rect(r);
    r = wbr_part(WLS_PART_BIND);
    const int px = r.x + r.w / 2, py = r.y + r.h / 2;
    wbr_down(px, py);
    run(TAP_MS);
    wbr_up(px, py);
    /* Through the frame that sends the BIND, then the poll that runs it. */
    for (unsigned k = 0; k < 100u && g.bound_lines == 0u; ++k) {
        run(1u);
    }
    CHECK_EQ(g.bound_lines, 1u);
    CHECK_EQ(g.pub_seq, g.c.bind_seq);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "");
    post_cfg();
    run(BLEC_MS);
    run(2u * FRAME_MS);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), NODE_TEXT);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_NAME), NODE_NAME);
    CHECK_STR_EQ(g.c.cfg.addr, NODE_TEXT);
    CHECK(g.c.b.bound);
    run(200u);
    CHECK(linked());
    CHECK_EQ(g.peer.connects, 1u);
    CHECK_STR_EQ(wbr_value(BIND_ROW), NODE_NAME);
    /* A post after the write-back takes a new address. */
    settings_set_text(SET_TEXT_BNODE_ADDR, "");
    run(2u * FRAME_MS);
    CHECK(!g.c.b.bound);
}

/* SCAN AGAIN tapped while the scan runs: the band says so, and no second
 * scan is asked. */
TEST_CASE(a_scan_tap_while_a_scan_runs_says_so)
{
    start();
    tap_row(SCAN_ROW);
    run(100u);
    CHECK_EQ(g.scan_lines, 1u);
    CHECK_STR_EQ(wbr_value(SCAN_ROW), TR(DIAL_SCAN_RUNS));
    const uint32_t n0 = ui_router_alert_number();
    tap_rect(wbr_part(WLS_PART_AGAIN));
    run(3u * FRAME_MS);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(wbr_band(), TR(NODE_SCAN_RUNNING));
    CHECK_EQ(g.scan_lines, 1u);
    CHECK_EQ(g.scan_no_lines, 0u);
}

/* Scans the controller refuses after the frame passed them on: a second
 * SCAN in the frame of the first (a scan then runs), and a SCAN while an
 * arm waits (the frame still reads SAFE).  Each says why on the band. */
TEST_CASE(a_scan_the_controller_refuses_says_so)
{
    start();
    SCAN_ROW->tap();
    SCAN_ROW->tap();
    const uint32_t n0 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.scan_lines, 1u);
    CHECK_EQ(g.scan_no_lines, 1u);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(wbr_band(), TR(NODE_SCAN_RUNNING));
    run(BLEU_SCAN_MS + 500u);
    g.live = true;
    SCAN_ROW->tap();
    const uint32_t n1 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.scan_no_lines, 2u);
    CHECK_EQ(g.c.refused, 1u);
    CHECK(ui_router_alert_number() != n1);
    CHECK_STR_EQ(wbr_band(), TR(BNODE_SCAN_ARMED));
}

/* A bound, linked node unbound on SEARCH by UNBIND held 2s: the link ends,
 * SETUP keeps the empty address and name and asks for a save, which
 * writes them to the store; BOUND NODE and SEARCH read NONE and UNBIND
 * goes grey; no post binds it back.  A pick on SEARCH binds the node again
 * and links it. */
TEST_CASE(an_unbind_forgets_the_node_and_a_pick_binds_it_again)
{
    start();
    settings_set_store(&s_text_store);
    scan();
    tap_row(BIND_ROW);
    CHECK(!wl_search_unbind_ready());
    pick(0);
    run(200u);
    CHECK(linked());
    CHECK(settings_save_tick(true));
    CHECK_STR_EQ(s_saved_text[SET_TEXT_BNODE_ADDR], NODE_TEXT);
    CHECK(wl_search_unbind_ready());
    wbr_hold_unbind(run, 2000u);
    run(3u * FRAME_MS);
    CHECK_EQ(g.unbound_lines, 1u);
    CHECK(!g.c.b.bound);
    CHECK(!g.c.linked);
    CHECK_EQ(g.peer.connects, 1u);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_NAME), "");
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_STR_EQ(s_saved_text[SET_TEXT_BNODE_ADDR], "");
    CHECK_STR_EQ(s_saved_text[SET_TEXT_BNODE_NAME], "");
    CHECK_STR_EQ(wbr_value(BIND_ROW), TR(DIAL_BIND_NONE));
    wls_view_t v;
    wl_search_view(&v);
    CHECK_STR_EQ(v.bound_addr, "");
    CHECK(!wl_search_unbind_ready());
    run(3000u);
    CHECK(!g.c.b.bound);
    CHECK_EQ(g.peer.connects, 1u);
    pick(0);
    run(200u);
    CHECK_EQ(g.bound_lines, 2u);
    CHECK(linked());
    CHECK_EQ(g.peer.connects, 2u);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), NODE_TEXT);
    CHECK_STR_EQ(wbr_value(BIND_ROW), NODE_NAME);
    CHECK_EQ(g.q_drops, 0u);
    settings_set_store(NULL);
}

int main(void)
{
    RUN(a_pick_binds_keeps_and_links_the_node);
    RUN(a_bind_the_controller_refuses_says_so);
    RUN(a_settings_post_between_bind_and_write_back_keeps_the_bind);
    RUN(a_scan_tap_while_a_scan_runs_says_so);
    RUN(a_scan_the_controller_refuses_says_so);
    RUN(an_unbind_forgets_the_node_and_a_pick_binds_it_again);
    return test_summary("bal_bind");
}
