/**
 * @file
 * @brief The IMU screen (imu_screen.h): its model, its touches and its
 *        texts.  imu_screen_draw.c draws it.
 *
 * The screen commands nothing itself: a tap queues an imu_cmd_t that the
 * panel hands to the IMU's task (imu_ctl.h), and the panel hands back
 * what to show each frame (imu_screen_set()).  SCAN opens the found list
 * at once; it shows the scan under way, then the devices, and a tap on one
 * binds it and closes the list.  CALIBRATE ACCELEROMETER is a 2s hold with
 * the band's hint on a short press (UI_HOLD_S).
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "imu_screen_int.h"
#include "sensors_pane.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_theme.h"

ims_state_t ims;

/** @brief The time since the newest angle frame reached the screen, ms. */
static uint32_t s_age_ms;

/** @brief Repaint on the next frame of each framebuffer. */
static void touched(void) { ims.drawn_mask = 0u; }

/** @brief Repaint on the next frame (imu_screen.h). */
void imu_screen_invalidate(void) { touched(); }

/* ------------------------------------------------------------ the model */

/**
 * @brief Bluetooth runs and no scan does: a scan or a bind may start.
 * @return Whether SCAN and a device take a press.
 */
static bool at_free(void)
{
    if (ims.v.cal == IMU_CAL_RUNNING) {
        return false;         /* the calibration keeps the sensor bound */
    }
    switch (ims.v.link) {
    case IMU_LINK_READY:
    case IMU_LINK_CONNECTING:
    case IMU_LINK_BOUND:
    case IMU_LINK_CONNECTED:
    case IMU_LINK_NO_FRAMES:
        return true;
    default:
        return false;
    }
}

bool ims_row_shown(int row)
{
    switch (row) {
    case IMS_ROW_SCAN: case IMS_ROW_CAL: case IMS_ROW_ZERO:
    case IMS_ROW_UNZERO: case IMS_ROW_SETTINGS:
        return !ims.list;
    case IMS_ROW_RESCAN: case IMS_ROW_CLOSE:
        return ims.list;
    default:
        return false;
    }
}

bool ims_row_enabled(int row)
{
    if (!ims_row_shown(row)) {
        return false;
    }
    switch (row) {
    case IMS_ROW_SCAN: case IMS_ROW_RESCAN:
        return at_free();
    case IMS_ROW_CAL:
        return ims.v.link == IMU_LINK_CONNECTED
               && ims.v.cal != IMU_CAL_RUNNING;
    case IMS_ROW_ZERO:
        return ims.v.r.valid;
    case IMS_ROW_UNZERO:
        return ims.v.r.zeroed;
    default:
        return true;          /* IMU SETTINGS and CLOSE */
    }
}

/** @brief A press on the hold the model took away under it ends here. */
static void drop_hold_if_gone(void)
{
    if (ims.have_press && ims.hit == IMS_ROW_CAL
        && !ims_row_enabled(IMS_ROW_CAL)) {
        (void)ui_hold_left(&ims.hold);
        ims.have_press = false;
        ims.hit = IMS_HIT_NONE;
    }
}

void imu_screen_set(const imu_view_t *v)
{
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &ims.v, sizeof(ims.v)) == 0) {
        return;
    }
    if (v->r.taken_ms != ims.v.r.taken_ms || !v->r.valid) {
        s_age_ms = 0u;
    }
    ims.v = *v;
    drop_hold_if_gone();
    touched();
}

/**
 * @brief Queue @p kind for the panel; a full queue drops the oldest.
 * @param kind The command.
 * @param arg  Its argument.
 */
static void post(imu_cmd_kind_t kind, uint8_t arg)
{
    if (ims.cmd_n >= IMS_CMDS) {
        memmove(&ims.cmds[0], &ims.cmds[1],
                (IMS_CMDS - 1u) * sizeof(ims.cmds[0]));
        ims.cmd_n = IMS_CMDS - 1u;
    }
    ims.cmds[ims.cmd_n].kind = kind;
    ims.cmds[ims.cmd_n].arg = arg;
    ++ims.cmd_n;
}

