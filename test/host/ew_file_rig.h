/*
 * What the suites of the settings file (protocols/escwire/ew_file.c) share: a
 * session on the simulated ESC, and its page as a file and a text.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_FILE_RIG_H
#define EW_FILE_RIG_H

#include <stdio.h>
#include <string.h>

#include "greatest.h"

/* Each suite uses a part of the simulation and of this rig. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "ew_file.h"
#include "ew_sim.h"

#define ED_HEAD "ESC settings backup, format 1\n"

static sim_t esc;
static ew_session_t s;
static ew_file_t g_f;
static ew_file_t g_r;
static char g_text[EW_FILE_MAX + 64u];
static char g_alt[EW_FILE_MAX + 64u];
static size_t g_len;

static void attach(void)
{
    const ew_driver_t d = sim_driver(&esc);

    (void)ew_session_init(&s, &d);
}

static void entered_and_read(void)
{
    CHECK_EQ(sim_enter(&s, &esc), EW_SES_OK);
    CHECK_EQ(ew_session_read(&s), EW_SES_BUSY);
    CHECK_EQ(sim_run(&s, &esc), EW_SES_OK);
}

/* The file of what the session read, as text in g_text. */
static void written(void)
{
    CHECK(ew_file_capture(&g_f, &s.contact, &s.id, s.orig));
    g_len = ew_file_format(&g_f, g_text, sizeof(g_text));
    CHECK(g_len > 0u);
}

static void up_s(void)
{
    sim_init_efm8(&esc, 0xB2u, false);
    attach();
    entered_and_read();
    written();
}

static void up_bj(void)
{
    sim_init_efm8(&esc, 0xB5u, true);
    attach();
    entered_and_read();
    written();
}

static void up_am(uint8_t rev, uint8_t protocol)
{
    sim_init_am(&esc, rev, protocol);
    attach();
    entered_and_read();
    written();
}

static ew_file_err_t parse_len(size_t len)
{
    unsigned line = 99u;

    return ew_file_parse(g_text, len, &g_r, &line);
}

static ew_file_err_t parse(void)
{
    return parse_len(strlen(g_text));
}

/* Put a right checksum line in place of the one the text ends with. */
static void resign(void)
{
    char *at = strstr(g_text, "checksum ");
    size_t head;
    uint32_t crc;

    CHECK(at != NULL);
    head = (size_t)(at - g_text);
    crc = ew_file_crc32(g_text, head);
    snprintf(at, sizeof(g_text) - head, "checksum %08X\n", (unsigned)crc);
    g_len = strlen(g_text);
}

/* Replace the first @p from in g_text. */
static void swap_text(const char *from, const char *to)
{
    const char *at;
    size_t head;

    g_text[g_len] = '\0';
    at = strstr(g_text, from);
    CHECK(at != NULL);
    head = (size_t)(at - g_text);
    snprintf(g_alt, sizeof(g_alt), "%.*s%s%s", (int)head, g_text, to,
             at + strlen(from));
    strcpy(g_text, g_alt);
    g_len = strlen(g_text);
}

/* The text with its checksum line turned into the word for an edited file. */
static void as_edited(void)
{
    char *at;

    g_text[g_len] = '\0';
    at = strstr(g_text, "checksum ");
    CHECK(at != NULL);
    strcpy(at, "edited\n");
    g_len = strlen(g_text);
}

/* A short edited file of AM32 revision 4 with @p body in it. */
static ew_file_err_t edited_body(const char *body, unsigned *line)
{
    int n = snprintf(g_text, sizeof(g_text),
                     "ESC settings backup, format 1\n"
                     "Firmware: AM32\nLayout: 4\n%sedited\n", body);

    return ew_file_parse(g_text, (size_t)n, &g_r, line);
}

#endif /* EW_FILE_RIG_H */
