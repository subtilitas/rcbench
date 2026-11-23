/*
 * The PASS and PASS_STAT pages of the remote servo node's IOmcu
 * (shared/outputs/pass_page.c, protocol 4.41).
 *
 * Under test: BENCH on every channel at the start, and FLAGS without the
 * capture; SOURCE taken while SAFE and a change refused while ARMED, the
 * SOURCEs in force taken while ARMED; values above BENCH, the read-only
 * registers and a range off the page refused, a refused write changing
 * nothing; OFF and PASS-THROUGH refused on a pin held elsewhere, BENCH
 * taken there; PASS-THROUGH refused without the capture, OFF taken; the
 * pads MIRROR drives (PASS-THROUGH while ARMED only) and the pins the page
 * holds; each channel mirrored only while the bank is armed for its own
 * role: surfaces alone, throttles alone, both, a role disarmed mid-run and
 * a channel's role changed while it passes; windows closing at 50ms and
 * not 49ms, their number's wrap at 65536, a late window restarting at
 * its close; FLAGS of the window a lap or a session fell in and not of
 * the next; IN_US, OUT_US, LOST and
 * GLITCH published; PASS_STAT's MISSED saturating at 65535 and FRAMES
 * modulo 65536, RESET taking the counts as zero at once, 0 doing nothing,
 * 2 refused, and its other registers read only; PASS-THROUGH counted as
 * an active channel for a session on the common rail, and the channels a
 * session drops switched to OFF except a BENCH channel a slot holds; the
 * session flag's source set by KST and Hitec on a programming port and by
 * ESC wire on a programming port and on an output slot, from the session's
 * first frame until its driver gives the pin back; a programming port's
 * pin (a prebound SERVO PWM port), a slot's and one held elsewhere busy,
 * OFF and PASS-THROUGH refused there until the port gives it back; every
 * route setting each held pad again after another driver touched it
 * (a port's take and give), BENCH pads untouched.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "link_pages_esc.h"
#include "pass_decode.h"
#include "pass_page.h"
#include "prog_port.h"
#include "rsn_pins.h"

static pass_page_t p;
static pass_dec_t  d;

static const pass_ctx_t SAFE  = { false, 0u };

static outputs_t bank;

/* The bank armed for @p roles (OUT_ROLE_BIT()s, 0 disarmed); channels 0
 * to 3 throttles, the rest surfaces unless a case changes them. */
static const outputs_t *armed_for(uint8_t roles)
{
    static bool set_up;
    if (!set_up) {
        outputs_init(&bank, 0u);
        for (uint8_t ch = 0; ch < OUT_MAX_CHANNELS; ++ch) {
            (void)outputs_set_role(&bank, ch, ch < 4u ? OUT_ROLE_THROTTLE
                                                     : OUT_ROLE_SURFACE);
        }
        set_up = true;
    }
    outputs_arm_roles(&bank, roles, 0u);
    return &bank;
}
static const pass_ctx_t ARMED = { true, 0u };

static uint8_t src(unsigned k, uint16_t v, const pass_ctx_t *c)
{
    return pass_page_write(&p, (uint8_t)LINK_PA_SOURCE(k), 1u, &v, c);
}

static uint16_t reg(unsigned r)
{
    uint16_t v = 0xDEADu;
    pass_page_read(&p, (uint8_t)r, 1u, &v);
    return v;
}

static uint16_t stat(unsigned r)
{
    uint16_t v = 0xDEADu;
    pass_stat_read(&p, (uint8_t)r, 1u, &v);
    return v;
}

TEST_CASE(every_channel_starts_on_bench)
{
    pass_page_init(&p, true, 0u);
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        CHECK_EQ(reg(LINK_PA_SOURCE(k)), LINK_PA_SRC_BENCH);
        CHECK_EQ(reg(LINK_PA_IN_US(k)), 0u);
    }
    CHECK_EQ(reg(LINK_PA_WINDOW), 0u);
    CHECK_EQ(reg(LINK_PA_FLAGS), 0u);
    CHECK_EQ(pass_page_held(&p), 0u);
    CHECK_EQ(pass_page_mirror(&p, armed_for(OUT_ROLES_ALL)), 0u);
    pass_page_init(&p, false, 0u);
    CHECK_EQ(reg(LINK_PA_FLAGS), LINK_PA_F_NO_CAPTURE);
}

