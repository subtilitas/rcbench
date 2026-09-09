/*
 * The bank rendered onto pins.  See outputs_hw.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_hw.h"

#include <string.h>

#include "pico/stdlib.h"

#include "dshot.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "ppm.h"

/*
 * How often an ESC (electronic speed controller) is told what to do.
 *
 * The OUTPUTS page's rate field is a bit rate for DShot, not a frame rate, so
 * this is the one output timing the wire does not carry.  A kilohertz is
 * twenty times the rate the panel polls at and far inside every ESC's own
 * signal timeout, and for bidirectional DShot it is also the telemetry rate:
 * a thousand speed readings a second against a bench that samples fifty.
 */
#define DSHOT_UPDATE_HZ   1000u
#define DSHOT_PERIOD_US   (1000000u / DSHOT_UPDATE_HZ)

typedef struct {
    bool     bound;
    uint32_t next_us;      /* DShot only: when the next frame is due */
    /*
     * Extended telemetry, which an ESC only sends after it has been asked.
     *
     * edt_left counts the repeats still owed: a command is an ordinary frame
     * and an ESC tells one from a glitch by counting them, so it is sent
     * DSHOT_CMD_REPEATS times before anything else goes out.  Zero with
     * edt_asked set means the asking is done and the replies may carry the
     * other frame types.
     *
     * The two have different lifetimes and that is the point.  edt_left is
     * owed again on every edge into driving, because extended telemetry is a
     * runtime setting an ESC forgets when it loses power and an ESC can be
     * swapped between runs.  edt_asked follows the binding instead: an ESC
     * that keeps power keeps the setting across a disarm, so a run that
     * started by reading replies as periods again would take an interleaved
     * temperature or current frame for a speed.
     */
    uint8_t  edt_left;
    bool     edt_asked;
} slot_state_t;

static out_slot_t   s_shadow[OUT_MAX_SLOTS];
static slot_state_t s_state[OUT_MAX_SLOTS];

/*
 * The last readings an ESC gave, with the clock each arrived on.
 *
 * Per slot, not per bank.  The binding allows eight outputs and any of them
 * may be bidirectional, and one set of readings shared between them would
 * publish a speed from one motor beside a voltage from another -- and a power
 * that is the product of two different ESCs.  Which slot is published is
 * decided in one place, by reporting_slot().
 *
 * Each kind keeps its own clock.  An ESC interleaves the extended frames
 * between eRPM ones, so temperature arrives far less often than speed does,
 * and one age for all of them would either throw away good readings or keep
 * stale ones.
 */
typedef struct {
    bool     have_erpm;
    uint32_t erpm;
    uint32_t erpm_ms;
    bool     have_edt[DSHOT_TELEM_KINDS];
    uint16_t edt[DSHOT_TELEM_KINDS];
    uint32_t edt_ms[DSHOT_TELEM_KINDS];
} telem_t;

static telem_t s_telem[OUT_MAX_SLOTS];

/*
 * The slot whose readings the bench publishes: the lowest-numbered bound
 * bidirectional output.
 *
 * Defined rather than "whichever answered last", so the numbers on the screen
 * belong to one motor and stay with it. A bench running two ESCs reports the
 * first; the page carries one set of readings and choosing between them here
 * is the only place that choice can be made once.
 */
static int reporting_slot(void)
{
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_state[i].bound
            && s_shadow[i].driver == OUT_DRIVER_DSHOT_BIDIR) {
            return (int)i;
        }
    }
    return -1;
}

/* Defined with the rendering, which is what decides when a run has ended. */
static void forget_telem(void);
static void forget_slot_telem(unsigned slot);

void outputs_hw_init(void)
{
    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_state, 0, sizeof(s_state));
    forget_telem();
}

bool outputs_hw_bound(uint8_t slot)
{
    return slot < OUT_MAX_SLOTS && s_state[slot].bound;
}

/* -------------------------------------------------------------- binding */

static bool same(const out_slot_t *a, const out_slot_t *b)
{
    return a->driver == b->driver
           && a->first_channel == b->first_channel
           && a->channels == b->channels
           && a->pin == b->pin
           && a->rate_hz == b->rate_hz;
}

