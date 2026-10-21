/**
 * @file
 * @brief The IOmcu application's own state, shared by the files it is cut
 * into.
 *
 * Private to firmware/iomcu/src: nothing under shared/ or protocols/
 * includes it.
 *
 * main.c         boot, the main loop and the page table; defines most of
 *                the state declared below
 * iomcu_report.c the console report every 3 s
 * iomcu_pages.c  IDENTITY, STATUS, CONTROL, BENCH, CHANNELS, CHAN_CFG,
 *                OUTPUTS, SERVO and PROG, and the glue that binds the outputs
 * iomcu_kst.c    the KST page and the port's hardware
 * iomcu_hitec.c  the HITEC page and the port's hardware
 * iomcu_sense.c  SUPPLY, SENSE, SERVO_SENSE, SERVO_WIN, SENSE_MAP,
 *                SENSE_WIN_A, SENSE_WIN_B and SENSE_LIMIT
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
#include "hitec_line.h"
#include "hitec_port.h"
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

/**
 * @brief Register images of the pages the IOmcu holds, one array per page.
 */
typedef struct {
    uint16_t identity[LINK_ID_COUNT];
    uint16_t control[LINK_CT_COUNT];
    uint16_t status[LINK_ST_COUNT];
    uint16_t bench[LINK_BN_COUNT];
    uint16_t channels[LINK_CH_COUNT];   /* what each output is asked for   */
    uint16_t chan_cfg[LINK_CC_COUNT];   /* what each channel is            */
    uint16_t slots[LINK_OS_COUNT];      /* which driver drives what        */
} iomcu_state_t;

/**
 * @brief The register images of the pages in iomcu_state_t.
 */
extern iomcu_state_t s_state;

/**
 * @brief What the output drivers make true, whatever is connected; see
 * main().
 */
#define IOMCU_CAPABILITIES \
    ((uint16_t)(LINK_CAP_SERVO_PWM | LINK_CAP_ESC_DRIVE | LINK_CAP_ESC_TELEM))
/**
 * @brief The SERVO page: the PWM target's frame rate and the sweep.
 *
 * Not kept.
 */
extern servo_page_t s_servo;
/**
 * @brief The SUPPLY page and the PD mini it drives on a PIO UART.
 *
 * Not kept.
 */
extern supply_page_t s_supply;
extern pdmini_t      s_pd;
extern bool          s_pd_open;      /* the UART claimed for its pins */
extern uint8_t       s_pd_rate;      /* the rate it runs at, 0..6     */
/**
 * @brief Wiring taken and not yet in flash: no ON until it is, so a restart
 * in the run finds the wiring that drives the module and can switch it off.
 */
extern bool          s_supply_unsaved;
/**
 * @brief New wiring taken, the UART to attach once it is saved.
 */
extern bool          s_supply_attach;
/**
 * @brief The SENSE and SERVO_SENSE pages: the current monitors' set-up,
 * kept, and the pins it holds.
 *
 * Core 1 reads the parts (sense_core1.h); this core
 * gives it its orders and publishes what it hands back.
 */
extern sense_page_t  s_sense;
/**
 * @brief The TONE page: the phase tap's set-up, kept, and the beeps core 1
 * hears on it.
 *
 * Core 0 owns the PIO state machine and the DMA ring (tone_cap.c
 * in protocols/phase_tap/rp2350); core 1 reads and detects (tone_core1.c).
 */
extern tone_page_t   s_tone;
/**
 * @brief Core 1's last snapshot.
 *
 * The pins it said it still holds are the page's
 * (sense_page_held()): a bus moved or closed keeps its old pins from the
 * outputs until core 1 has let them go, about 1 ms.
 */
extern sense_snap_t  s_sense_snap;
extern sense_cmd_t   s_sense_cmd;    /* core 1's order as last built */
/**
 * @brief Moves on each edge of the bank into driving: core 1 starts the
 * run's peaks and the INA228's totals on it.
 */
extern uint16_t      s_run_gen;
/**
 * @brief The capture's PWM edge, stamped by outputs_hw for capture order
 * s_edge_gen.
 */
extern bool          s_edge_set;
extern uint16_t      s_edge_gen;
extern uint64_t      s_edge_us;
/**
 * @brief What the board and this application hold, before the supply and the
 * sensor bus take their pins.
 */
