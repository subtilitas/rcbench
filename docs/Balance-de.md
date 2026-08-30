# Auswuchten

<sub>[English](Balance.md) · **Deutsch**</sub>

Was der Auswucht-Bildschirm wissen will, wohin die Sensoren gehören — und
warum Auswuchten an der Platzierung scheitert, nicht an der Messung.

Das Ergebnis einer Auswuchtung ist ein Betrag und ein Winkel: *so viel Masse,
dorthin.* Beides stammt aus zwei Sensoren, und beide lassen sich so anbringen,
dass die Antwort wertlos ist, ohne dass man es dem Bildschirm ansieht. Ein
Vibrationssensor auf weicher Unterlage misst eine gefilterte Fassung dessen,
was am Lager passiert; eine Indexmarke auf einem Blatt liefert einen Impuls
pro Blatt — und damit einen Winkel, der um einen ganzen Blattabstand daneben
liegt. Diese Seite zeigt, wie man beides vermeidet.

## Den Bildschirm einrichten

![Der Rotor und wohin die Masse kommt](img/balance.png)

**Blattzahl** (2–6) und Rotortyp einstellen. Der Bildschirm übersetzt den
gemessenen Winkel in eine Anweisung: „0,35 g bei 265°" ist eine Zahl —
„zwischen Blatt zwei und drei" kann man umsetzen.

Ein **Impeller** ist ein eigener Fall, und der Bildschirm behandelt ihn auch
so: an ein Blattende im Kanal kommt niemand heran, also wird die Korrektur als
Winkel auf der Nabe angegeben statt als Blatt, das beklebt wird:

![Ein fünfblättriger Impeller](img/balance-edf.png)

## Am Prüfstand

![Wohin die Sensoren am Prüfstand kommen](img/balance-rig.png)

Die zwei Fehler, die am häufigsten passieren:

**Die Indexmarke gehört auf die Motorglocke, nicht auf den Spinner.** Ein
Prüfstand läuft oft ganz ohne Spinner — und ein Sensor, der auf die
Spinnernase zielt, muss von vorn durch die Propellerebene schauen und zählt am
Ende Blätter statt Umdrehungen. Die Glocke dreht mit der Welle, ist starr, ist
immer da, und lässt sich von unten beobachten, wo nichts im Weg ist.

**Die Marke ist ein Filzstiftstrich, kein Reflexband.** Alles, was auf die
Glocke geklebt wird, ist zusätzliche Masse — ausgerechnet an dem Teil, dessen
Unwucht gemessen werden soll. Mit Klebeband wuchtet man am Ende die eigene
Markierung aus.

Und die Grundregel hinter jedem Messwert: der Vibrationssensor gehört auf
etwas **Starres**, so nah am Lager wie möglich. Eine weiche Befestigung ist
ein Filter, den niemand bestellt hat.

## Am fertigen Modell

![Wohin die Sensoren am Modell kommen](img/balance-aircraft.png)

Hier hat man die Platzierung kaum in der Hand, also zählen die Regeln umso
mehr:

- **Die Motorhaube ist nicht der Motorspant.** Sie ist eine Verkleidung — an
  einen Spant geschraubt, oft gummigelagert, und gegenüber dem, dessen
  Vibration gemessen werden soll, frei beweglich. Der Beschleunigungssensor
  gehört flach auf den **Motorspant**, die eine starre Fläche an diesem Ende
  des Rumpfs.
- Ein flach montierter Dreiachssensor hat zwei Achsen in der Spantebene, quer
  zur Welle. **Eine davon verwenden, die dritte ignorieren.**
- **Die Marke gehört auch hier auf die Glocke** — am Modell aus einem zweiten
  Grund: ein Spinner wird abgenommen. Nach jedem Transport sitzt er in einem
  anderen Winkel, und die Phasenreferenz der letzten Auswuchtung ist dahin.
  Die Glocke ist Teil des Rotors und sitzt immer gleich. Bei den meisten
  Elektromodellen steht der Außenläufer ohnehin frei vor der Haube — Sensor
  darunter, Blick senkrecht nach oben, und der Strahl kreuzt nichts.
- **Das Flugzeug festzurren.** Eine Maschine, die frei wippen kann, ist ein
  ungewolltes Federelement in Reihe mit allem, was gemessen werden soll.

## Welcher Sensor: eine Frage der Verzögerung

Ob überhaupt ein Indexsensor nötig ist — und welcher Vibrationssensor
funktioniert — hängt an zwei Tatsachen:

**Ohne Indeximpuls** gibt es keine Phasenreferenz. Gewuchtet wird dann nach
der Vier-Lauf-Methode: ein Grundlauf, danach dieselbe Probemasse bei 0°, 120°
und 240°. Sie misst nie eine Phase, eine konstante Sensorverzögerung schadet
ihr also nicht. Was sie braucht, ist **Bandbreite**: bei 10 000 min⁻¹ liegt
die Grundfrequenz bei 167 Hz — eine fertige IMU, die fusionierte Lagedaten mit
100 Hz liefert, sieht davon schlicht nichts. Sie ist nicht zu langsam, sie ist
blind. Richtig ist ein analoges Bauteil — ein Piezo oder ein analoger
Beschleunigungssensor — direkt an den A/D-Wandler des Koprozessors.

**Mit Indeximpuls** halbiert sich die Zahl der Läufe, und jetzt zählt die
Verzögerung — aber nur ihre *Schwankung*. Ein konstanter Anteil kürzt sich
heraus, weil der Probelauf die ganze Kette mitsamt Sensor mitmisst. Ein
analoges Bauteil liegt bei etwa fünfzehn Mikrosekunden — weniger als ein Grad
bei 10 000 min⁻¹. Eine fusionierte IMU liegt bei fünf Millisekunden, weder
konstant noch dokumentiert — das sind dreihundert Grad, also eine Masse, die
mit großer Sorgfalt an der falschen Stelle angebracht wird.

## Was zum Messen noch fehlt

Ein Beschleunigungssensor und optional ein Indexsensor am Koprozessor. Beides
ist noch nicht bestückt — der Bildschirm arbeitet deshalb aus dem Modell und
trägt die MODELLED-Marke. Einrichtung, Blattumrechnung und die beiden
Platzierungsanleitungen oben funktionieren schon heute.
