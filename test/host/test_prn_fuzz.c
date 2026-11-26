/*
 * Random printer files and random printers for the printer engine, with
 * what a caller relies on checked on every case.
 *
 * The cases are drawn from a 32-bit xorshift generator.  Each case seeds
 * it from a fixed seed and its own number, so the suite is the same every
 * time and a failing case names the two numbers it runs again from
 * (one_parse() and one_job() take them).
 *
 * The reader, on the T50M Pro file and the suites' two fixtures: the file
 * cut at every byte, every line removed, doubled and swapped with the
 * next, and 3 seeds x FUZZ_PARSES of each file changed 1 to 4 times (a
 * byte flipped, inserted from a pool of 0x00, 0x7F, 0x80, 0xFF, CR, TAB
 * and the format's punctuation, or deleted; a line removed, doubled,
 * swapped, indented or unindented; a token replaced by a boundary number
 * or a word of the format; a line padded to 120 or 121 bytes).  Checked:
 * a file taken passes prn_spec_check(); a refused one has a code of 1 to
 * 30 and a line inside the file, 0 only for E01 and E05, and the spec is
 * zeroed; the same bytes give the same result twice.
 *
 * The job, on the three files and on up to FUZZ_POOL changed files the
 * reader took: 3 seeds x FUZZ_JOBS jobs, QUERY and PRINT, with a random
 * stream, the clock started anywhere in 2^32ms and moved 1 to 40ms a
 * step, against a printer that answers writes late, refuses or loses
 * them, sends random bytes, frames with the file's magic and keys and
 * random fields, drops and restores the link and is aborted.  Checked:
 * every write comes from the job's own buffers and is at most the
 * command room or the file's fragment, which is at most mtu_min - 3; no
 * write while one is in flight, while the link is down or after the end;
 * the abort template at most once; every job ends within PRN_JOB_MS, plus
 * 2 x answer_ms and the data gap for one that sends the abort.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "prn_fixture.h"
#include "prn_job.h"

#define FUZZ_PARSES 2000u
#define FUZZ_JOBS   500u
#define FUZZ_POOL   200u

static const uint32_t k_seeds[] = { 12345u, 0x2545F491u, 0x9E3779B9u };

/* ------------------------------------------------------------ the draws */

static uint32_t rs;

static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static uint32_t upto(uint32_t n)
{
    return (n == 0u) ? 0u : rnd() % n;
}

/* The generator for case @p id of @p seed: never 0, which xorshift keeps. */
static void seed_case(uint32_t seed, unsigned id)
{
    rs = seed ^ ((uint32_t)id * 2654435761u);
    if (rs == 0u) {
        rs = 1u;
    }
}

static uint32_t g_seed;
static unsigned g_id;
static const char *g_what = "";

#define FCHECK(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("%s seed 0x%08lX case %u: expected %s", g_what,           \
                   (unsigned long)g_seed, g_id, #cond);                       \
            return false;                                                     \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------- the files */

static const char *const k_files[3] = {
    PRN_T50M_PATH, "fixtures/printers/rows-test.prn",
    "fixtures/printers/magic-test.prn",
};
static const char *const k_names[3] = {
    "supvan-t50m-pro.prn", "rows-test.prn", "magic-test.prn",
};
static char s_base[3][PRN_FILE_MAX + 1u];
static size_t s_base_n[3];

static const uint8_t k_bytes[] = { 0x00u, 0x09u, 0x0Au, 0x0Du, 0x20u,
                                   0x23u, 0x2Du, 0x3Au, 0x3Du, 0x7Bu,
                                   0x7Du, 0x7Eu, 0x7Fu, 0x80u, 0xFFu };
static const char *const k_tokens[] = {
    "0", "1", "7", "8", "15", "16", "23", "127", "128", "255", "256",
    "1024", "1025", "4096", "65535", "65536", "4294967295", "4294967296",
    "0x", "0xFF", "-1", "-129", "x", "end", "step", "send", "data", "wait",
    "relink", "verdict", "within", "expect", "poll", "until", "and", "else",
    "check", "save", "c0", "c0-1", "lay_w", "{data}", "{a:u8}",
    "{len:u8:0:-1}", "{ck:sum8:0:-1}", "{ck:svpage:le16}", "{idx:u8}",
    "{bogus:u8}", "SILENT", "ROLL", "set", "clear", "=", "!=", "busy",
    "left", "FF", "7E5A", "cmd", "done", "ready", "start",
};

