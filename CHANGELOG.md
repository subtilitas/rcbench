# Changelog

Notable changes to rcbench. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git.

## Unreleased

### Fixed

- **A power cut during a run left a 0-byte CSV (comma-separated values)
  file.** The run log was written and never committed, and closed only on the
  disarm edge. FAT (file allocation table) keeps a file's length in its
  directory entry, and `fwrite` alone never writes that entry, so the whole
  run was lost whatever the data sectors held. The file is committed every 20
  rows or 1000 ms of run, whichever comes first, with `fflush` followed by
  `fsync`. What a power cut costs is that commit interval plus the queue
  between the control task and the logger: at most `LOG_WRITER_FLUSH_ROWS`
  (20) uncommitted rows in the writer and at most `LOG_Q_LEN` (64) in the
  queue. Twenty and not nineteen -- the count reaches 20 before the commit is
  attempted and is cleared only once it succeeds, so those rows are not
  durable for as long as the card takes. With a card keeping up the queue is
  empty and the cost is under 1.0 s of run; with a card stalled it is 84 rows,
  4.20 s at the panel's 20 Hz sample rate. Past 64 queued rows the control
  task drops them and counts them, so the loss stops growing there and is
  reported when the run closes.
- **The run log put SD (Secure Digital) card writes on the safety line.** Row
  writes, the file-name scan and the close ran on the control task, which
  drives the heartbeat on GPIO6 and reads STOP. That task's ceiling is
  HEARTBEAT_MAX_GAP_MS (150 ms) and the coprocessor fails safe after 200 ms of
  link silence, while the SD specification allows a card 250 ms to finish one
  single-block write: a card that paused was a dropped heartbeat, not a late
  row. Every card access is now on the `runlog` task, and rows cross to it on
  a queue the control task never waits on. A card that falls behind the run
  costs rows, which are counted and reported on the panel when the run closes,
  rather than costing the heartbeat.
- **SPEED below 50% moved the servo horn at 50%.** `outputs_step()` rounded
  each slew increment up -- `(slew_per_s * dt_ms + 999) / 1000` -- so a step
  delivered at least one span unit whatever the rate said, and the
  coprocessor's loop steps about once a millisecond. Every `slew_per_s` under
  1,000 units a second therefore rendered as 1,000. SPEED on the servo screen
  is `2 * OUT_SPAN * pct / 100`, so every setting from 1% to 49% crossed at
  the same rate as 50%. The remainder is carried between steps instead: the
  rate is the one asked for, and a slew slower than one unit a step still
  arrives rather than being truncated to nothing. The elapsed time is capped
  at `1000 * OUT_SPAN / slew_per_s`, which is the interval past which one step
  covers the whole span and arrives regardless, so the cap discards nothing
  that could move the channel and the multiply still fits a `uint32_t`.
- **The coprocessor armed on the previous pass's heartbeat.** `outputs_arm()`
  read the cached `s_beat.alive` and `heartbeat_poll()` ran after
  `outputs_hw_service()`, so a line that went past `HEARTBEAT_MAX_GAP_MS`
  (150 ms) got one more service of every output before the poll noticed.
  Silence generates no event, so the poll is what notices it: it runs first
  now, and the arm below it uses this pass's answer.

- **Two servo pins 16 apart drove from one pulse width.** On the RP2350 a PWM
  (pulse-width modulation) channel is one compare register, and GPIO
  (general-purpose input/output) numbers fold onto the 12 slices: GP0 to GP31
  take slice (pin / 2) modulo 8, GP32 to GP47 take slice 8 + (pin / 2) modulo
  4, and the channel is the low bit of the pin number. `out_pwm_bind()`
  compared slices and not channels, so the second pin of a folded pair was
  accepted whenever it asked for the frame rate the first one runs at, and
  muxing it put both pads on one compare register: two leads, one pulse width,
  and only the pin written last saying what it is. Six such pairs are free on
  the coprocessor's header -- GP0 and GP16, GP1 and GP17, GP2 and GP18, GP4 and
  GP20, GP5 and GP21, GP6 and GP22 -- and the pin arrives from the OUTPUTS
  page, so ticking two of them on the outputs screen reaches it. The second pin
  of a pair is refused. The fold is `shared/outputs/out_pwm_map.c`, where the
  host suite holds it against the pico-sdk's own mapping, and `out_pwm_bind()`
  compares the two on every bind rather than assuming they agree.

  A refused bind leaves the slot unbound, and the OUTPUTS page carries no
  register saying whether a slot is bound, so an unbound slot reads back like a
  driving one. That is recorded as an open item rather than fixed here: it
  needs a bit on the page and a screen that draws it.

## 0.7.0 - 2026-09-08

The servo screen can drive a servo. Arming existed only on MOTOR & ESC and
leaving that screen disarms, so the horn followed the finger and nothing
moved; SPEED changed the drawing and not the servo; and every bench showed
FAULT 01 from power-up.

Most of what follows is the arming and command path either side of that
button. A stop, a disarm and a release each have to survive a link that is
slow, a queue that is full, a screen that is being left and a touch
controller that stops answering, and each of those was a way for something to
keep driving after the operator had asked it to stop.

### Fixed

- **Every bench showed FAULT 01 from power-up.** The coprocessor's silence
  watchdog counted from its own boot, and it is awake a few hundred
  milliseconds before the panel starts polling, so LINK_DEV_SILENCE_MS
  (200 ms) elapsed before the first request could arrive and the link-silent
  fault latched on every bench, before anything had happened. It cleared only
  on the first arm. The watchdog does not run until a request has arrived:
  the wait for the first one is not silence. Reported as #99, where an
  operator asked whether the bench was broken.

### Added

- **ARM on the servo screen.** Arming existed only on MOTOR & ESC and leaving
  that screen disarms, so the servo screen could not drive a servo at all: the
  coprocessor writes a pulse of length zero to every PWM (pulse-width
  modulation) pin while the bench is not armed, and there was no way to reach
  an armed bench with the horn on screen. The button is the same two-second
  hold as the other screen's, and the gesture itself -- the hold, what a
  finger leaving the control does to it, and what happens when the bench
  disarms under a press -- now lives once in `ui_widgets` (`ui_hold_t`) rather
  than twice. Leaving the screen disarms and releases the pin. Reported as #99.

### Changed

- **SPEED on the servo screen sets the rate the bench moves the output.** It
  moved the drawn arm and nothing else, so a servo went at its own rate
  whatever the screen said. It is the channel's slew now, carried with the
  command: 100% is immediate, which is where the screen starts and what
  everybody had before, and below that the bench ramps the command in front
  of the servo -- 30% takes three times as long to cross as 90%. Changing it
  applies to an output already being held.

