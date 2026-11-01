/*
 * The signal wire's RP2350 driver (protocols/escwire/rp2350/ew_line.c) against
 * a fake of the pico-sdk calls it makes (fake_pico/): what it leaves on the pad
 * and at which moment, what it claims and gives back.
 *
 * Under test: the pad at the open, at every level, in order, so a pad going
 * driven never shows the old latch; the rest after a good transaction; the
 * parked pad on every error path, before the channels are waited for; a
 * refused start and a refused open touching nothing; the resources on every
 * way out.
 *
 * Not tested here: the chip.  test_ew_pio runs the program; the console test
 * runs this driver on a pin (docs/BenchConsole.md).
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ew_line.h"
#include "hardware/pio.h"

#define PIN 13u
#define OFFSET 4u
#define SYS_HZ 150000000u

/* --------------------------------------------------------------- the fake */

static struct {
    /* the pad */
    bool out;            /* the machine's direction */
    bool latch;
    bool pull_up;
    bool pull_down;
    bool input;
    bool pio_func;
    enum gpio_drive_strength drive;
    enum gpio_slew_rate slew;
    /* the order of what happens */
    unsigned pad_writes;
    bool latch_at_drive; /* the latch when the pad last went driven */
    unsigned rises;      /* driven pad released onto its pull-up */
    unsigned dips;       /* undriven pad put on its pull-down */
    /* the machine */
    pio_hw_t hw;
    bool enabled;
    uint32_t pc;
    uint32_t clk_int;
    uint32_t clk_frac;
    pio_sm_config cfg;
    bool tx_empty;
    bool rx_empty;
    unsigned restarts;
    /* the channels */
    bool dma_busy[2];
    dma_channel_hw_t dma_hw[2];
    int claimed;
    int free_channels;
    dma_channel_config rx_cfg;
    const volatile void *rx_read;
    uint32_t rx_count;
    uint32_t tx_count;
    /* the claims */
    bool pio_refuses;
    bool sm_claimed;
    /* the rest */
    uint32_t sys_hz;
    uint32_t now_us;
    /* the pad as it was at the first read of the receive FIFO's level */
    bool settle_seen;
    bool out_at_settle;
    bool down_at_settle;
    bool input_at_settle;
} g;

const pio_program_t ew_line_program = { -1 };

uint32_t clock_get_hz(enum clock_index c) { (void)c; return g.sys_hz; }
uint32_t time_us_32(void) { return g.now_us; }
void tight_loop_contents(void) { g.rx_empty = true; }

bool dma_channel_is_busy(uint ch) { return g.dma_busy[ch]; }
void dma_channel_abort(uint ch) { g.dma_busy[ch] = false; }
void dma_channel_unclaim(uint ch) { (void)ch; g.claimed--; g.free_channels++; }
int dma_claim_unused_channel(bool required)
{
    (void)required;
    if (g.free_channels <= 0) {
        return -1;
    }
    g.free_channels--;
    return g.claimed++;
}
dma_channel_hw_t *dma_channel_hw_addr(uint ch) { return &g.dma_hw[ch]; }
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
    (void)write;
    g.dma_busy[ch] = trigger;
    if (ch == 1u) {
        g.rx_cfg = *c;
        g.rx_read = read;
        g.rx_count = count;
        g.dma_hw[1].transfer_count = count;
    } else {
        g.tx_count = count;
    }
}

void gpio_init(uint pin)
{
    (void)pin;
    if (g.out && g.pull_up) {
        g.rises++;
    }
    g.out = false;
    g.pio_func = false;
    g.input = true;
}
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_set_pulls(uint pin, bool up, bool down)
{
    (void)pin;
    if (down && !g.pull_down && !g.out) {
        g.dips++;
    }
    g.pull_up = up;
    g.pull_down = down;
    g.pad_writes++;
}
void gpio_set_input_enabled(uint pin, bool on)
{
    (void)pin;
    g.input = on;
    g.pad_writes++;
}
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength d)
{
    (void)pin;
    g.drive = d;
    g.pad_writes++;
}
void gpio_set_slew_rate(uint pin, enum gpio_slew_rate s)
{
    (void)pin;
    g.slew = s;
    g.pad_writes++;
}
enum gpio_drive_strength gpio_get_drive_strength(uint pin)
{
    (void)pin;
    return g.drive;
}
enum gpio_slew_rate gpio_get_slew_rate(uint pin) { (void)pin; return g.slew; }

