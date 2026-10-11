/*
 * The KST page on a programming port (shared/outputs/kst_port.c) against
 * the simulated servo of kst_sim.h and a simulated PD mini: every way out
 * of PWM (pulse-width modulation) and back, the refusals, a stop in each
 * state and in the middle of a frame, the write enable, the supply set,
 * cycled, refusing and not answering, and each timer across the wrap of
 * the millisecond clock.
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
#include "pdmini.h"
#include "tick_wrap.h"

#define CH    0u     /* the port under test */
#define OTHER 1u     /* the build's second port */
#define PIN   13u
#define KEY   LINK_KST_WRITE_KEY
#define MV    5000u  /* what the tests stage for the supply */
#define MA    1000u
#define LAG_MS 20u   /* the simulated PD mini reads back this late */

/* The supply a case starts with. */
typedef enum {
    SUPPLY_NONE,     /* no callbacks: a build without a PD mini */
    SUPPLY_OFFLINE,  /* callbacks, and no PD mini answers */
    SUPPLY_OFF,      /* a PD mini, asked off and read off */
    SUPPLY_ON,       /* asked on at MV and read on */
    SUPPLY_ON_HIGH,  /* asked on at 7400 mV and read on */
} supply_kind_t;

static sim_t g_sim;
static kst_port_t g_port;

static struct {
    uint8_t  pin[LINK_PG_PORTS_MAX];
    bool     take_ok;
    bool     taken;          /* between take() and give() */
    unsigned takes, gives;
    uint8_t  take_port, take_pin;
    link_kst_state_t give_state;
    /* The PD mini. */
    supply_port_view_t v;
    bool     set_ok;         /* supply_set(on) is taken */
    bool     follow;         /* the reading follows the command, LAG_MS late */
    bool     moving;         /* a command the reading has not followed */
    uint32_t asked_ms;
    unsigned sets, ons, offs;
    unsigned raises;         /* an ON above the voltage of an output on */
} g_hw;

static uint32_t g_ms;
static unsigned g_sub;

/* ------------------------------------------------------------- the hardware */

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return g_hw.pin[port];
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    g_hw.takes++;
    g_hw.take_port = port;
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

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    *out = g_hw.v;
}

/* As supply_page_port_set(): an OFF is always taken, an ON not above the
 * voltage of an output that is on. */
static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    g_hw.sets++;
    if (!on) {
        g_hw.offs++;
        g_hw.v.asked_on = false;
        g_hw.v.on = false;
        g_hw.v.off = false;
        g_hw.moving = true;
        g_hw.asked_ms = g_ms;
        return true;
    }
    if (g_hw.v.asked_on && mv > g_hw.v.set_mv) {
        g_hw.raises++;
        return false;
    }
    if (!g_hw.set_ok || !g_hw.v.online) {
        return false;
    }
    g_hw.ons++;
    g_hw.v.asked_on = true;
    g_hw.v.set_mv = mv;
    g_hw.v.set_ma = ma;
    g_hw.v.on = false;
    g_hw.v.off = false;
    g_hw.moving = true;
    g_hw.asked_ms = g_ms;
    return true;
}

/* The PD mini's reading catches up with what was asked. */
static void supply_reads(void)
{
    g_hw.v.on = g_hw.v.asked_on;
    g_hw.v.off = !g_hw.v.asked_on;
    g_hw.moving = false;
}

/* The SUPPLY page switches the output off: the panel's OFF, or the
 * heartbeat lost. */
static void supply_asked_off(void)
{
    g_hw.v.asked_on = false;
    g_hw.v.on = false;
    g_hw.v.off = false;
    g_hw.moving = true;
    g_hw.asked_ms = g_ms;
}

static const uint16_t k_supply[2] = { MV, MA };

static void fresh_at(uint32_t t0, supply_kind_t supply)
{
    kst_port_hw_t hw;

    sim_init(&g_sim);
    memset(&g_hw, 0, sizeof(g_hw));
    g_hw.pin[CH] = PIN;
    g_hw.pin[OTHER] = 9u;
    g_hw.pin[2] = 0xFFu;
    g_hw.pin[3] = 0xFFu;
    g_hw.take_ok = true;
    g_hw.set_ok = true;
    g_hw.follow = true;
    g_hw.v.online = supply != SUPPLY_OFFLINE;
    g_hw.v.asked_on = supply == SUPPLY_ON || supply == SUPPLY_ON_HIGH;
    g_hw.v.on = g_hw.v.asked_on;
    g_hw.v.off = supply == SUPPLY_OFF;
    g_hw.v.set_mv = supply == SUPPLY_ON_HIGH ? 7400u : MV;
    g_hw.v.set_ma = 2000u;
    g_hw.v.vin_mv = 20000u;

    memset(&hw, 0, sizeof(hw));
    hw.ports = 2u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    if (supply != SUPPLY_NONE) {
        hw.supply = hw_supply;
        hw.supply_set = hw_supply_set;
    }
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
    g_ms = t0;
    g_sub = 0;
    /* A port is not safe until a pass has said so. */
    kst_port_step(&g_port, true, false, t0);
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, k_supply, t0),
             0);
}

static void fresh(void)
{
    fresh_at(0u, SUPPLY_NONE);
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

    return kst_port_write(&g_port, 0, 4, in, g_ms);
}

