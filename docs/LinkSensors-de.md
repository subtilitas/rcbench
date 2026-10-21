# Die INA3221-Map und ihre Fenster

<sub>[English](LinkSensors.md) · **Deutsch**</sub>

Diese Seite ist ein Abschnitt von [Der Link](Link-de.md), der die Verkabelung, das
Frame-Format, die Page Map und die übrigen Pages enthält. I2C steht für
Inter-Integrated Circuit.

SENSE_MAP (0x35), SENSE_WIN_A (0x37) und SENSE_WIN_B (0x38) sind neu in 4.25.
SENSE_MAP richtet bis zu 4 Strommonitore TI INA3221 auf dem Sensorbus ein,
den SENSE öffnet, an 0x40 bis 0x43: welche Bauteile bestückt sind, einen
Shunt für jeden ihrer 12 Kanäle, was jeder Kanal misst, und den FAST-Kanal.
Die beiden Fenster-Pages tragen das letzte 50-ms-Fenster jedes Kanals.
`shared/outputs/sense_map.c` hält die Regeln; `test_sense_map` testet sie auf
dem Host.

Kanal c ist Kanal ch von Bauteil a für c = 3 a + ch - 1, also 0 bis 11:
Kanal 0 ist CH1 des Bauteils an 0x40, Kanal 11 ist CH3 des Bauteils an 0x43.

## SENSE_MAP

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 | `ENABLE` | Bit a: das Bauteil an 0x40 + a ist bestückt. 0 bis 15 |
| 1 | `FAST` | der Kanal, der jede 1 ms gelesen wird, 0 bis 11, oder 0xFF für keinen. SERVO_WIN trägt seine letzten 4 Fenster, und SERVO_SENSE misst an ihm die Zeit einer Bewegung |
| 2 bis 13 | `SHUNT[c]` | der Shunt von Kanal c in 0,1 mΩ, 50 bis 10000 (5 mΩ bis 1 Ω); 0 schaltet den Kanal ab: er wird nie gelesen |
| 14 bis 25 | `WATCH[c]` | was Kanal c misst: 0 nichts genannt, 0x01 bis 0x08 Ausgangskanal 1 bis 8, 0x10 bis 0x13 Programmierport 1 bis 4, 0x20 die Versorgung. Der IOmcu hält den Wert und handelt nach nichts davon |
| 26 bis 29 | `ID[a]` | nur lesbar: die Die-ID, die Bauteil a bei seiner letzten Abfrage geantwortet hat, 0 vor der ersten |
| 30 | `FLAGS` | nur lesbar: Bit a Bauteil a online; Bit 4 + a Bauteil a hat mit einer anderen Identität geantwortet; Bit 8 die Map wurde geschrieben und wird gehalten (`IN_FORCE`); Bit 9 sie wurde nie geschrieben und ist das eine Bauteil von SENSE (`DERIVED`) |
| 31 | reserviert | liest 0 |

Die Register 0 bis 25 hält der IOmcu im Flash. Ein Schreiben liegt in ihnen:
eines, das Register 26 erreicht, wird mit READ_ONLY abgewiesen. Die
geschriebenen Register werden in die Map übernommen, und die ganze Map wird
geprüft; eine abgewiesene Map lässt jedes Register, wie es war. Mit
BAD_VALUE abgewiesen:

- `ENABLE` über 15;
- `FAST` nicht 0xFF und über 11, auf einem nicht bestückten Bauteil oder auf
  einem Kanal mit Shunt 0;
- ein Shunt ungleich 0 außerhalb von 50 bis 10000;
- ein `WATCH`-Wert außerhalb der Liste oben;
- ein bestücktes Bauteil mit allen drei Shunts 0;
- ein bestücktes Bauteil auf der Adresse des INA228, solange SENSE den
  INA228 freigibt;
- eine Map, deren gespiegelte SENSE-Konfiguration (unten) SENSE selbst
  abweist: die Pins des Busses, solange die Map den Bus öffnet;
