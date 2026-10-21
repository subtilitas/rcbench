/*
 * What storage_list.c needs of storage.c: the drive of the mounted
 * volume, and the end of a read made under a hold.  Not for callers
 * outside the component.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

/** The FatFs drive of the mounted volume, "0:", or "" with none.  Read
 *  under a hold (storage_hold()), which keeps it from changing. */
const char *storage_drive(void);

/**
 * The end of a read made under storage_hold(): the hold given back, and
 * the read's FatFs answer @p fr (0 for one that worked) weighed.  A
 * failure that says the card is gone lets the volume go (storage_walk()
 * says how); @p how names the read in the log line.  With @p may_mount,
 * a volume let go with nothing else holding it is mounted again once.
 *
 * @return whether a card in the slot was mounted again by this call.
 */
bool storage_read_end(int fr, const char *how, bool may_mount);
