/**
 * @file kst_panel_text.c
 * @brief The texts of the programmer's overlay.
 *
 * The texts of the programmer's overlay for KST and Chaservo servos: the
 * tables from the model's codes to the interface's words, and the lines
 * of a message.  The state and the seam are kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <stdio.h>

#include "kst_limits.h"
#include "kst_port.h"
#include "link_pages.h"
#include "ui_card_text.h"
#include "ui_text.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/* ---------------------------------------------------------------- the texts */

/** @brief A message's first line and, where it has one, its second. */
static const int16_t k_fixed[][2] = {
    [KST_PROG_MSG_NONE]            = { -1, -1 },
    [KST_PROG_MSG_READ]            = { TX_KP_M_READ, -1 },
    [KST_PROG_MSG_READ_ONLY]       = { TX_KP_M_READ_ONLY, TX_KP_M_READ_ONLY_2 },
    [KST_PROG_MSG_WRITTEN]         = { TX_KP_M_WRITTEN, -1 },
    [KST_PROG_MSG_VERIFIED]        = { TX_KP_M_VERIFIED, -1 },
    [KST_PROG_MSG_VERIFY_DIFF]     = { TX_KP_M_VERIFY_DIFF, -1 },
    [KST_PROG_MSG_RESTORED]        = { TX_KP_M_RESTORED, -1 },
    [KST_PROG_MSG_RELEASED]        = { TX_KP_M_RELEASED, -1 },
    [KST_PROG_MSG_LEFT]            = { TX_KP_M_LEFT, -1 },
    [KST_PROG_MSG_SAVED]           = { TX_KP_M_SAVED, -1 },
    [KST_PROG_MSG_CARD_NONE]       = { TX_KP_M_CARD_NONE, -1 },
    [KST_PROG_MSG_STORE_FAILED]    = { TX_KP_M_STORE_FAILED, -1 },
    [KST_PROG_MSG_FILE_BAD]        = { TX_KP_M_FILE_BAD, -1 },
    [KST_PROG_MSG_JOURNAL]         = { TX_KP_M_JOURNAL, TX_KP_M_JOURNAL_2 },
    [KST_PROG_MSG_JOURNAL_SAME]    = { TX_KP_M_JOURNAL_ASK,
                                       TX_KP_M_JOURNAL_SAME },
    [KST_PROG_MSG_JOURNAL_DONE]    = { TX_KP_M_JOURNAL_ASK,
                                       TX_KP_M_JOURNAL_DONE },
    [KST_PROG_MSG_JOURNAL_DIFF]    = { TX_KP_M_INCONSISTENT, TX_KP_M_DO_NOT_USE },
    [KST_PROG_MSG_FORGOTTEN]       = { TX_KP_M_FORGOTTEN, -1 },
    [KST_PROG_MSG_EDIT_HARD]       = { TX_KP_M_OUTSIDE, -1 },
    [KST_PROG_MSG_EDIT_LOCKED]     = { TX_KP_M_EDIT_LOCKED, -1 },
    [KST_PROG_MSG_NO_CHANGES]      = { TX_KP_M_NO_CHANGES, -1 },
    [KST_PROG_MSG_NEED_ENABLE]     = { TX_KP_M_NEED_ENABLE, -1 },
    [KST_PROG_MSG_NEED_READ]       = { TX_KP_M_NEED_READ, -1 },
    [KST_PROG_MSG_NEED_PORT]       = { TX_KP_M_NEED_PORT, -1 },
    [KST_PROG_MSG_NO_RESTORE]      = { TX_KP_M_NO_RESTORE, -1 },
    [KST_PROG_MSG_RESTORE_REFUSED] = { TX_KP_M_RESTORE_REFUSED, -1 },
    [KST_PROG_MSG_STOPPED]         = { TX_KP_M_STOPPED, -1 },
    [KST_PROG_MSG_TIMEOUT]         = { TX_KP_M_TIMEOUT, -1 },
    [KST_PROG_MSG_JOURNAL_OTHER]   = { TX_KP_M_JOURNAL_OTHER,
                                       TX_KP_M_JOURNAL_OTHER_2 },
    [KST_PROG_MSG_CARD_FULL]       = { TX_KP_M_CARD_FULL, -1 },
    [KST_PROG_MSG_CARD_WRITE]      = { TX_KP_M_CARD_WRITE, -1 },
    [KST_PROG_MSG_CARD_READ]       = { TX_KP_M_CARD_READ, -1 },
    [KST_PROG_MSG_NO_FILES]        = { TX_KP_M_NO_FILES, -1 },
    [KST_PROG_MSG_NAME_BAD]        = { TX_KP_M_NAME_BAD, -1 },
    [KST_PROG_MSG_NAME_EXISTS]     = { TX_KP_M_NAME_EXISTS, -1 },
    [KST_PROG_MSG_LOAD_BAD]        = { TX_KP_M_LOAD_BAD, -1 },
    [KST_PROG_MSG_LOAD_CHECKSUM]   = { TX_KP_M_LOAD_CHECKSUM,
                                       TX_KP_M_LOAD_CHECKSUM_2 },
    [KST_PROG_MSG_LOAD_NEWER]      = { TX_KP_M_LOAD_NEWER, -1 },
    [KST_PROG_MSG_LOAD_TYPE]       = { TX_KP_M_LOAD_TYPE, -1 },
    [KST_PROG_MSG_LOAD_VALUE]      = { TX_KP_M_LOAD_VALUE, -1 },
    [KST_PROG_MSG_LOAD_REFUSED]    = { TX_KP_M_LOAD_REFUSED, -1 },
    [KST_PROG_MSG_LOAD_SAME]       = { TX_KP_M_LOAD_SAME, -1 },
    [KST_PROG_MSG_LOAD_SOFT]       = { TX_KP_M_LOAD_SOFT, -1 },
    [KST_PROG_MSG_LOADED]          = { TX_KP_M_LOADED, -1 },
    [KST_PROG_MSG_LOADED_EDITED]   = { TX_KP_M_LOADED, TX_KP_FILE_EDITED },
    [KST_PROG_MSG_LOADED_UNTYPED]  = { TX_KP_M_LOADED, TX_KP_FILE_UNTYPED },
    [KST_PROG_MSG_NEED_TARGET]     = { TX_KP_NEED_TARGET, TX_KP_NEED_TARGET_2 },
    [KST_PROG_MSG_LEAVE_SUPPLY]    = { TX_KP_M_LEAVE_SUPPLY,
                                       TX_KP_M_LEAVE_SUPPLY_2 },
    [KST_PROG_MSG_LEFT_NO_WAIT]    = { TX_KP_M_LEFT, TX_KP_M_LEFT_NO_WAIT },
    [KST_PROG_MSG_LEFT_DOUBT]      = { TX_KP_M_LEFT, TX_KP_M_LEFT_DOUBT },
    [KST_PROG_MSG_LEAVE_CUT]       = { TX_KP_M_LEAVE_CUT, -1 },
    [KST_PROG_MSG_NEED_DISARM]     = { TX_KP_M_NEED_DISARM, -1 },
    [KST_PROG_MSG_BACKUP_OTHER]    = { TX_KP_M_BACKUP_OTHER,
                                       TX_KP_M_BACKUP_OTHER_2 },
    [KST_PROG_MSG_SESSION_ENDED]   = { TX_KP_M_SESSION_ENDED,
                                       TX_KP_M_SESSION_ENDED_2 },
    [KST_PROG_MSG_UNKNOWN]         = { TX_KP_M_UNKNOWN_CODE, -1 },
    [KST_PROG_MSG_CARD_DENIED]     = { TX_KP_FILE_DENIED, -1 },
    [KST_PROG_MSG_SUPPLY_CAPS]     = { TX_KP_M_SUPPLY_CAPS,
                                       TX_KP_M_SUPPLY_CAPS_2 },
    [KST_PROG_MSG_NEED_TYPE]       = { TX_KP_M_NEED_TYPE, -1 },
    [KST_PROG_MSG_CYCLE_CUT]       = { TX_KP_M_CYCLE_CUT, -1 },
    [KST_PROG_MSG_LEFT_CYCLED]     = { TX_KP_M_LEFT, TX_KP_M_LEFT_CYCLED },
    [KST_PROG_MSG_LEFT_NOTE]       = { TX_KP_M_LEFT, TX_KP_M_LEFT_NOTE },
    [KST_PROG_MSG_LEFT_CYCLED_OLD] = { TX_KP_M_LEFT_CYCLED,
                                       TX_KP_M_CYCLED_NO_WAIT },
};
_Static_assert(KST_PROG_MSG_LEFT_CYCLED_OLD < KST_PROG_MSG_START,
               "the fixed messages end below the start results");
