/*
 * The coprocessor application.
 *
 * Answers polls and never transmits unsolicited: every transmission here
 * follows a decoded request.
 *
 * The power path is not written.  This file runs on core 0 and holds the
 * wire, the failsafe and the output bank; the output protocols are in
 * out_pwm.c, out_ppm.c and out_dshot.c behind outputs_hw.c.  Core 1 reads
 * the current monitors on their I2C bus (sense_core1.c) and nothing else;
 * this file gives it its orders and publishes what it reads.
 *
 * Nothing here models a reading.  A number this end publishes came off a
 * wire or a sensor, and a quantity nothing measures is left at zero with its
 * valid bit clear.  The panel models when no coprocessor answers at all,
 * which is the only case where a modelled number cannot be mistaken for a
 * measured one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "iomcu_pins.h"
#include "bench_state.h"
#include "can_selftest.h"
#include "heartbeat.h"
#include "link_control.h"
#include "link_dev.h"
#include "link_pages.h"
#include "dshot.h"
#include "out_bind.h"
#include "rcbench_version.h"
#include "out_store.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "outputs_pages.h"
#include "pd_uart.h"
#include "pdmini.h"
#include "sense_core1.h"
#include "sense_page.h"
#include "servo_page.h"
#include "supply_page.h"
#include "tone_cap.h"
#include "tone_core1.h"
#include "tone_page.h"
#include "xl2515.h"

/* ------------------------------------------------------------- the pages */

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    uint16_t status[LINK_ST_COUNT];
    uint16_t bench[LINK_BN_COUNT];
    uint16_t channels[LINK_CH_COUNT];   /* what each output is asked for   */
    uint16_t chan_cfg[LINK_CC_COUNT];   /* what each channel is            */
    uint16_t slots[LINK_OS_COUNT];      /* which driver drives what        */
} iomcu_state_t;

static iomcu_state_t s_state;

/* What the output drivers make true, whatever is connected; see main(). */
#define IOMCU_CAPABILITIES \
    ((uint16_t)(LINK_CAP_SERVO_PWM | LINK_CAP_ESC_DRIVE | LINK_CAP_ESC_TELEM))
/* The SERVO page: the surfaces' frame rate and their sweep.  Not kept. */
static servo_page_t s_servo;
/* The SUPPLY page and the PD mini it drives on a PIO UART.  Not kept. */
static supply_page_t s_supply;
static pdmini_t      s_pd;
static bool          s_pd_open;      /* the UART claimed for its pins */
static uint8_t       s_pd_rate;      /* the rate it runs at, 0..6     */
/* Wiring taken and not yet in flash: no ON until it is, so a restart in
 * the run finds the wiring that drives the module and can switch it off. */
static bool          s_supply_unsaved;
/* New wiring taken, the UART to attach once it is saved. */
static bool          s_supply_attach;
/*
 * The SENSE and SERVO_SENSE pages: the current monitors' set-up, kept, and
 * the pins it holds.  Core 1 reads the parts (sense_core1.h); this core
 * gives it its orders and publishes what it hands back.
 */
static sense_page_t  s_sense;
/*
 * The TONE page: the phase tap's set-up, kept, and the beeps core 1 hears
 * on it.  Core 0 owns the PIO state machine and the DMA ring
 * (tone_cap.c); core 1 reads the ring and detects (tone_core1.c).
 */
static tone_page_t   s_tone;
/* Core 1's last snapshot.  The pins it said it still holds are the page's
 * (sense_page_held()): a bus moved or closed keeps its old pins from the
 * outputs until core 1 has let them go, about 1 ms. */
static sense_snap_t  s_sense_snap;
/* Moves on each edge of the bank into driving: core 1 starts the run's
 * peaks and the INA228's totals on it. */
static uint16_t      s_run_gen;
static bool          s_pass_driving;
/* The capture's PWM edge, stamped by outputs_hw for capture order
 * s_edge_gen. */
static bool          s_edge_set;
static uint16_t      s_edge_gen;
static uint64_t      s_edge_us;
/* What the board and this file hold, before the supply and the sensor bus
 * take their pins. */
static uint64_t      s_base_reserved;
static link_dev_t    s_dev;

/*
 * Every output, behind one set of rules.
 *
 * The pages above are the wire format; the rules (arming, clamping, slew and
 * the silence timeout) live in shared/outputs and nowhere else, so this end
 * and the panel cannot answer them differently.
 *
 * The output pages address bank channels 0..LINK_OUT_CHANNELS-1 directly.
 * The throttle lives above that range, so the control page commands it
 * without colliding with a CHANNELS-page write, and a motor command keeps
 * the control page's priority on the wire.
 */
static outputs_t s_outputs;

#define CH_THROTTLE  LINK_OUT_CHANNELS   /* bank channel 8, off the page */

/* The panel's safety line, as judged in firmware.  The control page consults
 * it before it arms; see heartbeat_init() below. */
static heartbeat_mon_t s_beat;

/* Requests this end has answered, published on the STATUS page. */
static uint32_t s_frames;

/*
 * The pass's clock, used by everything the pass reaches -- including the page
 * callbacks, which run inside can_service() and have no `now` of their own.
 *
 * Read at the top of the loop and once more before its tail, and nowhere
 * else.  The second read is there because two things in a pass can stop this
 * core for longer than a pass lasts: a flash window, and a printf to a USB
 * host.  See the loop for why that matters to what is measured after them.
 *
 * Every timeout in this file is a wrap-safe unsigned subtraction of two
 * timestamps.  A second reading of the clock inside a pass can be a
 * millisecond ahead of the pass's own, and a stamp ahead of the `now` it is
 * later compared against subtracts to 4,294,967,295 ms -- past every timeout
 * there is.  A CHANNELS write stamped that way reads as stale in the same
 * pass and is put back to rest; a save stamped that way skips the settle and
 * the quiet-bus wait and takes its flash window inside the request burst it
 * was meant to wait out.
 */
static uint32_t s_now_ms;

static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->identity[off + i];
    }
}

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->control[off + i];
    }
}

static void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->status[off + i];
    }
}

/*
 * The numbers.  Read-only by construction: there is no write handler, so a
 * host that tries to set a measurement is refused with READ_ONLY rather than
 * quietly believed.
 */
static void bench_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->bench[off + i];
    }
}

/*
 * The output pages: three pages, one bank.
 *
 * The rules (clamp, arm, slew, refuse an impossible range, refuse an unknown
 * driver) live in shared/outputs, so this end and the host end cannot hold
 * different opinions.  The coprocessor supplies the one thing only it knows,
 * whether it is safe to drive at all, by arming the bank rather than by
 * gating each write.
 *
 * A read returns the stored register array; a write validates and stores,
 * then re-derives the bank from the whole page so a partial write composes.
 */
/*
 * Keep what describes the outputs.
 *
 * The configuration is this board's, because the wires are this board's: a
 * panel that remembered a binding would reapply it to whatever is on the
 * bench now.  Nothing reaches flash here -- the request is taken later, by
 * the loop, once the bank has stopped driving.
 */
static void save_outputs(const iomcu_state_t *s)
{
    out_store_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.slots, s->slots, sizeof(cfg.slots));
    memcpy(cfg.chan_cfg, s->chan_cfg, sizeof(cfg.chan_cfg));
    /* And the supply's wiring, so a restart can still switch a module off. */
    memcpy(cfg.supply, &s_supply.regs[LINK_SP_ENABLE], sizeof(cfg.supply));
    /* And the sensor bus's set-up, so its pins stay held across one. */
    memcpy(cfg.sense, &s_sense.sense[LINK_SN_ENABLE], sizeof(cfg.sense));
    /* And the phase tap's. */
    memcpy(cfg.tone, s_tone.cfg, sizeof(cfg.tone));
    out_store_save(&cfg, s_now_ms);
}

static void channels_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->channels[off + i];
    }
}

static uint8_t channels_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    const uint8_t nack = outputs_channels_write(s->channels, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    /*
     * Only what this frame carried.  Applying the whole page would stamp
     * every channel's clock on a write that named one of them, and the
     * timeout that returns an uncommanded output to rest is per channel
     * exactly so that one screen's traffic cannot hold another's output up.
     */
    outputs_channels_apply_n(&s_outputs, s->channels, off, n, s_now_ms);
    return 0u;
}

/* The slots the silicon has bound, one bit each. */
static uint8_t bound_slots(void)
{
    uint8_t bound = 0u;
    for (uint8_t i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (outputs_hw_bound(i)) {
            bound |= (uint8_t)(1u << i);
        }
    }
    return bound;
}

/*
 * The silicon made to agree with the bank, every slot at the rate it runs
 * at: its own, or the SERVO page's for a PWM surface.
 */
static void hw_apply(void)
{
    uint16_t rate[OUT_MAX_SLOTS];
    outputs_slot_rates(&s_outputs, servo_page_hz(&s_servo), rate);
    outputs_hw_apply(&s_outputs, rate);
    /* And the SERVO_SENSE page told which slots render frames, so a
     * capture never arms on one the silicon left unbound. */
    sense_page_bound(&s_sense, bound_slots());
}

static void chan_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->chan_cfg[off + i];
    }
}

