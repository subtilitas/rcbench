/*
 * The overlay of the programmer for KST and Chaservo servos.  See
 * kst_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_card.h"
#include "kst_limits.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "link_pages.h"
#include "settings.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

#define TAB_H    26
#define TAB_W    368
#define ROW0     48           /* below the area's top */
#define PITCH    42
#define ROW_H    34
#define VAL_W    100
#define WIDE_W   200          /* a wide row's value */
#define STEP_W   44           /* its minus and plus keys */
#define LINE_H   18

/* The plus and minus keys repeat as SETUP's do. */
#define REPEAT_DELAY_S 0.45f
#define REPEAT_SLOW_HZ 8.0f
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f

enum { PG_SERVO = 0, PG_TUNE, PG_PULSE, PG_FAILSAFE, PG_LOCKED, PG_COUNT };

enum { K_FIELD = 0, K_STEP, K_LOCKED, K_ACT };

enum {
    A_PORT = 0, A_READ, A_FILES, A_RESTORE, A_MORE, A_ENABLE,
    A_WRITE
};

typedef struct {
    uint8_t page, row, col, wide, kind, what;
    int16_t label;          /* a ui_text_id_t, or -1 for name */
    const char *name;       /* the field's name, as the vendor's tool has it */
} row_t;

static const row_t k_rows[] = {
    { PG_SERVO, 0, 0, 1, K_ACT, A_PORT, -1, "PORT" },
    { PG_SERVO, 1, 0, 0, K_ACT, A_READ, -1, "SERVO" },
    { PG_SERVO, 1, 1, 0, K_ACT, A_FILES, TX_KP_FILES, NULL },
    { PG_SERVO, 2, 0, 0, K_ACT, A_RESTORE, TX_KP_RESTORE, NULL },
    { PG_SERVO, 2, 1, 0, K_ACT, A_MORE, TX_KP_MORE, NULL },
    { PG_SERVO, 3, 0, 1, K_ACT, A_ENABLE, TX_KP_ENABLE, NULL },
    { PG_SERVO, 4, 0, 1, K_ACT, A_WRITE, TX_KP_CHANGES, NULL },

    { PG_TUNE, 0, 0, 1, K_STEP, KST_F_DEAD_BAND, -1, "DEAD BAND" },
    { PG_TUNE, 1, 0, 0, K_FIELD, KST_F_DUTY, -1, "MAX. DUTY" },
    { PG_TUNE, 1, 1, 0, K_FIELD, KST_F_BOOST, -1, "BOOST" },
    { PG_TUNE, 2, 0, 0, K_FIELD, KST_F_STRETCH, -1, "STRETCH" },
    { PG_TUNE, 2, 1, 0, K_FIELD, KST_F_SOFT_START, -1, "SOFT START" },
    { PG_TUNE, 3, 0, 0, K_FIELD, KST_F_SPD_SEL, TX_KP_SPEED_LIMIT, NULL },
    { PG_TUNE, 3, 1, 0, K_FIELD, KST_F_SPD, TX_KP_SPEED, NULL },

    { PG_PULSE, 0, 0, 1, K_STEP, KST_F_NEUTRAL, -1, "NEUTRAL" },
    { PG_PULSE, 1, 0, 0, K_FIELD, KST_F_PULSE_LOWER, -1, "PULSE LOWER" },
    { PG_PULSE, 1, 1, 0, K_FIELD, KST_F_PULSE_UPPER, -1, "PULSE UPPER" },
    { PG_PULSE, 2, 0, 0, K_FIELD, KST_F_LEFT_ANGLE, -1, "LEFT ANGLE" },
    { PG_PULSE, 2, 1, 0, K_FIELD, KST_F_RIGHT_ANGLE, -1, "RIGHT ANGLE" },
    { PG_PULSE, 3, 0, 0, K_FIELD, KST_F_REVERSION, -1, "REVERSION" },

    { PG_FAILSAFE, 0, 0, 1, K_FIELD, KST_F_ALLOW_UNCONT, TX_KP_FS_MOVE, NULL },
    { PG_FAILSAFE, 1, 0, 0, K_FIELD, KST_F_UNCONT_POS, -1, "POSITION" },
    { PG_FAILSAFE, 1, 1, 0, K_FIELD, KST_F_UNCONT_TIME, TX_KP_FS_AFTER, NULL },

    { PG_LOCKED, 0, 0, 0, K_LOCKED, KST_F_20K_SEL, TX_KP_TYPE, NULL },
    { PG_LOCKED, 0, 1, 0, K_LOCKED, KST_F_LOCK, -1, "LOCK" },
    { PG_LOCKED, 1, 0, 0, K_LOCKED, KST_F_GYRO, -1, "MODE" },
    { PG_LOCKED, 1, 1, 0, K_LOCKED, KST_F_PROT1_EN, -1, "PROT1 EN" },
    { PG_LOCKED, 2, 0, 0, K_LOCKED, KST_F_PROT1_CANCLE, -1, "PROT1 CANCLE" },
    { PG_LOCKED, 2, 1, 0, K_LOCKED, KST_F_PROT1_TIME, -1, "PROT1 TIME" },
    { PG_LOCKED, 3, 0, 0, K_LOCKED, KST_F_PROT1_PWM, -1, "PROT1 PWM" },
    { PG_LOCKED, 3, 1, 0, K_LOCKED, KST_F_PROT_POT_ERR, -1, "POT ERR" },
    { PG_LOCKED, 4, 0, 0, K_LOCKED, KST_F_POT_SAM_TIMES, -1, "POT SAMPLES" },
    { PG_LOCKED, 4, 1, 0, K_LOCKED, KST_F_PROT_PWM_ERR, -1, "PWM ERR" },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

static const ui_text_id_t k_pages[PG_COUNT] = {
    TX_KP_TAB_SERVO, TX_KP_TAB_TUNE, TX_KP_TAB_PULSE, TX_KP_TAB_FAILSAFE,
    TX_KP_TAB_LOCKED,
};

/* ---------------------------------------------------------------- the texts */

/* A message's first line and, where it has one, its second. */
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
    [KST_PROG_MSG_JOURNAL_SAME]    = { TX_KP_M_JOURNAL_SAME, -1 },
    [KST_PROG_MSG_JOURNAL_DONE]    = { TX_KP_M_JOURNAL_DONE, -1 },
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
};
#define FIXED_COUNT ((unsigned)(sizeof(k_fixed) / sizeof(k_fixed[0])))

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
#define START_COUNT ((unsigned)(sizeof(k_start) / sizeof(k_start[0])))

static const int16_t k_out[] = {
    [KST_LINK_OUT_NONE]      = TX_KP_O_NONE,
    [KST_LINK_OUT_DONE]      = TX_KP_O_DONE,
    [KST_LINK_OUT_REFUSED]   = TX_KP_O_REFUSED,
    [KST_LINK_OUT_NACK]      = TX_KP_O_NACK,
    [KST_LINK_OUT_NO_ANSWER] = TX_KP_O_NO_ANSWER,
    [KST_LINK_OUT_RESTARTED] = TX_KP_O_RESTARTED,
    [KST_LINK_OUT_DROPPED]   = TX_KP_O_DROPPED,
};
#define OUT_COUNT ((unsigned)(sizeof(k_out) / sizeof(k_out[0])))

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
};
#define REF_COUNT ((unsigned)(sizeof(k_ref) / sizeof(k_ref[0])))

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
#define SES_COUNT ((unsigned)(sizeof(k_ses) / sizeof(k_ses[0])))

static const int16_t k_rule[KST_RULE_COUNT] = {
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

/* -------------------------------------------------------------------- state */

enum {
    P_NONE = 0, P_TAB, P_CLOSE, P_ROW, P_MINUS, P_PLUS, P_KEYPAD, P_CHOICE,
    P_CHOICE_CANCEL, P_APPLY, P_CANCEL, P_TEXTKEY
};

enum { C_STRETCH = 0, C_RESTORE, C_MORE, C_FILES };
#define FILES_SAVE (-1)       /* the list's first entry: a new file */
#define FILES_NEXT (-2)       /* its last: the files behind these   */
enum { MORE_VERIFY = 0, MORE_DISCARD, MORE_RELEASE, MORE_VOLTAGE,
       MORE_CURRENT, MORE_WAIT };
#define RESTORE_FORGET ((int)KST_PROG_FROM_COUNT)

enum { W_SOFT = 0, W_ENABLE, W_WRITE, W_RESTORE, W_RELEASE, W_LEAVE,
       W_CHECK, W_HV };

/* The keypad edits what an ENTER asks of the supply, not a row. */
#define KP_VOLTAGE (-1)
#define KP_CURRENT (-2)
#define KP_WAIT    (-3)
#define CHOICE_MAX 8
#define WARN_LINES 9
/* The lines a page gives to the warnings of soft limits. */
#define WARN_RULES 7

static struct {
    kst_prog_t prog;
    gfx_rect_t area;
    bool       open;
    ui_tabs_t  tabs;
    bool       hv;             /* HV SERVO: above KST_PANEL_STD_MV may be set */
    bool       loaded;         /* the options were read from the settings   */
    bool       card_look;      /* a card is asked for and none is mounted   */

    ui_keypad_t kp;
    int        kp_row;

    ui_textkey_t tk;               /* a new file's name */
    char       name[KST_PROG_NAME_MAX];
    bool       name_wait;          /* the save of that name is on its way */
    bool       files_wait;         /* the list was asked for */
    uint32_t   files_seq;

    struct {
        bool open;
        int  kind;
        int  count;
        char title[64];
        char labels[CHOICE_MAX][KST_PANEL_LINE];
        int  values[CHOICE_MAX];
    } ch;

    struct {
        bool      open;
        int       kind;
        bool      danger;
        bool      down;
        ui_hold_t hold;
        int       count;
        char      title[64];
        char      apply[64];
        char      lines[WARN_LINES][KST_PANEL_LINE];
        uint8_t   level[WARN_LINES];
        bool      no_file;      /* the page says: no card, no file */
        uint16_t  mv;           /* W_HV: the voltage the hold sets */
        kst_prog_restore_t plan;
    } warn;

    bool    have;              /* a press is on the glass */
    uint8_t id;
    int     pressed;
    int     row;
    float   held_s;            /* on a plus or minus key */

    kst_prog_state_t state_seen;
    uint32_t sig;
    uint32_t rev;
    uint32_t hold_rev;
} k;

/* ------------------------------------------------------------------ helpers */

static unsigned ones(uint64_t v)
{
    unsigned n = 0;

    for (; v != 0u; v &= v - 1u) {
        ++n;
    }
    return n;
}

/* The programming port the overlay works on: the SERVO screen's target,
 * and the first port while that is the servo outputs. */
static uint8_t port_no(void)
{
    const uint8_t t = k.prog.f.target;

    return t != 0u ? (uint8_t)(t - 1u) : 0u;
}

/* The SERVO screen's tester drives a programming port. */
static bool on_port(void)
{
    return k.prog.f.target != 0u;
}

static bool flag(uint16_t f)
{
    return (k.prog.f.rd.flags & f) != 0u;
}

static bool programming(void)
{
    return kst_prog_state(&k.prog) == KST_PROG_PROGRAMMING;
}

static unsigned servo_type(void)
{
    return kst_prog_have_servo(&k.prog)
               ? kst_card_type(kst_prog_servo(&k.prog)) : 0u;
}

static const char *row_label(const row_t *r)
{
    if (r->kind == K_ACT && r->what == A_READ && servo_type() != 0u) {
        /* Static: the caller draws the label after this returns. */
        static char typed[24];

        snprintf(typed, sizeof(typed), TR(KP_SERVO_V), servo_type());
        return typed;
    }
    return r->label >= 0 ? ui_tr((ui_text_id_t)r->label) : r->name;
}

/* The name a field has on its row; NULL for one without a row. */
static const char *field_name(unsigned id)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind != K_ACT && k_rows[i].what == id) {
            return row_label(&k_rows[i]);
        }
    }
    return NULL;
}

