/**
 * @file
 * @brief The carrier's programming port choice; see prog_choice.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "prog_choice.h"

#include <stdio.h>

#include "link_pages.h"
#include "out_bind.h"
#include "out_pwm_map.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "prog5v_box.h"
#include "prog_link.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_board.h"
#include "ui_text.h"

/** @brief The state behind the choice. */
static struct {
    uint16_t minor;        /**< the far end's minor, 0 with no link      */
    uint8_t  asked_port;   /**< the port of the last ask made here, 1 to 4 */
    uint8_t  asked_fn;     /**< and its FUNC                              */
    uint8_t  refused_port; /**< the port of the last ask refused, 0 none  */
    uint8_t  refused_fn;   /**< and its FUNC                              */
    uint8_t  refused_for;  /**< the choice it was refused under           */
    uint32_t refused_rev;  /**< the view's revision it was refused at     */
} s; /**< the choice's state, the UI task's alone */

void prog_choice_set_far_minor(uint16_t minor)
{
    s.minor = minor;
}

void prog_choice_reset(void)
{
    s.minor = 0u;
    s.asked_port = 0u;
    s.asked_fn = 0u;
    s.refused_port = 0u;
    s.refused_fn = 0u;
    s.refused_for = 0u;
    s.refused_rev = 0u;
}

bool prog_choice_carrier(void)
{
    return ui_board_roles() != NULL;
}

bool prog_choice_offered(void)
{
    return prog_choice_carrier() && s.minor >= LINK_MINOR_CARRIER_PWM;
}

uint8_t prog_choice_port(void)
{
    const int t = settings_get_int(SET_SERVO_TARGET);
    return (t >= 1 && t <= (int)LINK_PG_PORTS_MAX) ? (uint8_t)t : 0u;
}

bool prog_choice_refusal(uint8_t port, char *buf, size_t cap)
{
    const outbind_roles_t *r = ui_board_roles();
    if (buf != NULL && cap > 0u) {
        buf[0] = '\0';
    }
    if (r == NULL || port < 1u || port > LINK_PG_PORTS_MAX) {
        return false;
    }
    const uint8_t gpio = r->prog[port - 1u];
    const outbind_t *b = outputs_screen_binding();
    const outbind_proto_t *protos = outbind_protos();
    if (b == NULL || protos == NULL) {
        return false;
    }
    for (unsigned i = 0; i < OUTBIND_FIXED_OUTS; ++i) {
        const uint8_t out = r->out[i];
        const uint8_t g = outbind_group_of(b, outbind_index_of(ui_board(), out));
        if (g == 0u || protos[g].driver != OUT_DRIVER_PWM
            || !out_pwm_same_slice(out, gpio)) {
            continue;
        }
        if (buf != NULL && cap > 0u) {
            char name[12];
            char role[12];
            (void)outputs_role_name(ui_board(), gpio, name, sizeof(name));
            (void)outputs_role_name(ui_board(), out, role, sizeof(role));
            snprintf(buf, cap, TR(PC_SLICE), name,
                     (unsigned)out_pwm_slice_of(gpio), role, (unsigned)out);
        }
        return true;
    }
    return false;
}

bool prog_choice_locked(void)
{
    return servo_screen_tester_target_locked() || servo_screen_arm_asked();
}

bool prog_choice_lock_line(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (!prog_choice_locked()) {
        return false;
    }
    snprintf(buf, cap, "%s",
             settings_held(SET_SERVO_TARGET) ? TR(OUT_PORT_HELD)
                                             : TR(OUT_PORT_LOCKED));
    return true;
}

bool prog_choice_pick(uint8_t port)
{
    if (!prog_choice_offered() || port > LINK_PG_PORTS_MAX
        || prog_choice_locked() || prog_choice_refusal(port, NULL, 0u)) {
        return false;
    }
    if (port != prog_choice_port()) {
        settings_set(SET_SERVO_TARGET, (float)port);
        settings_request_save();
    }
    return prog_choice_port() == port;
}

