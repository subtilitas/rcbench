/*
 * The coprocessor application.
 *
 * Answer polls, and never speak first.  That is the whole loop, and the
 * property is structural rather than disciplined: there is no code path here
 * that transmits without having decoded a request.
 *
 * The measurement front end, the PIO programs and the power path land on top
 * of this.  What is here is the part that has to be right before any of them
 * can be trusted: the wire, the failsafe, and the refusal to invent traffic.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "copro_pins.h"
#include "can_selftest.h"
#include "heartbeat.h"
#include "link_dev.h"
#include "link_pages.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "telemetry_sim.h"
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
} copro_state_t;

static copro_state_t s_state;
static link_dev_t    s_dev;

/*
 * Every output, behind one set of rules.
 *
 * The pages above are still the wire format; what they are no longer is the
 * place the rules live.  The rules were written once, in the bench's throttle
 * model, and nothing here used them: the servo page answered the same
 * questions again and answered two of them differently, and skipped the
 * silence timeout entirely.  The failsafe below showed the same shape, having
 * been written before the servo page existed.
 *
 * The output pages address bank channels 0..LINK_OUT_CHANNELS-1 directly.
 * The throttle is not one of them: it lives above that range so the control
 * page can command it without colliding with a channels-page write, and so a
 * motor keeps the control page's priority on the wire.
 */
static outputs_t s_outputs;

#define CH_THROTTLE  LINK_OUT_CHANNELS   /* bank channel 8, off the page */

/* The panel's safety line, as judged in firmware.  Declared here with the
 * other state because the control page consults it before it will arm --
 * the reasoning is with heartbeat_init() below. */
static heartbeat_mon_t s_beat;

/*
 * Requests this end has answered, published on the STATUS page.  Those
 * registers have been in the page map since it was written and nothing ever
 * filled them -- three that read as zero and look like data, which is worse
 * than three that are not there.
 */
static uint32_t s_frames;

static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->identity[off + i];
    }
}

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->control[off + i];
    }
}

static void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
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
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->bench[off + i];
    }
}

/*
 * The output pages.
 *
 * Three pages, one bank.  The rules -- clamp, arm, slew, refuse an impossible
 * range, refuse an unknown driver -- live in shared/outputs so this end and
 * the host end cannot hold different opinions; what the coprocessor supplies
 * is the one thing only it knows, which is when it is safe to drive at all,
 * and it supplies that by arming the bank rather than by gating each write.
 *
 * A read returns the stored register array; a write validates and stores,
 * then re-derives the bank from the whole page so a partial write composes.
 */
static void channels_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->channels[off + i];
    }
}

static uint8_t channels_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    copro_state_t *s = (copro_state_t *)ctx;
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
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->chan_cfg[off + i];
    }
}

static uint8_t chan_cfg_write(void *ctx, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    copro_state_t *s = (copro_state_t *)ctx;
    const uint8_t nack = outputs_chan_cfg_write(s->chan_cfg, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    outputs_chan_cfg_apply(&s_outputs, s->chan_cfg);
    return 0u;
}

static void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->slots[off + i];
    }
}

static uint8_t slots_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    copro_state_t *s = (copro_state_t *)ctx;
    const uint8_t nack = outputs_slots_write(s->slots, off, n, in);
    if (nack != 0u) {
        return nack;
    }
    outputs_slots_apply(&s_outputs, s->slots);
    return 0u;
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    copro_state_t *s = (copro_state_t *)ctx;
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
             * The clock this pass is using, which the dispatcher has already
             * recorded -- not a fresh read.  A fresh one would be later than
             * the `now` that link_dev_tick is given a few lines further on,
             * and the wrap-safe comparison there would read a timestamp in the
             * future as forty-nine days of silence.  Clearing the failsafe
             * would have re-armed it on the spot.
             */
            link_dev_clear_failsafe(&s_dev, s_dev.last_request_ms);
            continue;
        }
        /* Refusing to arm while in failsafe is the coprocessor's own decision
         * to make: the panel is not the authority on whether it is safe here. */
        if (reg == LINK_CT_ARM && in[i] != 0
            && (s_dev.failsafe || !s_beat.alive)) {
            return LINK_NACK_NOT_ARMED;
        }
        s->control[reg] = in[i];
    }
    /*
     * Stamped here rather than in the loop, so the silence timeout measures
     * what it is for: how long since the *host* last said anything.
     */
    /*
     * The throttle rides the control page rather than the channels page, so
     * a motor command keeps control priority on the wire -- but it is the same
     * bank underneath, so it is set here the same way a channel is.
     */
    (void)outputs_set(&s_outputs, CH_THROTTLE,
                      (uint16_t)(((uint32_t)s->control[LINK_CT_THROTTLE]
                                  * OUT_SPAN) / LINK_THROTTLE_MAX),
                      (uint32_t)to_ms_since_boot(get_absolute_time()));
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_STATUS,   LINK_ST_COUNT, status_read,   NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_CHANNELS, LINK_CH_COUNT, channels_read, channels_write },
    { LINK_PAGE_BENCH,    LINK_BN_COUNT, bench_read,    NULL },
    { LINK_PAGE_OUTPUTS,  LINK_OS_COUNT, slots_read,    slots_write },
    { LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, chan_cfg_read, chan_cfg_write },
};

