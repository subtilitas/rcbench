/**
 * @file
 * @brief The settings backup's driver.  See sbk_ctl.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sbk_ctl.h"

#include <stdio.h>
#include <string.h>

#include "rcbench_version.h"
#include "sbk_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "wall_clock.h"

/** @brief The driver's state. */
typedef struct {
    bool        busy;          /**< a request is under way              */
    bool        busy_check;    /**< it is the offer's read              */
    bool        owe_auto;      /**< an automatic copy is owed           */
    bool        owe_save;      /**< SAVE SETTINGS is owed               */
    bool        owe_list;      /**< a list is owed                      */
    bool        owe_read;      /**< a read is owed                      */
    bool        read_check;    /**< the owed read is the offer's        */
    char        read_name[SB_NAME_MAX]; /**< the file it reads          */
    bool        mounted;       /**< a card was mounted at the last frame */
    bool        armed;         /**< the bench was ARMED at the last frame */
    int64_t     wall_s;        /**< the clock at the last frame         */
    bool        boot_defaults; /**< the panel started on defaults only  */
    bool        auto_alerted;  /**< a failed copy is said; the next said
                                    again only after one that worked    */
    sbk_save_t  save;          /**< SAVE SETTINGS' state                */
    uint8_t     save_code;     /**< its last failure                    */
    sbk_list_t  list;          /**< the list's state                    */
    uint8_t     list_code;     /**< its last answer                     */
    uint8_t     count;         /**< files in @c files                   */
    sb_entry_t  files[SB_FILES]; /**< the last list                     */
    sbk_load_t  load;          /**< the load's state                    */
    sbk_why_t   why;           /**< why a load failed                   */
    uint8_t     why_code;      /**< its code                            */
    unsigned    why_line;      /**< the line of a file refused whole    */
    int         changed;       /**< values a load changed               */
    char        load_name[SB_NAME_MAX]; /**< the file it read           */
    sbk_offer_t offer;         /**< the restore offer                   */
    int         offer_index;   /**< the file it names in @c files       */
} sbk_state_t;

/** @brief The driver's one instance. */
static sbk_state_t k;
/** @brief A request's text and a read's answer. */
static char s_buf[SF_FILE_MAX + 1u];
/** @brief A file read, parsed. */
static sf_load_t s_load;

void sbk_init(void)
{
    memset(&k, 0, sizeof(k));
    k.offer_index = -1;
    k.boot_defaults = settings_stored_defaults();
    k.offer = k.boot_defaults ? SBK_OFFER_WAIT : SBK_OFFER_DONE;
}

char *sbk_buffer(void) { return s_buf; }

size_t sbk_buffer_size(void) { return sizeof(s_buf); }

bool sbk_busy(void) { return k.busy; }

bool sbk_armed(void) { return k.armed; }

bool sbk_mounted(void) { return k.mounted; }

bool sbk_auto_allowed(void) { return k.offer == SBK_OFFER_DONE; }

/** @brief The stored settings as a file in s_buf; its bytes, 0 for none. */
static size_t format_now(void)
{
    char when[SF_WRITTEN_MAX] = "";
    if (wall_valid(k.wall_s)) {
        (void)wall_utc_text(when, sizeof(when), k.wall_s);
    }
    return sf_format(s_buf, sizeof(s_buf), RCBENCH_VERSION_STRING, when);
}

/** @brief The offer's state follows the stored copy: a save of other
 *         settings than the defaults makes it not due. */
static void offer_follow(void)
{
    if ((k.offer == SBK_OFFER_WAIT || k.offer == SBK_OFFER_CHECKING
         || k.offer == SBK_OFFER_DUE)
        && !settings_stored_defaults()) {
        k.offer = SBK_OFFER_DONE;
    }
    if (k.offer == SBK_OFFER_DUE && !k.armed
        && ui_router_current() == SCREEN_OVERVIEW) {
        k.offer = SBK_OFFER_SHOWN;
        sbk_screen_open(true);
    }
}

