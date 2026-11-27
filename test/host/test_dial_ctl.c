/*
 * The panel's M5Stack Dial controller (dial_ctl.h) against the scripted
 * host stack and Dial of dial_rig.h.
 *
 * Under test: the link up with INFO read and the whole model streamed; a
 * loss while ARMED at 999ms (none) and at 1,000ms (an error), and at the
 * disconnect; a loss while SAFE (none); a connect without an EVENT (no
 * loss however long); a scan refused while ARMED and started while SAFE;
 * the scan listing the Dial's service only, as dk_adv_has_service() does;
 * a reconnect without a scan whose first EVENT moves nothing; the buttons'
 * held states handing on nothing, PRESS and HOLD counted; EVENTs dropped
 * before INFO and from another protocol; Dial OFF ending the link with no
 * loss and handing on nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bleuart.h"
#include "dial_rig.h"

/* A link comes up: connect, the service, INFO read; the stream then writes
 * SCREEN first and STATE, and every packet decodes. */
TEST_CASE(the_link_comes_up_and_the_model_streams)
{
    drig_t r;
    drig_up(&r);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_WAITING);
    CHECK_EQ(r.c.link.state, DK_LINK_CONNECTING);
    CHECK_EQ(r.npkt, 0u);               /* no model yet */
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_OTHER;
    (void)dk_text_copy(m.title, sizeof m.title, "SETUP");
    m.thr_tenths = 10u;
    m.srv_tenths = 10u;
    dial_ctl_model(&r.c, &m);
    drig_run(&r, 10u);
    if (r.npkt < 2u) {
        T_FAIL("%u packets", r.npkt);
        return;
    }
    CHECK_EQ(r.pkt[0][0], DK_K_SCREEN);
    CHECK_EQ(r.pkt[1][0], DK_K_STATE);
    for (unsigned i = 0; i < r.npkt; ++i) {
        dk_panel_pkt_t p;
        CHECK(dk_panel_decode(r.pkt[i], r.len[i], &p));
    }
    drig_event(&r, 0, 0u, DK_G_NONE);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_UP);
}

/* No EVENT for 999ms while ARMED is DEGRADED, no error; 1,000ms is LOST and
 * one error, taken once. */
TEST_CASE(a_loss_while_armed_is_an_error_at_1000ms_not_999ms)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_event(&r, 0, 0u, DK_G_NONE);
    const uint32_t t0 = r.now;
    drig_run(&r, 999u);
    CHECK_EQ(r.now - t0, 999u);
    CHECK_EQ(r.c.errors, 0u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_DEGRADED);
    drig_run(&r, 1u);
    CHECK_EQ(r.c.errors, 1u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_LOST);
    drig_run(&r, 2000u);
    CHECK_EQ(r.c.errors, 1u);
    /* At 500ms DEGRADED, at 499ms still UP. */
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_run(&r, 499u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_UP);
    drig_run(&r, 1u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_DEGRADED);
}

/* The same silence on a SAFE bench is no error, and an ARM after it finds
 * none kept. */
TEST_CASE(a_loss_while_safe_is_no_error)
{
    drig_t r;
    drig_up(&r);
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_run(&r, 1500u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_LOST);
    r.armed = true;
    drig_run(&r, 100u);
    CHECK_EQ(r.c.errors, 0u);
}

/* The disconnect while ARMED is an error at once. */
TEST_CASE(a_disconnect_while_armed_is_an_error_at_once)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_run(&r, 10u);
    drig_drop(&r);
    drig_step(&r);
    CHECK_EQ(r.c.errors, 1u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_LOST);
}

/* SEARCH's UNBIND while ARMED with the Dial UP: the link ends at once, as
 * a disconnect does, and the loss is an error at the next step; nothing is
 * bound, the link reads NONE, no connect follows, and a second unbind
 * changes nothing.  The same while SAFE is no error. */
