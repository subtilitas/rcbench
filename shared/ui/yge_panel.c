/**
 * @file
 * @brief Programming for YGE ESCs: the session's state, its stages, its page
 *        set and the card's flow.  See yge_panel.h.
 *
 * The tabs, the rows, the draft and the editors are yge_panel_edit.c, the
 * words yge_panel_text.c, the presses yge_panel_touch.c and the pages drawn
 * yge_panel_draw.c, with yge_panel_int.h between them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_panel.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "openyge_session.h"
#include "port_view.h"
#include "supply_screen.h"
#include "yge_panel_int.h"

ygp_state_t ygp_s;

/* ------------------------------------------------------------------ stages */

/**
 * @brief Where the page and the model are.
 * @return The stage.
 */
static yge_panel_stage_t page_stage(void)
{
    const yge_prog_t *m = &ygp_s.prog;
    const yge_link_readout_t *rd = &m->f.rd;
    const bool busy = yge_prog_busy(m);
    const bool stood = (rd->flags & (LINK_YP_F_STOPPED | LINK_YP_F_FAULT)) != 0u;
    const bool have = (rd->flags & LINK_YP_F_HAVE_READ) != 0u;

    if (!rd->known || rd->state == (uint8_t)LINK_YGE_FREE) {
        return busy ? YGE_PANEL_ENTERING : YGE_PANEL_CONNECT;
    }
    if (!rd->ours && !busy) {
        return rd->state == (uint8_t)LINK_YGE_NOT_BACK ? YGE_PANEL_NOT_BACK
                                                       : YGE_PANEL_OTHER;
    }
    switch (rd->state) {
    case LINK_YGE_NOT_BACK:
        return YGE_PANEL_NOT_BACK;
    case LINK_YGE_HAND_WAIT:
        return YGE_PANEL_HAND;
    case LINK_YGE_READY:
        if (busy) {
            return have ? YGE_PANEL_RUNNING : YGE_PANEL_ENTERING;
        }
        /* Not before the copy of this read is here: an edit would be lost to
         * it. */
        if (have && (m->f.image.gen == 0u || m->f.image.reads != rd->reads
                     || m->f.image.read_crc != rd->read_crc)) {
            return YGE_PANEL_RUNNING;
        }
        return YGE_PANEL_SESSION;
    case LINK_YGE_RUN:
        return have ? YGE_PANEL_RUNNING : YGE_PANEL_ENTERING;
    case LINK_YGE_OPEN:
        if (busy) {
            return YGE_PANEL_ENTERING;
        }
        return stood ? YGE_PANEL_STOPPED : YGE_PANEL_PIN_HELD;
    default:
        return YGE_PANEL_ENTERING;
    }
}

yge_panel_stage_t ygp_stage(void)
{
    return page_stage();
}

yge_panel_stage_t yge_panel_stage(void)
{
    return page_stage();
}

void ygp_note(const char *s, bool warn)
{
    snprintf(ygp_s.note, sizeof(ygp_s.note), "%s", s);
    ygp_s.note_warn = warn;
    ++ygp_s.rev;
}

void ygp_words_clear(void)
{
    if (ygp_s.note[0] != '\0' || ygp_s.says_hidden != ygp_s.prog.says) {
        ++ygp_s.rev;
    }
    ygp_s.note[0] = '\0';
    ygp_s.says_hidden = ygp_s.prog.says;
}

/* -------------------------------------------------------------------- card */

/**
 * @brief Hands the operator's request to the card's task.
 * @param op A yge_card_op_t, the rest of @c card filled in.
 */
static void card_ask(uint8_t op)
{
    ygp_s.card.op = op;
    if (++ygp_s.card_id == 0u) {
        ygp_s.card_id = 1u;
    }
    ygp_s.card.id = ygp_s.card_id;
    ygp_s.card_want = true;
    ygp_s.card_look = true;
}

bool ygp_card_busy(void)
{
    return ygp_s.card_want || ygp_s.card_out;
}

bool yge_panel_card_look(void)
{
    const bool look = ygp_s.card_look;

    ygp_s.card_look = false;
    return look;
}

/**
 * @brief The model's backup to the card, when it asks and the card is free.
 */
static void backup_follow(void)
{
    size_t len = 0u;
    uint32_t id = 0u;

    if (ygp_card_busy()) {
        return;
    }
    if (!yge_prog_card_take(&ygp_s.prog, ygp_s.card.text, sizeof(ygp_s.card.text),
                            &len, ygp_s.card.tag, sizeof(ygp_s.card.tag), &id)) {
        return;
    }
    ygp_s.card.op = (uint8_t)YGE_CARD_SAVE;
    ygp_s.card.id = id;
    ygp_s.card.len = len;
    ygp_s.backup_id = id;
    ygp_s.card_want = true;
    ygp_s.card_look = true;
}