TEST_CASE(source_is_written_while_safe_and_a_change_refused_while_armed)
{
    pass_page_init(&p, true, 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(8, LINK_PA_SRC_OFF, &SAFE), 0u);
    CHECK_EQ(reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_PASS);
    CHECK_EQ(reg(LINK_PA_SOURCE(8)), LINK_PA_SRC_OFF);
    CHECK_EQ(src(0, LINK_PA_SRC_BENCH, &ARMED), LINK_NACK_BUSY);
    CHECK_EQ(src(1, LINK_PA_SRC_PASS, &ARMED), LINK_NACK_BUSY);
    CHECK_EQ(reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_PASS);
    CHECK_EQ(reg(LINK_PA_SOURCE(1)), LINK_PA_SRC_BENCH);
    /* The SOURCEs in force, all 9 in one frame, are taken while ARMED. */
    uint16_t all[LINK_PA_CHANNELS];
    pass_page_read(&p, 0u, LINK_PA_CHANNELS, all);
    CHECK_EQ(pass_page_write(&p, 0u, LINK_PA_CHANNELS, all, &ARMED), 0u);
}

TEST_CASE(bad_values_read_only_registers_and_ranges_are_refused_whole)
{
    pass_page_init(&p, true, 0u);
    const uint16_t v[3] = { LINK_PA_SRC_PASS, 3u, LINK_PA_SRC_OFF };
    CHECK_EQ(pass_page_write(&p, 0u, 3u, v, &SAFE), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_BENCH);
    CHECK_EQ(reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_BENCH);
    for (unsigned r = LINK_PA_CHANNELS; r < LINK_PA_COUNT; ++r) {
        const uint16_t z = 0u;
        CHECK_EQ(pass_page_write(&p, (uint8_t)r, 1u, &z, &SAFE),
                 LINK_NACK_READ_ONLY);
    }
    /* SOURCE 8 and IN_US 0 together: read only, and SOURCE 8 unchanged. */
    const uint16_t two[2] = { LINK_PA_SRC_OFF, 0u };
    CHECK_EQ(pass_page_write(&p, 8u, 2u, two, &SAFE), LINK_NACK_READ_ONLY);
    CHECK_EQ(reg(LINK_PA_SOURCE(8)), LINK_PA_SRC_BENCH);
    const uint16_t z = 0u;
    CHECK_EQ(pass_page_write(&p, (uint8_t)LINK_PA_COUNT, 1u, &z, &SAFE),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(pass_page_write(&p, 0u, 0u, NULL, &SAFE), 0u);
}

TEST_CASE(off_and_pass_through_are_refused_on_a_pin_held_elsewhere)
{
    pass_page_init(&p, true, 0u);
    const pass_ctx_t held = { false, (uint16_t)(1u << 3) };
    CHECK_EQ(src(3, LINK_PA_SRC_PASS, &held), LINK_NACK_BUSY);
    CHECK_EQ(src(3, LINK_PA_SRC_OFF, &held), LINK_NACK_BUSY);
    CHECK_EQ(src(3, LINK_PA_SRC_BENCH, &held), 0u);
    CHECK_EQ(src(4, LINK_PA_SRC_PASS, &held), 0u);
    CHECK_EQ(pass_page_held(&p), 1u << 4);
    /* A channel the page holds already moves between OFF and PASS-THROUGH. */
    const pass_ctx_t also = { false, (uint16_t)(1u << 4) };
    CHECK_EQ(src(4, LINK_PA_SRC_OFF, &also), 0u);
}

TEST_CASE(pass_through_wants_the_capture_and_off_does_not)
{
    pass_page_init(&p, false, 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), LINK_NACK_BAD_VALUE);
    CHECK_EQ(src(0, LINK_PA_SRC_OFF, &SAFE), 0u);
    CHECK_EQ(pass_page_mirror(&p, armed_for(OUT_ROLES_ALL)), 0u);
    CHECK_EQ(pass_page_held(&p), 1u);
}

