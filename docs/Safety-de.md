# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Wie der Prüfstand anhält, welche externe Schaltung der Entwurf voraussetzt,
und welche Verhaltensweisen Absicht sind.

Das Projekt ist in Entwicklung, und seine Funktionen sind nicht auf Hardware
verifiziert.

## Stoppmechanismen

| Mechanismus | Deckt ab | Stand |
| --- | --- | --- |
| Der Heartbeat bleibt aus | das Panel hängt, ist resettet, hat einen Brown-out oder ist abgesteckt; ein gedrücktes STOP | an beiden Enden erzeugt und überwacht; das Monoflop, das er steuert, ist nicht bestückt |
| Stille-Watchdog des IOmcu, 200 ms | der Link ist in einer der beiden Richtungen tot | gebaut und getestet |
| STOP-Kommando über den Link | ein bewusster Stopp, quittiert und gemeldet | geschrieben |

Ein gedrücktes STOP stoppt den Heartbeat, schaltet das eigene
Ausgangsmodell des Panels auf SAFE und schreibt ARM = 0 auf die Control-Page. Bei
stehendem Link schreibt das Panel ARM und THROTTLE mit jedem Poll alle 50 ms;
ein bewusstes Schalten auf ARMED wartet, bis der IOmcu meldet, dass er dem
Heartbeat vertraut, schreibt dann CLEAR (0x5AFE) allein, dann ARM, THROTTLE
und MOTOR_POLES in einem Frame, und ein NACK (negative Quittung) des
IOmcu auf eines von beiden lässt das Panel SAFE.

## Vorausgesetzte externe Schaltung: das Monoflop

Die Sicherheitsleitung ist GPIO6 (General-Purpose Input/Output) des Panels
auf dem Header J8 (3V3, GND, GPIO6). Sie trägt Flanken, keinen Pegel. Das
Output Enable des IOmcu und der Leistungspfad für Servos und ESC
(Electronic Speed Controller, Motorregler) müssen hinter einem retriggerbaren
Monoflop liegen, das nur angezogen bleibt, solange Flanken eintreffen.
Absturz, hängender Task, Reset, Brown-out und abgestecktes Kabel führen dann
zum selben Ergebnis: keine Flanken, kein Ausgang, unabhängig von der Firmware
an beiden Enden.

Fenster des Monoflops: etwa 150 ms. Der Heartbeat kommt aus dem Control-Task
des Panels, der alle 5 ms auf dem Kern läuft, der nicht zeichnet, und dessen
Periode damit nicht davon abhängt, was ein Frame kostet. Das Fenster bleibt
bei 150 ms und liegt innerhalb des 200-ms-Link-Failsafes des IOmcu,
statt auf die neue Periode zu schrumpfen: die Reserve ist das, was einen
verspäteten Task überlebt.

Das Monoflop ist auf keiner Platine. Auf dem Aufbau-Prüfstand erreichen die
Flanken über eine direkte Leitung von J8 den GP3 des IOmcu, weshalb
dieser Prüfstand auf ARMED schalten kann.

Das Monoflop wird von den Flanken des Panels nachgetriggert und nimmt die
Ausgänge weg, wenn diese ausbleiben. Was es gegenüber der direkten Leitung
hinzufügt: Es tut das, ohne dass die Firmware des IOmcu etwas tun muss.
Drei Fälle, und der mittlere ist die Lücke:

| | Direkte Leitung und Firmware | Mit Monoflop |
|---|---|---|
| Das Panel hört auf zu schlagen, der IOmcu ist gesund | schaltet auf SAFE nach HEARTBEAT_MAX_GAP_MS | dasselbe, nach seiner eigenen Zeit |
| Das Panel hört auf zu schlagen und der IOmcu kann nicht handeln -- hängt, oder bedient seinen Monitor nicht | nichts schaltet auf SAFE, die Bank treibt weiter | die Ausgänge fallen trotzdem weg |
| Das Panel ist gesund und der IOmcu verhält sich falsch | es bleiben STOP am Panel und der Link-Watchdog | **keine Hilfe**: das Panel schlägt weiter, das Monoflop bleibt bestromt |

Die dritte Zeile ist kein Fall, für den das Monoflop da ist, und keine
Hardware in diesem Entwurf deckt sie ab. Solange das Bauteil fehlt, ist auch
die zweite Zeile unabgedeckt, und die Firmware an beiden Enden ist die gesamte
Verriegelung.

## Heartbeat-Überwachung

Der IOmcu prüft den Heartbeat zusätzlich in der Firmware, weil ein
Monoflop Heartbeat und Rauschen nicht unterscheiden kann. Die Konstanten aus
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Wert | |
| --- | ---: | --- |
| Flankenabstand des Panels | 20 ms | `HEARTBEAT_PERIOD_MS`, eine Flanke alle 20 ms, also alle 4 Perioden des Control-Tasks zu 5 ms, nicht je Frame |
| Akzeptierter Abstand | 4–150 ms | kürzer ist Rauschen; länger heisst, die Schleife, die STOP besitzt, steht |
| Gute Abstände, bevor der Leitung vertraut wird | 4 | 80 ms bei der Rate des Panels |

Die Prüfung ist asymmetrisch: vier gute Abstände, bevor der Leitung vertraut
wird; ein schlechter Abstand oder ein stilles Fenster, und das Vertrauen ist
weg. Der IOmcu verweigert das Schalten auf ARMED, solange der Leitung nicht
vertraut wird, und schaltet seine Ausgänge auf SAFE und setzt seinen Arm-Latch,
sobald sie ausbleibt.

Der Heartbeat wird vom Control-Task erzeugt, der STOP besitzt und alle 5 ms
auf Kern 1 läuft, dem Kern, der nicht zeichnet. Er kommt nicht von einem
Timer oder einer Peripherie und nicht aus der Schleife, die zeichnet: ein
Panel, das nicht mehr zeichnet, lässt sich noch stoppen. Den Touch-Controller
liest eine eigene Task, Priorität 5 auf Kern 0, dem Kern, der zeichnet. Der
Control-Task nimmt deren Ereignisse aus einer Queue und beurteilt den
Controller nach der Zeit seit dessen letzter Antwort: 500 ms ohne Antwort
schalten den Prüfstand auf SAFE und stoppen den Heartbeat. Der Eingang des
IOmcu hat einen Pull-down, sodass ein unversorgtes oder abgestecktes
Panel als Leitung ohne Flanken gelesen wird.

