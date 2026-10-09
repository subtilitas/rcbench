/*
 * A binding on the link: the panel's write and read sequences
 * (shared/outputs/bind_link.c, shared/link/link_port.c) against the
 * coprocessor's page rules and its prepared pages (shared/outputs/
 * outputs_pages.c, out_stage.c), over a modelled bus.
 *
 * The model is the part of the bench the defect lives in.  The coprocessor's
 * CAN (Controller Area Network) controller holds 2 received frames and is
 * read from a polled loop; a frame that arrives with both taken is lost.  A
 * frame is 125 us here (about 130 us at 1 Mbit/s), the loop takes every
 * buffered frame once a pass, and a deaf window is a stretch in which the
 * loop takes none.  The silence watchdog (200 ms) and the host's timeout
 * (1000 ms) are the real ones, run on the model's clock.  The store is
 * modelled as the firmware's is timed: what is in force is saved 400 ms
 * after its last change.
 *
 * The panel's control task is not in this suite.  panel_cmd() and
 * panel_poll() walk what firmware/panel/main/main.c does with an OUTPUTS
 * command, a poll and a link edge, through the same shared calls.
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
#include "outputs_screen.h"
#include "picker_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)

/* ------------------------------------------------------- the coprocessor */

typedef struct {
    uint16_t cfg[LINK_CC_COUNT];        /* CHAN_CFG in force            */
    uint16_t slots[LINK_OS_COUNT];      /* OUTPUTS in force             */
    out_stage_t stage;                  /* BIND_CFG and BIND_OUT        */
    uint16_t servo_hz;
    bool     armed;
    bool     refuse_rate;               /* SERVO's FRAME_HZ is refused  */
    bool     refuse_slots;              /* the silicon binds no page    */
    unsigned takes;                     /* pages put in force           */

    /* The store: what is in force, 400 ms after its last change. */
    uint16_t saved_cfg[LINK_CC_COUNT];
    uint16_t saved_slots[LINK_OS_COUNT];
    bool     dirty;
    uint32_t changed_ms;
    unsigned saves;
} far_t;

static far_t       far;
static link_dev_t  dev;
static link_host_t host;

/* The clock: 125 us ticks from an offset in ms, so a case can start a few
 * hundred ms before the 2^32 ms wrap. */
#define TICKS_PER_MS 8u
static uint64_t s_tick;
static uint32_t s_ms0;

static uint32_t now_ms(void)
{
    return (uint32_t)(s_ms0 + (uint32_t)(s_tick / TICKS_PER_MS));
}

static void far_changed(void)
{
    ++far.takes;
    far.dirty = true;
    far.changed_ms = now_ms();
}

static void window(const uint16_t *regs, uint8_t off, uint8_t n,
                   uint16_t *out)
{
    memcpy(out, regs + off, (size_t)n * sizeof(regs[0]));
}

static void cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    window(((far_t *)ctx)->cfg, off, n, out);
}

static void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    window(((far_t *)ctx)->slots, off, n, out);
}

/* The coprocessor's bank: the channels' roles, commands and outputs. */
static outputs_t s_bank;

/* The two pages' whole-page rules, as firmware/iomcu/src/main.c orders
 * them, less the ones that need its silicon: refuse_slots stands for a
 * bind the silicon refuses. */
static uint8_t take_cfg(void *ctx, const uint16_t *next)
{
    far_t *f = (far_t *)ctx;
    if (outputs_chan_cfg_armed_check(f->cfg, next, f->armed) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(f->cfg, next, sizeof(f->cfg));
    /* The roles reach the bank, where a role change rests the channel. */
    outputs_chan_cfg_apply(&s_bank, f->cfg);
    far_changed();
    return 0u;
}

static uint8_t take_slots(void *ctx, const uint16_t *next)
{
    far_t *f = (far_t *)ctx;
    if (f->armed) {
        return outputs_slots_armed_check(f->slots, next, true);
    }
    if (f->refuse_slots) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(f->slots, next, sizeof(f->slots));
    far_changed();
    return 0u;
}

static void put_cfg(void *ctx, const uint16_t *prev)
{
    far_t *f = (far_t *)ctx;
    memcpy(f->cfg, prev, sizeof(f->cfg));
    far_changed();
}

static unsigned s_keeps;
static void keep(void *ctx) { (void)ctx; ++s_keeps; }

static const out_stage_ops_t k_ops = {
    take_cfg, take_slots, put_cfg, keep, &s_bank,
};

static uint8_t cfg_write(void *ctx, uint8_t off, uint8_t n,
                         const uint16_t *in)
{
    far_t *f = (far_t *)ctx;
    uint16_t next[LINK_CC_COUNT];
    memcpy(next, f->cfg, sizeof(next));
    const uint8_t nack = outputs_chan_cfg_write(next, off, n, in);
    return (nack != 0u) ? nack : take_cfg(f, next);
}

static uint8_t slots_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    far_t *f = (far_t *)ctx;
    uint16_t next[LINK_OS_COUNT];
    memcpy(next, f->slots, sizeof(next));
    const uint8_t nack = outputs_slots_write(next, off, n, in);
    return (nack != 0u) ? nack : take_slots(f, next);
}

static void bcfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    out_stage_cfg_read(&((far_t *)ctx)->stage, off, n, out);
}
static uint8_t bcfg_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    return out_stage_cfg_write(&((far_t *)ctx)->stage, off, n, in);
}
static void bout_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    out_stage_slots_read(&((far_t *)ctx)->stage, off, n, out);
}
static uint8_t bout_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    return out_stage_slots_write(&((far_t *)ctx)->stage, off, n, in);
}
static void bind_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)off;
    (void)n;
    out[0] = out_stage_held_crc(&((far_t *)ctx)->stage);
}
static uint8_t bind_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    far_t *f = (far_t *)ctx;
    if (off != LINK_BD_COMMIT || n != 1u) {
        return LINK_NACK_BAD_RANGE;
    }
    return out_stage_commit(&f->stage, in[0], f->cfg, &k_ops, f);
}

static void servo_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = (off + i == LINK_SV_FRAME_HZ) ? ((far_t *)ctx)->servo_hz : 0u;
    }
}
static uint8_t servo_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    far_t *f = (far_t *)ctx;
    if (off != LINK_SV_FRAME_HZ || n != 1u || f->refuse_rate) {
        return LINK_NACK_BAD_VALUE;
    }
    f->servo_hz = in[0];
    return 0u;
}

/* A 4.10 coprocessor serves all six; a 4.9 one the first three. */
static const link_page_t k_pages[] = {
    { LINK_PAGE_SERVO,    LINK_SV_COUNT, servo_read, servo_write },
    { LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, cfg_read,   cfg_write },
    { LINK_PAGE_OUTPUTS,  LINK_OS_COUNT, slots_read, slots_write },
    { LINK_PAGE_BIND_CFG, LINK_CC_COUNT, bcfg_read,  bcfg_write },
    { LINK_PAGE_BIND_OUT, LINK_OS_COUNT, bout_read,  bout_write },
    { LINK_PAGE_BIND,     LINK_BD_COUNT, bind_read,  bind_write },
};

/* ---------------------------------------------------------------- the bus */

#define RX_BUFFERS 2u

static struct {
    link_can_frame_t rx[RX_BUFFERS];
    unsigned rxn;
    unsigned overruns;          /* request frames lost to a full buffer   */

    unsigned pass_ticks;        /* the loop takes frames once in this many */
    uint64_t deaf_from, deaf_to;    /* ticks; the loop takes none between  */

    int      drop_request;      /* the n-th request frame from now is lost */
    int      drop_reply;        /* the n-th reply frame from now is lost   */

    unsigned exchanges, request_frames, reply_frames;
    unsigned most_in_flight;    /* request frames of one exchange          */
    unsigned betweens;          /* calls between two frames of one write   */

    bool     answered;
    link_msg_t reply;

    bool     latched;           /* the silence failsafe fired              */
    uint32_t latched_ms;
    uint32_t last_dispatch_ms;
    uint32_t longest_gap_ms;    /* between two requests the device heard   */

    /* firmware/panel/main/main.c: an exchange that ended unanswered sends
     * nothing more until the next poll takes the link down. */
    bool     quiet;
} bus;

static bool deaf_now(void)
{
    return s_tick >= bus.deaf_from && s_tick < bus.deaf_to;
}

static void device_pass(void)
{
    while (bus.rxn > 0u) {
        const link_can_frame_t f = bus.rx[0];
        bus.rx[0] = bus.rx[1];
        --bus.rxn;
        link_msg_t req, ans;
        if (!link_can_decode(&f, &req)) {
            continue;
        }
        const uint32_t gap = (uint32_t)(now_ms() - bus.last_dispatch_ms);
        if (dev.heard && gap > bus.longest_gap_ms) {
            bus.longest_gap_ms = gap;
        }
        bus.last_dispatch_ms = now_ms();
        if (!link_dev_dispatch(&dev, &req, &ans, now_ms())) {
            continue;
        }
        link_can_frame_t back[LINK_CAN_MAX_FRAMES];
        const size_t m = link_can_encode(&ans, back, LINK_CAN_MAX_FRAMES);
        for (size_t k = 0; k < m; ++k) {
            ++bus.reply_frames;
            if (bus.drop_reply >= 0 && bus.drop_reply-- == 0) {
                continue;
            }
            link_msg_t part;
            if (link_can_decode(&back[k], &part)
                && link_host_accept(&host, &part, now_ms(), &bus.reply)) {
                bus.answered = true;
            }
        }
    }
}

/* One 125 us tick of everything. */
static void step(void)
{
    ++s_tick;
    const bool ms_edge = (s_tick % TICKS_PER_MS) == 0u;
    if (!deaf_now() && (s_tick % bus.pass_ticks) == 0u) {
        device_pass();
    }
    if (!ms_edge) {
        return;
    }
    /* The watchdog runs whatever the loop is busy with: the case most
     * likely to latch. */
    if (link_dev_tick(&dev, now_ms()) && !bus.latched) {
        bus.latched = true;
        bus.latched_ms = now_ms();
    }
    (void)link_host_tick(&host, now_ms());
    if (far.dirty && (uint32_t)(now_ms() - far.changed_ms) >= 400u) {
        memcpy(far.saved_cfg, far.cfg, sizeof(far.cfg));
        memcpy(far.saved_slots, far.slots, sizeof(far.slots));
        far.dirty = false;
        ++far.saves;
    }
}

