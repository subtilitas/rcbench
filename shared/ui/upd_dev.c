/**
 * @file upd_dev.c
 * @brief SETUP > UPDATES' rows of the wireless devices. See upd_dev.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_dev.h"

#include <stdio.h>
#include <string.h>

#include "ui_footer.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_screen.h"

/** @brief The rows' face: UPDATES' buttons and the IOmcu section's. */
#define DEV_FONT (&gfx_font_10x20)

_Static_assert(UPD_DEV_BTN_H >= UI_TAP_MIN, "a device hold is a finger tall");
_Static_assert(UPD_DEV_HEAD_Y + 20 + 4 <= UPD_DEV_LINE_Y,
               "the heading ends over the versions' line");
_Static_assert(UPD_DEV_LINE_Y + 20 + 8 <= UPD_DEV_BTN_Y,
               "the versions' line ends over the hold");
_Static_assert(UPD_DEV_BTN_Y + UPD_DEV_BTN_H + 8 <= UPD_C6_BTN_Y,
               "the Dial's hold ends 8px over the IOmcu's");

/** @brief The words of one row. */
typedef struct {
    ui_text_id_t head; /**< the heading */
    ui_text_id_t hold; /**< the hold's label */
    ui_text_id_t reinstall; /**< the hold's label at the carried version */
    ui_text_id_t connecting; /**< the label while the link comes up */
    ui_text_id_t hint; /**< the hold's hint */
    ui_text_id_t hint_re; /**< its hint at the carried version */
} dev_words_t;

/** @brief Each row's words. */
static const dev_words_t k_words[UPD_DEV_COUNT] = {
    [UPD_DEV_DIAL] = {
        .head = TX_UPD_DIAL_HEAD,
        .hold = TX_UPD_DIAL_HOLD, .reinstall = TX_UPD_DIAL_REINSTALL,
        .connecting = TX_UPD_DIAL_CONNECTING, .hint = TX_HOLD_HINT_DIAL_UPDATE,
        .hint_re = TX_HOLD_HINT_DIAL_REINSTALL,
    },
};

/** @brief One row's press. */
typedef struct {
    bool      have;  /**< a press is on the row */
    uint8_t   id;    /**< its contact */
    bool      live;  /**< it began on a live hold */
    ui_hold_t hold;  /**< the 2s hold */
} dev_press_t;

/** @brief One row's state. */
typedef struct {
    upd_dev_view_t v;    /**< what the row shows */
    bool           req;  /**< the hold completed, not yet taken */
    dev_press_t    p;    /**< the row's press */
} dev_state_t;

/** @brief The rows. */
static dev_state_t s_rows[UPD_DEV_COUNT];

/**
 * @brief A row id in range.
 * @param id the id
 * @return it, or the first row's
 */
static upd_dev_id_t in_range(upd_dev_id_t id)
{
    return (unsigned)id < (unsigned)UPD_DEV_COUNT ? id : UPD_DEV_DIAL;
}

/**
 * @brief Whether a row's hold may run now.
 * @param r the row
 * @return true while READY or REINSTALL
 */
static bool live(const dev_state_t *r)
{
    return r->v.row == DEVUPD_ROW_READY || r->v.row == DEVUPD_ROW_REINSTALL;
}

gfx_rect_t upd_dev_rect(upd_dev_id_t id)
{
    (void)in_range(id);
    return gfx_rect_make(UPD_DEV_X, UPD_DEV_BTN_Y, UPD_DEV_W, UPD_DEV_BTN_H);
}