static uint8_t cmd(link_kst_op_t o, unsigned ch, uint16_t key, uint16_t crc)
{
    return cmd_raw(LINK_KS_W_CMD_OF(o, ch), key, crc);
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
                                (uint8_t)n, &s[at], g_ms), 0);
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
    if (g_hw.follow && g_hw.moving
        && (uint32_t)(g_ms - g_hw.asked_ms) >= LAG_MS) {
        supply_reads();
    }
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
    /* Nor ask for a higher voltage under an output that is on. */
    CHECK_EQ(g_hw.raises, 0);
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

static void run_ms_as(unsigned ms, bool safe, bool stop)
{
    for (unsigned i = 0; i < 10u * ms; ++i) {
        tick_as(safe, stop);
    }
}

/* In PROGRAMMING on a port with @p supply. */
static void enter_with(supply_kind_t supply)
{
    fresh_at(0u, supply);
    enter();
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
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
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
    const uint16_t in[4] = { LINK_KS_W_CMD_OF(LINK_KST_OP_ENTER, CH), 1, 0,
                             0 };

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
    CHECK_EQ(kst_port_quiet_mask(NULL), 0);
    CHECK_EQ(kst_port_pin_mask(NULL), 0);
    kst_port_step(NULL, true, false, 0);
    kst_port_read(NULL, 0, 1, &v);
    kst_port_read(&g_port, 0, 1, NULL);
    kst_port_read(&g_port, 31, 2, &v);
    CHECK_EQ(v, 0x1234);
    CHECK_EQ(kst_port_write(NULL, 0, 4, in, 0), LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 0, 4, NULL, 0), LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 4, 0, in, 0), LINK_NACK_BAD_RANGE);

    /* No port, more than the model holds, no pin function, half a
     * supply. */
    hw = g_port.hw;
    hw.ports = 0u;
    CHECK(!kst_port_init(&p, &hw));
    hw.ports = LINK_PG_PORTS_MAX + 1u;
    CHECK(!kst_port_init(&p, &hw));
    hw.ports = LINK_PG_PORTS_MAX;
    CHECK(kst_port_init(&p, &hw));
    hw.pin = NULL;
    CHECK(!kst_port_init(&p, &hw));
    hw = g_port.hw;
    hw.give = NULL;
    CHECK(!kst_port_init(&p, &hw));
    hw = g_port.hw;
    hw.supply = hw_supply;
    CHECK(!kst_port_init(&p, &hw));
    hw.supply = NULL;
    hw.supply_set = hw_supply_set;
    CHECK(!kst_port_init(&p, &hw));
    hw.supply = hw_supply;
    CHECK(kst_port_init(&p, &hw));
}

TEST_CASE(enter_stops_pwm_holds_the_pin_low_and_then_talks)
{
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(LINK_KS_PORT_OF(reg(LINK_KS_STATE)), CH);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), KST_SES_BUSY);
    CHECK(flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    /* No pulse on the port, and its pin still the generator's. */
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);

    run_ms(KST_PORT_STOP_MS - 1u);
    tick();
    CHECK_EQ(state(), LINK_KST_STOPPING);
    CHECK_EQ(g_hw.takes, 0);
    until_state(LINK_KST_LOW);
    CHECK_EQ(g_hw.takes, 1);
    CHECK_EQ(g_hw.take_port, CH);
    CHECK_EQ(g_hw.take_pin, PIN);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << CH);
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

TEST_CASE(enter_is_refused_for_anything_but_a_programming_port_with_a_pin)
{
    fresh();
    /* A port the build does not have, and one past the model. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 2, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 15, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    /* A command that names an output channel, as protocol 4.12 wrote it:
     * a standard servo port is no programming port. */
    CHECK_EQ(cmd_raw((uint16_t)(LINK_KST_OP_ENTER | (3u << 8)), 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd_raw(LINK_KST_OP_ENTER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd_raw(LINK_KST_OP_REBOOT, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    /* A port without a pin on the generator. */
    g_hw.pin[CH] = 0xFFu;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_PIN);
    g_hw.pin[CH] = OUT_MAX_PIN + 1u;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_PIN);
    g_hw.pin[CH] = PIN;

    /* Each was taken, and none started anything. */
    CHECK_EQ(reg(LINK_KS_SEQ), 7);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_NONE);
    CHECK_EQ(result(), 0);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
    run_ms(200);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_sim.n_frames, 0);

    /* The refusal goes with the next command that starts. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(state(), LINK_KST_STOPPING);

    /* The other port of the build is one too, on its own pin. */
    fresh();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(LINK_KS_PORT_OF(reg(LINK_KS_STATE)), OTHER);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << OTHER);
    until_state(LINK_KST_LOW);
    CHECK_EQ(g_hw.take_port, OTHER);
    CHECK_EQ(g_hw.take_pin, 9);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << OTHER);
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
    CHECK_EQ(cmd_raw(LINK_KS_W_CMD_CONFIRM | LINK_KS_W_CMD_OF(LINK_KST_OP_WRITE, CH),
                     KEY, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0x57A4u, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 1, 0), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);
    CHECK_EQ(state(), LINK_KST_PWM);

    /* A window that is not the 4 registers of the command. */
    in[0] = LINK_KS_W_CMD_OF(LINK_KST_OP_ENTER, CH);
    in[1] = 1;
    in[2] = 0;
    in[3] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 3, in, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 1, 3, in, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 2, 4, in, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(kst_port_write(&g_port, 3, 1, in, g_ms),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);

    /* A SEQ that is not the next. */
    in[1] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms),
             LINK_NACK_BAD_VALUE);
    in[1] = 2;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(state(), LINK_KST_PWM);
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 1);
    CHECK_EQ(state(), LINK_KST_STOPPING);
}

