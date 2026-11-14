/*
 * SETUP > WIRELESS > DIAL from the glass to the link: SCAN FOR DIAL and
 * BOUND DIAL tapped through the setlist's touch path, the pane's queue,
 * the panel's frame and `blec` glue (a model of panel_dial.c's order), the
 * controller (dial_ctl.h) and the hub (bleuart_hub.h) against scripted
 * peers (ble_sim.h): the Dial, and an IMU and a power node that are bound
 * and never answer.
 *
 * The failures this is about: a tap that does nothing and says nothing, a
 * bind the controller refuses without a word, a bind that a stale settings
 * post undoes, a Dial whose connect waits behind other clients' connects
 * for long, and a popup choice that is drawn where its tap does not land.
 *
 * Cases:
 * - a_pick_in_the_frame_found_shows_binds_and_connects
 * - the_scan_row_reading_found_opens_the_list_and_binds
 * - a_bound_dial_tap_with_no_list_says_why
 * - a_pick_while_a_second_scan_runs_says_so
 * - a_bind_the_controller_refuses_says_so
 * - a_settings_post_between_bind_and_write_back_keeps_the_bind
 * - a_pick_waits_for_one_running_attempt_at_most
 * - a_bind_drops_the_backoff_of_an_old_dial
 * - the_popup_choice_lands_on_index_0_at_1024x600
 *
 * SPDX-License-Identifier: MIT
 */
#include "wl_bind_rig.h"

#include "ble_sim.h"
#include "bleuart_hub.h"
#include "dial_ctl.h"
#include "dialknob.h"

/** @brief The frame task's period, ms: 60Hz rounded down. */
#define FRAME_MS 16u
/** @brief The `blec` task's poll, ms (panel_blec.c's BLEC_POLL_MS). */
#define BLEC_MS 5u
/** @brief The queue between the frame and `blec` (panel_dial.c's). */
#define Q_LEN 8u
/** @brief The owners of the hub's clients (panel_blec.h). @{ */
#define OWN_IMU   0u
#define OWN_DIAL  DIAL_BLE_OWNER
#define OWN_PNODE 2u
/** @} */
/** @brief A finger's tap, DOWN to UP, ms. */
#define TAP_MS 80u
/** @brief The Dial's EVENT keepalive the peer sends while subscribed, ms. */
#define EVT_MS 100u
/** @brief A measured time on the suite's output. */
#define T_LOG(...) (printf("  " __VA_ARGS__), printf("\n"))

/** @brief The Dial's address and its text. */
static const bleu_addr_t k_dial = {
    { 0x10, 0x3A, 0x7F, 0xC2, 0xB1, 0xA0 }, BLEU_ADDR_RANDOM
};
#define DIAL_TEXT "A0:B1:C2:7F:3A:10 R"
/** @brief An absent Dial's address, bound before the scan. */
#define OLD_TEXT "A0:B1:C2:00:00:99 R"

/** @brief The dialknob characteristics' UUIDs. @{ */
static const uint8_t k_evt_uuid[16] = { DK_UUID_LE(DK_UUID_EVENT) };
static const uint8_t k_scr_uuid[16] = { DK_UUID_LE(DK_UUID_SCREEN) };
static const uint8_t k_inf_uuid[16] = { DK_UUID_LE(DK_UUID_INFO) };
static const uint8_t k_sta_uuid[16] = { DK_UUID_LE(DK_UUID_STATUS) };
/** @} */

/** @brief What the frame posts to `blec` (panel_dial.c's dial_msg_t). */
typedef struct {
    bool       is_cfg;
    dial_cfg_t cfg;
    dial_cmd_t cmd;
} msg_t;

/** @brief The rig: both tasks, the hub, the clients and their peers. */
static struct {
    uint32_t   now;
    bool       armed;
    bleu_hub_t hub;
    dial_ctl_t c;
    bleu_t     other[BLEU_HUB_CLIENTS];
    bool       other_on[BLEU_HUB_CLIENTS];
    bsim_t     peer[BLEU_HUB_CLIENTS];
    msg_t      q[Q_LEN];
    unsigned   q_n;
    unsigned   q_drops;
    dial_view_t pub_v;        /* what `blec` published */
    uint32_t   pub_seq;
    uint32_t   kept_seq;      /* the binds the frame kept in SETUP */
    unsigned   bound_lines;   /* "dial: bound ..." console lines */
    unsigned   refused_lines; /* bind refusals on the console */
    unsigned   scan_lines;    /* "scan for 10000ms" console lines */
    uint32_t   t_bind;        /* when `blec` ran the last BIND */
    uint32_t   t_connect;     /* the Dial's first CONNECT after it */
    unsigned   dial_connects; /* the Dial's CONNECT actions */
    unsigned   busy_answers;  /* connects of the Dial the hub refused */
    unsigned   turn_answers;  /* ...with the next turn kept for it */
    uint8_t    gap_at_bind;   /* the hub's slot when the BIND ran */
    uint8_t    gap_owner_at_bind; /* its holder */
    uint32_t   gap_t_at_bind; /* when that holder took it */
    uint8_t    evt_seq;
    uint32_t   next_evt;
} g;