bool imu_screen_take(imu_cmd_t *out)
{
    if (out == NULL || ims.cmd_n == 0u) {
        return false;
    }
    *out = ims.cmds[0];
    --ims.cmd_n;
    memmove(&ims.cmds[0], &ims.cmds[1], ims.cmd_n * sizeof(ims.cmds[0]));
    return true;
}

/* ---------------------------------------------------------- the geometry */

gfx_rect_t imu_screen_row_rect(int row)
{
    switch (row) {
    case IMS_ROW_SCAN:
        return gfx_rect_make(IMS_BX, IMS_RY, IMS_BW, IMS_BH);
    case IMS_ROW_CAL:
        return gfx_rect_make(IMS_BX, IMS_RY + 2 * IMS_BPITCH, IMS_BW,
                             IMS_BH);
    case IMS_ROW_ZERO:
        return gfx_rect_make(IMS_BX, IMS_RY + 3 * IMS_BPITCH, IMS_HALF_W,
                             IMS_BH);
    case IMS_ROW_UNZERO:
        return gfx_rect_make(IMS_BX + IMS_BW - IMS_HALF_W,
                             IMS_RY + 3 * IMS_BPITCH, IMS_HALF_W, IMS_BH);
    case IMS_ROW_SETTINGS:
        return gfx_rect_make(IMS_BX, IMS_RY + 4 * IMS_BPITCH, IMS_BW,
                             IMS_BH);
    case IMS_ROW_RESCAN:
        return gfx_rect_make(IMS_SX, IMS_LY, IMS_SW, IMS_BH);
    case IMS_ROW_CLOSE:
        return gfx_rect_make(IMS_SX, IMS_FLOOR - IMS_BH, IMS_SW, IMS_BH);
    default:
        return gfx_rect_make(0, 0, 0, 0);
    }
}

gfx_rect_t imu_screen_choice_rect(int i)
{
    if (i < 0 || i >= (int)IMU_FOUND_MAX) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(IMS_LX, IMS_LY + i * IMS_LP, IMS_LW, IMS_LH);
}

/** @brief The calibration's hold (imu_screen.h). @return Its rectangle. */
gfx_rect_t imu_screen_hold_rect(void)
{
    return imu_screen_row_rect(IMS_ROW_CAL);
}

_Static_assert(IMS_RY + 4 * IMS_BPITCH + IMS_BH + 16 <= IMS_LINE_Y,
               "the calibration's line starts under IMU SETTINGS");
_Static_assert(IMS_LINE_Y + 2 * IMS_LPITCH + 12 <= IMS_ERR_Y,
               "two lines of the calibration end over the operation's line");
_Static_assert(IMS_ERR_Y + 2 * IMS_LPITCH + 20 <= IMS_FLOOR,
               "two lines of the operation end over the alert band");
_Static_assert(IMS_RY + IMS_RH <= IMS_FLOOR, "the card ends over the band");
_Static_assert(IMS_RX + IMS_RW <= UI_PANEL_W - UI_MARGIN,
               "the card keeps the margin");
_Static_assert(IMS_BX + IMS_BW + 8 <= IMS_RX, "the controls end left of the card");
_Static_assert(IMS_LY + (IMU_FOUND_MAX - 1) * IMS_LP + IMS_LH + 4 <= IMS_FLOOR,
               "8 devices end over the band");
_Static_assert(IMS_LX + IMS_LW + 4 + 16 <= IMS_SX,
               "the list's ground ends left of its buttons");
_Static_assert(IMS_HIDDEN_Y + 2 * IMS_LPITCH <= IMS_FLOOR - IMS_BH,
               "the hidden count's two lines end over CLOSE");
_Static_assert(IMS_BH >= UI_TAP_MIN && IMS_LH >= UI_TAP_MIN,
               "every control is a finger tall");

/* ------------------------------------------------------------- the texts */

/**
 * @brief The last operation's error in words.
 * @param buf Takes them.
 * @param n   Its size, bytes.
 * @return @p buf; "" for none.
 */
