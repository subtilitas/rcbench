/*
 * The M5Stack Dial's faces (shared/dial): which face a model shows, what
 * each reading says and in which colour, where its ring ends, and that a
 * draw of only what changed leaves the canvas as a full draw would.
 *
 * The failures this is about: a reading shown without its valid bit, a
 * reading in another type's colour, a ring that ends at the wrong angle
 * or moves without its full scale, a repaint that leaves a stale pixel or
 * paints over a neighbour, a value change that repaints the whole frame,
 * and the wrong face for the link's state.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>

#include "greatest.h"

#include "dial_face.h"
#include "dial_models.h"
#include "dial_text.h"
#include "gfx_damage.h"
#include "ui_theme.h"

#define N (DIAL_FACE_SIZE * DIAL_FACE_SIZE)

static gfx_color_t s_a[N];
static gfx_color_t s_b[N];
static dial_render_t s_ra;
static dial_render_t s_rb;

static gfx_color_t rgb(ui_color_id_t id)
{
    const uint32_t v = ui_theme_rgb24(UI_THEME_DARK, id);
    return GFX_RGB((v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu);
}

static void init(dial_render_t *r)
{
    CHECK(dial_render_init(r));
}

/** Render a model on a fresh renderer into s_b, in full. */
static void full(const dial_model_t *m, uint8_t lang)
{
    gfx_canvas_t c;
    dial_render_invalidate(&s_rb);
    s_rb.lang = lang;
    gfx_canvas_init(&c, s_b, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    dial_render(&s_rb, &c, &m->face, &m->view);
}

/** Render a model on the running renderer into s_a. */
static bool step(const dial_model_t *m)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_a, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    return dial_render(&s_ra, &c, &m->face, &m->view);
}

/** Pixels in which s_a and s_b differ. */
static int differ(void)
{
    int n = 0;
    for (int i = 0; i < N; ++i) {
        n += s_a[i] != s_b[i];
    }
    return n;
}

/** Step, then compare with a full draw of the same model. */
static void same(const dial_model_t *m, const char *what)
{
    step(m);
    full(m, s_ra.lang);
    const int d = differ();
    if (d != 0) {
        T_FAIL("%s: %d pixels differ from a full draw", what, d);
    }
}

/** The tables gfx_ring_build() makes, to hold the generated ones to. */
static gfx_ring_px_t s_tab[GFX_RING_CAP(DIAL_RING_R0)];
static uint32_t s_keys[GFX_RING_CAP(DIAL_RING_R0)];

TEST_CASE(the_generated_ring_tables_are_what_gfx_ring_build_makes)
{
    int at = 0;
    init(&s_ra);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        gfx_ring_t g;
        const int rad = dial_ring_radius(i);
        const int n = gfx_ring_build(&g, DIAL_FACE_C2, DIAL_FACE_C2, rad,
                                     s_tab, s_keys, GFX_RING_CAP(rad));
        CHECK(n > 0);
        CHECK_EQ(dial_ring_n[i], n);
        CHECK_EQ(s_ra.ring[i].n, n);
        CHECK_EQ(s_ra.ring[i].r, rad);
        CHECK_EQ(s_ra.ring[i].cx2, DIAL_FACE_C2);
        CHECK(s_ra.ring[i].px == &dial_ring_px[at]);
        for (int k = 0; k < n; ++k) {
            const gfx_ring_px_t *a = &dial_ring_px[at + k];
            if (a->x != s_tab[k].x || a->y != s_tab[k].y
                || a->cov != s_tab[k].cov) {
                T_FAIL("ring %d entry %d: %u,%u,%u, built %u,%u,%u", i, k,
                       a->x, a->y, a->cov, s_tab[k].x, s_tab[k].y,
                       s_tab[k].cov);
                return;
            }
        }
        at += n;
    }
}

