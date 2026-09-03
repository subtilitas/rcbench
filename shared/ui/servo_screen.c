/*
 * The servo bench screen.
 *
 * The horn is the control: dragging the drawn arm commands the servo, and
 * the arm is drawn at the measured position, so a servo that is slow, stuck
 * or fighting a linkage lags the finger by that much.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_slider.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)

#define PAD    6
#define LCARD_W 488
#define RCARD_X (PAD + LCARD_W + 8)
#define RCARD_W (W - RCARD_X - PAD)

/* The output shaft, which everything on the left is drawn around.  It sits
 * on the top face of the case, where a servo's output is. */
#define SHAFT_X 300
/* Integer division on purpose: this is a pixel row, and H is even. */
/* NOLINTNEXTLINE(bugprone-integer-division) */
#define SHAFT_Y ((H) / 2)          /* centred in the card, not under it */
#define ARC_R   140
#define HORN_L  92
#define BODY_W  260
#define BODY_H  88
/*
 * The mounting flanges run the full width of the case end, as on a servo.
 *
 * The three clearances are the chosen numbers.  The flange width is what
 * holds a bore with TAB_MARGIN of material outside it and TAB_INNER between
 * it and the case, and the hole positions follow from the same margin top
 * and bottom.
 */
#define TAB_HOLE_R  12
#define TAB_MARGIN  6                  /* material outside the bore     */
#define TAB_INNER   8                  /* between the bore and the case */
#define TAB_OVER    6                  /* how far the case laps over it */
#define TAB_W       (TAB_MARGIN + 2 * TAB_HOLE_R + TAB_INNER + TAB_OVER)
#define TAB_H       BODY_H
#define TAB_HOLE_DY (BODY_H / 2 - TAB_MARGIN - TAB_HOLE_R)

/*
 * Screen angle from servo angle.
 *
 * The case lies along the card with its output at the right-hand end; zero
 * is the arm pointing straight out, and positive is counter-clockwise, the
 * convention every other angle on the bench uses.
 */
#define PHI(a) (a)

/* A standard servo, and the two the bench meets most often after it. */
typedef struct {
    const char *name;
    uint16_t    min_us, centre_us, max_us;
} servo_type_t;

static const servo_type_t k_types[] = {
    { "STANDARD",   1000, 1500, 2000 },
    { "NARROW 760", 660,  760,  860  },
    { "WIDE",       800,  1500, 2200 },
};
#define TYPE_COUNT ((int)(sizeof(k_types) / sizeof(k_types[0])))

static struct {
    int      type;
    int16_t  trim_us;      /**< added to centre                        */
    float    travel_deg;   /**< how far each way the horn is allowed   */
    int      speed_pct;    /**< how fast the bench slews the command   */

    float    commanded_deg;
    float    shown_deg;    /**< what the horn is drawn at              */
    float    measured_deg;
    float    current_a;
    bool     have_feedback;

    bool     dragging;
    int      drag_id;

    int      shown_q_deg; /**< the position as drawn, in tenths          */
    int      shown_q_a;   /**< the current as drawn, in hundredths        */
    bool     driving;     /**< the output is being held somewhere */
    float    pulse;       /**< phase of the grip's breathing      */
    int      drawn_pulse[2];

    servo_cmd_t pending;

    gfx_rect_t centre_btn, release_btn;
    gfx_rect_t trim_dn, trim_up, travel_dn, travel_up, type_btn;
    ui_slider_t speed;

    uint32_t ctrl_rev;
    uint32_t drawn_ctrl[2];
    unsigned drawn_mask;
} s;

/* ------------------------------------------------------------- conversions */

static const servo_type_t *type(void) { return &k_types[s.type]; }

static uint16_t deg_to_us(float deg)
{
    const servo_type_t *t = type();
    const float span = (float)(t->max_us - t->min_us) * 0.5f;
    float us = (float)t->centre_us + (float)s.trim_us + deg / 90.0f * span;
    if (us < (float)t->min_us) { us = (float)t->min_us; }
    if (us > (float)t->max_us) { us = (float)t->max_us; }
    return (uint16_t)(us + 0.5f);
}

