/*
 * The board, with a button on every pin an output may have.
 * See picker_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen.h"

#include <stdio.h>
#include <string.h>

#include "board_drawing.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/*
 * The board is drawn as wide as the columns beside it leave room for, and as
 * tall as its own outline makes it.  500 is what fits with a table either
 * side; the height follows the aspect so a board is never stretched.
 */
#define BOARD_W  500
#define BOARD_MAX_H 240

/*
 * Two staggered rows of buttons above the board and two below.  A button is
 * 43 wide because that is a fingertip and because twenty of them at 2.54 mm
 * pitch do not fit in one row at any board size that fits the panel.
 */
#define TIERS 2
#define TGAP  5
#define BTNW  43
#define GAP   4

/*
 * The marks inside the board.  A ground is a round marker 36 px across, 40
 * px in from its pad: a servo lead needs a ground as much as a signal, so it
 * is as legible as a button, and grounds are five pads apart, so it has
 * nothing to collide with.  A rail is 32 px across, enough for "5V0", at one
 * of two depths that alternate along the row: rails sit on neighbouring
 * pads, 25 px apart, and a rail beside a ground goes below the ground's
 * marker -- 78 px is the ground's 40 plus both radii and both rings.
 */
#define GND_R       18
#define GND_DROP    40
#define RAIL_R      16
#define RAIL_DROP_0 30
#define RAIL_DROP_1 78
#define RING        2       /* the dark ring around a marker */

/*
 * Above the PINS table, left of the first button above the board: the
 * protocol list's face, the count, and the reason no pin can be added.  The
 * face is the outputs screen's: 266 px holds the longest protocol name,
 * "DSHOT600 BIDIR", in the head face.
 */
#define HEAD_X      6
#define HEAD_Y      8
#define HEAD_W      280
#define FACE_W      266
#define FACE_H      48
#define COUNT_Y     (HEAD_Y + FACE_H + 8)
#define REASON_Y    (COUNT_Y + 20)
#define HEAD_H      (REASON_Y + 16 - HEAD_Y)

/* The open list covers the board, as it covers the pins on OUTPUTS. */
#define POP_W       300
#define POP_ROW     46

/*
 * The PINS table, left of the board: 128 px, "CH7" then "GP28" in the head
 * face from 40 px in, and the pad number right-aligned.  It starts below the
 * reason line and ends above nothing: the first button below the board is
 * right of it.
 */
#define PINS_X      6
#define PINS_Y      (HEAD_Y + HEAD_H + 10)
#define PINS_W      128
#define ROW_H       28

/*
 * The IN USE table, right of the board.  Its lines start under the heading,
 * the rule and the BY OTHERS line, and end at the area's foot.  A name is
 * 20 px; a pin is 28 px with the GPIO in the head face, or 20 px with it in
 * the label face when the 28 px rows run past the foot.
 */
#define USE_X       (SCREEN_W - 132)
#define USE_Y       8
#define USE_W       126
#define USE_TOP     (USE_Y + 60)
#define USE_FOOT    (SCREEN_H - 4)
#define USE_NAME_H  20
#define USE_PIN_H   28
#define USE_PIN_H_C 20

/* The largest table, at 20 px a line, is under the heading. */
_Static_assert(USE_TOP + (int)PICKER_USE_MAX * USE_PIN_H_C <= USE_FOOT,
               "the IN USE table cannot hold every line");

#define IVORY GFX_RGB(236, 235, 228)
#define EBONY GFX_RGB(22, 23, 28)
#define INK   GFX_RGB(10, 10, 14)
#define HALO  GFX_RGB(6, 6, 8)

enum { HIT_NONE = 0, HIT_FACE, HIT_ROW, HIT_BUTTON };

static struct {
    outbind_t         bind;
    picker_apply_fn   apply;
    picker_select_fn  select;
    picker_drawing_fn source;
    /* The coprocessor's drawing, for the board drawing_board names; it
     * agreed with that board's catalogue when it was set. */
    link_drawing_t    drawing;
    uint16_t          drawing_board;
    bool              has_drawing;

    int      hit_kind;
    int      hit;            /**< row or catalogue index under the finger    */
    bool     open;           /**< the protocol list is open                  */
    /* The binding shown is not one a read confirmed: no button acts. */
    bool     unread;
    bool     chrome_valid[MAX_FBS];
} s;

/* The GPIO the coprocessor's programming port is bound to, or none.  Kept
 * out of `s` so a reset of the screen does not forget it. */
static uint8_t g_prog_gpio = PICKER_NO_PIN;

/*
 * A change invalidates every framebuffer, and a render validates the one it
 * drew.  Not a counter compared against the last frame drawn: with two
 * buffers alternating, a counter that is satisfied by the first of them
 * leaves the second holding the old picture, and clearing the others on
 * every paint instead makes the screen repaint for ever.
 *
 * A repaint draws the board, every button and both tables; a picker that
 * never settled would spend that on every frame for as long as it is open.
 */
static void touched(void) { picker_screen_invalidate(); }

void picker_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

const outbind_t *picker_screen_binding(void) { return &s.bind; }

void picker_screen_set_binding(const outbind_t *b)
{
    if (b != NULL) {
        s.bind = *b;
        /* Read off the wire or restored, so it is trimmed before it is drawn
         * rather than trusted to mean something on this board. */
        outbind_trim(&s.bind);
        touched();
    }
}