TEST_CASE(every_fixed_model_shows_its_face)
{
    static const uint8_t want[DM_COUNT] = {
        DIAL_FACE_NAV, DIAL_FACE_VALUE, DIAL_FACE_READING, DIAL_FACE_READING,
        DIAL_FACE_READING, DIAL_FACE_LOST, DIAL_FACE_LOST, DIAL_FACE_WAITING,
        DIAL_FACE_BIND, DIAL_FACE_OTHER,
    };
    dial_model_t m;
    dial_scene_t s;
    init(&s_rb);
    for (int l = 0; l < 2; ++l) {
        for (int id = 0; id < DM_COUNT; ++id) {
            dial_model(&m, id, (uint8_t)l);
            dial_scene_build(&s, &m.face, &m.view, (uint8_t)l);
            CHECK_EQ(s.face, want[id]);
            CHECK_EQ(s.lang, l);
            full(&m, (uint8_t)l);
            int lit = 0;
            for (int i = 0; i < N; ++i) {
                lit += s_b[i] != rgb(UI_C_BG);
            }
            if (lit < 1000) {
                T_FAIL("%s draws %d pixels", k_dial_model_name[id], lit);
            }
        }
    }
}

TEST_CASE(the_link_decides_the_face_before_the_screen)
{
    dial_model_t m;
    dial_scene_t s;
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    /* Bound and connected, no packet for 500ms after one: LINK LOST, and
     * the Dial sends no EVENT, so the bench is on its way to SAFE. */
    m.view.lost = true;
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_LOST);
    CHECK(s.title_dim);
    CHECK_STR_EQ(s.title, "MOTOR & ESC");
    CHECK_STR_EQ(s.block[0].text, "LINK LOST");
    CHECK_STR_EQ(s.block[1].text, "NO TURN IS SENT");
    CHECK_STR_EQ(s.block[2].text, "THE BENCH GOES SAFE");
    CHECK_STR_EQ(s.hint[0].text, "ARM ON THE PANEL");
    dial_model(&m, DM_LINK_LOST, DK_LANG_DE);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_DE);
    CHECK_STR_EQ(s.block[0].text, "LINK VERLOREN");
    CHECK_STR_EQ(s.block[1].text, "KEINE DREHUNG GESENDET");
    CHECK_STR_EQ(s.block[2].text, "PRÜFSTAND WIRD SAFE");
    CHECK_STR_EQ(s.hint[0].text, "ARM AM PANEL");
    /* 500ms later the Dial ended the link: the panel has seen LOST. */
    dial_model(&m, DM_LINK_ENDED, DK_LANG_EN);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_LOST);
    CHECK_STR_EQ(s.block[0].text, "LINK LOST");
    CHECK_STR_EQ(s.block[1].text, "THE BENCH IS SAFE");
    CHECK_STR_EQ(s.block[2].text, "ALL OUTPUTS OFF");
    CHECK_STR_EQ(s.hint[0].text, "ARM ON THE PANEL");
    dial_model(&m, DM_LINK_ENDED, DK_LANG_DE);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_DE);
    CHECK_STR_EQ(s.block[1].text, "DER PRÜFSTAND IST SAFE");
    CHECK_STR_EQ(s.block[2].text, "ALLE AUSGÄNGE AUS");
    m.view.ready = true;
    /* No packet since the connect: waiting, and the turn moves nothing. */
    dk_face_link_reset(&m.face);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_DE);
    CHECK_EQ(s.face, DIAL_FACE_WAITING);
    CHECK_EQ(s.lang, DK_LANG_DE);
    CHECK_STR_EQ(s.block[0].text, "WARTE AUF DAS PANEL");
    CHECK_STR_EQ(s.block[1].text, "DREHEN BEWEGT NICHTS");
    CHECK_EQ(s.pill, DIAL_PILL_NONE);
    /* STATE came, no SCREEN yet: EVENTs go, so the turn's claim goes;
     * the pill says what STATE says. */
    m.view.lost = false;
    CHECK(dm_state(&m, DK_STA_ARMED));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_WAITING);
    CHECK_STR_EQ(s.block[0].text, "WAITING FOR THE PANEL");
    CHECK_STR_EQ(s.block[1].text, "");
    CHECK_EQ(s.pill, DIAL_PILL_ARMED);
    CHECK_STR_EQ(s.pill_text, "ARMED");
    CHECK(dm_state(&m, 0));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.pill, DIAL_PILL_SAFE);
    m.view.lost = true;
    /* Not bound and not connected: bind, whatever the model holds. */
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    m.view.bound = false;
    m.view.ready = false;
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_BIND);
    CHECK_STR_EQ(s.block[0].text, "DIAL-3A10");
    /* Connected and not yet bound: the panel binds at its subscribe; no
     * packet yet is waiting. */
    m.view.ready = true;
    m.view.lost = true;
    dk_face_link_reset(&m.face);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_WAITING);
}

