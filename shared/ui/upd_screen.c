/**
 * @file upd_screen.c
 * @brief UPDATES: upd_screen.h says what it is for.  The reminder is
 *        upd_remind.c, SINGLE UPDATES upd_single.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_footer.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen_int.h"
#include "upd_settings_int.h"
#include "upd_single.h"

_Static_assert(BH >= UI_TAP_MIN, "UPDATES' buttons are a finger tall");
_Static_assert(RY + RH <= UI_FOOTER_FLOOR, "the card ends over the footer");
_Static_assert(RX + RW <= UI_PANEL_W - UI_MARGIN, "the card keeps the margin");

enum {
    HIT_NONE = 0,
    HIT_CHECK,          /* CHECK FOR UPDATES, or CANCEL while one runs */
    HIT_SETTINGS,       /* the footer's OPTIONS */
    HIT_DOWNLOAD,       /* HOLD 2s TO DOWNLOAD & INSTALL */
    HIT_NET,            /* no home network: opens WI-FI */
};

/** @brief UPDATES' press, hold and drawn buffers. */
typedef struct {
    int       hit;           /**< the control the press began on */
    uint8_t   press_id;      /**< the press's contact */
    bool      have_press;    /**< a press is on a control */
    ui_hold_t hold;          /**< DOWNLOAD & INSTALL */
    unsigned  drawn_mask;    /**< the buffers that hold the current frame */
    uint32_t  drawn_list[2]; /**< the list's revision as each buffer drew it */
} upd_main_t;

/** @brief The update screens' state, one instance per image. */
typedef struct {
    upd_view_t    v;       /**< what both screens show */
    unsigned      req;     /**< what they ask for, not yet taken */
    upd_main_t    s;       /**< UPDATES */
} upd_state_t;

/** @brief The update screens' one instance. */
static upd_state_t u;

/** @brief Marks both screens to be drawn again. */
static void touched(void)
{
    u.s.drawn_mask = 0u;
    upd_remind_touched();
}

void upd_screen_invalidate(void) { touched(); }

/* ------------------------------------------------------------- the model */

bool upd_busy(const upd_view_t *v)
{
    return v != NULL && v->phase != UPD_PH_IDLE && v->phase != UPD_PH_ENDED;
}

/**
 * @brief The last check found a newer release, and nothing runs.
 */
static bool have_newer(void)
{
    return u.v.job == UPD_JOB_CHECK && u.v.phase == UPD_PH_ENDED
           && u.v.got == UPD_GOT_NEWER && !u.v.unavailable;
}

bool upd_download_allowed(void)
{
    return have_newer() && u.v.dl_block == UPD_WHY_NONE && u.v.have_net;
}

/** @brief Whether a download may start now. @return True then. */
static bool download_allowed(void) { return upd_download_allowed(); }

void upd_screen_ask(unsigned req) { u.req |= req; }

/** @brief Whether a check may start now. */
static bool check_allowed(void)
{
    return !u.v.unavailable && u.v.have_net && u.v.block == UPD_WHY_NONE
           && !upd_busy(&u.v);
}

/**
 * @brief A busy CHECK is cancelled from the same button.
 */
static bool cancel_allowed(void)
{
    return upd_busy(&u.v) && u.v.job == UPD_JOB_CHECK;
}

/**
 * @brief A press on a hold the model took away under it ends here.
 */
static void drop_holds_if_gone(void)
{
    if (!download_allowed() && u.s.have_press && u.s.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&u.s.hold);
        u.s.hit = HIT_NONE;
        u.s.have_press = false;
    }
    upd_remind_drop_if_gone();
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

unsigned upd_screen_take(void)
{
    const unsigned q = u.req;
    u.req = 0u;
    return q;
}

/* ------------------------------------------------------------ the texts */

/** @brief Gives the text of the release problem a job found. */
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

/** @brief Gives the text of the join problem a job found. */
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