static uint8_t chan_cfg_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    uint16_t next[LINK_CC_COUNT];
    memcpy(next, s->chan_cfg, sizeof(next));
    const uint8_t nack = outputs_chan_cfg_write(next, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    /* Not a role change that would split a PWM slice under the SERVO
     * page's rate: refused whole, rather than taken with an output left
     * unbound. */
    if (outputs_chan_cfg_rate_check(&s_outputs, next,
                                    servo_page_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    memcpy(s->chan_cfg, next, sizeof(next));
    outputs_chan_cfg_apply(&s_outputs, s->chan_cfg);
    /* A channel that became a surface, or stopped being one, moves to or
     * from the SERVO page's rate. */
    hw_apply();
    save_outputs(s);
    return 0u;
}

static void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const iomcu_state_t *s = (const iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->slots[off + i];
    }
}

/*
 * What no output may have: the board's own pins, and the ones the supply,
 * the sensor bus and the phase tap hold now -- the pages', and the ones
 * core 1 has not let go of yet.
 */
static void reserve_held(void)
{
    outputs_reserve_pins(&s_outputs, s_base_reserved
                                     | supply_page_pins(&s_supply)
                                     | sense_page_held(&s_sense)
                                     | tone_page_pins(&s_tone));
}

/* ------------------------------------------------------------- core 1 */

/* Core 1's order, from the page as it stands, the run and the edge. */
static void sense_build(sense_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    sense_page_cmd(&s_sense, cmd);
    cmd->run_gen  = s_run_gen;
    cmd->edge_set = s_edge_set && s_edge_gen == s_sense.cap_gen;
    cmd->edge_us  = s_edge_us;
}

/* Sent whenever something in it changes; core 1 takes it at its next
 * tick.  A copy under a lock, about a microsecond. */
static void sense_order(void)
{
    sense_cmd_t cmd;
    sense_build(&cmd);
    sense_core1_order(&cmd);
}

/*
 * Core 1's newest snapshot into the pages, when there is one.  Called
 * where its contents are needed -- a read of either page, the 50 Hz
 * sample, a capture ending -- rather than every pass, so a pass with
 * nothing to publish costs one load.
 */
static void sense_sync(void)
{
    if (!sense_core1_snapshot(&s_sense_snap)) {
        return;
    }
    const uint64_t was = s_sense.held;
    sense_page_publish(&s_sense, &s_sense_snap, s_run_gen);
    if (s_sense.held != was) {
        reserve_held();
    }
}

/*
 * The identity page's capability word: what the output drivers make true,
 * and the current monitors the SENSE set-up enables (sense_page_caps()).
 * Fitted as configured, not online now: a panel reads identity at link-up
 * and after it writes SENSE, so the word moves only with a SENSE write
 * taken.  Whether a part answers is SENSE FLAGS and BENCH bit 5.
 */
static void capabilities_update(void)
{
    s_state.identity[LINK_ID_CAPABILITIES] =
        (uint16_t)(IOMCU_CAPABILITIES | sense_page_caps(&s_sense));
}

/* Whether the page holds a capture that has not finished. */
static bool sense_capture_open(void)
{
    const uint16_t st = s_sense.servo[LINK_SS_CAP_STATE];
    return st == (uint16_t)LINK_CAP_ARMED || st == (uint16_t)LINK_CAP_WAIT_MOVE
           || st == (uint16_t)LINK_CAP_MOVING;
}

/*
 * The supply's pins: reserved from the outputs while it holds them, and the
 * PIO UART claimed for them.  False when no PIO block can reach them or has
 * room.
 */
static bool supply_rewire(void)
{
    pd_uart_close();
    s_pd_open = false;
    reserve_held();
    if (!supply_page_enabled(&s_supply)) {
        return true;
    }
    if (!pd_uart_open(supply_page_tx(&s_supply), supply_page_rx(&s_supply),
                      supply_page_uart_baud(&s_supply))) {
        return false;
    }
    const pdmini_io_t io = { pd_uart_attach, pd_uart_detach, pd_uart_send,
                             NULL };
    pdmini_init(&s_pd, &io, s_now_ms);
    /* Whatever the module was doing before these pins reached it -- left
     * on before a restart, or on from its own button -- is not known: it
     * is held as maybe on until read off. */
    pdmini_restored(&s_pd);
    s_pd_rate = supply_page_rate(&s_supply, NULL);
    s_pd_open = true;
    return true;
}

/*
 * The UART released and the pins this page names reserved, without
 * attaching: true when a PIO block can serve them, tried and let go again.
 */
static bool supply_probe(void)
{
    s_supply_attach = false;
    pd_uart_close();
    s_pd_open = false;
    reserve_held();
    if (!supply_page_enabled(&s_supply)) {
        return true;
    }
    if (!pd_uart_open(supply_page_tx(&s_supply), supply_page_rx(&s_supply),
                      supply_page_uart_baud(&s_supply))) {
        return false;
    }
    pd_uart_close();
    return true;
}

static void supply_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    supply_page_read(&s_supply, off, n, out);
}

/*
 * A write to the SUPPLY page, from the link or (@p checked) the wiring
 * change a state read has just shown the module off for
 * (supply_page_wire_ready()), with the UART, the reservation and the save
 * that follow it.
 */
static uint8_t supply_take(uint8_t off, uint8_t n, const uint16_t *in,
                           bool checked)
{
    /* What the driver says now, not what the last pass published: replies
     * taken since may have shown the module come on by itself. */
    if (s_pd_open) {
        /* Bytes already in the FIFO first: a reply there may show the
         * module come on by itself, and a rewire would throw it away. */
        uint8_t b;
        while (pd_uart_getc(&b)) {
            pdmini_rx(&s_pd, b, s_now_ms);
        }
    }
    supply_page_follow(&s_supply, s_pd_open ? &s_pd : NULL);
    const supply_page_t was = s_supply;
    if (s_supply_unsaved && (unsigned)off <= (unsigned)LINK_SP_OUTPUT
        && (unsigned)off + (unsigned)n > (unsigned)LINK_SP_OUTPUT
        && in[LINK_SP_OUTPUT - off] != 0u) {
        return LINK_NACK_NOT_ARMED;
    }
    const uint8_t nack =
        checked ? supply_page_wire_write(&s_supply, &s_outputs)
                : supply_page_write(&s_supply, off, n, in, &s_outputs,
                                    s_beat.alive && !s_dev.failsafe);
    if (nack != 0u) {
        return nack;
    }
    /* Each register on its own: TX and RX swapped hold the same pins. */
    const bool rewired =
        supply_page_enabled(&s_supply) != supply_page_enabled(&was)
        || (supply_page_enabled(&s_supply)
            && (s_supply.regs[LINK_SP_TX_PIN] != was.regs[LINK_SP_TX_PIN]
                || s_supply.regs[LINK_SP_RX_PIN] != was.regs[LINK_SP_RX_PIN]
                || s_supply.regs[LINK_SP_BAUD] != was.regs[LINK_SP_BAUD]));
    const bool wiring =
        memcmp(&s_supply.regs[LINK_SP_ENABLE], &was.regs[LINK_SP_ENABLE],
               LINK_SP_OUTPUT * sizeof(uint16_t)) != 0;
    if (wiring && s_supply.regs[LINK_SP_OUTPUT] != 0u) {
        /* An ON in the same frame as new wiring: that wiring is not in
         * flash yet, and the ON waits for it. */
        s_supply = was;
        return LINK_NACK_NOT_ARMED;
    }
    if (rewired && !supply_probe()) {
        /* No UART for those pins: the wiring is as it was, and the panel
         * is told rather than shown a supply that is never there.  Wiring
         * still waiting for flash goes back to waiting, detached. */
        s_supply = was;
        if (s_supply_unsaved) {
            (void)supply_probe();
            s_supply_attach = supply_page_enabled(&s_supply);
        } else {
            (void)supply_rewire();
        }
        if (checked) {
            supply_page_wire_refuse(&s_supply);   /* said in FLAGS bit 9 */
        }
        return LINK_NACK_BAD_VALUE;
    }
    if (wiring) {
        save_outputs(&s_state);
        s_supply_unsaved = out_store_pending();
    }
    if (rewired) {
        /* Attached once the wiring is in flash, so a restart in between
         * finds the pins of the module it may have to switch off; a page
         * now disabled has nothing to attach. */
        if (s_supply_unsaved && supply_page_enabled(&s_supply)) {
            s_supply_attach = true;
        } else {
            (void)supply_rewire();
        }
    }
    return 0u;
}

static uint8_t supply_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    (void)ctx;
    /* Judged against the pins core 1 still holds, as of its last tick. */
    sense_sync();
    return supply_take(off, n, in, false);
}

static void sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_page_read(&s_sense, off, n, out);
}

/*
 * The sensor bus's set-up: judged by the page against the bank and the
 * supply's pins, then its pins reserved, the set-up saved and core 1 told.
 * Refused while armed, so a save it asks for is taken at the next disarm
 * like any other.
 */
static uint8_t sense_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    (void)ctx;
    /* The pins core 1 still holds, as of its last tick. */
    sense_sync();
    uint16_t was[LINK_SN_CONFIG_COUNT];
    memcpy(was, s_sense.sense, sizeof(was));
    const uint8_t nack = sense_page_write(&s_sense, off, n, in, &s_outputs,
                                          supply_page_pins(&s_supply)
                                          | tone_page_pins(&s_tone));
    if (nack != 0u) {
        return nack;
    }
    if (memcmp(was, s_sense.sense, sizeof(was)) != 0) {
        reserve_held();
        save_outputs(&s_state);
        capabilities_update();
        sense_order();
    }
    return 0u;
}

