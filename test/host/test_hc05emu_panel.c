/*
 * The panel's own HC-05 machine (protocols/hc05) against the HC-05
 * emulator (protocols/hc05emu) and a scripted radio (hc05emu_rig.h): the
 * bridge needs no change on the panel.
 *
 * Under test: PROBE on a fresh bridge and on one started in full AT mode;
 * SCAN with names read, names from the inquiry, a failed and a silent
 * name, no device, and the INQM count; BIND with the right PIN, a wrong
 * one and a silent device, and the data mode it ends in; ANY and its
 * search by RSSI; a restart from the stored setting; the retry after a
 * loss and after a connect that never ends; KEY raised over an open link;
 * an abort during the inquiry and the operation after it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05emu.h"
#include "hc05emu_rig.h"

static const uint8_t k_phone[6] = { 0x00, 0x1A, 0x7D, 0xDA, 0x71, 0x13 };
static const uint8_t k_other[6] = { 0x20, 0x16, 0x05, 0x12, 0x34, 0x56 };

TEST_CASE(a_probe_finds_a_fresh_bridge_at_115200_and_reads_its_version)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    CHECK(hc05_probe(&b.h, 0));
    const uint32_t end = bench_run(&b, 0, 10000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.h.baud, HC05_BAUD_DATA);
    CHECK_STR_EQ(b.h.version, "btbridge-test");
    CHECK_EQ(b.junk, 0u);
    /* KEY settles 100ms, then two exchanges of 2ms each. */
    CHECK(end < 120u);
    CHECK_EQ(b.e.mode, HC05EMU_MODE_DATA);
}

TEST_CASE(a_probe_finds_a_bridge_started_with_key_high_at_38400)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    hc05emu_init(&b.e, NULL, "btbridge-test", true, 0);
    b.h.key = true;
    CHECK(hc05_probe(&b.h, 0));
    const uint32_t end = bench_run(&b, 0, 10000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.h.baud, HC05_BAUD_ALT);
    /* AT at 115200 reached it as junk, and the panel's one retry after the
     * ERROR:(0) the junk caused found it. */
    CHECK(b.junk >= 1u);
    CHECK_EQ(bench_sent(&b, "AT"), 3u);
    /* KEY low: data mode at the stored baud, set on the next ms. */
    bench_idle(&b, end + 1u, 1u);
    CHECK_EQ(b.e.mode, HC05EMU_MODE_DATA);
    CHECK_EQ(b.r.baud, HC05EMU_BAUD_DEFAULT);
}

TEST_CASE(a_scan_lists_the_devices_with_names_classes_and_rssi)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    radio_add(&b.r, k_sensor, -64, "HC-06", 3000u);
    rdev_t *phone = radio_add(&b.r, k_phone, -80, "PHONE", 5000u);
    phone->cod = 0x5A020Cu;
    phone->name_in_inq = true;
    CHECK(hc05_scan(&b.h, 0));
    (void)bench_run(&b, 0, 30000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.h.found_n, 2u);
    CHECK_STR_EQ(b.h.found[0].addr, SENSOR);
    CHECK_STR_EQ(b.h.found[0].name, "HC-06");
    CHECK_EQ(b.h.found[0].rssi, -64);
    CHECK_EQ(b.h.found[0].dev_class, 0x1F00u);
    CHECK_STR_EQ(b.h.found[1].addr, "1A:7D:DA7113");
    CHECK_STR_EQ(b.h.found[1].name, "PHONE");
    CHECK_EQ(b.h.found[1].rssi, -80);
    CHECK_EQ(b.h.found[1].dev_class, 0x020Cu);
    /* The phone's name came with the inquiry: one read, the sensor's. */
    CHECK_EQ(b.r.names, 1u);
    CHECK_EQ(b.r.last_units, HC05_INQ_UNITS);
    CHECK_EQ(b.r.last_max, HC05_INQ_MAX);
    CHECK_EQ(bench_sent(&b, "AT+STATE?"), 0u);
}

TEST_CASE(a_scan_with_no_device_in_range_ends_none_found)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    CHECK(hc05_scan(&b.h, 0));
    (void)bench_run(&b, 0, 30000u);
    CHECK_EQ(b.h.state, HC05_ST_FAILED);
    CHECK_EQ(b.h.err, HC05_ERR_NONE_FOUND);
}

TEST_CASE(a_name_that_fails_or_never_comes_leaves_it_empty_without_a_fence)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    radio_add(&b.r, k_phone, -50, NULL, 1000u);
    rdev_t *s = radio_add(&b.r, k_sensor, -60, "HC-06", 2000u);
    s->name_silent = true;
    radio_add(&b.r, k_other, -70, "OTHER", 3000u);
    CHECK(hc05_scan(&b.h, 0));
    (void)bench_run(&b, 0, 40000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.h.found_n, 3u);
    CHECK_STR_EQ(b.h.found[0].name, "");
    CHECK_STR_EQ(b.h.found[1].name, "");
    CHECK_STR_EQ(b.h.found[2].name, "OTHER");
    /* The emulator's FAIL at 2500ms beat the panel's 3000ms wait. */
    CHECK_EQ(bench_sent(&b, "AT+STATE?"), 0u);
}

