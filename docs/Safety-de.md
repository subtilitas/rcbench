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

Das Monoflop ist auf keiner Platine. Auf dem Aufbau-Prüfstand erreichen die
Flanken über eine direkte Leitung von J8 den GP3 des Koprozessors, weshalb
dieser Prüfstand scharfschalten kann.

Was die direkte Leitung nicht abdeckt, ist der Fall, für den das Monoflop da
ist: Die Firmware des Koprozessors selbst hängt. Dann fragt nichts mehr den
Monitor ab, also entschärft auch nichts, und die Bank treibt weiter, was immer
das Panel tut. Ein Panel, das für sich abstürzt, ist bereits abgedeckt -- der
Monitor unten sieht HEARTBEAT_MAX_GAP_MS lang keine Flanke und die Schleife
entschärft --, und das ist der Fall, den die Leitung erledigt. Das Monoflop
ist der Fall, der nicht davon abhängt, dass die Firmware des Koprozessors
läuft. Solange es fehlt, ist die Firmware an beiden Enden die gesamte
Verriegelung.

## Heartbeat-Überwachung

Der Koprozessor prüft den Heartbeat zusätzlich in der Firmware, weil ein
Monoflop Heartbeat und Rauschen nicht unterscheiden kann. Die Konstanten aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Wert | |
| --- | ---: | --- |
| Flankenabstand des Panels | 20 ms | `HEARTBEAT_PERIOD_MS`, eine Flanke je Periode des Control-Tasks, nicht je Frame |
| Akzeptierter Abstand | 4–150 ms | kürzer ist Rauschen; länger heisst, die Schleife, die STOP besitzt, steht |
| Gute Abstände, bevor der Leitung vertraut wird | 4 | 80 ms bei der Rate des Panels |

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
- Das Scharfschalten ist ein zwei Sekunden langes Halten auf ARM, und das
  Kommando geht ab, wenn das Halten durchgelaufen ist, nicht wenn der Finger
  abhebt. Das Entschärfen ist ein Druck.
- Das Gas bewegt sich um die Strecke, die ein Finger zurücklegt, nicht auf die
  Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, sodass
  eine Berührung am Ende nicht mit einem Kontakt den vollen Weg anfordern kann.
  Slider, die nichts Gefährliches kommandieren, etwa die Sweep-Geschwindigkeit
  des Servobildschirms, behalten Tap-to-set.
- Ein Entschärfen setzt das Gas auf null, damit ein Scharfschalten bei null
  beginnt und nicht dort, wo der letzte Lauf aufgehört hat.
- Das Verlassen eines Prüfstandsbildschirms entschärft.
- Antwortet der Touch-Controller 500 ms lang nicht, entschärft der Prüfstand
  und verweigert das Schärfen. Das Panel ist der einzige Ort mit einem
  STOP-Knopf.
- Nach einem Link-Failsafe schaltet der Koprozessor nicht wieder scharf, wenn
  Verkehr zurückkehrt. Das Failsafe wird durch das Schreiben eines definierten
  Werts (0x5AFE) auf die Control-Page verlassen.
- Bei Überstrom, Übertemperatur, Stall-Timeout und totem Link handelt der
  Koprozessor aus eigener Befugnis und meldet den Fehler beim nächsten Poll.
- Ein scharfer Prüfstand treibt jeden gebundenen Pin, ob ihn etwas
  kommandiert oder nicht. Ein Kanal, der 500 ms lang kein Kommando bekommt,
  wird auf der Ruhelage seiner Rolle ausgegeben: gestoppt bei throttle,
  zentriert bei surface. Zentriert ist die Mitte der Endpunkte dieses Kanals
  — 1500 us über den voreingestellten 1000 bis 2000 us, 760 us über den 660
  bis 860 us eines schmalen Servos. Das Timeout legt den Kanal auf diese
  Ruhelage und lässt den Pin weiter treiben; beendet werden die Flanken durch
  Entschärfen.
- 1500 us an einem Empfängerausgang sind etwa halbes Gas. Was ein ESC tut,
  der noch keinen Puls gesehen hat und dann 1500 us bekommt, ist an diesem
  Prüfstand nicht gemessen: er kann mit etwa halbem Gas laufen, und er kann
  das Scharfschalten verweigern, bis er einen Stopp gesehen hat. Ein Pin, der
  als Servoausgang gebunden ist, kann daher einen laufenden Motor führen.
- Das Gas erreicht einen Kanal mit der Rolle surface nicht, weil es die Pins
  kommandiert, die die Bindung als Motoren führt. Ein Schreiben auf die
  CHANNELS-Page erreicht ihn, weil diese Page Kanäle über den Index adressiert
  und nicht über die Rolle.
- In den ersten 500 ms nach dem Scharfschalten steht ein Kanal nicht auf
  seiner Ruhelage. Das Scharfschalten stempelt die Uhr jedes Kanals, sodass
  ein Kommando, das im entschärften Zustand gegeben wurde, nicht überfällig
  ist und ausgegeben wird, bis es überfällig wird. Was ausgegeben wird, hängt
  vom Slew des Kanals ab: ohne Slew ist der erste Schritt die ganze Strecke,
  der Kanal steht also auf diesem Kommando; mit Slew hat das Entschärfen ihn
  bereits auf die Ruhelage gestellt und er rampt von dort, höchstens
  `slew_per_s * 500 / 1000` weit, bevor der Timeout ihn zurückholt. In beiden
  Fällen treibt er, und beendet wird das durch ein Entschärfen.

## Heartbeat statt Enable-Pegel

Ein statischer Enable-Pegel versagt, wenn die Firmware mit gesetztem Pin
hängen bleibt. Flanken laufen von selbst ab. Die Periodenprüfung in der
Firmware weist eine kurzgeschlossene oder klingelnde Leitung ab, die ein
Monoflop allein annehmen würde.

Das Panel hat keine Leitung zu irgendeinem Ausgang.