### Fixed

- **A servo swung back to centre half a second after the finger stopped.**
  The panel wrote the servo's channel on a touch and never again; a channel
  nobody has commanded for OUT_DEFAULT_TIMEOUT_MS (500 ms) goes to its rest,
  which for a surface is mid-travel. The position is now said again every
  100 ms while something is being held, one register at a time. Not reachable
  before this release, because the servo screen could not arm.
- **The disarm inhibit lasted one pass instead of 300 ms.** Whether the timer
  was running was encoded by forcing its timestamp non-zero, which moved it
  a millisecond into the future whenever the clock was even; the elapsed time
  then wrapped to its maximum and ended the inhibit immediately. A far end
  that could not be reached never saw its 150 ms of silence and kept driving.
  The timer's state is its own flag.
- **The disarm inhibit was lifted by the transaction meant to honour it.**
  The request was cleared before the write that delivers it, so the pump
  inside that write's wait saw nothing outstanding and put the safety line
  back up -- with the far end still armed if the write was lost. The request
  stands until the far end has taken it, or until the line has been down
  twice HEARTBEAT_MAX_GAP_MS (300 ms), by which time the far end has failed
  safe on its own account. A disarm nobody can deliver does not hold the line
  down for ever.
- **An explicit RELEASE waited for the end of the drain.** Only the disarm
  branch cleared the slot it had let go of; a release recorded the debt and
  left it to the post-drain service, behind whatever else was queued -- two
  blocking exchanges for an outputs binding, for instance. Both pay it before
  returning to the queue.
- **A disarm could not reach a write already on the wire.** The far end
  applies a write and then acknowledges it, so a disarm arriving while that
  acknowledgement was in flight found the output already bound -- and a lost
  acknowledgement left it driving for the full LINK_HOST_TIMEOUT_MS (1000 ms)
  with the request unserved. The safety line is the one channel that does not
  need the link, so it now carries an unserved disarm: on a healthy link the
  request is served on the next pass and nothing changes, and on a link that
  has stopped answering the far end fails safe. A release does not do this;
  letting go of one pin is not worth latching a failsafe.
- **A touch outage that recovered left the bank armed.** Touch can die and
  answer again inside one blocking link exchange: by the time the policy ran
  the controller was healthy, so nothing in the state said the outage had
  happened, and the heartbeat need not have been withheld long enough for the
  far end to fail safe either. What was armed comes down, on the edge that
  saw the outage rather than on the state afterwards.
- **A servo's three writes could not be countermanded partway through.** Each
  waits up to LINK_HOST_TIMEOUT_MS (1000 ms) with the pump running inside it,
  so a stop or a disarm could arrive between one write and the next and the
  slot was bound anyway. The writes stop when that happens, and the slot is
  bound by the last of them, so the pin is left unbound rather than driving.
- **RELEASE waited behind the backlog while DISARM did not.** Tapping RELEASE
  lets go of the pin without disarming the bench, and it queued like any
  other command -- behind positions that are three exchanges each. It is out
  of band now, as the disarm is, and it voids drive commands that predate it.
- **An arm went through even when the operator changed their mind mid-write.**
  Arming is two exchanges, each of which can wait a second, and the pump runs
  inside both: a stop applied there, or a disarm posted while the failsafe
  clear was still on the wire, was not looked at before the write that
  actually arms. The far end could then be driving for a timeout before
  anything took it back. The bench is asked again between the two.
- **A disarm waited behind a backlog of positions.** Making it unloseable did
  not make it prompt: the flag was read once before the queue was drained, and
  a backlog of servo positions is three exchanges each, so a degraded link
  could keep the bench armed for seconds after a disarm. The flags are
  serviced between queue entries now, and a command that predates a disarm no
  longer drives after it -- each carries the count of disarms its sender had
  seen, as it already carried the count of stops.
- **A stop the pump saw but could not route could swallow the next one.** The
  marker that stops the backstop counting a press twice was set whether or not
  the event reached the router; when the router's queue was full it never
  latched, and the marker stayed armed until it consumed a later stop the
  backstop really did have to apply. It is set only when the event was
  actually routed.
- **One STOP press counted as two stops.** The press is applied where it is
  seen, and the screen still receives the event, so the router latched the
  same release and the backstop stopped the bench again. The count is what
  rejects commands made before a stop, so a command the operator made after
  it -- between the two -- was thrown away. A press the pump applied is not
  counted again by the backstop.
- **An arm could be granted from a gesture invalidated while it waited.** The
  servo screen's arm releases the slot first, and that release can wait a
  second on the wire with the pump running inside it, so a stop -- or touch
  dying and recovering -- could invalidate the gesture between the drain's
  check and the request. The gesture is asked about again after the release.
- **A newer servo write did not void an older release.** A release that failed
  keeps its debt, and a position written afterwards rebinds the slot to what
  was asked for; paying the debt then cleared it and left the pin dead until
  the next refresh. A write that the far end acknowledged voids a release
  owed for that slot, and one that failed does not.
- **A leave-time disarm could be evicted from the command queue.** The queue
  drops its oldest entry when full, and leaving a screen generates commands
  on the way out while the next screen generates more, so the disarm posted by
  leaving an armed servo screen could be dropped -- leaving the bench armed
  and the servo held behind a screen nobody was watching. A disarm is a flag
  as well as a queue entry now, the way a stop is; applying it twice costs
  nothing.
- **An older servo release could clear a binding that had just been made.** A
  release that timed out keeps its debt, and a binding applied afterwards in
  the same drain writes every slot including slot 0; paying the debt then
  cleared the binding the outputs screen had just been told was written, and
  the far end kept the cleared page. A binding that lands says what slot 0 is,
  so it voids an older release owed for it.
- **STOP waited out a link exchange before it did anything.** The press was
  recorded and acted on by the policy, which is exactly what a blocked loop
  cannot reach: a servo command makes up to three exchanges of up to
  LINK_HOST_TIMEOUT_MS (1000 ms) each, and the far end drives throughout. The
  press is applied where it is seen now, in the pump that runs inside that
  wait, so the safety line stops being asserted in the same pass and the
  coprocessor fails safe within HEARTBEAT_MAX_GAP_MS (150 ms) whatever the
  panel is waiting for. The rest of a stop follows when the loop is free.
