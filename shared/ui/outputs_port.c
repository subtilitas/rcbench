/*
 * The OUTPUTS screen's PROG PORT entries: the IOmcu's programming ports as
 * outputs.  See outputs_screen.h.
 *
 * One entry a port the IOmcu reports; outputs_port_entry.c draws the one
 * shown.  This file holds what the entries and PICK A PIN share: the
 * ports' pins, the pins SETUP > INTERFACES holds, the entry the screen
 * opens on.
 *
 * The lines that name a PWM pin the IOmcu refuses beside a port's pin or a
 * bound PWM pin (outputs_clash_label(), outputs_clash_reason()) are here
 * too, for OUTPUTS and PICK A PIN alike.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "settings.h"
#include "settings_screen.h"
#include "sensors_rows.h"
#include "settings_sensors.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "outputs_screen_int.h"
#include "port_view.h"

/* The GPIO the IOmcu's programming port 1 is bound to, or none. */
static uint8_t g_prog_gpio = OUTPUTS_NO_PIN;
/* The ports the IOmcu reports, OUTPUTS_PORTS_UNKNOWN while not known, and
 * the pins of ports 2 to 4 (port 1's is g_prog_gpio). */
static uint8_t g_port_count = OUTPUTS_PORTS_UNKNOWN;
static uint8_t g_port_pin[LINK_PG_PORTS_MAX] = {
    OUTPUTS_NO_PIN, OUTPUTS_NO_PIN, OUTPUTS_NO_PIN, OUTPUTS_NO_PIN
};
_Static_assert(LINK_PG_PORTS_MAX == 4u, "g_port_pin's initialiser");

static void touched(void)
{
    outputs_port_touched();
}

void outputs_screen_set_prog_pin(uint8_t gpio)
{
    if (gpio != g_prog_gpio) {
        g_prog_gpio = gpio;
        touched();
    }
}

uint8_t outputs_screen_port_entries(void)
{
    if (g_port_count == OUTPUTS_PORTS_UNKNOWN) {
        return 1u;
    }
    return (g_port_count > LINK_PG_PORTS_MAX) ? (uint8_t)LINK_PG_PORTS_MAX
                                              : g_port_count;
}

uint8_t outputs_screen_port_pin(uint8_t port)
{
    if (port >= outputs_screen_port_entries()) {
        return OUTPUTS_NO_PIN;
    }
    return (port == 0u) ? g_prog_gpio : g_port_pin[port];
}

void outputs_screen_set_prog_ports(uint8_t count, const uint8_t *pins)
{
    const bool known = count != OUTPUTS_PORTS_UNKNOWN;
    bool moved = count != g_port_count;
    g_port_count = count;
    for (uint8_t n = 1; n < LINK_PG_PORTS_MAX; ++n) {
        const uint8_t pin = (known && pins != NULL && n < count)
                                ? pins[n] : OUTPUTS_NO_PIN;
        moved = moved || pin != g_port_pin[n];
        g_port_pin[n] = pin;
    }
    /* Port 1's pin while the count is not known: the PIN register's, as
     * outputs_screen_set_prog_pin() was given it. */
    if (known) {
        outputs_screen_set_prog_pin((pins != NULL && count > 0u)
                                        ? pins[0] : OUTPUTS_NO_PIN);
    } else if (pins != NULL) {
        outputs_screen_set_prog_pin(pins[0]);
    }
    /* A port the IOmcu no longer reports is no entry to stay on. */
    if (outputs_screen_port_shown() >= (int)outputs_screen_port_entries()) {
        outputs_screen_select(0u);
        moved = true;
    }
    if (moved) {
        touched();
    }
}

/* ------------------------------------------------- SETUP > INTERFACES */

/* Set by the control task (panel_poll.c), read by the task that draws:
 * 0 while what the page holds is not known, else bit 0 set and each pin
 * + 1 in bits 8 to 15 and 16 to 23, 0 for none. */
static atomic_int g_held[OUTPUTS_IFACE_COUNT];

/* Pin @p v + 1 in a byte, 0 for none and anything past the mask. */
static int held_byte(int v)
{
    return (v >= 0 && v < 64) ? v + 1 : 0;
}

