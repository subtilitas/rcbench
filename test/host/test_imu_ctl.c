/*
 * The Bluetooth IMU's controller (imu_ctl.h) against the scripted host
 * stack and sensor of ble_sim.h, 1ms at a time (imu_rig.h).
 *
 * The failures this is about: a panel that scans or connects before
 * Bluetooth is synced; a scan that lists another device as the sensor or
 * loses the sensor's name; a bind that leaves SETUP without the address;
 * sensor bytes read before the link is subscribed; a link that drops and
 * never comes back; a calibration that leaves the sensor in CALSW 1; a
 * command group cut in the middle by a refused write; a RATE written to a
 * sensor that is not there and never again; an IMU OFF that leaves the
 * link up.
 *
 * SPDX-License-Identifier: MIT
 */
#include "imu_rig.h"

/* The panel's Bluetooth synced, IMU ON, bound to @p addr. */
static void on_with_sensor(rig_t *r, const char *addr)
{
    rig_init(r);
    rig_cfg(r, true, addr, 2, true);
}

/* Bound to the sensor, subscribed, and streaming at 50Hz. */
static void connected(rig_t *r)
{
    on_with_sensor(r, RIG_ADDR);
    (void)rig_settle(r, 2000u);
    CHECK(imu_ble_can_send(&r->c.ble));
    rig_stream(r, 100u, 20u, 1.0f, 2.0f, 3.0f);
    CHECK_EQ(imu_ctl_link(&r->c), IMU_LINK_CONNECTED);
}

/* Bluetooth's state is the link's until it is synced, and nothing goes to
 * the host stack before: STARTING, NO BLUETOOTH ON C6, C6 FAILED.  Synced
 * without a sensor bound: READY; IMU OFF: OFF whatever Bluetooth says. */
TEST_CASE(the_link_waits_for_bluetooth_and_reads_its_state)
{
    rig_t r;
    rig_init(&r);
    imu_ctl_radio(&r.c, IMU_RADIO_OFF, NULL, r.t);
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    static const struct { imu_radio_t radio; imu_link_t link; } k[] = {
        { IMU_RADIO_OFF, IMU_LINK_STARTING },
        { IMU_RADIO_STARTING, IMU_LINK_STARTING },
        { IMU_RADIO_NO_SYNC, IMU_LINK_NO_BT },
        { IMU_RADIO_NO_LINK, IMU_LINK_C6_FAILED },
        { (imu_radio_t)99, IMU_LINK_STARTING },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        imu_ctl_radio(&r.c, k[i].radio, "", r.t);
        rig_run(&r, 3000u);
        CHECK_EQ(imu_ctl_link(&r.c), k[i].link);
    }
    CHECK_EQ(r.sim.nlog, 0u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.version, "");
    /* Synced: the bound sensor is connected at once. */
    imu_ctl_radio(&r.c, IMU_RADIO_READY, "2.13.0", r.t);
    rig_tick(&r);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTING);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), 1u);
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.version, "2.13.0");
    /* Nothing bound: READY. */
    rig_cfg(&r, true, "", 2, true);
    rig_run(&r, 3000u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_READY);
    rig_cfg(&r, false, "", 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
    /* A stored HC-05 address is no BLE address: nothing bound. */
    rig_cfg(&r, true, "98D3:31:F5A1B2", 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_READY);
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.bound_addr, "");
}

/* SCAN runs 10s: SCANNING at 9,999ms, the list at 10,000ms with the sensor
 * by its name, address and RSSI, the phone that does not advertise the
 * service counted as hidden.  A tap binds it at once: SETUP keeps the
 * address with its type and the name, and the connect follows. */
