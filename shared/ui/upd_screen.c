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
#include "ui_footer.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_settings_int.h"

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
 * CHECK on the left, 460 x 56px at y 16.  The card on the right, x 484 to
 * 1016, y 16 to 336: what is kept and what last happened, each text broken
 * into lines of its 500px column (41 cells of UPD_FONT_BODY), 28px apart.
 * Under it the C6 RADIO row (upd_c6.h): its line at y 348, its hold beside
 * the IOmcu's at y 380.  The footer (ui_footer.h) under both: UPDATES,
 * the download's hold in the middle slot while a newer release is found,
 * and SETTINGS, the list of the reminder and RELEASES (upd_settings.c).
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
/** @brief The card's height, px: it ends 12px over the C6 row's line. */
#define RH      320
/** @brief The card's inner padding, px. */
#define RPAD    16
/** @brief A UPD_FONT_BODY line's pitch, px. */
#define PITCH   28
/*
 * The IOmcu section, in the left column under CHECK:
 * Its heading, the two versions and up to two lines of state in
 * UPD_FONT_LABEL 24px apart, and the hold, 460 x 56px, whose foot is
 * body y 436, over the footer, beside the C6 RADIO's hold.
 */
/** @brief The IOmcu section's heading. */
#define CU_HEAD_Y  88
/** @brief The version the IOmcu runs. */
#define CU_RUN_Y   112
/** @brief The version this panel carries. */
#define CU_CAR_Y   136
/** @brief The first line of state. */
#define CU_LINE_Y  164
/** @brief The state's line pitch, px. */
#define CU_PITCH   24
/** @brief HOLD 2s: UPDATE IOMCU's top. */
#define CU_BTN_Y   UPD_C6_BTN_Y
/** @brief HOLD 2s: UPDATE IOMCU. */
#define CU_BTN     gfx_rect_make(BX, CU_BTN_Y, BW, BH)

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
_Static_assert(RY + BH + 16 <= CU_HEAD_Y,
               "the IOmcu section starts under CHECK");
_Static_assert(CU_LINE_Y + 2 * CU_PITCH + 20 < CU_BTN_Y,
               "two lines of state end over the IOmcu's hold");
_Static_assert(CU_BTN_Y + BH <= UI_FOOTER_FLOOR,
               "the IOmcu's hold ends over the footer");
_Static_assert(RY + RH <= UI_FOOTER_FLOOR, "the card ends over the footer");
_Static_assert(RY + RH + 12 <= UPD_C6_LINE_Y, "the card ends over the C6 row");
_Static_assert(RX + RW <= UI_PANEL_W - UI_MARGIN, "the card keeps the margin");
_Static_assert(8 + 480 <= UPD_FLOOR, "the reminder ends over the band");
_Static_assert(400 + 64 + 24 <= 8 + 480, "the hold ends inside the box");

enum {
    HIT_NONE = 0,
    HIT_CHECK,          /* CHECK FOR UPDATES, or CANCEL while one runs */
    HIT_SETTINGS,       /* the footer's SETTINGS */
    HIT_DOWNLOAD,
    HIT_IGNORE,
    HIT_NEVER,
    HIT_COPROC,         /* HOLD 2s: UPDATE IOMCU */
    HIT_NET,            /* no home network: opens WI-FI */
};

/** @brief UPDATES' press, holds and drawn buffers. */
typedef struct {
    int       hit;           /**< the control the press began on */
    uint8_t   press_id;      /**< the press's contact */
    bool      have_press;    /**< a press is on a control */
    ui_hold_t hold;          /**< DOWNLOAD & INSTALL */
    ui_hold_t cu_hold;       /**< UPDATE IOMCU */
    unsigned  drawn_mask;    /**< the buffers that hold the current frame */
    uint32_t  drawn_list[2]; /**< the list's revision as each buffer drew it */
} upd_main_t;

