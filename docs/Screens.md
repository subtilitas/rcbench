# Screens

<sub>**English** · [Deutsch](Screens-de.md)</sub>

The bench is eight tiles behind a menu, plus a splash that tells you what came
up. This page covers the shell; each bench gets its own page as it is built.

## Why a status band, when the predecessor deliberately had none

The predecessor gave every screen its whole 800×480 and shared nothing but a
home tag, on the argument that a log viewer, a settings list and a live plot
want very different things from their top edge. That argument still holds for
the *body* of a screen, and is why the band is only 48 px.

What overturned the rest of it: **more than one screen can now arm something.**
STOP has to be in the same place everywhere, and a band is horizontal — the
cheap direction on a panel whose frame rate is bandwidth-bound, see
[Performance](Performance.md).

![The feature menu](img/overview.png)

### The menu says what the bench can do

Three states, not two. **SOON** is a screen that does not exist yet. **MODELLED**
is one that does, whose hardware is not fitted -- it opens, it works, and every
number in it is invented. Nothing at all means the part is on and the readings
are real.

That middle state used to be indistinguishable from the third, so the menu
quietly promised measurements the bench could not take. It now comes from
`LINK_ID_CAPABILITIES`, a bitmap the coprocessor fills from what is actually
soldered on, which today is nothing.

This replaced a page of checkboxes for turning features off, and the reason is
worth keeping. On a diagnostic instrument a preference that makes the tool
*not look* is dangerous: untick a receiver bus, plug one in six months later,
and the analyser reports nothing on the wire with no way to find out why. It
would also have put "I do not want this" and "this is not fitted" behind one
widget, and those must never be confused -- the second is a fact, and it
corrects itself the moment the part goes on.

Absent is not forbidden. A screen whose capability is missing still opens and
still works from the model, wearing the mark that says so.

Right to left, so no item has to guess another's width: link state, the output
mode, armed, the run clock, and STOP. **STOP is global and always live** — it
disarms from any screen, so it never needs explaining.

One consequence is deliberate: **ARM lives at the bottom of a bench and STOP at
the top of the band**, separated by the full height of the panel, so reaching
for one cannot find the other.

### The band is enforced, not agreed

The router owns the top 48 px and hands each screen a **sub-canvas** of what is
left, via `gfx_canvas_sub`. Not by convention: physically. Every screen begins
by clearing its canvas, so a screen handed the whole panel would erase STOP on
the way past — silently, and looking like a cosmetic glitch rather than the
safety problem it is. `test_nav` asserts the band's own pixels survive a full
render of every screen.

Screens also work in their own coordinates. The offset is removed once, in the
router, rather than remembered in nine places.

## The splash is the self-test

![The splash](img/splash.png)

Not decoration. Each subsystem reports its own result as it initialises, so a
board with no card, a touch controller that did not answer, or a coprocessor
speaking a protocol version this panel does not, says so on the way past —
rather than looking mysteriously broken three screens later.

A **failure still counts as reported**: you must be able to read it and move
on, not be stranded here. Once every step has answered, the list holds briefly
and hands over to the menu; a tap skips the hold.

## The menu is things, not capabilities

The catalogue runs to sixty-odd entries across measure, drive, listen, program
and compute. A menu of *capabilities* would be a filing system, and a workshop
tool is not one. A tile is **a physical object you have in front of you**, and
the benches combine measure, drive and listen for that one object.

Five are live. Three are named, routed and honest:

![Setup](img/setup.png)

Built, and the seven screen cases that were held back while it was re-cut are
back with it. In the light theme too, because a palette change that only
breaks one theme is the kind that ships:

![Setup in the light theme](img/setup-light.png)

Both themes are committed, because a palette change that only breaks one theme
is the kind that ships:

![The menu in the light theme](img/overview-light.png)

## A screen that cannot do its job yet says why

A "coming soon" panel teaches nobody anything. One that names what it will do,
and the single decision or part that has to arrive first, is a to-do list
somebody can answer.

![Motor and ESC](img/motor.png)

And the note turns **green** when nothing is blocking it — which is a different
and less comfortable position than being blocked:

![Logs](img/logs.png)

Which is now built: browse the card, see what the import view detected and the
evidence behind it, then plot.

![The import view](img/logs-import.png)
![The plot](img/logs-plot.png)

The layout moved up 40 px on the way in. It was drawn for a screen that owned
all 480 and painted its own home tag in the top 40; the router owns both now
and hands it a 432 px window, so without the shift the footer ran from 430 to
472 in a canvas 432 tall and RESCAN, OPEN and PLOT could not be pressed at
all.

