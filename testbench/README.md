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

**The bench itself** — panel and coprocessor — attaches as the device under
test when the measurement is about what this project emits rather than about
what it reads.

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
| 4 | CAN TX | panel | link traffic, for timing the exchanges |
| 5 | CAN RX | panel | |
| 6 | DShot reply | ESC to coprocessor | the same wire as channel 0 when bidirectional; kept separate so a probe can sit on the ESC end |
| 7 | programmer line | one-wire, 19,200 baud | BLHeli_S, AM32, ESCape32 |
| 8 | telemetry RX | ESC to bench | OpenYGE, and DShot extended telemetry |
| 9 | SBUS | receiver to bench | inverted; the analyser reads it as it is and the decoder inverts |
| 10 | RP2350 stimulus A | stimulus board | known-good reference traffic |
| 11 | RP2350 stimulus B | stimulus board | |
| 12–15 | spare | | |

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
flashed over SWD (serial wire debug) rather than by unplugging it. On a
Raspberry Pi 5 the general-purpose input/output pins sit behind the RP1
controller, so OpenOCD's older native Broadcom driver does not work; the two
routes that do are `linuxgpiod`, or a Raspberry Pi Debug Probe on USB
(universal serial bus). The Debug Probe is the one to prefer: it is the same
interface whatever the host, and it leaves the Pi's pins for the star ground
and the relay.

The same applies to the bench's own coprocessor if it is to be reflashed
between runs.

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

So the decoder is checked against traffic whose content is known
independently: `shared/dshot/dshot_frame.c` builds a frame from a value and a
cyclic redundancy check, the host suite can emit that frame as a waveform, and
the decoder must read back the value and the check that went in. That
verification needs no hardware and can be done before the bench exists.

Only then is a capture of the real coprocessor worth reading, because a
disagreement is then the firmware's rather than the decoder's.

---

## What is here

    testbench/
      README.md      this
      host/          the scripts the Pi runs: capture, decode, self-test
      decoders/      protocol decoders, and their offline verification
      captures/      run artefacts, not committed

---

## What is not decided

Two things I cannot settle from here, and both change the design:

1. **Does the Pi have the LA2016's FPGA bitstream?** Without it libsigrok
   drives nothing. If the extraction is a problem, the alternative is a
   supported analyser, and it is better to know before the wiring is made.

2. **Debug Probe or `linuxgpiod` for SWD?** If a Debug Probe is on the Pi, the
   RP2350 can be reflashed unattended and a run can start from a build. If not,
   every firmware change needs a person, and the recipes should be written to
   assume a fixed stimulus firmware instead.

One more, smaller: whether the panel and coprocessor live on this bench
permanently or are attached per session. Permanent is what makes a regression
run possible — the same recipes over the same wiring after every merge.