/** @brief The case's file. */
static char s_txt[2u * PRN_FILE_MAX + 256u];
static size_t s_n;

/** @brief The start and length of line @p k of s_txt, or false. */
static bool line_at(unsigned k, size_t *from, size_t *len)
{
    size_t at = 0u;
    for (unsigned i = 0; i < k; ++i) {
        const char *nl = memchr(s_txt + at, '\n', s_n - at);
        if (nl == NULL) {
            return false;
        }
        at = (size_t)(nl - s_txt) + 1u;
    }
    const char *nl = memchr(s_txt + at, '\n', s_n - at);
    *from = at;
    *len = (nl != NULL) ? (size_t)(nl - (s_txt + at)) : s_n - at;
    return true;
}

static unsigned line_count(void)
{
    unsigned n = 1u;
    for (size_t i = 0; i < s_n; ++i) {
        n += (s_txt[i] == '\n') ? 1u : 0u;
    }
    return n;
}

/** @brief Replace @p len bytes at @p at by @p with. */
static void splice(size_t at, size_t len, const char *with, size_t wn)
{
    if (s_n - len + wn > sizeof s_txt) {
        return;
    }
    memmove(s_txt + at + wn, s_txt + at + len, s_n - at - len);
    memcpy(s_txt + at, with, wn);
    s_n = s_n - len + wn;
}

static void change(void)
{
    size_t from = 0u;
    size_t len = 0u;
    const unsigned lines = line_count();
    char buf[PRN_LINE_MAX + 8u];
    switch (upto(9u)) {
    case 0:
        if (s_n > 0u) {
            s_txt[upto((uint32_t)s_n)] ^= (char)(1u << upto(8u));
        }
        break;
    case 1: {
        const char b = (char)k_bytes[upto(sizeof k_bytes)];
        splice(upto((uint32_t)s_n + 1u), 0u, &b, 1u);
        break;
    }
    case 2:
        if (s_n > 0u) {
            splice(upto((uint32_t)s_n), 1u, "", 0u);
        }
        break;
    case 3:
        if (line_at(upto(lines), &from, &len)) {
            splice(from, len + ((from + len < s_n) ? 1u : 0u), "", 0u);
        }
        break;
    case 4:
        if (line_at(upto(lines), &from, &len) && len < PRN_LINE_MAX) {
            memcpy(buf, s_txt + from, len);
            buf[len] = '\n';
            splice(from, 0u, buf, len + 1u);
        }
        break;
    case 5:
        if (line_at(upto(lines), &from, &len)) {
            splice(from, 0u, (upto(2u) != 0u) ? "  " : "\t",
                   (upto(2u) != 0u) ? 2u : 1u);
        }
        break;
    case 6:
        if (line_at(upto(lines), &from, &len)) {
            while (len > 0u && (s_txt[from] == ' ' || s_txt[from] == '\t')) {
                splice(from, 1u, "", 0u);
                len--;
            }
        }
        break;
    case 7:
        if (line_at(upto(lines), &from, &len) && len > 0u) {
            /* a token: from a space to the next */
            size_t a = from + upto((uint32_t)len);
            while (a > from && s_txt[a - 1u] != ' ') {
                a--;
            }
            size_t b = a;
            while (b < from + len && s_txt[b] != ' ') {
                b++;
            }
            const char *t = k_tokens[upto(sizeof k_tokens
                                          / sizeof k_tokens[0])];
            splice(a, b - a, t, strlen(t));
        }
        break;
    default:
        if (line_at(upto(lines), &from, &len) && len < 110u) {
            const size_t w = 120u + upto(2u);
            memset(buf, 'x', sizeof buf);
            buf[0] = ' ';
            buf[1] = '#';
            splice(from + len, 0u, buf, w - len);
        }
        break;
    }
}

/* --------------------------------------------------------- the reader */

static prn_spec_t s_a;
static prn_spec_t s_b;
static prn_spec_t s_zero;
static prn_spec_t s_pool[FUZZ_POOL];
static unsigned s_npool;