uint pio_encode_set(enum pio_src_dest dest, uint value)
{
    return 0x1000u | ((uint)dest << 4) | value;
}
uint pio_encode_jmp(uint addr) { return 0x2000u | addr; }
void pio_sm_exec(PIO pio, uint sm, uint instr)
{
    (void)pio;
    (void)sm;
    if ((instr & 0xF000u) == 0x2000u) {
        g.pc = instr & 0xFFFu;
    } else if (((instr >> 4) & 0xFu) == (uint)pio_pins) {
        g.latch = (instr & 1u) != 0u;
    } else {
        if (g.out && (instr & 1u) == 0u && g.pull_up) {
            g.rises++;
        }
        g.out = (instr & 1u) != 0u;
        if (g.out) {
            g.latch_at_drive = g.latch;
        }
    }
    g.pad_writes++;
}
void pio_sm_set_enabled(PIO pio, uint sm, bool on)
{
    (void)pio;
    (void)sm;
    g.enabled = on;
}
void pio_sm_clear_fifos(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    g.rx_empty = true;
    g.tx_empty = true;
}
void pio_sm_restart(PIO pio, uint sm) { (void)pio; (void)sm; g.restarts++; }
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *c)
{
    (void)pio;
    (void)sm;
    g.pc = pc;
    g.cfg = *c;
    g.enabled = false;
}
void pio_sm_set_clkdiv_int_frac8(PIO pio, uint sm, uint32_t i, uint8_t f)
{
    (void)pio;
    (void)sm;
    g.clk_int = i;
    g.clk_frac = f;
}
uint pio_sm_get_pc(PIO pio, uint sm) { (void)pio; (void)sm; return g.pc; }
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    return g.tx_empty;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    if (!g.settle_seen) {
        g.settle_seen = true;
        g.out_at_settle = g.out;
        g.down_at_settle = g.pull_down;
        g.input_at_settle = g.input;
    }
    return g.rx_empty;
}
uint pio_get_dreq(PIO pio, uint sm, bool tx)
{
    (void)pio;
    (void)sm;
    return tx ? 1u : 2u;
}
void pio_gpio_init(PIO pio, uint pin)
{
    (void)pio;
    (void)pin;
    g.pio_func = true;
    g.input = true;
}
bool pio_claim_free_sm_and_add_program_for_gpio_range(
    const pio_program_t *prog, PIO *pio, uint *sm, uint *offset, uint base,
    uint count, bool set_range)
{
    (void)prog;
    (void)base;
    (void)count;
    (void)set_range;
    if (g.pio_refuses) {
        return false;
    }
    *pio = &g.hw;
    *sm = 1u;
    *offset = OFFSET;
    g.sm_claimed = true;
    return true;
}
void pio_remove_program_and_unclaim_sm(const pio_program_t *prog, PIO pio,
                                       uint sm, uint offset)
{
    (void)prog;
    (void)pio;
    (void)sm;
    (void)offset;
    g.sm_claimed = false;
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
    c->clkdiv_int = i;
    c->clkdiv_frac = f;
}
pio_sm_config ew_line_program_get_default_config(uint offset)
{
    pio_sm_config c;

    (void)offset;
    memset(&c, 0, sizeof(c));
    return c;
}

/* ---------------------------------------------------------------- helpers */

static ew_pin_line_t line;
static const uint8_t k_tx[] = { 0xFF, 0x00, 0x1A, 0x00, 0x3B, 0x74 };
static uint8_t rx[16];

static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    g.sys_hz = SYS_HZ;
    g.free_channels = 2;
    g.tx_empty = true;
    g.rx_empty = true;
    g.drive = GPIO_DRIVE_STRENGTH_8MA;
    g.slew = GPIO_SLEW_RATE_FAST;
    g.input = true;
    ew_line_init(&line);
}

static ew_xfer_t xfer(uint16_t max_rx)
{
    ew_xfer_t x;

    memset(&x, 0, sizeof(x));
    x.rate = EW_RATE_A;
    x.n_tx = sizeof(k_tx);
    x.tx = k_tx;
    x.max_rx = max_rx;
    x.first_us = max_rx != 0u ? 4000u : 0u;
    x.gap_us = max_rx != 0u ? 2000u : 0u;
    x.rx = max_rx != 0u ? rx : NULL;
    return x;
}

