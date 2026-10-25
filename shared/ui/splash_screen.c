/**
 * @file
 * @brief splash_screen.h says what this screen is for.
 *
 * SPDX-License-Identifier: MIT
 */

#include "splash_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The panel's width, px. */
#define W UI_PANEL_W
/** @brief The panel's height, px. */
#define H UI_PANEL_H

/* Hold after the last step has answered, before the menu: 1.6s to read
 * seven lines.  A tap skips it; a failed step stays reported on screen. */
/** @brief The hold after the last step has answered, s. */
#define HOLD_S 1.6f

/** @brief Each step's name, in the order of splash_step_t. */
static const ui_text_id_t k_labels[SPLASH_STEP_COUNT] = {
    TX_SPLASH_BOARD, TX_SPLASH_DISPLAY, TX_SPLASH_TOUCH, TX_SPLASH_STORAGE,
    TX_SPLASH_SETTINGS, TX_SPLASH_LINK, TX_SPLASH_IOMCU,
};

static struct {
    splash_result_t result[SPLASH_STEP_COUNT];   /**< each step's result */
    char            detail[SPLASH_STEP_COUNT][SPLASH_DETAIL_MAX]; /**< and
                                                    its detail */
    float           held_s;   /**< the hold since the last answer, s     */
    bool            skipped;  /**< a tap skipped the hold                */
    unsigned        drawn_mask;   /**< per framebuffer, bit per buffer      */
} s;  /**< the steps and the hold */

/** @brief Draw the whole splash again on both framebuffers. */
void splash_invalidate(void) { s.drawn_mask = 0; }

/** @brief Every step pending, nothing held. */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

/**
 * @brief A step's result and its detail.
 * @param step   The step.
 * @param result Its result.
 * @param detail What it found, or NULL for nothing.
 */
void splash_screen_set(splash_step_t step, splash_result_t result,
                       const char *detail)
{
    if (step < 0 || step >= SPLASH_STEP_COUNT) {
        return;
    }
    s.result[step] = result;
    snprintf(s.detail[step], sizeof(s.detail[step]), "%s",
             detail != NULL ? detail : "");
    s.drawn_mask = 0;   /* a line changed; both buffers are stale */
}

/** @brief Whether every step has answered.
 *  @return true when none is pending */
static bool all_answered(void)
{
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        if (s.result[i] == SPLASH_PENDING) {
            return false;
        }
    }
    return true;
}

bool splash_screen_done(void)
{
    return s.skipped || (all_answered() && s.held_s >= HOLD_S);
}

/** @brief Time passed: the hold runs once every step has answered.
 *  @param dt_s The time since the last tick, in seconds. */
static void tick(float dt_s)
{
    if (all_answered()) {
        s.held_s += dt_s;
    }
}

/** @brief A touch: a tap skips the hold.
 *  @param evt The event. */
static void event(const touch_event_t *evt)
{
    /* A tap skips the hold, not the report: if a step failed it has already
     * been drawn, and the operator chose to move on. */
    if (evt != NULL && touch_event_is_tap_up(evt) && all_answered()) {
        s.skipped = true;
    }
}

/** @brief A result's colour.
 *  @param r The result.
 *  @return OK green, warning, danger, or faint while pending */
static gfx_color_t colour_of(splash_result_t r)
{
    switch (r) {
    case SPLASH_OK:   return ui_theme_color(UI_C_OK);
    case SPLASH_WARN: return ui_theme_color(UI_C_WARN);
    case SPLASH_FAIL: return ui_theme_color(UI_C_DANGER);
    default:          return ui_theme_color(UI_C_TEXT_FAINT);
    }
}

/** @brief A result's mark.
 *  @param r The result.
 *  @return "OK", "--", "!!", or ".." while pending */
static const char *mark_of(splash_result_t r)
{
    switch (r) {
    case SPLASH_OK:   return "OK";
    case SPLASH_WARN: return "--";
    case SPLASH_FAIL: return "!!";
    default:          return "..";
    }
}

/*
 * The splash on the whole panel:
 * The mark: the home tag's shape at twice its size, "rcbench" in
 * SP_FONT_MARK (20 x 36) at scale 2, centred from MARK_Y.  The tagline in
 * SP_FONT_BODY (12 x 24) centred under it.  The seven steps in rows of
 * ROW_H from LIST_Y, each its label, its mark and its detail in
 * SP_FONT_BODY; the detail 47 cells from DETAIL_X to 24px short of the
 * panel's edge.  The credit in SP_FONT_LABEL (10 x 20) at the foot.
 */
