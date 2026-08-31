/*
 * SPDX-License-Identifier: MIT
 */

#include "splash_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/* Long enough to read seven lines, short enough not to be in the way.  A tap
 * skips it -- but a failure still has to be *reported*, not survived: you must
 * be able to read it and move on, rather than be stranded here. */
#define HOLD_S 1.6f

static const char *const k_labels[SPLASH_STEP_COUNT] = {
    "BOARD", "DISPLAY", "TOUCH", "STORAGE", "SETTINGS", "LINK", "COPROCESSOR",
};

static struct {
    splash_result_t result[SPLASH_STEP_COUNT];
    char            detail[SPLASH_STEP_COUNT][24];
    float           held_s;
    bool            skipped;
    unsigned        drawn_mask;   /**< per framebuffer, bit per buffer      */
} s;

void splash_invalidate(void) { s.drawn_mask = 0; }

static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

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

static void tick(float dt_s)
{
    if (all_answered()) {
        s.held_s += dt_s;
    }
}

static void event(const touch_event_t *evt)
{
    /* A tap skips the hold, not the report: if a step failed it has already
     * been drawn, and the operator chose to move on. */
    if (evt != NULL && evt->type == TOUCH_EVENT_UP && all_answered()) {
        s.skipped = true;
    }
}

static gfx_color_t colour_of(splash_result_t r)
{
    switch (r) {
    case SPLASH_OK:   return ui_theme_color(UI_C_OK);
    case SPLASH_WARN: return ui_theme_color(UI_C_WARN);
    case SPLASH_FAIL: return ui_theme_color(UI_C_DANGER);
    default:          return ui_theme_color(UI_C_TEXT_FAINT);
    }
}

static const char *mark_of(splash_result_t r)
{
    switch (r) {
    case SPLASH_OK:   return "OK";
    case SPLASH_WARN: return "--";
    case SPLASH_FAIL: return "!!";
    default:          return "..";
    }
}

#define ROW_H  34
#define LIST_Y 168
#define LIST_X 200

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);

    /* The frame and the wordmark never change; the rows do.  Painting only
     * what moved is the whole reason render() is handed a buffer index. */
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_wordmark(c, "rcbench", ui_theme_color(UI_C_ACCENT));
        gfx_text_in(c, (gfx_rect_t){ 0, 108, W, 24 },
                    "motor, ESC and servo test bench", &gfx_font_8x16,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_CENTER);
        /* One credit, in one place, at the bottom of the one screen nobody
         * is working on when they read it.  Not on every commit, not in the
         * docs, not in a header banner. */
        gfx_text_in(c, (gfx_rect_t){ 0, H - 34, W, 20 },
                    "built with Claude Code", &gfx_font_8x16,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_CENTER);
        s.drawn_mask |= bit;
    }

    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        const int y = LIST_Y + i * ROW_H;
        gfx_fill_rect(c, LIST_X, y, 400, ROW_H - 4,
                      ui_theme_color(UI_C_BG));
        gfx_text(c, LIST_X, y + 6, k_labels[i], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, LIST_X + 120, y + 6, mark_of(s.result[i]),
                 &gfx_font_8x16, colour_of(s.result[i]), 1);
        if (s.detail[i][0] != '\0') {
            gfx_text(c, LIST_X + 160, y + 6, s.detail[i], &gfx_font_8x16,
                     ui_theme_color(UI_C_TEXT), 1);
        }
    }
}

static const ui_screen_t k_screen = {
    .title  = "rcbench",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = tick,
    .event  = event,
    .render = render,
};

const ui_screen_t *splash_screen(void) { return &k_screen; }
