/**
 * @file
 * @brief The ESC signal-wire session: its state, its stages, its page set and the
 *        card's flow.  See esc_wire_panel.h.
 *
 * The rows, the draft and the editors are esc_wire_edit.c, the words
 * esc_wire_text.c, the presses esc_wire_touch.c and the pages drawn
 * esc_wire_draw.c, with esc_wire_int.h between them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_panel.h"

#include <stdio.h>
#include <string.h>

#include "esc_wire_int.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "port_view.h"
#include "supply_screen.h"
#include "ui_theme.h"

ewp_state_t ewp_s;

/** @brief Not part of the state: a start of the screens leaves it. */
static esc_prog_gate_t *s_gate;

/* ------------------------------------------------------------------ stages */

/**
 * @brief Where the page and the model are, with no write waiting for the card in it.
 * @return The stage.
 */
static ewp_stage_t page_stage(void)
{
    const esc_prog_t *m = &ewp_s.prog;
    const esc_link_readout_t *rd = &m->f.rd;
    const bool busy = esc_prog_busy(m);
    const bool stood = (rd->flags & (LINK_EW_F_STOPPED | LINK_EW_F_FAULT)) != 0u;

    if (!rd->known || rd->state == (uint8_t)LINK_ESC_FREE) {
        return busy ? ESC_WIRE_ENTERING : ESC_WIRE_CONNECT;
    }
    if (!rd->ours && !busy) {
        return rd->state == (uint8_t)LINK_ESC_NOT_BACK ? ESC_WIRE_NOT_BACK
                                                       : ESC_WIRE_OTHER;
    }
    switch (rd->state) {
    case LINK_ESC_NOT_BACK:
        return ESC_WIRE_NOT_BACK;
    case LINK_ESC_HAND_WAIT:
        return ESC_WIRE_HAND;
    case LINK_ESC_READY:
        if (busy) {
            return (rd->flags & LINK_EW_F_HAVE_READ) != 0u ? ESC_WIRE_RUNNING
                                                           : ESC_WIRE_ENTERING;
        }
        /* Not before the copy of this read is here: an edit would be lost to it. */
        if ((rd->flags & LINK_EW_F_HAVE_READ) != 0u
            && (m->f.image.gen == 0u || m->f.image.reads != rd->reads)) {
            return ESC_WIRE_RUNNING;
        }
        return ESC_WIRE_SESSION;
    case LINK_ESC_RUN:
        return (rd->flags & LINK_EW_F_HAVE_READ) != 0u ? ESC_WIRE_RUNNING
                                                       : ESC_WIRE_ENTERING;
    case LINK_ESC_OPEN:
        if (busy) {
            return ESC_WIRE_ENTERING;
        }
        return stood ? ESC_WIRE_STOPPED : ESC_WIRE_PIN_HELD;
    default:
        return ESC_WIRE_ENTERING;
    }
}

ewp_stage_t ewp_stage(void)
{
    const ewp_stage_t st = page_stage();

    /* From the hold to the write's outcome: ABORT and STOP, nothing else. */
    return st == ESC_WIRE_SESSION && ewp_s.w_step != EWP_W_NONE ? ESC_WIRE_RUNNING : st;
}

esc_wire_stage_t esc_wire_panel_stage(void)
{
    return ewp_stage();
}

void ewp_note(const char *s, bool warn)
{
    snprintf(ewp_s.note, sizeof(ewp_s.note), "%s", s);
    ewp_s.note_warn = warn;
    ++ewp_s.rev;
}

/* -------------------------------------------------------------------- card */

void ewp_card_ask(uint8_t op)
{
    ewp_s.card.op = op;
    ewp_s.card.id = ++ewp_s.card_id;
    ewp_s.card_want = true;
    ewp_s.card_look = true;
}

bool esc_wire_panel_card_look(void)
{
    const bool look = ewp_s.card_look;

    ewp_s.card_look = false;
    return look;
}

bool esc_wire_panel_card_take(esc_card_req_t *out)
{
    if (!ewp_s.card_want || ewp_s.card_out || out == NULL) {
        return false;
    }
    *out = ewp_s.card;
    ewp_s.card_want = false;
    ewp_s.card_out = true;
    return true;
}

