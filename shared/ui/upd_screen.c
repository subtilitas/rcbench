/**
 * @file upd_screen.c
 * @brief upd_screen.h says what these two screens are for.
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

/*
 * The faces of the type scale these screens draw in:
 * UPD_FONT_LABEL (10 x 20) for the buttons, the IOmcu section's lines and
 * the reminder's buttons; UPD_FONT_BODY (12 x 24) for the card's text and
 * the reminder's lines.
 */
/** @brief The buttons and the IOmcu section: 10 x 20. */
#define UPD_FONT_LABEL (&gfx_font_10x20)
/** @brief The card's text and the reminder's lines: 12 x 24. */
#define UPD_FONT_BODY  UI_FONT_BODY

/*
 * UPDATES, in the body below the band: 1024 x 540px:
 * The buttons on the left, 460 x 56px at a 64px pitch from y 16: CHECK,
 * the reminder, RELEASES, and the download's hold.  The card on the right,
 * x 484 to 1016, y 16 to 488: what is kept and what last happened, each
 * text broken into lines of its 500px column (41 cells of UPD_FONT_BODY),
 * 28px apart.
 */
/** @brief The buttons' left edge. */
#define BX      16
/** @brief The buttons' width, px. */
#define BW      460
/** @brief The buttons' height, px. */
#define BH      56
/** @brief The step from one button to the next, px. */
#define BPITCH  64
/** @brief The card's left edge. */
#define RX      484
/** @brief The card's top, and the first button's. */
#define RY      16
/** @brief The card's width, px. */
#define RW      532
/** @brief The card's height, px. */
#define RH      472
/** @brief The card's inner padding, px. */
#define RPAD    16
/** @brief A UPD_FONT_BODY line's pitch, px. */
#define PITCH   28
/** @brief The download's hold's row. */
#define DL_ROW  3

/*
 * The IOmcu section, in the left column under the fourth row:
 * Its heading, the two versions and up to two lines of state in
 * UPD_FONT_LABEL 24px apart, and the hold, 460 x 56px, whose foot is
 * body y 484, 12px above the alert band.
 */
/** @brief The IOmcu section's heading. */
#define CU_HEAD_Y  280
/** @brief The version the IOmcu runs. */
#define CU_RUN_Y   304
/** @brief The version this panel carries. */
#define CU_CAR_Y   328
/** @brief The first line of state. */
#define CU_LINE_Y  356
/** @brief The state's line pitch, px. */
#define CU_PITCH   24
/** @brief HOLD 2s: UPDATE IOMCU. */
#define CU_BTN     gfx_rect_make(BX, 428, BW, BH)

/*
 * The reminder:
 * A box 860 x 480px from body y 8, its lines in UPD_FONT_BODY; NOT NOW and
 * NEVER REMIND ME side by side, 398 x 56px, the hold 812 x 64px under
 * them; the box's foot is body y 488.
 */
/** @brief The reminder's box, in the body. */
#define R_BOX     gfx_rect_make(82, 8, 860, 480)
/** @brief NOT NOW. */
#define R_IGNORE  gfx_rect_make(106, 328, 398, 56)
/** @brief NEVER REMIND ME. */
#define R_NEVER   gfx_rect_make(520, 328, 398, 56)
/** @brief The reminder's hold. */
#define R_HOLD    gfx_rect_make(106, 400, 812, 64)

/** @brief The body row 4px above the alert band: every control ends over
 *         it. */
#define UPD_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)
_Static_assert(BH >= UI_TAP_MIN, "UPDATES' buttons are a finger tall");
_Static_assert(16 + DL_ROW * BPITCH + BH + 16 <= CU_HEAD_Y,
               "the IOmcu section starts under the fourth row");
_Static_assert(CU_LINE_Y + 2 * CU_PITCH + 20 < 428,
               "two lines of state end over the IOmcu's hold");
_Static_assert(428 + BH <= UPD_FLOOR, "the IOmcu's hold ends over the band");
_Static_assert(RY + RH <= UPD_FLOOR, "the card ends over the alert band");
_Static_assert(RX + RW <= UI_PANEL_W - UI_MARGIN, "the card keeps the margin");
_Static_assert(8 + 480 <= UPD_FLOOR, "the reminder ends over the band");
_Static_assert(400 + 64 + 24 <= 8 + 480, "the hold ends inside the box");