static void servo_sense_read(void *ctx, uint8_t off, uint8_t n,
                             uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_servo_read(&s_sense, off, n, out);
}

/*
 * A capture armed or disarmed.  An arm watches its output channel for the
 * edge: the first pass that renders a changed pulse there stamps the frame
 * that carries it (outputs_hw_watch()).  Either way core 1 is told, and an
 * edge stamped for an earlier capture no longer counts.
 */
static uint8_t servo_sense_write(void *ctx, uint8_t off, uint8_t n,
                                 const uint16_t *in)
{
    (void)ctx;
    const uint16_t gen = s_sense.cap_gen;
    const uint8_t nack = sense_servo_write(&s_sense, off, n, in, &s_outputs);
    if (nack != 0u || s_sense.cap_gen == gen) {
        return nack;
    }
    const uint16_t arm = s_sense.servo[LINK_SS_CAP_ARM];
    outputs_hw_watch(((arm & LINK_SS_ARM) != 0u) ? (int)LINK_SS_ARM_OUT(arm)
                                                 : -1);
    s_edge_set = false;
    sense_order();
    return 0u;
}

/* ------------------------------------------------------------ the phase tap */

/* The pins the tap may not take: the supply's, and the sensor bus's with the
 * ones core 1 has not let go of yet. */
static uint64_t tone_taken(void)
{
    return supply_page_pins(&s_supply) | sense_page_held(&s_sense);
}

/* Core 1's order from the page, running when the capture does. */
static void tone_order(void)
{
    tone_cmd_t cmd;
    tone_page_cmd(&s_tone, tone_cap_running(), &cmd);
    tone_core1_order(&cmd, tone_cap_start_us());
}

/*
 * The capture made to agree with the page: stopped, then started on the
 * page's pin if the tap is enabled and not refused, else the pin left an
 * input with its pull-down on.  False when the tap is enabled and the
 * wiring could not take its pin (no PIO state machine, no DMA channel).
 * Core 1 is told first that nothing runs and given up to
 * TONE_CORE1_WAIT_US to finish the pass it is in, then the capture is torn
 * down, and core 1 is told again once the capture runs: it never reads a
 * ring that is being started over.
 */
static bool tone_rewire(void)
{
    tone_cap_pause();
    tone_order();
    tone_core1_quiesce();
    tone_cap_stop();
    reserve_held();
    if (!tone_page_wanted(&s_tone)) {
        if (tone_page_pin_free(&s_tone, &s_outputs, tone_taken())) {
            tone_cap_rest(tone_page_pin(&s_tone));
        }
        return true;
    }
    if (!tone_cap_start(tone_page_pin(&s_tone))) {
        return false;
    }
    tone_page_capture(&s_tone);
    tone_order();
    return true;
}

static void tone_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    tone_core1_sync(&s_tone);
    tone_page_read(&s_tone, off, n, out);
}

/*
 * The tap's set-up: judged by the page against the bank and the other
 * pages' pins, then the capture started or moved, the pin reserved, the
 * set-up saved and core 1 told.  A change of ENABLE or of the pin, and a
 * write that tries a refused tap again, rewire the capture; the wiring
 * failing there puts the old set-up back and refuses the write.  A change
 * of a range only reaches core 1.  Taken
 * armed or not: the tap is an input.
 */
static uint8_t tone_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    /* The pins core 1 still holds, as of its last tick. */
    sense_sync();
    tone_core1_sync(&s_tone);
    uint16_t was[LINK_TN_CONFIG_COUNT];
    memcpy(was, s_tone.cfg, sizeof(was));
    const bool was_refused = s_tone.refused;
    const uint16_t gen = s_tone.gen;
    const uint8_t nack = tone_page_write(&s_tone, off, n, in, &s_outputs,
                                         tone_taken());
    if (nack != 0u || s_tone.gen == gen) {
        return nack;
    }
    const bool rewire = was_refused
                        || s_tone.cfg[LINK_TN_ENABLE] != was[LINK_TN_ENABLE]
                        || s_tone.cfg[LINK_TN_PIN] != was[LINK_TN_PIN];
    if (rewire && !tone_rewire()) {
        /* No state machine or channel for that pin: the set-up as it was,
         * and the capture as it was. */
        tone_page_revert(&s_tone, was, was_refused);
        if (!tone_rewire()) {
            tone_page_refuse(&s_tone);
            reserve_held();
        }
        return LINK_NACK_BAD_VALUE;
    }
    if (!rewire) {
        reserve_held();
        tone_order();
    }
    save_outputs(&s_state);
    return 0u;
}

static void servo_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    servo_page_read(&s_servo, off, n, out);
}

/*
 * Not saved: a restart drives every slot at the binding's rate again, so a
 * servo plugged in after it never meets a rate meant for another, and runs
 * no sweep.
 */
static uint8_t servo_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    (void)ctx;
    const uint8_t nack = servo_page_write(&s_servo, off, n, in, &s_outputs,
                                          s_now_ms);
    if (nack != 0u) {
        return nack;
    }
    if (off == (uint8_t)LINK_SV_FRAME_HZ) {
        hw_apply();
    }
    return 0u;
}

