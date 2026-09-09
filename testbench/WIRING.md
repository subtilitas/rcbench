# Wiring the bench, step by step

In order. Each step ends with a check that can be run before the next one is
made, so a mistake is found at the step that made it rather than three steps
later in a capture that reads plausibly.

`README.md` says what the bench is for and why each part is there. This says
what to connect.

**Nothing is powered while anything is being connected.** The one exception is
the Pi, which stays on for the checks and whose 3.3 V and 5 V pins are not
used for any of them.

---

## What you need

| | |
|---|---|
| Jumper leads | female-to-female for headers, and a few female-to-pin for probe tips |
| Signal relays | two per board -- one for BOOT, one for RESET -- with gold-plated contacts. A button line switches microamps and contacts rated for power grow a film a dry circuit will not break through |
| Relay driver | a transistor and a flyback diode per coil, or a relay module that has them, run from its own supply rather than the panel's rail. **Each input needs a resistor holding it at the inactive level** -- pull-down for an active-high driver, pull-up for active-low. GP16 to GP19 are inputs whenever the RP2350 is reset or reflashed, which is the recovery this guide relies on, and an undriven gate can close a BOOT or RESET contact and hold a board out of its own firmware |
| Series resistors | a few hundred ohms, one per analyser probe lead. It costs nothing and a mistake then costs a resistor |
| Two 120 Ω terminators | one at each end of the CAN pair. Two in parallel are the 60 Ω the pair should measure |
| Level translation | for any line that leaves the 3.3 V island: a translator that senses the target's rail, not a divider chosen once |
| A camera and a mount | rigid enough that it does not move between sessions; the calibration is only valid while it does not |
| The monostable | required by `docs/Safety.md`, on no board, **and not specified anywhere in this tree**. It cannot be built from this guide. What it has to do is below; what it is made of is a decision nobody has taken |

---

## 1. The star ground

Everything else depends on this, and a bad ground looks like every other
fault: edges that are not there, an I²C device that answers intermittently, a
capture full of noise.

Choose one point. The Pi's ground, the two boards' grounds, the analyser's
ground lead, the relay driver's ground, **the servo supply's return and the
ESC pack's return**, and the camera's mount if it is conductive all return to
it, each with its own lead. Not a chain from one board to the next.

The two load returns are on that list for the same reason as the rest and one
of its own: a servo or an ESC on its own supply has no reference to the pin
driving it unless the returns meet. Without that the pulse has no defined
level at the load, the output checks fail for a reason that looks like the
output, and the first scope or analyser ground lead attached becomes the
return path for a motor's current.

**Check.** With everything unpowered, measure between the star point and each
board's ground pad: under an ohm. Between the star point and any signal pad:
open. A signal pad that reads a few ohms to ground is a lead in the wrong
hole, and it is easier to find now than after power.

### Where the power comes from

Every check from here on says "powered" or "unpowered", so the supplies are
part of the wiring rather than something assumed:

| | |
|---|---|
| The Pi | its own supply |
| The RP2350 | its own USB, from a charger or a powered hub -- **not from the Pi**, and **not from the Pi's 3.3 V pin**. A cable to the Pi grounds the two a second time, through the USB shell, and the star lead in step 2 is then one of two return paths rather than the only one |
| The coprocessor | its own USB |
| The panel | its own USB-C, on the socket it is flashed from |
| Relay coils | their own supply, through the driver |
| The servo and ESC rails | their own supplies, switched by the monostable's power path and never from a board's rail |

Four separate USB supplies is the ordinary case, and the grounds meet only at
the star point above -- **except while a target board is on a data cable to
the Pi.** Step 4's loader check and section 9 need those cables, and each one
ties that board's ground to the Pi's through the shell, in parallel with its
star lead.

That is tolerable because those two are functional checks -- does the board
restart, does it enumerate -- and not measurements. **Do not take an analyser
or scope reading while a target is on a data cable to the Pi**, or use an
isolated USB adapter for it. A second return path is not visible in a capture;
it is visible as noise nobody can source.

