/**
 * @file
 * @brief The RECEIVER screen (analyser_*: the code's name for it).
 *
 * A receiver in failsafe sends sixteen well-formed channel values, so the
 * state block (NO SIGNAL, FAILSAFE, FRAME LOST, LIVE) is the largest element
 * on the screen and the channels are small.
 *
 * SPDX-License-Identifier: MIT
 */

#include "analyser_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W /**< The panel's width, px. */
#define H UI_BODY_H  /**< The body's height under the band, px. */

#define PAD     UI_MARGIN /**< The screen's outer margin, px. */

/**
 * @brief The pane tabs, CHANNELS | RAW DATA: two tabs of 150 x 48px, 4px
 *        apart, from x 11 and y 8, a finger tall.
 */
#define TAB_X   (PAD + 3)
#define TAB_Y   8                       /**< The tabs' top, px. */
#define TAB_H   UI_TAP_MIN              /**< A tab's height: 48px. */
#define TAB_W   150                     /**< A tab's width, px. */
#define TAB_ROW_W (2 * TAB_W + 4)       /**< The row: 304px. */
#define LCARD_Y 64                      /**< The left card's top, px. */

/**
 * @brief The lowest row a line of text or a lane may reach: 4px above the
 *        alert band, body y 492.
 */
#define TEXT_FLOOR (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The state card's width, px: the word KEIN SIGNAL in the 20 x 36
 *        face is 220px, inside the card's 12px margins.  The rest of the
 *        width goes to the traces.
 */
#define RCARD_W 260
#define RCARD_X (W - PAD - RCARD_W)            /**< x 756. */
#define LCARD_W (RCARD_X - 8 - PAD)            /**< 740px. */
#define LCARD_H (H - PAD - LCARD_Y)            /**< 468px, to y 532. */

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in:
 *        AN_FONT_LABEL (10 x 20) for the channel names, rows and labels,
 *        AN_FONT_FINE (8 x 16) for the channel values and RAW DATA's bytes
 *        and counts, AN_FONT_BODY (12 x 24) for the state's line and
 *        AN_FONT_HEAD (20 x 36) for the state word.
 */
#define AN_FONT_LABEL (&gfx_font_10x20)
#define AN_FONT_FINE  UI_FONT_FINE       /**< 8 x 16. */
#define AN_FONT_BODY  UI_FONT_BODY       /**< 12 x 24. */
#define AN_FONT_HEAD  (&gfx_font_20x36)  /**< 20 x 36. */

/*
 * Sixteen lanes of recent history, one per channel.
 *
 * A lane keeps the last 200 samples, so the channel that moved is the one
 * with a step in its trace.  A glitch is a spike in one lane; a dropout is a
 * notch across all sixteen at the same instant.
 *
 * A lane is history, then the current value: the trace answers which
 * channel moved, the bar answers how far it is from neutral.
 */
/** @brief Samples a lane keeps: one pixel a sample, about 2.8s of S.BUS
 *         frames 14ms apart. */
#define HIST      200
#define PITCH     1                  /**< Pixels a sample. */

/* Eight rows of two. */
#define COLS      2                  /**< Lane columns. */
#define ROWS      8                  /**< Lanes a column. */
#define COL_W     ((LCARD_W - 16) / COLS)  /**< A column's pitch: 362px. */
#define LANE_H    52                 /**< A lane's height, px. */
#define LANE_TOP  (LCARD_Y + 8)      /**< The first lane's top: y 72. */

#define LAB_W     48   /**< The channel's name, CH01 at 10 x 20, and a gap. */
#define TRACE_W   (HIST * PITCH)     /**< The trace: 200px. */
#define BAR_GAP   8                  /**< Between the trace and the bar. */
#define BAR_W     56                 /**< The bar, px. */
#define VAL_GAP   6                  /**< Between the bar and the value. */
#define VAL_W     32                 /**< The value: 4 cells of 8 x 16. */
/** @brief A lane's width from its column's left edge: 350px. */
#define LANE_W    (LAB_W + TRACE_W + BAR_GAP + BAR_W + VAL_GAP + VAL_W)

_Static_assert(LANE_W + 12 <= COL_W, "a lane leaves 12px to the next column");
_Static_assert(LANE_TOP + ROWS * LANE_H <= TEXT_FLOOR,
               "the last lane ends above the alert band");
_Static_assert(TAB_H >= UI_TAP_MIN, "a tab is a finger tall");
_Static_assert(TAB_X + TAB_ROW_W + 3 <= RCARD_X, "the tabs end left of the state card");
_Static_assert(TAB_Y + TAB_H + 3 < LCARD_Y, "the tabs end above the lanes' card");

