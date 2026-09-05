/*
 * The coprocessor application.
 *
 * Answers polls and never transmits unsolicited: every transmission here
 * follows a decoded request.
 *
 * The measurement front end and the power path are not written.  This file
 * holds the wire, the failsafe and the output bank; the output protocols are
 * in out_pwm.c, out_ppm.c and out_dshot.c behind outputs_hw.c.
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
#include "link_dev.h"
#include "link_pages.h"
#include "dshot.h"
#include "out_bind.h"
#include "rcbench_version.h"
#include "out_store.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "outputs_pages.h"
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
    memcpy(cfg.slots, s->slots, sizeof(cfg.slots));
    memcpy(cfg.chan_cfg, s->chan_cfg, sizeof(cfg.chan_cfg));
    out_store_save(&cfg, (uint32_t)to_ms_since_boot(get_absolute_time()));
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
    outputs_channels_apply(&s_outputs, s->channels,
                           (uint32_t)to_ms_since_boot(get_absolute_time()));
    return 0u;
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
    const uint8_t nack = outputs_chan_cfg_write(s->chan_cfg, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    outputs_chan_cfg_apply(&s_outputs, s->chan_cfg);
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

static uint8_t slots_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    const uint8_t nack = outputs_slots_write(s->slots, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    outputs_slots_apply(&s_outputs, s->slots);
    /* The bank has decided what the slots are; this makes the silicon agree
     * with it before the next pass renders anything. */
    outputs_hw_apply(&s_outputs);
    save_outputs(s);
    return 0u;
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    iomcu_state_t *s = (iomcu_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_THROTTLE && in[i] > LINK_THROTTLE_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_CT_CLEAR) {
            if (in[i] != LINK_CLEAR_MAGIC) {
                return LINK_NACK_BAD_VALUE;
            }
            /*
             * The clock of this pass, as recorded by the dispatcher, not a
             * fresh read.  A fresh read is later than the `now` that
             * link_dev_tick() receives a few lines further on, and the
             * wrap-safe comparison there reads a timestamp in the future as
             * 4,294,967,295 ms of silence, which re-arms the failsafe
             * immediately.
             */
            link_dev_clear_failsafe(&s_dev, s_dev.last_request_ms);
            continue;
        }
        /* Refusing to arm while in failsafe is the coprocessor's decision:
         * the panel is not the authority on whether it is safe here. */
        if (reg == LINK_CT_ARM && in[i] != 0
            && (s_dev.failsafe || !s_beat.alive)) {
            return LINK_NACK_NOT_ARMED;
        }
        /* Zero means nobody has said; anything else is an even count in the
         * range a motor comes in.  An odd count is a typo, and accepting one
         * would put a plausible wrong speed on the screen. */
        if (reg == LINK_CT_MOTOR_POLES && in[i] != 0u
            && (in[i] < LINK_POLES_MIN || in[i] > LINK_POLES_MAX
                || (in[i] % 2u) != 0u)) {
            return LINK_NACK_BAD_VALUE;
        }
        s->control[reg] = in[i];
    }
    /*
     * The throttle rides the control page rather than the CHANNELS page, so
     * a motor command keeps control priority on the wire; underneath it is
     * the same bank, so it is set here the same way a channel is.
     */
    (void)outputs_set(&s_outputs, CH_THROTTLE,
                      (uint16_t)(((uint32_t)s->control[LINK_CT_THROTTLE]
                                  * OUT_SPAN) / LINK_THROTTLE_MAX),
                      (uint32_t)to_ms_since_boot(get_absolute_time()));
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
 * A photograph of this board, if this build carries one.
 *
 * None yet: the artwork is a generated C array and the tool that generates
 * it does not exist.  The pages are here so the mechanism is whole and the
 * panel's answer to "this board has no picture" is exercised rather than
 * assumed -- LINK_AW_BLOCKS of zero is the ordinary reply, not a fault, and
 * a board with no photograph is drawn from its shape page instead.
 *
 * When there is one it is a const array in this binary rather than anything
 * in the flash store: no erase window, no wear, and its version is the
 * firmware version.  It has to sit inside the same conservative four
 * megabytes out_store.c pins itself to, for the same reason.
 */