**Unpowered means all of them.** A board still on USB while its neighbour is
being rewired is the one that finds a lead in the wrong hole the expensive
way. Take the supplies off at the point each step says so, not the ground
leads: the star is what makes the measurements above mean anything.

---

## 2. SWD, from the Pi to the RP2350

Three wires: SWCLK, SWDIO, and the ground that is already part of the star.
**No supply wire.** The Pi's 3.3 V pin is a regulated output, not a reference,
and the RP2350 has its own.

| Pi GPIO | Header pin | To the RP2350 |
|---|---|---|
| GPIO25 | 22 | SWCLK |
| GPIO24 | 18 | SWDIO |
| ground | 20 | ground, already on the star |

These are the pins Raspberry Pi's own instructions for debugging one Pi from
another use, so the wiring can be checked against a second source. Any two free
header GPIOs work -- the lines are bit-banged through `linuxgpiod`, not a
peripheral -- but the bench is reproducible only if every assembler uses the
same two, and `testbench/host/selftest.sh` reports whichever it is given.

**Check**, with the RP2350 powered from its own supply:

    gpiodetect                      # which chip carries the 40-pin header
    export SWD_GPIOCHIP=<n> SWD_SWCLK=25 SWD_SWDIO=24
    openocd -f interface/linuxgpiod.cfg -f target/rp2350.cfg \
            -c "adapter gpio swclk -chip $SWD_GPIOCHIP $SWD_SWCLK" \
            -c "adapter gpio swdio -chip $SWD_GPIOCHIP $SWD_SWDIO" \
            -c "adapter speed 1000" -c "init; exit"

The chip number stays a placeholder because it moves with the kernel and the
firmware -- it has been `gpiochip4` and it has been `gpiochip0` -- which is
what `gpiodetect` is for. The two GPIO numbers do not move.

It should find a target and exit without complaint. If it half-works --
connects sometimes, fails to verify a flash -- come down to 500 kHz before
suspecting the wiring: on this interface a clock that is too fast looks like
intermittent verification rather than an error.

---

## 3. The analyser

Its ground lead to the star point. One probe on anything that is switching --
the heartbeat is the easiest, once the boards run -- and the rest left off
until the step that uses them.

**Check, one: the instrument.**

    testbench/host/selftest.sh

It reports what it found rather than a pass. The line that matters is the
capture: an analyser that enumerates but returns nothing has no bitstream, and
that failure otherwise looks like a quiet bench.

**Check, two: the lead.** The capture above passes with the probe off, the
ground lead off, or both -- it asks the device for samples and reports that
samples arrived, and a floating input produces samples like any other. So
drive something known and require the channel to follow it.

At this point in the order the boards are not running, so use the Pi: take a
free header GPIO, drive it while the capture runs, and capture the probe that
is on it.

    while :; do
        gpioset --mode=time --usec=100000 gpiochip<n> <line>=1
        gpioset --mode=time --usec=100000 gpiochip<n> <line>=0
    done &
    pulse=$!
    testbench/host/capture.sh probe D0 1m 2m 1.65
    kill "$pulse"

**The toggle runs during the capture, not before it.** One `gpioset` that
ends before `capture.sh` starts puts the only transition outside the trace,
and the channel then reads flat whether the lead is on the pin or not --
which is the reading this check exists to rule out. The loop edges every
100 ms and the capture spans 2 s, so about twenty edges land inside it. Each
`gpioset` releases the line as it exits, so the trace has a short undriven
gap between levels; the check asks only that the level change. `kill` ends
the loop, and the `gpioset` still running exits within 100 ms on its own.
libgpiod 2 spells these options differently -- read `gpioset --help` on the
Pi before copying this.

