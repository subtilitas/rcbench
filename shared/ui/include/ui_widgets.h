/**
 * @file
 * @brief Stateless drawing helpers shared by every screen.
 *
 * Every call takes explicit geometry and draws immediately.  There is no
 * retained widget tree: on this hardware the cost is in touching framebuffer
 * pixels, and a retained tree touches them more often.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_icons.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Panel with a chamfered top-left / bottom-right, 1px edge, optional title. */
void ui_panel(gfx_canvas_t *c, gfx_rect_t r, const char *title,
              gfx_color_t accent);

/** The title strip a panel draws for itself; exposed for custom headers. */
void ui_panel_header(gfx_canvas_t *c, gfx_rect_t r, const char *title,
                     gfx_color_t accent);

/**
 * @brief Filled rounded button, its label in UI_FONT_LABEL.
 * @param c       Canvas.
 * @param r       The button.
 * @param label   Its label, centred, or NULL for none.
 * @param fill    Its colour while enabled.
 * @param pressed Lightens the fill.
 * @param enabled False draws it in ui_button_off_fill() with a dim edge and
 *                a faint label.
 */
void ui_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
               gfx_color_t fill, bool pressed, bool enabled);

/**
 * @brief The fill of a button that takes no press: the theme's ground
 *        (UI_BG), so with its faint label it reads dimmer than an enabled
 *        button in the dark theme and in the light one, and differs from
 *        every fill an enabled button takes (UI_PANEL, UI_PANEL_HI,
 *        UI_PANEL_SUNK, UI_EDGE_HI, an accent).
 * @return the colour
 */
gfx_color_t ui_button_off_fill(void);

/**
 * @brief ui_button() with its label in face @p f.
 * @param c       Canvas.
 * @param r       The button.
 * @param label   Its label, centred, or NULL for none.
 * @param fill    Its colour.
 * @param pressed Lightens the fill.
 * @param enabled False dims it.
 * @param f       The label's face.
 */
void ui_button_font(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                    gfx_color_t fill, bool pressed, bool enabled,
                    const gfx_font_t *f);

/** True when @p c is bright enough to need dark text on it. */
bool ui_is_light(gfx_color_t c);

/**
 * @brief Whether @p s in face @p f fits a box @p w x @p h px with @p pad px
 *        to spare at each side.
 *
 * The overlays pick the largest face of the type scale a label fits: a key
 * of the keypad or the keyboard over a narrow host card takes a smaller
 * face rather than overflow.
 * @param f   The face; NULL fits nothing.
 * @param s   The text; NULL is the empty text.
 * @param w   The box's width, in pixels.
 * @param h   The box's height, in pixels: at least the face's cell height.
 * @param pad The room to keep left and right of the text, in pixels.
 * @return True when the text's width plus 2 * @p pad is at most @p w and
 *         the face's height at most @p h.
 */
bool ui_text_fits(const gfx_font_t *f, const char *s, int w, int h, int pad);

/**
 * @brief Where the key grid of the keypad and of the keyboard ends at the
 *        latest, in body coordinates: 4px above the alert band, y 492.
 *
 * An overlay paints its whole area; its keys end here when the area
 * reaches further, so an alert covers no key on any screen.
 */
#define UI_KEYS_FOOT (UI_ALERT_Y - UI_BAND_H - 4)

/** A raised surface with a hairline edge: the base of every panel. */
void ui_card(gfx_canvas_t *c, gfx_rect_t r, gfx_color_t fill);

/**
 * @p t broken at spaces into lines as wide as @p first, each drawn left
 * aligned in a box of @p first's size, @p pitch px under the one before.
 * A word wider than a line is cut where the line ends.  Draws when @p c is
 * not NULL and returns the number of lines either way, 0 for an empty
 * @p t, so a caller measures with the code that draws.
 */
