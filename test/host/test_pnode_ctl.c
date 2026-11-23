/*
 * The panel's power node controller (pnode_ctl.h) against a scripted host
 * stack and power node.
 *
 * Under test: the profile's UUIDs are rcnode's; the link up with INFO read
 * and checked, a node of another protocol, of another role or with a
 * set-up ina228_calibrate() does not give taking no packet; STREAM's
 * windows becoming readings and the link UP, STALE after 300ms, LOST after
 * a drop; a scan refused while ARMED and one that runs stopped at the
 * switch to ARMED; the scan listing power nodes and nodes of no name yet,
 * not balance nodes; the bind; the RUN marker written with the run's tag
 * at the switch to ARMED, and once the link comes back within the run; an
 * INFO read that fails ending the link; the dropout error of a run on NODE
 * reaching the controller's count; POWER NODE OFF ending everything; the
 * update's hold handing the link to the update client and taking it back
 * with no STALE or LOST on the way, and the version read back from the
 * first STATUS after the reconnect.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bleuart.h"
#include "devimg.h"
#include "pnode_ctl.h"

/** @brief The handles the rig serves. @{ */
#define CONN       4u
#define SVC_START  0x0020u
#define SVC_END    0x002Bu
#define STREAM_VAL 0x0022u
#define STREAM_CCCD 0x0023u
#define CONTROL_VAL 0x0025u
#define INFO_VAL   0x0027u
#define STATUS_VAL 0x0029u
/** @} */

/** @brief The bound node's address. */
#define ADDR "7C:DF:A1:12:34:56 P"

/** @brief The rig. */
typedef struct {
    pnode_ctl_t c;        /**< the controller under test */
    uint32_t    now;      /**< the clock, ms */
    bool        armed;    /**< the bench is ARMED */
    bool        servo_run; /**< the run is SERVO's: it reads no BENCH */
    rcnode_info_t info;   /**< what INFO reads */
    unsigned    fail_info; /**< the next INFO reads that fail */
    uint16_t    tok_conn; /**< the last connect's token */
    unsigned    connects; /**< CONNECT actions */
    unsigned    scans;    /**< SCAN actions */
    unsigned    stops;    /**< SCAN_STOP actions */
    unsigned    writes;   /**< CONTROL writes but LANG */
    unsigned    langs;    /**< LANG writes */
    uint8_t     lang;     /**< the last one's language */
    uint16_t    lang_status; /**< the ATT status the node answers LANG with */
    uint8_t     wr[4];    /**< the last one's bytes */
    uint8_t     wr_len;   /**< its length */
    uint8_t     seq;      /**< the node's STREAM seq */
    bool        hold_read; /**< INFO reads wait for read_answer() */
    uint16_t    read_tok; /**< the waiting read's token, 0 for none */
} rig_t;

/** @brief An event with its kind and token. */
static bleu_evt_t ev(uint8_t kind, uint16_t token)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.token = token;
    e.conn = CONN;
    return e;
}

