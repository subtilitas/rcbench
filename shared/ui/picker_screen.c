/**
 * @file picker_screen.c
 * @brief OUTPUTS' board view, its state, touch handling and interface: the
 *        board, with a button on every pin an output may have.  See
 *        picker_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen.h"

#include <stdio.h>
#include <string.h>

#include "board_drawing.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "outputs_screen_int.h"
#include "picker_port.h"
#include "port_view.h"
#include "ui_text.h"

#include "picker_screen_int.h"

/** @brief The screen's state; picker_screen_int.h names it s. */
pk_state_t pk_s;

/** @brief The GPIOs the IOmcu's programming ports are bound to,
 *         PICKER_NO_PIN for none.  Kept out of `s` so a reset of the
 *         screen does not forget them. */
static uint8_t g_prog_pins[LINK_PG_PORTS_MAX] = {
    PICKER_NO_PIN, PICKER_NO_PIN, PICKER_NO_PIN, PICKER_NO_PIN
};
_Static_assert(LINK_PG_PORTS_MAX == 4u, "g_prog_pins' initialiser");

/**
 * @brief Count a change: every framebuffer's picture is stale.
 *
 * A change invalidates every framebuffer, and a render validates the one it
 * drew.  Not a counter compared against the last frame drawn: with two
 * buffers alternating, a counter that is satisfied by the first of them
 * leaves the second holding the old picture, and clearing the others on
 * every paint instead makes the screen repaint for ever.
 *
 * A repaint draws the board, every button and both tables; a picker that
 * never settled would spend that on every frame for as long as it is open.
 */
void pk_touched(void) { picker_screen_invalidate(); }

void picker_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

const outbind_t *picker_screen_binding(void) { return &s.bind; }

void picker_screen_set_binding(const outbind_t *b)
{
    if (b != NULL) {
        const uint16_t was = s.bind.board;
        s.bind = *b;
        /* Read off the wire or restored, so it is trimmed before it is drawn
         * rather than trusted to mean something on this board. */
        outbind_trim(&s.bind);
        pk_touched();
        /*
         * Another board: its drawing is asked for now.  A drawing published
         * for it before this binding arrived was refused then, because the
         * picker still named the board before, and nothing else asks again
         * until the screen is entered.
         */
        if (s.bind.board != was) {
            s.refused = -1;     /* an index into the catalogue before */
            if (s.source != NULL) {
                s.source(s.bind.board);
            }
        }
    }
}

void picker_screen_follow(uint8_t proto, bool editable)
{
    const bool unread = !editable;
    if (proto < OUTBIND_PROTOS && proto != s.bind.proto) {
        outbind_set_proto(&s.bind, proto);
        pk_touched();
    }
    /* And a port picked there, or none. */
    const int port = outputs_screen_port_shown();
    if (port != s.port) {
        s.port = port;
        s.refused = -1;
        pk_touched();
    }
    if (unread != s.unread) {
        s.unread = unread;
        /* A press begun while the binding could be edited does not act on
         * its release. */
        if (s.hit_kind == HIT_BUTTON) {
            s.hit_kind = HIT_NONE;
            s.hit = -1;
        }
        pk_touched();
    }
}

bool picker_screen_editable(void) { return !s.unread; }

/**
 * @brief Name the function a tick or untick applies the binding through.
 * @param fn the function, or NULL for none
 */
void picker_screen_set_apply(picker_apply_fn fn) { s.apply = fn; }

/**
 * @brief Name the function a pick in the list tells the Outputs screen by.
 * @param fn the function, or NULL for none
 */
void picker_screen_set_select(picker_select_fn fn) { s.select = fn; }

/**
 * @brief Name the function that asks for the drawing of a board.
 * @param fn the function, or NULL for none
 */
void picker_screen_set_drawing_source(picker_drawing_fn fn) { s.source = fn; }

