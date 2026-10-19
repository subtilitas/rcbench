# Hitec harness test plan

What the bench station measures with the harness, per question and per
servo, so that `protocols/hitec` can be checked against the real servos.
Every run pairs the harness's report with a logic analyser capture of the
same line. Captures and reports stay on the Pi; none go into the
repository.

Servos: HS-5645MG and HS-7955TG (classic), D485HW (D series).

## Before the first run

Each step that connects, powers or drives something waits for Julian's
yes (see "Electrical risks" in [README.md](README.md)).

1. Flash the harness with no servo connected. `hello` answers; `open
   classic 2 mark=3`, `status`, `close` answer `ok`.
2. Check the analyser channels on the line and the marker as
   `testbench/WIRING.md`, section 3, does: `open classic 2 mark=3`,
   `level 0 1000` ten times, the line channel must follow.
3. Classic servo powered, harness not connected: the signal wire's idle
   voltage against the star point, with a multimeter. Then with 10 kOhm
   from the wire to ground: the voltage again. Both go to Julian before the
   harness is connected.
4. D485HW powered, harness not connected: the same two readings, and the
   resistance from the signal wire to ground with the servo unpowered.
5. Fit `R_s` and, for the D series, `R_p` as the README's wiring says. With
   the harness open and idle, the line's level on the analyser: classic
   high (the servo's), D series as `R_p` gives it.

The analyser runs with `testbench/host/capture.sh`, threshold 1.4 V (the
threshold the module's timings were read at), trigger on the marker's
rising edge:

```bash
# classic: 52.08 us a bit; 8 MHz gives 417 samples a bit
testbench/host/capture.sh hitec-c-<servo>-<run> D<line>,D<mark> 8m 1m 1.4 D<mark>=r
# D series: 8.68 us a bit; 24 MHz gives 208 samples a bit
testbench/host/capture.sh hitec-d-d485hw-<run> D<line>,D<mark> 24m 2m 1.4 D<mark>=r
```

`<line>` and `<mark>` are the analyser channels the station clips on; the
channel map has no Hitec channel yet.

Each run below is one harness command with its capture, repeated 20 times
unless it says otherwise. Keep each report next to its capture under the
same run name.

## Classic: HS-5645MG, then HS-7955TG

`open classic <pin> mark=<mark>`.

| # | Question | Commands | Read from the capture and the report |
| --- | --- | --- | --- |
| C1 | Does the servo answer the harness at all | `c 0x67 0 0`, then `c 0x61 0x00 0` to `c 0x61 0x0F 0` | `dec rx=OK` share; on the analyser the 4 master bytes, then the 2 reply bytes |
| C2 | Do the capture's times match the wire | the C1 captures | each `byte k at_ns` against the analyser's start edge from the release (start of the last master byte's stop bit): difference under 1/16 bit, 3.3 us |
| C3 | The reply's first start bit after the master's last byte | the C1 reports, `delay_ns` | spread over 20 reads, after the first command and after reads; against the accept window 494.8 us to 1 ms (`HITEC_C_REPLY_MIN_NS`, `HITEC_C_REPLY_MAX_NS`) |
| C4 | The servo's bit time | the C1 captures | reply byte start to start, and the bit edges inside one byte, against 52.083 us |
| C5 | Does the servo accept the harness's pacing | `c 0x61 0x05 0` with `gap=0`, `gap=1` (572.9 us start to start), `gap=2`, `gap=6` | `rx=OK` share per gap |
| C6 | Write and read back | only after C1 passes: read an address, `c 0x62 <addr> <the value read>`, read it again | `dec rx=OK` for the write (`+ +`); the read-back value unchanged; the reply delay after a write (up to 614 us measured before) |
| C7 | The listen and its answer | power the servo up during `listen 3000000 2 answer=0x2b want=0x2b` (needs a switched servo supply; Julian decides how) | whether the servo sends `+` at power-up; `answered=1`; on the analyser the answer's start bit 520.8 to 527.3 us after the servo's |
| C8 | Does the servo start a byte right after the answer | C7 with `rx=2` and then `rx=4` | on the analyser, the servo's next start bit against the end of the answer's stop bit: inside the 6 released bit times the harness does not sample (the open Codex finding on the answer loop of #477) or later |
| C9 | `-` and `0x63` | `send` of a frame with a bad sum, e.g. `send 0x61 0x05 0x00 0x00 window=1500 rx=2`; `c 0x63 0x00 0` | whether `-` (0x2D) comes back; what 0x63 answers |

## D series: D485HW

`open d <pin> mark=<mark>` (open drain). Push-pull only in D7.

| # | Question | Commands | Read from the capture and the report |
| --- | --- | --- | --- |
| D1 | Does the servo answer the harness at all | `dread 0x0C` | `dec rx=OK` share; 5 request bytes, the answer 7 bytes |
| D2 | Do the capture's times match the wire | the D1 captures | `byte k at_ns` against the analyser's start edges from 1 bit after the request's last stop bit: difference under 1/16 bit, 0.54 us |
| D3 | The answer delay and the accept window | the D1 reports, `delay_ns` | spread over 20 reads against 5.9 to 6.5 ms measured before and the 2 to 20 ms window (`HITEC_D_ANSWER_MIN_NS`, `HITEC_D_ANSWER_MAX_NS`) |
| D4 | The read lead: 117.2 us (the harness) against 88 us (the HFP-30) | `dread 0x0C`, `dread 0x0C lead=0`, and `send 0x96 0 0x0C 0 0x0C lead=60 window=25000 rx=8` | `rx=OK` share per lead; `lead_ns` in the report and on the analyser |
| D5 | The trailing byte 0x00 | the D1 reports | how often byte 7 is 0x00 with `framing=0`, and whether a byte 8 (the release) shows; on the analyser the line after the answer: low 110 to 190 us, then high (measured before) |
| D6 | Release or hold after a write | only after D1 passes: `dwrite <addr> <the value read>`, then `dread <addr>`; again with `level 0 20000` between the two (the line held low as the HFP-30 holds it) | `rx=OK` of the read-back in both orders; the line level between the two on the analyser |
| D7 | Push-pull | `close`, `open d <pin> pp mark=<mark>`, then D1 | `rx=OK` share against open drain; on the analyser the sent bits' high level and edges; whether the answer still reads |
| D8 | An ID other than 0 | `dread 0x0C id=1` | whether anything answers |
| D9 | The idle level between transactions | 20 s of capture with the line open and no command | high or low, and when it changes (seen both ways before) |

## Not covered by the harness

- The servos' tolerance to the master's bit time: the harness's divider is
  set by the system clock, within 0.049 %. A sweep needs a build with
  another system clock.
- Edges, rise times and the voltages themselves: a scope, not the analyser.

## Open questions

1. Which RP2350 board runs the harness. A spare board keeps the IOmcu and
   the panel as they are. Flashing it onto the RP2350-CAN replaces the
   IOmcu image until it is flashed back, and its GP3 and GP8 to GP12 carry
   the heartbeat and the CAN controller's SPI.
2. How the harness reaches the Pi while the analyser captures: an isolated
   USB adapter, or Julian's decision for this tool against
   `testbench/WIRING.md`, section 1.
3. How the servo supply is switched for C7 and C8 (the power-up listen).
4. `R_s` and `R_p`: the values above are start values, not derived from a
   measurement; the readings from "Before the first run" decide them.