/**
 * @brief The write itself, with its record on the card or the operator's word that
 *        none goes.
 */
static void write_now(void)
{
    (void)esc_prog_write(&ewp_s.prog, ewp_s.w_draft, ewp_s.prog.f.image.len);
    ewp_s.w_step = EWP_W_NONE;
    memset(ewp_s.w_draft, 0, sizeof(ewp_s.w_draft));
}

/**
 * @brief No write goes on: its step and snapshot go, and an answer still out finds
 *        another request's number and acts on nothing.
 */
static void write_give_up(void)
{
    ewp_s.w_step = EWP_W_NONE;
    memset(ewp_s.w_draft, 0, sizeof(ewp_s.w_draft));
    ewp_s.card_want = false;
    ++ewp_s.card_id;
}

void ewp_abort(void)
{
    if (ewp_s.w_step != EWP_W_NONE) {
        write_give_up();
        ewp_note(TR(EW_WRITE_GIVEN_UP), true);
        return;
    }
    esc_prog_abort(&ewp_s.prog);
}

bool ewp_card_busy(void)
{
    return ewp_s.card_want || ewp_s.card_out;
}

const char *ewp_no_backup_text(void)
{
    switch (esc_prog_no_backup(&ewp_s.prog)) {
    case ESC_PROG_NO_BACKUP_ERASED:
        return TR(EW_ERASED_NO_BACKUP);
    case ESC_PROG_NO_BACKUP_MUSIC:
        return TR(EW_MUSIC_NO_BACKUP);
    case ESC_PROG_NO_BACKUP_FORMAT:
        return TR(EW_FORMAT_NO_BACKUP);
    default:
        return TR(EW_NOT_READ);
    }
}

void ewp_write_go(void)
{
    const esc_prog_t *m = &ewp_s.prog;

    if (ewp_s.no_card_ok) {
        write_now();
        return;
    }
    ewp_s.card.len = esc_prog_record_text(m, ewp_s.w_draft, ewp_s.card.text,
                                          sizeof(ewp_s.card.text));
    if (ewp_s.card.len == 0u) {
        ewp_s.w_step = EWP_W_NONE;
        ewp_note(TR(EW_RECORD_FAILED), true);
        return;
    }
    ewp_s.w_step = EWP_W_RECORD;
    ewp_card_ask(ESC_CARD_RECORD);
}

/**
 * @brief The backup to the card, for a write or by hand.
 * @return false with no backup held.
 */
static bool backup_ask(void)
{
    ewp_s.card.len = esc_prog_backup_text(&ewp_s.prog, ewp_s.card.text,
                                          sizeof(ewp_s.card.text));
    if (ewp_s.card.len == 0u) {
        return false;
    }
    ewp_card_ask(ESC_CARD_SAVE);
    return true;
}

/**
 * @brief What a failed request says.
 * @param r An esc_card_result_t.
 * @return The text.
 */
static const char *card_words(uint8_t r)
{
    switch (r) {
    case ESC_CARD_DONE_NO_CARD:
        return TR(EW_CARD_NONE);
    case ESC_CARD_DONE_FULL:
        return TR(EW_CARD_FULL);
    case ESC_CARD_DONE_DENIED:
        return TR(EW_CARD_DENIED);
    case ESC_CARD_DONE_WRITE:
        return TR(EW_CARD_WRITE);
    case ESC_CARD_DONE_MISSING:
        return TR(EW_CARD_MISSING);
    case ESC_CARD_DONE_LARGE:
        return TR(EW_CARD_LARGE);
    default:
        return TR(EW_CARD_READ);
    }
}

/**
 * @brief The question a write asks when no backup or record reaches the card.
 * @param why What the card said.
 */
static void ask_no_backup(const char *why)
{
    ewp_s.w_step = EWP_W_NONE;
    ewp_page_open(EWP_PAGE_NO_BACKUP);
    ewp_page_add(why);
    ewp_page_add(TR(EW_NO_BACKUP_1));
    ewp_page_add(TR(EW_NO_BACKUP_2));
}