TEST_CASE(a_command_sent_again_is_acknowledged_and_does_nothing)
{
    uint16_t in[4];
    unsigned reads;

    up();
    reads = g_sim.n_reads;
    in[0] = LINK_KS_W_CMD_OF(LINK_KST_OP_READ_ALL, CH);
    in[1] = (uint16_t)(reg(LINK_KS_SEQ) + 1u);
    in[2] = 0;
    in[3] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
    settle();
    CHECK_EQ(g_sim.n_reads, reads + 96u);
    /* Its acknowledgement was lost: the panel sends the frame again. */
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(reg(LINK_KS_SEQ), in[1]);
    settle();
    run_ms(50);
    CHECK_EQ(g_sim.n_reads, reads + 96u);
    /* The same SEQ with another content is no repeat. */
    in[0] = LINK_KS_W_CMD_OF(LINK_KST_OP_ENTER, CH);
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms),
             LINK_NACK_BAD_VALUE);
    conduct();
}

TEST_CASE(the_sequence_counter_wraps_to_0)
{
    uint16_t in[4] = { LINK_KST_OP_ABORT, 0xFFFFu, 0, 0 };

    fresh();
    g_port.seq = 0xFFFEu;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 0xFFFF);
    /* 0x10000 is not a SEQ: the next is 0. */
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms),
             LINK_NACK_BAD_VALUE);
    in[1] = 0;
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
    CHECK_EQ(reg(LINK_KS_SEQ), 0);
    /* The repeat of the frame that wrapped. */
    CHECK_EQ(kst_port_write(&g_port, 0, 4, in, g_ms), 0);
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
    CHECK_EQ(kst_port_write(&g_port, 4, 8, in, g_ms), 0);
    CHECK_EQ(kst_port_write(&g_port, 23, 2, in, g_ms), 0);
    CHECK_EQ(g_port.staged[0], 0x1100);
    CHECK_EQ(g_port.staged[7], 0x1107);
    CHECK_EQ(g_port.staged[19], 0x1100);
    CHECK_EQ(g_port.staged[20], 0x1101);

    /* VIEW is 0 or 1, and a window with another value changes nothing. */
    in[0] = 0x2200u;
    in[1] = 2;
    CHECK_EQ(kst_port_write(&g_port, 24, 2, in, g_ms),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(g_port.staged[20], 0x1101);
    CHECK(!flag(LINK_KS_F_VIEW_BACKUP));
    in[1] = 1;
    CHECK_EQ(kst_port_write(&g_port, 24, 2, in, g_ms), 0);
    CHECK_EQ(g_port.staged[20], 0x2200);
    CHECK(flag(LINK_KS_F_VIEW_BACKUP));
    v = 0;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, g_ms), 0);
    CHECK(!flag(LINK_KS_F_VIEW_BACKUP));

    /* The supply of an ENTER, with VIEW or alone. */
    in[0] = 0x3300u;
    in[1] = 0;
    in[2] = 6000;
    in[3] = 1500;
    CHECK_EQ(kst_port_write(&g_port, 24, 4, in, g_ms), 0);
    CHECK_EQ(g_port.staged[20], 0x3300);
    CHECK_EQ(g_port.supply_mv, 6000);
    CHECK_EQ(g_port.supply_ma, 1500);
    v = 1200;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MA, 1, &v, g_ms), 0);
    CHECK_EQ(g_port.supply_mv, 6000);
    CHECK_EQ(g_port.supply_ma, 1200);

    /* Beyond the write map. */
    CHECK_EQ(kst_port_write(&g_port, 27, 2, in, g_ms), LINK_NACK_READ_ONLY);
    CHECK_EQ(kst_port_write(&g_port, 28, 4, in, g_ms), LINK_NACK_READ_ONLY);
    CHECK_EQ(kst_port_write(&g_port, 30, 4, in, g_ms), LINK_NACK_BAD_RANGE);
    CHECK_EQ(g_port.staged[20], 0x3300);
    CHECK_EQ(g_port.supply_ma, 1200);
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

TEST_CASE(one_port_is_out_of_pwm_at_a_time)
{
    up();
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_PORT);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(LINK_KS_PORT_OF(reg(LINK_KS_STATE)), CH);
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
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, g_ms), 0);
    now = page_image();
    CHECK(same(&now, &before));
    v = 0;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_VIEW, 1, &v, g_ms), 0);
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
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_UNLOCK, 1, &v, g_ms),
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

TEST_CASE(a_stop_before_the_first_frame_returns_the_port_to_pwm)
{
    /* While the pulse ends: the pin was never taken. */
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
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);

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
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << CH);
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
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
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

/* --- the supply on the way in --------------------------------------------- */

TEST_CASE(enter_switches_a_supply_that_is_off_on_at_the_staged_voltage)
{
    fresh_at(0u, SUPPLY_OFF);
    CHECK(flag(LINK_KS_F_RAIL));
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    until_state(LINK_KST_LOW);
    /* The pin is low before the servo has power. */
    CHECK(g_hw.taken);
    CHECK_EQ(g_hw.sets, 0);
    tick();
    CHECK_EQ(g_hw.ons, 1);
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, MA);

    /* Read on LAG_MS later; the servo then has the settle time. */
    run_ms(LAG_MS + KST_PORT_RAIL_WAIT_MS - 2u);
    CHECK_EQ(state(), LINK_KST_LOW);
    CHECK_EQ(g_sim.n_frames, 0);
    run_ms(4);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 1);
    CHECK(g_hw.v.asked_on);
    conduct();
}

