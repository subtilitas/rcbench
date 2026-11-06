/*
 * The DShot pin driver (protocols/dshot/rp2350/out_dshot.c) against a fake
 * of the pico-sdk calls it makes (fake_pico_dshot/): the level it leaves on
 * the pad at every step, in both modes.
 *
 * Under test: a pin that sends no frames is driven low, from the bind and
 * after a stop, with no rise to high on the way, also over a latch another program
 * left high; a bidirectional frame never starts from a low line, because the
 * first send after the rest releases the line and sends nothing; the
 * inversion survives a stop and a wake; a released pin is a plain input that
 * stop, wake and send leave alone, which is what a programmer holding the
 * pin needs; a bind that cannot invert or pair is refused with a plain input.
 *
 * Not tested here: the chip and the program's timing.  The fake runs a frame
 * as one step: the line driven at its active level, then left as the
 * program leaves it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "dshot.pio.h"
#include "out_dshot.h"

#define PIN   6u
#define OTHER 7u

/* --------------------------------------------------------------- the fake */

/* What the pad shows. */
typedef enum {
    LVL_DRIVEN_LOW,
    LVL_DRIVEN_HIGH,
    LVL_PULLED_UP,
    LVL_PULLED_DOWN,
    LVL_FLOATING
} level_t;

io_bank0_hw_t fake_io_bank0;

const pio_program_t dshot_tx_program       = { 1 };
const pio_program_t dshot_bidir_tx_program = { 2 };
const pio_program_t dshot_rx_program       = { 3 };

static pio_hw_t g_block;

static struct {
    /* the pad of PIN */
    bool pull_up;
    bool pull_down;
    bool sio_out;
    bool sio_oe;
    /* the PIO block's latch and direction for PIN */
    bool latch;
    bool oe;
    /* the block */
    unsigned claimed;              /* bit per state machine */
    const pio_program_t *prog[NUM_PIO_STATE_MACHINES];
    bool enabled[NUM_PIO_STATE_MACHINES];
    unsigned programs;             /* loaded, less removed */
    /* knobs */
    bool claim_refused;
    bool no_room;
    bool outover_lost;
    /* what the line did */
    unsigned pad_writes;
    bool last_high;                /* the pad high after the last step */
    unsigned rises;                /* steps that took the pad high */
    unsigned frames;
    unsigned frames_from_low;      /* bidirectional frames begun on a low line */
} g;

/* The pad's level, as the function, the override and the pulls give it. */
static level_t level(void)
{
    const uint32_t ctrl = fake_io_bank0.io[PIN].ctrl;
    const uint32_t fsel = ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS;
    const bool inv = ((ctrl & IO_BANK0_GPIO0_CTRL_OUTOVER_BITS)
                      >> IO_BANK0_GPIO0_CTRL_OUTOVER_LSB)
                     == (uint32_t)GPIO_OVERRIDE_INVERT;
    bool oe = false;
    bool out = false;

    if (fsel == (uint32_t)GPIO_FUNC_SIO) {
        oe = g.sio_oe;
        out = g.sio_out;
    } else if (fsel == (uint32_t)GPIO_FUNC_PIO0) {
        oe = g.oe;
        out = g.latch;
    }
    if (oe) {
        return (out != inv) ? LVL_DRIVEN_HIGH : LVL_DRIVEN_LOW;
    }
    if (g.pull_up) {
        return LVL_PULLED_UP;
    }
    return g.pull_down ? LVL_PULLED_DOWN : LVL_FLOATING;
}

static bool is_high(level_t l)
{
    return l == LVL_DRIVEN_HIGH || l == LVL_PULLED_UP;
}

/* Every write to the pad of PIN: counted, and a rise to high by it counted. */
static void observe(uint gpio)
{
    if (gpio != PIN) {
        return;
    }
    g.pad_writes++;
    const bool high = is_high(level());
    if (high && !g.last_high) {
        g.rises++;
    }
    g.last_high = high;
}

/* The pad as a case left it by hand is where rises count from. */
static void settle(void)
{
    g.last_high = is_high(level());
    g.rises = 0u;
}

static uint32_t outover(void)
{
    return (fake_io_bank0.io[PIN].ctrl & IO_BANK0_GPIO0_CTRL_OUTOVER_BITS)
           >> IO_BANK0_GPIO0_CTRL_OUTOVER_LSB;
}

