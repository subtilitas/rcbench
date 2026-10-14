/*
 * The panel's name on the home network, rcbench-XXXX.local.
 *
 * The failures under test: an answer for a name that is not the panel's
 * (it would claim another device's name), no answer for the panel's own
 * name in another case of letters, a malformed query read past its end,
 * and a one-shot query answered in a form the asker drops.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "wa_mdns.h"

static const uint8_t k_ip[4] = { 192, 168, 1, 23 };
static uint8_t g_out[WA_MDNS_MAX];

/* A query for @p name (labels separated by '.') of @p type and @p cls. */
static size_t query(uint8_t *q, uint16_t id, const char *name, uint16_t type,
                    uint16_t cls)
{
    memset(q, 0, 12u);
    q[0] = (uint8_t)(id >> 8);
    q[1] = (uint8_t)id;
    q[5] = 1u;
    size_t at = 12u;
    const char *p = name;
    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        const size_t len = (dot != NULL) ? (size_t)(dot - p) : strlen(p);
        q[at++] = (uint8_t)len;
        memcpy(q + at, p, len);
        at += len;
        p += len + ((dot != NULL) ? 1u : 0u);
    }
    q[at++] = 0u;
    q[at++] = (uint8_t)(type >> 8);
    q[at++] = (uint8_t)type;
    q[at++] = (uint8_t)(cls >> 8);
    q[at++] = (uint8_t)cls;
    return at;
}

TEST_CASE(the_panels_name_is_answered_with_its_address)
{
    uint8_t q[128];
    size_t n = query(q, 0x1234u, "rcbench-1A2B.local", 1u, 1u);
    size_t r = wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                             sizeof(g_out));
    CHECK(r > 0u);
    CHECK_EQ(g_out[0], 0u);               /* ID 0 to the group */
    CHECK_EQ(g_out[1], 0u);
    CHECK_EQ(g_out[2], 0x84u);            /* response, authoritative */
    CHECK_EQ(g_out[5], 0u);               /* no question */
    CHECK_EQ(g_out[7], 1u);               /* one answer */
    CHECK_EQ(memcmp(g_out + 12, "\x0crcbench-1A2B\x05local\x00", 20u), 0);
    CHECK_EQ(g_out[34], 0x80u);           /* cache flush, class IN */
    CHECK_EQ(g_out[35], 1u);
    CHECK_EQ(g_out[39], 120u);            /* TTL */
    CHECK_EQ(memcmp(g_out + r - 4u, k_ip, 4u), 0);
    CHECK_EQ(r, 12u + 20u + 10u + 4u);
    /* Another case of letters, a QU question, type ANY. */
    n = query(q, 0u, "RCBENCH-1a2b.LOCAL", 255u, 0x8001u);
    CHECK(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                        sizeof(g_out)) > 0u);
    /* The announcement is the same answer, asked by no one. */
    CHECK_EQ(wa_mdns_announce("rcbench-1A2B", k_ip, g_out, sizeof(g_out)),
             r);
}

TEST_CASE(a_one_shot_query_gets_its_id_and_question_back)
{
    uint8_t q[128];
    const size_t n = query(q, 0xBEEFu, "rcbench-1A2B.local", 1u, 1u);
    const size_t r = wa_mdns_reply(q, n, 54321u, "rcbench-1A2B", k_ip, g_out,
                                   sizeof(g_out));
    CHECK_EQ(r, 12u + 24u + 34u);
    CHECK_EQ(g_out[0], 0xBEu);
    CHECK_EQ(g_out[1], 0xEFu);
    CHECK_EQ(g_out[5], 1u);
    CHECK_EQ(g_out[7], 1u);
    CHECK_EQ(memcmp(g_out + 12, g_out + 36, 20u), 0);   /* the same name */
    CHECK_EQ(g_out[58], 0x00u);           /* class IN, no flush */
    CHECK_EQ(g_out[59], 0x01u);
    CHECK_EQ(g_out[63], 10u);             /* TTL 10 s */
}

