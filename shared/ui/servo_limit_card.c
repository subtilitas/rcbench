/**
 * @file
 * @brief LIMIT RUN's left card and its footer hold: the mode and what the
 *        run drives and reads, LOW END and HIGH END, the state lines, the
 *        last result and its report; HOLD 2s: ARM + START, the step under
 *        way, or STOP RUN.
 *
 * The card, in the screen's coordinates, x 18 to 618:
 *
 *   - the mode line, y 16, and the output and meter line, y 40, 10 x 20;
 *   - LOW END and HIGH END, two cells of 295 x 96px from y 70: the end the
 *     search found first in us, FREE where the servo met no stop, the
 *     pulse under way while the run searches that end, "-" before;
 *   - the state lines from y 178, 24px apart, each wrapped across 600px:
 *     the step under way, the sequence's step or its failure, or why the
 *     hold is refused; the mode's warning; the last result, its values or
 *     its reason, RESTORE or the output, the report; the mode's note.
 *
 * The run itself is servo_limit_panel.c's, the READY card's rows
 * servo_limit_ready.c's.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "log_name.h"
#include "sensors_rows.h"
#include "servo_limrun.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/** @brief The card's column, px. */
#define LC_X       (PAD + 10)
#define LC_W       (LCARD_W - 20)      /**< Its width: 600px. */
#define LC_MODE_Y  (CARD_Y + 8)        /**< The mode line. */
#define LC_OUT_Y   (CARD_Y + 32)       /**< The output and meter line. */
#define LC_END_Y   (CARD_Y + 62)       /**< LOW END and HIGH END. */
#define LC_END_W   295                 /**< A cell's width, px. */
#define LC_END_H   96                  /**< Its height, px. */
#define LC_END_GAP 10                  /**< Between the two, px. */
#define LC_LINE_Y  (LC_END_Y + LC_END_H + 12) /**< The first state line. */
#define LC_LINE_P  24                  /**< Their pitch, px. */
/** @brief The state lines the card holds at most. */
#define LC_LINES   8
/** @brief The plot of what feeds the servo: 4px under eight rows of
 *         state lines, down to 4px over the card's floor (58px). */
#define LC_PLOT_Y  (LC_LINE_Y + LC_LINES * LC_LINE_P + 4)

_Static_assert(2 * LC_END_W + LC_END_GAP <= LC_W,
               "the two cells fit the column");
_Static_assert(LC_PLOT_Y + 48 <= CARD_FLOOR - 4,
               "eight state lines and a 48px plot end above the footer");

/* --------------------------------------------------------------- texts */

/** @brief The limit run's report, as the card names it. */
static void lim_file_name(char *b, size_t n)
{
    if (s.lim_off) {
        snprintf(b, n, "%s", TR(SV_USER_LOGS_OFF));
    } else if (s.lim_file > 0) {
        log_name_build(b, n, LOG_PREFIX_LIMIT, s.lim_file, true);
    } else {
        snprintf(b, n, "%s", (s.lim_file < 0) ? TR(SV_NOT_RECORDED)
                                               : TR(SV_WRITING));
    }
}

/** @brief Whether the run under way, or one the hold fires now, programs
 *         the servo: as it started, else as chosen on a PROG PORT where
 *         the run can write once the hold's steps are done. */
static bool prog_now(void)
{
    return servo_limrun_running(&s.lim)
               ? s.lim.cfg.program
               : (lim_reads() && s.lim_prog && lim_write_why_held() == NULL);
}

/** @brief Whether it sets SERVO PULSES: as it started, else as chosen for
 *         a KST or Chaservo servo on a PROG PORT where the run can
 *         write. */
static bool pulses_now(void)
{
    return servo_limrun_running(&s.lim)
               ? s.lim.cfg.set_pulses
               : (lim_reads() && !lim_hitec_now() && s.lim_pulses
                  && lim_write_why_held() == NULL);
}

/**
 * @brief The mode line: SEARCH ONLY or SEARCH + PROGRAM, with SERVO PULSES
 *        or the Hitec series on a PROG PORT.
 * @param b where it goes
 * @param n its size
 */
static void mode_line(char *b, size_t n)
{
    const bool running = servo_limrun_running(&s.lim);
    const char *mode = prog_now() ? TR(SV_LIM_PROGRAM) : TR(SV_LIM_SEARCH);
    const uint8_t fam = lim_family_now();
    const bool reads = running ? s.lim.cfg.read : lim_reads();
    if (reads && fam != SERVO_LIMRUN_FAM_KST) {
        snprintf(b, n, TR(SV_SUM_LIM_HITEC), mode,
                 ui_setting_option(SET_HITEC_SERIES,
                                   fam == SERVO_LIMRUN_FAM_HITEC_D ? 1 : 0));
    } else if (reads) {
        snprintf(b, n, TR(SV_SUM_LIM), mode,
                 pulses_now() ? TR(SV_LIM_PULSES_STD)
                              : TR(SV_LIM_PULSES_KEEP));
    } else {
        snprintf(b, n, "%s", mode);
    }
}