/** @brief A characteristic of the service. */
static void chr(rig_t *r, uint16_t token, uint16_t id, uint16_t val,
                uint8_t props)
{
    bleu_evt_t e = ev(BLEU_EV_CHR, token);
    rcnode_uuid(id, e.data);
    e.len = 16u;
    e.h1 = (uint16_t)(val - 1u);
    e.h2 = val;
    e.props = props;
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief Answer one action as the host stack and the node would. */
static void act(rig_t *r, const bleu_act_t *a)
{
    bleu_evt_t e;
    switch (a->kind) {
    case BLEU_ACT_SCAN:      ++r->scans; break;
    case BLEU_ACT_SCAN_STOP: ++r->stops; break;
    case BLEU_ACT_CONNECT:
        ++r->connects;
        r->tok_conn = a->token;
        e = ev(BLEU_EV_CONNECT, a->token);
        e.h1 = 40u;
        e.h3 = 400u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_SVC:
        e = ev(BLEU_EV_SVC, a->token);
        e.h1 = SVC_START;
        e.h2 = SVC_END;
        pnode_ctl_event(&r->c, &e, r->now);
        e = ev(BLEU_EV_SVC_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_CHRS:
        chr(r, a->token, RCNODE_ID_STREAM, STREAM_VAL, BLEU_PROP_NOTIFY);
        chr(r, a->token, RCNODE_ID_CONTROL, CONTROL_VAL,
            BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR);
        chr(r, a->token, RCNODE_ID_INFO, INFO_VAL, BLEU_PROP_READ);
        chr(r, a->token, RCNODE_ID_STATUS, STATUS_VAL,
            BLEU_PROP_READ | BLEU_PROP_NOTIFY);
        e = ev(BLEU_EV_CHR_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISC_DSCS:
        e = ev(BLEU_EV_DSC, a->token);
        e.h1 = STREAM_CCCD;
        e.data[0] = 0x02u;
        e.data[1] = 0x29u;
        e.len = 2u;
        pnode_ctl_event(&r->c, &e, r->now);
        e = ev(BLEU_EV_DSC_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_MTU:
        e = ev(BLEU_EV_MTU, a->token);
        e.h1 = 247u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_SUBSCRIBE:
        e = ev(BLEU_EV_WRITE_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_READ:
        if (r->hold_read) {
            r->read_tok = a->token;
            break;
        }
        e = ev(BLEU_EV_READ, a->token);
        e.len = (uint8_t)rcnode_enc_info(&r->info, e.data, sizeof e.data);
        if (r->fail_info > 0u) {
            --r->fail_info;
            e.len = 0u;
            e.status = 0x10Eu;
        }
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_WRITE:
    case BLEU_ACT_WRITE_NR:
        if (a->len == 2u && a->data[0] == RCNODE_OP_LANG) {
            ++r->langs;
            r->lang = a->data[1];
            e = ev(BLEU_EV_WRITE_DONE, a->token);
            e.status = r->lang_status;
            pnode_ctl_event(&r->c, &e, r->now);
            break;
        }
        ++r->writes;
        r->wr_len = a->len < sizeof r->wr ? a->len : (uint8_t)sizeof r->wr;
        memcpy(r->wr, a->data, r->wr_len);
        e = ev(BLEU_EV_WRITE_DONE, a->token);
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    case BLEU_ACT_DISCONNECT:
        e = ev(BLEU_EV_DISCONNECT, r->tok_conn);
        e.status = 0x216u;
        pnode_ctl_event(&r->c, &e, r->now);
        break;
    default:
        break;
    }
}

/** @brief Step the controller until it hands out nothing. */
static void step(rig_t *r)
{
    bleu_act_t a;
    for (int i = 0; i < 64 && pnode_ctl_step(&r->c, r->now, r->armed, !r->servo_run, &a);
         ++i) {
        act(r, &a);
    }
}

/** @brief Answer the INFO read held by hold_read. */
static void read_answer(rig_t *r)
{
    bleu_evt_t e = ev(BLEU_EV_READ, r->read_tok);
    e.len = (uint8_t)rcnode_enc_info(&r->info, e.data, sizeof e.data);
    r->read_tok = 0u;
    r->hold_read = false;
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief Run @p ms of the clock, a step each ms. */
static void run(rig_t *r, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        ++r->now;
        step(r);
    }
}

/** @brief One STREAM notification of @p n bytes. */
static void notify(rig_t *r, const uint8_t *b, size_t n)
{
    bleu_evt_t e = ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = STREAM_VAL;
    e.len = (uint8_t)n;
    memcpy(e.data, b, n);
    pnode_ctl_event(&r->c, &e, r->now);
}

/** @brief One window with the INA228 online: 10A, 22.2V. */
static void window(rig_t *r, uint8_t flags)
{
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.hdr.seq = r->seq++;
    w.flags = flags;
    w.n = 18u;
    w.i_mean = 10000000;
    w.i_max = 12000000;
    w.v_mean = 11100u;
    w.v_min = 11000u;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_pwr_win(&w, b, sizeof b);
    notify(r, b, n);
}

/** @brief One STATUS on STREAM: the cell at @p mv. */
static void status(rig_t *r, uint16_t mv, uint16_t flags)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.vbat = mv;
    s.flags = flags;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_status(&s, b, sizeof b);
    notify(r, b, n);
}

/** @brief One STATUS on STREAM naming firmware @p minor.@p patch. */
static void status_fw(rig_t *r, uint8_t minor, uint8_t patch)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.vbat = 3900u;
    s.fw[1] = minor;
    s.fw[2] = patch;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_status(&s, b, sizeof b);
    notify(r, b, n);
}

/** @brief POWER NODE ON with @p src_node, bound to ADDR, Bluetooth synced,
 *         at 1000ms; INFO the MATEK's. */
static void init(rig_t *r, bool src_node)
{
    memset(r, 0, sizeof *r);
    r->now = 1000u;
    r->info.proto = RCNODE_PROTO;
    r->info.role = RCNODE_ROLE_POWER;
    r->info.board = RCNODE_BOARD_C6_ZERO;
    r->info.i228_addr = 0x45u;
    r->info.adcrange = 1u;
    r->info.shunt_uohm = 200u;
    pnode_ctl_init(&r->c);
    pnode_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.en = true;
    cfg.src_node = src_node;
    memcpy(cfg.addr, ADDR, sizeof ADDR);
    pnode_ctl_config(&r->c, &cfg, r->now);
    pnode_ctl_radio(&r->c, IMU_RADIO_READY, r->now);
}

/** @brief init() and the link up with INFO read; 50ms of clock. */
static void up(rig_t *r, bool src_node)
{
    init(r, src_node);
    run(r, 50u);
}

TEST_CASE(the_profile_names_rcnodes_uuids)
{
    uint8_t u[16];
    rcnode_uuid(RCNODE_ID_SERVICE, u);
    CHECK(memcmp(pnode_profile.svc, u, 16) == 0);
    static const uint16_t ids[4] = { RCNODE_ID_STREAM, RCNODE_ID_CONTROL,
                                     RCNODE_ID_INFO, RCNODE_ID_STATUS };
    static const uint8_t roles[4] = { BLEU_ROLE_STREAM, BLEU_ROLE_CMD,
                                      BLEU_ROLE_PARAM, BLEU_ROLE_NONE };
    CHECK_EQ(pnode_profile.chr_n, 4u);
    for (unsigned k = 0; k < 4u; ++k) {
        rcnode_uuid(ids[k], u);
        CHECK(memcmp(pnode_profile.chr[k].uuid, u, 16) == 0);
        CHECK_EQ(pnode_profile.chr[k].role, roles[k]);
    }
    /* The scan lists the node's own advertising. */
    static const uint8_t addr[6] = { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    uint8_t ad[RCNODE_ADV_LEN];
    const size_t n = rcnode_adv_build(RCNODE_ROLE_POWER, addr, ad, sizeof ad);
    bleu_adv_t parsed;
    CHECK(bleu_adv_parse_for(&pnode_profile, ad, n, &parsed));
}

TEST_CASE(the_link_comes_up_and_windows_are_readings)
{
    rig_t r;
    init(&r, false);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_CONNECTING);
    run(&r, 50u);
    CHECK_EQ(r.connects, 1u);
    CHECK(r.c.info_ok);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_WAITING);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_UP);
    CHECK_EQ(r.c.packets, 1u);
    status(&r, 3920u, RCNODE_ST_SENSOR);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.link, PNODE_LINK_UP);
    CHECK(v.have_status);
    CHECK_EQ(v.vbat_mv, 3920u);
    CHECK_STR_EQ(v.bound_addr, ADDR);
    CHECK_EQ(v.proto, RCNODE_PROTO);
    CHECK(!v.bat_low && !v.bat_none && !v.bat_err);
    status(&r, 0u, RCNODE_ST_BAT_NONE);
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(v.bat_none && !v.bat_low && !v.bat_err);
    status(&r, 3400u, RCNODE_ST_BAT_LOW | RCNODE_ST_BAT_ERR);
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(v.bat_low && v.bat_err && !v.bat_none);
    run(&r, PNODE_STALE_MS);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_UP);
    run(&r, 1u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_STALE);
    /* Windows without the INA228 online: no sensor. */
    window(&r, 0u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_NO_SENSOR);
    /* A drop after a reading is a loss until the link is back. */
    window(&r, RCNODE_WF_ONLINE);
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_LOST);
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(!v.have_status);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK_EQ(r.connects, 2u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_WAITING);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_UP);
}

