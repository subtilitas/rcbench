/**
 * @file motor_logname.c
 * @brief RENAME LOG on MOTOR & ESC: the button in the strip row, the
 *        keyboard it opens for the label, and the request the panel's
 *        logger runs (motor_screen.h).
 *
 * The button is a tap: renaming switches no power.  It is live while the
 * bench is SAFE with ARM not held, no log file is open, and the MOTOR log
 * closed since power-on is on the card mounted now.  Dimmed, a tap says
 * the first reason in that order in the alert band.  The keyboard is
 * FILES' name keyboard over the body, with its cursor: "<-", "->" and a
 * tap in the field move it, and it opens at the label's end.  It types
 * the label alone, and its title names the run by its plain name.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "fm_io.h"
#include "log_name.h"
#include "motor_screen.h"
#include "settings.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: motor_screen_int.h's layout macros have short names, which no
 * header included after it may meet. */
#include "motor_live_int.h"

/** @brief Where the keyboard draws: the body as FILES' name keyboard. */
#define MLN_TK_AREA gfx_rect_make(UI_MARGIN, 4, UI_PANEL_W - 2 * UI_MARGIN, \
                                  UI_BODY_H - UI_ALERT_H - 8)

/** @brief RENAME LOG and its keyboard. */
static struct {
    char         last[LOG_LABELLED_SIZE]; /**< the log closed last, "" none */
    bool         on_card;     /**< it is on the card mounted now           */
    bool         logging;     /**< the logger has a file open              */
    ui_textkey_t k;           /**< the keyboard                            */
    char         label[LOG_LABELLED_SIZE]; /**< the label OK asked for     */
    bool         want;        /**< a request waits to be taken             */
    bool         out;         /**< taken, its answer not yet back          */
    bool         down;        /**< a press on the button                   */
    uint8_t      down_id;     /**< its contact                             */
    bool         live;        /**< the button's state as last followed     */
    uint32_t     rev;         /**< bumped when the button's look changes   */
    uint32_t     drawn[2];    /**< rev each framebuffer shows              */
    bool         drawn_open[2]; /**< the keyboard shown in each one        */
    uint32_t     drawn_key[2];  /**< the keyboard's revision in each one   */
} g; /**< RENAME LOG's state */

/**
 * @brief The button's rectangle in the strip row.
 * @return x 449 to 629, the strip's 40px.
 */
static gfx_rect_t button_rect(void)
{
    return (gfx_rect_t){ MO_LOG_X, MO_STRIP_Y, MO_LOG_W, MO_STRIP_H };
}

/**
 * @brief Why the button is dimmed, into @p out.
 * @param out Takes the reason; may be NULL.
 * @param n   Bytes at @p out.
 * @return True when it is live: nothing written.
 */
static bool why_not(char *out, size_t n)
{
    const char *why = NULL;
    if (!mo_endpoints_free()) {
        why = TR(FM_GATE_ARMED);
    } else if (g.logging) {
        why = TR(FM_GATE_LOGGING);
    } else if (g.want || g.out) {
        why = TR(MO_LOG_BUSY);
    } else if (g.last[0] == '\0') {
        why = settings_get_bool(SET_LOG_AUTO) ? TR(MO_LOG_NONE)
                                              : TR(LOG_AUTO_OFF);
    } else if (!g.on_card) {
        if (out != NULL && n > 0u) {
            snprintf(out, n, TR(MO_LOG_MISSING), g.last);
        }
        return false;
    } else {
        return true;
    }
    if (out != NULL && n > 0u) {
        snprintf(out, n, "%s", why);
    }
    return false;
}

/* ---------------------------------------------------------- the keyboard */

