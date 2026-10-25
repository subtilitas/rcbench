/**
 * @file picker_draw.c
 * @brief PICK A PIN's drawing: the board, its buttons and marks, the head with
 *        the protocol list's face, the PINS and IN USE tables and the open
 *        list. picker_screen.c holds the state, the touch handling and the
 *        screen's interface (picker_screen.h); picker_geometry.c says where
 *        things sit; picker_screen_int.h is what the three share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "board_drawing.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "outputs_screen_int.h"
#include "picker_port.h"
#include "picker_screen.h"
#include "port_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "picker_screen_int.h"

/**
 * @brief The board itself: the IOmcu's drawing, or its outline and pads.
 */
static void draw_board(gfx_canvas_t *c)
{
    int bx, by, bw, bh;
    pk_board_box(&bx, &by, &bw, &bh);

    const link_drawing_t *d = pk_drawing();
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
    const outbind_shape_t *sh = pk_shape();
    if (sh == NULL) {
        return;
    }
    const unsigned pads = (unsigned)sh->per_side * 2u;
    for (unsigned p = 1; p <= pads; ++p) {
        int px, py;
        if (pk_pad_xy((uint8_t)p, &px, &py)) {
            gfx_fill_circle(c, px, py, 4, GFX_RGB(196, 160, 84));
            gfx_fill_circle(c, px, py, 2, GFX_RGB(24, 24, 28));
        }
    }
}

/* --------------------------------------------------------------- drawing */

/** @brief A cross with a dark halo, on a pad that has no button. */
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

/**
 * @brief Draws a vertical trace from @p y0 to @p y1 at column @p x with a dark
 *        halo.
 */
static void trace(gfx_canvas_t *c, int x, int y0, int y1, gfx_color_t col)
{
    for (int o = -3; o <= 3; ++o) { gfx_line(c, x + o, y0, x + o, y1, HALO); }
    for (int o = -1; o <= 1; ++o) { gfx_line(c, x + o, y0, x + o, y1, col); }
}

/**
 * @brief A disc with a dark ring, as a rounded square so the fit check sees the
 *        shape the label sits on.
 */
static void disc(gfx_canvas_t *c, int x, int y, int r, gfx_color_t col)
{
    gfx_fill_round_rect(c, x - r - RING, y - r - RING, 2 * (r + RING),
                        2 * (r + RING), r + RING, HALO);
    gfx_fill_round_rect(c, x - r, y - r, 2 * r, 2 * r, r, col);
}

/**
 * @brief The grounds and the rails, marked inside the board on a short trace; RUN
 *        and the enable crossed dim on the pad.
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
        if (!pk_pad_xy(pads[i].pad, &px, &py)) { continue; }

        if (pads[i].kind == (uint8_t)LINK_PAD_OTHER) {
            /* Neither a ground nor a rail, and not what anybody is looking
             * for: crossed dim, which says "not this one" without the red
             * of a pin the IOmcu keeps. */
            cross(c, px, py, GFX_RGB(120, 124, 132), 6, 2);
            continue;
        }
        int mx, my, r;
        if (!pk_marker_at(i, &mx, &my, &r)) { continue; }

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
        const gfx_font_t *font = ground ? PK_FONT_PIN : PK_FONT_FINE;
        const int lw = gfx_text_width(font, label, 1);
        const int lh = gfx_text_height(font, 1);
        gfx_text(c, mx - lw / 2, my - lh / 2, label, font, INK, 1);
    }
}

/** @brief Draws the pin buttons of both tiers, the picked ones lit. */
static void draw_buttons(gfx_canvas_t *c)
{
    const uint16_t board = s.bind.board;
    const outbind_pin_t *pins = outbind_pins(board);
    const uint8_t n = outbind_pin_count(board);

    for (int i = 0; i < (int)n; ++i) {
        int px, py;
        if (!pk_pad_xy(pins[i].pad, &px, &py)) { continue; }

        const picker_pin_t st = picker_screen_pin((uint8_t)i, NULL, 0);
        if (st == PICKER_PIN_RESERVED) {
            cross(c, px, py, UI_DANGER, 8, 3);
            continue;
        }

        gfx_rect_t r;
        int tier = 0;
        if (!pk_button_at(i, &r, &tier)) { continue; }

        const bool on   = (st == PICKER_PIN_TICKED);
        const bool grey = !on && st != PICKER_PIN_FREE;
        const bool down = (s.hit_kind == HIT_BUTTON && s.hit == i);

        const gfx_color_t wire = grey ? GFX_RGB(104, 108, 116)
                                 : (on ? UI_ACCENT : GFX_RGB(214, 218, 226));
        const bool top = pk_pad_is_top(pins[i].pad);
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
        const int lw = gfx_text_width(PK_FONT_HEAD, nm, 1);
        gfx_text(c, r.x + (r.w - lw) / 2,
                 r.y + (r.h - gfx_text_height(PK_FONT_HEAD, 1)) / 2, nm,
                 PK_FONT_HEAD, ink, 1);
    }
}

