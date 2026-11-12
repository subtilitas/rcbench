/*
 * The output bank rendered onto pins (shared/outputs/out_render.c), over a
 * fake of the out_render_hw_*() calls that writes every call into one log.
 *
 * Under test: the slot whose readings are published is the lowest-numbered
 * bound bidirectional DShot slot, and a reply from another slot is not
 * published; two slots or two ports that exchange pins let both go before
 * either binds; a slot bound anew on PIO (DShot, PPM) lets every DShot port
 * go before its bind, and a slot outside the bind mask or a PWM slot does
 * not; a PWM slot lets a SERVO PWM port on its slice go right before its
 * bind, and leaves ports on other slices, parked ports and DShot ports; the
 * extended-telemetry ask (DSHOT_CMD_EDT_ENABLE with the telemetry bit) goes
 * out in place of stop frames, DSHOT_CMD_REPEATS of them, never in place of
 * a throttle, and never on a plain DShot slot or a DShot port; a moved
 * rate rebinds a slot at the new rate, the last slot and a port on
 * OUT_MAX_PIN bind, a returned slot takes the bank's rate; the end of a
 * run forgets the readings; the last slot and the last port render; the
 * capture's edge is stamped once on the watched channel and ends the watch,
 * and a fresh renderer watches nothing; a DShot slot's due time follows the
 * clock while disarmed and its frames are 1000us apart; a PPM write carries
 * at most OUT_RENDER_PPM_MAX channels; the last slot, port and channel of
 * each range bind, report, let go and drive like the first; a slot bound
 * afresh has no pulse to change from; NULL arguments change nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "dshot.h"
#include "link_pages.h"
#include "out_render.h"
#include "outputs.h"

/* ------------------------------------------------------------ the fake */

/* One call on the pins. */
typedef enum {
    EV_BIND, EV_RELEASE, EV_PARK, EV_UNPARK, EV_PWM, EV_STAMPED, EV_PPM,
    EV_PPM_STOP, EV_POLL, EV_STOP, EV_WAKE, EV_SEND
} ev_kind_t;

typedef struct {
    ev_kind_t    kind;
    uint8_t      pin;
    out_driver_t driver;    /* EV_BIND, EV_RELEASE */
    uint16_t     value;     /* EV_PWM, EV_STAMPED: pulse; EV_SEND: value */
    bool         telemetry; /* EV_SEND */
} ev_t;

#define LOG_MAX 256u

static ev_t     g_log[LOG_MAX];
static unsigned g_n;
static uint32_t g_us, g_ms;

/* Pins whose bind is refused, bit n for GPn. */
static uint64_t g_refuse;
/* Bidirectional pins at their low rest, bit n for GPn: from a bind or a
 * stop until a wake, as out_dshot.c keeps them. */
static uint64_t g_low;
/* The reply the next poll of g_reply_pin returns, once. */
static bool          g_reply_queued;
static uint8_t       g_reply_pin;
static dshot_telem_t g_reply;

static void log_ev(ev_kind_t k, uint8_t pin, out_driver_t d, uint16_t v,
                   bool tel)
{
    if (g_n < LOG_MAX) {
        g_log[g_n++] = (ev_t){ k, pin, d, v, tel };
    }
}

bool out_render_hw_bind(const out_slot_t *s)
{
    log_ev(EV_BIND, s->pin, s->driver, 0u, false);
    if ((g_refuse >> s->pin) & 1u) {
        return false;
    }
    if (s->driver == OUT_DRIVER_DSHOT_BIDIR) {
        g_low |= 1ull << s->pin;
    }
    return true;
}

void out_render_hw_release(const out_slot_t *s)
{
    log_ev(EV_RELEASE, s->pin, s->driver, 0u, false);
    g_low &= ~(1ull << s->pin);
}

bool out_render_hw_park(uint8_t pin)
{
    log_ev(EV_PARK, pin, OUT_DRIVER_NONE, 0u, false);
    return true;
}

void out_render_hw_unpark(uint8_t pin)
{
    log_ev(EV_UNPARK, pin, OUT_DRIVER_NONE, 0u, false);
}

void out_render_hw_pwm(uint8_t pin, uint16_t pulse_us)
{
    log_ev(EV_PWM, pin, OUT_DRIVER_PWM, pulse_us, false);
}

bool out_render_hw_pwm_stamped(uint8_t pin, uint16_t pulse_us,
                               uint64_t *at_us)
{
    log_ev(EV_STAMPED, pin, OUT_DRIVER_PWM, pulse_us, false);
    *at_us = g_us;
    return true;
}

/* The channel count and widths of the last PPM write. */
static uint8_t  g_ppm_n;
static uint16_t g_ppm_us[OUT_MAX_CHANNELS];

void out_render_hw_ppm(uint8_t pin, const uint16_t *us, uint8_t n)
{
    log_ev(EV_PPM, pin, OUT_DRIVER_PPM, n > 0u ? us[0] : 0u, false);
    g_ppm_n = n;
    for (uint8_t i = 0; i < n && i < OUT_MAX_CHANNELS; ++i) {
        g_ppm_us[i] = us[i];
    }
}

void out_render_hw_ppm_stop(uint8_t pin)
{
    log_ev(EV_PPM_STOP, pin, OUT_DRIVER_PPM, 0u, false);
}

