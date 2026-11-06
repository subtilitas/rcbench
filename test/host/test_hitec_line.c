/*
 * The Hitec line's RP2350 driver (protocols/hitec/rp2350/hitec_line.c)
 * against a fake of the pico-sdk calls it makes (fake_pico_hitec/): the order
 * in which it stops a state machine, lets the receive channel take the FIFO,
 * reads the channel's count and restarts.
 *
 * The fake machine pushes the words a case gives it, one at each SDK call
 * while it runs, as the chip's machine runs between any two calls.  The fake
 * receive channel takes a word from the FIFO only in tight_loop_contents(),
 * the latest a channel can be: a word in the FIFO is one the channel's count
 * does not have yet.
 *
 * Under test: the restart past the time with a word still in the FIFO and
 * with one pushed as the time runs out, the time at and one microsecond
 * before its limit and across the clock's wrap, the settle's 64 reads with
 * a channel that takes nothing, the words of one transaction kept out of
 * the next, the end of a receive at idle and at its count, the answer of a
 * 1-byte LISTEN let out whole, and every abort path: a receive, inside and
 * after the answer, nothing running, twice, a PRECHARGE request and the
 * PRECHARGE sampler.
 *
 * Not tested here: the chip.  test_hitec_pio and test_hitec_pio_answer run
 * the program; the cycle counts of the chip's DMA and FIFO are not measured.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hitec_line.h"
#include "hitec_pio.h"
#include "hitec_pre.h"
#include "hitec_wire.h"
#include "hitec_line.pio.h"

#define PIN         9u
#define LINE_OFFSET 2u
#define PRE_OFFSET  20u
#define SYS_HZ      150000000u
#define CH_TX       0u
#define CH_RX       1u
#define CH_PRE      2u
#define LINE        0u   /* the line's block */
#define SAMPLER     1u   /* the sampler's block */
#define WAIT_HI_PC  (HITEC_PIO_RELEASE_PC + 1u)
#define MAX_EV      512u
#define MAX_PEND    8u

/* --------------------------------------------------------------- the fake */

/* What a call did, for the order: kind << 4 | the block or the channel. */
enum {
    E_STOP = 1,   /* a machine stopped */
    E_RUN,        /* a machine enabled */
    E_TAKE,       /* a channel took a word from a FIFO */
    E_COUNT,      /* the receive channel's count read */
    E_ABORT,      /* a channel aborted */
    E_CLEAR,      /* a machine's FIFOs cleared */
    E_RESTART,    /* a machine restarted */
    E_CONF,       /* a channel configured */
    E_RELEASE,    /* a machine's pad direction set to input */
};
#define EV(kind, tag) ((unsigned)(kind) << 4 | (unsigned)(tag))

typedef struct {
    bool enabled;
    uint32_t pc;
    uint32_t fifo[4];
    unsigned n_fifo;
    uint32_t pend[MAX_PEND];   /* words it pushes as it runs, in order */
    unsigned n_pend;
    unsigned pushed;
    unsigned dropped;          /* pushes into a full FIFO */
    unsigned cleared;          /* words a clear threw away */
    unsigned restarts_running; /* restarts of a machine not stopped */
    bool dir;                  /* the pad drives */
    bool claimed;
} fake_sm_t;

typedef struct {
    bool claimed;
    bool busy;
    int block;                 /* the FIFO it reads; -1 for memory */
    uint32_t *dst;
    dma_channel_hw_t hw;
} fake_ch_t;

static struct {
    pio_hw_t hw[2];
    fake_sm_t sm[2];
    fake_ch_t ch[3];
    unsigned next_ch;
    bool dma_stuck;            /* the receive channels take nothing */
    uint32_t now_us;
    unsigned spins;
    unsigned ev[MAX_EV];
    unsigned n_ev;
    bool count_running;        /* the line machine ran at the last count */
    unsigned count_fifo;       /* and its FIFO held this many */
} g;

io_bank0_hw_t fake_io_bank0;
pads_bank0_hw_t fake_pads_bank0;
const pio_program_t hitec_line_program = { NULL, HITEC_PIO_PROGRAM_WORDS, -1 };

static hitec_line_t line;

static void log_ev(unsigned kind, unsigned tag)
{
    if (g.n_ev < MAX_EV) {
        g.ev[g.n_ev++] = EV(kind, tag);
    }
}

static unsigned block_of(PIO pio)
{
    return pio == &g.hw[SAMPLER] ? SAMPLER : LINE;
}

