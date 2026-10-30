/**
 * @file
 * @brief coproc_update_screen.h says what this screen is for.
 *
 * SPDX-License-Identifier: MIT
 */

#include "coproc_update_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"

/*
 * The faces of the type scale this screen draws in:
 * CU_FONT_HEAD (20 x 36) for the title and the step, CU_FONT_BODY (12 x 24)
 * for every line read at a distance, CU_FONT_LABEL (10 x 20) for the
 * buttons.
 */
/** @brief The title and the step: 20 x 36. */
#define CU_FONT_HEAD  (&gfx_font_20x36)
/** @brief The lines read at a distance: 12 x 24. */
#define CU_FONT_BODY  UI_FONT_BODY
/** @brief The buttons: 10 x 20. */
#define CU_FONT_LABEL (&gfx_font_10x20)

/*
 * The question, in the body under the band:
 * A box 860 x 248px, its lines in CU_FONT_BODY 36px apart from 44px
 * under its top edge, CANCEL and START side by side under them, 398 x 56px
 * each, 16px apart.  START's foot is body y 344, 152px above the alert
 * band.  On the panel CANCEL's middle is (305, 376) and START's (719, 376).
 */
/** @brief The question's box, in the body. */
#define Q_BOX    gfx_rect_make(82, 120, 860, 248)
#define Q_TEXT_X (82 + 24)          /**< the lines' left edge */
#define Q_TEXT_W (860 - 48)         /**< 67 cells of CU_FONT_BODY */
/** @brief CANCEL, in the body. */
#define Q_CANCEL gfx_rect_make(106, 288, 398, 56)
/** @brief START, in the body. */
#define Q_START  gfx_rect_make(520, 288, 398, 56)

/*
 * The progress, on the whole panel:
 * A 64px title bar; the step in CU_FONT_HEAD and the versions under it;
 * the bar 36px tall across the panel less 24px each side; the block count
 * under it; the three notes in CU_FONT_BODY, 28px apart.
 */
/** @brief The panel's width, px. */
#define W        UI_PANEL_W
/** @brief The progress's title bar's height, px. */
#define BAR_H    64
/** @brief The progress's left edge. */
#define COL_X    24
#define COL_W    (W - 2 * COL_X)    /**< 976px: 81 cells of CU_FONT_BODY */
#define PITCH    28                 /**< a CU_FONT_BODY line's pitch */
/** @brief The step's top. */
#define STEP_Y   (BAR_H + 20)
/** @brief The versions' top. */
#define FROM_Y   (STEP_Y + 48)
/** @brief The progress bar, 36px tall. */
#define PBAR     gfx_rect_make(COL_X, 180, COL_W, 36)
/** @brief The block count's top. */
#define COUNT_Y  228
/** @brief The first note's top. */
#define NOTE_Y   288

_Static_assert(56 >= UI_TAP_MIN, "CANCEL and START are a finger tall");
_Static_assert(288 + 56 + 4 <= UI_ALERT_Y - UI_BAND_H,
               "the question's buttons end above the alert band");

static struct {
    cu_view_t    view;
    cu_version_t running;
    cu_version_t carried;

    cu_step_t step;
    uint16_t  block;
    uint16_t  blocks;

    ui_hold_t hold;          /**< START                                    */
    int       press_btn;     /**< 0 CANCEL, 1 START, -1 none               */
    bool      have_press;    /**< a contact owns the press               */
    uint8_t   press_id;      /**< the contact that owns it               */

    bool      start;         /**< latched for the application             */
    bool      cancel;        /**< latched for the application             */

    unsigned  drawn_mask;    /**< the static half, per framebuffer         */
} s = { .press_btn = -1 };  /**< the question's or the progress's state */

void coproc_update_screen_invalidate(void) { s.drawn_mask = 0; }

bool coproc_update_older(cu_version_t running, cu_version_t carried)
{
    if (carried.major != running.major) {
        return carried.major < running.major;
    }
    if (carried.minor != running.minor) {
        return carried.minor < running.minor;
    }
    return carried.patch < running.patch;
}

