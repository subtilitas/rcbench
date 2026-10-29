/**
 * @file wa_api_rig.h
 * @brief The rig of the file API's suites (wa_api.h): a card in memory
 *        (fm_fake.h) behind a runner whose bench and CANCEL the case sets,
 *        and the API's JSON caught in a string.  Included once per suite.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_WA_API_RIG_H
#define RCBENCH_WA_API_RIG_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fm_fake.h"
#include "fm_ops.h"
#include "fm_req.h"
#include "wa_api.h"

static fk_card_t g_card;
static fm_io_t g_io;
static fm_req_t g_req;
static uint8_t g_chunk[FM_CHUNK];
static wa_api_t g_api;
static fm_runner_t g_run;
static bool g_armed;            /* bench(): ARMED                      */
static int g_benches;           /* bench() calls                       */
static int g_cancel_after;      /* CANCEL from this bench() call on; -1 */
static log_marks_t g_marks;     /* the logger's open files             */

/** @brief The JSON an answer wrote. */
static char *g_json;
/** @brief Its bytes. */
static size_t g_json_len;
/** @brief Room at g_json. */
static size_t g_json_cap;

/**
 * @brief bench(): ARMED as the case sets it, the link up, g_marks open.
 * @param out  takes the bench
 * @param ctx  unused
 */
static inline void rig_bench(fm_bench_t *out, void *ctx)
{
    (void)ctx;
    ++g_benches;
    out->armed = g_armed;
    out->link_up = true;
    out->marks = &g_marks;
}

/**
 * @brief cancelled(): from g_cancel_after bench() calls on.
 * @param ctx  unused
 * @return true to stop
 */
static inline bool rig_cancelled(void *ctx)
{
    (void)ctx;
    return g_cancel_after >= 0 && g_benches > g_cancel_after;
}

/**
 * @brief The output's sink: everything appended to g_json.
 * @param ctx   unused
 * @param data  the bytes
 * @param n     how many
 * @return true
 */
static inline bool rig_sink(void *ctx, const void *data, size_t n)
{
    (void)ctx;
    if (g_json_len + n + 1u > g_json_cap) {
        g_json_cap = (g_json_len + n + 1u) * 2u;
        REQUIRE_REALLOC(g_json, g_json_cap);
    }
    memcpy(g_json + g_json_len, data, n);
    g_json_len += n;
    g_json[g_json_len] = '\0';
    return true;
}

/** @brief The output the JSON writers write into. */
static wa_out_t g_out;

/** @brief Start catching an answer's JSON. */
static inline void json_begin(void)
{
    g_json_len = 0u;
    if (g_json != NULL) {
        g_json[0] = '\0';
    }
    wa_out_init(&g_out, rig_sink, NULL);
}

/**
 * @brief The JSON caught since json_begin().
 * @return the text
 */
static inline const char *json_end(void)
{
    CHECK(wa_out_flush(&g_out));
    return (g_json != NULL) ? g_json : "";
}

/** @brief A fresh, empty card in the slot, SAFE, the API on it. */
static inline void rig_fresh(void)
{
    fk_free(&g_card);
    fk_init(&g_card);
    g_io = fk_io(&g_card);
    memset(&g_marks, 0, sizeof(g_marks));
    g_armed = false;
    g_benches = 0;
    g_cancel_after = -1;
    const fm_runner_t r = {
        .io = &g_io, .bench = rig_bench, .cancelled = rig_cancelled,
        .progress = NULL, .look = NULL, .chunk = g_chunk,
        .size = sizeof(g_chunk), .ctx = NULL,
    };
    g_run = r;
    wa_api_init(&g_api, &g_run, &g_req);
}

/**
 * @brief Whether @p hay holds @p needle.
 * @param hay     the text
 * @param needle  what to find
 * @return true when found
 */
static inline bool has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

#endif /* RCBENCH_WA_API_RIG_H */