static bool in_set(kst_field_set_t set, unsigned id)
{
    return (set & KST_FIELD_BIT(id)) != 0u;
}

static const char *unit_of(kst_field_id_t id)
{
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_TENTH_US:
        return "us";
    case KST_UNIT_PERCENT:
    case KST_UNIT_TENTH_PERCENT:
        return "%";
    case KST_UNIT_TENTH_S:
        return "s";
    default:
        return "";
    }
}

/* Digits after the point a field's displayed value has. */
static int decimals_of(kst_field_id_t id)
{
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_TENTH_US:
    case KST_UNIT_TENTH_PERCENT:
    case KST_UNIT_TENTH_S:
        return 1;
    default:
        return 0;
    }
}

static void fmt_field(kst_field_id_t id, uint16_t raw, char *out, size_t n)
{
    int32_t v = 0;

    if (id == KST_F_GYRO) {
        snprintf(out, n, "%s", raw != 0u ? "GYRO" : "SERVO");
        return;
    }
    if (!kst_field_display(id, raw, &v)) {
        snprintf(out, n, "%u", (unsigned)raw);
        return;
    }
    switch ((kst_unit_t)kst_field(id)->unit) {
    case KST_UNIT_BOOL:
        snprintf(out, n, "%s", v != 0 ? TR(ON) : TR(OFF));
        break;
    case KST_UNIT_FACTOR:
        snprintf(out, n, "x%d", (int)v);
        break;
    case KST_UNIT_TENTH_US:
    case KST_UNIT_TENTH_PERCENT:
    case KST_UNIT_TENTH_S:
        snprintf(out, n, "%d.%d %s", (int)(v / 10), (int)(v % 10),
                 unit_of(id));
        break;
    case KST_UNIT_PERCENT:
        snprintf(out, n, "%d %%", (int)v);
        break;
    default:
        snprintf(out, n, "%d", (int)v);
        break;
    }
}

static bool changed(kst_field_id_t id)
{
    return kst_prog_have_servo(&k.prog)
        && kst_field_get(kst_prog_draft(&k.prog), id)
           != kst_field_get(kst_prog_servo(&k.prog), id);
}

/* ----------------------------------------------------------------- geometry */

static gfx_rect_t tabs_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 10), (int16_t)(k.area.y + 8),
                         TAB_W, TAB_H };
}

static gfx_rect_t row_rect(const row_t *r)
{
    const int col_w = (k.area.w - 30) / 2;
    const int x = r->col == 0 ? k.area.x + 10 : k.area.x + 20 + col_w;

    return (gfx_rect_t){ (int16_t)x,
                         (int16_t)(k.area.y + ROW0 + r->row * PITCH),
                         (int16_t)(r->wide != 0u ? k.area.w - 20 : col_w),
                         ROW_H };
}

/* A row's controls: the value, and for a stepped row its keys beside it. */
static gfx_rect_t control_rect(const row_t *r)
{
    const gfx_rect_t rr = row_rect(r);
    const int w = r->wide != 0u ? WIDE_W : VAL_W;

    return (gfx_rect_t){ (int16_t)(rr.x + rr.w - w), rr.y, (int16_t)w, rr.h };
}

static gfx_rect_t value_rect(const row_t *r)
{
    const gfx_rect_t v = control_rect(r);

    if (r->kind != K_STEP) {
        return v;
    }
    return (gfx_rect_t){ (int16_t)(v.x + STEP_W + 4), v.y,
                         (int16_t)(v.w - 2 * (STEP_W + 4)), v.h };
}

static gfx_rect_t step_rect(const row_t *r, bool up)
{
    const gfx_rect_t v = control_rect(r);

    return (gfx_rect_t){ (int16_t)(up ? v.x + v.w - STEP_W : v.x), v.y,
                         STEP_W, v.h };
}

static const row_t *row_at(int page, int row, int col)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].page == page && k_rows[i].row == row
            && k_rows[i].col == col) {
            return &k_rows[i];
        }
    }
    return &k_rows[0];
}

gfx_rect_t kst_panel_tab_rect(int page)
{
    return k.tabs.rect[page >= 0 && page < PG_COUNT ? page : 0];
}

gfx_rect_t kst_panel_close_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 10 - 90),
                         (int16_t)(k.area.y + 6), 90, 30 };
}

gfx_rect_t kst_panel_value_rect(int page, int row, int col)
{
    return value_rect(row_at(page, row, col));
}

gfx_rect_t kst_panel_step_rect(int page, int row, bool up)
{
    return step_rect(row_at(page, row, 0), up);
}

gfx_rect_t kst_panel_choice_rect(int i)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 10),
                         (int16_t)(k.area.y + 48 + i * 46),
                         (int16_t)(k.area.w - 20), 40 };
}

gfx_rect_t kst_panel_choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 10 - 110),
                         (int16_t)(k.area.y + k.area.h - 46), 110, 36 };
}

gfx_rect_t kst_panel_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + 20),
                         (int16_t)(k.area.y + k.area.h - 76), 260, 56 };
}

gfx_rect_t kst_panel_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(k.area.x + k.area.w - 20 - 160),
                         (int16_t)(k.area.y + k.area.h - 76), 160, 56 };
}

ui_keypad_t *kst_panel_keypad(void)
{
    return &k.kp;
}

/* A file name as a line shows it: whole up to KST_PANEL_NAME_TYPED
 * characters, else its start and "..". */
static const char *shown(const char *name)
{
    static char s[KST_PANEL_NAME_TYPED + 1];

    if (strlen(name) <= (size_t)KST_PANEL_NAME_TYPED) {
        return name;
    }
    snprintf(s, sizeof(s), "%.*s..", KST_PANEL_NAME_TYPED - 2, name);
    return s;
}

ui_textkey_t *kst_panel_textkey(void)
{
    return &k.tk;
}

const char *kst_panel_warning_line(int i)
{
    return k.warn.open && i >= 0 && i < k.warn.count ? k.warn.lines[i] : NULL;
}

const char *kst_panel_label(int page, int row, int col)
{
    return row_label(row_at(page, row, col));
}

const char *kst_panel_choice_title(void)
{
    return k.ch.open ? k.ch.title : NULL;
}

const char *kst_panel_choice_label(int i)
{
    return k.ch.open && i >= 0 && i < k.ch.count ? k.ch.labels[i] : NULL;
}

int kst_panel_page(void)
{
    return k.tabs.selected;
}

int kst_panel_editor(void)
{
    return k.kp.open ? 1 : k.tk.open ? 4 : k.ch.open ? 2
         : k.warn.open ? 3 : 0;
}

/* ----------------------------------------------------------------- messages */

const char *kst_panel_rule_text(unsigned rule)
{
    return rule < (unsigned)KST_RULE_COUNT
               ? ui_tr((ui_text_id_t)k_rule[rule]) : TR(KP_M_OUTSIDE);
}

/* A session result that leaves a servo nobody should use. */
static bool do_not_use(unsigned id)
{
    return id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_TORN
        || id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_UNINTENDED
        || id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_VERIFY;
}

/* The lines that count: 1 has its own text. */
static void stay_line(char *out, unsigned n)
{
    if (n == 1u) {
        snprintf(out, KST_PANEL_LINE, "%s", TR(KP_STAY_ONE));
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_STAY), n);
    }
}

static void file_stay_line(char *out, unsigned n)
{
    if (n == 1u) {
        snprintf(out, KST_PANEL_LINE, "%s", TR(KP_FILE_STAY_ONE));
    } else {
        snprintf(out, KST_PANEL_LINE, TR(KP_FILE_STAY), n);
    }
}