TEST_CASE(another_protocol_role_or_setup_takes_no_packet)
{
    rig_t r;
    init(&r, false);
    r.info.proto = 2u;
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_PROTO);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(r.c.packets, 0u);
    CHECK_EQ(r.c.unread, 1u);

    init(&r, false);
    r.info.role = RCNODE_ROLE_BALANCE;
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_NOT_NODE);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(r.c.packets, 0u);

    init(&r, false);
    r.info.shunt_uohm = 50u;
    r.info.adcrange = 0u;
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_SETUP);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(r.c.packets, 0u);
}

TEST_CASE(an_info_read_that_fails_ends_the_link_and_it_comes_back)
{
    rig_t r;
    init(&r, false);
    r.fail_info = 1u;
    run(&r, 50u);
    CHECK_EQ(r.c.info_failed, 1u);
    CHECK(!r.c.info_ok);
    run(&r, 8000u);
    CHECK(r.c.info_ok);
    CHECK(r.connects >= 2u);
}

TEST_CASE(a_scan_is_refused_while_armed_and_stopped_at_the_arm)
{
    rig_t r;
    init(&r, false);
    CHECK_EQ(pnode_ctl_scan(&r.c, true, r.now), PNODE_SCAN_ARMED);
    CHECK_EQ(r.c.refused, 1u);
    run(&r, 5u);
    CHECK_EQ(r.scans, 0u);
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_STARTED);
    run(&r, 5u);
    CHECK_EQ(r.scans, 1u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_SCANNING);
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_NOT_READY);
    r.armed = true;
    run(&r, 5u);
    CHECK_EQ(r.stops, 1u);
    CHECK(r.c.scan_cut);
    r.armed = false;
    run(&r, 3000u);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK(v.scan_cut);
    /* A new scan clears the last one's end and its cut. */
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_STARTED);
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(!v.scan_done);
    CHECK(!v.scan_cut);
    run(&r, BLEU_SCAN_MS + 100u);
    /* OFF refuses a scan, as does no sync. */
    pnode_ctl_radio(&r.c, IMU_RADIO_NO_SYNC, r.now);
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_NOT_READY);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_NO_BT);
    CHECK_EQ(pnode_ctl_scan(NULL, false, r.now), PNODE_SCAN_NOT_READY);
}

