# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Zwei Messungen über das Servo **wie eingebaut** statt über das Servo wie
gekauft. Beide brauchen einen Stromsensor je Servoausgang, der noch nicht
bestückt ist — beide laufen heute also gegen ein modelliertes Servo, und gegen
das echte an dem Tag, an dem der Sensor daran ist, ohne Änderung dazwischen.

## Die verbaute mechanische Endlage finden

### Warum

Endpunkte, die man im Sender nach Augenmaß setzt, sind Schätzungen, und der
Preis für zu großzügiges Schätzen ist still: ein Servo, das gegen einen
mechanischen Anschlag gehalten wird, zieht Blockierstrom, solange man es darum
bittet. Es kocht sich selbst, leert den Akku und verschleißt das Getriebe, und
niemand merkt es, bis etwas überdreht.

Die Endlage lässt sich nicht nachschlagen, denn sie gehört nicht dem Servo.
Sie gehört dem Gestänge, der Hornstellung und den Anschlägen der Fläche — in
jedem Einbau anders, und an den beiden Enden des Ausschlags einer Fläche
verschieden. Beide Enden messen.

### Was es tut

Die Suche geht ein Viertelgrad auf einmal aus der Mitte heraus, lässt
beruhigen und beobachtet den Strom. Solange die Fläche frei ist, ist der Strom
flach und niedrig. In dem Moment, in dem das Gestänge klemmt, steigt er steil
— das Servo bewegt nichts mehr und drückt nur noch. Die Suche **hält beim
ersten Anstieg an, statt hindurchzudrücken**, geht um eine Reserve zurück und
meldet das als Endpunkt.

Rechne mit knapp **acht Sekunden je Ende** bei den Standardeinstellungen.
Langsam ist Absicht: die Methode hängt daran, den Anschlag sanft zu *treffen*
statt mit Schwung jenseits davon anzukommen.

### Was das Servo währenddessen schützt

| | |
| --- | --- |
| eine harte Stromobergrenze | bricht sofort ab, bei jedem Sample geprüft |
| ein Stall-Timeout | Strom über „arbeitet schwer" darf nicht andauern, wo auch immer |
| die langsame Annäherung | ein Schritt kann das Horn nicht von frei bis hart gegen den Anschlag tragen |

Alle drei laufen auf dem Koprozessor, und keiner fragt vorher um Erlaubnis.

### Das Ergebnis lesen

- Ein Endpunkt wird bereits um die Reserve zurückgenommen gemeldet — nimm ihn,
  wie er ist.
- **„Keine Endlage gefunden"** heißt, die Suche hat den gesamten erlaubten
  Ausschlag ohne Klemmen durchlaufen. Das ist eine echte Antwort und kein
  Fehlschlag: die Mechanik hat innerhalb des erlaubten Bereichs keinen
  Anschlag getroffen.
- Eine Endlage überraschend nah an der Mitte kann eine echte enge Stelle sein
  — ein klemmender Umlenkhebel, ein scheuerndes Gestänge — statt des Endes des
  Ausschlags. Der Test sichert die üblichen Fälle ab, aber ein Gestänge, das
  eine Messung wert ist, ist auch einen Blick wert.

## Zwei Servos auf einer Fläche abstimmen

### Warum

Zwei Servos auf einer Fläche — Doppelquerruder, Höhenruderhälften — arbeiten
über die Fläche gegeneinander, sobald ihr Ausschlag oder ihre Mitte nicht
übereinstimmen, und beide ziehen dafür dauerhaft zusätzlichen Strom. Nichts am
Modell sagt es einem. Die Fläche sitzt einfach da, steif, und zieht das
Doppelte von dem, was sie sollte.

### Was es tut

Der Punkt, an dem die beiden aufhören zu kämpfen, ist der Punkt des
**minimalen Gesamtstroms** — die Suche fährt also eine Korrektur nach der
anderen ab und findet dieses Minimum, ohne Ermessen. Die beiden Fehler trennen
sich sauber:

- Kämpfen **in der Mitte** ist ein Offsetfehler
- Kämpfen **an den Enden** ist ein Ausschlagsfehler

Gemessen wird in der Mitte und an beiden Enden, und jede Korrektur fällt aus
ihrer eigenen Messung. Jedes Ende bekommt seine eigene Zahl, denn ein Gestänge
ist nicht symmetrisch um die Mitte, sobald ein Horn und ein Gestänge im Spiel
sind.

Ein Stromsensor **über das Paar** reicht — minimiert wird, was die beiden
zusammen ziehen.

### Das Ergebnis lesen

- Ein Paar, das schon abgestimmt war, liefert trotzdem ein sauberes Minimum —
  bei **null Korrektur**. Das ist der gesunde Befund.
- **„Kein Minimum"** heißt, der Sensor konnte über den weitesten Scan keinen
  Kampf auflösen. Die Suche sagt das, statt eine aus Rauschen
  zusammengesetzte Korrektur zurückzugeben.
- Was der Strom nicht sehen kann: zwei Servos, die sich einig und *beide*
  falsch sind. Eine Fläche, die nirgends steif ist, aber fünf Grad daneben
  sitzt, gibt dem Paar nichts zu streiten — dieser Fall braucht den
  Beschleunigungssensor oder dein Auge.

## Worauf beide warten

Das PWM des Koprozessors und Strommessung an den Servoausgängen — je Ausgang
für die Endlagensuche, einer über das Paar für die Abstimmung. Siehe
[das Protokoll](https://github.com/subtilitas/rcbench#readme), wo das in der
Arbeitsreihenfolge steht.