TEST_CASE(scan_lists_the_sensor_hides_the_rest_and_bind_keeps_its_address)
{
    rig_t r;
    on_with_sensor(&r, "");
    static const uint8_t phone[] = { 0x02, 0x01, 0x06, 0x06, 0x09,
                                     'P', 'H', 'O', 'N', 'E' };
    const bleu_addr_t pa = { { 1, 2, 3, 4, 5, 6 }, BLEU_ADDR_RANDOM };
    bsim_adv(&r.sim, 300u, &pa, -40, phone, sizeof phone);
    rig_run(&r, 10u);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));   /* one at a time */
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_SCANNING);
    rig_tick(&r);
    const bsim_log_t *s = bsim_last(&r.sim, BLEU_ACT_SCAN);
    CHECK(s != NULL);
    const uint32_t t0 = s->t;
    while (r.t != t0 + BLEU_SCAN_MS) {
        rig_tick(&r);
    }
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_SCANNING);
    rig_tick(&r);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_READY);
    CHECK_EQ(v.found_n, 1u);
    CHECK_STR_EQ(v.found[0].addr, "00:0C:BF:08:19:29");
    CHECK_EQ(v.found[0].addr_type, BLEU_ADDR_PUBLIC);
    CHECK_STR_EQ(v.found[0].name, "HC-06");
    CHECK_EQ(v.found[0].rssi, -50);
    CHECK_EQ(v.found[0].flags, IMU_DEV_UUID_18F0);
    CHECK_EQ(v.hidden, 1u);
    CHECK_EQ(v.err, IMU_ERR_NONE);
    /* An index past the list is refused. */
    const imu_cmd_t past = { IMU_CMD_BIND, 1u };
    CHECK(!imu_ctl_command(&r.c, &past, r.t));
    const imu_cmd_t bind = { IMU_CMD_BIND, 0u };
    CHECK(imu_ctl_command(&r.c, &bind, r.t));
    CHECK_EQ(r.c.bind_seq, 1u);
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.bound_addr, RIG_ADDR);
    CHECK_STR_EQ(v.bound_name, "HC-06");
    CHECK_EQ(v.link, IMU_LINK_CONNECTING);
    /* SETUP's next values say the same: nothing changes. */
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    CHECK_EQ(r.c.bind_seq, 1u);
    (void)rig_settle(&r, 2000u);
    const bsim_log_t *c = bsim_last(&r.sim, BLEU_ACT_CONNECT);
    CHECK(c != NULL);
    CHECK(bleu_addr_eq(&c->a.addr, &k_rig_addr));
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BOUND);
    rig_stream(&r, 60u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTED);
    rig_run(&r, IMU_STALE_MS);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_NO_FRAMES);
}

/* A scan that hears no sensor says so; a scan the host stack refuses
 * gives its status; the next scan clears both. */
TEST_CASE(a_scan_without_a_sensor_or_refused_says_why)
{
    rig_t r;
    on_with_sensor(&r, "");
    r.sim.nadv = 0u;
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    rig_run(&r, BLEU_SCAN_MS + 10u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.found_n, 0u);
    CHECK_EQ(v.err, IMU_ERR_NONE_FOUND);
    r.sim.scan_fail = true;
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.err, IMU_ERR_NONE);
    rig_run(&r, 10u);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_READY);
    CHECK_EQ(v.err, IMU_ERR_SCAN);
    CHECK_EQ(v.err_code, 2u);
}

/* The sensor's bytes reach the reading only once 1E4D is subscribed; the
 * owner's capture, cut at 20, 4 and 1 bytes, gives its angle frame. */
TEST_CASE(the_stream_reaches_the_reading_once_subscribed)
{
    static const uint8_t cap[20] = {
        0x02, 0x47, 0x55, 0x53, 0x33, 0xFF, 0x19, 0xFD, 0xF0, 0xED,
        0x38, 0x48, 0x4D, 0x55, 0x54, 0x1A, 0xEE, 0x49, 0x24, 0x68,
    };
    static const size_t cuts[] = { 20u, 4u, 1u };
    for (size_t k = 0; k < sizeof cuts / sizeof cuts[0]; ++k) {
        rig_t r;
        on_with_sensor(&r, RIG_ADDR);
        /* Connected, not yet subscribed: a notification is not the
         * stream. */
        while (!r.sim.connected) {
            rig_tick(&r);
        }
        rig_notify(&r, cap, sizeof cap, cuts[k]);
        CHECK_EQ(r.c.sensor.m.r.frames, 0u);
        (void)rig_settle(&r, 2000u);
        rig_notify(&r, cap, sizeof cap, cuts[k]);
        imu_view_t v;
        imu_ctl_view(&r.c, &v);
        CHECK(v.r.valid);
        CHECK(fabsf(v.r.angle[0] - -1.126f) < 0.001f);
        CHECK(fabsf(v.r.angle[1] - -4.081f) < 0.001f);
        CHECK(fabsf(v.r.angle[2] - -25.400f) < 0.001f);
        CHECK_EQ(v.r.version, 0x4838u);
        CHECK_EQ(v.link, IMU_LINK_CONNECTED);
    }
}

