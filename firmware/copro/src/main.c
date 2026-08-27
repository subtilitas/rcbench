/*
 * The coprocessor application.
 *
 * Answer polls, and never speak first.  That is the whole loop, and the
 * property is structural rather than disciplined: there is no code path here
 * that transmits without having decoded a request.
 *
 * The measurement front end, the PIO programs and the power path land on top
 * of this.  What is here is the part that has to be right before any of them
 * can be trusted: the wire, the failsafe, and the refusal to invent traffic.
 */
#include <stdio.h>
#include <string.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "copro_pins.h"
#include "link_dev.h"
#include "link_frame.h"
#include "link_pages.h"
#include "link_uart.h"
#include "link_wire.h"
#include "telemetry_sim.h"

/* ------------------------------------------------------------- the pages */

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    uint16_t status[LINK_ST_COUNT];
    uint16_t bench[LINK_BN_COUNT];
} copro_state_t;

static copro_state_t s_state;
static link_dev_t    s_dev;

static void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->identity[off + i];
    }
}

static void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->control[off + i];
    }
}

static void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->status[off + i];
    }
}

/*
 * The numbers.  Read-only by construction: there is no write handler, so a
 * host that tries to set a measurement is refused with READ_ONLY rather than
 * quietly believed.
 */
static void bench_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    const copro_state_t *s = (const copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = s->bench[off + i];
    }
}

static uint8_t control_write(void *ctx, uint8_t off, uint8_t n,
                             const uint16_t *in)
{
    copro_state_t *s = (copro_state_t *)ctx;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_THROTTLE && in[i] > LINK_THROTTLE_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_CT_CLEAR) {
            if (in[i] != LINK_CLEAR_MAGIC) {
                return LINK_NACK_BAD_VALUE;
            }
            link_dev_clear_failsafe(&s_dev, (uint32_t)to_ms_since_boot(
                                                get_absolute_time()));
            continue;
        }
        /* Refusing to arm while in failsafe is the coprocessor's own decision
         * to make: the panel is not the authority on whether it is safe here. */
        if (reg == LINK_CT_ARM && in[i] != 0 && s_dev.failsafe) {
            return LINK_NACK_NOT_ARMED;
        }
        s->control[reg] = in[i];
    }
    return 0;
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_STATUS,   LINK_ST_COUNT, status_read,   NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_BENCH,    LINK_BN_COUNT, bench_read,    NULL },
};

/* ---------------------------------------------------------------- the loop */

/*
 * With no measurement front end fitted there is nothing to read, so the
 * numbers are modelled here -- and every one of them carries
 * LINK_BN_SIMULATED, which travels the wire and puts SIMULATION across the
 * panel's screen.  A remote fake declares itself; it is not assumed honest.
 */
static telemetry_sim_t s_sim;
static bench_state_t   s_bench;

static void sample(float dt_s)
{
    const float throttle = (s_state.control[LINK_CT_ARM] != 0)
                               ? (float)s_state.control[LINK_CT_THROTTLE] / 100.0f
                               : 0.0f;
    telemetry_sim_step(&s_sim, throttle, dt_s, &s_bench);
    bench_state_to_regs(&s_bench, s_state.bench);

    s_state.status[LINK_ST_STATE] =
        s_dev.failsafe ? (uint16_t)LINK_STATE_FAILSAFE
                       : (s_state.control[LINK_CT_ARM] != 0
                              ? (uint16_t)LINK_STATE_ARMED
                              : (uint16_t)LINK_STATE_IDLE);
    s_state.status[LINK_ST_FAULTS] =
        s_dev.failsafe ? (uint16_t)LINK_FAULT_LINK_SILENT : 0u;

    const uint32_t up = (uint32_t)to_ms_since_boot(get_absolute_time());
    s_state.status[LINK_ST_UPTIME_MS_LO] = (uint16_t)(up & 0xFFFFu);
    s_state.status[LINK_ST_UPTIME_MS_HI] = (uint16_t)(up >> 16);
}

static void outputs_off(void)
{
    /* Nothing to switch off yet.  The call exists so the failsafe path is
     * written and reachable now rather than added once there is something to
     * forget to add it to. */
    s_state.control[LINK_CT_ARM]      = 0;
    s_state.control[LINK_CT_THROTTLE] = 0;
}

int main(void)
{
    stdio_init_all();

    s_state.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    s_state.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    s_state.identity[LINK_ID_CAPABILITIES]   = 0;

    const uint32_t now0 = (uint32_t)to_ms_since_boot(get_absolute_time());
    link_dev_init(&s_dev, k_pages, count_of(k_pages), &s_state, now0);

    if (!link_uart_init(LINK_BAUD_BRINGUP)) {
        /* Below the floor the driver switches off mid-frame, which looks like
         * a flaky link rather than a misconfiguration.  Refuse instead. */
        for (;;) {
            printf("rcbench-copro: %u baud is below the floor of %u\n",
                   (unsigned)LINK_BAUD_BRINGUP, (unsigned)LINK_BAUD_FLOOR);
            sleep_ms(1000);
        }
    }

    telemetry_sim_init(&s_sim, NULL);
    memset(&s_bench, 0, sizeof(s_bench));

    link_decoder_t rx;
    link_decoder_reset(&rx);
    link_msg_t req;
    uint32_t last_sample = (uint32_t)to_ms_since_boot(get_absolute_time());

    for (;;) {
        const uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());

        /* A short read rather than a blocking one: the failsafe has to fire on
         * time whether or not anything is arriving, and 200 ms of silence is
         * exactly the case where nothing is. */
        const int byte = link_uart_read_byte(1000);
        if (byte >= 0 && link_decode_byte(&rx, (uint8_t)byte, &req)) {
            /*
             * Wait out the panel's own direction circuit before answering.
             * Its RC one-shot holds the bus for up to 179 us after its last
             * falling edge, and the hold starts from that edge rather than
             * from the end of the frame -- so this waits from the last byte
             * received, which is later, and covers both.
             */
            while (link_uart_since_last_rx_us() < LINK_TURNAROUND_US) {
                tight_loop_contents();
            }

            uint8_t reply[LINK_MAX_FRAME];
            const size_t n = link_dev_handle(&s_dev, &req, reply,
                                             sizeof(reply), now);
            if (n > 0) {
                link_uart_write(reply, n);
            }
        }

        /* 50 Hz, which is faster than the panel polls, so a poll always finds
         * a fresh sample rather than the one it was already shown. */
        if ((uint32_t)(now - last_sample) >= 20u) {
            sample((float)(now - last_sample) / 1000.0f);
            last_sample = now;
        }

        if (link_dev_tick(&s_dev, now)) {
            outputs_off();   /* the edge, once */
        }
    }
}