- **A stop on a bench that was not armed left the servo driven.** Letting go
  hung off the policy's disarm, which does not happen when nothing was armed,
  so a servo held from before the stop kept its slot refreshed every 100 ms --
  over the top of an outputs binding made afterwards. Every stop lets go now,
  counted rather than inferred from the disarm.
- **A position asked for before a stop was still driven after it.** Only
  arms were dropped as stale; a servo position or centre queued before a stop
  was applied afterwards, rebinding the slot the stop had just released and
  refreshing it from then on -- and a stop from touch dying or from the far
  end has no queued STOP behind it to release it a second time. Nothing that
  drives an output survives a stop it predates; a disarm, a release or a
  binding still does.
- **A stop waited behind commands that talk to the link.** The control loop
  drained the screens' commands before it served a pending STOP, and an
  unanswered exchange holds that loop for LINK_HOST_TIMEOUT_MS (1000 ms) --
  several such commands multiply it, with the far end armed throughout. The
  stop is served first again. What made that unsafe before was an arm queued
  from a gesture made before the stop, and each command now carries the count
  of stops its sender had seen, so a stale arm is dropped instead.
- **An arm learned after this frame's touch discarded a position issued after
  it.** The screens are told whether the bench is armed at the end of a
  frame, and an arm discards what was held before it; a position commanded
  earlier in the same frame, after the bench had actually armed, was thrown
  away by that. The bench's state is read before the frame's touch is
  dispatched, so a press acts on what the bench already is.
- **A stop on a bench that was not armed left the servo screen holding.**
  Dragging while disarmed commands a position, and a stop then changes
  nothing about the armed state while the panel releases the slot all the
  same. The screen cleared its driving state only on that state's edge, so it
  went on showing the output as held and the next change of type, trim or
  travel said the released position again. A stop lets go, whether or not
  anything was armed.
- **A touch outage inside a link wait left no trace.** The stop was counted
  only where the policy steps, and a link exchange waits up to 1000 ms while
  touch is still being pumped: a controller that died and recovered inside
  one such wait was never seen to have died, and a hold left standing by it
  completed and was accepted. Touch health is judged where touch is judged
  now, at the rate it is judged.
- **An arm still settling was not abandoned when touch died.** The disarm was
  gated on the bench being armed, and an arm that is settling is not armed
  yet, so a controller recovering before the deadline armed the bench from a
  gesture nobody had re-made -- and no screen could retract a request the
  policy already held.
- **Leaving MOTOR & ESC under a held ARM stranded the button.** No release
  arrives for a contact that was on ARM as the screen changed, and the press
  stayed recorded; with the hold kept for the contact that began it, every
  later press was then refused. Leaving drops the press it was routing.
- **Touch that stopped answering left an arming hold running.** A hold
  advances on frames rather than on touch events, so one still down when the
  controller went quiet kept counting; the arm was refused while touch stayed
  dead, but a controller that answered again before the two seconds were up
  let it through -- arming with nobody having touched anything since. Touch
  going dead counts as a stop now, once, on the edge, so every screen
  abandons its gesture and an arm made before it is dropped.
- **An arm queued before a stop could still arm the bench.** The command
  queue and the stop count cross between the two tasks independently, so an
  arm could be queued from a gesture the sender had watched and a stop be
  applied before that arm was drained -- and the arm then cleared the latch
  the stop had set. Each arm carries the count of stops its sender had seen,
  and one whose count is no longer current is dropped.
- **Arming from the servo screen could energise a slot the panel had never
  written.** After a panel restart the coprocessor still holds its slots, so
  slot 0 and the command in it survive while the panel remembers nothing; the
  screen's DISARM and RELEASE asked for the clear unconditionally but its ARM
  did not, and the arm rendered the old position. It asks first now, and the
  arm waits for the clear.
- **An unpayable release debt refused every arm.** Leaving the servo screen
  with the link down leaves a release owed that nothing can pay, and the arm
  gate refused every arm until a coprocessor answered -- including arming the
  panel's own bank in simulation, where no far-end output exists. The gate
  applies only while there is a link, and the far end is not told to arm while
  a slot is still owed a release instead.
- **Arming left the servo screen showing a position it had discarded.** An
  arm drops the held command and the slot so it starts from nothing, and the
  screen went on believing it was driving -- so the next change of type, trim
  or travel said that discarded position again, onto a bench that was armed
  by then.
- **Binding the servo's pin before its position arrived drove the old one.**
  The three writes a servo command makes are three transactions and the far
  end steps its outputs between them, so binding the slot second rendered
  whatever channel 0 was holding -- the position from before the last
  release, or mid-travel once that had gone stale -- and kept driving it if
  the position write then failed. The slot is bound last, so the pin is
  either unbound or already carrying what was asked for.
- **A servo pulse could be sent against a configuration that never landed.**
  Only the last of the three writes a servo command makes was checked, so a
  CHAN_CFG that was refused or timed out left the far end clamping against
  the endpoints it had before -- a narrow servo selected against a standard
  configuration renders 1500 us, past its 860 us maximum. All three are
  required now, and nothing is sent when the configuration has not arrived.
- **A cancelled arming gesture could still deliver its arm.** The hold can
  complete in the same frame the stop arrives, leaving the command waiting to
  be read; the screens were asked about the stop after their commands had
  been forwarded, so the arm went out a frame later and cleared the latch.
  The stop edge is handled before anything the screens produced, and
  cancelling a hold takes its unread command with it -- including after the
  finger has lifted, which clears the gesture but not what it produced.
- **A second stop during a new arming hold did nothing.** The latch is a
  level and says only that a stop is in force, so a STOP pressed while one
  was already latched -- during a hold begun after the first, which is how a
  stop is cleared -- left it exactly as it was, and the screens watching that
  level saw no change. The hold ran to completion and cleared the latch.
  Stops are counted now, in the policy where the latch is set, and every one
  of them cancels both screens' holds.
- **A stop did not end an arming gesture already under way.** A stop can
  latch on a bench that is not armed -- a STOP press, a dead touch, the far
  end -- so nothing about the armed state changed and a hold still under a
  finger ran on, completed, and asked to arm, clearing the latch that had
  just been set. Both screens abandon a hold when a stop latches.
- **A second contact could take over an arming hold.** A finger, or a palm,
  landing on ARM while another was holding it replaced the contact the
  gesture belonged to; the first finger's release was then ignored and the
  second contact could arm the bench. The hold now stays with the contact
  that began it, on both screens.
