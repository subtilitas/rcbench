/**
 * @file
 * @brief The M5Stack Dial's controller on the panel (dial_ctl.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "dial_ctl.h"

#include <stdio.h>
#include <string.h>

_Static_assert(DK_LEN_INFO <= BLEU_PARAM_MAX, "INFO is kept whole");
_Static_assert(DK_PKT_MAX <= BLEU_WRITE_MAX, "a panel packet is one write");
_Static_assert(DIAL_BLE_OWNER < BLEU_OWNER_MAX, "the owner fits a token");

/** @brief The dialknob service's UUID, least significant byte first. */
static const uint8_t k_svc[16] = { DK_UUID_LE(DK_UUID_SVC) };
/** @brief EVENT's UUID: the Dial's detents and gestures, NOTIFY. */
static const uint8_t k_event[16] = { DK_UUID_LE(DK_UUID_EVENT) };
/** @brief STATUS's UUID: battery and link, never subscribed here. */
static const uint8_t k_status[16] = { DK_UUID_LE(DK_UUID_STATUS) };
/** @brief SCREEN's UUID: the panel packets, WRITE NO RESPONSE. */
static const uint8_t k_screen[16] = { DK_UUID_LE(DK_UUID_SCREEN) };
/** @brief INFO's UUID: protocol, firmware and name, READ. */
static const uint8_t k_info[16] = { DK_UUID_LE(DK_UUID_INFO) };

const bleu_profile_t dial_profile = {
    .svc = k_svc,
    .chr = {
        { .uuid = k_event,  .role = BLEU_ROLE_STREAM, .flags = 0u },
        { .uuid = k_screen, .role = BLEU_ROLE_CMD,    .flags = 0u },
        { .uuid = k_info,   .role = BLEU_ROLE_PARAM,  .flags = 0u },
        { .uuid = k_status, .role = BLEU_ROLE_NONE,   .flags = 0u },
    },
    .chr_n = 4u,
    /* The IMU's set, which connected on the first try on the station; the
     * Dial asks for its own once subscribed (DK_ITVL_MIN). */
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

/** @brief SET_DIAL_THR's and SET_DIAL_SRV's options in tenths. */
static const uint8_t k_steps[] = { 5u, 10u, 20u, 50u };

uint8_t dial_step_tenths(int option)
{
    if (option < 0 || option >= (int)(sizeof(k_steps) / sizeof(k_steps[0]))) {
        return 10u;
    }
    return k_steps[option];
}

void dial_ctl_init(dial_ctl_t *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    bleu_init(&c->b);
    (void)bleu_setup(&c->b, &dial_profile, DIAL_BLE_OWNER);
    dk_track_init(&c->track);
    dk_link_init(&c->link);
    dial_stream_init(&c->stream);
    c->cfg.thr_tenths = 10u;
    c->cfg.srv_tenths = 10u;
}

/**
 * @brief Run the central while Dial is ON and the radio is synced.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void apply(dial_ctl_t *c, uint32_t now_ms)
{
    bleu_enable(&c->b, c->cfg.en && c->radio == IMU_RADIO_READY, now_ms);
}

/**
 * @brief The link's own view after the central moved: a subscribe is a
 *        connect, the loss clock waiting for the first EVENT; its end a
 *        disconnect; INFO taken once its read ended.
 * @param c The controller.
 * @param now_ms The clock.
 */
static void follow_link(dial_ctl_t *c, uint32_t now_ms)
{
    const bool up = bleu_link_up(&c->b);
    if (up && !c->linked) {
        dk_link_connect(&c->link, now_ms);
        dk_track_reset(&c->track);
        dial_stream_reset(&c->stream);
        c->pend_len = 0u;
        c->info_read = false;
        c->info_ok = false;
        c->info_proto = 0u;
        c->rtt_valid = false;
    } else if (!up && c->linked) {
        dk_link_disconnect(&c->link);
        dk_track_reset(&c->track);
        c->info_read = false;
        c->info_ok = false;
    }
    c->linked = up;
    if (up && !c->info_read && !c->b.param_due && !c->b.rd_busy) {
        dk_info_t i;
        c->info_read = true;
        if (c->b.param_status == 0u
            && dk_info_decode(c->b.param, c->b.param_len, &i)) {
            c->info = i;
            c->info_proto = i.proto;
            c->info_ok = i.proto == DK_PROTOCOL;
        } else {
            /* INFO that would not read or decode: a failed GATT step.
             * The link ends and comes back after the backoff, rather than
             * standing connected and silent. */
            ++c->info_failed;
            bleu_fail_link(&c->b, c->b.param_status, now_ms);
            follow_link(c, now_ms);
        }
    }
}

/**
 * @brief A scan's end, read once: its Dials, the hidden count.
 * @param c The controller.
 */
static void take_scan(dial_ctl_t *c)
{
    if (!c->scan_open || c->b.scanning) {
        return;
    }
    c->scan_open = false;
    c->scan_done = true;
    const bleu_list_t *l = &c->b.list;
    memset(c->found, 0, sizeof(c->found));
    c->found_n = l->found_n > DIAL_FOUND_MAX ? (uint8_t)DIAL_FOUND_MAX
                                             : l->found_n;
    for (uint8_t i = 0; i < c->found_n; ++i) {
        dial_dev_t *o = &c->found[i];
        (void)bleu_addr_format(&l->found[i].addr, true, o->addr,
                               sizeof(o->addr));
        memcpy(o->name, l->found[i].name, sizeof(o->name) - 1u);
        o->rssi = l->found[i].rssi;
    }
    c->hidden = l->hidden;
}

/**
 * @brief The address SETUP keeps: bind it, or unbind for "" or a text
 *        bleu_addr_parse() does not take.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void set_addr(dial_ctl_t *c, uint32_t now_ms)
{
    bleu_addr_t a;
    if (bleu_addr_parse(c->cfg.addr, &a)) {
        bleu_bind(&c->b, &a, now_ms);
    } else {
        bleu_bind(&c->b, NULL, now_ms);
    }
}

void dial_ctl_config(dial_ctl_t *c, const dial_cfg_t *cfg, uint32_t now_ms)
{
    if (c == NULL || cfg == NULL) {
        return;
    }
    const dial_cfg_t was = c->cfg;
    c->cfg = *cfg;
    c->cfg.addr[DIAL_ADDR_MAX - 1u] = '\0';
    c->cfg.name[DIAL_NAME_MAX - 1u] = '\0';
    if (!c->configured || strcmp(was.addr, c->cfg.addr) != 0) {
        set_addr(c, now_ms);
    }
    if (!c->configured || was.en != c->cfg.en) {
        if (!c->cfg.en) {
            /* Switched off on the panel: no loss, nothing kept. */
            dk_link_off(&c->link);
            dk_track_reset(&c->track);
            c->linked = false;
            c->scan_open = false;
            c->scan_done = false;
            c->scan_cut = false;
            c->found_n = 0u;
            c->hidden = 0u;
            memset(&c->in, 0, sizeof(c->in));
        }
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
    c->configured = true;
}