static void write_n(char *out, size_t size, unsigned n)
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
        const char *name = field_name(m->reg);

        if (name != NULL) {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_VALUE), arg, name);
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_VALUE_ANY), arg);
        }
        return n;
    }
    if (id < FIXED_COUNT) {
        first = k_fixed[id][0];
        second = k_fixed[id][1];
    } else if (id >= KST_PROG_MSG_SES && id - KST_PROG_MSG_SES < SES_COUNT) {
        first = k_ses[id - KST_PROG_MSG_SES];
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
    case TX_KP_S_SUPPLY:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_S_SUPPLY),
                 (unsigned)KST_PORT_SUPPLY_MS);
        break;
    case TX_KP_M_SAVED:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_SAVED),
                 shown(kst_prog_card_name(&k.prog)));
        break;
    case TX_KP_M_LOAD_TYPE:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_TYPE), arg & 0xFFu,
                 arg >> 8);
        break;
    case TX_KP_M_LOAD_SAME:
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOAD_SAME),
                 shown(kst_prog_card_name(&k.prog)));
        break;
    case TX_KP_M_LOADED:
        if ((arg & 0xFFu) == 1u) {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOADED_ONE),
                     shown(kst_prog_card_name(&k.prog)));
        } else {
            snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_LOADED),
                     shown(kst_prog_card_name(&k.prog)), arg & 0xFFu);
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
        stay_line(out[n++], arg);
    } else if (id == KST_PROG_MSG_LOAD_SAME && arg != 0u) {
        file_stay_line(out[n++], arg);
    }
    if (second == TX_KP_M_JOURNAL_2) {
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_JOURNAL_2), arg);
    } else if (second == TX_KP_M_JOURNAL_OTHER_2) {
        snprintf(out[n++], KST_PANEL_LINE, TR(KP_M_JOURNAL_OTHER_2), arg);
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

/* ------------------------------------------------------------------ editors */

static void close_editors(void)
{
    if (k.warn.open && k.warn.kind == W_SOFT) {
        kst_prog_soft_cancel(&k.prog);
    }
    ui_keypad_close(&k.kp);
    ui_textkey_close(&k.tk);
    k.name_wait = false;
    k.files_wait = false;
    k.ch.open = false;
    k.warn.open = false;
    k.warn.down = false;
    ui_hold_reset(&k.warn.hold);
}

static void warn_open(int kind, bool danger, const char *title,
                      const char *apply)
{
    memset(&k.warn, 0, sizeof(k.warn));
    k.warn.open = true;
    k.warn.kind = kind;
    k.warn.danger = danger;
    snprintf(k.warn.title, sizeof(k.warn.title), "%s", title);
    snprintf(k.warn.apply, sizeof(k.warn.apply), "%s", apply);
}

static char *warn_line(kst_prog_level_t level)
{
    const int i = k.warn.count < WARN_LINES ? k.warn.count++ : WARN_LINES - 1;

    k.warn.level[i] = (uint8_t)level;
    return k.warn.lines[i];
}

static void warn_text(kst_prog_level_t level, const char *s)
{
    snprintf(warn_line(level), KST_PANEL_LINE, "%s", s);
}

/* The two lines every first write of a session shows. */
static void warn_horn(void)
{
    if (kst_prog_first_write(&k.prog)) {
        warn_text(KST_PROG_DANGER, TR(KP_HORN_FREE));
        warn_text(KST_PROG_WARN, TR(KP_BENCH_FIRST));
    }
}

/* The warnings of @p rules, one line each, on at most @p room lines.  More
 * than fit leave the last line to their count. */
static void warn_rules(kst_rules_t rules, int room)
{
    int16_t lines[KST_RULE_COUNT];
    int n = 0;

    for (unsigned rule = 0; rule < (unsigned)KST_RULE_COUNT; ++rule) {
        bool twice = false;

        if ((rules & KST_RULE_BIT(rule)) == 0u) {
            continue;
        }
        /* Two rules share a line where the line says the same of both. */
        for (int i = 0; i < n; ++i) {
            twice = twice || lines[i] == k_rule[rule];
        }
        if (!twice) {
            lines[n++] = k_rule[rule];
        }
    }
    for (int i = 0; i < n; ++i) {
        if (n > room && i == room - 1) {
            if (n - i == 1) {
                warn_text(KST_PROG_WARN, TR(KP_CHECK_MORE_ONE));
            } else {
                snprintf(warn_line(KST_PROG_WARN), KST_PANEL_LINE,
                         TR(KP_CHECK_MORE), (unsigned)(n - i));
            }
            break;
        }
        warn_text(KST_PROG_WARN, ui_tr((ui_text_id_t)lines[i]));
    }
}

static void warn_soft(const row_t *r)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;
    char was[24], now[24];

    warn_open(W_SOFT, false, TR(KP_SOFT_TITLE), TR(SV_HOLD_TO_APPLY));
    fmt_field(id, kst_field_get(kst_prog_draft(&k.prog), id), was,
              sizeof(was));
    fmt_field(id, kst_field_get(kst_prog_soft_image(&k.prog), id), now,
              sizeof(now));
    snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, "%s: %s -> %s",
             row_label(r), was, now);
    warn_rules(kst_prog_soft_rules(&k.prog), WARN_RULES - 1);
}

static void warn_enable(void)
{
    warn_open(W_ENABLE, true, TR(KP_ENABLE_TITLE), TR(KP_HOLD_ENABLE));
    warn_text(KST_PROG_DANGER, TR(KP_ENABLE_1));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_2));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_3));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_4));
    warn_text(KST_PROG_INFO, TR(KP_ENABLE_5));
}

/* A write that saves the servo to a file first, and no card to take one:
 * it goes without, and its page says so. */
static bool no_card(void)
{
    return kst_prog_save_first(&k.prog) && !k.prog.f.card;
}

/* What a write with no card leaves out, on the page whose hold sends it. */
static bool warn_no_file(void)
{
    if (!no_card()) {
        return false;
    }
    k.warn.no_file = true;
    warn_text(KST_PROG_WARN, TR(KP_NO_FILE_1));
    warn_text(KST_PROG_WARN, TR(KP_NO_FILE_2));
    warn_text(KST_PROG_WARN, TR(KP_NO_FILE_3));
    return true;
}

static void warn_card(void)
{
    if (warn_no_file()) {
        return;
    }
    if (kst_prog_save_first(&k.prog)) {
        warn_text(KST_PROG_INFO, TR(KP_WRITE_BACKUP));
    } else {
        warn_text(KST_PROG_INFO, k.prog.f.card ? TR(KP_WRITE_CARD)
                                               : TR(KP_WRITE_NO_CARD));
    }
}

/* The fields in which @p a differs from the servo, register 01's copy of
 * Max. Duty left out. */
static unsigned differing(const kst_image_t *a)
{
    unsigned n = 0;

    for (unsigned id = 0; id < KST_F_COUNT; ++id) {
        if (id != KST_F_DUTY_COPY
            && kst_field_get(a, (kst_field_id_t)id)
                   != kst_field_get(kst_prog_servo(&k.prog),
                                    (kst_field_id_t)id)) {
            ++n;
        }
    }
    return n;
}

static int kind_text(kst_prog_file_kind_t kind)
{
    return kind == KST_PROG_FILE_EDITED    ? TX_KP_FILE_EDITED
         : kind == KST_PROG_FILE_UNTYPED   ? TX_KP_FILE_UNTYPED
                                           : -1;
}

/* A file whose values pass soft limits: its warnings, and the draft takes
 * it after the hold. */
static void warn_file(void)
{
    const kst_prog_t *p = &k.prog;
    const int kind = kind_text(kst_prog_load_kind(p));

    warn_open(W_SOFT, false, TR(KP_FILE_TITLE), TR(KP_HOLD_LOAD));
    if (differing(kst_prog_soft_image(p)) == 1u) {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE,
                 TR(KP_FILE_DIFF_ONE), shown(kst_prog_card_name(p)));
    } else {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_FILE_DIFF),
                 shown(kst_prog_card_name(p)),
                 differing(kst_prog_soft_image(p)));
    }
    if (kind >= 0) {
        warn_text(KST_PROG_INFO, ui_tr((ui_text_id_t)kind));
    }
    warn_rules(kst_prog_soft_rules(p), WARN_RULES - k.warn.count);
}

static void warn_write(void)
{
    char title[64];

    write_n(title, sizeof(title), kst_prog_changes(&k.prog));
    warn_open(W_WRITE, true, title, TR(KP_HOLD_WRITE));
    warn_horn();
    warn_text(KST_PROG_INFO, TR(KP_WRITE_1));
    warn_text(KST_PROG_INFO, TR(KP_WRITE_2));
    warn_card();
}

static ui_text_id_t from_text(unsigned from)
{
    return from == KST_PROG_FROM_SESSION ? TX_KP_FROM_SESSION
                                         : TX_KP_FROM_JOURNAL;
}

static void warn_restore(const kst_prog_restore_t *plan)
{
    warn_open(W_RESTORE, true, TR(KP_RESTORE_TITLE), TR(KP_HOLD_RESTORE));
    k.warn.plan = *plan;
    warn_text(KST_PROG_INFO, ui_tr(from_text(plan->from)));
    if (plan->writes == 0xFFu) {
        warn_text(KST_PROG_INFO, TR(KP_RESTORE_ALL));
    } else if (plan->writes == 1u) {
        warn_text(KST_PROG_INFO, TR(KP_RESTORE_ONE));
    } else {
        snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_RESTORE_N),
                 (unsigned)plan->writes);
    }
    if (plan->stay != 0u) {
        stay_line(warn_line(KST_PROG_WARN), (unsigned)plan->stay);
    }
    if (plan->unchecked) {
        warn_text(KST_PROG_WARN, TR(KP_RESTORE_UNCHECKED));
    }
    warn_horn();
    if (plan->from != KST_PROG_FROM_SESSION) {
        warn_card();
    } else {
        (void)warn_no_file();
    }
}

/* A backup whose values pass soft limits the servo's own keep: its
 * warnings first, and the restore's page after the hold. */