enum {
    HIT_NONE = 0,
    HIT_CHECK,          /* CHECK FOR UPDATES, or CANCEL while one runs */
    HIT_REMIND,
    HIT_CHANNEL,        /* RELEASES: STABLE | TESTING */
    HIT_DOWNLOAD,
    HIT_IGNORE,
    HIT_NEVER,
    HIT_COPROC,         /* HOLD 2s: UPDATE IOMCU */
};

/* What both screens show, and what they ask for. */
static upd_view_t s_v;
static unsigned   s_req;

/* UPDATES' IOmcu section, and its hold's latch. */
static upd_cu_view_t s_cu;
static bool          s_cu_req;

/* UPDATES. */
static struct {
    int       hit;
    uint8_t   press_id;
    bool      have_press;
    ui_hold_t hold;       /* DOWNLOAD & INSTALL */
    ui_hold_t cu_hold;    /* UPDATE IOMCU */
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

static bool coproc_allowed(void) { return s_cu.sec == CU_SEC_READY; }

/* A press on a hold the model took away under it ends here. */
static void drop_holds_if_gone(void)
{
    if (!coproc_allowed() && s.have_press && s.hit == HIT_COPROC) {
        (void)ui_hold_left(&s.cu_hold);
        s.hit = HIT_NONE;
        s.have_press = false;
    }
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

void upd_screen_set_coproc(const upd_cu_view_t *v)
{
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &s_cu, sizeof(s_cu)) == 0) {
        return;
    }
    s_cu = *v;
    drop_holds_if_gone();
    touched();
}

const upd_cu_view_t *upd_screen_coproc(void) { return &s_cu; }

bool upd_screen_take_coproc(void)
{
    const bool was = s_cu_req;
    s_cu_req = false;
    return was;
}

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
    case UPD_REL_TAG_NONE:   return TR(UPD_REL_TAG_NONE);
    case UPD_REL_TAG_LONG:   return TR(UPD_REL_TAG_LONG);
    case UPD_REL_TAG_FORM:   return TR(UPD_REL_TAG_FORM);
    case UPD_REL_SCAN_LONG:  return TR(UPD_REL_SCAN_LONG);
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
    case UPD_GOT_PAST_LINE:
        snprintf(out, n, v->got == UPD_GOT_PAST_LINE ? TR(UPD_PAST_LINE)
                 : TR(UPD_CURRENT), v->running);
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
    case UPD_GOT_LIMIT:
        snprintf(out, n, TR(UPD_FAIL_LIMIT), v->status);
        snprintf(second, n2, "%s", TR(UPD_LIMIT_2));
        break;
    case UPD_GOT_UNVERSIONED:
        snprintf(out, n, "%s", TR(UPD_UNVERSIONED));
        snprintf(second, n2, TR(UPD_LATEST), v->found);
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
    case UPD_GOT_PAST_LINE:
    case UPD_GOT_STAGED:
    case UPD_GOT_NONE:    return UI_TEXT;
    case UPD_GOT_ABORTED:
        return (s_v.why == UPD_WHY_CANCEL) ? UI_TEXT : UI_WARN;
    default:              return UI_WARN;
    }
}

/**
 * @brief One line of @p t in @p f at (@p x, @p y), @p w wide.
 * @param c   Canvas, the body.
 * @param x   Its left edge.
 * @param y   Its top.
 * @param w   Its width.
 * @param t   The text.
 * @param f   Its face.
 * @param col Its colour.
 */
static void text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                    const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, gfx_text_height(f, 1)), t, f, col,
                1, GFX_ALIGN_LEFT);
}

/**
 * @brief @p t in UPD_FONT_BODY from @p y, as many lines of @p w as it
 *        takes, PITCH apart.
 * @param c   Canvas, the body, or NULL to count the lines only.
 * @param x   The lines' left edge.
 * @param y   The first line's top.
 * @param w   The column's width.
 * @param t   The text.
 * @param col Its colour.
 * @return The y one PITCH under its last line.
 */
static int para(gfx_canvas_t *c, int x, int y, int w, const char *t,
                gfx_color_t col)
{
    return y + PITCH * ui_text_wrap(c, gfx_rect_make(x, y, w,
                                                     gfx_text_height(
                                                         UPD_FONT_BODY, 1)),
                                    PITCH, t, UPD_FONT_BODY, col);
}

static gfx_color_t hold_fill(const ui_hold_t *h)
{
    if (h->flash_ms > 0) {
        return ui_hold_flash(UI_OK, h->flash_ms);
    }
    return ui_hold_fill(UI_PANEL_HI, UI_OK, h->held_s);
}