/*
 * The two calls that reach into the name keyboard (ui_textkey.h).  The
 * field takes LOG_LABEL_MAX bytes, the label behind MOTORnnn_ and before
 * .CSV, the cursor working to that most (ui_textkey.h); the open leaves the
 * cursor at the label's end.  OK on an empty field is the plain name: the
 * file keyboard refuses an empty name, so that one release is answered
 * here, by the same rule the keyboard keeps for a key (the contact that
 * pressed OK lifts on it).
 */

/**
 * @brief Open the keyboard on @p label.
 * @param title Its title.
 * @param label The label the log carries now.
 */
static void key_open(const char *title, const char *label)
{
    ui_textkey_open_file(&g.k, MLN_TK_AREA, title, label);
    g.k.max_len = (int)LOG_LABEL_MAX;
}

/**
 * @brief One event to the keyboard.
 * @param evt The event.
 * @param out Takes the label at OK.
 * @param n   Bytes at @p out.
 * @return What the keyboard said.
 */
static ui_textkey_result_t key_event(const touch_event_t *evt, char *out,
                                     size_t n)
{
    if (evt->type == TOUCH_EVENT_UP && g.k.len == 0
        && g.k.pressed == UI_TK_OK && evt->point.id == g.k.press_id
        && touch_event_is_tap_up(evt)
        && gfx_rect_contains(ui_textkey_key_rect(&g.k, UI_TK_OK),
                             evt->point.x, evt->point.y)) {
        ui_textkey_close(&g.k);
        out[0] = '\0';
        return UI_TEXTKEY_OK;
    }
    return ui_textkey_event(&g.k, evt, out, n);
}

/**
 * @brief The keyboard over the log closed last, titled by its plain name.
 */
static void open_keyboard(void)
{
    char plain[LOG_LABELLED_SIZE];
    char label[LOG_LABELLED_SIZE];
    char title[sizeof(g.k.title)];
    (void)log_name_label(plain, sizeof(plain), g.last, "");
    (void)log_name_label_of(label, sizeof(label), g.last);
    snprintf(title, sizeof(title), TR(MO_TK_LOG), plain);
    key_open(title, label);
    g.down = false;
    ++g.rev;
}

/**
 * @brief OK with @p label: a request for the logger, unless the name stays.
 * @param label The label typed, ends trimmed; "" for the plain name.
 */
static void ask(const char *label)
{
    char name[LOG_LABELLED_SIZE];
    if (!log_name_label(name, sizeof(name), g.last, label)) {
        ui_router_set_alert(TR(FM_E_NAME));
        return;
    }
    if (strcmp(name, g.last) == 0) {
        return;                 /* the name it has: nothing to do */
    }
    snprintf(g.label, sizeof(g.label), "%s", label);
    g.want = true;
    ++g.rev;
}

/* ------------------------------------------------------------- the seam */

void mln_reset(void)
{
    memset(&g, 0, sizeof(g));
    g.k.pressed = -1;
    mln_invalidate();
}

bool mln_shown(void)
{
    return g.k.open;
}

bool mln_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return false;
    }
    if (g.k.open) {
        char label[UI_TEXTKEY_PASS_MAX + 1];
        const ui_textkey_result_t r = key_event(evt, label, sizeof(label));
        if (r == UI_TEXTKEY_OK) {
            ask(label);
        }
        return true;
    }
    const gfx_rect_t b = button_rect();
    const int x = evt->point.x;
    const int y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (g.down || !gfx_rect_contains(b, x, y)) {
            return false;
        }
        g.down = true;
        g.down_id = evt->point.id;
        ++g.rev;
        return true;
    }
    if (!g.down || evt->point.id != g.down_id) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        g.down = false;
        ++g.rev;
        if (touch_event_is_tap_up(evt) && gfx_rect_contains(b, x, y)) {
            char why[UI_ALERT_MAX];
            if (why_not(why, sizeof(why))) {
                open_keyboard();
            } else {
                ui_router_set_alert(why);
            }
        }
    }
    return true;
}

void mln_close(void)
{
    if (g.k.open) {
        ui_textkey_close(&g.k);
    }
    if (g.down) {
        g.down = false;
        ++g.rev;
    }
}