/** @brief The reminder's press, hold and answer. */
typedef struct {
    int       hit;           /**< the control the press began on */
    uint8_t   press_id;      /**< the press's contact */
    bool      have_press;    /**< a press is on a control */
    ui_hold_t hold;          /**< the reminder's DOWNLOAD & INSTALL */
    bool      answered;      /**< IGNORE NOW, NEVER REMIND ME or the hold */
    bool      yield;         /**< the driver takes it down: not an answer */
    unsigned  drawn_mask;    /**< the buffers that hold the current frame */
} upd_rem_t;

/** @brief The update screens' state, one instance per image. */
typedef struct {
    upd_view_t    v;       /**< what both screens show */
    unsigned      req;     /**< what they ask for, not yet taken */
    upd_cu_view_t cu;      /**< UPDATES' IOmcu section */
    bool          cu_req;  /**< the IOmcu hold completed, not yet taken */
    upd_main_t    s;       /**< UPDATES */
    upd_rem_t     rem;     /**< the reminder */
} upd_state_t;

/** @brief The update screens' one instance. */
static upd_state_t u;

static void touched(void)
{
    u.s.drawn_mask = 0u;
    u.rem.drawn_mask = 0u;
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
    return u.v.job == UPD_JOB_CHECK && u.v.phase == UPD_PH_ENDED
           && u.v.got == UPD_GOT_NEWER && !u.v.unavailable;
}

static bool download_allowed(void)
{
    return have_newer() && u.v.dl_block == UPD_WHY_NONE && u.v.have_net;
}

static bool check_allowed(void)
{
    return !u.v.unavailable && u.v.have_net && u.v.block == UPD_WHY_NONE
           && !upd_busy(&u.v);
}

/* A busy CHECK is cancelled from the same button. */
static bool cancel_allowed(void)
{
    return upd_busy(&u.v) && u.v.job == UPD_JOB_CHECK;
}

/** @brief Whether UPDATE IOMCU's hold may run. @return true while READY */
static bool coproc_allowed(void) { return u.cu.sec == CU_SEC_READY; }

/* A press on a hold the model took away under it ends here. */
static void drop_holds_if_gone(void)
{
    if (!coproc_allowed() && u.s.have_press && u.s.hit == HIT_COPROC) {
        (void)ui_hold_left(&u.s.cu_hold);
        u.s.hit = HIT_NONE;
        u.s.have_press = false;
    }
    if (!download_allowed()) {
        if (u.s.have_press && u.s.hit == HIT_DOWNLOAD) {
            (void)ui_hold_left(&u.s.hold);
            u.s.hit = HIT_NONE;
            u.s.have_press = false;
        }
        if (u.rem.have_press && u.rem.hit == HIT_DOWNLOAD) {
            (void)ui_hold_left(&u.rem.hold);
            u.rem.hit = HIT_NONE;
            u.rem.have_press = false;
        }
    }
}

