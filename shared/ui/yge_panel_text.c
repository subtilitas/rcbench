/**
 * @file
 * @brief Programming for YGE ESCs: the words.  The model's messages, the
 *        refusals, why a table is read-only, the card's answers and the
 *        page's state.  See yge_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "openyge_guard.h"
#include "openyge_session.h"
#include "yge_panel_int.h"

/** @brief A message with a number or a reason in it, formatted. */
static char s_msg[YGP_LINE];

/**
 * @brief What the page refused.
 * @param ref A link_yge_ref_t.
 * @return The text.
 */
static ui_text_id_t refusal(uint16_t ref)
{
    switch (ref) {
    case LINK_YGE_REF_BUSY:
        return TX_EW_REF_BUSY;
    case LINK_YGE_REF_TARGET:
        return TX_YG_REF_TARGET;
    case LINK_YGE_REF_HELD:
        return TX_YG_REF_HELD;
    case LINK_YGE_REF_ARMED:
        return TX_EW_MSG_ARMED;
    case LINK_YGE_REF_UNSAFE:
        return TX_EW_REF_UNSAFE;
    case LINK_YGE_REF_NOT_OPEN:
        return TX_EW_REF_NOT_OPEN;
    case LINK_YGE_REF_STOPPED:
        return TX_EW_REF_STOPPED;
    case LINK_YGE_REF_NO_PIN:
        return TX_YG_REF_NO_PIN;
    case LINK_YGE_REF_RANGE:
        return TX_EW_REF_RANGE;
    case LINK_YGE_REF_NO_SUPPLY:
        return TX_EW_REF_NO_SUPPLY;
    case LINK_YGE_REF_SUPPLY_OFF:
        return TX_EW_REF_SUPPLY_OFF;
    case LINK_YGE_REF_STALE:
        return TX_EW_MSG_STALE;
    case LINK_YGE_REF_WRITING:
        return TX_EW_REF_WRITING;
    case LINK_YGE_REF_SESSION:
        return TX_YG_REF_SESSION;
    default:
        return TX_EW_REF_OTHER;
    }
}

/**
 * @brief How a session's operation ended.
 * @param ses An oy_res_t.
 * @return The text.
 */
static ui_text_id_t session_result(uint16_t ses)
{
    switch (ses) {
    case OY_SILENT:
        return TX_YG_SES_SILENT;
    case OY_NO_TABLE:
        return TX_YG_SES_NO_TABLE;
    case OY_NOT_KNOWN:
        return TX_YG_SES_NOT_KNOWN;
    case OY_CHANGED:
        return TX_YG_SES_CHANGED;
    case OY_NOT_TAKEN:
        return TX_YG_SES_NOT_TAKEN;
    case OY_PARTIAL:
        return TX_YG_SES_PARTIAL;
    case OY_GUARD:
        return TX_YG_SES_GUARD;
    case OY_ABORTED:
        return TX_EW_MSG_CUT;
    default:
        return TX_YG_SES_OTHER;
    }
}

const char *ygp_ro_text(uint8_t why)
{
    switch (why) {
    case OY_REF_RO_LEGACY:
        return TR(YG_RO_LEGACY);
    case OY_REF_RO_COUNT:
        return TR(YG_RO_COUNT);
    case OY_REF_RO_FIRMWARE:
        return TR(YG_RO_FIRMWARE);
    default:
        return TR(YG_RO_OTHER);
    }
}

const char *ygp_card_text(uint8_t r)
{
    switch (r) {
    case YGE_CARD_DONE_NO_CARD:
        return TR(EW_CARD_NONE);
    case YGE_CARD_DONE_FULL:
        return TR(YG_CARD_FULL);
    case YGE_CARD_DONE_DENIED:
        return TR(EW_CARD_DENIED);
    case YGE_CARD_DONE_WRITE:
        return TR(EW_CARD_WRITE);
    case YGE_CARD_DONE_MISSING:
        return TR(EW_CARD_MISSING);
    case YGE_CARD_DONE_LARGE:
        return TR(YG_CARD_LARGE);
    default:
        return TR(EW_CARD_READ);
    }
}

/**
 * @brief Why the session's backup did not reach the card.
 * @param why A yge_prog_card_t.
 * @return The text.
 */
static const char *backup_why(uint16_t why)
{
    switch (why) {
    case YGE_PROG_CARD_NO_CARD:
        return TR(EW_CARD_NONE);
    case YGE_PROG_CARD_FULL:
        return TR(YG_CARD_FULL);
    case YGE_PROG_CARD_WRITE:
        return TR(EW_CARD_WRITE);
    case YGE_PROG_CARD_FORMAT:
        return TR(YG_CARD_FORMAT);
    case YGE_PROG_CARD_LATE:
        return TR(YG_CARD_SILENT);
    default:
        return TR(EW_CARD_READ);
    }
}