TEST_CASE(a_reading_without_its_valid_bit_shows_two_dashes_and_no_arc)
{
    dial_model_t m;
    dial_scene_t s;
    dk_live_motor_t lm = { 0, DK_LIVE_ARMED, 0, 4200, 0, 0, 0, 0, 0, 0, 10 };
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dm_motor(&m, lm));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_STR_EQ(s.cell[0].value, "42");
    CHECK_STR_EQ(s.cell[0].unit, "%");
    for (int i = 1; i < DIAL_CELLS; ++i) {
        CHECK_STR_EQ(s.cell[i].value, "--");
        CHECK_STR_EQ(s.cell[i].unit, "");
        CHECK_EQ(s.ring[i].end, 0u);
        CHECK(s.ring[i].used);
    }
    /* A servo without its measured angle and rail, and without a pulse. */
    dk_live_servo_t ls = { 0, DK_LIVE_ARMED, 0, 0, 0, 0, 0, 0, 0, 0 };
    dial_model(&m, DM_SERVO_LIVE, DK_LANG_EN);
    CHECK(dm_servo(&m, ls));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    for (int i = 0; i < DIAL_CELLS; ++i) {
        CHECK_STR_EQ(s.cell[i].value, "--");
        CHECK_EQ(s.ring[i].end, 0u);
    }
    /* No LIVE packet at all. */
    dial_model(&m, DM_OTHER, DK_LANG_EN);
    dm_screen(&m, DK_FACE_MOTOR, DK_LANG_EN, "MOTOR & ESC");
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.face, DIAL_FACE_READING);
    for (int i = 0; i < DIAL_CELLS; ++i) {
        CHECK_STR_EQ(s.cell[i].value, "--");
    }
}

