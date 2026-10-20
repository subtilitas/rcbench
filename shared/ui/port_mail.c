/*
 * The programming ports between the control task and the UI task.  See
 * port_mail.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "port_mail.h"

#include <stddef.h>

#include "link_pages.h"
#include "port_view.h"

/* meta: bits 0 to 15 a nibble a port, bit 0 NONE (else SERVO PWM), bit 1
 * refused, bit 3 not known; bits 16 to 23 the count, 0xFF not known; bit
 * 24 the IOmcu takes FUNC; bits 25 to 27 the link epoch. */
#define META_EPOCH_SHIFT 25u
/* ask: bits 24 to 31 the number, 21 to 23 the epoch, bit 20 FUNC, bits 16
 * to 17 the port, bits 0 to 15 the value. */
#define ASK_EPOCH_SHIFT  21u
#define EPOCH_MASK       0x7u
/* Tries at an untorn pair before the last view stays. */
#define VIEW_TRIES       8

_Static_assert(LINK_PG_PORTS_MAX == 4u,
               "the view packs 4 ports into two 32-bit words");

static void view_store(port_mail_t *m, unsigned pins, unsigned meta)
{
    atomic_fetch_add(&m->gen, 1u);
    atomic_store(&m->pins, pins);
    atomic_store(&m->meta, meta);
    atomic_fetch_add(&m->gen, 1u);
}

static void state_store(port_mail_t *m, uint8_t seq, prog_link_ask_state_t st)
{
    atomic_store(&m->ask_state, ((unsigned)seq << 8) | (unsigned)st);
}

/* Whether the word on ask @p seq is final: a dropped or refused ask keeps
 * its word, whatever prog_link holds from an older one. */
static bool settled(const port_mail_t *m, uint8_t seq)
{
    const unsigned now = atomic_load(&m->ask_state);
    return (now >> 8) == (unsigned)seq
           && (now & 0xFFu) != (unsigned)PROG_LINK_ASK_WAITING;
}

static void take_ask(port_mail_t *m, prog_link_t *pl)
{
    const unsigned ask = atomic_load(&m->ask);
    const uint8_t seq = (uint8_t)(ask >> 24);
    if (seq == 0u) {
        return;
    }
    if (seq != m->taken) {
        m->taken = seq;
        if (((ask >> ASK_EPOCH_SHIFT) & EPOCH_MASK)
            != (m->epoch & EPOCH_MASK)) {
            /* Made on the view of a link since lost. */
            state_store(m, seq, PROG_LINK_ASK_LOST);
            return;
        }
        if (!prog_link_ask(pl, (uint8_t)((ask >> 16) & 0x3u),
                           ((ask >> 20) & 1u) != 0u ? PROG_LINK_ASK_FUNC
                                                    : PROG_LINK_ASK_PIN,
                           (uint16_t)(ask & 0xFFFFu))) {
            state_store(m, seq, PROG_LINK_ASK_REFUSED);
            return;
        }
    }
    const prog_link_ask_state_t st =
        prog_link_ask_state(pl, NULL, NULL, NULL);
    if (st != PROG_LINK_ASK_IDLE && !settled(m, seq)) {
        state_store(m, seq, st);
    }
}

void port_mail_publish(port_mail_t *m, prog_link_t *pl)
{
    if (m == NULL || pl == NULL) {
        return;
    }
    take_ask(m, pl);

    const uint8_t count = prog_link_count(pl);
    unsigned pins = 0xFFFFFFFFu;
    unsigned meta = PORT_MAIL_META_UNKNOWN;
    if (count != PROG_LINK_COUNT_UNKNOWN) {
        meta = ((unsigned)count << 16)
               | (prog_link_has_func(pl) ? (1u << 24) : 0u)
               | ((m->epoch & EPOCH_MASK) << META_EPOCH_SHIFT);
        for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
            prog_link_port_t pt;
            unsigned nib = 0x8u;
            if (prog_link_port(pl, n, &pt)) {
                pins = (pins & ~(0xFFu << (8u * n)))
                       | ((unsigned)pt.pin << (8u * n));
                nib = ((pt.func == (uint8_t)LINK_PG_FN_NONE) ? 1u : 0u)
                      | (pt.refused ? 2u : 0u);
            }
            meta |= nib << (4u * n);
        }
    }
    view_store(m, pins, meta);
}

void port_mail_lost(port_mail_t *m)
{
    if (m == NULL) {
        return;
    }
    ++m->epoch;
    view_store(m, 0xFFFFFFFFu, PORT_MAIL_META_UNKNOWN);
    const uint8_t seq = (uint8_t)(atomic_load(&m->ask) >> 24);
    if (seq == 0u) {
        return;
    }
    /* Posted and not taken, or taken and waiting: LOST either way. */
    m->taken = seq;
    if (!settled(m, seq)) {
        state_store(m, seq, PROG_LINK_ASK_LOST);
    }
}

bool port_mail_post(port_mail_t *m, uint8_t seq, uint8_t port,
                    prog_link_ask_kind_t kind, uint16_t value)
{
    if (m == NULL || seq == 0u) {
        return false;     /* 0 is "none yet": the number wraps past it */
    }
    atomic_store(&m->ask,
                 ((unsigned)seq << 24)
                     | ((m->ui_epoch & EPOCH_MASK) << ASK_EPOCH_SHIFT)
                     | (kind == PROG_LINK_ASK_FUNC ? (1u << 20) : 0u)
                     | ((unsigned)(port & 0x3u) << 16) | value);
    return true;
}

void port_mail_to_view(port_mail_t *m)
{
    if (m == NULL) {
        return;
    }
    unsigned pins = 0u, meta = 0u;
    bool whole = false;
    for (int k = 0; k < VIEW_TRIES && !whole; ++k) {
        const unsigned g = atomic_load(&m->gen);
        pins = atomic_load(&m->pins);
        meta = atomic_load(&m->meta);
        whole = (g & 1u) == 0u && g == atomic_load(&m->gen);
    }
    if (whole) {
        port_view_t v;
        port_view_unknown(&v);
        const uint8_t count = (uint8_t)(meta >> 16);
        if (count != PROG_LINK_COUNT_UNKNOWN) {
            m->ui_epoch = (meta >> META_EPOCH_SHIFT) & EPOCH_MASK;
            v.count     = count;
            v.has_func  = ((meta >> 24) & 1u) != 0u;
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
    }
    const unsigned st = atomic_load(&m->ask_state);
    port_view_set_ask_state((uint8_t)(st >> 8),
                            (prog_link_ask_state_t)(st & 0xFFu));
}
