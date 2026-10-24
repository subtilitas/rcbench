---
screen: none
title: Anschlüsse
order: 61
---

# Anschlüsse

ANSCHLÜSSE, die zweite Kategorie auf [SETUP](setup.md), verdrahtet die Teile, die der IOmcu liest: den Strommonitor INA228 und einen Winkelsensor an einem I2C-Bus (Inter-Integrated Circuit), den Phasenabgriff, das Netzteil PD mini und wie der Programmierport ein Hitec-Servo der D-Serie empfängt. Ihre 23 Einstellungen stehen in fünf Abschnitten unter den Überschriften I2C-BUS, PHASENABGRIFF, BALANCE-SENSOREN, NETZTEIL und PROG-PORT; eine Überschrift ist eine eigene Zeile ohne Tasten. Die Einstellungen werden wie jede andere Einstellung auf SETUP gehalten und gespeichert. Das Panel schreibt jede Einstellung 500ms nach der letzten Änderung an den IOmcu, und nur, was von dem abweicht, was der IOmcu hält.

## Die Strommonitore

![Die Zeilen des INA228](view:setup-interfaces)

Der IOmcu liest einen TI INA228 im Strompfad des ESC, hier eingestellt, und bis zu vier TI INA3221 am selben Bus, unter [Sensoren](setup-sensors.md) eingestellt.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| INA228 | EIN, AUS | AUS | der Monitor im Strompfad des ESC |
| INA228 Adresse | 0x40 bis 0x4F | 0x45 | das MATEK I2C-INA-BM ab Werk |
| INA228 Shunt | 50 bis 20000uΩ | 200 | der Shunt, über dem er misst |
| INA228 Höchststrom | 1,0 bis 300,0A, Schritte von 0,1A | 204,8 | der Strom, für den der Messbereich gesetzt ist |
| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Sensor-SDA | -1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | der GPIO (General-Purpose Input/Output) des IOmcu für SDA; -1 setzt keinen Bus |
| Sensor-SCL | SDA + 1, oder -1 | 17 | folgt Sensor-SDA; die Zeile hat keine Tasten |

