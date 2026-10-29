/**
 * @file
 * @brief The IOmcu application.
 *
 * Answers polls and never transmits unsolicited: every transmission here
 * follows a decoded request.
 *
 * The power path is not written.  This application runs on core 0 and holds
 * the wire, the failsafe and the output bank; iomcu.h lists the files it is
 * cut into.  The output protocols are out_pwm.c and the drivers in
 * protocols/ppm and protocols/dshot, behind outputs_hw.c.
 * Core 1 reads the current monitors on their I2C bus (sense_core1.c) and
 * nothing else; core 0 gives it its orders and publishes what it reads.
 *
 * Nothing here models a reading.  A number this end publishes came off a
 * wire or a sensor, and a quantity nothing measures is left at zero with its
 * valid bit clear.  The panel models when no IOmcu answers at all,
 * which is the only case where a modelled number cannot be mistaken for a
 * measured one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "iomcu.h"

#include "iomcu_pins.h"
#include "bench_state.h"
#include "board_fix.h"
#include "dshot.h"
#include "flash_gate.h"
#include "out_bind.h"
#include "rcbench_version.h"
#include "out_stage.h"
#include "outputs_pages.h"
#include "sense_limit.h"
#include "sense_map.h"
#include "tone_core1.h"
#include "update_hw.h"
#include "xl2515.h"
#if SENSE_TRACE
#include "sense_trace_hw.h"
#endif
#if IOMCU_CONSOLE
#include "console_hw.h"
#endif

/**
 * @brief This file's report lines.
 *
 * The bench console build sends them through the console's output: none lands
 * inside a console line that leaves in pieces, and none waits for the host.
 * Every other build prints them, and printf() waits for the host for up to 500
 * ms (PICO_STDIO_USB_STDOUT_TIMEOUT_US).
 */
#if IOMCU_CONSOLE
#define IOMCU_SAY console_hw_say
#else
#define IOMCU_SAY printf
#endif

/* ------------------------------------------------------------- the state */

/**
 * @brief What iomcu.h declares and describes.
 */
iomcu_state_t   s_state;
servo_page_t    s_servo;
supply_page_t   s_supply;
pdmini_t        s_pd;
bool            s_pd_open;
uint8_t         s_pd_rate;
bool            s_supply_unsaved;
bool            s_supply_attach;
sense_page_t    s_sense;
tone_page_t     s_tone;
sense_snap_t    s_sense_snap;
sense_cmd_t     s_sense_cmd;
uint16_t        s_run_gen;
bool            s_edge_set;
uint16_t        s_edge_gen;
uint64_t        s_edge_us;
uint64_t        s_base_reserved;
prog_port_t     s_prog;
kst_port_t      s_kst;
kst_line_t      s_kst_line;
hitec_port_t    s_hitec;
hitec_line_t    s_hitec_line;
link_dev_t      s_dev;
outputs_t       s_outputs;
heartbeat_mon_t s_beat;
uint32_t        s_frames;
uint32_t        s_refused;
uint32_t        s_now_ms;

/**
 * @brief The bank was driving on the last pass: the edge into driving
 * moves s_run_gen.
 */
static bool          s_pass_driving;
/**
 * @brief The bank was asked to drive on the last pass: its falling edge
 * stops a session.
 */
static bool          s_kst_armed;

/**
 * @brief Whether the page holds a capture that has not finished.
 *
 * @return True while the capture is armed, waiting for a move, or moving.
 */
static bool sense_capture_open(void)
{
    const uint16_t st = s_sense.servo[LINK_SS_CAP_STATE];
    return st == (uint16_t)LINK_CAP_ARMED || st == (uint16_t)LINK_CAP_WAIT_MOVE
           || st == (uint16_t)LINK_CAP_MOVING;
}

/**
 * @brief The link pages this application serves. A page with a NULL write
 * handler is read-only.
 */