/* Every running machine pushes its next word, as it runs between calls. */
static void machine_runs(void)
{
    for (unsigned b = 0; b < 2u; ++b) {
        fake_sm_t *s = &g.sm[b];

        if (!s->enabled || s->n_pend == 0u) {
            continue;
        }
        if (s->n_fifo < 4u) {
            s->fifo[s->n_fifo++] = s->pend[0];
        } else {
            s->dropped++;
        }
        s->pushed++;
        memmove(&s->pend[0], &s->pend[1], (s->n_pend - 1u) * sizeof(s->pend[0]));
        s->n_pend--;
    }
}

/* Channel @p c takes one word from its FIFO, if it can. */
static void dma_step(unsigned c)
{
    fake_ch_t *ch = &g.ch[c];
    fake_sm_t *s;

    if (g.dma_stuck || !ch->busy || ch->block < 0) {
        return;
    }
    s = &g.sm[ch->block];
    if (s->n_fifo == 0u) {
        return;
    }
    *ch->dst++ = s->fifo[0];
    memmove(&s->fifo[0], &s->fifo[1], (s->n_fifo - 1u) * sizeof(s->fifo[0]));
    s->n_fifo--;
    ch->hw.transfer_count--;
    ch->hw.write_addr += 4u;
    log_ev(E_TAKE, c);
    if (ch->hw.transfer_count == 0u) {
        ch->busy = false;
    }
}

uint32_t clock_get_hz(enum clock_index c) { (void)c; return SYS_HZ; }
uint32_t time_us_32(void)
{
    machine_runs();
    return g.now_us;
}
void tight_loop_contents(void)
{
    machine_runs();
    g.spins++;
    dma_step(CH_RX);
    dma_step(CH_PRE);
}

bool dma_channel_is_busy(uint ch)
{
    machine_runs();
    return g.ch[ch].busy;
}
void dma_channel_abort(uint ch)
{
    machine_runs();
    g.ch[ch].busy = false;
    log_ev(E_ABORT, ch);
}
void dma_channel_unclaim(uint ch) { g.ch[ch].claimed = false; }
int dma_claim_unused_channel(bool required)
{
    (void)required;
    if (g.next_ch >= 3u) {
        return -1;
    }
    g.ch[g.next_ch].claimed = true;
    return (int)g.next_ch++;
}
dma_channel_hw_t *dma_channel_hw_addr(uint ch)
{
    machine_runs();
    if (ch == CH_RX) {
        log_ev(E_COUNT, ch);
        g.count_running = g.sm[LINE].enabled;
        g.count_fifo = g.sm[LINE].n_fifo;
    }
    return &g.ch[ch].hw;
}
dma_channel_config dma_channel_get_default_config(uint ch)
{
    dma_channel_config c;

    (void)ch;
    memset(&c, 0, sizeof(c));
    return c;
}
void channel_config_set_transfer_data_size(dma_channel_config *c,
                                           enum dma_channel_transfer_size s)
{
    c->data_size = (uint32_t)s;
}
void channel_config_set_read_increment(dma_channel_config *c, bool on)
{
    c->read_inc = on;
}
void channel_config_set_write_increment(dma_channel_config *c, bool on)
{
    c->write_inc = on;
}
void channel_config_set_dreq(dma_channel_config *c, uint dreq) { c->dreq = dreq; }
void dma_channel_configure(uint ch, const dma_channel_config *c,
                           volatile void *write, const volatile void *read,
                           uint32_t count, bool trigger)
{
    fake_ch_t *f = &g.ch[ch];

    (void)c;
    f->block = -1;
    for (unsigned b = 0; b < 2u; ++b) {
        for (unsigned k = 0; k < 4u; ++k) {
            if (read == (const volatile void *)&g.hw[b].rxf[k]) {
                f->block = (int)b;
            }
        }
    }
    f->dst = (uint32_t *)write;
    f->hw.transfer_count = count;
    f->hw.write_addr = (uintptr_t)write;
    f->busy = trigger;
    log_ev(E_CONF, ch);
}

void gpio_init(uint pin)
{
    fake_io_bank0.io[pin].ctrl = FAKE_FUNC_SIO;
}
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_set_pulls(uint pin, bool up, bool down)
{
    uint32_t *p = &fake_pads_bank0.io[pin];

    *p = (*p & ~(FAKE_PAD_PUE | FAKE_PAD_PDE)) | (up ? FAKE_PAD_PUE : 0u)
         | (down ? FAKE_PAD_PDE : 0u);
}
void gpio_set_inover(uint pin, uint value)
{
    uint32_t *c = &fake_io_bank0.io[pin].ctrl;

    *c = (*c & ~FAKE_CTRL_INOVER_BITS) | (uint32_t)value << FAKE_CTRL_INOVER_LSB;
}
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength d)
{
    uint32_t *p = &fake_pads_bank0.io[pin];

    *p = (*p & ~FAKE_PAD_DRIVE_BITS) | (uint32_t)d << FAKE_PAD_DRIVE_LSB;
}
void gpio_set_slew_rate(uint pin, enum gpio_slew_rate s)
{
    uint32_t *p = &fake_pads_bank0.io[pin];

    *p = (*p & ~FAKE_PAD_SLEW_FAST) | (s == GPIO_SLEW_RATE_FAST ? 1u : 0u);
}