TEST_CASE(readings_are_written_with_their_units_and_their_types_colours)
{
    static const uint8_t motor_col[DIAL_CELLS] = {
        UI_C_ACCENT, UI_C_RPM, UI_C_VOLT, UI_C_CURR, UI_C_POWER, UI_C_LILAC };
    static const uint8_t servo_col[DIAL_CELLS] = {
        UI_C_ACCENT, UI_C_TEAL, UI_C_LILAC, UI_C_VOLT, UI_C_CURR, UI_C_POWER };
    static const char *const motor_txt[DIAL_CELLS][2] = {
        { "42", "%" }, { "12584", "rpm" }, { "22.4", "V" }, { "18.3", "A" },
        { "410", "W" }, { "41", "\xC2\xB0" "C" } };
    static const char *const servo_txt[DIAL_CELLS][2] = {
        { "+12.5", "\xC2\xB0" }, { "+12.3", "\xC2\xB0" }, { "1625", "us" },
        { "6.02", "V" }, { "0.41", "A" }, { "2.5", "W" } };
    dial_model_t m;
    dial_scene_t s;
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_STR_EQ(s.pill_text, "ARMED 0:42");
    for (int i = 0; i < DIAL_CELLS; ++i) {
        CHECK_STR_EQ(s.cell[i].value, motor_txt[i][0]);
        CHECK_STR_EQ(s.cell[i].unit, motor_txt[i][1]);
        CHECK_EQ(s.cell[i].color, motor_col[i]);
        CHECK_EQ(s.ring[i].color, motor_col[i]);
    }
    dial_model(&m, DM_SERVO_LIVE, DK_LANG_EN);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_STR_EQ(s.pill_text, "ARMED");
    for (int i = 0; i < DIAL_CELLS; ++i) {
        CHECK_STR_EQ(s.cell[i].value, servo_txt[i][0]);
        CHECK_STR_EQ(s.cell[i].unit, servo_txt[i][1]);
        CHECK_EQ(s.cell[i].color, servo_col[i]);
    }
    /* Rounding and signs at the edges of each form. */
    dk_live_motor_t lm = { 0, DK_LIVE_ARMED,
                           DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER, 4255, 0,
                           9995, -1234, 999, 0, 0, 3700 };
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dm_motor(&m, lm));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_STR_EQ(s.cell[0].value, "42.6");
    CHECK_STR_EQ(s.cell[2].value, "10.00");
    CHECK_STR_EQ(s.cell[3].value, "-12.3");
    CHECK_STR_EQ(s.cell[4].value, "99.9");
    CHECK_STR_EQ(s.pill_text, "ARMED 1h01");
}

/** Whether every pixel of a box is the background or a blend of it with
 *  one colour. */
static bool one_colour(gfx_rect_t b, gfx_color_t fg, int *lit)
{
    const gfx_color_t bg = rgb(UI_C_BG);
    for (int y = b.y; y < b.y + b.h; ++y) {
        for (int x = b.x; x < b.x + b.w; ++x) {
            const gfx_color_t p = s_b[y * DIAL_FACE_SIZE + x];
            bool ok = p == bg;
            for (int k = 1; !ok && k < 256; ++k) {
                ok = p == gfx_lerp(bg, fg, (uint8_t)k);
            }
            if (!ok) {
                return false;
            }
            *lit += p != bg;
        }
    }
    return true;
}

/** The value rows of every cell, drawn: each in its type's colour, at half
 *  strength while SAFE. */
static void cells_coloured(int id, bool half)
{
    dial_model_t m;
    dial_scene_t s;
    init(&s_rb);
    dial_model(&m, id, DK_LANG_EN);
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    full(&m, DK_LANG_EN);
    for (int i = 0; i < DIAL_CELLS; ++i) {
        const gfx_rect_t cell = dial_cell_rect(i);
        /* The value's line: the cell's rows from 13 below its top. */
        const gfx_rect_t row = gfx_rect_make(cell.x, cell.y + 13, cell.w,
                                             cell.h - 13);
        const uint32_t v = ui_theme_rgb24(UI_THEME_DARK,
                                          (ui_color_id_t)s.cell[i].color);
        const uint32_t g = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
        gfx_color_t fg = rgb((ui_color_id_t)s.cell[i].color);
        if (half) {
            unsigned ch[3];
            for (int k = 0; k < 3; ++k) {
                const unsigned sh = (unsigned)(16 - 8 * k);
                ch[k] = (((g >> sh) & 0xFFu) * 127u + ((v >> sh) & 0xFFu) * 128u
                         + 127u) / 255u;
            }
            fg = GFX_RGB(ch[0], ch[1], ch[2]);
        }
        int lit = 0;
        if (!one_colour(row, fg, &lit) || lit < 20) {
            T_FAIL("%s cell %d: not one colour (%d lit)",
                   k_dial_model_name[id], i, lit);
        }
    }
}

TEST_CASE(each_value_and_its_unit_take_one_colour_per_type)
{
    cells_coloured(DM_MOTOR_ARMED, false);
    cells_coloured(DM_SERVO_LIVE, false);
    cells_coloured(DM_MOTOR_SAFE, true);
}