static void run_ms(uint32_t ms)
{
    for (uint64_t i = 0; i < (uint64_t)ms * TICKS_PER_MS; ++i) {
        step();
    }
}

static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    (void)ctx;
    if (bus.quiet) {
        link_host_abandon(h);
        return false;
    }
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    ++bus.exchanges;
    if (n > bus.most_in_flight) {
        bus.most_in_flight = (unsigned)n;
    }
    bus.answered = false;
    /* Every frame of the request back to back, a frame time apart. */
    for (size_t i = 0; i < n; ++i) {
        ++bus.request_frames;
        if (bus.drop_request >= 0 && bus.drop_request-- == 0) {
            /* lost on the wire */
        } else if (bus.rxn < RX_BUFFERS) {
            bus.rx[bus.rxn++] = out[i];
        } else {
            ++bus.overruns;
        }
        step();
    }
    while (!bus.answered && link_host_pending(h)) {
        step();
    }
    if (!bus.answered) {
        bus.quiet = true;
        return false;
    }
    *reply = bus.reply;
    return true;
}

static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return now_ms();
}

static void port_between(void *ctx)
{
    (void)ctx;
    ++bus.betweens;
}

static const link_port_t k_port = {
    port_exchange, port_now, NULL, port_between,
};

/* ------------------------------------------------------------- bindings */

static uint8_t proto_named(const char *name)
{
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(outbind_protos()[i].name, name) == 0) {
            return i;
        }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0u;
}

static uint8_t idx(uint8_t gpio) { return outbind_index_of(BOARD, gpio); }

/* The bench of the report: GP0 DSHOT600 BIDIR, GP1 MOTOR PWM, GP2 and GP13
 * SERVO PWM. */
static void bench_binding(outbind_t *b)
{
    outbind_init(b);
    outbind_set_board(b, BOARD);
    outbind_set_proto(b, proto_named("DSHOT600 BIDIR"));
    (void)outbind_toggle(b, idx(0));
    outbind_set_proto(b, proto_named("MOTOR PWM"));
    (void)outbind_toggle(b, idx(1));
    outbind_set_proto(b, proto_named("SERVO PWM"));
    (void)outbind_toggle(b, idx(2));
    (void)outbind_toggle(b, idx(13));
}

/* The same with GP0 unticked: every slot moves up one, so a page written in
 * part holds one pin in two slots. */
static void edited_binding(outbind_t *b)
{
    bench_binding(b);
    outbind_set_proto(b, proto_named("DSHOT600 BIDIR"));
    (void)outbind_toggle(b, idx(0));
}

typedef struct {
    uint16_t cfg[LINK_CC_COUNT];
    uint16_t slots[LINK_OS_COUNT];
} pages_t;

static void pages_of(const outbind_t *b, pages_t *p)
{
    outbind_to_chan_cfg(b, p->cfg, 1000u, 2000u);
    (void)outbind_to_slots(b, p->slots);
}

static bool far_holds(const pages_t *p)
{
    return memcmp(far.cfg, p->cfg, sizeof(far.cfg)) == 0
           && memcmp(far.slots, p->slots, sizeof(far.slots)) == 0;
}

static bool far_saved(const pages_t *p)
{
    return memcmp(far.saved_cfg, p->cfg, sizeof(far.cfg)) == 0
           && memcmp(far.saved_slots, p->slots, sizeof(far.slots)) == 0;
}

static pages_t s_old, s_new;

/* A coprocessor of protocol 4.@p minor holding the bench binding, saved. */
static void fresh_at(uint16_t minor, uint32_t ms0)
{
    memset(&far, 0, sizeof(far));
    memset(&bus, 0, sizeof(bus));
    bus.pass_ticks = 1u;
    bus.drop_request = -1;
    bus.drop_reply = -1;
    s_tick = 0u;
    s_ms0 = ms0;
    s_keeps = 0u;

    outbind_t b;
    bench_binding(&b);
    pages_of(&b, &s_old);
    edited_binding(&b);
    pages_of(&b, &s_new);

    memcpy(far.cfg, s_old.cfg, sizeof(far.cfg));
    memcpy(far.slots, s_old.slots, sizeof(far.slots));
    memcpy(far.saved_cfg, s_old.cfg, sizeof(far.cfg));
    memcpy(far.saved_slots, s_old.slots, sizeof(far.slots));
    out_stage_init(&far.stage, far.cfg, far.slots);
    outputs_init(&s_bank, now_ms());
    outputs_chan_cfg_apply(&s_bank, far.cfg);

    link_dev_init(&dev, k_pages,
                  (minor >= LINK_MINOR_BIND) ? 6u : 3u, &far, now_ms());
    link_host_init(&host, now_ms());
    /* The link has carried requests before the edit: the silence watchdog
     * counts only once it has heard one. */
    link_msg_t poll = { .op = LINK_OP_READ, .page = LINK_PAGE_OUTPUTS,
                        .offset = 0u, .count = 4u };
    link_msg_t ans;
    (void)link_dev_dispatch(&dev, &poll, &ans, now_ms());
    bus.last_dispatch_ms = now_ms();
}

static void fresh(uint16_t minor) { fresh_at(minor, 5000u); }

static bind_write_t write_new(uint16_t minor)
{
    return bind_link_write(&host, &k_port, minor, s_new.cfg, s_new.slots,
                           NULL);
}

/* -------------------------------------------------------- link_write_acked */

TEST_CASE(a_wide_write_goes_out_one_frame_per_exchange)
{
    fresh(10u);
    link_msg_t reply;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, s_new.slots, &reply));
    /* 32 registers: 8 exchanges of one request frame each. */
    CHECK_EQ(bus.exchanges, 8u);
    CHECK_EQ(bus.request_frames, 8u);
    CHECK_EQ(bus.most_in_flight, 1u);
    CHECK_EQ(bus.overruns, 0u);
    /* One acknowledgement for the window, carrying what was stored. */
    CHECK_EQ(reply.op, LINK_OP_ACK);
    CHECK_EQ(reply.page, LINK_PAGE_BIND_OUT);
    CHECK_EQ(reply.offset, 0);
    CHECK_EQ(reply.count, LINK_OS_COUNT);
    CHECK(memcmp(reply.regs, s_new.slots, sizeof(s_new.slots)) == 0);
    CHECK(memcmp(far.stage.slots, s_new.slots, sizeof(s_new.slots)) == 0);
    CHECK(!host.pending);
}

TEST_CASE(a_window_that_is_not_whole_frames_is_split_at_four)
{
    fresh(10u);
    /* 6 registers from offset 5: a frame of 4 and a frame of 2. */
    uint16_t regs[6];
    for (unsigned i = 0; i < 6u; ++i) {
        regs[i] = (uint16_t)(1000u + i);     /* a legal endpoint anywhere */
    }
    /* Offsets 5..10 of CHAN_CFG: slew, min, max, role, slew, min. */
    regs[3] = LINK_CC_ROLE_SURFACE;
    link_msg_t reply;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_CFG, 5u, 6u, regs,
                           &reply));
    CHECK_EQ(bus.exchanges, 2u);
    CHECK_EQ(reply.op, LINK_OP_ACK);
    CHECK_EQ(reply.offset, 5);
    CHECK_EQ(reply.count, 6);
    CHECK(memcmp(&far.stage.cfg[5], regs, sizeof(regs)) == 0);

    /* Up to 4 registers is one exchange, as link_host_write() sends it. */
    const unsigned before = bus.exchanges;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_CFG, 4u, 4u,
                           &s_new.cfg[4], &reply));
    CHECK_EQ(bus.exchanges, before + 1u);
}

TEST_CASE(a_refused_frame_ends_the_write_and_names_itself)
{
    fresh(10u);
    uint16_t regs[LINK_OS_COUNT];
    memcpy(regs, s_new.slots, sizeof(regs));
    regs[2u * LINK_OS_STRIDE + LINK_OS_DRIVER] = 99u;    /* no such driver */
    link_msg_t reply;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, regs, &reply));
    CHECK_EQ(reply.op, LINK_OP_NACK);
    CHECK_EQ(reply.offset, 2 * LINK_OS_STRIDE);
    CHECK_EQ(reply.regs[0], LINK_NACK_BAD_VALUE);
    /* The frames after the refusal are not sent. */
    CHECK_EQ(bus.exchanges, 3u);
    CHECK(!host.pending);
}

/*
 * The caller's other work runs between two frames of one write: the panel
 * runs its safety loop there, so a page of 8 exchanges answered at once
 * does not keep STOP and the heartbeat waiting for all 8.  Once after every
 * acknowledged frame that has a successor, and never for a write of one
 * frame, which is every write the arming path sends.
 */
TEST_CASE(the_port_is_called_between_the_frames_of_a_wide_write)
{
    fresh(10u);
    link_msg_t reply;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, s_new.slots, &reply));
    CHECK_EQ(bus.exchanges, 8u);
    CHECK_EQ(bus.betweens, 7u);

    /* One frame: 1, 3 and 4 registers. */
    static const uint8_t k_one[] = { 1u, 3u, 4u };
    for (unsigned i = 0; i < 3u; ++i) {
        fresh(10u);
        CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                               k_one[i], s_new.slots, &reply));
        CHECK_EQ(bus.exchanges, 1u);
        CHECK_EQ(bus.betweens, 0u);
    }
    /* 5 registers are two frames: once. */
    fresh(10u);
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 5u,
                           s_new.slots, &reply));
    CHECK_EQ(bus.exchanges, 2u);
    CHECK_EQ(bus.betweens, 1u);

    /* A refusal at the third frame: after the first two, not after it. */
    fresh(10u);
    uint16_t regs[LINK_OS_COUNT];
    memcpy(regs, s_new.slots, sizeof(regs));
    regs[2u * LINK_OS_STRIDE + LINK_OS_DRIVER] = 99u;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, regs, &reply));
    CHECK_EQ(reply.op, LINK_OP_NACK);
    CHECK_EQ(bus.betweens, 2u);

    /* A frame nobody answers: none after it. */
    fresh(10u);
    bus.drop_request = 1;
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                            LINK_OS_COUNT, s_new.slots, &reply));
    CHECK_EQ(bus.betweens, 1u);

    /* A binding: 7 in each of the two pages, none for the rate reset and
     * the commit, which are one frame each. */
    fresh(10u);
    CHECK_EQ(write_new(10u), BIND_WRITTEN);
    CHECK_EQ(bus.exchanges, 18u);
    CHECK_EQ(bus.betweens, 14u);

    /* A port with nothing to run between frames writes the same. */
    fresh(10u);
    const link_port_t plain = { port_exchange, port_now, NULL, NULL };
    CHECK(link_write_acked(&host, &plain, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, s_new.slots, &reply));
    CHECK_EQ(reply.op, LINK_OP_ACK);
    CHECK_EQ(bus.betweens, 0u);
}

