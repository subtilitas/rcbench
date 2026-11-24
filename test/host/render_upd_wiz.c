/**
 * @file render_upd_wiz.c
 * @brief UPDATES' step views. See render_upd_wiz.h.
 *
 * Every view starts from a bench on 0.22.0 with every step current: a
 * check that found nothing newer, the IOmcu on the carried build, the C6
 * on the carried esp_hosted 2.13.0, the Dial on the carried 0.22.0, the
 * power node bound and not connected, no balance node bound.  Each view
 * changes what its step needs.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_upd_wiz.h"
#include "render_versions.h"

#include <stdio.h>
#include <string.h>

#include "touch_types.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_fetch.h"
#include "upd_screen.h"
#include "upd_wizard.h"

/** @brief What one view hands UPDATES. */
typedef struct {
    upd_view_t     p;                  /**< the panel's view */
    upd_cu_view_t  cu;                 /**< the IOmcu section */
    upd_c6_view_t  c6;                 /**< the C6 row */
    upd_dev_view_t d[UPD_DEV_COUNT];   /**< the device rows */
    bool           run;                /**< an update run stands */
} wiz_pose_t;

/** @brief The view's name begins with @p prefix. */
static bool starts(const char *view, const char *prefix)
{
    return strncmp(view, prefix, strlen(prefix)) == 0;
}

bool render_upd_wiz_view(const char *view)
{
    return starts(view, "updates-step-") || starts(view, "updates-wiz-")
           || starts(view, "updates-run-")
           || strcmp(view, "updates-offer") == 0
           || strcmp(view, "updates-held") == 0
           || strcmp(view, "updates-done") == 0;
}

/** @brief A tap of finger 1 at body (@p x, @p y), through the router. */
static void body_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)(UI_BAND_H + y),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief An example version as the IOmcu's. */
static cu_version_t cu_ex(ex_ver_t which)
{
    const ex_version_t v = ex_version(which);
    return (cu_version_t){ (uint16_t)v.n[0], (uint16_t)v.n[1],
                           (uint16_t)v.n[2] };
}

/** @brief An example version as a wireless device's. */
static fw_version_t fw_ex(ex_ver_t which)
{
    const ex_version_t v = ex_version(which);
    return (fw_version_t){ (uint16_t)v.n[0], (uint16_t)v.n[1],
                           (uint16_t)v.n[2] };
}

/** @brief Every step current, as the file comment says. */
static void base(wiz_pose_t *w)
{
    memset(w, 0, sizeof(*w));
    w->p.have_net = true;
    strcpy(w->p.net, "Workshop 2.4");
    strcpy(w->p.running, ex_version(EX_VER_RUNNING).text);
    w->p.job = UPD_JOB_CHECK;
    w->p.phase = UPD_PH_ENDED;
    w->p.got = UPD_GOT_CURRENT;
    strcpy(w->p.found, ex_version(EX_VER_RUNNING).text);
    w->cu = (upd_cu_view_t){ .sec = CU_SEC_SAME, .known = true,
                             .have_image = true,
                             .running = cu_ex(EX_VER_RUNNING),
                             .carried = cu_ex(EX_VER_RUNNING) };
    w->c6 = (upd_c6_view_t){ .row = FW_C6_ROW_REINSTALL, .known = true,
                             .have_image = true,
                             .running = { 2u, 13u, 0u },
                             .carried = { 2u, 13u, 0u } };
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        upd_dev_view_t *d = &w->d[i];
        d->have_image = true;
        d->carried = fw_ex(EX_VER_RUNNING);
        d->row = DEVUPD_ROW_NOT_LINKED;
    }
    upd_dev_view_t *dial = &w->d[UPD_DEV_DIAL];
    dial->row = DEVUPD_ROW_REINSTALL;
    dial->bound = true;
    dial->known = true;
    dial->running = dial->carried;
    w->d[UPD_DEV_PNODE].bound = true;
    /* Each hold downloads its device's file of the release. */
    snprintf(w->c6.fetch.file, sizeof(w->c6.fetch.file),
             "rcbench-c6-2.13.0.bin");
    static const char *const k_file[UPD_DEV_COUNT] = {
        "rcbench-dial-0.22.0.bin", "rcbench-node-pwr-0.22.0.bin",
        "rcbench-node-bal-0.22.0.bin",
    };
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        snprintf(w->d[i].fetch.file, sizeof(w->d[i].fetch.file), "%s",
                 k_file[i]);
    }
}

