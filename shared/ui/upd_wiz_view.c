/**
 * @file upd_wiz_view.c
 * @brief UPDATES' steps: the list, the card's controls and their presses.
 *        See upd_wiz_view.h; the card's texts are upd_wiz_card.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_wiz_view.h"

#include <stdio.h>
#include <string.h>

#include "ui_footer.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"
#include "upd_screen_int.h"
#include "upd_single.h"

_Static_assert(UPD_WIZ_ROW_Y0 >= RY + BH + 16,
               "the first row starts 16px under CHECK");
_Static_assert(UPD_WIZ_ROW_Y0 + (UPD_WIZ_STEPS - 1) * UPD_WIZ_ROW_PITCH
                   + UPD_WIZ_ROW_H <= UI_FOOTER_FLOOR,
               "the last row ends over the footer");
_Static_assert(UPD_WIZ_ROW_H <= UPD_WIZ_ROW_PITCH - 8,
               "the rows stand 8px apart");
_Static_assert(UPD_WIZ_NAME_X >= UPD_WIZ_ROW_X + UPD_WIZ_BOX + 8,
               "the name starts 8px right of the number box");
_Static_assert(UPD_WIZ_NAME_X + UPD_WIZ_NAME_W + 8 <= UPD_WIZ_VER_X,
               "the version starts 8px right of the name");
_Static_assert(UPD_WIZ_VER_X + UPD_WIZ_VER_W + 8
                   <= UPD_WIZ_ROW_X + UPD_WIZ_ROW_W - 8 - UPD_WIZ_TAG_W,
               "the tag starts 8px right of the version");
_Static_assert(UPD_WIZ_ROW_X + UPD_WIZ_ROW_W + 8 <= UPD_WIZ_CARD_X,
               "the card stands 8px right of the rows");
_Static_assert(UPD_WIZ_CARD_X + UPD_WIZ_CARD_W <= UI_PANEL_W - UI_MARGIN,
               "the card keeps the margin");
_Static_assert(UPD_WIZ_CARD_Y + UPD_WIZ_CARD_H <= UI_FOOTER_FLOOR,
               "the card ends over the footer");
_Static_assert(UPD_WIZ_ACT_H >= UI_TAP_MIN && UPD_WIZ_SKIP_H >= UI_TAP_MIN,
               "the card's controls are a finger tall");
_Static_assert(UPD_WIZ_ACT_Y + UPD_WIZ_ACT_H + 8 <= UPD_WIZ_SKIP_Y,
               "SKIP stands 8px under the control");
_Static_assert(UPD_WIZ_SKIP_Y + UPD_WIZ_SKIP_H + UPD_WIZ_PAD
                   == UPD_WIZ_CARD_Y + UPD_WIZ_CARD_H,
               "SKIP ends 16px over the card's foot");

/** @brief The control and button faces: 10 x 20. */
#define WIZ_FONT UPD_FONT_LABEL

/** @brief Bytes of a step's last failed attempt: the band's line. */
#define RESULT_MAX UI_ALERT_MAX

/** @brief What a press began on. */
enum {
    WP_NONE = 0,
    WP_ACT,     /* the current step's live hold */
    WP_WHY,     /* its grey hold: why, on the band */
    WP_SKIP,    /* SKIP THIS STEP */
    WP_SHOW,    /* SHOW SKIPPED STEPS */
    WP_NET,     /* the last card's button that opens WI-FI */
    WP_CANCEL,  /* CANCEL UPDATE in the footer */
};

/** @brief The steps' state, one instance per image. */
typedef struct {
    bool          run;       /**< an update run stands after a restart */
    unsigned      skip;      /**< UPD_WIZ_BIT()s skipped since the start */
    bool          skip_cu;   /**< SKIP on the IOmcu's step, not yet taken */
    /** @brief Each step's last failed attempt, "" for none. */
    char          result[UPD_WIZ_STEPS][RESULT_MAX];
    int           hit;       /**< what the press began on */
    uint8_t       id;        /**< its contact */
    bool          have;      /**< a press is on a control of the steps */
    unsigned      step;      /**< the current step at its DOWN */
    upd_wiz_act_t act;       /**< the control at its DOWN */
    ui_hold_t     hold;      /**< the control's 2s hold */
} wiz_state_t;

