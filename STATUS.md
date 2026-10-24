# Where rcbench stands

The running record: what exists, what is open, what is settled. The suite
list, the coverage table, the compile table and every link are held to the
tree by `tools/check_docs.py` and `tools/coverage.py`. The directory sketch
below is not: nothing checks it, so read it as a map and not as an inventory.

[README.md](README.md) is the front page; the
[wiki](https://github.com/subtilitas/rcbench/wiki) is the manual.

## Architecture

Two processors. The IOmcu (RP2350) owns everything with a deadline; the
panel (ESP32-P4 on the Waveshare ESP32-P4 7B) owns everything else: a
1024x600 MIPI DSI (Mobile Industry Processor Interface, Display Serial
Interface) display with an EK79007 controller, GT911 touch, the SD card on
SDMMC (the SD/MMC host) with 4 data lines, Wi-Fi through the board's ESP32-C6
over SDIO (Secure Digital input/output), CAN on TWAI (Two-Wire Automotive
Interface) GPIO22 (TX) and GPIO21 (RX) and the heartbeat on IO28. The
ESP32-S3 panel's last release line is 0.22 ([README](README.md)).

Nothing raw crosses the link: bit timing, GCR (group-coded recording)
decoding, receiver framing and pulse capture stay on the IOmcu, and only
results travel.

| | Panel | IOmcu |
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
| CAN | TWAI, the ESP32-P4's CAN controller | XL2515 over SPI (Serial Peripheral Interface) |

**Link.** Classic CAN at 1Mbit/s. A 29-bit identifier carries priority, op,
page, offset and count; a frame carries up to four registers; the transport
does no reassembly; the IOmcu transmits only when asked. Worst-case
payload 52kB/s against 12 to 30kB/s of expected traffic. Protocol version
4.28. [Reference](docs/Link.md).

**Safety.** The panel's control task drives GPIO28 (`PANEL_HEARTBEAT_PIN`, IO28 of header P3) from the core that does
not draw. The task runs every 5ms; the line edges every 20ms
(`HEARTBEAT_PERIOD_MS`). The IOmcu's
outputs are to be gated by a retriggerable monostable with a window of about
150ms, and the line is checked in firmware (4 to 150ms between edges, four
good intervals before it is trusted). The IOmcu fails safe after 200ms
of link silence; the panel escalates after 1s. The IOmcu starts with an
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
pin is in the slot, not in the firmware: the IOmcu refuses the pins it
must not drive and binds the rest as they arrive. A bank in ARMED drives every
bound pin whether or not anything commands it: a channel nobody has commanded
for 500ms is rendered at its role's rest, which for a surface is the midpoint
of that channel's own endpoints -- 1500us across the default 1000 to 2000us,
and 760us across the 660 to 860us of a narrow servo. The timeout moves the
channel to that rest and leaves the pin driving; switching to SAFE is what stops it.
[Reference](docs/DShot.md).

**Measured or modelled.** An IOmcu that answers reports only what it
measures, with a valid bit per quantity and no `SIMULATED` flag. The panel
models the whole bench when nothing answers, and marks that with the
watermark. The two never appear on one screen.

**More than one IOmcu.** The IOmcu reports which board it is in
the identity page's hardware register, and the panel offers that board's pins
and no other. A board this build does not know offers nothing and reserves
every pin: guessing a pin map is how an output reaches the safety line.
Changing board clears any selection, because a pin index belongs to one
board's catalogue. A board whose outputs are soldered is shown rather than
offered.

**The output binding lives on the IOmcu.** A binding describes wiring,
and the panel is not the board the wires are in. The IOmcu keeps the
`OUTPUTS` and `CHAN_CFG` pages in its own flash and restores them at boot; the
panel stores none of it and reads the page back when the link comes up.
Restoring configures the outputs and does not drive them -- every driver is
gated on the bank being ARMED, which the IOmcu grants only while the ARM
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
| Rasteriser, fonts, touch mapping, theme, widgets, icons, router | built and tested; text is UTF-8, one cell a code point, and the five text faces carry °, Ä Ö Ü ß ä ö ü and Ω |
| Interface language | built and tested on the host: English and German, chosen by SETUP's Language and applied on the next frame without a restart. Every translated string is an entry in `shared/text/include/ui_text.def` with its English and its German side by side, and a German entry of `NULL` shows the English. The servo test's TXT report is in the language its run started in; its CSV stays English. `render_ui.py --fit` draws every view in both languages and fails on a German string that overflows; `test_text` holds the rest. [Reference](docs/Language.md) |
| Settings model and screen | built and tested on the host. The panel loads and saves the values in NVS (non-volatile storage) through `firmware/panel/components/settings_nvs/`. The home tag that leaves SETUP with values unsaved opens a warning listing each change against the stored copy, with SAVE, DISCARD (refused while ARMED) and STAY; no route the panel takes by itself is held (`test_settings`). A tap on a number's value opens the keypad, on SETUP's lists, ESC STICK's TIMING, BALANCE's blade count and SERVO's TRIM (signed, with a `+/-` key) as on SUPPLY, SERVO, MOTOR & ESC and the servo programmer; its OK sets the value through `settings_set()`, the call the `-` and `+` keys end in, and refuses a value outside the range, naming the range, or one the keys never reach, naming the reason in place of the range until the next key (`shared/ui/ui_setting_keypad.c`, `test_settings_keypad`, `test_esc_stick_panel_keypad`, `test_servo_trim_keypad`, `test_keypad_sign`, `test_balance`; `test_setting_keypad_eq` types every one of the 149,555 values `-` and `+` reach and finds each stored bit for bit as stepped) |
| CSV (comma-separated values) and number parsing, log viewer | built and tested against the fixture corpus; a report standing alone, `LIMITnnn.TXT` or the `SERVOnnn.TXT` alone of 0.21.0 and older, is listed by its number and opens as text (`shared/ui/log_select.c`, `shared/ui/log_text.c`, `test_logview_report`, `test_log_text`); the panel's two walks of a folder for it (`panel_card.c`) are not host-tested |
| Logger | built: a run is written while ARMED, or while the supply's output is on with the bench SAFE, in the format the viewer reads. The log of a bench in ARMED has one row per sample the control task takes -- an answered BENCH read with the link up, a 50ms model step with it down -- and its time column is the wall time since the switch to ARMED; a run armed from SERVO takes `voltage`, `current`, `power`, `charge` and `energy` from the servo rail's meter -- the INA3221 channel's newest window while it is the meter, the supply's last reading otherwise -- and the panel's count of it since the arm, and writes `pulse (us)` and `angle (deg)` before `source`: the pulse COMMANDED shows and the AS5600 angle MEASURED shows, handed from the render side in one atomic word; a figure whose source is not the row's is an empty cell (`log_writer_servo_row()`, `log_rail_take()`, `log_rail_take_ina()`, `log_rail_count()`, `log_servo_pack()` in `shared/bench/log_writer.c`, `servo_screen_log_pulse()`, `servo_screen_log_angle()`; host-tested in `test_servo_runlog`, `test_servo_runlog_cols` and `test_servo_log_figures`; the panel's calls in `panel_poll.c` and `panel_frame.c` are not host-tested); LOGS reads up to 25 columns and opens a SERVO file with `pulse`, `angle`, `current` and `voltage` chosen (`log_fields_picks()`, `test_servo_runlog_cols`); a poll that brings more than one INA3221 window writes one row for each, the sample in the last; the rule is `shared/bench/log_cadence.c`, host-tested in `test_log_cadence` against a model of the control task's pass grid and in `test_logwriter` against the modelled INA3221 through the IOmcu's pages. A file goes to one of two folders and is named by what started it: `BENCH/LOGS` holds `SERVOnnn` for an arm from SERVO, `MOTORnnn` for an arm from MOTOR & ESC or ESC STICK and `SUPPLnnn` for a supply run; `USER/LOGS` holds `ATESTnnn` for the automatic test and `LIMITnnn` for the limit run; the `BENCHnnn`, `ESCnnn` and `SERVOnnn` runs of firmware 0.21.0 and older stay in the root and stay readable. Each folder is made one level at a time on its first write, and its numbers are one series across every name in it, read by one walk and probed name by name before each take (`shared/logfile/log_name.c`, `shared/logfile/log_take.c`, host-tested in `test_log_name` and `test_log_take`). SETUP > SD CARD switches each folder's writing off (Auto logs, User logs; `test_log_cadence`, `test_servo_test_panel`, `test_servo_limit_panel`) and sets each folder (Auto log folder, User log folder: 1 or 2 folders of up to 8 characters, `card_folder_check()` in `shared/logfile/card_tree.c`; `test_card_tree`, `test_keypad`, `test_settings_folders`); a changed folder takes the next file and numbers from what it holds (`log_series_follow()`, `test_log_take`), and LOGS lists the folders set (`test_logview_tabs`). The logger's copy of the folders and the open markers' folders, under a spinlock in `panel_log.c`, are not host-tested. The bench's own files are under `BENCH` too: the fault log `BENCH/RCBENCH.LOG`, written whatever SETUP > SD CARD says and held to 1MiB with the full file kept as `BENCH/RCBENCH.OLD` (`shared/logfile/fault_log.c`, `test_fault_log`), the ESC profiles read from `BENCH/ESC/CONF` and the servo files and the write record in `BENCH/KST/CONF`; each folder is made one level at a time (`shared/logfile/card_tree.c`, `test_card_tree`). The folders `ESC` and `KST` and the root's `RCBENCH.LOG` of 0.21.0 and older are not read. An automatic servo test's CSV and report take the next number of `USER/LOGS`, and the bench log writes no row while the test runs (`log_cadence_bench_run()`): its time column steps over the test. The card is written by the `runlog` task, not by the control task that beats the safety line, and the file is committed every 20 rows or 1000ms of run, so a power cut mid-run costs that much of it plus whatever the queue to that task holds -- under 1.0s while the card keeps up, 84 rows and 4.20s at 20Hz with the queue full. A row the cut stops part way through has no line feed; the reader leaves it out and reports it (`log_analysis_t.cut_row`), host-tested at every cut position of a row in `test_logwriter` |
| File manager: FILES | built and tested on the host (`shared/ui/files_screen.c`, `files_draw.c`, `files_events.c`, `shared/files/fm_req.c`; `test_fm_req`, `test_files_screen`, `test_files_ops`, `test_files_layout`): two panes over the SD card, COPY, MOVE, RENAME, DELETE (a run's CSV with its report), NEW folder and empty text file, INFO, paging of 48 entries, entries not listed counted; writes only while SAFE with no log file open (`fm_gate()`), the bench's folders, the servo programmer's record and the files the logger writes refused by `fm_rules.c`; a copy stops before its next 16KiB chunk on CANCEL, ARMED or a run that starts logging. Opens from LOGS and from SETUP; OPEN hands a CSV file or a report in LOGS' three folders to LOGS. The requests run on the panel's task `cardjob` (`firmware/panel/main/panel_files.c`), not host-tested; after each job the logger forgets its folders' numbering (`log_series_forget_all()` in `shared/logfile/log_take.c`, `test_log_take`), so its next take walks the folder; not run on a panel: the copy rate (logged on the console after each copy), the stack left on `cardjob` and the times of files the panel writes are not measured. The text viewer and editor of the file manager are not built. [Files](docs/Screens.md#files) |
| Board, display, GT911, SD card | built; the panel boots and reports each step on the splash. SETUP > PANEL's Screen rotation 180° turns the picture in the flush: the screens draw into one of two logical buffers (2 x 1,228,800 bytes of PSRAM), and each flip copies what the frame drew, at most 8 rectangles of 16-row bands, into the frame buffer turned, through the ESP32-P4's PPA (pixel-processing accelerator), with the DIG-734 workaround on for every copy (`firmware/panel/ppa_dig734.cmake`). The glass's touch map turns by the flag the flip sets once the turned frame is on the glass, x' = 1023 - x, y' = 599 - y; the touch injector's contacts keep logical pixels. A flip whose copies take longer than 50ms or fail sends its frame unturned, and picture and touch stay NORMAL until the next start, with an alert on the band. The row is held while ARMED. The EK79007's scan direction (DCS command 0x36) is held by its PINCTL strap on this board and turns nothing (`test_gfx_damage`, `test_gfx_turn`, `test_touch_map`, `test_settings`, `test_settings_screen`) |
| Panel firmware update and card files over Wi-Fi | built and host-tested (`shared/fwupdate/`, `shared/webaccess/`, `test_wifi_screen`, `test_wifi_layout`); the panel side is `firmware/panel/components/wifi_access/` and `fw_ota/`. A panel install and the IOmcu's update exclude each other (`fw_gate()`, `fw_coproc_may_start()`, `test_fw_install`): the install waits while the IOmcu's update or BUY runs, and the IOmcu's question opens and its START is taken only with the radio wholly off and no install under way. On the ESP32-P4 the radio is the board's ESP32-C6 on SDIO through `esp_wifi_remote` and `esp_hosted` (`wifi_radio.c`, [Wi-Fi through the ESP32-C6](docs/Building.md#wi-fi-through-the-esp32-c6)). Measured on a home network on 2026-10-23, panel build 408c43bd: a download of a 16MiB file at 688-757kB/s, the card read and the radio together, not separated; an upload of a 2.56MB image to `/update` at 556-599kB/s; a ping average of 14.6ms; no SDIO error and 60fps during the downloads. The page, 18.9kB with its card listing, took 4.54s to arrive, about 4.2kB/s: a current limitation whose cause is not measured. Not measured: the range, the time of an erase and a write, what the glass shows during an erase, the free internal heap with the radio and the display up, a scan and the C6's firmware version. [Wi-Fi](docs/Screens.md#wi-fi), [migration](docs/FirstRun.md#1-flash-both-boards) |
| Release check, reminder and download from GitHub | built and host-tested: the strict reading of the release tag, the redirects and `SHA256SUMS` (`shared/fwupdate/fw_release.c`, `test_fw_release`), the scan of GitHub's API answer for the highest of its 10 newest releases and the rule that offers only a newer release (`test_fw_release_scan`), the check and the download with their budgets, aborts and a scripted HTTP client (`shared/webaccess/wa_fetch.c`, `test_wa_fetch`), RELEASES' two channels and GitHub's request limit (`test_wa_fetch_channel`), a job's task that outlives the radio in a name lookup (`test_wa_fetch_task`), and UPDATES, the reminder and the download's screen (`shared/ui/upd_screen.c`, `test_upd_screen`). The panel side is `wifi_access_fetch_*()` over esp_http_client in `firmware/panel/components/wifi_access/`. Not run on a panel or against GitHub: the API request of RELEASES on TESTING, the TLS (transport layer security) handshakes, the durations of the check and the download, and the free internal heap while a TLS connection runs are not measured. [Updates](docs/Screens.md#updates) |
| Wall clock and time zone | built and host-tested (`shared/clock/wall_clock.c`, `test_wall_clock`): the panel board has no RTC (real-time clock) chip, so the clock starts unset at each power-on and a software restart keeps it. Each join of the home network asks `pool.ntp.org` once, for at most 30000ms (`firmware/panel/components/wifi_access/wifi_ntp.c`); a join that ends for OFF, its release check's own end or the idle time-out while the request asks keeps the radio up for it, 30000ms from the request at most, and every other end takes the radio down at once (`wa_lingers()`, `wall_ntp_holds()`). FAT stamps are local time at the write and do not move with a later zone change. SETUP > PANEL's Time zone (10 zones, BERLIN by default) applies to FatFs's stamps, FILES INFO, the Wi-Fi page and the ZIP file; a 1980 stamp reads `not known` on INFO and empty on the page ([Building](docs/Building.md#the-panels-clock)) |
| Shell: band, router, splash, menu, simulation watermark | built; the router repaints the band in a framebuffer only when what it shows changes there, and every frame over a simulated bench (`ui_band_key()`, `test_frame_work`) |
| Motor & ESC (electronic speed controller) screen | built; reads `bench_state` from the link or the simulator. ARM, DISARM, STOP and the throttle are written to the IOmcu's control page at every 50ms poll while the link is up; switching to ARMED writes CLEAR on its own, then ARM, THROTTLE and MOTOR_POLES in one frame, and a NACK to either leaves the panel SAFE. The throttle changes only while ARMED: while SAFE, the slider, its `-1` and `+1` steps and the rotary knob are refused and the value is 0%, which every switch to SAFE returns it to; both bench screens read the value's state (OFF, ARMING, LIVE, LEAVING) from `ui_value_gate.h` (`test_motor_arm`, `test_value_state`; host suite only) |
| Servo screen | built; writes `CHAN_CFG` and `CHANNELS` over the link, the frame rate on `SERVO` to an IOmcu speaking protocol 4.1, and a sweep (square, sine or triangle, run by the IOmcu) to one speaking 4.2, which PAUSE holds (the button then reads PAUSED) and a tap on PAUSED carries on from the phase it was held at on an IOmcu speaking 4.6 (`LINK_SV_RESUME`), and starts over from the curve's beginning, with an alert, on an older one or one that refuses; SPEED's row reads SPEED LIMITS THE SWEEP while SPEED is slower than the curve's fastest change. The screen is a workbench (`ui_workbench.h`, `servo_workbench.c`): a strip with LIVE, TEST, PROGRAM and SETTINGS, a tile opening LIVE, each task keeping its page for the session, PROGRAM listing the programmers from one table (`prog_registry.h`: a row each, dimmed with its reason when the IOmcu does not report its capability, two rows on one port exclusive, SEARCH over 9 rows and headings over 8 shown; `test_prog_registry`) whose KST and Chaservo row opens that programmer one level down, and an output card on every task whose TARGET row (`ui_target_row.h`) names the target as `protocol pins` and opens the choice OUTPUTS / PROG PORT, locked while ARMED, during a sweep or a run, and while the programmer holds the port (`test_workbench`, `test_servo_workbench`). SETTINGS sets the type (STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760), the frame rate, the pulse widths, trim, travel and reverse for the session on its OUTPUT page, and SWEEP's curve and the device under test in NVS; each run's settings open from its card on TEST, and SUPPLY's caps show there read-only; a heli type or a rate above 60Hz is applied only through a warning held for 2s, and every restart is STANDARD PWM at 50Hz. The right card shows the servo rail as its meter reads it (`servo_source.h`), in the CURRENT row, on the line over the plot and in the plot: CH1's 50ms windows under the name `INA3221 CH1` while the INA3221 is the meter, the supply's samples under `PD MINI` otherwise -- the PD mini's when SETUP enables it, `supply_sim_t`'s with a SIMULATED pill if not. A current is signed and shown in hundredths of an amp, a window with a sample at an end of the range in the warning colour, a reading that did not arrive as dashes; the row's value is repainted by itself when its digits or colour change (`test_servo_meter`; host suite only). START TEST on its card on TEST runs the automatic test (`shared/servo/servo_test.c`): the supply stepped through 4.8 and 6.0V, and 7.4 and 8.4V with HV SERVO on (session only, through the HV warning's 2s hold), idle, moving and holding current and the travel time at each, a brown-out walk from 5.0V down in 0.2V steps, and `ATESTnnn.CSV` and `ATESTnnn.TXT` written to `USER/LOGS` by the `runlog` task. A run reads one meter from its start to its end, the servo rail's at START TEST: the PD mini's readings, or, while the INA3221 is the meter and the supply is the PD mini, CH1's 50ms windows -- mean current, the highest and lowest 1ms sample, the bus voltage at the load side of the shunt and its lowest sample -- with the supply read for its state only (`test_servo_test_win` and `test_servo_test_win_read` with windows given by hand, `test_servo_test_win_chain` through the modelled INA3221, the IOmcu's schedule and pages, `sense_link` and `servo_source`). A run on the INA3221 ends ABORTED when the INA3221 stops being the meter or no window arrives for 200ms (`SERVO_TEST_WIN_STALE_MS`, 199, 200 and 201ms and the millis wrap in `test_servo_test_win`), and never goes on with the PD mini. Currents are compared by magnitude in whole mA; the supply in constant current for 1000ms ends a run whatever STALL AT is, and a STALL AT at or above the current limit or the INA3221's range is said on the automatic test's card and in the report. Movement is a reading max(0.020A, 3 x the idle readings' standard deviation) from the level before the command; a move with none is unseen, neither timed nor late, and a run with unseen moves and nothing failed reads NOT MEASURABLE. On the PD mini, whose readings lag about 0.3s and repeat, and on the INA3221's windows, from which the panel times a move to 50ms, every travel time is an upper bound and TRAVEL TIME is reported, not checked. STOP, a switch to SAFE, link loss, the supply and the operator end a run with the output off and a report marked ABORTED. A position is taken only while ARMED: while SAFE, the dial, the rotary knob, CENTRE and SWEEP are refused and the value shown is the one last driven, and at every switch to ARMED the commanded value is the surface's rest, the midpoint of the channel's endpoints; RELEASE while ARMED sets it to that rest as well (`test_servo_arm`; host suite only). A drag on the dial goes through a one-pole filter of 20ms (`SV_DRAG_TAU_MS`), reckoned from each frame's length, which the horn and the pulse sent both follow; DISARM, STOP and SAFE end it at once and an ARM starts from the rest (`test_servo_drag`; host suite only). The horn is drawn in fractions of a pixel (`gfx_subpixel.h`, `test_gfx_subpixel`), and a horn that moves repaints the boxes around it and the numerals that follow it, not the two cards (`test_servo_repaint`: both buffers pixel for pixel the same as frames drawn whole). A repaint clipped to a box walks what lies in the box: the rasteriser skips a glyph, a digit or a stretch of arc outside the clip and walks the antialiased shapes row by row over their own pixels (`test_gfx_clip`). ARMED at rest draws what SAFE at rest draws plus the band's run clock once a second, a held position draws the grip's box, and a moving arm stays under 140,000 pixel visits a frame (`test_frame_work`; host suite only). |
| Supply screen | built and tested on the host; the control task drives the PD mini through the SUPPLY page (`shared/bench/supply_link.c`) when SETUP INTERFACES enables it, and runs `supply_sim_t` otherwise. Set points shown beside their readings, a keypad, a SETTINGS overlay (caps, start values, current and voltage trips, the confirmation for live changes) kept in NVS, and a question before a set point changes a live output. Every stop, a trip, a lost ON and a supply that stops answering switch the output off; the supply is stepped and logged from `control_pump()`, so it keeps its 50ms cadence while an exchange waits. A supply run is logged with its own columns |
| PD mini driver | the codec and driver (`protocols/pdmini/pdmini.c`, `pdmini_codec.c`, `pdmini_jobs.c`) are built and tested on the host against a modelled module; the IOmcu runs it on a PIO UART on the pins the SUPPLY page (protocol 4.3) names, and the panel writes and reads that page. The module's baud rate is 38.4 kbaud, which the automatic baud search finds. A set point above the module's input voltage sends the module into a fault that only a power cycle clears, so the panel and the driver cap the set point 0.7V under the input. The module is read every 102 to 106ms, a change of current shows in the readings 0.31s after the command (median), and a current often repeats over several readings. The wiring is kept in the IOmcu's flash with the output bindings (record version 4; version 3 records still load) and driven at boot with the output off; the command is not kept. A wiring change while a module has answered waits for a state read sent after it and is taken only if that read shows the output off (protocol 4.5). A live output whose input reads under the set point plus 0.5V on 2 input reads in a row is switched off and the panel names the input and the set point; the 0.2V between the cap and that line covers 3000mA through 1m of 20 AWG cable out and back, and the rule is chosen, not measured. The PIO receive FIFO holds 8 bytes, 4.2ms of reply at 19200 baud; the main loop's pass time is not measured |
| Receiver, balance, battery screens; the modelled programming tables | screens built, rendered from models. The parameter tables for BLHeli_S, AM32, ESCape32, VESC and Hitec D-series are data with no transport and no row on a PROGRAM list (`shared/ui/prog_models.c`, `test_prog_models`). ESC STICK on MOTOR & ESC > PROGRAM goes out through the arming policy and the output bank (`test_motor_program`) |
| ESC programmer protocols | the screen is table-driven for ESCs with BLHeli_S, AM32, ESCape32 and VESC firmware; no protocol is transmitted. ESCs with BLHeli_32 firmware are not supported: [details](docs/BLHeli32.md) |
| Balancing measurement | the screen and the placement guides are built; the accelerometer and the index sensor are not fitted, so nothing is measured |
| Link codec, page map, dispatcher, both watchdogs, CAN framing | built and tested on the host |
| CAN drivers (TWAI on the panel, XL2515 on the IOmcu) and the echo self-test | built |
| Bring-up diagnosis, both ends' counters compared | built |
| Heartbeat generator and monitor | built and driven at both ends; the monostable is not fitted |
| IOmcu firmware | answers the identity, status, control, bench and three output pages; fails safe at 200ms. The decision to switch to ARMED is `shared/safety/safety_gate.c`, called once a pass and at every CONTROL write and host-tested in `test_safety_gate` against a model of both main loops: an arm latch set at a start, on link silence, on a distrusted heartbeat and by a refused ARM, released by CLEAR alone. CHAN_CFG and OUTPUTS refuse a role, a throttle channel's range and a slot change while ARMED. It publishes what the ESC reports over extended DShot telemetry -- speed, voltage, current, power and the ESC's own temperature. With an INA228 enabled on the SENSE page and answering, BENCH carries the INA228's voltage, current, power, peaks, charge and energy instead (see Current monitors). Core 0 runs the link and the outputs; core 1 runs the current monitors' bus and nothing else |
| IOmcu firmware update over CAN | built and host-tested. The boot ROM's partition table holds slots A and B of 2036KiB; the panel writes the slot that is not running through UPDATE_CTL (0xF0) and UPDATE_DATA (0xF1) in 64-byte blocks with a CRC-16 each and a SHA-256 over the image, the IOmcu restarts into it on trial (TBYB, try before you buy), and the panel's BUY keeps it; without the BUY the boot ROM's 16.7s watchdog returns to the old image. The IOmcu refuses ARM for the whole session and writes flash only behind the store's gate. A session ends after 5s without a request to either update page, and a panel that finds one at its check ends it with ABORT. After the BUY the IOmcu erases the old slot's first sector, and owes the erase until it holds when it fails or the gate is shut. The panel carries the image when built with `RCBENCH_IOMCU_BIN`, checks the IOmcu's build by SHA-256 at link-up and offers the update on a question that takes the 2s hold. `test_link_update` runs both ends over the link layer against a simulated flash and boot ROM; `test_coproc_update_screen` the question and the progress; `test_upd_coproc` SETUP > UPDATES' IOmcu section, whose 2s hold starts the same update, also after a declined question. The transfer time is not measured; the bus time for the 0.18 image is 7.1 to 8.6s. An IOmcu loaded before the table takes one USB load of `rcbench-iomcu-install.uf2`. [Reference](docs/Link.md#firmware-update-over-the-link) |
| Output drivers (PWM, PPM, DShot, bidirectional DShot) | built; the frame arithmetic, the group code, the reply sampler and the GPIO-to-PWM-slice fold are host-tested. No instrument has been on any output pin. [Reference](docs/DShot.md) |
| S.BUS decoder | built and tested; the PIO (programmable input/output) receiver is not written |
| Motor pole count over the link | the panel sends `Motor poles` on the CONTROL page when the IOmcu answers, at every edit, and in the frame that switches to ARMED, so a run starts on the count that was sent or does not start; with none sent the IOmcu reports no speed rather than one derived from a guess |
| Outputs screen | built and tested on the host: a protocol list and a pin grid behind the OUTPUTS tile on SETUP. Every visit, here and on PICK A PIN, opens on OFF, the overview: every bound pin named by its protocol, and a tap on a pin edits and writes nothing until a protocol is picked; OFF stays selected across leaving the screen and read-backs, and a change of board selects OFF whatever was selected. Ticking or unticking a pin writes the binding and reads it back; a pick in the list writes nothing, and OFF shows every bound pin's protocol and edits nothing. A write goes one frame per exchange, each acknowledged (`shared/link/link_port.c`); on an IOmcu speaking 4.10 both pages are prepared on BIND_CFG and BIND_OUT and put in force by one BIND commit carrying their CRC-16, both or neither (`shared/outputs/bind_link.c`, `out_stage.c`), and an IOmcu older than that is written entry by entry. A read gives a binding, pages not read, pages no binding describes, or a board with no pin map; in the second and third the screen and PICK A PIN keep the last binding read, dimmed, and send no write, except UNBIND ALL PINS held 2s over pages no binding describes. `test_bind_link` runs the sequences against the IOmcu's page rules and a model of the XL2515's 2-frame buffer: a 20ms deaf window at every 125us, a frame lost at each of 18 positions, the timeouts across the 2^32ms wrap. Whether one frame per exchange ends the `NO LINK` a tester's bench shows on 0.14.0 with an INA3221 enabled is not known; why a frame is lost there is not known. Reserved pins are shown and refused. PICK A PIN has the same protocol list, the count `N OF 8 SLOTS IN USE` with the refusal reason, a PAD column, and shows the programming port's pin (GP13 by default) held under `PROG PORT` (`test_picker_screen`, `test_picker_screen_hold` and `test_picker_screen_table`, 42 cases). |
| Output binding in the IOmcu's flash | built: the data partition `store` of the partition table, 0x3FC000 to 0x3FDFFF (two 4KiB sectors), as 16 record slots of two 256-byte pages (32 one-page slots for the records of version 7 and older), read by storage address through the uncached, untranslated XIP (execute in place) window; a store with no record reads the store's old place, the last two sectors of the 4MB, once, so a USB load that installs the table keeps the bindings (on an RP2350 A2 the load's RP2350-E10 block can erase the second of those sectors); restored at boot, saved once the bank is idle and the bus quiet. A save is one program of two pages; a sector erase falls to one per eight saves and is taken ahead of the save that needs it, or at boot. The placement rules are host-tested in `test_outstore`; Eight saves of the single-sector store this one replaces printed an erase-and-program window of 19,174 to 19,186us. Every erase and program runs through `flash_safe_execute()`, which parks core 1 in RAM for the window; a window core 1 does not stop for within 10ms writes nothing, prints `window refused` and is tried again 1000ms on. A boot whose core 1 does not register for the lock-out within 100ms switches the store off: no window opens and `flash_safe_execute()` is not called, set-ups run from RAM until a restart, STATUS fault bit 6 is set and the console says so every 3s. The offsets are the 4MB module's rather than the 16MB board file's. The record is version 8, 308 bytes: the bindings, the supply's wiring, the current monitors' set-up, the phase tap, the programming ports and the INA3221 map. Version 3 to 7 records load from the one-page slots, with what they lack off, until the first version 8 save, and the sector holding the newest of them is kept until then; that decode is `shared/outputs/out_store_rec.c`, host-tested in `test_outstore`. A record of any other version reads as unwritten, so the first boot on such a store starts from the defaults -- no driver and no pin in any slot -- and the binding is gone until an operator sets it again on the OUTPUTS screen |
| Current monitors (INA228, INA3221) | drivers built and host-tested against a modelled bus in `test_ina228` and `test_ina3221` (`shared/sense/`). Link pages built and host-tested in `test_sense_page`: SENSE and SERVO_SENSE (protocol 4.7), the bus's pins, clock, addresses, shunts and channels validated (the INA228's shunt and maximum by the driver's `ina228_calibrate()`), kept in the IOmcu's flash, and the pins held from the outputs. The sampling schedule (`shared/sense/sense_sched.c`) built and host-tested in `test_sense_sched` against the modelled bus: per 1ms tick on a 400kHz bus up to 4 INA3221 at 0x40 to 0x43, 12 channels with a shunt each: one channel, FAST (CH1 of the part SENSE names), at 1000Hz, the others in turn one a tick, each bus voltage every 24ms, the INA228's current and voltage at 500Hz each, its die temperature and each INA3221's Mask/Enable at 25Hz and the INA228's DIAG_ALRT, ENERGY and CHARGE at 50Hz, the longest tick 742.5us of bus time (`test_sense_parts`); 50ms windows with a clipped reading kept out of them; a ring of FAST's last 4 windows, read on the page SERVO_WIN (0x31, protocol 4.11), in which a clipped sample counts at the end of the range and is counted; each part's set-up register read back every 40ms, a part found on its power-on set-up taken offline, counted in SENSE register 31, its open window emptied and set up again after 1000ms; the run's INA228 peaks and its ENERGY and CHARGE cleared at the switch to ARMED, no longer the run's after one failed INA228 read; a move capture on FAST, in 1ms steps, timed from a PWM edge the caller gives, judged by the servo test's own rules (`shared/servo/servo_move.c`, `test_servo_move`) with a 4-sample filter, the level before the command the mean of FAST over the 50ms before the edge. The IOmcu reads both parts on core 1 (`firmware/iomcu/src/sense_core1.c`, `sense_i2c.c`): a 1ms tick, the I2C block at 400kHz on the SENSE page's pins with the pads' pulls off, a 1ms limit on each transaction, a bus clear of 9 clocks and a STOP when the bus sticks, and an address scan for PRESENT, repeated every 1s while a part is missing. Core 1's step is `shared/sense/sense_svc.c`, host-tested in `test_sense_svc`. Core 0 hands it orders and takes back a snapshot, each a copy under one spin lock; it publishes the snapshot into SENSE and SERVO_SENSE, fills BENCH from the INA228 at the existing scales with bits 5 and 6, and stamps the capture's edge from the PWM slice's counter, good to 1µs. The flash store's windows run through `flash_safe_execute()`, which parks core 1. Both images compile, and [First run](docs/FirstRun.md) §8 lists what a bench session measures. The panel sets both parts up from SETUP INTERFACES (`shared/bench/sense_link.c`, host-tested in `test_sense_link` against the IOmcu's page code and against a 4.6 IOmcu, which it sends nothing): SENSE read at every link-up, a set-up written 500ms after its last edit while the bank is SAFE and only where it differs, both parts off on the page while a part's own frame changes with both enabled, a refused frame not written again until the next edit, and the identity page read again after a write is taken. It reads SENSE every 50ms while a part is enabled and SERVO_SENSE every 50ms window while the INA3221 is, and says on the band, once each, a part not answering 2.5s after its set-up was taken, another identity at its address, a stuck bus, a clipped reading and STATUS fault bit 6. MOTOR & ESC's TABLE pane shows the ESC's own telemetry beside the INA228's readings with the difference, the totals are the INA228's while BENCH bit 6 is set, and a run's CSV file carries `ina voltage`, `ina current` and `esc current`, and each INA3221 window once. On a 4.11 IOmcu with CH1 enabled the panel reads SERVO_WIN every poll and takes each CH1 window number once and in order, none lost with reads up to 200ms apart and the lost ones counted beyond (`test_sense_windows`, the modelled part through the IOmcu's schedule and pages); a 4.10 IOmcu is sent nothing on that page. `shared/bench/servo_source.c` names the servo rail's meter once per poll -- the INA3221 after 1000ms of a held set-up, a part online, a window with current in the last 200ms and a reset count that stands, the PD mini otherwise, the model with no link -- host-tested in `test_servo_source`; the answer and the windows reach the render task, where the SERVO screen shows the meter's reading in its CURRENT row, line and plot. The panel arms no capture; an automatic servo test reads the meter named at its start, the INA3221's windows or the PD mini (Servo screen, above). Link pages SENSE_MAP, SENSE_WIN_A and SENSE_WIN_B (protocol 4.25) built and host-tested in `test_sense_map`: up to 4 INA3221 with a shunt and a watch target per channel and the FAST channel, kept in the IOmcu's flash in store record version 8 over two 256-byte pages (`test_outstore`: torn writes in either page, the records of version 3 to 7 read until the first version 8 save); derived from SENSE until first written, SENSE's INA3221 bit and registers 8 to 10 mirroring a map in force; every channel's last window on two pages. SENSE_LIMIT (protocol 4.27) built and host-tested in `test_sense_limit`: a current limit per channel, kept in the same record, each 50ms window judged once against it on core 1 as it closes (`sense_window_over()`, `test_sense_svc`), core 0 adding core 1's counts, a sticky OVER bit per channel cleared by its read and a count; an alert only. The panel writes SENSE_MAP and SENSE_LIMIT (`shared/bench/sense_limit_link.c`, `test_sense_limit_link` against the IOmcu's page code: read whole at link-up, the limits written 500ms after their last edit in any bank state, OVER read every 200ms while a limit is held, a band alert at most every 5s with the limit the window was judged by (OVER read once before new limits are written), once in 30s for a channel that keeps passing, channels over at once in turn, all 12 within 12 alerts; bits lost with a reply said as lost after the next link-up from OVER_COUNT kept across it, an IOmcu restart (count 0) not; nothing to an IOmcu older than 4.27, and BAD_PAGE from a 4.27 one said as refused; the alert line `sensors_rows_alert()` in EN and DE in `test_settings_sensors`; SETUP > SENSORS' LIMIT rows marked while the IOmcu does not hold them), and reads the window pages on every poll after SERVO_WIN, at most every 40ms, while a part on the page is fitted (`shared/bench/sense_win_link.c`, `test_sense_win_link`, the poll at 50 and 53ms modelled: FAST's group on every row from the ring, the others missing about 1 row in 17 at 53ms): [the pages](docs/LinkSensors.md). The run log's INA3221 columns follow the map in force, one group per watching channel named after its target, FAST's first, at most 3 (`shared/bench/log_cols.c`, `test_log_cols`, written and read back through the LOGS reader). SETUP > SENSORS sets up to four INA3221 parts at 0x40 to 0x43: per part FITTED, per channel its shunt (0 off, 5.0 to 1000.0mΩ) and what it watches (NONE, an output OUT 1 to OUT 8, one of the four programming ports, SUPPLY), seeded at start-up from the one INA3221 of 0.21 where the store holds no SENSORS key (`shared/settings/settings_sensors.c`, `test_settings_sensors`). The panel writes the map to SENSE_MAP (0x35, protocol 4.25) in one frame of 26 registers, read whole first, 500ms after the last edit and while SAFE, a refused map not again until the next edit, a part with every channel off or on the INA228's address never written, and reads the parts' IDs and FLAGS every 500ms; with the map in force SENSE's ENABLE bit 1 and registers 8 to 10 are the IOmcu's mirror and are never written (`shared/bench/sense_map_link.c`, `test_sense_map_link` against a model of the page as `link_pages.h` states it). An IOmcu older than 4.25 is sent SENSE's one-part frame: FAST's part, else the lowest fitted. FAST, the channel read at 1kHz with the window ring, is the lowest channel with a shunt on a fitted part watching the servo tester's target; with the map in force it is the servo rail's meter on any channel, named over SERVO's plot (`INA 0x41 CH2`), in the LIMIT RUN line and in both reports, and OUTPUTS' PROG PORT entry names the channel watching the port |
| Bench console (`IOMCU_CONSOLE`) | built and host-tested. An IOmcu build option, off by default: a released image holds none of its code, and CI fails when the default image carries a console symbol. With it on, the IOmcu reads command lines on its USB serial port and reads and writes every link page through the link's page handlers (`link_dev_apply()`), which does not feed the link's silence watchdog; `hitec` commands stage, check and send HITEC transactions and report the capture. [Reference](docs/BenchConsole.md) |
| Trace of CH1's 1ms samples (`SENSE_TRACE`) | built and host-tested. An IOmcu build option, off by default: a released image holds none of its code, and CI fails when the default image carries a trace symbol. With it on, core 1 copies each CH1 sample and each CH1 bus voltage out of the schedule's memory into a ring of 4096 records (49,152 bytes), with no bus transaction added to the tick, and core 0 prints them as text on the USB console: 4s after a changed pulse on a PWM slot or a capture edge, 10s after `t` on the console, 8.35 bytes a sample at 3-digit codes (9.35 above about 0.4A on the 0.1Ω shunt) in the output of `sense_trace_gen`, not measured on hardware. Core 0 writes whole lines into the room the console's 64-byte transmit buffer has and nothing without a terminal; a full ring drops records and the trace counts them, in its end line every one from before its end, with or without a record kept after it; a change of the part's set-up or state is a record in the ring, in its place among the samples, with the start time of the tick it was seen in; a trace whose lines a connected console has not taken 29.8h after its end is dropped; a trace ended early by the console or a changed set-up writes what the ring held from before its end. Core 1 publishes the start time of each tick after that tick's records; a trigger line and the end line wait for a tick that started at or after their time, 100ms at most, and an end line written without it carries its reason as a capital letter, which the tool reports as a problem. The rules and the line format are `shared/sense/sense_trace.c` (core 0) and `shared/sense/sense_trace_feed.c` (core 1), host-tested in `test_sense_trace_feed`, `test_sense_trace`, `test_sense_trace_next`, `test_sense_trace_setup` and `test_sense_trace_lines` over `test/host/sense_trace_rig.h`; the firmware side is `firmware/iomcu/src/sense_trace_hw.c`. `tools/sense_trace.py` reads every line of a captured console against the format (every field of the line's type, nothing else, each number in its range) and each trace against its end line, reports a trace with any other line in it as damaged and replays none of its moves, writes a CSV file per trace and replays every move through `shared/servo/servo_move.c` (`test/host/sense_trace_replay.c`) with the filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10A; from the traces alone it names the settings that see and time every move and how far apart the settings' arrivals lie, and with a servo test's CSV file recorded with the AS5600 the arrival less the encoder's travel time; a move is not replayed when a window its levels come from (the samples before the first command, the 50ms and the 200ms before a move, the move's own samples) has no sample, missing records or a change of the part's state, or when another command lies within 30ms of it; `--loaded-channel N` (repeatable) names the output channels with a servo connected, and the `$C` and `$D` lines of every other channel are then no moves and count for no 30ms, which the report's second line counts; the format has no check value, so a line cut inside its last number is read as cut; ctest runs it on the console log `test/host/sense_trace_gen.c` writes for the servo model (`sense_trace_tool`). Not measured: the time core 0's pass gains from writing the lines. [First run](docs/FirstRun.md) §8.9 has the bench procedure: part A without an encoder, part B with the AS5600 |
| Output encoder (AS5600) | built and host-tested. An AS5600 magnetic angle sensor on the servo's output shaft, at 0x36 on the sensor bus (protocol 4.9). The driver (`shared/sense/as5600.c`, `test_as5600`) decodes STATUS, RAW ANGLE, AGC and MAGNITUDE and takes differences on the circle; the schedule reads STATUS and RAW ANGLE at 500Hz on the ticks without a rotation item, each register in a transaction of its own (STATUS 1 byte, RAW ANGLE 2, AGC 1, MAGNITUDE 2), and one slot in 25 reads RAW ANGLE, AGC and MAGNITUDE instead of STATUS (20Hz, 337.5us), and keeps how long the angle has stayed within 12 counts (1.05 degrees) of an anchor, to the 2ms sample interval. SENSE gains ENABLE bit 2 and registers 26 to 31 (flags, angle, magnitude, sample count, still time); the panel gates both on minor 9, says so on the band for an older IOmcu and for a missing, weak or strong magnet, and reads registers 12 to 31 every 40ms (`test_sense_page`, `test_sense_link_view`, `test_link_pages`). SETUP INTERFACES has AS5600 (default OFF) and its centre count; the SERVO screen shows the angle in the MEASURED row and sets the centre from the live count on the DUT page (`test_servo_target`, `test_servo_run`). With it on, the automatic servo test (`test_servo_test_enc`) adds per step the end angles, the angle error against the commanded angle and a travel time from the start of the angle's 100ms stillness within 1.05 degrees, beside the current's, with two CSV columns and a report section; the deadband is not derived, because the moves go end to end. An angle read with STATUS MD clear (no magnet) is no position: the IOmcu holds no still time for it and `sense_link_enc()` hands it to no consumer, so MEASURED shows `---`, ENC CENTRE stores nothing and a run judges no angle and reports how often it happened; with ML or MH beside MD the angle is used and counted in the report. STATUS bits 7, 6, 2, 1 and 0 are masked off, and the probe refuses only ML and MH both set. Angles are compared and averaged on the circle; an end more than 180 degrees from the centre count is reported 360 degrees off, and turns are not counted. Not measured: what the blank STATUS bits read on a part, the sensor on the bus, the tolerance against a real servo's jitter, the delay of the command to the pin |
| Phase tap (ESC tones) | the detector (`protocols/phase_tap/tone.c`, `test_tone`) turns edge times into beeps and their pitch. The capture is built and host-tested: the PIO program (`protocols/phase_tap/rp2350/tone_cap.pio`, 22 of a block's 32 instructions) assembled from its source and run in a cycle-counting model against `tone_holdoff_edge()` in `test_tone_pio` (every decrement of the counter 4 cycles after the one before; a low of 1200 cycles, 8µs, always kept and one of 1196 never); the word decode, the 31-to-64-bit extension and the lap detection in `test_edge_ring`; the pass core 1 runs in `test_tone_svc`; the TONE page (protocol 4.8) with its pin checks, the 64-beep ring and the set-up kept in flash (store record version 6) in `test_tone_page`, `test_link_pages` and `test_outstore`. Core 1 reads the ring on its 1ms tick (`tone_core1.c`) and core 0 owns the state machine and the DMA channel (`tone_cap.c`). Not measured: the PIO program's timing against the system timer, the 2-cycle input delay the model assumes, the DMA ring at 96,000 edges/s and the load on core 1. The panel's side is not part of this row |
| Other receiver buses | not started |
| Servo limit search | built; START LIMIT RUN on the SERVO screen's LIMIT RUN card on TEST, a 2s hold while ARMED, runs it (`shared/servo/servo_limrun.c`) in 10us steps on INA3221 CH1's 50ms windows, bound above 1.8 x the free current and +0.15A: on a SERVO PWM pin from PULSE CENTRE to PULSE MIN and PULSE MAX; on the programming port after a read session, from the servo's NEUTRAL to its own PULSE LOWER and PULSE UPPER, refused before any drive when those lie outside PULSE MIN to PULSE MAX. SERVO PULSES at 1000-2000 writes PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0us in that read session. It reads the INA3221 and nothing else: with the PD mini or the model as the servo rail's meter it is refused before anything is driven, as it is on a rail under 3.0V in the last window, on the programming port without a PD mini on a rail under 4.8V or with a PD mini whose output is off, and, on the bench's outputs, with no output bound as SERVO PWM; a run whose meter changes or that gets no window for 200ms ends ABORTED with the output off. The search and the protections read the current's magnitude. A sample at 3.0A, a clipped one, a search's window without the rail voltage or under 3.0V, window means above 1.0A for 400ms, 40s a search, 3s for the bench to switch, 150s a programmer step and a supply changed outside the programmer's sessions, an output off at the start or a supply that answers only after the start included, end it too; a run that ends while it waits for ARMED asks SAFE. SEARCH + PROGRAM, for a servo on the programming port, lowers the servo's LEFT ANGLE and RIGHT ANGLE (unit not known) in sessions of the programmer -- centre, SAFE, CONNECT, one write, LEAVE, ARMED, the side searched again --: the travel taken as linear in the pulse, the register for each side found from the first write, 2 writes a bound side (an overset 3% above the share at the first bound pulse, then the final value from the share the overset's search found free), both ends searched again; at most 8 values a run, a later session reading another type, REVERSION, pulses or range ending it, RESTORE offering the values before the run, a session after the run saving its own backup. Report `LIMITnnn.TXT` in `USER/LOGS` (`shared/servo/servo_limrun_report.c`). Host-tested against `servo_sim` behind a modelled KST servo (`test_servo_limrun`, `test_servo_limrun_abort` and `test_servo_limrun_report` over `servo_limrun_rig.h`, 30 + 16 + 4 cases, among them the low stop from 920 to 1440us in 3us steps with both pulse settings; `test_kst_panel_limrun` and `test_servo_limit_panel` through the screen and the programmer). The ends found are not applied to the bench's PULSE MIN and MAX. Which register limits which pulse side on a real servo, and whether its travel is linear in the pulse, is not known |
| Servo synchronisation | built and tested against a modelled servo |
| OpenYGE codec | built and tested; not wired in. The implementation is pursued in a separate repository |
| ESC programming profiles | 72 families, 451 models, from 153 manuals; compiled in from `shared/esc/profiles/*.json` and replaced or extended by `/BENCH/ESC/CONF/*.json` on the card at start-up. The reader and the registry are tested on the host. Every profile is unverified and carries no beep timing. The nine profiles for YGE ESCs name where the stick rests while the menu sounds (`scheme.listen`). [Reference](docs/EscProfiles.md) |
| Stick programming | built and tested on the host against a simulated ESC (`shared/esc/esc_stick.c`, `esc_stick_rules.c`, `esc_det.c`, `esc_stick_read.c`, `esc_sim.c`): ESC STICK on MOTOR & ESC > PROGRAM runs 24 of the 72 profiles (13 two-stage, 11 one-stage; at 20V the list opens 23, one needing 22.8V, and refuses 4 more model rows of families that open, each needing 22.8V; 4 model rows that state no voltage rating open with a warning from a warning held for 2s (PROPELLER REMOVED? MOTOR SECURED?: a resistor load or a motor mounted solid without propeller), switching to ARMED and moving the throttle through the MOTOR screen's commands and switching the supply through SUPPLY's. Beeps are counted from the supply current with hysteresis from a quiet line, lengths judged in readings, and a group acted on only when it and the one before it are in the menu's order; a sweep of lost beeps against entry times stores no wrong value in the simulation. A planned end switches the supply off before the stick moves. The list shows the makers, then one maker's models by current, voltage and name, each opening its family's profile at the model's own cell count; a search over maker and family or maker and model filters both levels, case-insensitive with `*` as a wildcard, filtered with every key on a keyboard docked beside it. A stack light on the run and the result shows green while the detector holds a beep (at least 150ms) and red on an end caused by something not as expected, a stop the bench raised itself (BENCH STOPPED, told from a pressed STOP by the arming policy's count of presses) included. Profiles list the steps a person does at the ESC (`manual`: a jumper, a button) with when each is due; 24 carry them, and the 10 Kontronik profiles that run ask for each with a prompt (DONE, ABORT, 60s), powered only with the stick at motor-off; each value is powered up from the stick position the manual programs it from (`entry_throttle`: the Kontronik car modes at MID), the stick moved only with the supply off. MANUAL INTERVENTION REQUIRED on a profile's page and a MANUAL tag in the list show the steps. With the PD mini off, the panel's modelled supply draws the simulated ESC's current. Every beep timing is a default in the settings, not a measurement. [Reference](docs/StickProgramming.md) |
| Measurement front end | parts chosen, nothing fitted: the INA228 as motor monitor and three TPS55285 servo converters with a fourth as the adjustable supply (owner, 2026-09-30); 7 INA3221 port monitors, both motor shunts and the BQ25713 pack charger from round 1 of the component research: [hardware](hardware/STATUS.md) |
| Servo programmer | Parameter table for Hitec D-series servos in the programmer screen; KST and Chaservo servos (two brands with one programming protocol): the protocol core, the RP2350 pin driver, the IOmcu's programming port (its own pin, GP13 by default, with servo PWM from the servo tester or the programming protocol, and the PD mini as the servo's supply), link pages 0x32 and 0x33 and the panel's link module are built and host-tested against a servo, line and supply model; the SERVO screen's programmer (PROGRAM) reads, edits, writes, saves and loads through them, host-tested against the same model: [the screen](docs/ServoProgrammer.md), [the ports](docs/Link.md#the-programming-ports), [the protocol on a port](docs/LinkKST.md), [what a bench has to measure](protocols/kst/README.md#what-is-measured-and-what-is-not). Hitec servos: the wire core, the RP2350 pin driver and link page 0x34 (raw transactions on a programming port, with a POWER cycle at 4800 to 6000mV) are built and host-tested against a line and servo model; no Hitec session is built. A D series line receives with a pull-up on the wire or, with SETUP > INTERFACES > Hitec D receive at PRECHARGE (register 23 of the page, protocol 4.24, the panel's only write to it), by precharge and sample with no pull-up: the decoder host-tested, the receive measured on one D485HW with the bench reader it is built from: [the page](docs/LinkHitec.md), [the receiver](protocols/hitec/README.md#receive-by-precharge-and-sample) |

## The tree

Directories only. `git ls-files <directory>` lists the files of one.

```
rcbench/
  README.md  README-de.md  STATUS.md  CHANGELOG.md  CHANGELOG-0.9.md
  CONTRIBUTING.md  SECURITY.md  LICENSE  NOTICE  .gitattributes  .gitignore
  .clang-tidy  .cppcheck-suppress  ruff.toml  codecov.yml  Doxyfile
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
    safety/ servo/ can/ esc/ sense/ fwupdate/ webaccess/ text/ files/
    clock/

  protocols/              one module per external interface: a pure C core,
                          its RP2350 pin driver in rp2350/, a README.md
    dshot/ ppm/ sbus/ openyge/ pdmini/ phase_tap/ kst/ hitec/

  firmware/panel/         ESP-IDF: main/, components/, sdkconfig.defaults,
                          partitions.csv
  firmware/iomcu/         pico-sdk: src/, include/
```

### Who compiles what

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `files` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` · `text` | ✔ | ✔ | ✔ |
| `artwork` · `esc` · `fwupdate` · `webaccess` · `clock` | ✔ | | ✔ |
| `openyge` · `ppm` | | ✔ | ✔ |
| `sense` · `pdmini` · `phase_tap` · `kst` · `hitec` · `dshot` | ✔ | ✔ | ✔ |

Each module carries one `CMakeLists.txt` that registers an IDF component under
`ESP_PLATFORM` and a static library otherwise. The panel sets
`EXTRA_COMPONENT_DIRS`; the IOmcu and the host suite use
`add_subdirectory()`. Includes are flat: `#include "gfx.h"`. A module under
`protocols/` includes nothing from the rest of the tree, and its `rp2350/`
folder is built by the IOmcu only; `tools/check_protocols.py --check`
fails on an include that leaves the module and on a public header that does
not compile as C++17.

## Tests and CI

CI (continuous integration) runs the workflows below on GitHub Actions.

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under ASan (AddressSanitizer) and UBSan (UndefinedBehaviorSanitizer); coverage `--check` and the Codecov upload, which fails the job when it fails; the font, screenshot, fit, translated-format, docs, wiki-link and research-script checks; `check_protocols.py --check`; clang-tidy and cppcheck over `shared/`, the cores under `protocols/` and `test/host/`, cppcheck's warning, performance and portability classes over `firmware/` and the pin drivers under `protocols/`, and ruff with its docstring (D) rules; the pytest cases of the tools (`test/tools/`); `tools/doc_check.py --check`, the Doxygen comments of the C sources against `tools/doc_check_baseline.txt`, which on a pull request also fails on any warning in a C file the pull request adds or on a line it changes, on a C file it changes that has no `@file` block with a `@brief`, and on a comment block without `@brief` it adds or edits; on a pull request `tools/mutate.py` on the changed lines of `shared/` and of the cores under `protocols/`, which reports survivors and does not fail on one; the panel build on ESP-IDF (Espressif Internet-of-Things Development Framework) v5.5.5 for the ESP32-P4, with the check of the image against its update slot, of the embedded IOmcu image, of the touch injector and of exFAT, and a second build without the injector; the pico-sdk build of the IOmcu with `pinmap_check.py` and the stack check of its two cores, a second build with `-DSENSE_TRACE=ON`, with the same stack check, that fails when the default image holds a trace symbol, and a third with `-DIOMCU_CONSOLE=ON`, with the same stack check, that fails when the default image holds a console symbol; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/`, manual | waits for the CI run on the same commit (`tools/ci_gate.py`, at most 5400s) and publishes `docs/` to the GitHub wiki when it passed |
| `mutation.yml` | Sunday 03:17 UTC, manual | every mutant of the lines of `shared/safety` and `shared/outputs` (3032 mutants) in 6 parallel shards of files, no mutant limit, no mutant started after 18000s in a shard; the mutants left then are reported as not run; does not fail on a survivor; the result is the artifacts `mutation-full-0` to `mutation-full-5` and the job summaries |
| `release.yml` | tag `v*`, manual for a tag | waits for the CI run on the tagged commit and, when it passed, builds both images, packages them with checksums, creates a release, a pre-release for an odd minor number (`tools/release_kind.py`); beside that, without holding the release back, runs the configurator data check (`configurator/core/tools/check-annotate.sh --warn-only`) and shows each warning, such as a release tag newer than the configurator data, as an annotation and in the run summary |
| `pages.yml` | pull request and push to `main` touching `configurator/`, `help/`, `docs/img/`, the help generator, the `Doxyfile` or `tools/install_doxygen.sh`; a push to `main` touching the C sources as well | type check, tests, data check, size budget and Playwright browser tests of the configurator; the web help; the C API (application programming interface) reference built by Doxygen 1.18.0; on a push to `main` it publishes the built page to GitHub Pages, with the help under `help/` and the API reference under `api/`. A run that changes only the C sources, the `Doxyfile` or `tools/install_doxygen.sh` builds the configurator page without its checks and runs no Playwright test |

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

The host suite is 291 binaries, one line per case: `test_gfx`, `test_gfx_faces`, `test_gfx_subpixel`, `test_gfx_clip`, `test_gfx_damage`, `test_gfx_turn`, `test_layout`, `test_touch_map`, `test_overlays`,
`test_touch_merge`, `test_touch_merge_multi`, `test_inj_line_id`, `test_inj_emu`, `test_inj_multi`,
`test_nav`, `test_nav_tiles`, `test_nav_alert`, `test_nav_mark`, `test_nav_contact`, `test_nav_drag`, `test_nav_stop`, `test_nav_bubble`, `test_widgets`, `test_workbench`, `test_prog_registry`, `test_keypad`, `test_keypad_sign`, `test_pinlist`, `test_bench`, `test_supply`,
`test_supply_screen`, `test_supply_screen_tasks`, `test_supply_screen_home`, `test_supply_screen_draw`, `test_supply_layout`, `test_pdmini`,
`test_motor`, `test_motor_draw`, `test_motor_throttle`, `test_motor_arm`, `test_motor_workbench`, `test_motor_program`, `test_motor_layout`, `test_value_state`, `test_servo`, `test_servo_knob`, `test_servo_drag`, `test_servo_repaint`, `test_frame_work`, `test_servo_dial_sweep`, `test_servo_letgo`, `test_servo_curve`, `test_servo_supply`, `test_servo_run`, `test_servo_target`, `test_servo_rail`, `test_servo_meter`, `test_servo_settings`, `test_servo_trim_keypad`,
`test_servo_test_panel`, `test_servo_limit_panel`, `test_servo_workbench`, `test_servo_arm`, `test_safe_press`,
`test_analyser`, `test_prog_models`, `test_esc_stick_panel`, `test_esc_stick_panel_phases`, `test_esc_stick_panel_list`, `test_esc_stick_panel_steps`, `test_esc_stick_panel_makers`, `test_esc_stick_panel_hold`, `test_esc_stick_panel_touch`, `test_esc_stick_panel_home`, `test_esc_stick_panel_keypad`, `test_esc_stick_layout`, `test_balance`, `test_battery`, `test_instruments_layout`,
`test_settings`, `test_settings_screen`, `test_settings_gesture`,
`test_settings_pins_hub`, `test_settings_leave`, `test_settings_folders`, `test_settings_sensors`, `test_settings_keypad`, `test_settings_rows`, `test_setting_keypad_eq`, `test_log_numbers`, `test_log_reader`, `test_log_csv`, `test_link_crc`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_link_bringup`,
`test_link_can`, `test_link_sha256`, `test_link_update`, `test_link_artxfer`, `test_link_drawing`, `test_art_store`, `test_art_fetch`, `test_outputs`, `test_outputs_hw`, `test_outputs_hw_ports`, `test_outstore`,
`test_can_timing`, `test_can_selftest`,
`test_mcp2515`, `test_heartbeat`, `test_arming`, `test_safety_gate`, `test_flash_gate`, `test_reset_cause`, `test_touch_loss`, `test_servo_limit`, `test_servo_limrun`, `test_servo_limrun_abort`, `test_servo_limrun_report`,
`test_servo_sync`, `test_servo_sweep`, `test_servo_move`, `test_servo_test`, `test_servo_test_judge`, `test_servo_test_replay`, `test_servo_test_enc`, `test_servo_test_win`, `test_servo_test_win_read`, `test_servo_test_win_chain`, `test_servo_page`, `test_supply_page`, `test_sense_page`, `test_supply_link`, `test_sense_link`, `test_sense_link_view`, `test_sense_map_link`, `test_sense_win_link`, `test_sense_limit_link`, `test_log_cols`, `test_sense_windows`, `test_servo_source`, `test_sbus`, `test_dshot_frame`, `test_dshot_telem`, `test_dshot_edt`,
`test_ppm`, `test_outbind`, `test_outputs_screen`, `test_outputs_screen_draw`, `test_outputs_screen_off`, `test_outputs_screen_port`, `test_outputs_layout`, `test_picker_screen`, `test_picker_screen_hold`, `test_picker_screen_table`, `test_picker_layout`, `test_board_drawing`, `test_busfault_screen`, `test_coproc_update_screen`, `test_upd_coproc`, `test_upd_layout`, `test_text`, `test_openyge_frame`, `test_openyge_status`,
`test_openyge_params`, `test_esc_profiles`, `test_esc_det`, `test_esc_stick_rules`, `test_esc_stick`, `test_esc_stick_hand`, `test_esc_stick_entry`, `test_ina228`, `test_ina3221`, `test_as5600`, `test_sense_sched`, `test_sense_parts`, `test_sense_map`, `test_sense_limit`, `test_sense_svc`, `test_sense_trace_feed`, `test_sense_trace`, `test_sense_trace_next`, `test_sense_trace_setup`, `test_sense_trace_lines`, `test_tone`, `test_edge_ring`, `test_tone_svc`, `test_tone_page`, `test_tone_pio`, `test_tone_link`, `test_endpoints_link`, `test_hitec_rx_link`, `test_knob`, `test_panel_i2c`, `test_bind_link`, `test_servo_link`, `test_logview`, `test_logview_report`, `test_log_text`, `test_logview_plot`, `test_logview_rank`, `test_logview_pages`, `test_logview_tabs`, `test_logview_delete`, `test_logview_touch`, `test_logview_layout`, `test_log_name`, `test_log_take`, `test_card_tree`, `test_fm_tree`, `test_fm_ops`, `test_fm_recheck`, `test_fm_req`, `test_files_screen`, `test_files_ops`, `test_files_layout`, `test_fault_log`, `test_logwriter`, `test_servo_runlog`, `test_servo_runlog_cols`, `test_servo_log_figures`, `test_log_cadence`, `test_fuzz_engines`, `test_kst_wire`, `test_kst_reg`, `test_kst_plan`, `test_kst_session`, `test_kst_pio`, `test_kst_port`, `test_kst_link`, `test_prog_port`, `test_prog_port_fn`, `test_kst_file`, `test_kst_store`, `test_kst_prog`, `test_kst_prog_card`, `test_kst_panel`, `test_kst_panel_leave`, `test_kst_panel_edit`, `test_kst_panel_write`, `test_kst_panel_texts`, `test_kst_panel_writepage`, `test_kst_panel_screen`, `test_kst_panel_notes`, `test_kst_panel_files`, `test_kst_panel_nocard`, `test_kst_panel_limrun`, `test_kst_panel_ports`, `test_kst_panel_layout`, `test_hitec_wire`, `test_hitec_pio`, `test_hitec_pre`, `test_hitec_port`, `test_hitec_port_stop`, `test_hitec_port_rx`, `test_page_console`, `test_kst_card`, `test_prog_link`, `test_prog_link_ask`, `test_port_mail`, `test_port_screens`, `test_wifi_screen`, `test_wifi_layout`, `test_fw_image`, `test_fw_install`, `test_fw_upload`, `test_fw_release`, `test_fw_release_scan`, `test_wa_session`, `test_wa_scan`, `test_wa_fetch`, `test_wa_fetch_channel`, `test_wa_fetch_task`, `test_upd_screen`, `test_wa_text`, `test_wa_walk`, `test_wa_send`, `test_wa_zip`, `test_wa_page`, `test_wa_page_de`, `test_wa_mdns`, `test_sense_i2c`, `test_storage`, `test_storage_swap`, `test_text_buf`, `test_text_hl`, `test_text_hl_limrun`, `test_fm_name` and `test_wall_clock`. The harness is
`test/host/greatest.h`, written for this project. `test/host/touch_feed.h`
feeds the router, one screen or one widget frames of contacts through
`touch_tracker_update()`, as the panel does, so a touch case cannot use an
event sequence the tracker does not emit. `test/host/tick_wrap.h` runs a
case with the clock started at tick 0 and again a chosen number of ms
before the 32-bit millisecond count wraps at 2^32ms, and a failure names
the start it happened at. `test_outputs_hw` builds the IOmcu's output
layer, `firmware/iomcu/src/outputs_hw.c`, over fake pin drivers and the two
pico-sdk clocks in `test/host/pico_stub/pico/stdlib.h`, and holds the age of
an ESC's last reading across that wrap. `test_sense_i2c` builds the
IOmcu's sensor bus wire, `firmware/iomcu/src/sense_i2c.c`, over a modelled
pair of pads in `test/host/sense_i2c_stub/`, and `test_storage` the panel's
SD card mount, `firmware/panel/components/storage/`, over the ESP-IDF calls
in `test/host/storage_stub/`; `test_storage_swap` holds a card taken out,
swapped or put in while the panel runs over the same slot. `test_text_buf`,
`test_text_hl`, `test_text_hl_limrun` and `test_fm_name` hold the core of
the file manager and text editor, `shared/files/`: the text buffer at 1MiB and 65,536 line ends and one byte
or line past them, every kind of line end, the BOM (byte order mark), tabs
and UTF-8 units against `gfx_utf8_next()`; the highlighter on the servo
test's and the limit run's reports in English and German, line by line,
and on the KST files; the name rules and the path limits against the
storage component's walk. `test_fm_req` runs the card task's
requests (`shared/files/fm_req.c`) over a card in memory: a page, INFO of
a file and of a folder, a job to its end, CANCEL and ARMED between two
chunks of a copy. `test_files_screen`, `test_files_ops` and
`test_files_layout` drive FILES through taps over that card behind a card
task on the test's thread (`test/host/files_host_io.c`): the panes, the
paging, a card swapped, each operation and each refusal, the write gate,
the name keyboard and the layout. The panel's card task and its table
over the storage component, `firmware/panel/main/panel_files.c`, are not
host-tested. `test/host/guard_canvas.h`
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
takes 6.2s on one x86-64 host, 8.1s in the sanitizer build, and ctest
stops it after 120s. `tools/mutate.py --files shared/safety/heartbeat.c
--max-mutants 0` makes 36 mutants and the suite fails on each. A case that is
defined and not run fails the build (`-Werror=unused-function`).
`tools/check_docs.py` holds this list to `test/host/CMakeLists.txt`.
Of the 25 `test_kst_*` binaries, 23 run against the servo and line model in
`test/host/kst_sim.h`; no servo is connected. 5 run the core of
`protocols/kst/`; `test_kst_file` is the one for the configuration file
and reads 40000 random texts from 1 fixed seed. `test_kst_store` runs
the card layer for those files, `shared/logfile/kst_store.c`, against a
directory held in memory. `test_kst_pio` runs the pin driver's arithmetic and its
PIO (programmable input/output) program, word by word, in a model of the
state machine; the IOmcu build fails when the assembled program
differs from the words the test runs. `test_kst_port` runs the
IOmcu's programming port and its page with a model of the supply,
`test_prog_port` the ports' pins and pulse page, and `test_kst_link` the
panel's module through the dispatcher against that port.
`test_kst_prog` runs the programmer's state and `test_kst_prog_card` its
card operations (the save, the list and the load of servo files), both over
the rig `test/host/kst_prog_fix.h`; `test_kst_panel` and its seven siblings `test_kst_panel_leave`, `test_kst_panel_edit`,
`test_kst_panel_write`, `test_kst_panel_texts`, `test_kst_panel_writepage`,
`test_kst_panel_screen` and `test_kst_panel_notes` run its
overlay on the SERVO screen through touches, `test_kst_panel_files`,
`test_kst_panel_nocard` and `test_kst_panel_limrun` the same overlay's servo
files, its cases with no card and the limit run (all four over
`test/host/kst_panel_rig.h`), `test_kst_card` the
adapter between the two and the card layer, with a card that fails,
fills and is removed, and `test_prog_link` the panel's module for the
ports' page. ctest also
builds and runs `kst_headers_cxx`, which calls the module through its 7
headers from C++11, when the host has a C++ compiler.
`test_hitec_wire` runs the core of `protocols/hitec/`: the frames of both
wire families, the transactions and every refusal of the reply decoders,
with frames and reply times read off bench captures. `test_hitec_pio` runs
the pin driver's arithmetic and its PIO program in a model of the state
machine, as `test_kst_pio` does; the IOmcu build fails when the assembled
program differs from the words the test runs. No servo is connected.
`test_hitec_pre` runs the precharge-and-sample receiver's core: the cycle
counts at every clock from 48 to 250MHz, the sampler's program stepped
instruction by instruction, a 7-byte answer at 7.99 samples a bit with the
servo 4% fast and slow, a start bit on the first sample and on the
window's last, a framing error, no answer, a line stuck high, the 30ms
window, a sampler that stops short and an abort in every phase.
`hitec_headers_cxx` calls the module through its 3 headers from C++11.
`test_hitec_port` and `test_hitec_port_stop` run the HITEC page
(`shared/outputs/hitec_port.c`) against a model of the wire driver with a
classic and a D series servo on it and a PD mini (both over
`test/host/hitec_port_rig.h`): the first every refusal, each transaction
kind and decoder, the key, the check and `SEQ` across 65535; the second a
stop in each state and what it blocks until CLEAR, POWER's 4800 to
6000mV and its timers across the wrap of the millisecond clock, and the
10s abort. `test_hitec_port_rx` runs the page's receive mode
(register 23): what an OPEN takes, classic ignoring it, the transactions a
PRECHARGE line refuses, and the supply switched off under a PRECHARGE
receive. `test_hitec_rx_link` runs the panel's module that writes SETUP's
Hitec D receive to the page, against an IOmcu of 4.24 and of 4.23.
`test_page_console` runs the
bench console (`shared/outputs/page_console.c`) on a page table with the
HITEC page: the raw read and write, the link's refusals, the parsing, the
hitec commands down to the registers they stage, `hitec open` with `pu`
and `pre`, the classic and D series
commands refused on a line of the other family, one report for a CLOSE of a
watched transaction, the worked example of `docs/LinkHitec.md`, and a
console that does not hold the link's failsafe off. `test_kst_port` holds the KST page to a port the HITEC page holds, and
`test_link_watchdog` holds `link_dev_apply()` to the watchdog it leaves
alone.
ctest runs one case more that is not a binary of the suite:
`sense_trace_tool`, `tools/sense_trace.py` on a synthetic console log
(`test/host/test_sense_trace_tool.py`).

Coverage floors: 94% overall, 85% for every file. `tools/coverage.py --check` fails on drift of the table
below and of the figure in the two READMEs, and on a C file under `shared/`
or a core file under `protocols/` that is in neither of its two lists, is not compiled, or has no counters.
`render_ui.py --check` holds 496 committed screenshots to the current
render, 248 in English and the same 248 in German, and `render_ui.py --fit`
fails on a string that overflows where it is drawn, in either language,
except an English overflow it lists as known (none listed). `image_slot.py`
holds the panel image to the smaller of its two update slots.
`stack_check.py --iomcu` holds each IOmcu core's deepest chain, one
interrupt and 256 bytes to the core's stack: core 0 reaches 1484 of
4096 with 528 for an interrupt. The panel's stack depths on the ESP32-P4
are not measured: `stack_check.py` decodes Xtensa code only and stops on
the P4's RISC-V image, and no run-time high-water log exists yet. No CI step
holds a frame-cost ceiling or the internal RAM of the panel.
`check_docs.py` holds the protocol version,
the heartbeat and link timings, the heartbeat and CAN pins, the coverage
floors, the stack margin, the servo test's constants and the ESP-IDF and
pico-sdk tags the pages state to the headers, the tools and `ci.yml`.
`check_sanitizers.py` holds every compile command of the sanitizer build to
`-fsanitize=address,undefined -fno-sanitize-recover=all`.

<!-- coverage:start -->
| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 668 | 659 | 98.7% |
| `shared/gfx/gfx_text.c` | 233 | 221 | 94.8% |
| `shared/gfx/gfx_seg.c` | 102 | 99 | 97.1% |
| `shared/gfx/gfx_damage.c` | 116 | 116 | 100.0% |
| `shared/gfx/gfx_turn.c` | 111 | 109 | 98.2% |
| `shared/touch/touch_map.c` | 128 | 128 | 100.0% |
| `shared/touch/touch_merge.c` | 146 | 146 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 343 | 336 | 98.0% |
| `shared/ui/ui_icons.c` | 189 | 189 | 100.0% |
| `shared/ui/ui_band.c` | 270 | 257 | 95.2% |
| `shared/ui/ui_watermark.c` | 42 | 40 | 95.2% |
| `shared/ui/ui_plot.c` | 194 | 184 | 94.8% |
| `shared/ui/ui_slider.c` | 163 | 154 | 94.5% |
| `shared/ui/ui_tabs.c` | 97 | 89 | 91.8% |
| `shared/ui/ui_workbench.c` | 65 | 65 | 100.0% |
| `shared/ui/ui_target_row.c` | 93 | 92 | 98.9% |
| `shared/ui/prog_registry.c` | 417 | 414 | 99.3% |
| `shared/ui/ui_text.c` | 131 | 130 | 99.2% |
| `shared/ui/ui_router.c` | 380 | 373 | 98.2% |
| `shared/ui/ui_alert_slot.c` | 33 | 33 | 100.0% |
| `shared/ui/splash_screen.c` | 72 | 69 | 95.8% |
| `shared/ui/overview_screen.c` | 96 | 94 | 97.9% |
| `shared/ui/motor_screen.c` | 260 | 253 | 97.3% |
| `shared/ui/motor_events.c` | 165 | 161 | 97.6% |
| `shared/ui/motor_draw.c` | 359 | 347 | 96.7% |
| `shared/ui/motor_workbench.c` | 167 | 160 | 95.8% |
| `shared/ui/motor_settings_page.c` | 184 | 174 | 94.6% |
| `shared/ui/motor_program_page.c` | 170 | 165 | 97.1% |
| `shared/ui/supply_screen.c` | 445 | 439 | 98.7% |
| `shared/ui/supply_events.c` | 309 | 305 | 98.7% |
| `shared/ui/supply_draw.c` | 392 | 383 | 97.7% |
| `shared/ui/ui_keypad.c` | 273 | 271 | 99.3% |
| `shared/ui/ui_setting_keypad.c` | 85 | 85 | 100.0% |
| `shared/ui/ui_textkey.c` | 346 | 335 | 96.8% |
| `shared/ui/ui_pinlist.c` | 49 | 49 | 100.0% |
| `shared/ui/servo_screen.c` | 364 | 355 | 97.5% |
| `shared/ui/servo_screen_sweep.c` | 471 | 468 | 99.4% |
| `shared/ui/servo_screen_link.c` | 382 | 378 | 99.0% |
| `shared/ui/servo_screen_touch.c` | 285 | 271 | 95.1% |
| `shared/ui/servo_screen_draw.c` | 529 | 516 | 97.5% |
| `shared/ui/servo_settings_panel.c` | 510 | 495 | 97.1% |
| `shared/ui/servo_settings_draw.c` | 340 | 333 | 97.9% |
| `shared/ui/servo_test_panel.c` | 430 | 418 | 97.2% |
| `shared/ui/servo_limit_panel.c` | 408 | 369 | 90.4% |
| `shared/ui/servo_workbench.c` | 337 | 335 | 99.4% |
| `shared/ui/kst_panel.c` | 339 | 333 | 98.2% |
| `shared/ui/kst_panel_text.c` | 160 | 160 | 100.0% |
| `shared/ui/kst_panel_warn.c` | 271 | 268 | 98.9% |
| `shared/ui/kst_panel_edit.c` | 329 | 322 | 97.9% |
| `shared/ui/kst_panel_touch.c` | 216 | 213 | 98.6% |
| `shared/ui/kst_panel_run.c` | 155 | 145 | 93.5% |
| `shared/ui/kst_panel_draw.c` | 506 | 500 | 98.8% |
| `shared/ui/analyser_screen.c` | 256 | 255 | 99.6% |
| `shared/ui/balance_screen.c` | 381 | 381 | 100.0% |
| `shared/ui/battery_screen.c` | 222 | 217 | 97.8% |
| `shared/ui/prog_models.c` | 8 | 8 | 100.0% |
| `shared/ui/esc_stick_pages.c` | 111 | 104 | 93.7% |
| `shared/ui/esc_stick_panel.c` | 489 | 473 | 96.7% |
| `shared/ui/esc_stick_list.c` | 246 | 233 | 94.7% |
| `shared/ui/esc_stick_touch.c` | 359 | 357 | 99.4% |
| `shared/ui/esc_stick_draw.c` | 466 | 450 | 96.6% |
| `shared/ui/esc_stick_draw_run.c` | 572 | 517 | 90.4% |
| `shared/ui/log_viewer_screen.c` | 321 | 312 | 97.2% |
| `shared/ui/log_viewer_load.c` | 210 | 183 | 87.1% |
| `shared/ui/log_viewer_draw.c` | 417 | 405 | 97.1% |
| `shared/ui/log_viewer_events.c` | 492 | 478 | 97.2% |
| `shared/ui/log_select.c` | 93 | 93 | 100.0% |
| `shared/ui/log_text.c` | 138 | 136 | 98.5% |
| `shared/ui/files_screen.c` | 621 | 594 | 95.7% |
| `shared/ui/files_draw.c` | 332 | 321 | 96.7% |
| `shared/ui/files_events.c` | 208 | 207 | 99.5% |
| `shared/ui/settings_screen.c` | 492 | 486 | 98.8% |
| `shared/ui/settings_draw.c` | 220 | 215 | 97.7% |
| `shared/ui/settings_rows.c` | 58 | 57 | 98.3% |
| `shared/ui/sensors_rows.c` | 128 | 128 | 100.0% |
| `shared/ui/outputs_screen.c` | 497 | 493 | 99.2% |
| `shared/ui/outputs_port.c` | 167 | 166 | 99.4% |
| `shared/ui/outputs_port_entry.c` | 296 | 292 | 98.7% |
| `shared/ui/port_view.c` | 81 | 81 | 100.0% |
| `shared/ui/port_mail.c` | 101 | 101 | 100.0% |
| `shared/ui/picker_screen.c` | 316 | 314 | 99.4% |
| `shared/ui/picker_geometry.c` | 138 | 134 | 97.1% |
| `shared/ui/picker_draw.c` | 311 | 294 | 94.5% |
| `shared/ui/picker_port.c` | 121 | 121 | 100.0% |
| `shared/ui/board_drawing.c` | 88 | 85 | 96.6% |
| `shared/ui/busfault_screen.c` | 320 | 319 | 99.7% |
| `shared/ui/wifi_screen.c` | 498 | 491 | 98.6% |
| `shared/ui/update_screen.c` | 105 | 105 | 100.0% |
| `shared/ui/coproc_update_screen.c` | 271 | 270 | 99.6% |
| `shared/ui/upd_screen.c` | 605 | 582 | 96.2% |
| `shared/settings/settings.c` | 370 | 363 | 98.1% |
| `shared/settings/settings_sensors.c` | 128 | 127 | 99.2% |
| `shared/settings/settings_text.c` | 14 | 13 | 92.9% |
| `shared/logfile/log_numbers.c` | 393 | 371 | 94.4% |
| `shared/logfile/log_csv.c` | 468 | 444 | 94.9% |
| `shared/logfile/log_reader.c` | 212 | 201 | 94.8% |
| `shared/logfile/log_fields.c` | 72 | 71 | 98.6% |
| `shared/logfile/log_name.c` | 120 | 120 | 100.0% |
| `shared/logfile/log_take.c` | 96 | 95 | 99.0% |
| `shared/logfile/card_tree.c` | 76 | 76 | 100.0% |
| `shared/logfile/fault_log.c` | 58 | 58 | 100.0% |
| `shared/files/text_buf.c` | 444 | 443 | 99.8% |
| `shared/files/text_hl.c` | 337 | 335 | 99.4% |
| `shared/files/fm_name.c` | 116 | 116 | 100.0% |
| `shared/files/fm_select.c` | 76 | 76 | 100.0% |
| `shared/files/fm_rules.c` | 173 | 172 | 99.4% |
| `shared/files/fm_tree.c` | 165 | 164 | 99.4% |
| `shared/files/fm_ops.c` | 401 | 395 | 98.5% |
| `shared/files/fm_req.c` | 89 | 88 | 98.9% |
| `shared/safety/heartbeat.c` | 58 | 58 | 100.0% |
| `shared/safety/arming.c` | 220 | 214 | 97.3% |
| `shared/safety/safety_gate.c` | 55 | 55 | 100.0% |
| `shared/safety/flash_gate.c` | 4 | 4 | 100.0% |
| `shared/safety/reset_cause.c` | 61 | 61 | 100.0% |
| `shared/safety/touch_loss.c` | 52 | 52 | 100.0% |
| `shared/servo/servo_limit.c` | 142 | 136 | 95.8% |
| `shared/servo/servo_sync.c` | 183 | 177 | 96.7% |
| `shared/servo/servo_sweep.c` | 118 | 114 | 96.6% |
| `shared/servo/servo_move.c` | 145 | 145 | 100.0% |
| `shared/servo/servo_test.c` | 783 | 769 | 98.2% |
| `shared/servo/servo_str.c` | 43 | 43 | 100.0% |
| `shared/servo/servo_report.c` | 511 | 511 | 100.0% |
| `shared/servo/servo_limrun.c` | 625 | 600 | 96.0% |
| `shared/servo/servo_limrun_report.c` | 263 | 259 | 98.5% |
| `protocols/openyge/openyge_frame.c` | 172 | 169 | 98.3% |
| `protocols/openyge/openyge_status.c` | 39 | 39 | 100.0% |
| `protocols/openyge/openyge_params.c` | 66 | 66 | 100.0% |
| `shared/esc/esc_json.c` | 601 | 559 | 93.0% |
| `shared/esc/esc_json_lex.c` | 292 | 285 | 97.6% |
| `shared/esc/esc_registry.c` | 168 | 166 | 98.8% |
| `shared/esc/esc_stick.c` | 552 | 540 | 97.8% |
| `shared/esc/esc_stick_rules.c` | 284 | 263 | 92.6% |
| `shared/esc/esc_det.c` | 132 | 130 | 98.5% |
| `shared/esc/esc_stick_read.c` | 72 | 69 | 95.8% |
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
| `protocols/hitec/hitec_wire.c` | 247 | 247 | 100.0% |
| `protocols/hitec/hitec_pio.c` | 161 | 161 | 100.0% |
| `protocols/hitec/hitec_pre.c` | 216 | 208 | 96.3% |
| `shared/logfile/kst_store.c` | 209 | 209 | 100.0% |
| `shared/outputs/kst_port.c` | 622 | 621 | 99.8% |
| `shared/outputs/hitec_port.c` | 492 | 491 | 99.8% |
| `shared/outputs/page_console.c` | 567 | 537 | 94.7% |
| `shared/outputs/prog_port.c` | 405 | 405 | 100.0% |
| `shared/bench/kst_link.c` | 433 | 431 | 99.5% |
| `shared/bench/kst_prog.c` | 1318 | 1290 | 97.9% |
| `shared/bench/kst_prog_card.c` | 248 | 243 | 98.0% |
| `shared/bench/kst_prog_file.c` | 148 | 143 | 96.6% |
| `shared/bench/kst_card.c` | 134 | 130 | 97.0% |
| `shared/bench/prog_link.c` | 279 | 279 | 100.0% |
| `shared/can/can_timing.c` | 105 | 103 | 98.1% |
| `shared/can/can_selftest.c` | 145 | 135 | 93.1% |
| `shared/can/mcp2515.c` | 44 | 43 | 97.7% |
| `shared/link/link_bringup.c` | 81 | 81 | 100.0% |
| `shared/link/link_can.c` | 104 | 102 | 98.1% |
| `shared/link/link_crc.c` | 13 | 13 | 100.0% |
| `shared/link/link_dev.c` | 90 | 87 | 96.7% |
| `shared/link/link_host.c` | 136 | 132 | 97.1% |
| `shared/link/link_artxfer.c` | 94 | 93 | 98.9% |
| `shared/link/link_drawing.c` | 141 | 141 | 100.0% |
| `shared/link/link_control.c` | 35 | 35 | 100.0% |
| `shared/link/link_port.c` | 38 | 38 | 100.0% |
| `shared/link/link_sha256.c` | 85 | 85 | 100.0% |
| `shared/link/link_update_image.c` | 95 | 89 | 93.7% |
| `shared/link/link_update_dev.c` | 277 | 276 | 99.6% |
| `shared/link/link_update_host.c` | 444 | 441 | 99.3% |
| `shared/artwork/art_store.c` | 193 | 187 | 96.9% |
| `shared/artwork/art_fetch.c` | 81 | 81 | 100.0% |
| `shared/bench/bench_state.c` | 220 | 216 | 98.2% |
| `shared/outputs/outputs.c` | 230 | 227 | 98.7% |
| `shared/outputs/outputs_pages.c` | 311 | 302 | 97.1% |
| `shared/outputs/out_bind.c` | 517 | 506 | 97.9% |
| `shared/outputs/out_pwm_map.c` | 20 | 20 | 100.0% |
| `shared/outputs/servo_page.c` | 236 | 234 | 99.2% |
| `shared/outputs/supply_page.c` | 313 | 310 | 99.0% |
| `shared/outputs/out_store_map.c` | 74 | 74 | 100.0% |
| `shared/outputs/out_store_rec.c` | 68 | 68 | 100.0% |
| `shared/outputs/out_stage.c` | 66 | 66 | 100.0% |
| `shared/outputs/bind_link.c` | 72 | 72 | 100.0% |
| `shared/outputs/servo_cmd.c` | 22 | 22 | 100.0% |
| `shared/outputs/servo_link.c` | 555 | 512 | 92.2% |
| `shared/outputs/sense_page.c` | 514 | 514 | 100.0% |
| `shared/outputs/sense_map.c` | 194 | 193 | 99.5% |
| `shared/outputs/sense_limit.c` | 36 | 36 | 100.0% |
| `shared/outputs/tone_page.c` | 251 | 251 | 100.0% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/supply.c` | 170 | 168 | 98.8% |
| `protocols/pdmini/pdmini.c` | 292 | 288 | 98.6% |
| `protocols/pdmini/pdmini_codec.c` | 41 | 39 | 95.1% |
| `protocols/pdmini/pdmini_jobs.c` | 254 | 252 | 99.2% |
| `shared/bench/supply_link.c` | 311 | 301 | 96.8% |
| `shared/bench/sense_link.c` | 601 | 579 | 96.3% |
| `shared/bench/sense_link_view.c` | 269 | 265 | 98.5% |
| `shared/bench/sense_map_link.c` | 316 | 305 | 96.5% |
| `shared/bench/sense_win_link.c` | 123 | 123 | 100.0% |
| `shared/bench/sense_limit_link.c` | 191 | 191 | 100.0% |
| `shared/bench/servo_source.c` | 108 | 108 | 100.0% |
| `shared/bench/tone_link.c` | 451 | 446 | 98.9% |
| `shared/bench/endpoints_link.c` | 80 | 80 | 100.0% |
| `shared/bench/hitec_rx_link.c` | 57 | 57 | 100.0% |
| `shared/bench/knob.c` | 47 | 47 | 100.0% |
| `shared/bench/panel_i2c.c` | 10 | 10 | 100.0% |
| `shared/bench/log_writer.c` | 299 | 286 | 95.7% |
| `shared/bench/log_cols.c` | 47 | 47 | 100.0% |
| `shared/bench/log_cadence.c` | 50 | 50 | 100.0% |
| `shared/sense/sense_bus.c` | 133 | 132 | 99.2% |
| `shared/sense/ina228.c` | 200 | 200 | 100.0% |
| `shared/sense/ina3221.c` | 142 | 142 | 100.0% |
| `shared/sense/as5600.c` | 78 | 78 | 100.0% |
| `shared/sense/sense_sched.c` | 393 | 393 | 100.0% |
| `shared/sense/sense_sched_cap.c` | 129 | 129 | 100.0% |
| `shared/sense/sense_i3221_cfg.c` | 12 | 12 | 100.0% |
| `shared/sense/sense_svc.c` | 194 | 194 | 100.0% |
| `shared/sense/sense_trace_feed.c` | 122 | 122 | 100.0% |
| `shared/sense/sense_trace.c` | 530 | 530 | 100.0% |
| `protocols/phase_tap/tone.c` | 372 | 372 | 100.0% |
| `protocols/phase_tap/edge_ring.c` | 43 | 43 | 100.0% |
| `protocols/phase_tap/tone_svc.c` | 109 | 109 | 100.0% |
| `shared/fwupdate/fw_sha256.c` | 83 | 83 | 100.0% |
| `shared/fwupdate/fw_image.c` | 131 | 130 | 99.2% |
| `shared/fwupdate/fw_upload.c` | 55 | 55 | 100.0% |
| `shared/fwupdate/fw_release.c` | 430 | 430 | 100.0% |
| `shared/fwupdate/fw_install.c` | 60 | 60 | 100.0% |
| `shared/fwupdate/fw_boot.c` | 11 | 11 | 100.0% |
| `shared/webaccess/wa_session.c` | 364 | 362 | 99.5% |
| `shared/webaccess/wa_fetch.c` | 423 | 411 | 97.2% |
| `shared/webaccess/wa_text.c` | 287 | 285 | 99.3% |
| `shared/webaccess/wa_walk.c` | 98 | 93 | 94.9% |
| `shared/webaccess/wa_send.c` | 74 | 74 | 100.0% |
| `shared/webaccess/wa_zip.c` | 386 | 375 | 97.2% |
| `shared/webaccess/wa_page.c` | 149 | 144 | 96.6% |
| `shared/webaccess/wa_mdns.c` | 119 | 118 | 99.2% |
| `shared/clock/wall_clock.c` | 82 | 81 | 98.8% |
| **total** | **56804** | **55597** | **97.9%** |

_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._
<!-- coverage:end -->

## Open items

| Item | State | Needs |
| --- | --- | --- |
| The control page under measurement | the paths a session has to provoke rather than pass through are the failsafe clear, a STOP mid-throttle, the link unplugged while ARMED, an IOmcu reset while ARMED, a panel in ARMED with the IOmcu unpowered and then powered, the first switch to ARMED after a STOP, and a switch refused for each of its five reasons (a servo release still owed, the servo frame rate not known, the throttle's pulse range still owed or not taken by the IOmcu (refused read, inverted pair, refused write, another pair stored), the heartbeat not reported trusted within 300ms, a NACK from the IOmcu). The arm latch and the panel's stop on a lost link are host-tested in `test_safety_gate`, the refusal for an owed pulse range in `test_endpoints_link` | a session with both boards that provokes them one at a time, and writes down what the band said. For the reset: a motor channel on GP0, a scope on GP0 and GP3, ARMED at 70%, the RUN pin held low for 100ms; pulses must not return on GP0 until ARM is held again |
| Output driver timing | no timing has been seen on an instrument. What a host test cannot reach: every bit timing, the DMA ring that plays a PPM frame, the PIO turnaround from a bidirectional DShot frame to its reply, and whether an ESC other than the one AM32 2.21 ESC on a tester's bench (#172) answers | an oscilloscope, a servo, and an ESC that does bidirectional DShot. [What is unconfirmed](docs/DShot.md#what-has-not-been-confirmed-on-a-wire) |
| The flash save costs CAN frames | measured on the bring-up module: eight erase-and-program windows printed 19,174 to 19,186us, and the XL2515's overrun count climbed from 2 to 8 while nine saves were taken. A frame at 1Mbit/s is about 130us and the controller holds two, so 19ms is about 150 frame times, or seventy times over the 260us the two buffers hold, with nobody emptying them; the bus reports no error, because the frames arrived and nobody collected them. A lost request costs the panel LINK_HOST_TIMEOUT_MS (1000ms) of waiting, and 1000ms of silence latches this end's 200ms failsafe, so one lost frame is enough for `FAULT 01`. What the store does about it is above; what it does not do is get under the 260us the two buffers hold. The 19ms is an erase and a page program inside one window, which is what the store this replaces printed: neither half of it is measured on its own. The program is the window every save still pays, and 256 bytes against the erase's 4,096 puts it one to two orders of magnitude below 19ms on serial NOR flash, or roughly 200 to 2,000us against a 260us budget | a board: the page program and erase windows from the console lines, the overrun count across sixteen saves, the heartbeat after a window, and a power cut mid-save |
| Screens that do not name the output they drive | the rule is in [CONTRIBUTING.md](CONTRIBUTING.md#the-active-output). The SERVO screen meets it for its target: the TARGET row on its output card names the protocol and the pins or the programming port on every task and stays in sight under the pages, their keypads and the servo programmer. The MOTOR screen names its throttle pins and the SUPPLY header the PD mini's UART pins. These screens do not meet it: ARM on SERVO and MOTOR switches the whole bank to ARMED and names none of it; the servo programmer overlay names a port number instead of its pin; a PD mini output stays on after SERVO is left while the servo programmer holds its port, and no other screen names it. A user can then drive a pin other than the one they expect | ARM limited to the screen's role; the programmer naming its pin; the held output named in the status band |
| Betaflight logs are listed nowhere | the log viewer opens `.csv` and, as text, a report standing alone (`LIMITnnn.TXT`) only. Nothing in the tree decodes a Betaflight blackbox log, so `.bfl` is neither offered on the card nor named on the screen | a BFL decoder, and the suffix back in `CARD_SUFFIXES` and in the viewer's empty-card line |
| An unbound output slot is invisible from the panel | the IOmcu leaves a slot the silicon cannot serve unbound: a reserved pin, a pin the package does not have, a PWM (pulse-width modulation) compare register another bound pin already holds, or no free PIO (programmable input/output) block. The OUTPUTS page reads back what was asked for and carries no register saying whether a slot is bound, so the outputs screen draws an unbound slot exactly as it draws a driving one and the refusal reaches the operator as a lead that does not move | a bound bit per slot on the OUTPUTS page, and the outputs screen drawing the difference |
| IOmcu board file | the build uses `pimoroni_pico_plus2_rp2350` (RP2350B, 16MB flash); the bring-up module is a Waveshare RP2350-CAN (RP2350A, 4MB flash) | a board header for the module, or a `-DPICO_BOARD` in CI. The IO board carries two RP2354B (the RP2350B die with 2MB of flash in the package), a main and a measurement IOmcu (owner, 2026-09-30); the draft pin map uses 44 and 32 of their 48 GPIO (general-purpose input/output) each ([Pins](hardware/docs/Pins.md)). The tree builds one IOmcu image; the measurement IOmcu's image is not written. With 2MB, `firmware/iomcu/partitions.json` needs a 2MB layout: two slots of about 1012KiB, the store and the unused last 8KiB; the image guard in `firmware/iomcu/CMakeLists.txt` takes the slot size from that file: [IO (input/output) board research](hardware/docs/Research.md#consequence-of-the-rp2354bs-2mb-flash) |
| The console's USB-C socket | UART0 (universal asynchronous receiver-transmitter 0) is the primary console and USB-Serial-JTAG (the ESP32-P4's built-in USB (Universal Serial Bus) serial and debug bridge) the secondary (`firmware/panel/sdkconfig.defaults`); which of the board's two USB-C sockets reaches which is not traced | read off the board's schematic |
| S.BUS receiver | decoder built; the inverted 8E2 receiver at 100 kbaud is a PIO program that is not written | PIO program |
| Bus servos are not started | step 5 of the servo tester (#223): Feetech SCS/STS and Dynamixel 2.0 serial servos, S.BUS and S.BUS2 output, and JETI EX Bus channel output, each from its published specification. A "ditec" bus servo on the owner's list is unconfirmed, possibly Hitec; a Hitec bus is taken up only if one is specified. S.BUS2 telemetry slots and configuration over EX Bus stay not planned | the specifications, a servo of each, and a decision on the order |
| Measurement front end | round 1 of [the component research](hardware/docs/Research.md) ran from 2026-09-28 to 2026-09-30 and selected 49 parts. The motor monitor is the INA228 and the servo supply three TPS55285, which the owner keeps although round 1 refutes one at 4.0A continuous from the pack floor (owner, 2026-09-30). The 20 output ports take a channel each of 7 INA3221. Round 2 selects the parts round 1 left open and is planned in the research page. No schematic exists. [The IO board specification](hardware/docs/IOBoard.md) is a draft | [hardware record](hardware/STATUS.md) |
| Monostable | not on any board | hardware |
| Rotary knob | an MT6701 magnetic angle sensor at 0x06 on the panel's I2C terminal, read as 0x03, 0x04 and 0x03 again, one byte each (14 bits; a poll whose two 0x03 bytes differ is no reading), after a probe while it has no reference; the reader, the mapping to the throttle and the horn and the settings are built and tested on the host (`test_knob`). The panel refuses any device address the board answers at on that bus, 0x14, 0x20 to 0x27, 0x30 to 0x3F and 0x5D, and the I2C-reserved 0x00 to 0x03 and 0x78 to 0x7F (`shared/bench/include/panel_i2c.h`, `test_panel_i2c`). The I2C terminal's pin order is from the schematic's text, and the turn direction is the datasheet's. The sensor's I2C registers carry no magnet status | a probe of 0x00 to 0x07 with nothing on the terminal (the one scan covered 0x08 to 0x77), an I2C bus scan with the sensor, then a turn on a bench screen, and Rotary knob ON with no sensor fitted: panel, touch and link stay up |
| Touch injector in the panel build | `RCBENCH_TOUCH_INJECTOR` defaults to ON, so every panel build, releases included, probes 0x14 once at start-up for an injector (`testbench/touch-injector`); `-D RCBENCH_TOUCH_INJECTOR=OFF` leaves the code out, and CI builds that as the extra panel build. Nothing at start-up: 0x14 is not read or probed again until the next start. An injector that answered is merged with the glass, detached after 1000ms silent and probed every 1000ms; the merge is tested on the host (`test_touch_merge`, `test_touch_merge_multi`). The injector runs up to 5 contacts at once (`test_inj_multi`); when the glass and the injector together have more than 5, an injected contact is cut until the injector lifts it, and its UP carries `TOUCH_FLAG_NO_TAP`. The injector's I2C driver half, `touch_inject.c`, runs on the panel only and has no host test. A device answering at 0x14 on the touch bus is accepted as touch input (SECURITY.md) | a panel with an injector attached, and one without |
| Touch ownership on the panel | the router gives each contact one owner from its press to its release, and a contact that leaves the screen body by the status band is released at its last point in the body; tested on the host through the tracker. That release, and the one the tracker makes for a contact more than 120px from its previous report, is marked (`TOUCH_FLAG_NO_TAP`) and activates no control except OUTPUT OFF and START TEST while a servo test runs. STOP and DISARM act on the press; a press the router keeps from the screen (band, STOP, home tag, chip, alert band) releases the screen's contacts the same way, so a dial or slider drag ends at it and the dial's filter commands nothing after it (`test_nav_stop`). A press on a control that stops what the bench drives -- DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST, STOP RUN -- reaches the screen under the alert band and with a chip's help box open, and closes either; ARM and OUTPUT ON there start no hold (`test_safe_press`). SETUP's keys, PROGRAMMER's controls and LOGS's buttons and rows act on a tap: a release by the contact that pressed, inside the control, no more than 8px from the press in x and in y; a swipe matrix of 3528 swipes down SETUP's key columns changes no setting (`test_settings_gesture`). The track id the GT911 gives a lone finger on successive touches is not measured: the host cases assume the same id each time, which is the condition under which a latched drag takes a later contact for its own. Not measured either: the GT911's report interval on the panel, and how often it leaves a moving contact out of a report | a log of the id of 20 successive lone touches, then the sequences of the ownership cases in `test/host/test_nav_contact.c` and `test/host/test_nav_drag.c` replayed by a touch injector under a camera; on SETUP INTERFACES, swipes down the `-` column at three speeds with every value read before and after, a tap and a 1s hold |
| The automatic servo test's threshold | 0.13.0 ran on the bench with the PD mini, from a tester, on three servos: the MS24 (moving 0.16 to 0.18A) passed; the MG90S (holding 0.001A, moving 0.04 to 0.077A) and the 1102HB (holding 0.015 to 0.029A, peaking at 0.039 to 0.044A) moved but read FAIL, every move late, because movement was a reading 0.10A from the level before the command. The threshold is per step, max(0.020A, 3 x the idle noise), and a move with no movement is unseen, not late. Replays of the MG90S's and the 1102HB's CSVs in the host suite (`test/host/fixtures/`) give PASS, 41 and 41 moves timed at 771 to 989ms, and NOT MEASURABLE, 21 of 46 moves timed (those to the low end) and none late. A move has 3000ms plus the meter's lag to arrive: 3300ms on the PD mini. With SETTLING TIME at 500ms the IDLE readings can still carry the move to the centre, which raises the threshold (0.026A on the 1102HB at 5.00V). The PD mini's travel times are an upper bound; the command's delay from the panel to the pin is inside every one and is not measured | a bench run on this build with the MG90S and the 1102HB, and a faster current sensor (INA219, INA3221 or INA228, planned) for travel times that TRAVEL TIME can check |
| The PD mini | the driver, the IOmcu's SUPPLY page and PIO UART, and the panel's half of the page are tested on the host against a modelled module. The protocol is the vendor's sheet and the bench station's notes (#223); its timings are the vendor's Python driver's, not measured. Not measured either: whether a live module goes to ERR when its input sags under the set point, which the switch-off on 2 low input reads assumes; and whether its button or AUTO OUT switch it on while the UART is attached, which the state read before a wiring change assumes | a bench run against the module: identify, set points read back, ON and OFF, a heartbeat stop, a pulled cable, the input lowered under a live set point in 0.5V steps |
| IOmcu core 0's stack | `stack_check.py --iomcu` measures 1484 bytes for the deepest chain and 528 for one interrupt on the 4096-byte stack, 1828 bytes above the 256-byte margin. The figures are those of the image CI builds with arm-none-eabi-gcc 13.2.1, and `stack_check.py --iomcu --check-doc` holds them. The depth is a lower bound: 161 calls through a register are not followed. The stack has no guard (`PICO_USE_STACK_GUARDS` is off). Not measured on hardware. An image built with another option set is measured only when CI builds it | a high-water mark read on a board; a decision on stack guards |
| OpenYGE wire facts | seven items want a capture: rpm scale, CRC (cyclic redundancy check) seed, frame length, legacy header, turnaround, parameter indices, `status2` | an ESC and a logic analyser; [list](docs/OpenYGE.md#8-what-to-measure-before-trusting-this-page) |
| The bench in a browser | serving the interface to a browser on another machine is open; a browser on the panel is not planned. There is no network stack in the tree: no Wi-Fi bring-up, no sockets, no HTTP (Hypertext Transfer Protocol), and Wi-Fi costs internal RAM and CPU time on a board whose frame budget is spent. The safety line is a heartbeat, and a remote client cannot hold one: a browser that stops answering is indistinguishable from one whose user is idle | a read-only client (numbers, plots and logs out; ARM, throttle and STOP stay at the panel), and before any code, a written answer to how a remote session proves it is still present |
| No ESC reports its kV | the ESC screen shows the rated kV, what the motor turns per volt, and the ratio of the two as EFF, an estimate documented as such in [Screens](docs/Screens.md). The rated value is read from the connected ESC when it reports one and from `SET_MOTOR_KV` when it does not; nothing calls `motor_screen_set_esc_kv()` yet, so it is whatever the operator entered, and zero draws the field empty | an ESC parameter set on the link. The OpenYGE cache is built and unconnected; BLHeli_32's parameters are not published |
| The heartbeat has no hardware backstop | the wire from the panel's heartbeat pin to the IOmcu's GP3 is fitted on the bring-up bench. What is not fitted is the retriggerable monostable the wire is supposed to pass through, so firmware at both ends is the only thing gating the outputs. The wire covers a panel that stops beating while the IOmcu is healthy: the monitor sees no edge for HEARTBEAT_MAX_GAP_MS (150ms) and the loop switches to SAFE. Uncovered is a panel that stops beating while the IOmcu cannot act -- nothing then removes the outputs. That is what the monostable does, retriggered by the panel's edges and needing no firmware. A healthy panel beside a misbehaving IOmcu is covered by neither, and by no hardware in this design | the monostable specified in [Safety](docs/Safety.md), on a board. `testbench/WIRING.md` carries the specification and deliberately no part numbers |
| The control task has no test of its own | touch, STOP, the ARMED state, the outputs, the link and the heartbeat run in a task on the core that does not draw. Nothing exercises it deliberately: its files, `main.c`, `panel_link.c`, `panel_poll.c`, `panel_services.c`, `panel_supply.c`, `panel_arm.c`, `panel_art.c` and `panel_update.c` (the IOmcu's update), are not in the host suite. The `runlog` task beside it (`panel_log.c`), which owns every write to the card, is in the same position. A multi-agent review found six defects in it, including a heartbeat that stopped for up to 1000ms on an unanswered poll and a splash tap that latched STOP; those are fixed, and the rules it drives are in `shared/safety/arming.c` under `test_arming`, the link-loss stop and the wait for the far end's word on the heartbeat included | a session with both boards: press ARM, drag the throttle while the screen is busy, press STOP, unplug the link, and confirm the heartbeat's period on a scope at the panel's heartbeat pin. Both cores share PSRAM bandwidth with the display's scan-out, and a scan-out that runs short is an underrun ESP-IDF's DPI driver logs. The control task touches no frame buffer, which is the reason to expect it is clear, not evidence that it is |
| Screen rotation 180° on the board | the PPA copy of a turned frame is tested on the host with a fake engine (`test_gfx_turn`); its time on the board, the frame rate at 180° and the fallback after a PPA timeout are not measured. The host copies 77,093px a frame on average on SERVO LIVE sweeping and 192,911px on MOTOR & ESC LIVE ARMED | a station check: 180° on SETUP > PANEL turns the picture on the next frame; touch lands under the finger at the 4 corners and on STOP; STOP and DISARM work turned; the console's fps and `TURN` on SERVO LIVE at rest and sweeping, NORMAL against 180°; 180° kept over a restart |
| The clock on the board | the NTP request, its console line and the time kept across a software restart are not measured on the board; how long an answer takes is not measured beyond one bench log, 7.2s after the address. A cell in the board's RTC battery holder BAT1 keeps no time: the ESP32-P4 keeps its RTC timer on VBAT only from deep sleep, which the panel never enters |
| Settings save disturbs the picture | `settings_save()` writes NVS while the panel scans. The scan-out reads PSRAM by DMA (direct memory access) with no bounce buffer, and the DSI host's interrupt runs during the write (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), so the picture is expected to stay; not measured | a photo of the home screen after a settings save and after a board drawing's keep, with the console's lines beside it |
| Stick programming's beep timing | the engine, ESC STICK on MOTOR & ESC > PROGRAM and the simulated ESC are tested on the host only. No ESC's menu has been recorded, so the beep and gap lengths, the long beep, the gap between groups, the idle current and the current a beep adds are defaults chosen to be plausible, and the simulated ESC sounds numbers made up to match them. The PD mini is read 100 to 150ms apart at the panel, and whether its current is an instant reading or an average is not known: a beep shorter than about 200ms may not be seen. The ESC's tones after a selection are not decoded, so DONE does not say the ESC stored anything | one ESC of a profile the engine runs, on the PD mini with a resistor load, and once with a motor mounted solid without propeller: a recording of its menu's current at the fastest rate available, the module's read interval, and one run of each kind (two-stage, one-stage) checked afterwards with the ESC's program card |
| The German interface | the tables, the fonts and the fit check are built and tested on the host, and the 248 German screenshots are rendered by the panel's code; An alert already on the band, and the title of a keypad or choice already open, keep the language they were raised in until replaced. | a beta tester's pass over every screen in German on a panel |
| Comment gaps of the existing tree | the comment rule ([CONTRIBUTING.md](CONTRIBUTING.md#comments)) holds new files, changed lines and the `@file` block of a changed file. The other gaps are listed: the Doxygen warnings in `tools/doc_check_baseline.txt`, the Python docstring codes per file in `ruff.toml`. A C file without a `@file` block is one line there, `file has no @file block`; Doxygen reports nothing inside such a file, so its other gaps are not listed until the block is added | documentation of the listed files |

## Constraints

- The XL2515 module's crystal is 16MHz; `test_can_timing` pins it. An 8MHz
  part caps the bus at 500kbit/s.
- No 5V transceiver is in the path: the SIT65HVD230 is a 3.3V part. A 5V
  transceiver on RP2350 bank 0 needs a 2.2kΩ series resistor on RO, because
  the 3.3V rail comes up after the 5V rail on a module build.
- The panel's CAN pins, GPIO21 and GPIO22, are CAN's alone. The console is
  UART0 with USB-Serial-JTAG as secondary.
- The motor monitor is the INA228 (owner, 2026-09-30). Its stock is not read
  after 2026-09-01, when JLCPCB held 29 with a presale count of -477 and
  Digi-Key 0. The INA238 takes the same footprint, and DEVICE_ID (register
  0x3F: 0x2281 or 0x2381) says which is fitted.
- The monostable window is 150ms.
- The panel's DSI (Display Serial Interface) host scans the frame buffers
  out of PSRAM by DMA (direct memory access), about 74MB/s at 60Hz, with
  no bounce buffer, and its interrupt runs during a main-flash operation
  (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`). An underrun is logged by ESP-IDF's DPI
  (Display Pixel Interface) driver and not recovered. Whether a settings
  save or a second core's PSRAM traffic causes one is not measured.
- The link is CAN only. There is no RS485 transceiver, direction circuit or
  turnaround in the design.
- Every stop latches; nothing switches back to ARMED on its own. On the IOmcu a start,
  200ms of link silence, a distrusted heartbeat and a refused ARM set an arm
  latch that only a CLEAR releases. On the panel a STOP, an ARM = 1 sent while ARMED that
  the IOmcu refuses, and a link that goes quiet while
  ARMED or during an arm latch a stop that only a switch to ARMED clears. Two things
  switch the panel to SAFE without that latch: 500ms without touch, after which the
  heartbeat resumes when touch answers again, and a switch to ARMED the IOmcu
  refuses at the hold, after which the heartbeat keeps running. Neither switches the bench to ARMED;
  the next switch to ARMED is a hold.

## Not planned

- **Configuration over JETI EX Bus.** The specification restricts remote
  configuration to JETI products and does not document it.
- **Parameters of ESCs with BLHeli_32 firmware.** The information is not published; the rights holder
  declined in August 2026. [Details](docs/BLHeli32.md).
- **Futaba S.BUS2 telemetry slots.** Answering in the 325µs slot without
  colliding with the receiver's own sensors is out of scope.
- **A web browser on the panel.** The leanest headless Chromium build is
  over 100MB against a 3,968KiB app slot; one blank tab needs more memory
  than the board's PSRAM, of which 1.5MB is the two framebuffers; and the
  multi-process sandbox and the JIT (just-in-time compiler) need processes,
  an MMU (memory management unit) and a POSIX layer, none of which exist
  here. A cut-down HTML renderer would compete with the screens for the
  976KiB per frame of PSRAM traffic in [the budget](docs/Performance.md).
  The other direction, a browser elsewhere reading this interface, is in the
  [open items](#open-items).
- **A ramp on the physical throttle.** `Ramp limit` (5 to 300%/s, default
  55) sets the slew on the panel's own throttle bank, which is the modelled
  bench: `telemetry_sim_step()` reads it and nothing else does, and only
  while the link is down. An IOmcu that is answering is sent the raw
  command on the CONTROL page, `outbind_to_chan_cfg()` writes no
  `LINK_CC_SLEW` for any channel, and a pin bound as a throttle steps to what
  the slider asked for on the next 1ms pass. The bank ramps a throttle
  upward only -- a command below the actual value is applied on the pass it
  arrives, and STOP and a switch to SAFE bypass the slew -- so a ramp on the wire
  would slow the rise and nothing else. The slider is delta-only, the preset
  row is not drawn and a nudge is 1% per press, but a drag applies the
  finger's travel across the track to the whole span and the command slot
  keeps only the latest position, so a swipe across the track inside one
  50ms poll is a 0 to 100% step at the pin. What bounds the rise is finger
  travel between two 50ms writes, and an ESC's own ramp underneath it.
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
| 1. Foundation: tree, ported modules with tests, tools and CI, link codec, heartbeat, IOmcu skeleton, screens | done |
| 2. The CAN link | done 2026-08-28 |
| 3. Measured numbers: ESC telemetry or bidirectional DShot, and the current sensor | extended DShot telemetry fills `bench_state` with the ESC's own voltage, current, power, speed and temperature; on one AM32 2.21 ESC on a tester's bench the voltage read correct and the current read 72A at idle, which looks like an ESC with no current sensor (#172). The bench's own current sensor is selected and not fitted. OpenYGE is pursued in a separate repository |
| 4. Recording: the logger, and the viewer reads what the bench wrote | done |
| 5. Outputs: the control page from the panel, then PWM, then the sum-signal protocols | drivers built and host-tested; no timing seen on an instrument. SETTINGS/OUTPUTS configures a slot |
| 6. Receiver buses, one decoder at a time | S.BUS decoded; the PIO receiver and the other buses open |
| 7. Programming: ESCs with BLHeli_S and AM32 firmware, Hitec D-series servos | screen built; no protocol on a wire |

Next: an instrument on a pin. Every bit timing in this tree was written from a
specification and exercised only against frames the same code builds, and a
servo turning is not a frame period. `testbench/` is what answers that -- the
bench is being assembled. After that, the
monostable, which is the one safety element specified and on no board.
