/*
 * The synthetic console log tools/sense_trace.py is tested on: the servo
 * model of servo_sim.h on the modelled INA3221 of fake_ina.h, read by the
 * coprocessor's sensor service and written out by sense_trace.c as the
 * SENSE_TRACE build writes it, one core 1 tick and one core 0 pass a
 * millisecond.
 *
 * The session, in ms from its start, the clock at 20 s:
 *
 *   400    `t` on the console: a trace of the servo holding the centre
 *   900    `x`: that trace ends
 *   2000   the pulse goes from 1500 to 1800 µs; a trace starts
 *   3600   to 1200 µs, 5200 to 1800 µs, 6800 to 1200 µs: each 4 s of
 *          trace more
 *   10800  the trace ends on its time
 *
 * A changed pulse reaches the servo at the next 20 ms frame, which is the
 * time its trigger line carries.  The servo draws servo_sim's travel
 * current until its horn arrives, 1.2 µs of pulse a millisecond; the rail
 * sags 0.4 V an ampere under 6.0 V.  Two lines of the coprocessor's other
 * console output stand between the trace's lines.
 *
 * With a second argument, a servo test CSV file as the panel writes one
 * with the output encoder on: the header, and for each move a row with
 * `travel angle (ms)`, the time from the frame to the horn coming within
 * 12 µs of pulse of its end, on a row 100 ms to 150 ms after it and a
 * clock 12.345 s behind the coprocessor's.
 *
 *   sense_trace_gen LOG [CSV]
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "fake_ina.h"
#include "sense_svc.h"
#include "sense_trace.h"
#include "servo_sim.h"

#define RING     4096u
#define FRAME_US 20000u

static fake_bus_t        fb;
static sense_svc_t       svc;
static sense_trace_t     tr;
static sense_trace_rec_t ring[RING];
static uint64_t          g_us;

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    return g_us;
}

static void recover(void *ctx)
{
    (void)ctx;
}

static bool open_bus(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static void close_bus(void *ctx)
{
    (void)ctx;
}

static sense_err_t ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                         : SENSE_NACK;
}

/* The pulse commanded at @p ms. */
static uint16_t pulse_at(unsigned ms)
{
    if (ms < 2000u) {
        return 1500u;
    }
    return (((ms - 2000u) / 1600u) % 2u == 0u && ms < 8400u) ? 1800u : 1200u;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: sense_trace_gen LOG [CSV]\n");
        return 2;
    }
    FILE *log = fopen(argv[1], "wb");
    FILE *csv = (argc > 2) ? fopen(argv[2], "wb") : NULL;
    if (log == NULL || (argc > 2 && csv == NULL)) {
        fprintf(stderr, "sense_trace_gen: cannot write the output\n");
        return 2;
    }
    if (csv != NULL) {
        fputs("time (s);test;step;phase;command (us);position (us);set (V);"
              "voltage (V);limit (A);current (A);power (W);mode;travel (ms);"
              "angle (deg);travel angle (ms);meter;window;current max (A);"
              "current min (A);voltage min (V);clipped\n", csv);
    }

    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    fake_part_t *part = fake_add(&fb, FAKE_INA3221, 0x40u, 0.1);
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &fb }, now_us, recover, &fb },
        .open = open_bus, .close = close_bus, .ask = ask,
    };
    sense_svc_init(&svc, &io);
    sense_cmd_t cmd;
    sense_snap_t snap;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.ina3221_en = true;
    cmd.parts.ina3221_addr = 0x40u;
    cmd.parts.ina3221_shunt_uohm = 100000u;
    cmd.parts.ina3221_channels = 1u;
    (void)sense_trace_init(&tr, ring, RING);

    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);
    sim.position_us = 1500.0f;

    g_us = 20000000u;
    uint16_t at_pin = 1500u;        /* the pulse the servo is given     */
    uint16_t next = 1500u;          /* and the one the next frame brings */
    uint64_t frame_us = 0u;         /* when it does; 0 for none owed    */
    uint64_t moved_us = 0u;         /* the frame of the move under way  */
    uint64_t row_us = 0u;           /* when its CSV row is due; 0 none  */
    unsigned travel_ms = 0u;

    for (unsigned ms = 0u; ms < 11000u; ++ms) {
        /* Core 0's pass: the console's keys and the pulse rendered. */
        if (ms == 400u) {
            sense_trace_key(&tr, 't', g_us);
        }
        if (ms == 900u) {
            sense_trace_key(&tr, 'x', g_us);
        }
        const uint16_t pulse = pulse_at(ms);
        if (sense_trace_pulse(&tr, 0u, 0u, pulse, g_us)) {
            frame_us = (g_us / FRAME_US + 1u) * FRAME_US;
            next = pulse;
            sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, frame_us, 0u,
                                pulse);
        }
        if (frame_us != 0u && g_us >= frame_us) {
            at_pin   = next;
            moved_us = frame_us;
            frame_us = 0u;
        }

        /* The servo and the rail, then core 1's tick. */
        const float amps = servo_sim_step(&sim, at_pin, ms);
        part->amps[0]  = (double)amps;
        part->volts[0] = 6.0 - 0.4 * (double)amps;
        sense_svc_step(&svc, &cmd, &snap);
        sense_trace_feed(&tr, svc.open ? &svc.sched : NULL);

        /* The horn within 12 µs of its end: the encoder's travel time,
         * on a row 100 ms and up to 50 ms later. */
        if (moved_us != 0u) {
            float off = sim.position_us - (float)at_pin;
            if (off < 0.0f) {
                off = -off;
            }
            if (off <= 12.0f) {
                travel_ms = (unsigned)((g_us - moved_us) / 1000u);
                row_us    = ((g_us + 100000u) / 50000u + 1u) * 50000u;
                moved_us  = 0u;
            }
        }
        if (csv != NULL && row_us != 0u && g_us >= row_us) {
            fprintf(csv, "%.3f;;;;%u;;;;;;;;;;%u;;;;;;\n",
                    (double)(g_us - 12345000u) / 1e6, (unsigned)at_pin,
                    travel_ms);
            row_us = 0u;
        }

        /* The console: other output, then the lines there is room for. */
        if (ms == 150u || ms == 3150u) {
            fputs("rcbench-iomcu: CAN up, 1000000 bit/s, 0 requests served, "
                  "silent 0 ms\r\n", log);
        }
        g_us += 1000u;
        char out[SENSE_TRACE_LINE_MAX];
        const size_t n = sense_trace_pump(&tr, g_us, true, out, sizeof(out));
        (void)fwrite(out, 1u, n, log);
    }
    if (csv != NULL) {
        (void)fclose(csv);
    }
    return (fclose(log) == 0) ? 0 : 2;
}
