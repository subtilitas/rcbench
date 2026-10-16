/*
 * upd_screen.h says what these two screens are for.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_screen.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* The area below the band: 800 x 432 px.  UPDATES lays out as FILES &
 * UPDATE does: the buttons on the left, what is kept and what last happened
 * on the right, each text broken into lines of the card's 44 cells, 20 px
 * apart. */
#define BX      16
#define BW      360
#define BH      56
#define BPITCH  68
#define RX      400
#define RW      384

/* The reminder: a box with the title in its header, two taps side by side,
 * the hold under them. */
#define R_BOX     gfx_rect_make(80, 24, 640, 384)
#define R_IGNORE  gfx_rect_make(104, 248, 280, 56)
#define R_NEVER   gfx_rect_make(416, 248, 280, 56)
#define R_HOLD    gfx_rect_make(104, 320, 592, 64)

enum {
    HIT_NONE = 0,
    HIT_CHECK,          /* CHECK FOR UPDATES, or CANCEL while one runs */
    HIT_REMIND,
    HIT_DOWNLOAD,
    HIT_IGNORE,
    HIT_NEVER,
};

/* What both screens show, and what they ask for. */
static upd_view_t s_v;
static unsigned   s_req;

/* UPDATES. */
static struct {
    int       hit;
    uint8_t   press_id;
    bool      have_press;
    ui_hold_t hold;
    unsigned  drawn_mask;
} s;

/* The reminder. */
static struct {
    int       hit;
    uint8_t   press_id;
    bool      have_press;
    ui_hold_t hold;
    bool      answered;   /* IGNORE NOW, NEVER REMIND ME or the hold      */
    bool      yield;      /* the driver takes it down: not an answer      */
    unsigned  drawn_mask;
} s_rem;

static void touched(void)
{
    s.drawn_mask = 0u;
    s_rem.drawn_mask = 0u;
}

void upd_screen_invalidate(void) { touched(); }

/* ------------------------------------------------------------- the model */

bool upd_busy(const upd_view_t *v)
{
    return v != NULL && v->phase != UPD_PH_IDLE && v->phase != UPD_PH_ENDED;
}

/* The last check found a newer release, and nothing runs. */
static bool have_newer(void)
{
    return s_v.job == UPD_JOB_CHECK && s_v.phase == UPD_PH_ENDED
           && s_v.got == UPD_GOT_NEWER && !s_v.unavailable;
}

static bool download_allowed(void)
{
    return have_newer() && s_v.dl_block == UPD_WHY_NONE && s_v.have_net;
}

static bool check_allowed(void)
{
    return !s_v.unavailable && s_v.have_net && s_v.block == UPD_WHY_NONE
           && !upd_busy(&s_v);
}

/* A busy CHECK is cancelled from the same button. */
static bool cancel_allowed(void)
{
    return upd_busy(&s_v) && s_v.job == UPD_JOB_CHECK;
}

/* A press on a hold the model took away under it ends here. */
static void drop_holds_if_gone(void)
{
    if (!download_allowed()) {
        if (s.have_press && s.hit == HIT_DOWNLOAD) {
            (void)ui_hold_left(&s.hold);
            s.hit = HIT_NONE;
            s.have_press = false;
        }
        if (s_rem.have_press && s_rem.hit == HIT_DOWNLOAD) {
            (void)ui_hold_left(&s_rem.hold);
            s_rem.hit = HIT_NONE;
            s_rem.have_press = false;
        }
    }
}

