# Bildschirme

<sub>[English](Screens.md) · **Deutsch**</sub>

Den Prüfstand bedienen: was immer auf dem Bildschirm ist, was die
Kennzeichnungen im Menü bedeuten, und wie jeder Bildschirm benutzt wird.

## Das Statusband

Der obere Streifen gehört auf jedem Bildschirm dem Prüfstand. Von rechts nach
links: Linkzustand, Ausgangsmodus, scharf oder nicht, die Laufzeit, und STOP.

**STOP funktioniert immer.** Es entschärft von jedem Bildschirm aus, und es
rastet ein: der Prüfstand bleibt gestoppt, bis du bewusst wieder scharf
schaltest. Nichts anderes löst einen Stopp — nicht wegnavigieren, kein
ablaufender Hinweis, nicht der zurückkehrende Link.

ARM sitzt unten an einem Prüfstandsbildschirm und STOP oben im Band, einen
ganzen Bildschirm auseinander, damit der Griff nach dem einen nicht das andere
findet.

## Was die Kennzeichnungen im Menü bedeuten

![Das Funktionsmenü](img/overview.png)

| Kennzeichnung | Bedeutung |
| --- | --- |
| **SOON** | den Bildschirm gibt es noch nicht |
| **MODELLED** | den Bildschirm gibt es und er funktioniert, aber seine Hardware ist nicht bestückt — jede Zahl darin ist erfunden, und der Bildschirm sagt das |
| nichts | das Bauteil ist da und die Messwerte sind echt |

MODELLED korrigiert sich selbst: die Kennzeichnung kommt aus dem, was der
Koprozessor als tatsächlich bestückt meldet, sie verschwindet also in dem
Moment, in dem das Bauteil daran ist. Abwesend heißt nicht verboten — ein
Bildschirm, dessen Hardware fehlt, öffnet weiterhin und funktioniert aus dem
Modell, mit der Kennzeichnung, die das sagt.

Das Menü gibt es auch im hellen Theme:

![Das Menü im hellen Theme](img/overview-light.png)

## Der Splash ist der Selbsttest

![Der Splash](img/splash.png)

Jedes Subsystem meldet beim Hochkommen sein eigenes Ergebnis — ein Board ohne
Karte, ein Touch-Controller, der nicht geantwortet hat, oder ein Koprozessor
mit der falschen Protokollversion sagt es also hier, statt drei Bildschirme
später rätselhaft kaputt auszusehen. Ein Fehlschlag wird trotzdem gemeldet und
ist keine Sackgasse: lesen und weitergehen. Sobald jeder Schritt geantwortet
hat, hält die Liste kurz und übergibt ans Menü; ein Tippen überspringt das
Halten.

## Motor & ESC

![Motor und Regler](img/motor.png)

Vier Traces auf eigenen Skalen, das Gas auf dem Slider, Peak-Werte unter den
Siebensegment-Anzeigen. Erst ARM lässt den Slider etwas treiben; DISARM und
STOP halten beide sofort an, ohne Rampe. RESET PEAKS löscht die Peak-Marken
und lässt die Live-Werte in Ruhe.

Solange nichts Echtes angeschlossen ist, läuft der Bildschirm aus dem Modell
und sagt das — SIMULATION liegt quer über dem Plot. Es verschwindet in dem
Moment, in dem echte Zahlen ankommen, und dafür braucht es keinen extra
Sensor: ein ESC mit Telemetrie (KISS, BLHeli_32, OpenYGE) oder Bidirectional
DShot meldet Spannung, Strom, Verbrauch, RPM und Temperaturen über die
Signalleitung selbst.

## Servo

![Servo](img/servo.png)

Irgendwo auf dem Bogen ziehen, und das Horn folgt. Der kräftige Arm ist die
**gemessene** Stellung; der blasse Arm dahinter ist, was du befohlen hast.
Wenn die beiden auseinandergehen, ist dieser Abstand der Verzug des Servos
selbst — der Bildschirm fügt keinen eigenen hinzu, ein langsames Servo zeigt
sich also als zwei Arme und nicht als träges Bild. Die Ringe um die Spitze
pulsieren, solange das Servo aktiv getrieben wird.

## Analyser

![Analyser](img/analyser.png)

Sechzehn Kanäle, jeder mit seinen letzten anderthalb Sekunden Verlauf und
einem Balken für den Stand jetzt. Beweg ein Bedienelement am Sender, und der
Kanal, den du bewegt hast, ist der mit der Stufe in seiner Spur — das ist die
Frage, für die dieser Bildschirm gebaut ist. Die Balken beantworten die
andere: wie weit, gerade jetzt, ohne eine Zahl zu lesen. CH17 und CH18 sind
die digitalen Kanäle.

Ein Glitch zeigt sich als Spitze in einer Spur. Ein Dropout als Kerbe über
alle sechzehn im selben Augenblick.

Der Zustandsblock ist das Größte auf dem Bildschirm, weil der Empfänger lügen
kann:

![Ein Empfänger im Failsafe](img/analyser-failsafe.png)