void outputs_screen_set_iface_held(outputs_iface_t which, bool known,
                                   int pin_a, int pin_b)
{
    if (which <= OUTPUTS_IFACE_NONE || which >= OUTPUTS_IFACE_COUNT) {
        return;
    }
    atomic_store(&g_held[which],
                 known ? (1 | (held_byte(pin_a) << 8)
                          | (held_byte(pin_b) << 16))
                       : 0);
}

/* Whether part @p en of the sensor bus is on and the IOmcu has not
 * refused it. */
static bool sense_part(setting_id_t en)
{
    return settings_get_bool(en) && !settings_screen_unheld(en);
}

/* Whether an INA3221 part is fitted on SETUP > SENSORS and its rows are
 * not marked: a part the page refused, dark or on the INA228's address
 * holds no bus (sensors_rows.h). */
static bool sensors_on(void)
{
    const uint8_t parts = settings_sensors_fitted();
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        if ((parts & (1u << a)) != 0u
            && !sensors_rows_unheld(SET_INA_W(3 * a))) {
            return true;
        }
    }
    return false;
}

/* GPIO @p v as a mask bit, or 0 for -1 and anything past the mask. */
static uint64_t pin_bit(int v)
{
    return (v >= 0 && v < 64) ? ((uint64_t)1u << (unsigned)v) : 0u;
}

uint64_t outputs_iface_pins(outputs_iface_t which)
{
    if (which > OUTPUTS_IFACE_NONE && which < OUTPUTS_IFACE_COUNT) {
        const int held = atomic_load(&g_held[which]);
        if (held != 0) {
            return pin_bit(((held >> 8) & 0xff) - 1)
                   | pin_bit(((held >> 16) & 0xff) - 1);
        }
    }
    /* Not read yet: as the settings stand. */
    switch (which) {
    case OUTPUTS_IFACE_SENSE: {
        /* A bus the IOmcu refused, its pins row or every part on marked
         * on SETUP, holds no pin there: the page enables no part. */
        const bool on = sense_part(SET_INA228_EN)
                        || sensors_on()
                        || sense_part(SET_ENC_EN);
        const int sda = settings_get_int(SET_SENSE_SDA);
        if (!on || sda < 0 || settings_screen_unheld(SET_SENSE_SDA)) {
            return 0u;
        }
        return pin_bit(sda) | pin_bit(settings_get_int(SET_SENSE_SCL));
    }
    case OUTPUTS_IFACE_PDMINI: {
        /* The page is written ON only with both pins set and apart
         * (supply_link.c); otherwise it holds neither. */
        const int tx = settings_get_int(SET_PDMINI_TX);
        const int rx = settings_get_int(SET_PDMINI_RX);
        if (!settings_get_bool(SET_PDMINI_EN) || tx < 0 || rx < 0
            || tx == rx) {
            return 0u;
        }
        return pin_bit(tx) | pin_bit(rx);
    }
    case OUTPUTS_IFACE_TAP: {
        /* The TONE page refuses an ADC pin -- GP26 to GP29 of the
         * RP2350A, GP40 to GP47 of the RP2354B (tone_page.c) -- and a tap
         * the IOmcu refused for any reason, its rows marked on SETUP, holds
         * no pin there. */
        const int pin = settings_get_int(SET_TONE_PIN);
        const bool adc = (pin >= 26 && pin <= 29) || (pin >= 40 && pin <= 47);
        if (!settings_get_bool(SET_TONE_EN) || adc
            || settings_screen_unheld(SET_TONE_PIN)) {
            return 0u;
        }
        return pin_bit(pin);
    }
    case OUTPUTS_IFACE_NONE:
    case OUTPUTS_IFACE_COUNT:
    default:
        return 0u;
    }
}

outputs_iface_t outputs_iface_at(uint8_t gpio)
{
    const uint64_t bit = pin_bit(gpio);
    if (bit == 0u) {
        return OUTPUTS_IFACE_NONE;
    }
    for (int f = OUTPUTS_IFACE_SENSE; f < OUTPUTS_IFACE_COUNT; ++f) {
        if ((outputs_iface_pins((outputs_iface_t)f) & bit) != 0u) {
            return (outputs_iface_t)f;
        }
    }
    return OUTPUTS_IFACE_NONE;
}