/* A bound device without the ISSC service: BOUND, NOT A WITMOTION SENSOR,
 * and no connect again until another bind. */
TEST_CASE(a_device_without_the_service_is_not_a_witmotion_sensor)
{
    rig_t r;
    rig_init(&r);
    r.sim.no_svc = true;
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    rig_run(&r, 1000u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_BOUND);
    CHECK_EQ(v.err, IMU_ERR_NOT_WIT);
    const unsigned n = bsim_count(&r.sim, BLEU_ACT_CONNECT);
    rig_run(&r, 10000u);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), n);
}

/* The link drops: CONNECTING with NO CONNECTION, the reading goes stale,
 * the link comes back after the retry gap, and the zero stays. */
TEST_CASE(a_dropped_link_comes_back_and_keeps_the_zero)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t zero = { IMU_CMD_ZERO, 0u };
    CHECK(imu_ctl_command(&r.c, &zero, r.t));
    bsim_drop(&r.sim, r.t, 0x208u);
    rig_run(&r, 10u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_CONNECTING);
    CHECK_EQ(v.err, IMU_ERR_CONNECT);
    CHECK_EQ(v.err_code, 0x208u);
    const uint32_t g = r.c.sensor.gaps;
    (void)rig_settle(&r, BLEU_RETRY_MS + 2000u);
    CHECK(imu_ble_up(&r.c.ble));
    rig_stream(&r, 100u, 20u, 1.0f, 2.0f, 3.0f);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_CONNECTED);
    CHECK_EQ(v.err, IMU_ERR_NONE);
    CHECK(v.r.zeroed);
    CHECK_EQ(r.c.sensor.gaps, g + 1u);
}

/* A write the sensor's module refuses: THE SENSOR DID NOT TAKE THE COMMAND,
 * and the rest of the group is dropped, not sent alone. */
TEST_CASE(a_refused_write_drops_the_rest_of_its_group)
{
    rig_t r;
    connected(&r);
    r.sim.fail = BSIM_Q_WRITE;
    r.sim.fail_status = 0x10Eu;
    const unsigned at = r.nsent;
    rig_cfg(&r, true, RIG_ADDR, 4, true);              /* RATE 200Hz */
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 1u);
    CHECK(rig_sent_unlock(&r, at));
    CHECK_EQ(r.c.sensor.out_n, 0u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.err, IMU_ERR_WRITE);
    CHECK_EQ(v.err_code, 0x10Eu);
    CHECK_EQ(v.link, IMU_LINK_CONNECTED);
}

/* CALIBRATE: unlock, CALSW 1; 3000ms later unlock, CALSW 0, SAVE; each
 * command 50ms after the one before; DONE with the frames on throughout. */
