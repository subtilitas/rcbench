# Safety

<sub>**English** · [Deutsch](Safety-de.md)</sub>

How the bench stops, the external circuit the design requires, and the
behaviours that are deliberate.

The project is under development and its functions are not verified on
hardware.

## Stop mechanisms

| Mechanism | Covers | State |
| --- | --- | --- |
| Heartbeat stops | the panel is wedged, reset, browned out or unplugged; a pressed STOP | generated and monitored at both ends; the monostable it gates is not fitted |
| IOmcu silence watchdog, 200ms | the link is dead in either direction | built and tested |
| STOP command over the link | a deliberate stop, acknowledged and reported | written |
| Render-loop watchdog, 1000ms | the panel's render loop stops passing while the bench is ARMED, the supply output is on or the carrier's programming-port 5V is live | built; the decision is host-tested (`test_frame_watch`) |

A pressed STOP stops the heartbeat, switches the panel's own output model to SAFE
and writes ARM = 0 to the control page. While the link is up the panel
writes ARM and THROTTLE at every 50ms poll; an explicit switch to ARMED waits until the
IOmcu reports the heartbeat trusted, writes CLEAR (0x5AFE) on its own,
then ARM, THROTTLE and MOTOR_POLES in one frame, and a NACK (negative
acknowledge) from the IOmcu to either leaves the panel SAFE.

### Render-loop watchdog

The control task reads touch, tests STOP's rectangle and beats the heartbeat
without the render loop. A render loop that stops leaves STOP working and
nothing else. DISARM, the throttle slider, the servo dial and OUTPUT OFF are
the render loop's, and the outputs keep their last command.

