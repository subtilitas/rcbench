/**
 * @file
 * @brief A SERVO function screen's READY card and its ARM + START
 *        sequence.  See servo_ready_int.h for the layout.
 *
 * The card draws the rows the function gives (RUDDER AUTO's:
 * servo_rudder_auto_ready.c, TEST's: servo_test_ready.c, LIMIT RUN's:
 * servo_limit_ready.c, through one table of each function's calls) and
 * takes their taps.  The sequence is
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

#include "link_pages.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "sensors_rows.h"
#include "settings.h"
#include "servo_limrun.h"
#include "supply_screen.h"
#include "ui_footer.h"
#include "ui_screen.h"
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
#define DOT_DX 5 /**< The center's offset, px. */
/** @brief A tag's width past its text: the text's room each side and the
 *         slanted end, px. */
#define TAG_EXTRA (2 * READY_TAG_PAD + READY_TAG_SLANT)

_Static_assert(READY_ROW_Y + READY_ROWS_MAX * READY_ROW_H <= READY_RAIL_Y,
               "the rows end above the rail line");
_Static_assert(READY_RAIL_Y + 20 + 8 <= CARD_FLOOR,
               "the rail line ends inside the card");
_Static_assert(18 + READY_LABEL_W + READY_VALUE_W <= RDY_W,
               "the dot, the label and the value fit the column");
_Static_assert(18 + READY_LABEL_W + READY_TAG_GAP
                   + (READY_VALUE_W / SV_CELL) * SV_CELL + TAG_EXTRA
                   <= RDY_W,
               "a tag round a whole state stays clear of the labels");

/* ------------------------------------------------------------ the card */

void ready_init(void)
{
    memset(&s.rdy, 0, sizeof(s.rdy));
    servo_seq_init(&s.rdy.seq);
    s.rdy.pressed = -1;
    prog_ready_reset();
}

/** @brief What the card asks of a function: its rows, its first refusal
 *         and its row, a row's tap, what a text names when a step fails
 *         on it, the hold's press and drawing. */
typedef struct {
    int count;                               /**< its rows            */
    bool (*row)(int i, rdy_row_t *r);        /**< row @p i            */
    const char *(*blocked)(int *row);        /**< the first refusal   */
    void (*tap)(int i);                      /**< a tap on row @p i   */
    rdy_kind_t (*kind)(const char *why);     /**< what @p why names   */
    bool (*running)(void);                   /**< its run under way   */
    bool (*hold_down)(const touch_event_t *evt); /**< a DOWN on the hold */
    void (*draw_hold)(gfx_canvas_t *c);      /**< the hold drawn      */
} rdy_fn_t;

/** @brief RUDDER AUTO's first refusal, as rdy_fn_t asks it. */
static const char *ra_blocked(int *row)
{
    rauto_row_t r = RAR_COUNT;
    const char *why = rauto_hold_blocked(&r);
    *row = (int)r;
    return why;
}

/** @brief What a text of RUDDER AUTO's gate names. */
static rdy_kind_t ra_kind(const char *why)
{
    switch (rauto_row_of(why)) {
    case RAR_SUPPLY: return RDY_K_SUPPLY;
    case RAR_PWM:    return RDY_K_PWM;
    case RAR_ARM:    return RDY_K_ARM;
    default:         return RDY_K_OTHER;
    }
}

/** @brief LIMIT RUN's first refusal, as rdy_fn_t asks it. */
static const char *lr_blocked(int *row)
{
    limrun_row_t r = LRR_COUNT;
    const char *why = lim_hold_blocked(&r);
    *row = (int)r;
    return why;
}

/** @brief TEST's first refusal, as rdy_fn_t asks it. */
static const char *te_blocked(int *row)
{
    test_row_t r = TRR_COUNT;
    const char *why = test_hold_blocked(&r);
    *row = (int)r;
    return why;
}

/** @brief What a text of TEST's gate names.  The steps switch the PD mini
 *         themselves, so its row names it whatever the hold switches. */
