/*
 * The IOmcu application's own state, shared by the files it is cut
 * into.  Private to firmware/iomcu/src: nothing under shared/ or protocols/
 * includes it.
 *
 * main.c         boot, the main loop, the console report and the page
 *                table; defines most of the state declared below
 * iomcu_pages.c  IDENTITY, STATUS, CONTROL, BENCH, CHANNELS, CHAN_CFG,
 *                OUTPUTS, SERVO and PROG, and the glue that binds the outputs
 * iomcu_kst.c    the KST page and the port's hardware
 * iomcu_sense.c  SUPPLY, SENSE, SERVO_SENSE and SERVO_WIN
 * iomcu_tone.c   the TONE page and the phase tap's capture
 * iomcu_bind.c   BIND, BIND_CFG, BIND_OUT and the board's own pages
 * iomcu_can.c    the heartbeat's input and the CAN bus
 *
 * The helpers at the end are `static inline`, so the main loop and each
 * page handler that calls one carry their own inlined copy.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef IOMCU_H
#define IOMCU_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/gpio.h"

#include "heartbeat.h"
#include "iomcu_pins.h"
#include "kst_line.h"
#include "kst_port.h"
#include "link_dev.h"
#include "link_pages.h"
#include "out_stage.h"
#include "out_store.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "pdmini.h"
#include "prog_port.h"
#include "safety_gate.h"
#include "sense_core1.h"
#include "sense_page.h"
#include "servo_page.h"
#include "supply_page.h"
#include "tone_page.h"

/* ------------------------------------------------------------- the pages */

typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    uint16_t status[LINK_ST_COUNT];
    uint16_t bench[LINK_BN_COUNT];
    uint16_t channels[LINK_CH_COUNT];   /* what each output is asked for   */
    uint16_t chan_cfg[LINK_CC_COUNT];   /* what each channel is            */
    uint16_t slots[LINK_OS_COUNT];      /* which driver drives what        */
} iomcu_state_t;

extern iomcu_state_t s_state;

/* What the output drivers make true, whatever is connected; see main(). */
#define IOMCU_CAPABILITIES \
    ((uint16_t)(LINK_CAP_SERVO_PWM | LINK_CAP_ESC_DRIVE | LINK_CAP_ESC_TELEM))
/* The SERVO page: the PWM target's frame rate and the sweep.  Not kept. */
extern servo_page_t s_servo;
/* The SUPPLY page and the PD mini it drives on a PIO UART.  Not kept. */
extern supply_page_t s_supply;
extern pdmini_t      s_pd;
extern bool          s_pd_open;      /* the UART claimed for its pins */
extern uint8_t       s_pd_rate;      /* the rate it runs at, 0..6     */
/* Wiring taken and not yet in flash: no ON until it is, so a restart in
 * the run finds the wiring that drives the module and can switch it off. */
extern bool          s_supply_unsaved;
/* New wiring taken, the UART to attach once it is saved. */
extern bool          s_supply_attach;
/*
 * The SENSE and SERVO_SENSE pages: the current monitors' set-up, kept, and
 * the pins it holds.  Core 1 reads the parts (sense_core1.h); this core
 * gives it its orders and publishes what it hands back.
 */
extern sense_page_t  s_sense;
/*
 * The TONE page: the phase tap's set-up, kept, and the beeps core 1 hears
 * on it.  Core 0 owns the PIO state machine and the DMA ring (tone_cap.c
 * in protocols/phase_tap/rp2350); core 1 reads and detects (tone_core1.c).
 */
extern tone_page_t   s_tone;
/* Core 1's last snapshot.  The pins it said it still holds are the page's
 * (sense_page_held()): a bus moved or closed keeps its old pins from the
 * outputs until core 1 has let them go, about 1 ms. */
extern sense_snap_t  s_sense_snap;
/* Moves on each edge of the bank into driving: core 1 starts the run's
 * peaks and the INA228's totals on it. */
extern uint16_t      s_run_gen;
/* The capture's PWM edge, stamped by outputs_hw for capture order
 * s_edge_gen. */
extern bool          s_edge_set;
extern uint16_t      s_edge_gen;
extern uint64_t      s_edge_us;
/* What the board and this application hold, before the supply and the
 * sensor bus take their pins. */
