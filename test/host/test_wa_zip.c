/*
 * The whole card as one ZIP archive, written while it is sent.
 *
 * The failures under test: an archive another program cannot open, a
 * Content-Length that differs from the bytes sent (the browser then
 * reports the download failed or waits for bytes that never come), a
 * size, offset or entry count past what 32 or 16 bits hold written without
 * ZIP64, a read error or a pulled card that ends in an archive that looks
 * whole, and a file written to during the download that breaks the archive.
 *
 * Every archive written here goes to ZIP_OUT_DIR with a manifest of what
 * it has to hold; check_wa_zip.py then opens each with Python's zipfile
 * (testzip()) and with `unzip -t`, and holds names, sizes, CRCs and times
 * to the manifest.  The ZIP64 limits are written as sparse files, the data
 * a hole, and read by their directories only: 4 GiB of data is not read.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "greatest.h"

#include "wa_fakefs.h"
#include "wa_zip.h"

/* The sparse archives seek past 4 GiB. */
_Static_assert(sizeof(off_t) >= 8, "off_t must hold an offset past 4 GiB");

#ifndef ZIP_OUT_DIR
#define ZIP_OUT_DIR "wa_zip_out"
#endif

/* ------------------------------------------------------------ helpers */

/* CRC-32 bit by bit, apart from the table the code under test uses. */
static uint32_t crc_bitwise(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int k = 0; k < 8; ++k) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

/* CRC-32 of node @p node's first @p size bytes, bit by bit. */
static uint32_t prefix_crc(int node, uint64_t size)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint64_t i = 0; i < size; ++i) {
        c = crc_bitwise(c, fake_byte(node, i));
    }
    return ~c;
}

static uint32_t node_crc(const fake_fs_t *fs, int node)
{
    return prefix_crc(node, fs->n[node].size);
}

static void out_path(char *buf, size_t cap, const char *name, const char *ext)
{
    snprintf(buf, cap, "%s/%s.%s", ZIP_OUT_DIR, name, ext);
}

/* The manifest: one line an entry, in the order a walk gives them. */
static void manifest_walk(fake_fs_t *fs, FILE *m, int node)
{
    /* Files, then folders, as wa_walk.h orders them. */
    for (int pass = 0; pass < 2; ++pass) {
        for (int c = fs->n[node].first; c >= 0; c = fs->n[c].next) {
            const fake_node_t *n = &fs->n[c];
            if (n->dir != (pass == 1)) {
                continue;
            }
            if (n->dir) {
                fprintf(m, "D\t%s/\t0\t0\t%lld\n", n->path,
                        (long long)n->mtime);
                manifest_walk(fs, m, c);
            } else {
                fprintf(m, "F\t%s\t%llu\t%u\t%lld\n", n->path,
                        (unsigned long long)n->size, node_crc(fs, c),
                        (long long)n->mtime);
            }
        }
    }
}

static uint8_t g_buf[8192];

/* Called after each piece a stream takes, with the bytes taken so far: a
 * case changes the card there as a program writing to it would. */
typedef void (*zip_hook_t)(fake_fs_t *fs, const wa_zip_t *z, uint64_t total);

/* Stream @p fs as an archive into "<name>.zip" in pieces of @p piece bytes,
 * @p hook called after each; write the manifest of the card as it was when
 * the archive began.  Returns bytes produced, checks they are announced. */