/**
 * @brief A SAVE answered.
 * @param d The answer.
 */
static void saved(const esc_card_done_t *d)
{
    if (d->result == ESC_CARD_DONE_OK) {
        char line[EWP_LINE];

        esc_prog_backup_settled(&ewp_s.prog);
        snprintf(line, sizeof(line), TR(EW_BACKUP_SAVED), d->name);
        ewp_note(line, false);
        if (ewp_s.w_step == EWP_W_BACKUP) {
            ewp_write_go();
        }
    } else if (ewp_s.w_step == EWP_W_BACKUP) {
        ask_no_backup(card_words(d->result));
    } else {
        ewp_note(card_words(d->result), true);
    }
}

/**
 * @brief A RECORD answered: a card that refuses it stops the write and says so, as
 *        no record means no way to see afterwards what it changed.
 * @param d The answer.
 */
static void recorded(const esc_card_done_t *d)
{
    if (ewp_s.w_step != EWP_W_RECORD) {
        return;
    }
    if (d->result == ESC_CARD_DONE_OK) {
        write_now();
    } else {
        ask_no_backup(card_words(d->result));
    }
}

/**
 * @brief Whether a list or a load has to stay out: a hold page or an editor is open,
 *        or the session is no longer on its rows.
 * @return true then.
 */
static bool under_page(void)
{
    return ewp_s.page != EWP_PAGE_NONE || ewp_s.kp.open
           || ewp_s.ch_what != EWP_CH_NONE || ewp_stage() != ESC_WIRE_SESSION;
}

/**
 * @brief A LIST answered: the choice list, one entry to save and the next page's.
 * @param d The answer.
 */
static void listed(const esc_card_done_t *d)
{
    if (d->result != ESC_CARD_DONE_OK) {
        ewp_note(card_words(d->result), true);
        return;
    }
    if (under_page()) {
        ewp_note(TR(EW_CARD_LATE), true);
        return;
    }
    ewp_s.files_total = d->total;
    ewp_editors_close();
    ewp_s.ch_what = EWP_CH_FILES;
    if (!ewp_s.files_for_restore) {
        ewp_choice_add(TR(EW_FILE_SAVE), -1);
    }
    for (unsigned i = 0u; i < d->count; ++i) {
        memcpy(ewp_s.names[i], d->names[i], sizeof(ewp_s.names[i]));
        ewp_choice_add(ewp_s.names[i], (int)i);
    }
    if ((unsigned)ewp_s.files_first + d->count < d->total) {
        ewp_choice_add(TR(EW_FILES_NEXT), EWP_CHOICE_NEXT);
    }
}

/**
 * @brief A LOAD answered: its text against the ESC, a draft from it, or the restore's
 *        hold.
 * @param d The answer.
 */
static void loaded(const esc_card_done_t *d)
{
    esc_prog_t *m = &ewp_s.prog;

    if (d->result != ESC_CARD_DONE_OK) {
        ewp_note(card_words(d->result), true);
        return;
    }
    if (under_page()) {
        ewp_note(TR(EW_CARD_LATE), true);
        return;
    }
    memcpy(ewp_s.file_text, d->text, d->len + 1u);
    ewp_s.file_len = d->len;
    if (ewp_s.files_for_restore) {
        char line[EWP_LINE];

        ewp_page_open(EWP_PAGE_RESTORE);
        snprintf(line, sizeof(line), TR(EW_RESTORE_1), ewp_s.file_name);
        ewp_page_add(line);
        ewp_page_add(TR(EW_RESTORE_2));
        ewp_page_add(TR(EW_RESTORE_3));
        return;
    }
    unsigned line = 0u;

    if (esc_prog_file_load(m, ewp_s.file_text, ewp_s.file_len, &line)
        == ESC_PROG_MSG_LOADED) {
        uint16_t len = 0u;
        const uint8_t *draft = esc_prog_file_draft(m, &len);

        if (draft != NULL) {
            memcpy(ewp_s.draft, draft, len);
        }
    }
}