/* The line open and a transaction of @p max_rx on the wire, mid-byte. */
static void running(uint16_t max_rx)
{
    const ew_xfer_t x = xfer(max_rx);

    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    CHECK(ew_line_start(&line, &x));
    g.hw.fdebug = 0u;                      /* the chip clears on that write */
    g.out = true;
    g.latch = false;
    g.pc = OFFSET + 7u;
    g.dma_busy[0] = false;
}

static void check_parked(void)
{
    CHECK(!g.out);
    CHECK(g.pull_down);
    CHECK(!g.pull_up);
    CHECK(!g.input);
}

static void check_listening(void)
{
    CHECK(!g.out);
    CHECK(g.pull_up);
    CHECK(!g.pull_down);
    CHECK(g.input);
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(an_open_parks_the_pad_and_claims_what_it_says)
{
    fresh();
    CHECK(!ew_line_is_open(&line));
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    CHECK(ew_line_is_open(&line));
    check_parked();
    CHECK(g.pio_func);
    CHECK(g.enabled);
    CHECK(g.latch);
    CHECK_EQ(g.drive, GPIO_DRIVE_STRENGTH_2MA);
    CHECK_EQ(g.slew, GPIO_SLEW_RATE_SLOW);
    CHECK(g.sm_claimed);
    CHECK_EQ(g.claimed, 2);
    /* Shifts: right, no autopull, autopush at 9 bits. */
    CHECK(g.cfg.out_right);
    CHECK(!g.cfg.out_auto);
    CHECK_EQ(g.cfg.out_thresh, 32u);
    CHECK(g.cfg.in_right);
    CHECK(g.cfg.in_auto);
    CHECK_EQ(g.cfg.in_thresh, 9u);
    CHECK_EQ(g.cfg.jmp_pin, PIN);
    CHECK_EQ(g.cfg.clkdiv_int, 487u);
    CHECK_EQ(g.cfg.clkdiv_frac, 128u);
    CHECK_EQ(ew_line_bit_ns(&line, EW_RATE_A), 52000u);
    CHECK_EQ(ew_line_bit_ns(&line, EW_RATE_TEXT), 26042u);
    CHECK_EQ(ew_line_bit_ns(&line, EW_RATES), 0u);
}

TEST_CASE(a_refused_open_touches_nothing)
{
    fresh();
    CHECK_EQ(ew_line_open(NULL, PIN), EW_LINE_ERR_ARG);
    CHECK_EQ(ew_line_open(&line, NUM_BANK0_GPIOS), EW_LINE_ERR_ARG);
    g.sys_hz = 1000000u;
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_ERR_CLOCK);
    g.sys_hz = SYS_HZ;
    g.pio_refuses = true;
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_ERR_PIO);
    g.pio_refuses = false;
    g.free_channels = 1;
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_ERR_DMA);
    CHECK_EQ(g.claimed, 0);
    CHECK_EQ(g.free_channels, 1);
    g.free_channels = 0;
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_ERR_DMA);
    CHECK(!g.sm_claimed);
    CHECK_EQ(g.pad_writes, 0u);
    CHECK(!g.pio_func);
    CHECK(!ew_line_is_open(&line));
}

TEST_CASE(a_close_gives_everything_back_from_any_state)
{
    running(9u);
    ew_line_close(&line);
    CHECK(!ew_line_is_open(&line));
    CHECK(!ew_line_busy(&line));
    CHECK(!g.sm_claimed);
    CHECK_EQ(g.claimed, 0);
    CHECK(!g.enabled);
    CHECK(!g.pio_func);
    CHECK(g.pull_down);
    CHECK(!g.pull_up);
    CHECK(g.input);
    /* The drive and slew as the open found them. */
    CHECK_EQ(g.drive, GPIO_DRIVE_STRENGTH_8MA);
    CHECK_EQ(g.slew, GPIO_SLEW_RATE_FAST);
    /* A second close and a close of nothing do nothing. */
    g.pad_writes = 0u;
    ew_line_close(&line);
    ew_line_close(NULL);
    CHECK_EQ(g.pad_writes, 0u);
    /* A line can open again. */
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    /* And an open of an open line closes it first. */
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    CHECK_EQ(g.claimed, 2);
}