/** @brief The steps' one instance. */
static wiz_state_t z;

/* ------------------------------------------------------------ the model */

void upd_wiz_now(upd_wiz_t *out)
{
    upd_wiz_in_t in;
    memset(&in, 0, sizeof(in));
    in.panel = upd_screen_view();
    in.cu = upd_screen_coproc();
    in.c6 = upd_c6_view();
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        in.dev[d] = upd_dev_view((upd_dev_id_t)d);
    }
    in.run = z.run;
    in.skip = z.skip;
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        in.result[i] = z.result[i];
    }
    upd_wizard_eval(&in, out);
}

/**
 * @brief The steps now, in one static answer: the model is decided once
 *        per call on the render task, off its stack.
 * @return the answer, valid until the next call
 */
static const upd_wiz_t *now(void)
{
    static upd_wiz_t w;
    upd_wiz_now(&w);
    return &w;
}

void upd_screen_set_run(bool run)
{
    if (run != z.run) {
        z.run = run;
        upd_screen_invalidate();
    }
}

void upd_screen_set_result(unsigned step, const char *text)
{
    if (step >= UPD_WIZ_STEPS) {
        return;
    }
    const char *t = text != NULL ? text : "";
    if (strncmp(z.result[step], t, RESULT_MAX - 1u) != 0) {
        (void)snprintf(z.result[step], RESULT_MAX, "%s", t);
        upd_screen_invalidate();
    }
}

bool upd_screen_take_skip_coproc(void)
{
    const bool was = z.skip_cu;
    z.skip_cu = false;
    return was;
}

bool upd_wiz_running(void)
{
    const upd_wiz_t *w = now();
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        if (w->step[i].state == UPD_WIZ_RUNNING) {
            return true;
        }
    }
    return false;
}

bool upd_wiz_net_shown(const upd_wiz_t *w)
{
    const upd_view_t *v = upd_screen_view();
    return w->current >= UPD_WIZ_STEPS && !v->unavailable && !v->have_net
           && !upd_busy(v);
}

/* ------------------------------------------------------------ the rects */

gfx_rect_t upd_wiz_row_rect(unsigned i)
{
    const unsigned k = i < UPD_WIZ_STEPS ? i : 0u;
    return gfx_rect_make(UPD_WIZ_ROW_X,
                         UPD_WIZ_ROW_Y0 + (int)k * UPD_WIZ_ROW_PITCH,
                         UPD_WIZ_ROW_W, UPD_WIZ_ROW_H);
}

gfx_rect_t upd_wiz_card_rect(void)
{
    return gfx_rect_make(UPD_WIZ_CARD_X, UPD_WIZ_CARD_Y, UPD_WIZ_CARD_W,
                         UPD_WIZ_CARD_H);
}

gfx_rect_t upd_wiz_act_rect(void)
{
    return gfx_rect_make(UPD_WIZ_CARD_X + UPD_WIZ_PAD, UPD_WIZ_ACT_Y,
                         UPD_WIZ_CARD_W - 2 * UPD_WIZ_PAD, UPD_WIZ_ACT_H);
}

gfx_rect_t upd_wiz_skip_rect(void)
{
    return gfx_rect_make(UPD_WIZ_CARD_X + UPD_WIZ_PAD, UPD_WIZ_SKIP_Y,
                         UPD_WIZ_CARD_W - 2 * UPD_WIZ_PAD, UPD_WIZ_SKIP_H);
}

/** @brief CANCEL UPDATE, the footer's middle slot. @return its rectangle */
static gfx_rect_t cancel_rect(void) { return ui_footer_rect(UI_FOOTER_HOLD); }