int ui_text_wrap(gfx_canvas_t *c, gfx_rect_t first, int pitch, const char *t,
                 const gfx_font_t *f, gfx_color_t col);

/**
 * @brief Where a tile places its parts and in which faces: one look per
 *        grid, the menu's and SETUP's.  Offsets are in pixels from the
 *        tile's top edge.
 */
typedef struct {
    int16_t icon_dy;          /**< the icon box's top                     */
    int16_t icon;             /**< the icon box's side                    */
    int16_t name_dy;          /**< the name's top                         */
    int16_t line_dy;          /**< the line's top                         */
    int16_t line_pitch;       /**< a broken line's second row below the
                               *   first; 0: no second row               */
    const gfx_font_t *name_f; /**< the name's face                        */
    const gfx_font_t *line_f; /**< the line's face                        */
} ui_tile_look_t;

/** @brief A tile's name and line keep this far from its sides, px. */
#define UI_TILE_PAD 8

/**
 * @brief A tile of a grid that opens what it names: the menu's, and
 *        SETUP's.
 *
 * A rounded surface, @p icon in a square box placed by @p look, centred in
 * x, the name under it in the text colour and the line under the name in
 * the dim one, both centred and clipped to the tile less UI_TILE_PAD px on
 * either side.  A line wider than that breaks at the last space that fits
 * onto a second row look->line_pitch px lower, where line_pitch is not 0
 * and that row ends inside the tile; a line with no such space or no such
 * row is clipped.
 *
 * @param c       Canvas.
 * @param r       The tile.
 * @param look    Where its parts go and their faces.
 * @param icon    Its icon, or NULL for none.
 * @param name    Its name, or NULL for none.
 * @param line    The line under the name, or NULL for none.
 * @param pressed Lightens the surface.
 */
void ui_tile(gfx_canvas_t *c, gfx_rect_t r, const ui_tile_look_t *look,
             ui_icon_fn icon, const char *name, const char *line,
             bool pressed);

/** Small status pill: a filled dot, then a label UI_PILL_INSET px from
 *  the pill's left edge, the dot centred half that from it. */
#define UI_PILL_INSET 24
/**
 * @brief A status pill in the label face, its label UI_PILL_INSET px in.
 * @param c     The canvas.
 * @param r     The pill.
 * @param label Its label; NULL draws the dot alone.
 * @param dot   The dot's colour.
 * @param fill  The pill's fill.
 */
void ui_pill(gfx_canvas_t *c, gfx_rect_t r, const char *label,
             gfx_color_t dot, gfx_color_t fill);

/**
 * @brief ui_pill() with the label in face @p f, @p inset px from the left
 *        edge and centred in the pill's height.
 * @param c     Canvas.
 * @param r     The pill.
 * @param label Its label, or NULL for none.
 * @param dot   The dot's colour.
 * @param fill  The pill's colour.
 * @param inset The label's start from the pill's left edge, in pixels; the
 *              dot is centred half that from it.
 * @param f     The label's face.
 */
void ui_pill_font(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                  gfx_color_t dot, gfx_color_t fill, int inset,
                  const gfx_font_t *f);

/**
 * @brief ui_pill_font() with the label in colour @p ink, for a fill the
 *        theme's text colour does not read on.
 * @param c     Canvas.
 * @param r     The pill.
 * @param label Its label, or NULL for none.
 * @param dot   The dot's colour.
 * @param fill  The pill's colour.
 * @param ink   The label's colour.
 * @param inset The label's start from the pill's left edge, in pixels.
 * @param f     The label's face.
 */
void ui_pill_ink(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                 gfx_color_t dot, gfx_color_t fill, gfx_color_t ink,
                 int inset, const gfx_font_t *f);

/** Horizontal proportional bar, @p frac in 0..1, with an optional peak tick. */
void ui_bar(gfx_canvas_t *c, gfx_rect_t r, float frac, float peak_frac,
            gfx_color_t color);