/** @brief The Dial connected on 0.21.0: its step OPEN. */
static void dial_behind(wiz_pose_t *w)
{
    upd_dev_view_t *dial = &w->d[UPD_DEV_DIAL];
    dial->row = DEVUPD_ROW_READY;
    dial->running = ex_versions_set() ? fw_ex(EX_VER_OLDER)
                    : (fw_version_t){ 0u, 21u, 0u };
}

/** @brief The power node connected on 0.21.0: its step OPEN. */
static void pnode_behind(wiz_pose_t *w)
{
    upd_dev_view_t *pn = &w->d[UPD_DEV_PNODE];
    pn->row = DEVUPD_ROW_READY;
    pn->known = true;
    pn->running = ex_versions_set() ? fw_ex(EX_VER_OLDER)
                  : (fw_version_t){ 0u, 21u, 0u };
}

/** @brief The IOmcu on 0.21.2 in @p sec. */
static void cu_behind(wiz_pose_t *w, cu_sec_t sec)
{
    w->cu.sec = sec;
    w->cu.running = cu_ex(EX_VER_OLDER);
}

/**
 * @brief The views of the steps' committed shots: 3.1 to 3.4 of the plan
 *        and the states around them.
 * @param view the view's name
 * @param w    the pose, from base()
 * @return true when @p view is one of them
 */
