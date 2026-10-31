---
screen: none
title: Sensoren
order: 62
---

# Sensoren

SENSORS, die vierte Kategorie auf [SETUP](setup.md), stellt jedes Gerät ein, das der IOmcu liest: den Sensorbus, den INA228, bis zu vier TI INA3221, Dreikanal-Strommonitore auf 0x40 bis 0x43, den Winkelsensor AS5600 und den Phasenabgriff. Die Einstellungen werden wie jede andere Einstellung auf SETUP gehalten und gespeichert.

## Die linke Liste

![Zwei Bausteine bestückt, 0x40 online](view:setup-sensors)

Links steht eine Liste mit 8 Einträgen, 220px breit: SENSORBUS, INA228, INA3221 0x40 bis 0x43, AS5600, PHASENABGRIFF. Ein Eintrag zeigt den Namen des Sensors und darunter, was der Sensor tut. Ein Tippen auf einen Eintrag zeigt rechts seine Zeilen, 754px breit.

- Ein Eintrag mit Einschaltzeile zeigt allein diese Zeile, solange er aus ist, und sein Zustand lautet `AUS`. EIN ergänzt die übrigen Zeilen darunter.
- Ein Druck wählt beim Loslassen, innerhalb 8px vom Druck und auf dem Eintrag. Weiteres Rutschen wählt nichts und verschiebt nichts.
- Jeder Eintrag behält seine Scrollposition.
- Ein Wechsel des Eintrags behält, was ungespeichert ist. SPEICHERN und die Warnung beim Verlassen arbeiten wie auf jeder Liste.
- RESET nennt den Eintrag, `INA228 ZURÜCKSETZEN`, und setzt alle Zeilen dieses Eintrags auf ihre Vorgaben, auch die verborgenen. Ein Wischen, das auf der Taste landet, bewirkt nichts.
- Der Drehknopf tut auf SETUP nichts.