TEST_CASE(an_unbind_ends_the_link_as_a_disconnect_does)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_run(&r, 10u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_UP);
    const uint32_t seq = r.c.bind_seq;
    CHECK(dial_ctl_unbind(&r.c, r.now));
    CHECK_EQ(r.c.bind_seq, seq + 1u);
    CHECK_EQ(r.c.cfg.bind_seq, r.c.bind_seq);
    CHECK_STR_EQ(r.c.cfg.addr, "");
    CHECK_STR_EQ(r.c.cfg.name, "");
    CHECK(!r.c.b.bound);
    drig_step(&r);
    CHECK_EQ(r.c.errors, 1u);
    CHECK(!r.linked);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_NONE);
    dial_view_t v;
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_STR_EQ(v.bound_addr, "");
    const unsigned connects = r.connects;
    drig_run(&r, 3u * BLEU_RETRY_MS);
    CHECK_EQ(r.connects, connects);
    CHECK_EQ(r.c.errors, 1u);
    CHECK(!dial_ctl_unbind(&r.c, r.now));
    CHECK_EQ(r.c.bind_seq, seq + 1u);
    /* SAFE: the link ends the same way and counts no error. */
    drig_up(&r);
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_run(&r, 10u);
    CHECK(dial_ctl_unbind(&r.c, r.now));
    drig_run(&r, 10u);
    CHECK_EQ(r.c.errors, 0u);
    CHECK(!r.linked);
}

/* A connect that never brings an EVENT starts no loss clock: no error
 * however long, and none at its disconnect. */
TEST_CASE(a_connect_without_an_event_is_no_loss)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_run(&r, 5000u);
    CHECK_EQ(r.c.errors, 0u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_WAITING);
    drig_drop(&r);
    drig_run(&r, 10u);
    CHECK_EQ(r.c.errors, 0u);
}

/* A scan while ARMED is refused and asks the host stack for nothing; on a
 * SAFE bench it starts. */
TEST_CASE(a_scan_while_armed_is_refused)
{
    drig_t r;
    drig_up(&r);
    CHECK_EQ(dial_ctl_scan(&r.c, true, r.now), DIAL_SCAN_ARMED);
    drig_run(&r, 20u);
    CHECK_EQ(r.scans, 0u);
    CHECK(!r.c.b.scanning);
    CHECK_EQ(r.c.refused, 1u);
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, 1u);
    CHECK_EQ(r.scans, 1u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_SCANNING);
    /* A second scan while one runs, and any scan with the Dial OFF. */
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_NOT_READY);
}

/* One advertising report of address @p last into the controller. */
static void adv(drig_t *r, uint8_t last, const uint8_t *ad, uint8_t len)
{
    bleu_evt_t e = drig_ev(BLEU_EV_ADV, 0u);
    e.addr.val[0] = last;
    e.addr.type = BLEU_ADDR_RANDOM;
    e.rssi = (int8_t)(-50 - last);
    memcpy(e.data, ad, len);
    e.len = len;
    dial_ctl_event(&r->c, &e, r->now);
}

/* The scan lists a device whose advertising names the Dial's service, not
 * the IMU's ISSC one; a tap on it binds it. */
TEST_CASE(the_scan_lists_dials_only_and_a_bind_takes_one)
{
    drig_t r;
    drig_init(&r);
    dial_cfg_t cfg = r.c.cfg;
    cfg.addr[0] = '\0';
    dial_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_NONE);
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, 1u);
    uint8_t dial[21] = { 2u, 0x01u, 0x06u, 17u, 0x07u, DK_UUID_LE(0) };
    uint8_t issc[21] = { 2u, 0x01u, 0x06u, 17u, 0x07u };
    memcpy(&issc[5], bleu_uuid_svc, 16u);
    adv(&r, 1u, issc, sizeof issc);
    adv(&r, 2u, dial, sizeof dial);
    const uint8_t name[] = { 10u, 0x09u, 'D', 'I', 'A', 'L', '-', '3', 'A',
                             '1', '0' };
    adv(&r, 2u, name, sizeof name);
    bleu_evt_t end = drig_ev(BLEU_EV_SCAN_END, r.c.b.tok_scan);
    dial_ctl_event(&r.c, &end, r.now);
    CHECK_EQ(r.c.found_n, 1u);
    CHECK_STR_EQ(r.c.found[0].name, "DIAL-3A10");
    CHECK_STR_EQ(r.c.found[0].addr, "00:00:00:00:00:02 R");
    CHECK_EQ(r.c.hidden, 1u);
    dial_view_t v;
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK_EQ(v.found_n, 1u);
    CHECK(!dial_ctl_bind(&r.c, 1u, r.now));
    CHECK(dial_ctl_bind(&r.c, 0u, r.now));
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_STR_EQ(r.c.cfg.addr, "00:00:00:00:00:02 R");
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_STR_EQ(v.bound_name, "DIAL-3A10");
}