TEST_CASE(each_level_on_the_pad)
{
    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    ew_line_level(&line, EW_LINE_LOW);
    CHECK(!g.latch_at_drive);              /* driven at its own level at once */
    ew_line_level(&line, EW_LINE_HIGH);
    CHECK(g.out);
    CHECK(g.latch);
    CHECK(g.latch_at_drive);
    CHECK(g.input);
    ew_line_level(&line, EW_LINE_LOW);
    CHECK(g.out);
    CHECK(!g.latch);
    CHECK(!g.latch_at_drive);
    CHECK(g.pull_down);
    CHECK(!g.input);
    ew_line_level(&line, EW_LINE_UP);
    check_listening();
    ew_line_level(&line, EW_LINE_PARK);
    check_parked();
    /* An unknown level parks. */
    ew_line_level(&line, EW_LINE_HIGH);
    ew_line_level(&line, (ew_line_t)99);
    check_parked();
    /* Nothing on a closed line, or with no line. */
    ew_line_close(&line);
    g.pad_writes = 0u;
    ew_line_level(&line, EW_LINE_HIGH);
    ew_line_level(NULL, EW_LINE_HIGH);
    ew_line_abort(&line);
    ew_line_abort(NULL);
    CHECK_EQ(g.pad_writes, 0u);
}

TEST_CASE(a_start_listens_and_sets_the_rate)
{
    const uint8_t *top;
    ew_xfer_t x = xfer(9u);

    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    g.pc = OFFSET;
    g.hw.fdebug = 0u;
    CHECK(ew_line_start(&line, &x));
    CHECK(ew_line_busy(&line));
    check_listening();
    CHECK_EQ(g.clk_int, 487u);
    CHECK_EQ(g.clk_frac, 128u);
    /* The channel takes a FIFO word's upper half. */
    top = (const uint8_t *)&g.hw.rxf[1] + 2;
    CHECK(g.rx_read == (const volatile void *)top);
    CHECK_EQ(g.rx_cfg.data_size, DMA_SIZE_16);
    CHECK_EQ(g.rx_count, 9u);
    CHECK_EQ(g.tx_count, line.run.n_words);
    /* The stall flag of this machine cleared by a write of its bit. */
    CHECK_EQ(g.hw.fdebug, 1u << 1);

    /* The text rate's divider. */
    ew_line_abort(&line);
    g.pc = OFFSET;
    x = xfer(0u);
    x.rate = EW_RATE_TEXT;
    CHECK(ew_line_start(&line, &x));
    CHECK_EQ(g.clk_int, 244u);
    CHECK_EQ(g.clk_frac, 36u);
}

TEST_CASE(a_refused_start_touches_nothing)
{
    ew_xfer_t x = xfer(1u);

    fresh();
    CHECK(!ew_line_start(&line, &x));      /* closed */
    CHECK(!ew_line_start(NULL, &x));
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    ew_line_level(&line, EW_LINE_HIGH);
    g.pad_writes = 0u;
    CHECK(!ew_line_start(&line, NULL));
    x.n_tx = 0u;
    CHECK(!ew_line_start(&line, &x));      /* ew_xfer_valid() */
    x = xfer(1u);
    x.rate = EW_RATES;
    CHECK(!ew_line_start(&line, &x));
    x = xfer(1u);
    g.pc = OFFSET + 3u;
    CHECK(!ew_line_start(&line, &x));      /* the machine is not at rest */
    g.pc = OFFSET;
    g.dma_busy[0] = true;
    CHECK(!ew_line_start(&line, &x));
    g.dma_busy[0] = false;
    g.tx_empty = false;
    CHECK(!ew_line_start(&line, &x));
    g.tx_empty = true;
    CHECK_EQ(g.pad_writes, 0u);
    CHECK(!ew_line_busy(&line));
    CHECK(g.out);                          /* still as line() left it */
    CHECK(ew_line_start(&line, &x));
    CHECK(!ew_line_start(&line, &x));      /* one already runs */
}