- **A stop pressed after an arming gesture could be undone by it.** The
  control loop stepped the arming policy before it drained what the screens
  had asked for, so a STOP that latched at the top of a pass was cleared
  later in the same pass by an arm queued before it: the bench armed from a
  press made to stop it. The queue is drained first now, and
  `arming_stop()` abandons an arm rather than deferring it, so the newer
  stop stands. Both screens' ARM went through this.
- **An arm could proceed on a servo release that did not land.** A release
  that is not acknowledged keeps its debt, but the arm went ahead anyway, so
  a transient link error during the release meant arming with the slot and
  its command still installed at the far end. An outstanding release now
  refuses the arm and says so on the alert band.
- **The servo screen went on showing a hold the bench had let go of.** A
  STOP, a dead touch or a far-end disarm left the driving state set, so the
  rings kept pulsing and a change to the type, trim or travel said the
  position again -- rebuilding the command the stop had released.
- **A write to one channel kept every other channel alive.** The coprocessor
  applied a CHANNELS write by commanding all eight channels of the stored
  page, whatever the frame carried, so any periodic write stamped every
  channel's clock. The timeout that returns an uncommanded output to its rest
  is per channel precisely so one screen's traffic cannot hold another's
  output up, and applying the page defeated it. Only the registers a frame
  carries are applied now. Latent until this release, which is the first
  thing to write a channel periodically.
- **Changing the servo type, trim or travel left the old pulse on the pin.**
  The pulse is the angle put through those three, and none of them said the
  position again, so a servo held while switching from STANDARD to NARROW 760
  kept 1500 us on a servo whose maximum is 860 while the screen showed the
  new range.
- **An arm could render a servo's slot before the release reached the far
  end.** The release was posted after ARM in the same pass, and the far end
  applies ARM and stamps every channel's clock before it steps its outputs,
  so a slot still bound at that moment rendered its old command until the
  clear arrived. The slot now goes first. A clear is also kept until the far
  end acknowledges it, rather than assumed from a write that may have gone
  into a link that was dropping, and RELEASE asks for the clear whether or
  not the panel remembers binding the slot -- after a restart the far end can
  hold a slot this end has never written. The servo screen's own DISARM asks
  for the same clear; a stop from anywhere else stays conditional, so it
  cannot quietly clear a slot the OUTPUTS screen bound.
- **A stop left a servo position an arm would step back to.** Only the servo
  screen's own disarm let go of the pin; a STOP, a dead touch, a disarm from
  MOTOR & ESC and a far-end disarm did not, and the far end keeps both the
  slot and the channel command through a disarm and a failsafe. The next arm
  found the channel neither overdue nor at rest and drove the servo to where
  it had been, with nobody having touched anything. Letting go of the servo
  is now paired with returning the throttle to zero, in every path that stops
  the bench. A release that cannot be sent -- the screen left while the link
  is down -- is kept as a debt and sent when the link is back, so a slot
  cannot outlive the screen that bound it.
- **A servo other than the standard one was driven against the wrong
  endpoints.** The panel configured the channel with a fixed 1000 to 2000 us
  whatever the screen's TYPE said, so a narrow servo (660 to 860 us) had its
  whole travel clamped to one end and a wide one (800 to 2200 us) was clipped
  at both. The command now carries the endpoints of the type it was made for.

## 0.6.1 - 2026-09-07

The throttle reached no bound pin, so an ESC (electronic speed controller) on
the OUTPUTS screen initialised, armed, and held one value at every slider
position. A PWM (pulse-width modulation) ESC could only be bound as a servo,
whose channel rests at half throttle.

### Added

- **MOTOR PWM, beside SERVO PWM.** The same pulse at the same 50 Hz, bound to
  a channel whose role is throttle rather than surface. An ESC (electronic
  speed controller) on a pulse pin was previously bindable only as SERVO PWM,
  which rests its channel at mid-span: 1500 us, half throttle, whenever the
  channel stopped being commanded while armed. The two entries are one driver
  at one rate, so the OUTPUTS page alone cannot tell them apart and the panel
  reads the CHAN_CFG page with it; a binding read back without the roles is
  not shown at all rather than shown as a servo.

### Fixed

- **The throttle drove no bound pin.** The MOTOR & ESC throttle commands bank
  channel 8, which is off the OUTPUTS page by design so that a motor command
  keeps the control page's priority on the wire; a pin bound on the OUTPUTS
  screen renders channels 0 to 7, and nothing wrote those. An ESC bound to
  GP0 with DShot therefore initialised, armed, and held whatever its channel
  rested at, at every slider position. The control page now commands every
  channel the binding marks a throttle -- the DShot entries and MOTOR PWM --
  and leaves the surface channels to the screens that own them. Reported as
  #99 from a bench: the ESC initialising, ARMED lit, and an unchanging pulse
  on a scope.
- **Opening the run log stopped the heartbeat.** `log_start()` runs on the
  arming edge, on the task that drives the heartbeat and reads STOP, and it
  probed for a free file name one card transaction at a time: a card holding
  400 runs cost 400 opens with nothing pumped in between, against a
  HEARTBEAT_MAX_GAP_MS ceiling of 150 ms. The pump now runs between probes and
  the scan starts where the last one finished. The retry was the worse half --
  the arming edge was read from the file handle, so a card that is full or
  unwritable made every pass look like a fresh arm and rescan once per
  CONTROL_PERIOD_MS for as long as the bench stayed armed. Whether a run is
  open is its own flag now, and a run that is not being recorded says so on
  the alert band rather than only on a console that is not reachable on every
  board.
- **The outputs board went grey without saying why.** The rules refusing every
  pin are right and the screen kept them to itself: a bench with one servo pin
  bound leaves seven channels free, PPM needs eight, and the whole board greys
  with nothing beside it. The reason now sits under the protocol in amber --
  the channels needed against the channels free, the slots when those run out
  first, or the protocol's own pin limit -- and only when nothing can be
  added. Reported as #99.

## 0.6.0 - 2026-09-07

Applying an output binding never worked, PPM never worked, and an arm could
carry the throttle from before the last stop. All three were on the bench the
whole time and none of them showed as an error.

### Fixed

