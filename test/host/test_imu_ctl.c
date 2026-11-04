/*
 * The Bluetooth IMU's controller (imu_ctl.h) against the scripted HC-05 of
 * hc05_sim.h, 1ms at a time (imu_rig.h).
 *
 * The failures this is about: a panel that never finds a module plugged in
 * after start; sensor frames fed to the AT machine or AT answers to the
 * reading; a bind that leaves SETUP without the address; a calibration
 * that leaves the sensor in CALSW 1; a RATE written to a sensor that is
 * not there and never again; an IMU OFF that leaves KEY high.
 *
 * SPDX-License-Identifier: MIT
 */
#include "imu_rig.h"

/* A 2.0 module at 115200 with one sensor in range, IMU ON. */
static void on_with_sensor(rig_t *r, const char *addr)
{
    rig_init(r, 2, HC05_BAUD_DATA);
    sim_add_dev(&r->sim, "98D3:31:F5A1B2", 0x1F00u, 0xFFC0u, "HC-06", 900u);
    rig_cfg(r, true, addr, 2, true);
}

/* Probed, bound to the sensor and streaming at 50Hz. */
static void connected(rig_t *r)
{
    on_with_sensor(r, "98D3:31:F5A1B2");
    (void)rig_settle(r, 5000u);
    CHECK(r->c.probed);
    rig_stream(r, 100u, 20u, 1.0f, 2.0f, 3.0f);
    CHECK_EQ(imu_ctl_link(&r->c), IMU_LINK_CONNECTED);
}

/* IMU ON probes at once; a module that does not answer is NO HC-05, and the
 * probe goes again 10s later, not before. */
TEST_CASE(imu_on_probes_and_a_silent_hc05_is_no_module_retried_every_10s)
{
    rig_t r;
    rig_init(&r, 2, HC05_BAUD_DATA);
    r.sim.silent = true;
    rig_cfg(&r, false, "", 2, true);
    rig_run(&r, 50u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
    CHECK_EQ(r.sim.sends, 0u);
    rig_cfg(&r, true, "", 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_NO_MODULE);
    const uint32_t took = rig_settle(&r, 10000u);
    /* KEY settles, AT times out at the 3 bauds, then the fence's
     * AT+STATE? waits its 2000ms. */
    CHECK(took <= HC05_KEY_SETTLE_MS + 3u * HC05_AT_MS + 2u * HC05_AT_MS);
    CHECK_EQ(r.c.err, HC05_ERR_NO_AT);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_NO_MODULE);
    const unsigned sends = r.sim.sends;
    CHECK(sends >= 3u);              /* AT at each of the three bauds */
    const uint32_t failed_at = r.t;
    rig_run(&r, IMU_PROBE_RETRY_MS - 2u);
    CHECK_EQ(r.sim.sends, sends);
    rig_run(&r, 200u);
    CHECK(r.sim.sends > sends);
    CHECK(r.t - failed_at >= IMU_PROBE_RETRY_MS);
    /* The module plugged in: the next probe finds it. */
    r.sim.silent = false;
    (void)rig_settle(&r, 10000u);
    rig_run(&r, IMU_PROBE_RETRY_MS + 10u);
    (void)rig_settle(&r, 10000u);
    CHECK(r.c.probed);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_READY);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_STR_EQ(v.version, "2.0-20100601");
    CHECK_EQ(r.at_key_low, 0u);
    /* OFF and ON again in the 10s after a failed probe: the probe goes at
     * once, not at the retry's time. */
    r.sim.silent = true;
    rig_cfg(&r, false, "", 2, true);
    (void)rig_settle(&r, 10000u);
    rig_cfg(&r, true, "", 2, true);
    (void)rig_settle(&r, 10000u);
    CHECK_EQ(r.c.err, HC05_ERR_NO_AT);
    rig_cfg(&r, false, "", 2, true);
    rig_cfg(&r, true, "", 2, true);
    const unsigned before = r.sim.sends;
    rig_run(&r, 200u);
    CHECK(r.sim.sends > before);
}

/* Probed: READY without a sensor, BOUND with one, CONNECTED with frames,
 * NO FRAMES when they stop for 300ms. */