/** @brief What is owed, answered at once without a card. */
static void without_card(void)
{
    if (k.owe_save) {
        k.owe_save = false;
        k.save = SBK_SAVE_FAILED;
        k.save_code = SB_NO_CARD;
    }
    if (k.owe_list) {
        k.owe_list = false;
        k.list = SBK_LIST_READY;
        k.list_code = SB_NO_CARD;
        k.count = 0u;
    }
    if (k.owe_read) {
        k.owe_read = false;
        if (!k.read_check) {
            k.load = SBK_LOAD_FAILED;
            k.why = SBK_WHY_CARD;
            k.why_code = SB_NO_CARD;
        }
    }
    k.owe_auto = false;
}

/** @brief The next request into @p req; whether there is one. */
static bool next(sb_req_t *req)
{
    memset(req, 0, sizeof(*req));
    if (k.owe_read) {
        k.owe_read = false;
        k.busy_check = k.read_check;
        req->op = SB_REQ_READ;
        (void)snprintf(req->name, sizeof(req->name), "%s", k.read_name);
        return true;
    }
    if (k.owe_save) {
        k.owe_save = false;
        req->len = format_now();
        if (req->len == 0u) {
            k.save = SBK_SAVE_FAILED;
            k.save_code = SB_ARG;
            return false;
        }
        req->op = SB_REQ_SAVE;
        return true;
    }
    if (k.owe_list) {
        k.owe_list = false;
        req->op = SB_REQ_LIST;
        return true;
    }
    if (k.owe_auto && sbk_auto_allowed()) {
        k.owe_auto = false;
        req->len = format_now();
        if (req->len == 0u) {
            return false;
        }
        req->op = SB_REQ_AUTO;
        return true;
    }
    return false;
}

bool sbk_tick(const sbk_feed_t *f, sb_req_t *req)
{
    if (f == NULL || req == NULL) {
        return false;
    }
    const bool mounted_now = f->mounted && !k.mounted;
    k.mounted = f->mounted;
    k.armed = f->armed;
    k.wall_s = f->wall_s;
    if (f->committed && f->mounted) {
        k.owe_auto = true;
    }
    if (mounted_now && k.offer == SBK_OFFER_WAIT) {
        k.offer = SBK_OFFER_CHECKING;
        k.owe_list = true;
    }
    if (!f->mounted && k.offer == SBK_OFFER_CHECKING) {
        k.offer = SBK_OFFER_WAIT;        /* the next card is looked at */
    }
    offer_follow();
    if (k.busy) {
        return false;
    }
    if (!f->mounted) {
        without_card();
        return false;
    }
    k.busy = next(req);
    return k.busy;
}

/** @brief A copy that did not reach the card, said once. */
static void auto_answer(uint8_t result)
{
    if (result == SB_OK) {
        k.auto_alerted = false;
        return;
    }
    if (result == SB_NO_CARD || k.auto_alerted) {
        return;
    }
    k.auto_alerted = true;
    if (result == SB_FULL) {
        ui_router_set_alert(TR(ALERT_SB_FULL));
    } else {
        char line[96];
        (void)snprintf(line, sizeof(line), TR(ALERT_SB_FAILED),
                       (unsigned)result);
        ui_router_set_alert(line);
    }
}

/** @brief The offer's list: its newest file is read, or the offer is not
 *         due. */
static void offer_list(void)
{
    if (k.offer != SBK_OFFER_CHECKING) {
        return;
    }
    const int newest = sb_newest(k.files, k.count);
    if (k.list_code != SB_OK || newest < 0) {
        k.offer = SBK_OFFER_DONE;
        return;
    }
    k.offer_index = newest;
    k.owe_read = true;
    k.read_check = true;
    (void)snprintf(k.read_name, sizeof(k.read_name), "%s",
                   k.files[newest].name);
}

/** @brief The offer's read: due when the file holds other settings. */
static void offer_read(const sb_done_t *d)
{
    if (k.offer != SBK_OFFER_CHECKING) {
        return;
    }
    if (d->result == SB_OK
        && sf_parse(s_buf, d->len, &s_load, NULL) == SF_OK
        && sf_differs(&s_load)) {
        k.offer = SBK_OFFER_DUE;
        offer_follow();
    } else {
        k.offer = SBK_OFFER_DONE;
    }
}

