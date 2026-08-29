/*
 * The servo bench.
 *
 * The horn is the control.  A servo is a thing whose output arm you point
 * somewhere, so pointing the drawn arm is the honest gesture for commanding
 * it -- and because the drawn arm follows the *measured* position rather than
 * the commanded one, a servo that is slow, stuck or fighting a linkage shows
 * that by lagging your finger.  A slider could not say that.
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

/* The output shaft, which everything on the left is drawn around. */
/*
 * The shaft sits on the top face of the case, which is where a servo's output
 * actually is -- drawn with the case below and clear of it, the arm appeared
 * to rotate about a point in mid air.
 */
#define SHAFT_X 300
#define SHAFT_Y ((H) / 2)          /* centred in the card, not under it */
#define ARC_R   140
#define HORN_L  106
#define BODY_W  260
#define BODY_H  88

/*
 * Screen angle from servo angle.
 *
 * The case lies along the card with its output at the right-hand end, so the
 * horn sweeps the space to the right of it and zero is the arm pointing
 * straight out.  Positive is counter-clockwise, which is the convention every
 * other angle in this trade uses; nobody has to hold a second one for this
 * screen.
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
    s.measured_deg  = us_to_deg(position_us);
    s.current_a     = current_a;
    s.have_feedback = valid;
    /*
     * The arm is drawn exactly where the servo says it is, with no easing.
     *
     * Easing towards a measurement would add the bench's own lag on top of
     * the servo's, and the two are indistinguishable once drawn -- a slow
     * servo and a slow screen look identical, and only one of them is the
     * thing under test.  The smoothing below exists for the case where
     * nothing is reporting, and only for that.
     */
    if (valid) {
        s.shown_deg = s.measured_deg;
    }
    ++s.ctrl_rev;
}

uint16_t servo_screen_commanded(void) { return deg_to_us(s.commanded_deg); }

void servo_screen_set_commanded(float deg)
{
    command(deg);
}

/* ------------------------------------------------------------------ layout */

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

/* The case, its tabs and its boss.  Drawn once per framebuffer: the only
 * thing on this card that moves is the horn. */
static void draw_body(gfx_canvas_t *c)
{
    const gfx_color_t shell = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const int bx = SHAFT_X - BODY_W + 24;
    const int by = SHAFT_Y - BODY_H / 2;

    /* Mounting tabs first, so the case overlaps them. */
    for (int i = 0; i < 2; ++i) {
        const int tx = (i == 0) ? bx - 30 : bx + BODY_W - 10;
        gfx_fill_round_rect(c, tx, by + 12, 40, 38, 6, shell);
        gfx_draw_round_rect(c, tx, by + 12, 40, 38, 6, edge);
        gfx_fill_circle(c, tx + 20, by + 31, 7, ui_theme_color(UI_C_PANEL));
        gfx_draw_circle(c, tx + 20, by + 31, 7, edge);
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

static void draw_horn(gfx_canvas_t *c, float deg, gfx_color_t col)
{
    int tx, ty;
    at(deg, HORN_L, &tx, &ty);

    gfx_thick_line(c, SHAFT_X, SHAFT_Y, tx, ty, 26, col);
    gfx_fill_circle(c, tx, ty, 13, col);
    gfx_fill_circle(c, SHAFT_X, SHAFT_Y, 25, col);

    /* Holes along the arm, which is what tells you it is a horn. */
    for (int i = 1; i <= 4; ++i) {
        int hx, hy;
        at(deg, 46 + i * 15, &hx, &hy);
        gfx_fill_circle(c, hx, hy, 5, ui_theme_color(UI_C_PANEL));
    }
    gfx_fill_circle(c, tx, ty, 5, ui_theme_color(UI_C_PANEL));

    /*
     * Two broken rings around the outermost hole: the grip.
     *
     * The dial can be touched anywhere along its sweep, but nothing on the
     * drawing said so, and an arm you are meant to take hold of should look
     * like one.  They are split along the arm's own axis and stop well short
     * of it, so the gaps sit where the arm enters and leaves -- a closed ring
     * reads as another hole, which is the one thing this must not be
     * mistaken for.
     *
     * They breathe while the output is being held.  A servo under command is
     * a servo that will move if the linkage lets it, and that is worth saying
     * on the picture rather than only in a status line.
     */
    const float breath = s.driving
                             ? 0.42f + 0.58f * (0.5f + 0.5f * sinf(s.pulse))
                             : 0.62f;
    const gfx_color_t grip =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(70.0f + 150.0f * breath));
    const gfx_color_t faint =
        gfx_lerp(col, GFX_WHITE, (uint8_t)(40.0f + 90.0f * breath));

    gfx_arc(c, tx, ty, 18, 3, deg + 34.0f, deg + 146.0f, grip);
    gfx_arc(c, tx, ty, 18, 3, deg + 214.0f, deg + 326.0f, grip);
    gfx_arc(c, tx, ty, 24, 2, deg + 44.0f, deg + 136.0f, faint);
    gfx_arc(c, tx, ty, 24, 2, deg + 224.0f, deg + 316.0f, faint);

    /* The boss and its splines. */
    gfx_fill_circle(c, SHAFT_X, SHAFT_Y, 17, ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_circle(c, SHAFT_X, SHAFT_Y, 17, gfx_lerp(col, GFX_BLACK, 60));
    gfx_fill_circle(c, SHAFT_X, SHAFT_Y, 6, col);
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
    const int grip_r = 30;

    if (s.drawn_ctrl[buf] == s.ctrl_rev) {
        /*
         * Nothing moved, so only the grip needs repainting -- and only while
         * it is breathing.  Clipped to the tip, because the alternative is
         * repainting a 488x418 card at the frame rate to animate a ring
         * thirty pixels across, which is most of this panel's bandwidth spent
         * on a decoration.
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
 * Leaving releases the output.  A screen you can no longer see the horn on
 * must not be holding it somewhere, for the same reason the motor bench
 * disarms on the way out.
 */
static void leave(void)
{
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
