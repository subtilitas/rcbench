/*
 * The KST programming port and its link page (shared/outputs/kst_port.c)
 * against the simulated servo of kst_sim.h: every way out of PWM
 * (pulse-width modulation) and back, the refusals, a stop in each state and
 * in the middle of a frame, the write enable, and each timer across the
 * wrap of the millisecond clock.
 *
 * The clock: a pass is 100 us on the wire's clock, and every tenth pass is
 * 1 ms on the port's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "kst_plan.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "tick_wrap.h"

#define CH    3u     /* the channel under test */
#define SLOT  2u
#define PIN   7u
#define CH_THROTTLE 0u
#define CH_UNBOUND  5u
#define KEY   LINK_KST_WRITE_KEY

static sim_t g_sim;
static kst_port_t g_port;
static outputs_t g_out;

static struct {
    bool     bound[OUT_MAX_SLOTS];
    bool     path;
    bool     take_ok;
    bool     taken;          /* between take() and give() */
    unsigned takes, gives;
    uint8_t  take_slot, take_pin;
    link_kst_state_t give_state;
    bool     rail_on;
    unsigned rail_calls;
} g_hw;

static uint32_t g_ms;
static unsigned g_sub;

/* ------------------------------------------------------------- the hardware */

static bool hw_bound(void *ctx, uint8_t slot)
{
    (void)ctx;
    return g_hw.bound[slot];
}

static bool hw_path(void *ctx, uint8_t pin)
{
    (void)ctx;
    (void)pin;
    return g_hw.path;
}

static bool hw_take(void *ctx, uint8_t slot, uint8_t pin)
{
    (void)ctx;
    g_hw.takes++;
    g_hw.take_slot = slot;
    g_hw.take_pin = pin;
    g_hw.taken = true;
    return g_hw.take_ok;
}

static void hw_give(void *ctx)
{
    (void)ctx;
    g_hw.gives++;
    g_hw.taken = false;
    g_hw.give_state = kst_port_state(&g_port);
}

static void hw_rail(void *ctx, bool on)
{
    (void)ctx;
    g_hw.rail_calls++;
    g_hw.rail_on = on;
}

static void fresh_at(uint32_t t0, bool rail)
{
    kst_port_hw_t hw;

    sim_init(&g_sim);
    memset(&g_hw, 0, sizeof(g_hw));
    memset(&g_out, 0, sizeof(g_out));
    g_out.slot[0].driver = OUT_DRIVER_PWM;
    g_out.slot[0].first_channel = CH_THROTTLE;
    g_out.slot[0].channels = 1;
    g_out.slot[0].pin = 4;
    g_out.channel[CH_THROTTLE].role = OUT_ROLE_THROTTLE;
    g_out.slot[1].driver = OUT_DRIVER_PWM;
    g_out.slot[1].first_channel = CH_UNBOUND;
    g_out.slot[1].channels = 1;
    g_out.slot[1].pin = 6;
    g_out.channel[CH_UNBOUND].role = OUT_ROLE_SURFACE;
    g_out.slot[SLOT].driver = OUT_DRIVER_PWM;
    g_out.slot[SLOT].first_channel = CH;
    g_out.slot[SLOT].channels = 1;
    g_out.slot[SLOT].pin = PIN;
    g_out.channel[CH].role = OUT_ROLE_SURFACE;
    g_out.slot[3].driver = OUT_DRIVER_DSHOT;
    g_out.slot[3].first_channel = 8;
    g_out.slot[3].channels = 1;
    g_out.slot[3].pin = 9;
    g_out.channel[8].role = OUT_ROLE_SURFACE;
    g_hw.bound[0] = true;
    g_hw.bound[SLOT] = true;
    g_hw.bound[3] = true;
    g_hw.path = true;
    g_hw.take_ok = true;
    g_hw.rail_on = true;

    memset(&hw, 0, sizeof(hw));
    hw.bound = hw_bound;
    hw.reply_path = hw_path;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.rail = rail ? hw_rail : NULL;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
    g_ms = t0;
    g_sub = 0;
    /* A port is not safe until a pass has said so. */
    kst_port_step(&g_port, true, false, t0);
}

static void fresh(void)
{
    fresh_at(0u, false);
}

/* ------------------------------------------------------------------ the page */

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xDEADu;

    kst_port_read(&g_port, (uint8_t)i, 1, &v);
    return v;
}

static link_kst_state_t state(void)
{
    return (link_kst_state_t)LINK_KS_STATE_OF(reg(LINK_KS_STATE));
}

static bool flag(uint16_t f)
{
    return (reg(LINK_KS_STATE) & f) != 0u;
}

static unsigned op(void)
{
    return LINK_KS_OP_OF(reg(LINK_KS_OP));
}

static unsigned result(void)
{
    return reg(LINK_KS_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_KS_RESULT) >> 8;
}

static unsigned fail_reg(void)
{
    return reg(LINK_KS_FAIL) & 0xFFu;
}

static unsigned steps_done(void)
{
    return reg(LINK_KS_FAIL) >> 8;
}

static kst_image_t page_image(void)
{
    uint16_t r[LINK_KS_IMAGE_REGS];
    kst_image_t img;

    kst_port_read(&g_port, LINK_KS_IMAGE, LINK_KS_IMAGE_REGS, r);
    for (unsigned k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        img.r[2u * k] = (uint8_t)(r[k] & 0xFFu);
        img.r[2u * k + 1u] = (uint8_t)(r[k] >> 8);
    }
    return img;
}

static bool same(const kst_image_t *a, const kst_image_t *b)
{
    return memcmp(a->r, b->r, sizeof(a->r)) == 0;
}