void upd_screen_set(const upd_view_t *v)
{
    /* As wifi_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &s_v, sizeof(s_v)) == 0) {
        return;
    }
    s_v = *v;
    drop_holds_if_gone();
    touched();
}

const upd_view_t *upd_screen_view(void) { return &s_v; }

unsigned upd_screen_take(void)
{
    const unsigned q = s_req;
    s_req = 0u;
    return q;
}

/* ------------------------------------------------------------ the texts */

static const char *rel_text(const upd_view_t *v)
{
    switch (v->rel) {
    case UPD_REL_MISSING:    return TR(UPD_REL_MISSING);
    case UPD_REL_LONG:       return TR(UPD_REL_LONG);
    case UPD_REL_CHAR:       return TR(UPD_REL_CHAR);
    case UPD_REL_PLACE:      return TR(UPD_REL_PLACE);
    case UPD_REL_VERSION:
        /* A download's VERSION is the image naming another release than
         * the tag it came from; a check's is the tag's text. */
        return (v->job == UPD_JOB_DOWNLOAD) ? TR(UPD_REL_IMG_VERSION)
                                             : TR(UPD_REL_VERSION);
    case UPD_REL_HOPS:       return TR(UPD_REL_HOPS);
    case UPD_REL_HOST:       return TR(UPD_REL_HOST);
    case UPD_REL_NO_LENGTH:  return TR(UPD_REL_NO_LENGTH);
    case UPD_REL_EMPTY:      return TR(UPD_REL_EMPTY);
    case UPD_REL_TOO_BIG:    return TR(UPD_REL_TOO_BIG);
    case UPD_REL_SHORT:      return TR(UPD_REL_SHORT);
    case UPD_REL_OVERRUN:    return TR(UPD_REL_OVERRUN);
    case UPD_REL_SUMS_BAD:   return TR(UPD_REL_SUMS_BAD);
    case UPD_REL_SUMS_NONE:  return TR(UPD_REL_SUMS_NONE);
    case UPD_REL_SUMS_TWICE: return TR(UPD_REL_SUMS_TWICE);
    case UPD_REL_OK:
    case UPD_REL_STATUS:
    default:                 return "";
    }
}

static const char *join_text(const upd_view_t *v)
{
    switch (v->join) {
    case WIFI_JOIN_PASSWORD:   return TR(WF_JOIN_PASSWORD);
    case WIFI_JOIN_NO_ADDRESS: return TR(WF_JOIN_NO_ADDRESS);
    case WIFI_JOIN_LOST:       return TR(WF_JOIN_LOST);
    case WIFI_JOIN_NOT_FOUND:
    case WIFI_JOIN_OK:
    default:                   return TR(WF_JOIN_NOT_FOUND);
    }
}

/* Why a job did not start (BLOCKED): one line, or two for the gate. */
static void blocked_text(upd_why_t why, char *out, size_t n, char *second,
                         size_t n2)
{
    if (n2 > 0u) {
        second[0] = '\0';
    }
    switch (why) {
    case UPD_WHY_ARMED:
    case UPD_WHY_SUPPLY:
    case UPD_WHY_RUN:
    case UPD_WHY_STOP:
        snprintf(out, n, "%s", TR(WF_NEEDS_1));
        snprintf(second, n2, "%s", TR(WF_NEEDS_2));
        break;
    case UPD_WHY_COPROC:  snprintf(out, n, "%s", TR(UPD_WAIT_COPROC));  break;
    case UPD_WHY_DRAWING: snprintf(out, n, "%s", TR(UPD_WAIT_DRAWING)); break;
    case UPD_WHY_FILES:   snprintf(out, n, "%s", TR(UPD_STOP_FILES));   break;
    case UPD_WHY_INSTALL:
    case UPD_WHY_BUSY:
    case UPD_WHY_CANCEL:
    case UPD_WHY_NONE:
    default:              snprintf(out, n, "%s", TR(UPD_WAIT_BUSY));    break;
    }
}

/* Why a job was ended from outside (ABORTED). */
static const char *aborted_text(upd_why_t why)
{
    switch (why) {
    case UPD_WHY_ARMED:   return TR(WF_OFF_ARMED);
    case UPD_WHY_SUPPLY:  return TR(WF_OFF_SUPPLY);
    case UPD_WHY_RUN:     return TR(WF_OFF_RUN);
    case UPD_WHY_STOP:    return TR(WF_OFF_STOP);
    case UPD_WHY_FILES:   return TR(UPD_STOP_FILES);
    case UPD_WHY_COPROC:  return TR(UPD_STOP_COPROC);
    case UPD_WHY_INSTALL: return TR(UPD_STOP_INSTALL);
    case UPD_WHY_DRAWING: return TR(UPD_STOP_DRAWING);
    case UPD_WHY_BUSY:    return TR(UPD_WAIT_BUSY);
    case UPD_WHY_CANCEL:
    case UPD_WHY_NONE:
    default:              return TR(UPD_STOP_CANCEL);
    }
}

static const char *phase_text(upd_phase_t ph)
{
    switch (ph) {
    case UPD_PH_ASKING:   return TR(UPD_ASKING);
    case UPD_PH_SUMS:     return TR(UPD_DL_SUMS);
    case UPD_PH_IMAGE:    return TR(UPD_DL_IMAGE);
    case UPD_PH_CHECKING: return TR(UPD_DL_CHECKING);
    case UPD_PH_JOINING:
    case UPD_PH_IDLE:
    case UPD_PH_ENDED:
    default:              return "";
    }
}

void upd_result_text(const upd_view_t *v, char *out, size_t n, char *second,
                     size_t n2)
{
    if (out == NULL || n == 0u || second == NULL || n2 == 0u) {
        return;
    }
    out[0] = '\0';
    second[0] = '\0';
    if (v == NULL) {
        return;
    }
    if (upd_busy(v)) {
        if (v->phase == UPD_PH_JOINING) {
            snprintf(out, n, TR(WF_JOINING), v->net);
        } else {
            snprintf(out, n, "%s", phase_text(v->phase));
        }
        snprintf(second, n2, TR(UPD_LEFT), (unsigned)v->left_s);
        return;
    }
    if (v->phase != UPD_PH_ENDED) {
        return;
    }
    switch (v->got) {
    case UPD_GOT_NEWER:
        snprintf(out, n, TR(UPD_NEWER), v->found);
        break;
    case UPD_GOT_CURRENT:
        snprintf(out, n, TR(UPD_CURRENT), v->running);
        snprintf(second, n2, TR(UPD_LATEST), v->found);
        break;
    case UPD_GOT_STAGED:
        snprintf(out, n, "%s", TR(UPD_STAGED));
        break;
    case UPD_GOT_NO_NET:
        snprintf(out, n, "%s", TR(WF_HOME_NONE));
        snprintf(second, n2, "%s", TR(UPD_KEEP_NET));
        break;
    case UPD_GOT_BLOCKED:
        blocked_text(v->why, out, n, second, n2);
        break;
    case UPD_GOT_ABORTED:
        snprintf(out, n, "%s", aborted_text(v->why));
        break;
    case UPD_GOT_JOIN:
        snprintf(out, n, "%s", join_text(v));
        break;
    case UPD_GOT_RADIO:
        snprintf(out, n, "%s", TR(WF_OFF_FAILED));
        break;
    case UPD_GOT_TIMEOUT:
        snprintf(out, n, TR(UPD_FAIL_TIMEOUT), (unsigned)v->budget_s);
        break;
    case UPD_GOT_CONNECT:
        snprintf(out, n, "%s", TR(UPD_FAIL_CONNECT));
        break;
    case UPD_GOT_REFUSED:
        snprintf(out, n, TR(UPD_FAIL_REFUSED), v->status);
        break;
    case UPD_GOT_UNEXPECTED:
        snprintf(out, n, "%s", TR(UPD_FAIL_UNEXPECTED));
        snprintf(second, n2, "%s", rel_text(v));
        break;
    case UPD_GOT_SUMS:
        snprintf(out, n, "%s", TR(UPD_FAIL_SUMS));
        if (v->rel == UPD_REL_STATUS) {
            snprintf(second, n2, TR(UPD_REL_STATUS), v->status);
        } else {
            snprintf(second, n2, "%s", rel_text(v));
        }
        break;
    case UPD_GOT_HASH:
        snprintf(out, n, "%s", TR(UPD_FAIL_HASH));
        break;
    case UPD_GOT_IMAGE:
        snprintf(out, n, "%s", TR(UPD_FAIL_IMAGE));
        snprintf(second, n2, "%s", rel_text(v));
        break;
    case UPD_GOT_MEMORY:
        snprintf(out, n, "%s", TR(UPD_FAIL_MEMORY));
        break;
    case UPD_GOT_NONE:
    default:
        break;
    }
}

/* Whether the result line is good news, bad news or neither. */
static gfx_color_t result_colour(void)
{
    switch (s_v.got) {
    case UPD_GOT_NEWER:   return UI_OK;
    case UPD_GOT_CURRENT:
    case UPD_GOT_STAGED:
    case UPD_GOT_NONE:    return UI_TEXT;
    case UPD_GOT_ABORTED:
        return (s_v.why == UPD_WHY_CANCEL) ? UI_TEXT : UI_WARN;
    default:              return UI_WARN;
    }
}

static void text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                    const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, (f == UI_FONT_HEAD) ? 30 : 18), t, f,
                col, 1, GFX_ALIGN_LEFT);
}