/* As the SDK's: the whole control register, the override with it. */
void gpio_set_function(uint gpio, gpio_function_t fn)
{
    fake_io_bank0.io[gpio].ctrl = (uint32_t)fn;
    observe(gpio);
}
void gpio_set_outover(uint gpio, uint value)
{
    if (!g.outover_lost) {
        fake_io_bank0.io[gpio].ctrl =
            (fake_io_bank0.io[gpio].ctrl & ~IO_BANK0_GPIO0_CTRL_OUTOVER_BITS)
            | ((uint32_t)value << IO_BANK0_GPIO0_CTRL_OUTOVER_LSB);
    }
    observe(gpio);
}
void gpio_disable_pulls(uint gpio)
{
    if (gpio == PIN) {
        g.pull_up = false;
        g.pull_down = false;
    }
    observe(gpio);
}
void gpio_pull_up(uint gpio)
{
    if (gpio == PIN) {
        g.pull_up = true;
        g.pull_down = false;
    }
    observe(gpio);
}
void gpio_set_dir(uint gpio, bool out)
{
    if (gpio == PIN) {
        g.sio_oe = out;
    }
    observe(gpio);
}
void gpio_put(uint gpio, bool value)
{
    if (gpio == PIN) {
        g.sio_out = value;
    }
    observe(gpio);
}

bool pio_sm_is_claimed(PIO pio, uint sm)
{
    (void)pio;
    return (g.claimed & (1u << sm)) != 0u;
}
bool pio_can_add_program(PIO pio, const pio_program_t *program)
{
    (void)pio;
    (void)program;
    return !g.no_room;
}
void pio_sm_claim(PIO pio, uint sm)
{
    (void)pio;
    g.claimed |= 1u << sm;
}
void pio_sm_unclaim(PIO pio, uint sm)
{
    (void)pio;
    g.claimed &= ~(1u << sm);
    g.prog[sm] = NULL;
}
int pio_add_program(PIO pio, const pio_program_t *program)
{
    (void)pio;
    (void)program;
    g.programs++;
    return 16;
}
void pio_remove_program(PIO pio, const pio_program_t *program, uint offset)
{
    (void)pio;
    (void)program;
    (void)offset;
    g.programs--;
}
bool pio_claim_free_sm_and_add_program_for_gpio_range(
    const pio_program_t *program, PIO *pio, uint *sm, uint *offset,
    uint gpio_base, uint gpio_count, bool set_gpio_base)
{
    (void)gpio_base;
    (void)gpio_count;
    (void)set_gpio_base;
    if (g.claim_refused) {
        return false;
    }
    for (uint i = 0; i < NUM_PIO_STATE_MACHINES; ++i) {
        if ((g.claimed & (1u << i)) == 0u) {
            g.claimed |= 1u << i;
            g.prog[i] = program;
            g.programs++;
            *pio = &g_block;
            *sm = i;
            *offset = 0u;
            return true;
        }
    }
    return false;
}
void pio_gpio_init(PIO pio, uint pin)
{
    (void)pio;
    fake_io_bank0.io[pin].ctrl = (uint32_t)GPIO_FUNC_PIO0;
    observe(pin);
}
/* A transmitter's every instruction side-sets: the first one sets the latch
 * to 0, the line's idle through the inverter or plain DShot's low. */
