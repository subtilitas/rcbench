/*
 * Protocol 4.41: 16 slots and 16 channels over the output pages and their
 * _HI halves (shared/link/include/link_pages.h), the binding prepared and
 * taken over both halves (shared/outputs/out_stage.c, bind_link.c), and a
 * 4.40 end against a 4.41 one.
 *
 * The far end is a link_dev serving the output pages as
 * firmware/iomcu/src/iomcu_pages.c and iomcu_bind.c do, less the rules
 * that need its silicon: a slot the bank refuses (a pin two slots name)
 * stands for every whole-page refusal.  Replies cross the CAN mapping
 * frame by frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bind_link.h"
#include "link_can.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "out_bind.h"
#include "out_stage.h"
#include "outputs_pages.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)

/* ------------------------------------------------------------ the IOmcu */

typedef struct {
    uint16_t    ch[LINK_CH_ALL];      /* CHANNELS, CHANNELS_HI          */
    uint16_t    cfg[LINK_CC_ALL];     /* CHAN_CFG, CHAN_CFG_HI in force */
    uint16_t    slots[LINK_OS_ALL];   /* OUTPUTS, OUTPUTS_HI in force   */
    out_stage_t stage;                /* the four prepared pages        */
    bool        armed;
    unsigned    takes;                /* pages put in force             */
} far_t;

static far_t       far;
static outputs_t   s_bank;
static link_dev_t  g_dev;
static link_host_t g_host;
static uint32_t    g_now;
static unsigned    g_exchanges;
static uint8_t     g_page[64];        /* the page of each request       */

static uint8_t take_cfg(void *ctx, const uint16_t *next,
                        const uint16_t *slots)
{
    far_t *f = (far_t *)ctx;
    (void)slots;
    if (outputs_chan_cfg_armed_check(f->cfg, next, f->armed) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(f->cfg, next, sizeof(f->cfg));
    outputs_chan_cfg_apply(&s_bank, f->cfg);
    ++f->takes;
    return 0u;
}

static uint8_t take_slots(void *ctx, const uint16_t *next)
{
    far_t *f = (far_t *)ctx;
    if (outputs_slots_armed_check(f->slots, next, f->armed) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    static outputs_t trial;
    trial = s_bank;
    outputs_slots_apply(&trial, next);
    for (unsigned s = 0; s < LINK_OUT_SLOTS_ALL; ++s) {
        if (next[s * LINK_OS_STRIDE + LINK_OS_DRIVER] != LINK_DRIVER_NONE
            && trial.slot[s].driver == OUT_DRIVER_NONE) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    s_bank = trial;
    memcpy(f->slots, next, sizeof(f->slots));
    ++f->takes;
    return 0u;
}

static void put_cfg(void *ctx, const uint16_t *prev)
{
    far_t *f = (far_t *)ctx;
    memcpy(f->cfg, prev, sizeof(f->cfg));
    outputs_chan_cfg_apply(&s_bank, f->cfg);
}

static const out_stage_ops_t k_ops = {
    take_cfg, take_slots, put_cfg, NULL, &s_bank,
};

static void win(const uint16_t *r, uint8_t off, uint8_t n, uint16_t *out)
{
    memcpy(out, &r[off], (size_t)n * sizeof(uint16_t));
}

static uint8_t ch_put(unsigned base, uint8_t off, uint8_t n,
                      const uint16_t *in)
{
    const uint8_t nack = outputs_channels_write(&far.ch[base], off, n, in);
    if (nack == 0u) {
        outputs_channels_apply_n(&s_bank, far.ch, (uint8_t)(base + off), n,
                                 g_now);
    }
    return nack;
}

static uint8_t cfg_put(unsigned base, uint8_t off, uint8_t n,
                       const uint16_t *in)
{
    uint16_t next[LINK_CC_ALL];
    memcpy(next, far.cfg, sizeof(next));
    const uint8_t nack = outputs_chan_cfg_write(&next[base], off, n, in);
    return (nack != 0u) ? nack : take_cfg(&far, next, far.slots);
}

static uint8_t slots_put(unsigned base, uint8_t off, uint8_t n,
                         const uint16_t *in)
{
    uint16_t next[LINK_OS_ALL];
    memcpy(next, far.slots, sizeof(next));
    const uint8_t nack = outputs_slots_write(&next[base], off, n, in);
    return (nack != 0u) ? nack : take_slots(&far, next);
}

#define HALVES(name, rd, wr)                                                  \
    static void name##_lo_rd(void *c, uint8_t o, uint8_t n, uint16_t *out)    \
    { (void)c; rd(false, o, n, out); }                                        \
    static void name##_hi_rd(void *c, uint8_t o, uint8_t n, uint16_t *out)    \
    { (void)c; rd(true, o, n, out); }                                         \
    static uint8_t name##_lo_wr(void *c, uint8_t o, uint8_t n,                \
                                const uint16_t *in)                           \
    { (void)c; return wr(false, o, n, in); }                                  \
    static uint8_t name##_hi_wr(void *c, uint8_t o, uint8_t n,                \
                                const uint16_t *in)                           \
    { (void)c; return wr(true, o, n, in); }

static void ch_rd(bool hi, uint8_t o, uint8_t n, uint16_t *out)
{ win(&far.ch[hi ? LINK_CH_COUNT : 0u], o, n, out); }
static uint8_t ch_wr(bool hi, uint8_t o, uint8_t n, const uint16_t *in)
{ return ch_put(hi ? LINK_CH_COUNT : 0u, o, n, in); }
static void cfg_rd(bool hi, uint8_t o, uint8_t n, uint16_t *out)
{ win(&far.cfg[hi ? LINK_CC_COUNT : 0u], o, n, out); }
static uint8_t cfg_wr(bool hi, uint8_t o, uint8_t n, const uint16_t *in)
{ return cfg_put(hi ? LINK_CC_COUNT : 0u, o, n, in); }
static void os_rd(bool hi, uint8_t o, uint8_t n, uint16_t *out)
{ win(&far.slots[hi ? LINK_OS_COUNT : 0u], o, n, out); }
static uint8_t os_wr(bool hi, uint8_t o, uint8_t n, const uint16_t *in)
{ return slots_put(hi ? LINK_OS_COUNT : 0u, o, n, in); }
static void bcfg_rd(bool hi, uint8_t o, uint8_t n, uint16_t *out)
{ out_stage_cfg_read(&far.stage, hi, o, n, out); }
static uint8_t bcfg_wr(bool hi, uint8_t o, uint8_t n, const uint16_t *in)
{ return out_stage_cfg_write(&far.stage, hi, o, n, in); }
static void bos_rd(bool hi, uint8_t o, uint8_t n, uint16_t *out)
{ out_stage_slots_read(&far.stage, hi, o, n, out); }
static uint8_t bos_wr(bool hi, uint8_t o, uint8_t n, const uint16_t *in)
{ return out_stage_slots_write(&far.stage, hi, o, n, in); }

HALVES(ch, ch_rd, ch_wr)
HALVES(cfg, cfg_rd, cfg_wr)
HALVES(os, os_rd, os_wr)
HALVES(bcfg, bcfg_rd, bcfg_wr)
HALVES(bos, bos_rd, bos_wr)

static void bind_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned r = (unsigned)off + i;
        out[i] = (r == LINK_BD_COMMIT) ? out_stage_held_crc(&far.stage)
                 : (r == LINK_BD_COMMIT_ALL)
                     ? out_stage_held_crc_all(&far.stage) : 0u;
    }
}

