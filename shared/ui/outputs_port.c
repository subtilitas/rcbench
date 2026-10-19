/*
 * The OUTPUTS screen's PROG PORT entries: the IOmcu's programming ports as
 * outputs.  See outputs_screen.h.
 *
 * One entry a port the IOmcu reports.  An entry shows the port's pin,
 * read-only, and what the port carries; port 1's carries the two settings
 * that make it the SERVO tester's output, SERVO TESTER (SET_SERVO_TARGET)
 * and PORT PWM (SET_SERVO_PORT_PWM), each a segment under its label,
 * locked under the SERVO screen's rules.  Neither writes a page from here:
 * the control task takes the settings to the IOmcu on a bench at rest.
 *
 * The lines that name a PWM pin the IOmcu refuses beside a port's pin or a
 * bound PWM pin (outputs_clash_label(), outputs_clash_reason()) are here
 * too, for OUTPUTS and PICK A PIN alike.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "outputs_screen_int.h"

/*
 * The entry's left column, under the dropdown: the pin and what the port
 * carries where a protocol has its count and reason; the SERVO TESTER and
 * PORT PWM segments, each under its label; and the line that says why
 * they are locked.
 */
#define PORT_PIN_Y     (DD_Y + DD_H + 16)
#define PORT_CARRY_Y   (DD_Y + DD_H + 38)
#define PORT_TESTER_Y  184
#define PORT_PWM_Y     254
#define PORT_SEG_DY    20     /* label to segment */
#define PORT_SEG_H     36
#define PORT_WHY_Y     326

/* The GPIO the IOmcu's programming port 1 is bound to, or none. */
static uint8_t g_prog_gpio = OUTPUTS_NO_PIN;
/* The ports the IOmcu reports, OUTPUTS_PORTS_UNKNOWN while not known, and
 * the pins of ports 2 to 4 (port 1's is g_prog_gpio). */
static uint8_t g_port_count = OUTPUTS_PORTS_UNKNOWN;
static uint8_t g_port_pin[LINK_PG_PORTS_MAX] = {
    OUTPUTS_NO_PIN, OUTPUTS_NO_PIN, OUTPUTS_NO_PIN, OUTPUTS_NO_PIN
};
_Static_assert(LINK_PG_PORTS_MAX == 4u, "g_port_pin's initialiser");

static struct {
    int       port;         /* the entry shown, or -1 for a protocol      */
    ui_tabs_t tester;       /* SERVO TESTER: OUTPUTS | PROG PORT          */
    ui_tabs_t pwm;          /* PORT PWM: ON | OFF                         */
    uint32_t  sig;          /* what the column last showed                */
    uint32_t  rev;
} p = { .port = -1 };

static void touched(void)
{
    ++p.rev;
}

uint32_t outputs_port_revision(void) { return p.rev; }

void outputs_screen_set_prog_pin(uint8_t gpio)
{
    if (gpio != g_prog_gpio) {
        g_prog_gpio = gpio;
        touched();
    }
}

uint8_t outputs_screen_port_entries(void)
{
    if (g_port_count == OUTPUTS_PORTS_UNKNOWN) {
        return 1u;
    }
    return (g_port_count > LINK_PG_PORTS_MAX) ? (uint8_t)LINK_PG_PORTS_MAX
                                              : g_port_count;
}

uint8_t outputs_screen_port_pin(uint8_t port)
{
    if (port >= outputs_screen_port_entries()) {
        return OUTPUTS_NO_PIN;
    }
    return (port == 0u) ? g_prog_gpio : g_port_pin[port];
}

void outputs_screen_set_prog_ports(uint8_t count, const uint8_t *pins)
{
    const bool known = count != OUTPUTS_PORTS_UNKNOWN;
    bool moved = count != g_port_count;
    g_port_count = count;
    for (uint8_t n = 1; n < LINK_PG_PORTS_MAX; ++n) {
        const uint8_t pin = (known && pins != NULL && n < count)
                                ? pins[n] : OUTPUTS_NO_PIN;
        moved = moved || pin != g_port_pin[n];
        g_port_pin[n] = pin;
    }
    /* Port 1's pin while the count is not known: the PIN register's, as
     * outputs_screen_set_prog_pin() was given it. */
    if (known) {
        outputs_screen_set_prog_pin((pins != NULL && count > 0u)
                                        ? pins[0] : OUTPUTS_NO_PIN);
    } else if (pins != NULL) {
        outputs_screen_set_prog_pin(pins[0]);
    }
    /* A port the IOmcu no longer reports is no entry to stay on. */
    if (p.port >= (int)outputs_screen_port_entries()) {
        outputs_screen_select(0u);
        moved = true;
    }
    if (moved) {
        touched();
    }
}

/* ------------------------------------------------- SETUP > INTERFACES */