extern uint64_t      s_base_reserved;
/**
 * @brief The PROG page: the programming ports, outputs of their own kind
 * with their own pins (shared/outputs/prog_port.h).
 *
 * Pin, slew, endpoints and
 * function are kept; the pulse's switch and the tester's target are not.
 */
extern prog_port_t   s_prog;
/**
 * @brief The programming ports this build has; the model holds
 * LINK_PG_PORTS_MAX.
 */
#define IOMCU_PROG_PORTS 4u
/**
 * @brief The KST page: the wire protocol for KST and Chaservo servos on a
 * programming port (shared/outputs/kst_port.h) and the wire driver the
 * port's pin is handed to (protocols/kst/rp2350).
 *
 * Not kept: a restart
 * finds the port in PWM (pulse-width modulation) with no session.
 */
extern kst_port_t    s_kst;
extern kst_line_t    s_kst_line;
/**
 * @brief The HITEC page: raw transactions of the Hitec wire on a programming
 * port (shared/outputs/hitec_port.h) and the wire driver the port's pin is
 * handed to (protocols/hitec/rp2350).
 *
 * A port is free, the KST page's or
 * this page's, never two of them.  Not kept: a restart finds the port
 * free.
 */
extern hitec_port_t  s_hitec;
extern hitec_line_t  s_hitec_line;
extern link_dev_t    s_dev;

/**
 * @brief Every output, behind one set of rules.
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

/**
 * @brief The panel's safety line, as judged in firmware.
 *
 * The control page consults
 * it before it arms; see heartbeat_init() in iomcu_can.c.
 */
extern heartbeat_mon_t s_beat;

/**
 * @brief Requests this end has answered, published on the STATUS page.
 */
extern uint32_t s_frames;

/**
 * @brief CAN frames received and refused since power-up, published on the
 * STATUS page (LINK_ST_REFUSED_LO and _HI).
 *
 * Counted in two places, both shared
 * code: a receive buffer with no frame the link carries in it
 * (mcp2515_recv_next()) and a frame link_can_decode() refuses
 * (link_can_decode_counted()).  Wraps at 2^32.
 */
extern uint32_t s_refused;

/**
 * @brief The pass's clock, used by everything the pass reaches -- including
 * the page callbacks, which run inside can_service() and have no `now` of
 * their own.
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

/**
 * @brief The heartbeat line's level at the last sample; see iomcu_can.c.
 */
extern bool s_beat_level;

/**
 * @brief The CAN controller, its echoes and its receive overruns; see
 * iomcu_can.c.
 */
extern bool     s_can_up;
extern uint32_t s_can_echoes;
extern uint32_t s_can_overflows;

/**
 * @brief When a frame was last taken out of the controller; see iomcu_can.c.
 */
extern uint32_t s_last_rx_ms;

/**
 * @brief The binding BIND_CFG and BIND_OUT hold prepared; see iomcu_bind.c.
 */
extern out_stage_t s_stage;

/* ------------------------------------------------------- the page handlers */

