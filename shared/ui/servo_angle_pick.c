/**
 * @file
 * @brief The ANGLE SOURCE list of the SERVO function screens
 *        (servo_angle_pick.h).
 *
 * The list is the screens' choice list (open_choice()), the PORT list's:
 * one column while it holds 5 entries or fewer, two columns of 5 above
 * that.  Its entries, top down, CHOICE_MAX (10) at most:
 *
 *   - `AS5600`; `AS5600: OFF IN SETUP`, refused, while SET_ENC_EN is off.
 *     Its tap opens SETUP > SENSORS on the AS5600 entry: the AS5600 is
 *     the IOmcu's part, switched on there;
 *   - the bound sensor, `name 00:0C:BF:08:19:29 P`, when the last scan
 *     did not list it;
 *   - each sensor of the last scan, `name 00:0C:BF:08:19:29 P -61dBm`: its
 *     name or `NO NAME` cut to AP_NAME_CELLS, its full address, its type (P
 *     public, R random) and its RSSI when the scan heard one.  An entry the
 *     line does not fit, a column of two, takes two lines: the name and the
 *     RSSI over the address and the type.  While no bind is taken (no
 *     sync with the C6, the calibration running, the IMU off) each but the
 *     bound one is refused, its tap naming why;
 *   - `NO IMU FOUND`, refused, when the last scan listed none, or the
 *     scan's failure with its code; `NO OTHER IMU FOUND` while the bound
 *     sensor holds the link (imu_view_bound_linked()): a connected sensor
 *     does not advertise, so the scan cannot list it;
 *   - `SEARCH FOR IMU`; refused with the state while Bluetooth LE starts
 *     for it, without sync with the C6, while a scan runs (`SCANNING:
 *     10s`) and on a C6 that failed.
 *
 * The entry in force is in the accent: the AS5600, or with ANGLE SOURCE
 * IMU the bound sensor.  A pick of the AS5600 sets SET_ANGLE_SOURCE to
 * AS5600; a pick of a sensor binds it when it is not the bound one -- a
 * sensor is its address and its type, public or random --
 * switches the IMU on when it is off and sets SET_ANGLE_SOURCE to IMU; all
 * three save at once and close the list.  SEARCH FOR IMU switches the IMU
 * on when it is off and scans once Bluetooth LE runs (imu_screen_search());
 * the list stays open and follows the scan.
 *
 * TEST's and LIMIT RUN's cards draw an ANGLE row with angle_pick_run_row():
 * the source's name in green while a run reads it, `AS5600 OFF` or `IMU X+
 * NO READING` in amber while it does not.  It is never a refusal: a run
 * without an angle runs without its angle columns.
 *
 * The entries are built from the view the panel hands the IMU screen each
 * frame, whatever screen shows, so the list needs no panel code of its own.
 *
 * RUDDER AUTO refuses the IMU's yaw, AXIS Z, in either ALGORITHM
 * (angle_pick_yaw_why()).  On a BWT901CL on the bench yaw drifts 2.4° in
 * 100s in 6-AXIS, against a tolerance of 0.5°; in 9-AXIS it rose 24° while
 * the rudder moved, fell back and settled 38° off with the servo
 * unpowered.  Roll and pitch, X and Y, hold to gravity in either mode: a
 * sensor turned 90° puts the hinge on X or Y.  The ANGLE row opens
 * the IMU YAW list then (angle_fix_open()), the axes that work and no
 * other: `USE IMU AXIS X (ROLL)` and `USE IMU AXIS Y (PITCH)`.  A pick
 * writes SET_IMU_AXIS and saves, as SETUP > SENSORS does.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_angle_pick.h"

#include <stdio.h>
#include <string.h>

#include "imu.h"
#include "imu_screen.h"
#include "sensors_pane.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_int.h"

/** @name The entries' values
 *  @{ */