TEST_CASE(enter_leaves_a_supply_that_is_on_at_or_under_the_staged_voltage)
{
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    /* No settle time: the servo had power all along. */
    run_ms(KST_PORT_LOW_MS - 2u);
    CHECK_EQ(state(), LINK_KST_LOW);
    run_ms(4);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, 2000);

    /* One under the staged voltage is not raised to it. */
    fresh_at(0u, SUPPLY_ON);
    g_hw.v.set_mv = 4800u;
    enter();
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(g_hw.v.set_mv, 4800);
    conduct();
}

TEST_CASE(enter_lowers_a_supply_that_is_on_above_the_staged_voltage)
{
    fresh_at(0u, SUPPLY_ON_HIGH);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    /* Lowered as the pin goes low, with the current limit it had. */
    CHECK_EQ(g_hw.ons, 1);
    CHECK_EQ(g_hw.offs, 0);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, 2000);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(g_hw.sets, 1);

    /* Leaving does not raise it again: the cycle ends at 5.0 V. */
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, 2000);
    conduct();
}

/* The refusal of an ENTER with @p mv and @p ma staged on a PD mini fed
 * with @p vin. */
static unsigned range_case(supply_kind_t supply, uint16_t mv, uint16_t ma,
                           uint16_t vin)
{
    const uint16_t in[2] = { mv, ma };

    fresh_at(0u, supply);
    g_hw.v.vin_mv = vin;
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, in, g_ms), 0);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    if (refusal() != LINK_KST_REF_NONE) {
        CHECK_EQ(state(), LINK_KST_PWM);
        CHECK_EQ(op(), LINK_KST_OP_NONE);
        CHECK_EQ(g_hw.sets, 0);
    }
    return refusal();
}

TEST_CASE(enter_is_refused_for_a_supply_outside_the_range)
{
    /* The voltage: the PD mini's floor to the port's ceiling. */
    CHECK_EQ(range_case(SUPPLY_OFF, PDMINI_V_MIN_MV, MA, 20000u),
             LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFF, PDMINI_V_MIN_MV - 1u, MA, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    CHECK_EQ(range_case(SUPPLY_OFF, KST_PORT_SUPPLY_MAX_MV, MA, 20000u),
             LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFF, KST_PORT_SUPPLY_MAX_MV + 1u, MA, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    CHECK_EQ(range_case(SUPPLY_OFF, 0xFFFFu, MA, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    /* The current limit. */
    CHECK_EQ(range_case(SUPPLY_OFF, MV, PDMINI_I_MIN_MA, 20000u),
             LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFF, MV, PDMINI_I_MIN_MA - 1u, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    CHECK_EQ(range_case(SUPPLY_OFF, MV, PDMINI_I_MAX_MA, 20000u),
             LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFF, MV, PDMINI_I_MAX_MA + 1u, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    /* Nothing staged. */
    CHECK_EQ(range_case(SUPPLY_OFF, 0u, 0u, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    /* The headroom under the PD mini's input, where that is known. */
    CHECK_EQ(range_case(SUPPLY_OFF, MV, MA, MV + PDMINI_HEADROOM_MV),
             LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFF, MV, MA, MV + PDMINI_HEADROOM_MV - 1u),
             LINK_KST_REF_SUPPLY_RANGE);
    CHECK_EQ(range_case(SUPPLY_OFF, MV, MA, 0u), LINK_KST_REF_NONE);
    /* One that is on is held to the same range. */
    CHECK_EQ(range_case(SUPPLY_ON, KST_PORT_SUPPLY_MAX_MV + 1u, MA, 20000u),
             LINK_KST_REF_SUPPLY_RANGE);
    /* No PD mini: the servo's supply is not the port's, and nothing is
     * checked. */
    CHECK_EQ(range_case(SUPPLY_NONE, 0u, 0u, 0u), LINK_KST_REF_NONE);
    CHECK_EQ(range_case(SUPPLY_OFFLINE, 20000u, 9000u, 0u),
             LINK_KST_REF_NONE);
}

TEST_CASE(a_port_without_a_pd_mini_enters_as_it_did_and_asks_for_the_word)
{
    fresh_at(0u, SUPPLY_OFFLINE);
    CHECK(!flag(LINK_KS_F_RAIL));
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    run_ms(KST_PORT_LOW_MS - 2u);
    CHECK_EQ(state(), LINK_KST_LOW);
    run_ms(4);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 0);

    /* No reboot without a supply to switch. */
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_SUPPLY);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);

    /* The operator's word that the servo was without power. */
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 0);

    /* A PD mini that answers by then, asked off: the word again. */
    g_sim.in_mode = false;
    enter();
    g_hw.v.online = true;
    g_hw.v.off = true;
    CHECK(flag(LINK_KS_F_RAIL));
    run_ms(50);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 0);
    conduct();
}

TEST_CASE(a_pd_mini_that_stops_answering_leaves_the_session_to_the_word)
{
    enter_with(SUPPLY_ON);
    g_hw.v.online = false;
    CHECK(!flag(LINK_KS_F_RAIL));
    run_ms(500);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_SUPPLY);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(g_hw.sets, 0);
    conduct();
}

TEST_CASE(a_supply_that_does_not_come_on_ends_the_entry)
{
    /* Switched on, and not read on within the supply time. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    tick();
    CHECK_EQ(g_hw.ons, 1);
    g_hw.follow = false;
    run_ms(KST_PORT_SUPPLY_MS - 3u);
    CHECK_EQ(state(), LINK_KST_LOW);
    CHECK(flag(LINK_KS_F_BUSY));
    run_ms(5);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_ENTER);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    /* Off again: it was switched on here. */
    CHECK_EQ(g_hw.offs, 1);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
    CHECK_EQ(g_sim.n_frames, 0);

    /* The PD mini refuses the switch-on. */
    fresh_at(0u, SUPPLY_OFF);
    g_hw.set_ok = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.sets, 1);
    CHECK_EQ(g_hw.offs, 0);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);

    /* Asked off and never read off. */
    fresh_at(0u, SUPPLY_OFF);
    g_hw.v.off = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    run_ms(KST_PORT_SUPPLY_MS - 3u);
    CHECK_EQ(state(), LINK_KST_LOW);
    run_ms(5);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.sets, 0);

    /* The PD mini stops answering after the switch-on. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    tick();
    g_hw.v.online = false;
    run_ms(KST_PORT_SUPPLY_MS + 2u);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.offs, 1);

    /* The lowering refused. */
    fresh_at(0u, SUPPLY_ON_HIGH);
    g_hw.set_ok = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    /* Left as it was found: on, at its voltage. */
    CHECK_EQ(g_hw.offs, 0);
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.v.set_mv, 7400);

    /* The input voltage fell between the command and the low pin. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    g_hw.v.vin_mv = MV;
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(g_hw.gives, 1);
    conduct();
}

TEST_CASE(an_entry_that_ends_early_leaves_the_supply_as_it_found_it)
{
    /* Switched on here: off again, by ABORT, STOP and the heartbeat. */
    for (unsigned how = 0; how < 3u; ++how) {
        fresh_at(0u, SUPPLY_OFF);
        CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
        until_state(LINK_KST_LOW);
        run_ms(LAG_MS + 200u);
        CHECK_EQ(state(), LINK_KST_LOW);
        CHECK(g_hw.v.on);
        if (how == 0u) {
            CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
        } else {
            tick_as(how != 2u, how == 1u);
        }
        CHECK_EQ(state(), LINK_KST_PWM);
        CHECK_EQ(result(), KST_SES_ERR_ABORTED);
        CHECK_EQ(g_hw.offs, 1);
        CHECK(!g_hw.v.asked_on);
        CHECK_EQ(g_hw.gives, 1);
        CHECK_EQ(g_sim.n_frames, 0);
    }

    /* Found on: left on. */
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_LOW);
    run_ms(10);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(g_hw.sets, 0);
    CHECK(g_hw.v.asked_on);

    /* Before the pin is taken nothing was switched. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    run_ms(10);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(g_hw.sets, 0);

    /* In PROGRAMMING before the first frame. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    until_state(LINK_KST_PROGRAMMING);
    run_ms(10);
    tick_as(true, true);
    run_ms(20);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_frames, 0);
    CHECK_EQ(g_hw.offs, 1);
    CHECK(!g_hw.v.asked_on);
    conduct();
}

/* --- the supply cycled ----------------------------------------------------- */