/* ------------------------------------------------------------ the words */

const char *upd_wiz_name(unsigned i)
{
    static const ui_text_id_t k_name[UPD_WIZ_STEPS] = {
        TX_UPD_WIZ_N_PANEL, TX_UPD_WIZ_N_IOMCU, TX_UPD_WIZ_N_C6,
        TX_UPD_WIZ_N_DIAL,  TX_UPD_WIZ_N_PNODE, TX_UPD_WIZ_N_BAL,
    };
    return ui_tr(k_name[i < UPD_WIZ_STEPS ? i : 0u]);
}

/**
 * @brief A tag's word.
 * @param t the tag
 * @return the text
 */
static const char *tag_text(upd_wiz_tag_t t)
{
    static const ui_text_id_t k_tag[] = {
        [UPD_WIZ_TAG_NOT_CHECKED]   = TX_UPD_WIZ_T_NOT_CHECKED,
        [UPD_WIZ_TAG_CURRENT]       = TX_UPD_WIZ_T_CURRENT,
        [UPD_WIZ_TAG_NEXT]          = TX_UPD_WIZ_T_NEXT,
        [UPD_WIZ_TAG_UPDATING]      = TX_UPD_WIZ_T_UPDATING,
        [UPD_WIZ_TAG_CHECKING]      = TX_UPD_WIZ_T_CHECKING,
        [UPD_WIZ_TAG_FAILED]        = TX_UPD_WIZ_T_FAILED,
        [UPD_WIZ_TAG_SKIPPED]       = TX_UPD_WIZ_T_SKIPPED,
        [UPD_WIZ_TAG_LATER]         = TX_UPD_WIZ_T_LATER,
        [UPD_WIZ_TAG_NO_IMAGE]      = TX_UPD_WIZ_T_NO_IMAGE,
        [UPD_WIZ_TAG_BAD_IMAGE]     = TX_UPD_WIZ_T_BAD_IMAGE,
        [UPD_WIZ_TAG_NO_LINK]       = TX_UPD_WIZ_T_NO_LINK,
        [UPD_WIZ_TAG_CHECK_FAILED]  = TX_UPD_WIZ_T_CHECK_FAILED,
        [UPD_WIZ_TAG_USB_ONLY]      = TX_UPD_WIZ_T_USB_ONLY,
        [UPD_WIZ_TAG_LINK_FAILED]   = TX_UPD_WIZ_T_LINK_FAILED,
        [UPD_WIZ_TAG_NOT_REPORTED]  = TX_UPD_WIZ_T_NOT_REPORTED,
        [UPD_WIZ_TAG_NOT_CONNECTED] = TX_UPD_WIZ_T_NOT_CONNECTED,
        [UPD_WIZ_TAG_NOT_BOUND]     = TX_UPD_WIZ_T_NOT_BOUND,
        [UPD_WIZ_TAG_OFF]           = TX_UPD_WIZ_T_OFF,
    };
    const size_t n = sizeof(k_tag) / sizeof(k_tag[0]);
    return ui_tr(k_tag[(size_t)t < n ? (size_t)t : 0u]);
}

/**
 * @brief A tag's colour: done in the OK colour, the current step's in
 *        the accent, a failure in the warning colour, the rest dim.
 * @param st      the step
 * @param current whether it is the current step
 * @return the colour
 */
static gfx_color_t tag_colour(const upd_wiz_step_t *st, bool current)
{
    switch (st->tag) {
    case UPD_WIZ_TAG_CURRENT:  return UI_OK;
    case UPD_WIZ_TAG_FAILED:   return UI_WARN;
    case UPD_WIZ_TAG_NEXT:
    case UPD_WIZ_TAG_UPDATING:
    case UPD_WIZ_TAG_CHECKING: return current ? UI_ACCENT : UI_TEXT;
    default:                   return current ? UI_TEXT : UI_TEXT_DIM;
    }
}