static rdy_kind_t te_kind(const char *why)
{
    switch (test_row_of(why)) {
    case TRR_PORT:
        /* The carrier's port is no part the steps switch. */
        return (why == ui_servo_str(SERVO_STR_START_PROG_STEPS))
                   ? RDY_K_OTHER : RDY_K_PWM;
    case TRR_PD:  return RDY_K_PD_MINI;
    case TRR_ARM: return RDY_K_ARM;
    default:      return RDY_K_OTHER;
    }
}

/** @brief Whether RUDDER AUTO's run is under way. */
static bool ra_running(void) { return servo_rauto_running(&s.ra.run); }
/** @brief Whether the limit run is under way. */
static bool lr_running(void) { return servo_limrun_running(&s.lim); }
/** @brief Whether TEST's run is under way. */
static bool te_running(void) { return servo_test_running(&s.test); }

/** @brief RUDDER AUTO's calls. */
static const rdy_fn_t k_rauto = {
    (int)RAR_COUNT, rauto_ready_row, ra_blocked, rauto_ready_tap, ra_kind,
    ra_running, rauto_down, rauto_draw_hold,
};
/** @brief TEST's calls. */
static const rdy_fn_t k_test = {
    (int)TRR_COUNT, test_ready_row, te_blocked, test_ready_tap, te_kind,
    te_running, test_hold_down, test_draw_hold,
};
/** @brief LIMIT RUN's calls. */
static const rdy_fn_t k_limrun = {
    (int)LRR_COUNT, lim_ready_row, lr_blocked, lim_ready_tap, lim_kind_of,
    lr_running, lim_hold_down, lim_draw_hold,
};

/** @brief The function shown's calls.  @return NULL without a card */
static const rdy_fn_t *fn_now(void)
{
    switch ((servo_fn_t)s.fn) {
    case SERVO_FN_RAUTO:  return &k_rauto;
    case SERVO_FN_TEST:   return &k_test;
    case SERVO_FN_LIMRUN: return &k_limrun;
    default:              return NULL;
    }
}

bool ready_shown(void)
{
    return fn_now() != NULL;
}

int ready_count(void)
{
    if (drive_shown()) {
        return drive_ready_count();
    }
    if (prog_ready_shown()) {
        return prog_ready_count();     /* KST's and HITEC's */
    }
    return ready_shown() ? fn_now()->count : 0;
}

bool ready_row(int i, rdy_row_t *r)
{
    if (drive_shown()) {
        return drive_ready_row(i, r);
    }
    if (prog_ready_shown()) {
        return prog_ready_row(i, r);
    }
    if (!ready_shown()) {
        return false;
    }
    return fn_now()->row(i, r);
}

const char *ready_refusal(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return NULL;
    }
    if (drive_shown()) {
        int i = 0;
        const char *why = drive_hold_blocked(&i);
        rdy_row_t r;
        if (why == NULL || !drive_ready_row(i, &r)) {
            return NULL;
        }
        snprintf(buf, n, TR(RDY_BAND), r.label, why);
        return buf;
    }
    if (!ready_shown()) {
        return NULL;
    }
    int row = -1;
    const char *why = fn_now()->blocked(&row);
    if (why == NULL) {
        return NULL;
    }
    rdy_row_t r;
    const char *label = (ready_row(row, &r)) ? r.label : "";
    snprintf(buf, n, TR(RDY_BAND), label, why);
    return buf;
}

gfx_rect_t ready_hold_rect(void)
{
    return ui_footer_rect(UI_FOOTER_HOLD);
}

bool ready_hold_stops(void)
{
    return ready_shown() && (svs_seq_busy() || fn_now()->running());
}

bool ready_hold_down(const touch_event_t *evt)
{
    return ready_shown() && fn_now()->hold_down(evt);
}

void ready_draw_hold(gfx_canvas_t *c)
{
    if (ready_shown()) {
        fn_now()->draw_hold(c);
    }
}

