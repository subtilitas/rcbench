/**
 * @file upd_dev.c
 * @brief SETUP > UPDATES' wireless devices: the entry and the page of
 *        rows. See upd_dev.h.
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
/** @brief The page's note: the card's face, 12 x 24. */
#define NOTE_FONT  UI_FONT_BODY
/** @brief The note's lines, 28px apart. */
#define NOTE_PITCH 28

_Static_assert(UPD_DEV_BTN_H >= UI_TAP_MIN, "a device hold is a finger tall");
_Static_assert(UPD_DEV_HEAD_Y + 20 + 4 <= UPD_DEV_LINE_Y,
               "the heading ends over the summary line");
_Static_assert(UPD_DEV_LINE_Y + 20 + 8 <= UPD_DEV_BTN_Y,
               "the summary line ends over the button");
_Static_assert(UPD_DEV_BTN_Y + UPD_DEV_BTN_H + 8 <= UPD_C6_BTN_Y,
               "the entry ends 8px over the IOmcu's hold");
_Static_assert(UPD_DEVP_LINE_DY + 20 <= UPD_DEV_BTN_H,
               "a row's line ends beside its hold");
_Static_assert(UPD_DEVP_TEXT_X + UPD_DEVP_TEXT_W + 16 <= UPD_DEVP_HOLD_X,
               "a row's text ends 16px left of its hold");
_Static_assert(UPD_DEVP_HOLD_X + UPD_DEVP_HOLD_W <= UI_PANEL_W - UI_MARGIN,
               "the holds keep the margin");
_Static_assert(UPD_DEVP_Y0 + (UPD_DEV_COUNT - 1) * UPD_DEVP_PITCH
                   + UPD_DEV_BTN_H + 24 <= UPD_DEVP_NOTE_Y,
               "the last row ends 24px over the note");
_Static_assert(UPD_DEVP_NOTE_Y + 2 * NOTE_PITCH + 24 <= UI_FOOTER_FLOOR,
               "three lines of the note end over the footer");

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
    [UPD_DEV_PNODE] = {
        .head = TX_UPD_PNODE_HEAD,
        .hold = TX_UPD_PNODE_HOLD, .reinstall = TX_UPD_PNODE_REINSTALL,
        .connecting = TX_UPD_PNODE_CONNECTING,
        .hint = TX_HOLD_HINT_PNODE_UPDATE,
        .hint_re = TX_HOLD_HINT_PNODE_REINSTALL,
    },
    [UPD_DEV_BAL] = {
        .head = TX_UPD_BAL_HEAD,
        .hold = TX_UPD_BAL_HOLD, .reinstall = TX_UPD_BAL_REINSTALL,
        .connecting = TX_UPD_BAL_CONNECTING,
        .hint = TX_HOLD_HINT_BAL_UPDATE,
        .hint_re = TX_HOLD_HINT_BAL_REINSTALL,
    },
};

/** @brief One press: the entry's, a row's or BACK's. */
typedef struct {
    bool      have;  /**< a press is on it */
    uint8_t   id;    /**< its contact */
    bool      live;  /**< it began on a live hold */
    ui_hold_t hold;  /**< a row's 2s hold */
} dev_press_t;

/** @brief One row's state. */
typedef struct {
    upd_dev_view_t v;    /**< what the row shows */
    bool           req;  /**< the hold completed, not yet taken */
    dev_press_t    p;    /**< the row's press */
} dev_state_t;

/** @brief The rows. */
static dev_state_t s_rows[UPD_DEV_COUNT];
/** @brief The page is open. */
static bool        s_shown;
/** @brief The entry's press, on UPDATES. */
static dev_press_t s_entry;
/** @brief BACK's press, on the page. */
static dev_press_t s_back;

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
    const int y = UPD_DEVP_Y0 + (int)in_range(id) * UPD_DEVP_PITCH;
    return gfx_rect_make(UPD_DEVP_HOLD_X, y, UPD_DEVP_HOLD_W, UPD_DEV_BTN_H);
}

gfx_rect_t upd_dev_entry_rect(void)
{
    return gfx_rect_make(UPD_DEV_X, UPD_DEV_BTN_Y, UPD_DEV_W, UPD_DEV_BTN_H);
}

gfx_rect_t upd_dev_back_rect(void)
{
    return ui_footer_rect(UI_FOOTER_SETTINGS);
}

/** @brief Every row's press ends unfired. */
static void drop_rows(void)
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

bool upd_dev_shown(void) { return s_shown; }

void upd_dev_open(void)
{
    upd_dev_drop();
    s_shown = true;
    upd_screen_invalidate();
}

void upd_dev_close(void)
{
    upd_dev_drop();
    if (s_shown) {
        s_shown = false;
        upd_screen_invalidate();
    }
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
    case UPD_DEV_PNODE:
    case UPD_DEV_BAL:
        snprintf(out, n, TR(UPD_NODE_LINE), run, car);
        break;
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
    case UPD_DEV_PNODE:
    case UPD_DEV_BAL:
        snprintf(out, n, TR(UPD_NODE_PRESS), secs);
        break;
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
    case UPD_DEV_PNODE:
        snprintf(out, n, TR(UPD_PNODE_PROGRESS), pct);
        break;
    case UPD_DEV_BAL:
        snprintf(out, n, TR(UPD_BAL_PROGRESS), pct);
        break;
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

void upd_dev_summary(char *out, size_t n)
{
    unsigned known = 0u, other = 0u;
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        known += s_rows[i].v.known ? 1u : 0u;
        other += s_rows[i].v.row == DEVUPD_ROW_READY ? 1u : 0u;
    }
    snprintf(out, n, TR(UPD_DEVS_LINE), known, (unsigned)UPD_DEV_COUNT,
             other);
}

