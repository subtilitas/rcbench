/**
 * @file
 * @brief WI-FI: wifi_screen.h says what the screen is for.  PANEL UPDATE, the
 * screen the same header declares, is update_screen.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_screen.h"

#include <stdio.h>
#include <string.h>

#include "logs_strip.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The area below the band: 1024 x 540px. */
#define W UI_PANEL_W
/** @brief The body's height under the band, UI_BODY_H px. */
#define H UI_BODY_H

/**
 * @brief The faces of the 1024 x 600 type scale the screen draws in:
 *        WF_FONT_LABEL (10 x 20) for the buttons, the row labels, the
 *        card's lines and the signal column; WF_FONT_BODY (12 x 24) for
 *        the lines under the rows, the network names of the list and the
 *        question's sentences; WF_FONT_HEAD (20 x 36) for the row values,
 *        the scan's heading and the question's title and versions.
 */
#define WF_FONT_LABEL (&gfx_font_10x20)
#define WF_FONT_BODY  UI_FONT_BODY            /**< 12 x 24: sentences. */
#define WF_FONT_HEAD  (&gfx_font_20x36)       /**< 20 x 36: values. */

/**
 * @brief The lowest body row anything is drawn on, plus one: 4px above
 *        the alert band, body y 492.
 */
#define FOOT    (UI_ALERT_Y - UI_BAND_H - 4)

/*
 * With the radio off: under the LOGS tile's strip (logs_strip.h), five
 * buttons of 460 x 60px (69.0 x 9.0mm) 8px apart on the left, from body
 * y 56; the card on the right, 16px beside them, to 8px from the right
 * edge and down to FOOT.
 * The card's lines are 49 cells of WF_FONT_LABEL, 24px apart.
 */
#define BX       16          /**< The buttons' left edge. */
#define BY       56          /**< The first button's top. */
#define BW       460         /**< A button's width, px. */
#define BH       60          /**< A button's height, px. */
#define BPITCH   68          /**< The buttons' pitch, px. */
#define RX       (BX + BW + 16) /**< The card's left edge. */
#define RY       56          /**< Its top. */
#define RW       (W - UI_MARGIN - RX) /**< Its width: 524px. */
#define RH       (FOOT - RY) /**< Its height: 436px. */
#define CARD_P   24          /**< The card's line pitch, px. */

/*
 * With the radio on: under the strip, rows of 54px from body y 52, a label
 * column
 * of 240px and a value column of 740px, an SSID of 32 characters
 * 640px of it in WF_FONT_HEAD; the lines under the rows in
 * WF_FONT_BODY 28px apart; WI-FI OFF 460 x 60px, 8px above FOOT.
 */
#define LBL_X    16          /**< The label column's left edge. */
#define LBL_W    240         /**< Its width, px. */
#define VAL_X    (LBL_X + LBL_W + 8) /**< The value column's left edge. */
#define VAL_W    740         /**< Its width, px. */
#define ROW_Y    52          /**< The first row's top. */
#define ROW_P    54          /**< The rows' pitch, px. */
#define LINE_P   28          /**< The pitch of the lines under the rows, px. */
#define OFF_Y    (FOOT - 8 - BH) /**< WI-FI OFF's top: body y 424. */
/** @brief The browser writes' button, beside WI-FI OFF at the right edge,
 *         the same size: body x 548. */
#define WR_X     (W - 16 - BW)

/*
 * The scan list: two columns of eight rows of 48px, 52px apart,
 * each row 492px wide on a ground 4px larger every side, the two
 * grounds 8px apart across the body; an SSID of 32 characters is
 * 384px of the row in WF_FONT_BODY beside the signal's 80px.
 * Under it SCAN AGAIN, HIDDEN NETWORK and CANCEL, 328 x 56px each,
 * 12px apart, ending at FOOT.
 */
#define SC_X0    (UI_MARGIN + 4) /**< The left column's rows' left edge. */
#define SC_X1    (W / 2 + 8) /**< The right column's. */
#define SC_Y     (UI_MARGIN + 4) /**< The first row's top. */
#define SC_W     492         /**< A row's width, px. */
#define SC_H     48          /**< A row's height, px. */
#define SC_P     52          /**< The rows' pitch, px. */
#define SC_ROWS  8           /**< Rows a column. */
#define SC_BW    328         /**< A scan button's width, px. */
#define SC_BH    UI_BTN_H_TALL /**< Its height, px. */
#define SC_BY    (FOOT - SC_BH) /**< The scan buttons' top: body y 436. */
/** @brief Scan button @p i's left edge. */
#define SC_BX(i) (UI_MARGIN + (i) * (SC_BW + 12))

/*
 * The question: a card of 1008 x 484px over the body from
 * UI_MARGIN down to FOOT; CANCEL and HOLD 2s: INSTALL 460 x 72px
 * at its foot, 16px in; its sentences in WF_FONT_BODY, 80 cells a
 * line, 28px apart.
 */
#define Q_X      UI_MARGIN   /**< The question's left edge. */
#define Q_Y      UI_MARGIN   /**< Its top. */
#define Q_W      (W - 2 * UI_MARGIN) /**< Its width, px. */
#define Q_H      (FOOT - Q_Y) /**< Its height, px. */
#define Q_BW     460         /**< A button's width, px. */
#define Q_BH     72          /**< Its height, px. */
#define Q_BY     (Q_Y + Q_H - 16 - Q_BH) /**< The buttons' top: body y 404. */

/* Every control a finger tall and above the alert band. */
_Static_assert(BH >= UI_TAP_MIN && SC_H >= UI_TAP_MIN
               && SC_BH >= UI_TAP_MIN && Q_BH >= UI_TAP_MIN,
               "a WI-FI control under UI_TAP_MIN");
_Static_assert(BY + 4 * BPITCH + BH <= OFF_Y && OFF_Y + BH <= FOOT,
               "the buttons reach the alert band");