static uint64_t write_zip_with(fake_fs_t *fs, const char *name,
                               uint32_t piece, zip_hook_t hook)
{
    const wa_fs_t w = fake_wa(fs);
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    wa_zip_slot_t *slot = calloc((size_t)plan.entries + 1u, sizeof(*slot));
    wa_zip_t *z = malloc(sizeof(*z));
    REQUIRE_ALLOC(z);
    REQUIRE_ALLOC(slot);
    CHECK_EQ(wa_zip_begin(z, &w, slot, plan.entries), WA_ZIP_OK);
    CHECK_EQ(z->plan.bytes, plan.bytes);
    CHECK_EQ(z->plan.entries, plan.entries);
    char path[512];
    out_path(path, sizeof(path), name, "txt");
    FILE *m = fopen(path, "w");
    CHECK(m != NULL);
    fprintf(m, "# full\n");
    manifest_walk(fs, m, 0);
    fclose(m);
    out_path(path, sizeof(path), name, "zip");
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    uint64_t total = 0u;
    for (;;) {
        const int32_t n = wa_zip_next(z, g_buf, piece);
        CHECK(n >= 0);
        if (n <= 0) {
            break;
        }
        CHECK(n <= (int32_t)piece);
        if ((size_t)n > sizeof(g_buf)) {
            break;    /* failed above */
        }
        fwrite(g_buf, 1u, (size_t)n, f);
        total += (uint64_t)n;
        if (hook != NULL) {
            hook(fs, z, total);
        }
    }
    fclose(f);
    CHECK_EQ(wa_zip_next(z, g_buf, piece), 0);
    /* The announced length is the length. */
    CHECK_EQ(total, plan.bytes);
    CHECK_EQ(fs->files_open, 0);
    CHECK_EQ(fs->dirs_open, 0);
    CHECK_EQ(fs->dirs_open_max, 1);
    free(z);
    free(slot);
    return total;
}

static uint64_t write_zip(fake_fs_t *fs, const char *name, uint32_t piece)
{
    return write_zip_with(fs, name, piece, NULL);
}

static void setup_dir(void)
{
    if (mkdir(ZIP_OUT_DIR, 0755) != 0 && errno != EEXIST) {
        T_FAIL("cannot make %s", ZIP_OUT_DIR);
    }
}

/* ------------------------------------------------------------ cases */

TEST_CASE(crc32_and_dos_times_are_as_zip_takes_them)
{
    CHECK_EQ(wa_crc32(0u, "123456789", 9u), 0xCBF43926u);
    CHECK_EQ(wa_crc32(0u, "", 0u), 0u);
    CHECK_EQ(wa_crc32(wa_crc32(0u, "1234", 4u), "56789", 5u), 0xCBF43926u);
    uint16_t t, d;
    wa_dos_time(0, &t, &d);                     /* before 1980 */
    CHECK_EQ(d, 0x0021u);
    CHECK_EQ(t, 0u);
    wa_dos_time(315532800, &t, &d);             /* 1980-01-01 00:00:00 */
    CHECK_EQ(d, 0x0021u);
    wa_dos_time(1791961199, &t, &d);            /* 2026-10-14 06:59:59 */
    CHECK_EQ(d, (uint16_t)((46u << 9) | (10u << 5) | 14u));
    CHECK_EQ(t, (uint16_t)((6u << 11) | (59u << 5) | 29u));
    wa_dos_time(4354819198, &t, &d);            /* the last DOS time */
    CHECK_EQ(d, 0xFF9Fu);
    CHECK_EQ(t, 0xBF7Du);
    wa_dos_time(4354819199 + 86400, &t, &d);    /* past it */
    CHECK_EQ(d, 0xFF9Fu);
    CHECK_EQ(t, 0xBF7Du);
}

TEST_CASE(an_empty_card_is_an_empty_archive)
{
    fake_fs_t fs;
    fake_init(&fs);
    CHECK_EQ(write_zip(&fs, "empty", 4096u), 22u);
    fake_free(&fs);
}

