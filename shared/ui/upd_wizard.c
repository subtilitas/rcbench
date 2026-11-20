/**
 * @file upd_wizard.c
 * @brief SETUP > UPDATES' step model. See upd_wizard.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_wizard.h"

#include <stdio.h>
#include <string.h>

_Static_assert(UPD_WIZ_STEPS == 3 + UPD_DEV_COUNT,
               "PANEL, IOMCU, C6 RADIO, then one step per device");
_Static_assert(UPD_WIZ_DIAL + UPD_DEV_DIAL == UPD_WIZ_DIAL
               && UPD_WIZ_DIAL + UPD_DEV_PNODE == UPD_WIZ_PNODE
               && UPD_WIZ_DIAL + UPD_DEV_BAL == UPD_WIZ_BAL,
               "the device steps follow upd_dev_id_t's order");

bool upd_wiz_blocks(upd_wiz_state_t s)
{
    switch (s) {
    case UPD_WIZ_OPEN:
    case UPD_WIZ_HELD:
    case UPD_WIZ_RUNNING:
    case UPD_WIZ_PENDING:
    case UPD_WIZ_WAITING:
    case UPD_WIZ_FAILED:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Sets a step's state, tag and reason.
 * @param st  the step
 * @param s   its state
 * @param t   its tag
 * @param why HELD's or PENDING's reason
 */
static void set(upd_wiz_step_t *st, upd_wiz_state_t s, upd_wiz_tag_t t,
                upd_wiz_why_t why)
{
    st->state = s;
    st->tag = t;
    st->why = why;
}

/**
 * @brief Writes a version as "major.minor.patch".
 * @param out the text
 * @param maj the major number
 * @param min the minor number
 * @param pat the patch number
 */
static void ver_text(char *out, unsigned maj, unsigned min, unsigned pat)
{
    (void)snprintf(out, UPD_WIZ_VER_MAX, "%u.%u.%u", maj, min, pat);
}

/**
 * @brief Compares two firmware versions, as fw_version_cmp() does; here so
 *        the host renderers link the model without the image checks.
 * @param a the first version
 * @param b the second version
 * @return negative, zero or positive as @p a is older, the same or newer
 */
static int ver_cmp(fw_version_t a, fw_version_t b)
{
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    return a.patch == b.patch ? 0 : (a.patch < b.patch ? -1 : 1);
}

/**
 * @brief A firmware version's text.
 * @param out the text
 * @param v   the version
 */
static void fw_text(char *out, fw_version_t v)
{
    ver_text(out, v.major, v.minor, v.patch);
}

/* ------------------------------------------------------------------ PANEL */

/**
 * @brief UPDATES' download gate as a step's reason.
 * @param w the gate's reason, not UPD_WHY_NONE
 * @return the step's reason
 */
static upd_wiz_why_t panel_why(upd_why_t w)
{
    switch (w) {
    case UPD_WHY_ARMED:   return UPD_WIZ_WHY_ARMED;
    case UPD_WHY_SUPPLY:  return UPD_WIZ_WHY_SUPPLY;
    case UPD_WHY_RUN:     return UPD_WIZ_WHY_RUN;
    case UPD_WHY_STOP:    return UPD_WIZ_WHY_STOP;
    case UPD_WHY_FILES:   return UPD_WIZ_WHY_RADIO;
    case UPD_WHY_INSTALL: return UPD_WIZ_WHY_INSTALL;
    case UPD_WHY_DRAWING: return UPD_WIZ_WHY_DRAWING;
    default:              return UPD_WIZ_WHY_BUSY;
    }
}

/**
 * @brief PANEL from UPDATES' view.  A check that found no newer release,
 *        or failed, never blocks; a failed download neither, nor a
 *        download whose install failed (UPD_GOT_INSTALL): the hold
 *        takes a check that found a newer release, so the retry is CHECK.
 * @param st the step
 * @param v  the view, or NULL
 */
