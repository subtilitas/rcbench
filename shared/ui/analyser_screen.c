/*
 * The receiver-bus analyser screen.
 *
 * A receiver in failsafe sends sixteen well-formed channel values, so the
 * state block (SILENT, FAILSAFE, FRAME LOST, LIVE) is the largest element on
 * the screen and the channels are small.
 *
 * SPDX-License-Identifier: MIT
 */

#include "analyser_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD     6
#define TAB_Y   8
#define TAB_H   28
#define BODY_Y  44

/*
 * The state card is only as wide as the widest thing it must hold: the word
 * FRAME LOST at 16x28, and the sentence under it at 8x16, both 160 pixels.
 * Everything it saves goes to the traces, which is the half of this screen
 * that can use more room.
 */
#define RCARD_PREF 190
#define LCARD_W (W - 2 * PAD - 8 - RCARD_PREF)
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W RCARD_PREF

/*
 * Sixteen lanes of recent history, one per channel.
 *
 * A lane keeps the last 118 samples, so the channel that moved is the one
 * with a step in its trace.  A glitch is a spike in one lane; a dropout is a
 * notch across all sixteen at the same instant.
 *
 * A lane is two thirds history and one third current value: the trace
 * answers which channel moved, the bar answers how far it is from neutral.
 */
#define HIST      118             /* one pixel a sample, so about 1.7 s */
#define PITCH     1

/* Eight rows of two.  Half the history each, twice the height to draw it in. */
#define COLS      2
#define ROWS      8
#define COL_W     ((LCARD_W - 16) / COLS)
#define LANE_H    46
#define LANE_TOP  (BODY_Y + 10)

#define LAB_W     40               /* the channel's name              */
#define TRACE_W   (HIST * PITCH)
#define BAR_W     58
#define BAR_GAP   8
#define VAL_W     40

static const char *const k_tab_labels[] = { "CHANNELS", "RAW" };

static struct {
    ui_tabs_t tabs;

    sbus_frame_t   frame;
    sbus_decoder_t dec;
    uint8_t        raw[SBUS_FRAME_BYTES];
    unsigned       raw_len;
    bool           have;
    bool           silent;

    /* Frames per second, measured rather than assumed: a receiver that has
     * quietly dropped to half rate is still sending valid frames. */
    /* A ring of recent positions per channel, in tenths of a percent of
     * travel -- enough for a lane sixteen pixels tall, and half the memory
     * of keeping microseconds. */
    int16_t  hist[SBUS_CHANNELS][HIST];
    unsigned head;
    unsigned filled;

    uint32_t last_ms;
    uint32_t window_ms;
    unsigned window_frames;
    unsigned rate_hz;

    uint32_t rev;
    uint32_t drawn[2];
    unsigned drawn_mask;
} s;

void analyser_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.silent   = true;
    ui_tabs_init(&s.tabs, k_tab_labels, ANALYSER_PANE_COUNT,
                 (gfx_rect_t){ PAD + 3, TAB_Y, 260, TAB_H });
}

void analyser_screen_push(const sbus_frame_t *frame,
                          const sbus_decoder_t *dec,
                          const uint8_t *raw, unsigned raw_len,
                          uint32_t now_ms)
{
    if (frame == NULL || dec == NULL) {
        return;
    }
    s.frame  = *frame;
    s.dec    = *dec;
    s.have   = true;
    s.silent = false;

    if (raw != NULL) {
        s.raw_len = (raw_len > SBUS_FRAME_BYTES) ? SBUS_FRAME_BYTES : raw_len;
        memcpy(s.raw, raw, s.raw_len);
    }

    /* A whole second of frames, then divide: timing the gap between two
     * frames reports the jitter of one pair as if it were the rate. */
    ++s.window_frames;
    if ((uint32_t)(now_ms - s.window_ms) >= 1000u) {
        s.rate_hz = s.window_frames;
        s.window_frames = 0;
        s.window_ms = now_ms;
    }
    for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
        const float us = (float)sbus_to_us(s.frame.channel[i]);
        float f = (us - 1500.0f) / 500.0f;
        if (f < -1.0f) { f = -1.0f; }
        if (f >  1.0f) { f =  1.0f; }
        s.hist[i][s.head] = (int16_t)(f * 1000.0f);
    }
    s.head = (s.head + 1u) % HIST;
    if (s.filled < HIST) {
        ++s.filled;
    }

    s.last_ms = now_ms;
    ++s.rev;
}