TEST_CASE(nested_folders_utf8_spaces_and_empty_files_are_kept)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "README.TXT", false, 1234u, 1791961199);
    fake_add(&fs, "empty.csv", false, 0u, 1791961199);
    fake_add(&fs, "logs", true, 0u, 1760000000);
    fake_add(&fs, "logs/run 1.csv", false, 70000u, 1760000001);
    fake_add(&fs, "logs/M\xc3\xbcller \xe2\x82\xac.csv", false, 4096u,
             1760000002);
    fake_add(&fs, "logs/2026", true, 0u, 1760000003);
    fake_add(&fs, "logs/2026/\xf0\x9f\x93\x81 deep", true, 0u, 1760000004);
    fake_add(&fs, "logs/2026/\xf0\x9f\x93\x81 deep/x.bin", false, 8192u,
             1760000005);
    fake_add(&fs, "logs/2026/\xf0\x9f\x93\x81 deep/old.bin", false, 3u,
             100);                               /* before 1980 */
    fake_add(&fs, "art", true, 0u, 1760000006);  /* an empty folder */
    /* Pieces of 1 byte, of 7, of 4096 and of 8192 give the same archive. */
    const uint64_t a = write_zip(&fs, "nested_1", 1u);
    CHECK_EQ(write_zip(&fs, "nested_7", 7u), a);
    CHECK_EQ(write_zip(&fs, "nested", 4096u), a);
    CHECK_EQ(write_zip(&fs, "nested_8192", 8192u), a);
    fake_free(&fs);
}

TEST_CASE(the_length_is_known_before_a_byte_is_read)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "a", false, 10u, 0);
    fake_add(&fs, "d", true, 0u, 0);
    fake_add(&fs, "d/bb", false, 0u, 0);
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    CHECK_EQ(plan.entries, 3u);
    CHECK_EQ(plan.files, 2u);
    CHECK_EQ(plan.file_bytes, 10u);
    /* a: 30+1+10+16, "d/": 30+2+16, "d/bb": 30+4+16; central 46+1, 46+2,
     * 46+4; end 22. */
    CHECK_EQ(plan.cd_offset, 57u + 48u + 50u);
    CHECK_EQ(plan.bytes, 155u + 47u + 48u + 50u + 22u);
    CHECK_EQ(fs.readdirs > 0, true);
    fake_free(&fs);
}

TEST_CASE(entries_at_the_16_bit_count_switch_to_zip64)
{
    CHECK(!wa_zip_end64(65534u, 0u, 0u));
    CHECK(wa_zip_end64(65535u, 0u, 0u));
    CHECK(wa_zip_end64(65536u, 0u, 0u));
    CHECK_EQ(wa_zip_end_bytes(65534u, 0u, 0u), 22u);
    CHECK_EQ(wa_zip_end_bytes(65535u, 0u, 0u), 98u);
    const unsigned counts[] = { 65534u, 65535u, 65536u };
    const char *names[] = { "count_65534", "count_65535", "count_65536" };
    for (int k = 0; k < 3; ++k) {
        fake_fs_t fs;
        fake_init(&fs);
        char name[16];
        for (unsigned i = 0; i < counts[k]; ++i) {
            snprintf(name, sizeof(name), "%05u", i);
            fake_add_in(&fs, 0, name, false, (i == 7u) ? 3u : 0u, 1760000000);
        }
        write_zip(&fs, names[k], 8192u);
        fake_free(&fs);
    }
}

/* A sparse archive: @p n entries of the sizes given, the data of each a
 * hole but for the last, which is 5 real bytes. */
