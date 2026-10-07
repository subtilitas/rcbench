# Changelog

Notable changes to rcbench. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git.

## Unreleased

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
  `during_menu`, `after_programming`. 24 profiles carry them: the 22
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
  for a `before_menu` step, powered with the stick at MIN; DONE goes on,
  ABORT and STOP end it, and no DONE within 60 s ends it with NOT
  CONFIRMED, red light lit. The result lists the `after_programming` steps.
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
  profiles join their maker. The count moves to the line under the rows.
- **The menu-starting step is listened through.** Pulling the jumper or
  pressing the button starts the Kontronik mode series at once, while the
  operator's hand is at the ESC. The run counts beeps from the moment it
  asks for that step; the menu heard in order, or DONE, takes it as done,
  and SILENCE and TIMEOUT run from then. Before, the run listened only after
  DONE, at least 1000 ms after the prompt, and lost the first groups.
- **The steps judge the model tapped.** A model refused for its voltage
  opened the steps with the family's lowest voltage, which could say the run
  will ask for them; the steps, the page, RUN and the warning now judge the
  model tapped.
- **A step says whether it starts the menu.** `manual[].starts_menu`
  marks the step whose action starts the series; the run listens from the
  prompt only for that step, and every other waits for DONE. One a profile,
  only on before_menu, none after it, in the generator and the card reader
  alike. 16 Kontronik profiles mark their pull or press.
- **Every step after programming shows.** The result showed at most two
  lines of `after_programming` steps; more are now counted there, the steps
  open by themselves over the result when the run ends, and MANUAL
  INTERVENTION REQUIRED in the result's header opens them again.
- **Manual steps in German.** A step carries its German in `action_de`
  (1-120 bytes of UTF-8), shown when the interface is German, with the
  English as the fallback. All 24 profiles' steps have it.

### Changed

- **SERVO's sweep button pauses and resumes.** While a sweep runs it reads
  PAUSE (in English and German) where it read HOLD. A tap holds the output
  where it has got to, as HOLD did, and the button reads PAUSED (German:
  PAUSIERT), filled in the warning colour instead of the accent. A tap on
  PAUSED carries the sweep on from the point of the curve it was paused at,
  its dwell and its count of ends included. A changed SPEED keeps the
  pause, and the resume runs at it. CENTRE, RELEASE, a finger on the dial,
  STOP, a disarm, leaving the screen, a hold unrepeated for 500 ms, and a
  changed type, frame rate, pulse, trim, travel or reverse end a pause,
  and the button reads SWEEP. Touch events going missing pause a running
  sweep.
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

## 0.9.1 - 2026-10-05

Two panel fixes. The ESC pulse endpoints, Idle pulse and Full pulse, reach
the motor channels and no others, and reach them when they change rather than
with the next pin ticked on OUTPUTS. A touch queue that loses events can no
longer hand a surviving event to a stale gesture or let a hold complete on a
finger that has gone: both queues number their events, and an arm is watched
until the screens have seen it. The link protocol is 4.0, unchanged, so either
image goes on alone.

### Fixed

- **A touch queue that filled dropped the release that ends an arming
  gesture.** The panel's touch queue evicted an entry when it filled, and no
  choice of which one is safe: a release that never arrives leaves a screen
  holding a press, a press that never arrives orphans the release after it,
  and the movement where a finger leaves a button is what abandons the hold.
  Both queues, the panel's and the driver's, now number every event their
  one producer offers, and the consumer finds a loss as a gap in the
  numbers, on the first event after it and before that event is handled,
  or at the end of a drain against the number published before it
  (`shared/safety/touch_loss.c`). The render task then tells every screen
  that its record of the glass is stale, since a surviving event can have
  navigated away from the one that holds the press: each screen drops the
  gesture in progress, which asks for nothing. A DISARM that cancellation
  posts is forwarded at once. Every control that holds state between a
  press and its release cancels, including the log viewer's buttons, rows
  and DELETE question and the tab rows of MOTOR & ESC, ANALYSER and
  BALANCE, because a press left latched owns a track id the controller
  reuses; a cancelled tab is redrawn released. The frame log carries the
  events found missing in each queue as `TOUCHLOST <panel>/<driver>`.
  - Two controls are the exception, because asking for nothing is their
    failure. Cancelling an armed bench's disarm still disarms: disarming is
    a press, so a release lost to a full queue is a disarm the operator made
    and the bench never saw. A touch stream that breaks while STOP is held
    stops the bench: the control task owns that press on its own, and the
    release that would have stopped the bench may be the event that went
    missing.
  - A hold cannot complete on a finger that has already gone. A screen's
    command is collected on the frame after the one that posts it, and the
    frame that observes a loss cancels before that collection, so an arm
    completed by a hold whose contact was lost is dropped before it reaches
    the bench. An arm already handed to the control task carries how many
    times the render task had dropped gestures and the last event it had
    taken; the control task drains the driver's queue, drops an arm the
    render task has had a loss since, and after applying it stops the bench
    on any loss until the render task acknowledges the armed bench, which a
    frame does only when it began armed and found the stream whole. The
    look runs inside every link exchange's wait, so a backlog of servo
    exchanges behind the arm cannot keep it driving. A later loss
    is seen by the screens against an armed bench. A hold
    is credited at most 250 ms per frame, so one late
    frame cannot complete a hold that began while it was dispatching the
    press.

- **Idle pulse and Full pulse reached servo channels, and an edit reached no
  channel until the binding was written again.** The two ESC / BENCH
  settings went into every channel the OUTPUTS binding wrote, so a SERVO PWM
  channel bound beside a motor took the ESC's range and rested at its
  midpoint, off centre for any range not symmetric about 1500 us. And the
  settings reached the coprocessor only inside that binding write, so a
  changed value did nothing until a pin on OUTPUTS was ticked again. The
  endpoints now go to throttle channels only
  (`outputs_chan_cfg_set_throttle_range()`), and an edit is sent 300 ms after
  the last change while the bench is disarmed, and at every link-up: the
  panel reads CHAN_CFG back, sets the throttle channels and writes the page
  only if it changed. The rewrite runs only in a poll whose control write
  put ARM = 0 at the far end and was acknowledged, and a coprocessor that
  links up while the bench is armed is held disarmed until it has the
  range. The
  case behind it is #170: an ESC calibrated on a 985 to 2012 us transmitter
  needs an Idle pulse of 980 us.

## 0.9.0 - 2026-10-05

Two reports from a bench and one protocol change. Extended telemetry comes on
with AM32 2.21, which showed speed and nothing else: voltage, current, power
and ESC temperature stayed empty because the bench asked before the ESC could
take the command. LOGS deletes a file from the card behind a second panel
that names it. The link protocol is 4.0: the write that arms carries the pole
count, and a panel and a coprocessor on different majors do not arm, so both
images go on together.

### Added