TEST_CASE(calibrate_sends_calsw_1_then_after_3s_calsw_0_and_save)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    const uint32_t t0 = r.t;
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    CHECK(!imu_ctl_command(&r.c, &cal, r.t));    /* one at a time */
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_RUNNING);
    CHECK_EQ(v.cal_left_ms, IMU_CAL_MS);
    rig_stream(&r, 1500u, 20u, 0.0f, 0.0f, 0.0f);
    imu_ctl_view(&r.c, &v);
    /* The last step of the 1500 was at 1499ms. */
    CHECK_EQ(v.cal_left_ms, IMU_CAL_MS - 1499u);
    CHECK_EQ(r.nsent, at + 2u);
    CHECK(rig_sent_unlock(&r, at));
    CHECK(rig_sent_is(&r, at + 1u, WIT_REG_CALSW, 1u, 0u));
    CHECK(r.sent_at[at + 1u] - r.sent_at[at] >= IMU_CMD_GAP_MS);
    /* 2999ms held: still running; at 3000ms it ends. */
    rig_stream(&r, 1500u, 20u, 0.0f, 0.0f, 0.0f);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_RUNNING);
    CHECK_EQ(v.cal_left_ms, 1u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_DONE);
    rig_stream(&r, 200u, 20u, 0.0f, 0.0f, 0.0f);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_DONE);
    CHECK_EQ(v.cal_left_ms, 0u);
    CHECK_EQ(r.nsent, at + 5u);
    CHECK(rig_sent_unlock(&r, at + 2u));
    CHECK(rig_sent_is(&r, at + 3u, WIT_REG_CALSW, 0u, 0u));
    CHECK(rig_sent_is(&r, at + 4u, WIT_REG_SAVE, 0u, 0u));
    CHECK(r.sent_at[at + 2u] - t0 >= IMU_CAL_MS);
    CHECK(r.sent_at[at + 4u] - r.sent_at[at + 3u] >= IMU_CMD_GAP_MS);
}

/* Frames that stop during the 3s: CALSW 0 and SAVE all the same, FAILED.
 * IMU OFF in it: FAILED as well, the exit queued; it goes once IMU is ON
 * and the link is up again. */
TEST_CASE(calibrate_with_the_frames_lost_ends_failed)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    rig_run(&r, 3000u + 3u * IMU_CMD_GAP_MS);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_FAILED);
    CHECK(rig_sent_is(&r, r.nsent - 1u, WIT_REG_SAVE, 0u, 0u));
    CHECK(rig_sent_is(&r, r.nsent - 2u, WIT_REG_CALSW, 0u, 0u));
    /* Not connected now: refused. */
    CHECK(!imu_ctl_command(&r.c, &cal, r.t));

    connected(&r);
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    rig_stream(&r, 500u, 20u, 0.0f, 0.0f, 0.0f);
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_FAILED);
    const unsigned off = r.nsent;
    rig_run(&r, 300u);
    CHECK_EQ(r.nsent, off);
    CHECK_EQ(r.c.sensor.out_n, 3u);
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    (void)rig_settle(&r, 2000u);
    rig_run(&r, 300u);
    CHECK_EQ(r.nsent, off + 3u);
    CHECK(rig_sent_is(&r, r.nsent - 2u, WIT_REG_CALSW, 0u, 0u));
    CHECK(rig_sent_is(&r, r.nsent - 1u, WIT_REG_SAVE, 0u, 0u));
}

/* Frames every 300ms in a calibration: each one ends a stale stretch the
 * step never saw stale, and the calibration still ends FAILED. */
TEST_CASE(calibrate_with_frames_300ms_apart_ends_failed)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    const uint32_t g0 = r.c.sensor.gaps;
    rig_stream(&r, 3100u, IMU_STALE_MS, 0.0f, 0.0f, 0.0f);
    CHECK(r.c.sensor.gaps > g0);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_FAILED);
}

/* The outbox holds IMU_OUTBOX commands; one more is dropped. */
TEST_CASE(a_full_outbox_drops_the_command_past_its_room)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    r.c.sensor.out_n = IMU_OUTBOX - 1u;              /* 7 held: room for one */
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));  /* unlock in, CALSW 1 out */
    CHECK_EQ(r.c.sensor.out_n, IMU_OUTBOX);
    rig_stream(&r, 100u * IMU_OUTBOX, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + IMU_OUTBOX);
    CHECK(rig_sent_unlock(&r, at + IMU_OUTBOX - 1u));
}

/* A RATE or ALGORITHM change while bound but not connected waits; once
 * frames come it goes: unlock, RATE, AXIS6, SAVE.  Only once. */
