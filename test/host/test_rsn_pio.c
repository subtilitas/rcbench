/*
 * The remote servo node's PIO block placement (shared/outputs/rsn_pio.c)
 * against a model of the RP2350's 3 blocks of 4 state machines and 32
 * words, searched as pico-sdk 2.3.0 and out_dshot.c's claim_pair() search
 * them: PIO2 down to PIO0, the lowest free state machine (a free adjacent
 * pair n, n + 1 modulo 4 for bidirectional DShot), room for the program.
 *
 * Under test: the fixed load (the PD mini UART 4 + 8 words, MIRROR 1,
 * CAPTURE 6) in PIO0 and nothing else there; the outputs in PIO2 first,
 * then PIO1; a session's line in a block that holds no output, PIO1 first,
 * refused while both hold one; no output in the line's block, and the
 * line's block free for outputs once it is gone; the Hitec D line with its
 * precharge sampler in the slot block's last state machine; the 7th PPM
 * slot refused; board 3's table of tools/pio_budget.py row by row, with
 * the session opened before and after the slots; every order of up to 9
 * plain DShot, bidirectional DShot and PPM slots with each session against
 * the table's rule; the fence let go after every bind, taken or refused;
 * the calls refused on a NULL, an unknown use, a pin past 47 and a fence
 * that already stands.
 *
 * The model's lengths are the programs' (protocols/ .pio files, as
 * tools/pio_budget.py reads them); it says nothing about timing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "rsn_pio.h"

/* The programs' words. */
#define W_DSHOT   4u
#define W_BIDIR_TX 11u
#define W_BIDIR_RX 5u
#define W_PPM     4u
#define W_KST     24u
#define W_EW      25u
#define W_HITEC   30u
#define W_PRE     12u
#define W_PD_TX   4u
#define W_PD_RX   8u
#define W_MIRROR  1u
#define W_CAPTURE 6u
#define ROOM      32u

/* ------------------------------------------------------------ the chip */

static struct {
    uint8_t  claimed[RSN_PIO_BLOCKS];
    unsigned words[RSN_PIO_BLOCKS];
    unsigned fence_calls;           /* claim() calls by the placement */
} g;

static bool m_claimed(void *ctx, unsigned b, unsigned sm)
{
    (void)ctx;
    return (g.claimed[b] & (1u << sm)) != 0u;
}
static void m_claim(void *ctx, unsigned b, unsigned sm)
{
    (void)ctx;
    g.claimed[b] |= (uint8_t)(1u << sm);
    g.fence_calls++;
}
static void m_unclaim(void *ctx, unsigned b, unsigned sm)
{
    (void)ctx;
    g.claimed[b] &= (uint8_t)~(1u << sm);
}
static const rsn_pio_ops_t k_ops = { m_claimed, m_claim, m_unclaim, NULL };

/* One program claimed: its block, state machine and words. */
typedef struct {
    int      block;                 /* -1: none */
    unsigned sm;
    unsigned words;
} held_t;

/* The SDK's search for one program: PIO2 down, the lowest free state
 * machine of a block with room. */
static held_t sdk_add(unsigned len)
{
    held_t h = { -1, 0u, 0u };
    for (unsigned b = RSN_PIO_BLOCKS; b-- > 0u;) {
        if (g.words[b] + len > ROOM) {
            continue;
        }
        for (unsigned sm = 0; sm < RSN_PIO_SMS; ++sm) {
            if ((g.claimed[b] & (1u << sm)) == 0u) {
                g.claimed[b] |= (uint8_t)(1u << sm);
                g.words[b] += len;
                h.block = (int)b;
                h.sm = sm;
                h.words = len;
                return h;
            }
        }
    }
    return h;
}

/* claim_pair(): PIO2 down, the first free pair n, n + 1 modulo 4 of a
 * block with room for both programs.  The pair's second machine is
 * h.sm + 1 modulo 4. */