#define AP_AS5600 0   /**< the AS5600 */
#define AP_BOUND  1   /**< the bound sensor, not in the last scan */
#define AP_SEARCH 2   /**< SEARCH FOR IMU */
#define AP_NONE   3   /**< NO IMU FOUND, refused */
#define AP_FAIL   4   /**< the scan's failure, refused */
#define AP_IMU0   16  /**< sensor 0 of the last scan; sensor i is + i */
/** @} */

/** @brief A sensor's name, cut to this many cells: with ` -100dBm` it
 *         takes 26 of a two-column entry's 29 cells of 10px, the address
 *         and type on the line under it. */
#define AP_NAME_CELLS 18

/** @brief A sensor entry's address and type, `00:0C:BF:08:19:29 P`, in
 *         characters. */
#define AP_ADDR_LEN 19

/** @brief The entries as last drawn: a hash of their labels, values and
 *         refusals, so a rebuild repaints only when one changed. */
static uint32_t s_sig;

/** @brief The address of each sensor entry as built, and its type below:
 *         a pick binds the sensor of that address and type in the view of
 *         its release, whose found list may have changed under a finger. */
static char s_addr[IMU_FOUND_MAX][IMU_ADDR_MAX];
/** @brief Each sensor entry's address type: 0 public, 1 random. */
static uint8_t s_type[IMU_FOUND_MAX];
/** @brief Where each entry's address starts in its label: 0 on an entry that
 *         names no sensor. */
static uint8_t s_split[CHOICE_MAX];

/**
 * @brief Whether the bound sensor is scanned device @p d.  The bound
 *        address carries its type after a space ("00:0C:BF:08:19:29 P",
 *        "... R"); a public and a random address of the same 48 bits are
 *        two devices.
 * @param bound The bound address, "" for none.
 * @param d     A device of the scan: its address, colons, no type, and its
 *              type.
 * @return true for the same address and type.
 */
static bool same_dev(const char *bound, const imu_dev_t *d)
{
    const size_t n = strlen(d->addr);
    return n == 17u && strlen(bound) == 19u && bound[17] == ' '
           && strncmp(bound, d->addr, n) == 0
           && bound[18] == (d->addr_type == 1u ? 'R' : 'P');
}

/**
 * @brief The index of the sensor at @p addr of type @p type in the view's
 *        found list.
 * @param v    The IMU's view.
 * @param addr The address, colons, no type.
 * @param type Its type: 0 public, 1 random.
 * @return The index; -1 when the list holds none of that address and type.
 */