_Static_assert(LOGS_STRIP_FOOT + 4 <= ROW_Y && LOGS_STRIP_FOOT + 4 <= BY,
               "the first row starts below the tile's strip");
/* The most the running view holds: four rows, then the lines OPEN_ALL,
 * PLAIN or CHANNEL, IDLE, WRITES and, 8px on, RECEIVING. */
_Static_assert(ROW_Y + 4 * ROW_P + 4 + 4 * LINE_P + 8 + LINE_P <= OFF_Y,
               "the running view's lines reach WI-FI OFF");
_Static_assert(BX + BW + 16 <= WR_X, "the writes' button meets WI-FI OFF");
_Static_assert(SC_Y + (SC_ROWS - 1) * SC_P + SC_H + 4 + 8 <= SC_BY,
               "the scan list reaches its buttons");
_Static_assert(SC_X1 + SC_W + 4 <= W - UI_MARGIN
               && SC_BX(2) + SC_BW <= W - UI_MARGIN,
               "the scan list leaves the body");

/**
 * @brief The keyboard, over the whole body, UI_MARGIN in from its sides and
 *        top, and ending 4px above the alert band: 1008 x 484px, keys of
 *        93 x 71px.
 */
#define KEY_AREA gfx_rect_make(UI_MARGIN, UI_MARGIN, W - 2 * UI_MARGIN, \
                               FOOT - UI_MARGIN)

typedef enum {
    SUB_MAIN = 0,
    SUB_SCAN,
    SUB_KEY_SSID,
    SUB_KEY_PASS,
} sub_t;

enum {
    HIT_NONE = 0,
    HIT_AP,
    HIT_JOIN,
    HIT_CHOOSE,
    HIT_FORGET,
    HIT_CODE,
    HIT_OFF,
    HIT_Q_CANCEL,
    HIT_INSTALL,
    HIT_RESCAN,
    HIT_HIDDEN,
    HIT_SCAN_CANCEL,
    HIT_WRITES,        /* HOLD 2s: ALLOW BROWSER WRITES */
    HIT_WRITES_OFF,    /* BROWSER WRITES OFF */
    HIT_ROW,           /* HIT_ROW + i for row i */
};

static struct {
    wifi_view_t  v;
    unsigned     req;
    sub_t        sub;
    ui_textkey_t tk;
    char         pick[WIFI_SSID_MAX + 1];
    char         save_ssid[WIFI_SSID_MAX + 1];
    char         save_pass[WIFI_PASS_MAX + 1];
    int          hit;
    uint8_t      press_id;
    bool         have_press;
    ui_hold_t    hold;
    int          hold_hit;      /**< the held control: HIT_INSTALL or
                                     HIT_WRITES                          */
    bool         installing;    /**< INSTALL went: leaving keeps the image */
    bool         q_cancelled;   /**< the home tag answered the question */
    unsigned     drawn_mask;    /**< the buffers drawn since a change   */
} s;  /**< the screen's state: the view, the keyboard, the press */

static void touched(void) { s.drawn_mask = 0u; }

gfx_rect_t wifi_screen_key_area(void)
{
    return KEY_AREA;
}

const ui_textkey_t *wifi_screen_textkey(void)
{
    return &s.tk;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    ui_hold_reset(&s.hold);
}

/* ------------------------------------------------------------- the model */

static bool staged(void) { return s.v.up == WIFI_UP_STAGED; }

static bool install_allowed(void)
{
    return staged() && s.v.offer != WIFI_OFFER_INSTALLED
           && s.v.gate == WIFI_GATE_OPEN;
}

static bool radio_on(void)
{
    return s.v.radio != WIFI_RADIO_OFF && s.v.radio != WIFI_RADIO_SCANNING;
}

/** @brief The page is served: the access point up or the network joined.
 *  @return true while a browser can reach it */
static bool serving(void)
{
    return s.v.radio == WIFI_RADIO_AP || s.v.radio == WIFI_RADIO_JOINED;
}

/** @brief The writes' button shows: HOLD, the page served, no image
 *         waiting.  @return true when it does */
static bool writes_button(void)
{
    return s.v.writes == WIFI_WRITES_HOLD && serving() && !staged()
           && s.sub != SUB_SCAN;
}

/** @brief HOLD 2s: ALLOW BROWSER WRITES takes a hold.
 *  @return true while the button shows and writes are not allowed yet */
static bool writes_hold_allowed(void)
{
    return writes_button() && !s.v.writes_open;
}

/** @brief Whether the control the hold is on may still act.
 *  @return true for the INSTALL hold while an install is allowed, and the
 *          writes' hold while it is allowed */
static bool hold_allowed(void)
{
    return (s.hold_hit == HIT_WRITES) ? writes_hold_allowed()
                                      : install_allowed();
}

/* A press on the hold that the model took away under it ends here. */
static void drop_hold_if_gone(void)
{
    if (!hold_allowed() && ui_hold_left(&s.hold) && s.hit == s.hold_hit) {
        s.hit = HIT_NONE;
        s.have_press = false;
    }
}