void analyser_screen_silent(uint32_t now_ms)
{
    (void)now_ms;
    if (!s.silent) {
        s.silent = true;
        s.rate_hz = 0;
        ++s.rev;
    }
}

static void event(const touch_event_t *evt)
{
    if (ui_tabs_event(&s.tabs, evt)) {
        ++s.rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static gfx_color_t live(void)
{
    return (s.have && s.frame.failsafe) ? ui_theme_color(UI_C_DANGER)
                                        : ui_theme_color(UI_C_ACCENT);
}

/* One channel's lane: its name, its trace, and its current value. */
static void draw_lane(gfx_canvas_t *c, int ch)
{
    const int col = ch / ROWS;
    const int row = ch % ROWS;
    const int x0  = PAD + 8 + col * COL_W;
    const int y   = LANE_TOP + row * LANE_H;
    const int mid = y + LANE_H / 2;

    const int tx = x0 + LAB_W;
    const int bx = tx + TRACE_W + BAR_GAP;
    const int vx = bx + BAR_W + 6;

    const int gh   = LANE_H - 14;
    const int half = gh / 2 - 2;

    char lbl[16];
    snprintf(lbl, sizeof(lbl), "CH%02d", (ch + 1) & 0xFF);
    gfx_text(c, x0, mid - 8, lbl, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    /*
     * The trace runs in a trough of the same sunk colour the bar sits in, so
     * the two halves of a lane read as one instrument.
     */
    gfx_fill_round_rect(c, tx, mid - gh / 2, TRACE_W, gh, 4,
                        ui_theme_color(UI_C_PANEL_SUNK));

    /*
     * The neutral line is the trace colour at 56/255 over the sunk panel
     * colour: the unlit-segment convention the numerals and the servo rings
     * use.
     */
    gfx_hline(c, tx, mid, TRACE_W,
              gfx_lerp(ui_theme_color(UI_C_PANEL_SUNK), live(), 56));

    /* A rule under the lane, so a trace that wanders near the edge of its
     * own row cannot be read as belonging to the next one. */
    if (row < ROWS - 1) {
        gfx_hline(c, x0, y + LANE_H - 1, COL_W - 16,
                  gfx_lerp(ui_theme_color(UI_C_PANEL),
                           ui_theme_color(UI_C_EDGE), 130));
    }

    const gfx_color_t ink = live();
    const unsigned n = s.filled;

    if (n > 0u) {
        /*
         * Clipped to the trough while the trace is drawn.  Three things put
         * ink outside it otherwise: the newest column starts at the right
         * edge and is a whole pitch wide, a span between two samples is drawn
         * 1 px taller than it measures so a flat run stays visible, and the
         * trough's corners are rounded while the columns are not.
         */
        const gfx_rect_t trough = { (int16_t)tx, (int16_t)(mid - gh / 2),
                                    (int16_t)TRACE_W, (int16_t)gh };
        const gfx_rect_t was = c->clip;
        if (gfx_clip_set(c, trough)) {
            /*
             * Oldest at the left, newest at the right, so a change arrives at
             * the edge you are already looking at and walks away from it.
             */
            int prev_y = 0;
            for (unsigned i = 0; i < n; ++i) {
                const unsigned idx = (s.head + HIST - n + i) % HIST;
                const int v  = s.hist[ch][idx];
                const int py = mid - (v * half) / 1000;
                const int px = tx + TRACE_W - PITCH
                               - (int)((n - 1u - i) * PITCH);
                if (i == 0u) {
                    prev_y = py;
                }
                const int top = (py < prev_y) ? py : prev_y;
                const int bot = (py < prev_y) ? prev_y : py;
                gfx_fill_rect(c, px, top, PITCH, bot - top + 1, ink);
                prev_y = py;
            }
        }
        c->clip = was;
    }

    /*
     * The bar, filled from its own centre.  A control channel's interesting
     * quantity is how far it is from neutral and which way, and a bar growing
     * from one end puts that sign in the number rather than in the picture.
     */
    const int bh = 12;
    gfx_fill_round_rect(c, bx, mid - bh / 2, BAR_W, bh, bh / 2,
                        ui_theme_color(UI_C_PANEL_SUNK));
    if (s.have) {
        const int bmid  = bx + BAR_W / 2;
        const int reach = BAR_W / 2 - 3;
        const int v     = s.hist[ch][(s.head + HIST - 1u) % HIST];
        int span = (v * reach) / 1000;
        if (span >= 0) {
            gfx_fill_round_rect(c, bmid, mid - bh / 2 + 2,
                                (span < 3) ? 3 : span, bh - 4, 2, ink);
        } else {
            gfx_fill_round_rect(c, bmid + span, mid - bh / 2 + 2,
                                (-span < 3) ? 3 : -span, bh - 4, 2, ink);
        }
        gfx_vline(c, bmid, mid - bh / 2, bh, ui_theme_color(UI_C_TEXT_DIM));
    }

    char val[16];
    if (s.have) {
        snprintf(val, sizeof(val), "%u",
                 (unsigned)sbus_to_us(s.frame.channel[ch]));
    } else {
        snprintf(val, sizeof(val), "---");
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)vx, (int16_t)(mid - 8),
                                 (int16_t)VAL_W, 16 },
                val, UI_FONT_LABEL,
                s.have ? ui_theme_color(UI_C_TEXT)
                       : ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

static void draw_channels(gfx_canvas_t *c)
{
    for (int i = 0; i < 16; ++i) {
        draw_lane(c, i);
    }
}

static void draw_raw(gfx_canvas_t *c)
{
    const int x = PAD + 14;
    gfx_text(c, x, BODY_Y + 14, "LAST FRAME", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    if (!s.have) {
        gfx_text(c, x, BODY_Y + 44, "nothing decoded yet", UI_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
        return;
    }

    /* Header and footer called out, because those are the two bytes that
     * decide whether the twenty-three between them meant anything. */
    for (unsigned i = 0; i < s.raw_len; ++i) {
        const int col = (int)(i % 8u), row = (int)(i / 8u);
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X", s.raw[i]);
        const bool ends = (i == 0u) || (i + 1u == s.raw_len);
        gfx_text(c, x + col * 46, BODY_Y + 44 + row * 30, hex, UI_FONT_HEAD,
                 ends ? ui_theme_color(UI_C_ACCENT)
                      : ui_theme_color(UI_C_TEXT), 1);
    }

    const int y = BODY_Y + 44 + 4 * 30 + 14;
    char line[80];
    snprintf(line, sizeof(line), "%u bytes, framed on a %u us gap",
             s.raw_len, (unsigned)SBUS_GAP_US);
    gfx_text(c, x, y, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    snprintf(line, sizeof(line), "resyncs %u   bad tail %u",
             (unsigned)s.dec.resyncs, (unsigned)s.dec.bad_footer);
    gfx_text(c, x, y + 22, line, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

/* A state block, sized to be the first thing read. */
static void draw_state(gfx_canvas_t *c)
{
    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;

    const char *word;
    gfx_color_t tone;
    if (s.silent || !s.have) {
        word = "SILENT";     tone = ui_theme_color(UI_C_TEXT_FAINT);
    } else if (s.frame.failsafe) {
        word = "FAILSAFE";   tone = ui_theme_color(UI_C_DANGER);
    } else if (s.frame.frame_lost) {
        word = "FRAME LOST"; tone = ui_theme_color(UI_C_WARN);
    } else {
        word = "LIVE";       tone = ui_theme_color(UI_C_OK);
    }

    gfx_fill_round_rect(c, x, BODY_Y + 8, w, 56, UI_R_CARD,
                        gfx_lerp(ui_theme_color(UI_C_PANEL), tone, 40));
    gfx_draw_round_rect(c, x, BODY_Y + 8, w, 56, UI_R_CARD, tone);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)(BODY_Y + 26),
                                 (int16_t)w, 20 },
                word, UI_FONT_HEAD, tone, 1, GFX_ALIGN_CENTER);

    /*
     * One line per state.  A receiver in failsafe is still sending sixteen
     * well-formed numbers, and the line says they are its failsafe values.
     */
    const char *why = s.silent           ? "nothing on the wire"
                    : s.frame.failsafe   ? "numbers are invented"
                    : s.frame.frame_lost ? "frame arrived broken"
                                         : "transmitter heard";
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)(BODY_Y + 72),
                                 (int16_t)w, 16 },
                why, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_CENTER);
}

static void stat_row(gfx_canvas_t *c, int y, const char *label,
                     const char *value)
{
    const int x = RCARD_X + 12;
    gfx_text(c, x, y, label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 70), (int16_t)y,
                                 (int16_t)(RCARD_W - 24 - 70), 16 },
                value, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

static void draw_right(gfx_canvas_t *c)
{
    gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_state(c);

    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;
    gfx_hline(c, x, BODY_Y + 100, w, ui_theme_color(UI_C_EDGE));

    char buf[24];
    stat_row(c, BODY_Y + 114, "BUS", "S.BUS");
    snprintf(buf, sizeof(buf), "%u Hz", s.rate_hz);
    stat_row(c, BODY_Y + 138, "RATE", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.frames);
    stat_row(c, BODY_Y + 162, "FRAMES", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.resyncs);
    stat_row(c, BODY_Y + 186, "RESYNC", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.bad_footer);
    stat_row(c, BODY_Y + 210, "BAD TAIL", s.have ? buf : "---");

    gfx_hline(c, x, BODY_Y + 238, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, BODY_Y + 250, "DIGITAL", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    /* Stacked, so the card stays 190 px wide and the width goes to the
     * traces. */
    for (int i = 0; i < 2; ++i) {
        const bool on = s.have && ((i == 0) ? s.frame.ch17 : s.frame.ch18);
        const gfx_rect_t r = { (int16_t)x, (int16_t)(BODY_Y + 272 + i * 34),
                               (int16_t)w, 28 };
        ui_button(c, r, (i == 0) ? "CH17" : "CH18",
                  on ? ui_theme_color(UI_C_ACCENT)
                     : ui_theme_color(UI_C_PANEL_HI),
                  false, true);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, (gfx_rect_t){ PAD, BODY_Y - 4, LCARD_W,
                                 (int16_t)(H - BODY_Y - 2) },
                ui_theme_color(UI_C_PANEL));
        ui_card(c, (gfx_rect_t){ RCARD_X, PAD, RCARD_W,
                                 (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        s.drawn_mask |= bit;
    }

    if (s.drawn[buf] == s.rev) {
        return;
    }
    s.drawn[buf] = s.rev;

    ui_tabs_render(&s.tabs, c);
    gfx_fill_rect(c, PAD + 1, BODY_Y - 3, LCARD_W - 2, H - BODY_Y - 4,
                  ui_theme_color(UI_C_PANEL));
    if (s.tabs.selected == ANALYSER_PANE_CHANNELS) {
        draw_channels(c);
    } else {
        draw_raw(c);
    }
    draw_right(c);
}

static const ui_screen_t k_screen = {
    .title  = "ANALYSER",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *analyser_screen(void) { return &k_screen; }