static int find_sensor(const imu_view_t *v, const char *addr, uint8_t type)
{
    for (int i = 0; i < (int)v->found_n && i < (int)IMU_FOUND_MAX; ++i) {
        if (addr[0] != '\0' && strcmp(v->found[i].addr, addr) == 0
            && v->found[i].addr_type == type) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Why no sensor of the scan can be bound now: the calibration runs,
 *        the IMU is off, or Bluetooth LE does not run free of a scan.
 * @param v The IMU's view.
 * @return The reason for the alert band; NULL while a bind is taken.
 */
static const char *bind_why(const imu_view_t *v)
{
    static char word[48];
    if (imu_screen_bind_free()) {
        return NULL;
    }
    if (v->cal == IMU_CAL_RUNNING) {
        return TR(SV_ANG_BIND_CAL);
    }
    if (v->link == IMU_LINK_OFF) {
        return TR(SV_ANG_BIND_OFF);
    }
    return sensors_rows_imu_word(v->link, 0u, word, sizeof(word));
}

/**
 * @brief The length of @p t, at most @p max characters read.
 * @param t   The text.
 * @param max The most characters read.
 * @return The characters before the NUL, @p max at most.
 */
static size_t text_len(const char *t, size_t max)
{
    size_t n = 0u;
    while (n < max && t[n] != '\0') {
        ++n;
    }
    return n;
}

/**
 * @brief A sensor's entry: its name or NO NAME, cut, its full address, its
 *        type and its RSSI when known: `HC-06 00:0C:BF:08:19:29 P -60dBm`.
 * @param name Its name, "" for none.
 * @param addr Its address, colons; anything after the 17th character is
 *             not read.
 * @param type Its type: 'P' public, 'R' random.
 * @param rssi Its RSSI, dBm; 127 unknown.
 * @param out  Takes the entry.
 * @param n    @p out's size.
 * @return Where the address starts in @p out.
 */
static uint8_t sensor_label(const char *name, const char *addr, char type,
                            int8_t rssi, char *out, size_t n)
{
    const char *nm = (name[0] != '\0') ? name : TR(IMU_NO_NAME);
    const int cut = (int)gfx_text_prefix(nm, AP_NAME_CELLS);
    const int alen = (int)text_len(addr, 17u);
    if (rssi != 127) {
        snprintf(out, n, "%.*s %.*s %c %ddBm", cut, nm, alen, addr, type,
                 (int)rssi);
    } else {
        snprintf(out, n, "%.*s %.*s %c", cut, nm, alen, addr, type);
    }
    /* The address follows the name's bytes and a space. */
    const size_t at = strlen(nm) < (size_t)cut ? strlen(nm) : (size_t)cut;
    return (uint8_t)(at + 1u);
}

/**
 * @brief An entry added, refused or not.
 * @param label   Its text, the reason on a refused one.
 * @param value   Its AP_* value.
 * @param refused Whether it takes no pick.
 */
static void add(const char *label, int value, bool refused)
{
    const int before = s.ch.count;
    choice_add(label, value);
    if (refused && s.ch.count > before) {
        s.ch.refused = (uint16_t)(s.ch.refused | (1u << (s.ch.count - 1)));
    }
}

/**
 * @brief SEARCH FOR IMU's entry: offered, or refused with what it waits
 *        on.
 * @param v The IMU's view.
 */
static void add_search(const imu_view_t *v)
{
    if (v->link == IMU_LINK_C6_FAILED || v->link == IMU_LINK_NO_BT) {
        char word[48];
        add(sensors_rows_imu_word(v->link, 0u, word, sizeof(word)),
            AP_SEARCH, true);
    } else if (v->link == IMU_LINK_SCANNING) {
        add(TR(IMU_SCANNING), AP_SEARCH, true);
    } else if (imu_screen_search_owed()) {
        /* Bluetooth LE starts for it: the state word until it runs. */
        add(TR(IMU_ST_STARTING), AP_SEARCH, true);
    } else {
        add(TR(SV_ANG_SEARCH), AP_SEARCH, false);
    }
}

/** @brief The entries rebuilt from the settings and the IMU's view; the
 *         title and the list's place kept. */
static void build(void)
{
    const imu_view_t *v = imu_screen_view();
    s.ch.count = 0;
    s.ch.refused = 0u;
    memset(s_split, 0, sizeof(s_split));
    if (settings_get_bool(SET_ENC_EN)) {
        add("AS5600", AP_AS5600, false);
    } else {
        add(TR(SV_ANG_AS5600_OFF), AP_AS5600, true);
    }
    const int found = (v->found_n > IMU_FOUND_MAX) ? (int)IMU_FOUND_MAX
                                                    : (int)v->found_n;
    bool listed = false;
    for (int i = 0; i < found; ++i) {
        listed = listed || same_dev(v->bound_addr, &v->found[i]);
    }
    char line[64];
    if (v->bound_addr[0] != '\0' && !listed) {
        const char bound_type = strlen(v->bound_addr) == 19u
                                    ? v->bound_addr[18] : '?';
        const uint8_t at = sensor_label(v->bound_name, v->bound_addr,
                                        bound_type,
                                        127, line, sizeof(line));
        add(line, AP_BOUND, false);
        s_split[s.ch.count - 1] = at;
    }
    /* SEARCH FOR IMU keeps the last place: 1 + 8 + 1 fill CHOICE_MAX, and
     * a bound sensor beside 8 found drops the eighth. */
    const int room = CHOICE_MAX - 1 - s.ch.count;
    /* While no bind is taken a sensor other than the bound one is refused,
     * its line kept; a tap names why in the band. */
    const bool bind_off = bind_why(v) != NULL;
    for (int i = 0; i < found && i < room; ++i) {
        const uint8_t at = sensor_label(
            v->found[i].name, v->found[i].addr,
            v->found[i].addr_type == 1u ? 'R' : 'P', v->found[i].rssi, line,
            sizeof(line));
        add(line, AP_IMU0 + i,
            bind_off && !same_dev(v->bound_addr, &v->found[i]));
        s_split[s.ch.count - 1] = at;
        snprintf(s_addr[i], sizeof(s_addr[i]), "%s", v->found[i].addr);
        s_type[i] = v->found[i].addr_type;
    }
    const bool idle = v->link != IMU_LINK_SCANNING
                      && !imu_screen_search_owed();
    if (found == 0 && idle && v->err == IMU_ERR_NONE_FOUND) {
        /* A connected sensor does not advertise: the scan cannot list
         * the bound one, which stays above. */
        add(imu_view_bound_linked(v) ? TR(SV_ANG_NONE_OTHER)
                                     : TR(SV_ANG_NONE),
            AP_NONE, true);
    } else if (found == 0 && idle && v->err == IMU_ERR_SCAN) {
        snprintf(line, sizeof(line), TR(SV_ANG_SCAN_FAIL),
                 (unsigned)v->err_code);
        add(line, AP_FAIL, true);
    }
    add_search(v);
}

/** @brief A hash of the entries as built.  @return the hash */
static uint32_t entries_sig(void)
{
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)s.ch.count) * 16777619u;
    h = (h ^ s.ch.refused) * 16777619u;
    for (int k = 0; k < s.ch.count; ++k) {
        h = (h ^ (uint32_t)s.ch.values[k]) * 16777619u;
        for (const char *p = s.ch.labels[k]; *p != '\0'; ++p) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
    }
    return h;
}

