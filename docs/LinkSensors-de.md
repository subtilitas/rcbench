# Die INA3221-Map und ihre Fenster

<sub>[English](LinkSensors.md) · **Deutsch**</sub>

Diese Seite beschreibt die Konfigurations- und Messwert-Pages des
INA3221. [Der Link](Link-de.md) behandelt Verkabelung, Frame-Format,
Page Map und die übrigen Pages. I2C steht für Inter-Integrated Circuit.

SENSE_MAP (0x35), SENSE_WIN_A (0x37) und SENSE_WIN_B (0x38) benötigen
Protokoll 4.25 oder höher; SENSE_LIMIT (0x36) benötigt 4.27.
SENSE_MAP konfiguriert bis zu 4 Strommonitore TI INA3221 an den
Adressen 0x40 bis 0x43 auf dem von SENSE geöffneten Sensorbus. Die
Map legt die bestückten Bauteile, den Shunt und die Messzuordnung
jedes ihrer 12 Kanäle sowie den FAST-Kanal fest. Die beiden
Fenster-Pages enthalten das letzte 50-ms-Fenster jedes Kanals.
SENSE_LIMIT legt pro Kanal eine Stromgrenze fest und markiert
Fenster, die sie überschreiten. `shared/outputs/sense_map.c` und `sense_limit.c` halten die Regeln;
`test_sense_map` und `test_sense_limit` testen sie auf dem Host.

Kanal c ist Kanal ch von Bauteil a für c = 3 a + ch - 1, also 0 bis 11:
Kanal 0 ist CH1 des Bauteils an 0x40, Kanal 11 ist CH3 des Bauteils an 0x43.