static bool same_version(void)
{
    return s.running.major == s.carried.major
           && s.running.minor == s.carried.minor
           && s.running.patch == s.carried.patch;
}

static void drop_press(void)
{
    s.have_press = false;
    s.press_btn = -1;
    (void)ui_hold_end(&s.hold);
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.press_btn = -1;
    ui_hold_reset(&s.hold);
}

void coproc_update_screen_offer(cu_version_t running, cu_version_t carried)
{
    s.view = CU_VIEW_QUESTION;
    s.running = running;
    s.carried = carried;
    s.step = CU_STEP_PREPARE;
    s.block = 0u;
    s.blocks = 0u;
    s.start = false;
    s.cancel = false;
    drop_press();
    ui_hold_reset(&s.hold);
    s.drawn_mask = 0;
}

void coproc_update_screen_progress(cu_step_t step, uint16_t block,
                                   uint16_t blocks)
{
    if (s.view != CU_VIEW_PROGRESS || step != s.step) {
        s.drawn_mask = 0;
    }
    s.view = CU_VIEW_PROGRESS;
    s.step = step;
    s.block = (block > blocks) ? blocks : block;
    s.blocks = blocks;
}

cu_view_t coproc_update_screen_view(void) { return s.view; }

bool coproc_update_screen_take_start(void)
{
    const bool was = s.start;
    s.start = false;
    return was;
}

bool coproc_update_screen_take_cancel(void)
{
    const bool was = s.cancel;
    s.cancel = false;
    return was;
}

/* ------------------------------------------------------------------ input */

static int button_at(int16_t x, int16_t y)
{
    if (gfx_rect_contains(Q_CANCEL, x, y)) {
        return 0;
    }
    if (gfx_rect_contains(Q_START, x, y)) {
        return 1;
    }
    return -1;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL || s.view != CU_VIEW_QUESTION) {
        return;
    }
    const int16_t x = evt->point.x, y = evt->point.y;
    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        /* The first contact on a button owns the press. */
        if (!s.have_press) {
            s.press_btn = button_at(x, y);
            if (s.press_btn >= 0) {
                s.have_press = true;
                s.press_id = evt->point.id;
                if (s.press_btn == 1) {
                    ui_hold_begin(&s.hold);
                }
            }
        }
        break;
    case TOUCH_EVENT_MOVE:
        /* A finger that slides off START abandons the hold, as on every
         * held control. */
        if (s.have_press && evt->point.id == s.press_id
            && s.press_btn == 1 && button_at(x, y) != 1) {
            (void)ui_hold_leave(&s.hold);
        }
        break;
    case TOUCH_EVENT_UP:
    default:
        if (!s.have_press || evt->point.id != s.press_id) {
            break;
        }
        /* CANCEL is a tap: pressed and released on it by the same
         * contact.  START asked for nothing unless its hold completed. */
        if (s.press_btn == 0 && (evt->flags & TOUCH_FLAG_NO_TAP) == 0u
            && button_at(x, y) == 0) {
            s.cancel = true;
        }
        drop_press();
        break;
    }
}

static void tick(float dt_s)
{
    if (s.view != CU_VIEW_QUESTION) {
        return;
    }
    if (ui_hold_tick(&s.hold, dt_s)) {
        /* The hold is the gesture: it completes here, not on the release. */
        ui_hold_reached(&s.hold);
        s.start = true;
        s.view = CU_VIEW_PROGRESS;
        s.step = CU_STEP_PREPARE;
        s.drawn_mask = 0;
    }
    ui_hold_flash_step(&s.hold, dt_s);
}

/* Leaving the question any other way than START declines the offer. */
static void leave(void)
{
    if (s.view == CU_VIEW_QUESTION && !s.start) {
        s.cancel = true;
    }
    drop_press();
}

static void enter(void)
{
    s.drawn_mask = 0;
}

static void cancel(void)
{
    drop_press();
}

/* ---------------------------------------------------------------- drawing */

static void fmt_version(char *out, size_t n, cu_version_t v)
{
    snprintf(out, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
             (unsigned)v.patch);
}