void dial_ctl_radio(dial_ctl_t *c, imu_radio_t radio, uint32_t now_ms)
{
    if (c != NULL) {
        c->radio = radio;
        apply(c, now_ms);
        follow_link(c, now_ms);
    }
}

dial_scan_t dial_ctl_scan(dial_ctl_t *c, bool armed, uint32_t now_ms)
{
    if (c == NULL) {
        return DIAL_SCAN_NOT_READY;
    }
    if (armed) {
        /* A scan holds the host stack's one scan or connect for up to
         * 12s: a Dial that drops then could not come back.  The bench's
         * ARMED state is no time to look for one. */
        ++c->refused;
        return DIAL_SCAN_ARMED;
    }
    if (!c->cfg.en || c->radio != IMU_RADIO_READY || c->b.scanning) {
        return DIAL_SCAN_NOT_READY;
    }
    c->found_n = 0u;
    c->hidden = 0u;
    c->scan_done = false;
    c->scan_cut = false;
    c->scan_open = bleu_scan(&c->b, now_ms);
    return c->scan_open ? DIAL_SCAN_STARTED : DIAL_SCAN_NOT_READY;
}

bool dial_ctl_bind(dial_ctl_t *c, uint8_t i, uint32_t now_ms)
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
    ++c->bind_seq;
    follow_link(c, now_ms);
    return true;
}

/**
 * @brief One EVENT's bytes: decoded, the link's clock, the turn and the
 *        gesture counted.  The buttons' held states are read by nobody.
 * @param c      The controller.
 * @param data   The bytes.
 * @param len    Their count.
 * @param now_ms The arrival.
 */
static void take_event(dial_ctl_t *c, const uint8_t *data, size_t len,
                       uint32_t now_ms)
{
    dk_evt_t ev;
    if (!c->info_ok) {
        ++c->unread;
        return;
    }
    if (!dk_evt_decode(data, len, &ev)) {
        ++c->bad;
        return;
    }
    dk_link_rx(&c->link, now_ms);
    dk_take_t why;
    const int32_t moved = dk_track_feed(&c->track, &ev, now_ms, &why);
    if (why == DK_TAKE_REPEAT) {
        return;
    }
    ++c->events;
    c->in.detents += moved;
    switch (ev.gesture) {
    case DK_G_PRESS: ++c->in.presses; break;
    case DK_G_HOLD:  ++c->in.holds;   break;
    case DK_G_TAP:   ++c->taps;       break;
    default:                          break;
    }
    /* Only the first EVENT that echoes a STATE times it: a later one
     * carries the same echo and would time the STATE's age. */
    if (c->stream.state_sent && ev.echo != c->echo_seen) {
        c->rtt_ms = (uint16_t)((uint16_t)now_ms - ev.echo);
        c->rtt_valid = true;
    }
    c->echo_seen = ev.echo;
}