## SENSE_MAP

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 | `ENABLE` | Bit a: das Bauteil an 0x40 + a ist bestückt. 0 bis 15 |
| 1 | `FAST` | der Kanal, der jede 1ms gelesen wird, 0 bis 11, oder 0xFF für keinen. SERVO_WIN trägt seine letzten 4 Fenster, und SERVO_SENSE misst an ihm die Zeit einer Bewegung |
| 2 bis 13 | `SHUNT[c]` | der Shunt von Kanal c in 0,1mΩ, 50 bis 10000 (5mΩ bis 1Ω); 0 schaltet den Kanal ab: er wird nie gelesen |
| 14 bis 25 | `WATCH[c]` | was Kanal c misst: 0 nichts genannt, 0x01 bis 0x08 Ausgangskanal 1 bis 8, 0x10 bis 0x13 Programmierport 1 bis 4, 0x20 die Versorgung; ab 4.33 die eigenen Schienen eines Boards, 0x30 Servoschiene A, 0x31 Servoschiene B, 0x32 die 5V der Programmierports. Der IOmcu hält den Wert und setzt Capability-Bit 3 von IDENTITY, solange ein Kanal, den er liest, einen Ausgang, einen Port oder eine Schiene misst. Auf dem rbmod-Carrier halten 0x40 CH1 bis CH3 die Werte 0x31, 0x30 und 0x32 ([Link, Boards mit verlöteten Pins](Link-de.md#boards-mit-verlöteten-pins)) |
| 26 bis 29 | `ID[a]` | nur lesbar: die Die-ID, die Bauteil a bei seiner letzten Abfrage geantwortet hat, 0 vor der ersten |
| 30 | `FLAGS` | nur lesbar: Bit a Bauteil a online; Bit 4 + a Bauteil a hat mit einer anderen Identität geantwortet; Bit 8 die Map wurde geschrieben und wird gehalten (`IN_FORCE`); Bit 9 sie wurde nie geschrieben und ist das eine Bauteil von SENSE (`DERIVED`) |
| 31 | reserviert | liest 0 |

Die Register 0 bis 25 werden im Flash des IOmcu gespeichert. Ein
Schreibzugriff muss innerhalb dieses Bereichs bleiben; erreicht er
Register 26, folgt READ_ONLY. Die vorgeschlagenen Werte werden in
die Map eingefügt, danach wird die gesamte Map geprüft. Bei einer
Ablehnung bleiben alle Register unverändert. BAD_VALUE folgt bei:

- `ENABLE` über 15;
- `FAST` nicht 0xFF und über 11, auf einem nicht bestückten Bauteil oder auf
  einem Kanal mit Shunt 0;
- ein Shunt ungleich 0 außerhalb von 50 bis 10000;
- ein `WATCH`-Wert außerhalb der Liste oben;
- auf dem rbmod-Carrier eine Map ohne das Bauteil auf 0x40 oder mit einem
  anderen `WATCH` auf einem seiner drei Kanäle;
- ein bestücktes Bauteil mit allen drei Shunts 0;
- ein bestücktes Bauteil auf der Adresse des INA228, solange SENSE den
  INA228 freigibt;
- eine Map, deren gespiegelte SENSE-Konfiguration (unten) SENSE selbst
  abweist: die Pins des Busses, solange die Map den Bus öffnet;
- jede Änderung, solange die Bank einen Ausgang treibt.

Werte für ein nicht bestücktes Bauteil bleiben erhalten und werden
nur auf ihren Bereich geprüft. Eine Änderung von `ENABLE`, `FAST`
oder einem Shunt erzeugt eine neue Konfiguration: Core 1 richtet den
Bus neu ein und verwirft alle Messwerte und Fenster der vorherigen
Konfiguration. Eine Änderung allein von `WATCH` löst das nicht aus.
Das Schreiben der bereits gültigen Map ändert nichts und wird in
jedem Zustand der Bank angenommen. Dasselbe gilt für das erste
Schreiben der abgeleiteten Map mit ihren aktuell gelesenen Werten.

## SENSE und die Map

Bis SENSE_MAP zum ersten Mal geschrieben wird, ist die Map aus dem einen
INA3221 von SENSE abgeleitet: `ENABLE` Bit a für das Bauteil an Register 8
von SENSE, solange Bit 1 von ENABLE in SENSE gesetzt ist, der Shunt von SENSE
auf jedem Kanal in Register 10 von SENSE, `FAST` CH1 dieses Bauteils,
solange CH1 gelesen wird, und nichts beobachtet. Bit 9 von `FLAGS` sagt das.
Ein Prüfstand, dessen Panel SENSE_MAP nie schreibt, arbeitet allein mit dem einen INA3221 von SENSE.

Das erste Schreiben aktiviert die Map. Kein weiterer Schreibzugriff
kann sie in den abgeleiteten Modus zurücksetzen. Danach spiegeln
Bit 1 von ENABLE und die Register 8 bis 10 von SENSE die Map:

| SENSE | Mit der Map in Kraft |
| --- | --- |
| ENABLE Bit 1 | gesetzt, solange ein Bauteil bestückt ist |
| 8 `I3221_ADDR` | das Spiegelbauteil: das Bauteil von `FAST`, oder das niedrigste bestückte, solange `FAST` 0xFF ist |
| 9 `I3221_SHUNT_DMOHM` | der CH1-Shunt des Spiegelbauteils, oder sein erster Shunt ungleich 0 |
| 10 `I3221_CHANNELS` | die Kanäle des Spiegelbauteils mit einem Shunt |

Ein Schreiben an SENSE, das Bit 1 von ENABLE oder die Register 8 bis 10
ändern würde, wird dann mit BAD_VALUE abgewiesen; ein Schreiben an SENSE,
das sie so trägt, wie sie lesen, wird angenommen. Die Flag-Bits 4 bis 6 von
SENSE und seine INA3221-Die-ID sind die des Spiegelbauteils. Die Register 0
bis 11 von SERVO_SENSE sind die drei Kanäle des Spiegelbauteils. SERVO_WIN
ist der Ring von `FAST`, und Kanal 1 einer Messung auf SERVO_SENSE ist
`FAST`: ein ARM der Messung, solange `FAST` 0xFF ist, wird mit
BAD_VALUE abgewiesen.

Das Fähigkeitsbit 3 von IDENTITY (`SERVO_SENSE`, Strom je Ausgang) ist das
INA3221-Bit von SENSE, solange die Map abgeleitet ist. Mit der Map in Kraft
ist es gesetzt, solange ein Kanal, den die Map liest (ein bestücktes
Bauteil, ein Shunt ungleich 0), einen Ausgangskanal oder einen
Programmierport beobachtet. Es ändert sich nur mit einem angenommenen
Schreiben an SENSE oder SENSE_MAP.

## SENSE_WIN_A und SENSE_WIN_B

Diese Pages enthalten das letzte vollständige 50-ms-Fenster jedes
Kanals: Kanäle 0 bis 5 auf SENSE_WIN_A, 6 bis 11 auf SENSE_WIN_B.
Alle Schreibzugriffe werden mit READ_ONLY abgelehnt. Die Pages
werden nicht im Flash gespeichert.

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 | `WINDOW` | die Nummer des Fensters modulo 65536, Register 12 von SERVO_SENSE |
| 1 | `FLAGS` | Bit a Bauteil a online, auf beiden Pages; Bit 8 ein Fenster hat unter der geltenden Konfiguration geschlossen (`HAVE`) |
| 2 + 5 k | `MEAN_MA` | Eintrag k, Kanal k der Page: mittlerer Strom in mA, mit Vorzeichen |
| 3 + 5 k | `MAX_MA` | höchster Strom in mA, mit Vorzeichen |
| 4 + 5 k | `MEAN_MV` | mittlere Busspannung in mV, an der Lastseite des Shunts |
| 5 + 5 k | `MIN_MV` | niedrigste Busspannung in mV |
| 6 + 5 k | `E_FLAGS` | Bit 0 Stromsamples, Bit 1 Spannungssamples, Bit 2 ein Stromsample am oberen Bereichsende, Bit 3 eines am unteren |

Eintrag k von SENSE_WIN_B entspricht Kanal 6 + k. Wie bei SERVO_SENSE
gehen Samples am Bereichsende nicht in die berechneten Werte ein.
Ein abgeschalteter Kanal oder ein Kanal eines nicht erreichbaren
Bauteils liefert durchgehend 0.

Die Raten, je 1-ms-Tick bei 400kHz: `FAST` jede 1ms; jeder andere Kanal
mit einem Shunt reihum, einer je Tick, 1000 / n Hz bei n davon (500Hz je
für CH2 und CH3 eines Bauteils, 90,9Hz je bei 11); jede Busspannung alle
24ms (41,7Hz); Mask/Enable und das Zurücklesen der Konfiguration jedes
Bauteils mit 25Hz. Der längste Tick hält 742,5µs Buszeit. 4 Bauteile mit
12 Kanälen, der INA228 und der Ausgangs-Encoder AS5600 belegen 60,8% des
Busses; ein Bauteil mit 3 Kanälen und der INA228 43,4%. Höchstens ein
Bauteil wird je Tick abgefragt.

## SENSE_LIMIT

Diese Page enthält eine Stromgrenze je Kanal und markiert Kanäle,
deren Fenster sie überschreiten. Der IOmcu setzt nur die Markierung.
Das Panel liest sie alle 200ms und behandelt sie als Fehler: Der
Prüfstand schaltet auf SAFE und den Netzteilausgang aus. Ab Link
4.31 wartet die Abschaltung auf das Ende eines laufenden Schreibens
in den Servospeicher, wenn ein Programmierport die Versorgung belegt
([Sicherheit, Die Versorgung eines Programmierports](Safety-de.md#die-versorgung-eines-programmierports)).

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 bis 11 | `LIMIT_MA[c]` | die Grenze von Kanal c in mA, 1 bis 32767; 0 = keine |
| 12 | `OVER` | nur lesbar: Bit c, ein Fenster von Kanal c lag seit dem letzten Lesen dieses Registers über seiner Grenze |
| 13 | `OVER_COUNT` | nur lesbar: Kanal-Fenster über der Grenze, modulo 65536, seit dem Start des IOmcu |
| 14, 15 | reserviert | lesen 0 |

Die Register 0 bis 11 werden im Flash des IOmcu gespeichert. Ein
Schreibzugriff muss in diesem Bereich bleiben. Erreicht er Register
12, folgt READ_ONLY; ein Wert über 32767 führt zu BAD_VALUE. Bei
einer Ablehnung wird keines der Register des Schreibzugriffs
gespeichert. Gültige Zugriffe werden in jedem Zustand der Bank
angenommen.

Core 1 prüft das 50-ms-Fenster jedes Kanals einmal, wenn das Fenster
schließt. Ein Fenster liegt über der Grenze, wenn sein höchster Strom, in mA
wie SENSE_WIN ihn zeigt, über der Grenze liegt: bei einer Grenze von 100mA
liegen 100,4mA nicht darüber und 100,5mA schon. Ein Fenster liegt auch
darüber, wenn eines seiner Samples das obere Bereichsende gelesen hat und
dieses Ende, ungerundet, über der Grenze liegt: 163,8mV über dem Shunt,
1638,0mA an 0,1Ω, 163,8mA an 1Ω, so dass eine Grenze von 163mA an 1Ω
überschritten wird. Ein negativer Strom liegt nie darüber. Ein
abgeschalteter Kanal oder einer eines Bauteils, das beim Schließen des
Fensters nicht online ist, liegt nie darüber. Core 1 zählt die Fenster jedes
Kanals über der Grenze; Core 0 addiert, was die Zähler gewonnen haben, sooft
er den Snapshot von Core 1 übernimmt, so dass kein Fenster verloren geht,
gleich wie lange Core 0 braucht. Eine geschriebene Grenze gilt ab dem
nächsten Fenster, das schließt.

`OVER` behält seine Bits, bis ein Lesezugriff Register 12 einschließt.
Dieser Zugriff liefert die Bits zurück und löscht sie. Geht die
Antwort auf dem Link verloren, fehlen dem Leser diese Bits;
`OVER_COUNT` zählt sie trotzdem. Eine neue Konfiguration erhält
`OVER` und `OVER_COUNT`. Die Konsole liest beide, ohne sie zu löschen.

## Im Flash gehalten

Der Speicherdatensatz des IOmcu in Version 8 enthält die Register
0 bis 25 von SENSE_MAP, das Flag für die aktivierte Map und die
Register 0 bis 11 von SENSE_LIMIT. Ein Datensatz der Version 8
belegt 308 Bytes in einem Slot aus zwei Flash-Pages zu 256 Bytes:
8 Slots je Sektor zu 4096 Bytes, 16 im Speicher. Ein Speichervorgang
programmiert beide Pages in einem Fenster. Ein Abbruch in einer der
Pages hinterlässt keinen gültigen Datensatz: Die Zahl seiner 0-Bits
und seine CRC-16 decken beide Pages ab.

Beim Booten durchlaufen die gespeicherten Grenzen dieselben Prüfungen
wie ein Schreibzugriff auf SENSE_LIMIT. Ein Wert über 32767 lässt
alle Grenzen auf 0. Die Map wird erst nach erfolgreicher
SENSE-Konfiguration wiederhergestellt und ebenfalls mit den
Schreibprüfungen geprüft. Wird die gespeicherte Map abgelehnt,
bleibt sie im abgeleiteten Modus.

Ein Speicher, den ein früherer Build geschrieben hat, hält Records der
Versionen 3 bis 7 in Slots aus einer Page. Dieser Build liest den neuesten
davon, solange der Speicher keinen Record der Version 8 hält: die Map liest
dann abgeleitet. Der Sektor mit diesem Record wird erst gelöscht, wenn der
erste Record der Version 8 geschrieben ist, so dass ein Stromausfall davor
nichts verliert. Ein IOmcu-Image älter als 4.25 liest keinen Record der
Version 8 und startet wie mit einem leeren Speicher.

## Ältere Gegenstellen

| Panel | IOmcu | SENSE_MAP, SENSE_WIN_A, SENSE_WIN_B und SENSE_LIMIT |
| --- | --- | --- |
| 4.25 oder neuer | 4.27 oder neuer | bedient. Das Panel liest SENSE_MAP bei jedem Link-Aufbau ganz, schreibt die Register 0 bis 25 in einem Schreiben so, wie SETUP > SENSORS sie einstellt, und liest die Register 26 bis 30 alle 500ms, solange ein Baustein bestückt ist. Nach einem angenommenen Schreiben liest es SENSE und IDENTITY neu: Fähigkeitsbit 3 kann sich schon mit `WATCH` allein ändern. SENSE_WIN_A liest es ganz, solange 0x40 oder 0x41 in der Map in Kraft bestückt ist, SENSE_WIN_B, solange 0x42 oder 0x43 es ist, jede in jedem Poll nach SERVO_WIN und höchstens alle 40ms, in ARMED wie in SAFE, für die Spalten des Lauf-Logs ([Screens](Screens-de.md#tabelle-und-was-der-esc-meldet)). SENSE_LIMIT liest es bei jedem Link-Aufbau ganz, schreibt die Register 0 bis 11 in einem Schreiben 500ms nach der letzten Änderung einer Grenze, in jedem Zustand der Bank und nur, wo die Page abweicht, und liest die Register 12 und 13 alle 200ms, solange die Page eine Grenze hält, die nicht 0 ist ([Screens](Screens-de.md#sensors-die-ina3221-bausteine)) |
| 4.33 oder neuer | 4.32 oder älter, auf dem rbmod-Carrier | SENSE_MAP lehnt die Map des Carriers ab: ihre `WATCH`-Werte 0x30 bis 0x32 stehen nicht in der Liste dieses IOmcu. SETUP > SENSORS markiert 0x40 mit `VOM IOMCU NICHT ÜBERNOMMEN`, bis der IOmcu das Image des Panels fährt |
| 4.25 oder neuer | 4.25 oder 4.26 | SENSE_MAP und SENSE_WIN wie oben; das Panel sendet nichts an SENSE_LIMIT, und SETUP > SENSORS markiert jede Grenze, die nicht AUS ist, als nicht geprüft |
| 4.25 oder neuer | 4.24 oder älter | das Panel sendet an keine der vier Pages und richtet den einen INA3221 von SENSE auf einen bestückten Baustein ein ([Screens](Screens-de.md)) |
| 4.24 oder älter | 4.25 oder neuer, Map abgeleitet | das Panel liest und schreibt die vier Pages nie. SENSE behält seinen einen INA3221. Gehaltene Grenzen werden geprüft, und `OVER` hält seine Bits, bis etwas es liest |
| 4.24 oder älter | 4.25 oder neuer, Map in Kraft | Bit 1 von ENABLE und die Register 8 bis 10 von SENSE lesen das Spiegelbauteil. Ein Schreiben an SENSE, das sie ändert, wird mit BAD_VALUE abgewiesen, so dass dieses Panel die INA3221-Konfiguration nicht ändern kann. Der INA228, der Encoder und die Pins bleiben seine |

## Auf der Konsole

Ein mit `IOMCU_CONSOLE=ON` gebautes Image druckt die Map alle 3s, solange
ein Bauteil auf SENSE freigegeben ist, nach den beiden `sense`-Zeilen
([Erster Lauf auf der Hardware](FirstRun-de.md#8-strommonitore-ina228-ina3221)):

```
rcbench-iomcu: sense map fit 0x3 fast 0 flags 0x0103 | mA 120 4 0 310 0 0 0 0 0 0 0 0 | over 0x008 count 3
```

`fit` ist `ENABLE`, `fast` ist `FAST` (255 für keinen), `flags` ist
`FLAGS`, die 12 Werte sind `MEAN_MA` jedes Kanals ab Kanal 0, und `over` und
`count` sind `OVER` und `OVER_COUNT` von SENSE_LIMIT.
