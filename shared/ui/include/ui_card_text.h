/*
 * What a screen says of the SD (Secure Digital) card slot when no volume is
 * mounted, by what the slot holds (card_state.h) and where it is said: no
 * card, a card neither FAT32 nor exFAT, or a card that cannot be read.
 *
 * One table for every place, so each of them tells the three apart the
 * same way.  Implemented in ui_text.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "card_state.h"
#include "ui_text.h"

typedef enum {
    UI_CARD_AT_LOGS = 0,     /**< LOGS' empty list                        */
    UI_CARD_AT_SPLASH,       /**< the start-up line's SD card step         */
    UI_CARD_AT_RUN,          /**< the band, a run not recorded             */
    UI_CARD_AT_TEST,         /**< the band, a servo test not recorded      */
    UI_CARD_AT_KP_WRITE,     /**< KST programmer, a write's card note      */
    UI_CARD_AT_KP_NO_FILE,   /**< KST programmer, no backup file to come   */
    UI_CARD_AT_KP_NO_BACKUP, /**< KST programmer, no backup file was saved */
    UI_CARD_AT_KP_MSG,       /**< KST programmer, nothing saved or written */
    UI_CARD_AT_COUNT
} ui_card_at_t;

/**
 * The text ID for @p state at @p at.  CARD_STATE_MOUNTED and a state out
 * of range take the place's no-card text: a caller asks when it found no
 * volume, and a mount since then changes nothing said.  A place out of
 * range takes LOGS' no-card text.
 */
ui_text_id_t ui_card_text_id(card_state_t state, ui_card_at_t at);

/** ui_card_text_id() in the language showing. */
const char *ui_card_text(card_state_t state, ui_card_at_t at);
