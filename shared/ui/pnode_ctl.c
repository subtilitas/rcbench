/**
 * @file
 * @brief The power node's controller on the panel (pnode_ctl.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "pnode_ctl.h"

#include <stdio.h>
#include <string.h>

_Static_assert(RCNODE_INFO_LEN <= BLEU_PARAM_MAX, "INFO is kept whole");
_Static_assert(RCNODE_PKT_MAX <= BLEU_EVT_DATA, "a packet is one event");
_Static_assert(RCNODE_CTL_MAX <= BLEU_WRITE_MAX, "a control is one write");
_Static_assert(PNODE_BLE_OWNER < BLEU_OWNER_MAX, "the owner fits a token");
_Static_assert(PNODE_ADDR_MAX == BLEU_ADDR_TEXT_MAX, "an address text fits");
_Static_assert(PNODE_NAME_MAX == BLEU_NAME_MAX, "a scan's name fits");
_Static_assert(PNODE_FOUND_MAX == BLEU_FOUND_MAX, "a scan's list fits");

/** @brief The rcnode service's UUID, least significant byte first. */
static const uint8_t k_svc[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
};
/** @brief STREAM's UUID: windows, totals, extremes and status, NOTIFY. */
static const uint8_t k_stream[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
};
/** @brief STATUS's UUID: READ and NOTIFY, never subscribed here. */
static const uint8_t k_status[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01,
    0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
};
/** @brief CONTROL's UUID: the RUN marker, WRITE. */
static const uint8_t k_control[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01,
    0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
};
/** @brief INFO's UUID: protocol, role and the INA228's set-up, READ. */
static const uint8_t k_info[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01,
    0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
};

const bleu_profile_t pnode_profile = {
    .svc = k_svc,
    .chr = {
        { .uuid = k_stream,  .role = BLEU_ROLE_STREAM, .flags = 0u },
        { .uuid = k_control, .role = BLEU_ROLE_CMD,    .flags = 0u },
        { .uuid = k_info,    .role = BLEU_ROLE_PARAM,  .flags = 0u },
        { .uuid = k_status,  .role = BLEU_ROLE_NONE,   .flags = 0u },
    },
    .chr_n = 4u,
    /* The IMU's set: 30ms to 50ms carries the 50 windows a second in two
     * or three notifications an interval. */
    .conn = {
        .itvl_min = BLEU_ITVL_MIN,
        .itvl_max = BLEU_ITVL_MAX,
        .latency = BLEU_LATENCY,
        .supervision = BLEU_SUPERVISION,
        .scan_itvl = BLEU_CONN_SCAN_ITVL,
        .scan_window = BLEU_CONN_SCAN_WINDOW,
    },
    .adv_svc = NULL,
    .adv_uuid16 = 0u,
};

void pnode_ctl_init(pnode_ctl_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    bleu_init(&c->b);
    (void)bleu_setup(&c->b, &pnode_profile, PNODE_BLE_OWNER);
    pnode_meter_init(&c->m);
}

/**
 * @brief POWER NODE ON and no update holding the link (pnode_ctl_hold()).
 * @param c The controller.
 * @return Whether the controller runs the link.
 */
static bool is_on(const pnode_ctl_t *c)
{
    return c->cfg.en && !c->held;
}

/**
 * @brief Run the central while POWER NODE is ON and the radio is synced.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void apply(pnode_ctl_t *c, uint32_t now_ms)
{
    bleu_enable(&c->b, is_on(c) && c->radio == IMU_RADIO_READY, now_ms);
}

/**
 * @brief INFO, read once after the subscribe: its protocol, its role and
 *        its INA228 set-up; one that will not read or decode ends the
 *        link, which comes back after the backoff.
 * @param c      The controller.
 * @param now_ms The clock.
 * @return false when the link was ended.
 */
static bool take_info(pnode_ctl_t *c, uint32_t now_ms)
{
    rcnode_pkt_t p;
    c->info_read = true;
    if (c->b.param_status != 0u
        || rcnode_decode(c->b.param, c->b.param_len, &p) != RCNODE_OK
        || p.hdr.type != RCNODE_T_INFO) {
        ++c->info_failed;
        bleu_fail_link(&c->b, c->b.param_status, now_ms);
        return false;
    }
    c->info_proto = p.u.info.proto;
    c->info_role = p.u.info.role;
    c->info_ok = p.u.info.proto == RCNODE_PROTO
                 && p.u.info.role == RCNODE_ROLE_POWER
                 && pnode_meter_info(&c->m, &p.u.info);
    return true;
}