/** The largest angle, degrees from 12 o'clock clockwise, at which ring i
 *  of the canvas s_b shows the arc's colour, from the pixels of its table
 *  covered at least 96/255: below that an arc's and a track's blend can
 *  fall on the same RGB565 value. */
static double ring_end_deg(const dial_render_t *r, int i, gfx_color_t on)
{
    const gfx_ring_t *g = &r->ring[i];
    double last = -1.0;
    for (int k = 0; k < g->n; ++k) {
        const gfx_ring_px_t *p = &g->px[k];
        if (p->cov >= 96u && s_b[p->y * DIAL_FACE_SIZE + p->x]
                                  == gfx_lerp(rgb(UI_C_BG), on, p->cov)) {
            double a = atan2((double)p->x - 119.5, 119.5 - (double)p->y)
                       * 180.0 / 3.14159265358979323846;
            a = a < 0.0 ? a + 360.0 : a;
            last = a > last ? a : last;
        }
    }
    return last;
}

TEST_CASE(a_ring_at_50_percent_of_its_full_scale_ends_at_180_degrees)
{
    dial_model_t m;
    dial_scene_t s;
    dk_live_motor_t lm = { 0, DK_LIVE_ARMED,
                           DK_MV_RPM | DK_MV_VOLT | DK_MV_CURR | DK_MV_POWER
                           | DK_MV_ESC_T,
                           5000, 7000, 15000, 4000, 6000, 60, 0, 1 };
    dk_scale_t sc = { 0, 14000, 30000, 8000, 0, 0, 0, 0, 0 };
    init(&s_rb);
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dm_scale(&m, sc));
    CHECK(dm_motor(&m, lm));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    full(&m, DK_LANG_EN);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        /* Throttle 50%, rpm 7,000 of 14,000, 15V of 30V, 40A of 80A,
         * 600W of 2,400W (25%), 60°C of 120°C. */
        const double want = i == 4 ? 90.0 : 180.0;
        CHECK_EQ(s.ring[i].end, i == 4 ? 16384u : 32768u);
        CHECK_NEAR(ring_end_deg(&s_rb, i, rgb((ui_color_id_t)s.ring[i].color)),
                   want, 1.0);
    }
    /* A servo at the center of its travel is half a ring; the pulse 1,500us
     * of 500 to 2,500us as well. */
    dk_live_servo_t ls = { 0, DK_LIVE_ARMED | DK_LIVE_MEAS, 1500, 0, 0, 0, 0,
                           0, 0, 0 };
    dial_model(&m, DM_SERVO_LIVE, DK_LANG_EN);
    CHECK(dm_servo(&m, ls));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.ring[0].end, 32768u);
    CHECK_EQ(s.ring[1].end, 32768u);
    CHECK_EQ(s.ring[2].end, 32768u);
}

TEST_CASE(a_full_scale_not_known_leaves_the_ring_at_its_track)
{
    dial_model_t m;
    dial_scene_t s;
    dk_scale_t none = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dm_scale(&m, none));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK(s.ring[0].end > 0u);      /* throttle: 100% is its own scale */
    CHECK_EQ(s.ring[1].end, 0u);
    CHECK_EQ(s.ring[2].end, 0u);
    CHECK_EQ(s.ring[3].end, 0u);
    CHECK_EQ(s.ring[4].end, 0u);
    CHECK(s.ring[5].end > 0u);      /* 120°C */
    /* No SCALE packet since the connect. */
    dial_model(&m, DM_OTHER, DK_LANG_EN);
    dm_screen(&m, DK_FACE_SERVO, DK_LANG_EN, "SERVO LIVE");
    dk_live_servo_t ls = { 0, DK_LIVE_ARMED | DK_LIVE_MEAS | DK_LIVE_RAIL,
                           1625, 125, 1230, 6020, 41, 25, 0, 0 };
    CHECK(dm_servo(&m, ls));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        CHECK_EQ(s.ring[i].end, 0u);
    }
    /* Past the full scale is the whole ring; below 0 is none. */
    dk_scale_t sc = { 0, 1000, 1000, 100, 100, 1000, 2000, 1000, 100 };
    dk_live_motor_t lm = { 0, DK_LIVE_ARMED, DK_MV_RPM | DK_MV_ESC_T, 0,
                           5000, 0, 0, 0, -20, 0, 0 };
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(dm_scale(&m, sc));
    CHECK(dm_motor(&m, lm));
    dial_scene_build(&s, &m.face, &m.view, DK_LANG_EN);
    CHECK_EQ(s.ring[1].end, GFX_RING_TURN);
    CHECK_EQ(s.ring[5].end, 0u);
    CHECK_STR_EQ(s.cell[5].value, "-20");
}

