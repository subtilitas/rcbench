/*
 * A test harness of about 100 lines: asserts, named cases, a crash handler
 * and a summary.  No framework is vendored.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_tests;
static int t_failures;
static int t_case_failed;
static const char *t_case_name = "";

/* A crash prints no "ok" line.  The handler names the case that died, so it
 * is not identified by its position in the log. */
static void t_crash(int sig)
{
    fprintf(stderr, "\nCRASH %s (signal %d)\n", t_case_name, sig);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void t_install_crash_handler(void)
{
    signal(SIGSEGV, t_crash);
    signal(SIGABRT, t_crash);
    signal(SIGFPE, t_crash);
    signal(SIGILL, t_crash);
}

#define TEST_CASE(name)                                                       \
    static void name(void);                                                   \
    static void run_##name(void)                                              \
    {                                                                         \
        t_install_crash_handler();                                            \
        t_case_name = #name;                                                  \
        t_case_failed = 0;                                                    \
        t_tests++;                                                            \
        name();                                                               \
        if (t_case_failed) {                                                  \
            t_failures++;                                                     \
            printf("FAIL  %s\n", #name);                                      \
        } else {                                                              \
            printf("ok    %s\n", #name);                                      \
        }                                                                     \
        fflush(stdout); /* a crashing case must still name itself */          \
    }                                                                         \
    static void name(void)

#define RUN(name) run_##name()

#define T_FAIL(fmt, ...)                                                      \
    do {                                                                      \
        t_case_failed = 1;                                                    \
        printf("      %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long _a = (long)(a);                                                  \
        long _b = (long)(b);                                                  \
        if (_a != _b) {                                                       \
            T_FAIL("%s == %s: got %ld, want %ld", #a, #b, _a, _b);            \
        }                                                                     \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                    \
    do {                                                                      \
        const char *_a = (a);                                                 \
        const char *_b = (b);                                                 \
        if (strcmp(_a, _b) != 0) {                                            \
            T_FAIL("%s: got \"%s\", want \"%s\"", #a, _a, _b);                \
        }                                                                     \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                 \
    do {                                                                      \
        double _a = (double)(a);                                              \
        double _b = (double)(b);                                              \
        double _d = _a - _b;                                                  \
        if (_d < 0) {                                                         \
            _d = -_d;                                                         \
        }                                                                     \
        if (!(_d <= (double)(tol))) {                                         \
            T_FAIL("%s == %s: got %g, want %g", #a, #b, _a, _b);               \
        }                                                                     \
    } while (0)

static int test_summary(const char *suite)
{
    printf("\n%s: %d case(s), %d failure(s)\n", suite, t_tests, t_failures);
    return t_failures == 0 ? 0 : 1;
}
