/*
 * Where the panel's control task finds an error, and what it does there
 * (shared/safety/error_sites.c), with the steps the control task takes
 * for each act as firmware/panel/main/panel_supply.c's error_act() takes
 * them.
 *
 * Under test, every site against every bench: error_sites_stop() for an
 * error found elsewhere and an ON across lost touch events, the counters'
 * error_sites_follow(), error_sites_cut() for the panel's own supply trip,
 * and error_sites_reading() for a silent supply and one without a reading.
 * Each asks DISARM of an ARMED bench and of one on its way to ARMED and of
 * no SAFE one.  With an IOmcu of link 4.31 each switches the supply off and
 * writes the page's OFF whoever holds the supply and whatever the panel
 * reads of it; with an older IOmcu, or one without the SUPPLY page, every
 * site but the trip leaves the PD mini a programming port holds to the
 * port, and switches the model off.  A reading fault the port keeps acts
 * once, and again when the port lets go.  The SUPPLY page's events: an ON
 * refused under a port's ON left to the port on an older IOmcu, written
 * off on 4.31; an output off at the far end followed off whoever holds it.
 * The finders end to end: the run log's failed write and its failed close,
 * the automatic test's failed CSV line and its failed final commit or
 * close, each once a run; a KST and a Hitec session fault; an INA3221
 * channel over its limit; the SUPPLY page's TRIPPED and its ON let go; a
 * supply trip; an ON taken across lost touch events.  The operator's own
 * endings -- OUTPUT OFF on SUPPLY, STOP TEST -- leave the bench ARMED.
 * On a board that switches the programming ports' own supply (4.32) every
 * error writes that page's OFF.  A port holds the supply while the SUPPLY
 * page's last read (LINK_SP_PORT) and a programmer's session both say so;
 * a port that takes or lets go of it between two reads, on each IOmcu.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "error_sites.h"
#include "hitec_prog.h"
#include "kst_link.h"
#include "link_msg.h"
#include "link_pages.h"
#include "log_writer.h"
#include "outputs.h"
#include "pdmini.h"
#include "sense_limit.h"
#include "sense_limit_link.h"
#include "sense_page.h"
#include "supply.h"
#include "supply_link.h"
#include "supply_page.h"
#include "touch_loss.h"

#define MV 5000u
#define MA 1000u

/* ------------------------------------------------- the control task, faked */

/* What the bench is: SAFE, ARMED, or a switch to ARMED waiting. */
typedef enum { BENCH_SAFE, BENCH_ARMED, BENCH_ARMING } bench_t;

static struct {
    arming_t a;
    bool     bank_armed;
    bool     on;           /* the supply's output, as the panel asks it  */
    bool     port;         /* a programming port holds the supply, for a
                              bench without the page's link (g.link)     */
    bool     session;      /* a programmer has a port (s_prog_taken),
                              which error_bench() does not read          */
    bool     real;         /* the PD mini, not the model                 */
    bool     page;         /* the IOmcu serves the SUPPLY page           */
    uint16_t minor;
    unsigned disarms;      /* DISARM requests                            */
    unsigned cuts;         /* the page's OFF, whatever the panel reads   */
    error_reading_t told;  /* the reading fault of the last step         */
    supply_link_t *link;   /* when set, act() drives it as the panel does */
} g;

/* The bench as the control task's error_bench() reads it.  With the page's
 * link driven (g.link), a port holds the supply while the page's last read
 * and a programmer's session (g.session) both say so
 * (supply_link_port_holds()). */
static error_bench_t bench_now(void)
{
    const bool port = (g.link != NULL)
                      ? supply_link_port_holds(g.link, g.real, g.session)
                      : g.port;
    const error_bench_t b = {
        arming_link_needed(&g.a, g.bank_armed), g.on, port, g.real,
        g.page, g.minor, false,
    };
    return b;
}

/* supply_switch(false): nothing for an output that is off. */
static void supply_off(void)
{
    if (g.on && g.link != NULL) {
        supply_link_command(g.link, false, MV, MA);
    }
    g.on = false;
}

/* error_act(): the steps in their order.  DISARM is a request: the bench
 * stays as it is until the control task serves it. */
static void act(error_act_t e)
{
    if (e.off_first) {
        supply_off();
    }
    if (e.disarm) {
        ++g.disarms;
    }
    if (e.supply_off) {
        supply_off();
    }
    if (e.cut) {
        ++g.cuts;
        if (g.link != NULL) {
            supply_link_cut(g.link);
        }
    }
}

/* The sites as panel_supply.c calls them. */
static void do_stop(void)
{
    const error_bench_t b = bench_now();
    act(error_sites_stop(&b));
}

static void do_follow(error_watch_t *watch, uint32_t count)
{
    const error_bench_t b = bench_now();
    act(error_sites_follow(watch, count, &b));
}

static void do_cut(void)
{
    const error_bench_t b = bench_now();
    act(error_sites_cut(&b));
}

static error_reading_t do_reading(bool online, bool blind)
{
    const error_bench_t b = bench_now();
    error_act_t e;
    const error_reading_t r =
        error_sites_reading(&b, online, blind, &g.told, &e);
    act(e);
    return r;
}

/* A bench in state @p s, the output @p on, nothing asked yet; an IOmcu of
 * 4.31 with the PD mini and no port holding it. */
static void bench(bench_t s, bool on)
{
    memset(&g, 0, sizeof(g));
    arming_init(&g.a, 0u, 0u);
    g.bank_armed = s == BENCH_ARMED;
    if (s == BENCH_ARMING) {
        arming_request_arm(&g.a, 0u);
    }
    CHECK_EQ(arming_link_needed(&g.a, g.bank_armed), s != BENCH_SAFE);
    g.on = on;
    g.real = true;
    g.page = true;
    g.minor = LINK_MINOR_PORT_CUT;
}

/* --------------------------------------------------------------- the rule */

/* The sites, each called once.  An ON across lost touch events is a stop
 * (supply_watch_service()); the trip is the cut. */
