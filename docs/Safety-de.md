# Sicherheit

<sub>[English](Safety.md) · **Deutsch**</sub>

Ein Prüfstand, der Propeller dreht, braucht das Anhalten als das eine, das nicht
leise versagen darf. Diese Seite beschreibt, wie das eingerichtet ist, und warum
so und nicht auf die naheliegende Art.

## Die Sicherheitsleitung ist ein Heartbeat, kein Enable

Der naheliegende Entwurf ist eine statische Enable-Leitung: das Bedienteil hält
einen Pin high, solange es die Ausgänge laufen lassen will, und zieht ihn zum
Anhalten runter.

Dieser Entwurf versagt beim wahrscheinlichsten Fehler. Die Firmware bleibt
hängen, der Pin bleibt high, und die Ausgänge bleiben scharf — der eine Fall, in
dem der Prüfstand am dringendsten anhalten soll, ist genau der, den ein Pegel
nicht ausdrücken kann.

Also **flankt GPIO6**, getrieben von der Schleife, die den Touch liest und den
STOP-Knopf zeichnet, und das Output Enable des Koprozessors samt seines Servo-
und ESC-Leistungspfads hängt hinter einem **retriggerbaren Monoflop**.

Ein Absturz, ein hängender Task, ein Reset, ein Brown-out und ein abgezogenes
Kabel zeigen sich dann identisch: keine Flanken, also kein Ausgang. Fail-safe
durch Abwesenheit, in Hardware, unabhängig von der Firmware an beiden Enden.

Zwei Verfeinerungen. Rauschen kann einen Heartbeat *vortäuschen*, also ist das
Monoflop der grobe Rückfall und der Koprozessor prüft die Periode zusätzlich in
Firmware. Und der Heartbeat wird von der Schleife getrieben, deren Lebendigkeit
er behauptet — nie von einem Timer oder einem eigenen Task, denn ein Heartbeat
aus einem Timer beweist, dass der Timer lebt, und das ist nicht die Frage.

### Was aus der Rate werden musste, und warum

Diese Seite trug **100 Hz bis 1 kHz** und ein Monoflop-Fenster von **20–50 ms**
aus einer Zeit, bevor das Bedienteil eine gemessene Frame Rate hatte. Diese
beiden Zahlen können nicht zusammen mit dem Absatz darüber bestehen, und der
Absatz ist der Teil, den es zu behalten lohnt.

Der Heartbeat kommt aus der Render-Schleife, also setzt die Render-Schleife die
Rate. Diese Schleife läuft mit 39 Hz, wenn sich auf dem Bildschirm nichts
ändert, und mit **19,5 Hz in den Frames, in denen ein Telemetriesample landet**
— siehe [Performance](Performance-de.md), warum 19,5 der Auslegungspunkt ist und
kein Defizit. Die schnellste ehrliche Flankenrate ist damit etwa 39 Hz und nicht
100, und der längste Abstand zwischen zwei Flanken etwa 52 ms. Ein Monoflop mit
50 ms würde die Ausgänge bei jedem zweiten Frame eines belasteten Prüfstands
fallen lassen.

Die Zahlen, die aus der Schleife folgen, alle in
[`shared/safety/heartbeat.h`](../shared/safety/include/heartbeat.h):

| | | |
| --- | ---: | --- |
| Das Bedienteil verlangt eine Flanke alle | **20 ms** | also jeden Frame, ohne dass der Generator die Frame Rate kennen muss |
| Die Schleife liefert tatsächlich eine alle | **26–52 ms** | 39 Hz im Leerlauf, 19,5 Hz in einem Sample-Frame |
| Die Firmware akzeptiert einen Abstand von | **4–150 ms** | unter der Untergrenze ist es Rauschen; über der Obergrenze hat das Bedienteil aufgehört zu zeichnen |
| Die Firmware traut der Leitung nach | **4 guten Abständen** | gut einer Zehntelsekunde |

> **Das ändert einen Bauteilwert auf einer Platine, die es noch nicht gibt.**
> Das Monoflop muss länger halten als der längste Abstand, den die
> Render-Schleife erzeugt, mit Reserve für einen Frame, der überzieht — sein
> Fenster gehört also auf **rund 150 ms** und nicht auf die 20–50 ms, die hier
> früher standen. Es fällt damit immer noch deutlich innerhalb des eigenen
> 200-ms-Link-Failsafes des Koprozessors. Wer die Tochterplatine layoutet, sollte
> das RC-Glied auf 150 ms auslegen und den alten Wert als zurückgezogen
> betrachten.

### Langsam im Vertrauen, sofort im Zweifel

Der Firmware-Monitor ist bewusst asymmetrisch. Er will vier aufeinanderfolgende,
sauber verteilte Abstände, bevor er die Leitung lebendig nennt, und ein einziger
schlechter — zu schnell, zu langsam, oder ein Fenster Stille — nimmt ihm das
sofort wieder.

Der Grund ist, dass die beiden Fehler nicht gleich schlimm sind. Einer Leitung zu
langsam zu trauen kostet eine Zehntelsekunde, bevor der Prüfstand scharf wird.
Einer zu bereitwillig zu trauen heißt, dass ein Rauschburst beim Einschalten
einen Ausgang freigeben kann. Ein Sicherheitsinterlock, das bei einem Glitch
freigibt und beim Sperren zögert, ist das genaue Gegenteil dessen, wonach es
benannt ist.