static kst_image_t servo(void)
{
    kst_image_t img;

    memcpy(img.r, g_sim.reg, sizeof(img.r));
    return img;
}

/* A command frame with the next SEQ: the NACK, or 0. */
static uint8_t cmd_raw(uint16_t word, uint16_t key, uint16_t crc)
{
    const uint16_t in[4] = { word, (uint16_t)(reg(LINK_KS_SEQ) + 1u), key,
                             crc };

    return kst_port_write(&g_port, 0, 4, in, &g_out, g_ms);
}

static uint8_t cmd(link_kst_op_t o, unsigned ch, uint16_t key, uint16_t crc)
{
    return cmd_raw((uint16_t)((unsigned)o | (ch << 8)), key, crc);
}

/* Stage registers 4 to 24 and say their CRC. */
static uint16_t stage(const kst_image_t *img, kst_field_set_t unlock,
                      uint16_t start_crc)
{
    uint16_t s[LINK_KS_W_STAGED];

    memset(s, 0, sizeof(s));
    for (unsigned k = 0; k < LINK_KS_IMAGE_REGS; ++k) {
        s[k] = (uint16_t)(img->r[2u * k] | (img->r[2u * k + 1u] << 8));
    }
    for (unsigned i = 0; i < 4u; ++i) {
        s[LINK_KS_W_UNLOCK - LINK_KS_W_IMAGE + i] =
            (uint16_t)(unlock >> (16u * i));
    }
    s[LINK_KS_W_START_CRC - LINK_KS_W_IMAGE] = start_crc;
    for (unsigned at = 0; at < LINK_KS_W_STAGED; at += 4u) {
        const unsigned n = at + 4u <= LINK_KS_W_STAGED
                               ? 4u : LINK_KS_W_STAGED - at;

        CHECK_EQ(kst_port_write(&g_port, (uint8_t)(LINK_KS_W_IMAGE + at),
                                (uint8_t)n, &s[at], &g_out, g_ms), 0);
    }
    return link_kst_crc(s, LINK_KS_W_STAGED);
}

/* The CRC of the image the page shows: what a panel plans from. */
static uint16_t shown_crc(void)
{
    uint16_t r[LINK_KS_IMAGE_REGS];

    kst_port_read(&g_port, LINK_KS_IMAGE, LINK_KS_IMAGE_REGS, r);
    return link_kst_crc(r, LINK_KS_IMAGE_REGS);
}

/* ----------------------------------------------------------------- the clock */

static void tick_as(bool safe, bool stop)
{
    kst_port_step(&g_port, safe, stop, g_ms);
    g_sim.now_us += 100u;
    if (++g_sub == 10u) {
        g_sub = 0;
        g_ms++;
    }
}

static void tick(void)
{
    tick_as(true, false);
}

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < 10u * ms; ++i) {
        tick();
    }
}

/* Pass until no operation runs; 20 s is the bound. */
static void settle(void)
{
    for (unsigned i = 0; i < 200000u && flag(LINK_KS_F_BUSY); ++i) {
        tick();
    }
    CHECK(!flag(LINK_KS_F_BUSY));
}

/* Pass until the wire carries a frame. */
static void until_frame(void)
{
    for (unsigned i = 0; i < 200000u && !g_sim.busy; ++i) {
        tick();
    }
    CHECK(g_sim.busy);
}

static void until_state(link_kst_state_t s)
{
    for (unsigned i = 0; i < 200000u && state() != s; ++i) {
        tick();
    }
    CHECK_EQ(state(), s);
}

/* What no port may do, whatever happens to it. */
static void conduct(void)
{
    CHECK_EQ(g_sim.n_bad_frames, 0);
    CHECK_EQ(g_sim.n_bad_writes, 0);
    CHECK_EQ(g_sim.n_overlap, 0);
}

static void enter(void)
{
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
}

static void read_all(void)
{
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
}

/* In PROGRAMMING with the servo's registers read. */
static void up(void)
{
    fresh();
    enter();
    read_all();
}

static kst_image_t edited(kst_field_id_t id, unsigned raw)
{
    kst_image_t target = page_image();

    CHECK(kst_field_edit(&target, id, (uint16_t)raw));
    return target;
}

/* A write of @p target from the image the page shows. */
static void write_image(const kst_image_t *target, kst_field_set_t unlock)
{
    const uint16_t start = shown_crc();
    const uint16_t crc = stage(target, unlock, start);

    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
}

/* ---------------------------------------------------------------- the cases */

TEST_CASE(a_port_starts_in_pwm_with_an_empty_page)
{
    uint16_t all[LINK_KS_COUNT];

    fresh();
    kst_port_read(&g_port, 0, LINK_KS_COUNT, all);
    CHECK_EQ(all[LINK_KS_STATE], LINK_KST_PWM);
    CHECK_EQ(all[LINK_KS_SEQ], 0);
    CHECK_EQ(all[LINK_KS_OP], 0);
    CHECK_EQ(all[LINK_KS_RESULT], 0);
    CHECK_EQ(all[LINK_KS_FAIL], 0x00FF);
    for (unsigned i = LINK_KS_DIFF_LO; i < LINK_KS_COUNT; ++i) {
        CHECK_EQ(all[i], 0);
    }
    CHECK_EQ(kst_port_state(&g_port), LINK_KST_PWM);
    CHECK_EQ(kst_port_hold_mask(&g_port), 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);
    run_ms(20);
    CHECK_EQ(g_sim.n_frames, 0);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_hw.gives, 0);
}