static bool shot_views(const char *view, wiz_pose_t *w)
{
    if (strcmp(view, "updates-step-iomcu") == 0
        || strcmp(view, "updates-run-iomcu") == 0
        || strcmp(view, "updates-offer") == 0) {
        /* updates-run-iomcu: the same, opened by the update run after
         * the panel's restart; updates-offer: opened by a link-up's
         * offer, no check since the start. */
        cu_behind(w, CU_SEC_READY);
        dial_behind(w);
        if (strcmp(view, "updates-step-iomcu") != 0) {
            w->run = strcmp(view, "updates-run-iomcu") == 0;
            /* No check in a run: PANEL reads NOT CHECKED. */
            w->p.phase = UPD_PH_IDLE;
            w->p.got = UPD_GOT_NONE;
            w->p.found[0] = '\0';
        }
    } else if (strcmp(view, "updates-held") == 0) {
        cu_behind(w, CU_SEC_UNSAFE);
        dial_behind(w);
    } else if (strcmp(view, "updates-step-c6") == 0) {
        w->c6.row = FW_C6_ROW_READY;
        w->c6.carried = (fw_version_t){ 2u, 14u, 0u };
        snprintf(w->c6.fetch.file, sizeof(w->c6.fetch.file),
                 "rcbench-c6-2.14.0.bin");
        dial_behind(w);
    } else if (strcmp(view, "updates-step-dial-card") == 0) {
        /* The Dial's download failed: the next hold reads the card. */
        dial_behind(w);
        w->d[UPD_DEV_DIAL].fetch.src = DEVIMG_SRC_CARD;
        snprintf(w->d[UPD_DEV_DIAL].fetch.file,
                 sizeof(w->d[UPD_DEV_DIAL].fetch.file),
                 "BENCH/UPDATES/rcbench-dial-0.22.0.bin");
    } else if (strcmp(view, "updates-step-dial") == 0
               || strcmp(view, "updates-done") == 0) {
        dial_behind(w);
    } else if (strcmp(view, "updates-step-dial-ask") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_UPDATING;
        w->d[UPD_DEV_DIAL].phase = DEVUPD_CONFIRM;
        w->d[UPD_DEV_DIAL].secs = 27u;
    } else if (strcmp(view, "updates-step-waiting") == 0) {
        /* After a restart: the Dial bound, not connected yet. */
        w->run = true;
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_NOT_LINKED;
        w->d[UPD_DEV_DIAL].known = false;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief The fit check's views: every tag and every card line.
 * @param view the view's name
 * @param w    the pose, from base()
 */
static void fit_views(const char *view, wiz_pose_t *w)
{
    if (strcmp(view, "updates-wiz-node") == 0) {
        pnode_behind(w);
    } else if (strcmp(view, "updates-wiz-dnode") == 0) {
        /* The display node bound as the power node, its widest file name
         * on the card. */
        pnode_behind(w);
        w->d[UPD_DEV_PNODE].display = true;
        snprintf(w->d[UPD_DEV_PNODE].fetch.file,
                 sizeof(w->d[UPD_DEV_PNODE].fetch.file),
                 "BENCH/UPDATES/rcbench-node-display-0.23.18.bin");
        w->d[UPD_DEV_PNODE].fetch.src = DEVIMG_SRC_CARD;
    } else if (strcmp(view, "updates-wiz-node-ask") == 0) {
        pnode_behind(w);
        w->d[UPD_DEV_PNODE].row = DEVUPD_ROW_UPDATING;
        w->d[UPD_DEV_PNODE].phase = DEVUPD_CONFIRM;
        w->d[UPD_DEV_PNODE].secs = 27u;
    } else if (strcmp(view, "updates-wiz-dev-download") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_UPDATING;
        w->d[UPD_DEV_DIAL].fetch.run = UPD_FETCH_NET;
        w->d[UPD_DEV_DIAL].fetch.percent = 43u;
    } else if (strcmp(view, "updates-wiz-dev-card") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_UPDATING;
        w->d[UPD_DEV_DIAL].fetch.run = UPD_FETCH_CARD;
        w->d[UPD_DEV_DIAL].fetch.src = DEVIMG_SRC_CARD;
        snprintf(w->d[UPD_DEV_DIAL].fetch.file,
                 sizeof(w->d[UPD_DEV_DIAL].fetch.file),
                 "BENCH/UPDATES/rcbench-dial-0.22.0.bin");
    } else if (strcmp(view, "updates-wiz-dev-ram") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].fetch.src = DEVIMG_SRC_RAM;
        snprintf(w->d[UPD_DEV_DIAL].fetch.file,
                 sizeof(w->d[UPD_DEV_DIAL].fetch.file),
                 "rcbench-node-bal-10.10.10.bin");
    } else if (strcmp(view, "updates-wiz-dev-nosig") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].fetch.src = DEVIMG_SRC_RAM;
        w->d[UPD_DEV_DIAL].fetch.nosig = true;
        /* The longest file name the release has: a node's. */
        snprintf(w->d[UPD_DEV_DIAL].fetch.file,
                 sizeof(w->d[UPD_DEV_DIAL].fetch.file),
                 "rcbench-node-bal-10.10.10.bin");
    } else if (strcmp(view, "updates-wiz-c6-check") == 0) {
        w->c6.row = FW_C6_ROW_UPDATING;
        w->c6.carried = (fw_version_t){ 2u, 14u, 0u };
        w->c6.fetch.run = UPD_FETCH_CHECK;
    } else if (strcmp(view, "updates-wiz-dev-send") == 0) {
        dial_behind(w);
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_UPDATING;
        w->d[UPD_DEV_DIAL].phase = DEVUPD_SEND;
        w->d[UPD_DEV_DIAL].percent = 42u;
    } else if (strcmp(view, "updates-wiz-wait-iomcu") == 0) {
        w->run = true;
        w->cu.sec = CU_SEC_NO_LINK;
        w->cu.known = false;
    } else if (strcmp(view, "updates-wiz-wait-node") == 0) {
        w->run = true;
    } else if (strcmp(view, "updates-wiz-failed") == 0) {
        dial_behind(w);
    } else if (strcmp(view, "updates-wiz-older") == 0) {
        w->cu.sec = CU_SEC_READY;
        w->cu.running = cu_ex(EX_VER_NEWER);
    } else if (strcmp(view, "updates-wiz-cu-busy") == 0) {
        cu_behind(w, CU_SEC_BUSY);
    } else if (strcmp(view, "updates-wiz-pending") == 0) {
        cu_behind(w, CU_SEC_CHECKING);
        dial_behind(w);
    } else if (strcmp(view, "updates-wiz-c6-running") == 0) {
        w->c6.row = FW_C6_ROW_UPDATING;
        w->c6.carried = (fw_version_t){ 2u, 14u, 0u };
        w->c6.percent = 37u;
    } else if (strcmp(view, "updates-wiz-dl-held") == 0) {
        w->p.got = UPD_GOT_NEWER;
        strcpy(w->p.found, ex_version(EX_VER_NEWER).text);
        w->p.dl_block = UPD_WHY_ARMED;
    } else if (strcmp(view, "updates-wiz-absent-a") == 0) {
        w->cu = (upd_cu_view_t){ .sec = CU_SEC_NO_IMAGE };
        w->c6.row = FW_C6_ROW_BAD_IMAGE;
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_NOT_LINKED;
        w->d[UPD_DEV_DIAL].known = false;
        w->d[UPD_DEV_PNODE].row = DEVUPD_ROW_NO_IMAGE;
        w->d[UPD_DEV_BAL].row = DEVUPD_ROW_BAD_IMAGE;
    } else if (strcmp(view, "updates-wiz-absent-b") == 0) {
        w->cu.sec = CU_SEC_NO_LINK;
        w->cu.known = false;
        w->c6.row = FW_C6_ROW_LINK_FAILED;
        w->c6.known = false;
    } else if (strcmp(view, "updates-wiz-absent-c") == 0) {
        w->cu.sec = CU_SEC_FAILED;
        w->c6.row = FW_C6_ROW_READY;
        w->c6.known = false;
    } else if (strcmp(view, "updates-wiz-absent-d") == 0) {
        w->cu.sec = CU_SEC_USB;
    } else if (strcmp(view, "updates-wiz-capped") == 0) {
        w->p.phase = UPD_PH_IDLE;
        w->p.got = UPD_GOT_NONE;
        w->p.line_capped = true;
    } else if (strcmp(view, "updates-wiz-off") == 0) {
        /* The Dial bound and OFF on WIRELESS: the tag OFF, named on the
         * last card. */
        w->d[UPD_DEV_DIAL].row = DEVUPD_ROW_NOT_LINKED;
        w->d[UPD_DEV_DIAL].known = false;
        w->d[UPD_DEV_DIAL].off = true;
    } else if (strcmp(view, "updates-wiz-no-ble") == 0) {
        /* The power node bound and ON while Bluetooth LE cannot link
         * it: the tag NO BLUETOOTH, named on the last card. */
        w->d[UPD_DEV_PNODE].row = DEVUPD_ROW_NOT_LINKED;
        w->d[UPD_DEV_PNODE].bound = true;
        w->d[UPD_DEV_PNODE].known = false;
        w->d[UPD_DEV_PNODE].no_ble = true;
    } else if (strcmp(view, "updates-wiz-all-current") == 0) {
        /* No power node bound: every step current. */
        w->d[UPD_DEV_PNODE].bound = false;
    } else if (strcmp(view, "updates-wiz-no-check") == 0) {
        /* The same without a check since the start. */
        w->d[UPD_DEV_PNODE].bound = false;
        w->p.phase = UPD_PH_IDLE;
        w->p.got = UPD_GOT_NONE;
        w->p.found[0] = '\0';
    } else if (strcmp(view, "updates-wiz-inst-failed") == 0) {
        /* A download staged, its install failed. */
        w->d[UPD_DEV_PNODE].bound = false;
        w->p.job = UPD_JOB_DOWNLOAD;
        w->p.got = UPD_GOT_INSTALL;
        strcpy(w->p.found, ex_version(EX_VER_NEWER).text);
    }
}