static void warn_check(const kst_prog_restore_t *plan)
{
    warn_open(W_CHECK, false, TR(KP_CHECK_TITLE), TR(KP_HOLD_NEXT));
    k.warn.plan = *plan;
    warn_text(KST_PROG_INFO, ui_tr(from_text(plan->from)));
    warn_text(KST_PROG_INFO, TR(KP_CHECK_1));
    warn_rules(plan->soft, WARN_RULES - 2);
}

static void warn_release(void)
{
    warn_open(W_RELEASE, true, TR(KP_RELEASE_TITLE), TR(KP_HOLD_WRITE));
    warn_text(KST_PROG_WARN, TR(KP_RELEASE_1));
    warn_text(KST_PROG_INFO, TR(KP_RELEASE_2));
    warn_text(KST_PROG_INFO, TR(KP_RELEASE_3));
    warn_horn();
    (void)warn_no_file();
}

static void options_keep(void);

/* The way back to PWM, the same with and without a PD mini. */
static void warn_leave(void)
{
    warn_open(W_LEAVE, false, TR(KP_LEAVE_TITLE), TR(KP_HOLD_LEAVE));
    if (kst_prog_in_doubt(&k.prog)) {
        warn_text(KST_PROG_DANGER, TR(KP_LEAVE_DOUBT_1));
        warn_text(KST_PROG_DANGER, TR(KP_LEAVE_DOUBT_2));
    }
    if (kst_prog_changes(&k.prog) != 0u) {
        warn_text(KST_PROG_WARN, TR(KP_LEAVE_CHANGES));
    }
    warn_text(KST_PROG_INFO, TR(KP_LEAVE_1));
    warn_text(KST_PROG_WARN, TR(KP_LEAVE_2));
}

/* A programming voltage above what a standard servo is rated for. */
static void warn_hv(uint16_t mv)
{
    warn_open(W_HV, true, TR(KP_PROG_VOLTAGE), TR(SV_HOLD_TO_APPLY));
    k.warn.mv = mv;
    snprintf(warn_line(KST_PROG_INFO), KST_PANEL_LINE, TR(KP_SUPPLY_V),
             (unsigned)(mv / 1000u), (unsigned)(mv % 1000u / 100u));
    warn_text(KST_PROG_WARN, TR(SV_HV_1));
    warn_text(KST_PROG_WARN, TR(SV_HV_2));
    warn_text(KST_PROG_WARN, TR(SV_HV_3));
    warn_text(KST_PROG_DANGER, TR(SV_HV_4));
    warn_text(KST_PROG_INFO, TR(SV_HV_5));
}

/* The hold completed: what the page asked for. */
static void warn_apply(void)
{
    const int kind = k.warn.kind;
    const kst_prog_restore_t plan = k.warn.plan;
    const uint16_t mv = k.warn.mv;
    /* The card left the slot under a page that promised a file: the page
     * comes back with what a write without one leaves out, and nothing is
     * sent before its own hold. */
    const bool unsaid = no_card() && !k.warn.no_file;

    k.warn.open = false;
    k.warn.down = false;
    ui_hold_reset(&k.warn.hold);
    k.pressed = P_NONE;
    switch (kind) {
    case W_SOFT:
        kst_prog_soft_confirm(&k.prog);
        break;
    case W_ENABLE:
        (void)kst_prog_enable(&k.prog, true);
        break;
    case W_WRITE:
        if (unsaid) {
            warn_write();
        } else {
            (void)kst_prog_write(&k.prog);
        }
        break;
    case W_RESTORE:
        if (unsaid) {
            warn_restore(&plan);
        } else {
            (void)kst_prog_restore(&k.prog, &plan);
        }
        break;
    case W_CHECK:
        warn_restore(&plan);
        break;
    case W_RELEASE:
        if (unsaid) {
            warn_release();
        } else {
            (void)kst_prog_release(&k.prog);
        }
        break;
    case W_HV:
        if (k.hv) {
            kst_prog_supply_set(&k.prog, mv, kst_prog_supply_ma(&k.prog));
            options_keep();
        }
        break;
    default:
        (void)kst_prog_leave(&k.prog);
        break;
    }
}

static void choice_open(int kind, const char *title)
{
    k.ch.open = true;
    k.ch.kind = kind;
    k.ch.count = 0;
    snprintf(k.ch.title, sizeof(k.ch.title), "%s", title);
}

static void choice_add(const char *label, int value)
{
    if (k.ch.count < CHOICE_MAX) {
        snprintf(k.ch.labels[k.ch.count], sizeof(k.ch.labels[0]), "%s",
                 label);
        k.ch.values[k.ch.count++] = value;
    }
}

static void name_open(unsigned why)
{
    ui_textkey_open(&k.tk, k.area,
                    why == KST_PROG_MSG_NAME_EXISTS ? TR(KP_NAME_EXISTS)
                    : why == KST_PROG_MSG_NAME_BAD  ? TR(KP_NAME_BAD)
                                                    : TR(KP_NAME_TITLE),
                    k.name, KST_PANEL_NAME_TYPED);
}

/* The name is typed: the save, and the keyboard again for a name the card's
 * side does not take. */
static void name_done(const char *name)
{
    snprintf(k.name, sizeof(k.name), "%.*s", (int)sizeof(k.name) - 1, name);
    if (kst_prog_save(&k.prog, k.name) == KST_PROG_MSG_NONE) {
        k.name_wait = true;
    } else if (kst_prog_message(&k.prog)->id == KST_PROG_MSG_NAME_BAD) {
        name_open(KST_PROG_MSG_NAME_BAD);
    }
}

/* The list of files as the card gave it, under the entry that saves. */
static void files_open(void)
{
    const kst_prog_t *p = &k.prog;
    unsigned total = 0;
    const unsigned n = kst_prog_file_count(p, &total);
    char title[64];

    const unsigned first = kst_prog_files_first(p);

    if (total > n && n != 0u) {
        snprintf(title, sizeof(title), TR(KP_FILES_TITLE_N), first + 1u,
                 first + n, total);
    } else {
        snprintf(title, sizeof(title), "%s", TR(KP_FILES_TITLE));
    }
    choice_open(C_FILES, title);
    choice_add(TR(KP_FILE_SAVE), FILES_SAVE);
    for (unsigned i = 0; i < n; ++i) {
        char label[KST_PANEL_LINE];
        const unsigned type = kst_prog_file_type(p, i);

        if (type != 0u) {
            snprintf(label, sizeof(label), "%-16s V%u",
                     kst_prog_file_name(p, i), type);
        } else {
            snprintf(label, sizeof(label), "%s", kst_prog_file_name(p, i));
        }
        choice_add(label, (int)i);
    }
    if (total > n) {
        choice_add(TR(KP_FILES_NEXT), FILES_NEXT);
    }
}

/* Ask for the list from file @p first on. */
static void files_ask(unsigned first)
{
    k.files_seq = kst_prog_files_seq(&k.prog);
    k.files_wait = kst_prog_files(&k.prog, first) == KST_PROG_MSG_NONE;
}

/* What the card's side brought: the list, the name again, a file's
 * warnings. */
static void card_moved(void)
{
    const kst_prog_t *p = &k.prog;
    const unsigned id = kst_prog_message(p)->id;

    if (kst_prog_card_busy(p)) {
        return;
    }
    if (k.files_wait) {
        k.files_wait = false;
        if (kst_prog_files_seq(p) != k.files_seq && programming()
            && kst_panel_editor() == 0) {
            files_open();
        }
    }
    if (k.name_wait) {
        k.name_wait = false;
        if ((id == KST_PROG_MSG_NAME_BAD || id == KST_PROG_MSG_NAME_EXISTS)
            && programming() && kst_panel_editor() == 0) {
            name_open(id);
        }
    }
    if (kst_prog_soft_is_load(p) && kst_prog_soft_rules(p) != 0u
        && !k.warn.open) {
        ui_keypad_close(&k.kp);
        ui_textkey_close(&k.tk);
        k.ch.open = false;
        warn_file();
    }
}

/* What an edit came to: a warning page for a soft limit. */
static void edited(const row_t *r, kst_prog_edit_t e)
{
    if (e == KST_PROG_EDIT_SOFT) {
        warn_soft(r);
    }
}

static void edit_field(const row_t *r)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;
    const kst_unit_t unit = (kst_unit_t)kst_field(id)->unit;
    const uint16_t raw = kst_field_get(kst_prog_draft(&k.prog), id);
    uint16_t lo = 0, hi = 0;
    int32_t dlo = 0, dhi = 0, dv = 0;
    float scale;

    if (unit == KST_UNIT_BOOL) {
        edited(r, kst_prog_edit(&k.prog, id,
                                (uint16_t)(raw != 0u ? 0u : 1u)));
        return;
    }
    if (!kst_prog_editable(&k.prog)
        || !kst_prog_range(&k.prog, id, &lo, &hi)
        || !kst_field_display(id, lo, &dlo)
        || !kst_field_display(id, hi, &dhi)
        || !kst_field_display(id, raw, &dv)) {
        /* The model says why not. */
        (void)kst_prog_edit(&k.prog, id, raw);
        return;
    }
    if (unit == KST_UNIT_FACTOR) {
        choice_open(C_STRETCH, row_label(r));
        for (unsigned v = lo; v <= hi; ++v) {
            char label[16];

            fmt_field(id, (uint16_t)v, label, sizeof(label));
            choice_add(label, (int)v);
        }
        k.kp_row = (int)(r - k_rows);
        return;
    }
    scale = decimals_of(id) != 0 ? 10.0f : 1.0f;
    ui_keypad_open(&k.kp, k.area, row_label(r), unit_of(id),
                   (float)dv / scale,
                   (float)(dlo < dhi ? dlo : dhi) / scale,
                   (float)(dlo < dhi ? dhi : dlo) / scale, decimals_of(id));
    k.kp_row = (int)(r - k_rows);
}