- **LOGS deletes a file from the card (#171).** DELETE in the browse footer
  is active once a file is selected and opens a second panel that names the
  file and its size. Only that panel's DELETE, pressed and released on the
  button, removes the file; CANCEL, or leaving the screen, deletes nothing.
  The panel refuses the run the logger has open. A deleted file that was open
  in the import view or the plot is dropped from both. `log_viewer_io_t`
  gains `remove`; with it NULL the browse view offers no DELETE.

### Changed

- **The link protocol is 4.0.** The write that arms carries ARM, THROTTLE and
  MOTOR_POLES as one three-register frame from offset 0 of the CONTROL page,
  so the coprocessor starts a run on the pole count the panel sent or does not
  start it: a frame is all-or-nothing at the far end, and a count edited
  between the panel's last pole write and its ARM write no longer reaches the
  coprocessor one 50 ms poll into the run, where the run's sticky `rpm_max`
  kept the speed the old divisor produced. MOTOR_POLES is register 2 of the
  page and CLEAR is register 3; a register changing meaning is a major. CLEAR
  stays a write of its own ahead of the arm, because the coprocessor checks
  ARM against its failsafe before it applies a CLEAR from the same frame. The
  page's rules -- the throttle range, a pole count even and 2 to 42 or zero,
  the CLEAR magic, ARM refused in failsafe, a refusal storing nothing -- are
  `shared/link/link_control.c`, shared by the coprocessor and the host suite,
  under `test_link_pages`.
  - A panel and a coprocessor on different sides of the bump do not arm. The
    panel treats a coprocessor reporting another protocol major as absent,
    keeps the link down and raises `protocol mismatch -- will not arm`, so
    both images go on together.

### Fixed

- **Extended telemetry comes on with AM32 (#172).** The bench sent command 13
  only as the first ten frames of a run. AM32 2.21 takes a command only once
  it has armed itself on 1 s of zero throttle, and it restarts after 0.5 s
  without a frame, which a disarmed bench never sends, so every ask arrived
  too early: speed showed, and voltage, current, power and ESC temperature
  stayed empty. The ask is now repeated every 500 ms while the throttle is at
  zero, until an extended frame comes back or 10 asks have gone, and it never
  takes the place of a throttle above zero. Replies are read as extended
  telemetry from the first repeat, so the one status frame AM32 sends when it
  takes the command is heard. The schedule is `shared/dshot/dshot_edt.c`,
  held by `test_dshot_edt` against a model of AM32 2.21's command and arming
  rules; it has not been tried on an ESC.

## 0.8.2 - 2026-09-10

Ten defects found in review of 0.8.1 and one reported from a bench. Three
reach a pin: a servo bound beside a motor drove its low endpoint for 500 ms
on every arm from MOTOR & ESC, a CONTROL write refused on one register kept
the registers ahead of it, and the extended-telemetry enable went out
without the bit that marks it as a command. The Motor & ESC plot holds the
last run instead of scrolling it away. The link protocol is 3.0, unchanged,
so either image goes on alone.

### Fixed

- **A servo bound beside a motor drove its low endpoint for 500 ms on every
  arm from MOTOR & ESC.** The coprocessor filled the CHANNELS page with zero
  at boot and applied it to the output bank as commands. Zero is a throttle's
  rest and a surface's low endpoint, so every channel bound as a surface was
  asked for its endpoint; arming stamps every channel's clock, so the 500 ms
  staleness rest could not return it to centre until a whole timeout after
  the arm. On the default range that is 1000 us held for 500 ms; on a 660 to
  860 us servo it is 660 us, which is that servo's hard stop, reached in one
  step because the default slew is zero. It repeated on every arm until
  something commanded that channel. Arming from SERVO was unaffected: that
  path centres the surfaces first. At boot the page now takes its values from
  the bank, so a channel nobody has commanded reads back at its role's rest.

- **A failsafe reset the CHANNELS page to zero.** The failsafe edge disarms
  the bank and filled the page with the same zero boot used to, so after one
  an uncommanded surface rested at its centre and read back as its low
  endpoint until something commanded it. The page is filled from the bank on
  that edge too: an uncommanded channel reads its rest, and a commanded one
  reads what it was asked for, with ARM = 0 beside it.

- **A refused settings write reported SAVED.** The store's save callback
  returned nothing, so the model cleared the dirty flag whatever happened and
  the button was drawn inert, which prevented the retry that would have
  noticed. A panel whose NVS could not be brought up reported a save for
  every press of the session. The callback returns a status; a refusal keeps
  the values dirty and shows NOT SAVED; every key of the NVS write is
  checked. A refusal part way through leaves the keys written before it
  committed, so the next boot can load a mix of the new values and the old,
  and the screen cannot say which. On this version of ESP-IDF `nvs_commit()`
  answers OK for any valid handle, so the console line that said "settings
  saved" printed even when every key had failed, and now prints only when
  every key was taken.

- **An analysis pass that stopped reading was reported as a clean read.** The
  second of the CSV log's three passes finalised its column statistics
  without checking whether the source had failed, so a card that stopped
  answering part way gave a row count, a unit and a time axis derived from
  the rows that arrived. On a 40 s run logged in milliseconds under a bare
  header, a failure 300 bytes in leaves 31 of 400 rows, whose span reads as
  seconds -- and a later successful build then plots 39,900 s. Nothing on the
  import screen was drawn as a warning.

- **A pole count edited while the coprocessor was answering never reached
  it.** It was sent only when the link came up, so the far end went on
  converting with the value it was handed at boot: setup showed the new
  number, the Motor screen and the run's CSV carried the old one. The guard
  that reports no speed when none was sent covers zero only, and every count
  the schema allows is non-zero, so a stale count reaches the plot and the
  CSV carrying a valid bit. Fitting a 12-pole motor to a bench still holding
  14 reads 14.3 % low. An edit is now owed to the far end and paid at the
  next poll, and, when it is still outstanding, once more before the write
  that arms -- a run begun on the old divisor puts a wrong speed into its
  sticky peak, which no later correction removes. An edit landing in the few
  instructions between that payment and the arming write reaches the far end
  at the next 50 ms poll, so up to 50 ms of that run is converted with the
  previous count; STATUS.md carries the window as an open item. A write
  nobody answers stays owed; one the coprocessor refuses is not retried.

- **Extended DShot telemetry was requested with the command frame's telemetry
  bit clear.** On a value of 1 to 47 that bit is what marks the frame as a
  command for the BLHeli_S family, which discards a command without it and
  zeroes its repeat counter, so the ten repeats never accumulated to the six
  its handler counts. AM32 has no such gate. Where it landed, temperature,
  voltage, current, stress, status and power stayed empty for the life of the
  bench.

- **A CONTROL write refused on one register kept the registers ahead of it.**
  The coprocessor's handler validated and stored in one pass, so a rejected
  write left part of itself applied -- including a CLEAR, which lifts a
  latched link failsafe while the panel is told the write failed. It now
  validates the whole frame before storing any of it, which is what the
  output page handlers already do.

- **A refusal naming a later fragment of a wide write was discarded.** The
  host matched a NACK only against the transaction's starting offset, so the
  transaction waited out its 1000 ms timeout and lost the reason -- which
  turns "the coprocessor refused these pins" into "there is no link" on the
  outputs screen. Latent on a matched pair, reachable across a firmware skew.

- **The Ramp limit setting governed nothing.** It was read by no path in the
  tree; the panel's throttle bank used a compile-time constant that merely
  equalled its default. It now sets the slew on the panel's own bank, which
  is the modelled bench. A coprocessor that is answering is sent the raw
  command and a pin bound as a throttle steps to it on the next 1 ms pass.
  That is by decision, recorded in STATUS.md under Not planned: the bank
  ramps a throttle upward only, so a ramp on the wire would slow the rise and
  nothing else. A tap moves the slider by 1 %; a swipe across the whole track
  inside one 50 ms poll is a 0 to 100 % step at the pin, and only the ESC's
  own ramp is under it.

- **The version header documented the wrong string.** `rcbench_version.h`
  described `RCBENCH_VERSION_STRING` as "0.8.0" while the defines above it
  make 0.8.1. The macro composes its value from those defines, so the comment
  compiled; `check_docs.py` compared only the numeric defines and could not
  see the line. It reads the comment now.

- **The Motor & ESC plot advanced whether or not the bench was armed**, so a
  run scrolled off the left edge about 27 s after it ended and there was no
  way to hold it. Reported from a bench. The run clock above the plot already
  holds the last run's length once it stops; the plot threw away the trace of
  the same run, and the CSV file it corresponds to closes on the same edge, so
  the two described different intervals. The trace now advances only while the
  bench is armed: arming clears it, disarming holds it as it stood. The panel
  tag reads `TELEMETRY HELD` while it holds a run and `TELEMETRY IDLE` over
  `no run recorded` before the first arm, the right axis reads `END` rather
  than `NOW`, and the plot is framed -- a held plot that looked live would be
  a worse defect than the one being fixed. The readouts, the TABLE pane, the
  totals and the temperature strip stay live at all times. What is given up:
  a disarmed bench has no rolling trace, so the current falling after a STOP
  is on the readouts and not on the plot.

### Changed

- **The sanitizer job instruments the code it tests.** The flags were applied
  after the shared libraries were added, and a directory takes its copy of
  the compile options as it is added, so the 44 test executables were
  instrumented and the 67 shared translation units they exercise were not.
  Every UBSan check in `shared/` was absent. `tools/check_sanitizers.py`
  fails the build if that returns.

## 0.8.1 - 2026-09-09

No output could be bound on 0.8.0. Choosing a protocol on SETTINGS/OUTPUTS put
the screen back to `OFF` within one poll, and `OFF` takes no pins, so no pin
could be ticked and no binding could be made. PICK A PIN had the same cause.
There is no workaround on 0.8.0.

### Fixed

- **No output could be bound at all on 0.8.0.** Choosing a protocol on
  SETTINGS/OUTPUTS put the screen straight back to `OFF`, and `OFF` takes no
  pins, so nothing could be ticked and no binding could be made. Picking a
  protocol posts the binding and the panel reads it straight back, so a lost
  acknowledgement cannot leave the screen showing what the coprocessor is not
  doing. But a page carries pins, and the protocol read back out of one is the
  lowest that holds a pin -- there is nowhere on a page to say which protocol
  is being edited. A protocol just chosen holds none, so the read-back named
  something else and, landing whole, took the choice away within one poll.
  Reported from a bench on the first boot after 0.8.0, which is when it always
  happens: the record format changed, so every store reads as unwritten and
  every operator starts at `OFF`.

  Which protocol is being edited is the screen's, and a protocol equal to what
  the pins already say is no longer treated as a claim about it. Two more
  cases go with the first. A second protocol started while one is bound is not
  on the page either, so a bench wired for an ESC could not then add a servo;
  and editing the higher of two bound protocols was dragged back to the lower
  one at every poll. A protocol named by something that is not a page -- the
  screen being posed, or told what to show at start-up -- still lands. A
  change of board takes the protocol with it, as it already took the pins,
  because a protocol chosen for the hardware that was there is worth no more
  than a pin index into its catalogue. PICK A PIN follows: it has no protocol
  control and only reads one to know which group a tap joins, so it takes what
  the OUTPUTS screen reconciled rather than what came off the wire.

## 0.8.0 - 2026-09-09

Bidirectional DShot leaves the pin inverted, which it never has. Whether an
ESC answers it is untested: plain DShot has run a motor on the bring-up bench,
and nothing bidirectional -- no inverted frame, no reply, no telemetry -- has
been put against an ESC. A save on the OUTPUTS screen costs one page program rather than
an erase and a program, so the erase that stops the coprocessor answering for
about 19 ms falls to one save in sixteen and is taken in a quiet window
instead of under the save. Neither window has been measured on this build, so
whether a save can still cost a CAN frame -- and with it `FAULT 01` -- is
open. The run log is written by a task of its own rather than by the one that
beats the safety line, and is committed as the run goes rather than at the
disarm. SPEED on the servo screen renders a rate below 50% that differs from
50%.

Several things change what a bench does after it is flashed, and each needs
something from the operator or shows them something new:

- **The link protocol is 3.0**, so both images go on together. The panel
  refuses to arm against a coprocessor that speaks another major.
- **The output store's record format is version 3**, so the first boot starts
  from no binding at all. The pins are ticked again on SETTINGS/OUTPUTS, and
  that save writes the first version 3 record.
- **The SERVO screen drives the channels the binding marks as surfaces**
  rather than channel 0 on GP2. Any surface channel counts, so a bound PPM
  output moves with it as a SERVO PWM one does. With the store reset above it
  drives nothing until something is bound as a surface.
- **The coprocessor sends `DSHOT_CMD_EDT_ENABLE` to the ESC on every edge into
  driving**, ten frames at the 1,000 Hz update rate. Nothing acknowledges it,
  so the bench reads replies as extended telemetry afterwards whether the ESC
  agreed or not. One that acts on the command reports temperature, voltage and
  current. One that ignores it *and* does not normalise its exponent has some
  of its speed frames read as temperature or voltage -- the case under Changed
  below, and the reason to watch the first readings on a new ESC.
- **The bench shows the ESC's own voltage, current, power and temperature**,
  which it never did. `MOT` stays blank unless the ESC reports a second
  temperature, where it used to read `0C`.
- **The run log writes an empty cell** where nothing measured a quantity,
  where it used to write a number. A reader that treated 0 as a measurement
  sees a gap instead.

### Fixed

- **Bidirectional DShot never inverted the pin.** `out_dshot_bind()` set the
  pad's output override and then called `dshot_bidir_tx_program_init()`, which
  begins with `pio_gpio_init()` -- `gpio_set_function()` -- which assigns the
  pad's control register whole rather than masking it. `OUTOVER` is one of the
  fields it zeroes, so the inversion was wiped before a frame went out. Every
  `DSHOT300 BIDIR` and `DSHOT600 BIDIR` frame therefore left in plain-DShot
  polarity carrying the complemented bidirectional checksum, which is wrong
  polarity for one reading and a wrong checksum for the other. The pull-up
  survived, because pulls live in the pad block, so the line still idled high
  and the fault read as a protocol no ESC accepts rather than as a pin that
  was never inverted. Plain DShot was untouched, which is why one protocol on
  the same pin ran a motor and the other did nothing. The override goes on
  after the init now, and the bind reads `io_bank0_hw->io[pin].ctrl` back and
  refuses rather than driving a line whose polarity it could not set. The
  inverted path itself is not run on hardware: an ESC has been on the pin for
  plain DShot and none has been asked for a bidirectional reply, so whether
  one answers is still open.
- **The servo horn drove a fixed pin rather than the binding.**
  `write_servo()` wrote channel 0, slot 0 and pin GP2 whatever the operator
  had bound, and slot 0 is where the first pin ticked on SETTINGS/OUTPUTS
  goes, so the horn took that binding over. With a DShot motor on the lowest
  pin the slot write is refused because its pin is already taken, the channel
  writes stand, and the motor's channel is left at half travel. The screen
  drives the channels the binding marks as surfaces now, by the same walk the
  binding itself makes, and writes no slot at all.
- **The LOGS screen could not reach the card.** `log_viewer_set_io()` was
  called by two tests and by nothing in `firmware/panel/`, so the viewer took
  its no-volume branch and showed "No card" whatever was in the slot, and
  RESCAN re-entered the same branch. It is given an adapter over the storage
  component, and RESCAN mounts a card put in after boot, which the only
  `storage_init()` at the time -- in the splash -- could not.
- **A power cut during a run left a 0-byte CSV (comma-separated values)
  file.** The run log was written and never committed, and closed only on the
  disarm edge. FAT (file allocation table) keeps a file's length in its
  directory entry, and `fwrite` alone never writes that entry, so the whole
  run was lost whatever the data sectors held. The file is committed every 20
  rows or 1000 ms of run, whichever comes first, with `fflush` followed by
  `fsync`. What a power cut costs is that commit interval plus the queue
  between the control task and the logger: at most `LOG_WRITER_FLUSH_ROWS`
  (20) uncommitted rows in the writer and at most `LOG_Q_LEN` (64) in the
  queue. Twenty and not nineteen -- the count reaches 20 before the commit is
  attempted and is cleared only once it succeeds, so those rows are not
  durable for as long as the card takes. With a card keeping up the queue is
  empty and the cost is under 1.0 s of run; with a card stalled it is 84 rows,
  4.20 s at the panel's 20 Hz sample rate. Past 64 queued rows the control
  task drops them and counts them, so the loss stops growing there and is
  reported when the run closes.
- **The run log put SD (Secure Digital) card writes on the safety line.** Row
  writes, the file-name scan and the close ran on the control task, which
  drives the heartbeat on GPIO6 and reads STOP. That task's ceiling is
  HEARTBEAT_MAX_GAP_MS (150 ms) and the coprocessor fails safe after 200 ms of
  link silence, while the SD specification allows a card 250 ms to finish one
  single-block write: a card that paused was a dropped heartbeat, not a late
  row. Every card access the run log makes is on the `runlog` task now, and
  rows cross to it on
  a queue the control task never waits on. The LOGS screen's own reads are not
  on it: the viewer opens a run from the task that renders, which is why the
  logger publishes the run it has open and the viewer leaves that one alone. A
  card that falls behind the run
  costs rows, which are counted and reported on the panel when the run closes,
  rather than costing the heartbeat.
- **A save on the OUTPUTS screen cost CAN (Controller Area Network) frames.**
  The coprocessor erased and programmed one flash sector per save, which held
  interrupts off for a measured 19,174 to 19,186 us; a frame is about 130 us
  at 1 Mbit/s and the XL2515 holds two, so the receive buffers overran in step
  with the saves -- the count climbed from 2 to 8 across nine of them -- and
  the bus reported no error, because the frames arrived and nobody collected
  them. A lost request costs the panel `LINK_HOST_TIMEOUT_MS` (1000 ms) of
  waiting and 1000 ms of silence latches the coprocessor's 200 ms failsafe, so
  one lost frame ends as `FAULT 01`. The store is now two sectors of sixteen
  record slots: a save programs one page, and a sector is erased only once
  every record in it is superseded, which is one erase per sixteen saves.
  That erase is taken ahead of the save that needs it: at boot before the CAN
  controller is started, where every sector there is to reclaim is taken and
  nothing can arrive to be lost, and otherwise on the pass after the save that
  leaves a sector behind. Both windows wait for `OUT_STORE_QUIET_MS` (5 ms) of
  silence, which is a minimum quiet time rather than a promise of where in the
  panel's 50 ms poll cycle the window opens. A settled save takes its
  window without a gap after `OUT_STORE_GAP_WAIT_MS` (1000 ms), so a busy bus
  cannot postpone a binding for ever.
- **SPEED below 50% moved the servo horn at 50%.** `outputs_step()` rounded
  each slew increment up -- `(slew_per_s * dt_ms + 999) / 1000` -- so a step
  delivered at least one span unit whatever the rate said, and the
  coprocessor's loop steps about once a millisecond. Every `slew_per_s` under
  1,000 units a second therefore rendered as 1,000. SPEED on the servo screen
  is `2 * OUT_SPAN * pct / 100`, so every setting from 1% to 49% crossed at
  the same rate as 50%. The remainder is carried between steps instead: the
  rate is the one asked for, and a slew slower than one unit a step still
  arrives rather than being truncated to nothing. The elapsed time is capped
  at `1000 * OUT_SPAN / slew_per_s + 1` ms, which is the interval past which
  one step
  covers the whole span and arrives regardless, so the cap discards nothing
  that could move the channel and the multiply still fits a `uint32_t`.
- **The coprocessor armed on the previous pass's heartbeat.** `outputs_arm()`
  read the cached `s_beat.alive` and `heartbeat_poll()` ran after
  `outputs_hw_service()`, so a line that went past `HEARTBEAT_MAX_GAP_MS`
  (150 ms) got one more service of every output before the poll noticed.
  Silence generates no event, so the poll is what notices it: it runs first
  now, and the arm below it uses this pass's answer.

- **Two servo pins 16 apart drove from one pulse width.** On the RP2350 a PWM
  (pulse-width modulation) channel is one compare register, and GPIO
  (general-purpose input/output) numbers fold onto the 12 slices: GP0 to GP31
  take slice (pin / 2) modulo 8, GP32 to GP47 take slice 8 + (pin / 2) modulo
  4, and the channel is the low bit of the pin number. `out_pwm_bind()`
  compared slices and not channels, so the second pin of a folded pair was
  accepted whenever it asked for the frame rate the first one runs at, and
  muxing it put both pads on one compare register: two leads, one pulse width,
  and only the pin written last saying what it is. Six such pairs are free on
  the coprocessor's header -- GP0 and GP16, GP1 and GP17, GP2 and GP18, GP4 and
  GP20, GP5 and GP21, GP6 and GP22 -- and the pin arrives from the OUTPUTS
  page, so ticking two of them on the outputs screen reaches it. The second pin
  of a pair is refused. The fold is `shared/outputs/out_pwm_map.c`, where the
  host suite holds it against the pico-sdk's own mapping, and `out_pwm_bind()`
  compares the two on every bind rather than assuming they agree.

  A refused bind leaves the slot unbound, and the OUTPUTS page carries no
  register saying whether a slot is bound, so an unbound slot reads back like a
  driving one. That is recorded as an open item rather than fixed here: it
  needs a bit on the page and a screen that draws it.

### Changed

- **The link protocol is 3.0.** `LINK_BN_TEMP_OK` used to gate both the ESC
  and the motor temperature, so an ESC that reports one turned `MOT --` into
  `MOT 0C`; it now validates the ESC's alone and `LINK_BN_TEMP_MOT_OK` (bit 4)
  validates the motor's. That is a meaning change on an existing bit rather
  than an addition, which is what moves the major. The panel refuses to arm
  against a coprocessor whose major differs, so both images go on together.
- **The bench page carries the ESC's own measurements.** Voltage, current,
  power and ESC temperature were zeroed every sample and had no source at all;
  the coprocessor publishes them from extended DShot telemetry now, on a
  2,000 ms staleness window against 200 ms for speed. There is no measurement
  front end on the coprocessor, so these are the ESC's numbers and not the
  bench's.
- **`DSHOT_CMD_EDT_ENABLE` goes out on every edge into driving**, ten frames
  at the 1,000 Hz update rate, because extended telemetry has to be asked for.
  The case this cannot survive is an ESC that ignores command 13 and does not
  normalise its exponent: some of its speed frames would be read as
  temperature or voltage. One that normalises is safe either way. Unmeasured.
- **`SET_TELEM_SRC`, `SET_OUT_PROTO`, `SET_OUT_PIN` and `SET_TELEM_HZ` are
  gone** from SETTINGS / ESC & BENCH. They had no consumer anywhere in the
  tree: the output protocol and pin come from the OUTPUTS page, and the
  telemetry source and rate were read by nothing. Settings are stored in
  non-volatile storage by name, so removing them orphans four keys and
  disturbs nothing else.
- **A run takes one number above the highest the card holds**, rather than the
  lowest free one. A gap left by deleting a run on a computer is not filled,
  because a run written into one is the newest run wearing the oldest number
  and a full list would rank it last and drop it. A card already holding
  `BENCH999.CSV` therefore records nothing, where before it would have taken a
  gap. Deleting *that* run is what frees the number, because a gap lower down
  is not filled -- and deleting it means the card comes out, which the panel
  cannot see: a card swapped while running is not re-mounted, so the panel has
  to be restarted for the change to be read. The numbering is re-read after a
  run that could not be opened, which covers a card that stays in the slot;
  it cannot cover one that was taken out from under a mount that is still
  standing.
- **The run log writes an empty cell for a quantity nothing measured.** Each
  column carries the valid flag that says whether anything measured it, and a
  field with no flag set is written empty rather than as `0`, so the reader
  refuses it instead of guessing. A reader that treated `0` as a measurement
  sees a gap now.
- **A power cut during a save leaves the binding from before it.** A record is
  only ever programmed into an erased slot, and a sector is only ever erased
  while the live record is in the other one, so the record being written can
  be torn without taking the previous one with it. The store's checksum covers
  the record's sequence number as well as its configuration: an erase lifts
  bits towards 0xFF, and a record caught half way through one has to fail its
  check rather than outrank the record still wanted. A checksum alone makes
  that likely and not certain -- 65,535 chances in 65,536 per candidate, over
  an enormous number of candidates -- and the same is true of a program that
  did not finish, which leaves a header that has landed above a configuration
  that is part 0xFF. So each record carries the number of 0 bits it holds
  from its checksum onwards, beside that number's own complement. An erase
  sets bits and a program clears them, so either one interrupted leaves fewer
  0 bits than the record claims, and the claim cannot be faked: keeping the
  complement pair across a partial write would need a bit cleared to pay for
  one that was set, or set to pay for one cleared, and no single flash
  operation does both. The rule is `out_store_intact()` in
  `shared/outputs/out_store_map.c`, where the host suite holds it against
  every bit position of both words and against a count one either side of the
  written one. The record format is version 3; a store written by an earlier build reads as unwritten, so the
  first boot on this build starts from the defaults -- no driver and no pin in
  any slot. The panel keeps no binding of its own and sends none unasked: it
  reads the pages back when the link comes up and shows nothing configured, so
  the binding is set again on the OUTPUTS screen, and that save writes the
  first version 3 record.
- The coprocessor times the erase and the page program separately and prints
  four lines: `outputs saved, record <n>, program window <n> us`, `output
  store sector erased, window <n> us`, `output store sector reclaimed, window
  <n> us` and `output store sector reclaimed at boot, window <n> us`. Neither
  window is measured on hardware: the 19,174 to 19,186 us above is an erase
  and a page program inside one window, from the store this replaces.
- The coprocessor's store takes the last two sectors of the first 4 MB rather
  than the last one, so the image size check falls from 4,190,208 to
  4,186,112 bytes.

### Added

- **`testbench/`, the measurement bench.** A Raspberry Pi 5, a Kingst LA2016
  logic analyser and an RP2350 board, wired so this project's protocol claims
  can be measured rather than asserted. `testbench/README.md` says what it is
  for and why each part is there; `testbench/WIRING.md` is what to connect, in
  order, with a check after every step. **Nothing on it has been run**: the
  hardware is being assembled, the channel map is a proposal to be checked
  against the wiring as built, and the decoders and recipes are described
  rather than written. Two tap points are stated as unknown rather than
  guessed -- where the panel's I2C bus is brought out, and where RXCAN can be
  probed between the XL2515 and its transceiver.
- **The LOGS screen reads a run off the card**, which is what the viewer was
  written against a fake for. It lists `.csv` runs, opens one and analyses it;
  a `.bfl` decoder does not exist, so the empty line says `.csv` only.
- `test_outstore`, the forty-fourth host binary: where a save goes in the
  store, which sector can be erased and what a power cut in either leaves
  behind, in `shared/outputs/out_store_map.c`. The rules are on the host
  because the board has one copy of the sector and the cases need many.

## 0.7.0 - 2026-09-08

The servo screen can drive a servo. Arming existed only on MOTOR & ESC and
leaving that screen disarms, so the horn followed the finger and nothing
moved; SPEED changed the drawing and not the servo; and every bench showed
FAULT 01 from power-up.

Most of what follows is the arming and command path either side of that
button. A stop, a disarm and a release each have to survive a link that is
slow, a queue that is full, a screen that is being left and a touch
controller that stops answering, and each of those was a way for something to
keep driving after the operator had asked it to stop.

### Fixed

- **Every bench showed FAULT 01 from power-up.** The coprocessor's silence
  watchdog counted from its own boot, and it is awake a few hundred
  milliseconds before the panel starts polling, so LINK_DEV_SILENCE_MS
  (200 ms) elapsed before the first request could arrive and the link-silent
  fault latched on every bench, before anything had happened. It cleared only
  on the first arm. The watchdog does not run until a request has arrived:
  the wait for the first one is not silence. Reported as #99, where an
  operator asked whether the bench was broken.

### Added

- **ARM on the servo screen.** Arming existed only on MOTOR & ESC and leaving
  that screen disarms, so the servo screen could not drive a servo at all: the
  coprocessor writes a pulse of length zero to every PWM (pulse-width
  modulation) pin while the bench is not armed, and there was no way to reach
  an armed bench with the horn on screen. The button is the same two-second
  hold as the other screen's, and the gesture itself -- the hold, what a
  finger leaving the control does to it, and what happens when the bench
  disarms under a press -- now lives once in `ui_widgets` (`ui_hold_t`) rather
  than twice. Leaving the screen disarms and releases the pin. Reported as #99.

### Changed

- **SPEED on the servo screen sets the rate the bench moves the output.** It
  moved the drawn arm and nothing else, so a servo went at its own rate
  whatever the screen said. It is the channel's slew now, carried with the
  command: 100% is immediate, which is where the screen starts and what
  everybody had before, and below that the bench ramps the command in front
  of the servo -- 30% takes three times as long to cross as 90%. Changing it
  applies to an output already being held.

### Fixed

- **A servo swung back to centre half a second after the finger stopped.**
  The panel wrote the servo's channel on a touch and never again; a channel
  nobody has commanded for OUT_DEFAULT_TIMEOUT_MS (500 ms) goes to its rest,
  which for a surface is mid-travel. The position is now said again every
  100 ms while something is being held, one register at a time. Not reachable
  before this release, because the servo screen could not arm.
- **The disarm inhibit lasted one pass instead of 300 ms.** Whether the timer
  was running was encoded by forcing its timestamp non-zero, which moved it
  a millisecond into the future whenever the clock was even; the elapsed time
  then wrapped to its maximum and ended the inhibit immediately. A far end
  that could not be reached never saw its 150 ms of silence and kept driving.
  The timer's state is its own flag.
- **The disarm inhibit was lifted by the transaction meant to honour it.**
  The request was cleared before the write that delivers it, so the pump
  inside that write's wait saw nothing outstanding and put the safety line
  back up -- with the far end still armed if the write was lost. The request
  stands until the far end has taken it, or until the line has been down
  twice HEARTBEAT_MAX_GAP_MS (300 ms), by which time the far end has failed
  safe on its own account. A disarm nobody can deliver does not hold the line
  down for ever.
- **An explicit RELEASE waited for the end of the drain.** Only the disarm
  branch cleared the slot it had let go of; a release recorded the debt and
  left it to the post-drain service, behind whatever else was queued -- two
  blocking exchanges for an outputs binding, for instance. Both pay it before
  returning to the queue.
- **A disarm could not reach a write already on the wire.** The far end
  applies a write and then acknowledges it, so a disarm arriving while that
  acknowledgement was in flight found the output already bound -- and a lost
  acknowledgement left it driving for the full LINK_HOST_TIMEOUT_MS (1000 ms)
  with the request unserved. The safety line is the one channel that does not
  need the link, so it now carries an unserved disarm: on a healthy link the
  request is served on the next pass and nothing changes, and on a link that
  has stopped answering the far end fails safe. A release does not do this;
  letting go of one pin is not worth latching a failsafe.
- **A touch outage that recovered left the bank armed.** Touch can die and
  answer again inside one blocking link exchange: by the time the policy ran
  the controller was healthy, so nothing in the state said the outage had
  happened, and the heartbeat need not have been withheld long enough for the
  far end to fail safe either. What was armed comes down, on the edge that
  saw the outage rather than on the state afterwards.
- **A servo's three writes could not be countermanded partway through.** Each
  waits up to LINK_HOST_TIMEOUT_MS (1000 ms) with the pump running inside it,
  so a stop or a disarm could arrive between one write and the next and the
  slot was bound anyway. The writes stop when that happens, and the slot is
  bound by the last of them, so the pin is left unbound rather than driving.
- **RELEASE waited behind the backlog while DISARM did not.** Tapping RELEASE
  lets go of the pin without disarming the bench, and it queued like any
  other command -- behind positions that are three exchanges each. It is out
  of band now, as the disarm is, and it voids drive commands that predate it.
- **An arm went through even when the operator changed their mind mid-write.**
  Arming is two exchanges, each of which can wait a second, and the pump runs
  inside both: a stop applied there, or a disarm posted while the failsafe
  clear was still on the wire, was not looked at before the write that
  actually arms. The far end could then be driving for a timeout before
  anything took it back. The bench is asked again between the two.
- **A disarm waited behind a backlog of positions.** Making it unloseable did
  not make it prompt: the flag was read once before the queue was drained, and
  a backlog of servo positions is three exchanges each, so a degraded link
  could keep the bench armed for seconds after a disarm. The flags are
  serviced between queue entries now, and a command that predates a disarm no
  longer drives after it -- each carries the count of disarms its sender had
  seen, as it already carried the count of stops.
- **A stop the pump saw but could not route could swallow the next one.** The
  marker that stops the backstop counting a press twice was set whether or not
  the event reached the router; when the router's queue was full it never
  latched, and the marker stayed armed until it consumed a later stop the
  backstop really did have to apply. It is set only when the event was
  actually routed.
- **One STOP press counted as two stops.** The press is applied where it is
  seen, and the screen still receives the event, so the router latched the
  same release and the backstop stopped the bench again. The count is what
  rejects commands made before a stop, so a command the operator made after
  it -- between the two -- was thrown away. A press the pump applied is not
  counted again by the backstop.
- **An arm could be granted from a gesture invalidated while it waited.** The
  servo screen's arm releases the slot first, and that release can wait a
  second on the wire with the pump running inside it, so a stop -- or touch
  dying and recovering -- could invalidate the gesture between the drain's
  check and the request. The gesture is asked about again after the release.
- **A newer servo write did not void an older release.** A release that failed
  keeps its debt, and a position written afterwards rebinds the slot to what
  was asked for; paying the debt then cleared it and left the pin dead until
  the next refresh. A write that the far end acknowledged voids a release
  owed for that slot, and one that failed does not.
- **A leave-time disarm could be evicted from the command queue.** The queue
  drops its oldest entry when full, and leaving a screen generates commands
  on the way out while the next screen generates more, so the disarm posted by
  leaving an armed servo screen could be dropped -- leaving the bench armed
  and the servo held behind a screen nobody was watching. A disarm is a flag
  as well as a queue entry now, the way a stop is; applying it twice costs
  nothing.
- **An older servo release could clear a binding that had just been made.** A
  release that timed out keeps its debt, and a binding applied afterwards in
  the same drain writes every slot including slot 0; paying the debt then
  cleared the binding the outputs screen had just been told was written, and
  the far end kept the cleared page. A binding that lands says what slot 0 is,
  so it voids an older release owed for it.
- **STOP waited out a link exchange before it did anything.** The press was
  recorded and acted on by the policy, which is exactly what a blocked loop
  cannot reach: a servo command makes up to three exchanges of up to
  LINK_HOST_TIMEOUT_MS (1000 ms) each, and the far end drives throughout. The
  press is applied where it is seen now, in the pump that runs inside that
  wait, so the safety line stops being asserted in the same pass and the
  coprocessor fails safe within HEARTBEAT_MAX_GAP_MS (150 ms) whatever the
  panel is waiting for. The rest of a stop follows when the loop is free.
- **A stop on a bench that was not armed left the servo driven.** Letting go
  hung off the policy's disarm, which does not happen when nothing was armed,
  so a servo held from before the stop kept its slot refreshed every 100 ms --
  over the top of an outputs binding made afterwards. Every stop lets go now,
  counted rather than inferred from the disarm.
- **A position asked for before a stop was still driven after it.** Only
  arms were dropped as stale; a servo position or centre queued before a stop
  was applied afterwards, rebinding the slot the stop had just released and
  refreshing it from then on -- and a stop from touch dying or from the far
  end has no queued STOP behind it to release it a second time. Nothing that
  drives an output survives a stop it predates; a disarm, a release or a
  binding still does.
- **A stop waited behind commands that talk to the link.** The control loop
  drained the screens' commands before it served a pending STOP, and an
  unanswered exchange holds that loop for LINK_HOST_TIMEOUT_MS (1000 ms) --
  several such commands multiply it, with the far end armed throughout. The
  stop is served first again. What made that unsafe before was an arm queued
  from a gesture made before the stop, and each command now carries the count
  of stops its sender had seen, so a stale arm is dropped instead.
- **An arm learned after this frame's touch discarded a position issued after
  it.** The screens are told whether the bench is armed at the end of a
  frame, and an arm discards what was held before it; a position commanded
  earlier in the same frame, after the bench had actually armed, was thrown
  away by that. The bench's state is read before the frame's touch is
  dispatched, so a press acts on what the bench already is.
- **A stop on a bench that was not armed left the servo screen holding.**
  Dragging while disarmed commands a position, and a stop then changes
  nothing about the armed state while the panel releases the slot all the
  same. The screen cleared its driving state only on that state's edge, so it
  went on showing the output as held and the next change of type, trim or
  travel said the released position again. A stop lets go, whether or not
  anything was armed.
- **A touch outage inside a link wait left no trace.** The stop was counted
  only where the policy steps, and a link exchange waits up to 1000 ms while
  touch is still being pumped: a controller that died and recovered inside
  one such wait was never seen to have died, and a hold left standing by it
  completed and was accepted. Touch health is judged where touch is judged
  now, at the rate it is judged.
- **An arm still settling was not abandoned when touch died.** The disarm was
  gated on the bench being armed, and an arm that is settling is not armed
  yet, so a controller recovering before the deadline armed the bench from a
  gesture nobody had re-made -- and no screen could retract a request the
  policy already held.
- **Leaving MOTOR & ESC under a held ARM stranded the button.** No release
  arrives for a contact that was on ARM as the screen changed, and the press
  stayed recorded; with the hold kept for the contact that began it, every
  later press was then refused. Leaving drops the press it was routing.
- **Touch that stopped answering left an arming hold running.** A hold
  advances on frames rather than on touch events, so one still down when the
  controller went quiet kept counting; the arm was refused while touch stayed
  dead, but a controller that answered again before the two seconds were up
  let it through -- arming with nobody having touched anything since. Touch
  going dead counts as a stop now, once, on the edge, so every screen
  abandons its gesture and an arm made before it is dropped.
- **An arm queued before a stop could still arm the bench.** The command
  queue and the stop count cross between the two tasks independently, so an
  arm could be queued from a gesture the sender had watched and a stop be
  applied before that arm was drained -- and the arm then cleared the latch
  the stop had set. Each arm carries the count of stops its sender had seen,
  and one whose count is no longer current is dropped.
- **Arming from the servo screen could energise a slot the panel had never
  written.** After a panel restart the coprocessor still holds its slots, so
  slot 0 and the command in it survive while the panel remembers nothing; the
  screen's DISARM and RELEASE asked for the clear unconditionally but its ARM
  did not, and the arm rendered the old position. It asks first now, and the
  arm waits for the clear.
- **An unpayable release debt refused every arm.** Leaving the servo screen
  with the link down leaves a release owed that nothing can pay, and the arm
  gate refused every arm until a coprocessor answered -- including arming the
  panel's own bank in simulation, where no far-end output exists. The gate
  applies only while there is a link, and the far end is not told to arm while
  a slot is still owed a release instead.
- **Arming left the servo screen showing a position it had discarded.** An
  arm drops the held command and the slot so it starts from nothing, and the
  screen went on believing it was driving -- so the next change of type, trim
  or travel said that discarded position again, onto a bench that was armed
  by then.
- **Binding the servo's pin before its position arrived drove the old one.**
  The three writes a servo command makes are three transactions and the far
  end steps its outputs between them, so binding the slot second rendered
  whatever channel 0 was holding -- the position from before the last
  release, or mid-travel once that had gone stale -- and kept driving it if
  the position write then failed. The slot is bound last, so the pin is
  either unbound or already carrying what was asked for.
- **A servo pulse could be sent against a configuration that never landed.**
  Only the last of the three writes a servo command makes was checked, so a
  CHAN_CFG that was refused or timed out left the far end clamping against
  the endpoints it had before -- a narrow servo selected against a standard
  configuration renders 1500 us, past its 860 us maximum. All three are
  required now, and nothing is sent when the configuration has not arrived.
- **A cancelled arming gesture could still deliver its arm.** The hold can
  complete in the same frame the stop arrives, leaving the command waiting to
  be read; the screens were asked about the stop after their commands had
  been forwarded, so the arm went out a frame later and cleared the latch.
  The stop edge is handled before anything the screens produced, and
  cancelling a hold takes its unread command with it -- including after the
  finger has lifted, which clears the gesture but not what it produced.
- **A second stop during a new arming hold did nothing.** The latch is a
  level and says only that a stop is in force, so a STOP pressed while one
  was already latched -- during a hold begun after the first, which is how a
  stop is cleared -- left it exactly as it was, and the screens watching that
  level saw no change. The hold ran to completion and cleared the latch.
  Stops are counted now, in the policy where the latch is set, and every one
  of them cancels both screens' holds.
- **A stop did not end an arming gesture already under way.** A stop can
  latch on a bench that is not armed -- a STOP press, a dead touch, the far
  end -- so nothing about the armed state changed and a hold still under a
  finger ran on, completed, and asked to arm, clearing the latch that had
  just been set. Both screens abandon a hold when a stop latches.
- **A second contact could take over an arming hold.** A finger, or a palm,
  landing on ARM while another was holding it replaced the contact the
  gesture belonged to; the first finger's release was then ignored and the
  second contact could arm the bench. The hold now stays with the contact
  that began it, on both screens.
- **A stop pressed after an arming gesture could be undone by it.** The
  control loop stepped the arming policy before it drained what the screens
  had asked for, so a STOP that latched at the top of a pass was cleared
  later in the same pass by an arm queued before it: the bench armed from a
  press made to stop it. The queue is drained first now, and
  `arming_stop()` abandons an arm rather than deferring it, so the newer
  stop stands. Both screens' ARM went through this.
- **An arm could proceed on a servo release that did not land.** A release
  that is not acknowledged keeps its debt, but the arm went ahead anyway, so
  a transient link error during the release meant arming with the slot and
  its command still installed at the far end. An outstanding release now
  refuses the arm and says so on the alert band.
- **The servo screen went on showing a hold the bench had let go of.** A
  STOP, a dead touch or a far-end disarm left the driving state set, so the
  rings kept pulsing and a change to the type, trim or travel said the
  position again -- rebuilding the command the stop had released.
- **A write to one channel kept every other channel alive.** The coprocessor
  applied a CHANNELS write by commanding all eight channels of the stored
  page, whatever the frame carried, so any periodic write stamped every
  channel's clock. The timeout that returns an uncommanded output to its rest
  is per channel precisely so one screen's traffic cannot hold another's
  output up, and applying the page defeated it. Only the registers a frame
  carries are applied now. Latent until this release, which is the first
  thing to write a channel periodically.
- **Changing the servo type, trim or travel left the old pulse on the pin.**
  The pulse is the angle put through those three, and none of them said the
  position again, so a servo held while switching from STANDARD to NARROW 760
  kept 1500 us on a servo whose maximum is 860 while the screen showed the
  new range.
- **An arm could render a servo's slot before the release reached the far
  end.** The release was posted after ARM in the same pass, and the far end
  applies ARM and stamps every channel's clock before it steps its outputs,
  so a slot still bound at that moment rendered its old command until the
  clear arrived. The slot now goes first. A clear is also kept until the far
  end acknowledges it, rather than assumed from a write that may have gone
  into a link that was dropping, and RELEASE asks for the clear whether or
  not the panel remembers binding the slot -- after a restart the far end can
  hold a slot this end has never written. The servo screen's own DISARM asks
  for the same clear; a stop from anywhere else stays conditional, so it
  cannot quietly clear a slot the OUTPUTS screen bound.
- **A stop left a servo position an arm would step back to.** Only the servo
  screen's own disarm let go of the pin; a STOP, a dead touch, a disarm from
  MOTOR & ESC and a far-end disarm did not, and the far end keeps both the
  slot and the channel command through a disarm and a failsafe. The next arm
  found the channel neither overdue nor at rest and drove the servo to where
  it had been, with nobody having touched anything. Letting go of the servo
  is now paired with returning the throttle to zero, in every path that stops
  the bench. A release that cannot be sent -- the screen left while the link
  is down -- is kept as a debt and sent when the link is back, so a slot
  cannot outlive the screen that bound it.
- **A servo other than the standard one was driven against the wrong
  endpoints.** The panel configured the channel with a fixed 1000 to 2000 us
  whatever the screen's TYPE said, so a narrow servo (660 to 860 us) had its
  whole travel clamped to one end and a wide one (800 to 2200 us) was clipped
  at both. The command now carries the endpoints of the type it was made for.

## 0.6.1 - 2026-09-07

The throttle reached no bound pin, so an ESC (electronic speed controller) on
the OUTPUTS screen initialised, armed, and held one value at every slider
position. A PWM (pulse-width modulation) ESC could only be bound as a servo,
whose channel rests at half throttle.

### Added

- **MOTOR PWM, beside SERVO PWM.** The same pulse at the same 50 Hz, bound to
  a channel whose role is throttle rather than surface. An ESC (electronic
  speed controller) on a pulse pin was previously bindable only as SERVO PWM,
  which rests its channel at mid-span: 1500 us, half throttle, whenever the
  channel stopped being commanded while armed. The two entries are one driver
  at one rate, so the OUTPUTS page alone cannot tell them apart and the panel
  reads the CHAN_CFG page with it; a binding read back without the roles is
  not shown at all rather than shown as a servo.

### Fixed

- **The throttle drove no bound pin.** The MOTOR & ESC throttle commands bank
  channel 8, which is off the OUTPUTS page by design so that a motor command
  keeps the control page's priority on the wire; a pin bound on the OUTPUTS
  screen renders channels 0 to 7, and nothing wrote those. An ESC bound to
  GP0 with DShot therefore initialised, armed, and held whatever its channel
  rested at, at every slider position. The control page now commands every
  channel the binding marks a throttle -- the DShot entries and MOTOR PWM --
  and leaves the surface channels to the screens that own them. Reported as
  #99 from a bench: the ESC initialising, ARMED lit, and an unchanging pulse
  on a scope.
- **Opening the run log stopped the heartbeat.** `log_start()` runs on the
  arming edge, on the task that drives the heartbeat and reads STOP, and it
  probed for a free file name one card transaction at a time: a card holding
  400 runs cost 400 opens with nothing pumped in between, against a
  HEARTBEAT_MAX_GAP_MS ceiling of 150 ms. The pump now runs between probes and
  the scan starts where the last one finished. The retry was the worse half --
  the arming edge was read from the file handle, so a card that is full or
  unwritable made every pass look like a fresh arm and rescan once per
  CONTROL_PERIOD_MS for as long as the bench stayed armed. Whether a run is
  open is its own flag now, and a run that is not being recorded says so on
  the alert band rather than only on a console that is not reachable on every
  board.
- **The outputs board went grey without saying why.** The rules refusing every
  pin are right and the screen kept them to itself: a bench with one servo pin
  bound leaves seven channels free, PPM needs eight, and the whole board greys
  with nothing beside it. The reason now sits under the protocol in amber --
  the channels needed against the channels free, the slots when those run out
  first, or the protocol's own pin limit -- and only when nothing can be
  added. Reported as #99.

## 0.6.0 - 2026-09-07

Applying an output binding never worked, PPM never worked, and an arm could
carry the throttle from before the last stop. All three were on the bench the
whole time and none of them showed as an error.

### Fixed

- **Applying a binding reported NO LINK against a link that was up.** Every
  frame of a request is self-describing and the coprocessor answers each one
  it decodes, so a write of 32 registers draws eight acknowledgements of four
  at offsets 0, 4 ... 28. The panel waited for one acknowledgement covering
  the whole window, matched none of them, counted all eight as mismatches and
  waited out LINK_HOST_TIMEOUT_MS (1000 ms) -- transmitting nothing for that
  second, so the coprocessor's own LINK_DEV_SILENCE_MS (200 ms) watchdog
  latched failsafe. Both output pages are 32 registers wide, so it happened
  on every apply, and the second page of the pair was never sent: the far end
  kept a new channel configuration against the old slots. An acknowledgement
  is now taken in pieces the way a read's answer already was. Reported as #99
  and identified from a bench: LINK steady, LAST WRITE reading NO LINK, and
  `stale 8` in the panel's own counters -- one mismatch per frame.
- **PPM could be selected and never drove a pin.** The catalogue offered PPM
  at 50 Hz with eight channels, and eight channels need 8 x 2500 + 300 + 3000
  = 23,300 us of frame against the 20,000 us that 50 Hz gives.
  outputs_configure() accepted the rate, the coprocessor acknowledged the page
  and read it back unchanged, and out_ppm_bind() then refused the frame and
  left the slot unbound. The screen showed a binding the bench did not have.
  PPM runs at 40 Hz now, which gives 25,000 us and binds.
- **An arm could carry the throttle from before the last stop.** ARM and
  THROTTLE travel in one transaction, and the coprocessor's throttle is bank
  channel 8, off the CHAN_CFG page that addresses channels 0 to 7, so its
  slew is never configured and it steps straight to the command. Three disarm
  paths returned the command to zero; the far-end disarm did not, and no
  later disarm could cover for it -- arming_stop_from_far_end() clears
  a->armed itself and arming_step()'s disarm is gated on a->armed. A
  coprocessor disarm therefore left the throttle at its last position while
  the screen showed zero, and the next arm sent it. Every arm now starts from
  zero, at the arm rather than at each path that has to remember.
- **The arming hold did not end when the finger left the button**, so a press
  that started on ARM and slid onto the plot armed the bench two seconds
  later. Coming back does not resume it: the contact is finished and arming
  takes a fresh press. And a press standing while the bench was armed -- a
  press made to DISARM -- started its own two seconds the moment the bench
  disarmed, turning the operator's stopping contact into an arming one. The
  gesture now ends on the way to disarmed.
- **The link-up edge counted a sample that was never read.** While the link is
  down the question asked is the identity page, so an answer there says a
  coprocessor is present and nothing about the bench. One plot column and one
  CSV row carried the previous bench values, or zeros at boot.

### Changed

- **control_task() is 69 lines and calls sixteen named steps**, from 578. The
  order of every operation and every early exit is preserved; no loop local
  became a file static. The image differs by 12 bytes of .flash.text, all of
  it two inlining decisions: no other symbol changes size, and a control
  build with the steps forced inline reproduces the baseline's choices
  exactly. It buys readability, not coverage -- the steps are static and
  reach file statics, so the host suite still cannot link them.
- The panel's expander write records that it is single-threaded, names every
  caller, and states that a lock added later has to be bounded well under
  HEARTBEAT_MAX_GAP_MS (150 ms) -- I2C_TIMEOUT_MS is already 100 ms of it.
- Three claims corrected against the code: the protocol version on the wire
  is 2.5, the status page is polled every 500 ms, and CI holds seven
  per-screen and seven `-chrome` modes to their ceilings rather than six.

## 0.5.0 - 2026-09-06

A link that dropped never came back, and the panel could not say why. Both
are fixed, and the panel now explains a bad bus on its own screen instead of
on a console that is not always reachable.

### Fixed

- **A link that dropped stayed dropped, and nothing on the bench said so.**
  The panel holds one request at a time. `link_host_tick()` answers true for
  two different facts -- this request has been outstanding too long, and the
  link as a whole has gone quiet -- and only the first releases the
  outstanding slot. The wait in the panel ended on either, so a wait that
  ended on the second left the request pending for ever: `link_host_read()`
  then refuses every later request, returning before the wait is entered, and
  the wait held the only call to the clock that would have cleared it. The
  panel stopped transmitting, the CAN (Controller Area Network) controller
  reported a healthy bus because nothing reached the wire, and only a power
  cycle cleared it. The order that triggers it is the ordinary one:
  escalation is measured from the last reply and the request timeout from the
  request, so after a second of silence the next request wedges. Reported as
  #99, and diagnosed from a field log showing polls frozen at 1545 with zero
  timeouts and zero bus errors.

### Added

- **The CAN self-test runs at every start-up**, 1200 ms inside the splash and
  before the identity poll. A verdict other than every probe coming back
  intact puts the panel on a screen of its own instead of the menu: the
  verdict, what to check in the order that costs least to check, and what
  both ends counted. It was behind a compile-time flag and a console before,
  which an operator has neither of.
- **A link that was up and has been gone for 4 s says so on the same
  screen**, with this end's own counters -- what the CAN controller is doing,
  its transmit, receive and bus error counts, how many times the bus has been
  asked back, and how long the link has been quiet. Never while armed: the
  screen carries no STOP, and a bench with something spinning must not have
  its stop button covered by a diagnosis.
- **`RCBENCH.LOG` on the SD card**, one line per report while the link is
  down, the same fields in the same order every time. A tester can send a
  file rather than a photograph.
- The acknowledgement on both is a two-second hold, ARM's gesture and its
  fade. `UI_HOLD_S`, `ui_hold_fill()` and `ui_hold_flash()` move to
  `ui_widgets` and ARM is ported onto them, so the two cannot come to look
  different from each other.

### Changed

- **The coprocessor's CAN report counts the link requests it has answered**,
  not only self-test echoes -- which are zero in ordinary use and read as
  nothing arriving.
- **The bridged USB-C socket is on UART0, and a switch decides what it
  reaches.** A slide switch beside the BOOT and RESET buttons is marked
  `UART1` and `UART2`. In one position the bridge chip enumerates, names
  itself to the operating system and passes nothing in either direction,
  which is the symptom a broken cable gives. It is step 0 of
  [First run on hardware](docs/FirstRun.md) now. The header said UART0's pins
  were assumed and not traced; a board accepts a firmware download through
  that socket and the ROM bootloader takes one on UART0 and nowhere else, so
  they are not assumed any more.

## 0.4.2 - 2026-09-06

The panel could not get back on the CAN bus once it fell off it, and the
coprocessor's console could not say whether anything was arriving.

### Fixed

- **A link that dropped stayed dropped.** A CAN (Controller Area Network)
  transmitter that gets no acknowledgement retransmits on its own and adds 8
  to its transmit error counter each attempt; at 1 Mbit/s a frame nobody
  answers reaches the bus-off threshold of 256 in about 4 ms. The ESP32-S3's
  TWAI (Two-Wire Automotive Interface) controller does not recover on its own,
  and nothing asked it to: after that every transmit failed, the identity poll
  never got an answer, and the panel showed NO LINK with SIM until it was
  power-cycled. `can_twai_recover()` runs on the identity poll while the link
  is down and takes one step per call -- `twai_initiate_recovery()` from
  bus off, then `twai_start()` once the controller has counted its 128
  bus-free signals and stopped. Recovery takes up to three polls, about 3 s.

### Changed

- **The coprocessor's CAN report counts requests.** It carried `echoes
  served`, which counts self-test probe frames and is zero whenever the
  panel's CAN self-test is not running -- so on a bench with a link that had
  stopped it read as "nothing arrived" when it meant "no self-test was
  running". It now prints the link requests this end has answered as well:

  ```
  rcbench-iomcu: CAN up, 1000000 bit/s, 4271 requests served, 0 self-test echoes, tx_err 0 rx_err 0 eflg 0x00
  ```

  A count standing still while the panel says NO LINK means nothing is
  arriving; a count climbing while the panel says NO LINK means the answers
  are not getting back. No counter and no protocol register changed:
  `s_frames` is the one already published as `LINK_ST_FRAMES_LO`/`_HI`.
- The panel's link report, printed every 5 s while the link is down, carries
  the controller's own counters and ends the line in `-- BUS OFF` when it is,
  or says the controller is not running when TWAI never started. Zeros there
  read as a healthy bus for the one fault that stops everything.

## 0.4.1 - 2026-09-06

Two flash writes that stopped the bench, and the button that said a save had
happened when none had been asked for. Both were found on hardware.

### Fixed

- **Opening OUTPUTS dropped the link.** Selecting a protocol showed `NO LINK`
  and put the panel into SIM. The panel fetches a board's photograph once and
  writes it to flash as one erase of 256 kB and one write of 201 kB; a flash
  operation on the ESP32-S3 disables the instruction cache, so for its
  duration neither core runs code outside IRAM -- including the task that
  drives the heartbeat. The coprocessor saw the line still for longer than
  HEARTBEAT_MAX_GAP_MS (150 ms), failed safe and stopped answering, which is
  the interlock working. Every artwork flash operation now runs in chunks of
  SPI_FLASH_SEC_SIZE (4096 bytes) with 2 ms between them, so the heartbeat
  comes through between chunks. Reported as #99.
- **SAVE on the settings screen did nothing.** The values were written
  whenever the screen was left, and the only sign of it was the word `SAVED`
  under `RESET CATEGORY`, in the same column and at the same width -- which
  reads as a button that does not work. Reported as #100.

### Changed

- **Settings are written when SAVE is pressed, and not otherwise.** The press
  is a request: `settings_save_tick(safe)` takes it on the first frame at
  which the bench is disarmed and no board photograph is being fetched or
  stored, so the NVS (non-volatile storage) commit and its cache-off stall
  never land while the safety line is being driven. The label carries the
  whole state -- `SAVED` with nothing unwritten, `SAVE` with something to
  write, `WHEN IDLE` once asked and still waiting. Leaving the screen writes
  nothing; unsaved values are kept until the panel is switched off.
- The two doors on the settings screen, OUTPUTS and PICK A PIN, move up 16
  pixels: SAVE takes the space under RESET CATEGORY.

### Added

- The coprocessor measures the window its own flash write spends with
  interrupts off and prints it: `rcbench-iomcu: outputs saved, flash window
  <n> us`, and `out_store_last_window_us()` reads it back. The window is
  bounded by the flash part rather than by anything this firmware chooses,
  and it has never been measured on hardware; the number says whether it is a
  second cause of the same kind as #99. Nothing is compensated for it.
- `setup-dirty.png`, the twenty-ninth committed screenshot: the settings
  screen with a value changed and SAVE offered.

## 0.4.0 - 2026-09-05

A board the panel has never met is now usable without reflashing the panel:
it says which pins it has, where they are, which pads are grounds and rails,
and what it looks like.

### Added

- **A board describes itself over the link.** Four read-only pages, and each
  one degrades on its own rather than taking the others with it. `CATALOGUE`
  (0x24) carries which GPIOs the board brings out, the pad number printed
  beside each and what holds the ones an output may not have. `SHAPE` (0x25)
  carries the outline, the pitch and the corner pad 1 sits at, which is what
  turns a pad number into a position. `PADS` (0x28) carries the grounds and
  the rails, with each rail's voltage in tenths of a volt. `ARTWORK` (0x26)
  and `ART_DATA` (0x27) carry a photograph of the board, 62 bytes at a time.
  A coprocessor that serves none of them behaves as one built before them
  did: the panel offers nothing for that board, which is what it did before.
- **The pin picker**, behind PICK A PIN on the Setup screen. The board drawn
  with a button on every pin an output may have, each on a straight trace to
  its own pad, because at any size that fits a 480-pixel panel a pad is under
  40 px across and smaller than a fingertip. A pin the coprocessor reserves
  gets no button and a cross on the pad. A board that reports no shape is not
  drawn at all: a picture from a guessed form factor points at the wrong pad
  as confidently as the right one.
- **Photographs are fetched once and kept.** A 2 MB `boardart` partition in
  the panel's flash, eight slots of 256 kB. A transfer costs about ten
  seconds of link and happens once per board; it runs in 15 ms slices of each
  50 ms poll and the flash write runs on its own task, because the control
  task beats the safety line and its ceiling is 150 ms.
- `tools/gen_board_art.py` turns the PNG beside a board into a checked-in C
  array, the `gen_font.py` convention: no Python in any firmware build, and
  `--check` in CI so the two cannot drift.
- [First run on hardware](docs/FirstRun.md), a nine-step bench guide for the
  first time both boards are powered with the heartbeat wire fitted.
- The coprocessor build fails when the image would not fit the module fitted.
  The linker measures against the board file's 16 MB while the bring-up
  module has 4 MB, so it would accept an image that does not boot.

### Changed

- **Every protocol has its own pin set.** `outbind_t` held one protocol and
  one set of pins, so binding a second protocol meant unbinding the first.
  Choosing a protocol now says which set is being edited and trims nothing;
  a pin another protocol holds is drawn grey with the holder's name, which is
  a different fact from a reserved pin's red. Slots fill in pin order across
  every protocol, so a bench wired to one protocol writes the page it always
  wrote. Slots and channels are one budget of eight each, shared.
- Protocol minor 1 to 5. Every change is an added page; an older panel and a
  newer coprocessor, or the reverse, still bring the link up.
- `log_csv_analyse` 241 lines to 36 and `log_csv_build` 221 to 22, split into
  named pieces with behaviour unchanged.
- The artwork fetch sequence moved from the panel into
  `shared/artwork/art_fetch.c` with the link passed in, so the block order and
  every failure path are exercised by the host suite instead of by nothing.

### Fixed


- Both images reported firmware 0.0.0. The identity page has carried
  firmware major, minor and patch since the page map was written and nothing
  ever set them, so a board asked what it was running answered wrongly and
  answered confidently. `shared/link/include/rcbench_version.h` holds the
  three numbers once; the coprocessor publishes them and the panel prints
  what came back on its bring-up line, beside the protocol version. The panel
  is the host and publishes no identity of its own, so it puts its own build
  on the first splash line.
- `tools/check_docs.py` holds that header to the newest heading in this file,
  so a release cut without moving it fails the build rather than shipping a
  board that misreports itself.
- `outbind_learn_board()` wrote pins into its slot as it parsed, so a
  catalogue refused part way left half of itself over the board already
  learned while reporting failure.
- A board numbered from its right-hand corner had its pad row mirrored across
  the outline rather than reversed along itself, which moves the whole row by
  a hundredth of a millimetre when the centring remainder is odd.
- The picker repainted every frame: it cleared the other framebuffers on each
  paint, which with two buffers alternating never settles. Its chrome is a
  photograph, so that held the whole panel at 13 frames a second for as long
  as the screen was open.
- `link_artxfer_begin()` compared width times height times two against the
  byte count in 32-bit arithmetic. 65535 x 32769 x 2 wraps to 65534, a length
  that agrees with a plausible block count, so such a page would transfer,
  pass its checksum, and hand back 65534 bytes calling themselves a
  65535 x 32769 image.
- `art_store_put()` persisted an entry whose width and height did not match
  its byte count, which is what later code sizes a buffer from.
- `art_fetch_meta()` returned a byte count it had not validated, so a
  coprocessor disagreeing with itself got as far as an allocation before
  being refused.
- The `.clang-tidy` note said every header uses `#pragma once`. Forty do and
  eighteen use `#ifndef` guards.

## 0.3.0 - 2026-09-04

The coprocessor drives a pin, and remembers which one.

### Added

- Four output drivers on the coprocessor: servo PWM on the hardware PWM
  slices, PPM from a PIO program fed by a pair of DMA channels that retrigger
  each other, DShot, and bidirectional DShot with the turnaround and the
  reply capture inside the PIO block. `shared/dshot/` carries the frame, the
  group code, the checksum, the period-to-speed arithmetic and the sampler
  that resynchronises on every transition; `shared/ppm/` carries the frame
  layout. Both are host-tested. [docs/DShot.md](docs/DShot.md) says what has
  not been confirmed against an ESC.
- Bidirectional DShot is driver 4 on the OUTPUTS page, not a flag on driver
  3: it inverts the line and the checksum, so an ESC set up for one protocol
  ignores the other.
- CONTROL register 3, MOTOR_POLES. An ESC reports electrical periods and has
  no idea what it is bolted to, so the magnet count is the one number the
  wire has to carry for a mechanical speed to exist. The panel sends it from
  the `Motor poles` setting when the coprocessor answers; at zero the
  coprocessor reports no speed rather than one derived from a guess. Protocol
  version 2.1.
- The output bank refuses a pin the build has reserved. The pin in an OUTPUTS
  slot is whatever an operator typed, and the coprocessor reserves the safety
  line, the CAN controller's five pins, and every number above the last GPIO
  the part has.
- The coprocessor's capability word reports servo PWM, ESC drive and ESC
  telemetry, which the panel marks its menu from.
- An outputs screen behind Setup: a protocol list and a grid of the 26 GPIOs
  the header brings out, ticked to bind. Each ticked pin becomes one slot on
  the OUTPUTS page in pin order. Reserved pins are shown and refused, with
  what holds them under the name; `shared/outputs/out_bind.c` carries the
  catalogue, the rules and the mapping both ways, host-tested.
- The output binding lives in the coprocessor's flash, not the panel's. A
  binding describes wiring and the panel is not the board the wires are in, so
  the coprocessor restores it at boot and the panel reads it back when the
  link comes up. Restoring configures the outputs and does not drive them, and
  channel commands are not restored. The save waits for the bank to stop
  driving, because writing flash stops the core for longer than the
  heartbeat's window.

### Changed

- The coprocessor no longer models the bench. It publishes what it measures,
  with a valid bit per quantity and no SIMULATED flag; with no measurement
  front end fitted that is rpm from a bidirectional DShot ESC and nothing
  else, and the other fields are drawn empty. The panel still models the
  whole bench while nothing answers, and marks that with the watermark, so a
  modelled number and a measured one never appear on one screen.


### Known limitations

- No driver has been seen on a pin. Bit timings, the PPM DMA ring and the
  bidirectional turnaround are what a host test cannot reach.
- The coprocessor's flash store has never run: the erase window, the heartbeat
  re-acquiring across it, and the sector surviving a power cycle are all
  unmeasured.
- `Output` and `Output pin` are still in the Setup table and no longer do
  anything; the outputs screen replaced them.

## 0.2.1 - 2026-09-03

Arming is a deliberate gesture, and the first release cut from a tree whose
CI passes.

### Changed

- ARM is a two-second hold. The fill fades from the OK green to the danger
  red across the hold and the command goes when the fade completes, so
  letting go early arms nothing and the release itself arms nothing. A press
  is a gesture an elbow can make. Disarming stays a press: stopping never
  needs a hold.
- Arming flashes the whole button twice -- white, black, the danger red it
  settles on, and again, one drawn frame each, about 154 ms at 39 Hz. What
  was there decayed from white over 180 ms, which is a fade and reads as one.

### Fixed

- The release after a hold disarmed what that same press had just armed.
  `motor_screen_set_armed()` cleared the flag remembering that this press was
  the one that armed, and the application calls it between the hold
  completing and the finger lifting, so the release read as a fresh press on
  DISARM.
- `tools/frame_cost.py` failed ruff on an 81-character line, and had done
  since the per-screen modes were added.
- `tools/frame_cost.py --check-doc` failed on CI and not locally. Its
  tolerance is for machine-to-machine variation and was 1%, which was enough
  only while the row pattern skipped every hyphenated mode; the largest of
  them drifts about 275 fills of 22,700 between machines. It is 2%.
- The coverage table in `STATUS.md` did not follow `motor_screen.c`.

### Known limitations

- The coprocessor refuses to arm while it is connected: it reads the
  heartbeat on GP3 with a pull-down and the panel drives GPIO6 on J8, and
  nothing joins them. The interlock is working; the wire is not fitted.
- The control task has not run on hardware.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

## 0.2.0 - 2026-09-03

The panel runs the bench from a task of its own, and the screen from what is
left. Three defects that made the panel unusable on hardware are fixed, and a
multi-agent review of the result found twenty more.

### Added

- A control task pinned to the core that does not draw owns touch, STOP,
  arming, the outputs, the link and the heartbeat, at a fixed 5 ms. Drawing
  keeps `app_main`. A frame that costs 50 ms no longer rate-limits steering.
- `shared/safety/arming.c` holds the rules that decide whether the bench may
  be armed, as a state machine over timestamps with no driver, link or task,
  and `test_arming` holds nine cases over them. The policy was inside the
  control loop, which no test can compile.
- The Motor & ESC screen is laid out in two columns: the plot and the throttle
  on the left, the four readouts and the controls on a rail to the right. A
  strip above both carries the poll rate, the link's error count and the ESC,
  motor and panel-die temperatures.
- The rated kV, the revolutions per minute per volt the motor is turning, and
  EFF, the ratio of the two. The rated value comes from the connected ESC when
  one reports it, through `motor_screen_set_esc_kv()`, and from `SET_MOTOR_KV`
  otherwise. `docs/Screens.md` documents EFF as an estimate and says why.
- A percentage point at each end of the throttle track.
- `ui_slider_painted_rect()`, `ui_slider_release()` and
  `ui_slider_set_tap_to_set()`; `ui_router_stop_live()`;
  `gfx_text_rotated_points()`.
- `tools/frame_cost.py` gains a mode per screen, `-sim` and `-chrome`
  variants and `throttle`. CI holds the per-screen modes to 1,200 fills, the
  `-sim` modes to 2,800 and the `-chrome` modes to 45,000.

### Changed

- The throttle moves by the distance a finger travels rather than to where it
  lands: a press on the track commands nothing, so a touch at the far end
  asks for nothing, and a drag asks for its travel. Sliders that command
  nothing dangerous keep tap-to-set.
- A disarm returns the throttle to zero.
- An armed bench carries the danger red. ARM fades from green to red over
  350 ms while held and flashes once for 180 ms as the arm takes effect.
- The band's clock times the run rather than the panel's uptime.
- Each readout follows the bench page's flag for its own channel, so a
  coprocessor that measures nothing shows a gap rather than a zero.
- The panel loads its settings before `display_init()`.
- `CONFIG_LCD_RGB_ISR_IRAM_SAFE` and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` are
  off; `SET_MOTOR_KV` defaults to 0 rather than 920.

### Fixed

- The panel looped through the splash. The bounce-buffer refill reads PSRAM
  through the data cache, which a main-flash operation closes, and formatting
  an empty NVS partition did exactly that while the panel scanned. The core
  panicked with `Cache disabled but cached memory region accessed`.
- The picture sat 17 px to the left with each line's tail wrapped one line
  down. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` restarts the DMA every vertical
  blanking interval, and each restart empties the LCD FIFO and then resumes
  from a link that skips `LCD_LL_FIFO_DEPTH + 1` pixels the FIFO no longer
  holds.
- The SIMULATION watermark cost 8,412,078 instructions per frame, drawn at
  8.9 fps. Its stencil is the same set of pixels every frame; recording them
  once costs 144,721.
- The heartbeat stopped for up to 1000 ms. `beat()` shared a loop with a link
  exchange that spins to `LINK_HOST_TIMEOUT_MS`, against a 150 ms ceiling.
  `control_pump()` runs inside that wait.
- A latched stop could not be cleared while a coprocessor was attached: the
  latch suppresses the heartbeat, the coprocessor refuses to arm without one,
  and the latch cleared only after a successful arm.
- A tap on the splash latched STOP, which draws no STOP to press.
- A stop could be dropped by a full command queue, and by a test-then-clear
  that lost one arriving between the two.
- A drag whose release never arrived stayed latched, and a later contact
  applied its distance to a stale origin.
- The simulator and the log ran twenty times slower than the wall clock while
  the link was down, and a timed-out poll republished stale readings as a
  fresh plot column and a log row.
- The plot advanced at the frame rate rather than the sample rate.
- `s_bring.have_status` was never set, disabling two link diagnoses.
- `ui_router_invalidate()` reached six screens of ten.
- `tools/frame_cost.py --check-doc` skipped every hyphenated mode.
- The throttle survived a disarm and was re-applied on the next arm.
- The full-panel clears squared off the panels' chamfered corners, and the
  throttle readout's ghost and the slider thumb's shadow were derived from a
  background they no longer sit on.

### Known limitations

- The control task has not run on hardware. `firmware/panel/main/main.c` is
  not in the host suite.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

## 0.1.0 - 2026-09-02

First tagged release. Pre-release firmware: no output driver produces a
signal, and the control-page and settings-store changes have not run on
hardware.

### Added

- The panel writes ARM, THROTTLE and CLEAR to the coprocessor's control page:
  ARM and THROTTLE at every 50 ms poll, CLEAR on an explicit arm. STOP,
  DISARM and a dead touch controller write ARM = 0. The band shows the
  coprocessor's fault bits.
- The panel initialises settings from the schema and loads and saves them in
  NVS (non-volatile storage) through the new `settings_nvs` component.
- `README-de.md`, a German front page, with a language switch at the top of
  both READMEs.
- `CHANGELOG.md`.
- `tools/check_docs.py` checks the README pair, the pages under `hardware/`,
  the `Who compiles what` table against the three build files, and the
  screenshot count in `STATUS.md`.

### Changed

- The wiki pages, both READMEs, `STATUS.md`, `CONTRIBUTING.md`, `SECURITY.md`
  and the `hardware/` pages describe the system in its current state, without
  development history. Stale statements corrected: the module tree, the compile
  table, the console configuration, the coprocessor board, the Performance
  table, and the bit-timing figures for both CAN (Controller Area Network)
  controllers.
- `STATUS.md` lists the open items with what each needs, and the order of work
  with the state of each step.

### Fixed

- `test/host/test_settings.c` defined its geometry macros twice.

### Known limitations

- The control page writes and the NVS (non-volatile storage) settings store
  have not run on hardware.
- No output driver produces a signal on a pin.
