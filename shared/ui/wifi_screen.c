/*
 * WI-FI: wifi_screen.h says what the screen is for.  PANEL UPDATE, the
 * screen the same header declares, is update_screen.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* The area below the band: 1024 x 540 px. */
#define W UI_PANEL_W
#define H UI_BODY_H

/* With the radio off: the buttons on the left, what is kept and what last
 * happened on the right. */
#define BX      16
#define BW      360
#define BH      56
#define BPITCH  68
#define RX      400
#define RW      384

/* With the radio on: a label column and a value column. */
#define LBL_X   16
#define LBL_W   184
#define VAL_X   208
#define VAL_W   576
#define ROW_Y   20
#define ROW_P   46
#define OFF_Y   360
#define OFF_W   240

/* The scan list: two columns of eight rows. */
#define SC_X0   16
#define SC_X1   408
#define SC_W    376
#define SC_H    40
#define SC_P    44
#define SC_ROWS 8
#define SC_BY   376
#define SC_BH   48

/* The question. */
#define Q_X     16
#define Q_Y     12
#define Q_W     768
#define Q_H     408
#define Q_BY    (Q_Y + Q_H - 80)
#define Q_BH    64

/* The keyboard, over the whole area. */
#define KEY_AREA gfx_rect_make(8, 8, 784, 416)

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
    bool         installing;    /* INSTALL went: leaving keeps the image */
    bool         q_cancelled;   /* the home tag answered the question */
    unsigned     drawn_mask;
} s;

static void touched(void) { s.drawn_mask = 0u; }

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

/* A press on the hold that the model took away under it ends here. */
static void drop_hold_if_gone(void)
{
    if (!install_allowed() && ui_hold_left(&s.hold)
        && s.hit == HIT_INSTALL) {
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

static gfx_rect_t btn(int row)
{
    return gfx_rect_make(BX, 16 + row * BPITCH, BW, BH);
}

static gfx_rect_t off_rect(void) { return gfx_rect_make(BX, OFF_Y, OFF_W, BH); }

static gfx_rect_t q_cancel_rect(void)
{
    return gfx_rect_make(Q_X + 16, Q_BY, 240, Q_BH);
}

static gfx_rect_t install_rect(void)
{
    return gfx_rect_make(Q_X + Q_W - 16 - 360, Q_BY, 360, Q_BH);
}

static gfx_rect_t row_rect(int i)
{
    return gfx_rect_make((i < SC_ROWS) ? SC_X0 : SC_X1,
                         16 + (i % SC_ROWS) * SC_P, SC_W, SC_H);
}

static gfx_rect_t rescan_rect(void) { return gfx_rect_make(16, SC_BY, 240, SC_BH); }
static gfx_rect_t hidden_rect(void) { return gfx_rect_make(272, SC_BY, 256, SC_BH); }
static gfx_rect_t scan_cancel_rect(void)
{
    return gfx_rect_make(544, SC_BY, 240, SC_BH);
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
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.have_press) {
            return;                /* one press at a time */
        }
        s.hit = hit_at(x, y);
        if (s.hit != HIT_NONE) {
            s.have_press = true;
            s.press_id = evt->point.id;
            if (s.hit == HIT_INSTALL) {
                ui_hold_begin(&s.hold);
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
        if (s.hit == HIT_INSTALL && !in(install_rect(), x, y)
            && ui_hold_leave(&s.hold)) {
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
    if (hit == HIT_INSTALL) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&s.hold);
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
    if (install_allowed() && ui_hold_tick(&s.hold, dt_s)) {
        s.req |= WIFI_REQ_INSTALL;
        s.installing = true;
        ui_hold_reached(&s.hold);
    }
}

static void cancel(void)
{
    if (s.hit == HIT_INSTALL) {
        (void)ui_hold_left(&s.hold);
    }
    s.hit = HIT_NONE;
    s.have_press = false;
    ui_textkey_cancel_press(&s.tk);
    touched();
}

static void enter(void)
{
    s.sub = SUB_MAIN;
    s.installing = false;
    s.q_cancelled = false;
    ui_hold_reset(&s.hold);
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
    ui_hold_reset(&s.hold);
}

/* ------------------------------------------------------------ drawing */

static void text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                    const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, (f == UI_FONT_HEAD) ? 30 : 18), t, f,
                col, 1, GFX_ALIGN_LEFT);
}

/* @p t in the card from @p y, broken into lines of its 44 cells 20 px
 * apart; returns the y under its last line. */
static int para(gfx_canvas_t *c, int y, const char *t, gfx_color_t col)
{
    return y + 20 * ui_text_wrap(c, gfx_rect_make(RX + 16, y, RW - 32, 18),
                                 20, t, UI_FONT_LABEL, col);
}

/* What the page is for: one sentence, given in two parts, wrapped once. */
static void jobs_text(char *out, size_t n)
{
    snprintf(out, n, "%s %s", TR(WF_JOBS_1), TR(WF_JOBS_2));
}