/** @brief The list repainted with its entries' new hash kept. */
static void repaint(void)
{
    s_sig = entries_sig();
    servo_invalidate();
    ++s.ctrl_rev;
}

/** @brief The IMU switched on in SETUP when it is off, and saved. */
static void imu_on(void)
{
    if (!settings_get_bool(SET_IMU_EN)) {
        settings_set(SET_IMU_EN, 1.0f);
        settings_request_save();
    }
}

/**
 * @brief SET_ANGLE_SOURCE set and saved.
 * @param src SERVO_ANGLE_AS5600 or SERVO_ANGLE_IMU.
 */
static void set_source(servo_angle_src_t src)
{
    if (settings_get_int(SET_ANGLE_SOURCE) != (int)src) {
        settings_set(SET_ANGLE_SOURCE, (float)src);
        settings_request_save();
    }
}

void angle_pick_open(void)
{
    const char *why = svs_list_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    close_panels();
    open_choice(CH_ANGLE, SETTING_COUNT, TR(SV_ANG_TITLE));
    /* Nothing bound and nothing scanned yet: the list fills by itself.  A
     * bound sensor is not scanned under: the user's SEARCH does that. */
    const imu_view_t *v = imu_screen_view();
    if (v->link == IMU_LINK_READY && v->found_n == 0u
        && v->err == IMU_ERR_NONE) {
        (void)imu_screen_search();
    }
    build();
    repaint();
}

void angle_pick_follow(void)
{
    if (!s.ch.open || s.ch.target != CH_ANGLE) {
        return;
    }
    if (svs_list_bench_why() != NULL) {
        s.ch.open = false;
        repaint();
        return;
    }
    /* Under a finger the entries stay: its release picks what it pressed. */
    if (s.ov_have && s.ov_pressed == OP_CHOICE) {
        return;
    }
    build();
    if (entries_sig() != s_sig) {
        repaint();
    }
}