TEST_CASE(a_write_that_cannot_be_built_sends_nothing)
{
    fresh(10u);
    link_msg_t reply;
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 0u,
                            s_new.slots, &reply));
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 30u, 4u,
                            s_new.slots, &reply));
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 33u,
                            s_new.slots, &reply));
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 4u, NULL,
                            &reply));
    CHECK(!link_write_acked(NULL, &k_port, LINK_PAGE_BIND_OUT, 0u, 4u,
                            s_new.slots, &reply));
    CHECK(!link_write_acked(&host, NULL, LINK_PAGE_BIND_OUT, 0u, 4u,
                            s_new.slots, &reply));
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 4u,
                            s_new.slots, NULL));
    const link_port_t no_clock = { port_exchange, NULL, NULL, NULL };
    CHECK(!link_write_acked(&host, &no_clock, LINK_PAGE_BIND_OUT, 0u, 4u,
                            s_new.slots, &reply));
    CHECK(!link_read_window(&host, &no_clock, LINK_PAGE_OUTPUTS, 0u, 4u,
                            &reply));
    CHECK_EQ(bus.exchanges, 0u);

    /* A request already outstanding refuses the next: one at a time. */
    link_msg_t req;
    CHECK(link_host_read(&host, LINK_PAGE_OUTPUTS, 0u, 4u, now_ms(), &req));
    CHECK(!link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u, 4u,
                            s_new.slots, &reply));
    CHECK(!link_read_window(&host, &k_port, LINK_PAGE_OUTPUTS, 0u, 4u,
                            &reply));
    CHECK_EQ(bus.exchanges, 0u);
}

/* -------------------------------------------------- the defect, modelled */

/*
 * The write as 0.14.0 sends it: one request of 32 registers, 8 frames back
 * to back.  With the loop taking frames every 125 us nothing is lost.  With
 * one 20 ms deaf window over the burst, 6 of the 8 find both buffers full:
 * the host waits its 1000 ms, the coprocessor latches at 200 ms, and the
 * page is left with 2 of its 8 entries written.
 */
TEST_CASE(frames_sent_back_to_back_are_lost_to_a_deaf_window)
{
    fresh(9u);
    link_msg_t req, reply;
    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0u, LINK_OS_COUNT,
                          s_new.slots, now_ms(), &req));
    CHECK(port_exchange(NULL, &host, &req, &reply));
    CHECK_EQ(bus.most_in_flight, 8u);
    CHECK_EQ(bus.overruns, 0u);

    fresh(9u);
    bus.deaf_from = 0u;
    bus.deaf_to = 20u * TICKS_PER_MS;
    const uint32_t t0 = now_ms();
    CHECK(link_host_write(&host, LINK_PAGE_OUTPUTS, 0u, LINK_OS_COUNT,
                          s_new.slots, now_ms(), &req));
    CHECK(!port_exchange(NULL, &host, &req, &reply));
    CHECK_EQ(bus.overruns, 6u);
    CHECK_EQ((uint32_t)(now_ms() - t0), LINK_HOST_TIMEOUT_MS);
    CHECK(bus.latched);
    CHECK_EQ((uint32_t)(bus.latched_ms - t0), 20u + LINK_DEV_SILENCE_MS);
    /* Neither the page it was nor the page sent. */
    CHECK(memcmp(far.slots, s_old.slots, sizeof(far.slots)) != 0);
    CHECK(memcmp(far.slots, s_new.slots, sizeof(far.slots)) != 0);
    bind_reading_t r;
    CHECK_EQ(bind_link_classify(&r, BOARD, far.slots, far.cfg),
             BIND_READ_ODD);
}

/* ----------------------------------------------------- the write sequence */

TEST_CASE(a_binding_is_written_in_eighteen_exchanges_and_read_in_two)
{
    fresh(10u);
    bind_rate_t rate = BIND_RATE_NOT_SENT;
    CHECK_EQ(bind_link_write(&host, &k_port, 10u, s_new.cfg, s_new.slots,
                             &rate), BIND_WRITTEN);
    CHECK_EQ(rate, BIND_RATE_LANDED);
    CHECK_EQ(bus.exchanges, 18u);
    CHECK_EQ(bus.request_frames, 18u);
    CHECK_EQ(bus.reply_frames, 18u);
    CHECK_EQ(bus.most_in_flight, 1u);
    CHECK(far_holds(&s_new));
    /* Both pages are put in force by the commit, and saved once. */
    CHECK_EQ(far.takes, 2u);
    CHECK_EQ(s_keeps, 1u);

    bind_reading_t r;
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_OK);
    CHECK_EQ(bus.exchanges, 20u);
    /* Two requests, each answered by 8 data frames. */
    CHECK_EQ(bus.request_frames, 20u);
    CHECK_EQ(bus.reply_frames, 34u);
    outbind_t want;
    edited_binding(&want);
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        CHECK_EQ(r.bind.pins[g], want.pins[g]);
    }
    CHECK_EQ(bus.overruns, 0u);
    CHECK(!bus.latched);
}

TEST_CASE(an_older_coprocessor_is_written_page_by_page)
{
    fresh(9u);
    bind_rate_t rate = BIND_RATE_NOT_SENT;
    CHECK_EQ(bind_link_write(&host, &k_port, 9u, s_new.cfg, s_new.slots,
                             &rate), BIND_WRITTEN);
    CHECK_EQ(rate, BIND_RATE_LANDED);
    CHECK_EQ(bus.exchanges, 17u);
    CHECK_EQ(bus.most_in_flight, 1u);
    CHECK(far_holds(&s_new));
    /* In force entry by entry: 16 takes. */
    CHECK_EQ(far.takes, 16u);

    /* And a 4.0 coprocessor has no SERVO page to reset. */
    fresh(0u);
    CHECK_EQ(bind_link_write(&host, &k_port, 0u, s_new.cfg, s_new.slots,
                             &rate), BIND_WRITTEN);
    CHECK_EQ(rate, BIND_RATE_NOT_SENT);
    CHECK_EQ(bus.exchanges, 16u);
    CHECK(far_holds(&s_new));
}

/*
 * One 20 ms deaf window, opened at every 125 us of the sequence and at
 * every loop pass time from 125 us to 5 ms.  A frame waits in the buffer;
 * none is lost, no exchange times out and the coprocessor does not latch.
 */
TEST_CASE(a_deaf_window_anywhere_delays_the_sequence_and_loses_nothing)
{
    static const unsigned k_pass[] = { 1u, 8u, 40u };
    static const uint16_t k_minor[] = { 10u, 9u };
    unsigned runs = 0u;
    for (unsigned m = 0; m < 2u; ++m) {
        for (unsigned p = 0; p < 3u; ++p) {
            /* How long the sequence takes with nothing deaf. */
            fresh(k_minor[m]);
            bus.pass_ticks = k_pass[p];
            CHECK_EQ(write_new(k_minor[m]), BIND_WRITTEN);
            const uint64_t len = s_tick;
            for (uint64_t at = 0; at <= len; ++at) {
                fresh(k_minor[m]);
                bus.pass_ticks = k_pass[p];
                bus.deaf_from = at;
                bus.deaf_to = at + 20u * TICKS_PER_MS;
                const bind_write_t res = write_new(k_minor[m]);
                bind_reading_t r;
                const bind_read_t rd = bind_link_read(&host, &k_port, BOARD,
                                                      &r);
                ++runs;
                if (res != BIND_WRITTEN || rd != BIND_READ_OK
                    || !far_holds(&s_new) || bus.overruns != 0u
                    || host.timeouts != 0u || bus.latched || dev.failsafe
                    || bus.longest_gap_ms > 20u + 5u + 1u) {
                    T_FAIL("minor %u pass %u ticks deaf at tick %u: res %d "
                           "read %d overruns %u timeouts %u latched %d "
                           "gap %u ms",
                           (unsigned)k_minor[m], k_pass[p], (unsigned)at,
                           (int)res, (int)rd, bus.overruns,
                           (unsigned)host.timeouts, (int)bus.latched,
                           (unsigned)bus.longest_gap_ms);
                    return;
                }
            }
        }
    }
    CHECK(runs > 1000u);
}

/*
 * A sequence spread over many passes does not starve the coprocessor's
 * silence watchdog: it counts from the last request heard, and every frame
 * of the sequence is one.  The loop here takes frames once in 22 ms, so the
 * 20 exchanges take about 440 ms, twice the 200 ms limit.
 */
TEST_CASE(a_slow_sequence_does_not_latch_the_silence_failsafe)
{
    fresh(10u);
    bus.pass_ticks = 22u * TICKS_PER_MS;
    const uint32_t t0 = now_ms();
    CHECK_EQ(write_new(10u), BIND_WRITTEN);
    bind_reading_t r;
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_OK);
    const uint32_t took = (uint32_t)(now_ms() - t0);
    CHECK(took > 2u * LINK_DEV_SILENCE_MS);
    CHECK(bus.longest_gap_ms <= 22u);
    CHECK(!bus.latched);
    CHECK(!dev.failsafe);
    CHECK_EQ(host.timeouts, 0u);
    CHECK(far_holds(&s_new));
}