static const link_page_t k_pages[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, identity_read, NULL },
    { LINK_PAGE_STATUS,   LINK_ST_COUNT, status_read,   NULL },
    { LINK_PAGE_CONTROL,  LINK_CT_COUNT, control_read,  control_write },
    { LINK_PAGE_CHANNELS, LINK_CH_COUNT, channels_read, channels_write },
    { LINK_PAGE_BENCH,    LINK_BN_COUNT, bench_read,    NULL },
    { LINK_PAGE_OUTPUTS,  LINK_OS_COUNT, slots_read,    slots_write },
    { LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, chan_cfg_read, chan_cfg_write },
    { LINK_PAGE_CATALOGUE, LINK_CAT_COUNT, catalogue_read, NULL },
    { LINK_PAGE_SHAPE,     LINK_SH_COUNT,  shape_read,     NULL },
    { LINK_PAGE_ARTWORK,   LINK_AW_COUNT,  artwork_read,   NULL },
    { LINK_PAGE_ART_DATA,  LINK_AD_COUNT,  art_data_read,  art_data_write },
    { LINK_PAGE_PADS,      LINK_PAD_COUNT, pads_read,      NULL },
    { LINK_PAGE_SERVO,     LINK_SV_COUNT,  servo_read,     servo_write },
    { LINK_PAGE_SUPPLY,    LINK_SP_COUNT,  supply_read,    supply_write },
    { LINK_PAGE_SENSE,     LINK_SN_COUNT,  sense_read,     sense_write },
    { LINK_PAGE_SERVO_SENSE, LINK_SS_COUNT, servo_sense_read,
      servo_sense_write },
    { LINK_PAGE_TONE,      LINK_TN_COUNT,  tone_read,      tone_write },
    { LINK_PAGE_BIND_CFG,  LINK_CC_COUNT,  bind_cfg_read,  bind_cfg_write },
    { LINK_PAGE_BIND_OUT,  LINK_OS_COUNT,  bind_out_read,  bind_out_write },
    { LINK_PAGE_BIND,      LINK_BD_COUNT,  bind_read,      bind_write },
    { LINK_PAGE_SERVO_WIN, LINK_SW_COUNT,  servo_win_read, NULL },
    { LINK_PAGE_KST,       LINK_KS_COUNT,  kst_read,       kst_write },
    { LINK_PAGE_PROG,      LINK_PG_REGS,   prog_read,      prog_write },
    { LINK_PAGE_HITEC,     LINK_HT_COUNT,  hitec_read,     hitec_write },
    { LINK_PAGE_SENSE_MAP, LINK_SM_COUNT,  sense_map_rd,   sense_map_wr },
    { LINK_PAGE_SENSE_LIMIT, LINK_SL_COUNT, sense_limit_rd, sense_limit_wr },
    { LINK_PAGE_SENSE_WIN_A, LINK_SX_COUNT, sense_win_a_read, NULL },
    { LINK_PAGE_SENSE_WIN_B, LINK_SX_COUNT, sense_win_b_read, NULL },
    { LINK_PAGE_UPDATE_CTL,  LINK_UPD_COUNT,      update_hw_ctl_read,
      update_hw_ctl_write },
    { LINK_PAGE_UPDATE_DATA, LINK_UPD_DATA_COUNT, update_hw_data_read,
      update_hw_data_write },
};

/* ---------------------------------------------------------------- the loop */

/**
 * @brief The numbers, and only the ones something measured.
 *
 * Speed, and the ESC's own voltage, current and temperature, come from a
 * bidirectional DShot ESC (electronic speed controller) answering on its
 * own signal line.  Voltage, current, power, their peaks, charge and energy
 * come from the INA228 instead while it is enabled and answering
 * (LINK_BN_SENSED, sense_page_bench()), and the ESC's two then stay on the
 * SENSE page.  A quantity nothing measured is zero with its valid bit
 * clear, and the panel draws that field empty.
 *
 * LINK_BN_SIMULATED is never set here.  An IOmcu that is answering is
 * reporting what it can see; the panel models only when nothing answers at
 * all, and marks that itself.
 */
static bench_state_t s_bench;

/**
 * @brief The peaks belong to a run, and a run starts when the bank arms.
 *
 * They are kept on this end because it has the fast samples and the panel
 * sees one poll in fifty of them, so only this end can see the peak at all.
 * Holding them since boot instead would report a maximum from a motor that
 * was taken off the bench two runs ago.
 */
static bool s_was_driving;

/**
 * @brief Takes one 50Hz sample of the bench readings and publishes them.
 */
static void sample(void)
{
    /*
     * The ESC's telemetry as the driver holds it, and
     * bench_state_from_telem() for what is reported of it: the live
     * readings, their valid bits and the staleness rules, host-tested.
     * The speed is asked for only with the poles set.
     */
    bench_telem_t telem;
    memset(&telem, 0, sizeof(telem));
    telem.poles = s_state.control[LINK_CT_MOTOR_POLES];
    telem.erpm_ok = telem.poles != 0u
                    && outputs_hw_erpm(&telem.erpm, &telem.erpm_age_ms);
    telem.voltage.ok = outputs_hw_edt(DSHOT_TELEM_VOLTAGE,
                                      &telem.voltage.count,
                                      &telem.voltage.age_ms);
    telem.current.ok = outputs_hw_edt(DSHOT_TELEM_CURRENT,
                                      &telem.current.count,
                                      &telem.current.age_ms);
    telem.temperature.ok = outputs_hw_edt(DSHOT_TELEM_TEMPERATURE,
                                          &telem.temperature.count,
                                          &telem.temperature.age_ms);
    bench_state_from_telem(&s_bench, &telem);
    /* The ESC's own voltage and current go to SENSE as well, where they
     * stay while BENCH carries the INA228's. */
    sense_page_esc(&s_sense,
                   (s_bench.flags & (uint16_t)LINK_BN_VOLTAGE_OK) != 0u,
                   s_bench.voltage,
                   (s_bench.flags & (uint16_t)LINK_BN_CURRENT_OK) != 0u,
                   s_bench.current);

    /* On the edge into driving, so a run's peaks are that run's.  The reset
     * takes the current reading rather than zero, which is what stops a sag
     * floor of 0V reading as a collapsed pack. */
    const bool driving = outputs_driving(&s_outputs);
    if (driving && !s_was_driving) {
        bench_state_reset_peaks(&s_bench);
    }
    s_was_driving = driving;

    /* Peaks only from readings that arrived, and a sag floor seeded by the
     * first voltage of the run rather than by the reset that opened it. */
    bench_state_track_peaks(&s_bench);
    /* Then the INA228's numbers over the ESC's, while it is the source:
     * the 50ms window's voltage and current, the run's peaks from its
     * 500Hz samples, and its charge and energy (sense_page_bench()). */
    sense_sync();
    sense_page_bench(&s_sense, &s_sense_snap, s_run_gen, driving, &s_bench);
    bench_state_to_regs(&s_bench, s_state.bench);

    status_gate(&s_state);

    const uint32_t up = (uint32_t)to_ms_since_boot(get_absolute_time());
    s_state.status[LINK_ST_UPTIME_MS_LO] = (uint16_t)(up & 0xFFFFu);
    s_state.status[LINK_ST_UPTIME_MS_HI] = (uint16_t)(up >> 16);

    /* What this end has seen of the wire.  The panel compares these against
     * its own, and the comparison is what tells a dead IOmcu apart from
     * a return path that never releases. */
    s_state.status[LINK_ST_FRAMES_LO] = (uint16_t)(s_frames & 0xFFFFu);
    s_state.status[LINK_ST_FRAMES_HI] = (uint16_t)(s_frames >> 16);
    /*
     * LINK_ST_CRC_ERRORS and LINK_ST_RESYNCS carry the controller's receive
     * and transmit error counters: CAN finds and checks frame boundaries in
     * silicon, so this end has no CRC (cyclic redundancy check) or resync
     * count of its own.
     */
    uint8_t tec = 0, rec = 0;
    xl2515_errors(&tec, &rec, NULL);
    s_state.status[LINK_ST_CRC_ERRORS] = rec;
    s_state.status[LINK_ST_RESYNCS]    = tec;
    s_state.status[LINK_ST_REFUSED_LO] = (uint16_t)(s_refused & 0xFFFFu);
    s_state.status[LINK_ST_REFUSED_HI] = (uint16_t)(s_refused >> 16);
}

