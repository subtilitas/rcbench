/*
 * Which requests the Wi-Fi page answers (wa_guard.h).
 *
 * The failures under test: a rebound name served the card, a page of
 * another site posted a code or an image, an upload without the page's own
 * header taken, the panel's own name refused for its case or its port, and
 * a body read on a request that has none.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "wa_guard.h"

static const wa_self_t k_ap = { "rcbench-1A2B", { 192, 168, 4, 1 } };
static const wa_self_t k_sta = { "rcbench-1A2B", { 10, 0, 0, 23 } };

TEST_CASE(the_panels_names_are_taken_in_any_case_and_with_port_80)
{
    CHECK(wa_host_ok("rcbench-1A2B.local", &k_ap));
    CHECK(wa_host_ok("rcbench-1a2b.local", &k_ap));
    CHECK(wa_host_ok("RCBENCH-1A2B.LOCAL", &k_ap));
    CHECK(wa_host_ok("rcbench-1a2b.local:80", &k_ap));
    CHECK(wa_host_ok("rcbench-1a2b.local.", &k_ap));
    CHECK(wa_host_ok("rcbench-1a2b.local.:80", &k_ap));
    CHECK(wa_host_ok("rcbench-1a2b", &k_ap));
    CHECK(wa_host_ok("rcbench-1A2B:80", &k_ap));
    CHECK(wa_host_ok("192.168.4.1", &k_ap));
    CHECK(wa_host_ok("192.168.4.1:80", &k_ap));
    CHECK(wa_host_ok("10.0.0.23", &k_sta));
    CHECK(wa_host_ok("10.0.0.23:80", &k_sta));
}

TEST_CASE(any_other_host_is_refused)
{
    CHECK(!wa_host_ok(NULL, &k_ap));
    CHECK(!wa_host_ok("", &k_ap));
    CHECK(!wa_host_ok("evil.example", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.local.evil.example", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.evil.example", &k_ap));
    CHECK(!wa_host_ok("x.rcbench-1a2b.local", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2c.local", &k_ap));     /* another panel */
    CHECK(!wa_host_ok("rcbench-1a2b.loca", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.local..", &k_ap));
    CHECK(!wa_host_ok(" rcbench-1a2b.local", &k_ap));
    /* Ports: only 80, written as 80. */
    CHECK(!wa_host_ok("rcbench-1a2b.local:8080", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.local:080", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.local:", &k_ap));
    CHECK(!wa_host_ok("rcbench-1a2b.local:80:80", &k_ap));
    CHECK(!wa_host_ok("192.168.4.1:81", &k_ap));
    /* The other mode's address, and addresses in other forms. */
    CHECK(!wa_host_ok("192.168.4.1", &k_sta));
    CHECK(!wa_host_ok("10.0.0.23", &k_ap));
    CHECK(!wa_host_ok("192.168.004.001", &k_ap));
    CHECK(!wa_host_ok("3232236545", &k_ap));          /* 192.168.4.1 */
    CHECK(!wa_host_ok("192.168.4.1.", &k_ap));
    /* IPv6 literals, the mapped form of the address included. */
    CHECK(!wa_host_ok("[::1]", &k_ap));
    CHECK(!wa_host_ok("[::ffff:192.168.4.1]", &k_ap));
    CHECK(!wa_host_ok("[::ffff:192.168.4.1]:80", &k_ap));
    /* No address known: 0.0.0.0 matches nothing. */
    const wa_self_t none = { "rcbench-1A2B", { 0, 0, 0, 0 } };
    CHECK(!wa_host_ok("0.0.0.0", &none));
    CHECK(wa_host_ok("rcbench-1a2b.local", &none));
    const wa_self_t nameless = { NULL, { 192, 168, 4, 1 } };
    CHECK(!wa_host_ok("192.168.4.1", &nameless));
    CHECK(!wa_host_ok("rcbench-1a2b.local", NULL));
    /* 63 bytes and more: no name of the panel's. */
    char longer[80];
    memset(longer, 'a', 70u);
    longer[70] = '\0';
    CHECK(!wa_host_ok(longer, &k_ap));
}

TEST_CASE(a_write_needs_the_panels_own_origin)
{
    CHECK(wa_origin_ok("http://rcbench-1a2b.local", NULL, &k_sta));
    CHECK(wa_origin_ok("HTTP://RCBENCH-1A2B.LOCAL:80", NULL, &k_sta));
    CHECK(wa_origin_ok("http://10.0.0.23", NULL, &k_sta));
    CHECK(wa_origin_ok("http://192.168.4.1", NULL, &k_ap));
    CHECK(!wa_origin_ok("https://rcbench-1a2b.local", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://evil.example", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://192.168.4.1", NULL, &k_sta));
    CHECK(!wa_origin_ok("null", NULL, &k_sta));
    CHECK(!wa_origin_ok("", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://rcbench-1a2b.local/", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://user@rcbench-1a2b.local", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://[::1]", NULL, &k_sta));
    CHECK(!wa_origin_ok("http://rcbench-1a2b.local:8080", NULL, &k_sta));
    /* Neither header: refused. */
    CHECK(!wa_origin_ok(NULL, NULL, &k_sta));
}

TEST_CASE(without_an_origin_the_referer_decides)
{
    CHECK(wa_origin_ok(NULL, "http://rcbench-1a2b.local/", &k_sta));
    CHECK(wa_origin_ok(NULL, "http://rcbench-1a2b.local/code", &k_sta));
    CHECK(wa_origin_ok(NULL, "http://rcbench-1a2b.local", &k_sta));
    CHECK(wa_origin_ok(NULL, "http://10.0.0.23:80/?x=1", &k_sta));
    CHECK(wa_origin_ok(NULL, "http://10.0.0.23#top", &k_sta));
    CHECK(!wa_origin_ok(NULL, "http://evil.example/", &k_sta));
    CHECK(!wa_origin_ok(NULL, "http://evil.example/http://10.0.0.23/",
                        &k_sta));
    CHECK(!wa_origin_ok(NULL, "http://rcbench-1a2b.local.evil.example/",
                        &k_sta));
    CHECK(!wa_origin_ok(NULL, "http://user@10.0.0.23/", &k_sta));
    CHECK(!wa_origin_ok(NULL, "https://10.0.0.23/", &k_sta));
    CHECK(!wa_origin_ok(NULL, "", &k_sta));       /* too long to read */
    /* An Origin that is there decides alone: a good Referer does not save
     * a foreign one. */
    CHECK(!wa_origin_ok("http://evil.example", "http://10.0.0.23/", &k_sta));
    CHECK(!wa_origin_ok("null", "http://10.0.0.23/", &k_sta));
    CHECK(wa_origin_ok("http://10.0.0.23", "http://evil.example/", &k_sta));
}

TEST_CASE(the_guard_checks_host_body_origin_and_upload_in_order)
{
    wa_req_t r = { "rcbench-1a2b.local", NULL, NULL, NULL, 0u };
    CHECK_EQ(wa_guard(WA_REQ_READ, &r, &k_sta), WA_GUARD_OK);
    /* A read needs no Origin, a write does. */
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_ORIGIN);
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_ORIGIN);
    r.origin = "http://rcbench-1a2b.local";
    r.body = 13u;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_OK);
    /* The upload's own header, exactly. */
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_UPLOAD);
    r.upload = "0";
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_UPLOAD);
    r.upload = "";
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_UPLOAD);
    r.upload = WA_UPLOAD_VAL;
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_OK);
    /* A foreign Origin is refused before the header is looked at. */
    r.origin = "http://evil.example";
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_ORIGIN);
    /* The Host first: a rebound name never reaches the other checks. */
    r.host = "evil.example";
    CHECK_EQ(wa_guard(WA_REQ_READ, &r, &k_sta), WA_GUARD_HOST);
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_HOST);
    r.host = NULL;
    CHECK_EQ(wa_guard(WA_REQ_READ, &r, &k_sta), WA_GUARD_HOST);
    CHECK_EQ(wa_guard(WA_REQ_READ, NULL, &k_sta), WA_GUARD_HOST);
}