## Arm-Latch

Der IOmcu hält einen Arm-Latch. Solange er gesetzt ist, wird ein
Schreiben von ARM mit NOT_ARMED abgewiesen, und die Bank bleibt SAFE,
was immer im Register ARM steht.

| Setzt den Latch | Bedingung |
| --- | --- |
| Ein Start des IOmcu | Einschalten, ein Reset, der RUN-Pin, ein Neuflashen |
| Stille auf dem Link | 200 ms ohne Anfrage, zusammen mit dem Link-Failsafe |
| Dem Heartbeat wird nicht mehr vertraut | ein Abstand unter 4 ms oder über 150 ms, oder 150 ms ohne Flanke |
| Ein abgewiesenes ARM | ein ARM, das mit NOT_ARMED beantwortet wurde; ein CLEAR mit anschließend abgewiesenem ARM lässt den Latch also gesetzt |

Eines löscht ihn: das Schreiben von 0x5AFE in CLEAR auf der Control-Page.
Das Panel schreibt CLEAR an einer Stelle, beim Schalten auf ARMED, das eine Person
durch 2 s Halten von ARM vollendet. Das ARM, das das Panel bei jedem
Poll alle 50 ms einer Bank in ARMED schreibt, trägt kein CLEAR und bringt
einen IOmcu mit gesetztem Latch daher nie zurück.

Der Latch ist vom Link-Failsafe getrennt. Das ON der Versorgung und die
STATUS-Page lesen das Failsafe und den Heartbeat und nicht den Latch: nach
einem Start meldet STATUS Idle ohne Fault-Bit, und ein ON der Versorgung wird
vor dem ersten Schalten auf ARMED angenommen. Der Latch steht nicht auf STATUS. Ein
IOmcu mit gesetztem Latch zeigt sich als abgewiesenes ARM, das das
Panel als `IOmcu hat auf SAFE geschaltet. ARM erneut halten.` meldet.

Das Panel hat für dieselben Ereignisse eine eigene Regel. Antwortet der
IOmcu nicht mehr, während der Prüfstand ARMED ist oder ein Schalten auf ARMED
auf die Leitung wartet, rastet am Panel bei diesem Poll ein Stopp ein: die
eigene Bank wird auf SAFE geschaltet, das Gas geht auf null, eine gehaltene
Servo-Position wird losgelassen, und der Heartbeat stoppt. Eine ausbleibende
Antwort wird nach dem Timeout von 1000 ms bemerkt, oder sofort, wenn der
CAN-Controller (Controller Area Network) des Panels bus-off ist. Jeder
Austausch zählt, nicht nur das Lesen der Bench-Page im Poll: das Schreiben
von ARM und THROTTLE, das darauf folgt, ein Page-Dienst, ein Servo-Refresh,
das Lesen von STATUS. Endet einer ohne Antwort, gilt der Link beim nächsten
Durchlauf des Control-Tasks als ausgefallen, und bis dahin sendet das Panel
nichts mehr darauf. Solange der Prüfstand ARMED ist oder ein Schalten auf ARMED
wartet, rastet der Stopp ein, sobald dieser Austausch endet, ohne auf den
Durchlauf zu warten. Eine Bank,
die ohne angeschlossenen IOmcu ARMED ist, der simulierte Prüfstand,
wird auf dieselbe Weise bei dem Poll gestoppt, bei dem ein IOmcu zum
ersten Mal antwortet, bevor ein ARM an ihn geschrieben wird.

### Neustart des IOmcu während eines Laufs

Der IOmcu stellt seine Ausgangsbindung aus dem Flash wieder her und
startet mit gesetztem Latch und ohne Kommando. Die Bank des Panels ist noch
bis zu 1000 ms ARMED. Zwei Fälle:

- Das Panel bemerkt den Ausfall. Bei ihm rastet ein Stopp ein, und es
  schreibt von da an ARM = 0.
- Das Panel bemerkt ihn nicht, weil die Anfrage wiederholt wurde, bis der neu
  gestartete IOmcu antwortete. Sein nächstes Schreiben von ARM wird
  mit NOT_ARMED abgewiesen, und bei dieser Antwort rastet am Panel ein Stopp
  ein.

In beiden bleiben die Ausgänge aus, bis eine Person ARM hält. Auf dem Host
getestet in `test_safety_gate` für Ausfälle von 5, 30, 50, 60, 300, 1000 und
5000 ms bei je 55 Reset-Phasen und drei Modellen der Leitung. Wie lange der IOmcu vom Reset bis zu seinem ersten
beantworteten Frame braucht, ist nicht gemessen.

### Das erste Schalten auf ARMED nach einem Stopp

Der Heartbeat wird zurückgehalten, solange ein Stopp eingerastet ist. Ein
Schalten auf ARMED löst den Stopp am Panel, der Heartbeat läuft wieder, und der Monitor
des IOmcu braucht fünf Flanken, bevor er der Leitung vertraut: 100 bis
125 ms bei den 5 ms des Control-Tasks. Das Panel wartet 100 ms, liest dann
einmal je Durchlauf das Fault-Register von STATUS, bis das Heartbeat-Bit
gelöscht ist, und schreibt erst dann CLEAR und den Frame, der auf ARMED schaltet. Ist
das Bit 300 ms nach dem vollendeten Halten nicht gelöscht, gibt das Panel
das Schalten auf ARMED mit `Der IOmcu hat den Wechsel zu ARMED abgelehnt.` auf und schreibt nichts, der
Latch des IOmcu bleibt also gesetzt. Eine Antwort, die später als
diese 300 ms zurückkommt, wird nicht verwendet, was immer sie sagt.