void wifi_screen_set(const wifi_view_t *v)
{
    /* Padding that differs reads as a change and costs one repaint; a
     * member that differs always differs in its bytes. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &s.v, sizeof(s.v)) == 0) {
        return;
    }
    s.v = *v;
    if (!staged()) {
        s.q_cancelled = false;
    }
    drop_hold_if_gone();
    touched();
}

void wifi_screen_set_unavailable(void)
{
    if (s.v.block == WIFI_WHY_UNAVAILABLE) {
        return;
    }
    memset(&s.v, 0, sizeof(s.v));
    s.v.block = WIFI_WHY_UNAVAILABLE;
    s.v.code_on = true;
    s.sub = SUB_MAIN;
    drop_hold_if_gone();
    touched();
}

void wifi_screen_invalidate(void) { touched(); }

const wifi_view_t *wifi_screen_view(void) { return &s.v; }

unsigned wifi_screen_take(void)
{
    const unsigned r = s.req;
    s.req = 0u;
    return r;
}

void wifi_screen_net(char ssid[WIFI_SSID_MAX + 1], char pass[WIFI_PASS_MAX + 1])
{
    memcpy(ssid, s.save_ssid, sizeof(s.save_ssid));
    memcpy(pass, s.save_pass, sizeof(s.save_pass));
}

/* ------------------------------------------------------------ geometry */

/**
 * @brief Button @p row of the radio-off view.
 * @param row 0 ACCESS POINT to 4 ACCESS CODE.
 * @return Its rectangle, body coordinates.
 */
static gfx_rect_t btn(int row)
{
    return gfx_rect_make(BX, BY + row * BPITCH, BW, BH);
}

/** @brief WI-FI OFF.  @return Its rectangle. */
static gfx_rect_t off_rect(void) { return gfx_rect_make(BX, OFF_Y, BW, BH); }

/** @brief The question's CANCEL.  @return Its rectangle. */
static gfx_rect_t q_cancel_rect(void)
{
    return gfx_rect_make(Q_X + 16, Q_BY, Q_BW, Q_BH);
}

/** @brief The writes' button.  @return Its rectangle. */
static gfx_rect_t writes_rect(void)
{
    return gfx_rect_make(WR_X, OFF_Y, BW, BH);
}

/** @brief HOLD 2s: INSTALL.  @return Its rectangle. */
static gfx_rect_t install_rect(void)
{
    return gfx_rect_make(Q_X + Q_W - 16 - Q_BW, Q_BY, Q_BW, Q_BH);
}

/**
 * @brief Row @p i of the scan list: eight a column.
 * @param i 0 to WIFI_SCAN_ROWS - 1.
 * @return Its rectangle.
 */
static gfx_rect_t row_rect(int i)
{
    return gfx_rect_make((i < SC_ROWS) ? SC_X0 : SC_X1,
                         SC_Y + (i % SC_ROWS) * SC_P, SC_W, SC_H);
}

/** @brief SCAN AGAIN.  @return Its rectangle. */
static gfx_rect_t rescan_rect(void)
{
    return gfx_rect_make(SC_BX(0), SC_BY, SC_BW, SC_BH);
}

/** @brief HIDDEN NETWORK.  @return Its rectangle. */
static gfx_rect_t hidden_rect(void)
{
    return gfx_rect_make(SC_BX(1), SC_BY, SC_BW, SC_BH);
}

/** @brief The scan list's CANCEL.  @return Its rectangle. */
static gfx_rect_t scan_cancel_rect(void)
{
    return gfx_rect_make(SC_BX(2), SC_BY, SC_BW, SC_BH);
}

static bool in(gfx_rect_t r, int x, int y) { return gfx_rect_contains(r, x, y); }

/* ------------------------------------------------------------ the views */

typedef enum { VIEW_MAIN, VIEW_RUNNING, VIEW_SCAN, VIEW_QUESTION } view_t;

static view_t view_of(void)
{
    if (staged()) {
        return VIEW_QUESTION;
    }
    if (s.sub == SUB_SCAN) {
        return VIEW_SCAN;
    }
    return radio_on() ? VIEW_RUNNING : VIEW_MAIN;
}

/* The hit under a press, in the view showing; HIT_NONE for none. */
static int hit_at(int x, int y)
{
    switch (view_of()) {
    case VIEW_QUESTION:
        if (in(q_cancel_rect(), x, y)) {
            return HIT_Q_CANCEL;
        }
        if (install_allowed() && in(install_rect(), x, y)) {
            return HIT_INSTALL;
        }
        return HIT_NONE;
    case VIEW_SCAN:
        if (s.v.radio != WIFI_RADIO_SCANNING) {
            for (int i = 0; i < s.v.scan_n && i < WIFI_SCAN_ROWS; ++i) {
                if (in(row_rect(i), x, y)) {
                    return HIT_ROW + i;
                }
            }
            if (s.v.block == WIFI_WHY_NONE && in(rescan_rect(), x, y)) {
                return HIT_RESCAN;
            }
            if (in(hidden_rect(), x, y)) {
                return HIT_HIDDEN;
            }
        }
        return in(scan_cancel_rect(), x, y) ? HIT_SCAN_CANCEL : HIT_NONE;
    case VIEW_RUNNING:
        if (writes_button() && in(writes_rect(), x, y)) {
            return s.v.writes_open ? HIT_WRITES_OFF : HIT_WRITES;
        }
        return in(off_rect(), x, y) ? HIT_OFF : HIT_NONE;
    case VIEW_MAIN:
    default: {
        if (s.v.block == WIFI_WHY_UNAVAILABLE) {
            return HIT_NONE;
        }
        const bool free_ = (s.v.block == WIFI_WHY_NONE);
        if (free_ && in(btn(0), x, y)) {
            return HIT_AP;
        }
        if (free_ && s.v.have_net && in(btn(1), x, y)) {
            return HIT_JOIN;
        }
        if (free_ && in(btn(2), x, y)) {
            return HIT_CHOOSE;
        }
        if (s.v.have_net && in(btn(3), x, y)) {
            return HIT_FORGET;
        }
        if (in(btn(4), x, y)) {
            return HIT_CODE;
        }
        return HIT_NONE;
    }
    }
}

static gfx_rect_t hit_rect(int hit)
{
    switch (hit) {
    case HIT_AP:          return btn(0);
    case HIT_JOIN:        return btn(1);
    case HIT_CHOOSE:      return btn(2);
    case HIT_FORGET:      return btn(3);
    case HIT_CODE:        return btn(4);
    case HIT_OFF:         return off_rect();
    case HIT_Q_CANCEL:    return q_cancel_rect();
    case HIT_INSTALL:     return install_rect();
    case HIT_RESCAN:      return rescan_rect();
    case HIT_HIDDEN:      return hidden_rect();
    case HIT_SCAN_CANCEL: return scan_cancel_rect();
    case HIT_WRITES:
    case HIT_WRITES_OFF:  return writes_rect();
    default:
        return (hit >= HIT_ROW) ? row_rect(hit - HIT_ROW)
                                : gfx_rect_make(0, 0, 0, 0);
    }
}

static void act(int hit)
{
    switch (hit) {
    case HIT_AP:      s.req |= WIFI_REQ_AP;   break;
    case HIT_JOIN:    s.req |= WIFI_REQ_JOIN; break;
    case HIT_CHOOSE:
        s.req |= WIFI_REQ_SCAN;
        s.sub = SUB_SCAN;
        break;
    case HIT_FORGET:  s.req |= WIFI_REQ_FORGET; break;
    case HIT_CODE:
        s.req |= s.v.code_on ? WIFI_REQ_CODE_OFF : WIFI_REQ_CODE_ON;
        break;
    case HIT_OFF:     s.req |= WIFI_REQ_OFF; break;
    case HIT_WRITES_OFF: s.req |= WIFI_REQ_WRITES_OFF; break;
    case HIT_Q_CANCEL:
        s.req |= WIFI_REQ_DISCARD;
        break;
    case HIT_RESCAN:  s.req |= WIFI_REQ_SCAN; break;
    case HIT_HIDDEN:
        s.sub = SUB_KEY_SSID;
        ui_textkey_open_wifi(&s.tk, KEY_AREA, TR(WF_KEY_SSID), "", false);
        break;
    case HIT_SCAN_CANCEL:
        s.sub = SUB_MAIN;
        break;
    default:
        if (hit >= HIT_ROW && hit - HIT_ROW < s.v.scan_n) {
            snprintf(s.pick, sizeof(s.pick), "%s", s.v.scan[hit - HIT_ROW]);
            char title[48];
            snprintf(title, sizeof(title), TR(WF_KEY_PASS), s.pick);
            s.sub = SUB_KEY_PASS;
            ui_textkey_open_wifi(&s.tk, KEY_AREA, title, "", true);
        }
        break;
    }
}

/* ------------------------------------------------------------ input */

static void key_event(const touch_event_t *evt)
{
    char out[WIFI_PASS_MAX + 1];
    const ui_textkey_result_t r = ui_textkey_event(&s.tk, evt, out,
                                                   sizeof(out));
    if (r == UI_TEXTKEY_CANCELLED) {
        s.sub = SUB_SCAN;
    } else if (r == UI_TEXTKEY_OK && s.sub == SUB_KEY_SSID) {
        /* The keyboard holds a name to 32 bytes; the copy holds it too. */
        const char *end = memchr(out, '\0', WIFI_SSID_MAX);
        const size_t n = (end != NULL) ? (size_t)(end - out) : WIFI_SSID_MAX;
        memcpy(s.pick, out, n);
        s.pick[n] = '\0';
        char title[48];
        snprintf(title, sizeof(title), TR(WF_KEY_PASS), s.pick);
        s.sub = SUB_KEY_PASS;
        ui_textkey_open_wifi(&s.tk, KEY_AREA, title, "", true);
    } else if (r == UI_TEXTKEY_OK) {
        memcpy(s.save_ssid, s.pick, sizeof(s.save_ssid));
        snprintf(s.save_pass, sizeof(s.save_pass), "%s", out);
        memset(out, 0, sizeof(out));
        s.req |= WIFI_REQ_SAVE_NET;
        s.sub = SUB_MAIN;
    }
    touched();
}

/**
 * @brief One touch event: the keyboard while it shows, the tile's strip on
 *        the first level, else the buttons of the view showing.
 * @param evt The event, in the body's coordinates.
 */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (s.sub == SUB_KEY_SSID || s.sub == SUB_KEY_PASS) {
        if (!staged()) {
            key_event(evt);
            return;
        }
        ui_textkey_close(&s.tk);   /* an image arrived: the question first */
        s.sub = SUB_MAIN;
    }
    /* The tile's strip, on the first level and while no other contact holds
     * a press here.  A tab leaves the screen: leave() switches the radio off. */
    if (view_of() != VIEW_MAIN && view_of() != VIEW_RUNNING) {
        logs_strip_cancel();
    } else if ((!s.have_press || evt->point.id == s.press_id)
               && logs_strip_event(evt, SCREEN_WIFI)) {
        s.hit = HIT_NONE;
        s.have_press = false;
        touched();
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.have_press) {
            return;                /* one press at a time */
        }
        s.hit = hit_at(x, y);
        if (s.hit != HIT_NONE) {
            s.have_press = true;
            s.press_id = evt->point.id;
            if (s.hit == HIT_INSTALL || s.hit == HIT_WRITES) {
                s.hold_hit = s.hit;
                ui_hold_begin(&s.hold, (s.hit == HIT_INSTALL)
                                           ? TX_HOLD_HINT_INSTALL
                                           : TX_HOLD_HINT_WRITES);
            }
        }
        touched();
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off the hold abandons it, as on every held control. */
        if ((s.hit == HIT_INSTALL || s.hit == HIT_WRITES)
            && !in(hit_rect(s.hit), x, y) && ui_hold_leave(&s.hold)) {
            s.hit = HIT_NONE;
            s.have_press = false;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = s.hit;
    s.hit = HIT_NONE;
    s.have_press = false;
    touched();
    if (hit == HIT_INSTALL || hit == HIT_WRITES) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&s.hold, evt, hit_rect(hit));
        return;
    }
    if (touch_event_is_tap_up(evt) && in(hit_rect(hit), x, y)
        && hit_at(x, y) == hit) {
        act(hit);
    }
}

