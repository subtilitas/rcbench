/**
 * @file
 * @brief The session's backup and the settings files of programming for YGE
 *        ESCs.  See yge_prog.h; the format is
 *        protocols/openyge/include/openyge_file.h.
 *
 * The backup is the table as the session's first whole read found it.  The
 * first write of the session asks the card to save it as
 * BENCH/YGE/CONF/YGE001-5712.TXT for type code 5712, and goes when the card
 * has answered: saved, or kept in RAM only.  The IOmcu judges a draft; what
 * is held here says what a load would do before anything goes to the link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "openyge_guard.h"
#include "openyge_map.h"
#include "yge_prog.h"
#include "yge_prog_int.h"

bool yge_prog_guard_of(const yge_prog_t *p, oy_guard_t *g)
{
    if (p == NULL || g == NULL) {
        return false;
    }
    const yge_link_image_t *im = &p->f.image;

    memset(g, 0, sizeof(*g));
    if (im->gen == 0u || im->entries == 0u || im->entries > OPENYGE_MAX_PARAMS
        || (p->f.rd.flags & LINK_YP_F_HAVE_READ) == 0u) {
        return false;
    }
    oy_guard_table(g, im->value, im->entries,
                   (p->f.rd.flags & LINK_YP_F_LEGACY) != 0u);
    /* The page's numbers of this read, so a draft judged here is judged as
     * the IOmcu's guard judges it. */
    g->read_no = im->reads;
    return g->read_crc == im->read_crc;
}

/**
 * @brief The write that waited for the card goes, if it still may.
 * @param p the model
 */
static void pending_write_goes(yge_prog_t *p)
{
    p->flow = YGE_PROG_FLOW_NONE;
    const yge_prog_msg_t m =
        yge_prog_write_gate(p, p->pend_n, p->pend_reads, p->pend_crc);

    if (m != YGE_PROG_MSG_NONE) {
        (void)yge_prog_say(p, m, 0u);
        return;
    }
    yge_prog_write_go(p, p->pend, p->pend_n, p->pend_reads, p->pend_crc);
}

/**
 * @brief A write waiting for the card that may no longer go: under a stop, on
 *        an ARMED bench, with the ESC gone, or with the card silent past
 *        YGE_PROG_CARD_WAIT_MS.
 * @param p the model
 */
static void pending_write_follow(yge_prog_t *p)
{
    if (p->flow != YGE_PROG_FLOW_BACKUP) {
        return;
    }
    yge_prog_msg_t m = YGE_PROG_MSG_NONE;

    if (p->f.stopped) {
        m = YGE_PROG_MSG_STOPPED;
    } else if (p->f.armed) {
        m = YGE_PROG_MSG_ARMED;
    } else if (p->f.rd.phase != YGE_LINK_READY
               || p->f.rd.state != (uint8_t)LINK_YGE_READY) {
        m = YGE_PROG_MSG_NOT_READY;
    } else if ((uint32_t)(p->f.now_ms - p->card_ms) >= YGE_PROG_CARD_WAIT_MS) {
        m = YGE_PROG_MSG_NO_ANSWER;
    }
    if (m != YGE_PROG_MSG_NONE) {
        p->flow = YGE_PROG_FLOW_NONE;
        p->card_owed = 0u;
        (void)yge_prog_say(p, m, 0u);
    }
}

void yge_prog_backup_follow(yge_prog_t *p)
{
    if (p == NULL) {
        return;
    }
    const yge_link_image_t *im = &p->f.image;
    const yge_link_readout_t *rd = &p->f.rd;

    pending_write_follow(p);
    /* HAVE_READ clears at the page's ENTER and at its free: a new session.
     * A link that is gone says nothing of the session. */
    if (rd->phase == YGE_LINK_DOWN || rd->phase == YGE_LINK_NO_PAGE
        || (rd->known && (!rd->held || (rd->flags & LINK_YP_F_HAVE_READ) == 0u))) {
        if (rd->known) {
            p->backup_state = (uint8_t)YGE_PROG_BACKUP_NONE;
            p->load_ok = 0u;
        }
        return;
    }
    if (p->backup_state != (uint8_t)YGE_PROG_BACKUP_NONE || im->gen == 0u
        || rd->state != (uint8_t)LINK_YGE_READY || im->reads != rd->reads
        || im->read_crc != rd->read_crc || im->entries == 0u
        || im->entries > YGE_LINK_VALUES_MAX) {
        return;
    }
    memcpy(p->backup, im->value, sizeof(p->backup));
    p->backup_n = im->entries;
    p->backup_state = (uint8_t)YGE_PROG_BACKUP_HELD;
    p->backup_why = (uint8_t)YGE_PROG_CARD_OK;
}