/** One xorshift32 step: a fixed, portable noise source. */
static uint32_t noise(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* The profile's advertising match is dk_adv_has_service(), report for
 * report, over random advertising data with the UUID planted at random. */
TEST_CASE(the_scan_filter_is_the_protocols_own)
{
    uint32_t seed = 7u;
    static const uint8_t svc[16] = { DK_UUID_LE(DK_UUID_SVC) };
    for (int k = 0; k < 20000; ++k) {
        uint8_t ad[31] = { 0 };
        const size_t len = (size_t)(noise(&seed) % 32u);
        for (size_t i = 0; i < len; ++i) {
            ad[i] = (uint8_t)noise(&seed);
        }
        if (len >= 18u && (noise(&seed) & 1u) != 0u) {
            const size_t at = (size_t)(noise(&seed) % (len - 17u));
            ad[at] = 17u;
            ad[at + 1u] = (uint8_t)(0x06u + (noise(&seed) & 1u));
            memcpy(&ad[at + 2u], svc, 16u);
        }
        bleu_adv_t a;
        const bool mine = bleu_adv_parse_for(&dial_profile, ad, len, &a);
        if (mine != dk_adv_has_service(ad, len)) {
            T_FAIL("report %d of %u bytes: bleuart %d, dialknob %d", k,
                   (unsigned)len, mine, !mine);
            break;
        }
    }
    CHECK(bleu_profile_valid(&dial_profile));
}

/* After a drop the bound Dial is connected again without a scan, and the
 * first EVENT of the new link moves nothing, wherever its counter is. */
TEST_CASE(a_reconnect_moves_nothing)
{
    drig_t r;
    drig_up(&r);
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_event(&r, 3, 0u, DK_G_NONE);
    CHECK_EQ(drig_take(&r).detents, 3);
    drig_drop(&r);
    drig_run(&r, 2000u);
    CHECK(r.c.linked);
    CHECK_EQ(r.connects, 2u);
    CHECK_EQ(r.scans, 0u);
    drig_event(&r, 200, 0u, DK_G_NONE);    /* turned while away */
    CHECK_EQ(drig_take(&r).detents, 0);
    drig_event(&r, 201, 0u, DK_G_NONE);
    CHECK_EQ(drig_take(&r).detents, 1);
    CHECK_EQ(r.c.track.refs, 2u);
}

/* The screen button and a finger held for 10s hand on nothing; PRESS and
 * HOLD are each one gesture; a TAP on the glass is not handed on. */
TEST_CASE(held_buttons_hand_on_nothing_and_gestures_count_once)
{
    drig_t r;
    drig_up(&r);
    for (int k = 0; k < 50; ++k) {
        drig_event(&r, 0, DK_BTN_SCREEN | DK_BTN_TOUCH, DK_G_NONE);
        drig_run(&r, 200u);
    }
    dial_input_t in = drig_take(&r);
    CHECK_EQ(in.detents, 0);
    CHECK_EQ(in.presses, 0u);
    CHECK_EQ(in.holds, 0u);
    drig_event(&r, 0, DK_BTN_SCREEN, DK_G_HOLD);
    drig_event(&r, 0, 0u, DK_G_PRESS);
    drig_event(&r, 0, 0u, DK_G_TAP);
    in = drig_take(&r);
    CHECK_EQ(in.presses, 1u);
    CHECK_EQ(in.holds, 1u);
    CHECK_EQ(r.c.taps, 1u);
    /* A repeated seq hands its gesture on once. */
    r.seq = (uint8_t)(r.seq - 1u);
    drig_event(&r, 0, 0u, DK_G_TAP);
    r.seq = (uint8_t)(r.seq - 1u);
    drig_event(&r, 0, 0u, DK_G_PRESS);
    in = drig_take(&r);
    CHECK_EQ(in.presses, 0u);
}

/* A Dial whose INFO names another protocol moves nothing; nor does an
 * EVENT before INFO is read. */
TEST_CASE(another_protocol_moves_nothing)
{
    drig_t r;
    drig_init(&r);
    r.proto = DK_PROTOCOL + 1u;
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_PROTO);
    drig_event(&r, 0, 0u, DK_G_PRESS);
    drig_event(&r, 5, 0u, DK_G_HOLD);
    const dial_input_t in = drig_take(&r);
    CHECK_EQ(in.detents, 0);
    CHECK_EQ(in.presses + in.holds, 0u);
    CHECK_EQ(r.c.unread, 2u);
    dial_view_t v;
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.proto, DK_PROTOCOL + 1u);
}