- **Applying a binding reported NO LINK against a link that was up.** Every
  frame of a request is self-describing and the coprocessor answers each one
  it decodes, so a write of 32 registers draws eight acknowledgements of four
  at offsets 0, 4 ... 28. The panel waited for one acknowledgement covering
  the whole window, matched none of them, counted all eight as mismatches and
  waited out LINK_HOST_TIMEOUT_MS (1000 ms) -- transmitting nothing for that
  second, so the coprocessor's own LINK_DEV_SILENCE_MS (200 ms) watchdog
  latched failsafe. Both output pages are 32 registers wide, so it happened
  on every apply, and the second page of the pair was never sent: the far end
  kept a new channel configuration against the old slots. An acknowledgement
  is now taken in pieces the way a read's answer already was. Reported as #99
  and identified from a bench: LINK steady, LAST WRITE reading NO LINK, and
  `stale 8` in the panel's own counters -- one mismatch per frame.
- **PPM could be selected and never drove a pin.** The catalogue offered PPM
  at 50 Hz with eight channels, and eight channels need 8 x 2500 + 300 + 3000
  = 23,300 us of frame against the 20,000 us that 50 Hz gives.
  outputs_configure() accepted the rate, the coprocessor acknowledged the page
  and read it back unchanged, and out_ppm_bind() then refused the frame and
  left the slot unbound. The screen showed a binding the bench did not have.
  PPM runs at 40 Hz now, which gives 25,000 us and binds.
- **An arm could carry the throttle from before the last stop.** ARM and
  THROTTLE travel in one transaction, and the coprocessor's throttle is bank
  channel 8, off the CHAN_CFG page that addresses channels 0 to 7, so its
  slew is never configured and it steps straight to the command. Three disarm
  paths returned the command to zero; the far-end disarm did not, and no
  later disarm could cover for it -- arming_stop_from_far_end() clears
  a->armed itself and arming_step()'s disarm is gated on a->armed. A
  coprocessor disarm therefore left the throttle at its last position while
  the screen showed zero, and the next arm sent it. Every arm now starts from
  zero, at the arm rather than at each path that has to remember.
- **The arming hold did not end when the finger left the button**, so a press
  that started on ARM and slid onto the plot armed the bench two seconds
  later. Coming back does not resume it: the contact is finished and arming
  takes a fresh press. And a press standing while the bench was armed -- a
  press made to DISARM -- started its own two seconds the moment the bench
  disarmed, turning the operator's stopping contact into an arming one. The
  gesture now ends on the way to disarmed.
- **The link-up edge counted a sample that was never read.** While the link is
  down the question asked is the identity page, so an answer there says a
  coprocessor is present and nothing about the bench. One plot column and one
  CSV row carried the previous bench values, or zeros at boot.

### Changed

- **control_task() is 69 lines and calls sixteen named steps**, from 578. The
  order of every operation and every early exit is preserved; no loop local
  became a file static. The image differs by 12 bytes of .flash.text, all of
  it two inlining decisions: no other symbol changes size, and a control
  build with the steps forced inline reproduces the baseline's choices
  exactly. It buys readability, not coverage -- the steps are static and
  reach file statics, so the host suite still cannot link them.
- The panel's expander write records that it is single-threaded, names every
  caller, and states that a lock added later has to be bounded well under
  HEARTBEAT_MAX_GAP_MS (150 ms) -- I2C_TIMEOUT_MS is already 100 ms of it.
- Three claims corrected against the code: the protocol version on the wire
  is 2.5, the status page is polled every 500 ms, and CI holds seven
  per-screen and seven `-chrome` modes to their ceilings rather than six.

## 0.5.0 - 2026-09-06

A link that dropped never came back, and the panel could not say why. Both
are fixed, and the panel now explains a bad bus on its own screen instead of
on a console that is not always reachable.

### Fixed

- **A link that dropped stayed dropped, and nothing on the bench said so.**
  The panel holds one request at a time. `link_host_tick()` answers true for
  two different facts -- this request has been outstanding too long, and the
  link as a whole has gone quiet -- and only the first releases the
  outstanding slot. The wait in the panel ended on either, so a wait that
  ended on the second left the request pending for ever: `link_host_read()`
  then refuses every later request, returning before the wait is entered, and
  the wait held the only call to the clock that would have cleared it. The
  panel stopped transmitting, the CAN (Controller Area Network) controller
  reported a healthy bus because nothing reached the wire, and only a power
  cycle cleared it. The order that triggers it is the ordinary one:
  escalation is measured from the last reply and the request timeout from the
  request, so after a second of silence the next request wedges. Reported as
  #99, and diagnosed from a field log showing polls frozen at 1545 with zero
  timeouts and zero bus errors.

### Added

- **The CAN self-test runs at every start-up**, 1200 ms inside the splash and
  before the identity poll. A verdict other than every probe coming back
  intact puts the panel on a screen of its own instead of the menu: the
  verdict, what to check in the order that costs least to check, and what
  both ends counted. It was behind a compile-time flag and a console before,
  which an operator has neither of.
- **A link that was up and has been gone for 4 s says so on the same
  screen**, with this end's own counters -- what the CAN controller is doing,
  its transmit, receive and bus error counts, how many times the bus has been
  asked back, and how long the link has been quiet. Never while armed: the
  screen carries no STOP, and a bench with something spinning must not have
  its stop button covered by a diagnosis.
- **`RCBENCH.LOG` on the SD card**, one line per report while the link is
  down, the same fields in the same order every time. A tester can send a
  file rather than a photograph.
- The acknowledgement on both is a two-second hold, ARM's gesture and its
  fade. `UI_HOLD_S`, `ui_hold_fill()` and `ui_hold_flash()` move to
  `ui_widgets` and ARM is ported onto them, so the two cannot come to look
  different from each other.

### Changed

- **The coprocessor's CAN report counts the link requests it has answered**,
  not only self-test echoes -- which are zero in ordinary use and read as
  nothing arriving.
- **The bridged USB-C socket is on UART0, and a switch decides what it
  reaches.** A slide switch beside the BOOT and RESET buttons is marked
  `UART1` and `UART2`. In one position the bridge chip enumerates, names
  itself to the operating system and passes nothing in either direction,
  which is the symptom a broken cable gives. It is step 0 of
  [First run on hardware](docs/FirstRun.md) now. The header said UART0's pins
  were assumed and not traced; a board accepts a firmware download through
  that socket and the ROM bootloader takes one on UART0 and nowhere else, so
  they are not assumed any more.

## 0.4.2 - 2026-09-06

The panel could not get back on the CAN bus once it fell off it, and the
coprocessor's console could not say whether anything was arriving.

### Fixed

