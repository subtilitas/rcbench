/*
 * The automatic servo test against recorded runs on the bench.
 *
 * Under test: runs replayed from the recordings of an MG90S micro servo,
 * a 1102HB digital servo and an MS24 digital servo on the PD mini
 * (fixtures/servo-mg90s.csv, servo-1102hb.csv, servo-ms24-pdmini.csv): a
 * move seen at the threshold, a move under it not failed, a run NOT
 * MEASURABLE and not FAIL, spells of constant current passing, and a
 * standard servo passing.
 *
 * The rig is servo_test_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_rig.h"
#include "ui_text.h"

/* -------------------------------------------- replays of runs on the bench */

/*
 * Two runs on the bench on 0.13.0 with the PD mini, from a tester: an
 * MG90S micro servo (fixtures/servo-mg90s.csv) and a 1102HB digital
 * (fixtures/servo-1102hb.csv).  Each ran 4.80 and 6.00 V for 60 s and the
 * brown-out walk: LENGTH BY TIME, SETTLE 500 ms, DWELL 200 ms, the ends
 * 1100 and 1900 us, current limit 2.00 A.  0.13.0 took movement to be a
 * reading 0.10 A from the level before the command and saw none: every
 * move ran out at 3000 ms, late, and both runs read FAIL.  The files are
 * the runs' CSVs, trimmed: every SET, SETTLE and IDLE row, and of each
 * command's MOVE and HOLD rows those within 2.0 s of its first and its
 * last, the reading just before the next command.
 *
 * A replay plays a recording back against the commands the run gives
 * now.  Each command starts the recorded response to the same command: a
 * step's start its SET, SETTLE and IDLE rows, a move the next recorded
 * move to the same end.  Rows arrive at their recorded time after the
 * recorded command, which fell between two readings and is taken at their
 * middle.  Past the end of a response its last reading repeats every
 * 105 ms, and so does it across a gap the trim left.  Moves end at their
 * arrival where 0.13.0 waited 3000 ms, so a step makes more of them than
 * were recorded: they start over from the first move end to end.
 * Brown-out voltages under the recorded 5.00 V replay its response, so a
 * replay says nothing about a servo below 5.00 V.
 *
 * A third recording is a whole file, untrimmed: an MS24 digital servo on
 * 0.14.0 (fixtures/servo-ms24-pdmini.csv), 4.80 and 6.00 V for 60 s and the
 * brown-out walk down to 3.00 V, eleven voltages, each replayed as it was
 * recorded.  The PD mini reported constant current in two spells of five
 * readings, 0.43 s each, at 0.004 to 0.342 A: the mode column is replayed
 * with the readings.
 *
 * With RP_PRINT set in the environment each replay prints its report: the
 * sample in docs/Servo.md is the MG90S's, with TRAVEL TIME at 800 ms.
 */
#define RP_ROWS     2048u
#define RP_SEGS     48u
#define RP_STEPS    16u
#define RP_EVERY_MS 105u
#define RP_GAP_MS   400u        /* longer between two rows is a trim */

typedef struct {
    uint32_t at;            /* ms after the command */
    float    v, i;
    bool     set;           /* in SET: the set point not yet read back */
    uint8_t  mode;          /* 1 CV, 2 CC, as the supply reported it */
} rp_row_t;

typedef struct {
    uint16_t cmd;
    uint16_t first, n;
} rp_seg_t;

typedef struct {
    bool     brownout;
    rp_seg_t prep;          /* SET, SETTLE and IDLE */
    rp_seg_t moves[RP_SEGS];
    unsigned n_moves;
} rp_step_t;

typedef struct {
    rp_row_t  rows[RP_ROWS];
    unsigned  n_rows;
    rp_step_t steps[RP_STEPS];
    unsigned  n_steps;
} rp_rec_t;

