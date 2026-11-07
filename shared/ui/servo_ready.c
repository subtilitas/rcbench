/**
 * @file
 * @brief A SERVO function screen's READY card and its ARM + START
 *        sequence.  See servo_ready_int.h for the layout.
 *
 * The card draws the rows the function gives (RUDDER AUTO's:
 * servo_rudder_auto_ready.c) and takes their taps.  The sequence is
 * servo_start_seq.h's steps done on the bench the way a finger does them:
 * PORT PWM on as OUTPUTS' segment sets it, PROG 5V on as its box's hold
 * asks it, the PD mini's output on as SUPPLY's OUTPUT ON, ARM as ARM's
 * hold.  A failure asks DISARM and every supply off and names the step
 * in the alert band; a leave ends the sequence and leaves the outputs to
 * the leave, which keeps the ports' 5V for another SERVO screen.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "sensors_rows.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The card's inner column. */
#define RDY_X (RCARD_X + 12)
#define RDY_W (RCARD_W - 24) /**< Its width: 356px. */
/** @brief The dot's radius, px. */
#define DOT_R  5
#define DOT_DX 5 /**< The centre's offset, px. */

_Static_assert(READY_ROW_Y + READY_ROWS_MAX * READY_ROW_H <= READY_RAIL_Y,
               "the rows end above the rail line");
_Static_assert(READY_RAIL_Y + 20 + 8 <= CARD_FLOOR,
               "the rail line ends inside the card");
_Static_assert(18 + READY_LABEL_W + READY_VALUE_W <= RDY_W,
               "the dot, the label and the value fit the column");

/* ------------------------------------------------------------ the card */

void ready_init(void)
{
    memset(&s.rdy, 0, sizeof(s.rdy));
    servo_seq_init(&s.rdy.seq);
    s.rdy.pressed = -1;
}

bool ready_shown(void)
{
    return s.fn == SERVO_FN_RAUTO;
}

int ready_count(void)
{
    return ready_shown() ? (int)RAR_COUNT : 0;
}

bool ready_row(int i, rdy_row_t *r)
{
    if (!ready_shown()) {
        return false;
    }
    return rauto_ready_row(i, r);
}

const char *ready_refusal(char *buf, size_t n)
{
    if (buf == NULL || n == 0u || !ready_shown()) {
        return NULL;
    }
    rauto_row_t row = RAR_COUNT;
    const char *why = rauto_hold_blocked(&row);
    if (why == NULL) {
        return NULL;
    }
    rdy_row_t r;
    const char *label = (rauto_ready_row((int)row, &r)) ? r.label : "";
    snprintf(buf, n, TR(RDY_BAND), label, why);
    return buf;
}

gfx_rect_t ready_card_rect(void)
{
    return (gfx_rect_t){ RCARD_X, RCARD_Y, RCARD_W, RCARD_H };
}

gfx_rect_t ready_row_rect(int i)
{
    if (i < 0 || i >= READY_ROWS_MAX) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ RCARD_X + 4,
                         (int16_t)(READY_ROW_Y + i * READY_ROW_H),
                         RCARD_W - 8, READY_ROW_H };
}

gfx_rect_t ready_rail_rect(void)
{
    return (gfx_rect_t){ RDY_X, READY_RAIL_Y - 4, RDY_W, 28 };
}

/**
 * @brief The colour of a row's dot.
 * @param st the row's rdy_state_t
 * @return the colour
 */
static gfx_color_t dot_color(uint8_t st)
{
    switch ((rdy_state_t)st) {
    case RDY_OK:   return ui_theme_color(UI_C_OK);
    case RDY_LIVE:
    case RDY_BAD:  return ui_theme_color(UI_C_DANGER);
    case RDY_SEQ:  return ui_theme_color(UI_C_WARN);
    default:       return ui_theme_color(UI_C_TEXT_FAINT);
    }
}

/**
 * @brief The colour of a row's value.
 * @param st the row's rdy_state_t
 * @return the colour
 */
static gfx_color_t value_color(uint8_t st)
{
    switch ((rdy_state_t)st) {
    case RDY_SEQ:  return ui_theme_color(UI_C_WARN);
    case RDY_BAD:  return ui_theme_color(UI_C_DANGER);
    case RDY_IDLE: return ui_theme_color(UI_C_TEXT_FAINT);
    default:       return ui_theme_color(UI_C_TEXT);
    }
}

/**
 * @brief Row @p i drawn: its ground, dot, label and value, the value cut
 *        to its 20 cells.
 * @param c the canvas
 * @param i the row
 */