static uint8_t slots_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    /* The reservation as of core 1's last tick: a bus that moved gives its
     * old pins back once core 1 has let them go. */
    sense_sync();
    uint16_t next[LINK_OS_COUNT];
    memcpy(next, s->slots, sizeof(next));
    const uint8_t nack = outputs_slots_write(next, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    /* Nor one on a pin the supply holds: the bank would leave it unbound,
     * and a restart would drive it on the module's pin. */
    if (supply_page_slots_check(&s_supply, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor on one the sensor bus holds, for the same reason: its set-up's
     * pins, and the ones core 1 has not let go of yet. */
    if (sense_page_slots_check(&s_sense, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor on the phase tap's pin while the tap runs. */
    if (tone_page_slots_check(&s_tone, next) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Nor a slot bound beside a surface at another rate. */
    if (outputs_slots_rate_check(&s_outputs, next,
                                 servo_page_hz(&s_servo)) != 0u) {
        return LINK_NACK_BAD_VALUE;
    }
    uint16_t prev[LINK_OS_COUNT];
    memcpy(prev, s->slots, sizeof(prev));
    const uint8_t bound_before = bound_slots();
    memcpy(s->slots, next, sizeof(next));
    outputs_slots_apply(&s_outputs, s->slots);
    /* The bank has decided what the slots are; this makes the silicon agree
     * with it before the next pass renders anything. */
    hw_apply();
    /*
     * A slot the bank took that the silicon could not bind is refused
     * rather than kept unbound, whatever holds what it needs: a PIO state
     * machine, instruction memory or DMA channel of the phase tap or the
     * supply's UART, a PWM slice, a pin.  The slots this write changed and
     * the ones it unbound are judged; one an earlier write or a restart left
     * unbound does not refuse a write that did not touch it.  A restart
     * binds the outputs first, so a binding kept here that the silicon
     * refused would take the resource from the tap or the supply there.
     * outputs_bind_check() in shared/ decides; this is the glue.
     */
    uint8_t watch = (uint8_t)(outputs_slots_changed(prev, next)
                              | bound_before);
    if (s_pd_open) {
        watch = 0xFFu;   /* the UART holds two state machines: every slot */
    }
    bool refused = outputs_bind_check(&s_outputs, watch, bound_slots()) != 0u;
    if (!refused && !s_pd_open && s_supply_attach) {
        /* The supply's UART waits for its save: the slots must leave it a
         * PIO block, tried now, or the attach would fail unseen. */
        refused = !pd_uart_open(supply_page_tx(&s_supply),
                                supply_page_rx(&s_supply),
                                supply_page_uart_baud(&s_supply));
        pd_uart_close();
    }
    if (refused) {
        memcpy(s->slots, prev, sizeof(prev));
        outputs_slots_apply(&s_outputs, s->slots);
        hw_apply();
        return LINK_NACK_BAD_VALUE;
    }
    save_outputs(s);
    return 0u;
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    /*
     * The page's rules are link_control_write()'s, host-tested: every
     * register of the frame is validated before any is stored, so a refused
     * write leaves the page as it was and lifts no latched failsafe.  What
     * is decided here is whether the bench may arm -- the coprocessor's
     * call, not the panel's -- and what a clear does.
     */
    bool cleared = false;
    const uint8_t nack = link_control_write(s->control, off, n, in,
                                            !s_dev.failsafe && s_beat.alive,
                                            &cleared);
    if (nack != 0u) {
        return nack;
    }
    if (cleared) {
        /*
         * The clock of this pass, as recorded by the dispatcher, not a
         * fresh read.  A fresh read is later than the `now` that
         * link_dev_tick() receives a few lines further on, and the
         * wrap-safe comparison there reads a timestamp in the future as
         * 4,294,967,295 ms of silence, which re-arms the failsafe
         * immediately.
         */
        link_dev_clear_failsafe(&s_dev, s_dev.last_request_ms);
    }
    /*
     * The throttle rides the control page rather than the CHANNELS page, so
     * a motor command keeps control priority on the wire; underneath it is
     * the same bank, so it is set here the same way a channel is.
     */
    const uint16_t thr = (uint16_t)(((uint32_t)s->control[LINK_CT_THROTTLE]
                                     * OUT_SPAN) / LINK_THROTTLE_MAX);
    (void)outputs_set(&s_outputs, CH_THROTTLE, thr, s_now_ms);
    /*
     * And the same command to the pins bound as motors.  CH_THROTTLE is off
     * the page and nothing renders it, so on its own it drives no pin: the
     * throttle screen would command a channel no slot reads while the ESC on
     * GP0 sat at its rest for ever.  Which channels are motors is the
     * binding's answer, carried as the role on the CHAN_CFG page, so a servo
     * bound beside a motor is left alone.
     */
    (void)outputs_set_role_channels(&s_outputs, OUT_ROLE_THROTTLE,
                                    (uint8_t)LINK_OUT_CHANNELS, thr,
                                    s_now_ms);
    return 0;
}

/*
 * This board's own pins, so a panel that has never heard of it can still
 * offer the right ones.
 *
 * Rendered on demand rather than held: the catalogue is const and the page
 * is read once at link-up, so a cached copy would be thirty-two registers of
 * RAM to save a loop that runs once.
 *
 * Saying a pin is free here does not make it free.  outputs_reserve_pins()
 * has already been given the union of this catalogue and the pins this file
 * assigns, and refuses the rest whatever the page says -- so a catalogue
 * that is wrong costs a pin rather than the safety line.
 */
static void catalogue_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_CAT_COUNT];
    outbind_board_to_regs(outbind_board(IOMCU_BOARD_ID), all);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/*
 * And where those pins are, so a panel that has never seen this board can
 * draw it rather than only list it.  Zeroes when the build has no shape for
 * the board, which the panel reads as "not drawable" and nothing worse.
 */
static void shape_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_SH_COUNT];
    outbind_shape_to_regs(outbind_board(IOMCU_BOARD_ID), all);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/*
 * A photograph of this board.
 *
 * Generated by tools/gen_board_art.py from artwork/rp2350-can.png and
 * checked in, so this build needs no Python and no image library.  It is a
 * const array in this binary rather than anything in the flash store: no
 * erase window, no wear, and its version is the firmware version.  Two
 * hundred kilobytes, well inside the four megabytes out_store.c pins itself
 * to for a module that has four rather than the sixteen the board file
 * claims.
 *
 * A build carrying no picture sets these to nothing and answers
 * LINK_AW_BLOCKS of zero, which is the ordinary reply and not a fault: the
 * panel then draws the board from its shape page instead.
 */
extern const uint16_t art_rp2350_can[];
extern const uint16_t art_rp2350_can_width;
extern const uint16_t art_rp2350_can_height;
extern const uint32_t art_rp2350_can_bytes;
extern const uint16_t art_rp2350_can_crc;

#define ART_PIXELS art_rp2350_can
#define ART_BYTES  art_rp2350_can_bytes
#define ART_W      art_rp2350_can_width
#define ART_H      art_rp2350_can_height
#define ART_CRC    art_rp2350_can_crc

static void artwork_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_AW_COUNT];
    for (unsigned i = 0; i < LINK_AW_COUNT; ++i) { all[i] = 0u; }
    if (ART_BYTES != 0u) {
        const uint32_t blocks =
            (ART_BYTES + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
        all[LINK_AW_BLOCKS]   = (uint16_t)blocks;
        all[LINK_AW_WIDTH]    = ART_W;
        all[LINK_AW_HEIGHT]   = ART_H;
        all[LINK_AW_FORMAT]   = (uint16_t)LINK_ART_RGB565;
        all[LINK_AW_BYTES_LO] = (uint16_t)(ART_BYTES & 0xFFFFu);
        all[LINK_AW_BYTES_HI] = (uint16_t)(ART_BYTES >> 16);
        all[LINK_AW_CRC]      = ART_CRC;
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

/*
 * Which block the data page is answering with.  Held here because the host
 * says it in one transaction and reads it in the next: a block that advanced
 * on being read would resend nothing after a reply went missing, and the
 * panel would assemble a picture with a hole in it.
 */
static uint16_t s_art_block;

static void art_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_AD_COUNT];
    for (unsigned i = 0; i < LINK_AD_COUNT; ++i) { all[i] = 0u; }
    all[LINK_AD_BLOCK] = s_art_block;
    /*
     * The artwork is pixels; the page carries bytes, low byte of a pixel
     * first.  The split is written out rather than left to the part being
     * little-endian, because the byte order here is the wire's and not this
     * processor's.
     */
    const uint32_t at = (uint32_t)s_art_block * LINK_AD_BYTES;
    for (unsigned i = 0; i < LINK_AD_BYTES && at + i < ART_BYTES; ++i) {
        const uint32_t byte = at + i;
        const uint16_t px   = ART_PIXELS[byte / 2u];
        const uint16_t b    = (byte & 1u) ? (uint16_t)(px >> 8)
                                          : (uint16_t)(px & 0xFFu);
        all[LINK_AD_DATA + i / 2u] |=
            (uint16_t)((i & 1u) ? (uint16_t)(b << 8) : b);
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

static uint8_t art_data_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    (void)ctx;
    /* Only the block register is writable; the payload is this board's. */
    if (off != LINK_AD_BLOCK || n != 1u) {
        return (uint8_t)LINK_NACK_READ_ONLY;
    }
    s_art_block = in[0];
    return 0u;
}

/*
 * And which pads are grounds and rails, so a servo lead's other two wires
 * can be found on the screen instead of by counting pads on the board.
 */
static void pads_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_PAD_COUNT];
    outbind_pads_to_regs(outbind_board(IOMCU_BOARD_ID), all);
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = all[off + i];
    }
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_STATUS,   LINK_ST_COUNT, status_read,   NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_CHANNELS, LINK_CH_COUNT, channels_read, channels_write },
    { LINK_PAGE_BENCH,    LINK_BN_COUNT, bench_read,    NULL },
    { LINK_PAGE_OUTPUTS,  LINK_OS_COUNT, slots_read,    slots_write },
    { LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, chan_cfg_read, chan_cfg_write },
    { LINK_PAGE_CATALOGUE, LINK_CAT_COUNT, catalogue_read, NULL },
    { LINK_PAGE_SHAPE,     LINK_SH_COUNT,  shape_read,     NULL },
    { LINK_PAGE_ARTWORK,   LINK_AW_COUNT,  artwork_read,   NULL },
    { LINK_PAGE_ART_DATA,  LINK_AD_COUNT,  art_data_read,  art_data_write },
    { LINK_PAGE_PADS,      LINK_PAD_COUNT, pads_read,      NULL },
    { LINK_PAGE_SERVO,     LINK_SV_COUNT,  servo_read,     servo_write },
    { LINK_PAGE_SUPPLY,    LINK_SP_COUNT,  supply_read,    supply_write },
    { LINK_PAGE_SENSE,     LINK_SN_COUNT,  sense_read,     sense_write },
    { LINK_PAGE_SERVO_SENSE, LINK_SS_COUNT, servo_sense_read,
      servo_sense_write },
    { LINK_PAGE_TONE,      LINK_TN_COUNT,  tone_read,      tone_write },
};

/* ------------------------------------------------------------ the heartbeat */

/*
 * The panel's safety line, watched in firmware as well as in hardware.
 *
 * The retriggerable monostable on the daughterboard is the backstop: it
 * holds the output enable up only while edges keep arriving, with no
 * software involved.  It cannot tell a heartbeat from noise: a ringing line,
 * a short to a clock or a floating input next to a switching supply all
 * retrigger it.  heartbeat_mon_t knows the expected period and rejects what
 * cannot be a 39 Hz render loop.
 *
 * The pin is sampled in the main loop rather than by an interrupt.  The loop
 * has no blocking call and turns over far faster than HEARTBEAT_MIN_GAP_MS
 * (4 ms), so a sample cannot miss an edge the monitor would accept; the
 * slowest thing in the loop is a printf every 3 s.  An ISR (interrupt
 * service routine) would notice edges faster than the floor, which the
 * monitor rejects anyway.
 */
static bool s_beat_level;

static void heartbeat_init(void)
{
    gpio_init(IOMCU_HEARTBEAT_PIN);
    gpio_set_dir(IOMCU_HEARTBEAT_PIN, GPIO_IN);
    /* Pulled down, so an unplugged or unpowered panel reads as a line that is
     * not edging rather than as one held high. */
    gpio_pull_down(IOMCU_HEARTBEAT_PIN);
    heartbeat_mon_init(&s_beat);
    s_beat_level = gpio_get(IOMCU_HEARTBEAT_PIN);
}

/** Sample the line; returns whether it may currently be believed. */
static bool heartbeat_poll(uint32_t now)
{
    const bool level = gpio_get(IOMCU_HEARTBEAT_PIN);
    if (level != s_beat_level) {
        s_beat_level = level;
        heartbeat_mon_edge(&s_beat, now);
    }
    return heartbeat_mon_alive(&s_beat, now);
}

/* ------------------------------------------------------------ the CAN bus */

