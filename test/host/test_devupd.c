/**
 * @file test_devupd.c
 * @brief The device update's engine (devupd_t) against a simulated device,
 *        the device side of protocols/bleota over a modelled flash: a
 *        whole image in order, a lossless link with 0ms, 20ms and 40ms of
 *        latency sent once with no rewind, a lost chunk, a lost last chunk
 *        found by POLL, a credit stall ended after three polls, each
 *        refusal code, a link lost during the install left unknown and
 *        settled by the device's version, the start's gate,
 *        a wrong SHA-256, no confirmation, a disconnect mid-transfer and a
 *        retry from the first byte, the link and answer timeouts, a failed
 *        control write, the chunk size at each MTU, devupd_busy() over every
 *        phase (the bench refuses ARM while it holds) and UPDATES' row;
 *        a device that answers without moving on, ended at 30s of no
 *        progress and not at 29.9s; an abort in every phase, which waits
 *        through the install from END on; and the settle of an unknown
 *        install tied to its own device, reported before a second
 *        device's start.
 *
 * SPDX-License-Identifier: MIT
 */
#include "devupd_rig.h"

TEST_CASE(a_whole_image_goes_in_order_and_installs)
{
    static sim_t s;
    sim_init(&s);
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK(s.restarted);
    CHECK_EQ(s.flash_n, IMG);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
    CHECK_EQ(s.e.rewinds, 0u);
    CHECK_EQ(s.e.polls_sent, 0u);
    CHECK(s.e.chunks >= (IMG + BLEOTA_CHUNK_MAX - 1u) / BLEOTA_CHUNK_MAX);
    CHECK_EQ(devupd_percent(&s.e), 100u);
    CHECK_EQ(s.e.err, DEVUPD_E_NONE);
    /* Never more than the window ahead of the device's last write. */
    CHECK_EQ(s.d.over, 0u);
}

TEST_CASE(a_lost_chunk_is_sent_again_from_the_devices_offset)
{
    static sim_t s;
    sim_init(&s);
    s.drop_off = 20u * BLEOTA_CHUNK_MAX;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK(s.e.rewinds >= 1u);
    CHECK(s.d.gaps >= 1u);
    CHECK_EQ(s.flash_n, IMG);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
}

TEST_CASE(a_lost_last_chunk_is_found_by_poll)
{
    static sim_t s;
    sim_init(&s);
    s.drop_tail = true;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK_EQ(s.e.polls_sent, 1u);
    CHECK(s.e.rewinds >= 1u);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
}

TEST_CASE(a_credit_stall_polls_three_times_then_ends_it)
{
    static sim_t s;
    sim_init(&s);
    s.mute = true;
    s.mute_from = 10000u;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_TIMEOUT);
    CHECK_EQ(s.e.polls_sent, DEVUPD_POLLS);
    CHECK(!s.restarted);
    CHECK(!devupd_busy(&s.e));
}

TEST_CASE(each_refusal_ends_it_with_the_devices_code)
{
    static sim_t s;
    struct { uint8_t kind; uint32_t size; fw_version_t v; bool re;
             uint8_t code; } k[] = {
        { BLEOTA_KIND_NODE_POWER, IMG, { 0u, 25u, 0u }, false,
          BLEOTA_E_KIND },
        { BLEOTA_KIND_DIAL, SLOT + 1u, { 0u, 25u, 0u }, false,
          BLEOTA_E_SIZE },
        { BLEOTA_KIND_DIAL, IMG, { 0u, 24u, 0u }, false, BLEOTA_E_SAME },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        sim_init(&s);
        s.im.kind = k[i].kind;
        s.im.size = k[i].size;
        s.im.version = k[i].v;
        s.im.reinstall = k[i].re;
        sim_start(&s);
        CHECK_EQ(sim_run(&s, 10000u), DEVUPD_FAILED);
        CHECK_EQ(s.e.err, DEVUPD_E_DEVICE);
        CHECK_EQ(s.e.dev_code, k[i].code);
        CHECK(!s.d.open);
        CHECK_EQ(s.flash_n, 0u);
    }
    /* The running version with the reinstall flag goes through. */
    sim_init(&s);
    s.im.version = (fw_version_t){ 0u, 24u, 0u };
    s.im.reinstall = true;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    /* Refused on the device: a hold. */
    sim_init(&s);
    s.press = false;
    sim_start(&s);
    for (int i = 0; i < 20; ++i) {
        sim_step(&s);
    }
    CHECK_EQ(s.e.phase, DEVUPD_CONFIRM);
    CHECK_EQ(s.e.secs, 30);
    bleota_dev_key(&s.d, true, s.now);
    bleota_dev_key(&s.d, true, s.now + BLEOTA_HOLD_MS);
    CHECK_EQ(sim_run(&s, 1000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_REFUSED);
}

TEST_CASE(a_wrong_sha_is_refused_at_end_and_nothing_installs)
{
    static sim_t s;
    sim_init(&s);
    s.im.sha[0] ^= 0x80u;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_DEVICE);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_SHA);
    CHECK(!s.restarted);
    CHECK(!s.d.open);
    CHECK_EQ(s.flash_n, IMG);
}

