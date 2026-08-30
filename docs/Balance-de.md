# Auswuchten

<sub>[English](Balance.md) · **Deutsch**</sub>

Was der Auswucht-Bildschirm von dir wissen will, wohin die Sensoren müssen,
und warum die Platzierung — nicht die Messung — die Stelle ist, an der
Auswuchten schiefgeht.

Eine Auswuchtantwort ist ein Betrag und ein Winkel: *so viel Masse, dorthin.*
Beides kommt aus zwei Sensoren, und keine der beiden Arten, sie schlecht zu
platzieren, sieht auf dem Bildschirm falsch aus. Ein Vibrationssensor auf einer
nachgiebigen Aufnahme liest eine gefilterte Fassung dessen, was das Lager getan
hat; eine Indexmarke auf einem Blatt liefert einen Impuls je Blatt und einen
Winkel, der um einen ganzen Blattabstand danebenliegt. Diese Seite ist, wie man
beides vermeidet.

## Den Bildschirm einrichten

![Der Rotor und wohin die Masse kommt](img/balance.png)

Stelle die **Blattzahl** (2–6) und den Rotortyp ein. Der Bildschirm macht aus
dem gemessenen Winkel eine Anweisung: „0,35 g bei 265°" ist eine Zahl,
„zwischen Blatt zwei und drei" ist etwas, wonach man handeln kann.

Ein **Impeller** ist eine andere Aufgabe, und der Bildschirm sagt das. An ein
Blattende im Kanal kommt man nicht heran, die Korrektur wird also als Winkel an
der Nabe angegeben statt als Blatt zum Bekleben:

![Ein fünfblättriger Impeller](img/balance-edf.png)

## Am Prüfstand

![Wohin die Sensoren am Prüfstand kommen](img/balance-rig.png)

Die zwei Details, die falsch gemacht werden:

**Die Indexmarke kommt auf die Glocke des Motors, nicht auf einen Spinner.**
Ein Prüfstand läuft oft ohne Spinner, und ein auf die Spinnernase gerichteter
Strahl muss von vorn kommen, quer über die Kreisfläche — dieselbe
Blickrichtung, die am Ende Blätter zählt statt Umdrehungen. Die Glocke dreht
mit der Welle, ist starr, ist da, welcher Propeller auch montiert ist, und
lässt sich von unten beobachten, wo nichts im Weg ist.

**Die Marke ist ein Filzstiftstrich, kein Reflexband.** Alles, was auf die
Glocke geklebt wird, ist Masse — an genau dem Teil der Maschine, dessen Masse
gemessen werden soll. Klebeband heißt, den eigenen Marker auszuwuchten.

Und die Montageregel hinter jedem Messwert: der Vibrationssensor kommt auf
etwas **Starres**, so nah am Lager wie möglich. Eine nachgiebige Aufnahme ist
ein Filter, den niemand gewählt hat.

## Am fertigen Modell

![Wohin die Sensoren am Modell kommen](img/balance-aircraft.png)

Hier liegt fast nichts an der Platzierung in deiner Hand, die Regeln zählen
also umso mehr:

- **Die Motorhaube ist nicht der Spant.** Sie ist eine Verkleidung, an einen
  Spant geschraubt und oft in Gummi, frei beweglich gegenüber dem, dessen
  Vibration du haben willst. Der Beschleunigungssensor kommt flach gegen den
  **Motorspant** — die eine starre Fläche an diesem Ende des Modells.
- Ein flach montiertes Dreiachsteil legt zwei seiner Achsen in die Ebene des
  Spants, quer zur Welle. **Eine davon nehmen und die dritte ignorieren.**
- **Die Marke kommt weiterhin auf die Glocke**, und am Modell gibt es dafür
  einen zweiten Grund: ein Spinner geht ab. Jedes Mal, wenn er zum Transport
  abgenommen wird, kommt er in einem neuen Winkel wieder dran, und die
  Phasenreferenz der letzten Auswuchtung geht mit ihm. Die Glocke ist Teil des
  Rotors und bewegt sich nie. An den meisten Elektromodellen steht der
  Außenläufer ohnehin vor der Haube — Sensor darunter, Blick gerade nach oben,
  nichts kreuzend.
- **Das Flugzeug festzurren.** Eine Maschine, die frei schaukeln kann, ist
  eine Feder, die niemand gewählt hat, in Reihe mit allem, was du messen
  willst.

## Sensorwahl: die Frage nach der Verzögerung

Ob du überhaupt einen Indexsensor brauchst — und welcher Vibrationssensor
funktioniert — hängt an zwei Tatsachen:

**Ohne Indeximpuls** gibt es keine Phasenreferenz, ausgewuchtet wird mit der
Vier-Lauf-Methode: ein Grundlauf, dann eine Probemasse bei 0°, 120° und 240°.
Sie misst nie eine Phase, eine konstante Sensorverzögerung kann ihr also nichts
anhaben. Was sie braucht, ist **Bandbreite**: bei 10 000 min⁻¹ liegt die
Grundfrequenz bei 167 Hz, und eine fertige IMU, die fusionierte Lage mit 100 Hz
streamt, sieht davon gar nichts. Sie ist nicht zu spät — sie ist blind. Ein
analoges Bauteil (ein Piezo, ein analoger Beschleunigungssensor) in den eigenen
Wandler des Koprozessors ist das richtige Teil.

**Mit Indeximpuls** halbieren sich die Läufe, und dann zählt die Verzögerung —
aber nur ihre *Schwankung*. Eine konstante Verzögerung kürzt sich heraus, denn
der Probelauf misst die ganze Kette einschließlich des Sensors. Ein analoges
Bauteil sind etwa fünfzehn Mikrosekunden, unter einem Grad bei 10 000 min⁻¹.
Eine fusionierte IMU sind fünf Millisekunden, weder konstant noch
veröffentlicht — dreihundert Grad, und eine mit großer Präzision an der
falschen Stelle angebrachte Masse.

## Was der Bildschirm zum Messen braucht

Einen Beschleunigungssensor und (optional) einen Indexsensor am Koprozessor.
Beides ist noch nicht bestückt, der Bildschirm arbeitet also derzeit aus dem
Modell und trägt die MODELLED-Kennzeichnung — die Einrichtung, die
Blattarithmetik und die beiden Platzierungsanleitungen oben funktionieren
heute.