/* KP_M_ACT_AT_LEAVE and KP_M_LEFT_CYCLED name the off time. */
_Static_assert(KST_PORT_RAIL_OFF_MS == 1000u,
               "the texts of the supply cycle name 1s");
/** @brief The number of rows in k_fixed. */
#define FIXED_COUNT ((unsigned)(sizeof(k_fixed) / sizeof(k_fixed[0])))

/** @brief Text of each kst_link_start_t result. */
static const int16_t k_start[] = {
    [KST_LINK_START_OK]        = TX_KP_ST_OK,
    [KST_LINK_START_DOWN]      = TX_KP_ST_DOWN,
    [KST_LINK_START_NO_PAGE]   = TX_KP_ST_NO_PAGE,
    [KST_LINK_START_BUSY]      = TX_KP_ST_BUSY,
    [KST_LINK_START_ARG]       = TX_KP_ST_ARG,
    [KST_LINK_START_NO_ENABLE] = TX_KP_M_NEED_ENABLE,
    [KST_LINK_START_NO_IMAGE]  = TX_KP_M_NEED_READ,
    [KST_LINK_START_NO_OP]     = TX_KP_ST_NO_OP,
};
/** @brief The number of rows in k_start. */
#define START_COUNT ((unsigned)(sizeof(k_start) / sizeof(k_start[0])))

