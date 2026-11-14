/**
 * @file
 * @brief The balance node's controller on the panel (bal_ctl.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "bal_ctl.h"

#include <stdio.h>
#include <string.h>

_Static_assert(RCNODE_INFO_LEN <= BLEU_PARAM_MAX, "INFO is kept whole");
_Static_assert(RCNODE_STATUS_LEN <= BLEU_PARAM_MAX, "STATUS is kept whole");
_Static_assert(RCNODE_BAL_REPORT_LEN <= BLEU_EVT_DATA,
               "a report comes in one event");
_Static_assert(RCNODE_CTL_MAX <= BLEU_WRITE_MAX, "a CONTROL write is one write");
_Static_assert(BAL_BLE_OWNER < BLEU_OWNER_MAX, "the owner fits a token");

/** @brief An rcnode UUID, least significant byte first:
 *         72636265-6E63-6800-XXXX-000000000000 (rcnode_uuid()). */
#define BAL_UUID_LE(id) 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, \
    (uint8_t)((id) & 0xFFu), (uint8_t)((id) >> 8), \
    0x00u, 0x68u, 0x63u, 0x6Eu, 0x65u, 0x62u, 0x63u, 0x72u

/** @brief The node service. */
static const uint8_t k_svc[16] = { BAL_UUID_LE(RCNODE_ID_SERVICE) };
/** @brief STREAM: the reports, NOTIFY. */
static const uint8_t k_stream[16] = { BAL_UUID_LE(RCNODE_ID_STREAM) };
/** @brief STATUS: the battery, READ. */
static const uint8_t k_status[16] = { BAL_UUID_LE(RCNODE_ID_STATUS) };
/** @brief CONTROL: BLADES and RPM. */
static const uint8_t k_control[16] = { BAL_UUID_LE(RCNODE_ID_CONTROL) };
/** @brief INFO: protocol and role, READ. */
static const uint8_t k_info[16] = { BAL_UUID_LE(RCNODE_ID_INFO) };

const bleu_profile_t bal_profile = {
    .svc = k_svc,
    .chr = {
        { .uuid = k_stream,  .role = BLEU_ROLE_STREAM, .flags = 0u },
        { .uuid = k_control, .role = BLEU_ROLE_CMD,    .flags = 0u },
        { .uuid = k_info,    .role = BLEU_ROLE_PARAM,  .flags = 0u },
        { .uuid = k_status,  .role = BLEU_ROLE_AUX,    .flags = 0u },
    },
    .chr_n = 4u,
    /* The IMU's set, as the Dial's. */
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

/** @brief The RSSI of a report that carries none. */
#define RSSI_NONE 127

void bal_ctl_init(bal_ctl_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    bleu_init(&c->b);
    (void)bleu_setup(&c->b, &bal_profile, BAL_BLE_OWNER);
    c->blades = 2u;
    c->rpm_sent = BAL_RPM_UNSENT;
}

/**
 * @brief Run the central while ON and the radio is synced.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void apply(bal_ctl_t *c, uint32_t now_ms)
{
    bleu_enable(&c->b, c->cfg.en && c->radio == IMU_RADIO_READY, now_ms);
}

/**
 * @brief What a new link starts with: INFO unread, nothing written, no
 *        battery, the mode the reports will choose.
 * @param c The controller.
 */
static void link_reset(bal_ctl_t *c)
{
    c->info_read = false;
    c->info_ok = false;
    c->info_role = 0u;
    c->info_proto = 0u;
    c->blades_sent = 0u;
    c->rpm_sent = BAL_RPM_UNSENT;
    c->wr_op = 0u;
    c->wr_any = false;
    c->bat_known = false;
    c->aux_seen = c->b.aux_reads;
    c->noidx_n = 0u;
    c->noidx = false;
}

/**
 * @brief The link's own view after the central moved: a subscribe starts a
 *        link, INFO is taken once its read ended, a STATUS read taken.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void follow_link(bal_ctl_t *c, uint32_t now_ms)
{
    const bool up = bleu_link_up(&c->b);
    if (up != c->linked) {
        link_reset(c);
        c->t_status = now_ms;
    }
    c->linked = up;
    if (!up) {
        return;
    }
    if (!c->info_read && !c->b.param_due && !c->b.rd_busy) {
        rcnode_pkt_t p;
        c->info_read = true;
        if (c->b.param_status == 0u
            && rcnode_decode(c->b.param, c->b.param_len, &p) == RCNODE_OK
            && p.hdr.type == RCNODE_T_INFO) {
            c->info_role = p.u.info.role;
            c->info_proto = p.u.info.proto;
            c->info_ok = p.u.info.proto == RCNODE_PROTO
                         && p.u.info.role == RCNODE_ROLE_BALANCE;
        } else {
            /* INFO that would not read or decode: a failed GATT step.  The
             * link ends and comes back after the backoff. */
            ++c->info_failed;
            bleu_fail_link(&c->b, c->b.param_status, now_ms);
            c->linked = false;
            link_reset(c);
            return;
        }
    }
    if (c->b.aux_reads != c->aux_seen) {
        c->aux_seen = c->b.aux_reads;
        rcnode_pkt_t p;
        if (c->b.aux_status == 0u
            && rcnode_decode(c->b.aux, c->b.aux_len, &p) == RCNODE_OK
            && p.hdr.type == RCNODE_T_STATUS) {
            c->bat_known = true;
            c->bat_mv = p.u.status.vbat;
            c->bat_flags = p.u.status.flags;
        }
    }
}