/* The chip's encodings: SET 7, OUT 3, MOV 5, JMP 0 in the top 3 bits. */
uint pio_encode_set(enum pio_src_dest dest, uint value)
{
    return 0xE000u | (uint)dest << 5 | (value & 31u);
}
uint pio_encode_out(enum pio_src_dest dest, uint count)
{
    return 0x6000u | (uint)dest << 5 | (count & 31u);
}
uint pio_encode_mov(enum pio_src_dest dest, enum pio_src_dest src)
{
    return 0xA000u | (uint)dest << 5 | (uint)src;
}
uint pio_encode_jmp(uint addr) { return addr & 31u; }
void pio_sm_exec(PIO pio, uint sm, uint instr)
{
    fake_sm_t *s = &g.sm[block_of(pio)];

    (void)sm;
    machine_runs();
    if ((instr >> 13) == 0u) {
        s->pc = instr & 31u;
    } else if ((instr >> 13) == 7u && ((instr >> 5) & 7u) == (uint)pio_pindirs) {
        s->dir = (instr & 1u) != 0u;
        if (!s->dir) {
            log_ev(E_RELEASE, block_of(pio));
        }
    }
}
void pio_sm_set_enabled(PIO pio, uint sm, bool on)
{
    (void)sm;
    machine_runs();
    g.sm[block_of(pio)].enabled = on;
    log_ev(on ? E_RUN : E_STOP, block_of(pio));
}
void pio_sm_clear_fifos(PIO pio, uint sm)
{
    fake_sm_t *s = &g.sm[block_of(pio)];

    (void)sm;
    machine_runs();
    s->cleared += s->n_fifo;
    s->n_fifo = 0;
    log_ev(E_CLEAR, block_of(pio));
}
void pio_sm_restart(PIO pio, uint sm)
{
    fake_sm_t *s = &g.sm[block_of(pio)];

    (void)sm;
    machine_runs();
    if (s->enabled) {
        s->restarts_running++;
    }
    /* A restarted machine has nothing in flight. */
    s->n_pend = 0;
    log_ev(E_RESTART, block_of(pio));
}
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *c)
{
    (void)sm;
    (void)c;
    g.sm[block_of(pio)].pc = pc;
    g.sm[block_of(pio)].enabled = false;
}
uint pio_sm_get_pc(PIO pio, uint sm)
{
    (void)sm;
    machine_runs();
    return g.sm[block_of(pio)].pc;
}
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    machine_runs();
    return true;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm)
{
    (void)sm;
    machine_runs();
    return g.sm[block_of(pio)].n_fifo == 0u;
}
uint pio_get_dreq(PIO pio, uint sm, bool tx)
{
    return block_of(pio) * 8u + sm + (tx ? 0u : 4u);
}
void pio_gpio_init(PIO pio, uint pin)
{
    fake_io_bank0.io[pin].ctrl = FAKE_FUNC_PIO0 + block_of(pio);
}
bool pio_claim_free_sm_and_add_program_for_gpio_range(
    const pio_program_t *prog, PIO *pio, uint *sm, uint *offset, uint base,
    uint count, bool set_range)
{
    const unsigned b = prog == &hitec_line_program ? LINE : SAMPLER;

    (void)base;
    (void)count;
    (void)set_range;
    *pio = &g.hw[b];
    *sm = 0u;
    *offset = b == LINE ? LINE_OFFSET : PRE_OFFSET;
    g.sm[b].claimed = true;
    return true;
}
void pio_remove_program_and_unclaim_sm(const pio_program_t *prog, PIO pio,
                                       uint sm, uint offset)
{
    (void)prog;
    (void)sm;
    (void)offset;
    g.sm[block_of(pio)].claimed = false;
}
pio_sm_config pio_get_default_sm_config(void)
{
    pio_sm_config c;

    memset(&c, 0, sizeof(c));
    return c;
}
pio_sm_config hitec_line_program_get_default_config(uint offset)
{
    pio_sm_config c = pio_get_default_sm_config();

    sm_config_set_wrap(&c, offset, offset + HITEC_PIO_PROGRAM_WORDS - 1u);
    return c;
}
void sm_config_set_wrap(pio_sm_config *c, uint bottom, uint top)
{
    c->wrap_bottom = bottom;
    c->wrap_top = top;
}
void sm_config_set_out_pins(pio_sm_config *c, uint base, uint count)
{
    (void)count;
    c->out_pin = base;
}
void sm_config_set_set_pins(pio_sm_config *c, uint base, uint count)
{
    (void)count;
    c->set_pin = base;
}
void sm_config_set_in_pin_base(pio_sm_config *c, uint base) { c->in_pin = base; }
void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) { c->jmp_pin = pin; }
void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull,
                             uint thresh)
{
    c->out_right = right;
    c->out_auto = autopull;
    c->out_thresh = thresh;
}
void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush,
                            uint thresh)
{
    c->in_right = right;
    c->in_auto = autopush;
    c->in_thresh = thresh;
}
void sm_config_set_clkdiv_int_frac8(pio_sm_config *c, uint32_t i, uint8_t f)
{
    c->clk_int = i;
    c->clk_frac = f;
}
void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join join)
{
    c->join = (uint32_t)join;
}

