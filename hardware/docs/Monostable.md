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
| Powered-off isolation | every logic device in the interlock whose input can be driven while its own supply is absent has I_off inputs or a series isolation resistor sized for its input protection, and the static tests remove each device's supply alone with everything upstream live; see [Powered-off isolation](#powered-off-isolation) |
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
circuit's own 3.3 V rail may be absent. That is one instance of a rule that
covers every logic device in the path.

### Powered-off isolation

An ordinary CMOS input has a protection diode to its supply pin. Drive that
input while the device's own supply is absent (a lifted supply lead, a
board rail that is down, a device whose supply comes up later than its
neighbour's) and the diode conducts: the device runs from its input, its
outputs are driven, and the pull-downs that are supposed to hold its
outputs' nodes low cannot override a driven output. The interlock's safe
state depends on every unpowered device being silent, so:

**Every logic device in the interlock whose input can be driven while its
own supply is absent has, on every such input, the I_off specification (a
datasheet limit on the input current with V_I above V_CC and V_CC = 0 V,
which means no diode from the input to the supply pin), or a series
isolation resistor sized for its input protection: at most 1 mA into the
input at 3.3 V, so 4.7 kΩ or more.** The devices and what drives them:

| Device | Driven while unpowered by | Isolation |
| --- | --- | --- |
| The monostable, or any both-edge part in front of it | the panel, through the trigger line | I_off, and the 4.7 kΩ series resistor in the trigger branch |
| Any inverter in the trigger path | the panel | I_off, behind the same series resistor |
| The OR gate | a Q output of a powered monostable, when only the OR gate's supply is lost | I_off on both inputs |
| The inverter on an active-low output enable, and the logic input of each switch driver | the enable node, from a powered OR gate | I_off, or a 4.7 kΩ series resistor from the enable node |
| The output buffer or translator | the RP2350's pins and the enable node | I_off on every input on its RP2350 side, the data inputs and the enable |

The series resistor in the trigger branch sits between the junction and the
trigger input, on the monostable's side of its removable link. It bounds any
current into an unpowered input to 0.7 mA at 3.3 V, and with the 100 kΩ
pull-down behind it the high level at the input is 3.15 V, above a
74HC-class V_IH of 2.31 V at 3.3 V.