static float us_to_deg(uint16_t us)
{
    const servo_type_t *t = type();
    const float span = (float)(t->max_us - t->min_us) * 0.5f;
    if (span <= 0.0f) {
        return 0.0f;
    }
    return ((float)us - (float)t->centre_us - (float)s.trim_us) * 90.0f / span;
}

static float clamp_travel(float deg)
{
    if (deg < -s.travel_deg) { return -s.travel_deg; }
    if (deg >  s.travel_deg) { return  s.travel_deg; }
    return deg;
}

static void post(servo_cmd_kind_t kind, uint16_t us)
{
    s.pending.kind     = kind;
    s.pending.value_us = us;
    /* The grip only breathes while something is actually being held, so this
     * has to follow the command rather than the screen being open. */
    s.driving = (kind != SERVO_CMD_RELEASE);
}

static void command(float deg)
{
    s.commanded_deg = clamp_travel(deg);
    post(SERVO_CMD_POSITION, deg_to_us(s.commanded_deg));
    ++s.ctrl_rev;
}

bool servo_screen_take(servo_cmd_t *out)
{
    if (out == NULL || s.pending.kind == SERVO_CMD_NONE) {
        return false;
    }
    *out = s.pending;
    s.pending.kind = SERVO_CMD_NONE;
    return true;
}

void servo_screen_feedback(uint16_t position_us, float current_a, bool valid)
{
    const float deg = us_to_deg(position_us);

    /*
     * A reading counts as new only if it is drawn differently: compare at
     * the precision shown, tenths of a degree and hundredths of an amp.
     * Feedback arrives at the poll rate whether or not the servo moved, and
     * a revision bump per reading would repaint the 488x418 card at that
     * rate and never reach the grip's clipped repaint.
     */
    const int q_deg = (int)(deg * 10.0f + (deg >= 0.0f ? 0.5f : -0.5f));
    const int q_a   = (int)(current_a * 100.0f + 0.5f);
    const bool same = valid && s.have_feedback
                      && q_deg == s.shown_q_deg && q_a == s.shown_q_a;

    s.measured_deg  = deg;
    s.current_a     = current_a;
    s.have_feedback = valid;
    /*
     * The arm is drawn where the servo reports it, with no easing: easing
     * would add the screen's lag to the servo's, and the two are
     * indistinguishable when drawn.  The smoothing in tick() applies only
     * while nothing is reporting.
     */
    if (valid) {
        s.shown_deg = deg;
    }
    if (!same) {
        s.shown_q_deg = q_deg;
        s.shown_q_a   = q_a;
        ++s.ctrl_rev;
    }
}

uint16_t servo_screen_commanded(void) { return deg_to_us(s.commanded_deg); }

void servo_screen_set_commanded(float deg)
{
    command(deg);
}

/* ------------------------------------------------------------------ layout */

void servo_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn_ctrl[0] = UINT32_MAX;
    s.drawn_ctrl[1] = UINT32_MAX;
    /* No step the arm can be drawn at, so the next frame draws it. */
    s.drawn_pulse[0] = -1;
    s.drawn_pulse[1] = -1;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn_ctrl[0] = UINT32_MAX;
    s.drawn_ctrl[1] = UINT32_MAX;
    s.travel_deg    = 90.0f;
    s.speed_pct     = 100;

    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;
    s.trim_dn   = (gfx_rect_t){ (int16_t)(x + w - 96), 156, 30, 26 };
    s.trim_up   = (gfx_rect_t){ (int16_t)(x + w - 30), 156, 30, 26 };
    s.travel_dn = (gfx_rect_t){ (int16_t)(x + w - 96), 194, 30, 26 };
    s.travel_up = (gfx_rect_t){ (int16_t)(x + w - 30), 194, 30, 26 };
    s.type_btn  = (gfx_rect_t){ (int16_t)(x + w - 150), 232, 150, 26 };

    ui_slider_init(&s.speed, (gfx_rect_t){ (int16_t)x, 296, (int16_t)w, 22 },
                   10.0f, 100.0f, 0);
    s.speed.value = 100.0f;
    ui_slider_set_ticks(&s.speed, 0);

    s.centre_btn  = (gfx_rect_t){ (int16_t)x, 350, (int16_t)(w / 2 - 5), 32 };
    s.release_btn = (gfx_rect_t){ (int16_t)(x + w / 2 + 5), 350,
                                  (int16_t)(w / 2 - 5), 32 };
}

