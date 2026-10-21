/*
 * The programming ports as the screens see them, and the operator's asks
 * that change one (protocol 4.26).
 *
 * The control task reads the IOmcu's PROG page (prog_link.h) and hands
 * the UI task what each port holds: its PIN register, whether the IOmcu
 * refused that pin, and its FUNC.  OUTPUTS and PICK A PIN draw from this
 * view.  A port's PIN and FUNC are the IOmcu's, kept in its flash; a
 * screen changes one only by asking (port_view_ask()), which the control
 * task carries to prog_link_ask() and writes on a bench at rest.  The
 * outcome comes back here and the screens say it in words
 * (port_view_ask_line()).
 *
 * Until the page is read after a link-up the ports are not known: count
 * PORT_VIEW_UNKNOWN, no pin and no function.  A screen shows them as not
 * read, never as free.
 *
 * Pure state, no I/O.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages.h"
#include "prog_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The count while the ports are not known. */
#define PORT_VIEW_UNKNOWN 0xFFu
/** A port without a pin. */
#define PORT_VIEW_NO_PIN  0xFFu
/** A function not known. */
#define PORT_VIEW_FN_UNKNOWN 0xFFu

typedef struct {
    uint8_t count;                      /**< ports the IOmcu has, 0 for no
                                             page, PORT_VIEW_UNKNOWN      */
    bool    has_func;                   /**< it takes FUNC (4.26)         */
    bool    has_dshot;                  /**< FUNC takes DShot (4.28)      */
    uint8_t pin[LINK_PG_PORTS_MAX];     /**< the PIN register              */
    uint8_t func[LINK_PG_PORTS_MAX];    /**< a link_pg_func_t              */
    uint8_t refused;                    /**< bit n: the pin is refused     */
} port_view_t;

/** Nothing known: what the screens start with and show with no link. */
void port_view_unknown(port_view_t *v);

/** The view as the control task last read it; a change bumps
 *  port_view_revision(). */
void port_view_set(const port_view_t *v);
const port_view_t *port_view_get(void);
uint32_t port_view_revision(void);

/** Whether port @p port is one the IOmcu has, as last read. */
bool port_view_has(uint8_t port);

/** Port @p port's function, PORT_VIEW_FN_UNKNOWN while not known. */
uint8_t port_view_func(uint8_t port);

/** Whether port @p port is under the SERVO PWM rules: its function is
 *  SERVO PWM, or not known yet (the stricter rule). */
bool port_view_pwm(uint8_t port);

/** Whether port @p port's function is DSHOT150, DSHOT300 or DSHOT600. */
bool port_view_dshot(uint8_t port);

/** Function @p fn's name as the screens write it: SERVO PWM and NONE in
 *  the panel's language, the DShot rates by their catalogue names, the
 *  same in every language; "?" for a value no IOmcu sends. */
const char *port_view_fn_name(uint8_t fn);

/** Where an ask goes: true when the control task took it. */
typedef bool (*port_view_ask_fn)(uint8_t seq, uint8_t port,
                                 prog_link_ask_kind_t kind, uint16_t value);
void port_view_set_ask(port_view_ask_fn fn);

/**
 * Ask for port @p port's PIN (a GPIO, or LINK_PG_PIN_NONE) or FUNC.
 * False with no hook, a port the view does not have, a FUNC the IOmcu
 * does not take, or the hook refusing it.  Taken, the ask waits
 * (PROG_LINK_ASK_WAITING) until port_view_set_ask_state() reports it.
 */
bool port_view_ask(uint8_t port, prog_link_ask_kind_t kind, uint16_t value);

/** The control task's word on the ask numbered @p seq; a word about an
 *  older ask is ignored. */
void port_view_set_ask_state(uint8_t seq, prog_link_ask_state_t st);

/** The last ask: where it stands, and its port and kind (may be NULL). */
prog_link_ask_state_t port_view_ask_state(uint8_t *port, uint8_t *kind);

/** The number the last ask carried. */
uint8_t port_view_ask_seq(void);

/**
 * The line a screen shows for port @p port's last ask: waiting for a
 * bench at rest, refused, or the link lost before the write.  False and
 * an empty string when the last ask was for another port, is done, or
 * there was none.  The amber line under the port's rows.
 */
bool port_view_ask_line(uint8_t port, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
