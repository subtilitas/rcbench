/*
 * SPDX-License-Identifier: MIT
 */

#include "stub_screen.h"

#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
/* Where the copy starts, and the margin it must leave on the right. */
#define COPY_X 44
#define COPY_R 24
#define H (480 - UI_BAND_H)

typedef struct {
    const char  *title;    /**< the screen's title: English always        */
    ui_text_id_t what[6];  /**< what it will do; TX_COUNT-terminated      */
    ui_text_id_t blocker;  /**< TX_COUNT when nothing is blocking it      */
} copy_t;

/*
 * Each entry lists what the screen will do and the one thing that blocks it.
 * With a NULL blocker the note turns green.
 */
static const copy_t k_copy[SCREEN_COUNT] = {
    [SCREEN_MOTOR] = {
        "MOTOR & ESC",
        { TX_STUB_MOTOR_1, TX_STUB_MOTOR_2, TX_STUB_MOTOR_3, TX_STUB_MOTOR_4,
          TX_COUNT, TX_COUNT },
        TX_STUB_MOTOR_WHY,
    },
    /*
     * The servo limit and synchronisation procedures are in shared/servo/ and
     * documented in docs/Servo.md.  A surface angle measured with an
     * accelerometer on the control surface has two observable axes: rotation
     * about the gravity vector is not observable, so a vertical hinge line (a
     * rudder on an upright model) reads zero across its whole throw.
     */
    /* Reached only if the log viewer is unrouted. */
    [SCREEN_LOGS] = {
        "LOGS",
        { TX_STUB_LOGS_1, TX_STUB_LOGS_2, TX_STUB_LOGS_3, TX_COUNT, TX_COUNT,
          TX_COUNT },
        TX_COUNT,   /* nothing blocks it */
    },
    [SCREEN_SETUP] = {
        "SETUP",
        { TX_STUB_SETUP_1, TX_STUB_SETUP_2, TX_STUB_SETUP_3, TX_STUB_SETUP_4,
          TX_COUNT, TX_COUNT },
        TX_COUNT,   /* nothing blocks it */
    },
};

const char *const *stub_copy_lines(ui_screen_id_t id)
{
    /* The lines in the language showing, NULL-terminated, until the next
     * call. */
    static const char *lines[7];
    if (id < 0 || id >= SCREEN_COUNT || k_copy[id].title == NULL) {
        return NULL;
    }
    int n = 0;
    for (; n < 6 && k_copy[id].what[n] != TX_COUNT; ++n) {
        lines[n] = ui_tr(k_copy[id].what[n]);
    }
    lines[n] = NULL;
    return lines;
}

const char *stub_copy_blocker(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT || k_copy[id].title == NULL
        || k_copy[id].blocker == TX_COUNT) {
        return NULL;
    }
    return ui_tr(k_copy[id].blocker);
}

static struct {
    ui_screen_id_t id;
    unsigned       drawn_mask;
} s;

void stub_invalidate(void) { s.drawn_mask = 0; }

static void reset(void) { s.drawn_mask = 0; }

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((s.drawn_mask & bit) != 0) {
        return;
    }
    s.drawn_mask |= bit;

    const copy_t *k = &k_copy[s.id];
    gfx_clear(c, ui_theme_color(UI_C_BG));

    gfx_text(c, 24, 20, TR(STUB_WHAT), &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, 24, 44, W - 48, ui_theme_color(UI_C_EDGE));

    int y = 64;
    for (int i = 0; i < 6 && k->what[i] != TX_COUNT; ++i) {
        gfx_fill_rect(c, 26, y + 7, 6, 6, ui_theme_color(UI_C_ACCENT));
        gfx_text(c, COPY_X, y, ui_tr(k->what[i]), &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        y += 34;
    }

    const int note_y = H - 96;
    const bool blocked = (k->blocker != TX_COUNT);
    const gfx_color_t accent = blocked ? ui_theme_color(UI_C_WARN)
                                       : ui_theme_color(UI_C_OK);

    gfx_fill_round_rect(c, 24, note_y, W - 48, 64, 6,
                        ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, 24, note_y, 4, 64, accent);
    gfx_text(c, 44, note_y + 12,
             blocked ? TR(STUB_WAITING) : TR(STUB_CLEAR),
             &gfx_font_8x16, accent, 1);
    gfx_text(c, 44, note_y + 34,
             blocked ? ui_tr(k->blocker) : TR(STUB_BUILT),
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1);
}

static const ui_screen_t k_screen = {
    .title  = "",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = NULL,
    .render = render,
};

/* One instance, re-pointed at the selected copy on the way in. */
static ui_screen_t s_instance;

const ui_screen_t *stub_screen(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT || k_copy[id].title == NULL) {
        return NULL;
    }
    if (s.id != id) {
        s.id = id;
        s.drawn_mask = 0;
    }
    s_instance = k_screen;
    s_instance.title = k_copy[id].title;
    return &s_instance;
}