/**
 * @brief The failsafe edge: disarm the one bank every output goes through,
 * then bring the pages into line with it so what the panel reads back is
 * what the bank holds.
 */
static void outputs_off(void)
{
    outputs_arm(&s_outputs, false, s_now_ms);

    s_state.control[LINK_CT_ARM]      = 0;
    s_state.control[LINK_CT_THROTTLE] = 0;
    /*
     * The channels page is what a read shows the panel, and it is filled from
     * the bank for the same reason boot fills it that way: zero is a
     * throttle's rest and a surface's low endpoint, so a zeroed page reports
     * a surface nobody commanded at its stop while the pin rests at centre.
     * The bank's commands survive a disarm, so a channel that was commanded
     * reads back what it was asked for, and ARM = 0 beside it says nothing
     * renders it.
     */
    outputs_channels_from_bank(&s_outputs, s_state.channels);
    /* And the pins, now rather than at the top of the next pass: a failsafe
     * that waits for the loop to come round is a failsafe with a latency. */
    outputs_hw_service(&s_outputs);
}

/**
 * @brief Whether flash may not be written now: flash_gate_busy(),
 * host-tested, with this end's four inputs.
 *
 * A function of its own because
 * update_hw_init() takes one.  The PD mini is asked only while it is open.
 *
 * @return True while a flash write must wait.
 */
static bool flash_busy(void)
{
    return flash_gate_busy(outputs_driving(&s_outputs),
                           s_supply.regs[LINK_SP_OUTPUT] != 0u,
                           s_pd_open && pdmini_may_be_on(&s_pd),
                           ports_quiet() != 0u);
}

/**
 * @brief Drive the programming ports' 5V enable low and hold it there.
 *
 * @param r  The board's roles, or NULL for a board without the switch: then
 *           nothing is touched.
 */
static void prog_5v_off(const outbind_roles_t *r)
{
    if (r == NULL) {
        return;
    }
    gpio_init(r->prog_5v_en);
    gpio_put(r->prog_5v_en, false);
    gpio_set_dir(r->prog_5v_en, GPIO_OUT);
}

/**
 * @brief Boots the IOmcu, then runs the main loop.
 *
 * @return Does not return.
 */