/*
 * The limit itself.  The watchdog fires when no request has been heard for
 * 200 ms, so one frame held 199 ms is in time and one held 200 ms is not.
 * The sequence still ends written in both: the host waits 1000 ms.
 */
TEST_CASE(one_frame_held_200_ms_latches_and_199_ms_does_not)
{
    static const uint32_t k_held[] = { 199u, 200u, 201u };
    for (unsigned i = 0; i < 3u; ++i) {
        fresh(10u);
        /* A request first, so the watchdog has something to count from. */
        link_msg_t reply;
        CHECK(link_read_window(&host, &k_port, LINK_PAGE_OUTPUTS, 0u, 4u,
                               &reply));
        const uint64_t heard = s_tick;
        run_ms(1u);
        /* Deaf until k_held ms after the request that was heard. */
        bus.deaf_from = s_tick;
        bus.deaf_to = heard + (uint64_t)k_held[i] * TICKS_PER_MS;
        CHECK_EQ(write_new(10u), BIND_WRITTEN);
        CHECK(far_holds(&s_new));
        CHECK_EQ(bus.latched, k_held[i] >= LINK_DEV_SILENCE_MS);
        CHECK_EQ(host.timeouts, 0u);
    }
}

/* An exchange nobody answers ends LINK_HOST_TIMEOUT_MS after it was sent,
 * and one answered 1 ms sooner is answered. */
TEST_CASE(an_unanswered_frame_ends_after_1000_ms_and_not_before)
{
    static const uint32_t k_deaf[] = { 999u, 1001u };
    for (unsigned i = 0; i < 2u; ++i) {
        fresh(10u);
        bus.deaf_from = 0u;
        bus.deaf_to = (uint64_t)k_deaf[i] * TICKS_PER_MS;
        const uint32_t t0 = now_ms();
        const bind_write_t res = write_new(10u);
        if (k_deaf[i] < LINK_HOST_TIMEOUT_MS) {
            CHECK_EQ(res, BIND_WRITTEN);
            CHECK_EQ(host.timeouts, 0u);
        } else {
            CHECK_EQ(res, BIND_NO_LINK);
            CHECK_EQ((uint32_t)(now_ms() - t0), LINK_HOST_TIMEOUT_MS);
            CHECK_EQ(host.timeouts, 1u);
            CHECK_EQ(bus.exchanges, 1u);
        }
    }
}

/* ------------------------------------------------------- a frame is lost */

/* After a sequence that ended unanswered: the store's 400 ms pass, the link
 * comes back, and the pages are read. */
static bind_read_t settle_and_read(bind_reading_t *r)
{
    run_ms(2000u);
    bus.quiet = false;
    return bind_link_read(&host, &k_port, BOARD, r);
}

/*
 * A request frame lost at each of the 18 positions.  The exchange ends
 * unanswered, the coprocessor latches its silence failsafe as it does for
 * any lost frame, and the binding in force and the binding saved are the
 * ones from before the edit, whole.
 */
TEST_CASE(a_lost_request_at_any_position_leaves_the_binding_as_it_was)
{
    for (int at = 0; at < 18; ++at) {
        fresh(10u);
        bus.drop_request = at;
        const uint32_t t0 = now_ms();
        const bind_write_t res = write_new(10u);
        const uint32_t took = (uint32_t)(now_ms() - t0);
        bind_reading_t r;
        const bind_read_t rd = settle_and_read(&r);
        if (res != BIND_NO_LINK || !far_holds(&s_old) || !far_saved(&s_old)
            || far.saves != 0u || far.takes != 0u || rd != BIND_READ_OK
            || bus.exchanges != (unsigned)at + 3u || !bus.latched
            || took < LINK_HOST_TIMEOUT_MS
            || took > LINK_HOST_TIMEOUT_MS + 3u) {
            T_FAIL("request %d lost: res %d holds old %d saved old %d saves "
                   "%u takes %u read %d exchanges %u latched %d took %u",
                   at, (int)res, (int)far_holds(&s_old),
                   (int)far_saved(&s_old), far.saves, far.takes, (int)rd,
                   bus.exchanges, (int)bus.latched, (unsigned)took);
            return;
        }
    }
}

/*
 * An acknowledgement lost at each position.  Before the commit nothing is
 * in force.  The commit's own acknowledgement lost leaves the new binding
 * in force and saved, whole: the panel says NO LINK and reads it back.
 */
TEST_CASE(a_lost_acknowledgement_never_leaves_a_mixed_binding)
{
    for (int at = 0; at < 18; ++at) {
        fresh(10u);
        bus.drop_reply = at;
        const bind_write_t res = write_new(10u);
        bind_reading_t r;
        const bind_read_t rd = settle_and_read(&r);
        const pages_t *want = (at == 17) ? &s_new : &s_old;
        if (res != BIND_NO_LINK || !far_holds(want) || !far_saved(want)
            || rd != BIND_READ_OK) {
            T_FAIL("acknowledgement %d lost: res %d holds %d saved %d "
                   "read %d", at, (int)res, (int)far_holds(want),
                   (int)far_saved(want), (int)rd);
            return;
        }
    }
}

/* The same across the 2^32 ms wrap: the sequence starts 300 ms before it,
 * so the lost frame's 1000 ms and the watchdog's 200 ms both span it. */
TEST_CASE(a_lost_frame_is_timed_the_same_across_the_tick_wrap)
{
    for (int at = 0; at < 18; ++at) {
        fresh_at(10u, 0xFFFFFFFFu - 300u);
        bus.drop_request = at;
        const uint32_t t0 = now_ms();
        const bind_write_t res = write_new(10u);
        const uint32_t took = (uint32_t)(now_ms() - t0);
        CHECK_EQ(res, BIND_NO_LINK);
        CHECK(took >= LINK_HOST_TIMEOUT_MS);
        CHECK(took <= LINK_HOST_TIMEOUT_MS + 3u);
        CHECK(now_ms() < 0x80000000u);          /* it did wrap */
        CHECK(bus.latched);
        /* 200 ms after the last request the coprocessor heard. */
        CHECK_EQ((uint32_t)(bus.latched_ms - bus.last_dispatch_ms),
                 LINK_DEV_SILENCE_MS);
        CHECK(far_holds(&s_old));
    }
    /* And a sequence with nothing lost, across it. */
    fresh_at(10u, 0xFFFFFFFFu);
    bus.pass_ticks = 8u;
    CHECK_EQ(write_new(10u), BIND_WRITTEN);
    CHECK(now_ms() < 0x80000000u);
    CHECK(!bus.latched);
    CHECK(far_holds(&s_new));
    run_ms(400u);
    CHECK(far_saved(&s_new));
}

/*
 * An older coprocessor has no prepared pages, so a frame lost part way
 * leaves the entries before it in force.  The pages are then read as what
 * they are: a binding, or pages no binding describes, and never as nothing
 * bound.
 */
TEST_CASE(an_older_coprocessor_can_be_left_with_pages_no_binding_describes)
{
    unsigned odd = 0u;
    for (int at = 1; at < 17; ++at) {
        fresh(9u);
        bus.drop_request = at;
        CHECK_EQ(write_new(9u), BIND_NO_LINK);
        bind_reading_t r;
        const bind_read_t rd = settle_and_read(&r);
        CHECK(rd == BIND_READ_OK || rd == BIND_READ_ODD);
        if (rd == BIND_READ_ODD) {
            ++odd;
            outbind_t tap;
            edited_binding(&tap);
            CHECK(!bind_link_may_write(rd, BOARD, &tap));
        }
    }
    CHECK(odd > 0u);
}

/* --------------------------------------------------------------- refusals */

TEST_CASE(a_refused_rate_reset_is_refused_and_writes_no_page)
{
    static const uint16_t k_minor[] = { 10u, 9u };
    for (unsigned m = 0; m < 2u; ++m) {
        fresh(k_minor[m]);
        far.refuse_rate = true;
        bind_rate_t rate = BIND_RATE_NOT_SENT;
        CHECK_EQ(bind_link_write(&host, &k_port, k_minor[m], s_new.cfg,
                                 s_new.slots, &rate), BIND_REFUSED);
        CHECK_EQ(rate, BIND_RATE_REFUSED);
        CHECK_EQ(bus.exchanges, 1u);
        CHECK(far_holds(&s_old));
        CHECK(!bus.quiet);

        /* And one nobody answers is NO LINK. */
        fresh(k_minor[m]);
        bus.drop_request = 0;
        CHECK_EQ(bind_link_write(&host, &k_port, k_minor[m], s_new.cfg,
                                 s_new.slots, &rate), BIND_NO_LINK);
        CHECK_EQ(rate, BIND_RATE_NO_ANSWER);
        CHECK_EQ(bus.exchanges, 1u);
        CHECK(far_holds(&s_old));
    }
    fresh(10u);
    CHECK_EQ(bind_link_write(NULL, &k_port, 10u, s_new.cfg, s_new.slots,
                             NULL), BIND_NO_LINK);
    CHECK_EQ(bind_link_write(&host, &k_port, 10u, NULL, s_new.slots, NULL),
             BIND_NO_LINK);
    CHECK_EQ(bus.exchanges, 0u);
}

/* A commit the coprocessor refuses changes neither page: the OUTPUTS page
 * refused after the CHAN_CFG page was taken puts that one back. */
TEST_CASE(a_refused_commit_leaves_both_pages_as_they_were)
{
    fresh(10u);
    far.refuse_slots = true;
    CHECK_EQ(write_new(10u), BIND_REFUSED);
    CHECK_EQ(bus.exchanges, 18u);
    CHECK(far_holds(&s_old));
    CHECK_EQ(s_keeps, 0u);
    run_ms(500u);
    CHECK(far_saved(&s_old));

    /* Armed: a role change is refused at the CHAN_CFG page, before the
     * OUTPUTS page is looked at. */
    fresh(10u);
    far.armed = true;
    CHECK_EQ(write_new(10u), BIND_REFUSED);
    CHECK(far_holds(&s_old));
    CHECK_EQ(far.takes, 0u);

    /* And the binding in force, written again while armed, is taken. */
    fresh(10u);
    far.armed = true;
    CHECK_EQ(bind_link_write(&host, &k_port, 10u, s_old.cfg, s_old.slots,
                             NULL), BIND_WRITTEN);
    CHECK(far_holds(&s_old));

    /* A value a page refuses is refused where it is prepared. */
    fresh(10u);
    pages_t bad = s_new;
    bad.slots[LINK_OS_PIN] = 0x0100u;
    CHECK_EQ(bind_link_write(&host, &k_port, 10u, bad.cfg, bad.slots, NULL),
             BIND_REFUSED);
    CHECK_EQ(bus.exchanges, 10u);
    CHECK(far_holds(&s_old));
}