/**
 * @brief Reads the IDENTITY page.
 *
 * iomcu_pages.c
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void identity_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the CONTROL page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void control_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the CONTROL page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t control_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the STATUS page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void status_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the BENCH page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void bench_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the CHANNELS page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void channels_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the CHANNELS page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t channels_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
/**
 * @brief Reads the CHAN_CFG page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void chan_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the CHAN_CFG page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t chan_cfg_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
/**
 * @brief Reads the OUTPUTS page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void slots_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the OUTPUTS page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t slots_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the SERVO page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void servo_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SERVO page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t servo_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the PROG page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void prog_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the PROG page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t prog_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief Reads the KST page.
 *
 * iomcu_kst.c
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void kst_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the KST page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t kst_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief Reads the HITEC page.
 *
 * iomcu_hitec.c
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void hitec_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the HITEC page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t hitec_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief Reads the SUPPLY page.
 *
 * iomcu_sense.c
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void supply_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SUPPLY page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t supply_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the SENSE page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SENSE page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t sense_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the SERVO_SENSE page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void servo_sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SERVO_SENSE page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t servo_sense_write(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in);
/**
 * @brief Reads the SERVO_WIN page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void servo_win_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the SENSE_MAP page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void sense_map_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SENSE_MAP page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t sense_map_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the SENSE_WIN_A page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void sense_win_a_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the SENSE_WIN_B page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void sense_win_b_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the SENSE_LIMIT page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void sense_limit_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the SENSE_LIMIT page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t sense_limit_wr(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);

/**
 * @brief Reads the TONE page.
 *
 * iomcu_tone.c
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void tone_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the TONE page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t tone_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief Reads the BIND_CFG page.
 *
 * iomcu_bind.c
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void bind_cfg_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the BIND_CFG page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t bind_cfg_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
/**
 * @brief Reads the BIND_OUT page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void bind_out_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the BIND_OUT page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t bind_out_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
/**
 * @brief Reads the BIND page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void bind_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the BIND page.
 *
 * @param ctx  The iomcu_state_t the page reads or writes.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t bind_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);
/**
 * @brief Reads the CATALOGUE page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void catalogue_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the SHAPE page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void shape_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the ARTWORK page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void artwork_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Reads the ART_DATA page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void art_data_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);
/**
 * @brief Writes the ART_DATA page.
 *
 * @param ctx  Unused.
 * @param off  First register to write.
 * @param n  Number of registers to write.
 * @param in  The @p n register values to write.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t art_data_write(void *ctx, uint8_t off, uint8_t n,
                       const uint16_t *in);
/**
 * @brief Reads the PADS page.
 *
 * @param ctx  Unused.
 * @param off  First register to read.
 * @param n  Number of registers to read.
 * @param out  Receives the @p n register values.
 */
void pads_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);

/* ---------------------------------------------- shared between the files */

/**
 * @brief Asks for the output configuration to be saved.
 *
 * iomcu_pages.c
 *
 * @param s  The state whose output configuration is saved.
 */
void save_outputs(const iomcu_state_t *s);
/**
 * @brief Makes the programming ports' pins agree with the PROG page.
 */
void ports_apply(void);
/**
 * @brief Makes the silicon agree with the bank, binding only the slots in @p
 * may_bind.
 *
 * @param may_bind  Bit mask of the slots that may be bound. The others are
 *                  released and left unbound.
 */
void hw_apply_only(uint8_t may_bind);
/**
 * @brief Makes the silicon agree with the bank, binding every slot.
 */
void hw_apply(void);
/**
 * @brief Takes a whole CHAN_CFG page into force, when it passes the checks.
 *
 * @param s  The state to update.
 * @param next  The new CHAN_CFG register values.
 * @param slots  The OUTPUTS page prepared beside @p next by a BIND commit, or
 *               NULL for a CHAN_CFG write.
 * @param save  True to save the configuration to flash.
 * @return 0 to accept, or a link_nack_t to refuse. A refused page leaves the
 *         page in force unchanged.
 */
uint8_t chan_cfg_take(iomcu_state_t *s, const uint16_t *next,
                      const uint16_t *slots, bool save);
/**
 * @brief Takes a whole OUTPUTS page (slots) into force, when it passes the
 * checks.
 *
 * @param s  The state to update.
 * @param next  The new slot register values.
 * @param save  True to save the configuration to flash.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t slots_take(iomcu_state_t *s, const uint16_t *next, bool save);

/**
 * @brief Returns the pin of a programming port for the KST wire driver.
 *
 * iomcu_kst.c: the port's hardware, which main() hands to kst_port_init()
 *
 * @param ctx  Unused.
 * @param port  Index of the programming port.
 * @return The GPIO number, or the value prog_port_wire_pin() gives for a port
 *         with no pin.
 */
uint8_t kst_hw_pin(void *ctx, uint8_t port);
/**
 * @brief Takes the programming port's pin for the KST wire driver.
 *
 * @param ctx  Unused.
 * @param port  Index of the programming port. Not used here.
 * @param pin  GPIO number to take.
 * @return True when the KST line opened on the pin.
 */
bool kst_hw_take(void *ctx, uint8_t port, uint8_t pin);
/**
 * @brief Gives the programming port's pin back from the KST wire driver.
 *
 * @param ctx  Unused.
 */
void kst_hw_give(void *ctx);
/**
 * @brief Fills the supply view of the KST port.
 *
 * @param ctx  Unused.
 * @param out  Receives the port's supply view.
 */
