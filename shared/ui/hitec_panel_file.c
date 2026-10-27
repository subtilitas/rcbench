/**
 * @file hitec_panel_file.c
 * @brief The servo files and backups of the Hitec programmer's overlay: the
 *        FILES list and its pages, the keyboard of a new file's name, CHECK
 *        THIS FILE, RESTORE of the session's backup, FACTORY DEFAULT, the
 *        lines a write page says of the backup file, and the texts of the
 *        messages of the files.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "card_state.h"
#include "hitec_reg.h"
#include "ui_card_text.h"
#include "ui_text.h"
#include "ui_textkey.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

_Static_assert(HITEC_CARD_FILES + 2u <= HP_CHOICE_MAX,
               "the FILES list fits HP_CHOICE_MAX entries");

/* ------------------------------------------------------------------ FILES */

void hpn_files_ask(void)
{
    hp.files_seq = hitec_prog_files_seq(&hp.prog);
    hp.files_wait = hitec_prog_files(&hp.prog, 0u) == HITEC_PROG_MSG_NONE;
}

/**
 * @brief The list of files as the card gave it, under the entry that
 *        saves; the next page's entry where the card holds more.
 */
static void files_open(void)
{
    const hitec_prog_t *p = &hp.prog;
    unsigned total = 0;
    unsigned first = 0;
    const unsigned n = hitec_prog_file_count(p, &total, &first);
    char title[64];

    if (total > n && n != 0u) {
        snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), first + 1u,
                 first + n, total);
    } else {
        snprintf(title, sizeof(title), "%s", TR(KP_FILES_TITLE));
    }
    hpn_choice_open(C_FILES, title);
    hpn_choice_add(TR(KP_FILE_SAVE), FL_SAVE);
    for (unsigned i = 0; i < n; ++i) {
        char label[HITEC_PANEL_LINE];
        const char *model = "";
        const char *name = hitec_prog_file_name(p, i, &model);

        snprintf(label, sizeof(label), "%-16s  %s", name, model);
        hpn_choice_add(label, (int)i);
    }
    if (total > n) {
        hpn_choice_add(TR(KP_FILES_NEXT), FL_NEXT);
    }
}

/**
 * @brief Opens the keyboard of a new file's name.
 * @param title its title: what it asks for, or why it asks again
 */
static void name_open(const char *title)
{
    ui_textkey_open(&hp.tk, hp.area, title, hp.name, HP_NAME_TYPED);
}

void hpn_files_choice(int value)
{
    hitec_prog_t *p = &hp.prog;

    if (value == FL_SAVE) {
        snprintf(hp.name, sizeof(hp.name), "%s", hitec_prog_preset_name(p));
        name_open(TR(KP_NAME_TITLE));
        return;
    }
    if (value == FL_NEXT) {
        unsigned total = 0;
        unsigned first = 0;
        const unsigned n = hitec_prog_file_count(p, &total, &first);
        /* Back to the first page after the last. */
        const unsigned next = first + n < total ? first + n : 0u;

        hp.files_seq = hitec_prog_files_seq(p);
        hp.files_wait = hitec_prog_files(p, next) == HITEC_PROG_MSG_NONE;
        return;
    }
    if (value >= 0) {
        (void)hitec_prog_load(p, hitec_prog_file_name(p, (unsigned)value,
                                                      NULL));
    }
}

void hpn_name_done(const char *name)
{
    snprintf(hp.name, sizeof(hp.name), "%s", name);
    /* A save refused at once, with no card or a request out, is said by
     * the message; the keyboard takes no empty name. */
    hp.name_wait = hitec_prog_save(&hp.prog, hp.name) == HITEC_PROG_MSG_NONE;
}

void hpn_file_answer(void)
{
    const hitec_prog_t *p = &hp.prog;
    const unsigned id = hitec_prog_message(p)->id;

    if (hp.files_wait && hitec_prog_files_seq(p) != hp.files_seq) {
        hp.files_wait = false;
        hpn_close_editors();
        files_open();
        return;
    }
    if (hp.files_wait && !hitec_prog_card_busy(p)) {
        /* The list did not come: the message says why. */
        hp.files_wait = false;
    }
    if (hp.name_wait && !hitec_prog_card_busy(p)) {
        hp.name_wait = false;
        if (id == HITEC_PROG_MSG_NAME_EXISTS) {
            name_open(TR(KP_NAME_EXISTS));
            return;
        }
        if (id == HITEC_PROG_MSG_NAME_BAD) {
            name_open(TR(KP_NAME_BAD));
            return;
        }
    }
    if (hitec_prog_load_waits(p) && !hp.page.open) {
        hpn_page_load();
    }
}

/* ------------------------------------------------------------------ pages */

/**
 * @brief The name of a layout's servos: "D series", "HS-5xxx", "HS-7xxx".
 * @param layout hitec_layout_t
 * @return the name
 */