static void unbind(const out_slot_t *s)
{
    switch (s->driver) {
    case OUT_DRIVER_PWM:         out_pwm_release(s->pin);   break;
    case OUT_DRIVER_PPM:         out_ppm_release(s->pin);   break;
    case OUT_DRIVER_DSHOT:
    case OUT_DRIVER_DSHOT_BIDIR: out_dshot_release(s->pin); break;
    case OUT_DRIVER_NONE:
    case OUT_DRIVER_COUNT:
    default:                                                break;
    }
}

static bool bind(const out_slot_t *s)
{
    switch (s->driver) {
    case OUT_DRIVER_PWM:
        return out_pwm_bind(s->pin, s->rate_hz);
    case OUT_DRIVER_PPM:
        return out_ppm_bind(s->pin, s->channels, s->rate_hz);
    case OUT_DRIVER_DSHOT:
        return out_dshot_bind(s->pin, s->rate_hz, false);
    case OUT_DRIVER_DSHOT_BIDIR:
        return out_dshot_bind(s->pin, s->rate_hz, true);
    case OUT_DRIVER_NONE:
    case OUT_DRIVER_COUNT:
    default:
        return false;
    }
}

void outputs_hw_apply(const outputs_t *o)
{
    if (o == NULL) {
        return;
    }
    /*
     * Every slot that moved is released before any is bound.  Two slots
     * exchanging pins is a legal reconfiguration, and binding the first one
     * while the second still held its new pin would find the backend of a
     * slot that is about to go and take it for its own.
     */
    bool moved[OUT_MAX_SLOTS];
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        moved[i] = !(same(&s_shadow[i], &o->slot[i]) && s_state[i].bound);
        if (moved[i]) {
            /* Whatever this slot's ESC said belongs to a binding that is
             * going away, and so does whether that ESC was ever asked for
             * extended telemetry.  Cleared for a slot that was never bound
             * too: a failed bind leaves no output but the readings from
             * before it would otherwise still be published. */
            forget_slot_telem(i);
            s_state[i].edt_asked = false;
        }
        if (moved[i] && s_state[i].bound) {
            unbind(&s_shadow[i]);
            s_state[i].bound = false;
        }
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!moved[i]) {
            continue;
        }
        s_shadow[i] = o->slot[i];
        if (s_shadow[i].driver == OUT_DRIVER_NONE) {
            continue;
        }
        /*
         * A slot the silicon cannot serve is left unbound, which is the same
         * way a slot the bank refused already behaves.  The OUTPUTS page
         * still reads back what was asked for and no register on it says
         * whether a slot is bound, so the panel draws an unbound slot exactly
         * as it draws a driving one and the operator meets it as a lead that
         * does not move.
         */
        s_state[i].bound   = bind(&s_shadow[i]);
        s_state[i].next_us = time_us_32();
        /* A pin that has just been taken has told its ESC nothing yet. */
        s_state[i].edt_left  = (uint8_t)DSHOT_CMD_REPEATS;
        s_state[i].edt_asked = false;
    }
}

/* ------------------------------------------------------------ rendering */

/*
 * One reply, filed under what it turned out to be.
 *
 * Each kind keeps its own arrival clock because they do not arrive at one
 * rate: speed comes back on every frame and the extended kinds are
 * interleaved between them, a few a second at most.
 */
static void record_telem(unsigned slot, const dshot_telem_t *t)
{
    if (slot >= OUT_MAX_SLOTS) {
        return;
    }
    telem_t *c = &s_telem[slot];
    const uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());
    if (t->kind == DSHOT_TELEM_ERPM) {
        c->erpm      = t->erpm;
        c->erpm_ms   = now;
        c->have_erpm = true;
        return;
    }
    if ((unsigned)t->kind < (unsigned)DSHOT_TELEM_KINDS) {
        c->edt[t->kind]      = t->value;
        c->edt_ms[t->kind]   = now;
        c->have_edt[t->kind] = true;
    }
}

