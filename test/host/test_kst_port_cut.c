/*
 * An error while the KST page holds a port (protocol 4.31): the panel's OFF
 * of the supply, which the SUPPLY page holds back from the PD mini while
 * the port writes to the servo's memory (kst_port_writing(),
 * supply_page_port_writes()).
 *
 * The port is shared/outputs/kst_port.c against the simulated servo of
 * kst_sim.h; the SUPPLY page is the real one (shared/outputs/supply_page.c)
 * over a PD mini that reads back what it is asked in the same pass, wired
 * as firmware/iomcu/src/main.c wires them each pass: the port's step, the
 * page told of the hold, the frame and the write, then the page's step.
 * A pass is 100us on the wire's clock, every tenth 1ms on the port's.
 *
 * Under test: an OFF during a WRITE's planning switches the supply off at
 * once and nothing is written; an OFF in the middle of a WRITE, a RESTORE
 * and a RELEASE waits for every write frame and its read-backs, the servo
 * holding the target, and switches the supply off KST_PORT_WRITE_TAIL_MS
 * after the last read-back to the millisecond, cutting the full read
 * after it; an OFF in that tail the same, one after it at once; an OFF
 * during an ENTER, a READ_ALL, a VERIFY and the full read before a WRITE's
 * first write frame at once, the WRITE then writing nothing; the port then
 * keeps the supply off and returns to PWM; an OFF
 * during a REBOOT's off time refuses the cycle's ON, and the port lets go
 * in PWM, after which an ENTER switches the supply on again; the heartbeat
 * lost during a held OFF switches it off at once, within the frame wait.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "kst_plan.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "kst_sim.h"
#include "link_msg.h"
#include "outputs.h"
#include "pdmini.h"
#include "supply_page.h"

#define CH  0u
#define PIN 13u
#define KEY LINK_KST_WRITE_KEY
#define MV  5000u
#define MA  1000u

static sim_t         g_sim;
static kst_port_t    g_port;
static supply_page_t g_sp;
static outputs_t     g_out;
static pdmini_t      g_drv;
static uint32_t      g_ms;
static unsigned      g_sub;
static unsigned      g_ons;     /* passes the module was asked on        */

/* ------------------------------------------------------------- the hardware */

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port == CH ? PIN : 0xFFu;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    (void)port;
    (void)pin;
    return true;
}

static void hw_give(void *ctx)
{
    (void)ctx;
}

/* As firmware/iomcu/src/iomcu_kst.c: the SUPPLY page's view and request. */
static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    supply_page_port_view(&g_sp, out);
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    return supply_page_port_set(&g_sp, on, mv, ma, true);
}

/* ---------------------------------------------------------------- the page */

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

static bool busy(void)
{
    return (reg(LINK_KS_STATE) & LINK_KS_F_BUSY) != 0u;
}

static unsigned result(void)
{
    return reg(LINK_KS_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_KS_RESULT) >> 8;
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

static bool servo_is(const kst_image_t *img)
{
    return memcmp(g_sim.reg, img->r, sizeof(img->r)) == 0;
}

static uint8_t cmd(link_kst_op_t o, uint16_t key, uint16_t crc)
{
    const uint16_t in[4] = { LINK_KS_W_CMD_OF(o, CH),
                             (uint16_t)(reg(LINK_KS_SEQ) + 1u), key, crc };

    return kst_port_write(&g_port, 0, 4, in, g_ms);
}

/* Stage the image and the start CRC; their CRC. */
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

static uint16_t shown_crc(void)
{
    uint16_t r[LINK_KS_IMAGE_REGS];

    kst_port_read(&g_port, LINK_KS_IMAGE, LINK_KS_IMAGE_REGS, r);
    return link_kst_crc(r, LINK_KS_IMAGE_REGS);
}

/* --------------------------------------------------------------- the clock */

/* One pass of the IOmcu, as main.c orders it. */
static void tick_as(bool safe)
{
    kst_port_step(&g_port, safe, false, g_ms);
    supply_page_hold(&g_sp, kst_port_state(&g_port) != LINK_KST_PWM);
    supply_page_port_frame(&g_sp, kst_port_frame_out(&g_port), g_ms);
    supply_page_port_writes(&g_sp, kst_port_writing(&g_port));
    supply_page_step(&g_sp, safe, &g_drv);
    g_drv.st.output = g_drv.want_output;
    g_drv.st.set_mv = g_drv.want_mv;
    g_drv.st.set_ma = g_drv.want_ma;
    g_ons += g_drv.want_output ? 1u : 0u;
    g_sim.now_us += 100u;
    if (++g_sub == 10u) {
        g_sub = 0;
        g_ms++;
    }
}

static void tick(void)
{
    tick_as(true);
}

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < 10u * ms; ++i) {
        tick();
    }
}