static bool rp_is_prep(const char *phase)
{
    return strcmp(phase, "SET") == 0 || strcmp(phase, "SETTLE") == 0
           || strcmp(phase, "IDLE") == 0;
}

/* A recording into @p r, by the command each row answers. */
static bool rp_load(rp_rec_t *r, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    char line[256];
    bool ok = fgets(line, sizeof(line), f) != NULL;     /* the header */
    char test_was[16] = "";
    unsigned step_was = 0u;
    uint32_t t_was = 0u, origin = 0u;
    rp_step_t *st = NULL;
    rp_seg_t *seg = NULL;
    while (ok && fgets(line, sizeof(line), f) != NULL) {
        double ts, set, v, lim, i;
        char test[16], phase[16], mode[8];
        unsigned step, cmd;
        if (sscanf(line, "%lf;%15[^;];%u;%15[^;];%u;;%lf;%lf;%lf;%lf;%*f;"
                         "%7[^;]", &ts, test, &step, phase, &cmd, &set, &v,
                   &lim, &i, mode) != 10) {
            ok = false;
            break;
        }
        const uint32_t t = (uint32_t)lround(ts * 1000.0);
        /* The command fell between the last reading and this one. */
        const uint32_t mid = (st != NULL) ? t_was + (t - t_was) / 2u : t;
        if (st == NULL || strcmp(test, test_was) != 0 || step != step_was) {
            if (r->n_steps >= RP_STEPS) {
                ok = false;
                break;
            }
            st = &r->steps[r->n_steps++];
            st->brownout = strcmp(test, "BROWN-OUT") == 0;
            seg = &st->prep;
            seg->cmd = (uint16_t)cmd;
            seg->first = (uint16_t)r->n_rows;
            origin = mid;
            snprintf(test_was, sizeof(test_was), "%s", test);
            step_was = step;
        } else if (!rp_is_prep(phase)
                   && (seg == &st->prep || cmd != seg->cmd)) {
            if (st->n_moves >= RP_SEGS) {
                ok = false;
                break;
            }
            seg = &st->moves[st->n_moves++];
            seg->cmd = (uint16_t)cmd;
            seg->first = (uint16_t)r->n_rows;
            origin = mid;
        }
        if (r->n_rows >= RP_ROWS) {
            ok = false;
            break;
        }
        r->rows[r->n_rows].at = t - origin;
        r->rows[r->n_rows].v  = (float)v;
        r->rows[r->n_rows].i  = (float)i;
        r->rows[r->n_rows].set = strcmp(phase, "SET") == 0;
        r->rows[r->n_rows].mode = (strcmp(mode, "CC") == 0) ? 2u : 1u;
        ++r->n_rows;
        ++seg->n;
        t_was = t;
    }
    fclose(f);
    return ok && r->n_steps > 0u;
}

typedef struct {
    const rp_rec_t  *rec;
    float            scale;     /* every current, times this */
    const rp_step_t *step;
    const rp_seg_t  *seg;       /* the response playing */
    uint32_t         seg_at;    /* its command */
    unsigned         next;      /* its next row */
    unsigned         cursor;    /* the next recorded move to look at */
    float            v, i;      /* the last reading */
    uint8_t          mode;      /* and the supply's mode in it */
    uint32_t         last_at;
    uint16_t         samples;
    float            set_v;     /* the set point asked */
    float            set_read;  /* and read back */
} rp_play_t;

/* The recorded step for the run's step: the characterisation steps in
 * order, the last for any after it; the brown-out's for every voltage. */
static const rp_step_t *rp_step_for(const rp_rec_t *r, bool brownout,
                                    unsigned idx)
{
    const rp_step_t *found = NULL;
    unsigned k = 0u;
    for (unsigned s = 0; s < r->n_steps; ++s) {
        if (r->steps[s].brownout != brownout) {
            continue;
        }
        found = &r->steps[s];
        if (k++ == idx) {
            break;
        }
    }
    return found;
}