TEST_CASE(a_power_cycle_with_a_pd_mini_is_run_by_the_port)
{
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK_EQ(g_hw.offs, 1);
    CHECK(!g_hw.v.asked_on);
    CHECK(flag(LINK_KS_F_BUSY));
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(result(), KST_SES_BUSY);
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << CH);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);

    /* Nothing is taken on the way, and a stop changes nothing. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    tick_as(true, true);
    tick_as(false, false);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);

    /* The off time counts from the reading, LAG_MS after the command. */
    run_ms(LAG_MS + KST_PORT_RAIL_OFF_MS - 5u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK_EQ(g_hw.ons, 0);
    until_state(LINK_KST_RAIL_WAIT);
    CHECK_EQ(g_hw.ons, 1);
    CHECK(g_hw.v.asked_on);
    /* At what it was set to. */
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, 2000);
    tick_as(true, true);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    run_ms(LAG_MS + KST_PORT_RAIL_WAIT_MS - 5u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK_EQ(g_hw.gives, 0);
    until_state(LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_hw.give_state, LINK_KST_PWM);
    CHECK_EQ(g_hw.sets, 2);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
    conduct();
}

TEST_CASE(a_reboot_from_pwm_cycles_the_supply_and_leaves_the_pin)
{
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    /* At once: no pulse runs out first. */
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK(flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.offs, 1);
    /* No pulse while the servo is without power, and the pin stays the
     * generator's. */
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);
    CHECK_EQ(LINK_KS_PORT_OF(reg(LINK_KS_STATE)), CH);

    run_ms(LAG_MS + KST_PORT_RAIL_OFF_MS - 5u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    until_state(LINK_KST_RAIL_WAIT);
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << CH);
    run_ms(LAG_MS + KST_PORT_RAIL_WAIT_MS - 5u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    until_state(LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
    CHECK_EQ(g_hw.takes, 0);
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(g_sim.n_frames, 0);

    /* The other port of the build. */
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, OTHER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 1u << OTHER);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
}