/** @brief Text of each kst_link_outcome_t. */
static const int16_t k_out[] = {
    [KST_LINK_OUT_NONE]      = TX_KP_O_NONE,
    [KST_LINK_OUT_DONE]      = TX_KP_O_DONE,
    [KST_LINK_OUT_REFUSED]   = TX_KP_O_REFUSED,
    [KST_LINK_OUT_NACK]      = TX_KP_O_NACK,
    [KST_LINK_OUT_NO_ANSWER] = TX_KP_O_NO_ANSWER,
    [KST_LINK_OUT_RESTARTED] = TX_KP_O_RESTARTED,
    [KST_LINK_OUT_DROPPED]   = TX_KP_O_DROPPED,
};
/** @brief The number of rows in k_out. */
#define OUT_COUNT ((unsigned)(sizeof(k_out) / sizeof(k_out[0])))

/** @brief Text of each LINK_KST_REF_* refusal. */
static const int16_t k_ref[] = {
    [LINK_KST_REF_NONE]            = TX_KP_O_DONE,
    [LINK_KST_REF_BUSY]            = TX_KP_R_BUSY,
    [LINK_KST_REF_PORT]            = TX_KP_R_PORT,
    [LINK_KST_REF_UNSAFE]          = TX_KP_R_UNSAFE,
    [LINK_KST_REF_NOT_PROGRAMMING] = TX_KP_R_NOT_PROGRAMMING,
    [LINK_KST_REF_NO_ENABLE]       = TX_KP_M_NEED_ENABLE,
    [LINK_KST_REF_STAGED]          = TX_KP_R_STAGED,
    [LINK_KST_REF_START]           = TX_KP_R_START,
    [LINK_KST_REF_MUST_READ]       = TX_KP_R_MUST_READ,
    [LINK_KST_REF_NO_IMAGE]        = TX_KP_M_NEED_READ,
    [LINK_KST_REF_PLAN_RULES]      = TX_KP_R_PLAN_RULES,
    [LINK_KST_REF_PLAN_R00]        = TX_KP_R_PLAN_R00,
    [LINK_KST_REF_PLAN_PAIRING]    = TX_KP_R_PLAN_PAIRING,
    [LINK_KST_REF_PLAN_NO_PATH]    = TX_KP_R_PLAN_NO_PATH,
    [LINK_KST_REF_NO_PIN]          = TX_KP_R_NO_PIN,
    [LINK_KST_REF_SUPPLY_RANGE]    = TX_KP_R_SUPPLY_RANGE,
    [LINK_KST_REF_NO_SUPPLY]       = TX_KP_R_NO_SUPPLY,
    [LINK_KST_REF_LEAVE_MS]        = TX_KP_R_LEAVE_MS,
    [LINK_KST_REF_LOCKED]          = TX_KP_R_LOCKED,
    [LINK_KST_REF_SUPPLY_LOW]      = TX_KP_R_SUPPLY_LOW,
};
/** @brief The number of rows in k_ref. */
#define REF_COUNT ((unsigned)(sizeof(k_ref) / sizeof(k_ref[0])))