extern uint64_t      s_base_reserved;
/*
 * The PROG page: the programming ports, outputs of their own kind with
 * their own pins (shared/outputs/prog_port.h).  Pin, slew and endpoints are
 * kept; the pulse's switch and the tester's target are not.
 */
extern prog_port_t   s_prog;
/* The programming ports this build has; the model holds
 * LINK_PG_PORTS_MAX. */
#define IOMCU_PROG_PORTS 1u
/*
 * The KST page: the wire protocol for KST and Chaservo servos on a
 * programming port (shared/outputs/kst_port.h) and the wire driver the
 * port's pin is handed to (protocols/kst/rp2350).  Not kept: a restart
 * finds the port in PWM (pulse-width modulation) with no session.
 */
extern kst_port_t    s_kst;
extern kst_line_t    s_kst_line;
extern link_dev_t    s_dev;

/*
 * Every output, behind one set of rules.
 *
 * The pages above are the wire format; the rules (arming, clamping, slew and
 * the silence timeout) live in shared/outputs and nowhere else, so this end
 * and the panel cannot answer them differently.
 *
 * The output pages address bank channels 0..LINK_OUT_CHANNELS-1 directly.
 * The throttle lives above that range, so the control page commands it
 * without colliding with a CHANNELS-page write, and a motor command keeps
 * the control page's priority on the wire.
 */
extern outputs_t s_outputs;

#define CH_THROTTLE  LINK_OUT_CHANNELS   /* bank channel 8, off the page */

/* The panel's safety line, as judged in firmware.  The control page consults
 * it before it arms; see heartbeat_init() in iomcu_can.c. */
extern heartbeat_mon_t s_beat;

/* Requests this end has answered, published on the STATUS page. */
extern uint32_t s_frames;

/*
 * CAN frames received and refused since power-up, published on the STATUS
 * page (LINK_ST_REFUSED_LO and _HI).  Counted in two places, both shared
 * code: a receive buffer with no frame the link carries in it
 * (mcp2515_recv_next()) and a frame link_can_decode() refuses
 * (link_can_decode_counted()).  Wraps at 2^32.
 */
extern uint32_t s_refused;

/*
 * The pass's clock, used by everything the pass reaches -- including the page
 * callbacks, which run inside can_service() and have no `now` of their own.
 *
 * Read at the top of the loop and once more before its tail, and nowhere
 * else.  The second read is there because two things in a pass can stop this
 * core for longer than a pass lasts: a flash window, and a printf to a USB
 * host.  See the loop for why that matters to what is measured after them.
 *
 * Every timeout in this application is a wrap-safe unsigned subtraction of
 * two timestamps.  A second reading of the clock inside a pass can be a
 * millisecond ahead of the pass's own, and a stamp ahead of the `now` it is
 * later compared against subtracts to 4,294,967,295 ms -- past every timeout
 * there is.  A CHANNELS write stamped that way reads as stale in the same
 * pass and is put back to rest; a save stamped that way skips the settle and
 * the quiet-bus wait and takes its flash window inside the request burst it
 * was meant to wait out.
 */
extern uint32_t s_now_ms;

/* The heartbeat line's level at the last sample; see iomcu_can.c. */
extern bool s_beat_level;

/* The CAN controller, its echoes and its receive overruns; see iomcu_can.c. */
extern bool     s_can_up;
extern uint32_t s_can_echoes;
extern uint32_t s_can_overflows;

/* When a frame was last taken out of the controller; see iomcu_can.c. */
extern uint32_t s_last_rx_ms;

/* The binding BIND_CFG and BIND_OUT hold prepared; see iomcu_bind.c. */
extern out_stage_t s_stage;

/* ------------------------------------------------------- the page handlers */

/* iomcu_pages.c */
void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t control_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void bench_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void channels_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t channels_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
void chan_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t chan_cfg_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t slots_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void servo_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t servo_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void prog_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t prog_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/* iomcu_kst.c */
void kst_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t kst_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/* iomcu_sense.c */
void supply_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t supply_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t sense_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void servo_sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t servo_sense_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in);
void servo_win_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);

/* iomcu_tone.c */
void tone_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t tone_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/* iomcu_bind.c */
void bind_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t bind_cfg_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
void bind_out_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t bind_out_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
void bind_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t bind_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
void catalogue_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void shape_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void artwork_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
void art_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
uint8_t art_data_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
void pads_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);

/* ---------------------------------------------- shared between the files */