| Eintrag | Zustand im Betrieb |
|---|---|
| SENSORBUS | `GPIO 16 / 17`; `KEINE PINS GESETZT`; `VOM IOMCU ABGELEHNT`; auf dem Carrier `GPIO 16 / 17 VERLÖTET` |
| INA228, INA3221 0x40 bis 0x43 | [Was ein Baustein tut](#was-ein-baustein-tut) |
| AS5600, PHASENABGRIFF | `EIN`; `VOM IOMCU ABGELEHNT` |

## SENSORBUS und INA228

![Die Zeilen des INA228](view:setup-sensors-ina228)

Der IOmcu liest einen TI INA228 im Strompfad des ESC und bis zu vier TI INA3221 am selben I2C-Bus (Inter-Integrated Circuit).

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
- Ein Monitor, der aufhört zu antworten, schaltet den Prüfstand nicht auf SAFE: Auf die Messwerte der Monitore löst nichts aus.

![GPIO 5 für Sensor-SDA abgelehnt](view:setup-sda-why)

Ein Pin steht mit dem Wort voran, `GPIO 1`, im Eintrag des Tastenfelds, in der Zeile und in der Frage beim Verlassen von SETUP ohne Speichern. Der Kopf des Tastenfelds nennt den Bereich ebenso, `GPIO -1 bis 47`.

![Das Tastenfeld von Sensor-SDA, GPIO 1 getippt](view:setup-sda-keypad)

Das Band nennt, was schiefgeht, eine Meldung zur selben Zeit, höchstens eine alle 5s. Zum Beispiel: `SDA oder SCL der Sensoren nicht festgelegt. SETUP > SENSORS prüfen.`, `INA228 und INA3221 nutzen beide 0x40. SETUP > SENSORS prüfen.`, `INA228 antwortet nicht unter 0x45.`, `INA3221 0x40 CH1 bei 1.64A übersteuert. Anzeige zeigt nur den Grenzwert.`. Eine Meldung über die Einstellung (Pins nicht festgelegt, eine Adresse, ein abgelehnter Wert) verlässt das Band, sobald die Einstellung wieder stimmt und der IOmcu sie hält.

Der Zustand des Eintrags INA228 liest sich wie der eines Bausteins ([Was ein Baustein tut](#was-ein-baustein-tut)): `VOM IOMCU ABGELEHNT` auch für abgelehnte Pins und für einen IOmcu ohne die Page SENSE, und `ADRESSE WIE INA3221`, wenn ein bestückter Baustein seine Adresse hat, sodass keiner gelesen wird.

## Die INA3221-Bausteine

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| 0x40 bestückt bis 0x43 bestückt | EIN, AUS | AUS | ein INA3221 an dieser Adresse; das DAOKAI-Modul nimmt 0x40 mit A0 nach GND gebrückt |

Ein bestückter Baustein zeigt drei Zeilen mehr für jeden seiner drei Kanäle. Ein nicht bestückter Baustein zeigt keine weiteren.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| 0x40 CH1 misst | KEINE, AUSGANG 1 bis AUSGANG 8, PORT 1 bis 4, NETZTEIL | AUSGANG 1 für 0x40 CH1, sonst KEINE | worin der Shunt des Kanals liegt: ein Ausgangskanal, ein Programmierport oder die Versorgungsschiene |
| 0x40 CH1 Shunt | AUS, 5,0 bis 1000,0mΩ, Schritte von 0,1mΩ | 100,0 | AUS: der Kanal wird nicht gelesen; 100mΩ misst bis 1,64A |
| 0x40 CH1 Grenze | AUS, 10 bis 32767mA, Schritte von 10mA mit den Pfeilen | AUS | das Band sagt, wenn der höchste Strom eines 50-ms-Fensters sie überschreitet, und der Prüfstand schaltet auf SAFE mit dem Ausgang des Netzteils aus |

- `+` auf einem Shunt bei AUS gibt 5,0mΩ, `-` bei 5,0mΩ gibt AUS.
- Ein Tippen auf den Wert eines Shunts öffnet das Tastenfeld: 44,4mΩ vom Standardwert 100,0 sind `4`, `4`, `.`, `4` und OK, wo `-` 556 Schritte braucht. 0 ist AUS; 0,1 bis 4,9 wird abgelehnt: Das Tastenfeld bleibt offen und zeigt bis zur nächsten Taste `0 oder 5.0 bis 1000.0mΩ` statt des Bereichs.
- Der Kanal, der jede Millisekunde gelesen wird und dessen Strom der Bildschirm [SERVO](servo.md#das-messgerät-der-servoschiene) und der Servotest nutzen, ist der niedrigste Kanal mit Shunt an einem bestückten Baustein, der das Ziel des Servotesters misst: einen Ausgang, solange das Ziel OUTPUTS ist, PORT n, solange es Programmierport n ist.
- Das Panel schreibt die Einstellung 500ms nach der letzten Änderung, nur bei einem Prüfstand in SAFE. Die Grenzen gehen 500ms nach ihrer letzten Änderung in jedem Zustand an einen IOmcu mit 0.22.0 oder neuer.

## Auf dem rbmod-Carrier

![SENSORS auf dem Carrier, 0x40 verlötet](view:setup-sensors-rbmod)

Der [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) lötet Sensor SDA und SCL auf GPIO 16 und GPIO 17, Tap-Pin auf GPIO 22 und einen INA3221 auf 0x40. Diese Zeilen stehen ohne Tasten und öffnen kein Tastenfeld, die für die Pins gespeicherten Werte bleiben und gelten wieder an einem von Hand verdrahteten Modul, und sie sagen `Auf dem Board verlötet. Keine Einstellung.`:

| Zeile | Zeigt | Shunt zwischen |
|---|---|---|
| 0x40 bestückt | EIN | |
| 0x40 CH1 misst | SCHIENE B | dem PD mini und Servoschiene B: PWM03, PWM02 |
| 0x40 CH2 misst | SCHIENE A | dem PD mini und Servoschiene A: PWM01, PWM04 |
| 0x40 CH3 misst | PROG PORTS 5V | den 5V von GPIO 21 und den Programmierports |

- Die Shunts und die Grenzen bleiben Einstellungen, mit den Bereichen oben.
- 0x41 bis 0x43 sind frei für Bausteine am Stecker J5 des Sensorbusses.
- Der Kanal, der jede Millisekunde gelesen wird, ist die Schiene des niedrigsten GPIO, den der Servotester als SERVO PWM treibt: CH2 für PWM01 (GPIO 2) oder PWM04 (GPIO 1), CH1 für PWM02 (GPIO 4) oder PWM03 (GPIO 5). Mit einem Programmierport als Ziel ist es CH3.
- SERVO nennt das Messgerät nach seiner Schiene, `INA SCHIENE A`; eine Grenzmeldung lautet `INA3221 0x40 CH2 (SCHIENE A) über der Grenze von 0.80A.`.

## Grenzen

![Eine Grenze bei einem IOmcu älter als 0.22.0](view:setup-sensors-limit)

Solange eine Grenze gesetzt ist, prüft der IOmcu jedes 50-ms-Fenster eines Kanals, und das Panel liest alle 200ms, welche Kanäle sie überschritten haben. Das Band sagt, höchstens einmal alle 5s und einmal alle 30s für einen Kanal, der sie weiter überschreitet. Kanäle, die gleichzeitig darüber liegen, kommen reihum dran: sind alle 12 darüber, wird jeder innerhalb von 60s gesagt.

| Band | Bedeutung |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) über der Grenze von 0.80A.` | ein Fenster hat die Grenze des Kanals überschritten; in Klammern, was der Kanal misst; die Grenze, an der das Fenster geprüft wurde |
| `Ein INA3221-Kanal hat seine Grenze überschritten; welcher, ging auf dem Link verloren.` | die Antwort, die den Kanal nannte, ging verloren; gesagt nach dem nächsten Link-Aufbau, nicht nach einem Neustart des IOmcu |
| `INA3221-Grenzen abgelehnt. SETUP > SENSORS prüfen.` | der IOmcu hat die Grenzen abgelehnt und behält seine, oder er hat die Page abgelehnt |

Eine Zeile GRENZE, die der IOmcu nicht hält, steht in der Warnfarbe: solange ihr Schreiben wartet, nach einer Ablehnung und bei einem IOmcu älter als 0.22.0 (Protokoll 4.27), der keine Grenze prüft (`IOmcu vor 0.22.0: keine Grenze geprüft.`).

![Das Tastenfeld von 0x40 CH1 Shunt, 44,4 getippt](view:setup-sensors-keypad)

![3mΩ abgelehnt, der Grund statt des Bereichs](view:setup-sensors-why)

## Was ein Baustein tut

Der Eintrag eines Bausteins sagt unter seinem Namen, was er tut:

| Zustand | Bedeutung |
|---|---|
| `ONLINE` | der IOmcu liest den Baustein |
| `SUCHT DEN BAUSTEIN` | die Einstellung ist angenommen; die ersten 2,5s |
| `ANTWORTET NICHT` | an der Adresse antwortet nichts |
| `ANDERE ID ANTWORTET` | dort antwortet etwas anderes |
| `NOCH NICHT GESCHRIEBEN` | eine Änderung wartet auf ihr Schreiben |
| `NOCH NICHT GELESEN` | noch antwortet kein IOmcu |
| `VOM IOMCU ABGELEHNT` | der IOmcu hat die Einstellung abgelehnt |
| `ALLE KANÄLE AUS` | alle drei Shunts stehen auf AUS; nichts wird geschrieben |
| `INA228 AUF DIESER ADRESSE` | der INA228 ist auf dieser Adresse eingeschaltet; keiner wird gelesen |
| `UNGELESEN, IOMCU < 0.22.0` | ein älterer IOmcu liest nur einen Baustein |
| `KEINE BUS-PINS` | SENSORBUS hat keinen Pin für SDA oder SCL; nichts wird gelesen |

![Ein IOmcu älter als 0.22.0](view:setup-sensors-old)

Die Zeilen eines Bausteins, den der IOmcu nicht liest, stehen in der Warnfarbe, mit dem Grund statt der Hilfezeile. Ein IOmcu älter als 0.22.0 (Protokoll 4.25) liest einen INA3221: den Baustein des Kanals, der das Ziel des Servotesters misst, sonst den niedrigsten bestückten.

Ein Panel, das von 0.21 aktualisiert wurde, nimmt seine erste Einstellung von dem einen INA3221, den es hatte: der Baustein an der alten Adresse bestückt, sein CH1 auf dem alten Shunt und misst AUSGANG 1, oder PORT 1, wenn das Ziel des Servotesters der Programmierport war; jede Grenze AUS. Das nächste SPEICHERN behält sie.

## Eine Zeile, die der IOmcu nicht hält

![Zeilen, die der IOmcu nicht übernommen hat](view:setup-unheld)

Eine Zeile, deren Wert nicht der ist, den der IOmcu hält, hat Rand und Wert in der Warnfarbe, und ihre Hilfezeile lautet `Vom IOmcu nicht übernommen.`. Die Markierung steht:

- nach einer Änderung, bis das Schreiben übernommen ist: 500ms nach der letzten Änderung, für die Strommonitore erst, wenn der Prüfstand SAFE ist;
- bei einem Wert, den der IOmcu abgelehnt hat, bis er auf einen geändert wird, den er annimmt;
- bei einem eingeschalteten Teil, das der IOmcu aus hält: Sensor-SDA auf -1, der INA228 auf der Adresse eines bestückten INA3221 oder ein IOmcu, der für das Teil zu alt ist.

Solange kein IOmcu antwortet, ist keine Zeile markiert. AS5600-Mitte ist ein eigener Wert des Panels und wird nie markiert.

Die Markierung auf Sensor SDA, auf AS5600 und auf den Zeilen des Phasenabgriffs stellt auch den Zustand des Eintrags auf `VOM IOMCU ABGELEHNT`.

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
| Abgriff-Pin | 0, 1, 2, 4 bis 7, 13 bis 22 | 22 | der GPIO des IOmcu; GPIO 22 ist Pad 29. Die Pins, die das RP2350-CAN herausführt und nicht belegt, ohne die ADC-Pins GPIO 26 bis GPIO 29 |
| Tiefster Ton | 50 bis 2000Hz, Schritte von 10Hz | 400 | ein tieferer Ton ist kein Piepton |
| Höchster Ton | 100 bis 6900Hz, Schritte von 50Hz | 6500 | über dem tiefsten Ton |
| Tonsprung | 0 bis 50% | 8 | ein so großer Tonhöhensprung beginnt einen neuen Piepton; 0 trennt nur an Stille |
| Abgriff-Pause | 1 bis 100ms | 3 | die Stille, die einen Piepton beendet |
| Min. Perioden | 1 bis 64 | 3 | die Tonperioden, die einen Piepton ergeben |

Der IOmcu lehnt einen ADC-Pin (Analog-Digital-Umsetzer), einen an einen Ausgang gebundenen Pin und einen Pin ab, den der Sensorbus oder der PD mini hält. Abgriff-Pause muss mindestens eine Periode von Tiefster Ton lang sein: 3ms bei den voreingestellten 400Hz, deren Periode 2,5ms ist. Die Meldung eines abgelehnten Werts verlässt das Band, sobald der Wert auf einen zurückgestellt ist, den der IOmcu nimmt. Die Laufseite von [ESC STICK](motor-stick.md) zeigt die Pieptöne.

Vibration und Optischer Tacho haben gespeicherte Schlüssel und keinen Eintrag: kein Bildschirm und kein IOmcu liest sie in diesem Release.

## Verwandte Seiten

- [Setup](setup.md)
- [Anschlüsse](setup-interfaces.md)
- [Servo](servo.md)