TEST_CASE(no_confirmation_ends_it_with_the_devices_code)
{
    static sim_t s;
    sim_init(&s);
    s.press = false;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, BLEOTA_CONFIRM_MS + 1000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.dev_code, BLEOTA_E_NO_CONFIRM);
}

TEST_CASE(a_disconnect_mid_transfer_ends_it_and_a_retry_starts_at_byte_0)
{
    static sim_t s;
    sim_init(&s);
    s.cut = true;
    s.cut_at = IMG / 2u;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_LOST);
    CHECK(!s.restarted);
    device_work(&s);
    CHECK(!s.d.open);
    /* The retry: a new start, the link again, from the first byte. */
    s.first_seen = false;
    s.press_at = 0u;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 120000u), DEVUPD_DONE);
    CHECK(s.first_seen);
    CHECK_EQ(s.first_off, 0u);
    CHECK_EQ(s.flash_n, IMG);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
}

TEST_CASE(no_link_and_no_answer_end_it_at_their_limits)
{
    static sim_t s;
    sim_init(&s);
    CHECK(devupd_start(&s.e, &s.im, 1000u));
    devupd_out_t o;
    CHECK(!devupd_step(&s.e, 1000u + DEVUPD_LINK_MS - 1u, true, &o));
    CHECK_EQ(s.e.phase, DEVUPD_LINK);
    (void)devupd_step(&s.e, 1000u + DEVUPD_LINK_MS, true, &o);
    CHECK_EQ(s.e.phase, DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_LINK);

    /* BEGIN and its six HASH parts taken, then nothing from the
     * device. */
    devupd_init(&s.e);
    CHECK(devupd_start(&s.e, &s.im, 0u));
    devupd_link(&s.e, true, 247u, 0u);
    for (unsigned i = 0; i < 1u + BLEOTA_PARTS_MAX; ++i) {
        CHECK(devupd_step(&s.e, 10u, true, &o));
        CHECK_EQ(o.kind, DEVUPD_OUT_CTRL);
        devupd_written(&s.e, 0u, 10u);
    }
    CHECK_EQ(o.ctrl[0], BLEOTA_OP_HASH);
    CHECK(!devupd_step(&s.e, DEVUPD_ANSWER_MS - 1u, true, &o));
    CHECK_EQ(s.e.phase, DEVUPD_BEGIN);
    (void)devupd_step(&s.e, DEVUPD_ANSWER_MS, true, &o);
    CHECK_EQ(s.e.err, DEVUPD_E_TIMEOUT);

    /* A control write whose answer never comes. */
    devupd_init(&s.e);
    CHECK(devupd_start(&s.e, &s.im, 0u));
    devupd_link(&s.e, true, 247u, 0u);
    CHECK(devupd_step(&s.e, 5u, true, &o));
    CHECK(!devupd_step(&s.e, 5u + DEVUPD_ANSWER_MS - 1u, true, &o));
    CHECK_EQ(s.e.phase, DEVUPD_BEGIN);
    (void)devupd_step(&s.e, 5u + DEVUPD_ANSWER_MS, true, &o);
    CHECK_EQ(s.e.err, DEVUPD_E_WRITE);
}

TEST_CASE(a_refused_control_write_ends_it)
{
    static sim_t s;
    sim_init(&s);
    s.ctrl_fail = 2u;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 10000u), DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_WRITE);
}