/* ------------------------------------------------------------ the heartbeat */

/*
 * The panel's safety line, watched in firmware as well as in hardware.
 *
 * The retriggerable monostable on the daughterboard is the backstop: it holds
 * the output enable up only while edges keep arriving, and it does that with
 * no software involved, which is exactly what a backstop should be.  What it
 * cannot do is tell a heartbeat from noise.  Anything that edges fast enough
 * retriggers it -- a ringing line, a short to a clock, a floating input next
 * to a switching supply -- and it would hold the outputs enabled through all
 * of them.  heartbeat_mon_t knows what period to expect and rejects what
 * cannot be a 39 Hz render loop.
 *
 * The pin is sampled in the main loop rather than through an interrupt.  That
 * used to be justified by a blocking UART read bounding the loop at a
 * millisecond; there is no blocking call left, so the loop now runs as fast as
 * the processor allows and the bound is far tighter than it was.  What has to
 * stay true is that a sample cannot miss an edge the monitor would have
 * accepted -- the floor is HEARTBEAT_MIN_GAP_MS, and the slowest thing in this
 * loop is a printf every three seconds, which is the one place worth watching
 * if that ever stops holding.  An ISR would notice edges faster than the
 * floor, which is not useful: those are the ones being rejected anyway.
 */
static bool s_beat_level;

static void heartbeat_init(void)
{
    gpio_init(COPRO_HEARTBEAT_PIN);
    gpio_set_dir(COPRO_HEARTBEAT_PIN, GPIO_IN);
    /* Pulled down, so an unplugged or unpowered panel reads as a line that is
     * not edging rather than as one held high. */
    gpio_pull_down(COPRO_HEARTBEAT_PIN);
    heartbeat_mon_init(&s_beat);
    s_beat_level = gpio_get(COPRO_HEARTBEAT_PIN);
}

/** Sample the line; returns whether it may currently be believed. */
static bool heartbeat_poll(uint32_t now)
{
    const bool level = gpio_get(COPRO_HEARTBEAT_PIN);
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
 * The controller either answers on SPI or it does not, and the datasheet
 * guarantees which mode it wakes in -- so this distinguishes "no module
 * fitted" and "SPI miswired" from anything to do with the CAN bus itself,
 * before a scope comes out.
 *
 * The echo responder then runs unconditionally.  It costs one register read
 * per loop, answers only frames addressed to a page the map does not use, and
 * runs at the lowest priority on the bus -- so it can be left in and cannot
 * get in the way of anything.  Leaving it in is the point: the bring-up tool
 * that is already flashed is the one that gets used.
 */
static bool     s_can_up;
static uint32_t s_can_echoes;
static uint32_t s_can_overflows;

static void can_start(void)
{
    s_can_up = xl2515_init(COPRO_CAN_BITRATE);
    printf("rcbench-copro: CAN %s at %u bit/s\n",
           s_can_up ? "up" : "DID NOT ANSWER (module fitted? SPI wiring?)",
           (unsigned)COPRO_CAN_BITRATE);
}

/*
 * One clock for the whole pass, handed in.
 *
 * This read its own clock and the loop read another, so the dispatcher could
 * stamp last_request_ms a millisecond *later* than the `now` that link_dev_tick
 * was then given.  The comparison is wrap-safe unsigned subtraction, so a
 * timestamp one millisecond in the future reads as 4,294,967,295 ms of silence
 * -- past every timeout there is.  The failsafe fired on the very requests
 * that proved the link was alive.
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
         * The far end asking what this end can see.  Answering it is what
         * lets one console show both halves of a fault instead of a USB
         * cable being moved to find the other half.
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
         * And everything else is the link itself: a page request, answered by
         * the same dispatcher that answered them over the old byte transport.
         * It never knew what carried it, which is why that transport could be
         * deleted rather than adapted.
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
             * has to wait for each to win arbitration.  Bounded, because a bus
             * that has stopped accepting must not stall the failsafe.
             */
            for (int spin = 0; spin < 1000 && !xl2515_send(&frames[i]); ++spin) {
                tight_loop_contents();
            }
        }
    }
}