/* Dial OFF ends the link: no loss while ARMED, nothing handed on after. */
TEST_CASE(dial_off_ends_the_link_without_a_loss)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_event(&r, 4, 0u, DK_G_NONE);
    dial_cfg_t cfg = r.c.cfg;
    cfg.en = false;
    dial_ctl_config(&r.c, &cfg, r.now);
    drig_run(&r, 3000u);
    CHECK_EQ(r.c.errors, 0u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_OFF);
    const dial_input_t in = drig_take(&r);
    CHECK_EQ(in.detents, 0);
    CHECK(!r.linked);
    CHECK_EQ(dial_step_tenths(0), 5u);
    CHECK_EQ(dial_step_tenths(3), 50u);
    CHECK_EQ(dial_step_tenths(9), 10u);
}

/* The radio's own failure while ARMED ends the link: a loss, an error. */
TEST_CASE(a_c6_that_fails_while_armed_is_an_error)
{
    drig_t r;
    drig_up(&r);
    r.armed = true;
    drig_event(&r, 0, 0u, DK_G_NONE);
    dial_ctl_radio(&r.c, IMU_RADIO_NO_LINK, r.now);
    drig_step(&r);
    CHECK_EQ(r.c.errors, 1u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_C6_FAILED);
}

TEST_CASE(a_scan_ends_when_the_bench_arms)
{
    drig_t r;
    drig_up(&r);
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, 10u);
    CHECK(r.c.b.scanning);
    /* ARMED 4s later, inside the scan's 10s: it stops in that step. */
    r.armed = true;
    drig_run(&r, 1u);
    CHECK(!r.c.b.scanning);
    CHECK_EQ(r.stops, 1u);
    drig_run(&r, 4000u);
    CHECK_EQ(r.stops, 1u);
    dial_view_t v;
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK(v.scan_cut);
    CHECK(v.link != DIAL_LINK_SCANNING);
    /* A new scan while SAFE runs whole and is no cut one. */
    r.armed = false;
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, BLEU_SCAN_MS + 10u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK(!v.scan_cut);
}