/**
 * @brief What the mode does, the card's last line while no run is under
 *        way.
 * @return the note
 */
static const char *mode_note(void)
{
    if (prog_now()) {
        return lim_hitec_now() ? TR(SV_LIM_PROG_NOTE_HITEC)
                               : TR(SV_LIM_PROG_NOTE);
    }
    return pulses_now() ? TR(SV_LIM_PULSES_NOTE) : TR(SV_LIM_SEARCH_NOTE);
}

/**
 * @brief Why no run is under way, as the first state line gives it: the
 *        last sequence's failure, a start the gate refused, or the first
 *        row that refuses the hold.
 * @return the text; NULL when the hold may begin
 */
static const char *idle_why(void)
{
    if (svs_seq_failure() != NULL) {
        return svs_seq_failure();
    }
    return (s.lim_note != NULL) ? s.lim_note : lim_hold_blocked(NULL);
}

/* ---------------------------------------------------------------- lines */

/** @brief The state lines, each its text and colour. */
typedef struct {
    char        t[LC_LINES][96]; /**< the texts                   */
    ui_color_id_t col[LC_LINES]; /**< their colours               */
    int         n;              /**< how many                    */
} lc_lines_t;

/** @brief The lines last built, 8 of 96 bytes, off the screen task's stack.
 *  The card is drawn and read on that task alone. */
static lc_lines_t s_lines;

/**
 * @brief One line more, where there is room.
 * @param L the lines
 * @param col its colour
 * @param t its text; nothing for NULL or ""
 */
static void add(lc_lines_t *L, ui_color_id_t col, const char *t)
{
    if (t == NULL || t[0] == '\0' || L->n >= LC_LINES) {
        return;
    }
    snprintf(L->t[L->n], sizeof(L->t[0]), "%s", t);
    L->col[L->n] = col;
    ++L->n;
}

/**
 * @brief The run under way: its title and its phase, with the end and the
 *        pulse while it searches, probes or checks.
 * @param L the lines
 */
static void running_lines(lc_lines_t *L)
{
    const servo_limrun_t *r = &s.lim;
    char b[96];
    if (r->cfg.program) {
        snprintf(b, sizeof(b), TR(SV_LIM_TITLE_PROG), r->writes,
                 (unsigned)SERVO_LIMRUN_WRITES_MAX);
    } else {
        snprintf(b, sizeof(b), "%s", TR(SV_LIM_TITLE));
    }
    add(L, UI_C_ACCENT, b);
    const char *ph = ui_servo_str(servo_limrun_phase_str(r->phase));
    if (r->phase == SERVO_LIMRUN_PH_SEARCH || r->phase == SERVO_LIMRUN_PH_PROBE
        || r->phase == SERVO_LIMRUN_PH_CHECK) {
        snprintf(b, sizeof(b), TR(SV_LIM_AT), ph,
                 r->side == SERVO_LIMRUN_LO ? TR(SV_LIM_LOW)
                                            : TR(SV_LIM_HIGH),
                 (unsigned)r->cmd_us);
    } else if (r->read_once) {
        snprintf(b, sizeof(b), TR(SV_LIM_VALUES), ph,
                 (unsigned)r->cur[SERVO_LIMRUN_LEFT],
                 (unsigned)r->cur[SERVO_LIMRUN_RIGHT]);
    } else {
        snprintf(b, sizeof(b), "%s", ph);
    }
    add(L, UI_C_TEXT, b);
}

/**
 * @brief The last run's result: its verdict, its values or its reason,
 *        RESTORE, its pulses against the bench's or its output, and its
 *        report.
 * @param L the lines
 */
