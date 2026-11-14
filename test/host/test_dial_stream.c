/*
 * The panel's stream to the M5Stack Dial (dial_stream.h).
 *
 * Under test: SCREEN first, then VALUE_META and VALUE, then LIVE at 20Hz,
 * then STATE; SCREEN again on a change and after 5,000ms, not 4,999ms;
 * STATE after 200ms without a packet, not 199ms, and at once when ARMED
 * changes; VALUE in the frame the value changed; NAV's three rows, the
 * focused one first; the whole model again after a reset; fields out of
 * range clamped so that every packet decodes; no model, no packet; SCALE
 * after VALUE on a bench face, again after a reset and on a face entered,
 * and on a change no sooner than 200ms after the last.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "dial_stream.h"

/** @brief A MOTOR face at 25.0% with a few readings. */
static dial_model_t motor_model(void)
{
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_MOTOR;
    (void)dk_text_copy(m.title, sizeof m.title, "MOTOR & ESC");
    m.armed = true;
    m.thr_tenths = 10u;
    m.srv_tenths = 10u;
    m.has_value = true;
    (void)dk_text_copy(m.meta.unit, sizeof m.meta.unit, "%");
    m.meta.decimals = 1u;
    m.meta.max = 1000;
    m.meta.step = 10;
    m.value.value = 250;
    m.has_motor = true;
    m.motor.flags = DK_LIVE_ARMED | DK_LIVE_DRIVING;
    m.motor.valid = DK_MV_VOLT;
    m.motor.thr_cpct = 2500u;
    m.motor.mv = 16800u;
    return m;
}

/** @brief The next packet's kind at @p now, 0 for none; decoded. */
static uint8_t next(dial_stream_t *st, const dial_model_t *m, uint32_t now)
{
    uint8_t pkt[DK_PKT_MAX];
    const size_t n = dial_stream_next(st, m, now, false, pkt);
    if (n == 0u) {
        return 0u;
    }
    dk_panel_pkt_t p;
    if (!dk_panel_decode(pkt, n, &p)) {
        T_FAIL("kind 0x%02X of %u bytes does not decode", pkt[0],
               (unsigned)n);
    }
    return pkt[0];
}

TEST_CASE(the_order_is_screen_value_live_state)
{
    dial_stream_t st;
    dial_stream_init(&st);
    const dial_model_t m = motor_model();
    CHECK_EQ(next(&st, &m, 0), DK_K_SCREEN);
    CHECK_EQ(next(&st, &m, 0), DK_K_VALUE_META);
    CHECK_EQ(next(&st, &m, 0), DK_K_VALUE);
    CHECK_EQ(next(&st, &m, 0), DK_K_LIVE_MOTOR);
    CHECK_EQ(next(&st, &m, 0), DK_K_STATE);
    CHECK_EQ(next(&st, &m, 1), 0u);
    CHECK_EQ(st.sent[0], 1u);
    CHECK_EQ(st.seq, 5u);
}

TEST_CASE(live_goes_at_20hz)
{
    dial_stream_t st;
    dial_stream_init(&st);
    const dial_model_t m = motor_model();
    while (next(&st, &m, 0) != 0u) { }
    CHECK_EQ(next(&st, &m, 49), 0u);
    CHECK_EQ(next(&st, &m, 50), DK_K_LIVE_MOTOR);
    unsigned live = 0u;
    for (uint32_t t = 51; t <= 1050u; ++t) {
        while (next(&st, &m, t) == DK_K_LIVE_MOTOR) {
            ++live;
        }
    }
    CHECK_EQ(live, 20u);
}