- jede Änderung, solange die Bank einen Ausgang treibt.

Ein nicht bestücktes Bauteil behält die für es geschriebenen Werte, nur auf
ihren Bereich geprüft. Eine Änderung von `ENABLE`, `FAST` oder einem Shunt
ist eine neue Konfiguration: Core 1 richtet den Bus neu ein, und jede Lesung
und jedes Fenster der Konfiguration davor fällt weg. Eine Änderung nur von
`WATCH` ist keine neue Konfiguration. Ein Schreiben der geltenden Map ändert
nichts und wird in jedem Zustand der Bank angenommen, ebenso ein erstes
Schreiben der abgeleiteten Map, wie sie liest.

## SENSE und die Map

Bis SENSE_MAP zum ersten Mal geschrieben wird, ist die Map aus dem einen
INA3221 von SENSE abgeleitet: `ENABLE` Bit a für das Bauteil an Register 8
von SENSE, solange Bit 1 von ENABLE in SENSE gesetzt ist, der Shunt von SENSE
auf jedem Kanal in Register 10 von SENSE, `FAST` CH1 dieses Bauteils,
solange CH1 gelesen wird, und nichts beobachtet. Bit 9 von `FLAGS` sagt das.
Eine Bench, deren Panel SENSE_MAP nie schreibt, läuft wie vor 4.25.

Das erste Schreiben setzt die Map in Kraft, und sie bleibt in Kraft: kein
Schreiben macht sie wieder zur abgeleiteten Map. Von da an spiegeln Bit 1
von ENABLE und die Register 8 bis 10 von SENSE die Map:

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
`FAST`: ein Scharfschalten der Messung, solange `FAST` 0xFF ist, wird mit
BAD_VALUE abgewiesen.

Das Fähigkeitsbit 3 von IDENTITY (`SERVO_SENSE`, Strom je Ausgang) ist das
INA3221-Bit von SENSE, solange die Map abgeleitet ist. Mit der Map in Kraft
ist es gesetzt, solange ein Kanal, den die Map liest -- ein bestücktes
Bauteil, ein Shunt ungleich 0 --, einen Ausgangskanal oder einen
Programmierport beobachtet. Es ändert sich nur mit einem angenommenen
Schreiben an SENSE oder SENSE_MAP.

## SENSE_WIN_A und SENSE_WIN_B

Das letzte vollständige 50-ms-Fenster jedes Kanals: Kanäle 0 bis 5 auf
SENSE_WIN_A, 6 bis 11 auf SENSE_WIN_B. Jedes Schreiben wird mit READ_ONLY
abgewiesen. Nichts wird gehalten.

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 | `WINDOW` | die Nummer des Fensters modulo 65536, Register 12 von SERVO_SENSE |
| 1 | `FLAGS` | Bit a Bauteil a online, auf beiden Pages; Bit 8 ein Fenster hat unter der geltenden Konfiguration geschlossen (`HAVE`) |
| 2 + 5 k | `MEAN_MA` | Eintrag k, Kanal k der Page: mittlerer Strom in mA, mit Vorzeichen |
| 3 + 5 k | `MAX_MA` | höchster Strom in mA, mit Vorzeichen |
| 4 + 5 k | `MEAN_MV` | mittlere Busspannung in mV, an der Lastseite des Shunts |
| 5 + 5 k | `MIN_MV` | niedrigste Busspannung in mV |
| 6 + 5 k | `E_FLAGS` | Bit 0 Stromsamples, Bit 1 Spannungssamples, Bit 2 ein Stromsample am oberen Bereichsende, Bit 3 eines am unteren |

Eintrag k von SENSE_WIN_B ist Kanal 6 + k. Ein Sample am Bereichsende bleibt
aus den Werten heraus, wie auf SERVO_SENSE. Ein abgeschalteter Kanal oder
einer eines Bauteils, das nicht online ist, liest durchweg 0.