/** Right-aligned numeric readout in the hero face, with a unit suffix. */
void ui_value(gfx_canvas_t *c, gfx_rect_t box, const char *number,
              const char *unit, gfx_color_t color);

/** Format @p value with @p decimals decimal places (0 to 3); a non-finite
 *  value prints as "--". */
void ui_fmt(char *out, size_t n, float value, int decimals);

/**
 * Run clock as H:MM:SS, or MM:SS below an hour.
 *
 * Shared so every screen prints the same number the same way.
 */
void ui_clock(char *out, size_t n, uint32_t seconds);

/*
 * The home tag, at the same place at the top left of every screen that has
 * one: 44px tall (6.6mm), 8px from the band's top and left edges, the
 * title in UI_FONT_TAG (20 x 36).  The router hit-tests it before the
 * screen sees the event.
 */
/** @brief The home tag's left edge: 8px. */
#define UI_TAG_X 8
/** @brief The home tag's top edge: 8px. */
#define UI_TAG_Y 8
/** @brief The home tag's height: 44px (6.6mm). */
#define UI_TAG_H 44

/**
 * @brief Where the home tag with @p title is drawn and hit.
 * @param title The screen's name.
 * @return The tag, panel coordinates: UI_TAG_X, UI_TAG_Y, UI_TAG_H tall
 *         and as wide as its chevron and title.
 */
gfx_rect_t ui_home_tag_rect(const char *title);

/**
 * @brief Draw the home tag: a chevron and @p title in UI_FONT_TAG.
 * @param c       The canvas, the panel's.
 * @param title   The screen's name.
 * @param pressed A press is on it: the fill lightens.
 */
void ui_home_tag(gfx_canvas_t *c, const char *title, bool pressed);

/**
 * @brief The home tag's shape without the chevron: an identity mark, not a
 *        control.
 * @param c    The canvas, the panel's.
 * @param text The mark's text, in UI_FONT_TAG: near-black on a light
 *             @p fill, the theme's panel colour on a dark one.
 * @param fill The mark's fill.
 */
void ui_wordmark(gfx_canvas_t *c, const char *text, gfx_color_t fill);

/** Left-pointing solid triangle, apex at (x, cy). */
void ui_chevron_left(gfx_canvas_t *c, int x, int cy, int size, gfx_color_t color);

/**
 * @brief A 1px horizontal rule.
 * @param c the canvas
 * @param x its left end, px
 * @param y its row, px
 * @param w its length, px
 * @param color its colour
 */
void ui_rule(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color);

/* ----------------------------------------------------------- choice list */

/*
 * A list that opens over the screen: a face that names the choice, and the
 * rows it opens into.  The chevron on the face says it opens rather than
 * steps, which is the difference between this and a settings row.  The
 * outputs screen's protocol list and the pin picker's are this one.
 *
 * A face or a row UI_CHOICE_ROW_H (56px, 8.4mm) tall or taller draws its
 * text in the type scale's head face, 20 x 36 (UI_FONT_TAG), when the text
 * fits; a shorter one, and a text that does not fit, in the 16 x 28 face
 * (UI_FONT_HEAD).  A screen takes the larger face with the row height its
 * own layout gives.  The text is centred in the face's and each row's
 * height.  A face takes 10px before its text and 33px after it for a
 * 7px chevron, 12px and 43px for an 11px one when tall; a row 12px
 * before and after its text.  Each face and row picks its own face: the
 * rows of one list can differ where one name does not fit.
 */
/** @brief The row height from which a choice face or row draws in the
 *         20 x 36 face: 56px (8.4mm), the plan's choice row. */
#define UI_CHOICE_ROW_H 56

/**
 * @brief The face a choice face or row @p h px tall draws its text in.
 * @param h The face's or the row's height, in pixels.
 * @return UI_FONT_TAG (20 x 36) from UI_CHOICE_ROW_H on, UI_FONT_HEAD
 *         (16 x 28) under it.
 */