void picker_screen_follow(uint8_t proto, bool editable)
{
    const bool unread = !editable;
    if (proto < OUTBIND_PROTOS && proto != s.bind.proto) {
        outbind_set_proto(&s.bind, proto);
        touched();
    }
    if (unread != s.unread) {
        s.unread = unread;
        /* A press begun while the binding could be edited does not act on
         * its release. */
        if (s.hit_kind == HIT_BUTTON) {
            s.hit_kind = HIT_NONE;
            s.hit = -1;
        }
        touched();
    }
}

bool picker_screen_editable(void) { return !s.unread; }

void picker_screen_set_apply(picker_apply_fn fn) { s.apply = fn; }

void picker_screen_set_select(picker_select_fn fn) { s.select = fn; }

void picker_screen_set_drawing_source(picker_drawing_fn fn) { s.source = fn; }

bool picker_screen_set_drawing(const uint8_t *data, uint32_t bytes,
                               const char **why)
{
    const char *reason = "no drawing";
    link_drawing_t d;
    bool ok = false;
    if (data != NULL && bytes != 0u
        && link_drawing_parse(&d, data, bytes, &reason)) {
        ok = board_drawing_agrees(&d, outbind_board(s.bind.board), &reason);
    }
    s.has_drawing   = ok;
    s.drawing       = ok ? d : (link_drawing_t){ 0 };
    s.drawing_board = s.bind.board;
    if (why != NULL) {
        *why = ok ? "" : reason;
    }
    touched();
    return ok;
}

void picker_screen_set_prog_pin(uint8_t gpio)
{
    if (gpio != g_prog_gpio) {
        g_prog_gpio = gpio;
        touched();
    }
}

static const outbind_shape_t *shape(void)
{
    const outbind_board_t *bd = outbind_board(s.bind.board);
    return (bd != NULL) ? bd->shape : NULL;
}

/* The drawing, while it is the one for the board the binding names. */
static const link_drawing_t *drawing(void)
{
    return (s.has_drawing && s.drawing_board == s.bind.board) ? &s.drawing
                                                              : NULL;
}

bool picker_screen_has_drawing(void) { return drawing() != NULL; }

bool picker_screen_can_draw(void)
{
    return (drawing() != NULL || shape() != NULL)
           && outbind_pin_count(s.bind.board) > 0u;
}

static uint8_t proto_shown(void)
{
    return (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
}

/* ------------------------------------------------------------ board art */

static bool pad_xy(uint8_t pad, int *px, int *py);

/*
 * Everything that depends on what the board looks like, as against where
 * its pads are, is in this section: the box the board occupies and
 * draw_board().  The buttons, the marks and the tables take pad positions
 * from pad_xy() and nothing from the drawing.
 */

/*
 * The box the board occupies: BOARD_W wide and whatever height the
 * outline's aspect gives, the drawing's outline or else the shape's.  The
 * drawing is drawn to scale into it; its outline is the shape's whenever
 * the board has one (board_drawing_agrees()).
 */
static void board_box(int *x, int *y, int *w, int *h)
{
    const link_drawing_t *d = drawing();
    const outbind_shape_t *sh = shape();
    uint16_t ow = 0u, oh = 0u;
    if (d != NULL) {
        ow = d->width;
        oh = d->height;
    } else if (sh != NULL) {
        ow = sh->width_cmm;
        oh = sh->height_cmm;
    }
    int bh = BOARD_MAX_H;
    if (ow != 0u) {
        bh = (int)(((uint32_t)BOARD_W * oh) / ow);
        if (bh > BOARD_MAX_H) { bh = BOARD_MAX_H; }
    }
    *w = BOARD_W;
    *h = bh;
    *x = (SCREEN_W - BOARD_W) / 2;
    *y = (SCREEN_H - bh) / 2;
}

/* The board itself: the coprocessor's drawing, or its outline and pads. */
static void draw_board(gfx_canvas_t *c)
{
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);

    const link_drawing_t *d = drawing();
    if (d != NULL) {
        board_drawing_render(c, d, bx, by, bw, bh, UI_BG);
        return;
    }

    /*
     * No drawing, so the board is drawn from its shape: an outline and
     * every pad the shape places.  The pads are what an operator counts
     * along, so all of them are drawn and not only the ones that are GPIOs.
     */
    gfx_fill_round_rect(c, bx, by, bw, bh, 6, GFX_RGB(18, 42, 30));
    gfx_draw_round_rect(c, bx, by, bw, bh, 6, GFX_RGB(60, 110, 84));
    const outbind_shape_t *sh = shape();
    if (sh == NULL) {
        return;
    }
    const unsigned pads = (unsigned)sh->per_side * 2u;
    for (unsigned p = 1; p <= pads; ++p) {
        int px, py;
        if (pad_xy((uint8_t)p, &px, &py)) {
            gfx_fill_circle(c, px, py, 4, GFX_RGB(196, 160, 84));
            gfx_fill_circle(c, px, py, 2, GFX_RGB(24, 24, 28));
        }
    }
}

/* ------------------------------------------------------------- geometry */

/*
 * Where a pad sits on screen, from where the board says it sits on itself:
 * the drawing's pad record, else the shape's grid.  Scaled the way
 * board_drawing_render() scales a ring, so a trace ends on its ring.
 */