void mln_cancel(void)
{
    ui_textkey_cancel_press(&g.k);
    if (g.down) {
        g.down = false;
        ++g.rev;
    }
}

void mln_tick(void)
{
    /* The keyboard covers DISARM: a bench that leaves SAFE under it, by a
     * report or an ask from elsewhere, closes it. */
    if (g.k.open && !mo_bench_safe()) {
        ui_textkey_close(&g.k);
    }
    const bool live = why_not(NULL, 0u);
    if (live != g.live) {
        g.live = live;
        ++g.rev;
    }
}

void mln_invalidate(void)
{
    g.drawn[0] = g.drawn[1] = UINT32_MAX;
    g.drawn_key[0] = g.drawn_key[1] = UINT32_MAX;
}

void mln_forget(int buf)
{
    g.drawn[buf & 1] = UINT32_MAX;
}

void mln_render_button(gfx_canvas_t *c, int buf)
{
    const int b = buf & 1;
    if (g.drawn[b] == g.rev) {
        return;
    }
    g.drawn[b] = g.rev;
    const gfx_rect_t r = button_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_button_font(c, r, TR(MO_LOG_RENAME), ui_theme_color(UI_C_PANEL_SUNK),
                   g.down, g.live, MO_FONT_LABEL);
}

bool mln_render_over(gfx_canvas_t *c, int buf)
{
    const int b = buf & 1;
    if (g.k.open != g.drawn_open[b]) {
        g.drawn_open[b] = g.k.open;
        g.drawn_key[b] = UINT32_MAX;
        if (!g.k.open) {
            /* The body under it, painted whole. */
            motor_invalidate();
            ++s.ctrl_rev;
            ++s.arm_rev;
        }
    }
    if (!g.k.open) {
        return false;
    }
    if (g.drawn_key[b] != g.k.revision) {
        g.drawn_key[b] = g.k.revision;
        gfx_fill_rect(c, 0, 0, UI_PANEL_W, UI_BODY_H,
                      ui_theme_color(UI_C_BG));
        ui_textkey_render(&g.k, c);
    }
    return true;
}

/* ------------------------------------------------- the screen's interface */

void motor_screen_set_last_log(const char *name, bool on_card, bool logging)
{
    snprintf(g.last, sizeof(g.last), "%s", (name != NULL) ? name : "");
    g.on_card = on_card;
    g.logging = logging;
}

bool motor_screen_log_take(char *label, size_t n)
{
    if (!g.want || label == NULL || n == 0u) {
        return false;
    }
    snprintf(label, n, "%s", g.label);
    g.want = false;
    g.out = true;
    return true;
}

void motor_screen_log_done(int err, const char *name)
{
    const char *nm = (name != NULL) ? name : "";
    char line[UI_ALERT_MAX];
    g.out = false;
    ++g.rev;
    switch ((fm_err_t)err) {
    case FM_OK:
        snprintf(g.last, sizeof(g.last), "%s", nm);
        snprintf(line, sizeof(line), TR(MO_LOG_RENAMED), nm);
        break;
    case FM_ERR_NOT_FOUND:
    case FM_ERR_NO_CARD:
    case FM_ERR_CARD_GONE:
        snprintf(line, sizeof(line), TR(MO_LOG_MISSING), g.last);
        break;
    case FM_ERR_BUSY:
        snprintf(line, sizeof(line), "%s", TR(MO_LOG_BUSY));
        break;
    default:
        files_err_text((fm_err_t)err, nm, line, sizeof(line));
        break;
    }
    ui_router_set_alert(line);
}

bool motor_screen_logname_shown(void)
{
    return g.k.open;
}

bool motor_screen_logname_live(void)
{
    return why_not(NULL, 0u);
}

const ui_textkey_t *motor_screen_logname_keyboard(void)
{
    return &g.k;
}