/** @brief Read s_txt twice and hold the results to the rules above. */
static bool check_read(const char *name)
{
    prn_perr_t e1;
    prn_perr_t e2;
    memset(&s_a, 0x5A, sizeof s_a);
    const bool ok = prn_parse(s_txt, s_n, name, &s_a, &e1);
    const bool ok2 = prn_parse(s_txt, s_n, name, &s_b, &e2);
    FCHECK(ok == ok2 && e1.code == e2.code && e1.line == e2.line);
    FCHECK(memcmp(&s_a, &s_b, sizeof s_a) == 0);
    if (ok) {
        FCHECK(e1.code == 0u);
        FCHECK(prn_spec_check(&s_a) == 0u);
        if (s_npool < FUZZ_POOL) {
            s_pool[s_npool++] = s_a;
        }
        return true;
    }
    FCHECK(e1.code >= 1u && e1.code <= 30u);
    FCHECK(e1.line <= PRN_LINES_MAX + 1u);
    FCHECK(e1.line != 0u || e1.code == 1u || e1.code == 5u);
    FCHECK(memcmp(&s_a, &s_zero, sizeof s_a) == 0);
    return true;
}

static bool one_parse(uint32_t seed, unsigned id)
{
    g_seed = seed;
    g_id = id;
    g_what = "parse";
    seed_case(seed, id);
    const unsigned f = upto(3u);
    memcpy(s_txt, s_base[f], s_base_n[f]);
    s_n = s_base_n[f];
    const unsigned changes = 1u + upto(4u);
    for (unsigned i = 0; i < changes; ++i) {
        change();
    }
    return check_read(k_names[f]);
}

TEST_CASE(every_cut_and_every_line_removed_doubled_and_swapped)
{
    unsigned done = 0u;
    g_seed = 0u;
    g_what = "edit";
    for (unsigned f = 0; f < 3u; ++f) {
        for (size_t cut = 0; cut <= s_base_n[f]; ++cut) {
            memcpy(s_txt, s_base[f], cut);
            s_n = cut;
            g_id = (unsigned)cut;
            done += check_read(k_names[f]) ? 1u : 0u;
        }
        memcpy(s_txt, s_base[f], s_base_n[f]);
        s_n = s_base_n[f];
        const unsigned lines = line_count();
        for (unsigned k = 0; k < lines; ++k) {
            for (unsigned how = 0; how < 3u; ++how) {
                memcpy(s_txt, s_base[f], s_base_n[f]);
                s_n = s_base_n[f];
                size_t from;
                size_t len;
                size_t from2;
                size_t len2;
                if (!line_at(k, &from, &len)) {
                    continue;
                }
                char buf[2u * PRN_LINE_MAX + 4u];
                if (how == 0u) {
                    splice(from, len + ((from + len < s_n) ? 1u : 0u), "", 0u);
                } else if (how == 1u) {
                    memcpy(buf, s_txt + from, len);
                    buf[len] = '\n';
                    splice(from, 0u, buf, len + 1u);
                } else if (line_at(k + 1u, &from2, &len2)) {
                    memcpy(buf, s_txt + from2, len2);
                    buf[len2] = '\n';
                    memcpy(buf + len2 + 1u, s_txt + from, len);
                    splice(from, len + 1u + len2, buf, len2 + 1u + len);
                }
                g_id = k * 3u + how;
                done += check_read(k_names[f]) ? 1u : 0u;
            }
        }
    }
    CHECK(done > 3000u);
}

TEST_CASE(random_files_keep_the_rules_a_caller_relies_on)
{
    unsigned ok = 0u;
    for (size_t s = 0; s < sizeof k_seeds / sizeof k_seeds[0]; ++s) {
        for (unsigned id = 0; id < 3u * FUZZ_PARSES; ++id) {
            ok += one_parse(k_seeds[s], id) ? 1u : 0u;
        }
    }
    CHECK_EQ(ok, 3u * 3u * FUZZ_PARSES);
    printf("      %u changed files taken into the job pool\n", s_npool);
}

/* ------------------------------------------------------------ the job */

static prn_job_t s_j;
/** @brief Jobs by their end, and the data frames they sent. */
static unsigned s_ends[PRN_ERR_COUNT];
static unsigned s_frames;
/** @brief The job's stream. */
static prn_stream_t s_z;
/** @brief The random printer's field values, kept for a job. */
static uint64_t s_val[PRN_F_NAMES];
static uint8_t s_stream[4096];