TEST_CASE(null_arguments_and_a_port_without_hardware_are_refused)
{
    kst_port_hw_t hw;
    kst_port_t p;
    uint16_t v = 0x1234u;
    const uint16_t in[4] = { LINK_KST_OP_ENTER, 1, 0, 0 };

    fresh();
    hw = g_port.hw;
    CHECK(!kst_port_init(NULL, &hw));
    CHECK(!kst_port_init(&p, NULL));
    hw.take = NULL;
    CHECK(!kst_port_init(&p, &hw));
    hw = g_port.hw;
    hw.driver.poll = NULL;
    CHECK(!kst_port_init(&p, &hw));
    CHECK_EQ(kst_port_state(&p), LINK_KST_PWM);

    CHECK_EQ(kst_port_state(NULL), LINK_KST_PWM);
    CHECK_EQ(kst_port_hold_mask(NULL), 0);
    CHECK_EQ(kst_port_pin_mask(NULL), 0);
    kst_port_step(NULL, true, false, 0);
    kst_port_read(NULL, 0, 1, &v);
    kst_port_read(&g_port, 0, 1, NULL);
    kst_port_read(&g_port, 31, 2, &v);
    CHECK_EQ(v, 0x1234);
    CHECK_EQ(kst_port_write(NULL, 0, 4, in, &g_out, 0), LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 0, 4, NULL, &g_out, 0),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 4, 0, in, &g_out, 0),
             LINK_NACK_BAD_RANGE);
    /* ENTER without the outputs: no channel to find. */
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, NULL, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
}

TEST_CASE(enter_stops_pwm_holds_the_pin_low_and_then_talks)
{
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(LINK_KS_CHANNEL_OF(reg(LINK_KS_STATE)), CH);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), KST_SES_BUSY);
    CHECK(flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    /* No pulse on the slot, and the slot still bound. */
    CHECK_EQ(kst_port_hold_mask(&g_port), 1u << SLOT);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);

    run_ms(KST_PORT_STOP_MS - 1u);
    tick();
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(g_hw.takes, 0);
    until_state(LINK_KST_LOW);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.take_slot, SLOT);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK_EQ(kst_port_hold_mask(&g_port), 1u << SLOT);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << SLOT);
    CHECK_EQ(g_sim.n_frames, 0);

    run_ms(KST_PORT_LOW_MS - 2u);
    CHECK_EQ(state(), LINK_KST_LOW);
    until_state(LINK_KST_PROGRAMMING);
    CHECK_EQ(g_sim.n_frames, 0);
    CHECK(!flag(LINK_KS_F_IN_MODE));

    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK(!flag(LINK_KS_F_IMAGE));
    CHECK(!flag(LINK_KS_F_RAIL));
    CHECK(g_sim.n_frames > 0u);
    CHECK(g_sim.in_mode);
    CHECK_EQ(LINK_KS_FRAMES_OF(reg(LINK_KS_OP)), g_sim.n_frames);
    CHECK_EQ(g_hw.gives, 0);
    conduct();
}

TEST_CASE(read_all_puts_the_image_and_the_timing_on_the_page)
{
    kst_image_t img, sv;

    up();
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK(flag(LINK_KS_F_IMAGE));
    CHECK(flag(LINK_KS_F_BACKUP));
    CHECK(flag(LINK_KS_F_FP_OK));
    CHECK(!flag(LINK_KS_F_LOCKED));
    CHECK(!flag(LINK_KS_F_MUST_READ));
    img = page_image();
    sv = servo();
    CHECK(same(&img, &sv));
    CHECK_EQ(fail_reg(), 0xFF);
    CHECK_EQ(reg(LINK_KS_FP_RULES), 0);
    CHECK_EQ(reg(LINK_KS_FP_REGS_LO), 0);
    CHECK_EQ(reg(LINK_KS_FP_REGS_HI), 0);
    CHECK_NEAR(reg(LINK_KS_HALF_MIN_NS), SIM_HALF_NS, 600);
    CHECK_NEAR(reg(LINK_KS_HALF_MAX_NS), SIM_HALF_NS, 600);
    CHECK(reg(LINK_KS_HALF_MIN_NS) <= reg(LINK_KS_HALF_MAX_NS));
    CHECK_NEAR(reg(LINK_KS_DELAY_MIN_US), SIM_READ_DELAY_NS / 1000u, 30);
    CHECK_NEAR(reg(LINK_KS_DELAY_MAX_US), SIM_READ_DELAY_NS / 1000u, 30);
    CHECK_EQ(LINK_KS_FRAMES_OF(reg(LINK_KS_OP)), 96);
    conduct();
}

