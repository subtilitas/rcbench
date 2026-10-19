---
screen: none
title: Anschlüsse
order: 61
---

# Anschlüsse

ANSCHLÜSSE, die zweite Kategorie auf [SETUP](setup.md), verdrahtet die Teile, die der IOmcu liest: zwei Strommonitore und einen Winkelsensor an einem I2C-Bus (Inter-Integrated Circuit), den Phasenabgriff und das Netzteil PD mini. Ihre 26 Einstellungen stehen in vier Abschnitten unter den Überschriften I2C-BUS, PHASENABGRIFF, BALANCE-SENSOREN und NETZTEIL; eine Überschrift ist eine eigene Zeile ohne Tasten. Die Einstellungen werden wie jede andere Einstellung auf SETUP gehalten und gespeichert. Das Panel schreibt jede Einstellung 500 ms nach der letzten Änderung an den IOmcu, und nur, was von dem abweicht, was der IOmcu hält.

## Die Strommonitore

![Die Zeilen des INA228](view:setup-interfaces)

Der IOmcu liest einen TI INA228 im Strompfad des ESC und einen TI INA3221 an der Servoschiene.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| INA228 | EIN, AUS | AUS | der Monitor im Strompfad des ESC |
| INA228 Adresse | 0x40 bis 0x4F | 0x45 | das MATEK I2C-INA-BM ab Werk |
| INA228 Shunt | 50 bis 20000 uOhm | 200 | der Shunt, über dem er misst |
| INA228 Höchststrom | 1,0 bis 300,0 A, Schritte von 0,1 A | 204,8 | der Strom, für den der Messbereich gesetzt ist |
| INA3221 | EIN, AUS | AUS | der Dreikanal-Monitor der Servoschiene |
| INA3221 Adresse | 0x40 bis 0x43 | 0x40 | das DAOKAI-Modul mit A0 nach GND gebrückt |
| INA3221 Shunt | 5,0 bis 1000,0 mOhm, Schritte von 0,1 mOhm | 100,0 | je Kanal; 100 mOhm misst bis 1,64 A |
| INA3221 Kanäle | CH1, CH1+2+3 | CH1 | CH1 für den Servotest; CH2 und CH3 für ein Paar |

![Die Zeilen des INA3221 und die Pins des Busses](view:setup-sensors)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Sensor-SDA | -1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | der GPIO (General-Purpose Input/Output) des IOmcu für SDA; -1 setzt keinen Bus |
| Sensor-SCL | SDA + 1, oder -1 | 17 | folgt Sensor-SDA; die Zeile hat keine Tasten |

