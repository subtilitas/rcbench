/**
 * @file upd_screen.c
 * @brief UPDATES: upd_screen.h says what it is for.  CHECK, OPTIONS and
 *        the result texts; the steps and the card are upd_wiz_view.c and
 *        upd_wiz_card.c, the reminder upd_remind.c, SINGLE UPDATES
 *        upd_single.c.
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
#include "upd_wiz_view.h"

_Static_assert(BH >= UI_TAP_MIN, "UPDATES' buttons are a finger tall");
_Static_assert(RY + RH <= UI_FOOTER_FLOOR, "the card ends over the footer");
_Static_assert(RX + RW <= UI_PANEL_W - UI_MARGIN, "the card keeps the margin");

enum {
    HIT_NONE = 0,
    HIT_CHECK,          /* CHECK FOR UPDATES, or CANCEL while one runs */
    HIT_SETTINGS,       /* the footer's OPTIONS */
};

/** @brief UPDATES' press on CHECK or OPTIONS, and its drawn buffers.  The
 *         steps' presses are upd_wiz_view.c's. */
typedef struct {
    int       hit;           /**< the control the press began on */
    uint8_t   press_id;      /**< the press's contact */
    bool      have_press;    /**< a press is on a control */
    unsigned  drawn_mask;    /**< the buffers that hold the current frame */
    uint32_t  drawn_list[2]; /**< the list's revision as each buffer drew it */
} upd_main_t;

/** @brief The update screens' state, one instance per image. */
typedef struct {
    upd_view_t    v;       /**< what both screens show */
    unsigned      req;     /**< what they ask for, not yet taken */
    bool          left;    /**< the home tag left UPDATES, not yet taken */
    bool          left_cu; /**< the same, for the IOmcu's offer */
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

void upd_screen_set(const upd_view_t *v)
{
    /* As wifi_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &u.v, sizeof(u.v)) == 0) {
        return;
    }
    u.v = *v;
    upd_remind_drop_if_gone();
    touched();
}

const upd_view_t *upd_screen_view(void) { return &u.v; }

unsigned upd_screen_take(void)
{
    const unsigned q = u.req;
    u.req = 0u;
    return q;
}

bool upd_screen_take_left(void)
{
    const bool was = u.left;
    u.left = false;
    return was;
}

bool upd_screen_take_left_cu(void)
{
    const bool was = u.left_cu;
    u.left_cu = false;
    return was;
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

gfx_color_t upd_result_colour(const upd_view_t *v)
{
    switch (v->got) {
    case UPD_GOT_NEWER:   return UI_OK;
    case UPD_GOT_CURRENT:
    case UPD_GOT_PAST_LINE:
    case UPD_GOT_STAGED:
    case UPD_GOT_NONE:    return UI_TEXT;
    case UPD_GOT_ABORTED:
        return (v->why == UPD_WHY_CANCEL) ? UI_TEXT : UI_WARN;
    default:              return UI_WARN;
    }
}

void upd_text_in(gfx_canvas_t *c, int x, int y, int w, const char *t,
                 const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, gfx_text_height(f, 1)), t, f, col,
                1, GFX_ALIGN_LEFT);
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

bool upd_settings_free(void)
{
    return !upd_busy(&u.v) && !upd_wiz_holding() && !upd_wiz_running();
}

/** @brief Finds UPDATES' own control at a point: CHECK or OPTIONS. */
static int hit_at(int x, int y)
{
    if (gfx_rect_contains(check_rect(), x, y)
        && (check_allowed() || cancel_allowed())) {
        return HIT_CHECK;
    }
    if (gfx_rect_contains(ui_footer_rect(UI_FOOTER_SETTINGS), x, y)) {
        return HIT_SETTINGS;
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
        /* The list opens with no job and no update under way, and says
         * why it does not. */
        if (upd_settings_free()) {
            upd_list_open();
        } else {
            ui_router_set_alert(TR(FOOT_SETTINGS_BUSY));
        }
        break;
    default:
        break;
    }
}

/** @brief Returns the UPDATES screen to its initial state: the panel's
 *         start, so the skipped steps are cleared too. */
static void reset(void)
{
    memset(&u.s, 0, sizeof(u.s));
    u.left = false;
    u.left_cu = false;
    upd_wiz_reset();
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
    if (upd_wiz_event(evt, u.s.have_press)) {
        return;                    /* the steps' controls */
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
        }
        touched();
        return;
    }
    if (!u.s.have_press || evt->point.id != u.s.press_id) {
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = u.s.hit;
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    touched();
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

/** @brief Runs the UPDATES screen for one frame: its holds. */
static void tick(float dt_s)
{
    upd_remind_drop_if_gone();
    if (upd_list_shown() && upd_busy(&u.v)) {
        upd_list_close();      /* a job started under it: the card shows */
        touched();
    }
    upd_list_tick(dt_s);
    if (upd_single_tick(dt_s)) {
        touched();             /* a fill on SINGLE UPDATES follows its hold */
    }
    if (upd_wiz_tick(dt_s)) {
        touched();             /* the fill follows the step's hold */
    }
}

/** @brief Drops the presses under way, as touch events were lost. */
static void cancel(void)
{
    upd_list_cancel();
    upd_single_cancel();
    upd_wiz_drop();
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
    touched();
}

/** @brief Enters the UPDATES screen. */
static void enter(void)
{
    upd_list_close();
    upd_single_close();
    upd_wiz_drop();
    touched();
}

/** @brief Leaves the UPDATES screen: SINGLE UPDATES closes, every hold on
 *         it ends unfired. */
static void leave(void)
{
    upd_list_close();
    upd_single_close();
    upd_wiz_drop();
    u.s.hit = HIT_NONE;
    u.s.have_press = false;
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

    /* CHECK, in the accent only while no step is current: one bright
     * control on the screen.  The footer: UPDATES, OPTIONS. */
    static upd_wiz_t w;       /* off the render task's stack */
    upd_wiz_now(&w);
    const bool busy = cancel_allowed();
    upd_button(c, check_rect(), busy ? TR(CANCEL) : TR(UPD_CHECK),
               (busy || w.current < UPD_WIZ_STEPS) ? UI_PANEL_HI : UI_ACCENT,
               u.s.have_press && u.s.hit == HIT_CHECK, busy || check_allowed());
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "UPDATES", NULL, false);
    ui_footer_settings(c, u.s.have_press && u.s.hit == HIT_SETTINGS,
                       upd_settings_free());

    /* The steps, the card, its controls and CANCEL UPDATE. */
    if (upd_wiz_render(c)) {
        u.s.drawn_mask = 0u;
    } else {
        u.s.drawn_mask |= bit;
    }
}

/** @brief The home tag closes the devices' page, SINGLE UPDATES, or the
 *        list through its leave warning; with none of them shown it leaves
 *        UPDATES, which upd_screen_take_left() reports once.
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
    /* Nothing covers UPDATES: the router leaves it for the parent. */
    u.left = u.left || !climbed;
    u.left_cu = u.left_cu || !climbed;
    return climbed;
}

/** @brief The list, SINGLE UPDATES or the devices' page covers the screen.
 *  @return Whether one is open. */
static bool covered(void)
{
    return upd_list_shown() || upd_single_shown() || upd_dev_shown();
}

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
