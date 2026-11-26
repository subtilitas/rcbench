/**
 * @file prn_fixture.h
 * @brief The printer files of record for the engine's suites: read from
 *        protocols/printers/ (the suites run in test/host) and parsed.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRN_FIXTURE_H
#define PRN_FIXTURE_H

#include <stdio.h>
#include <string.h>

#include "prn_spec.h"

/** @brief The T50M Pro file, from the suite's directory. */
#define PRN_T50M_PATH "../../protocols/printers/supvan-t50m-pro.prn"

/**
 * @brief Read a file.
 * @param path its path
 * @param buf  the buffer
 * @param cap  its size
 * @return the bytes read, 0 when the file cannot be read
 */
static inline size_t prn_read(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0u;
    }
    const size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

/**
 * @brief Read and parse the T50M Pro file.
 * @param s   the spec
 * @param txt PRN_FILE_MAX + 1 bytes for the text, or NULL
 * @param n   set to the text's length, or NULL
 * @return true when it parsed
 */
static inline bool prn_load_t50m(prn_spec_t *s, char *txt, size_t *n)
{
    static char buf[PRN_FILE_MAX + 1u];
    char *b = (txt != NULL) ? txt : buf;
    const size_t len = prn_read(PRN_T50M_PATH, b, PRN_FILE_MAX + 1u);
    if (n != NULL) {
        *n = len;
    }
    prn_perr_t err;
    const bool ok = prn_parse(b, len, "supvan-t50m-pro.prn", s, &err);
    if (!ok) {
        printf("      %s: E%02u line %u\n", PRN_T50M_PATH, err.code,
               err.line);
    }
    return ok;
}

#endif /* PRN_FIXTURE_H */
