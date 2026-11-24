/**
 * @file
 * @brief SEARCH (wl_search.h): the model, the touches and the screen's
 *        table.  wl_search_draw.c draws it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wl_search.h"

#include <stdio.h>
#include <string.h>

/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "settings_screen.h"
#include "ui_theme.h"
#include "wl_search_int.h"

wls_state_t wls;

/** @brief Repaint on the next frame of each framebuffer. */
static void touched(void) { wls.drawn_mask = 0u; }

void wl_search_invalidate(void) { touched(); }

/* ------------------------------------------------------------ the model */

void wl_search_view(wls_view_t *out)
{
    if (out == NULL) {
        return;
    }
    /* Zeroed first: the strings' tails and the padding read alike in two
     * views of the same state, which the render's key relies on. */
    memset(out, 0, sizeof(*out));
    if (wls.dev != NULL && wls.dev->view != NULL) {
        wls.dev->view(out);
    }
    if (out->found_n > WLS_FOUND_MAX) {
        out->found_n = (uint8_t)WLS_FOUND_MAX;
    }
}

void wl_search_put(wls_view_t *v, unsigned i, const char *addr,
                   const char *name, int8_t rssi)
{
    if (v == NULL || i >= WLS_FOUND_MAX) {
        return;
    }
    snprintf(v->found[i].addr, sizeof(v->found[i].addr), "%s",
             addr != NULL ? addr : "");
    snprintf(v->found[i].name, sizeof(v->found[i].name), "%s",
             name != NULL ? name : "");
    v->found[i].rssi = rssi;
}

const wls_device_t *wl_search_device(void) { return wls.dev; }

bool wl_search_asking(void) { return wls.asking; }

const wls_dev_t *wl_search_asked(void)
{
    return wls.asking ? &wls.ask : NULL;
}

const char *wl_search_dev_line(const wls_dev_t *d, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (d == NULL) {
        return buf;
    }
    const char *who = d->name[0] != '\0' ? d->name : d->addr;
    if (d->rssi == 127) {
        snprintf(buf, n, "%s", who);     /* no report carried an RSSI */
    } else {
        snprintf(buf, n, TR(DIAL_FOUND_ROW), who, (int)d->rssi);
    }
    return buf;
}