static const char *err_text(char *buf, size_t n)
{
    switch (ims.v.err) {
    case IMU_ERR_CONNECT:    snprintf(buf, n, "%s", TR(IMU_ERR_CONNECT)); break;
    case IMU_ERR_NOT_WIT:    snprintf(buf, n, "%s", TR(IMU_ERR_NOT_WIT)); break;
    case IMU_ERR_WRITE:      snprintf(buf, n, "%s", TR(IMU_ERR_WRITE)); break;
    case IMU_ERR_NONE_FOUND: snprintf(buf, n, "%s", TR(IMU_ERR_NONE_FOUND));
                             break;
    case IMU_ERR_SCAN:
        snprintf(buf, n, TR(IMU_ERR_SCAN), (unsigned)ims.v.err_code);
        break;
    case IMU_ERR_NONE:
    default:                 buf[0] = '\0'; break;
    }
    return buf;
}

/**
 * @brief An angle line: the axis, its degrees from the zero or "---".
 * @param axis The axis.
 * @param buf  Takes the line.
 * @param n    Its size, bytes.
 */
static void angle_line(imu_axis_t axis, char *buf, size_t n)
{
    if (!ims.v.r.valid) {
        snprintf(buf, n, "%s  ---", imu_axis_name(axis));
        return;
    }
    snprintf(buf, n, "%s  %+.1f°", imu_axis_name(axis),
             (double)imu_deg(&ims.v.r, axis, false));
}

/**
 * @brief The found list's heading: the scan under way, the count, or why
 *        none came.
 * @param buf Takes it.
 * @param n   Its size, bytes.
 * @param ink Takes its colour.
 */
static void head_line(char *buf, size_t n, gfx_color_t *ink)
{
    *ink = UI_TEXT;
    if (ims.v.link == IMU_LINK_SCANNING) {
        snprintf(buf, n, "%s", TR(IMU_SCANNING));
    } else if (ims.v.found_n > 0u) {
        snprintf(buf, n, TR(IMU_FOUND), (unsigned)ims.v.found_n);
    } else if (ims.v.err != IMU_ERR_NONE) {
        (void)err_text(buf, n);
        *ink = UI_WARN;
    } else {
        snprintf(buf, n, "%s", TR(IMU_LIST_EMPTY));
        *ink = UI_TEXT_DIM;
    }
}

/**
 * @brief The card's sensor lines: SENSOR with the name, and the address.
 * @param i   IMS_L_SENSOR or IMS_L_ADDR.
 * @param buf Takes the line.
 * @param n   Its size, bytes.
 * @param ink Takes its colour.
 */
static void sensor_line(int i, char *buf, size_t n, gfx_color_t *ink)
{
    const char *addr = ims.v.bound_addr;
    if (i == IMS_L_ADDR) {
        snprintf(buf, n, "%s", addr);
        *ink = UI_TEXT_DIM;
    } else if (addr[0] == '\0') {
        snprintf(buf, n, "%s", TR(IMU_NONE_BOUND));
        *ink = UI_TEXT_DIM;
    } else {
        snprintf(buf, n, TR(IMU_SENSOR), ims.v.bound_name[0] != '\0'
                                             ? ims.v.bound_name
                                             : TR(IMU_NO_NAME));
    }
}