/* ------------------------------------------------------------- the peers */

/** @brief The Dial's GATT table on its peer: EVENT 0x12 with its CCCD at
 *         0x13, SCREEN 0x15, INFO 0x17, STATUS 0x19. */
static void dial_table(bsim_t *s)
{
    const bsim_chr_t chr[4] = {
        { 0x0011u, 0x0012u, BLEU_PROP_NOTIFY, k_evt_uuid },
        { 0x0014u, 0x0015u, BLEU_PROP_WRITE_NR, k_scr_uuid },
        { 0x0016u, 0x0017u, BLEU_PROP_READ, k_inf_uuid },
        { 0x0018u, 0x0019u, BLEU_PROP_NOTIFY | BLEU_PROP_READ, k_sta_uuid },
    };
    memcpy(s->chr, chr, sizeof chr);
    s->nchr = 4;
    s->dsc[0] = (bsim_dsc_t){ 0x0013u, BLEU_UUID16_CCCD };
    s->ndsc = 1;
    s->tx_cccd = 0x0013u;
    s->mtu = 247u;
}

/** @brief The Dial's advertising: its service at 40ms, its name at 60ms. */
static void dial_adv(bsim_t *s)
{
    const uint8_t svc[21] = { 2u, 0x01u, 0x06u, 17u, 0x07u, DK_UUID_LE(0) };
    const uint8_t name[11] = { 10u, 0x09u, 'D', 'I', 'A', 'L', '-', '3', 'A',
                               '1', '0' };
    bsim_adv(s, 40u, &k_dial, -58, svc, sizeof svc);
    bsim_adv(s, 60u, &k_dial, -58, name, sizeof name);
}

/** @brief INFO as the Dial reads it back, into a READ answer. */
static void dial_info(bleu_evt_t *e)
{
    dk_info_t i;
    memset(&i, 0, sizeof i);
    i.proto = DK_PROTOCOL;
    i.fw[1] = 1u;
    i.hw = DK_HW_DIAL11;
    memcpy(i.name, "DIAL-3A10", 10u);
    e->len = (uint8_t)dk_info_encode(&i, e->data);
}

/* ---------------------------------------------- the `blec` task's side */

/** @brief One event to the client @p o. */
static void client_event(uint8_t o, const bleu_evt_t *e)
{
    if (o == OWN_DIAL) {
        dial_ctl_event(&g.c, e, g.now);
    } else if (o < BLEU_HUB_CLIENTS && g.other_on[o]) {
        (void)bleu_event(&g.other[o], e, g.now);
    }
}

/** @brief The peer that takes a hub action, as hub_rig.h's rig_peer(). */
static int peer_of(const bleu_act_t *a)
{
    if (a->token != 0u) {
        return bleu_token_owner(a->token);
    }
    for (unsigned i = 0; i < BLEU_HUB_CLIENTS; ++i) {
        const bsim_t *s = &g.peer[i];
        if ((a->kind == BLEU_ACT_CANCEL && s->connecting)
            || (a->kind == BLEU_ACT_SCAN_STOP && s->scanning)
            || (a->kind == BLEU_ACT_DISCONNECT && s->connected
                && s->conn == a->conn)) {
            return (int)i;
        }
    }
    return -1;
}

/** @brief The hub's events to the clients and its actions to the peers, a
 *         write without response answered at once (panel_blec.c's
 *         hand_out()). */
static void hand_out(void)
{
    bool more = true;
    while (more) {
        more = false;
        uint8_t o;
        bleu_evt_t e;
        while (bleu_hub_next_event(&g.hub, &o, &e)) {
            if (o == OWN_DIAL && e.kind == BLEU_EV_CONNECT
                && e.status == BLEU_STATUS_BUSY) {
                ++g.busy_answers;
            }
            if (o == OWN_DIAL && e.kind == BLEU_EV_CONNECT
                && e.status == BLEU_STATUS_TURN) {
                ++g.turn_answers;
            }
            client_event(o, &e);
        }
        bleu_act_t a;
        while (bleu_hub_next_action(&g.hub, &a)) {
            if (a.kind == BLEU_ACT_CONNECT && a.token != 0u
                && bleu_token_owner(a.token) == OWN_DIAL) {
                ++g.dial_connects;
                if (g.t_connect == 0u) {
                    g.t_connect = g.now;
                }
            }
            if (a.kind == BLEU_ACT_WRITE_NR) {
                bleu_hub_written(&g.hub, &a, 0, g.now);
                more = true;
                continue;
            }
            const int p = peer_of(&a);
            if (p >= 0 && (unsigned)p < BLEU_HUB_CLIENTS) {
                bsim_act(&g.peer[p], g.now, &a);
            }
        }
    }
}

