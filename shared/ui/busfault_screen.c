/**
 * @file
 * @brief busfault_screen.h says what this screen is for.
 *
 * SPDX-License-Identifier: MIT
 */

#include "busfault_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/*
 * The faces of the type scale this screen draws in:
 * BF_FONT_HEAD (20 x 36) for the bar's title and the verdict, BF_FONT_BODY
 * (12 x 24) for the meaning, the checks and the notes, BF_FONT_LABEL
 * (10 x 20) for the headings, the number column and the button.
 */
/** @brief The bar's title and the verdict: 20 x 36. */
#define BF_FONT_HEAD  (&gfx_font_20x36)
/** @brief The meaning, the checks and the notes: 12 x 24. */
#define BF_FONT_BODY  UI_FONT_BODY
/** @brief The headings, the number column and the button: 10 x 20. */
#define BF_FONT_LABEL (&gfx_font_10x20)
#define BODY_CELL     12              /**< BF_FONT_BODY's cell width, px */

/*
 * The red bar across the top, and the verdict under it:
 * The bar is 64px tall; the verdict runs across the whole width: 48 cells
 * of BF_FONT_HEAD.
 */
/** @brief The red bar's height, px. */
#define BAR_H     64
/** @brief The verdict's top. */
#define VERDICT_Y (BAR_H + 12)

/*
 * The two columns under the verdict:
 * What it means and what to do on the left, 712px: 59 cells of
 * BF_FONT_BODY, its text wrapped; what was measured on the right, 240px:
 * 24 cells of BF_FONT_LABEL.
 */
/** @brief The two columns' top. */
#define COLS_Y    (VERDICT_Y + 48)
#define COL_X     24
/** @brief The number column's width, px: 24 cells of BF_FONT_LABEL. */
#define NUM_W     240
#define NUM_X     (W - 24 - NUM_W)
/** @brief The left column's width, px. */
#define COL_W     (NUM_X - 24 - COL_X)
/** @brief The left column's cells of BF_FONT_BODY. */
#define COL_CELLS (COL_W / BODY_CELL)

/*
 * Line pitches:
 * A wrapped line's pitch in BF_FONT_BODY; the right column's line, heading
 * and section steps in BF_FONT_LABEL.
 */
/** @brief A wrapped BF_FONT_BODY line's pitch, px. */
#define WRAP_PITCH 28
/** @brief The number column's line pitch, px. */
#define NUM_PITCH  22
/** @brief The step under a number column heading, px. */
#define NUM_HEAD   24
/** @brief The step between two number column sections, px. */
#define NUM_GAP    28

/*
 * The meaning, the list and the notes:
 * The meaning takes up to two lines from COLS_Y; the heading in
 * BF_FONT_LABEL under it; up to LIST_MAX checks 32px apart, a check wider
 * than the column wrapping under itself at WRAP_PITCH; the three notes from
 * NOTE_Y, which the fullest list (five checks, two of them wrapped) ends
 * above.
 */
/** @brief The lines the meaning takes at most. */
#define MEANING_LINES 2
#define HEAD_Y    (COLS_Y + MEANING_LINES * WRAP_PITCH + 8)
/** @brief The first check's top. */
#define LIST_Y    (HEAD_Y + 28)
/** @brief The step from one check to the next, px. */
#define LIST_PITCH 32
#define LIST_MAX  5
/** @brief A check's text right of its bullet, px. */
#define LIST_INDENT 16
/** @brief A check's cells of BF_FONT_BODY. */
#define LIST_CELLS ((COL_W - LIST_INDENT) / BODY_CELL)
/** @brief The first note's top: under the fullest list. */
#define NOTE_Y    (LIST_Y + LIST_MAX * LIST_PITCH + 2 * WRAP_PITCH)

/*
 * The acknowledgement, across the bottom:
 * 976 x 56px, 16px above the panel's edge; the rule 8px over it.
 */
/** @brief The acknowledgement's height, px. */
#define ACK_H     56
/** @brief The acknowledgement's top, 16px above the panel's edge. */
#define ACK_Y     (H - ACK_H - 16)
#define ACK_X     COL_X
#define ACK_W     (W - 2 * COL_X)
/** @brief The rule over the acknowledgement. */
#define RULE_Y    (ACK_Y - 8)