static bool pad_xy(uint8_t pad, int *px, int *py)
{
    const link_drawing_t *d = drawing();
    uint16_t xc, yc, ow, oh;
    if (d != NULL) {
        link_draw_pad_t p;
        if (!link_drawing_find_pad(d, pad, &p)) {
            return false;
        }
        xc = p.x;
        yc = p.y;
        ow = d->width;
        oh = d->height;
    } else {
        const outbind_shape_t *sh = shape();
        if (sh == NULL || !outbind_pad_xy(sh, pad, &xc, &yc)) {
            return false;
        }
        ow = sh->width_cmm;
        oh = sh->height_cmm;
    }
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    *px = bx + board_drawing_scale(xc, bw, ow);
    *py = by + board_drawing_scale(yc, bh, oh);
    return true;
}

/* Which of the two pad rows a pad is in: the near one is the lower. */
static bool pad_is_top(uint8_t pad)
{
    int x, y;
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    return pad_xy(pad, &x, &y) && y < by + bh / 2;
}

/*
 * The button for catalogue entry @p i and its tier, or false when it has
 * none.  Tier 0 is the row nearer the board.
 *
 * Only a pin an output may have gets one: a reserved pin is crossed on the
 * pad, which says the pin exists and is spoken for, and a button under it
 * would say it could be pressed.
 */
static bool button_at(int i, gfx_rect_t *out, int *tier_out)
{
    const uint16_t board = s.bind.board;
    const outbind_pin_t *pins = outbind_pins(board);
    const uint8_t n = outbind_pin_count(board);
    const outbind_board_t *bd = outbind_board(board);
    if (pins == NULL || i < 0 || i >= (int)n || bd == NULL) {
        return false;
    }
    /*
     * A reserved pin gets no button; a soldered board's pins do.
     *
     * Refusing them there would take the screen's answer away on exactly the
     * board that most needs it: an operator holding one still has to find
     * where GP7 is, and outbind_toggle() is what refuses the change. They are
     * drawn as held instead, which is what they are.
     */
    if (pins[i].reserved) {
        return false;
    }
    int px, py;
    if (!pad_xy(pins[i].pad, &px, &py)) {
        return false;
    }

    /*
     * The tier alternates along the row rather than by catalogue index, so
     * neighbouring pads never land in the same tier and the traces do not
     * cross.  Counting the buttons before this one in the same row is what
     * gives that, and it is cheap: a board has at most a few dozen pins.
     */
    const bool top = pad_is_top(pins[i].pad);
    int before = 0;
    for (int k = 0; k < (int)n; ++k) {
        if (k == i || pins[k].reserved) { continue; }
        int qx, qy;
        if (!pad_xy(pins[k].pad, &qx, &qy)) { continue; }
        if (pad_is_top(pins[k].pad) != top) { continue; }
        if (qx < px) { ++before; }
    }
    const int tier = before % TIERS;

    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    /*
     * The room above the board is `by` and no less: the router hands a screen
     * its own body, so the band is already out of these coordinates and
     * taking it off again would shrink every top button by the height of a
     * band it is not sharing the screen with.
     */
    const int th = (((top ? by : (SCREEN_H - (by + bh)))
                     - GAP) - (TIERS - 1) * TGAP) / TIERS;
    const int ty = top ? (by - GAP - (tier + 1) * th - tier * TGAP)
                       : (by + bh + GAP + tier * (th + TGAP));
    int x = px - BTNW / 2;
    if (x < 2) { x = 2; }
    if (x + BTNW > SCREEN_W - 2) { x = SCREEN_W - 2 - BTNW; }

    out->x = (int16_t)x;
    out->y = (int16_t)ty;
    out->w = (int16_t)BTNW;
    out->h = (int16_t)((th > 0) ? th : 0);
    if (tier_out != NULL) { *tier_out = tier; }
    return out->h > 0;
}

bool picker_screen_button_rect(uint8_t index, gfx_rect_t *out)
{
    gfx_rect_t r;
    if (!picker_screen_can_draw() || !button_at(index, &r, NULL)) {
        return false;
    }
    if (out != NULL) { *out = r; }
    return true;
}

/*
 * The centre and radius of the marker of pad-list entry @p e, or false for
 * a pad that has none.
 *
 * A rail's depth is the parity of the rails before it in its row, counted by
 * position and not by list order, so neighbouring rails always alternate.
 */
static bool marker_at(uint8_t e, int *mx, int *my, int *r)
{
    const outbind_pad_t *pads = outbind_pads(s.bind.board);
    const uint8_t n = outbind_pad_count(s.bind.board);
    if (pads == NULL || e >= n) {
        return false;
    }
    const uint8_t kind = pads[e].kind;
    if (kind != (uint8_t)LINK_PAD_GROUND && kind != (uint8_t)LINK_PAD_POWER) {
        return false;
    }
    int px, py;
    if (!pad_xy(pads[e].pad, &px, &py)) {
        return false;
    }
    const bool top = pad_is_top(pads[e].pad);
    int drop = GND_DROP;
    *r = GND_R;
    if (kind == (uint8_t)LINK_PAD_POWER) {
        int before = 0;
        for (uint8_t k = 0; k < n; ++k) {
            int qx, qy;
            if (k == e || pads[k].kind != (uint8_t)LINK_PAD_POWER
                || !pad_xy(pads[k].pad, &qx, &qy)
                || pad_is_top(pads[k].pad) != top) {
                continue;
            }
            if (qx < px) { ++before; }
        }
        drop = (before & 1) ? RAIL_DROP_1 : RAIL_DROP_0;
        *r = RAIL_R;
    }
    *mx = px;
    *my = top ? (py + drop) : (py - drop);
    return true;
}