**The channel has to change.** A trace that sits at one level says the probe
is not on the pin, or the ground lead is not on the star -- and either of
those is a bench where every later capture reads quiet and every later step is
diagnosed as the thing being measured. Repeat it for each probe as that probe
is placed, rather than once for the first one.

---

## 4. Relays across BOOT and RESET

One contact in parallel with each button, on both boards: four in all. The
contact goes across the button, not to a supply -- a button is a dry contact
to ground and the relay is only closing it.

Coils from their own supply, through their driver, controlled by the RP2350.
Coil unpowered is button not pressed, which is the state a board runs in.

Four driver inputs need four pins. The testbench firmware does not exist yet,
so the whole of this table is a proposal to fix when it is written rather than
something the tree already says. It is here because an assembler cannot wire
steps 5 and 7 without it.

| RP2350 pin | Carries | Direction |
|---|---|---|
| GP16 | panel RESET relay | out |
| GP17 | panel BOOT relay | out |
| GP18 | coprocessor RESET relay | out |
| GP19 | coprocessor BOOT relay | out |
| GP20 | touch emulator SDA, step 5 | open drain, both ways |
| GP21 | touch emulator SCL, step 5 | open drain, both ways |
| GP22 | heartbeat injection, step 7 | out |

GP20 and GP21 are an I²C0 pair on this part: the function table gives
`I2C0_SDA` on GP20 and `I2C0_SCL` on GP21. They must be **open drain with no
pull-up added here** -- the panel's bus already has its pull-ups, and a second
set changes the rise time on a bus this bench exists to measure. The emulator
is an I²C target at 0x14, not a master; the panel is the master and the real
controller stays at 0x5D.

The same two pins do double duty in step 5's second check, where the emulator
pulls one line at a time to prove which lead is on which. That is the same
open-drain drive, so it needs no extra pin.

GP22 is a plain output, and it is an input at every other moment. It drives
the heartbeat junction with an edge every 20 ms inside step 7 alone.

**The panel's link is open before it drives; the monostable's stays closed.**
The panel drives its end push-pull, so that node carries one driver at a time
or it carries contention, and opening the panel's branch is what makes room.
The monostable's branch is not in the way -- it only listens -- and it has to
stay closed so the interlock is still being fed when the capture starts.
Opening it early is what makes the window unmeasurable: step 7 takes it out
while the capture runs, so the last trigger edge and the enable falling are in
one trace. GP22 is tri-stated before either link is closed again.

The relays are at GP16 to GP19 rather than at the start of the header so that
GP0 to GP15 stay free for the analyser leads and for a stimulus generator this
guide does not yet describe.

**Before the panel half of this check**, set the panel's UART selector to
`UART1` and use the bridged USB-C socket with a cable that carries data. On
`UART2` the CH343 port still enumerates and passes nothing
(`docs/FirstRun.md`), so a correctly wired relay reads as a loader that will
not accept a download.

**Check**, one at a time, with the boards powered:

- **Pulse** RESET alone -- close it, then release it. The board restarts on
  the release, so a contact left closed holds it in reset and looks exactly
  like a relay that is not wired.
- Close BOOT, pulse RESET, release RESET, release BOOT: the board comes up in
  its loader. The coprocessor appears as a mass-storage device; the panel
  accepts a serial download.

If a board will not start at all after this step, the contact is closed when
it should be open: check the coil polarity and that the relay is not the
normally-closed contact.

---

## 5. The touch bus

Two probe leads on the panel's I²C bus, SCL and SDA, and the emulator's two
lines to the same bus -- SDA to the RP2350's GP20 and SCL to GP21, open drain,
adding no pull-up. The emulator answers at 0x14; the real controller stays at
0x5D and is not disturbed.

**Do not probe this bus with the Pi.** The panel is the master. A second
master is a fault, not a measurement.

**Check, one: the tap.** Capture SCL and SDA while the panel starts:

    testbench/host/capture.sh touch-boot D9,D10 4m 2m 1.65