static held_t pair_add(void)
{
    held_t h = { -1, 0u, 0u };
    for (unsigned b = RSN_PIO_BLOCKS; b-- > 0u;) {
        for (unsigned tx = 0; tx < RSN_PIO_SMS; ++tx) {
            const unsigned rx = (tx + 1u) % RSN_PIO_SMS;
            if ((g.claimed[b] & ((1u << tx) | (1u << rx))) != 0u) {
                continue;
            }
            if (g.words[b] + W_BIDIR_TX + W_BIDIR_RX > ROOM) {
                break;              /* claim_pair() gives up on the block */
            }
            g.claimed[b] |= (uint8_t)((1u << tx) | (1u << rx));
            g.words[b] += W_BIDIR_TX + W_BIDIR_RX;
            h.block = (int)b;
            h.sm = tx;
            h.words = W_BIDIR_TX + W_BIDIR_RX;
            return h;
        }
    }
    return h;
}

static void drop(held_t *h, bool pair)
{
    if (h->block < 0) {
        return;
    }
    const unsigned b = (unsigned)h->block;
    g.claimed[b] &= (uint8_t)~(1u << h->sm);
    if (pair) {
        g.claimed[b] &= (uint8_t)~(1u << ((h->sm + 1u) % RSN_PIO_SMS));
    }
    g.words[b] -= h->words;
    h->block = -1;
}

/* ------------------------------------------------------- the placement */

static rsn_pio_t p;

/* What a test binds. */
typedef enum { K_PLAIN, K_BIDIR, K_PPM } kind_t;
typedef enum { S_NONE, S_KST, S_EW, S_HITEC, S_PRE, S_COUNT } session_t;


/* The bound outputs, by pin; the line and its sampler. */
static held_t g_out[RSN_PIO_GPIOS];
static bool   g_out_pair[RSN_PIO_GPIOS];
static held_t g_line, g_pre;

/* The boot: every block free, then the fixed load into PIO0 as the
 * firmware places it, the PD mini UART and A5's MIRROR and CAPTURE. */
static void boot(void)
{
    memset(&g, 0, sizeof(g));
    for (unsigned i = 0; i < RSN_PIO_GPIOS; ++i) {
        g_out[i].block = -1;
        g_out_pair[i] = false;
    }
    g_line.block = -1;
    g_pre.block = -1;
    rsn_pio_init(&p);
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_FIXED, 0u), RSN_PIO_OK);
    held_t tx = sdk_add(W_PD_TX);
    held_t rx = sdk_add(W_PD_RX);
    rsn_pio_end(&p, &k_ops, tx.block >= 0 && rx.block >= 0);
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_FIXED, 0u), RSN_PIO_OK);
    held_t mi = sdk_add(W_MIRROR);
    held_t ca = sdk_add(W_CAPTURE);
    rsn_pio_end(&p, &k_ops, mi.block >= 0 && ca.block >= 0);
    CHECK_EQ(g.claimed[0], 0xFu);
    CHECK_EQ(g.words[0], 19u);
    CHECK_EQ(g.claimed[1] | g.claimed[2], 0u);
    CHECK_EQ(rsn_pio_output_blocks(&p), 0u);
    CHECK_EQ(p.line, RSN_PIO_NONE);
}

/* The claims the model's programs hold: every fence let go. */
static bool no_fence_left(void)
{
    uint8_t want[RSN_PIO_BLOCKS] = { 0xFu, 0u, 0u };
    for (unsigned i = 0; i < RSN_PIO_GPIOS; ++i) {
        const held_t *h = &g_out[i];
        if (h->block >= 0) {
            want[h->block] |= (uint8_t)(1u << h->sm);
            if (g_out_pair[i]) {
                want[h->block] |= (uint8_t)(1u << ((h->sm + 1u) % 4u));
            }
        }
    }
    if (g_line.block >= 0) {
        want[g_line.block] |= (uint8_t)(1u << g_line.sm);
    }
    if (g_pre.block >= 0) {
        want[g_pre.block] |= (uint8_t)(1u << g_pre.sm);
    }
    return !p.open && memcmp(want, g.claimed, sizeof(want)) == 0;
}