static void side_set_0(uint sm)
{
    if (g.prog[sm] == &dshot_tx_program
        || g.prog[sm] == &dshot_bidir_tx_program) {
        g.latch = false;
        observe(PIN);
    }
}
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled)
{
    (void)pio;
    g.enabled[sm] = enabled;
    if (enabled) {
        side_set_0(sm);
    }
}
void pio_sm_clear_fifos(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
}
int pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count,
                                   bool is_out)
{
    (void)pio;
    (void)sm;
    (void)count;
    if (pin == PIN) {
        g.oe = is_out;
    }
    observe(pin);
    return 0;
}
void pio_sm_set_pins_with_mask64(PIO pio, uint sm, uint64_t values,
                                 uint64_t mask)
{
    (void)pio;
    (void)sm;
    if ((mask & (1ull << PIN)) != 0u) {
        g.latch = (values & (1ull << PIN)) != 0u;
        observe(PIN);
    }
}
void pio_sm_restart(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
}
void pio_sm_clkdiv_restart(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
}
/* Only jumps are executed here, and on a transmitter they side-set too. */
void pio_sm_exec(PIO pio, uint sm, uint instr)
{
    (void)pio;
    (void)instr;
    side_set_0(sm);
}
uint pio_encode_jmp(uint addr) { return addr; }
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    return false;
}
/* A frame queued on a running transmitter goes out in this one step. */
void pio_sm_put(PIO pio, uint sm, uint32_t data)
{
    (void)pio;
    (void)data;
    if (!g.enabled[sm]) {
        return;
    }
    if (g.prog[sm] == &dshot_bidir_tx_program) {
        if (!is_high(level())) {
            g.frames_from_low++;
        }
        g.oe = true;             /* set pindirs, 1: the line taken */
        g.latch = false;
        observe(PIN);
        g.frames++;
        g.oe = false;            /* set pindirs, 0: let go for the reply */
        observe(PIN);
    } else if (g.prog[sm] == &dshot_tx_program) {
        g.frames++;
        g.latch = false;         /* the out stalls with the side-set low */
        observe(PIN);
    }
}
void pio_interrupt_clear(PIO pio, uint irq)
{
    (void)pio;
    (void)irq;
}
uint pio_sm_get_rx_fifo_level(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    return 0u;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    return true;
}
uint32_t pio_sm_get(PIO pio, uint sm)
{
    (void)pio;
    (void)sm;
    return 0u;
}

/* The init functions, step for step as dshot.pio's: the plain one hands the
 * pad to the block and then drives it; the bidirectional one makes it an
 * input first.  pio_sm_init() ends in a jump to the program. */
void dshot_tx_program_init(PIO pio, uint sm, uint offset, uint pin,
                           uint32_t bits_per_second)
{
    (void)bits_per_second;
    pio_gpio_init(pio, pin);
    (void)pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset));
}
void dshot_bidir_tx_program_init(PIO pio, uint sm, uint offset, uint pin,
                                 uint32_t bits_per_second)
{
    (void)bits_per_second;
    (void)pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);
    pio_gpio_init(pio, pin);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset));
}
void dshot_rx_program_init(PIO pio, uint sm, uint offset, uint pin,
                           uint32_t bits_per_second)
{
    (void)pio;
    (void)sm;
    (void)offset;
    (void)pin;
    (void)bits_per_second;
}

/* Nothing bound, and the pad as the chip leaves it at reset: no function, a
 * pull-down.  @p latch is what another program left in the block. */
static void fresh(bool latch)
{
    out_dshot_release(PIN);
    out_dshot_release(OTHER);
    memset(&g, 0, sizeof(g));
    memset(&fake_io_bank0, 0, sizeof(fake_io_bank0));
    fake_io_bank0.io[PIN].ctrl = (uint32_t)GPIO_FUNC_NULL;
    g.pull_down = true;
    g.latch = latch;
    g.oe = latch;
    settle();
}

/* ------------------------------------------------------------- the cases */

/* Bound bidirectional, the pin is driven low, inverted, and on the block:
 * no step of the bind leaves it high, over a latch and a direction another
 * program left driving high. */
TEST_CASE(a_bidirectional_pin_is_driven_low_from_the_bind_with_no_high_on_the_way)
{
    for (int stale = 0; stale < 2; ++stale) {
        fresh(stale != 0);
        CHECK(out_dshot_bind(PIN, 600u, true));
        CHECK_EQ(level(), LVL_DRIVEN_LOW);
        CHECK_EQ(g.rises, 0u);
        CHECK_EQ(outover(), (uint32_t)GPIO_OVERRIDE_INVERT);
        CHECK(g.pull_up);
        CHECK_EQ(g.frames, 0u);
        /* The loop's stop every pass changes nothing on a pin at rest. */
        const unsigned writes = g.pad_writes;
        out_dshot_stop(PIN);
        CHECK_EQ(g.pad_writes, writes);
    }
}

/* Plain DShot the same: low from the bind, also when the block's latch for
 * the pin was left at 1 and its direction out. */
TEST_CASE(a_plain_pin_is_driven_low_from_the_bind_over_a_latch_left_high)
{
    for (int stale = 0; stale < 2; ++stale) {
        fresh(stale != 0);
        CHECK(out_dshot_bind(PIN, 300u, false));
        CHECK_EQ(level(), LVL_DRIVEN_LOW);
        CHECK_EQ(g.rises, 0u);
    }
}

