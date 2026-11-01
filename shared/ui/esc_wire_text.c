/**
 * @file
 * @brief The ESC signal-wire session's words: the model's messages, the refusals, why
 *        a page is not written, and a row's help line.  See esc_wire_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "esc_wire_int.h"
#include "ew_id.h"
#include "ew_session.h"
#include "link_pages.h"

/** @brief A message with a number in it, formatted. */
static char s_msg[EWP_LINE];

/**
 * @brief What the page refused.
 * @param ref A link_esc_ref_t.
 * @return The text.
 */
static ui_text_id_t refusal(uint16_t ref)
{
    switch (ref) {
    case LINK_ESC_REF_BUSY:
        return TX_EW_REF_BUSY;
    case LINK_ESC_REF_TARGET:
        return TX_EW_REF_TARGET;
    case LINK_ESC_REF_HELD:
        return TX_EW_REF_HELD;
    case LINK_ESC_REF_ARMED:
        return TX_EW_MSG_ARMED;
    case LINK_ESC_REF_UNSAFE:
        return TX_EW_REF_UNSAFE;
    case LINK_ESC_REF_NOT_OPEN:
        return TX_EW_REF_NOT_OPEN;
    case LINK_ESC_REF_STOPPED:
        return TX_EW_REF_STOPPED;
    case LINK_ESC_REF_NO_PIN:
        return TX_EW_REF_NO_PIN;
    case LINK_ESC_REF_RANGE:
        return TX_EW_REF_RANGE;
    case LINK_ESC_REF_NO_SUPPLY:
        return TX_EW_REF_NO_SUPPLY;
    case LINK_ESC_REF_SUPPLY_OFF:
        return TX_EW_REF_SUPPLY_OFF;
    case LINK_ESC_REF_STALE:
        return TX_EW_MSG_STALE;
    case LINK_ESC_REF_WRITING:
        return TX_EW_REF_WRITING;
    default:
        return TX_EW_REF_OTHER;
    }
}

/**
 * @brief How a session's operation ended.
 * @param ses An ew_ses_t.
 * @return The text.
 */
static ui_text_id_t session_result(uint16_t ses)
{
    switch (ses) {
    case EW_SES_ERR_SILENT:
        return TX_EW_SES_SILENT;
    case EW_SES_ERR_CONTACT:
        return TX_EW_SES_CONTACT;
    case EW_SES_ERR_NOT_KNOWN:
        return TX_EW_SES_NOT_KNOWN;
    case EW_SES_ERR_READ:
        return TX_EW_SES_READ;
    case EW_SES_ERR_CHANGED:
        return TX_EW_SES_CHANGED;
    case EW_SES_ERR_WRITE_FAILED:
        return TX_EW_SES_WRITE_FAILED;
    case EW_SES_ERR_TORN:
        return TX_EW_SES_TORN;
    case EW_SES_ERR_LOST:
        return TX_EW_SES_LOST;
    case EW_SES_ERR_ABORTED:
    case EW_SES_ERR_STOPPED:
        return TX_EW_MSG_CUT;
    case EW_SES_ERR_CLIPPED:
        return TX_EW_SES_CLIPPED;
    case EW_SES_ERR_ESC_ERROR:
        return TX_EW_SES_ESC_ERROR;
    case EW_SES_ERR_REFUSED:
        return TX_EW_SES_REFUSED;
    case EW_SES_ERR_NOT_WRITABLE:
        return TX_EW_MSG_READ_ONLY;
    case EW_SES_ERR_DIRTY:
        return TX_EW_SES_DIRTY;
    case EW_SES_ERR_ERASED:
        return TX_EW_MSG_ERASED;
    case EW_SES_ERR_BACKUP:
        return TX_EW_SES_BACKUP;
    default:
        return TX_EW_SES_OTHER;
    }
}