int main(void)
{
    /*
     * The programming ports' 5V off before anything else, on a board that
     * switches it (the rbmod carrier's GP21).  Driven low and kept low: no
     * part of this build switches it on, so a lead on a programming port has
     * no supply from the carrier.
     */
    prog_5v_off(outbind_roles(IOMCU_BOARD_ID));
    stdio_init_all();
    /* Why this start happened, before anything else runs. */
    reset_start();

    s_state.identity[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
    s_state.identity[LINK_ID_PROTOCOL_MINOR] = LINK_PROTOCOL_MINOR;
    /*
     * What this image is, so a board can be asked rather than guessed at.
     * The protocol version says what it speaks; these say which build is
     * speaking it, and the two move independently.
     */
    s_state.identity[LINK_ID_FIRMWARE_MAJOR] = RCBENCH_VERSION_MAJOR;
    s_state.identity[LINK_ID_FIRMWARE_MINOR] = RCBENCH_VERSION_MINOR;
    s_state.identity[LINK_ID_FIRMWARE_PATCH] = RCBENCH_VERSION_PATCH;
    /*
     * Which board this is.  The panel offers the pins of the board that
     * answered and of no other, so this register is what stops a pin map
     * being shown for hardware that is not on the bench.
     */
    s_state.identity[LINK_ID_HARDWARE] = IOMCU_BOARD_ID;
    /*
     * What this build can do, which the panel marks its menu from.  The three
     * bits set are the three the output drivers make true: pulses to a servo
     * lead, a signal line to an ESC (electronic speed controller), and
     * telemetry back from one over bidirectional DShot.  The rest are parts
     * that are not fitted -- a shunt, a cell monitor, an accelerometer -- or
     * a program that is not written, and each is set by the thing arriving.
     *
     * These say the IOmcu can, not that anything is connected.  An ESC
     * that does not answer is an ESC that does not answer, and the BENCH
     * page's valid bits are where that shows.  The current monitors add
     * pack sense and servo sense for the parts the SENSE set-up enables,
     * set once the set-up is restored below (capabilities_update()).
     */
    s_state.identity[LINK_ID_CAPABILITIES] = IOMCU_CAPABILITIES;

    /* A range before anybody sets one, so the clamp is meaningful from the
     * first frame rather than from the first configuration. */
    outputs_channels_defaults(s_state.channels);
    outputs_chan_cfg_defaults(s_state.chan_cfg);
    outputs_slots_defaults(s_state.slots);
    servo_page_init(&s_servo);

    /*
     * Then what was saved, over the defaults.  This configures the outputs;
     * it does not drive them.  Every driver is gated by outputs_driving(),
     * which is the bank's armed flag, and this end sets that flag only while
     * the ARM register is set, the link is out of failsafe, the heartbeat is
     * trusted and the arm latch is clear.  The latch is set from start-up
     * until a CLEAR arrives, so a restored binding claims its pins and
     * holds them at idle until somebody arms.  The channels are not restored: a command is
     * not a configuration, and a bench that came back holding the last
     * throttle it was given is exactly what must not happen.
     */
    static out_store_t saved;            /* 288 bytes, off main()'s frame */
    const bool have_saved = out_store_load(&saved);
    /*
     * A board with soldered pins starts each role on its pin, whatever the
     * flash kept: a record written by the image of another board keeps its
     * enables, ranges and endpoints and none of its pins
     * (board_fix_store()).  Its interfaces are restored from that even with
     * nothing kept, so the PD mini, the sensor bus and the tap start on
     * their pins, disabled.
     */
    const outbind_roles_t *roles = outbind_roles(IOMCU_BOARD_ID);
    if (roles != NULL) {
        if (!have_saved) {
            out_store_cfg_defaults(&saved);
        }
        board_fix_store(roles, &saved);
    }
    const bool have_ifaces = have_saved || roles != NULL;
    if (have_saved) {
        memcpy(s_state.slots, saved.slots, sizeof(s_state.slots));
        /* A kept slew above LINK_CC_SLEW_MAX starts as that maximum, here
         * and in BIND_CFG, which is seeded from this below. */
        outputs_chan_cfg_restore(s_state.chan_cfg, saved.chan_cfg);
        /* A stored slot on a programming port's bank channel starts
         * unbound: an OUTPUTS write of it is refused. */
        (void)prog_port_slots_free(s_state.slots);
    }

    const uint32_t now0 = (uint32_t)to_ms_since_boot(get_absolute_time());
    /* Before anything that can reach a page callback: can_start() below opens
     * the controller, and the first frame after it is dispatched with this. */
    s_now_ms = now0;
    outputs_init(&s_outputs, now0);
    /*
     * The pins this build will not hand out, whatever the host asks for: the
     * safety line, the CAN (Controller Area Network) controller's four SPI
     * (Serial Peripheral Interface) pins and its interrupt, the pins the
     * module uses and does not break out (GP23, GP24, GP25, GP29), and every
     * number above the last GPIO (general-purpose input/output) the module
     * has, whatever board the SDK is built for.  The pin arrives from the panel over the OUTPUTS page, so it is whatever an
     * operator typed, and an output bound to the heartbeat input is an
     * interlock that stops working with nothing to show for it.
     *
     * The union of what shared/outputs greys out on the panel and what this
     * file assigns, so the two disagreeing costs a pin rather than the
     * safety line.
     */
    s_base_reserved = outbind_reserved_mask(IOMCU_BOARD_ID)
                      | IOMCU_RESERVED_PINS;
    outputs_reserve_pins(&s_outputs, s_base_reserved);
    supply_page_init(&s_supply);
    sense_page_init(&s_sense);
    tone_page_init(&s_tone);
    tone_core1_init();
    (void)outputs_set_role(&s_outputs, CH_THROTTLE, OUT_ROLE_THROTTLE);
    outputs_chan_cfg_apply(&s_outputs, s_state.chan_cfg);
    /* A stored PWM slot whose channel's endpoint is longer than the frame
     * of the slot's own rate starts unbound: an OUTPUTS write of it is
     * refused, and the SERVO page that may have let it fit starts at 0. */
    (void)outputs_slots_free_long(&s_outputs, s_state.slots);
    outputs_slots_apply(&s_outputs, s_state.slots);
    /* What BIND_CFG and BIND_OUT hold prepared starts as the pages in
     * force. */
    out_stage_init(&s_stage, s_state.chan_cfg, s_state.slots);
    /*
     * The page takes its values from the bank, not the other way round.
     * Nobody has commanded anything yet, and the defaults above filled the
     * page with zero -- which is a throttle's rest and a surface's low
     * endpoint.  Applied as commands, that asks every bound surface for its
     * endpoint, and outputs_arm() restamps the clock, so the staleness
     * timeout cannot return it to centre for a further 500ms.  A servo
     * bound beside a motor would drive its stop for that long on every arm
     * until something commanded it.
     */
    outputs_channels_from_bank(&s_outputs, s_state.channels);
    outputs_hw_init();
    /* The programming ports, none with a pin yet, and the KST port in PWM,
     * before the first binding asks either for the pins it holds. */
    prog_port_init(&s_prog, IOMCU_PROG_PORTS);
    kst_line_init(&s_kst_line);
    const kst_port_hw_t kst_hw = {
        .ctx        = NULL,
        .ports      = IOMCU_PROG_PORTS,
        .pin        = kst_hw_pin,
        .take       = kst_hw_take,
        .give       = kst_hw_give,
        .supply     = kst_hw_supply,
        .supply_set = kst_hw_supply_set,
        .driver     = kst_line_driver(&s_kst_line),
    };
    (void)kst_port_init(&s_kst, &kst_hw);
    hitec_line_init(&s_hitec_line);
    const hitec_port_hw_t hitec_hw = {
        .ctx        = NULL,
        .ports      = IOMCU_PROG_PORTS,
        .pin        = kst_hw_pin,
        .take       = hitec_hw_take,
        .give       = hitec_hw_give,
        .abort      = hitec_hw_abort,
        .run        = hitec_hw_run,
        .supply     = kst_hw_supply,
        .supply_set = kst_hw_supply_set,
        .driver     = hitec_line_driver(&s_hitec_line),
    };
    (void)hitec_port_init(&s_hitec, &hitec_hw);
    hw_apply();
    /*
     * The PD mini's wiring, as last written, driven with the output off: a
     * module this end left on before it restarted is identified, read on and
     * switched off before any panel has written the page.  Through the
     * page's own checks, after the slots, so a pin a slot holds is refused;
     * and after the slots' hardware, so the UART takes what PIO the outputs
     * leave, as it does when written at run time, and never displaces one.
     */
    if (have_ifaces
        && supply_page_write(&s_supply, LINK_SP_ENABLE, LINK_SP_OUTPUT,
                             saved.supply, &s_outputs, false) == 0u) {
        if (!supply_rewire()) {
            supply_page_init(&s_supply);
            (void)supply_rewire();
        }
    }
    /* And switched off now, before the rest of the start (supply_boot_off()):
     * a module this end left on has been on for as long as it was gone. */
    supply_boot_off();
    /*
     * The sensor bus's set-up, through the page's own checks as well: after
     * the slots and the supply, so a pin either already holds is refused and
     * the page starts with both parts off.  The bank is not armed yet, so
     * nothing refuses it for that.
     *
     * And the phase tap's, last, the same way: a pin an output, the supply
     * or the sensor bus holds is refused, the tap does not run, and FLAGS
     * says so.  Its capture starts after their PIO programs, so it takes
     * what state machine they leave: when the outputs and an enabled tap
     * need more than the silicon has, the outputs bind and the tap is the
     * one left out, with the TONE page's flag bit 1 (LINK_TN_PIN_REFUSED)
     * set until a write tries it again.  An OUTPUTS write at run time
     * cannot reach that state with the tap running: it is refused, so the
     * order here and the one at run time agree.
     */
    if (have_ifaces) {
        /* The map, kept in force, after the SENSE frame it mirrors, and
         * only when that frame was taken: a map refused stays derived. */
        if (sense_page_write(&s_sense, LINK_SN_ENABLE,
                             (uint8_t)LINK_SN_CONFIG_COUNT, saved.sense,
                             &s_outputs, supply_page_pins(&s_supply)) == 0u) {
            (void)sense_map_restore(&s_sense, saved.sense_map,
                                    saved.sense_map_set, &s_outputs,
                                    supply_page_pins(&s_supply));
        }
        (void)sense_limit_restore(&s_sense, saved.sense_limit);
        reserve_held();
        tone_page_restore(&s_tone, saved.tone, &s_outputs, tone_taken());
    }
    if (!tone_rewire()) {
        tone_page_refuse(&s_tone);
        reserve_held();
    }
    /*
     * The programming ports, last: a pin a slot, the supply, the sensor bus
     * or the phase tap holds is theirs, and the port is without one, with
     * the PROG page's refused flag set until another pin is written.  A
     * board that has stored no port (a record older than version 7, or
     * none) has port 0 on PROG_PORT_DEFAULT_PIN; a port's function is the
     * record's (4.26), else port 0 SERVO PWM and the others NONE.
     */
    uint16_t prog_kept[PROG_PORT_KEPT_COUNT];
    if (have_ifaces) {
        memcpy(prog_kept, saved.prog, sizeof(prog_kept));
    } else {
        prog_port_defaults(prog_kept, PROG_PORT_DEFAULT_PIN);
    }
    prog_port_restore_fn(&s_prog, prog_kept,
                         have_ifaces ? saved.prog_func : 0u, &s_outputs,
                         supply_page_pins(&s_supply) | sense_page_held(&s_sense)
                             | tone_page_pins(&s_tone));
    reserve_held();
    ports_apply();
#if IOMCU_SENSE_BRINGUP
    /*
     * A bring-up build (cmake -DIOMCU_SENSE_BRINGUP=ON): with no part
     * enabled in flash, both are enabled at the page's defaults -- GP16 and
     * GP17, the INA228 at 0x45 on 200 uOhm, the INA3221 at 0x40 reading
     * CH1 -- for this boot, through the page's own checks, and not saved.
     * It is how a bench reads the parts before the panel can write the
     * SENSE page.
     */
    if (!sense_page_enabled(&s_sense)) {
        const uint16_t en = (uint16_t)(LINK_SN_EN_I228 | LINK_SN_EN_I3221);
        (void)sense_page_write(&s_sense, LINK_SN_ENABLE, 1u, &en, &s_outputs,
                               supply_page_pins(&s_supply));
        reserve_held();
    }
#endif
    capabilities_update();
    /* Which slot runs and whether it waits for BUY, before the first
     * request can ask. */
    update_hw_init(flash_busy);
    link_dev_init(&s_dev, k_pages, count_of(k_pages), &s_state, now0);
#if IOMCU_CONSOLE
    /* The bench console on the same pages, through link_dev_apply(): it
     * does not feed the link's silence watchdog. */
    console_hw_init(&s_dev);
#endif

    heartbeat_init();

    /*
     * The store's spare sectors, erased before the controller is started.
     * Nothing can arrive yet, so these are the windows in the run that cost no
     * frame at all, and the first save after them is a page program.
     *
     * A loop, because out_store_reclaim() erases one sector per call and a
     * store can have more than one to take: a store whose sectors all hold
     * records this build cannot read -- an earlier record version -- has one
     * per sector.  Left to the main loop the second of those would be a 19ms
     * window with the controller already up.
     *
     * Bounded by the store's shape, not by the flash answering.
     * flash_range_erase() reports nothing, so a sector that will not erase is
     * surveyed as not erased and offered again; a loop that ended only when
     * the store came back clean would spin here for ever, before can_start(),
     * with no CAN controller and no failsafe. Past this bound the sector
     * falls to the main loop, which costs a window per pass and keeps the
     * bench answering.
     */
    for (unsigned i = 0; i < OUT_STORE_SECTORS
                         && out_store_reclaim(false, OUT_STORE_QUIET_MS); ++i) {
        IOMCU_SAY("rcbench-iomcu: output store sector reclaimed at boot, "
                  "window %lu us\n",
                  (unsigned long)out_store_last_erase_us());
    }

    /*
     * Core 1 and the sensor bus, after the boot's flash windows -- core 1 is
     * not running for them, so they need no lock-out -- and with the set-up
     * restored above as its first order.  It opens the bus within its first
     * tick.  The pins are already held from the outputs by the page.
     */
    {
        sense_build(&s_sense_cmd);
#if SENSE_TRACE
        /* The trace's ring, before core 1 can write to it. */
        sense_trace_hw_init();
#endif
        /*
         * A core 1 that did not register for the lock-out cannot be parked
         * for a flash window: flash_safe_execute() would refuse every one,
         * or assert in a build with asserts on, and an erase run anyway
         * would fault core 1 executing from flash.  The store is switched
         * off for this boot instead -- set-ups run from RAM -- and the
         * STATUS page says so in LINK_FAULT_STORE_OFF, which the panel's
         * band shows as a fault; the console repeats it every 3s.
         */
        if (sense_core1_start(&s_sense_cmd)) {
            out_store_core1_parkable();   /* windows park core 1 from now */
        } else {
            out_store_off();
            /* No core 1 known to run, so nothing reads the tap: its capture
             * is let go and the page says refused (FLAGS PIN_REFUSED).
             * Every later write that enables the tap is refused with
             * BAD_VALUE, not BUSY: nothing frees before a restart. */
            const bool was_wanted = tone_page_wanted(&s_tone);
            tone_page_no_capture(&s_tone);
            if (was_wanted) {
                (void)tone_rewire();
                reserve_held();
            }
        }
    }

    can_start();
    memset(&s_bench, 0, sizeof(s_bench));

    uint32_t last_sample = (uint32_t)to_ms_since_boot(get_absolute_time());

    for (;;) {
        /*
         * ONE CLOCK PER PASS, used by everything below.  Every timeout here
         * is a wrap-safe unsigned subtraction, so a timestamp 1ms ahead of
         * the `now` it is compared against reads as 4,294,967,295ms of
         * silence, past every timeout there is.
         *
         * Published as s_now_ms for the same reason: the page callbacks run
         * under can_service() below, they stamp the bank and the store, and
         * a clock they read for themselves is a clock that can already be
         * ahead of this one.
         */
        uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());
        s_now_ms = now;

        /* Polled rather than interrupt-driven: the loop turns over far faster
         * than a frame takes to arrive, and the failsafe has to fire on time
         * whether or not anything is arriving. */
        can_service(now);
        /* Core 1's beeps and status into the TONE page. */
        tone_core1_sync(&s_tone);
        /* The firmware update's slice of a hash, its restart, and the end
         * of a session the panel stopped talking to. */
        update_hw_poll(now);

        /*
         * Two independent watchdogs, and both before the bank is armed.  The
         * link watchdog says the panel has stopped talking; this one says the
         * panel has stopped running.  A panel wedged mid-frame can still have
         * an interrupt answering polls, so the link watchdog alone would
         * not fire.
         *
         * Both are polled here rather than left to the end of the pass,
         * because silence generates no event: s_beat.alive and s_dev.failsafe
         * are only as fresh as the last call that looked, and arming on last
         * pass's answer renders one more service of every output after the
         * line has gone past HEARTBEAT_MAX_GAP_MS (150ms) or the link past
         * its own silence timeout.
         *
         * After can_service() above, so a frame that arrived this pass has
         * already cleared the silence before it is judged.
         *
         * safety_gate_step() is the decision, host-tested: either edge
         * sets the arm latch, and only a CLEAR releases it.
         */
        const safety_gate_t gate =
            safety_gate_step(&s_beat, heartbeat_edge(), &s_dev,
                             s_state.control, now);
        if (gate.off) {
            outputs_off();   /* fires on the edge only */
        }

        /*
         * Arming is the IOmcu's judgement: the panel asks and this end
         * decides, recomputed every pass from what only this end knows.
         * outputs_arm() is idempotent and does not stamp the clock, so
         * calling it every pass does not keep a channel alive.  Commands are
         * not refreshed here for the same reason: a channel is alive because
         * the host wrote it.
         */
        /* safety_gate_arm_now() is the rule, host-tested: the gate's arm,
         * held down by a programming port out of PWM and by a firmware
         * update. */
        const bool arm_now = safety_gate_arm_now(gate.arm, ports_quiet(),
                                                 update_hw_holds_arm());
        /* Only the roles ARM names: an arm for the throttle leaves the
         * servos and the programming ports without a pulse, and an arm for
         * the surfaces leaves the motors without one. */
        outputs_arm_roles(&s_outputs,
                          arm_now ? outputs_arm_roles_of(gate.roles) : 0u,
                          now);
        kst_port_bench_armed(&s_kst, arm_now);
        hitec_port_bench_armed(&s_hitec, arm_now);
        /*
         * The KST port: a failsafe edge and a disarm stop its session, and
         * no frame starts while the heartbeat or the link is not trusted.
         * Then the programming ports that render a pulse, from this pass
         * on: none that is out of PWM.
         */
        const bool port_safe = safety_gate_supply_ok(&s_beat, &s_dev);
        const bool port_stop = gate.off || (s_kst_armed && !arm_now);
        kst_port_step(&s_kst, port_safe, port_stop, now);
        /* The HITEC port the same way: a stop cuts its transaction and
         * keeps the port with the pin released. */
        hitec_port_step(&s_hitec, port_safe, port_stop, now);
        s_kst_armed = arm_now;
        ports_exclusive();
        outputs_hw_ports_drive(ports_pulsing());
        sense_page_ports(&s_sense, ports_pulsing());
        /* And the SUPPLY page told that a port commands the module, and
         * whether it writes. */
        supply_page_hold(&s_supply, ports_hold_supply());
        supply_page_port_writes(&s_supply, ports_writing());
        /* The edge into driving starts a run on core 1: its peaks and the
         * INA228's totals.  One compare a pass, an order on the edge. */
        const bool driving_now = outputs_driving(&s_outputs);
        if (driving_now && !s_pass_driving) {
            ++s_run_gen;
            sense_order();
            /* And the last run's totals off the BENCH page now, not at the
             * next 50Hz sample: a panel polling in between would take
             * them, marked TOTALS_OK, as this run's. */
            bench_state_run_starts(&s_bench, s_state.bench);
        }
        s_pass_driving = driving_now;
        /* The sweep's command for this pass, before the step slews to it. */
        (void)servo_page_step(&s_servo, &s_outputs, now);
        /* A capture ends with the run it was timing -- the arm for the
         * surfaces it times -- and one on a programming port with the
         * port's pulse; one core 1 has finished meanwhile keeps its result,
         * so the page takes core 1's view first. */
        const bool timing_now = outputs_role_armed(&s_outputs,
                                                   OUT_ROLE_SURFACE)
                                && !sense_page_port_gone(&s_sense);
        if (!timing_now && sense_capture_open()) {
            sense_sync();
        }
        if (sense_page_step(&s_sense, timing_now)) {
            outputs_hw_watch(-1);
            sense_order();
        }
        /* The supply: its bytes in, a step of its driver, and the output off
         * whenever the panel's heartbeat is not there to switch it off.  A
         * port's write, as the ports' steps above left it, keeps the
         * panel's OFF of a held supply back until it ends. */
        supply_pass(now,
                    kst_port_frame_out(&s_kst)
                        || hitec_port_frame_out(&s_hitec, now),
                    ports_writing(),
                    safety_gate_supply_ok(&s_beat, &s_dev));
        outputs_step(&s_outputs, now);
        /* Straight after the step, so what reaches a pin is what the bank
         * has just decided rather than what it decided a pass ago. */
        outputs_hw_service(&s_outputs);
#if SENSE_TRACE
        /* The trace build reads what this pass rendered: a changed pulse
         * on a PWM slot starts or extends a trace. */
        sense_trace_hw_outputs(&s_outputs);
#endif
        /* The capture's edge, when this pass rendered it: to core 1, for
         * the capture order in force. */
        uint64_t edge_us;
        if (outputs_hw_edge(&edge_us)) {
            s_edge_us  = edge_us;
            s_edge_gen = s_sense.cap_gen;
            s_edge_set = true;
            sense_order();
#if SENSE_TRACE
            sense_trace_hw_edge(edge_us);
#endif
        }

        /* 50Hz, which is faster than the panel polls, so a poll always finds
         * a fresh sample rather than the one it was already shown. */
        if ((uint32_t)(now - last_sample) >= 20u) {
            sample();
            last_sample = now;
        }

        /*
         * A deferred save, once nothing is driving, the writes have stopped
         * and the bus has gone quiet.
         *
         * Writing flash stops this core with interrupts off -- an erase and a
         * program together measured 19,178us on the bring-up module -- and
         * what that costs is CAN frames: the controller holds two, about
         * 130us each, and this loop collects none of them while the window is
         * open.  A frame lost there can be the CONTROL write that disarms, and this
         * loop steps no output while the window is open either, so the save
         * waits for a disarm: out_store_tick() is gated on outputs_driving(),
         * which is the bank's armed flag.
         *
         * The heartbeat is not what bounds the window.  The line edges every
         * HEARTBEAT_PERIOD_MS (20ms) and is sampled by this loop, so an edge
         * inside a 19ms window is timestamped when the loop resumes rather
         * than lost, and the stretched interval stays far under
         * HEARTBEAT_MAX_GAP_MS (150ms).  The interval after it can read short
         * instead: under HEARTBEAT_MIN_GAP_MS (4ms) the monitor rejects it and
         * drops the line until HEARTBEAT_GOOD_RUN (4) good intervals have run,
         * about 80ms.  That is derived from the periods, not measured
         * on hardware.
         *
         * The save also waits for the pages to stop arriving, so CHAN_CFG and
         * OUTPUTS are saved as the pair they are.  How long since a frame
         * arrived is what keeps the window out of a run of the panel's poll
         * transactions: a minimum quiet time, which says nothing about how
         * much of the gap after it is left.
         */
        const uint32_t quiet = (uint32_t)(now - s_last_rx_ms);
        /* And the supply and a KST session: flash_busy(). */
        const bool driving = flash_busy();
        const out_store_step_t step = out_store_tick(driving, quiet, now);
        switch (step) {
        case OUT_STORE_WROTE:
            s_supply_unsaved = false;
            if (s_supply_attach) {
                s_supply_attach = false;
                (void)supply_rewire();
            }
            /*
             * Printed because it is the number that decides whether a save
             * costs a frame: this core answers nothing while it writes, the
             * controller holds two frames of about 130us each, and a request
             * lost there costs the panel 1000ms of waiting, which is past
             * this end's 200ms silence failsafe.
             */
            IOMCU_SAY("rcbench-iomcu: outputs saved, record %u, "
                      "program window %lu us\n",
                      (unsigned)out_store_last_record(),
                      (unsigned long)out_store_last_program_us());
            break;
        case OUT_STORE_ERASED:
            IOMCU_SAY("rcbench-iomcu: output store sector erased, "
                      "window %lu us\n",
                      (unsigned long)out_store_last_erase_us());
            break;
        case OUT_STORE_REFUSED:
            IOMCU_SAY("rcbench-iomcu: output store window refused, core 1 did "
                      "not stop within %u ms; tried again in %u ms\n",
                      (unsigned)OUT_STORE_LOCKOUT_MS,
                      (unsigned)OUT_STORE_REFUSED_WAIT_MS);
            break;
        case OUT_STORE_IDLE:
        default:
            break;
        }
        /*
         * And the erase for the save after next, taken now rather than in
         * front of the save that will need it.  Now is the pass after the
         * record that left a sector behind, with the bus quiet: one save in
         * sixteen leaves one, and the other fifteen find nothing to do.
         *
         * Only on a pass that wrote nothing: two windows in one pass would be
         * one long window with a printf in the middle of it, and the pass
         * between them is what empties the receive buffers.
         */
        if (step == OUT_STORE_IDLE && out_store_reclaim(driving, quiet)) {
            IOMCU_SAY("rcbench-iomcu: output store sector reclaimed, "
                      "window %lu us\n",
                      (unsigned long)out_store_last_erase_us());
        }

        can_report(now);
#if SENSE_TRACE
        /* The trace build's console: one character in, and the lines the
         * transmit buffer has room for.  Before the clock is read again,
         * as the report is. */
        sense_trace_hw_pass();
#endif
#if IOMCU_CONSOLE
        /* The bench console's lines in and its output out, the same way. */
        console_hw_pass();
#endif

        /*
         * The clock is re-read here, once, and this is the only place a pass
         * re-reads it.
         *
         * One clock per pass is the rule and it holds because a pass is
         * short.  Two things in this one are not.  A flash window stops this
         * core for about 19ms with interrupts off.  can_report() prints to a
         * USB host, which blocks for as long as the host takes, up to 500ms
         * (outside the bench console build, see IOMCU_SAY).  Everything
         * below measures elapsed time across whichever of them just ran:
         * s_last_rx_ms is what the next pass's quiet-bus guard subtracts
         * from, so a frame collected after one of them and stamped with a
         * clock from before it reports a quiet bus at the moment a frame was
         * handled.  The next page program then passes the 5ms guard
         * immediately and lands in the same burst of requests, which is the
         * collision the guard exists to prevent.
         *
         * link_dev_tick() has the same reason with a bigger margin: tens of
         * milliseconds against a 200ms silence timeout.
         *
         * Unconditional rather than after a window only.  A timer read costs
         * nothing beside either of the two, and a condition here is a list of
         * what can block that has to be kept in step with the code above it.
         */
        now = (uint32_t)to_ms_since_boot(get_absolute_time());
        s_now_ms = now;

        /* Straight after the report: printing to a USB host can take
         * milliseconds, and the part holds two frames. */
        can_service(now);
    }
}