int wifi_screen_jobs_lines(void)
{
    char jobs[128];

    jobs_text(jobs, sizeof(jobs));
    return ui_text_wrap(NULL, gfx_rect_make(RX + 16, 0, RW - 32, 18), 20,
                        jobs, UI_FONT_LABEL, UI_TEXT);
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
        ui_button(c, btn(i), labels[i],
                  (i == 0) ? UI_ACCENT : UI_PANEL_HI,
                  s.have_press && s.hit == hits[i], on[i]);
    }

    ui_card(c, gfx_rect_make(RX, 16, RW, 400), UI_PANEL);
    int y = 32;
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

static void row(gfx_canvas_t *c, int i, const char *label, const char *value)
{
    const int y = ROW_Y + i * ROW_P;
    text_in(c, LBL_X, y + 6, LBL_W, label, UI_FONT_LABEL, UI_TEXT_DIM);
    text_in(c, VAL_X, y, VAL_W, value, UI_FONT_HEAD, UI_TEXT);
}

static void draw_running(gfx_canvas_t *c)
{
    char v[96];
    const bool ap = (s.v.radio == WIFI_RADIO_STARTING
                     || s.v.radio == WIFI_RADIO_AP);
    int n = 0;
    if (s.v.radio == WIFI_RADIO_STARTING) {
        text_in(c, LBL_X, ROW_Y, W - 32, TR(WF_STARTING), UI_FONT_HEAD,
                UI_TEXT);
        n = 1;
    } else if (s.v.radio == WIFI_RADIO_JOINING
               || s.v.radio == WIFI_RADIO_ADDRESS) {
        snprintf(v, sizeof(v), TR(WF_JOINING), s.v.net);
        text_in(c, LBL_X, ROW_Y, W - 32, v, UI_FONT_HEAD, UI_TEXT);
        if (s.v.radio == WIFI_RADIO_ADDRESS) {
            text_in(c, LBL_X, ROW_Y + ROW_P, W - 32, TR(WF_WAIT_ADDRESS),
                    UI_FONT_LABEL, UI_TEXT_DIM);
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
            text_in(c, LBL_X, y, W - 32, TR(WF_OPEN_ALL), UI_FONT_LABEL,
                    UI_WARN);
            y += 22;
        }
        if (!ap) {
            text_in(c, LBL_X, y, W - 32, TR(WF_PLAIN), UI_FONT_LABEL, UI_WARN);
            y += 22;
        }
        if (ap && s.v.channel != 0u) {
            snprintf(v, sizeof(v), TR(WF_CHANNEL), (unsigned)s.v.channel);
            text_in(c, LBL_X, y, W - 32, v, UI_FONT_LABEL, UI_TEXT_DIM);
            y += 22;
        }
        snprintf(v, sizeof(v), TR(WF_IDLE),
                 (unsigned)((s.v.idle_left_s + 59u) / 60u));
        text_in(c, LBL_X, y, W - 32, v, UI_FONT_LABEL, UI_TEXT_DIM);
        y += 30;
        if (s.v.up == WIFI_UP_RECEIVING) {
            const unsigned pct = (s.v.total > 0u)
                ? (unsigned)((uint64_t)s.v.received * 100u / s.v.total) : 0u;
            snprintf(v, sizeof(v), TR(WF_RECEIVING),
                     (unsigned long)s.v.received, (unsigned long)s.v.total,
                     pct);
            text_in(c, LBL_X, y, W - 32, v, UI_FONT_LABEL, UI_TEXT);
        } else if (s.v.up == WIFI_UP_FAILED) {
            text_in(c, LBL_X, y, W - 32, TR(WF_UP_REFUSED), UI_FONT_LABEL,
                    UI_WARN);
        }
    }
    ui_button(c, off_rect(), TR(WF_OFF), UI_PANEL_HI,
              s.have_press && s.hit == HIT_OFF, true);
}

