/**
 * @file
 * @brief The SERVO LABEL rows (servo_label_rows.h): SERVO ID's value and
 *        choice list, TYPE ID's keyboard, NEW ID's value, tap and locks.
 *
 * The rows are custom rows of the SERVO lists (ui_setlist.h); ID PREFIX is
 * the string row of SET_TEXT_SERVO_PREFIX, which the list types itself.
 * The row PRINT that a printed label needs goes under NEW ID in
 * SERVO_LABEL_ROWS.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_label_rows.h"

#include <stdio.h>
#include <string.h>

#include "scan_flow.h"
#include "servo_active.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_widgets.h"

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

/** @brief SERVO ID's value: the active id, or NONE. */
static void id_value(char *out, size_t n)
{
    const char *id = servo_active_id();
    snprintf(out, n, "%s", (id != NULL) ? id : TR(SVL_ID_NONE));
}

int servo_label_choices(void)
{
    servo_ids_t ids;
    (void)card_ids(&ids);
    return 2 + (int)ids.recent_n;
}

/** @brief Choice @p i: NONE, the ids highest first, TYPE ID last. */
static const char *id_choice(int i)
{
    static servo_ids_t ids;
    (void)card_ids(&ids);
    if (i <= 0) {
        return TR(SVL_ID_NONE);
    }
    if (i <= (int)ids.recent_n) {
        return ids.recent[i - 1];
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
    servo_ids_t ids;
    (void)card_ids(&ids);
    for (int i = 0; i < (int)ids.recent_n; ++i) {
        if (strcmp(ids.recent[i], id) == 0) {
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

/** @brief Choice @p i tapped: NONE, an id of the card, or TYPE ID. */
static void id_pick(int i)
{
    const bool run = servo_screen_run_active();
    servo_ids_t ids;
    (void)card_ids(&ids);
    if (i > (int)ids.recent_n) {
        type_open();
        return;
    }
    const bool ok = (i <= 0) ? servo_active_clear(run)
                             : servo_active_set(ids.recent[i - 1],
                                                SERVO_ACTIVE_PICK, run);
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

const char *servo_label_new_why(void)
{
    if (servo_screen_run_active()) {
        return TR(SVL_RUN_ACTIVE);
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
 *         answer makes the servo active. */
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
