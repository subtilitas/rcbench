/*
 * SPDX-License-Identifier: MIT
 */

#include "stub_screen.h"

#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
/* Where the copy starts, and the margin it must leave on the right. */
#define COPY_X 44
#define COPY_R 24
#define H (480 - UI_BAND_H)

typedef struct {
    const char *title;
    const char *what[6];   /**< what it will do; NULL-terminated          */
    const char *blocker;   /**< NULL when nothing is blocking it          */
} copy_t;

/*
 * Each entry lists what the screen will do and the one thing that blocks it.
 * With a NULL blocker the note turns green.
 */
static const copy_t k_copy[SCREEN_COUNT] = {
    [SCREEN_MOTOR] = {
        "MOTOR & ESC",
        { "Throttle: pulse, OneShot, MultiShot, DShot at every rate",
          "Volts, amps, watts, charge and energy, with peaks and sag",
          "Rpm from an optical or magnetic pickup, a phase clip, or the ESC",
          "KISS telemetry decoded, and limits that cut out without asking" },
        "the bench page is not defined yet; the link that carries it is",
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
        { "Record every channel to the card at up to 1 kHz",
          "Read it back with cursors and comparison",
          "Export a run as CSV or a report",
          NULL },
        NULL,   /* nothing blocks it */
    },
    [SCREEN_SETUP] = {
        "SETUP",
        { "Pack, motor, output, interfaces and theme, persisted to NVS",
          "Socket ceilings: low and high voltage, each set in hardware",
          "Signal-only mode per socket, supply pin disconnected",
          "Save, compare and restore profiles on the card" },
        NULL,   /* nothing blocks it */
    },
};

const char *const *stub_copy_lines(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT || k_copy[id].title == NULL) {
        return NULL;
    }
    return k_copy[id].what;
}

const char *stub_copy_blocker(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT) {
        return NULL;
    }
    return k_copy[id].blocker;
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

    gfx_text(c, 24, 20, "WHAT THIS WILL DO", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, 24, 44, W - 48, ui_theme_color(UI_C_EDGE));

    int y = 64;
    for (int i = 0; i < 6 && k->what[i] != NULL; ++i) {
        gfx_fill_rect(c, 26, y + 7, 6, 6, ui_theme_color(UI_C_ACCENT));
        gfx_text(c, COPY_X, y, k->what[i], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        y += 34;
    }

    const int note_y = H - 96;
    const bool blocked = (k->blocker != NULL);
    const gfx_color_t accent = blocked ? ui_theme_color(UI_C_WARN)
                                       : ui_theme_color(UI_C_OK);

    gfx_fill_round_rect(c, 24, note_y, W - 48, 64, 6,
                        ui_theme_color(UI_C_PANEL));
    gfx_fill_rect(c, 24, note_y, 4, 64, accent);
    gfx_text(c, 44, note_y + 12,
             blocked ? "WAITING ON" : "NOTHING IS BLOCKING THIS",
             &gfx_font_8x16, accent, 1);
    gfx_text(c, 44, note_y + 34,
             blocked ? k->blocker
                     : "the parts it needs are built and tested",
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