void upd_screen_set(const upd_view_t *v)
{
    /* As wifi_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &u.v, sizeof(u.v)) == 0) {
        return;
    }
    u.v = *v;
    drop_holds_if_gone();
    touched();
}

const upd_view_t *upd_screen_view(void) { return &u.v; }

void upd_screen_set_coproc(const upd_cu_view_t *v)
{
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &u.cu, sizeof(u.cu)) == 0) {
        return;
    }
    u.cu = *v;
    drop_holds_if_gone();
    touched();
}

const upd_cu_view_t *upd_screen_coproc(void) { return &u.cu; }

bool upd_screen_take_coproc(void)
{
    const bool was = u.cu_req;
    u.cu_req = false;
    return was;
}

unsigned upd_screen_take(void)
{
    const unsigned q = u.req;
    u.req = 0u;
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
    case UPD_WHY_COPROC:
        /* The gate holds for the IOmcu's update and the C6's alike. */
        snprintf(out, n, "%s",
                 upd_c6_view()->row == FW_C6_ROW_UPDATING ? TR(UPD_WAIT_C6)
                                                          : TR(UPD_WAIT_COPROC));
        break;
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

bool upd_result_said_above(const upd_view_t *v)
{
    return v != NULL && v->phase == UPD_PH_ENDED
           && v->got == UPD_GOT_NO_NET && !v->unavailable && !v->have_net;
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
    /* "is using Wi-Fi" holds only while WI-FI's session has the radio. */
    if ((v->got == UPD_GOT_ABORTED || v->got == UPD_GOT_BLOCKED)
        && v->why == UPD_WHY_FILES && v->radio_free) {
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
    switch (u.v.got) {
    case UPD_GOT_NEWER:   return UI_OK;
    case UPD_GOT_CURRENT:
    case UPD_GOT_PAST_LINE:
    case UPD_GOT_STAGED:
    case UPD_GOT_NONE:    return UI_TEXT;
    case UPD_GOT_ABORTED:
        return (u.v.why == UPD_WHY_CANCEL) ? UI_TEXT : UI_WARN;
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
    return ui_hold_button_fill(UI_PANEL_HI, UI_OK, h);
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
 * @brief CHECK FOR UPDATES, top left.
 * @return Its rectangle in the body.
 */
static gfx_rect_t check_rect(void)
{
    return gfx_rect_make(BX, RY, BW, BH);
}

/**
 * @brief The download's hold, in the footer's middle slot.
 * @return Its rectangle in the body.
 */
static gfx_rect_t dl_rect(void) { return ui_footer_rect(UI_FOOTER_HOLD); }

bool upd_settings_free(void)
{
    return !upd_busy(&u.v) && !(u.s.have_press && u.s.hit == HIT_DOWNLOAD);
}

/**
 * @brief The button that opens WI-FI shows: no home network, and Wi-Fi at
 *        this start.
 * @return Whether it is on the card.
 */
static bool net_shown(void)
{
    return !u.v.unavailable && !u.v.have_net;
}

/**
 * @brief The button that opens WI-FI, on the card under its lines on the
 *        running version and the home network.
 * @return Its rectangle in the body.
 */
static gfx_rect_t net_rect(void)
{
    const int x = RX + RPAD, w = RW - 2 * RPAD;
    char line[96];
    snprintf(line, sizeof(line), TR(WF_Q_RUN), u.v.running);
    int y = para(NULL, x, RY + RPAD, w, line, UI_TEXT) + 4;
    y = para(NULL, x, y, w, TR(WF_HOME_NONE), UI_WARN);
    return gfx_rect_make(x, y + 4, w, BH);
}

static int hit_at(int x, int y)
{
    if (net_shown() && !upd_busy(&u.v) && gfx_rect_contains(net_rect(), x, y)) {
        return HIT_NET;
    }
    if (gfx_rect_contains(check_rect(), x, y)
        && (check_allowed() || cancel_allowed())) {
        return HIT_CHECK;
    }
    if (gfx_rect_contains(ui_footer_rect(UI_FOOTER_SETTINGS), x, y)) {
        return HIT_SETTINGS;
    }
    if (download_allowed() && gfx_rect_contains(dl_rect(), x, y)) {
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
        u.req |= cancel_allowed() ? UPD_REQ_CANCEL : UPD_REQ_CHECK;
        break;
    case HIT_SETTINGS:
        /* The list opens with no job under way, and says why it does
         * not. */
        if (upd_settings_free()) {
            upd_list_open();
        } else {
            ui_router_set_alert(TR(FOOT_SETTINGS_BUSY));
        }
        break;
    case HIT_NET:
        /* WI-FI's home tag comes back here (ui_router_parent()). */
        ui_router_goto(SCREEN_WIFI);
        break;
    default:
        break;
    }
}

static void reset(void)
{
    memset(&u.s, 0, sizeof(u.s));
    ui_hold_reset(&u.s.hold);
    ui_hold_reset(&u.s.cu_hold);
    upd_c6_drop();
    memset(&u.cu, 0, sizeof(u.cu));
    u.cu_req = false;
    upd_list_reset();
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (upd_list_shown()) {
        upd_list_event(evt);
        if (!upd_list_shown()) {
            touched();
        }
        return;
    }
    if (upd_c6_event(evt, u.s.have_press)) {
        return;                    /* the C6 RADIO row's press */
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (u.s.have_press || upd_c6_pressed()) {
            return;                /* one press at a time */
        }
        u.s.hit = hit_at(x, y);
        if (u.s.hit != HIT_NONE) {
            u.s.have_press = true;
            u.s.press_id = evt->point.id;
            if (u.s.hit == HIT_DOWNLOAD) {
                ui_hold_begin(&u.s.hold, TX_HOLD_HINT_DOWNLOAD);
            } else if (u.s.hit == HIT_COPROC) {
                ui_hold_begin(&u.s.cu_hold, TX_HOLD_HINT_IOMCU_UPDATE);
            }
        }
        touched();
        return;
    }
    if (!u.s.have_press || evt->point.id != u.s.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off the hold abandons it, as on every held control. */
        if ((u.s.hit == HIT_DOWNLOAD && !gfx_rect_contains(dl_rect(), x, y)
             && ui_hold_leave(&u.s.hold))
            || (u.s.hit == HIT_COPROC && !gfx_rect_contains(CU_BTN, x, y)
                && ui_hold_leave(&u.s.cu_hold))) {
            u.s.hit = HIT_NONE;
            u.s.have_press = false;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = u.s.hit;
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    touched();
    if (hit == HIT_DOWNLOAD) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&u.s.hold, evt, dl_rect());
        return;
    }
    if (hit == HIT_COPROC) {
        (void)ui_hold_end(&u.s.cu_hold, evt, CU_BTN);
        return;
    }
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

static void tick(float dt_s)
{
    drop_holds_if_gone();
    if (upd_list_shown() && upd_busy(&u.v)) {
        upd_list_close();      /* a job started under it: the card shows */
        touched();
    }
    upd_list_tick(dt_s);
    if (upd_c6_tick(dt_s)) {
        touched();             /* the C6 row's fill follows its hold */
    }
    if (u.s.hold.down || u.s.hold.flash_ms > 0 || u.s.cu_hold.down
        || u.s.cu_hold.flash_ms > 0) {
        touched();             /* the fill follows the hold */
    }
    ui_hold_flash_step(&u.s.hold, dt_s);
    ui_hold_flash_step(&u.s.cu_hold, dt_s);
    if (download_allowed() && ui_hold_tick(&u.s.hold, dt_s)) {
        u.req |= UPD_REQ_DOWNLOAD;
        ui_hold_reached(&u.s.hold);
    }
    if (coproc_allowed() && ui_hold_tick(&u.s.cu_hold, dt_s)) {
        /* coproc_update_drive() takes it next frame and opens the
         * progress. */
        u.cu_req = true;
        ui_hold_reached(&u.s.cu_hold);
    }
}

static void cancel(void)
{
    upd_list_cancel();
    upd_c6_drop();
    if (u.s.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&u.s.hold);
    } else if (u.s.hit == HIT_COPROC) {
        (void)ui_hold_left(&u.s.cu_hold);
    }
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    touched();
}

static void enter(void)
{
    upd_list_close();
    upd_c6_drop();
    ui_hold_reset(&u.s.hold);
    ui_hold_reset(&u.s.cu_hold);
    touched();
}

static void leave(void)
{
    upd_list_close();
    upd_c6_drop();
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    ui_hold_reset(&u.s.hold);
    ui_hold_reset(&u.s.cu_hold);
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
    case CU_SEC_NO_BOARD: *line = TR(UPD_CU_NO_BOARD); *col = UI_TEXT_DIM;
                          break;
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
    cu_version_text(ver, sizeof(ver), u.cu.known, u.cu.running);
    snprintf(line, sizeof(line), TR(UPD_CU_RUNNING), ver);
    text_in(c, BX, CU_RUN_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);
    cu_version_text(ver, sizeof(ver), u.cu.have_image, u.cu.carried);
    snprintf(line, sizeof(line), TR(UPD_CU_CARRIED), ver);
    text_in(c, BX, CU_CAR_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);

    const char *state = "";
    gfx_color_t col = UI_TEXT;
    upd_cu_text(&u.cu, &state, &col);
    (void)ui_text_wrap(c, gfx_rect_make(BX, CU_LINE_Y, BW,
                                        gfx_text_height(UPD_FONT_LABEL, 1)),
                       CU_PITCH, state, UPD_FONT_LABEL, col);

    const gfx_color_t fill = coproc_allowed() ? hold_fill(&u.s.cu_hold)
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
    const int b = buffer_index & 1;
    if (upd_list_shown()) {
        /* The list draws itself whole when its look changed. */
        if ((u.s.drawn_mask & bit) == 0u || u.s.drawn_list[b] != upd_list_revision()) {
            u.s.drawn_list[b] = upd_list_revision();
            upd_list_render(c);
            u.s.drawn_mask |= bit;
        }
        return;
    }
    if ((u.s.drawn_mask & bit) != 0u) {
        return;            /* every event and every change clears the mask */
    }
    gfx_clear(c, UI_BG);

    /* CHECK, and the footer: the download's hold, SETTINGS. */
    const bool busy = cancel_allowed();
    button(c, check_rect(), busy ? TR(CANCEL) : TR(UPD_CHECK),
           busy ? UI_PANEL_HI : UI_ACCENT,
           u.s.have_press && u.s.hit == HIT_CHECK, busy || check_allowed());
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "UPDATES", NULL, false);
    if (have_newer()) {
        ui_hold_button(c, dl_rect(), TR(UPD_HOLD_DL), UI_PANEL_HI, UI_OK,
                       &u.s.hold, false, download_allowed(), UPD_FONT_LABEL);
    }
    ui_footer_settings(c, u.s.have_press && u.s.hit == HIT_SETTINGS,
                       upd_settings_free());
    render_coproc(c);
    upd_c6_render(c);

    /* What is kept, what runs, what last happened. */
    ui_card(c, gfx_rect_make(RX, RY, RW, RH), UI_PANEL);
    const int x = RX + RPAD, w = RW - 2 * RPAD;
    char line[96], second[96];
    int y = RY + RPAD;
    snprintf(line, sizeof(line), TR(WF_Q_RUN), u.v.running);
    y = para(c, x, y, w, line, UI_TEXT) + 4;
    if (u.v.unavailable) {
        y = para(c, x, y, w, TR(WF_UNAVAIL_1), UI_WARN);
        y = para(c, x, y, w, TR(WF_UNAVAIL_2), UI_WARN) + 12;
    } else if (u.v.have_net) {
        snprintf(line, sizeof(line), TR(WF_HOME_IS), u.v.net);
        y = para(c, x, y, w, line, UI_TEXT_DIM) + 12;
    } else {
        y = para(c, x, y, w, TR(WF_HOME_NONE), UI_WARN);
        button(c, net_rect(), TR(UPD_OPEN_WIFI), UI_PANEL_HI,
               u.s.have_press && u.s.hit == HIT_NET, !upd_busy(&u.v));
        y += 4 + BH + 12;
    }

    /* The job under way, or how the last one ended.  A check that ended
     * for want of a home network says what the lines above already say,
     * and is not said twice. */
    upd_result_text(&u.v, line, sizeof(line), second, sizeof(second));
    if (line[0] != '\0' && !upd_result_said_above(&u.v)) {
        const gfx_color_t col = upd_busy(&u.v) ? UI_TEXT : result_colour();
        y = para(c, x, y, w, line, col);
        y = para(c, x, y, w, second, UI_TEXT_DIM) + 12;
    }

    /* Why the buttons are grey, when they are and the result did not
     * already say so. */
    if (!u.v.unavailable && u.v.have_net && !upd_busy(&u.v)) {
        const upd_why_t why = (u.v.block != UPD_WHY_NONE)
            ? u.v.block
            : (have_newer() ? u.v.dl_block : UPD_WHY_NONE);
        if (why != UPD_WHY_NONE
            && !(u.v.got == UPD_GOT_BLOCKED && u.v.why == why)) {
            blocked_text(why, line, sizeof(line), second, sizeof(second));
            y = para(c, x, y, w, line, UI_WARN);
            y = para(c, x, y, w, second, UI_WARN) + 12;
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
    const ui_text_id_t *about = u.v.line_capped ? k_capped : k_open;
    const size_t n_about = u.v.line_capped
                               ? sizeof(k_capped) / sizeof(k_capped[0])
                               : sizeof(k_open) / sizeof(k_open[0]);
    int foot = 0;
    for (size_t i = 0; i < n_about; ++i) {
        foot += ui_text_wrap(NULL, gfx_rect_make(x, 0, w, 24), PITCH,
                             ui_tr(about[i]), UPD_FONT_BODY, UI_TEXT);
    }
    const int top = RY + RH - 12 - gfx_text_height(UPD_FONT_BODY, 1)
                    - PITCH * (foot - 1);
    /* Under a long result they give way: the result is what happened. */
    const bool fits = top >= y;
    y = top;
    for (size_t i = 0; fits && i < n_about; ++i) {
        const bool lit = u.v.line_capped && i == 0u;
        y = para(c, x, y, w, ui_tr(about[i]), lit ? UI_TEXT : UI_TEXT_DIM);
    }

    if (u.s.hold.flash_ms > 0 || u.s.cu_hold.flash_ms > 0 || upd_c6_flashing()) {
        u.s.drawn_mask = 0u;
    } else {
        u.s.drawn_mask |= bit;
    }
}

/** @brief The home tag closes the list first, through its leave warning.
 *  @return Whether it went up a level. */
static bool back(void)
{
    const bool was = upd_list_shown();
    const bool climbed = upd_list_back();
    if (was) {
        touched();
    }
    return climbed;
}

/** @brief The list covers the screen. @return Whether it is open. */
static bool covered(void) { return upd_list_shown(); }

static const ui_screen_t k_screen = {
    .title  = "UPDATES",
    .covered = covered,
    .back   = back,
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
    memset(&u.rem, 0, sizeof(u.rem));
    ui_hold_reset(&u.rem.hold);
}

static void r_event(const touch_event_t *evt)
{
    if (evt == NULL || u.rem.answered) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (u.rem.have_press) {
            return;
        }
        u.rem.hit = r_hit_at(x, y);
        if (u.rem.hit != HIT_NONE) {
            u.rem.have_press = true;
            u.rem.press_id = evt->point.id;
            if (u.rem.hit == HIT_DOWNLOAD) {
                ui_hold_begin(&u.rem.hold, TX_HOLD_HINT_DOWNLOAD);
            }
        }
        touched();
        return;
    }
    if (!u.rem.have_press || evt->point.id != u.rem.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (u.rem.hit == HIT_DOWNLOAD && !gfx_rect_contains(R_HOLD, x, y)
            && ui_hold_leave(&u.rem.hold)) {
            u.rem.hit = HIT_NONE;
            u.rem.have_press = false;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = u.rem.hit;
    u.rem.hit = HIT_NONE;
    u.rem.have_press = false;
    touched();
    if (hit == HIT_DOWNLOAD) {
        (void)ui_hold_end(&u.rem.hold, evt, R_HOLD);
        return;
    }
    if (!touch_event_is_tap_up(evt) || r_hit_at(x, y) != hit) {
        return;
    }
    if (hit == HIT_NEVER) {
        settings_set(SET_UPD_REMIND, 0.0f);
        settings_request_save();
        u.rem.answered = true;
    } else if (hit == HIT_IGNORE) {
        u.rem.answered = true;
    }
}

static void r_tick(float dt_s)
{
    drop_holds_if_gone();
    if (u.rem.hold.down || u.rem.hold.flash_ms > 0) {
        touched();
    }
    ui_hold_flash_step(&u.rem.hold, dt_s);
    if (!u.rem.answered && download_allowed() && ui_hold_tick(&u.rem.hold, dt_s)) {
        u.req |= UPD_REQ_DOWNLOAD;
        u.rem.answered = true;
        ui_hold_reached(&u.rem.hold);
    }
}

static void r_cancel(void)
{
    if (u.rem.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&u.rem.hold);
    }
    u.rem.hit = HIT_NONE;
    u.rem.have_press = false;
    touched();
}

static void r_enter(void)
{
    u.rem.answered = false;
    u.rem.yield = false;
    u.rem.hit = HIT_NONE;
    u.rem.have_press = false;
    ui_hold_reset(&u.rem.hold);
    touched();
}

/* Left any way but the driver's own: answered, as IGNORE NOW. */
static void r_leave(void)
{
    if (!u.rem.yield) {
        u.rem.answered = true;
    }
    u.rem.hit = HIT_NONE;
    u.rem.have_press = false;
    ui_hold_reset(&u.rem.hold);
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
    if ((u.rem.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    const gfx_rect_t box = R_BOX;
    ui_panel(c, box, TR(UPD_R_TITLE), UI_ACCENT);
    const int x = box.x + 24, w = box.w - 48;
    char line[112];
    snprintf(line, sizeof(line), TR(UPD_R_VERSIONS), u.v.found, u.v.running);
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
        const upd_why_t why = (u.v.dl_block != UPD_WHY_NONE)
                                  ? u.v.dl_block : UPD_WHY_BUSY;
        blocked_text(why, line, sizeof(line), second, sizeof(second));
        text_in(c, x, box.y + 196, w, line, UPD_FONT_BODY, UI_WARN);
        if (second[0] != '\0') {
            text_in(c, x, box.y + 196 + PITCH, w, second, UPD_FONT_BODY,
                    UI_WARN);
        }
    }
    button(c, R_IGNORE, TR(UPD_IGNORE), UI_PANEL_HI,
           u.rem.have_press && u.rem.hit == HIT_IGNORE, true);
    button(c, R_NEVER, TR(UPD_NEVER), UI_PANEL_HI,
           u.rem.have_press && u.rem.hit == HIT_NEVER, true);
    button(c, R_HOLD, TR(UPD_HOLD_DL), hold_fill(&u.rem.hold), false,
           download_allowed());
    if (u.rem.hold.flash_ms > 0) {
        u.rem.drawn_mask = 0u;
    } else {
        u.rem.drawn_mask |= bit;
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
        if (u.rem.answered) {
            d->dismissed = true;
            /* The hold's download takes the panel to PANEL UPDATE: the
             * application goes there itself. */
            ui_router_goto(SCREEN_OVERVIEW);
        } else if (!may) {
            /* ARMED, the supply, the IOmcu's question: they come
             * first.  The reminder comes back once they have gone. */
            u.rem.yield = true;
            ui_router_goto(SCREEN_OVERVIEW);
            u.rem.yield = false;
        }
        return;
    }
    if (u.rem.answered) {
        d->dismissed = true;        /* left by the HOME tag or the menu */
        u.rem.answered = false;
    }
    if (!d->dismissed && may && ui_router_current() == SCREEN_OVERVIEW) {
        ui_router_goto(SCREEN_UPD_REMIND);
    }
}