static uint8_t bind_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    if (n != 1u) {
        return LINK_NACK_BAD_RANGE;
    }
    if (off == LINK_BD_COMMIT) {
        return out_stage_commit(&far.stage, in[0], far.cfg, far.slots,
                                &k_ops, &far);
    }
    if (off == LINK_BD_COMMIT_ALL) {
        return out_stage_commit_all(&far.stage, in[0], far.cfg, far.slots,
                                    &k_ops, &far);
    }
    return LINK_NACK_BAD_RANGE;
}

static void servo_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    (void)off;
    memset(out, 0, (size_t)n * sizeof(uint16_t));
}

static uint8_t servo_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return (off == LINK_SV_FRAME_HZ && n == 1u && in[0] == 0u)
               ? 0u : (uint8_t)LINK_NACK_BAD_VALUE;
}

/* A 4.41 IOmcu serves all twelve; a 4.40 one the first seven, with a BIND
 * page of LINK_BD_COUNT_V440 registers (fresh()). */
static link_page_t k_pages[] = {
    { LINK_PAGE_SERVO,       LINK_SV_COUNT, servo_rd,   servo_wr },
    { LINK_PAGE_CHANNELS,    LINK_CH_COUNT, ch_lo_rd,   ch_lo_wr },
    { LINK_PAGE_CHAN_CFG,    LINK_CC_COUNT, cfg_lo_rd,  cfg_lo_wr },
    { LINK_PAGE_OUTPUTS,     LINK_OS_COUNT, os_lo_rd,   os_lo_wr },
    { LINK_PAGE_BIND_CFG,    LINK_CC_COUNT, bcfg_lo_rd, bcfg_lo_wr },
    { LINK_PAGE_BIND_OUT,    LINK_OS_COUNT, bos_lo_rd,  bos_lo_wr },
    { LINK_PAGE_BIND,        LINK_BD_COUNT, bind_rd,    bind_wr },
    { LINK_PAGE_CHANNELS_HI, LINK_CH_COUNT, ch_hi_rd,   ch_hi_wr },
    { LINK_PAGE_CHAN_CFG_HI, LINK_CC_COUNT, cfg_hi_rd,  cfg_hi_wr },
    { LINK_PAGE_OUTPUTS_HI,  LINK_OS_COUNT, os_hi_rd,   os_hi_wr },
    { LINK_PAGE_BIND_CFG_HI, LINK_CC_COUNT, bcfg_hi_rd, bcfg_hi_wr },
    { LINK_PAGE_BIND_OUT_HI, LINK_OS_COUNT, bos_hi_rd,  bos_hi_wr },
};