/*
 * Drop everything an ESC said.
 *
 * At the end of a run, because the next run need not be the same ESC: one can
 * be unplugged and another fitted between two arms, and a reading that
 * outlived its sender would be published as the new one's for as long as its
 * staleness window lasts -- and would seed that run's peaks on the way past.
 */
static void forget_telem(void)
{
    memset(s_telem, 0, sizeof(s_telem));
}

/* The same, for one slot: what it is bound to has changed, so what its ESC
 * said belongs to an output that is no longer there. */
static void forget_slot_telem(unsigned slot)
{
    if (slot < OUT_MAX_SLOTS) {
        memset(&s_telem[slot], 0, sizeof(s_telem[slot]));
    }
}

static void service_ppm(const outputs_t *o, const out_slot_t *s, bool drive)
{
    if (!drive) {
        out_ppm_stop(s->pin);
        return;
    }
    uint16_t us[PPM_MAX_CHANNELS];
    const uint8_t n = (s->channels > PPM_MAX_CHANNELS)
                          ? (uint8_t)PPM_MAX_CHANNELS : s->channels;
    for (uint8_t c = 0; c < n; ++c) {
        us[c] = outputs_pulse_us(o, (uint8_t)(s->first_channel + c));
    }
    (void)out_ppm_write(s->pin, us, n);
}