/*
 * A refused commit leaves the channels as they were, not only the pages.
 * The new binding makes channel 1 a surface where it was a throttle, and a
 * role change rests a disarmed channel.  Taking the old page again would
 * give the role back and leave the channel at the old role's rest; the
 * commit puts the bank back whole, command and output with the role.
 */
TEST_CASE(a_refused_commit_leaves_each_channels_command_as_it_was)
{
    fresh(10u);
    CHECK_EQ(s_old.cfg[1u * LINK_CC_STRIDE + LINK_CC_ROLE],
             LINK_CC_ROLE_THROTTLE);
    CHECK_EQ(s_new.cfg[1u * LINK_CC_STRIDE + LINK_CC_ROLE],
             LINK_CC_ROLE_SURFACE);
    CHECK(outputs_set(&s_bank, 1u, 250u, now_ms()));
    CHECK(outputs_set(&s_bank, 2u, 700u, now_ms()));
    const outputs_t before = s_bank;

    far.refuse_slots = true;
    CHECK_EQ(write_new(10u), BIND_REFUSED);
    CHECK(far_holds(&s_old));
    CHECK_EQ(outputs_command(&s_bank, 1u), 250u);
    CHECK_EQ(outputs_command(&s_bank, 2u), 700u);
    CHECK_EQ(s_bank.channel[1].role, OUT_ROLE_THROTTLE);
    CHECK(memcmp(&before, &s_bank, sizeof(before)) == 0);

    /* A commit that is taken does move the role, and rests the channel. */
    far.refuse_slots = false;
    CHECK_EQ(write_new(10u), BIND_WRITTEN);
    CHECK_EQ(s_bank.channel[1].role, OUT_ROLE_SURFACE);
    CHECK(outputs_command(&s_bank, 1u) != 250u);

    /* Without a bank the commit still orders and puts back the pages. */
    fresh(10u);
    out_stage_ops_t ops = k_ops;
    ops.bank = NULL;
    out_stage_t st;
    out_stage_init(&st, s_new.cfg, s_new.slots);
    far.refuse_slots = true;
    CHECK_EQ(out_stage_commit(&st, out_stage_held_crc(&st), far.cfg, &ops,
                              &far), LINK_NACK_BAD_VALUE);
    CHECK(far_holds(&s_old));
}

/*
 * A coprocessor that restarts part way holds its pages in force prepared
 * again, not the frames sent before it restarted.  The commit names a CRC
 * (cyclic redundancy check) it does not hold and is refused.
 */
TEST_CASE(a_commit_over_pages_prepared_in_part_is_refused)
{
    fresh(10u);
    link_msg_t reply;
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_CFG, 0u,
                           LINK_CC_COUNT, s_new.cfg, &reply));
    out_stage_init(&far.stage, far.cfg, far.slots);       /* the restart */
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND_OUT, 0u,
                           LINK_OS_COUNT, s_new.slots, &reply));
    const uint16_t crc = out_stage_crc(s_new.cfg, s_new.slots);
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND, 0u, 1u, &crc,
                           &reply));
    CHECK_EQ(reply.op, LINK_OP_NACK);
    CHECK_EQ(reply.regs[0], LINK_NACK_BAD_VALUE);
    CHECK(far_holds(&s_old));
    CHECK_EQ(far.takes, 0u);

    /* The register reads the CRC of what is prepared. */
    CHECK(link_read_window(&host, &k_port, LINK_PAGE_BIND, 0u, 1u, &reply));
    CHECK_EQ(reply.regs[0], out_stage_crc(s_old.cfg, s_new.slots));
    /* And a commit is one register at offset 0. */
    const uint16_t two[2] = { crc, crc };
    CHECK(link_write_acked(&host, &k_port, LINK_PAGE_BIND, 0u, 2u, two,
                           &reply));
    CHECK_EQ(reply.op, LINK_OP_NACK);
}

/* ----------------------------------------------------------- out_stage */

TEST_CASE(the_commit_crc_is_ccitt_false_over_low_byte_first)
{
    uint16_t cfg[LINK_CC_COUNT], slots[LINK_OS_COUNT];
    memset(cfg, 0, sizeof(cfg));
    memset(slots, 0, sizeof(slots));
    /* 128 zero bytes from seed 0xFFFF, by the bitwise definition. */
    uint16_t crc = 0xFFFFu;
    for (unsigned i = 0; i < 128u * 8u; ++i) {
        crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    CHECK_EQ(out_stage_crc(cfg, slots), crc);

    /* "123456789" as the first registers, low byte first, then zeros: the
     * check value 0x29B1 carried on through the 119 zero bytes after it. */
    const char *digits = "123456789";
    for (unsigned i = 0; i < 4u; ++i) {
        cfg[i] = (uint16_t)((uint16_t)digits[2u * i]
                            | ((uint16_t)digits[2u * i + 1u] << 8));
    }
    cfg[4] = (uint16_t)digits[8];
    crc = 0x29B1u;
    for (unsigned i = 0; i < 119u * 8u; ++i) {
        crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    CHECK_EQ(out_stage_crc(cfg, slots), crc);

    /* Every register counts, and so does which page it is on. */
    const uint16_t base = out_stage_crc(s_old.cfg, s_old.slots);
    for (unsigned i = 0; i < LINK_CC_COUNT; ++i) {
        pages_t p = s_old;
        p.cfg[i] ^= 1u;
        CHECK(out_stage_crc(p.cfg, p.slots) != base);
        p = s_old;
        p.slots[i] ^= 0x100u;
        CHECK(out_stage_crc(p.cfg, p.slots) != base);
    }
    CHECK_EQ(out_stage_crc(NULL, slots), 0u);
    CHECK_EQ(out_stage_crc(cfg, NULL), 0u);
    CHECK_EQ(out_stage_held_crc(NULL), 0u);
}

TEST_CASE(the_prepared_pages_start_from_defaults_or_from_what_is_given)
{
    out_stage_t st;
    uint16_t cfg[LINK_CC_COUNT], slots[LINK_OS_COUNT];
    outputs_chan_cfg_defaults(cfg);
    outputs_slots_defaults(slots);
    memset(&st, 0xA5, sizeof(st));
    out_stage_init(&st, NULL, NULL);
    CHECK(memcmp(st.cfg, cfg, sizeof(cfg)) == 0);
    CHECK(memcmp(st.slots, slots, sizeof(slots)) == 0);
    out_stage_init(NULL, NULL, NULL);

    fresh(10u);
    out_stage_init(&st, s_new.cfg, s_new.slots);
    CHECK_EQ(out_stage_held_crc(&st), out_stage_crc(s_new.cfg, s_new.slots));

    uint16_t out[4] = { 9u, 9u, 9u, 9u };
    out_stage_cfg_read(&st, 4u, 4u, out);
    CHECK(memcmp(out, &s_new.cfg[4], sizeof(out)) == 0);
    out_stage_slots_read(&st, 4u, 4u, out);
    CHECK(memcmp(out, &s_new.slots[4], sizeof(out)) == 0);
    /* Past the page reads 0 and writes nothing. */
    out_stage_slots_read(&st, 30u, 4u, out);
    CHECK_EQ(out[2], 0u);
    CHECK_EQ(out_stage_slots_write(&st, 30u, 4u, out), LINK_NACK_BAD_RANGE);
    CHECK_EQ(out_stage_cfg_write(&st, 30u, 4u, out), LINK_NACK_BAD_RANGE);
    CHECK_EQ(out_stage_cfg_write(NULL, 0u, 4u, out), LINK_NACK_BAD_RANGE);
    CHECK_EQ(out_stage_slots_write(NULL, 0u, 4u, out), LINK_NACK_BAD_RANGE);
    out_stage_cfg_read(NULL, 0u, 4u, out);
    out_stage_slots_read(&st, 0u, 4u, NULL);

    /* A refused window stores none of its registers. */
    const uint16_t bad[4] = { LINK_DRIVER_PWM, 0x0100u, 0u, 50u };
    const uint16_t before = out_stage_held_crc(&st);
    CHECK_EQ(out_stage_slots_write(&st, 0u, 4u, bad), LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_held_crc(&st), before);
}

TEST_CASE(a_commit_needs_its_ops_and_its_crc)
{
    fresh(10u);
    out_stage_t st;
    out_stage_init(&st, s_new.cfg, s_new.slots);
    const uint16_t crc = out_stage_held_crc(&st);
    out_stage_ops_t ops = k_ops;
    CHECK_EQ(out_stage_commit(NULL, crc, far.cfg, &ops, &far),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit(&st, crc, NULL, &ops, &far),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit(&st, crc, far.cfg, NULL, &far),
             LINK_NACK_BAD_VALUE);
    ops.take_cfg = NULL;
    CHECK_EQ(out_stage_commit(&st, crc, far.cfg, &ops, &far),
             LINK_NACK_BAD_VALUE);
    ops = k_ops;
    ops.take_slots = NULL;
    CHECK_EQ(out_stage_commit(&st, crc, far.cfg, &ops, &far),
             LINK_NACK_BAD_VALUE);
    ops = k_ops;
    ops.put_cfg = NULL;
    CHECK_EQ(out_stage_commit(&st, crc, far.cfg, &ops, &far),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit(&st, (uint16_t)(crc + 1u), far.cfg, &k_ops,
                              &far), LINK_NACK_BAD_VALUE);
    CHECK_EQ(out_stage_commit(&st, (uint16_t)(crc - 1u), far.cfg, &k_ops,
                              &far), LINK_NACK_BAD_VALUE);
    CHECK(far_holds(&s_old));
    CHECK_EQ(far.takes, 0u);

    /* keep is optional. */
    ops = k_ops;
    ops.keep = NULL;
    CHECK_EQ(out_stage_commit(&st, crc, far.cfg, &ops, &far), 0u);
    CHECK(far_holds(&s_new));
    CHECK_EQ(s_keeps, 0u);
}

/* -------------------------------------------------------------- the read */

TEST_CASE(a_reading_is_one_of_four_things)
{
    fresh(10u);
    bind_reading_t r;

    /* Read and nothing bound: a binding, and it can be edited. */
    pages_t none;
    outputs_chan_cfg_defaults(none.cfg);
    outputs_slots_defaults(none.slots);
    CHECK_EQ(bind_link_classify(&r, BOARD, none.slots, none.cfg),
             BIND_READ_OK);
    CHECK(r.pages);
    CHECK_EQ(outbind_chosen_total(&r.bind), 0);
    CHECK_EQ(r.bind.board, BOARD);

    /* Not read: either page missing. */
    CHECK_EQ(bind_link_classify(&r, BOARD, NULL, s_old.cfg), BIND_READ_NONE);
    CHECK(!r.pages);
    CHECK_EQ(r.bind.board, BOARD);
    CHECK_EQ(outbind_chosen_total(&r.bind), 0);
    CHECK_EQ(bind_link_classify(&r, BOARD, s_old.slots, NULL),
             BIND_READ_NONE);
    CHECK_EQ(bind_link_classify(NULL, BOARD, s_old.slots, s_old.cfg),
             BIND_READ_NONE);

    /* Read, and one pin in two slots: not describable. */
    pages_t dup = s_old;
    dup.slots[1u * LINK_OS_STRIDE + LINK_OS_PIN] =
        dup.slots[0u * LINK_OS_STRIDE + LINK_OS_PIN];
    CHECK_EQ(bind_link_classify(&r, BOARD, dup.slots, dup.cfg),
             BIND_READ_ODD);
    CHECK(r.pages);
    CHECK_EQ(outbind_chosen_total(&r.bind), 0);

    /* Read, for a board this build has no pin map for. */
    CHECK_EQ(bind_link_classify(&r, 0x7777u, s_old.slots, s_old.cfg),
             BIND_READ_NO_BOARD);
    CHECK_EQ(r.bind.board, 0x7777u);

    /* Over the link: a refused page and an unanswered one are not read. */
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_OK);
    CHECK_EQ(bind_link_read(NULL, &k_port, BOARD, &r), BIND_READ_NONE);
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, NULL), BIND_READ_NONE);
    bus.drop_request = 1;                       /* the CHAN_CFG request */
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_NONE);
    /* With the link quiet the first read fails and the second is not sent. */
    const unsigned sent = bus.exchanges;
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_NONE);
    CHECK_EQ(bus.exchanges, sent);
    /* A coprocessor that refuses a page. */
    fresh(10u);
    link_dev_init(&dev, k_pages, 1u, &far, now_ms());      /* SERVO only */
    CHECK_EQ(bind_link_read(&host, &k_port, BOARD, &r), BIND_READ_NONE);
    CHECK_EQ(bus.exchanges, 1u);
}