Die Raten, je 1-ms-Tick bei 400 kHz: `FAST` jede 1 ms; jeder andere Kanal
mit einem Shunt reihum, einer je Tick, 1000 / n Hz bei n davon (500 Hz je
für CH2 und CH3 eines Bauteils, 90,9 Hz je bei 11); jede Busspannung alle
24 ms (41,7 Hz); Mask/Enable und das Zurücklesen der Konfiguration jedes
Bauteils mit 25 Hz. Der längste Tick hält 742,5 µs Buszeit. 4 Bauteile mit
12 Kanälen, der INA228 und der Ausgangs-Encoder AS5600 belegen 60,8 % des
Busses; ein Bauteil mit 3 Kanälen und der INA228 43,4 %. Höchstens ein
Bauteil wird je Tick abgefragt.

## Im Flash gehalten

Der IOmcu hält die Register 0 bis 25 und ob die Map in Kraft ist in seinem
Speicher-Record, Version 8. Ein Record der Version 8 ist 308 Bytes groß und
füllt einen Slot aus zwei Flash-Pages zu 256 Bytes: 8 Slots je Sektor zu
4096 Bytes, 16 im Speicher. Ein Speichern programmiert beide Pages in einem
Fenster. Ein in einer der beiden Pages abgebrochenes Programmieren liest als
kein Record: die Zahl der 0-Bits des Records und seine CRC-16 decken beide
Pages ab.

Beim Booten kommt die Map nach der Konfiguration von SENSE zurück, durch die
Prüfungen des Schreibens, und nur, wenn die Konfiguration von SENSE
angenommen wurde. Eine gehaltene Map, die dort abgewiesen wird, kommt nicht
zurück, und die Map liest abgeleitet.

Ein Speicher, den ein früherer Build geschrieben hat, hält Records der
Versionen 3 bis 7 in Slots aus einer Page. Dieser Build liest den neuesten
davon, solange der Speicher keinen Record der Version 8 hält: die Map liest
dann abgeleitet. Der Sektor mit diesem Record wird erst gelöscht, wenn der
erste Record der Version 8 geschrieben ist, so dass ein Stromausfall davor
nichts verliert. Ein IOmcu-Image älter als 4.25 liest keinen Record der
Version 8 und startet wie mit einem leeren Speicher.

## Ältere Gegenstellen

| Panel | IOmcu | SENSE_MAP, SENSE_WIN_A und SENSE_WIN_B |
| --- | --- | --- |
| 4.25 | 4.25 | bedient. Das Panel dieses Builds liest und schreibt keine der drei Pages und richtet den einen INA3221 von SENSE ein, so dass die Map abgeleitet bleibt |
| 4.25 | 4.24 oder älter | der IOmcu beantwortet jede der drei Pages mit BAD_PAGE |
| 4.24 oder älter | 4.25, Map abgeleitet | das Panel liest und schreibt die drei Pages nie. SENSE läuft wie zuvor |
| 4.24 oder älter | 4.25, Map in Kraft | Bit 1 von ENABLE und die Register 8 bis 10 von SENSE lesen das Spiegelbauteil. Ein Schreiben an SENSE, das sie ändert, wird mit BAD_VALUE abgewiesen, so dass dieses Panel die INA3221-Konfiguration nicht ändern kann. Der INA228, der Encoder und die Pins bleiben seine |

## Auf der Konsole

Ein mit `IOMCU_CONSOLE=ON` gebautes Image druckt die Map alle 3 s, solange
ein Bauteil auf SENSE freigegeben ist, nach den beiden `sense`-Zeilen
([Erster Lauf auf der Hardware](FirstRun-de.md#8-strommonitore-ina228-ina3221)):

```
rcbench-iomcu: sense map fit 0x3 fast 0 flags 0x0103 | mA 120 4 0 310 0 0 0 0 0 0 0 0
```

`fit` ist `ENABLE`, `fast` ist `FAST` (255 für keinen), `flags` ist
`FLAGS`, und die 12 Werte sind `MEAN_MA` jedes Kanals ab Kanal 0.
