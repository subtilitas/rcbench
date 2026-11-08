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
 *   - the bound sensor, `name 19:29`, when the last scan did not list it;
 *   - each sensor of the last scan, `name 19:29 -61dBm`: its name or `NO
 *     NAME` cut to AP_NAME_CELLS, the last 5 characters of its address
 *     and its RSSI when the scan heard one;
 *   - `NO IMU FOUND`, refused, when the last scan listed none, or the
 *     scan's failure with its code;
 *   - `SEARCH FOR IMU`; refused with the state while Bluetooth LE starts
 *     for it, without sync with the C6, while a scan runs (`SCANNING:
 *     10s`) and on a C6 that failed.
 *
 * The entry in force is in the accent: the AS5600, or with ANGLE SOURCE
 * IMU the bound sensor.  A pick of the AS5600 sets SET_ANGLE_SOURCE to
 * AS5600; a pick of a sensor binds it when it is not the bound one,
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
 * SPDX-License-Identifier: MIT
 */

#include "servo_angle_pick.h"

#include <stdio.h>
#include <string.h>

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

/** @brief A sensor's name, cut to this many cells: with ` 19:29 -100dBm`
 *         it takes 26 of a two-column entry's 29. */
#define AP_NAME_CELLS 12

/** @brief The entries as last drawn: a hash of their labels, values and
 *         refusals, so a rebuild repaints only when one changed. */
static uint32_t s_sig;

/** @brief The address of each sensor entry as built: a pick binds the
 *         sensor of that address in the view of its release, whose found
 *         list may have changed under a finger. */
static char s_addr[IMU_FOUND_MAX][IMU_ADDR_MAX];

/**
 * @brief Whether the bound sensor's address, which carries its type after
 *        a space ("00:0C:BF:08:19:29 P"), is @p found's.
 * @param bound The bound address, "" for none.
 * @param found A scanned address, colons, no type.
 * @return true for the same address.
 */
static bool same_addr(const char *bound, const char *found)
{
    const size_t n = strcspn(bound, " ");
    return n > 0u && strlen(found) == n && strncmp(bound, found, n) == 0;
}

/**
 * @brief The index of the sensor at @p addr in the view's found list.
 * @param v    The IMU's view.
 * @param addr The address, colons, no type.
 * @return The index; -1 when the list holds none of that address.
 */
static int find_sensor(const imu_view_t *v, const char *addr)
{
    for (int i = 0; i < (int)v->found_n && i < (int)IMU_FOUND_MAX; ++i) {
        if (addr[0] != '\0' && strcmp(v->found[i].addr, addr) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief A sensor's entry: its name or NO NAME, cut, the last 5
 *        characters of its address, and its RSSI when known.
 * @param name Its name, "" for none.
 * @param addr Its address, a type after a space allowed.
 * @param rssi Its RSSI, dBm; 127 unknown.
 * @param out  Takes the entry.
 * @param n    @p out's size.
 */
static void sensor_label(const char *name, const char *addr, int8_t rssi,
                         char *out, size_t n)
{
    const char *nm = (name[0] != '\0') ? name : TR(IMU_NO_NAME);
    const int cut = (int)gfx_text_prefix(nm, AP_NAME_CELLS);
    const size_t len = strcspn(addr, " ");
    const char *tail = addr + ((len > 5u) ? len - 5u : 0u);
    const int tlen = (int)((len > 5u) ? 5u : len);
    if (rssi != 127) {
        snprintf(out, n, "%.*s %.*s %ddBm", cut, nm, tlen, tail, (int)rssi);
    } else {
        snprintf(out, n, "%.*s %.*s", cut, nm, tlen, tail);
    }
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
    if (settings_get_bool(SET_ENC_EN)) {
        add("AS5600", AP_AS5600, false);
    } else {
        add(TR(SV_ANG_AS5600_OFF), AP_AS5600, true);
    }
    const int found = (v->found_n > IMU_FOUND_MAX) ? (int)IMU_FOUND_MAX
                                                    : (int)v->found_n;
    bool listed = false;
    for (int i = 0; i < found; ++i) {
        listed = listed || same_addr(v->bound_addr, v->found[i].addr);
    }
    char line[64];
    if (v->bound_addr[0] != '\0' && !listed) {
        sensor_label(v->bound_name, v->bound_addr, 127, line, sizeof(line));
        add(line, AP_BOUND, false);
    }
    /* SEARCH FOR IMU keeps the last place: 1 + 8 + 1 fill CHOICE_MAX, and
     * a bound sensor beside 8 found drops the eighth. */
    const int room = CHOICE_MAX - 1 - s.ch.count;
    for (int i = 0; i < found && i < room; ++i) {
        sensor_label(v->found[i].name, v->found[i].addr, v->found[i].rssi,
                     line, sizeof(line));
        add(line, AP_IMU0 + i, false);
        snprintf(s_addr[i], sizeof(s_addr[i]), "%s", v->found[i].addr);
    }
    const bool idle = v->link != IMU_LINK_SCANNING
                      && !imu_screen_search_owed();
    if (found == 0 && idle && v->err == IMU_ERR_NONE_FOUND) {
        add(TR(SV_ANG_NONE), AP_NONE, true);
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
        const int i = find_sensor(v, s_addr[value - AP_IMU0]);
        if (i >= 0) {
            if (!same_addr(v->bound_addr, v->found[i].addr)) {
                (void)imu_screen_bind((uint8_t)i);
            }
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
    ui_router_set_alert(s.ch.values[k] == AP_NONE ? TR(IMU_ERR_NONE_FOUND)
                                                  : s.ch.labels[k]);
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
           && same_addr(v->bound_addr, v->found[value - AP_IMU0].addr);
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

int angle_pick_count(void)
{
    return (s.ch.open && s.ch.target == CH_ANGLE) ? s.ch.count : -1;
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