/** @brief One advertising report, and its scan response with the name when
 *         @p named. */
static void report(rig_t *r, uint8_t last, bool named, int8_t rssi,
                   uint8_t role)
{
    const uint8_t addr[6] = { last, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    bleu_evt_t e = ev(BLEU_EV_ADV, 0u);
    e.len = (uint8_t)rcnode_adv_build(role, addr, e.data, sizeof e.data);
    memcpy(e.addr.val, addr, 6);
    e.addr.type = BLEU_ADDR_PUBLIC;
    e.rssi = rssi;
    pnode_ctl_event(&r->c, &e, r->now);
    if (named) {
        e.len = (uint8_t)rcnode_rsp_build(role, addr, e.data, sizeof e.data);
        pnode_ctl_event(&r->c, &e, r->now);
    }
}

TEST_CASE(the_scan_lists_power_nodes_and_binds_one)
{
    rig_t r;
    init(&r, false);
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_STARTED);
    run(&r, 5u);
    report(&r, 0x01u, true, -50, RCNODE_ROLE_POWER);
    report(&r, 0x02u, true, -40, RCNODE_ROLE_BALANCE);
    report(&r, 0x03u, false, -70, RCNODE_ROLE_POWER);
    /* A balance node whose name never came: its manufacturer data says
     * balance, and it is not listed. */
    report(&r, 0x04u, false, -30, RCNODE_ROLE_BALANCE);
    run(&r, BLEU_SCAN_MS + 100u);
    pnode_view_t v;
    pnode_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK_EQ(v.found_n, 2u);
    CHECK_STR_EQ(v.found[0].name, "rcbench-pwr-3401");
    CHECK_EQ(v.found[0].rssi, -50);
    CHECK_STR_EQ(v.found[1].name, "");
    CHECK_STR_EQ(v.found[1].addr, "7C:DF:A1:12:34:03 P");
    CHECK(v.hidden >= 2u);
    CHECK(!pnode_ctl_bind(&r.c, 2u, r.now));
    CHECK(pnode_ctl_bind(&r.c, 0u, r.now));
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_STR_EQ(r.c.cfg.name, "rcbench-pwr-3401");
    CHECK_STR_EQ(r.c.cfg.addr, "7C:DF:A1:12:34:01 P");
}

TEST_CASE(the_run_marker_goes_at_the_arm_and_after_a_drop_within_the_run)
{
    rig_t r;
    up(&r, true);
    window(&r, RCNODE_WF_ONLINE);
    r.armed = true;
    run(&r, 2u);
    CHECK_EQ(r.writes, 1u);
    CHECK_EQ(r.wr_len, 2u);
    CHECK_EQ(r.wr[0], RCNODE_OP_RUN);
    CHECK_EQ(r.wr[1], 1u);
    CHECK_EQ(r.c.runs_sent, 1u);
    run(&r, 10u);
    CHECK_EQ(r.writes, 1u);
    /* The next run: tag 2.  The link is down at the arm: the write waits
     * for it. */
    r.armed = false;
    run(&r, 5u);
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    r.armed = true;
    run(&r, 5u);
    CHECK_EQ(r.writes, 1u);
    CHECK(r.c.run_owed);
    /* Its dropout ends the run on NODE: the controller counts it. */
    run(&r, 300u);
    CHECK_EQ(r.c.m.errors, 1u);
    run(&r, BLEU_RETRY_MS);
    CHECK_EQ(r.writes, 2u);
    CHECK_EQ(r.wr[1], 2u);
    /* SAFE drops a write that is still owed: a link back after it gets
     * none. */
    r.armed = false;
    run(&r, 5u);
    e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    r.armed = true;
    run(&r, 2u);
    CHECK(r.c.run_owed);
    r.armed = false;
    run(&r, 2u);
    CHECK(!r.c.run_owed);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(r.c.linked);
    CHECK_EQ(r.writes, 2u);
}

