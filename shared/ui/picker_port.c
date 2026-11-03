/**
 * @file
 * @brief OUTPUTS' board view on a programming port.  See picker_port.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "picker_port.h"

#include <stdio.h>
#include <string.h>

#include "out_pwm_map.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "ui_text.h"

#include "outputs_screen_int.h"

/* Why a pin clashes with a SERVO PWM port's rules. */
typedef enum {
    CLASH_NONE = 0,
    CLASH_SLICE,      /* the PWM slice of a bound PWM output */
    CLASH_COMPARE,    /* the compare register of another SERVO PWM port */
} clash_t;

static bool port_pwm(uint8_t port)
{
    return port_view_pwm(port);
}

/* Whether port @p n holds the pin its PIN register names: one the IOmcu
 * refused is shown, not held. */
static bool holds(const port_view_t *v, uint8_t n)
{
    return port_view_has(n) && v->pin[n] != PORT_VIEW_NO_PIN
           && (v->refused & (1u << n)) == 0u;
}

static clash_t clash(const outbind_t *b, uint8_t index, uint8_t port,
                     uint8_t *with)
{
    if (!port_pwm(port)) {
        return CLASH_NONE;
    }
    const uint8_t gpio = outbind_pins(b->board)[index].gpio;
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(b, regs);
    for (uint8_t sl = 0; sl < used; ++sl) {
        const uint16_t *r = &regs[(size_t)sl * LINK_OS_STRIDE];
        if (r[LINK_OS_DRIVER] == (uint16_t)OUT_DRIVER_PWM
            && r[LINK_OS_PIN] <= OUT_MAX_PIN
            && out_pwm_same_slice((uint8_t)r[LINK_OS_PIN], gpio)) {
            *with = (uint8_t)r[LINK_OS_PIN];
            return CLASH_SLICE;
        }
    }
    const port_view_t *v = port_view_get();
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        if (n != port && holds(v, n) && port_pwm(n) && v->pin[n] != gpio
            && out_pwm_same_compare(v->pin[n], gpio)) {
            *with = v->pin[n];
            return CLASH_COMPARE;
        }
    }
    return CLASH_NONE;
}

static void hold(char *holder, size_t cap, const char *what)
{
    if (holder != NULL && cap != 0u) {
        snprintf(holder, cap, "%s", what);
    }
}

picker_pin_t picker_port_pin(const outbind_t *b, uint8_t index, uint8_t port,
                             bool locked, char *holder, size_t cap)
{
    hold(holder, cap, "");
    if (b == NULL || index >= outbind_pin_count(b->board)) {
        return PICKER_PIN_NONE;
    }
    if (!port_view_has(port)) {
        return PICKER_PIN_LOCKED;
    }
    const port_view_t *v = port_view_get();
    const uint8_t gpio = outbind_pins(b->board)[index].gpio;
    if (v->pin[port] == gpio) {
        return locked ? PICKER_PIN_LOCKED : PICKER_PIN_TICKED;
    }
    char name[24];
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        if (n != port && holds(v, n) && v->pin[n] == gpio) {
            outputs_port_name(n, name, sizeof(name));
            hold(holder, cap, name);
            return PICKER_PIN_HELD;
        }
    }
    const uint8_t g = outbind_group_of(b, index);
    if (g != 0u) {
        hold(holder, cap, outbind_protos()[g].name);
        return PICKER_PIN_HELD;
    }
    const outputs_iface_t f = outputs_iface_at(gpio);
    if (f != OUTPUTS_IFACE_NONE) {
        hold(holder, cap, outputs_iface_name(f));
        return PICKER_PIN_HELD;
    }
    if (locked) {
        return PICKER_PIN_LOCKED;
    }
    uint8_t with = 0u;
    switch (clash(b, index, port, &with)) {
    case CLASH_SLICE:
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, TR(OUT_SLICE_OF), (unsigned)with);
        }
        return PICKER_PIN_HELD;
    case CLASH_COMPARE:
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, TR(OUT_COMPARE_OF), (unsigned)with);
        }
        return PICKER_PIN_HELD;
    case CLASH_NONE:
    default:
        return PICKER_PIN_FREE;
    }
}

bool picker_port_tap(const outbind_t *b, uint8_t index, uint8_t port,
                     picker_pin_t st)
{
    if (b == NULL || index >= outbind_pin_count(b->board)) {
        return false;
    }
    if (st == PICKER_PIN_FREE) {
        return port_view_ask(port, PROG_LINK_ASK_PIN,
                             outbind_pins(b->board)[index].gpio);
    }
    if (st == PICKER_PIN_TICKED) {
        return port_view_ask(port, PROG_LINK_ASK_PIN, LINK_PG_PIN_NONE);
    }
    return false;
}

bool picker_port_clashes(const outbind_t *b, uint8_t index, uint8_t port)
{
    uint8_t with;
    return b != NULL && index < outbind_pin_count(b->board)
           && clash(b, index, port, &with) != CLASH_NONE;
}

bool picker_port_reason(const outbind_t *b, uint8_t port, int refused,
                        char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (!port_view_has(port)) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_NOT_READ));
        return true;
    }
    if (port_view_ask_line(port, buf, cap)) {
        return true;
    }
    if (b == NULL || refused < 0
        || refused >= (int)outbind_pin_count(b->board)) {
        return false;
    }
    uint8_t with = 0u;
    const uint8_t gpio = outbind_pins(b->board)[refused].gpio;
    switch (clash(b, (uint8_t)refused, port, &with)) {
    case CLASH_SLICE:
        snprintf(buf, cap, TR(PK_PORT_SLICE_WHY), (unsigned)gpio,
                 (unsigned)with);
        return true;
    case CLASH_COMPARE:
        snprintf(buf, cap, TR(OUT_COMPARE_WHY), (unsigned)gpio,
                 (unsigned)with);
        return true;
    case CLASH_NONE:
    default:
        return false;
    }
}

bool picker_port_row(const outbind_t *b, uint8_t k, picker_row_t *out)
{
    const uint8_t n = outbind_pin_count(b->board);
    for (uint8_t i = 0; i < n && k == 0u; ++i) {
        if (picker_screen_pin(i, NULL, 0) != PICKER_PIN_TICKED) {
            continue;
        }
        if (out != NULL) {
            const outbind_pin_t *pin = &outbind_pins(b->board)[i];
            out->index   = i;
            out->gpio    = pin->gpio;
            out->channel = -1;
            out->pad     = pin->pad;
        }
        return true;
    }
    return false;
}

bool picker_port_own_pin(const outbind_t *b, uint8_t index, int port)
{
    return port >= 0 && port < (int)LINK_PG_PORTS_MAX
           && port_view_get()->pin[port] == outbind_pins(b->board)[index].gpio;
}