TEST_CASE(a_binding_is_written_only_over_pages_that_were_read)
{
    outbind_t some, none, other;
    bench_binding(&some);
    outbind_init(&none);
    outbind_set_board(&none, BOARD);
    outbind_init(&other);
    outbind_set_board(&other, (uint16_t)(BOARD + 1u));

    CHECK(bind_link_may_write(BIND_READ_OK, BOARD, &some));
    CHECK(bind_link_may_write(BIND_READ_OK, BOARD, &none));
    CHECK(!bind_link_may_write(BIND_READ_NONE, BOARD, &some));
    CHECK(!bind_link_may_write(BIND_READ_NONE, BOARD, &none));
    /* Pages no binding describes take the binding with no pin, and no
     * other. */
    CHECK(!bind_link_may_write(BIND_READ_ODD, BOARD, &some));
    CHECK(bind_link_may_write(BIND_READ_ODD, BOARD, &none));
    CHECK(!bind_link_may_write(BIND_READ_NO_BOARD, BOARD, &none));
    CHECK(!bind_link_may_write((bind_read_t)99, BOARD, &none));
    /* Never one made for another board, and never nothing. */
    CHECK(!bind_link_may_write(BIND_READ_OK, BOARD, &other));
    CHECK(!bind_link_may_write(BIND_READ_OK, BOARD, NULL));
}

/* ------------------------------------------------- the panel, walked */

static struct {
    bool        link_up;
    bind_read_t read;           /* s_bind_read */
    uint16_t    minor;
    bool        have_cmd;
    outbind_t   cmd;
    int         applied;        /* calls of the apply seam */
    outputs_result_t result;
} panel;

static void on_apply(const outbind_t *b)
{
    ++panel.applied;
    panel.cmd = *b;
    panel.have_cmd = true;
}

/* app_main: a reading to both screens, and the picker following. */
static void panel_show(const bind_reading_t *r)
{
    panel.read = r->state;
    outputs_screen_set_reading(r);
    picker_screen_set_binding(outputs_screen_binding());
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    outputs_screen_set_result(panel.result);
}

static void panel_read(void)
{
    bind_reading_t r;
    (void)bind_link_read(&host, &k_port, BOARD, &r);
    panel_show(&r);
}

static void panel_no_reading(void)
{
    bind_reading_t r;
    (void)bind_link_classify(&r, BOARD, NULL, NULL);
    panel_show(&r);
}

/* The control task's PANEL_CMD_OUTPUTS. */
static void panel_cmd(void)
{
    if (!panel.have_cmd) {
        return;
    }
    panel.have_cmd = false;
    if (!panel.link_up) {
        panel.result = OUTPUTS_NO_LINK;
        panel_no_reading();
        return;
    }
    if (!bind_link_may_write(panel.read, BOARD, &panel.cmd)) {
        if (panel.result == OUTPUTS_OK) {
            panel.result = OUTPUTS_IDLE;
        }
        outputs_screen_set_result(panel.result);
        return;
    }
    pages_t p;
    pages_of(&panel.cmd, &p);
    switch (bind_link_write(&host, &k_port, panel.minor, p.cfg, p.slots,
                            NULL)) {
    case BIND_WRITTEN: panel.result = OUTPUTS_OK;      break;
    case BIND_REFUSED: panel.result = OUTPUTS_REFUSED; break;
    default:           panel.result = OUTPUTS_NO_LINK; break;
    }
    panel_read();
}

/* poll_far_end(): a quiet link is taken down, and found again by the next
 * identity probe, 1000 ms on. */
static void panel_poll(void)
{
    if (bus.quiet) {
        bus.quiet = false;
        panel.link_up = false;
        panel_no_reading();
        return;
    }
    if (!panel.link_up) {
        panel.link_up = true;
        panel_read();
    }
}

static void tap(const ui_screen_t *scr, int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 } };
    scr->event(&d);
    scr->event(&u);
}

/* The outputs screen's geometry, as test_outputs_screen.c holds it. */
#define COL_X  16
#define COL_W  266
#define DD_Y   44
#define DD_H   54
#define POP_ROW 46
#define CLR_Y  (DD_Y + DD_H + 92)
#define CLR_H  48
#define GRID_X 288
#define GRID_Y 16
#define CELL_GAP 4
#define CELL_W ((496 - 3 * CELL_GAP) / 4)
#define CELL_H 53
#define GRID_ROWS 7

static void tap_pin(uint8_t gpio)
{
    const uint8_t i = idx(gpio);
    tap(outputs_screen(),
        GRID_X + (i / GRID_ROWS) * (CELL_W + CELL_GAP) + CELL_W / 2,
        GRID_Y + (i % GRID_ROWS) * (CELL_H + CELL_GAP) + CELL_H / 2);
}

static void pick(const char *name)
{
    tap(outputs_screen(), COL_X + COL_W / 2, DD_Y + DD_H / 2);
    tap(outputs_screen(), COL_X + 40,
        DD_Y + 4 + proto_named(name) * POP_ROW + POP_ROW / 2);
}

static const char *label(uint8_t gpio)
{
    static char buf[4][24];
    static unsigned at;
    char *b = buf[at++ % 4u];
    (void)outputs_screen_cell(idx(gpio), b, sizeof(buf[0]));
    return b;
}

static const char *reason(void)
{
    static char buf[64];
    (void)outputs_screen_reason(buf, sizeof(buf));
    return buf;
}

/* A panel with the link up on a 4.@p minor coprocessor, the bench binding
 * read, and SERVO PWM selected: the lowest-numbered protocol with a pin. */
static void panel_fresh(uint16_t minor)
{
    fresh(minor);
    memset(&panel, 0, sizeof(panel));
    panel.minor = minor;
    panel.read = BIND_READ_NONE;
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    outputs_screen()->reset();
    picker_screen()->reset();
    outputs_screen_set_apply(on_apply);
    picker_screen_set_apply(on_apply);
    picker_screen_set_artwork(NULL, 0, 0);
    panel_poll();
    outputs_screen()->enter();
}

static void check_bench_shown(void)
{
    CHECK_STR_EQ(label(0), "DSHOT600 BIDIR");
    CHECK_STR_EQ(label(1), "MOTOR PWM");
    CHECK_STR_EQ(label(2), "PAD 4");
    CHECK_STR_EQ(label(13), "PAD 17");
    CHECK_EQ(outputs_screen_cell(idx(2), NULL, 0), OUTPUTS_CELL_TICKED);
    CHECK_EQ(outputs_screen_cell(idx(13), NULL, 0), OUTPUTS_CELL_TICKED);
    CHECK_EQ(outputs_screen_cell(idx(0), NULL, 0), OUTPUTS_CELL_HELD);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 4);
}