The servo bench is commanded by its horn: drag anywhere on the sweep and the
arm points there. The case beside it is not the dial, and neither is the middle
of the boss, which is not a direction.

![Servo](img/servo.png)

The arm follows the *measured* position and not the commanded one, with no
easing of its own. Easing towards a measurement would add the bench's lag on
top of the servo's, and once drawn the two are indistinguishable -- a slow
servo and a slow screen look identical, and only one of them is under test. So
the commanded position is drawn faintly behind the measured one, and lag shows
as two arms rather than as a number disagreeing with a picture.

The analyser is two thirds history and one third now. The question it is asked
is "I moved that -- which channel was it?", and no arrangement of current
values can answer it, because the information is in the movement. Each channel
keeps the last second and a half, so the one that moved is the one with a step
in it, and the eye finds a step among fifteen flat lines without being told
where to look. The bar beside each trace answers the other question -- how far
is it now -- without reading a number.

![Analyser](img/analyser.png)

It is also the only arrangement that separates the two faults worth catching: a
glitch is a spike in one lane, a dropout is a notch across all sixteen at the
same instant.

The state block is the largest thing on that screen, and this is why:

![A receiver in failsafe](img/analyser-failsafe.png)

A receiver in failsafe sends sixteen perfectly well-formed channel values that
it was told to invent. Every trace and every bar turns red and the screen says
so in words, because a bench that draws invented numbers the same colour as
measured ones is helping somebody trust them. Four states, not two: silent,
failsafe, frame lost and live, each with a line saying what it means.

The programmer is not one programmer. BLHeli_S and AM32 speak a one-wire
bootloader at 19,200; ESCape32 answers a text CLI; VESC wants framed packets; a
Hitec D-series servo has its own thing entirely. They share a connector and
nothing else.

So it asks in order: what are you programming, which of its protocols, and only
then what answered.

![What are you programming](img/programmer.png)

Flat, the five sat in one row and the screen quietly claimed that a servo
protocol and four ESC ones were the same kind of choice. They are not. Picking
the class is picking which lead is in your hand; picking the protocol is
picking what to say down it, and every row names its own transport because
that is the whole reason the list exists rather than an autodetect.

![The protocols of a class](img/programmer-protocols.png)

**BLHeli_32 is not in that list, and the reason is not effort.** Its settings
are a 256-byte XTEA block whose key exists only inside a closed binary, so the
bench can connect to one of these ESCs, identify it and read the block without
being able to name a single value in it. Direction, 3D mode, beacon and
telemetry still work, because those are DShot special commands rather than
parameters. [The whole reasoning](BLHeli32.md).

Stepping back up drops the connection. The device that answered a bootloader is
not the one that will answer a CLI, and leaving the old identity above a new
list is the single lie this screen must not tell. Back climbs one level rather
than leaving the screen -- the band's own tag does that.

![Nothing has answered](img/programmer-idle.png)

## The parameters draw themselves

A definition says what kind of thing it is -- a switch, a choice, a number on a
range -- and the renderer owns one widget per kind. Nothing in the drawing code
knows what BLHeli_S is; it knows what a bounded number looks like. Adding a
firmware is a table, and the only way to need new drawing code would be to
invent a kind of setting that none of these have, which in twenty years of ESCs
nobody has.

![Connected](img/programmer-params.png)

Timing is the clearest evidence for that. BLHeli_S puts it in named steps
because that is what its own configurator does; every firmware after it settled
on degrees of advance, nought to about thirty. Two representations of one
physical quantity, in one list, drawn by the same code -- the definition says
which kind it is, and that is the whole of the difference:

![Degrees rather than named steps](img/programmer-am32.png)

That is how the real configurators do it, and the reason they are right is
worth stating rather than only that they agree: a screen with one widget per
setting drifts, because the fortieth setting is written by somebody in a hurry.
A screen with three widgets cannot. BLHeli's configurator has exactly three --
checkbox, select, number -- and AM32's groups them; both were read before this
was built.

Two more things came from reading them. VESC Tool gives every parameter a
button that explains what it does, which is why the selected row's help sits
under the list: "Demag compensation" is four syllables and no information.
And Hitec's DPC-11 divides its screen into Connection, File Operations, Testing
and Programming -- the *Testing* section is the one thing a PC configurator
sells that this bench could do properly, since it can change a servo's
endpoints and then exercise the servo. That is not built yet.

## Changed is not written

![Two staged edits](img/programmer-dirty.png)

Every configurator worth using separates what was read, what has been changed,
and what has been written. A value edited into a row that looks exactly like a
value read off the hardware is the measured-versus-invented mistake wearing
other clothes, and this bench has opinions about that. So a staged change wears
a mark and its own colour, and the button counts them.