static void rp_command(rp_play_t *p, const servo_test_t *t,
                       const servo_test_do_t *d, uint32_t now)
{
    if (d->set || p->step == NULL) {
        /* A brown-out voltage by its place in the walk. */
        const servo_test_step_t *s = &t->steps[t->step];
        p->step = rp_step_for(p->rec, s->brownout,
                              s->brownout ? (unsigned)t->step
                                                - t->cfg.step_count
                                          : t->step);
        p->seg = (p->step != NULL) ? &p->step->prep : NULL;
        p->cursor = 0u;
    } else {
        const rp_seg_t *m = NULL;
        for (unsigned k = 0; m == NULL && k < 2u * p->step->n_moves; ++k) {
            if (p->cursor >= p->step->n_moves) {
                p->cursor = 1u;
            }
            if (p->step->moves[p->cursor].cmd == d->cmd_us) {
                m = &p->step->moves[p->cursor];
            }
            ++p->cursor;
        }
        CHECK(m != NULL);
        p->seg = m;
    }
    p->seg_at = now;
    p->next = 0u;
}

/* The readings due by @p now, to the run. */
static void rp_readings(rp_play_t *p, servo_test_t *t, uint32_t now)
{
    for (;;) {
        uint32_t at = 0u;
        const rp_row_t *row = NULL;
        if (p->seg != NULL && p->next < p->seg->n) {
            row = &p->rec->rows[p->seg->first + p->next];
            at = p->seg_at + row->at;
            /* The rows a trim took out: the last reading repeats. */
            if ((int32_t)(at - p->last_at) > (int32_t)RP_GAP_MS) {
                row = NULL;
            }
        }
        if (row != NULL) {
            if ((int32_t)(at - now) > 0) {
                return;
            }
            p->v = row->v;
            p->i = row->i * p->scale;
            p->mode = row->mode;
            /* The set point read back where the recording read it. */
            if (!row->set) {
                p->set_read = p->set_v;
            }
            ++p->next;
        } else {
            at = p->last_at + RP_EVERY_MS;
            if ((int32_t)(at - now) > 0) {
                return;
            }
        }
        if ((int32_t)(at - p->last_at) <= 0) {
            at = p->last_at + 1u;
        }
        servo_test_reading_t r;
        memset(&r, 0, sizeof(r));
        r.v = p->v;
        r.i = p->i;
        r.set_v = p->set_read;
        r.set_i = 2.0f;
        r.mode = (p->mode != 0u) ? p->mode : 1u;
        r.output = true;
        r.online = true;
        r.ok = true;
        r.samples = ++p->samples;
        r.taken_ms = at;
        p->last_at = at;
        servo_test_reading(t, &r, 0u);
    }
}

/* The tester's settings, read by the PD mini. */
static void rp_cfg(servo_test_cfg_t *c)
{
    cfg_defaults(c);
    c->i_limit  = 2.0f;
    c->brownout = true;
    c->by_moves = false;
    c->time_s   = 60u;
    servo_test_meter_pdmini(&c->meter);
}

