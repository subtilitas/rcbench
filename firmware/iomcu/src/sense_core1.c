/*
 * Core 1: the sensor bus and the hand-over.  See sense_core1.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_core1.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "sense_i2c.h"
#include "tone_core1.h"

/* --- shared between the cores, under s_lock --------------------------- */

static spin_lock_t   *s_lock;
static sense_cmd_t    s_order;          /* core 0 writes, core 1 copies   */
static sense_snap_t   s_shared;         /* core 1 writes, core 0 copies   */
/* Moved under the lock with each snapshot; read without it by core 0 to
 * see whether there is one to copy.  A 32-bit load is one access. */
static volatile uint32_t s_shared_seq;

/* --- core 1's own ------------------------------------------------------- */

/* Static rather than on core 1's 2 kB stack: the service holds the
 * schedule with its windows and its CH1 history, 1,200 bytes. */
static sense_svc_t  s_svc;
static sense_cmd_t  s_cmd;
static sense_snap_t s_snap;

/* Core 1's stack, 4 kB: the default 2 kB was the sensor bus's, and the tone
 * service runs on it too. */
static uint32_t s_stack[1024];

/* --- core 0's own ------------------------------------------------------- */

static uint32_t s_taken_seq;

static uint64_t clock_us(void *ctx)
{
    (void)ctx;
    return time_us_64();
}

static void recover(void *ctx)
{
    (void)ctx;
    sense_i2c_recover();
}

static bool bus_open(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    return sense_i2c_open(sda, scl);
}

static void bus_close(void *ctx)
{
    (void)ctx;
    sense_i2c_close();
}

static sense_err_t bus_ask(void *ctx, uint8_t addr)
{
    (void)ctx;
    return sense_i2c_ask(addr);
}

static void core1_main(void)
{
    /* First: the flash store may not stop this core before it can be
     * parked in RAM. */
    (void)flash_safe_execute_core_init();

    const sense_svc_io_t io = {
        .sched = { .i2c     = { sense_i2c_read, sense_i2c_write, NULL },
                   .now_us  = clock_us,
                   .recover = recover,
                   .ctx     = NULL },
        .open  = bus_open,
        .close = bus_close,
        .ask   = bus_ask,
    };
    sense_svc_init(&s_svc, &io);

    uint64_t next = time_us_64();
    for (;;) {
        /* Paced on the timer, not on the work: a tick that ran long (a
         * probe is 1.1 ms of bus, a scan 0.8 ms) is followed at once, and
         * one that ran past a whole tick starts the count again from now
         * rather than running the missed ones back to back. */
        while ((int64_t)(time_us_64() - next) < 0) {
            tight_loop_contents();
        }
        const uint64_t now = time_us_64();
        next = (now - next >= SENSE_TICK_US) ? now + SENSE_TICK_US
                                             : next + SENSE_TICK_US;

        uint32_t irq = spin_lock_blocking(s_lock);
        memcpy(&s_cmd, &s_order, sizeof(s_cmd));
        spin_unlock(s_lock, irq);

        sense_svc_step(&s_svc, &s_cmd, &s_snap);
        /* The phase tap's pass: the words the PIO left in its ring since
         * the last tick.  Nothing when no tap runs. */
        tone_core1_step();

        irq = spin_lock_blocking(s_lock);
        memcpy(&s_shared, &s_snap, sizeof(s_shared));
        s_shared_seq = s_shared_seq + 1u;
        spin_unlock(s_lock, irq);
    }
}

bool sense_core1_start(const sense_cmd_t *first)
{
    s_lock = spin_lock_instance((uint)spin_lock_claim_unused(true));
    s_order = *first;
    multicore_launch_core1_with_stack(core1_main, s_stack, sizeof(s_stack));
    const uint64_t until = time_us_64() + SENSE_CORE1_START_MS * 1000u;
    while (!multicore_lockout_victim_is_initialized(1u)) {
        if (time_us_64() >= until) {
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

void sense_core1_order(const sense_cmd_t *cmd)
{
    if (s_lock == NULL) {
        return;                         /* not started */
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    memcpy(&s_order, cmd, sizeof(s_order));
    spin_unlock(s_lock, irq);
}

bool sense_core1_snapshot(sense_snap_t *out)
{
    if (s_lock == NULL || s_shared_seq == s_taken_seq) {
        return false;
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    memcpy(out, &s_shared, sizeof(*out));
    s_taken_seq = s_shared_seq;
    spin_unlock(s_lock, irq);
    return true;
}