/*
 * Tried at boot, and not fatal if it fails.
 *
 * The controller either answers on SPI (Serial Peripheral Interface) or it
 * does not, and the datasheet guarantees the mode it wakes in, so this
 * distinguishes "no module fitted" and "SPI miswired" from anything on the
 * CAN (Controller Area Network) bus itself.
 *
 * The echo responder runs unconditionally.  It costs one register read per
 * loop, answers only frames addressed to a page the map does not use, and
 * runs at the lowest priority on the bus, so it stays in every build.
 */
static bool     s_can_up;
static uint32_t s_can_echoes;
static uint32_t s_can_overflows;

/*
 * When a frame was last taken out of the controller.
 *
 * can_service() empties both receive buffers every pass, so where this is
 * read it is the age of the last frame on the bus rather than of the last one
 * collected.  The flash windows are opened against it: this core answers
 * nothing while it writes, and the two buffers hold 260 us of frames.  Zero
 * until the first frame, so a board that has heard nothing since boot reads
 * as a quiet bus, which is what it has.
 */
static uint32_t s_last_rx_ms;

static void can_start(void)
{
    s_can_up = xl2515_init(IOMCU_CAN_BITRATE);
    printf("rcbench-iomcu: CAN %s at %u bit/s\n",
           s_can_up ? "up" : "DID NOT ANSWER (module fitted? SPI wiring?)",
           (unsigned)IOMCU_CAN_BITRATE);
}

/*
 * One clock for the whole pass, handed in.  Every timeout is a wrap-safe
 * unsigned subtraction, so a last_request_ms stamped 1 ms later than the
 * `now` that link_dev_tick() receives reads as 4,294,967,295 ms of silence,
 * past every timeout there is.
 */
static void can_service(uint32_t now)
{
    if (!s_can_up) {
        return;
    }
    if (xl2515_take_overflow()) {
        ++s_can_overflows;
    }
    link_can_frame_t in, out;
    while (xl2515_recv(&in)) {
        s_last_rx_ms = now;
        if (can_selftest_echo(&in, &out)) {
            if (xl2515_send(&out)) {
                ++s_can_echoes;
            }
            continue;
        }
        /*
         * The far end asking what this end can see, so one console shows
         * both halves of a fault.
         */
        can_remote_status_t st;
        memset(&st, 0, sizeof(st));
        st.up = s_can_up;
        xl2515_errors(&st.tx_errors, &st.rx_errors, &st.flags);
        st.echoes    = (uint16_t)s_can_echoes;
        st.overflows = (uint16_t)s_can_overflows;
        if (can_selftest_status_reply(&in, &st, &out)) {
            (void)xl2515_send(&out);
            continue;
        }

        /*
         * Everything else is the link itself: a page request, answered by
         * the dispatcher, which has no transport in it.
         */
        link_msg_t req, reply;
        if (!link_can_decode(&in, &req)) {
            continue;
        }
        ++s_frames;
        if (!link_dev_dispatch(&s_dev, &req, &reply, now)) {
            continue;
        }
        link_can_frame_t frames[LINK_CAN_MAX_FRAMES];
        const size_t n = link_can_encode(&reply, frames, LINK_CAN_MAX_FRAMES);
        for (size_t i = 0; i < n; ++i) {
            /*
             * The transmit buffer holds one frame, so a multi-frame answer
             * waits for each frame to win arbitration.  Bounded at 1000
             * spins, because a bus that has stopped accepting must not stall
             * the failsafe.
             */
            for (int spin = 0; spin < 1000 && !xl2515_send(&frames[i]); ++spin) {
                tight_loop_contents();
            }
        }
    }
}

/*
 * The current monitors as the pages show them, with can_report() every 3 s,
 * while either part is enabled: what a bench session reads before the
 * panel has a screen for them.  The registers as published, in their own
 * units: FLAGS, PRESENT and the IDs in hex, BENCH's 10 mV and 10 mA, CH1's
 * mA and mV, times in 0.1 ms.
 */
static void sense_report(void)
{
    if (!sense_page_enabled(&s_sense)) {
        return;
    }
    sense_sync();
    const uint16_t *n = s_sense.sense;
    const uint16_t *v = s_sense.servo;
    const uint16_t *b = s_state.bench;
    printf("rcbench-iomcu: sense flags 0x%04X present 0x%04X ids 0x%04X "
           "0x%04X errors %u | bench %u cV %u cA %u mAh %u dWh flags 0x%02X "
           "| temp %d dC\n",
           n[LINK_SN_FLAGS], n[LINK_SN_PRESENT], n[LINK_SN_I228_ID],
           n[LINK_SN_I3221_ID], n[LINK_SN_ERRORS], b[LINK_BN_VOLTAGE_CV],
           b[LINK_BN_CURRENT_CA], b[LINK_BN_CHARGE_MAH], b[LINK_BN_ENERGY_DWH],
           b[LINK_BN_FLAGS], (int)(int16_t)n[LINK_SN_I228_TEMP_DC]);
    printf("rcbench-iomcu: sense window %u CH1 %d mA %u mV ch_flags 0x%02X | "
           "capture state %u seq %u move %u arrive %u\n",
           v[LINK_SS_WINDOW], (int)(int16_t)v[LINK_SS_CH_MEAN_MA],
           v[LINK_SS_CH_MEAN_MV], v[LINK_SS_CH_FLAGS], v[LINK_SS_CAP_STATE],
           v[LINK_SS_CAP_SEQ], v[LINK_SS_CAP_MOVE_T], v[LINK_SS_CAP_ARRIVE_T]);
}

/*
 * Repeated every 3 s rather than printed at boot alone.  USB (Universal
 * Serial Bus) CDC (communications device class) does not exist until a host
 * enumerates it, so anything printed before the terminal opens is lost.
 */
static void can_report(uint32_t now)
{
    static uint32_t last;
    if (now - last < 3000u && last != 0u) {
        return;
    }
    last = now;

    if (out_store_is_off()) {
        printf("rcbench-iomcu: flash store OFF -- core 1 did not register "
               "for the flash lock-out; set-ups run from RAM and are lost at "
               "restart\n");
    }
    if (!s_can_up) {
        printf("rcbench-iomcu: CAN did not answer on SPI -- module fitted? "
               "wiring on GP9-12?\n");
        sense_report();
        return;
    }
    uint8_t tec = 0, rec = 0, eflg = 0;
    xl2515_errors(&tec, &rec, &eflg);
    /*
     * Requests first, because it is the number that answers "is the other
     * end talking to me at all".  Echoes are self-test traffic and are zero
     * in ordinary use, which reads as nothing arriving when this line carried
     * them alone.  A request count standing still while the panel says NO
     * LINK means the panel has stopped transmitting; one that climbs while
     * the panel says NO LINK means the answers are not getting back.
     */
    printf("rcbench-iomcu: CAN up, %u bit/s, %lu requests served, "
           "%lu self-test echoes, tx_err %u rx_err %u eflg 0x%02X\n",
           (unsigned)IOMCU_CAN_BITRATE, (unsigned long)s_frames,
           (unsigned long)s_can_echoes, tec, rec, eflg);
    /*
     * Said in words: an overflow is the one thing that explains a frame
     * going missing while the bus reports no error.  The part has two
     * receive buffers, so anything that stops this loop for two frame times
     * costs a frame, and a printf to a USB host that has stopped reading is
     * such a thing.
     */
    if (s_can_overflows > 0u) {
        printf("rcbench-iomcu: CAN receive buffers overran %lu time(s) -- "
               "frames arrived with nowhere to put them; not a bus fault\n",
               (unsigned long)s_can_overflows);
    }
    sense_report();
}

/* ---------------------------------------------------------------- the loop */

/*
 * The numbers, and only the ones something measured.
 *
 * Speed, and the ESC's own voltage, current and temperature, come from a
 * bidirectional DShot ESC (electronic speed controller) answering on its
 * own signal line.  Voltage, current, power, their peaks, charge and energy
 * come from the INA228 instead while it is enabled and answering
 * (LINK_BN_SENSED, sense_page_bench()), and the ESC's two then stay on the
 * SENSE page.  A quantity nothing measured is zero with its valid bit
 * clear, and the panel draws that field empty.
 *
 * LINK_BN_SIMULATED is never set here.  A coprocessor that is answering is
 * reporting what it can see; the panel models only when nothing answers at
 * all, and marks that itself.
 */
static bench_state_t s_bench;

/*
 * How stale a speed may be before it stops being reported.
 *
 * Frames go out at a kilohertz, so a reply older than this is not a slow
 * update but an ESC that has stopped answering -- unplugged, or one that
 * never did bidirectional DShot.  A held-over speed on a stopped motor is
 * exactly the plausible wrong number this bench exists to avoid.
 */
#define RPM_STALE_MS  200u

/*
 * How old an extended-telemetry reading may be before the field goes empty.
 *
 * Longer than RPM_STALE_MS because the rates are not the same: an ESC answers
 * every frame with a period and interleaves temperature, voltage and current
 * between them a few times a second.  Holding those to the speed's window
 * would blank fields that are arriving exactly as the protocol sends them.
 */
#define EDT_STALE_MS  2000u

/*
 * The peaks belong to a run, and a run starts when the bank arms.
 *
 * They are kept on this end because it has the fast samples and the panel
 * sees one poll in fifty of them, so only this end can see the peak at all.
 * Holding them since boot instead would report a maximum from a motor that
 * was taken off the bench two runs ago.
 */
static bool s_was_driving;