TEST_CASE(lang_goes_once_a_link_and_once_a_change_after_run)
{
    rig_t r;
    up(&r, true);
    run(&r, 5u);
    CHECK_EQ(r.langs, 1u);
    CHECK_EQ(r.lang, RCNODE_LANG_EN);
    run(&r, 100u);
    CHECK_EQ(r.langs, 1u);
    pnode_ctl_lang(&r.c, RCNODE_LANG_DE);
    run(&r, 5u);
    CHECK_EQ(r.langs, 2u);
    CHECK_EQ(r.lang, RCNODE_LANG_DE);
    /* A value past German is English. */
    pnode_ctl_lang(&r.c, 7u);
    run(&r, 5u);
    CHECK_EQ(r.langs, 3u);
    CHECK_EQ(r.lang, RCNODE_LANG_EN);
    /* RUN first: a change at the arm waits for the RUN write. */
    window(&r, RCNODE_WF_ONLINE);
    pnode_ctl_lang(&r.c, RCNODE_LANG_DE);
    r.armed = true;
    bleu_act_t a;
    CHECK(pnode_ctl_step(&r.c, ++r.now, r.armed, true, &a));
    CHECK_EQ(a.data[0], RCNODE_OP_RUN);
    act(&r, &a);
    run(&r, 5u);
    CHECK_EQ(r.writes, 1u);
    CHECK_EQ(r.langs, 4u);
    CHECK_EQ(r.lang, RCNODE_LANG_DE);
    /* A new link gets it again. */
    r.armed = false;
    run(&r, 5u);
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(r.c.linked);
    CHECK_EQ(r.langs, 5u);
    CHECK_EQ(r.lang, RCNODE_LANG_DE);
}

TEST_CASE(a_node_that_refuses_lang_keeps_its_link_and_is_not_asked_again)
{
    rig_t r;
    init(&r, true);
    r.lang_status = 0x113u;      /* "value not allowed": a node before LANG */
    run(&r, 50u);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(r.langs, 1u);
    run(&r, 500u);
    CHECK_EQ(r.langs, 1u);
    CHECK(r.c.linked);
    /* RUN still goes after the refusal. */
    window(&r, RCNODE_WF_ONLINE);
    r.armed = true;
    run(&r, 2u);
    CHECK_EQ(r.writes, 1u);
    CHECK_EQ(r.wr[0], RCNODE_OP_RUN);
}

TEST_CASE(power_node_off_ends_the_link_and_is_no_loss)
{
    rig_t r;
    up(&r, false);
    window(&r, RCNODE_WF_ONLINE);
    pnode_cfg_t cfg = r.c.cfg;
    cfg.en = false;
    pnode_ctl_config(&r.c, &cfg, r.now);
    run(&r, 50u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_OFF);
    CHECK(!r.c.lost);
    window(&r, RCNODE_WF_ONLINE);
    pnode_view_t v;
    pnode_ctl_view(NULL, 0u, &v);
    CHECK_EQ(v.link, PNODE_LINK_OFF);
    CHECK_EQ(pnode_ctl_link(NULL, 0u), PNODE_LINK_OFF);
    /* An unbind: NONE. */
    cfg.en = true;
    cfg.addr[0] = '\0';
    pnode_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_NONE);
    pnode_ctl_init(NULL);
    pnode_ctl_config(NULL, &cfg, 0u);
    pnode_ctl_radio(NULL, IMU_RADIO_READY, 0u);
    pnode_ctl_event(NULL, NULL, 0u);
    bleu_act_t a;
    CHECK(!pnode_ctl_step(NULL, 0u, false, true, &a));
    CHECK(!pnode_ctl_bind(NULL, 0u, 0u));
}

