/*
 * What GitHub says about the panel's releases, read strictly.
 *
 * The failure under test is an answer that is acted on and should not be:
 * a Location that names another place, a version text that is not one, a
 * redirect to a host outside the list, a body size that is missing or over
 * the cap, a SHA256SUMS that is out of form or names the file twice.  Each
 * bound is tested on both sides.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "fw_release.h"

#define TAG FW_REL_TAG_PREFIX

static fw_rel_err_t tag(const char *loc, fw_version_t *v)
{
    return fw_release_parse_tag(loc, strlen(loc), false, v);
}

TEST_CASE(the_latest_redirect_names_the_release)
{
    fw_version_t v;
    CHECK_EQ(tag(TAG "v0.19.0", &v), FW_REL_OK);
    CHECK_EQ(v.major, 0u);
    CHECK_EQ(v.minor, 19u);
    CHECK_EQ(v.patch, 0u);
    CHECK_EQ(tag(TAG "v65535.65535.65535", &v), FW_REL_OK);
    CHECK_EQ(v.major, 65535u);
    CHECK_EQ(v.patch, 65535u);
    CHECK_EQ(tag(TAG "v0.0.0", &v), FW_REL_OK);
    CHECK_EQ(tag(TAG "v10.200.3000", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 200u);
    CHECK_EQ(v.patch, 3000u);
}

TEST_CASE(a_version_text_out_of_form_is_refused)
{
    fw_version_t v = { 7, 7, 7 };
    /* Over 65535, a sixth digit, a leading zero. */
    CHECK_EQ(tag(TAG "v65536.0.0", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v0.65536.0", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v0.0.65536", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v99999.0.0", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.02.3", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v00.1.1", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.03", &v), FW_REL_VERSION);
    /* Parts missing, one too many, empty, signs, letters, a suffix. */
    CHECK_EQ(tag(TAG "v1.2", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.3.4", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1..3", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v.1.2", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.-3", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.+3", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.3a", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.3-rc1", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v0x1.2.3", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.3/", &v), FW_REL_VERSION);
    CHECK_EQ(tag(TAG "v1.2.3?x=1", &v), FW_REL_VERSION);
    /* Nothing refused changed the output. */
    CHECK_EQ(v.major, 7u);
    CHECK_EQ(v.minor, 7u);
    CHECK_EQ(v.patch, 7u);
}

TEST_CASE(a_location_elsewhere_is_refused)
{
    fw_version_t v;
    CHECK_EQ(tag("http://github.com/subtilitas/rcbench/releases/tag/v1.2.3",
                  &v), FW_REL_PLACE);
    CHECK_EQ(tag("https://github.com.evil/subtilitas/rcbench/releases/tag/"
                  "v1.2.3", &v), FW_REL_PLACE);
    CHECK_EQ(tag("https://evil.com/subtilitas/rcbench/releases/tag/v1.2.3",
                  &v), FW_REL_PLACE);
    CHECK_EQ(tag("https://github.com/other/rcbench/releases/tag/v1.2.3", &v),
             FW_REL_PLACE);
    CHECK_EQ(tag("https://GITHUB.com/subtilitas/rcbench/releases/tag/v1.2.3",
                  &v), FW_REL_PLACE);
    CHECK_EQ(tag(TAG "1.2.3", &v), FW_REL_PLACE);
    CHECK_EQ(tag(TAG "V1.2.3", &v), FW_REL_PLACE);
    CHECK_EQ(tag(TAG "v", &v), FW_REL_PLACE);
    CHECK_EQ(tag(TAG, &v), FW_REL_PLACE);
    /* The latest page itself: no release yet. */
    CHECK_EQ(tag("https://github.com/subtilitas/rcbench/releases", &v),
             FW_REL_PLACE);
    /* Relative: a server may send one; the panel does not resolve it. */
    CHECK_EQ(tag("/subtilitas/rcbench/releases/tag/v1.2.3", &v),
             FW_REL_PLACE);
}

TEST_CASE(a_location_missing_too_long_or_with_odd_bytes_is_refused)
{
    fw_version_t v;
    CHECK_EQ(fw_release_parse_tag(NULL, 0u, false, &v), FW_REL_MISSING);
    CHECK_EQ(fw_release_parse_tag("", 0u, false, &v), FW_REL_MISSING);
    /* Cut on the way in: whatever is left is not read. */
    CHECK_EQ(fw_release_parse_tag(TAG "v1.2.3", strlen(TAG "v1.2.3"), true,
                                  &v), FW_REL_LONG);
    CHECK_EQ(fw_release_parse_tag("", 0u, true, &v), FW_REL_LONG);
    /* The longest tag fits; one byte more does not. */
    CHECK_EQ(tag(TAG "v65535.65535.65535", &v), FW_REL_OK);
    CHECK_EQ(tag(TAG "v65535.65535.655355", &v), FW_REL_LONG);
    char big[4096];
    memset(big, 'a', sizeof(big));
    CHECK_EQ(fw_release_parse_tag(big, sizeof(big), false, &v), FW_REL_LONG);
    /* A NUL, a space, a control byte, a high byte inside the bytes. */
    char loc[64];
    const size_t n = (size_t)snprintf(loc, sizeof(loc), "%s", TAG "v1.2.3");
    const char odd[] = { '\0', ' ', '\r', '\n', '\t', (char)0x7F,
                         (char)0x80, (char)0xFF };
    for (size_t i = 0; i < sizeof(odd); ++i) {
        char copy[64];
        memcpy(copy, loc, n);
        copy[n - 3u] = odd[i];
        CHECK_EQ(fw_release_parse_tag(copy, n, false, &v), FW_REL_CHAR);
    }
    /* Bytes not terminated: exactly n are read. */
    char raw[64];
    memcpy(raw, TAG "v1.2.3XXXX", strlen(TAG "v1.2.3XXXX"));
    CHECK_EQ(fw_release_parse_tag(raw, strlen(TAG "v1.2.3"), false, &v),
             FW_REL_OK);
    CHECK_EQ(v.patch, 3u);
}

TEST_CASE(the_version_parser_on_its_own)
{
    fw_version_t v;
    CHECK(fw_release_parse_version("0.0.0", 5u, &v));
    CHECK(fw_release_parse_version("1.20.300", 8u, &v));
    CHECK_EQ(v.minor, 20u);
    CHECK(!fw_release_parse_version(NULL, 0u, &v));
    CHECK(!fw_release_parse_version("", 0u, &v));
    CHECK(!fw_release_parse_version("1.2.3", 4u, &v));   /* "1.2." */
    CHECK(!fw_release_parse_version("100000.0.0", 10u, &v));
    CHECK(fw_release_parse_version("1.2.3", 5u, NULL));
}

TEST_CASE(a_status_says_body_redirect_or_refused)
{
    CHECK_EQ(fw_release_status(200), FW_REL_BODY);
    CHECK_EQ(fw_release_status(301), FW_REL_REDIRECT);
    CHECK_EQ(fw_release_status(302), FW_REL_REDIRECT);
    CHECK_EQ(fw_release_status(303), FW_REL_REDIRECT);
    CHECK_EQ(fw_release_status(307), FW_REL_REDIRECT);
    CHECK_EQ(fw_release_status(308), FW_REL_REDIRECT);
    const int refused[] = { 0, -1, 100, 199, 201, 204, 206, 300, 304, 305,
                            306, 309, 400, 403, 404, 429, 500, 503, 999 };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK_EQ(fw_release_status(refused[i]), FW_REL_REFUSED);
    }
}