typedef enum {
    SITE_STOP, SITE_FOLLOW, SITE_CUT, SITE_SILENT, SITE_BLIND, SITE_COUNT,
} site_t;

static void hit(site_t s)
{
    error_watch_t watch;

    switch (s) {
    case SITE_STOP:
        do_stop();
        break;
    case SITE_FOLLOW:
        error_watch_init(&watch, 41u);
        do_follow(&watch, 42u);
        CHECK_EQ(watch.seen, 42u);
        break;
    case SITE_CUT:
        do_cut();
        break;
    case SITE_SILENT:
        CHECK_EQ(do_reading(false, false),
                 ERROR_READING_SILENT);
        break;
    default:
        CHECK_EQ(do_reading(true, true), ERROR_READING_BLIND);
        break;
    }
}

/* Every site, bench, IOmcu, holder, output and supply.  The trip switches
 * the output off first, whoever holds it; every other site leaves it to
 * error_stop(), with a port holding the PD mini and never the model. */
TEST_CASE(every_site_against_every_bench)
{
    static const struct {
        bool     page;
        uint16_t minor;
    } k_far[] = {
        { true, LINK_MINOR_PORT_CUT },
        { true, LINK_MINOR_PORT_CUT - 1u },
        { false, LINK_MINOR_PORT_CUT },
    };

    for (int s = 0; s < (int)SITE_COUNT; ++s) {
        const bool own = s == SITE_CUT;
        for (int b = 0; b < 3; ++b) {
            for (unsigned f = 0; f < 3u; ++f) {
                for (int k = 0; k < 8; ++k) {
                    const bool on   = (k & 1) != 0;
                    const bool port = (k & 2) != 0;
                    const bool real = (k & 4) != 0;
                    /* A reading is judged with the output on; a blind one
                     * is the PD mini's. */
                    if ((s == SITE_SILENT || s == SITE_BLIND) && !on) {
                        continue;
                    }
                    if (s == SITE_BLIND && !real) {
                        continue;
                    }
                    bench((bench_t)b, on);
                    g.port = port;
                    g.real = real;
                    g.page = k_far[f].page;
                    g.minor = k_far[f].minor;
                    const bool defers = f == 0u;
                    hit((site_t)s);
                    CHECK_EQ(g.disarms, (b != BENCH_SAFE) ? 1u : 0u);
                    CHECK_EQ(g.on, on && !own && !defers && port && real);
                    const error_bench_t kb = {
                        false, on, port, real, k_far[f].page, k_far[f].minor,
                        false,
                    };
                    CHECK_EQ(error_sites_keeps(&kb),
                             on && !defers && port && real);
                    CHECK_EQ(g.cuts, (real && defers) ? 1u : 0u);
                }
            }
        }
    }
}

TEST_CASE(the_iomcu_defers_from_4_31_with_the_supply_page)
{
    CHECK(error_port_defers(true, LINK_MINOR_PORT_CUT));
    CHECK(error_port_defers(true, 0xFFFFu));
    CHECK(!error_port_defers(true, LINK_MINOR_PORT_CUT - 1u));
    CHECK(!error_port_defers(true, 0u));
    CHECK(!error_port_defers(false, LINK_MINOR_PORT_CUT));
}

/* The cut judges the bench as its OFF leaves it, as bench_error_stop()
 * after supply_switch(false) did: an older IOmcu gets no second OFF. */
TEST_CASE(a_cut_judges_the_bench_with_the_output_off)
{
    error_bench_t b = { true, true, false, true, true, LINK_MINOR_PORT_CUT,
                        false };
    error_act_t e;

    b.far_minor = LINK_MINOR_PORT_CUT - 1u;
    e = error_sites_cut(&b);
    CHECK(e.off_first && e.disarm);
    CHECK(!e.supply_off && !e.cut);
    b.far_minor = LINK_MINOR_PORT_CUT;
    e = error_sites_cut(&b);
    CHECK(e.off_first && e.disarm && e.supply_off && e.cut);
}

/* With the output off a reading finds nothing; a silent supply is named
 * before a blind one; the model has no blind reading. */
TEST_CASE(a_reading_is_judged_with_the_output_on)
{
    bench(BENCH_ARMED, false);
    CHECK_EQ(do_reading(false, true), ERROR_READING_NONE);
    CHECK_EQ(g.disarms, 0u);
    CHECK_EQ(g.cuts, 0u);
    bench(BENCH_ARMED, true);
    CHECK_EQ(do_reading(true, false), ERROR_READING_NONE);
    CHECK(g.on);
    CHECK_EQ(do_reading(false, true), ERROR_READING_SILENT);
    CHECK(!g.on);
    bench(BENCH_ARMED, true);
    g.real = false;
    CHECK_EQ(do_reading(true, true), ERROR_READING_NONE);
    CHECK(g.on);
    CHECK_EQ(g.disarms, 0u);
}

/* A counter acts once per change, across its wrap as well. */
TEST_CASE(a_counter_acts_once_per_change)
{
    error_watch_t watch;

    bench(BENCH_ARMED, true);
    error_watch_init(&watch, 0xFFFFFFFFu);
    do_follow(&watch, 0xFFFFFFFFu);
    CHECK_EQ(g.disarms, 0u);
    CHECK(g.on);
    do_follow(&watch, 0u);
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
    do_follow(&watch, 0u);
    CHECK_EQ(g.disarms, 1u);
    CHECK_EQ(g.cuts, 1u);
}

/* The programming ports' own supply (4.32, the carrier's GPIO 21): every
 * site writes its OFF on every bench, whoever holds it and whatever the
 * panel reads, and none without the page or the switch; a counter that
 * has not moved writes nothing. */