TEST_CASE(a_node_that_started_again_gets_run_again_and_a_servo_run_no_gap)
{
    rig_t r;
    up(&r, true);
    window(&r, RCNODE_WF_ONLINE);
    r.armed = true;
    run(&r, 2u);
    CHECK_EQ(r.writes, 1u);
    const uint8_t tag = r.wr[1];
    window(&r, RCNODE_WF_ONLINE);
    /* The link drops and comes back to a node that started again: its RUN
     * packets carry tag 0, and the panel writes RUN again. */
    bleu_evt_t e = ev(BLEU_EV_DISCONNECT, r.tok_conn);
    e.status = 0x213u;
    pnode_ctl_event(&r.c, &e, r.now);
    run(&r, BLEU_RETRY_MS + 100u);
    CHECK(r.c.linked);
    CHECK_EQ(r.writes, 1u);
    rcnode_pwr_run_t x;
    memset(&x, 0, sizeof x);
    x.i_max = RCNODE_I_NONE;
    x.v_min = RCNODE_V_NONE;
    uint8_t b[RCNODE_PKT_MAX];
    size_t n = rcnode_enc_pwr_run(&x, b, sizeof b);
    notify(&r, b, n);
    run(&r, 2u);
    CHECK_EQ(r.writes, 2u);
    CHECK_EQ(r.wr[0], RCNODE_OP_RUN);
    CHECK_EQ(r.wr[1], tag);
    /* Its next RUN packet carries the tag: nothing more. */
    x.tag = tag;
    n = rcnode_enc_pwr_run(&x, b, sizeof b);
    notify(&r, b, n);
    run(&r, 5u);
    CHECK_EQ(r.writes, 2u);

    /* A SERVO run on NODE does not end on the node's silence. */
    rig_t s;
    up(&s, true);
    s.servo_run = true;
    s.armed = true;
    run(&s, 2000u);
    CHECK_EQ(s.c.m.errors, 0u);
}

/** @brief Run @p ms of the clock and say whether the link read STALE or
 *         LOST at any ms of it. */
static bool run_saw_loss(rig_t *r, uint32_t ms)
{
    bool saw = false;
    for (uint32_t k = 0; k < ms; ++k) {
        ++r->now;
        step(r);
        const pnode_link_t l = pnode_ctl_link(&r->c, r->now);
        saw = saw || l == PNODE_LINK_STALE || l == PNODE_LINK_LOST;
    }
    return saw;
}

/* UPDATES picks the node's image by the board its INFO names: the
 * board reads once INFO is taken and stays while the update holds the
 * link off; another address clears it. */
TEST_CASE(the_board_reads_after_info_and_stays_over_the_update_hold)
{
    rig_t r;
    up(&r, false);
    CHECK(r.c.info_ok);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_C6_ZERO);

    init(&r, false);
    r.info.board = RCNODE_BOARD_ES3C28P;
    CHECK_EQ(pnode_ctl_board(&r.c), 0u);           /* no INFO yet */
    run(&r, 50u);
    CHECK(r.c.info_ok);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_ES3C28P);
    pnode_ctl_hold(&r.c, true, r.now);
    run(&r, 100u);
    CHECK(!r.c.info_ok);                           /* the link ended */
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_ES3C28P);
    CHECK_EQ(pnode_ctl_board(NULL), 0u);
}

/* Drop the update owed to the image the row no longer sends. */
static void follow(devimg_id_t *row, devimg_slot_t sl[DEVIMG_COUNT],
                   uint8_t board)
{
    const devimg_id_t gone = devimg_follow(row, devimg_pnode_image(board));
    if (gone < DEVIMG_COUNT) {
        (void)devimg_cancel(&sl[gone]);
    }
}

/* The display node reconnects while its image downloads: through every
 * millisecond of the reconnect, its INFO unread included, the board stays
 * the ES3C28P, and the update owed to the display image stays owed. */
TEST_CASE(a_reconnect_with_info_pending_keeps_the_board_and_the_owed_update)
{
    rig_t r;
    init(&r, false);
    r.info.board = RCNODE_BOARD_ES3C28P;
    run(&r, 50u);
    devimg_slot_t sl[DEVIMG_COUNT];
    for (unsigned i = 0; i < DEVIMG_COUNT; ++i) {
        devimg_init(&sl[i]);
    }
    devimg_id_t row = DEVIMG_PNODE;
    follow(&row, sl, pnode_ctl_board(&r.c));
    CHECK_EQ(row, DEVIMG_PNODE_S3);
    CHECK(devimg_begin(&sl[DEVIMG_PNODE_S3], DEVIMG_SRC_NET));
    /* The link drops and comes back by itself. */
    pnode_ctl_hold(&r.c, true, r.now);
    run(&r, 100u);
    r.hold_read = true;
    pnode_ctl_hold(&r.c, false, r.now);
    unsigned pending = 0u;
    for (unsigned k = 0; k < 500u; ++k) {
        ++r.now;
        step(&r);
        if (r.c.linked && !r.c.info_ok) {
            ++pending;                 /* linked, INFO unread */
        }
        CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_ES3C28P);
        follow(&row, sl, pnode_ctl_board(&r.c));
    }
    CHECK(pending > 100u);             /* the window was there */
    CHECK(r.read_tok != 0u);
    read_answer(&r);
    run(&r, 50u);
    CHECK(r.c.info_ok);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_ES3C28P);
    CHECK_EQ(row, DEVIMG_PNODE_S3);
    CHECK(sl[DEVIMG_PNODE_S3].owed);
    /* A new INFO naming another board moves the image and drops it. */
    r.info.board = RCNODE_BOARD_C6_ZERO;
    pnode_ctl_hold(&r.c, true, r.now);
    run(&r, 100u);
    pnode_ctl_hold(&r.c, false, r.now);
    run(&r, 500u);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_C6_ZERO);
    follow(&row, sl, pnode_ctl_board(&r.c));
    CHECK_EQ(row, DEVIMG_PNODE);
    CHECK(!sl[DEVIMG_PNODE_S3].owed);
}