const char *outputs_iface_name(outputs_iface_t which)
{
    switch (which) {
    case OUTPUTS_IFACE_SENSE:  return TR(OUT_IFACE_SENSE);
    case OUTPUTS_IFACE_PDMINI: return TR(OUT_IFACE_PDMINI);
    case OUTPUTS_IFACE_TAP:    return TR(OUT_IFACE_TAP);
    case OUTPUTS_IFACE_NONE:
    case OUTPUTS_IFACE_COUNT:
    default:                   return "";
    }
}

void outputs_iface_read(uint64_t pins[OUTPUTS_IFACE_COUNT])
{
    pins[OUTPUTS_IFACE_NONE] = 0u;
    for (int f = OUTPUTS_IFACE_SENSE; f < OUTPUTS_IFACE_COUNT; ++f) {
        pins[f] = outputs_iface_pins((outputs_iface_t)f);
    }
}

bool outputs_port_holds(uint8_t gpio)
{
    for (uint8_t n = 0; n < outputs_screen_port_entries(); ++n) {
        const uint8_t pin = outputs_screen_port_pin(n);
        if (pin != OUTPUTS_NO_PIN && pin == gpio) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------- the entry opened on */

/* What the next entry opens on: the overview, a protocol, or a port. */
enum { OPEN_OVERVIEW = 0, OPEN_PROTO, OPEN_PORT };
static uint8_t g_open_kind = OPEN_OVERVIEW;
static uint8_t g_open_value;

void outputs_screen_open_on(uint8_t proto)
{
    g_open_kind  = OPEN_PROTO;
    g_open_value = proto;
}

void outputs_screen_open_on_port(uint8_t port)
{
    g_open_kind  = OPEN_PORT;
    g_open_value = port;
}

void outputs_port_forget_open(void)
{
    g_open_kind = OPEN_OVERVIEW;
}

void outputs_port_open(void)
{
    const uint8_t kind = g_open_kind, value = g_open_value;
    g_open_kind = OPEN_OVERVIEW;
    if (kind == OPEN_PORT && value < outputs_screen_port_entries()) {
        outputs_screen_select_port(value);
    } else if (kind == OPEN_PROTO && value < OUTBIND_PROTOS) {
        outputs_screen_select(value);
    } else {
        outputs_screen_select(0u);
    }
}

/* -------------------------------------------------------- the clashes */

const uint8_t *outputs_pwm_pins(const uint8_t *pins)
{
    static uint8_t pwm[LINK_PG_PORTS_MAX];
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        /* A function not known counts as SERVO PWM: the stricter rule.
         * NONE and DShot pins are under no PWM rule. */
        pwm[n] = (pins != NULL && port_view_pwm(n)) ? pins[n]
                                                    : OUTBIND_NO_GPIO;
    }
    return pwm;
}

const uint8_t *outputs_port_pins(void)
{
    uint8_t pins[LINK_PG_PORTS_MAX];
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        pins[n] = outputs_screen_port_pin(n);
    }
    return outputs_pwm_pins(pins);
}

bool outputs_clash_label(const outbind_t *b, uint8_t index,
                         const uint8_t *ports, uint8_t count, char *buf,
                         size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    uint8_t with;
    switch (outbind_pwm_clash_ports(b, index, ports, count, &with)) {
    case OUTBIND_CLASH_PROG_SLICE:
        snprintf(buf, cap, TR(OUT_SLICE_OF), (unsigned)with);
        return true;
    case OUTBIND_CLASH_COMPARE:
        snprintf(buf, cap, TR(OUT_COMPARE_OF), (unsigned)with);
        return true;
    case OUTBIND_CLASH_NONE:
    default:
        return false;
    }
}

bool outputs_clash_reason(const outbind_t *b, int index,
                          const uint8_t *ports, uint8_t count, char *buf,
                          size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (b == NULL) {
        return false;
    }
    if (index < 0 || index >= (int)outbind_pin_count(b->board)) {
        return false;
    }
    uint8_t with;
    const outbind_clash_t why = outbind_pwm_clash_ports(b, (uint8_t)index,
                                                        ports, count, &with);
    if (why == OUTBIND_CLASH_NONE) {
        return false;
    }
    snprintf(buf, cap,
             (why == OUTBIND_CLASH_PROG_SLICE) ? TR(OUT_SLICE_WHY)
                                               : TR(OUT_COMPARE_WHY),
             (unsigned)outbind_pins(b->board)[index].gpio, (unsigned)with);
    return true;
}