/**
 * @brief The link's own view after the central moved: a subscribe is a
 *        link, a drop after a reading a loss; INFO taken once its read
 *        ended.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void follow_link(pnode_ctl_t *c, uint32_t now_ms)
{
    const bool up = bleu_link_up(&c->b);
    if (up && !c->linked) {
        pnode_meter_link(&c->m);
        c->info_read = false;
        c->info_ok = false;
        c->info_proto = 0u;
        c->info_role = 0u;
        c->was_up = false;
        c->m.have_status = false;
    } else if (!up && c->linked) {
        c->lost = c->was_up;
        c->info_ok = false;
    }
    c->linked = up;
    if (up && !c->info_read && !c->b.param_due && !c->b.rd_busy
        && !take_info(c, now_ms)) {
        follow_link(c, now_ms);
    }
}

/**
 * @brief The role a scan's reports named for @p a.
 * @param c The controller.  @param a The address.
 * @return RCNODE_ROLE_*, 0 for none named.
 */
static uint8_t role_seen(const pnode_ctl_t *c, const bleu_addr_t *a)
{
    for (uint8_t i = 0; i < c->roles_n; ++i) {
        if (bleu_addr_eq(&c->role_addr[i], a)) {
            return c->role_of[i];
        }
    }
    return 0u;
}

/**
 * @brief One advertising report while a scan runs: the role it names,
 *        kept for its address.  The first report that names a role decides,
 *        and the advertising, whose manufacturer data names it, comes
 *        before the scan response's name.
 * @param c The controller.  @param e The report.
 */
static void take_report(pnode_ctl_t *c, const bleu_evt_t *e)
{
    uint8_t role = 0u;
    if (!c->scan_open || !rcnode_adv_match(e->data, e->len, &role)
        || role == 0u || role_seen(c, &e->addr) != 0u
        || c->roles_n >= PNODE_ROLES_MAX) {
        return;
    }
    c->role_addr[c->roles_n] = e->addr;
    c->role_of[c->roles_n] = role;
    ++c->roles_n;
}

/**
 * @brief A scan's end, read once: its nodes of the power role or of no
 *        name yet, the rest counted as hidden.
 * @param c The controller.
 */
static void take_scan(pnode_ctl_t *c)
{
    if (!c->scan_open || c->b.scanning) {
        return;
    }
    c->scan_open = false;
    c->scan_done = true;
    const bleu_list_t *l = &c->b.list;
    memset(c->found, 0, sizeof(c->found));
    c->found_n = 0u;
    c->hidden = l->hidden;
    for (uint8_t i = 0; i < l->found_n; ++i) {
        const char *name = l->found[i].name;
        if (role_seen(c, &l->found[i].addr) != RCNODE_ROLE_POWER) {
            ++c->hidden;    /* a balance node, or no role named */
            continue;
        }
        pnode_dev_t *o = &c->found[c->found_n++];
        (void)bleu_addr_format(&l->found[i].addr, true, o->addr,
                               sizeof(o->addr));
        memcpy(o->name, name, sizeof(o->name) - 1u);
        o->rssi = l->found[i].rssi;
    }
}

/**
 * @brief The address SETUP keeps: bind it, or unbind for "" or a text
 *        bleu_addr_parse() does not take.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void set_addr(pnode_ctl_t *c, uint32_t now_ms)
{
    bleu_addr_t a;
    bleu_bind(&c->b, bleu_addr_parse(c->cfg.addr, &a) ? &a : NULL, now_ms);
}

void pnode_ctl_config(pnode_ctl_t *c, const pnode_cfg_t *cfg,
                      uint32_t now_ms)
{
    if (c == NULL || cfg == NULL) {
        return;
    }
    const pnode_cfg_t was = c->cfg;
    c->cfg = *cfg;
    c->cfg.addr[PNODE_ADDR_MAX - 1u] = '\0';
    c->cfg.name[PNODE_NAME_MAX - 1u] = '\0';
    if (!c->configured || strcmp(was.addr, c->cfg.addr) != 0) {
        set_addr(c, now_ms);
        c->lost = false;
    }
    if (!c->configured || was.en != c->cfg.en) {
        if (!c->cfg.en) {
            /* Switched off on the panel: no loss, nothing kept. */
            c->linked = false;
            c->lost = false;
            c->scan_open = false;
            c->scan_done = false;
            c->scan_cut = false;
            c->found_n = 0u;
            c->hidden = 0u;
        }
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
    c->configured = true;
}