Dieselben 300 ms gelten durch die Schreibvorgänge, die folgen. Das Panel
fragt die Frist vor dem CLEAR und vor dem Frame, der auf ARMED schaltet, erneut ab,
weil das Freigeben des Servos davor und das CLEAR selbst spät beantwortet
werden können. Ist die Frist vor dem CLEAR abgelaufen, wird das Schalten auf ARMED wie
oben aufgegeben, und nichts wird geschrieben. Ist sie nach einem quittierten
CLEAR abgelaufen, wird kein Frame geschrieben, und am Panel rastet ein Stopp
ein: das CLEAR hat den Latch des IOmcu gelöst, und kein Register
setzt ihn wieder, also hält der Stopp den Heartbeat zurück, und der
IOmcu setzt seinen Latch, sobald er der Leitung nicht mehr vertraut,
innerhalb von 150 ms. Die Grenze der Frist ist der Frame, der auf ARMED schaltet,
selbst. Er wird zur Frist oder davor geschrieben, und seine Quittung kann
bis zum Timeout von 1000 ms (`LINK_HOST_TIMEOUT_MS`) später eintreffen; der
IOmcu hat dann die angefragte Rolle auf ARMED geschaltet, in Ruhe: Gas 0
für MOTOR & ESC und dessen Stick-Läufe, Surfaces zentriert für SERVO. Das Panel macht das nicht rückgängig. Bleibt ein Austausch des Schaltens auf ARMED
unbeantwortet -- das Lesen von STATUS, das Freigeben des Servos, CLEAR oder
der Frame, der auf ARMED schaltet --, hat er den Timeout von 1000 ms abgewartet
oder die Leitung nicht erreicht: das ist der Link, der unter einem Schalten auf ARMED
ausfällt, und am Panel rastet ein Stopp ein wie oben. Eine Ablehnung ist
eine Antwort und lässt das Panel SAFE und ohne Stopp. Ohne
angeschlossenen IOmcu schaltet das Panel seine eigene Bank nach den
100 ms auf ARMED.

### Ein Pulsbereich unterwegs

Der Frame, der auf ARMED schaltet, trägt ARM, THROTTLE und die Polzahl,
aber kein CHAN_CFG. Leerlaufpuls und Vollpuls des Gases erreichen den IOmcu
auf einem eigenen Weg: eine Änderung, eine Link-up-Flanke oder der Start des
Panels schuldet dem IOmcu das Paar. Das Panel begleicht die Schuld bei einem
Poll im 50-ms-Takt, dessen Schreiben von ARM = 0 quittiert wurde, sobald
300 ms (`ENDPOINTS_SETTLE_MS`) seit der letzten Änderung vergangen sind. Es
liest CHAN_CFG, setzt das Paar auf die Gaskanäle und schreibt die Seite,
wenn sie abweicht. Der IOmcu übernimmt ein Schreiben von CHAN_CFG in seine
Ausgänge, bevor er es quittiert.

| Begleichen | Die Schuld |
| --- | --- |
| die Seite hält das Paar schon | beglichen, nichts geschrieben |
| das Schreiben wird quittiert | beglichen |
| das Lesen oder das Schreiben bleibt unbeantwortet | offen, beim nächsten Poll erneut gesendet |
| das Lesen wird abgelehnt | beendet, `Der IOmcu hat seinen Pulsbereich nicht gemeldet.` |
| der Leerlaufpuls liegt nicht unter dem Vollpuls | beendet, nichts gesendet, `Leerlaufpuls muss kürzer als Vollgaspuls sein. Nicht gesendet.` |
| das Schreiben wird abgelehnt | beendet, `Der IOmcu hat den Pulsbereich abgelehnt.` |

Solange die Schuld bei bestehendem Link offen ist, lehnt das Panel das
Schalten auf ARMED mit `Pulsbereich noch nicht an den IOmcu gesendet. ARM
erneut halten.` ab. Es fragt vor dem CLEAR, nach dem Freigeben des Servos
und der Pulsrate, und schreibt nichts, auch kein CLEAR; der Prüfstand bleibt
SAFE mit den Ausgängen in Ruhe. Es fragt nach einem
quittierten CLEAR erneut, für eine Änderung, die eintraf, während das CLEAR
unterwegs war, und schreibt dann keinen Frame, der auf ARMED schaltet. Die
Schuld wird auf dem Control-Task beglichen, dem Task, der das Schalten
schreibt, also läuft das Schalten nie, während ein Begleichen unterwegs ist.
Ein abgelehntes Schalten wird nicht von selbst wiederholt: ARM wird erneut
gehalten. STOP ist davon nicht berührt.

Eine Änderung zur Zeit 0 wird beim ersten Poll ab 300 ms beglichen: ein Poll
bei 299 ms lässt sie offen, ein Poll bei 300 ms begleicht sie. Die Zeit wird
modulo 2^32 ms genommen, also läuft die Ruhezeit über den Überlauf der
Millisekundenuhr hinweg. Die Zeilen der beiden Werte auf OPTIONEN sind
gesperrt, solange ARM gehalten wird, und das Halten dauert 2 s, also ist eine
Änderung vor dem Druck in der Regel innerhalb des Haltens beglichen.

Eine Ablehnung beendet die Schuld, und ein Schalten auf ARMED läuft dann auf
dem Paar, das der IOmcu hält; der Hinweis sagt das. Ohne Link schaltet die
eigene Bank des Panels auf ARMED, und nichts wird geschrieben. Ein Link, der
zurückkommt, schuldet das Paar erneut und hält den IOmcu SAFE, bis das Paar
beglichen ist; die Ablehnung dauert also bis zum ersten Poll nach dem
Link-up. Auf dem Host getestet in `test_endpoints_link` und `test_arming`.
Der Control-Task des Panels, der das Flag liest und den Hinweis zeigt, ist
nicht in der Coverage-Zahl.

## Verhaltensweisen, die Absicht sind

- STOP rastet ein. Der Prüfstand bleibt SAFE, bis er erneut auf ARMED
  geschaltet wird.