bool yge_panel_card_take(yge_card_req_t *out)
{
    if (!ygp_s.card_want || ygp_s.card_out || out == NULL) {
        return false;
    }
    *out = ygp_s.card;
    ygp_s.card_want = false;
    ygp_s.card_out = true;
    return true;
}

/**
 * @brief Whether a list or a load has to stay out: a hold page or an editor
 *        is open, or the session is no longer on its rows.
 * @return true then.
 */
static bool under_page(void)
{
    return ygp_s.page != YGP_PAGE_NONE || ygp_s.kp.open
           || ygp_s.ch_what != YGP_CH_NONE || ygp_stage() != YGE_PANEL_SESSION;
}

/**
 * @brief A LIST answered: the choice list, with the next page's entry.
 * @param d The answer.
 */
static void listed(const yge_card_done_t *d)
{
    if (d->result != (uint8_t)YGE_CARD_DONE_OK) {
        ygp_note(ygp_card_text(d->result), true);
        return;
    }
    if (under_page()) {
        ygp_note(TR(YG_CARD_LATE), true);
        return;
    }
    if (d->count == 0u) {
        ygp_note(TR(YG_FILES_NONE), false);
        return;
    }
    ygp_s.files_total = d->total;
    ygp_s.files_count = (uint8_t)(d->count < YGP_FILES_PAGE ? d->count
                                                            : YGP_FILES_PAGE);
    ygp_editors_close();
    ygp_s.ch_what = YGP_CH_FILES;
    for (unsigned i = 0u; i < ygp_s.files_count; ++i) {
        memcpy(ygp_s.names[i], d->names[i], sizeof(ygp_s.names[i]));
        ygp_choice_add(ygp_s.names[i], (int)i);
    }
    if ((unsigned)ygp_s.files_first + ygp_s.files_count < ygp_s.files_total) {
        ygp_choice_add(TR(EW_FILES_NEXT), YGP_CHOICE_NEXT);
    }
}

/**
 * @brief A LOAD answered: its text against the read copy, and the draft it
 *        gives.
 * @param d The answer.
 */
static void loaded(const yge_card_done_t *d)
{
    yge_prog_t *m = &ygp_s.prog;

    if (d->result != (uint8_t)YGE_CARD_DONE_OK) {
        ygp_note(ygp_card_text(d->result), true);
        return;
    }
    if (under_page()) {
        ygp_note(TR(YG_CARD_LATE), true);
        return;
    }
    unsigned line = 0u;

    if (yge_prog_file_load(m, d->text, d->len, &line) == YGE_PROG_MSG_LOADED) {
        uint16_t n = 0u;
        uint16_t reads = 0u;
        uint16_t crc = 0u;
        const uint16_t *draft = yge_prog_file_draft(m, &n, &reads, &crc);

        if (draft != NULL && n <= YGE_LINK_VALUES_MAX) {
            memcpy(ygp_s.draft, draft, (size_t)n * sizeof(draft[0]));
            ygp_s.draft_reads = reads;
            ygp_s.draft_crc = crc;
        }
    }
}

void yge_panel_card_done(const yge_card_done_t *d)
{
    if (d == NULL) {
        return;
    }
    ygp_s.card_out = false;
    if (d->op == (uint8_t)YGE_CARD_SAVE) {
        if (d->id == ygp_s.backup_id && d->result == (uint8_t)YGE_CARD_DONE_OK) {
            char line[YGP_LINE];

            snprintf(line, sizeof(line), TR(YG_BACKUP_SAVED), d->name);
            ygp_note(line, false);
        }
        ygp_s.backup_id = 0u;
        yge_prog_card_done(&ygp_s.prog, d->id, yge_card_prog_result(d->result));
    } else if (d->id == ygp_s.card_id) {
        if (d->op == (uint8_t)YGE_CARD_LIST) {
            listed(d);
        } else if (d->op == (uint8_t)YGE_CARD_LOAD) {
            loaded(d);
        }
    }
    ++ygp_s.rev;
}

void ygp_files_open(void)
{
    ygp_s.card.first = ygp_s.files_first;
    card_ask((uint8_t)YGE_CARD_LIST);
}