TEST_CASE(every_error_writes_the_ports_own_supply_off)
{
    for (int k = 0; k < 32; ++k) {
        error_bench_t b = {
            (k & 1) != 0, (k & 2) != 0, (k & 4) != 0, (k & 8) != 0, true,
            LINK_MINOR_PROG_SUPPLY, true,
        };
        if ((k & 16) != 0) {
            b.far_minor = LINK_MINOR_PORT_CUT - 1u;
        }
        CHECK(error_sites_stop(&b).prog_off);
        CHECK(error_sites_cut(&b).prog_off);
        error_watch_t w;
        error_watch_init(&w, 7u);
        CHECK(!error_sites_follow(&w, 7u, &b).prog_off);
        CHECK(error_sites_follow(&w, 8u, &b).prog_off);
        CHECK(!error_sites_follow(&w, 8u, &b).prog_off);
        /* Across the counter's wrap: a change is a change. */
        error_watch_init(&w, 0xFFFFFFFFu);
        CHECK(error_sites_follow(&w, 0u, &b).prog_off);
        error_reading_t told = ERROR_READING_NONE;
        error_act_t a;
        b.supply_on = true;
        (void)error_sites_reading(&b, false, false, &told, &a);
        CHECK(a.prog_off);              /* the fault's first pass acts */
        /* No fault: nothing. */
        told = ERROR_READING_NONE;
        (void)error_sites_reading(&b, true, false, &told, &a);
        CHECK(!a.prog_off);

        b.prog_supply = false;
        CHECK(!error_sites_stop(&b).prog_off);
        CHECK(!error_sites_cut(&b).prog_off);
        error_watch_init(&w, 0u);
        CHECK(!error_sites_follow(&w, 1u, &b).prog_off);
    }
    CHECK(!error_sites_stop(NULL).prog_off);
}

TEST_CASE(nothing_is_done_without_a_bench)
{
    error_watch_t watch;
    error_act_t e = { true, true, true, true, true };
    error_reading_t told = ERROR_READING_BLIND;
    const error_bench_t b = { true, true, false, true, true, 31u, false };

    bench(BENCH_ARMED, true);
    error_watch_init(&watch, 0u);
    act(error_sites_stop(NULL));
    act(error_sites_cut(NULL));
    act(error_sites_follow(&watch, 1u, NULL));
    act(error_sites_follow(NULL, 1u, &b));
    CHECK_EQ(error_sites_reading(NULL, false, true, &told, &e),
             ERROR_READING_NONE);
    CHECK(!e.off_first && !e.disarm && !e.supply_off && !e.cut);
    e.disarm = true;
    CHECK_EQ(error_sites_reading(&b, false, true, NULL, &e),
             ERROR_READING_NONE);
    CHECK(!e.disarm);
    CHECK_EQ(error_sites_reading(&b, false, true, &told, NULL),
             ERROR_READING_NONE);
    CHECK_EQ(told, ERROR_READING_BLIND);
    CHECK(!error_sites_keeps(NULL));
    CHECK(!error_sites_page_keeps(NULL, false));
    CHECK_EQ(g.disarms, 0u);
    CHECK_EQ(g.cuts, 0u);
    CHECK_EQ(watch.seen, 0u);
}

/* -------------------------------------------------------------- the card */

/* A card that takes every byte until it is pulled. */
static bool g_card_gone;

static int card_write(void *ctx, const void *data, size_t len)
{
    (void)ctx;
    (void)data;
    return g_card_gone ? -1 : (int)len;
}

static bool card_flush(void *ctx)
{
    (void)ctx;
    return !g_card_gone;
}

/* The logger's pass for one row of a supply run: the counter raised at the
 * writer's failure's edge, as panel_log.c raises s_card_faults. */
static void log_pass(log_writer_t *w, bool open, float t_s,
                     uint32_t *faults)
{
    const supply_state_t st = { 0 };
    const bool was_failed = log_writer_failed(w);

    if (open) {
        (void)log_writer_supply_row(w, t_s, &st);
    }
    if (error_card_rows_edge(open, was_failed, log_writer_failed(w))) {
        ++*faults;
    }
}

/* The run log's failed write: one error a run, at the failure's edge; a
 * run that ends whole raises none, and a pass without a file none. */
TEST_CASE(the_run_logs_failed_write_is_one_error)
{
    const log_sink_t sink = { card_write, card_flush, NULL };
    log_writer_t w;
    error_watch_t watch;
    uint32_t faults = 7u;

    bench(BENCH_ARMED, true);
    error_watch_init(&watch, faults);
    g_card_gone = false;
    log_writer_init(&w, &sink);
    for (int i = 0; i < 40; ++i) {
        log_pass(&w, true, 0.05f * (float)i, &faults);
        do_follow(&watch, faults);
    }
    CHECK_EQ(faults, 7u);
    CHECK_EQ(g.disarms, 0u);
    g_card_gone = true;
    for (int i = 40; i < 80; ++i) {
        log_pass(&w, true, 0.05f * (float)i, &faults);
        do_follow(&watch, faults);
    }
    CHECK_EQ(faults, 8u);
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
    CHECK_EQ(g.cuts, 1u);
    CHECK(!error_card_rows_edge(false, false, true));   /* no file */
    CHECK(!error_card_rows_edge(true, true, true));     /* failed before */
}

/* The automatic test's lines as panel_log.c writes them: the run's first
 * failure counts when it is the CSV's; the report after the run, and every
 * later failure, do not. */
static uint32_t test_lines(const bool *bad, const bool *txt, int n)
{
    uint32_t faults = 0u;
    bool failed = false;           /* s_test_failed, cleared at the OPEN */

    for (int i = 0; i < n; ++i) {
        if (bad[i]) {
            if (error_card_csv_edge(true, failed, txt[i])) {
                ++faults;
            }
            failed = true;
        }
    }
    return faults;
}

TEST_CASE(the_tests_failed_csv_is_one_error)
{
    static const bool k_csv[] = { false, false, false, false };
    static const bool k_txt[] = { false, false, true, true };
    static const bool k_twice[] = { false, true, true, false };
    static const bool k_late[] = { false, false, true, false };
    static const bool k_last[] = { false, false, false, true };
    error_watch_t watch;

    CHECK_EQ(test_lines(k_twice, k_csv, 4), 1u);
    CHECK_EQ(test_lines(k_late, k_txt, 4), 0u);    /* the report's */
    CHECK_EQ(test_lines(k_last, k_csv, 4), 1u);
    CHECK_EQ(test_lines(k_twice, k_txt, 4), 1u);   /* CSV, then report */
    CHECK(!error_card_csv_edge(false, false, false));

    bench(BENCH_ARMED, true);
    error_watch_init(&watch, 0u);
    do_follow(&watch, test_lines(k_twice, k_csv, 4));
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
}