TEST_CASE(screen_repeats_at_5000ms_and_on_a_change)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_OTHER;
    m.thr_tenths = m.srv_tenths = 10u;
    (void)dk_text_copy(m.title, sizeof m.title, "SETUP");
    CHECK_EQ(next(&st, &m, 0), DK_K_SCREEN);
    CHECK_EQ(next(&st, &m, 0), DK_K_STATE);
    /* STATE fills a gap of 200ms, not 199ms. */
    CHECK_EQ(next(&st, &m, 199), 0u);
    CHECK_EQ(next(&st, &m, 200), DK_K_STATE);
    for (uint32_t t = 201; t < 5000u; ++t) {
        if (next(&st, &m, t) == DK_K_SCREEN) {
            T_FAIL("SCREEN at %u", (unsigned)t);
            break;
        }
    }
    CHECK_EQ(next(&st, &m, 5000), DK_K_SCREEN);
    (void)dk_text_copy(m.title, sizeof m.title, "LOGS");
    CHECK_EQ(next(&st, &m, 5001), DK_K_SCREEN);
    m.lang = DK_LANG_DE;
    CHECK_EQ(next(&st, &m, 5002), DK_K_SCREEN);
    /* ARMED goes at once in STATE. */
    m.armed = true;
    CHECK_EQ(next(&st, &m, 5003), DK_K_STATE);
    CHECK_EQ(next(&st, &m, 5004), 0u);
}

TEST_CASE(value_goes_in_the_frame_it_changed)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m = motor_model();
    while (next(&st, &m, 0) != 0u) { }
    m.value.value = 260;
    CHECK_EQ(next(&st, &m, 16), DK_K_VALUE);
    CHECK_EQ(next(&st, &m, 16), 0u);
    /* A new range or step is VALUE_META, then VALUE again. */
    m.meta.step = 50;
    CHECK_EQ(next(&st, &m, 17), DK_K_VALUE_META);
    CHECK_EQ(next(&st, &m, 17), DK_K_VALUE);
}

TEST_CASE(nav_sends_three_rows_the_focused_first)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_NAV;
    m.thr_tenths = m.srv_tenths = 10u;
    m.has_nav = true;
    for (unsigned k = 0; k < 3u; ++k) {
        m.nav[k].index = 1u;
        m.nav[k].count = 3u;
    }
    (void)dk_text_copy(m.nav[0].item, sizeof m.nav[0].item, "PANEL");
    (void)dk_text_copy(m.nav[1].item, sizeof m.nav[1].item, "SENSORS");
    (void)dk_text_copy(m.nav[2].item, sizeof m.nav[2].item, "OUTPUTS");
    CHECK_EQ(next(&st, &m, 0), DK_K_SCREEN);
    uint8_t pkt[DK_PKT_MAX];
    dk_nav_t n;
    CHECK_EQ(dial_stream_next(&st, &m, 0, false, pkt), DK_LEN_NAV);
    CHECK(dk_nav_decode(pkt, DK_LEN_NAV, &n));
    CHECK_EQ(n.slot, DK_SLOT_FOCUS);
    CHECK_STR_EQ(n.item, "SENSORS");
    CHECK_EQ(next(&st, &m, 0), DK_K_NAV);
    CHECK_EQ(next(&st, &m, 0), DK_K_NAV);
    CHECK_EQ(next(&st, &m, 0), DK_K_STATE);
    CHECK_EQ(next(&st, &m, 1), 0u);
}

TEST_CASE(a_reset_sends_the_whole_model_again)
{
    dial_stream_t st;
    dial_stream_init(&st);
    const dial_model_t m = motor_model();
    while (next(&st, &m, 0) != 0u) { }
    dial_stream_reset(&st);
    CHECK_EQ(next(&st, &m, 10), DK_K_SCREEN);
    CHECK_EQ(next(&st, &m, 10), DK_K_VALUE_META);
    CHECK_EQ(next(&st, &m, 10), DK_K_VALUE);
    CHECK_EQ(next(&st, &m, 10), DK_K_LIVE_MOTOR);
    CHECK_EQ(next(&st, &m, 10), DK_K_STATE);
    CHECK_EQ(st.sent[0], 2u);
}

