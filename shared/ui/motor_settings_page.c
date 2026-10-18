/*
 * MOTOR & ESC > SETTINGS: the ESC / BENCH category's 8 rows (SET_CAT_ESC),
 * under PACK, MOTOR and THROTTLE OUTPUT.  The rows are the settings SETUP
 * listed before they moved here: one home each, the same label, help and
 * value text.
 *
 * A tap on a row's value opens the keypad over the page and the strip; its
 * OK sets the value onto the setting's step inside its range and asks for
 * a save (settings_request_save()).  The store is one set: the panel writes
 * the whole of it when the bench is SAFE and no transfer runs, so a value
 * changed on SETUP and not saved there is written with it.  The line at the
 * page's foot says where the store stands: SAVED, SAVE PENDING, NOT SAVED,
 * or SETUP CHANGES NOT SAVED while SETUP holds a change nothing has asked
 * to keep.
 *
 * Idle pulse and Full pulse are the throttle's endpoints, which the IOmcu
 * (the RP2350 board) takes only on a disarmed bench.  They take a press
 * only while the bench is SAFE and ARM is not held (motor_screen.c's
 * endpoints_free()); otherwise their help line says so, and a keypad open
 * for one closes.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "motor_screen.h"
#include "settings.h"
#include "ui_keypad.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names. */
#include "motor_screen_int.h"

/* The page's rows in their order, each under its heading. */
enum { HD_PACK = 0, HD_MOTOR, HD_THROTTLE, HD_COUNT };

