/**
 * @file render_versions.h
 * @brief The versions of the update screens' examples.
 *
 * Fixed rather than read from rcbench_version.h: a panel and an IOmcu on
 * 0.22.0, the patch release 0.22.1 offered and installed, 0.21.2 as the
 * older one.  The screenshots and the alerts quoted in Screens.md then tell
 * one sequence at every release.  A video sets RCBENCH_VIDEO_VERSIONS to
 * "running,newer,older", three versions of the form X.Y.Z, and its views
 * show those in the same places; the start-up check and a wireless device
 * that is behind then show the running and the older one.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RENDER_VERSIONS_H
#define RENDER_VERSIONS_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Which example version. */
typedef enum { EX_VER_RUNNING, EX_VER_NEWER, EX_VER_OLDER } ex_ver_t;

/** @brief One example version as numbers and as text. */
typedef struct {
    unsigned n[3];
    char text[24];
} ex_version_t;

/**
 * @brief Whether a video set the example versions.
 * @return true with RCBENCH_VIDEO_VERSIONS holding three versions
 */
static inline bool ex_versions_set(void)
{
    const char *s = getenv("RCBENCH_VIDEO_VERSIONS");
    unsigned v[9];
    return s != NULL && sscanf(s, "%u.%u.%u,%u.%u.%u,%u.%u.%u", &v[0], &v[1],
                               &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                               &v[8]) == 9;
}

/**
 * @brief An example version.
 * @param which the running, the newer or the older one
 * @return its numbers and its text
 */
static inline ex_version_t ex_version(ex_ver_t which)
{
    unsigned v[9] = { 0u, 22u, 0u, 0u, 22u, 1u, 0u, 21u, 2u };
    const char *s = getenv("RCBENCH_VIDEO_VERSIONS");
    unsigned r[9];
    if (s != NULL && sscanf(s, "%u.%u.%u,%u.%u.%u,%u.%u.%u", &r[0], &r[1],
                            &r[2], &r[3], &r[4], &r[5], &r[6], &r[7],
                            &r[8]) == 9) {
        for (int i = 0; i < 9; ++i) {
            v[i] = r[i] % 1000u;
        }
    }
    ex_version_t out;
    for (int i = 0; i < 3; ++i) {
        out.n[i] = v[(int)which * 3 + i];
    }
    snprintf(out.text, sizeof(out.text), "%u.%u.%u", out.n[0], out.n[1],
             out.n[2]);
    return out;
}

#endif /* RENDER_VERSIONS_H */
