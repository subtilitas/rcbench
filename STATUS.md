# Where rcbench stands

The running record: what exists, what is open, what is settled. The suite
list, the coverage table, the compile table and every link are held to the
tree by `tools/check_docs.py` and `tools/coverage.py`. The directory sketch
below is not: nothing checks it, so read it as a map and not as an inventory.

[README.md](README.md) is the front page; the
[wiki](https://github.com/subtilitas/rcbench/wiki) is the manual.

## Architecture

Two processors. The coprocessor (RP2350) owns everything with a deadline; the
panel (ESP32-S3) owns everything else. Nothing raw crosses the link: bit
timing, GCR (group-coded recording) decoding, receiver framing and pulse
capture stay on the coprocessor, and only results travel.

| | Panel | Coprocessor |
| --- | :---: | :---: |
| UI, touch, screens, settings, SD card | ✔ | |
| Stop | drives the heartbeat line; sends the STOP command | honours the line; times out the link |
| Servo outputs, PPM (pulse-position modulation), DShot, bidirectional DShot | | ✔ |
| Receiver buses: S.BUS, iBUS, SUMD, CRSF, EX Bus, SRXL2 | | ✔ |
| One-wire programming | | ✔ |
| Current, voltage, cells, rpm (revolutions per minute), temperature | | ✔ |
| Accelerometer and index pulse, one timebase | | ✔ |
| Balancing arithmetic, plots, verdicts | ✔ | |
| Power path, current limiting, protection | | ✔ |
| CAN | TWAI (Two-Wire Automotive Interface, the ESP32-S3's CAN controller) controller | XL2515 over SPI (Serial Peripheral Interface) |

**Link.** Classic CAN at 1 Mbit/s. A 29-bit identifier carries priority, op,
page, offset and count; a frame carries up to four registers; the transport
does no reassembly; the coprocessor transmits only when asked. Worst-case
payload 52 kB/s against 12 to 30 kB/s of expected traffic. Protocol version
4.22. [Reference](docs/Link.md).

**Safety.** The panel's control task drives GPIO6 (J8) from the core that does
not draw. The task runs every 5 ms; the line edges every 20 ms
(`HEARTBEAT_PERIOD_MS`). The coprocessor's
outputs are to be gated by a retriggerable monostable with a window of about
150 ms, and the line is checked in firmware (4 to 150 ms between edges, four
good intervals before it is trusted). The coprocessor fails safe after 200 ms
of link silence; the panel escalates after 1 s. The coprocessor starts with an
arm latch set, sets it again on link silence and on a distrusted heartbeat,
and releases it only on a CLEAR; the panel latches a stop when the link goes
quiet while the bench is ARMED. [Reference](docs/Safety.md).

**Outputs.** Every output is a channel (0 to 1000 of its own travel) with a
role (throttle or surface) rendered by a driver from a table (PWM (pulse-width
modulation), PPM, DShot, bidirectional DShot). The switch to ARMED, clamping, slew and the
silence timeout are implemented once, in `shared/outputs/`, and used by both
processors. The role is what a screen's command looks for: the MOTOR & ESC
throttle commands every channel bound as a throttle, so an ESC on a bound pin
follows the slider and a servo beside it does not. On the wire: the
`CHANNELS`, `CHAN_CFG` and `OUTPUTS` pages. The
pin is in the slot, not in the firmware: the coprocessor refuses the pins it
must not drive and binds the rest as they arrive. A bank in ARMED drives every
bound pin whether or not anything commands it: a channel nobody has commanded
for 500 ms is rendered at its role's rest, which for a surface is the midpoint
of that channel's own endpoints -- 1500 us across the default 1000 to 2000 us,
and 760 us across the 660 to 860 us of a narrow servo. The timeout moves the
channel to that rest and leaves the pin driving; switching to SAFE is what stops it.
[Reference](docs/DShot.md).

**Measured or modelled.** A coprocessor that answers reports only what it
measures, with a valid bit per quantity and no `SIMULATED` flag. The panel
models the whole bench when nothing answers, and marks that with the
watermark. The two never appear on one screen.

**More than one coprocessor.** The coprocessor reports which board it is in
the identity page's hardware register, and the panel offers that board's pins
and no other. A board this build does not know offers nothing and reserves
every pin: guessing a pin map is how an output reaches the safety line.
Changing board clears any selection, because a pin index belongs to one
board's catalogue. A board whose outputs are soldered is shown rather than
offered.

**The output binding lives on the coprocessor.** A binding describes wiring,
and the panel is not the board the wires are in. The coprocessor keeps the
`OUTPUTS` and `CHAN_CFG` pages in its own flash and restores them at boot; the
panel stores none of it and reads the page back when the link comes up.
Restoring configures the outputs and does not drive them -- every driver is
gated on the bank being ARMED, which the coprocessor grants only while the ARM
register is set, the link is out of failsafe, the heartbeat is trusted and the
arm latch a start sets has been released by a CLEAR --
and channel commands are not restored, so a bench never comes back holding the
throttle it was last given.
The save waits for the bank to stop driving and then for a gap in the
traffic, because writing flash stops the core answering: two sectors of
sixteen record slots spend one sector erase per sixteen saves, and that erase
is taken in a gap ahead of the save that needs it.

## State

| Subsystem | State |
| --- | --- |
| Rasteriser, fonts, touch mapping, theme, widgets, icons, router | built and tested; text is UTF-8, one cell a code point, and the two text faces carry Ä Ö Ü ß ä ö ü |
| Interface language | built and tested on the host: English and German, chosen by SETUP's Language and applied on the next frame without a restart. Every translated string is an ID in `shared/ui/include/ui_text.def`, German is `shared/ui/ui_text_de.c`, and a missing entry shows the English. The servo test's TXT report is in the language its run started in; its CSV stays English. `render_ui.py --fit` draws every view in both languages and fails on a German string that overflows; `test_text` holds the rest. [Reference](docs/Language.md) |
| Settings model and screen | built and tested on the host. The panel loads and saves the values in NVS (non-volatile storage) through `firmware/panel/components/settings_nvs/`. The home tag that leaves SETUP with values unsaved opens a warning listing each change against the stored copy, with SAVE, DISCARD (refused while ARMED) and STAY; no route the panel takes by itself is held (`test_settings`) |
| CSV (comma-separated values) and number parsing, log viewer | built and tested against the fixture corpus |
| Logger | built: a run is written while ARMED, or while the supply's output is on with the bench SAFE, in the format the viewer reads. The log of a bench in ARMED has one row per sample the control task takes -- an answered BENCH read with the link up, a 50 ms model step with it down -- and its time column is the wall time since the switch to ARMED; a poll that brings more than one INA3221 window writes one row for each, the sample in the last; the rule is `shared/bench/log_cadence.c`, host-tested in `test_log_cadence` against a model of the control task's pass grid and in `test_logwriter` against the modelled INA3221 through the coprocessor's pages. An automatic servo test's CSV and report take the next run number, and the bench log writes no row while the test runs (`log_cadence_bench_run()`): its time column steps over the test. The card is written by the `runlog` task, not by the control task that beats the safety line, and the file is committed every 20 rows or 1000 ms of run, so a power cut mid-run costs that much of it plus whatever the queue to that task holds -- under 1.0 s while the card keeps up, 84 rows and 4.20 s at 20 Hz with the queue full. A row the cut stops part way through has no line feed; the reader leaves it out and reports it (`log_analysis_t.cut_row`), host-tested at every cut position of a row in `test_logwriter` |
| Board, display, GT911, SD card | built; the panel boots and reports each step on the splash |
| Panel firmware update and card files over Wi-Fi | built and host-tested (`shared/fwupdate/`, `shared/webaccess/`, `test_wifi_screen`); the panel side is `firmware/panel/components/wifi_access/` and `fw_ota/`. A panel install and the coprocessor's update exclude each other (`fw_gate()`, `fw_coproc_may_start()`, `test_fw_install`): the install waits while the coprocessor's update or BUY runs, and the coprocessor's question opens and its START is taken only with the radio wholly off and no install under way. Not run on a panel: the transfer rate, the range, the time of an erase and a write, what the glass shows during an erase and the free internal heap with the radio and the display up are not measured. [Files & update](docs/Screens.md#files--update), [migration](docs/FirstRun.md#1-flash-both-boards) |
| Release check, reminder and download from GitHub | built and host-tested: the strict reading of the release tag, the redirects and `SHA256SUMS` (`shared/fwupdate/fw_release.c`, `test_fw_release`), the check and the download with their budgets, aborts and a scripted HTTP client (`shared/webaccess/wa_fetch.c`, `test_wa_fetch`), and UPDATES, the reminder and the download's screen (`shared/ui/upd_screen.c`, `test_upd_screen`). The panel side is `wifi_access_fetch_*()` over esp_http_client in `firmware/panel/components/wifi_access/`. Not run on a panel or against GitHub: the TLS (transport layer security) handshakes, the durations of the check and the download, and the free internal heap while a TLS connection runs are not measured. [Updates](docs/Screens.md#updates) |
| Shell: band, router, splash, menu, simulation watermark | built |
| Motor & ESC (electronic speed controller) screen | built; reads `bench_state` from the link or the simulator. ARM, DISARM, STOP and the throttle are written to the coprocessor's control page at every 50 ms poll while the link is up; switching to ARMED writes CLEAR on its own, then ARM, THROTTLE and MOTOR_POLES in one frame, and a NACK to either leaves the panel SAFE. The throttle changes only while ARMED: while SAFE, the slider, its `-1` and `+1` steps and the rotary knob are refused and the value is 0 %, which every switch to SAFE returns it to; both bench screens read the value's state (OFF, ARMING, LIVE, LEAVING) from `ui_value_gate.h` (`test_motor_arm`, `test_value_state`; host suite only) |
| Servo screen | built; writes `CHAN_CFG` and `CHANNELS` over the link, the frame rate on `SERVO` to a coprocessor speaking protocol 4.1, and a sweep (square, sine or triangle, run by the coprocessor) to one speaking 4.2, which PAUSE holds (the button then reads PAUSED) and a tap on PAUSED carries on from the phase it was held at on a coprocessor speaking 4.6 (`LINK_SV_RESUME`), and starts over from the curve's beginning, with an alert, on an older one or one that refuses; SPEED's row reads SPEED LIMITS THE SWEEP while SPEED is slower than the curve's fastest change. A SETTINGS overlay sets the type (STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760), the frame rate, the pulse widths, trim, travel and reverse for the session, and the automatic test, its limits and the device under test in NVS; a heli type or a rate above 60 Hz is applied only through a warning held for 2 s, and every restart is STANDARD PWM at 50 Hz. The right card shows the servo rail as its meter reads it (`servo_source.h`), in the CURRENT row, on the line over the plot and in the plot: CH1's 50 ms windows under the name `INA3221 CH1` while the INA3221 is the meter, the supply's samples under `PD MINI` otherwise -- the PD mini's when SETUP enables it, `supply_sim_t`'s with a MODELLED pill if not. A current is signed and shown in hundredths of an amp, a window with a sample at an end of the range in the warning colour, a reading that did not arrive as dashes; the row's value is repainted by itself when its digits or colour change (`test_servo`; host suite only). START TEST on the TEST page runs the automatic test (`shared/servo/servo_test.c`): the supply stepped through 4.8 and 6.0 V, and 7.4 and 8.4 V with HV SERVO on (session only, through the HV warning's 2 s hold), idle, moving and holding current and the travel time at each, a brown-out walk from 5.0 V down in 0.2 V steps, and `BENCHnnn.CSV` and `BENCHnnn.TXT` written by the `runlog` task. A run reads one meter from its start to its end, the servo rail's at START TEST: the PD mini's readings, or, while the INA3221 is the meter and the supply is the PD mini, CH1's 50 ms windows -- mean current, the highest and lowest 1 ms sample, the bus voltage at the load side of the shunt and its lowest sample -- with the supply read for its state only (`test_servo_test_win`, through the modelled INA3221, the coprocessor's schedule and pages, `sense_link` and `servo_source`). A run on the INA3221 ends ABORTED when the INA3221 stops being the meter or no window arrives for 200 ms (`SERVO_TEST_WIN_STALE_MS`, 199, 200 and 201 ms and the millis wrap in `test_servo_test_win`), and never goes on with the PD mini. Currents are compared by magnitude in whole mA; the supply in constant current for 1000 ms ends a run whatever STALL AT is, and a STALL AT at or above the current limit or the INA3221's range is said on the TEST page and in the report. Movement is a reading max(0.020 A, 3 x the idle readings' standard deviation) from the level before the command; a move with none is unseen, neither timed nor late, and a run with unseen moves and nothing failed reads NOT MEASURABLE. On the PD mini, whose readings lag about 0.3 s and repeat, and on the INA3221's windows, from which the panel times a move to 50 ms, every travel time is an upper bound and TRAVEL TIME is reported, not checked. STOP, a switch to SAFE, link loss, the supply and the operator end a run with the output off and a report marked ABORTED. A position is taken only while ARMED: while SAFE, the dial, the rotary knob, CENTRE and SWEEP are refused and the value shown is the one last driven, and at every switch to ARMED the commanded value is the surface's rest, the midpoint of the channel's endpoints; RELEASE while ARMED sets it to that rest as well (`test_servo_arm`; host suite only). |
| Supply screen | built and tested on the host; the control task drives the PD mini through the SUPPLY page (`shared/bench/supply_link.c`) when SETUP INTERFACES enables it, and runs `supply_sim_t` otherwise. Set points shown beside their readings, a keypad, a SETTINGS overlay (caps, start values, current and voltage trips, the confirmation for live changes) kept in NVS, and a question before a set point changes a live output. Every stop, a trip, a lost ON and a supply that stops answering switch the output off; the supply is stepped and logged from `control_pump()`, so it keeps its 50 ms cadence while an exchange waits. A supply run is logged with its own columns |
| PD mini driver | the codec and driver (`protocols/pdmini/pdmini.c`) are built and tested on the host against a modelled module; the coprocessor runs it on a PIO UART on the pins the SUPPLY page (protocol 4.3) names, and the panel writes and reads that page. The module's baud rate is 38.4 kbaud, which the automatic baud search finds. A set point above the module's input voltage sends the module into a fault that only a power cycle clears, so the panel and the driver cap the set point 0.7 V under the input. The module is read every 102 to 106 ms, a change of current shows in the readings 0.31 s after the command (median), and a current often repeats over several readings. The wiring is kept in the coprocessor's flash with the output bindings (record version 4; version 3 records still load) and driven at boot with the output off; the command is not kept. A wiring change while a module has answered waits for a state read sent after it and is taken only if that read shows the output off (protocol 4.5). A live output whose input reads under the set point plus 0.5 V on 2 input reads in a row is switched off and the panel names the input and the set point; the 0.2 V between the cap and that line covers 3000 mA through 1 m of 20 AWG cable out and back, and the rule is chosen, not measured. The PIO receive FIFO holds 8 bytes, 4.2 ms of reply at 19200 baud; the main loop's pass time is not measured |
| Analyser, programmer, balance, battery screens | built, rendered from models. The programmer's ESC and SERVO classes are a model with no transport: CONNECT, READ and WRITE change the panel's own copy of a device and send nothing on a wire, and every page of the two classes carries the MODELLED mark and says that no device is reached (`test_programmer`). ESC STICK runs go out through the arming policy and the output bank |
| ESC programmer protocols | the screen is table-driven for ESCs with BLHeli_S, AM32, ESCape32 and VESC firmware; no protocol is transmitted. ESCs with BLHeli_32 firmware are not supported: [details](docs/BLHeli32.md) |
| Balancing measurement | the screen and the placement guides are built; the accelerometer and the index sensor are not fitted, so nothing is measured |
| Link codec, page map, dispatcher, both watchdogs, CAN framing | built and tested on the host |
| CAN drivers (TWAI on the panel, XL2515 on the coprocessor) and the echo self-test | built |
| Bring-up diagnosis, both ends' counters compared | built |
| Heartbeat generator and monitor | built and driven at both ends; the monostable is not fitted |
| Coprocessor firmware | answers the identity, status, control, bench and three output pages; fails safe at 200 ms. The decision to switch to ARMED is `shared/safety/safety_gate.c`, called once a pass and at every CONTROL write and host-tested in `test_safety_gate` against a model of both main loops: an arm latch set at a start, on link silence, on a distrusted heartbeat and by a refused ARM, released by CLEAR alone. CHAN_CFG and OUTPUTS refuse a role, a throttle channel's range and a slot change while ARMED. It publishes what the ESC reports over extended DShot telemetry -- speed, voltage, current, power and the ESC's own temperature. With an INA228 enabled on the SENSE page and answering, BENCH carries the INA228's voltage, current, power, peaks, charge and energy instead (see Current monitors). Core 0 runs the link and the outputs; core 1 runs the current monitors' bus and nothing else |
| Coprocessor firmware update over CAN | built and host-tested. The boot ROM's partition table holds slots A and B of 2036 KiB; the panel writes the slot that is not running through UPDATE_CTL (0xF0) and UPDATE_DATA (0xF1) in 64-byte blocks with a CRC-16 each and a SHA-256 over the image, the coprocessor restarts into it on trial (TBYB, try before you buy), and the panel's BUY keeps it; without the BUY the boot ROM's 16.7 s watchdog returns to the old image. The coprocessor refuses ARM for the whole session and writes flash only behind the store's gate. A session ends after 5 s without a request to either update page, and a panel that finds one at its check ends it with ABORT. After the BUY the coprocessor erases the old slot's first sector, and owes the erase until it holds when it fails or the gate is shut. The panel carries the image when built with `RCBENCH_IOMCU_BIN`, checks the coprocessor's build by SHA-256 at link-up and offers the update on a question that takes the 2 s hold. `test_link_update` runs both ends over the link layer against a simulated flash and boot ROM; `test_coproc_update_screen` the question and the progress. The transfer time is not measured; the bus time for the 0.18 image is 7.1 to 8.6 s. A coprocessor loaded before the table takes one USB load of `rcbench-iomcu-install.uf2`. [Reference](docs/Link.md#firmware-update-over-the-link) |
| Output drivers (PWM, PPM, DShot, bidirectional DShot) | built; the frame arithmetic, the group code, the reply sampler and the GPIO-to-PWM-slice fold are host-tested. No instrument has been on any output pin. [Reference](docs/DShot.md) |
| S.BUS decoder | built and tested; the PIO (programmable input/output) receiver is not written |
| Motor pole count over the link | the panel sends `Motor poles` on the CONTROL page when the coprocessor answers, at every edit, and in the frame that switches to ARMED, so a run starts on the count that was sent or does not start; with none sent the coprocessor reports no speed rather than one derived from a guess |
| Outputs screen | built and tested on the host: a protocol list and a pin grid behind the OUTPUTS tile on SETUP. Ticking or unticking a pin writes the binding and reads it back; a pick in the list writes nothing, and OFF shows every bound pin's protocol and edits nothing. A write goes one frame per exchange, each acknowledged (`shared/link/link_port.c`); on a coprocessor speaking 4.10 both pages are prepared on BIND_CFG and BIND_OUT and put in force by one BIND commit carrying their CRC-16, both or neither (`shared/outputs/bind_link.c`, `out_stage.c`), and a coprocessor older than that is written entry by entry. A read gives a binding, pages not read, pages no binding describes, or a board with no pin map; in the second and third the screen and PICK A PIN keep the last binding read, dimmed, and send no write, except UNBIND ALL PINS held 2 s over pages no binding describes. `test_bind_link` runs the sequences against the coprocessor's page rules and a model of the XL2515's 2-frame buffer: a 20 ms deaf window at every 125 us, a frame lost at each of 18 positions, the timeouts across the 2^32 ms wrap. Whether one frame per exchange ends the `NO LINK` a tester's bench shows on 0.14.0 with an INA3221 enabled is not known; why a frame is lost there is not known. Reserved pins are shown and refused. PICK A PIN has the same protocol list, the count `N OF 8 SLOTS IN USE` with the refusal reason, a PAD column, and shows the programming port's pin (GP13 by default) held under `PROG PORT` (`test_picker_screen`, 26 cases). |
| Output binding in the coprocessor's flash | built: the data partition `store` of the partition table, 0x3FC000 to 0x3FDFFF (two 4 KiB sectors), as 32 record slots, read by storage address through the uncached, untranslated XIP (execute in place) window; a store with no record reads the store's old place, the last two sectors of the 4 MB, once, so a USB load that installs the table keeps the bindings (on an RP2350 A2 the load's RP2350-E10 block can erase the second of those sectors); restored at boot, saved once the bank is idle and the bus quiet. A save is one page program; a sector erase falls to one per sixteen saves and is taken ahead of the save that needs it, or at boot. The placement rules are host-tested in `test_outstore`; Eight saves of the single-sector store this one replaces printed an erase-and-program window of 19,174 to 19,186 us. Every erase and program runs through `flash_safe_execute()`, which parks core 1 in RAM for the window; a window core 1 does not stop for within 10 ms writes nothing, prints `window refused` and is tried again 1000 ms on. A boot whose core 1 does not register for the lock-out within 100 ms switches the store off: no window opens and `flash_safe_execute()` is not called, set-ups run from RAM until a restart, STATUS fault bit 6 is set and the console says so every 3 s. The offsets are the 4 MB module's rather than the 16 MB board file's. The record is version 5: the bindings, the supply's wiring and the current monitors' set-up. Version 3 and 4 records load, with what they lack off; that decode is `shared/outputs/out_store_rec.c`, host-tested in `test_outstore`. A record of any other version reads as unwritten, so the first boot on such a store starts from the defaults -- no driver and no pin in any slot -- and the binding is gone until an operator sets it again on the OUTPUTS screen |
| Current monitors (INA228, INA3221) | drivers built and host-tested against a modelled bus in `test_ina228` and `test_ina3221` (`shared/sense/`). Link pages built and host-tested in `test_sense_page`: SENSE and SERVO_SENSE (protocol 4.7), the bus's pins, clock, addresses, shunts and channels validated (the INA228's shunt and maximum by the driver's `ina228_calibrate()`), kept in the coprocessor's flash, and the pins held from the outputs. The sampling schedule (`shared/sense/sense_sched.c`) built and host-tested in `test_sense_sched` against the modelled bus: per 1 ms tick on a 400 kHz bus INA3221 CH1 at 1000 Hz, CH2 and CH3 at 50 Hz or 1000 Hz while a pair runs, the INA228's current and voltage at 500 Hz each, its die temperature and the INA3221's Mask/Enable at 25 Hz and the rest at 50 Hz; 50 ms windows with a clipped reading kept out of them; a ring of CH1's last 4 windows, read on the page SERVO_WIN (0x31, protocol 4.11), in which a clipped sample counts at the end of the range and is counted; each part's set-up register read back every 40 ms, a part found on its power-on set-up taken offline, counted in SENSE register 31, its open window emptied and set up again after 1000 ms; the run's INA228 peaks and its ENERGY and CHARGE cleared at the switch to ARMED, no longer the run's after one failed INA228 read; a move capture on CH1, in 1 ms steps, timed from a PWM edge the caller gives, judged by the servo test's own rules (`shared/servo/servo_move.c`, `test_servo_move`) with a 4-sample filter, the level before the command the mean of CH1 over the 50 ms before the edge. The coprocessor reads both parts on core 1 (`firmware/iomcu/src/sense_core1.c`, `sense_i2c.c`): a 1 ms tick, the I2C block at 400 kHz on the SENSE page's pins with the pads' pulls off, a 1 ms limit on each transaction, a bus clear of 9 clocks and a STOP when the bus sticks, and an address scan for PRESENT, repeated every 1 s while a part is missing. Core 1's step is `shared/sense/sense_svc.c`, host-tested in `test_sense_svc`. Core 0 hands it orders and takes back a snapshot, each a copy under one spin lock; it publishes the snapshot into SENSE and SERVO_SENSE, fills BENCH from the INA228 at the existing scales with bits 5 and 6, and stamps the capture's edge from the PWM slice's counter, good to 1 µs. The flash store's windows run through `flash_safe_execute()`, which parks core 1. Both images compile, and [First run](docs/FirstRun.md) §8 lists what a bench session measures. The panel sets both parts up from SETUP INTERFACES (`shared/bench/sense_link.c`, host-tested in `test_sense_link` against the coprocessor's page code and against a 4.6 coprocessor, which it sends nothing): SENSE read at every link-up, a set-up written 500 ms after its last edit while the bank is SAFE and only where it differs, both parts off on the page while a part's own frame changes with both enabled, a refused frame not written again until the next edit, and the identity page read again after a write is taken. It reads SENSE every 50 ms while a part is enabled and SERVO_SENSE every 50 ms window while the INA3221 is, and says on the band, once each, a part not answering 2.5 s after its set-up was taken, another identity at its address, a stuck bus, a clipped reading and STATUS fault bit 6. MOTOR & ESC's TABLE pane shows the ESC's own telemetry beside the INA228's readings with the difference, the totals are the INA228's while BENCH bit 6 is set, and a run's CSV file carries `ina voltage`, `ina current` and `esc current`, and each INA3221 window once. On a 4.11 coprocessor with CH1 enabled the panel reads SERVO_WIN every poll and takes each CH1 window number once and in order, none lost with reads up to 200 ms apart and the lost ones counted beyond (`test_sense_windows`, the modelled part through the coprocessor's schedule and pages); a 4.10 coprocessor is sent nothing on that page. `shared/bench/servo_source.c` names the servo rail's meter once per poll -- the INA3221 after 1000 ms of a held set-up, a part online, a window with current in the last 200 ms and a reset count that stands, the PD mini otherwise, the model with no link -- host-tested in `test_servo_source`; the answer and the windows reach the render task, where the SERVO screen shows the meter's reading in its CURRENT row, line and plot. The panel arms no capture; an automatic servo test reads the meter named at its start, the INA3221's windows or the PD mini (Servo screen, above) |
| Trace of CH1's 1 ms samples (`SENSE_TRACE`) | built and host-tested. A coprocessor build option, off by default: a released image holds none of its code, and CI fails when the default image carries a trace symbol. With it on, core 1 copies each CH1 sample and each CH1 bus voltage out of the schedule's memory into a ring of 4096 records (49,152 bytes), with no bus transaction added to the tick, and core 0 prints them as text on the USB console: 4 s after a changed pulse on a PWM slot or a capture edge, 10 s after `t` on the console, 8.35 bytes a sample at 3-digit codes (9.35 above about 0.4 A on the 0.1 Ω shunt) in the output of `sense_trace_gen`, not measured on hardware. Core 0 writes whole lines into the room the console's 64-byte transmit buffer has and nothing without a terminal; a full ring drops records and the trace counts them, in its end line every one from before its end, with or without a record kept after it; a change of the part's set-up or state is a record in the ring, in its place among the samples, with the start time of the tick it was seen in; a trace whose lines a connected console has not taken 29.8 h after its end is dropped; a trace ended early by the console or a changed set-up writes what the ring held from before its end. Core 1 publishes the start time of each tick after that tick's records; a trigger line and the end line wait for a tick that started at or after their time, 100 ms at most, and an end line written without it carries its reason as a capital letter, which the tool reports as a problem. The rules and the line format are `shared/sense/sense_trace.c`, host-tested in `test_sense_trace`; the firmware side is `firmware/iomcu/src/sense_trace_hw.c`. `tools/sense_trace.py` reads every line of a captured console against the format (every field of the line's type, nothing else, each number in its range) and each trace against its end line, reports a trace with any other line in it as damaged and replays none of its moves, writes a CSV file per trace and replays every move through `shared/servo/servo_move.c` (`test/host/sense_trace_replay.c`) with the filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A; from the traces alone it names the settings that see and time every move and how far apart the settings' arrivals lie, and with a servo test's CSV file recorded with the AS5600 the arrival less the encoder's travel time; a move is not replayed when a window its levels come from (the samples before the first command, the 50 ms and the 200 ms before a move, the move's own samples) has no sample, missing records or a change of the part's state, or when another command lies within 30 ms of it; `--loaded-channel N` (repeatable) names the output channels with a servo connected, and the `$C` and `$D` lines of every other channel are then no moves and count for no 30 ms, which the report's second line counts; the format has no check value, so a line cut inside its last number is read as cut; ctest runs it on the console log `test/host/sense_trace_gen.c` writes for the servo model (`sense_trace_tool`). Not measured: the time core 0's pass gains from writing the lines. [First run](docs/FirstRun.md) §8.9 has the bench procedure: part A without an encoder, part B with the AS5600 |
| Output encoder (AS5600) | built and host-tested. An AS5600 magnetic angle sensor on the servo's output shaft, at 0x36 on the sensor bus (protocol 4.9). The driver (`shared/sense/as5600.c`, `test_as5600`) decodes STATUS, RAW ANGLE, AGC and MAGNITUDE and takes differences on the circle; the schedule reads STATUS and RAW ANGLE at 500 Hz on the ticks without a rotation item, each register in a transaction of its own (STATUS 1 byte, RAW ANGLE 2, AGC 1, MAGNITUDE 2), and one slot in 25 reads RAW ANGLE, AGC and MAGNITUDE instead of STATUS (20 Hz, 337.5 us), and keeps how long the angle has stayed within 12 counts (1.05 degrees) of an anchor, to the 2 ms sample interval. SENSE gains ENABLE bit 2 and registers 26 to 31 (flags, angle, magnitude, sample count, still time); the panel gates both on minor 9, says so on the band for an older coprocessor and for a missing, weak or strong magnet, and reads registers 12 to 31 every 40 ms (`test_sense_page`, `test_sense_link`, `test_link_pages`). SETUP INTERFACES has AS5600 (default OFF) and its centre count; the SERVO screen shows the angle in the MEASURED row and sets the centre from the live count on the DUT page (`test_servo`). With it on, the automatic servo test (`test_servo_test`) adds per step the end angles, the angle error against the commanded angle and a travel time from the start of the angle's 100 ms stillness within 1.05 degrees, beside the current's, with two CSV columns and a report section; the deadband is not derived, because the moves go end to end. An angle read with STATUS MD clear (no magnet) is no position: the coprocessor holds no still time for it and `sense_link_enc()` hands it to no consumer, so MEASURED shows `---`, ENC CENTRE stores nothing and a run judges no angle and reports how often it happened; with ML or MH beside MD the angle is used and counted in the report. STATUS bits 7, 6, 2, 1 and 0 are masked off, and the probe refuses only ML and MH both set. Angles are compared and averaged on the circle; an end more than 180 degrees from the centre count is reported 360 degrees off, and turns are not counted. Not measured: what the blank STATUS bits read on a part, the sensor on the bus, the tolerance against a real servo's jitter, the delay of the command to the pin |
| Phase tap (ESC tones) | the detector (`protocols/phase_tap/tone.c`, `test_tone`) turns edge times into beeps and their pitch. The capture is built and host-tested: the PIO program (`protocols/phase_tap/rp2350/tone_cap.pio`, 22 of a block's 32 instructions) assembled from its source and run in a cycle-counting model against `tone_holdoff_edge()` in `test_tone_pio` (every decrement of the counter 4 cycles after the one before; a low of 1200 cycles, 8 µs, always kept and one of 1196 never); the word decode, the 31-to-64-bit extension and the lap detection in `test_edge_ring`; the pass core 1 runs in `test_tone_svc`; the TONE page (protocol 4.8) with its pin checks, the 64-beep ring and the set-up kept in flash (store record version 6) in `test_tone_page`, `test_link_pages` and `test_outstore`. Core 1 reads the ring on its 1 ms tick (`tone_core1.c`) and core 0 owns the state machine and the DMA channel (`tone_cap.c`). Not measured: the PIO program's timing against the system timer, the 2-cycle input delay the model assumes, the DMA ring at 96,000 edges/s and the load on core 1. The panel's side is not part of this row |
| Other receiver buses | not started |
| Servo limit search | built; START LIMIT RUN on the SERVO screen's LIMITS page, a 2 s hold while ARMED, runs it (`shared/servo/servo_limrun.c`) in 10 us steps on INA3221 CH1's 50 ms windows, bound above 1.8 x the free current and +0.15 A: on a SERVO PWM pin from PULSE CENTRE to PULSE MIN and PULSE MAX; on the programming port after a read session, from the servo's NEUTRAL to its own PULSE LOWER and PULSE UPPER, refused before any drive when those lie outside PULSE MIN to PULSE MAX. SERVO PULSES at 1000-2000 writes PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0 us in that read session. It reads the INA3221 and nothing else: with the PD mini or the model as the servo rail's meter it is refused before anything is driven, as it is on a rail under 3.0 V in the last window, on the programming port without a PD mini on a rail under 4.8 V or with a PD mini whose output is off, and, on the bench's outputs, with no output bound as SERVO PWM; a run whose meter changes or that gets no window for 200 ms ends ABORTED with the output off. The search and the protections read the current's magnitude. A sample at 3.0 A, a clipped one, a search's window without the rail voltage or under 3.0 V, window means above 1.0 A for 400 ms, 40 s a search, 3 s for the bench to switch, 150 s a programmer step and a supply changed outside the programmer's sessions, an output off at the start or a supply that answers only after the start included, end it too; a run that ends while it waits for ARMED asks SAFE. SEARCH+PROG, for a servo on the programming port, lowers the servo's LEFT ANGLE and RIGHT ANGLE (unit not known) in sessions of the programmer -- centre, SAFE, CONNECT, one write, LEAVE, ARMED, the side searched again --: the travel taken as linear in the pulse, the register for each side found from the first write, 2 writes a bound side (an overset 3 % above the share at the first bound pulse, then the final value from the share the overset's search found free), both ends searched again; at most 8 values a run, a later session reading another type, REVERSION, pulses or range ending it, RESTORE offering the values before the run, a session after the run saving its own backup. Report `BENCHnnn.TXT`. Host-tested against `servo_sim` behind a modelled KST servo (`test_servo_limrun`, 50 cases, among them the low stop from 920 to 1440 us in 3 us steps with both pulse settings; `test_kst_panel` and `test_servo` through the screen and the programmer). The ends found are not applied to the bench's PULSE MIN and MAX. Which register limits which pulse side on a real servo, and whether its travel is linear in the pulse, is not known |
| Servo synchronisation | built and tested against a modelled servo |
| OpenYGE codec | built and tested; not wired in. The implementation is pursued in a separate repository |
| ESC programming profiles | 72 families, 451 models, from 153 manuals; compiled in from `shared/esc/profiles/*.json` and replaced or extended by `/ESC/*.json` on the card at start-up. The reader and the registry are tested on the host. Every profile is unverified and carries no beep timing. The nine profiles for YGE ESCs name where the stick rests while the menu sounds (`scheme.listen`). [Reference](docs/EscProfiles.md) |
| Stick programming | built and tested on the host against a simulated ESC (`shared/esc/esc_stick.c`, `esc_sim.c`): the ESC STICK class on PROGRAMMER runs 24 of the 72 profiles (13 two-stage, 11 one-stage; at 20 V the list opens 23, one needing 22.8 V, and refuses 4 more model rows of families that open, each needing 22.8 V; 4 model rows that state no voltage rating open with a warning from a warning held for 2 s (NO PROPELLER, MOTOR SECURED?: a resistor load or a motor mounted solid without propeller), switching to ARMED and moving the throttle through the MOTOR screen's commands and switching the supply through SUPPLY's. Beeps are counted from the supply current with hysteresis from a quiet line, lengths judged in readings, and a group acted on only when it and the one before it are in the menu's order; a sweep of lost beeps against entry times stores no wrong value in the simulation. A planned end switches the supply off before the stick moves. The list shows the makers, then one maker's models by current, voltage and name, each opening its family's profile at the model's own cell count; a search over maker and family or maker and model filters both levels, case-insensitive with `*` as a wildcard, filtered with every key on a keyboard docked beside it. A stack light on the run and the result shows green while the detector holds a beep (at least 150 ms) and red on an end caused by something not as expected, a stop the bench raised itself (BENCH STOPPED, told from a pressed STOP by the arming policy's count of presses) included. Profiles list the steps a person does at the ESC (`manual`: a jumper, a button) with when each is due; 24 carry them, and the 10 Kontronik profiles that run ask for each with a prompt (DONE, ABORT, 60 s), powered only with the stick at motor-off; each value is powered up from the stick position the manual programs it from (`entry_throttle`: the Kontronik car modes at MID), the stick moved only with the supply off. MANUAL INTERVENTION REQUIRED on a profile's page and a MANUAL tag in the list show the steps. With the PD mini off, the panel's modelled supply draws the simulated ESC's current. Every beep timing is a default in the settings, not a measurement. [Reference](docs/StickProgramming.md) |
| Measurement front end | parts chosen, nothing fitted: the INA228 as motor monitor and three TPS55285 servo converters with a fourth as the adjustable supply (owner, 2026-09-30); 7 INA3221 port monitors, both motor shunts and the BQ25713 pack charger from round 1 of the component research: [hardware](hardware/STATUS.md) |
| Servo programmer | Parameter table for Hitec D-series servos in the programmer screen; KST and Chaservo servos (two brands with one programming protocol): the protocol core, the RP2350 pin driver, the coprocessor's programming port (its own pin, GP13 by default, with servo PWM from the servo tester or the programming protocol, and the PD mini as the servo's supply), link pages 0x32 and 0x33 and the panel's link module are built and host-tested against a servo, line and supply model; the SERVO screen's programmer (PROGRAM) reads, edits, writes, saves and loads through them, host-tested against the same model: [the screen](docs/ServoProgrammer.md), [the ports](docs/Link.md#the-programming-ports), [the protocol on a port](docs/Link.md#the-programming-port-for-kst-and-chaservo-servos), [what a bench has to measure](protocols/kst/README.md#what-is-measured-and-what-is-not) |

## The tree

Directories only. `git ls-files <directory>` lists the files of one.

```
rcbench/
  README.md  README-de.md  STATUS.md  CHANGELOG.md  CONTRIBUTING.md
  SECURITY.md  LICENSE  NOTICE  .gitattributes  .gitignore
  .clang-tidy  .cppcheck-suppress  ruff.toml  codecov.yml
  .github/workflows/      ci · docs · pages · release
  docs/                   the wiki source, English and German
  tools/                  generators, checks and analysis scripts;
                          research/ holds the component research scripts
  test/tools/             pytest cases for the tools
  test/host/              one suite over shared/ and protocols/;
                          fixtures/ holds the CSV corpus
  hardware/               board design record: README, STATUS, docs/
  testbench/              the measurement bench: README, WIRING, host scripts,
                          decoders
  configurator/           the bench configurator web page (own README)

  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    artwork/ gfx/ touch/ ui/ settings/ logfile/ link/ bench/ outputs/
    safety/ servo/ can/ esc/ sense/ fwupdate/ webaccess/

  protocols/              one module per external interface: a pure C core,
                          its RP2350 pin driver in rp2350/, a README.md
    dshot/ ppm/ sbus/ openyge/ pdmini/ phase_tap/ kst/

  firmware/panel/         ESP-IDF: main/, components/, sdkconfig.defaults,
                          partitions.csv
  firmware/iomcu/         pico-sdk: src/, include/
```

### Who compiles what

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` | ✔ | ✔ | ✔ |
| `artwork` · `esc` · `fwupdate` · `webaccess` | ✔ | | ✔ |
| `openyge` · `ppm` | | ✔ | ✔ |
| `sense` · `pdmini` · `phase_tap` · `kst` · `dshot` | ✔ | ✔ | ✔ |

Each module carries one `CMakeLists.txt` that registers an IDF component under
`ESP_PLATFORM` and a static library otherwise. The panel sets
`EXTRA_COMPONENT_DIRS`; the coprocessor and the host suite use
`add_subdirectory()`. Includes are flat: `#include "gfx.h"`. A module under
`protocols/` includes nothing from the rest of the tree, and its `rp2350/`
folder is built by the coprocessor only; `tools/check_protocols.py --check`
fails on an include that leaves the module and on a public header that does
not compile as C++17.

## Tests and CI

CI (continuous integration) runs the workflows below on GitHub Actions.

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under ASan (AddressSanitizer) and UBSan (UndefinedBehaviorSanitizer); coverage `--check` and the Codecov upload, which fails the job when it fails; the font, frame-cost, screenshot, fit, translated-format, docs, wiki-link and research-script checks; `check_protocols.py --check`; clang-tidy and cppcheck over `shared/`, the cores under `protocols/` and `test/host/`, cppcheck's warning, performance and portability classes over `firmware/` and the pin drivers under `protocols/`, and ruff; the pytest cases of the tools (`test/tools/`); on a pull request `tools/mutate.py` on the changed lines of `shared/` and of the cores under `protocols/`, which reports survivors and does not fail on one; the ESP-IDF (Espressif Internet-of-Things Development Framework) matrix (v5.4, v5.5.3) building the panel, each with the task stack check; the pico-sdk build of the coprocessor with `pinmap_check.py` and the stack check of its two cores, and a second build with `-DSENSE_TRACE=ON`, with the same stack check, that fails when the default image holds a trace symbol; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/`, manual | waits for the CI run on the same commit (`tools/ci_gate.py`, at most 5400 s) and publishes `docs/` to the GitHub wiki when it passed |
| `mutation.yml` | Sunday 03:17 UTC, manual | every mutant of the lines of `shared/safety` and `shared/outputs` (3032 mutants) in 6 parallel shards of files, no mutant limit, no mutant started after 18000 s in a shard; the mutants left then are reported as not run; does not fail on a survivor; the result is the artifacts `mutation-full-0` to `mutation-full-5` and the job summaries |
| `release.yml` | tag `v*`, manual for a tag | waits for the CI run on the tagged commit and, when it passed, builds both images, packages them with checksums, creates a release; beside that, without holding the release back, runs the configurator data check (`configurator/core/tools/check-annotate.sh --warn-only`) and shows each warning, such as a release tag newer than the configurator data, as an annotation and in the run summary |
| `pages.yml` | pull request and push to `main` touching `configurator/` | type check, tests, data check, size budget and Playwright browser tests of the configurator; on a push to `main` it publishes the built page to GitHub Pages |

Not in CI: clang-tidy over `firmware/`, which needs the ESP-IDF and pico-sdk
headers to compile each file, and cppcheck's style class over `firmware/`,
which reports 12 findings there with cppcheck 2.17. The cppcheck step over
`shared/`, `protocols/` and `test/host/` resolves the headers under
`shared/*/include`, `protocols/*/include` and `test/host`, and reports no
finding. clang-tidy reports none over the same files except
`test/host/render_screen.c` and `test/host/bench_frame.c`, which CMake does not
build and so have no compile command; `tools/render_ui.py` builds the first
with `-Werror`, and cppcheck reads both. The step over `firmware/` passes all 25. `docs.yml` and
`release.yml` with the wait for CI have not run.

The host suite is 123 binaries, one line per case: `test_gfx`, `test_touch_map`,
`test_touch_merge`, `test_inj_line_id`, `test_inj_emu`,
`test_nav`, `test_widgets`, `test_keypad`, `test_pinlist`, `test_bench`, `test_supply`,
`test_supply_screen`, `test_pdmini`,
`test_motor`, `test_motor_arm`, `test_value_state`, `test_servo`, `test_servo_arm`,
`test_analyser`, `test_programmer`, `test_balance`, `test_battery`,
`test_settings`, `test_logfile`, `test_link_crc`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_link_bringup`,
`test_link_can`, `test_link_sha256`, `test_link_update`, `test_link_artxfer`, `test_link_drawing`, `test_art_store`, `test_art_fetch`, `test_outputs`, `test_outstore`,
`test_can_timing`, `test_can_selftest`,
`test_mcp2515`, `test_heartbeat`, `test_arming`, `test_safety_gate`, `test_flash_gate`, `test_touch_loss`, `test_servo_limit`, `test_servo_limrun`,
`test_servo_sync`, `test_servo_sweep`, `test_servo_move`, `test_servo_test`, `test_servo_test_win`, `test_servo_page`, `test_supply_page`, `test_sense_page`, `test_supply_link`, `test_sense_link`, `test_sense_windows`, `test_servo_source`, `test_sbus`, `test_dshot_frame`, `test_dshot_telem`, `test_dshot_edt`,
`test_ppm`, `test_outbind`, `test_outputs_screen`, `test_picker_screen`, `test_board_drawing`, `test_busfault_screen`, `test_coproc_update_screen`, `test_text`, `test_openyge_frame`, `test_openyge_status`,
`test_openyge_params`, `test_esc_profiles`, `test_esc_stick`, `test_ina228`, `test_ina3221`, `test_as5600`, `test_sense_sched`, `test_sense_svc`, `test_sense_trace`, `test_tone`, `test_edge_ring`, `test_tone_svc`, `test_tone_page`, `test_tone_pio`, `test_tone_link`, `test_knob`, `test_bind_link`, `test_logview`, `test_logwriter`, `test_log_cadence`, `test_fuzz_engines`, `test_kst_wire`, `test_kst_reg`, `test_kst_plan`, `test_kst_session`, `test_kst_pio`, `test_kst_port`, `test_kst_link`, `test_prog_port`, `test_kst_file`, `test_kst_store`, `test_kst_prog`, `test_kst_panel`, `test_kst_card`, `test_prog_link`, `test_wifi_screen`, `test_fw_image`, `test_fw_install`, `test_fw_upload`, `test_fw_release`, `test_wa_session`, `test_wa_fetch`, `test_upd_screen`, `test_wa_text`, `test_wa_walk`, `test_wa_send`, `test_wa_zip`, `test_wa_page` and `test_wa_mdns`. The harness is
`test/host/greatest.h`, written for this project. `test/host/touch_feed.h`
feeds the router, one screen or one widget frames of contacts through
`touch_tracker_update()`, as the panel does, so a touch case cannot use an
event sequence the tracker does not emit. `test/host/tick_wrap.h` runs a
case with the clock started at tick 0 and again a chosen number of ms
before the 32-bit millisecond count wraps at 2^32 ms, and a failure names
the start it happened at. `test/host/guard_canvas.h`
puts 8 guard pixels above, below and beside a canvas, so a pixel written
off the canvas fails the plain build and not only the sanitizer one.
`test_fuzz_engines` runs 600 random servo tests and 2400 random stick
programming runs from 3 fixed seeds and checks at every step what a caller
relies on; a failure states the seed and the case number. ctest runs one
test more that is no suite binary and not in the coverage build:
`esc_parity` (`test/host/fuzz_parity.py`, Python 3) changes the profiles
of record in structure and in text, 1500 cases from each of 2 fixed seeds,
reads each with `tools/gen_esc_profiles.py` and, through
`test/host/esc_parse_dump.c`, with `esc_profile_parse()`, and fails on a
file one takes and the other refuses or the two read differently. It
takes 6.2 s on one x86-64 host, 8.1 s in the sanitizer build, and ctest
stops it after 120 s. `tools/mutate.py --files shared/safety/heartbeat.c
--max-mutants 0` makes 36 mutants and the suite fails on each. A case that is
defined and not run fails the build (`-Werror=unused-function`).
`tools/check_docs.py` holds this list to `test/host/CMakeLists.txt`.
Of the 12 `test_kst_*` binaries, 10 run against the servo and line model in
`test/host/kst_sim.h`; no servo is connected. 5 run the core of
`protocols/kst/`; `test_kst_file` is the one for the configuration file
and reads 40000 random texts from 1 fixed seed. `test_kst_store` runs
the card layer for those files, `shared/logfile/kst_store.c`, against a
directory held in memory. `test_kst_pio` runs the pin driver's arithmetic and its
PIO (programmable input/output) program, word by word, in a model of the
state machine; the coprocessor build fails when the assembled program
differs from the words the test runs. `test_kst_port` runs the
coprocessor's programming port and its page with a model of the supply,
`test_prog_port` the ports' pins and pulse page, and `test_kst_link` the
panel's module through the dispatcher against that port.
`test_kst_prog` runs the programmer's state, `test_kst_panel` its
overlay on the SERVO screen through touches, `test_kst_card` the
adapter between the two and the card layer, with a card that fails,
fills and is removed, and `test_prog_link` the panel's module for the
ports' page. ctest also
builds and runs `kst_headers_cxx`, which calls the module through its 7
headers from C++11, when the host has a C++ compiler.
ctest runs one case more that is not a binary of the suite:
`sense_trace_tool`, `tools/sense_trace.py` on a synthetic console log
(`test/host/test_sense_trace_tool.py`).

Coverage floors: 94% overall, 85% for every file. `tools/coverage.py --check` fails on drift of the table
below and of the figure in the two READMEs, and on a C file under `shared/`
or a core file under `protocols/` that is in neither of its two lists, is not compiled, or has no counters.
`render_ui.py --check` holds 414 committed screenshots to the current
render, 207 in English and the same 207 in German, and `render_ui.py --fit`
fails on a string that overflows where it is drawn, in either language,
except an English overflow it lists as known (none listed); `frame_cost.py` holds a bench frame to 15,600 cache-line fills and a
chrome-cached screen to 2,000. `stack_check.py` holds every panel task's
deepest call chain to its stack less 1024 bytes: the UI's main task reaches
4672 of 8192. With `--iomcu` it holds each coprocessor core's deepest chain,
one interrupt and 256 bytes to the core's stack: core 0 reaches 1708 of
4096 with 528 for an interrupt. `ram_budget.py` holds the panel's
internal heap left with the largest Wi-Fi mode up to at least 32,768 bytes,
by an estimate that counts three allowances that are not measured. `check_docs.py` holds the protocol version,
the heartbeat and link timings, the heartbeat and CAN pins, the coverage
floors, the stack margin, the internal RAM floor, the servo test's constants and the frame-cost
ceilings the pages state to the headers, the tools and `ci.yml`.
`check_sanitizers.py` holds every compile command of the sanitizer build to
`-fsanitize=address,undefined -fno-sanitize-recover=all`.

<!-- coverage:start -->
| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 702 | 680 | 96.9% |
| `shared/gfx/gfx_seg.c` | 96 | 91 | 94.8% |
| `shared/touch/touch_map.c` | 106 | 106 | 100.0% |
| `shared/touch/touch_merge.c` | 92 | 92 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 237 | 232 | 97.9% |
| `shared/ui/ui_icons.c` | 155 | 155 | 100.0% |
| `shared/ui/ui_band.c` | 234 | 222 | 94.9% |
| `shared/ui/ui_watermark.c` | 42 | 40 | 95.2% |
| `shared/ui/ui_plot.c` | 194 | 184 | 94.8% |
| `shared/ui/ui_slider.c` | 163 | 154 | 94.5% |
| `shared/ui/ui_tabs.c` | 79 | 70 | 88.6% |
| `shared/ui/ui_text.c` | 93 | 92 | 98.9% |
| `shared/ui/ui_router.c` | 322 | 318 | 98.8% |
| `shared/ui/splash_screen.c` | 60 | 57 | 95.0% |
| `shared/ui/overview_screen.c` | 72 | 71 | 98.6% |
| `shared/ui/motor_screen.c` | 631 | 615 | 97.5% |
| `shared/ui/supply_screen.c` | 1089 | 1072 | 98.4% |
| `shared/ui/ui_keypad.c` | 164 | 162 | 98.8% |
| `shared/ui/ui_textkey.c` | 236 | 229 | 97.0% |
| `shared/ui/ui_pinlist.c` | 49 | 49 | 100.0% |
| `shared/ui/servo_screen.c` | 3652 | 3543 | 97.0% |
| `shared/ui/kst_panel.c` | 1682 | 1650 | 98.1% |
| `shared/ui/analyser_screen.c` | 224 | 221 | 98.7% |
| `shared/ui/balance_screen.c` | 323 | 323 | 100.0% |
| `shared/ui/battery_screen.c` | 178 | 173 | 97.2% |
| `shared/ui/programmer_screen.c` | 2419 | 2318 | 95.8% |
| `shared/ui/log_viewer_screen.c` | 1049 | 985 | 93.9% |
| `shared/ui/log_select.c` | 26 | 26 | 100.0% |
| `shared/ui/settings_screen.c` | 516 | 511 | 99.0% |
| `shared/ui/outputs_screen.c` | 417 | 410 | 98.3% |
| `shared/ui/picker_screen.c` | 632 | 625 | 98.9% |
| `shared/ui/board_drawing.c` | 88 | 85 | 96.6% |
| `shared/ui/busfault_screen.c` | 299 | 296 | 99.0% |
| `shared/ui/wifi_screen.c` | 593 | 576 | 97.1% |
| `shared/ui/coproc_update_screen.c` | 210 | 209 | 99.5% |
| `shared/ui/upd_screen.c` | 491 | 465 | 94.7% |
| `shared/settings/settings.c` | 290 | 283 | 97.6% |
| `shared/logfile/log_numbers.c` | 393 | 371 | 94.4% |
| `shared/logfile/log_csv.c` | 682 | 647 | 94.9% |
| `shared/logfile/log_fields.c` | 46 | 45 | 97.8% |
| `shared/logfile/log_name.c` | 62 | 62 | 100.0% |
| `shared/safety/heartbeat.c` | 58 | 58 | 100.0% |
| `shared/safety/arming.c` | 180 | 174 | 96.7% |
| `shared/safety/safety_gate.c` | 53 | 53 | 100.0% |
| `shared/safety/flash_gate.c` | 2 | 2 | 100.0% |
| `shared/safety/touch_loss.c` | 45 | 45 | 100.0% |
| `shared/servo/servo_limit.c` | 142 | 136 | 95.8% |
| `shared/servo/servo_sync.c` | 183 | 177 | 96.7% |
| `shared/servo/servo_sweep.c` | 118 | 114 | 96.6% |
| `shared/servo/servo_move.c` | 145 | 145 | 100.0% |
| `shared/servo/servo_test.c` | 783 | 769 | 98.2% |
| `shared/servo/servo_report.c` | 554 | 554 | 100.0% |
| `shared/servo/servo_limrun.c` | 889 | 860 | 96.7% |
| `protocols/openyge/openyge_frame.c` | 172 | 169 | 98.3% |
| `protocols/openyge/openyge_status.c` | 39 | 39 | 100.0% |
| `protocols/openyge/openyge_params.c` | 66 | 66 | 100.0% |
| `shared/esc/esc_json.c` | 893 | 844 | 94.5% |
| `shared/esc/esc_registry.c` | 168 | 166 | 98.8% |
| `shared/esc/esc_stick.c` | 1040 | 1008 | 96.9% |
| `shared/esc/esc_sim.c` | 367 | 343 | 93.5% |
| `shared/servo/servo_sim.c` | 126 | 124 | 98.4% |
| `protocols/sbus/sbus.c` | 55 | 54 | 98.2% |
| `protocols/dshot/dshot_frame.c` | 25 | 25 | 100.0% |
| `protocols/dshot/dshot_telem.c` | 132 | 128 | 97.0% |
| `protocols/dshot/dshot_edt.c` | 33 | 33 | 100.0% |
| `protocols/ppm/ppm.c` | 42 | 42 | 100.0% |
| `protocols/kst/kst_wire.c` | 142 | 142 | 100.0% |
| `protocols/kst/kst_reg.c` | 180 | 180 | 100.0% |
| `protocols/kst/kst_limits.c` | 127 | 127 | 100.0% |
| `protocols/kst/kst_plan.c` | 396 | 396 | 100.0% |
| `protocols/kst/kst_session.c` | 584 | 583 | 99.8% |
| `protocols/kst/kst_pio.c` | 83 | 83 | 100.0% |
| `protocols/kst/kst_file.c` | 589 | 589 | 100.0% |
| `shared/logfile/kst_store.c` | 209 | 209 | 100.0% |
| `shared/outputs/kst_port.c` | 614 | 613 | 99.8% |
| `shared/outputs/prog_port.c` | 313 | 313 | 100.0% |
| `shared/bench/kst_link.c` | 433 | 431 | 99.5% |
| `shared/bench/kst_prog.c` | 1707 | 1669 | 97.8% |
| `shared/bench/kst_card.c` | 134 | 130 | 97.0% |
| `shared/bench/prog_link.c` | 171 | 171 | 100.0% |
| `shared/can/can_timing.c` | 105 | 103 | 98.1% |
| `shared/can/can_selftest.c` | 145 | 135 | 93.1% |
| `shared/can/mcp2515.c` | 44 | 43 | 97.7% |
| `shared/link/link_bringup.c` | 81 | 81 | 100.0% |
| `shared/link/link_can.c` | 104 | 102 | 98.1% |
| `shared/link/link_crc.c` | 13 | 13 | 100.0% |
| `shared/link/link_dev.c` | 86 | 83 | 96.5% |
| `shared/link/link_host.c` | 136 | 132 | 97.1% |
| `shared/link/link_artxfer.c` | 94 | 93 | 98.9% |
| `shared/link/link_drawing.c` | 141 | 141 | 100.0% |
| `shared/link/link_control.c` | 35 | 35 | 100.0% |
| `shared/link/link_port.c` | 38 | 38 | 100.0% |
| `shared/link/link_sha256.c` | 85 | 85 | 100.0% |
| `shared/link/link_update_image.c` | 95 | 89 | 93.7% |
| `shared/link/link_update_dev.c` | 277 | 276 | 99.6% |
| `shared/link/link_update_host.c` | 428 | 425 | 99.3% |
| `shared/artwork/art_store.c` | 193 | 187 | 96.9% |
| `shared/artwork/art_fetch.c` | 81 | 81 | 100.0% |
| `shared/bench/bench_state.c` | 219 | 215 | 98.2% |
| `shared/outputs/outputs.c` | 230 | 227 | 98.7% |
| `shared/outputs/outputs_pages.c` | 311 | 302 | 97.1% |
| `shared/outputs/out_bind.c` | 489 | 478 | 97.8% |
| `shared/outputs/out_pwm_map.c` | 20 | 20 | 100.0% |
| `shared/outputs/servo_page.c` | 236 | 234 | 99.2% |
| `shared/outputs/supply_page.c` | 313 | 310 | 99.0% |
| `shared/outputs/out_store_map.c` | 68 | 68 | 100.0% |
| `shared/outputs/out_store_rec.c` | 20 | 20 | 100.0% |
| `shared/outputs/out_stage.c` | 66 | 66 | 100.0% |
| `shared/outputs/bind_link.c` | 72 | 72 | 100.0% |
| `shared/outputs/sense_page.c` | 494 | 494 | 100.0% |
| `shared/outputs/tone_page.c` | 251 | 251 | 100.0% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/supply.c` | 170 | 168 | 98.8% |
| `protocols/pdmini/pdmini.c` | 585 | 577 | 98.6% |
| `shared/bench/supply_link.c` | 295 | 285 | 96.6% |
| `shared/bench/sense_link.c` | 777 | 749 | 96.4% |
| `shared/bench/servo_source.c` | 108 | 108 | 100.0% |
| `shared/bench/tone_link.c` | 420 | 415 | 98.8% |
| `shared/bench/knob.c` | 37 | 37 | 100.0% |
| `shared/bench/log_writer.c` | 152 | 140 | 92.1% |
| `shared/bench/log_cadence.c` | 42 | 42 | 100.0% |
| `shared/sense/sense_bus.c` | 133 | 132 | 99.2% |
| `shared/sense/ina228.c` | 200 | 200 | 100.0% |
| `shared/sense/ina3221.c` | 132 | 132 | 100.0% |
| `shared/sense/as5600.c` | 78 | 78 | 100.0% |
| `shared/sense/sense_sched.c` | 459 | 459 | 100.0% |
| `shared/sense/sense_svc.c` | 160 | 160 | 100.0% |
| `shared/sense/sense_trace.c` | 642 | 642 | 100.0% |
| `protocols/phase_tap/tone.c` | 372 | 372 | 100.0% |
| `protocols/phase_tap/edge_ring.c` | 43 | 43 | 100.0% |
| `protocols/phase_tap/tone_svc.c` | 109 | 109 | 100.0% |
| `shared/fwupdate/fw_sha256.c` | 83 | 83 | 100.0% |
| `shared/fwupdate/fw_image.c` | 131 | 130 | 99.2% |
| `shared/fwupdate/fw_upload.c` | 55 | 55 | 100.0% |
| `shared/fwupdate/fw_release.c` | 150 | 150 | 100.0% |
| `shared/fwupdate/fw_install.c` | 60 | 60 | 100.0% |
| `shared/fwupdate/fw_boot.c` | 11 | 11 | 100.0% |
| `shared/webaccess/wa_session.c` | 313 | 310 | 99.0% |
| `shared/webaccess/wa_fetch.c` | 316 | 304 | 96.2% |
| `shared/webaccess/wa_text.c` | 287 | 285 | 99.3% |
| `shared/webaccess/wa_walk.c` | 98 | 93 | 94.9% |
| `shared/webaccess/wa_send.c` | 33 | 33 | 100.0% |
| `shared/webaccess/wa_zip.c` | 349 | 333 | 95.4% |
| `shared/webaccess/wa_page.c` | 148 | 143 | 96.6% |
| `shared/webaccess/wa_mdns.c` | 119 | 118 | 99.2% |
| **total** | **43206** | **42219** | **97.7%** |

_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._
<!-- coverage:end -->

## Open items

| Item | State | Needs |
| --- | --- | --- |
| The control page under measurement | the paths a session has to provoke rather than pass through are the failsafe clear, a STOP mid-throttle, the link unplugged while ARMED, a coprocessor reset while ARMED, a panel in ARMED with the coprocessor unpowered and then powered, the first switch to ARMED after a STOP, and a switch refused for each of its four reasons (a servo release still owed, the servo frame rate not known, the heartbeat not reported trusted within 300 ms, a NACK from the coprocessor). The arm latch and the panel's stop on a lost link are host-tested in `test_safety_gate` | a session with both boards that provokes them one at a time, and writes down what the band said. For the reset: a motor channel on GP0, a scope on GP0 and GP3, ARMED at 70 %, the RUN pin held low for 100 ms; pulses must not return on GP0 until ARM is held again |
| Output driver timing | no timing has been seen on an instrument. What a host test cannot reach: every bit timing, the DMA ring that plays a PPM frame, the PIO turnaround from a bidirectional DShot frame to its reply, and whether an ESC other than the one AM32 2.21 ESC on a tester's bench (#172) answers | an oscilloscope, a servo, and an ESC that does bidirectional DShot. [What is unconfirmed](docs/DShot.md#what-has-not-been-confirmed-on-a-wire) |
| The flash save costs CAN frames | measured on the bring-up module: eight erase-and-program windows printed 19,174 to 19,186 us, and the XL2515's overrun count climbed from 2 to 8 while nine saves were taken. A frame at 1 Mbit/s is about 130 us and the controller holds two, so 19 ms is about 150 frame times, or seventy times over the 260 us the two buffers hold, with nobody emptying them; the bus reports no error, because the frames arrived and nobody collected them. A lost request costs the panel LINK_HOST_TIMEOUT_MS (1000 ms) of waiting, and 1000 ms of silence latches this end's 200 ms failsafe, so one lost frame is enough for `FAULT 01`. What the store does about it is above; what it does not do is get under the 260 us the two buffers hold. The 19 ms is an erase and a page program inside one window, which is what the store this replaces printed: neither half of it is measured on its own. The program is the window every save still pays, and 256 bytes against the erase's 4,096 puts it one to two orders of magnitude below 19 ms on serial NOR flash, or roughly 200 to 2,000 us against a 260 us budget | a board: the page program and erase windows from the console lines, the overrun count across sixteen saves, the heartbeat after a window, and a power cut mid-save |
| Screens that do not name the output they drive | the rule is in [CONTRIBUTING.md](CONTRIBUTING.md#the-active-output). The SERVO screen meets it for its PWM target: the TARGET row on its right card names the pins or the programming port and stays in sight under the settings, their keypads and the servo programmer. The MOTOR screen names its throttle pins and the SUPPLY header the PD mini's UART pins. These screens do not meet it: ARM on SERVO and MOTOR switches the whole bank to ARMED and names none of it; the servo programmer overlay names a port number instead of its pin; a PD mini output stays on after SERVO is left while the servo programmer holds its port, and no other screen names it. A user can then drive a pin other than the one they expect | ARM limited to the screen's role; the programmer naming its pin; the held output named in the status band |
| A card swapped while running is not re-mounted | `storage_mounted()` is cleared only by `storage_deinit()`, so a card removed after boot leaves a stale mount and RESCAN keeps reading the old volume. The viewer does not unmount to recover: it lists from the render task while the `runlog` task writes the run log to the same volume, and unmounting under an open handle frees the SPI bus beneath a write from another task | one task owning the card's lifetime, so a remount can be sequenced against the writer |
| The browse list shows 48 of a card's entries | a card takes 999 runs and the list holds `LOG_VIEWER_MAX_FILES`, so it keeps the newest runs -- by the number in `BENCHnnn.CSV`, the only age on a card whose every FAT timestamp is 1980-01-01 -- and its tab says how many entries the card holds. A run is the only entry with a known age, so on a card holding 48 or more runs nothing else is listed: an imported `SWEEP.CSV` is outranked by every run and cannot be reached | a second page, or a filter on the browse list |
| Betaflight logs are listed nowhere | the log viewer opens `.csv` only. Nothing in the tree decodes a Betaflight blackbox log, so `.bfl` is neither offered on the card nor named on the screen | a BFL decoder, and the suffix back in `CARD_SUFFIXES` and in the viewer's empty-card line |
| An unbound output slot is invisible from the panel | the coprocessor leaves a slot the silicon cannot serve unbound: a reserved pin, a pin the package does not have, a PWM (pulse-width modulation) compare register another bound pin already holds, or no free PIO (programmable input/output) block. The OUTPUTS page reads back what was asked for and carries no register saying whether a slot is bound, so the outputs screen draws an unbound slot exactly as it draws a driving one and the refusal reaches the operator as a lead that does not move | a bound bit per slot on the OUTPUTS page, and the outputs screen drawing the difference |
| Coprocessor board file | the build uses `pimoroni_pico_plus2_rp2350` (RP2350B, 16 MB flash); the bring-up module is a Waveshare RP2350-CAN (RP2350A, 4 MB flash) | a board header for the module, or a `-DPICO_BOARD` in CI. The IO board carries two RP2354B (the RP2350B die with 2 MB of flash in the package), a main and a measurement coprocessor (owner, 2026-09-30); the draft pin map uses 44 and 32 of their 48 GPIO (general-purpose input/output) each ([Pins](hardware/docs/Pins.md)). The tree builds one coprocessor image; the measurement coprocessor's image is not written. With 2 MB, `firmware/iomcu/partitions.json` needs a 2 MB layout: two slots of about 1012 KiB, the store and the unused last 8 KiB; the image guard in `firmware/iomcu/CMakeLists.txt` takes the slot size from that file: [IO (input/output) board research](hardware/docs/Research.md#consequence-of-the-rp2354bs-2-mb-flash) |
| Panel TWAI pins | GPIO19 (RX) and GPIO20 (TX) inferred from the multiplexer | trace on the schematic |
| UART (universal asynchronous receiver-transmitter) socket GPIOs | UART0's default pins assumed for the bridged USB-C socket; the secondary USB-Serial-JTAG (the ESP32-S3's built-in USB (Universal Serial Bus) serial and debug bridge) console covers a mismatch | read off the schematic |
| S.BUS receiver | decoder built; the inverted 8E2 receiver at 100 kbaud is a PIO program that is not written | PIO program |
| Bus servos are not started | step 5 of the servo tester (#223): Feetech SCS/STS and Dynamixel 2.0 serial servos, S.BUS and S.BUS2 output, and JETI EX Bus channel output, each from its published specification. A "ditec" bus servo on the owner's list is unconfirmed, possibly Hitec; a Hitec bus is taken up only if one is specified. S.BUS2 telemetry slots and configuration over EX Bus stay not planned | the specifications, a servo of each, and a decision on the order |
| Measurement front end | round 1 of [the component research](hardware/docs/Research.md) ran from 2026-09-28 to 2026-09-30 and selected 49 parts. The motor monitor is the INA228 and the servo supply three TPS55285, which the owner keeps although round 1 refutes one at 4.0 A continuous from the pack floor (owner, 2026-09-30). The 20 output ports take a channel each of 7 INA3221. Round 2 selects the parts round 1 left open and is planned in the research page. No schematic exists. [The IO board specification](hardware/docs/IOBoard.md) is a draft | [hardware record](hardware/STATUS.md) |
| Monostable | not on any board | hardware |
| Rotary knob | the AS5600 reader, the mapping to the throttle and the horn and the settings are built and tested on the host; On the Waveshare ESP32-S3 Touch LCD 7 the CH422G expander answers at 0x20 to 0x27 and 0x30 to 0x3F, which includes the AS5600's 0x36, so the knob cannot be read on that bus. The I2C terminal's pin order is from the schematic's text, and the turn direction with DIR on GND is the datasheet's | an I2C bus scan with and without the sensor, then a turn on a bench screen |
| Touch ownership on the panel | the router gives each contact one owner from its press to its release, and a contact that leaves the screen body by the status band is released at its last point in the body; tested on the host through the tracker. That release, and the one the tracker makes for a contact more than 120 px from its previous report, is marked (`TOUCH_FLAG_NO_TAP`) and activates no control except STOP, DISARM, OUTPUT OFF and START TEST while a servo test runs. SETUP's keys, PROGRAMMER's controls and LOGS's buttons and rows act on a tap: a release by the contact that pressed, inside the control, no more than 8 px from the press in x and in y; a swipe matrix of 3528 swipes down SETUP's key columns changes no setting (`test_settings`). The track id the GT911 gives a lone finger on successive touches is not measured: the host cases assume the same id each time, which is the condition under which a latched drag takes a later contact for its own. Not measured either: the GT911's report interval on the panel, and how often it leaves a moving contact out of a report | a log of the id of 20 successive lone touches, then the sequences of the ownership cases in `test/host/test_nav.c` replayed by a touch injector under a camera; on SETUP INTERFACES, swipes down the `-` column at three speeds with every value read before and after, a tap and a 1 s hold |
| The automatic servo test's threshold | 0.13.0 ran on the bench with the PD mini, from a tester, on three servos: the MS24 (moving 0.16 to 0.18 A) passed; the MG90S (holding 0.001 A, moving 0.04 to 0.077 A) and the 1102HB (holding 0.015 to 0.029 A, peaking at 0.039 to 0.044 A) moved but read FAIL, every move late, because movement was a reading 0.10 A from the level before the command. The threshold is per step, max(0.020 A, 3 x the idle noise), and a move with no movement is unseen, not late. Replays of the MG90S's and the 1102HB's CSVs in the host suite (`test/host/fixtures/`) give PASS, 41 and 41 moves timed at 771 to 989 ms, and NOT MEASURABLE, 21 of 46 moves timed (those to the low end) and none late. A move has 3000 ms plus the meter's lag to arrive: 3300 ms on the PD mini. With SETTLE at 500 ms the IDLE readings can still carry the move to the centre, which raises the threshold (0.026 A on the 1102HB at 5.00 V). The PD mini's travel times are an upper bound; the command's delay from the panel to the pin is inside every one and is not measured | a bench run on this build with the MG90S and the 1102HB, and a faster current sensor (INA219, INA3221 or INA228, planned) for travel times that TRAVEL TIME can check |
| The PD mini | the driver, the coprocessor's SUPPLY page and PIO UART, and the panel's half of the page are tested on the host against a modelled module. The protocol is the vendor's sheet and the bench station's notes (#223); its timings are the vendor's Python driver's, not measured. Not measured either: whether a live module goes to ERR when its input sags under the set point, which the switch-off on 2 low input reads assumes; and whether its button or AUTO OUT switch it on while the UART is attached, which the state read before a wiring change assumes | a bench run against the module: identify, set points read back, ON and OFF, a heartbeat stop, a pulled cable, the input lowered under a live set point in 0.5 V steps |
| Coprocessor core 0's stack | `stack_check.py --iomcu` measures 1636 bytes for the deepest chain and 528 for one interrupt on the 4096-byte stack, 1676 bytes above the 256-byte margin. The figure is a lower bound: 91 calls through a register are not followed. The stack has no guard (`PICO_USE_STACK_GUARDS` is off). Not measured on hardware. An image built with another option set is measured only when CI builds it | a high-water mark read on a board; a decision on stack guards |
| OpenYGE wire facts | seven items want a capture: rpm scale, CRC (cyclic redundancy check) seed, frame length, legacy header, turnaround, parameter indices, `status2` | an ESC and a logic analyser; [list](docs/OpenYGE.md#8-what-to-measure-before-trusting-this-page) |
| The bench in a browser | serving the interface to a browser on another machine is open; a browser on the panel is not planned. There is no network stack in the tree: no Wi-Fi bring-up, no sockets, no HTTP (Hypertext Transfer Protocol), and Wi-Fi costs internal RAM and CPU time on a board whose frame budget is spent. The safety line is a heartbeat, and a remote client cannot hold one: a browser that stops answering is indistinguishable from one whose user is idle | a read-only client (numbers, plots and logs out; ARM, throttle and STOP stay at the panel), and before any code, a written answer to how a remote session proves it is still present |
| No ESC reports its kV | the ESC screen shows the rated kV, what the motor turns per volt, and the ratio of the two as EFF, an estimate documented as such in [Screens](docs/Screens.md). The rated value is read from the connected ESC when it reports one and from `SET_MOTOR_KV` when it does not; nothing calls `motor_screen_set_esc_kv()` yet, so it is whatever the operator entered, and zero draws the field empty | an ESC parameter set on the link. The OpenYGE cache is built and unconnected; BLHeli_32's parameters are not published |
| The heartbeat has no hardware backstop | the wire from J8's GPIO6 to the coprocessor's GP3 is fitted on the bring-up bench. What is not fitted is the retriggerable monostable the wire is supposed to pass through, so firmware at both ends is the only thing gating the outputs. The wire covers a panel that stops beating while the coprocessor is healthy: the monitor sees no edge for HEARTBEAT_MAX_GAP_MS (150 ms) and the loop switches to SAFE. Uncovered is a panel that stops beating while the coprocessor cannot act -- nothing then removes the outputs. That is what the monostable does, retriggered by the panel's edges and needing no firmware. A healthy panel beside a misbehaving coprocessor is covered by neither, and by no hardware in this design | the monostable specified in [Safety](docs/Safety.md), on a board. `testbench/WIRING.md` carries the specification and deliberately no part numbers |
| The control task has no test of its own | touch, STOP, the ARMED state, the outputs, the link and the heartbeat run in a task on the core that does not draw. Nothing exercises it deliberately: `main.c` is not in the host suite. The `runlog` task beside it, which owns every write to the card, is in the same position. A multi-agent review found six defects in it, including a heartbeat that stopped for up to 1000 ms on an unanswered poll and a splash tap that latched STOP; those are fixed, and the rules it drives are in `shared/safety/arming.c` under `test_arming`, the link-loss stop and the wait for the far end's word on the heartbeat included | a session with both boards: press ARM, drag the throttle while the screen is busy, press STOP, unplug the link, and confirm the heartbeat's period on a scope at J8. ESP-IDF warns that a second core touching PSRAM shares bandwidth with the bounce-buffer refill. A refill that finishes late shows stale lines in that frame; a refill interrupt held off for more than 512 us restarts the scan cleanly (`rgb_panel_rcbench.h`) at the first VBlank (vertical blanking interval) interrupt after it runs again, which can be mid-frame. The control task touches no framebuffer, which is the reason to expect it is clear, not evidence that it is |
| Settings save disturbs the picture | `settings_save()` writes NVS while the panel scans. The refill interrupt is masked for the length of the write, so the bounce buffer starves. A write that holds it off for more than 512 us makes the driver restart the scan with the clean start of boot (`rgb_panel_rcbench.h`) at the first VBlank interrupt after the write ends, which can be mid-frame, so the picture is in place from the first whole frame after; the console logs `display: scan restarted` each time. How long a save holds the interrupt off is not measured | a photo of the home screen after a settings save and after a board drawing's keep, with the console's restart lines beside it. Beyond that nothing, unless the disturbance proves unacceptable. `CONFIG_SPI_FLASH_AUTO_SUSPEND` would remove it (the module's flash is 0x46 4018, an XMC die ESP-IDF grants `SPI_FLASH_CHIP_CAP_SUSPEND`), but ESP-IDF warns against it for a workload with an interrupt every 512 us |
| Stick programming's beep timing | the engine, the PROGRAMMER tab and the simulated ESC are tested on the host only. No ESC's menu has been recorded, so the beep and gap lengths, the long beep, the gap between groups, the idle current and the current a beep adds are defaults chosen to be plausible, and the simulated ESC sounds numbers made up to match them. The PD mini is read 100 to 150 ms apart at the panel, and whether its current is an instant reading or an average is not known: a beep shorter than about 200 ms may not be seen. The ESC's tones after a selection are not decoded, so DONE does not say the ESC stored anything | one ESC of a profile the engine runs, on the PD mini with a resistor load, and once with a motor mounted solid without propeller: a recording of its menu's current at the fastest rate available, the module's read interval, and one run of each kind (two-stage, one-stage) checked afterwards with the ESC's program card |
| The German interface | the tables, the fonts and the fit check are built and tested on the host, and the 207 German screenshots are rendered by the panel's code; An alert already on the band, and the title of a keypad or choice already open, keep the language they were raised in until replaced. | a beta tester's pass over every screen in German on a panel |

## Constraints

- The XL2515 module's crystal is 16 MHz; `test_can_timing` pins it. An 8 MHz
  part caps the bus at 500 kbit/s.
- No 5 V transceiver is in the path: the SIT65HVD230 is a 3.3 V part. A 5 V
  transceiver on RP2350 bank 0 needs a 2.2 kΩ series resistor on RO, because
  the 3.3 V rail comes up after the 5 V rail on a module build.
- Native USB and CAN are exclusive on GPIO19/20. The console is UART0 with
  USB-Serial-JTAG as secondary.
- The motor monitor is the INA228 (owner, 2026-09-30). Its stock is not read
  after 2026-09-01, when JLCPCB held 29 with a presale count of -477 and
  Digi-Key 0. The INA238 takes the same footprint, and DEVICE_ID (register
  0x3F: 0x2281 or 0x2381) says which is fitted.
- The monostable window is 150 ms.
- A main-flash operation stops the panel scanning for its duration. The bounce
  buffer holds 10 lines (512 us at the 16 MHz pixel clock) and is refilled
  from PSRAM in an interrupt handler that reads through the data cache, which
  every main-flash read, write and erase closes. The handler is not IRAM-safe,
  so ESP-IDF masks it and the buffer starves rather than the core panicking
  with `Cache disabled but cached memory region accessed`. Settings still load
  before `display_init()`. A frame whose refill interrupt was held off for
  more than 512 us restarts the scan at the first VBlank interrupt after the
  operation ends, which can be mid-frame.
- The panel links its own copy of ESP-IDF's RGB driver. ESP-IDF v5.4 and
  v5.5 restart the ESP32-S3's scan by emptying the LCD (liquid crystal
  display) FIFO (first-in, first-out buffer) and resuming from a link that
  skips `LCD_LL_FIFO_DEPTH + 1` = 17 px, which the FIFO no longer holds: the
  picture sits 17 px to the left with each line's tail wrapped one line
  down, until the panel restarts. `tools/patch_rgb_driver.py` writes the
  copy at configure time with every restart made the clean start of boot, so
  the first whole frame after a restart is in place. A driver the tool
  cannot patch stops the build.
- `CONFIG_LCD_RGB_RESTART_IN_VSYNC` stays off. It restarts the scan on every
  vertical blanking interval, which with the clean start stops and restarts
  the LCD every frame; the patched copy refuses to build with it on.
- The link is CAN only. There is no RS485 transceiver, direction circuit or
  turnaround in the design.
- Every stop latches; nothing switches back to ARMED on its own. On the coprocessor a start,
  200 ms of link silence, a distrusted heartbeat and a refused ARM set an arm
  latch that only a CLEAR releases. On the panel a STOP, an ARM = 1 sent while ARMED that
  the coprocessor refuses, and a link that goes quiet while
  ARMED or during an arm latch a stop that only a switch to ARMED clears. Two things
  switch the panel to SAFE without that latch: 500 ms without touch, after which the
  heartbeat resumes when touch answers again, and a switch to ARMED the coprocessor
  refuses at the hold, after which the heartbeat keeps running. Neither switches the bench to ARMED;
  the next switch to ARMED is a hold.

## Not planned

- **Configuration over JETI EX Bus.** The specification restricts remote
  configuration to JETI products and does not document it.
- **Parameters of ESCs with BLHeli_32 firmware.** The information is not published; the rights holder
  declined in August 2026. [Details](docs/BLHeli32.md).
- **Futaba S.BUS2 telemetry slots.** Answering in the 325 µs slot without
  colliding with the receiver's own sensors is out of scope.
- **A USB oscilloscope.** The ESP32-S3's USB is full speed only.
- **A web browser on the panel.** The leanest headless Chromium build is
  over 100 MB against a 3,968 KiB app slot; one blank tab needs more memory
  than the board's PSRAM, of which 1.5 MB is the two framebuffers; and the
  multi-process sandbox and the JIT (just-in-time compiler) need processes,
  an MMU (memory management unit) and a POSIX layer, none of which exist
  here. A cut-down HTML renderer would compete with the screens for the
  976 KiB per frame of PSRAM traffic in [the budget](docs/Performance.md).
  The other direction, a browser elsewhere reading this interface, is in the
  [open items](#open-items).
- **A ramp on the physical throttle.** `Ramp limit` (5 to 300 %/s, default
  55) sets the slew on the panel's own throttle bank, which is the modelled
  bench: `telemetry_sim_step()` reads it and nothing else does, and only
  while the link is down. A coprocessor that is answering is sent the raw
  command on the CONTROL page, `outbind_to_chan_cfg()` writes no
  `LINK_CC_SLEW` for any channel, and a pin bound as a throttle steps to what
  the slider asked for on the next 1 ms pass. The bank ramps a throttle
  upward only -- a command below the actual value is applied on the pass it
  arrives, and STOP and a switch to SAFE bypass the slew -- so a ramp on the wire
  would slow the rise and nothing else. The slider is delta-only, the preset
  row is not drawn and a nudge is 1 % per press, but a drag applies the
  finger's travel across the track to the whole span and the command slot
  keeps only the latest position, so a swipe across the track inside one
  50 ms poll is a 0 to 100 % step at the pin. What bounds the rise is finger
  travel between two 50 ms writes, and an ESC's own ramp underneath it.
  Decided 2026-09-10.

Every protocol is implemented from its published specification or, where
none is published, from captures of the wire on hardware the project owns
(the programming protocol of KST and Chaservo servos). Nearly every
open implementation of these protocols is GPL (GNU General Public License) or
AGPL (GNU Affero General Public License) against this repository's MIT
(Massachusetts Institute of Technology) licence; the permissive exceptions
(PX4's receiver decoders under BSD (Berkeley Software Distribution), MIT
reference code for SRXL2, JETI EX Bus, DShot and DroneCAN) are read for
confirmation only. The source material of the OpenYGE protocol was supplied as GPL code and is
kept outside the repository; the specification in
[docs/OpenYGE.md](docs/OpenYGE.md) is what any implementation is written from.

## Order of work

| Step | State |
| --- | --- |
| 1. Foundation: tree, ported modules with tests, tools and CI, link codec, heartbeat, coprocessor skeleton, screens | done |
| 2. The CAN link | done 2026-08-28 |
| 3. Measured numbers: ESC telemetry or bidirectional DShot, and the current sensor | extended DShot telemetry fills `bench_state` with the ESC's own voltage, current, power, speed and temperature; on one AM32 2.21 ESC on a tester's bench the voltage read correct and the current read 72 A at idle, which looks like an ESC with no current sensor (#172). The bench's own current sensor is selected and not fitted. OpenYGE is pursued in a separate repository |
| 4. Recording: the logger, and the viewer reads what the bench wrote | done |
| 5. Outputs: the control page from the panel, then PWM, then the sum-signal protocols | drivers built and host-tested; no timing seen on an instrument. SETTINGS/OUTPUTS configures a slot |
| 6. Receiver buses, one decoder at a time | S.BUS decoded; the PIO receiver and the other buses open |
| 7. Programming: ESCs with BLHeli_S and AM32 firmware, Hitec D-series servos | screen built; no protocol on a wire |

Next: an instrument on a pin. Every bit timing in this tree was written from a
specification and exercised only against frames the same code builds, and a
servo turning is not a frame period. `testbench/` is what answers that -- the
bench is being assembled. After that, the
monostable, which is the one safety element specified and on no board.
