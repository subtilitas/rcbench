/*
 * Which of a card's entries a bounded list keeps, and the order it draws them
 * in.
 *
 * The browse list holds LOG_VIEWER_MAX_FILES entries and a card takes up to
 * LOG_RUN_LAST runs, so a listing of a full card is a choice.  Making that
 * choice by where the directory read stopped hides every run written after
 * the card passed that many files -- usually the newest experiments -- and
 * they stay hidden, because the screen has no second page to reach them from.
 *
 * Entries are offered one at a time, in whatever order the volume hands them
 * over, and the set holds the LOG_VIEWER_MAX_FILES that log_name_rank() puts
 * first.  Ordering for display is a separate pass, run once the directory has
 * been read to its end.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "log_viewer_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Offer @p cand to a set that holds @p count entries of at most @p capacity,
 * in log_name_rank() order, and return the count after the offer.
 *
 * The count is the same one that came back from the last call, so a set never
 * drifts past its capacity.  A candidate that ranks below a full set is
 * dropped and the count does not move.
 */
int log_select_keep(log_viewer_file_t *set, int count, int capacity,
                    const log_viewer_file_t *cand);

/**
 * Put @p count entries into the order the browse list draws them in:
 * directories first, then names, case-insensitively.
 */
void log_select_sort(log_viewer_file_t *set, int count);

#ifdef __cplusplus
}
#endif