- **A link that dropped stayed dropped.** A CAN (Controller Area Network)
  transmitter that gets no acknowledgement retransmits on its own and adds 8
  to its transmit error counter each attempt; at 1 Mbit/s a frame nobody
  answers reaches the bus-off threshold of 256 in about 4 ms. The ESP32-S3's
  TWAI (Two-Wire Automotive Interface) controller does not recover on its own,
  and nothing asked it to: after that every transmit failed, the identity poll
  never got an answer, and the panel showed NO LINK with SIM until it was
  power-cycled. `can_twai_recover()` runs on the identity poll while the link
  is down and takes one step per call -- `twai_initiate_recovery()` from
  bus off, then `twai_start()` once the controller has counted its 128
  bus-free signals and stopped. Recovery takes up to three polls, about 3 s.

### Changed

- **The coprocessor's CAN report counts requests.** It carried `echoes
  served`, which counts self-test probe frames and is zero whenever the
  panel's CAN self-test is not running -- so on a bench with a link that had
  stopped it read as "nothing arrived" when it meant "no self-test was
  running". It now prints the link requests this end has answered as well:

  ```
  rcbench-iomcu: CAN up, 1000000 bit/s, 4271 requests served, 0 self-test echoes, tx_err 0 rx_err 0 eflg 0x00
  ```

  A count standing still while the panel says NO LINK means nothing is
  arriving; a count climbing while the panel says NO LINK means the answers
  are not getting back. No counter and no protocol register changed:
  `s_frames` is the one already published as `LINK_ST_FRAMES_LO`/`_HI`.
- The panel's link report, printed every 5 s while the link is down, carries
  the controller's own counters and ends the line in `-- BUS OFF` when it is,
  or says the controller is not running when TWAI never started. Zeros there
  read as a healthy bus for the one fault that stops everything.

## 0.4.1 - 2026-09-06

Two flash writes that stopped the bench, and the button that said a save had
happened when none had been asked for. Both were found on hardware.

### Fixed

- **Opening OUTPUTS dropped the link.** Selecting a protocol showed `NO LINK`
  and put the panel into SIM. The panel fetches a board's photograph once and
  writes it to flash as one erase of 256 kB and one write of 201 kB; a flash
  operation on the ESP32-S3 disables the instruction cache, so for its
  duration neither core runs code outside IRAM -- including the task that
  drives the heartbeat. The coprocessor saw the line still for longer than
  HEARTBEAT_MAX_GAP_MS (150 ms), failed safe and stopped answering, which is
  the interlock working. Every artwork flash operation now runs in chunks of
  SPI_FLASH_SEC_SIZE (4096 bytes) with 2 ms between them, so the heartbeat
  comes through between chunks. Reported as #99.
- **SAVE on the settings screen did nothing.** The values were written
  whenever the screen was left, and the only sign of it was the word `SAVED`
  under `RESET CATEGORY`, in the same column and at the same width -- which
  reads as a button that does not work. Reported as #100.

### Changed

- **Settings are written when SAVE is pressed, and not otherwise.** The press
  is a request: `settings_save_tick(safe)` takes it on the first frame at
  which the bench is disarmed and no board photograph is being fetched or
  stored, so the NVS (non-volatile storage) commit and its cache-off stall
  never land while the safety line is being driven. The label carries the
  whole state -- `SAVED` with nothing unwritten, `SAVE` with something to
  write, `WHEN IDLE` once asked and still waiting. Leaving the screen writes
  nothing; unsaved values are kept until the panel is switched off.
- The two doors on the settings screen, OUTPUTS and PICK A PIN, move up 16
  pixels: SAVE takes the space under RESET CATEGORY.

### Added

- The coprocessor measures the window its own flash write spends with
  interrupts off and prints it: `rcbench-iomcu: outputs saved, flash window
  <n> us`, and `out_store_last_window_us()` reads it back. The window is
  bounded by the flash part rather than by anything this firmware chooses,
  and it has never been measured on hardware; the number says whether it is a
  second cause of the same kind as #99. Nothing is compensated for it.
- `setup-dirty.png`, the twenty-ninth committed screenshot: the settings
  screen with a value changed and SAVE offered.

## 0.4.0 - 2026-09-05

A board the panel has never met is now usable without reflashing the panel:
it says which pins it has, where they are, which pads are grounds and rails,
and what it looks like.

### Added

- **A board describes itself over the link.** Four read-only pages, and each
  one degrades on its own rather than taking the others with it. `CATALOGUE`
  (0x24) carries which GPIOs the board brings out, the pad number printed
  beside each and what holds the ones an output may not have. `SHAPE` (0x25)
  carries the outline, the pitch and the corner pad 1 sits at, which is what
  turns a pad number into a position. `PADS` (0x28) carries the grounds and
  the rails, with each rail's voltage in tenths of a volt. `ARTWORK` (0x26)
  and `ART_DATA` (0x27) carry a photograph of the board, 62 bytes at a time.
  A coprocessor that serves none of them behaves as one built before them
  did: the panel offers nothing for that board, which is what it did before.
- **The pin picker**, behind PICK A PIN on the Setup screen. The board drawn
  with a button on every pin an output may have, each on a straight trace to
  its own pad, because at any size that fits a 480-pixel panel a pad is under
  40 px across and smaller than a fingertip. A pin the coprocessor reserves
  gets no button and a cross on the pad. A board that reports no shape is not
  drawn at all: a picture from a guessed form factor points at the wrong pad
  as confidently as the right one.
- **Photographs are fetched once and kept.** A 2 MB `boardart` partition in
  the panel's flash, eight slots of 256 kB. A transfer costs about ten
  seconds of link and happens once per board; it runs in 15 ms slices of each
  50 ms poll and the flash write runs on its own task, because the control
  task beats the safety line and its ceiling is 150 ms.
- `tools/gen_board_art.py` turns the PNG beside a board into a checked-in C
  array, the `gen_font.py` convention: no Python in any firmware build, and
  `--check` in CI so the two cannot drift.
- [First run on hardware](docs/FirstRun.md), a nine-step bench guide for the
  first time both boards are powered with the heartbeat wire fitted.
- The coprocessor build fails when the image would not fit the module fitted.
  The linker measures against the board file's 16 MB while the bring-up
  module has 4 MB, so it would accept an image that does not boot.

### Changed

- **Every protocol has its own pin set.** `outbind_t` held one protocol and
  one set of pins, so binding a second protocol meant unbinding the first.
  Choosing a protocol now says which set is being edited and trims nothing;
  a pin another protocol holds is drawn grey with the holder's name, which is
  a different fact from a reserved pin's red. Slots fill in pin order across
  every protocol, so a bench wired to one protocol writes the page it always
  wrote. Slots and channels are one budget of eight each, shared.
- Protocol minor 1 to 5. Every change is an added page; an older panel and a
  newer coprocessor, or the reverse, still bring the link up.
- `log_csv_analyse` 241 lines to 36 and `log_csv_build` 221 to 22, split into
  named pieces with behaviour unchanged.
- The artwork fetch sequence moved from the panel into
  `shared/artwork/art_fetch.c` with the link passed in, so the block order and
  every failure path are exercised by the host suite instead of by nothing.

### Fixed


- Both images reported firmware 0.0.0. The identity page has carried
  firmware major, minor and patch since the page map was written and nothing
  ever set them, so a board asked what it was running answered wrongly and
  answered confidently. `shared/link/include/rcbench_version.h` holds the
  three numbers once; the coprocessor publishes them and the panel prints
  what came back on its bring-up line, beside the protocol version. The panel
  is the host and publishes no identity of its own, so it puts its own build
  on the first splash line.
- `tools/check_docs.py` holds that header to the newest heading in this file,
  so a release cut without moving it fails the build rather than shipping a
  board that misreports itself.
- `outbind_learn_board()` wrote pins into its slot as it parsed, so a
  catalogue refused part way left half of itself over the board already
  learned while reporting failure.
- A board numbered from its right-hand corner had its pad row mirrored across
  the outline rather than reversed along itself, which moves the whole row by
  a hundredth of a millimetre when the centring remainder is odd.
- The picker repainted every frame: it cleared the other framebuffers on each
  paint, which with two buffers alternating never settles. Its chrome is a
  photograph, so that held the whole panel at 13 frames a second for as long
  as the screen was open.
- `link_artxfer_begin()` compared width times height times two against the
  byte count in 32-bit arithmetic. 65535 x 32769 x 2 wraps to 65534, a length
  that agrees with a plausible block count, so such a page would transfer,
  pass its checksum, and hand back 65534 bytes calling themselves a
  65535 x 32769 image.
- `art_store_put()` persisted an entry whose width and height did not match
  its byte count, which is what later code sizes a buffer from.
- `art_fetch_meta()` returned a byte count it had not validated, so a
  coprocessor disagreeing with itself got as far as an allocation before
  being refused.
- The `.clang-tidy` note said every header uses `#pragma once`. Forty do and
  eighteen use `#ifndef` guards.