/**
 * @brief A scan's end, read once.
 * @param c The controller.
 */
static void take_scan(bal_ctl_t *c)
{
    if (!c->scan_open || c->b.scanning) {
        return;
    }
    c->scan_open = false;
    c->scanning = false;
    c->scan_done = true;
}

/**
 * @brief An address the scan heard that is no balance node: counted once.
 * @param c The controller.
 * @param a The address.
 */
static void count_other(bal_ctl_t *c, const bleu_addr_t *a)
{
    for (uint8_t i = 0; i < c->seen_n; ++i) {
        if (bleu_addr_eq(&c->seen[i], a)) {
            return;
        }
    }
    for (uint8_t i = 0; i < c->found_n; ++i) {
        if (bleu_addr_eq(&c->found_addr[i], a)) {
            return;
        }
    }
    if (c->seen_n < BAL_SEEN_MAX) {
        c->seen[c->seen_n++] = *a;
    }
    if (c->hidden < UINT16_MAX) {
        ++c->hidden;
    }
}

/**
 * @brief The order of two RSSI values, a missing one last.
 * @param r The RSSI.
 * @return A key that sorts the strongest first.
 */
static int rssi_key(int8_t r)
{
    return r == RSSI_NONE ? -1000 : (int)r;
}

/**
 * @brief Row @p i moved up to its place by RSSI.
 * @param c The controller.
 * @param i The row.
 */
static void resort(bal_ctl_t *c, uint8_t i)
{
    while (i > 0u && rssi_key(c->found[i].rssi) > rssi_key(c->found[i - 1u].rssi)) {
        const bal_dev_t d = c->found[i];
        const bleu_addr_t a = c->found_addr[i];
        c->found[i] = c->found[i - 1u];
        c->found_addr[i] = c->found_addr[i - 1u];
        c->found[i - 1u] = d;
        c->found_addr[i - 1u] = a;
        --i;
    }
}

/**
 * @brief One advertising report during a scan.  A report that names the
 *        balance role lists its device; a scan response that names only the
 *        service refreshes a listed one; anything else is counted hidden.
 * @param c The controller.
 * @param e The ADV event.
 */