static void panel_step(upd_wiz_step_t *st, const upd_view_t *v)
{
    if (v == NULL) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_CHECKED, UPD_WIZ_WHY_NONE);
        return;
    }
    if (v->running[0] != '\0') {
        (void)snprintf(st->ver, sizeof(st->ver), "%s", v->running);
    }
    const bool ended = v->phase == UPD_PH_ENDED;
    if (upd_busy(v)) {
        set(st, UPD_WIZ_RUNNING,
            v->job == UPD_JOB_CHECK ? UPD_WIZ_TAG_CHECKING
                                    : UPD_WIZ_TAG_UPDATING,
            UPD_WIZ_WHY_NONE);
    } else if (v->job == UPD_JOB_DOWNLOAD && ended) {
        /* STAGED: the install runs and the panel restarts. */
        if (v->got == UPD_GOT_STAGED) {
            set(st, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE);
        } else {
            set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_FAILED, UPD_WIZ_WHY_NONE);
        }
    } else if (v->job == UPD_JOB_CHECK && ended && v->got == UPD_GOT_NEWER
               && !v->unavailable) {
        if (!v->have_net) {
            set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NETWORK);
        } else if (v->dl_block != UPD_WHY_NONE) {
            set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, panel_why(v->dl_block));
        } else {
            set(st, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE);
        }
    } else if (v->job == UPD_JOB_CHECK && ended
               && (v->got == UPD_GOT_CURRENT || v->got == UPD_GOT_PAST_LINE
                   || v->got == UPD_GOT_UNVERSIONED)) {
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE);
    } else {
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_NOT_CHECKED, UPD_WIZ_WHY_NONE);
    }
    if (v->found[0] != '\0'
        && (st->state != UPD_WIZ_DONE || st->tag == UPD_WIZ_TAG_FAILED)) {
        (void)snprintf(st->next, sizeof(st->next), "%s", v->found);
    }
}

/* ------------------------------------------------------------------ IOMCU */

/**
 * @brief IOMCU from its section.
 * @param st  the step
 * @param v   the section, or NULL
 * @param run an update run stands
 */
static void cu_step(upd_wiz_step_t *st, const upd_cu_view_t *v, bool run)
{
    if (v == NULL) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_LINK, UPD_WIZ_WHY_NONE);
        return;
    }
    if (v->known) {
        ver_text(st->ver, v->running.major, v->running.minor,
                 v->running.patch);
    }
    if (v->have_image) {
        ver_text(st->next, v->carried.major, v->carried.minor,
                 v->carried.patch);
    }
    st->older = v->known && v->have_image
                && coproc_update_older(v->running, v->carried);
    switch (v->sec) {
    case CU_SEC_READY:
        set(st, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_UNSAFE:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_UNSAFE);
        break;
    case CU_SEC_RADIO:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RADIO);
        break;
    case CU_SEC_INSTALL:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_INSTALL);
        break;
    case CU_SEC_BUSY:
        set(st, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_UNKNOWN:
    case CU_SEC_CHECKING:
        set(st, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_SAME:
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_NO_LINK:
        /* The link comes up seconds after a start: a run waits for it. */
        set(st, run ? UPD_WIZ_WAITING : UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_LINK,
            UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_FAILED:
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_CHECK_FAILED, UPD_WIZ_WHY_NONE);
        break;
    case CU_SEC_USB:
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_USB_ONLY, UPD_WIZ_WHY_NONE);
        break;
    default: /* NO_IMAGE, NO_BOARD: no image for the board */
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE);
        break;
    }
}

/* --------------------------------------------------------------- C6 RADIO */

/**
 * @brief The C6 row's gate as a step's state and reason: a gate the
 *        operator clears holds the step; a wait that ends by itself
 *        leaves it PENDING.
 * @param st  the step
 * @param why the row's reason
 */