bool picker_screen_marker_rect(uint8_t entry, gfx_rect_t *out)
{
    int mx, my, r;
    if (!picker_screen_can_draw() || !marker_at(entry, &mx, &my, &r)) {
        return false;
    }
    if (out != NULL) {
        *out = gfx_rect_make(mx - r - RING, my - r - RING,
                             2 * (r + RING), 2 * (r + RING));
    }
    return true;
}

gfx_rect_t picker_screen_area(picker_area_t area)
{
    switch (area) {
    case PICKER_AREA_PINS:
        return gfx_rect_make(PINS_X, PINS_Y, PINS_W, SCREEN_H - PINS_Y - 4);
    case PICKER_AREA_IN_USE:
        return gfx_rect_make(USE_X, USE_Y, USE_W, SCREEN_H - USE_Y - 4);
    case PICKER_AREA_HEAD:
    default:
        return gfx_rect_make(HEAD_X, HEAD_Y, HEAD_W, HEAD_H);
    }
}

gfx_rect_t picker_screen_list_face(void)
{
    return gfx_rect_make(HEAD_X, HEAD_Y, FACE_W, FACE_H);
}

static gfx_rect_t pop_rect(void)
{
    return gfx_rect_make(HEAD_X, HEAD_Y, POP_W,
                         (int)(OUTBIND_PROTOS * POP_ROW) + 8);
}

gfx_rect_t picker_screen_list_row(uint8_t proto)
{
    return gfx_rect_make(HEAD_X + 4, HEAD_Y + 4 + (int)proto * POP_ROW,
                         POP_W - 8, POP_ROW);
}

bool picker_screen_list_open(void) { return s.open; }

/* ---------------------------------------------------------------- state */

/* Whether the programming port has pin @p index: it is the port's and no
 * protocol holds it.  A pin a stored slot holds is that slot's -- the
 * coprocessor gives the port no pin then -- and can be unticked. */
static bool prog_taken(uint8_t index)
{
    return g_prog_gpio != PICKER_NO_PIN
           && index < outbind_pin_count(s.bind.board)
           && !outbind_pins(s.bind.board)[index].reserved
           && outbind_pins(s.bind.board)[index].gpio == g_prog_gpio
           && outbind_group_of(&s.bind, index) == 0u;
}

uint8_t picker_screen_prog_listed(void)
{
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (uint8_t i = 0; i < n; ++i) {
        if (prog_taken(i)) {
            return g_prog_gpio;
        }
    }
    return PICKER_NO_PIN;
}

picker_pin_t picker_screen_pin(uint8_t index, char *holder, size_t cap)
{
    if (holder != NULL && cap != 0u) {
        holder[0] = '\0';
    }
    if (index >= outbind_pin_count(s.bind.board)) {
        return PICKER_PIN_NONE;
    }
    const outbind_pin_t *pin = &outbind_pins(s.bind.board)[index];
    const uint8_t proto = proto_shown();
    const uint8_t held = outbind_group_of(&s.bind, index);
    const outbind_board_t *bd = outbind_board(s.bind.board);

    if (pin->reserved) {
        if (holder != NULL && cap != 0u && pin->held_by != NULL) {
            snprintf(holder, cap, "%s", pin->held_by);
        }
        return PICKER_PIN_RESERVED;
    }
    if (prog_taken(index)) {
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, "%s", TR(OUT_PROG_PORT));
        }
        return PICKER_PIN_HELD;
    }
    if (held != 0u && held != proto) {
        if (holder != NULL && cap != 0u) {
            snprintf(holder, cap, "%s", outbind_protos()[held].name);
        }
        return PICKER_PIN_HELD;
    }
    /* A soldered board's pins are nobody's to press, and a binding no read
     * has confirmed is nobody's to edit: shown, and not offered. */
    if (s.unread || (bd != NULL && bd->fixed)) {
        return PICKER_PIN_LOCKED;
    }
    if (held != 0u) {
        return PICKER_PIN_TICKED;
    }
    return outbind_can_add(&s.bind, index) ? PICKER_PIN_FREE
                                           : PICKER_PIN_REFUSED;
}

/* The channel the OUTPUTS page gives catalogue entry @p index, or -1.
 * Channels are allocated in pin order across every protocol, so a group's
 * first pin is not its channel 0 unless it also holds the lowest pin. */
static int8_t channel_of(uint8_t index)
{
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(&s.bind, regs);
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    for (uint8_t sl = 0; sl < used; ++sl) {
        const uint16_t *r = &regs[(size_t)sl * LINK_OS_STRIDE];
        if (pins[index].gpio == (uint8_t)r[LINK_OS_PIN]) {
            return (int8_t)LINK_OS_FIRST(r[LINK_OS_RANGE]);
        }
    }
    return -1;
}

bool picker_screen_row(uint8_t k, picker_row_t *out)
{
    const uint8_t proto = proto_shown();
    const uint8_t n = outbind_pin_count(s.bind.board);
    if (proto == 0u) {
        return false;
    }
    uint8_t seen = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (outbind_group_of(&s.bind, i) != proto) { continue; }
        if (seen++ != k) { continue; }
        if (out != NULL) {
            const outbind_pin_t *pin = &outbind_pins(s.bind.board)[i];
            out->index   = i;
            out->gpio    = pin->gpio;
            out->channel = channel_of(i);
            out->pad     = pin->pad;
        }
        return true;
    }
    return false;
}