TEST_CASE(the_chunk_follows_the_mtu_and_a_refused_chunk_goes_again)
{
    static sim_t s;
    sim_init(&s);
    s.mtu = BLEOTA_MTU_MIN;
    sim_start(&s);
    CHECK_EQ(s.e.chunk, 16);
    CHECK_EQ(sim_run(&s, 600000u), DEVUPD_DONE);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);

    devupd_t e;
    devupd_init(&e);
    devupd_image_t im = s.im;
    CHECK(devupd_start(&e, &im, 0u));
    devupd_link(&e, true, 100u, 0u);
    CHECK_EQ(e.chunk, 93);
    /* In SEND with a credit of 2,048 bytes: a chunk not taken is given
     * again; one that is not the last given is ignored. */
    e.phase = DEVUPD_SEND;
    e.q_n = 0u;
    e.limit = BLEOTA_WINDOW;
    e.t_credit = 0u;
    devupd_out_t a, b;
    CHECK(devupd_step(&e, 1u, true, &a));
    CHECK(devupd_step(&e, 1u, true, &b));
    CHECK_EQ(b.off, 93u);
    devupd_unsend(&e, &a);
    CHECK_EQ(e.sent, 186u);
    devupd_unsend(&e, &b);
    CHECK_EQ(e.sent, 93u);
    CHECK(devupd_step(&e, 1u, true, &b));
    CHECK_EQ(b.off, 93u);
}

TEST_CASE(busy_holds_from_the_start_to_the_restart_and_arm_is_refused)
{
    static sim_t s;
    sim_init(&s);
    CHECK(!devupd_busy(&s.e));
    CHECK(!devupd_busy(NULL));
    CHECK(devupd_start(&s.e, &s.im, s.now));
    CHECK(devupd_busy(&s.e));
    CHECK(!devupd_start(&s.e, &s.im, s.now));
    bool seen[DEVUPD_FAILED + 1] = { false };
    seen[s.e.phase] = true;
    link_up(&s);
    while (devupd_busy(&s.e)) {
        seen[s.e.phase] = true;
        sim_step(&s);
    }
    for (int p = DEVUPD_LINK; p <= DEVUPD_RESTART; ++p) {
        /* END and the device's DONE come in one step of the model. */
        CHECK(seen[p] || p == DEVUPD_END);
    }
    CHECK_EQ(s.last_op, BLEOTA_OP_END);
    CHECK_EQ(s.e.phase, DEVUPD_DONE);
    CHECK(!devupd_busy(&s.e));
    /* A device that answers DONE and keeps the link: done after 5,000ms. */
    devupd_t e;
    devupd_init(&e);
    e.phase = DEVUPD_RESTART;
    e.t = 0u;
    devupd_out_t o;
    (void)devupd_step(&e, DEVUPD_RESTART_MS - 1u, true, &o);
    CHECK(devupd_busy(&e));
    (void)devupd_step(&e, DEVUPD_RESTART_MS, true, &o);
    CHECK(!devupd_busy(&e));
    CHECK_EQ(e.phase, DEVUPD_DONE);
    /* An abort ends it at once; no image fails the start. */
    devupd_init(&e);
    CHECK(devupd_start(&e, &s.im, 0u));
    devupd_abort(&e, 1u);
    CHECK(!devupd_busy(&e));
    CHECK_EQ(e.err, DEVUPD_E_ABORTED);
    CHECK(devupd_start(&e, NULL, 2u));
    CHECK_EQ(e.err, DEVUPD_E_IMAGE);
    CHECK(!devupd_busy(&e));
}

