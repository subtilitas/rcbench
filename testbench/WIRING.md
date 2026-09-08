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
| Relay driver | a transistor and a flyback diode per coil, or a relay module that has them, run from its own supply rather than the panel's rail |
| Series resistors | a few hundred ohms, one per analyser probe lead. It costs nothing and a mistake then costs a resistor |
| Two 120 Ω terminators | one at each end of the CAN pair. Two in parallel are the 60 Ω the pair should measure |
| Level translation | for any line that leaves the 3.3 V island: a translator that senses the target's rail, not a divider chosen once |
| A camera and a mount | rigid enough that it does not move between sessions; the calibration is only valid while it does not |
| The monostable | retriggerable, about a 150 ms window, gating the coprocessor's output enable and the servo and ESC power path. It is on no board and it is not optional: see `docs/Safety.md` |

---

## 1. The star ground

Everything else depends on this, and a bad ground looks like every other
fault: edges that are not there, an I²C device that answers intermittently, a
capture full of noise.

Choose one point. The Pi's ground, the two boards' grounds, the analyser's
ground lead, the relay driver's ground and the camera's mount if it is
conductive all return to it, each with its own lead. Not a chain from one
board to the next.

**Check.** With everything unpowered, measure between the star point and each
board's ground pad: under an ohm. Between the star point and any signal pad:
open. A signal pad that reads a few ohms to ground is a lead in the wrong
hole, and it is easier to find now than after power.

---

## 2. SWD, from the Pi to the RP2350

Three wires: SWCLK, SWDIO, and the ground that is already part of the star.
**No supply wire.** The Pi's 3.3 V pin is a regulated output, not a reference,
and the RP2350 has its own.

**Check**, with the RP2350 powered from its own supply:

    gpiodetect                      # which chip carries the 40-pin header
    export SWD_GPIOCHIP=<n> SWD_SWCLK=<pin> SWD_SWDIO=<pin>
    openocd -f interface/linuxgpiod.cfg -f target/rp2350.cfg \
            -c "adapter gpio swclk -chip $SWD_GPIOCHIP $SWD_SWCLK" \
            -c "adapter gpio swdio -chip $SWD_GPIOCHIP $SWD_SWDIO" \
            -c "adapter speed 1000" -c "init; exit"

It should find a target and exit without complaint. If it half-works --
connects sometimes, fails to verify a flash -- come down to 500 kHz before
suspecting the wiring: on this interface a clock that is too fast looks like
intermittent verification rather than an error.

---

## 3. The analyser

Its ground lead to the star point. One probe on anything that is switching --
the heartbeat is the easiest, once the boards run -- and the rest left off
until the step that uses them.

**Check.**

    testbench/host/selftest.sh

It reports what it found rather than a pass. The line that matters is the
capture: an analyser that enumerates but returns nothing has no bitstream,
and that failure otherwise looks like a quiet bench.

---

## 4. Relays across BOOT and RESET

One contact in parallel with each button, on both boards: four in all. The
contact goes across the button, not to a supply -- a button is a dry contact
to ground and the relay is only closing it.

Coils from their own supply, through their driver, controlled by the RP2350.
Coil unpowered is button not pressed, which is the state a board runs in.

**Check**, one at a time, with the boards powered:

- Close RESET alone: the board restarts. On the panel the splash comes back.
- Close BOOT, pulse RESET, release RESET, release BOOT: the board comes up in
  its loader. The coprocessor appears as a mass-storage device; the panel
  accepts a serial download.

If a board will not start at all after this step, the contact is closed when
it should be open: check the coil polarity and that the relay is not the
normally-closed contact.

---

## 5. The touch bus

Two probe leads on the panel's I²C bus, SCL and SDA, and the emulator's two
lines to the same bus. The emulator answers at 0x14; the real controller stays
at 0x5D and is not disturbed.

**Do not probe this bus with the Pi.** The panel is the master. A second
master is a fault, not a measurement.

**Check.** Capture SCL and SDA while the panel starts:

    testbench/host/capture.sh touch-boot D9,D10 4m 2m 1.65

Decoded as I²C, it shows the panel addressing 0x5D and the real controller
answering. That is the correct result today: the panel does not yet ask for
0x14, and until that firmware change exists the emulator is on the bus and
silent. Seeing 0x5D is how you know the tap is right.

---

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

**Check.** With both boards running, capture CS and RXCAN:

    testbench/host/capture.sh link-idle D7,D8 8m 4m 1.65

CS should be busy at the poll rate. RXCAN should carry traffic between the
bursts. If CS moves and RXCAN is flat, the probe is on the wrong side of the
transceiver -- it wants the logic-level pin, not the differential pair.

---

## 7. The outputs, and the heartbeat through the monostable

GP0, GP1 and GP2 for whatever OUTPUTS binds.

**The heartbeat does not go straight from the panel to the coprocessor.** It
leaves the panel at GPIO6 on J8, drives the retriggerable monostable, and the
monostable gates the coprocessor's output enable and the servo and ESC power
path; GP3 sees the line as well so the firmware can judge it. `docs/Safety.md`
sets this out and `docs/FirstRun.md` step 3 routes the wire through it.

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

A 20 ms square wave. A gap longer than 150 ms is what the monostable and the
coprocessor both act on, and should not be there on a healthy panel.

**Check, two: the interlock, and only the interlock.** Stopping the edges is
not a test on its own. `firmware/iomcu/src/main.c` polls the same line and
calls `outputs_off()` when it has been quiet for HEARTBEAT_MAX_GAP_MS
(150 ms) -- the same threshold, by design, since both watch the same wire. So
an output that stops when the edges stop proves nothing: a bench with no
monostable at all passes that.

Two probes settle it, and the second is the decisive one.

*The part itself.* Put leads on the monostable's own outputs -- the
coprocessor's output enable, and the switched servo and ESC power -- and stop
the edges. Both should go inactive within the window. No firmware runs on
those nodes, so what they do is the hardware's doing.

*The differential test.* Keep the firmware happy and starve only the hardware:
have the RP2350 drive GP3 with a clean 20 ms square so `heartbeat_poll()`
never expires, while the monostable's input gets nothing. Arm, bind an output,
and capture it.

    testbench/host/capture.sh interlock D0,D3 1m 2m 1.65

The firmware has every reason to keep driving, and the output must stop
anyway. If it keeps going, the monostable is not in the path or is not gating
what it should, and the bench is a direct wire wearing a part number. If it
stops, the only thing that could have stopped it is the part.

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
request as silence. A `FAULT 01` here means an older build on one of the two
boards.

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
