# STOP as an emergency stop

<sub>**English** · [Deutsch](Stop-de.md)</sub>

The panel's STOP behaves as an emergency stop button. One touch stops the
bench and latches the stop; holding STOP for 2s starts the reset procedure.
[Safety](Safety.md) describes the other stop mechanisms, the heartbeat line
and the IOmcu's own latch. The code is `shared/safety/stop_latch.c`,
`arming_held()` in `shared/safety/arming.c` and `shared/ui/stop_hold.c`,
host-tested in `test_stop_latch` and `test_stop_hold`.

- **Touch.** Pressing STOP on any screen puts the bench in SAFE, switches
  off every output, the PD mini output and the programming ports' 5V, and
  withholds the heartbeat, as [Safety](Safety.md#stop-mechanisms)
  describes. The stop latches: it stays when the finger lifts, when the
  screen changes and when the link comes back.
- **Latched.** STOP stays red inside a 3px yellow frame and reads
  `STOPPED` over `HOLD 2s TO RESET` (DE `GESTOPPT`, `2s HALTEN ZUM
  LÖSEN`). The band's arm chip reads STOP in place of SAFE or ARMED. ARM,
  OUTPUT ON, PROG 5V ON, a test or a limit run start are refused with the
  alert `STOP pressed: hold STOP 2s to reset.`, which leaves the band in
  the frame after the reset completes; the KST, Hitec and ESC
  wire programmers refuse CONNECT with their stop line. An arm does not
  clear this stop.
- **Reset hold.** A press that lands on a latched STOP is a STOP as well,
  and starts the reset's hold: the fill fades from red to the frame's
  yellow across 2s. The hold completes only on one unbroken contact on
  STOP for 2s, with the touch controller answering throughout. Each of
  the following cancels the hold without requesting a reset: a release before 2s, which shows
  `Hold 2s to reset the STOP.`; a slide off STOP; a second contact on the
  band; lost touch events; a screen change; a touch controller unanswered
  for 100ms (`STOP_LATCH_FRESH_MS`), which the control task counts as a
  contact break. A STOP or a break less than 1900ms (`STOP_LATCH_HOLD_SEEN_MS`)
  before the hold's end, or after it, refuses the request.
- **Reset.** A state machine in the control task performs the reset,
  advancing one step per pass:

  | State | Leaves when | To |
  |---|---|---|
  | LATCHED | the completed hold passes the timing check for the latest STOP and contact break | HOLD_DONE |
  | HOLD_DONE | 100ms after the hold: the first read issued | ASKING |
  | ASKING | everything reads off, 500ms (`STOP_LATCH_FLOOR_MS`) or more after the STOP | CONFIRMED |
  | ASKING | no confirmation within 2000ms (`STOP_LATCH_CONFIRM_MS`) | LATCHED |
  | CONFIRMED | the heartbeat edges again | RESUMED |
  | RESUMED | CLEAR written once, link up | CLEAR_WRITTEN |
  | CLEAR_WRITTEN | done | UNLATCHED |

  A STOP returns any state to LATCHED. A contact break or an error returns
  HOLD_DONE, ASKING or CONFIRMED to LATCHED. A STOP pressed during one
  of the machine's own reads counts before that read does. "Everything
  off" is read every 50ms in ASKING: STATUS not ARMED, the SUPPLY output
  off where the IOmcu serves that page, the PROG_SUPPLY output off where it
  serves that one, and this end's bank SAFE with no supply or 5V on. The
  bench stays SAFE after the reset: ARM, OUTPUT ON and PROG 5V ON each need
  their own hold. A programming port the STOP caught in a session stays
  held across the reset; CLEAR or LEAVE on its page goes on from there.
- **No confirmation.** The latch stands, the heartbeat stays withheld and
  the alert says `STOP not reset: the IOmcu did not report every output off
  within 2s.` The next hold asks again.
- **No link.** If no IOmcu answers, the reset checks the panel's own output
  bank and model, and sends no CLEAR. A connected IOmcu that is not
  answering has entered failsafe because the link is silent. It needs the
  CLEAR sent by ARM, as after any link failsafe.
- **FAULT 10.** While STOP is latched, while its reset finishes, and for
  1000ms after it (`STOP_LATCH_GRACE_MS`), the band leaves out
  LINK_FAULT_HEARTBEAT: the
  panel withholds the line itself, and FAULT 10 would read as the
  emergency stop or an open wire. The hardware emergency stop on the
  heartbeat line and an open wire show as FAULT 10, or as HWSTOP with
  SETUP > PANEL > Hardware stop ON
  ([Hardware stop button](Safety.md#hardware-stop-button)). The grace
  leaves out HWSTOP the same way: both are the same bit.
- **Restart.** The latch is not kept: a panel that restarts comes up
  unlatched, SAFE, with every output off.
- A stop the bench raises on its own, the link lost while ARMED, the IOmcu
  refusing an armed write or the render-loop watchdog, is not this latch:
  the band keeps SAFE and every FAULT bit, FAULT 10 included, since the
  cause may be the emergency stop or an open wire, and the next switch to
  ARMED clears it. A press on STOP over it latches STOP as above.