Ein Empfänger im Failsafe sendet sechzehn einwandfrei geformte Werte, die er
sich auf Ansage ausgedacht hat. Der Prüfstand färbt jede Spur rot und sagt
FAILSAFE in Worten. **Failsafe als Stopp behandeln, nie als sechzehn gültige
Zahlen** — genau danach sehen sie aus. Vier Zustände, jeder mit einer Zeile,
die sagt, was er bedeutet: SILENT, FAILSAFE, FRAME LOST und LIVE.

## Programmierer

Programmieren geht der Reihe nach: was du programmierst, welches Protokoll es
spricht, und erst dann verbinden.

![Was programmierst du](img/programmer.png)

Die Klasse zu wählen heißt zu wählen, welches Kabel du in der Hand hast; das
Protokoll zu wählen heißt zu wählen, was du hineinsprichst. Jede
Protokollzeile benennt ihren Transport — eine Autoerkennung gibt es nicht:

![Die Protokolle einer Klasse](img/programmer-protocols.png)

**BLHeli_32 steht nicht in der ESC-Liste.** Seine Einstellungen sind
verschlüsselt und der Key ist nicht öffentlich — der Prüfstand erkennt und
treibt diese Regler (Drehrichtung, 3D-Modus, Beacon und Save-Settings laufen
als DShot Special Commands), kann aber keinen Parameter benennen. [Die ganze
Antwort](BLHeli32-de.md).

Bis ein Gerät antwortet, ist nichts editierbar:

![Nichts hat geantwortet](img/programmer-idle.png)

Sobald eines geantwortet hat, erscheinen die Parameter gruppiert, und die
Hilfe der ausgewählten Zeile steht unter der Liste:

![Verbunden](img/programmer-params.png)

Jede Firmware zeigt ihre Einstellungen in ihren eigenen Einheiten — BLHeli_S
legt das Timing in benannte Stufen, alles danach in Grad Vorzündung:

![Grad statt benannter Stufen](img/programmer-am32.png)

**Geändert ist nicht geschrieben.** Eine vorgemerkte Änderung trägt eine
Markierung und eine eigene Farbe, und der WRITE-Knopf zählt, wie viele
vorgemerkt sind. Ein Wert, den du editiert hast, sieht nie aus wie einer, der
von der Hardware gelesen wurde:

![Zwei vorgemerkte Änderungen](img/programmer-dirty.png)

Stepper halten an ihren Enden an, statt umzulaufen — an einem Regler ist das
falsche Ende einer umlaufenden Liste eine Drehrichtung.

Eine Ebene zurück trennt die Verbindung. Das Gerät, das einen Bootloader
beantwortet hat, ist nicht das, das eine CLI beantworten wird, und der
Bildschirm zeigt nicht die eine Identität über den Parametern des anderen.
Back steigt eine Ebene auf einmal; das Tag im Band verlässt den Bildschirm.

## Akku

![Zellenabweichung](img/battery.png)

Zellen werden als Abweichungen von ihrem eigenen Mittelwert gezeichnet, denn
die Zahl, auf die es ankommt, ist die **Spreizung** — der größte Abstand
zwischen zwei beliebigen Zellen. Das Urteil folgt ihr: HEALTHY unter 30 mV,
WATCH ab 30, REPLACE ab 60.

Spreizung ist zwischen Zellen, nie gegen einen Nennwert. Ein Akku, der leer
aber gleichmäßig ist, ist ein entladener Akku; ein Akku, der voll aber
ungleichmäßig ist, ist ein kaputter — und nur der zweite geht diesen
Bildschirm etwas an.

Die Skala folgt dem Akku, bis zu einer Untergrenze von 12 mV, und steht neben
dem Plot — ein Balken, der den Plot füllt, könnte sonst vier Millivolt sein
oder vierzig.

**Unter Last messen.** In Ruhe sieht eine müde Zelle aus wie jede andere.

## Logs

![Der Dateibrowser](img/logs.png)

Die Karte durchsehen, eine Datei öffnen, prüfen, was der Import erkannt hat
und woran, dann plotten:

![Die Importansicht](img/logs-import.png)
![Der Plot](img/logs-plot.png)

CSV in mehreren Dialekten wird tolerant gelesen — Dezimalkommas
eingeschlossen —, und was der Reader entschieden hat, wird gezeigt, bevor du
dich darauf verlässt.

## Setup

![Setup](img/setup.png)

Die Einstellungen liegen hinter der SETUP-Kachel, in beiden Themes:

![Setup im hellen Theme](img/setup-light.png)

## Auswuchten

Auf eine eigene Seite umgezogen: [Auswuchten](Balance-de.md). Die Messung ist
der leichte Teil — wo die Sensoren hinkommen, entscheidet, ob die Antwort
etwas bedeutet.

## Ein Bildschirm, der seine Aufgabe noch nicht kann, sagt warum

Eine Kachel, die nicht fertig ist, sagt nicht „demnächst"; sie benennt, was
sie tun wird, und die eine Entscheidung oder das eine Bauteil, das zuerst
ankommen muss — das Menü ist damit zugleich die To-do-Liste, und es verspricht
nie eine Messung, die der Prüfstand nicht machen kann.