static void service_dshot(const outputs_t *o, const out_slot_t *s,
                          slot_state_t *st, unsigned slot, bool drive)
{
    /*
     * The reply to the previous frame is read before the next one is sent.
     * The receiver holds it in its queue until it is taken, and taking it
     * after sending would read it against a frame it did not answer.
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        dshot_telem_t t;
        if (out_dshot_poll(s->pin, st->edt_asked, &t)) {
            record_telem(slot, &t);
        }
    }

    if (!drive) {
        out_dshot_stop(s->pin);
        /*
         * Kept current while nothing is being sent.  The comparison below is
         * wrap-safe over half of the microsecond clock's 71 minutes, so a
         * due time left behind by a bench that sat disarmed for more than 36
         * of them would read as "not due yet" and stay that way for another
         * 36 after it was armed again.
         */
        st->next_us = time_us_32();
        /*
         * And the ask is owed again.  Extended telemetry is a runtime setting
         * an ESC forgets when it loses power, and an ESC can be swapped on a
         * bench between one run and the next; asking again costs ten frames
         * at the start of a run and nothing after that.
         *
         * What is not forgotten is that it was asked.  The setting lives in
         * the ESC, and an ESC that keeps power keeps it across this bench's
         * disarm: reading the next run's first replies as periods would take
         * an interleaved temperature or current frame for a speed, and a
         * current of 120 A decodes as 8,900 rpm -- a number nobody questions,
         * latched into the run's peak.  edt_asked follows the binding, not
         * the run; only a slot that moves or a restart clears it.
         */
        st->edt_left = (uint8_t)DSHOT_CMD_REPEATS;
        return;
    }
    const uint32_t now = time_us_32();
    if ((uint32_t)(now - st->next_us) >= 0x80000000u) {
        return;                              /* not due yet, wrap-safe */
    }
    st->next_us = now + DSHOT_PERIOD_US;

    /*
     * The ask goes first, before any throttle.  A command is an ordinary
     * frame and an ESC tells one from a glitch by counting repeats, so this
     * takes the first DSHOT_CMD_REPEATS frames of a run: ten at
     * DSHOT_UPDATE_HZ, which is 10 ms.  DSHOT_CMD_EDT_ENABLE is in the
     * command range, so nothing turns while it is being sent, and a throttle
     * the operator has already asked for arrives 10 ms later than it would
     * have.
     *
     * An ESC that does not know the command ignores it and keeps sending
     * periods.  Those still read as periods: this decoder only takes a frame
     * for an extended one when the mantissa's top bit is clear and the type
     * nibble is not zero, which an ESC that normalises its exponent never
     * sends.
     *
     * The telemetry bit is set on every one of the repeats, and it is not
     * decoration.  On a value of 1 to 47 the bit is what marks the frame as
     * a command: the BLHeli_S family (Bluejay) discards a command whose
     * telemetry bit is clear and zeroes its repeat counter with it, so ten
     * frames without the bit never reach the six repeats its command handler
     * counts and extended telemetry never comes on.  AM32 has no such gate
     * and takes the command either way.  On a bidirectional pin the word is
     * 0x01B5 with the bit and 0x01A4 without it -- a different payload and a
     * different checksum nibble.  The bit's other meaning is a request on
     * the separate serial telemetry wire; nothing on this bench reads that
     * wire.
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR && st->edt_left > 0u) {
        out_dshot_send(s->pin, (uint16_t)DSHOT_CMD_EDT_ENABLE, true);
        if (--st->edt_left == 0u) {
            st->edt_asked = true;
        }
        return;
    }

    const uint16_t command = outputs_actual(o, s->first_channel);
    out_dshot_send(s->pin, dshot_throttle(command, OUT_SPAN), false);
}

void outputs_hw_service(const outputs_t *o)
{
    if (o == NULL) {
        return;
    }
    /*
     * One question for the whole bank: is it armed.  Asking it per slot would
     * let one output keep driving on a stale answer while another had already
     * stopped.  Whether a channel is still being commanded is a separate
     * question, per channel, and it is not asked here: what reaches a pin is
     * outputs_actual(), whatever last wrote it.  The main loop calls
     * outputs_step() immediately before this, so an overdue channel is at its
     * rest by then, and what goes on the pin is that rest rather than nothing.
     * outputs_off() calls this with no step in front of it and needs none: it
     * disarms first, which puts every channel at its rest and makes drive
     * false.
     */
    const bool drive = outputs_driving(o);

    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!s_state[i].bound) {
            continue;
        }
        const out_slot_t *s = &s_shadow[i];
        switch (s->driver) {
        case OUT_DRIVER_PWM:
            out_pwm_write(s->pin,
                          drive ? outputs_pulse_us(o, s->first_channel) : 0u);
            break;
        case OUT_DRIVER_PPM:
            service_ppm(o, s, drive);
            break;
        case OUT_DRIVER_DSHOT:
        case OUT_DRIVER_DSHOT_BIDIR:
            service_dshot(o, s, &s_state[i], i, drive);
            break;
        case OUT_DRIVER_NONE:
        case OUT_DRIVER_COUNT:
        default:
            break;
        }
    }

    /*
     * The end of a run, and after the loop rather than before it: a
     * bidirectional slot reads the reply to its last frame on the way to
     * stopping, so a cache cleared first is repopulated by that reply with a
     * fresh timestamp and the run's final reading outlives the run.
     *
     * Asked once for the bank rather than per slot: the cache is one motor's
     * and the slots share it, so a slot that happened not to be driving must
     * not clear what another one just heard.
     */
    static bool s_was_driving;
    if (s_was_driving && !drive) {
        forget_telem();
    }
    s_was_driving = drive;
}

bool outputs_hw_erpm(uint32_t *erpm, uint32_t *age_ms)
{
    const int slot = reporting_slot();
    if (slot < 0 || erpm == NULL || age_ms == NULL
        || !s_telem[slot].have_erpm) {
        return false;
    }
    *erpm   = s_telem[slot].erpm;
    *age_ms = (uint32_t)(to_ms_since_boot(get_absolute_time())
                         - s_telem[slot].erpm_ms);
    return true;
}

bool outputs_hw_edt(dshot_telem_kind_t kind, uint16_t *value, uint32_t *age_ms)
{
    const int slot = reporting_slot();
    if (slot < 0 || (unsigned)kind >= (unsigned)DSHOT_TELEM_KINDS
        || value == NULL || age_ms == NULL
        || !s_telem[slot].have_edt[kind]) {
        return false;
    }
    *value  = s_telem[slot].edt[kind];
    *age_ms = (uint32_t)(to_ms_since_boot(get_absolute_time())
                         - s_telem[slot].edt_ms[kind]);
    return true;
}