/*
 * The state card, top down: the state word in its box, its line in up to
 * three wrapped lines, the counters in rows of 32px, and the two digital
 * channels.
 */
#define BADGE_Y   20                  /**< The state box's top, px. */
#define BADGE_H   64                  /**< Its height, px. */
#define WHY_Y     (BADGE_Y + BADGE_H + 8)   /**< The state's line: y 92. */
#define WHY_PITCH 26                  /**< A wrapped line under the last. */
#define WHY_LINES 3                   /**< The most lines it takes. */
#define RULE1_Y   (WHY_Y + WHY_LINES * WHY_PITCH + 4)  /**< y 174. */
#define ROW_Y0    (RULE1_Y + 8)       /**< The first counter row: y 182. */
#define ROW_H     32                  /**< A counter row, px. */
#define RULE2_Y   (ROW_Y0 + 5 * ROW_H + 4)              /**< y 346. */
#define DIG_Y     (RULE2_Y + 10)      /**< DIGITAL's line: y 356. */
#define CHN_Y     (DIG_Y + 28)        /**< CH17's top: y 384. */
#define CHN_H     44                  /**< CH17 and CH18, px. */
#define CHN_GAP   8                   /**< Between them, px. */
#define CHN_INSET 40                  /**< A lamp's label from its left, px. */
_Static_assert(CHN_Y + 2 * CHN_H + CHN_GAP <= TEXT_FLOOR,
               "CH18 ends above the alert band");

/** @brief RAW DATA's bytes: the first row's top, px. */
#define RAW_Y       (LCARD_Y + 48)
#define RAW_PITCH_X 32   /**< A byte's column, 4 cells of 8 x 16, px. */
#define RAW_PITCH_Y 24   /**< A row of eight bytes, px. */

/** @brief The pane tabs' labels, CHANNELS and RAW DATA. */
static const ui_text_id_t k_tab_labels[] = { TX_AN_TAB_CHANNELS,
                                              TX_AN_TAB_RAW };

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
} s; /**< The screen's state. */

void analyser_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

/**
 * @brief The screen's state cleared: CHANNELS shown, nothing decoded, the
 *        tabs laid out.
 */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.silent   = true;
    ui_tabs_init_text(&s.tabs, k_tab_labels, ANALYSER_PANE_COUNT,
                      (gfx_rect_t){ TAB_X, TAB_Y, TAB_ROW_W, TAB_H });
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

/**
 * @brief One touch event: the tabs are the screen's one control.
 * @param evt The event.
 */
static void event(const touch_event_t *evt)
{
    if (ui_tabs_event(&s.tabs, evt)) {
        ++s.rev;
    }
}

/* ----------------------------------------------------------------- drawing */

/**
 * @brief The traces' colour: danger in failsafe, the accent otherwise.
 * @return The colour.
 */
static gfx_color_t live(void)
{
    return (s.have && s.frame.failsafe) ? ui_theme_color(UI_C_DANGER)
                                        : ui_theme_color(UI_C_ACCENT);
}

/**
 * @brief One channel's lane: its name, its trace, and its current value.
 * @param c  The canvas.
 * @param ch The channel, 0 to 15: columns of eight, top down.
 */