/**
 * @brief A button of these screens: ui_button_font() in UPD_FONT_LABEL.
 * @param c       Canvas, the body.
 * @param r       The button.
 * @param label   Its label.
 * @param fill    Its fill.
 * @param pressed Whether a finger is on it.
 * @param enabled Whether it takes a press.
 */
static void button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                   gfx_color_t fill, bool pressed, bool enabled)
{
    ui_button_font(c, r, label, fill, pressed, enabled, UPD_FONT_LABEL);
}

/* ============================================================== UPDATES */

/**
 * @brief UPDATES' button in row @p row.
 * @param row 0 CHECK, 1 the reminder, 2 RELEASES, DL_ROW the download.
 * @return Its rectangle in the body.
 */
static gfx_rect_t btn(int row)
{
    return gfx_rect_make(BX, RY + row * BPITCH, BW, BH);
}

static bool testing_set(void) { return settings_get_int(SET_UPD_CHANNEL) == 1; }

static int hit_at(int x, int y)
{
    if (gfx_rect_contains(btn(0), x, y)
        && (check_allowed() || cancel_allowed())) {
        return HIT_CHECK;
    }
    if (gfx_rect_contains(btn(1), x, y)) {
        return HIT_REMIND;
    }
    if (gfx_rect_contains(btn(2), x, y)) {
        return HIT_CHANNEL;
    }
    if (download_allowed() && gfx_rect_contains(btn(DL_ROW), x, y)) {
        return HIT_DOWNLOAD;
    }
    if (coproc_allowed() && gfx_rect_contains(CU_BTN, x, y)) {
        return HIT_COPROC;
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
    case HIT_CHANNEL:
        settings_set(SET_UPD_CHANNEL, testing_set() ? 0.0f : 1.0f);
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
    ui_hold_reset(&s.cu_hold);
    memset(&s_cu, 0, sizeof(s_cu));
    s_cu_req = false;
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
            } else if (s.hit == HIT_COPROC) {
                ui_hold_begin(&s.cu_hold);
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
        if ((s.hit == HIT_DOWNLOAD && !gfx_rect_contains(btn(DL_ROW), x, y)
             && ui_hold_leave(&s.hold))
            || (s.hit == HIT_COPROC && !gfx_rect_contains(CU_BTN, x, y)
                && ui_hold_leave(&s.cu_hold))) {
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
    if (hit == HIT_COPROC) {
        (void)ui_hold_end(&s.cu_hold);
        return;
    }
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

static void tick(float dt_s)
{
    drop_holds_if_gone();
    if (s.hold.down || s.hold.flash_ms > 0 || s.cu_hold.down
        || s.cu_hold.flash_ms > 0) {
        touched();             /* the fill follows the hold */
    }
    ui_hold_flash_step(&s.hold, dt_s);
    ui_hold_flash_step(&s.cu_hold, dt_s);
    if (download_allowed() && ui_hold_tick(&s.hold, dt_s)) {
        s_req |= UPD_REQ_DOWNLOAD;
        ui_hold_reached(&s.hold);
    }
    if (coproc_allowed() && ui_hold_tick(&s.cu_hold, dt_s)) {
        /* coproc_update_drive() takes it next frame and opens the
         * progress. */
        s_cu_req = true;
        ui_hold_reached(&s.cu_hold);
    }
}

static void cancel(void)
{
    if (s.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&s.hold);
    } else if (s.hit == HIT_COPROC) {
        (void)ui_hold_left(&s.cu_hold);
    }
    s.hit = HIT_NONE;
    s.have_press = false;
    touched();
}

static void enter(void)
{
    ui_hold_reset(&s.hold);
    ui_hold_reset(&s.cu_hold);
    touched();
}

static void leave(void)
{
    s.hit = HIT_NONE;
    s.have_press = false;
    ui_hold_reset(&s.hold);
    ui_hold_reset(&s.cu_hold);
}

void upd_cu_text(const upd_cu_view_t *v, const char **line,
                 gfx_color_t *col)
{
    *col = UI_WARN;
    switch (v->sec) {
    case CU_SEC_NO_IMAGE: *line = TR(UPD_CU_NO_IMAGE); *col = UI_TEXT_DIM;
                          break;
    case CU_SEC_BUSY:     *line = TR(UPD_WAIT_COPROC);  break;
    case CU_SEC_NO_LINK:  *line = TR(UPD_CU_NO_LINK);   break;
    case CU_SEC_UNKNOWN:  *line = TR(UPD_CU_UNKNOWN); *col = UI_TEXT; break;
    case CU_SEC_CHECKING: *line = TR(UPD_CU_CHECKING); *col = UI_TEXT; break;
    case CU_SEC_FAILED:   *line = TR(UPD_CU_FAILED);    break;
    case CU_SEC_USB:      *line = TR(UPD_CU_USB);       break;
    case CU_SEC_SAME:     *line = TR(UPD_CU_SAME); *col = UI_TEXT_DIM; break;
    case CU_SEC_UNSAFE:   *line = TR(UPD_CU_UNSAFE);    break;
    case CU_SEC_RADIO:    *line = TR(UPD_CU_RADIO);     break;
    case CU_SEC_INSTALL:  *line = TR(UPD_CU_INSTALL);   break;
    case CU_SEC_READY:
    default:
        /* As the question says it: an older image in the warning colour,
         * the same number of another build plainly, else what the update
         * does to the bench. */
        if (coproc_update_older(v->running, v->carried)) {
            *line = TR(CU_Q_OLDER);
        } else if (v->running.major == v->carried.major
                   && v->running.minor == v->carried.minor
                   && v->running.patch == v->carried.patch) {
            *line = TR(CU_Q_SAME);
            *col = UI_TEXT_DIM;
        } else {
            *line = TR(CU_Q_WHAT);
            *col = UI_TEXT_DIM;
        }
        break;
    }
}

static void cu_version_text(char *out, size_t n, bool known, cu_version_t v)
{
    if (known) {
        snprintf(out, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
                 (unsigned)v.patch);
    } else {
        snprintf(out, n, "%s", TR(UPD_CU_NOT_KNOWN));
    }
}

/**
 * @brief The IOmcu section: heading, versions, state, hold.
 * @param c Canvas, the body.
 */
static void render_coproc(gfx_canvas_t *c)
{
    char ver[24], line[96];
    text_in(c, BX, CU_HEAD_Y, BW, TR(UPD_CU_HEAD), UPD_FONT_LABEL,
            UI_TEXT_DIM);
    cu_version_text(ver, sizeof(ver), s_cu.known, s_cu.running);
    snprintf(line, sizeof(line), TR(UPD_CU_RUNNING), ver);
    text_in(c, BX, CU_RUN_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);
    cu_version_text(ver, sizeof(ver), s_cu.have_image, s_cu.carried);
    snprintf(line, sizeof(line), TR(UPD_CU_CARRIED), ver);
    text_in(c, BX, CU_CAR_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);

    const char *state = "";
    gfx_color_t col = UI_TEXT;
    upd_cu_text(&s_cu, &state, &col);
    (void)ui_text_wrap(c, gfx_rect_make(BX, CU_LINE_Y, BW,
                                        gfx_text_height(UPD_FONT_LABEL, 1)),
                       CU_PITCH, state, UPD_FONT_LABEL, col);

    const gfx_color_t fill = coproc_allowed() ? hold_fill(&s.cu_hold)
                                              : UI_PANEL_HI;
    ui_button_font(c, CU_BTN, TR(UPD_CU_HOLD), fill, false, coproc_allowed(),
                   UPD_FONT_LABEL);
}

/**
 * @brief UPDATES, drawn whole once per framebuffer and again after every
 *        change and through a hold.
 * @param c            Canvas, the body.
 * @param buffer_index The framebuffer drawn into.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((s.drawn_mask & bit) != 0u) {
        return;            /* every event and every change clears the mask */
    }
    gfx_clear(c, UI_BG);

    /* The buttons. */
    const bool busy = cancel_allowed();
    button(c, btn(0), busy ? TR(CANCEL) : TR(UPD_CHECK),
           busy ? UI_PANEL_HI : UI_ACCENT,
           s.have_press && s.hit == HIT_CHECK, busy || check_allowed());
    const bool on = settings_get_bool(SET_UPD_REMIND);
    button(c, btn(1), on ? TR(UPD_REMIND_ON) : TR(UPD_REMIND_OFF),
           UI_PANEL_HI, s.have_press && s.hit == HIT_REMIND, true);
    button(c, btn(2),
           testing_set() ? TR(UPD_CHAN_TESTING) : TR(UPD_CHAN_STABLE),
           UI_PANEL_HI, s.have_press && s.hit == HIT_CHANNEL, true);
    if (have_newer()) {
        button(c, btn(DL_ROW), TR(UPD_HOLD_DL), hold_fill(&s.hold), false,
               download_allowed());
    }
    render_coproc(c);

    /* What is kept, what runs, what last happened. */
    ui_card(c, gfx_rect_make(RX, RY, RW, RH), UI_PANEL);
    const int x = RX + RPAD, w = RW - 2 * RPAD;
    char line[96], second[96];
    int y = RY + RPAD;
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

    /* The release line's end when it is capped, the reminder and RELEASES,
     * at the card's foot: its last line ends 12px above the card's bottom
     * edge.  The line's end is drawn in UI_TEXT, the rest dim. */
    static const ui_text_id_t k_capped[] = { TX_UPD_LAST_LINE, TX_UPD_ABOUT_1,
                                             TX_UPD_ABOUT_2,
                                             TX_UPD_ABOUT_3_LINE };
    static const ui_text_id_t k_open[] = { TX_UPD_ABOUT_1, TX_UPD_ABOUT_2,
                                           TX_UPD_ABOUT_3 };
    const ui_text_id_t *about = s_v.line_capped ? k_capped : k_open;
    const size_t n_about = s_v.line_capped
                               ? sizeof(k_capped) / sizeof(k_capped[0])
                               : sizeof(k_open) / sizeof(k_open[0]);
    int foot = 0;
    for (size_t i = 0; i < n_about; ++i) {
        foot += ui_text_wrap(NULL, gfx_rect_make(x, 0, w, 24), PITCH,
                             ui_tr(about[i]), UPD_FONT_BODY, UI_TEXT);
    }
    y = RY + RH - 12 - gfx_text_height(UPD_FONT_BODY, 1)
        - PITCH * (foot - 1);
    for (size_t i = 0; i < n_about; ++i) {
        const bool lit = s_v.line_capped && i == 0u;
        y = para(c, x, y, w, ui_tr(about[i]), lit ? UI_TEXT : UI_TEXT_DIM);
    }

    if (s.hold.flash_ms > 0 || s.cu_hold.flash_ms > 0) {
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
    if (s_rem.hold.down || s_rem.hold.flash_ms > 0) {
        touched();
    }
    ui_hold_flash_step(&s_rem.hold, dt_s);
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

/**
 * @brief The reminder, drawn whole once per framebuffer and again after
 *        every change and through the hold's flash.
 * @param c            Canvas, the body.
 * @param buffer_index The framebuffer drawn into.
 */
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
    /* The versions, what the hold does, how to turn the reminder back on,
     * and why the hold is grey: the lines of a group 28px apart, the
     * groups 36 to 48px. */
    text_in(c, x, box.y + 44, w, line, UPD_FONT_BODY, UI_TEXT);
    text_in(c, x, box.y + 84, w, TR(UPD_R_WHAT_1), UPD_FONT_BODY,
            UI_TEXT_DIM);
    text_in(c, x, box.y + 84 + PITCH, w, TR(UPD_R_WHAT_2), UPD_FONT_BODY,
            UI_TEXT_DIM);
    text_in(c, x, box.y + 148, w, TR(UPD_R_NEVER), UPD_FONT_BODY,
            UI_TEXT_DIM);
    if (!download_allowed()) {
        char second[96];
        const upd_why_t why = (s_v.dl_block != UPD_WHY_NONE)
                                  ? s_v.dl_block : UPD_WHY_BUSY;
        blocked_text(why, line, sizeof(line), second, sizeof(second));
        text_in(c, x, box.y + 196, w, line, UPD_FONT_BODY, UI_WARN);
        if (second[0] != '\0') {
            text_in(c, x, box.y + 196 + PITCH, w, second, UPD_FONT_BODY,
                    UI_WARN);
        }
    }
    button(c, R_IGNORE, TR(UPD_IGNORE), UI_PANEL_HI,
           s_rem.have_press && s_rem.hit == HIT_IGNORE, true);
    button(c, R_NEVER, TR(UPD_NEVER), UI_PANEL_HI,
           s_rem.have_press && s_rem.hit == HIT_NEVER, true);
    button(c, R_HOLD, TR(UPD_HOLD_DL), hold_fill(&s_rem.hold), false,
           download_allowed());
    if (s_rem.hold.flash_ms > 0) {
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
            /* ARMED, the supply, the IOmcu's question: they come
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