## 0.3.0 - 2026-09-04

The coprocessor drives a pin, and remembers which one.

### Added

- Four output drivers on the coprocessor: servo PWM on the hardware PWM
  slices, PPM from a PIO program fed by a pair of DMA channels that retrigger
  each other, DShot, and bidirectional DShot with the turnaround and the
  reply capture inside the PIO block. `shared/dshot/` carries the frame, the
  group code, the checksum, the period-to-speed arithmetic and the sampler
  that resynchronises on every transition; `shared/ppm/` carries the frame
  layout. Both are host-tested. [docs/DShot.md](docs/DShot.md) says what has
  not been confirmed against an ESC.
- Bidirectional DShot is driver 4 on the OUTPUTS page, not a flag on driver
  3: it inverts the line and the checksum, so an ESC set up for one protocol
  ignores the other.
- CONTROL register 3, MOTOR_POLES. An ESC reports electrical periods and has
  no idea what it is bolted to, so the magnet count is the one number the
  wire has to carry for a mechanical speed to exist. The panel sends it from
  the `Motor poles` setting when the coprocessor answers; at zero the
  coprocessor reports no speed rather than one derived from a guess. Protocol
  version 2.1.
- The output bank refuses a pin the build has reserved. The pin in an OUTPUTS
  slot is whatever an operator typed, and the coprocessor reserves the safety
  line, the CAN controller's five pins, and every number above the last GPIO
  the part has.
- The coprocessor's capability word reports servo PWM, ESC drive and ESC
  telemetry, which the panel marks its menu from.
- An outputs screen behind Setup: a protocol list and a grid of the 26 GPIOs
  the header brings out, ticked to bind. Each ticked pin becomes one slot on
  the OUTPUTS page in pin order. Reserved pins are shown and refused, with
  what holds them under the name; `shared/outputs/out_bind.c` carries the
  catalogue, the rules and the mapping both ways, host-tested.
- The output binding lives in the coprocessor's flash, not the panel's. A
  binding describes wiring and the panel is not the board the wires are in, so
  the coprocessor restores it at boot and the panel reads it back when the
  link comes up. Restoring configures the outputs and does not drive them, and
  channel commands are not restored. The save waits for the bank to stop
  driving, because writing flash stops the core for longer than the
  heartbeat's window.

### Changed

- The coprocessor no longer models the bench. It publishes what it measures,
  with a valid bit per quantity and no SIMULATED flag; with no measurement
  front end fitted that is rpm from a bidirectional DShot ESC and nothing
  else, and the other fields are drawn empty. The panel still models the
  whole bench while nothing answers, and marks that with the watermark, so a
  modelled number and a measured one never appear on one screen.


### Known limitations

- No driver has been seen on a pin. Bit timings, the PPM DMA ring and the
  bidirectional turnaround are what a host test cannot reach.
- The coprocessor's flash store has never run: the erase window, the heartbeat
  re-acquiring across it, and the sector surviving a power cycle are all
  unmeasured.
- `Output` and `Output pin` are still in the Setup table and no longer do
  anything; the outputs screen replaced them.

## 0.2.1 - 2026-09-03

Arming is a deliberate gesture, and the first release cut from a tree whose
CI passes.

### Changed

- ARM is a two-second hold. The fill fades from the OK green to the danger
  red across the hold and the command goes when the fade completes, so
  letting go early arms nothing and the release itself arms nothing. A press
  is a gesture an elbow can make. Disarming stays a press: stopping never
  needs a hold.
- Arming flashes the whole button twice -- white, black, the danger red it
  settles on, and again, one drawn frame each, about 154 ms at 39 Hz. What
  was there decayed from white over 180 ms, which is a fade and reads as one.

### Fixed

- The release after a hold disarmed what that same press had just armed.
  `motor_screen_set_armed()` cleared the flag remembering that this press was
  the one that armed, and the application calls it between the hold
  completing and the finger lifting, so the release read as a fresh press on
  DISARM.
- `tools/frame_cost.py` failed ruff on an 81-character line, and had done
  since the per-screen modes were added.
- `tools/frame_cost.py --check-doc` failed on CI and not locally. Its
  tolerance is for machine-to-machine variation and was 1%, which was enough
  only while the row pattern skipped every hyphenated mode; the largest of
  them drifts about 275 fills of 22,700 between machines. It is 2%.