/** @brief The next event of peer @p s due now, in time order. */
static bool due(bsim_t *s, bleu_evt_t *out)
{
    unsigned best = s->nev;
    for (unsigned i = 0; i < s->nev; ++i) {
        if (bsim_due(s->ev[i].at, g.now)
            && (best == s->nev
                || (int32_t)(s->ev[i].at - s->ev[best].at) < 0)) {
            best = i;
        }
    }
    if (best == s->nev) {
        return false;
    }
    *out = s->ev[best].e;
    memmove(&s->ev[best], &s->ev[best + 1u],
            (s->nev - best - 1u) * sizeof s->ev[0]);
    --s->nev;
    return true;
}

/** @brief One command of SETUP, with its console line (panel_dial.c's
 *         run_command()). */
static void run_command(const dial_cmd_t *cmd)
{
    if (cmd->kind == DIAL_CMD_SCAN) {
        if (dial_ctl_scan(&g.c, g.armed, g.now) == DIAL_SCAN_STARTED) {
            ++g.scan_lines;
        }
    } else if (cmd->kind == DIAL_CMD_BIND) {
        g.t_bind = g.now;
        g.t_connect = 0u;
        g.gap_at_bind = g.hub.gap;
        g.gap_owner_at_bind = g.hub.gap_owner;
        g.gap_t_at_bind = g.hub.gap_t;
        if (dial_ctl_bind(&g.c, cmd->arg, g.now)) {
            ++g.bound_lines;
        } else {
            ++g.refused_lines;
        }
    }
}

/** @brief The Dial's EVENT, every EVT_MS while subscribed. */
static void dial_events(void)
{
    const bsim_t *s = &g.peer[OWN_DIAL];
    if (!s->connected || !s->subscribed || !bsim_due(g.next_evt, g.now)) {
        return;
    }
    g.next_evt = g.now + EVT_MS;
    dk_evt_t ev;
    memset(&ev, 0, sizeof ev);
    ev.seq = g.evt_seq++;
    ev.ms = (uint16_t)g.now;
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_NOTIFY;
    e.conn = s->conn;
    e.h1 = 0x0012u;
    e.len = (uint8_t)dk_evt_encode(&ev, e.data);
    bleu_hub_event(&g.hub, &e, g.now);
    hand_out();
}