static fw_rel_err_t hop(const char *loc, unsigned hops)
{
    return fw_release_redirect(loc, strlen(loc), false, hops);
}

TEST_CASE(a_redirect_goes_only_to_the_listed_hosts)
{
    CHECK_EQ(hop("https://release-assets.githubusercontent.com/github-"
                 "production-release-asset/1/2?sp=r&sig=x", 0u), FW_REL_OK);
    CHECK_EQ(hop("https://objects.githubusercontent.com/a", 0u), FW_REL_OK);
    CHECK_EQ(hop("https://github.com/subtilitas/rcbench/releases/download/"
                 "v1.2.3/SHA256SUMS", 0u), FW_REL_OK);
    /* Other hosts, look-alikes, a user part, a port, another case. */
    CHECK_EQ(hop("https://evil.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://github.com.evil.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://evilgithub.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://x.github.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://githubusercontent.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://raw.githubusercontent.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://user@github.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://github.com:443/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https://GitHub.com/x", 0u), FW_REL_HOST);
    CHECK_EQ(hop("https:///x", 0u), FW_REL_HOST);
    /* Not https, no path, relative. */
    CHECK_EQ(hop("http://github.com/x", 0u), FW_REL_PLACE);
    CHECK_EQ(hop("ftp://github.com/x", 0u), FW_REL_PLACE);
    CHECK_EQ(hop("https://github.com", 0u), FW_REL_PLACE);
    CHECK_EQ(hop("/x/y", 0u), FW_REL_PLACE);
    CHECK_EQ(hop("https://", 0u), FW_REL_PLACE);
}