/** @brief A notification a random printer sends for @p s. */
static size_t random_frame(const prn_spec_t *s, uint8_t *out)
{
    const unsigned pick = upto(32u);
    size_t n = 1u + upto(PRN_RX_MAX);
    for (size_t i = 0; i < n; ++i) {
        out[i] = (uint8_t)rnd();
    }
    if (pick == 0u) {
        return n; /* noise */
    }
    n = s->rx_min + upto((uint32_t)(s->rx_max - s->rx_min) + 1u);
    /* mostly the key the job waits for, at least that answer's length */
    out[s->key_off] = s_j.await_key;
    for (unsigned a = 0; a < s->nanswer; ++a) {
        if (s->answer_key[a] == s_j.await_key || upto(8u) == 0u) {
            n = s->answer_min[a]
                + upto((uint32_t)(s->rx_max - s->answer_min[a]) + 1u);
            out[s->key_off] = s->answer_key[a];
            break;
        }
    }
    if (s->join == PRN_J_MAGIC_LEN) {
        memcpy(out, s->magic, s->magic_len);
        const uint32_t field = (uint32_t)n - s->len_add;
        uint8_t *l = out + s->len_off;
        if (s->len_enc == PRN_E_U8) {
            l[0] = (uint8_t)field;
        } else if (s->len_enc == PRN_E_LE16) {
            l[0] = (uint8_t)field;
            l[1] = (uint8_t)(field >> 8);
        } else {
            l[0] = (uint8_t)(field >> 8);
            l[1] = (uint8_t)field;
        }
    }
    if (s->tail_len != 0u && n >= s->tail_len) {
        memcpy(out + n - s->tail_len, s->tail, s->tail_len);
    }
    /* mostly quiet flags and the roll the label was laid out for, so jobs
     * get past their first steps */
    for (unsigned i = 0; i < s->nfield && pick != 1u; ++i) {
        const prn_field_t *f = &s->field[i];
        if (f->off >= n) {
            continue;
        }
        if (f->kind == PRN_FK_BIT) {
            bool error = false;
            for (unsigned e = 0; e < s->nerror; ++e) {
                error = error || s->error_field[e] == f->name;
            }
            out[f->off] &= (uint8_t)~(1u << f->n);
            if (!error && upto(2u) == 0u) {
                out[f->off] |= (uint8_t)(1u << f->n);
            }
            continue;
        }
        static const uint8_t k_w[] = { 1u, 1u, 2u, 2u, 4u, 4u, 0u };
        const unsigned w = (f->kind == PRN_FK_BYTES) ? f->n : k_w[f->kind];
        const bool be = f->kind == PRN_FK_BE16 || f->kind == PRN_FK_BE32;
        for (unsigned k = 0; k < w && f->off + k < n; ++k) {
            out[f->off + (be ? w - 1u - k : k)] =
                (uint8_t)(s_val[f->name] >> (8u * k));
        }
    }
    return n;
}