const gfx_font_t *ui_choice_font(int h);

/**
 * @brief The face a choice face or row draws @p text in.
 * @param h    The face's or the row's height, in pixels.
 * @param w    The width its text may take, in pixels.
 * @param text The text; NULL is the empty text.
 * @return ui_choice_font(@p h) when @p text fits @p w in it, else
 *         UI_FONT_HEAD (16 x 28).
 */
const gfx_font_t *ui_choice_text_font(int h, int w, const char *text);

void ui_choice_face(gfx_canvas_t *c, gfx_rect_t r, const char *text,
                    bool pressed, bool open);

/** The face of a choice that takes no press now, as ui_tabs_render_locked()
 *  draws a segment row: sunk, its text and chevron dim, no accent. */
void ui_choice_face_locked(gfx_canvas_t *c, gfx_rect_t r, const char *text);

/**
 * @brief The open list's ground: a shadow 4px down and right, then the
 *        panel.
 * @param c the canvas
 * @param r the list's box
 */
void ui_choice_list(gfx_canvas_t *c, gfx_rect_t r);

/** One row of the open list: @p chosen is the choice in force, accented. */
void ui_choice_row(gfx_canvas_t *c, gfx_rect_t r, const char *text,
                   bool chosen, bool pressed);

/* ------------------------------------------------------- hold-to-confirm */

/*
 * A button that is held rather than pressed, and what it looks like while it
 * is held.
 *
 * Two seconds of contact, the fill fading from where it starts to where it
 * ends across them, and the whole button flashing twice as the command goes.
 * ARM uses it because a bench should not spin a propeller on a touch that
 * could have been an elbow; the bus-fault screen uses it because an
 * acknowledgement that can be given by brushing the panel is not one.
 *
 * The timing, the colours and the rules are all here: two screens can arm
 * the bench, and a safety control that behaves differently on one of them is
 * a control the operator has to learn twice.
 */
#define UI_HOLD_S 2.0f

/*
 * The most one frame can add to a hold.  A frame's duration is measured at
 * its top and applied at its end, so without a cap a single late frame
 * completes a hold that began inside that same frame.  0.25s puts at least
 * eight frames with the press standing between a press and an arm.
 */
#define UI_HOLD_MAX_CREDIT_S 0.25f

/**
 * @brief The flash as the command goes, by time: phases of
 *        UI_HOLD_FLASH_PHASE_MS (25ms) each, white, black, settled, the
 *        cycle twice, UI_HOLD_FLASH_MS (150ms) in all: 9 frames at 60Hz.
 */
#define UI_HOLD_FLASH_PHASE_MS 25
/** @brief The phases of one cycle: white, black, settled. */
#define UI_HOLD_FLASH_CYCLE    3
/** @brief The cycles of one flash. */
#define UI_HOLD_FLASH_TIMES    2
/** @brief The whole flash: 150ms. */
#define UI_HOLD_FLASH_MS \
    (UI_HOLD_FLASH_PHASE_MS * UI_HOLD_FLASH_CYCLE * UI_HOLD_FLASH_TIMES)

/**
 * The fill for a button held @p held_s of UI_HOLD_S, fading @p base to
 * @p target. @p held_s of 0 is @p base; the fade previews the colour the
 * button is about to hold.
 */
gfx_color_t ui_hold_fill(gfx_color_t base, gfx_color_t target, float held_s);

/**
 * @brief The fill while the flash runs.
 *
 * 150ms in all is short enough to read as a flash rather than an
 * animation.  The colour follows the time the flash has run, not the frames
 * drawn: a frame later than a phase (25ms) skips that phase.
 * @param settled  What the fill returns to: the settled phase and the end.
 * @param ms_left  The flash's time still to run, counting down from
 *                 UI_HOLD_FLASH_MS; 0 or less is no flash.
 * @return White, black or @p settled.
 */
