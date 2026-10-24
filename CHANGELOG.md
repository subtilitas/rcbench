# Changelog

Notable changes to rcbench. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git.

## Unreleased

### Added

- The panel sets its clock by NTP (Network Time Protocol). Each join of
  the home network, the release check at start included, asks
  `pool.ntp.org` once, for at most 30000ms, and logs
  `ntp: pool.ntp.org set 2026-10-24 14:03:12 UTC, offset +...s from the old
  clock`. A join that ends before the answer, the release check's own end
  included, keeps the radio up for it, 30000ms from the request at most;
  ARM, STOP, the supply, a log run, an install and a failed radio take it
  down at once. SETUP > PANEL has a Time zone row: UTC, LONDON, BERLIN, HELSINKI,
  NEW YORK, CHICAGO, DENVER, LOS ANGELES, TOKYO and SYDNEY, BERLIN by
  default, with daylight saving. File stamps, FILES INFO, the Wi-Fi page and
  the ZIP file use it. The board has no RTC (real-time clock) chip: the
  clock starts unset at each power-on, and a software restart keeps it. A
  1980 stamp, an unset clock's, reads `not known` on FILES INFO and leaves
  the Wi-Fi page's time cell empty
  ([Building](docs/Building.md#the-panels-clock)).
- A SERVO run's log, `SERVOnnn.CSV`, has `pulse (us)` and `angle (deg)`
  before `source`: the pulse COMMANDED shows and the AS5600 angle MEASURED
  shows, empty without one. While an INA3221 channel is the servo rail's
  meter, `voltage`, `current` and `power` are its newest 50ms window's,
  otherwise the supply's. A MOTOR run's header and rows stay as they are.
  LOGS reads up to 25 columns, the fullest SERVO file, and opens a SERVO
  file with `pulse`, `angle`, `current` and `voltage` chosen.

- The IOmcu says why it last started. A console line at start-up and every
  3s names the cause and carries every raw value it was read from:
  `reset FLASH_UPDATE (chip_reset 0x00010000 reason 0x1 boot ... params ...
  scratch ...)`, from POWMAN CHIP_RESET, WATCHDOG REASON, the boot ROM's
  boot word and reboot parameters, and WATCHDOG SCRATCH2 and SCRATCH3. The
  words are POR, BOR, RUN_PIN, GLITCH, DEBUGGER, POWER_DOWN, FLASH_UPDATE,
  RAM_IMAGE, BOOTSEL_REBOOT, SW_RESET, WATCHDOG and UNKNOWN
  ([Bring-up](docs/Bringup.md#why-the-iomcu-last-started)).
- A tap on a number's value opens the keypad on every row that sets a
  number: SETUP's PANEL, INTERFACES and SENSORS lists, ESC STICK's TIMING
  and BALANCE's blade count, beside the `-` and `+` keys, which stay. The
  keypad names the setting, its range and its unit and takes the step's
  digits after the point. OK sets the value through the call a step ends
  in, onto the nearer step, and SAVE keeps it as it keeps a step. A value
  outside the range keeps the keypad open and names the range in the
  warning colour. One the keys never reach keeps it open with the reason
  in place of the range until the next key: `0 or 5.0 to 1000.0mΩ` for
  a SENSORS shunt between 0 and 5.0mΩ, `0 or 10 to 32767mA` for a
  LIMIT row between 0 and 10mA, `Pins the bus takes: 0 4 6 14
  16 18 20 26` for a Sensor SDA pin the bus cannot have, listed from the
  check the row's keys use, and `The other PD mini pin holds this GPIO`.
  A shunt of 44.4mΩ from the 100.0 default is five keys, where `-`
  takes 556 steps.
- SERVO > SETTINGS > OUTPUT > TRIM opens a signed keypad: a `+/-` key
  between the point and OK, -200 to +200us, onto the 5us steps of `-` and
  `+`. Keypads of unsigned rows keep their keys where they were; a pin's
  -1, not set, is still set with `-`.
- A settings value with a step finer than 0.1 shows the step's digits after
  the point, and one with a step of 1 none: ESC STICK's Current limit reads
  1.25A where it read 1.2A, MOTOR & ESC's Internal R 18mΩ where it
  read 18.0.
- ESP32-P4 panel: Wi-Fi through the board's ESP32-C6 on SDIO (Secure
  Digital input/output) slot 1, CLK GPIO18, CMD GPIO19, D0 to D3 GPIO14 to
  GPIO17, the C6's enable on GPIO54, 4 bits at 40MHz. The `esp_wifi` calls
  of the WI-FI session, the release check and the download reach the C6
  through the managed components `esp_wifi_remote` 1.6.5 and `esp_hosted`
  2.13.0 (Apache-2.0; esp_hosted carries the protobuf-c runtime,
  BSD-2-Clause, in NOTICE), pinned in `firmware/panel/dependencies.lock`. The
  first Wi-Fi start logs the C6's firmware version. A C6 that does not
  answer at the first start, or an SDIO link that fails later, ends the
  session, holds the C6 in reset (GPIO54 low) and leaves Wi-Fi off until
  the next start; the panel does not restart for it. The card's mount
  starts SDMMC slot 0 before ESP-IDF's mount, so a slot 0 that does not
  start leaves the controller and the C6's link running. Every task created
  through `xTaskCreate()` or `xTaskCreatePinnedToCore()` without a core
  runs on core 0, so esp_hosted's 6 tasks at priority 23 stay off the
  control task's core; the console lists every task's core and priority at
  start-up.
- Four glyph tables for the 1024 x 600 panel: 10x20, 12x24 and 20x36 text
  faces and a 32x44 numeral face (20,400, 29,376, 73,440 and 38,016 bytes
  of flash), and the roles `UI_FONT_FINE` (8x16) and `UI_FONT_BODY`
  (12x24). Cap heights are 12, 15, 23 and 40px. The 32x44 numeral face
  cuts `#` (5px), `$` (16px), `(` (12px), `)` (12px), `,` (23px) and
  `/` (6px) at the cell edge; the three text faces cut nothing, which
  `gen_font.py --check` verifies. No screen draws with the faces yet.
- The console names each SERVO test file as it opens and closes:
  `logging to .../ATEST004.TXT` when the report opens after the CSV,
  `.../ATEST004.CSV closed, 212 lines` or `... closed short: a write or the
  close failed, 212 lines sent` at its close, and the same for
  `LIMITnnn.TXT`. A test that gets no file logs `no test file in ... (take
  n); this test is not recorded`.

### Fixed

- A line on OUTPUTS and PICK A PIN no longer ends with a number alone on
  the next line: `SERVO TESTER DRIVES PROG PORT 1` broke before its `1` at
  30 cells, and now breaks before `PORT 1`. A word of digits alone goes
  down with the word before it.

- STOP and DISARM on MOTOR & ESC and on SERVO act on the press, not on the
  release. On the panel a STOP held 600ms stopped the pulses 0.62s after
  the press, at the lift, and a DISARM held 600ms 0.618s after it. The
  control task's own STOP test and the router's both act at the press; a
  press on DISARM takes no hold, so a finger left on it arms nothing. ARM
  stays a 2s hold.
- A press the router keeps from the screen -- on the band, STOP, the home
  tag, a chip or the alert band -- ends a dial or slider drag another finger
  makes, as a press on the screen does. A STOP pressed during a SERVO dial
  drag let the pulse follow the drag for the whole press.
- The SERVO dial's drag filter stops where it is when a press, a release
  at the band's edge or a touch loss ends the drag, and at every command
  other than its own and a restated position, the knob's included when it
  comes to the filter's last pulse.
- ESP32-P4 panel: the .bss that `firmware/panel/main/psram_bss.lf` names
  is in PSRAM (pseudo-static random-access memory): 29 objects, 166,472
  bytes of `.ext_ram.bss`; internal .bss is 29,408 bytes instead of
  195,880. The fragment
  defines its own scheme, `rcbench_extram_bss`: the `extram_bss` scheme it
  named is defined only in `esp_wifi`'s fragment, which the P4 build does
  not include, and ldgen skipped the mappings without a message. CI fails
  through `tools/psram_bss.py` when a named object keeps .bss in internal
  RAM.
- WI-FI > CHOOSE HOME NETWORK: a failed scan no longer reads as an empty
  list. A scan session starts one scan, however often the station's start
  is reported: esp_hosted 2.13.0 sends it from `req_wifi_start()` and from
  its event handler, and a second `esp_wifi_scan_start()` replaces the
  first, which then reports done with nothing found. Each scan has a token
  that the refusal, the done report and the retry carry: a call of a scan
  that ended acts on nothing of the session started after it. At the
  radio's stop the worker waits, 2000ms at most (`DRAIN_MS`), until the
  event loop has handled the radio's last events, so a done report of a
  radio taken down is never read as the next radio's. A failed
  scan, a list that cannot be read and a read that does not hold what the
  report counts (or a full buffer of 24) run the scan again, 3 scans at
  most (`WA_SCAN_TRIES`) within the 10s; then the list's place says
  `Wi-Fi could not start.` instead of `No WPA2 or WPA3 network found.`, as
  it does for a scan the radio refuses. The console logs each scan's start,
  its done report (status, counted, read, listed, left off) and an ignored
  report.
- A SERVO run's log, `SERVOnnn.CSV`, carries the supply's figures in
  `voltage`, `current`, `power`, `charge` and `energy`: the PD mini's last
  reading at each row, current to 0.001A, and charge and energy counted
  from those readings since the switch to ARMED. These cells were empty on
  a bench without an ESC or an INA228. The header and a MOTOR run's rows
  stay as they are.

### Changed

- The ESC profiles' texts the panel shows write a number and its unit
  together, as the rest of the interface does: `1.2-1.8ms`, `Nach 2s`,
  `8kHz`, 683 numbers in 624 texts of 67 of the 72 profiles. These are the
  strings `tools/gen_esc_profiles.py` compiles into the panel, and
  `tools/check_units.py --check` holds them; a word after a number keeps
  its space (`3 beeps`), and the profiles' research fields stay as written.
- A resistance reads with the ohm sign: `120Ω`, `2.2kΩ`, `100.0mΩ`, in the
  screens' texts, the settings' units (`uΩ` for the INA228's shunt) and the
  servo test's and the limit run's `.TXT` reports, in English and German.
  The panel font's O reads close to a zero, so `120Ohm` read as `1200hm`.
  The five text faces carry U+03A9, 1,784 bytes of glyphs more. The docs
  and the web help write Ω too; the settings' stored keys (`pack_mohm`,
  `ina228_uohm`) stay as they are.

- A number and its unit stand together, with no space: `40s`, `6.00V`,
  `20ms`, `100%`, `180°`, `100.0mΩ`, in English and German. This holds
  for every screen text, the servo test's and the limit run's `.TXT`
  reports, the Wi-Fi page, the web help, the docs and the code's comments.
  A word after a number keeps its space (`3 servos`), and so do the log
  and console lines, the CSV headers and the KST servo configuration file.
  `tools/check_units.py --check` holds the rule in CI; its exceptions are
  in `tools/check_units_allow.txt`, each with its reason.

- `tools/check_sizes.py` counts code lines: a line that is not blank and
  not comment-only. Block and `//` comments, Doxygen blocks included, and in
  Python `#` comments and docstrings do not count; a line with code and a
  trailing comment does. The limits are 1,000 and 1,300 code lines. 26
  entries of `tools/check_sizes.txt` whose files hold 1,000 code lines or
  fewer are removed, and the figure recorded for `shared/bench/kst_prog.c`
  is 1,938 code lines.
- Every text the interface shows or writes in the operator's language is
  one entry of `shared/text/include/ui_text.def`, with its English and its
  German side by side: the screens' strings, the settings' labels, help
  lines, categories and options, the servo test's words and the Wi-Fi
  page's. `shared/ui/ui_text_de.c` and the English in the settings schema,
  `servo_str.c` and `wa_page.c` are gone; every string is unchanged.
  `tools/merge_texts.py merge` carries text changes made in the old form
  into the table ([Interface language](docs/Language.md#the-tables)).
- The panel firmware targets the 7-inch ESP32-P4 display board from
  Waveshare (ESP32-P4-WIFI6-Touch-LCD-7B, chip revision 3.2) and no longer
  builds for the ESP32-S3-Touch-LCD-7. ESP-IDF v5.5.5, target `esp32p4`,
  32MB flash with two application slots of 7936kB. The EK79007 panel runs
  on 2 MIPI DSI (Display Serial Interface) lanes at 1000Mbit/s each, 1024 x
  600 RGB565 at 60.38Hz (measured on the console's VSYNC figure), two
  frame buffers in PSRAM; the UI draws 800 x 480 in the top-left corner and the rest stays
  black. Touch is polled (the GT911's INT line does not reach the chip),
  mirrored in x and y, and a touch outside 800 x 480 is dropped. The card
  mounts over SDMMC with 4 data lines; CAN runs on GPIO22 (TX) and GPIO21
  (RX) from the 40MHz crystal; the rotary knob has an I2C controller of its
  own. The heartbeat pin is IO28 and the knob pins are IO30 (SDA) and
  IO31 (SCL), all on the 12-pin header P3. Its Wi-Fi radio is the
  board's ESP32-C6 on SDIO, listed under Added.

## 0.22.1 - 2026-10-22

0.22.1 is a stable release: its minor number, 22, is even, and GitHub marks
it as the latest release. It is a patch release of 0.22.0, the last release
line for the ESP32-S3 panel, and fixes three defects: LOGS did not list a
report that stands alone, the card's ZIP download broke when a log grew
during it, and a limit run on a servo that is not a KST or a Chaservo
ended with a reason that did not name the cause. The link protocol stays
4.28.

A panel on 0.20.0 to 0.22.0 is offered 0.22.1 by CHECK FOR UPDATES with
RELEASES: STABLE, which reads `releases/latest`; 0.20.0 has no RELEASES
setting and reads the same. The steps for each release are in
[First run, §1](docs/FirstRun.md#1-flash-both-boards):

1. SETUP > UPDATES, `CHECK FOR UPDATES`, with a home network kept, the
   bench SAFE, the supply output off and no log run open. The card says
   `Version 0.22.1 is available.`.
2. Hold `HOLD 2 s: DOWNLOAD & INSTALL`. The panel restarts and keeps 0.22.1
   once the display and touch are up.
3. At link-up the panel offers the IOmcu's update over CAN (Controller Area
   Network). Hold START 2 s on the question on the menu, or `HOLD 2 s:
   UPDATE IOMCU` on SETUP > UPDATES. The band then says `IOmcu updated to
   0.22.1.`.

The SD card stays as 0.22.0 left it; a card of 0.21.0 and older moves as
[From 0.21.0](docs/FirstRun.md#from-0210) states. The settings and the
Wi-Fi settings are kept. The IOmcu's store record is kept; an IOmcu older
than 0.22.0 writes record version 8 at its first save. The release line is
unchanged: a panel on 0.22.1 is offered 0.22.x patch releases only.

### Fixed

- A limit run on PROG PORT whose first CONNECT finds no servo that answers
  (a Hitec servo, for one) ends FAIL with `no KST or Chaservo servo read`,
  not ABORTED with `programming port did not finish`. Every other failed CONNECT,
  LEAVE and timed-out step reads `programming port session failed`, in
  German `PROG-PORT-Sitzung gescheitert`.
- The card's ZIP download stays whole while a log on the card grows: a file
  is sent at the size it had when the download started, a file added
  meanwhile is left out. A file that shrinks, or an entry removed or
  renamed during the download, still fails the archive.
- LOGS lists a report standing alone: a LIMIT RUN's `LIMITnnn.TXT` in
  `USER/LOGS`, and in the root a `SERVOnnn.TXT` with no `SERVOnnn.CSV`
  beside it, the limit run of firmware 0.21.0 and older. It ranks by its
  number among the runs, and the newest is selected when LOGS opens. OPEN
  shows it as text: 14 rows of up to 86 characters, scrolled by a drag or
  the scroll bar, the first 32 KiB of a file. A report beside its run's
  CSV, `ATESTnnn.TXT`, stays off the list, and OPEN on the run reads the
  CSV. The Wi-Fi page lists every file, these reports included, as a
  download.

## 0.22.0 - 2026-10-21

0.22.0 is the last release for the ESP32-S3 panel. Later releases are for
the 7-inch ESP32-P4 display board from Waveshare. A panel on 0.22.0 is
offered 0.22.x patch releases only, with RELEASES on STABLE and on TESTING,
and SETUP > UPDATES says so at the foot of its card
([Screens: The release line](docs/Screens.md#the-release-line)). No
0.22.x patch release is planned.

0.22.0 is a stable release: its minor number, 22, is even, and GitHub marks
it as the latest release. It reads up to four INA3221 current monitors at
0x40 to 0x43, 12 channels, set up on the new SETUP > SENSORS tile with a
shunt, a watch target and a current limit of 10 to 32767 mA for each
channel, and logs them by target. A window above its channel's limit
raises a band alert and nothing else; an IOmcu older than link protocol
4.27 judges no limit. It drives four programming ports, PROG PORT 1 to
4, each with a FUNCTION of SERVO PWM, DSHOT150 to DSHOT600 or NONE; the
servo tester and the KST and Chaservo programmer run on any of them. Hitec D series servos answer
without a pull-up with PRECHARGE receive. The link protocol is 4.28. It
fixes defects of 0.21.2: SERVO's TARGET after a programming session, an SD
card swapped while the panel runs, and Wi-Fi page downloads whose receiver
stops reading.

A panel on 0.20.0 to 0.21.2 is offered 0.22.0 by CHECK FOR UPDATES with
RELEASES: STABLE, which reads `releases/latest`; 0.20.0 has no RELEASES
setting and reads the same. The steps for each release are in
[First run, §1](docs/FirstRun.md#1-flash-both-boards):

1. SETUP > UPDATES, `CHECK FOR UPDATES`, with a home network kept, the
   bench SAFE, the supply output off and no log run open. The card says
   `Version 0.22.0 is available.`.
2. Hold `HOLD 2 s: DOWNLOAD & INSTALL`. The panel restarts and keeps 0.22.0
   once the display and touch are up.
3. At link-up the panel offers the IOmcu's update over CAN (Controller Area
   Network). Hold START 2 s on the question on the menu, or `HOLD 2 s:
   UPDATE IOMCU` on SETUP > UPDATES. The band then says `IOmcu updated to
   0.22.0.`.

The INA3221 rows of SETUP > INTERFACES seed SETUP > SENSORS at the first
start ([From 0.21.2](docs/FirstRun.md#from-0212)). A card of 0.21.0 and
older moves by hand as [From 0.21.0](docs/FirstRun.md#from-0210) states.
The IOmcu's store record becomes version 8 at its first save; an IOmcu
image older than 0.22.0 reads no version 8 record. 0.20.0 to 0.21.2 run
link protocol 4.22 or 4.23, the same major as 4.28, so both boards link
before and after the IOmcu's update. A panel older than 0.22.0 is offered
any newer release GitHub names, also one for the ESP32-P4 panel, and its
checks refuse an image built for another chip. The time of the download
and of the update over CAN is not measured.

### Added

- The panel's release line, 0.22 (`FW_REL_LINE_MAJOR`,
  `FW_REL_LINE_MINOR` in `shared/fwupdate/include/fw_release.h`): a check
  offers a release of 0.22 only, newer than the running build, on
  RELEASES: STABLE and TESTING. A newer release past the line, 0.23.0,
  0.24.0 or 1.0.0, reads `No newer release for the ESP32-S3 panel. Panel
  version: 0.22.0.` with `Latest release on GitHub: 0.23.0.`; it is not
  offered, and the reminder at start does not open for it. With TESTING,
  the highest release of the line among the 10 newest stands in for a
  higher one past the line when it is newer than the running build. With
  STABLE, a latest release past the line and newer than the running build
  is followed by one request of the release list, whose newest 0.22.x is
  offered when it is newer; when that request fails, the result is as
  without it. The foot of the UPDATES card starts with `0.22 is the last
  release for the ESP32-S3 panel. Later releases are for the 7-inch
  ESP32-P4 display board from Waveshare.`.
- Hitec D series servos answer with no pull-up on the signal wire: SETUP >
  INTERFACES > Hitec D receive (PROG PORT, German `Hitec-D-Empfang`)
  chooses PULL-UP, the default, or PRECHARGE. With PRECHARGE the IOmcu
  drives the programming port's pad high for 100.0 ns every 1086.7 ns at
  2 mA and slow slew, samples it 113.3 ns after the release, and decodes
  the samples into the capture the PULL-UP receiver gives. The sampler is a
  second PIO state machine with its own DMA channel; the decode runs a few
  words per main-loop pass, in integer arithmetic. A LISTEN that answers
  and a window above 30,000 us are refused on such a line; writes and LEVEL
  run as before. A stop, CLOSE, ABORT, link loss, heartbeat loss and the
  supply switched off end the sampling with the pad released and its
  registers restored. The window was measured on one D485HW: every read
  passed with the sample 40 to 187 ns after the release and a precharge of
  6.7 to 200 ns, and none from 267 ns. Classic servos need neither and
  ignore the setting.
- Link protocol 4.24: HITEC write register 23 (`RX_MODE`), `STATE` bit 7
  and `VIEW` 2 register 25 bit 8. A 4.23 IOmcu refuses the register and
  receives with the pull-up; SETUP then says `IOmcu older than 4.24:
  receives with PULL-UP.` with PRECHARGE set. With an IOmcu older than
  4.23 it says `IOmcu older than 4.23: no Hitec line.`
- The bench console's `hitec open <port> d [od|pp] [pu|pre]` writes the
  receive mode before the OPEN, and the report names it (`rx=pu|pre`).
- Link protocol 4.25: SENSE_MAP (0x35) sets up to 4 INA3221 at 0x40 to
  0x43, a shunt of 5 mOhm to 1 Ohm (0 off) and a watch target (an output
  channel, a programming port or the supply) for each of their 12
  channels, and the FAST channel. SENSE_WIN_A and SENSE_WIN_B (0x37,
  0x38) carry every channel's last 50 ms window. Until it is first
  written the map is SENSE's one INA3221; once written, SENSE's INA3221
  bit and registers 8 to 10 mirror it and a SENSE write that would change
  them is refused. With a map in force, IDENTITY's capability bit 3
  (`SERVO_SENSE`) is set while a channel it reads watches an output
  channel or a programming port. The panel writes SENSE_MAP from SETUP >
  SENSORS (below) and reads SENSE_WIN_A and SENSE_WIN_B for the run log
  (below).
- The IOmcu's store record is version 8, 308 bytes in a slot of two
  256-byte flash pages, 16 slots in the store; it keeps the map. Records
  of version 3 to 7 are read until the first version 8 save, and the
  sector that holds the last of them is kept until then. An IOmcu image
  older than this build reads no version 8 record and starts as from an
  empty store.
- Link protocol 4.27: SENSE_LIMIT (0x36) holds a current limit of 1 to
  32767 mA (0 none) for each of SENSE_MAP's 12 channels, kept in the store
  record. Core 1 judges each 50 ms window as it closes. A window above
  its channel's limit -- its highest current, or a sample at the top of a
  range above the limit -- sets the
  channel's bit in OVER, which a read of the register clears, and counts
  in OVER_COUNT. An alert only: nothing stops or switches to SAFE. The
  panel writes the limits from SETUP > SENSORS (below).
- SETUP > SENSORS has a LIMIT row per channel, `0x40 CH1 limit` (German
  `0x40 CH1 Grenze`): OFF, or 10 to 32767 mA in 10 mA steps on the
  arrows. The panel writes the 12 limits to SENSE_LIMIT 500 ms after the
  last edit, in any bank state, and reads OVER and OVER_COUNT every 200 ms
  while a limit is held. The band says `INA3221 0x41 CH2 (PORT 1) over its
  0.80 A limit.` with the limit the window was judged by, at most once
  every 5 s, and once every 30 s for a channel that keeps passing;
  channels over at the same time take turns. `An INA3221 channel passed
  its limit; which one was lost on the link.` follows the link-up after a
  reply with the bits did not arrive, and not an IOmcu restart.
  `INA3221 limits rejected. Check SETUP > SENSORS.` follows a refused write
  or a BAD_PAGE answer from an IOmcu that names 4.27 or later. A
  LIMIT row the IOmcu does not hold is marked; on an IOmcu older than
  4.27 the line reads `IOmcu older than 4.27: no limit is judged.` and
  nothing is sent to the page.
- The IOmcu console prints a third sensor line, `sense map`: the parts
  fitted, FAST, the map's flags, each channel's mean current in mA, and
  SENSE_LIMIT's OVER and OVER_COUNT.
- Four programming ports on the IOmcu, PROG PORT 1 to PROG PORT 4. Each
  has a FUNCTION: SERVO PWM, a pulse from the PWM generator, or NONE, the
  pin held as an input with its pull-down. Port 1 defaults to SERVO PWM,
  ports 2 to 4 to NONE with no pin, so a bench that sets nothing on ports
  2 to 4 runs as with one port. PIN and FUNCTION are kept in the IOmcu's
  flash; the panel writes them only when the operator sets them, on the
  PROG PORT entry of SETUP > OUTPUTS or in PICK A PIN, and only on a bench
  in SAFE with no test, sweep or servo programmer running.
- SETUP > OUTPUTS: one PROG PORT entry per port with PIN (opens PICK A
  PIN on that port), FUNCTION, WATCHED BY (no current channel yet), SERVO
  TESTER and PORT PWM. PICK A PIN lists the programming ports and rejects
  a SERVO PWM pin that shares a PWM slice with an output.
- The servo tester's target is OUTPUTS or PORT 1 to PORT 4 (SETUP > SERVO
  > SERVO TESTER). A stored PROG PORT reads as PORT 1. The TARGET row names
  the port: `PROG PORT 2 GP14`. A port without a SERVO PWM pin, or one the
  IOmcu does not have, reads `PROG PORT 2` in the warning colour, and the
  tester then sends the standard outputs no command but a release.
- Link protocol 4.26: PROG page registers 24 to 27 (`FUNC`) and `FLAGS`
  bits 4 to 7 (port parked). The functions are kept in the store record's
  word before the ports, spare until 4.26, in version 7 and 8 records
  alike. An image of 4.25 writes the word back as 0, and an older one
  reads no version 8 record and starts empty: after such a downgrade and
  an upgrade the ports are on their default functions. With an IOmcu
  older than 4.26 every port is SERVO PWM and OUTPUTS says `IOMCU OLDER
  THAN 4.26: SERVO PWM ONLY`.
- The KST and Chaservo programmer runs on the port SERVO TESTER names,
  PORT 1 to PORT 4, with that port's pin. SERVO > PROGRAM's row names the
  port and pin (`PROG PORT 2 GP14`) and is dimmed `PORT IN USE` while a
  Hitec session opened from the bench console holds the port; the panel
  reads the HITEC page's STATE with the PROG page every 500 ms. With
  OUTPUTS the programmer says `Set SERVO TESTER to PORT 1 to PORT 4 on
  SETUP > OUTPUTS.`
- The HITEC page opens on any of the 4 ports, a NONE port's parked pin
  included, which goes back to NONE at CLOSE.
- A programming port's FUNCTION takes DSHOT150, DSHOT300 and DSHOT600:
  plain DShot from a PIO state machine of its own, a frame every 1 ms. The
  port is a throttle: ARM THROTTLE arms it, the MOTOR & ESC throttle drives
  it, and SAFE, ARM SURFACE, STOP, heartbeat loss and link silence send no
  frame, as on a motor slot. A DShot pin is under no PWM slice rule. The
  servo tester never drives a DShot port.
- Link protocol 4.28: PROG `FUNC` values 2 to 4 (DSHOT150 to DSHOT600)
  and `FLAGS` bits 12 to 15 (port DShot bound). A `CMD` write on a DShot
  port is refused. The store keeps a DShot function as 3 to 5 in the
  `prog_func` word; an image of 4.26 or 4.27 starts such a port at its
  default function. With an IOmcu older than 4.28 the panel offers SERVO
  PWM and NONE only.
- SETUP > SENSORS (German `SENSOREN`), the eighth tile: up to four INA3221
  current monitors at 0x40 to 0x43. Per part `0x40 fitted`; per channel
  `0x40 CH1 watches` (NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY) and
  `0x40 CH1 shunt` (OFF, 5.0 to 1000.0 mOhm). A fitted part's heading shows
  its state: `ONLINE`, `NOT ANSWERING`, `IOMCU OLDER THAN 4.25: NOT READ`
  and eight more. The panel writes the map to SENSE_MAP (link protocol
  4.25) in one write of 26 registers; an IOmcu older than 4.25 gets SENSE's
  one-part frame. The channel read at 1 kHz is the lowest one on a fitted
  part that watches the servo tester's target.
- The band says `INA3221 0x41 has every channel off. Check SETUP >
  SENSORS.` for a fitted part with all three shunts OFF; no map is written
  then.

### Fixed

- SERVO > TARGET reads the set programming port in SAFE within 500 ms after
  a programming session gives the port back and after an IOmcu restart,
  without waiting for the next ARM.
- The bench console's Hitec section says what `flags=PRECHARGE` and the
  `rx=` of `hitec line` each read: register 23, the mode of the next OPEN,
  and the mode of the open line. They differ after register 23 is written
  while a line is open, as the panel does at every edit of SETUP >
  INTERFACES > Hitec D receive, saved or not. It also says that `hitec`
  and `hitec state` print the report with `now`, that an option of
  `send`, `listen` or `power` left out is 0 (`answer` left out answers
  nothing), that a 5-byte D series SEND has its 117.2 us lead with or
  without `lead=`, and gives the raw `send` of a D series read with its sum
  byte.
- A POWER on a PRECHARGE line answers: its LISTEN starts in the pass of the
  ON, while the PD mini's output still reads off, and is cut only when the
  output reads off after it read on, when the supply is asked off, or when
  the PD mini stops answering. It was cut in the first pass, 2.4 ms into
  the window. A LISTEN whose output reads off for its whole window, 30 ms
  at most, ends `DONE` with what it heard.
- SETUP > INTERFACES > Hitec D receive at PRECHARGE reads `PRECHARGE: no
  pull-up on the signal wire` (German `PRECHARGE: kein Pull-up an der
  Signalleitung`); PULL-UP keeps `PULL-UP needs 2.2 kOhm on the signal
  wire`. A mark's line goes before both.
- The D series answer delay through PRECHARGE is given as measured: 5.93
  to 6.52 ms over 153 reads (D485HW, 2026-10-20).
- Downloads from the Wi-Fi page on a home network run without Wi-Fi power
  save and with a TCP (transmission control protocol) send buffer of
  11,520 bytes instead of 5,760. On 0.21.2 a station measured 15,900 and
  about 64,000 B/s; the expected rate is 300,000 to 1,000,000 B/s, not
  measured ([Performance](docs/Performance.md#wi-fi-downloads)).
- A download whose receiver closes, fails or takes nothing for 30 s ends
  at once and resets its connection, and the page answers the next
  request. A receiver that stopped reading held the page's server, and
  with it every other request, until the connection timed out: about
  7.5 min for one that kept its connection open.
- A card put in, taken out or swapped while the panel runs is found by the
  next RESCAN, opening of LOGS, page or OPEN of LOGS, run start or servo
  test start, with no restart. A mounted card is asked with one SEND_STATUS
  (CMD13) command at those events and not once a frame; one that does not
  answer is unmounted and the slot mounted again, so the volume label, the
  list, the run alerts and the card state are the card's in the slot. A
  card swapped for one with no FAT volume said `No card.` on LOGS and kept
  the old label; it says `Card is neither FAT32 nor exFAT. Format it on a
  computer.`. The look logs no E line. A card taken out during a run closes
  the run's file at the first failed write and the run goes on unrecorded
  under `Data can no longer be saved to the card. Recording stopped here.`;
  the next ARM names the slot's state now. A card that answers SEND_STATUS and
  fails a read is unreadable and is mounted once per look. The volume is
  unmounted only with no file or folder open on it from LOGS, the run log or
  the Wi-Fi access. The `runlog` task's stack is 6,656 bytes (5,632), for
  the mount it makes.

### Changed

- The last line of the UPDATES card reads `TESTING also reads testing
  releases. Both offer only 0.22.x.` (German `TESTING liest auch
  Testing-Releases. Beide bieten nur 0.22.x an.`), in place of `TESTING
  also offers testing releases: odd minor numbers.`.
- The four INA3221 rows leave SETUP > INTERFACES (23 settings now). A panel
  updated from 0.21 seeds SENSORS from them at start-up: the part at the old
  address fitted when the INA3221 was ON, its CH1 on the old shunt, CH2 and
  CH3 on it with CH1+2+3 and OFF with CH1, and CH1 watching PORT 1 when the
  servo tester's target was the programming port, OUT 1 otherwise. The old
  keys stay stored and unchanged for one release, for a panel taken back to
  0.21.
- `INA3221 settings rejected. Check SETUP > SENSORS.` names SENSORS in
  place of INTERFACES.
- The servo rail's INA3221 meter is the channel watching the servo
  tester's target, any of the 12, with an IOmcu of protocol 4.25; CH1 of
  the one part SENSE reads with an older IOmcu. SERVO names it over the
  plot, `INA 0x41 CH2`, the TEST card beside STOP TEST likewise, the
  LIMIT RUN line `CURRENT INA3221 0x41 CH2`, and both reports
  `INA3221 0x41 CH2`. Beside the channel's 96 px name the voltage on that
  line has 52 px and shows tenths from 10 V, `26.0 V`.
- The run log's INA3221 columns follow the sensor map with an IOmcu of
  protocol 4.25: one group of current, max and voltage per watching
  channel, named after its target (`port 1 current (A)`, `out 3 max (A)`,
  `supply voltage (V)`), the FAST channel first, at most 3 groups. FAST's
  group comes from the window ring and is on every row; the others from
  SENSE_WIN_A and SENSE_WIN_B, read on every poll after SERVO_WIN at most
  every 40 ms while a part on the page is fitted, and miss about 1 row in
  17 at a 53 ms poll. A fourth watching channel and later are not logged.
  An older IOmcu, or a map derived from SENSE, keeps `ch1` to `ch3`.
- OUTPUTS' PROG PORT entry names the channel watching the port,
  `WATCHED BY INA3221 0x41 CH2`.
- `INA3221 0x40 CH1 clipped at 1.64 A. Reading shows only the limit.`
  names the part's address.
- The IOmcu's sensor schedule reads up to 4 INA3221 at 0x40 to 0x43, 12
  channels, each with its own shunt. One channel, FAST, is read every 1 ms
  and carries the window ring and the move capture; the other channels
  are read in turn beside it, one a tick (500 Hz each for CH2 and CH3 of
  one part, 90.9 Hz each with 11); each bus voltage every 24 ms; each
  part's Mask/Enable and Configuration at 25 Hz. SENSE's one INA3221 is
  the part at its address with CH1 as FAST, so a bench set up today reads
  CH1 as before. The longest tick is 742.5 us of bus time at 400 kHz;
  4 parts, 12 channels, the INA228 and the AS5600 take 60.8 % of the bus.
  At most one INA3221 is probed a tick. The schedule's pair option (CH2
  and CH3 at 1000 Hz), which no link register switched on, is removed.
- LOGS lists its tabs in the order ROOT (German STAMM), USER, BENCH, and
  shows the card's name right-aligned at the end of the header row, with
  the folder after the tabs. The tab ids, the remembered tab and the
  folders are as before.

## 0.21.2 - 2026-10-20

0.21.2 is a testing release: its minor number, 21, is odd, and GitHub lists
it as a pre-release. It mounts SD (Secure Digital) cards formatted exFAT
(extended File Allocation Table) beside FAT12, FAT16 and FAT32, and carries
file sizes in 64 bits. It fixes defects of 0.21.1: DISARM, OUTPUT OFF and a
run's STOP or ABORT act on the first press while the alert band covers them
or a help box is open; a card with no FAT volume reads `not FAT32/exFAT` and
a card that does not mount `unreadable`, not `no card`; the update check on
RELEASES: TESTING reads GitHub's chunked answer, the start-up check
included; OUTPUTS and PICK A PIN show the pins SETUP > INTERFACES holds as
taken; a set-up alert leaves the band once its cause is over; PD mini TX and
RX never share a pin; Backlight leaves SETUP > PANEL.

A panel on 0.21.0 or 0.21.1 is offered 0.21.2 with RELEASES: TESTING on
SETUP > UPDATES only; RELEASES: STABLE reads `releases/latest`, which names
v0.20.0. The steps from 0.21.1 are in
[First run, §1](docs/FirstRun.md#from-0211):

1. SETUP > UPDATES, `RELEASES: TESTING`, `CHECK FOR UPDATES`, with a home
   network kept, the bench SAFE, the supply output off and no log run open.
   0.21.0 and 0.21.1 read only an answer that states its size: a panel that
   says `Unexpected answer from GitHub.` gets the cached answer, which
   does, with `CHECK FOR UPDATES` pressed again at once
   ([the check on 0.21.0 and 0.21.1](docs/FirstRun.md#the-check-on-0210-and-0211)).
2. Hold `HOLD 2 s: DOWNLOAD & INSTALL`. The panel restarts and keeps 0.21.2
   once the display and touch are up.
3. At link-up the panel offers the IOmcu's update over CAN (Controller Area
   Network). Hold START 2 s on the question on the menu, or `HOLD 2 s:
   UPDATE IOMCU` on SETUP > UPDATES. The band then says `IOmcu updated to
   0.21.2.`. The test of unwired sensor pins runs on the IOmcu and comes
   with this step.

The SD card stays as 0.21.1 left it; a card of 0.21.0 and older moves as
[From 0.21.0](docs/FirstRun.md#from-0210) states. The card holds a FAT32,
FAT16, FAT12 or exFAT volume; 0.21.1 does not read exFAT. 0.21.2 does not
read NTFS (New Technology File System), a card with a GPT (GUID partition
table) or a volume over 2 TiB, and formats no card ([The SD card's
format](docs/Screens.md#the-sd-cards-format)). A set-up stored with PD mini
TX and RX on one GPIO (general-purpose input/output) loads as stored, and
the PD mini stays off until one row moves. A bench without a home network
takes the upload on the panel's browser page. 0.21.1 and 0.21.2 run link
protocol 4.23; 0.21.0 runs 4.22, the same major, so both boards link before
and after the IOmcu's update. The time of the download and of the update
over CAN is not measured.

### Added

- The panel mounts SD cards formatted exFAT, the format SDXC cards of 64 GB
  to 2 TB come in, beside FAT12, FAT16 and FAT32. The build takes ESP-IDF's
  fatfs component with `FF_FS_EXFAT` 1 (`firmware/panel/fatfs_exfat.cmake`);
  ESP-IDF v5.4 and v5.5 have no Kconfig option for it. The application image
  grows by 9,184 bytes. The serial log names the format at mount: `storage:
  SK64G mounted, exFAT, 61002 MB`.
- File sizes are 64-bit from the card to the screen: LOGS lists a file of
  4 GiB and more with its size (`8.00 GB`), and WI-FI lists and downloads
  it whole. OPEN refuses a file of 2 GiB and more with `<name> is 2 GB or
  larger. Not opened.`; the reader's seek reaches 2 GiB less 1 byte. GPT
  cards and volumes over 2 TiB do not mount (`FF_LBA64` stays 0).

### Fixed

- WI-FI lists and downloads a FAT32 file of 2 GiB to 4 GiB less 1 byte with
  its size, and the fault log and the servo files read such a file's size
  right. Each took the size from stat(), whose 32-bit `st_size` gave a
  negative number. LOGS refuses such a file at OPEN instead of reading it
  through a seek that does not reach its end.
- A card in the panel's SD slot that holds no FAT volume -- NTFS, a GPT or
  no format -- reads `Card is neither FAT32 nor exFAT. Format it on a
  computer.` on LOGS, `not FAT32/exFAT` on the splash, and its own line in
  the run and servo test alerts and the servo programmer's card notes. A
  card that does not mount for another reason reads `Cannot read the card.`.
  Both read `No card` before. The console line names the FatFs result: `W
  storage: card holds no FAT volume (FatFs 13): format it FAT32 or exFAT`. A
  card that starts and does not mount is started once more to ask FatFs why.
- A press on a control that stops what the bench drives acts on the first
  press while the alert band covers it or a chip's help box is open:
  DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN. The
  press clears the band or closes the box. The alert band covers the lower
  22 px of SERVO's DISARM (panel y 446 to 467); a DISARM tap there only
  cleared the alert, and the bench stayed ARMED until a second tap. A press
  on ARM or OUTPUT ON under the band or the box still starts no hold.
- The update check with RELEASES: TESTING reads GitHub's API (application
  programming interface) answer in chunks (`Transfer-Encoding: chunked`, no
  `Content-Length`), the form GitHub sends when its cache does not hold the
  request. The check ended with "Unexpected answer from GitHub." and "The
  response does not specify a size." whenever the answer came in that form,
  the start-up check included. The console prints each GitHub job's last
  answer: HTTP (Hypertext Transfer Protocol) status, `Content-Length`,
  chunked, body bytes read and the reason code.
- OUTPUTS and PICK A PIN show the pins SETUP > INTERFACES holds as taken
  and take no tick on them: Sensor SDA and SCL (GP16 and GP17 by default)
  while the INA228, the INA3221 or the AS5600 is on, PD mini TX and RX
  while the PD mini is on, the tap pin while the phase tap is on. OUTPUTS
  names `SENSOR BUS`, `PD MINI` or `PHASE TAP` under the pin; PICK A PIN
  draws the key grey and lists the pins under those names in IN USE, after
  the programming port. While the IOmcu answers, the pins are the ones
  each page holds by its last read: a refused move keeps the old pins, a
  refused part holds none. With no IOmcu answering they follow the
  settings. Both screens listed these pins as free, and the IOmcu refused
  a binding on them.
- The settings of AUTOMATIC TEST and LIMIT RUN on SERVO > TEST have CLOSE at
  the top right; it goes back to the run cards, as the home tag does.
- With a band chip's help box open, a tap on another chip closes the box
  and opens that chip's box in one tap. It took two taps.
- An alert about a SETUP > INTERFACES set-up leaves the band once its cause
  is over, before its 30 s: a value the IOmcu rejected, once the IOmcu holds
  the value it is stepped back to (at once when that is the value it still
  holds), and unset sensor pins or both monitors on one address with the
  edit that puts them right. This covers the current monitors, the phase
  tap (Tap gap 2 ms at 400 Hz and back to 3 ms), the PD mini pins (RX on
  the sensor bus's SCL and back) and the PD mini wiring held for a module
  read. On MOTOR & ESC, a rejected pole count or pulse range and an
  inverted Idle pulse and Full pulse leave the band once a later value is
  taken. A rejected alert that never reached the band and is over by its
  next turn is not shown.
- The AS5600 weak, strong or no-magnet alert is said once while the
  magnet status bits stay the same; an edit of another INTERFACES row no
  longer says it again. It is said again when a bit changes (weak to strong
  included), after the field read right, after the sensor was reported not
  answering, after the sensor was switched off, and after a link-up.
- Sensor bus pins with nothing wired to them (no part, no pull-up) no longer
  raise `Sensor bus blocked: SDA low.`: the IOmcu tests a line that is low
  at idle on its own pad pull-up for up to 50 µs, and a line that rises is
  held by nothing. The monitors then read as not answering. Sensor SDA steps
  16 to 14 because GP15 is an SCL pin (an I2C (Inter-Integrated Circuit)
  block's SDA is an even GPIO and its SCL the next one).
- INA228 address and INA3221 address stop at the ends of their range
  (0x40 to 0x4F, 0x40 to 0x43) and have `-` and `+` keys; minus at 0x40
  wrapped to 0x43.
- An SD card mount tried again with no card sets the SPI (Serial Peripheral
  Interface) bus up once and logs `no card` at I (info) level once per try.
  It logged two E (error) lines each try: `spi_bus_initialize(802): SPI bus
  already initialized.` and `sdmmc_card_init failed (0x107).`.
- The link report's `nack` column is documented: it counts each NACK
  (negative acknowledgement) the IOmcu answers, one per set-up value
  rejected from SETUP among them, and a NACK is an answer, not a link fault
  ([Bring-up](docs/Bringup.md#link-report)).
- PD mini TX and PD mini RX on SETUP > INTERFACES step over the GPIO the
  other row holds, so the rows never set both to one pin: with RX on GP18,
  TX steps from GP19 to GP17. At the end of the range the key does nothing
  (TX stops at GP46 with RX on GP47). A set-up stored with both on one
  GPIO loads as stored: the PD mini stays off, the SUPPLY header says
  `PD MINI NO PINS`, and both rows say `TX and RX on one pin. The PD mini
  stays off.`. The rows took one pin for both, and the panel switched the
  supply off for it with the header still naming the pins.
- The bench console build (`IOMCU_CONSOLE`) sends a line longer than the
  free room of the USB (Universal Serial Bus) transmit buffer in pieces, and
  its rest first. With the SDK's (software development kit) 64-byte buffer a
  line of 65 bytes or more never left, and no reply came after it. A pass
  never waits for the host. The IOmcu's own `rcbench-iomcu:` lines go
  through the console's output in this build, so none lands inside a reply,
  and the transmit buffer is 1,024 bytes.
- docs/BenchConsole.md gives a D series read 117.2 us of lead, not 88 us:
  88 us is asked, and the 13 bit times that pad the PIO (programmable
  input/output) stream to a word make it longer; a write gets 89.0 us.
  `lead_ns` in `hitec line` runs from the lead's first edge to the release
  of the line, the lead and every bit time: 559,886 ns for a read, as the
  bench reports.
- The Hitec D series wiring names the tested pull-up: 2.2 kΩ from the signal
  wire to the servo supply, with which a D485HW answers every read. With the
  RP2350 pad's pull-up alone it answers none.

### Removed

- Backlight on SETUP > PANEL. The board switches the backlight only on or
  off, and no part of the firmware applied the setting. The backlight is on
  while the panel runs. A panel that stored the row keeps the old `backlight`
  key in its settings storage; no setting reads or writes it.

## 0.21.1 - 2026-10-19

0.21.1 is a testing release: its minor number, 21, is odd, and GitHub lists
it as a pre-release. It adds the HITEC link page and the bench console,
writes run logs to `BENCH/LOGS` and `USER/LOGS` on the SD (Secure Digital)
card, keeps the bench's own files under `BENCH`, and names the release
kind STABLE. Every panel image, the release images included, looks once at
start-up for a touch injector at I2C (Inter-Integrated Circuit) address
0x14 ([SECURITY](SECURITY.md) states the limitation).

The panel reads no file of 0.21.0 and older from its old place: ESC
profiles come from `BENCH/ESC/CONF`, not `/ESC/`; servo files, `KST.TXT`
and `KST.NEW` from `BENCH/KST/CONF`, not `/KST/`; the fault log is
`BENCH/RCBENCH.LOG`, and `/RCBENCH.LOG` is not read. The files are moved by
hand on a computer. Run logs in the card's root stay readable on the ROOT
tab of LOGS.

A panel on 0.21.0 is offered 0.21.1 with RELEASES: TESTING on SETUP >
UPDATES only. Its other value reads USABLE on 0.21.0 and STABLE from 0.21.1
on; it reads `releases/latest`, which names v0.20.0. The steps are in
[First run, §1](docs/FirstRun.md#from-0210):

1. With the panel off, on a computer: the files of `/ESC/` to
   `/BENCH/ESC/CONF/`, the files of `/KST/` to `/BENCH/KST/CONF/`.
2. SETUP > UPDATES, `RELEASES: TESTING`, `CHECK FOR UPDATES`, with a home
   network kept, the bench SAFE, the supply output off and no log run open.
3. Hold `HOLD 2 s: DOWNLOAD & INSTALL`. The panel restarts and keeps 0.21.1
   once the display and touch are up.
4. At link-up the panel offers the IOmcu's update over CAN (Controller Area
   Network). Hold START 2 s on the question on the menu, or `HOLD 2 s:
   UPDATE IOMCU` on SETUP > UPDATES. The band then says `IOmcu updated to
   0.21.1.`.

A bench without a home network takes the upload on the panel's browser
page. A bench on 0.20.0 loads as for 0.21.0, with the files of 0.21.1
([From 0.20.0](docs/FirstRun.md#from-0200)). The link protocol is 4.23;
0.19.0, 0.20.0 and 0.21.0 run 4.22, the same major, so both boards link
before and after the IOmcu's update. The time of the download and of the
update over CAN is not measured.

### Added

- `protocols/hitec/`: the signal-wire layer for programming Hitec servos,
  both wire families. Classic (HS-5xxx, HS-7xxx: 19 200 bit/s, normal
  polarity, 4-byte commands that sum to 0) and D series (115 200 bit/s,
  inverted, `96`/`69` frames with a byte sum): frame builders, transactions
  to send, listen with an answer to the first byte, or hold a level, and
  reply decoders with one result code per failed check. `rp2350/` holds the
  pin driver: one PIO program of 29 words for both families, 16 cycles a
  bit from the clock divider, open drain or, for the D series, push-pull,
  2 DMA channels. The IOmcu image holds its words to the host test's.
  `test_hitec_wire` and `test_hitec_pio`, 33 cases, run the core and the
  program in a model of the state machine. No byte has been on a wire.
  ([README](protocols/hitec/README.md))
- The HITEC link page (0x34), protocol 4.23: raw Hitec transactions on a
  programming port. OPEN takes the port's pin from PWM after 50 ms and
  opens the line, classic or D series; SEND, LISTEN, LEVEL and POWER run
  one transaction each and leave the capture, up to 16 bytes with their
  start times, and its decoding on the page. Every SEND, and a LISTEN or
  POWER that answers, needs the write key 0x48A5; every staged
  transaction needs its CRC-16. POWER switches the PD mini off, holds it
  off 0 to 10,000 ms and on at 4800 to 6000 mV, and listens from the ON. A
  stop aborts the transaction, keeps the port with the pin released and
  refuses transactions until CLEAR. The panel does not use the page.
  `shared/outputs/hitec_port.c`, `test_hitec_port` and
  `test_hitec_port_stop`, 39 cases.
  ([The programming port for Hitec servos](docs/LinkHitec.md))
- The bench console, IOmcu build option `IOMCU_CONSOLE`, off by default:
  command lines on the USB serial port read and write every link page
  through the link's page handlers, without feeding the link's silence
  watchdog; `hitec` commands stage, check and send HITEC transactions and
  report the capture. CI builds it and fails when the default image holds
  a console symbol. `shared/outputs/page_console.c`, `test_page_console`,
  13 cases. ([The bench console](docs/BenchConsole.md))
- SETUP > SD CARD, a new category tile: Auto logs and User logs, both ON
  by default. OFF writes no file and takes no number. With User logs OFF the
  SERVO screen names `USER LOGS OFF` where a test's files would be.
- SETUP > SD CARD sets the two log folders: Auto log folder (setting
  `log_auto_dir`, `BENCH/LOGS` by default) and User log folder
  (`log_user_dir`, `USER/LOGS`). A tap on a folder's row opens the
  keyboard in its folder mode, where the `.` key types `/`. OK takes 1 or 2
  folders of 1 to 8 characters of A-Z, 0-9, `_` and `-`, stored in upper
  case, and refuses a Windows device name and `BENCH/ESC`, `BENCH/KST` and
  `BENCH/HITEC`; a refused OK keeps the keyboard open and says why. A
  changed folder takes the next file, numbered from what it holds; the
  LOGS tabs USER and BENCH list the folders set. `card_folder_check()` in
  `shared/logfile/card_tree.c`; `test_card_tree`, `test_keypad`,
  `test_settings`, `test_settings_folders`, `test_log_take` and
  `test_logview`, 30 cases more.
- First run §1 gives the load of a 0.21.0 bench in four steps: the files
  moved on the card, the download with RELEASES: TESTING, the install, and
  the IOmcu's update over CAN that follows. The table of the SD card of
  0.21.0 and older names `KST.NEW` beside `KST.TXT`.

### Changed

- Every panel build, release images included, looks once at start-up for a
  touch injector at I2C 0x14: `RCBENCH_TOUCH_INJECTOR` defaults to ON, and
  `-D RCBENCH_TOUCH_INJECTOR=OFF` builds a panel without it. A panel that
  finds none reads and probes 0x14 no more until the next start; touch is
  the glass alone and the band never shows INJECT. The INJECT bubble no
  longer opens with `This is a test-bench build, not a release version.`
  The default image is 3,824 bytes larger, holds 112 bytes less internal
  RAM, and CI builds the option OFF as the extra build.
- The bench's own files live under `BENCH` on the SD card. ESC profiles
  are read from `BENCH/ESC/CONF`, the servo programmer's files and its
  write record `KST.TXT` are in `BENCH/KST/CONF`, and the fault log is
  `BENCH/RCBENCH.LOG`. A save makes each missing folder level. The panel
  reads no folder of 0.21.0 and older and moves nothing: the files of
  `/ESC/` and `/KST/` work again once they are moved to the new folders on a
  computer ([First run](docs/FirstRun.md#the-sd-card-of-0210-and-older)).
  The texts `These numbers are also saved in BENCH/RCBENCH.LOG on the
  card.` and `Cannot save: card write-protected, or BENCH/KST/CONF is not a
  folder.` name the new places.
- The fault log is held to 1 MiB (1,048,576 bytes). A line that would pass
  that renames the file to `BENCH/RCBENCH.OLD`, replacing the one there:
  at most 2 MiB of fault log on the card. It is written whatever SETUP >
  SD CARD says. `shared/logfile/fault_log.c`, `test_fault_log`, 9 cases;
  `shared/logfile/card_tree.c`, `test_card_tree`, 7 cases.
- The runlog task's stack is 5,632 bytes, 1,024 more:
  `tools/stack_check.py` follows the card callbacks of the fault log, the
  servo files and the folder maker (`CARD_CALLS`, `FAULT_CALLS`,
  `KST_CALLS`), and a servo save with its folder levels needs 3,744 bytes.
- Run logs go to two folders of the SD (Secure Digital) card, made one
  level at a time on the first write: `BENCH/LOGS` holds `SERVOnnn.CSV` (an
  arm from SERVO), `MOTORnnn.CSV` (an arm from MOTOR & ESC or ESC STICK) and
  `SUPPLnnn.CSV` (a supply run); `USER/LOGS` holds the AUTOMATIC TEST's
  `ATESTnnn.CSV` and `.TXT` and the LIMIT RUN's `LIMITnnn.TXT`. The numbers
  are one series per folder. The name follows the roles the arm asked for.
  `BENCHnnn`, `ESCnnn` and `SERVOnnn` runs of 0.21.0 and older stay in the
  root, nothing moves them, and LOGS lists them on its ROOT tab. A folder
  that cannot be made says `Cannot make folder BENCH/LOGS. Run not
  recorded.` Firmware 0.21.0 and older does not read the two folders.
- LOGS lists one folder at a time, chosen by three tabs in its header row:
  USER, BENCH and ROOT (STAMM in German). It opens on the tab of the file
  written last when one was written since its last visit, BENCH after a
  start. The header names the card and the folder; an empty folder names
  what goes there or the switch that is OFF.
- The release kind and the RELEASES value are named STABLE, not USABLE:
  `tools/release_kind.py` prints `kind=stable`, SETUP > UPDATES shows
  RELEASES: STABLE | TESTING, English and German. A panel set to USABLE
  keeps its setting, now shown as STABLE (setting `upd_channel` and its
  stored value 0 are unchanged).
- The KST page refuses ENTER, and a REBOOT taken in PWM, with BUSY while
  the HITEC page holds the port.
- The bench configurator describes release 0.21.1 and moves to a release in
  the release pull request, with `rcbench_version.h`. Every repository source
  of its data is a file and line at the tag `v0.21.1`: 83 sources, 9 of them
  on the line the text moved to, in `docs/FirstRun.md`, `docs/Servo.md` and
  `STATUS.md`. Until the tag exists, the data check reads the sources at
  `HEAD` when `CONFIGURATOR_BEFORE_TAG` is 1 and the header there names the
  release of `configurator/package.json`, and `--strict` passes; the
  Configurator workflow sets the variable on its clone, which holds every
  tag. Once the tag exists, it reads them at the tag. A clone without the
  tag and without the variable fails `--strict`. A release tag older than the data is no
  warning, a newer one is. `gitTree()` in `configurator/core/tools/check.ts`;
  one test case in `configurator/core/test/catalogue.test.ts`.
- The bench configurator describes release 0.21.0. Every repository source
  of its data is a file and line at the tag `v0.21.0`: 83 sources, 53 of
  them on the line the text moved to, in `shared/outputs/out_bind.c`,
  `shared/settings/settings.c`, `STATUS.md`, `docs/FirstRun.md`,
  `docs/Servo.md` and `docs/ServoProgrammer.md`. 9 sources name the text
  as the tag words it, IOmcu for coprocessor, ESC STICK on MOTOR & ESC >
  PROGRAM, the alert `Protocol versions do not match. ARMED is blocked.`
  and SERVO TESTER on SETUP > OUTPUTS. The AS5600 board's OUT and GPO,
  the GPO strap and the board's check name build guide stage 4: the rows of
  `docs/Building.md` they cited describe the panel's knob, an MT6701 since
  0.21.0, and are gone.
- The configurator's notes name the panel as 0.21.0 shows it: the
  protocol alert `Protocol versions do not match. ARMED is blocked.`
  (German `Protokollversionen passen nicht zusammen. ARMED ist gesperrt.`),
  SERVO TESTER and PORT PWM on the PROG PORT entry of SETUP > OUTPUTS in
  place of SETTINGS > DUT > PWM TARGET. The German texts take the German
  panel's words: SERVO-TESTER, PROG-PORT and PORT-PWM with the hyphen, EIN
  and AUS, TRIMMUNG, WEG and UMKEHREN, ZENTRIEREN and FREIGEBEN, VERBINDEN,
  HV-SERVO, ANSCHLÜSSE for INTERFACES, and Pulsrate for the frame rate.

### Fixed

- LOGS reads the card each time it opens, from the menu or back from
  WI-FI: the first page, its newest run selected. A run recorded while LOGS
  was closed is on the list without RESCAN. A file's import or plot view
  left open stays open, with the list behind it read again. The card was
  read on the first opening after a start only.

## 0.21.0 - 2026-10-18

0.21.0 is a testing release: its minor number, 21, is odd, and GitHub lists
it as a pre-release.

A bench on 0.20.0 loads the panel over Wi-Fi or over USB (Universal Serial
Bus), and the IOmcu over CAN (Controller Area Network) from the image the
new panel carries. CHECK FOR UPDATES on a 0.20.0 panel does not find
0.21.0: it reads `releases/latest`, which names the newest usable release,
v0.20.0. The upload on the panel's browser page takes it, around three
faults of 0.20.0. The script of its English page does not parse, and the
browser sends the form without the file; the German page takes the
upload. JOIN HOME NETWORK can end with `Join failed: password refused.` on
a correct password; ACCESS POINT joins no network. A panel with Rotary
knob ON saved blanks at the knob's first poll after each start, takes CAN
off the link and can hold the touch controller in reset, so it loads over
USB. The steps are in [First run, §1](docs/FirstRun.md#from-0200):

1. `sha256sum -c SHA256SUMS` prints `OK` for each of the three files.
2. SETUP > APPLICATION, Language `DEUTSCH`, not saved; a panel in German
   skips this. The restart in step 4 starts 0.21.0 in the language saved.
3. With the bench SAFE, the supply output off and no log run open:
   LOGS > WLAN > ACCESS POINT. Join the network the screen names with its
   password, open `http://192.168.4.1/` and enter the access code.
4. Upload `rcbench-panel-v0.21.0.bin`. On the panel, hold `2 s HALTEN:
   INSTALL`. The panel writes the slot it does not run from, restarts and
   keeps 0.21.0 once the display and touch are up. Its settings in NVS
   (non-volatile storage) and its board drawing stay.
5. At link-up the panel finds another build on the IOmcu by SHA-256 (Secure
   Hash Algorithm) and raises `IOmcu runs a different build. Update it
   under SETUP > UPDATES.`. Hold START 2 s on the question on the menu, or
   `HOLD 2 s: UPDATE IOMCU` on SETUP > UPDATES, with the bench SAFE, the
   supply output off and the Wi-Fi radio off. The band then says `IOmcu
   updated to 0.21.0.`.

A panel with Rotary knob ON saved, or a bench without a phone or computer
on Wi-Fi, takes `rcbench-panel-v0.21.0.zip` over USB as for 0.19.0, and
step 5 follows. The parts, `write_flash @flash_args`, keep the settings,
Rotary knob ON included, which 0.21.0 reads from an MT6701 at 0x06;
`rcbench-panel-merged.bin` at 0x0 erases them. A bench on 0.19.0 loads the
panel over USB and the IOmcu over CAN; a new bench and a bench on 0.18.0
or older load both boards over USB. The link protocol stays 4.22, so both
boards link before and after the IOmcu's update. The time of the upload
and of the update over CAN is not measured.

### Added

- SETUP > UPDATES has RELEASES: USABLE | TESTING (the same words in
  German, setting `upd_channel`, USABLE by default), kept with SAVE on
  SETUP. USABLE keeps the check on
  `https://github.com/subtilitas/rcbench/releases/latest`, which never names
  a pre-release. TESTING asks
  `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=10`
  with `Accept: application/vnd.github+json` and
  `X-GitHub-Api-Version: 2022-11-28`, reads `tag_name` and `draft` of the
  10 newest releases by date, testing or usable, and takes the highest
  version among those that are no draft, by a scan that keeps nothing but
  the tag under way and the highest. The answer needs status 200 and a
  `Content-Length` from 1 to 262144 bytes; the 10 newest releases on
  2026-10-18 were 108198 bytes. A `tag_name` that is missing, over 18
  bytes, not a plain JSON (JavaScript Object Notation) string, or not `v`
  and MAJOR.MINOR.PATCH in any of the 10 releases refuses the whole answer,
  each with its own second line under `Unexpected answer from GitHub.` A 403 or 429
  answer is `GitHub's request limit is reached: HTTP 403.`, with no retry.
  The same 30 s budget for the join, the address and the request applies.
  The start-up check and its reminder follow RELEASES. A switch of RELEASES
  hides the last check's result, so a release found on the other setting is
  not offered.
- On both settings a check offers only a release newer than the running
  build: a panel on testing release 0.21.0 with USABLE is not offered
  0.20.0. A running build whose version does not parse is offered nothing
  (`The panel's own version is not MAJOR.MINOR.PATCH: no update offered.`).
- The LOG VIEWER reaches every file on the card in pages of 48, newest
  runs first. PREV and NEXT (German `VORIGE` and `NÄCHSTE`) in the header
  row turn the page; they show only on a card of more than 48 files.
  Pages are cut from the top, so PREV with fewer than 48 entries ahead
  of the page, after a run was recorded, shows the first page. One page
  costs one walk of the card's root directory and holds 48 entries in
  memory. RESCAN reads the page shown again, and a page emptied by DELETE
  or a shrunk card shows the last page left.
- The LOG VIEWER list has a scroll bar 42 px (8.1 mm) wide at the right
  edge. Its thumb is the rows shown out of the rows on the page; a touch
  on the track moves the thumb's middle there, a drag on it scrolls the
  page.
- The LOG VIEWER header names the card by its FAT (File Allocation Table)
  volume label, read once per mount through `f_getlabel`
  (`CONFIG_FATFS_USE_LABEL=y`). A card without a label shows the name the
  card reports of itself, as before.
- The SERVO screen runs the installed-limit search: START LIMIT RUN on
  TEST's LIMIT RUN card, a 2 s hold while ARMED. On a SERVO PWM pin it searches from
  PULSE CENTRE to PULSE MIN and to PULSE MAX; on the programming port it
  reads the KST or Chaservo servo first, in a session of the programmer,
  and searches from the servo's NEUTRAL to its own PULSE LOWER and PULSE
  UPPER, never past them. A servo whose PULSE LOWER to PULSE UPPER lies
  outside the bench's PULSE MIN to PULSE MAX ends the run before anything
  is driven, `servo pulses outside MIN/MAX`, the bench left SAFE and both
  ranges named. The search runs in 10 us steps on INA3221 CH1's 50 ms
  windows, reports both endpoints in a `SERVOnnn.TXT` of its own, and changes
  nothing of the bench's own pulse range. It runs only with the INA3221 as
  the servo rail's meter, on a rail of 3.0 V or more in the last window
  (`SERVO RAIL UNDER 3.0 V OR NOT READ`), and on the bench's outputs only
  with an output bound as SERVO PWM; the line under the button names the
  output it drives and why it cannot start. The search and its protections
  read the current's magnitude, so a shunt fitted the other way round finds
  the same ends. The protections are active: 3.0
  A at every 1 ms sample, above 1.0 A for 400 ms, no window for 200 ms, 40 s
  a search, and a search's window without the rail voltage (`no rail
  voltage in a window`). A window without a current reading is not counted
  as a window. The supply's set points and output are held as at the start
  in every phase but the programmer's sessions.
- SERVO PULSES in the LIMIT RUN card's settings, KEEP or 1000-2000, for a servo on the
  programming port: 1000-2000 writes the servo's PULSE LOWER and PULSE
  UPPER to 1000.0 and 2000.0 us in the run's read session, before anything
  is driven, and reads them back. NEUTRAL is not written. It works with
  SEARCH ONLY and with SEARCH+PROG.
- SEARCH+PROG lowers LEFT ANGLE and RIGHT ANGLE of a KST or Chaservo servo
  on the programming port until neither end binds. The run takes the travel
  as linear in the pulse from NEUTRAL to PULSE LOWER and PULSE UPPER; that
  is not measured. Each bound side gets 2 writes: an overset 3 % above the
  share of travel at the first search's first bound pulse, which keeps the
  stop reachable, and the final value from the share the overset's search
  found free. A side free after the overset ends the run as FAIL, `no stop
  after the overset write`. At most 8 values are written a run. The
  register for each side is found from the run's first write. A later
  session that reads another type, REVERSION, other pulses or another range
  of values ends the run, `servo holds other values`. A run that ends while it waits for ARMED asks SAFE, so an
  ARM not yet taken does not arm the bench. A run's write refused before it
  is sent switches ENABLE WRITE TO SERVO off again. A write that fails once
  sent counts as written, so the box and the report offer RESTORE; a run
  that sent no write reports no backup. During a run the supply log writes
  no row and its time runs on. A run that fails inside a session ends the
  programmer's part of it once its LEAVE is over. A current reading of
  -32768 mA counts as 32768 mA in the protections. Without a PD mini a run
  on PROG PORT needs 4.8 V or more on the servo rail in the INA3221's last
  window, `PROGRAMMING: SERVO RAIL BELOW 4.8 V`. From a servo run's start,
  and from an automatic test's OPEN on, to the close of its file, Wi-Fi
  and an update wait. The FOUND and FREE boxes name the output the run
  started on. A PD mini that answers with its output off at a run's start
  stays off: switched on outside a session it ends the run, `supply or rail
  voltage changed`, and a run on PROG PORT does not start,
  `PROGRAMMING: PD MINI OUTPUT OFF`. A PD mini first heard after the
  start ends the run the same way, its output on or off, and no CONNECT
  goes to a PD mini the run did not start with. A search window
  under 3.0 V, the first one included, ends the run, `supply or rail
  voltage changed`. A run's `Backup:` line names the file
  its own first write saved, also when that write is sent and lost. No
  automatic test or limit run starts until the card has taken the last
  run's file open and close. RESTORE offers `Values
  before the limit run`, LEFT ANGLE, RIGHT ANGLE, PULSE LOWER and PULSE
  UPPER as the run found them; a session after the run saves its own backup
  file at its first write.
- A touch injector for the measurement bench: a Waveshare RP2350-Zero that
  answers as a GT911 at 0x14 and presses where a USB serial command says
  (`testbench/touch-injector`). It starts listen-only, names the line that
  carries SCL, and attaches only after `confirm scl=GP5`. The wiring check
  counts agreement over the transactions that name a line, and needs 20 of
  them and at least half of all; the verdict line reports them as
  `named=`. A panel built with `RCBENCH_TOUCH_INJECTOR=ON` reads 0x14 after
  the glass, merges both with the glass first, shows `INJECT` in the MODE
  chip, and counts a silent injector as lost touch. After 1000 ms without
  an answer it detaches the injector, so the glass alone decides touch and
  the heartbeat returns with the bench SAFE; it reads the product ID once
  every 1000 ms and re-attaches the injector on the first answer, with no
  contacts down. Release images are built without the option.
- The home tag that leaves SETUP for the menu with values unsaved opens a
  warning, `UNSAVED CHANGES`. It lists each value that differs from the
  stored one as `name: stored -> now`, 9 lines and `and N more` past 10
  changes. STAY closes it; DISCARD sets every value back through the path a
  key takes and opens the menu, and is refused while the bench is ARMED;
  SAVE asks for the write and opens the menu. A save asked (`WHEN IDLE`)
  opens no warning; a refused one does. STOP, the tool tiles and every
  screen the panel opens by itself pass without it. After a refused save,
  DISCARD also asks for a write of the values put back. The copy of the
  stored values takes 400 bytes of internal RAM; the heap left with the
  largest Wi-Fi mode up is 91,224 bytes.
- `tools/check_sizes.py --check` holds the files over 1,000 lines to
  `tools/check_sizes.txt`, one reason per file and a line count with a
  tolerance of 100 for six of them. It fails on an unlisted file over
  1,000 lines, a listed file at or under 1,000 and a count outside its
  tolerance. CI runs it with the tree checks.
- SETUP > UPDATES has an IOmcu section under its buttons: the version
  the IOmcu runs, the version of the image the panel carries, a line
  of state and `HOLD 2 s: UPDATE IOMCU`. The 2 s hold starts the
  update the link-up's question starts, also after that question was
  declined, and the progress returns to UPDATES. It takes what the question
  takes: another build, SAFE, the supply output off, the Wi-Fi radio wholly
  off and no panel install. Otherwise it is grey and the line names the
  first of 11 reasons that holds. After an image found on trial is kept,
  the panel compares the IOmcu's build with its own again, and after
  an update of an IOmcu of another protocol major ends, its next
  answer to the 1 s identity probe starts that comparison.
- Releases come in two kinds by the tag's minor number. An odd minor
  number is a testing release (0.21.x, 0.23.x), published as a GitHub
  pre-release and never marked latest; an even minor number is a usable
  release (0.22.x, 0.24.x), published as a full release and marked latest.
  The patch number carries fixes in either kind. `tools/release_kind.py`
  reads the tag in the form the panel's update check reads and prints
  `kind`, `prerelease` and `latest`; it exits 1 on a tag out of form.
  `release.yml` runs it before it waits for CI, which fails a tag out of
  form before anything is built, and again at the publish with the tags
  of the full releases GitHub holds then: a usable tag older than one of
  them is not marked latest, so `releases/latest` never moves back.
  Publish jobs run one at a time across tags.
  [Building](docs/Building.md#releases) states the rule.
- First run §1 names what a bench loads by the release it runs, and gives
  the load of a 0.20.0 bench in five steps: the panel over Wi-Fi through
  the German browser page, or over USB for a panel with Rotary knob ON
  saved, and the IOmcu's update over CAN that follows.

### Changed

- The TARGET row behaves the same on MOTOR & ESC and on SERVO: a tap opens
  SETUP > OUTPUTS on what the workbench drives, and the home tag there
  comes back to the workbench on the task the row was tapped on. From
  MOTOR & ESC OUTPUTS opens on the protocol the throttle's pins are bound
  as (the overview while they are bound as several or none is known),
  from SERVO on what the row shows (the PROG PORT entry while the IOmcu
  drives the port, SERVO PWM while it drives the outputs, the overview
  while the target is not known), not on the TARGET setting; from SETUP's
  tile on the overview. The row keeps its locks: ARMED, a sweep or a run,
  the servo programmer holding the port.
- OUTPUTS lists the IOmcu's programming ports as outputs: its protocol
  list ends in one PROG PORT entry per port the IOmcu reports (1 today, at
  most 4; one while the count is not known; none for an IOmcu older than
  protocol 4.13), its rows 41 px tall instead of
  46 px so 9 rows fit. The entry shows the port's pin lit and read-only
  (`PIN GP13`), what the port carries (`CARRIES SERVO PWM AND KST/CHASERVO
  PROGRAMMING`; no throttle protocol), and on port 1 SERVO TESTER (OUTPUTS
  | PROG PORT) and PORT PWM (ON | OFF) as segments, locked under the
  SERVO screen's rules with the reason under them. These two are the one
  home of the PWM target and Port PWM settings. The servo programmer's
  locked line reads `Set SERVO TESTER to PROG PORT on SETUP > OUTPUTS.`.
  OUTPUTS and PICK A PIN hold every reported port's pin and check the PWM
  slice rule against each, as the IOmcu does.
- SUPPLY is a workbench with the LIVE | SETTINGS strip (German `LIVE`,
  `OPTIONEN`) of MOTOR & ESC and SERVO, two tabs of 112 x 24 px at the top
  left. The SETTINGS button at the top right and the overlay's CLOSE are
  gone: SETTINGS is a tab, its page takes the left column under the strip,
  and the menu's tile opens LIVE. A tab switch closes an open keypad
  unchanged; while the question about a live output is open, the strip
  takes no tap. The header (source, ONLINE or NOT ANSWERING, the range)
  is the row right of the strip and stays in sight on both tasks and under
  the keypad and the question. PLOT | TABLE sit under the strip on LIVE,
  and the rail's four cards are 54 px high from y 38. The title SUPPLY
  SETTINGS (German `NETZTEIL-OPTIONEN`) is removed. A frame of the
  `supply` mode costs 8,034 cache-line fills (8,580 before), a full repaint
  30,543 (31,606).
- The menu tile ANALYSER and the title of its screen read RECEIVER, in
  English and in German. The SETUP category and tile APPLICATION read PANEL
  (German `ANWENDUNG` reads `PANEL`). The identifiers `SCREEN_ANALYSER` and
  `analyser_screen_*`, the render view names and the help page file
  `analyser.md` keep their names.
- The 8 menu tiles carry new lines under the name, 2 rows of at most 23
  characters: MOTOR & ESC `drive, V/A, rpm`, SERVO `travel, current,
  program`, SUPPLY `set and log V/A`, LOGS `runs and files`, RECEIVER
  `S.BUS, PPM, frames`, BATTERY `cells, iR, mAh`, BALANCE `vibration,
  phase`, SETUP `pins, sensors, panel`. The German lines are in
  `docs/Screens-de.md`.
- SETUP > INTERFACES shows its 26 settings under 4 headings: I2C BUS, PHASE
  TAP, BALANCE SENSORS and SUPPLY (German `I2C-BUS`, `PHASENABGRIFF`,
  `BALANCE-SENSOREN`, `NETZTEIL`). A heading is a row of its own, 54 px at
  the 58 px pitch, with no keys; the list is 30 rows long.
- The RP2350 board is named IOmcu (German `der IOmcu`) in the panel texts,
  the help pages, the configurator and the documentation. It was called the
  coprocessor (German `Koprozessor`). Upper-case texts read `IOMCU`, for
  example `IOMCU UPDATE`. Identifiers, file names, CAN page names and the
  entries of released versions keep their wording.
- The alert for an IOmcu on another build names the place of the update:
  `IOmcu runs a different build. Update it under SETUP > UPDATES.` (German
  `IOmcu nutzt einen anderen Build. Update unter SETUP > UPDATES.`). It said
  that an update is available in the menu.
- Band titles follow their tiles: SETUP (was SETTINGS), LOGS (was LOG
  VIEWER) and WI-FI (was FILES & UPDATE), English in both languages.
- SETUP has a WI-FI tile between PICK A PIN and UPDATES, line `NETWORK,
  FILES, UPLOAD` (German `NETZWERK, DATEIEN, UPLOAD`). It opens the WI-FI
  screen, which LOGS' WI-FI button opens too. WI-FI's home tag returns to
  the screen that opened it, and so does CLOSE on PANEL UPDATE after an
  upload's install failed. UPDATES' `Stopped:` line for it reads `Stopped:
  the WI-FI screen is using Wi-Fi.`
- The home tag climbs one level on PROGRAMMER and LOGS, as their BACK
  does: a device page to its protocols, a maker's models to the makers, a
  list to the classes, the plot to the import, the import to the file
  list. Over the RUN warning it is CANCEL, over the manual steps OK, over
  the DELETE question CANCEL. A stick run under way ends as ABORT ends it,
  and its result stays on the page. The home tag leaves PROGRAMMER from
  the classes and LOGS from the file list.
- The band's chips use their 24 px label inset until a row would reach
  x 226, where MOTOR & ESC's home tag ends, the widest. ARMED with a FAULT
  code still reaches under that tag, to x 224, in German x 216.
- UPDATES' hint without a home network reads
  `Save a network under SETUP > WI-FI.` (German
  `Speichere ein Netzwerk unter SETUP > WI-FI.`).
- `ram_budget.py --check-doc` holds the PSRAM .bss table in Performance
  and its German page to the build's map file: each object
  `psram_bss.lf` names, the count, the total and ESP-IDF's own 13,742
  bytes.
- `stack_check.py --iomcu --check-doc` holds the count of calls through a
  register reachable from core 0's `main` in both Performance pages, and
  core 0's sentence in `STATUS.md`.
- Run logs are named by the writer that made them: `SERVOnnn.CSV` and
  `SERVOnnn.TXT` for the automatic servo test and the limit run,
  `ESCnnn.CSV` for a switch of the throttle outputs to ARMED (MOTOR & ESC, a
  stick run on PROGRAMMER), `BENCHnnn.CSV` for a switch of the surfaces to
  ARMED from SERVO and for a SUPPLY run. With an IOmcu older than
  protocol 4.18 every switch to ARMED covers the throttle outputs, so a
  switch to ARMED from SERVO writes `ESCnnn.CSV`. The numbers 001 to 999 are
  one series across the three names: a run takes one above the highest
  number on the card under any name, and a card holding a number 999 takes
  no further run. A card holding only `BENCHnnn` files continues above their
  numbers. The LOG VIEWER lists, pages and deletes all three, newest number
  first across the names, opens on the highest number, and deletes a run's
  report under the run's own name. Two runs with one number, which only a
  card edited on a computer holds, follow by name. Its empty card reads
  `Runs are saved as BENCHnnn, ESCnnn or SERVOnnn.CSV.` Firmware 0.20.0 and
  older numbers runs above the highest `BENCHnnn` file only: going back to
  it on a card written since can give a run a number that an `ESCnnn` or
  `SERVOnnn` file already carries, and nothing is overwritten.
- The LOG VIEWER tab reads `FILES 1 TO 48 OF 200` (`DATEIEN 1 BIS 48 VON
  200`) on a card of more than one page.
- The LOG VIEWER draws the rows of a page newest run first, the order the
  pages run in, where it drew them by name.
- After DELETE the LOG VIEWER keeps the scroll and selects nothing, so
  DELETE and OPEN are grey until a row is chosen; it selected the page's
  newest run, which can be out of sight.
- The panel's directory walk reads a directory through FatFs `f_readdir`
  and takes each file's size from the directory entry. The 48 `stat`
  calls a LOG VIEWER listing made are gone; on a card of 999 runs each of
  them read up to 63 directory sectors.
- OUTPUTS and PICK A PIN open on the overview on every visit: OFF selected,
  every bound pin named by its protocol in full-brightness text. A tap on a
  pin changes nothing and writes nothing until a protocol is picked in the
  list; picking OFF goes back to the overview. The line under the list reads
  `CHOOSE A PROTOCOL, THEN TAP PINS` (German
  `PROTOKOLL WÄHLEN, DANN PIN TIPPEN`). OUTPUTS shows no pin count and PICK
  A PIN no PINS table on OFF. OFF stays selected across leaving the screen
  and read-backs. A change of board, such as an IOmcu that answers or
  is swapped during the visit, selects OFF whatever was selected, so a
  protocol picked before is not carried to it. Opening either screen writes
  nothing to the IOmcu.
- A value stepped back to the one stored is no change: SAVE returns to
  `SAVED`, and a write asked for and not yet taken is dropped. After a
  refused save the label stays `NOT SAVED` or `SAVE` until a save
  succeeds.
- The bench configurator describes release 0.20.0. Every repository source
  of its data is a file and line at the tag `v0.20.0`; 24 sources carry the
  line of that tag, in `STATUS.md`, `docs/Building.md` and
  `docs/FirstRun.md`. Release 0.20.0 adds no bench function.
- The automatic servo test on the INA3221 ends ABORTED after 200 ms without
  a window, `no INA3221 window for 0.2 s`, in place of 500 ms. The limit
  run shares the figure (`SERVO_TEST_WIN_STALE_MS`).
- A servo run's `SERVOnnn.TXT` report counts as an open log run until the
  card has closed it, as its CSV does: Wi-Fi and an update wait for it, and
  the log viewer neither lists nor opens it while it is written.
- The SERVO settings' line that says the setup is not saved spans the
  card: the German text is 296 px wide.
- The panel's texts are reworked: 846 English and 895 German strings change
  wording in `shared/ui/ui_text.def` and `shared/ui/ui_text_de.c`, and one
  string, `MO_THROTTLE`, is new. The pages that quote a string show the new
  wording, and `docs/Language.md` lists the German terms that change, among
  them VERBINDUNG for the link, ZUORDNUNG for a binding, GAS for the
  throttle and FIRMWARE for an image.
- SERVO is a workbench. A strip under the band holds LIVE, TEST, PROGRAM
  and SETTINGS (German `LIVE`, `TEST`, `PROGRAMMIEREN`, `OPTIONEN`), 112 px
  a tab; the tile
  opens LIVE, each task keeps its page for the session, and a tab switch is
  no navigation level. LIVE holds the dial. TEST holds two run cards,
  AUTOMATIC TEST and LIMIT RUN, each with a summary line, its start hold,
  the last result and its file; the card's SETTINGS opens the run's own
  settings one level down, and the home tag closes them. SETTINGS holds the
  pages OUTPUT, SWEEP (CURVE, SPEED, RANGE) and DUT (NAME, REPORT, ENC
  CENTRE, PORT PWM).
- SERVO's TARGET row on the output card reads `protocol pins`
  (`SERVO PWM GP2 GP4`, `PROG PORT GP13`) and is the control that picks
  the target: a tap opens OUTPUTS / PROG PORT. It is locked while ARMED,
  during a sweep or a run, and while the servo programmer holds the port.
  PWM TARGET leaves the DUT page, and AS5600 leaves it for SETUP >
  INTERFACES alone.
- SERVO > PROGRAM lists the programmers from one table,
  `shared/ui/prog_registry.c`, a row each: its name, and what it drives
  with the pin. The one row, `KST and Chaservo servos` (German `KST- und
  Chaservo-Servos`) with `PROG PORT GP13`, opens the KST and Chaservo
  programmer over the left column, one level down. CLOSE or the home tag
  returns to the list; while the port is in programming both open LEAVE
  PROGRAMMING, and over an editor in the programmer the home tag closes the
  editor. A row whose capability the IOmcu does not report is dimmed with
  its reason, `IOMCU HAS NO SERVO PWM`, and opens nothing: with no IOmcu
  answering, the KST row too. A row whose port another row's programmer
  holds reads `PORT IN USE`. A list of more than 9 rows gets SEARCH,
  more than 8 rows shown get group headings, and a list taller than the
  page pages with `^` and `v`. Back on SERVO with the port held, the
  programmer is open on PROGRAM.
- Programming is on the workbenches: ESCs on MOTOR & ESC > PROGRAM, servos
  on SERVO > PROGRAM. The PROGRAMMER tile and its screen are gone. The menu
  is a grid of 4 x 2 tiles of 190 x 204 px by kind: MOTOR & ESC, SERVO,
  SUPPLY and LOGS in the first row, ANALYSER, BATTERY, BALANCE and SETUP in
  the second. A fourth workbench or instrument fails the build at 4
  columns.
- MOTOR & ESC > PROGRAM lists the ESC programmers from the table SERVO's
  list comes from. Its one row, `ESCs by throttle stick` (German `ESCs per
  Gasknüppel`) in the group STICK MENU (German `STICK-MENÜ`), opens ESC
  STICK full width: over the left column, the rail and the tabs, one level
  down. In place of the tabs the strip row holds the output strip, `TARGET
  DSHOT300 GP0 GP1   SUPPLY 7.60 V  1.00 A   SAFE` (German `ZIEL`,
  `NETZTEIL`), at most 97 characters. Opening ESC STICK on a bench that
  is not SAFE switches it to SAFE. The home tag climbs ESC STICK one
  level at a time, then closes it to the list; leaving MOTOR & ESC ends a
  run, switches the bench to SAFE and closes the session.
- MOTOR & ESC is a workbench. The strip holds LIVE, PROGRAM and SETTINGS
  (German `LIVE`, `PROGRAMMIEREN`, `OPTIONEN`), 112 px a tab; the tile opens LIVE, and LIVE keeps
  PLOT or TABLE across a switch. LIVE holds the plot or the table and the
  throttle; PLOT, TABLE, the poll rate and the link's errors sit on a line
  under the strip. SETTINGS holds the 8 ESC / BENCH settings under PACK,
  MOTOR and THROTTLE OUTPUT (German `AKKU`, `MOTOR`, `GASAUSGANG`), each
  set on a keypad. A value is in force at once and asks for a write, which
  the panel makes when the bench is SAFE and idle; the write takes every
  unsaved value, SETUP's included. A line under the rows names the store's
  state: `SAVED`, `SAVE PENDING`, `NOT SAVED` or `SETUP CHANGES NOT SAVED`.
  Idle pulse and Full pulse take a value only on a SAFE bench with ARM not
  held, `Set on a SAFE bench only`. Over the keypad the home tag closes it; otherwise it
  leaves and the bench switches to SAFE. The rotary knob moves the throttle
  on LIVE only.
- ESC / BENCH leaves the SETUP hub, which holds APPLICATION, INTERFACES,
  OUTPUTS, PICK A PIN, WI-FI and UPDATES, with 3 slots free.
- MOTOR & ESC's TARGET row moves from the throttle panel to the top of the
  CONTROL card and names the protocol with the pins, `DSHOT300 GP0 GP1`,
  in a box of 18 characters: `MIXED` for two or more protocols, `NO MOTOR
  PIN` and `PINS NOT READ` in the warning colour. A tap opens OUTPUTS, and
  OUTPUTS' home tag returns to MOTOR & ESC on the task it left. The row is
  locked while the bench is not SAFE, while ARM is held and while the servo
  programmer holds a port.
- SUPPLY's voltage and current caps are no longer set on SERVO. TEST and
  DUT show them read-only, `Caps 21.00 V 5.00 A: SUPPLY > SETTINGS`.
- The limit run's report and box name the output with its protocol,
  `SERVO PWM GP2`. The automatic test's report heads its limit section
  `AGAINST THE PASS/FAIL LIMITS`.

### Removed

- SERVO's own choice OUTPUTS / PROG PORT behind its TARGET row, and the
  PORT PWM row of SETTINGS > DUT; both settings are set on OUTPUTS' PROG
  PORT entry.
- The modelled ESC and SERVO classes of the PROGRAMMER screen, with their
  CONNECT, READ and WRITE on the panel's own copy of a device. Their
  parameter tables for BLHeli_S, AM32, ESCape32, VESC and Hitec D-series
  stay as data (`shared/ui/prog_models.c`); no PROGRAM list has a row for
  them.

### Fixed

- The home tag on SUPPLY, WI-FI, OUTPUTS and PICK A PIN climbs one level a
  tap, as on MOTOR & ESC, SERVO, SETUP and LOGS. On SUPPLY it drops the
  change a question holds, as CANCEL does, then closes a keypad with the
  value unchanged; SETTINGS is a task of its strip, no level, and the tag
  leaves from it as from LIVE. On WI-FI it climbs from a keyboard to the
  network list, keeping nothing typed, and from the list to the buttons;
  over the question about an uploaded image it is CANCEL, and a second tap
  while the image is still there leaves. On OUTPUTS
  and PICK A PIN it closes the open protocol list, choosing nothing. The
  first tap left the screen before, and a typed SSID or password went with
  it. A SUPPLY drag that ends without its finger lifting, at the home tag
  under a second finger or a contact the touch controller loses, asks no
  question.
- A switch to ARMED with the IOmcu connected is refused while the IOmcu
  does not hold the panel's Idle pulse and Full pulse: the CHAN_CFG read
  refused, Idle pulse not below Full pulse, the write refused, or an
  acknowledgement whose throttle channels stored another pair. Each has its
  own text, which names what to change (`No pulse range from the IOmcu.
  Update the IOmcu firmware, then hold ARM again.`, `Idle pulse is not below
  Full pulse. Change one, then hold ARM again.`, `The IOmcu rejected the
  pulse range. Change Idle pulse, Full pulse or OUTPUTS.`, `The IOmcu holds
  another pulse range. Change Idle pulse or Full pulse.`, in German as
  well). The refusal ends at the first payment that finds the pair in force
  after an edit, a binding written on OUTPUTS or PICK A PIN, or a link-up.
  A refused pair ran the throttle on the IOmcu's own range before.
- An Idle pulse or Full pulse edit published while the frame that switches
  to ARMED is on the wire switches the bench to SAFE again in the next
  exchange, with `Pulse range not yet sent to the IOmcu. Hold ARM again.`.
- A switch to ARMED with the IOmcu connected is refused while an Idle pulse
  or Full pulse edit is still owed to it, with `Pulse range not yet sent to
  the IOmcu. Hold ARM again.` (German `Pulsbereich noch nicht an den IOmcu
  gesendet. ARM erneut halten.`). The frame that switches to ARMED carries
  no CHAN_CFG, and an edit reaches the IOmcu at the first poll 300 ms or
  more after it, so a switch that won the race ran the throttle on the
  range held before the edit. The panel asks before CLEAR, writing nothing,
  and again after it. The bench stays SAFE, ARM is held again, and STOP is
  unchanged. A pair the IOmcu acknowledged, or a page that already holds
  it, ends the debt; a read or a write nobody answered keeps it, and a
  link-up owes it again until the first poll pays it.
- Performance and its German page give `log_viewer_screen`'s PSRAM .bss
  as 35,400 bytes and the 17 objects' total as 162,904 bytes, as the
  ESP-IDF v5.4 build places them.
- Performance, its German page and `STATUS.md` give 133 calls through a
  register reachable from core 0's `main`, the figure of the image CI
  builds with arm-none-eabi-gcc 13.2.1; ARM GNU 14.2 gives 142.
- `hardware/docs/IOBoard.md` cites `STATUS.md` by section and item name
  instead of by line, so a coverage table that grows does not move the
  citations. Its capability-word rows state that the panel reads the word
  at bring-up, at each link-up and after a SENSE set-up the IOmcu
  takes.
- SETUP, APPLICATION, Rotary knob ON no longer blanks the panel. 0.20.0's
  knob read an AS5600 at 0x36 on the panel's I2C (Inter-Integrated Circuit)
  bus, and 0x36 is one of the CH422G I/O expander's command addresses: the
  register pointer the knob wrote became the expander's output byte, which
  switched the backlight off, took CAN (Controller Area Network) off the
  link and could hold the touch controller in reset, until a restart. The
  knob is an MT6701 at 0x06: each poll reads 0x03 (angle bits 13 to 6),
  0x04 (bits 5 to 0) and 0x03 again, one byte each with a 5 ms timeout,
  every 10 ms, and every 100 ms while it does not answer. A poll whose two
  0x03 bytes differ is no reading. While the knob has no reference, an
  address-only probe comes first, which the bus driver does not log. A
  clockwise turn seen from the chip's top raises the value on the -STD
  part with DIR at 0; a step is 360/16384 deg. The panel refuses to put the
  knob, or any device of the builder's, at an address the board answers at
  on that bus, 0x14, 0x20 to 0x27, 0x30 to 0x3F and 0x5D, or at the I2C
  reserved 0x00 to 0x03 and 0x78 to 0x7F
  (`shared/bench/include/panel_i2c.h`). With no sensor fitted, Rotary knob
  ON probes 0x06 every 100 ms; in the host tests' bus model that changes
  nothing else, and on the panel it is not measured. The setting's help
  line reads `MT6701 turns the active slider` (German `MT6701 dreht den
  aktiven Regler`).
- A HOLD on the SERVO screen that reaches the control task more than
  500 ms after the far end last took one keeps the release it owes. The
  pause ends there, the screen shows the surfaces released, and the next
  pass writes the range and the centre pulse; a SWEEP asked next waits for
  that centre. The release was dropped on that path: no centre was written,
  a SWEEP asked next started where the surfaces had rested, and a HOLD
  naming a frame rate above the one in force (50 Hz by default) still
  sent its FRAME_HZ write. A control-task pass longer than 500 ms, or HOLD
  repeats left unanswered with the link up, led there. The far end's
  500 ms channel timeout rested the surfaces either way.
- OUTPUTS and PICK A PIN no longer offer a SERVO PWM or MOTOR PWM pin that
  the IOmcu's slice and compare-register rules refuse, which ended in
  `LAST WRITE: REFUSED` with no reason.
  A pin on the programming port's PWM slice (GP28 while the port holds
  GP13, both slice 6) and a pin on the compare register of a pin bound as
  PWM (GP16 beside GP0, and the pairs GP1/17, GP2/18, GP4/20, GP5/21,
  GP6/22) are held: grey, the cell `SLICE OF GP13` or `PWM CH OF GP0`, and
  a tap puts `GP28: PWM SLICE OF PROG PORT GP13` or `GP16: SAME PWM CHANNEL
  AS GP0` under the protocol. The panel decides with the IOmcu's own
  `out_pwm_same_slice()` and `out_pwm_same_compare()`
  (`outbind_pwm_clash()`). DShot and PPM are offered both pins.
- The LOG VIEWER's footer buttons are reachable while an alert shows. They
  sat at body y 392 to 433 on the file list and 390 to 431 on the plot,
  under the alert band (body y 398 to 431), which takes the touches there;
  the import view's ran 16 px into it. Every footer sits at body y 352 to
  393, 4 px above the band. The file list shows 6 rows instead of 7, and the
  plot is 242 px high instead of 276.
- The INJECT mode chip of a panel built with `RCBENCH_TOUCH_INJECTOR=ON`
  opens a bubble with two paragraphs: where touch comes from, the I2C
  address 0x14, how old touch counts, and what a silent injector does
  through the 200 ms, 500 ms and 1000 ms steps to the detach and the probe
  that re-attaches it. It showed the title alone. The released image holds
  neither the mode name nor the text.
- Upload on the English page of the browser access works. The script of
  the page wrote the texts `The file is larger than the panel's slot.` and
  `The upload broke off.` inside single quotes unescaped; the apostrophe
  of the first text ended the string, the script did not parse, the form had
  no handler, and the browser sent the form itself: it went to `/?` and
  the file was dropped. The German page was not affected. The writer
  `wa_out_js()` escapes backslash, `'`, `"`, `<` (as `\x3c`), control
  characters, and U+2028 and U+2029 in every text inside the script. The
  file input is `required`, so Upload with no file chosen shows the
  browser's own message. An answer other than HTTP 200 with an empty body
  (403 stale cookie, 503 access closed) shows `The panel did not take the
  upload: HTTP 403.` (German: `Das Panel hat den Upload nicht angenommen:
  HTTP 403.`) and the progress bar ends on every answer and on a broken
  connection. `test/host/check_wa_page.py` hands every script of both pages
  to `node --check`; the CI host job runs it.
- The start-up check of 0.20.0 can end with `Join failed: password
  refused.` on a correct password, and JOIN HOME NETWORK pressed after it
  joins. One auth-class drop of the station ended a join: ESP-IDF reports
  a wrong password and a key handshake answered late with the same
  reasons, 15 and 204. A join ends as `password refused` after 3
  auth-class drops in a row within 20 s (`WA_AUTH_TRIES`, `WA_JOIN_MS`);
  a single one connects again, and any other drop sets the count back to
  0. A join that reaches 20 s ends as `password refused` when its last
  drop was auth-class, and as `network not found` after any other drop or
  none. The GitHub check, the download and JOIN HOME NETWORK count alike.
  Every drop goes to the serial log as one line with its reason number.
- The panel's picture can sit 17 px to the left, with the tail of each
  line wrapped one line down, until the panel restarts. It happens after a
  frame whose bounce-buffer refill interrupt is held off for more than
  512 us, which a main-flash operation on a scanning panel does for its
  whole length: a settings save, the board drawing's keep, a panel update.
  ESP-IDF v5.4 and v5.5 restart the scan at the first VBlank (vertical
  blanking interval) interrupt after the operation ends, which can be
  mid-frame, through a DMA (direct memory access) link that skips 17 px of
  an LCD (liquid crystal display) FIFO (first-in, first-out buffer) they
  have just emptied. The panel links a copy of that driver, written at
  configure time by `tools/patch_rgb_driver.py`, in which every restart is
  the clean start of boot: the first whole frame after a restart is in
  place. The console logs each restart as
  `display: scan restarted, N since start`.
- `tools/stack_check.py` counts the chain through `cu_port_exchange()` in
  the control task's depth. The IOmcu update's requests go through
  that port, which recovers the bus and then calls `exchange()`; the
  check followed only `port_exchange()` and counted the other call as
  followed (`PORT_CALLS`).
- `tools/stack_check.py` reports each task's deepest chain with no
  function on it twice, whatever the functions' addresses. The check kept
  a depth cut short where a chain met a function already on it, and gave
  that depth to every later caller, so a chain through a cycle read up to
  416 bytes low: `main_task` 5,024 bytes for 5,440, `control_task` 4,032
  for 4,448, `fetch_task` 2,400 for 2,816, `log_task` 2,992 for 3,408. The
  check splits the call graph into its strongly connected components,
  keeps the depth of each function entered from outside its component, and
  tries every way through a component. The `runlog` task's stack is 4,608
  bytes, 256 more: its deepest chain is 80 bytes over the limit of its
  4,352-byte stack.

## 0.20.0 - 2026-10-15

A bench on 0.19.0 loads the panel over USB (Universal Serial Bus) once more,
and only the panel. A 0.19.0 panel takes no image over Wi-Fi: ACCESS POINT
stops with `Wi-Fi did not start.` as it is pressed, and by the estimate of
internal RAM (random-access memory) JOIN HOME NETWORK stops the same way.
The panel takes the parts from `rcbench-panel-v0.20.0.zip` with
`esptool.py --chip esp32s3 -p PORT write_flash @flash_args`, which keeps the
settings in NVS (non-volatile storage) at 0x9000, or
`rcbench-panel-merged.bin` at 0x0, which erases them; a browser flasher such
as the ESP Web Tool writes the merged image. Both writes end with the
application, about 2.1 MB from 0x0, and keep the stored board drawing at
0x7E0000. The coprocessor needs no USB load: at link-up the 0.20.0 panel
finds another build on it by SHA-256 (Secure Hash Algorithm), and the menu
offers the update over CAN (Controller Area Network) from the image the
panel carries, behind a 2 s hold of START, while the bench is SAFE with the
supply output off. The update writes the slot the coprocessor does not run
from, and its bindings stay in the store. For a coprocessor that missed the
USB load of 0.19.0 the band says `coprocessor takes no update over CAN --
load it once over USB`, and it takes `rcbench-iomcu-install.uf2` from
`rcbench-iomcu-v0.20.0.zip`. A new bench and a bench on 0.18.0 or older load
both boards over USB, as for 0.19.0. The steps are in [First run,
§1](docs/FirstRun.md#1-flash-both-boards). From 0.20.0 on, the panel
installs a release over Wi-Fi or downloads it from GitHub on UPDATES. The
link protocol stays 4.22; the coprocessor's sources are those of 0.19.0
apart from the version. The time of the update over CAN is not measured.

### Added

- The bench configurator lists 2 more functions of the base setup: the panel
  update and the card files in a browser over Wi-Fi (the FILES & UPDATE
  screen), and the coprocessor firmware update over CAN (Controller Area
  Network) from the panel. Neither needs a part beyond the panel and the
  coprocessor; the panel update needs no microSD card. The base setup runs
  12 functions.
- Web help for the panel at
  [subtilitas.github.io/rcbench/help/](https://subtilitas.github.io/rcbench/help/),
  in English and German, published to GitHub Pages beside the bench
  configurator. `tools/help_site.py` builds it from `help/content/` and the
  screenshots of `tools/render_ui.py`, with no package outside the Python
  standard library. Its format is in `help/site/FORMAT.md`. The pages carry
  no script and load nothing from other servers. The README in both
  languages and the configurator's footer link to it; the footer link adds
  0.2 kB to the configurator's first load, 249.1 kB of 250.0 kB.
- `tools/help_site.py --check` in CI (continuous integration) fails on a
  screen without a page in either language, a screenshot that
  `render_ui.py` does not render or that has no PNG (Portable Network
  Graphics) file in the page's language, an English and a German page whose
  headings or screenshots differ, a link to a missing page or heading, and
  Markdown outside the subset.
- SETUP has an UPDATES tile, which opens the UPDATES screen. CHECK FOR UPDATES joins the home network kept
  on FILES & UPDATE, sends one HTTPS (hypertext transfer protocol over
  transport layer security) request to
  `https://github.com/subtilitas/rcbench/releases/latest`, reads the newest
  release's tag from the redirect's `Location` header and
  switches the radio off again, within 30 s for the join, the address and
  the request. The card then says `Version 0.20.0 is available.`,
  `Up to date: this panel runs 0.19.0.` or why the check failed. The tag is
  read strictly: `v`, then MAJOR.MINOR.PATCH, digits only, each 0 to 65535
  in at most 5 digits without a leading zero.
- REMINDER AT START (setting `upd_remind`, on by default) runs the same
  check once after each start, at the first moment the bench is SAFE with
  the supply output off, no log run open, FILES & UPDATE closed and no
  coprocessor update offered or running, and within 10 min of the start.
  When it finds a newer release, a reminder opens on the menu with IGNORE
  NOW (until the next start), NEVER REMIND ME (switches the setting off and
  saves it) and HOLD 2 s: DOWNLOAD & INSTALL. It waits while the bench is
  ARMED, the supply output is on, or the coprocessor's update question or
  progress is up.
- HOLD 2 s: DOWNLOAD & INSTALL, on UPDATES and on the reminder, downloads
  `SHA256SUMS` and `rcbench-panel-vX.Y.Z.bin` of the release found over
  HTTPS into PSRAM (pseudo-static random-access memory), following at most
  3 redirects and only to `github.com`, `release-assets.githubusercontent.com`
  and `objects.githubusercontent.com`. The image's SHA-256 must equal its
  `SHA256SUMS` line and the image passes every check an uploaded one does;
  then the install runs as INSTALL runs it. The download takes 180 s at
  most from the hold, ends on CANCEL, STOP, ARMED, the supply output, a log
  run or a board drawing being fetched, and writes nothing before every
  check has passed. A download and the coprocessor's update exclude each
  other as an install and the update do.
- The panel checks HTTPS servers against ESP-IDF's (Espressif
  Internet-of-Things Development Framework) bundle of common certificate
  authorities (16,984 bytes), with mbedTLS's memory in PSRAM. The panel
  image grows by 127,856 bytes, and the free static internal RAM goes from
  42,806 to 40,174 bytes. How long the check and the download take on a real
  network is not measured.
- A radio start that fails logs the step, the error's name and the free
  internal heap with its largest block. The panel logs the free internal
  heap, its largest block and the free PSRAM once at start-up.
- `tools/ram_budget.py` estimates the internal heap left with each Wi-Fi
  mode up from the panel build, the panel's task stacks, queues and
  buffers and three allowances, and fails under 32,768 bytes. CI runs it
  after both panel builds; on ESP-IDF v5.4 it holds the table in
  [Performance](docs/Performance.md#internal-ram).
- First run §1 names what a bench loads over USB by the release it runs,
  and gives the panel-only load of a 0.19.0 bench in four steps, with what
  each panel flash keeps and the coprocessor's update over CAN that
  follows.

### Changed

- SETUP opens on a page of 3 by 3 tiles in the menu's style: ESC / BENCH,
  APPLICATION and INTERFACES with their number of settings, and OUTPUTS,
  PICK A PIN and UPDATES with a line each; 3 slots are free. A category's
  list spans 776 px of the panel's 800 and shows 6 rows, over a strip with
  the category's name, RESET CATEGORY and SAVE that ends 4 px above the
  alert band. The home tag climbs one level: from a list
  to the tiles, from OUTPUTS, PICK A PIN and UPDATES to the tiles, from the
  tiles to the menu. SAVE shows on the tiles as on every list. The texts
  `PINS: A LIST OR THE BOARD` and the UPDATES key are gone; the tiles add
  `PROTOCOLS AND PINS`, `ON THE BOARD DRAWING` and `RELEASES ON GITHUB`.
- Every menu tile's icon takes the centre of its box, over the tile's
  name. On 0.19.0 the icons of MOTOR & ESC, SERVO, ANALYSER, LOGS, SETUP
  and PROGRAMMER sit 15 px right of the name, and those of SUPPLY, BATTERY
  and BALANCE 15 px higher than the others.
- Screens describes the menu's MODELLED mark per tile, in English and
  German. The mark follows the capability bits the coprocessor reports and
  the sensors enabled on SETUP under INTERFACES; it does not classify each
  value. MOTOR & ESC goes without it when the coprocessor drives ESCs (electronic speed controllers) and
  holds an INA228 set-up enabled on SETUP, SERVO when it drives servo PWM
  (pulse-width modulation) outputs, SUPPLY when the PD mini is enabled.
  ANALYSER, BATTERY, BALANCE and PROGRAMMER carry it in this release; LOGS
  and SETUP never do. With no coprocessor answering, MOTOR & ESC and SERVO
  carry it too.
- The bench configurator describes release 0.19.0. Every repository source
  of its data is a file and line at the tag `v0.19.0`; 23 sources carry the
  line of that tag, in `STATUS.md`, `docs/Building.md` and
  `docs/FirstRun.md`.

### Fixed

- ACCESS POINT fails on 0.19.0 with "Wi-Fi did not start." the moment it
  is pressed: internal RAM (random-access memory) cannot hold the Wi-Fi
  driver's static buffers, 51,200 bytes of DMA-capable (direct memory
  access) memory that `esp_wifi_init()` takes. By the same estimate a home
  network, the network search and the release check fail the same way.
  The zero-initialised data of 15 screen, card and page objects, 161,340
  bytes, and that of lwIP (lightweight IP stack) and the Wi-Fi driver's
  libraries, 13,742 bytes, move to PSRAM. The internal heap at start goes
  from 126,768 to 299,320 bytes, and the heap left with the largest Wi-Fi
  mode up from an estimated -81,168 to 91,384 bytes. The free heap on a
  panel and the frame rate with the screens' state in PSRAM are not
  measured.

## 0.19.0 - 2026-10-15

Load both boards over USB (Universal Serial Bus) once, on every bench: a
new one and one that runs 0.18.0 or older. The coprocessor takes
`rcbench-iomcu-install.uf2` from `rcbench-iomcu-v0.19.0.zip` on its
`RP2350` drive in BOOTSEL mode: the partition table and the image in slot
A. The panel takes the parts from `rcbench-panel-v0.19.0.zip` with
`esptool.py --chip esp32s3 -p PORT write_flash @flash_args`, which keeps
the settings in NVS (non-volatile storage) at 0x9000, or
`rcbench-panel-merged.bin` at 0x0, which erases them. The panel's stored
board drawing is lost once and fetched again at the next link-up. The
coprocessor keeps its bindings; on an RP2350 of stepping A2 the load can
erase the old store's second sector at 0x3FF000. The steps are in
[First run, §1](docs/FirstRun.md#1-flash-both-boards). After this load the
panel installs a release over Wi-Fi from `rcbench-panel-vX.Y.Z.bin`, and the
coprocessor takes its image from the panel over CAN (Controller Area
Network). The link protocol is 4.22, and a 0.18 board still links with a
0.19 one: a 0.19 panel shows `coprocessor takes no update over CAN -- load
it once over USB` for a 0.18 coprocessor, and a 0.18 panel never asks a
0.19 coprocessor's update pages. The coprocessor's flash has two image slots
of 2036 KiB and the output store at 0x3FC000. A panel built with
`RCBENCH_IOMCU_BIN`, as the release is, carries the coprocessor image of its
build, compares builds by SHA-256 at link-up and offers the update behind a
2 s hold of START: 64-byte blocks with a CRC-16 (cyclic redundancy check)
each, a SHA-256 over the image, a trial start the panel keeps with BUY, and
the old image back after 16.7 s without it. The panel's flash has two
application slots of 4,063,232 bytes. LOGS → WI-FI opens FILES & UPDATE,
which runs an access point or joins a home network (WPA2 or WPA3 personal)
behind an 8-character access code, takes an uploaded panel image, checks
it and installs it on a 2 s hold of INSTALL on the panel, and starts the new
build on trial with a return to the build before. The same page lists the
files on the card, sends each one and sends the whole card as one stored
ZIP. The radio runs only while that screen is open on a bench in SAFE with
the supply output off and no log run, and goes off after 10 min without a
request. A panel install and the coprocessor's update exclude each other.
The coprocessor counts the CAN frames it refuses (protocol 4.21, STATUS
registers 8 and 9), and MOTOR & ESC (electronic speed controller) shows the
count above 0. A tap on a chip of the status band opens a help bubble for
that chip. The time of a whole update over CAN is not measured (the bus
time of its 26,946 exchanges is 9.0 to 37.7 s for the 0.18 image), nor are
the transfer rate of the Wi-Fi page and the duration of a panel install's
erase and write.

### Added

- The coprocessor counts the CAN frames it receives and refuses to decode:
  a receive buffer with an 11-bit identifier, a remote frame or a DLC above
  8, and a frame the link decode refuses (an unknown op, a count past a
  page, a DLC that disagrees with the count). Protocol 4.21: STATUS (0x01)
  has 10 registers, the count in registers 8 and 9, low half first, 32 bits,
  0 at power-up and wrapping at 2^32. The panel reads 10 STATUS registers
  from a 4.21 coprocessor and 8 from an older one, whose count reads 0.
  MOTOR & ESC shows `Link frames refused: N` (German `Abgelehnte
  Link-Frames: N`) in its header strip after the error count, while the link
  is up and N is above 0; the bring-up log line `iomcu` adds `refused N`.
- The panel updates the coprocessor's firmware over CAN. Protocol 4.22 adds
  UPDATE_CTL (0xF0) and UPDATE_DATA (0xF1); with registers 0 to 6 of
  IDENTITY they are frozen and answer under every later protocol major. The
  coprocessor's flash carries a boot ROM partition table: slots A and B of
  2036 KiB, the output store in its own 8 KiB data partition at 0x3FC000, the
  last 8 KiB unused. The panel sends 64-byte blocks, each committed with a
  CRC-16, checks a SHA-256 over the image, restarts the coprocessor into the
  new slot on trial and keeps it with BUY; without the BUY the boot ROM
  returns to the old image after 16.7 s. The coprocessor refuses ARM for the
  whole update and writes flash only while the bank is not driving, the
  supply's output is off and no servo programming session runs. A session
  ends after 5 s without a request to UPDATE_CTL or UPDATE_DATA, and a panel
  that finds one at its check ends it with ABORT. After the BUY the
  coprocessor erases the old slot's first sector and retries the erase once
  a second until it holds; `SLOT` bit 9 says it is owed. A panel
  built with `RCBENCH_IOMCU_BIN` carries the coprocessor image of its own
  build, compares builds by SHA-256 at link-up and offers the update on a
  question that START takes with the 2 s hold, an older image included; a
  progress screen without band follows. New texts: the question, the five
  progress steps, three notes and the alerts `coprocessor runs another build
  -- the menu offers its update`, `coprocessor takes no update over CAN --
  load it once over USB`, `coprocessor updated to %u.%u.%u`, `coprocessor's
  new image kept` and `coprocessor update stopped: %s -- the old image runs`.
- A tap on a chip of the status band opens a help bubble under the band
  that says what the chip's state means for the bench and what to do: LINK
  or NO LINK, BENCH or SIM, FAULT, SAFE or ARMED and the run clock. The
  FAULT bubble has one paragraph per set bit of the code, names the four
  bits this release never sets, and gives the bits the panel has no name
  for as a code. The LINK bubble ends with `Link frames refused: N` while
  the link is up and N is above 0. The text follows the state while the
  bubble is open. Any tap closes it and does nothing else; a press on STOP
  closes it and stops at once. Leaving the screen, or the chip going,
  closes it. German text in `ui_text_de.c`.
- The panel updates its own firmware over Wi-Fi. LOGS → WI-FI opens FILES &
  UPDATE, which starts the radio as an access point (2.4 GHz, channel 6,
  WPA2, `rcbench-` and 4 hex digits, an 8-character password new at every
  start, 1 client, `http://192.168.4.1/`) or joins a home network (WPA2 or
  WPA3 personal) chosen from a scan of up to 16 networks or typed as a
  hidden network on the panel's keyboard. A browser uploads
  `rcbench-panel-vX.Y.Z.bin` into PSRAM; the panel checks the whole image
  (ESP32-S3 image header, project name, version, appended SHA-256, size
  against the slot) and asks with both versions, saying when the image is
  older. INSTALL is a 2 s hold on the panel; the browser cannot start it. The
  write switches the radio off, withholds the heartbeat so the coprocessor
  holds its outputs in failsafe, shows the bytes and the percentage on a
  full screen without band and restarts the panel. The new build's first
  start is a trial: it is kept once display and touch are up, otherwise the
  build before starts again and says so on the band. A failed start is
  never marked valid, also when marking it invalid fails; a good start
  whose otadata write fails says on the band that the next start goes back.
- The same page lists every file on the card with its size in bytes and its
  modification time, sends each one, and sends the whole card as one stored
  ZIP file (ZIP64 over 4 GiB − 1 B or 65,535 entries) with its size shown
  before the click. The page reads the card only.
- An 8-character access code, new at every start of the radio, guards the
  page; 16 wrong codes switch the radio off. ACCESS CODE switches it off
  for both ways in; the screen then says the page is open to every device
  on the network.
- The radio runs only while FILES & UPDATE is open, with the bench SAFE,
  the supply output off and no log run open. It is off at every start and
  goes off on ARMED, STOP, the supply output, a log run, 10 min without a
  request and before an install writes the flash. Wi-Fi that cannot be set
  up at start leaves the screen grey with a line saying so.
- A change of the kept Wi-Fi settings that NVS does not take stays owed, is
  tried again 3 times 10 s apart and shows `Not saved: the flash write
  failed.` until a write succeeds. A name responder (mDNS) that fails to
  start is tried again 3 times 5 s apart in each join.
- The panel's keyboard has a Wi-Fi mode: lower case, upper case (`ABC`) and
  the 32 ASCII marks (`#+=`), 8 to 63 characters for a password, 1 to 32
  bytes for a network name, the password shown as dots until SHOW.
- Each release carries the panel application on its own,
  `rcbench-panel-vX.Y.Z.bin`, covered by `SHA256SUMS`, for the upload.
- A panel install and the coprocessor's update exclude each other. INSTALL
  waits while the coprocessor's update or the BUY of an image on trial runs
  (`Install waits: the coprocessor's update runs.`, German `Installation
  wartet: das Update des Koprozessors läuft.`). The coprocessor's question
  opens only with the panel's radio wholly off and no install under way; a
  START given while either holds is spent and the question opens again.
- First run §1 lists the four release assets and gives the USB load of both
  boards from them in four steps, with what each panel flash keeps and
  loses.

### Changed

- The panel's flash has two application slots of 4,063,232 bytes, `ota_0`
  at 0x20000 and `ota_1` at 0x400000, and `otadata` at 0x10000. The 1 MiB
  `storage` partition is gone, and `boardart` is 128 KiB at 0x7E0000 with
  8 slots of 16 KiB. `nvs` and `phy_init` keep their offsets. A panel
  flashed with an earlier release needs one flash over USB to reach this
  layout; the parts keep the settings, the merged image at 0x0 erases them,
  and the stored board drawing is fetched again
  ([First run, §1](docs/FirstRun.md#1-flash-both-boards)).
- The panel's application version (`esp_app_desc_t.version`) is the release
  number from `shared/link/include/rcbench_version.h`.
- The panel's bootloader is built with application rollback.
- A coprocessor loaded with protocol 4.21 or older has no partition table
  and takes one USB load of `rcbench-iomcu-install.uf2` (table and slot A).
  Bindings are read from the old store at 0x3FE000 until the first save. On
  an RP2350 of stepping A2 the load can erase 0x3FF000, the old store's
  second sector. Later coprocessor images come over CAN. See
  [First run](docs/FirstRun.md#the-iomcus-partition-table).
- The bench configurator describes release 0.18.0. Every repository source
  of its data is a file and line at the tag `v0.18.0`; 28 sources carry the
  line of that tag. The sources of the logger, of the arm decision and of the
  protocol mismatch alert carry the wording of that tag, which names the
  switch to ARMED, and the alert is quoted as `protocol mismatch -- will not
  switch to ARMED`.
- The configurator states the cap of the PD mini set point as 0.7 V under the
  input voltage of the module, in the texts of the supply, the ESC power
  safety note, the wire from the T-plug, both PD power sources and the 20 V
  charger row of the parts list. The parts list row of the PD mini Buck
  version keeps the 0.5 V of the vendor.

### Fixed

- Two SETUP hints fit their 36 cells: "For the remaining-charge estimate"
  and "If the ESC reports none; 0 = empty". The LOGS import range shows no
  decimal from 1000 in magnitude, 999.95 included, which one decimal rounds
  up to 1000.0, and has a space before the unit. A range wider than the 17
  glyphs of its 140 px box drops the decimal, then that space, so two
  5-digit negative ends with a 3-letter unit fit; a wider range is cut at
  the box's edge. `render_ui.py` lists no known overflow.
- The configurator keeps a tick that Tab reaches during a smooth scroll in
  view with its help bubble: the focus stops the running scroll and scrolls
  the bubble and the row into view at once.

## 0.18.0 - 2026-10-14

The link protocol is 4.20. A 0.17 board still links and arms with a 0.18
one, since only the protocol's major number has to match, and an arm then
drives every output: a 0.18 panel writes ARM 1 to a coprocessor older than
4.18, and a 0.17 panel writes 1 to a 0.18 coprocessor. The arm by screen
role needs both boards at 0.18. The SERVO frame rate on the PWM
(pulse-width modulation) target alone and the refusal of a pulse longer
than its frame need a 0.18 coprocessor. A 0.18 panel draws the board of a
0.17 coprocessor from its shape, and a 0.17 panel reads neither the drawing
a 0.18 coprocessor serves nor a version 2 record of an interrupted servo
write. Flash both boards. PICK A PIN draws the coprocessor's board from a
drawing the coprocessor serves: the outline, the parts and every pad in
0.01 mm, 1,560 bytes with 40 pads and 102 shapes for the RP2350-CAN, checked
by the panel before it is shown. The board photograph is removed, and the
coprocessor image is 203,808 bytes smaller (184,360 bytes). PICK A PIN has
the protocol list of the Outputs screen, the count of the 8 slots in use and
a PAD column, and shows the programming port's pin (GP13 by default) as
taken. An arm drives only the outputs of the screen's role: MOTOR & ESC
(electronic speed controller) and a PROGRAMMER stick run arm the throttle
channels, SERVO the surfaces and programming ports. The band reads ARMED
MOTOR or ARMED SERVO, and an arm for the other role switches to SAFE first
and arms after 100 ms. The screens that drive an output name its pins:
`TARGET: GP0 GP1` on MOTOR & ESC, the TARGET row on SERVO, `PD MINI TX GP12
RX GP13` on SUPPLY. RUN on ESC STICK is refused while no pin is bound as a
throttle, and leaving SUPPLY, SERVO or ESC STICK switches off a PD mini
output that screen switched on. The coprocessor refuses a PWM endpoint
longer than the frame (1999 µs at 500 Hz) and a slew above 3000 (300 %/s),
and its TONE page answers BUSY (6) for a pin in use. The supply state shows
no valid reading after 1.5 s without a new one, and an output that is on
goes off after 3 s. A PD mini set point is capped 700 mV under the module's
input, 200 mV above the line a live output is cut at. The panel build stops
on ESP-IDF (Espressif IoT Development Framework) v5.5.0 and v5.5.1: their
TWAI (two-wire automotive interface) driver sends a CAN (Controller Area
Network) frame of DLC (data length code) 0 as DLC 8, and a panel built there
never links. CI (continuous integration) builds v5.4 and v5.5.3. The bench
states read SAFE and ARMED in English and German. The bench log and the
supply log end in a column `source` (`measured` or `model`), and a last row
without a line feed is left out. The ESC and SERVO classes of PROGRAMMER
carry the MODELLED mark. The bench configurator adds the function "Program
a KST or Chaservo servo", a list of the parts to buy with a "Have it" tick
per row, a help bubble for each function and part in English and German,
the heat per resistor of the motor simulator, and the 7 to 12 V DC (direct
current) supply of the Osoyoo breakout board, and it keeps the selection and
the language in the browser. The reply time of a real PD mini module, the
voltage drop on a real source and the level of the servo's reply on GP13
are not measured; the current the Osoyoo board's DC supply must deliver is
not stated.

### Added

- PICK A PIN draws the coprocessor's board from a drawing the coprocessor
  serves: the outline, the parts as flat shapes and every pad, to scale in
  0.01 mm on the board's real pad grid. Protocol 4.20: ARTWORK (0x26) has 9
  registers; format 2 (`LINK_ART_DRAWING`) is the drawing laid out under
  `LINK_DRAW_*` in `link_pages.h`, and registers 7 and 8 carry an FNV-1a
  hash of the payload next to its CRC. A new board brings its drawing,
  coordinates and pad kinds in its own coprocessor image. The panel checks
  the drawing (`link_drawing_parse()`) and its agreement with the board's
  shape, catalogue and pads (`board_drawing_agrees()`) before it shows it,
  and draws the board from its shape otherwise. The kept copy in the
  `boardart` partition is keyed on the board ID, format, outline, length,
  CRC and hash, so a coprocessor flashed with another drawing for the same
  board is fetched again. A 4.19 or older coprocessor is drawn from its
  shape.
- `tools/gen_board_drawing.py` writes
  `firmware/iomcu/src/board_drawing_rp2350_can.c` (1,560 bytes, 40 pads,
  102 shapes) from the configurator's `configurator/drawings/panel.json` and
  refuses a drawing the panel would refuse; CI runs it with `--check`.
  `npm run drawings` writes `panel.json` and `npm run drawings:check`
  compares it. The configurator's SVG drawings are unchanged.
- Configurator: the card of the motor simulator calculates the heat per
  resistor. Inputs: the LiPo pack (1S to 6S at 4.2 V per cell, default 4S,
  16.8 V), the resistance (the 21 E12 values from 100 Ω to 4.7 kΩ, default
  470 Ω), the rating (0.25, 0.5, 1, 2, 3 or 5 W, default 2 W) and the wiring
  (delta, the default, or star). The load case is 1 commutation step held at
  the full pack voltage V: in delta 1 resistor takes V²/R and 2 take V²/(4R),
  in star 2 take V²/(4R) and 1 none. The card shows current in mA, power in
  W and the share of the rating, a warning above the rating and a hint from
  50 % of it. The current limit of the ESC is not modelled. The link key `m`
  stores the values that differ from the default; a link without it opens
  the default values. The name of the part, its row in the parts list, the
  text and CSV copies and the PDF follow the values; a change of the
  resistance or the rating takes the "have it" tick off that row. The
  drawing of the motor simulator shows no colour bands and names no
  resistance.
- PICK A PIN has the protocol list of the Outputs screen, top left. It names
  the selected protocol; a pick selects it on both screens and writes
  nothing. Under it stand `N OF 8 SLOTS IN USE`, counted over every
  protocol, and in amber the reason no pin can be added, in the Outputs
  screen's words (`ALL 8 SLOTS IN USE`, `NEEDS 8 CHANNELS, 4 FREE`,
  `SERVO PWM TAKES 8 PINS`). The PINS table has a PAD column with the pad
  number printed on the board. Free keys are ivory in the row nearer the
  board and ebony in the row beyond it; a ground is a 36 px white round
  marker with a large G, 40 px in from its pad; a rail is a 32 px amber round
  marker with its voltage (5V0, 3V3, PWR), 30 px or 78 px in, alternating;
  RUN and 3V3_EN carry a dim cross. `ui_choice_face()`, `ui_choice_list()`
  and `ui_choice_row()` draw the protocol list on both screens. OFF picked
  on PICK A PIN ends when the screen is left, as on the Outputs screen
  (`outputs_screen_end_off()`). IN USE lists every pin: when 28 px pin rows
  run past the table's foot, as with OFF selected, the programming port
  listed and 8 pins over 6 protocols, the pin rows are 20 px;
  `picker_screen_in_use()` returns each line and its box.
- Configurator: every function with a tick and every part under "Parts you
  own" has a help bubble in English and German. It says what the item does
  and what it needs: the bench parts, the test object and the variant the
  resolver adds for it, and for a part its supply wires, logic and power.
  The bubble opens on the keyboard focus of the tick and on the "i" mark at
  the end of the row; a tap on the mark opens it without changing the tick.
  Escape, the loss of the focus and a press elsewhere close it. One bubble
  shows at a time. It opens under the row, or over it when the room under
  the row in the viewport and the scrolling chooser is too small and the
  room over it holds the whole bubble. `core/help.ts` holds the text of the
  12 functions; `tools/check.ts` checks it.
- The bench log and the supply log end in a column `source`: `measured` for a
  row a meter or the PD mini produced, `model` for a row the panel's model
  produced (`LINK_BN_SIMULATED`, `supply_state_t.modelled`). A file without
  the column is read as before; the column is text and is never plotted. The
  servo test's CSV is unchanged: its `meter` column names `MODEL`.

- The MOTOR & ESC throttle panel names the pins the throttle drives
  (`TARGET: GP0 GP1`), from the output pages the coprocessor's throttle role
  reads. No pin bound as a motor and a binding not read show in the warning
  colour. The SUPPLY header names the PD mini's UART pins
  (`PD MINI TX GP12 RX GP13`; `PD MINI NO PINS` when either is not set).
- `ui_pinlist()` formats a GPIO mask as a pin list within a cell budget:
  the pins that fit, then the count of the rest (`GP2 GP4 GP5 +2`).
  `outputs_role_pins()` returns the GPIOs of the slots whose channels carry a
  role.
- The DShot module exports the extended telemetry units: `DSHOT_EDT_MV_PER_COUNT`
  (250), `DSHOT_EDT_A_PER_COUNT` (1), `DSHOT_EDT_DEGC_PER_COUNT` (1) and
  `dshot_edt_millivolts()`, `dshot_edt_amps()`, `dshot_edt_celsius()`. The
  coprocessor firmware reads its ESC voltage, current and temperature through
  them.

- `CONTRIBUTING.md`, section "The active output": every screen, tab,
  overlay, prompt and automatic run that drives an output names the pin,
  port or supply it drives, from the state the drive path reads, with a
  warning when nothing is bound. A screen added or changed meets it with a
  host test; `STATUS.md` lists the screens that do not meet it.
- `tools/security_parsers.py --check`, run in CI, fails when the parser table
  in `SECURITY.md` and the parse functions in the public headers differ. The
  table holds the modules the name rule finds plus the forced ones in the
  tool, `shared/settings` (values and texts from NVS, non-volatile storage)
  and `shared/touch` (GT911 point list): 19 modules; it listed 4. The check also finds functions named
  `*_write`, `*_read`, `*_done` and `*_utf8_next`; `NOT_UNTRUSTED` is empty.
  A forced entry that names a missing module or one the name rule finds
  fails the check.
- `mutation.yml` runs every mutant of `shared/safety` and `shared/outputs`
  each Sunday at 03:17 UTC and on demand. A survivor does not fail it.
- The bench configurator offers "Program a KST or Chaservo servo"
  (`servoprog`, link `#v=1&f=servoprog`). It needs the panel and the
  coprocessor alone. Its tick puts the servo signal on wire 64, from pad 17
  (GP13), the programming port, in place of wire 7 on pad 4 (GP2). The
  result states that SETTINGS → DUT → PWM TARGET is PROG PORT, what the PD
  mini sets when it is part of the setup (4.8 to 6.0 V or up to 8.4 V with
  HV SERVO on, 50 to 3000 mA), that without it the programming voltage and
  current limit are stored and not applied, and that the level of the
  servo's reply on GP13 is not measured. Existing links keep their meaning:
  `kst` stays an unknown id.
- The bench configurator adds the DC supply of the Osoyoo breakout board,
  7 to 12 V DC (part `dcsup`, rule `osoyoo-dc`), to every setup in which the
  servo takes the V pin of a strip: a servo, no PD mini, servo power B
  (`servoPower: bench`) and the Osoyoo connection. In these setups the
  jumper V Selector sits on 5V, which takes its power from the DC input
  alone. The supply has 2 leads, wires 65 and 66, drawn as stubs to the DC
  jack, the VIN terminal or the header H4 (pin 2 PWRIN, pin 1 VIN-), exactly
  1 of them. The DC jack takes a 5.5 x 2.1 mm plug, centre positive; its
  minus is GND of the board, with no reverse-polarity protection. The
  product page names the socket DC005-2.1: the 2.1 mm pin follows from that
  name, and the 5.5 mm outer diameter is the usual size of that socket
  family, not stated by the vendor. The SI2302 in the lead VIN- protects the
  VIN terminal and H4 only, and the board notes state so. Above 12 V the
  gate-source voltage of the SI2302 exceeds its ±12 V rating: its gate sits
  at PWRIN through R7. The current the supply must deliver is not stated.
  The supply is the first modelled part (`Part.modelled`): the data states
  the range it needs, not a product, and the page and the PDF tag it
  "Modelled part: no product chosen". The drawing `dcsup` is a generic
  plug-in supply.
- The bench configurator lists the parts to buy for a setup, under "Parts
  to buy" ("Teile zum Kaufen") on the page and in the PDF export. Each row
  gives quantity, maker, ordering name, a check line, the maker page and a
  web search link (`https://www.google.com/search?q=` with the quoted maker
  and ordering name, or the quoted spec of a generic or modelled part). A
  disclaimer above the list names the release of the data. A part with a
  tick of its own under "Parts you own" is left out. The variant of a setup
  names its row: `o=osoyoo` adds the Osoyoo breakout board, `o=xt90` lists
  the XT90 INA228 module in place of the Matek board. The servo, the ESC,
  the servo supply and the ESC battery stand in a separate list "You bring"
  without links. "Copy as text" copies the setup link and the list; "Copy as
  CSV" copies 6 columns per row (RFC 4180). The links open in a new tab with
  `rel="noopener noreferrer"`; the page requests nothing from Google or a
  maker before a click. In the PDF each maker page and web search is a link
  annotation. Data: `Part.buy` with `maker`, `order`, `check`, `page`, `qty`
  and `only`, and `Part.yours`; `npm run check` reports a part without a row
  for a set of options, a variant without a row of its own, a page that is
  not https or carries a tracking parameter, and a brought part that names a
  maker.
- Each row of "Parts to buy" in the bench configurator has a tick "Have it"
  ("Habe ich"). A ticked row leaves the rows to buy and stands under "You
  have" ("Hast du"); removing the tick returns it. The rows under "You bring"
  have no tick. The URL hash keeps the ticks under the new key `h`, one row
  token each: the part id, or the part id, a dot and the row id
  (`#v=1&f=escpow&h=panel,wire.dmm`). A token that names no row to buy of the
  setup is dropped and named in the notice. Links without `h` open the same
  setup as before; the hash version stays 1. "Copy as text" lists the ticked
  rows under "You have", "Copy as CSV" leaves them out, and the PDF lists
  them under "You have". The tick works on a part that a function adds as
  well. Data: `Buy.id` names a row within its part; `npm run check`
  reports more than 1 row of a part without an id, an id that occurs twice
  in a part, and a malformed id.

- Every test of `test/host` stops after 120 s unless it sets its own limit,
  and every CI job has a `timeout-minutes` limit.
- `tools/check_protocols.py` checks the `_Static_assert` rule of CONTRIBUTING
  "Where code goes": the files outside `protocols/` with an assertion on a
  module's macros or enumerators are listed in the tool, and a new or a
  removed assertion fails the check.
- `tools/check_docs.py` checks every "N binaries" count of `STATUS.md` and the
  "N `test_kst_*` binaries" count, holds the pico-sdk and ESP-IDF tags of
  `docs/Building.md`, `docs/Performance.md` and their German pages to
  `ci.yml` and `release.yml`, and requires the SPDX line in the scripts
  under `tools/` and `test/tools/`. The 18 scripts without one carry it.

### Changed

- The splash's credit line reads "Designed by JuWi" in English and German.
- PICK A PIN places every pad, key and marker at the nearest pixel of its
  0.01 mm position instead of truncating it, so a drawn pad and its key
  meet. Positions move by up to 1 px.
- The panel's artwork store (`shared/artwork`) keeps any format with its
  hash: header version 2. Logs and comments name the board drawing; the
  `boardart` partition (2 MB, 8 slots) is unchanged. The panel image is
  4,256 bytes larger (1,127,872 to 1,132,128 bytes).
- Docs, configurator and the panel band name the input voltage of the PD mini
  and of the boards as "input voltage" (DE "Eingangsspannung") where a
  voltage is meant. The band text `ALERT_PDMINI_SAG` reads "PD mini input
  voltage ...". The German configurator and docs use the glossary terms
  Frame Rate, Ruderhorn and Pieptöne.

- The T-plug of the PD mini (`usbtap`) has no tick in the bench
  configurator: the PD mini brings it by the rule `pd-tplug`, and it stays in
  the setup, the parts list, the wires and the drawings whenever the PD mini
  is there. A link with `p=usbtap` loads; the id is dropped without a notice,
  and without the PD mini the setup holds no T-plug. A part that a rule adds
  along with a bench part reads "Included with these parts: PD mini."
  ("Enthalten bei diesen Teilen: PD mini."); a part that a rule adds for
  another cause keeps "Needed by: ". Copy link, its fallback field, the print
  header and the PDF link drop `p=usbtap`, the id of a function without a
  tick and a test object under `p`, which they name as the tick of its
  function; the address of the page keeps the link until a change. `f=usbtap`
  and `o=usbtap` keep their notice. Functions that the parts of the setup
  bring read "Enthalten bei diesen Teilen." in German. The sweep covers 8
  selectable parts, 4096 sets.

- The bench states are named SAFE and ARMED in English and German, as the
  status band shows them: in the panel texts, the docs and the configurator.
  The action reads "switches the bench to SAFE" / "schaltet den Prüfstand
  auf SAFE". The ESC STICK red-light results DISARMED and NOT ARMED read SAFE
  and STILL SAFE (German NOCH SAFE); the servo report's abort reason reads
  `bench SAFE`. The ARM and DISARM buttons, the protocol's command, register
  and error names (NOT_ARMED) and the OpenYGE state `DISARMED` keep their
  names.
- `tools/check_protocols.py` checks that a module's `README.md` has an
  "Interface" heading, a heading with "Trademark" in it where it names a
  brand, and the words "no pin driver" where the module has no `rp2350/`
  folder; the Interface heading and "no pin driver" count outside fenced
  code blocks and HTML comments only. That heading is any heading that contains
  "trademark", in any case, emphasis and link markup ignored. Brand names
  match anywhere in the file (paths, addresses, code blocks and comments
  included), in any case, with a version suffix (`DShot300`, `S.BUS2`) and
  inside an identifier, file name or number (`kst_session_t`, `hitec_x.c`,
  `www.futaba.com`, `2kst`), not next to a letter (`KSTART`, `KstSession`).
  An include path
  with a `..` component or an absolute path in a module fails; so does an
  include the tool cannot resolve (a macro operand, or the name
  `__has_include` outside a literal, also as the body of a macro; as the
  operand of `#ifdef`, `#ifndef`, `#elifdef` and `#elifndef` it passes, and
  as the operand of a `defined` term on an `#if` or `#elif` line that holds
  nothing but such terms, white space, `(`, `)`, `!`, `&` and `|`, on the
  directive's line as GCC reads it, which a comment or raw string literal
  across a line break continues), an
  `#include_next` of anything but a C standard header or, in a
  C file under `rp2350/`, a pico-sdk header, a line
  of only `#` or `%:`, any trigraph (`??` followed by one of
  `= / ' ( ) ! < > -`) anywhere in the C of the file, a token after an
  include operand, and a NUL byte anywhere in the file.
  `#import`, the `%:` digraph, line continuations (LF
  and CRLF, also with blanks between the backslash and the newline), a
  lone CR as a line end, `//` inside a string or a raw string literal, a
  quote without its closing quote, which runs to the line end (a
  backslash before that line end escapes nothing), a number with digit
  separators (`1'000`; a `'` before a `.`, a `$`, a universal character
  name or a character outside ASCII ends it), an exponent sign (`1e-x`),
  `$`, universal character names (also `\u{e9}` and `\N{...}` in gnu11,
  gnu17, gnu23, gnu++11 or later and C++23) and the characters outside
  ASCII that GCC 13 takes into an identifier (the ranges of ISO C11 Annex
  D.1; a number starts only at an ASCII digit, and a number or raw string
  literal starts after any character outside the ranges, as in C), a leading
  byte order mark, form feed and vertical tab are read. Each file is read under the literal rules
  of ISO C11 and C17, C++11 (raw strings), gnu11, gnu17 and gnu++11 (raw
  strings and delimited and named universal character names), C23 (digit separators), C++14 to
  C++20 (raw strings and digit separators), and gnu23, C++23 and gnu++14
  or later (all three); an include found under any of them counts. A line continuation inside a raw string literal is
  kept. The C of a `.pio` file is the text pioasm 2.3.0 writes into the
  generated header: the `% c-sdk {` blocks after the first `.program`,
  found as pioasm lexes the file (a `;` inside a `/* */` comment ends
  nothing, an empty block name is c-sdk, a lone CR is a character of its
  line, a block ends only at `%}` in column 0). The CMake include
  directories and link targets of a module are not checked, and a
  `__has_include` that a macro pastes together with `##` passes. `""R"(`
  and `''R"(` are read as C reads them, as a literal and a raw string,
  while C++11 or later reads `R` as a literal suffix, so an include that
  such a sequence hides in C and C++ reads is not found.

- The bench configurator states the supply of the V pins of the Osoyoo
  breakout board from its schematic REV 1.0, the XL1509 datasheet and the
  product page. The jumper V Selector (header H3, print 5V and 3V3) sets the
  V pins of all 40 strips: 5V from the XL1509 converter, fed by the DC jack,
  the VIN terminal or the 2-pin header H4 (chip input 7 to 40 V, print 7 to
  16 V, 2 A rated), 3V3 from the regulator fed by VSYS of the Pico board
  (500 mA rated, vendor values). Without a DC supply the 5V position gives 0 V. The data carries
  a warning to check the jumper before a servo is plugged in, since a servo
  gets 3.3 V in position 3V3, where the regulator is powered; whether USB
  alone powers it is not resolved. The data states that exactly 1 of the DC
  jack, the VIN terminal and the header H4 is connected. The vendor's
  workaround for USB alone, a Dupont cable from the S pin of the 3.3 V row to
  a V pin, is marked as not for the servo: that S pin is the 3V3 pin of the
  coprocessor board (pad 36), and with the jumper cap on 5V the cable puts
  5 V on it. With the servo on the external supply or the PD mini, the check
  entry `strip-servo-plus` states that the plus wire of the servo lead goes
  to that supply, through VIN1- and VIN1+ of the INA3221 board where it is in
  the setup, and the V pin of the strip stays free. The pro "plugs
  straight in" names the setup without the servo programmer and without the
  INA3221 current sensor. The data states that the Pico accepts 3.3 V I/O
  only, that the jumper cap sits on 3V3 as delivered, the voltage expected
  at the V pin per jumper position, and that without USB voltage the
  converter also feeds VSYS of the Pico board. Each check of the strip holds
  the facts of one source: schematic, product page, product photos, XL1509
  datasheet or SN74HC04 datasheet. A source of the kind `doc` names a
  document, its address and the place in it; the data check rejects one
  without them. The drawing of the board shows the jumper cap on the 5V
  pair, labelled "cap on 5V".

- An arm drives only the outputs of the screen's role (link protocol 4.18).
  ARM on the CONTROL page takes 0, 1 (every output), 0x0002 (throttle
  channels), 0x0004 (surfaces and programming ports) or 0x0006; any other
  value is refused with BAD_VALUE, and so is a change from one non-zero ARM
  to another. MOTOR & ESC and a PROGRAMMER stick run write 0x0002, SERVO
  writes 0x0004. A pin of a role the arm does not name sends no pulse. An
  arm for the other role disarms first and arms after 100 ms. SERVO sweeps,
  RESUME and SERVO_SENSE captures need the surfaces armed and answer
  NOT_ARMED on a bench armed for the throttle alone. STOP, heartbeat loss,
  link silence and a disarm still zero the whole ARM. A 4.18 panel writes 1
  to a coprocessor older than 4.18, which arms every output.
- The band's arm chip reads ARMED MOTOR, ARMED SERVO or, for an arm of every
  output, ARMED.
- The servo programmer takes no port while the bench is armed for the motor.
- Protocol 4.19: SERVO's frame rate drives the PWM target alone. With
  OUTPUTS as target it runs every PWM slot whose first channel is a surface,
  every programming port at 50 Hz. With PROG PORT as target it runs that
  port's pin alone; every slot keeps its OUTPUTS rate and every other port
  runs at 50 Hz. A SERVO rate change moves no throttle and no other output,
  and the pulse-longer-than-frame refusal (BAD_VALUE) judges the target
  alone. A change of PROG `TARGET` sets the rate to 0, and the panel sends
  it again with the next position. A port whose pin shares its RP2350 PWM
  slice with another port's pin takes 0 or 50 Hz only; a `PIN` write onto
  the slice of a target running at another rate is refused with BAD_VALUE.
  The SERVO screen's note in force names the target the coprocessor holds
  (`prog_link_drive()`): "OUTPUTS run at" or "PROG PORT GP13 runs at", in
  the warning colour while that target differs from the PWM TARGET setting,
  while the port has no pin, while PORT PWM is OFF and while the target is
  not known. The refusal note reads "a pin shares a PWM slice"; against a
  4.1 to 4.18 coprocessor the note in force reads "every PWM surface runs
  at". With no link the note in force names no rate: "No link: the rate in
  force is not known.". A PROG write refused because the silicon
  cannot bind a pin leaves SERVO's rate, its sweep and the slots' rates as
  they were.
- RUN on ESC STICK is refused while no pin is bound as a throttle, while the
  output binding is not read, and while the supply is the panel's model; the
  line beside RUN names the reason, in English and German, and such a reason
  closes an open warning.
- Leaving SUPPLY, SERVO or ESC STICK switches off a PD mini output that
  screen switched on: reported on, or an ON on its way. An output another
  screen switched on stays. Overlays, keypads, questions and the servo
  programmer on SERVO are no leave. A PD mini output switched on by the
  servo programmer's CONNECT is SERVO's. An output SERVO switched on stays
  on after SERVO is left while the servo programmer holds its port, until
  the port lets go, then goes off unless SERVO is shown again first; an ON
  SERVO asked for that is on its way when the port takes the supply counts
  as switched on. A touch loss during a hold on OUTPUT ON drops the ON. The
  screen drops one not yet sent. The control task drops one already sent, or
  switches the output off (`touch lost while switching on -- output off`)
  until a frame with an unbroken touch stream has seen it on. That frame can
  come up to 50 ms (one supply sample) before SUPPLY shows the output on. An
  ON a loss meets after that frame stays on, stays the screen's and goes off
  with the leave. MOTOR & ESC switches no supply; an ESC run there is
  powered from its own source.
- The servo programmer's record of an interrupted write (`KST/KST.TXT`) is
  version 2: `RCBENCH KST 2` with a `pin=` line, the GPIO the port drove.
  The record's lines name that pin (`Connect it on GP13 and READ ALL.`). A
  version 1 record, one with no pin (`FF`) and one with a pin above GP47
  are read and name the port. A record discarded leaves no pin behind: the
  next record names the pin of its own port. A panel of an earlier release
  does not read a version 2 record.
- The SERVO screen's right card has a TARGET row (German ZIEL) between SPEED
  and CENTRE that names the active PWM TARGET with its pins: `GP4` or
  `GP2 GP4` for the pins bound as SERVO PWM (as many pins as fit the 23
  cells of the value, then `+N`), `PROG PORT GP13` for the programming port (German
  `PROG-PORT GP13`), `PROG PORT` for a port target without a usable pin,
  with `PWM OFF` after either while PORT PWM is off
  (`PROG-PORT GP13 PWM AUS`). `NO LINK` (`KEIN LINK`) shows while no
  coprocessor answers and `NOT KNOWN` (`UNBEKANNT`) while the PROG page is
  not read or a write to it was not acknowledged; the tester sends no command
  in either. While the servo programmer holds the port, the row does not
  change: it keeps naming the target the coprocessor holds. The warning
  colour marks five states: the programming port, no SERVO PWM pin
  (`NO SERVO PWM PIN`), a binding not read, no link and a target not known.
  The pins come from the binding the tester drives. The row follows the
  target the coprocessor holds (`prog_link_drive()`), not the PWM TARGET
  setting. The right card stays in sight under the settings, their keypads
  and the servo programmer, so the row names the pin while they cover the
  left card.
- The SERVO screen's type row carries the pulse range after the type and the
  frame rate, `STANDARD PWM  50 Hz  1000-2000`, in place of the RANGE row
  (`BEREICH`) under SPEED, which the TARGET row takes.
- The SERVO screen names the supply on the line over the power plot
  `PD MINI` in both languages, in place of `SUPPLY` (`NETZTEIL`). While the
  panel's model stands in for the PD mini, a MODELLED pill (`SIMULIERT`),
  20 px high and 24 px plus the word wide, at least 88 px (88 px for
  MODELLED, 96 px for SIMULIERT), sits on the plot's top-left corner. With INA3221 CH1 as
  the meter the line shows no pill.
- `sbus_to_us()` rounds to the nearest on both sides of 1000 us. Below raw
  172 it rounded the other way: raw 0 gave 896 us, now 895 us; raw 171 gave
  1000 us, now 999 us.
- `docs/Receivers.md`, `docs/Screens.md` and the S.BUS module README state that
  no firmware feeds the analyser screen: the S.BUS core is in the panel image
  and the host tests, no byte source exists, and the screen shows SILENT. The
  German pages follow.
- The OpenYGE and the output driver headers state what the code does: a frame
  of version 3 or above is read with the 6-byte header, `crc_errors` and
  `rejected` count a failing candidate once, at the front of the buffer, and
  `out_dshot_bind()` / `out_ppm_bind()` return false and keep the old binding
  for a bound pin with other settings. The DShot module README,
  `docs/DShot.md` and `docs/DShot-de.md` state this.
- `dshot_throttle()` maps a command above its span to 0 (motor stop), as
  `dshot_frame()` maps a value above 2047 to 0. It mapped it to 2047. A
  command equal to the span still maps to 2047. The firmware passes
  `outputs_actual()` against a span of 1000, so no stored value of 0 to 1000
  changes.
- The coprocessor refuses a PWM (pulse-width modulation) endpoint longer
  than the frame of the output that renders it, with BAD_VALUE, on CHAN_CFG,
  OUTPUTS, a BIND commit, SERVO's frame rate and PROG's `PWM`, `MIN_US` and
  `MAX_US`. The longest endpoint is the period minus 1 µs: 1784 µs at
  560 Hz, 1999 µs at 500 Hz, 2499 µs at 400 Hz; below 400 Hz the 2500 µs
  ceiling governs. A standard 1000 to 2000 µs servo is refused at
  500 Hz and 560 Hz. A stored PWM slot that breaks the rule starts unbound.
  Link protocol 4.16. The panel writes a programming port the range the
  SERVO screen names before it switches the port's pulse on.
- Text corrections, no behaviour change: SECURITY.md lists the input
  parsers of 19 modules; the Manifest drops its state column and points to STATUS.md;
  STATUS.md, Building and the READMEs list the `pages.yml` workflow and the
  directory layout; sizes, counts and figures in the KST, DShot, configurator,
  NOTICE and First run texts match the code; the Servo, OpenYGE and
  monostable comments no longer say that unwired code runs; DShot, Servo,
  Bringup and Manifest German pages match the English ones.
- The host tests are built with the flags of the libraries, `-Wall -Wextra
  -Werror -Wshadow -Wconversion -Wno-sign-conversion`, and
  `tools/render_ui.py` builds `render_screen.c` with the same flags. clang-tidy
  and cppcheck run over `test/host/` in CI; the checks that are off for the
  tests are listed with their reasons in `test/host/.clang-tidy`, and
  cppcheck's `constParameterCallback` on the page callbacks of the link tests
  is suppressed inline, with the reason. The `analysis` job
  timeout is 30 min. Test code fixed under the stricter rules: canvases built
  with a zero clip rectangle in `test_kst_panel`, ids cast to a 16-bit type for
  an 8-bit field, an out-of-range float to integer conversion in the settings
  record helper, and allocations used without a check (`REQUIRE_ALLOC` and
  `REQUIRE_REALLOC` in `greatest.h` end the case with the line).
- The cppcheck command in CONTRIBUTING.md names the include directories the
  way CI does, and the list of failing changes names the flags and the scope
  of both analysers.
- The supply state reports no valid voltage or current (`ok` 0) when the
  supply's reading count has not moved for 1.5 s (`SUPPLY_LINK_STALE_MS`),
  with the last values kept. A count that stands for 3 s
  (`SUPPLY_LINK_BLIND_MS`) behind a page that answers switches an output
  that is on off, and the band says `supply sends no readings -- output
  off`. The PD mini driver takes a reading 400 ms past its due
  (`PDMINI_POLL_LATE_MS`) -- the display, the output state, the input or
  the slot -- before the next set-point write, one at a time with another
  transaction between two. A slider dragged with SLIDER AND STEPS off, a
  new set point about every 50 ms, then gets a display read 500 ms after
  the last one with a module answering at once and a state read 900 ms after the last one, so the count
  keeps moving, and an output the module switches on while an OFF is
  asked, or off by its protection, is seen during the drag. The state read,
  and the input read before a write, may also follow another reading
  straight away. The input read before a set-point write or an ON goes out
  only when one is waiting, and not between the reads for an ON, so set
  points and an ON reach a module that answers up to 390 ms late: one set
  point within 2.5 s, an ON within 3.6 s at set points already written and
  up to 7.5 s right after a set point change, a state the module changes
  itself seen within 2.1 s during a drag (host model, 40 start phases; the
  real module's reply time is not measured). Current limitation: a module
  answering late stretches the 500 ms state and input reads at rest -- the
  input 850 ms apart at 100 ms late, 2.5 s at 390 ms, the state up to
  1.5 s -- so a sagging input is cut 1.7 s after the sag at 100 ms late
  and up to 4.7 s up to 390 ms (about 1.0 s with a module answering at
  once), and an output the module switches on while OFF is asked stays on
  up to 1.8 s at rest. Current limitation: with replies 279 ms or more late, a drag
  holds the display read 3.7 s to 5.0 s, past `SUPPLY_LINK_BLIND_MS`, and
  the panel switches an output that is on off, possible from 263 ms with
  the panel's page read every 100 ms; at rest the display read stands up
  to 2.5 s at 390 ms, and the cards can show dashes from about 260 ms.
  The INA228 temperature and diagnostic flags go with the part on the tick it goes offline. An AS5600 STATUS byte with both the magnet-too-weak and magnet-too-strong bits set is a failed
  sample. A mark kind passed as a trace trigger is ignored, and the trace
  drops a sample while a shunt or configuration record is owed and counts
  it after that record.
- The servo limit search and the synchroniser keep every command inside 500
  to 2500 µs. The search refuses a step of 0 µs (fault `BAD_CONFIG`), keeps
  an endpoint backed off past the centre at the centre, and ends at the end
  of the pulse range. A last step shorter than `step_us` reaches the end of
  the travel, so the end is measured before `NO_LIMIT`. Both stop on a current that is not a number. The
  synchroniser reports `NO_MINIMUM` for a minimum at the edge of a scan
  window and runs at least one round.
- `tools/sense_trace.py` starts a new boot only when the tick runs back by
  more than 100 ms; a smaller inversion stays in the boot and keeps the
  signed difference. The `$D` line is documented as written about 50 ms after
  its `t=`, and `servo_move.h`, `bench_frame.c` and `servo_sync.h` describe
  what the code does.
- Link protocol 4.17: the TONE page refuses a pin held by an output, SENSE,
  SUPPLY or a programming port, and a capture the coprocessor cannot start
  for now, with the new NACK reason BUSY (6), and keeps BAD_VALUE for values
  no freed pin makes good and for an enabled tap on a coprocessor whose core
  1 did not start at boot. The panel shows that last case as `PIN NOT
  FREE` as well when the coprocessor's saved set-up has the tap enabled,
  and otherwise the refused message and `NOT RUNNING`; it clears only with
  a restart of the coprocessor. The
  panel offers a first frame refused with BUSY
  again every 5 s and writes one refused with BAD_VALUE again only after the
  next change of the set-up or link-up. From a 4.16 or older coprocessor,
  which says BAD_VALUE for both, it offers the frame again every 5 s when
  its tone range and gap are valid.
- The phase tap readout shows `WAITING` when the TONE page's flags say the
  tap runs and its window number has not moved for 1.5 s
  (`TONE_LINK_WINDOW_STALE_MS`).
- A read-back of FFFFh of the INA228's ADC_CONFIG or the INA3221's
  Configuration takes the part offline on that one read and counts in
  `RESETS`; another value that differs is still read a second time. The
  readings themselves are not checked for all ones.
- The CSV file of `tools/sense_trace.py` has a fourth column, `clipped`: 1
  at or past the top of the range, -1 at or past the bottom, 0 for a value.
- Trace format version 3: a `$D` line ends `cmd=` with its command's `$C`
  time, and `tools/sense_trace.py` ends that command with it, also when the
  output was let go and the next command came within 30 ms. The tool reads
  version 2 files with the 30 ms rule as before.
- The journal file's check in `kst_prog.c` calls `link_crc()`; a known-answer
  test holds the value CRC-16/CCITT-FALSE gives for a journal file.
- The programmer's pass gate uses C11 `<stdatomic.h>` in place of the
  `__atomic_*` builtins.
- The configurator takes the limit of 2 pictures per part from the core and
  builds the build order of the page and of the PDF from one function.
- `hardware/docs/pinmap.json` is `hardware/docs/ioboard-pinmap-draft.json`;
  `Pins.md` states that it is the draft of the planned IO board.
- The research workflow scripts are no longer in the repository. They are kept
  on the owner's machine, outside it; `ci.yml` no longer runs
  `session.py check`.
- The KST README names Tool Card #2 and Chaservo in its list of what is
  measured. `docs/Screens.md` and its German page state the 19,178 us
  window of a coprocessor flash save as 1 sample, with the flash part not
  yet identified.
- The release of the bench configurator is stated once, in the `version`
  field of `configurator/package.json`. The data file, the page header, the
  trademark notice, the print head, the source-link tag and the data check
  read it from there. `npm run check` in `configurator/core` compares that
  version with `RCBENCH_VERSION_*` in `shared/link/include/rcbench_version.h`.
  A header one release ahead, whose tag does not exist yet, is a warning,
  since the release pull request moves the header before the tag exists and
  `package.json` follows after the tag. A header ahead whose tag exists,
  `package.json` ahead of the header, a differing `version` field in
  `package-lock.json` and a README that names another release (in prose or
  as `"release": "X.Y.Z"`) are errors. The Configurator workflow also runs
  on a change of that header.
- `testbench/host/capture.sh` warns when a capture holds no samples or the
  read-back fails, and still prints the path. `selftest.sh` no longer reports
  `sigrok-cli --scan` error output as the analyser found, and skips a
  non-numeric `SWD_SWCLK` or `SWD_SWDIO`.
- `docs/Servo.md` and `docs/Servo-de.md` state that the limit search and the
  synchroniser are not called by the coprocessor image, give the scan windows
  of the synchroniser (±40, ±13, ±4 µs) and correct the half-turn example.
  The testbench documents carry no development history and the corrected
  splash hold (1.6 s), heartbeat rate and run state.
- The bench configurator describes release 0.17.0. Every repository source
  of its data is a file and line at the tag `v0.17.0`: 11 sources carry the
  line of that tag, and the source of the entry on serial ESC programming
  carries the line and the wording of `STATUS.md` at that tag. The list
  "Not in a release" holds 4 entries: the entry on the programming port for
  KST and Chaservo servos is removed, since 0.17.0 contains the servo
  programmer.
- The Configurator workflow shows each warning of the data check, such as a
  release tag newer than the data, as an annotation and in the run summary.
  The Release workflow runs the same check on every tag push and shows its
  warnings the same way. The check holds no release back.
- CI installs pillow 12.3.0, gcovr 8.6, ruff 0.16.5 and pytest 9.1.1 at
  exact versions.

### Removed

- The board photograph: `firmware/iomcu/artwork/rp2350-can.png`, its
  206,000-byte table `art_rp2350_can.c` and `tools/gen_board_art.py`. The
  coprocessor image is 203,808 bytes smaller (388,168 to 184,360 bytes). A
  photograph kept by an older panel is found as empty and replaced by the
  drawing.
- The placeholder screen for a screen id without a screen, its 16 English and
  16 German strings, the SOON tile mark and the per-file coverage exemption
  that existed for it. A screen id that names no screen draws the overview.
- Six exported functions used in one file are `static`: `esc_det_drop_group`,
  `esc_det_floor_only`, `ina228_ct_us`, `servo_test_start_str`,
  `balance_screen_angle` and `ui_tabs_label`.
- `ui_hero_render`, which no screen draws; `ui_hero_def_t` stays for the
  motor screen's rail cards.
- `board_lcd_reset()` in the panel firmware, which has no caller. The panel
  keeps `storage_deinit()`, the unmount for the SD card remount. Also the `conflicts` field of the configurator's resolved setup,
  which is always empty.

### Fixed

- The panel does not build on ESP-IDF v5.5.0 or v5.5.1. Their TWAI
  (two-wire automotive interface) driver sends a CAN frame queued with
  DLC (data length code) 0 as DLC 8 with 8 zero bytes. Every link read
  and the self-test status request are DLC 0, so the coprocessor refused
  them all and a panel built there never linked; the self-test echoes,
  which are DLC 8, passed. can_twai.c stops the build on those two
  releases. The CI matrix builds v5.4 and v5.5.3, and the docs check
  compares ESP-IDF point releases in full.
- PICK A PIN shows the pin of the coprocessor's programming port (GP13 by
  default) grey, lists it first under `PROG PORT` in IN USE, and a tap on it
  changes nothing. It was drawn as free, and a tap took it from the port.
- Configurator: the part "Emergency stop" states that the coprocessor
  switches the bench to SAFE and the PD mini output off when the heartbeat
  stops; the button breaks no power line.
- `gfx_utf8_next` rejects a four-byte sequence above U+10FFFF (lead `F4`,
  second byte `90` to `BF`) as malformed, one `?` cell per byte.
- `as5600_cdeg` computes in 64 bits. It overflowed `int32_t` above
  |counts| 59,652; the result now saturates at +-2,147,483,647 above
  |counts| 244,335,917.
- The wiki mirror points each link that leaves `docs/` at the repository on
  GitHub: `blob/main/<path>` for a file, `tree/main/<path>/` for a directory
  and `raw/main/<path>` for an image, with the anchor, the title, angle
  brackets around the target and spaces inside the parentheses kept.
  A target is a directory when it ends in `/` or names a directory of the
  repository, so `[x](..)` and `[x](../protocols/kst)` open the tree view.
  The legal-notice link on the servo programmer pages and the KST link on
  the Link pages open the repository instead of a missing wiki page.
  `tools/wiki_links.py` exits 1 on a link that still leaves `docs/`, and the
  CI step "The wiki link rewrite still works" converts a copy of `docs/` and
  runs `wiki_links.py --verify` on it.
  Reference definitions (`[label]: ../README.md "title"`) are rewritten and
  checked the same way; a link title in `"..."`, `'...'` or `(...)` stays;
  a bare target may hold backslash escapes and one level of parentheses
  (`API_(v2).md`); a name that starts with two dots (`..notes.md`) is a page
  inside `docs/`, and only `..` or `../` leaves it.
  Links in code spans, in fenced code blocks, in URI and email autolinks
  and after an escaped `[` stay as written; backslash escapes count in
  pairs, so `\\` before a backtick leaves it to open a code span. A
  definition that an image uses (`![alt][label]`) gets the `raw` form, and
  labels match after collapsing spaces, tabs and line endings only. A label
  may nest brackets to any depth, a URL scheme may hold `+`, `-` and `.`
  (`git+https:`), and a definition may put its target and title on the
  next line. An image may stand in link text, `\![` is not an image, `\]`
  stays in link text, a fence opens at 0 to 3 spaces of indent or after
  `>` markers and ends with its block quote, a line ending may follow `(`,
  and a blank line before a title ends the link.
  Every page is parsed with markdown-it-py 3.0.0 (a CommonMark parser)
  before and after the rewrite, in the rewrite and in `--check`. The two
  token streams match token by token in kind, tag, text and attributes,
  and each link href and image src is what the rewrite rule gives the
  original. A difference exits 1 with page, line and both values, and the
  page is not written. A definition in a block quote or list item, a
  definition line right after paragraph text, a label over two lines and a
  link-shaped text in a raw HTML block or indented code block fail this
  way; the tool's description gives the form that converts.
  A query string on a link target stays out of the check for a
  directory, so `../protocols/kst?tab=x` is a `/tree/main/` URL. Only the
  path of a target is normalised; its query string is kept as written, so
  `?a=b/../c` and `?u=http://x/y` are not altered.
  `tools/wiki_links.py --verify`, run by the CI step, reads every converted
  page with the same parser and fails on a link that is not an absolute
  URL, an anchor or a page name without `.md` and `../` (a title that
  ends in `.md` passes when a mirrored page has it), on a link to
  `/raw/main/` unless an image shares its definition, and on an image that is not relative inside `docs/` or
  `/raw/main/` above it. It prints page and line. The docs workflow
  installs markdown-it-py 3.0.0 before the rewrite.

- The panel writes the board drawing to its flash only on a bench in SAFE,
  with no arm waiting, and with the supply output off and no ON queued. The
  write (2 erases of 4 kB, 1 sector written and read back and the header for
  the 1,560-byte drawing of the RP2350-CAN; at most 3 erases for the
  largest drawing, 5,328 bytes) runs on the control task, one step per 5 ms
  pass, and pauses between two steps when the bench is armed or the supply
  comes on. It resumes from the same step on the first safe pass. Arming and
  the supply are not refused while it waits. Before, the write started after
  every fetch on a task of its own, armed or not, and each erase stalled
  both cores: STOP, OUTPUT OFF, the supply trips and the heartbeat waited
  for it, and a stall over 150 ms disarmed the bench. The settings save and
  the drawing keep ask one rule, `flash_gate_open()` in
  `shared/safety/flash_gate.c`, under the new host suite `test_flash_gate`.
  The settings save now also waits while an arm is waiting. The `artkeep`
  task and its 4,096-byte stack are gone. `tools/stack_check.py` follows the
  artwork store's flash callbacks by name (`FLASH_CALLS`).
- A log file whose last row has no line feed reads without that row. A
  write cut off by a power loss or a full card leaves the bytes the card
  took: a row cut after `95` of `9554` read back as 95 rpm. The panel's
  reader (`log_reader_row`) ends a row at its line feed and nothing else,
  leaves an unterminated last row out, and reports it in
  `log_analysis_t.cut_row`. Past the row cap (60000 rows) the cut row is
  decided from the file's last bytes: the source reports its size and
  seeks (`log_source_t.size` and `.seek`, on the card's stdio file up to
  2 GiB and on memory), and at most 4096 bytes before the end are read. A
  source without them, or a file whose last 4096 bytes are all blanks, is
  read on to its end. Past the cap, a quoted field left open with a line
  feed as the file's last byte is not reported as a cut row. The import
  view shows ROWS in the warning colour and `LAST ROW CUT OFF - LEFT OUT`,
  on its own line below any other import warning. `tools/sense_trace.py
  --servo-csv` applies the same rule and says so in its report. The
  delimiter is sniffed from the first 2048 bytes up to their last line
  feed, so a cut row cannot change how the rows before it split. Only
  space, tab and CR after the last line feed are blank; a vertical tab or
  form feed there is a cut row, in the panel's reader and in
  `tools/sense_trace.py`.
- The BALANCE pane carries the MODELLED pill and the words "Not measured."
  (SIMULIERT, "Nicht gemessen." in German) under its readings: the angle of
  265° and the 0.42 g and 0.35 g readings are fixed values.
- A frame of a held plus or minus key on SETUP counts for at most 0.25 s
  (`ui_hold_credit()`). A 1 s stall steps at most 3 times at 8 Hz and 8
  times at 30 Hz; the repeat rate of a normal hold is unchanged.
- A pulse that ends on tick 0 of the millisecond clock starts the quiet
  window of the ESC menu detector at the fall. A supply reading or INA3221
  window stamped on tick 0 keeps that stamp on the servo screen. A flag
  marks each stamp (`esc_det_t.fell`, `stamped` in `supply_state_t` and
  `sense_link_win_t`) where the value 0 stood for "none".

- A CHAN_CFG slew above 3000 (300 %/s of the span) is refused with
  BAD_VALUE and nothing is stored; before, any value to 65535 reached the
  output bank. The PROG page's port slew has the same limit. Link protocol
  4.16. A CHAN_CFG or
  PROG slew above 3000 kept in the coprocessor's flash by an older build
  starts as 3000, on the page, in BIND_CFG and in the output bank; the
  role, endpoints and pins kept with it start as kept.
- The coprocessor's CAN receive takes the next buffer after one holding a
  frame it refuses (11-bit identifier, remote frame, length above 8), so a
  valid frame in receive buffer 1 is served in the same pass.
- A KST panel row coordinate that names no row gives no row (empty
  rectangle, no label or button text, not live) instead of the first row.
- **A PD mini set point at the input cap lies 200 mV above the sag cut
  line.** The voltage set point is capped 700 mV under the module's input
  (`PDMINI_CAP_MV`), 200 mV above the line a live output is cut at, the set
  point plus 500 mV (`PDMINI_HEADROOM_MV`). In 0.17.0 both lines were the
  same: 20000 mV asked from a 19990 mV input ran at 19490 mV, and a 10 mV
  dip to 19980 mV on 2 input reads switched it off. 200 mV is
  3000 mA through 1 m of 20 AWG cable out and back; the drop on a real
  source is not measured. The panel's voltage cap, the servo test's
  brown-out cap and the programming port's ENTER check (SUPPLY_RANGE) use
  the same 700 mV.
- An input reading under 1500 mV caps a PD mini set point at 1000 mV in
  place of passing the set point asked, and an ON asked then is not
  written: the output is held off as for a sag (bit 10 of the SUPPLY flags)
  until an OFF. The SUPPLY screen and the servo test offer the cap the
  driver writes (`pdmini_cap_mv()`): an input of 1 to 1700 mV shows 1 V, in
  place of 20 V; 20 V stays for an input not yet read.
- `who_failed` of the PD mini driver is stated as a change count that wraps
  from 65535 to 0; the SUPPLY page's AUTO rate search steps across the
  wrap.
- The run clock counts a run that starts on tick 0 from that tick. A flag
  (`in_run` in `arming_t`) marks a run, so every 32-bit tick, 0 and
  0xFFFFFFFF included, is a valid start. A start time of 0 stood for "no
  run", and a run that began on tick 0 restarted at its next pass and lost
  that pass from its length. The tick passes 0 every 2^32 ms (49.7 days).

- The ESC and SERVO classes of PROGRAMMER show that they are a model. No
  transport carries their protocols, and CONNECT, READ and WRITE send
  nothing on a wire. The class tiles, the protocol rows and the device line
  carry the MODELLED mark (SIMULIERT in German). The device line's dot sits
  in the mark, amber while a session is open and grey otherwise, and is
  never green. The device page says "Modelled on the panel: no device is
  reached." `docs/Screens.md`, `STATUS.md` and `testbench/README.md`
  describe the two classes as a model.
- The phase tap's readout on a stick run's page shows PITCH and LOST as
  `--` and no OVERRUN once the last TONE status read is 500 ms old
  (`TONE_LINK_STALE_MS`), or while the state does not come from that read.
  `tone_link_readout()` clears the status's 7 values and sets
  `tone_readout_t.read` only while a read younger than 500 ms gives the
  state; the head's warning colour follows the state.
- An abort of the servo programmer sends no command again that went to the
  coprocessor without an answer. The panel reads registers 0 to 3 of the
  KST page instead: the `SEQ` before the command ends the request as
  dropped, with nothing written, and any other `SEQ` as restarted; a page
  that took the command runs it, and the ABORT that follows ends it with
  result ABORTED. A read that stays unanswered 3 times ends the request with
  no answer. The command's `SEQ` counts as taken only with the command's
  operation in `OP` or a refusal in `RESULT`: at the command after `SEQ`
  0xFFFF, whose `SEQ` is 0, a coprocessor that started again ends the
  request as restarted, not as done with result OK. A page whose `OP` reads
  0 after a read that named an operation started again at any `SEQ`: a
  WRITE taken at `SEQ` 1 after `SEQ` 0 by a coprocessor that then starts
  again ends as restarted, not as dropped after an abort and not as refused
  after the frame was sent again. An ABORT that the page refuses keeps these
  checks on the read after it: a coprocessor that started again under a
  running operation, or after the read that found the command taken, and
  refused the ABORT with BAD_VALUE for its `SEQ`, ends the operation as
  restarted, not as done with result OK, so the servo programmer keeps the
  record held before the write. Before, a WRITE whose answer was lost was
  sent again ahead of the ABORT and could run.
- `kst_session_power_cycled()` drops the session's backup, the record of
  its writes (`wrote_regs`) and the lock, after the end of an operation that
  runs. An edit, a write and a restore end with `KST_SES_ERR_NO_BACKUP`
  until a read of all registers takes the servo then on the line as the new
  backup, so a servo of the same type swapped in during the power cycle gets
  no value of the first. The coprocessor's port begins a new session at
  each ENTER from PWM, so on the bench the backup was already gone after a
  power cycle; `docs/ServoProgrammer.md` and its German page state it.
- The docs workflow no longer cancels a publish in progress when a later push
  starts one (`cancel-in-progress: false`).
- With the environment variable `CI` set, a missing C++ compiler fails the
  CMake configure step of `test/host`, and a missing `git`, `cmake`, `cc` or
  `c++` fails the cases of `test/tools` that need it. Each of these skipped
  and exited 0.
- The docs job and the job of the Release workflow that waits for CI with
  `tools/ci_gate.py` time out after 100 min; the gate waits up to 5400 s
  (90 min). A shorter limit ends the job while it waits.
- `tools/check_docs.py` fails when `docs/Building.md` or
  `docs/Building-de.md` has no "vX.Y and vX.Z" ESP-IDF statement.
- `tools/check_protocols.py` lists each `_Static_assert` on a module's
  constants by file and condition; removing one of two assertions in a file
  fails the check. `static_assert` counts as well.
- The cppcheck step over `shared/` resolves the headers under
  `shared/*/include`; its pathspec matched no file. `ui_seg_hero()` names its
  local `st`, which no longer shadows the file-scope `s` of the screens that
  include `ui_theme.h`, and the unused member `warn.id` is removed from the
  servo screen.
- `testbench/host/capture.sh` checks for a channel that never changed in
  time linear in the samples. The check keeps two flags per channel in place
  of the joined samples: 6 channels of 1 M samples take 0.6 s, where the
  joined string took 8 s, and 6 channels of 4 M samples take 2.2 s, where it
  took 187 s. The output is unchanged. `test/tools/test_capture_sh.py` covers
  flat, toggling, first-sample and last-sample changes, an empty capture and
  several channels.
- `docs/ServoProgrammer.md` and its German page name the servo the
  register layout and the timing come from: 1 Chaservo servo.
- The bench configurator keeps the selection and the chosen language in the
  browser (`localStorage`, key `rcbench.configurator`). A visit by the
  address without a hash shows the stored setup in the stored language, in
  place of the base setup in the browser language. A link with a setup wins
  over the stored one and stores nothing until the visitor changes it. The
  stored setup holds ids alone, so it is the same in English and German. A
  blocked or full store and an entry of another shape give the base setup
  without an error.

## 0.17.0 - 2026-10-12

The link protocol is 4.15. A 0.16 board still links and arms with a 0.17
one, since only the protocol's major number has to match. The servo
programmer for KST and Chaservo servos needs both boards at 0.17: nothing is
sent to the pages 0x32 and 0x33 unless both are 4.13 or later, no LEAVE is
sent unless both are 4.15, and a 0.17 panel with an older coprocessor shows
the programmer as unavailable with the reason. Flash both boards. PROGRAM on
the SERVO screen opens the programmer on the coprocessor's programming port
(GP13 on pad 17 by default): it reads all 32 registers, edits them within
the protocol core's limits, writes the changed registers only and reads each
back, keeps a backup from the first read of a session, and saves and loads a
configuration as a text file of 35 lines on the SD card. A write needs the
switch ENABLE WRITE TO SERVO, which is off by default and takes a hold of
2 s. With a PD mini answering, CONNECT sets the supply (4.8 to 8.4 V, 50 to
3000 mA), and the coprocessor arms no bank while the port is out of PWM. The
protocol core is the module `protocols/kst/`. Every interface to external
hardware is a module under `protocols/`, and `tools/check_protocols.py
--check` holds the modules to that rule. The READMEs carry a trademark
notice and a disclaimer, and makers and brands are named descriptively. The
READMEs and the wiki home link the bench configurator; a release carries the
2 firmware archives and `SHA256SUMS`. The timing and the register layout
come from 1 programming card and 1 servo of unidentified model. The effect
of a register on servo motion is not measured, and no pulse of a port is
measured.

### Added

- SERVO screen: a programmer for KST and Chaservo servos. PROGRAM opens an
  overlay of 5 tabs (SERVO, TUNE, PULSE, FAILSAFE, LOCKED) over the left
  card. It takes the coprocessor's programming port (GP13 on pad 17 by
  default, link protocol 4.13), reads all 32 registers, and edits dead
  band, maximum duty, boost, stretch, soft start, the speed limit, the
  pulse ends, neutral, the angles, reversion and the failsafe with the plus
  and minus keys, the keypad and choice lists. The limits are the protocol
  core's: a value past a hard limit cannot be entered, and a value past a
  soft limit shows the limit's warning and is taken after a hold of 2 s.
  Frequency, lock, the servo and gyro mode and the protection block are
  shown and never written. A write needs the switch ENABLE WRITE TO SERVO,
  which is off by default, takes a hold of 2 s, and goes off at every entry
  of the screen, link loss, coprocessor restart, stop, connect and leave;
  the first write of a session asks for a free horn. The coprocessor writes
  the changed registers only and reads each back. A backup is kept from the
  first read of a session and written back by RESTORE to the servo it was
  read from: a servo that differs from the backup in a register no write of
  the session changed takes no register of it, and the port asks for a
  read. A write that fails after earlier registers were written says that
  those stay, and every outcome that leaves the servo in doubt switches the
  write off.
- SERVO screen, DUT page: PWM TARGET selects what the tester drives, the
  pins bound as SERVO PWM (OUTPUTS) or the programming port (PROG PORT), and
  PORT PWM switches the port's pulse. Sweep, hold and the automatic test
  run on the port. The programmer connects only with PROG PORT; with
  OUTPUTS it names the setting and CONNECT is locked. Both rows change only
  on a disarmed bench with no sweep and no test running and the port not in
  programming. OUTPUTS shows the port's pin as `PROG PORT` and does not
  bind it.
- Servo programmer, supply: with a PD mini answering, CONNECT sets
  PROGRAMMING VOLTAGE (4.8 to 8.4 V, default 5.0 V) and CURRENT LIMIT (50 to
  3000 mA, default 1000 mA) on the PD mini. An output that is on runs the
  session at the lower of the two voltages and the lower of the two current
  limits. Under 4.8 V CONNECT is
  refused, and so it is with programming values above the SUPPLY screen's
  limits. While the port is out of PWM the coprocessor refuses every change
  of the supply's set points and every ON, and the SET row of the SERVO
  screen and the SUPPLY screen dim those controls; OUTPUT OFF works, and an
  OFF during a frame reaches the PD mini after the port has cut the frame,
  12 ms later at most. The coprocessor arms no bank while a port is out of
  PWM and refuses the port on an armed bench. A voltage above 6.0 V needs
  HV SERVO on and a hold of 2 s, and is not stored: a stored value above
  6.0 V is loaded as 6.0 V. The SERVO tab shows the measured voltage
  and current. The port's refusals for a port without a pin, a supply
  outside the range, a bench it does not trust and a supply that does not
  reach its voltage within 3000 ms each have their own line. A supply
  command the port sets is shown on SUPPLY. The panel's STOP ends a
  session through the port's abort, with the supply off.
- Servo programmer, leaving: LEAVE returns the port to PWM without a supply
  cycle, with and without a PD mini, through the port's LEAVE operation of
  link protocol 4.15. The port holds its line low for WAIT BEFORE PWM (0 to
  10000 ms, default 100 ms, kept with the SERVO settings) before the first
  pulse; the tester's controls stay off until the port reports PWM, and
  ABORT during the wait keeps the session. A port that asks for a read or
  holds a servo in doubt is not left with a PD mini's supply on: the line
  names READ ALL or RESTORE, and STOP ends the session with the supply off.
  A coprocessor of protocol 4.13 or 4.14 gets no LEAVE: without a PD mini
  the port returns to PWM without the wait and the line says so, with a PD
  mini the line asks for the update and the session stays. Whether a KST
  servo follows PWM in programming mode, whether a written value acts
  before the next power-up, and the wait a servo needs are not measured.
- Servo programmer and ARM: CONNECT is refused on a bench that is armed,
  asked to arm or has a finger on ARM, and so are the read, the verify,
  the write, the restore and the pairing release. While the programmer
  holds the port no screen arms the bench: ARM on SERVO and on MOTOR & ESC
  is drawn disabled and takes no press, a run of the ESC stick programmer
  does not start and names the reason, and the panel refuses any other arm
  with an alert. A card put in after the panel started is mounted when the
  programmer opens or one of its actions is tapped, and the record of an
  interrupted write on it is read then.
- Servo programmer, backups: the session's backup is not offered and not
  restored once a read shows another servo than the one it was read from,
  a servo that differs in a field no write of the session set out to
  change. A write that did not start, or that the coprocessor ends with
  no register written, counts as none. LEAVE and CONNECT begin a session with that servo's own backup.
  A locked session whose backup the panel holds takes ENABLE WRITE TO
  SERVO with no servo read, for the restore of that backup alone.
- Servo programmer, files: FILES saves the servo's 32 registers as a servo
  file in the folder `KST` on the SD card under a preset name (`KST001` on)
  that the text keyboard changes, and never replaces a file. The first
  write of a session saves such a file without asking: a mounted card that
  is full or fails the write ends it with its own line. With no card
  mounted the write is sent without a file; its page says that no backup
  file is saved and that the session's backup lives in the bench until the
  session ends or the bench loses power, and the SERVO tab shows `No backup
  file: no card. Session restore only.` until a card takes the file at a
  later write. That file holds the servo as the session found it, also
  after a link loss or a panel start under a session the coprocessor
  keeps; the servo as last read is not saved as the backup file once it
  may hold written values. A card that is write protected, or that has a
  file named `KST` in place of the folder, shows `Card takes no file: write
  protected or KST is no folder.` A file is loaded from a list of every
  servo file of the folder, sorted by name without case with the proposed
  names at the place of `KST000`, the highest number first, 5 a page, that
  names each file's servo type, V6 or V8 after the 20k_sel field. The time
  a page behind the first 64 files takes is not measured. Its editable values become the pending changes and reach
  the servo through the ordinary write only; a file of the other type, a
  damaged file, a file of a later format and a value past a hard limit are
  refused with the reason, a hand-edited file is named as one with the
  count of the 27 values its lines give, values past
  a soft limit wait for a hold of 2 s, and locked values that differ are
  counted as not written.
- Servo programmer, the pages before a write: the write page lists each
  changed value as `DEAD BAND: 2.0 -> 1.6 us`, the servo's value, the new
  value and the unit, a value past a soft limit in the warning colour, and
  counts the rest (`and 4 more`) past the page's 12 lines; the page of a
  file load lists the differing values the same way. CHECK THIS CHANGE
  counts from the value of the session's backup, and a confirmed soft limit
  is asked again for a value further from it. The horn lines stand on the
  first write of a session and on every write that changes REVERSION,
  NEUTRAL, PULSE LOWER, PULSE UPPER, LEFT ANGLE or RIGHT ANGLE. A tap on
  **WRITE n VALUES** with the write switch off opens the switch's page. A
  running write shows `Writing 3 registers. Frames sent: 94.` and `Writing:
  do not unplug.` and no interrupted write. A row that takes no tap is
  drawn off. The PORT row names the programming voltage and current limit
  in PWM, and the servo tester's COMMANDED reads `---` while the programmer
  holds the port. The German texts write numbers with a decimal point and
  keep SPEED LIMIT, SPEED and AFTER. The panel sends no ENTER while it
  reads a PD mini output that is on and set under 4.8 V.
- Servo programmer, interrupted writes: a record of a running write is
  stored in `KST/KST.TXT` (at most 400 bytes of text) before the write
  starts; after a link loss, a restart, an abort or a power loss during a
  write the programmer reports that the servo may hold part of it, asks for
  a read and offers the restore. The record names its port, and a servo
  that is not the record's takes no restore and no write: one on another
  port, or one with a register that holds neither the record's value nor
  the write's target. A servo whose 32 registers equal the image before
  the write or the write's target keeps the record, and the panel asks
  whether it is the servo of the write; nothing is written until the record
  is discarded. No backup file is saved while a record is held. A backup whose
  values pass a soft limit shows its warnings on a page of its own before
  the restore page. `KST/KST.TXT` is replaced through `KST/KST.NEW`, and a
  record the card does not take is written again every 5 s. An image that
  fails the layout fingerprint is shown and not written. PROGRAM ends a
  running automatic test. A coprocessor without the programming port's
  page shows the programmer as unavailable with the reason. German texts
  for every new string; 118 screenshots. Host tests: `test_kst_prog`,
  `test_kst_panel`, `test_kst_card`, `test_prog_link`.
  ([Servo programmer for KST and Chaservo servos](docs/ServoProgrammer.md))
- Servo programmer, the port is the programmer's alone: from **CONNECT**
  to the port back in PWM, **PWM TARGET** and **PORT PWM** change by no
  call of the settings (`settings_held()`), the panel writes nothing to
  the port's page and the tester sends the port no command
  (`prog_link_hold()`). A command the control task does not take within
  2 s is withdrawn and dropped when it leaves the queue
  (`kst_prog_gate_t`). A refusal of an exchange after the coprocessor took
  the operation asks for **READ ALL** before an edit, a save or a write. A
  restore, a release or a write that does not start leaves the pending
  values and the record of an interrupted write as they were. A record
  loaded from a card with a servo already read is compared with that servo
  at once.
- Servo programmer, stops and losses: a port found back in PWM without a
  command of the programmer ends the session on the panel with `Session
  ended by the port: supply off or a restart.` and the request to switch
  the servo's supply off and on; the line of a coprocessor restart asks for
  the same. An outcome counts for the request whose number it carries, so a
  late one ends no other operation, and the answer of the card to an older
  record releases no write. **ABORT** reaches an operation the port still
  reports running after the programmer gave it up. An operation is given up
  120 s after its start however long the port reports it running. A code
  the panel has no line for is shown with its number. A save the card ends
  after its 5 s is taken as the file it made. A status read the coprocessor
  refuses with a code other than BAD_PAGE is sent again, and a coprocessor
  that loses the page keeps the port as it was last read. The session marks
  a register as written when its write frame is on the wire, not before.
  **PORT PWM** does not go off during a sweep or a test. A record on a
  card put in later that the programmer does not use stays on the card.
- `tools/sense_trace.py --loaded-channel N` (repeatable): names the output
  channels that have a servo connected. The `$C` and `$D` lines of every
  other channel are no moves and do not count for the 30 ms within which 2
  commands are not replayed; an edge line within 30 ms of such a command
  that no named channel's command takes is left out. The report's second
  line names the channels and counts the lines left out. With 2 servo
  outputs bound and 1 servo connected, the tool replays that servo's
  moves; without the option it replays none, as every command has the
  other output's command in the same frame. Without the option the report
  is unchanged (`test/host/fixtures/sense-trace-sim.report.txt`).
  ([First run](docs/FirstRun.md) §8.9)
- `protocols/kst/`: the protocol core for programming KST and Chaservo
  servos over the signal wire. Frame coding, the reply decoder with one
  result code per failed check, the 32-register image with raw and displayed
  values, a layout fingerprint, 24 hard and 25 soft limits, write planning
  for fields that lie in 2 registers, and a non-blocking session that reads
  every write back and undoes a plan whose write does not take. The module
  includes C standard headers only and reaches the wire through a driver of
  3 functions. The host suite runs it against a servo and
  line model (`test_kst_wire`, `test_kst_reg`, `test_kst_plan`,
  `test_kst_session`) and links its 6 headers from C++11
  (`kst_headers_cxx`). The timing
  and the register layout come from 1 programming card and 1 servo of
  unidentified model, and the effect of a register on servo motion is not
  measured. ([README](protocols/kst/README.md))
- `protocols/kst/rp2350/`: the RP2350 pin driver for the signal wire of
  KST and Chaservo servos. 1 PIO
  (programmable input/output) state machine with a program of 24 words and
  2 DMA (direct memory access) channels per line, no interrupt. It clocks a
  frame out at 25.40 us per half-cell, releases the pad, stamps every level
  change of the reply window at 26.7 ns and drives the pad low again. The
  open refuses a system clock that misses the half-cell by more than 0.2 %.
  `test_kst_pio` runs the program's words in a model of the state machine,
  and the coprocessor build fails when the assembled program differs from
  those words.
  ([README](protocols/kst/README.md#rp2350-pin-driver))
- **A programming port for KST and Chaservo servos on the coprocessor.**
  A programming port is an output of its own kind with its own pin, no
  standard servo port. Link page PROG (0x33) holds 4 ports, 1 in the
  bring-up build, on GP13 unless another pin is stored. In PWM
  (pulse-width modulation) the port renders a servo pulse from the bank's
  generator: a manual command, the SERVO page's sweep and hold through
  `TARGET`, and a SERVO_SENSE capture reach it, and a `PWM` bit switches
  the pulse. A port's pin and a slot's pin are refused both ways.
  Link page KST (0x32) runs the programming session on one port: the port
  renders no pulse for 50 ms, the pin is held low for 100 ms, then the wire
  driver has it. A port is PWM or programming, never both. The servo's
  supply is the PD mini of the SUPPLY page: ENTER switches it on at the
  voltage the panel stages, 1000 to 8400 mV, or lowers an output that is
  on above it, and never raises one. POWER_CYCLED and the operation REBOOT
  switch it off for 2000 ms and on again, and the port returns to PWM
  1000 ms after it reads on. Both times are set values; what a KST servo
  needs is not measured. With no PD mini answering the port switches
  nothing and POWER_CYCLED is the operator's word. The supply ends off
  when the heartbeat or the link is lost.
  The session runs on the coprocessor with the core's planner and limits:
  the panel sends an operation and a target image, not register writes. A
  write, a restore and a pairing release are refused unless the command
  carries the key 0x57A5, which the panel sends only with its write switch
  on. A stop, a disarm, a lost heartbeat or a silent link ends the running
  operation after the frame on the wire, at most 12 ms, and the port then
  accepts a read of all registers before any write.
  `shared/bench/kst_link.c` is the panel's half: it stages the images and
  the supply, sends the command, polls every 50 ms and fetches the image
  and the results. The servo programmer of the SERVO screen is its caller.
  The port plans a WRITE and a RESTORE after the command's frame, one
  slice of the planner in each pass of the coprocessor's main loop: bit 3
  of register 0 is set and the result reads 35 while it plans, and a
  request ends with result 33 when no order of writes exists and 34 when
  216,000 units of planner work are spent first. A stop, a disarm, an
  ABORT, a lost heartbeat, a silent link or a supply found off ends the
  planning at the next pass with nothing sent.
  LEAVE (operation 10) returns a port from PROGRAMMING to PWM without
  switching the supply: the port holds the line low for the wait in write
  register 28, 0 to 10,000 ms and 100 ms until a panel writes it, then the
  PWM generator has the pin. A wait above 10,000 ms is refused and not
  clamped. A LEAVE is refused while the port runs or plans an operation,
  while a READ_ALL is owed and while only RESTORE is accepted. In the wait
  an ABORT or a disarm returns the port to PROGRAMMING, and a supply that
  reads off ends it as under a session. Not measured on a KST or Chaservo
  servo: the wait it needs, whether it follows PWM in programming mode,
  and whether a written value acts before its next power-up.
  The link protocol is 4.15; PROG and the ports are 4.13, the planning in
  slices 4.14. Every combination of a 4.15 and an older panel and
  coprocessor links and arms, nothing is sent to 0x32 or 0x33 unless both
  are 4.13 or later, and no LEAVE is sent unless both are 4.15.
  Host tests: `test_kst_port`, `test_prog_port`, `test_kst_link`. No pulse
  of a port is measured.
  ([Link](docs/LinkKST.md))
- **A servo configuration as a text file.** `protocols/kst/kst_file.c`
  writes the 32 registers of a KST or Chaservo servo as 35 lines of ASCII
  (American Standard Code for Information Interchange) text, fewer than
  1024 bytes: one line per field with its value and unit, the registers
  in hexadecimal and a CRC-32 (cyclic redundancy check) line. A file with
  a fitting checksum is read from its registers; one with a wrong
  checksum is refused as damaged; one without the line is read as a
  person's input, field line by field line, with 15 refusal codes and the
  line number. `kst_file_load()` holds a file against the connected
  servo: a file of the other servo type is denied, only editable fields
  are taken, a hard-limit violation refuses the file and names field and
  line, soft-limit findings are returned, and a locked field that differs
  is reported and not written. `kst_type()` gives the servo type, V6 or
  V8, from `20k_sel` (register 0x04 bit 6); the mapping rests on the
  manufacturer's stated defaults and is read on 1 servo.
  `shared/logfile/kst_store.c` keeps the files in the directory `KST` of
  the card as `<name>.TXT`: it proposes `KST001` to `KST999`, checks a
  name of 32 characters or fewer with 5 distinct refusals, never
  replaces a file, and lists the files sorted by name. The name `KST`,
  in any case, is refused and `KST.TXT` is left out of the list: the file
  is a caller's record of a write under way. FILES of the servo
  programmer uses both. Host tests: `test_kst_file`, `test_kst_store`.
  ([README](protocols/kst/README.md#configuration-file))
- **A trademark notice and a disclaimer.** `README.md` and `README-de.md`
  carry the section "Legal notice and disclaimer": KST, CHASERVO and Hitec
  are marks of their manufacturers, the names are used descriptively for
  compatibility, the project has no connection to the manufacturers, and
  the author accepts no liability, to the extent the law permits, for
  damage to servos, to connected hardware or from operating the hardware.
  `NOTICE` has the section "Trademarks" with the same paragraph and one
  sentence for every other product and company name;
  `protocols/kst/README.md`, the 7 public headers of `protocols/kst`, its
  pin driver's header and `protocols/openyge/include/openyge.h` carry a
  short form. The protocol of KST and Chaservo servos is implemented from
  wire captures of hardware the project owns, with no vendor code or
  documentation; the licensing rule in `CONTRIBUTING.md`, the manifest,
  `STATUS.md`, `README.md` and `NOTICE` names captures of the wire beside
  published specifications. The documents name the brands before a generic noun
  ("KST and Chaservo servos") and write Chaservo in full.

### Changed

- The documentation no longer marks single features as run or not run on
  hardware. The READMEs and `docs/Safety.md` state once that the project is
  under development and its functions are not verified on hardware.
- The READMEs and the wiki home link the bench configurator at
  https://subtilitas.github.io/rcbench/ in place of the build guide PDFs.
  `release.yml` attaches the 2 image archives and `SHA256SUMS` and no
  longer carries the PDFs over from the latest release.
- **The write planner for KST and Chaservo servos works in slices and a
  session plans its own writes.** `kst_plan_edit_begin()` and `kst_plan_restore_begin()` start a
  job in 176 bytes the caller holds, and `kst_plan_step(job, budget)`
  returns `KST_PLAN_BUSY` after `budget` to `budget + 9` units of work, a
  unit being a fixed piece of the search. `kst_session_edit()`,
  `kst_session_restore()` and `kst_session_release_pairing()` plan inside
  the session, 100 units per `kst_session_step()` call, and run the plan
  that results: 1 planner run per operation, and the session executes no
  plan but the one it built. On a Cortex-M33 at 150 MHz a slice costs at
  most 892 us and a begin call 64 us, computed in an emulator from the
  instructions executed with ARM GNU 14.2 at -O3 and not measured on
  hardware. A session ends planning after 216,000 units, at most 2,161
  slices, with `KST_SES_ERR_NO_PLAN` and `KST_PLAN_ERR_TOO_COMPLEX`; the
  costliest request found takes 120,136 units, and a costlier one is not
  ruled out. An abort while the session plans ends the operation with
  nothing sent. The session object is 848 bytes on a 32-bit target, up
  from 648.
  `kst_plan_edit()`, `kst_plan_restore()` and `kst_session_write()` remain
  and run the same job to its end without a cap. Every plan is the same
  byte for byte as before: the pinned digests of `test_kst_plan` and
  `test_kst_session` are unchanged, and 6,300,000 random requests give the
  plan of the planner this one replaces. The search skips work whose
  result is known: the costliest run found takes 397 to 647 ms by the
  same estimate, down from 1.2 to 3.0 s.
  The programming port of the coprocessor (`shared/outputs/kst_port.c`)
  calls `kst_session_edit()`, `kst_session_restore()` and
  `kst_session_release_pairing()`: a write command starts the planner's
  job, and each pass of core 0's main loop runs 1 slice. A write command
  reaches 1,304 bytes of stack and a pass that plans 1,016. The time of a
  pass is not measured on hardware.
  ([Performance](docs/Performance.md#the-iomcu))
- **The write planner for KST and Chaservo servos takes 488 bytes of
  stack, down from 1,272.**
  Measured by `tools/stack_check.py --iomcu` from `kst_plan_edit` down on
  the coprocessor image built with ARM GNU 14.2; 176 of them are the job
  state. The search over write
  orders is a loop over one working image with three 7-byte arrays, not a
  recursion of 7 levels with an image copy each, and the search for an
  extra write keeps a 256-bit set of candidates in place of a 512-byte
  cost table. Every plan is the same byte for byte: `test_kst_plan` pins a
  digest over the result, the violated rules and the whole plan of
  495,068 planner calls. No function signature differs.
  ([Performance](docs/Performance.md#the-iomcu))
- **`kst_session_write()` takes 56 bytes of stack, down from 256.** The
  session builds the planner's plan for the comparison into its own copy
  of the running plan, which no operation reads at that time, and holds no
  second 208-byte plan in its frame. A write command to the
  programming port for KST and Chaservo servos reaches 1,432 bytes of core 0's stack with ARM GNU
  14.2, down from 2,416, and is no longer core 0's deepest chain: that is
  a request that binds an output, 1,556 bytes of 4,096 on the image CI
  builds, down from 2,392. The session takes and refuses the same plans
  with the same results: `test_kst_session` pins a digest over the answers
  to 120,671 calls with the planner's plans and with every single-member
  change of each. One answer is added: a plan that is the session's own
  copy, or overlaps it, is refused with `KST_SES_ERR_ARG`.

- **Every interface to external hardware is a module under `protocols/`.**
  `protocols/dshot`, `ppm`, `sbus`, `openyge`, `pdmini` and `phase_tap`
  each hold a core in C11 that includes C standard headers only, the
  RP2350 pin driver in `rp2350/` where one exists (`dshot`, `ppm`,
  `pdmini`, `phase_tap`) and a `README.md` with the CMake lines for another
  pico-sdk project. The driver of a module is the INTERFACE library
  `rcbench_<name>_rp2350`. `openyge` carries its CRC-16/XMODEM
  (`openyge_crc()`), `ppm` its channel range (400 us to 2500 us) and the
  DShot and PPM drivers their pin count (8, set by `OUT_DSHOT_MAX_PINS`
  and `OUT_PPM_MAX_PINS`); `outputs_hw.c` holds each equal to the output
  bank's constant at compile time. No wire, link page or public function
  name differs, and the four PIO programs assemble to the same words. The
  rule is in `CONTRIBUTING.md`, "Where code goes".
- **`tools/check_protocols.py --check` holds the modules to the rule.** It
  fails on an include that leaves a module, on a pico-sdk header outside
  `rp2350/` or in a driver's header, on a public header without
  `extern "C"` or one that does not compile by itself as C11 and as C++17,
  on a module without a `README.md`, and on a CMake file that names another
  module. CI runs it in the host suite job.
- **Makers and brands are named descriptively, and the trademark notice
  covers every name in the tree.** The docs, the hardware pages and the
  UI name the rcbench part generically and the brand as whose product it
  works with ("servo programmer for KST and Chaservo
  servos", "the 9 profiles for YGE ESCs", "parameter table for Hitec
  servos"). The section "Legal notice and disclaimer" of `README.md` and
  `README-de.md` has a second paragraph after the one for KST, CHASERVO and
  Hitec: it names 5 groups (servo makers, ESC makers, protocol and firmware
  names, board, module and component makers, distributors) and covers every
  other maker, product, protocol and firmware name. `NOTICE`, "Trademarks",
  lists the names in 6 groups with the same statement. The public headers
  of `protocols/sbus`, `protocols/pdmini` and `protocols/dshot` carry the
  short form in their top comment, and the READMEs of `protocols/openyge`,
  `sbus`, `pdmini` and `dshot` have a section "Trademarks". The header and
  the README of `protocols/dshot` and `NOTICE` state DShot as the name of a
  protocol, without an owner or a trademark, and that the project claims no
  right in the name. `NOTICE` states that the project includes no logo or
  artwork of another party as its own artwork and that photographs of parts
  show the parts as their makers mark them. The footer of the configurator
  page links to `NOTICE` as "Trademark notice", in German "Markenhinweis".
  The name in `test_settings` is a generic one. The SETUP help lines of
  "INA228 address" and "INA3221 address" name the module of a maker, "Matek
  module" and "DAOKAI module", in German "Matek-Modul" and "DAOKAI-Modul",
  within the 36 cells of the help field. The device line of the servo
  protocol on the PROGRAMMER screen names the maker and the series, "Hitec
  servo, D-series", in German "Hitec-Servo, D-Serie", and no model.
- Configurator: below 1024 px width a control that takes the focus scrolls
  to above the bottom bar. The page sets `scroll-padding-bottom` and the
  space under the footer to the rendered height of the bar plus 16 px,
  73 px with labels of 1 line. The keyboard tests of the 4 phone cells in
  `e2e/matrix/keyboard.spec.ts` run (60 tests).

## 0.16.0 - 2026-10-10

The link protocol is 4.11. A 0.15 board still links and arms with a 0.16
one, since only the protocol's major number has to match. The SERVO_WIN page,
the INA3221 as the servo rail's meter in the automatic servo test and in the
bench log, and the `RESETS` count need both boards at 0.16: a 0.16 panel
with a 4.10 coprocessor reads the PD mini as the meter and takes the
INA3221's windows from SERVO_SENSE, and a 4.10 panel never reads the new
page. The sensor pins are a pair: a 0.16 panel offers only pairs and works
with either coprocessor, and a 0.15 panel paired with a 0.16 coprocessor
sees the pins it sets refused at the edit. Flash both boards. The automatic
servo test reads the INA3221's CH1 when it is the rail's meter, writes a CSV
file of 19 columns (21 with the AS5600), and ends a run in which the supply
reports constant current for 1 s. Sensor SCL follows Sensor SDA, a current
monitor that resets itself is found and set up again, and coprocessor core 0
runs on a 4096-byte stack. A coprocessor debug build, `-DSENSE_TRACE=ON`,
prints the servo current's 1 ms samples, which `tools/sense_trace.py` reads
and replays. CI and the tools hold more to the tree: `tools/mutate.py`,
tests for the tools, `stack_check.py --iomcu`, `ci_gate.py` and the numbers
`check_docs.py` takes from constants. Nothing in this release has run on
hardware: the changes have run on the host suite only, the coprocessor's
stack is not measured on a board, and whether either current monitor resets
on a supply dip is not measured.

### Added

- **A coprocessor debug build prints the servo current's 1 ms samples.**
  Built with `-DSENSE_TRACE=ON`, the coprocessor writes INA3221 CH1's 1 ms
  samples and CH1's bus voltage as text on its USB console: for 4 s after
  a PWM output renders another pulse width or after a capture edge, and
  for 10 s after `t` on the console. A trigger during a trace extends it.
  Core 1 samples as in the released image and copies each sample into a
  ring of 4096 records (49,152 bytes); no bus transaction is added. A
  change of the part's set-up or state is a record in the same ring and
  keeps its place among the samples, with the start time of the tick it
  was seen in. A trace whose lines a connected console has not taken
  29.8 h after its end is dropped. Core 0
  writes whole lines into the room the console's 64-byte transmit buffer
  has, and nothing without a terminal. A trigger line and the end line
  wait until core 1 has finished a tick that started at or after their
  time, 100 ms at most; an end line written without that carries its
  reason as a capital letter, and the tool reports the trace as a
  problem. A full ring drops records, and the
  trace says where and how many; its end line counts every one from
  before its end. A trace is about 8.6 bytes a sample. The
  option is off by default: a released image holds none of this code, and
  CI fails when it does. `tools/sense_trace.py` reads every line of a
  captured console against the format and each trace against its end
  line: a line inside a trace that is neither a whole line of the format
  nor a console line starting `rcbench-iomcu:` is damage, the trace is
  reported, none of its moves is replayed and the exit code is 1. The
  format has no check value, so a line cut inside its last number is read
  as cut. The tool writes a CSV file per trace, and replays
  every move through the move rules (`shared/servo/servo_move.c`) with the
  filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A. From
  the traces alone it prints which settings see and time every move, each
  setting's median arrival and how far apart the settings put one move's
  arrival, and says that no arrival is compared with the horn. With a
  servo test's CSV file recorded with the AS5600 it prints each arrival
  less the encoder's travel time and the median per setting. A move is
  not replayed when a window its levels come from has no sample, missing
  records or a change of the part's state; a clipped sample in a level
  counts as 4094 steps, as in the replay.
  [First run](docs/FirstRun.md) §8.9 has the bench procedure in two parts,
  the first without an encoder. Host suite only; not run on hardware. Not measured:
  the time the lines add to a pass of core 0.
- **`tools/mutate.py`.** Changes one line of `shared/` at a time in a copy
  of the tree (a comparison flipped, a bound plus or minus 1, a stored
  assignment dropped), builds and runs the host suite and reports the
  changes the suite passes with. By default the lines changed since
  `origin/main`, at most 60 mutants, a run within 2700 s. CI runs it on
  pull requests and does not fail on a survivor.
- **Tests for the tools.** `test/tools/` holds pytest cases for
  `coverage.py`, `check_docs.py`, `check_sanitizers.py`, `render_ui.py`,
  `stack_check.py`, `mutate.py`, `ci_gate.py`, `pinmap_check.py` and
  `wiki_links.py`; CI runs them in a job of their own.
- **A stack check for the coprocessor.** `tools/stack_check.py --iomcu`
  reads each core's deepest call chain and the deepest interrupt handler
  out of the RP2350 image and fails when chain, one interrupt and 256 bytes
  exceed the core's stack. The pico-sdk's `panic()` and what it calls to
  print are part of a chain. With `--check-doc` it holds core 0's row of
  the table in `docs/Performance.md` and its German page. CI runs it after
  the coprocessor build.
- **The coverage figure in the READMEs.** `README.md` and `README-de.md`
  state the total, the line counts and the two floors; `tools/coverage.py`
  writes them and `--check` fails when they differ from the measurement.
- **`tools/ci_gate.py`.** Waits for the CI run on a commit and fails unless
  one passed.
- **The coprocessor keeps the last 4 windows of the servo's current.** The
  new read-only page SERVO_WIN (0x31, protocol 4.11) holds the last 4
  complete 50 ms windows of INA3221 CH1, 200 ms, newest first: for each the
  mean, highest and lowest current in mA, the mean and lowest bus voltage in
  mV, whether a window closed with that number, and how many of its samples
  read an end of the range. SERVO_SENSE shows the last window alone, and a
  host that reads it more than 50 ms apart misses windows. A clipped sample
  counts in SERVO_WIN at the end of the range, 1.638 A or -1.6384 A on the
  0.1 Ω shunt, and is counted; SERVO_SENSE's figures leave it out as
  before. The page also carries the capture's state and count. Host suite
  only; not run on hardware.
- **The bench log has every 50 ms window of the servo's current.** On a
  4.11 coprocessor with INA3221 CH1 enabled the panel reads SERVO_WIN every
  poll and takes each window number once, in order. A poll that brings more
  than one window writes one row for each: the rows of one poll carry the
  same `time`, the sample is in the last, and the rows before it hold the
  window alone. No window is lost while two reads lie at most 200 ms apart,
  and a reply lost on the link loses none; beyond the ring's 4 windows the
  lost ones are counted. A log read from SERVO_SENSE alone, as with an
  older coprocessor or with CH1 off, skips 1 window in 17 at a 53 ms poll.
  CH1's figures in the log count a clipped sample at the end of the range,
  1.638 A on the 0.1 Ω shunt. The read costs 6 CAN frames more per poll,
  0.93 ms of bus time, 16 frames in place of 10.
- **The panel names the servo rail's meter.** Once per poll it is the
  INA3221's CH1, the PD mini or the model: the INA3221 after 1000 ms in
  which it is on with CH1, the coprocessor speaks 4.11 and holds the
  set-up, the part is online and identified on a bus that is not stuck, a
  window with current samples closed in the last 200 ms and its reset count
  stands. The band says `coprocessor older than 4.11 -- servo current read
  from PD mini` once per link-up for a 4.7 to 4.10 coprocessor, and
  `INA3221 reset itself -- check its supply` once per reset. Host suite
  only; not run on hardware.
- **The profile generator and the card reader are held to one verdict.**
  The host suite's `esc_parity` test (`test/host/fuzz_parity.py`, Python 3)
  changes the profiles of record in structure and in text, 1500 cases from
  each of 2 fixed seeds, and fails on a file `tools/gen_esc_profiles.py`
  and `esc_profile_parse()` read differently. 6.2 s on one x86-64 host,
  8.1 s in the sanitizer build; ctest stops it after 120 s. It is not in
  the coverage build. `tools/mutate.py` copies the generator beside the
  suite.
- **Heartbeat cases at the limits.** `test_heartbeat` holds an interval of
  3, 4, 150 and 151 ms, the 150 ms of silence, the run of 4 good intervals,
  the counters, the generator's 20 ms period and its resume after a stop,
  each at tick 0 and across the wrap of the millisecond count. The suite
  fails on each of the 36 mutants `tools/mutate.py` makes of
  `shared/safety/heartbeat.c`; `heartbeat.c` is unchanged.

### Changed

- **Coprocessor core 0 runs on a 4096-byte stack.** `PICO_STACK_SIZE` is
  0x1000 in `firmware/iomcu/CMakeLists.txt`; the pico-sdk's default is
  2048 bytes. The deepest chain from `main()` is 1516 bytes and one
  interrupt 528. The stack fills the RAM region `SCRATCH_Y`, 0x20081000 to
  0x20082000.
- **The wiki and a release wait for CI.** `docs.yml` and `release.yml`
  start as before, on a push to `main` that touches `docs/` and on a `v*`
  tag, and their first step waits for the CI run on the same commit, at
  most 5400 s. Nothing is published when that run fails, is cancelled or
  does not exist after 600 s. A manual run is held to the same rule.
- **`coverage.py --check` fails on a source that is not measured.** A C
  file under `shared/` that is in neither of the tool's two lists, that the
  suite does not compile, or that no test links fails the check; a file no
  test links is measured at 0%.
- **`check_sanitizers.py` holds the flags whole.** Every compile command of
  the sanitizer build carries `-fsanitize=address,undefined`,
  `-fno-sanitize-recover=all` and `-fno-omit-frame-pointer` and no other
  flag of that family; a build with AddressSanitizer alone fails.
- **`check_docs.py` holds numbers to constants.** The protocol version, the
  heartbeat and link timings, the heartbeat and CAN pins, the coverage
  floors and the stack margin the pages state, every table row that names
  a C constant, the frame-cost ceiling table against `ci.yml`, and the IO
  board's pin counts against `pinmap.json`.
- **`render_ui.py --fit` fails on an English overflow.** Three English
  overflows the layout has are listed by name in the tool and printed as
  `known`; any other fails, in either language.
- **`stack_check.py --check-doc`.** Holds the task table and the count of
  calls through a pointer in `docs/Performance.md` and its German page to
  the ESP-IDF v5.4 build; the count reads 176.
- **CI.** `pinmap_check.py` runs in the coprocessor job. cppcheck's
  warning, performance and portability classes run over `firmware/`. A
  failed Codecov upload fails the host job. The CI run of a push to `main`
  is not cancelled by a later push.

- **The automatic servo test reads the servo rail's meter, and one meter
  per run.** A run started while the INA3221 is the rail's meter and the
  supply is the PD mini reads CH1's 50 ms windows, 20 a second: idle and
  holding current from the windows that lie wholly in the phase, the peak
  from a window's highest or lowest 1 ms sample, the step's voltage and the
  new `V min` column from CH1's bus voltage at the load side of the shunt.
  The supply is read for its state only, and none of its voltages or
  currents is used or logged. Moves are timed by the panel from the
  windows: a travel time is late by up to 145 ms, an upper bound, and
  TRAVEL TIME is reported and not checked, as on the PD mini. A run on the
  INA3221 ends ABORTED, the output off and the servo released, when the
  INA3221 stops being the meter (`INA3221 reset itself`, `INA3221 not
  answering`, `INA3221 window without current`, `INA3221 set-up not held`)
  and when no window reaches it for 500 ms; it never goes on with the PD
  mini. A run that starts on the PD mini reads it to its end. The report
  names the supply, the meter of the current and of the voltage, the shunt
  and its range, and, on the PD mini with the INA3221 on in SETUP, why the
  INA3221 was not read. The result box on the SERVO screen names the meter.
  Windows that one read brings carry times 50 ms apart, each the latest it
  can have closed.
  A negative current is a reading: the limits, the peak and STALL AT take
  its magnitude, the CSV keeps the sign, and a run whose idle current is
  below -0.020 A says `Current reads negative at rest: shunt direction`. A
  window with a sample at an end of the INA3221's range is a reading like
  any other; the CSV counts its clipped samples and the report the clipped
  windows. Host suite only; not run on hardware.
- **A supply in constant current for 1 s ends a servo test.** A supply at
  its current limit reads the limit, so no reading was above a STALL AT at
  or above that limit, and both default to 2.00 A: a servo on a stop ran the
  whole test and read PASS. A run ends `constant current for 1 s` once the
  supply reports constant current in every reading for 1000 ms, on either
  meter and whatever STALL AT is. A shorter spell ends and fails nothing;
  the report's `Const. current` line counts its readings and gives the
  longest stretch. A STALL AT at or above the current limit, or at or above
  the INA3221's range (1.638 A on the 0.1 Ohm shunt, under the default
  2.00 A), does not stop START TEST: the TEST page and the report say
  `STALL AT 2.00 A cannot be reached: current limit 2.00 A`. Not measured:
  what a PD mini reports with a servo on a stop.
- **A reading equal to a LIMITS value passes it.** IDLE CURRENT, HOLD
  CURRENT and STALL AT are compared in whole mA, by one function the
  verdict and the report share. A reading of 0.050 A against a limit of
  0.05 A read FAIL for 138 of the 500 values a limit takes, and for 8 of
  STALL AT's 99, because the setting and the reading are different floats
  for the same number of mA.
- **The servo test's CSV names its meter.** Six columns follow the ones a
  file had: `meter` (`INA3221`, `PDMINI` or `MODEL`) on every row, and on
  the INA3221 `window`, `current max (A)`, `current min (A)`, `voltage min
  (V)` and `clipped`. The first 13 columns, and the two of the AS5600 after
  them, keep their places; the log viewer reads files with and without the
  new columns. The report's step table has a `V min` column.
- **One CSV per servo test.** The armed bench's own log writes no row while
  a servo test runs; its time column steps over the test and its rows start
  again when the test ends. The SERVO screen tells the panel of a run's
  start and end in the call that makes them.
- **The SERVO screen shows the servo rail's meter.** The CURRENT row, the
  line over the plot and the plot show the reading of the meter the panel
  names: with the INA3221 as the meter, CH1's 50 ms windows -- the mean
  current, the mean bus voltage at the load side of the shunt and their
  product, one point of the plot per window -- under the name
  `INA3221 CH1`; otherwise the supply's samples under `SUPPLY`, the PD
  mini's or the model's. The CURRENT row no longer shows the feedback's
  current, which no firmware supplied: it read `---` on every panel. A
  current is signed, in hundredths of an amp, and `-0.02 A` is shown as
  that. A window with a sample at an end of the INA3221's range shows its
  mean in the warning colour. A reading that did not arrive is `---` in the
  row and `--` on the line; a link that goes down takes the INA3221's last
  window with it. The plot is emptied when the INA3221 becomes or stops
  being the meter, and each window and supply sample carries the meter of
  the poll that took it, so the trace holds the readings taken under the
  meter it names and no others. The row's value is repainted by itself when its digits
  or colour change; `frame_cost.py servo-current` measures an armed frame
  with a window at 4,056 cache-line fills and CI holds it to 4,500. Host
  suite only; not run on hardware.
- **Sensor SCL follows Sensor SDA, and a set-up the coprocessor does not
  hold is marked on its row.** On SETUP > INTERFACES Sensor SDA steps
  through −1, 0, 4, 6, 14, 16, 18, 20 and 26 and stops at both ends: −1
  for no sensor bus, and the SDA pins of an I2C block's pair that the
  RP2350-CAN brings out and does not reserve. Sensor SCL is the GPIO after
  it, shown without keys. A stored `sns_scl` is not read; a stored
  Sensor SDA outside those values loads as 16. A row whose value the
  coprocessor's page does not hold (not written yet, or refused) has its
  edge and value in the warning colour and reads `Not taken by the
  coprocessor`, until the page holds it: the current monitors' rows, the
  two pins, AS5600 and the phase tap's rows. The 30 s alert is unchanged.
  The coprocessor's SENSE page refuses SDA and SCL that are not one I2C
  block's pair whatever ENABLE holds; with ENABLE 0 it took them and
  refused the part enabled afterwards. ENABLE 0 with SDA or SCL 0, a
  panel's pins that are not set, is taken as before. No register changes;
  the protocol stays 4.11. Host suite only; not run on hardware.
- **The INA3221 address hint names the A0 bridge.** The DAOKAI module ships
  with all four A0 solder bridges open and its address floats between 0x40
  and 0x41; 0x40, the default, needs A0 bridged to GND. The hint on SETUP
  and the INTERFACES table say so.
- **A capture's holding level is a signed register.** SERVO_SENSE's
  `CAP_HOLD_MA` takes -32768 to 32767 mA where it took 0 to 32767 and
  refused the rest: an end that holds near 0 A reads below zero by the
  INA3221's offset and can be armed. A value of 0 to 32767 means what it
  meant. The panel arms no capture.
- **The die temperature and the INA3221's flags are read 25 times a
  second.** The INA228's DIETEMP and the INA3221's Mask/Enable give every
  second of their 50 Hz slots to the read-back of a set-up register. Every
  other read keeps its rate, and no tick takes more bus time: 690 µs at
  most on an even tick, 742.5 µs in the tick that reads a set-up register a
  second time.

- **The throttle changes only on an armed bench.** On MOTOR & ESC the
  slider, its `-1` and `+1` steps and the rotary knob are refused while the
  bench is not armed: nothing is sent, and the slider and the readout stay
  at 0.0 %. They are drawn dimmed and the line under the track reads
  `ARM FIRST`. Not armed runs from the disarm until the bench reports armed,
  so a control touched between a completed ARM hold and the bench's answer
  is refused too, and so is one touched from a DISARM the screen asks for,
  or a STOP on an armed bench, until the bench reports disarmed. SERVO's
  position controls follow the same rule, from one definition both screens
  read (`ui_value_gate.h`), and a sweep, a pause, a held position and a run
  of the automatic test end where the DISARM or STOP is asked. The screen returns the throttle to 0 % at every disarm
  itself and drops a throttle posted and not yet sent. A knob turn made
  while not armed is not kept. Host suite only; not run on hardware.
- **RELEASE on SERVO sets the value shown to the rest.** On an armed bench
  RELEASE puts the pins at the rest, and the commanded value becomes that
  rest, as at an arm: 1500 us for STANDARD PWM, 760 us for NARROW 760,
  without TRIM. A drag under way ends, and the first drag or knob turn after
  it starts from the rest, where it counted from the position before the
  release. The release a run of the automatic test ends with and the one a
  changed type sends on a resting bench do the same. On a disarmed bench
  RELEASE is sent and the value stays. Host suite only; not run on hardware.
- **A servo position changes only on an armed bench.** On SERVO the dial,
  the rotary knob, CENTRE and SWEEP are refused while the bench is disarmed:
  nothing is sent, and the horn and COMMANDED stay at the position last
  driven. CENTRE is drawn dimmed with SWEEP, the horn is drawn dimmed and
  the left card reads `ARM FIRST`. At every arm the commanded value becomes
  the rest the pins drive, the midpoint of the channel's endpoints (1500 us
  for STANDARD PWM, 760 us for NARROW 760), and the first drag or knob turn
  starts from it. A knob turn made while disarmed is not kept. A drag ends
  at an arm, a disarm and a stop, and a position, a sweep or a pause posted
  and not yet sent at a disarm or a stop is not sent. A bench disarmed while
  ARM's flash runs shows ARM in its own green. Host suite only; not run on
  hardware.

### Fixed

- **`tools/gen_esc_profiles.py` refuses two malformed profiles it ended
  on with a Python exception.** An `applies_to` entry that is a list or an
  object raised `TypeError`, and a file nested about 1,000 levels or deeper
  raised `RecursionError`. Both are refused with the generator's own
  message, as the card reader refuses them; nesting is checked before the
  NUL check, so a file with both faults names the nesting.
- **A current monitor that resets itself is found and set up again.** An
  INA228 or INA3221 whose supply dips answers every read afterwards on its
  power-on set-up, and stayed online on it until the coprocessor restarted
  or the set-up was written again. The INA228 then reads a quarter of the
  current on the default MATEK set-up (200 µΩ, 204.8 A, ADCRANGE 1), with
  the valid flags set and its charge and energy counting from 0 as the
  run's: 25 A for 100 A in the host model. The INA3221 gives one CH1 result
  every 6.6 ms where the bench reads one every 1 ms. The coprocessor reads
  the INA228's ADC_CONFIG and the INA3221's Configuration back every 40 ms.
  A value other than the one written, read twice in a row, takes the part
  offline, empties the 50 ms window being filled from it, ends the INA228's
  charge and energy for the run, ends a move capture as lost, and counts in
  SENSE register 31 (`RESETS`, protocol 4.11: INA228 low byte, INA3221 high
  byte, each modulo 256). The part is set up again 1000 ms later. Samples
  read in the up to 40 ms before the reset is found stay in the window that
  closed in that time. Host suite only; whether either module resets on a
  supply dip is not measured.
- **Scrolling the SETUP list changes no value.** A `-` or `+` key stepped
  its setting when the finger came down, and the list scrolls by a drag that
  starts anywhere on it, the key columns included (`-` at x 556 to 599, `+`
  at x 726 to 769, the whole height of the list). A scroll swipe that
  started on a key column had stepped the setting under the finger before
  it scrolled: INTERFACES scrolled twice from its bottom row showed INA3221
  shunt 99.9 mOhm and Sensor SDA 15, one step under their defaults, and a
  stepped value is in force at once. A key is a tap: it steps once when the
  finger that pressed it lifts inside it, no more than 8 px from the press
  in x and in y. A key held steps for the first time 0.45 s after the press
  and then as before (8 a second, 30 a second from 2.25 s; 5 steps after
  1.00 s, 38 after 3.00 s); the lift after a hold adds none. Sideways travel
  of more than 8 px gives the key up. A second finger does nothing on SETUP
  while one presses or scrolls. A key is its column over its own row; the
  4 px between two rows step nothing. Host suite only; not run on hardware.
- **PROGRAMMER's controls take a tap.** Every control acted when the finger
  came down, DEFAULTS of the stick timing and its step keys included, so a
  finger that came down on one and travelled on had pressed it. A control
  acts when the finger lifts inside it, no more than 8 px from the press.
  ABORT of a stick run still acts on the press, HOLD TO RUN keeps its 2 s
  hold and the search keyboard its keys.
- **A LOGS button acts where it was pressed.** RESCAN, OPEN, DELETE, the
  import view's buttons and the plot's footer acted on the lift wherever the
  finger was: a press on DELETE that slid away still asked the delete
  question. A button or a row acts when the finger that pressed it lifts
  inside it, having moved no more than 8 px. A second finger on the file
  list or the import view does nothing.
- **A release the panel makes presses nothing.** A touch carried from the
  screen into the status band is released for the screen at its last point
  there, and a contact that is more than 120 px from its previous report is
  released where it was. Both could land on the control that was pressed
  and activate it: a tile or a tab carried into the band, a keypad key under
  a contact that jumped. Such a release ends the press or the drag and
  activates no tile, tab, key, button or row, on every screen. STOP, DISARM,
  OUTPUT OFF and START TEST while a servo test runs take it as before. The
  press that follows a jump is a press, except on a key of the SETUP list,
  where it scrolls and steps nothing.
- **An armed bench's log has a row for every sample, and its time column is
  wall time.** The panel wrote a row only in a pass of its control task in
  which the 50 ms poll gate and a second, independent 50 ms gate were both
  open. A command's write ahead of the poll shifted the 5 ms passes against
  the two gates, and no row was written until they lined up again. Two runs
  on hardware with firmware 0.14.0 showed it: 2508 of 3817 INA3221 windows
  (65.7 %) not logged in a run of 190.8 s, with gaps of up to 605 windows
  (30.25 s), and 1712 of 2986 (57.3 %) in a run of 149.3 s; 28 of 28 and 20
  of 21 gaps began within 0.15 s of a servo command. The time column grew
  only with a written row and ended at 69.0 s for the 190.8 s run. Every
  armed run's log was thinned this way, a motor run's included. A row is
  written for every sample the poll takes, and `time` is the wall time since
  the arm, across the tick's wrap at 2^32 ms; a stretch without samples and
  rows dropped by a full queue show as a step in it. Charge and energy were
  integrated on the sample's own interval and are unchanged. A supply run's
  log and a servo test's CSV have one gate each and are unchanged. The rule
  is `shared/bench/log_cadence.c`, tested on the host against a model of the
  pass grid; it has not run on hardware. Single windows are still skipped
  where the poll (52.9 ms measured) is longer than the 50 ms window.

## 0.15.0 - 2026-10-09

The link protocol is 4.10. A 0.14 board still links and arms with a 0.15
one, since only the protocol's major number has to match; the binding write
on OUTPUTS needs both boards at 0.15, because the panel prepares the pages
and commits them only on a 4.10 coprocessor, and the AS5600 encoder on a 4.9
one. Flash both boards. The panel and the coprocessor keep an arm latch, a
restarted coprocessor is not armed again, a touch has one owner from its
press to its release, and an edit on OUTPUTS with a current sensor enabled
no longer loses the binding. The log viewer's plot pans with one finger on
request, and an AS5600 can serve as a rotary knob or as a servo's output
encoder. The touch contact ownership, the arm latch and the OUTPUTS binding
write have run on the host suite only; none of the AS5600 work has run on
hardware.

### Fixed

- **A restarted coprocessor is not armed again.** The coprocessor starts with
  an arm latch set and sets it on 200 ms of link silence, on a distrusted
  heartbeat and when it refuses an ARM; only CLEAR releases it, and the
  panel writes CLEAR only for an arm an operator held. The supply's ON and
  the STATUS page do not read the latch. The panel latches a stop, disarms
  its bank and zeroes the throttle when the link goes quiet while armed, and
  when a coprocessor starts answering under a bank armed without one. Any
  request that ends unanswered while the link is up takes the link down on
  the control task's next pass, whichever service sent it, and the panel
  sends nothing more until then; armed or arming, the stop is latched as the
  request ends. This changes no register and no frame.
- **The first arm after a STOP is taken on the first hold.** After its 100 ms
  settle the panel reads the STATUS fault register once a pass until the
  coprocessor reports the heartbeat trusted, for at most 200 ms more, and
  writes CLEAR only then. An arm the coprocessor never trusts is given up
  with nothing written.
- **CHAN_CFG and OUTPUTS while armed.** The coprocessor refuses with
  BAD_VALUE an OUTPUTS write that changes a slot and a CHAN_CFG write that
  changes a role or anything of a throttle channel. A surface's slew and
  pulse range are taken armed, as the SERVO screen writes them.

The decisions are `shared/safety/safety_gate.c` and
`shared/safety/arming.c`, host-tested in `test_safety_gate` and
`test_arming`. Not run on hardware.

- **A failed write on OUTPUTS no longer loses the binding.** Known issue in
  0.14.0: with a current sensor enabled under SETUP, INTERFACES, a tap on
  OUTPUTS or PICK A PIN can end with `NO LINK` and `FAULT 01` and leave the
  stored binding empty or mixed. Workaround on 0.14.0: switch the sensor off
  under SETUP, INTERFACES, edit the binding, wait 3 s, restart both boards,
  switch the sensor on again. Why a frame is lost only with a sensor
  enabled is not known. What changes:
  - A binding is written one frame per exchange, each acknowledged before
    the next is sent, in place of 8 frames back to back into the
    coprocessor's 2 receive buffers. Every write wider than one frame goes
    out this way.
  - Protocol 4.10 adds the pages BIND_CFG (0x2E), BIND_OUT (0x2F) and BIND
    (0x30). The panel prepares both output pages on a 4.10 coprocessor and
    one commit, carrying their CRC-16, puts both in force or neither. A
    frame lost anywhere in the sequence leaves the binding in force and the
    binding in flash as they were. A coprocessor older than 4.10 is written
    entry by entry and can still be left with a mixed page by a lost frame.
  - A binding that did not read is shown as not read, and one whose pages
    describe no binding as that, with the last binding read kept on the
    screen, dimmed. Neither is treated as nothing bound: no tap on OUTPUTS
    or PICK A PIN writes until a read succeeds. For pages that describe no
    binding, `HOLD: UNBIND ALL PINS`, held 2 s, writes a binding with no
    pin.
  - A write refused because the SERVO frame rate could not be put back says
    `REFUSED`, not `NO LINK`.
- **Picking a protocol on OUTPUTS writes nothing.** A pick changes which
  protocol's pins the screen shows as ticked and which the next tick joins.
  Only ticking or unticking a pin writes.
- **OFF stays picked.** With OFF picked every bound pin shows its protocol's
  name, and no pin can be ticked; the screen says `OFF SHOWS ALL, EDITS
  NOTHING`. OFF is kept until another entry is picked or the screen is left.
  From 0.8.1 to 0.14.0 the list went back to a protocol after one write.
- **`DSHOT300 BIDIR` and `DSHOT600 BIDIR` fit the protocol list and a pin's
  cell.** The list is 266 px wide for the 224 px of the name; a cell prints
  the protocol under the pin's box and name, 112 px in 121.

The sequences are `shared/link/link_port.c`, `shared/outputs/bind_link.c`
and `shared/outputs/out_stage.c`, host-tested in `test_bind_link` against a
model of the 2-frame receive buffer. Not run on hardware.

The log viewer's plot has a setting, SETUP → APPLICATION → One finger pans
(Ein Finger schiebt), off by default. While it is on and the view is zoomed
in, a finger that moves more than 10 px pans the view; a shorter touch still
selects a value. With it off, one finger moves the cursor and two fingers pan,
as before.

### Added

- **A rotary knob on the panel.** An AS5600 magnetic angle sensor on the
  panel's I2C terminal (GPIO8 and GPIO9, 0x36) turns the throttle on MOTOR &
  ESC and the horn on SERVO by how far it turns, with the touch slider's
  rules: relative, never arming, back to zero on a disarm, dropped on a frame
  that lost touch events or changed screen, never replacing a waiting arm,
  disarm, release or peak reset, and a sensor that stops answering adds nothing.
  SETUP, APPLICATION has Rotary knob (OFF by default) and Knob scale (90 to
  720 deg of knob per full slider span, 270 by default). A task of its own
  reads STATUS, RAW ANGLE and MAGNITUDE as three transactions every 10 ms,
  each with a 5 ms bus timeout.
  `shared/bench/knob.c` decodes RAW ANGLE, STATUS and MAGNITUDE and maps
  steps across the 4095 to 0 wrap. Not run on hardware. Known limitation: on the Waveshare ESP32-S3 Touch
  LCD 7 panel the board's CH422G I/O expander answers at I2C addresses 0x20
  to 0x27 and 0x30 to 0x3F, which includes the AS5600's fixed address 0x36,
  so an AS5600 knob cannot be read on that bus. Rotary knob is OFF by
  default.

### Added

- **Output encoder on the SENSE page (protocol 4.9).** An ams OSRAM AS5600
  magnetic angle sensor on a servo's output shaft, at 0x36 on the sensor bus
  beside the INA parts. SENSE's ENABLE takes bit 2 for it, and registers 26
  to 31 carry its flags (answering, magnet detected, too weak, too strong,
  an impossible STATUS, angle valid), RAW ANGLE (0 to 4095, 0.0879 degrees a
  count), MAGNITUDE, an angle sample count and the time the angle has stayed
  within 12 counts (1.05 degrees) of an anchor, in ms. The coprocessor reads
  STATUS and RAW ANGLE at 500 Hz on the 1 ms schedule's ticks without a
  rotation item, each register in a transaction of its own; one slot in 25
  reads RAW ANGLE, AGC and MAGNITUDE instead of STATUS, so the angle is
  sampled every 2 ms. The still time is kept at the 2 ms sample interval, so the panel times the end of a move whatever its
  polling interval. The panel writes bit 2 and reads registers 26 to 31 only
  from a coprocessor that names 4.9 and says
  `coprocessor older than 4.9 -- AS5600 not read` for an older one; it says
  `AS5600 not answering at 0x36` and the three magnet messages once each.
  An angle read with STATUS MD clear (no magnet, field below 8 mT) is not a
  position: the coprocessor holds no still time for it and the panel hands it
  to no consumer, so MEASURED shows `---`, ENC CENTRE stores nothing and a
  run judges no angle. With ML or MH set beside MD the angle is used and the
  condition is reported. STATUS bits 7, 6, 2, 1 and 0 are blank in the
  datasheet and are masked off; the probe takes any answer at 0x36 as the
  part except ML and MH both set. The driver is `shared/sense/as5600.c`.
- **AS5600 in SETUP, INTERFACES, and on SERVO.** An AS5600 row (OFF at the
  start) and AS5600 centre, the sensor's count at the servo's neutral. The
  SERVO screen's MEASURED row shows the angle from the centre, and ENC CENTRE
  on the DUT page takes the live count as the centre.
- **Angle results in the automatic servo test.** With AS5600 on, each step
  reports the end angles, the angle error against the commanded angle and a
  travel time measured from the angle: the command to the start of 100 ms
  within 1.05 degrees (`SERVO_TEST_ENC_TOL_COUNTS`, `SERVO_TEST_ENC_HOLD_MS`),
  beside the supply current's, which is unchanged. The CSV gains `angle (deg)`
  and `travel angle (ms)` columns, each row taking the newest angle taken at
  or before it, and the report an ENCODER section; the
  deadband is reported as not measured, since the moves go end to end. With
  AS5600 off, the CSV and the report are as before. Angles are compared and
  averaged on the circle: movement the shortest way round, end angles as
  offsets from the step's first, the error wrapped to -180 to +180 degrees.
  An end more than 180 degrees from the centre count is reported 360 degrees
  off; turns are not counted. The report says how often the sensor reported
  no magnet and how many readings had the field too weak or too strong.

None of the encoder work has run on hardware.

### Changed

- **Arming returns the MOTOR & ESC throttle to 0 %.** The slider, the readout
  and the `-1` and `+1` steps are at 0.0 % when the ARM hold completes,
  whatever was set on the disarmed bench: a slider at 60.5 % before the arm
  posts 1.0 % on the first `+1`, where it posted 61.5 %. A throttle command
  waiting at that moment is dropped, and a drag under way ends there and at a
  disarm. A throttle set after the hold completes is kept. A bench armed
  without the hold (a stick run on PROGRAMMER) returns the slider to 0 % when
  it reports armed.
- **One finger drags the SERVO dial.** A press by a second finger anywhere on
  the screen ends a drag on the dial, as it ends a drag on a slider.

### Fixed

- **A touch has one owner from its press to its release.** The router hands
  a screen a move or a release only for a contact whose press it handed that
  screen. A contact that slides from the screen into the status band (the top
  48 px) is released for the screen at its last point on the screen; one that
  comes down on the band reaches no screen, and neither does one that was
  down when the screen changed or touch events were lost. A drag that ended
  on the band stayed latched to its track id: on an armed MOTOR & ESC a later
  touch that landed in the 6 px of band under STOP and crossed into the
  screen moved the throttle by its own distance from the first drag's origin
  (14.5 % to 100.0 % in the host test); on SUPPLY the next hold on OUTPUT ON
  switched nothing on and its lift moved the voltage set point from 6.72 V to
  the 21.00 V cap. The same latch held SERVO's dial and SPEED. Assumes the
  GT911 gives a lone finger the same track id on successive touches; not
  measured on a panel.
- **The SERVO dial's drag ends with every press and when the screen is
  left.** A home tap by a second finger during a drag left the dial latched;
  back on SERVO a contact that pressed beside the dial and slid onto it
  commanded a position.
- **SUPPLY drops a held press when a press with the same track id arrives.**
- **A knob command withdrawn after a stop or a disarm leaves SERVO holding
  nothing.** The withdrawal restored the screen's record of a held output on
  a disarmed bench, and the next change of SPEED posted the position again.
  The coprocessor holds a disarmed bank at rest, so no pin moved.

## 0.14.0 - 2026-10-09

The link protocol is 4.8. A 0.13 board still links and arms with a 0.14
one, since only the protocol's major number has to match; the current
monitors and the phase tap need both boards at 0.14, because the panel
reads SENSE only from a 4.7 coprocessor and TONE only from a 4.8 one. The
coprocessor reads an INA228 in the ESC's power path and an INA3221 on the
servo rail over I2C at 400 kHz, and the panel sets them up under SETUP →
INTERFACES and shows them on MOTOR & ESC, in alerts and in the run log.
The coprocessor captures an ESC's beeps on one motor phase (the phase tap,
GP22 by default), and the panel sets it up under SETUP → INTERFACES and
shows it on PROGRAMMER → ESC STICK; stick programming still counts beeps
from the supply current. The servo test no longer fails small servos it
cannot measure, the coprocessor refuses the module's own pins, and the log
viewer zooms and pans with two fingers. None of the sensor or tap work has
run on hardware.

### Added

- **TONE link page and the phase tap's capture (protocol 4.8).** TONE
  (0x2D) sets up and reads the tap that hears an ESC's beeps on one motor
  phase through a series resistor and a zener clamp: enable, the GPIO
  (default 22, pad 29), the lowest and highest tone (50 to 2000 Hz and up to
  6900 Hz), the pitch change that splits a beep (0 to 50 %), the silence that
  ends one (1 to 100 ms, at least the lowest tone's period) and the periods
  that make one (1 to 64). The GPIO is refused past the bank, on a reserved
  pin, on an output's, SENSE's or SUPPLY's, and on an ADC pin (GP26 to GP29,
  GP40 to GP47) while the tap is enabled, and an OUTPUTS write on it, or one
  whose slot the silicon cannot bind beside the running tap (no PIO state
  machine, instruction memory or DMA channel left), is refused with
  BAD_VALUE, keeps the slots in force and saves nothing. The page holds the last 64 beeps: start in ms since the capture,
  length in 0.1 ms, pitch in 0.1 Hz, bursts, carrier in 100 Hz steps and
  whether a pitch change bounds it, read by number through EVT_SEL without
  consuming one. The set-up, registers 0 to 6, is kept in the coprocessor's
  flash (store record version 6; records of version 3 to 5 still read), and
  the tap starts at boot, and a saved enabled pin the board reserves starts
  refused (PIN_REFUSED), the set-up kept. GLITCHES (register 23) counts lows
  under 500 ns, which the capture's 8 µs hold-off removes first, so it reads
  0 on the tap. The read at 20 Hz is registers 8 to 23, one request
  and four data frames, about 1.7 % of the bus. No capability bit. A 4.7
  coprocessor answers the page with BAD_PAGE; a 4.7 panel never writes it.
- **The coprocessor captures the phase tap's edges.** A PIO state machine
  (`firmware/iomcu/src/tone_cap.pio`, 22 instructions) counts a 31-bit
  down counter every 4 clocks (26.7 ns at 150 MHz) and pushes a word for each
  edge: the counter and the level. A fall is pushed only after the line has
  stayed low for the hold-off, 8 µs (300 counts), and carries the time it
  fell; a low that ends sooner is never pushed. A DMA channel writes the words
  endlessly into a ring of 4096 words (16 kB, 42.7 ms at 96,000 edges/s), and
  core 1 reads it on its 1 ms tick and feeds the detector
  (`shared/sense/edge_ring.c`, `tone_svc.c`), extending the 31-bit count to 64
  bits against the microsecond timer, counting a lap of the ring, also one
  before the first read (the ring is cleared at the start), or a FIFO
  overflow as an overrun that ends the beep under way; the words read in the
  pass that sees the overflow and in the next are discarded, since they hold
  edges from both sides of the dropped word. Core 0 waits up to 5 ms for core 1 to finish its pass before it stops or
  moves the capture; a pass still running then refuses the change and
  leaves the capture as it was. A refused change that has to restart the old
  capture keeps the ring and the time base of EVT_START_MS and shows
  OVERRUN, since a beep under way was cut. A core 1 that does not start leaves a
  saved tap refused, its capture let go. The pin is an input
  with its pull-down on while the tap is disabled. The host suite assembles
  the PIO program from its source, runs it in a cycle-counting model and
  holds it to `tone_holdoff_edge()`: every decrement of the counter 4 cycles
  after the one before on every path, a low of 1200 cycles always kept and one
  of 1196 never. The firmware build holds pioasm's words to the same list.
  Not run on hardware. Core 1's stack is 4 kB.
- **The panel sets up and reads the phase tap (TONE page, protocol 4.8).**
  SETUP INTERFACES gains seven rows after the bus's pins: Phase tap (ON or
  OFF, default OFF), Tap pin (0 to 47, default 22), Tap lowest tone (50 to
  2000 Hz, default 400), Tap highest tone (100 to 6900 Hz, default 6500),
  Tap pitch split (0 to 50 %, default 8), Tap gap (1 to 100 ms, default 3)
  and Tap min periods (1 to 64, default 3), with German labels. The panel
  writes them to the TONE page (0x2D) in two frames, 500 ms after the last
  edit and only what differs from what the page holds, the frame that leaves
  the page a valid set-up first. While the page holds the tap on it reads
  registers 8 to 23 every 50 ms and takes the coprocessor's last 64 beeps one
  by one by number, from 1 to 65535 and round to 1; a lost reply loses no
  beep, and beeps the ring moved past are counted as missed. A coprocessor
  older than 4.8 is sent nothing, and with the tap enabled the band says
  "coprocessor has no tone page". Switching the tap off writes the first
  frame with the values the page holds, so a refused range cannot keep it
  on; a saved tap refused at boot for a busy pin is written again every
  5 s; across a change of ENABLE, of the pin or of the range the panel keeps
  its place in the numbering, so every beep after the change is read and one
  it had not read before counts as missed. While the supply's wiring waits
  for flash, the coprocessor refuses a tap start that would take the PIO
  room the supply attaches into. The band also says a
  refused set-up, a pin the coprocessor does not hold free, and a capture
  overrun, each once: an overrun stays said while the capture goes on, so an
  edit of the range does not say it again, and a restarted capture says a
  new one.
  The ESC STICK run's page shows the tap read only under the current line:
  its state, the last window's pitch, the beeps lost and the beeps not read,
  and the last four beeps with number, length in ms and
  pitch in Hz. The run still counts its beeps from the supply current. Needs
  the coprocessor's TONE page; not run against hardware.
- **Zoom in the log viewer's plot.** Two fingers spread to zoom in, pinch to
  zoom out and move together to pan; the view stays where they leave it, from
  8 samples to the whole run. A bar under the plot shows which part of the run
  is on screen. The cursor and the `<` and `>` buttons work as before, and a
  cursor stepped past the edge carries the view with it. The plot's left
  button reads BACK (ZURÜCK) instead of FIELDS (FELDER), which is what it
  does. RESCAN (NEU LESEN), and every read of the card, selects the newest
  run, so OPEN opens the run just recorded. Requested by a beta tester.
- **SENSE and SERVO_SENSE link pages (protocol 4.7).** SENSE (0x2B) sets
  up an I2C (Inter-Integrated Circuit) bus on two coprocessor pins for a TI
  INA228 in the ESC's power path and a TI INA3221 on the servo rail:
  enable, SDA, SCL and the clock, 400 kHz only; the INA228's address,
  shunt (50 to 20000 µΩ) and range (1.0 to 300.0 A, the bench's design maximum); the INA3221's address, shunt (5 mΩ
  to 1 Ω, default 0.1 Ω for 1.638 A full scale) and channels. The INA228's
  shunt and range are held to the driver's `ina228_calibrate()`: SHUNT_CAL
  4096, ADCRANGE from the range, refused past 163.84 mV or 2000 A full
  scale. Registers 7 and 11 are reserved and take only 0. SDA and SCL must
  be one I2C block's
  pair (GP16 and GP17 by default), and are refused on a reserved pin, an
  output's pin or a SUPPLY pin; any change is refused while the bank is
  armed. The set-up is kept in the coprocessor's flash. SERVO_SENSE (0x2C)
  carries each INA3221 channel's 50 ms window, with a clipped bit where a
  reading hit the top of the range, and a move capture on CH1 armed by the
  panel, which ends arrived, at a stop, late, unseen or lost.
  BENCH gains bit 5 (the INA228's voltage, current and power) and bit 6
  (the INA228's charge and energy totals). The coprocessor serves both
  pages and holds the pins. A 4.6 peer
  still links and arms.
- **The coprocessor reads the INA228 and the INA3221.** It has not run on
  hardware: both images compile, and the host suite runs core 1's step
  (`shared/sense/sense_svc.c`) and the pages against modelled parts. Core 1
  runs the sensor bus and nothing else, on a 1 ms tick: the I2C block at
  400 kHz on the SENSE page's pins with the pads' pulls off, 1 ms at most
  per transaction, a bus clear of 9 clocks and a STOP when SDA or SCL
  sticks, and an address scan for PRESENT, repeated every 1 s while a part
  is missing. Core 0 publishes FLAGS, the IDs, the readings, the 50 ms
  windows and the move capture. BENCH carries the INA228's voltage,
  current, power, peaks, charge and energy at the existing scales, with
  bits 5 and 6; a run that starts with the INA228 answering keeps it as
  the source to its end. The edge into driving clears bit 6 and the charge
  and energy registers in that pass, so no read after an arm carries the
  last run's totals. Capability bits 3 and 4 say what the SENSE set-up
  enables and change only when a SENSE write is taken; whether a part
  answers is SENSE FLAGS and BENCH bit 5. A capture's edge is the start of
  the PWM frame that first carries the new pulse, stamped from the slice's
  counter to 1 µs, and its level before the command is CH1's mean over the
  50 ms before the edge. A capture is refused on a PWM slot the silicon did
  not bind, and one given no edge within 3000 ms of its arm ends lost. An
  OUTPUTS write on a pin the sensor bus has not yet let go of is refused. Nothing trips: a
  part that stops answering is reported in FLAGS and the bench stays
  armed. The flash store's erases and programs run through
  `flash_safe_execute()`, which parks core 1 in RAM. A boot whose core 1
  does not register for that lock-out switches the store off: nothing is
  saved, set-ups run from RAM, and STATUS fault bit 6 (new) is set. A new
  SENSE set-up clears every register read under the old one. A held line
  or timeouts during the address scan mark the bus stuck and start its
  recovery. `cmake
  -DIOMCU_SENSE_BRINGUP=ON` builds a coprocessor image that enables both
  parts at the page's defaults while no set-up is stored, and while a part
  is enabled the console prints the sensors' state every 3 s; the bench
  session is in [First run](docs/FirstRun.md) §8. The coprocessor image
  grows by 20,636 bytes, to 321,464.
- **Output store record version 5.** The record keeps the sensor set-up
  beside the bindings and the supply's wiring. Version 3 and 4 records
  still load, with the parts they lack off.
- **The panel sets up and reads the current monitors.** Not run on
  hardware. SETUP INTERFACES gains INA228 address (0x40 to 0x4F, default
  0x45), INA228 shunt (50 to 20000 µΩ, default 200), INA228 max current
  (1.0 to 300.0 A, default 204.8), INA3221, INA3221 address (0x40 to
  0x43, default 0x40), INA3221 shunt (5.0 to 1000.0 mΩ, default 100.0),
  INA3221 channels (CH1 or CH1+2+3) and Sensor SDA and SCL (default GP16
  and GP17). INA228 shunt in 0.1 mΩ and I2C speed are gone: the first
  could not hold 250 µΩ, and the coprocessor takes 400 kHz only. The INA228
  address has a new key, so an address chosen before reads 0x45 again. The
  panel reads SENSE at every link-up, writes the set-up 500 ms after its
  last edit while the bank is disarmed, only where it differs, and reads
  the identity page again once a write is taken; a coprocessor older than
  4.7 is sent nothing. It reads SENSE every 50 ms while a part is enabled
  and SERVO_SENSE every 50 ms window while the INA3221 is. The band says, once
  each, a set-up refused or not sent, a part not answering, another
  identity at a part's address, a stuck bus, a clipped reading and STATUS
  fault bit 6. MOTOR & ESC's TABLE pane shows the ESC's own voltage and
  current beside the INA228's, with the difference; the totals are the
  INA228's while BENCH bit 6 is set; a run's CSV file gains
  `ina voltage (V)`, `ina current (A)` and `esc current (A)`, and each
  INA3221 window once: `window` and per channel the mean and highest current
  and the lowest bus voltage; the log
  viewer groups them under INA228 and ESC.

### Fixed

- **The log viewer's traces are unbroken on long runs.** A run longer than
  the plot's 752 columns drew each column from its own samples only, so a
  steep stretch, such as rpm on a ramp, came apart into dashes. Every
  column now reaches its neighbour. The stretch before a column's first
  number draws nothing instead of a line along the bottom of the scale.
- A reading at or past a move's window no longer counts as its arrival.
  The window is 3000 ms plus the meter's lag: 3300 ms on the PD mini, and
  3005 ms for the coprocessor's capture on INA3221 CH1. The servo test
  took a reading that came in after the window ended, but before the next
  pass, as the arrival and timed the move at 3300 ms or more. Such a move
  is now late.
- The coprocessor refuses GP23, GP24, GP25 and GP29 for an output slot and for
  the PD mini's pins. The RP2350-CAN module uses them (GP23 is the MODE pin of
  its power converter, GP24 senses VBUS, GP25 drives LED1, GP29 senses VSYS)
  and does not bring them out.
- The coprocessor refuses GP30 to GP63 on the module whatever board file the
  SDK is built for. The default image targets an RP2350B board file with 48
  GPIOs and did not refuse GP30 to GP47. One reserved set, from the pin
  catalogue, serves the OUTPUTS and SUPPLY pages.

- **The automatic servo test sees small servos move.** Movement is a
  reading more than max(0.020 A, 3 x the idle noise) from the level before
  the command, per step and in the brown-out walk, where it was 0.10 A. The
  idle noise is the standard deviation of the IDLE readings. On the PD mini
  an MG90S moves at 0.04 to 0.077 A over a 0.001 A hold: 0.13.0 saw no
  move, counted all 34 late at 3000 ms and read FAIL; a replay of that run
  times all 82 counted moves at 771 to 989 ms and reads PASS. The report's
  step table gives each step's threshold (`Thresh`). Arrival stays the
  first reading back within 0.05 A of the holding level: replays with the
  band at the threshold time the same moves.
- **A move the current cannot show is not a failure.** A counted move with
  no reading past the threshold in its window is unseen: not timed, not
  late, counted in an `Unseen` column. A step with no move seen reads NOT
  MEASURABLE (NICHT MESSBAR), and a run with unseen moves and no limit
  exceeded reads NOT MEASURABLE, with how many of its counted moves showed
  no movement. A 1102HB, holding 0.015 to 0.029 A and peaking at 0.039 to
  0.044 A, read FAIL with 34 late moves; its replay reads NOT MEASURABLE,
  25 of 46 unseen, none late. A brown-out walk with no movement at 5.00 V
  reads not measurable, and makes the run NOT MEASURABLE: a run of the walk
  alone that sees nothing no longer reads PASS. The result line then names
  that voltage, and a `Brown-out start` line under the limits gives it. With no move arrived the
  `Travel time` line reads `longest --` and `not measured`, not `0 ms`.
- **A move's window adds the meter's lag.** A move is late when it has not
  arrived 3000 ms plus the meter's lag after its command: 3300 ms on the PD
  mini, whose readings show an arrival about 300 ms late. A servo arriving
  at 2900 ms is timed, not late; one that never arrives is still late.
- **Travel times on the PD mini are an upper bound.** Its readings come
  every 102 to 106 ms, show a change of current about 0.3 s after the
  command (median 0.31 s) and often repeat. The report says so on `Lag`,
  `Repeats` and `Travel times` lines, and TRAVEL TIME is reported against
  the longest travel time and not checked: `upper bound, not checked against
  the limit`. The run is told what reads the current (`servo_test_meter_t`),
  so a faster current sensor can have it checked.

## 0.13.1 - 2026-10-07

PROGRAMMER no longer restarts the panel: the panel's main task, which runs
the interface, has 8192 bytes of stack instead of 3584, and PROGRAMMER's
drawing takes 3184 bytes of it at most. `tools/stack_check.py` holds every
panel task's deepest call chain to its stack in CI. The link protocol stays
4.6, so a 0.13.0 coprocessor works with this panel; the fix is in the panel
image alone.

### Fixed

- **PROGRAMMER no longer restarts the panel.** The UI runs on ESP-IDF's main
  task, whose stack is 8192 bytes (ESP-IDF's default is 3584). The deepest
  call chain on it is 3856 bytes; with PROGRAMMER's pages in one 1760-byte
  frame it reached 5120. Each PROGRAMMER page is drawn by its own function,
  so `render()`'s frame is 32 bytes and the largest page's 464. The stack
  costs 4608 bytes more internal RAM (random-access memory).

### Added

- **`tools/stack_check.py`.** Reads every panel task's deepest call chain out
  of the ELF (Executable and Linkable Format) file and fails when one
  exceeds its stack less 1024 bytes; CI runs it after both panel builds.
  Calls it cannot follow (through a pointer, into ROM (read-only memory),
  recursion) are counted and named, and make the depth a lower bound
  ([Performance](docs/Performance.md#stacks)).

## 0.13.0 - 2026-10-07

ESC STICK asks for an ESC's manual steps: a jumper, a button or a stick
move named in the profile, shown before power-up, at power-up, in the menu
or before the supply goes off, with a red MANUAL INTERVENTION REQUIRED
button that opens them. The ESC list goes by maker, then model. VOLTAGE and
CURRENT LIMIT are held to the selected model's stated ratings; a model with
no voltage rating runs with a warning. A page offers only the items on the
selected model. A run cut short while the ESC stores a change marks it
UNSURE. SERVO's sweep button pauses and resumes the curve where it stopped,
and SPEED's row says when it limits a sweep. The status band reads BENCH or
SIM. None of it has run on hardware, and no ESC has been programmed by it.
The link protocol is 4.6. A 0.12.0 coprocessor (4.5) still links and arms
with this panel, but has no RESUME, so a paused sweep starts its curve over:
flash both images.

The link protocol is 4.6: the coprocessor keeps a held sweep's phase and
resumes it (SWEEP register 5, RESUME). Flash both images. A 0.12.0
coprocessor (4.5) still links and sweeps with this panel, but a paused
sweep starts over from the curve's beginning, and the alert band says so.

### Added

- **RESUME on the SERVO link page (protocol 4.6).** A hold (SWEEP 4) keeps
  the phase of a sweep that was running; SWEEP 5 written alone carries it
  on from there. Refused with BAD_VALUE when no phase is kept or registers
  2 to 5 changed since the hold, NOT_ARMED on a disarmed bench. A write of
  0, a curve written over the hold, a disarm, 500 ms unwritten or a
  restart forget the phase.
- **SERVO says when SPEED limits a sweep.** SPEED's row on the right card
  reads SPEED LIMITS THE SWEEP (TEMPO BEGRENZT DEN SWEEP in German), in the
  warning colour, while SPEED is slower than the fastest change the TEST
  page's curve asks for: 2 pi f A for a sine, 4 f A for a triangle, and a
  jump for a square, which every SPEED below 100 % limits. The curves then
  move alike, as ramps at SPEED's rate. It follows the settings, before
  SWEEP is pressed and while a sweep runs. With the TEST page's defaults
  (sine, 0.5 Hz, RANGE 80 %) and TRAVEL +/-90 deg it clears at SPEED 63 %.
- **Manual steps in ESC profiles.** A profile lists in `manual` what a
  person does at the ESC besides the throttle and the power -- a jumper
  fitted before the power-up and pulled after the entry tones, a button
  pressed -- and when: `before_power`, `at_power_up`, `before_menu`,
  `during_menu`, `before_power_off`, `after_programming`. 24 profiles
  carry them: the 22
  Kontronik families, `turnigy-aquastar` and
  `greatplanes-electrifly-c-series`. The generator and the card reader take
  the same files, at most 4 steps of at most 120 bytes each.
- **MANUAL INTERVENTION REQUIRED.** A profile with manual steps shows a red
  button in the header of its item list, and a red MANUAL tag on its row in
  the list. The button opens the steps over the whole screen; they open by
  themselves the first time the profile is opened after a start. A row
  that does not run but has steps opens them too.
- **The run asks for each step.** The warning lists the steps due before
  the power-up. A run stops before a power-up for an `at_power_up` step and
  for `before_power` steps from the second power-up on, and after the entry
  for a `before_menu` step, powered with the stick at the value's power-up
  position; DONE goes on, ABORT and STOP end it, and no DONE within 60 s
  ends it with NOT CONFIRMED, red light lit. Steps before the power-up that
  do not fit the warning whole are counted there, and HOLD TO RUN counts
  only once ALL STEPS has shown them. The result lists the
  `after_programming` steps in two lines, or counts them where they need
  more; when a run ends they open by themselves over the result, and
  MANUAL INTERVENTION REQUIRED in the result's header opens them again.
- **A value's own power-up position.** `values[].entry_throttle` names
  the stick position the manual programs a value from where it is not the
  entry's. A run powers the ESC up there for that value, with the stick
  moved only while the supply reads off: the Kontronik car modes start
  from the middle, the neutral they teach. A one-stage menu uses each
  value's position at its own power-up; a two-stage menu whose changes need
  different positions is refused. The warning and the run show POWER-UP AT
  when the position is not MIN.
- **A value's own entry time.** `values[].entry_hold_ms` gives the wait
  from power-on to the menu where the manual names one for that value:
  Kontronik SUN PLUS modes 4 to 6 wait 5 s, the others 2 s, and the button
  is asked for at that time. The entry also lasts at least the longest
  `at_power_up` hold.
- **Moves after a selection.** `values[].after_select` lists the stick
  moves a value asks for after its select move: PIX mode 2 and the
  Kontronik car modes go to the brake after full throttle. A run makes each
  STORE after the one before, then switches the supply off; the simulated
  ESC stores nothing without them. A two-stage profile takes none.
- **Makers, then models.** ESC STICK lists the makers, alphabetical, each
  with how many of its models run and a MANUAL tag where one has manual
  steps. A maker opens its models, one row a model by current, voltage and
  name, with its family and its run state; a model opens its family's
  profile, named with the model, and the supply takes the model's own cell
  count. The search filters both levels and is kept between them; card
  profiles join their maker, at most 512 models a maker. The count is on
  the line under the rows. The steps, the page, RUN and the warning judge
  the model tapped, at its own voltage.
- **The menu-starting step is listened through.**
  `manual[].starts_menu` marks the step whose action starts the series:
  pulling the jumper or pressing the button starts the Kontronik mode
  series at once, while the operator's hand is at the ESC. The run counts
  beeps from the moment it asks for that step; the menu heard in order, or
  DONE, takes it as done, and SILENCE and TIMEOUT run from then. Every
  other step waits for DONE. One a profile, only on `before_menu`, none
  after it, and only where the menu rests at the power-up position, in the
  generator and the card reader alike. 16 Kontronik profiles mark their
  pull or press.
- **No step at an ESC that may be powered.** RUN is refused while the
  supply does not read off: a reading no older than 1000 ms with the
  supply's own state off and the current at or under 20 mA for 200 ms.
  The warning shows its before-power steps and counts HOLD TO RUN only
  while the supply reads off. The run asks the supply off at its start,
  keeps the stick at MIN, and moves it, asks for a step at an unpowered
  ESC or powers anything only once its own readings say off (else SUPPLY
  STAYS ON); a reading with the output on or the current up after that
  ends the run at once, and readings that stop end it with NO READINGS.
  The result says not to touch the ESC while the supply does not read
  off.
- **The supply stays on until the ESC has confirmed.** A
  `before_power_off` step holds the run after the store's last move, the
  ESC powered and the stick where the store left it, until DONE; the
  prompt says not to touch the ESC. Every Kontronik profile that runs
  carries one: the ESC repeats the stored mode as tones before the manual
  disconnects it. No DONE within 60 s ends the run with NOT CONFIRMED; that
  end, and any other from the selection of the value on, says on the
  result that the mode may not be stored and marks the change UNSURE; so
  does an end before the last move that stores a value. `manual[].locks` marks the step where the ESC
  locks itself instead (8 to 10 LED flashes): the KOBY, JIVE Pro, KOLIBRI,
  KONTROL-X / KOLIBRI-X and KOSMIK profiles, where the result says the ESC
  may be locked.
- **Manual steps in German.** A step carries its German in `action_de`
  (1-120 bytes of UTF-8), shown when the interface is German, with the
  English as the fallback. All 24 profiles' steps have it.
- **The ESC's ratings hold the stick run's set points.** VOLTAGE, or the
  cell count's voltage, is refused over the model's `v_max_mv` and under
  its `v_min_mv`, and CURRENT LIMIT over its `current_a`; the row, the
  page and RUN say which, with both figures. A model that states no
  `v_max_mv` runs with a warning on its row and beside RUN: at VOLTAGE set
  by hand, or at the family's lowest cell count where it states none of
  its own. Profiles carry `models[].v_min_mv` from the JSON, in the
  generator and the card reader alike.
- **A page offers only the model's items.** An item with `applies_to` is
  listed only on the models it names, and RUN refuses a change to an item
  not on the model: Jeti's Cutoff mode and Switching frequency share item
  3 on different models.

### Changed

- **SERVO's sweep button pauses and resumes.** While a sweep runs it reads
  PAUSE (in English and German) where it read HOLD. A tap holds the output
  where it has got to, as HOLD did, and the button reads PAUSED (German:
  PAUSIERT), filled in the warning colour instead of the accent. A tap on
  PAUSED carries the sweep on from the point of the curve it was paused at,
  its dwell and its count of ends included. A changed SPEED keeps the
  pause, and the resume runs at it. CENTRE, RELEASE, a finger on the dial,
  STOP, a disarm, leaving the screen, a hold unrepeated for 500 ms, a HOLD
  answered only at a retry or after longer than 500 ms, the link going,
  and a changed type, frame rate, pulse, trim, travel or reverse end a
  pause, and the button reads SWEEP. A HOLD answered late or at a retry
  releases the surfaces to their centre, since where the coprocessor holds
  is then not known. The link going ends a sweep or a pause, and the
  output is drawn at rest, as the coprocessor rests the surfaces. A HOLD
  acknowledged after PAUSED was tapped still moves the horn to the output
  the coprocessor held, and the resume goes on from there. Touch events
  going missing pause a running sweep. SWEEP is offered only with a surface
  bound and the link up, greyed otherwise.
- **SERVO draws a sweep as the coprocessor runs it.** A started, resumed
  or restarted sweep is drawn only from the coprocessor's acknowledgement
  of it; until then the horn stays where the output is, and during a
  restart the old curve is drawn on. A PAUSE draws the curve on until the
  HOLD is acknowledged, for at most 500 ms. Two taps on the sweep button in
  one frame, before the first one's command has left, cancel out: nothing
  is sent.
- **At SPEED 100 % the horn is drawn at the command at once** when nothing
  measures the servo; it was drawn moving at 360 deg/s. The coprocessor
  takes the command at once at 100 %, and the drawing now does the same.
- **SERVO's CENTRE button reads ZENTRIEREN in German.** CENTRE stays
  English as a pulse name (PULS CENTRE).
- **The status band's mode reads BENCH or SIM.** It read LINK with the link
  up, the word the link chip beside it already showed. BENCH is the panel
  driving the coprocessor's outputs, SIM the panel running on its own
  models while no coprocessor answers. Both stay English in German.

### Fixed

- **Kontronik profiles run.** Every Kontronik ESC needs a jumper or a
  button to enter its programming mode, and stick programming refused all
  22 as `needs a person at the ESC`. 10 now run with their manual steps
  asked at their moment: `kontronik-3sl`, `-beat`, `-beat-car`,
  `-beat-fai`, `-jazz`, `-kontrol-x`, `-pix`, `-smile`, `-star-line` and
  `-sun-plus`; 24 of the 72 profiles run. The others name `manual step`
  where a jumper moves during the menu, or their menu's reason.
- **Kontronik profile data.** MINIJAZZ enters with a button, not a jumper;
  the 3SL and Star-Line jumper sits on the two gold contacts; the JIVE Pro
  jumper cable is pulled within 10 s; the OPTO and BEC procedure is on
  pages 3-4 of their manuals.

## 0.12.0 - 2026-10-07

SERVO runs an automatic servo test: supply steps, currents, travel time, a
brown-out walk and a CSV and TXT report. PROGRAMMER programs an ESC through
its throttle-stick menu, with a search over 72 profiles and a stack light
for beeps and faults; 14 profiles run, and every beep timing is a default,
as no ESC has been recorded. The interface and the servo report are in
English or German. The PD mini cuts a sagging input, holds a wiring change
for a fresh state read, and a STOP closes the OUTPUT IS ON question. None of
it has run on hardware. The link protocol is 4.5. A 0.11.0 coprocessor
(4.4) still links and arms with this panel, but without the sag cut-off and
the held wiring change, which run on the coprocessor: flash both images.

### Added

- **The automatic servo test.** START TEST on the SERVO screen's TEST page,
  a 2 s hold on an armed bench, steps the supply through 4.8 and 6.0 V, and
  with HV SERVO on (TEST page, off at every restart) through 7.4 and 8.4 V,
  which start only through HV SERVOS ONLY held for 2 s. Each step waits for
  the set point to read back and for SETTLE, then measures the idle current,
  the moving current, the holding current at each end and the travel time
  end to end, timed from the supply's current: from the command to the first
  reading back within 0.05 A of the destination end's holding level, after a
  reading 0.1 A above it, so ends held at different currents do not end a
  move early. A brown-out walk follows, from 5.0 V (or the voltage cap in
  force) down in 0.2 V steps to 3.0 V or the supply's lowest set point, until
  no reading of a move lies 0.1 A from the level before it. A step above the
  caps is refused before any warning opens. The run writes `BENCHnnn.CSV`,
  one row per supply reading, and with REPORT on `BENCHnnn.TXT`: the device,
  the settings in
  force, the results per step, the brown-out voltage, the reading rate and
  its time resolution, and PASS or FAIL against the LIMITS page. STOP, a
  disarm, link loss, leaving the screen, taking the servo, a supply that
  trips, goes off, stops answering or sends no reading for 1.5 s, and 1 s
  above STALL AT end a run with the output off and the report marked
  ABORTED. Afterwards, whichever screen is up, SUPPLY's set points go back
  to their values before the run, once its OFF has gone, a reading after it
  shows the output off and OUTPUT ON is held on neither SERVO nor SUPPLY;
  not if they were changed
  since. A number carried by a `BENCHnnn.TXT` alone is not reused, and the
  log viewer's DELETE removes a run's report with it. Progress and the
  result show on the left card. The engine is
  `shared/servo/servo_test.c`, host-tested against the servo and supply
  models; nothing of it has run on hardware. The supply's state carries the
  module's reading count (the SUPPLY page's SAMPLES) and the time of the
  page read that first showed it, so a page read with no new reading behind
  it is not taken for one; readings skipped between two page reads are
  counted in the report.

- **Stick programming.** PROGRAMMER has a third class, ESC STICK, that
  programs an ESC through its throttle-stick menu: pick a profile and the
  values to change, hold the red warning NO PROPELLER, MOTOR SECURED? for
  2 s, and the bench arms, holds the throttle at the entry position, switches
  the supply on and counts the menu's beeps in the supply current, moving
  the throttle on the group that names the wanted item or value. 14 of the
  72 profiles are of a kind it runs: 13 two-stage, 1 one-stage. Beeps are
  counted with a threshold and hysteresis over the idle floor, from a quiet
  line, with pulse and gap lengths judged in readings. A group is acted on
  only when it and the group before it are both in the menu's order, so one
  missed beep passes a group rather than selecting the number below. Reset
  and exit items are actions and are not offered. A run that ends as
  planned switches the supply off and moves the stick only once the supply
  itself reports the output off, with the current down. STOP, a
  disarm, a lost link, the supply going off or silent, ABORT and leaving
  the screen end a run with the throttle at minimum, the supply off and the
  bench disarmed; a STOP or a lost touch event also takes back an ARM the
  run queued and the panel had not yet sent. The beep timings are 14
  settings on a TIMING page, every one a default: no ESC has been recorded.
  With the PD mini off, the panel's modelled supply draws a simulated ESC's
  current. Never run against an ESC.
  [Stick programming](docs/StickProgramming.md).
- **The stick warning asks for no propeller.** The ESC enters its
  programming menu during a run and does not drive, but in rare cases a
  motor may start and run. The warning says so: a connected motor must be
  mounted solid and carry no propeller, and a resistor load in place of the
  motor works as well. The beeps are counted from the current either way.
- **Search in the ESC STICK list.** A SEARCH field filters the profiles as
  each key is typed, on the text keyboard docked to the right of the list.
  A profile is found when the text appears in its maker and name read as
  one text, or in its maker and a model name; case does not matter, and `*`
  stands for any run of characters (`kontr*jazz*55` finds the Kontronik
  Jazz by its model JAZZ 55 LV). The header counts what was found, OK keeps
  the search, CANCEL restores it and X clears it. The text keyboard has a
  search mode in which its `_` key types `*` and OK takes an empty text;
  the SERVO name keyboard is unchanged. The matcher is
  `esc_profile_matches()`, host-tested.
- **A stack light on the stick run.** A red-over-green signal tower beside
  the beep count. Green is on while the beep detector holds a pulse, at
  least 150 ms (`ESC_STICK_BEEP_LIGHT_MS`) so a beep shorter than a frame
  shows. Red is lit on a result that ended because something was not as
  expected (every end except DONE, a STOP pressed, ABORT and leaving the
  screen, decided in `esc_stick_reason_is_fault()`), until OK. A stop the
  bench raises itself -- touch silent or lost, the coprocessor's refusal or
  failsafe -- ends a run with BENCH STOPPED and lights red; the arming
  policy counts pressed stops (`arming_stop_pressed()`) apart from the
  rest, and `tools/check_docs.py` holds the docs' red light table to the
  code.
- **Where the stick rests, and the move that stores.** An ESC profile may
  name `scheme.listen`, the stick position while the menu sounds, and
  `scheme.store`, the move that stores a selection; the generator and the
  card reader take the same rules. The nine YGE profiles carry
  `scheme.listen` and the eight YGE mode setups `scheme.store`.
- **A German interface.** SETUP, APPLICATION, Language switches every
  screen between English and German on the next frame, with no restart.
  Labels, setting names and help, alerts, warnings, refusal notes and the
  servo test's TXT report are translated; ARM, DISARM, STOP, SWEEP, HOLD,
  the screen titles, protocol and mode names, units and the field's own
  terms stay English, as does the servo test's CSV. The strings are IDs in
  `shared/ui/include/ui_text.def` with English as the fallback, and German
  is `shared/ui/ui_text_de.c`. Text is UTF-8: `gfx` decodes it, one cell a
  code point, and the two text fonts carry Ä Ö Ü ß ä ö ü, 102 glyphs each.
  `tools/render_ui.py` renders every screenshot in German into
  `docs/img/de/` as well, the German wiki pages show those, and
  `render_ui.py --fit`, run in CI, fails on a German string that is cut,
  runs past its field, overlaps another or is painted over by a fill drawn
  after it. `tools/check_formats.py`, run in
  CI, compiles `shared/` with each English string in place of its lookup
  under `-Wformat=2 -Wformat-nonliteral -Wformat-signedness` and fails on
  any warning; `test_text` holds every German format to its English's
  conversions and every string no screenshot shows to its field, and
  `tools/check_docs.py` fails on a German page that quotes in backticks the
  English of an interface string, a format's output, a setting's label,
  help, option or category, or a servo test word the screen shows in
  German. Numbers
  keep the decimal point in every language. The how and the glossary are on
  the new Language page.

### Changed

- **Alerts hold 127 bytes.** The alert band took 47 bytes of an alert and
  cut the rest, so `coprocessor has no SUPPLY page -- PD mini not driven`
  lost its last five characters; `UI_ALERT_MAX` is 128 and the control
  task's copy is the same size.
- **Four bus-fault headings fit the screen.** "this panel is
  rejoining the bus", "this panel's controller has stopped", "probes cross,
  and not all of them" and "probes go missing without a bus error" ran under
  the measured numbers in the heading face; the screen now heads them "this
  panel is rejoining", "the controller has stopped", "not every probe
  crosses" and "probes lost, no bus error". The console keeps the longer
  words.

- **The Silver Series profile is not a counted menu.**
  `greatplanes-electrifly-silver-series` is `scheme.type` `other`: its stick
  procedure toggles the brake and sounds no menu of values, so stick
  programming does not run it.

### Fixed

- **SERVO's commanded pulse reads PULSBREITE in German.** It read SOLL, the
  same word as the supply's SET line beneath it.
- **A sagging input switches a live PD mini off.** While the output is on,
  the coprocessor switches it off when the module's input reads under the
  set point plus 0.5 V (`PDMINI_HEADROOM_MV`) on 2 input reads in a row
  (`PDMINI_SAG_READS`, one read every 500 ms), holds it off until an OFF,
  and sets SUPPLY flag bit 10; the panel switches its ON off and names the
  input and the set point in the band. One low reading switches nothing,
  and a failed read neither counts nor clears. A live output's set point is
  no longer lowered to follow a falling input. The rule is chosen without a
  bench measurement and may cut a run that would have survived.
- **A PD mini wiring change waits for a fresh state read.** A change to
  ENABLE, TX, RX or BAUD in the up to 500 ms after a state read that showed
  the output off was taken at once, and closed the UART of a module that
  might have switched itself on since, leaving it no OFF path. Once a module
  has answered, the coprocessor now holds the change (SUPPLY flag bit 8)
  until a state read sent after it, at most about 1.2 s, and takes it only
  if that read shows the output off; on, or a failed read, refuses it (bit
  9), and the band says `PD mini wiring refused -- its output may be on`.
  An ON is refused while a change is held. Protocol 4.5.
- **A wiring edit on SETUP is followed before each SUPPLY write.** The
  control task followed the PD mini's wiring once per service pass, and an
  edit published by the render core during an earlier write's exchange let
  an ON taken before it reach the page with the old wiring. The edit count
  and the wiring word are followed straight before every write, with no
  exchange between; an edit that has counted and not yet stored its word
  counts as a change.
- **STOP closes the OUTPUT IS ON question.** A STOP and a complete APPLY
  tap in the same render frame (about 50 ms) applied the typed set point on
  SERVO and on SUPPLY: the stop was seen before the tap, and the question
  still stood until the supply reported its output off a sample later. Each
  screen counts its stops, and a question stands only under the count it
  was asked with, so the tap applies nothing and the question closes in the
  same frame.
- **The servo test's set points wait for the supply's own OFF.** After a
  run, SUPPLY's set points went back once a reading after the run's OFF
  showed the panel's own request off; the PD mini follows that request a
  link exchange and a module transaction later, so a set point could reach
  an output still on. They now wait for a reading in which the supply
  reports its output off.

- **OUTPUT ON on SERVO above 6.0 V takes the HV hold.** The HV SERVOS ONLY
  warning opened only when the SET line raised the voltage across 6.0 V. A
  set point above 6.0 V made on SUPPLY, or already in force, reached the
  servo after the ordinary 2 s OUTPUT ON hold with no warning. OUTPUT ON on
  SERVO with a set point above 6.0 V now opens HV SERVOS ONLY, and the
  output comes on only after its HOLD TO APPLY is held for 2 s. A set point
  that rises above 6.0 V during the ordinary hold opens the warning as the
  hold completes, and nothing comes on. OUTPUT ON on SUPPLY is unchanged.
- **OUTPUT ON's flash on SERVO ends with the output.** An output switched
  off during the flash that marks it coming on left one of the two screen
  buffers drawing the button in the flash's red until the next change.

## 0.11.0 - 2026-10-06

SERVO sets and switches the supply itself; raising its voltage past
6.0 V there opens a warning applied after a 2 s hold. 72 ESC programming profiles
are built in and replaceable from the SD card; nothing uses them yet. An
alert clears after 30 s or on a tap. The link protocol stays 4.4, so a
0.10.1 coprocessor works with this panel; flash both images all the same.

### Added

- **SERVO sets and switches the supply.** A SET line under the supply plot
  holds SUPPLY's voltage and current limit set points, each opening the
  keypad on a tap, and OUTPUT ON (two-second hold) and OFF (tap). They are
  SUPPLY's own set points and switch, with its caps and its question before
  a live output changes. The supply plot is 48 px high, from 72 px.
  A voltage raised past 6.0 V there opens the warning HV SERVOS ONLY and is
  applied after a 2 s hold: above 6.0 V only a servo specified as HV is
  within its rating, and a standard servo can be destroyed immediately.
- **ESC programming profiles.** 72 profiles describe how 451 ESC
  (electronic speed controller) models are programmed through their
  throttle-stick menus: the entry gesture, how a number is sounded, and the
  menu's items, values and defaults. They are JSON files in
  `shared/esc/profiles/`, compiled into the panel by
  `tools/gen_esc_profiles.py`; CI runs its `--check`. A file in `/ESC/` on the
  SD card, named after its id, replaces the built-in profile with that id or
  adds a new one, read once at start-up; up to 32 card profiles, 64 KiB
  each. The panel's card now reads long file names (FATFS LFN, up to 63
  characters listed); before, only 8.3 names. Every profile
  is unverified and no beep timing is known. Nothing uses them yet: the mode
  programming that will is not built.

### Fixed

- **The PD mini's rate is no longer an alert.** With "PD mini baud" at AUTO,
  the rate found filled the red alert band at the foot of every screen, over
  the SUPPLY current limit, until another alert replaced it (bench,
  2026-10-06). The SUPPLY header shows it after ONLINE instead, e.g.
  `ONLINE 38400`, for AUTO and a fixed rate alike.
- **An alert clears.** The red alert band at the foot of the screen stayed
  until another alert replaced it, covering ARM and the controls beneath. It
  now clears after 30 s, or on a tap on the band, marked `x`; that tap
  reaches no screen. "touch did not answer -- the bench will not arm" at
  start stays until restart, as nothing can tap it away.
- **A protocol mismatch is reported once.** A coprocessor on another
  protocol major raised "protocol mismatch -- will not arm" at every
  one-second identity probe, so the alert never expired and a tap cleared it
  for a second. It is now raised and logged once per mismatch; a probe that
  matches or goes unanswered makes the next one news.

## 0.10.1 - 2026-10-06

The PD mini on the bench. The coprocessor finds the module's UART rate
itself, the voltage asked never exceeds the module's input less 0.5 V, and
SUPPLY's SETTINGS restarts a module in ERR. The link protocol is 4.4; with a
4.3 coprocessor the panel sends 19200 for AUTO, and RESET PD MINI shows
"coprocessor too old to reset the PD mini". The voltage cap and the reset
have not been run against a module.

### Added

- **RESET PD MINI.** SUPPLY's SETTINGS offers it while the PD mini is the
  supply: the output goes off and the module is sent SYSTEM_RESET, for a
  module in ERR, without unplugging it. SUPPLY register 17 (protocol 4.4)
  carries it; the driver sends it once the output reads off and asks the
  module who it is again about 1 s later.

- **PD mini baud AUTO.** With "PD mini baud" at AUTO, the default, the
  coprocessor finds the module's UART rate itself: after every WHO_AM_I
  without a valid answer it tries the next of the 7 rates, starting at
  19200, until a module has answered once, and holds that rate. The band
  says the rate found. Link protocol 4.4: SUPPLY's BAUD takes 7 for AUTO
  and register 16 (BAUD_FOUND) reports the rate in use. A 4.3 coprocessor
  is sent 19200 for AUTO, and its 16-register page is read as it is.

### Fixed

- **The PD mini is never asked for more than its input.** A set point over
  the module's input voltage put it into ERR until it was power cycled
  (bench, 2026-10-06: 5.88 V asked from a 4.88 V input). The driver now
  writes no more than the reported input less 0.5 V, and the SUPPLY
  screen's voltage cap follows the input the same way. The 0.5 V margin is
  not measured.

## 0.10.0 - 2026-10-06

The servo tester's first three steps (#223) and the PD mini. SERVO gets a
SETTINGS overlay with the servo type, frame rate, pulse widths, trim,
travel and reverse, a text keyboard and the supply's live power plot; the
coprocessor runs the frame rate and a square, sine or triangle sweep, and
holds a position on HOLD. The PD mini (WeAct PD Power Mini V1 Buck) is
driven by the coprocessor on a PIO UART on two pins SETUP INTERFACES
names, and the SUPPLY screen drives it through the coprocessor when it is
enabled. The link protocol is 4.3: a panel and a coprocessor of this
release use the SERVO and SUPPLY pages; with an older coprocessor the
frame rate, the sweep and the PD mini are not offered. The PD mini has not
been run against a module.

### Added

- **SUPPLY drives the PD mini** when SETUP INTERFACES enables it, through
  the coprocessor's SUPPLY page; the panel's model otherwise.
  - The panel writes the page and reads it every 100 ms: an OFF first,
    then the wiring once a read shows the output off, then the command.
  - The header says PD MINI and the menu tile drops MODELLED. The set
    points are capped to the module's 1 to 20 V and 0.05 to 3 A, in 10 mV
    and 10 mA steps.
  - Output off, with a line in the band: readings older than 1500 ms, a
    coprocessor without the page, an ON refused or let go at the far end.
    The band also says when the pins are refused, the output would not
    switch, or the set points would not take.
  - A change to the PD mini's pins or baud on SETUP switches the output
    off, as enabling or disabling it does. An ON queued before such a change is
    dropped rather than applied after it.
- **The coprocessor drives the PD mini** on a PIO UART, on the two pins the
  SUPPLY link page (0x2A, protocol 4.3) names.
  - Wiring: refused on a pin that is reserved, bound to an output or the
    other pin, and while the output is asked on or may be on -- read on,
    an ON not yet confirmed, or an OFF owed to a module that stopped
    answering. The pins are reserved from the outputs while held.
  - Output: an ON needs a live heartbeat and its set points in the same
    frame, and the output goes off when the heartbeat stops -- applied to
    the driver before it steps, so an ON queued in that pass is not sent.
  - New wiring is attached only once it is in flash; until then the pins
    are reserved and the UART only tried. A module that has answered and
    is not read off now counts as maybe on, so its wiring is held.
  - Wiring attached -- restored at boot or newly given -- holds the module
    as maybe on until a state read shows it off, or until 10 WHO_AM_I in
    a row, about 10 s, get not a byte back; any answer starts that count
    again. Disabling the supply schedules no attach.
  - SUPPLY traffic goes at control priority on CAN, as CONTROL, LIMITS
    and FAILSAFE do, so telemetry cannot hold back the supply's OFF. An
    OFF written alone names no set points.
  - Until a command is written the driver is asked for the output off and
    no set points, so attaching or restoring the wiring leaves the
    module's own set points alone. TX and RX swapped count as rewiring.
  - An ON waits for wiring just written to reach flash (NOT_ARMED until
    then), and the wiring is restored at boot whether or not it is
    enabled. An OUTPUTS write whose slot the UART leaves no PIO for is
    refused rather than kept unbound.
  - Set points under the module's least, 1000 mV and 50 mA, are taken and
    read back at it.
  - Flash saves wait while the supply is asked on or may be on, as they do
    while the outputs drive; a wiring write checks the driver itself, not
    the flags of the pass before.
  - No output slot is bound on a pin the supply holds: the OUTPUTS write is
    refused rather than stored.
  - The wiring is kept in the coprocessor's flash with the output bindings
    and driven at boot, after the outputs' hardware, with the output off,
    so a module left on is
    switched off after a restart. The store's record is version 4; output
    bindings saved by the build before still load.
  - The PD mini driver counts failed state reads on their own: three in a
    row take the module for gone however the other readings are answered,
    and an OFF it is owed goes blind if the last of them was silent.
  - Reading back: the page carries what the module last said, with flags
    for an output that would not switch, set points that would not take,
    and an output that is or may be on.
- **A driver for the WeAct PD Power Mini V1 Buck** (`pdmini`).
  - Protocol: the CRC8 (polynomial 0x31, initial 0xFF) checked against all
    fifteen values the vendor's sheet prints. Reply framing with WHO_AM_I
    ending in 0x0A or a CRC. A reply has 400 ms to start, 60 ms more per
    byte, and 600 ms from the request in all. Set points clamped to the module's 1 to 20 V
    and 0.05 to 3 A.
  - Behaviour:
    - one transaction at a time, with the pins pulled down between them;
    - nothing said before WHO_AM_I is answered with a text holding
      "PD Power Mini", so another device on the pins is never written;
    - every write confirmed by the matching read, since the module answers
      a write with nothing;
    - the output confirmed 250 ms after OUTPUT_EN, with no state read in
      between;
    - a module that misses three transactions in a row is taken as gone;
    - a slot number past 4, or set points for another slot than asked,
      fail the transaction.
  - OUTPUT_EN's argument for on is learnt from the module: 1 first, then 0,
    and kept across a module going quiet and answering again. It is
    learnt only from an output seen to come on after a write towards on,
    and relied on once two ONs with it have taken,
    never from one seen to go off, which the module's overcurrent
    protection does by itself. Once a
    read-back has shown it, only that argument is written. The output is
    written only when it reads otherwise than asked, so an OFF cannot
    switch on an output that was off.
  - Ordering: an OFF goes first.
    - An ON not yet sent when an OFF is asked for, or when the set points
      read back for it change, is dropped, and so are set points no longer
      the ones asked, or any while the output is or may be on. A read under way other
      than the state's is left.
    - An OFF that does not take is written again without pause; four
      that do not take raise the stuck flag.
    - An OFF asked for while an ON is being confirmed is written at once
      when the argument is known. Otherwise the state is read back to back
      for 1000 ms from the ON, and the output switched off the moment it
      reads on.
    - An ON waits until the active slot reads back the set points asked
      for. The set points and then the slot are read again straight before
      it, so the output never comes on at what a slot held before.
  - A module that stops answering with its output on is owed an OFF until
    a state read shows it off. The OFF is sent blind after each WHO_AM_I
    answered by not one byte, while one is asked for, with the argument a
    read-back showed to mean on. A module that answers is read, never
    written blind.
  - The slot and its set points are read again every second, so a change
    made on the module's buttons is put back.
  - The readings take turns by how late each is, so a slow module does
    not starve any of them, past 2^31 ms of uptime too.
  - Four OUTPUT_EN writes that do not take put the learnt argument in
    doubt, and both are tried again. READ_INPUT_STATE unanswered 3 times
    is not asked again until the module is identified again.
  - Set points that do not take, or replies naming no slot or another, are
    tried three times, then flagged and left alone for two seconds while
    the readings go on.
    With the output on, that switches it off, and it stays off until an
    OFF and a new ON are asked for.

- **SERVO has SETTINGS of its own.** The overlay sets the servo type --
  STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC (1520 us, +/-700 us, up to
  333 Hz) and HELI TAIL 760 (+/-350 us, up to 560 Hz) -- the frame rate,
  the pulse widths, trim, travel and reverse, and the automatic test's
  curve, speed, range, length, dwell, settle, supply steps, brown-out, its
  pass/fail limits, stall threshold and the device under test's name.
  STANDARD PWM keeps at least 1 ms between pulses, so its fastest rate is
  1 / (longest pulse + 1 ms). A heli type or a frame rate above 60 Hz opens
  a warning that it can destroy a servo not made for it and applies only
  after a 2 s hold; the profile in force shows red, and every restart is
  STANDARD PWM at 50 Hz. The test, limit and name settings are kept in NVS
  (non-volatile storage). No automatic test runs: the TEST, LIMITS and DUT
  settings are kept for it.
- **SERVO plots the supply's live power.** Voltage, current and power of the
  supply that feeds the servo, read and plotted on the right card.
- **The SERVO screen's frame rate reaches the pins.** A SERVO link page
  (0x29, protocol 4.1) holds one frame rate, 40 to 560 Hz, for every PWM
  output whose first channel is a surface; 0 is each slot's own rate. It is
  refused with BAD_VALUE when a surface would leave the rate of the output
  beside it on its PWM slice, and it is not kept across a coprocessor
  restart. The panel writes it with every held position and with the rest
  an armed servo takes when its profile changes: a rising rate after all
  pulse widths have landed, any other before them, holding the pulse widths
  back until it lands. It puts the page back to 0 at link-up and before
  every binding, and does not write a binding while that goes unanswered.
  The OUTPUT page shows whether the rate is in force, refused, or not taken
  by a protocol 4.0 coprocessor. The PWM
  driver's ceiling is 560 Hz.
- **SWEEP drives the servo through a curve on the coprocessor.** The
  SERVO page's registers 1 to 4 (protocol 4.2) start a square, sine or
  triangle about the surfaces' centre, at the TEST page's speed, range and
  dwell, run each pass by `servo_sweep` where the pins are; register 5
  ends it after a number of movements and register 6 counts them. It needs
  the bench armed, stops on a disarm and after 500 ms unwritten, and a
  finished sweep is not restarted by a repeat. The right card's SWEEP
  starts it, its HOLD stops it where the horn is, and the horn follows the
  same curve.
- **An on-screen keyboard** (`ui_textkey`) for names, and string settings
  beside the numbers (`settings_text()`), kept by the same store.

- **MOTOR & ESC counts the run's charge and energy.** Both read 0 on hardware
  because nothing filled them. The panel now counts them from the current and
  voltage it shows, while the bank is armed, from the arm
  (`bench_totals_t`): the ESC's own readings over extended DShot telemetry
  while the link is up, its model's while it is down. One count runs through
  the run whatever the source does, so the totals never go back within one
  log, and they stay after the run until the next arm. The time between two
  samples is measured, and a gap longer than 1 s counts as 1 s. A total
  nothing has counted is shown as `--` and written as an empty CSV cell,
  not as a 0. The totals count what the ESC reports: an ESC without a
  current sensor, such as the one in #172 that reports 72 A at idle, gives
  meaningless ones. The BENCH page's charge and energy registers are not
  used, and the protocol is unchanged.
- **SUPPLY sets, switches and records a programmable supply.** The screen
  is for the PD mini, a USB-PD (USB Power Delivery) trigger controlled over
  a UART (universal asynchronous receiver-transmitter): a voltage of 3.3 to
  21 V in 20 mV steps, a current limit of 0.5 to 5 A in 50 mA steps, a plot
  of voltage, current and power, the run's extremes, CV or CC, and the run's
  mAh and Wh. Each set point is shown beside its reading, in brackets on the
  rail and dashed in the plot. A tap on a card or a set point opens a
  keypad. OUTPUT ON is a 2 s hold and OUTPUT OFF a tap. Every stop the bench
  counts, a trip, an ON whose touch events went missing and a supply that
  stops answering switch the output off; leaving the screen does not. A run
  is one switch-on, recorded to a `BENCHnnn.CSV` of its own with the columns
  time, set voltage, voltage, limit, current, power, mode, charge and
  energy; an armed bench takes the log over. The PD mini's protocol is not
  in this repository, so the panel runs a model of a supply
  (`supply_sim_t`) and the tile is marked MODELLED.
- **SUPPLY has SETTINGS of its own**, kept in NVS (non-volatile storage):
  caps on the set points, the set points after a restart, a current trip
  and a voltage trip with a trip time, and whether a change to a live output
  asks first, for the slider and for the keypad separately. Both questions
  default to on. The PD mini's wiring -- enable, TX and RX pins, baud rate --
  is on SETUP under INTERFACES; no driver reads it.
- **The plot draws a series on another's scale** (`ui_plot_series_t.follows`),
  dashed and without a legend entry, and a numeric keypad widget
  (`ui_keypad`) is available to every screen.

### Changed

- **SERVO's trim (the CENTRE row), TRAVEL and TYPE are in its SETTINGS
  overlay** (trim as TRIM); the supply's plot takes their place on the right
  card.
- **The plot leaves a gap for a reading that did not arrive** instead of
  drawing it as zero.
- **A channel's endpoints may go down to 400 us** (`OUT_FLOOR_US`,
  `LINK_CC_FLOOR_US`), from 500 us, so a 760 us tail servo's travel, 410 to
  1110 us, reaches the pin. The coprocessor and the panel must both carry
  it: a coprocessor with the 500 us floor refuses HELI TAIL 760's range with
  BAD_VALUE.
- **The menu is five tiles by two**, 150 x 204 px each, with SUPPLY third.
  The tiles' lines are 16 characters or fewer to fit.

### Fixed

- **An output the PD mini switches off by itself stays off.** Its
  overcurrent protection or its button switching the output off while ON
  is asked no longer gets it switched on again: the driver holds it off
  and says so (SUPPLY flags bit 7), and the panel switches its own output
  off with a line in the band. A new 2 s hold switches it on again.

- **The bench does not arm while the SERVO page's rate is unknown.** A
  panel that restarted while the coprocessor held a heli rate, and whose
  reset to each slot's own rate at link-up went unanswered, could arm the
  surfaces at that rate from any screen, or pass on an arm made during a
  link outage once the link came back. Each arm now retries the reset and
  is refused with `servo frame rate not known -- arm again` while it goes
  unanswered; a bank already armed is not passed on to the coprocessor
  until the reset lands, and the reset is tried every pass until then,
  before an owed release is paid.
- **The panel's model and the log's clock ran slow while the link was
  down.** Each pass stepped them by a fixed 50 ms, and while the link is
  down a probe for the coprocessor's identity can hold a pass for its whole
  1000 ms timeout, so the modelled speed, voltage and temperature, the
  plot's run and the CSV's time column ran up to twenty times slow for the
  length of an outage. They now step by the measured time since the last
  sample, capped at 1 s.

Versions 0.9.1 and older are in [CHANGELOG-0.9.md](CHANGELOG-0.9.md).
