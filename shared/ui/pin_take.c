/**
 * @file pin_take.c
 * @brief OUTPUTS' question card for moving a pin another protocol holds.
 *        See pin_take.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pin_take.h"

#include <stdio.h>
#include <string.h>

#include "outputs_screen.h"
#include "outputs_screen_int.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The card's inner margin left and right, px. */
#define CARD_PAD    16
/** @brief Its margin above the title and under the buttons, px. */
#define CARD_TOP    10
#define TITLE_GAP   6           /**< Under the title, px. */
#define BTN_GAP     8           /**< Between the buttons, px. */
#define TEXT_GAP    6           /**< Between the text and the buttons. */
#define BTN_W       240         /**< A button side by side, px. */
/** @brief A card this wide puts its buttons side by side. */
#define WIDE_CARD   (2 * BTN_W + BTN_GAP + 2 * CARD_PAD)
#define LINE_MAX    96          /**< A line's bytes. */
#define ROWS_MAX    6           /**< The most rows one line wraps to. */

/** @brief No button pressed. */
#define BTN_NONE    (-1)

/** @brief The card and the last refusal. */
static struct {
    bool                open;     /**< the card is up                  */
    pin_take_view_t     view;     /**< the view that opened it         */
    gfx_rect_t          rect;     /**< where it is                     */
    uint8_t             index;    /**< the pin's catalogue entry       */
    uint8_t             taker;    /**< the protocol asked to take it   */
    uint16_t            board;    /**< the board it is on              */
    uint32_t            pins[OUTBIND_PROTOS]; /**< the pins at opening */
    outbind_take_info_t info;     /**< what the move does              */
    int                 press;    /**< the button pressed, BTN_NONE    */
    uint8_t             press_id; /**< the contact that pressed it     */

    bool                why;      /**< a refusal stands                */
    pin_take_view_t     why_view; /**< in that view                    */
    uint8_t             why_index; /**< for that pin                   */
    uint8_t             why_taker; /**< and that protocol              */
    uint16_t            why_board; /**< on that board                  */

    uint32_t            rev;      /**< bumped by every change drawn    */
    uint32_t            opened;   /**< cards opened, for the Dial's list */
} t = { .press = BTN_NONE };  /**< The card's state. */

/** @brief Marks the card's picture changed. */
static void touched(void) { ++t.rev; }

uint32_t pin_take_revision(void) { return t.rev; }

void pin_take_close(void)
{
    if (t.open) {
        t.open = false;
        t.press = BTN_NONE;
        touched();
    }
}

void pin_take_forget_why(void)
{
    if (t.why) {
        t.why = false;
        touched();
    }
}

void pin_take_reset(void)
{
    pin_take_close();
    pin_take_forget_why();
}

bool pin_take_is_open(void) { return t.open; }

bool pin_take_open_in(pin_take_view_t view)
{
    return t.open && t.view == view;
}

int pin_take_index(void) { return t.open ? (int)t.index : -1; }

void pin_take_cancel(void)
{
    if (t.press != BTN_NONE) {
        t.press = BTN_NONE;
        touched();
    }
}

/**
 * @brief The protocol a binding selects: OFF for one out of range.
 * @param b the binding
 * @return the protocol's index
 */
static uint8_t taker_of(const outbind_t *b)
{
    return (b->proto < OUTBIND_PROTOS) ? b->proto : 0u;
}

/**
 * @brief Whether verdict @p v is a refusal the reason line explains.
 * @param v the verdict
 * @return true for a port, an interface, OFF, a budget or a PWM rule
 */
static bool says_why(outbind_take_t v)
{
    switch (v) {
    case OUTBIND_TAKE_PORT:
    case OUTBIND_TAKE_IFACE:
    case OUTBIND_TAKE_OFF:
    case OUTBIND_TAKE_CAP:
    case OUTBIND_TAKE_SLOTS:
    case OUTBIND_TAKE_CHANNELS:
    case OUTBIND_TAKE_CLASH_PROG_SLICE:
    case OUTBIND_TAKE_CLASH_COMPARE:
    case OUTBIND_TAKE_CLASH_OUT_SLICE:
        return true;
    default:
        return false;
    }
}