TEST_CASE(a_reboot_from_programming_waits_for_the_frame)
{
    unsigned frames;

    enter_with(SUPPLY_ON);
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    run_ms(40);
    until_frame();
    frames = g_sim.n_frames;
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK_EQ(result(), KST_SES_BUSY);
    tick();
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(g_hw.offs, 0);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    until_state(LINK_KST_RAIL_OFF);
    CHECK_EQ(g_sim.n_frames, frames);
    CHECK_EQ(g_hw.offs, 1);
    settle();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK(!flag(LINK_KS_F_MUST_READ));
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(g_sim.n_frames, frames);
    conduct();
}

TEST_CASE(a_reboot_is_refused_without_a_supply_that_is_on)
{
    fresh_at(0u, SUPPLY_NONE);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_SUPPLY);
    fresh_at(0u, SUPPLY_OFFLINE);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_SUPPLY);
    /* Off: the port switches no supply on that the bench has off. */
    fresh_at(0u, SUPPLY_OFF);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NO_SUPPLY);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_NONE);
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);

    /* Not without the heartbeat. */
    fresh_at(0u, SUPPLY_ON);
    tick_as(false, false);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_UNSAFE);
    CHECK_EQ(g_hw.sets, 0);
    tick();
    /* Not while the port moves. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    until_state(LINK_KST_LOW);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_BUSY);
    CHECK_EQ(g_hw.sets, 0);
}

TEST_CASE(a_reboot_whose_pd_mini_went_keeps_the_session)
{
    enter_with(SUPPLY_ON);
    read_all();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
    until_frame();
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    g_hw.v.online = false;
    settle();
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.sets, 0);
    CHECK_EQ(g_hw.gives, 0);
    /* The session goes on from a full read. */
    read_all();
    CHECK(!flag(LINK_KS_F_MUST_READ));
    conduct();
}

/* The supply switched off under a session: @p read_off for the PD mini's
 * own protection, which reads off while the page asks for on. */
static void supply_lost(bool read_off, bool mid_frame)
{
    unsigned frames = 0;

    enter_with(SUPPLY_ON);
    read_all();
    if (mid_frame) {
        CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, CH, 0, 0), 0);
        run_ms(40);
        until_frame();
        frames = g_sim.n_frames;
    }
    if (read_off) {
        g_hw.v.on = false;
        g_hw.v.off = true;
    } else {
        supply_asked_off();
    }
    tick();
    if (mid_frame) {
        /* The frame on the wire ends first. */
        CHECK_EQ(state(), LINK_KST_PROGRAMMING);
        until_state(LINK_KST_RAIL_OFF);
        CHECK_EQ(g_sim.n_frames, frames);
    }
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK_EQ(g_hw.offs, 1);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(kst_port_pin_mask(&g_port), 1u << CH);
    run_ms(LAG_MS + KST_PORT_RAIL_OFF_MS - 5u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(10);
    /* The servo was without power for the off time: no mode is left, and
     * the supply stays as the bench put it. */
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK(!flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
    if (mid_frame) {
        CHECK_EQ(g_sim.n_frames, frames);
    }
    conduct();
}

TEST_CASE(a_supply_switched_off_under_a_session_ends_it_in_pwm)
{
    supply_lost(false, false);
    supply_lost(false, true);
    supply_lost(true, false);
    supply_lost(true, true);
}

TEST_CASE(a_heartbeat_lost_in_the_off_time_leaves_the_supply_off)
{
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(500);
    /* Lost and back within the off time: the cycle goes on. */
    run_ms_as(300, false, false);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(1000);
    /* Lost when the off time ends. */
    run_ms_as(400, false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
    /* And stays off when the heartbeat is back. */
    run_ms(3000);
    CHECK_EQ(g_hw.ons, 0);
    CHECK_EQ(state(), LINK_KST_PWM);

    /* The same for a reboot from PWM. */
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    run_ms_as(LAG_MS + KST_PORT_RAIL_OFF_MS + 5u, false, false);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
}

TEST_CASE(a_supply_switched_off_in_the_settle_time_ends_the_cycle)
{
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    until_state(LINK_KST_RAIL_WAIT);
    run_ms(LAG_MS + 100u);
    /* By the panel, or by the SUPPLY page when the heartbeat is lost. */
    supply_asked_off();
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_hw.sets, 2);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
}

/* STOP on the panel sends ABORT: a cycle that is aborted ends with the
 * supply off, wherever it was. */
TEST_CASE(an_abort_in_a_supply_cycle_ends_it_with_the_supply_off)
{
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(500);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    /* The off time runs out as it would have. */
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(LAG_MS + KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_POWER_CYCLED);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
    run_ms(3000);
    CHECK_EQ(g_hw.ons, 0);

    /* In the settle time the supply is switched off again. */
    fresh_at(0u, SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    until_state(LINK_KST_RAIL_WAIT);
    CHECK(g_hw.v.asked_on);
    CHECK_EQ(cmd(LINK_KST_OP_ABORT, CH, 0, 0), 0);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(op(), LINK_KST_OP_REBOOT);
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!g_hw.v.asked_on);
    run_ms(3000);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(state(), LINK_KST_PWM);
}

TEST_CASE(a_supply_that_is_not_read_off_keeps_the_session)
{
    enter_with(SUPPLY_ON);
    read_all();
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(KST_PORT_SUPPLY_MS - 3u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(5);
    /* The servo may have power still, and with it the mode. */
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(flag(LINK_KS_F_IN_MODE));
    CHECK(flag(LINK_KS_F_MUST_READ));
    CHECK_EQ(g_hw.gives, 0);
    CHECK_EQ(g_hw.ons, 0);
    CHECK(!g_hw.v.asked_on);
    /* Nothing else ends it by itself. */
    run_ms(KST_PORT_SUPPLY_MS);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, CH, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_MUST_READ);
    /* The operator's word does. */
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);

    /* From PWM there is no session to keep. */
    fresh_at(0u, SUPPLY_ON);
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    run_ms(KST_PORT_SUPPLY_MS - 3u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    run_ms(5);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK_EQ(g_hw.ons, 0);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
}

TEST_CASE(a_supply_that_does_not_come_back_ends_the_cycle_off)
{
    /* The switch-on refused. */
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    tick();
    g_hw.set_ok = false;
    run_ms(LAG_MS + KST_PORT_RAIL_OFF_MS + 5u);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK(!flag(LINK_KS_F_BUSY));
    CHECK(!flag(LINK_KS_F_IN_MODE));
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);

    /* Taken, and not read on within the supply time. */
    enter_with(SUPPLY_ON);
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    until_state(LINK_KST_RAIL_WAIT);
    g_hw.follow = false;
    run_ms(KST_PORT_SUPPLY_MS - 3u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    run_ms(5);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.offs, 2);
    CHECK(!g_hw.v.asked_on);
    CHECK_EQ(g_hw.gives, 1);
}

