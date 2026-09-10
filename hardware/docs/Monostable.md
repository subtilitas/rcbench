# The monostable

The hardware backstop behind the heartbeat. A retriggerable monostable IC
(integrated circuit) with an RC (resistor-capacitor) timing network is
triggered by every edge on the panel's safety line and holds an enable node
high only while edges keep arriving. The enable node gates the coprocessor's
output enable and the servo and ESC (electronic speed controller) power path.
When the edges stop, the outputs are removed within 200 ms with no firmware at
either end in the path. [Safety](../../docs/Safety.md) says why the line
carries edges and what this circuit covers; this page says what the circuit
has to be so it can be designed and checked.

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
| Window | no shorter than 155 ms and no longer than 200 ms at every corner of component tolerance and temperature |
| Output | one enable node, high while retriggered, low otherwise, feeding both gates |
| Gates | the output enable of the buffer between the RP2350 and the output connector, and a high-side switch on each of the servo rail and the ESC pack |
| Fail-safe direction | unpowered, undriven, unbuilt, or an open trigger line means both gates disabled |
| Not defeatable | no programmable element in the path from GPIO6 to either gate |
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

## Timing budget

| Item | Value | Where it comes from |
| --- | ---: | --- |
| Control-task period on the panel | 5 ms | `firmware/panel/main/main.c` |
| Edge interval, nominal | 20 ms | `HEARTBEAT_PERIOD_MS` |
| Edge interval seen by a single-edge trigger | 40 ms | the level toggles once per period |
| Longest interval the firmware accepts | 150 ms | `HEARTBEAT_MAX_GAP_MS` |
| Margin for one late control-task pass | 5 ms | one pass of the task that emits the edge |
| Window, lower bound at every corner | 155 ms | 150 ms + 5 ms: the monostable never drops out on a heartbeat the firmware accepts |
| Window, upper bound at every corner | 200 ms | the coprocessor's link failsafe; the backstop is not slower than the link |
| Window, nominal in the worked example | 176 ms | [Timing network](#timing-network) |
| Coprocessor link failsafe | 200 ms | silence on the CAN (Controller Area Network) link |

The order of events after the last edge at t = 0, with a healthy coprocessor:
the firmware monitor disarms at 150 ms; the enable node falls between 157 ms
and 197 ms in the worked example; the link failsafe fires at 200 ms after the
last frame. With a coprocessor that cannot act, the second event is the only
one, and the outputs are still gone before 200 ms.

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
| Rising-edge trigger input | the heartbeat line | held at its inactive level |
| Falling-edge trigger input | held at its inactive level | the heartbeat line |
| Clear input | inactive | inactive |
| Timing network | R, C as below | R, C as below, same values |
| Q output | to the OR gate | to the OR gate |

A part with one trigger input of fixed polarity per half takes an inverter in
front of one half; the inverter is in the trigger path and, unpowered, leaves
that half untriggered, which is the safe direction.

The OR gate's output is the enable node. It is a push-pull logic output, so
the enable node's pull-down acts only when the gate is unpowered or the node
is unbuilt. A diode-OR into the pull-down is the alternative combining
element; it is not the design because a Schottky drop of 0.3 V leaves 3.0 V
on the node, and a 74HC-class input needs 2.3 V at 3.3 V, so the margin is
spent on the diode.

Why the OR of two windows behaves like one window triggered on both edges:
the enable node falls when both halves have expired. The half retriggered by
the last edge expires a window after it; the other half was retriggered one
interval earlier and expires sooner. So the enable falls one window after the
last edge of either polarity, and a gap between any two consecutive edges is
covered as long as it is shorter than the window.

A half that has failed silent (an open trigger input, a dead output) makes
the surviving half a single-edge trigger: it sees 40 ms between its edges and
still holds the node up on a nominal heartbeat, and it drops out whenever two
consecutive intervals together exceed the window, which is the safe
direction. The test procedure
measures each half on its own for that reason.

## Timing network

A retriggerable monostable's window is

    t = k * R * C

where R and C are the external timing network and k is a constant of the
part's family, given in its datasheet, together with a graph of how it moves
with supply voltage and temperature. Values of k in common families lie
between about 0.3 and 1.0, so R and C cannot be chosen before the part is.

**Assumed for the worked example: k = 0.45 at 3.3 V and 25 °C, with a spread
of ±5 % over supply and temperature.** The value is what the widespread
74HC-class dual retriggerable monostable with paired trigger inputs quotes;
the spread is an assumption, since datasheets of this class graph k against
supply rather than stating a limit. Both are replaced from the chosen part's
datasheet before the values are final, and the corner table is recomputed.

Worked values for the band:

| | Value | Tolerance | Notes |
| --- | ---: | ---: | --- |
| R | 392 kΩ | ±1 %, ±100 ppm/°C | E96 metal film. The E24 neighbour 390 kΩ gives 175.5 ms nominal and meets the lower bound by 0.5 ms; not preferred |
| C | 1 µF | ±5 %, ±30 ppm/°C | C0G/NP0 ceramic or polypropylene film. Not X7R, X5R or electrolytic: ±15 % over temperature, a voltage coefficient and leakage that eats the timing current |
| k | 0.45 | ±5 % | assumed; from the datasheet |
| t, nominal | 176.4 ms | | 0.45 × 392 kΩ × 1 µF |

Corner analysis over 0 to 50 °C ambient (±25 °C from 25 °C, the range the
bench is assumed to be used in; not analysed outside it):

| Corner | R | C | k | t | Margin |
| --- | ---: | ---: | ---: | ---: | ---: |
| Minimum | 387.1 kΩ (−1 % −0.25 %) | 0.949 µF (−5 % −0.08 %) | 0.4275 | 157.0 ms | 2.0 ms above 155 ms |
| Nominal | 392 kΩ | 1.000 µF | 0.45 | 176.4 ms | |
| Maximum | 396.9 kΩ (+1 % +0.25 %) | 1.051 µF (+5 % +0.08 %) | 0.4725 | 197.1 ms | 2.9 ms below 200 ms |

The corners are products of the three worst cases, which is the exact worst
case for a product of independent terms. The margins are 2.0 ms and 2.9 ms
with the assumed k spread; a datasheet k spread wider than ±5 % or a
capacitor looser than ±5 % breaks the band and calls for a ±2 % capacitor or
a capacitor measured before fitting. Propagation delay from trigger edge to Q
and through the OR gate is below 1 µs and is not in the budget.

Rules for the split between R and C, whatever the part:

- R stays inside the part's permitted external resistor range, and between
  100 kΩ and 1 MΩ, so the timing current at 3.3 V is 3 to 33 µA, large
  against a timing pin's input leakage of up to 1 µA at the top of the
  temperature range. For k = 0.45 and a 176 ms window that puts C between
  0.39 µF and 3.9 µF.
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

**Power path.** A high-side switch on each rail that reaches a load: the
servo rail (up to 8.4 V, 4 to 8 A, [Power](Power.md)) and the ESC pack. Each
switch has a control input driven from the enable node through whatever level
shift or gate driver the switch needs. Enabled means the switch is closed and
the rail is present at the connector. Disabled means the switch is open. The
switch is open when its control input is undriven and when the 3.3 V logic
supply is absent: an enhancement-mode MOSFET (metal-oxide-semiconductor
field-effect transistor) with a gate-to-source resistor satisfies that, as
does a normally-open relay; a normally-closed contact or a depletion-mode
device does not.

The rail has to fall to the voltage its load stops at within the window, with
the load connected; the bulk capacitance on the switched side sets how fast
it falls and is part of the design. `testbench/WIRING.md` section 7 gives the
measurement and why a meter cannot make it.

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
| Monostable unpowered | its Q outputs are undriven; the OR gate is unpowered; a 100 kΩ pull-down on the enable node holds it low |
| Enable node unbuilt or a lead off | the same pull-down |
| Output enable undriven | the pull-down (active-high enable) or the pull-up (active-low enable) at the device's pin |
| Switch control undriven or logic supply absent | the gate-to-source resistor or the relay coil |
| Power-up | Q is low until the first trigger; the firmware side additionally needs four good intervals before it arms |

The RP2350 pulls GP3 down in firmware (`firmware/iomcu/src/main.c`). That
pull-down is not in this circuit's fail-safe list: it is set by firmware,
and this circuit is the thing that must hold when firmware does not.

## Not defeatable

The path from J8 to either gate is GPIO6, the trigger inputs, the timing
networks, the Q outputs, the OR gate, the enable node, and the gates. No
processor, no register and no firmware is in it. The coprocessor's GP3 is an
input on the same node: it listens so the firmware can judge the line, and it
cannot remove the enable or add to it.

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
are in one trace. The trigger pull-down sits on the monostable's side of its
link, so an open link is a low trigger and not a floating one.

## With the firmware monitor

The coprocessor checks the same wire in firmware because the monostable
cannot tell a heartbeat from noise: a line that rings, is shorted to a clock,
or is driven by something other than the panel edges fast, and every edge
retriggers. The firmware rejects intervals under 4 ms and over 150 ms and
needs four good intervals before it trusts the line. The monostable covers
the case the firmware cannot: a coprocessor that is wedged or not servicing
its monitor while the panel has stopped. Both exist for those two reasons,
and neither replaces the other.

The monostable does not latch. When edges resume, the enable node returns
within one propagation delay of the first edge. What latches is the
coprocessor's arm: the firmware monitor disarms on the gap, and nothing
re-arms without an operator. A coprocessor wedged with its outputs driving,
beside a panel that stops and then resumes beating, has its outputs
re-enabled when the edges return. A latch that needs an operator action to
release is not part of this design.

## Test procedure

Nothing below has been run; every result is "not measured" until it is.
Instruments: a two-channel oscilloscope with at least 200 ms of pre-trigger
record, the analyser and `capture.sh` from `testbench/`, and a probe rated for
the ESC pack voltage. The captures of `testbench/WIRING.md` section 7 apply;
what follows is what to measure on this circuit and what passes.

**1. Before power.** Measure R and C with a meter before fitting and record
them. Compute the expected window from the datasheet's k.

**2. Static, no edges.** Panel's branch open, GP22 tri-stated. Record the
voltage at the trigger input, each Q output, the enable node, the buffer's
enable pin, and each switched rail with its load connected. Pass: trigger
input, Q outputs and enable node below 0.4 V; the buffer's outputs high
impedance (each connector signal line at its pull-down level, below 0.4 V);
each rail below the voltage its load is measured to stop at, or below 0.5 V
until that voltage is measured.

**3. Static, unpowered.** Repeat step 2 with the 3.3 V supply to the
monostable and the OR gate removed and the rails' supplies present. Pass: the
same figures.

**4. Held, edges arriving.** Junction driven at one edge every 20 ms (GP22 per
`testbench/WIRING.md`, or a 3.3 V square wave at 25 Hz, panel's branch
open). Capture the enable node for 60 s. Pass: no falling edge on the
enable node in 60 s; each Q output shows no gap.

**5. The window, on the scope.** Trigger input on channel 1, probed on the
monostable's side of its link. Enable node on channel 2. Scope triggered on
channel 2 falling through 1.65 V, 200 ms or more of pre-trigger. Open the
monostable's link. Read the interval from the last edge on channel 1 to the
crossing on channel 2. Repeat ten times: five with a rising last edge, five
with a falling last edge (the link opens at an arbitrary phase, so sort the
captures by what channel 1 shows and keep going until each polarity has
five). Record every interval, the ambient temperature and the supply
voltage. Pass: every interval between 155 ms and 200 ms.

**6. Each half.** Repeat step 5 with channel 2 on each Q output in turn,
three captures each. Pass: each half's interval from its own last edge
between 155 ms and 200 ms. Record the difference between the halves; two
capacitors at opposite ends of ±5 % put it at up to 18 ms.

**7. The rails.** Repeat step 5 with channel 2 on each switched rail in turn,
the scope triggered on the rail falling through the load's stop voltage, the
load connected. Pass: each rail crosses its stop voltage within 200 ms of the
last edge. Record the voltage used as the threshold and whether it is
measured or the 0.5 V placeholder.

**8. The band, driven.** Junction driven at one edge every 210 ms from the
generator. The enable node then falls for 210 ms minus the window on every
cycle. Capture 100 cycles with the analyser at 1 MHz. Pass: every low pulse
between 10 ms and 55 ms, which is a window between 155 ms and 200 ms; record
the minimum and maximum. Then one edge every 150 ms for 60 s. Pass: no
falling edge on the enable node.

**9. Temperature.** Not measured. Steps 5 and 8 at 0 °C and 50 °C on the
timing network would confirm the corner table; until then the corners are
calculated, not measured.

What to record, per built unit: the part and its datasheet k, the measured R
and C, the ten intervals of step 5, the six of step 6, the two rail figures
of step 7, the minimum and maximum of step 8, ambient temperature and supply
voltage. A unit whose step 5 minimum is below 160 ms with the assumed k has
either a k below the assumption or a capacitor at the bottom of its band, and
R goes up one E96 step (+2.4 %, about 4 ms) with the analysis redone.

## Limitations

- The monostable does not latch; see
  [With the firmware monitor](#with-the-firmware-monitor).
- A healthy panel beside a misbehaving coprocessor is not covered, by this
  circuit or by any hardware in the design.
- Bidirectional DShot answers on the same wire it is driven on, so the
  element carrying the output enable has to pass the reply direction while
  enabled. Which element does that is not specified.
- The window's lower margin is 2.0 ms at the calculated corner. It rests on
  the assumed k spread.

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
- The bulk capacitance on each switched rail, which sets how fast the rail
  falls once the switch opens.