bool pin_take_tap(pin_take_view_t view, const outbind_t *b, uint8_t index,
                  const outbind_take_ctx_t *ctx, gfx_rect_t card)
{
    if (b == NULL) {
        return false;
    }
    const uint8_t taker = taker_of(b);
    outbind_take_info_t info;
    const outbind_take_t v = outbind_take_check(b, index, taker, ctx, NULL,
                                                &info);
    if (v == OUTBIND_TAKE_OK) {
        t.open  = true;
        t.view  = view;
        t.rect  = card;
        t.index = index;
        t.taker = taker;
        t.board = b->board;
        memcpy(t.pins, b->pins, sizeof(t.pins));
        t.info  = info;
        t.press = BTN_NONE;
        t.why   = false;
        ++t.opened;
        touched();
        return true;
    }
    if (!says_why(v)) {
        return false;
    }
    t.why       = true;
    t.why_view  = view;
    t.why_index = index;
    t.why_taker = taker;
    t.why_board = b->board;
    touched();
    return true;
}

void pin_take_follow(pin_take_view_t view, const outbind_t *b, bool read)
{
    if (b == NULL) {
        return;
    }
    if (t.why && t.why_view == view
        && (b->board != t.why_board || taker_of(b) != t.why_taker)) {
        pin_take_forget_why();
    }
    if (!pin_take_open_in(view)) {
        return;
    }
    if (!read || b->board != t.board || taker_of(b) != t.taker
        || memcmp(b->pins, t.pins, sizeof(t.pins)) != 0) {
        pin_take_close();
    }
}

/* ---------------------------------------------------------------- geometry */

gfx_rect_t pin_take_rect(void)
{
    return t.open ? t.rect : gfx_rect_make(0, 0, 0, 0);
}

gfx_rect_t pin_take_button(pin_take_btn_t which)
{
    if (!t.open) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    const gfx_rect_t r = t.rect;
    const int h = UI_BTN_H_TALL;
    const int foot = r.y + r.h - CARD_TOP - h;
    if (r.w >= WIDE_CARD) {
        /* Side by side at the foot: MOVE PIN left, CANCEL right. */
        return (which == PIN_TAKE_BTN_MOVE)
                   ? gfx_rect_make(r.x + CARD_PAD, foot, BTN_W, h)
                   : gfx_rect_make(r.x + r.w - CARD_PAD - BTN_W, foot, BTN_W,
                                   h);
    }
    /* A narrow card stacks them, CANCEL at the foot. */
    const int w = r.w - 2 * CARD_PAD;
    return (which == PIN_TAKE_BTN_MOVE)
               ? gfx_rect_make(r.x + CARD_PAD, foot - BTN_GAP - h, w, h)
               : gfx_rect_make(r.x + CARD_PAD, foot, w, h);
}

/* ------------------------------------------------------------------- input */

/**
 * @brief The button under a point.
 * @param x the point's column
 * @param y its row
 * @return the button, or BTN_NONE
 */
static int button_at(int x, int y)
{
    for (int k = PIN_TAKE_BTN_MOVE; k <= PIN_TAKE_BTN_CANCEL; ++k) {
        if (gfx_rect_contains(pin_take_button((pin_take_btn_t)k), x, y)) {
            return k;
        }
    }
    return BTN_NONE;
}

/**
 * @brief MOVE PIN: the move checked again on the binding as it stands.
 * @param b   the binding
 * @param ctx what stands beside it
 * @param out takes the binding to write
 * @return true when it stands for the same pin, holder, taker and board
 */
static bool move_stands(const outbind_t *b, const outbind_take_ctx_t *ctx,
                        outbind_t *out)
{
    if (b == NULL || out == NULL || b->board != t.board
        || taker_of(b) != t.taker) {
        return false;
    }
    outbind_take_info_t info;
    outbind_t after;
    if (outbind_take_check(b, t.index, t.taker, ctx, &after, &info)
            != OUTBIND_TAKE_OK
        || info.loser != t.info.loser) {
        return false;
    }
    *out = after;
    return true;
}

