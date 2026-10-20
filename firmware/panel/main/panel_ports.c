/*
 * The programming ports between the control task and the screens: the
 * PROG page as OUTPUTS and PICK A PIN draw it, and the operator's asks
 * for a port's PIN or FUNC (port_view.h, prog_link.h).
 *
 * The control task reads the page (prog_link_service()) and publishes it
 * here in two words, so a frame never draws one read's pins with
 * another's functions.  A screen's ask crosses as one word with a number;
 * the control task hands it to prog_link_ask() once and says how it
 * stands under the same number.  Nothing here writes the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel.h"

#include "outputs_screen.h"
#include "picker_screen.h"
#include "port_view.h"

/* Port n's PIN register in byte n, PROG_LINK_NO_PIN for none. */
static atomic_uint s_view_pins = 0xFFFFFFFFu;
/* Bits 0 to 15: per port a nibble, bit 0 NONE (else SERVO PWM), bit 1
 * refused, bit 3 not known; bits 16 to 23 the count, 0xFF not known; bit
 * 24 the IOmcu takes FUNC. */
#define META_UNKNOWN 0x00FF8888u
static atomic_uint s_view_meta = META_UNKNOWN;
/* The screens' last ask: bits 24 to 31 its number, bit 20 FUNC, bits 16
 * to 17 the port, bits 0 to 15 the value.  0 for none yet. */
static atomic_uint s_ask;
/* How it stands: its number in bits 8 to 15, a prog_link_ask_state_t in
 * bits 0 to 7. */
static atomic_uint s_ask_state;
/* Control task only: the number of the last ask handed on. */
static uint8_t s_ask_taken;

_Static_assert(LINK_PG_PORTS_MAX == 4u,
               "the view packs 4 ports into two 32-bit words");

void prog_ports_publish(void)
{
    /* An ask from the screens, once. */
    const unsigned ask = atomic_load(&s_ask);
    const uint8_t seq = (uint8_t)(ask >> 24);
    if (seq != 0u && seq != s_ask_taken) {
        s_ask_taken = seq;
        const bool taken = prog_link_ask(
            &s_prog_link, (uint8_t)((ask >> 16) & 0x3u),
            ((ask >> 20) & 1u) != 0u ? PROG_LINK_ASK_FUNC : PROG_LINK_ASK_PIN,
            (uint16_t)(ask & 0xFFFFu));
        if (!taken) {
            atomic_store(&s_ask_state, ((unsigned)seq << 8)
                                           | (unsigned)PROG_LINK_ASK_REFUSED);
        }
    }
    if (seq != 0u && seq == s_ask_taken) {
        const prog_link_ask_state_t st =
            prog_link_ask_state(&s_prog_link, NULL, NULL, NULL);
        if (st != PROG_LINK_ASK_IDLE) {
            atomic_store(&s_ask_state, ((unsigned)seq << 8) | (unsigned)st);
        }
    }

    const uint8_t count = prog_link_count(&s_prog_link);
    unsigned pins = 0xFFFFFFFFu;
    unsigned meta = META_UNKNOWN;
    if (count != PROG_LINK_COUNT_UNKNOWN) {
        meta = ((unsigned)count << 16)
               | (prog_link_has_func(&s_prog_link) ? (1u << 24) : 0u);
        for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
            prog_link_port_t pt;
            unsigned nib = 0x8u;
            if (prog_link_port(&s_prog_link, n, &pt)) {
                pins = (pins & ~(0xFFu << (8u * n)))
                       | ((unsigned)pt.pin << (8u * n));
                nib = ((pt.func == (uint8_t)LINK_PG_FN_NONE) ? 1u : 0u)
                      | (pt.refused ? 2u : 0u);
            }
            meta |= nib << (4u * n);
        }
    }
    atomic_store(&s_view_pins, pins);
    atomic_store(&s_view_meta, meta);
}

void prog_ports_lost(void)
{
    atomic_store(&s_view_pins, 0xFFFFFFFFu);
    atomic_store(&s_view_meta, META_UNKNOWN);
    const unsigned st = atomic_load(&s_ask_state);
    if ((st & 0xFFu) == (unsigned)PROG_LINK_ASK_WAITING) {
        atomic_store(&s_ask_state, (st & 0xFF00u)
                                       | (unsigned)PROG_LINK_ASK_LOST);
    }
}

/* port_view's ask hook: the ask to the control task as one word. */
static bool ask_post(uint8_t seq, uint8_t port, prog_link_ask_kind_t kind,
                     uint16_t value)
{
    if (seq == 0u) {
        return false;     /* 0 is "none yet": the number wraps past it */
    }
    atomic_store(&s_ask, ((unsigned)seq << 24)
                             | (kind == PROG_LINK_ASK_FUNC ? (1u << 20) : 0u)
                             | ((unsigned)(port & 0x3u) << 16) | value);
    return true;
}

void prog_ports_init_screens(void)
{
    port_view_set_ask(ask_post);
}

void prog_ports_to_screens(void)
{
    /* The view, from one publication: a word pair that does not match on
     * a second look is read again. */
    unsigned pins = 0u, meta = 0u;
    for (int k = 0; k < 3; ++k) {
        pins = atomic_load(&s_view_pins);
        meta = atomic_load(&s_view_meta);
        if (pins == atomic_load(&s_view_pins)) {
            break;
        }
    }
    port_view_t v;
    port_view_unknown(&v);
    const uint8_t count = (uint8_t)(meta >> 16);
    if (count != PROG_LINK_COUNT_UNKNOWN) {
        v.count    = count;
        v.has_func = ((meta >> 24) & 1u) != 0u;
        for (unsigned n = 0; n < LINK_PG_PORTS_MAX; ++n) {
            const unsigned nib = (meta >> (4u * n)) & 0xFu;
            if ((nib & 0x8u) != 0u) {
                continue;
            }
            v.pin[n]  = (uint8_t)(pins >> (8u * n));
            v.func[n] = (nib & 1u) ? (uint8_t)LINK_PG_FN_NONE
                                   : (uint8_t)LINK_PG_FN_SERVO_PWM;
            if ((nib & 2u) != 0u) {
                v.refused |= (uint8_t)(1u << n);
            }
        }
    }
    port_view_set(&v);
    const unsigned st = atomic_load(&s_ask_state);
    port_view_set_ask_state((uint8_t)(st >> 8),
                            (prog_link_ask_state_t)(st & 0xFFu));

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