static void c6_blocked(upd_wiz_step_t *st, fw_c6_why_t why)
{
    switch (why) {
    case FW_C6_WHY_ARMED:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_ARMED);
        break;
    case FW_C6_WHY_SUPPLY:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_SUPPLY);
        break;
    case FW_C6_WHY_RUN:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RUN);
        break;
    case FW_C6_WHY_RADIO:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_RADIO);
        break;
    case FW_C6_WHY_DRAWING:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_DRAWING);
        break;
    case FW_C6_WHY_PROG:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_PROG);
        break;
    case FW_C6_WHY_BLE:
        set(st, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_BLE);
        break;
    case FW_C6_WHY_IOMCU:
    case FW_C6_WHY_INSTALL:
    case FW_C6_WHY_DEVICE:
    case FW_C6_WHY_ENDING:
    case FW_C6_WHY_RESTART:
        set(st, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_BUSY);
        break;
    default:
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE);
        break;
    }
}

/**
 * @brief C6 RADIO from its row.  The step is OPEN only while the C6
 *        reports a version other than the carried one; REINSTALL, and a
 *        blocked row on the carried version, are DONE.  A row whose own
 *        update still ends (FW_C6_WHY_ENDING: its last step out;
 *        FW_C6_WHY_RESTART: the restart after a success owed) is PENDING on any version: the
 *        update's gate holds until the panel restarts.
 * @param st the step
 * @param v  the row, or NULL
 */
static void c6_step(upd_wiz_step_t *st, const upd_c6_view_t *v)
{
    if (v == NULL) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_REPORTED, UPD_WIZ_WHY_NONE);
        return;
    }
    if (v->known) {
        fw_text(st->ver, v->running);
    }
    if (v->have_image) {
        fw_text(st->next, v->carried);
    }
    const bool both = v->known && v->have_image;
    st->older = both && ver_cmp(v->carried, v->running) < 0;
    switch (v->row) {
    case FW_C6_ROW_NO_IMAGE:
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE);
        return;
    case FW_C6_ROW_BAD_IMAGE:
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE, UPD_WIZ_WHY_NONE);
        return;
    case FW_C6_ROW_LINK_FAILED:
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_LINK_FAILED, UPD_WIZ_WHY_NONE);
        return;
    case FW_C6_ROW_UPDATING:
        set(st, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE);
        return;
    case FW_C6_ROW_REINSTALL:
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE);
        return;
    default: /* READY, BLOCKED */
        break;
    }
    /* A row whose own update still ends is blocked on any version: the
     * update's gate holds until the panel restarts. */
    const bool ending = v->row == FW_C6_ROW_BLOCKED
                        && (v->why == FW_C6_WHY_ENDING
                            || v->why == FW_C6_WHY_RESTART);
    if (!ending && !v->known) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_REPORTED, UPD_WIZ_WHY_NONE);
    } else if (!ending && !v->have_image) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE);
    } else if (!ending && ver_cmp(v->running, v->carried) == 0) {
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE);
    } else if (v->row == FW_C6_ROW_READY) {
        set(st, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE);
    } else {
        c6_blocked(st, v->why);
    }
}

/* ---------------------------------------------------------------- devices */

/**
 * @brief A device's step from its row.  A device that is not connected is
 *        ABSENT whatever the row's gate says, and WAITING in a run when it
 *        is bound, its switch on WIRELESS is ON and Bluetooth LE can link
 *        it.  With the switch OFF the panel runs no link to it: it never
 *        waits and reads OFF.  With Bluetooth LE unable to link (no C6
 *        link, no sync) it never waits either and reads NO BLUETOOTH.
 * @param st  the step
 * @param v   the row, or NULL
 * @param run an update run stands
 */