static void draw_row(gfx_canvas_t *c, int i)
{
    rdy_row_t r;
    if (!ready_row(i, &r)) {
        return;
    }
    const gfx_rect_t a = ready_row_rect(i);
    gfx_fill_rect(c, a.x, a.y, a.w, a.h,
                  ui_theme_color(s.rdy.pressed == i ? UI_C_PANEL_HI
                                                    : UI_C_PANEL));
    const int ty = a.y + (READY_ROW_H - 20) / 2;
    gfx_fill_circle(c, RDY_X + DOT_DX, a.y + READY_ROW_H / 2, DOT_R,
                    dot_color(r.st));
    gfx_text(c, RDY_X + 18, ty, r.label, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    /* A text past the column is cut after its last whole word that
     * fits; a tap on the row shows it whole. */
    char v[64];
    const int cells = READY_VALUE_W / SV_CELL;
    size_t cut = gfx_text_prefix(r.value, cells);
    if (r.value[cut] != '\0') {
        size_t sp = cut;
        while (sp > 0u && r.value[sp] != ' ') {
            --sp;
        }
        cut = (sp > 0u) ? sp : cut;
    }
    snprintf(v, sizeof(v), "%.*s", (int)cut, r.value);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(RDY_X + RDY_W - READY_VALUE_W),
                                 (int16_t)ty, READY_VALUE_W, 20 },
                v, SV_FONT_LABEL, value_color(r.st), 1, GFX_ALIGN_RIGHT);
}

