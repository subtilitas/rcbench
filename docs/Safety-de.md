# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Wie der Prüfstand anhält, was du dafür bauen musst, und die Verhaltensweisen,
die dir auffallen werden und gegen die du nicht ankämpfen solltest.

## Drei unabhängige Arten, wie er anhält

| | Was es fängt |
| --- | --- |
| **Das STOP-Kommando** reist über den Link | ein bewusstes Anhalten, quittiert und gemeldet |
| **Der Heartbeat hört auf** | das Bedienteil hängt, ist resettet, im Brown-out oder abgezogen |
| **Der eigene Stille-Watchdog des Koprozessors** | der Link ist in einer der Richtungen tot |

Ein gedrücktes STOP nutzt die ersten beiden gleichzeitig: es sendet das
Kommando **und** hält den Heartbeat an, statt einem von beiden allein zu
trauen.

## Was du bauen musst: das Monoflop

Die Sicherheitsleitung ist **GPIO6, mit Flanken** — kein Pegel. Das Output
Enable des Koprozessors und der Servo-/ESC-Leistungspfad gehören hinter ein
**retriggerbares Monoflop**, das nur bestromt bleibt, solange Flanken
ankommen. Ein Absturz, ein hängender Task, ein Reset, ein Brown-out und ein
abgezogenes Kabel zeigen sich damit identisch: keine Flanken, kein Ausgang. In
Hardware, unabhängig von der Firmware an beiden Enden.

> **Das Fenster des Monoflops auf rund 150 ms auslegen.** Der Heartbeat kommt
> aus der Render-Schleife des Bedienteils, die je nach Last alle 26–52 ms eine
> Flanke liefert — ein 50-ms-Fenster ließe die Ausgänge bei jedem zweiten
> Frame eines belasteten Prüfstands fallen. 150 ms feuert immer noch deutlich
> innerhalb des 200-ms-Link-Failsafes des Koprozessors. (Ein früherer Wert von
> 20–50 ms stammt aus der Zeit vor der gemessenen Frame Rate; betrachte ihn
> als zurückgezogen.)

Die Firmware prüft den Heartbeat zusätzlich, denn ein Monoflop kann einen
Heartbeat nicht von Rauschen unterscheiden — alles, was schnell genug flankt,
triggert es nach. Die Zahlen, alle aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | | |
| --- | ---: | --- |
| Das Bedienteil erzeugt eine Flanke alle | 26–52 ms | eine je Frame |
| Die Firmware akzeptiert einen Abstand von | **4–150 ms** | schneller ist Rauschen; langsamer heißt, das Bedienteil hat aufgehört zu zeichnen |
| Die Firmware traut der Leitung nach | **4 guten Abständen** | ~einer Zehntelsekunde |

Diese Prüfung ist bewusst asymmetrisch — langsam im Vertrauen, sofort im
Zweifel. Vier gute Abstände, bevor die Leitung lebendig genannt wird; ein
schlechter Abstand oder ein stilles Fenster nimmt es sofort wieder. Ein
Interlock, das bei einem Glitch freigibt und beim Sperren zögert, wäre das
Gegenteil dessen, wonach es benannt ist.

## Verhaltensweisen, die Absicht sind

Dinge, die der Prüfstand tut und die wie Fehler aussehen können, aber keine
sind:

- **STOP rastet ein.** Nach einem Stopp bleibt der Prüfstand entschärft, bis
  du wieder scharf schaltest. Herumnavigieren, Warten oder ein zurückkehrender
  Link schalten nicht wieder scharf.
- **Einen Prüfstandsbildschirm zu verlassen entschärft.** Von einem scharfen
  Prüfstand wegzunavigieren darf keinen drehenden Propeller hinter einem
  Bildschirm zurücklassen, auf dem man ihn nicht mehr sieht.
- **Ein toter Touch-Controller entschärft und blockiert das Schärfen.** Wenn
  der Touch 500 ms lang nicht antwortet, entschärft der Prüfstand und weigert
  sich, scharf zu schalten — denn das Bedienteil ist der einzige Ort, an dem
  es einen STOP-Knopf gibt. Ein Prüfstand, den man nicht anhalten kann, ist
  nicht scharf — er ist ausgebrochen.
- **Nach einem Link-Failsafe schaltet sich der Prüfstand nicht selbst wieder
  scharf.** Zurückkehrender Verkehr beweist, dass der Link lebt — nicht, dass
  jemand weitermachen wollte. Vom Bedienteil aus wieder scharf schalten,
  sobald die Ursache behoben ist.
- **Der Koprozessor handelt, ohne zu fragen.** Überstrom, Übertemperatur,
  Stall-Timeout, verlorener Link: er schützt die Hardware aus eigener Befugnis
  und meldet beim nächsten Poll, was er getan hat. Er wartet nie auf die
  Erlaubnis, sicher zu versagen.

## Warum ein Heartbeat und keine Enable-Leitung

Ein statisches Enable — Pin high, solange Laufen erlaubt ist — versagt beim
wahrscheinlichsten Fehler: die Firmware bleibt mit dem Pin auf high hängen,
und die Ausgänge bleiben scharf. Der eine Fall, in dem der Prüfstand am
dringendsten anhalten soll, ist genau der, den ein Pegel nicht ausdrücken
kann. Flanken laufen von selbst ab.

Zwei Regeln halten den Heartbeat ehrlich. Er wird **von der Schleife
getrieben, die den Touch liest und STOP zeichnet** — nie von einem Timer,
einer DMA-gefütterten Peripherie oder einem eigenen Task, denn so ein Signal
beweist, dass das Falsche lebt: eine Peripherie kann tadellos weitertoggeln,
während die Anwendung hängt. Und die Firmware prüft die **Periode**, nicht nur
das Vorhandensein von Flanken, denn das ist die Prüfung, die eine
kurzgeschlossene oder klingelnde Leitung nicht bestehen kann.

Das Bedienteil selbst hat keine Leitung zu irgendeinem Ausgang: es kann keinen
Motor drehen, weder per Konvention noch aus Versehen, weil es von ihm zu
nichts, was das könnte, eine Leitung gibt.
