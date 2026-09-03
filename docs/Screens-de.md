# Bildschirme

<sub>[English](Screens.md) · **Deutsch**</sub>

Was auf jedem Bildschirm steht, was die Marken im Menü bedeuten und wie die
einzelnen Bildschirme bedient werden.

## Das Statusband

Das obere Band ist auf allen Bildschirmen gleich. Von rechts: STOP, die
Laufzeituhr (im scharfen Zustand oder nach einem Lauf), ARMED oder SAFE, ein
FAULT-Code, sobald einer gemeldet wird, der Ausgangsmodus (LINK oder SIM) und
LINK oder NO LINK.

STOP funktioniert auf jedem Bildschirm. Es entschärft und rastet ein: der
Prüfstand bleibt entschärft, bis er erneut scharf geschaltet wird. Ein
Bildschirmwechsel, ein ablaufender Hinweis oder ein zurückkehrender Link hebt
einen Stopp nicht auf.

ARM sitzt unten auf einem Prüfstandsbildschirm; STOP sitzt oben im Band.

## Marken im Menü

![Das Funktionsmenü](img/overview.png)

| Marke | Bedeutung |
| --- | --- |
| SOON | den Bildschirm gibt es nicht; die Kachel nennt, was er tun wird und worauf er wartet |
| MODELLED | den Bildschirm gibt es und er funktioniert, aber seine Hardware ist nicht bestückt; jeder Wert ist simuliert, und der Bildschirm sagt das |
| keine | die Hardware ist bestückt, die Messwerte sind gemessen |

Die Marke wird aus den Capability-Bits abgeleitet, die der Koprozessor beim
Hochfahren meldet. Ein Bildschirm ohne seine Hardware öffnet trotzdem und
arbeitet aus dem Modell.

Das Menü im hellen Theme:

![Das Menü im hellen Theme](img/overview-light.png)

## Splash

![Der Splash](img/splash.png)

Jedes Subsystem meldet beim Hochfahren sein Ergebnis: Platine, Display,
Touch, SD-Karte, Einstellungen, Link, Koprozessor. Eine fehlende Karte ist
eine Warnung. Ein Touch-Controller, der nicht antwortet, oder ein Koprozessor
mit einer anderen Major-Version des Protokolls ist ein Fehler, und der
Prüfstand schaltet nicht scharf. Wenn alle Schritte geantwortet haben,
übergibt der Splash nach 1,6 s an das Menü; ein Tippen überspringt das Warten.

## Motor & ESC

![Motor und ESC](img/motor.png)

Zwei Spalten. Der Plot und das Gas nehmen die linke, die vier Anzeigen und
die Bedienelemente eine Leiste auf der rechten, damit das Ablesen der Werte
und das Bedienen des Gases nicht um denselben Teil des Bildschirms
konkurrieren.

Der Streifen über beiden Spalten trägt die Abfragerate, die Fehlerzahl des
Links (CRC-Fehler und Resyncs zusammengezählt; CRC: Cyclic Redundancy Check)
und drei Temperaturen. ESC und MOT kommen von der Bench-Page. MCU ist der
eigene Die des Panels, gelesen vom Sensor des ESP32-S3: die Temperatur der
Displayplatine, nicht die des Koprozessors.

Das Gas bewegt sich um die Strecke, die ein Finger zurücklegt, nicht auf die
Stelle, an der er landet. Ein Druck auf den Track kommandiert nichts, sodass
eine Berührung am Ende nicht mit einem Kontakt den vollen Weg anfordern kann.
`-1` und `+1` an den Enden des Tracks schalten um einen Prozentpunkt.