/** @brief Text of each KST_SES_* session result. */
static const int16_t k_ses[] = {
    [KST_SES_OK]               = TX_KP_O_DONE,
    [KST_SES_BUSY]             = TX_KP_ST_BUSY,
    [KST_SES_ERR_BUSY]         = TX_KP_S_BUSY,
    [KST_SES_ERR_ARG]          = TX_KP_S_ARG,
    [KST_SES_ERR_NO_SERVO]     = TX_KP_S_NO_SERVO,
    [KST_SES_ERR_NOT_IN_MODE]  = TX_KP_S_NOT_IN_MODE,
    [KST_SES_ERR_READ]         = TX_KP_S_READ,
    [KST_SES_ERR_NO_BACKUP]    = TX_KP_S_NO_BACKUP,
    [KST_SES_ERR_FINGERPRINT]  = TX_KP_M_READ_ONLY,
    [KST_SES_ERR_LOCKED]       = TX_KP_S_LOCKED,
    [KST_SES_ERR_BAD_PLAN]     = TX_KP_S_BAD_PLAN,
    [KST_SES_ERR_UNCONFIRMED]  = TX_KP_S_UNCONFIRMED,
    [KST_SES_ERR_CHANGED]      = TX_KP_S_CHANGED,
    [KST_SES_ERR_WRITE_FAILED] = TX_KP_S_WRITE_FAILED,
    [KST_SES_ERR_ROLLED_BACK]  = TX_KP_S_ROLLED_BACK,
    [KST_SES_ERR_TORN]         = TX_KP_S_TORN,
    [KST_SES_ERR_UNINTENDED]   = TX_KP_S_UNINTENDED,
    [KST_SES_ERR_VERIFY]       = TX_KP_S_VERIFY,
    [KST_SES_ERR_DRIVER]       = TX_KP_S_DRIVER,
    [KST_SES_ERR_ABORTED]      = TX_KP_S_ABORTED,
    [KST_SES_ERR_NO_PLAN]      = TX_KP_R_PLAN_NO_PATH,
    [LINK_KST_RESULT_SUPPLY]   = TX_KP_S_SUPPLY,
    [LINK_KST_RESULT_NO_PATH]  = TX_KP_R_PLAN_NO_PATH,
    [LINK_KST_RESULT_TOO_COMPLEX] = TX_KP_S_TOO_COMPLEX,
};
/** @brief The number of rows in k_ses. */
#define SES_COUNT ((unsigned)(sizeof(k_ses) / sizeof(k_ses[0])))