/**
 * @brief Why a load is refused.
 * @param l An oy_load_t.
 * @return The text.
 */
static const char *load_why(uint16_t l)
{
    switch (l) {
    case OY_LOAD_TYPE:
        return TR(YG_LOAD_TYPE);
    case OY_LOAD_COUNT:
        return TR(YG_LOAD_COUNT);
    default:
        return TR(YG_LOAD_GUARD);
    }
}

/**
 * @brief A message that is one text, no number in it.
 * @param m The message.
 * @return The text; TX_COUNT for one that carries a detail.
 */
static ui_text_id_t plain(yge_prog_msg_t m)
{
    static const ui_text_id_t k_plain[] = {
        [YGE_PROG_MSG_ARMED] = TX_EW_MSG_ARMED,
        [YGE_PROG_MSG_STOPPED] = TX_EW_MSG_STOPPED,
        [YGE_PROG_MSG_NO_LINK] = TX_EW_MSG_NO_LINK,
        [YGE_PROG_MSG_NO_PAGE] = TX_YG_MSG_NO_PAGE,
        [YGE_PROG_MSG_BUSY] = TX_EW_MSG_BUSY,
        [YGE_PROG_MSG_HELD] = TX_EW_MSG_HELD,
        [YGE_PROG_MSG_RANGE] = TX_EW_MSG_RANGE,
        [YGE_PROG_MSG_NOT_READY] = TX_YG_MSG_NOT_READY,
        [YGE_PROG_MSG_NOT_READ] = TX_YG_MSG_NOT_READ,
        [YGE_PROG_MSG_STALE] = TX_EW_MSG_STALE,
        [YGE_PROG_MSG_NO_CHANGE] = TX_YG_MSG_NO_CHANGE,
        [YGE_PROG_MSG_NO_LINE] = TX_EW_MSG_NO_LINE,
        [YGE_PROG_MSG_SUPPLY] = TX_EW_MSG_SUPPLY,
        [YGE_PROG_MSG_TIMEOUT] = TX_EW_MSG_TIMEOUT,
        [YGE_PROG_MSG_CUT] = TX_EW_MSG_CUT,
        [YGE_PROG_MSG_NOT_BACK] = TX_EW_MSG_NOT_BACK,
        [YGE_PROG_MSG_NO_ANSWER] = TX_EW_MSG_NO_ANSWER,
        [YGE_PROG_MSG_RESTARTED] = TX_EW_MSG_RESTARTED,
        [YGE_PROG_MSG_NACK] = TX_EW_MSG_NACK,
        [YGE_PROG_MSG_DROPPED] = TX_EW_MSG_DROPPED,
        [YGE_PROG_MSG_ABORTED] = TX_EW_MSG_ABORTED,
        [YGE_PROG_MSG_HAND_WAIT] = TX_YG_MSG_HAND_WAIT,
        [YGE_PROG_MSG_READ_DONE] = TX_EW_MSG_READ_DONE,
        [YGE_PROG_MSG_WRITE_DONE] = TX_EW_MSG_WRITE_DONE,
        [YGE_PROG_MSG_LEFT] = TX_YG_MSG_LEFT,
        [YGE_PROG_MSG_CLEARED] = TX_EW_MSG_CLEARED,
    };

    if ((size_t)m >= sizeof(k_plain) / sizeof(k_plain[0]) || m == YGE_PROG_MSG_NONE
        || k_plain[m] == (ui_text_id_t)0) {
        return TX_COUNT;
    }
    return k_plain[m];
}