/* ---------------------------------------------------------------- helpers */

static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    memset(&fake_io_bank0, 0, sizeof(fake_io_bank0));
    memset(&fake_pads_bank0, 0, sizeof(fake_pads_bank0));
    g.now_us = 1000u;
    hitec_line_init(&line);
}

/* The index of the first event @p e at or after @p from; MAX_EV for none. */
static unsigned first_ev(unsigned e, unsigned from)
{
    for (unsigned i = from; i < g.n_ev; ++i) {
        if (g.ev[i] == e) {
            return i;
        }
    }
    return MAX_EV;
}

/* The index of the last event @p e; MAX_EV for none. */
static unsigned last_ev(unsigned e)
{
    for (unsigned i = g.n_ev; i > 0u; --i) {
        if (g.ev[i - 1u] == e) {
            return i - 1u;
        }
    }
    return MAX_EV;
}

static unsigned count_ev(unsigned e)
{
    unsigned n = 0;

    for (unsigned i = 0; i < g.n_ev; ++i) {
        n += g.ev[i] == e ? 1u : 0u;
    }
    return n;
}

/* The word the program pushes for byte @p k of @p value: the 8 data bits, a
 * good stop bit, the tick counter 50 ticks lower a byte. */
static uint32_t stamp(unsigned k, uint8_t value)
{
    return (uint32_t)value | 1u << 8 | (line.run.window_ticks - 50u * (k + 1u)) << 9;
}

/* A word the program pushed and the channel took. */
static void taken(uint32_t w)
{
    g.sm[LINE].fifo[g.sm[LINE].n_fifo++] = w;
    g.sm[LINE].pushed++;
    dma_step(CH_RX);
}

/* A word the program pushed that the channel has not taken yet. */
static void in_fifo(uint32_t w)
{
    g.sm[LINE].fifo[g.sm[LINE].n_fifo++] = w;
    g.sm[LINE].pushed++;
}

/* A word the program pushes at the next call while it runs. */
static void pushes(uint32_t w)
{
    g.sm[LINE].pend[g.sm[LINE].n_pend++] = w;
}

/* Open a D series line and start a LISTEN for @p max_rx bytes, the first
 * answered with 0x96 when @p answer; the stream is out and the receiver
 * waits for a start bit. */
static void listening(unsigned max_rx, bool answer)
{
    hitec_xfer_t x;

    CHECK_EQ(hitec_line_open(&line, PIN, HITEC_D, HITEC_DRIVE_OPEN_DRAIN),
             HITEC_LINE_OK);
    CHECK(hitec_listen_xfer(HITEC_D, 20000u, (uint8_t)max_rx, answer, 0x96, &x));
    CHECK(hitec_line_start(&line, &x));
    CHECK_EQ(line.run.max_rx, max_rx);
    g.ch[CH_TX].busy = false;
    g.sm[LINE].pc = LINE_OFFSET + WAIT_HI_PC;
}

/* The time the driver allows the running transaction. */
static uint32_t deadline(void)
{
    return line.t_start_us + line.run.span_us + HITEC_LINE_GRACE_US;
}

/* The machine at idle, waiting for a stream, its pad released. */
static void check_restarted(void)
{
    CHECK(g.sm[LINE].enabled);
    CHECK_EQ(g.sm[LINE].pc, LINE_OFFSET + HITEC_PIO_IDLE_PC);
    CHECK(!g.sm[LINE].dir);
    CHECK_EQ(g.sm[LINE].n_fifo, 0u);
    CHECK_EQ(g.sm[LINE].restarts_running, 0u);
    CHECK(!hitec_line_busy(&line));
}

/* The order of a stop that keeps every byte: the machine stopped, the
 * channel took the FIFO, the count read with the machine stopped and the
 * FIFO empty, then the channel stopped, the FIFO cleared and the restart. */