/** @brief One poll of the `blec` task (panel_blec.c's blec_task()). */
static void blec(void)
{
    while (g.q_n > 0u) {
        const msg_t m = g.q[0];
        --g.q_n;
        memmove(&g.q[0], &g.q[1], g.q_n * sizeof g.q[0]);
        if (m.is_cfg) {
            dial_ctl_config(&g.c, &m.cfg, g.now);
        } else {
            run_command(&m.cmd);
        }
    }
    dial_ctl_radio(&g.c, IMU_RADIO_READY, g.now);
    for (unsigned o = 0; o < BLEU_HUB_CLIENTS; ++o) {
        bleu_evt_t e;
        while (due(&g.peer[o], &e)) {
            bsim_open(&g.peer[o], &e);
            if (o == OWN_DIAL && e.kind == BLEU_EV_READ && e.status == 0u) {
                dial_info(&e);
            }
            bleu_hub_event(&g.hub, &e, g.now);
            hand_out();
        }
    }
    dial_events();
    bleu_hub_tick(&g.hub, g.now);
    hand_out();
    for (uint8_t o = 0; o < BLEU_HUB_CLIENTS; ++o) {
        bleu_act_t a;
        for (int i = 0; i < 16; ++i) {
            bool acted;
            if (o == OWN_DIAL) {
                acted = dial_ctl_step(&g.c, g.now, g.armed, &a);
            } else {
                acted = g.other_on[o] && bleu_step(&g.other[o], g.now, &a);
            }
            if (!acted) {
                break;
            }
            bleu_hub_action(&g.hub, o, &a, g.now);
            hand_out();
        }
    }
    dial_ctl_view(&g.c, &g.pub_v);
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

/** @brief SETUP's values to `blec` (panel_dial.c's post_cfg()). */
static void post_cfg(void)
{
    msg_t m;
    memset(&m, 0, sizeof m);
    m.is_cfg = true;
    m.cfg.en = settings_get_bool(SET_DIAL_EN);
    snprintf(m.cfg.addr, sizeof m.cfg.addr, "%s",
             settings_text(SET_TEXT_DIAL_ADDR));
    snprintf(m.cfg.name, sizeof m.cfg.name, "%s",
             settings_text(SET_TEXT_DIAL_NAME));
    m.cfg.thr_tenths = dial_step_tenths(settings_get_int(SET_DIAL_THR));
    m.cfg.srv_tenths = dial_step_tenths(settings_get_int(SET_DIAL_SRV));
    m.cfg.bind_seq = g.kept_seq;
    send(&m);
}

/** @brief The settings observer: the Dial's values to `blec`. */
static void on_setting(setting_id_t id)
{
    if (id == SET_DIAL_EN || id == SET_DIAL_THR || id == SET_DIAL_SRV) {
        post_cfg();
    }
}

/** @brief The text observer: the Dial's address and name to `blec`. */
static void on_text(setting_text_id_t id)
{
    if (id == SET_TEXT_DIAL_ADDR || id == SET_TEXT_DIAL_NAME) {
        post_cfg();
    }
}

/** @brief Whether Bluetooth can scan (panel_dial.c's bt_ready()). */
static bool bt_ready(uint8_t link)
{
    return link != DIAL_LINK_OFF && link != DIAL_LINK_STARTING
           && link != DIAL_LINK_NO_BT && link != DIAL_LINK_C6_FAILED;
}

/** @brief One frame's Dial glue (panel_dial.c's panel_dial_frame()). */
static void frame(void)
{
    const dial_view_t seen = g.pub_v;
    const uint32_t seq = g.pub_seq;
    wireless_pane_set_dial(&seen);
    if (seq != g.kept_seq) {
        g.kept_seq = seq;
        settings_set_text(SET_TEXT_DIAL_ADDR, seen.bound_addr);
        settings_set_text(SET_TEXT_DIAL_NAME, seen.bound_name);
        settings_request_save();
    }
    msg_t m;
    memset(&m, 0, sizeof m);
    while (wireless_pane_take_dial(&m.cmd)) {
        if (m.cmd.kind == DIAL_CMD_SCAN && g.armed) {
            ui_router_set_alert(TR(DIAL_SCAN_ARMED));
            continue;
        }
        if (m.cmd.kind == DIAL_CMD_SCAN && !bt_ready(seen.link)) {
            ui_router_set_alert(TR(DIAL_SCAN_OFF));
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

/** @brief The clock on to the next frame and through it. */
static void next_frame(void)
{
    do {
        run(1u);
    } while (g.now % FRAME_MS != 2u);
}

/* ------------------------------------------------------------- the glass */

/* The glass is wl_bind_rig.h's, on the DIAL entry. */
#define down(x, y)          wbr_down((x), (y))
#define up(x, y)            wbr_up((x), (y))
#define custom_row(c)       wbr_row(WL_ENTRY_DIAL, (c))
#define row_mid(c, x, y)    wbr_row_mid(WL_ENTRY_DIAL, (c), (x), (y))
#define choice_rect(k, out) wbr_choice((k), (out))
#define band()              wbr_band()
#define value_of(c)         wbr_value(c)

/** @brief A tap of TAP_MS at (@p x, @p y), the clock running under it. */
static void tap_at(int x, int y)
{
    down(x, y);
    run(TAP_MS);
    up(x, y);
}

/** @brief A tap on the row of @p c. */
static void tap_row(const ui_setlist_custom_t *c)
{
    int x, y;
    row_mid(c, &x, &y);
    tap_at(x, y);
}

/* --------------------------------------------------------------- set-up */

/** @brief The other clients start() switches on. @{ */
#define WITH_IMU   0x01u
#define WITH_PNODE 0x02u
/** @} */

/** @brief The bench at 0ms: SETUP > WIRELESS > DIAL open through its tile
 *         and entry, Dial ON, Bluetooth synced, the address @p addr kept,
 *         the Dial's peer never answering a connect when @p absent, and
 *         the IMU and the power node of @p others ON and bound to peers
 *         that never answer. */
static void start(const char *addr, unsigned others, bool absent)
{
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    memset(&g, 0, sizeof g);
    g.next_evt = 0u;
    settings_set_observer(on_setting);
    settings_set_text_observer(on_text);
    settings_set_text(SET_TEXT_DIAL_ADDR, addr);
    wireless_pane_set_dial(NULL);
    dial_cmd_t c;
    while (wireless_pane_take_dial(&c)) { }
    bleu_hub_init(&g.hub);
    for (uint8_t o = 0; o < BLEU_HUB_CLIENTS; ++o) {
        bsim_init(&g.peer[o]);
        g.peer[o].next_conn = (uint16_t)(1u + o);
        CHECK(bleu_hub_add(&g.hub, o));
    }
    dial_table(&g.peer[OWN_DIAL]);
    dial_adv(&g.peer[OWN_DIAL]);
    g.peer[OWN_DIAL].conn_ms = 40u;
    g.peer[OWN_DIAL].absent = absent;
    dial_ctl_init(&g.c);
    {
        static const uint8_t own[2] = { OWN_IMU, OWN_PNODE };
        for (unsigned i = 0; i < 2u; ++i) {
            const uint8_t o = own[i];
            if ((others & (1u << i)) == 0u) {
                continue;
            }
            bleu_init(&g.other[o]);
            CHECK(bleu_setup(&g.other[o], &bleu_profile_issc, o));
            const bleu_addr_t a = { { 0x01, 0x02, 0x03, 0x04, 0x05,
                                      (uint8_t)(0x10u + o) },
                                    BLEU_ADDR_PUBLIC };
            bleu_bind(&g.other[o], &a, 0u);
            bleu_enable(&g.other[o], true, 0u);
            g.peer[o].absent = true;
            g.other_on[o] = true;
        }
    }
    /* Dial ON: the settings observer posts it, as panel_dial_start()'s
     * first post_cfg() and the switch would. */
    open_wireless(SET_DIAL_EN);
    post_cfg();
    run(20u);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_DIAL);
    CHECK(custom_row(&wireless_pane_bind_row) >= 0);
}

/** @brief SCAN FOR DIAL tapped and the clock run until its result shows,
 *         at most 12s; @return the frames it took. */
static unsigned scan(void)
{
    tap_row(&wireless_pane_scan_row);
    unsigned frames = 0;
    char want[32];
    snprintf(want, sizeof want, TR(DIAL_SCAN_FOUND), 1u);
    while (strcmp(value_of(&wireless_pane_scan_row), want) != 0
           && frames < 12000u / FRAME_MS) {
        next_frame();
        ++frames;
    }
    CHECK_STR_EQ(value_of(&wireless_pane_scan_row), want);
    return frames;
}

/** @brief The popup's choice @p k tapped in its middle. */
static void pick(int k)
{
    gfx_rect_t r;
    CHECK(choice_rect(k, &r));
    tap_at(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Run until the Dial is UP, at most @p ms; @return the ms. */
static uint32_t until_up(uint32_t ms)
{
    const uint32_t t0 = g.now;
    while (dial_ctl_link(&g.c) != DIAL_LINK_UP && g.now - t0 < ms) {
        run(1u);
    }
    return g.now - t0;
}

/* ---------------------------------------------------------------- cases */

/* (a) BOUND DIAL tapped in the very frame FOUND 1 first shows, its popup's
 * choice tapped: bound, kept in SETUP, connected and UP. */
TEST_CASE(a_pick_in_the_frame_found_shows_binds_and_connects)
{
    start("", 0u, false);
    CHECK_STR_EQ(value_of(&wireless_pane_bind_row), TR(DIAL_BIND_NONE));
    (void)scan();
    CHECK_EQ(g.scan_lines, 1u);
    /* The same frame: the DOWN at once, the popup on its UP. */
    int x, y;
    row_mid(&wireless_pane_bind_row, &x, &y);
    down(x, y);
    up(x, y);
    gfx_rect_t r;
    CHECK(choice_rect(0, &r));
    const uint32_t t_pick = g.now;
    pick(0);
    run(200u);
    CHECK_EQ(g.bound_lines, 1u);
    CHECK_EQ(g.refused_lines, 0u);
    /* The BIND reached `blec` within one frame and one poll of the UP. */
    CHECK(g.t_bind - (t_pick + TAP_MS) <= FRAME_MS + BLEC_MS);
    CHECK(g.t_connect >= g.t_bind);
    CHECK(g.t_connect - g.t_bind <= BLEC_MS);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), DIAL_TEXT);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_NAME), "DIAL-3A10");
    CHECK_STR_EQ(value_of(&wireless_pane_bind_row), "DIAL-3A10");
    const uint32_t t_up = g.now + until_up(2000u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_UP);
    T_LOG("pick to BIND %ums, BIND to CONNECT %ums, CONNECT to UP %ums",
          (unsigned)(g.t_bind - t_pick), (unsigned)(g.t_connect - g.t_bind),
          (unsigned)(t_up - g.t_connect));
    /* The frame after it: no stale post took the bind back. */
    run(1000u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_UP);
    CHECK_STR_EQ(g.c.cfg.addr, DIAL_TEXT);
    CHECK_EQ(g.q_drops, 0u);
    /* Picked again: the same Dial, no reconnect, no refusal. */
    tap_row(&wireless_pane_bind_row);
    CHECK(choice_rect(0, &r));
    pick(0);
    run(200u);
    CHECK_EQ(g.bound_lines, 2u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_UP);
    CHECK_EQ(g.dial_connects, 1u);
}

/* SCAN FOR DIAL reading FOUND 1, the row the owner took for the add
 * button: a tap opens the found list, the Dial and SCAN AGAIN, and a pick
 * binds and connects.  SCAN AGAIN from the same row then scans beside the
 * link, which stays up, and the row reads FOUND 1 again. */
TEST_CASE(the_scan_row_reading_found_opens_the_list_and_binds)
{
    start("", 0u, false);
    (void)scan();
    tap_row(&wireless_pane_scan_row);
    CHECK_EQ(g.scan_lines, 1u);
    gfx_rect_t r;
    CHECK(choice_rect(1, &r));
    CHECK(!choice_rect(2, &r));
    const uint32_t t_pick = g.now;
    pick(0);
    CHECK(until_up(2000u) < 2000u);
    CHECK_EQ(g.bound_lines, 1u);
    CHECK_EQ(g.scan_lines, 1u);
    T_LOG("SCAN row pick to UP %ums", (unsigned)(g.now - t_pick));
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), DIAL_TEXT);
    tap_row(&wireless_pane_scan_row);
    pick(1);
    run(100u);
    CHECK_EQ(g.scan_lines, 2u);
    CHECK_STR_EQ(value_of(&wireless_pane_scan_row), TR(DIAL_SCAN_RUNS));
    run(10500u);
    char want[32];
    snprintf(want, sizeof want, TR(DIAL_SCAN_FOUND), 1u);
    CHECK_STR_EQ(value_of(&wireless_pane_scan_row), want);
    CHECK_EQ(g.bound_lines, 1u);
    CHECK_EQ(g.refused_lines, 0u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_UP);
}

