/*
 * DShot out and the reply back.  See out_dshot.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_dshot.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "dshot.pio.h"
#include "outputs.h"

/*
 * Eight words is 256 samples at five to a reply bit, which is 51 bits against
 * a reply of 21.  The state machine stalls when the queue is full, so the
 * extra is idle line after the burst rather than anything that has to arrive
 * in time.
 */
#define CAP_WORDS  8u

typedef struct {
    bool     used;
    bool     bidir;
    bool     running;        /* the transmitter is enabled                  */
    bool     armed;          /* a reply is expected for the frame just sent */
    uint8_t  pin;
    uint32_t bits_per_second;

    PIO      pio;
    uint     tx_sm;
    uint     tx_offset;
    uint     rx_sm;
    uint     rx_offset;
} dshot_out_t;

static dshot_out_t s_out[OUT_MAX_SLOTS];

static dshot_out_t *find(uint8_t pin)
{
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_out[i].used && s_out[i].pin == pin) {
            return &s_out[i];
        }
    }
    return NULL;
}

static const pio_program_t *tx_program(bool bidir)
{
    return bidir ? &dshot_bidir_tx_program : &dshot_tx_program;
}

/* ---------------------------------------------------------------- binding */

/*
 * The receiver has to sit on the state machine one above its transmitter, in
 * the same block: the transmitter raises a flag relative to its own number
 * and the receiver waits on the flag one below its own, so the pairing is
 * what keeps two ESCs on one block from starting each other's receivers.
 */
static bool claim_receiver(dshot_out_t *s)
{
    const uint want = (s->tx_sm + 1u) % (uint)NUM_PIO_STATE_MACHINES;
    if (pio_sm_is_claimed(s->pio, want)) {
        return false;
    }
    if (!pio_can_add_program(s->pio, &dshot_rx_program)) {
        return false;
    }
    pio_sm_claim(s->pio, want);
    s->rx_sm     = want;
    s->rx_offset = (uint)pio_add_program(s->pio, &dshot_rx_program);
    dshot_rx_program_init(s->pio, s->rx_sm, s->rx_offset, s->pin,
                          s->bits_per_second);
    return true;
}

/*
 * Give the pad back exactly as it was at boot: no inversion, no pull, and a
 * plain input rather than a pin still muxed to a PIO block.  A pin left
 * muxed is one the next configuration cannot have and one a scope reads as
 * driven by something that no longer exists.
 */
static void release_pad(uint8_t pin)
{
    gpio_set_outover(pin, GPIO_OVERRIDE_NORMAL);
    gpio_disable_pulls(pin);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
}

static void teardown(dshot_out_t *s)
{
    if (s->pio != NULL) {
        pio_sm_set_enabled(s->pio, s->tx_sm, false);
        pio_remove_program(s->pio, tx_program(s->bidir), s->tx_offset);
        pio_sm_unclaim(s->pio, s->tx_sm);
        if (s->bidir) {
            pio_sm_set_enabled(s->pio, s->rx_sm, false);
            pio_remove_program(s->pio, &dshot_rx_program, s->rx_offset);
            pio_sm_unclaim(s->pio, s->rx_sm);
        }
    }
    const uint8_t pin = s->pin;
    memset(s, 0, sizeof(*s));
    release_pad(pin);
}

bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional)
{
    if (rate_kbit == 0u) {
        return false;
    }
    dshot_out_t *s = find(pin);
    if (s != NULL) {
        /* Unchanged: leave the output running rather than dropping a frame
         * for a reconfiguration that did not move this slot. */
        return s->bidir == bidirectional
               && s->bits_per_second == (uint32_t)rate_kbit * 1000u;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS && s == NULL; ++i) {
        if (!s_out[i].used) {
            s = &s_out[i];
        }
    }
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->pin             = pin;
    s->bidir           = bidirectional;
    s->bits_per_second = (uint32_t)rate_kbit * 1000u;

    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            tx_program(bidirectional), &s->pio, &s->tx_sm, &s->tx_offset,
            pin, 1, true)) {
        s->pio = NULL;
        return false;
    }

    if (bidirectional) {
        /*
         * Inverted at the pad rather than in the program.  The inverter is on
         * the output driver only, so the same pin still reads the line's true
         * level once the transmitter has let go of it, and the pull-up is
         * what holds the idle in between.
         */
        gpio_set_outover(pin, GPIO_OVERRIDE_INVERT);
        gpio_pull_up(pin);
        dshot_bidir_tx_program_init(s->pio, s->tx_sm, s->tx_offset, pin,
                                    s->bits_per_second);
        if (!claim_receiver(s)) {
            /* The transmitter's program_init has already muxed the pad to
             * the block, so the pad is put back with everything else this
             * failed bind took. */
            pio_remove_program(s->pio, tx_program(true), s->tx_offset);
            pio_sm_unclaim(s->pio, s->tx_sm);
            memset(s, 0, sizeof(*s));
            release_pad(pin);
            return false;
        }
    } else {
        dshot_tx_program_init(s->pio, s->tx_sm, s->tx_offset, pin,
                              s->bits_per_second);
    }

    s->used = true;
    /* Not enabled here: nothing is on the wire until a frame is sent. */
    return true;
}