TEST_CASE(fields_out_of_range_are_clamped_and_decode)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    m.face = DK_FACE_SERVO;
    m.thr_tenths = 0u;          /* below 1 */
    m.srv_tenths = 200u;        /* above 100 */
    m.has_servo = true;
    m.servo.cmd_us = 3000u;
    m.servo.cmd_ddeg = 2000;
    m.servo.meas_cdeg = 5;      /* without its flag: 0 */
    m.servo.fb_us = 1500u;      /* without its flag: 0 */
    m.has_value = true;
    m.meta.min = 10;
    m.meta.max = -10;
    m.meta.step = 0;
    m.meta.decimals = 9u;
    unsigned n = 0u;
    for (uint32_t t = 0; t < 100u; ++t) {
        while (next(&st, &m, t) != 0u) {
            ++n;
        }
    }
    CHECK(n >= 5u);
    CHECK(st.clamped > 0u);
    dial_model_t c = m;
    CHECK(dial_model_clamp(&c));
    CHECK_EQ(c.servo.cmd_us, DK_US_MAX);
    CHECK_EQ(c.servo.cmd_ddeg, DK_DDEG_MAX);
    CHECK_EQ(c.servo.meas_cdeg, 0);
    CHECK_EQ(c.thr_tenths, DK_STEP_MIN);
    CHECK_EQ(c.srv_tenths, DK_STEP_MAX);
    CHECK(!dial_model_clamp(&c));
    CHECK(!dial_model_clamp(NULL));
}

TEST_CASE(no_model_no_packet)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m;
    memset(&m, 0, sizeof m);
    uint8_t pkt[DK_PKT_MAX];
    CHECK_EQ(dial_stream_next(&st, &m, 0, false, pkt), 0u);
    CHECK_EQ(dial_stream_next(NULL, &m, 0, false, pkt), 0u);
    CHECK_EQ(dial_stream_next(&st, NULL, 0, false, pkt), 0u);
    /* DEGRADED rides in STATE. */
    m.face = DK_FACE_HOME;
    m.thr_tenths = m.srv_tenths = 10u;
    (void)dial_stream_next(&st, &m, 0, false, pkt);
    CHECK_EQ(dial_stream_next(&st, &m, 0, true, pkt), DK_LEN_STATE);
    dk_state_t s;
    CHECK(dk_state_decode(pkt, DK_LEN_STATE, &s));
    CHECK_EQ(s.flags, DK_STA_DEGRADED);
}

/** @brief The model of motor_model() with its full scales. */
static dial_model_t scaled_model(void)
{
    dial_model_t m = motor_model();
    m.has_scale = true;
    m.scale.rpm = 12000u;
    m.scale.mv = 25200u;
    m.scale.ca = 4000u;
    m.scale.travel = 450u;
    m.scale.us_min = 1000u;
    m.scale.us_max = 2000u;
    return m;
}