bool picker_screen_set_drawing(const uint8_t *data, uint32_t bytes,
                               const char **why)
{
    const char *reason = "no drawing";
    link_drawing_t d;
    bool ok = false;
    if (data != NULL && bytes != 0u
        && link_drawing_parse(&d, data, bytes, &reason)) {
        ok = board_drawing_agrees(&d, outbind_board(s.bind.board), &reason);
    }
    s.has_drawing   = ok;
    s.drawing       = ok ? d : (link_drawing_t){ 0 };
    s.drawing_board = s.bind.board;
    if (why != NULL) {
        *why = ok ? "" : reason;
    }
    pk_touched();
    return ok;
}

void picker_screen_set_prog_ports(uint8_t count, const uint8_t *pins)
{
    bool moved = false;
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        const uint8_t pin = (pins != NULL && n < count) ? pins[n]
                                                         : PICKER_NO_PIN;
        moved = moved || pin != g_prog_pins[n];
        g_prog_pins[n] = pin;
    }
    if (moved) {
        pk_touched();
    }
}

void picker_screen_set_prog_pin(uint8_t gpio)
{
    picker_screen_set_prog_ports(1u, &gpio);
}

/**
 * @brief The shape page of the board the binding names.
 * @return the shape, or NULL for a board without one
 */
const outbind_shape_t *pk_shape(void)
{
    const outbind_board_t *bd = outbind_board(s.bind.board);
    return (bd != NULL) ? bd->shape : NULL;
}

/**
 * @brief The drawing, while it is the one for the board the binding names.
 * @return the drawing, or NULL
 */
const link_drawing_t *pk_drawing(void)
{
    return (s.has_drawing && s.drawing_board == s.bind.board) ? &s.drawing
                                                              : NULL;
}

bool picker_screen_has_drawing(void) { return pk_drawing() != NULL; }

bool picker_screen_can_draw(void)
{
    return (pk_drawing() != NULL || pk_shape() != NULL)
           && outbind_pin_count(s.bind.board) > 0u;
}

/**
 * @brief The protocol shown: the binding's, OFF for one out of range.
 * @return its index in the catalogue
 */
uint8_t pk_proto_shown(void)
{
    return (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
}

/* ---------------------------------------------------------------- state */

/**
 * @brief Whether the programming port has pin @p index: it is the port's
 *        and no protocol holds it.  A pin a stored slot holds is that
 *        slot's -- the IOmcu gives the port no pin then -- and can be
 *        unticked.
 * @param index the pin's index in the board's catalogue
 * @return true when the port has it
 */
bool pk_prog_taken(uint8_t index)
{
    if (index >= outbind_pin_count(s.bind.board)
        || outbind_pins(s.bind.board)[index].reserved
        || outbind_group_of(&s.bind, index) != 0u) {
        return false;
    }
    const uint8_t gpio = outbind_pins(s.bind.board)[index].gpio;
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        if (g_prog_pins[n] != PICKER_NO_PIN && g_prog_pins[n] == gpio) {
            return true;
        }
    }
    return false;
}

/**
 * @brief What SETUP > INTERFACES holds pin @p index for: no protocol and no
 *        port holds it, and the IOmcu does not reserve it.  A pin a
 *        protocol holds is that protocol's, as against a port.
 * @param index the pin's index in the board's catalogue
 * @return the interface, or OUTPUTS_IFACE_NONE
 */
outputs_iface_t pk_iface_taken(uint8_t index)
{
    if (index >= outbind_pin_count(s.bind.board)
        || outbind_pins(s.bind.board)[index].reserved
        || outbind_group_of(&s.bind, index) != 0u || pk_prog_taken(index)) {
        return OUTPUTS_IFACE_NONE;
    }
    return outputs_iface_at(outbind_pins(s.bind.board)[index].gpio);
}

uint8_t picker_screen_prog_listed(void)
{
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (uint8_t i = 0; i < n; ++i) {
        if (pk_prog_taken(i)) {
            return outbind_pins(s.bind.board)[i].gpio;
        }
    }
    return PICKER_NO_PIN;
}