static void sample(void)
{
    /*
     * The live readings and their valid bits are rebuilt every sample; the
     * peaks are not, because a peak that is recomputed from one sample is
     * the current reading wearing a different name.
     */
    s_bench.voltage     = 0.0f;
    s_bench.current     = 0.0f;
    s_bench.power       = 0.0f;
    s_bench.rpm         = 0.0f;
    s_bench.temp_esc    = 0.0f;
    s_bench.temp_motor  = 0.0f;
    s_bench.flags       = 0u;

    uint32_t erpm = 0u;
    uint32_t age  = 0u;
    const uint16_t poles = s_state.control[LINK_CT_MOTOR_POLES];
    if (poles != 0u && outputs_hw_erpm(&erpm, &age) && age <= RPM_STALE_MS) {
        /* Pole pairs, not poles: one electrical revolution per pair. */
        s_bench.rpm = (float)dshot_rpm(erpm, (uint8_t)(poles / 2u));
        s_bench.flags |= (uint16_t)LINK_BN_RPM_OK;
    }

    /*
     * What the ESC says about itself, when it does extended telemetry.  The
     * units belong to the frame type: a quarter of a volt and a whole amp per
     * count, and degrees Celsius as a byte.
     *
     * There is no other source for any of these on this board.  An ESC that
     * was not asked, or that does not know the command, leaves them empty and
     * the flags clear, which is what the panel draws as a blank field rather
     * than as a zero.
     */
    uint16_t edt = 0u;
    if (outputs_hw_edt(DSHOT_TELEM_VOLTAGE, &edt, &age) && age <= EDT_STALE_MS) {
        s_bench.voltage = (float)edt * 0.25f;
        s_bench.flags |= (uint16_t)LINK_BN_VOLTAGE_OK;
    }
    if (outputs_hw_edt(DSHOT_TELEM_CURRENT, &edt, &age) && age <= EDT_STALE_MS) {
        s_bench.current = (float)edt;
        s_bench.flags |= (uint16_t)LINK_BN_CURRENT_OK;
    }
    if (outputs_hw_edt(DSHOT_TELEM_TEMPERATURE, &edt, &age)
        && age <= EDT_STALE_MS) {
        /* The ESC's own, and only that.  LINK_BN_TEMP_MOT_OK stays clear:
         * an ESC knows nothing about the motor it drives, and temp_motor has
         * no source on this board at all. */
        s_bench.temp_esc = (float)edt;
        s_bench.flags |= (uint16_t)LINK_BN_TEMP_OK;
    }
    /* Power is the product and not a reading, so it is only as good as both
     * halves: one of them missing leaves it empty rather than zero. */
    if ((s_bench.flags & (uint16_t)LINK_BN_VOLTAGE_OK) != 0u
        && (s_bench.flags & (uint16_t)LINK_BN_CURRENT_OK) != 0u) {
        s_bench.power = s_bench.voltage * s_bench.current;
    }
    /* The ESC's own voltage and current go to SENSE as well, where they
     * stay while BENCH carries the INA228's. */
    sense_page_esc(&s_sense,
                   (s_bench.flags & (uint16_t)LINK_BN_VOLTAGE_OK) != 0u,
                   s_bench.voltage,
                   (s_bench.flags & (uint16_t)LINK_BN_CURRENT_OK) != 0u,
                   s_bench.current);

    /* On the edge into driving, so a run's peaks are that run's.  The reset
     * takes the current reading rather than zero, which is what stops a sag
     * floor of 0 V reading as a collapsed pack. */
    const bool driving = outputs_driving(&s_outputs);
    if (driving && !s_was_driving) {
        bench_state_reset_peaks(&s_bench);
    }
    s_was_driving = driving;

    /* Peaks only from readings that arrived, and a sag floor seeded by the
     * first voltage of the run rather than by the reset that opened it. */
    bench_state_track_peaks(&s_bench);
    /* Then the INA228's numbers over the ESC's, while it is the source:
     * the 50 ms window's voltage and current, the run's peaks from its
     * 500 Hz samples, and its charge and energy (sense_page_bench()). */
    sense_sync();
    sense_page_bench(&s_sense, &s_sense_snap, s_run_gen, driving, &s_bench);
    bench_state_to_regs(&s_bench, s_state.bench);

    s_state.status[LINK_ST_STATE] =
        (s_dev.failsafe || !s_beat.alive)
            ? (uint16_t)LINK_STATE_FAILSAFE
            : (s_state.control[LINK_CT_ARM] != 0
                   ? (uint16_t)LINK_STATE_ARMED
                   : (uint16_t)LINK_STATE_IDLE);
    uint16_t faults = 0;
    if (s_dev.failsafe) {
        faults |= (uint16_t)LINK_FAULT_LINK_SILENT;
    }
    if (!s_beat.alive) {
        faults |= (uint16_t)LINK_FAULT_HEARTBEAT;
    }
    if (out_store_is_off()) {
        faults |= (uint16_t)LINK_FAULT_STORE_OFF;
    }
    s_state.status[LINK_ST_FAULTS] = faults;

    const uint32_t up = (uint32_t)to_ms_since_boot(get_absolute_time());
    s_state.status[LINK_ST_UPTIME_MS_LO] = (uint16_t)(up & 0xFFFFu);
    s_state.status[LINK_ST_UPTIME_MS_HI] = (uint16_t)(up >> 16);

    /* What this end has seen of the wire.  The panel compares these against
     * its own, and the comparison is what tells a dead coprocessor apart from
     * a return path that never releases. */
    s_state.status[LINK_ST_FRAMES_LO] = (uint16_t)(s_frames & 0xFFFFu);
    s_state.status[LINK_ST_FRAMES_HI] = (uint16_t)(s_frames >> 16);
    /*
     * LINK_ST_CRC_ERRORS and LINK_ST_RESYNCS carry the controller's receive
     * and transmit error counters: CAN finds and checks frame boundaries in
     * silicon, so this end has no CRC (cyclic redundancy check) or resync
     * count of its own.
     */
    uint8_t tec = 0, rec = 0;
    xl2515_errors(&tec, &rec, NULL);
    s_state.status[LINK_ST_CRC_ERRORS] = rec;
    s_state.status[LINK_ST_RESYNCS]    = tec;
}

/*
 * The failsafe edge: disarm the one bank every output goes through, then
 * bring the pages into line with it so what the panel reads back is what the
 * bank holds.
 */
static void outputs_off(void)
{
    outputs_arm(&s_outputs, false, s_now_ms);

    s_state.control[LINK_CT_ARM]      = 0;
    s_state.control[LINK_CT_THROTTLE] = 0;
    /*
     * The channels page is what a read shows the panel, and it is filled from
     * the bank for the same reason boot fills it that way: zero is a
     * throttle's rest and a surface's low endpoint, so a zeroed page reports
     * a surface nobody commanded at its stop while the pin rests at centre.
     * The bank's commands survive a disarm, so a channel that was commanded
     * reads back what it was asked for, and ARM = 0 beside it says nothing
     * renders it.
     */
    outputs_channels_from_bank(&s_outputs, s_state.channels);
    /* And the pins, now rather than at the top of the next pass: a failsafe
     * that waits for the loop to come round is a failsafe with a latency. */
    outputs_hw_service(&s_outputs);
}

