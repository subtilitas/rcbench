# The monostable

The hardware backstop behind the heartbeat. A retriggerable monostable IC
(integrated circuit) with an RC (resistor-capacitor) timing network is
triggered by every edge on the panel's safety line and holds an enable node
high only while edges keep arriving. The enable node gates the coprocessor's
output enable and the servo and ESC (electronic speed controller) power path.
When the edges stop, the drive is removed and the power switches are open
within 200 ms, with no firmware at either end in the path.
[Safety](../../docs/Safety.md) says why the line carries edges and what this
circuit covers; this page says what the circuit has to be so it can be
designed and checked.

**No part number appears on this page.** [The README](../README.md) rules
that a part is not chosen until it is available at a vendor, and no part has
been checked. Every constant that depends on the part is marked as an
assumption to be replaced from the chosen part's datasheet.

**Status: specified, not built.** Nothing on this page has been measured.

## Constraints

| | Requirement |
| --- | --- |
| Input | edges on panel GPIO6 (general-purpose input/output pin 6), header J8 (3V3, GND, GPIO6), 3.3 V logic, one edge every 20 ms |
| Trigger | both edges, rising and falling. A single-edge trigger sees a 40 ms period and a worst case of 300 ms, and the window cannot cover that |
| Window | the enable node falls no sooner than 155 ms and no later than 185 ms after the last edge, at every corner of drift over 0 to 50 °C and a rail of 3.3 V ±3 %; set to 170 ms on test |
| Deadline | the output buffer is disabled and every power switch is open within 200 ms of the last edge |
| Output | one enable node, high while retriggered, low otherwise, feeding both gates |
| Gates | the output enable of the buffer between the RP2350 and the output connector, and a high-side switch on each of the servo rail and the ESC pack |
| Fail-safe direction | unpowered, undriven, unbuilt, an open trigger line, and the panel driving into an unpowered circuit all mean both gates disabled |
| Power-up | the enable node stays low through the 3.3 V ramp until edges arrive |
| Not defeatable | no programmable element in the path from GPIO6 to either gate |
| Fault model | absence of anything is disabled, and so is a timing element that shortens a window; a timing element that lengthens a window or opens, and a logic output or a switch failed conducting, are not covered, see [Fault model](#fault-model) |
| Supply | the coprocessor board's 3.3 V rail; ground common with J8's GND |

## Input

The panel's control task runs every 5 ms on the core that does not draw. On
every pass it asks the heartbeat generator for a level, and the generator
toggles once per `HEARTBEAT_PERIOD_MS` (20 ms), so the line carries one edge
every 20 ms and one full cycle every 40 ms. The panel drives GPIO6 push-pull
at 3.3 V (`firmware/panel/main/main.c`). Nothing else may drive the node.

The constants are in
[`heartbeat.h`](../../shared/safety/include/heartbeat.h):

| Constant | Value | Meaning |
| --- | ---: | --- |
| `HEARTBEAT_PERIOD_MS` | 20 ms | edge interval the panel aims for |
| `HEARTBEAT_MIN_GAP_MS` | 4 ms | shorter is rejected by the firmware as noise |
| `HEARTBEAT_MAX_GAP_MS` | 150 ms | longer is rejected by the firmware as a stall |
| `HEARTBEAT_GOOD_RUN` | 4 | intervals before the firmware trusts the line |

The monostable does not check any of those. It retriggers on any edge and
expires a window after the last one. The firmware check exists because of that
difference; see [With the firmware monitor](#with-the-firmware-monitor).

The panel is powered on its own USB and beats whether or not the coprocessor
board has power. The trigger input therefore sees 3.3 V edges while the
circuit's own 3.3 V rail may be absent. Two requirements follow:

- The trigger input is specified for partial power-down: a datasheet limit
  on the input current with V_I above V_CC and V_CC = 0 V (the I_off
  specification), which means no diode from the input to the supply pin. An
  input without that specification is back-powered by the panel through its
  protection diode, runs the IC from the heartbeat, and can assert Q.
- A 4.7 kΩ series resistor between the junction and the trigger input, on
  the monostable's side of its removable link. It bounds any current into
  an unpowered input to 0.7 mA at 3.3 V. With the 100 kΩ pull-down behind
  it the high level at the input is 3.15 V, above a 74HC-class V_IH of
  2.31 V at 3.3 V.

## Timing budget

| Item | Value | Where it comes from |
| --- | ---: | --- |
| Control-task period on the panel | 5 ms | `firmware/panel/main/main.c` |
| Edge interval, nominal | 20 ms | `HEARTBEAT_PERIOD_MS` |
| Edge interval seen by a single-edge trigger | 40 ms | the level toggles once per period |
| Longest interval the firmware accepts | 150 ms | `HEARTBEAT_MAX_GAP_MS` |
| Margin for one late control-task pass | 5 ms | one pass of the task that emits the edge |
| Enable node, lower bound at every corner | 155 ms | 150 ms + 5 ms: the monostable never drops out on a heartbeat the firmware accepts |
| Enable node, set point | 170 ms | set on test, see [Timing network](#timing-network) |
| Enable node, upper bound at every corner | 185 ms | the deadline less the downstream budget |
| Buffer disabled after the enable node falls | under 1 µs | a logic output-enable; not in the budget |
| Power switch open after the enable node falls | 5 ms | budget for the driver and the switch; a relay whose release time exceeds it is not a candidate |
| Reserve | 10 ms | between the switch opening and the deadline |
| Deadline: drive removed, switches open | 200 ms | the coprocessor's link failsafe; the backstop is not slower than the link |
| Coprocessor link failsafe | 200 ms | silence on the CAN (Controller Area Network) link |

The order of events after the last edge at t = 0, with a healthy coprocessor:
the firmware monitor disarms at 150 ms; the enable node falls between 155 ms
and 185 ms; the buffer is high impedance within 1 µs of that and every switch
is open within 5 ms of it, so by 190 ms at the latest; the link failsafe
fires at 200 ms after the last frame. With a coprocessor that cannot act, the
middle events are the only ones, and the drive is gone before 200 ms.

The switched rail's fall from the switch opening to the voltage its load
stops at is not in the budget. It depends on the load's own input
capacitance and its idle current: an ESC with 470 µF behind its connector
and a 50 mA idle draw takes about 230 ms from 25 V to 0.5 V, a servo with
10 µF and the same draw takes about 2 ms. The signal gate is the fast path,
and it is what removes a command; the open switch is the second barrier,
and the time its rail takes to decay is measured and recorded, not
specified.

The window's lower bound is above the longest accepted gap so that the two
watchers on the wire never disagree in the wrong order: a gap the firmware
forgives never removes the enable, so an armed run never sees its outputs
blink for a stall the coprocessor did not act on.

## Design: both edges

The two ways to retrigger on both edges:

1. A dual retriggerable monostable, one half triggered by the rising edge and
   the other by the falling edge, outputs combined by an OR gate.
2. A both-edge detector (an XOR (exclusive or) gate fed by the line and a
   delayed copy of it, which emits one short pulse per edge) in front of a
   single monostable.

**The design is option 1.** The common dual retriggerable monostable type
gives each half one trigger input that acts on a rising edge and one that
acts on a falling edge, so both polarities are triggered as the datasheet
specifies them and no pulse has to be shaped. Option 2 adds a second RC
network whose pulse width has to be longer than the monostable's minimum
trigger width and stable over temperature, which is a second timing analysis
for no gain. Both halves of one package share a die, a supply and a
temperature, so their windows track.

Wiring:

| Node | Half A | Half B |
| --- | --- | --- |
| Rising-edge trigger input | the heartbeat line, through the 4.7 kΩ series resistor | held at its inactive level |
| Falling-edge trigger input | held at its inactive level | the heartbeat line, through the same resistor |
| Clear input | the power-on reset network, shared | the same network |
| Timing network | R, C as below | R, C as below, same values |
| Q output | to the OR gate | to the OR gate |

A part with one trigger input of fixed polarity per half takes an inverter in
front of one half. The inverter's input then sees the live heartbeat while
the coprocessor's rail may be absent, exactly as the monostable's input
does, so it meets the same two requirements: the I_off specification, and a
place behind the 4.7 kΩ series resistor, which bounds the current into it
to 0.7 mA. An inverter without the I_off specification is back-powered by
the panel through its protection diode and can hand the interlock a supply
the output pull below does nothing about. Test step 4 records its supply
pin. The inverter's output carries a 100 kΩ pull to the level
that does not trigger that half's input (low for a rising-edge input, high
for a falling-edge input), so an inverter that is absent, unpowered or has
lost its supply while the monostable is powered leaves the input held at
its inactive level rather than floating. Without the pull, a high-impedance
inverter output is an input that can pick up edges, which is not the safe
direction. Test steps 2 and 3 cover the held level.

The OR gate's output is the enable node. It is a push-pull logic output, so
the enable node's pull-down acts only when the gate is unpowered or the node
is unbuilt. Each OR input carries a 100 kΩ pull-down of its own: an absent
monostable, a dead Q output or a lifted lead then reads as that half expired
rather than as a floating CMOS input, which the pull-down on the gate's
output cannot reach. A diode-OR into the pull-down is the alternative combining
element; it is not the design because a Schottky drop of 0.3 V leaves 3.0 V
on the node, and a 74HC-class input needs 2.31 V at 3.3 V, so the margin is
spent on the diode.

Why the OR of two windows behaves like one window triggered on both edges:
the enable node falls when both halves have expired. The half retriggered by
the last edge expires a window after it; the other half was retriggered one
interval earlier and expires sooner. So the enable falls one window after the
last edge of either polarity, and a gap between any two consecutive edges is
covered as long as it is shorter than the window.

### Power-up

Both clear inputs are held active through the 3.3 V ramp by one power-on
reset network: 100 kΩ from the rail to the clear inputs, 1 µF from the clear
inputs to ground, and a diode across the resistor so the capacitor empties
when the rail drops. The clear inputs are active for about 100 ms after the
rail reaches 3.3 V on a fast ramp, long after the monostable's own
supply-rise requirement, and Q is low throughout. The datasheet has to state
that Q is low while clear is active; every retriggerable monostable of this
class does.

The RC network has no threshold of its own. On a slow ramp or a shallow
brownout the clear input follows the rail and can release before the supply
is stable, and a clear input rising slowly can violate the part's input
transition-time limit. A reset supervisor with a stated threshold and delay
does not have those gaps. Which of the two the circuit uses is an open
decision, listed under [Not specified](#not-specified); the RC network is
the placeholder until it is taken, and test step 5 covers a fast ramp only.

The release of clear is itself a trigger on some parts: the common '123-type
dual monostable fires a full pulse when clear goes inactive while its trigger
inputs sit at their armed levels, and in this circuit one half or the other
always does, whichever level the heartbeat line holds. **A part whose clear
release can trigger is excluded**; the '423-type behaviour, where clear
release never triggers, is required and is stated in the datasheet. Until a
part is chosen this is the constraint that decides between otherwise
identical families.

The coprocessor's outputs are off at boot and it refuses to arm before four
good intervals, so a power-up pulse would drive nothing today. The circuit
does not rely on that: the requirement is no pulse.

### Fault model

What the circuit covers is absence: no supply, no drive, no wire, no part,
an open link. It also covers a timing element that shortens a window: R or C
low in value, a shorted C, one half's k low. In every one of those cases the
enable node is low or falls early, which is the safe direction. A half that
has failed silent (an open trigger input, a dead Q) makes the surviving half
a single-edge trigger: it holds the node up on a nominal heartbeat and drops
out whenever two consecutive intervals together exceed the window.

What it does not cover is a timing element that lengthens a window. R or C
risen in value expires late: 18 % above the set point is past the 200 ms
deadline. An open R never charges C, so that half's Q stays high from its
next trigger for as long as the part is powered, and the OR passes it while
the healthy half expires. The two-channel alternative below masks a single
long or open element; this design does not, and whether that is accepted is
an open decision listed under [Not specified](#not-specified).

Also not covered is a part failed conducting: a Q output stuck high, an OR
gate output stuck high, a buffer enable shorted to its active level, a
switch failed short. Any single-channel interlock has those points, and the
OR of two halves has two Q outputs where a single monostable has one. The
test procedure measures each half's own fall for that reason, and the rail
measurement in `testbench/WIRING.md` section 7 exists for the shorted switch.

The alternative that covers a stuck-high output is two channels in series:
each channel its own both-edge detector and its own monostable, the two Q
outputs combined by an AND gate, each channel fed every 20 ms so its window
is the one on this page. Either channel then removes the enable on its own,
so a stuck-high Q, a long timing element or an open R in one channel is
masked by the other. It costs two edge detectors with their own timing
analysis and an AND gate, and the AND gate's output is the remaining single
point. It is not this design; it is recorded here as the decision to take if
a stuck-high output or a long timing failure is brought into the fault
model.

## Timing network

A retriggerable monostable's window is

    t = k * R * C

where R and C are the external timing network and k is a constant of the
part's family, given in its datasheet, together with a graph of how it moves
with supply voltage and temperature. Values of k in common families lie
between about 0.3 and 1.0, so R and C cannot be chosen before the part is.

A fixed R and C do not put the window inside the band. A ±5 % capacitor, a
k that moves ±5 % with supply and temperature, a ±1 % resistor and the
timing pin's input leakage together span more than the 155 ms to 185 ms the
band allows (the product of the worst cases is a ratio of about 1.35 between the
longest and shortest window; the band is a ratio of 1.19). So the window is
**set on test**: C is fitted, the window is measured, and R is selected from
the E96 series to put the enable node at 170 ms. That calibrates out the
initial tolerance of C, of k and of the leakage at the setting temperature;
what is left is drift.

**Assumed for the worked values: k = 0.45 at 3.3 V and 25 °C, moving ±3 %
over a supply of 3.3 V ±3 % and 0 to 50 °C.** The value is what the
widespread 74HC-class dual retriggerable monostable with paired trigger
inputs quotes; the drift is an assumption, since datasheets of this class
graph k against supply rather than stating a limit. Both are replaced from
the chosen part's datasheet before the values are final, and the drift table
is recomputed.

Worked values:

| | Value | Tolerance | Notes |
| --- | ---: | ---: | --- |
| C | 3.3 µF | ±5 %, −200 ppm/°C | polypropylene film. Not X7R, X5R or electrolytic: ±15 % over temperature, a voltage coefficient and leakage that eats the timing current. C0G ceramic is preferred where the part's timing range allows a value it exists in |
| R | 115 kΩ nominal, selected between 102 kΩ and 127 kΩ | ±1 %, ±100 ppm/°C | E96 metal film. The selection range covers C at ±5 % and k at ±5 % from the assumed values; one E96 step moves the window by 2.4 %, 4 ms |
| k | 0.45 | assumed | from the datasheet |
| t, nominal | 170.8 ms | | 0.45 × 115 kΩ × 3.3 µF, before selection |
| Timing current | 29 µA at 3.3 V | | 3.3 V / 115 kΩ; the leakage bound below is 3.5 % of it |
| Timing-pin leakage | 1 µA at 85 °C, assumed 0.5 µA at 50 °C | assumed | the 85 °C figure is the class's datasheet limit; the 50 °C figure is an assumption, and the datasheet's 25 °C figure with the leakage's doubling per 10 °C replaces it |

Drift after setting, over 0 to 50 °C (±25 °C from the 25 °C setting
temperature, the range the bench is assumed to be used in; not analysed
outside it):

| Term | Bound | Basis |
| --- | ---: | --- |
| Setting | ±1.5 % | half an E96 step, 1.2 %, plus 0.3 % for reading the crossing on the scope |
| R over temperature | ±0.25 % | 100 ppm/°C × 25 °C |
| C over temperature | ±0.5 % | 200 ppm/°C × 25 °C, polypropylene |
| k over supply and temperature | ±3 % | assumed; from the datasheet |
| Leakage change over temperature | ±2 % | 0.5 µA × 115 kΩ = 58 mV against 3.3 V; assumed, see above |
| Sum | ±7.25 % | linear sum of the bounds, the worst case for terms that can all move one way |

| Corner | Window | Margin |
| --- | ---: | ---: |
| Minimum | 157.7 ms | 2.7 ms above 155 ms |
| Set point | 170 ms | |
| Maximum | 182.3 ms | 2.7 ms below 185 ms |

The margins are 2.7 ms each way with the assumed k drift and leakage. A
datasheet k drift wider than ±3 % or a leakage above the assumption breaks
the band; the responses are a lower R with a larger C (the leakage term
scales with R), a capacitor with a smaller temperature coefficient, or a part
with a stated k limit. Propagation delay from trigger edge to Q and through
the OR gate is below 1 µs and is not in the budget.

Rules for the split between R and C, whatever the part:

- R stays inside the part's permitted external resistor range, and between
  50 kΩ and 150 kΩ, so the timing current at 3.3 V is 22 to 66 µA and the
  1 µA leakage bound is at most 4.5 % of it. For k = 0.45 and a 170 ms window
  that puts C between 2.5 µF and 7.6 µF.
- C stays inside the part's permitted external capacitor range and in a
  dielectric with a stated temperature coefficient.
- The timing components sit at the part's pins, and the timing node has no
  other connection; a probe on it changes the window.

## The two gates

Both gates are downstream of the coprocessor's pins. The coprocessor drives
GP0, GP1 and GP2 exactly as it does with no gate fitted; what changes is
whether the drive reaches the connector and whether the connector has power.

**Output enable.** Between the RP2350's output pins and the output connector
sits a buffer or a level translator (for lines that leave the 3.3 V island),
and that device has an enable pin. Enabled means its outputs follow the
RP2350's pins. Disabled means its outputs are high impedance, and each
connector-side signal line is pulled down (10 kΩ) so a disabled buffer leaves
the line low, which is idle for a servo pulse and for a DShot frame. The
device's enable is taken from the enable node:

- an active-high enable takes the node directly, with the node's pull-down as
  its default;
- an active-low enable takes the node through an inverter and carries a
  pull-up (10 kΩ) of its own, so an unpowered inverter and an undriven node
  both read as disabled.

The buffer covers GP0, GP1 and GP2. The binding catalogue in
`shared/outputs/out_bind.c` offers more: GP4 to GP7, GP13 to GP22 and GP26
to GP28 are free for an output on the RP2350-CAN board, and `docs/FirstRun.md`
lists them. A pin bound there is driven by the coprocessor with nothing
between it and whatever is wired to it, so the claim that the drive is
removed holds for the gated connector only. Which of the two closes the
difference, a buffer on every bindable pin or a catalogue limited to the
gated pins, is an open decision listed under [Not specified](#not-specified).

The 10 kΩ pull-down is the idle for a servo pulse and for plain DShot.
Bidirectional DShot idles high: `firmware/iomcu/src/out_dshot.c` releases
the line for the ESC's reply and its pull-up of about 50 kΩ holds the idle,
and a 10 kΩ pull-down against that pull-up holds the line at about 0.55 V,
which is low, so the receiver cannot find the reply's first falling edge.
The bias on a channel that carries bidirectional DShot is an open decision
listed under [Not specified](#not-specified).

**Power path.** A high-side switch on each rail that reaches a load: the
servo rail (up to 8.4 V, 4 to 8 A, [Power](Power.md)) and the ESC pack. Each
switch has a control input driven from the enable node through whatever level
shift or gate driver the switch needs. Enabled means the switch is closed and
the rail is present at the connector. Disabled means the switch is open. The
switch is open when its control input is undriven and when the 3.3 V logic
supply is absent: an enhancement-mode MOSFET (metal-oxide-semiconductor
field-effect transistor) with a gate-to-source resistor satisfies that, as
does a normally-open relay; a normally-closed contact or a depletion-mode
device does not. The switch is open within 5 ms of the enable node falling.

The switched side of each rail decays through its load once the switch is
open. `testbench/WIRING.md` section 7 gives the measurement, why a meter
cannot make it, and why the rail is measured and not only the control node.

**Polarity.** The enable node is active high: the monostable's true Q output
is low at rest and high while retriggered, so the OR of the Q outputs is high
exactly while edges arrive. Every gate is arranged so that a low node means
disabled.

## Fail-safe direction

Every state the circuit can be left in reads as disabled:

| State | What holds it disabled |
| --- | --- |
| Trigger line open (a link out, the panel unplugged) | a 100 kΩ pull-down on the trigger input; no edges, both halves expire |
| Trigger line stuck at either level | no edges; both halves expire |
| Monostable unpowered, panel silent | its Q outputs are undriven; the OR gate is unpowered; a 100 kΩ pull-down on the enable node holds it low |
| Monostable unpowered, panel beating | the partial-power-down input and the 4.7 kΩ series resistor keep the heartbeat from powering the IC; the pull-downs as above |
| Coprocessor board unpowered, panel beating | the 4.7 kΩ series resistor in the GP3 branch bounds the current into the RP2350's pad to 0.7 mA; how far that raises the board's 3.3 V rail is not measured, and test step 4 measures it |
| Monostable absent or a Q lead off, OR gate powered | the 100 kΩ pull-down at each OR input |
| Enable node unbuilt or a lead off | the same pull-down |
| Output enable undriven | the pull-down (active-high enable) or the pull-up (active-low enable) at the device's pin |
| Switch control undriven or logic supply absent | the gate-to-source resistor or the relay coil |
| 3.3 V ramp | the power-on reset network holds clear active for about 100 ms; the release does not trigger |
| Powered, no edge yet | Q is low until the first trigger; the firmware side additionally needs four good intervals before it arms |

The RP2350 pulls GP3 down in firmware (`firmware/iomcu/src/main.c`). That
pull-down is not in this circuit's fail-safe list: it is set by firmware,
and this circuit is the thing that must hold when firmware does not.

## Not defeatable

The path from J8 to either gate is GPIO6, the series resistor, the trigger
inputs, the timing networks, the Q outputs, the OR gate, the enable node, and
the gates. No processor, no register and no firmware is in it. The
coprocessor's GP3 is an input on the same node: it listens so the firmware
can judge the line, and it cannot remove the enable or add to it.

A coprocessor that drove GP3 with edges would retrigger the monostable
against the panel's push-pull drive. That is the third row of the table in
[Safety](../../docs/Safety.md): a healthy panel beside a misbehaving
coprocessor, which no hardware in this design covers.

## The node and its two links

The heartbeat is one node with three branches: the panel's GPIO6, the
coprocessor's GP3 and the monostable's trigger input. Two removable links
are in it, one in the panel's branch and one in the monostable's trigger
branch, meeting at a junction with GP3. `testbench/WIRING.md` section 7 uses
them: the panel's link opens so the RP2350 can drive the junction from GP22
without contention (the panel drives push-pull), and the monostable's link
opens while a capture runs so the last trigger edge and the enable falling
are in one trace. The series resistor and the trigger pull-down sit on the
monostable's side of its link, so an open link is a low trigger and not a
floating one.

The GP3 branch has the same exposure as the trigger branch: with the
coprocessor board unpowered and the panel beating, the junction drives GP3
at 3.3 V into an RP2350 pad whose protection path leads to the board's IOVDD
(the I/O supply), and the RP2350 is not specified as tolerating an input
above an absent IOVDD. A 4.7 kΩ series resistor in the GP3 branch, on GP3's
side of the junction, bounds that current to 0.7 mA. With the firmware
pull-down of about 50 kΩ behind it the high level at GP3 is 3.0 V, above the
RP2350's V_IH of 2.0 V at 3.3 V. Whether 0.7 mA raises the board's rail
enough to power anything is not measured; test step 4 measures the rail with
the panel beating.

## With the firmware monitor

The coprocessor checks the same wire in firmware because the monostable
cannot tell a heartbeat from noise: a line that rings, is shorted to a clock,
or is driven by something other than the panel edges fast, and every edge
retriggers. The firmware rejects intervals under 4 ms and over 150 ms and
needs four good intervals before it trusts the line. The monostable covers
the case the firmware cannot: a coprocessor that is wedged or not servicing
its monitor while the panel has stopped. Both exist for those two reasons,
and neither replaces the other.

As specified on this page the monostable does not latch. When edges resume,
the enable node returns within one propagation delay of the first edge. What
latches is the coprocessor's arm: the firmware monitor disarms on the gap,
and nothing re-arms without an operator. A coprocessor wedged with its
outputs driving, beside a panel that stops and then resumes beating, has its
outputs re-enabled when the edges return, and `STATUS.md` holds that every
stop latches. Whether a hardware latch that only an operator action clears
is added to this circuit is an open decision listed under
[Not specified](#not-specified).

## Test procedure

Nothing below has been run; every result is "not measured" until it is.
Instruments: a two-channel oscilloscope with at least 200 ms of pre-trigger
record, the analyser and `capture.sh` from `testbench/`, and a probe rated for
the ESC pack voltage. The captures of `testbench/WIRING.md` section 7 apply;
what follows is what to measure on this circuit and what passes. Every
crossing of a logic node is read at 1.65 V.

**1. Setting, one half at a time.** Fit both capacitors and a starting R of
115 kΩ in each half. Measure half A's window by step 8: channel 2 on
half A's own Q output, the interval from the last rising edge on channel 1
to Q falling, three captures. Half A's Q does not depend on half B, so the
other half needs no attention. Compute R_A for 170 ms from the mean (the
window is proportional to R), select the nearest E96 value, fit it, and
measure again. Repeat for half B on its own Q output from the last falling
edge. Pass: three captures per half between 167.5 ms and 172.5 ms at 20 to
30 °C ambient with the rail at 3.3 V ±1 %. Record R_A, R_B, the measured
capacitors, the ambient temperature and the rail voltage.

**2. Static, no edges.** Panel's branch open, GP22 tri-stated. Record the
voltage at the trigger input, each Q output, the enable node, the buffer's
enable pin, the output of the inverter where one is fitted, and each
switched rail with its load connected. Pass: trigger input, Q outputs and
enable node below 0.4 V; the inverter's output at its non-triggering level
(below 0.4 V or above 2.9 V, whichever the half needs), and the same with
the inverter not fitted; each rail below the voltage its load is measured
to stop at, or below 0.5 V until that voltage is measured.

Then the buffer's high impedance, which a voltage reading cannot show: a
disabled buffer that drives low and a high-impedance one both read the
pull-down's 0 V. Apply a 4.7 kΩ resistor from each connector signal line to
3.3 V in turn. Pass: the line rises above 2.0 V (2.24 V with the 10 kΩ
pull-down) and returns below 0.4 V when the resistor is removed. A line that
stays below 0.4 V under the resistor is being driven low by the buffer, and
the buffer is not disabled. Step 12 repeats this with the coprocessor
driving the buffer's input.

**3. Static, unpowered, panel silent.** Repeat step 2 with the 3.3 V supply
to the monostable and the OR gate removed and the rails' supplies present.
Pass: the same figures.

**4. Unpowered, panel beating.** The coprocessor board's 3.3 V rail removed,
which takes the monostable, the OR gate and the RP2350 with it, the load
rails' supplies present, the panel's branch closed and the panel beating (an
edge every 20 ms on the junction). Record the voltage at the board's 3.3 V
rail, at the monostable's supply pin, at the inverter's supply pin where
one is fitted, at GP3, at each Q output and at the enable node, and capture
the enable node for 60 s. Pass: board rail and every supply pin below
0.3 V, Q outputs and enable node below 0.4 V, no edge on the enable node in
60 s, each load rail as in step 2. A supply pin above 0.3 V is the
heartbeat back-powering that part through its input, and the part is not
one with the I_off specification; a board rail above 0.3 V is
the heartbeat back-powering the RP2350 through GP3, and the GP3 branch's
series resistor is missing or too small.

**5. Power-up.** Panel's branch open, GP22 tri-stated. Capture the 3.3 V rail
on channel 1 and the enable node on channel 2, the scope triggered on the
rail rising through 1 V, 500 ms of record. Apply power. Ten times. Then ten
times more with the line held at a level with no edges, in one of two ways
and never both: the panel's branch closed and the panel holding the line
(the panel powered and not yet running its control task) with GP22
tri-stated; or the panel's branch open and the junction held from GP22.
GP22 on the junction with the panel's branch closed is two push-pull
drivers on one node, the contention `testbench/WIRING.md` section 7 opens
the branch to prevent. Pass: the enable node never crosses 1.65 V in any of
the twenty records. A pulse of one window on the enable node after the rail
settles is the clear release triggering, and the part is excluded.

**6. Held, edges arriving.** Junction driven at one edge every 20 ms (GP22 per
`testbench/WIRING.md`, or a 3.3 V square wave at 25 Hz, panel's branch
open). Capture the enable node for 60 s. Pass: no falling edge on the
enable node in 60 s; each Q output shows no gap.

**7. The window, on the scope.** Panel's branch open, the monostable's link
closed, the junction driven from GP22 or a generator as in step 6. Trigger
input on channel 1, probed at the monostable's input. Enable node on
channel 2. Scope triggered on channel 2 falling through 1.65 V, 200 ms or
more of pre-trigger. Stop the source with its output held at its final
level: high for a rising last edge, low for a falling one. Read the
interval from the last edge on channel 1 to the crossing on channel 2.
Repeat ten times: five stopped high, five stopped low. Record every
interval, the ambient temperature and the supply voltage. Pass: every
interval between 155 ms and 185 ms.

Opening the monostable's link, as `testbench/WIRING.md` section 7 does,
gives a falling last edge only: the pull-down on the monostable's side
makes the opening itself a falling edge whenever the line is high, and
when the line is low the last edge was already falling. That capture is
valid for the falling polarity and is not a way to get the rising one.

**8. Each half.** Repeat step 7 with channel 2 on each Q output in turn,
three captures each: half A from the last rising edge, half B from the last
falling edge. Pass: each half's interval from its own last edge between
155 ms and 185 ms. This is the measurement step 1 sets each half's R from;
before setting, two capacitors at opposite ends of ±5 % put the halves up
to 18 ms apart, and after it each is within 2.5 ms of 170 ms.

**9. The switches and the rails.** Two captures per switch, with the source
stopped as in step 7 and the load connected. The window varies from event
to event, so a delay measured against the enable node has to have the
enable node in the same trace.

*The 5 ms budget.* Enable node on channel 1, the switch's control node on
channel 2, the scope triggered on channel 1 falling through 1.65 V, 10 ms
of pre-trigger and 50 ms of record. Five events. Pass: the control node
reaches its open level within 5 ms of the trigger in every event.

*The rail.* Trigger input on channel 1, the switched rail on channel 2, the
scope triggered on channel 2 falling by 5 % of the rail's set voltage,
250 ms of pre-trigger (the last edge is at most 185 ms plus 5 ms before the
trigger) and at least 1 s after it. Five events. Pass: the rail leaves
regulation within 190 ms of the last edge in every event. From the same
trace, record the time from the last edge to the rail crossing the load's
stop voltage, the voltage used as the threshold, whether it is measured or
the 0.5 V placeholder, and the load. That time is recorded, not passed or
failed: it belongs to the load's capacitance and idle current, and the
page's own example of 470 µF at 25 V with 50 mA of idle draw puts it at
about 230 ms. A decay that has not reached the stop voltage when the record
ends is recorded as longer than the record, with the record length; the
trigger is on the rail leaving regulation and not on the stop voltage for
this reason, since a trigger at the stop voltage needs pre-trigger history
covering the whole decay and the last edge before it.

**10. The band, driven.** Junction driven at one edge every 200 ms from the
generator. The enable node then falls for 200 ms minus the window on every
cycle. Capture 100 cycles with the analyser at 1 MHz. Pass: every low pulse
between 15 ms and 45 ms, which is a window between 155 ms and 185 ms; record
the minimum and maximum. Then one edge every 150 ms for 60 s. Pass: no
falling edge on the enable node.

**11. The corners.** Not measured. The drift table's k term combines
temperature with supply, and the family's datasheet graphs k rather than
bounding it, so temperature alone cannot confirm the table. Steps 7 and 10
run at each of five combinations: 25 °C at 3.3 V, and 0 °C and 50 °C each
at the rail's permitted minimum and maximum, 3.2 V and 3.4 V, with the
temperature applied to the timing network and the part. Pass: every
interval between 155 ms and 185 ms at every combination. Record which
combination gives the shortest and the longest window; those are the two
the datasheet's k graph should predict, and a unit whose extremes lie
elsewhere has a drift term the table does not carry. Until this is run
the corners are calculated, not measured.

**12. Buffer high impedance, input driven.** The differential setup of
`testbench/WIRING.md` section 7: panel's branch open, GP22 driving the
junction so the firmware stays alive, the monostable's link open so the
enable is down, the bench armed and the output under test commanded away
from rest, so the buffer's input is toggling. Apply the 4.7 kΩ resistor
from the connector signal line to 3.3 V as in step 2. Pass: the line sits
above 2.0 V with no edge on it (analyser at 24 MHz, 2m samples, as the
quiet capture in the wiring guide), and returns below 0.4 V without the
resistor. Edges on the line are a buffer that is not disabled; a line held
low is a buffer driving low while disabled.

What to record, per built unit: the part and its datasheet k, clear-release
behaviour and I_off specification, the measured C and the fitted R per half,
the ten intervals of step 7, the six of step 8, the rail figures of step 9,
the minimum and maximum of step 10, the five combinations of step 11 with
their intervals, the two readings of step 12 per line, ambient temperature
and supply voltage.

## Limitations

- As specified, the monostable does not latch; see
  [With the firmware monitor](#with-the-firmware-monitor) and the open
  decision below.
- A healthy panel beside a misbehaving coprocessor is not covered, by this
  circuit or by any hardware in the design.
- A timing element that lengthens a window or opens, and a logic output or
  a switch failed conducting, are not covered; see
  [Fault model](#fault-model).
- The power-on reset network holds clear through a fast ramp only; a slow
  ramp or a brownout is not covered by it.
- The switched rail's decay to the load's stop voltage is not bounded; it is
  measured with the load.
- The gated buffer covers GP0 to GP2; the binding catalogue offers more
  pins, which are not gated.
- The 10 kΩ connector-side pull-down holds a bidirectional DShot line low
  during the ESC's reply, and the element carrying the output enable has to
  pass that reply while enabled. Neither is resolved.
- The window's margins are 2.7 ms each way at the calculated corners. They
  rest on the assumed k drift and leakage.

## Not specified

- Part numbers, for the monostable, the OR gate, the buffer or translator,
  the switches and their drivers. The rule is in [the README](../README.md).
- The switch technology and rating on the ESC pack. The pack path carries up
  to 300 A ([Power](Power.md)), and a switch there is its own decision.
- The PCB (printed circuit board): where the circuit sits, on the
  coprocessor's board or on its own.
- The connector between J8 and the trigger input, and the connector the
  gated outputs leave by.
- The temperature range outside 0 to 50 °C.

### Open decisions

Each of these is a design decision the project owner takes. The page
records the point, the two ways it can go and what each costs, and takes
neither.

- **Long timing-element failures.** Either the single-channel OR design
  stands, with a fault model that excludes an R or C risen in value and an
  open R (a half that then holds the enable up while the other expires), at
  no extra cost; or the two-channel AND design replaces it, masking any
  single long, open or stuck-high element, at the cost of two both-edge
  detectors with their own timing analysis, an AND gate whose output is the
  remaining single point, and a second setting procedure.
- **A hardware latch.** Either the enable returns on the first resumed edge,
  as specified, and the latch is the coprocessor's arm in firmware, which
  leaves a wedged coprocessor's outputs re-enabled when a stalled panel
  resumes; or a latch set by the first expiry and cleared only by an
  operator action is added, which honours `STATUS.md`'s rule that every stop
  latches, at the cost of a clear input that is a new path into the
  interlock, a control for it, and a bring-up bench that needs that control
  pressed after every stall.
- **The clear network.** Either the RC network stands (a resistor, a
  capacitor, a diode, no threshold), covering a fast ramp and not a slow
  ramp or a brownout; or a reset supervisor with a stated threshold and
  delay holds clear through any ramp and any brownout below its threshold,
  at the cost of a part that has to be chosen by the README's rule and a
  threshold that has to suit the monostable's minimum supply.
- **Which pins are gated.** Either every bindable pin (GP0 to GP2, GP4 to
  GP7, GP13 to GP22, GP26 to GP28) passes through a gated buffer, so the
  drive is removed wherever an operator binds an output, at the cost of
  buffers for 20 pins and a connector that carries them; or the binding
  catalogue is limited to the gated pins, which is a firmware change outside
  this page and leaves the other pins free for the bench's own use.
- **Bias on a bidirectional DShot channel.** Either the bias is selected
  per protocol, a pull-up for bidirectional DShot and a pull-down otherwise,
  which puts a protocol-dependent element on the connector side and asks who
  selects it, since firmware is not in this path; or the gated interface
  itself presents a high idle when disabled, an open-drain stage with a
  pull-up on the ESC side, which suits bidirectional DShot and changes the
  idle for a servo pulse and plain DShot from low to high, a level both
  protocols read as no signal but which is not measured on any ESC or servo
  here.
