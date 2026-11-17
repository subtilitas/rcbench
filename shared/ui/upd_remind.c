/**
 * @file upd_remind.c
 * @brief The start-up reminder (SCREEN_UPD_REMIND) and its driver, with
 *        the start-up check's: upd_screen.h says what they are for.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"
#include "upd_screen_int.h"

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
_Static_assert(8 + 480 <= UPD_FLOOR, "the reminder ends over the band");
_Static_assert(400 + 64 + 24 <= 8 + 480, "the hold ends inside the box");

/** @brief What a press on the reminder began on. */
enum {
    HIT_NONE = 0,
    HIT_DOWNLOAD,       /* HOLD 2s TO DOWNLOAD & INSTALL */
    HIT_IGNORE,         /* NOT NOW */
    HIT_NEVER,          /* NEVER REMIND ME */
};

/** @brief The reminder's press, hold and answer. */
typedef struct {
    int       hit;           /**< the control the press began on */
    uint8_t   press_id;      /**< the press's contact */
    bool      have_press;    /**< a press is on a control */
    ui_hold_t hold;          /**< the reminder's DOWNLOAD & INSTALL */
    bool      answered;      /**< NOT NOW, NEVER REMIND ME or the hold */
    bool      yield;         /**< the driver takes it down: not an answer */
    unsigned  drawn_mask;    /**< the buffers that hold the current frame */
} upd_rem_t;

/** @brief The reminder's one instance. */
static upd_rem_t rem;

void upd_remind_touched(void) { rem.drawn_mask = 0u; }

/** @brief Marks every update screen to be drawn again. */
static void touched(void) { upd_screen_invalidate(); }

void upd_remind_drop_if_gone(void)
{
    if (!upd_download_allowed() && rem.have_press && rem.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&rem.hold);
        rem.hit = HIT_NONE;
        rem.have_press = false;
    }
}

/** @brief Finds the control of the reminder at a point. */
static int r_hit_at(int x, int y)
{
    if (gfx_rect_contains(R_IGNORE, x, y)) {
        return HIT_IGNORE;
    }
    if (gfx_rect_contains(R_NEVER, x, y)) {
        return HIT_NEVER;
    }
    if (upd_download_allowed() && gfx_rect_contains(R_HOLD, x, y)) {
        return HIT_DOWNLOAD;
    }
    return HIT_NONE;
}

/** @brief Returns the reminder to its initial state. */
static void r_reset(void)
{
    memset(&rem, 0, sizeof(rem));
    ui_hold_reset(&rem.hold);
}