const int16_t kpn_rule[KST_RULE_COUNT] = {
    [KST_H_DUTY_HIGH]          = TX_KP_H_DUTY_HIGH,
    [KST_H_DUTY_LOW]           = TX_KP_H_DUTY_LOW,
    [KST_H_DUTY_COPY]          = TX_KP_H_DUTY_COPY,
    [KST_H_BOOST_HIGH]         = TX_KP_H_BOOST_HIGH,
    [KST_H_STRETCH_INDEX]      = TX_KP_H_STRETCH_INDEX,
    [KST_H_SPD_HIGH]           = TX_KP_H_SPD_HIGH,
    [KST_H_SPD_SEL_LOW_SPD]    = TX_KP_H_SPD_SEL_LOW_SPD,
    [KST_H_UNCONT_POS_LOW]     = TX_KP_H_UNCONT_POS_LOW,
    [KST_H_UNCONT_POS_HIGH]    = TX_KP_H_UNCONT_POS_HIGH,
    [KST_H_UNCONT_POS_SPAN]    = TX_KP_H_UNCONT_POS_SPAN,
    [KST_H_UNCONT_TIME_LOW]    = TX_KP_H_UNCONT_TIME_LOW,
    [KST_H_UNCONT_TIME_HIGH]   = TX_KP_H_UNCONT_TIME_HIGH,
    [KST_H_LOWER_LOW]          = TX_KP_H_LOWER_LOW,
    [KST_H_LOWER_MARGIN]       = TX_KP_H_LOWER_MARGIN,
    [KST_H_UPPER_HIGH]         = TX_KP_H_UPPER_HIGH,
    [KST_H_UPPER_MARGIN]       = TX_KP_H_UPPER_MARGIN,
    [KST_H_SPAN]               = TX_KP_H_SPAN,
    [KST_H_NEUTRAL_LOW]        = TX_KP_H_NEUTRAL_LOW,
    [KST_H_NEUTRAL_HIGH]       = TX_KP_H_NEUTRAL_HIGH,
    [KST_H_LEFT_ZERO]          = TX_KP_H_LEFT_ZERO,
    [KST_H_RIGHT_ZERO]         = TX_KP_H_RIGHT_ZERO,
    [KST_H_GYRO_MODE]          = TX_KP_H_GYRO_MODE,
    [KST_H_LOCKED]             = TX_KP_H_LOCKED,
    [KST_H_NEVER]              = TX_KP_H_NEVER,
    [KST_S_DUTY_RAISED]        = TX_KP_W_DUTY_RAISED,
    [KST_S_DUTY_BELOW_HALF]    = TX_KP_W_DUTY_BELOW_HALF,
    [KST_S_BOOST_RAISED]       = TX_KP_W_BOOST_RAISED,
    [KST_S_STRETCH_RAISED]     = TX_KP_W_STRETCH_RAISED,
    [KST_S_SOFT_START_OFF]     = TX_KP_W_SOFT_START_OFF,
    [KST_S_SPD_SEL_ON]         = TX_KP_W_SPD_SEL_ON,
    [KST_S_SPD_SLOW]           = TX_KP_W_SPD_SLOW,
    [KST_S_DEAD_BAND_NARROWED] = TX_KP_W_DEAD_BAND_NARROWED,
    [KST_S_DEAD_BAND_BELOW_1US] = TX_KP_W_DEAD_BAND_BELOW_1US,
    [KST_S_ALLOW_UNCONT_ON]    = TX_KP_W_ALLOW_UNCONT_ON,
    [KST_S_UNCONT_POS_FAR]     = TX_KP_W_UNCONT_POS_FAR,
    [KST_S_UNCONT_TIME_SHORT]  = TX_KP_W_UNCONT_TIME_SHORT,
    [KST_S_LOWER_NEAR]         = TX_KP_W_END_NEAR,
    [KST_S_LOWER_MOVED]        = TX_KP_W_END_MOVED,
    [KST_S_UPPER_NEAR]         = TX_KP_W_END_NEAR,
    [KST_S_UPPER_MOVED]        = TX_KP_W_END_MOVED,
    [KST_S_NEUTRAL_MOVED]      = TX_KP_W_NEUTRAL_MOVED,
    [KST_S_REVERSION_CHANGED]  = TX_KP_W_REVERSION_CHANGED,
    [KST_S_LEFT_RAISED]        = TX_KP_W_ANGLE_RAISED,
    [KST_S_LEFT_SMALL]         = TX_KP_W_ANGLE_SMALL,
    [KST_S_RIGHT_RAISED]       = TX_KP_W_ANGLE_RAISED,
    [KST_S_RIGHT_SMALL]        = TX_KP_W_ANGLE_SMALL,
    [KST_S_GYRO_CHANGED]       = TX_KP_W_GYRO_CHANGED,
    [KST_S_NARROW_BAND_GAIN]   = TX_KP_W_NARROW_BAND_GAIN,
    [KST_S_HIGH_DUTY_TRAVEL]   = TX_KP_W_HIGH_DUTY_TRAVEL,
};