void picker_screen_count(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return;
    }
    /* Slots used by every protocol, not by this one: they share the eight,
     * so this protocol's own count says nothing about what is left. */
    snprintf(buf, cap, TR(PK_SLOTS_OF),
             (unsigned)outbind_chosen_total(&s.bind),
             (unsigned)LINK_OUT_SLOTS);
}

bool picker_screen_reason(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (s.unread) {
        /* Why no button acts: the binding drawn is the last one read. */
        snprintf(buf, cap, "%s", TR(PK_NOT_READ));
        return true;
    }
    return outputs_reason_of(&s.bind, buf, cap);
}

/* --------------------------------------------------------------- drawing */

/* A cross with a dark halo, on a pad that has no button. */
static void cross(gfx_canvas_t *c, int x, int y, gfx_color_t col, int r,
                  int w)
{
    for (int o = -w / 2 - 1; o <= w / 2 + 1; ++o) {
        gfx_line(c, x - r + o, y - r, x + r + o, y + r, HALO);
        gfx_line(c, x - r + o, y + r, x + r + o, y - r, HALO);
    }
    for (int o = -w / 2; o <= w / 2; ++o) {
        gfx_line(c, x - r + o, y - r, x + r + o, y + r, col);
        gfx_line(c, x - r + o, y + r, x + r + o, y - r, col);
    }
}

static void trace(gfx_canvas_t *c, int x, int y0, int y1, gfx_color_t col)
{
    for (int o = -3; o <= 3; ++o) { gfx_line(c, x + o, y0, x + o, y1, HALO); }
    for (int o = -1; o <= 1; ++o) { gfx_line(c, x + o, y0, x + o, y1, col); }
}

/* A disc with a dark ring, as a rounded square so the fit check sees the
 * shape the label sits on. */
static void disc(gfx_canvas_t *c, int x, int y, int r, gfx_color_t col)
{
    gfx_fill_round_rect(c, x - r - RING, y - r - RING, 2 * (r + RING),
                        2 * (r + RING), r + RING, HALO);
    gfx_fill_round_rect(c, x - r, y - r, 2 * r, 2 * r, r, col);
}

/*
 * The grounds and the rails, marked inside the board on a short trace; RUN
 * and the enable crossed dim on the pad.
 *
 * A servo lead has three wires and the buttons describe one of them. These
 * are the other two, and they go inside the outline because that is the only
 * room left: the space beside the board is the buttons', and a mark on the
 * pad itself would be under 40 pixels like the pad is.
 */
static void draw_pads_that_are_not_pins(gfx_canvas_t *c)
{
    const outbind_pad_t *pads = outbind_pads(s.bind.board);
    const uint8_t n = outbind_pad_count(s.bind.board);
    if (pads == NULL) {
        return;                 /* this board does not say; nothing is drawn */
    }
    for (uint8_t i = 0; i < n; ++i) {
        int px, py;
        if (!pad_xy(pads[i].pad, &px, &py)) { continue; }

        if (pads[i].kind == (uint8_t)LINK_PAD_OTHER) {
            /* Neither a ground nor a rail, and not what anybody is looking
             * for: crossed dim, which says "not this one" without the red
             * of a pin the coprocessor keeps. */
            cross(c, px, py, GFX_RGB(120, 124, 132), 6, 2);
            continue;
        }
        int mx, my, r;
        if (!marker_at(i, &mx, &my, &r)) { continue; }

        const bool ground = (pads[i].kind == (uint8_t)LINK_PAD_GROUND);
        char label[6];
        if (ground) {
            label[0] = 'G';
            label[1] = '\0';
        } else if (pads[i].decivolts == 0u) {
            /* An input rail follows whatever feeds it, so it carries no
             * number rather than one that is only sometimes true. */
            snprintf(label, sizeof(label), "PWR");
        } else {
            snprintf(label, sizeof(label), "%uV%u",
                     (unsigned)(pads[i].decivolts / 10u),
                     (unsigned)(pads[i].decivolts % 10u));
        }
        const gfx_color_t face = ground ? GFX_RGB(232, 236, 244)
                                        : GFX_RGB(240, 176, 64);
        const int edge = (my > py) ? (my - r) : (my + r);
        for (int o = -2; o <= 2; ++o) {
            gfx_line(c, px + o, py, px + o, edge, HALO);
        }
        for (int o = -1; o <= 1; ++o) {
            gfx_line(c, px + o, py, px + o, edge, face);
        }
        disc(c, mx, my, r, face);
        const gfx_font_t *font = ground ? UI_FONT_HEAD : UI_FONT_LABEL;
        const int lw = gfx_text_width(font, label, 1);
        const int lh = ground ? 28 : 16;
        gfx_text(c, mx - lw / 2, my - lh / 2, label, font, INK, 1);
    }
}