void esc_wire_panel_card_done(const esc_card_done_t *d)
{
    if (d == NULL) {
        return;
    }
    ewp_s.card_out = false;
    if (d->id == ewp_s.card_id) {
        switch (d->op) {
        case ESC_CARD_SAVE:
            saved(d);
            break;
        case ESC_CARD_RECORD:
            recorded(d);
            break;
        case ESC_CARD_LIST:
            listed(d);
            break;
        case ESC_CARD_LOAD:
            loaded(d);
            break;
        default:
            break;
        }
    } else if (ewp_s.w_step != EWP_W_NONE && !ewp_s.card_want) {
        /* The request of this write has no answer to come: it does not wait on. */
        write_give_up();
        ewp_note(TR(EW_WRITE_GIVEN_UP), true);
    }
    ++ewp_s.rev;
}

void ewp_files_open(bool restore)
{
    ewp_s.files_for_restore = restore;
    ewp_s.card.first = ewp_s.files_first;
    ewp_card_ask(ESC_CARD_LIST);
}

void ewp_file_chosen(int value)
{
    if (value == EWP_CHOICE_NEXT) {
        const unsigned next = (unsigned)ewp_s.files_first + ESC_CARD_FILES;

        ewp_s.files_first = next < ewp_s.files_total ? (uint16_t)next : 0u;
        ewp_files_open(ewp_s.files_for_restore);
    } else if (value < 0) {
        if (!backup_ask()) {
            ewp_note(ewp_no_backup_text(), true);
        }
    } else if (value < (int)ESC_CARD_FILES) {
        snprintf(ewp_s.file_name, sizeof(ewp_s.file_name), "%s", ewp_s.names[value]);
        snprintf(ewp_s.card.name, sizeof(ewp_s.card.name), "%s", ewp_s.file_name);
        ewp_card_ask(ESC_CARD_LOAD);
    }
}

/* ---------------------------------------------------------------- the hold */

void ewp_connect_values(esc_prog_connect_t *c)
{
    const bool hand = ewp_s.power == (uint8_t)LINK_ESC_BY_HAND;

    memset(c, 0, sizeof(*c));
    if (ewp_s.target_at < ewp_s.target_count) {
        c->target = ewp_s.targets[ewp_s.target_at].target;
    }
    c->power.power = ewp_s.power;
    c->power.mv = hand ? 0u : ewp_s.mv;
    c->power.ma = hand ? 0u : ewp_s.ma;
    c->power.hold_ms = ewp_s.hold_ms;
    c->power.off_ms = ewp_s.off_ms;
}

/**
 * @brief CONNECT: the target, the power and the values on the page.
 */
static void connect_now(void)
{
    esc_prog_connect_t c;

    if (ewp_s.target_at >= ewp_s.target_count) {
        ewp_note(TR(EW_NO_TARGET), true);
        return;
    }
    ewp_connect_values(&c);
    ewp_s.note[0] = '\0';
    ewp_s.no_card_ok = false;
    write_give_up();
    (void)esc_prog_connect(&ewp_s.prog, &c);
}

/**
 * @brief ENTER AGAIN: the values on the page, the pin it holds.
 */
static void enter_again_now(void)
{
    esc_prog_connect_t c;

    ewp_connect_values(&c);
    ewp_s.note[0] = '\0';
    ewp_s.no_card_ok = false;
    write_give_up();
    (void)esc_prog_enter_again(&ewp_s.prog, &c);
}

void ewp_hold_apply(void)
{
    esc_prog_t *m = &ewp_s.prog;
    const int page = ewp_s.page;

    ewp_s.page = EWP_PAGE_NONE;
    ewp_s.note[0] = '\0';
    switch (page) {
    case EWP_PAGE_NONE:
        if (ewp_stage() == ESC_WIRE_PIN_HELD) {
            enter_again_now();
        } else if (ewp_stage() == ESC_WIRE_CONNECT) {
            connect_now();
        }
        break;
    case EWP_PAGE_WRITE:
        if (esc_prog_backup_state(m) == ESC_PROG_BACKUP_NONE) {
            ewp_note(ewp_no_backup_text(), true);
        } else if (esc_prog_backup_state(m) == ESC_PROG_BACKUP_OWED) {
            if (backup_ask()) {
                ewp_s.w_step = EWP_W_BACKUP;
            } else {
                ewp_note(ewp_no_backup_text(), true);
            }
        } else {
            ewp_write_go();
        }
        break;
    case EWP_PAGE_NO_BACKUP:
        ewp_s.no_card_ok = true;
        esc_prog_backup_settled(m);
        ewp_write_go();
        break;
    case EWP_PAGE_LEAVE:
        write_give_up();
        (void)esc_prog_leave(m);
        break;
    case EWP_PAGE_RELEASE:
        (void)esc_prog_close(m);
        break;
    case EWP_PAGE_RESTORE:
        (void)esc_prog_file_restore(m, ewp_s.file_text, ewp_s.file_len);
        break;
    default:
        break;
    }
    ++ewp_s.rev;
}