/* An output bound on @p pin as the firmware binds it: the fence, the
 * driver, the record. */
static bool bind(kind_t k, uint8_t pin)
{
    if (rsn_pio_begin(&p, &k_ops, (k == K_PPM) ? RSN_PIO_PPM : RSN_PIO_OUTPUT,
                      pin) != RSN_PIO_OK) {
        return false;
    }
    const held_t h = (k == K_BIDIR) ? pair_add()
                                    : sdk_add((k == K_PPM) ? W_PPM : W_DSHOT);
    const bool ok = h.block >= 0;
    if (ok) {
        g_out[pin] = h;
        g_out_pair[pin] = k == K_BIDIR;
    }
    rsn_pio_end(&p, &k_ops, ok);
    return ok;
}

static void unbind(uint8_t pin)
{
    drop(&g_out[pin], g_out_pair[pin]);
    rsn_pio_gone(&p, pin);
}

/* A session's line opened, as the drivers open it: the Hitec D line and
 * then its sampler, which gives the line back when it finds no room. */
static bool open_line(session_t s)
{
    const rsn_pio_use_t use = (s == S_PRE) ? RSN_PIO_LINE_PRE : RSN_PIO_LINE;
    if (rsn_pio_begin(&p, &k_ops, use, 16u) != RSN_PIO_OK) {
        return false;
    }
    const unsigned len = (s == S_KST) ? W_KST : (s == S_EW) ? W_EW : W_HITEC;
    g_line = sdk_add(len);
    bool ok = g_line.block >= 0;
    if (ok && s == S_PRE) {
        g_pre = sdk_add(W_PRE);
        if (g_pre.block < 0) {
            drop(&g_line, false);
            ok = false;
        }
    }
    rsn_pio_end(&p, &k_ops, ok);
    return ok;
}

static void close_line(void)
{
    drop(&g_line, false);
    drop(&g_pre, false);
    rsn_pio_line_gone(&p);
}

/* ----------------------------------------------------- the table's rule */

/* tools/pio_budget.py's _packs(): the slots into blocks of the given free
 * state machines, a pair in one block. */
static bool packs(const unsigned *blocks, unsigned nb, unsigned singles,
                  unsigned pairs)
{
    if (pairs == 0u) {
        unsigned sum = 0u;
        for (unsigned i = 0; i < nb; ++i) {
            sum += blocks[i];
        }
        return singles <= sum;
    }
    for (unsigned i = 0; i < nb; ++i) {
        if (blocks[i] >= 2u) {
            unsigned b2[2] = { blocks[0], nb > 1u ? blocks[1] : 0u };
            b2[i] -= 2u;
            if (packs(b2, nb, singles, pairs - 1u)) {
                return true;
            }
        }
    }
    return false;
}

/* rsn_allows(): [4, 4] without a session, [4] with one, [3] beside the
 * Hitec D sampler; at most 6 PPM; 9 pins, a session takes one. */
static bool allows(session_t s, unsigned singles, unsigned pairs,
                   unsigned ppm)
{
    const unsigned both[2] = { 4u, 4u };
    if (!packs(both, 2u, singles, pairs) || ppm > RSN_PIO_PPM_MAX
        || singles + pairs > 9u - (s != S_NONE ? 1u : 0u)) {
        return false;
    }
    if (s == S_NONE) {
        return true;
    }
    const unsigned one[1] = { (s == S_PRE) ? 3u : 4u };
    return packs(one, 1u, singles, pairs);
}

/* ------------------------------------------------------------ the cases */

/* The fixed load is PIO0's and nothing else goes there: an output, a line
 * and a second fixed program each keep to their blocks. */