TEST_CASE(a_probe_that_answers_is_ready_without_a_sensor_and_bound_with_one)
{
    rig_t r;
    on_with_sensor(&r, "");
    (void)rig_settle(&r, 5000u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_READY);
    on_with_sensor(&r, "98D3:31:F5A1B2");
    (void)rig_settle(&r, 5000u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BOUND);
    CHECK(!r.sim.key);
    rig_stream(&r, 60u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTED);
    rig_run(&r, 250u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_CONNECTED);
    rig_run(&r, 60u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_NO_FRAMES);
    /* Another address in SETUP: no frames from it yet. */
    rig_cfg(&r, true, "1:2:3", 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BOUND);
    CHECK(!r.c.op_open);
}

/* SCAN lists the devices with their names; BIND binds the one picked and
 * keeps its address and name; the bind counter says so once. */
TEST_CASE(scan_lists_the_devices_and_bind_keeps_the_address_and_name)
{
    rig_t r;
    on_with_sensor(&r, "");
    sim_add_dev(&r.sim, "12:34:56", 0x0200u, 0xFFB0u, "PHONE", 2000u);
    (void)rig_settle(&r, 5000u);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));   /* one at a time */
    rig_tick(&r);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_SCANNING);
    (void)rig_settle(&r, 40000u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.link, IMU_LINK_READY);
    CHECK_EQ(v.found_n, 2u);
    CHECK_STR_EQ(v.found[0].addr, "98D3:31:F5A1B2");
    CHECK_STR_EQ(v.found[0].name, "HC-06");
    CHECK_STR_EQ(v.found[1].name, "PHONE");
    /* An index past the list is refused. */
    const imu_cmd_t past = { IMU_CMD_BIND, 2u };
    CHECK(!imu_ctl_command(&r.c, &past, r.t));
    const imu_cmd_t bind = { IMU_CMD_BIND, 0u };
    CHECK(imu_ctl_command(&r.c, &bind, r.t));
    rig_tick(&r);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BINDING);
    (void)rig_settle(&r, 40000u);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(r.c.err, HC05_ERR_NONE);
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_STR_EQ(v.bound_addr, "98D3:31:F5A1B2");
    CHECK_STR_EQ(v.bound_name, "HC-06");
    CHECK_EQ(v.link, IMU_LINK_BOUND);
    CHECK_STR_EQ(r.sim.bind, "98D3,31,F5A1B2");
    CHECK_EQ(r.sim.cmode, 0);
    CHECK(!r.sim.key);
    /* SETUP's next values say the same: nothing changes. */
    rig_cfg(&r, true, "98D3:31:F5A1B2", 2, true);
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BOUND);
    /* The module unplugged: a scan finds no AT, the link reads NO HC-05,
     * and the probe goes again 10s later. */
    r.sim.silent = true;
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    (void)rig_settle(&r, 40000u);
    CHECK_EQ(r.c.err, HC05_ERR_NO_AT);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_NO_MODULE);
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));
    r.sim.silent = false;
    rig_run(&r, IMU_PROBE_RETRY_MS + 10u);
    (void)rig_settle(&r, 10000u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_BOUND);
}

/* A 3.0 module refuses the scan with ERROR:(1F): CONNECT TO ANY SENSOR is
 * offered, and ANY keeps IMU_ADDR_ANY with no name. */