/* ---------------------------------------------------------------- the port */

static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    static link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    link_msg_t ans;
    (void)ctx;
    if (g_exchanges < sizeof(g_page)) {
        g_page[g_exchanges] = req->page;
    }
    ++g_exchanges;
    if (!link_dev_dispatch(&g_dev, req, &ans, g_now)) {
        link_host_abandon(h);
        return false;
    }
    const size_t m = link_can_encode(&ans, f, LINK_CAN_MAX_FRAMES);
    bool whole = false;
    for (size_t i = 0u; i < m && !whole; ++i) {
        link_msg_t part;
        whole = link_can_decode(&f[i], &part)
                && link_host_accept(h, &part, g_now, reply);
    }
    return whole;
}

static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return g_now;
}

static const link_port_t k_port = { port_exchange, port_now, NULL, NULL };

/* An IOmcu of protocol 4.@p minor with nothing bound. */
static void fresh(uint16_t minor)
{
    memset(&far, 0, sizeof(far));
    g_now = 1000u;
    outputs_chan_cfg_defaults(far.cfg);
    outputs_chan_cfg_defaults(&far.cfg[LINK_CC_COUNT]);
    outputs_init(&s_bank, g_now);
    outputs_chan_cfg_apply(&s_bank, far.cfg);
    out_stage_init(&far.stage, far.cfg, far.slots);
    const bool hi = minor >= LINK_MINOR_OUT16;
    k_pages[6].count = hi ? (uint8_t)LINK_BD_COUNT : LINK_BD_COUNT_V440;
    link_dev_init(&g_dev, k_pages, hi ? 12u : 7u, &far, g_now);
    link_host_init(&g_host, g_now);
    g_exchanges = 0u;
    memset(g_page, 0, sizeof(g_page));
}

/* A write through the link; the reply's op, or the NACK's reason. */
static uint16_t put(uint8_t page, uint8_t off, uint8_t n, const uint16_t *v)
{
    link_msg_t reply;
    if (!link_write_acked(&g_host, &k_port, page, off, n, v, &reply)) {
        return 0xFFFFu;
    }
    return (reply.op == LINK_OP_ACK) ? 0u : reply.regs[0];
}