const char *ewp_message_text(void)
{
    uint16_t d = 0u;
    const esc_prog_msg_t m = esc_prog_message(&ewp_s.prog, &d);

    switch (m) {
    case ESC_PROG_MSG_NONE:
        return "";
    case ESC_PROG_MSG_ARMED:
        return TR(EW_MSG_ARMED);
    case ESC_PROG_MSG_STOPPED:
        return TR(EW_MSG_STOPPED);
    case ESC_PROG_MSG_NO_LINK:
        return TR(EW_MSG_NO_LINK);
    case ESC_PROG_MSG_NO_PAGE:
        return TR(EW_MSG_NO_PAGE);
    case ESC_PROG_MSG_BUSY:
        return TR(EW_MSG_BUSY);
    case ESC_PROG_MSG_HELD:
        return TR(EW_MSG_HELD);
    case ESC_PROG_MSG_RANGE:
        return TR(EW_MSG_RANGE);
    case ESC_PROG_MSG_NOT_IN_MODE:
    case ESC_PROG_MSG_NOT_READ:
        return TR(EW_NOT_READ);
    case ESC_PROG_MSG_READ_ONLY:
        return TR(EW_MSG_READ_ONLY);
    case ESC_PROG_MSG_ERASED:
        return TR(EW_MSG_ERASED);
    case ESC_PROG_MSG_STALE:
        return TR(EW_MSG_STALE);
    case ESC_PROG_MSG_NO_CHANGE:
        return TR(EW_NO_CHANGE);
    case ESC_PROG_MSG_BACKUP_FIRST:
        return TR(EW_MSG_BACKUP_FIRST);
    case ESC_PROG_MSG_REFUSED:
        return ui_tr(refusal(d));
    case ESC_PROG_MSG_NO_LINE:
        return TR(EW_MSG_NO_LINE);
    case ESC_PROG_MSG_SUPPLY:
        return TR(EW_MSG_SUPPLY);
    case ESC_PROG_MSG_TIMEOUT:
        return TR(EW_MSG_TIMEOUT);
    case ESC_PROG_MSG_CUT:
        return TR(EW_MSG_CUT);
    case ESC_PROG_MSG_SESSION:
        return ui_tr(session_result(d));
    case ESC_PROG_MSG_NOT_BACK:
        return TR(EW_MSG_NOT_BACK);
    case ESC_PROG_MSG_NO_ANSWER:
        return TR(EW_MSG_NO_ANSWER);
    case ESC_PROG_MSG_RESTARTED:
        return TR(EW_MSG_RESTARTED);
    case ESC_PROG_MSG_NACK:
        return TR(EW_MSG_NACK);
    case ESC_PROG_MSG_DROPPED:
        return TR(EW_MSG_DROPPED);
    case ESC_PROG_MSG_ABORTED:
        return TR(EW_MSG_ABORTED);
    case ESC_PROG_MSG_HAND_WAIT:
        return TR(EW_HAND_NOW);
    case ESC_PROG_MSG_READ_DONE:
        return (ewp_s.prog.f.rd.flags & LINK_EW_F_ERASED) != 0u ? TR(EW_ERASED_NO_BACKUP)
                                                                : TR(EW_MSG_READ_DONE);
    case ESC_PROG_MSG_WRITE_DONE:
        return TR(EW_MSG_WRITE_DONE);
    case ESC_PROG_MSG_RESTORE_DONE:
        return TR(EW_MSG_RESTORE_DONE);
    case ESC_PROG_MSG_LEFT:
        return TR(EW_MSG_LEFT);
    case ESC_PROG_MSG_CLEARED:
        return TR(EW_MSG_CLEARED);
    case ESC_PROG_MSG_FILE_REFUSED:
        snprintf(s_msg, sizeof(s_msg), TR(EW_MSG_FILE_REFUSED), (unsigned)d);
        return s_msg;
    case ESC_PROG_MSG_LOAD_REFUSED:
        return d == (uint16_t)EW_LOAD_FIRMWARE ? TR(EW_LOAD_FIRMWARE)
             : d == (uint16_t)EW_LOAD_LAYOUT   ? TR(EW_LOAD_LAYOUT)
             : d == (uint16_t)EW_LOAD_ERASED   ? TR(EW_MSG_ERASED)
             : d == (uint16_t)EW_LOAD_READ_ONLY ? TR(EW_MSG_READ_ONLY)
                                                : TR(EW_LOAD_VALUE);
    case ESC_PROG_MSG_RESTORE_REFUSED:
        snprintf(s_msg, sizeof(s_msg), TR(EW_MSG_RESTORE_REFUSED), (unsigned)d);
        return s_msg;
    case ESC_PROG_MSG_LOADED:
        snprintf(s_msg, sizeof(s_msg), TR(EW_MSG_LOADED), (unsigned)d);
        return s_msg;
    default:
        return "";
    }
}