static const struct {
    setting_id_t id;
    uint8_t      heading;
} k_rows[] = {
    { SET_PACK_CELLS,  HD_PACK },
    { SET_PACK_MAH,    HD_PACK },
    { SET_PACK_MOHM,   HD_PACK },
    { SET_MOTOR_POLES, HD_MOTOR },
    { SET_MOTOR_KV,    HD_MOTOR },
    { SET_OUT_MIN_US,  HD_THROTTLE },
    { SET_OUT_MAX_US,  HD_THROTTLE },
    { SET_OUT_RAMP,    HD_THROTTLE },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

static const ui_text_id_t k_heading[HD_COUNT] = {
    TX_MO_SET_PACK, TX_MO_SET_MOTOR, TX_MO_SET_THROTTLE,
};

/*
 * Down the page: a heading 24 px, a row 34 px with its 28 px value box.
 * Across: the label's 16 cells from x 16, the help's 36 cells from x 148,
 * the value box's 100 px to 10 px short of the page's edge.
 */
#define TOP_Y      (PAGE_Y + 8)
#define HEAD_H     24
#define ROW_PITCH  34
#define BOX_H      28
#define LABEL_X    (PAGE_X + 10)
#define HELP_X     (LABEL_X + 16 * 8 + 4)
#define BOX_W      100
#define BOX_X      (PAGE_X + PAGE_W - 10 - BOX_W)
#define SAVE_Y     (PAGE_Y + PAGE_H - 24)

static struct {
    ui_keypad_t  kp;
    int          kp_row;      /* the row the keypad types, or -1         */
    int          pressed;     /* the row whose value is pressed, or -1   */
    uint8_t      press_id;
    uint32_t     rev;
    uint8_t      drawn_save;  /* the save line's state last drawn        */
    bool         drawn_safe;
} p;

/* Whether the store reached the medium: 0 saved, 1 waiting for a quiet
 * moment, 2 refused, 3 changed on SETUP and not asked to be saved. */
static uint8_t save_state(void)
{
    if (settings_save_failed()) {
        return 2u;
    }
    if (settings_save_asked()) {
        return 1u;
    }
    return settings_dirty() ? 3u : 0u;
}

/* The top of row @p i: its heading's, the headings above it and its place
 * among the rows. */
static int row_y(int i)
{
    int y = TOP_Y;
    int head = -1;
    for (int k = 0; k <= i; ++k) {
        if ((int)k_rows[k].heading != head) {
            head = (int)k_rows[k].heading;
            y += HEAD_H;
        }
        if (k < i) {
            y += ROW_PITCH;
        }
    }
    return y;
}

static gfx_rect_t box_rect(int i)
{
    return (gfx_rect_t){ BOX_X, (int16_t)row_y(i), BOX_W, BOX_H };
}

/* The throttle's endpoints, which change on a SAFE bench only. */
static bool safe_only(int i)
{
    return k_rows[i].id == SET_OUT_MIN_US || k_rows[i].id == SET_OUT_MAX_US;
}

static bool row_locked(int i, bool safe)
{
    return (safe_only(i) && !safe) || settings_held(k_rows[i].id)
           || settings_derived(k_rows[i].id);
}

/* The digits the keypad takes after the point: none for a step of 1 or
 * more, which every row here has. */
static int decimals_of(const setting_def_t *d)
{
    if (d->step >= 1.0f) {
        return 0;
    }
    return (d->step >= 0.1f) ? 1 : 2;
}

static void open_keypad(int i)
{
    const setting_def_t *d = settings_def(k_rows[i].id);
    if (d == NULL) {
        return;
    }
    ui_keypad_open(&p.kp, (gfx_rect_t){ PAGE_X, 3, PAGE_W, H - 3 - PAD },
                   ui_setting_label(k_rows[i].id),
                   (d->unit != NULL) ? d->unit : "", settings_get(k_rows[i].id),
                   d->min, d->max, decimals_of(d));
    p.kp_row = i;
    ++p.rev;
}

static void close_keypad(void)
{
    ui_keypad_close(&p.kp);
    p.kp_row = -1;
    ++p.rev;
}

void msp_reset(void)
{
    memset(&p, 0, sizeof(p));
    p.kp_row = -1;
    p.pressed = -1;
    p.drawn_save = 0xFFu;
}

void msp_leave(void)
{
    if (p.kp.open) {
        close_keypad();
    }
    p.pressed = -1;
    ++p.rev;
}

void msp_cancel(void)
{
    ui_keypad_cancel_press(&p.kp);
    p.pressed = -1;
    ++p.rev;
}

bool msp_modal(void) { return p.kp.open; }

bool msp_pressed(void) { return p.pressed >= 0 || p.kp.pressed >= 0; }

bool msp_back(void)
{
    if (!p.kp.open) {
        return false;
    }
    close_keypad();
    return true;
}

uint32_t msp_revision(void) { return p.rev; }

void msp_tick(bool safe)
{
    if (p.kp.open && p.kp_row >= 0 && row_locked(p.kp_row, safe)) {
        /* The bench left SAFE under the keypad of an endpoint: what was
         * typed would not reach the IOmcu until it disarms. */
        close_keypad();
    }
    if (p.drawn_save != save_state() || p.drawn_safe != safe) {
        ++p.rev;
    }
}

void msp_event(const touch_event_t *evt, bool safe)
{
    if (evt == NULL) {
        return;
    }
    /* A DOWN on the rail is the CONTROL card's, under the keypad too. */
    if (evt->type == TOUCH_EVENT_DOWN && evt->point.x >= PAGE_X + PAGE_W) {
        return;
    }
    if (p.kp.open) {
        float v = 0.0f;
        const uint32_t before = p.kp.revision;
        const ui_keypad_result_t r = ui_keypad_event(&p.kp, evt, &v);
        if (r == UI_KEYPAD_OK && p.kp_row >= 0
            && !row_locked(p.kp_row, safe)) {
            /* Onto the setting's step inside its range (settings_set()),
             * in force at once, and kept when the bench is next SAFE. */
            settings_set(k_rows[p.kp_row].id, v);
            settings_request_save();
        }
        if (r != UI_KEYPAD_NONE) {
            p.kp_row = -1;
        }
        if (r != UI_KEYPAD_NONE || p.kp.revision != before) {
            ++p.rev;
        }
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (p.pressed >= 0 && evt->point.id != p.press_id) {
            return;   /* a second finger cannot take the first one's press */
        }
        p.pressed = -1;
        for (int i = 0; i < ROW_COUNT; ++i) {
            if (gfx_rect_contains(box_rect(i), x, y)) {
                /* A locked row takes the press and opens nothing. */
                if (!row_locked(i, safe)) {
                    p.pressed  = i;
                    p.press_id = evt->point.id;
                }
                break;
            }
        }
        ++p.rev;
        return;
    }
    if (p.pressed < 0 || evt->point.id != p.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const int i = p.pressed;
        p.pressed = -1;
        if (touch_event_is_tap_up(evt) && gfx_rect_contains(box_rect(i), x, y)
            && !row_locked(i, safe)) {
            open_keypad(i);
        }
        ++p.rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static void draw_row(gfx_canvas_t *c, int i, bool safe)
{
    const setting_id_t id = k_rows[i].id;
    const setting_def_t *d = settings_def(id);
    const int y = row_y(i);
    const bool locked = row_locked(i, safe);
    gfx_text(c, LABEL_X, y + 6, ui_setting_label(id), &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    /* The help, or why the row takes no press, clipped short of the box. */
    const bool armed_lock = safe_only(i) && !safe;
    const char *help = armed_lock ? TR(MO_SET_SAFE_ONLY) : ui_setting_help(id);
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, (gfx_rect_t){ HELP_X, (int16_t)y,
                                        (int16_t)(BOX_X - 6 - HELP_X),
                                        BOX_H });
    gfx_text(c, HELP_X, y + 6, help, &gfx_font_8x16,
             armed_lock ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_FAINT), 1);
    c->clip = saved;

    char v[24], line[40];
    (void)ui_setting_value(id, v, sizeof(v));
    const char *unit = (d != NULL && d->unit != NULL) ? d->unit : "";
    snprintf(line, sizeof(line), "%s%s%s", v, (unit[0] != '\0') ? " " : "",
             unit);
    ui_button(c, box_rect(i), line, ui_theme_color(UI_C_PANEL_SUNK),
              p.pressed == i, !locked);
}

void msp_render(gfx_canvas_t *c, bool safe)
{
    p.drawn_safe = safe;
    if (p.kp.open) {
        ui_keypad_render(&p.kp, c);
        return;
    }
    gfx_fill_rect(c, PAGE_X, PAGE_Y, PAGE_W, PAGE_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, PAGE_X, PAGE_Y, PAGE_W, PAGE_H,
                  ui_theme_color(UI_C_EDGE));
    int head = -1;
    for (int i = 0; i < ROW_COUNT; ++i) {
        if ((int)k_rows[i].heading != head) {
            head = (int)k_rows[i].heading;
            gfx_text(c, LABEL_X, row_y(i) - HEAD_H + 4, ui_tr(k_heading[head]),
                     &gfx_font_8x16, ui_theme_color(UI_C_ACCENT), 1);
        }
        draw_row(c, i, safe);
    }
    static const ui_text_id_t k_save[] = {
        TX_SUP_SAVED, TX_SUP_SAVE_WAITING, TX_SUP_NOT_SAVED,
        TX_SUP_SETUP_NOT_SAVED,
    };
    const uint8_t st = save_state();
    p.drawn_save = st;
    gfx_text(c, LABEL_X, SAVE_Y, ui_tr(k_save[st]), &gfx_font_8x16,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* ------------------------------------------------------------ for tests */

int motor_screen_keypad_setting(void)
{
    return (p.kp.open && p.kp_row >= 0) ? (int)k_rows[p.kp_row].id : -1;
}

int motor_settings_rows(setting_id_t *out, int max)
{
    for (int i = 0; i < ROW_COUNT && i < max; ++i) {
        if (out != NULL) {
            out[i] = k_rows[i].id;
        }
    }
    return ROW_COUNT;
}

int motor_settings_heading(int row)
{
    return (row >= 0 && row < ROW_COUNT) ? (int)k_heading[k_rows[row].heading]
                                         : -1;
}

const char *motor_endpoints_alert(endpoints_block_t why)
{
    switch (why) {
    case ENDPOINTS_BLOCK_OWED:    return TR(ALERT_ARM_ENDPOINTS);
    case ENDPOINTS_BLOCK_UNKNOWN: return TR(ALERT_ARM_RANGE_UNKNOWN);
    case ENDPOINTS_BLOCK_ORDER:   return TR(ALERT_ARM_RANGE_ORDER);
    case ENDPOINTS_BLOCK_REFUSED: return TR(ALERT_ARM_RANGE_REFUSED);
    case ENDPOINTS_BLOCK_DIFFERS: return TR(ALERT_ARM_RANGE_DIFFERS);
    case ENDPOINTS_BLOCK_NONE:
    default:                      return NULL;
    }
}

gfx_rect_t motor_settings_box(int row)
{
    return (row >= 0 && row < ROW_COUNT) ? box_rect(row)
                                         : (gfx_rect_t){ 0, 0, 0, 0 };
}