/* ------------------------------------------------------------------ events */

/* Whether a touch is close enough to the horn's sweep to mean the horn. */
static bool on_the_dial(int px, int py, float *deg)
{
    const float dx = (float)(px - SHAFT_X);
    const float dy = (float)(SHAFT_Y - py);
    const float r  = sqrtf(dx * dx + dy * dy);
    /* Generous inside, bounded outside: a finger landing short of the horn
     * still means the horn, and one landing well past the arc means the
     * card behind it. */
    if (r < 30.0f || r > (float)ARC_R + 34.0f) {
        return false;
    }
    if (dx < -30.0f) {
        return false;      /* left of the shaft is the case, not the sweep */
    }
    const float phi = atan2f(dy, dx) * 180.0f / 3.14159265358979f;
    if (phi < -104.0f || phi > 104.0f) {
        return false;
    }
    *deg = phi;
    return true;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;

    if (evt->type == TOUCH_EVENT_DOWN) {
        float deg;
        if (on_the_dial(px, py, &deg)) {
            s.dragging = true;
            s.drag_id  = evt->point.id;
            command(deg);
            return;
        }
        if (gfx_rect_contains(s.trim_dn, px, py))   { s.trim_us -= 5; ++s.ctrl_rev; }
        else if (gfx_rect_contains(s.trim_up, px, py)) { s.trim_us += 5; ++s.ctrl_rev; }
        else if (gfx_rect_contains(s.travel_dn, px, py)) {
            s.travel_deg -= 5.0f;
            if (s.travel_deg < 10.0f) { s.travel_deg = 10.0f; }
            s.commanded_deg = clamp_travel(s.commanded_deg);
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.travel_up, px, py)) {
            s.travel_deg += 5.0f;
            if (s.travel_deg > 90.0f) { s.travel_deg = 90.0f; }
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.type_btn, px, py)) {
            s.type = (s.type + 1) % TYPE_COUNT;
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.centre_btn, px, py)) {
            s.commanded_deg = 0.0f;
            post(SERVO_CMD_CENTRE, deg_to_us(0.0f));
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.release_btn, px, py)) {
            post(SERVO_CMD_RELEASE, 0);
            ++s.ctrl_rev;
        }
    }

    if (s.dragging && evt->point.id == s.drag_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            float deg;
            if (on_the_dial(px, py, &deg)) {
                command(deg);
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            s.dragging = false;
            return;
        }
    }

    if (ui_slider_event(&s.speed, evt)) {
        s.speed_pct = (int)(s.speed.value + 0.5f);
        ++s.ctrl_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static void at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float phi = PHI(deg) * k;
    *x = SHAFT_X + (int)((float)r * cosf(phi) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(phi) + 0.5f);
}

/* The case, its tabs and its boss.  Drawn per framebuffer, not per frame:
 * thing on this card that moves is the horn. */