pin_take_act_t pin_take_event(const touch_event_t *evt, const outbind_t *b,
                              const outbind_take_ctx_t *ctx, outbind_t *out)
{
    if (!t.open || evt == NULL) {
        return PIN_TAKE_PASS;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!gfx_rect_contains(t.rect, x, y)) {
            /* A press off the card closes it and is spent on that, as an
             * open list's is: the card is not a trap. */
            pin_take_close();
            return PIN_TAKE_TAKEN;
        }
        t.press = button_at(x, y);
        t.press_id = evt->point.id;
        touched();
        return PIN_TAKE_TAKEN;
    }
    if (evt->type != TOUCH_EVENT_UP || t.press == BTN_NONE
        || evt->point.id != t.press_id) {
        return PIN_TAKE_TAKEN;
    }
    const int was = t.press;
    t.press = BTN_NONE;
    touched();
    if (!touch_event_is_tap_up(evt) || button_at(x, y) != was) {
        return PIN_TAKE_TAKEN;  /* the finger left the button, or lifted
                                   for the tracker */
    }
    if (was == PIN_TAKE_BTN_CANCEL) {
        pin_take_close();
        return PIN_TAKE_TAKEN;
    }
    const bool stands = move_stands(b, ctx, out);
    pin_take_close();
    return stands ? PIN_TAKE_MOVE : PIN_TAKE_TAKEN;
}

/* ------------------------------------------------------------------ reason */

bool pin_take_reason(pin_take_view_t view, const outbind_t *b,
                     const outbind_take_ctx_t *ctx, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (b == NULL || !t.why || t.why_view != view || b->board != t.why_board
        || taker_of(b) != t.why_taker) {
        return false;
    }
    outbind_take_info_t in;
    const outbind_take_t v = outbind_take_check(b, t.why_index, t.why_taker,
                                                ctx, NULL, &in);
    /* The binding with the pin gone from its holder, as the check met it:
     * the budget and PWM lines are the ones a free pin gets there. */
    outbind_t moved = *b;
    if (in.loser != 0u && in.loser < OUTBIND_PROTOS) {
        moved.pins[in.loser] &= ~((uint32_t)1u << t.why_index);
    }
    const unsigned gpio = in.gpio;
    char name[24];
    switch (v) {
    case OUTBIND_TAKE_OFF:
        snprintf(buf, cap, TR(PK_TAKE_WHY_OFF), gpio,
                 outputs_proto_name(in.loser));
        return true;
    case OUTBIND_TAKE_PORT:
        outputs_port_name(in.port, name, sizeof(name));
        snprintf(buf, cap, TR(PK_TAKE_WHY_PORT), gpio, name);
        return true;
    case OUTBIND_TAKE_IFACE:
        snprintf(buf, cap,
                 (in.iface == (uint8_t)OUTPUTS_IFACE_PDMINI)
                     ? TR(PK_TAKE_WHY_PDMINI)
                 : (in.iface == (uint8_t)OUTPUTS_IFACE_TAP)
                     ? TR(PK_TAKE_WHY_TAP)
                     : TR(PK_TAKE_WHY_SENSE),
                 gpio);
        return true;
    case OUTBIND_TAKE_CAP:
    case OUTBIND_TAKE_SLOTS:
    case OUTBIND_TAKE_CHANNELS:
        return outputs_reason_of(&moved, buf, cap);
    case OUTBIND_TAKE_CLASH_PROG_SLICE:
    case OUTBIND_TAKE_CLASH_COMPARE:
    case OUTBIND_TAKE_CLASH_OUT_SLICE:
        return outputs_clash_reason(&moved, (int)t.why_index, ctx->pwm_ports,
                                    ctx->port_count, buf, cap);
    default:
        return false;
    }
}

/* -------------------------------------------------------------------- draw */

/** @brief The card's lines, each in the language in force. */
typedef struct {
    char title[LINE_MAX];  /**< GPIO n: holder        */
    char ask[LINE_MAX];    /**< the question          */
    char keeps[LINE_MAX];  /**< what the holder keeps */
    char role[LINE_MAX];   /**< the new role, or ""   */
    char lead[LINE_MAX];   /**< the lead on the pad   */
} card_text_t;

/**
 * @brief The card's lines for the move it asks about.
 * @param out takes them
 */