static const char *layout_name(unsigned layout)
{
    return layout == (unsigned)HITEC_LAYOUT_D     ? TR(HP_SERIES_D)
         : layout == (unsigned)HITEC_LAYOUT_HS7   ? "HS-7xxx"
                                                  : "HS-5xxx";
}

void hpn_page_load(void)
{
    hpn_page_load_at(0);
}

void hpn_page_load_at(int from)
{
    const hitec_prog_t *p = &hp.prog;
    /* Opened while a load waits: its image, against the servo read. */
    const hitec_image_t *to = &p->load_img;
    bool other = false;
    const uint16_t rules = hitec_prog_load_pending(p, &other);
    char line[HITEC_PANEL_LINE];
    int fixed = 1;

    hpn_page_open(W_LOAD, false, TR(KP_FILE_TITLE), TR(KP_HOLD_LOAD));
    {
        const unsigned c = hitec_prog_changes_in(p, to);

        if (c == 1u) {
            snprintf(line, sizeof(line), TR(KP_FILE_DIFF_ONE),
                     hitec_prog_card_name(p));
        } else {
            snprintf(line, sizeof(line), TR(KP_FILE_DIFF),
                     hitec_prog_card_name(p), c);
        }
        hpn_page_add(HITEC_PROG_INFO, line);
    }
    if (other) {
        char model[HITEC_CARD_MODEL_MAX + 1u];

        hitec_card_model_text(&p->servo, model);
        snprintf(line, sizeof(line), TR(HP_LOAD_MODEL), p->load_model,
                 model);
        hpn_page_add(HITEC_PROG_WARN, line);
        hpn_page_add(HITEC_PROG_WARN, TR(HP_LOAD_MODEL_2));
        fixed += 2;
    }
    if (p->load_raw != 0u) {
        /* A backup file of this servo: what no setting edits comes back. */
        snprintf(line, sizeof(line), TR(HP_LOAD_RAW), (unsigned)p->load_raw);
        hpn_page_add(HITEC_PROG_INFO, line);
        fixed += 1;
    }
    for (unsigned r = 0; r < (unsigned)HITEC_RULE_COUNT; ++r) {
        fixed += (rules & HITEC_RULE_BIT(r)) != 0u ? 1 : 0;
    }
    hpn_page_values(to, HP_PAGE_LINES - fixed < HP_PAGE_VALUES
                            ? HP_PAGE_LINES - fixed
                            : HP_PAGE_VALUES);
    hpn_page_rules(rules);
    /* The values give way to the warnings; warnings that still do not fit
     * go to a next part, behind HOLD 2s TO CONTINUE: every warning stands
     * above a hold before the one that loads. */
    if (!hpn_page_fit() || from > 0) {
        const int all = hp.page.count;
        const int end = hpn_page_part(from);

        if (end < all) {
            hp.page.kind = W_LOAD_MORE;
            hp.page.next = end;
            snprintf(hp.page.apply, sizeof(hp.page.apply), "%s",
                     TR(KP_HOLD_NEXT));
        }
    }
}

void hpn_page_factory(void)
{
    hitec_image_t to;

    hpn_page_open(W_FACTORY, true, TR(HP_FACTORY), TR(KP_HOLD_WRITE));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_FACTORY_1));
    hpn_page_add(HITEC_PROG_INFO, TR(HP_FACTORY_2));
    /* The write gate has passed: a classic servo is read. */
    if (hitec_prog_factory_image(&hp.prog, &to)) {
        const unsigned raw = hitec_prog_raw_cells(&hp.prog, &to);

        hpn_page_values(&to, 4);
        if (raw != 0u) {
            char line[HITEC_PANEL_LINE];

            snprintf(line, sizeof(line), TR(HP_FACTORY_RAW), raw);
            hpn_page_add(HITEC_PROG_INFO, line);
        }
    }
    hpn_page_add(HITEC_PROG_DANGER, TR(KP_HORN_FREE));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    hpn_page_saves();
    (void)hpn_page_fit();
}

void hpn_page_backup(void)
{
    hitec_image_t to;

    hpn_page_open(W_BACKUP, true, TR(KP_RESTORE_TITLE), TR(KP_HOLD_RESTORE));
    hpn_page_add(HITEC_PROG_INFO, TR(KP_FROM_SESSION));
    hpn_page_add(HITEC_PROG_WARN, TR(KP_RESTORE_UNCHECKED));
    /* The write gate has passed: the backup differs from the servo. */
    if (hitec_prog_backup_image(&hp.prog, &to)) {
        const unsigned raw = hitec_prog_raw_cells(&hp.prog, &to);

        hpn_page_values(&to, HP_PAGE_VALUES - 1);
        if (raw != 0u) {
            char line[HITEC_PANEL_LINE];

            snprintf(line, sizeof(line), TR(HP_RESTORE_RAW), raw);
            hpn_page_add(HITEC_PROG_INFO, line);
        }
    }
    hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_2));
    hpn_page_saves();
    (void)hpn_page_fit();
}