/**
 * @brief The device a step updates.
 * @param step the step, UPD_WIZ_DIAL to UPD_WIZ_BAL
 * @return its row
 */
static upd_dev_id_t dev_of(unsigned step)
{
    return (upd_dev_id_t)(step - (unsigned)UPD_WIZ_DIAL);
}

void upd_wiz_act_label(const upd_wiz_t *w, char *out, size_t n)
{
    switch (w->act) {
    case UPD_WIZ_ACT_DOWNLOAD:
        (void)snprintf(out, n, "%s", TR(UPD_HOLD_DL));
        break;
    case UPD_WIZ_ACT_IOMCU:
        (void)snprintf(out, n, "%s", TR(UPD_CU_HOLD));
        break;
    case UPD_WIZ_ACT_C6:
        upd_c6_label(upd_c6_view(), out, n);
        break;
    case UPD_WIZ_ACT_DIAL:
    case UPD_WIZ_ACT_PNODE:
    case UPD_WIZ_ACT_BAL:
        upd_dev_label(dev_of(w->current), upd_dev_view(dev_of(w->current)),
                      out, n);
        break;
    case UPD_WIZ_ACT_NONE:
    default:
        if (n > 0u) {
            out[0] = '\0';
        }
        break;
    }
}

void upd_wiz_why_text(const upd_wiz_t *w, char *out, size_t n)
{
    if (n == 0u) {
        return;
    }
    out[0] = '\0';
    if (w->current >= UPD_WIZ_STEPS) {
        return;
    }
    const char *t = NULL;
    switch (w->current) {
    case UPD_WIZ_PANEL: {
        const upd_view_t *v = upd_screen_view();
        if (!v->have_net) {
            t = TR(UPD_KEEP_NET);
            break;
        }
        char a[96], b[96];
        upd_blocked_text(v->dl_block, a, sizeof(a), b, sizeof(b));
        (void)snprintf(out, n, "%s%s%s", a, b[0] != '\0' ? " " : "", b);
        return;
    }
    case UPD_WIZ_IOMCU: {
        gfx_color_t col = UI_TEXT;
        upd_cu_text(upd_screen_coproc(), &t, &col);
        break;
    }
    case UPD_WIZ_C6:
        t = upd_c6_reason(upd_c6_view());
        break;
    default:
        t = upd_dev_reason(upd_dev_view(dev_of(w->current)));
        break;
    }
    (void)snprintf(out, n, "%s", t != NULL ? t : "");
}

/* ------------------------------------------------------------ the press */

bool upd_wiz_pressed(void) { return z.have; }

bool upd_wiz_holding(void) { return z.have && z.hit == WP_ACT; }

/** @brief The press ends; a hold on it ends unfired. */
static void drop_press(void)
{
    if (z.have && z.hit == WP_ACT) {
        (void)ui_hold_left(&z.hold);
    }
    z.have = false;
    z.hit = WP_NONE;
}

void upd_wiz_drop(void)
{
    drop_press();
    ui_hold_reset(&z.hold);
    upd_screen_invalidate();
}

void upd_wiz_drop_step(unsigned step)
{
    if (z.have && z.hit == WP_ACT && z.step == step) {
        drop_press();
        ui_hold_reset(&z.hold);
        upd_screen_invalidate();
    }
}

void upd_wiz_unskip(unsigned step)
{
    if (step < UPD_WIZ_STEPS && (z.skip & UPD_WIZ_BIT(step)) != 0u) {
        z.skip &= ~UPD_WIZ_BIT(step);
        upd_screen_invalidate();
    }
}

void upd_wiz_reset(void)
{
    memset(&z, 0, sizeof(z));
    ui_hold_reset(&z.hold);
}

/**
 * @brief The hold's hint for a control.
 * @param a the control
 * @return the hint's text ID
 */