TEST_CASE(a_refused_scan_offers_any_and_any_keeps_the_any_address)
{
    rig_t r;
    rig_init(&r, 3, HC05_BAUD_DATA);
    rig_cfg(&r, true, "", 2, true);
    (void)rig_settle(&r, 5000u);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    (void)rig_settle(&r, 40000u);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK(v.inq_refused);
    CHECK_EQ(v.err, HC05_ERR_INQ_REFUSED);
    CHECK_EQ(v.err_code, 0x1Fu);
    CHECK_EQ(v.found_n, 0u);
    /* The next operation's start clears the last error until it ends. */
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    CHECK_EQ(r.c.err, HC05_ERR_NONE);
    CHECK_EQ(r.c.err_code, 0u);
    (void)rig_settle(&r, 40000u);
    CHECK_EQ(r.c.err, HC05_ERR_INQ_REFUSED);
    const imu_cmd_t any = { IMU_CMD_ANY, 0u };
    CHECK(imu_ctl_command(&r.c, &any, r.t));
    (void)rig_settle(&r, 40000u);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.err, HC05_ERR_NONE);
    CHECK_STR_EQ(v.bound_addr, IMU_ADDR_ANY);
    CHECK_STR_EQ(v.bound_name, "");
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_EQ(r.sim.cmode, 1);
    CHECK_EQ(v.link, IMU_LINK_BOUND);
    /* A scan that starts again clears the offer until it ends. */
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    CHECK(!r.c.inq_refused);
    (void)rig_settle(&r, 40000u);
    CHECK(r.c.inq_refused);
    /* IMU OFF clears it too. */
    rig_cfg(&r, false, IMU_ADDR_ANY, 2, true);
    imu_ctl_view(&r.c, &v);
    CHECK(!v.inq_refused);
    CHECK_EQ(v.err, HC05_ERR_NONE);
}

/* Outside an operation the bytes are the sensor's; inside one they are the
 * HC-05's, and a frame there reaches no reading. */
TEST_CASE(sensor_bytes_reach_the_reading_only_outside_an_operation)
{
    rig_t r;
    connected(&r);
    const uint32_t frames = r.c.m.r.frames;
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    rig_stream(&r, 400u, 20u, 5.0f, 5.0f, 5.0f);
    CHECK_EQ(r.c.m.r.frames, frames);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_SCANNING);
    (void)rig_settle(&r, 40000u);
    rig_stream(&r, 40u, 20u, 5.0f, 5.0f, 5.0f);
    CHECK(r.c.m.r.frames > frames);
    /* IMU OFF: no reading at all. */
    rig_cfg(&r, false, "98D3:31:F5A1B2", 2, true);
    rig_stream(&r, 40u, 20u, 5.0f, 5.0f, 5.0f);
    CHECK_EQ(r.c.m.r.frames, 0u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
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
 * IMU OFF in it: FAILED as well, the exit queued. */
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
    rig_cfg(&r, false, "98D3:31:F5A1B2", 2, true);
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.cal, IMU_CAL_FAILED);
    rig_run(&r, 300u);
    CHECK(rig_sent_is(&r, r.nsent - 2u, WIT_REG_CALSW, 0u, 0u));
}

/* Frames every 300ms in a calibration: each one ends a stale stretch the
 * step never saw stale, and the calibration still ends FAILED. */
TEST_CASE(calibrate_with_frames_300ms_apart_ends_failed)
{
    rig_t r;
    connected(&r);
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    const uint32_t g0 = r.c.gaps;
    rig_stream(&r, 3100u, IMU_STALE_MS, 0.0f, 0.0f, 0.0f);
    CHECK(r.c.gaps > g0);
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
    r.c.out_n = IMU_OUTBOX - 1u;              /* 7 held: room for one */
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));  /* unlock in, CALSW 1 out */
    CHECK_EQ(r.c.out_n, IMU_OUTBOX);
    rig_stream(&r, 100u * IMU_OUTBOX, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + IMU_OUTBOX);
    CHECK(rig_sent_unlock(&r, at + IMU_OUTBOX - 1u));
}

/* A RATE or ALGORITHM change while bound but not connected waits; once
 * frames come it goes: unlock, RATE, AXIS6, SAVE.  Only once. */
TEST_CASE(a_rate_change_goes_to_the_sensor_once_it_is_connected)
{
    rig_t r;
    on_with_sensor(&r, "98D3:31:F5A1B2");
    (void)rig_settle(&r, 5000u);
    rig_cfg(&r, true, "98D3:31:F5A1B2", 4, false);   /* 200Hz, 9-AXIS */
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
    rig_cfg(&r, true, "98D3:31:F5A1B2", 4, false);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 4u);
    rig_cfg(&r, true, "98D3:31:F5A1B2", 4, true);
    rig_stream(&r, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, 7u);
    CHECK(rig_sent_is(&r, 5u, WIT_REG_AXIS6, 1u, 0u));
    /* ALGORITHM held on the arrow, 12 changes 33ms apart: the outbox's 8
     * slots fill, the change waits, and the last write is SETUP's last. */
    bool six = true;
    for (int i = 0; i < 12; ++i) {
        six = !six;
        rig_cfg(&r, true, "98D3:31:F5A1B2", 4, six);
        rig_stream(&r, 33u, 20u, 0.0f, 0.0f, 0.0f);
    }
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(!r.c.algo_due);
    CHECK_EQ(r.c.out_n, 0u);
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
    rig_init(&q, 2, HC05_BAUD_DATA);
    rig_cfg(&q, true, "98D3:31:F5A1B2", 0, false);
    (void)rig_settle(&q, 5000u);
    rig_stream(&q, 300u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(q.nsent, 0u);
}