/*
 * Repeated rather than printed once at boot.
 *
 * USB CDC does not exist until a host enumerates it, so anything printed
 * before the terminal is opened is gone -- and a boot message you have to
 * catch by power-cycling at the right moment is a boot message nobody reads.
 * Saying it every few seconds costs nothing and means the answer is on screen
 * whenever somebody looks.
 */
static void can_report(uint32_t now)
{
    static uint32_t last;
    if (now - last < 3000u && last != 0u) {
        return;
    }
    last = now;

    if (!s_can_up) {
        printf("rcbench-copro: CAN did not answer on SPI -- module fitted? "
               "wiring on GP9-12?\n");
        return;
    }
    uint8_t tec = 0, rec = 0, eflg = 0;
    xl2515_errors(&tec, &rec, &eflg);
    printf("rcbench-copro: CAN up, %u bit/s, %lu echoes served, "
           "tx_err %u rx_err %u eflg 0x%02X\n",
           (unsigned)COPRO_CAN_BITRATE, (unsigned long)s_can_echoes,
           tec, rec, eflg);
    /*
     * Said in words rather than left in a hex code, because it is the one
     * thing here that explains a frame going missing while the bus reports no
     * error at all: it arrived, it was correct, and both buffers were full.
     * The part has two, so anything that stops this loop for two frame times
     * costs a frame -- and a printf to a USB host that has stopped reading is
     * exactly such a thing.
     */
    if (s_can_overflows > 0u) {
        printf("rcbench-copro: CAN receive buffers overran %lu time(s) -- "
               "frames arrived with nowhere to put them; not a bus fault\n",
               (unsigned long)s_can_overflows);
    }
}

/* ---------------------------------------------------------------- the loop */

/*
 * With no measurement front end fitted there is nothing to read, so the
 * numbers are modelled here -- and every one of them carries
 * LINK_BN_SIMULATED, which travels the wire and puts SIMULATION across the
 * panel's screen.  A remote fake declares itself; it is not assumed honest.
 */
static telemetry_sim_t s_sim;
static bench_state_t   s_bench;



static void sample(float dt_s)
{
    /*
     * What the output is doing, not what was asked for.  The bank has already
     * applied arming, slew and the silence timeout, and a simulation fed the
     * raw request would show a motor at a speed the outputs are refusing to
     * produce -- which is the invented-versus-measured mistake with the
     * simulation's name on it.
     */
    const float throttle = (float)outputs_actual(&s_outputs, CH_THROTTLE)
                           * 100.0f / (float)OUT_SPAN;
    telemetry_sim_step(&s_sim, throttle, dt_s, &s_bench);
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
     * The CRC and resync counters belonged to a byte transport that had to
     * find its own frame boundaries.  CAN finds them in silicon and checks
     * them there, so what used to be counted here is now the controller's
     * error registers -- reported over the bus itself, and zero here.
     */
    uint8_t tec = 0, rec = 0;
    xl2515_errors(&tec, &rec, NULL);
    s_state.status[LINK_ST_CRC_ERRORS] = rec;
    s_state.status[LINK_ST_RESYNCS]    = tec;
}