void ygp_file_chosen(int value)
{
    if (value == YGP_CHOICE_NEXT) {
        const unsigned next = (unsigned)ygp_s.files_first + YGP_FILES_PAGE;

        ygp_s.files_first = next < ygp_s.files_total ? (uint16_t)next : 0u;
        ygp_files_open();
    } else if (value >= 0 && value < (int)ygp_s.files_count) {
        snprintf(ygp_s.card.name, sizeof(ygp_s.card.name), "%s",
                 ygp_s.names[value]);
        card_ask((uint8_t)YGE_CARD_LOAD);
    }
}

/* ---------------------------------------------------------------- the hold */

void ygp_connect_values(yge_prog_connect_t *c)
{
    const bool hand = ygp_s.power == (uint8_t)LINK_YGE_BY_HAND;

    memset(c, 0, sizeof(*c));
    c->port = ygp_s.port;
    c->power.power = ygp_s.power;
    c->power.mv = hand ? 0u : ygp_s.mv;
    c->power.ma = hand ? 0u : ygp_s.ma;
    c->power.boot_ms = ygp_s.boot_ms;
}

/**
 * @brief WRITE n VALUES: the draft and the read copy it was made from.
 */
static void write_now(void)
{
    const yge_prog_t *m = &ygp_s.prog;

    (void)yge_prog_write(&ygp_s.prog, ygp_s.draft, m->f.image.entries,
                         ygp_s.draft_reads, ygp_s.draft_crc);
}

void ygp_hold_apply(void)
{
    yge_prog_t *m = &ygp_s.prog;
    const int page = ygp_s.page;
    yge_prog_connect_t c;

    ygp_s.page = YGP_PAGE_NONE;
    ygp_s.note[0] = '\0';
    switch (page) {
    case YGP_PAGE_LEAVE:
        (void)yge_prog_leave(m);
        break;
    case YGP_PAGE_READ:
        (void)yge_prog_read(m);
        break;
    case YGP_PAGE_RELEASE:
        (void)yge_prog_close(m);
        break;
    default:
        switch (ygp_stage()) {
        case YGE_PANEL_CONNECT:
            ygp_connect_values(&c);
            (void)yge_prog_connect(m, &c);
            break;
        case YGE_PANEL_PIN_HELD:
            if (!ygp_s.no_again) {
                ygp_connect_values(&c);
                (void)yge_prog_enter_again(m, &c);
            }
            break;
        case YGE_PANEL_SESSION:
            if (yge_panel_hold_live()) {
                write_now();
            }
            break;
        default:
            break;
        }
        break;
    }
    ++ygp_s.rev;
}

/* ---------------------------------------------------------------- the feed */

/**
 * @brief What a render shows of the page, as one number: a count in a stage
 *        that shows one moves it 10 times a second, and nothing moves it at
 *        rest.
 * @return The hash.
 */
static uint32_t signature(void)
{
    const yge_prog_t *m = &ygp_s.prog;
    const yge_link_readout_t *rd = &m->f.rd;
    const yge_panel_stage_t st = ygp_stage();
    const uint32_t ticks = (st == YGE_PANEL_ENTERING || st == YGE_PANEL_HAND
                            || st == YGE_PANEL_RUNNING)
                               ? rd->t_ms / 100u : 0u;
    const port_view_t *pv = port_view_get();
    const uint32_t v[] = {
        (uint32_t)st, rd->known, rd->state, rd->flags, rd->line, rd->gpio,
        rd->port, rd->result, rd->ses, rd->ses_state, rd->seen, rd->writing,
        rd->ro_why, m->f.image.gen, m->says, m->backup_state, ticks,
        m->f.stopped, m->f.armed, ygp_s.mv, ygp_s.ma, ygp_s.no_again,
        ygp_s.held_stop, pv->count, ygp_s.port < LINK_PG_PORTS_MAX
                                        ? pv->pin[ygp_s.port] : 0u,
    };
    uint32_t h = 2166136261u;

    for (size_t i = 0u; i < sizeof(v) / sizeof(v[0]); ++i) {
        h = (h ^ v[i]) * 16777619u;
    }
    return h;
}

void yge_panel_gate(yge_prog_gate_t *g)
{
    ygp_s.gate = g;
    yge_prog_init(&ygp_s.prog);
    yge_prog_gate_set(&ygp_s.prog, g);
}

/**
 * @brief SUPPLY's values, never a default of this session's, until the
 *        operator types others.
 */