TEST_CASE(enter_is_refused_for_a_channel_the_port_cannot_take)
{
    fresh();
    /* On no slot at all. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 12, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    /* On a slot that is no PWM. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 8, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    /* On a PWM slot the silicon does not render. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH_UNBOUND, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH_THROTTLE, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_THROTTLE);
    g_hw.path = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_REPLY_PATH);
    g_hw.path = true;

    /* Each was taken, and none started anything. */
    CHECK_EQ(reg(LINK_KS_SEQ), 5);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_NONE);
    CHECK_EQ(result(), 0);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(kst_port_hold_mask(&g_port), 0);
    run_ms(200);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_sim.n_frames, 0);

    /* The refusal goes with the next command that starts. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_STOPPING);
}

TEST_CASE(nothing_but_enter_and_abort_is_taken_in_pwm)
{
    const kst_image_t img = sim_bench_image();
    uint16_t crc;

    fresh();
    crc = stage(&img, 0, 0);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NOT_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_NONE);
    run_ms(200);
    CHECK_EQ(g_sim.n_frames, 0);
    CHECK_EQ(g_hw.takes, 0);
}

TEST_CASE(a_command_frame_is_taken_whole_or_not_at_all)
{
    uint16_t in[4];

    fresh();
    /* Reserved bits, no operation, one past the last, a confirmation on
     * anything but a restore, a key that is not the key. */
    CHECK_EQ(cmd_raw(0x1000u | LINK_KST_OP_ENTER, 0, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd_raw(0x0010u | LINK_KST_OP_ENTER, 0, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd_raw(LINK_KST_OP_NONE, 0, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd_raw(LINK_KST_OP_COUNT, 0, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd_raw(0xFu, 0, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd_raw(LINK_KS_W_CMD_CONFIRM | LINK_KST_OP_WRITE | (CH << 8),
                     KEY, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0x57A4u, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 1, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);
    CHECK_EQ(state(), LINK_KST_PWM);

    /* A window that is not the 4 registers of the command. */
    in[0] = (uint16_t)(LINK_KST_OP_ENTER | (CH << 8));
    in[1] = 1;
    in[2] = 0;
    in[3] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 3, in, &g_out, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 1, 3, in, &g_out, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 2, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 3, 1, in, &g_out, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);

    /* A SEQ that is not the next. */
    in[1] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_VALUE);
    in[1] = 2;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(state(), LINK_KST_PWM);
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 1);
    CHECK_EQ(state(), LINK_KST_STOPPING);
}

TEST_CASE(a_command_sent_again_is_acknowledged_and_does_nothing)
{
    uint16_t in[4];
    unsigned reads;

    up();
    reads = g_sim.n_reads;
    in[0] = (uint16_t)(LINK_KST_OP_READ_ALL | (CH << 8));
    in[1] = (uint16_t)(reg(LINK_KS_SEQ) + 1u);
    in[2] = 0;
    in[3] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    settle();
    CHECK_EQ(g_sim.n_reads, reads + 96u);
    /* Its acknowledgement was lost: the panel sends the frame again. */
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(reg(LINK_KS_SEQ), in[1]);
    settle();
    run_ms(50);
    CHECK_EQ(g_sim.n_reads, reads + 96u);
    /* The same SEQ with another content is no repeat. */
    in[0] = (uint16_t)(LINK_KST_OP_ENTER | (CH << 8));
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_VALUE);
    conduct();
}

TEST_CASE(the_sequence_counter_wraps_to_0)
{
    uint16_t in[4] = { LINK_KST_OP_ABORT, 0xFFFFu, 0, 0 };

    fresh();
    g_port.seq = 0xFFFEu;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 0xFFFF);
    /* 0x10000 is not a SEQ: the next is 0. */
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_VALUE);
    in[1] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);
    /* The repeat of the frame that wrapped. */
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, &g_out, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 1);
    CHECK_EQ(state(), LINK_KST_STOPPING);
}

TEST_CASE(the_staged_registers_are_taken_in_any_windows)
{
    uint16_t in[8];
    uint16_t v;

    fresh();
    for (unsigned i = 0; i < 8u; ++i) {
        in[i] = (uint16_t)(0x1100u + i);
    }
    CHECK_EQ(kst_port_write(&g_port, 4, 8, in, &g_out, g_ms), 0);
    CHECK_EQ(kst_port_write(&g_port, 23, 2, in, &g_out, g_ms), 0);
    CHECK_EQ(g_port.staged[0], 0x1100);
    CHECK_EQ(g_port.staged[7], 0x1107);
    CHECK_EQ(g_port.staged[19], 0x1100);
    CHECK_EQ(g_port.staged[20], 0x1101);

    /* VIEW is 0 or 1, and a window with another value changes nothing. */
    in[0] = 0x2200u;
    in[1] = 2;
    CHECK_EQ(kst_port_write(&g_port, 24, 2, in, &g_out, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(g_port.staged[20], 0x1101);
    CHECK(!flag(LINK_KS_F_VIEW_BACKUP));
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 24, 2, in, &g_out, g_ms), 0);
    CHECK_EQ(g_port.staged[20], 0x2200);
    CHECK(flag(LINK_KS_F_VIEW_BACKUP));
    v = 0;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, &g_out, g_ms), 0);
    CHECK(!flag(LINK_KS_F_VIEW_BACKUP));

    /* Beyond the write map. */
    CHECK_EQ(kst_port_write(&g_port, 26, 1, in, &g_out, g_ms),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(kst_port_write(&g_port, 24, 3, in, &g_out, g_ms),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(kst_port_write(&g_port, 28, 4, in, &g_out, g_ms),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(kst_port_write(&g_port, 30, 4, in, &g_out, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(g_port.staged[20], 0x2200);
}

TEST_CASE(a_command_while_the_port_moves_or_works_is_busy)
{
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    until_state(LINK_KST_LOW);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    /* The entry is what the page still names, and it still runs. */
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), KST_SES_BUSY);
    settle();
    CHECK_EQ(result(), KST_SES_OK);

    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    until_frame();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(result(), KST_SES_BUSY);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_sim.n_reads, 96);
    conduct();
}

TEST_CASE(the_port_holds_one_channel)
{
    up();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 4, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, 4, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, 4, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_CHANNEL);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(LINK_KS_CHANNEL_OF(reg(LINK_KS_STATE)), CH);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(g_sim.n_reads, 96);
}