static void slot_of(uint16_t *r, uint16_t pin, uint8_t first, uint8_t n)
{
    r[LINK_OS_DRIVER]  = (n == 1u) ? LINK_DRIVER_PWM : LINK_DRIVER_PPM;
    r[LINK_OS_PIN]     = pin;
    r[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(first, n);
    r[LINK_OS_RATE_HZ] = (n == 1u) ? 50u : 40u;
}

static bool page_asked(uint8_t page)
{
    for (unsigned i = 0; i < g_exchanges && i < sizeof(g_page); ++i) {
        if (g_page[i] == page) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------- the page rules */

/* Channel 8 is the last register of CHANNELS, channel 9 the first of
 * CHANNELS_HI, channel 16 its last; channel 17 is no register. */
TEST_CASE(the_channels_run_on_from_one_page_to_the_other)
{
    fresh(41u);
    const uint16_t c8 = 600u, c9 = 700u, c16 = 800u;
    CHECK_EQ(put(LINK_PAGE_CHANNELS, 7u, 1u, &c8), 0u);
    CHECK_EQ(put(LINK_PAGE_CHANNELS_HI, 0u, 1u, &c9), 0u);
    CHECK_EQ(put(LINK_PAGE_CHANNELS_HI, 7u, 1u, &c16), 0u);
    CHECK_EQ(outputs_command(&s_bank, 7u), 600u);
    CHECK_EQ(outputs_command(&s_bank, 8u), 700u);
    CHECK_EQ(outputs_command(&s_bank, 15u), 800u);
    CHECK_EQ(s_bank.channel[15].last_command_ms, g_now);
    /* Past the end of either page, nothing of the frame is taken. */
    const uint16_t two[2] = { 100u, 100u };
    CHECK_EQ(put(LINK_PAGE_CHANNELS, 7u, 2u, two), LINK_NACK_BAD_RANGE);
    CHECK_EQ(put(LINK_PAGE_CHANNELS_HI, 8u, 1u, &c8), LINK_NACK_BAD_RANGE);
    CHECK_EQ(outputs_command(&s_bank, 7u), 600u);

    /* CHAN_CFG_HI: channel 16 is registers 28 to 31, the page's last. */
    const uint16_t ch16[4] = { LINK_CC_ROLE_SURFACE, 0u, 900u, 2100u };
    CHECK_EQ(put(LINK_PAGE_CHAN_CFG_HI, 28u, 4u, ch16), 0u);
    CHECK_EQ(s_bank.channel[15].min_us, 900u);
    CHECK_EQ(s_bank.channel[15].max_us, 2100u);
    CHECK_EQ(s_bank.channel[7].max_us, 2000u);
    const uint16_t low = 399u;
    CHECK_EQ(put(LINK_PAGE_CHAN_CFG_HI, 30u, 1u, &low), LINK_NACK_BAD_VALUE);
    link_msg_t r;
    CHECK(link_read_window(&g_host, &k_port, LINK_PAGE_CHAN_CFG_HI, 28u, 4u,
                           &r));
    CHECK(memcmp(r.regs, ch16, sizeof(ch16)) == 0);
}

/* A slot of either page renders any run of channels 1 to 16; a run that
 * reaches channel 17 is refused. */
TEST_CASE(a_slot_renders_up_to_channel_16_and_no_further)
{
    uint16_t page[LINK_OS_COUNT] = { 0 };
    uint16_t r[LINK_OS_STRIDE];
    slot_of(r, 20u, 15u, 1u);                      /* channel 16 */
    CHECK_EQ(outputs_slots_write(page, 0u, 4u, r), 0u);
    slot_of(r, 20u, 16u, 1u);                      /* channel 17 */
    CHECK_EQ(outputs_slots_write(page, 0u, 4u, r), LINK_NACK_BAD_VALUE);
    slot_of(r, 20u, 8u, 8u);                       /* 9 to 16 */
    CHECK_EQ(outputs_slots_write(page, 0u, 4u, r), 0u);
    slot_of(r, 20u, 9u, 8u);                       /* 10 to 17 */
    CHECK_EQ(outputs_slots_write(page, 0u, 4u, r), LINK_NACK_BAD_VALUE);
    CHECK_EQ(page[LINK_OS_RANGE], LINK_OS_RANGE_OF(8u, 8u));

    fresh(41u);
    /* Slot 8 on channel 8, slot 9 on channel 9, slot 16 on channel 16. */
    slot_of(r, 7u, 7u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS, 28u, 4u, r), 0u);
    slot_of(r, 8u, 8u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 0u, 4u, r), 0u);
    slot_of(r, 20u, 15u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 28u, 4u, r), 0u);
    CHECK_EQ(s_bank.slot[7].first_channel, 7u);
    CHECK_EQ(s_bank.slot[8].first_channel, 8u);
    CHECK_EQ(s_bank.slot[15].first_channel, 15u);
    CHECK_EQ(s_bank.slot[15].pin, 20u);
    CHECK(!outputs_configure(&s_bank, (uint8_t)OUT_MAX_SLOTS, &s_bank.slot[0]));
    /* Past channel 16, and a pin slot 8 already has. */
    slot_of(r, 9u, 16u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 4u, 4u, r), LINK_NACK_BAD_VALUE);
    slot_of(r, 7u, 9u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 4u, 4u, r), LINK_NACK_BAD_VALUE);
    CHECK_EQ(far.slots[LINK_OS_COUNT + 4u + LINK_OS_DRIVER], 0u);
    CHECK_EQ(s_bank.slot[9].driver, OUT_DRIVER_NONE);
    /* A run across the two pages' channels, 8 and 9, in slot 9 once
     * slot 8 lets channel 8 go. */
    memset(r, 0, sizeof(r));
    CHECK_EQ(put(LINK_PAGE_OUTPUTS, 28u, 4u, r), 0u);
    slot_of(r, 8u, 7u, 2u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 0u, 4u, r), 0u);
    CHECK_EQ(s_bank.slot[8].driver, OUT_DRIVER_PPM);
    CHECK_EQ(s_bank.slot[8].first_channel, 7u);
    CHECK_EQ(s_bank.slot[8].channels, 2u);
}