TEST_CASE(a_redirect_chain_stops_at_its_limit)
{
    const char *ok = "https://github.com/x";
    for (unsigned h = 0; h < FW_REL_HOPS_MAX; ++h) {
        CHECK_EQ(hop(ok, h), FW_REL_OK);
    }
    CHECK_EQ(hop(ok, FW_REL_HOPS_MAX), FW_REL_HOPS);
    CHECK_EQ(hop(ok, 1000u), FW_REL_HOPS);
    /* The hop count is checked first: a missing Location past the limit
     * is still past the limit. */
    CHECK_EQ(fw_release_redirect(NULL, 0u, false, FW_REL_HOPS_MAX),
             FW_REL_HOPS);
}

TEST_CASE(a_redirect_missing_long_or_with_odd_bytes_is_refused)
{
    CHECK_EQ(fw_release_redirect(NULL, 0u, false, 0u), FW_REL_MISSING);
    CHECK_EQ(fw_release_redirect("", 0u, false, 0u), FW_REL_MISSING);
    CHECK_EQ(fw_release_redirect("https://github.com/x", 20u, true, 0u),
             FW_REL_LONG);
    static char url[FW_REL_URL_MAX + 2u];
    const char head[] = "https://github.com/";
    memcpy(url, head, sizeof(head) - 1u);
    memset(url + sizeof(head) - 1u, 'a', sizeof(url) - sizeof(head) + 1u);
    CHECK_EQ(fw_release_redirect(url, FW_REL_URL_MAX, false, 0u), FW_REL_OK);
    CHECK_EQ(fw_release_redirect(url, FW_REL_URL_MAX + 1u, false, 0u),
             FW_REL_LONG);
    const char sp[] = "https://github.com/a b";
    CHECK_EQ(hop(sp, 0u), FW_REL_CHAR);
    const char crlf[] = "https://github.com/a\r\nX-Evil: 1";
    CHECK_EQ(hop(crlf, 0u), FW_REL_CHAR);
    char nul[] = "https://github.com/a\0b";
    CHECK_EQ(fw_release_redirect(nul, sizeof(nul) - 1u, false, 0u),
             FW_REL_CHAR);
}

TEST_CASE(a_body_size_is_announced_and_within_the_cap)
{
    const uint32_t slot = 4063232u;
    CHECK_EQ(fw_release_length(1, false, slot), FW_REL_OK);
    CHECK_EQ(fw_release_length(slot, false, slot), FW_REL_OK);
    CHECK_EQ(fw_release_length((int64_t)slot + 1, false, slot),
             FW_REL_TOO_BIG);
    CHECK_EQ(fw_release_length(INT64_MAX, false, slot), FW_REL_TOO_BIG);
    CHECK_EQ(fw_release_length(0, false, slot), FW_REL_EMPTY);
    CHECK_EQ(fw_release_length(-1, false, slot), FW_REL_NO_LENGTH);
    CHECK_EQ(fw_release_length(INT64_MIN, false, slot), FW_REL_NO_LENGTH);
    /* Chunked: the size is not known before the end. */
    CHECK_EQ(fw_release_length(100, true, slot), FW_REL_NO_LENGTH);
    CHECK_EQ(fw_release_length(-1, true, slot), FW_REL_NO_LENGTH);
}