TEST_CASE(the_fixed_load_is_pio0s_alone)
{
    boot();
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_FIXED, NULL), 0x1u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_OUTPUT, NULL), 0x6u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_PPM, NULL), 0x6u);
    uint8_t at = 0u;
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_LINE, &at), 0x2u);
    CHECK_EQ(at, 1u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_LINE_PRE, &at), 0x6u);
    CHECK_EQ(at, 2u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_FIXED, &at), 0x1u);
    CHECK_EQ(at, RSN_PIO_NONE);
    /* With PIO0 full and PIO1 and PIO2 fenced, a fixed program finds no
     * block: the driver fails and the fence goes. */
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_FIXED, 0u), RSN_PIO_OK);
    CHECK_EQ(g.claimed[1] & g.claimed[2], 0xFu);
    const held_t h = sdk_add(W_PD_TX);
    CHECK_EQ(h.block, -1);
    rsn_pio_end(&p, &k_ops, false);
    CHECK(no_fence_left());
}

/* Outputs fill PIO2 first and then PIO1; PIO0 never.  9 plain: the 9th
 * finds no state machine and is refused, the fence let go. */
TEST_CASE(outputs_fill_pio2_then_pio1)
{
    boot();
    for (uint8_t k = 0; k < 4u; ++k) {
        CHECK(bind(K_PLAIN, (uint8_t)(16u + 2u * k)));
        CHECK_EQ(p.block[16u + 2u * k], 2u);
    }
    CHECK_EQ(rsn_pio_output_blocks(&p), 0x4u);
    for (uint8_t k = 4; k < 8u; ++k) {
        CHECK(bind(K_PLAIN, (uint8_t)(16u + 2u * k)));
        CHECK_EQ(p.block[16u + 2u * k], 1u);
    }
    CHECK(!bind(K_PLAIN, 32u));
    CHECK_EQ(p.block[32], RSN_PIO_NONE);
    CHECK_EQ(g.words[0], 19u);
    CHECK(no_fence_left());
    /* Bidirectional: 2 a block. */
    boot();
    CHECK(bind(K_BIDIR, 16u));
    CHECK(bind(K_BIDIR, 18u));
    CHECK(bind(K_BIDIR, 20u));
    CHECK(bind(K_BIDIR, 22u));
    CHECK_EQ(p.block[16], 2u);
    CHECK_EQ(p.block[18], 2u);
    CHECK_EQ(p.block[20], 1u);
    CHECK_EQ(p.block[22], 1u);
    CHECK(!bind(K_BIDIR, 24u));
    CHECK(!bind(K_PLAIN, 24u));
    CHECK(no_fence_left());
}

/* A session's line goes into PIO1 while PIO2 holds the outputs, and is
 * refused once both hold one: the panel's "NO PIO BLOCK FREE: UNBIND A
 * DSHOT OUTPUT".  Unbinding the output in PIO1 lets it in. */
TEST_CASE(a_line_needs_a_block_without_outputs)
{
    for (session_t s = S_KST; s < S_COUNT; s = (session_t)(s + 1)) {
        boot();
        CHECK(bind(K_PLAIN, 16u));
        CHECK(open_line(s));
        /* The line's block: PIO1, the block without outputs. */
        CHECK_EQ(p.line, 1u);
        CHECK_EQ(g_line.block, 1);
        if (s == S_PRE) {
            /* The sampler beside the outputs, in PIO2. */
            CHECK_EQ(g_pre.block, 2);
        }
        CHECK(no_fence_left());
        close_line();
        CHECK_EQ(p.line, RSN_PIO_NONE);

        /* Five plain: PIO2 full, the fifth in PIO1.  No block for a line,
         * and nothing claimed by the refusal. */
        for (uint8_t k = 1; k < 5u; ++k) {
            CHECK(bind(K_PLAIN, (uint8_t)(16u + 2u * k)));
        }
        CHECK_EQ(rsn_pio_output_blocks(&p), 0x6u);
        const unsigned calls = g.fence_calls;
        CHECK(!open_line(s));
        CHECK_EQ(g.fence_calls, calls);
        CHECK(no_fence_left());
        CHECK_EQ(p.line, RSN_PIO_NONE);
        unbind(24u);                        /* the one in PIO1 */
        CHECK_EQ(rsn_pio_output_blocks(&p), 0x4u);
        if (s == S_PRE) {
            /* PIO2 holds 4 outputs: no state machine for the sampler. */
            CHECK(!open_line(s));
            CHECK(no_fence_left());
            unbind(22u);
        }
        CHECK(open_line(s));
        CHECK_EQ(p.line, 1u);
        close_line();
    }
}