- Der Bus läuft mit 400 kHz; das ist keine Einstellung.
- Die beiden Adressen bleiben an den Enden ihres Bereichs stehen: `-` bei 0x40 und `+` bei 0x4F oder 0x43 ändern nichts.
- Pins, an die nichts angeschlossen ist, gelten als nicht antwortende Monitore, nicht als blockierter Bus.
- Der IOmcu übernimmt eine Änderung nur, solange der Prüfstand SAFE ist.
- Ist der INA228 eingeschaltet und antwortet er, verliert die Kachel MOTOR & ESC ihre Marke SIMULIERT, und [MOTOR & ESC](motor.md#tabelle) zeigt seine Spannung und seinen Strom.
- Ist der INA3221 mit CH1 eingeschaltet, liest [SERVO](servo.md#das-messgerät-der-servoschiene) den Strom der Servoschiene von ihm.
- Ein Messwert am oberen Ende des Bereichs wird als übersteuert gezeigt, nie als Wert: 163,8 mV über dem Shunt des INA3221.
- Ein Monitor, der nicht mehr antwortet, schaltet den Prüfstand nicht auf SAFE: Auf die Messwerte der Monitore löst nichts aus.

Das Band nennt, was schiefgeht, eine Meldung zur selben Zeit, höchstens eine alle 5 s. Zum Beispiel: `SDA oder SCL der Sensoren nicht festgelegt. SETUP > ANSCHLÜSSE prüfen.`, `INA228 und INA3221 nutzen beide 0x40. SETUP > ANSCHLÜSSE prüfen.`, `INA228 antwortet nicht unter 0x45.`, `INA3221 CH1 bei 1.64 A übersteuert. Anzeige zeigt nur den Grenzwert.`. Eine Meldung über die Einstellung -- Pins nicht festgelegt, eine Adresse, ein abgelehnter Wert -- verlässt das Band, sobald die Einstellung wieder stimmt und der IOmcu sie hält.

## Eine Zeile, die der IOmcu nicht hält

![Zeilen, die der IOmcu nicht übernommen hat](view:setup-unheld)

Eine Zeile, deren Wert nicht der ist, den der IOmcu hält, hat Rand und Wert in der Warnfarbe, und ihre Hilfezeile lautet `Vom IOmcu nicht übernommen.`. Die Markierung steht:

- nach einer Änderung, bis das Schreiben übernommen ist: 500 ms nach der letzten Änderung, für die Strommonitore erst, wenn der Prüfstand SAFE ist;
- bei einem Wert, den der IOmcu abgelehnt hat, bis er auf einen geändert wird, den er annimmt;
- bei einem eingeschalteten Teil, das der IOmcu aus hält: Sensor-SDA auf -1, beide Monitore auf einer Adresse oder ein IOmcu, der für das Teil zu alt ist.

Solange kein IOmcu antwortet, ist keine Zeile markiert. AS5600-Mitte ist ein eigener Wert des Panels und wird nie markiert.

## Der Winkelsensor

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| AS5600 | EIN, AUS | AUS | ein magnetischer Winkelsensor AS5600 an der Abtriebswelle des Servos, auf seiner festen Adresse 0x36 am Sensorbus |
| AS5600-Mitte | 0 bis 4095 | 0 | der Zählwert mit dem Servo in Neutral; 4096 Zählwerte je Umdrehung, 0,0879 deg je Zählwert |

Der Winkelsensor braucht einen IOmcu mit Protokoll 4.9 oder neuer. SENSOR-MITTE in den [Servo-Optionen](servo-settings.md#prüfling) setzt die Mitte aus dem aktuellen Messwert. Das Band meldet, wenn der Sensor keinen Magneten sieht, `AS5600 erkennt keinen Magneten. Magnet an der Servoabtriebswelle prüfen.`, oder wenn der Magnet zu schwach oder zu stark ist. Eine Magnetmeldung kommt einmal, solange der Magnetstatus des Sensors gleich bleibt: eine Änderung einer anderen Zeile wiederholt sie nicht.

## Der Phasenabgriff

![Die Zeilen des Phasenabgriffs](view:setup-tap)

Der Phasenabgriff hört die Pieptöne eines ESC an einer Motorphase. Ein GPIO des IOmcu liest die Phase über einen Vorwiderstand und eine Zener-Klemme. Der Abgriff ist ein Eingang: Er treibt nichts, und der IOmcu übernimmt eine Änderung bei ARMED wie bei SAFE.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Phasenabgriff | EIN, AUS | AUS | die Erfassung läuft auf dem IOmcu |
| Abgriff-Pin | 0 bis 47 | 22 | der GPIO des IOmcu; GP22 ist Pad 29 |
| Tiefster Ton | 50 bis 2000 Hz, Schritte von 10 Hz | 400 | ein tieferer Ton ist kein Piepton |
| Höchster Ton | 100 bis 6900 Hz, Schritte von 50 Hz | 6500 | über dem tiefsten Ton |
| Tonsprung | 0 bis 50 % | 8 | ein so großer Tonhöhensprung beginnt einen neuen Piepton; 0 trennt nur an Stille |
| Abgriff-Pause | 1 bis 100 ms | 3 | die Stille, die einen Piepton beendet |
| Min. Perioden | 1 bis 64 | 3 | die Tonperioden, die einen Piepton ergeben |

Der IOmcu lehnt einen ADC-Pin (Analog-Digital-Umsetzer), einen an einen Ausgang gebundenen Pin und einen Pin ab, den der Sensorbus oder der PD mini hält. Abgriff-Pause muss mindestens eine Periode von Tiefster Ton lang sein: 3 ms bei den voreingestellten 400 Hz, deren Periode 2,5 ms ist. Die Meldung eines abgelehnten Werts verlässt das Band, sobald der Wert auf einen zurückgestellt ist, den der IOmcu nimmt. Die Laufseite von [ESC STICK](motor-stick.md) zeigt die Pieptöne.

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

Der IOmcu lehnt einen Pin ab, der reserviert, an einen Ausgang gebunden, vom Sensorbus oder vom Phasenabgriff gehalten oder der andere Pin ist: RX auf GP17 wird abgelehnt, solange Sensor-SCL GP17 ist. Die Meldung verlässt das Band, sobald der Pin auf einen zurückgestellt ist, den der IOmcu nimmt. Eine Änderung an den Pins, der Baudrate oder dem Einschalter schaltet den Netzteilausgang ab.

## Verwandte Seiten

- [Setup](setup.md): Tasten, Speichern und die anderen Kategorien.
- [Ausgänge](outputs.md): welche Pins gebunden und damit nicht frei für die Teile hier sind.