/* ---------------------------------------------------------------- the feed */

/**
 * @brief What a render shows of the page, as one number: a count in a stage that
 *        shows one moves it 10 times a second, and nothing moves it at rest.
 * @return The hash.
 */
static uint32_t signature(void)
{
    const esc_prog_t *m = &ewp_s.prog;
    const esc_link_readout_t *rd = &m->f.rd;
    const ewp_stage_t st = ewp_stage();
    const uint32_t ticks = (st == ESC_WIRE_ENTERING || st == ESC_WIRE_HAND
                            || st == ESC_WIRE_RUNNING)
                               ? rd->t_ms / 100u : 0u;
    const uint32_t v[] = {
        (uint32_t)st, rd->known, rd->state, rd->flags, rd->line, rd->gpio, rd->fw,
        rd->layout, rd->why, rd->result, rd->ses, rd->target, m->f.image.gen,
        (uint32_t)m->msg, m->detail, m->backup_state, ticks, m->f.stopped,
        m->f.armed, ewp_s.mv, ewp_s.ma, ewp_s.target_count,
    };
    uint32_t h = 2166136261u;

    for (size_t i = 0u; i < sizeof(v) / sizeof(v[0]); ++i) {
        h = (h ^ v[i]) * 16777619u;
    }
    return h;
}

void esc_wire_panel_gate(esc_prog_gate_t *g)
{
    s_gate = g;
    esc_prog_init(&ewp_s.prog);
    esc_prog_gate_set(&ewp_s.prog, g);
}

void esc_wire_panel_feed(const esc_prog_feed_t *f)
{
    esc_prog_t *m = &ewp_s.prog;

    esc_prog_feed(m, f);
    /* What the model says newer than the note is what shows. */
    if (m->msg != ewp_s.msg_seen || m->detail != ewp_s.detail_seen) {
        ewp_s.msg_seen = m->msg;
        ewp_s.detail_seen = m->detail;
        ewp_s.note[0] = '\0';
    }
    if (m->f.image.gen != ewp_s.draft_gen) {
        ewp_s.draft_gen = m->f.image.gen;
        memcpy(ewp_s.draft, m->f.image.page, sizeof(ewp_s.draft));
        ++ewp_s.rev;
    }
    if (esc_prog_backup_state(m) == ESC_PROG_BACKUP_NONE) {
        ewp_s.no_card_ok = false;
    }
    if (!ewp_s.supply_typed) {
        /* SUPPLY's values, never a default of this session's. */
        const uint16_t mv = (uint16_t)(supply_screen_set_v() * 1000.0f + 0.5f);
        const uint16_t ma = (uint16_t)(supply_screen_set_i() * 1000.0f + 0.5f);

        /* A hold confirms what the page shows: a value that moves under it ends it. */
        if ((mv != ewp_s.mv || ma != ewp_s.ma) && ewp_s.hold_down
            && ewp_s.page == EWP_PAGE_NONE) {
            ewp_cancel();
        }
        ewp_s.mv = mv;
        ewp_s.ma = ma;
    }
    ewp_tabs_follow();
    /* A stop, a supply or a link gone: what was open answers to a stage that is over. */
    const ewp_stage_t now = page_stage();

    if (now != ewp_s.stage_seen) {
        ewp_s.stage_seen = now;
        ewp_cancel();
        ewp_editors_close();
        ewp_s.page = EWP_PAGE_NONE;
        if (ewp_s.w_step != EWP_W_NONE) {
            write_give_up();
        }
    }
    const uint32_t sig = signature();

    if (sig != ewp_s.sig) {
        ewp_s.sig = sig;
        ++ewp_s.rev;
    }
}

