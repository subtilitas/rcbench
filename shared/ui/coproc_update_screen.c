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
#include "upd_wiz_view.h"
#include "upd_wizard.h"

/*
 * The faces of the type scale the progress draws in:
 * CU_FONT_HEAD (20 x 36) for the title and the step, CU_FONT_BODY (12 x 24)
 * for every line read at a distance.
 */
/** @brief The title and the step: 20 x 36. */
#define CU_FONT_HEAD  (&gfx_font_20x36)
/** @brief The lines read at a distance: 12 x 24. */
#define CU_FONT_BODY  UI_FONT_BODY

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

static struct {
    cu_version_t running;  /**< the version the IOmcu runs */
    cu_version_t carried;  /**< the version this panel carries */

    cu_step_t step;  /**< the step under way */
    uint16_t  block;  /**< the blocks committed */
    uint16_t  blocks;  /**< the blocks the update has */

    bool      opened;        /**< an offer opened UPDATES in this start   */
    unsigned  drawn_mask;    /**< the static half, per framebuffer         */
} s;  /**< the progress's state */

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

/** @brief Returns the screen to its initial state: the panel's start. */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

void coproc_update_screen_versions(cu_version_t running,
                                   cu_version_t carried)
{
    s.running = running;
    s.carried = carried;
    s.drawn_mask = 0;
}

void coproc_update_screen_progress(cu_step_t step, uint16_t block,
                                   uint16_t blocks)
{
    if (step != s.step) {
        s.drawn_mask = 0;
    }
    s.step = step;
    s.block = (block > blocks) ? blocks : block;
    s.blocks = blocks;
}

bool coproc_update_opened(void) { return s.opened; }

/** @brief Enters the screen. */
static void enter(void)
{
    s.drawn_mask = 0;
}

/* ---------------------------------------------------------------- drawing */

/** @brief Writes a version as "major.minor.patch". */
static void fmt_version(char *out, size_t n, cu_version_t v)
{
    snprintf(out, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
             (unsigned)v.patch);
}

/** @brief Gives the progress view's text for a step. */
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