static void result_lines(lc_lines_t *L)
{
    const servo_limrun_t *r = &s.lim;
    const servo_limrun_verdict_t v = r->verdict;
    char b[96];
    snprintf(b, sizeof(b), TR(SV_LIM_VERDICT),
             ui_servo_str(servo_limrun_verdict_str(v)));
    add(L, (v == SERVO_LIMRUN_PASS || v == SERVO_LIMRUN_FREE) ? UI_C_OK
           : (v == SERVO_LIMRUN_FAIL)                         ? UI_C_DANGER
           : (v == SERVO_LIMRUN_ABORTED)                      ? UI_C_WARN
                                                              : UI_C_ACCENT,
        b);
    uint16_t lo = 0u, hi = 0u;
    if (v == SERVO_LIMRUN_FAIL || v == SERVO_LIMRUN_ABORTED) {
        add(L, UI_C_TEXT, ui_servo_str(servo_limrun_why_str(r)));
    } else if (v == SERVO_LIMRUN_PASS && servo_limrun_original(r, &lo, &hi)) {
        snprintf(b, sizeof(b), TR(SV_LIM_SET), (unsigned)lo,
                 (unsigned)r->cur[SERVO_LIMRUN_LEFT], (unsigned)hi,
                 (unsigned)r->cur[SERVO_LIMRUN_RIGHT]);
        add(L, UI_C_TEXT, b);
    }
    if (servo_limrun_restore(r)) {
        /* The values last written stay in the servo; the programmer puts
         * back the ones before the run: KST's RESTORE, the Hitec
         * programmer's backup file. */
        if (r->cfg.family == SERVO_LIMRUN_FAM_KST) {
            snprintf(b, sizeof(b), TR(SV_LIM_RESTORE), TR(SVH_KST));
        } else if (r->servo.backup[0] != '\0') {
            snprintf(b, sizeof(b), TR(SV_LIM_RESTORE_HITEC), r->servo.backup);
        } else {
            snprintf(b, sizeof(b), "%s", TR(SV_LIM_NO_BACKUP));
        }
        add(L, UI_C_WARN, b);
    } else if (r->why == SERVO_LIMRUN_WHY_RANGE) {
        /* The servo's pulses and the bench's range, side by side. */
        snprintf(b, sizeof(b), TR(SV_LIM_RANGE),
                 (unsigned)(r->pulse_now[SERVO_LIMRUN_P_LOWER] / 10u),
                 (unsigned)((r->pulse_now[SERVO_LIMRUN_P_UPPER] + 9u) / 10u),
                 (unsigned)r->cfg.min_us, (unsigned)r->cfg.max_us);
        add(L, UI_C_TEXT_DIM, b);
    } else {
        /* The output the ends were found on, as the run named it at its
         * start: not the target now, which may have changed since. */
        add(L, UI_C_TEXT_DIM, r->cfg.output);
    }
    lim_file_name(b, sizeof(b));
    add(L, UI_C_TEXT_DIM, b);
}

/**
 * @brief Every state line the card shows now, in order.
 * @param L receives them
 */
static void lines_now(lc_lines_t *L)
{
    memset(L, 0, sizeof(*L));
    const bool running = servo_limrun_running(&s.lim);
    char b[96];
    if (running) {
        running_lines(L);
        return;
    }
    if (svs_seq_busy()) {
        add(L, UI_C_ACCENT, svs_seq_line(b, sizeof(b)));
    } else {
        add(L, UI_C_WARN, idle_why());
    }
    /* SEARCH+PROG or SERVO PULSES chosen and not offered: why, in the
     * warning colour. */
    if ((s.lim_prog || (s.lim_pulses && !lim_hitec_now())) && lim_reads()) {
        add(L, UI_C_WARN, lim_write_why_held());
    }
    if (s.lim.state == SERVO_LIMRUN_DONE) {
        result_lines(L);
    }
    add(L, UI_C_TEXT_FAINT, mode_note());
}

/* ------------------------------------------------------------- the ends */

/**
 * @brief End cell @p k's place.
 * @param k SERVO_LIMRUN_LO or SERVO_LIMRUN_HI
 * @return its rectangle
 */
static gfx_rect_t end_rect(int k)
{
    return (gfx_rect_t){ (int16_t)(LC_X + k * (LC_END_W + LC_END_GAP)),
                         (int16_t)LC_END_Y, LC_END_W, LC_END_H };
}

gfx_rect_t lim_ends_rect(void)
{
    const gfx_rect_t a = end_rect(0);
    return (gfx_rect_t){ a.x, a.y, 2 * LC_END_W + LC_END_GAP, LC_END_H };
}

gfx_rect_t lim_lines_rect(void)
{
    return (gfx_rect_t){ LC_X, (int16_t)(LC_LINE_Y - 2), LC_W,
                         (int16_t)(LC_LINES * LC_LINE_P + 2) };
}

gfx_rect_t lim_power_rect(void)
{
    return (gfx_rect_t){ LC_X, (int16_t)LC_PLOT_Y, LC_W,
                         (int16_t)(CARD_FLOOR - 4 - LC_PLOT_Y) };
}

/**
 * @brief End cell @p k: its name, and the end in us, FREE, the pulse
 *        under way, or "-".
 * @param c the canvas
 * @param k SERVO_LIMRUN_LO or SERVO_LIMRUN_HI
 */