TEST_CASE(other_names_types_and_responses_are_not_answered)
{
    uint8_t q[128];
    size_t n = query(q, 0u, "rcbench-1A2C.local", 1u, 1u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2B.local.x", 1u, 1u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2B", 1u, 1u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2.local", 1u, 1u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2B.local", 28u, 1u);   /* AAAA */
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2B.local", 1u, 3u);    /* class CH */
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    n = query(q, 0u, "rcbench-1A2B.local", 1u, 1u);
    q[2] = 0x84u;                                      /* a response */
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    /* Too little room for the answer. */
    n = query(q, 0u, "rcbench-1A2B.local", 1u, 1u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out, 45u),
             0u);
    CHECK_EQ(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out, 46u),
             46u);
}

TEST_CASE(a_second_question_and_compressed_names_are_followed)
{
    /* Question 1: printer.local A; question 2: a pointer to "local" in
     * question 1 behind the label "rcbench-1A2B". */
    uint8_t q[128];
    size_t n = query(q, 0u, "printer.local", 1u, 1u);
    q[5] = 2u;
    const size_t local_at = 12u + 8u;    /* "\x07printer" then "\x05local" */
    q[n++] = 12u;
    memcpy(q + n, "rcbench-1A2B", 12u);
    n += 12u;
    q[n++] = (uint8_t)(0xC0u | (local_at >> 8));
    q[n++] = (uint8_t)local_at;
    q[n++] = 0u;
    q[n++] = 1u;
    q[n++] = 0u;
    q[n++] = 1u;
    CHECK(wa_mdns_reply(q, n, 5353u, "rcbench-1A2B", k_ip, g_out,
                        sizeof(g_out)) > 0u);
}

TEST_CASE(malformed_queries_are_dropped_without_reading_past_them)
{
    uint8_t q[128];
    const size_t n = query(q, 0u, "rcbench-1A2B.local", 1u, 1u);
    /* Every length short of the whole. */
    for (size_t k = 0; k < n; ++k) {
        CHECK_EQ(wa_mdns_reply(q, k, 5353u, "rcbench-1A2B", k_ip, g_out,
                               sizeof(g_out)), 0u);
    }
    /* A pointer to itself, a pointer past the end, a reserved label. */
    uint8_t loop[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 12, 0, 1,
                       0, 1 };
    CHECK_EQ(wa_mdns_reply(loop, sizeof(loop), 5353u, "rcbench-1A2B", k_ip,
                           g_out, sizeof(g_out)), 0u);
    uint8_t past[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 200, 0, 1,
                       0, 1 };
    CHECK_EQ(wa_mdns_reply(past, sizeof(past), 5353u, "rcbench-1A2B", k_ip,
                           g_out, sizeof(g_out)), 0u);
    uint8_t res[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 1,
                      0, 1 };
    CHECK_EQ(wa_mdns_reply(res, sizeof(res), 5353u, "rcbench-1A2B", k_ip,
                           g_out, sizeof(g_out)), 0u);
    /* A count of questions the message does not hold. */
    uint8_t many[128];
    const size_t m = query(many, 0u, "printer.local", 1u, 1u);
    many[4] = 0xFFu;
    CHECK_EQ(wa_mdns_reply(many, m, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    CHECK_EQ(wa_mdns_reply(NULL, 40u, 5353u, "rcbench-1A2B", k_ip, g_out,
                           sizeof(g_out)), 0u);
    CHECK_EQ(wa_mdns_announce("", k_ip, g_out, sizeof(g_out)), 0u);
}

int main(void)
{
    RUN(the_panels_name_is_answered_with_its_address);
    RUN(a_one_shot_query_gets_its_id_and_question_back);
    RUN(other_names_types_and_responses_are_not_answered);
    RUN(a_second_question_and_compressed_names_are_followed);
    RUN(malformed_queries_are_dropped_without_reading_past_them);
    return test_summary("wa_mdns");
}