bool ewp_message_warns(void)
{
    uint16_t d = 0u;

    switch (esc_prog_message(&ewp_s.prog, &d)) {
    case ESC_PROG_MSG_READ_DONE:
        return (ewp_s.prog.f.rd.flags & LINK_EW_F_ERASED) != 0u;
    case ESC_PROG_MSG_NONE:
    case ESC_PROG_MSG_WRITE_DONE:
    case ESC_PROG_MSG_RESTORE_DONE:
    case ESC_PROG_MSG_LEFT:
    case ESC_PROG_MSG_CLEARED:
    case ESC_PROG_MSG_LOADED:
    case ESC_PROG_MSG_HAND_WAIT:
    case ESC_PROG_MSG_NO_CHANGE:
        return false;
    default:
        return true;
    }
}

void ewp_why_text(uint8_t why, char *out, size_t size)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;

    switch (why) {
    case EW_WHY_OLD_LAYOUT:
        snprintf(out, size, TR(EW_WHY_LAYOUT), (unsigned)rd->layout);
        break;
    case EW_WHY_RESET_PAGE:
        snprintf(out, size, "%s", TR(EW_WHY_RESET));
        break;
    case EW_WHY_MARKER:
        snprintf(out, size, "%s", TR(EW_WHY_MARKER_T));
        break;
    case EW_WHY_OLD_REVISION:
    case EW_WHY_NEW_REVISION:
    case EW_WHY_TEXT_REVISION:
        snprintf(out, size, TR(EW_WHY_REVISION), (unsigned)rd->layout);
        break;
    case EW_WHY_ERASED:
        snprintf(out, size, "%s", TR(EW_MSG_ERASED));
        break;
    case EW_WHY_CAN_BUILD:
        snprintf(out, size, "%s", TR(EW_WHY_CAN));
        break;
    case EW_WHY_SHOW_KEYS:
        snprintf(out, size, "%s", TR(EW_WHY_KEYS));
        break;
    default:
        snprintf(out, size, TR(EW_WHY_CODE), (unsigned)why);
        break;
    }
}

const char *ewp_fw_name(uint8_t fw)
{
    switch (fw) {
    case EW_FW_S:
        return "BLHeli_S";
    case EW_FW_BJ:
        return "Bluejay";
    case EW_FW_AM:
        return "AM32";
    case EW_FW_E32:
        return "ESCape32";
    default:
        return "-";
    }
}

void ewp_line_text(char *out, size_t size)
{
    const esc_link_readout_t *rd = &ewp_s.prog.f.rd;
    const char *word = rd->line == (uint8_t)LINK_ESC_LINE_PARK ? TR(EW_LINE_PARK)
                     : rd->line == (uint8_t)LINK_ESC_LINE_LOW  ? "LOW" : "HIGH";

    if (rd->gpio == 0xFFu) {
        snprintf(out, size, "%s", TR(EW_LINE_NONE));
    } else {
        snprintf(out, size, "GPIO %u %s", (unsigned)rd->gpio, word);
    }
}

const char *ewp_state_text(uint8_t state)
{
    switch (state) {
    case LINK_ESC_STOPPING:
        return TR(EW_ST_STOPPING);
    case LINK_ESC_OPEN:
        return TR(EW_ST_OPEN);
    case LINK_ESC_POWER_OFF:
        return TR(EW_ST_POWER_OFF);
    case LINK_ESC_LEAD:
        return TR(EW_ST_LEAD);
    case LINK_ESC_HAND_WAIT:
        return TR(EW_ST_HAND);
    case LINK_ESC_RUN:
        return TR(EW_ST_RUN);
    case LINK_ESC_READY:
        return TR(EW_ST_READY);
    case LINK_ESC_POWER_ON:
        return TR(EW_ST_POWER_ON);
    default:
        return TR(EW_ST_FREE);
    }
}

const char *ewp_help_text(const ewp_row_t *r)
{
    if (r->k != NULL) {
        return r->slot < EW_E32_KEYS ? esc_wire_help_key(r->slot) : "";
    }
    return r->f != NULL ? esc_wire_help_field(ewp_s.prog.f.rd.fw, r->f->off) : "";
}
