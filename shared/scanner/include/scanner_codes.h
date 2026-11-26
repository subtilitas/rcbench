/**
 * @file
 * @brief The QR scanner's setup codes as stored module matrices, drawn by
 *        SETUP > SENSORS > SCANNER's SHOW SETUP CODE (scanner_pane.h).
 *
 * One code: the GM861 manual's "Serial Output" (V1.2.4, page 15), which
 * turns a module that sends as a USB keyboard back to its UART.  A matrix
 * is SCANNER_CODE_MODULES rows; bit SCANNER_CODE_MODULES - 1 of a row is
 * its left module, a set bit a dark module.  tools/gen_scanner_codes.py
 * writes scanner_codes.c and checks the matrix as a QR code: the finder and
 * timing patterns, the format bits and the Reed-Solomon check.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Modules a side: a QR code of version 1. */
#define SCANNER_CODE_MODULES 21u

/** @brief "Serial Output", one row a word. */
extern const uint32_t scanner_code_serial[SCANNER_CODE_MODULES];

/**
 * @brief Whether a module is dark.
 * @param code The matrix; NULL reads light.
 * @param x    The column, 0 left.
 * @param y    The row, 0 top.
 * @return true for a dark module; false outside the matrix.
 */
static inline bool scanner_code_dark(const uint32_t *code, int x, int y)
{
    if (code == NULL || x < 0 || y < 0 || x >= (int)SCANNER_CODE_MODULES
        || y >= (int)SCANNER_CODE_MODULES) {
        return false;
    }
    return ((code[y] >> (SCANNER_CODE_MODULES - 1u - (unsigned)x)) & 1u)
           != 0u;
}

#ifdef __cplusplus
}
#endif
