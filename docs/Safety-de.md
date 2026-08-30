# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Wie der Prüfstand anhält, was dafür gebaut werden muss, und welche
Verhaltensweisen Absicht sind — damit niemand gegen sie ankämpft.

## Drei unabhängige Wege, auf denen er anhält

| | Was damit abgedeckt ist |
| --- | --- |
| **Das STOP-Kommando** über den Link | ein bewusster Stopp, quittiert und gemeldet |
| **Der Heartbeat bleibt aus** | das Panel hängt, wurde resettet, hatte einen Brown-out oder ist abgesteckt |
| **Der Stille-Watchdog des Koprozessors** | der Link ist in einer Richtung tot |

Ein gedrücktes STOP nutzt die ersten beiden gleichzeitig: es sendet das
Kommando **und** stoppt den Heartbeat, statt sich auf einen Weg allein zu
verlassen.

## Was gebaut werden muss: das Monoflop

Die Sicherheitsleitung ist **GPIO6, und sie trägt Flanken** — keinen Pegel.
Das Output Enable des Koprozessors und der Leistungspfad für Servos und ESC
gehören hinter ein **retriggerbares Monoflop**, das nur angezogen bleibt,
solange Flanken eintreffen. Absturz, hängender Task, Reset, Brown-out und
abgestecktes Kabel sehen dann alle gleich aus: keine Flanken, kein Ausgang.
In Hardware, unabhängig von der Firmware an beiden Enden.

> **Das Fenster des Monoflops auf etwa 150 ms auslegen.** Der Heartbeat kommt
> aus der Render-Schleife des Panels, und die liefert je nach Last alle
> 26–52 ms eine Flanke — ein 50-ms-Fenster würde die Ausgänge bei jedem
> zweiten Frame eines belasteten Prüfstands abwerfen. 150 ms liegt trotzdem
> deutlich innerhalb des 200-ms-Link-Failsafes des Koprozessors. (Ein früher
> genannter Wert von 20–50 ms stammt aus der Zeit vor der gemessenen Frame
> Rate und gilt als zurückgezogen.)

Die Firmware prüft den Heartbeat zusätzlich, denn ein Monoflop kann Heartbeat
und Rauschen nicht unterscheiden — alles, was schnell genug flankt, triggert
es nach. Die Zahlen, alle aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | | |
| --- | ---: | --- |
| Das Panel erzeugt eine Flanke alle | 26–52 ms | eine pro Frame |
| Die Firmware akzeptiert Abstände von | **4–150 ms** | schneller ist Rauschen; langsamer heißt, das Panel zeichnet nicht mehr |
| Die Firmware vertraut der Leitung nach | **4 guten Abständen** | rund einer Zehntelsekunde |

Diese Prüfung ist mit Absicht asymmetrisch — langsam beim Vertrauen, sofort
beim Zweifeln. Vier gute Abstände, bevor die Leitung als lebendig gilt; ein
einziger schlechter, oder ein Fenster Stille, und das Vertrauen ist sofort
weg. Eine Verriegelung, die auf einen Glitch hin freigibt und beim Sperren
zögert, wäre das Gegenteil ihres Namens.

## Verhaltensweisen, die Absicht sind

Dinge, die der Prüfstand tut und die nach Fehlern aussehen können — es sind
keine:

- **STOP rastet ein.** Nach einem Stopp bleibt der Prüfstand entschärft, bis
  bewusst wieder scharf geschaltet wird. Bildschirmwechsel, Warten oder ein
  zurückkehrender Link ändern daran nichts.
- **Den Prüfstandsbildschirm zu verlassen entschärft.** Wer von einem scharfen
  Prüfstand wegnavigiert, darf keinen drehenden Propeller hinter einem
  Bildschirm zurücklassen, auf dem er nicht mehr zu sehen ist.
- **Ein stummer Touch-Controller entschärft und blockiert das Schärfen.**
  Antwortet der Touch 500 ms lang nicht, wird entschärft und das Schärfen
  verweigert — das Panel ist der einzige Ort mit einem STOP-Knopf. Ein
  Prüfstand, der sich nicht anhalten lässt, ist nicht scharf: er ist
  durchgegangen.
- **Nach einem Link-Failsafe schaltet sich der Prüfstand nicht von selbst
  wieder scharf.** Zurückkehrender Verkehr beweist, dass der Link lebt — nicht,
  dass jemand weitermachen wollte. Nach behobener Ursache am Panel neu
  schärfen.
- **Der Koprozessor fragt nicht.** Überstrom, Übertemperatur, Stall-Timeout,
  toter Link: er schützt die Hardware aus eigener Befugnis und meldet beim
  nächsten Poll, was er getan hat. Auf die Erlaubnis, sicher auszufallen,
  wartet er nie.

## Warum ein Heartbeat und keine Enable-Leitung

Ein statisches Enable — Pin high, solange laufen erlaubt ist — versagt genau
beim wahrscheinlichsten Fehler: die Firmware bleibt hängen, der Pin bleibt
high, die Ausgänge bleiben scharf. Ausgerechnet der Fall, in dem der
Prüfstand am dringendsten anhalten soll, ist der, den ein Pegel nicht
ausdrücken kann. Flanken dagegen müssen immer wieder neu erzeugt werden —
bleiben sie aus, fällt der Ausgang von selbst ab.

Zwei Regeln halten den Heartbeat ehrlich. Er wird **von der Schleife erzeugt,
die den Touch liest und STOP zeichnet** — nie von einem Timer, einer
DMA-gespeisten Peripherie oder einem eigenen Task. So ein Signal bewiese
nämlich, dass das Falsche lebt: eine Peripherie kann tadellos weitertakten,
während die Anwendung längst hängt. Und die Firmware prüft die **Periode**,
nicht nur, dass Flanken da sind — das ist die Prüfung, die eine
kurzgeschlossene oder klingelnde Leitung nicht bestehen kann.

Das Panel selbst hat keine Leitung zu irgendeinem Ausgang. Es kann keinen
Motor drehen — nicht per Vereinbarung, sondern weil es an nichts angeschlossen
ist, was das könnte.