static void check_kept_order(void)
{
    const unsigned stop = first_ev(EV(E_STOP, LINE), 0);
    const unsigned take = last_ev(EV(E_TAKE, CH_RX));
    const unsigned count = last_ev(EV(E_COUNT, CH_RX));
    const unsigned abort_rx = first_ev(EV(E_ABORT, CH_RX), stop);
    const unsigned clear = first_ev(EV(E_CLEAR, LINE), stop);
    const unsigned restart = first_ev(EV(E_RESTART, LINE), stop);

    CHECK(stop < MAX_EV);
    CHECK(stop < take && take < count);
    CHECK(count < abort_rx && abort_rx < clear && clear < restart);
    CHECK(restart < MAX_EV);
    CHECK(!g.count_running);
    CHECK_EQ(g.count_fifo, 0u);
    CHECK_EQ(g.sm[LINE].cleared, 0u);
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(a_restart_past_the_time_counts_the_byte_still_in_the_fifo)
{
    hitec_capture_t cap;

    fresh();
    listening(4, false);
    taken(stamp(0, 0x11));
    in_fifo(stamp(1, 0x22));
    g.now_us = deadline();
    g.n_ev = 0;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.byte[0].value, 0x11);
    CHECK_EQ(cap.byte[1].value, 0x22);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.full, 0);
    CHECK_EQ(cap.aborted, 0);
    check_kept_order();
    check_restarted();
}

TEST_CASE(a_byte_pushed_as_the_time_runs_out_is_counted)
{
    hitec_capture_t cap;

    /* One word, then three: each pushed at a call before the stop is in
     * the capture, and none is thrown away by the clear. */
    for (unsigned more = 1; more <= 3u; ++more) {
        fresh();
        listening(8, false);
        taken(stamp(0, 0x10));
        for (unsigned k = 0; k < more; ++k) {
            pushes(stamp(1u + k, (uint8_t)(0x20u + k)));
        }
        g.now_us = deadline();
        g.n_ev = 0;
        CHECK(hitec_line_poll(&line, &cap));
        CHECK(g.sm[LINE].pushed >= 2u);
        CHECK_EQ(cap.n, g.sm[LINE].pushed);
        for (unsigned k = 1; k < cap.n; ++k) {
            CHECK_EQ(cap.byte[k].value, 0x20u + k - 1u);
        }
        CHECK_EQ(cap.overflow, 1);
        CHECK_EQ(g.sm[LINE].dropped, 0u);
        check_kept_order();
        check_restarted();
    }
}

TEST_CASE(the_restart_comes_at_its_time_and_not_one_us_before)
{
    hitec_capture_t cap;
    static const uint32_t starts[] = { 1000u, 0xFFFFFF00u };

    for (unsigned i = 0; i < 2u; ++i) {
        fresh();
        g.now_us = starts[i];     /* the second across the clock's wrap */
        listening(4, false);
        CHECK_EQ(line.t_start_us, starts[i]);
        taken(stamp(0, 0x5A));
        g.now_us = deadline() - 1u;
        g.n_ev = 0;
        CHECK(!hitec_line_poll(&line, &cap));
        CHECK_EQ(count_ev(EV(E_STOP, LINE)), 0u);
        CHECK_EQ(count_ev(EV(E_RESTART, LINE)), 0u);
        CHECK(g.sm[LINE].enabled);
        CHECK(hitec_line_busy(&line));
        g.now_us = deadline();
        CHECK(hitec_line_poll(&line, &cap));
        CHECK_EQ(cap.n, 1);
        CHECK_EQ(cap.overflow, 1);
        check_restarted();
    }
    CHECK(deadline() < starts[1]);     /* it did wrap */
}

TEST_CASE(the_settle_gives_up_after_64_reads_of_a_fifo_no_channel_takes)
{
    hitec_capture_t cap;

    fresh();
    listening(4, false);
    taken(stamp(0, 0x33));
    in_fifo(stamp(1, 0x44));
    g.dma_stuck = true;
    g.now_us = deadline();
    g.spins = 0;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(g.spins, 64u);
    /* The byte the channel never took is not in the capture, and the
     * restart clears it out of the FIFO. */
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(g.sm[LINE].cleared, 1u);
    check_restarted();
}