TEST_CASE(the_row_offers_the_hold_only_when_nothing_stands_in_the_way)
{
    devupd_row_in_t in = { .have_image = true, .image_ok = true,
                           .linked = true, .known = true,
                           .running = { 0u, 24u, 0u },
                           .carried = { 0u, 25u, 0u } };
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_READY);
    in.running = in.carried;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_REINSTALL);
    in.running = (fw_version_t){ 1u, 0u, 0u };
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_READY);
    in.known = false;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_NOT_LINKED);
    in.known = true;
    in.linked = false;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_NOT_LINKED);
    in.other = true;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_BUSY);
    in.armed = true;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_ARMED);
    in.image_ok = false;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_BAD_IMAGE);
    /* Not checked yet: no hold, whatever the check will say. */
    in.image_due = true;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_CHECKING);
    in.image_ok = true;
    in.armed = false;
    in.other = false;
    in.linked = true;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_CHECKING);
    in.image_due = false;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_READY);
    in.image_ok = false;
    in.have_image = false;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_NO_IMAGE);
    in.busy = true;
    CHECK_EQ(devupd_row(&in), DEVUPD_ROW_UPDATING);
    CHECK_EQ(devupd_row(NULL), DEVUPD_ROW_NO_IMAGE);
}

/* A routine CREDIT's next lies behind the chunks in flight; only a marked
 * gap or a repeated CREDIT moves the sending back.  A lossless link sends
 * every byte once, whatever its latency. */
TEST_CASE(a_lossless_link_with_latency_sends_every_byte_once)
{
    static sim_t s;
    static const uint32_t k_lat_ms[] = { 0u, 20u, 40u };
    for (size_t k = 0; k < sizeof k_lat_ms / sizeof k_lat_ms[0]; ++k) {
        sim_init(&s);
        s.lat = k_lat_ms[k] / STEP;
        s.rate = 3u;
        s.slow = true;
        sim_start(&s);
        CHECK_EQ(sim_run(&s, 300000u), DEVUPD_DONE);
        CHECK_EQ(s.e.rewinds, 0u);
        CHECK_EQ(s.d.gaps, 0u);
        CHECK_EQ(s.d.dups, 0u);
        CHECK_EQ(s.sent, (uint64_t)IMG);
        CHECK(memcmp(s_flash, s_img, IMG) == 0);
    }
    /* With 40ms and a lost chunk: sent again once, from the device's
     * offset, and no more than the window twice over. */
    sim_init(&s);
    s.lat = 8u;
    s.rate = 3u;
    s.slow = true;
    s.drop_off = 30u * BLEOTA_CHUNK_MAX;
    sim_start(&s);
    CHECK_EQ(sim_run(&s, 300000u), DEVUPD_DONE);
    CHECK_EQ(s.e.rewinds, 1u);
    CHECK(s.sent <= (uint64_t)IMG + 2u * BLEOTA_WINDOW);
    CHECK(memcmp(s_flash, s_img, IMG) == 0);
}

/* After END the device may set its boot slot before the panel hears DONE:
 * a lost link or no DONE leaves the result unknown, and the device's
 * version once it is back settles it. */
TEST_CASE(a_link_lost_after_end_leaves_the_install_to_the_version)
{
    static sim_t s;
    sim_init(&s);
    sim_start(&s);
    /* Every byte credited: END goes out, and the device is installing
     * when the link drops. */
    devupd_out_t o;
    while (devupd_step(&s.e, s.now, true, &o)) {
        devupd_written(&s.e, 0u, s.now);
    }
    s.e.phase = DEVUPD_SEND;
    s.e.next = s.e.im.size;
    s.e.t_credit = s.now;
    (void)devupd_step(&s.e, s.now, true, &o);
    CHECK_EQ(o.kind, DEVUPD_OUT_CTRL);
    CHECK_EQ(o.ctrl[0], BLEOTA_OP_END);
    CHECK_EQ(s.e.phase, DEVUPD_END);
    devupd_link(&s.e, false, 0u, s.now);
    CHECK_EQ(s.e.phase, DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_UNSURE);
    CHECK(!devupd_busy(&s.e));
    /* END with no DONE in time: the same. */
    devupd_t e;
    devupd_init(&e);
    e.phase = DEVUPD_END;
    e.t = 0u;
    (void)devupd_step(&e, DEVUPD_END_MS - 1u, true, &o);
    CHECK_EQ(e.phase, DEVUPD_END);
    (void)devupd_step(&e, DEVUPD_END_MS, true, &o);
    CHECK_EQ(e.err, DEVUPD_E_UNSURE);
    /* Before END a lost link is a plain loss. */
    devupd_init(&e);
    e.phase = DEVUPD_SEND;
    devupd_link(&e, false, 0u, 0u);
    CHECK_EQ(e.err, DEVUPD_E_LOST);

    const fw_version_t want = { 0u, 25u, 0u };
    const fw_version_t old = { 0u, 24u, 0u };
    CHECK_EQ(devupd_settle(false, old, want, 0u), DEVUPD_SETTLE_WAIT);
    CHECK_EQ(devupd_settle(false, old, want, DEVUPD_SETTLE_MS - 1u),
             DEVUPD_SETTLE_WAIT);
    CHECK_EQ(devupd_settle(false, old, want, DEVUPD_SETTLE_MS),
             DEVUPD_SETTLE_UNKNOWN);
    CHECK_EQ(devupd_settle(true, want, want, 10u), DEVUPD_SETTLE_DONE);
    CHECK_EQ(devupd_settle(true, old, want, 10u), DEVUPD_SETTLE_FAILED);
    CHECK_EQ(devupd_settle(true, want, want, DEVUPD_SETTLE_MS + 1u),
             DEVUPD_SETTLE_DONE);
}