/* The close of a run's file: the run log's, after its rows, and the
 * automatic test's CSV, then its report.  A failed final commit or close
 * is the run's card fault when nothing of the run failed before; the
 * report's is not one. */
TEST_CASE(a_failed_close_is_one_error_a_run)
{
    const log_sink_t sink = { card_write, card_flush, NULL };
    log_writer_t w;
    error_watch_t watch;
    uint32_t faults = 0u;

    CHECK(error_card_close_edge(true, false, false));
    CHECK(!error_card_close_edge(false, false, false));
    CHECK(!error_card_close_edge(true, true, false));
    CHECK(!error_card_close_edge(true, false, true));

    /* The run log: whole, its close fails -- one error. */
    bench(BENCH_ARMED, true);
    error_watch_init(&watch, faults);
    g_card_gone = false;
    log_writer_init(&w, &sink);
    for (int i = 0; i < 10; ++i) {
        log_pass(&w, true, 0.05f * (float)i, &faults);
    }
    if (error_card_close_edge(true, log_writer_failed(&w), false)) {
        ++faults;
    }
    CHECK_EQ(faults, 1u);
    do_follow(&watch, faults);
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
    /* Its rows failed, then its close: one error, counted at the rows. */
    g_card_gone = false;
    log_writer_init(&w, &sink);
    log_pass(&w, true, 0.0f, &faults);
    g_card_gone = true;
    for (int i = 1; i < 40; ++i) {
        log_pass(&w, true, 0.05f * (float)i, &faults);
    }
    CHECK_EQ(faults, 2u);
    if (error_card_close_edge(true, log_writer_failed(&w), false)) {
        ++faults;
    }
    CHECK_EQ(faults, 2u);
    /* A close that went through: none. */
    if (error_card_close_edge(false, false, false)) {
        ++faults;
    }
    CHECK_EQ(faults, 2u);
}

/* The automatic test: its CSV's lines, the CSV's close, the report's lines
 * and the report's close, in that order, as panel_log.c writes them. */
static uint32_t test_run(bool csv_line_bad, bool csv_close_bad,
                         bool txt_line_bad, bool txt_close_bad)
{
    uint32_t faults = 0u;
    bool failed = false;           /* s_test_failed, cleared at the OPEN */

    if (csv_line_bad) {
        faults += error_card_csv_edge(true, failed, false) ? 1u : 0u;
        failed = true;
    }
    faults += error_card_close_edge(csv_close_bad, failed, false) ? 1u : 0u;
    failed = failed || csv_close_bad;
    if (txt_line_bad) {
        faults += error_card_csv_edge(true, failed, true) ? 1u : 0u;
        failed = true;
    }
    faults += error_card_close_edge(txt_close_bad, failed, true) ? 1u : 0u;
    return faults;
}

TEST_CASE(the_tests_failed_close_is_one_error)
{
    error_watch_t watch;

    CHECK_EQ(test_run(false, false, false, false), 0u);
    CHECK_EQ(test_run(false, true, false, false), 1u);    /* the CSV's */
    CHECK_EQ(test_run(true, true, false, false), 1u);     /* once a run */
    CHECK_EQ(test_run(false, false, true, true), 0u);     /* the report's */
    CHECK_EQ(test_run(false, false, false, true), 0u);
    CHECK_EQ(test_run(false, true, true, true), 1u);

    bench(BENCH_SAFE, true);
    error_watch_init(&watch, 0u);
    do_follow(&watch, test_run(false, true, false, false));
    CHECK_EQ(g.disarms, 0u);
    CHECK(!g.on);
}

/* -------------------------------------------------------- the programmers */

/* An outcome taken as kst_publish() takes it, ended with @p result. */
static void kst_outcome(kst_link_t *k, uint8_t result)
{
    kst_link_outcome_t o;

    k->outcome.kind = KST_LINK_OUT_DONE;
    k->outcome.result = result;
    k->outcome_new = true;
    CHECK(kst_link_outcome(k, &o));
}

/* A session fault on a SAFE bench, its port holding the supply: no DISARM;
 * with an IOmcu of 4.31 the supply off and the page's OFF; with an older
 * one the supply left to the port. */
TEST_CASE(a_session_fault_is_an_error)
{
    kst_link_t k;
    hitec_prog_gate_t gate;
    error_watch_t watch;

    for (int f = 0; f < 2; ++f) {
        bench(BENCH_SAFE, true);
        g.port = true;
        g.minor = (f == 0) ? LINK_MINOR_PORT_CUT : LINK_MINOR_PORT_CUT - 1u;
        kst_link_init(&k);
        hitec_prog_gate_init(&gate);
        error_watch_init(&watch, 0u);
        kst_outcome(&k, KST_SES_OK);
        kst_outcome(&k, KST_SES_ERR_ABORTED);
        do_follow(&watch, kst_link_faults(&k)
                                       + hitec_prog_gate_faults(&gate));
        CHECK(g.on);
        CHECK_EQ(g.cuts, 0u);
        kst_outcome(&k, KST_SES_ERR_VERIFY);
        CHECK_EQ(kst_link_faults(&k), 1u);
        do_follow(&watch, kst_link_faults(&k)
                                       + hitec_prog_gate_faults(&gate));
        CHECK_EQ(g.disarms, 0u);
        CHECK_EQ(g.on, f != 0);
        CHECK_EQ(g.cuts, (f == 0) ? 1u : 0u);
        /* A Hitec fault, counted by the model's gate. */
        g.on = true;
        atomic_fetch_add(&gate.faults, 1u);
        do_follow(&watch, kst_link_faults(&k)
                                       + hitec_prog_gate_faults(&gate));
        CHECK_EQ(g.on, f != 0);
        CHECK_EQ(g.cuts, (f == 0) ? 2u : 0u);
    }
    CHECK_EQ(kst_link_faults(NULL), 0u);
}