/* A run of @p c against recording @p r, its report into @p report. */
static void rp_run(servo_test_t *t, const rp_rec_t *r, float scale,
                   const servo_test_cfg_t *c, char *report, size_t cap)
{
    rp_play_t p;
    memset(&p, 0, sizeof(p));
    p.rec = r;
    p.scale = scale;
    uint32_t now = 1000u;
    p.last_at = now;
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    servo_test_init(t);
    CHECK_EQ(servo_test_start(t, c, now, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
    size_t len = 0u;
    report[0] = '\0';
    for (int k = 0; k < 100000 && !servo_test_drained(t); ++k) {
        now += 10u;
        rp_readings(&p, t, now);
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.set) {
            p.set_v = d.set_v;
        }
        if (d.command) {
            rp_command(&p, t, &d, now);
        }
        const char *text = NULL;
        servo_test_out_t o;
        while ((o = servo_test_peek(t, &text)) != SERVO_TEST_OUT_NONE) {
            const size_t n = strlen(text);
            if (o == SERVO_TEST_OUT_TXT && len + n + 2u < cap) {
                memcpy(report + len, text, n);
                len += n;
                report[len++] = '\n';
                report[len] = '\0';
            }
            servo_test_pop(t);
        }
    }
    CHECK(servo_test_drained(t));
}

static rp_rec_t     g_rec;
static servo_test_t g_rp;
static char         g_rp_report[12288];

/* What a characterisation step of a replay must show: every counted move
 * either timed or unseen, none late, at least @p seen_min of them seen. */
static void rp_check_step(const servo_test_step_t *s, unsigned seen_min)
{
    CHECK(s->done);
    CHECK(s->moves >= 20u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK_EQ(s->travels + s->no_rise, s->moves);
    CHECK(s->travels >= seen_min);
}

/* The MG90S draws 0.001 A holding and 0.04 to 0.077 A moving: every move
 * is seen at the 0.020 A threshold, timed, none late, and the run passes.
 * The brown-out walk sees it move at 5.00 V. */
TEST_CASE(a_micro_servo_on_the_pd_mini_is_seen_moving)
{
    CHECK(rp_load(&g_rec, "servo-mg90s.csv"));
    CHECK_EQ(g_rec.n_steps, 3u);
    servo_test_cfg_t c;
    rp_cfg(&c);
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_PASS);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], g_rp.steps[k].moves);
    }
    CHECK(g_rp.steps[2].brownout);
    CHECK(g_rp.steps[2].moved);
    CHECK(strstr(g_rp_report, "Result:         PASS\n") != NULL);
}

/* The 1102HB holds 0.015 to 0.029 A and peaks 0.039 to 0.044 A moving:
 * moves to the high end, from the low end's 0.028 A, never pass the level
 * before them by the 0.020 A threshold.  Those are unseen, neither timed
 * nor late; the moves seen all arrive, and the run reads NOT MEASURABLE,
 * not FAIL. */
TEST_CASE(a_servo_moving_under_the_threshold_is_not_failed)
{
    CHECK(rp_load(&g_rec, "servo-1102hb.csv"));
    CHECK_EQ(g_rec.n_steps, 3u);
    servo_test_cfg_t c;
    rp_cfg(&c);
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_NOT_MEASURABLE);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], 1u);
        CHECK(g_rp.steps[k].no_rise > 0u);
    }
    CHECK(strstr(g_rp_report, "Result:         NOT MEASURABLE - ") != NULL);
    CHECK(strstr(g_rp_report, "Moves arrived    0 late: PASS") != NULL);
    /* As the bench writes it in German, where the tester read FAIL. */
    c.text = ui_text_table(UI_LANG_DE)->servo;
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    CHECK(strstr(g_rp_report, "NICHT BESTANDEN") == NULL);
    CHECK(strstr(g_rp_report, "Ergebnis:        NICHT MESSBAR - bei ")
          != NULL);
}

/* The MS24's whole recording.  Its two spells of constant current, 0.43 s
 * each during a move, end nothing and fail nothing: the run passes as it
 * did on the bench, and the report counts them.  The recorded LIMITS page
 * had STALL AT at 3.00 A over a current limit of 2.00 A, which the report
 * says cannot be reached. */