- STOP schließt die Rückfrage AUSGANG IST EINGESCHALTET auf SUPPLY und SERVO. Jeder
  Bildschirm zählt die Stopps, die ihm gemeldet werden, und eine Rückfrage
  gilt nur unter dem Zählerstand, mit dem sie gestellt wurde. Ein ÜBERNEHMEN
  im selben Frame wie STOP ändert also keinen Sollwert, auch wenn das
  Netzteil den Ausgang erst eine Messung später als aus meldet.
- Die Verdrahtung des PD mini (SETUP ANSCHLÜSSE) ändert sich, sobald das
  Modul einmal geantwortet hat, nur auf eine nach der Änderung gesendete
  Zustandslesung, die den Ausgang aus zeigt. Ein Modul, das sich in den
  500 ms zwischen zwei Lesungen selbst eingeschaltet hat, behält seine Pins
  und damit seinen Weg zum OFF; der IOmcu schaltet es ab.
- Einen eingeschalteten Ausgang des PD mini, dessen Eingangsspannung bei 2 Lesungen
  hintereinander unter dem Sollwert plus 0,5 V liegt, schaltet der
  IOmcu ab und meldet es dem Panel danach. Eine vorsichtige Regel,
  ohne Messung gewählt: sie kann einen Lauf abbrechen, der durchgehalten
  hätte. Sollwerte sind auf 0,7 V unter der Eingangsspannung begrenzt, 0,2 V
  über dieser Linie: sinkt die Eingangsspannung unter Last um weniger als
  0,2 V, schaltet nichts ab. Ein ON, verlangt während die Eingangsspannung
  unter 1,5 V liegt, wird nicht geschrieben.
- Das Schalten auf ARMED ist ein zwei Sekunden langes Halten auf ARM, und das
  Kommando geht ab, wenn das Halten durchgelaufen ist, nicht wenn der Finger
  abhebt. Das Schalten auf SAFE ist ein Druck.
- Das Panel schreibt seinen eigenen Flash nur bei einem Prüfstand in SAFE,
  ohne wartendes Schalten auf ARMED, mit ausgeschaltetem
  Netzteil-Ausgang und ohne eingereihtes ON. Flash-Auto-Suspend ist aus,
  also hält jedes Löschen und Schreiben beide Kerne des Panels an, den
  Control-Task mit Heartbeat, STOP, OUTPUT OFF und den Abschaltungen des
  Netzteils eingeschlossen. Die Regel ist `flash_gate_open()` in
  `shared/safety/flash_gate.c`, unter `test_flash_gate`. Sie hält das
  Speichern der Einstellungen und das Ablegen der Zeichnung der Platine zurück.
  Das Schalten auf ARMED und das Netzteil werden für einen Schreibvorgang nie
  verweigert: der Schreibvorgang wartet. Das Ablegen (2 Löschvorgänge zu 4 kB
  und 2 Schreibvorgänge für die 1 560 Bytes große Zeichnung des RP2350-CAN)
  läuft einen Sektor pro 5-ms-Durchlauf des Control-Tasks und hält zwischen
  zwei Sektoren an, wenn der Prüfstand ARMED ist oder das Netzteil
  einschaltet. Es macht beim ersten sicheren Durchlauf mit demselben Sektor
  weiter. Sein Header wird zuerst gelöscht und zuletzt geschrieben; ein
  angehaltenes oder durch Stromausfall abgebrochenes Ablegen hinterlässt also
  keine auffindbare Zeichnung für die Platine, und der nächste Link-Aufbau
  holt sie neu. Die Dauer eines Löschvorgangs auf dem Flash des Panels
  ist nicht gemessen.
- Ein Stick-Lauf auf MOTOR & ESC > PROGRAMMIEREN schaltet über dieselbe Policy auf ARMED, nachdem
  seine Warnung (PROPELLER AB? MOTOR GESICHERT?) 2 s gehalten ist. Danach
  setzt er das Gas auf die Einstiegsstellung des Profils, bevor er das
  Netzteil einschaltet; bei den meisten ESCs ist das der Knüppel auf MAX: so
  wird ihr Menü betreten. Der ESC gibt dann sein Programmiermenü aus und
  treibt nicht, aber in seltenen Fällen kann ein Motor anlaufen und drehen;
  deshalb verlangt die Warnung einen Lastwiderstand anstelle des Motors oder
  einen fest montierten Motor ohne Propeller. Das ARM
  des Haltens wartet einen Frame im Bildschirm; ein STOP oder ein verlorenes
  Touch-Ereignis in diesem Frame nimmt es zurück und beendet den Lauf, damit
  es den Stopp nicht aufheben kann. STOP, ein Schalten auf SAFE, ein verlorener
  Link, ABBRECHEN und das Verlassen des Bildschirms beenden den Lauf mit dem Gas
  auf Minimum, dem Netzteil aus und dem Prüfstand auf SAFE, in einem
  Schritt. Ein Lauf, der wie geplant endet, schaltet zuerst das Netzteil aus
  und bewegt den Knüppel, wenn das Netzteil selbst den Ausgang aus meldet.
  [Stick-Programmierung](StickProgramming-de.md).
- Einem Halten werden höchstens 250 ms pro Frame gutgeschrieben, es erstreckt
  sich also über mindestens acht Frames mit stehendem Druck. Die Dauer eines
  Frames wird an seinem Anfang gemessen und an seinem Ende angewendet; ohne
  die Obergrenze schreibt ein einzelner verspäteter Frame einem Halten Zeit
  gut, das entstand, während derselbe Frame seine Touch-Events zustellte. Das
  Halten zum Schalten auf ARMED und die Quittierung des Bus-Fault nehmen beide
  diese Obergrenze.