void kst_hw_supply(void *ctx, supply_port_view_t *out);
/**
 * @brief Sets the supply of the KST port.
 *
 * No ON while the supply's wiring is not in flash, as the SUPPLY page
 * refuses one: a restart in the run could not switch the module off.
 *
 * @param ctx  Unused.
 * @param on  True to switch the supply on.
 * @param mv  Set voltage in millivolts, for ON.
 * @param ma  Set current in milliamps, for ON.
 * @return False for an ON while the wiring is not saved; otherwise what
 *         supply_page_port_set() returns.
 */
bool kst_hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma);

/**
 * @brief Takes the programming port's pin for the HITEC wire driver.
 *
 * iomcu_hitec.c: the port's hardware, which main() hands to
 * hitec_port_init() beside kst_hw_pin() and the supply's two
 *
 * @param ctx  Unused.
 * @param port  Index of the programming port. Not used here.
 * @param pin  GPIO number to take.
 * @param family  Hitec protocol family the line is opened for.
 * @param drive  Drive setting of the pin.
 * @param rx  Nonzero to claim the sampler as well, for PRECHARGE.
 * @return True when the HITEC line opened on the pin.
 */
bool hitec_hw_take(void *ctx, uint8_t port, uint8_t pin, uint8_t family,
                   uint8_t drive, uint8_t rx);
/**
 * @brief Gives the programming port's pin back from the HITEC wire driver.
 *
 * @param ctx  Unused.
 */
void hitec_hw_give(void *ctx);
/**
 * @brief Aborts the HITEC operation in progress.
 *
 * @param ctx  Unused.
 */
void hitec_hw_abort(void *ctx);
/**
 * @brief Reports the timing of the last HITEC run.
 *
 * @param ctx  Unused.
 * @param out  Receives the lead time, span and bit time of the run.
 */
void hitec_hw_run(void *ctx, hitec_port_run_t *out);

/**
 * @brief Refreshes the IDENTITY page's capability word.
 *
 * iomcu_sense.c
 */
void capabilities_update(void);
/**
 * @brief Rebinds the supply's PD mini UART to the wiring in force.
 *
 * @return True when the supply is disabled or its UART is attached. False
 *         when no PIO block can reach the pins or has room.
 */
bool supply_rewire(void);
/**
 * @brief Takes a SUPPLY write into the supply's wiring and output state.
 *
 * @param off  First register of the write.
 * @param n  Number of registers in the write.
 * @param in  The @p n register values.
 * @param checked  True for the wiring change a state read has shown the
 *                 module off for (supply_page_wire_ready()); false for a
 *                 write from the link.
 * @return 0 to accept, or a link_nack_t to refuse.
 */
uint8_t supply_take(uint8_t off, uint8_t n, const uint16_t *in,
                    bool checked);

/**
 * @brief Returns the pins the phase tap may not take now.
 *
 * iomcu_tone.c
 *
 * @return One bit per GPIO: the supply's pins, the programming ports' pins
 *         and the sensor bus's pins, including those core 1 has not let go of
 *         yet.
 */
uint64_t tone_taken(void);
/**
 * @brief Makes the phase tap's capture agree with the TONE page's pin.
 *
 * @return True when the capture agrees with the page. False when the wiring
 *         could not take the pin, or core 1 was still in a pass.
 */
bool tone_rewire(void);

/**
 * @brief Sets up the heartbeat input.
 *
 * iomcu_can.c
 */
void heartbeat_init(void);
/**
 * @brief Brings up the CAN controller at IOMCU_CAN_BITRATE.
 *
 * A failure is not fatal: it is reported on the USB serial port, and s_can_up
 * stays false.
 */
void can_start(void);
/**
 * @brief Empties the CAN receive buffers and answers each frame.
 *
 * @param now  The pass's clock in ms, stamped on each frame received and
 *             passed to the link dispatcher.
 */
void can_service(uint32_t now);

/**
 * @brief Prints the console report when 3 s have passed.
 *
 * iomcu_report.c: every 3 s at most, from the main loop
 *
 * @param now  The pass's clock in ms.
 */
void can_report(uint32_t now);

/* ------------------------------------------------------ the inline helpers */