TEST_CASE(a_write_without_the_enable_is_refused_in_every_state)
{
    const link_kst_op_t writes[3] = { LINK_KST_OP_WRITE, LINK_KST_OP_RESTORE,
                                      LINK_KST_OP_RELEASE };
    kst_image_t target;
    uint16_t crc;
    unsigned frames;

    fresh();
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK_EQ(cmd(writes[i], CH, 0, 0), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_NO_ENABLE);
    }
    enter();
    read_all();
    target = edited(KST_F_DEAD_BAND, 50);
    crc = stage(&target, 0, shown_crc());
    frames = g_sim.n_frames;
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK_EQ(cmd(writes[i], CH, 0, crc), 0);
        CHECK_EQ(refusal(), LINK_KST_REF_NO_ENABLE);
        CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
        CHECK(!flag(LINK_KS_F_BUSY));
    }
    /* The heartbeat lost as well: the missing enable is still the answer. */
    tick_as(false, false);
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, 0, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_ENABLE);
    run_ms(100);
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.reg[0x05], 0x77);

    /* With it the same request writes. */
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(g_sim.n_writes > 0u);
    conduct();
}

TEST_CASE(a_write_takes_the_servo_to_the_staged_image)
{
    kst_image_t before, target, now, sv;
    uint16_t v;

    up();
    before = page_image();
    target = edited(KST_F_DEAD_BAND, 50);
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_WRITE);
    CHECK(flag(LINK_KS_F_BUSY));
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(steps_done(), 1);
    CHECK_EQ(fail_reg(), 0xFF);
    CHECK_EQ(g_sim.n_writes, 1);
    sv = servo();
    CHECK(same(&sv, &target));
    now = page_image();
    CHECK(same(&now, &target));
    CHECK_EQ(reg(LINK_KS_DIFF_LO), 0);
    CHECK_EQ(reg(LINK_KS_DIFF_HI), 0);

    /* The backup is what the servo held when the session first read it. */
    v = 1;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, &g_out, g_ms), 0);
    now = page_image();
    CHECK(same(&now, &before));
    v = 0;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, &g_out, g_ms), 0);
    now = page_image();
    CHECK(same(&now, &target));
    conduct();
}

TEST_CASE(a_write_planned_from_another_image_is_refused)
{
    kst_image_t target;
    uint16_t crc, start;

    up();
    target = edited(KST_F_DEAD_BAND, 50);
    start = shown_crc();
    crc = stage(&target, 0, (uint16_t)(start ^ 1u));
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_START);
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_START);

    /* The servo changed and was read again since the panel's image: the
     * panel's CRC names an image the session no longer holds. */
    crc = stage(&target, 0, start);
    g_sim.reg[0x03] = 19;
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_START);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    run_ms(100);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
}

TEST_CASE(staged_registers_that_changed_under_the_crc_are_refused)
{
    kst_image_t target;
    uint16_t crc, v;

    up();
    target = edited(KST_F_DEAD_BAND, 50);
    crc = stage(&target, 0, shown_crc());
    v = 0x0001u;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_UNLOCK, 1, &v, &g_out, g_ms),
             0);
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_STAGED);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_STAGED);
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_STAGED);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_STAGED);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_sim.n_reads, 96);
}

TEST_CASE(a_write_needs_an_image_read_in_this_session)
{
    const kst_image_t img = sim_bench_image();
    uint16_t crc;

    fresh();
    enter();
    crc = stage(&img, 0, 0);
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_IMAGE);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_IMAGE);
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_IMAGE);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_target_the_planner_refuses_starts_nothing)
{
    kst_image_t target;

    up();
    target = page_image();
    target.r[0x00] = 0x01;
    target.r[0x03] = 19;
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PLAN_R00);

    target = page_image();
    target.r[0x1D] = 0x00;
    write_image(&target, ~(kst_field_set_t)0);
    CHECK_EQ(refusal(), LINK_KST_REF_PLAN_PAIRING);

    target = edited(KST_F_DUTY, 251);
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PLAN_RULES);

    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_target_no_order_of_writes_reaches_is_refused)
{
    kst_image_t start = sim_bench_image();
    kst_image_t target;

    /* Uncontrolled Pos at Upper itself, as test_kst_plan has it. */
    CHECK(kst_field_edit(&start, KST_F_PULSE_UPPER, 0xBFF));
    CHECK(kst_field_edit(&start, KST_F_UNCONT_POS, 0xBFF));
    fresh();
    memcpy(g_sim.reg, start.r, sizeof(g_sim.reg));
    enter();
    read_all();
    target = edited(KST_F_PULSE_UPPER, 0xC00);
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PLAN_NO_PATH);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_locked_field_is_written_only_when_the_request_unlocks_it)
{
    kst_image_t target, sv;

    up();
    target = edited(KST_F_20K_SEL, 0);
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PLAN_RULES);
    CHECK_EQ(g_sim.n_writes, 0);
    write_image(&target, KST_FIELD_BIT(KST_F_20K_SEL));
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    sv = servo();
    CHECK(same(&sv, &target));
    conduct();
}

TEST_CASE(a_failed_write_names_its_register)
{
    kst_image_t target;

    up();
    target = page_image();
    CHECK(kst_field_edit(&target, KST_F_BOOST, 19));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    sim_add_fault(&g_sim, SIM_WRITE, 0x05, 0, 3, SIM_F_WRITE_IGNORE, 0);
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_WRITE_FAILED);
    CHECK_EQ(fail_reg(), 0x05);
    CHECK_EQ(steps_done(), 1);
    CHECK_EQ(g_sim.reg[0x03], 19);
    CHECK_EQ(g_sim.reg[0x05], 0x77);
    conduct();
}

TEST_CASE(a_read_that_fails_names_its_register)
{
    fresh();
    enter();
    /* 2 of 5 reads unanswered and 1 with another value: no 3 equal. */
    sim_add_fault(&g_sim, SIM_READ, 0x09, 0, 2, SIM_F_NO_REPLY, 0);
    sim_add_fault(&g_sim, SIM_READ, 0x09, 0, 1, SIM_F_READ_VALUE, 0x5A);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_READ);
    CHECK_EQ(fail_reg(), 0x09);
    CHECK_EQ(reg(LINK_KS_BAD_LO), 1u << 9);
    CHECK_EQ(reg(LINK_KS_BAD_HI), 0);
    CHECK(!flag(LINK_KS_F_IMAGE));
    CHECK_EQ(reg(LINK_KS_IMAGE), 0);
    CHECK_EQ(reg(LINK_KS_FP_RULES), 0);
}