static void write_sparse(const char *name, const uint64_t *sizes, int n)
{
    char path[512];
    out_path(path, sizeof(path), name, "zip");
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    out_path(path, sizeof(path), name, "txt");
    FILE *m = fopen(path, "w");
    CHECK(m != NULL);
    fprintf(m, "# sparse\n");
    static uint8_t piece[WA_ZIP_PIECE_MAX];
    wa_zip_len_t l;
    wa_zip_len_init(&l);
    uint64_t offsets[8];
    uint32_t crcs[8];
    char nm[8][16];
    for (int i = 0; i < n; ++i) {
        snprintf(nm[i], sizeof(nm[i]), "f%d.bin", i);
        const size_t nl = strlen(nm[i]);
        offsets[i] = (uint64_t)ftello(f);
        CHECK_EQ(offsets[i], l.at);
        size_t k = wa_zip_local(piece, nm[i], nl, sizes[i], 1760000000, false);
        CHECK_EQ(k, wa_zip_local_bytes(nl, sizes[i]));
        fwrite(piece, 1u, k, f);
        if (i == n - 1) {
            CHECK_EQ(sizes[i], 5u);
            fwrite("hello", 1u, 5u, f);
            crcs[i] = wa_crc32(0u, "hello", 5u);
        } else {
            CHECK(fseeko(f, (off_t)sizes[i], SEEK_CUR) == 0);
            crcs[i] = 0u;     /* a hole: its CRC is not read */
        }
        k = wa_zip_desc(piece, crcs[i], sizes[i]);
        CHECK_EQ(k, wa_zip_desc_bytes(sizes[i]));
        fwrite(piece, 1u, k, f);
        wa_zip_len_add(&l, nl, sizes[i]);
        fprintf(m, "F\t%s\t%llu\t%u\t1760000000\t%llu\n", nm[i],
                (unsigned long long)sizes[i], crcs[i],
                (unsigned long long)offsets[i]);
    }
    const uint64_t cd_at = (uint64_t)ftello(f);
    uint64_t cd = 0u;
    for (int i = 0; i < n; ++i) {
        const size_t nl = strlen(nm[i]);
        const size_t k = wa_zip_central(piece, nm[i], nl, sizes[i], crcs[i],
                                        offsets[i], 1760000000, false);
        CHECK_EQ(k, wa_zip_central_bytes(nl, sizes[i], offsets[i]));
        fwrite(piece, 1u, k, f);
        cd += k;
    }
    CHECK_EQ(cd, l.cd);
    const size_t k = wa_zip_end(piece, (uint64_t)n, cd, cd_at);
    CHECK_EQ(k, wa_zip_end_bytes((uint64_t)n, cd, cd_at));
    fwrite(piece, 1u, k, f);
    CHECK_EQ((uint64_t)ftello(f), wa_zip_len_total(&l));
    fclose(f);
    fclose(m);
}

TEST_CASE(sizes_at_the_32_bit_limit_switch_to_zip64)
{
    CHECK(!wa_zip_entry64(0xFFFFFFFEu));
    CHECK(wa_zip_entry64(0xFFFFFFFFu));          /* 4 GiB - 1 B: the mark */
    CHECK(wa_zip_entry64(0x100000000u));         /* 4 GiB */
    CHECK_EQ(wa_zip_local_bytes(5u, 0xFFFFFFFEu), 35u);
    CHECK_EQ(wa_zip_local_bytes(5u, 0xFFFFFFFFu), 55u);
    CHECK_EQ(wa_zip_desc_bytes(0xFFFFFFFEu), 16u);
    CHECK_EQ(wa_zip_desc_bytes(0xFFFFFFFFu), 24u);
    CHECK_EQ(wa_zip_central_bytes(5u, 1u, 0xFFFFFFFEu), 51u);
    CHECK_EQ(wa_zip_central_bytes(5u, 1u, 0xFFFFFFFFu), 79u);
    CHECK_EQ(wa_zip_central_bytes(5u, 0x100000000u, 0xFFFFFFFFu), 79u);
    CHECK(!wa_zip_end64(1u, 0xFFFFFFFEu, 0u));
    CHECK(wa_zip_end64(1u, 0xFFFFFFFFu, 0u));
    CHECK(wa_zip_end64(1u, 0u, 0xFFFFFFFFu));

    /* A file of 4 GiB - 2 B, of 4 GiB - 1 B and of 4 GiB, each followed
     * by one of 5 bytes whose header then sits past 4 GiB. */
    const uint64_t below[] = { 0xFFFFFFFEu, 5u };
    const uint64_t at[]    = { 0xFFFFFFFFu, 5u };
    const uint64_t over[]  = { 0x100000000u, 5u };
    write_sparse("big_4g_minus_2", below, 2);
    write_sparse("big_4g_minus_1", at, 2);
    write_sparse("big_4g", over, 2);

    /* The archive's length by the plan of a card with those files, which
     * reads no data. */
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "f0.bin", false, 0x100000000u, 1760000000);
    fake_add(&fs, "f1.bin", false, 5u, 1760000000);
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    wa_zip_len_t l;
    wa_zip_len_init(&l);
    wa_zip_len_add(&l, 6u, 0x100000000u);
    wa_zip_len_add(&l, 6u, 5u);
    CHECK_EQ(plan.bytes, wa_zip_len_total(&l));
    CHECK_EQ(plan.cd_offset, 0x100000000u + 56u + 24u + 41u + 16u);
    fake_free(&fs);
}