TEST_CASE(scale_goes_after_value_and_again_on_reset_and_entry)
{
    dial_stream_t st;
    dial_stream_init(&st);
    dial_model_t m = scaled_model();
    CHECK_EQ(next(&st, &m, 0), DK_K_SCREEN);
    CHECK_EQ(next(&st, &m, 0), DK_K_VALUE_META);
    CHECK_EQ(next(&st, &m, 0), DK_K_VALUE);
    uint8_t pkt[DK_PKT_MAX];
    CHECK_EQ(dial_stream_next(&st, &m, 0, false, pkt), DK_LEN_SCALE);
    dk_scale_t c;
    CHECK(dk_scale_decode(pkt, DK_LEN_SCALE, &c));
    CHECK_EQ(c.rpm, 12000u);
    CHECK_EQ(c.mv, 25200u);
    CHECK_EQ(c.us_max, 2000u);
    CHECK_EQ(next(&st, &m, 0), DK_K_LIVE_MOTOR);
    CHECK_EQ(next(&st, &m, 0), DK_K_STATE);
    CHECK_EQ(next(&st, &m, 1), 0u);
    CHECK_EQ(st.sent[DK_K_SCALE - DK_K_SCREEN], 1u);
    /* A reconnect: the whole model again, SCALE in it. */
    dial_stream_reset(&st);
    unsigned scales = 0u;
    for (uint8_t k = next(&st, &m, 10); k != 0u; k = next(&st, &m, 10)) {
        scales += (k == DK_K_SCALE) ? 1u : 0u;
    }
    CHECK_EQ(scales, 1u);
    /* A peak that climbs: once at 210ms, not before, the newest then. */
    m.scale.rpm = 12100u;
    CHECK(next(&st, &m, 209) != DK_K_SCALE);
    m.scale.rpm = 12200u;
    while (next(&st, &m, 209) != 0u) { }
    CHECK_EQ(dial_stream_next(&st, &m, 210, false, pkt), DK_LEN_SCALE);
    CHECK(dk_scale_decode(pkt, DK_LEN_SCALE, &c));
    CHECK_EQ(c.rpm, 12200u);
    /* Unchanged: none, 5,000ms on. */
    for (uint32_t t = 211; t < 5000u; t += 7u) {
        if (next(&st, &m, t) == DK_K_SCALE) {
            T_FAIL("SCALE at %u unchanged", (unsigned)t);
            break;
        }
    }
    /* Another face and back: SCALE on the entry, at once. */
    m.face = DK_FACE_SERVO;
    m.has_motor = false;
    CHECK_EQ(next(&st, &m, 5001), DK_K_SCREEN);
    CHECK_EQ(next(&st, &m, 5001), DK_K_VALUE_META);
    CHECK_EQ(next(&st, &m, 5001), DK_K_VALUE);
    CHECK_EQ(next(&st, &m, 5001), DK_K_SCALE);
    /* No full scales off a bench face. */
    m.face = DK_FACE_OTHER;
    m.has_value = false;
    m.has_scale = false;
    CHECK_EQ(next(&st, &m, 5002), DK_K_SCREEN);
    for (uint32_t t = 5002; t < 6000u; ++t) {
        if (next(&st, &m, t) == DK_K_SCALE) {
            T_FAIL("SCALE on OTHER at %u", (unsigned)t);
            break;
        }
    }
}

TEST_CASE(a_scale_out_of_range_is_clamped_and_decodes)
{
    dial_model_t m = scaled_model();
    m.scale.travel = 5000u;
    m.scale.us_min = 2100u;     /* above the other end */
    m.scale.us_max = 900u;
    CHECK(dial_model_clamp(&m));
    CHECK_EQ(m.scale.travel, (uint16_t)DK_DDEG_MAX);
    CHECK_EQ(m.scale.us_min, 0u);
    CHECK_EQ(m.scale.us_max, 0u);
    m = scaled_model();
    m.scale.us_min = 300u;      /* below DK_US_MIN */
    CHECK(dial_model_clamp(&m));
    CHECK_EQ(m.scale.us_max, 0u);
    m = scaled_model();
    CHECK(!dial_model_clamp(&m));
    dial_stream_t st;
    dial_stream_init(&st);
    m.scale.us_max = 3000u;
    unsigned n = 0u;
    for (uint8_t k = next(&st, &m, 0); k != 0u; k = next(&st, &m, 0)) {
        n += (k == DK_K_SCALE) ? 1u : 0u;
    }
    CHECK_EQ(n, 1u);
}

int main(void)
{
    RUN(the_order_is_screen_value_live_state);
    RUN(live_goes_at_20hz);
    RUN(screen_repeats_at_5000ms_and_on_a_change);
    RUN(value_goes_in_the_frame_it_changed);
    RUN(nav_sends_three_rows_the_focused_first);
    RUN(a_reset_sends_the_whole_model_again);
    RUN(fields_out_of_range_are_clamped_and_decode);
    RUN(no_model_no_packet);
    RUN(scale_goes_after_value_and_again_on_reset_and_entry);
    RUN(a_scale_out_of_range_is_clamped_and_decodes);
    return test_summary("dial_stream");
}