static int hint_of(upd_wiz_act_t a)
{
    switch (a) {
    case UPD_WIZ_ACT_IOMCU: return TX_HOLD_HINT_IOMCU_UPDATE;
    case UPD_WIZ_ACT_C6:    return TX_HOLD_HINT_C6_UPDATE;
    case UPD_WIZ_ACT_DIAL:  return TX_HOLD_HINT_DIAL_UPDATE;
    case UPD_WIZ_ACT_PNODE: return TX_HOLD_HINT_PNODE_UPDATE;
    case UPD_WIZ_ACT_BAL:   return TX_HOLD_HINT_BAL_UPDATE;
    case UPD_WIZ_ACT_DOWNLOAD:
    case UPD_WIZ_ACT_NONE:
    default:                return TX_HOLD_HINT_DOWNLOAD;
    }
}

/**
 * @brief Whether CANCEL UPDATE ends the running device's transfer now:
 *        before END.
 * @param w the steps
 * @return true then
 */
static bool cancel_live(const upd_wiz_t *w)
{
    return w->cancel && w->current >= UPD_WIZ_DIAL
           && w->current < UPD_WIZ_STEPS
           && upd_dev_view(dev_of(w->current))->phase < DEVUPD_END;
}

/**
 * @brief The control of the steps at a point.
 * @param w the steps
 * @param x body x
 * @param y body y
 * @return a WP_ value
 */
static int hit_at(const upd_wiz_t *w, int x, int y)
{
    if (gfx_rect_contains(upd_wiz_act_rect(), x, y)) {
        if (w->act != UPD_WIZ_ACT_NONE) {
            if (w->act_live) {
                return WP_ACT;
            }
            return w->step[w->current].state == UPD_WIZ_HELD ? WP_WHY
                                                               : WP_NONE;
        }
        return upd_wiz_net_shown(w) ? WP_NET : WP_NONE;
    }
    if (gfx_rect_contains(upd_wiz_skip_rect(), x, y)) {
        return w->can_skip ? WP_SKIP : w->show_skipped ? WP_SHOW : WP_NONE;
    }
    if (w->cancel && gfx_rect_contains(cancel_rect(), x, y)) {
        return WP_CANCEL;
    }
    return WP_NONE;
}

/**
 * @brief A hold that completed sets the latch its step's own control on
 *        SINGLE UPDATES sets; the services drain it as theirs.
 * @param a the control
 */
static void fire(upd_wiz_act_t a)
{
    switch (a) {
    case UPD_WIZ_ACT_DOWNLOAD: upd_screen_ask(UPD_REQ_DOWNLOAD); break;
    case UPD_WIZ_ACT_IOMCU:    upd_single_fire_coproc();         break;
    case UPD_WIZ_ACT_C6:       upd_c6_fire();                    break;
    case UPD_WIZ_ACT_DIAL:     upd_dev_fire(UPD_DEV_DIAL);       break;
    case UPD_WIZ_ACT_PNODE:    upd_dev_fire(UPD_DEV_PNODE);      break;
    case UPD_WIZ_ACT_BAL:      upd_dev_fire(UPD_DEV_BAL);        break;
    case UPD_WIZ_ACT_NONE:
    default:                                                     break;
    }
}

/**
 * @brief Acts on a tap that ended where it began.
 * @param w   the steps
 * @param hit what was tapped
 */
static void act(const upd_wiz_t *w, int hit)
{
    char why[UI_ALERT_MAX];
    switch (hit) {
    case WP_WHY:
        upd_wiz_why_text(w, why, sizeof(why));
        ui_router_set_notice(why);
        break;
    case WP_SKIP:
        /* The step the press began on, still offered: a step that moved
         * on under the finger is not passed. */
        if (w->can_skip && w->current == z.step) {
            z.skip |= UPD_WIZ_BIT(w->current);
            z.skip_cu = z.skip_cu || w->current == UPD_WIZ_IOMCU;
        }
        break;
    case WP_SHOW:
        z.skip = 0u;
        break;
    case WP_NET:
        /* WI-FI's home tag comes back here (ui_router_parent()). */
        ui_router_goto(SCREEN_WIFI);
        break;
    case WP_CANCEL:
        if (cancel_live(w)) {
            upd_dev_fire_cancel();
        } else {
            ui_router_set_notice(TR(DEV_WHY_INSTALLING));
        }
        break;
    default:
        break;
    }
}