/** @brief Takes a touch event of the reminder. */
static void r_event(const touch_event_t *evt)
{
    if (evt == NULL || rem.answered) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (rem.have_press) {
            return;
        }
        rem.hit = r_hit_at(x, y);
        if (rem.hit != HIT_NONE) {
            rem.have_press = true;
            rem.press_id = evt->point.id;
            if (rem.hit == HIT_DOWNLOAD) {
                ui_hold_begin(&rem.hold, TX_HOLD_HINT_DOWNLOAD);
            }
        }
        touched();
        return;
    }
    if (!rem.have_press || evt->point.id != rem.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (rem.hit == HIT_DOWNLOAD && !gfx_rect_contains(R_HOLD, x, y)
            && ui_hold_leave(&rem.hold)) {
            rem.hit = HIT_NONE;
            rem.have_press = false;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = rem.hit;
    rem.hit = HIT_NONE;
    rem.have_press = false;
    touched();
    if (hit == HIT_DOWNLOAD) {
        (void)ui_hold_end(&rem.hold, evt, R_HOLD);
        return;
    }
    if (!touch_event_is_tap_up(evt) || r_hit_at(x, y) != hit) {
        return;
    }
    if (hit == HIT_NEVER) {
        settings_set(SET_UPD_REMIND, 0.0f);
        settings_request_save();
        rem.answered = true;
    } else if (hit == HIT_IGNORE) {
        rem.answered = true;
    }
}

/** @brief Runs the reminder for one frame: its hold. */
static void r_tick(float dt_s)
{
    upd_remind_drop_if_gone();
    if (rem.hold.down || rem.hold.flash_ms > 0) {
        touched();
    }
    ui_hold_flash_step(&rem.hold, dt_s);
    if (!rem.answered && upd_download_allowed() && ui_hold_tick(&rem.hold, dt_s)) {
        upd_screen_ask(UPD_REQ_DOWNLOAD);
        rem.answered = true;
        ui_hold_reached(&rem.hold);
    }
}

/** @brief Drops the presses under way, as touch events were lost. */
static void r_cancel(void)
{
    if (rem.hit == HIT_DOWNLOAD) {
        (void)ui_hold_left(&rem.hold);
    }
    rem.hit = HIT_NONE;
    rem.have_press = false;
    touched();
}

/** @brief Enters the reminder. */
static void r_enter(void)
{
    rem.answered = false;
    rem.yield = false;
    rem.hit = HIT_NONE;
    rem.have_press = false;
    ui_hold_reset(&rem.hold);
    touched();
}

/**
 * @brief Left any way but the driver's own: answered, as NOT NOW.
 */
static void r_leave(void)
{
    if (!rem.yield) {
        rem.answered = true;
    }
    rem.hit = HIT_NONE;
    rem.have_press = false;
    ui_hold_reset(&rem.hold);
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
    if ((rem.drawn_mask & bit) != 0u) {
        return;
    }
    gfx_clear(c, UI_BG);
    const upd_view_t *v = upd_screen_view();
    const gfx_rect_t box = R_BOX;
    ui_panel(c, box, TR(UPD_R_TITLE), UI_ACCENT);
    const int x = box.x + 24, w = box.w - 48;
    char line[112];
    snprintf(line, sizeof(line), TR(UPD_R_VERSIONS), v->found, v->running);
    /* The versions, what the hold does, how to turn the reminder back on,
     * and why the hold is grey: the lines of a group 28px apart, the
     * groups 36 to 48px. */
    upd_text_in(c, x, box.y + 44, w, line, UPD_FONT_BODY, UI_TEXT);
    upd_text_in(c, x, box.y + 84, w, TR(UPD_R_WHAT_1), UPD_FONT_BODY,
            UI_TEXT_DIM);
    upd_text_in(c, x, box.y + 84 + PITCH, w, TR(UPD_R_WHAT_2), UPD_FONT_BODY,
            UI_TEXT_DIM);
    upd_text_in(c, x, box.y + 148, w, TR(UPD_R_NEVER), UPD_FONT_BODY,
            UI_TEXT_DIM);
    if (!upd_download_allowed()) {
        char second[96];
        const upd_why_t why = (v->dl_block != UPD_WHY_NONE)
                                  ? v->dl_block : UPD_WHY_BUSY;
        upd_blocked_text(why, line, sizeof(line), second, sizeof(second));
        upd_text_in(c, x, box.y + 196, w, line, UPD_FONT_BODY, UI_WARN);
        if (second[0] != '\0') {
            upd_text_in(c, x, box.y + 196 + PITCH, w, second, UPD_FONT_BODY,
                    UI_WARN);
        }
    }
    upd_button(c, R_IGNORE, TR(UPD_IGNORE), UI_PANEL_HI,
           rem.have_press && rem.hit == HIT_IGNORE, true);
    upd_button(c, R_NEVER, TR(UPD_NEVER), UI_PANEL_HI,
           rem.have_press && rem.hit == HIT_NEVER, true);
    upd_button(c, R_HOLD, TR(UPD_HOLD_DL), upd_hold_fill(&rem.hold), false,
           upd_download_allowed());
    if (rem.hold.flash_ms > 0) {
        rem.drawn_mask = 0u;
    } else {
        rem.drawn_mask |= bit;
    }
}

/** @brief The reminder's table. */
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
    if (!f->gate_open || f->files_open || f->coproc || f->run
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
        if (rem.answered) {
            d->dismissed = true;
            /* The hold's download takes the panel to PANEL UPDATE: the
             * application goes there itself. */
            ui_router_goto(SCREEN_OVERVIEW);
        } else if (!may) {
            /* ARMED, the supply, the IOmcu's offer: they come first.
             * The reminder comes back once they have gone. */
            rem.yield = true;
            ui_router_goto(SCREEN_OVERVIEW);
            rem.yield = false;
        }
        return;
    }
    if (rem.answered) {
        d->dismissed = true;        /* left by the HOME tag or the menu */
        rem.answered = false;
    }
    if (!d->dismissed && may && ui_router_current() == SCREEN_OVERVIEW) {
        ui_router_goto(SCREEN_UPD_REMIND);
    }
}