TEST_CASE(a_scan_ends_at_the_device_count_it_asked_for)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    static uint8_t addrs[10][6];
    for (unsigned i = 0; i < 10u; ++i) {
        memcpy(addrs[i], k_other, 6);
        addrs[i][5] = (uint8_t)i;
        rdev_t *d = radio_add(&b.r, addrs[i], -70, "X", 100u + 100u * i);
        d->name_in_inq = true;
    }
    CHECK(hc05_scan(&b.h, 0));
    (void)bench_run(&b, 0, 40000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.h.found_n, HC05_FOUND_MAX);
    /* The ninth report ended the inquiry long before its 11.52s. */
    CHECK(b.r.stops >= 1u);
    CHECK_EQ(b.e.inq_told, HC05_INQ_MAX);
}

/* BIND the sensor on a fresh bridge; @return the time the run ended. */
static uint32_t bind_sensor(bench_t *b, const char *sensor_pin)
{
    bench_init(b, NULL, 0);
    rdev_t *s = radio_add(&b->r, k_sensor, -60, "HC-06", 2000u);
    s->pin = sensor_pin;
    CHECK(hc05_bind(&b->h, SENSOR, "1234", 0));
    return bench_run(b, 0, 40000u);
}

TEST_CASE(bind_sets_the_bridge_to_the_sensor_and_it_passes_bytes_in_data_mode)
{
    static bench_t b;
    const uint32_t end = bind_sensor(&b, "1234");
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.r.pairs, 1u);
    CHECK_EQ(b.r.nvs.bound, 1u);
    CHECK(memcmp(b.r.nvs.bind, k_sensor, 6) == 0);
    CHECK_EQ(b.r.nvs.cmode, 0u);
    CHECK_EQ(b.r.nvs.role, 1u);
    CHECK_STR_EQ(b.r.nvs.pin, "1234");
    CHECK_EQ(b.r.nvs.baud, 115200u);
    /* The restart came with KEY low: data mode, a connect to the sensor. */
    CHECK_EQ(b.e.mode, HC05EMU_MODE_DATA);
    CHECK_EQ(b.r.connects, 1u);
    bench_idle(&b, end + 1u, b.r.conn_ms + 10u);
    CHECK(hc05emu_forwarding(&b.e));
    CHECK(hc05emu_connected(&b.e));
    const uint32_t t = end + b.r.conn_ms + 20u;
    static const uint8_t frame[11] = { 0x55, 0x53, 1, 2, 3, 4, 5, 6, 7, 8,
                                       0xA0 };
    bench_sensor_says(&b, frame, sizeof frame, t);
    static const uint8_t cmd[5] = { 0xFF, 0xAA, 0x69, 0x88, 0xB5 };
    pipe_put(&b.to_emu, t + 1u, b.panel_baud, cmd, sizeof cmd);
    bench_idle(&b, t, 5u);
    CHECK_EQ(b.panel_data_n, sizeof frame);
    CHECK(memcmp(b.panel_data, frame, sizeof frame) == 0);
    CHECK_EQ(b.r.to_sensor_n, sizeof cmd);
    CHECK(memcmp(b.r.to_sensor, cmd, sizeof cmd) == 0);
}

TEST_CASE(bind_with_a_wrong_pin_fails_pair_and_binds_nothing)
{
    static bench_t b;
    (void)bind_sensor(&b, "0000");
    CHECK_EQ(b.h.state, HC05_ST_FAILED);
    CHECK_EQ(b.h.err, HC05_ERR_PAIR);
    CHECK_EQ(b.e.cfg.bound, 0u);
    CHECK_EQ(b.r.nvs.bound, 0u);
    CHECK_EQ(bench_sent(&b, "AT+BIND=" SENSOR_CMD), 0u);
}

TEST_CASE(bind_to_a_silent_device_fails_at_its_pairing_time)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    rdev_t *s = radio_add(&b.r, k_sensor, -60, "HC-06", 2000u);
    s->silent = true;
    CHECK(hc05_bind(&b.h, SENSOR, "1234", 0));
    const uint32_t end = bench_run(&b, 0, 40000u);
    CHECK_EQ(b.h.state, HC05_ST_FAILED);
    CHECK_EQ(b.h.err, HC05_ERR_PAIR);
    /* KEY settle, four exchanges, then the 20s the command named. */
    CHECK(end >= 20000u && end < 20200u);
    CHECK(b.r.stops >= 1u);
}

