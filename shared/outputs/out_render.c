/**
 * @file
 * @brief The output bank rendered onto pins.
 *
 * See out_render.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_render.h"

#include <string.h>

#include "out_pwm_map.h"

/* ------------------------------------------------------------- telemetry */

/**
 * @brief One reply, filed under what it turned out to be.
 *
 * Each kind keeps its own arrival clock because they do not arrive at one
 * rate: speed comes back on every frame and the extended kinds are
 * interleaved between them, a few a second at most.
 *
 * @param r The renderer.
 * @param slot The slot the reply came from. Ignored when out of range.
 * @param t The reply.
 */
static void record_telem(out_render_t *r, unsigned slot,
                         const dshot_telem_t *t)
{
    if (slot >= OUT_MAX_SLOTS) {
        return;
    }
    out_render_telem_t *c = &r->telem[slot];
    const uint32_t now = out_render_hw_ms();
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

/**
 * @brief Drop everything every ESC said.
 *
 * At the end of a run, because the next run need not be the same ESC: one can
 * be unplugged and another fitted between two arms, and a reading that
 * outlived its sender would be published as the new one's for as long as its
 * staleness window lasts, and would seed that run's peaks on the way past.
 *
 * @param r The renderer.
 */
static void forget_telem(out_render_t *r)
{
    memset(r->telem, 0, sizeof(r->telem));
}

/**
 * @brief Drop what one slot's ESC said: what the slot is bound to has
 *        changed, so the readings belong to an output that is gone.
 *
 * @param r The renderer.
 * @param slot The slot. Ignored when out of range.
 */
static void forget_slot_telem(out_render_t *r, unsigned slot)
{
    if (slot < OUT_MAX_SLOTS) {
        memset(&r->telem[slot], 0, sizeof(r->telem[slot]));
    }
}

int out_render_reporting_slot(const out_render_t *r)
{
    /*
     * Defined rather than "whichever answered last", so the numbers on the
     * screen belong to one motor and stay with it.  A bench running two ESCs
     * reports the first; the page carries one set of readings.
     */
    if (r == NULL) {
        return -1;
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (r->slot[i].bound
            && r->shadow[i].driver == OUT_DRIVER_DSHOT_BIDIR) {
            return (int)i;
        }
    }
    return -1;
}

bool out_render_erpm(const out_render_t *r, uint32_t *erpm, uint32_t *age_ms)
{
    const int slot = out_render_reporting_slot(r);
    if (slot < 0 || erpm == NULL || age_ms == NULL
        || !r->telem[slot].have_erpm) {
        return false;
    }
    *erpm   = r->telem[slot].erpm;
    *age_ms = out_render_hw_ms() - r->telem[slot].erpm_ms;
    return true;
}

bool out_render_edt(const out_render_t *r, dshot_telem_kind_t kind,
                    uint16_t *value, uint32_t *age_ms)
{
    const int slot = out_render_reporting_slot(r);
    if (slot < 0 || (unsigned)kind >= (unsigned)DSHOT_TELEM_KINDS
        || value == NULL || age_ms == NULL
        || !r->telem[slot].have_edt[kind]) {
        return false;
    }
    *value  = r->telem[slot].edt[kind];
    *age_ms = out_render_hw_ms() - r->telem[slot].edt_ms[kind];
    return true;
}

/* --------------------------------------------------------------- binding */

void out_render_init(out_render_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->watch_ch = -1;
}

bool out_render_bound(const out_render_t *r, uint8_t slot)
{
    return r != NULL && slot < OUT_MAX_SLOTS && r->slot[slot].bound;
}

/**
 * @brief Tells whether two slot configurations bind the same way.
 *
 * @param a One slot.
 * @param b The other slot.
 * @return true when driver, channels, pin and rate all match.
 */
static bool same(const out_slot_t *a, const out_slot_t *b)
{
    return a->driver == b->driver
           && a->first_channel == b->first_channel
           && a->channels == b->channels
           && a->pin == b->pin
           && a->rate_hz == b->rate_hz;
}

/**
 * @brief Whether a driver runs on a PIO state machine.
 *
 * @param d The driver.
 * @return true for DShot, bidirectional DShot and PPM.
 */
static bool on_pio(out_driver_t d)
{
    return d == OUT_DRIVER_DSHOT || d == OUT_DRIVER_DSHOT_BIDIR
           || d == OUT_DRIVER_PPM;
}

/**
 * @brief Let every DShot port go: no frame, its state machine free, held
 *        again by the next out_render_ports().
 *
 * @param r The renderer.
 */
static void ports_release_dshot(out_render_t *r)
{
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        out_render_port_t *p = &r->port[i];
        if (p->bound && !p->parked && p->as.driver == OUT_DRIVER_DSHOT) {
            out_render_hw_release(&p->as);
            p->bound = false;
        }
    }
}

