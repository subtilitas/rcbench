/**
 * @file picker_tag.c
 * @brief OUTPUTS' board view: the tag under a grey key's number that names
 *        what holds the pin.  See picker_screen_pin_tag() in picker_screen.h.
 *
 * The tag is a role word of at most 5 cells of 8 x 16: ESC for MOTOR PWM
 * and the four DShot entries, SERVO for SERVO PWM, PPM, PORT or PORT1 to
 * PORT4 for a programming port, BUS, PD and TAP for the interfaces.  IN USE
 * names the exact protocol.  A key grey for a PWM clash, a full protocol
 * or a binding not read carries none.
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen.h"

#include <stdio.h>

#include "outputs_screen.h"
#include "picker_port.h"
#include "port_view.h"
#include "ui_text.h"

#include "picker_screen_int.h"

/**
 * @brief The role word of protocol @p proto.
 * @param proto the protocol's index in the catalogue, not OFF
 * @return PPM for the PPM driver, ESC for a throttle, SERVO otherwise
 */
static const char *proto_tag(uint8_t proto)
{
    const outbind_proto_t *k = &outbind_protos()[proto];
    if (k->driver == OUT_DRIVER_PPM) {
        return TR(PK_TAG_PPM);
    }
    return (k->role == OUT_ROLE_THROTTLE) ? TR(PK_TAG_ESC) : TR(PK_TAG_SERVO);
}

/**
 * @brief The tag of interface @p f.
 * @param f the interface
 * @return BUS, PD or TAP; NULL for none
 */
static const char *iface_tag(outputs_iface_t f)
{
    switch (f) {
    case OUTPUTS_IFACE_SENSE:  return TR(PK_TAG_BUS);
    case OUTPUTS_IFACE_PDMINI: return TR(PK_TAG_PD);
    case OUTPUTS_IFACE_TAP:    return TR(PK_TAG_TAP);
    case OUTPUTS_IFACE_NONE:
    case OUTPUTS_IFACE_COUNT:
    default:                   return NULL;
    }
}

/**
 * @brief Writes the tag of programming port @p port: PORT on a bench with
 *        one port, PORT and its number from 1 on one with more.
 * @param port the port, from 0
 * @param buf takes the tag
 * @param cap bytes at @p buf
 */
static void port_tag(uint8_t port, char *buf, size_t cap)
{
    if (outputs_screen_port_entries() <= 1u) {
        snprintf(buf, cap, "%s", TR(PK_TAG_PORT));
    } else {
        snprintf(buf, cap, "%s%u", TR(PK_TAG_PORT), (unsigned)port + 1u);
    }
}

/**
 * @brief The holder's tag on a PROG PORT entry's view: another port's pin,
 *        a protocol's, an interface's; in picker_port_pin()'s order.
 * @param index the pin's catalogue entry
 * @param buf takes the tag
 * @param cap bytes at @p buf
 * @return true when the holder has a tag
 */
static bool port_view_tag(uint8_t index, char *buf, size_t cap)
{
    const uint8_t gpio = outbind_pins(s.bind.board)[index].gpio;
    const int other = picker_port_other(gpio, (uint8_t)s.port);
    if (other >= 0) {
        port_tag((uint8_t)other, buf, cap);
        return true;
    }
    const uint8_t held = outbind_group_of(&s.bind, index);
    if (held != 0u) {
        snprintf(buf, cap, "%s", proto_tag(held));
        return true;
    }
    const char *f = iface_tag(outputs_iface_at(gpio));
    if (f != NULL) {
        snprintf(buf, cap, "%s", f);
        return true;
    }
    return false;
}

bool picker_screen_pin_tag(uint8_t index, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (picker_screen_pin(index, NULL, 0) != PICKER_PIN_HELD) {
        return false;
    }
    if (s.port >= 0) {
        return port_view_tag(index, buf, cap);
    }
    /* picker_screen_pin()'s order: the ports, the interfaces, a protocol. */
    if (pk_prog_taken(index)) {
        const uint8_t gpio = outbind_pins(s.bind.board)[index].gpio;
        uint8_t n = 0u;
        while (n + 1u < LINK_PG_PORTS_MAX && s.prog_pins[n] != gpio) {
            ++n;
        }
        port_tag(n, buf, cap);
        return true;
    }
    const char *f = iface_tag(pk_iface_taken(index));
    if (f != NULL) {
        snprintf(buf, cap, "%s", f);
        return true;
    }
    const uint8_t held = outbind_group_of(&s.bind, index);
    if (held != 0u && held != pk_proto_shown()) {
        snprintf(buf, cap, "%s", proto_tag(held));
        return true;
    }
    return false;               /* a PWM clash: the tap says why */
}