const char *wl_search_state_line(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    wls_view_t v;
    wl_search_view(&v);
    if (v.scanning) {
        snprintf(buf, n, TR(WLS_SCANNING), (unsigned)v.scan_left_s);
    } else if (v.scan_cut) {
        snprintf(buf, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (!v.scan_done) {
        snprintf(buf, n, "%s", TR(WLS_NO_SCAN));
    } else if (v.found_n == 0u) {
        snprintf(buf, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(buf, n, TR(WLS_FOUND), (unsigned)v.found_n);
    }
    return buf;
}

/** @brief The press dropped: nothing acts on its release. */
static void drop_press(void)
{
    wls.have_press = false;
    wls.hit = WLS_HIT_NONE;
    wls.press_addr[0] = '\0';
    touched();
}

/** @brief The question closed. */
static void close_ask(void)
{
    wls.asking = false;
    memset(&wls.ask, 0, sizeof(wls.ask));
    touched();
}

/**
 * @brief The band's text of a device's entry.
 * @param id The text.
 */
static void say(ui_text_id_t id)
{
    ui_router_set_alert(ui_tr(id));
}

/**
 * @brief A found device tapped: the question opens on it, as the tap took
 *        it; a running scan refuses it, and a row that names another
 *        device than the press took says the list changed.
 * @param i    The row.
 * @param addr The address the row held at the press.
 */
static void ask(int i, const char *addr)
{
    wls_view_t v;
    wl_search_view(&v);
    if (v.scanning) {
        say(wls.dev->bind_scanning);
        return;
    }
    if (i < 0 || i >= (int)v.found_n
        || strcmp(v.found[i].addr, addr) != 0) {
        say(wls.dev->bind_gone);
        return;
    }
    wls.asking = true;
    wls.ask_i = (uint8_t)i;
    wls.ask = v.found[i];
    touched();
}

/**
 * @brief BIND: the entry's bind of the asked device by its row, unless a
 *        scan runs or the row holds another device; the question
 *        closes either way.
 */
static void bind(void)
{
    wls_view_t v;
    wl_search_view(&v);
    const uint8_t i = wls.ask_i;
    if (v.scanning) {
        say(wls.dev->bind_scanning);
    } else if (i >= v.found_n || strcmp(v.found[i].addr, wls.ask.addr) != 0) {
        say(wls.dev->bind_gone);
    } else if (wls.dev->bind != NULL) {
        wls.dev->bind(i);
    }
    close_ask();
}

/**
 * @brief What lies under (@p x, @p y) and takes a press now: the
 *        question's buttons while it is open and nothing else, else SCAN
 *        AGAIN or a found device.
 * @param x The body's x.
 * @param y The body's y.
 * @return WLS_HIT_*.
 */
static int hit_at(int x, int y)
{
    if (wls.asking) {
        if (gfx_rect_contains(wl_search_part(WLS_PART_CANCEL), x, y)) {
            return WLS_HIT_CANCEL;
        }
        if (gfx_rect_contains(wl_search_part(WLS_PART_BIND), x, y)) {
            return WLS_HIT_BIND;
        }
        return WLS_HIT_NONE;
    }
    if (gfx_rect_contains(wl_search_part(WLS_PART_AGAIN), x, y)) {
        return WLS_HIT_AGAIN;
    }
    wls_view_t v;
    wl_search_view(&v);
    for (int i = 0; i < (int)v.found_n; ++i) {
        if (gfx_rect_contains(wl_search_row_rect(i), x, y)) {
            return WLS_HIT_DEV0 + i;
        }
    }
    return WLS_HIT_NONE;
}

/**
 * @brief A tap's action.
 * @param hit  What the tap landed on.
 * @param addr A found device's address as the press took it; "" for any
 *             other control.
 */
static void act(int hit, const char *addr)
{
    if (wls.dev == NULL) {
        return;
    }
    if (hit >= WLS_HIT_DEV0) {
        ask(hit - WLS_HIT_DEV0, addr);
        return;
    }
    switch (hit) {
    case WLS_HIT_AGAIN:
        if (wls.dev->scan != NULL) {
            wls.dev->scan();
        }
        break;
    case WLS_HIT_CANCEL:
        close_ask();
        break;
    case WLS_HIT_BIND:
        bind();
        break;
    default:
        break;
    }
}

/**
 * @brief ui_screen_t's event: one press at a time; a tap acts on its
 *        release over what it pressed.
 * @param evt The event, in the body's coordinates.
 */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (wls.have_press) {
            return;                /* one press at a time */
        }
        wls.hit = hit_at(x, y);
        if (wls.hit != WLS_HIT_NONE) {
            wls.have_press = true;
            wls.press_id = evt->point.id;
            wls.press_addr[0] = '\0';
            if (wls.hit >= WLS_HIT_DEV0) {
                /* The device the finger came down on: its UP opens the
                 * question on this one only. */
                wls_view_t v;
                wl_search_view(&v);
                memcpy(wls.press_addr, v.found[wls.hit - WLS_HIT_DEV0].addr,
                       sizeof(wls.press_addr));
            }
        }
        touched();
        return;
    }
    if (!wls.have_press || evt->point.id != wls.press_id
        || evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = wls.hit;
    char addr[WLS_ADDR_MAX];
    memcpy(addr, wls.press_addr, sizeof(addr));
    drop_press();
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit, addr);
    }
}

/** @brief ui_screen_t's reset: no device, nothing pressed or asked. */
static void reset(void)
{
    memset(&wls, 0, sizeof(wls));
    wls.hit = WLS_HIT_NONE;
}

/** @brief ui_screen_t's cancel: the press ends without acting. */
static void cancel(void) { drop_press(); }

/** @brief ui_screen_t's enter: the list, no press, no question. */
static void enter(void)
{
    drop_press();
    close_ask();
}

/** @brief ui_screen_t's leave: the press and the question end; the way
 *         back to SETUP finds WIRELESS on the device's entry. */
static void leave(void)
{
    drop_press();
    close_ask();
    if (ui_router_leaving_to() == SCREEN_SETUP) {
        settings_screen_resume();
    }
}

/** @brief The home tag closes the question first. @return Whether it
 *         did. */
static bool back(void)
{
    if (!wls.asking) {
        return false;
    }
    drop_press();
    close_ask();
    return true;
}

/** @brief The question covers the screen. @return Whether it is open. */
static bool covered(void) { return wls.asking; }

/** @brief The screen, for the router. */
static const ui_screen_t k_screen = {
    .title   = "SEARCH",
    .covered = covered,
    .reset   = reset,
    .enter   = enter,
    .leave   = leave,
    .event   = event,
    .cancel  = cancel,
    .render  = wls_render,
    .back    = back,
    .help_count = wls_help_count,
    .help_page  = wls_help_page,
};

const ui_screen_t *wl_search_screen(void) { return &k_screen; }

void wl_search_open(const wls_device_t *dev, bool scan)
{
    if (dev == NULL) {
        return;
    }
    if (ui_router_current() == SCREEN_WL_SEARCH) {
        drop_press();
        close_ask();
    }
    wls.dev = dev;
    touched();
    /* The screen first: a refusal the scan says on the band is read on
     * it. */
    ui_router_goto(SCREEN_WL_SEARCH);
    if (scan && dev->scan != NULL) {
        dev->scan();
    }
}