TEST_CASE(the_next_transaction_gets_none_of_the_words_of_the_last)
{
    hitec_capture_t cap;
    hitec_xfer_t x;
    unsigned clear;
    unsigned conf;

    /* 2 bytes taken, a third the channel has no room for: the receive is
     * full, and the restart clears the third from the FIFO. */
    fresh();
    listening(2, false);
    taken(stamp(0, 0x01));
    taken(stamp(1, 0x02));
    in_fifo(stamp(2, 0x03));
    CHECK(!g.ch[CH_RX].busy);
    g.n_ev = 0;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.full, 1);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(g.sm[LINE].cleared, 1u);
    CHECK(last_ev(EV(E_COUNT, CH_RX)) < first_ev(EV(E_CLEAR, LINE), 0));
    check_restarted();

    /* A word left in the FIFO all the same: the next start clears it
     * before the receive channel is armed, and the next capture has only
     * its own byte. */
    in_fifo(stamp(0, 0xEE));
    CHECK(hitec_listen_xfer(HITEC_D, 20000u, 2, false, 0, &x));
    g.n_ev = 0;
    CHECK(hitec_line_start(&line, &x));
    clear = first_ev(EV(E_CLEAR, LINE), 0);
    conf = first_ev(EV(E_CONF, CH_RX), 0);
    CHECK(clear < conf && conf < MAX_EV);
    CHECK_EQ(g.sm[LINE].n_fifo, 0u);
    g.ch[CH_TX].busy = false;
    g.sm[LINE].pc = LINE_OFFSET + WAIT_HI_PC;
    taken(stamp(0, 0x77));
    g.sm[LINE].pc = LINE_OFFSET + HITEC_PIO_IDLE_PC;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.byte[0].value, 0x77);
}

TEST_CASE(a_receive_that_ends_at_idle_takes_the_last_word_from_the_fifo)
{
    hitec_capture_t cap;

    fresh();
    listening(4, false);
    taken(stamp(0, 0xA1));
    in_fifo(stamp(1, 0xA2));
    g.sm[LINE].pc = LINE_OFFSET + HITEC_PIO_IDLE_PC;   /* the window is over */
    g.n_ev = 0;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 2);
    CHECK_EQ(cap.byte[1].value, 0xA2);
    CHECK_EQ(cap.full, 0);
    CHECK_EQ(cap.overflow, 0);
    /* No restart: the machine waits already.  The channel took the word,
     * then the count, then the channel stopped. */
    CHECK_EQ(count_ev(EV(E_RESTART, LINE)), 0u);
    CHECK(first_ev(EV(E_TAKE, CH_RX), 0) < last_ev(EV(E_COUNT, CH_RX)));
    CHECK(last_ev(EV(E_COUNT, CH_RX)) < last_ev(EV(E_ABORT, CH_RX)));
    CHECK_EQ(g.count_fifo, 0u);
    CHECK(!hitec_line_busy(&line));
}

TEST_CASE(a_one_byte_listen_lets_its_answer_out_before_the_restart)
{
    hitec_capture_t cap;
    static const unsigned due[] = {
        HITEC_PIO_ASK_PC, HITEC_PIO_ANSWER_PC, HITEC_PIO_ANSWER_PC + 2u,
        HITEC_PIO_BIT_PC, HITEC_PIO_RELEASE_PC - 1u,
    };

    fresh();
    listening(1, true);
    taken(stamp(0, 0x5C));
    /* Full, the answer on its way: no restart while it is due. */
    for (unsigned i = 0; i < sizeof(due) / sizeof(due[0]); ++i) {
        g.sm[LINE].pc = LINE_OFFSET + due[i];
        g.n_ev = 0;
        CHECK(!hitec_line_poll(&line, &cap));
        CHECK_EQ(count_ev(EV(E_RESTART, LINE)), 0u);
        CHECK(g.sm[LINE].enabled);
    }
    /* Released after the answer: the restart, and the answer whole. */
    g.sm[LINE].pc = LINE_OFFSET + HITEC_PIO_RELEASE_PC;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.full, 1);
    CHECK_EQ(cap.answered, 1);
    check_restarted();
}

TEST_CASE(a_restart_past_the_time_inside_the_answer_marks_it_cut)
{
    static const struct {
        unsigned pc;
        bool cut;
    } c[] = {
        { HITEC_PIO_ANSWER_PC, true },
        { HITEC_PIO_BIT_PC + 1u, true },
        { WAIT_HI_PC, false },
    };

    /* The machine stuck where it is past the time: the capture says
     * whether the answer went out whole, read where the machine stopped,
     * not where the restart put it. */
    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        hitec_capture_t cap;

        fresh();
        listening(2, true);
        taken(stamp(0, 0x5C));
        g.sm[LINE].pc = LINE_OFFSET + c[i].pc;
        g.now_us = deadline();
        CHECK(hitec_line_poll(&line, &cap));
        CHECK_EQ(cap.n, 1);
        CHECK_EQ(cap.overflow, 1);
        CHECK_EQ(cap.answered, c[i].cut ? 0 : 1);
        check_restarted();
    }
}

