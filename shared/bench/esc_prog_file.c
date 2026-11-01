/**
 * @file
 * @brief The session's backup and the settings files of the ESC programmer.  See
 *        esc_prog.h; the format is protocols/escwire/include/ew_file.h.
 *
 * The IOmcu judges a draft, and ew_session_restore() a backup: what is held here
 * says what a load or a restore would do before anything goes to the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "esc_prog.h"
#include "esc_prog_int.h"
#include "ew_id.h"
#include "ew_map.h"

/**
 * @brief The identity the page reports, as identification gave it.
 * @param p   the model
 * @param out takes it
 */
static void ident_of(const esc_prog_t *p, ew_ident_t *out)
{
    const esc_link_readout_t *rd = &p->f.rd;

    memset(out, 0, sizeof(*out));
    out->fw = rd->fw;
    out->layout = rd->layout;
    out->why = rd->why;
    out->writable = (rd->flags & LINK_EW_F_WRITABLE) != 0u;
    out->erased = (rd->flags & LINK_EW_F_ERASED) != 0u;
    out->can_build = (rd->flags & LINK_EW_F_CAN) != 0u;
    out->len = rd->len;
    for (size_t i = 0u; i + 1u < sizeof(out->name) && p->f.image.name[i] != '\0';
         ++i) {
        out->name[i] = p->f.image.name[i];
    }
}

/**
 * @brief The first contact as the page reports it.
 * @param p   the model
 * @param out takes it
 */
static void contact_of(const esc_prog_t *p, ew_contact_t *out)
{
    const esc_link_readout_t *rd = &p->f.rd;

    memset(out, 0, sizeof(*out));
    out->cpu = rd->cpu;
    out->pin_code = rd->pin_code;
    out->flash_code = rd->flash_code;
    out->protocol = rd->protocol;
    out->signature = rd->signature;
}

/**
 * @brief The ESC as the read copy holds it, as a file.
 * @param p   the model
 * @param out takes it
 * @return false when the read copy gives none
 */
static bool capture_now(const esc_prog_t *p, ew_file_t *out)
{
    const esc_link_image_t *im = &p->f.image;
    ew_ident_t id;
    ew_contact_t c;

    ident_of(p, &id);
    contact_of(p, &c);
    return id.fw == (uint8_t)EW_FW_E32
               ? ew_file_capture_text(out, &id, im->page, im->music)
               : ew_file_capture(out, &c, &id, im->page);
}

void esc_prog_backup_follow(esc_prog_t *p)
{
    if (p == NULL) {
        return;
    }
    const esc_link_image_t *im = &p->f.image;

    /* HAVE_READ clears only at the page's ENTER (ew_session_hold()), never in a READ
     * or a WRITE, so this is a new session on a pin still held. */
    if (!p->f.rd.held || (p->f.rd.known
                          && (p->f.rd.flags & LINK_EW_F_HAVE_READ) == 0u)) {
        p->backup_state = (uint8_t)ESC_PROG_BACKUP_NONE;
        return;
    }
    /* An ESC of this session that answered and was read: at OPEN the registers
     * and the copy may still be the session's before. */
    if (p->backup_state != (uint8_t)ESC_PROG_BACKUP_NONE || im->gen == 0u
        || p->f.rd.state != (uint8_t)LINK_ESC_READY
        || (p->f.rd.flags & LINK_EW_F_HAVE_READ) == 0u
        || im->reads != p->f.rd.reads || im->read_crc != p->f.rd.read_crc) {
        return;
    }
    if (capture_now(p, &p->file)) {
        p->backup_state = (uint8_t)ESC_PROG_BACKUP_OWED;
        p->backup_gen = im->gen;
    }
}

esc_prog_backup_t esc_prog_backup_state(const esc_prog_t *p)
{
    return p != NULL ? (esc_prog_backup_t)p->backup_state : ESC_PROG_BACKUP_NONE;
}

size_t esc_prog_backup_text(const esc_prog_t *p, char *out, size_t size)
{
    if (p == NULL || out == NULL
        || p->backup_state == (uint8_t)ESC_PROG_BACKUP_NONE) {
        return 0u;
    }
    return ew_file_format(&p->file, out, size);
}

/**
 * @brief The changed fields of a draft as lines of the record.
 * @param p     the model
 * @param draft the draft
 * @param out   takes the lines
 * @param size  bytes at @p out
 * @return bytes written
 */
static size_t record_changes(const esc_prog_t *p, const uint8_t *draft, char *out,
                             size_t size)
{
    const uint8_t *page = p->f.image.page;
    char from[32];
    char to[32];
    size_t at = 0u;

    if (p->f.rd.fw == (uint8_t)EW_FW_E32) {
        for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
            const int16_t was = ew_e32_get(page, i);
            const int16_t now = ew_e32_get(draft, i);

            if (was != now && at < size) {
                (void)ew_key_text(&ew_map_e32[i], was, from, sizeof(from));
                (void)ew_key_text(&ew_map_e32[i], now, to, sizeof(to));
                at += (size_t)snprintf(out + at, size - at, "%s: %s -> %s\n",
                                       ew_map_e32[i].key, from, to);
            }
        }
    } else {
        const ew_field_t *f;

        for (size_t i = 0u; (f = ew_map_nth(p->f.rd.fw, p->f.rd.layout, i)) != NULL;
             ++i) {
            if (page[f->off] != draft[f->off] && at < size) {
                (void)ew_field_text(f, page[f->off], from, sizeof(from));
                (void)ew_field_text(f, draft[f->off], to, sizeof(to));
                at += (size_t)snprintf(out + at, size - at, "%s: %s -> %s\n",
                                       f->name, from, to);
            }
        }
    }
    return at < size ? at : size;
}