TEST_CASE(an_edit_is_written_whole_and_read_back)
{
    panel_fresh(10u);
    CHECK(outputs_screen_editable());
    check_bench_shown();

    tap_pin(4);                                 /* a third servo lead */
    CHECK_EQ(panel.applied, 1);
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_OK);
    CHECK(outputs_screen_editable());
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);
    CHECK_STR_EQ(label(4), "PAD 6");
    CHECK_EQ(outputs_screen_cell(idx(4), NULL, 0), OUTPUTS_CELL_TICKED);
    bind_reading_t r;
    CHECK_EQ(bind_link_classify(&r, BOARD, far.slots, far.cfg),
             BIND_READ_OK);
    CHECK_EQ(outbind_chosen_total(&r.bind), 5);
    CHECK(!bus.latched);
    CHECK_EQ(bus.overruns, 0u);
}

/*
 * The chain of the report, link by link.  A frame of the write is lost: the
 * read-back that follows is not sent, because the link is quiet.  The screen
 * goes back to the binding last read, marked, and a tap writes nothing --
 * not while the link is down, not on a command already queued, and not
 * until a read succeeds.
 */
TEST_CASE(a_failed_write_neither_blanks_the_screen_nor_lets_a_tap_write)
{
    for (int at = 0; at < 18; ++at) {
        panel_fresh(10u);
        tap_pin(4);
        bus.drop_request = at;
        panel_cmd();
        CHECK_EQ(panel.result, OUTPUTS_NO_LINK);
        CHECK_EQ(outputs_screen_read_state(), BIND_READ_NONE);
        CHECK(!outputs_screen_editable());
        /* The binding last read, and not the tick that was not written. */
        check_bench_shown();
        CHECK_STR_EQ(label(4), "PAD 6");
        CHECK_EQ(outputs_screen_cell(idx(4), NULL, 0), OUTPUTS_CELL_FREE);
        CHECK_STR_EQ(reason(), "BINDING NOT READ - NO EDITS");

        /* A tap now: no call of the seam, nothing sent. */
        const unsigned sent = bus.exchanges;
        tap_pin(5);
        tap_pin(2);
        CHECK_EQ(panel.applied, 1);
        panel_cmd();
        CHECK_EQ(bus.exchanges, sent);

        /* The poll takes the link down; still the same picture. */
        panel_poll();
        CHECK(!panel.link_up);
        check_bench_shown();
        tap_pin(5);
        CHECK_EQ(panel.applied, 1);

        /* A command that was queued before the screen was told. */
        panel.cmd = *outputs_screen_binding();
        (void)outbind_toggle(&panel.cmd, idx(5));
        panel.have_cmd = true;
        panel_cmd();
        CHECK_EQ(bus.exchanges, sent);
        panel.link_up = true;                   /* and with the link held up */
        panel.cmd = *outputs_screen_binding();
        (void)outbind_toggle(&panel.cmd, idx(5));
        panel.have_cmd = true;
        panel_cmd();
        CHECK_EQ(bus.exchanges, sent);
        /* LAST WRITE keeps the NO LINK that explains the state. */
        CHECK_EQ(panel.result, OUTPUTS_NO_LINK);
        panel.link_up = false;
        CHECK(far_holds(&s_old));

        /* The link comes back and the read succeeds: edits are taken. */
        run_ms(1000u);
        panel_poll();
        CHECK(panel.link_up);
        CHECK(outputs_screen_editable());
        CHECK_STR_EQ(reason(), "");
        check_bench_shown();
        tap_pin(4);
        CHECK_EQ(panel.applied, 2);
        panel_cmd();
        CHECK_EQ(panel.result, OUTPUTS_OK);
        CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);
        CHECK(!far_holds(&s_old));
        run_ms(500u);
        bind_reading_t r;
        CHECK_EQ(bind_link_classify(&r, BOARD, far.saved_slots,
                                    far.saved_cfg), BIND_READ_OK);
        CHECK_EQ(outbind_chosen_total(&r.bind), 5);
    }
}

/* The commit's acknowledgement alone lost: the binding was written.  The
 * screen says NO LINK and shows the binding last read until the link-up
 * read shows the one in force. */
TEST_CASE(a_write_whose_last_acknowledgement_is_lost_is_read_back_whole)
{
    panel_fresh(10u);
    tap_pin(4);
    bus.drop_reply = 17;
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_NO_LINK);
    CHECK(!outputs_screen_editable());
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 4);
    panel_poll();
    run_ms(1000u);
    panel_poll();
    CHECK(outputs_screen_editable());
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);
    CHECK_EQ(outputs_screen_cell(idx(4), NULL, 0), OUTPUTS_CELL_TICKED);
}

/* Pages no binding describes: one pin in two slots, as a write that landed
 * in part on a 4.9 coprocessor leaves them. */
static void far_gets_odd_pages(void)
{
    far.slots[1u * LINK_OS_STRIDE + LINK_OS_PIN] =
        far.slots[0u * LINK_OS_STRIDE + LINK_OS_PIN];
}

static void hold_clear(float seconds, float dt)
{
    const int x = COL_X + COL_W / 2, y = CLR_Y + CLR_H / 2;
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 } };
    outputs_screen()->event(&d);
    for (float t = 0.0f; t < seconds - 0.001f; t += dt) {
        outputs_screen()->tick(dt);
    }
}

static void lift_clear(void)
{
    const int x = COL_X + COL_W / 2, y = CLR_Y + CLR_H / 2;
    touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)x, (int16_t)y, 40 } };
    outputs_screen()->event(&u);
}

TEST_CASE(pages_no_binding_describes_are_shown_as_such_and_not_overwritten)
{
    panel_fresh(10u);
    far_gets_odd_pages();
    uint16_t odd_slots[LINK_OS_COUNT];
    memcpy(odd_slots, far.slots, sizeof(odd_slots));
    panel_read();
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK(!outputs_screen_editable());
    CHECK_STR_EQ(reason(), "PAGES HOLD NO VALID BINDING");
    /* The binding last read stays on the screen. */
    check_bench_shown();

    /* A tap on a pin, ticked or free, on either screen: nothing. */
    const unsigned sent = bus.exchanges;
    tap_pin(2);
    tap_pin(5);
    CHECK_EQ(panel.applied, 0);
    panel_cmd();
    CHECK_EQ(bus.exchanges, sent);
    CHECK(memcmp(far.slots, odd_slots, sizeof(odd_slots)) == 0);
    /* A pick changes what is shown and writes nothing. */
    pick("MOTOR PWM");
    CHECK_EQ(panel.applied, 0);
    CHECK_STR_EQ(label(1), "PAD 2");
    CHECK_STR_EQ(label(2), "SERVO PWM");

    /* A binding with pins, queued: not sent over these pages, and a LAST
     * WRITE of WRITTEN goes to NOT WRITTEN. */
    panel.cmd = *outputs_screen_binding();
    panel.have_cmd = true;
    panel.result = OUTPUTS_OK;
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_IDLE);
    CHECK_EQ(bus.exchanges, sent);
    CHECK(memcmp(far.slots, odd_slots, sizeof(odd_slots)) == 0);

    /* The read is tried again and gives the same: the screen stays. */
    panel_read();
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 4);
}

/*
 * The way out of pages no binding describes: UNBIND ALL PINS held for
 * UI_HOLD_S (2 s).  A frame adds at most 0.25 s, so 8 frames at least.
 */
TEST_CASE(unbind_all_pins_is_held_two_seconds_and_writes_an_empty_binding)
{
    /* Not offered while the binding is confirmed, or merely not read. */
    panel_fresh(10u);
    hold_clear(3.0f, 0.25f);
    lift_clear();
    CHECK_EQ(panel.applied, 0);
    bind_reading_t none;
    (void)bind_link_classify(&none, BOARD, NULL, NULL);
    panel_show(&none);
    hold_clear(3.0f, 0.25f);
    lift_clear();
    CHECK_EQ(panel.applied, 0);

    /* 1.75 s and a lift: nothing. */
    panel_fresh(10u);
    far_gets_odd_pages();
    panel_read();
    hold_clear(1.75f, 0.25f);
    CHECK_EQ(panel.applied, 0);
    lift_clear();
    outputs_screen()->tick(1.0f);
    CHECK_EQ(panel.applied, 0);

    /* One late frame is worth 0.25 s, not the time it took. */
    hold_clear(0.25f, 0.25f);
    outputs_screen()->tick(10.0f);
    CHECK_EQ(panel.applied, 0);
    lift_clear();

    /* A finger that slides off the key abandons the hold. */
    hold_clear(1.75f, 0.25f);
    touch_event_t mv = { TOUCH_EVENT_MOVE,
                         { 0, (int16_t)(COL_X + COL_W + 40), CLR_Y, 40 } };
    outputs_screen()->event(&mv);
    outputs_screen()->tick(0.25f);
    outputs_screen()->tick(0.25f);
    CHECK_EQ(panel.applied, 0);
    lift_clear();

    /* A touch stream that broke drops it. */
    hold_clear(1.75f, 0.25f);
    outputs_screen()->cancel();
    outputs_screen()->tick(0.25f);
    CHECK_EQ(panel.applied, 0);
    lift_clear();

    /* Leaving the screen drops it. */
    hold_clear(1.75f, 0.25f);
    outputs_screen()->leave();
    outputs_screen()->tick(0.25f);
    CHECK_EQ(panel.applied, 0);
    outputs_screen()->enter();

    /* 2.0 s: one call, with no pin, and it does not repeat. */
    hold_clear(1.75f, 0.25f);
    CHECK_EQ(panel.applied, 0);
    outputs_screen()->tick(0.25f);
    CHECK_EQ(panel.applied, 1);
    CHECK_EQ(outbind_chosen_total(&panel.cmd), 0);
    CHECK_EQ(panel.cmd.board, BOARD);
    outputs_screen()->tick(0.25f);
    outputs_screen()->tick(2.0f);
    CHECK_EQ(panel.applied, 1);
    lift_clear();
    CHECK_EQ(panel.applied, 1);

    /* It is written whole, read back as nothing bound, and edits return. */
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_OK);
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_OK);
    CHECK(outputs_screen_editable());
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 0);
    for (unsigned i = 0; i < LINK_OS_COUNT; ++i) {
        CHECK_EQ(far.slots[i], 0u);
    }
    tap_pin(4);
    CHECK_EQ(panel.applied, 2);

    /* A reading that confirms a binding under a hold ends the hold. */
    panel_fresh(10u);
    far_gets_odd_pages();
    panel_read();
    hold_clear(1.75f, 0.25f);
    memcpy(far.slots, s_old.slots, sizeof(far.slots));
    panel_read();
    outputs_screen()->tick(0.25f);
    outputs_screen()->tick(0.25f);
    CHECK_EQ(panel.applied, 0);
    lift_clear();
    CHECK_EQ(panel.applied, 0);
}

