# Servoverfahren

<sub>[English](Servo.md) · **Deutsch**</sub>

Zwei Dinge, die dieser Prüfstand kann und ein Sender nicht, beide aus derselben
Messung, und beide über das Servo **wie eingebaut** statt über das Servo wie
gekauft. Keines davon steht in der kleinen Liste ohne Bauteile, denn beide
wollen einen Stromsensor je Servoausgang.

Das erste ist gebaut. Das zweite ist entworfen und hier benannt, damit seine
Form festgehalten ist, bevor es geschrieben wird.

## Die verbaute mechanische Endlage finden

Endpunkte, die man im Sender nach Augenmaß setzt, sind Schätzungen, und der
Preis für zu großzügiges Schätzen ist keine Warnung. Ein Servo, das gegen einen
mechanischen Anschlag gehalten wird, zieht Blockierstrom, solange man es darum
bittet — es kocht sich selbst, leert den Akku und verschleißt das Getriebe, und
niemand merkt es, bis etwas überdreht.

Der Anschlag hat eine Signatur. Solange die Fläche frei ist, ist der Strom über
der befohlenen Stellung flach und niedrig: es kostet fast nichts, eine
ausgewogene Fläche in einem Winkel zu halten. In dem Moment, in dem das Gestänge
klemmt, steigt der Strom steil, denn das Servo bewegt nichts mehr und drückt nur
noch.

Die Suche geht also in kleinen Schritten aus der Mitte heraus, lässt beruhigen,
misst und wartet auf den Knick — und **hält bei dem ersten Anstieg an, statt
hindurchzudrücken**, geht um eine Reserve zurück und meldet das als Endpunkt.

Es muss vor Ort gemessen werden. Die Endlage gehört dem Gestänge, der
Hornstellung und den Anschlägen der Fläche, nicht dem Servo — sie ist also in
jedem Einbau anders und lässt sich nicht nachschlagen. Genau deshalb lohnt sich
ein Prüfstand, der sie messen kann, und genau deshalb sind die beiden Enden des
Ausschlags einer Fläche keine Spiegelbilder voneinander.

### Der Knicktest hat zwei Hälften

Ein Schritt gilt erst dann als klemmend, wenn sein gemittelter Strom die frei
laufende Baseline **sowohl** um ein Verhältnis **als auch** um einen absoluten
Abstand schlägt. Jede Hälfte allein liest einen Fall falsch, den es wirklich
gibt:

| | Was sie allein falsch macht |
| --- | --- |
| Nur ein Verhältnis | Eine enge Stelle im Gestänge — ein Umlenkhebel, der leicht klemmt, ein Gestänge, das an einem Spant scheuert — ist auf einer leicht belasteten Fläche ein großes *Vielfaches* der Baseline und dabei fünfzig Milliampere. Die Suche bliebe beim ersten stehen und meldete ihn als Ende des Ausschlags. |
| Nur ein Abstand | Der Freilaufstrom eines großen Servos übersteigt jeden Abstand, den man für ein kleines wählt — der Test feuert also entweder nie oder sofort, je nachdem, für welches Servo man die Zahl bestimmt hat. |

Beides zu verlangen kostet nichts und beseitigt beides. Jede Hälfte hat einen
Test, der umfällt, wenn man sie löscht.

### Die Baseline sind mehrere Stellungen, nicht eine

Eine Fläche mit Vorspannung zieht in der Mitte am meisten und ein paar Grad
daneben weniger. Eine Baseline aus diesem einen Wert liegt hoch genug, um einen
echten Knick zu verdecken, also speisen die ersten drei Stellungen sie — und es
sind drei *Stellungen*, nicht drei Messungen einer einzigen, eine Unterscheidung,
die nur deshalb in den Code überlebt hat, weil ihr Löschen einen Test rot machte.

### Drei Schutzmaßnahmen, und keine braucht das Bedienteil

Das ist die eine Routine im Projekt, die ein Servo absichtlich auf etwas Festes
zutreibt — und damit der klarste Fall für die Regel des Koprozessors, Hardware
zu schützen, ohne zu fragen.

| | |
| --- | --- |
| **Eine harte Stromobergrenze** | Bricht sofort ab, wo immer sie überschritten wird — bei jedem Sample geprüft und nicht am Ende eines Messfensters, denn ein Servo, das seinen Anschlag noch in Fahrt erreicht, würde sonst die ganze Beruhigungszeit lang drücken. |
| **Ein Stall-Timeout** | Ein Strom über „arbeitet schwer" darf nicht andauern. Er hat eine eigene Schwelle statt der des Knicks, und er wird *über* Stellungen hinweg gemessen. Eine frühere Fassung schärfte ihn aus dem Knicktest, was bedeutete: den Knick zu hoch zu setzen, um überhaupt etwas zu finden, schaltete zugleich den Timer ab, der eine blind drückende Suche fangen sollte — die eine Konfiguration, in der er gebraucht wurde, war die, in der er nicht feuern konnte. |
| **Eine langsame Annäherung** | Zehn Mikrosekunden je Schritt, etwa ein Viertelgrad. Klein genug, dass ein Schritt das Horn nicht von frei bis hart gegen den Anschlag tragen kann, denn die ganze Methode hängt daran, den Knick zu *treffen* und nicht jenseits davon anzukommen. |

Eine Suche, die den gesamten erlaubten Ausschlag ohne Klemmen durchläuft,
meldet genau das, statt die Ausschlagsgrenze zurückzugeben, als wäre sie eine
mechanische Endlage.

### Warum es eine State Machine ist