TEST_CASE(a_rate_change_goes_to_the_sensor_once_it_is_connected)
{
    rig_t r;
    on_with_sensor(&r, RIG_ADDR);
    (void)rig_settle(&r, 2000u);
    rig_cfg(&r, true, RIG_ADDR, 4, false);   /* 200Hz, 9-AXIS */
    rig_run(&r, 500u);
    CHECK_EQ(r.nsent, 0u);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 4u);
    CHECK(rig_sent_unlock(&r, 0u));
    CHECK(rig_sent_is(&r, 1u, WIT_REG_RATE, WIT_RATE_200HZ, 0u));
    CHECK(rig_sent_is(&r, 2u, WIT_REG_AXIS6, 0u, 0u));
    CHECK(rig_sent_is(&r, 3u, WIT_REG_SAVE, 0u, 0u));
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 4u);
    /* The same values again send nothing; ALGORITHM alone sends it alone. */
    rig_cfg(&r, true, RIG_ADDR, 4, false);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 4u);
    rig_cfg(&r, true, RIG_ADDR, 4, true);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 7u);
    CHECK(rig_sent_is(&r, 5u, WIT_REG_AXIS6, 1u, 0u));
    /* ALGORITHM held on the arrow, 12 changes 33ms apart: the outbox's 8
     * slots fill, the change waits, and the last write is SETUP's last. */
    bool six = true;
    for (int i = 0; i < 12; ++i) {
        six = !six;
        rig_cfg(&r, true, RIG_ADDR, 4, six);
        rig_stream(&r, 33u, 20u, 0.0f, 0.0f, 0.0f);
    }
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(!r.c.sensor.algo_due);
    CHECK_EQ(r.c.sensor.out_n, 0u);
    CHECK(r.nsent < RIG_SENT_MAX);
    unsigned last = RIG_SENT_MAX;
    for (unsigned i = 0; i < r.nsent; ++i) {
        if (r.sent[i][2] == WIT_REG_AXIS6) {
            last = i;
        }
    }
    CHECK(last < r.nsent);
    CHECK(rig_sent_is(&r, last, WIT_REG_AXIS6, six ? 1u : 0u, 0u));
    CHECK(rig_sent_is(&r, r.nsent - 1u, WIT_REG_SAVE, 0u, 0u));
    /* The first values a controller takes send nothing. */
    rig_t q;
    rig_init(&q);
    rig_cfg(&q, true, RIG_ADDR, 0, false);
    (void)rig_settle(&q, 2000u);
    rig_stream(&q, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(q.nsent, 0u);
}

/* IMU OFF: the link ends, the list and the errors go, nothing is
 * connected again; ON again connects the bound sensor. */
TEST_CASE(imu_off_ends_the_link_and_forgets_the_list)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    rig_run(&r, BLEU_SCAN_MS + 10u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.found_n, 1u);
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
    rig_run(&r, 100u);
    CHECK(!r.sim.connected);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.found_n, 0u);
    CHECK_EQ(v.hidden, 0u);
    CHECK_EQ(v.err, IMU_ERR_NONE);
    CHECK(!v.r.valid);
    const unsigned n = bsim_count(&r.sim, BLEU_ACT_CONNECT);
    rig_run(&r, 5000u);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), n);
    /* Commands with IMU OFF are refused. */
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    (void)rig_settle(&r, 2000u);
    CHECK(imu_ble_up(&r.c.ble));
}

/* The panel's start after a restart: SETUP holds the address and IMU OFF,
 * Bluetooth is not started.  IMU OFF keeps the address in the view; IMU
 * ON starts the radio, and its sync connects to the address at once,
 * without a scan. */
TEST_CASE(a_stored_address_with_imu_off_connects_without_a_scan_at_imu_on)
{
    rig_t r;
    memset(&r, 0, sizeof r);
    bsim_init(&r.sim);
    bsim_adv(&r.sim, 200u, &k_rig_addr, -50, k_rig_adv, sizeof k_rig_adv);
    imu_ctl_init(&r.c);
    r.t = 1000u;
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    rig_run(&r, 100u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_OFF);
    CHECK_STR_EQ(v.bound_addr, RIG_ADDR);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), 0);
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    imu_ctl_radio(&r.c, IMU_RADIO_STARTING, NULL, r.t);
    rig_run(&r, 100u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_STARTING);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), 0);
    imu_ctl_radio(&r.c, IMU_RADIO_READY, "2.13.0", r.t);
    rig_tick(&r);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_CONNECT), 1);
    CHECK_EQ(bsim_count(&r.sim, BLEU_ACT_SCAN), 0);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTING);
    (void)rig_settle(&r, 2000u);
    CHECK(imu_ble_up(&r.c.ble));
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.bound_addr, RIG_ADDR);
}