static void tick(float dt_s)
{
    drop_hold_if_gone();
    if (s.hold.down || s.hold.flash_ms > 0) {
        touched();             /* the fill follows the hold */
    }
    ui_hold_flash_step(&s.hold, dt_s);
    if (hold_allowed() && ui_hold_tick(&s.hold, dt_s)) {
        if (s.hold_hit == HIT_WRITES) {
            s.req |= WIFI_REQ_WRITES_ON;
        } else {
            s.req |= WIFI_REQ_INSTALL;
            s.installing = true;
        }
        ui_hold_reached(&s.hold);
    }
}

/** @brief The touch stream lost an event: every press under way ends here. */
static void cancel(void)
{
    if (s.hit == HIT_INSTALL || s.hit == HIT_WRITES) {
        (void)ui_hold_left(&s.hold);
    }
    s.hit = HIT_NONE;
    s.have_press = false;
    ui_textkey_cancel_press(&s.tk);
    logs_strip_cancel();
    touched();
}

/** @brief On the way in: the first level, no hold and no press under way. */
static void enter(void)
{
    s.sub = SUB_MAIN;
    s.installing = false;
    s.q_cancelled = false;
    s.hold_hit = HIT_NONE;
    ui_hold_reset(&s.hold);
    logs_strip_cancel();
    touched();
}