static void draw_buttons(gfx_canvas_t *c)
{
    const uint16_t board = s.bind.board;
    const outbind_pin_t *pins = outbind_pins(board);
    const uint8_t n = outbind_pin_count(board);

    for (int i = 0; i < (int)n; ++i) {
        int px, py;
        if (!pad_xy(pins[i].pad, &px, &py)) { continue; }

        const picker_pin_t st = picker_screen_pin((uint8_t)i, NULL, 0);
        if (st == PICKER_PIN_RESERVED) {
            cross(c, px, py, UI_DANGER, 8, 3);
            continue;
        }

        gfx_rect_t r;
        int tier = 0;
        if (!button_at(i, &r, &tier)) { continue; }

        const bool on   = (st == PICKER_PIN_TICKED);
        const bool grey = !on && st != PICKER_PIN_FREE;
        const bool down = (s.hit_kind == HIT_BUTTON && s.hit == i);

        const gfx_color_t wire = grey ? GFX_RGB(104, 108, 116)
                                 : (on ? UI_ACCENT : GFX_RGB(214, 218, 226));
        const bool top = pad_is_top(pins[i].pad);
        const int meet = top ? (r.y + r.h) : r.y;
        trace(c, px, py, meet, wire);
        gfx_fill_circle(c, px, py, 5, HALO);
        gfx_fill_circle(c, px, py, 3, wire);

        /*
         * A free pin is a key: ivory in the row nearer the board, ebony in
         * the one beyond it, so neighbouring keys differ in colour as well
         * as in height and a finger counts along them like a keyboard.
         */
        gfx_color_t face, ink, edge;
        if (grey) {
            face = GFX_RGB(52, 55, 62);
            ink  = GFX_RGB(132, 136, 144);
            edge = GFX_RGB(78, 82, 90);
        } else if (on) {
            face = UI_ACCENT;
            ink  = GFX_RGB(6, 30, 36);
            edge = GFX_RGB(6, 40, 48);
        } else if (down) {
            face = UI_PANEL_HI;
            ink  = UI_TEXT;
            edge = UI_EDGE_HI;
        } else {
            face = (tier == 0) ? IVORY : EBONY;
            ink  = (tier == 0) ? EBONY : IVORY;
            edge = UI_EDGE_HI;
        }
        gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 8, 8, 8, face);
        gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 8, 8, 8, edge);

        char nm[6];
        snprintf(nm, sizeof(nm), "%u", (unsigned)pins[i].gpio);
        const int lw = gfx_text_width(UI_FONT_HEAD, nm, 1);
        gfx_text(c, r.x + (r.w - lw) / 2, r.y + (r.h - 28) / 2, nm,
                 UI_FONT_HEAD, ink, 1);
    }
}

/* Above the PINS table: the protocol list's face, the count, the reason. */
static void draw_head(gfx_canvas_t *c)
{
    ui_choice_face(c, picker_screen_list_face(),
                   outbind_protos()[proto_shown()].name,
                   s.hit_kind == HIT_FACE, s.open);
    char buf[64];
    picker_screen_count(buf, sizeof(buf));
    gfx_text(c, HEAD_X, COUNT_Y, buf, UI_FONT_LABEL, UI_TEXT_DIM, 1);
    if (picker_screen_reason(buf, sizeof(buf))) {
        gfx_text(c, HEAD_X, REASON_Y, buf, UI_FONT_LABEL, UI_WARN, 1);
    }
}

/* The left column: what this protocol holds, in channel order, with the
 * pad number printed on the board beside each. */
