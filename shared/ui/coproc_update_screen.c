/*
 * coproc_update_screen.h says what this screen is for.
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

/* The question, in the body under the band: the box and the buttons of
 * LOGS' DELETE question. */
#define Q_BOX    gfx_rect_make(120, 110, 560, 220)
#define Q_CANCEL gfx_rect_make(144, 258, 240, 48)
#define Q_START  gfx_rect_make(416, 258, 240, 48)

/* The progress, on the whole panel. */
#define W        800
#define BAR_H    56
#define COL_X    24
#define STEP_Y   (BAR_H + 16)
#define FROM_Y   (STEP_Y + 38)
#define PBAR     gfx_rect_make(COL_X, 170, W - 2 * COL_X, 28)
#define COUNT_Y  210
#define NOTE_Y   280

static struct {
    cu_view_t    view;
    cu_version_t running;
    cu_version_t carried;

    cu_step_t step;
    uint16_t  block;
    uint16_t  blocks;

    ui_hold_t hold;          /**< START                                    */
    int       press_btn;     /**< 0 CANCEL, 1 START, -1 none               */
    bool      have_press;
    uint8_t   press_id;

    bool      start;         /**< latched for the application             */
    bool      cancel;

    unsigned  drawn_mask;    /**< the static half, per framebuffer         */
} s = { .press_btn = -1 };

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
    ui_hold_flash_step(&s.hold);
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

static void render_question(gfx_canvas_t *c, unsigned bit)
{
    const gfx_rect_t box = Q_BOX;
    if ((s.drawn_mask & bit) == 0u) {
        gfx_clear(c, UI_BG);
        ui_panel(c, box, TR(CU_TITLE), UI_ACCENT);

        char run[20], car[20], line[112];
        fmt_version(run, sizeof(run), s.running);
        fmt_version(car, sizeof(car), s.carried);
        snprintf(line, sizeof(line), TR(CU_Q_VERSIONS), run, car);
        gfx_text(c, box.x + 24, box.y + 44, line, UI_FONT_LABEL, UI_TEXT, 1);

        /* An older image is said in its own colour: it is the way back, and
         * a step back taken by mistake costs a second update. */
        if (coproc_update_older(s.running, s.carried)) {
            gfx_text(c, box.x + 24, box.y + 70, TR(CU_Q_OLDER),
                     UI_FONT_LABEL, UI_WARN, 1);
        } else if (same_version()) {
            gfx_text(c, box.x + 24, box.y + 70, TR(CU_Q_SAME),
                     UI_FONT_LABEL, UI_TEXT_DIM, 1);
        }
        gfx_text(c, box.x + 24, box.y + 98, TR(CU_Q_WHAT),
                 UI_FONT_LABEL, UI_TEXT_DIM, 1);
        s.drawn_mask |= bit;
    }

    /* The buttons every frame: START fades across its hold. */
    ui_button(c, Q_CANCEL, TR(CANCEL), UI_PANEL_HI, s.press_btn == 0, true);
    const gfx_color_t fill =
        (s.hold.flash_left > 0)
            ? ui_hold_flash(UI_ACCENT, s.hold.flash_left)
            : ui_hold_fill(UI_PANEL_HI, UI_ACCENT, s.hold.held_s);
    ui_button(c, Q_START, TR(CU_START), fill, false, true);
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

static void render_progress(gfx_canvas_t *c, unsigned bit)
{
    if ((s.drawn_mask & bit) == 0u) {
        gfx_clear(c, UI_BG);
        gfx_fill_rect(c, 0, 0, W, BAR_H, UI_ACCENT);
        gfx_text(c, COL_X, 14, TR(CU_TITLE), UI_FONT_HEAD,
                 ui_is_light(UI_ACCENT) ? UI_TEXT_ON_LIGHT : UI_TEXT, 1);
        gfx_text(c, COL_X, STEP_Y, step_text(s.step), UI_FONT_HEAD, UI_TEXT,
                 1);

        char run[20], car[20], line[112];
        fmt_version(run, sizeof(run), s.running);
        fmt_version(car, sizeof(car), s.carried);
        snprintf(line, sizeof(line), TR(CU_P_FROM_TO), run, car);
        gfx_text(c, COL_X, FROM_Y, line, UI_FONT_LABEL, UI_TEXT_DIM, 1);

        gfx_text(c, COL_X, NOTE_Y, TR(CU_P_NOTE_OFF), UI_FONT_LABEL,
                 UI_TEXT, 1);
        gfx_text(c, COL_X, NOTE_Y + 24, TR(CU_P_NOTE_POWER), UI_FONT_LABEL,
                 UI_TEXT_DIM, 1);
        gfx_text(c, COL_X, NOTE_Y + 48, TR(CU_P_NOTE_PANEL), UI_FONT_LABEL,
                 UI_TEXT_DIM, 1);
        s.drawn_mask |= bit;
    }

    /* The bar and its count every frame; the count is repainted on its
     * own background so a shorter number leaves no digits behind. */
    const float frac = (s.blocks == 0u) ? 0.0f
                                        : (float)s.block / (float)s.blocks;
    ui_bar(c, PBAR, frac, -1.0f, UI_ACCENT);
    gfx_fill_rect(c, COL_X, COUNT_Y, W - 2 * COL_X, 20, UI_BG);
    if (s.blocks != 0u) {
        char count[64];
        snprintf(count, sizeof(count), TR(CU_P_BLOCKS), (unsigned)s.block,
                 (unsigned)s.blocks,
                 (unsigned)((uint32_t)s.block * 100u / s.blocks));
        gfx_text(c, COL_X, COUNT_Y, count, UI_FONT_LABEL, UI_TEXT, 1);
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

/* Where a progress that has ended goes. */
static void progress_done(cu_driver_t *d)
{
    ui_router_goto(d->from_updates ? SCREEN_UPDATES : SCREEN_OVERVIEW);
    d->from_updates = false;
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
