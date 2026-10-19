/*
 * The folders of the panel's SD (Secure Digital) card, and the files in them
 * with a fixed name.  Every path the panel spells on the card is here.
 *
 *   /BENCH/                    what the bench writes or reads for itself
 *     LOGS/                    automatic logs: SERVOnnn, MOTORnnn, SUPPLnnn
 *     ESC/CONF/                ESC (electronic speed controller) profiles
 *     KST/CONF/                KST and Chaservo servo files
 *     HITEC/CONF/              Hitec servo files
 *     RCBENCH.LOG              the fault log
 *   /USER/
 *     LOGS/                    user-started tests: ATESTnnn, LIMITnnn
 *   /                          run logs of firmware 0.21 and older
 *
 * Every folder name is upper case, 1 to 8 characters of A-Z, 0-9, "_" and
 * "-", without an extension: a FAT (File Allocation Table) 8.3 short name,
 * the same on every computer.  A path is relative to the card's root, its
 * folders separated by "/", with no "/" at either end; "" is the root.
 *
 * The run logs are written to CARD_AUTO_LOGS and CARD_USER_LOGS.  The ESC
 * profiles, the servo files and the fault log are read and written in the
 * root's ESC and KST folders and the root's RCBENCH.LOG; CARD_ESC_CONF,
 * CARD_KST_CONF, CARD_HITEC_CONF and CARD_FAULT_LOG are the places they
 * move to, and nothing reads them yet.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** The card's root. */
#define CARD_ROOT        ""

/** The bench's own folder. */
#define CARD_BENCH       "BENCH"

/** The automatic logs: a log of each ARMED run and each supply run. */
#define CARD_AUTO_LOGS   "BENCH/LOGS"

/** The user's folder. */
#define CARD_USER        "USER"

/** The files of the tests a user starts: AUTOMATIC TEST and LIMIT RUN. */
#define CARD_USER_LOGS   "USER/LOGS"

/** ESC profile overrides, <id>.json. */
#define CARD_ESC_CONF    "BENCH/ESC/CONF"

/** KST and Chaservo servo files, their backups and the write record. */
#define CARD_KST_CONF    "BENCH/KST/CONF"

/** The Hitec programmer's files. */
#define CARD_HITEC_CONF  "BENCH/HITEC/CONF"

/** The fault log: a line every 5 s while the link to the IOmcu is down. */
#define CARD_FAULT_LOG   "BENCH/RCBENCH.LOG"

/**
 * Room for a folder path and its terminator: two folders of 8 characters
 * and the "/" between them are 17, and the room is the settings store's
 * string, SETTINGS_TEXT_MAX (24).
 */
#define CARD_DIR_MAX     24

#ifdef __cplusplus
}
#endif
