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

/** @brief The ports as the screens see them, one instance per image. */
typedef struct {
    port_view_t      view;      /**< the view the screens draw */
    uint32_t         rev;       /**< counts the changes of what they draw */
    port_view_ask_fn ask_fn;    /**< where an ask goes */
    uint8_t          seq;       /**< the last ask's number */
    uint8_t          ask_state; /**< where it stands: a prog_link_ask_state_t */
    uint8_t          ask_wait;  /**< what it waits on: a prog_link_wait_t */
    uint8_t          ask_port;  /**< its port */
    uint8_t          ask_kind;  /**< its kind: a prog_link_ask_kind_t */
} port_view_state_t;

/** @brief The view's one instance. */
static port_view_state_t pv = {
    .view = {
        .count = PORT_VIEW_UNKNOWN,
        .pin   = { PORT_VIEW_NO_PIN, PORT_VIEW_NO_PIN, PORT_VIEW_NO_PIN,
                   PORT_VIEW_NO_PIN },
        .func  = { PORT_VIEW_FN_UNKNOWN, PORT_VIEW_FN_UNKNOWN,
                   PORT_VIEW_FN_UNKNOWN, PORT_VIEW_FN_UNKNOWN },
    },
    .ask_state = (uint8_t)PROG_LINK_ASK_IDLE,
    .ask_wait  = (uint8_t)PROG_LINK_WAIT_NONE,
};
_Static_assert(LINK_PG_PORTS_MAX == 4u, "pv.view's initialiser");

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
    if (v != NULL && memcmp(v, &pv.view, sizeof(pv.view)) != 0) {
        pv.view = *v;
        ++pv.rev;
    }
}

const port_view_t *port_view_get(void) { return &pv.view; }

uint32_t port_view_revision(void) { return pv.rev; }

bool port_view_has(uint8_t port)
{
    return pv.view.count != PORT_VIEW_UNKNOWN && port < pv.view.count
           && port < LINK_PG_PORTS_MAX;
}

uint8_t port_view_func(uint8_t port)
{
    return port_view_has(port) ? pv.view.func[port]
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

void port_view_set_ask(port_view_ask_fn fn) { pv.ask_fn = fn; }

bool port_view_ask(uint8_t port, prog_link_ask_kind_t kind, uint16_t value)
{
    if (pv.ask_fn == NULL || !port_view_has(port)
        || (kind == PROG_LINK_ASK_FUNC
            && (!pv.view.has_func
                || value >= (pv.view.has_dshot
                                 ? (uint16_t)LINK_PG_FN_COUNT
                                 : (uint16_t)LINK_PG_FN_COUNT_V426)))) {
        return false;
    }
    /* 1 to 255: 0 stands for no ask in the word that carries it. */
    const uint8_t seq = (pv.seq == 0xFFu) ? 1u : (uint8_t)(pv.seq + 1u);
    if (!pv.ask_fn(seq, port, kind, value)) {
        return false;
    }
    pv.seq       = seq;
    pv.ask_state = (uint8_t)PROG_LINK_ASK_WAITING;
    pv.ask_wait  = (uint8_t)PROG_LINK_WAIT_NONE;
    pv.ask_port  = port;
    pv.ask_kind  = (uint8_t)kind;
    ++pv.rev;
    return true;
}

void port_view_set_ask_state(uint8_t seq, prog_link_ask_state_t st)
{
    if (seq != pv.seq || pv.ask_state == (uint8_t)PROG_LINK_ASK_IDLE
        || (uint8_t)st == pv.ask_state || st == PROG_LINK_ASK_IDLE) {
        return;
    }
    pv.ask_state = (uint8_t)st;
    ++pv.rev;
}

void port_view_set_ask_wait(prog_link_wait_t wait)
{
    if ((uint8_t)wait != pv.ask_wait) {
        pv.ask_wait = (uint8_t)wait;
        ++pv.rev;
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
    switch ((prog_link_wait_t)pv.ask_wait) {
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
        *port = pv.ask_port;
    }
    if (kind != NULL) {
        *kind = pv.ask_kind;
    }
    return (prog_link_ask_state_t)pv.ask_state;
}

uint8_t port_view_ask_seq(void) { return pv.seq; }

bool port_view_ask_line(uint8_t port, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (port != pv.ask_port) {
        return false;
    }
    const char *line = NULL;
    switch ((prog_link_ask_state_t)pv.ask_state) {
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