/* The highest programming voltage the keypad takes: a standard servo's
 * until HV SERVO is on, then the port's. */
static uint16_t supply_max_mv(void)
{
    return k.hv ? (uint16_t)KST_PORT_SUPPLY_MAX_MV
                : (uint16_t)KST_PANEL_STD_MV;
}

/* The programmer's options as the settings keep them. */
static void options_keep(void)
{
    settings_set(SET_SERVO_PROG_V,
                 (float)kst_prog_supply_mv(&k.prog) / 1000.0f);
    settings_set(SET_SERVO_PROG_I,
                 (float)kst_prog_supply_ma(&k.prog) / 1000.0f);
    settings_set(SET_SERVO_PROG_WAIT, (float)kst_prog_wait_ms(&k.prog));
    settings_request_save();
}

/* The options from the settings.  A stored voltage above a standard
 * servo's is not used until HV SERVO is on. */
static void options_load(void)
{
    long mv = lroundf(settings_get(SET_SERVO_PROG_V) * 10.0f) * 100L;
    const long ma = lroundf(settings_get(SET_SERVO_PROG_I) * 1000.0f);
    const int wait = settings_get_int(SET_SERVO_PROG_WAIT);

    if (mv > (long)supply_max_mv()) {
        mv = (long)KST_PROG_SUPPLY_MV;
    }
    kst_prog_supply_set(&k.prog, (uint16_t)mv, (uint16_t)ma);
    (void)kst_prog_wait_set(&k.prog, (uint32_t)wait);
}

static void keypad_done(float v)
{
    if (k.kp_row == KP_VOLTAGE) {
        const long mv = lroundf(v * 10.0f) * 100L;

        if (mv < (long)PDMINI_V_MIN_MV || mv > (long)supply_max_mv()) {
            return;
        }
        if (mv > (long)KST_PANEL_STD_MV) {
            warn_hv((uint16_t)mv);
        } else {
            kst_prog_supply_set(&k.prog, (uint16_t)mv,
                                kst_prog_supply_ma(&k.prog));
            options_keep();
        }
        return;
    }
    if (k.kp_row == KP_CURRENT) {
        const long ma = lroundf(v);

        if (ma >= (long)PDMINI_I_MIN_MA && ma <= (long)PDMINI_I_MAX_MA) {
            kst_prog_supply_set(&k.prog, kst_prog_supply_mv(&k.prog),
                                (uint16_t)ma);
            options_keep();
        }
        return;
    }
    if (k.kp_row == KP_WAIT) {
        const long ms = lroundf(v);

        if (ms >= 0L && kst_prog_wait_set(&k.prog, (uint32_t)ms)) {
            options_keep();
        }
        return;
    }
    {
        const row_t *r = &k_rows[k.kp_row];
        const kst_field_id_t id = (kst_field_id_t)r->what;
        const float scale = decimals_of(id) != 0 ? 10.0f : 1.0f;
        uint16_t raw = 0;

        if (!kst_field_raw_from_display(id, (int32_t)lroundf(v * scale),
                                        &raw)) {
            raw = 0xFFFFu;      /* outside the field: the model says so */
        }
        edited(r, kst_prog_edit(&k.prog, id, raw));
    }
}

/* One step of a plus or minus key, in the direction the shown value moves. */
static void step(const row_t *r, bool up)
{
    const kst_field_id_t id = (kst_field_id_t)r->what;
    int32_t a = 0, b = 1;
    bool rising;

    (void)kst_field_display(id, 0, &a);
    (void)kst_field_display(id, 1, &b);
    rising = b >= a;
    edited(r, kst_prog_step(&k.prog, id, up == rising ? 1 : -1));
}

/* A LEAVE whose wait runs: an abort keeps the session. */
static bool leave_waits(const kst_prog_t *p)
{
    return kst_prog_state(p) == KST_PROG_LEAVING
        && kst_prog_op(p) == LINK_KST_OP_LEAVE;
}

static void act(const row_t *r)
{
    kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    /* A card put in since the last look is found before the next tap. */
    k.card_look = k.card_look || (p->fed && !p->f.card);
    switch (r->what) {
    case A_PORT:
        if (st == KST_PROG_PWM) {
            (void)kst_prog_connect(p, port_no());
        } else if (st == KST_PROG_PROGRAMMING) {
            warn_leave();
        } else if (st == KST_PROG_ENTERING || st == KST_PROG_WORKING
                   || leave_waits(p)) {
            kst_prog_abort(p);
        }
        break;
    case A_READ:
        (void)kst_prog_read(p);
        break;
    case A_FILES:
        files_ask(0);
        break;
    case A_RESTORE:
        choice_open(C_RESTORE, TR(KP_RESTORE_TITLE));
        for (unsigned from = KST_PROG_FROM_SESSION;
             from < (unsigned)KST_PROG_FROM_COUNT; ++from) {
            if (kst_prog_restore_offered(p, (kst_prog_from_t)from)) {
                choice_add(ui_tr(from_text(from)), (int)from);
            }
        }
        if (p->journal) {
            choice_add(TR(KP_FORGET), RESTORE_FORGET);
        }
        break;
    case A_MORE:
        choice_open(C_MORE, TR(KP_MORE));
        if (st == KST_PROG_PWM) {
            char label[KST_PANEL_LINE];
            const unsigned mv = kst_prog_supply_mv(p);

            snprintf(label, sizeof(label), TR(KP_SUPPLY_V), mv / 1000u,
                     mv % 1000u / 100u);
            choice_add(label, MORE_VOLTAGE);
            snprintf(label, sizeof(label), TR(KP_SUPPLY_A),
                     (unsigned)kst_prog_supply_ma(p));
            choice_add(label, MORE_CURRENT);
            snprintf(label, sizeof(label), TR(KP_PWM_WAIT),
                     (unsigned)kst_prog_wait_ms(p));
            choice_add(label, MORE_WAIT);
        } else {
            choice_add(TR(KP_VERIFY), MORE_VERIFY);
            choice_add(TR(KP_DISCARD), MORE_DISCARD);
            choice_add(TR(KP_RELEASE_TITLE), MORE_RELEASE);
        }
        break;
    case A_ENABLE:
        if (kst_prog_write_enabled(p)) {
            (void)kst_prog_enable(p, false);
        } else if (kst_prog_enable(p, true) == KST_PROG_MSG_NONE) {
            /* It can be: off again until the hold says so. */
            (void)kst_prog_enable(p, false);
            warn_enable();
        }
        break;
    default:
        if (kst_prog_write_check(p) == KST_PROG_MSG_NONE) {
            warn_write();
        } else {
            (void)kst_prog_write(p);    /* says why not */
        }
        break;
    }
}

static void choice_done(int value)
{
    kst_prog_t *p = &k.prog;
    const int kind = k.ch.kind;

    k.ch.open = false;
    if (kind == C_STRETCH) {
        const row_t *r = &k_rows[k.kp_row];

        edited(r, kst_prog_edit(p, (kst_field_id_t)r->what, (uint16_t)value));
    } else if (kind == C_RESTORE) {
        kst_prog_restore_t plan;

        if (value == RESTORE_FORGET) {
            kst_prog_forget(p);
        } else if (kst_prog_restore_plan(p, (kst_prog_from_t)value, &plan)
                   == KST_PROG_MSG_NONE) {
            if (kst_prog_write_enabled(p)) {
                if (plan.soft != 0u) {
                    warn_check(&plan);
                } else {
                    warn_restore(&plan);
                }
            } else {
                (void)kst_prog_restore(p, &plan);   /* says why not */
            }
        }
    } else if (kind == C_FILES) {
        if (value == FILES_SAVE) {
            snprintf(k.name, sizeof(k.name), "%s", kst_prog_preset_name(p));
            name_open(KST_PROG_MSG_NONE);
        } else if (value == FILES_NEXT) {
            unsigned total = 0;
            const unsigned next = kst_prog_files_first(p)
                                + kst_prog_file_count(p, &total);

            /* Behind the last file the list starts again. */
            files_ask(next < total ? next : 0u);
        } else {
            (void)kst_prog_load(p, kst_prog_file_name(p, (unsigned)value));
        }
    } else if (value == MORE_VOLTAGE) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PROG_VOLTAGE), "V",
                       (float)kst_prog_supply_mv(p) / 1000.0f,
                       (float)PDMINI_V_MIN_MV / 1000.0f,
                       (float)supply_max_mv() / 1000.0f, 1);
        k.kp_row = KP_VOLTAGE;
    } else if (value == MORE_CURRENT) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PROG_CURRENT), "mA",
                       (float)kst_prog_supply_ma(p), (float)PDMINI_I_MIN_MA,
                       (float)PDMINI_I_MAX_MA, 0);
        k.kp_row = KP_CURRENT;
    } else if (value == MORE_WAIT) {
        ui_keypad_open(&k.kp, k.area, TR(KP_PWM_WAIT_TITLE), "ms",
                       (float)kst_prog_wait_ms(p), 0.0f,
                       (float)KST_PROG_WAIT_MAX_MS, 0);
        k.kp_row = KP_WAIT;
    } else if (value == MORE_VERIFY) {
        (void)kst_prog_verify(p);
    } else if (value == MORE_DISCARD) {
        kst_prog_discard(p);
    } else if (kst_prog_release_check(p) == KST_PROG_MSG_NONE) {
        warn_release();
    } else {
        (void)kst_prog_release(p);      /* says why not */
    }
}

