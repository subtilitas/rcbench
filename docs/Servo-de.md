# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Zwei Messungen am Servo im eingebauten Zustand. Beide brauchen einen
Stromsensor am Servoausgang, der nicht bestückt ist; beide laufen gegen ein
modelliertes Servo. Die Vorgabewerte unten stammen aus
`servo_limit_defaults()` und `servo_sync_defaults()` in `shared/servo/`.

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

## Voraussetzungen

Die Strommessung an den Servoausgängen: ein Sensor je Ausgang für die
Endlagensuche, einer über das Paar für den Abgleich. Keiner ist bestückt, und
beide Verfahren sind der Grund, warum sie gebraucht werden — jede Zahl hier
ist ein Strom.

Die Pulse selbst gibt es: der PWM-Treiber (Pulsweitenmodulation) des
Koprozessors ist geschrieben, [DShot und die Output-Treiber](DShot-de.md)
beschreibt ihn. Die Reihenfolge der Arbeiten steht in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