/* The first send after the rest releases the line onto its pull-up and sends
 * nothing; the next one sends, from the high line. */
TEST_CASE(the_first_send_after_the_rest_releases_the_line_and_sends_nothing)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, true));
    out_dshot_send(PIN, 0u, false);
    CHECK_EQ(g.frames, 0u);
    CHECK_EQ(level(), LVL_PULLED_UP);
    out_dshot_send(PIN, 0u, false);
    CHECK_EQ(g.frames, 1u);
    CHECK_EQ(g.frames_from_low, 0u);
    /* Between frames of the run the line is released to its pull-up. */
    CHECK_EQ(level(), LVL_PULLED_UP);
    out_dshot_send(PIN, 48u, false);
    CHECK_EQ(g.frames, 2u);
    CHECK_EQ(g.frames_from_low, 0u);
}

/* out_dshot_wake() releases a resting bidirectional pin once and answers
 * true for it alone; the call before the first frame keeps that frame. */
TEST_CASE(wake_releases_a_resting_bidirectional_pin_once)
{
    fresh(false);
    CHECK(!out_dshot_wake(PIN));                 /* not bound */
    CHECK(out_dshot_bind(PIN, 600u, true));
    CHECK(out_dshot_wake(PIN));
    CHECK_EQ(level(), LVL_PULLED_UP);
    CHECK(!out_dshot_wake(PIN));                 /* released already */
    out_dshot_send(PIN, 0u, false);              /* sent at once */
    CHECK_EQ(g.frames, 1u);
    CHECK(!out_dshot_wake(PIN));                 /* running */
    out_dshot_stop(PIN);
    CHECK(out_dshot_wake(PIN));                  /* at rest again */
    /* A plain pin needs no wake: it starts from its low idle. */
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, false));
    CHECK(!out_dshot_wake(PIN));
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
}

/* A stop drives the pin low: after a frame, with the line released to its
 * pull-up, and in a frame, with the line driven high.  The low holds until
 * the next send, and no step of the stop leaves the pad high. */
TEST_CASE(a_stopped_bidirectional_pin_is_driven_low_and_stays_low)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, true));
    CHECK(out_dshot_wake(PIN));
    out_dshot_send(PIN, 0u, false);
    CHECK_EQ(level(), LVL_PULLED_UP);
    settle();
    out_dshot_stop(PIN);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
    CHECK_EQ(g.rises, 0u);
    dshot_telem_t t;
    CHECK(!out_dshot_poll(PIN, false, &t));
    /* Asked every pass: nothing moves. */
    const unsigned writes = g.pad_writes;
    out_dshot_stop(PIN);
    out_dshot_stop(PIN);
    CHECK_EQ(g.pad_writes, writes);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);

    /* Cut in a frame: the program has the line taken and high. */
    out_dshot_send(PIN, 0u, false);              /* the wake */
    out_dshot_send(PIN, 0u, false);
    g.oe = true;
    g.latch = false;
    CHECK_EQ(level(), LVL_DRIVEN_HIGH);
    settle();
    out_dshot_stop(PIN);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
    CHECK_EQ(g.rises, 0u);
    CHECK(!g.enabled[0]);
    CHECK(!g.enabled[1]);
}

/* The inversion is set once, at the bind, and nothing after it wipes it: a
 * stop, a wake and the frames keep the pad on the block. */
TEST_CASE(the_inversion_survives_a_stop_and_a_wake)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, true));
    for (int run = 0; run < 3; ++run) {
        out_dshot_send(PIN, 0u, false);
        out_dshot_send(PIN, 0u, false);
        out_dshot_stop(PIN);
        CHECK_EQ(outover(), (uint32_t)GPIO_OVERRIDE_INVERT);
        CHECK_EQ(fake_io_bank0.io[PIN].ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS,
                 (uint32_t)GPIO_FUNC_PIO0);
    }
    CHECK_EQ(g.frames, 3u);
    CHECK_EQ(g.frames_from_low, 0u);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
}

/* A stopped plain pin is the processor's at 0; the next send hands it back
 * to the block at a latch of 0, so the pad shows no high before the frame
 * even when the block's latch was left at 1 in between. */