/*
 * The home tag climbs one level, as CANCEL does.  Over the question about
 * a staged image it answers CANCEL, and a list or keyboard under the
 * question closes with it; a second tap while the image is still staged
 * leaves.  From a keyboard it climbs to the network list, keeping nothing
 * typed, and from the list to the buttons.
 */
static bool back(void)
{
    if (staged()) {
        if (s.q_cancelled) {
            return false;
        }
        ui_textkey_close(&s.tk);
        s.sub = SUB_MAIN;
        s.req |= WIFI_REQ_DISCARD;
        s.q_cancelled = true;
        touched();
        return true;
    }
    if (s.sub == SUB_KEY_SSID || s.sub == SUB_KEY_PASS) {
        ui_textkey_close(&s.tk);
        s.sub = SUB_SCAN;
    } else if (s.sub == SUB_SCAN) {
        s.sub = SUB_MAIN;
    } else {
        return false;
    }
    touched();
    return true;
}

static void leave(void)
{
    /* Wi-Fi is on only while the screen is open.  An image the hold sent
     * to be written is the application's now, not a staged one to drop. */
    s.req |= WIFI_REQ_OFF;
    if (!s.installing) {
        s.req |= WIFI_REQ_DISCARD;
    }
    ui_textkey_close(&s.tk);
    s.sub = SUB_MAIN;
    s.hit = HIT_NONE;
    s.have_press = false;
    s.hold_hit = HIT_NONE;
    ui_hold_reset(&s.hold);
}

/* ------------------------------------------------------------ drawing */

/**
 * @brief @p t left aligned in face @p f, in a box of @p w x @p h px from
 *        (@p x, @p y), centred in its height.
 * @param c   Canvas.
 * @param x   Left edge.
 * @param y   Top edge.
 * @param w   Width.
 * @param h   Height.
 * @param t   The text.
 * @param f   The face.
 * @param col Its colour.
 */
static void text_in(gfx_canvas_t *c, int x, int y, int w, int h,
                    const char *t, const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, h), t, f, col, 1, GFX_ALIGN_LEFT);
}

/**
 * @brief @p t in the card from @p y, broken into lines of its 49 cells
 *        CARD_P px apart.
 * @param c   Canvas, or NULL to count the lines only.
 * @param y   The first line's top.
 * @param t   The text.
 * @param col Its colour.
 * @return The y under its last line.
 */
static int para(gfx_canvas_t *c, int y, const char *t, gfx_color_t col)
{
    return y + CARD_P * ui_text_wrap(c, gfx_rect_make(RX + 16, y, RW - 32,
                                                      CARD_P),
                                     CARD_P, t, WF_FONT_LABEL, col);
}

/**
 * @brief What the page is for: one sentence, given in two parts, wrapped
 *        once.
 * @param out Where it goes.
 * @param n   The bytes at @p out.
 */
static void jobs_text(char *out, size_t n)
{
    snprintf(out, n, "%s %s", TR(WF_JOBS_1), TR(WF_JOBS_2));
}

int wifi_screen_jobs_lines(void)
{
    char jobs[128];

    jobs_text(jobs, sizeof(jobs));
    return ui_text_wrap(NULL, gfx_rect_make(RX + 16, 0, RW - 32, CARD_P),
                        CARD_P, jobs, WF_FONT_LABEL, UI_TEXT);
}

static const char *why_text(void)
{
    switch (s.v.off) {
    case WIFI_WHY_ARMED:   return TR(WF_OFF_ARMED);
    case WIFI_WHY_SUPPLY:  return TR(WF_OFF_SUPPLY);
    case WIFI_WHY_RUN:     return TR(WF_OFF_RUN);
    case WIFI_WHY_STOP:    return TR(WF_OFF_STOP);
    case WIFI_WHY_IDLE:    return TR(WF_OFF_IDLE);
    case WIFI_WHY_GUESSES: return TR(WF_OFF_GUESSES);
    case WIFI_WHY_FAILED:  return TR(WF_OFF_FAILED);
    case WIFI_WHY_JOIN:
        switch (s.v.join) {
        case WIFI_JOIN_PASSWORD:   return TR(WF_JOIN_PASSWORD);
        case WIFI_JOIN_NO_ADDRESS: return TR(WF_JOIN_NO_ADDRESS);
        case WIFI_JOIN_LOST:       return TR(WF_JOIN_LOST);
        case WIFI_JOIN_NOT_FOUND:
        case WIFI_JOIN_OK:
        default:                   return TR(WF_JOIN_NOT_FOUND);
        }
    case WIFI_WHY_NONE:
    case WIFI_WHY_ASKED:
    case WIFI_WHY_INSTALL:
    default:
        return NULL;
    }
}

/**
 * @brief A button in WF_FONT_LABEL.
 * @param c       Canvas.
 * @param r       Where.
 * @param label   Its text.
 * @param fill    Its fill.
 * @param hit     The hit it answers, drawn pressed while held.
 * @param enabled Whether it takes a press.
 */
static void button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                   gfx_color_t fill, int hit, bool enabled)
{
    ui_button_font(c, r, label, fill, s.have_press && s.hit == hit, enabled,
                   WF_FONT_LABEL);
}