/* IMU OFF in a scan: the scan ends, KEY goes low once the machine has
 * fenced the inquiry's late answers, nothing is taken from it; ON again
 * probes again, and the probe 10s later finds a module still busy. */
TEST_CASE(imu_off_aborts_the_operation_and_lowers_key)
{
    rig_t r;
    on_with_sensor(&r, "");
    (void)rig_settle(&r, 5000u);
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    CHECK(imu_ctl_command(&r.c, &scan, r.t));
    rig_run(&r, 500u);
    CHECK(r.sim.key);
    rig_cfg(&r, false, "", 2, true);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
    /* The machine fences the inquiry's late answers before KEY goes low. */
    (void)rig_settle(&r, 40000u);
    CHECK(!r.sim.key);
    CHECK(!hc05_busy(&r.c.bt));
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_OFF);
    imu_view_t v;
    imu_ctl_view(&r.c, &v);
    CHECK_EQ(v.found_n, 0u);
    CHECK_STR_EQ(v.version, "");
    /* Commands with IMU OFF are refused. */
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));
    rig_cfg(&r, true, "", 2, true);
    (void)rig_settle(&r, 5000u);
    rig_run(&r, IMU_PROBE_RETRY_MS + 10u);
    (void)rig_settle(&r, 5000u);
    CHECK_EQ(imu_ctl_link(&r.c), IMU_LINK_READY);
}

/* What cannot run now is refused: before the probe, ZERO without a
 * reading, an unknown kind.  ZERO and UNZERO with one. */