TEST_CASE(an_abort_counts_the_byte_still_in_the_fifo_before_the_restart)
{
    hitec_capture_t cap;

    fresh();
    listening(4, false);
    taken(stamp(0, 0x61));
    in_fifo(stamp(1, 0x62));
    pushes(stamp(2, 0x63));
    g.n_ev = 0;
    hitec_line_abort(&line);
    /* Released at the abort, before any poll. */
    CHECK(!g.sm[LINE].dir);
    CHECK(first_ev(EV(E_RELEASE, LINE), 0) < MAX_EV);
    CHECK_EQ(line.kept, g.sm[LINE].pushed);
    CHECK(line.kept >= 2u);
    check_kept_order();
    CHECK(hitec_line_busy(&line));
    g.n_ev = 0;
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.aborted, 1);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(cap.n, g.sm[LINE].pushed);
    CHECK_EQ(cap.byte[0].value, 0x61);
    CHECK_EQ(cap.byte[1].value, 0x62);
    /* The poll after an abort touches the machine no more. */
    CHECK_EQ(count_ev(EV(E_RESTART, LINE)), 0u);
    check_restarted();
}

TEST_CASE(an_abort_inside_the_answer_cuts_it_and_one_after_it_does_not)
{
    static const struct {
        unsigned pc;
        bool cut;
    } c[] = {
        { HITEC_PIO_ASK_PC, true },
        { HITEC_PIO_ANSWER_PC + 1u, true },
        { HITEC_PIO_BIT_PC, true },
        { HITEC_PIO_RELEASE_PC - 1u, true },
        { HITEC_PIO_RELEASE_PC, false },
        { WAIT_HI_PC, false },
    };

    for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        hitec_capture_t cap;

        fresh();
        listening(2, true);
        taken(stamp(0, 0x5C));
        g.sm[LINE].pc = LINE_OFFSET + c[i].pc;
        hitec_line_abort(&line);
        CHECK_EQ(line.cut, c[i].cut ? 1u : 0u);
        CHECK(hitec_line_poll(&line, &cap));
        CHECK_EQ(cap.n, 1);
        CHECK_EQ(cap.aborted, 1);
        CHECK_EQ(cap.answered, c[i].cut ? 0 : 1);
    }
}

TEST_CASE(an_abort_with_nothing_running_or_a_second_one_does_nothing)
{
    hitec_capture_t cap;
    unsigned n;

    fresh();
    hitec_line_abort(NULL);
    hitec_line_abort(&line);                 /* closed */
    CHECK_EQ(g.n_ev, 0u);
    CHECK_EQ(hitec_line_open(&line, PIN, HITEC_D, HITEC_DRIVE_OPEN_DRAIN),
             HITEC_LINE_OK);
    g.n_ev = 0;
    hitec_line_abort(&line);                 /* open, no transaction */
    CHECK_EQ(g.n_ev, 0u);
    CHECK(!hitec_line_poll(&line, &cap));

    hitec_line_close(&line);
    fresh();
    listening(4, false);
    taken(stamp(0, 0x01));
    hitec_line_abort(&line);
    n = g.n_ev;
    in_fifo(stamp(1, 0x02));
    hitec_line_abort(&line);                 /* the second */
    CHECK_EQ(g.n_ev, n);
    CHECK_EQ(line.kept, 1u);
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.aborted, 1);
}

/* A D series read on a PRECHARGE line: the request on the line's machine,
 * then the sampler.  Returns false when the plan was not a SEND. */
static bool precharge_read(void)
{
    hitec_xfer_t x;

    CHECK_EQ(hitec_line_open_rx(&line, PIN, HITEC_D, HITEC_DRIVE_OPEN_DRAIN,
                                HITEC_RX_PRECHARGE),
             HITEC_LINE_OK);
    hitec_d_read_xfer(0x00, 0x0C, &x);
    CHECK(hitec_line_start(&line, &x));
    CHECK_EQ(line.pre.phase, HITEC_PRE_TX);
    g.sm[LINE].pc = LINE_OFFSET + HITEC_PIO_BIT_PC;   /* sending */
    return line.pre_on != 0u;
}

TEST_CASE(an_abort_of_a_precharge_request_restarts_the_line)
{
    hitec_capture_t cap;
    const uint32_t ctrl_line = (FAKE_FUNC_PIO0 + LINE)
                               | (uint32_t)GPIO_OVERRIDE_INVERT
                                     << FAKE_CTRL_INOVER_LSB;

    fresh();
    CHECK(precharge_read());
    CHECK_EQ(fake_io_bank0.io[PIN].ctrl, ctrl_line);
    g.n_ev = 0;
    hitec_line_abort(&line);
    CHECK(first_ev(EV(E_STOP, LINE), 0) < first_ev(EV(E_RESTART, LINE), 0));
    CHECK(!g.sm[LINE].dir);
    /* The sampler never had the pad. */
    CHECK_EQ(count_ev(EV(E_CONF, CH_PRE)), 0u);
    CHECK_EQ(fake_io_bank0.io[PIN].ctrl, ctrl_line);
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.aborted, 1);
    CHECK_EQ(cap.n, 0);
    check_restarted();
}