/* iomcu_pages.c */
void save_outputs(const iomcu_state_t *s);
void ports_apply(void);
void hw_apply_only(uint8_t may_bind);
void hw_apply(void);
uint8_t chan_cfg_take(iomcu_state_t *s, const uint16_t *next,
                      const uint16_t *slots, bool save);
uint8_t slots_take(iomcu_state_t *s, const uint16_t *next, bool save);

/* iomcu_kst.c: the port's hardware, which main() hands to kst_port_init() */
uint8_t kst_hw_pin(void *ctx, uint8_t port);
bool kst_hw_take(void *ctx, uint8_t port, uint8_t pin);
void kst_hw_give(void *ctx);
void kst_hw_supply(void *ctx, supply_port_view_t *out);
bool kst_hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma);

/* iomcu_sense.c */
void capabilities_update(void);
bool supply_rewire(void);
uint8_t supply_take(uint8_t off, uint8_t n, const uint16_t *in,
                    bool checked);

/* iomcu_tone.c */
uint64_t tone_taken(void);
bool tone_rewire(void);

/* iomcu_can.c */
void heartbeat_init(void);
void can_start(void);
void can_service(uint32_t now);

/* ------------------------------------------------------ the inline helpers */

/*
 * STATUS's state and fault registers.  The state and the link and heartbeat
 * bits are the gate's (safety_gate_status()); the arm latch is on neither.
 */
static inline void status_gate(iomcu_state_t *s)
{
    uint16_t faults = 0;
    safety_gate_status(&s_beat, &s_dev, s->control,
                       &s->status[LINK_ST_STATE], &faults);
    if (out_store_is_off()) {
        faults |= (uint16_t)LINK_FAULT_STORE_OFF;
    }
    s->status[LINK_ST_FAULTS] = faults;
}

/* The slots the silicon has bound, one bit each. */
static inline uint8_t bound_slots(void)
{
    uint8_t bound = 0u;
    for (uint8_t i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (outputs_hw_bound(i)) {
            bound |= (uint8_t)(1u << i);
        }
    }
    return bound;
}

/** Sample the line; returns whether its level changed since the last
 *  sample.  safety_gate_step() hands the edge to the monitor. */
static inline bool heartbeat_edge(void)
{
    const bool level = gpio_get(IOMCU_HEARTBEAT_PIN);
    if (level == s_beat_level) {
        return false;
    }
    s_beat_level = level;
    return true;
}

/* The ports that render their pulse while the bank drives: bound, switched
 * on, and not out of PWM for the wire protocol or a supply cycle. */
static inline uint8_t ports_pulsing(void)
{
    return (uint8_t)(prog_port_pwm(&s_prog) & outputs_hw_ports_bound()
                     & ~kst_port_quiet_mask(&s_kst));
}

/*
 * What no output may have: the board's own pins, and the ones the supply,
 * the sensor bus and the phase tap hold now -- the pages', and the ones
 * core 1 has not let go of yet.
 */
static inline void reserve_held(void)
{
    outputs_reserve_pins(&s_outputs, s_base_reserved
                                     | supply_page_pins(&s_supply)
                                     | sense_page_held(&s_sense)
                                     | tone_page_pins(&s_tone)
                                     | prog_port_pins(&s_prog));
}

/* Core 1's order, from the page as it stands, the run and the edge. */
static inline void sense_build(sense_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    sense_page_cmd(&s_sense, cmd);
    cmd->run_gen  = s_run_gen;
    cmd->edge_set = s_edge_set && s_edge_gen == s_sense.cap_gen;
    cmd->edge_us  = s_edge_us;
}

/* Sent whenever something in it changes; core 1 takes it at its next
 * tick.  A copy under a lock, about a microsecond. */
static inline void sense_order(void)
{
    sense_cmd_t cmd;
    sense_build(&cmd);
    sense_core1_order(&cmd);
}

/*
 * Core 1's newest snapshot into the pages, when there is one.  Called
 * where its contents are needed -- a read of either page, the 50 Hz
 * sample, a capture ending -- rather than every pass, so a pass with
 * nothing to publish costs one load.
 */
static inline void sense_sync(void)
{
    if (!sense_core1_snapshot(&s_sense_snap)) {
        return;
    }
    const uint64_t was = s_sense.held;
    sense_page_publish(&s_sense, &s_sense_snap, s_run_gen);
    if (s_sense.held != was) {
        reserve_held();
    }
}

#endif /* IOMCU_H */