/* ----------------------------------------------------------------- messages */

const char *kst_panel_rule_text(unsigned rule)
{
    return rule < (unsigned)KST_RULE_COUNT
               ? ui_tr((ui_text_id_t)kpn_rule[rule]) : TR(KP_M_OUTSIDE);
}

/** @brief A session result that leaves a servo nobody should use. */
static bool do_not_use(unsigned id)
{
    return id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_TORN
        || id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_UNINTENDED
        || id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_VERIFY;
}

/**
 * @brief Whether message @p id ends a write to the servo: a write, a
 *        restore, a pairing release, or a check after one.
 *
 * @param id the message id
 * @return true for those messages
 */
static bool wrote(unsigned id)
{
    return id == KST_PROG_MSG_WRITTEN || id == KST_PROG_MSG_RESTORED
        || id == KST_PROG_MSG_RELEASED || id == KST_PROG_MSG_VERIFIED;
}

/* The lines that count: 1 has its own text. */
void kpn_stay_line(char *out, unsigned n)
{
    if (n == 1u) {
        snprintf(out, KST_PANEL_LINE, "%s", TR(KP_STAY_ONE));
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_STAY), n);
    }
}

/* A line of the interrupted write's record, kst_prog_journal_arg() in
 * @p arg: it names the GPIO the write went through, and the port when the
 * record holds no pin. */
void kpn_journal_line(char *out, int text, unsigned arg)
{
    const unsigned port = arg & 0xFFu;
    const unsigned pin1 = arg >> 8;

    if (text == TX_KP_M_JOURNAL_ASK) {
        if (pin1 != 0u) {
            snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_ASK_PIN), pin1 - 1u);
        } else {
            snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_ASK), port);
        }
    } else if (text == TX_KP_M_JOURNAL_OTHER_2) {
        if (pin1 != 0u) {
            snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_OTHER_2_PIN),
                     pin1 - 1u);
        } else {
            snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_OTHER_2), port);
        }
    } else if (pin1 != 0u) {
        snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_2_PIN), pin1 - 1u);
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_M_JOURNAL_2), port);
    }
}

/* What an edited file is, with the fields it gives a value for where the
 * card's side counted them. */
void kpn_kind_line(char *out, int text)
{
    unsigned fields = 0;
    const unsigned named = kst_prog_load_named(&k.prog, &fields);

    if (fields == 0u) {
        snprintf(out, KST_PANEL_LINE, "%s", ui_tr((ui_text_id_t)text));
    } else if (text == TX_KP_FILE_EDITED) {
        snprintf(out, KST_PANEL_LINE, TR(KP_FILE_EDITED_N), named, fields);
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_FILE_UNTYPED_N), named, fields);
    }
}

/**
 * @brief Writes the line that says how many values stay as they are after a
 *        file load.
 */