int main(void)
{
    stdio_init_all();

    s_state.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    s_state.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    /*
     * What this image is, so a board can be asked rather than guessed at.
     * The protocol version says what it speaks; these say which build is
     * speaking it, and the two move independently.
     */
    s_state.identity[LINK_ID_FIRMWARE_MAJOR] = RCBENCH_VERSION_MAJOR;
    s_state.identity[LINK_ID_FIRMWARE_MINOR] = RCBENCH_VERSION_MINOR;
    s_state.identity[LINK_ID_FIRMWARE_PATCH] = RCBENCH_VERSION_PATCH;
    /*
     * Which board this is.  The panel offers the pins of the board that
     * answered and of no other, so this register is what stops a pin map
     * being shown for hardware that is not on the bench.
     */
    s_state.identity[LINK_ID_HARDWARE] = IOMCU_BOARD_ID;
    /*
     * What this build can do, which the panel marks its menu from.  The three
     * bits set are the three the output drivers make true: pulses to a servo
     * lead, a signal line to an ESC (electronic speed controller), and
     * telemetry back from one over bidirectional DShot.  The rest are parts
     * that are not fitted -- a shunt, a cell monitor, an accelerometer -- or
     * a program that is not written, and each is set by the thing arriving.
     *
     * These say the coprocessor can, not that anything is connected.  An ESC
     * that does not answer is an ESC that does not answer, and the BENCH
     * page's valid bits are where that shows.  The current monitors add
     * pack sense and servo sense for the parts the SENSE set-up enables,
     * set once the set-up is restored below (capabilities_update()).
     */
    s_state.identity[LINK_ID_CAPABILITIES] = IOMCU_CAPABILITIES;

    /* A range before anybody sets one, so the clamp is meaningful from the
     * first frame rather than from the first configuration. */
    outputs_channels_defaults(s_state.channels);
    outputs_chan_cfg_defaults(s_state.chan_cfg);
    outputs_slots_defaults(s_state.slots);
    servo_page_init(&s_servo);

    /*
     * Then what was saved, over the defaults.  This configures the outputs;
     * it does not drive them.  Every driver is gated by outputs_driving(),
     * which is the bank's armed flag, and this end sets that flag only while
     * the ARM register is set, the link is out of failsafe and the heartbeat
     * is trusted, so a restored binding claims its pins and holds them at
     * idle until somebody arms.  The channels are not restored: a command is
     * not a configuration, and a bench that came back holding the last
     * throttle it was given is exactly what must not happen.
     */
    out_store_t saved;
    const bool have_saved = out_store_load(&saved);
    if (have_saved) {
        memcpy(s_state.slots, saved.slots, sizeof(s_state.slots));
        memcpy(s_state.chan_cfg, saved.chan_cfg, sizeof(s_state.chan_cfg));
    }

    const uint32_t now0 = (uint32_t)to_ms_since_boot(get_absolute_time());
    /* Before anything that can reach a page callback: can_start() below opens
     * the controller, and the first frame after it is dispatched with this. */
    s_now_ms = now0;
    outputs_init(&s_outputs, now0);
    /*
     * The pins this build will not hand out, whatever the host asks for: the
     * safety line, the CAN (Controller Area Network) controller's four SPI
     * (Serial Peripheral Interface) pins and its interrupt, the pins the
     * module uses and does not break out (GP23, GP24, GP25, GP29), and every
     * number above the last GPIO (general-purpose input/output) the module
     * has, whatever board the SDK is built for.  The pin arrives from the panel over the OUTPUTS page, so it is whatever an
     * operator typed, and an output bound to the heartbeat input is an
     * interlock that stops working with nothing to show for it.
     *
     * The union of what shared/outputs greys out on the panel and what this
     * file assigns, so the two disagreeing costs a pin rather than the safety
     * line.
     */
    s_base_reserved = outbind_reserved_mask(IOMCU_BOARD_ID)
                      | IOMCU_RESERVED_PINS;
    outputs_reserve_pins(&s_outputs, s_base_reserved);
    supply_page_init(&s_supply);
    sense_page_init(&s_sense);
    tone_page_init(&s_tone);
    tone_core1_init();
    (void)outputs_set_role(&s_outputs, CH_THROTTLE, OUT_ROLE_THROTTLE);
    outputs_chan_cfg_apply(&s_outputs, s_state.chan_cfg);
    outputs_slots_apply(&s_outputs, s_state.slots);
    /*
     * The page takes its values from the bank, not the other way round.
     * Nobody has commanded anything yet, and the defaults above filled the
     * page with zero -- which is a throttle's rest and a surface's low
     * endpoint.  Applied as commands, that asks every bound surface for its
     * endpoint, and outputs_arm() restamps the clock, so the staleness
     * timeout cannot return it to centre for a further 500 ms.  A servo
     * bound beside a motor would drive its stop for that long on every arm
     * until something commanded it.
     */
    outputs_channels_from_bank(&s_outputs, s_state.channels);
    outputs_hw_init();
    hw_apply();
    /*
     * The PD mini's wiring, as last written, driven with the output off: a
     * module this end left on before it restarted is identified, read on and
     * switched off before any panel has written the page.  Through the
     * page's own checks, after the slots, so a pin a slot holds is refused;
     * and after the slots' hardware, so the UART takes what PIO the outputs
     * leave, as it does when written at run time, and never displaces one.
     */
    if (have_saved
        && supply_page_write(&s_supply, LINK_SP_ENABLE, LINK_SP_OUTPUT,
                             saved.supply, &s_outputs, false) == 0u) {
        if (!supply_rewire()) {
            supply_page_init(&s_supply);
            (void)supply_rewire();
        }
    }
    /*
     * The sensor bus's set-up, through the page's own checks as well: after
     * the slots and the supply, so a pin either already holds is refused and
     * the page starts with both parts off.  The bank is not armed yet, so
     * nothing refuses it for that.
     *
     * And the phase tap's, last, the same way: a pin an output, the supply
     * or the sensor bus holds is refused, the tap does not run, and FLAGS
     * says so.  Its capture starts after their PIO programs, so it takes
     * what state machine they leave: when the outputs and an enabled tap
     * need more than the silicon has, the outputs bind and the tap is the
     * one left out, with the TONE page's flag bit 1 (LINK_TN_PIN_REFUSED)
     * set until a write tries it again.  An OUTPUTS write at run time
     * cannot reach that state with the tap running: it is refused, so the
     * order here and the one at run time agree.
     */
    if (have_saved) {
        (void)sense_page_write(&s_sense, LINK_SN_ENABLE,
                               (uint8_t)LINK_SN_CONFIG_COUNT, saved.sense,
                               &s_outputs, supply_page_pins(&s_supply));
        reserve_held();
        tone_page_restore(&s_tone, saved.tone, &s_outputs, tone_taken());
    }
    if (!tone_rewire()) {
        tone_page_refuse(&s_tone);
        reserve_held();
    }
#if IOMCU_SENSE_BRINGUP
    /*
     * A bring-up build (cmake -DIOMCU_SENSE_BRINGUP=ON): with no part
     * enabled in flash, both are enabled at the page's defaults -- GP16 and
     * GP17, the INA228 at 0x45 on 200 uOhm, the INA3221 at 0x40 reading
     * CH1 -- for this boot, through the page's own checks, and not saved.
     * It is how a bench reads the parts before the panel can write the
     * SENSE page.
     */
    if (!sense_page_enabled(&s_sense)) {
        const uint16_t en = (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221);
        (void)sense_page_write(&s_sense, LINK_SN_ENABLE, 1u, &en, &s_outputs,
                               supply_page_pins(&s_supply));
        reserve_held();
    }
#endif
    capabilities_update();
    link_dev_init(&s_dev, k_pages, count_of(k_pages), &s_state, now0);

    heartbeat_init();

    /*
     * The store's spare sectors, erased before the controller is started.
     * Nothing can arrive yet, so these are the windows in the run that cost no
     * frame at all, and the first save after them is a page program.
     *
     * A loop, because out_store_reclaim() erases one sector per call and a
     * store can have more than one to take: a store whose sectors all hold
     * records this build cannot read -- an earlier record version -- has one
     * per sector.  Left to the main loop the second of those would be a 19 ms
     * window with the controller already up.
     *
     * Bounded by the store's shape, not by the flash answering.
     * flash_range_erase() reports nothing, so a sector that will not erase is
     * surveyed as not erased and offered again; a loop that ended only when
     * the store came back clean would spin here for ever, before can_start(),
     * with no CAN controller and no failsafe. Past this bound the sector
     * falls to the main loop, which costs a window per pass and keeps the
     * bench answering.
     */
    for (unsigned i = 0; i < OUT_STORE_SECTORS
                         && out_store_reclaim(false, OUT_STORE_QUIET_MS); ++i) {
        printf("rcbench-iomcu: output store sector reclaimed at boot, "
               "window %lu us\n",
               (unsigned long)out_store_last_erase_us());
    }

    /*
     * Core 1 and the sensor bus, after the boot's flash windows -- core 1
     * is not running for them, so they need no lock-out -- and with the
     * set-up restored above as its first order.  It opens the bus within
     * its first tick.  The pins are already held from the outputs by the
     * page.
     */
    {
        sense_cmd_t first;
        sense_build(&first);
        /*
         * A core 1 that did not register for the lock-out cannot be parked
         * for a flash window: flash_safe_execute() would refuse every one,
         * or assert in a build with asserts on, and an erase run anyway
         * would fault core 1 executing from flash.  The store is switched
         * off for this boot instead -- set-ups run from RAM -- and the
         * STATUS page says so in LINK_FAULT_STORE_OFF, which the panel's
         * band shows as a fault; the console repeats it every 3 s.
         */
        if (sense_core1_start(&first)) {
            out_store_core1_parkable();   /* windows park core 1 from now */
        } else {
            out_store_off();
        }
    }

    can_start();
    memset(&s_bench, 0, sizeof(s_bench));

    uint32_t last_sample = (uint32_t)to_ms_since_boot(get_absolute_time());

    for (;;) {
        /*
         * ONE CLOCK PER PASS, used by everything below.  Every timeout here
         * is a wrap-safe unsigned subtraction, so a timestamp 1 ms ahead of
         * the `now` it is compared against reads as 4,294,967,295 ms of
         * silence, past every timeout there is.
         *
         * Published as s_now_ms for the same reason: the page callbacks run
         * under can_service() below, they stamp the bank and the store, and
         * a clock they read for themselves is a clock that can already be
         * ahead of this one.
         */
        uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());
        s_now_ms = now;

        /* Polled rather than interrupt-driven: the loop turns over far faster
         * than a frame takes to arrive, and the failsafe has to fire on time
         * whether or not anything is arriving. */
        can_service(now);
        /* Core 1's beeps and status into the TONE page. */
        tone_core1_sync(&s_tone);

        /*
         * Two independent watchdogs, and both before the bank is armed.  The
         * link watchdog says the panel has stopped talking; this one says the
         * panel has stopped running.  A panel wedged mid-frame can still have
         * an interrupt answering polls, so the link watchdog alone would not
         * fire.
         *
         * Both are polled here rather than left to the end of the pass,
         * because silence generates no event: s_beat.alive and s_dev.failsafe
         * are only as fresh as the last call that looked, and arming on last
         * pass's answer renders one more service of every output after the
         * line has gone past HEARTBEAT_MAX_GAP_MS (150 ms) or the link past
         * its own silence timeout.
         *
         * After can_service() above, so a frame that arrived this pass has
         * already cleared the silence before it is judged.
         */
        const bool was_beating = s_beat.alive;
        if (!heartbeat_poll(now) && was_beating) {
            outputs_off();   /* fires on the edge only */
        }
        if (link_dev_tick(&s_dev, now)) {
            outputs_off();   /* fires on the edge only */
        }

        /*
         * Arming is the coprocessor's judgement: the panel asks and this end
         * decides, recomputed every pass from what only this end knows.
         * outputs_arm() is idempotent and does not stamp the clock, so
         * calling it every pass does not keep a channel alive.  Commands are
         * not refreshed here for the same reason: a channel is alive because
         * the host wrote it.
         */
        outputs_arm(&s_outputs,
                    s_state.control[LINK_CT_ARM] != 0
                        && !s_dev.failsafe && s_beat.alive,
                    now);
        /* The edge into driving starts a run on core 1: its peaks and the
         * INA228's totals.  One compare a pass, an order on the edge. */
        const bool driving_now = outputs_driving(&s_outputs);
        if (driving_now && !s_pass_driving) {
            ++s_run_gen;
            sense_order();
            /* And the last run's totals off the BENCH page now, not at the
             * next 50 Hz sample: a panel polling in between would take
             * them, marked TOTALS_OK, as this run's. */
            bench_state_run_starts(&s_bench, s_state.bench);
        }
        s_pass_driving = driving_now;
        /* The sweep's command for this pass, before the step slews to it. */
        (void)servo_page_step(&s_servo, &s_outputs, now);
        /* A capture ends with the run it was timing; one core 1 has
         * finished meanwhile keeps its result, so the page takes core 1's
         * view first. */
        if (!driving_now && sense_capture_open()) {
            sense_sync();
        }
        if (sense_page_step(&s_sense, driving_now)) {
            outputs_hw_watch(-1);
            sense_order();
        }
        /* The supply: its bytes in, a step of its driver, and the output off
         * whenever the panel's heartbeat is not there to switch it off. */
        /* The page first, so a heartbeat lost this pass reaches the driver
         * as an OFF before it steps and can send an ON already queued. */
        supply_page_step(&s_supply, s_beat.alive && !s_dev.failsafe,
                         s_pd_open ? &s_pd : NULL);
        /* A wiring change whose state read has just shown the module off:
         * taken now, before the driver's next transaction, so the module
         * has no time to come on between that read and the rewire. */
        uint16_t wire_next[4];
        if (supply_page_wire_ready(&s_supply, wire_next)) {
            (void)supply_take(LINK_SP_ENABLE, 4u, wire_next, true);
        }
        if (s_pd_open) {
            uint8_t b;
            while (pd_uart_getc(&b)) {
                pdmini_rx(&s_pd, b, now);
            }
            pdmini_step(&s_pd, now);
            /* Under AUTO, the next rate after a WHO_AM_I nothing answered
             * validly -- between transactions, with the pins at rest. */
            const uint8_t rate = supply_page_rate(&s_supply, &s_pd);
            if (rate != s_pd_rate
                && (s_pd.phase == PD_IDLE || s_pd.phase == PD_GAP)) {
                pd_uart_baud(supply_page_baud(rate));
                s_pd_rate = rate;
            }
        }
        outputs_step(&s_outputs, now);
        /* Straight after the step, so what reaches a pin is what the bank
         * has just decided rather than what it decided a pass ago. */
        outputs_hw_service(&s_outputs);
        /* The capture's edge, when this pass rendered it: to core 1, for
         * the capture order in force. */
        uint64_t edge_us;
        if (outputs_hw_edge(&edge_us)) {
            s_edge_us  = edge_us;
            s_edge_gen = s_sense.cap_gen;
            s_edge_set = true;
            sense_order();
        }

        /* 50 Hz, which is faster than the panel polls, so a poll always finds
         * a fresh sample rather than the one it was already shown. */
        if ((uint32_t)(now - last_sample) >= 20u) {
            sample();
            last_sample = now;
        }

        /*
         * A deferred save, once nothing is driving, the writes have stopped
         * and the bus has gone quiet.
         *
         * Writing flash stops this core with interrupts off -- an erase and a
         * program together measured 19,178 us on the bring-up module -- and
         * what that costs is CAN frames: the controller holds two, about
         * 130 us each, and this loop collects none of them while the window is
         * open.  A frame lost there can be the CONTROL write that disarms, and this
         * loop steps no output while the window is open either, so the save
         * waits for a disarm: out_store_tick() is gated on outputs_driving(),
         * which is the bank's armed flag.
         *
         * The heartbeat is not what bounds the window.  The line edges every
         * HEARTBEAT_PERIOD_MS (20 ms) and is sampled by this loop, so an edge
         * inside a 19 ms window is timestamped when the loop resumes rather
         * than lost, and the stretched interval stays far under
         * HEARTBEAT_MAX_GAP_MS (150 ms).  The interval after it can read short
         * instead: under HEARTBEAT_MIN_GAP_MS (4 ms) the monitor rejects it
         * and drops the line until HEARTBEAT_GOOD_RUN (4) good intervals have
         * run, about 80 ms.  That is derived from the periods, not measured on
         * hardware.
         *
         * The save also waits for the pages to stop arriving, so CHAN_CFG and
         * OUTPUTS are saved as the pair they are.  How long since a frame
         * arrived is what keeps the window out of a run of the panel's poll
         * transactions: a minimum quiet time, which says nothing about how
         * much of the gap after it is left.
         */
        const uint32_t quiet = (uint32_t)(now - s_last_rx_ms);
        /* And the supply, asked on or perhaps on: its UART replies would
         * overrun the 8-byte FIFO in a 19 ms window, and an OFF asked over
         * the link would wait for it. */
        const bool driving = outputs_driving(&s_outputs)
                             || s_supply.regs[LINK_SP_OUTPUT] != 0u
                             || (s_pd_open && pdmini_may_be_on(&s_pd));
        const out_store_step_t step = out_store_tick(driving, quiet, now);
        switch (step) {
        case OUT_STORE_WROTE:
            s_supply_unsaved = false;
            if (s_supply_attach) {
                s_supply_attach = false;
                (void)supply_rewire();
            }
            /*
             * Printed because it is the number that decides whether a save
             * costs a frame: this core answers nothing while it writes, the
             * controller holds two frames of about 130 us each, and a request
             * lost there costs the panel 1000 ms of waiting, which is past
             * this end's 200 ms silence failsafe.
             */
            printf("rcbench-iomcu: outputs saved, record %u, "
                   "program window %lu us\n",
                   (unsigned)out_store_last_record(),
                   (unsigned long)out_store_last_program_us());
            break;
        case OUT_STORE_ERASED:
            printf("rcbench-iomcu: output store sector erased, "
                   "window %lu us\n",
                   (unsigned long)out_store_last_erase_us());
            break;
        case OUT_STORE_REFUSED:
            printf("rcbench-iomcu: output store window refused, core 1 did "
                   "not stop within %u ms; tried again in %u ms\n",
                   (unsigned)OUT_STORE_LOCKOUT_MS,
                   (unsigned)OUT_STORE_REFUSED_WAIT_MS);
            break;
        case OUT_STORE_IDLE:
        default:
            break;
        }
        /*
         * And the erase for the save after next, taken now rather than in
         * front of the save that will need it.  Now is the pass after the
         * record that left a sector behind, with the bus quiet: one save in
         * sixteen leaves one, and the other fifteen find nothing to do.
         *
         * Only on a pass that wrote nothing: two windows in one pass would be
         * one long window with a printf in the middle of it, and the pass
         * between them is what empties the receive buffers.
         */
        if (step == OUT_STORE_IDLE && out_store_reclaim(driving, quiet)) {
            printf("rcbench-iomcu: output store sector reclaimed, "
                   "window %lu us\n",
                   (unsigned long)out_store_last_erase_us());
        }

        can_report(now);

        /*
         * The clock is re-read here, once, and this is the only place a pass
         * re-reads it.
         *
         * One clock per pass is the rule and it holds because a pass is
         * short.  Two things in this one are not.  A flash window stops this
         * core for about 19 ms with interrupts off.  can_report() prints to a
         * USB host, which blocks for as long as the host takes.  Everything
         * below measures elapsed time across whichever of them just ran:
         * s_last_rx_ms is what the next pass's quiet-bus guard subtracts
         * from, so a frame collected after one of them and stamped with a
         * clock from before it reports a quiet bus at the moment a frame was
         * handled.  The next page program then passes the 5 ms guard
         * immediately and lands in the same burst of requests, which is the
         * collision the guard exists to prevent.
         *
         * link_dev_tick() has the same reason with a bigger margin: tens of
         * milliseconds against a 200 ms silence timeout.
         *
         * Unconditional rather than after a window only.  A timer read costs
         * nothing beside either of the two, and a condition here is a list of
         * what can block that has to be kept in step with the code above it.
         */
        now = (uint32_t)to_ms_since_boot(get_absolute_time());
        s_now_ms = now;

        /* Straight after the report: printing to a USB host can take
         * milliseconds, and the part holds two frames. */
        can_service(now);
    }
}