void pnode_ctl_radio(pnode_ctl_t *c, imu_radio_t radio, uint32_t now_ms)
{
    if (c != NULL) {
        c->radio = radio;
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
}

void pnode_ctl_hold(pnode_ctl_t *c, bool on, uint32_t now_ms)
{
    if (c == NULL || on == c->held) {
        return;
    }
    c->held = on;
    /* The link ends for the update and comes back to a node that
     * restarted: neither is a loss, as POWER NODE OFF is none. */
    c->lost = false;
    c->scan_open = false;
    c->scan_cut = false;
    apply(c, now_ms);
    follow_link(c, now_ms);
}

bool pnode_ctl_target(const pnode_ctl_t *c, bleu_addr_t *addr, bool *linked,
                      uint8_t version[3], bool *down)
{
    if (c == NULL || addr == NULL || linked == NULL || version == NULL
        || down == NULL || !c->b.bound
        || !bleu_addr_parse(c->cfg.addr, addr)) {
        return false;
    }
    *linked = c->linked && c->info_ok && c->m.have_status;
    memcpy(version, c->m.status.fw, 3u);
    *down = !c->linked && c->b.settle == 0u
            && (c->b.st == BLEU_ST_OFF || c->b.st == BLEU_ST_IDLE);
    return true;
}

pnode_scan_t pnode_ctl_scan(pnode_ctl_t *c, bool armed, uint32_t now_ms)
{
    if (c == NULL) {
        return PNODE_SCAN_NOT_READY;
    }
    if (armed) {
        /* A scan holds the host stack's one scan or connect for up to
         * 12s: a node or a Dial that drops then could not come back. */
        ++c->refused;
        return PNODE_SCAN_ARMED;
    }
    if (!is_on(c) || c->radio != IMU_RADIO_READY || c->b.scanning) {
        return PNODE_SCAN_NOT_READY;
    }
    c->found_n = 0u;
    c->hidden = 0u;
    c->scan_done = false;
    c->scan_cut = false;
    c->roles_n = 0u;
    c->scan_open = bleu_scan(&c->b, now_ms);
    return c->scan_open ? PNODE_SCAN_STARTED : PNODE_SCAN_NOT_READY;
}

bool pnode_ctl_bind(pnode_ctl_t *c, uint8_t i, uint32_t now_ms)
{
    if (c == NULL || i >= c->found_n || c->b.scanning) {
        return false;
    }
    bleu_addr_t a;
    if (!bleu_addr_parse(c->found[i].addr, &a)) {
        return false;
    }
    snprintf(c->cfg.addr, sizeof(c->cfg.addr), "%s", c->found[i].addr);
    snprintf(c->cfg.name, sizeof(c->cfg.name), "%s", c->found[i].name);
    bleu_bind(&c->b, &a, now_ms);
    c->lost = false;
    ++c->bind_seq;
    follow_link(c, now_ms);
    return true;
}

void pnode_ctl_event(pnode_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms)
{
    if (c == NULL || e == NULL) {
        return;
    }
    if (e->kind == BLEU_EV_ADV) {
        take_report(c, e);
    }
    const bool stream = bleu_event(&c->b, e, now_ms);
    take_scan(c);
    follow_link(c, now_ms);
    if (!stream || !is_on(c)) {
        return;
    }
    if (!c->info_ok) {
        ++c->unread;
        return;
    }
    const uint32_t readings = c->m.r.win_n;
    if (pnode_meter_packet(&c->m, e->data, e->len, now_ms) != 0u) {
        ++c->packets;
    }
    if (c->m.r.win_n != readings) {
        c->was_up = true;
        c->lost = false;
    }
}

/**
 * @brief The RUN write owed, when the central can send: the run's tag.
 * @param c      The controller.
 * @param now_ms The clock.
 * @return true when it was handed to the central.
 */
static bool send_run(pnode_ctl_t *c, uint32_t now_ms)
{
    if (!c->run_owed || !c->info_ok || !bleu_can_send(&c->b)) {
        return false;
    }
    const rcnode_ctl_t w = { .op = RCNODE_OP_RUN, .tag = c->m.tag };
    uint8_t buf[RCNODE_CTL_MAX];
    const size_t n = rcnode_enc_ctl(&w, buf, sizeof(buf));
    if (n == 0u || !bleu_send(&c->b, buf, n, now_ms)) {
        return false;
    }
    c->run_owed = false;
    ++c->runs_sent;
    return true;
}

bool pnode_ctl_step(pnode_ctl_t *c, uint32_t now_ms, bool armed,
                    bool bench_run, bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    follow_link(c, now_ms);
    if (pnode_meter_step(&c->m, now_ms, armed, c->cfg.src_node,
                         bench_run)) {
        c->run_owed = true;
    }
    if (c->m.tag_wrong) {
        /* The node started again since the run's RUN write. */
        c->m.tag_wrong = false;
        c->run_owed = true;
    }
    if (!armed) {
        c->run_owed = false;
    }
    /* No scan while ARMED: one started while SAFE ends at the switch. */
    if (armed && c->b.scanning) {
        bleu_scan_stop(&c->b, now_ms);
        c->scan_cut = true;
    }
    if (bleu_step(&c->b, now_ms, out)) {
        take_scan(c);
        follow_link(c, now_ms);
        return true;
    }
    take_scan(c);
    if (send_run(c, now_ms)) {
        return bleu_step(&c->b, now_ms, out);
    }
    return false;
}

pnode_link_t pnode_ctl_link(const pnode_ctl_t *c, uint32_t now_ms)
{
    if (c == NULL || !is_on(c)) {
        return PNODE_LINK_OFF;
    }
    switch (c->radio) {
    case IMU_RADIO_NO_LINK: return PNODE_LINK_C6_FAILED;
    case IMU_RADIO_NO_SYNC: return PNODE_LINK_NO_BT;
    case IMU_RADIO_READY:   break;
    case IMU_RADIO_OFF:
    case IMU_RADIO_STARTING:
    default:                return PNODE_LINK_STARTING;
    }
    if (c->b.scanning) {
        return PNODE_LINK_SCANNING;
    }
    if (!c->b.bound) {
        return PNODE_LINK_NONE;
    }
    if (c->b.st == BLEU_ST_NOT_PEER) {
        return PNODE_LINK_NOT_NODE;
    }
    if (!bleu_link_up(&c->b)) {
        return c->lost ? PNODE_LINK_LOST : PNODE_LINK_CONNECTING;
    }
    if (!c->info_read) {
        return PNODE_LINK_WAITING;
    }
    if (c->info_proto != RCNODE_PROTO) {
        return PNODE_LINK_PROTO;
    }
    if (c->info_role != RCNODE_ROLE_POWER) {
        return PNODE_LINK_NOT_NODE;
    }
    if (!c->info_ok) {
        return PNODE_LINK_SETUP;
    }
    const pnode_reading_t *r = &c->m.r;
    if (r->have_win && now_ms - r->win_ms <= PNODE_STALE_MS) {
        return PNODE_LINK_UP;
    }
    if (c->m.have_any && now_ms - c->m.any_ms <= PNODE_STALE_MS) {
        return PNODE_LINK_NO_SENSOR;    /* windows without a reading */
    }
    return c->was_up ? PNODE_LINK_STALE : PNODE_LINK_WAITING;
}

void pnode_ctl_view(const pnode_ctl_t *c, uint32_t now_ms, pnode_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (c == NULL) {
        return;
    }
    out->link = (uint8_t)pnode_ctl_link(c, now_ms);
    out->scan_done = c->scan_done;
    out->scan_cut = c->scan_cut;
    out->found_n = c->found_n;
    memcpy(out->found, c->found, sizeof(out->found));
    out->hidden = c->hidden;
    if (c->b.bound) {
        memcpy(out->bound_addr, c->cfg.addr, sizeof(out->bound_addr));
        memcpy(out->bound_name, c->cfg.name, sizeof(out->bound_name));
    }
    out->proto = c->info_proto;
    if (c->linked && c->m.have_status) {
        const uint16_t f = c->m.status.flags;
        out->have_status = true;
        out->bat_low = (f & RCNODE_ST_BAT_LOW) != 0u;
        out->bat_none = (f & RCNODE_ST_BAT_NONE) != 0u;
        out->bat_err = (f & RCNODE_ST_BAT_ERR) != 0u;
        out->vbat_mv = c->m.status.vbat;
    }
}