Decoded as I²C, it shows the panel addressing 0x5D and the real controller
answering. That is the correct result today: the panel does not yet ask for
0x14, so this says the probes are on the right two lines and nothing about the
emulator.

**Check, two: the emulator's own leads.** The capture above passes with the
emulator unplugged, or with its two leads swapped, and the fault would then
surface only when the panel starts probing 0x14 -- one firmware change and
many steps later.

So make the emulator prove each lead, one at a time, while the panel is idle:
have it pull SDA low for a few milliseconds, then release it and pull SCL low.

    testbench/host/capture.sh touch-leads D9,D10 4m 2m 1.65

The line it was told to pull is the line that moves. If pulling SDA moves SCL,
the leads are swapped; if neither moves, the lead is not on the bus. Do this
before the panel's touch is in use -- a line held low during a transaction
costs that transaction, which is a missed touch sample and nothing worse.---

## 6. The link

**The bus first, then the probes.** CANH to CANH and CANL to CANL between the
two transceivers, with 120 Ω at both ends -- `docs/Link.md` gives the
termination and the 1 Mbit/s rate. Without the pair and its terminators a
transmitter gets no acknowledgement, retries, and goes bus-off: the capture
below would show nothing and the panel's start-up self-test would fail, and
neither would be telling you about the probes.

Measured across CANH and CANL with everything unpowered, two 120 Ω
terminators in parallel read about 60 Ω. One terminator, or none, reads 120 Ω
or open, and that is the commonest way this is built wrong.

**And the panel has to be in CAN mode to be on that bus at all.** GPIO19 and
GPIO20 carry both USB and CAN, and the FSUSB42UMX multiplexer chooses -- CH422G
EXIO5, 0 for USB and 1 for CAN. Selecting CAN takes the panel's native USB
away, which is worth knowing before it is the port a flash was going to use.
`docs/Link.md` has the detail.

Then four leads on the coprocessor's SPI to the XL2515 -- SCK on GP10 pad 14, MOSI
on GP11 pad 15, MISO on GP12 pad 16, CS on GP9 pad 12 -- and one on RXCAN
between the controller and its transceiver.

Not the controller's INT pin. `firmware/iomcu/src/xl2515.c` writes zero to
CANINTE and polls CANINTF, so INT never asserts and a probe there measures
nothing.

**Check.** With both boards running, capture the SPI probes and RXCAN
together:

    testbench/host/capture.sh link-idle D4,D5,D6,D7,D8 24m 4m 1.65

CS should be busy at the poll rate, and RXCAN should carry traffic between the
bursts. SCK, MOSI and MISO are in the capture because they are passive taps on
a link that works without them: a lead that is off or on the wrong pin changes
nothing about the traffic, and a capture of CS and RXCAN alone passes with all
three wrong. Every one of the five has to move.

24 MHz for a 10 MHz clock is the lowest rate that resolves it; at 8 MHz the
clock aliases and the decode is worse than no capture.

**If CS moves and RXCAN is flat, do not move the probe first.** A crossed pair
gives exactly this picture and the probe is already right: CANH to CANL
measures the same 60 Ω, the panel keeps polling its controller over SPI, and
the coprocessor's RXCAN sits recessive until the transmitter gives up and goes
bus-off. In order of what costs least to check:

1. **The multiplexer.** The panel is only on the bus with CH422G EXIO5 set to
   CAN. In USB mode it is not transmitting at all.
2. **Polarity.** CANH to CANH and CANL to CANL. Crossed reads 60 Ω and works
   for nobody.
3. **What the ends say.** The panel's start-up self-test gives a verdict, and
   the coprocessor's error counters say whether it has been transmitting into
   silence. A transmitter that has gone bus-off is a bus fault, not a probe
   fault.
4. **Then the probe**, which wants the logic-level pin between the controller
   and the transceiver rather than the differential pair.

---

## 7. The outputs, and the heartbeat through the monostable

GP0, GP1 and GP2 for whatever OUTPUTS binds.