TEST_CASE(a_write_the_host_refuses_goes_again_and_the_stream_goes_on)
{
    drig_t r;
    drig_up(&r);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_OTHER;
    m.thr_tenths = m.srv_tenths = 10u;
    (void)dk_text_copy(m.title, sizeof m.title, "SETUP");
    /* The first packet, SCREEN, refused twice: the third send takes it,
     * in the same few ms, and nothing waits BLEU_WRITE_MS. */
    r.refuse_writes = 2u;
    dial_ctl_model(&r.c, &m);
    drig_run(&r, 5u);
    CHECK(r.npkt >= 2u);
    CHECK_EQ(r.pkt[0][0], DK_K_SCREEN);
    CHECK_EQ(r.pkt[1][0], DK_K_STATE);
    CHECK_EQ(r.c.resent, 2u);
    /* Refused DIAL_SEND_TRIES times: dropped, and the stream goes on. */
    const unsigned had = r.npkt;
    (void)dk_text_copy(m.title, sizeof m.title, "LOGS");
    dial_ctl_model(&r.c, &m);
    r.refuse_writes = DIAL_SEND_TRIES;
    drig_run(&r, 300u);
    CHECK(r.npkt > had);
    CHECK_EQ(r.pkt[had][0], DK_K_STATE);   /* the SCREEN was dropped */
    CHECK_EQ(r.c.resent, 2u + DIAL_SEND_TRIES - 1u);
    CHECK_EQ(r.c.errors, 0u);
}

/** @brief The panel ms of the newest STATE the rig kept, 0 for none. */
static uint32_t newest_state(const drig_t *r, unsigned *at)
{
    uint32_t ms = 0u;
    for (unsigned i = 0; i < r->npkt; ++i) {
        dk_state_t s;
        if (dk_state_decode(r->pkt[i], r->len[i], &s)) {
            ms = s.panel_ms;
            if (at != NULL) {
                *at = i;
            }
        }
    }
    return ms;
}

TEST_CASE(state_goes_every_1000ms_under_live_and_times_the_link)
{
    drig_t r;
    drig_up(&r);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_MOTOR;
    m.has_motor = true;
    (void)dk_text_copy(m.title, sizeof m.title, "MOTOR & ESC");
    m.thr_tenths = m.srv_tenths = 10u;
    dial_ctl_model(&r.c, &m);
    drig_run(&r, 20u);
    unsigned at = 0u;
    uint32_t last = newest_state(&r, &at);
    CHECK(last != 0u);
    /* LIVE every 50ms; STATE still at least every 1000ms. */
    unsigned states = 0u;
    for (uint32_t t = 0; t < 5000u; ++t) {
        drig_run(&r, 1u);
        const uint32_t ms = newest_state(&r, NULL);
        if (ms != last) {
            if (ms - last > DIAL_STATE_MS) {
                T_FAIL("STATE %ums after the last", (unsigned)(ms - last));
            }
            last = ms;
            ++states;
        }
        if (r.npkt > DRIG_PKTS - 8u) {
            r.npkt = 0u;
        }
    }
    CHECK(states >= 5u);
    /* The Dial echoes a new STATE 20ms after it went: rtt 20ms. */
    r.npkt = 0u;
    for (unsigned k = 0; k < DIAL_STATE_MS + 10u
                         && newest_state(&r, NULL) == 0u; ++k) {
        drig_run(&r, 1u);
    }
    last = newest_state(&r, NULL);
    CHECK(last != 0u);
    drig_run(&r, 20u);
    dk_evt_t ev;
    memset(&ev, 0, sizeof ev);
    ev.seq = r.seq++;
    ev.echo = (uint16_t)last;
    bleu_evt_t e = drig_ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = DRIG_EVENT_VAL;
    e.len = (uint8_t)dk_evt_encode(&ev, e.data);
    dial_ctl_event(&r.c, &e, r.now);
    CHECK(r.c.rtt_valid);
    CHECK_EQ(r.c.rtt_ms, 20u);
    /* A keepalive 900ms later with the same echo times nothing. */
    drig_run(&r, 900u);
    ev.seq = r.seq++;
    e.len = (uint8_t)dk_evt_encode(&ev, e.data);
    dial_ctl_event(&r.c, &e, r.now);
    CHECK_EQ(r.c.rtt_ms, 20u);
}

