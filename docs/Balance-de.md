# Auswuchten

<sub>[English](Balance.md) · **Deutsch**</sub>

Den Auswucht-Bildschirm einrichten, wohin die beiden Sensoren gehören und
welche Sensoren funktionieren.

Ein Auswuchtergebnis ist ein Betrag und ein Winkel. Beides stammt aus zwei
Sensoren: einem Vibrationssensor und optional einem Indexsensor mit einem
Impuls pro Umdrehung. Zwei Platzierungsfehler erzeugen ein falsches Ergebnis
ohne sichtbares Zeichen auf dem Bildschirm: ein Vibrationssensor auf einer
nachgiebigen Befestigung misst eine gefilterte Fassung der Lagerbewegung, und
eine Indexmarke auf einem Blatt liefert einen Impuls pro Blatt statt einen pro
Umdrehung.

## Den Bildschirm einrichten

![Der Rotor und wohin die Masse kommt](img/balance.png)

Blattzahl (2 bis 6) und Rotortyp einstellen. Der Bildschirm rechnet den
gemessenen Winkel zusätzlich in eine Blattangabe um („zwischen Blatt zwei und
drei").

Bei einem Impeller wird die Korrektur als Winkel auf der Nabe angegeben, weil
eine Blattspitze im Kanal nicht erreichbar ist:

![Ein fünfblättriger Impeller](img/balance-edf.png)

## Sensorplatzierung am Prüfstand

![Wohin die Sensoren am Prüfstand kommen](img/balance-rig.png)

- Die Indexmarke gehört auf die Motorglocke, nicht auf einen Spinner. Die
  Glocke dreht mit der Welle, ist starr, ist bei jedem Propeller vorhanden und
  lässt sich von unten beobachten, wo nichts die Sichtlinie des Sensors
  kreuzt. Ein Sensor, der von vorn auf einen Spinner zielt, blickt durch die
  Propellerebene und zählt Blätter.
- Die Marke ist ein Stiftstrich, kein Reflexband. Alles, was auf die Glocke
  geklebt wird, ist Masse auf dem Teil, das ausgewuchtet wird.
- Der Vibrationssensor gehört auf ein starres Teil, so nah am Lager wie
  möglich. Eine nachgiebige Befestigung ist ein Tiefpassfilter.

## Sensorplatzierung am fertigen Modell

![Wohin die Sensoren am Modell kommen](img/balance-aircraft.png)

- Der Beschleunigungssensor gehört flach auf den Motorspant, nicht auf die
  Motorhaube. Die Haube ist eine Verkleidung, oft gummigelagert, und bewegt
  sich gegenüber dem Motorträger.
- Ein flach montierter Dreiachssensor hat zwei Achsen in der Spantebene, quer
  zur Welle. Eine davon verwenden, die dritte ignorieren.
- Die Indexmarke gehört auf die Glocke. Ein Spinner wird für den Transport
  abgenommen und in einem anderen Winkel wieder aufgesetzt, was die
  Phasenreferenz der vorherigen Auswuchtung ungültig macht.
- Das Flugzeug festzurren. Eine frei stehende Zelle ist eine Feder in Reihe
  mit der Messung.

## Sensorauswahl

Ohne Indeximpuls gibt es keine Phasenreferenz, und gewuchtet wird nach der
Vier-Lauf-Methode: ein Grundlauf, danach eine Probemasse bei 0°, 120° und
240°. Die Methode misst keine Phase, eine konstante Sensorverzögerung wirkt
sich also nicht aus. Sie braucht Bandbreite: bei 10 000 rpm (revolutions per
minute, Umdrehungen pro Minute) liegt die Grundfrequenz bei 167 Hz, die eine
IMU (Inertial Measurement Unit, Trägheitssensor), die fusionierte Lagedaten
mit 100 Hz liefert, nicht auflösen kann. Einen analogen Sensor (Piezo oder
analoger Beschleunigungssensor) an den ADC (Analog-Digital-Wandler) des
Koprozessors anschließen.

Mit Indeximpuls halbiert sich die Zahl der Läufe, und nur die Schwankung der
Sensorverzögerung zählt; ein konstanter Anteil kürzt sich heraus, weil der
Probelauf die ganze Kette mitmisst. Ein analoger Sensor hat etwa 15 µs
Verzögerung, unter 1° bei 10 000 rpm. Eine fusionierte IMU hat etwa 5 ms
Verzögerung, weder konstant noch spezifiziert, das sind 300° bei 10 000 rpm.

## Was die Messung braucht

Einen Beschleunigungssensor und optional einen Indexsensor am Koprozessor.
Keiner von beiden ist bestückt; der Bildschirm arbeitet deshalb aus dem Modell
und trägt die Marke MODELLED. Einrichtung, Blattumrechnung und beide
Platzierungsanleitungen funktionieren ohne die Sensoren.