_Static_assert(ACK_H >= UI_TAP_MIN, "the acknowledgement is a finger tall");
_Static_assert(NOTE_Y + 2 * WRAP_PITCH + 24 <= RULE_Y,
               "the last note ends above the rule");
_Static_assert(COLS_Y + 3 * NUM_HEAD + 10 * NUM_PITCH + 2 * NUM_GAP + 20
                   <= RULE_Y,
               "the fullest number column ends above the rule");

static struct {
    busfault_report_t r;
    bool              have;

    float    held_s;      /**< how long the acknowledgement has been held */
    bool     fired;       /**< the hold completed; do not repeat it       */
    int      flash_ms;    /**< ms of the flash still to run               */
    bool     ack;         /**< latched for the application                */
    bool     pressed;     /**< the contact is on the button            */
    uint8_t  press_id;    /**< the contact that owns the press         */
    bool     have_press;  /**< a contact owns the press                */

    unsigned drawn_mask;  /**< the static half, per framebuffer */
} s;  /**< the report and the hold */

void busfault_screen_invalidate(void) { s.drawn_mask = 0; }

static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

void busfault_screen_set(const busfault_report_t *r)
{
    if (r == NULL) {
        memset(&s.r, 0, sizeof(s.r));
        s.have = false;
    } else {
        s.r = *r;
        s.have = true;
    }
    s.held_s = 0.0f;
    s.fired = false;
    s.flash_ms = 0;
    s.drawn_mask = 0;
}

const busfault_report_t *busfault_screen_report(void)
{
    return s.have ? &s.r : NULL;
}

bool busfault_screen_take_ack(void)
{
    const bool was = s.ack;
    s.ack = false;
    return was;
}

/* ------------------------------------------------------------ what to say */

/*
 * One line of what the verdict means, in the operator's terms rather than:
 * the controller's.  can_selftest_text() says what the test saw; this says
 * what it is.
 */
/*
 * The heading, which is the phrase an operator repeats when they ask somebody:
 * about it.  For a link that dropped it is the controller's own state: the
 * wire carried frames a moment ago, so what happened since is this end's
 * business and not the bench's.
 */
static const char *bus_text(busfault_bus_t b)
{
    switch (b) {
    case BUSFAULT_BUS_OFF:        return TR(BF_BUS_OFF_TEXT);
    case BUSFAULT_BUS_RECOVERING: return TR(BF_BUS_REJOIN_TEXT);
    case BUSFAULT_BUS_STOPPED:    return TR(BF_BUS_STOPPED_TEXT);
    case BUSFAULT_BUS_RUNNING:    return TR(BF_BUS_RUNNING_TEXT);
    case BUSFAULT_BUS_UNKNOWN:
    default:                      return TR(BF_BUS_UNKNOWN_TEXT);
    }
}

/* The same state as one word, for the column: the heading already carries
 * the sentence, and a column that repeats it says nothing twice. */
static const char *bus_word(busfault_bus_t b)
{
    switch (b) {
    case BUSFAULT_BUS_OFF:        return "BUS OFF";   /* the CAN state's name */
    case BUSFAULT_BUS_RECOVERING: return TR(BF_BUS_REJOIN_WORD);
    case BUSFAULT_BUS_STOPPED:    return TR(BF_BUS_STOPPED_WORD);
    case BUSFAULT_BUS_RUNNING:    return TR(BF_BUS_RUNNING_WORD);
    case BUSFAULT_BUS_UNKNOWN:
    default:                      return TR(BF_BUS_UNKNOWN_WORD);
    }
}

static const char *bus_meaning(busfault_bus_t b)
{
    switch (b) {
    case BUSFAULT_BUS_OFF:        return TR(BF_BUS_OFF_MEANS);
    case BUSFAULT_BUS_RECOVERING: return TR(BF_BUS_REJOIN_MEANS);
    case BUSFAULT_BUS_STOPPED:    return TR(BF_BUS_STOPPED_MEANS);
    case BUSFAULT_BUS_RUNNING:    return TR(BF_BUS_RUNNING_MEANS);
    case BUSFAULT_BUS_UNKNOWN:
    default:                      return TR(BF_BUS_UNKNOWN_MEANS);
    }
}