static void take_adv(bal_ctl_t *c, const bleu_evt_t *e)
{
    uint8_t role = 0u;
    const bool match = rcnode_adv_match(e->data, e->len, &role);
    uint8_t i = 0u;
    while (i < c->found_n && !bleu_addr_eq(&c->found_addr[i], &e->addr)) {
        ++i;
    }
    if (i < c->found_n) {
        if (e->rssi != RSSI_NONE) {
            c->found[i].rssi = e->rssi;
            resort(c, i);
        }
        return;
    }
    if (!match || role != RCNODE_ROLE_BALANCE) {
        if (!match || role != 0u) {
            count_other(c, &e->addr);
        }
        return;
    }
    if (c->found_n >= BAL_FOUND_MAX) {
        count_other(c, &e->addr);
        return;
    }
    bal_dev_t *d = &c->found[c->found_n];
    memset(d, 0, sizeof(*d));
    (void)bleu_addr_format(&e->addr, true, d->addr, sizeof(d->addr));
    bleu_adv_t adv;
    (void)bleu_adv_parse(e->data, e->len, &adv);
    memcpy(d->name, adv.name, sizeof(d->name) - 1u);
    d->rssi = e->rssi;
    c->found_addr[c->found_n] = e->addr;
    ++c->found_n;
    resort(c, (uint8_t)(c->found_n - 1u));
}

/**
 * @brief The address SETUP keeps: bind it, or unbind for "" or a text
 *        bleu_addr_parse() does not take.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void set_addr(bal_ctl_t *c, uint32_t now_ms)
{
    bleu_addr_t a;
    if (bleu_addr_parse(c->cfg.addr, &a)) {
        bleu_bind(&c->b, &a, now_ms);
    } else {
        bleu_bind(&c->b, NULL, now_ms);
    }
}

void bal_ctl_config(bal_ctl_t *c, const bal_cfg_t *cfg, uint32_t now_ms)
{
    if (c == NULL || cfg == NULL) {
        return;
    }
    const bal_cfg_t was = c->cfg;
    c->cfg = *cfg;
    c->cfg.addr[BAL_ADDR_MAX - 1u] = '\0';
    c->cfg.name[BAL_NAME_MAX - 1u] = '\0';
    if (!c->configured || strcmp(was.addr, c->cfg.addr) != 0) {
        set_addr(c, now_ms);
        c->have_rep = false;
    }
    if (!c->configured || was.en != c->cfg.en) {
        if (!c->cfg.en) {
            c->scan_open = false;
            c->scanning = false;
            c->scan_done = false;
            c->scan_cut = false;
            c->found_n = 0u;
            c->hidden = 0u;
            c->have_rep = false;
        }
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
    c->configured = true;
}

void bal_ctl_radio(bal_ctl_t *c, imu_radio_t radio, uint32_t now_ms)
{
    if (c != NULL) {
        c->radio = radio;
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
}

bal_scan_t bal_ctl_scan(bal_ctl_t *c, bool armed, uint32_t now_ms)
{
    if (c == NULL) {
        return BAL_SCAN_NOT_READY;
    }
    if (armed) {
        /* A scan holds the host stack's one scan or connect for 10s: the
         * Dial, which ends a run when lost, could not come back meanwhile. */
        ++c->refused;
        return BAL_SCAN_ARMED;
    }
    if (!c->cfg.en || c->radio != IMU_RADIO_READY || c->b.scanning) {
        return BAL_SCAN_NOT_READY;
    }
    c->found_n = 0u;
    c->seen_n = 0u;
    c->hidden = 0u;
    c->scan_done = false;
    c->scan_cut = false;
    c->scan_open = bleu_scan(&c->b, now_ms);
    c->scanning = c->scan_open;
    return c->scan_open ? BAL_SCAN_STARTED : BAL_SCAN_NOT_READY;
}

bool bal_ctl_bind(bal_ctl_t *c, uint8_t i, uint32_t now_ms)
{
    if (c == NULL || i >= c->found_n || c->b.scanning) {
        return false;
    }
    snprintf(c->cfg.addr, sizeof(c->cfg.addr), "%s", c->found[i].addr);
    snprintf(c->cfg.name, sizeof(c->cfg.name), "%s", c->found[i].name);
    bleu_bind(&c->b, &c->found_addr[i], now_ms);
    c->have_rep = false;
    ++c->bind_seq;
    follow_link(c, now_ms);
    return true;
}