/**
 * @brief STATUS's state and fault registers.
 *
 * The state and the link and heartbeat
 * bits are the gate's (safety_gate_status()); the arm latch is on neither.
 *
 * @param s  The state whose STATUS registers are updated.
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

/**
 * @brief The slots the silicon has bound, one bit each.
 *
 * @return One bit per slot: bit i is set when slot i is bound.
 */
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

/**
 * @brief Sample the line; returns whether its level changed since the last
 * sample.
 *
 * safety_gate_step() hands the edge to the monitor.
 *
 * @return True when the level changed since the last sample.
 */
static inline bool heartbeat_edge(void)
{
    const bool level = gpio_get(IOMCU_HEARTBEAT_PIN);
    if (level == s_beat_level) {
        return false;
    }
    s_beat_level = level;
    return true;
}

/**
 * @brief The programming ports out of PWM: held by the KST page or the HITEC
 * page, in any state but PWM and free.
 *
 * @return One bit per programming port.
 */
static inline uint8_t ports_quiet(void)
{
    return (uint8_t)(kst_port_quiet_mask(&s_kst)
                     | hitec_port_quiet_mask(&s_hitec));
}

/**
 * @brief The programming ports whose pin a wire driver holds.
 *
 * @return One bit per programming port.
 */
static inline uint8_t ports_wire(void)
{
    return (uint8_t)(kst_port_pin_mask(&s_kst)
                     | hitec_port_pin_mask(&s_hitec));
}

/**
 * @brief Each wire page told which ports the other holds: neither takes a
 * port the other has.
 *
 * Called after a write of either and once a pass.
 */
static inline void ports_exclusive(void)
{
    kst_port_other_mask(&s_kst, hitec_port_quiet_mask(&s_hitec));
    hitec_port_other_mask(&s_hitec, kst_port_quiet_mask(&s_kst));
}

/**
 * @brief A wire page holds the SUPPLY page: the KST port out of PWM, or a
 * HITEC POWER running.
 *
 * @return True when the SUPPLY page is held.
 */
static inline bool ports_hold_supply(void)
{
    return kst_port_state(&s_kst) != LINK_KST_PWM
           || hitec_port_supply_hold(&s_hitec);
}

/**
 * @brief The ports that render their pulse while the bank drives: bound,
 * switched on, and not out of PWM for a wire protocol or a supply cycle.
 *
 * @return One bit per programming port.
 */
static inline uint8_t ports_pulsing(void)
{
    return (uint8_t)(prog_port_pwm(&s_prog) & outputs_hw_ports_bound()
                     & ~ports_quiet());
}

/**
 * @brief What no output may have: the board's own pins, and the ones the
 * supply, the sensor bus and the phase tap hold now -- the pages', and the
 * ones core 1 has not let go of yet.
 */
static inline void reserve_held(void)
{
    outputs_reserve_pins(&s_outputs, s_base_reserved
                                     | supply_page_pins(&s_supply)
                                     | sense_page_held(&s_sense)
                                     | tone_page_pins(&s_tone)
                                     | prog_port_pins(&s_prog));
}

/**
 * @brief Core 1's order, from the page as it stands, the run and the edge.
 *
 * @param cmd  Receives the order. It is zeroed first.
 */
static inline void sense_build(sense_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    sense_page_cmd(&s_sense, cmd);
    cmd->run_gen  = s_run_gen;
    cmd->edge_set = s_edge_set && s_edge_gen == s_sense.cap_gen;
    cmd->edge_us  = s_edge_us;
}

/**
 * @brief Sent whenever something in it changes; core 1 takes it at its next
 * tick.
 *
 * A copy under a lock, about a microsecond.  The order, 136 bytes with the
 * 12 channels' shunts and limits, is built in s_sense_cmd rather than on
 * core 0's 4 kB stack: core 0 alone builds it, from its main loop.
 */
static inline void sense_order(void)
{
    sense_build(&s_sense_cmd);
    sense_core1_order(&s_sense_cmd);
}

/**
 * @brief Core 1's newest snapshot into the pages, when there is one.
 *
 * Called where its contents are needed -- a read of either page, the 50 Hz
 * sample, a capture ending -- rather than every pass, so a pass with nothing
 * to publish costs one load.
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
