/*
 * Shared by the log parsing suites: test_log_numbers.c, test_log_reader.c and
 * test_log_csv.c.  Only static inline helpers and the buffer that
 * load_fixture() fills.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "log_numbers.h"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "fixtures"
#endif

static inline void check_split(const char *raw, const char *digits, int sign,
                               int exp, const char *unit)
{
    log_value_t v;
    if (!log_split_value(raw, &v)) {
        T_FAIL("split(\"%s\") failed", raw);
        return;
    }
    if (strcmp(v.digits, digits) != 0 || v.sign != sign || v.exp != exp ||
        strcmp(v.unit, unit) != 0) {
        T_FAIL("split(\"%s\") -> {%s, %d, %d, \"%s\"}, want {%s, %d, %d, \"%s\"}",
               raw, v.digits, v.sign, v.exp, v.unit, digits, sign, exp, unit);
    }
}

static char g_text[8192];
static size_t g_len;
static log_source_t g_src;
static log_mem_ctx_t g_ctx;

static inline bool load_fixture(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        T_FAIL("cannot open %s", path);
        return false;
    }
    g_len = fread(g_text, 1, sizeof(g_text) - 1, f);
    fclose(f);
    g_text[g_len] = '\0';
    log_source_memory(&g_src, &g_ctx, g_text, g_len);
    return true;
}