static void draw_body(gfx_canvas_t *c)
{
    const gfx_color_t shell = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const int bx = SHAFT_X - BODY_W + 24;
    const int by = SHAFT_Y - BODY_H / 2;

    /* Mounting tabs first, so the case overlaps them. */
    for (int i = 0; i < 2; ++i) {
        /* Two holes, and the flange centred on the case: a servo's mounting
         * lugs are symmetric about its centreline. */
        const int tx = (i == 0) ? bx - TAB_W + TAB_OVER
                                : bx + BODY_W - TAB_OVER;
        const int ty = by;
        gfx_fill_round_rect(c, tx, ty, TAB_W, TAB_H, 5, shell);
        gfx_draw_round_rect(c, tx, ty, TAB_W, TAB_H, 5, edge);
        /*
         * Open slots, not drilled holes: a servo's lugs are cut through to
         * the outer edge so it drops into a mount whose screws are already
         * in.
         */
        /* Set in from the flange's own outer edge, so the bore keeps its
         * distance from the case whichever end it is on. */
        const int cxh   = (i == 0) ? tx + TAB_MARGIN + TAB_HOLE_R
                                   : tx + TAB_W - TAB_MARGIN - TAB_HOLE_R;
        const int mouth = (i == 0) ? tx : tx + TAB_W;   /* the open edge */
        /*
         * The bore is darker than both the lug and the card behind it: at
         * these two greys a hole showing the card reads as a smudge, and a
         * recess reads as a hole.
         */
        const gfx_color_t bore = ui_theme_color(UI_C_PANEL_SUNK);
        for (int h = 0; h < 2; ++h) {
            const int hy = by + BODY_H / 2
                           + ((h == 0) ? -TAB_HOLE_DY : TAB_HOLE_DY);
            for (int pass = 0; pass < 2; ++pass) {
                const gfx_color_t ink = (pass == 0) ? edge : bore;
                const int r  = TAB_HOLE_R - pass;
                /* The throat is a fraction of the bore, not a fixed
                 * inset off it: subtracting a couple of pixels from a large
                 * hole leaves a slot with no waist at all. */
                const int nk = TAB_HOLE_R * 3 / 5 - pass;
                gfx_fill_circle_aa(c, cxh, hy, r, ink);
                /* Both passes run flush to the lug's edge, so the mouth is
                 * open rather than capped by its own outline. */
                const int x0 = (mouth < cxh) ? mouth : cxh;
                const int x1 = (mouth < cxh) ? cxh : mouth;
                gfx_fill_rect(c, x0, hy - nk, x1 - x0, 2 * nk, ink);
            }
        }
    }

    gfx_fill_round_rect(c, bx, by, BODY_W, BODY_H, 8, shell);
    gfx_draw_round_rect(c, bx, by, BODY_W, BODY_H, 8, edge);
    gfx_hline(c, bx + 8, by + 1, BODY_W - 16, gfx_lerp(shell, GFX_WHITE, 34));
    /* The band across the case, which is the one place a servo has colour. */
    gfx_fill_rect(c, bx + 26, by + 1, 24, BODY_H - 2,
                  ui_theme_color(UI_C_ACCENT));
    /* The label. */
    gfx_fill_round_rect(c, bx + 74, by + 20, 92, 46, 4,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, bx + 74, by + 20, 92, 46, 4, edge);
    /* The lead, leaving the case at the end away from the output. */
    for (int i = 0; i < 3; ++i) {
        gfx_hline(c, bx - 62, by + 32 + i * 8, 34,
                  ui_theme_color(UI_C_TEXT_FAINT));
    }
}

/*
 * Where a ring of radius @p r has to stop so that a constant band of dark
 * card shows between its end and the arm.
 *
 * A fixed angle does not do it: the arm is the same width at every radius,
 * so it subtends less angle further out, and two rings sharing one angular
 * gap leave the outer one further clear of the arm than the inner.  The
 * outer ring is therefore the longer of the two.
 */
static float ring_gap_deg(float r, float half_w, float dark_px)
{
    const float k = 180.0f / 3.14159265358979f;
    float sine = half_w / r;
    if (sine > 0.99f) {
        sine = 0.99f;
    }
    return (asinf(sine) + dark_px / r) * k;
}