- The coverage table in `STATUS.md` did not follow `motor_screen.c`.

### Known limitations

- The coprocessor refuses to arm while it is connected: it reads the
  heartbeat on GP3 with a pull-down and the panel drives GPIO6 on J8, and
  nothing joins them. The interlock is working; the wire is not fitted.
- The control task has not run on hardware.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

## 0.2.0 - 2026-09-03

The panel runs the bench from a task of its own, and the screen from what is
left. Three defects that made the panel unusable on hardware are fixed, and a
multi-agent review of the result found twenty more.

### Added

- A control task pinned to the core that does not draw owns touch, STOP,
  arming, the outputs, the link and the heartbeat, at a fixed 5 ms. Drawing
  keeps `app_main`. A frame that costs 50 ms no longer rate-limits steering.
- `shared/safety/arming.c` holds the rules that decide whether the bench may
  be armed, as a state machine over timestamps with no driver, link or task,
  and `test_arming` holds nine cases over them. The policy was inside the
  control loop, which no test can compile.
- The Motor & ESC screen is laid out in two columns: the plot and the throttle
  on the left, the four readouts and the controls on a rail to the right. A
  strip above both carries the poll rate, the link's error count and the ESC,
  motor and panel-die temperatures.
- The rated kV, the revolutions per minute per volt the motor is turning, and
  EFF, the ratio of the two. The rated value comes from the connected ESC when
  one reports it, through `motor_screen_set_esc_kv()`, and from `SET_MOTOR_KV`
  otherwise. `docs/Screens.md` documents EFF as an estimate and says why.
- A percentage point at each end of the throttle track.
- `ui_slider_painted_rect()`, `ui_slider_release()` and
  `ui_slider_set_tap_to_set()`; `ui_router_stop_live()`;
  `gfx_text_rotated_points()`.
- `tools/frame_cost.py` gains a mode per screen, `-sim` and `-chrome`
  variants and `throttle`. CI holds the per-screen modes to 1,200 fills, the
  `-sim` modes to 2,800 and the `-chrome` modes to 45,000.

### Changed

- The throttle moves by the distance a finger travels rather than to where it
  lands: a press on the track commands nothing, so a touch at the far end
  cannot ask for full travel in one contact. Sliders that command nothing
  dangerous keep tap-to-set.
- A disarm returns the throttle to zero.
- An armed bench carries the danger red. ARM fades from green to red over
  350 ms while held and flashes once for 180 ms as the arm takes effect.
- The band's clock times the run rather than the panel's uptime.
- Each readout follows the bench page's flag for its own channel, so a
  coprocessor that measures nothing shows a gap rather than a zero.
- The panel loads its settings before `display_init()`.
- `CONFIG_LCD_RGB_ISR_IRAM_SAFE` and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` are
  off; `SET_MOTOR_KV` defaults to 0 rather than 920.

### Fixed

- The panel looped through the splash. The bounce-buffer refill reads PSRAM
  through the data cache, which a main-flash operation closes, and formatting
  an empty NVS partition did exactly that while the panel scanned. The core
  panicked with `Cache disabled but cached memory region accessed`.
- The picture sat 17 px to the left with each line's tail wrapped one line
  down. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` restarts the DMA every vertical
  blanking interval, and each restart empties the LCD FIFO and then resumes
  from a link that skips `LCD_LL_FIFO_DEPTH + 1` pixels the FIFO no longer
  holds.
- The SIMULATION watermark cost 8,412,078 instructions per frame, drawn at
  8.9 fps. Its stencil is the same set of pixels every frame; recording them
  once costs 144,721.
- The heartbeat stopped for up to 1000 ms. `beat()` shared a loop with a link
  exchange that spins to `LINK_HOST_TIMEOUT_MS`, against a 150 ms ceiling.
  `control_pump()` runs inside that wait.
- A latched stop could not be cleared while a coprocessor was attached: the
  latch suppresses the heartbeat, the coprocessor refuses to arm without one,
  and the latch cleared only after a successful arm.
- A tap on the splash latched STOP, which draws no STOP to press.
- A stop could be dropped by a full command queue, and by a test-then-clear
  that lost one arriving between the two.
- A drag whose release never arrived stayed latched, and a later contact
  applied its distance to a stale origin.
- The simulator and the log ran twenty times slower than the wall clock while
  the link was down, and a timed-out poll republished stale readings as a
  fresh plot column and a log row.
- The plot advanced at the frame rate rather than the sample rate.
- `s_bring.have_status` was never set, disabling two link diagnoses.
- `ui_router_invalidate()` reached six screens of ten.
- `tools/frame_cost.py --check-doc` skipped every hyphenated mode.
- The throttle survived a disarm and was re-applied on the next arm.
- The full-panel clears squared off the panels' chamfered corners, and the
  throttle readout's ghost and the slider thumb's shadow were derived from a
  background they no longer sit on.

### Known limitations

- The control task has not run on hardware. `firmware/panel/main/main.c` is
  not in the host suite.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

## 0.1.0 - 2026-09-02

First tagged release. Pre-release firmware: no output driver produces a
signal, and the control-page and settings-store changes have not run on
hardware.

### Added

- The panel writes ARM, THROTTLE and CLEAR to the coprocessor's control page:
  ARM and THROTTLE at every 50 ms poll, CLEAR on an explicit arm. STOP,
  DISARM and a dead touch controller write ARM = 0. The band shows the
  coprocessor's fault bits.
- The panel initialises settings from the schema and loads and saves them in
  NVS (non-volatile storage) through the new `settings_nvs` component.
- `README-de.md`, a German front page, with a language switch at the top of
  both READMEs.
- `CHANGELOG.md`.
- `tools/check_docs.py` checks the README pair, the pages under `hardware/`,
  the `Who compiles what` table against the three build files, and the
  screenshot count in `STATUS.md`.

### Changed

- The wiki pages, both READMEs, `STATUS.md`, `CONTRIBUTING.md`, `SECURITY.md`
  and the `hardware/` pages describe the system in its current state, without
  development history. Stale statements corrected: the module tree, the compile
  table, the console configuration, the coprocessor board, the Performance
  table, and the bit-timing figures for both CAN (Controller Area Network)
  controllers.
- `STATUS.md` lists the open items with what each needs, and the order of work
  with the state of each step.

### Fixed

- `test/host/test_settings.c` defined its geometry macros twice.

### Known limitations

- The control page writes and the NVS (non-volatile storage) settings store
  have not run on hardware.
- No output driver produces a signal on a pin.
