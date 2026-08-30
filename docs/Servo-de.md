# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Zwei Messungen am Servo **wie eingebaut**, nicht wie gekauft. Beide brauchen
einen Stromsensor je Servoausgang, der noch nicht bestückt ist — heute laufen
sie deshalb gegen ein modelliertes Servo, und später unverändert gegen das
echte.

## Die verbaute mechanische Endlage finden

### Warum

Endpunkte, die im Sender nach Augenmaß gesetzt werden, sind Schätzungen — und
wer zu großzügig schätzt, merkt es nicht. Ein Servo, das gegen einen
mechanischen Anschlag gestellt wird, zieht Blockierstrom, solange es dort
gehalten wird: es überhitzt, leert den Akku und verschleißt das Getriebe, bis
irgendwann Zähne fehlen.

Nachschlagen lässt sich die Endlage nicht, denn sie gehört nicht dem Servo,
sondern dem Gestänge, der Hornstellung und den Anschlägen der Ruderfläche. Sie
ist in jedem Einbau anders — und an den beiden Enden desselben Ruderwegs
verschieden. Deshalb: beide Enden messen.

### Was die Suche tut

Sie tastet sich in Viertelgrad-Schritten aus der Mitte heraus, wartet jeweils
kurz und beobachtet den Strom. Solange die Fläche frei läuft, bleibt er flach
und niedrig. Sobald das Gestänge anläuft, steigt er steil — das Servo bewegt
nichts mehr, es drückt nur noch. Die Suche **hält beim ersten Anstieg an,
statt weiterzudrücken**, nimmt eine Sicherheitsreserve zurück und meldet das
als Endpunkt.

Mit den Standardeinstellungen dauert das knapp **acht Sekunden pro Ende**.
Langsam ist Absicht: die Methode lebt davon, den Anschlag sanft zu *treffen*,
nicht mit Schwung dahinter zu landen.

### Was das Servo dabei schützt

| | |
| --- | --- |
| eine harte Stromobergrenze | bricht sofort ab, bei jedem Messwert geprüft |
| ein Stall-Timeout | Strom über „arbeitet schwer" darf nicht andauern, egal wo |
| die langsame Annäherung | ein einzelner Schritt kann das Horn nicht von frei bis hart an den Anschlag tragen |

Alle drei laufen auf dem Koprozessor, und keiner fragt vorher um Erlaubnis.

### Das Ergebnis lesen

- Der gemeldete Endpunkt hat die Reserve schon abgezogen — er kann direkt
  übernommen werden.
- **„Keine Endlage gefunden"** heißt: der gesamte erlaubte Weg wurde
  durchlaufen, ohne dass etwas geklemmt hat. Das ist eine echte Antwort, kein
  Fehlschlag — im erlaubten Bereich gibt es schlicht keinen Anschlag.
- Eine Endlage überraschend nah an der Mitte kann eine echte Schwergängigkeit
  sein — ein klemmender Umlenkhebel, ein streifendes Gestänge — statt des
  Endes des Ruderwegs. Die Suche sichert die üblichen Fälle ab, aber ein
  Gestänge, das eine Messung wert ist, ist auch einen Blick wert.

## Zwei Servos auf einer Ruderfläche abstimmen

### Warum

Zwei Servos an einer Fläche — Doppelquerruder, geteiltes Höhenruder — arbeiten
gegeneinander, sobald Ruderweg oder Mittelstellung nicht übereinstimmen, und
ziehen dafür dauerhaft zusätzlichen Strom. Am Modell sieht man davon nichts:
die Fläche steht einfach da, steif, und braucht das Doppelte.

### Was die Suche tut

Wo die beiden aufhören, gegeneinander zu arbeiten, ist der Gesamtstrom am
niedrigsten. Die Suche fährt also eine Korrektur nach der anderen durch und
sucht dieses **Minimum** — eine physikalische Antwort, keine Einschätzung. Die
beiden Fehler trennen sich dabei sauber:

- Kampf **in der Mitte** ist ein Offsetfehler
- Kampf **an den Enden** ist ein Wegfehler

Gemessen wird in der Mitte und an beiden Enden, und jede Korrektur ergibt sich
aus ihrer eigenen Messung. Jedes Ende bekommt seinen eigenen Wert, denn sobald
Horn und Gestänge im Spiel sind, ist kein Anlenkweg symmetrisch zur Mitte.

Ein einziger Stromsensor **über das Paar** genügt — minimiert wird ja, was
beide zusammen ziehen.

### Das Ergebnis lesen

- Ein Paar, das schon zusammenpasst, liefert trotzdem ein sauberes Minimum —
  bei **null Korrektur**. Das ist der gesunde Befund.
- **„Kein Minimum"** heißt: der Sensor konnte über den breitesten Suchlauf
  keinen Kampf auflösen. Das wird gemeldet — statt einer Korrektur, die aus
  Rauschen zusammengesetzt wäre.
- Was der Strom nicht sehen kann: zwei Servos, die sich einig sind und *beide*
  falsch stehen. Eine Fläche, die nirgends steif ist, aber fünf Grad daneben
  hängt, gibt dem Paar nichts, worüber es streiten könnte — dieser Fall
  braucht den Beschleunigungssensor oder das Auge.

## Worauf beide warten

Die PWM-Ausgänge des Koprozessors und Strommessung an den Servoausgängen — je
Ausgang für die Endlagensuche, einer über das Paar für die Abstimmung. Wo das
in der Reihenfolge der Arbeiten steht, sagt
[das Protokoll](https://github.com/subtilitas/rcbench#readme).