/* With both blocks free a line goes into PIO1, and a Hitec D line into
 * PIO2 with its sampler in PIO1.  An output then goes into the other
 * block only, and none is placed beside the line, though the KST line's
 * 24 words leave room for two. */
TEST_CASE(no_output_goes_into_the_lines_block)
{
    boot();
    CHECK(open_line(S_KST));
    CHECK_EQ(p.line, 1u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_OUTPUT, NULL), 0x4u);
    for (uint8_t k = 0; k < 4u; ++k) {
        CHECK(bind(K_PLAIN, (uint8_t)(16u + 2u * k)));
        CHECK_EQ(p.block[16u + 2u * k], 2u);
    }
    CHECK(!bind(K_PLAIN, 24u));             /* PIO1 has 8 words, 3 SMs */
    CHECK_EQ(g.words[1], W_KST);
    CHECK(no_fence_left());
    /* A second line while one stands: none. */
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_LINE, NULL), 0u);
    CHECK_EQ(rsn_pio_blocks(&p, RSN_PIO_LINE_PRE, NULL), 0u);
    /* Once it is gone its block takes outputs again. */
    close_line();
    CHECK(bind(K_PLAIN, 24u));
    CHECK_EQ(p.block[24], 1u);

    boot();
    CHECK(open_line(S_PRE));
    CHECK_EQ(p.line, 2u);
    CHECK_EQ(g_line.block, 2);
    CHECK_EQ(g_pre.block, 1);
    CHECK(bind(K_BIDIR, 16u));
    CHECK(bind(K_PLAIN, 18u));
    CHECK_EQ(p.block[16], 1u);
    CHECK_EQ(p.block[18], 1u);
    CHECK_EQ(g.words[1], W_PRE + W_BIDIR_TX + W_BIDIR_RX + W_DSHOT);
    CHECK(!bind(K_PLAIN, 20u));
    CHECK(no_fence_left());
}

/* Six PPM slots, the seventh refused with PPM_FULL before anything is
 * claimed; a PPM slot gone lets the next in.  A plain DShot output past
 * the six is not counted. */
TEST_CASE(the_seventh_ppm_slot_is_refused)
{
    boot();
    for (uint8_t k = 0; k < RSN_PIO_PPM_MAX; ++k) {
        CHECK(bind(K_PPM, (uint8_t)(16u + 2u * k)));
    }
    CHECK_EQ(rsn_pio_ppm_count(&p), 6u);
    const unsigned calls = g.fence_calls;
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_PPM, 28u), RSN_PIO_PPM_FULL);
    CHECK_EQ(g.fence_calls, calls);
    CHECK(!p.open);
    CHECK(bind(K_PLAIN, 28u));
    CHECK_EQ(rsn_pio_ppm_count(&p), 6u);
    unbind(16u);
    CHECK_EQ(rsn_pio_ppm_count(&p), 5u);
    CHECK(bind(K_PPM, 16u));
    CHECK_EQ(rsn_pio_ppm_count(&p), 6u);
    /* The pin moves from PPM to DShot: no longer counted. */
    unbind(16u);
    CHECK(bind(K_PLAIN, 16u));
    CHECK_EQ(rsn_pio_ppm_count(&p), 5u);
    CHECK(no_fence_left());
}

/* One row of the table: the slots, then the session (or the session
 * first); whether all of it is taken. */
