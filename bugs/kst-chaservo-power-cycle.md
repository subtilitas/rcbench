# Chaservo programming needs a power cycle before and after

Status: open. Measured on 2026-10-16 against main edd813d6. No code change
yet.

Abbreviations: PD = USB Power Delivery (the PD mini supply module); PWM =
pulse-width modulation; KST = the programming wire of KST and Chaservo
servos.

## Summary

A Chaservo of type V6 enters programming mode only when the 64-pulse sync
arrives after a power-up. A written LEFT ANGLE or RIGHT ANGLE acts only
after the next power-up. The servo programmer cycles the supply in neither
case. With the PD mini output already on, CONNECT takes the port, but the
servo stays out of programming mode. After a write, LEAVE hands the port
back to PWM, and the servo neither moves nor uses the new value.

## Setup

| Item | Value |
| --- | --- |
| Servo | Chaservo, read as `Servotyp V6: Ansteuerung mit 20 kHz`; 32 registers |
| Port | programming port 1, GP13 (pad 17) |
| Supply | PD mini, 5.00 V set, 1.00 A limit, PD mini TX GP19, RX GP18 |
| Panel language | German |
| Signal check | logic analyser on GP13 |
| Angle reading | photos of the horn; camera tilt about 5 deg; each value +-10 deg |

## Before programming: CONNECT

| Start state | Result |
| --- | --- |
| Servo powered from the IO board | 3 attempts. Each CONNECT sends read 0x01 (1,321 us), a 64-pulse sync (3,226 us) and read 0x01, 15.3 ms in total, half-cells 25 us. No edge from the servo in the 13.5 s after it. `Kein Servo antwortet. Stecker, Versorgung, Port prüfen.` |
| PD mini output on at 5.00 V, servo idling | The port goes to programming: `Port 1 im Programmiermodus, kein PWM: 4.99 V, 6 mA`. READ ALL reads nothing: every field `--`, `Das Servo ist nicht im Programmiermodus: VERBINDEN.` |
| PD mini output off (0.05 V) | CONNECT switches the supply on. The servo enters programming mode: `Alle 32 Register gelesen.` |

In the second state the advice line names VERBINDEN, but the programmer
shows no VERBINDEN control while the port is in programming. The control
there is VERLASSEN.

## After programming: the written value

| Step | Command on GP13 | Horn |
| --- | --- | --- |
| Before the change, LEFT/RIGHT ANGLE 150/150 | - | 10.4 deg at rest; the travel was not measured |
| 150 -> 75 written and verified (`2 Werte geschrieben und geprüft`), then LEAVE | 2,000 us and 1,000 us at 50.0025 Hz (period 19,998.5 to 19,999 us) | 10.2 deg and 9.8 deg: the servo does not follow |
| PD mini output off for 4 s or more, then on | the same | -14 to -16 deg and +36.6 deg: about 52 deg of travel |
| Restored to 150/150 (output off, CONNECT, write, verify, LEAVE, power cycle) | the same | -71 deg and +77 to +78 deg: about 149 deg of travel |

The new value acts after the power cycle. Before it, the servo does not
follow PWM at all.

## Power-off time

The cycles that worked kept the output off for 4 s or more. Shorter times
were not tried. The owner's estimate is that 0.5 s to 1 s without power is
enough to enter and to leave programming mode. That estimate is not
measured.

## What the code does today

- CONNECT from PWM (`enter_from_pwm()` in `shared/outputs/kst_port.c`)
  checks the supply's range and leaves an output that is on as it is. It
  cycles nothing.
- The coprocessor can cycle the supply. `LINK_KST_OP_POWER_CYCLED` and
  `LINK_KST_OP_REBOOT` (`shared/link/include/link_pages.h`, protocol 4.13)
  switch the PD mini off, keep it off for `KST_PORT_RAIL_OFF_MS` (2,000 ms),
  switch it on and wait `KST_PORT_RAIL_WAIT_MS` (1,000 ms)
  (`shared/outputs/include/kst_port.h`).
- The panel sends `POWER_CYCLED` only when the registers are in doubt
  (`leave_doubt` in `shared/bench/kst_prog.c`). Otherwise it sends
  `LINK_KST_OP_LEAVE`, which returns the port to PWM without a cycle and
  leaves an output that the port switched on still on.
- No panel code sends `REBOOT`.
- The limit run (`shared/servo/servo_limrun.c`) programs LEFT ANGLE and
  RIGHT ANGLE and then searches both ends again. Nothing in it cycles the
  supply. With this servo the search after programming measures the old
  travel, and a second session started with the output on does not enter
  programming mode. This follows from the measurements above. The limit run
  itself was not run against this servo.
- `docs/ServoProgrammer.md` (Constraints) states the assumption that a servo
  needs no power cycle, "not measured". Its list of open measurements names
  items 2, 4 and 7 (a written value acting at once, persistence, entry and
  exit timing). This servo answers 2 and part of 7.

## Changes to decide

1. CONNECT with a PD mini output that is on: switch it off, keep it off for
   the rail-off time, then switch on and send the sync.
2. LEAVE after a write, with a PD mini: return to PWM through a supply cycle
   (`POWER_CYCLED`). Without a PD mini: tell the operator to cycle the
   servo's power.
3. The limit run: cycle the supply between programming and the search
   after it, and at each new session.
4. `KST_PORT_RAIL_OFF_MS`: 2,000 ms today. The owner's estimate allows
   500 ms to 1,000 ms. The value needs a measurement first.
5. The advice line `Das Servo ist nicht im Programmiermodus: VERBINDEN.`
   (`KP_S_NOT_IN_MODE`) names a control that is not shown in that state.
6. The docs: replace the "no power cycle" assumption with the measured
   behaviour, and mark open items 2 and 7 as answered for this servo type.

## Not measured

- The shortest power-off time that enters or leaves programming mode.
- The shortest time after power-up before the servo accepts the sync.
- Whether the same holds for KST servos. A KST servo on the Tool Card
  accepted the sync about 3 s after its power-up.
- Whether a written value survives more than one power cycle. One cycle
  was observed per value.
- Registers other than LEFT ANGLE and RIGHT ANGLE.
- The travel at 150/150 before the change.