static bool one_job(uint32_t seed, unsigned id)
{
    g_seed = seed;
    g_id = id;
    g_what = "job";
    seed_case(seed, id);
    const prn_spec_t *s = &s_pool[upto(s_npool)];
    const bool print = upto(4u) != 0u;
    prn_stream_t z;
    memset(&z, 0, sizeof z);
    const uint8_t *lim = s->roll_lim;
    for (size_t i = 0; i < sizeof s_stream; ++i) {
        s_stream[i] = (uint8_t)rnd();
    }
    z.data = s_stream;
    z.len = 1u + upto(3000u);
    if (s->chunk_size == 0u && s->line_bytes != 0u) {
        z.len = s->line_bytes * (1u + upto(3000u / s->line_bytes));
    }
    z.pages = upto(6u);
    z.lines = upto(500u);
    z.w_mm = (uint8_t)(lim[0] + upto((uint32_t)(lim[1] - lim[0]) + 2u));
    z.h_mm = (uint8_t)(lim[2] + upto((uint32_t)(lim[3] - lim[2]) + 2u));
    s_z = z;
    for (unsigned i = 0; i < PRN_F_NAMES; ++i) {
        s_val[i] = ((uint64_t)rnd() << 32) | rnd();
    }
    s_val[PRN_F_ROLL_W] = z.w_mm;
    s_val[PRN_F_ROLL_H] = z.h_mm;
    s_val[PRN_F_LEFT] = upto(300u);
    uint32_t now = rnd();
    const uint32_t t0 = now;
    if (!prn_job_start(&s_j, s, print ? PRN_JOB_PRINT : PRN_JOB_QUERY,
                       print ? &z : NULL, now)) {
        return true; /* a stream the file cannot carry */
    }
    bool link = true;
    bool owed = false;
    bool owed_ok = true;
    uint32_t owed_at = 0u;
    unsigned aborts = 0u;
    const uint32_t cap = (s->frag > PRN_CMD_MAX) ? s->frag : PRN_CMD_MAX;
    FCHECK(s->frag + 3u <= s->mtu_min);
    while (!prn_job_ended(&s_j)) {
        FCHECK((uint32_t)(now - t0)
               <= PRN_JOB_MS + 2u * (uint32_t)s->answer_ms + s->gap_ms);
        prn_job_tick(&s_j, now);
        if (owed && link && (uint32_t)(now - owed_at) < 0x80000000u) {
            owed = false;
            prn_job_written(&s_j, owed_ok, now);
        }
        const bool flying = s_j.in_flight;
        const uint8_t *p = NULL;
        uint16_t n = 0u;
        prn_write_t kind;
        if (prn_job_tx(&s_j, now, &p, &n, &kind)) {
            FCHECK(link && !flying);
            FCHECK(n >= 1u && n <= cap);
            const bool in_cmd = p >= s_j.cmd && p + n <= s_j.cmd + PRN_CMD_MAX;
            const bool in_buf = p >= s_j.buf && p + n <= s_j.buf + PRN_FRAME_MAX;
            FCHECK(in_cmd || in_buf);
            FCHECK(!in_buf || n <= s->frag);
            aborts += (s_j.phase == PRN_PH_STOP) ? 1u : 0u;
            FCHECK(aborts <= 1u);
            const unsigned fate = upto(40u);
            if (fate != 0u) { /* 1 in 40 lost */
                owed = true;
                owed_ok = fate != 1u;
                owed_at = now + upto(30u);
            }
        }
        const unsigned ev = upto(100u);
        if (ev < 30u && link) {
            uint8_t f[PRN_RX_MAX + 1u];
            const size_t k = random_frame(s, f);
            const size_t cut = (upto(3u) == 0u) ? upto((uint32_t)k + 1u) : k;
            prn_job_rx(&s_j, f, cut, now);
            if (cut < k) {
                prn_job_rx(&s_j, f + cut, k - cut, now);
            }
        } else if (ev == 30u && upto(8u) == 0u) {
            link = !link;
            owed = false;
            prn_job_link(&s_j, link, now);
        } else if (ev == 31u && upto(10u) == 0u) {
            prn_job_abort(&s_j, now);
        }
        if (!link) {
            FCHECK(!prn_job_tx(&s_j, now, &p, &n, &kind));
        }
        now += 1u + upto(40u);
    }
    FCHECK(s_j.err < PRN_ERR_COUNT);
    s_ends[s_j.err]++;
    s_frames += s_j.frame;
    const uint8_t *p = NULL;
    uint16_t n = 0u;
    prn_write_t kind;
    FCHECK(!prn_job_tx(&s_j, now + 100000u, &p, &n, &kind));
    return true;
}

TEST_CASE(random_jobs_keep_the_rules_a_caller_relies_on)
{
    /* the three files, then the changed files the reader took */
    static prn_spec_t keep[FUZZ_POOL];
    const unsigned taken = s_npool;
    memcpy(keep, s_pool, sizeof s_pool);
    s_npool = 0u;
    for (unsigned f = 0; f < 3u; ++f) {
        prn_perr_t e;
        CHECK(prn_parse(s_base[f], s_base_n[f], k_names[f], &s_pool[f], &e));
    }
    for (unsigned i = 0; i + 3u < FUZZ_POOL && i < taken; ++i) {
        s_pool[3u + i] = keep[i];
    }
    s_npool = (taken + 3u < FUZZ_POOL) ? taken + 3u : FUZZ_POOL;
    unsigned ok = 0u;
    for (size_t s = 0; s < sizeof k_seeds / sizeof k_seeds[0]; ++s) {
        for (unsigned id = 0; id < FUZZ_JOBS; ++id) {
            ok += one_job(k_seeds[s], id) ? 1u : 0u;
        }
    }
    CHECK_EQ(ok, 3u * FUZZ_JOBS);
    printf("      ends:");
    for (unsigned i = 0; i < PRN_ERR_COUNT; ++i) {
        printf(" %u", s_ends[i]);
    }
    printf("; %u data frames\n", s_frames);
}

int main(void)
{
    memset(&s_zero, 0, sizeof s_zero);
    for (unsigned f = 0; f < 3u; ++f) {
        s_base_n[f] = prn_read(k_files[f], s_base[f], sizeof s_base[f]);
        if (s_base_n[f] == 0u) {
            printf("%s cannot be read\n", k_files[f]);
            return 1;
        }
    }
    RUN(every_cut_and_every_line_removed_doubled_and_swapped);
    RUN(random_files_keep_the_rules_a_caller_relies_on);
    RUN(random_jobs_keep_the_rules_a_caller_relies_on);
    return test_summary("prn_fuzz");
}