TEST_CASE(an_abort_while_the_sampler_has_the_pad_gives_it_back)
{
    hitec_capture_t cap;
    uint32_t ctrl;
    uint32_t pad;
    unsigned stop;

    fresh();
    /* 4mA and fast slew before the sampler: its 2mA and slow slew have to
     * be undone. */
    fake_pads_bank0.io[PIN] = (uint32_t)GPIO_DRIVE_STRENGTH_4MA
                                  << FAKE_PAD_DRIVE_LSB
                              | FAKE_PAD_SLEW_FAST;
    CHECK(precharge_read());
    ctrl = fake_io_bank0.io[PIN].ctrl;
    pad = fake_pads_bank0.io[PIN];
    /* The request is out: the next poll hands the pad to the sampler. */
    g.ch[CH_TX].busy = false;
    g.sm[LINE].pc = LINE_OFFSET + HITEC_PIO_IDLE_PC;
    CHECK(!hitec_line_poll(&line, &cap));
    CHECK_EQ(line.pre.phase, HITEC_PRE_SAMPLE);
    CHECK_EQ(fake_io_bank0.io[PIN].ctrl, FAKE_FUNC_PIO0 + SAMPLER);
    CHECK_EQ((fake_pads_bank0.io[PIN] & FAKE_PAD_DRIVE_BITS) >> FAKE_PAD_DRIVE_LSB,
             GPIO_DRIVE_STRENGTH_2MA);
    CHECK_EQ(fake_pads_bank0.io[PIN] & FAKE_PAD_SLEW_FAST, 0u);
    CHECK(pad != fake_pads_bank0.io[PIN]);
    CHECK(g.sm[SAMPLER].enabled);
    CHECK(g.ch[CH_PRE].busy);
    /* The driver's write clears the stall flag on the chip. */
    g.hw[SAMPLER].fdebug = 0;
    /* Two sample words in the sampler's FIFO the channel has not taken. */
    g.sm[SAMPLER].fifo[0] = 0xFFFFFFFFu;
    g.sm[SAMPLER].fifo[1] = 0xFFFFFFFFu;
    g.sm[SAMPLER].n_fifo = 2;
    g.n_ev = 0;
    hitec_line_abort(&line);
    stop = first_ev(EV(E_STOP, SAMPLER), 0);
    CHECK(stop < first_ev(EV(E_TAKE, CH_PRE), 0));
    CHECK(first_ev(EV(E_RELEASE, SAMPLER), 0) < MAX_EV);
    CHECK(last_ev(EV(E_TAKE, CH_PRE)) < first_ev(EV(E_CLEAR, SAMPLER), 0));
    CHECK_EQ(g.sm[SAMPLER].cleared, 0u);
    CHECK(!g.sm[SAMPLER].enabled);
    /* The pin's two registers as they were before the sampler. */
    CHECK_EQ(fake_io_bank0.io[PIN].ctrl, ctrl);
    CHECK_EQ(fake_pads_bank0.io[PIN], pad);
    CHECK_EQ(line.pre_held, 0u);
    CHECK(hitec_line_poll(&line, &cap));
    CHECK_EQ(cap.aborted, 1);
    CHECK_EQ(cap.overflow, 0);
    CHECK(!hitec_line_busy(&line));
}

int main(void)
{
    RUN(a_restart_past_the_time_counts_the_byte_still_in_the_fifo);
    RUN(a_byte_pushed_as_the_time_runs_out_is_counted);
    RUN(the_restart_comes_at_its_time_and_not_one_us_before);
    RUN(the_settle_gives_up_after_64_reads_of_a_fifo_no_channel_takes);
    RUN(the_next_transaction_gets_none_of_the_words_of_the_last);
    RUN(a_receive_that_ends_at_idle_takes_the_last_word_from_the_fifo);
    RUN(a_one_byte_listen_lets_its_answer_out_before_the_restart);
    RUN(a_restart_past_the_time_inside_the_answer_marks_it_cut);
    RUN(an_abort_counts_the_byte_still_in_the_fifo_before_the_restart);
    RUN(an_abort_inside_the_answer_cuts_it_and_one_after_it_does_not);
    RUN(an_abort_with_nothing_running_or_a_second_one_does_nothing);
    RUN(an_abort_of_a_precharge_request_restarts_the_line);
    RUN(an_abort_while_the_sampler_has_the_pad_gives_it_back);
    return test_summary("hitec_line");
}