/* SEARCH's list picked again for the node already connected: the link
 * stays, no new INFO comes, and the board stays.  Another node from the
 * list clears it. */
TEST_CASE(a_bind_of_the_bound_address_keeps_the_board_and_another_clears_it)
{
    rig_t r;
    init(&r, false);
    r.info.board = RCNODE_BOARD_ES3C28P;
    run(&r, 50u);
    CHECK(pnode_ctl_scan(&r.c, false, r.now) == PNODE_SCAN_STARTED);
    run(&r, 5u);
    report(&r, 0x56u, true, -50, RCNODE_ROLE_POWER);   /* ADDR itself */
    report(&r, 0x01u, true, -60, RCNODE_ROLE_POWER);
    run(&r, BLEU_SCAN_MS + 100u);
    CHECK_STR_EQ(r.c.found[0].addr, ADDR);
    CHECK(pnode_ctl_bind(&r.c, 0u, r.now));
    run(&r, 1000u);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_ES3C28P);
    CHECK(pnode_ctl_bind(&r.c, 1u, r.now));
    CHECK_EQ(pnode_ctl_board(&r.c), 0u);
}

/* An address change on SETUP and an unbind clear the board, and the row
 * drops the update owed to the display image. */
TEST_CASE(an_address_change_clears_the_board_and_drops_the_owed_update)
{
    rig_t r;
    init(&r, false);
    r.info.board = RCNODE_BOARD_ES3C28P;
    run(&r, 50u);
    devimg_slot_t sl[DEVIMG_COUNT];
    for (unsigned i = 0; i < DEVIMG_COUNT; ++i) {
        devimg_init(&sl[i]);
    }
    devimg_id_t row = DEVIMG_PNODE;
    follow(&row, sl, pnode_ctl_board(&r.c));
    CHECK(devimg_begin(&sl[DEVIMG_PNODE_S3], DEVIMG_SRC_NET));
    pnode_cfg_t cfg = r.c.cfg;
    memcpy(cfg.addr, "11:22:33:44:55:66 P", sizeof("11:22:33:44:55:66 P"));
    pnode_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(pnode_ctl_board(&r.c), 0u);
    follow(&row, sl, pnode_ctl_board(&r.c));
    CHECK_EQ(row, DEVIMG_PNODE);
    CHECK(!sl[DEVIMG_PNODE_S3].owed);
    /* The same address written again keeps a board. */
    up(&r, false);
    cfg = r.c.cfg;
    pnode_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(pnode_ctl_board(&r.c), RCNODE_BOARD_C6_ZERO);
    /* An unbind clears it. */
    cfg.addr[0] = '\0';
    pnode_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(pnode_ctl_board(&r.c), 0u);
    CHECK_EQ(devimg_follow(NULL, DEVIMG_PNODE), DEVIMG_COUNT);
}

/* The update's hand-over: the hold ends the link of a node that sent
 * readings and keeps it ended whatever SETUP sends; nothing on the way
 * reads STALE or LOST, so the band shows no loss.  The take-back connects
 * again, and the version comes from the first STATUS on the new link. */