static void settle(void)
{
    for (unsigned i = 0; i < 200000u && busy(); ++i) {
        tick();
    }
    CHECK(!busy());
}

static void until_state(link_kst_state_t s)
{
    for (unsigned i = 0; i < 200000u && state() != s; ++i) {
        tick();
    }
    CHECK_EQ(state(), s);
}

/* The panel's OFF, as error_act() has supply_link_cut() write it. */
static void panel_off(void)
{
    const uint16_t off = 0u;

    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_OUTPUT, 1u, &off, &g_out,
                               true), 0u);
}

/* ----------------------------------------------------------------- the bench */

/* The servo holding @p img, the page wired with its output off, the
 * port's ENTER sent (it switches the supply on). */
static void entering(const kst_image_t *img)
{
    static const uint16_t k_wiring[4] = { 1u, 8u, 9u, 1u };
    static const uint16_t k_supply[2] = { MV, MA };
    kst_port_hw_t hw;

    sim_init(&g_sim);
    if (img != NULL) {
        memcpy(g_sim.reg, img->r, sizeof(g_sim.reg));
    }
    outputs_init(&g_out, 0u);
    outputs_reserve_pins(&g_out, (uint64_t)1u << 3);
    supply_page_init(&g_sp);
    pdmini_init(&g_drv, NULL, 0u);
    g_drv.answered = true;
    g_drv.state_known = true;
    g_drv.st.online = true;
    CHECK_EQ(supply_page_write(&g_sp, LINK_SP_ENABLE, 4u, k_wiring, &g_out,
                               true), 0u);
    memset(&hw, 0, sizeof(hw));
    hw.ports = 1u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.supply = hw_supply;
    hw.supply_set = hw_supply_set;
    hw.driver = sim_driver(&g_sim);
    CHECK(kst_port_init(&g_port, &hw));
    g_ms = 0u;
    g_sub = 0u;
    tick();
    tick();
    CHECK_EQ(kst_port_write(&g_port, LINK_KS_W_SUPPLY_MV, 2, k_supply, g_ms),
             0);
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
}

/* The servo holding @p img, the page wired with its output off, the port
 * entered (it switches the supply on) and the servo read. */
static void up_holding(const kst_image_t *img)
{
    entering(img);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK_EQ(state(), LINK_KST_PROGRAMMING);
    CHECK(g_drv.want_output);
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
}

static void up(void)
{
    up_holding(NULL);
}

/* After the OFF reached the module: it stays off, the port returns to PWM
 * without switching it on, and lets go of the page. */
static void off_to_pwm(void)
{
    const unsigned ons = g_ons;

    CHECK(!g_drv.want_output);
    until_state(LINK_KST_PWM);
    run_ms(KST_PORT_RAIL_OFF_MS);
    CHECK_EQ(g_ons, ons);
    CHECK(!g_drv.want_output);
    CHECK_EQ(reg(LINK_KS_STATE) & LINK_KS_F_IN_MODE, 0u);
}

/* ---------------------------------------------------------------- the cases */

/* An edit of 86,312 units of planning, as test_kst_port has it. */
static const char k_long_a[] =
    "00f5f514487704eff4b20fc710091400969600010b000000000000e301100000";
static const char k_long_b[] =
    "00f5f51448770465cb840d8e66041400969600010b000000000000e301100000";

static kst_image_t image_of(const char *hex)
{
    kst_image_t img;

    for (unsigned i = 0; i < KST_REG_COUNT; ++i) {
        unsigned v = 0;

        (void)sscanf(hex + 2u * i, "%2x", &v);
        img.r[i] = (uint8_t)v;
    }
    return img;
}

TEST_CASE(an_off_while_a_write_plans_is_at_once_and_writes_nothing)
{
    const kst_image_t a = image_of(k_long_a);
    const kst_image_t b = image_of(k_long_b);

    up_holding(&a);
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, KEY,
                 stage(&b, ~(kst_field_set_t)0, shown_crc())), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    for (unsigned i = 0; i < 50u; ++i) {
        tick();
    }
    CHECK_EQ(result(), LINK_KST_RESULT_PLANNING);
    CHECK(!kst_port_writing(&g_port));
    panel_off();
    CHECK(!supply_page_off_held(&g_sp));
    tick();
    CHECK(!g_drv.want_output);
    settle();
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(servo_is(&a));
    off_to_pwm();
}