void out_dshot_release(uint8_t pin)
{
    dshot_out_t *s = find(pin);
    if (s != NULL) {
        teardown(s);
    }
}

/* ---------------------------------------------------------------- sending */

void out_dshot_stop(uint8_t pin)
{
    dshot_out_t *s = find(pin);
    if (s == NULL || (!s->running && !s->armed)) {
        return;      /* already stopped; the loop asks every pass */
    }
    pio_sm_set_enabled(s->pio, s->tx_sm, false);
    pio_sm_clear_fifos(s->pio, s->tx_sm);
    s->running = false;
    if (s->bidir) {
        pio_sm_set_enabled(s->pio, s->rx_sm, false);
        pio_sm_clear_fifos(s->pio, s->rx_sm);
        /* Released, so the line sits at the idle its pull-up gives it. */
        pio_sm_set_consecutive_pindirs(s->pio, s->tx_sm, s->pin, 1, false);
    }
    s->armed = false;
}

/* Put a state machine back at the top of its program with nothing queued. */
static void restart(PIO pio, uint sm, uint offset)
{
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_clkdiv_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset));
    pio_sm_set_enabled(pio, sm, true);
}

void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    dshot_out_t *s = find(pin);
    if (s == NULL) {
        return;
    }
    const uint16_t frame = dshot_frame(value, telemetry, s->bidir);

    if (!s->bidir) {
        if (!s->running) {
            restart(s->pio, s->tx_sm, s->tx_offset);
            s->running = true;
        }
        /*
         * Dropped rather than queued.  A full queue means the last frame has
         * not gone yet, and what would be sent late is a throttle value that
         * has already been superseded.
         */
        if (!pio_sm_is_tx_fifo_full(s->pio, s->tx_sm)) {
            pio_sm_put(s->pio, s->tx_sm, (uint32_t)frame << 16);
        }
        return;
    }

    /*
     * The receiver is put back to the top and the rendezvous flag cleared
     * before the frame is queued.  A flag left set from the frame before
     * would start the receiver on the transmitter's own edges instead of the
     * ESC's reply.
     */
    restart(s->pio, s->rx_sm, s->rx_offset);
    pio_interrupt_clear(s->pio, s->tx_sm);
    if (!s->running) {
        restart(s->pio, s->tx_sm, s->tx_offset);
        s->running = true;
    }
    if (!pio_sm_is_tx_fifo_full(s->pio, s->tx_sm)) {
        pio_sm_put(s->pio, s->tx_sm, (uint32_t)frame << 16);
        s->armed = true;
    }
}

/* ------------------------------------------------------------- the reply */

bool out_dshot_poll(uint8_t pin, dshot_telem_t *out)
{
    dshot_out_t *s = find(pin);
    if (s == NULL || out == NULL || !s->bidir || !s->armed) {
        return false;
    }
    uint32_t cap[CAP_WORDS];
    unsigned n = 0u;
    while (n < CAP_WORDS && !pio_sm_is_rx_fifo_empty(s->pio, s->rx_sm)) {
        cap[n++] = pio_sm_get(s->pio, s->rx_sm);
    }
    s->armed = false;
    if (n == 0u) {
        return false;
    }
    uint32_t line = 0u;
    if (!dshot_rx_bits(cap, n, DSHOT_RX_OVERSAMPLE, &line)) {
        return false;
    }
    /* Extended telemetry is never enabled, so every reply is a period. */
    return dshot_telem_decode(line, false, out);
}