void angle_pick_choose(int value)
{
    if (svs_list_bench_why() != NULL) {
        return;
    }
    const imu_view_t *v = imu_screen_view();
    if (value == AP_AS5600) {
        set_source(SERVO_ANGLE_AS5600);
    } else if (value == AP_SEARCH) {
        (void)imu_screen_search();
        open_choice(CH_ANGLE, SETTING_COUNT, TR(SV_ANG_TITLE));
        build();
    } else if (value == AP_BOUND) {
        imu_on();
        set_source(SERVO_ANGLE_IMU);
    } else if (value >= AP_IMU0 && value - AP_IMU0 < (int)IMU_FOUND_MAX) {
        const int k = value - AP_IMU0;
        const int i = find_sensor(v, s_addr[k], s_type[k]);
        if (i >= 0 && !same_dev(v->bound_addr, &v->found[i])
            && !imu_screen_bind((uint8_t)i)) {
            /* No bind taken now: nothing changes, the list stays, the
             * band names why. */
            const char *why = bind_why(v);
            ui_router_set_alert(why != NULL ? why : TR(SV_ANG_BIND_OFF));
            open_choice(CH_ANGLE, SETTING_COUNT, TR(SV_ANG_TITLE));
            build();
        } else if (i >= 0) {
            imu_on();
            set_source(SERVO_ANGLE_IMU);
        }
    }
    repaint();
}

void angle_pick_refused(int k)
{
    if (k < 0 || k >= s.ch.count) {
        return;
    }
    if (s.ch.values[k] == AP_AS5600) {
        /* SETUP's home tag comes back here (ui_router_parent()). */
        s.ch.open = false;
        ui_router_goto(SCREEN_SETUP);
        settings_screen_open_on(SET_CAT_SENSORS, SENS_ENTRY_AS5600);
        return;
    }
    if (s.ch.values[k] >= AP_IMU0) {
        /* A sensor while no bind is taken: why. */
        const char *why = bind_why(imu_screen_view());
        ui_router_set_alert(why != NULL ? why : s.ch.labels[k]);
        return;
    }
    if (s.ch.values[k] == AP_NONE) {
        ui_router_set_alert(imu_view_bound_linked(imu_screen_view())
                                ? TR(IMU_ERR_NONE_OTHER)
                                : TR(IMU_ERR_NONE_FOUND));
        return;
    }
    ui_router_set_alert(s.ch.labels[k]);
}

bool angle_pick_current(int k)
{
    if (k < 0 || k >= s.ch.count) {
        return false;
    }
    const int value = s.ch.values[k];
    if (enc_src() == SERVO_ANGLE_AS5600) {
        return value == AP_AS5600;
    }
    const imu_view_t *v = imu_screen_view();
    if (value == AP_BOUND) {
        return true;
    }
    return value >= AP_IMU0 && value - AP_IMU0 < (int)v->found_n
           && same_dev(v->bound_addr, &v->found[value - AP_IMU0]);
}

/** @name The IMU YAW list's values
 *  @{ */
#define AF_AXIS_X 1   /**< AXIS X, roll */
#define AF_AXIS_Y 2   /**< AXIS Y, pitch */
/** @} */

const char *angle_pick_yaw_why(void)
{
    /* Yaw does not follow the surface in either ALGORITHM: 6-AXIS drifts
     * without the magnetometer; 9-AXIS settled 38° off on the bench, the
     * servo's magnetic field suspected. */
    return (enc_src() == SERVO_ANGLE_IMU && s.imu_axis == (uint8_t)IMU_AXIS_Z)
               ? TR(RA_IMU_YAW) : NULL;
}