/**
 * @brief The radio-off view: the buttons and the card.
 * @param c Canvas.
 */
static void draw_main(gfx_canvas_t *c)
{
    const bool free_ = (s.v.block == WIFI_WHY_NONE);
    const char *labels[5] = { TR(WF_AP), TR(WF_JOIN), TR(WF_CHOOSE),
                              TR(WF_FORGET),
                              s.v.code_on ? TR(WF_CODE_ON) : TR(WF_CODE_OFF) };
    const bool here = (s.v.block != WIFI_WHY_UNAVAILABLE);
    const bool on[5] = { free_, free_ && s.v.have_net, free_,
                         here && s.v.have_net, here };
    const int hits[5] = { HIT_AP, HIT_JOIN, HIT_CHOOSE, HIT_FORGET, HIT_CODE };
    for (int i = 0; i < 5; ++i) {
        button(c, btn(i), labels[i], (i == 0) ? UI_ACCENT : UI_PANEL_HI,
               hits[i], on[i]);
    }

    ui_card(c, gfx_rect_make(RX, RY, RW, RH), UI_PANEL);
    int y = RY + 16;
    {
        char jobs[128];

        jobs_text(jobs, sizeof(jobs));
        y = para(c, y, jobs, UI_TEXT) + 16;
    }
    if (here) {
        /* Without Wi-Fi set up the kept network was never read. */
        char line[80];
        if (s.v.have_net) {
            snprintf(line, sizeof(line), TR(WF_HOME_IS), s.v.net);
        } else {
            snprintf(line, sizeof(line), "%s", TR(WF_HOME_NONE));
        }
        y = para(c, y, line, UI_TEXT_DIM) + 12;
    }
    if (!here) {
        y = para(c, y, TR(WF_UNAVAIL_1), UI_WARN);
        y = para(c, y, TR(WF_UNAVAIL_2), UI_WARN) + 12;
    } else if (!free_) {
        y = para(c, y, TR(WF_NEEDS_1), UI_WARN);
        y = para(c, y, TR(WF_NEEDS_2), UI_WARN) + 12;
    }
    if (s.v.save_failed) {
        y = para(c, y, TR(WF_SAVE_FAILED_1), UI_WARN);
        y = para(c, y, TR(WF_SAVE_FAILED_2), UI_WARN) + 12;
    }
    const char *why = why_text();
    if (why != NULL) {
        y = para(c, y, why, UI_WARN);
        if (s.v.off == WIFI_WHY_JOIN) {
            y = para(c, y, TR(WF_TRY_AP), UI_TEXT_DIM);
        }
        y += 12;
    }
    if (!s.v.code_on) {
        y = para(c, y, TR(WF_OPEN_1), UI_WARN);
        (void)para(c, y, TR(WF_OPEN_2), UI_WARN);
    }
}

/**
 * @brief Row @p i of the running view: its label and its value.
 * @param c     Canvas.
 * @param i     The row, 0 at the top.
 * @param label The label, WF_FONT_LABEL.
 * @param value The value, WF_FONT_HEAD.
 */
static void row(gfx_canvas_t *c, int i, const char *label, const char *value)
{
    const int y = ROW_Y + i * ROW_P;
    text_in(c, LBL_X, y, LBL_W, ROW_P, label, WF_FONT_LABEL, UI_TEXT_DIM);
    text_in(c, VAL_X, y, VAL_W, ROW_P, value, WF_FONT_HEAD, UI_TEXT);
}

/**
 * @brief A line under the rows of the running view, WF_FONT_BODY across
 *        the body.
 * @param c   Canvas.
 * @param y   Its top.
 * @param t   The text.
 * @param col Its colour.
 * @return The y of the next line.
 */
static int line_at(gfx_canvas_t *c, int y, const char *t, gfx_color_t col)
{
    text_in(c, LBL_X, y, W - 2 * LBL_X, LINE_P, t, WF_FONT_BODY, col);
    return y + LINE_P;
}

/**
 * @brief A held button's fill: the hold's colour while held, its flash once
 *        it completes.
 * @return The fill.
 */
static gfx_color_t hold_fill(void)
{
    if (s.hold.flash_ms > 0) {
        return ui_hold_flash(UI_OK, s.hold.flash_ms);
    }
    return ui_hold_fill(UI_PANEL_HI, UI_OK, s.hold.held_s);
}

/**
 * @brief What a browser needs to write to the card, as a line under the
 *        rows: allowed with the count of writes, the access code it needs,
 *        the hold it needs, or off.
 * @param c Canvas.
 * @param y Its top.
 * @return The y of the next line.
 */
static int writes_line(gfx_canvas_t *c, int y)
{
    char v[112];
    gfx_color_t col = UI_TEXT;
    if (s.v.writes == WIFI_WRITES_OFF) {
        snprintf(v, sizeof(v), "%s", TR(WF_WR_OFF));
        col = UI_TEXT_DIM;
    } else if (s.v.writes == WIFI_WRITES_HOLD && !s.v.writes_open) {
        snprintf(v, sizeof(v), "%s", TR(WF_WR_HOLD));
    } else if (s.v.writes == WIFI_WRITES_CODE && !s.v.code_on) {
        snprintf(v, sizeof(v), TR(WF_WR_CODE), s.v.code);
    } else {
        snprintf(v, sizeof(v), TR(WF_WR_OPEN), (unsigned)s.v.written);
    }
    return line_at(c, y, v, col);
}

/**
 * @brief The radio-on view: the rows, the lines under them, WI-FI OFF.
 * @param c Canvas.
 */