TEST_CASE(the_update_hold_hands_the_link_over_and_takes_it_back)
{
    rig_t r;
    up(&r, true);
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_UP);
    bleu_addr_t a;
    bool linked = true, down = true;
    uint8_t ver[3] = { 9u, 9u, 9u };
    CHECK(pnode_ctl_target(&r.c, &a, &linked, ver, &down));
    CHECK(!linked);                       /* no STATUS yet: no version */
    CHECK(!down);
    status_fw(&r, 23u, 11u);
    CHECK(pnode_ctl_target(&r.c, &a, &linked, ver, &down));
    CHECK(linked);
    CHECK(!down);
    CHECK_EQ(ver[1], 23u);
    CHECK_EQ(ver[2], 11u);
    bleu_addr_t want;
    CHECK(bleu_addr_parse(ADDR, &want));
    CHECK(bleu_addr_eq(&a, &want));

    pnode_ctl_hold(&r.c, true, r.now);
    pnode_ctl_hold(&r.c, true, r.now);    /* twice is once */
    CHECK(!run_saw_loss(&r, 100u));
    CHECK(!r.c.linked);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_OFF);
    CHECK(pnode_ctl_target(&r.c, &a, &linked, ver, &down));
    CHECK(!linked);
    CHECK(down);
    /* SETUP's values while held keep the link ended; no scan, no
     * packet taken. */
    const unsigned connects = r.connects;
    pnode_cfg_t cfg = r.c.cfg;
    cfg.src_node = false;
    pnode_ctl_config(&r.c, &cfg, r.now);
    pnode_ctl_radio(&r.c, IMU_RADIO_READY, r.now);
    CHECK_EQ(pnode_ctl_scan(&r.c, false, r.now), PNODE_SCAN_NOT_READY);
    const uint32_t packets = r.c.packets;
    window(&r, RCNODE_WF_ONLINE);
    CHECK_EQ(r.c.packets, packets);
    CHECK(!run_saw_loss(&r, 30000u));
    CHECK_EQ(r.connects, connects);
    CHECK(r.c.cfg.en);                    /* SETUP's values kept */
    CHECK(!r.c.cfg.src_node);

    /* The take-back: the node, restarted on its new image, is back with
     * no loss read on the way. */
    pnode_ctl_hold(&r.c, false, r.now);
    /* Before the node answers: connecting, not lost. */
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_CONNECTING);
    CHECK(!run_saw_loss(&r, 100u));
    CHECK_EQ(r.connects, connects + 1u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_WAITING);
    CHECK(pnode_ctl_target(&r.c, &a, &linked, ver, &down));
    CHECK(!linked);                       /* the old link's STATUS is gone */
    status_fw(&r, 23u, 12u);
    window(&r, RCNODE_WF_ONLINE);
    CHECK(pnode_ctl_target(&r.c, &a, &linked, ver, &down));
    CHECK(linked);
    CHECK_EQ(ver[2], 12u);
    CHECK_EQ(pnode_ctl_link(&r.c, r.now), PNODE_LINK_UP);
    CHECK_EQ(r.c.m.errors, 0u);

    /* No node bound, or a missing argument: no target. */
    CHECK(!pnode_ctl_target(&r.c, &a, NULL, ver, &down));
    CHECK(!pnode_ctl_target(NULL, &a, &linked, ver, &down));
    rig_t n;
    init(&n, false);
    pnode_cfg_t none = n.c.cfg;
    none.addr[0] = '\0';
    pnode_ctl_config(&n.c, &none, n.now);
    CHECK(!pnode_ctl_target(&n.c, &a, &linked, ver, &down));
    pnode_ctl_hold(NULL, true, 0u);
}

int main(void)
{
    RUN(the_profile_names_rcnodes_uuids);
    RUN(the_link_comes_up_and_windows_are_readings);
    RUN(another_protocol_role_or_setup_takes_no_packet);
    RUN(an_info_read_that_fails_ends_the_link_and_it_comes_back);
    RUN(a_scan_is_refused_while_armed_and_stopped_at_the_arm);
    RUN(the_scan_lists_power_nodes_and_binds_one);
    RUN(the_run_marker_goes_at_the_arm_and_after_a_drop_within_the_run);
    RUN(power_node_off_ends_the_link_and_is_no_loss);
    RUN(lang_goes_once_a_link_and_once_a_change_after_run);
    RUN(a_node_that_refuses_lang_keeps_its_link_and_is_not_asked_again);
    RUN(a_node_that_started_again_gets_run_again_and_a_servo_run_no_gap);
    RUN(the_board_reads_after_info_and_stays_over_the_update_hold);
    RUN(a_reconnect_with_info_pending_keeps_the_board_and_the_owed_update);
    RUN(a_bind_of_the_bound_address_keeps_the_board_and_another_clears_it);
    RUN(an_address_change_clears_the_board_and_drops_the_owed_update);
    RUN(the_update_hold_hands_the_link_over_and_takes_it_back);
    return test_summary("pnode_ctl");
}