/* What cannot run now is refused: BIND without a scan, CALIBRATE and ZERO
 * without a reading, an unknown kind, SCAN before Bluetooth is synced.
 * ZERO and UNZERO with one. */
TEST_CASE(commands_are_refused_when_they_cannot_run)
{
    rig_t r;
    rig_init(&r);
    rig_cfg(&r, true, "", 2, true);
    imu_ctl_radio(&r.c, IMU_RADIO_STARTING, "", r.t);
    CHECK(!imu_ctl_command(&r.c, &(imu_cmd_t){ IMU_CMD_SCAN, 0u }, r.t));
    imu_ctl_radio(&r.c, IMU_RADIO_READY, "2.13.0", r.t);
    static const imu_cmd_kind_t k[] = { IMU_CMD_BIND, IMU_CMD_CALIBRATE,
                                        IMU_CMD_ZERO, IMU_CMD_NONE,
                                        (imu_cmd_kind_t)99 };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        const imu_cmd_t c = { k[i], 0u };
        CHECK(!imu_ctl_command(&r.c, &c, r.t));
    }
    CHECK(!imu_ctl_command(&r.c, NULL, r.t));
    CHECK(!imu_ctl_command(NULL, &(imu_cmd_t){ IMU_CMD_ZERO, 0u }, r.t));
    connected(&r);
    const imu_cmd_t zero = { IMU_CMD_ZERO, 0u }, unzero = { IMU_CMD_UNZERO, 0u };
    CHECK(imu_ctl_command(&r.c, &zero, r.t));
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK(v.r.zeroed);
    CHECK(fabsf(v.r.zero[2] - 3.0f) < 0.01f);
    CHECK_EQ(r.c.sensor.zero_gen, 1u);
    CHECK(imu_ctl_command(&r.c, &unzero, r.t));
    imu_ctl_view(&r.c, &v);
    CHECK(!v.r.zeroed);
    CHECK_EQ(r.c.sensor.zero_gen, 2u);
    /* IMU OFF with a zero kept is a new zero too; without one it is not. */
    CHECK(imu_ctl_command(&r.c, &zero, r.t));
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    CHECK_EQ(r.c.sensor.zero_gen, 4u);
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    CHECK_EQ(r.c.sensor.zero_gen, 4u);
}

/* BIND waits while a scan runs; binding the sensor that is connected
 * keeps the link. */
TEST_CASE(no_bind_while_a_scan_runs_and_a_rebind_keeps_the_link)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    const imu_cmd_t bind = { IMU_CMD_BIND, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    rig_stream(&r, BLEU_SCAN_MS + 10u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    CHECK(!imu_ctl_command(&r.c, &bind, r.t));          /* a scan runs */
    rig_stream(&r, BLEU_SCAN_MS + 10u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(imu_ctl_command(&r.c, &bind, r.t));
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTED);   /* the same one */
    CHECK_EQ(r.c.bind_seq, 1u);
}

/* RATE held on the arrow during a calibration: no setting is queued while
 * it runs; CALSW 0 and SAVE go first, then unlock, SETUP's last RATE and
 * SAVE. */
TEST_CASE(a_rate_change_in_a_calibration_waits_for_its_end)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    for (int i = 0; i < 20; ++i) {
        rig_cfg(&r, true, RIG_ADDR, i % 5, true);
        rig_stream(&r, 33u, 20u, 0.0f, 0.0f, 0.0f);
        CHECK(r.c.sensor.cal != IMU_CAL_RUNNING || r.nsent <= at + 2u);
    }
    rig_stream(&r, 3000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.sensor.cal, IMU_CAL_DONE);
    CHECK(!r.c.sensor.rate_due);
    CHECK_EQ(r.nsent, at + 8u);
    CHECK(rig_sent_unlock(&r, at));
    CHECK(rig_sent_is(&r, at + 1u, WIT_REG_CALSW, 1u, 0u));
    CHECK(rig_sent_unlock(&r, at + 2u));
    CHECK(rig_sent_is(&r, at + 3u, WIT_REG_CALSW, 0u, 0u));
    CHECK(rig_sent_is(&r, at + 4u, WIT_REG_SAVE, 0u, 0u));
    CHECK(rig_sent_unlock(&r, at + 5u));
    /* The 20th change is option 4: 200Hz. */
    CHECK(rig_sent_is(&r, at + 6u, WIT_REG_RATE, WIT_RATE_200HZ, 0u));
    CHECK(rig_sent_is(&r, at + 7u, WIT_REG_SAVE, 0u, 0u));
}

