#include "stub_screen.h"

#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

typedef struct {
    const char *title;
    const char *what[4];   /**< what it will do; NULL-terminated          */
    const char *blocker;   /**< NULL when nothing is blocking it          */
} copy_t;

/*
 * The blocker line is the point of this screen.  Where it is NULL the note
 * turns green and says so -- "nothing is blocking this" is a claim worth
 * making loudly, because it is a claim somebody can act on this afternoon.
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
    [SCREEN_SERVO] = {
        "SERVO",
        { "Eight channels of pulse, any width and rate, narrow band included",
          "Sweep, step, centre, hold, manual and endpoint",
          "Width, frame rate and jitter measured; glitches and dropouts kept",
          "Behaviour across supply voltage, and the brown-out found on purpose" },
        "the coprocessor's PWM and capture are not written",
    },
    [SCREEN_ANALYSER] = {
        "ANALYSER",
        { "Decode a receiver bus and show the channels",
          "Watch a receiver fail: cut the transmitter, see what comes out",
          "Link quality and RSSI from the pilot's own system",
          "A raw view: bytes, gaps, errors, framing" },
        "one PIO soft UART, and then one parser at a time",
    },
    [SCREEN_LOGS] = {
        "LOGS",
        { "Record every channel to the card at up to 1 kHz",
          "Read it back with cursors and comparison",
          "Export a run as CSV or a report",
          NULL },
        NULL,   /* the card mounts and the CSV reader is ported and tested */
    },
    [SCREEN_SETUP] = {
        "SETUP",
        { "Pack, motor, output, interfaces and theme, persisted to NVS",
          "Socket ceilings: low and high voltage, each set in hardware",
          "Signal-only mode per socket, supply pin disconnected",
          "Save, compare and restore profiles on the card" },
        NULL,   /* the settings model is ported; the screen is being re-cut */
    },
    [SCREEN_BATTERY] = {
        "BATTERY",
        { "Cell voltages over the balance lead, 1 to 14S",
          "Internal resistance per cell and pack, under a switched load",
          "Capacity, sag and cell divergence, arranged as a verdict",
          "Smart-battery data: cycles, chemistry, error history" },
        "a cell-monitor part, and the INA228 is back-ordered into 2027",
    },
    [SCREEN_BALANCE] = {
        "BALANCE",
        { "Vibration spectrum from an accelerometer",
          "Phase from an index pulse on the same timebase -- which is the "
          "whole difficulty",
          "Where to add mass, and how much",
          NULL },
        "an accelerometer and an index pickup on the coprocessor",
    },
    [SCREEN_PROGRAMMER] = {
        "PROGRAMMER",
        { "BLHeli_S and AM32 over the one-wire bootloader at 19,200",
          "ESCape32 over its CLI, and VESC over framed packets",
          "Hitec D-series servos -- the one protocol published in full",
          "Programme four ESCs identically, and keep profiles on the card" },
        "for the rest: borrow the programmer and capture the wire",
    },
};

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
    for (int i = 0; i < 4 && k->what[i] != NULL; ++i) {
        gfx_fill_rect(c, 26, y + 7, 6, 6, ui_theme_color(UI_C_ACCENT));
        gfx_text(c, 44, y, k->what[i], &gfx_font_8x16,
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

/* One instance, re-pointed on the way in.  Eight near-identical screens would
 * be eight places for the same redraw bug. */
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