/* Whether a row's control takes a tap now. */
static bool row_live(const row_t *r)
{
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    if (r->kind == K_LOCKED) {
        return false;
    }
    if (kst_prog_card_busy(p) && st == KST_PROG_PROGRAMMING) {
        /* A list or a load is on its way: it finds the servo as it is. */
        return false;
    }
    if (r->kind != K_ACT) {
        return kst_prog_editable(p) && st == KST_PROG_PROGRAMMING;
    }
    switch (r->what) {
    case A_PORT:
        return (st == KST_PROG_PWM && on_port())
            || st == KST_PROG_PROGRAMMING
            || st == KST_PROG_ENTERING || st == KST_PROG_WORKING
            || leave_waits(p);
    case A_READ:
    case A_FILES:
        return st == KST_PROG_PROGRAMMING;
    case A_RESTORE:
        return p->journal
            || kst_prog_restore_offered(p, KST_PROG_FROM_SESSION)
            || kst_prog_restore_offered(p, KST_PROG_FROM_JOURNAL);
    case A_MORE:
        return (st == KST_PROG_PROGRAMMING && kst_prog_have_servo(p))
            || (st == KST_PROG_PWM && on_port());
    case A_ENABLE:
        return kst_prog_write_enabled(p)
            || (st == KST_PROG_PROGRAMMING && kst_prog_have_servo(p));
    default:
        return st == KST_PROG_PROGRAMMING && kst_prog_changes(p) != 0u;
    }
}

/* -------------------------------------------------------------------- touch */

static void take(const touch_event_t *evt, int what, int row)
{
    k.have = true;
    k.id = evt->point.id;
    k.pressed = what;
    k.row = row;
    k.held_s = 0.0f;
}

static void let_go(void)
{
    k.have = false;
    k.pressed = P_NONE;
}

static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    if (k.warn.open) {
        if (gfx_rect_contains(kst_panel_apply_rect(), x, y)) {
            take(evt, P_APPLY, -1);
            k.warn.down = true;
            ui_hold_begin(&k.warn.hold);
        } else if (gfx_rect_contains(kst_panel_cancel_rect(), x, y)) {
            take(evt, P_CANCEL, -1);
        }
        return;
    }
    if (k.kp.open) {
        take(evt, P_KEYPAD, -1);
        (void)ui_keypad_event(&k.kp, evt, NULL);
        return;
    }
    if (k.tk.open) {
        take(evt, P_TEXTKEY, -1);
        (void)ui_textkey_event(&k.tk, evt, NULL, 0);
        return;
    }
    if (k.ch.open) {
        for (int i = 0; i < k.ch.count; ++i) {
            if (gfx_rect_contains(kst_panel_choice_rect(i), x, y)) {
                take(evt, P_CHOICE, i);
                return;
            }
        }
        if (gfx_rect_contains(kst_panel_choice_cancel_rect(), x, y)) {
            take(evt, P_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains(tabs_rect(), x, y)) {
        take(evt, P_TAB, -1);
        (void)ui_tabs_event(&k.tabs, evt);
        return;
    }
    if (gfx_rect_contains(kst_panel_close_rect(), x, y)) {
        take(evt, P_CLOSE, -1);
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        const row_t *r = &k_rows[i];

        if (r->page != k.tabs.selected || !row_live(r)) {
            continue;
        }
        if (r->kind == K_STEP) {
            /* The steps act on the press and repeat while it is held. */
            const bool up = gfx_rect_contains(step_rect(r, true), x, y);

            if (up || gfx_rect_contains(step_rect(r, false), x, y)) {
                take(evt, up ? P_PLUS : P_MINUS, i);
                step(r, up);
                return;
            }
        }
        if (gfx_rect_contains(value_rect(r), x, y)) {
            take(evt, P_ROW, i);
            return;
        }
    }
}

void kst_panel_close(void)
{
    if (!kst_panel_holds_port()) {
        close_editors();
        k.open = false;
    } else if (programming() && !k.warn.open) {
        close_editors();
        warn_leave();
    }
    ++k.rev;
}

static void rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = evt->type == TOUCH_EVENT_UP;
    const int was = k.pressed;
    const int which = k.row;

    switch (was) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&k.kp, evt, &v);

        if (up) {
            let_go();
        }
        if (r == UI_KEYPAD_OK) {
            keypad_done(v);
        }
        return;
    }
    case P_TEXTKEY: {
        char name[UI_TEXTKEY_MAX + 1];
        const ui_textkey_result_t r = ui_textkey_event(&k.tk, evt, name,
                                                       sizeof(name));

        if (up) {
            let_go();
        }
        if (r == UI_TEXTKEY_OK) {
            name_done(name);
        }
        return;
    }
    case P_TAB:
        (void)ui_tabs_event(&k.tabs, evt);
        if (up) {
            let_go();
        }
        return;
    case P_APPLY:
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(kst_panel_apply_rect(), x, y)
                && ui_hold_leave(&k.warn.hold)) {
                k.warn.down = false;
                let_go();
            }
            return;
        }
        (void)ui_hold_end(&k.warn.hold);
        k.warn.down = false;
        let_go();
        return;
    case P_PLUS:
    case P_MINUS:
        /* A finger that leaves the key ends the repeat. */
        if (up || !gfx_rect_contains(step_rect(&k_rows[which],
                                               was == P_PLUS), x, y)) {
            let_go();
        }
        return;
    default:
        break;
    }
    if (!up) {
        return;
    }
    let_go();
    switch (was) {
    case P_CLOSE:
        if (gfx_rect_contains(kst_panel_close_rect(), x, y)) {
            kst_panel_close();
        }
        break;
    case P_ROW:
        if (gfx_rect_contains(value_rect(&k_rows[which]), x, y)
            && row_live(&k_rows[which])) {
            if (k_rows[which].kind == K_ACT) {
                act(&k_rows[which]);
            } else {
                edit_field(&k_rows[which]);
            }
        }
        break;
    case P_CHOICE:
        if (k.ch.open && which < k.ch.count
            && gfx_rect_contains(kst_panel_choice_rect(which), x, y)) {
            choice_done(k.ch.values[which]);
        }
        break;
    case P_CHOICE_CANCEL:
        if (gfx_rect_contains(kst_panel_choice_cancel_rect(), x, y)) {
            k.ch.open = false;
        }
        break;
    case P_CANCEL:
        if (gfx_rect_contains(kst_panel_cancel_rect(), x, y)) {
            close_editors();
        }
        break;
    default:
        break;
    }
}

bool kst_panel_has_press(uint8_t id)
{
    return k.have && k.id == id;
}

void kst_panel_event(const touch_event_t *evt)
{
    if (evt == NULL || !k.open) {
        return;
    }
    if (k.have && evt->point.id == k.id && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !k.have) {
        /* One press at a time in the overlay. */
        down(evt);
    }
    ++k.rev;
}

void kst_panel_cancel(void)
{
    ui_keypad_cancel_press(&k.kp);
    ui_textkey_cancel_press(&k.tk);
    ui_tabs_cancel(&k.tabs);
    ui_hold_reset(&k.warn.hold);
    k.warn.down = false;
    let_go();
    ++k.rev;
}

void kst_panel_leave(void)
{
    kst_panel_cancel();
    close_editors();
    k.open = false;
}

void kst_panel_tick(float dt_s)
{
    if (k.warn.open && k.warn.down) {
        if (ui_hold_tick(&k.warn.hold, dt_s)) {
            warn_apply();
            ++k.rev;
        }
        ++k.hold_rev;
    }
    if (k.have && (k.pressed == P_PLUS || k.pressed == P_MINUS)) {
        const float before = k.held_s;
        const float hz = before > REPEAT_DELAY_S + REPEAT_FAST_S
                             ? REPEAT_FAST_HZ : REPEAT_SLOW_HZ;
        int fired_before, fired_now;

        k.held_s += ui_hold_credit(dt_s);
        if (k.held_s < REPEAT_DELAY_S) {
            return;
        }
        fired_before = before < REPEAT_DELAY_S
                           ? 0 : 1 + (int)((before - REPEAT_DELAY_S) * hz);
        fired_now = 1 + (int)((k.held_s - REPEAT_DELAY_S) * hz);
        for (int i = fired_before; i < fired_now && k.pressed != P_NONE; ++i) {
            step(&k_rows[k.row], k.pressed == P_PLUS);
            if (k.warn.open) {
                /* A soft limit: the page takes over and the repeat ends. */
                let_go();
            }
            ++k.rev;
        }
    }
}

/* --------------------------------------------------------------- the caller */

void kst_panel_init(gfx_rect_t area)
{
    memset(&k, 0, sizeof(k));
    kst_prog_init(&k.prog);
    k.area = area;
    ui_tabs_init_text(&k.tabs, k_pages, PG_COUNT, tabs_rect());
    k.kp.pressed = -1;
    k.tk.pressed = -1;
}

/* The options are read where the settings are there: at a screen entry and
 * at an opening, never at kst_panel_init(), which runs before the settings
 * are loaded.  A session keeps the values it began with, except one found
 * running by a panel that has read none yet. */
static void options_fresh(void)
{
    if (!k.loaded || !kst_panel_holds_port()) {
        options_load();
        k.loaded = true;
    }
}

void kst_panel_screen_entry(void)
{
    kst_prog_screen_entry(&k.prog);
    options_fresh();
    ++k.rev;
}

void kst_panel_open(void)
{
    if (!k.open) {
        k.open = true;
        /* The settings may have gone back to their defaults. */
        options_fresh();
        k.card_look = k.card_look || (k.prog.fed && !k.prog.f.card);
        ++k.rev;
    }
}

bool kst_panel_is_open(void)
{
    return k.open;
}

bool kst_panel_holds_port(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);

    return st == KST_PROG_ENTERING || st == KST_PROG_PROGRAMMING
        || st == KST_PROG_WORKING || st == KST_PROG_LEAVING;
}

uint32_t kst_panel_revision(void)
{
    return k.rev + k.kp.revision + k.tk.revision;
}

uint32_t kst_panel_hold_revision(void)
{
    return k.hold_rev;
}

