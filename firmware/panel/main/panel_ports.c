/*
 * The programming ports between the control task and the screens: the
 * PROG page as both views of OUTPUTS draw it, and the operator's asks
 * for a port's PIN or FUNC, through one mailbox (port_mail.h).  Nothing
 * here writes the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel.h"

#include "outputs_screen.h"
#include "picker_screen.h"
#include "port_mail.h"
#include "port_view.h"
#include "prog_registry.h"

static port_mail_t s_mail = PORT_MAIL_INIT;

void prog_ports_publish(void) { port_mail_publish(&s_mail, &s_prog_link); }

void prog_ports_lost(void) { port_mail_lost(&s_mail); }

/* port_view's ask hook. */
static bool ask_post(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                     uint16_t value)
{
    return port_mail_post(&s_mail, seq, port, kind, value);
}

void prog_ports_init_screens(void)
{
    port_view_set_ask(ask_post);
}

void prog_ports_to_screens(void)
{
    port_mail_to_view(&s_mail);

    /* The PROGRAM list's ports: the one the tester drives, and those the
     * HITEC page holds, from one word. */
    const unsigned drive = atomic_load(&s_drive);
    const unsigned target = (drive >> 8) & 0xFFu;
    prog_registry_set_ports(
        (target >= 1u && target <= LINK_PG_PORTS_MAX)
            ? (uint8_t)(target - 1u) : (uint8_t)PROG_REG_NO_PORT,
        (uint8_t)((drive >> DRIVE_HELD_SHIFT) & 0xFu));

    /* The pins the IOmcu holds, to both views of the binding, as before
     * there were functions: a refused pin is none. */
    const uint8_t prog_pin = (uint8_t)atomic_load(&s_prog_pin);
    outputs_screen_set_prog_pin(prog_pin);
    const unsigned ports = atomic_load(&s_prog_ports);
    uint8_t held[LINK_PG_PORTS_MAX];
    held[0] = prog_pin;
    for (unsigned n = 1; n < LINK_PG_PORTS_MAX; ++n) {
        held[n] = (uint8_t)(ports >> (8u * n));
    }
    const uint8_t held_count = (uint8_t)(ports & 0xFFu);
    outputs_screen_set_prog_ports(held_count, held);
    /* While the count is not known the picker holds port 0's pin, as the
     * PIN register reads it. */
    picker_screen_set_prog_ports(
        (held_count == OUTPUTS_PORTS_UNKNOWN) ? 1u : held_count, held);
}