#define H0 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define H1 "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
#define NAME "rcbench-panel-v0.19.0.bin"

static fw_rel_err_t sums(const char *text, uint8_t out[FW_SHA256_BYTES])
{
    return fw_release_parse_sums(text, strlen(text), NAME, out);
}

TEST_CASE(sha256sums_as_the_release_writes_it_gives_the_hash)
{
    /* The form release.yml writes: sha256sum over the ZIP and BIN files,
     * each named with "./". */
    uint8_t h[FW_SHA256_BYTES];
    CHECK_EQ(sums(H1 "  ./rcbench-coproc-v0.19.0.zip\n"
                  H0 "  ./" NAME "\n", h), FW_REL_OK);
    CHECK_EQ(h[0], 0x01u);
    CHECK_EQ(h[1], 0x23u);
    CHECK_EQ(h[31], 0xEFu);
    /* Without "./", and the binary-mode marker. */
    CHECK_EQ(sums(H0 "  " NAME "\n", h), FW_REL_OK);
    CHECK_EQ(sums(H1 " *" NAME "\n", h), FW_REL_OK);
    CHECK_EQ(h[0], 0xFEu);
}

TEST_CASE(sha256sums_without_the_files_line_or_with_it_twice)
{
    uint8_t h[FW_SHA256_BYTES];
    CHECK_EQ(sums(H0 "  ./rcbench-coproc-v0.19.0.zip\n", h),
             FW_REL_SUMS_NONE);
    /* Another version's image is another file. */
    CHECK_EQ(sums(H0 "  ./rcbench-panel-v0.19.1.bin\n", h), FW_REL_SUMS_NONE);
    CHECK_EQ(sums(H0 "  ./" NAME ".sig\n", h), FW_REL_SUMS_NONE);
    CHECK_EQ(sums(H0 "  ./x" NAME "\n", h), FW_REL_SUMS_NONE);
    CHECK_EQ(sums(H0 "  ./" NAME "\n" H1 "  " NAME "\n", h),
             FW_REL_SUMS_TWICE);
    CHECK_EQ(sums(H0 "  ./" NAME "\n" H0 "  ./" NAME "\n", h),
             FW_REL_SUMS_TWICE);
}

