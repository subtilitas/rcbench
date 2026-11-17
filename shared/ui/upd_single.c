/**
 * @file upd_single.c
 * @brief SINGLE UPDATES. See upd_single.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_single.h"

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

_Static_assert(BH >= UI_TAP_MIN, "the page's buttons are a finger tall");
_Static_assert(RY + BH + 16 <= CU_HEAD_Y,
               "the IOmcu section starts under CHECK's place");
_Static_assert(CU_LINE_Y + 2 * CU_PITCH + 20 < CU_BTN_Y,
               "two lines of state end over the IOmcu's hold");
_Static_assert(CU_BTN_Y + BH <= UI_FOOTER_FLOOR,
               "the IOmcu's hold ends over the footer");
_Static_assert(RY + RH + 12 <= UPD_C6_LINE_Y,
               "the heading's card ends over the C6 row");

/** @brief What a press on the page began on. */
enum {
    SP_NONE = 0,
    SP_COPROC,      /* HOLD 2s TO UPDATE THE IOMCU */
    SP_COPROC_WHY,  /* the grey IOmcu hold: why, on the band */
    SP_BACK,        /* BACK */
};

/** @brief The page's state, one instance per image. */
typedef struct {
    bool          shown;      /**< the page covers UPDATES */
    upd_cu_view_t cu;         /**< the IOmcu section */
    bool          cu_req;     /**< the IOmcu hold completed, not yet taken */
    int           hit;        /**< the control the press began on */
    uint8_t       press_id;   /**< the press's contact */
    bool          have_press; /**< a press is on the IOmcu hold or BACK */
    ui_hold_t     cu_hold;    /**< UPDATE IOMCU */
} upd_single_t;

/** @brief The page's one instance. */
static upd_single_t sp;

/* ------------------------------------------------------------ the model */

/** @brief Whether UPDATE IOMCU's hold may run. @return true while READY */
static bool coproc_allowed(void) { return sp.cu.sec == CU_SEC_READY; }

/** @brief A press on the IOmcu hold the section took away under it ends. */
static void drop_hold_if_gone(void)
{
    if (!coproc_allowed() && sp.have_press && sp.hit == SP_COPROC) {
        (void)ui_hold_left(&sp.cu_hold);
        sp.hit = SP_NONE;
        sp.have_press = false;
    }
}

void upd_screen_set_coproc(const upd_cu_view_t *v)
{
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &sp.cu, sizeof(sp.cu)) == 0) {
        return;
    }
    sp.cu = *v;
    drop_hold_if_gone();
    upd_screen_invalidate();
}

const upd_cu_view_t *upd_screen_coproc(void) { return &sp.cu; }

bool upd_screen_take_coproc(void)
{
    const bool was = sp.cu_req;
    sp.cu_req = false;
    return was;
}

/* ---------------------------------------------------------- the page */

bool upd_single_shown(void) { return sp.shown; }

gfx_rect_t upd_single_back_rect(void)
{
    return ui_footer_rect(UI_FOOTER_SETTINGS);
}

/** @brief Every press on the page ends unfired. */
static void drop_presses(void)
{
    if (sp.have_press && sp.hit == SP_COPROC) {
        (void)ui_hold_left(&sp.cu_hold);
    }
    sp.hit = SP_NONE;
    sp.have_press = false;
    upd_c6_drop();
    upd_dev_drop();
}

void upd_single_open(void)
{
    drop_presses();
    ui_hold_reset(&sp.cu_hold);
    sp.shown = true;
    upd_screen_invalidate();
}

void upd_single_close(void)
{
    drop_presses();
    upd_dev_close();
    ui_hold_reset(&sp.cu_hold);
    if (sp.shown) {
        sp.shown = false;
        upd_screen_invalidate();
    }
}

void upd_single_reset(void)
{
    upd_single_close();
    memset(&sp, 0, sizeof(sp));
    ui_hold_reset(&sp.cu_hold);
}

void upd_single_cancel(void)
{
    drop_presses();
    upd_screen_invalidate();
}

/** @brief Finds the page's own control at a point. */
static int hit_at(int x, int y)
{
    if (gfx_rect_contains(CU_BTN, x, y)) {
        return coproc_allowed() ? SP_COPROC : SP_COPROC_WHY;
    }
    if (gfx_rect_contains(upd_single_back_rect(), x, y)) {
        return SP_BACK;
    }
    return SP_NONE;
}

/** @brief Acts on a control of the page that was tapped. */
static void act(int hit)
{
    if (hit == SP_BACK) {
        upd_single_close();
    } else if (hit == SP_COPROC_WHY) {
        /* A tap or a hold on the grey hold: the section's line, on the
         * band, as the C6 and device rows say theirs. */
        const char *why = "";
        gfx_color_t col = UI_TEXT;
        upd_cu_text(&sp.cu, &why, &col);
        ui_router_set_notice(why);
    }
}