static const uint8_t *const k_art       = NULL;
static const uint32_t       k_art_bytes = 0u;
static const uint16_t       k_art_w     = 0u;
static const uint16_t       k_art_h     = 0u;
static const uint16_t       k_art_crc   = 0u;

static void artwork_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    uint16_t all[LINK_AW_COUNT];
    for (unsigned i = 0; i < LINK_AW_COUNT; ++i) { all[i] = 0u; }
    if (k_art != NULL && k_art_bytes != 0u) {
        const uint32_t blocks =
            (k_art_bytes + LINK_AD_BYTES - 1u) / LINK_AD_BYTES;
        all[LINK_AW_BLOCKS]   = (uint16_t)blocks;
        all[LINK_AW_WIDTH]    = k_art_w;
        all[LINK_AW_HEIGHT]   = k_art_h;
        all[LINK_AW_FORMAT]   = (uint16_t)LINK_ART_RGB565;
        all[LINK_AW_BYTES_LO] = (uint16_t)(k_art_bytes & 0xFFFFu);
        all[LINK_AW_BYTES_HI] = (uint16_t)(k_art_bytes >> 16);
        all[LINK_AW_CRC]      = k_art_crc;
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
    if (k_art != NULL) {
        const uint32_t at = (uint32_t)s_art_block * LINK_AD_BYTES;
        for (unsigned i = 0; i < LINK_AD_BYTES && at + i < k_art_bytes; ++i) {
            const uint16_t b = k_art[at + i];
            /* Low byte first, the order the framebuffer is already in. */
            all[LINK_AD_DATA + i / 2u] |=
                (uint16_t)((i & 1u) ? (uint16_t)(b << 8) : b);
        }
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

    if (!s_can_up) {
        printf("rcbench-iomcu: CAN did not answer on SPI -- module fitted? "
               "wiring on GP9-12?\n");
        return;
    }
    uint8_t tec = 0, rec = 0, eflg = 0;
    xl2515_errors(&tec, &rec, &eflg);
    printf("rcbench-iomcu: CAN up, %u bit/s, %lu echoes served, "
           "tx_err %u rx_err %u eflg 0x%02X\n",
           (unsigned)IOMCU_CAN_BITRATE, (unsigned long)s_can_echoes,
           tec, rec, eflg);
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
}

/* ---------------------------------------------------------------- the loop */

/*
 * The numbers, and only the ones something measured.
 *
 * There is no measurement front end, so voltage, current and temperature are
 * zero with their valid bits clear and the panel draws those fields empty.
 * The one quantity that has a source is speed, from a bidirectional DShot
 * ESC (electronic speed controller) answering on its own signal line.
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

    /* On the edge into driving, so a run's peaks are that run's.  The reset
     * takes the current reading rather than zero, which is what stops a sag
     * floor of 0 V reading as a collapsed pack. */
    const bool driving = outputs_driving(&s_outputs);
    if (driving && !s_was_driving) {
        bench_state_reset_peaks(&s_bench);
    }
    s_was_driving = driving;

    if (s_bench.rpm > s_bench.rpm_max) {
        s_bench.rpm_max = s_bench.rpm;
    }
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
 * clear the pages so what the panel reads back agrees with what the outputs
 * are doing.
 */
static void outputs_off(void)
{
    outputs_arm(&s_outputs,
                false,
                (uint32_t)to_ms_since_boot(get_absolute_time()));

    s_state.control[LINK_CT_ARM]      = 0;
    s_state.control[LINK_CT_THROTTLE] = 0;
    /* The channels page is what a read shows the panel; the bank is already at
     * rest from the disarm above, so the two agree only if this agrees too. */
    outputs_channels_defaults(s_state.channels);
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
     * page's valid bits are where that shows.
     */
    s_state.identity[LINK_ID_CAPABILITIES] =
        (uint16_t)(LINK_CAP_SERVO_PWM | LINK_CAP_ESC_DRIVE
                   | LINK_CAP_ESC_TELEM);

    /* A range before anybody sets one, so the clamp is meaningful from the
     * first frame rather than from the first configuration. */
    outputs_channels_defaults(s_state.channels);
    outputs_chan_cfg_defaults(s_state.chan_cfg);
    outputs_slots_defaults(s_state.slots);

    /*
     * Then what was saved, over the defaults.  This configures the outputs;
     * it does not drive them.  Every driver is gated by outputs_driving(),
     * which wants the bench armed, the heartbeat trusted and a command
     * arriving, so a restored binding claims its pins and holds them at idle
     * until somebody arms.  The channels are not restored: a command is not
     * a configuration, and a bench that came back holding the last throttle
     * it was given is exactly what must not happen.
     */
    out_store_t saved;
    if (out_store_load(&saved)) {
        memcpy(s_state.slots, saved.slots, sizeof(s_state.slots));
        memcpy(s_state.chan_cfg, saved.chan_cfg, sizeof(s_state.chan_cfg));
    }

    const uint32_t now0 = (uint32_t)to_ms_since_boot(get_absolute_time());
    outputs_init(&s_outputs, now0);
    /*
     * The pins this build will not hand out, whatever the host asks for: the
     * safety line, the CAN (Controller Area Network) controller's four SPI
     * (Serial Peripheral Interface) pins and its interrupt, and every number
     * above the last GPIO (general-purpose input/output) this part has.  The
     * pin arrives from the panel over the OUTPUTS page, so it is whatever an
     * operator typed, and an output bound to the heartbeat input is an
     * interlock that stops working with nothing to show for it.
     *
     * The union of what shared/outputs greys out on the panel and what this
     * file assigns, so the two disagreeing costs a pin rather than the safety
     * line.
     */
    outputs_reserve_pins(&s_outputs,
                         outbind_reserved_mask(IOMCU_BOARD_ID)
                             | IOMCU_RESERVED_PINS | IOMCU_ABSENT_PINS);
    (void)outputs_set_role(&s_outputs, CH_THROTTLE, OUT_ROLE_THROTTLE);
    outputs_chan_cfg_apply(&s_outputs, s_state.chan_cfg);
    outputs_slots_apply(&s_outputs, s_state.slots);
    outputs_channels_apply(&s_outputs, s_state.channels, now0);
    outputs_hw_init();
    outputs_hw_apply(&s_outputs);
    link_dev_init(&s_dev, k_pages, count_of(k_pages), &s_state, now0);

    heartbeat_init();
    can_start();
    memset(&s_bench, 0, sizeof(s_bench));

    uint32_t last_sample = (uint32_t)to_ms_since_boot(get_absolute_time());

    for (;;) {
        /*
         * ONE CLOCK PER PASS, used by everything below.  Every timeout here
         * is a wrap-safe unsigned subtraction, so a timestamp 1 ms ahead of
         * the `now` it is compared against reads as 4,294,967,295 ms of
         * silence, past every timeout there is.
         */
        const uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());

        /* Polled rather than interrupt-driven: the loop turns over far faster
         * than a frame takes to arrive, and the failsafe has to fire on time
         * whether or not anything is arriving. */
        can_service(now);

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
        outputs_step(&s_outputs, now);
        /* Straight after the step, so what reaches a pin is what the bank
         * has just decided rather than what it decided a pass ago. */
        outputs_hw_service(&s_outputs);

        /* 50 Hz, which is faster than the panel polls, so a poll always finds
         * a fresh sample rather than the one it was already shown. */
        if ((uint32_t)(now - last_sample) >= 20u) {
            sample();
            last_sample = now;
        }

        /*
         * Two independent watchdogs.  The link watchdog says the panel has
         * stopped talking; this one says the panel has stopped running.  A
         * panel wedged mid-frame can still have an interrupt answering polls,
         * so the link watchdog alone would not fire.
         */
        const bool was_beating = s_beat.alive;
        if (!heartbeat_poll(now) && was_beating) {
            outputs_off();   /* fires on the edge only */
        }

        /*
         * A deferred save, once nothing is driving and the writes have
         * stopped.  Writing flash stops this core with interrupts off for
         * longer than the heartbeat's window, so it cannot happen while an
         * output is live; the monitor loses its edges across the write and
         * has to re-acquire, which is why it waits for the bench to be idle
         * rather than merely disarmed.  It also waits for the pages to stop
         * arriving, so CHAN_CFG and OUTPUTS are saved as the pair they are.
         */
        (void)out_store_tick(outputs_driving(&s_outputs), now);

        can_report(now);
        /* Again straight after the report: printing to a USB host can take
         * milliseconds, and the part holds two frames. */
        can_service(now);

        if (link_dev_tick(&s_dev, now)) {
            outputs_off();   /* fires on the edge only */
        }
    }
}