/* GPIO @p v as a mask bit, or 0 for -1 and anything past the mask. */
static uint64_t pin_bit(int v)
{
    return (v >= 0 && v < 64) ? ((uint64_t)1u << (unsigned)v) : 0u;
}

uint64_t outputs_iface_pins(outputs_iface_t which)
{
    switch (which) {
    case OUTPUTS_IFACE_SENSE: {
        const bool on = settings_get_bool(SET_INA228_EN)
                        || settings_get_bool(SET_INA3221_EN)
                        || settings_get_bool(SET_ENC_EN);
        const int sda = settings_get_int(SET_SENSE_SDA);
        if (!on || sda < 0) {
            return 0u;
        }
        return pin_bit(sda) | pin_bit(settings_get_int(SET_SENSE_SCL));
    }
    case OUTPUTS_IFACE_PDMINI: {
        /* The page is written ON only with both pins set and apart
         * (supply_link.c); otherwise it holds neither. */
        const int tx = settings_get_int(SET_PDMINI_TX);
        const int rx = settings_get_int(SET_PDMINI_RX);
        if (!settings_get_bool(SET_PDMINI_EN) || tx < 0 || rx < 0
            || tx == rx) {
            return 0u;
        }
        return pin_bit(tx) | pin_bit(rx);
    }
    case OUTPUTS_IFACE_TAP:
        return settings_get_bool(SET_TONE_EN)
                   ? pin_bit(settings_get_int(SET_TONE_PIN)) : 0u;
    case OUTPUTS_IFACE_NONE:
    case OUTPUTS_IFACE_COUNT:
    default:
        return 0u;
    }
}

outputs_iface_t outputs_iface_at(uint8_t gpio)
{
    const uint64_t bit = pin_bit(gpio);
    if (bit == 0u) {
        return OUTPUTS_IFACE_NONE;
    }
    for (int f = OUTPUTS_IFACE_SENSE; f < OUTPUTS_IFACE_COUNT; ++f) {
        if ((outputs_iface_pins((outputs_iface_t)f) & bit) != 0u) {
            return (outputs_iface_t)f;
        }
    }
    return OUTPUTS_IFACE_NONE;
}

const char *outputs_iface_name(outputs_iface_t which)
{
    switch (which) {
    case OUTPUTS_IFACE_SENSE:  return TR(OUT_IFACE_SENSE);
    case OUTPUTS_IFACE_PDMINI: return TR(OUT_IFACE_PDMINI);
    case OUTPUTS_IFACE_TAP:    return TR(OUT_IFACE_TAP);
    case OUTPUTS_IFACE_NONE:
    case OUTPUTS_IFACE_COUNT:
    default:                   return "";
    }
}

/* All of them, for the screens' repaint: a row stepped on SETUP moves a
 * pin without a touch here. */
uint64_t outputs_iface_all(void)
{
    uint64_t m = 0u;
    for (int f = OUTPUTS_IFACE_SENSE; f < OUTPUTS_IFACE_COUNT; ++f) {
        m |= outputs_iface_pins((outputs_iface_t)f);
    }
    return m;
}