bool out_render_hw_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *t)
{
    log_ev(EV_POLL, pin, OUT_DRIVER_DSHOT_BIDIR, 0u, edt);
    if (!g_reply_queued || pin != g_reply_pin) {
        return false;
    }
    g_reply_queued = false;
    *t = g_reply;
    return true;
}

void out_render_hw_dshot_stop(uint8_t pin)
{
    log_ev(EV_STOP, pin, OUT_DRIVER_DSHOT, 0u, false);
}

bool out_render_hw_dshot_wake(uint8_t pin)
{
    const bool low = ((g_low >> pin) & 1u) != 0u;
    g_low &= ~(1ull << pin);
    if (low) {
        log_ev(EV_WAKE, pin, OUT_DRIVER_DSHOT, 0u, false);
    }
    return low;
}

void out_render_hw_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    log_ev(EV_SEND, pin, OUT_DRIVER_DSHOT, value, telemetry);
}

uint32_t out_render_hw_us(void) { return g_us; }

uint32_t out_render_hw_ms(void) { return g_ms; }

/* ---------------------------------------------------------- the helpers */

static out_render_t g_r;
static outputs_t    g_o;
static uint16_t     g_rate[OUT_MAX_SLOTS];

/* A fresh renderer and an empty bank at 1s, every rate 50. */
static void fresh(void)
{
    g_n = 0u;
    g_us = 1000000u;
    g_ms = 1000u;
    g_refuse = 0u;
    g_low = 0u;
    g_reply_queued = false;
    out_render_init(&g_r);
    outputs_init(&g_o, g_ms);
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        g_rate[i] = 50u;
    }
}

/* Slot @p slot as @p driver on @p pin, rendering channel @p ch. */
static void slot(uint8_t slot, out_driver_t driver, uint8_t pin, uint8_t ch)
{
    const uint16_t rate = (driver == OUT_DRIVER_DSHOT
                           || driver == OUT_DRIVER_DSHOT_BIDIR) ? 600u : 50u;
    const out_slot_t s = { driver, ch, 1u, pin, rate };
    CHECK(outputs_configure(&g_o, slot, &s));
    g_rate[slot] = rate;
}

/* Index of the first log entry of @p kind on @p pin from @p from on, or
 * LOG_MAX when there is none. */
static unsigned find(ev_kind_t kind, uint8_t pin, unsigned from)
{
    for (unsigned i = from; i < g_n; ++i) {
        if (g_log[i].kind == kind && g_log[i].pin == pin) {
            return i;
        }
    }
    return LOG_MAX;
}

/* Count of log entries of @p kind. */
static unsigned count(ev_kind_t kind)
{
    unsigned n = 0u;
    for (unsigned i = 0; i < g_n; ++i) {
        n += g_log[i].kind == kind;
    }
    return n;
}

/* The ports: port 0 and port 1 on @p pin0 and @p pin1 (0xFF for none),
 * @p parked and @p dshot as bit masks, channels 9 and 10. */
static void ports(uint8_t pin0, uint8_t pin1, uint8_t parked, uint8_t dshot)
{
    const uint8_t pin[LINK_PG_PORTS_MAX] = { pin0, pin1, 0xFFu, 0xFFu };
    const uint16_t hz[LINK_PG_PORTS_MAX] = { 50u, 300u, 50u, 50u };
    out_render_ports(&g_r, pin, hz, parked, dshot, 9u);
}

/* One pass: the clock 1ms on, the bank stepped, the pins rendered. */
static void pass(void)
{
    g_us += 1000u;
    g_ms += 1u;
    outputs_step(&g_o, g_ms);
    out_render_service(&g_r, &g_o);
}

/* ----------------------------------------------------------- the cases */

/*
 * The lowest-numbered bound bidirectional slot reports.  A plain DShot slot
 * below it does not count; a bidirectional slot whose bind failed does not
 * count, and the next one reports; a reply from a slot that does not report
 * is kept for it and not published.
 */
TEST_CASE(the_lowest_bound_bidirectional_slot_reports)
{
    fresh();
    CHECK_EQ(out_render_reporting_slot(&g_r), -1);
    slot(0u, OUT_DRIVER_DSHOT, 6u, 0u);
    slot(1u, OUT_DRIVER_PWM, 8u, 1u);
    slot(2u, OUT_DRIVER_DSHOT_BIDIR, 2u, 2u);
    slot(4u, OUT_DRIVER_DSHOT_BIDIR, 3u, 4u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(out_render_reporting_slot(&g_r), 2);

    /* A reply on slot 4's pin is not slot 2's reading. */
    uint32_t erpm = 0u, age = 0u;
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 4000u };
    g_reply_pin = 3u;
    g_reply_queued = true;
    pass();
    CHECK(!g_reply_queued);
    CHECK(!out_render_erpm(&g_r, &erpm, &age));
    /* One on slot 2's pin is. */
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 2000u };
    g_reply_pin = 2u;
    g_reply_queued = true;
    pass();
    CHECK(out_render_erpm(&g_r, &erpm, &age));
    CHECK_EQ(erpm, 2000u);
    CHECK_EQ(age, 0u);

    /* Slot 2's pin refused: slot 4 reports, with its own reading. */
    fresh();
    g_refuse = 1ull << 2;
    slot(2u, OUT_DRIVER_DSHOT_BIDIR, 2u, 2u);
    slot(4u, OUT_DRIVER_DSHOT_BIDIR, 3u, 4u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(!out_render_bound(&g_r, 2u));
    CHECK_EQ(out_render_reporting_slot(&g_r), 4);
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 4000u };
    g_reply_pin = 3u;
    g_reply_queued = true;
    pass();
    CHECK(out_render_erpm(&g_r, &erpm, &age));
    CHECK_EQ(erpm, 4000u);

    /* Slot 4 cleared: nothing reports, nothing is published. */
    const out_slot_t none = { .driver = OUT_DRIVER_NONE };
    CHECK(outputs_configure(&g_o, 4u, &none));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(out_render_reporting_slot(&g_r), -1);
    CHECK(!out_render_erpm(&g_r, &erpm, &age));
}