TEST_CASE(a_good_transaction_rests_on_the_pull_up)
{
    ew_capture_t c;

    /* Runs: no capture. */
    running(2u);
    g.out = false;
    g.now_us = 10u;
    CHECK(!ew_line_poll(&line, &c));
    CHECK(ew_line_busy(&line));
    CHECK(!ew_line_poll(NULL, &c));
    CHECK(!ew_line_poll(&line, NULL));

    /* Ends at a wait: the machine back on its first `pull`. */
    g.pc = OFFSET;
    g.dma_hw[1].transfer_count = 1u;
    line.rx[0] = (uint16_t)(0x8000u | (0x30u << 7));
    CHECK(ew_line_poll(&line, &c));
    CHECK_EQ(c.n, 1u);
    CHECK(!c.aborted);
    CHECK(!c.overflow);
    CHECK_EQ(rx[0], 0x30u);
    check_listening();
    CHECK(!ew_line_busy(&line));
    CHECK(!ew_line_poll(&line, &c));       /* one capture */

    /* Ends at its count, the machine still waiting on the wire. */
    running(2u);
    g.out = false;
    g.dma_hw[1].transfer_count = 0u;
    line.rx[0] = line.rx[1] = (uint16_t)(0x8000u | (0x55u << 7));
    CHECK(ew_line_poll(&line, &c));
    CHECK_EQ(c.n, 2u);
    CHECK_EQ(g.pc, OFFSET);
    CHECK(g.restarts > 0u);
    check_listening();

    /* A framing error is an answer: the session judges it, on the pull-up. */
    running(1u);
    g.out = false;
    g.dma_hw[1].transfer_count = 0u;
    line.rx[0] = (uint16_t)(0x30u << 7);
    CHECK(ew_line_poll(&line, &c));
    CHECK_EQ(c.bad_stop, 1u);
    check_listening();
}

TEST_CASE(a_cut_past_its_time_parks_before_it_waits_for_the_channel)
{
    ew_capture_t c;

    /* The pad still driven, low, the transmit never ended. */
    running(4u);
    g.now_us = line.t_start_us + line.run.span_us + EW_PIO_GRACE_US;
    CHECK(!ew_line_poll(&line, &c));
    g.now_us += 1u;
    g.dma_hw[1].transfer_count = 3u;
    g.rx_empty = false;
    line.rx[0] = (uint16_t)(0x8000u | (0x11u << 7));
    CHECK(ew_line_poll(&line, &c));
    CHECK(g.settle_seen);
    CHECK(!g.out_at_settle);
    CHECK(g.down_at_settle);
    CHECK(!g.input_at_settle);
    check_parked();
    CHECK(c.aborted);
    CHECK_EQ(c.n, 1u);
    CHECK(!ew_line_busy(&line));
    CHECK_EQ(g.pc, OFFSET);
    CHECK(g.enabled);

    /* A transaction with no receive, the same. */
    running(0u);
    g.now_us = line.t_start_us + line.run.span_us + EW_PIO_GRACE_US + 1u;
    CHECK(ew_line_poll(&line, &c));
    check_parked();
    CHECK(c.aborted);
    CHECK_EQ(c.n, 0u);

    /* The next start takes the pad from there. */
    {
        const ew_xfer_t x = xfer(1u);

        CHECK(ew_line_start(&line, &x));
        check_listening();
    }
}

TEST_CASE(a_capture_with_lost_bytes_parks)
{
    ew_capture_t c;

    running(4u);
    g.out = false;
    g.pc = OFFSET;
    g.dma_hw[1].transfer_count = 2u;
    g.hw.fdebug = 1u << 1;                 /* this machine's stall */
    CHECK(ew_line_poll(&line, &c));
    CHECK(c.overflow);
    check_parked();

    /* Another machine's stall is not this line's. */
    running(4u);
    g.out = false;
    g.pc = OFFSET;
    g.dma_hw[1].transfer_count = 2u;
    g.hw.fdebug = 1u << 2;
    CHECK(ew_line_poll(&line, &c));
    CHECK(!c.overflow);
    check_listening();

    /* A stall after the last byte loses nothing. */
    running(4u);
    g.out = false;
    g.dma_hw[1].transfer_count = 0u;
    g.hw.fdebug = 1u << 1;
    CHECK(ew_line_poll(&line, &c));
    CHECK(!c.overflow);
    check_listening();
}

TEST_CASE(an_abort_parks_at_once_with_no_capture)
{
    ew_capture_t c;

    running(4u);
    ew_line_abort(&line);
    check_parked();
    CHECK(!ew_line_busy(&line));
    CHECK(!ew_line_poll(&line, &c));
    CHECK_EQ(g.pc, OFFSET);
    CHECK(!g.dma_busy[1]);
    /* With nothing running as well, from a driven level. */
    ew_line_level(&line, EW_LINE_HIGH);
    ew_line_abort(&line);
    check_parked();
}