/**
 * @brief Let every SERVO PWM port on the slice of @p pin go, held again by
 *        the next out_render_ports() if the slice leaves it room.
 *
 * Two channels of one slice run at one rate, and the PWM driver refuses the
 * second binding at another.  A slot comes first: a port bound earlier on
 * its slice would otherwise keep the slot unbound.  On the rbmod carrier
 * PROGP04 (GP0) shares slice 0 with PWM04 (GP1).
 *
 * @param r The renderer.
 * @param pin The pin of a slot about to be bound as PWM.
 */
static void ports_release_pwm_on(out_render_t *r, uint8_t pin)
{
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        out_render_port_t *p = &r->port[i];
        if (p->bound && !p->parked && p->as.driver == OUT_DRIVER_PWM
            && out_pwm_same_slice(p->as.pin, pin)) {
            out_render_hw_release(&p->as);
            p->bound = false;
        }
    }
}

/**
 * @brief A slot or port bound afresh: due at once, and nothing asked of its
 *        ESC yet.
 *
 * @param st The render state.
 */
static void fresh(out_render_slot_t *st)
{
    st->next_us = out_render_hw_us();
    dshot_edt_bind(&st->edt, out_render_hw_ms());
}

void out_render_apply(out_render_t *r, const outputs_t *o,
                      const uint16_t *rate_hz, uint8_t may_bind)
{
    if (r == NULL || o == NULL || rate_hz == NULL) {
        return;
    }
    /* What each slot is to be: the bank's, at the rate it runs at.  A rate
     * that moved is a slot that moved, so it is rebound like one. */
    out_slot_t want[OUT_MAX_SLOTS];
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        want[i] = o->slot[i];
        want[i].rate_hz = rate_hz[i];
    }
    /*
     * Every slot that moved is released before any is bound.  Two slots
     * exchanging pins is a legal reconfiguration, and binding the first one
     * while the second still held its new pin would find the backend of a
     * slot that is about to go and take it for its own.
     */
    bool moved[OUT_MAX_SLOTS];
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        out_render_slot_t *st = &r->slot[i];
        moved[i] = !(same(&r->shadow[i], &want[i]) && st->bound)
                   || (may_bind & (1u << i)) == 0u;
        if (!moved[i]) {
            continue;
        }
        /* Whatever this slot's ESC said belongs to a binding that is going
         * away, and so does whether that ESC was ever asked for extended
         * telemetry.  Cleared for a slot that was never bound too: a failed
         * bind leaves no output, and the readings from before it would
         * otherwise still be published. */
        forget_slot_telem(r, i);
        dshot_edt_bind(&st->edt, out_render_hw_ms());
        if (st->bound) {
            out_render_hw_release(&r->shadow[i]);
            st->bound = false;
        }
        /* A slot bound afresh has rendered nothing to change from. */
        st->pwm_have = false;
    }
    /*
     * The slots come first and the programming ports follow on what they
     * leave.  A DShot port bound earlier holds a state machine and program
     * space a slot bound now may need, so every DShot port is let go before
     * a slot that runs on PIO is bound; the caller's out_render_ports()
     * binds them again from what is left.
     */
    bool pio_slot = false;
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        pio_slot = pio_slot
                   || (moved[i] && (may_bind & (1u << i)) != 0u
                       && on_pio(want[i].driver));
    }
    if (pio_slot) {
        ports_release_dshot(r);
    }
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!moved[i]) {
            continue;
        }
        r->shadow[i] = want[i];
        if (want[i].driver == OUT_DRIVER_NONE
            || (may_bind & (1u << i)) == 0u) {
            continue;
        }
        if (want[i].driver == OUT_DRIVER_PWM) {
            ports_release_pwm_on(r, want[i].pin);
        }
        /*
         * A slot the silicon cannot serve is left unbound, the way a slot
         * the bank refused already behaves.  The OUTPUTS page still reads
         * back what was asked for and no register on it says whether a slot
         * is bound, so the panel draws an unbound slot as it draws a driving
         * one and the operator meets it as a lead that does not move.
         */
        r->slot[i].bound = out_render_hw_bind(&r->shadow[i]);
        fresh(&r->slot[i]);
    }
}

