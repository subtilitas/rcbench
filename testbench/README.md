# The measurement bench

A Raspberry Pi 5, a Kingst LA2016 logic analyser and an RP2350 board, wired so
that this project's protocol claims can be measured instead of asserted, and
driven over SSH (secure shell) so a measurement can be repeated without
anybody holding a probe.

Everything in `firmware/` is outside the host test suite, and every bit timing
in the tree was written from a specification and exercised only against frames
the same code builds. `docs/FirstRun.md` section 7 is the list of numbers
nobody has measured. This bench is what answers it.

**Nothing here has been run.** The hardware is being assembled; the pin map
below is a proposal to be checked against the wiring, and the two open
questions at the end need answering before any of it is true.

---

## What it is for

Six things the tree does that only an instrument can confirm:

| | What is claimed | What has to be seen |
|---|---|---|
| DShot | 300 and 600 kbit/s, bit 0 is 37% high and bit 1 is 75% | the actual high time of each bit, against an ESC's tolerance rather than the specification |
| Bidirectional DShot | the ESC replies at five quarters of the wire rate after a turnaround | the turnaround delay, and whether 30 µs is what a real ESC waits |
| PPM (pulse position modulation) | 8 channels in a 25,000 µs frame at 40 Hz, direct memory access ring | the frame geometry with the ring running, and the gap between repeats |
| Servo PWM (pulse-width modulation) | 50 Hz, 500 to 2500 µs, 1 µs resolution | the frame period, the pulse at both ends of travel, and the jitter |
| SBUS | inverted serial, 100 kbit/s, 25 bytes | the framing, and what the panel makes of a receiver in failsafe |
| OpenYGE | telemetry frames and parameter access | the frame timing and the reply latency of a real ESC |

And the programmer's four ESC protocols and one servo protocol, which the
panel can drive but which no test exercises against a device: BLHeli_S and
AM32 (one-wire bootloader, 19,200 baud), ESCape32 (text command line over the
signal line), VESC (framed packets, 115,200 baud) and Hitec D-series.

---

## The parts

**Raspberry Pi 5** — the host. It runs `sigrok-cli` for capture, OpenOCD for
flashing, and the run scripts in `host/`. It is the only thing on this bench
with a network address, and it is what an agent drives.

**Kingst LA2016** — 16 channels, passive. Supported by libsigrok's
`kingst-la2016` driver, which needs the field-programmable gate array (FPGA)
bitstream extracted from the vendor's software: that extraction is a setup
step and is the first thing to confirm, because without it the analyser
enumerates and captures nothing.

**RP2350 board** — the same part as the bench coprocessor, in one of two roles
per run:

- **Stimulus.** Its programmable input/output (PIO) blocks generate protocol
  traffic that is known exactly, because the generator states it. A capture of
  known traffic is what tells you whether a decoder is right, which has to
  come before any capture of the bench itself is worth reading.
- **Responder.** It pretends to be the device at the other end — an ESC that
  answers a BLHeli_S bootloader handshake, or one that emits OpenYGE
  telemetry, or a servo that answers Hitec D-series. That makes the panel's
  programmer and telemetry paths testable without owning every device.

**The bench itself** — panel, display, touch and the CAN (controller area
network) controller — lives on the rig rather than being attached per session.
That is what makes a regression run possible: the same recipes over the same
wiring after every merge, rather than a measurement somebody remembered to
take.

Having the display and the CAN controller wired changes what can be measured,
in three ways:

- **The panel runs as it ships.** The renderer is drawing while the control
  task arms, stops and drives, which is the condition the frame budget and the
  PSRAM (pseudo-static random-access memory) bandwidth concern in `STATUS.md`
  are about. A timing measured with the screen dark is not the bench's timing.
- **The link is observable at both ends.** Four probes on the XL2515's serial
  peripheral interface (SPI) show every register the driver touches, and a
  fifth on RXCAN between the controller and the transceiver shows the bit
  stream itself. Both are needed: the SPI says when the driver noticed a
  frame, RXCAN says when it arrived, and the difference between those two is
  the polling interval. The controller's INT pin is not a probe point --
  `firmware/iomcu/src/xl2515.c` writes zero to CANINTE and polls CANINTF, so
  INT never asserts. With RXCAN, the 1,000 ms host timeout and the 200 ms
  silence watchdog stop being numbers only the code believes.
- **Touch becomes something a script can do.** See below; it is the one that
  matters most.

---

## Wiring

One star ground. Every ground in the table below returns to the same point,
and the analyser's ground is part of that star rather than a daisy chain from
the far end of it.

Proposed channel map, to be checked against the wiring as built:

| Channel | Signal | Source | Note |
|---|---|---|---|
| 0 | GP0 output | coprocessor | whatever OUTPUTS binds first |
| 1 | GP1 output | coprocessor | |
| 2 | GP2 output | coprocessor | the servo screen's own pin |
| 3 | heartbeat | panel J8 / GPIO6 to coprocessor GP3 | 20 ms square, the safety line |
| 4 | CAN SCK | coprocessor GP10, pad 14 | the XL2515's SPI clock |
| 5 | CAN MOSI | coprocessor GP11, pad 15 | |
| 6 | CAN MISO | coprocessor GP12, pad 16 | with SCK and MOSI, every register the driver writes |
| 7 | CAN CS | coprocessor GP9, pad 12 | frames the transaction, and is the cheapest thing to trigger on |
| 8 | RXCAN | XL2515 to transceiver | the bit stream itself, which is when a frame actually arrived |
| 9 | touch SCL | panel to GT911 | the touch controller's clock |
| 10 | touch SDA | panel to GT911 | with SCL, what the panel believes a finger did |
| 11 | DShot reply | ESC to coprocessor | the same wire as channel 0 when bidirectional; kept separate so a probe can sit on the ESC end |
| 12 | programmer line | one-wire, 19,200 baud | BLHeli_S, AM32, ESCape32 |
| 13 | telemetry RX | ESC to bench | OpenYGE, and DShot extended telemetry |
| 14 | SBUS | receiver to bench | inverted; the analyser reads it as it is and the decoder inverts |
| 15 | TXCAN, or spare | XL2515 to transceiver | the other direction when a run wants both ends of an exchange |

**The budget is 16 channels and the map is 15**, so a run takes the subset it
needs rather than everything at once: an output measurement wants channels 0
to 3, a link measurement wants 4 to 8, and only a failure that crosses the two
wants both.

**Levels.** The RP2350 runs at 3.3 V and its pins are not 5 V tolerant.
Servo and ESC signal lines are commonly 5 V, and a one-wire programming line
idles high through a pull-up at whatever the device runs at. Anything crossing
between the 3.3 V island and a 5 V device goes through a level shifter, and
every probe point gets a series resistor so a mistake costs a resistor. The
analyser's threshold is set per run rather than left where the last run put
it.

---

## Safety

- **No propeller, ever.** A motor on this bench runs bare.
- The bench supply has a current limit set low enough that a short trips it
  rather than burning a track.
- Power to the device under test goes through a relay the Pi can open, so a
  run that has gone wrong ends without a person in the room.
- The analyser is passive and stays passive: it observes, and nothing on it
  drives a line.

---

## Flashing without hands

The RP2350's BOOTSEL button is not available to an agent, so the board is
flashed over SWD (serial wire debug) rather than by unplugging it, driven from
the Pi's own pins through OpenOCD's `linuxgpiod` interface.

On a Raspberry Pi 5 the general-purpose input/output pins sit behind the RP1
controller, so OpenOCD's older native Broadcom driver does not work and the
character device is the route. Which device the 40-pin header is depends on
the kernel and the firmware -- it has been `gpiochip4` and it has been
`gpiochip0` -- so it is read from `gpiodetect` rather than assumed:

    gpiodetect                       # which chip carries the header
    export SWD_GPIOCHIP=<n> SWD_SWCLK=<pin> SWD_SWDIO=<pin>
    openocd -f interface/linuxgpiod.cfg -f target/rp2350.cfg \
            -c "adapter gpio swclk -chip $SWD_GPIOCHIP $SWD_SWCLK" \
            -c "adapter gpio swdio -chip $SWD_GPIOCHIP $SWD_SWDIO" \
            -c "adapter speed 1000"

`host/selftest.sh` reads the same three variables, so the wiring is stated
once and nothing in the scripts has to know it.

Three wires and a ground: SWCLK, SWDIO, and the target's 3.3 V as a reference
only. Start at 1,000 kHz and come down if a flash fails to verify; a bad clock
on this interface looks like intermittent verification rather than a clean
error.

The same applies to the bench's own coprocessor if it is to be reflashed
between runs.

---

## Touch, which is the one that matters

Fifty-one review findings on one branch were about arming, and every one was
argued rather than measured, because the panel's control task is outside the
host suite and a gesture needs a finger. The cases were: a press held for two
seconds, a finger that slides off the button, a second contact landing on it,
a stop pressed while a hold is running, and touch that stops answering
altogether. All of them are decided by what the GT911 touch controller reports
over I²C (inter-integrated circuit).

**So the RP2350 pretends to be the GT911**, and the real controller has to be
out of the way while it does.

Two devices at one address both acknowledge, and their open-drain pulls
combine rather than one replacing the other: what the panel would read is
neither. The panel also probes the physical part at start-up --
`firmware/panel/components/gt911/touch.c` runs `board_touch_reset_sequence()`
and then `gt911_new()`, which tries 0x5D and then 0x14 -- so a controller that
is merely ignored still answers that probe.