**The heartbeat does not go straight from the panel to the coprocessor.** It
leaves the panel at GPIO6 on J8, drives the retriggerable monostable, and the
monostable gates the coprocessor's output enable and the servo and ESC power
path; GP3 sees the line as well so the firmware can judge it. `docs/Safety.md`
sets this out and `docs/FirstRun.md` step 3 routes the wire through it.

**The circuit does not exist yet.** `docs/Safety.md` requires it and says it
is on no board; no page in this tree gives a part, a timing network, a trigger
polarity, or the two gates. So this guide cannot be followed to build it, and
an assembler improvising one may well produce something that stays enabled
after the edges stop -- which is the failure it exists to prevent.

What it has to do, so that a circuit can be designed against it and checked:

| | |
|---|---|
| Input | edges from the panel's GPIO6 on J8, nominally one every 20 ms, which is a 40 ms cycle: `heartbeat_gen_step()` toggles the level once per period |
| Behaviour | retriggerable: asserted while edges keep arriving, deasserted no later than the window after the last one |
| Window | about 150 ms. Above the 20 ms period with margin for a late task, and below the coprocessor's 200 ms link failsafe |
| Gates | two, both downstream of the coprocessor's pins: its output enable, and the servo and ESC power path |
| Fail-safe direction | unpowered, undriven or unbuilt means disabled. A failure of the interlock cannot be a bench that keeps driving |
| Not defeatable | no firmware at either end is in the path, which is the whole point |
| Two links, both removable | one in the panel's GPIO6 branch and one in the monostable's trigger branch, meeting at a junction with the coprocessor's GP3. The differential test needs to split that node three ways, and the panel drives GPIO6 push-pull, so it cannot simply be joined |

Part numbers are deliberately absent: `hardware/README.md` says a part is not
chosen until it is available at a vendor, and this belongs in `hardware/` as
its own page rather than being improvised inside a wiring guide.

A direct wire is the failure this bench must not build. It satisfies the
firmware's heartbeat monitor, so every check in this guide would pass, and the
one thing the interlock exists for -- a panel that has crashed, wedged, reset
or browned out taking the outputs down without asking firmware at either end
-- would be absent. The window is about 150 ms, inside the coprocessor's
200 ms link failsafe.

A servo or an ESC signal line that runs at 5 V goes through the translator on
its way to anything at 3.3 V. The analyser can watch a 5 V line directly with
its threshold set for it, which is a per-run setting and an argument to
`capture.sh` rather than something left where the last run put it.

**Check, one.** The heartbeat with nothing else running:

    testbench/host/capture.sh heartbeat D3 1m 2m 1.65

An edge every 20 ms, so a 40 ms cycle: 20 ms high then 20 ms low. A gap longer
than 150 ms is what the monostable and the coprocessor both act on, and should
not be there on a healthy panel.

**Check, two: the interlock, and only the interlock.** Stopping the edges is
not a test on its own. `firmware/iomcu/src/main.c` polls the same line and
calls `outputs_off()` when it has been quiet for HEARTBEAT_MAX_GAP_MS
(150 ms) -- the same threshold, by design, since both watch the same wire. So
an output that stops when the edges stop proves nothing: a bench with no
monostable at all passes that.

Two probes settle it, and the second is the decisive one.

*The part itself.* Put leads on the monostable's own outputs and stop the
edges. Both should go inactive within the window. No firmware runs on those
nodes, so what they do is the hardware's doing.

**Two instruments, and both are needed.**

*The analyser, on logic only.* The output enable takes a lead directly. The
servo and ESC power do not: the servo rail is 8.4 V and an ESC pack is higher,
every analyser lead here is a direct connection with a series resistor, and
`capture.sh`'s threshold argument sets a comparator level rather than
attenuating anything. An LA2016 input is rated to 5 V. So the analyser takes
the **switch's control node** -- the gate or enable pin of whatever passes the
rail -- which is logic and carries the timing.