TEST_CASE(an_info_that_fails_ends_the_link_and_it_comes_back)
{
    drig_t r;
    drig_init(&r);
    r.fail_info = 1u;
    r.armed = true;
    drig_run(&r, 50u);
    /* The read failed: no silent link, a GATT failure instead. */
    CHECK_EQ(r.c.info_failed, 1u);
    CHECK(!r.c.linked);
    CHECK_EQ(r.c.b.err, BLEU_ERR_GATT);
    CHECK_EQ(r.connects, 1u);
    CHECK_EQ(r.c.errors, 0u);       /* no EVENT yet: no loss */
    /* Not before the backoff; then the link and INFO come back. */
    drig_run(&r, BLEU_RETRY_MS - 60u);
    CHECK_EQ(r.connects, 1u);
    drig_run(&r, 200u);
    CHECK_EQ(r.connects, 2u);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(r.c.errors, 0u);
}

TEST_CASE(input_is_taken_only_while_the_link_is_up_or_degraded)
{
    drig_t r;
    drig_up(&r);
    drig_event(&r, 0, 0u, DK_G_NONE);
    drig_event(&r, 3, 0u, DK_G_NONE);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_UP);
    dial_input_t in = drig_take(&r);
    CHECK_EQ(in.detents, 3);
    /* Detents counted, then the link LOST before the take: none handed
     * on, and none kept for later. */
    drig_event(&r, 5, 0u, DK_G_PRESS);
    drig_event(&r, 5, 0u, DK_G_HOLD);
    drig_run(&r, DK_LOST_MS + 5u);
    CHECK_EQ(dial_ctl_link(&r.c), DIAL_LINK_LOST);
    in = drig_take(&r);
    CHECK_EQ(in.detents, 0);
    CHECK_EQ(in.presses, 0u);
    CHECK_EQ(in.holds, 1u);         /* a HOLD only disarms: it goes */
    drig_event(&r, 6, 0u, DK_G_NONE);   /* a reference after the gap */
    in = drig_take(&r);
    CHECK_EQ(in.detents, 0);
    CHECK_EQ(in.presses, 0u);
}

TEST_CASE(the_panel_streams_before_an_event_and_never_pauses_500ms)
{
    for (int face = 0; face < 2; ++face) {
        drig_t r;
        drig_init(&r);
        r.armed = face == 1;
        dial_model_t m;
        memset(&m, 0, sizeof m);
        m.face = face ? DK_FACE_MOTOR : DK_FACE_OTHER;
        m.has_motor = face == 1;
        m.armed = r.armed;
        (void)dk_text_copy(m.title, sizeof m.title,
                           face ? "MOTOR & ESC" : "SETUP");
        m.thr_tenths = m.srv_tenths = 10u;
        dial_ctl_model(&r.c, &m);
        uint32_t last = r.now, gap = 0u;
        unsigned seen = 0u;
        for (uint32_t k = 0; k < 5000u; ++k) {
            drig_run(&r, 1u);
            if (r.npkt != seen) {
                if (seen != 0u && r.now - last > gap) {
                    gap = r.now - last;
                }
                last = r.now;
                seen = r.npkt;
            }
            if (r.npkt > DRIG_PKTS - 8u) {
                r.npkt = 0u;
                seen = 0u;
            }
        }
        CHECK(seen > 0u);
        CHECK(gap < DK_DIAL_LOST_MS);
        CHECK_EQ(r.c.errors, 0u);
    }
}

/* STATUS: read after INFO, then every 5,000ms while the link stands, into
 * the view with INFO's board; an M5Stack Dial of an older release, which
 * sends USB and an unknown cell, reads as before; a K518's LOW and EMPTY
 * come through; a failed read keeps the last STATUS; the link's end
 * drops it. */