static void card_text(card_text_t *out)
{
    const outbind_take_info_t *in = &t.info;
    const char *loser = outputs_proto_name(in->loser);
    const char *taker = outputs_proto_name(t.taker);
    snprintf(out->title, sizeof(out->title), TR(PK_TAKE_TITLE),
             (unsigned)in->gpio, loser);
    snprintf(out->ask, sizeof(out->ask), TR(PK_TAKE_ASK), (unsigned)in->gpio,
             loser, taker);
    if (in->keeps == 0u) {
        snprintf(out->keeps, sizeof(out->keeps), TR(PK_TAKE_KEEPS_NONE),
                 loser);
    } else if (in->keeps == 1u) {
        snprintf(out->keeps, sizeof(out->keeps), TR(PK_TAKE_KEEPS_1), loser);
    } else {
        snprintf(out->keeps, sizeof(out->keeps), TR(PK_TAKE_KEEPS), loser,
                 (unsigned)in->keeps);
    }
    out->role[0] = '\0';
    if (in->role_was != in->role_now && in->channel != OUTBIND_TAKE_UNSET) {
        snprintf(out->role, sizeof(out->role),
                 (in->role_now == OUT_ROLE_THROTTLE) ? TR(PK_TAKE_TO_MOTOR)
                                                     : TR(PK_TAKE_TO_SERVO),
                 (unsigned)in->channel);
    }
    /* Where the lead is: a soldered output's name, the pad printed on the
     * board, or the GPIO on a board that prints none. */
    char place[16];
    if (!outputs_role_name(t.board, in->gpio, place, sizeof(place))) {
        if (in->pad != 0u) {
            snprintf(place, sizeof(place), "PAD %u", (unsigned)in->pad);
        } else {
            snprintf(place, sizeof(place), "GPIO %u", (unsigned)in->gpio);
        }
    }
    snprintf(out->lead, sizeof(out->lead), TR(PK_TAKE_LEAD), place, taker);
}

/** @brief The faces the card's lines are drawn in. */
typedef struct {
    const gfx_font_t *title; /**< the title                      */
    const gfx_font_t *body;  /**< the question and the holder    */
    const gfx_font_t *small; /**< the role and the lead          */
} card_faces_t;

/**
 * @brief The rows @p text takes in face @p f across @p w px.
 * @param text the line
 * @param f    the face
 * @param w    the width, px
 * @param cut  set when the line needs more than ROWS_MAX rows
 * @return the rows, 0 for an empty line
 */
static int rows_of(const char *text, const gfx_font_t *f, int w, bool *cut)
{
    if (text[0] == '\0') {
        return 0;
    }
    const int n = outputs_wrap(NULL, gfx_rect_make(0, 0, w, 0), 0, text,
                               ROWS_MAX + 1, f, 0, false);
    if (n > ROWS_MAX) {
        *cut = true;
    }
    return (n > ROWS_MAX) ? ROWS_MAX : n;
}

/**
 * @brief The pitch of face @p f's rows: its height and 2px.
 * @param f the face
 * @return the pitch, px
 */
static int pitch_of(const gfx_font_t *f) { return gfx_text_height(f, 1) + 2; }

/**
 * @brief Lays the card out: the title in the largest face that holds it on
 *        one row, the rest in 10 x 20, the role and the lead in 8 x 16
 *        where 10 x 20 runs into the buttons, and every line in 8 x 16
 *        where that still does.
 * @param tx    the lines
 * @param faces takes the faces
 * @param out   takes the layout
 */
static void lay_out(const card_text_t *tx, card_faces_t *faces,
                    pin_take_layout_t *out)
{
    const gfx_rect_t r = t.rect;
    const int w = r.w - 2 * CARD_PAD;
    const gfx_font_t *const titles[] = { &gfx_font_16x28, &gfx_font_12x24,
                                         &gfx_font_10x20 };
    faces->title = &gfx_font_10x20;
    for (unsigned i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        if (gfx_text_width(titles[i], tx->title, 1) <= w) {
            faces->title = titles[i];
            break;
        }
    }
    const int top = r.y + CARD_TOP + gfx_text_height(faces->title, 1)
                    + TITLE_GAP;
    const int room = pin_take_button(PIN_TAKE_BTN_MOVE).y - TEXT_GAP;
    const gfx_font_t *const tries[3][2] = {
        { &gfx_font_10x20, &gfx_font_10x20 },
        { &gfx_font_10x20, &gfx_font_8x16 },
        { &gfx_font_8x16,  &gfx_font_8x16 },
    };
    int end = top;
    bool cut = gfx_text_width(faces->title, tx->title, 1) > w;
    for (int k = 0; k < 3; ++k) {
        bool rows_cut = false;
        const gfx_font_t *body = tries[k][0], *small = tries[k][1];
        end = top
              + rows_of(tx->ask, body, w, &rows_cut) * pitch_of(body)
              + rows_of(tx->keeps, body, w, &rows_cut) * pitch_of(body)
              + rows_of(tx->role, small, w, &rows_cut) * pitch_of(small)
              + rows_of(tx->lead, small, w, &rows_cut) * pitch_of(small);
        faces->body = body;
        faces->small = small;
        if (end - 2 <= room && !rows_cut) {
            break;
        }
        cut = cut || k == 2;    /* even 8 x 16 runs into the buttons */
    }
    out->title_h   = gfx_text_height(faces->title, 1);
    out->body_h    = gfx_text_height(faces->body, 1);
    out->small_h   = gfx_text_height(faces->small, 1);
    out->text_end  = end - 2;
    out->buttons_y = room + TEXT_GAP;
    out->cut       = cut;
}