/**
 * @brief The mode the report asks for: no-index once BAL_NOIDX_REPORTS in
 *        a row came without an index edge, the index again with the first
 *        that has one.
 * @param c The controller.
 * @param r The report.
 */
static void follow_index(bal_ctl_t *c, const rcnode_bal_report_t *r)
{
    if ((r->flags & RCNODE_BF_INDEX_SEEN) != 0u) {
        c->noidx_n = 0u;
        c->noidx = false;
    } else if (c->noidx_n < BAL_NOIDX_REPORTS) {
        if (++c->noidx_n >= BAL_NOIDX_REPORTS) {
            c->noidx = true;
        }
    }
}

/**
 * @brief One STREAM notification: a report taken once INFO matched.
 * @param c      The controller.
 * @param data   The bytes.
 * @param len    Their count.
 * @param now_ms The arrival.
 */
static void take_stream(bal_ctl_t *c, const uint8_t *data, size_t len,
                        uint32_t now_ms)
{
    if (!c->info_ok) {
        ++c->unread;
        return;
    }
    rcnode_pkt_t p;
    if (rcnode_decode(data, len, &p) != RCNODE_OK
        || p.hdr.type != RCNODE_T_BAL_REPORT) {
        ++c->bad;
        return;
    }
    if (c->have_rep) {
        c->lost += rcnode_seq_lost(c->rep.hdr.seq, p.u.bal.hdr.seq);
    }
    c->rep = p.u.bal;
    c->rep_ms = now_ms;
    c->have_rep = true;
    ++c->reports;
    follow_index(c, &c->rep);
}

void bal_ctl_event(bal_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms)
{
    if (c == NULL || e == NULL) {
        return;
    }
    if (e->kind == BLEU_EV_ADV && c->scanning) {
        take_adv(c, e);
    }
    const bool stream = bleu_event(&c->b, e, now_ms);
    take_scan(c);
    follow_link(c, now_ms);
    if (stream && c->cfg.en) {
        take_stream(c, e->data, e->len, now_ms);
    }
}

void bal_ctl_drive(bal_ctl_t *c, uint8_t blades, uint32_t rpm)
{
    if (c == NULL) {
        return;
    }
    if (blades < RCNODE_BLADES_MIN) {
        blades = RCNODE_BLADES_MIN;
    } else if (blades > RCNODE_BLADES_MAX) {
        blades = RCNODE_BLADES_MAX;
    }
    c->blades = blades;
    c->bench_rpm = (uint16_t)(rpm > RCNODE_RPM_MAX ? RCNODE_RPM_MAX : rpm);
}

/**
 * @brief The write in flight ended: what it set is what the node holds, or
 *        it goes again.
 * @param c The controller.
 */
static void follow_write(bal_ctl_t *c)
{
    if (c->wr_op == 0u || c->b.wr == BLEU_WR_BUSY) {
        return;
    }
    if (c->b.wr == BLEU_WR_OK) {
        if (c->wr_op == RCNODE_OP_BLADES) {
            c->blades_sent = (uint8_t)c->wr_val;
        } else {
            c->rpm_sent = c->wr_val;
        }
    } else {
        ++c->wr_failed;
    }
    c->wr_op = 0u;
}

/**
 * @brief The next CONTROL write, when one is due and the last one is
 *        BAL_CTL_MS old: BLADES before RPM.
 * @param c      The controller.
 * @param now_ms The clock.
 * @return true when a write was handed to the central.
 */