TEST_CASE(status_is_read_every_5000ms_and_shows_the_board_and_the_cell)
{
    drig_t r;
    dial_view_t v;
    drig_up(&r);
    CHECK_EQ(r.st_reads, 1u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.hw, DK_HW_DIAL11);
    CHECK(v.have_status);
    CHECK_EQ(v.batt_mv, 0u);
    CHECK_EQ(v.st_flags, DK_ST_USB | DK_ST_BATT_UNKNOWN);
    const uint32_t t0 = r.c.status_ms;
    drig_run(&r, t0 + DIAL_STATUS_READ_MS - 1u - r.now);
    CHECK_EQ(r.st_reads, 1u);
    r.st.flags = DK_ST_BATT_LOW;
    r.st.batt_mv = 3450u;
    drig_run(&r, 2u);
    CHECK_EQ(r.st_reads, 2u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.batt_mv, 3450u);
    CHECK_EQ(v.st_flags, DK_ST_BATT_LOW);
    r.fail_status = 1u;
    drig_run(&r, DIAL_STATUS_READ_MS);
    CHECK_EQ(r.st_reads, 3u);
    CHECK_EQ(r.c.status_bad, 1u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.batt_mv, 3450u);             /* the last one stays */
    CHECK(r.c.linked);                      /* no reconnect for it */
    drig_drop(&r);
    drig_step(&r);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(!v.have_status);
    CHECK_EQ(v.hw, 0u);
}

TEST_CASE(a_k518_names_its_board_and_its_empty_cell)
{
    drig_t r;
    dial_view_t v;
    drig_init(&r);
    r.hw = DK_HW_K518;
    r.st.flags = DK_ST_BATT_LOW | DK_ST_BATT_EMPTY;
    r.st.batt_mv = 3330u;
    drig_run(&r, 50u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.hw, DK_HW_K518);
    CHECK_EQ(v.batt_mv, 3330u);
    CHECK_EQ(v.st_flags, DK_ST_BATT_LOW | DK_ST_BATT_EMPTY);
}

/* The board belongs to the Dial it came from: a bind to another Dial and
 * an unbind drop it, so the update screen never picks the image of the
 * Dial bound before (devimg_dial_image()). */
TEST_CASE(a_bind_or_an_unbind_forgets_the_last_dials_board)
{
    drig_t r;
    drig_init(&r);
    r.hw = DK_HW_K518;
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    CHECK_EQ(dial_ctl_board(&r.c), DK_HW_K518);
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, 1u);
    uint8_t dial[21] = { 2u, 0x01u, 0x06u, 17u, 0x07u, DK_UUID_LE(0) };
    adv(&r, 2u, dial, sizeof dial);
    bleu_evt_t end = drig_ev(BLEU_EV_SCAN_END, r.c.b.tok_scan);
    dial_ctl_event(&r.c, &end, r.now);
    CHECK_EQ(r.c.found_n, 1u);
    CHECK(dial_ctl_bind(&r.c, 0u, r.now));
    CHECK_EQ(dial_ctl_board(&r.c), 0u);
    r.hw = DK_HW_DIAL11;
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    CHECK_EQ(dial_ctl_board(&r.c), DK_HW_DIAL11);
    CHECK(dial_ctl_unbind(&r.c, r.now));
    CHECK_EQ(dial_ctl_board(&r.c), 0u);
}

/* Switched OFF on the panel, the Dial's cell is no longer shown: the
 * STATUS read before the switch goes with the link. */