const char *ygp_message_text(void)
{
    const yge_prog_t *p = &ygp_s.prog;
    uint16_t d = 0u;
    const yge_prog_msg_t m = yge_prog_message(p, &d);
    const ui_text_id_t t = plain(m);

    /* The IOmcu's silence and the card's share a message: a backup still
     * out names the card. */
    if (m == YGE_PROG_MSG_NO_ANSWER && ygp_s.backup_id != 0u) {
        return TR(YG_CARD_SILENT);
    }
    if (t != TX_COUNT) {
        return ui_tr(t);
    }
    switch (m) {
    case YGE_PROG_MSG_READ_ONLY:
        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_READ_ONLY), ygp_ro_text((uint8_t)d));
        break;
    case YGE_PROG_MSG_GUARD: {
        const ygp_row_t r = { (uint8_t)yge_prog_bad(p), 0u };

        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_GUARD), ygp_row_name(&r),
                 (unsigned)yge_prog_bad(p), (unsigned)d);
        break;
    }
    case YGE_PROG_MSG_REFUSED:
        snprintf(s_msg, sizeof(s_msg), "%s", ui_tr(refusal(d)));
        break;
    case YGE_PROG_MSG_SESSION:
        snprintf(s_msg, sizeof(s_msg), "%s", ui_tr(session_result(d)));
        break;
    case YGE_PROG_MSG_BACKUP_RAM:
        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_BACKUP_RAM), backup_why(d));
        break;
    case YGE_PROG_MSG_FILE_REFUSED:
        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_FILE_REFUSED), (unsigned)d,
                 (unsigned)p->file_line);
        break;
    case YGE_PROG_MSG_LOAD_REFUSED:
        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_LOAD_REFUSED), load_why(d));
        break;
    case YGE_PROG_MSG_LOADED:
        snprintf(s_msg, sizeof(s_msg), TR(YG_MSG_LOADED), (unsigned)d);
        break;
    default:
        return "";
    }
    return s_msg;
}

bool ygp_message_warns(void)
{
    switch (yge_prog_message(&ygp_s.prog, NULL)) {
    case YGE_PROG_MSG_NONE:
    case YGE_PROG_MSG_HAND_WAIT:
    case YGE_PROG_MSG_READ_DONE:
    case YGE_PROG_MSG_WRITE_DONE:
    case YGE_PROG_MSG_LEFT:
    case YGE_PROG_MSG_CLEARED:
    case YGE_PROG_MSG_LOADED:
        return false;
    default:
        return true;
    }
}

const char *ygp_state_text(void)
{
    static const ui_text_id_t k_state[LINK_YGE_STATE_COUNT] = {
        [LINK_YGE_FREE] = TX_YG_ST_FREE,
        [LINK_YGE_STOPPING] = TX_YG_ST_STOPPING,
        [LINK_YGE_OPEN] = TX_YG_ST_OPEN,
        [LINK_YGE_POWER_OFF] = TX_YG_ST_POWER_OFF,
        [LINK_YGE_POWER_ON] = TX_YG_ST_POWER_ON,
        [LINK_YGE_HAND_WAIT] = TX_YG_ST_HAND,
        [LINK_YGE_RUN] = TX_YG_ST_RUN,
        [LINK_YGE_READY] = TX_YG_ST_READY,
        [LINK_YGE_NOT_BACK] = TX_YG_ST_NOT_BACK,
    };
    const uint8_t st = ygp_s.prog.f.rd.state;

    return ui_tr(st < LINK_YGE_STATE_COUNT ? k_state[st] : TX_YG_ST_FREE);
}

void ygp_session_text(char *out, size_t size)
{
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;

    out[0] = '\0';
    if (rd->state != (uint8_t)LINK_YGE_RUN) {
        return;
    }
    switch (rd->ses_state) {
    case OY_ST_WAIT:
        snprintf(out, size, TR(YG_SS_WAIT), (unsigned)ygp_s.boot_ms / 1000u,
                 (unsigned)ygp_s.boot_ms % 1000u / 100u);
        break;
    case OY_ST_LISTEN:
        snprintf(out, size, "%s", TR(YG_SS_LISTEN));
        break;
    case OY_ST_TAKE:
        snprintf(out, size, "%s", TR(YG_SS_TAKE));
        break;
    case OY_ST_READ:
        snprintf(out, size, TR(YG_SS_READ), (unsigned)rd->seen);
        break;
    case OY_ST_RECHECK:
        snprintf(out, size, "%s", TR(YG_SS_RECHECK));
        break;
    case OY_ST_WRITE:
        snprintf(out, size, TR(YG_SS_WRITE), (unsigned)rd->writing);
        break;
    case OY_ST_VERIFY:
        snprintf(out, size, "%s", TR(YG_SS_VERIFY));
        break;
    default:
        break;
    }
}

void ygp_line_text(char *out, size_t size)
{
    const yge_link_readout_t *rd = &ygp_s.prog.f.rd;

    if (!rd->known || rd->gpio >= 64u) {
        snprintf(out, size, "%s", TR(EW_NO_PIN));
        return;
    }
    snprintf(out, size,
             rd->line == (uint8_t)LINK_YGE_LINE_LISTEN ? TR(YG_LINE_LISTEN)
                                                       : TR(YG_LINE_PARK),
             (unsigned)rd->gpio);
}
