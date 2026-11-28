/**
 * @file
 * @brief The Bluetooth IMU's view and commands (imu_view.h).
 *
 * The model commands nothing itself but IMU ON in SETUP: a call queues an
 * imu_cmd_t that the panel hands to the IMU's task (imu_ctl.h), and the
 * panel hands back what to show each frame (imu_view_set()).  A search
 * with the IMU off switches it on and waits for Bluetooth LE to run before
 * its SCAN goes out.
 *
 * SPDX-License-Identifier: MIT
 */

#include "imu_view.h"

#include <stddef.h>
#include <string.h>

/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "settings.h"
#include "ui_screen.h"

/** @brief Commands the model holds until the panel takes them. */
#define IMV_CMDS 4

/** @brief The model's state. */
typedef struct {
    imu_view_t v;              /**< what the panel last handed over */
    imu_cmd_t  cmds[IMV_CMDS]; /**< commands not yet taken, oldest first */
    uint8_t    cmd_n;          /**< how many */
    bool       scan_owed;      /**< a search waits for Bluetooth LE to run
                                    before its SCAN is queued */
    uint32_t   age_ms;         /**< the time since the newest angle frame
                                    reached the panel, ms */
    uint32_t   rev;            /**< views that changed more than the
                                    reading, counted */
    uint32_t   read_rev;       /**< views that changed the reading alone */
} imv_state_t;

/** @brief The state. */
static imv_state_t imv;

void imu_view_reset(void)
{
    memset(&imv, 0, sizeof(imv));
}

/**
 * @brief Bluetooth runs and no scan does: a scan or a bind may start.
 * @return Whether SCAN and a BIND are taken.
 */