/* What goes on to SERVO: each frame once, a stale reading once, after a
 * reset (ANGLE SOURCE turned to IMU) the next reading whatever went before,
 * and a stale stretch that frames ended before SERVO looked as a stale
 * reading ahead of the valid one. */
TEST_CASE(the_handout_gives_each_frame_once_and_again_after_a_reset)
{
    imu_handout_t h;
    imu_handout_reset(&h);
    imu_reading_t rd;
    memset(&rd, 0, sizeof rd);
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING); /* stale */
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_NONE);
    imu_handout_reset(&h);
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING);
    rd.valid = true;
    rd.taken_ms = 100u;
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING);
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_NONE); /* same frame */
    rd.taken_ms = 120u;
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING);
    CHECK_EQ(h.last_taken, 120u);
    imu_handout_reset(&h);
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING);
    rd.valid = false;
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING); /* stale */
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_NONE);
    CHECK_EQ(h.last_taken, 120u);
    rd.valid = true;
    CHECK_EQ(imu_handout_next(&h, &rd, 0u), IMU_HAND_READING);
    /* Stale and valid again between two looks: the gap, then the frame. */
    rd.taken_ms = 520u;
    CHECK_EQ(imu_handout_next(&h, &rd, 1u), IMU_HAND_GAP);
    CHECK_EQ(imu_handout_next(&h, &rd, 1u), IMU_HAND_READING);
    CHECK_EQ(imu_handout_next(&h, &rd, 1u), IMU_HAND_NONE);
    /* A gap SERVO already saw as stale goes once, as the stale reading. */
    rd.valid = false;
    CHECK_EQ(imu_handout_next(&h, &rd, 1u), IMU_HAND_READING);
    rd.valid = true;
    rd.taken_ms = 900u;
    CHECK_EQ(imu_handout_next(&h, &rd, 2u), IMU_HAND_READING);
    /* A reset takes the count as it stands. */
    imu_handout_reset(&h);
    CHECK_EQ(imu_handout_next(&h, &rd, 7u), IMU_HAND_READING);
    CHECK_EQ(imu_handout_next(&h, &rd, 7u), IMU_HAND_NONE);
    CHECK_EQ(imu_handout_next(NULL, &rd, 0u), IMU_HAND_NONE);
    CHECK_EQ(imu_handout_next(&h, NULL, 0u), IMU_HAND_NONE);
    imu_handout_reset(NULL);
}

/* A frame 300ms or more after the one before ends a stale stretch: it is
 * counted with the time the reading went stale; a shorter pause is none. */
