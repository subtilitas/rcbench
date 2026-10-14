/*
 * SHA-256 (Secure Hash Algorithm) as the coprocessor update uses it at both
 * ends, held to the test vectors of FIPS (Federal Information Processing
 * Standard) 180-2 appendix B and to the padding boundaries: 55 bytes is the
 * longest message whose length fits its last block, 56 the shortest that
 * needs another, 64 a block exactly.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "link_sha256.h"

static void hex(const uint8_t *d, char *out)
{
    for (unsigned i = 0; i < LINK_SHA256_BYTES; ++i) {
        snprintf(&out[2u * i], 3, "%02x", d[i]);
    }
}

static void check_digest(const void *msg, size_t len, const char *want)
{
    uint8_t d[LINK_SHA256_BYTES];
    char got[2u * LINK_SHA256_BYTES + 1u];
    link_sha256(msg, len, d);
    hex(d, got);
    CHECK_STR_EQ(got, want);
}

TEST_CASE(the_published_vectors)
{
    check_digest("abc", 3u,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_digest("", 0u,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                 56u,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/* One million 'a's, fed 1000 at a time: the long message of appendix B.3,
 * and 15,625 blocks through the same state. */
TEST_CASE(a_million_a_in_pieces)
{
    static uint8_t chunk[1000];
    memset(chunk, 'a', sizeof(chunk));
    link_sha256_t s;
    link_sha256_init(&s);
    for (unsigned i = 0; i < 1000u; ++i) {
        link_sha256_update(&s, chunk, sizeof(chunk));
    }
    uint8_t d[LINK_SHA256_BYTES];
    char got[2u * LINK_SHA256_BYTES + 1u];
    link_sha256_final(&s, d);
    hex(d, got);
    CHECK_STR_EQ(got,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE(the_padding_boundaries)
{
    static uint8_t a[65];
    memset(a, 'a', sizeof(a));
    check_digest(a, 55u,
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    check_digest(a, 56u,
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    check_digest(a, 63u,
        "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    check_digest(a, 64u,
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    check_digest(a, 65u,
        "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
}

/* Every split of a 200-byte message gives the one-call digest: the update
 * hashes 1 KiB on one pass and 8 KiB on another. */
TEST_CASE(any_split_gives_the_same_digest)
{
    uint8_t msg[200];
    for (unsigned i = 0; i < sizeof(msg); ++i) {
        msg[i] = (uint8_t)(i * 37u + 11u);
    }
    uint8_t whole[LINK_SHA256_BYTES];
    link_sha256(msg, sizeof(msg), whole);
    for (size_t split = 0; split <= sizeof(msg); ++split) {
        link_sha256_t s;
        link_sha256_init(&s);
        link_sha256_update(&s, msg, split);
        link_sha256_update(&s, NULL, 0u);
        link_sha256_update(&s, msg + split, sizeof(msg) - split);
        uint8_t d[LINK_SHA256_BYTES];
        link_sha256_final(&s, d);
        CHECK(memcmp(d, whole, sizeof(d)) == 0);
    }
}

TEST_CASE(null_arguments_do_nothing)
{
    uint8_t d[LINK_SHA256_BYTES];
    memset(d, 0xA5, sizeof(d));
    link_sha256_init(NULL);
    link_sha256_update(NULL, "x", 1u);
    link_sha256_final(NULL, d);
    link_sha256_t s;
    link_sha256_init(&s);
    link_sha256_final(&s, NULL);
    CHECK_EQ(d[0], 0xA5);
}

int main(void)
{
    RUN(the_published_vectors);
    RUN(a_million_a_in_pieces);
    RUN(the_padding_boundaries);
    RUN(any_split_gives_the_same_digest);
    RUN(null_arguments_do_nothing);
    return test_summary("link_sha256");
}