void upd_single_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (upd_dev_shown()) {
        (void)upd_dev_event(evt, false);   /* the devices' page, whole */
        return;
    }
    if (upd_c6_event(evt, sp.have_press || upd_dev_pressed())) {
        return;                    /* the C6 RADIO row's press */
    }
    if (upd_dev_event(evt, sp.have_press || upd_c6_pressed())) {
        return;                    /* the devices' entry */
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (sp.have_press || upd_c6_pressed() || upd_dev_pressed()) {
            return;                /* one press at a time */
        }
        sp.hit = hit_at(x, y);
        if (sp.hit != SP_NONE) {
            sp.have_press = true;
            sp.press_id = evt->point.id;
            if (sp.hit == SP_COPROC) {
                ui_hold_begin(&sp.cu_hold, TX_HOLD_HINT_IOMCU_UPDATE);
            }
        }
        upd_screen_invalidate();
        return;
    }
    if (!sp.have_press || evt->point.id != sp.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off the hold abandons it, as on every held control. */
        if (sp.hit == SP_COPROC && !gfx_rect_contains(CU_BTN, x, y)
            && ui_hold_leave(&sp.cu_hold)) {
            sp.hit = SP_NONE;
            sp.have_press = false;
            upd_screen_invalidate();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = sp.hit;
    sp.hit = SP_NONE;
    sp.have_press = false;
    upd_screen_invalidate();
    if (hit == SP_COPROC) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&sp.cu_hold, evt, CU_BTN);
        return;
    }
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

bool upd_single_tick(float dt_s)
{
    drop_hold_if_gone();
    bool moving = upd_c6_tick(dt_s);    /* the C6 row's fill */
    moving = upd_dev_tick(dt_s) || moving;  /* a device row's fill */
    moving = moving || sp.cu_hold.down || sp.cu_hold.flash_ms > 0;
    ui_hold_flash_step(&sp.cu_hold, dt_s);
    if (coproc_allowed() && ui_hold_tick(&sp.cu_hold, dt_s)) {
        /* coproc_update_drive() takes it next frame and opens the
         * progress. */
        sp.cu_req = true;
        ui_hold_reached(&sp.cu_hold);
    }
    return moving;
}

/* ---------------------------------------------------------- the drawing */

/** @brief Writes a version as "major.minor.patch", or says it is not known. */
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
    upd_text_in(c, BX, CU_HEAD_Y, BW, TR(UPD_CU_HEAD), UPD_FONT_LABEL,
                UI_TEXT_DIM);
    cu_version_text(ver, sizeof(ver), sp.cu.known, sp.cu.running);
    snprintf(line, sizeof(line), TR(UPD_CU_RUNNING), ver);
    upd_text_in(c, BX, CU_RUN_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);
    cu_version_text(ver, sizeof(ver), sp.cu.have_image, sp.cu.carried);
    snprintf(line, sizeof(line), TR(UPD_CU_CARRIED), ver);
    upd_text_in(c, BX, CU_CAR_Y, BW, line, UPD_FONT_LABEL, UI_TEXT);

    const char *state = "";
    gfx_color_t col = UI_TEXT;
    upd_cu_text(&sp.cu, &state, &col);
    (void)ui_text_wrap(c, gfx_rect_make(BX, CU_LINE_Y, BW,
                                        gfx_text_height(UPD_FONT_LABEL, 1)),
                       CU_PITCH, state, UPD_FONT_LABEL, col);

    const gfx_color_t fill = coproc_allowed() ? upd_hold_fill(&sp.cu_hold)
                                              : UI_PANEL_HI;
    ui_button_font(c, CU_BTN, TR(UPD_CU_HOLD), fill, false, coproc_allowed(),
                   UPD_FONT_LABEL);
}

/**
 * @brief The heading in the card's place: what the page is for, then what
 *        a reinstall does.
 * @param c Canvas, the body.
 */
static void render_heading(gfx_canvas_t *c)
{
    ui_card(c, gfx_rect_make(RX, RY, RW, RH), UI_PANEL);
    const int x = RX + RPAD, w = RW - 2 * RPAD;
    const gfx_rect_t first = gfx_rect_make(x, RY + RPAD, w,
                                           gfx_text_height(UPD_FONT_BODY, 1));
    const int n = ui_text_wrap(c, first, PITCH, TR(UPD_SINGLE_HEAD),
                               UPD_FONT_BODY, UI_TEXT);
    (void)ui_text_wrap(c, gfx_rect_make(x, first.y + n * PITCH + 12, w,
                                        first.h),
                       PITCH, TR(UPD_SINGLE_NOTE), UPD_FONT_BODY,
                       UI_TEXT_DIM);
}

bool upd_single_render(gfx_canvas_t *c)
{
    if (upd_dev_shown()) {
        upd_dev_render_page(c);
        return upd_dev_flashing();
    }
    gfx_clear(c, UI_BG);
    render_heading(c);
    render_coproc(c);
    upd_c6_render(c);
    upd_dev_render(c);
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "UPDATES", NULL, false);
    upd_button(c, upd_single_back_rect(), TR(UPD_DEVS_BACK), UI_PANEL_HI,
               sp.have_press && sp.hit == SP_BACK, true);
    return sp.cu_hold.flash_ms > 0 || upd_c6_flashing() || upd_dev_flashing();
}