bool upd_wiz_event(const touch_event_t *evt, bool other)
{
    if (evt == NULL) {
        return false;
    }
    const upd_wiz_t *w = now();
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (z.have) {
            return true;           /* one press at a time */
        }
        const int hit = other ? WP_NONE : hit_at(w, x, y);
        if (hit == WP_NONE) {
            return false;
        }
        z.have = true;
        z.hit = hit;
        z.id = evt->point.id;
        z.step = w->current;
        z.act = w->act;
        if (hit == WP_ACT) {
            ui_hold_begin(&z.hold, hint_of(w->act));
        }
        upd_screen_invalidate();
        return true;
    }
    if (!z.have || evt->point.id != z.id) {
        return false;
    }
    const gfx_rect_t r = z.hit == WP_ACT || z.hit == WP_WHY || z.hit == WP_NET
                             ? upd_wiz_act_rect()
                         : z.hit == WP_CANCEL ? cancel_rect()
                                              : upd_wiz_skip_rect();
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off abandons the press, as on every held control. */
        if (!gfx_rect_contains(r, x, y)) {
            drop_press();
            upd_screen_invalidate();
        }
        return true;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    const int hit = z.hit;
    z.have = false;
    z.hit = WP_NONE;
    upd_screen_invalidate();
    if (hit == WP_ACT) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&z.hold, evt, r);
        return true;
    }
    /* The control is asked again at the release.  A press on the grey
     * hold says why however long it lasted; every other control acts on a
     * tap. */
    if (hit_at(w, x, y) == hit
        && (hit == WP_WHY || touch_event_is_tap_up(evt))) {
        act(w, hit);
    }
    return true;
}

bool upd_wiz_tick(float dt_s)
{
    const upd_wiz_t *w = now();
    if (z.have) {
        /* A control the steps no longer offer ends its press unfired: the
         * step moved on, its gate closed, a newer release came in. */
        const bool gone =
            (z.hit == WP_ACT && (w->current != z.step || w->act != z.act
                                 || !w->act_live))
            || (z.hit == WP_WHY && (w->current != z.step || w->act != z.act))
            || (z.hit == WP_SKIP && (w->current != z.step || !w->can_skip))
            || (z.hit == WP_SHOW && !w->show_skipped)
            || (z.hit == WP_NET && !upd_wiz_net_shown(w))
            || (z.hit == WP_CANCEL && !w->cancel);
        if (gone) {
            drop_press();
            upd_screen_invalidate();
        }
    }
    const bool moving = z.hold.down || z.hold.flash_ms > 0;
    ui_hold_flash_step(&z.hold, dt_s);
    if (z.have && z.hit == WP_ACT && ui_hold_tick(&z.hold, dt_s)) {
        fire(z.act);
        ui_hold_reached(&z.hold);
    }
    return moving;
}

/* ------------------------------------------------------------- drawing */

/**
 * @brief One step's row: the number box, the name, the version, the tag.
 * @param c canvas, the body
 * @param w the steps
 * @param i the step
 */