Steppers clamp rather than wrap. A parameter that rolls from its last value
round to its first will one day be set to the wrong end by somebody pressing
once more than they meant to, and on an ESC the wrong end is a direction.

## Balancing starts with where the sensors go

The measurement is easy and the mounting is not. A balance answer is a
magnitude and an angle -- add this much, there -- and both come out of two
sensors whose placement decides whether the answer means anything. Neither way
of getting it wrong looks wrong on screen: a vibration sensor on a compliant
mount reads a filtered version of what the bearing did, and an index mark on a
blade reads one pulse per blade and gives an angle out by a whole blade
spacing.

The answer is an angle from the index mark, and an angle is only actionable
once you know which blade it is near: "0.35 g at 265 degrees" is a number, and
"between blades two and three" is an instruction. So the blade count is asked
for, and the screen does that arithmetic.

![The rotor and where the mass goes](img/balance.png)

A ducted fan is a different job and says so. You cannot reach a blade tip
inside a duct, so the correction is given as an angle on the hub rather than as
a blade to tape:

![A five-blade fan](img/balance-edf.png)

![Where the sensors go on a rig](img/balance-rig.png)

Two details in there are the ones people get wrong. The index mark goes on the
motor's **bell**, not on a spinner: a rig often runs without one, and a beam
aimed at a spinner's nose has to come from in front, across the disc -- the
same sighting that ends up counting blades. The bell turns with the shaft, is
rigid, is there whichever prop is fitted, and can be watched from underneath
where nothing is in the way.

And the mark is a **pen line**, not reflective tape. Anything stuck to the bell
is mass, on the one part of the machine whose mass is the thing being
measured -- you would be balancing out your own marker.

The rig is the easy case, because the placement is under your control. On an
aircraft almost none of it is:

![Where the sensors go on an aircraft](img/balance-aircraft.png)

The **cowl is not the firewall**. It is a fairing, screwed to a former and
often on rubber, free to move relative to the thing whose vibration you want.
The firewall is the one rigid face at that end of the model, and the
accelerometer goes flat against it -- which is worth saying, because a
three-axis part mounted flat puts two of its axes in the firewall's plane,
across the shaft. Use one of those and ignore the third.

The mark still goes on the bell, and here there is a second reason for it: **a
spinner comes off**. Every time it is taken away for transport it goes back on
at a new angle, and the phase reference the last balance was measured against
goes with it. The bell is part of the rotor and never moves. On a great many
electric models the outrunner stands proud of the cowl anyway, which is what
makes the same arrangement as the rig possible -- sensor underneath, looking
straight up, crossing nothing.

And the whole aeroplane has to be tied down. A machine free to rock is a spring
nobody chose, in series with everything you are trying to measure.

Two things about the delay, since the choice of sensor turns on it. Without an
index pulse there is no phase reference at all, so balancing is the four-run
method -- baseline, then one trial mass at nought, a hundred and twenty, and
two hundred and forty degrees -- which never measures phase and therefore
cannot be hurt by a constant lag. What it needs instead is *bandwidth*: at
10,000 rpm the fundamental is 167 Hz, and a packaged IMU streaming fused
orientation at 100 Hz cannot see it at all. It is not late, it is blind.

With an index pulse the runs halve, and then the lag matters -- but only its
variability. A constant delay cancels in the influence coefficient, because the
trial run measures the whole chain including the sensor. An analogue part into
the coprocessor's own converter is about fifteen microseconds, which is under a
degree at 10,000 rpm; a fused IMU is five milliseconds and neither constant nor
published, which is three hundred degrees and a mass fitted with great
precision in the wrong place.

## The pack, arranged as a verdict

A column of cell voltages is easy to draw and hard to read: six numbers that
agree to two decimals, one of which is quietly forty millivolts adrift. The
quantity that matters is the **spread**, so the cells are drawn as departures
from their own mean and the number that decides the verdict is the widest gap
between any two of them.

![Cell divergence](img/battery.png)

Spread is between cells, never from a nominal. A pack that is flat but even is
a discharged pack; a pack that is full but uneven is a broken one, and only the
second is this screen's business.

The scale follows the pack, with a floor. A fixed hundred-millivolt scale draws
a healthy pack as six flat lines and a sick one as five flat lines and a stub,
which is the same picture -- so the range is the pack's own worst departure
with a little headroom, and the scale is printed beside it, because a bar that
fills the plot could otherwise be four millivolts or forty.

And it is measured under load. At rest a tired cell looks like every other one.