Den Fall, den das Monoflop nicht sehen kann, ist der schnelle: alles, was schnell
genug flankt, triggert es tadellos nach und würde die Ausgänge durchgehend
freigeben. Nur etwas, das weiß, welche Periode zu erwarten ist, kann das beim
Namen nennen — und genau deshalb existiert die Firmwareprüfung neben der
Hardwareprüfung und nicht an ihrer Stelle.

## Warum die Richtungsleitung nicht der Heartbeat sein kann

> **Gegenstandslos, seit der Link auf CAN umgezogen ist**, das gar keine
> Richtungsleitung hat. Die Begründung bleibt stehen, weil das erste Argument
> unten davon handelt, *was ein Signal beweist*, und das gilt für jeden
> Kandidaten für diese Aufgabe — auch für den nächsten, den jemand vorschlägt.

Es wurde gefragt, und es lohnt sich aufzuschreiben, denn die Idee ist nicht
albern — eine Leitung, die immer dann toggelt, wenn das Bedienteil sendet, *ist*
ein Beleg dafür, dass das Bedienteil sendet.

Sie scheitert an vier Punkten.

**Sie beweist, dass das Falsche lebt.** Von der UART-Peripherie getrieben — und
das tut der RS485-Half-Duplex-Modus von ESP-IDF — toggelt sie immer dann, wenn
die Peripherie Bytes zu senden hat. Ein DMA-gefütterter UART, der einen
eingereihten Fahrplan abarbeitet, toggelt tadellos weiter, während die
Anwendung hängt. Genau dieser Fehler ist der, für den es den Heartbeat gibt.

**Ein gedrücktes STOP kann auf einer Leitung nicht beides.** STOP reist als
Kommando *und* hält den Heartbeat an. Ist der Heartbeat die Richtungsleitung,
heißt ihn anzuhalten, das Senden anzuhalten — man kann also das Kommando nicht
mehr senden, für das man die Leitung gerade angehalten hat.

**Die Raten treffen sich nicht.** Die Untergrenze des Heartbeats weist alles
Schnellere als eine Flanke je 4 ms zurück, seine Obergrenze alles Langsamere als
eine je 150 ms, und das ist der Bereich der Render-Schleife und nicht der des
Links. Ein Telemetriefahrplan mit 20 Hz sitzt ganz unten darin, die Pollrate
würde also sicherheitskritisch — und [der Link](Link-de.md) hat keine Reserve zu
verbrennen.

**Sie bricht die Redundanz ein.** Aus drei unabhängigen Mechanismen werden zwei,
die sich einen Transceiver als gemeinsame Fehlerquelle teilen.

## Drei unabhängige Arten, wie dieser Prüfstand anhält

| | Was es fängt |
| --- | --- |
| **Das STOP-Kommando** reist über den Link | ein bewusstes Anhalten, quittiert und gemeldet |
| **Der Heartbeat hört auf** | das Bedienteil hängt, ist resettet, im Brown-out oder abgezogen |
| **Der eigene Stille-Watchdog des Koprozessors** | der Link ist in einer der Richtungen tot |

Ein gedrücktes STOP nutzt die ersten beiden gleichzeitig: es sendet das Kommando
**und** hält den Heartbeat an, statt einem von beiden allein zu trauen.

Hinter allen dreien steht die eigene Regel des Bedienteils, vom Vorgänger
geerbt und auf Hardware bewiesen: **die Anwendung entschärft und weigert sich,
erneut scharf zu schalten, wenn der Touch-Controller 500 ms lang nicht
antwortet** — denn das Bedienteil ist der einzige Ort, an dem es einen
STOP-Knopf gibt. Ein Prüfstand, den man nicht anhalten kann, ist nicht scharf,
er ist ausgebrochen.

Und noch eine, aus derselben Quelle: **den Testerbildschirm zu verlassen
entschärft.** Von einem scharfen Prüfstand wegzunavigieren darf keinen
drehenden Propeller hinter einem Bildschirm zurücklassen, auf dem man ihn nicht
mehr sieht.

## Der Koprozessor fragt nicht

Überstrom, Übertemperatur, Stall-Timeout, verlorener Link: der Koprozessor
handelt aus eigener Befugnis und meldet beim nächsten Poll, was er getan hat. Er
wartet nie auf die Erlaubnis, sicher zu versagen. Das ist der ganze Grund, warum
die elektrische Seite auf einen Prozessor umgezogen ist, der sonst nichts zu tun
hat.

## Was das gekostet und was es gebracht hat

Das Bedienteil treibt überhaupt keinen Servopuls mehr. GPIO6 war der
Gasausgang; er hat jetzt eine Aufgabe, und es ist diese. Das Interlock und die
Slew-Begrenzung überleben als Policy, die in den Koprozessor kompiliert wird —
dorthin, wo der Ausgang hingegangen ist.

Das Bedienteil ist damit außerstande, allein einen Motor zu drehen — nicht per
Konvention, sondern weil es keine Leitung von ihm zu irgendetwas gibt, das das
könnte.