bool out_render_return_slot(out_render_t *r, const outputs_t *o,
                            const uint16_t *rate_hz, uint8_t slot)
{
    if (r == NULL || o == NULL || rate_hz == NULL || slot >= OUT_MAX_SLOTS) {
        return false;
    }
    out_render_slot_t *st = &r->slot[slot];
    if (st->bound) {
        return true;
    }
    r->shadow[slot] = o->slot[slot];
    r->shadow[slot].rate_hz = rate_hz[slot];
    st->pwm_have = false;
    st->bound = r->shadow[slot].driver != OUT_DRIVER_NONE
                && out_render_hw_bind(&r->shadow[slot]);
    fresh(st);
    return st->bound;
}

void out_render_ports(out_render_t *r, const uint8_t *pin,
                      const uint16_t *rate_hz, uint8_t parked, uint8_t dshot,
                      uint8_t channel0)
{
    if (r == NULL || pin == NULL || rate_hz == NULL) {
        return;
    }
    /* Every port that moved is released before any is bound, as the slots
     * are: two ports may exchange pins. */
    bool moved[LINK_PG_PORTS_MAX];
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        const out_render_port_t *p = &r->port[i];
        const bool wanted = pin[i] <= OUT_MAX_PIN;
        const bool park_it = (parked & (1u << i)) != 0u;
        const out_driver_t drv = (!park_it && (dshot & (1u << i)) != 0u)
                                     ? OUT_DRIVER_DSHOT : OUT_DRIVER_PWM;
        moved[i] = wanted != p->bound
                   || (wanted && (p->as.pin != pin[i] || p->parked != park_it
                                  || (!park_it
                                      && (p->as.driver != drv
                                          || p->as.rate_hz != rate_hz[i]))));
        if (moved[i] && p->bound) {
            if (p->parked) {
                out_render_hw_unpark(p->as.pin);
            } else {
                out_render_hw_release(&p->as);
            }
        }
    }
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        out_render_port_t *p = &r->port[i];
        if (!moved[i]) {
            continue;
        }
        memset(p, 0, sizeof(*p));
        if (pin[i] > OUT_MAX_PIN) {
            continue;
        }
        p->as.driver        = OUT_DRIVER_PWM;
        p->as.first_channel = (uint8_t)(channel0 + i);
        p->as.channels      = 1u;
        p->as.pin           = pin[i];
        p->as.rate_hz       = rate_hz[i];
        if ((parked & (1u << i)) != 0u) {
            p->parked = true;
            p->bound  = out_render_hw_park(pin[i]);
        } else {
            if ((dshot & (1u << i)) != 0u) {
                p->as.driver = OUT_DRIVER_DSHOT;
            }
            p->bound = out_render_hw_bind(&p->as);
            fresh(&p->st);
        }
    }
}

uint8_t out_render_ports_bound(const out_render_t *r)
{
    uint8_t bound = 0u;
    if (r == NULL) {
        return bound;
    }
    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        if (r->port[i].bound) {
            bound |= (uint8_t)(1u << i);
        }
    }
    return bound;
}

void out_render_ports_drive(out_render_t *r, uint8_t ports)
{
    if (r != NULL) {
        r->port_drive = ports;
    }
}

void out_render_watch(out_render_t *r, int ch)
{
    if (r != NULL) {
        r->watch_ch   = ch;
        r->edge_ready = false;
    }
}