/* A held OFF: the module on through every write frame and read-back and
 * KST_PORT_WRITE_TAIL_MS after the last, to the millisecond of the port's
 * clock; the hold ends then and the module goes off within the frame
 * wait. */
static void held_to_tail(void)
{
    uint32_t last = g_ms;

    while (kst_session_writing(&g_port.session)) {
        CHECK(g_drv.want_output);
        tick();
    }
    CHECK(g_port.tail != 0u);
    while (kst_port_writing(&g_port)) {
        CHECK(g_drv.want_output);
        last = g_ms;
        tick();
    }
    CHECK_EQ(last - g_port.tail_t0, KST_PORT_WRITE_TAIL_MS);
    CHECK(!supply_page_off_held(&g_sp));
    /* The full read's frame on the wire: the frame wait as for any OFF. */
    for (unsigned i = 0; i < 10u * (SUPPLY_PAGE_OFF_WAIT_MS + 1u)
                         && g_drv.want_output;
         ++i) {
        tick();
    }
    CHECK(!g_drv.want_output);
}

/* An OFF while the port writes nothing: not held, the module off within
 * the frame wait (SUPPLY_PAGE_OFF_WAIT_MS). */
static void off_at_once(void)
{
    CHECK(!kst_port_writing(&g_port));
    panel_off();
    CHECK(!supply_page_off_held(&g_sp));
    for (unsigned i = 0; i < 10u * (SUPPLY_PAGE_OFF_WAIT_MS + 1u)
                         && g_drv.want_output;
         ++i) {
        tick();
    }
    CHECK(!g_drv.want_output);
}

/* The request of @p kind (0 WRITE, 1 RESTORE, 2 RELEASE) sent, on a bench
 * that is up; a RESTORE after a write, from the backup.  The image the
 * servo is to end with. */
static kst_image_t send_kind(unsigned kind)
{
    kst_image_t target = page_image();

    if (kind == 1u) {
        const kst_image_t backup = target;

        CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
        CHECK_EQ(cmd(LINK_KST_OP_WRITE, KEY, stage(&target, 0, shown_crc())),
                 0);
        settle();
        CHECK_EQ(result(), KST_SES_OK);
        CHECK_EQ(cmd(LINK_KST_OP_RESTORE, KEY, stage(&backup, 0, 0)), 0);
        return backup;
    }
    if (kind == 2u) {
        kst_plan_t plan;

        CHECK_EQ(kst_plan_release_pairing(&target, &plan), KST_PLAN_OK);
        CHECK_EQ(cmd(LINK_KST_OP_RELEASE, KEY,
                     stage(&target, 0, shown_crc())), 0);
        return plan.target;
    }
    CHECK(kst_field_edit(&target, KST_F_DEAD_BAND, 50));
    CHECK(kst_field_edit(&target, KST_F_BOOST,
                         (uint16_t)(kst_field_get(&target, KST_F_BOOST) - 1u)));
    CHECK_EQ(cmd(LINK_KST_OP_WRITE, KEY, stage(&target, 0, shown_crc())), 0);
    return target;
}

TEST_CASE(an_off_mid_write_waits_for_each_write_kind_to_end_verified)
{
    for (unsigned kind = 0; kind < 3u; ++kind) {
        kst_image_t target;
        unsigned writes;

        up();
        target = send_kind(kind);
        CHECK_EQ(refusal(), LINK_KST_REF_NONE);
        writes = g_sim.n_writes;
        /* Into the write: its first write frame has gone. */
        for (unsigned i = 0; i < 200000u && g_sim.n_writes == writes; ++i) {
            tick();
        }
        CHECK(g_sim.n_writes > writes);
        CHECK(kst_port_writing(&g_port));
        panel_off();
        CHECK_EQ(supply_page_off_held(&g_sp), true);
        held_to_tail();
        CHECK(servo_is(&target));
        /* The full read after the last write is cut. */
        settle();
        CHECK(result() != KST_SES_OK);
        off_to_pwm();
    }
}

/* The OFF in the tail after the last read-back: held to the tail's end.
 * An OFF after the tail, while the full read after the write runs: at
 * once. */