static void draw_lane(gfx_canvas_t *c, int ch)
{
    const int col = ch / ROWS;
    const int row = ch % ROWS;
    const int x0  = PAD + 8 + col * COL_W;
    const int y   = LANE_TOP + row * LANE_H;
    const int mid = y + LANE_H / 2;

    const int tx = x0 + LAB_W;
    const int bx = tx + TRACE_W + BAR_GAP;
    const int vx = bx + BAR_W + VAL_GAP;

    const int gh   = LANE_H - 14;
    const int half = gh / 2 - 2;

    char lbl[16];
    snprintf(lbl, sizeof(lbl), "CH%02d", (ch + 1) & 0xFF);
    gfx_text(c, x0, mid - 10, lbl, AN_FONT_LABEL,
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
        gfx_hline(c, x0, y + LANE_H - 1, LANE_W,
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
         * 1px taller than it measures so a flat run stays visible, and the
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
    const int bh = 16;
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
                val, AN_FONT_FINE,
                s.have ? ui_theme_color(UI_C_TEXT)
                       : ui_theme_color(UI_C_TEXT_FAINT), 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief CHANNELS: the sixteen lanes.
 * @param c The canvas.
 */
static void draw_channels(gfx_canvas_t *c)
{
    for (int i = 0; i < 16; ++i) {
        draw_lane(c, i);
    }
}

/**
 * @brief RAW DATA: the last frame's bytes, eight to a row, the header and
 *        footer in the accent colour, then the byte count and the decoder's
 *        counters.
 * @param c The canvas.
 */
static void draw_raw(gfx_canvas_t *c)
{
    const int x = PAD + 14;
    gfx_text(c, x, LCARD_Y + 14, TR(AN_LAST_FRAME), AN_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    if (!s.have) {
        gfx_text(c, x, LCARD_Y + 48, TR(AN_NOTHING_YET), AN_FONT_LABEL,
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
        gfx_text(c, x + col * RAW_PITCH_X, RAW_Y + row * RAW_PITCH_Y, hex,
                 AN_FONT_FINE,
                 ends ? ui_theme_color(UI_C_ACCENT)
                      : ui_theme_color(UI_C_TEXT), 1);
    }

    const int y = RAW_Y + 4 * RAW_PITCH_Y + 12;
    char line[80];
    snprintf(line, sizeof(line), TR(AN_BYTES),
             s.raw_len, (unsigned)SBUS_GAP_US);
    gfx_text(c, x, y, line, AN_FONT_FINE,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    snprintf(line, sizeof(line), "resyncs %u   bad tail %u",
             (unsigned)s.dec.resyncs, (unsigned)s.dec.bad_footer);
    gfx_text(c, x, y + 24, line, AN_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

/**
 * @brief The bytes of the next line of @p at that fit @p cells: up to the
 *        last space in it, or the whole width where no space falls after
 *        its start.
 * @param at    The text from the line's start.
 * @param cells The line's width, in cells.
 * @return The line's length, in bytes.
 */
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
 * @brief The state's line in the 12 x 24 face, broken after a space into
 *        lines as wide as the state card's inner column, each centred, at
 *        most WHY_LINES of them.
 * @param c    The canvas, or NULL to count the lines only.
 * @param why  The line.
 * @param ink  Its colour.
 * @return The lines @p why takes, drawn or not; past WHY_LINES none is
 *         drawn.
 */
static int why_wrap(gfx_canvas_t *c, const char *why, gfx_color_t ink)
{
    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;
    const int cells = w / gfx_text_width(AN_FONT_BODY, "M", 1);
    int n = 0;
    if (why == NULL) {
        return 0;
    }
    const char *at = why;
    while (*at != '\0') {
        char line[64];
        const size_t len = line_len(at, cells);
        const size_t k = (len < sizeof(line)) ? len : sizeof(line) - 1u;
        memcpy(line, at, k);
        line[k] = '\0';
        if (c != NULL && n < WHY_LINES) {
            gfx_text_in(c, (gfx_rect_t){ (int16_t)x,
                                         (int16_t)(WHY_Y + n * WHY_PITCH),
                                         (int16_t)w, 24 },
                        line, AN_FONT_BODY, ink, 1, GFX_ALIGN_CENTER);
        }
        ++n;
        at += len;
        while (*at == ' ') {
            ++at;
        }
    }
    return n;
}

int analyser_screen_why_lines(const char *why)
{
    return why_wrap(NULL, why, 0);
}

/**
 * @brief The state block, sized to be the first thing read: the word in
 *        its box and its line under it.
 * @param c The canvas.
 */
static void draw_state(gfx_canvas_t *c)
{
    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;

    const char *word;
    gfx_color_t tone;
    if (s.silent || !s.have) {
        word = TR(AN_SILENT); tone = ui_theme_color(UI_C_TEXT_FAINT);
    } else if (s.frame.failsafe) {
        word = "FAILSAFE";   tone = ui_theme_color(UI_C_DANGER);
    } else if (s.frame.frame_lost) {
        word = "FRAME LOST"; tone = ui_theme_color(UI_C_WARN);
    } else {
        word = TR(AN_LIVE);  tone = ui_theme_color(UI_C_OK);
    }

    gfx_fill_round_rect(c, x, BADGE_Y, w, BADGE_H, UI_R_CARD,
                        gfx_lerp(ui_theme_color(UI_C_PANEL), tone, 40));
    gfx_draw_round_rect(c, x, BADGE_Y, w, BADGE_H, UI_R_CARD, tone);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)(BADGE_Y + 14),
                                 (int16_t)w, 36 },
                word, AN_FONT_HEAD, tone, 1, GFX_ALIGN_CENTER);

    /*
     * One line per state.  A receiver in failsafe is still sending sixteen
     * well-formed numbers, and the line says they are its failsafe values.
     */
    const char *why = s.silent           ? TR(AN_WHY_SILENT)
                    : s.frame.failsafe   ? TR(AN_WHY_FAILSAFE)
                    : s.frame.frame_lost ? TR(AN_WHY_LOST)
                                         : TR(AN_WHY_LIVE);
    (void)why_wrap(c, why, ui_theme_color(UI_C_TEXT_DIM));
}

/**
 * @brief One counter row of the state card: its label at the left, its
 *        value at the right, in a row ROW_H tall.
 * @param c     The canvas.
 * @param i     The row, 0 for the first.
 * @param label The label.
 * @param value The value.
 */
static void stat_row(gfx_canvas_t *c, int i, const char *label,
                     const char *value)
{
    const int x = RCARD_X + 12;
    const int y = ROW_Y0 + i * ROW_H + (ROW_H - 20) / 2;
    gfx_text(c, x, y, label, AN_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 90), (int16_t)y,
                                 (int16_t)(RCARD_W - 24 - 90), 20 },
                value, AN_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief The state card: the state, the bus and its counters, and the two
 *        digital channels, lit while set.
 * @param c The canvas.
 */
static void draw_right(gfx_canvas_t *c)
{
    gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_state(c);

    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;
    gfx_hline(c, x, RULE1_Y, w, ui_theme_color(UI_C_EDGE));

    char buf[24];
    stat_row(c, 0, TR(AN_STAT_BUS), "S.BUS");
    snprintf(buf, sizeof(buf), "%uHz", s.rate_hz);
    stat_row(c, 1, TR(AN_STAT_RATE), s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.frames);
    stat_row(c, 2, TR(AN_STAT_FRAMES), s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.resyncs);
    stat_row(c, 3, TR(AN_STAT_RESYNC), s.have ? buf : "---");
    snprintf(buf, sizeof(buf), "%u", (unsigned)s.dec.bad_footer);
    stat_row(c, 4, TR(AN_STAT_BAD_TAIL), s.have ? buf : "---");

    gfx_hline(c, x, RULE2_Y, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, DIG_Y, "DIGITAL", AN_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    /* Stacked, so the card stays 260px wide and the width goes to the
     * traces.  Lamps, not keys: a pill whose dot lights while the channel
     * is set, as the band's chips are drawn, so nothing invites a tap. */
    for (int i = 0; i < 2; ++i) {
        const bool on = s.have && ((i == 0) ? s.frame.ch17 : s.frame.ch18);
        const gfx_rect_t r = { (int16_t)x,
                               (int16_t)(CHN_Y + i * (CHN_H + CHN_GAP)),
                               (int16_t)w, CHN_H };
        ui_pill_font(c, r, (i == 0) ? "CH17" : "CH18",
                     on ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_TEXT_FAINT),
                     ui_theme_color(UI_C_PANEL_SUNK), CHN_INSET,
                     AN_FONT_LABEL);
    }
}

/**
 * @brief The screen drawn on a revision not yet drawn: both cards once per
 *        framebuffer, then the tabs, the pane shown and the state card.
 * @param c The canvas.
 * @param buffer_index The framebuffer, whose revision it follows.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, (gfx_rect_t){ PAD, LCARD_Y, LCARD_W, LCARD_H },
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
    gfx_fill_rect(c, PAD + 1, LCARD_Y + 1, LCARD_W - 2, LCARD_H - 2,
                  ui_theme_color(UI_C_PANEL));
    if (s.tabs.selected == ANALYSER_PANE_CHANNELS) {
        draw_channels(c);
    } else {
        draw_raw(c);
    }
    draw_right(c);
}

/**
 * @brief The panel could not hand over every touch event, so this screen's
 *        record of the glass is stale.
 *
 * The tab row is the one control here that holds state between a press
 * and its release: a press it kept would own a track id the controller
 * reuses, and a later contact that began elsewhere and lifts over the tab
 * would switch the pane on a tap nobody made.
 */
static void cancel(void)
{
    ui_tabs_cancel(&s.tabs);
    /* The tab row may already be drawn pressed in both framebuffers; a
     * cleared press with the same revision would leave it painted held. */
    ++s.rev;
}

/** @brief The screen, as the router takes it. */
static const ui_screen_t k_screen = {
    .title  = "RECEIVER",
    .reset  = reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

/**
 * @brief The RECEIVER screen.
 * @return The screen.
 */
const ui_screen_t *analyser_screen(void) { return &k_screen; }