static int bus_checks(busfault_bus_t b, const char *out[LIST_MAX])
{
    switch (b) {
    case BUSFAULT_BUS_OFF:
    case BUSFAULT_BUS_RECOVERING:
        out[0] = TR(BF_REJOIN_1);
        out[1] = TR(BF_REJOIN_2);
        out[2] = TR(BF_REJOIN_3);
        return 3;
    case BUSFAULT_BUS_RUNNING:
        out[0] = TR(BF_RUNNING_1);
        out[1] = TR(BF_RUNNING_2);
        out[2] = TR(BF_RUNNING_3);
        return 3;
    case BUSFAULT_BUS_STOPPED:
    case BUSFAULT_BUS_UNKNOWN:
    default:
        out[0] = TR(BF_STOPPED_1);
        out[1] = TR(BF_STOPPED_2);
        out[2] = TR(BF_STOPPED_3);
        return 3;
    }
}

static const char *meaning_of(can_selftest_verdict_t v)
{
    switch (v) {
    case CAN_SELFTEST_SILENT:  return TR(BF_SILENT_MEANS);
    case CAN_SELFTEST_CORRUPT: return TR(BF_CORRUPT_MEANS);
    case CAN_SELFTEST_LOSSY:   return TR(BF_LOSSY_MEANS);
    case CAN_SELFTEST_DROPPED: return TR(BF_DROPPED_MEANS);
    default:                   return "";
    }
}

/*
 * What to check, cheapest first: a list that starts with the meter is a list:
 * nobody finishes.
 *
 * Terminators are last under SILENT and first under CORRUPT on purpose. A bus
 * with no terminator at all usually still carries frames over half a metre;
 * one with a terminator missing reflects, and reflection is what alters a
 * frame rather than stopping it.
 */
static int checks_for(can_selftest_verdict_t v, const char *out[LIST_MAX])
{
    switch (v) {
    case CAN_SELFTEST_SILENT:
        out[0] = TR(BF_SILENT_1);
        out[1] = TR(BF_SILENT_2);
        out[2] = TR(BF_SILENT_3);
        out[3] = TR(BF_SILENT_4);
        out[4] = TR(BF_SILENT_5);
        return 5;
    case CAN_SELFTEST_CORRUPT:
        out[0] = TR(BF_CORRUPT_1);
        out[1] = TR(BF_CORRUPT_2);
        out[2] = TR(BF_CORRUPT_3);
        out[3] = TR(BF_TWISTED);
        return 4;
    case CAN_SELFTEST_LOSSY:
        out[0] = TR(BF_LOSSY_1);
        out[1] = TR(BF_LOSSY_2);
        out[2] = TR(BF_TWISTED);
        out[3] = TR(BF_LOSSY_4);
        return 4;
    case CAN_SELFTEST_DROPPED:
        out[0] = TR(BF_DROPPED_1);
        out[1] = TR(BF_DROPPED_2);
        out[2] = TR(BF_DROPPED_3);
        return 3;
    default:
        return 0;
    }
}

const char *busfault_verdict_text(can_selftest_verdict_t v)
{
    switch (v) {
    case CAN_SELFTEST_OK:      return TR(CAN_OK);
    case CAN_SELFTEST_SILENT:  return TR(CAN_SILENT);
    case CAN_SELFTEST_CORRUPT: return TR(CAN_CORRUPT);
    case CAN_SELFTEST_LOSSY:   return TR(CAN_LOSSY);
    case CAN_SELFTEST_DROPPED: return TR(CAN_DROPPED);
    case CAN_SELFTEST_RUNNING:
    default:                   return TR(CAN_RUNNING);
    }
}

/* ------------------------------------------------------------------ input */

static bool in_ack(int16_t x, int16_t y)
{
    return gfx_rect_contains(gfx_rect_make(ACK_X, ACK_Y, ACK_W, ACK_H), x, y);
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        if (!s.have_press && in_ack(evt->point.x, evt->point.y)) {
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = true;
            s.held_s     = 0.0f;
            s.fired      = false;
        }
        break;
    case TOUCH_EVENT_MOVE:
        /*
         * A finger that slides off the button abandons the hold: the gesture
         * is contact with the control, not with the panel.  ARM and the servo
         * screen's hold do the same through ui_hold_leave(), so the three
         * cannot come to behave differently.
         */
        if (s.have_press && evt->point.id == s.press_id
            && !in_ack(evt->point.x, evt->point.y)) {
            s.pressed = false;
            s.held_s  = 0.0f;
        }
        break;
    case TOUCH_EVENT_UP:
    default:
        if (s.have_press && evt->point.id == s.press_id) {
            s.have_press = false;
            s.pressed    = false;
            s.held_s     = 0.0f;
        }
        break;
    }
}