gfx_rect_t ready_card_rect(void)
{
    /* LIVE, SWEEP and RUDDER: the title and the rows; their controls are
     * under them. */
    if (drive_shown()) {
        return (gfx_rect_t){ RCARD_X, RCARD_Y, RCARD_W,
                             (int16_t)(READY_ROW_Y + ready_count()
                                       * READY_ROW_H + 4 - RCARD_Y) };
    }
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
    case RDY_SEQ:
    case RDY_WARN: return ui_theme_color(UI_C_WARN);
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
    case RDY_SEQ:
    case RDY_WARN: return ui_theme_color(UI_C_WARN);
    case RDY_BAD:  return ui_theme_color(UI_C_DANGER);
    case RDY_IDLE: return ui_theme_color(UI_C_TEXT_FAINT);
    default:       return ui_theme_color(UI_C_TEXT);
    }
}

/**
 * @brief A row's value as the card shows it: cut after its last whole
 *        word that fits @p cells, mid-word for a first word longer.  A
 *        tap on the row shows it whole.
 * @param value the row's value
 * @param cells the cells it may take
 * @param out receives the text
 * @param n @p out's size
 */
static void cut_value(const char *value, int cells, char *out, size_t n)
{
    size_t cut = gfx_text_prefix(value, cells);
    if (value[cut] != '\0') {
        size_t sp = cut;
        while (sp > 0u && value[sp] != ' ') {
            --sp;
        }
        cut = (sp > 0u) ? sp : cut;
    }
    snprintf(out, n, "%.*s", (int)cut, value);
}

int ready_tag_left(void)
{
    int widest = 0;
    for (int i = 0; i < ready_count(); ++i) {
        rdy_row_t r;
        if (ready_row(i, &r)) {
            char v[64];
            cut_value(r.value, READY_VALUE_W / SV_CELL, v, sizeof(v));
            const int w = gfx_text_width(SV_FONT_LABEL, v, 1);
            widest = (w > widest) ? w : widest;
        }
    }
    return RDY_X + RDY_W - TAG_EXTRA - widest;
}

gfx_rect_t ready_tag_rect(int i)
{
    if (i < 0 || i >= ready_count()) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    const int x = ready_tag_left();
    const gfx_rect_t a = ready_row_rect(i);
    return (gfx_rect_t){ (int16_t)x,
                         (int16_t)(a.y + (READY_ROW_H - READY_TAG_H) / 2),
                         (int16_t)(RDY_X + RDY_W - x), READY_TAG_H };
}

/**
 * @brief A value's raised tag from @p x to the values' right edge, its
 *        right end slanted: the button fill, a lit top and left edge, a
 *        dark bottom edge, a dim edge along the slant and a 2px drop
 *        shadow down-right in the page's ground.
 * @param c the canvas
 * @param x its left edge
 * @param y its top
 * @param pressed lightens the fill, as a pressed button's
 */
static void draw_tag(gfx_canvas_t *c, int x, int y, bool pressed)
{
    const int r = RDY_X + RDY_W;
    const int h = READY_TAG_H;
    const int k = READY_TAG_SLANT;
    gfx_fill_trapezoid(c, y + 2, h, x + 2, r + 2, x + 2, r + 2 - k,
                       ui_theme_color(UI_C_BG));
    gfx_fill_trapezoid(c, y, h, x, r, x, r - k, ui_theme_color(UI_C_GRID));
    gfx_color_t body = ui_theme_color(UI_C_PANEL_HI);
    if (pressed) {
        body = gfx_lerp(body, GFX_WHITE, 110);
    }
    gfx_fill_trapezoid(c, y, h, x + 1, r - 1, x + 1, r - 1 - k, body);
    const gfx_color_t lit = ui_theme_color(UI_C_BEVEL_LIT);
    gfx_hline(c, x, y, r - x, lit);
    gfx_vline(c, x, y, h - 1, lit);
    gfx_hline(c, x, y + h - 1, r - k - x, ui_theme_color(UI_C_BEVEL_DARK));
}

/**
 * @brief Row @p i drawn: its ground, dot, label and its value on a tag
 *        from @p left, the value cut to its 20 cells.
 * @param c the canvas
 * @param i the row
 * @param left the tags' shared left edge, ready_tag_left()
 */
