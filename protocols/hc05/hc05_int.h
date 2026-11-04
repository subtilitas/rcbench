/**
 * @file hc05_int.h
 * @brief What the two source files of the HC-05 module share: the fields
 *        of an +INQ line and the copy of a text the module sent.  Not for
 *        callers of the module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05_INT_H
#define HC05_INT_H

#include <stdbool.h>
#include <stddef.h>

#include "hc05.h"

/**
 * @brief Read the value of an +INQ line, `addr,class[,rssi[,...]]`.
 *
 * The address is 3 hex parts of 1 to 4, 1 to 2 and 1 to 6 digits joined by
 * colons; the class 1 to 6 hex digits, of which @p dev keeps the low 16
 * bits; the RSSI (received signal strength indication) 1 to 4 hex digits,
 * a 16-bit two's complement word, INT16_MAX when the line has none.
 * Fields after the third are not read.
 *
 * @param value The text after `+INQ:`.
 * @param dev   Receives the device, name ""; unchanged on false.
 * @return false for a value of another form.
 */
bool hc05_inq_fields(const char *value, hc05_dev_t *dev);

/**
 * @brief Copy a text the module sent into @p dst, cut to @p cap - 1 bytes,
 *        every byte outside printable ASCII (0x20 to 0x7E) as `?`.
 * @param dst The buffer; NUL-terminated.
 * @param cap Its size, at least 1.
 * @param src The text.
 */
void hc05_copy_text(char *dst, size_t cap, const char *src);

#endif
