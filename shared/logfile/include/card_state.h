/**
 * @file
 * @brief State of the SD card slot as the last mount found it.
 *
 * What the panel's SD (Secure Digital) card slot holds, as the last mount
 * found it: no card, a mounted volume, a card with no FAT (File Allocation
 * Table) volume on it, or a card that does not mount for another reason.
 *
 * The panel's storage component sets it; the screens, the alerts and the
 * start-up line read it to say which of the four it is (ui_card_text.h).
 * The values are stable: the programmer's feed carries one as a uint8_t.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** @brief Mount state of the SD card. */
typedef enum {
    /** No card answers in the slot, or no mount was tried yet.  Zero, so
     *  a zeroed feed reads as no card. */
    CARD_STATE_NONE = 0,
    /** @brief A volume is mounted. */
    CARD_STATE_MOUNTED = 1,
    /** A card answers, and FatFs finds no FAT12, FAT16, FAT32 or exFAT
     *  volume on it (FR_NO_FILESYSTEM, 13): NTFS, a GPT or no format at
     *  all. */
    CARD_STATE_NO_FAT = 2,
    /** A card answers, or the slot's bus did not come up, and the volume
     *  does not mount for another reason: a read error, a card that does
     *  not finish its start-up, no memory for the mount. */
    CARD_STATE_UNREADABLE = 3,
} card_state_t;
