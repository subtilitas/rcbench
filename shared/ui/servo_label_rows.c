/**
 * @file
 * @brief The SERVO LABEL rows (servo_label_rows.h): SERVO ID's value and
 *        choice list, TYPE ID's keyboard, NEW ID's value, tap and locks,
 *        PRINT's name, value, tap and locks.
 *
 * The rows are custom rows of the SERVO lists (ui_setlist.h); ID PREFIX is
 * the string row of SET_TEXT_SERVO_PREFIX, which the list types itself.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_label_rows.h"

#include <stdio.h>
#include <string.h>

#include "print_ctl.h"
#include "scan_flow.h"
#include "servo_active.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_widgets.h"
#include "wireless_printer.h"

#include "servo_screen_int.h"

/** @brief The card's side; NULL until the panel registers it. */
static const servo_label_io_t *s_io;

void servo_label_set_io(const servo_label_io_t *io)
{
    s_io = io;
}

/**
 * @brief The card's ids now.
 * @param out Takes them; empty when the card reads as not there.
 * @return false with no card, or a walk not read to its end.
 */
static bool card_ids(servo_ids_t *out)
{
    servo_ids_begin(out);
    return s_io != NULL && s_io->ids != NULL && s_io->ids(out);
}

/* ------------------------------------------------------------ SERVO ID */

/** @brief The ids SERVO ID's choice list shows: taken from the card while
 *         the list is shut, kept as drawn while it is open. */
static servo_ids_t s_shown;

/** @brief Whether SERVO ID's choice list is open over the list. */
static bool id_list_open(void)
{
    if (s.sl.choice_row < 0 || s.sl.host == NULL) {
        return false;
    }
    return s.sl.host->row(s.sl.choice_row).custom == &svl_id_row;
}

/**
 * @brief The ids the choice list shows: the card's now while it is shut,
 *        the ones drawn while it is open.
 * @return s_shown.
 */
static const servo_ids_t *shown_ids(void)
{
    if (!id_list_open()) {
        (void)card_ids(&s_shown);
    }
    return &s_shown;
}

/** @brief SERVO ID's value: the active id, or NONE. */
static void id_value(char *out, size_t n)
{
    const char *id = servo_active_id();
    snprintf(out, n, "%s", (id != NULL) ? id : TR(SVL_ID_NONE));
}

int servo_label_choices(void)
{
    return 2 + (int)shown_ids()->recent_n;
}

/** @brief Choice @p i: NONE, the ids highest first, TYPE ID last. */
static const char *id_choice(int i)
{
    const servo_ids_t *ids = shown_ids();
    if (i <= 0) {
        return TR(SVL_ID_NONE);
    }
    if (i <= (int)ids->recent_n) {
        return ids->recent[i - 1];
    }
    return TR(SVL_TYPE_ID);
}

/** @brief The choice in force: NONE, the active id where the list holds
 *         it, else none of them. */
static int id_chosen(void)
{
    const char *id = servo_active_id();
    if (id == NULL) {
        return 0;
    }
    const servo_ids_t *ids = shown_ids();
    for (int i = 0; i < (int)ids->recent_n; ++i) {
        if (strcmp(ids->recent[i], id) == 0) {
            return i + 1;
        }
    }
    return -1;
}

/**
 * @brief TYPE ID's OK: the id runs as a scanned line, its source TYPED.
 * @param id The id, upper case and well formed.
 */
static void typed_done(const char *id)
{
    (void)scan_flow_typed(id);
    ++s.sl_rev;
    servo_invalidate();
}

/** @brief TYPE ID: the keyboard on the saved prefix and its "-". */
static void type_open(void)
{
    char start[SERVO_ID_SIZE];
    snprintf(start, sizeof(start), "%.*s-", (int)SERVO_ID_PREFIX_MAX,
             settings_text(SET_TEXT_SERVO_PREFIX));
    ui_setlist_type_id(&s.sl, TR(SVL_TYPE_ID_TITLE), start, typed_done);
}

/**
 * @brief Whether the card holds @p id among its newest ids now.
 * @param id The id.
 * @return true then.
 */