/* What judges a write sees all 16 slots and all 16 channels. */
TEST_CASE(the_bank_is_judged_over_both_halves)
{
    uint16_t a[LINK_OS_ALL] = { 0 }, b[LINK_OS_ALL] = { 0 };
    b[15u * LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 50u;
    CHECK_EQ(outputs_slots_changed(a, b), 0x8000u);
    CHECK_EQ(outputs_slots_armed_check(a, b, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(outputs_slots_armed_check(a, a, true), 0u);
    CHECK_EQ(outputs_slots_armed_check(a, b, false), 0u);

    uint16_t was[LINK_CC_ALL], now[LINK_CC_ALL];
    outputs_chan_cfg_defaults(was);
    outputs_chan_cfg_defaults(&was[LINK_CC_COUNT]);
    memcpy(now, was, sizeof(now));
    now[15u * LINK_CC_STRIDE + LINK_CC_ROLE] = LINK_CC_ROLE_THROTTLE;
    CHECK_EQ(outputs_chan_cfg_armed_check(was, now, true),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(outputs_chan_cfg_armed_check(was, was, true), 0u);

    outputs_t o;
    outputs_init(&o, 0u);
    outputs_chan_cfg_apply(&o, now);
    CHECK_EQ(o.channel[15].role, OUT_ROLE_THROTTLE);
    slot_of(&a[15u * LINK_OS_STRIDE], 20u, 15u, 1u);
    outputs_slots_apply(&o, a);
    CHECK_EQ(o.slot[15].driver, OUT_DRIVER_PWM);
    CHECK_EQ(outputs_bind_check(&o, 0x8000u, 0x0000u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(outputs_bind_check(&o, 0x8000u, 0x8000u), 0u);
    CHECK_EQ(outputs_bind_check(&o, 0x7FFFu, 0x0000u), 0u);
    /* A kept slot 16 with a pin no write takes starts unbound. */
    a[15u * LINK_OS_STRIDE + LINK_OS_PIN] = 64u;
    CHECK_EQ(outputs_slots_free_unwritable(a), 1u);
    CHECK_EQ(a[15u * LINK_OS_STRIDE + LINK_OS_DRIVER], 0u);
    uint16_t cmd[LINK_CH_ALL];
    outputs_channels_from_bank(&o, cmd);
    CHECK_EQ(cmd[15], outputs_command(&o, 15u));
}

/* ------------------------------------------------------------- the binding */

/* The panel's sequence to a 4.41 IOmcu: four pages and COMMIT_ALL. */
TEST_CASE(a_binding_is_prepared_and_taken_over_both_halves)
{
    fresh(41u);
    uint16_t cfg[LINK_CC_ALL], slots[LINK_OS_ALL] = { 0 };
    outputs_chan_cfg_defaults(cfg);
    outputs_chan_cfg_defaults(&cfg[LINK_CC_COUNT]);
    cfg[15u * LINK_CC_STRIDE + LINK_CC_MIN_US] = 900u;
    slot_of(&slots[0], 2u, 0u, 1u);
    slot_of(&slots[8u * LINK_OS_STRIDE], 10u, 8u, 1u);
    slot_of(&slots[15u * LINK_OS_STRIDE], 17u, 15u, 1u);
    CHECK_EQ(bind_link_write(&g_host, &k_port, 41u, cfg, slots, NULL),
             BIND_WRITTEN);
    /* The rate reset, 8 frames to each of four pages, the commit. */
    CHECK_EQ(g_exchanges, 34u);
    CHECK(page_asked(LINK_PAGE_BIND_CFG_HI));
    CHECK(page_asked(LINK_PAGE_BIND_OUT_HI));
    CHECK_EQ(far.takes, 2u);
    CHECK(memcmp(far.slots, slots, sizeof(slots)) == 0);
    CHECK(memcmp(far.cfg, cfg, sizeof(cfg)) == 0);
    CHECK_EQ(s_bank.slot[8].pin, 10u);
    CHECK_EQ(s_bank.slot[15].pin, 17u);
    CHECK_EQ(s_bank.channel[15].min_us, 900u);
    /* COMMIT_ALL reads the CRC of the four, COMMIT of the first two. */
    link_msg_t r;
    CHECK(link_read_window(&g_host, &k_port, LINK_PAGE_BIND, 0u, 3u, &r));
    CHECK_EQ(r.regs[LINK_BD_COMMIT], out_stage_crc(cfg, slots));
    CHECK_EQ(r.regs[LINK_BD_COMMIT_ALL], out_stage_crc_all(cfg, slots));

    /* A CRC that is not the four's changes nothing. */
    const uint16_t bad = (uint16_t)(out_stage_crc_all(cfg, slots) + 1u);
    CHECK_EQ(put(LINK_PAGE_BIND, LINK_BD_COMMIT_ALL, 1u, &bad),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(far.takes, 2u);

    /* Slot 9 prepared on slot 1's pin, channel 16 at 950us: the OUTPUTS
     * pages are refused, and the CHAN_CFG pages go back with them. */
    uint16_t clash[LINK_OS_STRIDE];
    slot_of(clash, 2u, 8u, 1u);
    CHECK_EQ(put(LINK_PAGE_BIND_OUT_HI, 0u, 4u, clash), 0u);
    const uint16_t min950 = 950u;
    CHECK_EQ(put(LINK_PAGE_BIND_CFG_HI, 30u, 1u, &min950), 0u);
    const uint16_t crc = out_stage_held_crc_all(&far.stage);
    CHECK_EQ(put(LINK_PAGE_BIND, LINK_BD_COMMIT_ALL, 1u, &crc),
             LINK_NACK_BAD_VALUE);
    CHECK(memcmp(far.slots, slots, sizeof(slots)) == 0);
    CHECK(memcmp(far.cfg, cfg, sizeof(cfg)) == 0);
    CHECK_EQ(s_bank.channel[15].min_us, 900u);

    /* COMMIT takes the first two prepared pages alone: what BIND_OUT_HI
     * and BIND_CFG_HI hold is not taken, the _HI halves stay in force. */
    const uint16_t lo = out_stage_held_crc(&far.stage);
    CHECK_EQ(put(LINK_PAGE_BIND, LINK_BD_COMMIT, 1u, &lo), 0u);
    CHECK(memcmp(far.slots, slots, sizeof(slots)) == 0);
    CHECK_EQ(far.cfg[15u * LINK_CC_STRIDE + LINK_CC_MIN_US], 900u);
    /* And a commit is one register at 0 or 2. */
    const uint16_t two[2] = { lo, lo };
    CHECK_EQ(put(LINK_PAGE_BIND, 0u, 2u, two), LINK_NACK_BAD_RANGE);
    CHECK_EQ(put(LINK_PAGE_BIND, LINK_BD_WHY, 1u, &lo), LINK_NACK_BAD_RANGE);
}

/* A 4.40 panel commits CHAN_CFG and OUTPUTS: slots 9 to 16 and channels 9
 * to 16 stay as they are, and its read sees the first halves alone. */
TEST_CASE(a_4_40_panel_leaves_the_hi_halves_of_a_4_41_iomcu)
{
    fresh(41u);
    uint16_t r[LINK_OS_STRIDE];
    slot_of(r, 10u, 8u, 1u);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 0u, 4u, r), 0u);
    uint16_t cfg[LINK_CC_ALL], slots[LINK_OS_ALL] = { 0 };
    outputs_chan_cfg_defaults(cfg);
    outputs_chan_cfg_defaults(&cfg[LINK_CC_COUNT]);
    slot_of(&slots[0], 2u, 0u, 1u);
    g_exchanges = 0u;
    CHECK_EQ(bind_link_write(&g_host, &k_port, 40u, cfg, slots, NULL),
             BIND_WRITTEN);
    CHECK_EQ(g_exchanges, 18u);
    CHECK(!page_asked(LINK_PAGE_BIND_CFG_HI));
    CHECK(!page_asked(LINK_PAGE_BIND_OUT_HI));
    CHECK_EQ(s_bank.slot[0].pin, 2u);
    CHECK_EQ(s_bank.slot[8].pin, 10u);
    CHECK_EQ(far.slots[LINK_OS_COUNT + LINK_OS_PIN], 10u);

    /* Read as a 4.40 panel reads: two pages, and a binding of slot 1. */
    bind_reading_t rd;
    g_exchanges = 0u;
    CHECK_EQ(bind_link_read(&g_host, &k_port, 40u, BOARD, &rd),
             BIND_READ_OK);
    CHECK(!page_asked(LINK_PAGE_OUTPUTS_HI));
    CHECK_EQ(rd.slots[LINK_OS_COUNT + LINK_OS_DRIVER], 0u);
    /* As this build reads a 4.41 IOmcu: slot 9 bound, which the binding
     * model of slots 1 to 8 does not describe. */
    CHECK_EQ(bind_link_read(&g_host, &k_port, 41u, BOARD, &rd),
             BIND_READ_ODD);
    CHECK(page_asked(LINK_PAGE_OUTPUTS_HI));
    CHECK(page_asked(LINK_PAGE_CHAN_CFG_HI));
    CHECK_EQ(rd.slots[LINK_OS_COUNT + LINK_OS_PIN], 10u);
}

/* This build against a 4.40 IOmcu: the minor it reports keeps every
 * _HI page and COMMIT_ALL off the wire. */
TEST_CASE(a_4_41_panel_sends_a_4_40_iomcu_nothing_it_lacks)
{
    fresh(40u);
    uint16_t cfg[LINK_CC_ALL], slots[LINK_OS_ALL] = { 0 };
    outputs_chan_cfg_defaults(cfg);
    outputs_chan_cfg_defaults(&cfg[LINK_CC_COUNT]);
    slot_of(&slots[0], 2u, 0u, 1u);
    CHECK_EQ(bind_link_write(&g_host, &k_port, 40u, cfg, slots, NULL),
             BIND_WRITTEN);
    CHECK_EQ(g_exchanges, 18u);
    CHECK_EQ(s_bank.slot[0].pin, 2u);
    /* A binding with slot 9 bound is refused before anything is sent. */
    slot_of(&slots[8u * LINK_OS_STRIDE], 10u, 8u, 1u);
    g_exchanges = 0u;
    CHECK_EQ(bind_link_write(&g_host, &k_port, 40u, cfg, slots, NULL),
             BIND_REFUSED);
    CHECK_EQ(g_exchanges, 0u);
    CHECK_EQ(s_bank.slot[8].driver, OUT_DRIVER_NONE);
    /* What a 4.40 IOmcu answers the new pages and register. */
    const uint16_t v = 0u;
    CHECK_EQ(put(LINK_PAGE_CHANNELS_HI, 0u, 1u, &v), LINK_NACK_BAD_PAGE);
    CHECK_EQ(put(LINK_PAGE_OUTPUTS_HI, 0u, 1u, &v), LINK_NACK_BAD_PAGE);
    CHECK_EQ(put(LINK_PAGE_BIND, LINK_BD_COMMIT_ALL, 1u, &v),
             LINK_NACK_BAD_RANGE);
    /* The read asks for the two pages it has; the _HI halves read as no
     * slot and CHAN_CFG_HI's defaults. */
    bind_reading_t rd;
    g_exchanges = 0u;
    CHECK_EQ(bind_link_read(&g_host, &k_port, 40u, BOARD, &rd),
             BIND_READ_OK);
    CHECK(!page_asked(LINK_PAGE_OUTPUTS_HI));
    CHECK(!page_asked(LINK_PAGE_CHAN_CFG_HI));
    CHECK_EQ(rd.slots[LINK_OS_COUNT + LINK_OS_DRIVER], 0u);
    CHECK_EQ(rd.cfg[LINK_CC_COUNT + LINK_CC_MAX_US], LINK_CC_DEFAULT_MAX);
}

/*
 * A slot whose channel run reaches past channel 8 is for a 4.41 IOmcu only.
 * A 4.40 IOmcu's bank channel 8 is its bench throttle and 9 to 12 its
 * programming ports: (8,1) would bind the pin to the throttle, not to
 * channel 9.  Refused before anything is sent.
 */
TEST_CASE(a_4_40_iomcu_is_sent_no_slot_past_channel_8)
{
    static const struct { uint16_t minor; uint8_t first, n; bool sent; }
        k_case[] = {
            { 40u, 8u, 1u, false }, { 40u, 7u, 2u, false },
            { 40u, 12u, 1u, false }, { 40u, 7u, 1u, true },
            { 40u, 0u, 8u, true },  { 41u, 8u, 1u, true },
            { 41u, 15u, 1u, true },
        };
    for (size_t k = 0; k < sizeof(k_case) / sizeof(k_case[0]); ++k) {
        fresh(k_case[k].minor);
        uint16_t cfg[LINK_CC_ALL], slots[LINK_OS_ALL] = { 0 };
        outputs_chan_cfg_defaults(cfg);
        outputs_chan_cfg_defaults(&cfg[LINK_CC_COUNT]);
        slot_of(&slots[0], 2u, k_case[k].first, k_case[k].n);
        CHECK_EQ(bind_link_fits(k_case[k].minor, slots), k_case[k].sent);
        CHECK_EQ(bind_link_write(&g_host, &k_port, k_case[k].minor, cfg,
                                 slots, NULL),
                 k_case[k].sent ? BIND_WRITTEN : BIND_REFUSED);
        if (k_case[k].sent) {
            CHECK_EQ(s_bank.slot[0].pin, 2u);
            CHECK_EQ(s_bank.slot[0].first_channel, k_case[k].first);
        } else {
            CHECK_EQ(g_exchanges, 0u);
            CHECK_EQ(s_bank.slot[0].driver, OUT_DRIVER_NONE);
        }
    }
    /* An unbound slot's RANGE is not looked at. */
    uint16_t slots[LINK_OS_ALL] = { 0 };
    slots[LINK_OS_RANGE] = LINK_OS_RANGE_OF(8u, 1u);
    CHECK(bind_link_fits(40u, slots));
    CHECK(!bind_link_fits(41u, NULL));
}

/* ---------------------------------------------------------------- the CRC */

TEST_CASE(the_commit_all_crc_runs_on_over_the_hi_pages)
{
    uint16_t cfg[LINK_CC_ALL] = { 0 }, slots[LINK_OS_ALL] = { 0 };
    /* 256 zero bytes from seed 0xFFFF, by the bitwise definition. */
    uint16_t crc = 0xFFFFu;
    for (unsigned i = 0; i < 256u * 8u; ++i) {
        crc = (uint16_t)((uint16_t)(crc << 1)
                         ^ ((crc & 0x8000u) != 0u ? 0x1021u : 0u));
    }
    CHECK_EQ(out_stage_crc_all(cfg, slots), crc);
    /* Every _HI register counts in COMMIT_ALL's CRC and not in COMMIT's. */
    const uint16_t lo = out_stage_crc(cfg, slots);
    const uint16_t all = out_stage_crc_all(cfg, slots);
    for (unsigned i = 0; i < LINK_CC_COUNT; ++i) {
        cfg[LINK_CC_COUNT + i] ^= 1u;
        CHECK(out_stage_crc_all(cfg, slots) != all);
        CHECK_EQ(out_stage_crc(cfg, slots), lo);
        cfg[LINK_CC_COUNT + i] ^= 1u;
        slots[LINK_OS_COUNT + i] ^= 0x100u;
        CHECK(out_stage_crc_all(cfg, slots) != all);
        CHECK_EQ(out_stage_crc(cfg, slots), lo);
        slots[LINK_OS_COUNT + i] ^= 0x100u;
    }
    CHECK_EQ(out_stage_crc_all(NULL, slots), 0u);
    CHECK_EQ(out_stage_crc_all(cfg, NULL), 0u);
    CHECK_EQ(out_stage_held_crc_all(NULL), 0u);

    /* Prepared pages from defaults: both _HI halves as their pages start. */
    out_stage_t st;
    memset(&st, 0xA5, sizeof(st));
    out_stage_init(&st, NULL, NULL);
    outputs_chan_cfg_defaults(cfg);
    CHECK(memcmp(&st.cfg[LINK_CC_COUNT], cfg,
                 LINK_CC_COUNT * sizeof(uint16_t)) == 0);
    CHECK_EQ(st.slots[LINK_OS_ALL - 1u], 0u);
    uint16_t w[4] = { LINK_DRIVER_PWM, 20u, LINK_OS_RANGE_OF(15u, 1u), 50u };
    CHECK_EQ(out_stage_slots_write(&st, true, 28u, 4u, w), 0u);
    uint16_t back[4];
    out_stage_slots_read(&st, true, 28u, 4u, back);
    CHECK(memcmp(back, w, sizeof(w)) == 0);
    out_stage_slots_read(&st, false, 28u, 4u, back);
    CHECK_EQ(back[0], 0u);
    CHECK_EQ(out_stage_slots_write(&st, true, 30u, 4u, w),
             LINK_NACK_BAD_RANGE);
    w[2] = LINK_OS_RANGE_OF(16u, 1u);
    CHECK_EQ(out_stage_slots_write(&st, true, 28u, 4u, w),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit_all(NULL, 0u, cfg, slots, &k_ops, &far),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit_all(&st, out_stage_held_crc_all(&st), cfg,
                                  NULL, &k_ops, &far), LINK_NACK_BAD_VALUE);
}

int main(void)
{
    RUN(the_channels_run_on_from_one_page_to_the_other);
    RUN(a_slot_renders_up_to_channel_16_and_no_further);
    RUN(the_bank_is_judged_over_both_halves);
    RUN(a_binding_is_prepared_and_taken_over_both_halves);
    RUN(a_4_40_panel_leaves_the_hi_halves_of_a_4_41_iomcu);
    RUN(a_4_41_panel_sends_a_4_40_iomcu_nothing_it_lacks);
    RUN(a_4_40_iomcu_is_sent_no_slot_past_channel_8);
    RUN(the_commit_all_crc_runs_on_over_the_hi_pages);
    return test_summary("out16");
}