TEST_CASE(any_connects_to_the_strongest_device_that_takes_the_pin)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    rdev_t *phone = radio_add(&b.r, k_phone, -40, "PHONE", 1000u);
    phone->spp = false;
    rdev_t *other = radio_add(&b.r, k_other, -70, "OTHER", 2000u);
    other->pin = "0000";
    radio_add(&b.r, k_sensor, -60, "HC-06", 3000u);
    CHECK(hc05_any(&b.h, "1234", 0));
    const uint32_t end = bench_run(&b, 0, 40000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_EQ(b.r.nvs.cmode, 1u);
    CHECK_EQ(b.e.link, HC05EMU_LINK_SEARCH);
    CHECK_EQ(b.r.last_units, 8u);
    bench_idle(&b, end + 1u, 10240u + 4000u + 1500u + 100u);
    /* By RSSI: the phone (-40) refused, the sensor (-60) took it; the
     * device with the other PIN (-70) was never tried. */
    CHECK_EQ(b.r.connects, 2u);
    CHECK(hc05emu_forwarding(&b.e));
    CHECK_EQ(b.r.link_dev, 2);
}

TEST_CASE(a_restart_from_the_stored_setting_connects_at_once)
{
    static bench_t b;
    hc05emu_cfg_t c;
    hc05emu_cfg_default(&c);
    c.bound = 1u;
    memcpy(c.bind, k_sensor, 6);
    bench_init(&b, &c, 0);
    radio_add(&b.r, k_sensor, -60, "HC-06", 2000u);
    bench_idle(&b, 0, 2u);
    CHECK_EQ(b.r.connects, 1u);
    CHECK_EQ(b.r.baud, 115200u);
    bench_idle(&b, 2u, b.r.conn_ms + 2u);
    CHECK(hc05emu_forwarding(&b.e));
    /* And the panel's probe at start-up finds it there. */
    CHECK(hc05_probe(&b.h, 2000u));
    (void)bench_run(&b, 2000u, 10000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK(hc05emu_forwarding(&b.e));
}

TEST_CASE(key_raised_over_an_open_link_keeps_it_and_holds_the_bytes_back)
{
    static bench_t b;
    hc05emu_cfg_t c;
    hc05emu_cfg_default(&c);
    c.bound = 1u;
    memcpy(c.bind, k_sensor, 6);
    bench_init(&b, &c, 0);
    radio_add(&b.r, k_sensor, -60, "HC-06", 2000u);
    bench_idle(&b, 0, 2000u);
    CHECK(hc05emu_forwarding(&b.e));
    CHECK(hc05_probe(&b.h, 2000u));
    bench_idle(&b, 2000u, 50u);
    CHECK(!hc05emu_forwarding(&b.e));
    CHECK(hc05emu_connected(&b.e));
    CHECK(hc05emu_at_mode(&b.e));
    (void)bench_run(&b, 2050u, 10000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK(hc05emu_forwarding(&b.e));
    CHECK_EQ(b.r.connects, 1u);
    CHECK_EQ(b.r.stops, 0u);
}

TEST_CASE(an_abort_in_the_inquiry_ends_after_the_fence_and_the_next_probe_works)
{
    static bench_t b;
    bench_init(&b, NULL, 0);
    radio_add(&b.r, k_sensor, -60, "HC-06", 3000u);
    CHECK(hc05_scan(&b.h, 0));
    bench_idle(&b, 0, 2000u);
    CHECK_EQ(b.e.op, HC05EMU_OP_INQ);
    hc05_abort(&b.h);
    const uint32_t end = bench_run(&b, 2000u, 30000u);
    CHECK_EQ(b.h.state, HC05_ST_FAILED);
    CHECK_EQ(b.h.err, HC05_ERR_NONE);
    /* The fence's AT+STATE? waited in the emulator for the inquiry's end
     * at 11.52s after AT+INQ. */
    CHECK_EQ(bench_sent(&b, "AT+STATE?"), 1u);
    CHECK(2000u + end > 11500u);
    CHECK(hc05_probe(&b.h, 2001u + end));
    (void)bench_run(&b, 2001u + end, 10000u);
    CHECK_EQ(b.h.state, HC05_ST_DONE);
    CHECK_STR_EQ(b.h.version, "btbridge-test");
}

int main(void)
{
    RUN(a_probe_finds_a_fresh_bridge_at_115200_and_reads_its_version);
    RUN(a_probe_finds_a_bridge_started_with_key_high_at_38400);
    RUN(a_scan_lists_the_devices_with_names_classes_and_rssi);
    RUN(a_scan_with_no_device_in_range_ends_none_found);
    RUN(a_name_that_fails_or_never_comes_leaves_it_empty_without_a_fence);
    RUN(a_scan_ends_at_the_device_count_it_asked_for);
    RUN(bind_sets_the_bridge_to_the_sensor_and_it_passes_bytes_in_data_mode);
    RUN(bind_with_a_wrong_pin_fails_pair_and_binds_nothing);
    RUN(bind_to_a_silent_device_fails_at_its_pairing_time);
    RUN(any_connects_to_the_strongest_device_that_takes_the_pin);
    RUN(a_restart_from_the_stored_setting_connects_at_once);
    RUN(key_raised_over_an_open_link_keeps_it_and_holds_the_bytes_back);
    RUN(an_abort_in_the_inquiry_ends_after_the_fence_and_the_next_probe_works);
    return test_summary("hc05emu_panel");
}