TEST_CASE(verify_compares_the_servo_with_the_staged_image)
{
    kst_image_t img;
    uint16_t crc;

    up();
    img = page_image();
    crc = stage(&img, 0, 0);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(op(), LINK_KST_OP_VERIFY);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(fail_reg(), 0xFF);

    img.r[0x11] ^= 0x04u;
    img.r[0x1B] ^= 0x01u;
    crc = stage(&img, 0, 0);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, crc), 0);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_VERIFY);
    CHECK_EQ(fail_reg(), 0x11);
    CHECK_EQ(reg(LINK_KS_DIFF_LO), 0);
    CHECK_EQ(reg(LINK_KS_DIFF_HI), (1u << 1) | (1u << 11));
    CHECK_EQ(g_sim.n_writes, 0);
    conduct();
}

TEST_CASE(a_restore_returns_the_servo_to_the_backup)
{
    kst_image_t backup, target, sv, other;
    uint16_t crc;

    up();
    backup = page_image();
    target = edited(KST_F_DEAD_BAND, 50);
    write_image(&target, 0);
    settle();
    CHECK_EQ(result(), KST_SES_OK);

    /* Another image than the session's backup: the session refuses, and
     * its answer is the operation's result. */
    other = backup;
    other.r[0x03] = 19;
    crc = stage(&other, 0, 0);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_RESTORE);
    CHECK_EQ(result(), KST_SES_ERR_BAD_PLAN);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_sim.n_writes, 1);

    /* A restore does not name a start image. */
    crc = stage(&backup, 0, 0x1234u);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    sv = servo();
    CHECK(same(&sv, &backup));
    CHECK_EQ(g_sim.n_writes, 2);
    conduct();
}

TEST_CASE(a_release_of_pairing_runs_the_planners_plan)
{
    kst_plan_t plan;
    kst_image_t img, sv;
    uint16_t crc;

    up();
    img = page_image();
    CHECK_EQ(kst_plan_release_pairing(&img, &plan), KST_PLAN_OK);
    crc = stage(&img, 0, shown_crc());
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(op(), LINK_KST_OP_RELEASE);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(steps_done(), plan.n);
    CHECK_EQ(g_sim.n_writes, plan.n);
    sv = servo();
    CHECK(same(&sv, &plan.target));
    conduct();
}

TEST_CASE(a_stop_before_the_first_frame_returns_the_channel_to_pwm)
{
    /* While the pulse ends: the slot was never released. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    run_ms(10);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(kst_port_hold_mask(&g_port), 0);

    /* With the pin held low. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    run_ms(10);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_hw.give_state, LINK_KST_PWM);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);

    /* In the entry's own low time. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_PROGRAMMING);
    run_ms(10);
    tick_as(true, true);
    run_ms(20);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK(!flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_sim.n_frames, 0);

    /* The heartbeat lost does the same as a stop. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    run_ms(300);
    CHECK_EQ(g_sim.n_frames, 0);
}

/* A stop with a frame on the wire, @p safe false for a lost heartbeat. */
static void stop_mid_read(bool safe, bool stop)
{
    unsigned frames;

    up();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    run_ms(60);
    until_frame();
    frames = g_sim.n_frames;
    tick_as(safe, stop);
    /* The frame on the wire is finished, not cut. */
    CHECK(g_sim.busy);
    CHECK(flag(LINK_KS_F_BUSY));
    for (unsigned i = 0; i < 2000u && flag(LINK_KS_F_BUSY); ++i) {
        tick_as(safe, false);
    }
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!g_sim.busy);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.gives, 0);
    for (unsigned i = 0; i < 3000u; ++i) {
        tick_as(safe, false);
    }
    CHECK_EQ(g_sim.n_frames, frames);
    conduct();
}

TEST_CASE(a_stop_mid_frame_lets_the_frame_end_and_asks_for_a_full_read)
{
    kst_image_t target;
    uint16_t crc;

    stop_mid_read(true, true);

    /* Nothing but a read, an entry, an abort and the power cycle. */
    target = edited(KST_F_DEAD_BAND, 50);
    crc = stage(&target, 0, shown_crc());
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    CHECK_EQ(cmd(LINK_KST_OP_RESTORE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    CHECK_EQ(cmd(LINK_KST_OP_RELEASE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK(flag(LINK_KS_F_MUST_READ));

    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(flag(LINK_KS_F_MUST_READ));

    read_all();
    CHECK(!flag(LINK_KS_F_MUST_READ));
    crc = stage(&target, 0, shown_crc());
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, CH, KEY, crc), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    conduct();
}

TEST_CASE(a_lost_heartbeat_mid_frame_ends_the_session_the_same_way)
{
    stop_mid_read(false, false);

    /* While it stays lost nothing starts but an abort and the power
     * cycle. */
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_UNSAFE);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_UNSAFE);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_UNSAFE);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.gives, 1);

    /* And no channel leaves PWM. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_UNSAFE);
    CHECK_EQ(state(), LINK_KST_PWM);
}

TEST_CASE(a_frame_does_not_start_while_the_heartbeat_is_lost)
{
    unsigned frames;

    up();
    frames = g_sim.n_frames;
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    /* Lost in the pass after the command, before the first frame. */
    for (unsigned i = 0; i < 2000u; ++i) {
        tick_as(false, false);
    }
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(flag(LINK_KS_F_MUST_READ));
}