/* Stream the card and break it part way as @p how says; the archive must
 * end failed and short of its announced length, with nothing left open. */
static void zip_breaks(int how)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "a.csv", false, 20000u, 0);
    const int d = fake_add(&fs, "d", true, 0u, 0);
    const int b = fake_add(&fs, "d/b.csv", false, 20000u, 0);
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_slot_t slot[8];
    wa_zip_t *z = malloc(sizeof(*z));
    REQUIRE_ALLOC(z);
    CHECK_EQ(wa_zip_begin(z, &w, slot, 8u), WA_ZIP_OK);
    const wa_zip_plan_t plan = z->plan;
    if (how == 0) {
        fs.n[b].fail_at = 7000;          /* a read error in the second file */
    }
    if (how == 3) {
        fs.n[b].end_at = 15000;          /* its data ends short of its size */
    }
    uint64_t total = 0u;
    int32_t n;
    while ((n = wa_zip_next(z, g_buf, 4096u)) > 0) {
        total += (uint64_t)n;
        if (how == 1 && total >= 10000u) {
            fs.pulled = true;            /* pulled in the first file */
        }
        if (how == 2 && total >= 30000u) {
            fs.pulled = true;            /* pulled while in a folder */
        }
        if (how == 4 && total == 4096u) {
            fs.n[b].size = 19999u;       /* a file shrank */
        }
        if (how == 5 && total == 4096u) {
            fs.n[d].first = fs.n[d].last = -1;   /* d/b.csv removed */
        }
    }
    CHECK_EQ(n, -1);
    CHECK_EQ(z->err, WA_ZIP_READ);
    CHECK(total < plan.bytes);
    if (how == 0) {
        /* The bytes before the failure go out: a.csv 35 + 20000 + 16,
         * "d/" 32 + 16, d/b.csv's header 37 and its first 7000 bytes. */
        CHECK_EQ(total, 20051u + 48u + 37u + 7000u);
    }
    CHECK_EQ(wa_zip_next(z, g_buf, 4096u), -1);
    CHECK_EQ(fs.files_open, 0);
    CHECK_EQ(fs.dirs_open, 0);
    free(z);
    fake_free(&fs);
}

TEST_CASE(a_read_error_or_a_pulled_card_ends_the_archive_failed)
{
    for (int how = 0; how < 6; ++how) {
        zip_breaks(how);
    }
    /* No card at all: said before a byte goes. */
    fake_fs_t fs;
    fake_init(&fs);
    fs.pulled = true;
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_NO_CARD);
    wa_zip_slot_t slot[1];
    wa_zip_t *z = malloc(sizeof(*z));
    REQUIRE_ALLOC(z);
    CHECK_EQ(wa_zip_begin(z, &w, slot, 1u), WA_ZIP_NO_CARD);
    CHECK_EQ(wa_zip_next(z, g_buf, 4096u), -1);
    free(z);
    fake_free(&fs);
}