TEST_CASE(mirror_drives_pass_through_while_armed_only)
{
    pass_page_init(&p, true, 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(5, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(8, LINK_PA_SRC_OFF, &SAFE), 0u);
    CHECK_EQ(pass_page_mirror(&p, armed_for(OUT_ROLES_ALL)), (1u << 0) | (1u << 5));
    CHECK_EQ(pass_page_mirror(&p, armed_for(0u)), 0u);
    CHECK_EQ(pass_page_mirror(&p, NULL), 0u);
    CHECK_EQ(pass_page_held(&p), (1u << 0) | (1u << 5) | (1u << 8));
}

TEST_CASE(each_channel_passes_only_while_its_own_role_is_armed)
{
    const uint8_t thr = OUT_ROLE_BIT(OUT_ROLE_THROTTLE);
    const uint8_t srf = OUT_ROLE_BIT(OUT_ROLE_SURFACE);
    pass_page_init(&p, true, 0u);
    /* Channel 1 a throttle, channel 6 a surface, both passing. */
    CHECK_EQ(src(1, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(6, LINK_PA_SRC_PASS, &SAFE), 0u);
    /* Surfaces alone: no receiver throttle pulse reaches the ESC. */
    CHECK_EQ(pass_page_mirror(&p, armed_for(srf)), 1u << 6);
    /* Throttles alone: the reverse. */
    CHECK_EQ(pass_page_mirror(&p, armed_for(thr)), 1u << 1);
    /* Both. */
    CHECK_EQ(pass_page_mirror(&p, armed_for(thr | srf)), (1u << 1) | (1u << 6));
    /* A role disarmed mid-run: its channel out at the next call, the
     * other kept. */
    outputs_arm_roles(&bank, srf, 1u);
    CHECK_EQ(pass_page_mirror(&p, &bank), 1u << 6);
    outputs_arm_roles(&bank, 0u, 2u);
    CHECK_EQ(pass_page_mirror(&p, &bank), 0u);
    /* A channel whose role changes while passing follows its new role. */
    (void)armed_for(srf);
    CHECK_EQ(pass_page_mirror(&p, &bank), 1u << 6);
    CHECK(outputs_set_role(&bank, 6u, OUT_ROLE_THROTTLE));
    CHECK_EQ(pass_page_mirror(&p, &bank), 0u);
    CHECK(outputs_set_role(&bank, 1u, OUT_ROLE_SURFACE));
    CHECK_EQ(pass_page_mirror(&p, &bank), 1u << 1);
    CHECK(outputs_set_role(&bank, 1u, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_role(&bank, 6u, OUT_ROLE_SURFACE));
    outputs_arm_roles(&bank, 0u, 3u);
}

/* A decoder with input k pulsing @p w microseconds, output k 3 more. */
static void figures(uint32_t w)
{
    pass_dec_init(&d, 0u);
    pass_dec_event(&d, 0u, 0u);
    for (unsigned f = 0; f < 3u; ++f) {
        uint32_t lvl = 0u;
        for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
            lvl |= (1u << PASS_IN_BIT(k)) | (1u << PASS_OUT_BIT(k));
        }
        pass_dec_event(&d, lvl, 1000u + f * 20000u);
        pass_dec_event(&d, lvl & ~0x2AAAAu, 1000u + f * 20000u + w);
        pass_dec_event(&d, 0u, 1000u + f * 20000u + w + 3u);
    }
}

TEST_CASE(windows_close_every_50ms_and_their_number_wraps)
{
    pass_page_init(&p, true, 1000u);
    figures(1500u);
    CHECK(!pass_page_tick(&p, &d, 1049u));
    CHECK_EQ(reg(LINK_PA_IN_US(0)), 0u);
    CHECK(pass_page_tick(&p, &d, 1050u));
    CHECK_EQ(reg(LINK_PA_WINDOW), 1u);
    for (unsigned k = 0; k < LINK_PA_CHANNELS; ++k) {
        CHECK_EQ(reg(LINK_PA_IN_US(k)), 1500u);
        CHECK_EQ(reg(LINK_PA_OUT_US(k)), 1503u);
        CHECK_EQ(stat(LINK_PT_PERIOD(k)), 20000u);
        CHECK_EQ(stat(LINK_PT_FRAMES(k)), 3u);
    }
    /* On time: the next closes at 1100 even when ticked at 1060. */
    CHECK(!pass_page_tick(&p, &d, 1060u));
    CHECK(!pass_page_tick(&p, &d, 1099u));
    CHECK(pass_page_tick(&p, &d, 1100u));
    /* Late by more than a window: the next starts at the close. */
    CHECK(pass_page_tick(&p, &d, 1300u));
    CHECK(!pass_page_tick(&p, &d, 1349u));
    CHECK(pass_page_tick(&p, &d, 1350u));
    CHECK_EQ(reg(LINK_PA_WINDOW), 4u);
    /* The number wraps at 65536, and the clock's 32 bits too. */
    pass_page_init(&p, true, 0xFFFFFFF0u);
    p.window = 0xFFFFu;
    CHECK(pass_page_tick(&p, &d, 0x22u));
    CHECK_EQ(reg(LINK_PA_WINDOW), 0u);
}

TEST_CASE(flags_mark_the_window_a_lap_or_a_session_fell_in)
{
    pass_page_init(&p, true, 0u);
    figures(1500u);
    pass_page_flag(&p, LINK_PA_F_LAPPED);
    CHECK(pass_page_tick(&p, &d, 50u));
    CHECK_EQ(reg(LINK_PA_FLAGS), LINK_PA_F_LAPPED);
    CHECK(pass_page_tick(&p, &d, 100u));
    CHECK_EQ(reg(LINK_PA_FLAGS), 0u);
    pass_page_flag(&p, LINK_PA_F_SESSION);
    pass_page_flag(&p, LINK_PA_F_STALL);
    CHECK(pass_page_tick(&p, &d, 150u));
    CHECK_EQ(reg(LINK_PA_FLAGS), LINK_PA_F_SESSION | LINK_PA_F_STALL);
    pass_page_init(&p, false, 0u);
    CHECK(pass_page_tick(&p, &d, 50u));
    CHECK_EQ(reg(LINK_PA_FLAGS), LINK_PA_F_NO_CAPTURE);
}

TEST_CASE(lost_and_glitch_bits_are_published_per_input)
{
    pass_page_init(&p, true, 0u);
    figures(1500u);
    /* A 100us spike on input 2. */
    pass_dec_event(&d, 1u << PASS_IN_BIT(2), 70000u);
    pass_dec_event(&d, 0u, 70100u);
    pass_dec_now(&d, 41000u + 100000u);
    CHECK(pass_page_tick(&p, &d, 50u));
    CHECK_EQ(reg(LINK_PA_GLITCH), 1u << 2);
    CHECK_EQ(reg(LINK_PA_LOST), 0x1FFu);
    CHECK_EQ(reg(LINK_PA_IN_US(0)), 0u);
    CHECK_EQ(stat(LINK_PT_GLITCHES), 1u);
    /* The next window without a glitch. */
    CHECK(pass_page_tick(&p, &d, 100u));
    CHECK_EQ(reg(LINK_PA_GLITCH), 0u);
    CHECK_EQ(stat(LINK_PT_GLITCHES), 1u);
}

TEST_CASE(missed_saturates_frames_wrap_and_reset_takes_the_counts_as_zero)
{
    pass_page_init(&p, true, 0u);
    figures(1500u);
    d.pin[PASS_IN_BIT(0)].missed = 70000u;
    d.pin[PASS_IN_BIT(1)].frames = 65536u + 7u;
    CHECK(pass_page_tick(&p, &d, 50u));
    CHECK_EQ(stat(LINK_PT_MISSED(0)), 65535u);
    CHECK_EQ(stat(LINK_PT_FRAMES(1)), 7u);
    const uint16_t one = 1u;
    CHECK_EQ(pass_stat_write(&p, (uint8_t)LINK_PT_RESET, 1u, &one, &d), 0u);
    CHECK_EQ(stat(LINK_PT_MISSED(0)), 0u);
    CHECK_EQ(stat(LINK_PT_FRAMES(1)), 0u);
    CHECK_EQ(stat(LINK_PT_GLITCHES), 0u);
    CHECK_EQ(stat(LINK_PT_RESET), 0u);
    /* The period is no count. */
    CHECK_EQ(stat(LINK_PT_PERIOD(0)), 20000u);
    /* Counts after the reset, from the new zero. */
    d.pin[PASS_IN_BIT(0)].missed += 2u;
    CHECK(pass_page_tick(&p, &d, 100u));
    CHECK_EQ(stat(LINK_PT_MISSED(0)), 2u);
    const uint16_t zero = 0u;
    CHECK_EQ(pass_stat_write(&p, (uint8_t)LINK_PT_RESET, 1u, &zero, &d), 0u);
    CHECK_EQ(stat(LINK_PT_MISSED(0)), 2u);
    const uint16_t two = 2u;
    CHECK_EQ(pass_stat_write(&p, (uint8_t)LINK_PT_RESET, 1u, &two, &d),
             LINK_NACK_BAD_VALUE);
    for (unsigned r = 0; r < LINK_PT_COUNT; ++r) {
        if (r != (unsigned)LINK_PT_RESET) {
            CHECK_EQ(pass_stat_write(&p, (uint8_t)r, 1u, &zero, &d),
                     LINK_NACK_READ_ONLY);
        }
    }
    CHECK_EQ(pass_stat_write(&p, (uint8_t)LINK_PT_COUNT, 1u, &zero, &d),
             LINK_NACK_BAD_RANGE);
}

TEST_CASE(a_session_counts_pass_through_active_and_switches_channels_off)
{
    pass_page_init(&p, true, 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(1, LINK_PA_SRC_OFF, &SAFE), 0u);
    /* BENCH counts through its slot, OFF drives nothing. */
    CHECK_EQ(pass_page_active(&p), 1u << 0);
    /* Channels 0, 2 and 3 dropped; 3's pin a slot holds. */
    const uint16_t off = pass_page_force_off(&p, 0x000Du, 1u << 3);
    CHECK_EQ(off, (1u << 0) | (1u << 2));
    CHECK_EQ(reg(LINK_PA_SOURCE(0)), LINK_PA_SRC_OFF);
    CHECK_EQ(reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_OFF);
    CHECK_EQ(reg(LINK_PA_SOURCE(3)), LINK_PA_SRC_BENCH);
    CHECK_EQ(pass_page_active(&p), 0u);
    CHECK_EQ(pass_page_mirror(&p, armed_for(OUT_ROLES_ALL)), 0u);
    /* Back only with a new write. */
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(pass_page_active(&p), 1u << 0);
}

TEST_CASE(every_programming_session_sets_the_session_flag)
{
    kst_port_t   kst;
    hitec_port_t hitec;
    esc_port_t   esc;
    memset(&kst, 0, sizeof(kst));
    memset(&hitec, 0, sizeof(hitec));
    memset(&esc, 0, sizeof(esc));
    /* None open: PWM, FREE, FREE. */
    CHECK(!pass_page_session(&kst, &hitec, &esc));
    CHECK(!pass_page_session(NULL, NULL, NULL));
    /* KST on programming port 2: from STOPPING, and while its driver
     * still holds the pin after PWM. */
    kst.port = 2u;
    kst.state = (uint8_t)LINK_KST_STOPPING;
    CHECK(pass_page_session(&kst, &hitec, &esc));
    kst.state = (uint8_t)LINK_KST_PROGRAMMING;
    kst.has_pin = 1u;
    CHECK(pass_page_session(&kst, NULL, NULL));
    kst.state = (uint8_t)LINK_KST_PWM;
    CHECK(pass_page_session(&kst, NULL, NULL));
    kst.has_pin = 0u;
    CHECK(!pass_page_session(&kst, &hitec, &esc));
    /* Hitec on programming port 1. */
    hitec.port = 1u;
    hitec.state = (uint8_t)LINK_HITEC_OPEN;
    CHECK(pass_page_session(&kst, &hitec, &esc));
    hitec.state = (uint8_t)LINK_HITEC_FREE;
    hitec.has_pin = 1u;
    CHECK(pass_page_session(NULL, &hitec, NULL));
    hitec.has_pin = 0u;
    CHECK(!pass_page_session(&kst, &hitec, &esc));
    /* ESC wire on programming port 0. */
    esc.target = (uint16_t)LINK_EW_TARGET_PORT;   /* port 0 */
    esc.state = (uint8_t)LINK_ESC_OPEN;
    esc.has_pin = true;
    esc.pin = 22u;
    CHECK(pass_page_session(&kst, &hitec, &esc));
    /* ESC wire on output slot 2 (S3's pin, GPIO 20): the pin mask of the
     * programming ports names none, the session counts all the same. */
    esc.target = 2u;
    esc.pin = 20u;
    CHECK_EQ(esc_port_pin_mask(&esc), 0u);
    CHECK(pass_page_session(&kst, &hitec, &esc));
    /* From OPEN's frame, before the line takes the slot's pin. */
    esc.has_pin = false;
    CHECK(pass_page_session(NULL, NULL, &esc));
    /* A line still holding its pin once the page reads FREE. */
    esc.state = (uint8_t)LINK_ESC_FREE;
    esc.has_pin = true;
    CHECK(pass_page_session(NULL, NULL, &esc));
    esc.has_pin = false;
    CHECK(!pass_page_session(&kst, &hitec, &esc));
}

/* A model of the 9 output pads: who drives each. */
enum { PAD_BENCH = 0, PAD_MIRROR, PAD_LOW, PAD_PWM };
static unsigned pads[LINK_PA_CHANNELS];
static unsigned pad_calls;

static void pad_set(void *ctx, unsigned k, bool mirror)
{
    (void)ctx;
    pads[k] = mirror ? PAD_MIRROR : PAD_LOW;
    ++pad_calls;
}

TEST_CASE(a_programming_ports_pin_is_busy_for_every_source_change)
{
    outputs_t o;
    prog_port_t pg;
    outputs_init(&o, 0u);
    prog_port_init(&pg, 4u);
    pg.regs[LINK_PG_FUNC(0u)] = (uint16_t)LINK_PG_FN_SERVO_PWM;
    /* A SERVO PWM port prebound on S3 (GPIO 20). */
    const uint16_t pin = (uint16_t)RSN_PIN_OUT(2);
    CHECK_EQ(prog_port_write(&pg, (uint8_t)LINK_PG_REG(0u, LINK_PG_PIN), 1u,
                             &pin, &o, 0u, false, 0u), 0u);
    CHECK_EQ(pass_page_busy(&o, &pg, 0u), 1u << 2);
    /* A slot on S5 and a pin held elsewhere on S9. */
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = (uint8_t)RSN_PIN_OUT(4),
                             .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    CHECK_EQ(pass_page_busy(&o, &pg, (uint64_t)1u << RSN_PIN_OUT(8)),
             (1u << 2) | (1u << 4) | (1u << 8));
    CHECK_EQ(pass_page_busy(NULL, NULL, 0u), 0u);
    /* OFF and PASS-THROUGH on the port's pin: BUSY, SOURCE stays BENCH. */
    pass_page_init(&p, true, 0u);
    const pass_ctx_t c = { false, pass_page_busy(&o, &pg, 0u) };
    CHECK_EQ(src(2, LINK_PA_SRC_OFF, &c), LINK_NACK_BUSY);
    CHECK_EQ(src(2, LINK_PA_SRC_PASS, &c), LINK_NACK_BUSY);
    CHECK_EQ(reg(LINK_PA_SOURCE(2)), LINK_PA_SRC_BENCH);
    CHECK_EQ(pass_page_held(&p), 0u);
    /* The port gives the pin back: taken. */
    const uint16_t none = (uint16_t)LINK_PG_PIN_NONE;
    CHECK_EQ(prog_port_write(&pg, (uint8_t)LINK_PG_REG(0u, LINK_PG_PIN), 1u,
                             &none, &o, 0u, false, 0u), 0u);
    const pass_ctx_t c2 = { false, pass_page_busy(&o, &pg, 0u) };
    CHECK_EQ(src(2, LINK_PA_SRC_OFF, &c2), 0u);
}

TEST_CASE(every_route_sets_each_held_pad_again)
{
    pass_page_init(&p, true, 0u);
    CHECK_EQ(src(0, LINK_PA_SRC_PASS, &SAFE), 0u);
    CHECK_EQ(src(2, LINK_PA_SRC_OFF, &SAFE), 0u);
    memset(pads, 0, sizeof(pads));
    pad_calls = 0u;
    const uint8_t srf = OUT_ROLE_BIT(OUT_ROLE_SURFACE);
    CHECK_EQ(pass_page_route(&p, armed_for(0u), pad_set, NULL), 0u);
    CHECK_EQ(pads[0], (unsigned)PAD_LOW);
    CHECK_EQ(pads[2], (unsigned)PAD_LOW);
    CHECK_EQ(pads[4], (unsigned)PAD_BENCH);   /* BENCH not touched */
    CHECK_EQ(pad_calls, 2u);
    /* A programming port's take and give: ports_apply() rebinds PWM on
     * S3's pad, and on S1's.  The next route sets both again. */
    pads[2] = PAD_PWM;
    pads[0] = PAD_PWM;
    CHECK_EQ(pass_page_route(&p, armed_for(0u), pad_set, NULL), 0u);
    CHECK_EQ(pads[2], (unsigned)PAD_LOW);
    CHECK_EQ(pads[0], (unsigned)PAD_LOW);
    /* Armed for channel 0's role (a throttle): MIRROR; touched again and
     * set again; S3 stays low through a surface ARM. */
    CHECK_EQ(pass_page_route(&p, armed_for(OUT_ROLES_ALL), pad_set, NULL),
             1u << 0);
    CHECK_EQ(pads[0], (unsigned)PAD_MIRROR);
    pads[0] = PAD_PWM;
    pads[2] = PAD_PWM;
    CHECK_EQ(pass_page_route(&p, armed_for(srf), pad_set, NULL), 0u);
    CHECK_EQ(pads[0], (unsigned)PAD_LOW);
    CHECK_EQ(pads[2], (unsigned)PAD_LOW);
    CHECK_EQ(pass_page_route(&p, armed_for(OUT_ROLES_ALL), pad_set, NULL),
             1u << 0);
    CHECK_EQ(pads[0], (unsigned)PAD_MIRROR);
    (void)armed_for(0u);
}

int main(void)
{
    RUN(every_channel_starts_on_bench);
    RUN(source_is_written_while_safe_and_a_change_refused_while_armed);
    RUN(bad_values_read_only_registers_and_ranges_are_refused_whole);
    RUN(off_and_pass_through_are_refused_on_a_pin_held_elsewhere);
    RUN(pass_through_wants_the_capture_and_off_does_not);
    RUN(mirror_drives_pass_through_while_armed_only);
    RUN(each_channel_passes_only_while_its_own_role_is_armed);
    RUN(windows_close_every_50ms_and_their_number_wraps);
    RUN(flags_mark_the_window_a_lap_or_a_session_fell_in);
    RUN(lost_and_glitch_bits_are_published_per_input);
    RUN(missed_saturates_frames_wrap_and_reset_takes_the_counts_as_zero);
    RUN(a_session_counts_pass_through_active_and_switches_channels_off);
    RUN(every_programming_session_sets_the_session_flag);
    RUN(a_programming_ports_pin_is_busy_for_every_source_change);
    RUN(every_route_sets_each_held_pad_again);
    return test_summary("pass_page");
}
