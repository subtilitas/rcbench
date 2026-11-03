/**
 * @file
 * @brief The UI's view of the programming ports; see port_view.h.
 *
 * The programming ports as the screens see them.  See port_view.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "port_view.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"

/** @brief The view the screens draw. */
static port_view_t      g_view = {
    .count = PORT_VIEW_UNKNOWN,
    .pin   = { PORT_VIEW_NO_PIN, PORT_VIEW_NO_PIN, PORT_VIEW_NO_PIN,
               PORT_VIEW_NO_PIN },
    .func  = { PORT_VIEW_FN_UNKNOWN, PORT_VIEW_FN_UNKNOWN,
               PORT_VIEW_FN_UNKNOWN, PORT_VIEW_FN_UNKNOWN },
};
_Static_assert(LINK_PG_PORTS_MAX == 4u, "g_view's initialiser");
/** @brief Counts the changes of what the screens draw. */
static uint32_t         g_rev;
/** @brief Where an ask goes. */
static port_view_ask_fn g_ask;
/** @brief The last ask's number. */
static uint8_t          g_seq;
/** @brief Where it stands: a prog_link_ask_state_t. */
static uint8_t          g_ask_state = (uint8_t)PROG_LINK_ASK_IDLE;
/** @brief What it waits on: a prog_link_wait_t. */
static uint8_t          g_ask_wait  = (uint8_t)PROG_LINK_WAIT_NONE;
/** @brief Its port. */
static uint8_t          g_ask_port;
/** @brief Its kind: a prog_link_ask_kind_t. */
static uint8_t          g_ask_kind;

void port_view_unknown(port_view_t *v)
{
    if (v == NULL) {
        return;
    }
    memset(v, 0, sizeof(*v));
    v->count = PORT_VIEW_UNKNOWN;
    for (unsigned n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        v->pin[n]  = PORT_VIEW_NO_PIN;
        v->func[n] = PORT_VIEW_FN_UNKNOWN;
    }
}

void port_view_set(const port_view_t *v)
{
    if (v != NULL && memcmp(v, &g_view, sizeof(g_view)) != 0) {
        g_view = *v;
        ++g_rev;
    }
}

const port_view_t *port_view_get(void) { return &g_view; }

uint32_t port_view_revision(void) { return g_rev; }

bool port_view_has(uint8_t port)
{
    return g_view.count != PORT_VIEW_UNKNOWN && port < g_view.count
           && port < LINK_PG_PORTS_MAX;
}

uint8_t port_view_func(uint8_t port)
{
    return port_view_has(port) ? g_view.func[port]
                               : (uint8_t)PORT_VIEW_FN_UNKNOWN;
}

bool port_view_pwm(uint8_t port)
{
    const uint8_t fn = port_view_func(port);
    return fn == (uint8_t)LINK_PG_FN_SERVO_PWM
           || fn == (uint8_t)PORT_VIEW_FN_UNKNOWN;
}

bool port_view_dshot(uint8_t port)
{
    return LINK_PG_FN_IS_DSHOT(port_view_func(port));
}

const char *port_view_fn_name(uint8_t fn)
{
    switch (fn) {
    case LINK_PG_FN_SERVO_PWM: return TR(OUT_PORT_FN_PWM);
    case LINK_PG_FN_NONE:      return TR(OUT_PORT_FN_NONE);
    case LINK_PG_FN_DSHOT150:  return "DSHOT150";
    case LINK_PG_FN_DSHOT300:  return "DSHOT300";
    case LINK_PG_FN_DSHOT600:  return "DSHOT600";
    default:                   return "?";
    }
}

void port_view_set_ask(port_view_ask_fn fn) { g_ask = fn; }

bool port_view_ask(uint8_t port, prog_link_ask_kind_t kind, uint16_t value)
{
    if (g_ask == NULL || !port_view_has(port)
        || (kind == PROG_LINK_ASK_FUNC
            && (!g_view.has_func
                || value >= (g_view.has_dshot
                                 ? (uint16_t)LINK_PG_FN_COUNT
                                 : (uint16_t)LINK_PG_FN_COUNT_V426)))) {
        return false;
    }
    /* 1 to 255: 0 stands for no ask in the word that carries it. */
    const uint8_t seq = (g_seq == 0xFFu) ? 1u : (uint8_t)(g_seq + 1u);
    if (!g_ask(seq, port, kind, value)) {
        return false;
    }
    g_seq       = seq;
    g_ask_state = (uint8_t)PROG_LINK_ASK_WAITING;
    g_ask_wait  = (uint8_t)PROG_LINK_WAIT_NONE;
    g_ask_port  = port;
    g_ask_kind  = (uint8_t)kind;
    ++g_rev;
    return true;
}

void port_view_set_ask_state(uint8_t seq, prog_link_ask_state_t st)
{
    if (seq != g_seq || g_ask_state == (uint8_t)PROG_LINK_ASK_IDLE
        || (uint8_t)st == g_ask_state || st == PROG_LINK_ASK_IDLE) {
        return;
    }
    g_ask_state = (uint8_t)st;
    ++g_rev;
}

void port_view_set_ask_wait(prog_link_wait_t wait)
{
    if ((uint8_t)wait != g_ask_wait) {
        g_ask_wait = (uint8_t)wait;
        ++g_rev;
    }
}

/**
 * @brief The line of an ask waiting: what holds it, or the rules of a wait not
 *        yet judged, for at most a pass of the control task.
 *
 * @return The text.
 */
static const char *wait_line(void)
{
    switch ((prog_link_wait_t)g_ask_wait) {
    case PROG_LINK_WAIT_READ:  return TR(OUT_PORT_WAIT_READ);
    case PROG_LINK_WAIT_KST:   return TR(OUT_PORT_WAIT_KST);
    case PROG_LINK_WAIT_HITEC: return TR(OUT_PORT_WAIT_HITEC);
    case PROG_LINK_WAIT_ESC:   return TR(OUT_PORT_WAIT_ESC);
    case PROG_LINK_WAIT_SAFE:  return TR(OUT_PORT_WAIT_SAFE);
    case PROG_LINK_WAIT_TEST:  return TR(OUT_PORT_WAIT_TEST);
    case PROG_LINK_WAIT_NONE:
    default:                   return TR(OUT_PORT_ASK_WAIT);
    }
}

prog_link_ask_state_t port_view_ask_state(uint8_t *port, uint8_t *kind)
{
    if (port != NULL) {
        *port = g_ask_port;
    }
    if (kind != NULL) {
        *kind = g_ask_kind;
    }
    return (prog_link_ask_state_t)g_ask_state;
}

uint8_t port_view_ask_seq(void) { return g_seq; }

bool port_view_ask_line(uint8_t port, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (port != g_ask_port) {
        return false;
    }
    const char *line = NULL;
    switch ((prog_link_ask_state_t)g_ask_state) {
    case PROG_LINK_ASK_WAITING: line = wait_line();              break;
    case PROG_LINK_ASK_REFUSED: line = TR(OUT_PORT_ASK_REFUSED); break;
    case PROG_LINK_ASK_LOST:    line = TR(OUT_PORT_ASK_LOST);    break;
    case PROG_LINK_ASK_IDLE:
    case PROG_LINK_ASK_DONE:
    default:                                                     break;
    }
    if (line == NULL) {
        return false;
    }
    snprintf(buf, cap, "%s", line);
    return true;
}