TEST_CASE(a_stop_mid_write_locks_the_session)
{
    kst_image_t target;
    unsigned writes;

    up();
    target = page_image();
    CHECK(kst_field_edit(&target, KST_F_BOOST, 19));
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    write_image(&target, 0);
    for (unsigned i = 0; i < 200000u && g_sim.n_writes == 0u; ++i) {
        tick();
    }
    CHECK_EQ(g_sim.n_writes, 1);
    CHECK(g_sim.busy);
    tick_as(true, true);
    CHECK(g_sim.busy);
    settle();
    writes = g_sim.n_writes;
    CHECK(result() != KST_SES_OK);
    CHECK(result() != KST_SES_BUSY);
    CHECK_EQ(op(), LINK_KST_OP_WRITE);
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK(flag(LINK_KS_F_LOCKED));
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    run_ms(300);
    CHECK_EQ(g_sim.n_writes, writes);

    /* After the read the session takes a restore and nothing else. */
    read_all();
    write_image(&target, 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(result(), KST_SES_ERR_LOCKED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_sim.n_writes, writes);
    conduct();
}

TEST_CASE(a_stop_between_operations_asks_for_a_full_read_too)
{
    up();
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    read_all();
    CHECK(!flag(LINK_KS_F_MUST_READ));
}

TEST_CASE(abort_ends_the_operation_and_is_never_the_operation_named)
{
    unsigned frames;

    /* On the way out of PWM. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    run_ms(10);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_hw.gives, 0);

    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, 9, 0, 0), 0);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_hw.gives, 1);

    /* With a frame on the wire. */
    up();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    run_ms(40);
    until_frame();
    frames = g_sim.n_frames;
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK(g_sim.busy);
    settle();
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);

    /* With nothing running. */
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(!flag(LINK_KS_F_MUST_READ));
    conduct();
}

TEST_CASE(pwm_returns_only_after_the_power_cycle_is_confirmed)
{
    up();
    /* Nothing running, for as long as it takes. */
    run_ms(3000);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(kst_port_hold_mask(&g_port), 1u << SLOT);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << SLOT);
    CHECK_EQ(g_hw.gives, 0);

    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK(!flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_hw.give_state, LINK_KST_PWM);
    CHECK_EQ(g_hw.rail_calls, 0);
    CHECK_EQ(kst_port_hold_mask(&g_port), 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);

    /* The next entry is a session of its own. */
    g_sim.in_mode = false;
    enter();
    CHECK(!flag(LINK_KS_F_IMAGE));
    CHECK(!flag(LINK_KS_F_BACKUP));
    CHECK_EQ(g_hw.takes, 2);
    conduct();
}

TEST_CASE(a_power_cycle_confirmed_mid_frame_waits_for_the_frame)
{
    unsigned frames;

    up();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    run_ms(40);
    until_frame();
    frames = g_sim.n_frames;
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(result(), KST_SES_BUSY);
    tick();
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(g_hw.gives, 0);
    /* A second one while the first waits. */
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK_EQ(g_hw.gives, 1);
    conduct();
}

TEST_CASE(a_switched_rail_is_cycled_by_the_port)
{
    fresh_at(0u, true);
    CHECK(flag(LINK_KS_F_RAIL));
    enter();
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK_EQ(g_hw.rail_calls, 1);
    CHECK(!g_hw.rail_on);
    CHECK(flag(LINK_KS_F_BUSY));
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(result(), KST_SES_BUSY);
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << SLOT);

    /* Nothing is taken on the way, and a stop changes nothing. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    tick_as(true, true);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);

    run_ms(KST_PORT_RAIL_OFF_MS - 10u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    until_state(LINK_KST_RAIL_WAIT);
    CHECK_EQ(g_hw.rail_calls, 2);
    CHECK(g_hw.rail_on);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    run_ms(KST_PORT_RAIL_WAIT_MS - 10u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK_EQ(g_hw.gives, 0);
    until_state(LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_hw.rail_calls, 2);
}

TEST_CASE(a_driver_without_resources_returns_the_channel_to_pwm)
{
    fresh();
    g_hw.take_ok = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), KST_SES_ERR_DRIVER);
    CHECK_EQ(g_hw.takes, 1);
    /* The slot take() released is bound again. */
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(kst_port_hold_mask(&g_port), 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);
    CHECK_EQ(g_sim.n_frames, 0);
}

TEST_CASE(a_driver_that_refuses_the_first_frame_returns_the_channel_to_pwm)
{
    fresh();
    sim_add_fault(&g_sim, SIM_ANY, -1, 0, 1, SIM_F_START_FAIL, 0);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_DRIVER);
    run_ms(5);
    /* Nothing was said to the servo: no power cycle is asked. */
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.gives, 1);
}

TEST_CASE(a_servo_that_does_not_answer_still_needs_the_power_cycle)
{
    fresh();
    g_sim.powered = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_NO_SERVO);
    run_ms(500);
    /* Frames went out; a servo that heard them and did not answer may be
     * in programming mode. */
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(reg(LINK_KS_HALF_MIN_NS), 0);
    CHECK_EQ(reg(LINK_KS_DELAY_MAX_US), 0);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(result(), KST_SES_ERR_NOT_IN_MODE);

    /* The entry again, with the servo there. */
    g_sim.powered = true;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.takes, 1);
    read_all();
    conduct();
}

TEST_CASE(the_frame_count_holds_at_4095)
{
    up();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    until_frame();
    g_port.frames = 4094u;
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(LINK_KS_FRAMES_OF(reg(LINK_KS_OP)), 4095);
    CHECK_EQ(op(), LINK_KST_OP_READ_ALL);
}