void dial_ctl_event(dial_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms)
{
    if (c == NULL || e == NULL) {
        return;
    }
    const bool stream = bleu_event(&c->b, e, now_ms);
    take_scan(c);
    follow_link(c, now_ms);
    if (stream && c->cfg.en) {
        take_event(c, e->data, e->len, now_ms);
    }
}

void dial_ctl_model(dial_ctl_t *c, const dial_model_t *m)
{
    if (c != NULL && m != NULL) {
        c->model = *m;
    }
}

bool dial_ctl_step(dial_ctl_t *c, uint32_t now_ms, bool armed,
                   bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    follow_link(c, now_ms);
    (void)dk_link_step(&c->link, now_ms);
    if (dk_link_loss_error(&c->link, armed)) {
        ++c->errors;
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
    if (!c->info_ok || !bleu_can_send(&c->b)) {
        return false;
    }
    uint8_t pkt[DK_PKT_MAX];
    size_t n;
    const bool again = c->b.wr == BLEU_WR_FAILED && c->pend_len != 0u
                       && c->pend_tries < DIAL_SEND_TRIES;
    if (again) {
        /* The host refused the last one (a full buffer): it goes again
         * before the stream moves on, so no change is lost. */
        n = c->pend_len;
        memcpy(pkt, c->pend, n);
    } else {
        n = dial_stream_next(&c->stream, &c->model, now_ms,
                             c->link.state == DK_LINK_DEGRADED, pkt);
    }
    if (n == 0u || !bleu_send(&c->b, pkt, n, now_ms)) {
        return false;
    }
    if (again) {
        ++c->pend_tries;
        ++c->resent;
    } else {
        memcpy(c->pend, pkt, n);
        c->pend_len = (uint8_t)n;
        c->pend_tries = 1u;
    }
    ++c->writes;
    return bleu_step(&c->b, now_ms, out);
}

void dial_ctl_take(dial_ctl_t *c, dial_input_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (c == NULL) {
        return;
    }
    const bool up = c->link.state == DK_LINK_UP
                    || c->link.state == DK_LINK_DEGRADED;
    if (c->cfg.en && up) {
        *out = c->in;
    } else if (c->cfg.en) {
        /* A HOLD only disarms: it goes through whatever the link. */
        out->holds = c->in.holds;
    }
    memset(&c->in, 0, sizeof(c->in));
}

dial_link_t dial_ctl_link(const dial_ctl_t *c)
{
    if (c == NULL || !c->cfg.en) {
        return DIAL_LINK_OFF;
    }
    switch (c->radio) {
    case IMU_RADIO_NO_LINK: return DIAL_LINK_C6_FAILED;
    case IMU_RADIO_NO_SYNC: return DIAL_LINK_NO_BT;
    case IMU_RADIO_READY:   break;
    case IMU_RADIO_OFF:
    case IMU_RADIO_STARTING:
    default:                return DIAL_LINK_STARTING;
    }
    if (c->b.scanning) {
        return DIAL_LINK_SCANNING;
    }
    if (!c->b.bound) {
        return DIAL_LINK_NONE;
    }
    if (c->b.st == BLEU_ST_NOT_PEER) {
        return DIAL_LINK_NOT_DIAL;
    }
    if (!bleu_link_up(&c->b)) {
        return c->link.state == DK_LINK_LOST ? DIAL_LINK_LOST
                                             : DIAL_LINK_CONNECTING;
    }
    if (!c->info_read) {
        return DIAL_LINK_WAITING;
    }
    if (!c->info_ok) {
        return DIAL_LINK_PROTO;
    }
    switch (c->link.state) {
    case DK_LINK_UP:       return DIAL_LINK_UP;
    case DK_LINK_DEGRADED: return DIAL_LINK_DEGRADED;
    case DK_LINK_LOST:     return DIAL_LINK_LOST;
    case DK_LINK_CONNECTING:
    case DK_LINK_DOWN:
    default:               return DIAL_LINK_WAITING;
    }
}

void dial_ctl_view(const dial_ctl_t *c, dial_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (c == NULL) {
        return;
    }
    out->link = (uint8_t)dial_ctl_link(c);
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
}