/**
 * @brief Time passed: the acknowledge flash runs on by @p dt_s.
 * @param dt_s The time since the last tick, in seconds.
 */
static void tick(float dt_s)
{
    s.flash_ms = ui_hold_flash_left(s.flash_ms, dt_s);
    if (s.pressed && !s.fired) {
        s.held_s += ui_hold_credit(dt_s);
        if (s.held_s >= UI_HOLD_S) {
            /*
             * The hold is the acknowledgement: it completes here rather than
             * on the release, so letting go early acknowledges nothing.
             */
            s.held_s     = UI_HOLD_S;
            s.fired      = true;
            s.ack        = true;
            s.flash_ms   = UI_HOLD_FLASH_MS;
        }
    }
}

/* ---------------------------------------------------------------- drawing */

/* The bytes of the next line of @p at that fit @p cells: up to the last
 * space in it, or the whole width where no space falls after its start. */
static size_t line_len(const char *at, int cells)
{
    if (gfx_text_cells(at) <= cells) {
        return strlen(at);
    }
    const size_t len = gfx_text_prefix(at, cells);
    size_t cut = len;
    while (cut > 0u && at[cut] != ' ') {
        --cut;
    }
    return (cut > 0u) ? cut : len;
}

/**
 * @brief @p text in lines of at most @p cells of BF_FONT_BODY, broken after
 *        a space, from (x, y) down by WRAP_PITCH.
 * @param c     Canvas, the whole panel.
 * @param x     The lines' left edge.
 * @param y     The first line's top.
 * @param text  The text.
 * @param cells The cells a line holds.
 * @param ink   Its colour.
 * @return The lines drawn.
 */
static int wrap_text(gfx_canvas_t *c, int x, int y, const char *text,
                     int cells, gfx_color_t ink)
{
    int lines = 0;
    const char *at = text;
    while (*at != '\0') {
        char buf[128];
        size_t len = line_len(at, cells);
        const size_t n = (len < sizeof(buf)) ? len : sizeof(buf) - 1u;
        memcpy(buf, at, n);
        buf[n] = '\0';
        gfx_text(c, x, y + lines * WRAP_PITCH, buf, BF_FONT_BODY, ink, 1);
        ++lines;
        at += len;
        while (*at == ' ') {
            ++at;
        }
    }
    return lines;
}

/**
 * @brief The acknowledge button's fill.
 *
 * Danger red while it is waiting to be held, fading to the OK green it
 * settles on, and the same 150ms flash ARM gives as the command goes.
 * The direction is the other way round from ARM because what it does is the
 * other way round: this one leaves a fault behind rather than entering one.
 * @return The fill for this frame.
 */
static gfx_color_t ack_fill(void)
{
    if (s.flash_ms > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_OK), s.flash_ms);
    }
    return ui_hold_fill(ui_theme_color(UI_C_DANGER),
                        ui_theme_color(UI_C_OK), s.held_s);
}

/**
 * @brief The right-hand column for a link that dropped: this end's
 *        controller, and how long it has been quiet.
 *
 * Different numbers from the start-up test's -- there was no echo run, so
 * there is nothing to count probes against.
 * @param c Canvas, the whole panel.
 */
static void draw_link_numbers(gfx_canvas_t *c)
{
    int y = COLS_Y;
    char line[48];

    gfx_text(c, NUM_X, y, TR(BF_THIS_PANEL), BF_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += NUM_HEAD;

    snprintf(line, sizeof(line), TR(BF_DOWN), (unsigned long)s.r.down_s);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_WARN), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_REQUESTS), (unsigned long)s.r.polls);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_NO_ANSWER),
             (unsigned long)s.r.timeouts);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.timeouts > 0 ? ui_theme_color(UI_C_WARN)
                              : ui_theme_color(UI_C_TEXT), 1);
    y += NUM_GAP;

    gfx_text(c, NUM_X, y, "CONTROLLER", BF_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += NUM_HEAD;
    gfx_text(c, NUM_X, y, bus_word(s.r.bus), BF_FONT_LABEL,
             s.r.bus == BUSFAULT_BUS_RUNNING ? ui_theme_color(UI_C_OK)
                                             : ui_theme_color(UI_C_DANGER), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), "tx err %lu  rx err %lu",
             (unsigned long)s.r.tx_errors, (unsigned long)s.r.rx_errors);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.tx_errors >= 128u ? ui_theme_color(UI_C_DANGER)
                                   : ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), "bus err %lu",
             (unsigned long)s.r.bus_errors);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_REJOINS),
             (unsigned long)s.r.recoveries);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.recoveries > 0 ? ui_theme_color(UI_C_WARN)
                                : ui_theme_color(UI_C_TEXT), 1);
}