/* Everything of the model a render shows, as one number. */
static uint32_t signature(void)
{
    const kst_prog_t *p = &k.prog;
    const uint32_t part[] = {
        (uint32_t)kst_prog_state(p), (uint32_t)kst_prog_op(p),
        kst_prog_planning(p),
        kst_prog_message(p)->seq, p->f.rd.flags, p->f.rd.frames,
        p->f.rd.port_no, kst_prog_have_servo(p), kst_prog_editable(p),
        (uint32_t)kst_prog_changed(p),
        (uint32_t)(kst_prog_changed(p) >> 32),
        kst_prog_backed_up(p), p->journal, kst_prog_in_doubt(p),
        kst_prog_write_enabled(p), p->f.stopped, p->f.card,
        kst_prog_card_busy(p), kst_prog_files_seq(p),
        (uint32_t)kst_prog_load_skipped(p),
        (uint32_t)(kst_prog_load_skipped(p) >> 32),
        p->f.target, p->f.supply_read, p->f.out_mv, p->f.out_ma,
        kst_prog_supply_mv(p), kst_prog_supply_ma(p), k.hv,
        kst_prog_wait_ms(p),
    };
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < sizeof(part) / sizeof(part[0]); ++i) {
        h = (h ^ part[i]) * 16777619u;
    }
    return h;
}

static void model_moved(void)
{
    const kst_prog_state_t st = kst_prog_state(&k.prog);
    uint32_t sig;

    if (st != k.state_seen) {
        /* An editor asks for something of the state it was opened in. */
        if (st != KST_PROG_PROGRAMMING) {
            close_editors();
            if (k.pressed != P_TAB && k.pressed != P_CLOSE) {
                k.pressed = P_NONE;
            }
        }
        k.state_seen = st;
    }
    card_moved();
    sig = signature();
    if (sig != k.sig) {
        k.sig = sig;
        ++k.rev;
    }
}

void kst_panel_feed(const kst_prog_feed_t *f)
{
    kst_prog_feed(&k.prog, f);
    model_moved();
}

void kst_panel_set_hv(bool on)
{
    if (on == k.hv) {
        return;
    }
    k.hv = on;
    if (!on) {
        if (kst_prog_supply_mv(&k.prog) > KST_PANEL_STD_MV) {
            kst_prog_supply_set(&k.prog, KST_PROG_SUPPLY_MV,
                                kst_prog_supply_ma(&k.prog));
        }
        /* An editor that offers what HV SERVO allowed. */
        if (k.kp.open && k.kp_row == KP_VOLTAGE) {
            k.kp.open = false;
        }
        if (k.warn.open && k.warn.kind == W_HV) {
            k.warn.open = false;
            k.warn.down = false;
            ui_hold_reset(&k.warn.hold);
        }
    }
    model_moved();
}

bool kst_panel_take(kst_prog_cmd_t *out)
{
    return kst_prog_take(&k.prog, out);
}

void kst_panel_bench_armed(bool armed)
{
    kst_prog_bench_armed(&k.prog, armed);
}

bool kst_panel_card_look(void)
{
    const bool look = k.card_look;

    k.card_look = false;
    return look;
}

bool kst_panel_card_take(kst_prog_card_req_t *out)
{
    return kst_prog_card_take(&k.prog, out);
}

void kst_panel_card_done(const kst_prog_card_done_t *done)
{
    kst_prog_card_done(&k.prog, done);
    model_moved();
    ++k.rev;
}

size_t kst_panel_file_take(char *out, size_t n)
{
    return kst_prog_file_take(&k.prog, out, n);
}

void kst_panel_file_done(bool ok)
{
    kst_prog_file_done(&k.prog, ok);
    model_moved();
}

bool kst_panel_file_load(const char *text, size_t len)
{
    const bool ok = kst_prog_file_load(&k.prog, text, len);

    model_moved();
    return ok;
}

bool kst_panel_file_late(const char *text, size_t len)
{
    /* A record the model holds is the one that stands, also after the
     * read that follows the interrupted write: RESTORE offers its values. */
    return !kst_prog_journal_held(&k.prog)
        && kst_panel_file_load(text, len);
}

kst_prog_t *kst_panel_model(void)
{
    return &k.prog;
}

/* --------------------------------------------------------------------- draw */

static gfx_color_t level_color(unsigned level)
{
    return level == KST_PROG_DANGER ? ui_theme_color(UI_C_DANGER)
         : level == KST_PROG_WARN   ? ui_theme_color(UI_C_WARN)
                                    : ui_theme_color(UI_C_TEXT);
}

static void line(gfx_canvas_t *c, int i, const char *s, gfx_color_t color)
{
    gfx_text(c, k.area.x + 10, k.area.y + ROW0 + 5 * PITCH + 6 + i * LINE_H,
             s, &gfx_font_8x16, color, 1);
}

/*
 * What stands until it is dealt with: up to two lines on the SERVO tab.
 * Returns the lines used; @p warn is set for amber, cleared for red.
 */
static int standing(const char *out[2], bool *warn)
{
    const kst_prog_t *p = &k.prog;

    *warn = false;
    if (p->f.stopped) {
        *warn = true;
        out[0] = TR(KP_M_STOPPED);
        return 1;
    }
    if (kst_prog_journal(p)) {
        /* Static: the caller draws the line after this returns. */
        static char second[KST_PANEL_LINE];
        const unsigned ch = (unsigned)kst_prog_journal_port(p) + 1u;

        if (kst_prog_journal_other(p)) {
            out[0] = TR(KP_M_JOURNAL_OTHER);
            snprintf(second, sizeof(second), TR(KP_M_JOURNAL_OTHER_2), ch);
        } else {
            out[0] = TR(KP_M_JOURNAL);
            snprintf(second, sizeof(second), TR(KP_M_JOURNAL_2), ch);
        }
        out[1] = second;
        return 2;
    }
    if (flag(LINK_KS_F_LOCKED)) {
        out[0] = TR(KP_S_LOCKED);
        out[1] = TR(KP_S_LOCKED_2);
        return 2;
    }
    if (kst_prog_in_doubt(p)) {
        out[0] = TR(KP_DOUBT);
        out[1] = TR(KP_M_NEED_READ);
        return 2;
    }
    if (kst_prog_have_servo(p) && !kst_prog_editable(p)) {
        out[0] = TR(KP_M_READ_ONLY);
        out[1] = TR(KP_M_READ_ONLY_2);
        return 2;
    }
    if (p->journal) {
        *warn = true;
        out[0] = TR(KP_JOURNAL_KEPT);
        return 1;
    }
    return 0;
}

/* The last message, on the overlay's last lines.  One that only repeats
 * what stands above it on the SERVO tab is left out. */
static void draw_message(gfx_canvas_t *c)
{
    const kst_prog_msg_t *m = kst_prog_message(&k.prog);
    /* Static: this frame lies on the UI task's deepest path. */
    static char text[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
    const int n = kst_panel_message_lines(m, text);
    const char *st[2];
    bool warn;

    if (n > 0 && k.tabs.selected == PG_SERVO && standing(st, &warn) > 0
        && strcmp(text[0], st[0]) == 0) {
        return;
    }
    for (int i = 0; i < n; ++i) {
        gfx_text(c, k.area.x + 10,
                 k.area.y + k.area.h - 6 - (KST_PANEL_MSG_LINES - i) * LINE_H,
                 text[i], &gfx_font_8x16, level_color(m->level), 1);
    }
}

static void state_line(char *out, size_t n)
{
    const kst_prog_t *p = &k.prog;
    const unsigned ch = (unsigned)kst_prog_port(p) + 1u;

    switch (kst_prog_state(p)) {
    case KST_PROG_DOWN:
        snprintf(out, n, "%s", TR(KP_ST_DOWN));
        break;
    case KST_PROG_NO_PAGE:
        snprintf(out, n, "%s", TR(KP_ST_NO_PAGE));
        break;
    case KST_PROG_SYNC:
        snprintf(out, n, "%s", TR(KP_STATE_SYNC));
        break;
    case KST_PROG_PWM:
        if (!on_port()) {
            snprintf(out, n, "%s", TR(KP_NEED_TARGET));
        } else if (flag(LINK_KS_F_RAIL)) {
            snprintf(out, n, TR(KP_STATE_PWM), (unsigned)port_no() + 1u,
                     (unsigned)(kst_prog_supply_mv(p) / 1000u),
                     (unsigned)(kst_prog_supply_mv(p) % 1000u / 100u),
                     (unsigned)kst_prog_supply_ma(p));
        } else {
            snprintf(out, n, TR(KP_STATE_PWM_AS_IS),
                     (unsigned)port_no() + 1u);
        }
        break;
    case KST_PROG_ENTERING:
        snprintf(out, n, TR(KP_STATE_ENTERING), ch);
        break;
    case KST_PROG_PROGRAMMING:
        if (p->f.supply_read) {
            snprintf(out, n, TR(KP_STATE_PROG_V), ch,
                     (unsigned)(p->f.out_mv / 1000u),
                     (unsigned)(p->f.out_mv % 1000u / 10u),
                     (unsigned)p->f.out_ma);
        } else {
            snprintf(out, n, TR(KP_STATE_PROGRAMMING), ch);
        }
        break;
    case KST_PROG_WORKING:
        if (kst_prog_planning(p)) {
            snprintf(out, n, "%s", TR(KP_STATE_PLANNING));
        } else if (p->f.rd.frames == 1u) {
            snprintf(out, n, "%s", TR(KP_STATE_WORKING_ONE));
        } else {
            snprintf(out, n, TR(KP_STATE_WORKING), (unsigned)p->f.rd.frames);
        }
        break;
    default:
        snprintf(out, n, "%s", TR(KP_STATE_LEAVING));
        break;
    }
}

/* The line under the state: what the state needs, or the servo's type. */
static const char *state_second(bool *type)
{
    const kst_prog_t *p = &k.prog;

    *type = false;
    if (kst_prog_state(p) == KST_PROG_NO_PAGE) {
        return TR(KP_NO_PAGE_2);
    }
    if (kst_prog_state(p) == KST_PROG_PWM && !on_port()) {
        return TR(KP_NEED_TARGET_2);
    }
    if (servo_type() != 0u) {
        *type = true;
        return servo_type() == 6u ? TR(KP_TYPE_V6) : TR(KP_TYPE_V8);
    }
    return NULL;
}

const char *kst_panel_status_line(int i)
{
    bool type;

    if (i == 0) {
        static char buf[KST_PANEL_LINE];

        state_line(buf, sizeof(buf));
        return buf;
    }
    return i == 1 ? state_second(&type) : NULL;
}

/* What stands until it is dealt with, over the last message. */
static void draw_standing(gfx_canvas_t *c)
{
    const char *st[2];
    bool warn;
    const int n = standing(st, &warn);

    for (int i = 0; i < n; ++i) {
        line(c, 2 + i, st[i],
             ui_theme_color(warn ? UI_C_WARN : UI_C_DANGER));
    }
}

static void draw_notes(gfx_canvas_t *c)
{
    const kst_prog_t *p = &k.prog;
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);
    char buf[KST_PANEL_LINE];
    bool type;

    switch (k.tabs.selected) {
    case PG_SERVO:
        state_line(buf, sizeof(buf));
        line(c, 0, buf, ui_theme_color(UI_C_TEXT));
        if (state_second(&type) != NULL) {
            line(c, 1, state_second(&type),
                 type ? ui_theme_color(UI_C_ACCENT) : faint);
        }
        if (kst_prog_backed_up(p)) {
            snprintf(buf, sizeof(buf), TR(KP_BACKUP_FILE),
                     shown(kst_prog_backup_name(p)));
            line(c, 4, buf, faint);
        } else if (kst_prog_no_file(p)) {
            line(c, 4, TR(KP_NO_BACKUP_FILE), ui_theme_color(UI_C_WARN));
        }
        draw_standing(c);
        break;
    case PG_FAILSAFE:
        line(c, -3, TR(KP_FS_NOTE_1), faint);
        line(c, -2, TR(KP_FS_NOTE_2), faint);
        line(c, 0, TR(KP_EDIT_NOTE_1), faint);
        line(c, 1, TR(KP_EDIT_NOTE_2), faint);
        break;
    case PG_LOCKED:
        line(c, 0, TR(KP_LOCKED_NOTE), faint);
        if (kst_prog_have_servo(p)) {
            const unsigned pair = kst_prog_servo(p)->r[KST_REG_NODE_ADDR];

            snprintf(buf, sizeof(buf), TR(KP_PAIRING), pair);
            line(c, 1, buf, ui_theme_color(UI_C_TEXT_DIM));
            if (pair != 0x00u && pair != 0xFFu) {
                snprintf(buf, sizeof(buf), TR(KP_PAIRING_CARD), pair);
                line(c, 2, buf, faint);
            }
        }
        if (kst_prog_load_skipped(p) != 0u) {
            if (ones(kst_prog_load_skipped(p)) == 1u) {
                snprintf(buf, sizeof(buf), "%s", TR(KP_LOCKED_SKIPPED_ONE));
            } else {
                snprintf(buf, sizeof(buf), TR(KP_LOCKED_SKIPPED),
                         ones(kst_prog_load_skipped(p)));
            }
            line(c, 3, buf, ui_theme_color(UI_C_WARN));
        }
        break;
    default:
        line(c, 0, TR(KP_EDIT_NOTE_1), faint);
        line(c, 1, TR(KP_EDIT_NOTE_2), faint);
        break;
    }
}

