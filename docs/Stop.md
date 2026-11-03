# STOP as an emergency stop

<sub>**English** · [Deutsch](Stop-de.md)</sub>

The panel's STOP behaves as an emergency stop button: one touch stops the
bench and latches, a hold of 2s resets the latch. Everything else about how
the bench stops, the heartbeat line and the IOmcu's own latch is in
[Safety](Safety.md). The code is `shared/safety/stop_latch.c`,
`arming_held()` in `shared/safety/arming.c` and `shared/ui/stop_hold.c`,
host-tested in `test_stop_latch` and `test_stop_hold`.

- **Touch.** A press on STOP, from any screen, puts the bench to SAFE,
  switches every output, the PD mini output and the programming ports' 5V
  off and withholds the heartbeat, as [Safety](Safety.md#stop-mechanisms)
  describes. The stop latches: it stays when the finger lifts, when the
  screen changes and when the link comes back.
- **Latched.** STOP stays red inside a 3px yellow frame and reads
  `STOPPED` over `HOLD 2s TO RESET` (DE `GESTOPPT`, `2s HALTEN ZUM
  LÖSEN`). The band's arm chip reads STOP in place of SAFE or ARMED. ARM,
  OUTPUT ON, PROG 5V ON, a test or a limit run start are refused with the
  alert `STOP pressed: hold STOP 2s to reset.`; the KST, Hitec and ESC
  wire programmers refuse CONNECT with their stop line. An arm does not
  clear this stop.
- **Reset.** A press that lands on a latched STOP is a STOP as well, and
  starts the reset's hold: the fill fades from red to the frame's yellow
  across 2s. A release before 2s shows `Hold 2s to reset the STOP.` and
  asks for nothing; a press that slides off STOP or a lost touch asks for
  nothing. The hold counts only when the last STOP, its own press, was
  applied 1900ms (`STOP_LATCH_HOLD_SEEN_MS`) or more before it completed: a
  STOP pressed after that, before the control task takes the request,
  refuses it, and so does a new press on STOP or a lost touch before the
  request is taken. The heartbeat is then withheld for 1900ms at least,
  well over the 500ms the IOmcu needs to doubt the line. At 2s the panel
  asks the IOmcu, every 50ms, whether everything is off: STATUS not ARMED,
  the SUPPLY output off, the PROG_SUPPLY output off, read after the hold,
  and its own bank SAFE with no supply on. Once it is, the heartbeat edges
  again and CLEAR is written once. The bench stays
  SAFE: ARM, OUTPUT ON and PROG 5V ON each need their own hold. A
  programming port the STOP caught in a session stays held across the
  reset; CLEAR or LEAVE on its page goes on from there.
- **No confirmation.** Without it within 2000ms (`STOP_LATCH_CONFIRM_MS`)
  the latch stands, the heartbeat stays withheld and the alert says
  `STOP not reset: the IOmcu did not report every output off within 2s.`
  A STOP or an error during the wait withdraws the reset.
- **No link.** With no IOmcu answering there is nobody to ask: the reset
  takes the panel's own bank and model. An IOmcu that is wired and not
  answering has failed safe on link silence and needs an ARM's CLEAR, as
  after any link failsafe.
- **FAULT 10.** While a stop is latched, and for 1000ms after a reset
  (`STOP_LATCH_GRACE_MS`), the band leaves out LINK_FAULT_HEARTBEAT: the
  panel withholds the line itself, and FAULT 10 would read as the
  emergency stop or an open wire. The hardware emergency stop on the
  heartbeat line and an open wire show as FAULT 10.
- **Restart.** The latch is not kept: a panel that restarts comes up
  unlatched, SAFE, with every output off.
- A stop the bench raises on its own, the link lost while ARMED or the
  render-loop watchdog, shows the same STOP and is reset the same way. The
  next switch to ARMED clears it as well.