/* @p t in the card from @p y, as many lines as it takes; returns the y
 * under its last line. */
static int para(gfx_canvas_t *c, int x, int y, int w, const char *t,
                gfx_color_t col)
{
    return y + 20 * ui_text_wrap(c, gfx_rect_make(x, y, w, 18), 20, t,
                                 UI_FONT_LABEL, col);
}

static gfx_color_t hold_fill(const ui_hold_t *h)
{
    if (h->flash_left > 0) {
        return ui_hold_flash(UI_OK, h->flash_left);
    }
    return ui_hold_fill(UI_PANEL_HI, UI_OK, h->held_s);
}

/* ============================================================== UPDATES */

static gfx_rect_t btn(int row)
{
    return gfx_rect_make(BX, 16 + row * BPITCH, BW, BH);
}

static int hit_at(int x, int y)
{
    if (gfx_rect_contains(btn(0), x, y)
        && (check_allowed() || cancel_allowed())) {
        return HIT_CHECK;
    }
    if (gfx_rect_contains(btn(1), x, y)) {
        return HIT_REMIND;
    }
    if (download_allowed() && gfx_rect_contains(btn(2), x, y)) {
        return HIT_DOWNLOAD;
    }
    return HIT_NONE;
}

static void act(int hit)
{
    switch (hit) {
    case HIT_CHECK:
        s_req |= cancel_allowed() ? UPD_REQ_CANCEL : UPD_REQ_CHECK;
        break;
    case HIT_REMIND:
        /* The setting is the screen's own, as SETUP's are: set here, and
         * kept when the application finds the bench idle. */
        settings_set(SET_UPD_REMIND,
                     settings_get_bool(SET_UPD_REMIND) ? 0.0f : 1.0f);
        settings_request_save();
        break;
    default:
        break;
    }
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    ui_hold_reset(&s.hold);
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
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
            if (s.hit == HIT_DOWNLOAD) {
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
        if (s.hit == HIT_DOWNLOAD && !gfx_rect_contains(btn(2), x, y)
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
    if (hit == HIT_DOWNLOAD) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&s.hold);
        return;
    }
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

static void tick(float dt_s)
{
    drop_holds_if_gone();
    if (s.hold.down || s.hold.flash_left > 0) {
        touched();             /* the fill follows the hold */
    }
    if (download_allowed() && ui_hold_tick(&s.hold, dt_s)) {
        s_req |= UPD_REQ_DOWNLOAD;
        ui_hold_reached(&s.hold);
    }
}

static void cancel(void)
{
    if (s.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&s.hold);
    }
    s.hit = HIT_NONE;
    s.have_press = false;
    touched();
}