TEST_CASE(a_plain_pin_goes_back_to_the_block_at_low)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, false));
    out_dshot_send(PIN, 48u, false);
    CHECK_EQ(g.frames, 1u);
    out_dshot_stop(PIN);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
    CHECK_EQ(fake_io_bank0.io[PIN].ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS,
             (uint32_t)GPIO_FUNC_SIO);
    g.latch = true;
    g.oe = true;
    settle();
    out_dshot_send(PIN, 48u, false);
    CHECK_EQ(g.rises, 0u);
    CHECK_EQ(g.frames, 2u);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
}

/* A released pin is given back as the chip had it, a plain input with no
 * pull, and the driver does not touch it again: a programmer that holds the
 * pin and idles it high on its own pull-up keeps it high through the loop's
 * stop, a wake and a send. */
TEST_CASE(a_released_pin_is_a_plain_input_the_driver_leaves_alone)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, true));
    out_dshot_release(PIN);
    CHECK_EQ(level(), LVL_FLOATING);
    CHECK_EQ(outover(), (uint32_t)GPIO_OVERRIDE_NORMAL);
    CHECK_EQ(g.claimed, 0u);
    CHECK_EQ(g.programs, 0u);
    g.pull_up = true;                            /* the programmer's idle */
    const unsigned writes = g.pad_writes;
    out_dshot_stop(PIN);
    CHECK(!out_dshot_wake(PIN));
    out_dshot_send(PIN, 0u, false);
    CHECK_EQ(g.pad_writes, writes);
    CHECK_EQ(g.frames, 0u);
    CHECK_EQ(level(), LVL_PULLED_UP);
}

/* A bind that would go out non-inverted, or that cannot pair its receiver,
 * is refused and leaves a plain input: never a driven line, never the low
 * rest of a pin the driver does not hold. */
TEST_CASE(a_bind_that_cannot_invert_or_pair_is_refused_with_a_plain_input)
{
    fresh(false);
    g.outover_lost = true;
    CHECK(!out_dshot_bind(PIN, 600u, true));
    CHECK_EQ(level(), LVL_FLOATING);
    CHECK_EQ(g.claimed, 0u);
    CHECK(!out_dshot_wake(PIN));

    fresh(false);
    g.claimed = 1u << 1;                         /* the receiver's machine */
    CHECK(!out_dshot_bind(PIN, 600u, true));
    CHECK_EQ(level(), LVL_FLOATING);
    CHECK_EQ(g.claimed, 1u << 1);

    fresh(false);
    g.no_room = true;
    CHECK(!out_dshot_bind(PIN, 600u, true));
    CHECK_EQ(level(), LVL_FLOATING);

    fresh(false);
    g.claim_refused = true;
    CHECK(!out_dshot_bind(PIN, 600u, true));
    CHECK(!out_dshot_bind(PIN, 0u, true));
}

/* The same pin bound again the same way keeps its state: a resting pin stays
 * at rest, a running one keeps running. */
TEST_CASE(binding_again_the_same_way_moves_nothing)
{
    fresh(false);
    CHECK(out_dshot_bind(PIN, 600u, true));
    const unsigned writes = g.pad_writes;
    CHECK(out_dshot_bind(PIN, 600u, true));
    CHECK(!out_dshot_bind(PIN, 300u, true));
    CHECK(!out_dshot_bind(PIN, 600u, false));
    CHECK_EQ(g.pad_writes, writes);
    CHECK_EQ(level(), LVL_DRIVEN_LOW);
}

int main(void)
{
    RUN(a_bidirectional_pin_is_driven_low_from_the_bind_with_no_high_on_the_way);
    RUN(a_plain_pin_is_driven_low_from_the_bind_over_a_latch_left_high);
    RUN(the_first_send_after_the_rest_releases_the_line_and_sends_nothing);
    RUN(wake_releases_a_resting_bidirectional_pin_once);
    RUN(a_stopped_bidirectional_pin_is_driven_low_and_stays_low);
    RUN(the_inversion_survives_a_stop_and_a_wake);
    RUN(a_plain_pin_goes_back_to_the_block_at_low);
    RUN(a_released_pin_is_a_plain_input_the_driver_leaves_alone);
    RUN(a_bind_that_cannot_invert_or_pair_is_refused_with_a_plain_input);
    RUN(binding_again_the_same_way_moves_nothing);
    return test_summary("out_dshot");
}