/**
 * @brief The right-hand column for the start-up test: the probes, this
 *        panel's controller and the IOmcu's.
 * @param c Canvas, the whole panel.
 */
static void draw_numbers(gfx_canvas_t *c)
{
    int y = COLS_Y;
    gfx_text(c, NUM_X, y, TR(BF_MEASURED), BF_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += NUM_HEAD;

    char line[48];
    snprintf(line, sizeof(line), TR(BF_SENT), (unsigned long)s.r.sent);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_RETURNED), (unsigned long)s.r.echoed);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.echoed > 0 ? ui_theme_color(UI_C_OK)
                            : ui_theme_color(UI_C_DANGER), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_ALTERED), (unsigned long)s.r.corrupt);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.corrupt > 0 ? ui_theme_color(UI_C_DANGER)
                             : ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), TR(BF_LOST), (unsigned long)s.r.lost);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
             s.r.lost > 0 ? ui_theme_color(UI_C_WARN)
                          : ui_theme_color(UI_C_TEXT), 1);
    y += NUM_GAP;

    gfx_text(c, NUM_X, y, TR(BF_THIS_PANEL), BF_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += NUM_HEAD;
    snprintf(line, sizeof(line), "tx err %lu  rx err %lu",
             (unsigned long)s.r.tx_errors, (unsigned long)s.r.rx_errors);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), "bus err %lu", (unsigned long)s.r.bus_errors);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    if (s.r.bus_off) {
        gfx_text(c, NUM_X, y, "BUS OFF", BF_FONT_LABEL,
                 ui_theme_color(UI_C_DANGER), 1);
    }
    y += NUM_GAP;

    gfx_text(c, NUM_X, y, TR(BF_COPROCESSOR), BF_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    y += NUM_HEAD;
    if (!s.r.have_remote) {
        /*
         * Not an error line of its own: a far end that cannot be asked is
         * already the whole of what SILENT says, and repeating it in red
         * would read as a second fault.
         */
        gfx_text(c, NUM_X, y, TR(BF_NO_REPLY), BF_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        return;
    }
    gfx_text(c, NUM_X, y, s.r.remote_up ? TR(BF_CAN_UP) : TR(BF_CAN_DOWN),
             BF_FONT_LABEL,
             s.r.remote_up ? ui_theme_color(UI_C_OK)
                           : ui_theme_color(UI_C_DANGER), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), "tx err %u  rx err %u",
             (unsigned)s.r.remote_tx_errors, (unsigned)s.r.remote_rx_errors);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    snprintf(line, sizeof(line), "flags 0x%02X", (unsigned)s.r.remote_flags);
    gfx_text(c, NUM_X, y, line, BF_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    y += NUM_PITCH;
    if (s.r.remote_overflows > 0u) {
        snprintf(line, sizeof(line), TR(BF_DROPPED), s.r.remote_overflows);
        gfx_text(c, NUM_X, y, line, BF_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
}

/**
 * @brief The screen: the bar, the verdict, both columns and the notes once
 *        per framebuffer, the acknowledgement every frame.
 * @param c            Canvas, the whole panel.
 * @param buffer_index The framebuffer drawn into.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));

        /* The heading, on the danger colour: this is the one screen that
         * says the bench is not usable as it stands. */
        const bool lost = (s.r.kind == BUSFAULT_LINK_LOST);

        gfx_fill_rect(c, 0, 0, W, BAR_H, ui_theme_color(UI_C_DANGER));
        const int hh = gfx_text_height(BF_FONT_HEAD, 1);
        gfx_text(c, COL_X, (BAR_H - hh) / 2,
                 lost ? "LINK LOST" : "CAN BUS FAULT", BF_FONT_HEAD,
                 GFX_WHITE, 1);

        /* The verdict is the heading of the page, not a caption: it is what
         * the operator repeats when they ask somebody about it. */
        gfx_text(c, COL_X, VERDICT_Y,
                 lost ? bus_text(s.r.bus) : busfault_verdict_text(s.r.verdict),
                 BF_FONT_HEAD, ui_theme_color(UI_C_TEXT), 1);
        (void)wrap_text(c, COL_X, COLS_Y,
                        lost ? bus_meaning(s.r.bus) : meaning_of(s.r.verdict),
                        COL_CELLS, ui_theme_color(UI_C_TEXT_DIM));

        gfx_text(c, COL_X, HEAD_Y,
                 lost ? TR(BF_HEAD_MEANS) : TR(BF_HEAD_CHECK),
                 BF_FONT_LABEL, ui_theme_color(UI_C_TEXT_FAINT), 1);

        const char *checks[LIST_MAX] = { 0 };
        const int n = lost ? bus_checks(s.r.bus, checks)
                           : checks_for(s.r.verdict, checks);
        int y = LIST_Y;
        for (int i = 0; i < n; ++i) {
            /* A continuation line is indented and carries no bullet: it is
             * the same instruction, not the next one.  An item wider than
             * the column wraps under itself at WRAP_PITCH. */
            const bool cont = (checks[i][0] == ' ');
            if (!cont) {
                gfx_fill_rect(c, COL_X + 2, y + 9, 6, 6,
                              ui_theme_color(UI_C_ACCENT));
            }
            const int lines = wrap_text(c, COL_X + LIST_INDENT, y, checks[i],
                                        LIST_CELLS,
                                        cont ? ui_theme_color(UI_C_TEXT_DIM)
                                             : ui_theme_color(UI_C_TEXT));
            y += LIST_PITCH + (lines - 1) * WRAP_PITCH;
        }

        /*
         * And what acknowledging costs, because the button is not a
         * dismissal: the bench is still on a bus that does not work.
         */
        const int lh = gfx_text_height(BF_FONT_BODY, 1);
        gfx_text_in(c, gfx_rect_make(COL_X, NOTE_Y, ACK_W, lh), TR(BF_NOTE_1),
                    BF_FONT_BODY, ui_theme_color(UI_C_TEXT_DIM), 1,
                    GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(COL_X, NOTE_Y + WRAP_PITCH, ACK_W, lh),
                    TR(BF_NOTE_2), BF_FONT_BODY,
                    ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_LEFT);
        /* The last note may run under the number column: the link's
         * column ends higher, and the test's last note is shorter. */
        gfx_text_in(c,
                    gfx_rect_make(COL_X, NOTE_Y + 2 * WRAP_PITCH, ACK_W, lh),
                    lost ? TR(BF_NOTE_LOST) : TR(BF_NOTE_TEST), BF_FONT_BODY,
                    ui_theme_color(UI_C_TEXT_FAINT), 1, GFX_ALIGN_LEFT);

        ui_rule(c, COL_X, RULE_Y, ACK_W, ui_theme_color(UI_C_EDGE));
        if (lost) {
            draw_link_numbers(c);
        } else {
            draw_numbers(c);
        }
        s.drawn_mask |= bit;
    }

    /*
     * The button is the only thing that moves, and it is repainted every
     * frame: the hold fades across two seconds, so a frame that skipped it
     * would be a step in the fade.
     */
    ui_button_font(c, gfx_rect_make(ACK_X, ACK_Y, ACK_W, ACK_H),
                   s.fired ? TR(BF_ACKNOWLEDGED) : TR(BF_HOLD), ack_fill(),
                   false, true, BF_FONT_LABEL);
}

/*
 * Touch events were lost between two frames, so this screen's record of what:
 * is on the glass cannot be trusted.  Drop the gesture rather than let a
 * hold that completes on a timer finish on a contact that may have gone.
 * Nothing is commanded here: a gesture abandoned part way asks for nothing,
 * which is what letting go early already does.
 */
static void cancel(void)
{
    s.pressed = false;
    s.held_s  = 0.0f;
    /* And the id it owned: without this every later press is refused until
     * one arrives carrying the id whose release went missing, and the
     * operator cannot acknowledge the fault or leave the screen. */
    s.have_press = false;
}

static const ui_screen_t k_screen = {
    .title  = "CAN BUS FAULT",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *busfault_screen(void) { return &k_screen; }