static bool row(session_t s, unsigned plain, unsigned pairs, unsigned ppm,
                bool session_first)
{
    boot();
    bool ok = true;
    if (s != S_NONE && session_first) {
        ok = open_line(s);
    }
    uint8_t pin = 16u;
    for (unsigned k = 0; k < pairs && ok; ++k, pin = (uint8_t)(pin + 2u)) {
        ok = bind(K_BIDIR, pin);
    }
    for (unsigned k = 0; k < plain && ok; ++k, pin = (uint8_t)(pin + 2u)) {
        ok = bind(K_PLAIN, pin);
    }
    for (unsigned k = 0; k < ppm && ok; ++k, pin = (uint8_t)(pin + 2u)) {
        ok = bind(K_PPM, pin);
    }
    if (ok && s != S_NONE && !session_first) {
        ok = open_line(s);
    }
    CHECK(no_fence_left());
    CHECK_EQ(g.words[0], 19u);
    return ok;
}

/* tools/pio_budget.py's board 3 table, each row both ways round. */
TEST_CASE(board_3s_table_holds_with_the_session_before_and_after)
{
    for (int first = 0; first < 2; ++first) {
        const bool f = first != 0;
        CHECK(row(S_NONE, 8u, 0u, 0u, f));
        CHECK(row(S_NONE, 0u, 4u, 0u, f));
        for (session_t s = S_KST; s <= S_HITEC; s = (session_t)(s + 1)) {
            CHECK(row(s, 4u, 0u, 0u, f));
            CHECK(row(s, 0u, 2u, 0u, f));
            CHECK(row(s, 2u, 1u, 0u, f));
            CHECK(!row(s, 5u, 0u, 0u, f));
            CHECK(!row(s, 1u, 2u, 0u, f));
        }
        CHECK(row(S_PRE, 3u, 0u, 0u, f));
        CHECK(row(S_PRE, 1u, 1u, 0u, f));
        CHECK(!row(S_PRE, 4u, 0u, 0u, f));
        CHECK(!row(S_PRE, 0u, 2u, 0u, f));
        CHECK(row(S_NONE, 0u, 0u, 6u, f));
        CHECK(!row(S_NONE, 0u, 0u, 7u, f));
    }
}

/* With the slots bound, the session opened and closed again: taken exactly
 * when the table allows the mix with it.  Counts into @p wrong and
 * @p allowed. */
static void try_session(session_t s, unsigned singles, unsigned pairs,
                        unsigned ppm, unsigned *wrong, unsigned *allowed)
{
    const bool want = allows(s, singles, pairs, ppm);
    const bool got = open_line(s);
    if (got != want || !no_fence_left()) {
        ++*wrong;
    }
    if (got) {
        close_line();
        ++*allowed;
    }
}

/* Every order of up to 9 outputs (8 beside a session), each output plain
 * DShot, bidirectional DShot or PPM, with each session opened before the
 * slots or tried after each of them: each bind and each session is taken
 * exactly when the table allows the mix it makes, and a refusal leaves
 * nothing claimed. */