void upd_dev_set(upd_dev_id_t id, const upd_dev_view_t *v)
{
    dev_state_t *r = &s_rows[in_range(id)];
    /* As upd_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &r->v, sizeof(r->v)) == 0) {
        return;
    }
    r->v = *v;
    if (!live(r) && r->p.have && r->p.live) {
        (void)ui_hold_left(&r->p.hold);
        r->p.have = false;
    }
    upd_screen_invalidate();
}

const upd_dev_view_t *upd_dev_view(upd_dev_id_t id)
{
    return &s_rows[in_range(id)].v;
}

bool upd_dev_take(upd_dev_id_t id)
{
    dev_state_t *r = &s_rows[in_range(id)];
    const bool was = r->req;
    r->req = false;
    return was;
}

const char *upd_dev_reason(const upd_dev_view_t *v)
{
    if (v == NULL) {
        return NULL;
    }
    switch (v->row) {
    case DEVUPD_ROW_NO_IMAGE:   return TR(DEV_WHY_NO_IMAGE);
    case DEVUPD_ROW_BAD_IMAGE:  return TR(DEV_WHY_BAD_IMAGE);
    case DEVUPD_ROW_CHECKING:   return TR(DEV_WHY_CHECKING);
    case DEVUPD_ROW_NOT_LINKED: return TR(DEV_WHY_NOT_LINKED);
    case DEVUPD_ROW_ARMED:      return TR(DEV_WHY_ARMED);
    case DEVUPD_ROW_BUSY:       return TR(DEV_WHY_BUSY);
    case DEVUPD_ROW_UPDATING:   return TR(DEV_WHY_UPDATING);
    case DEVUPD_ROW_READY:
    case DEVUPD_ROW_REINSTALL:
    default:
        return NULL;
    }
}

/**
 * @brief A version as text, or @p none's text.
 * @param out  the text
 * @param n    bytes of @p out
 * @param have whether there is a version
 * @param v    the version
 * @param none the text for none
 */
static void ver_text(char *out, size_t n, bool have, fw_version_t v,
                     const char *none)
{
    if (have) {
        snprintf(out, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
                 (unsigned)v.patch);
    } else {
        snprintf(out, n, "%s", none);
    }
}

void upd_dev_line(upd_dev_id_t id, const upd_dev_view_t *v, char *out,
                  size_t n)
{
    char run[24], car[24];
    ver_text(run, sizeof(run), v->known, v->running, TR(UPD_DEV_NOT_LINKED));
    ver_text(car, sizeof(car), v->have_image, v->carried, TR(UPD_C6_NONE));
    /* Each row's formats are named at their call, so that
     * tools/check_formats.py holds them to their arguments. */
    switch (in_range(id)) {
    case UPD_DEV_DIAL:
    default:
        snprintf(out, n, TR(UPD_DIAL_LINE), run, car);
        break;
    }
}

/**
 * @brief The label while the device asks for its confirmation.
 * @param id   the row
 * @param secs the seconds the device gave
 * @param out  receives it
 * @param n    its bytes
 */
static void press_label(upd_dev_id_t id, unsigned secs, char *out, size_t n)
{
    switch (id) {
    case UPD_DEV_DIAL:
    default:
        snprintf(out, n, TR(UPD_DIAL_PRESS), secs);
        break;
    }
}

/**
 * @brief The label while the device takes chunks.
 * @param id  the row
 * @param pct the share taken, 0 to 100
 * @param out receives it
 * @param n   its bytes
 */
static void progress_label(upd_dev_id_t id, unsigned pct, char *out,
                           size_t n)
{
    switch (id) {
    case UPD_DEV_DIAL:
    default:
        snprintf(out, n, TR(UPD_DIAL_PROGRESS), pct);
        break;
    }
}

void upd_dev_label(upd_dev_id_t id, const upd_dev_view_t *v, char *out,
                   size_t n)
{
    const dev_words_t *w = &k_words[in_range(id)];
    if (v->row == DEVUPD_ROW_UPDATING) {
        if (v->phase == DEVUPD_CONFIRM) {
            press_label(in_range(id), v->secs, out, n);
        } else if (v->phase >= DEVUPD_SEND) {
            progress_label(in_range(id),
                           v->percent > 100u ? 100u : v->percent, out, n);
        } else {
            snprintf(out, n, "%s", ui_tr(w->connecting));
        }
    } else if (v->row == DEVUPD_ROW_REINSTALL) {
        snprintf(out, n, "%s", ui_tr(w->reinstall));
    } else {
        snprintf(out, n, "%s", ui_tr(w->hold));
    }
}

bool upd_dev_pressed(void)
{
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        if (s_rows[i].p.have) {
            return true;
        }
    }
    return false;
}