/* Two PWM slots exchange pins: both are let go before either binds. */
TEST_CASE(slots_exchanging_pins_let_both_go_before_either_binds)
{
    fresh();
    slot(0u, OUT_DRIVER_PWM, 4u, 0u);
    slot(1u, OUT_DRIVER_PWM, 6u, 1u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(out_render_bound(&g_r, 0u) && out_render_bound(&g_r, 1u));

    const out_slot_t none = { .driver = OUT_DRIVER_NONE };
    CHECK(outputs_configure(&g_o, 0u, &none));
    slot(1u, OUT_DRIVER_PWM, 4u, 1u);
    slot(0u, OUT_DRIVER_PWM, 6u, 0u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(count(EV_RELEASE), 2u);
    CHECK_EQ(count(EV_BIND), 2u);
    const unsigned last_release =
        find(EV_RELEASE, 4u, 0u) > find(EV_RELEASE, 6u, 0u)
            ? find(EV_RELEASE, 4u, 0u) : find(EV_RELEASE, 6u, 0u);
    const unsigned first_bind =
        find(EV_BIND, 4u, 0u) < find(EV_BIND, 6u, 0u)
            ? find(EV_BIND, 4u, 0u) : find(EV_BIND, 6u, 0u);
    CHECK(last_release < first_bind);

    /* An apply that moves nothing calls nothing. */
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(g_n, 0u);
}

/*
 * A slot on PIO bound anew lets every DShot port go before its bind; a SERVO
 * PWM port stays.  A PPM slot is on PIO too.  A DShot slot outside the bind
 * mask is let go and not bound, and moves no port.
 */
TEST_CASE(a_pio_slot_bound_anew_lets_every_dshot_port_go_first)
{
    fresh();
    ports(13u, 14u, 0u, 2u);         /* port 0 SERVO PWM, port 1 DShot */
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
    slot(0u, OUT_DRIVER_DSHOT_BIDIR, 2u, 0u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    const unsigned rel = find(EV_RELEASE, 14u, 0u);
    CHECK(rel < LOG_MAX);
    CHECK_EQ(g_log[rel].driver, OUT_DRIVER_DSHOT);
    CHECK(rel < find(EV_BIND, 2u, 0u));
    CHECK(find(EV_BIND, 2u, 0u) < LOG_MAX);
    CHECK_EQ(find(EV_RELEASE, 13u, 0u), LOG_MAX);
    CHECK_EQ(out_render_ports_bound(&g_r), 1u);

    /* The ports bind again on what is left. */
    ports(13u, 14u, 0u, 2u);
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);

    /* A PPM slot is on PIO. */
    const out_slot_t ppm = { OUT_DRIVER_PPM, 4u, 2u, 7u, 50u };
    CHECK(outputs_configure(&g_o, 1u, &ppm));
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(find(EV_RELEASE, 14u, 0u) < find(EV_BIND, 7u, 0u));
    ports(13u, 14u, 0u, 2u);

    /* A DShot slot left out of the mask: let go, not bound, no port moved. */
    slot(2u, OUT_DRIVER_DSHOT, 5u, 6u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, (uint8_t)~(1u << 2));
    CHECK_EQ(find(EV_RELEASE, 14u, 0u), LOG_MAX);
    CHECK_EQ(find(EV_BIND, 5u, 0u), LOG_MAX);
    CHECK(!out_render_bound(&g_r, 2u));
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
    /* Returned on its own, it binds and moves nothing else. */
    g_n = 0u;
    CHECK(out_render_return_slot(&g_r, &g_o, g_rate, 2u));
    CHECK_EQ(g_n, 1u);
    CHECK_EQ(g_log[0].kind, EV_BIND);
    CHECK_EQ(g_log[0].pin, 5u);
    CHECK(out_render_return_slot(&g_r, &g_o, g_rate, 2u));
    CHECK_EQ(g_n, 1u);
}

/*
 * A PWM slot lets a SERVO PWM port on its slice go right before its bind:
 * GP1 shares slice 0 with GP0.  A port on another slice (GP14), a parked
 * port on the slice (GP16, slice 0 folded) and a DShot port stay.
 */
TEST_CASE(a_pwm_slot_lets_the_ports_on_its_slice_go_right_before_its_bind)
{
    fresh();
    ports(0u, 14u, 0u, 0u);
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
    slot(0u, OUT_DRIVER_PWM, 1u, 0u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    const unsigned rel = find(EV_RELEASE, 0u, 0u);
    CHECK(rel < LOG_MAX);
    CHECK_EQ(find(EV_BIND, 1u, 0u), rel + 1u);
    CHECK_EQ(find(EV_RELEASE, 14u, 0u), LOG_MAX);
    CHECK_EQ(out_render_ports_bound(&g_r), 2u);

    /* Parked on the slice, and DShot: neither is let go for a PWM slot. */
    fresh();
    ports(0u, 16u, 2u, 1u);          /* port 0 DShot on GP0, port 1 parked */
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
    slot(0u, OUT_DRIVER_PWM, 1u, 0u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(count(EV_RELEASE), 0u);
    CHECK_EQ(count(EV_UNPARK), 0u);
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
}

/* Two ports exchange pins and functions: both let go before either is
 * taken. */
TEST_CASE(ports_exchanging_pins_let_both_go_before_either_binds)
{
    fresh();
    ports(13u, 14u, 2u, 0u);         /* 13 SERVO PWM, 14 parked */
    g_n = 0u;
    ports(14u, 13u, 2u, 0u);         /* 14 SERVO PWM, 13 parked */
    const unsigned rel = find(EV_RELEASE, 13u, 0u);
    const unsigned unp = find(EV_UNPARK, 14u, 0u);
    const unsigned bnd = find(EV_BIND, 14u, 0u);
    const unsigned prk = find(EV_PARK, 13u, 0u);
    CHECK(rel < LOG_MAX && unp < LOG_MAX && bnd < LOG_MAX && prk < LOG_MAX);
    CHECK(rel < bnd && rel < prk && unp < bnd && unp < prk);
    CHECK_EQ(out_render_ports_bound(&g_r), 3u);
}

/*
 * A rate that moved is a slot that moved: rebound at the new rate.  The last
 * slot binds like the first.  A slot returned on its own binds at the bank's
 * rate; an empty one binds nothing.  A port on OUT_MAX_PIN is held, one
 * above it is not.
 */
TEST_CASE(rates_and_the_ends_of_the_ranges_bind_as_asked)
{
    fresh();
    slot(0u, OUT_DRIVER_PWM, 4u, 0u);
    slot(7u, OUT_DRIVER_PWM, 9u, 7u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(out_render_bound(&g_r, 0u));
    CHECK(out_render_bound(&g_r, 7u));
    CHECK(find(EV_BIND, 9u, 0u) < LOG_MAX);

    g_rate[0] = 333u;
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(find(EV_RELEASE, 4u, 0u), 0u);
    CHECK_EQ(find(EV_BIND, 4u, 0u), 1u);
    CHECK_EQ(g_r.shadow[0].rate_hz, 333u);
    CHECK_EQ(g_n, 2u);

    /* Returned alone: at the bank's rate, not the one it left with. */
    out_render_apply(&g_r, &g_o, g_rate, (uint8_t)~1u);
    CHECK(!out_render_bound(&g_r, 0u));
    g_rate[0] = 400u;
    g_n = 0u;
    CHECK(out_render_return_slot(&g_r, &g_o, g_rate, 0u));
    CHECK_EQ(g_n, 1u);
    CHECK_EQ(g_r.shadow[0].rate_hz, 400u);
    /* An empty slot returned binds nothing. */
    g_n = 0u;
    CHECK(!out_render_return_slot(&g_r, &g_o, g_rate, 3u));
    CHECK_EQ(g_n, 0u);

    ports(OUT_MAX_PIN, OUT_MAX_PIN + 1u, 0u, 0u);
    CHECK_EQ(out_render_ports_bound(&g_r), 1u);
}

/* The pulse of the last EV_PWM or EV_STAMPED on @p pin, 0xFFFF for none. */
static uint16_t last_pulse(uint8_t pin)
{
    for (unsigned i = g_n; i-- > 0u;) {
        if ((g_log[i].kind == EV_PWM || g_log[i].kind == EV_STAMPED)
            && g_log[i].pin == pin) {
            return g_log[i].value;
        }
    }
    return 0xFFFFu;
}

/*
 * The end of a run forgets every reading: one heard while driving is
 * published until the first pass that does not drive, and none after it.
 * A reading heard on a bench that never drove is kept across its passes.
 */
TEST_CASE(the_end_of_a_run_forgets_the_readings)
{
    uint32_t erpm = 0u, age = 0u;
    fresh();
    slot(0u, OUT_DRIVER_DSHOT_BIDIR, 2u, 0u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 3000u };
    g_reply_pin = 2u;
    g_reply_queued = true;
    pass();                          /* disarmed: heard, kept */
    pass();
    pass();
    CHECK(out_render_erpm(&g_r, &erpm, &age));

    outputs_arm(&g_o, true, g_ms);
    pass();
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 5000u };
    g_reply_queued = true;
    pass();                          /* driving: heard */
    CHECK(out_render_erpm(&g_r, &erpm, &age));
    CHECK_EQ(erpm, 5000u);
    outputs_arm(&g_o, false, g_ms);
    pass();                          /* the run ends */
    CHECK(!out_render_erpm(&g_r, &erpm, &age));
}

/* The last slot and the first and last ports render their pulse while
 * armed, a port outside the drive mask renders 0, and every one renders 0
 * once the bank disarms. */
TEST_CASE(the_last_slot_and_the_last_port_render)
{
    fresh();
    slot(7u, OUT_DRIVER_PWM, 9u, 7u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    const uint8_t pin[LINK_PG_PORTS_MAX] = { 13u, 14u, 0xFFu, 20u };
    const uint16_t hz[LINK_PG_PORTS_MAX] = { 50u, 50u, 50u, 50u };
    out_render_ports(&g_r, pin, hz, 0u, 0u, 9u);
    CHECK_EQ(out_render_ports_bound(&g_r), 0x0Bu);
    out_render_ports_drive(&g_r, 0x09u);       /* ports 0 and 3, not 1 */
    outputs_arm(&g_o, true, g_ms);
    pass();
    CHECK_EQ(last_pulse(9u), outputs_pulse_us(&g_o, 7u));
    CHECK_EQ(last_pulse(13u), outputs_pulse_us(&g_o, 9u));
    CHECK_EQ(last_pulse(20u), outputs_pulse_us(&g_o, 12u));
    CHECK(last_pulse(9u) != 0u && last_pulse(13u) != 0u
          && last_pulse(20u) != 0u);
    CHECK_EQ(last_pulse(14u), 0u);
    outputs_arm(&g_o, false, g_ms);
    pass();
    CHECK_EQ(last_pulse(9u), 0u);
    CHECK_EQ(last_pulse(13u), 0u);
    CHECK_EQ(last_pulse(20u), 0u);
}

/*
 * The capture's edge: on the watched channel the first changed pulse is
 * written stamped, the edge is taken once, and the watch ends with it.  A
 * pulse of 0 (a disarm) is not an edge and keeps the watch.  Another
 * channel's change stamps nothing.  A renderer just initialised watches
 * nothing.
 */
TEST_CASE(a_watched_channel_stamps_its_first_changed_pulse_once)
{
    uint64_t at = 0u;
    fresh();
    slot(0u, OUT_DRIVER_PWM, 4u, 0u);
    slot(1u, OUT_DRIVER_PWM, 6u, 1u);
    CHECK(outputs_set_slew(&g_o, 0u, 0u));
    CHECK(outputs_set_slew(&g_o, 1u, 0u));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm(&g_o, true, g_ms);
    pass();
    /* Initialised: no watch, so a change on channel 0 stamps nothing. */
    CHECK(outputs_set(&g_o, 0u, 100u, g_ms));
    pass();
    CHECK_EQ(count(EV_STAMPED), 0u);
    CHECK(!out_render_edge(&g_r, &at));

    out_render_watch(&g_r, 0);
    CHECK(outputs_set(&g_o, 1u, 900u, g_ms));
    pass();                          /* another channel: no edge */
    CHECK_EQ(count(EV_STAMPED), 0u);
    outputs_arm(&g_o, false, g_ms);
    pass();                          /* a disarm: no edge, watch kept */
    CHECK_EQ(count(EV_STAMPED), 0u);
    CHECK(!out_render_edge(&g_r, &at));
    outputs_arm(&g_o, true, g_ms);
    g_n = 0u;
    pass();                          /* back from 0: the edge */
    CHECK_EQ(count(EV_STAMPED), 1u);
    CHECK_EQ(find(EV_STAMPED, 4u, 0u), 0u);
    CHECK_EQ(find(EV_PWM, 4u, 0u), LOG_MAX);
    CHECK(out_render_edge(&g_r, &at));
    CHECK_EQ(at, (uint64_t)g_us);
    CHECK(!out_render_edge(&g_r, &at));
    /* The watch has ended: the next change is a plain write. */
    CHECK(outputs_set(&g_o, 0u, 800u, g_ms));
    pass();
    CHECK_EQ(count(EV_STAMPED), 1u);
    CHECK(!out_render_edge(&g_r, &at));

    /* A new watch clears an edge not yet taken. */
    out_render_watch(&g_r, 0);
    CHECK(outputs_set(&g_o, 0u, 300u, g_ms));
    pass();
    out_render_watch(&g_r, 0);
    CHECK(!out_render_edge(&g_r, &at));
}

/*
 * A DShot slot's due time follows the clock while it sits disarmed: armed
 * again after 40 minutes, more than half the 71.6-minute microsecond wrap,
 * it frames on the first pass.  Armed, the frames are 1000us apart: none at
 * 999us, one at 1000us.  A pass 2^31us after the due time reads as not due,
 * which is why the due time follows the clock while disarmed.
 */
TEST_CASE(a_dshot_slot_is_due_at_once_after_a_long_disarm)
{
    fresh();
    slot(0u, OUT_DRIVER_DSHOT, 2u, 0u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    pass();
    g_us += 40u * 60u * 1000000u;
    g_ms += 40u * 60u * 1000u;
    out_render_service(&g_r, &g_o);  /* disarmed */
    outputs_arm(&g_o, true, g_ms);
    g_n = 0u;
    g_us += 1u;
    out_render_service(&g_r, &g_o);
    CHECK_EQ(count(EV_SEND), 1u);
    g_us += OUT_RENDER_DSHOT_PERIOD_US - 1u;
    out_render_service(&g_r, &g_o);
    CHECK_EQ(count(EV_SEND), 1u);
    g_us += 1u;
    out_render_service(&g_r, &g_o);
    CHECK_EQ(count(EV_SEND), 2u);
    /* The due test is wrap-safe over half the clock: a pass exactly 2^31us
     * after the due time reads as not due, one 2^31us - 1 after it as due. */
    g_us += OUT_RENDER_DSHOT_PERIOD_US + 0x80000000u;
    out_render_service(&g_r, &g_o);
    CHECK_EQ(count(EV_SEND), 2u);
    g_us -= 1u;
    out_render_service(&g_r, &g_o);
    CHECK_EQ(count(EV_SEND), 3u);
}

/*
 * A PPM slot writes its channels' widths, at most OUT_RENDER_PPM_MAX of
 * them: a slot holding nine (the bank refuses one, so the table is written
 * here directly) writes the first eight.  Disarmed, it stops.
 */
TEST_CASE(a_ppm_slot_writes_at_most_eight_channels)
{
    fresh();
    slot(0u, OUT_DRIVER_PPM, 7u, 0u);
    g_o.slot[0].channels = 3u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm(&g_o, true, g_ms);
    pass();
    CHECK_EQ(g_ppm_n, 3u);
    for (uint8_t c = 0; c < 3u; ++c) {
        CHECK_EQ(g_ppm_us[c], outputs_pulse_us(&g_o, c));
    }
    g_o.slot[0].channels = OUT_RENDER_PPM_MAX;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    pass();
    CHECK_EQ(g_ppm_n, OUT_RENDER_PPM_MAX);
    g_o.slot[0].channels = OUT_RENDER_PPM_MAX + 1u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    pass();
    CHECK_EQ(g_ppm_n, OUT_RENDER_PPM_MAX);
    outputs_arm(&g_o, false, g_ms);
    g_n = 0u;
    pass();
    CHECK_EQ(count(EV_PPM_STOP), 1u);
    CHECK_EQ(count(EV_PPM), 0u);
}

/*
 * The last of each range: a bidirectional slot 7 alone reports, its reading
 * is published and forgotten when it moves; a DShot slot 7 bound anew lets
 * the DShot ports go; slot 7 returns on its own; port 3 is let go for a PWM
 * slot on its slice and for a slot on PIO; a slot on channel 15 drives.
 */
TEST_CASE(the_last_slot_port_and_channel_count)
{
    uint32_t erpm = 0u, age = 0u;
    fresh();
    slot(7u, OUT_DRIVER_DSHOT_BIDIR, 3u, 7u);
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(out_render_reporting_slot(&g_r), 7);
    g_reply = (dshot_telem_t){ .kind = DSHOT_TELEM_ERPM, .erpm = 7000u };
    g_reply_pin = 3u;
    g_reply_queued = true;
    pass();
    CHECK(out_render_erpm(&g_r, &erpm, &age));
    CHECK_EQ(erpm, 7000u);
    uint16_t v = 0u;
    CHECK(!out_render_edt(&g_r, DSHOT_TELEM_KINDS, &v, &age));
    g_rate[7] = 300u;                /* moved: its reading goes */
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(out_render_reporting_slot(&g_r), 7);
    CHECK(!out_render_erpm(&g_r, &erpm, &age));

    /* Slot 7 on PIO bound anew: port 3 (DShot) goes first. */
    fresh();
    const uint8_t pin[LINK_PG_PORTS_MAX] = { 0xFFu, 0xFFu, 0xFFu, 20u };
    const uint16_t hz[LINK_PG_PORTS_MAX] = { 50u, 50u, 50u, 300u };
    out_render_ports(&g_r, pin, hz, 0u, 8u, 9u);
    CHECK_EQ(out_render_ports_bound(&g_r), 8u);
    slot(7u, OUT_DRIVER_DSHOT, 3u, 7u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(find(EV_RELEASE, 20u, 0u) < find(EV_BIND, 3u, 0u));
    CHECK_EQ(out_render_ports_bound(&g_r), 0u);
    /* Left out and returned on its own. */
    out_render_apply(&g_r, &g_o, g_rate, 0x7Fu);
    CHECK(!out_render_bound(&g_r, 7u));
    CHECK(out_render_return_slot(&g_r, &g_o, g_rate, 7u));
    CHECK(out_render_bound(&g_r, 7u));

    /* Port 3 SERVO PWM on GP21, slice 2 with GP20: a PWM slot there lets it
     * go. */
    fresh();
    const uint8_t pin3[LINK_PG_PORTS_MAX] = { 0xFFu, 0xFFu, 0xFFu, 21u };
    out_render_ports(&g_r, pin3, hz, 0u, 0u, 9u);
    CHECK_EQ(out_render_ports_bound(&g_r), 8u);
    slot(0u, OUT_DRIVER_PWM, 20u, 0u);
    g_n = 0u;
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK_EQ(find(EV_BIND, 20u, 0u), find(EV_RELEASE, 21u, 0u) + 1u);
    CHECK_EQ(out_render_ports_bound(&g_r), 0u);

    /* A PPM slot whose last channel is 15 drives when only 15's role is
     * armed. */
    fresh();
    const out_slot_t ppm = { OUT_DRIVER_PPM, 14u, 2u, 7u, 50u };
    CHECK(outputs_configure(&g_o, 0u, &ppm));
    CHECK(outputs_set_role(&g_o, 15u, OUT_ROLE_THROTTLE));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm_roles(&g_o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), g_ms);
    g_n = 0u;
    pass();
    CHECK_EQ(count(EV_PPM), 1u);
    CHECK_EQ(count(EV_PPM_STOP), 0u);

    /* A one-channel PWM slot on channel 0 does not drive for the armed
     * role of channel 1 beside it. */
    fresh();
    slot(0u, OUT_DRIVER_PWM, 4u, 0u);
    CHECK(outputs_set_role(&g_o, 1u, OUT_ROLE_THROTTLE));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm_roles(&g_o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), g_ms);
    pass();
    CHECK_EQ(last_pulse(4u), 0u);
}

/*
 * A slot bound afresh, by an apply or by a return, has rendered nothing to
 * change from: its first pulse is a plain write even on the watched channel
 * and with a pulse other than the one before the bind.  A returned slot
 * takes the bank's slot as it is now.
 */
TEST_CASE(a_slot_bound_afresh_has_no_pulse_to_change_from)
{
    fresh();
    slot(0u, OUT_DRIVER_PWM, 4u, 0u);
    CHECK(outputs_set_slew(&g_o, 0u, 0u));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm(&g_o, true, g_ms);
    pass();
    out_render_watch(&g_r, 0);
    g_rate[0] = 333u;                /* moved by its rate */
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    CHECK(outputs_set(&g_o, 0u, 100u, g_ms));
    pass();
    CHECK_EQ(count(EV_STAMPED), 0u);

    /* Left out, the bank's slot moved to GP6, returned: GP6, no stamp. */
    out_render_apply(&g_r, &g_o, g_rate, 0xFEu);
    const out_slot_t none = { .driver = OUT_DRIVER_NONE };
    CHECK(outputs_configure(&g_o, 0u, &none));
    slot(0u, OUT_DRIVER_PWM, 6u, 0u);
    g_rate[0] = 333u;
    g_n = 0u;
    CHECK(out_render_return_slot(&g_r, &g_o, g_rate, 0u));
    CHECK_EQ(find(EV_BIND, 6u, 0u), 0u);
    CHECK(outputs_set(&g_o, 0u, 900u, g_ms));
    pass();
    CHECK_EQ(count(EV_STAMPED), 0u);
    CHECK_EQ(last_pulse(6u), outputs_pulse_us(&g_o, 0u));
}

/* The sends on @p pin from log entry @p from on, into @p v and @p tel, at
 * most @p max; the count. */
static unsigned sends(uint8_t pin, unsigned from, uint16_t *v, bool *tel,
                      unsigned max)
{
    unsigned n = 0u;
    for (unsigned i = from; i < g_n && n < max; ++i) {
        if (g_log[i].kind == EV_SEND && g_log[i].pin == pin) {
            v[n]   = g_log[i].value;
            tel[n] = g_log[i].telemetry;
            n++;
        }
    }
    return n;
}

/* A throttle slot @p driver on GP2, channel 0, no slew, armed for the
 * throttle at @p cmd. */
static void armed_motor(out_driver_t driver, uint16_t cmd)
{
    fresh();
    slot(0u, driver, 2u, 0u);
    CHECK(outputs_set_role(&g_o, 0u, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_slew(&g_o, 0u, 0u));
    out_render_apply(&g_r, &g_o, g_rate, 0xFFu);
    outputs_arm(&g_o, true, g_ms);
    CHECK(outputs_set(&g_o, 0u, cmd, g_ms));
    g_n = 0u;
}

/*
 * At zero throttle a bidirectional slot's first due pass wakes the pin, and
 * the next DSHOT_CMD_REPEATS frames are DSHOT_CMD_EDT_ENABLE with the
 * telemetry bit, each in place of a stop frame; then stop frames without the
 * bit.
 */
TEST_CASE(the_edt_ask_goes_out_in_place_of_stop_frames)
{
    armed_motor(OUT_DRIVER_DSHOT_BIDIR, 0u);
    pass();
    CHECK_EQ(count(EV_WAKE), 1u);
    CHECK_EQ(count(EV_SEND), 0u);
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS + 3u; ++i) {
        pass();
    }
    uint16_t v[32];
    bool tel[32];
    const unsigned n = sends(2u, 0u, v, tel, 32u);
    CHECK_EQ(n, DSHOT_CMD_REPEATS + 3u);
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS; ++i) {
        CHECK_EQ(v[i], (uint16_t)DSHOT_CMD_EDT_ENABLE);
        CHECK(tel[i]);
    }
    for (unsigned i = DSHOT_CMD_REPEATS; i < n; ++i) {
        CHECK_EQ(v[i], (uint16_t)DSHOT_CMD_MOTOR_STOP);
        CHECK(!tel[i]);
    }
}

/*
 * A throttle above zero goes out as the throttle on every frame and is never
 * replaced by the ask.  When the throttle returns to zero the ask, still
 * owed, goes out in place of the stop frames.
 */
TEST_CASE(the_edt_ask_never_replaces_a_throttle)
{
    armed_motor(OUT_DRIVER_DSHOT_BIDIR, OUT_SPAN / 2u);
    outputs_step(&g_o, g_ms);
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS + 2u; ++i) {
        pass();
    }
    uint16_t v[32];
    bool tel[32];
    unsigned n = sends(2u, 0u, v, tel, 32u);
    CHECK_EQ(n, DSHOT_CMD_REPEATS + 1u);     /* the first pass woke */
    const uint16_t half = dshot_throttle(OUT_SPAN / 2u, OUT_SPAN);
    for (unsigned i = 0; i < n; ++i) {
        CHECK_EQ(v[i], half);
        CHECK(!tel[i]);
    }
    CHECK(outputs_set(&g_o, 0u, 0u, g_ms));
    const unsigned from = g_n;
    pass();
    n = sends(2u, from, v, tel, 32u);
    CHECK_EQ(n, 1u);
    CHECK_EQ(v[0], (uint16_t)DSHOT_CMD_EDT_ENABLE);
    CHECK(tel[0]);
}

/* A plain DShot slot and a DShot port send stop frames at zero throttle and
 * never the ask. */
TEST_CASE(plain_dshot_slots_and_dshot_ports_never_ask)
{
    armed_motor(OUT_DRIVER_DSHOT, 0u);
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS + 2u; ++i) {
        pass();
    }
    uint16_t v[32];
    bool tel[32];
    unsigned n = sends(2u, 0u, v, tel, 32u);
    CHECK_EQ(n, DSHOT_CMD_REPEATS + 2u);
    for (unsigned i = 0; i < n; ++i) {
        CHECK_EQ(v[i], (uint16_t)DSHOT_CMD_MOTOR_STOP);
        CHECK(!tel[i]);
    }
    CHECK_EQ(count(EV_POLL), 0u);

    /* Port 1 DShot on GP14, channel 10. */
    fresh();
    CHECK(outputs_set_role(&g_o, 10u, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_slew(&g_o, 10u, 0u));
    ports(0xFFu, 14u, 0u, 2u);
    outputs_arm(&g_o, true, g_ms);
    g_n = 0u;
    for (unsigned i = 0; i < DSHOT_CMD_REPEATS + 2u; ++i) {
        pass();
    }
    n = sends(14u, 0u, v, tel, 32u);
    CHECK_EQ(n, DSHOT_CMD_REPEATS + 2u);
    for (unsigned i = 0; i < n; ++i) {
        CHECK_EQ(v[i], (uint16_t)DSHOT_CMD_MOTOR_STOP);
        CHECK(!tel[i]);
    }
}

/* Every entry point with a NULL renderer or argument calls nothing and
 * answers nothing. */
TEST_CASE(null_arguments_change_nothing)
{
    fresh();
    const uint8_t pin[LINK_PG_PORTS_MAX] = { 13u, 14u, 0xFFu, 0xFFu };
    uint64_t us = 0u;
    uint32_t erpm = 0u, age = 0u;
    uint16_t value = 0u;
    out_render_init(NULL);
    out_render_apply(NULL, &g_o, g_rate, 0xFFu);
    out_render_apply(&g_r, NULL, g_rate, 0xFFu);
    out_render_apply(&g_r, &g_o, NULL, 0xFFu);
    CHECK(!out_render_return_slot(NULL, &g_o, g_rate, 0u));
    CHECK(!out_render_return_slot(&g_r, &g_o, g_rate, OUT_MAX_SLOTS));
    out_render_ports(NULL, pin, g_rate, 0u, 0u, 9u);
    out_render_ports(&g_r, NULL, g_rate, 0u, 0u, 9u);
    out_render_ports(&g_r, pin, NULL, 0u, 0u, 9u);
    out_render_service(NULL, &g_o);
    out_render_service(&g_r, NULL);
    out_render_ports_drive(NULL, 1u);
    out_render_watch(NULL, 0);
    CHECK(!out_render_edge(NULL, &us));
    CHECK(!out_render_edge(&g_r, NULL));
    CHECK(!out_render_bound(NULL, 0u));
    CHECK(!out_render_bound(&g_r, OUT_MAX_SLOTS));
    CHECK_EQ(out_render_ports_bound(NULL), 0u);
    CHECK_EQ(out_render_reporting_slot(NULL), -1);
    CHECK(!out_render_erpm(NULL, &erpm, &age));
    CHECK(!out_render_edt(NULL, DSHOT_TELEM_TEMPERATURE, &value, &age));
    CHECK_EQ(g_n, 0u);
}

int main(void)
{
    RUN(the_lowest_bound_bidirectional_slot_reports);
    RUN(slots_exchanging_pins_let_both_go_before_either_binds);
    RUN(a_pio_slot_bound_anew_lets_every_dshot_port_go_first);
    RUN(a_pwm_slot_lets_the_ports_on_its_slice_go_right_before_its_bind);
    RUN(ports_exchanging_pins_let_both_go_before_either_binds);
    RUN(rates_and_the_ends_of_the_ranges_bind_as_asked);
    RUN(the_edt_ask_goes_out_in_place_of_stop_frames);
    RUN(the_edt_ask_never_replaces_a_throttle);
    RUN(plain_dshot_slots_and_dshot_ports_never_ask);
    RUN(the_end_of_a_run_forgets_the_readings);
    RUN(the_last_slot_and_the_last_port_render);
    RUN(a_watched_channel_stamps_its_first_changed_pulse_once);
    RUN(a_dshot_slot_is_due_at_once_after_a_long_disarm);
    RUN(a_ppm_slot_writes_at_most_eight_channels);
    RUN(the_last_slot_port_and_channel_count);
    RUN(a_slot_bound_afresh_has_no_pulse_to_change_from);
    RUN(null_arguments_change_nothing);
    return test_summary("out_render");
}