/* END's write answer never comes, as when the response is lost after the
 * device took END: the install is unknown, not refused. */
TEST_CASE(an_unanswered_end_write_leaves_the_install_to_the_version)
{
    static sim_t s;
    sim_init(&s);
    sim_start(&s);
    devupd_out_t o;
    while (devupd_step(&s.e, s.now, true, &o)) {
        devupd_written(&s.e, 0u, s.now);
    }
    s.e.phase = DEVUPD_SEND;
    s.e.next = s.e.im.size;
    s.e.t_credit = s.now;
    (void)devupd_step(&s.e, s.now, true, &o);
    CHECK_EQ(o.ctrl[0], BLEOTA_OP_END);
    CHECK(s.e.ctrl_busy);
    const uint32_t t0 = s.now;
    (void)devupd_step(&s.e, t0 + DEVUPD_ANSWER_MS - 1u, true, &o);
    CHECK_EQ(s.e.phase, DEVUPD_END);
    (void)devupd_step(&s.e, t0 + DEVUPD_ANSWER_MS, true, &o);
    CHECK_EQ(s.e.phase, DEVUPD_FAILED);
    CHECK_EQ(s.e.err, DEVUPD_E_UNSURE);
    /* A refusal the device answers is still a refused write. */
    devupd_t e;
    devupd_init(&e);
    e.phase = DEVUPD_END;
    e.ctrl_busy = true;
    devupd_written(&e, 0x80u, 0u);
    CHECK_EQ(e.err, DEVUPD_E_WRITE);
}

/* The busy flag that refuses ARM is the request or the run.  At every
 * point between the frame's ask and the `blec` task's end it reads busy,
 * and a start the `blec` task takes while ARMED is refused there. */
TEST_CASE(the_gate_reads_busy_from_the_ask_to_the_end_and_refuses_armed)
{
    devupd_gate_t g;
    atomic_init(&g.req, false);
    atomic_init(&g.run, false);
    atomic_init(&g.dev, 0u);
    unsigned dev = 99u;
    CHECK(!devupd_gate_busy(&g));
    CHECK_EQ(devupd_gate_take(&g, false, &dev), DEVUPD_TAKE_NONE);
    CHECK_EQ(dev, 99u);                  /* nothing taken, nothing named */
    devupd_gate_ask(&g, 1u);
    CHECK(devupd_gate_busy(&g));
    /* The take's two stores, each seen on its own: busy throughout. */
    atomic_store(&g.run, true);
    CHECK(devupd_gate_busy(&g));
    atomic_store(&g.req, false);
    CHECK(devupd_gate_busy(&g));
    atomic_store(&g.run, false);
    CHECK(!devupd_gate_busy(&g));
    /* The real take names the device the ask named. */
    devupd_gate_ask(&g, 2u);
    CHECK_EQ(devupd_gate_take(&g, false, &dev), DEVUPD_TAKE_START);
    CHECK_EQ(dev, 2u);
    CHECK(devupd_gate_busy(&g));
    CHECK(!atomic_load(&g.req));
    devupd_gate_ask(&g, 0u);             /* a second ask while it runs */
    CHECK(devupd_gate_busy(&g));
    devupd_gate_end(&g);
    CHECK(devupd_gate_busy(&g));         /* the second ask still stands */
    CHECK_EQ(devupd_gate_take(&g, true, &dev), DEVUPD_TAKE_ARMED);
    CHECK_EQ(dev, 0u);                   /* the refused ask names its own */
    CHECK(!devupd_gate_busy(&g));
    CHECK_EQ(devupd_gate_take(&g, false, NULL), DEVUPD_TAKE_NONE);
    devupd_gate_ask(&g, 1u);
    CHECK_EQ(devupd_gate_take(&g, false, NULL), DEVUPD_TAKE_START);
    devupd_gate_end(&g);
    CHECK(!devupd_gate_busy(NULL));
    CHECK_EQ(devupd_gate_take(NULL, false, &dev), DEVUPD_TAKE_NONE);
    devupd_gate_ask(NULL, 0u);
    devupd_gate_end(NULL);
}