bool pin_take_line(pin_take_line_t which, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (!t.open) {
        return false;
    }
    card_text_t tx;
    card_text(&tx);
    const char *const lines[] = { tx.title, tx.ask, tx.keeps, tx.role,
                                  tx.lead };
    if ((unsigned)which < sizeof(lines) / sizeof(lines[0])) {
        snprintf(buf, cap, "%s", lines[which]);
    }
    return true;
}

bool pin_take_layout(pin_take_layout_t *out)
{
    if (!t.open || out == NULL) {
        return false;
    }
    card_text_t tx;
    card_faces_t faces;
    card_text(&tx);
    lay_out(&tx, &faces, out);
    return true;
}

/**
 * @brief Draws one line of the card, wrapped in its width.
 * @param c    the canvas
 * @param y    the first row's top, advanced past the line
 * @param text the line
 * @param f    the face
 * @param ink  the colour
 */
static void draw_line(gfx_canvas_t *c, int *y, const char *text,
                      const gfx_font_t *f, gfx_color_t ink)
{
    if (text[0] == '\0') {
        return;
    }
    const gfx_rect_t r = t.rect;
    const int n = outputs_wrap(c, gfx_rect_make(r.x + CARD_PAD, *y,
                                                r.w - 2 * CARD_PAD,
                                                gfx_text_height(f, 1)),
                               pitch_of(f), text, ROWS_MAX, f, ink, false);
    *y += n * pitch_of(f);
}

void pin_take_draw(gfx_canvas_t *c)
{
    if (!t.open || c == NULL) {
        return;
    }
    card_text_t tx;
    card_faces_t faces;
    pin_take_layout_t lay;
    card_text(&tx);
    lay_out(&tx, &faces, &lay);

    const gfx_rect_t r = t.rect;
    ui_choice_list(c, r);
    gfx_text(c, r.x + CARD_PAD, r.y + CARD_TOP, tx.title, faces.title,
             UI_TEXT, 1);
    int y = r.y + CARD_TOP + lay.title_h + TITLE_GAP;
    draw_line(c, &y, tx.ask, faces.body, UI_TEXT);
    draw_line(c, &y, tx.keeps, faces.body, UI_TEXT_DIM);
    draw_line(c, &y, tx.role, faces.small, UI_TEXT);
    draw_line(c, &y, tx.lead, faces.small, UI_WARN);

    /* Neutral: the move writes a binding at rest and switches nothing
     * on (docs/Screens.md, button colours: Edit). */
    ui_button_font(c, pin_take_button(PIN_TAKE_BTN_MOVE), TR(PK_TAKE_MOVE),
                   UI_PANEL_HI, t.press == PIN_TAKE_BTN_MOVE, true,
                   UI_FONT_BODY);
    ui_button_font(c, pin_take_button(PIN_TAKE_BTN_CANCEL), TR(CANCEL),
                   UI_PANEL_HI, t.press == PIN_TAKE_BTN_CANCEL, true,
                   UI_FONT_BODY);
}

bool pin_take_focus(ui_focus_list_t *out)
{
    if (!t.open || out == NULL) {
        return false;
    }
    /* "PT" and the opening's number: each card is a new list to the
     * focus layer, which starts it on CANCEL, never one to go back to. */
    out->list = 0x50540000u | (t.opened & 0xFFFFu);
    (void)ui_focus_add(out, pin_take_button(PIN_TAKE_BTN_MOVE),
                       TR(PK_TAKE_MOVE));
    (void)ui_focus_add(out, pin_take_button(PIN_TAKE_BTN_CANCEL), TR(CANCEL));
    out->start = 1;             /* CANCEL */
    return true;
}