static void draw_running(gfx_canvas_t *c)
{
    char v[96];
    const bool ap = (s.v.radio == WIFI_RADIO_STARTING
                     || s.v.radio == WIFI_RADIO_AP);
    int n = 0;
    if (s.v.radio == WIFI_RADIO_STARTING) {
        text_in(c, LBL_X, ROW_Y, W - 2 * LBL_X, ROW_P, TR(WF_STARTING),
                WF_FONT_BODY, UI_TEXT);
        n = 1;
    } else if (s.v.radio == WIFI_RADIO_JOINING
               || s.v.radio == WIFI_RADIO_ADDRESS) {
        snprintf(v, sizeof(v), TR(WF_JOINING), s.v.net);
        text_in(c, LBL_X, ROW_Y, W - 2 * LBL_X, ROW_P, v, WF_FONT_BODY,
                UI_TEXT);
        if (s.v.radio == WIFI_RADIO_ADDRESS) {
            text_in(c, LBL_X, ROW_Y + ROW_P, W - 2 * LBL_X, ROW_P,
                    TR(WF_WAIT_ADDRESS), WF_FONT_BODY, UI_TEXT_DIM);
        }
        n = 2;
    } else if (ap) {
        row(c, 0, TR(WF_NETWORK), s.v.name);
        row(c, 1, TR(WF_PASSWORD), s.v.pass);
        row(c, 2, TR(WF_ADDRESS), "http://192.168.4.1/");
        n = 3;
    } else {
        row(c, 0, TR(WF_NETWORK), s.v.net);
        snprintf(v, sizeof(v), "http://%u.%u.%u.%u/", s.v.ip[0], s.v.ip[1],
                 s.v.ip[2], s.v.ip[3]);
        row(c, 1, TR(WF_ADDRESS), v);
        snprintf(v, sizeof(v), "http://%s.local/", s.v.name);
        row(c, 2, TR(WF_NAME), v);
        n = 3;
    }
    if (s.v.radio == WIFI_RADIO_AP || s.v.radio == WIFI_RADIO_JOINED) {
        row(c, n, TR(WF_CODE), s.v.code_on ? s.v.code : TR(OFF));
        int y = ROW_Y + (n + 1) * ROW_P + 4;
        if (!s.v.code_on) {
            y = line_at(c, y, TR(WF_OPEN_ALL), UI_WARN);
        }
        if (!ap) {
            y = line_at(c, y, TR(WF_PLAIN), UI_WARN);
        }
        if (ap && s.v.channel != 0u) {
            snprintf(v, sizeof(v), TR(WF_CHANNEL), (unsigned)s.v.channel);
            y = line_at(c, y, v, UI_TEXT_DIM);
        }
        snprintf(v, sizeof(v), TR(WF_IDLE),
                 (unsigned)((s.v.idle_left_s + 59u) / 60u));
        y = line_at(c, y, v, UI_TEXT_DIM);
        y = writes_line(c, y) + 8;
        if (s.v.up == WIFI_UP_RECEIVING) {
            const unsigned pct = (s.v.total > 0u)
                ? (unsigned)((uint64_t)s.v.received * 100u / s.v.total) : 0u;
            snprintf(v, sizeof(v), TR(WF_RECEIVING),
                     (unsigned long)s.v.received, (unsigned long)s.v.total,
                     pct);
            (void)line_at(c, y, v, UI_TEXT);
        } else if (s.v.up == WIFI_UP_FAILED) {
            (void)line_at(c, y, TR(WF_UP_REFUSED), UI_WARN);
        }
    }
    button(c, off_rect(), TR(WF_OFF), UI_PANEL_HI, HIT_OFF, true);
    if (writes_button() && s.v.writes_open) {
        button(c, writes_rect(), TR(WF_WRITES_STOP), UI_PANEL_HI,
               HIT_WRITES_OFF, true);
    } else if (writes_button()) {
        ui_button_font(c, writes_rect(), TR(WF_HOLD_WRITES),
                       (s.hold_hit == HIT_WRITES) ? hold_fill() : UI_PANEL_HI,
                       false, true, WF_FONT_LABEL);
    }
}

/**
 * @brief The scan: the heading or the list, and its buttons.
 * @param c Canvas.
 */
static void draw_scan(gfx_canvas_t *c)
{
    if (s.v.radio == WIFI_RADIO_SCANNING) {
        text_in(c, 16, SC_Y, W - 32, ROW_P, TR(WF_SCANNING), WF_FONT_HEAD,
                UI_TEXT);
    } else if (s.v.scan_n == 0) {
        /* A scan that ended without a list says why the radio went off:
         * a failure is not an empty list. */
        const char *why = s.v.scanned ? NULL : why_text();
        if (why != NULL) {
            text_in(c, 16, SC_Y, W - 32, ROW_P, why, WF_FONT_LABEL, UI_WARN);
        } else {
            text_in(c, 16, SC_Y, W - 32, ROW_P,
                    s.v.scanned ? TR(WF_SCAN_NONE) : TR(WF_SCANNING),
                    WF_FONT_BODY, UI_TEXT_DIM);
        }
    }
    if (s.v.radio != WIFI_RADIO_SCANNING) {
        /* The list's ground under each column that has rows. */
        for (int col = 0; col * SC_ROWS < s.v.scan_n && col < 2; ++col) {
            const int rows = (s.v.scan_n - col * SC_ROWS < SC_ROWS)
                                 ? s.v.scan_n - col * SC_ROWS : SC_ROWS;
            ui_choice_list(c, gfx_rect_make(((col == 0) ? SC_X0 : SC_X1) - 4,
                                            SC_Y - 4, SC_W + 8,
                                            (rows - 1) * SC_P + SC_H + 8));
        }
        for (int i = 0; i < s.v.scan_n && i < WIFI_SCAN_ROWS; ++i) {
            const gfx_rect_t r = row_rect(i);
            const bool pressed = s.have_press && s.hit == HIT_ROW + i;
            ui_choice_row(c, r, "", false, pressed);
            /* A name of 32 bytes takes 384px in the body face, 12px in;
             * the signal 8 cells of the label face, 12px from the right. */
            gfx_text_in(c, gfx_rect_make(r.x + 12, r.y, r.w - 108, r.h),
                        s.v.scan[i], WF_FONT_BODY, UI_TEXT, 1,
                        GFX_ALIGN_LEFT);
            char dbm[16];
            snprintf(dbm, sizeof(dbm), "%ddBm", (int)s.v.rssi[i]);
            gfx_text_in(c, gfx_rect_make(r.x + r.w - 92, r.y, 80, r.h), dbm,
                        WF_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_RIGHT);
        }
        button(c, rescan_rect(), TR(WF_RESCAN), UI_PANEL_HI, HIT_RESCAN,
               s.v.block == WIFI_WHY_NONE);
        button(c, hidden_rect(), TR(WF_HIDDEN), UI_PANEL_HI, HIT_HIDDEN,
               true);
    }
    button(c, scan_cancel_rect(), TR(CANCEL), UI_PANEL_HI, HIT_SCAN_CANCEL,
           true);
}