- Ein Frame, der Touch-Events verloren hat, bricht die laufende Geste ab.
  Eine volle Touch-Queue verwirft ihren ältesten Eintrag, um den neuesten
  aufzunehmen, und keine Wahl dort ist für sich sicher: Ein Release, das nie
  ankommt, lässt eine Seite einen Druck halten, ein Druck, der nie ankommt,
  macht das Release danach elternlos, und die Bewegung, mit der ein Finger
  eine Taste verlässt, ist das, was das Halten aufgibt. Der Render-Task
  leert die Queue vom anderen Kern, das Prüfen eines Eintrags entscheidet
  also nicht, welcher entfernt wird. Stattdessen nummeriert der eine
  Erzeuger jeder Queue jedes Event, das er anbietet, ob die Queue es behält
  oder nicht, und der Verbraucher findet einen Verlust als Lücke in den
  Nummern: am ersten Event danach, bevor es behandelt wird, oder am Ende
  eines Leerens gegen die Nummer, die der Erzeuger vor dem Leeren
  veröffentlicht hat. Kein Zähler, der auf einem Kern steigt, muss rechtzeitig
  auf dem anderen gelesen werden. Der Render-Task sagt dann jeder Seite, dass
  ihr Bild vom Glas veraltet ist, nicht nur der obersten, weil ein Event,
  das den Verlust überlebt hat, von der Seite mit dem Druck weg navigiert haben
  kann; jede Seite verwirft jede laufende Geste. Das kommandiert nichts,
  genau wie ein frühes Loslassen, mit der einen Ausnahme weiter unten. Beide
  Queues sind nummeriert -- die Event-Queue des Treibers verwirft ihren
  ältesten Eintrag aus demselben Grund, und eine Lücke, die der Control-Task
  dort findet, reist als eigener Eintrag zum Render-Task, vor den Events, die
  ihr folgten. Der Render-Task leert die Queue unmittelbar vor dem Tick des
  Frames noch einmal, damit ein Event, das während des Durchlaufs verloren
  geht, in diesem Frame beantwortet wird und nicht erst im nächsten. Das
  Frame-Log führt die in jeder Queue fehlenden Events als
  `TOUCHLOST <Panel>/<Treiber>`. Der Control-Task verwirft aus demselben
  Grund seinen eigenen Vermerk eines STOP-Drucks bei einem Verlust im
  Treiber: Er besitzt diesen Druck unabhängig von den Seiten.
- Jedes Bedienelement, das zwischen Druck und Release Zustand hält, bricht
  ab. MOTOR & ESC, SERVO und CAN BUS FAULT haben eine Geste, die auf einem
  Timer fertig wird, ein verlorenes Release schaltet dort also von selbst
  auf ARMED oder quittiert; die Kacheln der Übersicht, die Zellen des Outputs-
  und des Picker-Bildschirms, die Tasten des Einstellungs-Bildschirms, die
  Tasten, Zeilen und die LÖSCHEN-Rückfrage des Log-Viewers und die Tab-Zeilen
  von MOTOR & ESC, ANALYSER und BALANCE wirken stattdessen auf das Release,
  und ein gehaltener Druck besitzt eine Track-ID, die der Controller
  wiederverwendet -- ein späterer Kontakt, der woanders begann, wird dann für
  das fehlende Release gehalten. Die LÖSCHEN-Rückfrage bleibt bei einem Abbruch
  offen und nimmt den nächsten frischen Druck an. HOME und STOP sind die eigene
  Geste des Routers, und er bricht sie selbst ab.
- Ein Kontakt hat vom Druck bis zum Release einen Besitzer, und der Router
  entscheidet ihn. Eine Seite bekommt ein Move oder ein Release nur für einen
  Kontakt, dessen Druck sie bekommen hat. Ein Kontakt, der auf dem Statusband
  (den oberen 48 px) oder auf dem Alert-Band aufsetzt, gehört dem Router, bis
  er abhebt, wohin er auch wandert. Ein Kontakt, der beim Bildschirmwechsel
  oder beim Verlust von Touch-Events auf dem Glas lag, gehört niemandem, bis
  er abhebt und neu aufsetzt. Ein Kontakt der Seite, der das Statusband
  erreicht, wird für die Seite an der letzten Stelle losgelassen, die sie
  bekommen hat, und gehört von da an dem Router: Ein Drag auf einem
  Schieberegler oder dem Zifferblatt endet mit dem Wert von dieser Stelle,
  ein Halten ist abgebrochen, und der Weg auf dem Band wird nicht angewendet.
  Ohne dieses Release bleibt der Drag an seiner Track-ID hängen, und ein
  späterer Kontakt mit derselben ID, der vom Band in den Bildschirm wandert,
  bewegt den Wert um seinen eigenen Weg vom Ursprung des ersten Drags aus.
  Das Release trägt eine Marke (`TOUCH_FLAG_NO_TAP`), ebenso das Release, das
  der Tracker für einen Kontakt erzeugt, der mehr als 120 px von der Stelle
  des vorigen Reports entfernt liegt. Ein markiertes Release beendet einen
  Druck oder einen Drag und löst kein Bedienelement aus: Der Finger hat dort
  nicht abgehoben. Vier Bedienelemente nehmen es trotzdem an, weil sie etwas
  anhalten: STOP, DISARM auf MOTOR & ESC und auf SERVO, AUSGANG AUS und
  START TEST, solange ein Servotest läuft.
  STOP und das Home-Tag reagieren auf einen Druck, der auf ihnen aufsetzt:
  Ein Kontakt, der vom Bildschirm auf STOP rutscht, drückt nichts. Die
  Tabelle fasst 5 Kontakte, so viele meldet der GT911; ein sechster erreicht
  keine Seite.
- Ein Bedienelement, das eine Einstellung ändert, wirkt auf ein Tippen und
  nicht auf das Aufsetzen. Auf SETUP stellt eine Taste `-` oder `+`, wenn der
  Finger, der sie gedrückt hat, innerhalb von ihr abhebt, in x und in y
  höchstens 8 px vom Aufsetzpunkt entfernt; die Liste scrollt von überall,
  die Tasten eingeschlossen, und ein Scrollen ändert keinen Wert. Eine
  gehaltene Taste stellt ab 0,45 s nach dem Aufsetzen. Die Bedienelemente von
  ESC STICK folgen derselben Regel, VORGABEN und die Tasten des Stick-Timings
  eingeschlossen. ABBRECHEN eines Stick-Laufs wirkt beim Aufsetzen, ebenso
  jedes Anhalten auf den Prüfstandsbildschirmen: Anhalten wartet nie auf ein
  Abheben, das ausbleiben kann.