The render loop moves a counter once per pass. The control task reads it in
every 5ms pass and inside every link exchange's wait. While the bench is
ARMED, while the supply output is on, or, on the rbmod carrier, while the
programming ports' 5V counts as live for the gates
([The carrier's programming-port 5V](#the-carriers-programming-port-5v)),
a counter that has not moved for
1000ms (`FRAME_WATCH_MS`, `shared/safety/include/frame_watch.h`) is a stop
of the bench's own, through the path STOP takes:

- the heartbeat drops in the same pass, and the IOmcu fails safe within
  150ms;
- the supply output switches off in the same pass;
- the alert `Screen stopped updating for 1s. Switched to SAFE, output off.`
  shows when the render loop draws again. A loop that never draws again
  shows nothing new.

The stop latches like STOP. A loop that recovers arms nothing and switches
nothing on; only the next switch to ARMED clears the latch. One stall makes
one stop, and the next stall starts at the counter's next move.

The clock starts at the counter's last move, not at the arm. A loop that
froze after it posted ARM or ON stops the bench in the first pass that finds
it ARMED or the output on. A SAFE bench with the supply off is never
stopped: the panel's update install, a log loaded on LOGS and a settings
save stall the loop on a SAFE bench. The install starts only on a SAFE
bench with the supply off and withholds the heartbeat on purpose while it
writes. At start-up the bench is SAFE with the supply off.

The timeout is 1000ms because of the longest passes the render loop makes
on a screen that drives an output:

| Call in the pass | Longest |
| --- | --- |
| PPA (Pixel-Processing Accelerator) copy for Screen rotation 180° | 50ms budget (`GFX_TURN_BUDGET_US`), then a copy of the 1,228,800-byte frame on a timeout, not measured |
| Flip: the wait for the frame-done event | 200ms (`FLIP_WAIT_MS`) |
| Paint, a full repaint included (a keypad opening, a screen change) | not measured on the ESP32-P4 |
| A command to the control task into a full queue | 5ms per command |
| The console line every 300 frames | about 11ms at 115200 baud, not measured |
| Settings save | never on an ARMED bench, with an arm waiting, or with the supply on or an ON queued (`flash_gate_open()`) |
| The servo programmer's look at the card (SERVO > PROGRAM) | one card status command or a mount, then the read of its record (`KST.TXT`); each card command is bounded by the driver's 1000ms, not measured |
| LOGS: the list, a log's open and its plot | counted as progress per directory entry and per read (`panel_card.c`); one read is bounded by the card driver, not measured |

The bounded part, 50ms + 200ms + the copy + the paint, is about 300ms.
1000ms is about three times that, and twice the 500ms after which a silent
touch controller switches the bench to SAFE.

Current limitations:

- A display that stops sending frame-done events makes every flip wait
  200ms. The loop then passes about 4 times a second, and the watchdog does
  not fire: DISARM and the other controls still act, on a picture that does
  not change.
- The servo programmer looks at the card on the render task, with the
  supply output on during a session. A status command or a mount that takes
  1000ms or more, for example behind another task's write on the same card,
  stops the bench and ends the session.
- Leaving SERVO while a programming port holds the supply keeps the output
  on. A single card read of 1000ms or more on LOGS then stops the bench and
  ends the port's session.

## Errors

The rule: an error switches every output off and the bench to SAFE. An
error is a fault that ends what the bench drives. The operator's own OFF,
DISARM and edits are not errors.

The outputs are the bank's servo PWM slots and DShot throttle channels, the
four programming ports (servo PWM, DShot, the KST line, the Hitec line), the
PD (Power Delivery) mini's output, and on the rbmod carrier the programming
ports' own 5V on GPIO 21 ([The carrier's programming-port 5V](#the-carriers-programming-port-5v)). The phase tap on the TONE page is an
input and drives nothing. The panel has no wire to any output: it acts
through the link and the heartbeat, and the IOmcu acts on its own when
either goes.

| Fault | Servo PWM and DShot, on the bank and the ports | KST and Hitec sessions | PD mini output | Who acts |
| --- | --- | --- | --- | --- |
| Link silent 200ms, the IOmcu running | no pulse from the same pass | cut, line released | asked off in the same pass, an OFF held for a write included | the IOmcu (`safety_gate_step()`); the panel latches a stop when ARMED |
| The IOmcu held in reset or BOOTSEL | pins are inputs | pins are inputs | stays as it was | nobody: [Hardware limits](#hardware-limits) |
| The IOmcu restarted | latched SAFE until CLEAR and ARM | none open | read and switched off before the rest of the start-up | the IOmcu (`supply_boot_off()`) |
| Heartbeat lost at the IOmcu | no pulse within 150ms | cut | asked off, an OFF held for a write included | the IOmcu |
| The panel restarted, wedged or unplugged | no pulse within 150ms | cut | asked off | the IOmcu, by the heartbeat |
| The panel's render loop stopped 1000ms | no pulse within 150ms more | cut | off | the panel's control task (`frame_watch.h`), then the IOmcu |
| STOP | no pulse within 150ms | cut | off | the panel, then the IOmcu |
| A run's meter or supply fails (`no INA3221 window for 0.2s` and the other errors in [Servo, What ends a run](Servo.md#what-ends-a-run)) | SAFE | none open: a run is ARMED | off | the SERVO screen |
| A session cut by the link or the heartbeat | as in the rows above | transaction cut | off, after a frame on the wire, 12ms at most | the IOmcu |
| A card write fails during a run, or the final commit or close of the run log or of the automatic test's CSV (comma-separated values) file after it, once a run | SAFE | the port's supply off, after a write that runs ([below](#a-programming-ports-supply)) | off; a port's after its write, 10000ms at most | the panel (`card_faults_follow()`), the IOmcu for the write |
| The panel's supply trip (CURRENT TRIP or VOLTAGE TRIP on SUPPLY) | SAFE | the port's supply off, after a write that runs | off; a port's after its write, 10000ms at most | the panel, the IOmcu for the write |
| An INA3221 channel over its limit | SAFE | the port's supply off, after a write that runs | off; a port's after its write, 10000ms at most | the panel, at the 200ms read of `OVER`; the IOmcu for the write |
| A run's current limit: STALL AT, constant current, 3.0A, 1.0A for 400ms | SAFE | none open: a search is ARMED | off | the SERVO screen |
| The PD mini's own protection, its input sagging, no answer, no reading, an ON refused or let go, an output or set point that would not take, an ON taken across lost touch events | SAFE | the port's supply off, after a write that runs | off where it can be switched: the module's protection and a sagging input at once, a write included; the rest after a port's write, 10000ms at most | the IOmcu's driver for the output, the panel for SAFE |
| A KST or Hitec operation ends in a fault of the servo or the wire (`kst_link_outcome_fault()`, `hitec_prog_ses_fault()`) | SAFE already: a session runs on a SAFE bench | the port's supply off; the operation has ended | off | the panel (`prog_faults_follow()`) |

SAFE means the panel asks DISARM. The request withholds the heartbeat while
it stands, so the IOmcu fails safe within 150ms even when the panel's
control task waits in an exchange. A bench that is SAFE already is not
disarmed again: on a SAFE bench the withheld heartbeat would end a
programming port's session for no output it drives. The rule is
`error_stop()` in `shared/safety/error_stop.c`, under `test_error_stop`.
The control task applies it where it finds an error through
`shared/safety/error_sites.c`, under `test_error_sites`.

The rows above are an IOmcu of link 4.31 or later. With an older IOmcu a
programming port that holds the supply keeps it through an error the panel
finds: the panel asks DISARM and sends no OFF, since that IOmcu would cut a
write to the servo's memory in the middle (`error_sites_keeps()`). That
covers every error above that the panel finds, among them a supply that
stops answering or gives no reading, an ON taken across lost touch events,
and an ON the SUPPLY page refuses while a port's ON is on it. Still off,
whoever holds the supply:

- the heartbeat or the link going: the IOmcu cuts it;
- the panel's own supply trips, CURRENT TRIP and VOLTAGE TRIP: the panel
  switches the output off first (`error_sites_cut()`);
- the PD mini's own protection and a sagging input: the module or the
  IOmcu has switched the output off already, and the panel follows
  (`error_sites_page_keeps()`). An ON the page let go is followed off the
  same way; the page lets an ON go only with no port holding it.

A port holds the supply while the SUPPLY page's FLAGS bit 11
(`LINK_SP_PORT`) says so, as the panel last read it
(`supply_link_port_holds()`). A KST or Hitec session that has a port counts
through that bit only. On an IOmcu older than 4.29 a Hitec session holds
the supply from a POWER's OFF to the end of its listen, and an error between
two POWERs switches it off. An IOmcu older than 4.13 has no such bit, and
no port holds the supply there. The panel reads the page every 100ms while
the PD mini is in use. A port that takes the supply between two reads gets
the OFF of an error in that gap. After a port lets go, the panel reads the
hold until its next answered read, and an error in that gap leaves the
output on (current limitation, IOmcu older than 4.31 only). From 4.31 the
hold changes nothing: the panel writes the OFF either way, and the IOmcu
holds it for a write that runs.

The carrier's programming-port 5V has no such exemption. Its page,
PROG_SUPPLY, exists from 4.32, and every IOmcu that serves it holds the
OFF for a write; its own port bit (`LINK_PS_PORT`) does not enter the rule.
On the carrier the SUPPLY page's bit stays clear, since the ports run from
the 5V and not from the PD mini.

A port holds the PD mini's output only. The panel's model has no port on
it, and an error switches it off. A supply that stops answering or gives
no reading with no port holding it is switched off, so that it does not
come back on by itself when it answers again. With a port keeping it, the
fault is acted on and named once while it stands, again when it turns
into the other fault, and once more when the port lets go, which switches
the output off. An ON the page refuses for a wiring the settings name and
the page refused is the port's again while the port holds the page
(`supply_link_keep()`); when the port lets go, the ON is refused and
switched off.

### A programming port's supply

A KST or Hitec session holds the PD mini's output while it has a port, on
a board without the programming ports' own 5V. On
an error the panel writes the supply's OFF to the IOmcu whoever holds it,
and whether it reads the output on or not (`supply_link_cut()`). The IOmcu
decides whether a write to the servo's memory runs, from its own state
(`supply_page.h`):

- No write runs: the OFF reaches the PD mini in the IOmcu's next pass.
- A write runs: OUTPUT reads 0 at once; the PD mini and the port's view of
  it stay on until the write ends, and the OFF reaches the PD mini in the
  pass after, 10000ms after the OFF at the latest
  (`SUPPLY_PAGE_WRITE_WAIT_MS`). The port does not cut itself in the
  middle of the write.
- A KST write is a WRITE, a RESTORE or a RELEASE of the session, from the
  gap before its first write frame to the end of the read-backs of its
  last, and 150ms after (`KST_PORT_WRITE_TAIL_MS`, `kst_port_writing()`).
  Its planning and the full reads before the first and after the last
  write frame send no write and are cut at once. An ENTER, a READ_ALL, a
  VERIFY, a power cycle and a LEAVE write nothing.
- A Hitec write runs while a SEND whose frame writes runs and for 150ms
  after it (`HITEC_PORT_WRITE_TAIL_MS`), or while the last command of the
  HITEC page started its step with `MORE` (`CMD` bit 4,
  `hitec_port_writing()`). The IOmcu tells a writing frame from its bytes:
  a classic WRITE_EEPROM or WRITE_RAM, a D series write request, the save
  and the restart included, with or without `MORE`. A read and a LISTEN
  write nothing, and an OFF during them is not held. How long a servo
  needs its supply after a writing frame is not measured; the 150ms are
  above the longest gap a vendor programmer leaves after one, 128ms
  ([An OFF during a write](LinkHitec.md#an-off-during-a-write)). The
  panel's programmer marks every step of a write but the last (`hitec_ses_step_t.more`): a plan step before the
  last, and a classic try that may be tried again. The bench console marks
  a step with `more` ([The bench console](BenchConsole.md)).
- The heartbeat lost or the link silent is not held: off at once, after a
  frame on the wire, 12ms at most.

From the panel's OFF until the port lets go of the supply, the IOmcu
refuses the port's own ON: a KST power cycle in its off time ends in PWM
with result 32 (`SUPPLY`) and the supply off; a Hitec POWER switches it
off and its ON is refused (`RESULT` 3), and the port stays held without
supply until CLOSE. A KST port that finds the output off after a write
keeps it off 1000ms and returns to PWM.

The longest writes the code allows end inside 10000ms, computed and not
measured: a KST write of 33 steps is 1.93s on the wire after its planning;
a classic Hitec write of 56 steps with its read-back of 36 cells is 2.9s
and a D series write of 21 registers with its save, its 1000ms reboot wait
and its read-back 3.5s, at 32ms a step on the panel's host loopback.

A session fault is an operation that ends in what the servo or the wire
did. KST: no servo, a register without 3 equal reads, a servo that changed
under the plan, a write that failed, was rolled back, was torn or changed
another register, a verify that differs, a driver fault, a supply that did
not switch. Hitec: no servo, a wrong entry answer, a failed read, a classic
write without its answer, a read-back that differs, a step the decoder
refused. Not a fault: a refusal, a plan that sends nothing, ABORT, and a
step cut by a stop or a lost link, which end the session themselves.

#### The carrier's programming-port 5V

On the [rbmod carrier](Carrier.md#power) the programming ports run from
the 5V buck M7, switched by GPIO 21 (link 4.32, the PROG_SUPPLY page). The
PD mini feeds the servo rails only. The KST and Hitec programmers switch
the 5V on and off for a session, and the PROG 5V box on SERVO and SUPPLY
switches it by hand ([below](#the-prog-5v-box)). The
programmers take no reading of the PD mini on the carrier: its set point
neither refuses a CONNECT under 4.8V nor sets the voltage of a power
cycle, and the SUPPLY screen's limits bound the servo rails only, not a
CONNECT. With the 5V read on, CONNECT and LEAVE after a write switch it
off and on, as they do the PD mini's output; with it off, CONNECT powers
the servo up. The page follows the SUPPLY page's rules for a port:

- An error writes the page's OFF, whoever holds it and whatever the panel
  reads of it (`error_sites.c`, `prog_supply_link_cut()`). A write to the
  servo's memory keeps it on until the write ends, 10000ms at most; from
  the OFF until the port lets go, the port's own ON is refused.
- The heartbeat lost or the link silent switches it off at once, after a
  frame on the wire, 12ms at most.
- It is off at every start of the IOmcu, driven low from the first
  statement of `main()`; the pull-down on M7's EN holds it off while the
  IOmcu does not drive GPIO 21.
- An ON from the panel is refused with NOT_ARMED without a live heartbeat
  and with BAD_VALUE while a port holds the supply.
- A port switches it on only after the panel has written the page's
  register 4 (PANEL) since the heartbeat last went. A panel before link
  4.32, which knows neither this supply nor its gates, never writes it, so
  its programmers never get the 5V.
- A firmware install, an IOmcu update, a write to the panel's flash and
  the panel's Wi-Fi wait while the 5V is on, not read since the
  link came up or since an unanswered read, or held by a programmer's
  port, as they wait for the PD mini's output
  (`prog_supply_link_live()`).

##### The PROG 5V box

The PROG 5V box on SERVO and SUPPLY switches the 5V by hand: PROG 5V ON is
a 2s hold, PROG 5V OFF a tap. The panel judges the ON as it judges the PD
mini's OUTPUT ON:

- It is taken with the bench SAFE or ARMED; no switch to ARMED is asked.
- It is dropped when a STOP, an OFF or a touch loss came after the hold
  completed, and while a port holds the 5V.
- It is written once as OUTPUT 1 and not repeated. The IOmcu refuses it
  without a live heartbeat (NOT_ARMED) and while a port holds the 5V
  (BAD_VALUE); each, and no answer, gets an alert
  ([Carrier, Power](Carrier.md#power)).
- An ON taken across lost touch events is an error: SAFE, the 5V off.
- An error, PROG 5V OFF and leaving the screen whose box switched it on
  write OFF, the last also while the ON is still on its way. An error
  drops an ON not yet written. The OFF is a flag beside its queue entry,
  as the PD mini's OUTPUT OFF is, so a full queue does not drop it.
- While a port holds the 5V the box takes no press.

SUPPLY shows its state under OUTPUT: `PROG PORTS 5V, GPIO 21: ON`, `OFF`,
or `NOT READ` when the last read went unanswered. The core is
`shared/outputs/prog_supply.c` under `test_prog_supply`; the panel's end
is `shared/bench/prog_supply_link.c` under `test_prog_supply_link`, the
box `shared/ui/prog5v_box.c` under `test_prog5v_box`.

Current limitations:

- An error during a write keeps the servo powered until the write ends,
  10000ms at most. A write still running then, with retries, on a slow
  link or typed step by step on the bench console, is cut in the middle.
- Every OFF the panel writes is held for a write the same way: the
  operator's OFF on SUPPLY and the panel's own trips (CURRENT TRIP,
  VOLTAGE TRIP) as well as an error's. The PD mini's own protection and
  the IOmcu's cut for a sagging input act at once, a write included.
- A Hitec write that ends in a fault after a step with `MORE` keeps the
  OFF held until the next command of the HITEC page or 10000ms.
- A session fault ends the session with the supply. After a KST write that
  failed, the session's own backup ends with it; the record made before
  the write stands, and the read after CONNECT compares the servo with it
  ([An interrupted write](ServoProgrammer.md#an-interrupted-write)).
- The IOmcu's STATUS fault bits for overcurrent, over-temperature and stall
  (`LINK_FAULT_OVERCURRENT`, `LINK_FAULT_OVERTEMP`, `LINK_FAULT_STALL`) are
  defined and set by nothing.
- The alerts name the error and do not say that the bench switched to SAFE;
  the band shows SAFE.
- With an IOmcu older than 4.31, a supply a port keeps through a supply
  that stopped answering or gives no reading stays on until the port lets
  go. The panel has no valid reading of it meanwhile, so CURRENT TRIP and
  VOLTAGE TRIP do not act on it; the PD mini's own protection and the
  IOmcu's cut for a sagging input do.

### Hardware limits

Firmware cannot cover a board that does not run:

- The IOmcu held in reset or in BOOTSEL. Its pins are inputs, so the bank
  and the ports send no pulse, but a servo or ESC (electronic speed
  controller) input that floats can read a level. The PD mini has its UART
  on the IOmcu alone and keeps its output as it was until the IOmcu runs
  again. The monostable below does not cover it: the panel goes on
  beating. A pull-down on each signal line covers the pulses; a second
  monostable that the IOmcu's own main loop retriggers, gating the same
  switches and a switch after the PD mini, would cover the rest. Neither is
  specified.
- The panel and the IOmcu both gone. Nothing switches the PD mini or the
  power path. Covered by the monostable.
- The PD mini not answering on its UART. The IOmcu sends OFF blind only to
  a module that has answered with a known OFF argument. The monostable
  does not cover it while both boards run. A load switch after the PD
  mini's output, open unless an IOmcu pin holds it closed, would; it is not
  specified.

On the bring-up bench the PD mini's output stayed on through a 15s outage
of the IOmcu in BOOTSEL, and a power-on reset drops the IOmcu's USB for
14.5 to 15s. How long a real reset leaves the PD mini on before the start-up's OFF
is not measured.

## Required external circuit: the monostable

The safety line is panel GPIO28 (general-purpose input/output pin 28), a free
pin of the 7-inch ESP32-P4 display board: IO28 of the 12-pin header P3
(`PANEL_HEARTBEAT_PIN` in `board_pins.h`). It carries edges, not a level. The
IOmcu's output enable and the servo and ESC (electronic speed controller)
power path must be gated by a retriggerable monostable that stays energised
only while edges keep arriving. A crash, a wedged task, a reset, a brown-out
and an unplugged cable then all produce the same result: no edges, no output,
independent of firmware at both ends.

Monostable window: about 150ms. The heartbeat comes from the panel's control
task, which runs every 5ms on the core that does not draw, so its period does
not depend on what a frame costs. The window stays at 150ms, inside the
IOmcu's 200ms link failsafe, rather than tightening to the new period:
the margin is what survives a task that is late, and nothing is gained by
removing it.

The monostable is on no board. On the bring-up bench the edges reach the
IOmcu's GPIO 3 by a direct wire from the panel's heartbeat pin, which is what lets that bench switch to ARMED.

The monostable is retriggered by the panel's edges and removes the outputs
when they stop. What it adds over the direct wire is that it does this without
the IOmcu's firmware doing anything. Three cases, and the middle one is
the gap:

| | Direct wire and firmware | With the monostable |
|---|---|---|
| The panel stops beating, the IOmcu is healthy | switches to SAFE after HEARTBEAT_MAX_GAP_MS | the same, and sooner or later by its own timing |
| The panel stops beating and the IOmcu cannot act (wedged, or not servicing its monitor) | nothing switches to SAFE; the bank goes on driving | the outputs are removed anyway |
| The panel is healthy and the IOmcu misbehaves | the panel's STOP and the link watchdog are what is left | **no help**: the panel is still beating, so the monostable stays energised |

The third row is not a case the monostable is for, and no hardware in this
design covers it. Until the part is fitted, the second row is uncovered too,
and firmware at both ends is the whole of the interlock.

## Heartbeat monitor

The IOmcu also checks the heartbeat in firmware, because a monostable
cannot distinguish a heartbeat from noise. Constants from
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Value | |
| --- | ---: | --- |
| Panel edge interval | 20ms | `HEARTBEAT_PERIOD_MS`, one edge every 20ms, 4 control-task periods of 5ms, not one per frame |
| Accepted interval | 4–150ms | shorter is noise; longer means the loop that owns STOP has stopped |
| Good intervals before the line is trusted | 4 | 80ms at the panel's rate |

The check is asymmetric: four good intervals before the line is trusted, one
bad interval or one silent window to distrust it. The IOmcu refuses to
switch to ARMED while the line is not trusted, and switches its outputs to SAFE and sets its arm
latch when it stops.

The heartbeat is generated by the control task, which owns STOP and runs
every 5ms on core 1, the core that does not draw. It is not generated by a
timer or a peripheral, and not by the loop that draws: a panel that has
stopped drawing can still be stopped. The touch controller is read by a task
of its own, priority 5 on core 0, the core that draws. The control task takes
that task's events from a queue and judges the controller by the time since
it last answered: 500ms without an answer switches the bench to SAFE and stops the
heartbeat. The IOmcu's input is pulled down, so an unpowered or
unplugged panel reads as a line that is not edging.

## Arm latch

The IOmcu holds an arm latch. While it is set, a write of ARM is
refused with NOT_ARMED and the bank stays SAFE whatever the ARM register
holds.

| Sets the latch | Condition |
| --- | --- |
| An IOmcu start | power-on, a reset, the RUN pin, a reflash |
| Link silence | 200ms without a request, together with the link failsafe |
| The heartbeat is distrusted | one interval under 4ms or over 150ms, or 150ms without an edge |
| A refused ARM | an ARM answered NOT_ARMED, so a CLEAR followed by a refused ARM leaves the latch set |

One thing clears it: a write of 0x5AFE to CLEAR on the control page. The
panel writes CLEAR in one place, the switch to ARMED an operator completes by holding
ARM for 2s. The ARM the panel writes at every 50ms poll while the
bench is ARMED carries no CLEAR, so it never brings a latched IOmcu back.

The latch is separate from the link failsafe. The supply's ON and the STATUS
page read the failsafe and the heartbeat and not the latch: after a start
STATUS reads idle with no fault bit, and a supply ON is taken before the
first switch to ARMED. The latch is not on STATUS. A latched IOmcu shows as the
refused ARM, which the panel reports as `IOmcu switched to SAFE. Hold ARM again.`.

The panel has its own rule for the same events. When the IOmcu stops
answering while the bench is ARMED, or while a switch to ARMED is waiting for the line,
the panel latches a stop on that poll: its own bank is switched to SAFE, the throttle
goes to zero, a held servo position is let go and the heartbeat stops. A
missing answer is noticed after the 1000ms exchange timeout, or at once
when the panel's CAN (Controller Area Network) controller is bus-off. Every
exchange counts, not only the poll's read of the bench page: the write of
ARM and THROTTLE that follows it, a page service, a servo refresh, the
status read. One that ends unanswered takes the link down on the control
task's next pass, and until then the panel sends nothing more on it. While
the bench is ARMED or a switch to ARMED is waiting, the stop is latched as that
exchange ends, without waiting for the pass. A bank
in ARMED with no IOmcu connected, the simulated bench, is stopped the
same way on the poll where an IOmcu first answers, before any ARM is
written to it.

### An IOmcu restart during a run

The IOmcu restores its output binding from flash and starts latched,
with no command. The panel's bank is still ARMED for up to 1000ms. Two
cases:

- The panel notices the outage. It latches a stop and writes ARM = 0 from
  then on.
- The panel does not notice it, because the request was retransmitted until
  the restarted IOmcu answered. Its next write of ARM is refused
  with NOT_ARMED, and the panel latches a stop on that answer.

In both the outputs stay off until an operator holds ARM. Host-tested in
`test_safety_gate` for outages of 5, 30, 50, 60, 300, 1000 and 5000ms at 55
reset phases each and three models of the wire. How long
the IOmcu takes from reset to its first answered frame is not measured.

The PD mini's output is switched off first. Right after the wiring is
restored, and before the sensor bus, the store's erases, core 1 and the CAN
controller, the IOmcu reads the module and writes OFF to an output that
reads on (`supply_boot_off()`). WHO_AM_I goes again as soon as one fails
(`pdmini_hurry()`), 415ms a try, so under AUTO all seven rates are tried in
2.9s. The wait ends when the module reads off or after 3500ms
(`SUPPLY_PAGE_BOOT_OFF_MS`); a module that does not answer delays the link
by that much at every start. Host-tested in `test_supply_boot`: a module
left on at a set rate is off within 100ms of the wait's start, and at
each AUTO rate within 3500ms.

### The first switch to ARMED after a stop

The heartbeat is withheld while a stop is latched. A switch to ARMED clears the panel's
latch, the heartbeat resumes, and the IOmcu's monitor needs five edges
before it trusts the line: 100 to 125ms at the control task's 5ms period.
The panel waits 100ms, then reads the STATUS fault register once per pass
until the heartbeat bit reads clear, and only then writes CLEAR and the frame
that switches to ARMED. If the bit has not cleared 300ms after the hold completed, the
panel gives the switch up with `The IOmcu refused to switch to ARMED.` and writes nothing,
so the IOmcu's latch stays set. An answer that comes back later than
those 300ms is not used, whatever it says.

The same 300ms hold through the writes that follow. The panel asks the
deadline again before CLEAR and before the frame that switches to ARMED, because the
servo release ahead of them and the CLEAR itself can be answered late. Past
the deadline before CLEAR, the switch is given up as above with nothing
written. Past it after an acknowledged CLEAR, no frame is written and the
panel latches a stop: the CLEAR released the IOmcu's latch and no
register sets it again, so the stop withholds the heartbeat and the
IOmcu sets its latch when it stops trusting the line, within 150ms.
The limit of the bound is the frame that switches to ARMED itself. It is written at or
before the deadline, and its acknowledgement can arrive up to the 1000ms
exchange timeout (`LINK_HOST_TIMEOUT_MS`) later; the IOmcu has then
switched the role it was asked for to ARMED, at rest: throttle 0 for MOTOR & ESC and
its stick runs, surfaces centred for SERVO. The panel does not
undo it. An exchange of the switch that nobody
answers (the STATUS read, the servo release, CLEAR or the frame that switches to ARMED)
has waited the 1000ms exchange timeout or did not reach the wire: that is
the link going quiet under a switch to ARMED, and the panel latches a stop as above. A
refusal is an answer and leaves the panel SAFE with no stop. With no
IOmcu connected the panel switches its own bank to ARMED after the 100ms.

### A pulse range on its way

The frame that switches to ARMED carries ARM, THROTTLE and the pole count,
and no CHAN_CFG. The throttle's Idle pulse and Full pulse reach the IOmcu on
their own path: an edit, a binding written on OUTPUTS or PICK A PIN, a
link-up edge or the panel's start owes the IOmcu the pair. An IOmcu restart
or update comes back as a link-up edge. The panel pays the debt at a 50ms
poll whose write of ARM = 0 was acknowledged, once 300ms
(`ENDPOINTS_SETTLE_MS`) have passed since the last edit. It reads CHAN_CFG,
puts the pair on the throttle channels and writes the page when it differs.
The IOmcu applies a CHAN_CFG write to its outputs before it acknowledges
it, and the acknowledgement carries what it stored.

| Payment | The debt | A switch to ARMED with the link up |
| --- | --- | --- |
| the page already holds the pair | paid, nothing written | goes on |
| the write is acknowledged and every throttle channel stored the pair | paid | goes on |
| the read or the write is not answered | owed, sent again at the next poll | `Pulse range not yet sent to the IOmcu. Hold ARM again.` |
| the read is refused | ended, `The IOmcu did not report its pulse range.` | `No pulse range from the IOmcu. Update the IOmcu firmware, then hold ARM again.` |
| Idle pulse is less than 100us below Full pulse | ended, nothing sent, `Idle pulse must be 100us or more below Full pulse. Not sent.` | `Idle pulse is not 100us or more below Full pulse. Change one, then hold ARM again.` |
| the write is refused | ended, `The IOmcu rejected the pulse range.` | `The IOmcu rejected the pulse range. Change Idle pulse, Full pulse or OUTPUTS.` |
| the write is acknowledged and a throttle channel stored another pair | ended | `The IOmcu holds another pulse range. Change Idle pulse or Full pulse.` |

The alert of a refused or an inverted pair leaves the band when a later
payment finds the page holding the pair asked, or a write of it is
acknowledged, before its 30s are up. The same holds for `The IOmcu
rejected the pole count.`: it leaves the band when a later pole count is
taken.

The panel asks before CLEAR, after the servo release and the frame rate,
and writes nothing, CLEAR included; the bench stays SAFE with the outputs at
rest. It asks again after an acknowledged CLEAR, for an edit that arrived
while CLEAR was on the wire, and then writes no frame that switches to
ARMED. It asks a third time once the frame that switches to ARMED is
acknowledged, for an edit that arrived while that frame was on the wire,
and then writes ARM = 0 in the next exchange: the IOmcu is ARMED at rest
on the range it held for that one exchange (time not measured), and the
panel's bank does not switch to ARMED. The panel's own screens cannot
publish an edit there: the two SETTINGS rows lock while ARM is held and
while an ARM waits for its answer. SETUP's DISCARD, which can revert an
unsaved value, is on another screen and takes no tap while the bench is
ARMED, and leaving MOTOR & ESC or SERVO switches the bench to SAFE.

The debt is paid on the control task, the task that writes the switch, so
the switch never runs while a payment is on the wire. A refused switch is
not tried again by itself: ARM is held again. STOP is not affected. A
refusal or another pair stored is not sent again by itself either: the
same request refused once is refused every time. The block ends at the
first payment that finds the pair in force after an edit, a binding or a
link-up, and not before; a payment that is refused again blocks again with
its reason.

An operator with an IOmcu that refuses the page reads `The IOmcu did not
report its pulse range.` at the link-up, and `No pulse range from the
IOmcu. Update the IOmcu firmware, then hold ARM again.` at every hold of
ARM. The bench does not switch to ARMED with that IOmcu connected. The
panel offers the IOmcu image it carries at the link-up, after its own 2s
hold; the IOmcu restarts after the update, and the link-up that follows
owes the pair again. Without an IOmcu, the panel's own bank switches to
ARMED as before.

An edit at time 0 is paid at the first poll at or after 300ms: a poll at
299ms leaves it owed, a poll at 300ms pays it. The time is taken modulo
2^32ms, so the rest runs across the wrap of the millisecond clock. The
SETTINGS rows for the two values lock while ARM is held, and the hold takes
2s, so an edit made before the press is normally paid inside the hold.

With no link the panel's own bank switches to ARMED and nothing is written.
A link that comes back owes the pair again and holds the IOmcu SAFE until
an answer, so a refusal from before the link-up lasts until the first
poll after it, and then only if that poll is refused again. Host-tested in
`test_endpoints_link` and `test_arming`. The panel's control task, which
reads the state and shows the alert, is not in the coverage figure.

## Deliberate behaviours

- STOP latches. The bench stays SAFE until it is switched to ARMED again.
- STOP closes the OUTPUT IS ON question on SUPPLY and SERVO. Each screen
  counts the stops it is told of, and a question stands only under the count
  it was asked with, so an APPLY tapped in the same frame as STOP changes no
  set point, though the supply reports its output off a sample later.
- The PD mini's wiring (SETUP INTERFACES) changes, once the module has
  answered, only on a state read sent after the change that shows the output
  off. A module that switched itself on in the 500ms between two reads keeps
  its pins and so its OFF path; the IOmcu switches it off.
- A live PD mini output whose input reads under the set point plus 0.5V on
  2 input reads in a row is switched off by the IOmcu, which tells the
  panel afterwards. A conservative rule chosen without a measurement: it may
  cut a run that would have survived. Set points are capped 0.7V under the
  input voltage, 0.2V above that line, so an input voltage dropping less
  than 0.2V under load cuts nothing. An ON asked while the input voltage
  reads under 1.5V is not written.
- Switching to ARMED is a two-second hold on ARM, and the command goes when the hold
  completes rather than when the finger lifts. Switching to SAFE (DISARM) is a press.
- The panel writes its own flash only while the bench is SAFE, with no
  switch to ARMED waiting, and with the supply output off and no ON queued. Flash
  auto-suspend is off, so each erase or write stalls both of the panel's
  cores, the control task with its heartbeat, STOP, OUTPUT OFF and the
  supply trips included. The rule is `flash_gate_open()` in
  `shared/safety/flash_gate.c`, under `test_flash_gate`. It holds back the
  settings save and the board drawing's keep. Switching to ARMED and the supply are
  never refused for a write: the write waits. The keep (2 erases of 4kB and
  2 writes for the RP2350-CAN's 1,560-byte drawing) runs one sector per 5ms
  control pass and stops between two sectors when the bench is ARMED or the
  supply comes on. It resumes from the same sector on the first safe pass.
  Its header is erased first and written last, so a keep that is paused or
  cut by a power loss leaves no drawing findable for the board, and the next
  link-up fetches it again. The duration of
  one erase on the panel's flash is not measured.
- A stick run on MOTOR & ESC > PROGRAM switches to ARMED through the same policy, after its warning
  (PROPELLER REMOVED? MOTOR SECURED?) is held for 2s. It then sets the throttle
  to the profile's entry position before it switches the supply on, which
  for most ESCs is the stick at MAX: that is how their menu is entered. The
  ESC then sounds its programming menu and does not drive, but in rare
  cases a motor may start and run, so the warning asks for a resistor load
  in place of the motor, or a motor mounted solid with no propeller. The
  hold's ARM waits one frame in
  the screen; a STOP or a lost touch event in that frame takes it back and
  ends the run, so it cannot clear the stop. STOP, a switch to SAFE, a lost link,
  ABORT and leaving the screen end the run with the throttle at minimum, the
  supply off and the bench SAFE, in one step. A run that ends as planned
  switches the supply off first and moves the stick once the supply itself
  reports the output off. [Stick programming](StickProgramming.md).
- A hold is credited at most 250ms per frame, so it spans at least eight
  frames with the press standing. A frame's duration is measured at its top
  and applied at its end, and without the cap one late frame credits a hold
  that began while that same frame was dispatching its touch events. Both
  the ARM hold and the bus-fault acknowledgement take the cap.
- A frame that lost touch events cancels the gesture in progress. A full
  touch queue drops its oldest entry to take the newest, and no choice there
  is safe on its own: a release that never arrives leaves a screen holding a
  press, a press that never arrives orphans the release after it, and the
  movement where a finger leaves a button is what abandons the hold. The
  render task drains the queue from the other core, so inspecting an entry
  does not decide which one is removed. Instead each queue's one producer
  numbers every event it offers, whether or not the queue keeps it, and the
  consumer finds a loss as a gap in the numbers: on the first event after it,
  before that event is handled, or, at the end of a drain, against the number
  the producer published before the drain began. No count raised on one core
  has to be read in time on the other. The render task then tells every
  screen that its record of the glass is stale, not only the one on top,
  because an event that survived the loss can have navigated away from the
  screen holding the press; each screen drops any gesture in progress. That
  asks for nothing, exactly as letting go early does, with the one exception
  below. Both queues are numbered: the driver's own event queue evicts its
  oldest for the same reason, and a gap the control task finds there travels
  to the render task as an item of its own, ahead of the events that followed
  it. The render task drains once more right before the frame's tick, so an
  event lost while the loop is running is answered in that frame rather than
  the next. The frame log carries the events found missing in each queue as
  `TOUCHLOST <panel>/<driver>`.
- Every control that holds state between a press and its release cancels.
  MOTOR & ESC, SERVO and CAN BUS FAULT have a gesture that completes on a
  timer, so a lost release there switches to ARMED or acknowledges on its own; the
  overview's tiles, the outputs and picker screens' cells, the settings
  screen's keys, the log viewer's buttons, rows and DELETE question and the
  tab rows of MOTOR & ESC, RECEIVER and BALANCE act on the release instead,
  and a press left latched owns a track id the controller reuses, so a later
  contact that began elsewhere is taken for the missing release. The DELETE
  question stays open through a cancel and takes the next fresh press. HOME
  and STOP are the router's own gesture and it cancels those itself.
- A contact has one owner from its press to its release, and the router
  decides it. A screen is handed a move or a release only for a contact whose
  press it was handed. A contact that comes down on the status band (the top
  48px) or on the alert band is the router's until it lifts, wherever it
  travels. The exception is a press on a control that stops what the bench
  drives (DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST, STOP RUN),
  which the screen names to the router (`safe_at` in ui_screen.h): under the
  alert band or with a chip's help box open it is the screen's, and the band
  or the box closes. ARM is never named, so a press on ARM under either
  starts no hold. A contact that was down when the screen changed or when touch
  events were lost is nobody's until it lifts and comes down again. A contact
  the screen owns that reaches the status band is released for the screen at
  the last point the screen was handed, and is the router's from there: a
  slider's or the dial's drag ends with the value from that point, a hold is
  abandoned, and the distance travelled on the band is not applied. Without
  that release the drag stays latched to its track id, and a later contact
  with the same id that crosses from the band into the body moves the value
  by its own distance from the first drag's origin. The release carries a
  mark (`TOUCH_FLAG_NO_TAP`), and so does the release the tracker makes for a
  contact that is more than 120px from where the previous report had it.
  A marked release ends a press or a drag and activates no control: the
  finger did not lift there. Two controls take it all the same, because
  they stop something: OUTPUT OFF, and START TEST while a servo test runs.
  STOP and DISARM on MOTOR & ESC and on SERVO have acted on the press
  already, and their release does nothing. STOP and the home tag
  answer to a press that lands on them: a contact that slides onto STOP from
  the body presses nothing. A press the router keeps from the screen (on
  the band, STOP, the home tag, a chip or the alert band) releases the
  contacts the screen owns in the same way, at their last point in the body,
  marked: a dial or slider drag under way ends at that press, and nothing
  it drove is commanded after it. The table holds 5 contacts, the number the GT911
  reports; a sixth is handed to no screen.
- A control that changes a setting acts on a tap, not on the press. On SETUP
  a `-` or `+` key steps when the finger that pressed it lifts inside it, no
  more than 8px from the press in x and in y; the list scrolls from
  anywhere, the keys included, and a scroll changes no value. A key held
  steps from 0.45s after the press. ESC STICK's controls follow the same
  rule, DEFAULTS and the stick timing's keys included. ABORT of a stick run
  acts on the press, and so does every stop on the bench screens, STOP and
  DISARM included: stopping never waits for a lift that may not come. A
  press on DISARM takes no hold, so a finger left on the button arms nothing
  when it reads ARM again; ARM stays a 2s hold.
- The screens do not rely on the router for it. Every press ends a drag on a
  slider or on the SERVO dial that is still latched, wherever the press
  lands, and leaving SERVO ends a dial drag. SUPPLY holds one press at a
  time; a press with the track id of the one it holds says that contact has
  gone, and the held press is dropped as a touch loss drops it. Not measured
  on a panel: whether the GT911 gives a lone finger the same track id on
  successive touches.
- The control task tests STOP's rectangle on every press itself,
  independently of the screens and of the frame, and stops the bench at the
  press: the heartbeat drops in the same pass and the IOmcu fails safe within
  150ms. A press that lands on STOP and is carried off it before lifting
  stops the bench all the same. A touch stream that breaks while STOP is held
  asks nothing more: the press has stopped the bench.
- A press on DISARM switches to SAFE at the press, so a release lost to a
  full queue loses nothing. A cancel while ARMED with a press of ARM still
  down whose hold did not fire switches to SAFE as well: the button under
  that finger reads DISARM. ARM has already sent its command by the time the finger
  lifts, so an ARM cancelled part way asks for nothing, which is correct. A
  cancel also drops an ARM the screen has posted and the application has not
  yet collected. An ARM already handed to the control task carries how many
  times the render task had dropped gestures and the number of the last
  event it had taken. The control task drains the driver's queue first, then
  drops the ARM when the render task has dropped gestures since, or has a
  loss notice it had not yet taken. Once the ARM is applied, any loss (the
  render task dropping gestures, or a gap in the driver's stream) undoes it
  until the render task acknowledges the ARMED bench, which a frame does
  only when it began ARMED and found the stream whole. The control task
  looks inside every link exchange's wait, not only once a pass, and stops
  the bench from there as it does for STOP: the heartbeat drops at once and
  the IOmcu fails safe within 150ms, however long the exchange
  backlog behind the ARM. A frame that
  began before the ARM was published cancels screens that still believe the
  bench is SAFE, which posts no DISARM, so the control task has to.
  After the acknowledgement a loss is seen by the screens while the bench is
  ARMED. Nothing switches to ARMED across a loss. A posted DISARM stays.
- The throttle moves by how far a finger travels, not to where it lands. A
  press on the track commands nothing, so a touch at the far end asks for
  nothing; a drag across the whole track asks for the whole span, and one
  completed inside a 50ms poll is a 0 to 100% step at the pin. Sliders that
  command nothing dangerous, such as the servo screen's sweep speed, keep
  tap-to-set.
- An output value changes only while the bench is ARMED, on MOTOR & ESC and on
  SERVO alike. While the bench is SAFE, no control on either screen
  changes the value it commands: nothing is posted, the value and its
  readout stay, the control is drawn dimmed and the screen reads
  `ARM FIRST`. A turn of the rotary knob made then is not kept. The
  refusal runs from the DISARM until the bench reports ARMED, so it covers the time
  between a completed ARM hold and the bench's answer. From a DISARM a
  screen asks for, or a STOP while the bench is ARMED, until the bench reports
  SAFE the controls are refused
  too and drawn dimmed, while the DISARM waits to be collected and after
  it. On SERVO a sweep, a pause, a held position and a run of the automatic
  test end where the DISARM or the STOP is asked, and a RELEASE in that time is sent and moves no
  value. A drag
  under way ends at an ARM, a DISARM and a stop, and a finger resting on the
  control moves nothing until it lifts and presses again. A value the screen
  has posted and the application has not collected at a DISARM or a stop is
  dropped. The one exception is a Hitec servo inside its programming
  session ([A Hitec servo moved while SAFE](#a-hitec-servo-moved-while-safe)).
- Every switch to ARMED starts from the channel's rest, not from where the last run left
  the control.
  - MOTOR & ESC: the throttle slider, its `-1` and `+1` steps and the knob
    are the controls. A switch to SAFE returns the throttle to 0%, and it stays
    there until the next switch to ARMED, so the first step after it posts 1.0%.
    The ARM hold returns the slider to 0% when it asks, and so does the
    change to ARMED for a switch the hold did not ask for, as for a stick run
    on PROGRAM.
  - SERVO: the dial, the knob, CENTRE and SWEEP are the controls. While SAFE,
    the horn and the pulse width shown stay at the position last driven. At
    a switch to ARMED the value becomes the surface's rest, which the pins drive once
    the switch has paid its release: the midpoint of the channel's endpoints,
    1500us across 1000 to 2000us and 760us across 660 to 860us. The
    first input after a switch to ARMED starts from there.
- RELEASE on SERVO is not a position and is taken while the bench is SAFE, where
  it moves no value. While ARMED it puts the pins at the rest and the
  value shown becomes that rest, as at a switch to ARMED: a drag under way ends, and
  the first input after it starts from the rest. The release that ends a run
  of the automatic test does the same.
- Leaving a bench screen switches the bench to SAFE.
- The rotary knob (an MT6701 angle sensor at 0x06; SETUP, PANEL, Rotary knob,
  OFF by default) moves the throttle on MOTOR & ESC and the horn on SERVO by
  how far it turns, as the slider does, and the same rules hold. It never
  switches to ARMED: the only way is the ARM hold. On both screens a turn while the bench
  is SAFE moves nothing and is not kept; after a switch to ARMED the knob goes on
  from 0% on MOTOR & ESC and from the rest on SERVO. Leaving the screen
  switches the bench to SAFE. The first
  reading after power-up, a reading that does not arrive (a NACK or a
  5ms timeout), and a step over
  90deg between two readings each set the reference again and move nothing,
  so a knob that stops answering stops contributing and no last value is held
  as a command. A finger on the track or dial owns the value against the
  knob for the whole frame in which it touched the control, and on SERVO a
  sweep, a test run and the settings panel own the horn. A frame that lost
  touch events, or in which the router navigated at all, drops the knob's
  motion. The knob's command goes out with the next frame; if that frame
  finds lost touch events first, the command is withdrawn and the value
  returns to what it was before the knob. The knob never replaces a command other than a throttle or
  a horn position that is waiting for the frame: a completed ARM, a DISARM, a
  release and a peak reset stay, and its motion is dropped. The knob is read by
  a task of its own on the core the renderer uses, never by the control task,
  with a 5ms bus timeout on each transaction of a poll: a probe while it
  has no reference, then three reads. A failed transaction also costs the
  driver's recovery, a controller reset and a bus clear that waits up to
  50ms more while SDA is held low. The knob sits on an I2C controller of its
  own (I2C_NUM_1, IO30 and IO31 of header P3) and nothing else is on that
  bus, so a sensor that holds SDA low delays the knob task only. The touch
  controller is on the other controller (I2C_NUM_0, GPIO7 and GPIO8), and
  a touch controller silent for 500ms switches the bench to SAFE
  (`ARMING_TOUCH_DEAD_MS`). The touch read is not on the heartbeat's path: the control task
  generates the heartbeat and does not use either I2C bus. The touch bus
  holds the GT911 at 0x5D, and the touch injector of a build with it at
  0x14. The panel refuses to put the knob, or any other device of the
  builder's, at those two addresses or at the ones the I2C specification
  reserves, 0x00 to 0x03 and 0x78 to 0x7F (`shared/bench/include/panel_i2c.h`).
  That nothing answers at the knob's 0x06 on the knob's bus rests on the
  bus being the knob's alone; what answers on the board is not measured.
- If the touch controller stops answering for 500ms, the bench switches to SAFE and
  refuses to switch to ARMED. The panel is the only place a STOP button exists.
- After a link failsafe the IOmcu does not switch back to ARMED when traffic returns.
  Leaving the failsafe takes a write of a defined value (0x5AFE) to the control
  page. The same holds after an IOmcu start and after a distrusted
  heartbeat: see [Arm latch](#arm-latch).
- A link lost while ARMED is a stop at the panel. It latches like STOP and is
  cleared by the next switch to ARMED.
- While the bench is ARMED the IOmcu refuses, with BAD_VALUE, an OUTPUTS
  write that changes a slot and a CHAN_CFG write that changes a channel's
  role or anything of a throttle channel. A surface's slew and pulse range are
  taken while ARMED: the SERVO screen writes them with every position. The panel
  writes a binding and the throttle's pulse range only while SAFE.
- The IOmcu acts on a lost link, a lost heartbeat and the PD mini's
  sagging input on its own authority and reports the fault at the next
  poll. Its fault bits for overcurrent, over-temperature and stall are set
  by nothing ([Errors](#errors)).
- A switch to ARMED names the roles it drives ([Link, An arm for one
  role](Link.md#an-arm-for-one-role)). MOTOR & ESC's ARM and a stick
  run on its PROGRAM switch the throttle outputs to ARMED, ARM 0x0002. SERVO switches the surfaces and the
  PROG PORT pin to ARMED, ARM 0x0004. A pin of a role the ARM does not name sends no
  pulse, as while SAFE. An IOmcu older than protocol 4.18
  takes any non-zero ARM as the whole bank, so on it every bound pin drives.
- A switch to ARMED for the other role goes through SAFE. The panel writes ARM = 0,
  waits at least 100ms, then writes the new roles; the IOmcu refuses
  a change from one non-zero ARM to another with BAD_VALUE.
- STOP, the heartbeat stopping, link silence and a switch to SAFE each zero the
  whole ARM, whatever roles it named, and every output stops.
- A bench in ARMED drives every bound pin of the roles it armed, whether or not
  anything is commanding it. A channel that has had no command for 500ms is rendered at its role's
  rest: stopped for a throttle, centred for a surface. Centred is the midpoint
  of that channel's own endpoints — 1500us across the default 1000 to 2000us,
  and 760us across the 660 to 860us of a narrow servo. The timeout moves the
  channel to that rest and leaves the pin driving; switching to SAFE is what stops the
  edges.
- A receiver output of 1500us is about half throttle. What an ESC that has
  seen no pulses does when it is then handed 1500us is not measured on this
  bench: it may run at about half throttle, and it may refuse to arm until it
  has seen a stop. A pin bound as a servo output can therefore carry a running
  motor.
- The throttle does not reach a channel bound as a surface, because it
  commands the pins the binding calls motors. A write to the CHANNELS page
  does, because that page addresses channels by index rather than by role.
- For the first 500ms after a switch to ARMED, a channel is not at its rest. The switch
  stamps every channel's clock, so a command given while the bench was
  SAFE is not overdue and is rendered until it goes overdue. What is
  rendered depends on the channel's slew: with no slew the first step is the
  whole distance, so the channel is at that command; with a slew set, the
  switch to SAFE has already put it at rest and it ramps from there, covering at most
  `slew_per_s * 500 / 1000` before the timeout takes it back. Either way it is
  driving, and a switch to SAFE is what stops it.
- The `Ramp limit` setting, 5 to 300%/s, governs the panel's own throttle
  bank. That bank is the modelled bench: its slewed value is read by the
  telemetry simulator and by nothing else, and only while the link is down. An
  IOmcu that is answering is sent the raw command instead, and
  `outbind_to_chan_cfg()` writes no slew for any channel, so a pin bound as a
  throttle steps to it on the next 1ms pass. The physical throttle is not
  ramped, by decision: the bank ramps a throttle upward only, so a ramp on
  the wire would slow the rise and nothing else. [STATUS.md, Not
  planned](https://github.com/subtilitas/rcbench/blob/main/STATUS.md#not-planned).

### A Hitec servo moved while SAFE

A Hitec servo may move while the bench is SAFE, inside a held programming
session only. This is the one exception to the rule that an output value
changes only while ARMED. The bench cannot switch to ARMED while the HITEC
page holds a port ([LinkHitec, STOP](LinkHitec.md#stop-a-switch-to-safe-heartbeat-loss-and-link-loss)),
and a Hitec servo takes its target over the programming wire, not as a
pulse width.

The rule:

- A move needs an entered session (`hitec_session_move()`; refused with
  `HITEC_SES_ERR_NOT_IN_MODE` otherwise). D series: a target of 0 to 6000
  written to register 0x1E, then register 0x0C, the position, read back.
  Classic: `66` with a target of 3600 to 8400 (900 to 2100us), then `65`.
  A target outside the range is refused (`HITEC_SES_ERR_ARG`).
- A move writes to the servo's RAM only: no EEPROM cell, no save
  (0x70), no reboot. No step of a move carries `MORE`, so an OFF of the
  supply during a move is not held for a write
  ([A programming port's supply](#a-programming-ports-supply)). A value
  reaches the servo's memory only through a write with its backup, record
  and read-back.
- The commanded position moves at most 150 counts (2.0deg) a pass and stays
  inside the reach of the servo's stored end points: the positions of
  targets 0 and 6000, 2606 to 13743 on a factory D485HW
  (`hitec_live_d_command()`).
- Each pass is judged (`hitec_live_judge()`). It binds when the position
  read lies more than 150 counts from a command that stands still or moves
  outward on 2 passes in a row, when the supply current stays at or above
  80% of the session's current limit for 400ms (800mA of the default
  1000mA), or when the AS5600 angle, while fitted, moved less than half of
  a commanded change of 3deg or more. A bind moves the command back to the
  last free position less 75 counts (1.0deg) toward the neutral.
- The mapping takes a servo set to CW only (`hitec_live_d_ends()`).
- Moves are switched on by a 2s hold, and only with a PD mini answering
  with its output on: STOP switches the PD mini off, and with it the
  servo. The PD mini's own current limit is the hard ceiling.

Current limitations:

- The panel has no control that switches moves on (MOVE SERVO on the
  TRAVEL tab): nothing calls `hitec_session_move()`.
- Every threshold of the judges is chosen, not measured with a horn
  against a block. The stall current of a D485HW is not known.
- Whether the servo follows the target line below 400 and above 5600 or
  stops at its end points is not measured; the reach assumes the line
  goes on.
- Whether a D series servo holds its last target with no frames is not
  measured. A switch to SAFE at the bench alone stops the port and leaves
  the supply on ([LinkHitec, STOP](LinkHitec.md#stop-a-switch-to-safe-heartbeat-loss-and-link-loss)).
- Whether a reboot discards a RAM write that was not saved is not
  measured.
- The mapping of a servo set to CCW is not measured.

## Heartbeat rather than enable level

A static enable level fails when firmware wedges with the pin high. Edges
expire on their own. The period check in firmware rejects a shorted or ringing
line, which a monostable alone would accept.

The panel has no wire to any output.