TEST_CASE(every_order_of_outputs_with_each_session_meets_the_table)
{
    unsigned runs = 0u, wrong = 0u, allowed = 0u;
    for (session_t s = S_NONE; s < S_COUNT; s = (session_t)(s + 1)) {
        const unsigned max = 9u - (s != S_NONE ? 1u : 0u);
        for (int first = 0; first < (s == S_NONE ? 1 : 2); ++first) {
            /* Sequences of length max in base 3; each prefix is a mix. */
            unsigned total = 1u;
            for (unsigned i = 0; i < max; ++i) {
                total *= 3u;
            }
            for (unsigned code = 0; code < total; ++code) {
                boot();
                ++runs;
                if (s != S_NONE && first != 0 && !open_line(s)) {
                    ++wrong;
                    continue;
                }
                unsigned singles = 0u, pairs = 0u, ppm = 0u;
                unsigned c = code;
                bool open = true;
                if (s != S_NONE && first == 0) {
                    try_session(s, 0u, 0u, 0u, &wrong, &allowed);
                }
                for (unsigned i = 0; i < max && open; ++i, c /= 3u) {
                    const kind_t k = (kind_t)(c % 3u);
                    const unsigned n1 = singles + (k != K_BIDIR ? 1u : 0u);
                    const unsigned n2 = pairs + (k == K_BIDIR ? 1u : 0u);
                    const unsigned nq = ppm + (k == K_PPM ? 1u : 0u);
                    /* With the session after, the slots alone are judged
                     * here and the session after each of them. */
                    const session_t judge =
                        (first != 0) ? s : S_NONE;
                    const bool want = allows(judge, n1, n2, nq);
                    const bool got = bind(k, (uint8_t)(16u + 2u * i));
                    if (got != want || !no_fence_left()) {
                        ++wrong;
                    }
                    if (!got) {
                        open = false;
                    } else {
                        singles = n1;
                        pairs = n2;
                        ppm = nq;
                        if (s != S_NONE && first == 0) {
                            try_session(s, singles, pairs, ppm, &wrong,
                                        &allowed);
                        }
                    }
                }
            }
        }
    }
    CHECK_EQ(wrong, 0u);
    CHECK(runs > 50000u);
    CHECK(allowed > 0u);
}

/* The calls refuse what they cannot place and change nothing then. */
TEST_CASE(the_calls_refuse_bad_arguments)
{
    boot();
    const rsn_pio_ops_t none = { NULL, m_claim, m_unclaim, NULL };
    CHECK_EQ(rsn_pio_begin(NULL, &k_ops, RSN_PIO_OUTPUT, 16u), RSN_PIO_BAD);
    CHECK_EQ(rsn_pio_begin(&p, NULL, RSN_PIO_OUTPUT, 16u), RSN_PIO_BAD);
    CHECK_EQ(rsn_pio_begin(&p, &none, RSN_PIO_OUTPUT, 16u), RSN_PIO_BAD);
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_USES, 16u), RSN_PIO_BAD);
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_OUTPUT, RSN_PIO_GPIOS),
             RSN_PIO_BAD);
    CHECK(no_fence_left());
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_OUTPUT, 16u), RSN_PIO_OK);
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_OUTPUT, 18u), RSN_PIO_BAD);
    rsn_pio_end(&p, &k_ops, false);
    CHECK(no_fence_left());
    rsn_pio_end(&p, &k_ops, true);          /* no fence: nothing */
    CHECK(no_fence_left());
    CHECK_EQ(rsn_pio_output_blocks(&p), 0u);
    CHECK_EQ(rsn_pio_blocks(NULL, RSN_PIO_OUTPUT, NULL), 0u);
    CHECK_EQ(rsn_pio_output_blocks(NULL), 0u);
    CHECK_EQ(rsn_pio_ppm_count(NULL), 0u);
    rsn_pio_gone(&p, RSN_PIO_GPIOS);
    rsn_pio_gone(NULL, 16u);
    rsn_pio_line_gone(NULL);
    rsn_pio_init(NULL);
    rsn_pio_end(NULL, &k_ops, true);
    /* A bind the driver kept as it was claims nothing: the record stays. */
    CHECK(bind(K_PLAIN, 16u));
    CHECK_EQ(rsn_pio_begin(&p, &k_ops, RSN_PIO_OUTPUT, 16u), RSN_PIO_OK);
    rsn_pio_end(&p, &k_ops, true);
    CHECK_EQ(p.block[16], 2u);
    CHECK(no_fence_left());
}

int main(void)
{
    RUN(the_fixed_load_is_pio0s_alone);
    RUN(outputs_fill_pio2_then_pio1);
    RUN(a_line_needs_a_block_without_outputs);
    RUN(no_output_goes_into_the_lines_block);
    RUN(the_seventh_ppm_slot_is_refused);
    RUN(board_3s_table_holds_with_the_session_before_and_after);
    RUN(every_order_of_outputs_with_each_session_meets_the_table);
    RUN(the_calls_refuse_bad_arguments);
    return test_summary("rsn_pio");
}