/* BOUND DIAL tapped with no list to pick from: before any scan, and after
 * one that found none.  The band says why; nothing is queued. */
TEST_CASE(a_bound_dial_tap_with_no_list_says_why)
{
    start("", 0u, false);
    const uint32_t n0 = ui_router_alert_number();
    tap_row(&wireless_pane_bind_row);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(band(), TR(DIAL_BIND_EMPTY));
    dial_cmd_t c;
    CHECK(!wireless_pane_take_dial(&c));
    gfx_rect_t r;
    CHECK(!choice_rect(0, &r));
    /* A scan that hears nothing. */
    g.peer[OWN_DIAL].nadv = 0u;
    tap_row(&wireless_pane_scan_row);
    run(10500u);
    CHECK_STR_EQ(value_of(&wireless_pane_scan_row), TR(DIAL_SCAN_NONE));
    const uint32_t n1 = ui_router_alert_number();
    tap_row(&wireless_pane_bind_row);
    CHECK(ui_router_alert_number() != n1);
    CHECK_STR_EQ(band(), TR(DIAL_BIND_EMPTY));
    CHECK_EQ(g.bound_lines + g.refused_lines, 0u);
}

/* (b) SCAN AGAIN, the found list's last row, starts a second scan; BOUND
 * DIAL tapped during it says the scan runs, and binds once it ended. */