ARM wird gehalten. Die Füllung blendet über zwei Sekunden von Grün ins
Gefahrenrot, und der Prüfstand schaltet scharf, wenn die Blende
durchgelaufen ist; früher loszulassen schaltet nichts scharf, und das
Loslassen selbst ebenfalls nicht. Das Scharfschalten lässt den ganzen Button
zweimal aufblitzen: weiss, schwarz, rot, und noch einmal, je ein
gezeichneter Frame. Ein scharfer
Prüfstand trägt das Gefahrenrot, und DISARM ist ein Druck und kein Halten:
Anhalten braucht nie ein Halten. DISARM und STOP
halten den Ausgang sofort an, ohne Rampe. RESET PEAKS löscht die
Spitzenwertmarken und lässt die Live-Anzeigen unverändert.

### EFF ist eine Guessimetrik

Der Header des Telemetrie-Panels zeigt die Nenn-kV, die Umdrehungen je Minute
und Volt, die der Motor tatsächlich dreht, und EFF: das Zweite geteilt durch
das Erste.

Die Rechnung dahinter ist tragfähig. Die Klemmenspannung teilt sich in den
ohmschen Abfall und die Gegen-EMK (elektromotorische Kraft) auf, weshalb rpm/V
unter Last die Nenn-kV skaliert mit dem Anteil der Spannung ist, der die
Gegen-EMK erreicht, und dieser Anteil ist der Anteil der Eingangsleistung, der
mechanisch wird. Im idealen Motor ist das Verhältnis exakt der
Umsetzungswirkungsgrad.

Die Zahl auf dem Bildschirm ist das nicht, aus drei Gründen, und sie heisst
deshalb EFF und nicht Wirkungsgrad:

- Sie erfasst nur die Kupferverluste (I hoch 2 mal R). Eisenverluste, Reibung
  und Luftwiderstand fallen auf die mechanische Seite der Aufteilung, also ist
  die Zahl eine obere Schranke des Wellenwirkungsgrads und nicht sein Wert.
- Der Prüfstand misst die Pack-Spannung, nicht die Klemmen des Motors, also
  stecken die Durchlass- und Schaltverluste des ESC in der Zahl. Sie
  beschreibt den Antriebsstrang; ein Vergleich mit einem Motordatenblatt
  vergleicht zwei verschiedene Dinge.
- Sie ist nur so gut wie die Nenn-kV. Ein Fehler dort geht direkt in den
  Prozentwert, und ein Wert über 100 % bedeutet, dass die Nennangabe falsch
  ist; der Bildschirm zeigt das, statt es zu verbergen, gedeckelt bei 199 %.

Die Nenn-kV kommt vom angeschlossenen ESC, sobald einer sie meldet. Noch
meldet sie keiner, also ist es in der Praxis die Einstellung `Rated kV`, deren
Standardwert null ist: eine geratene kV ergibt einen plausibel aussehenden,
aber falschen Prozentwert, und ohne Wert bleibt das Feld leer und es wird kein
Prozentwert gezeigt. Die gemessene rpm/V wird in jedem Fall gezeigt, weil sie
eine Messung und keine Herleitung ist.

Solange die Werte simuliert sind, steht SIMULATION quer über dem Bildschirm.
Das Watermark verschwindet, sobald gemessene Werte eintreffen: von einem ESC
(Electronic Speed Controller, Motorregler) mit Telemetrie (KISS, BLHeli_32,
OpenYGE), über Bidirectional DShot oder von den eigenen Sensoren des
Koprozessors.

## Servo

![Servo](img/servo.png)

An beliebiger Stelle auf dem Bogen ziehen, um eine Stellung zu befehlen. Der
kräftige Arm ist die gemessene Stellung, der blasse Arm die befohlene. Der
Abstand zwischen beiden ist die Verzögerung des Servos selbst. Die Ringe um
die Spitze pulsieren, solange das Servo angesteuert wird. Loslassen des Bogens
löscht den Ausgangs-Slot.

## Analyser

![Analyser](img/analyser.png)

Sechzehn Kanäle, jeder mit 1,5 s Verlauf und einem Balken für den aktuellen
Wert. CH17 und CH18 sind die beiden Digitalkanäle. Ein Glitch ist eine Spitze
in einer Spur; ein Dropout ist eine Kerbe durch alle sechzehn im selben
Moment.