static bool write_due(bal_ctl_t *c, uint32_t now_ms)
{
    if (!c->info_ok || c->wr_op != 0u || !bleu_can_send(&c->b)
        || (c->wr_any && (uint32_t)(now_ms - c->t_wr) < BAL_CTL_MS)) {
        return false;
    }
    rcnode_ctl_t w;
    memset(&w, 0, sizeof(w));
    const uint32_t rpm = c->noidx ? c->bench_rpm : 0u;
    if (c->blades != c->blades_sent) {
        w.op = RCNODE_OP_BLADES;
        w.blades = c->blades;
    } else if (rpm != c->rpm_sent) {
        w.op = RCNODE_OP_RPM;
        w.rpm = (uint16_t)rpm;
    } else {
        return false;
    }
    uint8_t buf[RCNODE_CTL_MAX];
    const size_t n = rcnode_enc_ctl(&w, buf, sizeof(buf));
    if (n == 0u || !bleu_send(&c->b, buf, n, now_ms)) {
        return false;
    }
    c->wr_op = w.op;
    c->wr_val = w.op == RCNODE_OP_BLADES ? w.blades : w.rpm;
    c->wr_any = true;
    c->t_wr = now_ms;
    ++c->writes;
    return true;
}

bool bal_ctl_step(bal_ctl_t *c, uint32_t now_ms, bool armed, bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    follow_link(c, now_ms);
    follow_write(c);
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
    if (c->info_ok && (uint32_t)(now_ms - c->t_status) >= BAL_STATUS_MS
        && bleu_read_aux(&c->b)) {
        c->t_status = now_ms;
        return bleu_step(&c->b, now_ms, out);
    }
    if (write_due(c, now_ms)) {
        return bleu_step(&c->b, now_ms, out);
    }
    return false;
}

bal_link_t bal_ctl_link(const bal_ctl_t *c, uint32_t now_ms)
{
    if (c == NULL || !c->cfg.en) {
        return BAL_LINK_OFF;
    }
    switch (c->radio) {
    case IMU_RADIO_NO_LINK: return BAL_LINK_C6_FAILED;
    case IMU_RADIO_NO_SYNC: return BAL_LINK_NO_BT;
    case IMU_RADIO_READY:   break;
    case IMU_RADIO_OFF:
    case IMU_RADIO_STARTING:
    default:                return BAL_LINK_STARTING;
    }
    if (c->b.scanning) {
        return BAL_LINK_SCANNING;
    }
    if (!c->b.bound) {
        return BAL_LINK_NONE;
    }
    if (c->b.st == BLEU_ST_NOT_PEER) {
        return BAL_LINK_NOT_NODE;
    }
    if (!bleu_link_up(&c->b)) {
        return BAL_LINK_CONNECTING;
    }
    if (!c->info_read) {
        return BAL_LINK_WAITING;
    }
    if (!c->info_ok) {
        return BAL_LINK_WRONG;
    }
    if (c->have_rep && (uint32_t)(now_ms - c->rep_ms) < BAL_STALE_MS) {
        return BAL_LINK_UP;
    }
    return BAL_LINK_QUIET;
}

void bal_ctl_view(const bal_ctl_t *c, uint32_t now_ms, bal_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (c == NULL) {
        return;
    }
    out->link = (uint8_t)bal_ctl_link(c, now_ms);
    out->scan_done = c->scan_done;
    out->scan_cut = c->scan_cut;
    out->found_n = c->found_n;
    memcpy(out->found, c->found, sizeof(out->found));
    out->hidden = c->hidden;
    if (c->b.bound) {
        memcpy(out->bound_addr, c->cfg.addr, sizeof(out->bound_addr));
        memcpy(out->bound_name, c->cfg.name, sizeof(out->bound_name));
    }
    out->info_role = c->info_role;
    out->info_proto = c->info_proto;
    out->bat_known = c->bat_known && c->linked;
    out->bat_mv = c->bat_mv;
    out->bat_flags = c->bat_flags;
}

bool bal_ctl_report(const bal_ctl_t *c, uint32_t now_ms,
                    rcnode_bal_report_t *out, uint32_t *age_ms)
{
    if (c == NULL || out == NULL || age_ms == NULL || !c->cfg.en
        || !c->have_rep) {
        return false;
    }
    *out = c->rep;
    *age_ms = (uint32_t)(now_ms - c->rep_ms);
    return true;
}