- Die Seiten verlassen sich dafür nicht auf den Router. Jeder Druck beendet
  einen Drag auf einem Schieberegler oder dem SERVO-Zifferblatt, der noch
  hängt, wo auch immer der Druck landet, und das Verlassen von SERVO beendet
  einen Drag auf dem Zifferblatt. SUPPLY hält einen Druck zur Zeit; ein Druck
  mit der Track-ID des gehaltenen sagt, dass dieser Kontakt weg ist, und der
  gehaltene Druck wird verworfen, wie ein Touch-Verlust ihn verwirft. Nicht
  am Panel gemessen: ob der GT911 einem einzelnen Finger bei
  aufeinanderfolgenden Berührungen dieselbe Track-ID gibt.
- Bricht der Touch-Strom ab, während STOP gehalten wird, wird gestoppt. Der
  Control-Task besitzt diesen Druck unabhängig von den Seiten, und das
  Release, das den Prüfstand gestoppt hätte, kann genau das verlorene Event
  sein -- oder es kommt an und passt zu keinem der beiden Besitzer, weil die
  Render-Seite den Druck des Bands beim selben Verlust abbricht. Sonst würde
  nichts stoppen, und der Bediener hat STOP bereits gedrückt. Was das aufgibt:
  Ein Druck, der auf STOP begann und vor dem Abheben davon weggeführt worden
  wäre, kommandiert heute nichts und stoppt stattdessen.
- Wird bei einem Prüfstand in ARMED ein DISARM abgebrochen, wird trotzdem
  auf SAFE geschaltet. Eine Geste abzubrechen kommandiert nichts, und bei einem
  Prüfstand in ARMED ist das für eine von ihnen die falsche Richtung: Das
  Schalten auf SAFE ist ein Druck, sein Release ist also das ganze Kommando, und
  ein Release, das eine volle Queue verschluckt hat, ist ein Schalten auf SAFE,
  das der Bediener gemacht und der Prüfstand nie gesehen hat. Das
  Schalten auf ARMED hat sein Kommando schon abgesetzt, wenn der Finger abhebt,
  ein halb abgebrochenes Schalten auf ARMED kommandiert also nichts, und das ist
  richtig so. Ein Abbruch verwirft außerdem ein Schalten auf ARMED, das die
  Seite bereits abgesetzt hat und die Anwendung noch nicht abgeholt hat --
  ein Kommando wird im Frame nach dem absetzenden weitergereicht, und der
  Frame, der einen Verlust bemerkt, bricht vor diesem Weiterreichen ab. Ein
  Schalten auf ARMED, das schon beim Control-Task liegt, trägt, wie oft der
  Render-Task Gesten verworfen hatte, und die Nummer des letzten Events, das
  er genommen hatte. Der Control-Task leert zuerst die Queue des Treibers und
  verwirft dann ein Schalten auf ARMED, wenn der Render-Task seither Gesten
  verworfen hat oder einen Verlust-Eintrag noch nicht genommen hatte. Ist
  das Schalten auf ARMED ausgeführt, macht jeder Verlust -- der Render-Task
  verwirft Gesten, oder im Strom des Treibers fehlt eine Nummer -- es
  rückgängig, bis der Render-Task ARMED bestätigt; das tut
  ein Frame nur, wenn er in ARMED begann und den Strom vollständig fand. Der
  Control-Task sieht in jeder Wartezeit einer Link-Transaktion nach, nicht
  nur einmal pro Durchlauf, und stoppt von dort wie bei STOP: der Heartbeat
  fällt sofort ab, und der IOmcu geht innerhalb von 150 ms auf
  SAFE, wie lang der Rückstau hinter dem Schalten auf ARMED auch
  ist. Ein Frame, der
  vor dem Veröffentlichen begann, bricht Seiten ab, die den Prüfstand noch
  für SAFE halten, und die setzen kein DISARM ab; also muss der
  Control-Task es tun. Nach der Bestätigung sehen die Seiten einen Verlust
  gegen einen Prüfstand in ARMED. Über einen Verlust hinweg schaltet also
  nichts auf ARMED. Ein abgesetztes DISARM bleibt.
- Das Gas bewegt sich um die Strecke, die ein Finger zurücklegt, nicht auf die
  Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, eine
  Berührung am Ende fordert also nichts an; ein Drag über den ganzen Track
  fordert den ganzen Weg an, und einer, der innerhalb eines 50-ms-Polls
  abgeschlossen ist, ist ein Sprung von 0 auf 100 % am Pin. Slider, die
  nichts Gefährliches kommandieren, etwa die Sweep-Geschwindigkeit des
  Servobildschirms, behalten Tap-to-set.
- Ein Ausgangswert ändert sich nur auf einem Prüfstand in ARMED, auf
  MOTOR & ESC wie auf SERVO. Solange der Prüfstand nicht ARMED ist, ändert
  kein Bedienelement der beiden Bildschirme den Wert, den es kommandiert:
  Nichts wird gepostet, der Wert und seine Anzeige bleiben, das
  Bedienelement wird gedimmt gezeichnet, und der Bildschirm zeigt
  `ZUERST ARM`. Eine Drehung des Drehknopfs in dieser Zeit wird nicht
  aufgehoben. Die Ablehnung reicht vom DISARM, bis der Prüfstand ARMED
  meldet, umfasst also die Zeit zwischen einem durchgelaufenen Halten auf
  ARM und der Antwort des Prüfstands. Von einem DISARM an, das ein Bildschirm
  anfordert, oder einem STOP auf einem Prüfstand in ARMED, bis der Prüfstand
  SAFE meldet, werden die Bedienelemente
  ebenfalls abgelehnt und gedimmt gezeichnet, solange das DISARM auf das
  Abholen wartet und danach. Auf SERVO enden ein Sweep, eine Pause, eine
  gehaltene Stellung und ein Lauf des automatischen Tests dort, wo das DISARM
  oder das STOP angefordert wird, und ein
  FREIGEBEN in dieser Zeit wird gesendet und bewegt keinen Wert. Ein
  laufender Drag endet bei einem ARM, einem DISARM und einem STOP, und ein
  Finger, der auf dem Bedienelement liegt, bewegt nichts, bis er abhebt und
  neu drückt. Ein Wert, den der Bildschirm
  gepostet und die Anwendung bei einem DISARM oder STOP noch nicht abgeholt
  hat, wird verworfen.