void upd_blocked_text(upd_why_t why, char *out, size_t n, char *second,
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
        /* The gate holds for the IOmcu's update, the C6's and a wireless
         * device's alike. */
        snprintf(out, n, "%s",
                 upd_c6_view()->row == FW_C6_ROW_UPDATING ? TR(UPD_WAIT_C6)
                 : upd_dev_running()                      ? TR(UPD_WAIT_DEVICE)
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

/**
 * @brief Why a job was ended from outside (ABORTED).
 */
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

/** @brief Gives the text of a job phase. */
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
        upd_blocked_text(v->why, out, n, second, n2);
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

/**
 * @brief Whether the result line is good news, bad news or neither.
 */
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

void upd_text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
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

gfx_color_t upd_hold_fill(const ui_hold_t *h)
{
    return ui_hold_button_fill(UI_PANEL_HI, UI_OK, h);
}

void upd_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
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

/** @brief Finds the control at a point. */
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
    return HIT_NONE;
}

/** @brief Acts on a control that was tapped. */
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

/** @brief Returns the UPDATES screen to its initial state. */
static void reset(void)
{
    memset(&u.s, 0, sizeof(u.s));
    ui_hold_reset(&u.s.hold);
    upd_single_reset();
    upd_list_reset();
}

/** @brief Takes a touch event of the UPDATES screen. */
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
    if (upd_single_shown()) {
        upd_single_event(evt);     /* SINGLE UPDATES, whole */
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (u.s.have_press) {
            return;                /* one press at a time */
        }
        u.s.hit = hit_at(x, y);
        if (u.s.hit != HIT_NONE) {
            u.s.have_press = true;
            u.s.press_id = evt->point.id;
            if (u.s.hit == HIT_DOWNLOAD) {
                ui_hold_begin(&u.s.hold, TX_HOLD_HINT_DOWNLOAD);
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
        if (u.s.hit == HIT_DOWNLOAD && !gfx_rect_contains(dl_rect(), x, y)
            && ui_hold_leave(&u.s.hold)) {
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
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

/** @brief Runs the UPDATES screen for one frame: its holds. */
static void tick(float dt_s)
{
    drop_holds_if_gone();
    if (upd_list_shown() && upd_busy(&u.v)) {
        upd_list_close();      /* a job started under it: the card shows */
        touched();
    }
    upd_list_tick(dt_s);
    if (upd_single_tick(dt_s)) {
        touched();             /* a fill on SINGLE UPDATES follows its hold */
    }
    if (u.s.hold.down || u.s.hold.flash_ms > 0) {
        touched();             /* the fill follows the hold */
    }
    ui_hold_flash_step(&u.s.hold, dt_s);
    if (download_allowed() && ui_hold_tick(&u.s.hold, dt_s)) {
        u.req |= UPD_REQ_DOWNLOAD;
        ui_hold_reached(&u.s.hold);
    }
}

/** @brief Drops the presses under way, as touch events were lost. */
static void cancel(void)
{
    upd_list_cancel();
    upd_single_cancel();
    if (u.s.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&u.s.hold);
    }
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    touched();
}

/** @brief Enters the UPDATES screen. */
static void enter(void)
{
    upd_list_close();
    upd_single_close();
    ui_hold_reset(&u.s.hold);
    touched();
}

/** @brief Leaves the UPDATES screen: SINGLE UPDATES closes, every hold on
 *         it ends unfired. */
static void leave(void)
{
    upd_list_close();
    upd_single_close();
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    ui_hold_reset(&u.s.hold);
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
    if (upd_single_shown()) {
        /* SINGLE UPDATES, or the devices' page over it. */
        if (upd_single_render(c)) {
            u.s.drawn_mask = 0u;
        } else {
            u.s.drawn_mask |= bit;
        }
        return;
    }
    gfx_clear(c, UI_BG);

    /* CHECK, and the footer: the download's hold, OPTIONS. */
    const bool busy = cancel_allowed();
    upd_button(c, check_rect(), busy ? TR(CANCEL) : TR(UPD_CHECK),
           busy ? UI_PANEL_HI : UI_ACCENT,
           u.s.have_press && u.s.hit == HIT_CHECK, busy || check_allowed());
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "UPDATES", NULL, false);
    if (have_newer()) {
        ui_hold_button(c, dl_rect(), TR(UPD_HOLD_DL), UI_PANEL_HI, UI_OK,
                       &u.s.hold, false, download_allowed(), UPD_FONT_LABEL);
    }
    ui_footer_settings(c, u.s.have_press && u.s.hit == HIT_SETTINGS,
                       upd_settings_free());

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
        upd_button(c, net_rect(), TR(UPD_OPEN_WIFI), UI_PANEL_HI,
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
            upd_blocked_text(why, line, sizeof(line), second, sizeof(second));
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

    if (u.s.hold.flash_ms > 0) {
        u.s.drawn_mask = 0u;
    } else {
        u.s.drawn_mask |= bit;
    }
}

/** @brief The home tag closes the devices' page, SINGLE UPDATES, or the
 *        list through its leave warning.
 *  @return Whether it went up a level. */
static bool back(void)
{
    if (upd_dev_shown()) {
        upd_dev_close();
        return true;
    }
    if (upd_single_shown()) {
        upd_single_close();
        return true;
    }
    const bool was = upd_list_shown();
    const bool climbed = upd_list_back();
    if (was) {
        touched();
    }
    return climbed;
}

/** @brief The list, SINGLE UPDATES or the devices' page covers the screen.
 *  @return Whether one is open. */
static bool covered(void)
{
    return upd_list_shown() || upd_single_shown() || upd_dev_shown();
}

bool upd_guide_newer(void) { return have_newer(); }

bool upd_guide_cancels(void) { return cancel_allowed(); }

/** @brief The UPDATES screen's table. */
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
    .help_count = upd_help_count,
    .help_page  = upd_help_page,
};

const ui_screen_t *upd_screen(void) { return &k_screen; }
