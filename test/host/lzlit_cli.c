/**
 * @file lzlit_cli.c
 * @brief Encode a file with protocols/supvan's literal-only LZMA encoder,
 *        for test/tools/test_lzlit.py, which decodes the output with
 *        Python's lzma (liblzma) and compares it with tools/lzlit.py.
 *
 *     lzlit_cli IN OUT
 *
 * The input goes to lzlit_put() in pieces of 1 to 4,096 bytes of uneven
 * length, as a print job feeds one 4,096-byte buffer at a time.  Exit 0 on
 * success, 2 on a read or write error, 3 when the stream overran
 * LZLIT_BOUND().
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "lzlit.h"

/** @brief The largest input taken, bytes. */
#define IN_MAX (1u << 20)

/** @brief The encoder's state, off the stack. */
static lzlit_t s_z;

/**
 * @brief Encode argv[1] into argv[2].
 * @param argc the argument count, 3
 * @param argv the program, the input and the output
 * @return 0, 2 on an I/O error or a wrong call, 3 on an overflow
 */
int main(int argc, char **argv)
{
    if (argc != 3) {
        (void)fprintf(stderr, "usage: lzlit_cli IN OUT\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        return 2;
    }
    uint8_t *in = malloc(IN_MAX);
    if (in == NULL) {
        (void)fclose(f);
        return 2;
    }
    const size_t n = fread(in, 1, IN_MAX, f);
    (void)fclose(f);
    const size_t cap = LZLIT_BOUND(n);
    uint8_t *out = malloc(LZLIT_BOUND(IN_MAX));
    if (out == NULL) {
        free(in);
        return 2;
    }
    lzlit_begin(&s_z, out, cap, (uint32_t)n);
    size_t k = 4096u;
    for (size_t o = 0u; o < n;) {
        const size_t m = (n - o < k) ? n - o : k;
        lzlit_put(&s_z, in + o, m);
        o += m;
        k = (k * 7u + 3u) % 4096u + 1u;
    }
    const size_t len = lzlit_end(&s_z);
    int rc = (len == 0u) ? 3 : 0;
    if (rc == 0) {
        f = fopen(argv[2], "wb");
        if (f == NULL || fwrite(out, 1, len, f) != len) {
            rc = 2;
        }
        if (f != NULL && fclose(f) != 0) {
            rc = 2;
        }
    }
    free(out);
    free(in);
    return rc;
}