TEST_CASE(dial_off_drops_the_last_status)
{
    drig_t r;
    dial_view_t v;
    drig_up(&r);
    r.st.batt_mv = 3450u;
    r.st.flags = DK_ST_BATT_LOW;
    drig_run(&r, DIAL_STATUS_READ_MS + 1u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(v.have_status);
    CHECK_EQ(v.batt_mv, 3450u);
    dial_cfg_t cfg = r.c.cfg;
    cfg.en = false;
    dial_ctl_config(&r.c, &cfg, r.now);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(!v.have_status);
    CHECK_EQ(v.batt_mv, 0u);
    CHECK_EQ(v.st_flags, 0u);
    drig_run(&r, DIAL_STATUS_READ_MS + 1u);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(!v.have_status);
}

/* A STATUS answer and the link's end that come in one drain, before the
 * next step: the answer belongs to the link that ended, and no cell is
 * shown. */
TEST_CASE(a_status_answer_with_the_links_end_is_dropped)
{
    drig_t r;
    dial_view_t v;
    drig_up(&r);
    r.st.batt_mv = 3450u;
    r.st.flags = DK_ST_BATT_LOW;
    r.hold_status = true;
    drig_run(&r, DIAL_STATUS_READ_MS + 1u);
    CHECK(r.have_held);
    dial_ctl_event(&r.c, &r.held, r.now);
    drig_drop(&r);
    drig_step(&r);
    CHECK(!r.c.linked);
    dial_ctl_view(&r.c, r.now, &v);
    CHECK(!v.have_status);
    CHECK_EQ(v.batt_mv, 0u);
}

/* The Dial bound and linked, picked again from the scan's list: the same
 * address, no new INFO, and its board stays. */
TEST_CASE(binding_the_bound_dial_again_keeps_its_board)
{
    drig_t r;
    drig_init(&r);
    r.hw = DK_HW_K518;
    dial_cfg_t cfg = r.c.cfg;
    cfg.addr[0] = '\0';
    dial_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(dial_ctl_scan(&r.c, false, r.now), DIAL_SCAN_STARTED);
    drig_run(&r, 1u);
    uint8_t dial[21] = { 2u, 0x01u, 0x06u, 17u, 0x07u, DK_UUID_LE(0) };
    adv(&r, 2u, dial, sizeof dial);
    bleu_evt_t end = drig_ev(BLEU_EV_SCAN_END, r.c.b.tok_scan);
    dial_ctl_event(&r.c, &end, r.now);
    CHECK(dial_ctl_bind(&r.c, 0u, r.now));
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    CHECK_EQ(dial_ctl_board(&r.c), DK_HW_K518);
    CHECK(dial_ctl_bind(&r.c, 0u, r.now));
    drig_run(&r, 50u);
    CHECK(r.c.linked);
    CHECK_EQ(dial_ctl_board(&r.c), DK_HW_K518);
}

int main(void)
{
    RUN(the_link_comes_up_and_the_model_streams);
    RUN(a_loss_while_armed_is_an_error_at_1000ms_not_999ms);
    RUN(a_loss_while_safe_is_no_error);
    RUN(a_disconnect_while_armed_is_an_error_at_once);
    RUN(a_connect_without_an_event_is_no_loss);
    RUN(an_unbind_ends_the_link_as_a_disconnect_does);
    RUN(a_scan_while_armed_is_refused);
    RUN(the_scan_lists_dials_only_and_a_bind_takes_one);
    RUN(the_scan_filter_is_the_protocols_own);
    RUN(a_reconnect_moves_nothing);
    RUN(held_buttons_hand_on_nothing_and_gestures_count_once);
    RUN(another_protocol_moves_nothing);
    RUN(dial_off_ends_the_link_without_a_loss);
    RUN(a_c6_that_fails_while_armed_is_an_error);
    RUN(a_scan_ends_when_the_bench_arms);
    RUN(a_write_the_host_refuses_goes_again_and_the_stream_goes_on);
    RUN(state_goes_every_1000ms_under_live_and_times_the_link);
    RUN(an_info_that_fails_ends_the_link_and_it_comes_back);
    RUN(input_is_taken_only_while_the_link_is_up_or_degraded);
    RUN(the_panel_streams_before_an_event_and_never_pauses_500ms);
    RUN(status_is_read_every_5000ms_and_shows_the_board_and_the_cell);
    RUN(a_k518_names_its_board_and_its_empty_cell);
    RUN(a_bind_or_an_unbind_forgets_the_last_dials_board);
    RUN(dial_off_drops_the_last_status);
    RUN(a_status_answer_with_the_links_end_is_dropped);
    RUN(binding_the_bound_dial_again_keeps_its_board);
    return test_summary("dial_ctl");
}