size_t esc_prog_record_text(const esc_prog_t *p, const uint8_t *draft, char *out,
                            size_t size)
{
    /* Big, and one caller on one task. */
    static ew_file_t s_before;

    if (p == NULL || draft == NULL || out == NULL || p->f.image.gen == 0u
        || !capture_now(p, &s_before)) {
        return 0u;
    }
    int n = snprintf(out, size, "%s\ntarget=%04X gpio=%u\n", ESC_PROG_RECORD_HEAD,
                     (unsigned)p->f.rd.target, (unsigned)p->f.rd.gpio);

    if (n < 0 || (size_t)n >= size) {
        return 0u;
    }
    size_t at = (size_t)n;

    at += record_changes(p, draft, out + at, size - at);
    if (size - at < sizeof("before\n")) {
        return 0u;
    }
    memcpy(out + at, "before\n", sizeof("before\n") - 1u);
    at += sizeof("before\n") - 1u;
    const size_t file = ew_file_format(&s_before, out + at, size - at);

    return file != 0u ? at + file : 0u;
}

void esc_prog_backup_settled(esc_prog_t *p)
{
    if (p != NULL && p->backup_state == (uint8_t)ESC_PROG_BACKUP_OWED) {
        p->backup_state = (uint8_t)ESC_PROG_BACKUP_SETTLED;
    }
}

esc_prog_msg_t esc_prog_file_load(esc_prog_t *p, const char *text, size_t len,
                                  unsigned *line)
{
    if (p == NULL || text == NULL) {
        return ESC_PROG_MSG_FILE_REFUSED;
    }
    p->load.len = 0u;
    const ew_file_err_t e = ew_file_parse(text, len, &p->loaded, line);

    if (e != EW_FILE_OK) {
        return esc_prog_say(p, ESC_PROG_MSG_FILE_REFUSED, (uint16_t)e);
    }
    if (p->f.image.gen == 0u || p->f.image.reads != p->f.rd.reads) {
        return esc_prog_say(p, ESC_PROG_MSG_NOT_READ, 0u);
    }
    ew_ident_t id;

    ident_of(p, &id);
    const ew_load_t l = ew_file_load(&p->loaded, &id, p->f.image.page, &p->load);

    if (l != EW_LOAD_OK) {
        p->load.len = 0u;
        return esc_prog_say(p, ESC_PROG_MSG_LOAD_REFUSED, (uint16_t)l);
    }
    return esc_prog_say(p, ESC_PROG_MSG_LOADED, p->load.changed);
}

const uint8_t *esc_prog_file_draft(const esc_prog_t *p, uint16_t *len)
{
    if (p == NULL || p->load.len == 0u) {
        return NULL;
    }
    if (len != NULL) {
        *len = p->load.len;
    }
    return p->load.target;
}

esc_prog_msg_t esc_prog_file_restore(esc_prog_t *p, const char *text, size_t len)
{
    if (p == NULL || text == NULL) {
        return ESC_PROG_MSG_FILE_REFUSED;
    }
    const ew_file_err_t e = ew_file_parse(text, len, &p->loaded, NULL);

    if (e != EW_FILE_OK) {
        return esc_prog_say(p, ESC_PROG_MSG_FILE_REFUSED, (uint16_t)e);
    }
    if (p->f.rd.phase != ESC_LINK_READY || p->flow != ESC_PROG_FLOW_NONE || p->wait_id != 0u
        || p->f.armed || p->f.stopped
        || p->f.rd.state != (uint8_t)LINK_ESC_READY) {
        return esc_prog_say(p, ESC_PROG_MSG_BUSY, 0u);
    }
    if (p->f.image.gen == 0u || p->f.image.reads != p->f.rd.reads) {
        return esc_prog_say(p, ESC_PROG_MSG_NOT_READ, 0u);
    }
    ew_ident_t id;
    ew_backup_id_t here;
    ew_backup_id_t from;

    ident_of(p, &id);
    memset(&here, 0, sizeof(here));
    here.pin_code = p->f.rd.pin_code;
    here.flash_code = p->f.rd.flash_code;
    here.protocol = p->f.rd.protocol;
    memcpy(here.name, id.name, sizeof(here.name));
    const ew_restore_t r =
        ew_file_restore_check(&p->loaded, &id, &here, p->f.image.page, &from);

    if (r != EW_RESTORE_OK) {
        return esc_prog_say(p, ESC_PROG_MSG_RESTORE_REFUSED, (uint16_t)r);
    }
    p->flow = ESC_PROG_FLOW_ONE;
    esc_prog_cmd_t *c = esc_prog_issue(p, ESC_PROG_CMD_RESTORE);

    c->reads = p->f.rd.reads;
    c->read_crc = p->f.rd.read_crc;
    c->len = p->loaded.len;
    memcpy(c->draft, p->loaded.page, p->loaded.len);
    c->identity[0] = from.pin_code;
    c->identity[1] = from.flash_code;
    c->identity[2] = from.protocol;
    memcpy(&c->identity[4], from.name, sizeof(from.name) - 1u);
    return esc_prog_say(p, ESC_PROG_MSG_NONE, 0u);
}