/**
 * @brief The question: the box and its lines once per framebuffer, the two
 *        buttons every frame.
 * @param c   Canvas, the body under the band.
 * @param bit The framebuffer's bit in the drawn mask.
 */
static void render_question(gfx_canvas_t *c, unsigned bit)
{
    const gfx_rect_t box = Q_BOX;
    const int lh = gfx_text_height(CU_FONT_BODY, 1);
    if ((s.drawn_mask & bit) == 0u) {
        gfx_clear(c, UI_BG);
        ui_panel(c, box, TR(CU_TITLE), UI_ACCENT);

        char run[20], car[20], line[112];
        fmt_version(run, sizeof(run), s.running);
        fmt_version(car, sizeof(car), s.carried);
        snprintf(line, sizeof(line), TR(CU_Q_VERSIONS), run, car);
        gfx_text_in(c, gfx_rect_make(Q_TEXT_X, box.y + 44, Q_TEXT_W, lh),
                    line, CU_FONT_BODY, UI_TEXT, 1, GFX_ALIGN_LEFT);

        /* An older image is said in its own colour: it is the way back, and
         * a step back taken by mistake costs a second update. */
        const gfx_rect_t second =
            gfx_rect_make(Q_TEXT_X, box.y + 80, Q_TEXT_W, lh);
        if (coproc_update_older(s.running, s.carried)) {
            gfx_text_in(c, second, TR(CU_Q_OLDER), CU_FONT_BODY, UI_WARN, 1,
                        GFX_ALIGN_LEFT);
        } else if (same_version()) {
            gfx_text_in(c, second, TR(CU_Q_SAME), CU_FONT_BODY, UI_TEXT_DIM,
                        1, GFX_ALIGN_LEFT);
        }
        gfx_text_in(c, gfx_rect_make(Q_TEXT_X, box.y + 116, Q_TEXT_W, lh),
                    TR(CU_Q_WHAT), CU_FONT_BODY, UI_TEXT_DIM, 1,
                    GFX_ALIGN_LEFT);
        s.drawn_mask |= bit;
    }

    /* The buttons every frame: START fades across its hold. */
    ui_button_font(c, Q_CANCEL, TR(CANCEL), UI_PANEL_HI, s.press_btn == 0,
                   true, CU_FONT_LABEL);
    const gfx_color_t fill =
        (s.hold.flash_ms > 0)
            ? ui_hold_flash(UI_ACCENT, s.hold.flash_ms)
            : ui_hold_fill(UI_PANEL_HI, UI_ACCENT, s.hold.held_s);
    ui_button_font(c, Q_START, TR(CU_START), fill, false, true,
                   CU_FONT_LABEL);
}

static const char *step_text(cu_step_t step)
{
    switch (step) {
    case CU_STEP_SEND:    return TR(CU_P_SEND);
    case CU_STEP_VERIFY:  return TR(CU_P_VERIFY);
    case CU_STEP_RESTART: return TR(CU_P_RESTART);
    case CU_STEP_BUY:     return TR(CU_P_BUY);
    case CU_STEP_PREPARE:
    default:              return TR(CU_P_PREPARE);
    }
}

/**
 * @brief The progress on the whole panel: the bar, the step and the notes
 *        once per framebuffer, the bar and the block count every frame.
 * @param c   Canvas, the whole panel.
 * @param bit The framebuffer's bit in the drawn mask.
 */