/* ---------------------------------------------------- an INA3221 channel */

/* SENSE_LIMIT at link 4.27 and later: a window over its limit read, its
 * trip taken as panel_limit.c takes it. */
TEST_CASE(an_ina3221_channel_over_its_limit_is_an_error)
{
    sense_page_t pg;
    sense_limit_link_t ll;
    uint16_t want[LINK_SL_KEPT] = { 0 };
    uint32_t now = 1000u;

    bench(BENCH_ARMED, true);
    sense_page_init(&pg);
    sense_limit_link_init(&ll);
    sense_limit_link_came_up(&ll, LINK_MINOR_SENSE_LIMIT, now);
    want[2] = 500u;
    sense_limit_link_want(&ll, want, now);
    for (int i = 0; i < 40; ++i) {
        if (i == 30) {
            pg.limit[LINK_SL_OVER] = (uint16_t)(1u << 2);
            ++pg.limit[LINK_SL_OVER_COUNT];
        }
        for (int x = 0; x < 2; ++x) {
            sense_limit_link_op_t op;
            uint16_t regs[LINK_MAX_REGS];
            if (!sense_limit_link_next(&ll, now, &op)) {
                break;
            }
            memset(regs, 0, sizeof(regs));
            int r = SENSE_LIMIT_LINK_ACK;
            if (op.write) {
                const uint8_t nack =
                    sense_limit_write(&pg, op.off, op.n, op.regs);
                r = (nack == 0u) ? SENSE_LIMIT_LINK_ACK : (int)nack;
            } else {
                sense_limit_read(&pg, op.off, op.n, regs);
            }
            sense_limit_link_done(&ll, r, op.write ? NULL : regs, now);
        }
        if (sense_limit_link_take_trip(&ll) != 0u) {
            do_stop();
        }
        now += 50u;
    }
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
    CHECK_EQ(g.cuts, 1u);
}

/* ------------------------------------------------- the SUPPLY page's word */

static outputs_t     g_out;
static supply_page_t g_pg;
static pdmini_t      g_drv;
static supply_link_t g_sl;
static uint32_t      g_now;
static unsigned      g_offs;   /* OUTPUT 0 written to the page */

/* supply_adopt(): a port's command the link took from the page. */
static void adopt(void)
{
    bool on;
    uint16_t mv;
    uint16_t ma;

    if (supply_link_adopted(&g_sl, &on, &mv, &ma) && on != g.on) {
        supply_link_command(&g_sl, on, mv, ma);
        g.on = on;
    }
}

/* The IOmcu's pass, then the panel's read of the page and its adoption of
 * a port's command, as supply_link_service() reads it. */
static void far_read(bool beat)
{
    uint16_t regs[LINK_SP_COUNT];
    uint16_t w[4];

    supply_page_step(&g_pg, beat, supply_page_enabled(&g_pg) ? &g_drv : NULL);
    if (supply_page_wire_ready(&g_pg, w)) {
        supply_page_follow(&g_pg, supply_page_enabled(&g_pg) ? &g_drv : NULL);
        (void)supply_page_wire_write(&g_pg, &g_out);
    }
    supply_page_read(&g_pg, 0u, LINK_SP_COUNT, regs);
    supply_link_read(&g_sl, regs, g_now);
    adopt();
}

/* supply_page_off(): an event that ends the panel's ON. */
static void page_off(uint16_t ev)
{
    const error_bench_t b = bench_now();
    const bool far_off = (ev & (SUPPLY_LINK_EV_ON_LOST | SUPPLY_LINK_EV_TRIPPED
                                | SUPPLY_LINK_EV_SAGGED)) != 0u;

    if (error_sites_page_keeps(&b, far_off) && supply_link_keep(&g_sl)) {
        adopt();
    } else {
        supply_off();
    }
}

/* The control task's service of the page: every write owed and a read
 * after each, the events taken as supply_link_alerts() takes them. */
static void serve(bool beat)
{
    for (int k = 0; k < 8; ++k) {
        uint8_t off = 0u;
        uint8_t n = 0u;
        uint16_t regs[LINK_SP_FLAGS];
        if (supply_link_next(&g_sl, &off, &n, regs) == SUPPLY_LINK_W_NONE) {
            far_read(beat);
            if (supply_link_next(&g_sl, &off, &n, regs)
                == SUPPLY_LINK_W_NONE) {
                break;
            }
        }
        if (off == (uint8_t)LINK_SP_OUTPUT && regs[0] == 0u) {
            ++g_offs;
        }
        supply_page_follow(&g_pg, supply_page_enabled(&g_pg) ? &g_drv : NULL);
        const uint8_t nack =
            supply_page_write(&g_pg, off, n, regs, &g_out, beat);
        supply_link_written(&g_sl, nack == 0u ? SUPPLY_LINK_ACK : (int)nack);
        far_read(beat);
        g_now += 50u;
    }
    const uint16_t ev = supply_link_events(&g_sl);
    if ((ev & (SUPPLY_LINK_EV_ON_REFUSED | SUPPLY_LINK_EV_ON_LOST
               | SUPPLY_LINK_EV_TRIPPED | SUPPLY_LINK_EV_SAGGED)) != 0u) {
        page_off(ev);
    }
    if (supply_link_ev_error(ev)) {
        do_stop();
    }
}

static uint16_t page_reg(unsigned i)
{
    uint16_t v = 0u;

    supply_page_read(&g_pg, (uint8_t)i, 1u, &v);
    return v;
}

/* An ARMED bench with the PD mini's output on through the page. */
static void supply_up(uint16_t minor)
{
    static const supply_wiring_t k_wired = { true, 8, 9, 1u };

    bench(BENCH_ARMED, false);
    g.minor = minor;
    g.link = &g_sl;
    outputs_init(&g_out, 0u);
    outputs_reserve_pins(&g_out, (uint64_t)1u << 3);
    supply_page_init(&g_pg);
    pdmini_init(&g_drv, NULL, 0u);
    supply_link_init(&g_sl);
    g_now = 1000u;
    g_offs = 0u;
    supply_link_wire(&g_sl, &k_wired);
    supply_link_command(&g_sl, true, MV, MA);
    g.on = true;
    serve(true);
    CHECK_EQ(page_reg(LINK_SP_OUTPUT), 1u);
    CHECK_EQ(g.disarms, 0u);
}