bool esc_wire_panel_next_cmd(esc_prog_cmd_t *out)
{
    return esc_prog_next_cmd(&ewp_s.prog, out);
}

bool esc_wire_panel_holds_port(void)
{
    return esc_prog_holds_port(&ewp_s.prog);
}

bool esc_wire_panel_idle(void)
{
    return esc_prog_idle(&ewp_s.prog);
}

void esc_wire_panel_set_targets(const esc_wire_target_t *t, unsigned n, bool known)
{
    const unsigned count = n < ESC_WIRE_TARGETS ? n : ESC_WIRE_TARGETS;

    if (count != ewp_s.target_count || known != ewp_s.targets_known
        || (count != 0u && t != NULL
            && memcmp(t, ewp_s.targets, count * sizeof(*t)) != 0)) {
        if (t != NULL && count != 0u) {
            memcpy(ewp_s.targets, t, count * sizeof(*t));
        }
        ewp_s.target_count = (uint8_t)count;
        ewp_s.targets_known = known;
        if (ewp_s.target_at >= count) {
            ewp_s.target_at = 0u;
        }
        ++ewp_s.rev;
    }
}

void esc_wire_panel_read_binding(const uint16_t *slots, const uint16_t *cfg,
                                 bool known)
{
    esc_wire_target_t t[ESC_WIRE_TARGETS];
    unsigned n = 0u;

    for (uint8_t sl = 0u; known && slots != NULL && cfg != NULL && sl < LINK_OUT_SLOTS
                          && n < ESC_WIRE_TARGETS; ++sl) {
        const uint16_t *r = &slots[(size_t)sl * LINK_OS_STRIDE];
        uint16_t only[LINK_OS_COUNT] = { 0 };

        if (r[LINK_OS_DRIVER] == (uint16_t)LINK_DRIVER_NONE || r[LINK_OS_PIN] >= 64u) {
            continue;
        }
        memcpy(only, r, (size_t)LINK_OS_STRIDE * sizeof(only[0]));
        if (outputs_role_channels(only, cfg, OUT_ROLE_THROTTLE) != 0u) {
            t[n].target = sl;
            t[n].gpio = (uint8_t)r[LINK_OS_PIN];
            ++n;
        }
    }
    const port_view_t *v = port_view_get();
    const uint8_t ports = motor_dshot_ports();

    for (uint8_t p = 0u; p < LINK_PG_PORTS_MAX && n < ESC_WIRE_TARGETS; ++p) {
        if ((ports & (1u << p)) != 0u) {
            t[n].target = (uint16_t)(LINK_EW_TARGET_PORT | p);
            t[n].gpio = v->pin[p];
            ++n;
        }
    }
    esc_wire_panel_set_targets(t, n, known);
}

bool esc_wire_panel_target_text(char *out, size_t size)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    uint16_t target;
    uint8_t gpio;

    if (rd->known && rd->held && rd->gpio != 0xFFu) {
        target = rd->target;
        gpio = rd->gpio;
    } else if (ewp_s.target_at < ewp_s.target_count) {
        target = ewp_s.targets[ewp_s.target_at].target;
        gpio = ewp_s.targets[ewp_s.target_at].gpio;
    } else {
        snprintf(out, size, "%s", ewp_s.targets_known ? TR(EW_NO_PIN)
                                                      : TR(EW_NOT_READ_YET));
        return true;
    }
    snprintf(out, size,
             (target & LINK_EW_TARGET_PORT) != 0u ? TR(EW_TARGET_PORT)
                                                  : TR(EW_TARGET_SLOT),
             (unsigned)(target & 0xFFu) + 1u, (unsigned)gpio);
    return false;
}

/* ------------------------------------------------------------ the page set */

/**
 * @brief Start over: closed, nothing held, laid out in @p area.
 * @param area The area.
 */