static void draw_horn(gfx_canvas_t *c, float deg, gfx_color_t col)
{
    int tx, ty;
    at(deg, HORN_L, &tx, &ty);

    /*
     * The unlit rings are drawn first and whole, so the arm is drawn over
     * them and they pass behind it.
     */
    const gfx_color_t ghost =
        gfx_lerp(ui_theme_color(UI_C_PANEL), col, 46);
    gfx_arc(c, tx, ty, 25, 3, 0.0f, 360.0f, ghost);
    gfx_arc(c, tx, ty, 33, 2, 0.0f, 360.0f, ghost);

    gfx_capsule_aa(c, SHAFT_X, SHAFT_Y, tx, ty, 26, col);
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 25, col);

    /* Holes along the arm, spaced from the tip inwards so the outermost stays
     * on the arm whatever HORN_L is; a hole drawn past the tip reads as a
     * bite out of the card. */
    for (int i = 1; i <= 3; ++i) {
        int hx, hy;
        at(deg, HORN_L - 12 - (3 - i) * 14, &hx, &hy);
        gfx_fill_circle_aa(c, hx, hy, 5, ui_theme_color(UI_C_PANEL));
    }
    /* And the outermost one, which is the hole the ring is pointing at. */
    gfx_fill_circle_aa(c, tx, ty, 5, ui_theme_color(UI_C_PANEL));

    /*
     * The lit part of each ring, over the top.  It is split on the side the
     * arm comes in from, with a constant 9 px band of card either side of
     * the metal (ring_gap_deg), and each end fades into the unlit ring
     * beneath.
     *
     * The rings breathe while the output is being held: the on-picture sign
     * that the servo is under command.
     */
    const float breath = s.driving
                             ? 0.42f + 0.58f * (0.5f + 0.5f * sinf(s.pulse))
                             : 0.62f;
    const gfx_color_t grip =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(150.0f + 100.0f * breath));
    const gfx_color_t faint =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(90.0f + 90.0f * breath));

    const float half_w = 13.0f;   /* the arm's half width, as drawn */
    const float dark   = 9.0f;    /* the band of card between ring and arm */
    const float g_in   = ring_gap_deg(25.0f, half_w, dark);
    const float g_out  = ring_gap_deg(33.0f, half_w, dark);
    gfx_arc_fade(c, tx, ty, 25, 3, deg + 180.0f + g_in,
                 deg + 540.0f - g_in, grip, ghost, 78.0f);
    gfx_arc_fade(c, tx, ty, 33, 2, deg + 180.0f + g_out,
                 deg + 540.0f - g_out, faint, ghost, 78.0f);

    /* The boss and its splines. */
    /* An outline is two discs, not a circle walked round: an arc closed on
     * itself lays some pixels twice and misses others, and at two pixels wide
     * that stipples. */
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 17, gfx_lerp(col, GFX_BLACK, 60));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 15,
                       ui_theme_color(UI_C_PANEL_SUNK));
    gfx_fill_circle_aa(c, SHAFT_X, SHAFT_Y, 6, col);
}

static void draw_dial(gfx_canvas_t *c)
{
    const gfx_color_t dim = ui_theme_color(UI_C_TEXT_FAINT);

    /* The travel the settings allow, over the travel the servo has. */
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 2, PHI(90.0f), PHI(-90.0f),
            ui_theme_color(UI_C_GRID));
    gfx_arc(c, SHAFT_X, SHAFT_Y, ARC_R, 4, PHI(s.travel_deg),
            PHI(-s.travel_deg), ui_theme_color(UI_C_EDGE_HI));

    for (int i = -2; i <= 2; ++i) {
        const float a = (float)i * 45.0f;
        int x0, y0, x1, y1;
        at(a, ARC_R - 10, &x0, &y0);
        at(a, ARC_R + 8, &x1, &y1);
        gfx_thick_line(c, x0, y0, x1, y1, (i == 0) ? 3 : 2,
                       (i == 0) ? ui_theme_color(UI_C_TEXT_DIM) : dim);

        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%+d", (int)a);
        int lx, ly;
        at(a, ARC_R + 26, &lx, &ly);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 24), (int16_t)(ly - 8),
                                     48, 16 },
                    lbl, UI_FONT_LABEL, dim, 1, GFX_ALIGN_CENTER);
    }
}

