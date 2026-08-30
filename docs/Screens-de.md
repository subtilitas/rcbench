# Bildschirme

<sub>[English](Screens.md) · **Deutsch**</sub>

Den Prüfstand bedienen: was immer auf dem Bildschirm steht, was die Marken im
Menü bedeuten, und wie die einzelnen Bildschirme benutzt werden.

## Das Statusband

Der obere Streifen gehört auf jedem Bildschirm dem Prüfstand. Von rechts nach
links: Linkzustand, Ausgangsmodus, scharf oder nicht, Laufzeit — und STOP.

**STOP funktioniert immer.** Es entschärft von jedem Bildschirm aus und
rastet ein: der Prüfstand bleibt gestoppt, bis du bewusst wieder scharf
schaltest. Nichts anderes hebt einen Stopp auf — kein Bildschirmwechsel, kein
ablaufender Hinweis, kein zurückkehrender Link.

ARM sitzt unten am Prüfstandsbildschirm, STOP oben im Band — eine volle
Bildschirmhöhe dazwischen, damit der Griff nach dem einen nicht versehentlich
das andere trifft.

## Was die Marken im Menü bedeuten

![Das Funktionsmenü](img/overview.png)

| Marke | Bedeutung |
| --- | --- |
| **SOON** | den Bildschirm gibt es noch nicht |
| **MODELLED** | den Bildschirm gibt es, aber seine Hardware ist nicht bestückt — jede Zahl darin ist erfunden, und der Bildschirm sagt das dazu |
| keine | das Bauteil ist da, die Messwerte sind echt |

MODELLED korrigiert sich von selbst: die Marke kommt aus dem, was der
Koprozessor als tatsächlich bestückt meldet, und verschwindet, sobald das
Bauteil eingebaut ist. Fehlende Hardware sperrt nichts — der Bildschirm öffnet
trotzdem und arbeitet aus dem Modell, mit der Marke, die genau das sagt.

Das Menü gibt es auch im hellen Theme:

![Das Menü im hellen Theme](img/overview-light.png)

## Der Splash ist der Selbsttest

![Der Splash](img/splash.png)

Jedes Subsystem meldet beim Hochfahren sein eigenes Ergebnis. Eine fehlende
Karte, ein stummer Touch-Controller oder ein Koprozessor mit der falschen
Protokollversion stehen also hier — statt drei Bildschirme später als
rätselhafter Defekt aufzutauchen. Ein Fehlschlag ist eine Meldung, keine
Sackgasse: lesen und weiter. Wenn alle Schritte geantwortet haben, wartet die
Liste kurz und übergibt ans Menü; Tippen überspringt das Warten.

## Motor & ESC

![Motor und Regler](img/motor.png)

Vier Kurven auf eigenen Skalen, das Gas auf dem Slider, Spitzenwerte unter den
Siebensegment-Anzeigen. Ohne ARM bewegt der Slider nichts; DISARM und STOP
halten beide sofort an, ohne Rampe. RESET PEAKS löscht die Spitzenwerte und
lässt die Live-Anzeigen unberührt.

Solange nichts Echtes angeschlossen ist, läuft der Bildschirm aus dem Modell —
SIMULATION liegt quer über dem Plot. Das Watermark verschwindet, sobald echte
Zahlen ankommen, und dafür braucht es keinen zusätzlichen Sensor: ein ESC mit
Telemetrie (KISS, BLHeli_32, OpenYGE) oder Bidirectional DShot liefert
Spannung, Strom, Verbrauch, Drehzahl und Temperaturen über die Signalleitung.

## Servo

![Servo](img/servo.png)

Irgendwo auf dem Bogen ziehen — das Horn folgt. Der kräftig gezeichnete Arm
ist die **gemessene** Stellung, der blasse dahinter die befohlene. Laufen die
beiden auseinander, ist dieser Abstand die Trägheit des Servos: der Bildschirm
fügt keine eigene hinzu, ein langsames Servo zeigt sich also als zwei Arme,
nicht als träges Bild. Die Ringe um die Hornspitze pulsieren, solange das
Servo aktiv angesteuert wird.

## Analyser

![Analyser](img/analyser.png)

Sechzehn Kanäle, jeder mit seinen letzten anderthalb Sekunden Verlauf und
einem Balken für den Momentanwert. Bewege ein Bedienelement am Sender: der
Kanal mit der Stufe in der Spur ist der, den du bewegt hast — für diese Frage
ist der Bildschirm gebaut. Die Balken beantworten die andere: wie weit, jetzt
gerade, ohne eine Zahl lesen zu müssen. CH17 und CH18 sind die digitalen
Kanäle.

Ein Glitch ist eine Spitze in einer Spur. Ein Dropout ist eine Kerbe durch
alle sechzehn im selben Moment.

Der Zustandsblock ist mit Absicht das Größte auf dem Bildschirm, denn der
Empfänger kann lügen:

![Ein Empfänger im Failsafe](img/analyser-failsafe.png)