static void render_progress(gfx_canvas_t *c, unsigned bit)
{
    const int lh = gfx_text_height(CU_FONT_BODY, 1);
    if ((s.drawn_mask & bit) == 0u) {
        gfx_clear(c, UI_BG);
        gfx_fill_rect(c, 0, 0, W, BAR_H, UI_ACCENT);
        const int hh = gfx_text_height(CU_FONT_HEAD, 1);
        gfx_text_in(c, gfx_rect_make(COL_X, (BAR_H - hh) / 2, COL_W, hh),
                    TR(CU_TITLE), CU_FONT_HEAD,
                    ui_is_light(UI_ACCENT) ? UI_TEXT_ON_LIGHT : UI_TEXT, 1,
                    GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(COL_X, STEP_Y, COL_W, hh),
                    step_text(s.step), CU_FONT_HEAD, UI_TEXT, 1,
                    GFX_ALIGN_LEFT);

        char run[20], car[20], line[112];
        fmt_version(run, sizeof(run), s.running);
        fmt_version(car, sizeof(car), s.carried);
        snprintf(line, sizeof(line), TR(CU_P_FROM_TO), run, car);
        gfx_text_in(c, gfx_rect_make(COL_X, FROM_Y, COL_W, lh), line,
                    CU_FONT_BODY, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);

        /* The notes, each as many lines as it takes at the pitch. */
        int y = NOTE_Y;
        y += PITCH * ui_text_wrap(c, gfx_rect_make(COL_X, y, COL_W, lh),
                                  PITCH, TR(CU_P_NOTE_OFF), CU_FONT_BODY,
                                  UI_TEXT);
        y += PITCH * ui_text_wrap(c, gfx_rect_make(COL_X, y, COL_W, lh),
                                  PITCH, TR(CU_P_NOTE_POWER), CU_FONT_BODY,
                                  UI_TEXT_DIM);
        (void)ui_text_wrap(c, gfx_rect_make(COL_X, y, COL_W, lh), PITCH,
                           TR(CU_P_NOTE_PANEL), CU_FONT_BODY, UI_TEXT_DIM);
        s.drawn_mask |= bit;
    }

    /* The bar and its count every frame; the count is repainted on its
     * own background so a shorter number leaves no digits behind. */
    const float frac = (s.blocks == 0u) ? 0.0f
                                        : (float)s.block / (float)s.blocks;
    ui_bar(c, PBAR, frac, -1.0f, UI_ACCENT);
    gfx_fill_rect(c, COL_X, COUNT_Y, COL_W, lh, UI_BG);
    if (s.blocks != 0u) {
        char count[64];
        snprintf(count, sizeof(count), TR(CU_P_BLOCKS), (unsigned)s.block,
                 (unsigned)s.blocks,
                 (unsigned)((uint32_t)s.block * 100u / s.blocks));
        gfx_text_in(c, gfx_rect_make(COL_X, COUNT_Y, COL_W, lh), count,
                    CU_FONT_BODY, UI_TEXT, 1, GFX_ALIGN_LEFT);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    /* A framebuffer index the mask has no bit for is redrawn whole. */
    const unsigned bit = (buffer_index >= 0 && buffer_index < 8)
                             ? 1u << buffer_index : 0u;
    if (s.view == CU_VIEW_PROGRESS) {
        render_progress(c, bit);
    } else {
        render_question(c, bit);
    }
}

/* ----------------------------------------------------------- the driver */

cu_sec_t coproc_update_section(const cu_driver_t *d, const cu_feed_t *f,
                               bool unsafe)
{
    if (f == NULL || !f->have_image) {
        return CU_SEC_NO_IMAGE;
    }
    if (f->active || f->check == CU_CHECK_UPDATING
        || (d != NULL && d->starts_sent != f->starts_taken)) {
        return CU_SEC_BUSY;
    }
    if (!f->reachable) {
        return CU_SEC_NO_LINK;
    }
    switch (f->check) {
    case CU_CHECK_NONE:      return CU_SEC_UNKNOWN;
    case CU_CHECK_RUNNING:   return CU_SEC_CHECKING;
    case CU_CHECK_FAILED:    return CU_SEC_FAILED;
    case CU_CHECK_USB:       return CU_SEC_USB;
    case CU_CHECK_NO_BOARD:  return CU_SEC_NO_BOARD;
    case CU_CHECK_SAME:      return CU_SEC_SAME;
    case CU_CHECK_DIFFERENT: break;
    case CU_CHECK_UPDATING:
    default:                 return CU_SEC_BUSY;
    }
    if (unsafe) {
        return CU_SEC_UNSAFE;
    }
    if (f->radio_up) {
        return CU_SEC_RADIO;
    }
    if (f->installing) {
        return CU_SEC_INSTALL;
    }
    return CU_SEC_READY;
}

/* UPDATES' hold, completed: the progress, and the START it hands over.
 * The same as the question's START, from another screen. */
static void start_from_updates(const cu_feed_t *f)
{
    s.running = f->running;
    s.carried = f->carried;
    s.view = CU_VIEW_PROGRESS;
    s.step = CU_STEP_PREPARE;
    s.block = 0u;
    s.blocks = 0u;
    s.start = true;
    s.cancel = false;
    drop_press();
    ui_hold_reset(&s.hold);
    s.drawn_mask = 0;
    ui_router_goto(SCREEN_COPROC_UPDATE);
}

/* Where a progress that has ended goes.  The origin stays until another
 * start sets it: a snapshot that shows the START taken a moment before it
 * shows the update active lands here, and the progress the next frame
 * opens again still returns to UPDATES. */
static void progress_done(const cu_driver_t *d)
{
    ui_router_goto(d->from_updates ? SCREEN_UPDATES : SCREEN_OVERVIEW);
}

void coproc_update_drive(cu_driver_t *d, const cu_feed_t *f, bool unsafe,
                         cu_hand_t *out)
{
    if (out != NULL) {
        out->starts = 0u;
        out->decline = false;
    }
    if (d == NULL || f == NULL || out == NULL) {
        return;
    }

    /* UPDATES' hold first: its START goes out with this frame's.  The
     * section is read again here, a frame after the hold completed, and a
     * hold the state no longer allows starts nothing. */
    if (upd_screen_take_coproc()
        && coproc_update_section(d, f, unsafe) == CU_SEC_READY
        && ui_router_current() == SCREEN_UPDATES) {
        d->from_updates = true;
        start_from_updates(f);
    }

    /* Both latches, wherever the router is: a way off the question other
     * than START latches CANCEL as the screen is left. */
    if (coproc_update_screen_take_start()) {
        ++d->starts_sent;
        ++out->starts;
    }
    if (coproc_update_screen_take_cancel() && f->offered) {
        out->decline = true;
        if (ui_router_current() == SCREEN_COPROC_UPDATE
            && s.view == CU_VIEW_QUESTION) {
            ui_router_goto(SCREEN_OVERVIEW);
            (void)coproc_update_screen_take_cancel();
        }
    }

    /* What UPDATES' section shows, after this frame's START is counted. */
    const upd_cu_view_t sec = {
        .sec        = coproc_update_section(d, f, unsafe),
        .known      = f->have_image && f->reachable
                      && f->check != CU_CHECK_NONE,
        .have_image = f->have_image,
        .running    = f->running,
        .carried    = f->carried,
    };
    upd_screen_set_coproc(&sec);

    if (f->active) {
        coproc_update_screen_progress(f->step, f->block, f->blocks);
        if (ui_router_current() != SCREEN_COPROC_UPDATE) {
            ui_router_goto(SCREEN_COPROC_UPDATE);
        }
        return;
    }

    if (ui_router_current() == SCREEN_COPROC_UPDATE) {
        if (s.view == CU_VIEW_PROGRESS) {
            /* Ended, or the START was spent on an offer that had gone. */
            if (f->starts_taken == d->starts_sent) {
                progress_done(d);
            }
        } else if (!f->offered) {
            /* The offer went -- another link-up checks again -- and with
             * it the question.  Leaving it declines nothing. */
            ui_router_goto(SCREEN_OVERVIEW);
            (void)coproc_update_screen_take_cancel();
        }
        return;
    }

    if (f->offered && f->offer_gen != d->shown_gen && !unsafe
        && !f->radio_up && !f->installing
        && ui_router_current() == SCREEN_OVERVIEW) {
        d->shown_gen = f->offer_gen;
        d->from_updates = false;
        coproc_update_screen_offer(f->running, f->carried);
        ui_router_goto(SCREEN_COPROC_UPDATE);
    }
}

static const ui_screen_t k_screen = {
    .title  = "UPDATE",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *coproc_update_screen(void) { return &k_screen; }