TEST_CASE(a_driver_without_resources_returns_the_port_to_pwm)
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
    /* The pin take() was given is the generator's again. */
    CHECK_EQ(g_hw.gives, 1);
    CHECK_EQ(kst_port_quiet_mask(&g_port), 0);
    CHECK_EQ(kst_port_pin_mask(&g_port), 0);
    CHECK_EQ(g_sim.n_frames, 0);
}

TEST_CASE(a_driver_that_refuses_the_first_frame_returns_the_port_to_pwm)
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
    fresh_at(t0, SUPPLY_NONE);
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

    fresh_at(t0, SUPPLY_NONE);
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

/* The supply switched on in LOW: the settle time across the wrap. */
static void low_supply_case(uint32_t t0)
{
    const uint32_t low = t0 + KST_PORT_STOP_MS;

    fresh_at(t0, SUPPLY_OFF);
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    kst_port_step(&g_port, true, false, low);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + 1u);
    CHECK_EQ(g_hw.ons, 1);
    kst_port_step(&g_port, true, false, low + 300u);
    supply_reads();
    kst_port_step(&g_port, true, false, low + 301u);
    kst_port_step(&g_port, true, false,
                  low + 301u + KST_PORT_RAIL_WAIT_MS - 1u);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + 301u + KST_PORT_RAIL_WAIT_MS);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
}

TEST_CASE(the_settle_time_of_an_entry_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(low_supply_case, KST_PORT_STOP_MS + 150u);
    at_tick_0_and_before_the_wrap(low_supply_case, KST_PORT_STOP_MS + 800u);
}

static void low_timeout_case(uint32_t t0)
{
    const uint32_t low = t0 + KST_PORT_STOP_MS;

    fresh_at(t0, SUPPLY_OFF);
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, CH, 0, 0), 0);
    kst_port_step(&g_port, true, false, low);
    kst_port_step(&g_port, true, false, low + 1u);
    CHECK_EQ(g_hw.ons, 1);
    kst_port_step(&g_port, true, false, low + KST_PORT_SUPPLY_MS - 1u);
    CHECK_EQ(state(), LINK_KST_LOW);
    kst_port_step(&g_port, true, false, low + KST_PORT_SUPPLY_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    CHECK_EQ(g_hw.offs, 1);
}

TEST_CASE(the_supply_time_holds_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(low_timeout_case,
                                  KST_PORT_STOP_MS + 1500u);
}

static uint32_t g_rail_k;

/* The cycle's times, with the wrap g_rail_k ms after the supply went off. */
static void rail_time_case(uint32_t t0)
{
    uint32_t off, on;

    fresh_at(0u, SUPPLY_ON);
    enter();
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_POWER_CYCLED, CH, 0, 0), 0);
    off = t0 == 0u ? g_ms : t0;
    kst_port_step(&g_port, true, false, off);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK(!g_hw.v.asked_on);
    /* Read off 1 ms later: the off time counts from there. */
    supply_reads();
    off += 1u;
    kst_port_step(&g_port, true, false, off);
    kst_port_step(&g_port, true, false, off + KST_PORT_RAIL_OFF_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    CHECK(!g_hw.v.asked_on);
    kst_port_step(&g_port, true, false, off + KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    CHECK(g_hw.v.asked_on);
    /* Read on 1 ms later. */
    supply_reads();
    on = off + KST_PORT_RAIL_OFF_MS + 1u;
    kst_port_step(&g_port, true, false, on);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false, on + KST_PORT_RAIL_WAIT_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false, on + KST_PORT_RAIL_WAIT_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), KST_SES_OK);
}

TEST_CASE(the_rail_off_time_holds_across_the_tick_wrap)
{
    g_rail_k = 1000u;
    at_tick_0_and_before_the_wrap(rail_time_case, g_rail_k);
}

TEST_CASE(the_rail_wait_time_holds_across_the_tick_wrap)
{
    g_rail_k = KST_PORT_RAIL_OFF_MS + 500u;
    at_tick_0_and_before_the_wrap(rail_time_case, g_rail_k);
}