static void act_value(const row_t *r, char *out, size_t n)
{
    const kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);

    switch (r->what) {
    case A_PORT:
        snprintf(out, n, "%s",
                 st == KST_PROG_ENTERING || st == KST_PROG_WORKING
                     || leave_waits(p)
                     ? TR(KP_ABORT)
                 : st == KST_PROG_PROGRAMMING || st == KST_PROG_LEAVING
                     ? TR(KP_LEAVE) : TR(KP_CONNECT));
        break;
    case A_READ:
        snprintf(out, n, "%s", TR(KP_READ_ALL));
        break;
    case A_FILES:
        snprintf(out, n, "%s", kst_prog_card_busy(p) ? TR(KP_WAIT)
                                                     : TR(KP_CHOOSE));
        break;
    case A_RESTORE:
    case A_MORE:
        snprintf(out, n, "%s", TR(KP_CHOOSE));
        break;
    case A_ENABLE:
        snprintf(out, n, "%s", kst_prog_write_enabled(p) ? TR(ON) : TR(OFF));
        break;
    default:
        write_n(out, n, kst_prog_changes(p));
        break;
    }
}

const char *kst_panel_button(int page, int row, int col)
{
    static char buf[40];
    const row_t *r = row_at(page, row, col);

    if (r == NULL || r->kind != K_ACT) {
        return NULL;
    }
    act_value(r, buf, sizeof(buf));
    return buf;
}

static void draw_row(gfx_canvas_t *c, int i)
{
    const row_t *r = &k_rows[i];
    const kst_prog_t *p = &k.prog;
    const gfx_rect_t rr = row_rect(r);
    const gfx_rect_t vr = value_rect(r);
    const bool live = row_live(r);
    const bool pressed = k.have && k.pressed == P_ROW && k.row == i;
    gfx_color_t fill = ui_theme_color(UI_C_PANEL_SUNK);
    char v[40];

    gfx_text(c, rr.x, rr.y + 9, row_label(r), &gfx_font_8x16,
             live || r->kind == K_LOCKED ? ui_theme_color(UI_C_TEXT_DIM)
                                         : ui_theme_color(UI_C_TEXT_FAINT),
             1);
    if (r->kind == K_ACT) {
        act_value(r, v, sizeof(v));
        if (r->what == A_ENABLE && kst_prog_write_enabled(p)) {
            fill = ui_theme_color(UI_C_DANGER);
        } else if (r->what == A_WRITE && live) {
            fill = ui_theme_color(UI_C_ACCENT);
        }
        ui_button(c, vr, v, fill, pressed, live);
        return;
    }
    if (!kst_prog_have_servo(p)) {
        ui_button(c, vr, "--", fill, false, false);
        return;
    }
    {
        const kst_field_id_t id = (kst_field_id_t)r->what;
        const bool diff = changed(id);
        char num[32];

        const bool skipped = in_set(kst_prog_load_skipped(p), id);

        if (id == KST_F_20K_SEL) {
            snprintf(num, sizeof(num), "V%u", servo_type());
        } else {
            fmt_field(id, kst_field_get(kst_prog_draft(p), id), num,
                      sizeof(num));
        }
        /* A value that differs from the servo's: marked twice, by the fill
         * and by the star.  One the loaded file has otherwise and that is
         * never written: by the fill and the mark of the note below. */
        snprintf(v, sizeof(v), "%s%s", diff ? "* " : skipped ? "! " : "", num);
        if (diff) {
            fill = ui_theme_color(UI_C_ACCENT);
        } else if (skipped) {
            fill = ui_theme_color(UI_C_WARN);
        }
        ui_button(c, vr, v, fill, pressed, live);
        if (r->kind != K_STEP) {
            return;
        }
        ui_button(c, step_rect(r, false), "-", ui_theme_color(UI_C_PANEL_HI),
                  k.have && k.pressed == P_MINUS && k.row == i, live);
        ui_button(c, step_rect(r, true), "+", ui_theme_color(UI_C_PANEL_HI),
                  k.have && k.pressed == P_PLUS && k.row == i, live);
    }
}

static void draw_page(gfx_canvas_t *c)
{
    ui_tabs_render(&k.tabs, c);
    ui_button(c, kst_panel_close_rect(), TR(SUP_CLOSE),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CLOSE,
              !kst_panel_holds_port() || programming());
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].page == k.tabs.selected) {
            draw_row(c, i);
        }
    }
    draw_notes(c);
    draw_message(c);
}

static void draw_choice(gfx_canvas_t *c)
{
    const row_t *r = &k_rows[k.kp_row >= 0 ? k.kp_row : 0];

    gfx_text(c, k.area.x + 10, k.area.y + 16, k.ch.title, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    for (int i = 0; i < k.ch.count; ++i) {
        const bool current = k.ch.kind == C_STRETCH
            && k.ch.values[i]
               == (int)kst_field_get(kst_prog_draft(&k.prog),
                                     (kst_field_id_t)r->what);

        ui_button(c, kst_panel_choice_rect(i), k.ch.labels[i],
                  current ? ui_theme_color(UI_C_ACCENT)
                          : ui_theme_color(UI_C_PANEL_SUNK),
                  k.have && k.pressed == P_CHOICE && k.row == i, true);
    }
    ui_button(c, kst_panel_choice_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CHOICE_CANCEL, true);
}

static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = k.area;
    const gfx_color_t edge = k.warn.danger ? ui_theme_color(UI_C_DANGER)
                                           : ui_theme_color(UI_C_WARN);

    gfx_draw_rect(c, a.x, a.y, a.w, a.h, edge);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, edge);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, edge);
    gfx_text(c, a.x + 12, a.y + 20, k.warn.title, &gfx_font_8x16, edge, 2);
    for (int i = 0; i < k.warn.count; ++i) {
        gfx_text(c, a.x + 12, a.y + 72 + i * 24, k.warn.lines[i],
                 &gfx_font_8x16, level_color(k.warn.level[i]), 1);
    }
    ui_button(c, kst_panel_apply_rect(), k.warn.apply,
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK), edge,
                           k.warn.hold.held_s),
              k.warn.down, true);
    ui_button(c, kst_panel_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              k.have && k.pressed == P_CANCEL, true);
}

void kst_panel_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = k.area;

    if (k.kp.open) {
        ui_keypad_render(&k.kp, c);
        return;
    }
    if (k.tk.open) {
        ui_textkey_render(&k.tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (k.warn.open) {
        draw_warning(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (k.ch.open) {
        draw_choice(c);
        return;
    }
    draw_page(c);
}