/** @brief Hand the engine one CTRL notification. */
static void notify_st(devupd_t *e, uint8_t ev, uint8_t code, uint32_t next,
                      uint32_t limit, uint32_t now)
{
    bleota_status_t st;
    uint8_t b[BLEOTA_LEN_STATUS];
    memset(&st, 0, sizeof st);
    st.ev = ev;
    st.code = code;
    st.next = next;
    st.limit = limit;
    st.secs = 30u;
    CHECK_EQ(bleota_status_encode(&st, b), (size_t)BLEOTA_LEN_STATUS);
    devupd_notify(e, b, sizeof b, now);
}

/** @brief Step the engine, every write taken, to @p until in 5ms steps,
 *         the device answering CREDIT with the gap mark at @p at every
 *         100ms. */
static void stuck_until(devupd_t *e, uint32_t *now, uint32_t until,
                        uint32_t at)
{
    devupd_out_t o;
    while (*now < until && devupd_busy(e)) {
        *now += STEP;
        for (int i = 0; i < 4 && devupd_step(e, *now, true, &o); ++i) {
            if (o.kind == DEVUPD_OUT_CTRL) {
                devupd_written(e, 0u, *now);
            }
        }
        if (*now % 100u == 0u) {
            notify_st(e, BLEOTA_EV_CREDIT, BLEOTA_CREDIT_GAP, at,
                      at + BLEOTA_WINDOW, *now);
        }
    }
}

/* A device that answers every chunk with the gap mark at the same offset
 * clears the polls each time: only the progress limit ends it, 30s after
 * the offset last moved, and a move starts the 30s again. */
TEST_CASE(a_transfer_that_makes_no_progress_ends_at_30s)
{
    make_image();
    devupd_image_t im;
    memset(&im, 0, sizeof im);
    im.img = s_img;
    im.size = IMG;
    im.kind = BLEOTA_KIND_DIAL;
    im.version = (fw_version_t){ 0u, 25u, 0u };
    static devupd_t e;
    devupd_init(&e);
    uint32_t now = 1000u;
    CHECK(devupd_start(&e, &im, now));
    devupd_link(&e, true, BLEOTA_MTU, now);
    devupd_out_t o;
    for (int i = 0; i < 8 && devupd_step(&e, now, true, &o); ++i) {
        if (o.kind == DEVUPD_OUT_CTRL) {
            devupd_written(&e, 0u, now);
        }
    }
    notify_st(&e, BLEOTA_EV_WAIT, 0u, 0u, 0u, now);
    notify_st(&e, BLEOTA_EV_CREDIT, 0u, 0u, BLEOTA_WINDOW, now);
    CHECK_EQ(e.phase, DEVUPD_SEND);
    /* 20s at offset 0, then one move to 4,096: the limit starts again. */
    stuck_until(&e, &now, 21000u, 0u);
    CHECK_EQ(e.phase, DEVUPD_SEND);
    const uint32_t moved = now;
    notify_st(&e, BLEOTA_EV_CREDIT, 0u, 4096u, 4096u + BLEOTA_WINDOW, now);
    stuck_until(&e, &now, moved + DEVUPD_STALL_MS - 100u, 4096u);
    CHECK_EQ(e.phase, DEVUPD_SEND);           /* 29.9s: still sending */
    CHECK_EQ(e.polls_sent, 0u);               /* every answer came */
    stuck_until(&e, &now, moved + DEVUPD_STALL_MS + 100u, 4096u);
    CHECK_EQ(e.phase, DEVUPD_FAILED);         /* 30.1s: ended */
    CHECK_EQ(e.err, DEVUPD_E_STALLED);
    CHECK(!devupd_busy(&e));
    CHECK_EQ(e.next, 4096u);
}