*A scope on the rail, on the same trigger.* The control node says the switch
was told to open. It does not say the rail went down: a switch that is
bypassed, miswired or failed short deasserts its gate exactly the same way.
`docs/Safety.md` requires the **power path** to be gated, so the verdict is
measured on the switched side, with a probe rated for 8.4 V or whatever the
ESC pack is.

**A meter is not enough.** It says the rail is down by the time you look,
which is a different claim from down within 150 ms: a switch with a slow gate
drive, a rail with a bulk capacitor, or a load light enough not to discharge
one, all read zero eventually and miss the deadline.

**Two channels on one scope, triggered on the rail.** The monostable's trigger
input goes on one channel and the switched rail on the other, and the scope
triggers on the **rail crossing its threshold downward**, with at least 200 ms
of pre-trigger. Triggering on a heartbeat edge cannot work: every edge is
identical and no ordinary edge trigger knows which one is the last, so the
instrument fires on an arbitrary one and the rail's fall lands outside the
record. Triggering on the rail and looking backwards puts the last trigger
edge in the same capture, and the interval between them is the number.

**Both rails, and to a voltage the load is known to stop at.** Two things make
that measurement mean something:

- *A threshold the load actually respects.* Leaving the regulation band is not
  being de-energised, and neither is the datasheet's minimum operating
  voltage: that is the bottom of *guaranteed* operation, and a servo or an ESC
  below it may go on holding, twitching or producing torque rather than
  stopping. The honest threshold is the voltage at which **this** load is
  measured to stop -- run it down on the bench and find it -- and until that
  measurement exists, a conservative near-zero figure. Nothing between the two
  is a claim about the load.
- *A load on it.* An unloaded rail with a bulk capacitor decays slowly and
  measures whatever the capacitor decides. Take it with the servo or the ESC
  connected -- the bench's own load, in the state the interlock exists for.

The servo rail and the ESC pack are separate supplies, so each has its own
switch and each is its own claim. Measuring one proves nothing about the
other: a bypassed or failed-short switch on the unmeasured rail leaves that
load powered through a check that passed. Capture both, or repeat the whole
measurement for each.

Passing the control side without the rail is the interlock's own failure mode:
the switch told to open and the power still on. Passing the rail without a
timebase is the same failure with a slower clock, and passing one rail is the
same failure on the other one.

*The differential test.* Keep the firmware happy and starve only the hardware.

The heartbeat is one node: the panel's GPIO6, the coprocessor's GP3 and the
monostable's trigger all meet on it. The panel drives its end **push-pull**
(`firmware/panel/main/main.c`), so a second driver on that node is contention
rather than a test -- a corrupted reading at best and a damaged pin at worst.

Split it before injecting anything, which is what the two links are for:

1. **Open the panel's branch.** Nothing of the panel's is driving now, so the
   next step is not contention.
2. **Have the RP2350 drive the junction from GP22** with one edge every
   20 ms -- a 40 ms cycle, not a 40 ms edge spacing and not a 20 ms cycle --
   so `heartbeat_poll()` on the coprocessor never expires. Twice that rate is
   rejected by the monitor's 4 ms floor only well beyond it, so a wrong
   reading here is injected rather than caught.

The monostable's trigger branch stays **closed** for now, and that is the
change that makes the test measurable. Both the firmware and the interlock are
being fed at this point, which is the state the run has to start from: the
enable asserted, the load side live, and edges on the trigger to measure a
window from. Opening that branch first instead leaves nothing in the trace but
an enable that is already down, and an enable that fell in 120 ms, one that
took four seconds and one that was never asserted at all are the same picture.

Firmware is being told the panel is alive, and so is the interlock -- until the
capture is running and the branch comes out.

**Bind first, then arm.** Binding is done on SETTINGS/OUTPUTS, and both bench
screens disarm as they are left (`shared/ui/motor_screen.c`,
`shared/ui/servo_screen.c`), so arming and then walking to OUTPUTS to bind
undoes the arm on the way. In order:

1. **SETTINGS/OUTPUTS**: bind the output the capture watches, which is GP0 on
   D0 here.
2. **MOTOR & ESC**, or **SERVO** for a pulse output: open it and arm there.
3. **Command it away from rest** -- throttle above zero, or the horn off
   centre. Arming is what makes the pin drive at all; a command away from rest
   is what makes the trace unambiguous, since a DShot zero-throttle frame and
   a 1500 us centre pulse are both edges and neither is distinctive.

Then capture **after the gate** -- the load-facing side of the gated output,
and the monostable's output-enable -- on whichever channels the run is not
otherwise using. The map in `testbench/README.md` gives channels 14 and 15 to
SBUS and a spare, and neither is in this run, so the two interlock leads take
them: **D14 the output-enable, D15 the load-facing side.**

Two captures, because the two questions want opposite settings.

**One: the window.** 1 MHz over 4m samples, which is 4 s:

    testbench/host/capture.sh interlock-window D0,D3,D14,D15 1m 4m 1.65

**Open the monostable's trigger branch while this one is running**, not
before it. The window is measured from the last edge into the trigger, so
that edge and the enable falling have to be in one trace, and the branch has
to come out inside the first 3.8 s for the 150 ms after it to still be in the
trace. 24 MHz over the same 4m samples covers 167 ms, which is less than the
window it would be measuring; span is what this capture needs, and 1 us
resolution on a 150 ms window is already finer than the number is worth
quoting to.

D0 aliases at 1 MHz: a DShot600 bit is 1.67 us and is sampled about twice,
so the trace shows the pin changing without being decodable. Changing is all
this capture asks of it.

**Two: the load side, once it is quiet.** The branch stays out, so the enable
stays down and the gate stays closed for as long as it is left out. That is a
steady state rather than a transient, so it is sampled on its own:

    testbench/host/capture.sh interlock-quiet D0,D15 24m 2m 1.65

24 MHz, not 1 MHz. The output under test may be DShot600, whose zero-bit high
is about 0.63 us: sampled once a microsecond, narrow activity leaking through
a failed gate falls between samples and the load side is reported quiet. The
rate has to out-sample whatever D0 is carrying, and DShot600 is the fastest
this bench binds. 2m samples at 24 MHz is 83 ms, which is 83 DShot frames at
the coprocessor's 1,000 Hz update rate and four servo frames at 50 Hz, so a
leak that repeats at all repeats inside it.

**Probe D3 on the monostable's side of that link, not on the junction.** The
channel map puts D3 at the GPIO6/GP3 node, and GP22 goes on driving that node
throughout -- a probe there keeps edging after the link comes out and gives no
last edge at all. What has to go quiet is the monostable's own input, which is
what the link disconnects. Move the lead, or put a second one there and
capture it instead.

The coprocessor keeps seeing GP22 on the junction throughout, so firmware
stays happy and D0 keeps toggling. Only the interlock is starved, which is the
whole point of the test.

Four things have to be seen, and each answers a different question:

| | Where | What it has to show |
|---|---|---|
| D0, the raw pin | both captures | **changing.** This is the precondition, not the evidence: it says the arm worked, the binding took and the pin is wired. Flat here and the test proved nothing -- a bench with no interlock at all would look identical |
| D3, the monostable's trigger input | the window capture | edges, then none. The last one is where the window starts, and without it in the trace there is no window to measure against. Past the removable link, not on the junction: the junction keeps edging from GP22 |
| D14, the enable | the window capture | deasserted, within 150 ms of that last edge |
| D15, the load side | both captures | it stops in the window capture, and the 24 MHz one is what says it is quiet rather than carrying something too narrow for 1 MHz to see |

An actively driven input, blocked downstream, is the whole of the claim.
Leaving D0 out of the capture turns a failed arm into a passing interlock
test.