TEST_CASE(sha256sums_out_of_form_is_refused_whole)
{
    uint8_t h[FW_SHA256_BYTES];
    CHECK_EQ(fw_release_parse_sums("", 0u, NAME, h), FW_REL_SUMS_BAD);
    CHECK_EQ(fw_release_parse_sums(NULL, 0u, NAME, h), FW_REL_SUMS_BAD);
    CHECK_EQ(fw_release_parse_sums("x\n", 2u, NULL, h), FW_REL_SUMS_BAD);
    /* No final newline: the last line may have been cut. */
    CHECK_EQ(sums(H0 "  ./" NAME, h), FW_REL_SUMS_BAD);
    /* Upper case, a short and a long hash, a non-hex digit. */
    char up[] = H0 "  ./" NAME "\n";
    up[10] = 'A';
    CHECK_EQ(sums(up, h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums("0123456789abcdef0123456789abcdef0123456789abcdef"
                  "0123456789abcde  ./" NAME "\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "0  ./" NAME "\n", h), FW_REL_SUMS_BAD);
    char g[] = H0 "  ./" NAME "\n";
    g[3] = 'g';
    CHECK_EQ(sums(g, h), FW_REL_SUMS_BAD);
    /* Separators: one space, a tab, three spaces. */
    CHECK_EQ(sums(H0 " ./" NAME "\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "\t./" NAME "\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "   ./" NAME "\n", h), FW_REL_SUMS_BAD);
    /* An empty line, a CRLF line, an empty name, a path, a space in it. */
    CHECK_EQ(sums(H0 "  ./" NAME "\n\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  ./" NAME "\r\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  ./\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  \n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  ../" NAME "\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  dir/" NAME "\n", h), FW_REL_SUMS_BAD);
    CHECK_EQ(sums(H0 "  ./a b\n" H0 "  ./" NAME "\n", h), FW_REL_SUMS_BAD);
    /* A bad line anywhere refuses the file, the image's line too. */
    CHECK_EQ(sums(H0 "  ./" NAME "\ngarbage\n", h), FW_REL_SUMS_BAD);
    /* A NUL inside. */
    char nul[] = H0 "  ./" NAME "\n";
    nul[70] = '\0';
    CHECK_EQ(fw_release_parse_sums(nul, sizeof(nul) - 1u, NAME, h),
             FW_REL_SUMS_BAD);
}

TEST_CASE(sha256sums_over_its_bound_is_refused)
{
    static char text[FW_REL_SUMS_MAX + 200u];
    const char line[] = H0 "  ./" NAME "\n";
    const size_t ln = sizeof(line) - 1u;
    size_t n = 0u;
    while (n + ln <= FW_REL_SUMS_MAX) {
        memcpy(text + n, line, ln);
        n += ln;
    }
    /* Lines to the bound: refused only because the name is on many. */
    uint8_t h[FW_SHA256_BYTES];
    CHECK_EQ(fw_release_parse_sums(text, n, NAME, h), FW_REL_SUMS_TWICE);
    CHECK_EQ(fw_release_parse_sums(text, FW_REL_SUMS_MAX + 1u, NAME, h),
             FW_REL_LONG);
}

TEST_CASE(names_and_urls_of_a_release)
{
    char t[FW_REL_VER_TEXT];
    char n[FW_REL_NAME_MAX];
    const fw_version_t big = { 65535u, 65535u, 65535u };
    fw_release_version_text(big, t);
    CHECK_STR_EQ(t, "65535.65535.65535");
    fw_release_image_name(big, n);
    CHECK_STR_EQ(n, "rcbench-panel-v65535.65535.65535.bin");
    const fw_version_t v = { 0u, 20u, 1u };
    fw_release_image_name(v, n);
    CHECK_STR_EQ(n, "rcbench-panel-v0.20.1.bin");
    char url[FW_REL_URL_MAX + 1u];
    CHECK(fw_release_url(v, n, url, sizeof(url)));
    CHECK_STR_EQ(url, "https://github.com/subtilitas/rcbench/releases/"
                      "download/v0.20.1/rcbench-panel-v0.20.1.bin");
    CHECK(fw_release_url(v, FW_REL_SUMS_NAME, url, sizeof(url)));
    CHECK_STR_EQ(url, "https://github.com/subtilitas/rcbench/releases/"
                      "download/v0.20.1/SHA256SUMS");
    /* A download URL leads to a listed host. */
    CHECK_EQ(hop(url, 0u), FW_REL_OK);
    char small[20];
    CHECK(!fw_release_url(v, n, small, sizeof(small)));
    CHECK(!fw_release_url(v, NULL, url, sizeof(url)));
    CHECK(!fw_release_url(v, n, NULL, 10u));
    CHECK(strcmp(fw_rel_err_str(FW_REL_HOST), "?") != 0);
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_COUNT), "?");
}

int main(void)
{
    RUN(the_latest_redirect_names_the_release);
    RUN(a_version_text_out_of_form_is_refused);
    RUN(a_location_elsewhere_is_refused);
    RUN(a_location_missing_too_long_or_with_odd_bytes_is_refused);
    RUN(the_version_parser_on_its_own);
    RUN(a_status_says_body_redirect_or_refused);
    RUN(a_redirect_goes_only_to_the_listed_hosts);
    RUN(a_redirect_chain_stops_at_its_limit);
    RUN(a_redirect_missing_long_or_with_odd_bytes_is_refused);
    RUN(a_body_size_is_announced_and_within_the_cap);
    RUN(sha256sums_as_the_release_writes_it_gives_the_hash);
    RUN(sha256sums_without_the_files_line_or_with_it_twice);
    RUN(sha256sums_out_of_form_is_refused_whole);
    RUN(sha256sums_over_its_bound_is_refused);
    RUN(names_and_urls_of_a_release);
    return test_summary("fw_release");
}