/** @brief A simulation run until the engine reaches @p phase; false when
 *         it did not within 60s. */
static bool run_to(sim_t *s, devupd_phase_t phase)
{
    const uint32_t end = s->now + 60000u;
    while (s->e.phase != phase && devupd_busy(&s->e) && s->now < end) {
        sim_step(s);
    }
    return s->e.phase == phase;
}

/* An abort before END ends the transfer at once in every phase, and the
 * device drops what it took on the disconnect.  From END on the device
 * installs whatever comes: the abort does nothing, and the transfer runs
 * on to DONE, or to the unknown result when the link drops. */
TEST_CASE(an_abort_ends_each_phase_before_end_and_waits_through_the_install)
{
    static const devupd_phase_t k_before[] = {
        DEVUPD_LINK, DEVUPD_BEGIN, DEVUPD_CONFIRM, DEVUPD_SEND,
    };
    for (size_t i = 0; i < sizeof(k_before) / sizeof(k_before[0]); ++i) {
        static sim_t s;
        sim_init(&s);
        s.press = false;
        s.press_at = 0u;
        CHECK(devupd_start(&s.e, &s.im, s.now));
        if (k_before[i] != DEVUPD_LINK) {
            link_up(&s);
            if (k_before[i] == DEVUPD_SEND) {
                s.press = true;
            }
            if (!run_to(&s, k_before[i])) {
                T_FAIL("the phase was not reached");
                continue;
            }
        }
        CHECK(devupd_abortable(&s.e));
        devupd_abort(&s.e, s.now);
        CHECK_EQ(s.e.phase, DEVUPD_FAILED);
        CHECK_EQ(s.e.err, DEVUPD_E_ABORTED);
        CHECK(!devupd_busy(&s.e));
        CHECK(!devupd_abortable(&s.e));
        link_down(&s);
        CHECK(!s.restarted);
        CHECK_EQ(s.e.err, DEVUPD_E_ABORTED);   /* the drop changes nothing */
    }
    /* END: the abort waits, DONE comes, the device restarts. */
    devupd_t e;
    devupd_init(&e);
    e.phase = DEVUPD_END;
    e.t = 0u;
    e.up = true;
    CHECK(!devupd_abortable(&e));
    devupd_abort(&e, 10u);
    CHECK_EQ(e.phase, DEVUPD_END);
    CHECK(devupd_busy(&e));
    notify_st(&e, BLEOTA_EV_DONE, 0u, 0u, 0u, 20u);
    CHECK_EQ(e.phase, DEVUPD_RESTART);
    devupd_abort(&e, 30u);
    CHECK_EQ(e.phase, DEVUPD_RESTART);
    devupd_link(&e, false, 0u, 40u);
    CHECK_EQ(e.phase, DEVUPD_DONE);
    /* END, aborted, then the link drops: the result is not known. */
    devupd_init(&e);
    e.phase = DEVUPD_END;
    e.up = true;
    devupd_abort(&e, 10u);
    devupd_link(&e, false, 0u, 20u);
    CHECK_EQ(e.err, DEVUPD_E_UNSURE);
    devupd_abort(NULL, 0u);
    CHECK(!devupd_abortable(NULL));
}

/* The settle of an unknown install reads its own device alone: a second
 * device reporting the wanted version settles nothing.  A second device's
 * start reports the pending settle first: from its own device when that
 * one reported, else as unknown. */