static void draw_left(gfx_canvas_t *c)
{
    /* Everything that moves lives in this rectangle, so this is what gets
     * cleared -- the case and the dial below it are chrome. */
    gfx_fill_rect(c, PAD + 1, PAD + 1, LCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_dial(c);
    /* The case first: the horn bolts to the top of it and must be drawn over
     * it, not under. */
    draw_body(c);

    /* The commanded position behind the measured one, so a lag is visible as
     * two arms rather than as a number that disagrees with a picture. */
    if (fabsf(s.shown_deg - s.commanded_deg) > 1.0f) {
        draw_horn(c, s.commanded_deg,
                  gfx_lerp(ui_theme_color(UI_C_PANEL),
                           ui_theme_color(UI_C_ACCENT), 70));
    }
    draw_horn(c, s.shown_deg, ui_theme_color(UI_C_ACCENT));

    char deg[16];
    snprintf(deg, sizeof(deg), "%+.1f", (double)s.shown_deg);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int dw = gfx_seg_width(deg, &seg);
    gfx_seg_text(c, PAD + 46, H - 84, deg, &seg,
                 ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_PANEL),
                          ui_theme_color(UI_C_ACCENT), 30));
    gfx_text(c, PAD + 52 + dw, H - 70, "DEG",
             UI_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
}

static void row(gfx_canvas_t *c, int y, const char *label, const char *value)
{
    const int x = RCARD_X + 12;
    gfx_text(c, x, y + 5, label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    if (value != NULL) {
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 80), (int16_t)(y + 5),
                                     (int16_t)(RCARD_W - 24 - 80), 16 },
                    value, UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }
}

static void draw_right(gfx_canvas_t *c)
{
    const int x = RCARD_X + 12;
    const int w = RCARD_W - 24;
    gfx_fill_rect(c, RCARD_X + 1, PAD + 1, RCARD_W - 2, H - 2 * PAD - 2,
                  ui_theme_color(UI_C_PANEL));

    gfx_text(c, x, 18, "CONFIGURATION", UI_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    char buf[24];
    snprintf(buf, sizeof(buf), "%u us", (unsigned)deg_to_us(s.commanded_deg));
    row(c, 44, "COMMANDED", buf);
    if (s.have_feedback) {
        snprintf(buf, sizeof(buf), "%+.1f deg", (double)s.measured_deg);
        row(c, 68, "MEASURED", buf);
        snprintf(buf, sizeof(buf), "%.2f A", (double)s.current_a);
        row(c, 92, "CURRENT", buf);
    } else {
        row(c, 68, "MEASURED", "---");
        row(c, 92, "CURRENT", "---");
    }

    gfx_hline(c, x, 124, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, 132, "SETTINGS", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    snprintf(buf, sizeof(buf), "%+d us", (int)s.trim_us);
    row(c, 156, "CENTRE", NULL);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 90), 161, 72, 16 }, buf,
                UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_RIGHT);
    ui_button(c, s.trim_dn, "-", ui_theme_color(UI_C_PANEL_HI), false, true);
    ui_button(c, s.trim_up, "+", ui_theme_color(UI_C_PANEL_HI), false, true);

    snprintf(buf, sizeof(buf), "+/-%d deg", (int)s.travel_deg);
    row(c, 194, "TRAVEL", NULL);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + 90), 199, 72, 16 }, buf,
                UI_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_RIGHT);
    ui_button(c, s.travel_dn, "-", ui_theme_color(UI_C_PANEL_HI), false, true);
    ui_button(c, s.travel_up, "+", ui_theme_color(UI_C_PANEL_HI), false, true);

    row(c, 232, "TYPE", NULL);
    ui_button(c, s.type_btn, type()->name, ui_theme_color(UI_C_PANEL_HI),
              false, true);

    snprintf(buf, sizeof(buf), "%d %%", s.speed_pct);
    row(c, 264, "SPEED", buf);
    s.speed.color = ui_theme_color(UI_C_ACCENT);
    ui_slider_render(&s.speed, c);

    snprintf(buf, sizeof(buf), "%u - %u us",
             (unsigned)type()->min_us, (unsigned)type()->max_us);
    row(c, 322, "RANGE", buf);

    ui_button(c, s.centre_btn, "CENTRE", ui_theme_color(UI_C_ACCENT),
              false, true);
    ui_button(c, s.release_btn, "RELEASE", ui_theme_color(UI_C_PANEL_HI),
              false, true);
}