bool out_render_edge(out_render_t *r, uint64_t *us)
{
    if (r == NULL || us == NULL || !r->edge_ready) {
        return false;
    }
    r->edge_ready = false;
    *us = r->edge_us;
    return true;
}

/* ------------------------------------------------------------- rendering */

/**
 * @brief Render one PPM slot for this pass.
 *
 * @param o The output bank.
 * @param s The slot, bound as PPM.
 * @param drive true to write the channels' pulses, false to stop the pin.
 */
static void service_ppm(const outputs_t *o, const out_slot_t *s, bool drive)
{
    if (!drive) {
        out_render_hw_ppm_stop(s->pin);
        return;
    }
    uint16_t us[OUT_RENDER_PPM_MAX];
    const uint8_t n = (s->channels > OUT_RENDER_PPM_MAX)
                          ? (uint8_t)OUT_RENDER_PPM_MAX : s->channels;
    for (uint8_t c = 0; c < n; ++c) {
        us[c] = outputs_pulse_us(o, (uint8_t)(s->first_channel + c));
    }
    /* cppcheck-suppress uninitvar
     * The loop above fills the n entries the write reads; with n at 0 it
     * reads none. */
    out_render_hw_ppm(s->pin, us, n);
}

/**
 * @brief The due time after a frame sent at @p now for due time @p due.
 *
 * One period after @p due, so a pass late by up to
 * OUT_RENDER_DSHOT_PERIOD_US - OUT_RENDER_DSHOT_MIN_US (500us) moves no
 * later frame.  A due time that would then fall less than
 * OUT_RENDER_DSHOT_MIN_US after @p now, or already lie behind it after a
 * stall of a period or more, is put OUT_RENDER_DSHOT_MIN_US after @p now:
 * one frame for the stall rather than a burst of the ones it missed, and
 * the grid runs on from there.  Wrap-safe over the 2^32us clock: both tests
 * are on differences.
 *
 * @param due The due time the frame was sent for, in us.
 * @param now The clock of the pass that sends it, in us, not before @p due.
 * @return The next due time, in us.
 */
static uint32_t dshot_next_due(uint32_t due, uint32_t now)
{
    const uint32_t next  = due + OUT_RENDER_DSHOT_PERIOD_US;
    const uint32_t ahead = next - now;
    if (ahead >= OUT_RENDER_DSHOT_MIN_US && ahead < 0x80000000u) {
        return next;
    }
    return now + OUT_RENDER_DSHOT_MIN_US;
}

/**
 * @brief Render one DShot slot or port for this pass.
 *
 * @param r The renderer.
 * @param o The output bank.
 * @param s The slot or port, bound as DShot.
 * @param st Its render state.
 * @param slot Its slot index, or OUT_MAX_SLOTS for a programming port.
 * @param drive true while the bank is armed for its channel.
 */