void prog_choice_entry(uint8_t port, char *buf, size_t cap)
{
    const outbind_roles_t *r = ui_board_roles();
    if (buf == NULL || cap == 0u) {
        return;
    }
    if (r == NULL || port < 1u || port > LINK_PG_PORTS_MAX) {
        buf[0] = '\0';
        return;
    }
    char name[12];
    (void)outputs_role_name(ui_board(), r->prog[port - 1u], name,
                            sizeof(name));
    snprintf(buf, cap, TR(PC_ENTRY), name, (unsigned)r->prog[port - 1u]);
}

uint8_t prog_choice_want_target(void)
{
    if (prog_choice_carrier() && !prog_choice_offered()) {
        return 0u;
    }
    return prog_choice_port();
}

bool prog_choice_needs_5v(void)
{
    return prog_choice_carrier() && prog_choice_port() != 0u
           && !prog5v_box_on();
}

bool prog_choice_want_pwm(void)
{
    const bool pwm = settings_get_bool(SET_SERVO_PORT_PWM);
    if (!prog_choice_carrier()) {
        return pwm;
    }
    return pwm && prog_choice_want_target() != 0u && !prog_choice_needs_5v()
           && !prog_choice_refusal(prog_choice_port(), NULL, 0u);
}

/**
 * @brief The FUNC the choice wants on port @p i, from 0.
 * @param i the port
 * @return SERVO PWM on the chosen, unrefused port; NONE on every other
 */
static uint8_t func_wanted(uint8_t i)
{
    const uint8_t port = prog_choice_port();
    return (port == i + 1u && !prog_choice_refusal(port, NULL, 0u))
               ? (uint8_t)LINK_PG_FN_SERVO_PWM
               : (uint8_t)LINK_PG_FN_NONE;
}

void prog_choice_tick(void)
{
    if (!prog_choice_offered()) {
        return;
    }
    const port_view_t *v = port_view_get();
    if (v == NULL || v->count == PORT_VIEW_UNKNOWN || !v->has_func) {
        return;
    }
    uint8_t ask_port = 0u, ask_kind = 0u;
    const prog_link_ask_state_t st = port_view_ask_state(&ask_port, &ask_kind);
    if (st == PROG_LINK_ASK_WAITING) {
        return;
    }
    if (st == PROG_LINK_ASK_REFUSED && ask_kind == (uint8_t)PROG_LINK_ASK_FUNC
        && s.asked_port == ask_port + 1u) {
        s.refused_port = s.asked_port;
        s.refused_fn = s.asked_fn;
        s.refused_for = prog_choice_port();
        s.refused_rev = port_view_revision();
        s.asked_port = 0u;
    }
    /* NONE first, so no two ports hold one slice on the way. */
    for (unsigned pass = 0; pass < 2u; ++pass) {
        for (uint8_t i = 0; i < v->count && i < LINK_PG_PORTS_MAX; ++i) {
            const uint8_t want = func_wanted(i);
            const bool none_pass = pass == 0u;
            if (v->func[i] == want
                || (want == (uint8_t)LINK_PG_FN_NONE) != none_pass) {
                continue;
            }
            if (s.refused_port == i + 1u && s.refused_fn == want
                && s.refused_for == prog_choice_port()
                && s.refused_rev == port_view_revision()) {
                return;             /* asked and refused: not again */
            }
            if (port_view_ask(i, PROG_LINK_ASK_FUNC, want)) {
                s.asked_port = (uint8_t)(i + 1u);
                s.asked_fn = want;
            }
            return;
        }
    }
}

int prog_choice_text(int id)
{
    if (id != (int)TX_KP_NEED_TARGET_2 || !prog_choice_carrier()) {
        return id;
    }
    return prog_choice_offered() ? (int)TX_PC_NEED_PORT_2
                                 : (int)TX_PC_OLD_IOMCU;
}