bool outputs_port_holds(uint8_t gpio)
{
    for (uint8_t n = 0; n < outputs_screen_port_entries(); ++n) {
        const uint8_t pin = outputs_screen_port_pin(n);
        if (pin != OUTPUTS_NO_PIN && pin == gpio) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------- the entry opened on */

/* What the next entry opens on: the overview, a protocol, or a port. */
enum { OPEN_OVERVIEW = 0, OPEN_PROTO, OPEN_PORT };
static uint8_t g_open_kind = OPEN_OVERVIEW;
static uint8_t g_open_value;

void outputs_screen_open_on(uint8_t proto)
{
    g_open_kind  = OPEN_PROTO;
    g_open_value = proto;
}

void outputs_screen_open_on_port(uint8_t port)
{
    g_open_kind  = OPEN_PORT;
    g_open_value = port;
}

void outputs_port_forget_open(void)
{
    g_open_kind = OPEN_OVERVIEW;
}

void outputs_port_open(void)
{
    const uint8_t kind = g_open_kind, value = g_open_value;
    g_open_kind = OPEN_OVERVIEW;
    if (kind == OPEN_PORT && value < outputs_screen_port_entries()) {
        outputs_screen_select_port(value);
    } else if (kind == OPEN_PROTO && value < OUTBIND_PROTOS) {
        outputs_screen_select(value);
    } else {
        outputs_screen_select(0u);
    }
}

/* ------------------------------------------------------------ the entry */

static gfx_rect_t seg_row(int label_y)
{
    return gfx_rect_make(COL_X + 3, label_y + PORT_SEG_DY, COL_W - 6,
                         PORT_SEG_H);
}

void outputs_port_reset(void)
{
    static const ui_text_id_t tester[] = { TX_OUT_TESTER_OUTPUTS,
                                           TX_OUT_PROG_PORT };
    static const ui_text_id_t pwm[] = { TX_ON, TX_OFF };
    p.port = -1;
    ui_tabs_init_text(&p.tester, tester, 2, seg_row(PORT_TESTER_Y));
    ui_tabs_init_text(&p.pwm, pwm, 2, seg_row(PORT_PWM_Y));
    touched();
}

void outputs_port_cancel(void)
{
    ui_tabs_cancel(&p.tester);
    ui_tabs_cancel(&p.pwm);
    touched();
}

void outputs_port_hide(void)
{
    p.port = -1;
    outputs_port_cancel();
}

/* A PROG PORT entry picked: the pins as the overview shows them, the
 * port's own lit, and no pin takes a tick.  OFF underneath, picked, so a
 * reading keeps it. */
void outputs_screen_select_port(uint8_t port)
{
    if (port >= outputs_screen_port_entries()) {
        return;
    }
    outputs_screen_select(0u);
    p.port = (int)port;
}

int outputs_screen_port_shown(void) { return p.port; }

void outputs_port_name(uint8_t port, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return;
    }
    if (outputs_screen_port_entries() == 1u) {
        snprintf(buf, cap, "%s", TR(OUT_PROG_PORT));
    } else {
        snprintf(buf, cap, TR(OUT_PROG_PORT_N), (unsigned)port + 1u);
    }
}

/* SERVO TESTER and PORT PWM, under the SERVO screen's rules
 * (servo_screen_tester_target_locked(), servo_screen_port_pwm_locked()). */
static bool tester_locked(void) { return servo_screen_tester_target_locked(); }
static bool pwm_locked(void) { return servo_screen_port_pwm_locked(); }

/* The segments follow the settings, whoever set them. */
static void sync_tabs(void)
{
    p.tester.selected = (settings_get_int(SET_SERVO_TARGET) != 0) ? 1 : 0;
    p.pwm.selected    = settings_get_bool(SET_SERVO_PORT_PWM) ? 0 : 1;
}

/* Whether the SERVO TESTER and PORT PWM rows are on the entry shown: the
 * TARGET setting names port 1 alone. */
static bool rows_shown(void) { return p.port == 0; }

bool outputs_screen_port_reason(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (!rows_shown()) {
        return false;
    }
    const char *why = NULL;
    if (settings_held(SET_SERVO_TARGET) || settings_held(SET_SERVO_PORT_PWM)) {
        why = TR(OUT_PORT_HELD);
    } else if (tester_locked()) {
        why = TR(OUT_PORT_LOCKED);
    } else if (pwm_locked()) {
        why = (settings_get_int(SET_SERVO_TARGET) == 0)
                  ? TR(OUT_PORT_PWM_NEEDS)
                  : TR(OUT_PORT_LOCKED);
    }
    if (why == NULL) {
        return false;
    }
    snprintf(buf, cap, "%s", why);
    return true;
}

/* What the column shows that the settings or the SERVO screen can change
 * from outside: a change repaints it. */
static uint32_t signature(void)
{
    if (p.port < 0) {
        return 0u;
    }
    return 1u | ((settings_get_int(SET_SERVO_TARGET) != 0) ? 2u : 0u)
           | (settings_get_bool(SET_SERVO_PORT_PWM) ? 4u : 0u)
           | (tester_locked() ? 8u : 0u) | (pwm_locked() ? 16u : 0u)
           | (settings_held(SET_SERVO_TARGET) ? 32u : 0u);
}

void outputs_port_tick(void)
{
    const uint32_t sig = signature();
    if (sig != p.sig) {
        p.sig = sig;
        touched();
    }
}

/*
 * A press on a segment: taken whether or not the row is locked, so nothing
 * under it sees it, and acted on only while it is not.  A row that locks
 * under a press under way drops it.  Returns whether the event was the
 * row's.
 */
static bool seg_event(ui_tabs_t *t, const touch_event_t *evt, bool locked,
                      setting_id_t id)
{
    if (evt->type == TOUCH_EVENT_DOWN) {
        bool on = false;
        for (int i = 0; i < t->count; ++i) {
            on = on || gfx_rect_contains(t->rect[i], evt->point.x,
                                         evt->point.y);
        }
        if (!on) {
            return false;
        }
        if (!locked && t->pressed < 0) {
            (void)ui_tabs_event(t, evt);
        }
        touched();
        return true;
    }
    if (t->pressed < 0 || evt->point.id != t->press_id) {
        return false;
    }
    if (locked) {
        ui_tabs_cancel(t);
        touched();
        return true;
    }
    if (ui_tabs_event(t, evt)) {
        /* SERVO TESTER: 0 OUTPUTS, 1 PROG PORT; PORT PWM: 0 ON, 1 OFF. */
        const float v = (id == SET_SERVO_TARGET) ? (float)t->selected
                                                 : (t->selected == 0) ? 1.0f
                                                                      : 0.0f;
        settings_set(id, v);
        settings_request_save();
    }
    sync_tabs();
    touched();
    return true;
}

bool outputs_port_event(const touch_event_t *evt)
{
    if (evt == NULL || !rows_shown()) {
        return false;
    }
    sync_tabs();
    return seg_event(&p.tester, evt, tester_locked(), SET_SERVO_TARGET)
           || seg_event(&p.pwm, evt, pwm_locked(), SET_SERVO_PORT_PWM);
}

/*
 * The column: the pin, read-only, what the port carries, and on port 1 the
 * tester's target and the port's pulse, each a segment under its label,
 * drawn locked under the SERVO screen's rules with the reason under them.
 * No last write: the entry writes no binding.
 */
void outputs_port_draw(gfx_canvas_t *c, gfx_rect_t dd, bool pressed,
                       bool open)
{
    if (c == NULL || p.port < 0) {
        return;
    }
    char buf[96];
    gfx_text(c, COL_X, COL_Y, TR(OUT_PROTOCOL), UI_FONT_LABEL, UI_TEXT_FAINT,
             1);
    outputs_port_name((uint8_t)p.port, buf, sizeof(buf));
    ui_choice_face(c, dd, buf, pressed, open);

    const uint8_t pin = outputs_screen_port_pin((uint8_t)p.port);
    if (pin != OUTPUTS_NO_PIN) {
        snprintf(buf, sizeof(buf), TR(OUT_PORT_PIN), (unsigned)pin);
    } else {
        snprintf(buf, sizeof(buf), "%s", TR(OUT_PORT_NO_PIN));
    }
    gfx_text(c, COL_X, PORT_PIN_Y, buf, UI_FONT_LABEL,
             (pin != OUTPUTS_NO_PIN) ? UI_TEXT_DIM : UI_WARN, 1);
    (void)outputs_wrap(c, gfx_rect_make(COL_X, PORT_CARRY_Y, COL_W, 16), 18,
                       TR(OUT_PORT_CARRIES), 2, UI_FONT_LABEL, UI_TEXT_FAINT,
                       false);

    if (!rows_shown()) {
        (void)outputs_wrap(c, gfx_rect_make(COL_X, PORT_TESTER_Y, COL_W, 16),
                           18, TR(OUT_PORT_TESTER_ONE), 2, UI_FONT_LABEL,
                           UI_TEXT_FAINT, false);
        return;
    }
    sync_tabs();
    gfx_text(c, COL_X, PORT_TESTER_Y, TR(OUT_PORT_TESTER), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.tester, c, tester_locked());
    gfx_text(c, COL_X, PORT_PWM_Y, TR(OUT_PORT_PWM), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.pwm, c, pwm_locked());
    if (outputs_screen_port_reason(buf, sizeof(buf))) {
        (void)outputs_wrap(c, gfx_rect_make(COL_X, PORT_WHY_Y, COL_W, 16), 18,
                           buf, 2, UI_FONT_LABEL, UI_WARN, false);
    }
}

/* -------------------------------------------------------- the clashes */

const uint8_t *outputs_port_pins(void)
{
    static uint8_t pins[LINK_PG_PORTS_MAX];
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        pins[n] = outputs_screen_port_pin(n);
    }
    return pins;
}