TEST_CASE(the_settle_reads_its_own_device_and_reports_before_a_new_start)
{
    const fw_version_t want = { 0u, 23u, 12u };
    const fw_version_t old = { 0u, 23u, 11u };
    devupd_seen_t seen[3];
    memset(seen, 0, sizeof seen);
    devupd_settler_t st;
    memset(&st, 0, sizeof st);
    devupd_settled_t out;
    CHECK(!devupd_settler_step(&st, seen, 3u, 0u, false, &out));
    devupd_settler_arm(&st, 1u, want, 1000u);
    /* Devices 0 and 2 report the wanted version: device 1 still waits. */
    seen[0] = (devupd_seen_t){ true, want };
    seen[2] = (devupd_seen_t){ true, want };
    CHECK(!devupd_settler_step(&st, seen, 3u, 2000u, false, &out));
    CHECK(st.pending);
    /* Device 2's start: device 1 has not reported, so unknown, and the
     * settle is over before the start. */
    CHECK(devupd_settler_step(&st, seen, 3u, 3000u, true, &out));
    CHECK_EQ(out.dev, 1u);
    CHECK_EQ(out.r, DEVUPD_SETTLE_UNKNOWN);
    CHECK(!st.pending);
    CHECK(!devupd_settler_step(&st, seen, 3u, 4000u, false, &out));
    /* Device 1 back on the old version, a start: failed, its version. */
    devupd_settler_arm(&st, 1u, want, 1000u);
    seen[1] = (devupd_seen_t){ true, old };
    CHECK(devupd_settler_step(&st, seen, 3u, 3000u, true, &out));
    CHECK_EQ(out.r, DEVUPD_SETTLE_FAILED);
    CHECK_EQ(out.running.patch, 11u);
    /* Without a start: done once its device reports the version, and
     * unknown at 60s, never earlier. */
    devupd_settler_arm(&st, 1u, want, 1000u);
    seen[1].known = false;
    CHECK(!devupd_settler_step(&st, seen, 3u,
                               1000u + DEVUPD_SETTLE_MS - 1u, false, &out));
    CHECK(devupd_settler_step(&st, seen, 3u, 1000u + DEVUPD_SETTLE_MS,
                              false, &out));
    CHECK_EQ(out.r, DEVUPD_SETTLE_UNKNOWN);
    devupd_settler_arm(&st, 1u, want, 1000u);
    seen[1] = (devupd_seen_t){ true, want };
    CHECK(devupd_settler_step(&st, seen, 3u, 1500u, false, &out));
    CHECK_EQ(out.r, DEVUPD_SETTLE_DONE);
    /* A device past the table reads as not reported. */
    devupd_settler_arm(&st, 7u, want, 0u);
    CHECK(!devupd_settler_step(&st, seen, 3u, 10u, false, &out));
    CHECK(devupd_settler_step(&st, seen, 3u, 10u, true, &out));
    CHECK_EQ(out.r, DEVUPD_SETTLE_UNKNOWN);
    CHECK(!devupd_settler_step(NULL, seen, 3u, 0u, true, &out));
}

int main(void)
{
    RUN(a_whole_image_goes_in_order_and_installs);
    RUN(a_lossless_link_with_latency_sends_every_byte_once);
    RUN(a_link_lost_after_end_leaves_the_install_to_the_version);
    RUN(an_unanswered_end_write_leaves_the_install_to_the_version);
    RUN(the_gate_reads_busy_from_the_ask_to_the_end_and_refuses_armed);
    RUN(a_lost_chunk_is_sent_again_from_the_devices_offset);
    RUN(a_lost_last_chunk_is_found_by_poll);
    RUN(a_credit_stall_polls_three_times_then_ends_it);
    RUN(each_refusal_ends_it_with_the_devices_code);
    RUN(a_wrong_sha_is_refused_at_end_and_nothing_installs);
    RUN(no_confirmation_ends_it_with_the_devices_code);
    RUN(a_disconnect_mid_transfer_ends_it_and_a_retry_starts_at_byte_0);
    RUN(no_link_and_no_answer_end_it_at_their_limits);
    RUN(a_refused_control_write_ends_it);
    RUN(the_chunk_follows_the_mtu_and_a_refused_chunk_goes_again);
    RUN(busy_holds_from_the_start_to_the_restart_and_arm_is_refused);
    RUN(the_row_offers_the_hold_only_when_nothing_stands_in_the_way);
    RUN(a_transfer_that_makes_no_progress_ends_at_30s);
    RUN(an_abort_ends_each_phase_before_end_and_waits_through_the_install);
    RUN(the_settle_reads_its_own_device_and_reports_before_a_new_start);
    return test_summary("devupd");
}