Der Zustandsblock zeigt einen von SILENT, FAILSAFE, FRAME LOST und LIVE, mit
einer Zeile Erklärung:

![Ein Empfänger im Failsafe](img/analyser-failsafe.png)

Im FAILSAFE sendet der Empfänger wohlgeformte Werte, die er selbst erzeugt;
jede Spur wird rot gezeichnet. FAILSAFE als Stopp behandeln, nicht als
sechzehn gültige Kanäle. [Empfängerbusse](Receivers-de.md) beschreibt die
Zustände.

## Programmierer

Die Reihenfolge: Geräteklasse, Protokoll, verbinden.

![Geräteklasse](img/programmer.png)

Jede Protokollzeile nennt ihren Transport. Eine automatische Erkennung gibt
es nicht:

![Die Protokolle einer Klasse](img/programmer-protocols.png)

BLHeli_32 steht nicht in der ESC-Liste. Der Prüfstand erkennt diese ESCs,
steuert sie an und sendet die DShot Special Commands, kann ihre Parameter aber
nicht lesen: [BLHeli_32-Parameter](BLHeli32-de.md).

Bevor ein Gerät geantwortet hat, ist nichts editierbar:

![Nichts hat geantwortet](img/programmer-idle.png)

Nachdem ein Gerät geantwortet hat, erscheinen die Parameter in Gruppen, mit
der Hilfe zur ausgewählten Zeile unter der Liste:

![Verbunden](img/programmer-params.png)

Jede Firmware zeigt ihre Einstellungen in ihren eigenen Einheiten. BLHeli_S
zeigt das Timing als benannte Stufen, die anderen in Grad Vorzündung:

![Grad statt benannter Stufen](img/programmer-am32.png)

Ein geänderter Wert wird erst geschrieben, wenn WRITE gedrückt wird.
Vorgemerkte Änderungen tragen eine Markierung und eine eigene Farbe, und der
WRITE-Knopf zeigt, wie viele vorgemerkt sind:

![Zwei vorgemerkte Änderungen](img/programmer-dirty.png)

Stepper halten an den Enden einer Liste an; sie springen nicht auf die andere
Seite.

Eine Ebene zurück trennt die Verbindung. Zurück geht eine Ebene auf einmal;
das Home-Tag im Band verlässt den Bildschirm.

## Akku

![Zellenabweichung](img/battery.png)

Die Zellen werden als Abweichung vom Mittelwert des Packs gezeichnet. Das
Urteil folgt der Spreizung, dem größten Abstand zwischen zwei beliebigen
Zellen: HEALTHY unter 30 mV, WATCH ab 30 mV, REPLACE ab 60 mV. Die Skala folgt
dem Pack bis hinunter zu einer Untergrenze von 12 mV und steht neben dem Plot.

Unter Last messen. In Ruhe liest sich eine schwache Zelle wie die anderen.

## Logs

![Der Dateibrowser](img/logs.png)

Karte durchsehen, Datei öffnen, prüfen, was der Import erkannt hat, dann
plotten:

![Die Importansicht](img/logs-import.png)
![Der Plot](img/logs-plot.png)

Der Reader für CSV (Comma-Separated Values) akzeptiert Dezimalkomma und
Dezimalpunkt, eine Einheitenzeile und Zeilen ungleicher Länge; die
Importansicht zeigt, was er entschieden hat, bevor die Datei geplottet wird.
Vom Prüfstand aufgezeichnete Läufe werden als `BENCHnnn.CSV` im
Wurzelverzeichnis der Karte abgelegt.

## Setup

![Setup](img/setup.png)

Die Einstellungen liegen hinter der SETUP-Kachel, in beiden Themes:

![Setup im hellen Theme](img/setup-light.png)

## Auswuchten

Auf einer eigenen Seite beschrieben: [Auswuchten](Balance-de.md).

## Bildschirme, die nicht fertig sind

Eine Kachel mit der Marke SOON nennt, was der Bildschirm tun wird und auf
welches Bauteil oder welche Entscheidung er wartet.