static void service_dshot(out_render_t *r, const outputs_t *o,
                          const out_slot_t *s, out_render_slot_t *st,
                          unsigned slot, bool drive)
{
    /*
     * The reply to the previous frame is read before the next one is sent.
     * The receiver holds it in its queue until it is taken, and taking it
     * after sending would read it against a frame it did not answer.
     */
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        dshot_telem_t t;
        if (out_render_hw_dshot_poll(s->pin, st->edt.asked, &t)) {
            record_telem(r, slot, &t);
            dshot_edt_heard(&st->edt, t.kind);
        }
    }

    if (!drive) {
        /* Driven low, in both modes, until the next run. */
        out_render_hw_dshot_stop(s->pin);
        /*
         * Kept current while nothing is being sent.  The comparison below is
         * wrap-safe over half of the microsecond clock's 71 minutes, so a
         * due time left behind by a bench that sat disarmed for more than 36
         * of them would read as "not due yet" and stay that way for another
         * 36 after it was armed again.
         */
        st->next_us = out_render_hw_us();
        /*
         * And the ask is owed again.  Extended telemetry is a runtime setting
         * an ESC forgets when it restarts (AM32 restarts after 0.5s without a
         * frame, and a disarmed bench sends none), and an ESC can be swapped
         * on a bench between one run and the next.
         *
         * What is not forgotten is that it was asked.  An ESC that keeps
         * power keeps the setting across a short disarm: reading the next
         * run's first replies as periods would take an interleaved
         * temperature or current frame for a speed, and a current of 120A
         * decodes as 8,900rpm, a number nobody questions, latched into the
         * run's peak.  dshot_edt_idle() keeps asked; only a slot that moves
         * or a restart clears it.
         */
        dshot_edt_idle(&st->edt, out_render_hw_ms());
        return;
    }
    const uint32_t now = out_render_hw_us();
    if ((uint32_t)(now - st->next_us) >= 0x80000000u) {
        return;                              /* not due yet, wrap-safe */
    }
    st->next_us = dshot_next_due(st->next_us, now);
    /*
     * A bidirectional pin at its low rest is released to its high idle on
     * the first due pass of a run, and the first frame goes out on the next
     * due pass, at least OUT_RENDER_DSHOT_MIN_US (500us) later and
     * OUT_RENDER_DSHOT_PERIOD_US (1000us) later when the wake pass was on
     * time: the ESC sees the line high before the first frame, as before
     * every other.  The pass sends nothing and asks nothing, so the ask
     * below keeps all DSHOT_CMD_REPEATS of its frames.
     */
    if (out_render_hw_dshot_wake(s->pin)) {
        return;
    }

    /*
     * The ask goes out in place of a stop frame and never in place of a
     * throttle.  A command is an ordinary frame and an ESC tells one from a
     * glitch by counting repeats, so one ask is DSHOT_CMD_REPEATS frames in a
     * row, 10ms at OUT_RENDER_DSHOT_HZ, and it is repeated every
     * DSHOT_EDT_RETRY_MS while the throttle is zero until an extended frame
     * comes back: AM32 takes a command only once it has armed itself on a
     * second of zero throttle, which a run's first 10ms never are.
     * DSHOT_CMD_EDT_ENABLE is in the command range, so nothing turns while
     * it is being sent.
     *
     * An ESC that does not know the command ignores it and keeps sending
     * periods.  Those still read as periods: this decoder only takes a frame
     * for an extended one when the mantissa's top bit is clear and the type
     * nibble is not zero, which an ESC that normalises its exponent
     * never sends.
     *
     * The telemetry bit is set on every one of the repeats.  On a value of
     * 1 to 47 the bit is what marks the frame as a command: the BLHeli_S
     * family (Bluejay) discards a command whose telemetry bit is clear and
     * zeroes its repeat counter with it, so ten frames without the bit never
     * reach the six repeats its command handler counts and extended
     * telemetry never comes on.  AM32 has no such gate and takes the command
     * either way.  On a bidirectional pin the word is 0x01B5 with the bit
     * and 0x01A4 without it: a different payload and a different checksum
     * nibble.  The bit's other meaning is a request on the separate serial
     * telemetry wire; nothing on this bench reads that wire.
     */
    const uint16_t value =
        dshot_throttle(outputs_actual(o, s->first_channel), OUT_SPAN);
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR
        && dshot_edt_frame(&st->edt, value == DSHOT_CMD_MOTOR_STOP,
                           out_render_hw_ms())) {
        out_render_hw_dshot_send(s->pin, (uint16_t)DSHOT_CMD_EDT_ENABLE, true);
        return;
    }
    out_render_hw_dshot_send(s->pin, value, false);
}

/**
 * @brief A PWM slot's or port's pulse for this pass.
 *
 * On the watched channel, the first pulse other than the last one rendered
 * (a changed command, not a disarm) is written stamped: the start of the
 * frame that first carries it is the capture's edge.  Once: the watch ends
 * with it, stamped or not.
 *
 * @param r The renderer.
 * @param st The render state of the slot or port.
 * @param s The slot or port, bound as PWM.
 * @param pulse Pulse width in microseconds. 0 stops the pin edging.
 */
static void service_pwm(out_render_t *r, out_render_slot_t *st,
                        const out_slot_t *s, uint16_t pulse)
{
    const bool changed = st->pwm_have && pulse != st->pwm_last;
    if (changed && pulse != 0u && r->watch_ch == (int)s->first_channel) {
        uint64_t at = 0u;
        if (out_render_hw_pwm_stamped(s->pin, pulse, &at)) {
            r->edge_us    = at;
            r->edge_ready = true;
        }
        r->watch_ch = -1;
    } else {
        out_render_hw_pwm(s->pin, pulse);
    }
    st->pwm_last = pulse;
    st->pwm_have = true;
}