`servo_limit_step()` bekommt einen Strommesswert und gibt die zu befehlende
Pulsweite zurück. Es schläft nie und fasst nie Hardware an, die Host-Suite lässt
es also gegen ein modelliertes Servo laufen und der Koprozessor wird es gegen ein
echtes laufen lassen, ohne Unterschied zwischen beiden. Das Modell liegt im
selben Verzeichnis und deklariert sich so, wie es [der
Telemetriesimulator](../README.md) tut: nichts, was es erzeugt, entscheidet auf
echter Hardware irgendetwas.

Die Standardsuche deckt 600 µs Ausschlag in 10-µs-Schritten mit 200 ms je
Schritt ab, also knapp acht Sekunden je Ende. Langsam ist der Punkt — aber ein
Verfahren, das niemand aussitzt, ist ein Verfahren, das niemand ausführt, also
hat die Dauer einen eigenen Test.

## Zwei Servos auf einer Fläche abstimmen

Zwei Servos auf einer Fläche — Doppelquerruder, Höhenruderhälften — arbeiten
über die Fläche gegeneinander, sobald ihr Ausschlag oder ihre Mitte nicht
übereinstimmen, und beide ziehen dafür dauerhaft zusätzlichen Strom. Nichts am
Modell sagt es einem; die Fläche sitzt einfach da, steif, und zieht das Doppelte
von dem, was sie sollte.

Das Ziel ist also gar keine Stellung. **Das Minimum des Gesamtstroms ist der
Punkt, an dem sie aufhören zu kämpfen** — und damit ist es eine eindimensionale
Suche mit einer physikalischen Antwort statt einer Ermessensfrage.

Und die beiden Fehler trennen sich sauber, was blindes Suchen erspart:

- Kämpfen **in der Mitte** ist ein Offsetfehler
- Kämpfen **an den Enden** ist ein Ausschlagsfehler

Man misst in der Mitte und an beiden Enden, und jede Korrektur fällt aus ihrer
eigenen Messung heraus. Jedes Ende bekommt seine eigene Zahl, aus demselben
Grund, aus dem die Endlagensuche beide Enden getrennt misst: ein Gestänge ist
nicht symmetrisch um die Mitte, sobald ein Horn und ein Gestänge im Spiel sind.

Der Beschleunigungssensor liefert das eine, was der Strom nicht sagen kann — ob
die beiden Servos einig und *beide* falsch sind. Eine Fläche, die nirgends steif
ist, aber fünf Grad daneben sitzt, ist für diese Messung unsichtbar, denn es gibt
nichts, worüber die beiden streiten könnten.

### Wie das Minimum gefunden wird

Ein Scan von grob nach fein, dreimal. Jede Stufe fährt eine Variable über ein
Fenster, lässt beruhigen und misst an jedem Punkt, nimmt den niedrigsten und
verengt das Fenster auf das Intervall zwischen dessen Nachbarn. Drei Runden zu
sieben Punkten bringen ein Fenster von ±40 µs auf etwa ±1,5 µs.

Gesamtstrom, nicht je Servo: minimiert wird, was die beiden zusammen ziehen, und
ein Sensor über beide Ausgänge reicht, um es zu finden — was zählt, denn Sensoren
sind das, woran es dieser ganzen Familie von Messungen fehlt.

### Die Beruhigungszeit hat zwei Teile

Zum nächsten Punkt eines Scans zu gehen bewegt ein paar Mikrosekunden. Die Stufe
zu wechseln schwenkt ein Servo über seinen ganzen Ausschlag, was bei einem
langsamen Servo deutlich über einer Sekunde liegt. Eine feste Wartezeit ist
entweder für die große Bewegung bemessen — und dann verbringt die Suche das
meiste damit, auf kleine zu warten — oder für die kleine, und dann wird der erste
Messwert jeder Stufe an zwei noch fahrenden Servos genommen.

Die Wartezeit ist also ein fester Teil plus so lange, wie die eben befohlene
Bewegung bei einer konfigurierten Slew-Schätzung dauern sollte. Diese Schätzung
sollte pessimistisch sein: sie zu langsam zu setzen kostet Zeit, sie zu schnell zu
setzen kostet die Antwort. Der Scan fährt in eine Richtung, ein Servo, das noch
nicht angekommen ist, ist also *immer* einen Schritt hinterher — ein Bias, der
sich nicht herausmittelt, sondern das Minimum verschiebt.

### Ein Minimum, das sie nicht sehen kann, wird gemeldet und nicht erfunden

Wenn der beste Punkt des ersten, weitesten Scans nicht um einen konfigurierten
Abstand unter dem schlechtesten liegt, hält die Suche an und sagt es.

Und man beachte, was das **nicht** heißt. Ein Paar, das schon synchron ist,
erzeugt trotzdem ein sauberes Minimum, denn ein Servo aus der Übereinstimmung
herauszubewegen ist genau das, was der Scan tut — ein korrekter Einbau zeigt sich
als Minimum *bei null Korrektur*, nicht als fehlendes Minimum. Die einzige
ehrliche Lesart, die bleibt, ist also, dass der Sensor den Kampf nicht auflösen
kann, und das zu sagen ist besser, als eine aus Rauschen zusammengesetzte
Korrektur zurückzugeben, die die Form einer Antwort hätte, ohne eine zu sein.

## Worauf beide warten

Das PWM des Koprozessors, und Strommessung an den Ausgängen — je Ausgang für die
Endlagensuche, und einer über das Paar für die Synchronisierung. Beides gibt es
noch nicht; siehe [das Protokoll](../README.md), wo das in der
Arbeitsreihenfolge steht.

Beide Suchen sind als State Machines geschrieben, die Messwerte bekommen und
Kommandos zurückgeben — was fehlt, ist also nur die Hardware darunter: derselbe
Code läuft heute in der Host-Suite gegen ein modelliertes Servo und gegen ein
echtes, sobald es eines gibt.