void angle_fix_open(void)
{
    const char *why = svs_list_refused();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    close_panels();
    open_choice(CH_ANGLE_FIX, SETTING_COUNT, TR(SV_YAW_TITLE));
    choice_add(TR(SV_YAW_X), AF_AXIS_X);
    choice_add(TR(SV_YAW_Y), AF_AXIS_Y);
    servo_invalidate();
    ++s.ctrl_rev;
}

void angle_fix_choose(int value)
{
    if (svs_list_bench_why() != NULL) {
        return;
    }
    if (value != AF_AXIS_X && value != AF_AXIS_Y) {
        return;
    }
    const uint8_t axis = value == AF_AXIS_X ? (uint8_t)IMU_AXIS_X
                                            : (uint8_t)IMU_AXIS_Y;
    settings_set(SET_IMU_AXIS, (float)axis);
    /* What the panel's settings observer does too: the runs' axis. */
    servo_screen_set_imu_axis(axis, settings_get_int(SET_IMU_SIGN) == 1);
    settings_request_save();
    servo_invalidate();
    ++s.ctrl_rev;
}

void angle_pick_run_row(rdy_row_t *r, const char *name, bool reads)
{
    r->label = TR(RDY_ANGLE);
    r->fix = true;
    if (reads) {
        r->st = (uint8_t)RDY_OK;
        snprintf(r->value, sizeof(r->value), "%s", name);
        return;
    }
    /* Amber: the run goes without the angle columns, refused by nothing. */
    r->st = (uint8_t)RDY_WARN;
    const bool off = enc_src() == SERVO_ANGLE_AS5600
                     && !settings_get_bool(SET_ENC_EN);
    snprintf(r->value, sizeof(r->value),
             off ? TR(SV_ANG_RUN_OFF) : TR(SV_ANG_RUN_NONE), name);
}

bool angle_pick_draw_entry(gfx_canvas_t *c, int k, gfx_color_t fill,
                           bool refused, bool down)
{
    if (k < 0 || k >= s.ch.count || s_split[k] == 0u) {
        return false;
    }
    const gfx_rect_t r = choice_rect(k);
    const char *label = s.ch.labels[k];
    /* One line while it fits with 10px each side, a column of one. */
    if (gfx_text_width(SV_FONT_LABEL, label, 1) <= r.w - 20) {
        return false;
    }
    const size_t at = s_split[k];
    const char *addr = label + at;
    const size_t alen = text_len(addr, AP_ADDR_LEN);
    char top[sizeof(s.ch.labels[0])];
    char low[AP_ADDR_LEN + 1];
    /* The name and the RSSI over the address and the type. */
    snprintf(top, sizeof(top), "%.*s%s", (int)(at - 1u), label, addr + alen);
    snprintf(low, sizeof(low), "%.*s", (int)alen, addr);
    ui_button_font(c, r, NULL, fill, down, true, SV_FONT_LABEL);
    const gfx_color_t ink = refused ? ui_theme_color(UI_C_WARN)
                            : ui_is_light(fill) ? UI_TEXT_ON_LIGHT
                                                : UI_TEXT;
    /* 20px over 16px with 2px between: 5px above and below in 48px. */
    const int y0 = r.y + (r.h - 38) / 2;
    gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)y0, r.w, 20 }, top,
                SV_FONT_LABEL, ink, 1, GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ r.x, (int16_t)(y0 + 22), r.w, 16 }, low,
                &gfx_font_8x16, ink, 1, GFX_ALIGN_CENTER);
    return true;
}

int angle_pick_count(void)
{
    return (s.ch.open
            && (s.ch.target == CH_ANGLE || s.ch.target == CH_ANGLE_FIX))
               ? s.ch.count : -1;
}

const char *angle_pick_label(int i, bool *refused)
{
    const bool in = i >= 0 && i < angle_pick_count();
    if (refused != NULL) {
        *refused = in && (s.ch.refused & (1u << i)) != 0u;
    }
    return in ? s.ch.labels[i] : "";
}

gfx_rect_t angle_pick_rect(int i) { return choice_rect(i); }