static void enter(void)
{
    ui_hold_reset(&s.hold);
    touched();
}

static void leave(void)
{
    s.hit = HIT_NONE;
    s.have_press = false;
    ui_hold_reset(&s.hold);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((s.drawn_mask & bit) != 0u) {
        return;            /* every event and every change clears the mask */
    }
    gfx_clear(c, UI_BG);

    /* The buttons. */
    const bool busy = cancel_allowed();
    ui_button(c, btn(0), busy ? TR(CANCEL) : TR(UPD_CHECK),
              busy ? UI_PANEL_HI : UI_ACCENT,
              s.have_press && s.hit == HIT_CHECK, busy || check_allowed());
    const bool on = settings_get_bool(SET_UPD_REMIND);
    ui_button(c, btn(1), on ? TR(UPD_REMIND_ON) : TR(UPD_REMIND_OFF),
              UI_PANEL_HI, s.have_press && s.hit == HIT_REMIND, true);
    if (have_newer()) {
        ui_button(c, btn(2), TR(UPD_HOLD_DL), hold_fill(&s.hold), false,
                  download_allowed());
    }

    /* What is kept, what runs, what last happened. */
    ui_card(c, gfx_rect_make(RX, 16, RW, 400), UI_PANEL);
    const int x = RX + 16, w = RW - 32;
    char line[96], second[96];
    int y = 32;
    snprintf(line, sizeof(line), TR(WF_Q_RUN), s_v.running);
    y = para(c, x, y, w, line, UI_TEXT) + 4;
    if (s_v.unavailable) {
        y = para(c, x, y, w, TR(WF_UNAVAIL_1), UI_WARN);
        y = para(c, x, y, w, TR(WF_UNAVAIL_2), UI_WARN) + 12;
    } else if (s_v.have_net) {
        snprintf(line, sizeof(line), TR(WF_HOME_IS), s_v.net);
        y = para(c, x, y, w, line, UI_TEXT_DIM) + 12;
    } else {
        y = para(c, x, y, w, TR(WF_HOME_NONE), UI_WARN);
        y = para(c, x, y, w, TR(UPD_KEEP_NET), UI_TEXT_DIM) + 12;
    }

    /* The job under way, or how the last one ended. */
    upd_result_text(&s_v, line, sizeof(line), second, sizeof(second));
    if (line[0] != '\0') {
        const gfx_color_t col = upd_busy(&s_v) ? UI_TEXT : result_colour();
        y = para(c, x, y, w, line, col);
        y = para(c, x, y, w, second, UI_TEXT_DIM) + 12;
    }

    /* Why the buttons are grey, when they are and the result did not
     * already say so. */
    if (!s_v.unavailable && s_v.have_net && !upd_busy(&s_v)) {
        const upd_why_t why = (s_v.block != UPD_WHY_NONE)
            ? s_v.block
            : (have_newer() ? s_v.dl_block : UPD_WHY_NONE);
        if (why != UPD_WHY_NONE
            && !(s_v.got == UPD_GOT_BLOCKED && s_v.why == why)) {
            blocked_text(why, line, sizeof(line), second, sizeof(second));
            y = para(c, x, y, w, line, UI_WARN);
            (void)para(c, x, y, w, second, UI_WARN);
        }
    }

    /* What the reminder does, at the card's foot: its last line ends 18 px
     * above the card's bottom edge. */
    const int foot = ui_text_wrap(NULL, gfx_rect_make(x, 0, w, 18), 20,
                                  TR(UPD_ABOUT_1), UI_FONT_LABEL, UI_TEXT)
                     + ui_text_wrap(NULL, gfx_rect_make(x, 0, w, 18), 20,
                                    TR(UPD_ABOUT_2), UI_FONT_LABEL, UI_TEXT);
    y = para(c, x, 16 + 400 - 16 - 20 * foot, w, TR(UPD_ABOUT_1),
             UI_TEXT_DIM);
    (void)para(c, x, y, w, TR(UPD_ABOUT_2), UI_TEXT_DIM);

    if (s.hold.flash_left > 0) {
        ui_hold_flash_step(&s.hold);
        s.drawn_mask = 0u;
    } else {
        s.drawn_mask |= bit;
    }
}