- Jedes ARM beginnt bei der Ruhelage des Kanals, nicht dort, wo der letzte
  Lauf das Bedienelement gelassen hat.
  - MOTOR & ESC: Der Gas-Schieberegler, seine Schritte `-1` und `+1` und der
    Drehknopf sind die Bedienelemente. Ein DISARM setzt das Gas auf 0 %, und
    es bleibt dort bis zum nächsten ARM; der erste Schritt nach einem ARM
    postet also 1,0 %. Das Halten auf ARM setzt den Schieberegler auf 0 %,
    wenn es anfordert, und ebenso der Wechsel auf ARMED bei einem ARM, das
    dieses Halten nicht angefordert hat, wie bei einem Knüppel-Lauf auf
    PROGRAMMIEREN.
  - SERVO: Das Zifferblatt, der Drehknopf, ZENTRIEREN und SWEEP sind die
    Bedienelemente. In SAFE bleiben das Horn und die gezeigte Pulsbreite
    auf der zuletzt gefahrenen Stellung. Bei einem ARM wird der Wert zur
    Ruhelage der Ruderfläche, die die Pins treiben, sobald das ARM seine
    Freigabe bezahlt hat: die Mitte zwischen den Endpunkten des Kanals,
    1500 us über 1000 bis 2000 us und 760 us über 660 bis 860 us. Die erste
    Eingabe nach einem ARM beginnt dort.
- FREIGEBEN auf SERVO ist keine Stellung und wird auf einem Prüfstand
  in SAFE angenommen, wo es keinen Wert bewegt. Auf einem Prüfstand
  in ARMED stellt es die Pins in die Ruhelage, und der gezeigte Wert wird
  zu dieser Ruhelage, wie bei einem ARM: Ein laufender Drag endet, und die
  erste Eingabe danach beginnt bei der Ruhelage. Die Freigabe, mit der ein
  Lauf des automatischen Tests endet, tut dasselbe.
- Das Verlassen eines Prüfstandsbildschirms schaltet auf SAFE.
- Der Drehknopf (ein Winkelsensor MT6701 unter 0x06; SETUP, ANWENDUNG, `Drehknopf`,
  standardmäßig AUS) bewegt das Gas auf MOTOR & ESC und das Horn auf SERVO um
  die Strecke, die er gedreht wird, wie der Slider, und es gelten dieselben
  Regeln. Er schaltet nie auf ARMED: Das einzige Schalten auf ARMED ist der ARM-Hold. Auf beiden
  Bildschirmen bewegt eine Drehung auf einem Prüfstand in SAFE nichts
  und wird nicht aufgehoben; nach einem ARM bewegt der Knopf auf MOTOR & ESC
  von 0 % aus weiter und auf SERVO von der Ruhelage aus. Das Verlassen des
  Bildschirms schaltet auf SAFE. Die
  erste Messung nach dem
  Einschalten, eine Messung, die nicht eintrifft (ein NACK oder ein
  Timeout von 5 ms), und ein
  Sprung über 90 deg zwischen zwei Messungen setzen jeweils die Referenz neu
  und bewegen nichts; ein Knopf, der nicht mehr antwortet, trägt also nichts
  mehr bei, und kein letzter Wert bleibt als Kommando stehen. Ein Finger auf
  Track oder Zifferblatt besitzt den Wert gegenüber dem Knopf für den ganzen
  Frame, in dem er das Bedienelement berührt hat, und auf SERVO besitzen ein
  Sweep, ein Testlauf und das Einstellungsfeld das Horn. Ein Frame, der
  Touch-Ereignisse verloren hat, oder in dem der Router überhaupt navigiert
  hat, verwirft die Bewegung des Knopfs. Das Kommando des Knopfs geht mit dem
  nächsten Frame hinaus; findet dieser Frame zuerst verlorene
  Touch-Ereignisse, wird das Kommando zurückgenommen, und der Wert kehrt auf
  den Stand vor dem Knopf zurück. Der Knopf ersetzt nie ein
  anderes wartendes Kommando als ein Gas oder eine Horn-Position: Ein
  vollendetes Schalten auf ARMED, ein Schalten auf SAFE, ein Freigeben und ein Zurücksetzen
  der Spitzen bleiben stehen, und seine Bewegung wird verworfen.
  Der Knopf wird von einer eigenen Task auf dem Kern gelesen, den der Renderer
  benutzt, nie von der Control-Task, mit 5 ms Bus-Timeout für jede
  Transaktion eines Polls: eine Adressprobe, solange er keine Referenz hat, dann drei
  Lesezugriffe. Eine fehlgeschlagene Transaktion kostet dazu die Erholung des
  Treibers, einen Reset des Controllers und ein Freiräumen des Busses, das bis
  zu 50 ms länger wartet, solange SDA low gehalten wird. Ein Sensor, der SDA
  low hält, verzögert das Lesen des Touch also um etwa 55 ms je Transaktion.
  Das liegt unter dem eigenen Bus-Timeout des Touch-Lesens von 100 ms
  (`GT911_I2C_TIMEOUT_MS`), und ein Touch-Controller, der 500 ms schweigt,
  schaltet den Prüfstand auf SAFE (`ARMING_TOUCH_DEAD_MS`).
  Das Lesen des Touch liegt nicht auf dem Weg des Heartbeats: die
  Control-Task erzeugt den Heartbeat und benutzt den I2C-Bus nicht. Die
  Platine antwortet an diesem Bus auf 0x14, 0x20 bis 0x27, 0x30 bis 0x3F und
  0x5D, und der I/O-Expander CH422G nimmt einen Zugriff auf seine Adressen als
  Befehl: Bei 0x30 bis 0x3F wird das geschriebene Byte sein Ausgangsbyte, das
  die Hintergrundbeleuchtung, den Touch-Reset und die USB/CAN-Auswahl (USB:
  Universal Serial Bus, CAN: Controller Area Network) hält. Das Panel legt
  weder den Knopf noch ein anderes selbst angeschlossenes Gerät auf diese
  Adressen oder auf die, die die I2C-Spezifikation reserviert, 0x00 bis 0x03
  und 0x78 bis 0x7F (`shared/bench/include/panel_i2c.h`). Dass auf der
  Platine nichts auf der Adresse 0x06 des Knopfs antwortet, beruht auf der
  dokumentierten Adressierung von CH422G und GT911: Der eine Busscan der
  Platine umfasste 0x08 bis 0x77.