static void draw_mine(gfx_canvas_t *c)
{
    const int tx = PINS_X;
    int ty = PINS_Y;

    gfx_text(c, tx, ty, TR(PK_PINS), UI_FONT_HEAD, UI_ACCENT, 1);
    /* PAD once, as the head of its column: "GP13" and "PAD 17" together are
     * wider than the column beside the board. */
    const char *pad = TR(PK_PAD);
    gfx_text(c, tx + PINS_W - gfx_text_width(UI_FONT_LABEL, pad, 1), ty + 10,
             pad, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    ty += 30;
    gfx_fill_rect(c, tx, ty, PINS_W, 2, UI_EDGE_HI);
    ty += 14;

    picker_row_t row;
    uint8_t k = 0;
    for (; ty < SCREEN_H - 24 && picker_screen_row(k, &row); ++k) {
        char buf[8];
        snprintf(buf, sizeof(buf), "CH%d", row.channel);
        gfx_text(c, tx, ty, buf, UI_FONT_LABEL, UI_TEXT_DIM, 1);
        snprintf(buf, sizeof(buf), "GP%u", (unsigned)row.gpio);
        gfx_text(c, tx + 40, ty - 6, buf, UI_FONT_HEAD, UI_ACCENT, 1);
        snprintf(buf, sizeof(buf), "%u", (unsigned)row.pad);
        gfx_text(c, tx + PINS_W - gfx_text_width(UI_FONT_LABEL, buf, 1), ty,
                 buf, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
        ty += ROW_H;
    }
    if (k == 0u) {
        gfx_text(c, tx, ty, TR(PK_NONE_YET), UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
}

/* A line of the IN USE table, its box not yet placed. */
static uint8_t use_add(picker_use_t *t, uint8_t n, picker_use_kind_t kind,
                       uint8_t proto, uint8_t gpio, int8_t channel)
{
    if (n >= PICKER_USE_MAX) {
        return n;               /* no binding gives more; kept in bounds */
    }
    t[n].kind    = kind;
    t[n].proto   = proto;
    t[n].gpio    = gpio;
    t[n].channel = channel;
    return (uint8_t)(n + 1u);
}

/*
 * The IN USE table: the programming port's pin first, because a pin the port
 * drives is the one a lead must not go on without knowing; then the pins
 * other protocols hold, groups in the order their first pin falls, so the
 * channels count down the page rather than jumping about with the protocol
 * numbering.  Returns the number of lines.
 */
static uint8_t in_use(picker_use_t t[PICKER_USE_MAX])
{
    const uint8_t proto = proto_shown();
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    const uint8_t n = outbind_pin_count(s.bind.board);
    uint8_t k = 0;

    const uint8_t prog = picker_screen_prog_listed();
    if (prog != PICKER_NO_PIN) {
        k = use_add(t, k, PICKER_USE_NAME, PICKER_USE_PROG, prog, -1);
        k = use_add(t, k, PICKER_USE_PIN, PICKER_USE_PROG, prog, -1);
    }
    uint32_t listed = 0u;
    for (uint8_t first = 0; first < n; ++first) {
        const uint8_t g = outbind_group_of(&s.bind, first);
        if (g == 0u || g == proto) { continue; }
        if ((listed & ((uint32_t)1u << g)) != 0u) { continue; }
        listed |= (uint32_t)1u << g;
        k = use_add(t, k, PICKER_USE_NAME, g, 0u, -1);
        for (uint8_t i = first; i < n; ++i) {
            if (outbind_group_of(&s.bind, i) != g) { continue; }
            k = use_add(t, k, PICKER_USE_PIN, g, pins[i].gpio, channel_of(i));
        }
    }

    /* 28 px pin rows while they reach no further than the foot. */
    int full = 0;
    for (uint8_t i = 0; i < k; ++i) {
        full += (t[i].kind == PICKER_USE_PIN) ? USE_PIN_H : USE_NAME_H;
    }
    const bool compact = (USE_TOP + full > USE_FOOT);
    int y = USE_TOP;
    for (uint8_t i = 0; i < k; ++i) {
        const int h = (t[i].kind == PICKER_USE_NAME) ? USE_NAME_H
                      : (compact ? USE_PIN_H_C : USE_PIN_H);
        t[i].compact = compact;
        t[i].rect = gfx_rect_make(USE_X, y, USE_W, h);
        y += h;
    }
    return k;
}

bool picker_screen_in_use(uint8_t k, picker_use_t *out)
{
    picker_use_t t[PICKER_USE_MAX];
    if (!picker_screen_can_draw() || k >= in_use(t)) {
        return false;
    }
    if (out != NULL) { *out = t[k]; }
    return true;
}

/* The right column: the programming port's pin, then pins another protocol
 * holds, and which one. */
static void draw_theirs(gfx_canvas_t *c)
{
    const int tx = USE_X;
    int ty = USE_Y;
    const gfx_color_t grey = GFX_RGB(150, 154, 162);

    gfx_text(c, tx, ty, TR(PK_IN_USE), UI_FONT_HEAD, grey, 1);
    ty += 30;
    gfx_fill_rect(c, tx, ty, USE_W, 2, UI_EDGE_HI);
    ty += 8;
    gfx_text(c, tx, ty, TR(PK_BY_OTHERS), UI_FONT_LABEL, UI_TEXT_FAINT, 1);

    picker_use_t t[PICKER_USE_MAX];
    const uint8_t lines = in_use(t);
    char row[24];
    for (uint8_t i = 0; i < lines; ++i) {
        const gfx_rect_t r = t[i].rect;
        if (t[i].kind == PICKER_USE_NAME) {
            const bool port = (t[i].proto == PICKER_USE_PROG);
            const char *name = port ? TR(OUT_PROG_PORT)
                                    : outbind_protos()[t[i].proto].name;
            gfx_text(c, r.x, r.y + 2, name, UI_FONT_LABEL,
                     port ? UI_DANGER : UI_TEAL, 1);
            continue;
        }
        /* The port has no channel, so no CH. */
        const int label_y = t[i].compact ? (r.y + 2) : (r.y + 6);
        if (t[i].channel >= 0) {
            snprintf(row, sizeof(row), "CH%d", t[i].channel);
            gfx_text(c, r.x + 4, label_y, row, UI_FONT_LABEL, UI_TEXT_FAINT,
                     1);
        }
        snprintf(row, sizeof(row), "GP%u", (unsigned)t[i].gpio);
        if (t[i].compact) {
            gfx_text(c, r.x + 44, label_y, row, UI_FONT_LABEL, grey, 1);
        } else {
            gfx_text(c, r.x + 44, r.y, row, UI_FONT_HEAD, grey, 1);
        }
    }
    if (lines == 0u) {
        gfx_text(c, tx, USE_TOP, TR(PK_NONE), UI_FONT_LABEL, UI_TEXT_FAINT,
                 1);
    }
}

static void draw_list(gfx_canvas_t *c)
{
    ui_choice_list(c, pop_rect());
    const outbind_proto_t *p = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        ui_choice_row(c, picker_screen_list_row(i), p[i].name,
                      i == proto_shown(),
                      s.hit_kind == HIT_ROW && s.hit == (int)i);
    }
}

/* ----------------------------------------------------------------- input */

static int button_under(int x, int y)
{
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (int i = 0; i < (int)n; ++i) {
        gfx_rect_t r;
        if (button_at(i, &r, NULL) && gfx_rect_contains(r, x, y)) {
            return i;
        }
    }
    return -1;
}

/* A pick says which set is shown and which group the next tap joins.  It
 * changes no page, so nothing is written; the outputs screen is told, so
 * the two screens show one selection. */
static void pick(uint8_t proto)
{
    outbind_set_proto(&s.bind, proto);
    if (s.select != NULL) {
        s.select(proto);
    }
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;

    if (evt->type == TOUCH_EVENT_DOWN) {
        s.hit_kind = HIT_NONE;
        s.hit = -1;
        if (s.open) {
            for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
                if (gfx_rect_contains(picker_screen_list_row(i), x, y)) {
                    s.hit_kind = HIT_ROW;
                    s.hit = (int)i;
                    touched();
                    return;
                }
            }
            /* A press anywhere else closes it without choosing: an open
             * list must not be a trap, and the press is spent on that. */
            s.open = false;
            touched();
            return;
        }
        if (!picker_screen_can_draw()) {
            return;
        }
        if (gfx_rect_contains(picker_screen_list_face(), x, y)) {
            s.hit_kind = HIT_FACE;
            touched();
            return;
        }
        const int b = s.unread ? -1 : button_under(x, y);
        if (b >= 0) {
            s.hit_kind = HIT_BUTTON;
            s.hit = b;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int kind = s.hit_kind, was = s.hit;
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    if (kind == HIT_NONE) {
        return;
    }
    touched();
    if (!touch_event_is_tap_up(evt)) {
        return;              /* a release the finger did not make */
    }
    if (kind == HIT_FACE) {
        if (gfx_rect_contains(picker_screen_list_face(), x, y)) {
            s.open = true;
        }
        return;
    }
    if (kind == HIT_ROW) {
        if (gfx_rect_contains(picker_screen_list_row((uint8_t)was), x, y)) {
            s.open = false;
            pick((uint8_t)was);
        }
        return;
    }
    if (button_under(x, y) != was) {
        return;              /* the finger left the button it pressed */
    }
    /* Only a button that is offered acts.  A refused one is not silent: the
     * reason is printed under the protocol list. */
    const picker_pin_t st = picker_screen_pin((uint8_t)was, NULL, 0);
    if (st != PICKER_PIN_FREE && st != PICKER_PIN_TICKED) {
        return;
    }
    if (outbind_toggle(&s.bind, (uint8_t)was) && s.apply != NULL) {
        s.apply(&s.bind);
    }
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    outbind_init(&s.bind);
    s.hit = -1;
    touched();
}

static void enter(void)
{
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    s.open = false;
    /* Asked on the way in, so a drawing that arrived since the last visit
     * is the one shown, and one for a board that has been swapped is not. */
    if (s.source != NULL) {
        s.source(s.bind.board);
    }
    touched();
}

static void leave(void)
{
    s.hit_kind = HIT_NONE;
    s.hit = -1;
    s.open = false;
    /*
     * OFF picked here ends with the visit, as it does on the Outputs
     * screen.  Kept there, the next frame's follow would select it here
     * again, and the next visit would open on a board where no key adds a
     * pin.
     */
    outputs_screen_end_off();
    if (s.bind.proto == 0u) {
        outbind_set_proto(&s.bind, outbind_wire_proto(&s.bind));
        touched();
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    if (buffer_index >= 0 && buffer_index < MAX_FBS
        && s.chrome_valid[buffer_index]) {
        return;
    }

    gfx_fill_rect(c, 0, 0, c->width, c->height, UI_BG);

    if (!picker_screen_can_draw()) {
        /*
         * A board that does not say where its pads are cannot be drawn, and
         * drawing a guessed one would point at the wrong pad as confidently
         * as the right one.
         *
         * Drawn as a panel rather than a line of text: an empty screen reads
         * as a screen that failed, and this one is working -- it is saying
         * what it does not know, and where the pins are still to be had.
         */
        const int pw = 520, ph = 150;
        const int px = (SCREEN_W - pw) / 2, py = (SCREEN_H - ph) / 2;
        gfx_fill_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_EDGE);
        const char *const lines[] = {
            TR(PK_NO_ART_1), TR(PK_NO_ART_2), TR(PK_NO_ART_3),
            TR(PK_NO_ART_4),
        };
        int ty = py + 26;
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            const gfx_font_t *font = (i == 0u) ? UI_FONT_HEAD : UI_FONT_LABEL;
            const gfx_color_t ink = (i == 0u) ? UI_TEXT : UI_TEXT_FAINT;
            const int w = gfx_text_width(font, lines[i], 1);
            gfx_text(c, px + (pw - w) / 2, ty, lines[i], font, ink, 1);
            ty += (i == 0u) ? 40 : 24;
        }
    } else {
        draw_board(c);
        draw_pads_that_are_not_pins(c);
        /* The text before the buttons, so a string that grows into a button
         * is painted over and the fit check sees it. */
        draw_head(c);
        draw_mine(c);
        draw_theirs(c);
        draw_buttons(c);
        if (s.open) {
            draw_list(c);
        }
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
    }
}

/*
 * Touch events were lost between two frames, so the button recorded under
 * the finger may not be under one any more.  A release toggles the pin when
 * it lands on the button the press began on, and a stale record turns the
 * next release over that button into a binding change nobody asked for.
 */
static void cancel(void)
{
    if (s.hit_kind != HIT_NONE) {
        s.hit_kind = HIT_NONE;
        s.hit = -1;
        touched();
    }
}

static const ui_screen_t s_screen = {
    .title  = "PICK A PIN",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = NULL,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *picker_screen(void) { return &s_screen; }