/*
 * The failsafe edge.
 *
 * This is what the intermediary is for.  The old version of this function
 * cleared the control page and said so in a comment -- "written and reachable
 * now rather than added once there is something to forget to add it to" -- and
 * then the servo page was added and nobody added it here.  It was latent only
 * because nothing yet puts an edge on a pin.
 *
 * Disarming the bank is now the whole of it, because there is one bank.  The
 * pages are cleared afterwards so that what the panel reads back agrees with
 * what the outputs are doing; a page still saying ENABLE over a servo that
 * has stopped is the same lie in the other direction.
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
}

int main(void)
{
    stdio_init_all();

    s_state.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    s_state.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    /*
     * Nothing yet, and saying so is the point.
     *
     * Every bit here is a thing that has to be soldered on: PWM to a servo
     * lead, a shunt, a PIO receiver, an accelerometer.  The board has the
     * pins and the firmware has the code for none of it, so it reports
     * nothing and the panel greys the menu accordingly.  When a part goes on,
     * one bit goes in here and the whole interface stops apologising for it.
     */
    s_state.identity[LINK_ID_CAPABILITIES]   = 0;

    /* A range before anybody sets one, so the clamp is meaningful from the
     * first frame rather than from the first configuration. */
    outputs_channels_defaults(s_state.channels);
    outputs_chan_cfg_defaults(s_state.chan_cfg);
    outputs_slots_defaults(s_state.slots);

    const uint32_t now0 = (uint32_t)to_ms_since_boot(get_absolute_time());
    outputs_init(&s_outputs, now0);
    (void)outputs_set_role(&s_outputs, CH_THROTTLE, OUT_ROLE_THROTTLE);
    outputs_chan_cfg_apply(&s_outputs, s_state.chan_cfg);
    outputs_slots_apply(&s_outputs, s_state.slots);
    outputs_channels_apply(&s_outputs, s_state.channels, now0);
    link_dev_init(&s_dev, k_pages, count_of(k_pages), &s_state, now0);

    heartbeat_init();
    can_start();
    telemetry_sim_init(&s_sim, NULL);
    memset(&s_bench, 0, sizeof(s_bench));

    uint32_t last_sample = (uint32_t)to_ms_since_boot(get_absolute_time());

    for (;;) {
        /*
         * ONE CLOCK PER PASS, used by everything below.
         *
         * Not a style preference.  Every timeout here is a wrap-safe unsigned
         * subtraction, so a timestamp even a millisecond ahead of the `now` it
         * is later compared against reads as 4,294,967,295 ms of silence --
         * past every timeout there is.  Reading the clock twice in one pass is
         * enough to make the failsafe fire on the very request that proved the
         * link was alive.
         */
        const uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());

        /* Polled rather than interrupt-driven: the loop turns over far faster
         * than a frame takes to arrive, and the failsafe has to fire on time
         * whether or not anything is arriving. */
        can_service(now);

        /*
         * Arming is the coprocessor's judgement -- the panel asks and this
         * decides -- so it is recomputed every pass from what only this end
         * knows, rather than remembered from when the write landed.  The call
         * is idempotent, which is what makes that safe: stamping the clock
         * here every pass would hold every channel alive and delete the
         * timeout the pass after it was added.
         *
         * Commands are *not* refreshed here, for the same reason.  A channel
         * is alive because the host wrote it, and re-commanding it out of the
         * coprocessor's own register would be the coprocessor keeping itself
         * company.
         */
        outputs_arm(&s_outputs,
                    s_state.control[LINK_CT_ARM] != 0
                        && !s_dev.failsafe && s_beat.alive,
                    now);
        outputs_step(&s_outputs, now);

        /* 50 Hz, which is faster than the panel polls, so a poll always finds
         * a fresh sample rather than the one it was already shown. */
        if ((uint32_t)(now - last_sample) >= 20u) {
            sample((float)(now - last_sample) / 1000.0f);
            last_sample = now;
        }

        /*
         * Two independent watchdogs, deliberately not folded together.  The
         * link one says the panel has stopped talking; this one says the
         * panel has stopped *running*.  A panel that is wedged mid-frame can
         * still have a UART interrupt answering polls, so the link watchdog
         * alone would never fire -- which is the failure this line exists for.
         */
        const bool was_beating = s_beat.alive;
        if (!heartbeat_poll(now) && was_beating) {
            outputs_off();   /* the edge, once */
        }

        can_report(now);
        /* Again straight after the report: printing to a USB host can take
         * milliseconds, and the part holds two frames. */
        can_service(now);

        if (link_dev_tick(&s_dev, now)) {
            outputs_off();   /* the edge, once */
        }
    }
}