static void row_render(gfx_canvas_t *c, const upd_wiz_t *w, unsigned i)
{
    const upd_wiz_step_t *st = &w->step[i];
    const bool current = i == w->current;
    const gfx_rect_t r = upd_wiz_row_rect(i);
    ui_card(c, r, UI_PANEL);
    if (current) {
        gfx_draw_rect(c, r.x, r.y, r.w, r.h, UI_ACCENT);
        gfx_draw_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, UI_ACCENT);
    }
    const gfx_color_t box = current ? UI_ACCENT
                            : st->tag == UPD_WIZ_TAG_CURRENT ? UI_OK
                                                             : UI_PANEL_HI;
    gfx_fill_rect(c, r.x, r.y, UPD_WIZ_BOX, UPD_WIZ_BOX, box);
    char num[4];
    (void)snprintf(num, sizeof(num), "%u", i + 1u);
    const int fh = gfx_text_height(UPD_FONT_BODY, 1);
    gfx_text_in(c, gfx_rect_make(r.x, r.y + (UPD_WIZ_BOX - fh) / 2,
                                 UPD_WIZ_BOX, fh),
                num, UPD_FONT_BODY,
                ui_is_light(box) ? UI_TEXT_ON_LIGHT : UI_TEXT, 1,
                GFX_ALIGN_CENTER);
    const int h = gfx_text_height(WIZ_FONT, 1);
    const int ty = r.y + (r.h - h) / 2;
    const bool lit = current || st->tag == UPD_WIZ_TAG_CURRENT;
    gfx_text_in(c, gfx_rect_make(UPD_WIZ_NAME_X, ty, UPD_WIZ_NAME_W, h),
                upd_wiz_name(i), WIZ_FONT, lit ? UI_TEXT : UI_TEXT_DIM, 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(c, gfx_rect_make(UPD_WIZ_VER_X, ty, UPD_WIZ_VER_W, h),
                st->ver, WIZ_FONT, st->older ? UI_WARN : UI_TEXT_DIM, 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(c, gfx_rect_make(r.x + r.w - 8 - UPD_WIZ_TAG_W, ty,
                                 UPD_WIZ_TAG_W, h),
                tag_text(st->tag), WIZ_FONT, tag_colour(st, current), 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief The card's controls: the current step's, SKIP or SHOW SKIPPED
 *        STEPS, and the footer's CANCEL UPDATE.
 * @param c canvas, the body
 * @param w the steps
 */
static void controls_render(gfx_canvas_t *c, const upd_wiz_t *w)
{
    const gfx_rect_t a = upd_wiz_act_rect();
    if (w->act != UPD_WIZ_ACT_NONE) {
        char label[96];
        upd_wiz_act_label(w, label, sizeof(label));
        if (w->step[w->current].state == UPD_WIZ_RUNNING) {
            /* The running update's phase, bright: the device's ask. */
            ui_button_font(c, a, label, UI_PANEL_HI, false, true, WIZ_FONT);
        } else {
            ui_hold_button(c, a, label, w->act_live ? UI_ACCENT : UI_PANEL_HI,
                           UI_OK, &z.hold,
                           z.have && z.hit == WP_WHY, w->act_live, WIZ_FONT);
        }
    } else if (upd_wiz_net_shown(w)) {
        upd_button(c, a, TR(UPD_OPEN_WIFI), UI_PANEL_HI,
                   z.have && z.hit == WP_NET, true);
    } else {
        ui_hold_ghost(c, &z.hold);  /* a hold that fired as its step went */
    }
    if (w->can_skip || w->show_skipped) {
        upd_button(c, upd_wiz_skip_rect(),
                   w->can_skip ? TR(UPD_WIZ_SKIP) : TR(UPD_WIZ_SHOW_SKIPPED),
                   UI_PANEL_HI,
                   z.have && (z.hit == WP_SKIP || z.hit == WP_SHOW), true);
    }
    if (w->cancel) {
        upd_button(c, cancel_rect(), TR(UPD_DEVS_CANCEL), UI_PANEL_HI,
                   z.have && z.hit == WP_CANCEL, cancel_live(w));
    }
}

bool upd_wiz_render(gfx_canvas_t *c)
{
    const upd_wiz_t *w = now();
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        row_render(c, w, i);
    }
    upd_wiz_card_render(c, w);
    controls_render(c, w);
    return z.hold.flash_ms > 0;
}