static void draw_scan(gfx_canvas_t *c)
{
    if (s.v.radio == WIFI_RADIO_SCANNING) {
        text_in(c, 16, 24, W - 32, TR(WF_SCANNING), UI_FONT_HEAD, UI_TEXT);
    } else if (s.v.scan_n == 0) {
        text_in(c, 16, 24, W - 32, s.v.scanned ? TR(WF_SCAN_NONE)
                                               : TR(WF_SCANNING),
                UI_FONT_LABEL, UI_TEXT_DIM);
    }
    if (s.v.radio != WIFI_RADIO_SCANNING) {
        /* The list's ground under each column that has rows. */
        for (int col = 0; col * SC_ROWS < s.v.scan_n && col < 2; ++col) {
            const int rows = (s.v.scan_n - col * SC_ROWS < SC_ROWS)
                                 ? s.v.scan_n - col * SC_ROWS : SC_ROWS;
            ui_choice_list(c, gfx_rect_make((col == 0) ? SC_X0 - 4
                                                       : SC_X1 - 4,
                                            12, SC_W + 8,
                                            rows * SC_P + 4));
        }
        for (int i = 0; i < s.v.scan_n && i < WIFI_SCAN_ROWS; ++i) {
            const gfx_rect_t r = row_rect(i);
            const bool pressed = s.have_press && s.hit == HIT_ROW + i;
            ui_choice_row(c, r, "", false, pressed);
            /* A name of 32 bytes takes 256 px in the label face. */
            gfx_text_in(c, gfx_rect_make(r.x + 12, r.y, r.w - 112, r.h),
                        s.v.scan[i], UI_FONT_LABEL, UI_TEXT, 1,
                        GFX_ALIGN_LEFT);
            char dbm[16];
            snprintf(dbm, sizeof(dbm), "%d dBm", (int)s.v.rssi[i]);
            gfx_text_in(c, gfx_rect_make(r.x + r.w - 92, r.y, 84, r.h), dbm,
                        UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_RIGHT);
        }
        ui_button(c, rescan_rect(), TR(WF_RESCAN), UI_PANEL_HI,
                  s.have_press && s.hit == HIT_RESCAN,
                  s.v.block == WIFI_WHY_NONE);
        ui_button(c, hidden_rect(), TR(WF_HIDDEN), UI_PANEL_HI,
                  s.have_press && s.hit == HIT_HIDDEN, true);
    }
    ui_button(c, scan_cancel_rect(), TR(CANCEL), UI_PANEL_HI,
              s.have_press && s.hit == HIT_SCAN_CANCEL, true);
}

static gfx_color_t install_fill(void)
{
    if (s.hold.flash_ms > 0) {
        return ui_hold_flash(UI_OK, s.hold.flash_ms);
    }
    return ui_hold_fill(UI_PANEL_HI, UI_OK, s.hold.held_s);
}

static void draw_question(gfx_canvas_t *c)
{
    ui_card(c, gfx_rect_make(Q_X, Q_Y, Q_W, Q_H), UI_PANEL);
    const int x = Q_X + 24, w = Q_W - 48;
    text_in(c, x, Q_Y + 20, w, TR(WF_Q_TITLE), UI_FONT_HEAD, UI_TEXT);
    char line[96];
    snprintf(line, sizeof(line), TR(WF_Q_NEW), s.v.new_ver);
    text_in(c, x, Q_Y + 70, w, line, UI_FONT_HEAD, UI_TEXT);
    snprintf(line, sizeof(line), TR(WF_Q_RUN), s.v.run_ver);
    text_in(c, x, Q_Y + 110, w, line, UI_FONT_HEAD, UI_TEXT_DIM);
    int y = Q_Y + 160;
    switch (s.v.offer) {
    case WIFI_OFFER_OLDER:
        snprintf(line, sizeof(line), TR(WF_Q_OLDER), s.v.new_ver,
                 s.v.run_ver);
        text_in(c, x, y, w, line, UI_FONT_LABEL, UI_WARN);
        y += 24;
        break;
    case WIFI_OFFER_REBUILD:
        text_in(c, x, y, w, TR(WF_Q_REBUILD), UI_FONT_LABEL, UI_WARN);
        y += 24;
        break;
    case WIFI_OFFER_INSTALLED:
        text_in(c, x, y, w, TR(WF_Q_INSTALLED), UI_FONT_LABEL, UI_WARN);
        y += 24;
        break;
    case WIFI_OFFER_NEWER:
    default:
        break;
    }
    text_in(c, x, y, w, TR(WF_Q_NOTE_1), UI_FONT_LABEL, UI_TEXT_DIM);
    text_in(c, x, y + 20, w, TR(WF_Q_NOTE_2), UI_FONT_LABEL, UI_TEXT_DIM);
    y += 52;
    if (s.v.offer != WIFI_OFFER_INSTALLED && s.v.gate != WIFI_GATE_OPEN) {
        if (s.v.gate == WIFI_GATE_DRAWING) {
            text_in(c, x, y, w, TR(WF_Q_DRAWING), UI_FONT_LABEL, UI_WARN);
        } else if (s.v.gate == WIFI_GATE_COPROC) {
            text_in(c, x, y, w, TR(WF_Q_COPROC), UI_FONT_LABEL, UI_WARN);
        } else {
            text_in(c, x, y, w, TR(WF_NEEDS_1), UI_FONT_LABEL, UI_WARN);
            text_in(c, x, y + 20, w, TR(WF_NEEDS_2), UI_FONT_LABEL, UI_WARN);
        }
    }
    ui_button(c, q_cancel_rect(), TR(CANCEL), UI_PANEL_HI,
              s.have_press && s.hit == HIT_Q_CANCEL, true);
    ui_button(c, install_rect(), TR(WF_HOLD_INSTALL), install_fill(), false,
              install_allowed());
}

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
        case VIEW_RUNNING:  draw_running(c);  break;
        case VIEW_MAIN:
        default:            draw_main(c);     break;
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