Ein Empfänger im Failsafe sendet sechzehn tadellos geformte Werte, die er frei
erfindet. Der Prüfstand färbt jede Spur rot und schreibt FAILSAFE aus.
**Failsafe heißt Stopp — niemals sechzehn gültige Zahlen**, auch wenn sie
genau so aussehen. Vier Zustände, jeder mit einer erklärenden Zeile: SILENT,
FAILSAFE, FRAME LOST und LIVE.

## Programmierer

Programmieren geht in dieser Reihenfolge: was programmierst du, welches
Protokoll spricht es — und erst dann verbinden.

![Was programmierst du](img/programmer.png)

Mit der Klasse wählst du, welches Kabel du in der Hand hast; mit dem
Protokoll, was hineingesprochen wird. Jede Protokollzeile nennt ihren
Transport — eine automatische Erkennung gibt es nicht:

![Die Protokolle einer Klasse](img/programmer-protocols.png)

**BLHeli_32 fehlt in der ESC-Liste.** Seine Einstellungen sind verschlüsselt,
und der Schlüssel ist nicht öffentlich. Der Prüfstand erkennt diese Regler und
steuert sie an — Drehrichtung, 3D-Modus, Beacon und Save-Settings laufen als
DShot Special Commands —, aber er kann keinen einzigen Parameter benennen.
[Die ganze Antwort](BLHeli32-de.md).

Bevor ein Gerät geantwortet hat, lässt sich nichts editieren:

![Nichts hat geantwortet](img/programmer-idle.png)

Danach erscheinen die Parameter in Gruppen, und die Hilfe zur ausgewählten
Zeile steht unter der Liste:

![Verbunden](img/programmer-params.png)

Jede Firmware zeigt ihre Einstellungen in ihren eigenen Einheiten — BLHeli_S
führt das Timing in benannten Stufen, alles Spätere in Grad Vorzündung:

![Grad statt benannter Stufen](img/programmer-am32.png)

**Geändert ist nicht geschrieben.** Eine vorgemerkte Änderung bekommt eine
Markierung und eine eigene Farbe, und der WRITE-Knopf zählt mit, wie viele
anstehen. Ein editierter Wert sieht nie aus wie einer, der vom Gerät gelesen
wurde:

![Zwei vorgemerkte Änderungen](img/programmer-dirty.png)

Die Stepper halten an ihren Enden an, statt zum Anfang zurückzuspringen — bei
einem Regler wäre das falsche Ende einer Liste schnell eine Drehrichtung.

Eine Ebene zurück trennt die Verbindung: das Gerät, das eben einen Bootloader
beantwortet hat, ist nicht das, das gleich eine CLI beantworten wird, und die
Identität des einen bleibt nicht über den Parametern des anderen stehen.
Zurück geht eine Ebene auf einmal; den Bildschirm verlässt man über das Tag im
Band.

## Akku

![Zellenabweichung](img/battery.png)

Die Zellen werden als Abweichung von ihrem eigenen Mittelwert gezeichnet, denn
entscheidend ist die **Spreizung** — der größte Abstand zwischen zwei
beliebigen Zellen. Danach richtet sich das Urteil: HEALTHY unter 30 mV, WATCH
ab 30, REPLACE ab 60.

Spreizung heißt: zwischen den Zellen, nie gegen einen Nennwert. Ein leerer,
aber gleichmäßiger Akku ist entladen; ein voller, aber ungleichmäßiger ist
defekt — und nur um den zweiten kümmert sich dieser Bildschirm.

Die Skala folgt dem Akku, bis hinunter zu 12 mV, und steht neben dem Plot —
ein Balken, der den Plot füllt, könnte sonst vier Millivolt bedeuten oder
vierzig.

**Unter Last messen.** In Ruhe sieht eine müde Zelle aus wie jede andere.

## Logs

![Der Dateibrowser](img/logs.png)

Karte durchsehen, Datei öffnen, prüfen, was der Import erkannt hat und woran —
dann plotten:

![Die Importansicht](img/logs-import.png)
![Der Plot](img/logs-plot.png)

CSV wird in mehreren Dialekten gelesen, Dezimalkommas eingeschlossen, und die
Importansicht zeigt, was der Reader entschieden hat, bevor du dich darauf
verlässt.

## Setup

![Setup](img/setup.png)

Die Einstellungen liegen hinter der SETUP-Kachel, in beiden Themes:

![Setup im hellen Theme](img/setup-light.png)

## Auswuchten

Hat eine eigene Seite: [Auswuchten](Balance-de.md). Die Messung ist der
leichte Teil — ob die Antwort etwas taugt, entscheidet die Platzierung der
Sensoren.

## Ein Bildschirm, der noch nicht kann, sagt warum

Eine unfertige Kachel sagt nicht „demnächst". Sie nennt, was sie tun wird, und
das eine Bauteil oder die eine Entscheidung, die vorher fehlt. Das Menü ist
damit zugleich die Aufgabenliste — und es verspricht keine Messung, die der
Prüfstand nicht machen kann.