const char *ims_line(int i, char *buf, size_t n, gfx_color_t *ink)
{
    gfx_color_t dummy;
    if (ink == NULL) {
        ink = &dummy;
    }
    *ink = UI_TEXT;
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const imu_reading_t *r = &ims.v.r;
    switch (i) {
    case IMS_L_SENSOR: case IMS_L_ADDR:
        sensor_line(i, buf, n, ink);
        break;
    case IMS_L_RADIO:
        snprintf(buf, n, "C6: %s",
                 ims.v.version[0] != '\0' ? ims.v.version : "---");
        break;
    case IMS_L_STATE:
        if (ims.v.link == IMU_LINK_OFF) {
            snprintf(buf, n, "%s", TR(IMU_OFF_LINE));
        } else {
            (void)sensors_rows_imu_word(ims.v.link, r->rate_dhz, buf, n);
        }
        *ink = sensors_rows_imu_color(ims.v.link);
        break;
    case IMS_L_X: angle_line(IMU_AXIS_X, buf, n); break;
    case IMS_L_Y: angle_line(IMU_AXIS_Y, buf, n); break;
    case IMS_L_Z: angle_line(IMU_AXIS_Z, buf, n); break;
    case IMS_L_RATE:
        if (r->valid) {
            snprintf(buf, n, TR(IMU_RATE_AGE), (double)r->rate_dhz / 10.0,
                     (unsigned)s_age_ms);
        } else {
            snprintf(buf, n, TR(IMU_RATE_NONE), (double)r->rate_dhz / 10.0);
        }
        *ink = UI_TEXT_DIM;
        break;
    case IMS_L_BATTERY:
        if (isfinite(r->battery_v) && r->battery_v <= IMS_BATTERY_MAX_V) {
            snprintf(buf, n, TR(IMU_BATTERY), (double)r->battery_v);
        }
        *ink = UI_TEXT_DIM;
        break;
    case IMS_L_CAL:
        if (ims.v.cal == IMU_CAL_RUNNING) {
            snprintf(buf, n, TR(IMU_CAL_STILL),
                     (unsigned)((ims.v.cal_left_ms + 999u) / 1000u));
        } else if (ims.v.cal == IMU_CAL_DONE) {
            snprintf(buf, n, "%s", TR(IMU_CAL_DONE));
            *ink = UI_OK;
        } else if (ims.v.cal == IMU_CAL_FAILED) {
            snprintf(buf, n, "%s", TR(IMU_CAL_FAILED));
            *ink = UI_WARN;
        }
        break;
    case IMS_L_ERR:
        (void)err_text(buf, n);
        *ink = UI_WARN;
        break;
    case IMS_L_HEAD:
        head_line(buf, n, ink);
        break;
    case IMS_L_HIDDEN:
        if (ims.v.link != IMU_LINK_SCANNING && ims.v.hidden > 0u) {
            snprintf(buf, n, TR(IMU_HIDDEN), (unsigned)ims.v.hidden);
        }
        *ink = UI_TEXT_DIM;
        break;
    default:
        break;
    }
    return buf;
}

const char *imu_screen_line(int i)
{
    static char buf[96];
    if (i < 0 || i >= IMS_L_COUNT) {
        return NULL;
    }
    return ims_line(i, buf, sizeof(buf), NULL);
}

/* ----------------------------------------------------------- the touches */

/**
 * @brief What lies under (@p x, @p y) and takes a press now.
 * @param x The body's x.
 * @param y The body's y.
 * @return A row, IMS_HIT_DEV0 + a device, or IMS_HIT_NONE.
 */
static int hit_at(int x, int y)
{
    for (int row = 0; row < IMS_ROW_COUNT; ++row) {
        if (ims_row_enabled(row)
            && gfx_rect_contains(imu_screen_row_rect(row), x, y)) {
            return row;
        }
    }
    if (ims.list && at_free()) {
        for (int i = 0; i < (int)ims.v.found_n && i < (int)IMU_FOUND_MAX;
             ++i) {
            if (gfx_rect_contains(imu_screen_choice_rect(i), x, y)) {
                return IMS_HIT_DEV0 + i;
            }
        }
    }
    return IMS_HIT_NONE;
}

/**
 * @brief A tap's action.
 * @param hit What the tap landed on.
 */
static void act(int hit)
{
    if (hit >= IMS_HIT_DEV0) {
        post(IMU_CMD_BIND, (uint8_t)(hit - IMS_HIT_DEV0));
        ims.list = false;
        return;
    }
    switch (hit) {
    case IMS_ROW_SCAN:
    case IMS_ROW_RESCAN:
        post(IMU_CMD_SCAN, 0u);
        ims.list = true;
        break;
    case IMS_ROW_ZERO:   post(IMU_CMD_ZERO, 0u);   break;
    case IMS_ROW_UNZERO: post(IMU_CMD_UNZERO, 0u); break;
    case IMS_ROW_CLOSE:  ims.list = false;         break;
    case IMS_ROW_SETTINGS:
        /* SETUP's home tag comes back here (ui_router_parent()). */
        ui_router_goto(SCREEN_SETUP);
        settings_screen_open_on(SET_CAT_SENSORS, SENS_ENTRY_IMU);
        break;
    default:
        break;
    }
}