- Der Bus läuft mit 400kHz; das ist keine Einstellung.
- Das Tastenfeld lehnt einen Pin für Sensor-SDA ab, den der Bus nicht haben kann, und listet statt des Bereichs die Pins, die er nimmt: GPIO 5 getippt zeigt `Pins, die der Bus nimmt: 0 4 6 14 16 18 20 26`.
- Die Adresse bleibt an den Enden ihres Bereichs stehen: `-` bei 0x40 und `+` bei 0x4F ändern nichts.
- Pins, an die nichts angeschlossen ist, gelten als nicht antwortende Monitore, nicht als blockierter Bus.
- Der IOmcu übernimmt eine Änderung nur, solange der Prüfstand SAFE ist.
- Ist der INA228 eingeschaltet und antwortet er, verliert die Kachel MOTOR & ESC ihre Marke SIMULIERT, und [MOTOR & ESC](motor.md#tabelle) zeigt seine Spannung und seinen Strom.
- Misst ein Kanal eines INA3221 unter [Sensoren](setup-sensors.md) das Ziel des Servotesters, liest [SERVO](servo.md#das-messgerät-der-servoschiene) den Strom der Servoschiene von ihm.
- Ein Messwert am oberen Ende des Bereichs wird als übersteuert gezeigt, nie als Wert: 163,8mV über dem Shunt des INA3221.
- Ein Monitor, der nicht mehr antwortet, schaltet den Prüfstand nicht auf SAFE: Auf die Messwerte der Monitore löst nichts aus.

![GPIO 5 für Sensor-SDA abgelehnt](view:setup-sda-why)

Ein Pin steht mit dem Wort voran, `GPIO 1`, im Eintrag des Tastenfelds, in der Zeile und in der Frage beim Verlassen von SETUP ohne Speichern. Der Kopf des Tastenfelds nennt den Bereich ebenso, `GPIO -1 bis 47`.

![Das Tastenfeld von Sensor-SDA, GPIO 1 getippt](view:setup-sda-keypad)

Das Band nennt, was schiefgeht, eine Meldung zur selben Zeit, höchstens eine alle 5s. Zum Beispiel: `SDA oder SCL der Sensoren nicht festgelegt. SETUP > ANSCHLÜSSE prüfen.`, `INA228 und INA3221 nutzen beide 0x40. SETUP > ANSCHLÜSSE prüfen.`, `INA228 antwortet nicht unter 0x45.`, `INA3221 0x40 CH1 bei 1.64A übersteuert. Anzeige zeigt nur den Grenzwert.`. Eine Meldung über die Einstellung -- Pins nicht festgelegt, eine Adresse, ein abgelehnter Wert -- verlässt das Band, sobald die Einstellung wieder stimmt und der IOmcu sie hält.

## Eine Zeile, die der IOmcu nicht hält

![Zeilen, die der IOmcu nicht übernommen hat](view:setup-unheld)

Eine Zeile, deren Wert nicht der ist, den der IOmcu hält, hat Rand und Wert in der Warnfarbe, und ihre Hilfezeile lautet `Vom IOmcu nicht übernommen.`. Die Markierung steht:

- nach einer Änderung, bis das Schreiben übernommen ist: 500ms nach der letzten Änderung, für die Strommonitore erst, wenn der Prüfstand SAFE ist;
- bei einem Wert, den der IOmcu abgelehnt hat, bis er auf einen geändert wird, den er annimmt;
- bei einem eingeschalteten Teil, das der IOmcu aus hält: Sensor-SDA auf -1, der INA228 auf der Adresse eines bestückten INA3221 oder ein IOmcu, der für das Teil zu alt ist.

Solange kein IOmcu antwortet, ist keine Zeile markiert. AS5600-Mitte ist ein eigener Wert des Panels und wird nie markiert.

## Der Winkelsensor

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| AS5600 | EIN, AUS | AUS | ein magnetischer Winkelsensor AS5600 an der Abtriebswelle des Servos, auf seiner festen Adresse 0x36 am Sensorbus |
| AS5600-Mitte | 0 bis 4095 | 0 | der Zählwert mit dem Servo in Neutral; 4096 Zählwerte je Umdrehung, 0,0879deg je Zählwert |

Der Winkelsensor braucht einen IOmcu mit 0.15.0 oder neuer. SENSOR-MITTE in den [Servo-Optionen](servo-settings.md#prüfling) setzt die Mitte aus dem aktuellen Messwert. Das Band meldet, wenn der Sensor keinen Magneten sieht, `AS5600 erkennt keinen Magneten. Magnet an der Servoabtriebswelle prüfen.`, oder wenn der Magnet zu schwach oder zu stark ist. Eine Magnetmeldung kommt einmal, solange der Magnetstatus des Sensors gleich bleibt: eine Änderung einer anderen Zeile wiederholt sie nicht. Nach dem Aus- und Wiedereinschalten des Sensors kommt sie wieder.

## Der Phasenabgriff

![Die Zeilen des Phasenabgriffs](view:setup-tap)

Der Phasenabgriff hört die Pieptöne eines ESC an einer Motorphase. Ein GPIO des IOmcu liest die Phase über einen Vorwiderstand und eine Zener-Klemme. Der Abgriff ist ein Eingang: Er treibt nichts, und der IOmcu übernimmt eine Änderung bei ARMED wie bei SAFE.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Phasenabgriff | EIN, AUS | AUS | die Erfassung läuft auf dem IOmcu |
| Abgriff-Pin | 0 bis 47 | 22 | der GPIO des IOmcu; GP22 ist Pad 29 |
| Tiefster Ton | 50 bis 2000Hz, Schritte von 10Hz | 400 | ein tieferer Ton ist kein Piepton |
| Höchster Ton | 100 bis 6900Hz, Schritte von 50Hz | 6500 | über dem tiefsten Ton |
| Tonsprung | 0 bis 50% | 8 | ein so großer Tonhöhensprung beginnt einen neuen Piepton; 0 trennt nur an Stille |
| Abgriff-Pause | 1 bis 100ms | 3 | die Stille, die einen Piepton beendet |
| Min. Perioden | 1 bis 64 | 3 | die Tonperioden, die einen Piepton ergeben |

Der IOmcu lehnt einen ADC-Pin (Analog-Digital-Umsetzer), einen an einen Ausgang gebundenen Pin und einen Pin ab, den der Sensorbus oder der PD mini hält. Abgriff-Pause muss mindestens eine Periode von Tiefster Ton lang sein: 3ms bei den voreingestellten 400Hz, deren Periode 2,5ms ist. Die Meldung eines abgelehnten Werts verlässt das Band, sobald der Wert auf einen zurückgestellt ist, den der IOmcu nimmt. Die Laufseite von [ESC STICK](motor-stick.md) zeigt die Pieptöne.

## Vibration und der optische Tacho

| Einstellung | Bereich | Vorgabe |
|---|---|---|
| Vibration | EIN, AUS | AUS |
| Optischer Tacho | EIN, AUS | AUS |
| Tacho-Pin | -1 bis 48 | -1 |

Diese drei werden gespeichert. Kein Bildschirm und kein IOmcu liest sie in diesem Release.

## Der PD mini

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| PD mini | EIN, AUS | AUS | das programmierbare Netzteil auf [SUPPLY](supply.md) und SERVO; AUS rechnet das Netzteilmodell des Panels |
| PD mini TX | -1 bis 47 | -1 | der GPIO des IOmcu zum DM des Moduls |
| PD mini RX | -1 bis 47 | -1 | der GPIO des IOmcu vom DP des Moduls |
| PD mini Baud | 9600, 19200, 38400, 57600, 115200, 230400, 460800, AUTO | AUTO | die UART-Rate (Universal Asynchronous Receiver-Transmitter) des Moduls; AUTO probiert jede der 7 Raten, eine pro Sekunde |

Die Tasten `-` und `+` von PD mini TX und PD mini RX springen über den GPIO, den die andere Zeile hält: mit RX auf GP18 geht TX von GP19 auf GP17. Am Ende des Bereichs tut die Taste dann nichts: mit RX auf GP47 bleibt TX auf GP46. Das Tastenfeld lehnt den GPIO der anderen Zeile ab und zeigt statt des Bereichs `Diesen GPIO hält der andere PD-mini-Pin`. Eine gespeicherte Einstellung mit beiden Zeilen auf einem GPIO wird so geladen; der PD mini bleibt aus, die SUPPLY-Kopfzeile sagt `PD MINI OHNE PINS`, und beide Zeilen sagen `TX und RX auf einem Pin. Der PD mini bleibt aus.`, bis eine von ihnen verstellt wird. Der Schalter PD mini schaltet das Modul ab. Der IOmcu lehnt einen Pin ab, der reserviert, an einen Ausgang gebunden, vom Sensorbus oder vom Phasenabgriff gehalten oder der andere Pin ist: RX auf GP17 wird abgelehnt, solange Sensor-SCL GP17 ist. Die Meldung verlässt das Band, sobald der Pin auf einen zurückgestellt ist, den der IOmcu nimmt. Eine Änderung an den Pins, der Baudrate oder dem Einschalter schaltet den Netzteilausgang ab.

![GPIO 9 von RX für PD mini TX abgelehnt](view:setup-pdmini-why)

## Der Programmierport

| Einstellung | Bereich | Voreinstellung | Was sie tut |
|---|---|---|---|
| Hitec-D-Empfang | PULL-UP, PRECHARGE | PULL-UP | wie der IOmcu die Antwort eines Hitec-Servos der D-Serie am Programmierport empfängt. PULL-UP: ein Widerstand von 2,2kΩ von der Signalleitung zur Servoversorgung. PRECHARGE: kein Widerstand; der IOmcu treibt das Pad alle 1,09us für 100ns auf High und tastet es 113ns später ab |

Der IOmcu nimmt den Wert beim nächsten OPEN einer Leitung der D-Serie. classic-Servos (HS-5xxx, HS-7xxx) brauchen keins davon und übergehen ihn. Ein IOmcu mit 0.21.1 oder 0.21.2 (Protokoll 4.23) empfängt mit PULL-UP; mit PRECHARGE lautet die Zeile dann `IOmcu älter als 0.22.0: empfängt mit PULL-UP.` Ein IOmcu älter als 0.21.1 hat keine Hitec-Leitung; mit PRECHARGE lautet die Zeile `IOmcu älter als 0.21.1: keine Hitec-Leitung.`

## Verwandte Seiten

- [Setup](setup.md): Tasten, Speichern und die anderen Kategorien.
- [Ausgänge](outputs.md): welche Pins gebunden und damit nicht frei für die Teile hier sind.