static void dev_step(upd_wiz_step_t *st, const upd_dev_view_t *v, bool run)
{
    if (v == NULL) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND, UPD_WIZ_WHY_NONE);
        return;
    }
    if (v->known) {
        fw_text(st->ver, v->running);
    }
    if (v->have_image) {
        fw_text(st->next, v->carried);
    }
    const bool both = v->known && v->have_image;
    st->older = both && ver_cmp(v->carried, v->running) < 0;
    if (v->row == DEVUPD_ROW_UPDATING) {
        set(st, UPD_WIZ_RUNNING, UPD_WIZ_TAG_UPDATING, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_NO_IMAGE) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_IMAGE, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_BAD_IMAGE) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_BAD_IMAGE, UPD_WIZ_WHY_NONE);
    } else if (!v->bound && !v->known) {
        /* A device that reported its version is bound. */
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NOT_BOUND, UPD_WIZ_WHY_NONE);
    } else if (v->off && !v->known) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_OFF, UPD_WIZ_WHY_NONE);
    } else if (v->no_ble && !v->known) {
        set(st, UPD_WIZ_ABSENT, UPD_WIZ_TAG_NO_BLE, UPD_WIZ_WHY_NONE);
    } else if (!v->known || v->row == DEVUPD_ROW_NOT_LINKED) {
        set(st, run ? UPD_WIZ_WAITING : UPD_WIZ_ABSENT,
            UPD_WIZ_TAG_NOT_CONNECTED, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_CHECKING) {
        set(st, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_REINSTALL
               || (both && ver_cmp(v->running, v->carried) == 0)) {
        set(st, UPD_WIZ_DONE, UPD_WIZ_TAG_CURRENT, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_READY) {
        set(st, UPD_WIZ_OPEN, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_NONE);
    } else if (v->row == DEVUPD_ROW_ARMED) {
        set(st, UPD_WIZ_HELD, UPD_WIZ_TAG_NEXT, UPD_WIZ_WHY_ARMED);
    } else { /* BUSY: another update runs or waits */
        set(st, UPD_WIZ_PENDING, UPD_WIZ_TAG_CHECKING, UPD_WIZ_WHY_BUSY);
    }
}

/* ------------------------------------------------------------- the steps */

/**
 * @brief The current step: the lowest whose update runs, else PANEL while
 *        its check runs, else the lowest that blocks.
 * @param w the steps
 * @return the step, or UPD_WIZ_NONE
 */
static unsigned current_of(const upd_wiz_t *w)
{
    /* An update before PANEL's check: the check takes no card from a
     * device's ask and no CANCEL UPDATE from the footer. */
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        if (w->step[i].state == UPD_WIZ_RUNNING
            && w->step[i].tag == UPD_WIZ_TAG_UPDATING) {
            return i;
        }
    }
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        if (w->step[i].state == UPD_WIZ_RUNNING) {
            return i;
        }
    }
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        if (upd_wiz_blocks(w->step[i].state)) {
            return i;
        }
    }
    return UPD_WIZ_NONE;
}

/**
 * @brief The tags of the steps after the current one: OPEN, HELD and
 *        FAILED read LATER; with PANEL current on a newer release, IOMCU
 *        and the connected devices on the carried version too.
 * @param w the steps, current set
 */
static void later_tags(upd_wiz_t *w)
{
    if (w->current >= UPD_WIZ_STEPS) {
        return;
    }
    const upd_wiz_step_t *p = &w->step[UPD_WIZ_PANEL];
    const bool newer = w->current == UPD_WIZ_PANEL
                       && p->tag != UPD_WIZ_TAG_CHECKING;
    for (unsigned i = w->current + 1u; i < UPD_WIZ_STEPS; ++i) {
        upd_wiz_step_t *st = &w->step[i];
        const bool waits = st->state == UPD_WIZ_OPEN
                           || st->state == UPD_WIZ_HELD
                           || st->state == UPD_WIZ_FAILED;
        const bool follows = newer && i != UPD_WIZ_C6
                             && st->state == UPD_WIZ_DONE;
        if (waits || follows) {
            st->tag = UPD_WIZ_TAG_LATER;
        }
    }
}

/**
 * @brief The current step's control, SKIP and CANCEL UPDATE.
 * @param w the steps, current set
 */