/** @brief Draws the screen. */
static void render(gfx_canvas_t *c, int buffer_index)
{
    /* A framebuffer index the mask has no bit for is redrawn whole. */
    const unsigned bit = (buffer_index >= 0 && buffer_index < 8)
                             ? 1u << buffer_index : 0u;
    render_progress(c, bit);
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

/**
 * @brief UPDATES' hold, completed: the progress, and the START it hands
 *        over.
 * @param d   What the driver keeps.
 * @param f   This frame's update.
 * @param out What this frame hands over.
 */
static void start_from_updates(cu_driver_t *d, const cu_feed_t *f,
                               cu_hand_t *out)
{
    coproc_update_screen_versions(f->running, f->carried);
    s.step = CU_STEP_PREPARE;
    s.block = 0u;
    s.blocks = 0u;
    ++d->starts_sent;
    ++out->starts;
    ui_router_goto(SCREEN_COPROC_UPDATE);
}

cu_wait_t coproc_update_wait(const cu_feed_t *f, bool unsafe)
{
    if (unsafe) {
        return CU_WAIT_UNSAFE;
    }
    if (f != NULL && f->radio_up) {
        return CU_WAIT_RADIO;
    }
    if (f != NULL && f->installing) {
        return CU_WAIT_INSTALL;
    }
    return CU_WAIT_NONE;
}

ui_text_id_t coproc_update_offer_text(cu_wait_t w)
{
    switch (w) {
    case CU_WAIT_UNSAFE:  return TX_ALERT_CU_OFFER_SAFE;
    case CU_WAIT_RADIO:   return TX_ALERT_CU_OFFER_WIFI;
    case CU_WAIT_INSTALL: return TX_ALERT_CU_OFFER_INSTALL;
    case CU_WAIT_NONE:
    default:              return TX_ALERT_CU_OFFER;
    }
}

/**
 * @brief The band line of an offer UPDATES has not shown: said once on the
 *        first frame with a band, and again when what the opening waits
 *        for changes to another reason.  While an update run waits to open
 *        UPDATES the line is the run's: the offer's counts as said.
 * @param d      What the driver keeps.
 * @param f      This frame's update.
 * @param unsafe ARMED, an arm waiting, or the supply's output on or coming.
 */
static void say_offer(cu_driver_t *d, const cu_feed_t *f, bool unsafe)
{
    if (!f->offered || f->offer_gen == d->shown_gen
        || !ui_router_stop_live()) {
        return;
    }
    const cu_wait_t w = coproc_update_wait(f, unsafe);
    const bool fresh = f->offer_gen != d->said_gen;
    if (fresh || (w != CU_WAIT_NONE && (uint8_t)w != d->said_wait)) {
        if (!f->run_waits) {
            ui_router_set_alert(ui_tr(coproc_update_offer_text(w)));
        }
        d->said_gen  = f->offer_gen;
        d->said_wait = (uint8_t)w;
    }
}

/**
 * @brief UPDATES shows the offer @p gen: it counts as shown, a skipped
 *        IOmcu step comes back for it, and a hold under way on that step
 *        ends unfired, as its versions may have changed.
 * @param d   What the driver keeps.
 * @param gen The offer's offer_gen.
 */
static void show_offer(cu_driver_t *d, uint32_t gen)
{
    d->shown_gen = gen;
    upd_wiz_unskip(UPD_WIZ_IOMCU);
    upd_wiz_drop_step(UPD_WIZ_IOMCU);
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

    /* UPDATES' hold first.  The section is read again here, a frame after
     * the hold completed, and a hold the state no longer allows starts
     * nothing. */
    if (upd_screen_take_coproc()
        && coproc_update_section(d, f, unsafe) == CU_SEC_READY
        && ui_router_current() == SCREEN_UPDATES) {
        start_from_updates(d, f, out);
    }

    /* SKIP THIS STEP and the home tag that left UPDATES, wherever the
     * router is now: each declines the offer UPDATES showed, and one given
     * for an offer replaced since declines nothing. */
    const bool skip = upd_screen_take_skip_coproc();
    const bool left = upd_screen_take_left_cu();
    if ((skip || left) && f->offered && f->offer_gen == d->shown_gen) {
        out->decline = true;
    }

    /* What UPDATES' IOmcu step shows, after this frame's START is
     * counted. */
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
        /* Ended, or the START was spent on an offer that had gone.  A
         * snapshot that shows the START taken a moment before it shows
         * the update active lands here too, and the progress the next
         * frame opens again returns to UPDATES as well. */
        if (f->starts_taken == d->starts_sent) {
            ui_router_goto(SCREEN_UPDATES);
        }
        return;
    }

    if (!f->offered || f->offer_gen == d->shown_gen) {
        return;
    }
    if (ui_router_current() == SCREEN_UPDATES) {
        /* UPDATES shows it already: its IOmcu step is the question. */
        show_offer(d, f->offer_gen);
        return;
    }
    say_offer(d, f, unsafe);
    if (!f->run_waits && coproc_update_wait(f, unsafe) == CU_WAIT_NONE
        && ui_router_current() == SCREEN_OVERVIEW) {
        show_offer(d, f->offer_gen);
        s.opened = true;
        ui_router_goto(SCREEN_UPDATES);
    }
}

/** @brief The screen's table: the progress takes no touch and has no
 *         band, so no home tag either. */
static const ui_screen_t k_screen = {
    .title  = "UPDATE",
    .reset  = reset,
    .enter  = enter,
    .render = render,
};

const ui_screen_t *coproc_update_screen(void) { return &k_screen; }

/* UPDATES' IOmcu step and SINGLE UPDATES' IOmcu section say its state. */
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
        /* An older image in the warning colour, the same number of
         * another build plainly, else what the update does to the
         * bench. */
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