TEST_CASE(a_recorded_run_with_spells_of_constant_current_passes)
{
    CHECK(rp_load(&g_rec, "servo-ms24-pdmini.csv"));
    CHECK_EQ(g_rec.n_steps, 13u);
    CHECK_EQ(g_rec.n_rows, 1734u);
    unsigned cc_rows = 0u;
    for (unsigned k = 0; k < g_rec.n_rows; ++k) {
        cc_rows += (g_rec.rows[k].mode == 2u) ? 1u : 0u;
    }
    CHECK_EQ(cc_rows, 10u);
    servo_test_cfg_t c;
    rp_cfg(&c);
    c.stall_a = 3.0f;
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_PASS);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], 20u);
        CHECK_EQ(g_rp.steps[k].no_rise, 0u);
    }
    /* The recorded report's figures: idle 0.004 and 0.023 A, the highest
     * reading 0.353 and 0.390 A.  The 6.00 V step's current is still
     * falling through IDLE, so its mean depends on where IDLE begins
     * between two readings. */
    CHECK_NEAR(g_rp.steps[0].idle.sum / (float)g_rp.steps[0].idle.n, 0.004f,
               0.002f);
    CHECK_NEAR(g_rp.steps[1].idle.sum / (float)g_rp.steps[1].idle.n, 0.032f,
               0.012f);
    CHECK_NEAR(g_rp.steps[0].peak_a, 0.353f, 0.0005f);
    CHECK_NEAR(g_rp.steps[1].peak_a, 0.390f, 0.0005f);
    CHECK_NEAR(g_rp.stall_peak_a, 0.390f, 0.0005f);
    /* The walk ran every voltage down to 3.00 V. */
    float moved = 0.0f;
    bool stopped = true;
    CHECK(servo_test_brownout(&g_rp, &moved, &stopped));
    CHECK_NEAR(moved, 3.0f, 0.001f);
    CHECK(!stopped);
    /* Each spell is five readings from its first to its last, 425 and
     * 405 ms; the second step replays its moves until its 60 s are over,
     * so the spell in it can come more than once. */
    CHECK(g_rp.cc_readings >= 10u);
    CHECK_EQ(g_rp.cc_readings % 5u, 0u);
    CHECK_EQ(g_rp.cc_longest_ms, 425u);
    CHECK(!g_rp.stalled);
    char want[96];
    snprintf(want, sizeof(want), "Const. current: %lu supply readings, "
             "longest stretch 425 ms\n", (unsigned long)g_rp.cc_readings);
    CHECK(strstr(g_rp_report, want) != NULL);
    CHECK(strstr(g_rp_report, "Result:         PASS\n") != NULL);
    CHECK(strstr(g_rp_report, "Stall threshold  highest 0.390 A, STALL AT "
                              "3.00 A: PASS\n") != NULL);
    CHECK(strstr(g_rp_report, "STALL AT 3.00 A cannot be reached: current "
                              "limit 2.00 A\n") != NULL);
}

/* The MG90S's recording with every current three times as large, as an
 * MS24 draws: 0.003 A holding, 0.11 to 0.23 A moving.  Every move seen,
 * none late, PASS; TRAVEL TIME at 500 ms is reported against the PD mini's
 * travel times and not checked.  The same readings from a meter whose
 * travel times hold do fail it. */
TEST_CASE(a_standard_servo_on_the_pd_mini_passes)
{
    CHECK(rp_load(&g_rec, "servo-mg90s.csv"));
    servo_test_cfg_t c;
    rp_cfg(&c);
    c.travel_max_ms = 500u;
    rp_run(&g_rp, &g_rec, 3.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_PASS);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], g_rp.steps[k].moves);
    }
    CHECK(strstr(g_rp_report, "limit 500 ms: upper bound, not checked "
                              "against the limit") != NULL);

    c.meter.upper_bound = false;
    rp_run(&g_rp, &g_rec, 3.0f, &c, g_rp_report, sizeof(g_rp_report));
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_FAIL);
}

int main(void)
{
    RUN(a_micro_servo_on_the_pd_mini_is_seen_moving);
    RUN(a_servo_moving_under_the_threshold_is_not_failed);
    RUN(a_standard_servo_on_the_pd_mini_passes);
    RUN(a_recorded_run_with_spells_of_constant_current_passes);
    free(g.csv_text);
    return test_summary("servo_test_replay");
}