picker_pin_t picker_screen_pin(uint8_t index, char *holder, size_t cap)
{
    if (holder != NULL && cap != 0u) {
        holder[0] = '\0';
    }
    if (index >= outbind_pin_count(s.bind.board)) {
        return PICKER_PIN_NONE;
    }
    const outbind_pin_t *pin = &outbind_pins(s.bind.board)[index];
    const uint8_t proto = pk_proto_shown();
    const uint8_t held = outbind_group_of(&s.bind, index);
    const outbind_board_t *bd = outbind_board(s.bind.board);

    if (pin->reserved) {
        if (holder != NULL && cap != 0u && pin->held_by != NULL) {
            snprintf(holder, cap, "%s", ui_pin_holder(pin->held_by));
        }
        return PICKER_PIN_RESERVED;
    }
    if (s.port >= 0) {
        return picker_port_pin(&s.bind, index, (uint8_t)s.port,
                               s.unread || (bd != NULL && bd->fixed), holder,
                               cap);
    }
    if (pk_prog_taken(index)) {
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, "%s", TR(OUT_PROG_PORT));
        }
        return PICKER_PIN_HELD;
    }
    const outputs_iface_t iface = pk_iface_taken(index);
    if (iface != OUTPUTS_IFACE_NONE) {
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, "%s", outputs_iface_name(iface));
        }
        return PICKER_PIN_HELD;
    }
    if (held != 0u && held != proto) {
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, "%s", outbind_protos()[held].name);
        }
        return PICKER_PIN_HELD;
    }
    /* A soldered board's pins are nobody's to press, and a binding no read
     * has confirmed is nobody's to edit: shown, and not offered. */
    if (s.unread || (bd != NULL && bd->fixed)) {
        return PICKER_PIN_LOCKED;
    }
    /* Free and offered, and the PWM generator cannot take it beside what it carries:
     * held by the pin it clashes with.  outbind_pwm_clash_ports() takes the
     * ports' pins as the IOmcu reads them, PICKER_NO_PIN being no pin. */
    if (outbind_pwm_clash_ports(&s.bind, index,
                                outputs_pwm_pins(g_prog_pins),
                                LINK_PG_PORTS_MAX, NULL)
        != OUTBIND_CLASH_NONE) {
        if (holder != NULL) {
            (void)outputs_clash_label(&s.bind, index,
                                      outputs_pwm_pins(g_prog_pins),
                                      LINK_PG_PORTS_MAX, holder, cap);
        }
        return PICKER_PIN_HELD;
    }
    if (held != 0u) {
        return PICKER_PIN_TICKED;
    }
    return outbind_can_add(&s.bind, index) ? PICKER_PIN_FREE
                                           : PICKER_PIN_REFUSED;
}

/**
 * @brief The channel the OUTPUTS page gives catalogue entry @p index.
 *        Channels are allocated in pin order across every protocol, so a
 *        group's first pin is not its channel 0 unless it also holds the
 *        lowest pin.
 * @param index the pin's index in the board's catalogue
 * @return the channel, or -1
 */
int8_t pk_channel_of(uint8_t index)
{
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(&s.bind, regs);
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    for (uint8_t sl = 0; sl < used; ++sl) {
        const uint16_t *r = &regs[(size_t)sl * LINK_OS_STRIDE];
        if (pins[index].gpio == (uint8_t)r[LINK_OS_PIN]) {
            return (int8_t)LINK_OS_FIRST(r[LINK_OS_RANGE]);
        }
    }
    return -1;
}

bool picker_screen_row(uint8_t k, picker_row_t *out)
{
    const uint8_t proto = pk_proto_shown();
    const uint8_t n = outbind_pin_count(s.bind.board);
    if (s.port >= 0) {
        return picker_port_row(&s.bind, k, out);
    }
    if (proto == 0u) {
        return false;
    }
    uint8_t seen = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (outbind_group_of(&s.bind, i) != proto) { continue; }
        if (seen++ != k) { continue; }
        if (out != NULL) {
            const outbind_pin_t *pin = &outbind_pins(s.bind.board)[i];
            out->index   = i;
            out->gpio    = pin->gpio;
            out->channel = pk_channel_of(i);
            out->pad     = pin->pad;
        }
        return true;
    }
    return false;
}

void picker_screen_count(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return;
    }
    buf[0] = '\0';
    if (s.port >= 0) {
        return;                 /* the FUNCTION face says what the port carries */
    }
    /* Slots used by every protocol, not by this one: they share the eight,
     * so this protocol's own count says nothing about what is left. */
    snprintf(buf, cap, TR(PK_SLOTS_OF),
             (unsigned)outbind_chosen_total(&s.bind),
             (unsigned)LINK_OUT_SLOTS);
}