void render_upd_wiz_pose(const char *view)
{
    if (!render_upd_wiz_view(view)) {
        return;
    }
    static wiz_pose_t w;
    base(&w);
    if (!shot_views(view, &w)) {
        fit_views(view, &w);
    }
    upd_screen_set(&w.p);
    upd_screen_set_coproc(&w.cu);
    upd_c6_set(&w.c6);
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        upd_dev_set((upd_dev_id_t)i, &w.d[i]);
    }
    upd_screen_set_run(w.run);
    if (strcmp(view, "updates-step-dial-card") == 0) {
        char line[UI_ALERT_MAX];
        upd_fetch_fail_line(UPD_FETCH_FAIL_NET, "D10", TR(UPD_FAIL_CONNECT),
                            line, sizeof(line));
        upd_screen_set_result(UPD_WIZ_DIAL, line);
    }
    if (strcmp(view, "updates-wiz-failed") == 0) {
        char line[UI_ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_DEV_FAILED),
                 TR(DEV_NAME_DIAL), TR(DEV_FAIL_TIMEOUT));
        upd_screen_set_result(UPD_WIZ_DIAL, line);
    }
    if (strcmp(view, "updates-run-iomcu") == 0) {
        /* The start's band line after the install. */
        char line[UI_ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_FW_UPDATED), w.p.running);
        ui_router_set_alert(line);
    }
    if (strcmp(view, "updates-offer") == 0) {
        /* The offer's band line, said in the frame that opened UPDATES. */
        ui_router_set_alert(TR(ALERT_CU_OFFER));
    }
    if (strcmp(view, "updates-done") == 0) {
        /* The Dial's step skipped: SKIP THIS STEP, 500 x 56px at y 364. */
        body_tap(750, 392);
    }
}