- Antwortet der Touch-Controller 500 ms lang nicht, schaltet der Prüfstand auf SAFE
  und verweigert das Schalten auf ARMED. Das Panel ist der einzige Ort mit einem
  STOP-Knopf.
- Nach einem Link-Failsafe schaltet der IOmcu nicht wieder auf ARMED, wenn
  Verkehr zurückkehrt. Das Failsafe wird durch das Schreiben eines definierten
  Werts (0x5AFE) auf die Control-Page verlassen. Dasselbe gilt nach einem
  Start des IOmcu und nach einem Heartbeat, dem nicht mehr vertraut
  wurde: siehe [Arm-Latch](#arm-latch).
- Ein Link, der bei einem Prüfstand in ARMED ausfällt, ist am Panel ein Stopp. Er
  rastet ein wie STOP und wird vom nächsten Schalten auf ARMED gelöst.
- Solange der Prüfstand ARMED ist, weist der IOmcu mit BAD_VALUE ein
  Schreiben auf OUTPUTS ab, das einen Slot ändert, und ein Schreiben auf
  CHAN_CFG, das die Rolle eines Kanals oder irgendetwas an einem
  Throttle-Kanal ändert. Slew und Pulsbereich einer Surface werden in ARMED
  angenommen: der SERVO-Bildschirm schreibt sie mit jeder Position. Das Panel
  schreibt eine Bindung und den Pulsbereich des Throttle nur in SAFE.
- Bei Überstrom, Übertemperatur, Stall-Timeout und totem Link handelt der
  IOmcu aus eigener Befugnis und meldet den Fehler beim nächsten Poll.
- Ein ARM nennt die Rollen, die es treibt ([Link, Ein Arm für eine
  Rolle](Link-de.md#ein-arm-für-eine-rolle)). ARM auf MOTOR & ESC und ein
  Stick-Lauf auf dessen PROGRAMMIEREN schalten die Throttle-Ausgänge auf ARMED, ARM
  0x0002. SERVO schaltet die Surfaces und den Pin des PROG PORT auf ARMED, ARM
  0x0004. Ein Pin einer Rolle, die das ARM nicht nennt, sendet keinen Puls,
  wie bei einem Prüfstand in SAFE. Ein IOmcu älter als Protokoll
  4.18 nimmt jedes ARM ungleich 0 als ganze Bank; an ihm treibt also jeder
  gebundene Pin.
- Ein ARM für die andere Rolle geht über ein Schalten auf SAFE. Das Panel schreibt
  ARM = 0, wartet mindestens 100 ms und schreibt dann die neuen Rollen; den
  Wechsel von einem ARM ungleich 0 zu einem anderen weist der IOmcu
  mit BAD_VALUE ab.
- STOP, das Ausbleiben des Heartbeat, Stille auf dem Link und ein
  Schalten auf SAFE setzen jeweils das ganze ARM auf 0, gleich welche Rollen es
  nannte, und jeder Ausgang hört auf.
- Ein Prüfstand in ARMED treibt jeden gebundenen Pin der Rollen in ARMED, ob
  ihn etwas kommandiert oder nicht. Ein Kanal, der 500 ms lang kein Kommando bekommt,
  wird auf der Ruhelage seiner Rolle ausgegeben: gestoppt bei throttle,
  zentriert bei surface. Zentriert ist die Mitte der Endpunkte dieses Kanals
  — 1500 us über den voreingestellten 1000 bis 2000 us, 760 us über den 660
  bis 860 us eines schmalen Servos. Das Timeout legt den Kanal auf diese
  Ruhelage und lässt den Pin weiter treiben; beendet werden die Flanken durch
  das Schalten auf SAFE.
- 1500 us an einem Empfängerausgang sind etwa halbes Gas. Was ein ESC tut,
  der noch keinen Puls gesehen hat und dann 1500 us bekommt, ist an diesem
  Prüfstand nicht gemessen: er kann mit etwa halbem Gas laufen, und er kann
  das Schalten auf ARMED verweigern, bis er einen Stopp gesehen hat. Ein Pin, der
  als Servoausgang gebunden ist, kann daher einen laufenden Motor führen.
- Das Gas erreicht einen Kanal mit der Rolle surface nicht, weil es die Pins
  kommandiert, die die Bindung als Motoren führt. Ein Schreiben auf die
  CHANNELS-Page erreicht ihn, weil diese Page Kanäle über den Index adressiert
  und nicht über die Rolle.
- In den ersten 500 ms nach dem Schalten auf ARMED steht ein Kanal nicht auf
  seiner Ruhelage. Das Schalten auf ARMED stempelt die Uhr jedes Kanals, sodass
  ein Kommando, das in SAFE gegeben wurde, nicht überfällig
  ist und ausgegeben wird, bis es überfällig wird. Was ausgegeben wird, hängt
  vom Slew des Kanals ab: ohne Slew ist der erste Schritt die ganze Strecke,
  der Kanal steht also auf diesem Kommando; mit Slew hat das Schalten auf SAFE ihn
  bereits auf die Ruhelage gestellt und er rampt von dort, höchstens
  `slew_per_s * 500 / 1000` weit, bevor der Timeout ihn zurückholt. In beiden
  Fällen treibt er, und beendet wird das durch ein Schalten auf SAFE.
- Die Einstellung `Rampengrenze`, 5 bis 300 %/s, gilt für die Gas-Bank des
  Panels. Diese Bank ist der modellierte Prüfstand: ihr geslewter Wert wird
  vom Telemetrie-Simulator gelesen und sonst von nichts, und nur solange der
  Link unten ist. Einem antwortenden IOmcu wird stattdessen das rohe
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