/** @brief LOAD SETTINGS' read: parsed and put in force on a SAFE bench. */
static void load_read(const sb_done_t *d)
{
    (void)snprintf(k.load_name, sizeof(k.load_name), "%s", d->name);
    k.load = SBK_LOAD_FAILED;
    if (d->result != SB_OK) {
        k.why = SBK_WHY_CARD;
        k.why_code = d->result;
        return;
    }
    const sf_err_t e = sf_parse(s_buf, d->len, &s_load, &k.why_line);
    if (e != SF_OK) {
        k.why = SBK_WHY_FILE;
        k.why_code = (uint8_t)e;
        return;
    }
    if (k.armed) {
        k.why = SBK_WHY_ARMED;
        return;
    }
    k.changed = sf_apply(&s_load);
    settings_request_save();
    settings_apply_all();
    k.why = SBK_WHY_NONE;
    k.load = SBK_LOAD_DONE;
}

void sbk_done(const sb_done_t *d)
{
    const bool check = k.busy_check;
    k.busy = false;
    k.busy_check = false;
    if (d == NULL) {
        return;
    }
    switch ((sb_req_op_t)d->op) {
    case SB_REQ_SAVE:
        k.save = (d->result == SB_OK) ? SBK_SAVE_WRITTEN : SBK_SAVE_FAILED;
        k.save_code = d->result;
        break;
    case SB_REQ_AUTO:
        auto_answer(d->result);
        break;
    case SB_REQ_LIST:
        k.list = SBK_LIST_READY;
        k.list_code = d->result;
        k.count = (d->count <= SB_FILES) ? d->count : SB_FILES;
        memcpy(k.files, d->files, sizeof(k.files));
        offer_list();
        break;
    case SB_REQ_READ:
        if (check) {
            offer_read(d);
        } else {
            load_read(d);
        }
        break;
    case SB_REQ_NONE:
    default:
        break;
    }
    sbk_screen_invalidate();
    settings_screen_invalidate();
}

void sbk_ask_save(void)
{
    k.owe_save = true;
    k.save = SBK_SAVE_WRITING;
}

sbk_save_t sbk_save_state(void) { return k.save; }

uint8_t sbk_save_code(void) { return k.save_code; }

void sbk_ask_list(void)
{
    k.owe_list = true;
    k.list = SBK_LIST_READING;
    k.count = 0u;
}

sbk_list_t sbk_list_state(void) { return k.list; }

uint8_t sbk_list_code(void) { return k.list_code; }

const sb_entry_t *sbk_files(uint8_t *count)
{
    if (count != NULL) {
        *count = k.count;
    }
    return k.files;
}

void sbk_ask_load(int index)
{
    if (index < 0 || index >= (int)k.count) {
        return;
    }
    (void)snprintf(k.read_name, sizeof(k.read_name), "%s",
                   k.files[index].name);
    k.owe_read = true;
    k.read_check = false;
    k.load = SBK_LOAD_READING;
    k.why = SBK_WHY_NONE;
}

sbk_load_t sbk_load_state(void) { return k.load; }

sbk_why_t sbk_load_why(uint8_t *code, unsigned *line)
{
    if (code != NULL) {
        *code = k.why_code;
    }
    if (line != NULL) {
        *line = k.why_line;
    }
    return k.why;
}

const sf_load_t *sbk_load_result(int *changed)
{
    if (changed != NULL) {
        *changed = k.changed;
    }
    return &s_load;
}

const char *sbk_load_name(void) { return k.load_name; }

void sbk_load_clear(void)
{
    if (k.load != SBK_LOAD_READING) {
        k.load = SBK_LOAD_IDLE;
    }
}

sbk_offer_t sbk_offer_state(void) { return k.offer; }

int sbk_offer_index(void) { return k.offer_index; }

void sbk_offer_answered(void)
{
    k.offer = SBK_OFFER_DONE;
}