static void tick(float dt_s)
{
    if (s.driving) {
        s.pulse += dt_s * 3.6f;         /* a little under two seconds a cycle */
        if (s.pulse > 6.28318f) {
            s.pulse -= 6.28318f;
        }
    }

    /*
     * With nothing reporting, the arm still must not arrive before a servo
     * could: it chases the command at the configured speed instead of
     * snapping to it, which is the only thing standing in for travel time
     * when there is no hardware to measure.  With feedback this does nothing,
     * because the measurement has already placed the arm.
     */
    if (s.have_feedback) {
        return;
    }
    const float full = 360.0f;   /* degrees per second at 100% */
    const float step = full * (float)s.speed_pct / 100.0f * dt_s;
    const float d = s.commanded_deg - s.shown_deg;

    if (fabsf(d) <= step) {
        if (s.shown_deg != s.commanded_deg) {
            s.shown_deg = s.commanded_deg;
            ++s.ctrl_rev;
        }
        return;
    }
    s.shown_deg += (d > 0.0f) ? step : -step;
    ++s.ctrl_rev;
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_card(c, (gfx_rect_t){ PAD, PAD, LCARD_W, (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        ui_card(c, (gfx_rect_t){ RCARD_X, PAD, RCARD_W,
                                 (int16_t)(H - 2 * PAD) },
                ui_theme_color(UI_C_PANEL));
        s.drawn_mask |= bit;
    }

    /* How far the grip reaches past the arm's tip, and so how much of the
     * card a breath alone has to repaint. */
    const int grip_r = 36;

    if (s.drawn_ctrl[buf] == s.ctrl_rev) {
        /*
         * Nothing moved, so only the grip is repainted, and only while it
         * breathes.  Clipped to the tip: repainting the 488x418 card at the
         * frame rate to animate a ring of 36 px radius would cost most of
         * the panel's bandwidth.
         */
        const int step = (int)(s.pulse * 8.0f);
        if (!s.driving || s.drawn_pulse[buf] == step) {
            return;
        }
        s.drawn_pulse[buf] = step;

        int tx, ty;
        at(s.shown_deg, HORN_L, &tx, &ty);
        const gfx_rect_t box = { (int16_t)(tx - grip_r), (int16_t)(ty - grip_r),
                                 (int16_t)(grip_r * 2), (int16_t)(grip_r * 2) };
        gfx_rect_t old_clip = c->clip;
        if (gfx_clip_set(c, box)) {
            draw_left(c);
        }
        c->clip = old_clip;
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_pulse[buf] = (int)(s.pulse * 8.0f);

    draw_left(c);
    draw_right(c);
}

/*
 * Leaving releases the output: a screen that is not visible must not hold
 * the servo somewhere, the same rule as the motor bench's disarm on leave.
 */
static void leave(void)
{
    /* No release arrives for a finger on the speed slider as the screen
     * changes, and a latched drag outlives the gesture. */
    ui_slider_release(&s.speed);
    post(SERVO_CMD_RELEASE, 0);
}

static const ui_screen_t k_screen = {
    .title  = "SERVO",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .render = render,
};

const ui_screen_t *servo_screen(void) { return &k_screen; }