/**
 * @brief @p t in the question from @p y, broken into lines of its 80
 *        cells LINE_P px apart.
 * @param c   Canvas.
 * @param y   The first line's top.
 * @param t   The text.
 * @param col Its colour.
 * @return The y under its last line.
 */
static int q_para(gfx_canvas_t *c, int y, const char *t, gfx_color_t col)
{
    return y + LINE_P * ui_text_wrap(c, gfx_rect_make(Q_X + 24, y, Q_W - 48,
                                                      LINE_P),
                                     LINE_P, t, WF_FONT_BODY, col);
}

/**
 * @brief The question about a staged image.
 * @param c Canvas.
 */
static void draw_question(gfx_canvas_t *c)
{
    ui_card(c, gfx_rect_make(Q_X, Q_Y, Q_W, Q_H), UI_PANEL);
    const int x = Q_X + 24, w = Q_W - 48;
    text_in(c, x, Q_Y + 16, w, 44, TR(WF_Q_TITLE), WF_FONT_HEAD, UI_TEXT);
    char line[96];
    snprintf(line, sizeof(line), TR(WF_Q_NEW), s.v.new_ver);
    text_in(c, x, Q_Y + 72, w, 44, line, WF_FONT_HEAD, UI_TEXT);
    snprintf(line, sizeof(line), TR(WF_Q_RUN), s.v.run_ver);
    text_in(c, x, Q_Y + 120, w, 44, line, WF_FONT_HEAD, UI_TEXT_DIM);
    int y = Q_Y + 176;
    switch (s.v.offer) {
    case WIFI_OFFER_OLDER:
        snprintf(line, sizeof(line), TR(WF_Q_OLDER), s.v.new_ver,
                 s.v.run_ver);
        y = q_para(c, y, line, UI_WARN) + 8;
        break;
    case WIFI_OFFER_REBUILD:
        y = q_para(c, y, TR(WF_Q_REBUILD), UI_WARN) + 8;
        break;
    case WIFI_OFFER_INSTALLED:
        y = q_para(c, y, TR(WF_Q_INSTALLED), UI_WARN) + 8;
        break;
    case WIFI_OFFER_NEWER:
    default:
        break;
    }
    y = q_para(c, y, TR(WF_Q_NOTE_1), UI_TEXT_DIM);
    y = q_para(c, y, TR(WF_Q_NOTE_2), UI_TEXT_DIM) + 16;
    if (s.v.offer != WIFI_OFFER_INSTALLED && s.v.gate != WIFI_GATE_OPEN) {
        if (s.v.gate == WIFI_GATE_DRAWING) {
            (void)q_para(c, y, TR(WF_Q_DRAWING), UI_WARN);
        } else if (s.v.gate == WIFI_GATE_COPROC) {
            (void)q_para(c, y, TR(WF_Q_COPROC), UI_WARN);
        } else {
            y = q_para(c, y, TR(WF_NEEDS_1), UI_WARN);
            (void)q_para(c, y, TR(WF_NEEDS_2), UI_WARN);
        }
    }
    button(c, q_cancel_rect(), TR(CANCEL), UI_PANEL_HI, HIT_Q_CANCEL, true);
    ui_button_font(c, install_rect(), TR(WF_HOLD_INSTALL),
                   (s.hold_hit == HIT_INSTALL) ? hold_fill() : UI_PANEL_HI,
                   false, install_allowed(), WF_FONT_LABEL);
}

/**
 * @brief The view showing, the tile's strip over the first level's two.
 * @param c            Canvas.
 * @param buffer_index The framebuffer, whose cache it follows.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const bool keys = (s.sub == SUB_KEY_SSID || s.sub == SUB_KEY_PASS)
                      && !staged();
    if ((s.drawn_mask & bit) != 0u) {
        return;            /* every event and every change clears the mask */
    }
    if (keys) {
        /* The keyboard fills its area: only the margin around it is
         * cleared, which saves a whole-screen fill on every key. */
        const gfx_rect_t k = s.tk.area;
        gfx_fill_rect(c, 0, 0, c->width, k.y, UI_BG);
        gfx_fill_rect(c, 0, k.y + k.h, c->width, c->height - k.y - k.h,
                      UI_BG);
        gfx_fill_rect(c, 0, k.y, k.x, k.h, UI_BG);
        gfx_fill_rect(c, k.x + k.w, k.y, c->width - k.x - k.w, k.h, UI_BG);
        ui_textkey_render(&s.tk, c);
    } else {
        gfx_clear(c, UI_BG);
        switch (view_of()) {
        case VIEW_QUESTION: draw_question(c); break;
        case VIEW_SCAN:     draw_scan(c);     break;
        case VIEW_RUNNING:
            logs_strip_render(c, SCREEN_WIFI);
            draw_running(c);
            break;
        case VIEW_MAIN:
        default:
            logs_strip_render(c, SCREEN_WIFI);
            draw_main(c);
            break;
        }
    }
    if (s.hold.flash_ms > 0) {
        s.drawn_mask = 0u;
    } else {
        s.drawn_mask |= bit;
    }
}

static const ui_screen_t k_screen = {
    .title  = "WI-FI",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
    .back   = back,
};

const ui_screen_t *wifi_screen(void) { return &k_screen; }