TEST_CASE(no_error_path_lets_a_driven_pad_rise)
{
    ew_capture_t c;

    /* A cut in a low bit: the pull-down is on before the pad lets go. */
    running(4u);
    ew_line_abort(&line);
    CHECK_EQ(g.rises, 0u);
    running(4u);
    ew_line_level(&line, EW_LINE_PARK);
    CHECK_EQ(g.rises, 0u);
    running(4u);
    g.now_us = line.t_start_us + line.run.span_us + EW_PIO_GRACE_US + 1u;
    CHECK(ew_line_poll(&line, &c));
    CHECK_EQ(g.rises, 0u);
    running(4u);
    ew_line_close(&line);
    CHECK_EQ(g.rises, 0u);
    /* The leave's low to parked. */
    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    ew_line_level(&line, EW_LINE_LOW);
    ew_line_level(&line, EW_LINE_PARK);
    CHECK_EQ(g.rises, 0u);
    check_parked();
}

TEST_CASE(a_start_from_the_rest_does_not_dip)
{
    ew_xfer_t x = xfer(1u);

    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    ew_line_level(&line, EW_LINE_UP);
    g.dips = 0u;
    CHECK(ew_line_start(&line, &x));
    CHECK_EQ(g.dips, 0u);
    ew_line_level(&line, EW_LINE_UP);
    /* Into a pull-down: driven high first, so the line does not fall before
     * the frame. */
    g.pc = OFFSET;
    g.dips = 0u;
    x = xfer(0u);
    ew_line_rest_down(&line, true);
    CHECK(ew_line_start(&line, &x));
    CHECK_EQ(g.dips, 0u);
    CHECK(g.out);
    CHECK(g.latch);
    CHECK(g.pull_down);
}

TEST_CASE(a_level_over_a_running_transaction_cuts_it)
{
    ew_capture_t c;

    running(4u);
    ew_line_level(&line, EW_LINE_PARK);
    check_parked();
    CHECK(!ew_line_busy(&line));
    CHECK(!g.dma_busy[1]);
    CHECK(!ew_line_poll(&line, &c));
}

TEST_CASE(the_rest_into_a_pull_down_only_with_no_receive)
{
    ew_xfer_t x = xfer(0u);

    fresh();
    CHECK_EQ(ew_line_open(&line, PIN), EW_LINE_OK);
    ew_line_rest_down(&line, true);
    CHECK(ew_line_start(&line, &x));
    CHECK(g.pull_down);
    CHECK(!g.input);
    ew_line_abort(&line);
    /* A receive needs the pull-up and the buffer, whatever was asked. */
    x = xfer(1u);
    g.pc = OFFSET;
    CHECK(ew_line_start(&line, &x));
    check_listening();
    ew_line_abort(&line);
    /* Off again. */
    ew_line_rest_down(&line, false);
    x = xfer(0u);
    g.pc = OFFSET;
    CHECK(ew_line_start(&line, &x));
    check_listening();
    ew_line_rest_down(NULL, true);
}

TEST_CASE(the_driver_table)
{
    fresh();
    const ew_driver_t d = ew_line_driver(&line);

    CHECK(d.ctx == &line);
    CHECK(d.start == ew_line_start);
    CHECK(d.poll == ew_line_poll);
    CHECK(d.abort == ew_line_abort);
    CHECK(d.line == ew_line_level);
    g.now_us = 1234u;
    CHECK_EQ(d.now_us(d.ctx), 1234u);
    ew_line_init(NULL);
}

int main(void)
{
    RUN(an_open_parks_the_pad_and_claims_what_it_says);
    RUN(a_refused_open_touches_nothing);
    RUN(a_close_gives_everything_back_from_any_state);
    RUN(each_level_on_the_pad);
    RUN(a_start_listens_and_sets_the_rate);
    RUN(a_refused_start_touches_nothing);
    RUN(a_good_transaction_rests_on_the_pull_up);
    RUN(a_cut_past_its_time_parks_before_it_waits_for_the_channel);
    RUN(a_capture_with_lost_bytes_parks);
    RUN(an_abort_parks_at_once_with_no_capture);
    RUN(no_error_path_lets_a_driven_pad_rise);
    RUN(a_start_from_the_rest_does_not_dip);
    RUN(a_level_over_a_running_transaction_cuts_it);
    RUN(the_rest_into_a_pull_down_only_with_no_receive);
    RUN(the_driver_table);
    return test_summary("ew_line");
}