static void draw_end(gfx_canvas_t *c, int k)
{
    const gfx_rect_t a = end_rect(k);
    const servo_limrun_t *r = &s.lim;
    const bool running = servo_limrun_running(r);
    gfx_fill_round_rect(c, a.x, a.y, a.w, a.h, 6,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, a.x, a.y, a.w, a.h, 6, ui_theme_color(UI_C_EDGE));
    gfx_text(c, a.x + 12, a.y + 10,
             (k == SERVO_LIMRUN_LO) ? TR(LR_LOW_END) : TR(LR_HIGH_END),
             SV_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
    /* The end the search found first: the check after a write finds the
     * servo short of it, FREE, which the verdict says. */
    const servo_limrun_side_t *e = &r->first[k];
    const bool shown = r->state != SERVO_LIMRUN_IDLE;
    const bool under_way =
        running && !e->done && r->side == (uint8_t)k
        && (r->phase == SERVO_LIMRUN_PH_SEARCH
            || r->phase == SERVO_LIMRUN_PH_PROBE
            || r->phase == SERVO_LIMRUN_PH_CHECK);
    const int vy = a.y + 40;
    if (shown && (under_way || (e->done && e->bound))) {
        char num[8];
        snprintf(num, sizeof(num), "%u",
                 (unsigned)(under_way ? r->cmd_us : e->limit_us));
        const gfx_color_t col = ui_theme_color(under_way ? UI_C_TEXT_DIM
                                                         : UI_C_TEXT);
        gfx_text(c, a.x + 12, vy, num, &gfx_font_num_32x44, col, 1);
        const int w = gfx_text_width(&gfx_font_num_32x44, num, 1);
        gfx_text(c, a.x + 12 + w + 4, vy + 22, "us", SV_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    } else if (shown && e->done) {
        gfx_text(c, a.x + 12, vy + 4, ui_servo_str(SERVO_STR_L_FREE),
                 &gfx_font_20x36, ui_theme_color(UI_C_OK), 1);
    } else {
        gfx_text(c, a.x + 12, vy + 4, "-", &gfx_font_20x36,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

/* ------------------------------------------------------------ drawing */

void lim_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    char line[96];
    mode_line(line, sizeof(line));
    gfx_text(c, LC_X, LC_MODE_Y, line, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    char out[32], meter[20];
    (void)servo_target_line(out, sizeof(out), s.drive_target, s.drive_pin,
                            s.drive_pwm, s.pins, s.pins_known);
    snprintf(line, sizeof(line), TR(SV_LIM_LINE), out,
             servo_limrun_running(&s.lim)
                 ? s.lim.cfg.meter
                 : sensors_rows_meter(meter, sizeof(meter)));
    gfx_text(c, LC_X, LC_OUT_Y, line, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    draw_end(c, SERVO_LIMRUN_LO);
    draw_end(c, SERVO_LIMRUN_HI);

    lc_lines_t *const L = &s_lines;
    lines_now(L);
    const gfx_rect_t lr = lim_lines_rect();
    const gfx_rect_t old = c->clip;
    if (gfx_clip_intersect(c, lr)) {
        int y = LC_LINE_Y;
        for (int i = 0; i < L->n; ++i) {
            y += LC_LINE_P
                 * ui_text_wrap(c, gfx_rect_make(LC_X, y, LC_W, LC_LINE_P),
                                LC_LINE_P, L->t[i], SV_FONT_LABEL,
                                ui_theme_color(L->col[i]));
        }
    }
    c->clip = old;
    /* Under the lines, what feeds the servo as every SERVO plot shows it. */
    svs_power_view(c, lim_power_rect());
}

const char *servo_screen_limit_line(int i, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    lines_now(&s_lines);
    snprintf(buf, n, "%s",
             (i >= 0 && i < s_lines.n) ? s_lines.t[i] : "");
    return buf;
}

void lim_draw_hold(gfx_canvas_t *c)
{
    const bool running = servo_limrun_running(&s.lim);
    const bool seq = svs_seq_busy();
    char label[40];
    ui_action_t now = UI_ACTION_AVAILABLE;
    if (running) {
        snprintf(label, sizeof(label), "%s", TR(SV_LIM_STOP));
        now = UI_ACTION_ACTIVE;
    } else if (seq) {
        (void)svs_seq_label(label, sizeof(label));
        now = UI_ACTION_WAITING;
    } else {
        snprintf(label, sizeof(label), "%s", TR(RA_START));
    }
    /* The fade runs from green to the amber of the sequence the hold
     * starts. */
    ui_hold_button(c, ready_hold_rect(), label, ui_action_fill(now),
                   ui_action_fill(UI_ACTION_WAITING), &s.lim_hold,
                   s.ov_have && s.ov_pressed == OP_LIM_START,
                   running || seq || lim_hold_blocked(NULL) == NULL,
                   SV_FONT_LABEL);
}