TEST_CASE(too_few_slots_are_refused_before_a_byte)
{
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "a", false, 1u, 0);
    fake_add(&fs, "b", false, 1u, 0);
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_slot_t slot[2];
    wa_zip_t *z = malloc(sizeof(*z));
    REQUIRE_ALLOC(z);
    CHECK_EQ(wa_zip_begin(z, &w, slot, 1u), WA_ZIP_MEMORY);
    CHECK_EQ(fs.dirs_open, 0);
    CHECK_EQ(wa_zip_next(z, g_buf, 4096u), -1);
    CHECK_EQ(wa_zip_begin(z, &w, NULL, 2u), WA_ZIP_MEMORY);
    CHECK_EQ(fs.dirs_open, 0);
    CHECK_EQ(wa_zip_begin(z, &w, slot, 2u), WA_ZIP_OK);
    CHECK_EQ(z->plan.entries, 2u);
    wa_zip_end_early(z);
    CHECK_EQ(fs.dirs_open, 0);
    free(z);
    fake_free(&fs);
}

TEST_CASE(the_bench_tree_keeps_its_folders_in_the_archive)
{
    /* check_wa_zip.py holds bench_tree.zip to its manifest: every path,
     * BENCH/KST/CONF/Wing left.TXT and the empty BENCH/HITEC/CONF with
     * them. */
    fake_fs_t fs;
    fake_init(&fs);
    fake_bench_tree(&fs);
    const wa_fs_t w = fake_wa(&fs);
    wa_zip_plan_t plan;
    CHECK_EQ(wa_zip_plan(&w, &plan), WA_ZIP_OK);
    CHECK_EQ(plan.files, (uint64_t)FAKE_TREE_FILES);
    const uint64_t a = write_zip(&fs, "bench_tree", 4096u);
    CHECK_EQ(write_zip(&fs, "bench_tree_7", 7u), a);
    fake_free(&fs);
}

/* ------------------------------------------------- a card written to */

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) | (get16(p + 2) << 16);
}

/* Read "<name>.zip" back as an unzip program does, apart from the code
 * under test: the end record, each central header, the local header and
 * data descriptor it points to, the entries back to back from offset 0 up
 * to the central directory.  Each entry's CRC must be the CRC of its data
 * as it lies in the archive, its sizes the same in all three places.
 * Returns the entries; the size and CRC of the entry named @p want go to
 * @p size and @p crc. */
static uint64_t read_back(const char *name, const char *want, uint64_t *size,
                          uint32_t *crc)
{
    char path[512];
    out_path(path, sizeof(path), name, "zip");
    FILE *f = fopen(path, "rb");
    REQUIRE_ALLOC(f);
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    *size = 0u;
    *crc = 0u;
    if (len < 22) {
        T_FAIL("%s: %ld bytes, shorter than an end record", name, len);
        fclose(f);
        return 0u;
    }
    uint8_t *a = malloc((size_t)len);
    REQUIRE_ALLOC(a);
    CHECK_EQ(fread(a, 1u, (size_t)len, f), (size_t)len);
    fclose(f);
    const uint8_t *e = a + len - 22;
    const uint32_t n = get16(e + 10), cd_bytes = get32(e + 12);
    const uint32_t cd_at = get32(e + 16);
    if (get32(e) != 0x06054b50u
        || (uint64_t)cd_at + cd_bytes + 22u != (uint64_t)len) {
        T_FAIL("%s: no end record that closes the archive", name);
        free(a);
        return 0u;
    }
    uint32_t at = cd_at, next_local = 0u;
    for (uint32_t i = 0; i < n && at + 46u <= cd_at + cd_bytes; ++i) {
        const uint8_t *c = a + at;
        CHECK_EQ(get32(c), 0x02014b50u);
        const uint32_t cs = get32(c + 16), csize = get32(c + 20);
        const uint32_t usize = get32(c + 24), nl = get16(c + 28);
        const uint32_t lho = get32(c + 42);
        CHECK_EQ(csize, usize);
        CHECK_EQ(lho, next_local);
        if (lho != next_local || (uint64_t)lho + 30u + nl > cd_at) {
            break;
        }
        const uint8_t *l = a + lho;
        CHECK_EQ(get32(l), 0x04034b50u);
        CHECK_EQ(get16(l + 26), nl);
        CHECK(memcmp(l + 30, c + 46, nl) == 0);
        const uint64_t data = (uint64_t)lho + 30u + nl + get16(l + 28);
        CHECK(data + usize + 16u <= cd_at);
        if (data + usize + 16u > cd_at) {
            break;
        }
        CHECK_EQ(wa_crc32(0u, a + data, usize), cs);
        const uint8_t *dd = a + data + usize;
        CHECK_EQ(get32(dd), 0x08074b50u);
        CHECK_EQ(get32(dd + 4), cs);
        CHECK_EQ(get32(dd + 8), usize);
        CHECK_EQ(get32(dd + 12), usize);
        if (nl == strlen(want) && memcmp(c + 46, want, nl) == 0) {
            *size = usize;
            *crc = cs;
        }
        next_local = (uint32_t)(data + usize + 16u);
        at += 46u + nl + get16(c + 30) + get16(c + 32);
    }
    CHECK_EQ(next_local, cd_at);
    CHECK_EQ(at, cd_at + cd_bytes);
    free(a);
    return n;
}

