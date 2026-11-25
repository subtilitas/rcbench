/**
 * @file
 * @brief Draw a servo label or the test label as the printer gets it and
 *        write it as a PNG (Portable Network Graphics) image in reading
 *        orientation, one pixel a dot, black on white.
 *
 * The picture is read back through label_px_at(), so it shows the printer's
 * column-major bitmap turned to the way the label is read.  With --check
 * the picture is compared byte for byte with a file; the encoder is
 * deterministic, so a changed layout is a changed file.
 *
 * Usage:
 *
 *     render_label --roll 40x30 --id KST_X10-0000A [--dut "WING LEFT"]
 *                  (--out label.png | --check label.png)
 *     render_label --roll 40x30 --test 0.23.17 (--out ... | --check ...)
 *
 * Exit status: 0 written or equal, 1 different, 2 refused or a usage
 * error.  The host suite runs --check against test/host/fixtures/label/
 * (cases.txt there); --out writes a fixture anew.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "label_bitmap.h"
#include "label_layout.h"
#include "png_enc.h"

/** @brief The longest label's bitmap. */
static uint8_t s_bits[LABEL_BMP_MAX];
/** @brief The encoder: too large for the stack. */
static png_enc_t s_png;
/** @brief The picture as the encoder wrote it. */
static uint8_t *s_file;
/** @brief Its length. */
static size_t s_len;
/** @brief Its room. */
static size_t s_cap;

/**
 * @brief The encoder's sink: keeps the bytes.
 * @param data  the bytes
 * @param n     how many
 * @param ctx   unused
 * @return false when memory runs out
 */
static bool sink(const uint8_t *data, size_t n, void *ctx)
{
    (void)ctx;
    if (s_len + n > s_cap) {
        const size_t cap = (s_len + n) * 2u;
        uint8_t *p = realloc(s_file, cap);
        if (p == NULL) {
            return false;
        }
        s_file = p;
        s_cap = cap;
    }
    memcpy(s_file + s_len, data, n);
    s_len += n;
    return true;
}

/**
 * @brief Encode the bitmap in reading orientation into s_file.
 * @param b  the bitmap
 * @return false on an encoder failure
 */
static bool encode(const label_bmp_t *b)
{
    static uint16_t row[PNG_ENC_MAX_W];
    if (!png_enc_start(&s_png, b->w, b->cols, sink, NULL)) {
        return false;
    }
    for (int y = 0; y < (int)b->cols; y++) {
        for (int x = 0; x < (int)b->w; x++) {
            row[x] = label_px_at(b, x, y) ? 0x0000u : 0xFFFFu;
        }
        if (!png_enc_row(&s_png, row)) {
            return false;
        }
    }
    return png_enc_end(&s_png);
}

/**
 * @brief Compare s_file with the file at @p path.
 * @param path  the fixture
 * @return 0 equal, 1 different or unreadable
 */
static int check_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "render_label: cannot read %s\n", path);
        return 1;
    }
    int rc = 0;
    for (size_t i = 0u; i < s_len && rc == 0; i++) {
        const int c = fgetc(f);
        if (c == EOF || (uint8_t)c != s_file[i]) {
            rc = 1;
        }
    }
    if (rc == 0 && fgetc(f) != EOF) {
        rc = 1;
    }
    (void)fclose(f);
    if (rc != 0) {
        fprintf(stderr, "render_label: %s differs from the layout; "
                        "--out writes it anew\n", path);
    }
    return rc;
}

/**
 * @brief Write s_file to @p path.
 * @param path  the file
 * @return 0 written, 2 on a write failure
 */
static int write_file(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL || fwrite(s_file, 1u, s_len, f) != s_len) {
        fprintf(stderr, "render_label: cannot write %s\n", path);
        if (f != NULL) {
            (void)fclose(f);
        }
        return 2;
    }
    return fclose(f) == 0 ? 0 : 2;
}

/**
 * @brief The usage line.
 * @return 2
 */
static int usage(void)
{
    fprintf(stderr, "usage: render_label --roll WxH (--id ID [--dut NAME] | "
                    "--test VERSION) (--out FILE | --check FILE)\n");
    return 2;
}

/**
 * @brief Parse the options, lay the label out, write or check it.
 * @param argc  argument count
 * @param argv  arguments
 * @return the exit status
 */
int main(int argc, char **argv)
{
    unsigned w = 0u;
    unsigned h = 0u;
    const char *id = NULL;
    const char *dut = "";
    const char *version = NULL;
    const char *out = NULL;
    const char *check = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *opt = argv[i];
        const char *val = argv[i + 1];
        if (strcmp(opt, "--roll") == 0) {
            if (sscanf(val, "%ux%u", &w, &h) != 2 || w > 255u || h > 255u) {
                return usage();
            }
        } else if (strcmp(opt, "--id") == 0) {
            id = val;
        } else if (strcmp(opt, "--dut") == 0) {
            dut = val;
        } else if (strcmp(opt, "--test") == 0) {
            version = val;
        } else if (strcmp(opt, "--out") == 0) {
            out = val;
        } else if (strcmp(opt, "--check") == 0) {
            check = val;
        } else {
            return usage();
        }
    }
    if (argc % 2 == 0 || (id == NULL) == (version == NULL)
        || (out == NULL) == (check == NULL)) {
        return usage();
    }
    const label_roll_t roll = { (uint8_t)w, (uint8_t)h };
    label_bmp_t bmp = { .bits = s_bits, .cap = sizeof s_bits };
    static label_info_t info;
    const label_text_t text = { id, dut };
    const label_err_t err = (id != NULL)
        ? label_layout(&roll, &text, &bmp, &info)
        : label_layout_test(&roll, version, &bmp, &info);
    if (err != LABEL_OK) {
        fprintf(stderr, "render_label: layout refused, error %d\n", (int)err);
        return 2;
    }
    if (!encode(&bmp)) {
        fprintf(stderr, "render_label: PNG encoder failed\n");
        return 2;
    }
    printf("%ux%umm: %ux%u dots, QR version %u, module %u dots, %s\n",
           w, h, (unsigned)bmp.w, (unsigned)bmp.cols, (unsigned)info.version,
           (unsigned)info.module, info.stacked ? "stacked" : "side by side");
    const int rc = (out != NULL) ? write_file(out) : check_file(check);
    free(s_file);
    return rc;
}