gfx_color_t ui_hold_flash(gfx_color_t settled, int ms_left);

/**
 * A hold in progress: how long, whether it has already fired, and the flash
 * that follows it.
 *
 * The screen owns the touch routing -- which of its buttons a press is on --
 * and hands this the four things that happen to the gesture: it began, it
 * left the control, it ended, and time passed. What the hold asked for
 * arriving or going away again is the fifth, and it comes from the bench
 * rather than from the finger.
 */
typedef struct {
    float held_s;      /**< how long the press has been held           */
    bool  down;        /**< a press is on the control                  */
    bool  fired;       /**< the hold completed; it does not repeat     */
    int   flash_ms;    /**< ms of the flash still to run               */
    bool  flash_new;   /**< the flash started and no step has run yet  */
} ui_hold_t;

/** Nothing held, nothing fired, nothing flashing. */
void ui_hold_reset(ui_hold_t *h);

/** A press landed on the control: the hold starts from zero. */
void ui_hold_begin(ui_hold_t *h);

/**
 * The press left the control, so the gesture is over: contact with the
 * control is the gesture, not contact with the panel. Sliding back on does
 * not resume it. Returns whether a hold was actually abandoned, which is
 * what tells the screen to drop its own record of the press.
 */
bool ui_hold_leave(ui_hold_t *h);

/** The press lifted. Returns whether it was the press that fired, which the
 *  release consumes: a release that fired is not also a press. */
bool ui_hold_end(ui_hold_t *h);

/**
 * What one frame is worth to a hold: @p dt_s, capped at
 * UI_HOLD_MAX_CREDIT_S and floored at zero. Used by ui_hold_tick() and by
 * any screen that keeps its own hold timer, so a late frame cannot complete
 * a gesture that began inside it.
 */
float ui_hold_credit(float dt_s);

/** Time passed. True on the one frame the hold completes, and never again
 *  until the press is lifted and made afresh. */
bool ui_hold_tick(ui_hold_t *h, float dt_s);

/**
 * What the hold asked for has arrived: flash, and stop filling.
 *
 * `fired` is deliberately kept. It remembers that the press still under the
 * finger is the one that fired, and the application calls this between the
 * hold completing and the finger lifting; clearing it would make that
 * release look like a fresh press.
 */
void ui_hold_reached(ui_hold_t *h);

/**
 * What the hold asked for has gone away -- a stop, a failsafe, or the far
 * end -- possibly under a finger that is still down.
 *
 * Both halves of the gesture end here. A press left standing would start a
 * fresh hold the moment the state went away and ask for it again, from a
 * contact the operator made to stop it. Returns whether such a press was
 * ended, so the screen can drop its own record of it; clearing the press
 * without `fired` would strand it, because the release consumes `fired` and
 * a release of a press that is no longer held does nothing.
 */
bool ui_hold_left(ui_hold_t *h);

/**
 * @brief Time passed for the flash.
 *
 * The first step after ui_hold_reached() spends nothing, so the frame the
 * flash starts on draws its first phase whatever the order of the reach
 * and the step in a tick.
 * @param h     The hold.
 * @param dt_s  The time, in seconds; the flash's time left falls by it as
 *              ui_hold_flash_left() spends it.
 */
void ui_hold_flash_step(ui_hold_t *h, float dt_s);

/**
 * @brief The flash's time left, @p ms_left, after @p dt_s seconds.
 *
 * One step goes no further than the start of the next phase, so every
 * phase is drawn on at least one frame at any frame rate: at 10 frames a
 * second the flash takes 6 frames, 600ms, instead of skipping phases.
 * @param ms_left The time left, in ms.
 * @param dt_s    The time passed, in seconds; under 0 counts as 0.
 * @return The time left, 0 at the least.
 */
int ui_hold_flash_left(int ms_left, float dt_s);

#ifdef __cplusplus
}
#endif
