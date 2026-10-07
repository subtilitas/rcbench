# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Drei Verfahren am Servo. Die ersten beiden messen ein Servo im eingebauten
Zustand; beide brauchen einen Stromsensor am Servoausgang, der nicht bestückt
ist, und beide laufen gegen ein modelliertes Servo. Ihre Vorgabewerte stammen
aus `servo_limit_defaults()` und `servo_sync_defaults()` in `shared/servo/`.
Das dritte, der [automatische Test](#automatischer-test), liest den Strom des
Netzteils, das das Servo versorgt, und ist nur gegen die Servo- und
Netzteilmodelle gelaufen.

## Eingebaute mechanische Endlage

### Zweck

Im Sender gesetzte Endpunkte sind Schätzungen. Ein Servo, das gegen einen
mechanischen Anschlag gehalten wird, zieht Blockierstrom, solange es dorthin
befohlen wird, und der Einbau, nicht das Servo, bestimmt, wo der Anschlag
liegt. Die beiden Enden des Ruderwegs unterscheiden sich; beide messen.

### Verfahren

Die Suche schreitet von der Mitte nach außen, in Schritten von 10 µs Pulsweite
(etwa 0,9° bei einem Servo mit 11 µs je Grad Weg), bis 600 µs von der Mitte.
Nach jedem Schritt wartet sie 120 ms, bis das Servo steht, und mittelt den
Strom über 80 ms. Solange die Fläche frei läuft, ist der Strom flach und
niedrig. Das Gestänge gilt als angelaufen, wenn der Strom das 1,8-Fache des
Grundstroms im freien Lauf übersteigt und mindestens 0,15 A darüber liegt. Die
Suche hält beim ersten solchen Schritt an, nimmt 25 µs zurück und meldet diese
Pulsweite als Endpunkt.

Dauer: der Host-Test begrenzt eine vollständige Suche gegen das modellierte
Servo auf unter 12 s. Auf Hardware nicht gemessen.

### Schutzmechanismen

| Schutz | Wert | Wirkung |
| --- | ---: | --- |
| Stromobergrenze | 3,0 A | sofortiger Abbruch, bei jedem Messwert geprüft |
| Stall-Timeout | über 1,0 A für 400 ms | Abbruch |
| Schrittweite | 10 µs | ein Schritt kann das Horn nicht von frei bis hart an den Anschlag bewegen |

Alle drei laufen auf dem Koprozessor.

### Ergebnisse

- Ein Endpunkt wird bereits um 25 µs zurückgenommen gemeldet.
- „Keine Endlage gefunden“ heißt: die Suche hat 600 µs von der Mitte erreicht,
  ohne dass das Gestänge angelaufen ist.
- Eine Endlage nahe der Mitte kann eine Schwergängigkeit im Gestänge sein,
  etwa ein klemmender Umlenkhebel oder eine streifende Schubstange, statt des
  Endes des Ruderwegs.

## Zwei Servos auf einer Ruderfläche abgleichen

### Zweck

Zwei Servos an einer Fläche (Doppelquerruder, geteiltes Höhenruder) arbeiten
gegeneinander, sobald ihr Weg oder ihre Mitte nicht übereinstimmen, und ziehen
dafür dauerhaft zusätzlichen Strom. Die Fläche zeigt kein sichtbares Zeichen.

### Verfahren

Die beiden hören am Punkt des kleinsten Gesamtstroms auf, gegeneinander zu
arbeiten. Die Suche hält Servo A fest und fährt eine Korrektur an Servo B
durch, in der Mitte und bei ±300 µs Ausschlag:

- ein Unterschied in der Mitte ist ein Offsetfehler (Trimmung);
- ein Unterschied an einem Ende ist ein Wegfehler für dieses Ende.

Jeder Suchlauf deckt ±40 µs um den aktuell besten Punkt in 7 Schritten ab und
engt dreimal ein. Jeder Punkt wartet 120 ms plus die Fahrzeit bei angenommenen
0,8 µs/ms und mittelt den Strom dann über 100 ms. Ein Minimum gilt, wenn der
Strom über den Suchlauf um mindestens 0,08 A schwankt. Die Stromobergrenze
für das Paar liegt bei 4,0 A.

Jedes Ende bekommt seine eigene Korrektur; ein Gestänge mit Horn und
Schubstange ist nicht symmetrisch zur Mitte. Ein Stromsensor über das Paar
genügt.

### Ergebnisse

- Ein Paar, das bereits abgeglichen ist, liefert ein Minimum bei Korrektur
  null.
- „Kein Minimum“ heißt: der Strom schwankt über den breitesten Suchlauf um
  weniger als 0,08 A.
- Zwei Servos, die sich einig sind und beide falsch stehen, erzeugen keinen
  Stromunterschied. Dieser Fall braucht den Beschleunigungssensor oder eine
  Sichtprüfung.

## Automatischer Test

### Zweck

Was ein Servo in Ruhe, in Bewegung und beim Halten eines Endes zieht und wie
lange es von Ende zu Ende braucht, bei den Versorgungsspannungen, für die es
ausgelegt ist; und die Spannung, unter der es sich nicht mehr bewegt
(Brown-out). Nichts am Prüfstand misst das Horn, also wird jedes Ergebnis am
Strom des Netzteils abgelesen, das das Servo versorgt: am PD mini (WeAct PD
Power Mini V1), wenn SETUP ANSCHLÜSSE ihn freigibt, sonst am Netzteilmodell
des Panels. Ein Lauf am Modell sagt das in seinem Bericht, und seine Zahlen
sind simuliert.

TEST STARTEN auf der TEST-Seite von SERVO startet ihn;
[Bildschirme](Screens-de.md#servo) beschreibt die Bedienung. Die Engine ist
`shared/servo/servo_test.c`; die Konstanten unten stehen in `servo_test.h`.

### Verfahren

Die Spannungsstufen sind STUFE 4.8 V und STUFE 6.0 V der TEST-Seite, und mit HV
SERVO an STUFE 7.4 V und STUFE 8.4 V. HV SERVO ist nach jedem Neustart aus. Ein
Lauf mit einer Stufe über 6,0 V startet nur über die Warnung NUR HV-SERVOS,
2 s gehalten. Die Strombegrenzung im Lauf ist die der Zeile SOLL.

Jede Stufe durchläuft diese Phasen:

| Phase | Das Servo | Endet |
| --- | --- | --- |
| SETZEN | auf PULS CENTRE; die Spannung wird verlangt; die erste Stufe schaltet den Ausgang ein | wenn das Netzteil den Sollwert auf 0,05 V genau zurückliest, spätestens nach 3000 ms |
| EINSCHWINGEN | auf PULS CENTRE | nach EINSCHWINGEN der TEST-Seite |
| RUHE | auf PULS CENTRE: der Ruhestrom ist der Mittelwert der Messwerte | nach 1000 ms |
| BEWEGEN | ein Sprung auf ein Ende, ohne Rampe: TEMPO gilt nicht | bei der Ankunft oder nach 3000 ms (verspätet) |
| HOLD | an diesem Ende: der Haltestrom ist der Mittelwert der Messwerte | nach VERWEILEN, mindestens 600 ms |

Die beiden Enden sind die von SWEEP: BEREICH des Wegs zu beiden Seiten von
PULS CENTRE, ohne Trim. Mit den Vorgaben (BEREICH 80 %, WEG +/-90 Grad,
PULS MIN 1000 us, MAX 2000 us) sind das 1100 und 1900 us. KURVE und TEMPO
gelten nur für SWEEP.

Auch ein Lauf, der zu Ende geht, schaltet den Ausgang aus und gibt das Servo
zur Mitte frei. Die ersten beiden Bewegungen einer Stufe, von der Mitte zum
unteren Ende und weiter zum oberen, werden nicht gezählt: sie messen den Haltestrom an jedem
Ende. Danach gehen die gezählten Bewegungen von Ende zu Ende, BEWEGUNGEN viele
oder für TESTZEIT, wie LÄNGE NACH sagt, höchstens 1000 je Stufe.

- **Bewegung:** ein Messwert, der mehr als 0,10 A (`SERVO_TEST_MOVE_A`) vom
  Wert vor dem Befehl entfernt ist: darüber, oder darunter, wenn das Servo
  ein Ende verlässt, an dem es gegen einen Anschlag gedrückt hat.
- **Ankunft:** nach Bewegung ein Messwert mehr als 0,10 A über dem
  Haltestrom des Zielendes, dann der erste Messwert, der wieder auf 0,05 A
  (`SERVO_TEST_BAND_A`) daran liegt, auf jeder Seite. Ein Messwert, der
  stattdessen mehr als 0,05 A unter den Wert fällt, wie nach einem
  Stromstoß beim Beschleunigen, übergibt die Bewegung der Regel unten. Die
  Halteströme der beiden Enden können
  sich um mehr als 0,05 A unterscheiden; ein Messwert, der noch auf dem
  Wert des Startendes liegt, oder ein steigender Strom, der den Wert des
  Ziels durchläuft, ist keine Ankunft.
- **Ankunft an einem Ende, das härter gehalten wird, als das Servo sich
  bewegt**, einem Ende am Anschlag: dieser Wert wird nie überschritten,
  also ist die Bewegung beim ersten von zwei Messwerten in Folge angekommen,
  die nach Bewegung auf 0,05 A am Wert und aneinander liegen. Ein Strom, der
  den Wert des Ziels mit weniger als 0,05 A je Messwert durchsteigt, kann
  dort für eine Ankunft gehalten werden.
- **Nicht unterscheidbar:** ein Servo, dessen Strom in Bewegung auf 0,05 A
  am Haltestrom des Ziels liegt, ist von einem, das schon dort steht, nicht
  zu unterscheiden; eine Bewegung zu diesem Ende endet bei ihren ersten zwei
  Messwerten.
- **Stellzeit:** vom Befehl bis zum Messwert der Ankunft.
- **Strom in Bewegung:** der Mittelwert der Messwerte zwischen Befehl und
  Ankunft; der Spitzenwert ist der höchste davon.

**Brown-out.** Nach den Stufen, mit BROWN-OUT an: ab 5,00 V
(`SERVO_TEST_BROWNOUT_START_V`) oder der geltenden Spannungsgrenze, wenn
die niedriger ist, in Schritten von 0,20 V (`SERVO_TEST_BROWNOUT_STEP_V`)
abwärts bis 3,00 V (`SERVO_TEST_BROWNOUT_FLOOR_V`) oder bis zum kleinsten
Sollwert des Netzteils, wenn der höher liegt. Die geltende Grenze ist die
niedrigste aus SPANNUNG MAX, dem Höchstwert des Netzteils und beim PD mini
seiner Eingangsspannung abzüglich 0,5 V Reserve. Eine Untergrenze neben dem
0,20-V-Raster ist die letzte Stufe: am Modell, dessen kleinster Sollwert
3,3 V ist, geht der Lauf 5,0, 4,8 ... 3,4, 3,3 V. Jede Spannung durchläuft SETZEN, EINSCHWINGEN
und RUHE, dann zwei Bewegungen (`SERVO_TEST_BROWNOUT_MOVES`), von der Mitte
zum oberen Ende und weiter zum unteren, jede 600 ms gehalten. Eine Spannung
zeigt **keine Bewegung**, wenn kein Messwert der beiden Bewegungen mehr als
0,10 A vom Wert vor ihrem Befehl entfernt liegt. Der Lauf abwärts endet bei
der ersten Spannung ohne Bewegung; der Bericht nennt sie und die niedrigste,
bei der sich das Servo noch bewegt hat.

| Konstante | Wert | Bedeutung |
| --- | ---: | --- |
| `SERVO_TEST_MOVE_A` | 0,10 A | Bewegung; über dem Wert des Ziels, noch nicht angekommen |
| `SERVO_TEST_BAND_A` | 0,05 A | am Haltestrom angekommen |
| `SERVO_TEST_IDLE_MS` | 1000 ms | die Ruhestrommessung |
| `SERVO_TEST_HOLD_MIN_MS` | 600 ms | das kürzeste gemessene Halten |
| `SERVO_TEST_TRAVEL_TIMEOUT_MS` | 3000 ms | eine Bewegung, die nicht angekommen ist, ist verspätet |
| `SERVO_TEST_SET_TOL_V` | 0,05 V | ein zurückgelesener Sollwert |
| `SERVO_TEST_SET_TIMEOUT_MS` | 3000 ms | für den Sollwert und für das Einschalten |
| `SERVO_TEST_STALE_MS` | 1500 ms | kein neuer Messwert beendet den Lauf |
| `SERVO_TEST_STALL_ABORT_MS` | 1000 ms | so lange über BLOCKIERT AB beendet den Lauf |
| `SERVO_TEST_BROWNOUT_START_V` | 5,00 V | wo der Brown-out-Lauf beginnt |
| `SERVO_TEST_BROWNOUT_STEP_V` | 0,20 V | jeder Schritt abwärts |
| `SERVO_TEST_BROWNOUT_FLOOR_V` | 3,00 V | die niedrigste verlangte Spannung |
| `SERVO_TEST_BROWNOUT_MOVES` | 2 | Bewegungen je Brown-out-Spannung |

### Zeitauflösung

Ein Messwert zählt einmal. Beim PD mini ist der Zähler das Register SAMPLES
der SUPPLY-Seite: die Zahl der beantworteten Ausgangsmessungen des Moduls,
vom Koprozessor gezählt. Ein Lesen der Seite, bei dem SAMPLES stehen
geblieben ist, bringt keinen neuen Messwert, gleich welche Spannung und
welchen Strom es trägt -- ein Display-Lesen auf dem Koprozessor, das langsam
war (bis 400 ms, `PDMINI_REPLY_MS`) oder fehlschlug, lässt die letzten stehen
--, und ein Lauf ohne neuen Messwert über 1,5 s endet. Ein Messwert trägt die
Panelzeit des ersten Lesens der Seite, das seinen Zählerstand zeigte; das
Modell zählt und stempelt seine eigenen Schritte. Der Koprozessor liest den
Ausgang des PD mini alle 100 ms (`PDMINI_DISPLAY_MS`), das Panel die Seite
alle 100 ms (`SUPPLY_LINK_READ_MS`); ein neuer Messwert erreicht den Test
also bestenfalls alle 100 ms, beim Modell alle 50 ms. Springt SAMPLES
zwischen zwei Lesevorgängen um mehr als eins, erreichen die Messwerte
dazwischen den Test nie: der Bericht zählt sie in seiner Zeile `Skipped`, und
die Rate des Netzteils schließt sie ein. Der Bericht nennt beide Raten, im
Lauf gemessen, und den mittleren Abstand zweier Messwerte, die den Test
erreicht haben.

Eine Stellzeit endet am ersten Messwert, der wieder am Haltestrom liegt; sie
ist also um bis zu einen Abstand zu lang. Sie enthält auch den Weg des Befehls
vom Panel zum Pin: Render-Schleife, Control-Task, CAN-Link (Controller Area
Network) und den nächsten PWM-Frame (Pulsweitenmodulation). Diese
Verzögerung ist nicht gemessen. Wie der PD mini einen Messwert mittelt, ist
nicht bekannt.

### Was einen Lauf beendet

Jedes Ende schaltet den Ausgang aus, gibt das Servo zur Mitte frei und
schreibt trotzdem den Bericht, als ABGEBROCHEN mit dem Grund markiert. Der
Prüfstand bleibt scharf, außer das Ende war ein Disarm, STOP oder das
Verlassen der Seite, die entschärfen. Ist ein Lauf vorbei, gehen die Sollwerte von SUPPLY auf ihre Werte vor dem
Lauf zurück, gleich welche Seite oben ist. Das wartet, bis das OFF des
Laufs gesendet ist, ein danach genommener Messwert zeigt, dass das Netzteil selbst den Ausgang
aus meldet,
kein ON unterwegs ist und AUSGANG EIN weder auf SERVO noch auf SUPPLY
gehalten wird. Sollwerte, die nach
dem Ende des Laufs geändert wurden, bleiben, wie sie sind.

| Grund im Bericht | Ursache |
| --- | --- |
| `STOP` | STOP in der Leiste |
| `Prüfstand DISARMED` | DISARM oder alles andere, was den Prüfstand entschärft hat |
| `Link verloren` | der Link zum Koprozessor ging während des Laufs verloren |
| `Seite SERVO verlassen` | eine andere Seite wurde geöffnet |
| `vom Bediener gestoppt` | TEST BEENDEN, ein Finger auf der Skala, CENTRE, SWEEP, FREIGEBEN, ein Tippen auf einen Sollwert |
| `Servo-Optionen geändert` | Typ, Frame Rate, Impulse, Trim, Weg, Reverse oder TEMPO geändert |
| `Touch-Ereignisse verloren` | zwischen zwei Frames gingen Ereignisse verloren |
| `Netzteil antwortet nicht` | ein Messwert als nicht antwortend markiert |
| `Netzteil abgeschaltet` | der Ausgang ging durch eine Abschaltschwelle aus |
| `Netzteilausgang ging aus` | der Ausgang ging anders aus, AUSGANG AUS eingeschlossen |
| `seit 1.5 s kein Messwert` | `SERVO_TEST_STALE_MS` |
| `Netzteilausgang ging nicht an` | 3000 ms nach dem ON nicht an |
| `Sollwert 3 s nicht bestätigt` | das Netzteil hat die Spannung einer Stufe nicht übernommen |
| `Stufe über Spannungsobergrenze` | eine Stufe über der geltenden Grenze: SPANNUNG MAX oder der Eingang des PD mini abzüglich seiner Reserve |
| `1 s über BLOCKIERT AB` | `SERVO_TEST_STALL_ABORT_MS` |

Eine Stufe wird nie über der Grenze verlangt: ein Lauf, dessen Stufen
außerhalb des Bereichs des Netzteils liegen, wird bei START abgelehnt, und
eine Grenze, die während des Laufs sinkt, beendet ihn vor der Stufe darüber.

### Urteil

BESTANDEN, außer eines davon trifft zu, über die Spannungsstufen (der
Brown-out-Lauf wird berichtet, nicht beurteilt):

- der höchste Ruhestrom liegt über RUHESTROM;
- der höchste Haltestrom liegt über HALTESTROM;
- die längste Stellzeit liegt über STELLZEIT;
- ein Messwert nach EINSCHWINGEN liegt über BLOCKIERT AB;
- eine gezählte Bewegung war verspätet, was bei einem Servo, das sich nicht
  bewegt, immer zutrifft.

Ein Wert 0 auf der GRENZEN-Seite wird nicht geprüft; BLOCKIERT AB immer.

### Dateien

Ein Lauf nimmt die nächste Laufnummer auf der Karte, wie ein scharfer
Prüfstand, und die eigene Task der SD-Karte schreibt seine Dateien:
`BENCHnnn.CSV`, eine Zeile je Messwert des Netzteils, und mit BERICHT an
(PRÜFLING-Seite) `BENCHnnn.TXT`. Das eigene Lauflog eines scharfen Prüfstands
läuft daneben in einer eigenen Datei weiter. Eine Nummer, die nur ein
`BENCHnnn.TXT` trägt, dessen CSV am Computer gelöscht wurde, ist trotzdem
vergeben, damit kein Bericht überschrieben wird, und LÖSCHEN in der
Log-Ansicht löscht mit einem Lauf seinen Bericht. Ein Lauf, den die Karte nicht
annimmt, sagt das in der Leiste und im Ergebnis, `OHNE DATEI`. Zeilen, für
die die Warteschlange zur Karte keinen Platz hatte, zählt der Bericht.

Die CSV, mit Semikolon getrennt und Dezimalpunkt, wie jedes Log des
Prüfstands:

| Spalte | Einheit | Bedeutung |
| --- | --- | --- |
| `time (s)` | s | wann der Messwert genommen wurde, ab Laufbeginn |
| `test` | | `STEP` oder `BROWN-OUT` |
| `step` | | die Stufe, 1 bis n in der Reihenfolge des Laufs |
| `phase` | | `SET`, `SETTLE`, `IDLE`, `MOVE` oder `HOLD` |
| `command (us)` | us | der befohlene Impuls |
| `position (us)` | us | die gemessene Stellung; leer, weil nichts sie misst |
| `set (V)` | V | die Spannung der Stufe |
| `voltage (V)` | V | am Ausgang |
| `limit (A)` | A | die Strombegrenzung |
| `current (A)` | A | aus dem Ausgang |
| `power (W)` | W | Spannung mal Strom |
| `mode` | | `CV`, `CC` oder `OFF` |
| `travel (ms)` | ms | in der Zeile einer Ankunft: die Stellzeit dieser Bewegung |

Der Bericht steht in der Sprache, die beim Start seines Laufs gilt; seine
deutschen Wörter liegen in `shared/ui/ui_text_de.c`, die englischen in
`shared/servo/servo_report.c` ([Sprache der Oberfläche](Language-de.md)).
Die CSV ist in jeder Sprache englisch. Ein englischer Bericht aus einem Lauf
gegen die Servo- und Netzteilmodelle der Host-Suite steht auf der
[englischen Seite](Servo.md#files). Das modellierte Servo legt 800 us in
667 ms zurück; die 700 ms im Bericht sind das, um die 100 ms zwischen zwei
Messwerten zu spät. Ein abgebrochener Lauf zeigt `Ergebnis: ABGEBROCHEN -
<Grund>`, eine abgeschnittene Stufe ist mit `(verkürzt)` markiert, eine nie
erreichte mit `nicht gelaufen`. `Kann zerstören` nennt das rote Schild, wenn
ein Heli-Typ oder eine Frame Rate über 60 Hz gilt.

### Nicht auf Hardware gelaufen

Kein Lauf hat ein Servo oder einen PD mini betrieben. Die Host-Suite prüft die
Engine gegen `servo_sim` und `supply_sim`, die Seite SERVO, die sie führt,
und die CSV, vom Parser der Log-Ansicht zurückgelesen. Nicht gemessen: die
wirkliche Rate der Messwerte über den Koprozessor, die Mittelung des PD mini,
die Verzögerung des Befehls bis zum Pin, und ob der Strom eines echten Servos
auf 0,05 A genau auf seinen Haltestrom zurückfällt.

## Sweep und TEMPO

SWEEP auf der Seite SERVO fährt das Servo die Kurve der TEST-Seite ab.
TEMPO auf der rechten Karte begrenzt, wie schnell sich der Ausgang bewegen
darf. Ist TEMPO langsamer als die schnellste Änderung, die die Kurve
verlangt, bestimmt TEMPO die Bewegung statt der Kurve, und die Zeile von
TEMPO lautet in der Warnfarbe TEMPO BEGRENZT DEN SWEEP
([Bildschirme](Screens-de.md#servo)).

Die schnellste Änderung, mit f der Rate der TEST-Seite in Hz und A der
Amplitude in Grad (BEREICH des Wegs zu beiden Seiten von PULS CENTRE):

| KURVE | Schnellste Änderung | Wo |
| --- | --- | --- |
| Rechteck | ein Sprung | bei jedem Wechsel des Endes |
| Sinus | 2 pi f A | durch die Mitte |
| Dreieck | 4 f A | überall |

TEMPO unter 100 % erlaubt 3,6 Grad/s je Prozent: 36 Grad/s bei 10 %,
356,4 Grad/s bei 99 %. Bei 100 % wird der Befehl nicht verlangsamt, und
nichts wird begrenzt. Ein Rechteck ist bei jedem TEMPO unter 100 %
begrenzt. VERWEILEN zählt nicht: es fügt Zeit an den Enden hinzu, nicht zur
Bewegung. Die Zeile folgt den Einstellungen, vor dem Druck auf SWEEP und
während er läuft. `sweep_slew_limited()` in `shared/servo/servo_sweep.c`
entscheidet das, in den Befehlseinheiten, in denen der Koprozessor die Rampe
fährt.

| TEST-Seite | A | Schnellste Änderung | Verschwindet ab TEMPO |
| --- | ---: | ---: | ---: |
| Sinus, 0,5 Hz, BEREICH 80 %, WEG +/-90 Grad | 72 Grad | 226 Grad/s | 63 % |
| Dreieck, 0,5 Hz, BEREICH 80 %, WEG +/-90 Grad | 72 Grad | 144 Grad/s | 40 % |
| Rechteck, beliebig | beliebig | ein Sprung | 100 % |

Die Zeile verschwindet, wenn TEMPO steigt oder TEMPO (die Rate) oder
BEREICH auf der TEST-Seite sinkt.

Die Zeile vergleicht die Kurve nur mit TEMPO. Ein Servo, das langsamer ist
als beide, begrenzt den Sweep ebenfalls; nichts auf dem Prüfstand misst das
Ruderhorn, daher wird das nicht angezeigt.

## Voraussetzungen

Die Strommessung an den Servoausgängen: ein Sensor je Ausgang für die
Endlagensuche, einer über das Paar für den Abgleich. Keiner ist bestückt, und
beide Verfahren sind der Grund, warum sie gebraucht werden — jede Zahl hier
ist ein Strom. Der automatische Test braucht keinen davon: er liest den PD
mini, verdrahtet und in SETUP ANSCHLÜSSE freigegeben, oder läuft am
Netzteilmodell des Panels.

Die Pulse selbst gibt es: der PWM-Treiber (Pulsweitenmodulation) des
Koprozessors ist geschrieben, [DShot und die Output-Treiber](DShot-de.md)
beschreibt ihn. Die Reihenfolge der Arbeiten steht in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
