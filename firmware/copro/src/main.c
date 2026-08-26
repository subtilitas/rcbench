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

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "copro_pins.h"
#include "link_dev.h"
#include "link_frame.h"
#include "link_pages.h"
#include "link_uart.h"
#include "link_wire.h"

/* ------------------------------------------------------------- the pages */

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
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
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
};

/* ---------------------------------------------------------------- the loop */

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

    link_decoder_t rx;
    link_decoder_reset(&rx);
    link_msg_t req;

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

        if (link_dev_tick(&s_dev, now)) {
            outputs_off();   /* the edge, once */
        }
    }
}