/** @brief The mark's face, drawn at MARK_SCALE: 20 x 36. */
#define SP_FONT_MARK  (&gfx_font_20x36)
/** @brief The tagline and the steps: 12 x 24. */
#define SP_FONT_BODY  UI_FONT_BODY
/** @brief The credit: 10 x 20. */
#define SP_FONT_LABEL (&gfx_font_10x20)
/** @brief The mark's scale: twice the home tag. */
#define MARK_SCALE    2
/** @brief The mark's top. */
#define MARK_Y        40
#define MARK_H        (MARK_SCALE * UI_TAG_H)     /**< 88px */
#define MARK_PAD      (MARK_SCALE * 12)          /**< text to either edge */
/** @brief The mark's chamfer, px. */
#define MARK_CHAMFER  (MARK_SCALE * UI_CHAMFER)
/** @brief The tagline's top. */
#define TAGLINE_Y     (MARK_Y + MARK_H + 20)
/** @brief A step's row, px. */
#define ROW_H         40
/** @brief The first step's top. */
#define LIST_Y        200
/** @brief The steps' names' left edge. */
#define LIST_X        192
#define MARK_X        (LIST_X + 180)            /**< a step's OK, --, !! */
/** @brief A step's detail's left edge. */
#define DETAIL_X      (LIST_X + 240)
/** @brief A step's detail's width: 47 cells of SP_FONT_BODY. */
#define DETAIL_W      (W - 24 - DETAIL_X)
/** @brief The credit's top. */
#define CREDIT_Y      (H - 36)

_Static_assert(LIST_Y + SPLASH_STEP_COUNT * ROW_H <= CREDIT_Y - 16,
               "the last step's row ends above the credit");

/**
 * @brief The mark: the home tag's chamfered shape at MARK_SCALE times its
 *        size, centred, with @p text on it.
 * @param c    Canvas, the whole panel.
 * @param text The word.
 * @param fill The shape's colour.
 */
static void draw_mark(gfx_canvas_t *c, const char *text, gfx_color_t fill)
{
    const int tw = gfx_text_width(SP_FONT_MARK, text, MARK_SCALE);
    const int w = MARK_PAD + tw + MARK_PAD;
    const int x = (W - w) / 2;
    gfx_fill_chamfer_rect_ex(c, x, MARK_Y, w, MARK_H, 0, MARK_CHAMFER, 0,
                             MARK_CHAMFER, fill);
    gfx_text(c, x + MARK_PAD,
             MARK_Y + (MARK_H - gfx_text_height(SP_FONT_MARK, MARK_SCALE)) / 2,
             text, SP_FONT_MARK, UI_TEXT_ON_LIGHT, MARK_SCALE);
}

/**
 * @brief The splash: the mark, the tagline and the credit once per
 *        framebuffer, the steps' rows every frame.
 * @param c            Canvas, the whole panel.
 * @param buffer_index The framebuffer drawn into.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int lh = gfx_text_height(SP_FONT_BODY, 1);

    /* The frame and the mark are painted once per framebuffer; the rows
     * are repainted every frame. */
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        draw_mark(c, "rcbench", ui_theme_color(UI_C_ACCENT));
        gfx_text_in(c, (gfx_rect_t){ 0, TAGLINE_Y, W, (int16_t)lh },
                    TR(SPLASH_TAGLINE), SP_FONT_BODY,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);
        /* The one credit line, at the bottom of the splash only. */
        gfx_text_in(c, (gfx_rect_t){ 0, CREDIT_Y, W,
                                     (int16_t)gfx_text_height(SP_FONT_LABEL,
                                                              1) },
                    TR(SPLASH_CREDIT), SP_FONT_LABEL,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        s.drawn_mask |= bit;
    }

    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        const int y = LIST_Y + i * ROW_H;
        const int ty = y + (ROW_H - 4 - lh) / 2;
        gfx_fill_rect(c, LIST_X, y, W - LIST_X, ROW_H - 4,
                      ui_theme_color(UI_C_BG));
        gfx_text(c, LIST_X, ty, ui_tr(k_labels[i]), SP_FONT_BODY,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, MARK_X, ty, mark_of(s.result[i]), SP_FONT_BODY,
                 colour_of(s.result[i]), 1);
        if (s.detail[i][0] != '\0') {
            gfx_text_in(c, gfx_rect_make(DETAIL_X, ty, DETAIL_W, lh),
                        s.detail[i], SP_FONT_BODY, ui_theme_color(UI_C_TEXT),
                        1, GFX_ALIGN_LEFT);
        }
    }
}

/** @brief The splash, as the router takes it. */
static const ui_screen_t k_screen = {
    .title  = "rcbench",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = tick,
    .event  = event,
    .render = render,
};

/** @brief The splash.
 *  @return its screen */
const ui_screen_t *splash_screen(void) { return &k_screen; }