bool outputs_clash_label(const outbind_t *b, uint8_t index,
                         const uint8_t *ports, uint8_t count, char *buf,
                         size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    uint8_t with;
    switch (outbind_pwm_clash_ports(b, index, ports, count, &with)) {
    case OUTBIND_CLASH_PROG_SLICE:
        snprintf(buf, cap, TR(OUT_SLICE_OF), (unsigned)with);
        return true;
    case OUTBIND_CLASH_COMPARE:
        snprintf(buf, cap, TR(OUT_COMPARE_OF), (unsigned)with);
        return true;
    case OUTBIND_CLASH_NONE:
    default:
        return false;
    }
}

bool outputs_clash_reason(const outbind_t *b, int index,
                          const uint8_t *ports, uint8_t count, char *buf,
                          size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (b == NULL) {
        return false;
    }
    if (index < 0 || index >= (int)outbind_pin_count(b->board)) {
        return false;
    }
    uint8_t with;
    const outbind_clash_t why = outbind_pwm_clash_ports(b, (uint8_t)index,
                                                        ports, count, &with);
    if (why == OUTBIND_CLASH_NONE) {
        return false;
    }
    snprintf(buf, cap,
             (why == OUTBIND_CLASH_PROG_SLICE) ? TR(OUT_SLICE_WHY)
                                               : TR(OUT_COMPARE_WHY),
             (unsigned)outbind_pins(b->board)[index].gpio, (unsigned)with);
    return true;
}