/* The PD mini's own protection, and an ON the IOmcu let go: SAFE, and the
 * page's OFF written to an IOmcu of 4.31 although the page reads off. */
TEST_CASE(the_supply_pages_errors_reach_the_rule)
{
    for (int f = 0; f < 2; ++f) {
        const uint16_t minor =
            (f == 0) ? LINK_MINOR_PORT_CUT : LINK_MINOR_PORT_CUT - 1u;
        supply_up(minor);
        g_drv.st.tripped = true;
        serve(true);
        CHECK_EQ(g.disarms, 1u);
        CHECK(!g.on);
        CHECK_EQ(g.cuts, (f == 0) ? 1u : 0u);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);

        supply_up(minor);
        far_read(false);                    /* the heartbeat stopped */
        serve(true);
        CHECK_EQ(g.disarms, 1u);
        CHECK(!g.on);
        CHECK_EQ(g.cuts, (f == 0) ? 1u : 0u);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);
    }
}

/* ------------------------------------------- the supply's own faults */

/* A CURRENT TRIP on SUPPLY, and an ON taken across lost touch events, as
 * supply_step() and supply_watch_service() find them. */
TEST_CASE(a_trip_and_a_lost_on_are_errors)
{
    const supply_limits_t lim = { 21.0f, 5.0f, 2.5f, 0.0f, 0.1f };
    supply_state_t st;
    supply_trip_t t;
    arm_watch_t aw;
    int trips = 0;

    bench(BENCH_ARMED, true);
    g.port = true;
    g.minor = LINK_MINOR_PORT_CUT - 1u;
    memset(&st, 0, sizeof(st));
    st.output = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    st.i = 2.6f;
    supply_trip_reset(&t);
    for (int i = 0; i < 3; ++i) {
        if (supply_trip_step(&t, &lim, &st, 0.05f) != SUPPLY_TRIP_NONE) {
            do_cut();
            ++trips;
        }
    }
    CHECK_EQ(trips, 1);
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);                 /* a trip is the panel's, port or not */

    bench(BENCH_ARMING, true);
    arm_watch_begin(&aw, 5u, 9u, 2u);
    CHECK(!arm_watch_lost(&aw, 5u, 9u, 1u));
    if (arm_watch_lost(&aw, 6u, 9u, 1u)) {
        do_stop();
    }
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.on);
    CHECK_EQ(g.cuts, 1u);
}

/* The trip and an ON across lost touch events with a port holding the
 * supply, on each IOmcu, the PD mini and the model, SAFE and ARMED: the
 * trip switches the output off whoever holds it; the lost ON leaves the
 * PD mini to a port on an IOmcu older than 4.31.  Both ask DISARM of an
 * ARMED bench, of a SAFE one never. */
TEST_CASE(a_port_keeps_its_supply_through_a_lost_on_not_a_trip)
{
    for (int k = 0; k < 16; ++k) {
        const bool old  = (k & 1) != 0;
        const bool real = (k & 2) != 0;
        const bool port = (k & 4) != 0;
        const bool live = (k & 8) != 0;
        arm_watch_t aw;

        bench(live ? BENCH_ARMED : BENCH_SAFE, true);
        g.port = port;
        g.real = real;
        g.minor = old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT;
        do_cut();
        CHECK(!g.on);
        CHECK_EQ(g.disarms, live ? 1u : 0u);
        CHECK_EQ(g.cuts, (!old && real) ? 1u : 0u);

        bench(live ? BENCH_ARMED : BENCH_SAFE, true);
        g.port = port;
        g.real = real;
        g.minor = old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT;
        arm_watch_begin(&aw, 5u, 9u, 2u);
        if (arm_watch_lost(&aw, 5u, 10u, 1u)) {
            do_stop();
        }
        CHECK_EQ(g.on, old && real && port);
        CHECK_EQ(g.disarms, live ? 1u : 0u);
        CHECK_EQ(g.cuts, (!old && real) ? 1u : 0u);
        CHECK(!arm_watch_lost(&aw, 6u, 11u, 1u));   /* once */
    }
}

/* A silent supply and one without a reading, a port holding it, on each
 * IOmcu, the PD mini and the model, SAFE and ARMED.  Kept for the port, the
 * fault acts once while it stands, again when it changes, and the output
 * goes off once the port lets go; not kept, the output goes off at once. */
TEST_CASE(a_reading_fault_a_port_keeps_acts_once)
{
    for (int k = 0; k < 16; ++k) {
        const bool old   = (k & 1) != 0;
        const bool real  = (k & 2) != 0;
        const bool live  = (k & 4) != 0;
        const bool blind = (k & 8) != 0;
        const bool keeps = old && real;
        const unsigned d = live ? 1u : 0u;

        if (blind && !real) {
            continue;                   /* the model has no blind reading */
        }
        bench(live ? BENCH_ARMED : BENCH_SAFE, true);
        g.port = true;
        g.real = real;
        g.minor = old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT;
        const error_reading_t want =
            blind ? ERROR_READING_BLIND : ERROR_READING_SILENT;
        CHECK_EQ(do_reading(blind, blind), want);
        CHECK_EQ(g.on, keeps);
        CHECK_EQ(g.disarms, d);
        CHECK_EQ(g.cuts, (!old && real) ? 1u : 0u);
        CHECK_EQ(g.told, want);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(do_reading(blind, blind), ERROR_READING_NONE);
        }
        CHECK_EQ(g.disarms, d);
        if (!keeps) {
            continue;
        }
        if (blind) {
            /* Another fault: named and acted on again. */
            CHECK_EQ(do_reading(false, false), ERROR_READING_SILENT);
            CHECK(g.on);
            CHECK_EQ(g.disarms, 2u * d);
            CHECK_EQ(do_reading(false, false), ERROR_READING_NONE);
        }
        /* The port lets go: the output off. */
        g.port = false;
        CHECK(do_reading(false, blind) != ERROR_READING_NONE);
        CHECK(!g.on);
        CHECK_EQ(g.disarms, (blind ? 3u : 2u) * d);
        CHECK_EQ(do_reading(false, blind), ERROR_READING_NONE);
        CHECK_EQ(g.told, ERROR_READING_NONE);
        CHECK_EQ(g.cuts, 0u);
    }
}

