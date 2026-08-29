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
    /*
     * The surface-angle line is measured with an accelerometer stuck to the
     * control surface, not an encoder on the servo's output shaft.  The
     * encoder tells you what the horn did; the accelerometer tells you what
     * the aeroplane got, linkage slop, horn geometry and pushrod flex
     * included.  Sub-0.1 degrees, limited by how repeatably it is mounted
     * rather than by the part.
     *
     * The gyro in the same package covers what the accelerometer cannot: it
     * gives rate through the transit, where the accelerometer is contaminated
     * by the very motion it is trying to measure.
     *
     * One thing to know before mounting one, because it costs an afternoon:
     * gravity gives two observable axes and never three.  Rotation *about*
     * the gravity vector is invisible, so a rudder on an upright model --
     * vertical hinge line -- reads exactly zero across its whole throw.  Lay
     * the model on its side and it reads like anything else.  Ailerons and
     * elevators on a level model are fine as they are.
     *
     * ------------------------------------------------------- the last two
     *
     * Both come from current, and both are about the servo *as installed*
     * rather than the servo as sold.
     *
     * FINDING THE LIMIT.  Endpoints set by eye in a transmitter are guesses,
     * and a servo held against a mechanical stop draws stall current for as
     * long as it is asked to: it cooks itself, empties the pack and wears the
     * gears, and nobody finds out until something strips.  The stop has a
     * signature -- current against commanded position is flat and low while
     * the surface is free, and climbs steeply the moment it binds.  Walk up
     * to the knee slowly, stop at the first rise rather than pushing through
     * it, back off by a margin, and programme *that* as the endpoint.
     *
     * It has to be measured in place: the limit belongs to the linkage, the
     * horn position and the surface stops, not to the servo, so it is
     * different in every installation and cannot be looked up.
     *
     * This one deliberately drives toward a stop, so it is the clearest case
     * for the coprocessor's rule of protecting hardware without asking:
     * current limit, stall timeout, and a slow approach.
     *
     * SYNCHRONISING TWO.  Two servos on one surface -- dual ailerons, elevator
     * halves -- fight each other through the surface whenever their travel or
     * centre disagree, and both draw extra current continuously to do it.  So
     * the objective is not a position at all: **the minimum of total current
     * is the point where they stop fighting**, which is a one-dimensional
     * search with a physical answer rather than a judgement.
     *
     * And the two errors separate cleanly, which saves searching blindly:
     * fighting at centre is an offset error, fighting at the extremes is a
     * travel error.  Measure at the centre and at both ends and each
     * correction falls out of its own measurement.
     *
     * The accelerometer adds the one thing current cannot say -- whether they
     * agree and are *both* wrong, or disagree.
     *
     * Both want a current sensor per output, which the catalogue costs as
     * medium: one sensor per channel, or a multiplexer.
     */
    /* Reached only if the log viewer is ever unrouted; it is built. */
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
    /*
     * Not the same sensor as the servo bench uses, and the reason is worth
     * writing down because the parts look interchangeable in a catalogue.
     *
     * A packaged serial IMU -- 9 axes, onboard fusion, RS232 on a lead -- is
     * ideal for a surface angle and useless here.  Balancing is a *phase*
     * measurement: the answer is an angle, "add mass there".  A prop at
     * 10,000 rpm turns once every 6 ms, and serial framing plus fusion group
     * delay is easily 5 ms and neither constant nor published.  That is 300
     * degrees of error.  Even a helicopter head at 2,000 rpm, one turn every
     * 30 ms, lands 60 degrees out.  The mass goes on with great precision, in
     * the wrong place.
     *
     * So: an analogue accelerometer or piezo on a shielded lead, sampled by
     * the coprocessor's own converter in lockstep with the index capture.
     * One timebase because the sampling happens at the instrument rather than
     * at the sensor.  The converter's 7.5 effective bits are poor for
     * absolute measurement and ample for relative amplitude and phase
     * averaged over many revolutions.
     */
    [SCREEN_BALANCE] = {
        "BALANCE",
        { "Vibration spectrum from an accelerometer on a shielded lead",
          "Phase against an index pulse on one timebase -- the whole difficulty",
          "Sampled at the instrument: a serial IMU cannot hold that phase",
          "Where to add mass, and how much" },
        "an analogue accelerometer and an index pickup on the coprocessor",
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