TEST_CASE(a_frame_after_the_stale_point_counts_a_gap)
{
    rig_t r;
    connected(&r);
    CHECK_EQ(r.c.sensor.gaps, 0u);
    const uint32_t last = r.c.sensor.m.r.taken_ms;
    rig_run(&r, 400u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.sensor.gaps, 1u);
    CHECK_EQ(r.c.sensor.gap_at_ms, last + IMU_STALE_MS);
    const uint32_t t1 = r.c.sensor.m.r.taken_ms;
    rig_run(&r, IMU_STALE_MS - 2u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(r.c.sensor.m.r.taken_ms - t1 < IMU_STALE_MS);
    CHECK_EQ(r.c.sensor.gaps, 1u);
    const uint32_t t2 = r.c.sensor.m.r.taken_ms;
    rig_run(&r, IMU_STALE_MS - 1u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.sensor.m.r.taken_ms - t2, IMU_STALE_MS);
    CHECK_EQ(r.c.sensor.gaps, 2u);
    /* IMU OFF and ON: the first frame after is no gap. */
    rig_cfg(&r, false, RIG_ADDR, 2, true);
    rig_cfg(&r, true, RIG_ADDR, 2, true);
    (void)rig_settle(&r, 5000u);
    rig_stream(&r, 100u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.sensor.gaps, 2u);
}

/* SET_IMU_RATE's options and what a NULL does. */
TEST_CASE(the_rate_options_are_the_wit_codes_and_null_does_nothing)
{
    CHECK_EQ(imu_ctl_rate_code(0), WIT_RATE_10HZ);
    CHECK_EQ(imu_ctl_rate_code(1), WIT_RATE_20HZ);
    CHECK_EQ(imu_ctl_rate_code(2), WIT_RATE_50HZ);
    CHECK_EQ(imu_ctl_rate_code(3), WIT_RATE_100HZ);
    CHECK_EQ(imu_ctl_rate_code(4), WIT_RATE_200HZ);
    CHECK_EQ(imu_ctl_rate_code(5), WIT_RATE_50HZ);
    CHECK_EQ(imu_ctl_rate_code(-1), WIT_RATE_50HZ);
    imu_ctl_init(NULL);
    imu_ctl_config(NULL, NULL, 0u);
    imu_ctl_radio(NULL, IMU_RADIO_READY, "", 0u);
    imu_ctl_t c;
    imu_ctl_init(&c);
    imu_ctl_config(&c, NULL, 0u);
    CHECK(!c.configured);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    imu_ctl_event(NULL, &e, 0u);
    imu_ctl_event(&c, NULL, 0u);
    bleu_act_t a;
    CHECK(!imu_ctl_step(NULL, 0u, &a));
    CHECK(!imu_ctl_step(&c, 0u, NULL));
    CHECK(!imu_ctl_step(&c, 0u, &a));        /* OFF: nothing */
    CHECK_EQ(imu_ctl_link(NULL), IMU_LINK_OFF);
    imu_view_t v;
    imu_ctl_view(NULL, &v);
    CHECK_EQ(v.link, IMU_LINK_OFF);
    imu_ctl_view(&c, NULL);
}

int main(void)
{
    RUN(the_link_waits_for_bluetooth_and_reads_its_state);
    RUN(scan_lists_the_sensor_hides_the_rest_and_bind_keeps_its_address);
    RUN(a_scan_without_a_sensor_or_refused_says_why);
    RUN(the_stream_reaches_the_reading_once_subscribed);
    RUN(a_device_without_the_service_is_not_a_witmotion_sensor);
    RUN(a_dropped_link_comes_back_and_keeps_the_zero);
    RUN(a_refused_write_drops_the_rest_of_its_group);
    RUN(calibrate_sends_calsw_1_then_after_3s_calsw_0_and_save);
    RUN(calibrate_with_the_frames_lost_ends_failed);
    RUN(calibrate_with_frames_300ms_apart_ends_failed);
    RUN(a_full_outbox_drops_the_command_past_its_room);
    RUN(a_rate_change_goes_to_the_sensor_once_it_is_connected);
    RUN(imu_off_ends_the_link_and_forgets_the_list);
    RUN(a_stored_address_with_imu_off_connects_without_a_scan_at_imu_on);
    RUN(commands_are_refused_when_they_cannot_run);
    RUN(no_bind_while_a_scan_runs_and_a_rebind_keeps_the_link);
    RUN(a_rate_change_in_a_calibration_waits_for_its_end);
    RUN(the_handout_gives_each_frame_once_and_again_after_a_reset);
    RUN(a_frame_after_the_stale_point_counts_a_gap);
    RUN(the_rate_options_are_the_wit_codes_and_null_does_nothing);
    return test_summary("imu_ctl");
}