/* A fault that clears while kept starts afresh: a later one acts again. */
TEST_CASE(a_kept_fault_that_clears_acts_again)
{
    bench(BENCH_ARMED, true);
    g.port = true;
    g.minor = LINK_MINOR_PORT_CUT - 1u;
    CHECK_EQ(do_reading(false, false), ERROR_READING_SILENT);
    CHECK_EQ(do_reading(true, false), ERROR_READING_NONE);
    CHECK_EQ(g.told, ERROR_READING_NONE);
    CHECK_EQ(do_reading(false, false), ERROR_READING_SILENT);
    CHECK_EQ(g.disarms, 2u);
    CHECK(g.on);
}

/* An event off at the far end is followed off whoever holds the supply;
 * an ON refused alone is left to a port the rule keeps. */
TEST_CASE(a_page_event_keeps_only_a_refused_on)
{
    for (int k = 0; k < 16; ++k) {
        const error_bench_t b = {
            true, (k & 1) != 0, (k & 2) != 0, (k & 4) != 0, true,
            ((k & 8) != 0) ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT,
            false,
        };
        const bool kept = b.port_holds && b.supply_real
                          && b.far_minor < LINK_MINOR_PORT_CUT;
        CHECK(!error_sites_page_keeps(&b, true));
        CHECK_EQ(error_sites_page_keeps(&b, false), kept);
        CHECK_EQ(error_sites_keeps(&b), b.supply_on && kept);
    }
}

/* A port's ON on a page whose wiring the settings' refused: the ON is
 * refused, an error, once the port lets go: the panel writes no OFF over the
 * port's ON until then, on any IOmcu.  SAFE and ARMED. */
TEST_CASE(an_on_refused_under_a_ports_on_is_left_to_the_port)
{
    static const supply_wiring_t k_bad = { true, 3, 9, 1u };   /* reserved */

    for (int k = 0; k < 4; ++k) {
        const bool old  = (k & 1) != 0;
        const bool live = (k & 2) != 0;
        const unsigned d = live ? 1u : 0u;

        supply_up(old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT);
        g.bank_armed = live;
        supply_off();
        serve(true);
        supply_link_wire(&g_sl, &k_bad);
        serve(true);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);
        /* The port takes the supply on the page's wiring, and writes. */
        supply_page_hold(&g_pg, true);
        CHECK(supply_page_port_set(&g_pg, true, MV, MA, true));
        supply_page_port_writes(&g_pg, true);
        g.session = true;
        const unsigned offs = g_offs;
        for (int i = 0; i < 4; ++i) {
            serve(true);
            g_now += 100u;
        }
        CHECK_EQ(g.disarms, 0u);
        CHECK_EQ(g_offs, offs);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 1u);
        CHECK(g.on);
        CHECK_EQ(g.cuts, 0u);
        /* The port lets go: refused now, and off. */
        supply_page_port_writes(&g_pg, false);
        supply_page_hold(&g_pg, false);
        g.session = false;
        for (int i = 0; i < 3; ++i) {
            serve(true);
        }
        CHECK_EQ(g.disarms, d);
        CHECK(g_offs > offs);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);
        CHECK(!g.on);
    }
}

/* The PD mini's own protection under a port's ON: off at the module, and
 * followed off here on each IOmcu, port or not. */
TEST_CASE(a_trip_of_the_module_is_followed_off_under_a_port)
{
    for (int f = 0; f < 2; ++f) {
        supply_up((f == 0) ? LINK_MINOR_PORT_CUT : LINK_MINOR_PORT_CUT - 1u);
        supply_page_hold(&g_pg, true);
        CHECK(supply_page_port_set(&g_pg, true, MV, MA, true));
        g.session = true;
        serve(true);
        CHECK(g.on);
        CHECK_EQ(g.disarms, 0u);
        g_drv.st.tripped = true;
        serve(true);
        CHECK_EQ(g.disarms, 1u);
        CHECK(!g.on);
        CHECK_EQ(g.cuts, (f == 0) ? 1u : 0u);
    }
}

/* --------------------------------------- who holds the supply, as read */

/* A port's ON on the page, a write running, and @p held the page's hold. */
static void port_on(bool held)
{
    supply_page_hold(&g_pg, held);
    if (held) {
        CHECK(supply_page_port_set(&g_pg, true, MV, MA, true));
        supply_page_port_writes(&g_pg, true);
    }
}

/* A programmer with a port or none, the SUPPLY page's flag clear or set,
 * on each IOmcu, SAFE and ARMED.  On an IOmcu older than 4.31 the supply
 * is kept only while both say a port holds it: a session whose port the
 * page does not hold gets the OFF, and so does a flag with no session.
 * From 4.31 the OFF is written either way and the page holds it for a
 * write. */
TEST_CASE(the_page_and_the_session_together_keep_the_supply)
{
    for (int k = 0; k < 16; ++k) {
        const bool old  = (k & 1) != 0;
        const bool flag = (k & 2) != 0;
        const bool live = (k & 4) != 0;
        const bool kept = old && flag && (k & 8) != 0;

        supply_up(old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT);
        g.bank_armed = live;
        g.session = (k & 8) != 0;
        port_on(flag);
        serve(true);
        CHECK_EQ(supply_link_port_holds(&g_sl, true, g.session),
                 flag && g.session);
        CHECK(g.on);
        const unsigned offs = g_offs;
        do_stop();
        CHECK_EQ(g.disarms, live ? 1u : 0u);
        CHECK_EQ(g.on, kept);
        CHECK_EQ(g.cuts, old ? 0u : 1u);
        serve(true);
        CHECK_EQ(g_offs > offs, !kept);
        /* Kept for the port, the page stays on; given the OFF, the page
         * reads 0 at once, a held OFF included. */
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), kept ? 1u : 0u);
        CHECK_EQ(supply_page_off_held(&g_pg), !kept && flag);
    }
}