TEST_CASE(bodies_are_bounded_before_a_byte_is_read)
{
    wa_req_t r = { "10.0.0.23", "http://10.0.0.23", NULL, NULL, 1u };
    CHECK_EQ(wa_guard(WA_REQ_READ, &r, &k_sta), WA_GUARD_BODY);
    r.body = 0u;
    CHECK_EQ(wa_guard(WA_REQ_READ, &r, &k_sta), WA_GUARD_OK);
    r.body = WA_CODE_BODY_MAX;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_OK);
    r.body = WA_CODE_BODY_MAX + 1u;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_BIG);
    r.body = 2000000000u;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_BIG);
    /* The size is checked before the Origin: a foreign code post of any
     * size is refused, never read. */
    r.origin = "http://evil.example";
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_BIG);
    r.body = 10u;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &r, &k_sta), WA_GUARD_ORIGIN);
    /* An upload's size is the receiver's to judge (fw_upload_begin()). */
    r.origin = "http://10.0.0.23";
    r.upload = WA_UPLOAD_VAL;
    r.body = UINT64_MAX;
    CHECK_EQ(wa_guard(WA_REQ_UPLOAD, &r, &k_sta), WA_GUARD_OK);
}

TEST_CASE(each_refusal_has_its_status_line)
{
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_HOST), "421 Misdirected Request");
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_BODY), "400 Bad Request");
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_BIG), "413 Content Too Large");
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_ORIGIN), "403 Forbidden");
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_UPLOAD), "403 Forbidden");
    CHECK_STR_EQ(wa_guard_status(WA_GUARD_OK), "200 OK");
}

int main(void)
{
    RUN(the_panels_names_are_taken_in_any_case_and_with_port_80);
    RUN(any_other_host_is_refused);
    RUN(a_write_needs_the_panels_own_origin);
    RUN(without_an_origin_the_referer_decides);
    RUN(the_guard_checks_host_body_origin_and_upload_in_order);
    RUN(bodies_are_bounded_before_a_byte_is_read);
    RUN(each_refusal_has_its_status_line);
    return test_summary("wa_guard");
}