void hpn_page_saves(void)
{
    const hitec_prog_t *p = &hp.prog;

    if (hitec_prog_backup_due(p)) {
        hpn_page_add(HITEC_PROG_INFO, TR(KP_WRITE_BACKUP));
    } else if (p->have_bk && !p->bk_saved && !p->f.card) {
        hpn_page_add(HITEC_PROG_WARN,
                     ui_card_text((card_state_t)p->f.card_state,
                                  UI_CARD_AT_KP_NO_FILE));
        hpn_page_add(HITEC_PROG_WARN, TR(KP_NO_FILE_2));
        hpn_page_add(HITEC_PROG_WARN, TR(KP_NO_FILE_3));
    }
}

/* ------------------------------------------------------------- the texts */

const char *hpn_backup_note(void)
{
    const hitec_prog_t *p = &hp.prog;
    const char *name = hitec_prog_backup_name(p);

    if (name[0] != '\0') {
        static char line[HITEC_PANEL_LINE];

        snprintf(line, sizeof(line), TR(KP_BACKUP_FILE), name);
        return line;
    }
    if (hitec_prog_backup_ram_only(p)) {
        return ui_card_text((card_state_t)p->f.card_state,
                            UI_CARD_AT_KP_NO_BACKUP);
    }
    return NULL;
}

/**
 * @brief The line of the values a file holds that are not loaded.
 * @param out   the line
 * @param count the values
 * @return 1 for a line, 0 for none
 */
static int skipped_line(char *out, unsigned count)
{
    if (count == 0u) {
        return 0;
    }
    if (count == 1u) {
        snprintf(out, HITEC_PANEL_LINE, "%s", TR(HP_LOAD_SKIPPED_ONE));
    } else {
        snprintf(out, HITEC_PANEL_LINE, TR(HP_LOAD_SKIPPED), count);
    }
    return 1;
}

/**
 * @brief The name of a series.
 * @param series hitec_family_t
 * @return "D SERIES" or "CLASSIC", in the language in force
 */
static const char *series_text(unsigned series)
{
    return series == (unsigned)HITEC_D ? TR(HP_SERIES_D) : TR(HP_SERIES_C);
}

int hpn_file_lines(const hitec_prog_msg_t *m,
                   char out[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE])
{
    const hitec_prog_t *p = &hp.prog;
    const char *name = hitec_prog_card_name(p);

    switch (m->id) {
    case HITEC_PROG_MSG_SAVED:
        snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_SAVED), name);
        return 1;
    case HITEC_PROG_MSG_CARD_NONE:
        snprintf(out[0], HITEC_PANEL_LINE, "%s",
                 ui_card_text((card_state_t)m->arg, UI_CARD_AT_KP_MSG));
        return 1;
    case HITEC_PROG_MSG_LOAD_DAMAGED:
        snprintf(out[0], HITEC_PANEL_LINE, "%s",
                 m->arg == (unsigned)HITEC_CARD_ERR_TRUNCATED
                     ? TR(HP_LOAD_END)
                     : TR(KP_M_LOAD_CHECKSUM));
        snprintf(out[1], HITEC_PANEL_LINE, "%s", TR(HP_LOAD_HAND));
        return 2;
    case HITEC_PROG_MSG_LOAD_LINE:
        snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_LOAD_LINE),
                 (unsigned)m->arg);
        snprintf(out[1], HITEC_PANEL_LINE, "%s", TR(HP_LOAD_HAND));
        return 2;
    case HITEC_PROG_MSG_LOAD_SERIES:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_LOAD_SERIES),
                 series_text(m->arg), series_text(hitec_prog_series(p)));
        return 1;
    case HITEC_PROG_MSG_LOAD_LAYOUT:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_LOAD_LAYOUT),
                 layout_name(m->arg), layout_name(m->reg));
        return 1;
    case HITEC_PROG_MSG_LOAD_SAME:
        snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_LOAD_SAME), name);
        return 1 + skipped_line(out[1], m->arg);
    case HITEC_PROG_MSG_LOADED:
    case HITEC_PROG_MSG_LOADED_RAW:
        if (m->arg == 1u) {
            snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_LOADED_ONE), name);
        } else {
            snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_LOADED), name,
                     (unsigned)m->arg);
        }
        if (m->id == HITEC_PROG_MSG_LOADED_RAW) {
            snprintf(out[1], HITEC_PANEL_LINE, TR(HP_LOAD_RAW),
                     (unsigned)m->reg);
            return 2;
        }
        return 1 + skipped_line(out[1], m->reg);
    default:
        return 0;
    }
}