/* The card a log is written to: a.csv, RCBENCH.LOG, d/, d/b.csv, entries
 * 0 to 3 in walk order.  The growing file is g_grow, entry g_grow_index. */
static int      g_grow;
static uint64_t g_grow_index;
static int      g_grow_when;
static bool     g_grown;

/* Grow g_grow by 37 bytes: once on the first piece, before the second walk
 * comes to it (when 0); once when the second walk has taken it, so the
 * third walk finds it larger (when 1); after every piece of the download
 * (when 2). */
static void grow_hook(fake_fs_t *fs, const wa_zip_t *z, uint64_t total)
{
    (void)total;
    const bool taken = z->index > g_grow_index
                       || (z->index == g_grow_index && z->in_file);
    if (g_grow_when == 2 || (g_grow_when == 0 && !g_grown)
        || (g_grow_when == 1 && !g_grown && taken && z->cd_at == 0u)) {
        fs->n[g_grow].size += 37u;
        g_grown = true;
    }
}

TEST_CASE(a_file_that_grows_is_sent_at_its_size_when_the_archive_began)
{
    static const char *const names[2][3] = {
        { "grow_mid_before", "grow_mid_taken", "grow_mid_always" },
        { "grow_last_before", "grow_last_taken", "grow_last_always" },
    };
    for (int last = 0; last < 2; ++last) {
        for (int when = 0; when < 3; ++when) {
            fake_fs_t fs;
            fake_init(&fs);
            fake_add(&fs, "a.csv", false, 20000u, 1760000000);
            const int log = fake_add(&fs, "RCBENCH.LOG", false, 5000u,
                                     1760000001);
            fake_add(&fs, "d", true, 0u, 1760000002);
            const int b = fake_add(&fs, "d/b.csv", false, 9000u, 1760000003);
            g_grow = last ? b : log;
            g_grow_index = last ? 3u : 1u;
            g_grow_when = when;
            g_grown = false;
            const uint64_t planned = fs.n[g_grow].size;
            const uint32_t planned_crc = prefix_crc(g_grow, planned);
            write_zip_with(&fs, names[last][when], 4096u, grow_hook);
            CHECK(fs.n[g_grow].size > planned);
            uint64_t size;
            uint32_t crc;
            const char *want = last ? "d/b.csv" : "RCBENCH.LOG";
            CHECK_EQ(read_back(names[last][when], want, &size, &crc), 4u);
            CHECK_EQ(size, planned);
            CHECK_EQ(crc, planned_crc);
            fake_free(&fs);
        }
    }
}