TEST_CASE(a_pick_while_a_second_scan_runs_says_so)
{
    start("", 0u, false);
    (void)scan();
    tap_row(&wireless_pane_scan_row);
    pick(1);
    run(100u);
    CHECK_STR_EQ(value_of(&wireless_pane_scan_row), TR(DIAL_SCAN_RUNS));
    CHECK_EQ(g.scan_lines, 2u);
    const uint32_t n0 = ui_router_alert_number();
    tap_row(&wireless_pane_bind_row);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(band(), TR(DIAL_BIND_SCANNING));
    gfx_rect_t r;
    CHECK(!choice_rect(0, &r));
    /* The scan's end: the list back, a pick binds. */
    run(10500u);
    tap_row(&wireless_pane_bind_row);
    pick(0);
    run(200u);
    CHECK_EQ(g.bound_lines, 1u);
    CHECK(until_up(2000u) < 2000u);
}

/* (b) The popup open on FOUND 1, a scan asked in the same frame as the
 * pick (the controller runs SCAN before BIND): the controller refuses the
 * bind, and the band says so instead of nothing. */
TEST_CASE(a_bind_the_controller_refuses_says_so)
{
    start("", 0u, false);
    (void)scan();
    tap_row(&wireless_pane_bind_row);
    gfx_rect_t r;
    CHECK(choice_rect(0, &r));
    /* The SCAN as the row's tap queues it, then the pick's UP. */
    wireless_pane_scan_row.tap();
    down(r.x + r.w / 2, r.y + r.h / 2);
    up(r.x + r.w / 2, r.y + r.h / 2);
    const uint32_t n0 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.scan_lines, 2u);
    CHECK_EQ(g.refused_lines, 1u);
    CHECK_EQ(g.bound_lines, 0u);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(band(), TR(DIAL_BIND_SCANNING));
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), "");
    /* The same refusal with no scan running: the list changed. */
    run(10500u);
    wireless_pane_bind_row.pick(0);
    g.c.found_n = 0u;
    const uint32_t n1 = ui_router_alert_number();
    run(3u * FRAME_MS);
    CHECK_EQ(g.refused_lines, 2u);
    CHECK(ui_router_alert_number() != n1);
    CHECK_STR_EQ(band(), TR(DIAL_BIND_GONE));
}

/* (c) A settings post with the old, empty address arrives between the
 * BIND and the frame's write-back (a value changed in the frame the bind
 * ran): the bind stands, the connect goes on, SETUP keeps the address. */