/** @brief ui_screen_t's reset: nothing pressed, nothing queued. */
static void reset(void)
{
    memset(&ims, 0, sizeof(ims));
    ims.hit = IMS_HIT_NONE;
    ui_hold_reset(&ims.hold);
    s_age_ms = 0u;
}

/** @brief End the press without acting. */
static void drop_press(void)
{
    if (ims.have_press && ims.hit == IMS_ROW_CAL) {
        (void)ui_hold_left(&ims.hold);
    }
    ims.have_press = false;
    ims.hit = IMS_HIT_NONE;
    touched();
}

/**
 * @brief ui_screen_t's event: one press at a time; a tap acts on its
 *        release over what it pressed, the hold when it completes.
 * @param evt The event.
 */
static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (ims.have_press) {
            return;                /* one press at a time */
        }
        ims.hit = hit_at(x, y);
        if (ims.hit != IMS_HIT_NONE) {
            ims.have_press = true;
            ims.press_id = evt->point.id;
            if (ims.hit == IMS_ROW_CAL) {
                ui_hold_begin(&ims.hold, TX_HOLD_HINT_IMU_CAL);
            }
        }
        touched();
        return;
    }
    if (!ims.have_press || evt->point.id != ims.press_id) {
        return;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off the hold abandons it, as on every held control. */
        if (ims.hit == IMS_ROW_CAL
            && !gfx_rect_contains(imu_screen_hold_rect(), x, y)
            && ui_hold_leave(&ims.hold)) {
            ims.have_press = false;
            ims.hit = IMS_HIT_NONE;
            touched();
        }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = ims.hit;
    ims.have_press = false;
    ims.hit = IMS_HIT_NONE;
    touched();
    if (hit == IMS_ROW_CAL) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&ims.hold, evt, imu_screen_hold_rect());
        return;
    }
    if (touch_event_is_tap_up(evt) && hit_at(x, y) == hit) {
        act(hit);
    }
}

/**
 * @brief ui_screen_t's tick: the hold, its flash and the reading's age.
 * @param dt_s The frame's time, s.
 */
static void tick(float dt_s)
{
    drop_hold_if_gone();
    if (ims.v.r.valid) {
        const float ms = dt_s * 1000.0f;
        const uint32_t add = (ms > 0.0f && ms < 60000.0f) ? (uint32_t)ms : 0u;
        s_age_ms = (s_age_ms + add > 99999u) ? 99999u : s_age_ms + add;
    }
    if (ims.hold.down || ims.hold.flash_ms > 0) {
        touched();             /* the fill follows the hold */
    }
    ui_hold_flash_step(&ims.hold, dt_s);
    if (ims_row_enabled(IMS_ROW_CAL) && ui_hold_tick(&ims.hold, dt_s)) {
        post(IMU_CMD_CALIBRATE, 0u);
        ui_hold_reached(&ims.hold);
    }
}

/** @brief ui_screen_t's cancel: the press ends unfired. */
static void cancel(void) { drop_press(); }

/** @brief ui_screen_t's enter: the main view, the hold reset. */
static void enter(void)
{
    ui_hold_reset(&ims.hold);
    ims.list = false;
    touched();
}

/** @brief ui_screen_t's leave: the press and the list end. */
static void leave(void)
{
    drop_press();
    ui_hold_reset(&ims.hold);
    ims.list = false;
}

/** @brief The home tag closes the found list first. @return Whether it did. */
static bool back(void)
{
    if (!ims.list) {
        return false;
    }
    drop_press();
    ims.list = false;
    return true;
}

/** @brief The found list covers the screen. @return Whether it is open. */
static bool covered(void) { return ims.list; }

/** @brief The screen, for the router. */
static const ui_screen_t k_screen = {
    .title   = "IMU",
    .covered = covered,
    .reset   = reset,
    .enter   = enter,
    .leave   = leave,
    .tick    = tick,
    .event   = event,
    .cancel  = cancel,
    .render  = ims_render,
    .back    = back,
};

/** @brief The IMU screen (imu_screen.h). @return The screen. */
const ui_screen_t *imu_screen(void) { return &k_screen; }