void ready_render_rail(gfx_canvas_t *c)
{
    const gfx_rect_t a = ready_rail_rect();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    char name[24], line[48];
    (void)sensors_rows_short(name, sizeof(name));
    const rail_t m = svs_rail_now();
    char v[12], i[12];
    if (m.v_ok) {
        snprintf(v, sizeof(v), "%.2fV", (double)m.v);
    } else {
        snprintf(v, sizeof(v), "--V");
    }
    if (m.i_ok) {
        snprintf(i, sizeof(i), "%.3fA", (double)m.i);
    } else {
        snprintf(i, sizeof(i), "--A");
    }
    snprintf(line, sizeof(line), "%s", (s.source == SERVO_SOURCE_INA3221)
                                           ? name : "PD MINI");
    gfx_text(c, RDY_X, READY_RAIL_Y, line, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    snprintf(line, sizeof(line), "%s  %s", v, i);
    gfx_text_in(c, (gfx_rect_t){ RDY_X, READY_RAIL_Y, RDY_W, 20 }, line,
                SV_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

void ready_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = ready_card_rect();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(c, RDY_X, READY_TITLE_Y, TR(RDY_TITLE), SV_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < ready_count(); ++i) {
        draw_row(c, i);
    }
    ready_render_rail(c);
}

uint32_t ready_sig(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < ready_count(); ++i) {
        rdy_row_t r;
        if (!ready_row(i, &r)) {
            continue;
        }
        h = (h ^ r.st) * 16777619u;
        for (const char *p = r.value; *p != '\0'; ++p) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        for (const char *p = r.label; *p != '\0'; ++p) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
    }
    h = (h ^ (uint32_t)(s.rdy.pressed + 1)) * 16777619u;
    return h;
}

bool ready_down(const touch_event_t *evt)
{
    if (evt == NULL || !ready_shown()
        || !gfx_rect_contains(ready_card_rect(), evt->point.x,
                              evt->point.y)) {
        return false;
    }
    /* A row's fix opens a list or leaves the screen: none under a hold's
     * finger, whose release it would take. */
    if (s.rdy.pressed >= 0 || svs_hold_down()) {
        return true;
    }
    for (int i = 0; i < ready_count(); ++i) {
        if (gfx_rect_contains(ready_row_rect(i), evt->point.x,
                              evt->point.y)) {
            s.rdy.pressed  = i;
            s.rdy.press_id = evt->point.id;
            ++s.ctrl_rev;
            break;
        }
    }
    return true;
}

bool ready_rest(const touch_event_t *evt)
{
    if (evt == NULL || s.rdy.pressed < 0
        || evt->point.id != s.rdy.press_id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    const int i = s.rdy.pressed;
    s.rdy.pressed = -1;
    ++s.ctrl_rev;
    if (touch_event_is_tap_up(evt) && ready_shown()
        && gfx_rect_contains(ready_row_rect(i), evt->point.x,
                             evt->point.y)) {
        rauto_ready_tap(i);
    }
    return true;
}

void ready_cancel(void)
{
    if (s.rdy.pressed >= 0) {
        s.rdy.pressed = -1;
        ++s.ctrl_rev;
    }
}

/* -------------------------------------------------------- the sequence */

servo_seq_need_t svs_seq_need(void)
{
    servo_seq_need_t n;
    memset(&n, 0, sizeof(n));
    const int target = settings_get_int(SET_SERVO_TARGET);
    n.port_pwm = target >= 1 && target <= (int)LINK_PG_PORTS_MAX;
    n.prog_5v  = prog_choice_carrier() && prog_choice_port() != 0u
                 && prog5v_box_shown();
    n.pd_mini  = lim_pd_feeds();
    /* A supply switched on: the INA3221's window under way still holds
     * the rail as it was, so the checks wait for one begun after it. */
    n.settle   = n.prog_5v || n.pd_mini;
    return n;
}

/**
 * @brief The part a refusal of the function shown names: the supply for a
 *        supply row's text, PORT PWM for its row's, ARM for ARM's.
 * @param why the refusal, or NULL
 * @return the part; SERVO_SEQ_PART_NONE for the step's own
 */
static servo_seq_part_t part_of(const char *why)
{
    if (why == NULL || s.fn != SERVO_FN_RAUTO) {
        return SERVO_SEQ_PART_NONE;
    }
    switch (rauto_row_of(why)) {
    case RAR_SUPPLY:
        return s.rdy.need.prog_5v   ? SERVO_SEQ_PART_PROG_5V
               : s.rdy.need.pd_mini ? SERVO_SEQ_PART_PD_MINI
                                    : SERVO_SEQ_PART_NONE;
    case RAR_PWM:
        return SERVO_SEQ_PART_PWM;
    case RAR_ARM:
        return SERVO_SEQ_PART_ARM;
    default:
        return SERVO_SEQ_PART_NONE;
    }
}

/** @brief The time the sequence runs on: the screen's clock. */
static uint32_t seq_now(void)
{
    return s.now_ms;
}

/**
 * @brief Every output the sequence may have switched, off; the bench to
 *        SAFE.  The run's own end does the same (ra_apply()).
 */
static void all_off(void)
{
    if (s.gate.armed || s.gate.arm_asked || s.pending.kind == SERVO_CMD_ARM) {
        svs_ask_disarm();
    } else {
        post(SERVO_CMD_DISARM, 0);
    }
    ++s.arm_rev;
    if (prog5v_box_shown()
        && (prog5v_box_on() || s.rdy.need.prog_5v)) {
        prog5v_box_ask_off(PROG5V_BY_SERVO);
    }
    if (s.rdy.need.pd_mini || supply_screen_output_live()) {
        supply_screen_ask_off();
    }
}

/** @brief The step a failure names, as its text. */
static const char *part_name(servo_seq_part_t p)
{
    switch (p) {
    case SERVO_SEQ_PART_PWM:     return TR(RDY_PWM);
    case SERVO_SEQ_PART_PROG_5V: return "PROG 5V";
    case SERVO_SEQ_PART_PD_MINI: return "PD MINI";
    case SERVO_SEQ_PART_ARM:     return "ARM";
    default:                     return "START";
    }
}

/**
 * @brief The failure's line and the band: `PROG 5V FAILED: NO READ-BACK
 *        IN 1000ms`, the caller's refusal, or STOP.
 * @param refused the caller's text that refused, or NULL
 */
static void name_failure(const char *refused)
{
    const servo_seq_part_t p = servo_seq_part(&s.rdy.seq);
    char cause[72];
    switch (servo_seq_why(&s.rdy.seq)) {
    case SERVO_SEQ_WHY_TIMEOUT:
        if (p == SERVO_SEQ_PART_ARM) {
            snprintf(cause, sizeof(cause), TR(SQ_NOT_ARMED),
                     (unsigned)SERVO_SEQ_ARMED_MS);
        } else {
            snprintf(cause, sizeof(cause), TR(SQ_NO_READBACK),
                     (unsigned)SERVO_SEQ_READBACK_MS);
        }
        break;
    case SERVO_SEQ_WHY_REFUSED:
        snprintf(cause, sizeof(cause), "%s",
                 refused != NULL ? refused : TR(RA_REFUSED));
        break;
    default:
        snprintf(cause, sizeof(cause), "STOP");
        break;
    }
    snprintf(s.rdy.fail_buf, sizeof(s.rdy.fail_buf), TR(SQ_FAILED),
             part_name(p), cause);
    s.rdy.fail = s.rdy.fail_buf;
    ui_router_set_alert(s.rdy.fail);
}

/**
 * @brief What a step asked, done the way a finger does it.
 * @param d the asks
 * @param refused the caller's text that refused, for a failure's line
 */
static void apply(const servo_seq_do_t *d, const char *refused)
{
    if (d->set_pwm && !target_locked_by_bench(SET_SERVO_PORT_PWM)) {
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        settings_request_save();
    }
    if (d->p5_on) {
        prog5v_box_ask_on(PROG5V_BY_SERVO);
        ++s.power_rev;
    }
    /* Never past a standard servo's rating: that ON is the HV warning's
     * alone (CK_PD_HV refuses the hold; the read-back then fails). */
    if (d->pd_on && !hv_set_point()) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
        ++s.sup_rev;
    }
    if (d->arm) {
        post(SERVO_CMD_ARM, 0);
        ++s.arm_rev;
    }
    if (d->all_off) {
        all_off();
        name_failure(refused);
    }
    ++s.ctrl_rev;
}

bool svs_seq_begin(void)
{
    if (svs_seq_busy()) {
        return false;
    }
    s.rdy.need = svs_seq_need();
    s.rdy.fail = NULL;
    if (!servo_seq_begin(&s.rdy.seq, &s.rdy.need, seq_now())) {
        return false;
    }
    ++s.ctrl_rev;
    ++s.arm_rev;
    return true;
}

bool svs_seq_frame(const char *refused)
{
    if (!svs_seq_busy()) {
        return false;
    }
    const servo_seq_in_t in = {
        .pwm_set = settings_get_bool(SET_SERVO_PORT_PWM),
        .pwm_on  = s.drive_pwm && !prog_reg_servo_holds(),
        .p5_on   = prog5v_box_on(),
        .pd_on   = s.have_sup && s.sup.online && s.sup.output,
        .armed   = value_live(),
        .refused = refused != NULL,
        .refused_part = (uint8_t)part_of(refused),
        /* The latest window's start: the end it closed by, less its
         * length. */
        .reading = s.have_win && s.win.stamped && s.win.voltage,
        .reading_from_ms = s.win.taken_ms - (uint32_t)SENSE_LINK_WINDOW_MS,
    };
    const servo_seq_step_t was = servo_seq_step_now(&s.rdy.seq);
    servo_seq_do_t d;
    servo_seq_step(&s.rdy.seq, seq_now(), &in, &d);
    apply(&d, refused);
    if (servo_seq_step_now(&s.rdy.seq) != was) {
        ++s.arm_rev;
    }
    return d.start;
}

void svs_seq_started(bool ok, const char *why)
{
    servo_seq_do_t d;
    servo_seq_started(&s.rdy.seq, ok, &d);
    apply(&d, why);
    ++s.arm_rev;
}

void svs_seq_end(servo_test_abort_t why)
{
    if (!svs_seq_busy()) {
        if (why == SERVO_TEST_AB_LEFT) {
            s.rdy.fail = NULL;      /* a screen entered again starts clean */
        }
        return;
    }
    servo_seq_do_t d;
    servo_seq_abort(&s.rdy.seq, SERVO_SEQ_WHY_STOP, &d);
    if (why == SERVO_TEST_AB_LEFT) {
        /* The leave switches the outputs itself: SAFE, and the ports' 5V
         * kept for another SERVO screen. */
        s.rdy.fail = NULL;
        ++s.arm_rev;
        return;
    }
    apply(&d, NULL);
    ++s.arm_rev;
}

bool svs_seq_busy(void)
{
    return servo_seq_busy(&s.rdy.seq);
}

/** @brief The step under way, by name. */
static const char *step_name(servo_seq_step_t st)
{
    switch (st) {
    case SERVO_SEQ_PWM:
        return TR(RDY_PWM);
    case SERVO_SEQ_SUPPLY:
        return s.rdy.need.prog_5v ? "PROG 5V" : "PD MINI";
    case SERVO_SEQ_READBACK:
        return TR(SQ_READBACK);
    case SERVO_SEQ_ARM:
        return "ARM";
    default:
        return "START";
    }
}

const char *svs_seq_line(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const servo_seq_step_t st = servo_seq_step_now(&s.rdy.seq);
    if (st == SERVO_SEQ_IDLE) {
        return buf;
    }
    int count = 0;
    const int at = servo_seq_number(&s.rdy.need, st, &count);
    snprintf(buf, n, TR(SQ_LINE), (unsigned)at, (unsigned)count,
             step_name(st));
    return buf;
}

const char *svs_seq_label(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const servo_seq_step_t st = servo_seq_step_now(&s.rdy.seq);
    if (st != SERVO_SEQ_IDLE) {
        snprintf(buf, n, "%s ...", step_name(st));
    }
    return buf;
}

const char *svs_seq_failure(void)
{
    return s.rdy.fail;
}
