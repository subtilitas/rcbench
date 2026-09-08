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
     * Asked again on every edge into driving rather than once at bind time.
     * Extended telemetry is a runtime setting an ESC forgets when it loses
     * power, and an ESC can be swapped on a bench between one run and the
     * next.
     */
    uint8_t  edt_left;
    bool     edt_asked;
} slot_state_t;

static out_slot_t   s_shadow[OUT_MAX_SLOTS];
static slot_state_t s_state[OUT_MAX_SLOTS];

/*
 * The last readings any bidirectional ESC gave, with the clock each arrived
 * on.  One motor is under test at a time, so one set is the whole of it.
 *
 * Each kind keeps its own clock.  An ESC interleaves the extended frames
 * between eRPM ones, so temperature arrives far less often than speed does,
 * and one age for all of them would either throw away good readings or keep
 * stale ones.
 */
static bool     s_have_erpm;
static uint32_t s_erpm;
static uint32_t s_erpm_ms;

static bool     s_have_edt[DSHOT_TELEM_KINDS];
static uint16_t s_edt[DSHOT_TELEM_KINDS];
static uint32_t s_edt_ms[DSHOT_TELEM_KINDS];

/* Defined with the rendering, which is what decides when a run has ended. */
static void forget_telem(void);

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
         * A slot the silicon cannot serve is left unbound.  The OUTPUTS page
         * still reads back what was asked for, so the disagreement between
         * the page and what is driving is visible from the panel, which is
         * the same way a slot the bank refused already behaves.
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
static void record_telem(const dshot_telem_t *t)
{
    const uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());
    if (t->kind == DSHOT_TELEM_ERPM) {
        s_erpm      = t->erpm;
        s_erpm_ms   = now;
        s_have_erpm = true;
        return;
    }
    if ((unsigned)t->kind < (unsigned)DSHOT_TELEM_KINDS) {
        s_edt[t->kind]      = t->value;
        s_edt_ms[t->kind]   = now;
        s_have_edt[t->kind] = true;
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
    s_have_erpm = false;
    s_erpm      = 0u;
    s_erpm_ms   = 0u;
    memset(s_have_edt, 0, sizeof(s_have_edt));
    memset(s_edt, 0, sizeof(s_edt));
    memset(s_edt_ms, 0, sizeof(s_edt_ms));
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
                          slot_state_t *st, bool drive)
{
    /*
     * The reply to the previous frame is read before the next one is sent.
     * The receiver holds it in its queue until it is taken, and taking it
     * after sending would read it against a frame it did not answer.
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        dshot_telem_t t;
        if (out_dshot_poll(s->pin, st->edt_asked, &t)) {
            record_telem(&t);
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
         */
        st->edt_left  = (uint8_t)DSHOT_CMD_REPEATS;
        st->edt_asked = false;
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
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR && st->edt_left > 0u) {
        out_dshot_send(s->pin, (uint16_t)DSHOT_CMD_EDT_ENABLE, false);
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
     * One question for the whole bank: is it armed and being commanded.
     * Asking it per slot would let one output keep driving on a stale answer
     * while another had already stopped.
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
            service_dshot(o, s, &s_state[i], drive);
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
    if (!s_have_erpm || erpm == NULL || age_ms == NULL) {
        return false;
    }
    *erpm   = s_erpm;
    *age_ms = (uint32_t)(to_ms_since_boot(get_absolute_time()) - s_erpm_ms);
    return true;
}

bool outputs_hw_edt(dshot_telem_kind_t kind, uint16_t *value, uint32_t *age_ms)
{
    if ((unsigned)kind >= (unsigned)DSHOT_TELEM_KINDS || value == NULL
        || age_ms == NULL || !s_have_edt[kind]) {
        return false;
    }
    *value  = s_edt[kind];
    *age_ms = (uint32_t)(to_ms_since_boot(get_absolute_time()) - s_edt_ms[kind]);
    return true;
}