/** Walk every face's fields in one language, each step against a full
 *  draw. */
static void walk(uint8_t lang)
{
    dial_model_t m;
    /* MOTOR: every reading moves, the clock ticks, the bench goes SAFE
     * and ARMED again, a reading loses its valid bit. */
    dial_model(&m, DM_MOTOR_ARMED, lang);
    same(&m, "motor first");
    for (int k = 0; k < 40; ++k) {
        const bool few = k % 7 == 3;
        dk_live_motor_t lm = {
            0, (uint8_t)(k % 13 == 5 ? 0 : DK_LIVE_ARMED),
            (uint8_t)(few ? DK_MV_VOLT : 0x1F),
            (uint16_t)(k * 250), (uint16_t)(few ? 0 : k * 397),
            (uint16_t)(20000 + k * 91), (int16_t)(few ? 0 : k * 77 - 900),
            (uint16_t)(few ? 0 : k * 133), (int8_t)(few ? 0 : k - 10), 0,
            (uint16_t)(30 + k / 3) };
        CHECK(dm_motor(&m, lm));
        m.view.rssi = (int8_t)(-50 - k);
        same(&m, "motor step");
    }
    /* SERVO on the same renderer: a new face. */
    dial_model(&m, DM_SERVO_LIVE, lang);
    same(&m, "servo first");
    for (int k = 0; k < 30; ++k) {
        const bool meas = k % 5 != 0;
        dk_live_servo_t ls = {
            0, (uint8_t)(DK_LIVE_ARMED | (meas ? DK_LIVE_MEAS : 0)
                         | DK_LIVE_RAIL),
            (uint16_t)(1000 + 33 * k), (int16_t)(-450 + 31 * k),
            (int16_t)(meas ? -4500 + 299 * k : 0), (uint16_t)(5000 + 50 * k),
            (int16_t)(k * 7), (uint16_t)(k * 3), 0, 0 };
        CHECK(dm_servo(&m, ls));
        same(&m, "servo step");
    }
    /* A list: the focus walks, the count grows past the ticks' limit. */
    dial_model(&m, DM_NAVIGATION, lang);
    same(&m, "nav first");
    for (int k = 0; k < 9; ++k) {
        dm_nav(&m, (uint8_t)k, 9, DK_SLOT_FOCUS, k % 2 ? "MOTOR" : "SERVO");
        dm_nav(&m, (uint8_t)k, 9, DK_SLOT_PREV, "A");
        same(&m, "nav step");
    }
    for (int k = 0; k < 40; k += 3) {
        dm_nav(&m, (uint8_t)k, 40, DK_SLOT_FOCUS, "ROW");
        same(&m, "long list step");
    }
    /* A value: set, to its limit, and out of SET. */
    dial_model(&m, DM_VALUE_SET, lang);
    same(&m, "value first");
    for (int k = 0; k <= 60; k += 7) {
        dk_value_t v = { m.seq++, k, 5, (uint8_t)(DK_VAL_SET
                                                  | (k > 50 ? DK_VAL_LIMIT
                                                            : 0)) };
        uint8_t b[DK_LEN_VALUE];
        const size_t n = dk_value_encode(&v, b);
        CHECK(dm_feed(&m, b, n));
        same(&m, "value step");
    }
    /* A value whose range and step lines meet their widest numbers. */
    CHECK(dm_value(&m, "\xC2\xB0", 1, -1000, 1000, 5, 125, 0, DK_VAL_SET));
    same(&m, "value trim");
    CHECK(dm_value(&m, "ms", 3, INT32_MIN, INT32_MAX, 1, -1, INT32_MIN, 0));
    same(&m, "value int32 ends");
}