static void supply_follow(void)
{
    if (ygp_s.supply_typed) {
        return;
    }
    const uint16_t mv = (uint16_t)(supply_screen_set_v() * 1000.0f + 0.5f);
    const uint16_t ma = (uint16_t)(supply_screen_set_i() * 1000.0f + 0.5f);

    /* A hold confirms what the page shows: a value that moves under it ends
     * it. */
    if ((mv != ygp_s.mv || ma != ygp_s.ma) && ygp_s.hold_down
        && ygp_s.page == YGP_PAGE_NONE) {
        ygp_cancel();
    }
    ygp_s.mv = mv;
    ygp_s.ma = ma;
}

/**
 * @brief A stage arrived at, or the link gone: what was open answers to a
 *        stage that is over.
 */
static void stage_follow(void)
{
    yge_prog_t *m = &ygp_s.prog;
    const yge_panel_stage_t now = page_stage();
    const bool link_up = m->f.rd.phase != YGE_LINK_DOWN
                         && m->f.rd.phase != YGE_LINK_NO_PAGE;

    if (now != ygp_s.stage_seen || (ygp_s.link_seen && !link_up)) {
        ygp_s.stage_seen = now;
        ygp_cancel();
        ygp_editors_close();
        ygp_s.page = YGP_PAGE_NONE;
    }
    ygp_s.link_seen = link_up;
    /* ENTER AGAIN stays for what leaves the supply free to come on.  A stop
     * or a fault has the panel's OFF latched on the SUPPLY page until the
     * hold ends. */
    if (m->f.rd.known && !m->f.rd.held) {
        ygp_s.no_again = false;
        ygp_s.held_stop = false;
    } else if ((m->f.rd.flags & (LINK_YP_F_STOPPED | LINK_YP_F_FAULT)) != 0u
               || m->f.stopped) {
        ygp_s.no_again = true;
        ygp_s.held_stop = true;
    } else if (m->msg == YGE_PROG_MSG_REFUSED
               && m->detail == (uint16_t)LINK_YGE_REF_SUPPLY_OFF) {
        ygp_s.no_again = true;
    }
}

void yge_panel_feed(const yge_prog_feed_t *f)
{
    yge_prog_t *m = &ygp_s.prog;
    const supply_caps_t caps = supply_screen_caps();

    yge_prog_limits(m, (uint16_t)(caps.v_max * 1000.0f + 0.5f),
                    (uint16_t)(caps.i_max * 1000.0f + 0.5f));
    yge_prog_feed(m, f);
    /* What the model says newer than the note is what shows. */
    if (m->says != ygp_s.says_seen) {
        ygp_s.says_seen = m->says;
        ygp_s.note[0] = '\0';
    }
    ygp_draft_follow();
    supply_follow();
    ygp_tabs_follow();
    stage_follow();
    backup_follow();
    const uint32_t sig = signature();

    if (sig != ygp_s.sig) {
        ygp_s.sig = sig;
        ++ygp_s.rev;
    }
}

bool yge_panel_next_cmd(yge_prog_cmd_t *out)
{
    return yge_prog_next_cmd(&ygp_s.prog, out);
}

bool yge_panel_holds_port(void)
{
    return yge_prog_holds_port(&ygp_s.prog);
}

bool yge_panel_idle(void)
{
    return yge_prog_idle(&ygp_s.prog);
}

bool yge_panel_by_hand(void)
{
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;

    if (rd->known && rd->held && rd->ours) {
        return (rd->flags & LINK_YP_F_BY_HAND) != 0u
               || (rd->state == (uint8_t)LINK_YGE_OPEN
                   && ygp_s.power == (uint8_t)LINK_YGE_BY_HAND);
    }
    return ygp_s.power == (uint8_t)LINK_YGE_BY_HAND;
}

bool yge_panel_target_text(char *out, size_t size)
{
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;
    const port_view_t *pv = port_view_get();
    unsigned port = ygp_s.port;
    unsigned gpio = 0xFFu;

    if (rd->known && rd->held && rd->port < YGE_PANEL_PORTS) {
        port = rd->port;
        gpio = rd->gpio;
    } else if (pv->count != PORT_VIEW_UNKNOWN && port < pv->count
               && port < LINK_PG_PORTS_MAX) {
        gpio = pv->pin[port];
    }
    if (gpio >= 64u) {
        snprintf(out, size, TR(YG_TARGET_PORT), port + 1u);
        return true;
    }
    snprintf(out, size, TR(EW_TARGET_PORT), port + 1u, gpio);
    return false;
}

/* ------------------------------------------------------------ the page set */

/**
 * @brief Start over: closed, nothing held, laid out in @p area.
 * @param area The area.
 */