/* A 4.9 coprocessor, a frame lost part way, pages left that no binding
 * describes: shown as such, no tap writes, and UNBIND ALL PINS recovers. */
TEST_CASE(an_older_coprocessor_left_mixed_is_not_overwritten_by_a_tap)
{
    panel_fresh(9u);
    pick("DSHOT600 BIDIR");
    tap_pin(0);                                 /* untick: every slot moves */
    CHECK_EQ(panel.applied, 1);
    bus.drop_request = 9 + 1;                   /* OUTPUTS entry 1 */
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_NO_LINK);
    panel_poll();
    run_ms(1000u);
    panel_poll();
    CHECK_EQ(outputs_screen_read_state(), BIND_READ_ODD);
    CHECK_STR_EQ(reason(), "PAGES HOLD NO VALID BINDING");
    /* The last binding read, GP0 included. */
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 4);
    CHECK_EQ(outputs_screen_cell(idx(0), NULL, 0), OUTPUTS_CELL_TICKED);

    uint16_t mixed[LINK_OS_COUNT];
    memcpy(mixed, far.slots, sizeof(mixed));
    pick("SERVO PWM");
    tap_pin(4);
    CHECK_EQ(panel.applied, 1);
    panel_cmd();
    CHECK(memcmp(far.slots, mixed, sizeof(mixed)) == 0);

    hold_clear(2.0f, 0.25f);
    lift_clear();
    CHECK_EQ(panel.applied, 2);
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_OK);
    CHECK(outputs_screen_editable());
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 0);
}

/* ------------------------------------------------------------ PICK A PIN */

#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)

/* The button of a pin, found as test_picker_screen.c finds it: walk out
 * from the pad until a press acts.  The pin is left as it was. */
static bool picker_button(uint8_t gpio, int *bx, int *by)
{
    const outbind_board_t *bd = outbind_board(BOARD);
    uint16_t xc, yc;
    if (bd == NULL || bd->shape == NULL
        || !outbind_pad_xy(bd->shape, bd->pins[idx(gpio)].pad, &xc, &yc)) {
        return false;
    }
    const int bw = 500;
    const int bh = (int)(((uint32_t)bw * bd->shape->height_cmm)
                         / bd->shape->width_cmm);
    const int px = (SCREEN_W - bw) / 2
                   + (int)(((uint32_t)xc * (uint32_t)bw)
                           / bd->shape->width_cmm);
    const int py = (SCREEN_H - bh) / 2
                   + (int)(((uint32_t)yc * (uint32_t)bh)
                           / bd->shape->height_cmm);
    const int step = (py < SCREEN_H / 2) ? -1 : 1;
    for (int y = py + step * 20; y > 0 && y < SCREEN_H; y += step * 2) {
        const int before = panel.applied;
        tap(picker_screen(), px, y);
        if (panel.applied != before) {
            tap(picker_screen(), px, y);        /* and back */
            panel.applied = before;
            panel.have_cmd = false;
            *bx = px;
            *by = y;
            return true;
        }
    }
    return false;
}

TEST_CASE(the_pin_picker_writes_through_the_same_sequence)
{
    panel_fresh(10u);
    int bx = 0, by = 0;
    if (!picker_button(4u, &bx, &by)) {
        T_FAIL("GP4 has no button");
        return;
    }
    CHECK(picker_screen_editable());

    /* A tap joins the protocol selected on the outputs screen and is
     * written prepared and committed: one frame in flight, both pages at
     * the commit. */
    tap(picker_screen(), bx, by);
    CHECK_EQ(panel.applied, 1);
    CHECK_EQ(outbind_group_of(&panel.cmd, idx(4)), proto_named("SERVO PWM"));
    const unsigned takes = far.takes;
    panel_cmd();
    CHECK_EQ(panel.result, OUTPUTS_OK);
    CHECK_EQ(bus.most_in_flight, 1u);
    CHECK_EQ(far.takes, takes + 2u);
    CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 5);
    CHECK_EQ(outbind_chosen_total(outputs_screen_binding()), 5);

    /* A frame lost at each position of a write from this screen. */
    for (int at = 0; at < 18; ++at) {
        panel_fresh(10u);
        tap(picker_screen(), bx, by);
        CHECK_EQ(panel.applied, 1);
        bus.drop_request = at;
        panel_cmd();
        CHECK_EQ(panel.result, OUTPUTS_NO_LINK);
        CHECK(far_holds(&s_old));
        /* Both screens go back to the binding last read, and the picker
         * takes no tap. */
        CHECK(!picker_screen_editable());
        CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 4);
        CHECK_EQ(outbind_group_of(picker_screen_binding(), idx(4)), 0);
        const unsigned sent = bus.exchanges;
        tap(picker_screen(), bx, by);
        CHECK_EQ(panel.applied, 1);
        panel_cmd();
        CHECK_EQ(bus.exchanges, sent);
        panel_poll();
        tap(picker_screen(), bx, by);
        CHECK_EQ(panel.applied, 1);
        run_ms(2000u);
        CHECK(far_saved(&s_old));

        /* Recovery: the link-up read, and the tap is taken again. */
        panel_poll();
        CHECK(picker_screen_editable());
        tap(picker_screen(), bx, by);
        CHECK_EQ(panel.applied, 2);
        panel_cmd();
        CHECK_EQ(panel.result, OUTPUTS_OK);
        CHECK_EQ(outbind_chosen_total(picker_screen_binding()), 5);
    }

    /* Pages no binding describes: no tap here either. */
    panel_fresh(10u);
    far_gets_odd_pages();
    panel_read();
    CHECK(!picker_screen_editable());
    tap(picker_screen(), bx, by);
    CHECK_EQ(panel.applied, 0);
}

/* A pick on the outputs screen writes nothing, so nothing is read back:
 * the picker follows the selection every frame instead. */
TEST_CASE(the_pin_picker_follows_a_pick_that_wrote_nothing)
{
    panel_fresh(10u);
    int bx = 0, by = 0;
    if (!picker_button(4u, &bx, &by)) {
        T_FAIL("GP4 has no button");
        return;
    }
    const unsigned sent = bus.exchanges;
    pick("MOTOR PWM");
    CHECK_EQ(panel.applied, 0);
    CHECK_EQ(bus.exchanges, sent);
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, proto_named("MOTOR PWM"));
    tap(picker_screen(), bx, by);
    CHECK_EQ(panel.applied, 1);
    CHECK_EQ(outbind_group_of(&panel.cmd, idx(4)), proto_named("MOTOR PWM"));

    /* OFF picked and the screen left: the picker is not left on OFF. */
    panel_fresh(10u);
    pick("OFF");
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, 0);
    outputs_screen()->leave();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    CHECK_EQ(picker_screen_binding()->proto, proto_named("SERVO PWM"));
    tap(picker_screen(), bx, by);
    CHECK_EQ(panel.applied, 1);
    /* A protocol number off the table is not followed. */
    picker_screen_follow((uint8_t)(OUTBIND_PROTOS + 3u), true);
    CHECK_EQ(picker_screen_binding()->proto, proto_named("SERVO PWM"));
}

int main(void)
{
    RUN(a_wide_write_goes_out_one_frame_per_exchange);
    RUN(a_window_that_is_not_whole_frames_is_split_at_four);
    RUN(a_refused_frame_ends_the_write_and_names_itself);
    RUN(the_port_is_called_between_the_frames_of_a_wide_write);
    RUN(a_write_that_cannot_be_built_sends_nothing);
    RUN(frames_sent_back_to_back_are_lost_to_a_deaf_window);
    RUN(a_binding_is_written_in_eighteen_exchanges_and_read_in_two);
    RUN(an_older_coprocessor_is_written_page_by_page);
    RUN(a_deaf_window_anywhere_delays_the_sequence_and_loses_nothing);
    RUN(a_slow_sequence_does_not_latch_the_silence_failsafe);
    RUN(one_frame_held_200_ms_latches_and_199_ms_does_not);
    RUN(an_unanswered_frame_ends_after_1000_ms_and_not_before);
    RUN(a_lost_request_at_any_position_leaves_the_binding_as_it_was);
    RUN(a_lost_acknowledgement_never_leaves_a_mixed_binding);
    RUN(a_lost_frame_is_timed_the_same_across_the_tick_wrap);
    RUN(an_older_coprocessor_can_be_left_with_pages_no_binding_describes);
    RUN(a_refused_rate_reset_is_refused_and_writes_no_page);
    RUN(a_refused_commit_leaves_both_pages_as_they_were);
    RUN(a_refused_commit_leaves_each_channels_command_as_it_was);
    RUN(a_commit_over_pages_prepared_in_part_is_refused);
    RUN(the_commit_crc_is_ccitt_false_over_low_byte_first);
    RUN(the_prepared_pages_start_from_defaults_or_from_what_is_given);
    RUN(a_commit_needs_its_ops_and_its_crc);
    RUN(a_reading_is_one_of_four_things);
    RUN(a_binding_is_written_only_over_pages_that_were_read);
    RUN(an_edit_is_written_whole_and_read_back);
    RUN(a_failed_write_neither_blanks_the_screen_nor_lets_a_tap_write);
    RUN(a_write_whose_last_acknowledgement_is_lost_is_read_back_whole);
    RUN(pages_no_binding_describes_are_shown_as_such_and_not_overwritten);
    RUN(unbind_all_pins_is_held_two_seconds_and_writes_an_empty_binding);
    RUN(an_older_coprocessor_left_mixed_is_not_overwritten_by_a_tap);
    RUN(the_pin_picker_writes_through_the_same_sequence);
    RUN(the_pin_picker_follows_a_pick_that_wrote_nothing);
    return test_summary("bind_link");
}