yge_prog_backup_t yge_prog_backup_where(const yge_prog_t *p)
{
    return p != NULL ? (yge_prog_backup_t)p->backup_state : YGE_PROG_BACKUP_NONE;
}

size_t yge_prog_backup_text(const yge_prog_t *p, char *out, size_t size)
{
    if (p == NULL || out == NULL
        || p->backup_state == (uint8_t)YGE_PROG_BACKUP_NONE) {
        return 0u;
    }
    return oy_file_format(p->backup, p->backup_n, out, size);
}

bool yge_prog_card_take(yge_prog_t *p, char *out, size_t size, size_t *len,
                        char *tag, size_t tag_size)
{
    if (p == NULL || out == NULL || len == NULL || tag == NULL
        || !p->card_owed) {
        return false;
    }
    p->card_owed = 0u;
    *len = yge_prog_backup_text(p, out, size);
    const int t = snprintf(tag, tag_size, "%u", (unsigned)p->backup[OY_I_TYPE]);

    if (*len == 0u || t < 0 || (size_t)t >= tag_size) {
        /* Nothing the card could hold: the old values stay in RAM. */
        yge_prog_card_done(p, (uint8_t)YGE_PROG_CARD_FORMAT);
        return false;
    }
    p->card_out = 1u;
    return true;
}

void yge_prog_card_done(yge_prog_t *p, uint8_t result)
{
    if (p == NULL) {
        return;
    }
    p->card_out = 0u;
    if (p->backup_state == (uint8_t)YGE_PROG_BACKUP_HELD) {
        p->backup_state = result == (uint8_t)YGE_PROG_CARD_OK
                              ? (uint8_t)YGE_PROG_BACKUP_CARD
                              : (uint8_t)YGE_PROG_BACKUP_RAM;
        p->backup_why = result;
    }
    if (p->flow != YGE_PROG_FLOW_BACKUP) {
        return;
    }
    pending_write_goes(p);
    if (p->flow != YGE_PROG_FLOW_NONE
        && p->backup_state == (uint8_t)YGE_PROG_BACKUP_RAM) {
        (void)yge_prog_say(p, YGE_PROG_MSG_BACKUP_RAM, p->backup_why);
    }
}

yge_prog_msg_t yge_prog_file_load(yge_prog_t *p, const char *text, size_t len,
                                  unsigned *line)
{
    if (p == NULL || text == NULL) {
        return YGE_PROG_MSG_FILE_REFUSED;
    }
    unsigned at = 0u;
    oy_guard_t g;

    p->load_ok = 0u;
    const oy_file_err_t e = oy_file_parse(text, len, &p->loaded, &at);

    if (line != NULL) {
        *line = at;
    }
    if (e != OY_FILE_OK) {
        p->file_line = (uint16_t)at;
        return yge_prog_say(p, YGE_PROG_MSG_FILE_REFUSED, (uint16_t)e);
    }
    if (!yge_prog_guard_of(p, &g) || p->f.image.reads != p->f.rd.reads) {
        return yge_prog_say(p, YGE_PROG_MSG_NOT_READ, 0u);
    }
    const oy_load_t l = oy_file_load(&p->loaded, &g, &p->load);

    if (l != OY_LOAD_OK) {
        return yge_prog_say(p, YGE_PROG_MSG_LOAD_REFUSED, (uint16_t)l);
    }
    p->load_ok = 1u;
    return yge_prog_say(p, YGE_PROG_MSG_LOADED, p->load.changed);
}

const uint16_t *yge_prog_file_draft(const yge_prog_t *p, uint16_t *n)
{
    if (p == NULL || !p->load_ok) {
        return NULL;
    }
    if (n != NULL) {
        *n = p->load.count;
    }
    return p->load.draft;
}