TEST_CASE(commands_are_refused_when_they_cannot_run)
{
    rig_t r;
    rig_init(&r, 2, HC05_BAUD_DATA);
    rig_cfg(&r, true, "", 2, true);
    static const imu_cmd_kind_t k[] = { IMU_CMD_SCAN, IMU_CMD_ANY,
                                        IMU_CMD_BIND, IMU_CMD_CALIBRATE,
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
    CHECK_EQ(r.c.zero_gen, 1u);
    CHECK(imu_ctl_command(&r.c, &unzero, r.t));
    imu_ctl_view(&r.c, &v);
    CHECK(!v.r.zeroed);
    CHECK_EQ(r.c.zero_gen, 2u);
    /* IMU OFF with a zero kept is a new zero too; without one it is not. */
    CHECK(imu_ctl_command(&r.c, &zero, r.t));
    rig_cfg(&r, false, "98D3:31:F5A1B2", 2, true);
    CHECK_EQ(r.c.zero_gen, 4u);
    rig_cfg(&r, true, "98D3:31:F5A1B2", 2, true);
    rig_cfg(&r, false, "98D3:31:F5A1B2", 2, true);
    CHECK_EQ(r.c.zero_gen, 4u);
}

/* No AT operation while the calibration runs or its exit commands wait:
 * CALSW 0 and SAVE go out on time, and SCAN, ANY and BIND are refused. */
TEST_CASE(no_at_operation_while_the_calibration_or_a_sensor_command_waits)
{
    rig_t r;
    connected(&r);
    const unsigned at = r.nsent;
    const imu_cmd_t cal = { IMU_CMD_CALIBRATE, 0u };
    const imu_cmd_t scan = { IMU_CMD_SCAN, 0u };
    const imu_cmd_t any = { IMU_CMD_ANY, 0u };
    CHECK(imu_ctl_command(&r.c, &cal, r.t));
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));  /* unlock still queued */
    rig_stream(&r, 1000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.out_n, 0u);
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));  /* the calibration runs */
    CHECK(!imu_ctl_command(&r.c, &any, r.t));
    CHECK(!hc05_busy(&r.c.bt));
    rig_stream(&r, 2001u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.cal, IMU_CAL_DONE);
    CHECK(r.c.out_n > 0u);                       /* CALSW 0 and SAVE wait */
    CHECK(!imu_ctl_command(&r.c, &scan, r.t));
    rig_stream(&r, 200u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.nsent, at + 5u);
    CHECK(rig_sent_is(&r, at + 4u, WIT_REG_SAVE, 0u, 0u));
    CHECK(r.sent_at[at + 4u] - r.sent_at[at] < IMU_CAL_MS + 4u * 60u);
    CHECK(imu_ctl_command(&r.c, &scan, r.t));    /* free again */
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
        rig_cfg(&r, true, "98D3:31:F5A1B2", i % 5, true);
        rig_stream(&r, 33u, 20u, 0.0f, 0.0f, 0.0f);
        CHECK(r.c.cal != IMU_CAL_RUNNING || r.nsent <= at + 2u);
    }
    rig_stream(&r, 3000u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.cal, IMU_CAL_DONE);
    CHECK(!r.c.rate_due);
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
    CHECK_EQ(r.c.gaps, 0u);
    const uint32_t last = r.c.m.r.taken_ms;
    rig_run(&r, 400u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.gaps, 1u);
    CHECK_EQ(r.c.gap_at_ms, last + IMU_STALE_MS);
    const uint32_t t1 = r.c.m.r.taken_ms;
    rig_run(&r, IMU_STALE_MS - 2u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK(r.c.m.r.taken_ms - t1 < IMU_STALE_MS);
    CHECK_EQ(r.c.gaps, 1u);
    const uint32_t t2 = r.c.m.r.taken_ms;
    rig_run(&r, IMU_STALE_MS - 1u);
    rig_stream(&r, 1u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.m.r.taken_ms - t2, IMU_STALE_MS);
    CHECK_EQ(r.c.gaps, 2u);
    /* IMU OFF and ON: the first frame after is no gap. */
    rig_cfg(&r, false, "98D3:31:F5A1B2", 2, true);
    rig_cfg(&r, true, "98D3:31:F5A1B2", 2, true);
    (void)rig_settle(&r, 5000u);
    rig_stream(&r, 100u, 20u, 0.0f, 0.0f, 0.0f);
    CHECK_EQ(r.c.gaps, 2u);
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
    imu_ctl_t c;
    imu_ctl_init(&c);
    imu_ctl_config(&c, NULL, 0u);
    CHECK(!c.configured);
    imu_ctl_feed(NULL, (const uint8_t *)"x", 1u, 0u);
    imu_ctl_feed(&c, NULL, 1u, 0u);
    hc05_act_t a;
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
    RUN(imu_on_probes_and_a_silent_hc05_is_no_module_retried_every_10s);
    RUN(a_probe_that_answers_is_ready_without_a_sensor_and_bound_with_one);
    RUN(scan_lists_the_devices_and_bind_keeps_the_address_and_name);
    RUN(a_refused_scan_offers_any_and_any_keeps_the_any_address);
    RUN(sensor_bytes_reach_the_reading_only_outside_an_operation);
    RUN(calibrate_sends_calsw_1_then_after_3s_calsw_0_and_save);
    RUN(calibrate_with_the_frames_lost_ends_failed);
    RUN(calibrate_with_frames_300ms_apart_ends_failed);
    RUN(a_full_outbox_drops_the_command_past_its_room);
    RUN(a_rate_change_goes_to_the_sensor_once_it_is_connected);
    RUN(imu_off_aborts_the_operation_and_lowers_key);
    RUN(commands_are_refused_when_they_cannot_run);
    RUN(no_at_operation_while_the_calibration_or_a_sensor_command_waits);
    RUN(a_rate_change_in_a_calibration_waits_for_its_end);
    RUN(the_handout_gives_each_frame_once_and_again_after_a_reset);
    RUN(a_frame_after_the_stale_point_counts_a_gap);
    RUN(the_rate_options_are_the_wit_codes_and_null_does_nothing);
    return test_summary("imu_ctl");
}