static bool at_free(void)
{
    if (imv.v.cal == IMU_CAL_RUNNING) {
        return false;         /* the calibration keeps the sensor bound */
    }
    switch (imv.v.link) {
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

/**
 * @brief Queue @p kind for the panel; a full queue drops the oldest.
 * @param kind The command.
 * @param arg  Its argument.
 */
static void post(imu_cmd_kind_t kind, uint8_t arg)
{
    if (imv.cmd_n >= IMV_CMDS) {
        memmove(&imv.cmds[0], &imv.cmds[1],
                (IMV_CMDS - 1u) * sizeof(imv.cmds[0]));
        imv.cmd_n = IMV_CMDS - 1u;
    }
    imv.cmds[imv.cmd_n].kind = kind;
    imv.cmds[imv.cmd_n].arg = arg;
    ++imv.cmd_n;
}

/**
 * @brief A search's SCAN, owed since Bluetooth LE was not running: queued
 *        once it runs and nothing scans or calibrates; dropped by IMU OFF,
 *        a C6 that failed, Bluetooth without sync with the C6, a bench
 *        that is ARMED or a scan that runs anyway.
 */
static void search_follow(void)
{
    if (!imv.scan_owed) {
        return;
    }
    if (!settings_get_bool(SET_IMU_EN) || imv.v.link == IMU_LINK_C6_FAILED
        || imv.v.link == IMU_LINK_NO_BT || imv.v.link == IMU_LINK_SCANNING
        || ui_router_status()->armed) {
        imv.scan_owed = false;
        ++imv.rev;
        return;
    }
    if (at_free()) {
        post(IMU_CMD_SCAN, 0u);
        imv.scan_owed = false;
        ++imv.rev;
    }
}

/**
 * @brief The band's word on a calibration that ended: DONE as a notice,
 *        FAILED as an alert.
 * @param was The calibration's state before the view.
 * @param now Its state in the view.
 */
static void cal_said(imu_cal_t was, imu_cal_t now)
{
    if (was != IMU_CAL_RUNNING || now == IMU_CAL_RUNNING) {
        return;
    }
    if (now == IMU_CAL_DONE) {
        ui_router_set_notice(TR(IMU_CAL_DONE));
    } else if (now == IMU_CAL_FAILED) {
        ui_router_set_alert(TR(IMU_CAL_FAILED));
    }
}

void imu_view_set(const imu_view_t *v)
{
    if (v == NULL) {
        return;
    }
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (memcmp(v, &imv.v, sizeof(imv.v)) == 0) {
        /* The same view, every frame: the bench's switch to ARMED still
         * ends a search that waits for Bluetooth LE. */
        search_follow();
        return;
    }
    if (v->r.taken_ms != imv.v.r.taken_ms || !v->r.valid) {
        imv.age_ms = 0u;
    }
    _Static_assert(offsetof(imu_view_t, r) + sizeof(imu_reading_t)
                       == sizeof(imu_view_t),
                   "the reading is the view's last member");
    /* The reading alone, the view's last member: the live rows repaint at
     * their own pace (wireless_imu_tick()). */
    const bool alone = memcmp(v, &imv.v, offsetof(imu_view_t, r)) == 0;
    /* A SCAN the controller refused after the model passed it on: asked
     * while an arm waits, before the band reads ARMED. */
    const bool armed_no = v->scan_armed != imv.v.scan_armed;
    const imu_cal_t was = imv.v.cal;
    imv.v = *v;
    if (armed_no) {
        ui_router_set_alert(TR(IMU_SCAN_ARMED));
    }
    cal_said(was, imv.v.cal);
    search_follow();
    if (alone) {
        ++imv.read_rev;
    } else {
        ++imv.rev;
    }
}

bool imu_view_take(imu_cmd_t *out)
{
    if (out == NULL || imv.cmd_n == 0u) {
        return false;
    }
    *out = imv.cmds[0];
    --imv.cmd_n;
    memmove(&imv.cmds[0], &imv.cmds[1], imv.cmd_n * sizeof(imv.cmds[0]));
    return true;
}

const imu_view_t *imu_view_get(void) { return &imv.v; }

bool imu_view_bind(uint8_t i)
{
    if (i >= imv.v.found_n || i >= IMU_FOUND_MAX || !at_free()) {
        return false;
    }
    post(IMU_CMD_BIND, i);
    return true;
}

bool imu_view_bind_free(void) { return at_free(); }

bool imu_view_zero(void)
{
    if (!imv.v.r.valid) {
        return false;
    }
    post(IMU_CMD_ZERO, 0u);
    return true;
}

bool imu_view_unzero(void)
{
    if (!imv.v.r.zeroed) {
        return false;
    }
    post(IMU_CMD_UNZERO, 0u);
    return true;
}

bool imu_view_cal_free(void)
{
    return imv.v.link == IMU_LINK_CONNECTED && imv.v.cal != IMU_CAL_RUNNING;
}

bool imu_view_calibrate(void)
{
    if (!imu_view_cal_free()) {
        return false;
    }
    post(IMU_CMD_CALIBRATE, 0u);
    return true;
}

bool imu_view_bound_linked(const imu_view_t *v)
{
    return v != NULL && v->bound_addr[0] != '\0'
           && (v->link == IMU_LINK_BOUND || v->link == IMU_LINK_CONNECTED
               || v->link == IMU_LINK_NO_FRAMES);
}

bool imu_view_search(void)
{
    if (ui_router_status()->armed) {
        /* As the Dial's and the node's scans: none while ARMED.  One
         * asked while an arm waits passes here; the controller refuses
         * it and imu_view_set() says so on the band. */
        ui_router_set_alert(TR(IMU_SCAN_ARMED));
        return false;
    }
    if (imv.v.link == IMU_LINK_C6_FAILED || imv.v.link == IMU_LINK_NO_BT
        || imv.v.link == IMU_LINK_SCANNING) {
        return false;
    }
    if (!settings_get_bool(SET_IMU_EN)) {
        settings_set(SET_IMU_EN, 1.0f);
        settings_request_save();
    }
    imv.scan_owed = true;
    ++imv.rev;
    search_follow();
    return true;
}

bool imu_view_search_owed(void) { return imv.scan_owed; }

uint32_t imu_view_age_ms(void) { return imv.age_ms; }

void imu_view_tick(float dt_s)
{
    if (!imv.v.r.valid) {
        return;
    }
    const float ms = dt_s * 1000.0f;
    const uint32_t add = (ms > 0.0f && ms < 60000.0f) ? (uint32_t)ms : 0u;
    imv.age_ms = (imv.age_ms + add > 99999u) ? 99999u : imv.age_ms + add;
}

uint32_t imu_view_rev(void) { return imv.rev; }

uint32_t imu_view_read_rev(void) { return imv.read_rev; }