TEST_CASE(an_off_in_the_tail_waits_and_one_after_it_is_at_once)
{
    kst_image_t target;

    up();
    target = send_kind(0u);
    for (unsigned i = 0; i < 200000u && g_port.tail == 0u; ++i) {
        tick();
    }
    CHECK(servo_is(&target));
    CHECK(busy());
    CHECK(kst_port_writing(&g_port));
    panel_off();
    CHECK(supply_page_off_held(&g_sp));
    held_to_tail();
    off_to_pwm();

    up();
    target = send_kind(0u);
    for (unsigned i = 0; i < 200000u && g_port.tail == 0u; ++i) {
        tick();
    }
    run_ms(KST_PORT_WRITE_TAIL_MS);
    CHECK(busy());
    off_at_once();
    CHECK(servo_is(&target));
    off_to_pwm();
}

/* An OFF during an operation that writes nothing, at once: CONNECT's
 * ENTER, a READ_ALL, a VERIFY, and the full read before a WRITE's first
 * write frame, which then writes nothing. */
TEST_CASE(an_off_during_an_operation_that_writes_nothing_is_at_once)
{
    unsigned writes;
    kst_image_t img;

    entering(NULL);
    run_ms(20u);
    CHECK(busy());
    off_at_once();

    up();
    CHECK_EQ(cmd(LINK_KST_OP_READ_ALL, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    run_ms(20u);
    CHECK(busy());
    off_at_once();

    up();
    img = page_image();
    CHECK_EQ(cmd(LINK_KST_OP_VERIFY, 0, stage(&img, 0, 0)), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    run_ms(20u);
    CHECK(busy());
    off_at_once();

    up();
    writes = g_sim.n_writes;
    (void)send_kind(0u);
    for (unsigned i = 0; i < 200000u && g_port.session.planning != 0u; ++i) {
        tick();
    }
    run_ms(20u);
    CHECK(busy());
    CHECK_EQ(g_sim.n_writes, writes);
    off_at_once();
    settle();
    CHECK_EQ(g_sim.n_writes, writes);
}

/* A REBOOT's off time: the panel's OFF comes with the output already off,
 * and the cycle's ON is refused. */
TEST_CASE(an_off_in_a_power_cycle_refuses_its_on)
{
    up();
    CHECK_EQ(cmd(LINK_KST_OP_REBOOT, 0, 0), 0);
    CHECK_EQ(refusal(), LINK_KST_REF_NONE);
    until_state(LINK_KST_RAIL_OFF);
    tick();
    CHECK(!g_drv.want_output);
    panel_off();
    const unsigned ons = g_ons;
    until_state(LINK_KST_PWM);
    CHECK_EQ(result(), LINK_KST_RESULT_SUPPLY);
    run_ms(2u * KST_PORT_RAIL_WAIT_MS);
    CHECK_EQ(g_ons, ons);
    CHECK(!g_drv.want_output);
    /* In PWM the port let go: the next ENTER switches the supply on. */
    CHECK_EQ(cmd(LINK_KST_OP_ENTER, 0, 0), 0);
    settle();
    CHECK_EQ(result(), KST_SES_OK);
    CHECK(g_drv.want_output);
}

/* The heartbeat lost, or the link silent, during a held OFF: off within
 * the frame wait, and the session cut. */
TEST_CASE(a_lost_heartbeat_during_a_held_off_cuts_at_once)
{
    unsigned writes;

    up();
    writes = g_sim.n_writes;
    (void)send_kind(0u);
    for (unsigned i = 0; i < 200000u && g_sim.n_writes == writes; ++i) {
        tick();
    }
    panel_off();
    run_ms(5u);
    CHECK(g_drv.want_output);
    CHECK(supply_page_off_held(&g_sp));
    for (unsigned i = 0; i < 10u * (SUPPLY_PAGE_OFF_WAIT_MS + 1u); ++i) {
        tick_as(false);
    }
    CHECK(!g_drv.want_output);
    CHECK(!supply_page_off_held(&g_sp));
    for (unsigned i = 0; i < 200000u && busy(); ++i) {
        tick_as(false);
    }
    CHECK_EQ(result(), KST_SES_ERR_ABORTED);
    CHECK(!g_drv.want_output);
}

int main(void)
{
    RUN(an_off_while_a_write_plans_is_at_once_and_writes_nothing);
    RUN(an_off_mid_write_waits_for_each_write_kind_to_end_verified);
    RUN(an_off_in_the_tail_waits_and_one_after_it_is_at_once);
    RUN(an_off_during_an_operation_that_writes_nothing_is_at_once);
    RUN(an_off_in_a_power_cycle_refuses_its_on);
    RUN(a_lost_heartbeat_during_a_held_off_cuts_at_once);
    return test_summary("kst_port_cut");
}