TEST_CASE(a_draw_of_what_changed_equals_a_full_draw)
{
    dial_model_t m;
    init(&s_ra);
    init(&s_rb);
    walk(DK_LANG_EN);
    walk(DK_LANG_DE);
    /* German MOTOR & ESC, ARMED: only the power reading changes.  The
     * cell's clear must leave the dots of DRÜCKEN on the hint below. */
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_DE);
    same(&m, "German motor");
    dk_live_motor_t lm = m.face.motor;
    lm.dw = (uint16_t)(lm.dw + 17u);
    CHECK(dm_motor(&m, lm));
    same(&m, "German power change");
    /* The language, the link, the bind. */
    dial_model(&m, DM_VALUE_SET, DK_LANG_DE);
    same(&m, "value German");
    m.view.lost = true;
    same(&m, "lost");
    m.view.ready = false;
    same(&m, "link ended");
    m.view.ready = true;
    dk_face_link_reset(&m.face);
    same(&m, "waiting");
    m.view.lost = false;
    CHECK(dm_state(&m, DK_STA_ARMED));
    same(&m, "waiting, STATE ARMED");
    m.view.lost = true;
    m.view.bound = false;
    m.view.ready = false;
    same(&m, "bind");
    m.view.usb = false;
    same(&m, "bind on battery");
    dial_model(&m, DM_OTHER, DK_LANG_EN);
    same(&m, "other");
    dm_state(&m, DK_STA_ARMED);
    same(&m, "other armed");
}

TEST_CASE(a_value_change_repaints_its_cell_and_its_ring_only)
{
    dial_model_t m;
    gfx_canvas_t c;
    gfx_damage_t dmg;
    gfx_rect_t box[GFX_DAMAGE_MAX_RECTS];
    init(&s_ra);
    dial_model(&m, DM_MOTOR_ARMED, DK_LANG_EN);
    CHECK(step(&m));
    CHECK(!step(&m));       /* nothing changed, nothing drawn */

    dk_live_motor_t lm = m.face.motor;
    lm.rpm = (uint16_t)(lm.rpm + 300u);
    CHECK(dm_motor(&m, lm));
    gfx_canvas_init(&c, s_a, DIAL_FACE_SIZE, DIAL_FACE_SIZE, 0);
    gfx_damage_init(&dmg, &c);
    gfx_damage_sink = &dmg;
    CHECK(step(&m));
    gfx_damage_sink = NULL;
    const int n = gfx_damage_rects(&dmg, box, GFX_DAMAGE_MAX_RECTS);
    long area = 0;
    for (int i = 0; i < n; ++i) {
        area += (long)box[i].w * box[i].h;
        /* The cell and the ring's end at about 10 o'clock: neither the
         * title tag nor the pill is sent again. */
        CHECK(!gfx_rect_contains(box[i], 120, 42));
        CHECK(!gfx_rect_contains(box[i], 120, 66));
    }
    CHECK(n >= 1);
    if (area * 10 > N) {
        T_FAIL("a speed change sends %ld of %d pixels", area, N);
    }
}

TEST_CASE(the_run_clock_reads_minutes_then_hours)
{
    char t[16];
    dial_clock_text(0, t, sizeof t);
    CHECK_STR_EQ(t, "0:00");
    dial_clock_text(59, t, sizeof t);
    CHECK_STR_EQ(t, "0:59");
    dial_clock_text(3599, t, sizeof t);
    CHECK_STR_EQ(t, "59:59");
    dial_clock_text(3600, t, sizeof t);
    CHECK_STR_EQ(t, "1h00");
    dial_clock_text(65535, t, sizeof t);
    CHECK_STR_EQ(t, "18h12");
}