TEST_CASE(a_settings_post_between_bind_and_write_back_keeps_the_bind)
{
    start("", 0u, false);
    (void)scan();
    tap_row(&wireless_pane_bind_row);
    gfx_rect_t r;
    CHECK(choice_rect(0, &r));
    const int px = r.x + r.w / 2, py = r.y + r.h / 2;
    down(px, py);
    run(TAP_MS);
    up(px, py);
    /* Through the frame that sends the BIND, then the poll that runs it. */
    while (g.bound_lines == 0u) {
        run(1u);
    }
    CHECK_EQ(g.pub_seq, g.c.bind_seq);
    /* SETUP still holds "": a post now carries it. */
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), "");
    post_cfg();
    const uint32_t t0 = g.now;
    run(BLEC_MS);
    T_LOG("after the stale post: bound %d, cfg.addr \"%s\"",
          (int)g.c.b.bound, g.c.cfg.addr);
    run(2u * FRAME_MS);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), DIAL_TEXT);
    CHECK_STR_EQ(g.c.cfg.addr, DIAL_TEXT);
    CHECK(g.c.b.bound);
    const uint32_t up_ms = until_up(10000u);
    T_LOG("stale post to UP %ums", (unsigned)(g.now - t0));
    CHECK(up_ms < 10000u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_UP);
    CHECK_STR_EQ(value_of(&wireless_pane_bind_row), "DIAL-3A10");
}

/** @brief The waits of a sweep of picks (worst_wait()). */
typedef struct {
    uint32_t connect;  /* the longest pick to the Dial's CONNECT */
    uint32_t up;       /* the longest pick to UP */
    uint32_t free_go;  /* the longest BIND to CONNECT, the slot free at it */
    uint32_t turn;     /* the longest end of a running attempt to CONNECT */
    unsigned free_n;   /* picks with the slot free at the BIND */
    unsigned held_n;   /* picks with another client's connect running */
    unsigned busy;     /* the most busy answers to the Dial in one pick */
    unsigned turns;    /* the most turn answers to the Dial in one pick */
} waits_t;

/**
 * @brief The waits from a pick to the Dial's CONNECT, with the clients of
 *        @p others bound to peers that never answer, the pick at every
 *        250ms of a 40s window of their cycle.  Another client's connect
 *        that runs at the BIND ends 5,000ms after it took the slot, and its
 *        cancel's event 2ms on.
 * @param others WITH_IMU, WITH_PNODE.
 * @return The waits.
 */
static waits_t worst_wait(unsigned others)
{
    waits_t w;
    memset(&w, 0, sizeof w);
    for (uint32_t lead = 0u; lead < 40000u; lead += 250u) {
        start("", others, false);
        run(lead);
        (void)scan();
        tap_row(&wireless_pane_bind_row);
        const uint32_t t_pick = g.now;
        pick(0);
        const uint32_t up_ms = until_up(40000u);
        CHECK(up_ms < 40000u);
        CHECK_EQ(g.bound_lines, 1u);
        const uint32_t to_connect = g.t_connect - t_pick;
        const uint32_t to_up = g.now - t_pick;
        w.connect = to_connect > w.connect ? to_connect : w.connect;
        w.up = to_up > w.up ? to_up : w.up;
        if (g.gap_at_bind == BLEU_HUB_GAP_FREE) {
            const uint32_t go = g.t_connect - g.t_bind;
            w.free_go = go > w.free_go ? go : w.free_go;
            ++w.free_n;
        } else if (g.gap_at_bind == BLEU_HUB_GAP_CONNECT
                   && g.gap_owner_at_bind != OWN_DIAL) {
            const uint32_t end_at = g.gap_t_at_bind + BLEU_CONNECT_MS + 2u;
            const uint32_t turn = g.t_connect - end_at;
            w.turn = turn > w.turn ? turn : w.turn;
            ++w.held_n;
        }
        w.busy = g.busy_answers > w.busy ? g.busy_answers : w.busy;
        w.turns = g.turn_answers > w.turns ? g.turn_answers : w.turns;
    }
    T_LOG("others 0x%X: pick to CONNECT at most %ums, to UP %ums; slot free "
          "at %u BINDs, CONNECT at most %ums after; another's connect "
          "running at %u, CONNECT at most %ums after its end; at most %u "
          "busy and %u turn answers", others, (unsigned)w.connect,
          (unsigned)w.up, w.free_n, (unsigned)w.free_go, w.held_n,
          (unsigned)w.turn, w.busy, w.turns);
    return w;
}

/* (d) The IMU and the power node ON, bound to peers that never answer:
 * their connects share the hub's one connect with the Dial's.  The pick's
 * connect is a hurried one: an attempt of theirs that runs goes to its
 * end, and the Dial's connect takes the next turn, within its
 * BLEU_RETRY_TURN_MS retry and a poll.  With none running it goes at once,
 * as without them. */