bool picker_screen_reason(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (s.unread) {
        /* Why no button acts: the binding drawn is the last one read, or
         * pages no binding describes, which UNBIND ALL PINS answers. */
        snprintf(buf, cap, "%s",
                 (outputs_screen_read_state() == BIND_READ_ODD)
                     ? TR(OUT_NOT_MAPPED) : TR(PK_NOT_READ));
        return true;
    }
    if (s.port >= 0) {
        return picker_port_reason(&s.bind, (uint8_t)s.port, s.refused, buf,
                                  cap);
    }
    return outputs_reason_of(&s.bind, buf, cap)
           || outputs_clash_reason(&s.bind, s.refused,
                                   outputs_pwm_pins(g_prog_pins),
                                   LINK_PG_PORTS_MAX, buf, cap);
}

/* ----------------------------------------------------------------- input */

/**
 * @brief The key under a point.
 * @param x the point's column, screen coordinates
 * @param y its row
 * @return the key's pin index, or -1 for none
 */
static int button_under(int x, int y)
{
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (int i = 0; i < (int)n; ++i) {
        gfx_rect_t r;
        if (pk_button_at(i, &r, NULL) && gfx_rect_contains(r, x, y)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Pick a protocol.  A pick says which set is shown and which group
 *        the next tap joins.  It changes no page, so nothing is written;
 *        the outputs screen is told, so the two screens show one
 *        selection.
 * @param proto the protocol's index in the catalogue
 */
static void pick(uint8_t proto)
{
    outbind_set_proto(&s.bind, proto);
    s.refused = -1;
    s.port = -1;
    if (s.select != NULL) {
        s.select(proto);
    }
}

/**
 * @brief Pick a port: its pin is the one a tap sets.  The outputs screen
 *        shows the same entry.
 * @param port the port, from 0
 */
static void pick_port(uint8_t port)
{
    if (port >= outputs_screen_port_entries()) {
        return;
    }
    outbind_set_proto(&s.bind, 0u);
    s.refused = -1;
    outputs_screen_select_port(port);
    s.port = outputs_screen_port_shown();
}

int picker_screen_port_shown(void) { return s.port; }

/**
 * @brief A touch event: the list, the face or a key.
 * @param evt the event, or NULL
 */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;

    /* The entry's rows, its open FUNCTION list and UNBIND ALL PINS first:
     * each takes the press it is under, and nothing under it sees it. */
    if (!s.open && ((s.port >= 0 && outputs_port_event(evt))
                    || outputs_unbind_event(evt))) {
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        s.hit_kind = HIT_NONE;
        s.hit = -1;
        if (s.open) {
            for (uint8_t i = 0; i < (uint8_t)pk_pop_rows(); ++i) {
                if (gfx_rect_contains(picker_screen_list_row(i), x, y)) {
                    s.hit_kind = HIT_ROW;
                    s.hit = (int)i;
                    pk_touched();
                    return;
                }
            }
            /* A press anywhere else closes it without choosing: an open
             * list must not be a trap, and the press is spent on that. */
            s.open = false;
            pk_touched();
            return;
        }
        if (!picker_screen_can_draw()) {
            return;
        }
        if (gfx_rect_contains(picker_screen_list_face(), x, y)) {
            s.hit_kind = HIT_FACE;
            pk_touched();
            return;
        }
        /* A binding that is not confirmed takes no press on a key, and
         * neither does OFF, which binds none, as on the Outputs screen.
         * OFF's keys are grey, and a grey key draws no pressed face. */
        const bool off = outbind_protos()[pk_proto_shown()].max_pins == 0u
                         && s.port < 0;
        const int b = (s.unread || off) ? -1 : button_under(x, y);
        if (b >= 0) {
            s.hit_kind = HIT_BUTTON;
            s.hit = b;
            pk_touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int kind = s.hit_kind, was = s.hit;
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    if (kind == HIT_NONE) {
        return;
    }
    pk_touched();
    if (!touch_event_is_tap_up(evt)) {
        return;              /* a release the finger did not make */
    }
    if (kind == HIT_FACE) {
        if (gfx_rect_contains(picker_screen_list_face(), x, y)) {
            s.open = true;
        }
        return;
    }
    if (kind == HIT_ROW) {
        if (gfx_rect_contains(picker_screen_list_row((uint8_t)was), x, y)) {
            s.open = false;
            if (was < (int)OUTBIND_PROTOS) {
                pick((uint8_t)was);
            } else {
                pick_port((uint8_t)(was - (int)OUTBIND_PROTOS));
            }
        }
        return;
    }
    if (button_under(x, y) != was) {
        return;              /* the finger left the button it pressed */
    }
    /* Only a button that is offered acts.  A refused one is not silent: the
     * reason is printed under the protocol list. */
    const picker_pin_t st = picker_screen_pin((uint8_t)was, NULL, 0);
    if (s.port >= 0) {
        s.refused = (st == PICKER_PIN_HELD
                     && picker_port_clashes(&s.bind, (uint8_t)was,
                                            (uint8_t)s.port))
                        ? was : -1;
        (void)picker_port_tap(&s.bind, (uint8_t)was, (uint8_t)s.port, st);
        return;
    }
    if (st != PICKER_PIN_FREE && st != PICKER_PIN_TICKED) {
        if (st == PICKER_PIN_HELD
            && outbind_pwm_clash_ports(&s.bind, (uint8_t)was,
                                       outputs_pwm_pins(g_prog_pins),
                                       LINK_PG_PORTS_MAX, NULL)
                   != OUTBIND_CLASH_NONE) {
            s.refused = was;
        }
        return;
    }
    if (outbind_toggle(&s.bind, (uint8_t)was)) {
        s.refused = -1;
        if (s.apply != NULL) {
            s.apply(&s.bind);
        }
    }
}

/** @brief Forget everything: no binding, no list, no cat. */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    outbind_init(&s.bind);
    s.hit = -1;
    s.refused = -1;
    s.port = -1;
    pk_cat_reset();
    pk_touched();
}

/**
 * @brief A visit begins.
 *
 * Every visit opens on the overview, as the Outputs screen's does: OFF
 * selected here for the first frame, and on the Outputs screen through the
 * select hook, which this screen follows every frame.  Without the hook the
 * next follow puts the Outputs screen's protocol back; without the line
 * here the first frame shows the protocol before.
 */
static void enter(void)
{
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    s.refused = -1;
    s.open = false;
    outbind_set_proto(&s.bind, 0u);
    s.port = -1;
    if (s.select != NULL) {
        s.select(0u);
    }
    /* Asked on the way in, so a drawing that arrived since the last visit
     * is the one shown, and one for a board that has been swapped is not. */
    if (s.source != NULL) {
        s.source(s.bind.board);
    }
    /* A cat, picked on this visit's first frame. */
    pk_cat_enter();
    pk_touched();
}

/** @brief The screen is left: no press, the list closed. */
static void leave(void)
{
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    s.open = false;
}

/**
 * @brief The home tag closes an open protocol or FUNCTION list without
 *        choosing; with none open it leaves the screen.
 * @return true when it closed a list
 */
static bool back(void)
{
    if (!s.open) {
        return outputs_port_back();
    }
    leave();
    pk_touched();
    return true;
}

/**
 * @brief Forget the press under way.
 *
 * Touch events were lost between two frames, so the button recorded under
 * the finger may not be under one any more.  A release toggles the pin when
 * it lands on the button the press began on, and a stale record turns the
 * next release over that button into a binding change nobody asked for.
 */
static void cancel(void)
{
    if (s.hit_kind != HIT_NONE) {
        s.hit_kind = HIT_NONE;
        s.hit = -1;
        pk_touched();
    }
}

/** @brief The screen's hooks for the router. */
static const ui_screen_t s_screen = {
    .title  = "OUTPUTS",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = pk_render,
    .back   = back,
};

/**
 * @brief OUTPUTS' board view.
 * @return its hooks
 */
const ui_screen_t *picker_screen(void) { return &s_screen; }