/* The supply times of a cycle that fails, across the wrap. */
static void rail_timeout_case(uint32_t t0)
{
    fresh_at(t0, SUPPLY_ON);
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    kst_port_step(&g_port, true, false, t0 + KST_PORT_SUPPLY_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_OFF);
    kst_port_step(&g_port, true, false, t0 + KST_PORT_SUPPLY_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);

    fresh_at(t0, SUPPLY_ON);
    g_hw.follow = false;
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, CH, 0, 0), 0);
    supply_reads();
    kst_port_step(&g_port, true, false, t0);
    kst_port_step(&g_port, true, false, t0 + KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false,
                  t0 + KST_PORT_RAIL_OFF_MS + KST_PORT_SUPPLY_MS - 1u);
    CHECK_EQ(state(), LINK_KST_RAIL_WAIT);
    kst_port_step(&g_port, true, false,
                  t0 + KST_PORT_RAIL_OFF_MS + KST_PORT_SUPPLY_MS);
    CHECK_EQ(state(), LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
}

TEST_CASE(the_supply_times_of_a_cycle_hold_across_the_tick_wrap)
{
    at_tick_0_and_before_the_wrap(rail_timeout_case, 1500u);
    at_tick_0_and_before_the_wrap(rail_timeout_case,
                                  KST_PORT_RAIL_OFF_MS + 1500u);
}

int main(void)
{
    RUN(a_port_starts_in_pwm_with_an_empty_page);
    RUN(null_arguments_and_a_port_without_hardware_are_refused);
    RUN(enter_stops_pwm_holds_the_pin_low_and_then_talks);
    RUN(read_all_puts_the_image_and_the_timing_on_the_page);
    RUN(enter_is_refused_for_anything_but_a_programming_port_with_a_pin);
    RUN(nothing_but_enter_and_abort_is_taken_in_pwm);
    RUN(a_command_frame_is_taken_whole_or_not_at_all);
    RUN(a_command_sent_again_is_acknowledged_and_does_nothing);
    RUN(the_sequence_counter_wraps_to_0);
    RUN(the_staged_registers_are_taken_in_any_windows);
    RUN(a_command_while_the_port_moves_or_works_is_busy);
    RUN(one_port_is_out_of_pwm_at_a_time);
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
    RUN(a_stop_before_the_first_frame_returns_the_port_to_pwm);
    RUN(a_stop_mid_frame_lets_the_frame_end_and_asks_for_a_full_read);
    RUN(a_lost_heartbeat_mid_frame_ends_the_session_the_same_way);
    RUN(a_frame_does_not_start_while_the_heartbeat_is_lost);
    RUN(a_stop_mid_write_locks_the_session);
    RUN(a_stop_between_operations_asks_for_a_full_read_too);
    RUN(abort_ends_the_operation_and_is_never_the_operation_named);
    RUN(pwm_returns_only_after_the_power_cycle_is_confirmed);
    RUN(a_power_cycle_confirmed_mid_frame_waits_for_the_frame);
    RUN(enter_switches_a_supply_that_is_off_on_at_the_staged_voltage);
    RUN(enter_leaves_a_supply_that_is_on_at_or_under_the_staged_voltage);
    RUN(enter_lowers_a_supply_that_is_on_above_the_staged_voltage);
    RUN(enter_is_refused_for_a_supply_outside_the_range);
    RUN(a_port_without_a_pd_mini_enters_as_it_did_and_asks_for_the_word);
    RUN(a_pd_mini_that_stops_answering_leaves_the_session_to_the_word);
    RUN(a_supply_that_does_not_come_on_ends_the_entry);
    RUN(an_entry_that_ends_early_leaves_the_supply_as_it_found_it);
    RUN(a_power_cycle_with_a_pd_mini_is_run_by_the_port);
    RUN(a_reboot_from_pwm_cycles_the_supply_and_leaves_the_pin);
    RUN(a_reboot_from_programming_waits_for_the_frame);
    RUN(a_reboot_is_refused_without_a_supply_that_is_on);
    RUN(a_reboot_whose_pd_mini_went_keeps_the_session);
    RUN(a_supply_switched_off_under_a_session_ends_it_in_pwm);
    RUN(a_heartbeat_lost_in_the_off_time_leaves_the_supply_off);
    RUN(a_supply_switched_off_in_the_settle_time_ends_the_cycle);
    RUN(an_abort_in_a_supply_cycle_ends_it_with_the_supply_off);
    RUN(a_supply_that_is_not_read_off_keeps_the_session);
    RUN(a_supply_that_does_not_come_back_ends_the_cycle_off);
    RUN(a_driver_without_resources_returns_the_port_to_pwm);
    RUN(a_driver_that_refuses_the_first_frame_returns_the_port_to_pwm);
    RUN(a_servo_that_does_not_answer_still_needs_the_power_cycle);
    RUN(the_frame_count_holds_at_4095);
    RUN(the_stop_time_holds_across_the_tick_wrap);
    RUN(the_low_time_holds_across_the_tick_wrap);
    RUN(the_settle_time_of_an_entry_holds_across_the_tick_wrap);
    RUN(the_supply_time_holds_across_the_tick_wrap);
    RUN(the_rail_off_time_holds_across_the_tick_wrap);
    RUN(the_rail_wait_time_holds_across_the_tick_wrap);
    RUN(the_supply_times_of_a_cycle_hold_across_the_tick_wrap);
    return test_summary("kst_port");
}