/* A port that takes the supply between two reads: the panel reads no hold
 * yet, and an error writes the OFF on every IOmcu.  From 4.31 the page
 * holds it for the write that runs.  The page here is 4.31's; an older
 * IOmcu takes the OFF at once, which is the panel's side alike. */
TEST_CASE(a_port_that_takes_the_supply_between_reads_gets_the_off)
{
    for (int f = 0; f < 2; ++f) {
        const bool old = f != 0;

        supply_up(old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT);
        g.session = true;
        port_on(true);                  /* not read yet */
        CHECK(!supply_link_port_holds(&g_sl, true, true));
        const unsigned offs = g_offs;
        do_stop();
        CHECK(!g.on);
        CHECK_EQ(g.cuts, old ? 0u : 1u);
        serve(true);
        CHECK(g_offs > offs);
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);
        CHECK(supply_page_off_held(&g_pg));
    }
}

/* A port that lets go before the next read of the SUPPLY page: once the
 * session has ended, the OFF is written on every IOmcu.  From 4.31 it is
 * written whatever the panel read.  On an older IOmcu, while neither the
 * SUPPLY page nor the port's own page has been read since, the panel
 * still reads the hold, and an error in that gap leaves the output on
 * (docs/Safety.md, Errors). */
TEST_CASE(a_port_that_lets_go_between_reads)
{
    for (int f = 0; f < 4; ++f) {
        const bool old = (f & 1) != 0;
        const bool ended = (f & 2) != 0;

        supply_up(old ? LINK_MINOR_PORT_CUT - 1u : LINK_MINOR_PORT_CUT);
        g.session = true;
        port_on(true);
        serve(true);
        CHECK(supply_link_port_holds(&g_sl, true, true));
        supply_page_port_writes(&g_pg, false);
        supply_page_hold(&g_pg, false); /* not read yet */
        g.session = !ended;
        do_stop();
        CHECK_EQ(g.on, old && !ended);
        CHECK_EQ(g.cuts, old ? 0u : 1u);
        serve(true);
        CHECK(!supply_link_port_holds(&g_sl, true, true));
        CHECK_EQ(page_reg(LINK_SP_OUTPUT), (old && !ended) ? 1u : 0u);
    }
}

/* ------------------------------------------------ the operator's endings */

/* OUTPUT OFF on SUPPLY and STOP TEST end a run on an ARMED bench that
 * stays ARMED: the OFF is the operator's, the run's files end whole, and
 * the pump finds no error. */
TEST_CASE(the_operators_endings_leave_the_bench_armed)
{
    static const bool k_none[] = { false, false, false, false };
    static const bool k_txt[] = { false, false, true, true };
    const log_sink_t sink = { card_write, card_flush, NULL };
    log_writer_t w;
    error_watch_t cards;
    error_watch_t progs;
    uint32_t faults = 0u;

    supply_up(LINK_MINOR_PORT_CUT);
    error_watch_init(&cards, 0u);
    error_watch_init(&progs, 0u);
    /* OUTPUT OFF: supply_switch(false), and the page reads it off. */
    supply_off();
    for (int i = 0; i < 20; ++i) {
        serve(true);
        do_follow(&cards, faults);
        do_follow(&progs, 0u);
        CHECK_EQ(do_reading(true, i > 10),
                 ERROR_READING_NONE);
        g_now += 100u;
    }
    CHECK_EQ(page_reg(LINK_SP_OUTPUT), 0u);
    /* STOP TEST: the CSV and the report written whole, the run log too. */
    g_card_gone = false;
    log_writer_init(&w, &sink);
    for (int i = 0; i < 40; ++i) {
        log_pass(&w, true, 0.05f * (float)i, &faults);
    }
    faults += test_lines(k_none, k_txt, 4);
    do_follow(&cards, faults);
    CHECK_EQ(faults, 0u);
    CHECK_EQ(g.disarms, 0u);
    CHECK_EQ(g.cuts, 0u);
    CHECK(arming_link_needed(&g.a, g.bank_armed));
}

int main(void)
{
    RUN(every_site_against_every_bench);
    RUN(the_iomcu_defers_from_4_31_with_the_supply_page);
    RUN(a_cut_judges_the_bench_with_the_output_off);
    RUN(a_reading_is_judged_with_the_output_on);
    RUN(a_counter_acts_once_per_change);
    RUN(every_error_writes_the_ports_own_supply_off);
    RUN(nothing_is_done_without_a_bench);
    RUN(the_run_logs_failed_write_is_one_error);
    RUN(the_tests_failed_csv_is_one_error);
    RUN(a_session_fault_is_an_error);
    RUN(an_ina3221_channel_over_its_limit_is_an_error);
    RUN(the_supply_pages_errors_reach_the_rule);
    RUN(a_trip_and_a_lost_on_are_errors);
    RUN(a_port_keeps_its_supply_through_a_lost_on_not_a_trip);
    RUN(a_reading_fault_a_port_keeps_acts_once);
    RUN(a_kept_fault_that_clears_acts_again);
    RUN(a_page_event_keeps_only_a_refused_on);
    RUN(an_on_refused_under_a_ports_on_is_left_to_the_port);
    RUN(a_trip_of_the_module_is_followed_off_under_a_port);
    RUN(the_page_and_the_session_together_keep_the_supply);
    RUN(a_port_that_takes_the_supply_between_reads_gets_the_off);
    RUN(a_port_that_lets_go_between_reads);
    RUN(a_failed_close_is_one_error_a_run);
    RUN(the_tests_failed_close_is_one_error);
    RUN(the_operators_endings_leave_the_bench_armed);
    return test_summary("error_sites");
}