static const ui_screen_t k_screen = {
    .title  = "UPDATES",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *upd_screen(void) { return &k_screen; }

/* ========================================================= the reminder */

static int r_hit_at(int x, int y)
{
    if (gfx_rect_contains(R_IGNORE, x, y)) {
        return HIT_IGNORE;
    }
    if (gfx_rect_contains(R_NEVER, x, y)) {
        return HIT_NEVER;
    }
    if (download_allowed() && gfx_rect_contains(R_HOLD, x, y)) {
        return HIT_DOWNLOAD;
    }
    return HIT_NONE;
}

static void r_reset(void)
{
    memset(&s_rem, 0, sizeof(s_rem));
    ui_hold_reset(&s_rem.hold);
}

static void r_event(const touch_event_t *evt)
{
    if (evt == NULL || s_rem.answered) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s_rem.have_press) {
            return;
        }
        s_rem.hit = r_hit_at(x, y);
        if (s_rem.hit != HIT_NONE) {
            s_rem.have_press = true;
            s_rem.press_id = evt->point.id;
            if (s_rem.hit == HIT_DOWNLOAD) {
                ui_hold_begin(&s_rem.hold);
            }
        }
        touched();
        return;
    }
    if (!s_rem.have_press || evt->point.id != s_rem.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (s_rem.hit == HIT_DOWNLOAD && !gfx_rect_contains(R_HOLD, x, y)
            && ui_hold_leave(&s_rem.hold)) {
            s_rem.hit = HIT_NONE;
            s_rem.have_press = false;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = s_rem.hit;
    s_rem.hit = HIT_NONE;
    s_rem.have_press = false;
    touched();
    if (hit == HIT_DOWNLOAD) {
        (void)ui_hold_end(&s_rem.hold);
        return;
    }
    if (!touch_event_is_tap_up(evt) || r_hit_at(x, y) != hit) {
        return;
    }
    if (hit == HIT_NEVER) {
        settings_set(SET_UPD_REMIND, 0.0f);
        settings_request_save();
        s_rem.answered = true;
    } else if (hit == HIT_IGNORE) {
        s_rem.answered = true;
    }
}

static void r_tick(float dt_s)
{
    drop_holds_if_gone();
    if (s_rem.hold.down || s_rem.hold.flash_left > 0) {
        touched();
    }
    if (!s_rem.answered && download_allowed() && ui_hold_tick(&s_rem.hold, dt_s)) {
        s_req |= UPD_REQ_DOWNLOAD;
        s_rem.answered = true;
        ui_hold_reached(&s_rem.hold);
    }
}

static void r_cancel(void)
{
    if (s_rem.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&s_rem.hold);
    }
    s_rem.hit = HIT_NONE;
    s_rem.have_press = false;
    touched();
}