static void file_stay_line(char *out, unsigned n)
{
    if (n == 1u) {
        snprintf(out, KST_PANEL_LINE, "%s", TR(KP_FILE_STAY_ONE));
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_FILE_STAY), n);
    }
}

void kpn_write_n(char *out, size_t size, unsigned n)
{
    if (n == 1u) {
        snprintf(out, size, "%s", TR(KP_WRITE_ONE));
    } else {
        snprintf(out, size, TR(KP_WRITE_N), n);
    }
}

int kst_panel_message_lines(const kst_prog_msg_t *m,
                            char out[KST_PANEL_MSG_LINES][KST_PANEL_LINE])
{
    const unsigned id = m->id;
    const unsigned arg = m->arg;
    int16_t first = -1, second = -1;
    int n = 0;

    if (id == KST_PROG_MSG_NONE) {
        return 0;
    }
    if (id == KST_PROG_MSG_EDIT_HARD) {
        snprintf(out[n++], KST_PANEL_LINE, "%s", kst_panel_rule_text(arg));
        return n;
    }
    if (id == KST_PROG_MSG_LOAD_BAD && arg != 0u) {
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_LINE), arg);
        return n;
    }
    if (id == KST_PROG_MSG_LOAD_VALUE) {
        const char *name = kpn_field_name(m->reg);

        if (name != NULL) {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_VALUE), arg, name);
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_VALUE_ANY), arg);
        }
        return n;
    }
    if (id == KST_PROG_MSG_CARD_NONE) {
        first = (int16_t)kpn_card_text_id(UI_CARD_AT_KP_MSG);
    } else if (id < FIXED_COUNT) {
        first = k_fixed[id][0];
        second = k_fixed[id][1];
    } else if (id >= KST_PROG_MSG_SES && id - KST_PROG_MSG_SES < SES_COUNT) {
        first = k_ses[id - KST_PROG_MSG_SES];
        if (arg != 0u && (first == TX_KP_S_WRITE_FAILED
                          || first == TX_KP_S_ROLLED_BACK)) {
            /* Writes before the one that failed are on the servo. */
            first = TX_KP_S_WRITE_PART;
        }
        if (id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_FINGERPRINT) {
            second = TX_KP_M_READ_ONLY_2;
        } else if (id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_LOCKED) {
            second = TX_KP_S_LOCKED_2;
        } else if (id == KST_PROG_MSG_SES + LINK_KST_RESULT_TOO_COMPLEX) {
            second = TX_KP_S_TOO_COMPLEX_2;
        } else if (do_not_use(id)) {
            second = TX_KP_M_DO_NOT_USE;
        }
    } else if (id >= KST_PROG_MSG_REF && id - KST_PROG_MSG_REF < REF_COUNT) {
        first = k_ref[id - KST_PROG_MSG_REF];
        if (id == KST_PROG_MSG_REF + (unsigned)LINK_KST_REF_LOCKED) {
            /* Only a port with a PD mini is asked to leave in doubt. */
            second = TX_KP_M_LEAVE_SUPPLY_2;
        }
    } else if (id >= KST_PROG_MSG_OUT && id - KST_PROG_MSG_OUT < OUT_COUNT) {
        first = k_out[id - KST_PROG_MSG_OUT];
        if (first == TX_KP_O_RESTARTED) {
            /* With no PD mini the servo is in programming mode still. */
            second = TX_KP_O_RESTARTED_2;
        }
    } else if (id >= KST_PROG_MSG_START
               && id - KST_PROG_MSG_START < START_COUNT) {
        first = k_start[id - KST_PROG_MSG_START];
    }
    /* A number the tables leave out between two names has no line. */
    if (first <= 0) {
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_UNKNOWN), id);
        return n;
    }
    /* The lines with a conversion are formatted by name, so that
     * tools/check_formats.py holds each to its argument. */
    switch (first) {
    case TX_KP_M_JOURNAL_ASK:
        kpn_journal_line(out[n++], first, arg);
        break;
    case TX_KP_M_WRITTEN:
        if (arg == 1u) {
            snprintf(out[n++], KST_PANEL_LINE, "%s", TR(KP_M_WRITTEN_ONE));
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_WRITTEN), arg);
        }
        break;
    case TX_KP_M_VERIFY_DIFF:
        if (arg == 1u) {
            snprintf(out[n++], KST_PANEL_LINE, "%s",
                     TR(KP_M_VERIFY_DIFF_ONE));
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_VERIFY_DIFF), arg);
        }
        break;
    case TX_KP_O_NACK:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_O_NACK), arg);
        break;
    case TX_KP_M_UNKNOWN_CODE:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_UNKNOWN_CODE), arg);
        break;
    case TX_KP_S_SUPPLY:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_S_SUPPLY),
                 (unsigned)KST_PORT_SUPPLY_MS);
        break;
    case TX_KP_M_SAVED:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_SAVED),
                 kpn_shown(kst_prog_card_name(&k.prog)));
        break;
    case TX_KP_M_LOAD_TYPE:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_TYPE), arg & 0xFFu,
                 arg >> 8);
        break;
    case TX_KP_M_LOAD_SAME:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_SAME),
                 kpn_shown(kst_prog_card_name(&k.prog)));
        break;
    case TX_KP_M_LOADED:
        if ((arg & 0xFFu) == 1u) {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOADED_ONE),
                     kpn_shown(kst_prog_card_name(&k.prog)));
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOADED),
                     kpn_shown(kst_prog_card_name(&k.prog)), arg & 0xFFu);
        }
        break;
    default:
        snprintf(out[n++], KST_PANEL_LINE, "%s",
                 ui_tr((ui_text_id_t)first));
        break;
    }
    if (id == KST_PROG_MSG_RESTORE_REFUSED
        || id == KST_PROG_MSG_LOAD_REFUSED) {
        snprintf(out[n++], KST_PANEL_LINE, "%s", kst_panel_rule_text(arg));
    } else if (id == KST_PROG_MSG_NO_RESTORE && arg != 0u) {
        kpn_stay_line(out[n++], arg);
    } else if (id == KST_PROG_MSG_LOAD_SAME && arg != 0u) {
        file_stay_line(out[n++], arg);
    }
    if (second == TX_KP_M_JOURNAL_2 || second == TX_KP_M_JOURNAL_OTHER_2) {
        kpn_journal_line(out[n++], second, arg);
    } else if (second == TX_KP_FILE_EDITED || second == TX_KP_FILE_UNTYPED) {
        kpn_kind_line(out[n++], second);
    } else if (second >= 0) {
        snprintf(out[n++], KST_PANEL_LINE, "%s",
                 ui_tr((ui_text_id_t)second));
    }
    if (first == TX_KP_M_LOADED && n < KST_PANEL_MSG_LINES) {
        if ((arg >> 8) != 0u) {
            file_stay_line(out[n++], arg >> 8);
        } else if (second < 0) {
            snprintf(out[n++], KST_PANEL_LINE, "%s", TR(KP_EDIT_NOTE_2));
        }
    }
    if (wrote(id) && kst_prog_cycle_owed(&k.prog)
        && n < KST_PANEL_MSG_LINES) {
        /* A written value acts after the servo's next power-up: LEAVE
         * makes it with a PD mini, the operator without one. */
        snprintf(out[n++], KST_PANEL_LINE, "%s",
                 kst_prog_leave_cycles(&k.prog) ? TR(KP_M_ACT_AT_LEAVE)
                                                : TR(KP_M_LEFT_NOTE));
    }
    if (id >= KST_PROG_MSG_SES && m->reg != 0xFFu && n < KST_PANEL_MSG_LINES) {
        if (arg == 1u) {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_AT_REG_ONE),
                     (unsigned)m->reg);
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_AT_REG),
                     (unsigned)m->reg, arg);
        }
    }
    return n;
}