static bool card_holds(const char *id)
{
    servo_ids_t live;
    if (!card_ids(&live)) {
        return false;
    }
    for (int i = 0; i < (int)live.recent_n; ++i) {
        if (strcmp(live.recent[i], id) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Choice @p i tapped, as the list drew it: NONE, an id of the
 *         card, or TYPE ID.  The list has shut by now; the ids it drew
 *         are still s_shown. */
static void id_pick(int i)
{
    const bool run = servo_screen_run_active();
    if (i > (int)s_shown.recent_n) {
        type_open();
        return;
    }
    char id[SERVO_ID_SIZE] = "";
    if (i > 0) {
        snprintf(id, sizeof(id), "%s", s_shown.recent[i - 1]);
        if (!card_holds(id)) {
            ui_action_refused(TR(SVL_ID_GONE));
            ++s.sl_rev;
            servo_invalidate();
            return;
        }
    }
    const bool ok = (i <= 0) ? servo_active_clear(run)
                             : servo_active_set(id, SERVO_ACTIVE_PICK, run);
    if (!ok) {
        ui_action_refused(TR(SVL_RUN_ACTIVE));
    }
    ++s.sl_rev;
    servo_invalidate();
}

/** @brief Why SERVO ID takes no press: a run holds the active servo. */
static const char *id_locked(void)
{
    return servo_screen_run_active() ? TR(SVL_RUN_ACTIVE) : NULL;
}

const ui_setlist_custom_t svl_id_row = {
    .label = TX_SVL_SERVO_ID, .help = TX_SVL_SERVO_ID_HELP,
    .value_text = id_value, .choices = servo_label_choices,
    .choice = id_choice, .chosen = id_chosen, .pick = id_pick,
    .locked = id_locked,
};

/* -------------------------------------------------------------- NEW ID */

/**
 * @brief The id NEW ID makes now, and why it makes none.
 * @param out Takes the id; "" when none.
 * @param n   Its size, SERVO_ID_SIZE.
 * @return The reason; NULL when @p out holds the id.
 */
static const char *next_id(char *out, size_t n)
{
    out[0] = '\0';
    servo_ids_t ids;
    if (!card_ids(&ids)) {
        return TR(SVL_NO_CARD);
    }
    uint32_t next = 0u;
    if (!servo_id_next(ids.highest, &next)) {
        return TR(SVL_NO_FREE_ID);
    }
    const char *prefix = settings_text(SET_TEXT_SERVO_PREFIX);
    if (prefix[0] == '.') {
        return TR(SVL_PREFIX_DOT);
    }
    if (servo_id_prefix_check(prefix, NULL, 0u) != SERVO_ID_OK
        || !servo_id_build(out, n, prefix, next)) {
        out[0] = '\0';
        return TR(SVL_PREFIX_BAD);
    }
    return NULL;
}

/** @brief Whether a NEW ID asked is not answered yet. */
static bool new_busy(void)
{
    return s_io != NULL && s_io->new_busy != NULL && s_io->new_busy();
}

const char *servo_label_new_why(void)
{
    if (servo_screen_run_active()) {
        return TR(SVL_RUN_ACTIVE);
    }
    if (new_busy()) {
        return TR(SVL_NEW_BUSY);
    }
    char id[SERVO_ID_SIZE];
    return next_id(id, sizeof(id));
}

/** @brief NEW ID's value: NEXT and the id it makes, "" when none. */
static void new_value(char *out, size_t n)
{
    char id[SERVO_ID_SIZE];
    if (next_id(id, sizeof(id)) != NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, n, "%s %s", TR(SVL_NEXT), id);
}

/** @brief A tap on NEW ID: the panel makes the id and its folder; the
 *         answer makes the servo active, and servo_label_tick() repaints
 *         the rows then. */
static void new_tap(void)
{
    const char *why = servo_label_new_why();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    if (s_io != NULL && s_io->new_id != NULL) {
        (void)s_io->new_id(settings_text(SET_TEXT_SERVO_PREFIX));
    }
    ++s.sl_rev;
    servo_invalidate();
}

const ui_setlist_custom_t svl_new_row = {
    .label = TX_SVL_NEW_ID, .help = TX_SVL_NEW_ID_HELP,
    .value_text = new_value, .tap = new_tap, .locked = servo_label_new_why,
};

/* --------------------------------------------------------------- PRINT */

/** @brief PRINT AGAIN's case: a label of the active servo printed in this
 *         session, while it was active or not, or its id came from its
 *         label, scanned or typed. */
static bool printed_before(void)
{
    const servo_active_from_t from = servo_active_from();
    return servo_active_printed()
           || wireless_printer_printed(servo_active_id())
           || from == SERVO_ACTIVE_SCAN || from == SERVO_ACTIVE_TYPED;
}

/** @brief PRINT's name: PRINT, or PRINT AGAIN. */
static ui_text_id_t print_label(void)
{
    return printed_before() ? TX_SVL_PRINT_AGAIN : TX_SVL_PRINT;
}

const char *servo_label_print_why(void)
{
    const print_view_t *v = print_ctl_view();
    if (servo_active_id() == NULL) {
        return TR(SVL_PRINT_NO_ID);
    }
    if (!wireless_printer_bound()) {
        return TR(PRN_LOCK_UNBOUND);
    }
    if (print_view_busy(v) || v->scanning) {
        return TR(PRN_LOCK_BUSY);
    }
    if (ui_router_status()->armed || servo_screen_run_active()) {
        return TR(PRN_LOCK_SAFE);
    }
    if (v->update) {
        return TR(PRN_LOCK_UPDATE);
    }
    return NULL;
}

/** @brief PRINT's value: the job in work, the active servo's last result,
 *         else the bound printer's name. */
static void print_value(char *out, size_t n)
{
    const print_view_t *v = print_ctl_view();
    const char *id = servo_active_id();
    const bool mine = v->kind == PRINT_REQ_LABEL && id != NULL
                      && strcmp(v->id, id) == 0 && v->phase != PRINT_IDLE;
    if (print_view_busy(v) || mine) {
        (void)wireless_printer_job_word(v, out, n);
        return;
    }
    wireless_printer_name(out, n);
}

/** @brief A tap on PRINT: the active servo's id and DUT NAME to the
 *         printer. */
static void print_tap(void)
{
    const char *why = servo_label_print_why();
    if (why == NULL
        && !print_ctl_label(servo_active_id(),
                            settings_text(SET_TEXT_DUT_NAME))) {
        why = TR(PRN_LOCK_BUSY);
    }
    if (why != NULL) {
        ui_router_set_alert(why);
    }
    ++s.sl_rev;
    servo_invalidate();
}

const ui_setlist_custom_t svl_print_row = {
    .label = TX_SVL_PRINT, .help = TX_SVL_PRINT_HELP,
    .value_text = print_value, .tap = print_tap,
    .locked = servo_label_print_why, .label_of = print_label,
};

/* ---------------------------------------------------------------- the tick */

/** @brief What the rows showed at the last tick, and the repaints asked. */
typedef struct {
    uint32_t key;       /**< the active servo, the ids, NEW ID, the print */
    bool     valid;     /**< a key was taken                              */
    uint32_t repaints;  /**< the repaints asked                           */
} seen_t;

/** @brief The one instance: nothing seen after power-on. */
static seen_t s_seen;

/**
 * @brief A key of what the rows show: it changes with the active servo, the
 *        card's ids and NEW ID's request.
 * @return The key.
 */
static uint32_t rows_key(void)
{
    servo_ids_t ids;
    const bool card = card_ids(&ids);
    /* FNV-1a over the ids, then the counts and the flags. */
    uint32_t h = 2166136261u;
    for (int i = 0; i < (int)ids.recent_n; ++i) {
        for (const char *p = ids.recent[i]; *p != '\0'; ++p) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        h = (h ^ 0x2Du) * 16777619u;
    }
    h = (h ^ ids.highest) * 16777619u;
    h = (h ^ servo_active_serial()) * 16777619u;
    h = (h ^ ((card ? 1u : 0u) | (new_busy() ? 2u : 0u)
              | (wireless_printer_bound() ? 4u : 0u))) * 16777619u;
    const print_view_t *v = print_ctl_view();
    h = (h ^ v->rev) * 16777619u;
    h = (h ^ v->jobs) * 16777619u;
    return h;
}

void servo_label_tick(void)
{
    const uint32_t key = rows_key();
    if (s_seen.valid && key == s_seen.key) {
        return;
    }
    const bool first = !s_seen.valid;
    s_seen.key = key;
    s_seen.valid = true;
    if (!first) {
        ++s_seen.repaints;
        ++s.sl_rev;
        servo_invalidate();
    }
}

uint32_t servo_label_repaints(void)
{
    return s_seen.repaints;
}