static void r_enter(void)
{
    s_rem.answered = false;
    s_rem.yield = false;
    s_rem.hit = HIT_NONE;
    s_rem.have_press = false;
    ui_hold_reset(&s_rem.hold);
    touched();
}

/* Left any way but the driver's own: answered, as IGNORE NOW. */
static void r_leave(void)
{
    if (!s_rem.yield) {
        s_rem.answered = true;
    }
    s_rem.hit = HIT_NONE;
    s_rem.have_press = false;
    ui_hold_reset(&s_rem.hold);
}

static void r_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((s_rem.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    const gfx_rect_t box = R_BOX;
    ui_panel(c, box, TR(UPD_R_TITLE), UI_ACCENT);
    const int x = box.x + 24, w = box.w - 48;
    char line[112];
    snprintf(line, sizeof(line), TR(UPD_R_VERSIONS), s_v.found, s_v.running);
    text_in(c, x, box.y + 48, w, line, UI_FONT_LABEL, UI_TEXT);
    text_in(c, x, box.y + 80, w, TR(UPD_R_WHAT_1), UI_FONT_LABEL,
            UI_TEXT_DIM);
    text_in(c, x, box.y + 100, w, TR(UPD_R_WHAT_2), UI_FONT_LABEL,
            UI_TEXT_DIM);
    text_in(c, x, box.y + 128, w, TR(UPD_R_NEVER), UI_FONT_LABEL,
            UI_TEXT_DIM);
    if (!download_allowed()) {
        char second[96];
        const upd_why_t why = (s_v.dl_block != UPD_WHY_NONE)
                                  ? s_v.dl_block : UPD_WHY_BUSY;
        blocked_text(why, line, sizeof(line), second, sizeof(second));
        text_in(c, x, box.y + 160, w, line, UI_FONT_LABEL, UI_WARN);
        if (second[0] != '\0') {
            text_in(c, x, box.y + 180, w, second, UI_FONT_LABEL, UI_WARN);
        }
    }
    ui_button(c, R_IGNORE, TR(UPD_IGNORE), UI_PANEL_HI,
              s_rem.have_press && s_rem.hit == HIT_IGNORE, true);
    ui_button(c, R_NEVER, TR(UPD_NEVER), UI_PANEL_HI,
              s_rem.have_press && s_rem.hit == HIT_NEVER, true);
    ui_button(c, R_HOLD, TR(UPD_HOLD_DL), hold_fill(&s_rem.hold), false,
              download_allowed());
    if (s_rem.hold.flash_left > 0) {
        ui_hold_flash_step(&s_rem.hold);
        s_rem.drawn_mask = 0u;
    } else {
        s_rem.drawn_mask |= bit;
    }
}

static const ui_screen_t k_remind = {
    .title  = "UPDATE",
    .reset  = r_reset,
    .enter  = r_enter,
    .leave  = r_leave,
    .tick   = r_tick,
    .event  = r_event,
    .cancel = r_cancel,
    .render = r_render,
};

const ui_screen_t *upd_remind_screen(void) { return &k_remind; }

/* =========================================================== the driver */

bool upd_startup_due(upd_driver_t *d, const upd_feed_t *f)
{
    if (d == NULL || f == NULL) {
        return false;
    }
    if (!d->started) {
        d->started = true;
        d->start_ms = f->now_ms;
    }
    if (d->check_done) {
        return false;
    }
    /* A start-up check: with the reminder off, no home network or no
     * Wi-Fi at this start there is none, and none comes later. */
    if (!f->remind || !f->available || !f->have_net
        || (uint32_t)(f->now_ms - d->start_ms) >= UPD_START_WAIT_MS) {
        d->check_done = true;
        return false;
    }
    if (!f->gate_open || f->files_open || f->coproc
        || ui_router_current() == SCREEN_SPLASH) {
        return false;      /* waits for a moment the bench allows */
    }
    d->check_done = true;
    return true;
}

void upd_remind_drive(upd_driver_t *d, const upd_feed_t *f)
{
    if (d == NULL || f == NULL) {
        return;
    }
    const bool may = f->newer && f->remind && !f->unsafe && !f->coproc;
    if (ui_router_current() == SCREEN_UPD_REMIND) {
        if (s_rem.answered) {
            d->dismissed = true;
            /* The hold's download takes the panel to PANEL UPDATE: the
             * application goes there itself. */
            ui_router_goto(SCREEN_OVERVIEW);
        } else if (!may) {
            /* ARMED, the supply, the coprocessor's question: they come
             * first.  The reminder comes back once they have gone. */
            s_rem.yield = true;
            ui_router_goto(SCREEN_OVERVIEW);
            s_rem.yield = false;
        }
        return;
    }
    if (s_rem.answered) {
        d->dismissed = true;        /* left by the HOME tag or the menu */
        s_rem.answered = false;
    }
    if (!d->dismissed && may && ui_router_current() == SCREEN_OVERVIEW) {
        ui_router_goto(SCREEN_UPD_REMIND);
    }
}
