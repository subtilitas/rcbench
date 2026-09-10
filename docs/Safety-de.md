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
ein bewusstes Schärfen schreibt zuerst CLEAR (0x5AFE) allein, dann ARM,
THROTTLE und MOTOR_POLES in einem Frame, und ein NACK des Koprozessors auf
eines von beiden lässt das Panel entschärft.

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

Das Monoflop wird von den Flanken des Panels nachgetriggert und nimmt die
Ausgänge weg, wenn diese ausbleiben. Was es gegenüber der direkten Leitung
hinzufügt: Es tut das, ohne dass die Firmware des Koprozessors etwas tun muss.
Drei Fälle, und der mittlere ist die Lücke:

| | Direkte Leitung und Firmware | Mit Monoflop |
|---|---|---|
| Das Panel hört auf zu schlagen, der Koprozessor ist gesund | entschärft nach HEARTBEAT_MAX_GAP_MS | dasselbe, nach seiner eigenen Zeit |
| Das Panel hört auf zu schlagen und der Koprozessor kann nicht handeln -- hängt, oder bedient seinen Monitor nicht | nichts entschärft, die Bank treibt weiter | die Ausgänge fallen trotzdem weg |
| Das Panel ist gesund und der Koprozessor verhält sich falsch | es bleiben STOP am Panel und der Link-Watchdog | **keine Hilfe**: das Panel schlägt weiter, das Monoflop bleibt bestromt |

Die dritte Zeile ist kein Fall, für den das Monoflop da ist, und keine
Hardware in diesem Entwurf deckt sie ab. Solange das Bauteil fehlt, ist auch
die zweite Zeile unabgedeckt, und die Firmware an beiden Enden ist die gesamte
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
  Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, eine
  Berührung am Ende fordert also nichts an; ein Drag über den ganzen Track
  fordert den ganzen Weg an, und einer, der innerhalb eines 50-ms-Polls
  abgeschlossen ist, ist ein Sprung von 0 auf 100 % am Pin. Slider, die
  nichts Gefährliches kommandieren, etwa die Sweep-Geschwindigkeit des
  Servobildschirms, behalten Tap-to-set.
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
- Die Einstellung `Ramp limit`, 5 bis 300 %/s, gilt für die Gas-Bank des
  Panels. Diese Bank ist der modellierte Prüfstand: ihr geslewter Wert wird
  vom Telemetrie-Simulator gelesen und sonst von nichts, und nur solange der
  Link unten ist. Einem antwortenden Koprozessor wird stattdessen das rohe
  Kommando geschickt, und `outbind_to_chan_cfg()` schreibt für keinen Kanal
  einen Slew, ein als Throttle gebundener Pin springt also im nächsten
  1-ms-Durchlauf darauf. Das physische Gas wird nicht gerampt, per
  Entscheidung: die Bank rampt ein Throttle nur aufwärts, ein Ramp auf dem
  Draht würde also nur den Anstieg verlangsamen und sonst nichts. [STATUS.md,
  Not planned](https://github.com/subtilitas/rcbench/blob/main/STATUS.md#not-planned).

## Heartbeat statt Enable-Pegel

Ein statischer Enable-Pegel versagt, wenn die Firmware mit gesetztem Pin
hängen bleibt. Flanken laufen von selbst ab. Die Periodenprüfung in der
Firmware weist eine kurzgeschlossene oder klingelnde Leitung ab, die ein
Monoflop allein annehmen würde.

Das Panel hat keine Leitung zu irgendeinem Ausgang.