/* --- each timer across the wrap of the millisecond clock ---------------------- */

static void stop_time_case(uint32_t t0)
{
    fresh_at(t0, false);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    kst_port_step(&g_port, true, false, t0 + 1u);
    CHECK_EQ(state(), LINK_KST_STOPPING);
    kst_port_step(&g_port, true, false, t0 + KST_PORT_STOP_MS - 1u);
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(g_hw.takes, 0);
    kst_port_step(&g_port, true, false, t0 + KST_PORT_STOP_MS);
    CHECK_EQ(state(), LINK_KST_LOW);
    CHECK_EQ(g_hw.takes, 1);
}

TEST_CASE(the_stop_time_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(stop_time_case, 20u);
}

static void low_time_case(uint32_t t0)
{
    const uint32_t low = t0 + KST_PORT_STOP_MS;

    fresh_at(t0, false);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    kst_port_step(&g_port, true, false, low);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + 1u);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + KST_PORT_LOW_MS - 1u);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + KST_PORT_LOW_MS);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(g_sim.n_frames, 0);
}

TEST_CASE(the_low_time_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(low_time_case, KST_PORT_STOP_MS + 30u);
}

static uint32_t g_rail_k;

/* The rail times, with the wrap g_rail_k ms after the rail went off. */
static void rail_time_case(uint32_t t0)
{
    uint32_t off;

    fresh_at(0u, true);
    enter();
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    off = t0 == 0u ? g_ms : t0;
    kst_port_step(&g_port, true, false, off);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    kst_port_step(&g_port, true, false, off + 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    kst_port_step(&g_port, true, false, off + KST_PORT_RAIL_OFF_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK(!g_hw.rail_on);
    kst_port_step(&g_port, true, false, off + KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK(g_hw.rail_on);
    kst_port_step(&g_port, true, false, off + KST_PORT_RAIL_OFF_MS + 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false,
                  off + KST_PORT_RAIL_OFF_MS + KST_PORT_RAIL_WAIT_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false,
                  off + KST_PORT_RAIL_OFF_MS + KST_PORT_RAIL_WAIT_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
}

TEST_CASE(the_rail_off_time_holds_across_the_tick_wrap)
{
    g_rail_k = 3000u;
    at_tick_0_and_before_the_wrap(rail_time_case, g_rail_k);
}

TEST_CASE(the_rail_wait_time_holds_across_the_tick_wrap)
{
    g_rail_k = KST_PORT_RAIL_OFF_MS + 1000u;
    at_tick_0_and_before_the_wrap(rail_time_case, g_rail_k);
}

int main(void)
{
    RUN(a_port_starts_in_pwm_with_an_empty_page);
    RUN(null_arguments_and_a_port_without_hardware_are_refused);
    RUN(enter_stops_pwm_holds_the_pin_low_and_then_talks);
    RUN(read_all_puts_the_image_and_the_timing_on_the_page);
    RUN(enter_is_refused_for_a_channel_the_port_cannot_take);
    RUN(nothing_but_enter_and_abort_is_taken_in_pwm);
    RUN(a_command_frame_is_taken_whole_or_not_at_all);
    RUN(a_command_sent_again_is_acknowledged_and_does_nothing);
    RUN(the_sequence_counter_wraps_to_0);
    RUN(the_staged_registers_are_taken_in_any_windows);
    RUN(a_command_while_the_port_moves_or_works_is_busy);
    RUN(the_port_holds_one_channel);
    RUN(a_write_without_the_enable_is_refused_in_every_state);
    RUN(a_write_takes_the_servo_to_the_staged_image);
    RUN(a_write_planned_from_another_image_is_refused);
    RUN(staged_registers_that_changed_under_the_crc_are_refused);
    RUN(a_write_needs_an_image_read_in_this_session);
    RUN(a_target_the_planner_refuses_starts_nothing);
    RUN(a_target_no_order_of_writes_reaches_is_refused);
    RUN(a_locked_field_is_written_only_when_the_request_unlocks_it);
    RUN(a_failed_write_names_its_register);
    RUN(a_read_that_fails_names_its_register);
    RUN(verify_compares_the_servo_with_the_staged_image);
    RUN(a_restore_returns_the_servo_to_the_backup);
    RUN(a_release_of_pairing_runs_the_planners_plan);
    RUN(a_stop_before_the_first_frame_returns_the_channel_to_pwm);
    RUN(a_stop_mid_frame_lets_the_frame_end_and_asks_for_a_full_read);
    RUN(a_lost_heartbeat_mid_frame_ends_the_session_the_same_way);
    RUN(a_frame_does_not_start_while_the_heartbeat_is_lost);
    RUN(a_stop_mid_write_locks_the_session);
    RUN(a_stop_between_operations_asks_for_a_full_read_too);
    RUN(abort_ends_the_operation_and_is_never_the_operation_named);
    RUN(pwm_returns_only_after_the_power_cycle_is_confirmed);
    RUN(a_power_cycle_confirmed_mid_frame_waits_for_the_frame);
    RUN(a_switched_rail_is_cycled_by_the_port);
    RUN(a_driver_without_resources_returns_the_channel_to_pwm);
    RUN(a_driver_that_refuses_the_first_frame_returns_the_channel_to_pwm);
    RUN(a_servo_that_does_not_answer_still_needs_the_power_cycle);
    RUN(the_frame_count_holds_at_4095);
    RUN(the_stop_time_holds_across_the_tick_wrap);
    RUN(the_low_time_holds_across_the_tick_wrap);
    RUN(the_rail_off_time_holds_across_the_tick_wrap);
    RUN(the_rail_wait_time_holds_across_the_tick_wrap);
    return test_summary("kst_port");
}