The rule is tested device by device: test step 3 removes each device's
supply alone with everything upstream of it live, and passes only with the
device's supply pin below 0.3 V and its outputs at their pull levels.

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
| Buffer disabled after the enable node falls | under 1 µs | a logic output-enable; measured in test step 12, not in the budget |
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
| Rising-edge trigger input (B on the '123-class part) | the heartbeat line, through the 4.7 kΩ series resistor | held high: the enabling level, since a falling edge on A is accepted only while B is high |
| Falling-edge trigger input (A on the '123-class part) | held low: the enabling level, since a rising edge on B is accepted only while A is low | the heartbeat line, through the same resistor |
| Clear input | the power-on reset network, shared | the same network |
| Timing network | R, C as below | R, C as below, same values |
| Q output | to the OR gate | to the OR gate |

The unused trigger input of each half is held at the level that enables
the other input, taken from the part's truth table, and not at a level
that merely does not trigger: on the '123-class part a rising edge on B is
accepted only while A is low and a falling edge on A only while B is high,
so a half whose spare input is parked at the wrong level never asserts. The
selected part's truth table governs the levels, and the wiring is checked
against it before the board is laid out.

A part with one trigger input of fixed polarity per half takes an inverter in
front of one half. Its input sees the live heartbeat while the
coprocessor's rail may be absent, so it is on the list in
[Powered-off isolation](#powered-off-isolation). The inverter's output
carries a 100 kΩ pull to the level
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
inputs to ground, and a diode across the resistor with its anode at the
clear node and its cathode at the rail. That orientation is reverse-biased
while the rail is up, so the capacitor charges through the resistor on
power-up, and conducts when the rail falls, so the capacitor empties into
the falling rail and the next ramp starts from a discharged network. The
other orientation bypasses the resistor on power-up and clear never
holds. The clear inputs are active for about 100 ms after the
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

- an active-high enable takes the node directly and carries a pull-down
  (10 kΩ) at its own enable pin, so a lead broken between the node and the
  pin reads disabled; the node's 100 kΩ pull-down is on the far side of
  such a break and does not reach it;
- an active-low enable takes the node through an inverter and carries a
  pull-up (10 kΩ) of its own, so an unpowered inverter and an undriven node
  both read as disabled. The inverter's input is driven by the enable node
  while its own supply may be absent, so it is on the list in
  [Powered-off isolation](#powered-off-isolation).

The buffer is on the same list: its data inputs are driven by the RP2350
and its enable by the OR gate while its own supply may be absent, and a
back-powered buffer's outputs are not high impedance. Test step 13 removes
the buffer's supply alone with the input toggling.

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
Open is a current, not a rail voltage: a rail drooped by 5 % on a resistive
load is a switch still passing 95 % of the current. The off limit is an
absolute leakage, the idle current of the lightest load the rail can carry,
below which the load out-draws the leak and the rail collapses: 5 mA per
rail as the placeholder until that idle current is measured (a small servo
idles at 5 to 10 mA, not measured; an ESC example idles at 50 mA).

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
| Any one logic device unpowered, everything upstream of it live: the monostable with the panel beating, the OR gate with a Q high, an inverter or a switch driver with the enable node high, the buffer with the RP2350 toggling | the [powered-off isolation](#powered-off-isolation) rule: I_off inputs or the series resistor, so the device stays silent; the pull-downs then hold its outputs' nodes |
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

Nothing below has been run. Bench mechanics are in `testbench/WIRING.md`
section 7. Logic nodes are read at 1.65 V; a delay against the enable node
has the enable node in the same trace. Each step: the property, the
quantity and probe point, the pass figure with its source. Recorded per
built unit: every figure with its instrument, the part with its k,
clear-release behaviour and I_off specification, C and R per half, the
temperature and the rail.

1. **Setting.** Each half at the set point. Measure each half's own Q from
   its own last edge (A rising, B falling), three events; R is proportional
   to the window, so the nearest E96 value is fitted and measured again.
   Pass: 167.5 ms to 172.5 ms (the ±1.5 % setting term) at 20 to 30 °C and
   3.3 V ±1 %.
2. **Static, no edges.** Every node reads disabled. Measure the trigger
   input, both Q, the enable node, the buffer's enable pin (also with its
   lead from the node lifted), a trigger-path inverter's output (fitted and
   removed), each rail with its load. Pass: logic nodes below 0.4 V; the
   inverter's output at its half's enabling level (below 0.4 V or above
   2.9 V); each rail below its load's stop voltage, or 0.5 V until measured.
   Buffer high impedance, which no voltage shows: a 4.7 kΩ pull from each
   connector line to 3.3 V, above 2.0 V with it (2.24 V against the 10 kΩ
   pull-down), below 0.4 V without. Record every voltage.
3. **Each device unpowered alone, everything upstream live.** The
   [powered-off isolation](#powered-off-isolation) rule. Measure, 60 s per
   device with its supply lead lifted, its supply pin and only the outputs
   it controls. Pass: supply pin below 0.3 V, and: the monostable (panel
   beating), both Q and the enable node below 0.4 V, no edge; a trigger-path
   inverter (panel beating), the Q of its half below 0.4 V while the other
   half and the enable node stay high; the OR gate (both Q high), the enable
   node below 0.4 V, no edge, the buffer's lines passing step 2's pull test,
   each switch control node at its open level; the enable inverter or a
   switch driver (enable node high), its own output disabled or open; the
   buffer, step 13. The whole circuit unpowered, panel silent: step 2.
4. **Unpowered board, panel beating.** A beating panel cannot power the
   board through GP3 or the trigger. Measure, 60 s, the board's rail removed
   and the load rails present: the board rail, each supply pin on the
   isolation list, GP3, both Q, the enable node. Pass: rail and supply pins
   below 0.3 V; Q and enable node below 0.4 V, no edge; rails as in step 2.
5. **Power-up.** No pulse on the enable node through a 3.3 V ramp. Measure
   the enable node for 500 ms from the rail passing 1 V: ten ramps with no
   source on the line, ten held low (the panel, closed branch, before its
   control task runs), ten held high (a generator through 4.7 kΩ, branch
   open); GP22 is on the rail being applied and holds nothing. Pass: never
   above 1.65 V in thirty records; a pulse of one window after the rail
   settles is the clear release triggering and excludes the part. Fast ramp
   only.
6. **Held.** The nominal rate never lets the enable fall. Measure the enable
   node and both Q for 60 s at an edge every 20 ms. Pass: no falling edge.
7. **The window.** 155 ms to 185 ms after the last edge of either polarity.
   Measure the last edge at the monostable's input to the enable node
   falling, the source stopped holding its final level (high for a rising
   last edge, low for a falling one; opening the link gives a falling edge
   only, because of its pull-down). Ten events, five each. Pass: the band.
8. **Each half.** Each half in the band, and step 1 holds. Measure each Q
   from its own last edge, three each. Pass: the band, and within 2.5 ms of
   170 ms after setting.
9. **The switches and the rails**, per switch. *Driver:* the control node
   against the enable node, five events; pass at its open level within 5 ms.
   *Off at the rated current:* the current through the switch by DC current
   probe on the switched-side lead, a resistive or electronic load at the
   rated figure with no capacitance (8 A at 8.4 V and 4 A at 5.5 V on the
   servo rail, [Power](Power.md); the ESC path's rating and pack voltage not
   chosen), five events per figure. Pass: below 20 mA (the clamp's floor,
   0.2 % of 8 A) within 5 ms of the enable falling, and the steady leakage
   by a meter in series at the rail voltage with the load connected below
   the off limit of [The two gates](#the-two-gates), 5 mA per rail.
   *Preliminary at 1 A:* a 100 mΩ, 2 W shunt in the switched-side return at
   the star ground (1 mV at 10 mA); pass below 10 mA within 190 ms of the
   last edge, leakage below the off limit; no substitute. *The rail,
   recorded, not passed:* with the bench's load, the time from the last edge
   to the rail leaving regulation and to the load's stop voltage (measured,
   or the 0.5 V placeholder), the record covering at least 1 s; the 470 µF,
   25 V, 50 mA example needs up to 420 ms after the last edge.
10. **The band, driven.** In the band under retriggering and above 150 ms.
    Measure the enable node at an edge every 200 ms for 100 cycles at 1 MHz
    (it falls for 200 ms minus the window), then at an edge every 150 ms for
    60 s. Pass: low pulses 15 ms to 45 ms; no falling edge at 150 ms.
11. **The corners.** Not measured. The drift table over supply and
    temperature: steps 7 and 10 at 25 °C and 3.3 V, and at 0 °C and 50 °C
    each at 3.2 V and 3.4 V. Pass: the band everywhere; the combination
    giving each extreme is compared with the datasheet's k graph.
12. **Buffer disabled, input driven.** *High impedance:* enable low, the
    bench armed and commanded away from rest under the guide's differential
    setup, step 2's pull test on the connector line; pass above 2.0 V with
    no edge at 24 MHz, below 0.4 V without. *Disable latency*, budgeted
    under 1 µs: the buffer's input driven by a generator at 300 kHz in place
    of the coprocessor's pin, left unbound, since stopping the heartbeat
    idles that pin at 150 ms; the connector line against the enable node on
    a 100 MHz scope, five events; pass: the last driven transition within
    1 µs after the enable falls and none after.
13. **Buffer supply removed, input driven.** The isolation rule on the
    buffer: its supply pin and the connector line with its supply lead
    lifted, the RP2350 toggling, the enable node high. Pass: supply pin
    below 0.3 V; the pull test as in step 12.

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
- The downstream gates are not tested at the environmental corners: test
  steps 9 and 12 run at 25 °C and 3.3 V only, and a switch driver, enable
  inverter, relay or buffer that meets 5 ms or 1 µs there may not at 0 °C
  or 50 °C or at 3.2 V or 3.4 V. Recorded as open by the owner's decision.
- The disable-latency stimulus is not phase-aligned to the enable falling:
  a buffer output that stays driven low past 1 µs but releases before the
  generator's next edge (about 1.67 µs away at 300 kHz) shows no
  transition, and repeated captures can stay phase-locked. Recorded as open
  by the owner's decision.

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