D0 keeps toggling throughout, and expecting it to stop is the mistake that
reads as a failed interlock on a bench that is wired correctly. It is the
input to the gate, not the output of it.

**Putting the heartbeat back, in this order.** The test leaves GP22 driving
the junction and both links open, which is not a state to walk away from: the
first power-up in section 9 cannot acquire a heartbeat with the panel's branch
open, and closing that branch while GP22 still drives is the push-pull
contention this section opened by warning about -- the panel's GPIO6 against
the RP2350's output, at whatever levels the two happen to be on.

1. **Disarm the bench**, so nothing is driving an output while the interlock
   is about to change state.
2. **Tri-state GP22.** Not "drive it high" and not "drive it low": an input,
   off the node entirely. This is the step that makes the next two safe.
3. **Close the monostable's trigger branch.**
4. **Close the panel's branch.**

The heartbeat is one node again, with one driver on it. Section 9 expects
that, and a bench left with GP22 driving fails there with a symptom that
points at the panel.

---

## 8. The camera

Mounted so the panel fills as much of the frame as it can, square to the glass
and fixed. Nothing about the calibration survives the mount moving.

**Check.** One photograph of the splash screen. It is drawn from constants,
fills the panel and has corners, so it is what the transform is computed from.
Keep it as the session's calibration frame.

---

## 9. First power-up

In this order, and stop at the first step that does not do what it says:

1. The Pi.
2. The RP2350, and `selftest.sh` again now that everything is attached.
3. The coprocessor.
4. The panel.

The panel shows the splash, runs its CAN self-test inside it, and comes up
with `LINK` and `SAFE`. With 0.7.0 or later there should be no `FAULT` at
power-up: the coprocessor no longer counts the wait for the panel's first
request as silence.

**A `FAULT 01` here has two causes and they look identical.** Bit 0 is "the
link was silent once", it latches, and nothing clears it until an arm writes
the CLEAR register -- so the link can be back and showing `LINK` while the bit
is still displayed. It says something happened, not that something is wrong
now.

- An older **coprocessor** image, which counts the wait for the panel's first
  request as silence. Only that board can produce this one: the pre-request
  watchdog is `link_dev_tick()`, and the panel does not run `link_dev` at all.
  A panel older than 0.7.0 does not cause it, so reflashing the panel for this
  symptom changes nothing.
- A real gap after the first request. `shared/link/link_dev.c` latches the
  failsafe after 200 ms of silence, and 200 ms of silence has causes that have
  nothing to do with the build: a save to the coprocessor's flash stops that
  core long enough to lose CAN frames, and an intermittent lead or a marginal
  termination does the same.

Do not read it as a version problem without checking. Both boards' versions
are in the splash, and the coprocessor's USB console counts what actually
happened -- `requests served`, the receive-buffer overrun line, and `tx_err` /
`rx_err`. A `FAULT 01` with a healthy bus and a climbing overrun count is
frames arriving with nobody to collect them, not an old image.

---

## Photographs that would help

When the bench exists, these are the ones worth taking, and what each has to
show:

1. **The whole rig**, so the layout and the lead lengths are visible.
2. **The star ground point**, with every lead that returns to it.
3. **The Pi's header**, close enough to count pins, with the three SWD leads
   in place.
4. **The relay board**, and where each contact meets each button pad, on both
   boards.
5. **The monostable**, and both of the things it gates -- output enable and
   the servo and ESC power path -- so the path can be checked against
   `docs/Safety.md` rather than taken on trust.
6. **The panel's touch connector**, close enough to read the silkscreen, with
   the tap and the emulator's leads.
7. **The coprocessor and its transceiver**, close enough to identify RXCAN.
8. **The analyser's probe ends**, labelled, against the channel map.
9. **One frame from the camera**, showing the splash, so the calibration can
   be checked before anything depends on it.

A photograph of a connector whose silkscreen cannot be read is a photograph
that has to be taken again, so close and lit beats wide and dim.