static void draw_row(gfx_canvas_t *c, int i, int left)
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
    draw_tag(c, left, a.y + (READY_ROW_H - READY_TAG_H) / 2,
             s.rdy.pressed == i);
    char v[64];
    cut_value(r.value, READY_VALUE_W / SV_CELL, v, sizeof(v));
    gfx_text(c, left + READY_TAG_PAD, ty, v, SV_FONT_LABEL,
             value_color(r.st), 1);
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
    const bool ina = (s.source == SERVO_SOURCE_INA3221);
    snprintf(line, sizeof(line), "%s", ina ? name : "PD MINI");
    gfx_text(c, RDY_X, READY_RAIL_Y, line, SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    /* The panel's model is no measurement: its word after the name. */
    if (!ina && supply_screen_model()) {
        gfx_text(c, RDY_X + gfx_text_width(SV_FONT_LABEL, line, 1) + SV_CELL,
                 READY_RAIL_Y, TR(SV_MODELLED), SV_FONT_LABEL,
                 ui_theme_color(UI_C_WARN), 1);
    }
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
    /* One left edge for every tag: the widest value's. */
    const int left = ready_tag_left();
    for (int i = 0; i < ready_count(); ++i) {
        draw_row(c, i, left);
    }
    /* On LIVE, SWEEP and RUDDER the card's controls are under the rows
     * and the rail line is drawn with them. */
    if (!drive_shown()) {
        ready_render_rail(c);
    }
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
    if (evt == NULL
        || !(ready_shown() || drive_shown() || prog_ready_shown())
        || !gfx_rect_contains(ready_card_rect(), evt->point.x,
                              evt->point.y)) {
        return false;
    }
    /* A stop counts on the press, whatever other fingers hold: KST's and
     * HITEC's supply row while it reads ON asks OFF at once. */
    if (prog_ready_shown()
        && prog_ready_stops_at(evt->point.x, evt->point.y)) {
        prog_ready_tap(prog_ready_at(PRR_SUPPLY));
        ++s.ctrl_rev;
        return true;
    }
    /* A row's fix opens a list, a warning or another screen: none under
     * a press held anywhere, whose release it would take. */
    if (s.rdy.pressed >= 0 || svs_press_held()) {
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
    if (prog_write_rest(evt)) {
        return true;            /* KST's and HITEC's WRITE */
    }
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
    if (!touch_event_is_tap_up(evt)
        || !gfx_rect_contains(ready_row_rect(i), evt->point.x,
                              evt->point.y)) {
        return true;
    }
    /* A press that came down elsewhere while the finger was on the row
     * keeps its release: the fix opens nothing over it. */
    if (svs_press_held()) {
        ui_action_refused(TR(FOOT_PRESS_HELD));
    } else if (drive_shown()) {
        drive_ready_tap(i);
    } else if (prog_ready_shown()) {
        prog_ready_tap(i);
    } else if (ready_shown()) {
        fn_now()->tap(i);
    }
    return true;
}

void ready_cancel(void)
{
    prog_ready_reset();
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
    /* The PD mini only where it is the rail's feed: on, or off on a rail
     * that reads no 3.0V.  One off beside a rail fed from elsewhere stays
     * off (on a PROG PORT the gate refuses it, SV_LIM_PROG_PD_OFF). */
    const bool pd_on = s.have_sup && s.sup.online && s.sup.output;
    const bool rail_live = s.have_win && s.win.voltage
                           && s.win.mean_mv
                                  >= (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
    n.pd_mini  = lim_pd_feeds() && (pd_on || !rail_live);
    /* TEST's steps set the PD mini and switch its output themselves, at
     * each step's voltage: the hold switches only the port's pulse. */
    if (s.fn == SERVO_FN_TEST) {
        n.prog_5v = false;
        n.pd_mini = false;
    }
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
    if (why != NULL && drive_shown()) {
        return drive_part_of(why);
    }
    if (why == NULL || !ready_shown()) {
        return SERVO_SEQ_PART_NONE;
    }
    switch (fn_now()->kind(why)) {
    case RDY_K_SUPPLY:
        return s.rdy.need.prog_5v   ? SERVO_SEQ_PART_PROG_5V
               : s.rdy.need.pd_mini ? SERVO_SEQ_PART_PD_MINI
                                    : SERVO_SEQ_PART_NONE;
    case RDY_K_PD_MINI:
        return SERVO_SEQ_PART_PD_MINI;
    case RDY_K_PWM:
        return SERVO_SEQ_PART_PWM;
    case RDY_K_ARM:
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

/* Every output the sequence may have switched, off; the bench to SAFE.
 * The run's own end does the same (ra_apply()). */
void svs_all_off(void)
{
    if (s.gate.armed || s.gate.arm_asked || s.pending.kind == SERVO_CMD_ARM) {
        svs_ask_disarm();
    } else {
        post(SERVO_CMD_DISARM, 0);
    }
    ++s.arm_rev;
    svs_supplies_off();
}

/* The supplies the sequence may have switched on, off: the carrier's PROG
 * 5V and the PD mini's output. */
void svs_supplies_off(void)
{
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
    case SERVO_SEQ_PART_CLEAR:   return TR(SQ_CLEAR);
    case SERVO_SEQ_PART_PWM:     return TR(RDY_PWM);
    case SERVO_SEQ_PART_PROG_5V: return "PROG 5V";
    case SERVO_SEQ_PART_PD_MINI: return "PD MINI";
    case SERVO_SEQ_PART_ARM:     return "ARM";
    default:                     return "START";
    }
}

/**
 * @brief The failure's line and the band: `PROG 5V FAILED: NO READ-BACK
 *        IN 1000ms`, the caller's refusal, STOP, `FAILSAFE CLEAR FAILED:
 *        STILL LATCHED AFTER 500ms`, or for a supply in the IOmcu's
 *        failsafe the supply's own refusal text.
 * @param refused the caller's text that refused, or NULL
 */
static void name_failure(const char *refused)
{
    const servo_seq_part_t p = servo_seq_part(&s.rdy.seq);
    char cause[72];
    if (servo_seq_why(&s.rdy.seq) == SERVO_SEQ_WHY_FAILSAFE
        && p != SERVO_SEQ_PART_CLEAR) {
        /* The words the control task gives the IOmcu's refusal of the
         * supply's ON (supply_link_on_ended()). */
        snprintf(s.rdy.fail_buf, sizeof(s.rdy.fail_buf), "%s",
                 TR(ALERT_SUPPLY_REFUSED_FAILSAFE));
        s.rdy.fail = s.rdy.fail_buf;
        ui_router_set_alert(s.rdy.fail);
        s.rdy.fail_alert = ui_router_alert_number();
        return;
    }
    switch (servo_seq_why(&s.rdy.seq)) {
    case SERVO_SEQ_WHY_FAILSAFE:
        snprintf(cause, sizeof(cause), TR(SQ_STILL_LATCHED),
                 (unsigned)SERVO_SEQ_CLEAR_MS);
        break;
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
    s.rdy.fail_alert = ui_router_alert_number();
}

/** @brief The failure's line and its band go: a screen left takes its
 *         steps' failure with it, and the next screen opens clean.  A band
 *         another alert replaced stays (ui_router_withdraw_alert()). */
static void drop_failure(void)
{
    s.rdy.fail = NULL;
    (void)ui_router_withdraw_alert(s.rdy.fail_alert);
    s.rdy.fail_alert = 0u;
}

/**
 * @brief What a step asked, done the way a finger does it.
 * @param d the asks
 * @param refused the caller's text that refused, for a failure's line
 */
static void apply(const servo_seq_do_t *d, const char *refused)
{
    if (d->clear) {
        s.rdy.clear_owed = true;
    }
    if (d->set_pwm && !target_locked_by_bench(SET_SERVO_PORT_PWM)) {
        settings_set(SET_SERVO_PORT_PWM, 1.0f);
        settings_request_save();
    }
    if (d->p5_on) {
        prog5v_box_ask_on(PROG5V_BY_SERVO);
        ++s.power_rev;
    }
    /* Never past a standard servo's rating: that ON is the HV warning's
     * alone (TEST's check refuses the hold; the read-back then fails).
     * LIMIT RUN and RUDDER AUTO set the PD mini to 5.0V at the hold
     * (servo_run_supply.c). */
    if (d->pd_on && !hv_set_point()) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
        ++s.sup_rev;
    }
    if (d->arm) {
        post(SERVO_CMD_ARM, 0);
        ++s.arm_rev;
    }
    if (d->all_off) {
        s.rdy.clear_owed = false;
        svs_all_off();
        name_failure(refused);
    }
    ++s.ctrl_rev;
}

/**
 * @brief Whether the IOmcu reports its failsafe latched: its STATUS bit
 *        LINK_FAULT_LINK_SILENT, as the band's FAULT 01.
 * @return true then
 */
static bool far_latched(void)
{
    return (ui_router_status()->faults & (uint16_t)LINK_FAULT_LINK_SILENT)
           != 0u;
}

/**
 * @brief Whether the IOmcu does not trust the heartbeat: its STATUS bit
 *        LINK_FAULT_HEARTBEAT, as the band's FAULT 10.  No CLEAR lifts it.
 * @return true then
 */
static bool far_line_out(void)
{
    return (ui_router_status()->faults & (uint16_t)LINK_FAULT_HEARTBEAT)
           != 0u;
}

bool servo_screen_take_clear(void)
{
    const bool owed = s.rdy.clear_owed;
    s.rdy.clear_owed = false;
    return owed;
}

bool svs_seq_begin(void)
{
    if (svs_seq_busy()) {
        return false;
    }
    s.rdy.need = drive_shown() ? drive_seq_need() : svs_seq_need();
    /* The failsafe latched at the hold: the steps write CLEAR first, as
     * ARM does.  Not while the heartbeat is distrusted: ARM writes its
     * CLEAR only once the IOmcu trusts the line, and no CLEAR lifts that
     * fault. */
    s.rdy.need.clear = far_latched() && !far_line_out();
    s.rdy.fail = NULL;
    s.rdy.clear_owed = false;
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
    /* The PD mini was judged at the hold.  Until the steps ask its ON, a
     * rail fed from elsewhere since then refuses them, so the PD mini is
     * never switched on beside another supply. */
    if (refused == NULL && s.rdy.need.pd_mini
        && servo_seq_step_now(&s.rdy.seq) < SERVO_SEQ_READBACK
        && !(drive_shown() ? drive_seq_need() : svs_seq_need()).pd_mini) {
        refused = TR(RDY_FED_WHY);
    }
    const servo_seq_in_t in = {
        .pwm_set = settings_get_bool(SET_SERVO_PORT_PWM),
        .pwm_on  = s.drive_pwm && !prog_reg_servo_holds(),
        .p5_on   = prog5v_box_on(),
        /* LIMIT RUN and RUDDER AUTO: on at the run's 5.0V. */
        .pd_on   = s.have_sup && s.sup.online && s.sup.output
                   && rsup_read_back(s.sup.set_v),
        .armed   = value_live(),
        .refused = refused != NULL,
        .refused_part = (uint8_t)part_of(refused),
        /* The latest window's start: the end it closed by, less its
         * length. */
        .reading = s.have_win && s.win.stamped && s.win.voltage,
        .reading_from_ms = s.win.taken_ms - (uint32_t)SENSE_LINK_WINDOW_MS,
        .latched = far_latched(),
        .line_out = far_line_out(),
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
            drop_failure();         /* a screen entered again starts clean */
        }
        return;
    }
    servo_seq_do_t d;
    servo_seq_abort(&s.rdy.seq, SERVO_SEQ_WHY_STOP, &d);
    s.rdy.clear_owed = false;
    if (why == SERVO_TEST_AB_LEFT) {
        /* The leave switches the outputs itself: SAFE, and the ports' 5V
         * kept for another SERVO screen. */
        drop_failure();
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
    case SERVO_SEQ_CLEAR:
        return TR(SQ_CLEAR);
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