/**
 * @brief A touch event for one row.
 * @param id    the row
 * @param evt   the event
 * @param other another control holds a press
 * @return true when the row took it
 */
static bool row_event(upd_dev_id_t id, const touch_event_t *evt, bool other)
{
    dev_state_t *r = &s_rows[id];
    const int x = evt->point.x, y = evt->point.y;
    const gfx_rect_t rect = upd_dev_rect(id);
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (other || r->p.have || !gfx_rect_contains(rect, x, y)) {
            return false;
        }
        r->p.have = true;
        r->p.id = evt->point.id;
        r->p.live = live(r);
        if (r->p.live) {
            ui_hold_begin(&r->p.hold, r->v.row == DEVUPD_ROW_REINSTALL
                                          ? k_words[id].hint_re
                                          : k_words[id].hint);
        }
        upd_screen_invalidate();
        return true;
    }
    if (!r->p.have || evt->point.id != r->p.id) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off abandons it, as on every held control. */
        if (!gfx_rect_contains(rect, x, y)) {
            if (r->p.live) {
                (void)ui_hold_leave(&r->p.hold);
            }
            r->p.have = false;
            upd_screen_invalidate();
        }
        return true;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    r->p.have = false;
    upd_screen_invalidate();
    if (r->p.live) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&r->p.hold, evt, rect);
        return true;
    }
    if (touch_event_is_tap_up(evt) && gfx_rect_contains(rect, x, y)) {
        ui_router_set_notice(upd_dev_reason(&r->v));
    }
    return true;
}

bool upd_dev_event(const touch_event_t *evt, bool other)
{
    if (evt == NULL) {
        return false;
    }
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        if (row_event((upd_dev_id_t)i, evt, other || (upd_dev_pressed()
                                                      && !s_rows[i].p.have))) {
            return true;
        }
    }
    return false;
}

bool upd_dev_tick(float dt_s)
{
    bool moving = false;
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        dev_state_t *r = &s_rows[i];
        moving = moving || r->p.hold.down || r->p.hold.flash_ms > 0;
        ui_hold_flash_step(&r->p.hold, dt_s);
        if (r->p.have && r->p.live && live(r)
            && ui_hold_tick(&r->p.hold, dt_s)) {
            r->req = true;
            ui_hold_reached(&r->p.hold);
        }
    }
    return moving;
}

bool upd_dev_flashing(void)
{
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        if (s_rows[i].p.hold.flash_ms > 0) {
            return true;
        }
    }
    return false;
}

void upd_dev_drop(void)
{
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        dev_state_t *r = &s_rows[i];
        if (r->p.have && r->p.live) {
            (void)ui_hold_left(&r->p.hold);
        }
        r->p.have = false;
        ui_hold_reset(&r->p.hold);
    }
}

void upd_dev_render(gfx_canvas_t *c)
{
    char line[96];
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        const upd_dev_id_t id = (upd_dev_id_t)i;
        dev_state_t *r = &s_rows[i];
        const int h = gfx_text_height(DEV_FONT, 1);
        gfx_text_in(c, gfx_rect_make(UPD_DEV_X, UPD_DEV_HEAD_Y, UPD_DEV_W, h),
                    ui_tr(k_words[i].head), DEV_FONT, UI_TEXT_DIM, 1,
                    GFX_ALIGN_LEFT);
        upd_dev_line(id, &r->v, line, sizeof(line));
        gfx_text_in(c, gfx_rect_make(UPD_DEV_X, UPD_DEV_LINE_Y, UPD_DEV_W, h),
                    line, DEV_FONT, UI_TEXT, 1, GFX_ALIGN_LEFT);
        upd_dev_label(id, &r->v, line, sizeof(line));
        gfx_color_t fill = UI_PANEL_HI;
        if (live(r)) {
            fill = ui_hold_button_fill(UI_PANEL_HI, UI_OK, &r->p.hold);
        }
        ui_button_font(c, upd_dev_rect(id), line, fill, false, live(r),
                       DEV_FONT);
    }
}