static void ygp_ps_init(gfx_rect_t area)
{
    yge_prog_gate_t *const gate = ygp_s.gate;

    memset(&ygp_s, 0, sizeof(ygp_s));
    ygp_s.gate = gate;
    ygp_s.area = area;
    ygp_s.power = (uint8_t)LINK_YGE_SWITCHED;
    ygp_s.boot_ms = (uint16_t)OY_BOOT_MS;
    ygp_s.page = YGP_PAGE_NONE;
    yge_prog_init(&ygp_s.prog);
    yge_prog_gate_set(&ygp_s.prog, ygp_s.gate);
}

/** @brief The host screen was entered. */
static void ygp_ps_entry(void)
{
    ++ygp_s.rev;
}

/** @brief The row was tapped. */
static void ygp_ps_open(void)
{
    ygp_s.open = true;
    ygp_s.note[0] = '\0';
    ++ygp_s.rev;
}

/**
 * @brief Whether the session shows.
 * @return true while open.
 */
static bool ygp_ps_is_open(void)
{
    return ygp_s.open;
}

/**
 * @brief Whether the session holds a pin.
 * @return true then.
 */
static bool ygp_ps_holds(void)
{
    return yge_panel_holds_port();
}

/**
 * @brief The bench's arm.
 * @param armed Armed, or asked to arm.
 */
static void ygp_ps_armed(bool armed)
{
    ygp_s.armed = armed;
}

/**
 * @brief Whether a press of this finger is the session's.
 * @param id The finger.
 * @return true then.
 */
static bool ygp_ps_has_press(uint8_t id)
{
    return ygp_s.have && ygp_s.id == id;
}

uint32_t yge_panel_revision(void)
{
    return ygp_s.rev + ygp_s.kp.revision;
}

/**
 * @brief Changes while a hold fills, and when a button is pressed or let go.
 * @return The count.
 */
static uint32_t ygp_ps_hold_revision(void)
{
    return ygp_s.hold_rev;
}

/**
 * @brief Where hold_revision shows.
 * @return The button a press is on or last was.
 */
static gfx_rect_t ygp_ps_hold_rect(void)
{
    return ygp_s.touched;
}

/** @brief The host is left: presses and editors go, a held pin stays. */
static void ygp_ps_leave(void)
{
    ygp_cancel();
    ygp_editors_close();
    ygp_s.page = YGP_PAGE_NONE;
    ygp_s.open = yge_panel_holds_port();
    ++ygp_s.rev;
}

/**
 * @brief The GPIO the session's port drives.
 * @return It, or PROG_REG_NO_PIN.
 */
static uint8_t ygp_ps_pin(void)
{
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;

    return rd->known && rd->held && rd->gpio != 0xFFu ? rd->gpio
                                                      : (uint8_t)PROG_REG_NO_PIN;
}

const prog_page_set_t yge_pages = {
    .init          = ygp_ps_init,
    .entry         = ygp_ps_entry,
    .open          = ygp_ps_open,
    .is_open       = ygp_ps_is_open,
    .back          = ygp_back,
    .holds_port    = ygp_ps_holds,
    .armed         = ygp_ps_armed,
    .tick          = ygp_tick,
    .has_press     = ygp_ps_has_press,
    .event         = ygp_event,
    .render        = ygp_render,
    .revision      = yge_panel_revision,
    .hold_revision = ygp_ps_hold_revision,
    .hold_rect     = ygp_ps_hold_rect,
    .leave         = ygp_ps_leave,
    .cancel        = ygp_cancel,
    .pin           = ygp_ps_pin,
    .safe_at       = ygp_safe_at,
};

/* ---------------------------------------------------------- for the tests */

yge_prog_t *yge_panel_model(void)
{
    return &ygp_s.prog;
}

const char *yge_panel_message(void)
{
    if (ygp_s.note[0] != '\0') {
        return ygp_s.note;
    }
    return ygp_s.says_hidden != ygp_s.prog.says ? ygp_message_text() : "";
}

int yge_panel_changed(void)
{
    return ygp_changed_count();
}

int yge_panel_page(void)
{
    return ygp_s.page;
}

const char *yge_panel_page_line(int i)
{
    return i >= 0 && i < ygp_s.page_lines ? ygp_s.lines[i] : NULL;
}

ui_keypad_t *yge_panel_keypad(void)
{
    return &ygp_s.kp;
}

const char *yge_panel_choice_label(int i)
{
    return i >= 0 && i < ygp_s.ch_count ? ygp_s.ch_labels[i] : NULL;
}

const char *yge_panel_check_line(void)
{
    static char s_text[YGP_LINE];

    ygp_check_line(s_text, sizeof(s_text));
    return s_text;
}