static void esc_wire_ps_init(gfx_rect_t area)
{
    memset(&ewp_s, 0, sizeof(ewp_s));
    ewp_s.area = area;
    ewp_s.hold_ms = ESC_WIRE_HOLD_DEFAULT_MS;
    ewp_s.off_ms = ESC_WIRE_OFF_DEFAULT_MS;
    ewp_s.tab_set = -1;
    ewp_s.page = EWP_PAGE_NONE;
    esc_prog_init(&ewp_s.prog);
    esc_prog_gate_set(&ewp_s.prog, s_gate);
}

/** @brief The host screen was entered. */
static void esc_wire_ps_entry(void)
{
    ++ewp_s.rev;
}

/** @brief The row was tapped. */
static void esc_wire_ps_open(void)
{
    ewp_s.open = true;
    ewp_s.note[0] = '\0';
    ++ewp_s.rev;
}

/**
 * @brief Whether the session shows.
 * @return true while open or while a pin is held: a session is not left behind.
 */
static bool esc_wire_ps_is_open(void)
{
    return ewp_s.open;
}

/** @brief Home tag or BACK: see ewp_back(). */
static void esc_wire_ps_back(void)
{
    ewp_back();
}

/**
 * @brief Whether the session holds a pin.
 * @return true then.
 */
static bool esc_wire_ps_holds(void)
{
    return esc_wire_panel_holds_port();
}

/**
 * @brief The bench's arm.
 * @param armed Armed, or asked to arm.
 */
static void esc_wire_ps_armed(bool armed)
{
    ewp_s.armed = armed;
}

/**
 * @brief A hold's time.
 * @param dt_s Seconds since the last.
 */
static void esc_wire_ps_tick(float dt_s)
{
    ewp_tick(dt_s);
}

/**
 * @brief Whether a press of this finger is the session's.
 * @param id The finger.
 * @return true then.
 */
static bool esc_wire_ps_has_press(uint8_t id)
{
    return ewp_s.have && ewp_s.id == id;
}

/**
 * @brief Changes when a render would show something else.
 * @return The count.
 */
static uint32_t esc_wire_ps_revision(void)
{
    return ewp_s.rev + ewp_s.kp.revision;
}

/**
 * @brief Changes while a hold fills.
 * @return The count.
 */
static uint32_t esc_wire_ps_hold_revision(void)
{
    return ewp_s.hold_rev;
}

/** @brief The host is left: presses and editors go, a held pin stays. */
static void esc_wire_ps_leave(void)
{
    ewp_cancel();
    ewp_editors_close();
    ewp_s.page = EWP_PAGE_NONE;
    ewp_s.open = esc_wire_panel_holds_port();
    ++ewp_s.rev;
}

/**
 * @brief The GPIO the session's port drives.
 * @return It, or PROG_REG_NO_PIN.
 */
static uint8_t esc_wire_ps_pin(void)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;

    return rd->known && rd->held && rd->gpio != 0xFFu ? rd->gpio
                                                      : (uint8_t)PROG_REG_NO_PIN;
}

const prog_page_set_t esc_wire_pages = {
    .init          = esc_wire_ps_init,
    .entry         = esc_wire_ps_entry,
    .open          = esc_wire_ps_open,
    .is_open       = esc_wire_ps_is_open,
    .back          = esc_wire_ps_back,
    .holds_port    = esc_wire_ps_holds,
    .armed         = esc_wire_ps_armed,
    .tick          = esc_wire_ps_tick,
    .has_press     = esc_wire_ps_has_press,
    .event         = ewp_event,
    .render        = ewp_render,
    .revision      = esc_wire_ps_revision,
    .hold_revision = esc_wire_ps_hold_revision,
    .hold_rect     = esc_wire_panel_hold_rect,
    .leave         = esc_wire_ps_leave,
    .cancel        = ewp_cancel,
    .pin           = esc_wire_ps_pin,
    .safe_at       = ewp_safe_at,
};

/* ---------------------------------------------------------- for the tests */

esc_prog_t *esc_wire_panel_model(void)
{
    return &ewp_s.prog;
}

const char *esc_wire_panel_message(void)
{
    return ewp_s.note[0] != '\0' ? ewp_s.note : ewp_message_text();
}