/**
 * @brief Above the PINS table: the protocol list's face, the count, the reason.
 */
static void draw_head(gfx_canvas_t *c)
{
    char buf[96];
    if (s.port >= 0) {
        outputs_port_name((uint8_t)s.port, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", outbind_protos()[pk_proto_shown()].name);
    }
    ui_choice_face(c, picker_screen_list_face(), buf, s.hit_kind == HIT_FACE,
                   s.open);
    picker_screen_count(buf, sizeof(buf));
    gfx_text(c, HEAD_X, COUNT_Y, buf, PK_FONT_LABEL, UI_TEXT_DIM, 1);
    if (picker_screen_reason(buf, sizeof(buf))) {
        (void)outputs_wrap(c, gfx_rect_make(HEAD_X, REASON_Y, HEAD_W, 20),
                           REASON_PITCH, buf, 2, PK_FONT_LABEL, UI_WARN,
                           false);
    }
}

/**
 * @brief The left column: what this protocol holds, in channel order, with the
 *        pad number printed on the board beside each.
 */
static void draw_mine(gfx_canvas_t *c)
{
    /* OFF holds no pin, so a table of its pins says nothing, as the count
     * on the Outputs screen. */
    if (outbind_protos()[pk_proto_shown()].max_pins == 0u && s.port < 0) {
        return;
    }
    const int tx = PINS_X;
    int ty = PINS_Y;

    gfx_text(c, tx, ty, TR(PK_PINS), PK_FONT_HEAD, UI_ACCENT, 1);
    /* PAD once, as the head of its column: "GP13" and "PAD 17" together are
     * wider than the column beside the board. */
    const char *pad = TR(PK_PAD);
    gfx_text(c, tx + PINS_W - gfx_text_width(PK_FONT_LABEL, pad, 1), ty + 12,
             pad, PK_FONT_LABEL, UI_TEXT_FAINT, 1);
    gfx_fill_rect(c, tx, ty + 38, PINS_W, 2, UI_EDGE_HI);
    ty = PINS_TOP;

    /* A row's label-face text 8px down, level with the GP name's
     * capitals. */
    picker_row_t row;
    uint8_t k = 0;
    for (; ty + ROW_H <= FOOT && picker_screen_row(k, &row); ++k) {
        char buf[8];
        if (row.channel >= 0) {
            snprintf(buf, sizeof(buf), "CH%d", row.channel);
            gfx_text(c, tx, ty + 8, buf, PK_FONT_LABEL, UI_TEXT_DIM, 1);
        }
        snprintf(buf, sizeof(buf), "GP%u", (unsigned)row.gpio);
        gfx_text(c, tx + 40, ty, buf, PK_FONT_HEAD, UI_ACCENT, 1);
        snprintf(buf, sizeof(buf), "%u", (unsigned)row.pad);
        gfx_text(c, tx + PINS_W - gfx_text_width(PK_FONT_LABEL, buf, 1),
                 ty + 8, buf, PK_FONT_LABEL, UI_TEXT_FAINT, 1);
        ty += ROW_H;
    }
    if (k == 0u) {
        gfx_text(c, tx, ty, TR(PK_NONE_YET), PK_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
}

/** @brief A line of the IN USE table, its box not yet placed. */
static uint8_t use_add(picker_use_t *t, uint8_t n, picker_use_kind_t kind,
                       uint8_t proto, uint8_t gpio, int8_t channel)
{
    if (n >= PICKER_USE_MAX) {
        return n;               /* no binding gives more (PICKER_USE_MAX) */
    }
    t[n].kind    = kind;
    t[n].proto   = proto;
    t[n].gpio    = gpio;
    t[n].channel = channel;
    return (uint8_t)(n + 1u);
}

/**
 * @brief The IN USE table: the programming port's pin first, because a pin the port
 *        drives is the one a lead must not go on without knowing; then what SETUP
 *        > INTERFACES holds, for the same reason; then the pins other protocols
 *        hold, groups in the order their first pin falls, so the channels count
 *        down the page rather than jumping about with the protocol numbering.
 *        Returns the number of lines.
 */
static uint8_t in_use(picker_use_t t[PICKER_USE_MAX])
{
    const uint8_t proto = pk_proto_shown();
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    const uint8_t n = outbind_pin_count(s.bind.board);
    uint8_t k = 0;

    /* Every port's pin under the one name, in the board's order; a pin two
     * ports report is listed once. */
    const uint8_t prog = picker_screen_prog_listed();
    if (prog != PICKER_NO_PIN) {
        k = use_add(t, k, PICKER_USE_NAME, PICKER_USE_PROG, prog, -1);
        for (uint8_t i = 0; i < n; ++i) {
            if (pk_prog_taken(i)
                && !picker_port_own_pin(&s.bind, i, s.port)) {
                k = use_add(t, k, PICKER_USE_PIN, PICKER_USE_PROG,
                            pins[i].gpio, -1);
            }
        }
    }
    /* Each interface's pins under its name, in the board's order. */
    for (int f = OUTPUTS_IFACE_SENSE; f < OUTPUTS_IFACE_COUNT; ++f) {
        bool named = false;
        for (uint8_t i = 0; i < n; ++i) {
            if (pk_iface_taken(i) != (outputs_iface_t)f) { continue; }
            if (!named) {
                k = use_add(t, k, PICKER_USE_NAME,
                            (uint8_t)PICKER_USE_IFACE(f), pins[i].gpio, -1);
                named = true;
            }
            k = use_add(t, k, PICKER_USE_PIN, (uint8_t)PICKER_USE_IFACE(f),
                        pins[i].gpio, -1);
        }
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
            k = use_add(t, k, PICKER_USE_PIN, g, pins[i].gpio, pk_channel_of(i));
        }
    }

    /* 28px pin rows while they reach no further than the foot, then 20,
     * then 16; then the names 16px too. */
    int names = 0, pins_n = 0;
    for (uint8_t i = 0; i < k; ++i) {
        if (t[i].kind == PICKER_USE_PIN) { ++pins_n; } else { ++names; }
    }
    const int room = USE_FOOT - USE_TOP;
    const bool compact = pins_n * USE_PIN_H > room - names * USE_NAME_H;
    const int pin_h = !compact ? USE_PIN_H
                      : (pins_n * USE_PIN_H_C <= room - names * USE_NAME_H)
                          ? USE_PIN_H_C : USE_PIN_H_D;
    const int name_h = (names * USE_NAME_H + pins_n * pin_h <= room)
                           ? USE_NAME_H : USE_PIN_H_D;
    /*
     * Still past the foot: the lines that fit, the last of them a "+N"
     * line with the number of lines left out.  No binding reaches it on
     * the 1024 x 600 panel: the fullest, 4 ports and every interface
     * beside the 8 slots, lists 27 lines in the 432px under the heading.
     */
    if (names * name_h + pins_n * pin_h > room) {
        int y = 0;
        uint8_t fit = 0;
        while (fit < k
               && y + ((t[fit].kind == PICKER_USE_PIN) ? pin_h : name_h)
                      + USE_PIN_H_D <= room) {
            y += (t[fit].kind == PICKER_USE_PIN) ? pin_h : name_h;
            ++fit;
        }
        const uint8_t left = (uint8_t)(k - fit);
        t[fit].kind    = PICKER_USE_MORE;
        t[fit].proto   = 0u;
        t[fit].gpio    = left;
        t[fit].channel = -1;
        k = (uint8_t)(fit + 1u);
    }
    int y = USE_TOP;
    for (uint8_t i = 0; i < k; ++i) {
        const int h = (t[i].kind == PICKER_USE_PIN)    ? pin_h
                      : (t[i].kind == PICKER_USE_MORE) ? USE_PIN_H_D
                                                       : name_h;
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

/**
 * @brief The right column: the programming port's pin, then pins another protocol
 *        holds, and which one.
 */
static void draw_theirs(gfx_canvas_t *c)
{
    const int tx = USE_X;
    int ty = USE_Y;
    const gfx_color_t grey = GFX_RGB(150, 154, 162);

    gfx_text(c, tx, ty, TR(PK_IN_USE), PK_FONT_HEAD, grey, 1);
    /* The rule under the heading's 36px, BY OTHERS in 8 x 16 under it,
     * ending at USE_TOP: the fullest table needs every row under it. */
    gfx_fill_rect(c, tx, ty + 36, USE_W, 2, UI_EDGE_HI);
    gfx_text(c, tx, ty + 39, TR(PK_BY_OTHERS), PK_FONT_FINE, UI_TEXT_FAINT,
             1);

    picker_use_t t[PICKER_USE_MAX];
    const uint8_t lines = in_use(t);
    char row[24];
    for (uint8_t i = 0; i < lines; ++i) {
        const gfx_rect_t r = t[i].rect;
        if (t[i].kind == PICKER_USE_MORE) {
            snprintf(row, sizeof(row), "+%u", (unsigned)t[i].gpio);
            gfx_text(c, r.x + 4, r.y, row, PK_FONT_FINE, UI_TEXT_FAINT, 1);
            continue;
        }
        if (t[i].kind == PICKER_USE_NAME) {
            /* PROG PORT and the interfaces in red, as a pin no protocol
             * may take; a protocol in teal.  A 20px name row holds the
             * label face, a 16px one 8 x 16. */
            const bool port = (t[i].proto == PICKER_USE_PROG);
            const bool iface = PICKER_USE_IS_IFACE(t[i].proto);
            const char *name =
                port ? TR(OUT_PROG_PORT)
                : iface ? outputs_iface_name(
                              (outputs_iface_t)(t[i].proto - 0xF0u))
                        : outbind_protos()[t[i].proto].name;
            gfx_text(c, r.x, r.y, name,
                     (r.h > 16) ? PK_FONT_LABEL : PK_FONT_FINE,
                     (port || iface) ? UI_DANGER : UI_TEAL, 1);
            continue;
        }
        /* The port has no channel, so no CH.  A 28px row: CH in the
         * label face level with the GPIO's capitals in PK_FONT_PIN; a
         * 20px row both in the label face, a 16px one in 8 x 16. */
        const gfx_font_t *lf = (r.h > 16) ? PK_FONT_LABEL : PK_FONT_FINE;
        const int label_y = t[i].compact ? r.y : (r.y + 5);
        if (t[i].channel >= 0) {
            snprintf(row, sizeof(row), "CH%d", t[i].channel);
            gfx_text(c, r.x + 4, label_y, row, lf, UI_TEXT_FAINT, 1);
        }
        snprintf(row, sizeof(row), "GP%u", (unsigned)t[i].gpio);
        if (t[i].compact) {
            gfx_text(c, r.x + 44, label_y, row, lf, grey, 1);
        } else {
            gfx_text(c, r.x + 44, r.y, row, PK_FONT_PIN, grey, 1);
        }
    }
    if (lines == 0u) {
        gfx_text(c, tx, USE_TOP, TR(PK_NONE), PK_FONT_LABEL, UI_TEXT_FAINT,
                 1);
    }
}

/** @brief Draws the open protocol list with its PROG PORT entries. */
static void draw_list(gfx_canvas_t *c)
{
    ui_choice_list(c, pk_pop_rect());
    const outbind_proto_t *p = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        ui_choice_row(c, picker_screen_list_row(i), p[i].name,
                      s.port < 0 && i == pk_proto_shown(),
                      s.hit_kind == HIT_ROW && s.hit == (int)i);
    }
    for (int n = 0; n < (int)outputs_screen_port_entries(); ++n) {
        const int i = (int)OUTBIND_PROTOS + n;
        char name[24];
        outputs_port_name((uint8_t)n, name, sizeof(name));
        ui_choice_row(c, picker_screen_list_row((uint8_t)i), name,
                      s.port == n, s.hit_kind == HIT_ROW && s.hit == i);
    }
}

void pk_render(gfx_canvas_t *c, int buffer_index)
{
    /* A row stepped on SETUP, or the IOmcu taking or refusing a part,
     * moves an interface's pin without a touch here. */
    uint64_t iface[OUTPUTS_IFACE_COUNT];
    outputs_iface_read(iface);
    if (memcmp(iface, s.iface, sizeof(iface)) != 0) {
        memcpy(s.iface, iface, sizeof(iface));
        pk_touched();
    }
    /* And the IOmcu taking or refusing a port's pin or function. */
    if (port_view_revision() != s.view_rev) {
        s.view_rev = port_view_revision();
        pk_touched();
    }
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
        /* 640 x 180px: the title in the head face, the three lines of
         * prose in 12 x 24, 52 cells inside the panel's 8px margins. */
        const int pw = NOART_W, ph = NOART_H;
        const int px = (SCREEN_W - pw) / 2, py = (FOOT - ph) / 2;
        gfx_fill_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_EDGE);
        const char *const lines[] = {
            TR(PK_NO_ART_1), TR(PK_NO_ART_2), TR(PK_NO_ART_3),
            TR(PK_NO_ART_4),
        };
        int ty = py + 24;
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            const gfx_font_t *font = (i == 0u) ? PK_FONT_HEAD : UI_FONT_BODY;
            const gfx_color_t ink = (i == 0u) ? UI_TEXT : UI_TEXT_FAINT;
            const int w = gfx_text_width(font, lines[i], 1);
            gfx_text(c, px + (pw - w) / 2, ty, lines[i], font, ink, 1);
            ty += (i == 0u) ? 48 : 30;
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
    /* Last, so the fit check sees a string under it as painted over. */
    pk_cat_render(c);

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
    }
}
