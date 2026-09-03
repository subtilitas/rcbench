# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Wie der Prüfstand anhält, welche externe Schaltung der Entwurf voraussetzt,
und welche Verhaltensweisen Absicht sind.

## Stoppmechanismen

| Mechanismus | Deckt ab | Stand |
| --- | --- | --- |
| Der Heartbeat bleibt aus | das Panel hängt, ist resettet, hat einen Brown-out oder ist abgesteckt; ein gedrücktes STOP | an beiden Enden erzeugt und überwacht; das Monoflop, das er steuert, ist nicht bestückt |
| Stille-Watchdog des Koprozessors, 200 ms | der Link ist in einer der beiden Richtungen tot | gebaut und getestet |
| STOP-Kommando über den Link | ein bewusster Stopp, quittiert und gemeldet | geschrieben; nicht auf Hardware gelaufen |

Ein gedrücktes STOP stoppt den Heartbeat, entschärft das eigene
Ausgangsmodell des Panels und schreibt ARM = 0 auf die Control-Page. Bei
stehendem Link schreibt das Panel ARM und THROTTLE mit jedem Poll alle 50 ms;
ein bewusstes Schärfen schreibt zuerst CLEAR (0x5AFE), und ein NACK des
Koprozessors lässt das Panel entschärft.

## Vorausgesetzte externe Schaltung: das Monoflop

Die Sicherheitsleitung ist GPIO6 (General-Purpose Input/Output) des Panels
auf dem Header J8 (3V3, GND, GPIO6). Sie trägt Flanken, keinen Pegel. Das
Output Enable des Koprozessors und der Leistungspfad für Servos und ESC
(Electronic Speed Controller, Motorregler) müssen hinter einem retriggerbaren
Monoflop liegen, das nur angezogen bleibt, solange Flanken eintreffen.
Absturz, hängender Task, Reset, Brown-out und abgestecktes Kabel führen dann
zum selben Ergebnis: keine Flanken, kein Ausgang, unabhängig von der Firmware
an beiden Enden.

Fenster des Monoflops: etwa 150 ms. Der Heartbeat kommt aus dem Control-Task
des Panels, der alle 5 ms auf dem Kern läuft, der nicht zeichnet, und dessen
Periode damit nicht davon abhängt, was ein Frame kostet. Das Fenster bleibt
bei 150 ms und liegt innerhalb des 200-ms-Link-Failsafes des Koprozessors,
statt auf die neue Periode zu schrumpfen: die Reserve ist das, was einen
verspäteten Task überlebt.

Das Monoflop ist auf keiner Platine. Die Flanken erreichen J8 und sonst
nichts.

## Heartbeat-Überwachung

Der Koprozessor prüft den Heartbeat zusätzlich in der Firmware, weil ein
Monoflop Heartbeat und Rauschen nicht unterscheiden kann. Die Konstanten aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Wert | |
| --- | ---: | --- |
| Flankenabstand des Panels | 26–52 ms | eine Flanke je gerendertem Frame |
| Akzeptierter Abstand | 4–150 ms | kürzer ist Rauschen; länger heißt, das Panel rendert nicht mehr |
| Gute Abstände, bevor der Leitung vertraut wird | 4 | etwa 0,1 s bei der Rate des Panels |

Die Prüfung ist asymmetrisch: vier gute Abstände, bevor der Leitung vertraut
wird; ein schlechter Abstand oder ein stilles Fenster, und das Vertrauen ist
weg. Der Koprozessor verweigert das Schärfen, solange der Leitung nicht
vertraut wird, und entschärft seine Ausgänge, sobald sie ausbleibt.

Der Heartbeat wird in der Schleife erzeugt, die den Touch liest und STOP
besitzt, nicht von einem Timer oder einer Peripherie und nicht in der
Schleife, die zeichnet: ein Panel, das nicht mehr zeichnet, lässt sich noch
stoppen, eines ohne Touch nicht. Der Eingang des
Koprozessors hat einen Pull-down, sodass ein unversorgtes oder abgestecktes
Panel als Leitung ohne Flanken gelesen wird.

## Verhaltensweisen, die Absicht sind

- STOP rastet ein. Der Prüfstand bleibt entschärft, bis er erneut scharf
  geschaltet wird.
- Das Gas bewegt sich um die Strecke, die ein Finger zurücklegt, nicht auf die
  Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, sodass
  eine Berührung am Ende nicht mit einem Kontakt den vollen Weg anfordern kann.
- Das Verlassen eines Prüfstandsbildschirms entschärft.
- Antwortet der Touch-Controller 500 ms lang nicht, entschärft der Prüfstand
  und verweigert das Schärfen. Das Panel ist der einzige Ort mit einem
  STOP-Knopf.
- Nach einem Link-Failsafe schaltet der Koprozessor nicht wieder scharf, wenn
  Verkehr zurückkehrt. Das Failsafe wird durch das Schreiben eines definierten
  Werts (0x5AFE) auf die Control-Page verlassen.
- Bei Überstrom, Übertemperatur, Stall-Timeout und totem Link handelt der
  Koprozessor aus eigener Befugnis und meldet den Fehler beim nächsten Poll.

## Heartbeat statt Enable-Pegel

Ein statischer Enable-Pegel versagt, wenn die Firmware mit gesetztem Pin
hängen bleibt. Flanken laufen von selbst ab. Die Periodenprüfung in der
Firmware weist eine kurzgeschlossene oder klingelnde Leitung ab, die ein
Monoflop allein annehmen würde.

Das Panel hat keine Leitung zu irgendeinem Ausgang.