static void controls(upd_wiz_t *w)
{
    static const upd_wiz_act_t k_act[UPD_WIZ_STEPS] = {
        UPD_WIZ_ACT_DOWNLOAD, UPD_WIZ_ACT_IOMCU, UPD_WIZ_ACT_C6,
        UPD_WIZ_ACT_DIAL,     UPD_WIZ_ACT_PNODE, UPD_WIZ_ACT_BAL,
    };
    if (w->current >= UPD_WIZ_STEPS) {
        for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
            w->show_skipped |= w->step[i].state == UPD_WIZ_SKIPPED;
        }
        return;
    }
    const unsigned c = w->current;
    const upd_wiz_state_t s = w->step[c].state;
    switch (s) {
    case UPD_WIZ_OPEN:
    case UPD_WIZ_FAILED:
        w->act = k_act[c];
        w->act_live = true;
        break;
    case UPD_WIZ_HELD:
        w->act = k_act[c];
        break;
    case UPD_WIZ_RUNNING:
        /* The C6's and a device's hold show the running update's phase;
         * PANEL's runs on CHECK or PANEL UPDATE, the IOmcu's on its own
         * screen. */
        if (c >= UPD_WIZ_C6) {
            w->act = k_act[c];
        }
        w->cancel = c >= UPD_WIZ_DIAL;
        break;
    default: /* PENDING, WAITING */
        break;
    }
    w->can_skip = s == UPD_WIZ_OPEN || s == UPD_WIZ_HELD
                  || s == UPD_WIZ_FAILED || s == UPD_WIZ_WAITING;
}

upd_wiz_end_t upd_wiz_end(const upd_wiz_t *w)
{
    if (w == NULL) {
        return UPD_WIZ_END_NONE_OPEN;
    }
    for (unsigned i = UPD_WIZ_IOMCU; i < UPD_WIZ_STEPS; ++i) {
        const upd_wiz_step_t *st = &w->step[i];
        const bool current = st->state == UPD_WIZ_DONE
                             && st->tag == UPD_WIZ_TAG_CURRENT;
        const bool none = st->state == UPD_WIZ_ABSENT
                          && st->tag == UPD_WIZ_TAG_NOT_BOUND;
        if (!current && !none) {
            return UPD_WIZ_END_NONE_OPEN;
        }
    }
    const upd_wiz_step_t *p = &w->step[UPD_WIZ_PANEL];
    if (p->state != UPD_WIZ_DONE) {
        return UPD_WIZ_END_NONE_OPEN;
    }
    return p->tag == UPD_WIZ_TAG_CURRENT ? UPD_WIZ_END_CURRENT
                                         : UPD_WIZ_END_NO_CHECK;
}

void upd_wizard_eval(const upd_wiz_in_t *in, upd_wiz_t *out)
{
    if (out == NULL) {
        return;
    }
    static const upd_wiz_in_t k_none;
    if (in == NULL) {
        in = &k_none;
    }
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        out->step[i].ver[0] = '-';
    }
    panel_step(&out->step[UPD_WIZ_PANEL], in->panel);
    cu_step(&out->step[UPD_WIZ_IOMCU], in->cu, in->run);
    c6_step(&out->step[UPD_WIZ_C6], in->c6);
    for (unsigned d = 0u; d < UPD_DEV_COUNT; ++d) {
        dev_step(&out->step[UPD_WIZ_DIAL + d], in->dev[d], in->run);
    }
    for (unsigned i = 0u; i < UPD_WIZ_STEPS; ++i) {
        upd_wiz_step_t *st = &out->step[i];
        const char *r = in->result[i];
        st->result = (r != NULL && r[0] != '\0') ? r : NULL;
        /* PANEL's failed download is in its own view; the others' last
         * failed attempt makes an OPEN step FAILED, its hold the retry. */
        if (i != UPD_WIZ_PANEL && st->result != NULL
            && st->state == UPD_WIZ_OPEN) {
            st->state = UPD_WIZ_FAILED;
            st->tag = UPD_WIZ_TAG_FAILED;
        }
        /* A skipped step passes until the panel restarts or SHOW SKIPPED
         * STEPS clears the mask; an update that runs is never passed. */
        if ((in->skip & UPD_WIZ_BIT(i)) != 0u && upd_wiz_blocks(st->state)
            && st->state != UPD_WIZ_RUNNING) {
            st->state = UPD_WIZ_SKIPPED;
            st->tag = UPD_WIZ_TAG_SKIPPED;
            st->why = UPD_WIZ_WHY_NONE;
        }
    }
    out->current = current_of(out);
    later_tags(out);
    controls(out);
}