TEST_CASE(a_pick_waits_for_one_running_attempt_at_most)
{
    const uint32_t ui = TAP_MS + 2u * FRAME_MS;
    const uint32_t turn = BLEU_RETRY_TURN_MS + 2u * BLEC_MS;
    const unsigned set[2] = { WITH_IMU, WITH_IMU | WITH_PNODE };
    for (unsigned k = 0; k < 2u; ++k) {
        const waits_t w = worst_wait(set[k]);
        CHECK(w.connect <= BLEU_CONNECT_MS + 2u + turn + ui);
        CHECK(w.free_n > 0u);
        CHECK(w.held_n > 0u);
        CHECK(w.free_go <= BLEC_MS);
        CHECK(w.turn <= turn);
        CHECK(w.up <= w.connect + 400u);
    }
}

/* (d) The Dial bound to an absent one first: its connects grow the
 * backoff to 30,000ms.  A scan and a bind of the present Dial connect at
 * once, not after the old backoff. */
TEST_CASE(a_bind_drops_the_backoff_of_an_old_dial)
{
    start(OLD_TEXT, 0u, true);
    run(120000u);
    CHECK(g.c.b.backoff_ms >= 16000u);
    CHECK_EQ(dial_ctl_link(&g.c), DIAL_LINK_CONNECTING);
    g.peer[OWN_DIAL].absent = false;
    (void)scan();
    tap_row(&wireless_pane_bind_row);
    const uint32_t t_pick = g.now;
    pick(0);
    const uint32_t up_ms = until_up(10000u);
    CHECK(up_ms < 10000u);
    T_LOG("old backoff: pick to CONNECT %ums, to UP %ums",
          (unsigned)(g.t_connect - t_pick), (unsigned)(g.now - t_pick));
    CHECK(g.t_connect - t_pick <= TAP_MS + FRAME_MS + 2u * BLEC_MS);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), DIAL_TEXT);
}

/* (e) The popup at 1024x600: its box inside the pane, beside its row and
 * not over it, and a DOWN and UP anywhere on choice 0 binds index 0. */
TEST_CASE(the_popup_choice_lands_on_index_0_at_1024x600)
{
    CHECK_EQ(UI_PANEL_W, 1024);
    CHECK_EQ(UI_PANEL_H, 600);
    static const int inset[][2] = { { 0, 0 }, { -1, 0 }, { 0, -1 },
                                    { -1, -1 }, { 0, 0 } };
    for (unsigned k = 0; k < sizeof inset / sizeof inset[0]; ++k) {
        start("", 0u, false);
        (void)scan();
        tap_row(&wireless_pane_bind_row);
        gfx_rect_t r, again;
        CHECK(choice_rect(1, &again));
        CHECK(!choice_rect(2, &r));
        CHECK(choice_rect(0, &r));
        /* SCAN AGAIN right under the Dial, not over it. */
        CHECK_EQ(again.x, r.x);
        CHECK_EQ(again.y, r.y + r.h);
        const gfx_rect_t row = settings_screen_row_rect(
            custom_row(&wireless_pane_bind_row));
        const int row_top = UI_BAND_H + row.y;
        const gfx_rect_t list = settings_screen_list_rect();
        CHECK(r.x >= list.x && r.x + r.w <= list.x + list.w);
        CHECK(r.y >= UI_BAND_H + list.y);
        CHECK(r.y + r.h <= UI_BAND_H + list.y + list.h);
        CHECK(r.y >= row_top + row.h || r.y + r.h <= row_top);
        CHECK(r.x >= 0 && r.x + r.w <= UI_PANEL_W);
        /* The corners, 1px in, and the middle. */
        const int x = k == 4u ? r.x + r.w / 2
                              : (inset[k][0] == 0 ? r.x : r.x + r.w - 1);
        const int y = k == 4u ? r.y + r.h / 2
                              : (inset[k][1] == 0 ? r.y : r.y + r.h - 1);
        down(x, y);
        run(TAP_MS);
        up(x, y);
        run(100u);
        CHECK_EQ(g.bound_lines, 1u);
        CHECK_STR_EQ(g.c.cfg.addr, DIAL_TEXT);
    }
}

int main(void)
{
    RUN(a_pick_in_the_frame_found_shows_binds_and_connects);
    RUN(the_scan_row_reading_found_opens_the_list_and_binds);
    RUN(a_bound_dial_tap_with_no_list_says_why);
    RUN(a_pick_while_a_second_scan_runs_says_so);
    RUN(a_bind_the_controller_refuses_says_so);
    RUN(a_settings_post_between_bind_and_write_back_keeps_the_bind);
    RUN(a_pick_waits_for_one_running_attempt_at_most);
    RUN(a_bind_drops_the_backoff_of_an_old_dial);
    RUN(the_popup_choice_lands_on_index_0_at_1024x600);
    return test_summary("dial_bind");
}