The wiring therefore has to isolate it. In order of preference:

1. **Hold the real GT911 in reset** while the emulator is active, with its
   reset line driven from the Pi. This is the cheapest and it is reversible
   between runs, but it needs checking that a GT911 in reset releases the bus
   rather than holding a line down.
2. **Break the bus** with an analogue switch on SDA and SCL, so the panel sees
   exactly one device and which one is a Pi output.
3. **Two panels**, one wired for measurement and one whole. The most work, and
   the only one that leaves a bench nobody has modified.

Until one of those is built, the emulator cannot inject anything, and this
section is a plan rather than a capability.

With it built, a recipe says "press at (612, 396), hold 2.1 s, release" and
the panel cannot tell the difference. With the
analyser on the heartbeat and the output pin at the same time, one capture
holds the gesture and what the bench did about it:

| Gesture the recipe drives | What the capture has to show |
|---|---|
| hold 2.1 s on ARM | the output starts driving, and not before 2.0 s |
| hold 1.9 s and release | nothing drives |
| hold, slide off the button, hold | nothing drives |
| second contact lands, first leaves | nothing drives |
| STOP while a hold is running | the heartbeat stops being asserted within 150 ms |
| the emulator stops answering | the bank comes down, and the far end fails safe |

That last one is the case that took four review rounds to get right and still
has no test. Here it is a wire going quiet on purpose.

It also gives the failure that cannot be staged any other way: a touch
controller that answers slowly, or that reports a contact that was never
there. Both are things a real GT911 does when its ground is poor, and neither
has ever been in front of this firmware.

---

## How a run works

A run is a recipe, a capture, a decode and a verdict, in that order, and it
leaves an artefact behind.

1. **Recipe** — a file that says what the stimulus does, what to capture
   (channels, sample rate, trigger, duration), which decoder to run, and what
   the assertion is. It is committed; the numbers it produces are not asserted
   by hand anywhere else.
2. **Capture** — `sigrok-cli` writes a `.sr` file into `captures/`. Captures
   are large and are not committed, except a small reference set that the
   decoders are tested against.
3. **Decode** — a protocol decoder turns the capture into rows: one per bit,
   frame or pulse, with times in microseconds.
4. **Verdict** — the recipe's assertion, printed as a number and a unit, not
   as a pass. "37.2% high at 300 kbit/s, tolerance ±5%" is a result; "OK" is
   not.

The artefact is the capture plus the decoded rows plus the verdict. A claim in
`docs/` that this bench has measured says which run measured it.

---

## Trusting a decoder before trusting a measurement

sigrok has no DShot decoder. One has to be written, and a decoder written from
the same specification as the firmware would agree with the firmware for the
same wrong reason.

The first check is a round trip against the tree's own builder:
`shared/dshot/dshot_frame.c` makes a frame from a value and a cyclic
redundancy check, the host suite emits it as a waveform, and the decoder must
read back what went in. It needs no hardware and can be done before the bench
exists.

**That check is necessary and it is not sufficient.** The production path
calls the same builder -- `firmware/iomcu/src/out_dshot.c` -- so decoder and
firmware can agree on a bit order or a checksum convention that is wrong in
the same way, and the round trip would pass. It proves transcription, not
convention.

Convention needs a source outside this tree, and there are three, in order of
what they cost:

1. **Published vectors** — a throttle value and the frame it must produce,
   from the protocol's own documentation rather than from an implementation.
2. **A device that is not ours.** An ESC that spins at the commanded throttle
   is evidence the frames are right, because it was written by somebody who
   never read this code. A reply decoded to an electrical revolutions figure
   that tracks a separately measured shaft speed is the same evidence for the
   reply direction.
3. **A capture of another implementation** driving the same ESC, decoded by
   this decoder and compared.

Until one of those has been done, a capture of the coprocessor says the
firmware and the decoder agree, and no more than that.

---

## What is here

    testbench/
      README.md      this
      host/          the scripts the Pi runs: capture, decode, self-test
      decoders/      protocol decoders, and their offline verification
      captures/      run artefacts, not committed

---

## What is not decided

**Does the Pi have the LA2016's field-programmable gate array bitstream?**
libsigrok's driver needs it extracted from the vendor's software, and without
it the analyser enumerates and captures nothing — which looks like a quiet
bench rather than a broken one. `host/selftest.sh` asks that question
specifically. If the extraction is a problem, the alternative is a different
analyser, and it is better to know before the wiring is made.

Settled: SWD over `linuxgpiod`, and the panel, display, touch and CAN
controller live on the rig rather than being attached per session.
