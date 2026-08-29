/*
 * The receiver-bus analyser.
 *
 * The point of this screen is not the sixteen numbers.  It is the two flags
 * beside them: a receiver in failsafe sends sixteen perfectly well-formed
 * channel values that mean nothing, and a bench that shows the numbers
 * without shouting about the flag is helping somebody trust invented data.
 * So the state block is the largest thing here and the channels are small.
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

#define LCARD_W 556
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W (W - RCARD_X - PAD)

/*
 * Sixteen lanes of recent history, one per channel.
 *
 * The question this screen is asked is "I moved that -- which channel was
 * it?", and neither a bar nor a dial can answer it: the information is in
 * the movement and both of those show only where a thing is now.  A lane
 * keeps the last few seconds, so the channel that just moved is the one with
 * a step in it, and the eye finds a step among flat lines without being told
 * where to look.
 *
 * It is also the only arrangement that shows the two faults the stub
 * promised -- a glitch is a spike in one lane, a dropout is a notch across
 * all sixteen at the same instant.
 */
/*
 * A lane is two thirds history and one third now.
 *
 * The trace answers "which channel moved"; the bar answers "how far is it,
 * right now" without reading a number.  Neither alone was enough -- a bar
 * cannot show the step that identifies a channel, and a trace two seconds
 * wide makes the eye measure a line's height against a faint centre when all
 * it wants is a length.
 */
#define HIST      108              /* about a second and a half at S.BUS rate */
#define LANE_H    23
#define LANE_TOP  (BODY_Y + 8)
#define LANE_X    56               /* where a lane's trace starts */
#define LANE_W    (HIST * 3)
#define BAR_X     (LANE_X + LANE_W + 14)
#define BAR_W     104
#define VAL_X     (BAR_X + BAR_W + 8)

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

/* One channel's lane: its name, its trace, and where it is now. */
static void draw_lane(gfx_canvas_t *c, int ch)
{
    const int y   = LANE_TOP + ch * LANE_H;
    const int mid = y + LANE_H / 2;
    const int half = LANE_H / 2 - 3;

    char lbl[16];
    snprintf(lbl, sizeof(lbl), "CH%02d", (ch + 1) & 0xFF);
    gfx_text(c, PAD + 8, mid - 8, lbl, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    /* Neutral, drawn the whole width: sixteen of these make a comb the eye
     * reads a step against without measuring anything. */
    gfx_hline(c, LANE_X, mid, LANE_W, ui_theme_color(UI_C_GRID));

    const gfx_color_t ink = live();

    /*
     * Oldest at the left, newest at the right, so a change arrives at the
     * edge you are already looking at and walks away from it.
     */
    const unsigned n = s.filled;
    if (n > 0u) {
    int prev_y = 0;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned idx = (s.head + HIST - n + i) % HIST;
        const int v  = s.hist[ch][idx];
        const int py = mid - (v * half) / 1000;
        const int px = LANE_X + LANE_W - (int)((n - 1u - i) * 3u);
        if (i == 0u) {
            prev_y = py;
        }
        /* A span rather than a point, so a step between two samples is a
         * line and not two dots with nothing between them. */
        const int top = (py < prev_y) ? py : prev_y;
        const int bot = (py < prev_y) ? prev_y : py;
        gfx_fill_rect(c, px, top, 3, bot - top + 2, ink);
        prev_y = py;
    }
    }

    /*
     * The bar, filled from its own centre.  A control channel's interesting
     * quantity is how far it is from neutral and which way, and a bar growing
     * from one end puts that sign in the number rather than in the picture.
     */
    const int bh = LANE_H - 11;
    gfx_fill_round_rect(c, BAR_X, mid - bh / 2, BAR_W, bh, bh / 2,
                        ui_theme_color(UI_C_PANEL_SUNK));
    if (s.have) {
        const int bmid  = BAR_X + BAR_W / 2;
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
    gfx_text_in(c, (gfx_rect_t){ (int16_t)VAL_X, (int16_t)(mid - 8),
                                 (int16_t)(LCARD_W - VAL_X + PAD - 10), 16 },
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
     * Spelled out, because the difference is the whole reason this screen
     * exists: a receiver in failsafe is still sending sixteen well-formed
     * numbers, and they are the ones it was told to invent.
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

static void row(gfx_canvas_t *c, int y, const char *label, const char *value)
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
    row(c, BODY_Y + 114, "BUS", "S.BUS");
    snprintf(buf, sizeof(buf), "%u Hz", s.rate_hz);
    row(c, BODY_Y + 138, "RATE", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.frames);
    row(c, BODY_Y + 162, "FRAMES", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.resyncs);
    row(c, BODY_Y + 186, "RESYNC", s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.bad_footer);
    row(c, BODY_Y + 210, "BAD TAIL", s.have ? buf : "---");

    gfx_hline(c, x, BODY_Y + 238, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, BODY_Y + 250, "DIGITAL", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    for (int i = 0; i < 2; ++i) {
        const bool on = s.have && ((i == 0) ? s.frame.ch17 : s.frame.ch18);
        const gfx_rect_t r = { (int16_t)(x + i * (w / 2)),
                               (int16_t)(BODY_Y + 272),
                               (int16_t)(w / 2 - 8), 28 };
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