void upd_dev_entry_label(char *out, size_t n)
{
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        if (s_rows[i].v.row == DEVUPD_ROW_UPDATING) {
            upd_dev_label((upd_dev_id_t)i, &s_rows[i].v, out, n);
            return;
        }
    }
    snprintf(out, n, "%s", TR(UPD_DEVS_OPEN));
}

bool upd_dev_pressed(void)
{
    if (s_entry.have || s_back.have) {
        return true;
    }
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        if (s_rows[i].p.have) {
            return true;
        }
    }
    return false;
}

/**
 * @brief A touch event for a tap button: the entry or BACK.
 * @param p     its press
 * @param rect  its rectangle
 * @param evt   the event
 * @param other another control holds a press
 * @return 1 when it took the event, 2 when that was a tap on it, 0 when
 *         it did not take it
 */
static int tap_event(dev_press_t *p, gfx_rect_t rect, const touch_event_t *evt,
                     bool other)
{
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (other || p->have || !gfx_rect_contains(rect, x, y)) {
            return 0;
        }
        p->have = true;
        p->id = evt->point.id;
        upd_screen_invalidate();
        return 1;
    }
    if (!p->have || evt->point.id != p->id) {
        return 0;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (!gfx_rect_contains(rect, x, y)) {
            p->have = false;
            upd_screen_invalidate();
        }
        return 1;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return 1;
    }
    p->have = false;
    upd_screen_invalidate();
    return touch_event_is_tap_up(evt) && gfx_rect_contains(rect, x, y) ? 2
                                                                      : 1;
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

/**
 * @brief A touch event on the open page: a row's, BACK's, or nobody's.
 * @param evt the event
 */
static void page_event(const touch_event_t *evt)
{
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        const bool busy = upd_dev_pressed() && !s_rows[i].p.have;
        if (row_event((upd_dev_id_t)i, evt, busy)) {
            return;
        }
    }
    const bool busy = upd_dev_pressed() && !s_back.have;
    if (tap_event(&s_back, upd_dev_back_rect(), evt, busy) == 2) {
        upd_dev_close();
    }
}

bool upd_dev_event(const touch_event_t *evt, bool other)
{
    if (evt == NULL) {
        return false;
    }
    if (s_shown) {
        page_event(evt);
        return true;
    }
    const int got = tap_event(&s_entry, upd_dev_entry_rect(), evt, other);
    if (got == 2) {
        upd_dev_open();
    }
    return got != 0;
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
    drop_rows();
    s_entry.have = false;
    s_back.have = false;
}

void upd_dev_render(gfx_canvas_t *c)
{
    char line[96];
    const int h = gfx_text_height(DEV_FONT, 1);
    gfx_text_in(c, gfx_rect_make(UPD_DEV_X, UPD_DEV_HEAD_Y, UPD_DEV_W, h),
                TR(UPD_DEVS_HEAD), DEV_FONT, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
    upd_dev_summary(line, sizeof(line));
    gfx_text_in(c, gfx_rect_make(UPD_DEV_X, UPD_DEV_LINE_Y, UPD_DEV_W, h),
                line, DEV_FONT, UI_TEXT, 1, GFX_ALIGN_LEFT);
    upd_dev_entry_label(line, sizeof(line));
    ui_button_font(c, upd_dev_entry_rect(), line, UI_PANEL_HI, s_entry.have,
                   true, DEV_FONT);
}

void upd_dev_render_page(gfx_canvas_t *c)
{
    char line[96];
    gfx_clear(c, UI_BG);
    const int h = gfx_text_height(DEV_FONT, 1);
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        const upd_dev_id_t id = (upd_dev_id_t)i;
        dev_state_t *r = &s_rows[i];
        const int y = UPD_DEVP_Y0 + (int)i * UPD_DEVP_PITCH;
        gfx_text_in(c, gfx_rect_make(UPD_DEVP_TEXT_X, y, UPD_DEVP_TEXT_W, h),
                    ui_tr(k_words[i].head), DEV_FONT, UI_TEXT_DIM, 1,
                    GFX_ALIGN_LEFT);
        upd_dev_line(id, &r->v, line, sizeof(line));
        gfx_text_in(c, gfx_rect_make(UPD_DEVP_TEXT_X, y + UPD_DEVP_LINE_DY,
                                     UPD_DEVP_TEXT_W, h),
                    line, DEV_FONT, UI_TEXT, 1, GFX_ALIGN_LEFT);
        upd_dev_label(id, &r->v, line, sizeof(line));
        gfx_color_t fill = UI_PANEL_HI;
        if (live(r)) {
            fill = ui_hold_button_fill(UI_PANEL_HI, UI_OK, &r->p.hold);
        }
        /* A running update's label reads bright: the device's ask. */
        ui_button_font(c, upd_dev_rect(id), line, fill, false,
                       live(r) || r->v.row == DEVUPD_ROW_UPDATING, DEV_FONT);
    }
    (void)ui_text_wrap(c, gfx_rect_make(UPD_DEVP_TEXT_X, UPD_DEVP_NOTE_Y,
                                        UI_PANEL_W - 2 * UPD_DEVP_TEXT_X,
                                        gfx_text_height(NOTE_FONT, 1)),
                       NOTE_PITCH, TR(UPD_DEVS_NOTE), NOTE_FONT,
                       UI_TEXT_DIM);
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "UPDATES", NULL, false);
    ui_button_font(c, upd_dev_back_rect(), TR(UPD_DEVS_BACK), UI_PANEL_HI,
                   s_back.have, true, DEV_FONT);
}