TEST_CASE(the_dials_words_exist_in_both_languages_with_their_glyphs)
{
    static const gfx_font_t *const faces[] = {
        &gfx_font_8x16, &gfx_font_10x20, &gfx_font_12x24, &gfx_font_16x28,
        &gfx_font_20x36 };
    for (int i = 0; i < DIAL_TEXT_COUNT; ++i) {
        const char *en = dial_text(DK_LANG_EN, (dial_text_id_t)i);
        const char *de = dial_text(DK_LANG_DE, (dial_text_id_t)i);
        CHECK(en[0] != '\0' && de[0] != '\0');
        /* The same conversions, in the same order. */
        const char *a = en;
        const char *b = de;
        for (;;) {
            a = strchr(a, '%');
            b = strchr(b, '%');
            if (a == NULL || b == NULL) {
                break;
            }
            if (a[1] != b[1]) {
                T_FAIL("%s converts %%%c and %%%c", dial_text_name(
                           (dial_text_id_t)i), a[1], b[1]);
            }
            /* A '%' that ends the string (`PRESS: 0%`) has no conversion
             * after it: step past it alone, never past the NUL. */
            a += (a[1] != '\0') ? 2 : 1;
            b += (b[1] != '\0') ? 2 : 1;
        }
        if ((a == NULL) != (b == NULL)) {
            T_FAIL("%s: the conversions differ", dial_text_name(
                       (dial_text_id_t)i));
        }
        for (int l = 0; l < 2; ++l) {
            const char *s = l ? de : en;
            for (size_t f = 0; f < sizeof faces / sizeof faces[0]; ++f) {
                const char *p = s;
                for (uint32_t cp = gfx_utf8_next(&p); cp != 0u;
                     cp = gfx_utf8_next(&p)) {
                    bool in = cp >= faces[f]->first && cp <= faces[f]->last;
                    for (int k = 0; !in && k < faces[f]->extra_count; ++k) {
                        in = faces[f]->extra[k] == cp;
                    }
                    if (!in) {
                        T_FAIL("%s: U+%04X has no glyph",
                               dial_text_name((dial_text_id_t)i),
                               (unsigned)cp);
                    }
                }
            }
            if (strstr(s, "CENTRE") != NULL || strstr(s, "centre") != NULL) {
                T_FAIL("%s writes centre", dial_text_name((dial_text_id_t)i));
            }
        }
    }
    CHECK_STR_EQ(dial_text(DK_LANG_EN, DIAL_TEXT_COUNT), "");
    CHECK_STR_EQ(dial_text(7, DIAL_LINK_LOST), "LINK LOST");
    CHECK_STR_EQ(dial_text_name(DIAL_LINK_LOST), "DIAL_LINK_LOST");
}

int main(void)
{
    RUN(the_generated_ring_tables_are_what_gfx_ring_build_makes);
    RUN(every_fixed_model_shows_its_face);
    RUN(the_link_decides_the_face_before_the_screen);
    RUN(a_reading_without_its_valid_bit_shows_two_dashes_and_no_arc);
    RUN(readings_are_written_with_their_units_and_their_types_colours);
    RUN(each_value_and_its_unit_take_one_colour_per_type);
    RUN(a_ring_at_50_percent_of_its_full_scale_ends_at_180_degrees);
    RUN(a_full_scale_not_known_leaves_the_ring_at_its_track);
    RUN(a_draw_of_what_changed_equals_a_full_draw);
    RUN(a_value_change_repaints_its_cell_and_its_ring_only);
    RUN(the_run_clock_reads_minutes_then_hours);
    RUN(the_dials_words_exist_in_both_languages_with_their_glyphs);
    return test_summary("dial_face");
}