/**
 * @brief Whether the bank is armed for the role of a channel of @p s, the
 *        slot as this module bound it.
 *
 * The shadow rather than the bank's slot: the shadow is what the pin is
 * doing.
 *
 * @param o The output bank.
 * @param s The slot, from the shadow.
 * @return true when any of the slot's channels is driving.
 */
static bool slot_driving(const outputs_t *o, const out_slot_t *s)
{
    for (unsigned k = 0; k < s->channels; ++k) {
        const unsigned ch = (unsigned)s->first_channel + k;
        if (ch < OUT_MAX_CHANNELS && outputs_channel_driving(o, (uint8_t)ch)) {
            return true;
        }
    }
    return false;
}

void out_render_service(out_render_t *r, const outputs_t *o)
{
    if (r == NULL || o == NULL) {
        return;
    }
    /*
     * One answer for the whole bank per pass: which roles it is armed for.
     * Every slot and port is judged against the same answer, so one output
     * cannot keep driving on a stale one while another has already stopped.
     * A slot drives when the arm covers the role of one of its channels; an
     * arm for the throttle leaves a surface without a pulse, and an arm for
     * the surface leaves a motor and its ESC without one.  A programming port
     * is a surface.
     *
     * Whether a channel is still being commanded is a separate question, per
     * channel, and it is not asked here: what reaches a pin is
     * outputs_actual(), whatever last wrote it.  The IOmcu's main loop calls
     * outputs_step() immediately before this, so an overdue channel is at its
     * rest by then, and what goes on the pin is that rest rather than nothing.
     * outputs_off() renders with no step in front of it and needs none: it
     * disarms first, which puts every channel at its rest and makes every
     * answer false.
     */
    const bool drive = outputs_driving(o);

    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (!r->slot[i].bound) {
            continue;
        }
        const out_slot_t *s = &r->shadow[i];
        const bool slot_drive = drive && slot_driving(o, s);
        switch (s->driver) {
        case OUT_DRIVER_PWM:
            service_pwm(r, &r->slot[i], s,
                        slot_drive ? outputs_pulse_us(o, s->first_channel)
                                   : 0u);
            break;
        case OUT_DRIVER_PPM:
            service_ppm(o, s, slot_drive);
            break;
        case OUT_DRIVER_DSHOT:
        case OUT_DRIVER_DSHOT_BIDIR:
            service_dshot(r, o, s, &r->slot[i], i, slot_drive);
            break;
        case OUT_DRIVER_NONE:
        case OUT_DRIVER_COUNT:
        default:
            break;
        }
    }

    for (unsigned i = 0; i < LINK_PG_PORTS_MAX; ++i) {
        out_render_port_t *p = &r->port[i];
        if (!p->bound || p->parked) {
            continue;
        }
        if (p->as.driver == OUT_DRIVER_DSHOT) {
            /* A motor slot's rendering: the throttle while the bank is
             * armed for it, no frame while it is not.  No slot of its own
             * for telemetry: plain DShot sends none. */
            service_dshot(r, o, &p->as, &p->st, OUT_MAX_SLOTS,
                          drive && outputs_channel_driving(
                                       o, p->as.first_channel));
            continue;
        }
        service_pwm(r, &p->st, &p->as,
                    drive && (r->port_drive & (1u << i)) != 0u
                        && outputs_channel_driving(o, p->as.first_channel)
                        ? outputs_pulse_us(o, p->as.first_channel) : 0u);
    }

    /*
     * The end of a run, and after the loop rather than before it: a
     * bidirectional slot reads the reply to its last frame on the way to
     * stopping, so a cache cleared first is repopulated by that reply with a
     * fresh timestamp and the run's final reading outlives the run.
     *
     * Asked once for the bank rather than per slot: a slot that happened not
     * to be driving must not clear what another one just heard.
     */
    if (r->was_driving && !drive) {
        forget_telem(r);
    }
    r->was_driving = drive;
}