/* Move @p node, the last entry of folder @p parent, to its front: where a
 * FAT (file allocation table) folder puts a new entry in a slot a removed
 * one freed. */
static void to_front(fake_fs_t *fs, int parent, int node)
{
    int prev = fs->n[parent].first;
    while (fs->n[prev].next != node) {
        prev = fs->n[prev].next;
    }
    fs->n[prev].next = -1;
    fs->n[parent].last = prev;
    fs->n[node].next = fs->n[parent].first;
    fs->n[parent].first = node;
}

static int  g_add_when;
static bool g_added;

/* Add an entry once: a file in the root during the second walk (when 0), a
 * folder with a file in it during the second walk (when 1), a file in d
 * during the third walk (when 2), a file at the front of the root while
 * the second walk is in d, so the root's place is counted one short when
 * the walk comes back to it (when 3). */
static void add_hook(fake_fs_t *fs, const wa_zip_t *z, uint64_t total)
{
    if (g_added) {
        return;
    }
    if (g_add_when == 0) {
        fake_add(fs, "NEW001.CSV", false, 300u, 1760000009);
    } else if (g_add_when == 1) {
        fake_add(fs, "LOGS", true, 0u, 1760000009);
        fake_add(fs, "LOGS/NEW001.CSV", false, 300u, 1760000009);
    } else if (g_add_when == 2) {
        if (total < z->plan.cd_offset) {
            return;
        }
        fake_add(fs, "d/NEW001.CSV", false, 300u, 1760000009);
    } else {
        if (!(z->index == 3u && z->in_file)) {
            return;
        }
        to_front(fs, 0, fake_add(fs, "NEW001.CSV", false, 300u, 1760000009));
    }
    g_added = true;
}

TEST_CASE(an_entry_added_during_the_download_is_left_out)
{
    static const char *const names[4] = {
        "add_file", "add_folder", "add_in_central", "add_in_front",
    };
    for (int when = 0; when < 4; ++when) {
        fake_fs_t fs;
        fake_init(&fs);
        fake_add(&fs, "a.csv", false, 20000u, 1760000000);
        fake_add(&fs, "RCBENCH.LOG", false, 5000u, 1760000001);
        fake_add(&fs, "d", true, 0u, 1760000002);
        const int b = fake_add(&fs, "d/b.csv", false, 9000u, 1760000003);
        g_add_when = when;
        g_added = false;
        /* Pieces of 64 bytes, so the third walk is still before d when
         * the hook runs on its first central header. */
        write_zip_with(&fs, names[when], (when == 2) ? 64u : 4096u, add_hook);
        CHECK(g_added);
        uint64_t size;
        uint32_t crc;
        CHECK_EQ(read_back(names[when], "d/b.csv", &size, &crc), 4u);
        CHECK_EQ(size, 9000u);
        CHECK_EQ(crc, prefix_crc(b, 9000u));
        fake_free(&fs);
    }
}

int main(void)
{
    setup_dir();
    RUN(crc32_and_dos_times_are_as_zip_takes_them);
    RUN(an_empty_card_is_an_empty_archive);
    RUN(nested_folders_utf8_spaces_and_empty_files_are_kept);
    RUN(the_bench_tree_keeps_its_folders_in_the_archive);
    RUN(the_length_is_known_before_a_byte_is_read);
    RUN(entries_at_the_16_bit_count_switch_to_zip64);
    RUN(sizes_at_the_32_bit_limit_switch_to_zip64);
    RUN(a_read_error_or_a_pulled_card_ends_the_archive_failed);
    RUN(too_few_slots_are_refused_before_a_byte);
    RUN(a_file_that_grows_is_sent_at_its_size_when_the_archive_began);
    RUN(an_entry_added_during_the_download_is_left_out);
    return test_summary("wa_zip");
}
