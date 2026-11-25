# Das OpenYGE-Telemetrie- und Parameterprotokoll

<sub>[English](OpenYGE.md) · **Deutsch**</sub>

Diese Spezifikation beschreibt Frameformat, Telemetrie-Payload,
Parameterzugriff und Statuskodierung einschließlich der Berechnungen,
die eine Implementierung benötigt.

> **Maßgeblich ist die englische Seite.** Diese Übersetzung übernimmt
> alle Felder, Namen und Werte. Die englische Fassung ist für die Prüfung
> durch YGE vorgesehen. Ziehe sie bei der Implementierung im Zweifel
> heran, damit widersprüchliche Fassungen eines Byte-Layouts nicht zu
> unterschiedlichen Implementierungen führen.

> Diese Seite gehört zur maßgeblichen Spezifikation für die Prüfung
> durch YGE. Der Codec unter `protocols/openyge/` ist gebaut und getestet,
> aber nicht an die Firmware angebunden.

**° markiert eine erschlossene Bedeutung.** Diese Einträge beruhen auf
Feldnamen und üblicher Praxis bei ESCs (Electronic Speed Controller,
Motorregler), nicht auf einer verbindlichen Quelle. Die Spalte
Widerspruch in Abschnitt 5.3 nennt, wo zwei Quellen sich widersprechen.
Abschnitt 8 nennt die Punkte, die durch Messungen oder Auskünfte geklärt
werden müssen. Die Markierung °, die Widersprüche und Abschnitt 8
kennzeichnen zusammen alle unsicheren Angaben dieser Seite.

## 1. Bitübertragungsschicht

| | |
| --- | --- |
| Signalisierung | UART (Universal Asynchronous Receiver-Transmitter), 115200 Baud, 8N1 |
| Bytereihenfolge | durchgehend Little-Endian |
| Topologie | Half Duplex, ein Master und bis zu 127 adressierbare ESCs |
| Rate | etwa 20Hz Telemetrie |

Der Master hört seine eigene Sendung mit: ein Sender verwirft so viele
empfangene Bytes, wie er gesendet hat. Die Verbindung ist eine einzelne
Leitung mit zusammengeführtem TX (Sendeleitung) und RX (Empfangsleitung).

Das Protokoll braucht auf dem IOmcu einen eigenen UART: einen
Hardware-UART mit über einen Widerstand verbundenem TX und RX und verworfenem
Echo, oder einen Soft-UART in PIO (Programmable Input/Output). Den Panel-Link
kann es nicht mitbenutzen; der ist CAN (Controller Area Network); siehe [Der
Link](Link-de.md).

Zeitbedarf bei 115200 8N1, 86,81µs je Byte:

| | |
| --- | ---: |
| 34-Byte-Telemetrieantwort | 2,95ms |
| 12-Byte-Anfrage | 1,04ms |
| Ein Poll, beide Richtungen | 3,99ms plus die Umschaltzeit des ESC |

Das sind 8% der Leitung bei einem 50-ms-Zyklus.

---

## 2. Frameaufbau

Jeder Frame ist `header · payload · CRC`. `frame_length` zählt den ganzen
Frame, Header und CRC (Cyclic Redundancy Check, Prüfsumme) eingeschlossen.

### 2.1 Header, Protokollversion 3 und neuer: 6 Bytes

| Off | Feld | |
| ---: | --- | --- |
| 0 | `sync` | immer 0xA5 |
| 1 | `version` | 3 für das aktuelle Protokoll |
| 2 | `frame_type` | Abschnitt 3 |
| 3 | `frame_length` | gesamt, Header und CRC eingeschlossen |
| 4 | `seq` | der Master zählt je Anfrage hoch; der ESC spiegelt ihn zurück |
| 5 | `device` | Bit 7 (0x80) gesetzt: Sender ist der Master. Bits 0–6: ESC-Adresse 0x01–0x7F; 0x00 adressiert alle ESCs |

### 2.2 Legacy-Header, Version unter 3: 4 Bytes

Nur `sync`, `version`, `frame_type`, `frame_length`. Ohne Sequenznummer und
ohne Adresse lässt sich ein ESC vor v3 weder pollen noch adressieren: er
sendet Telemetrie unaufgefordert. Parameterzugriff gibt es unterhalb von v3
nicht.

Eine Umsetzung muss `version` lesen, bevor sie weiß, wo die Payload beginnt.

### 2.3 CRC: 2 Bytes, Little-Endian, am Ende

Über jedes Byte von `sync` bis ausschließlich der CRC.

Der Algorithmus ist CRC-16/XMODEM, nicht CRC-16/CCITT-FALSE. Gleiches
Polynom, anderer Seed:

| | Poly | Init | Reflect | XorOut | Prüfwert über `"123456789"` |
| --- | --- | --- | --- | --- | --- |
| OpenYGE | 0x1021 | 0x0000 | keine | keiner | 0x31C3 |
| rcbench-Link-CRC (`link_crc`) | 0x1021 | 0xFFFF | keine | keiner | 0x29B1 |

`protocols/openyge` enthält die eigene Routine `openyge_crc()` und ist
damit unabhängig vom Link-Modul. Die Host-Suite vergleicht sie mit dem
Prüfwert und mit `link_crc()` bei Seed 0, für jede Framelänge von
0 bis 140 Bytes.

---

## 3. Frametypen

| Wert | Name | Von | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | unaufgeforderte Telemetrie: ein ESC vor v3 immer, ein v3-ESC, bis der Master übernimmt |
| 0x02 | `TELE_RESP` | ESC | Telemetrie als Antwort auf eine Anfrage |
| 0x03 | `TELE_REQ` | Master | Telemetrie anfordern |
| 0x04 | `WRITE_PARAM_RESP` | ESC | quittiert ein Parameterschreiben; trägt zugleich Telemetrie |
| 0x05 | `WRITE_PARAM_REQ` | Master | einen Parameter schreiben |

Anfragen enthalten die 4-Byte-Control-Payload aus Abschnitt 5.2. Jeder
Frame vom ESC zum Master enthält die vollständige Telemetrie-Payload
aus Abschnitt 4. Das gilt auch für die Bestätigung eines
Parameterschreibvorgangs.

Das Ausgangsmaterial gibt an, dass Bit 7 von `frame_type` ein Schreiben
markiert (0x81 für ein Parameter-Update). Keiner der gelisteten Werte benutzt
es. Behandle `frame_type` als Aufzählung und lehne nicht aufgeführte
Werte ab. Ob Bit 7 reserviert ist, muss YGE klären.

---

## 4. Telemetrie-Payload: 26 Bytes

Offsets innerhalb der Payload und absolut in einem Version-3-Frame, mit der
Umrechnung in SI-Einheiten (Internationales Einheitensystem). Ein
v3-Telemetrieframe hat 34 Bytes, ein Legacy-Frame 32.

| Payload | v3 abs | Größe | Feld | Einheit | nach SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C + 40 | `°C = v − 40`, −40…215 |
| 2 | 8 | u16 | `voltage` | 10mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | unverändert |
| 8 | 14 | u16 | `rpm` | 10eRPM (elektrische Umdrehungen pro Minute) | `eRPM = v × 10`, siehe unten |
| 10 | 16 | i8 | `pwm` | % | Ausgangstastgrad, vorzeichenbehaftet |
| 11 | 17 | i8 | `throttle` | % | Eingangssollwert, vorzeichenbehaftet |
| 12 | 18 | u16 | `bec_voltage` | mV | unverändert |
| 14 | 20 | u16 | `bec_current` | mA | unverändert |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | Abschnitt 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | Kondensatorpaket |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undokumentiert; mitführen, nicht interpretieren |
| 21 | 27 | u8 | `reserved1` | | möglicherweise ein High-Byte des Verbrauchs über 65Ah ° |
| 22 | 28 | u16 | `pidx` | | Parameterindex, Abschnitt 5 |
| 24 | 30 | u16 | `pdata` | | Parameterwert, Abschnitt 5 |

Jedes 16-Bit-Feld liegt bei beiden Headerlängen auf einem geraden Offset.
Parse trotzdem Byte für Byte. Ein Cast des Puffers auf eine Struktur
setzt Little-Endian und eine Struktur ohne Padding voraus. Außerdem
können unausgerichtete Zugriffe entstehen, die auf Architekturen mit
strikten Ausrichtungsanforderungen undefiniertes Verhalten haben.

### Skalierung von `rpm`

Das Feld ist als „0.1eRPM“ beschrieben, der Referenzcode multipliziert aber
mit zehn. Multiplizieren ist hier die Annahme: 65535 × 10 = 655 350eRPM ist
eine plausible Obergrenze, 6 553 nicht. Messen (T1, Abschnitt 8).

Die mechanische Drehzahl in RPM (Umdrehungen pro Minute) braucht die Pole
des Motors, Parameter 20. Eine Quelle liest Index 20 als Polpaare, die
andere als Pole (Abschnitt 5.3, Widerspruch bei Index 20):

    index 20 = pole pairs:  motor RPM = eRPM / pole pairs
    index 20 = poles:       motor RPM = eRPM / (poles / 2)
    head RPM = motor RPM × pinion teeth / main gear teeth

**Aktuelle Einschränkung.** `openyge_motor_rpm()` und `openyge_head_rpm()`
in `protocols/openyge/` lesen Index 20 als Pole. Bei einem ESC, dessen
Index 20 Polpaare enthält, liefern beide für 2 bis 100 Polpaare die
doppelte Drehzahl und für 1 Polpaar false, weil ein Wert unter 2
abgelehnt wird. Schritt H5 in Abschnitt 8 klärt, welche Lesart gilt.

---

## 5. Parameter

Das Protokoll liest und schreibt die Konfiguration des ESC.

### 5.1 Übertragung: ein Parameter je Frame

Jeder Telemetrieframe enthält ein Paar `(pidx, pdata)`. Der ESC
durchläuft seine Tabelle, und der Master speichert jedes Paar
zwischen. Parameter 0 nennt die Parameteranzahl. Die Tabelle ist erst
vollständig, wenn der Master jeden Index `0 … count−1` empfangen hat.

Bei 20Hz und 32 Parametern dauert das etwa 1,6s. Zeige währenddessen
den Fortschritt an. Gib eine unvollständige Tabelle nicht als die
Einstellungen des ESC aus.

- Übernimm keine Cache-Werte, solange Schreibvorgänge ausstehen. Sonst
  kann ein bereits unterwegs befindlicher Frame den neuen Wert
  überschreiben.
- Ziehe die gesamte Tabelle zurück, sobald ein Schreiben geplant ist.
  Stelle sie erst wieder bereit, wenn jeder Index neu gelesen wurde.

Der Cache in `protocols/openyge/` ist durch eine 64-Bit-Bitmap auf 64 Parameter
begrenzt; die aktuelle Firmware hat 32.

### 5.2 Control-Payload: 4 Bytes

Getragen von `TELE_REQ` und `WRITE_PARAM_REQ`:

| Off | Größe | Feld |
| ---: | ---: | --- |
| 0 | u16 | `index` |
| 2 | u16 | `param`: zu schreibender Wert |

Eine reine Telemetrieanfrage sendet beide als null. Ein Anfrageframe hat
12 Bytes. Geschrieben wird ein Parameter je Frame, jeder mit eigener
Sequenznummer. Blockweises Schreiben gibt es nicht; ein „Speichern“ ist eine
Warteschlange einzelner Schreibvorgänge.

### 5.3 Parametertabelle

Index i der Tabelle ist Parameter i auf der Leitung. Die Anzahl, Index 0,
beträgt 32 ab Firmware v1.03503. Rotorflight dokumentiert den
Parameterzugriff für ESC-Firmware ab V1.03547.

Quellen, mit ihrem Schlüssel in der Spalte Quelle:

| Schlüssel | Quelle |
| --- | --- |
| OY | das OpenYGE-Material, aus dem diese Seite geschrieben ist; die Referenz aus Abschnitt 7 |
| RF-L | Rotorflight-Lua-Skripte (rotorflight-lua-scripts): `mspEscYge.lua` und `esc_yge.lua` |
| RF-F | Rotorflight-Firmware (rotorflight-firmware): `src/main/sensors/esc_sensor.c` |
| RF-D | Rotorflight-Dokumentation, ESC Telemetry |
| M | YGE-Anleitungen: Saphir 125/155 V2, Aureus 105/135 V2, 35–135 LVT, Opto 255, ProgCard III |

Die Rotorflight-Quellen stehen unter der GPL-3.0 (GNU General Public
License Version 3.0). Diese Seite übernimmt daraus Zahlen, Namen und
Bedeutungen, keinen Code.

RF-F übergibt die zwischengespeicherte Tabelle an die Lua-Skripte als
2-Byte-Header, danach Parameter i als Little-Endian-u16 in Slot i. RF-L
liest die Felder in dieser Reihenfolge, also ergibt seine Feldreihenfolge
den Index. RF-L liest 12–13, 14–15, 28–29 und 30–31 als je ein u32 und
teilt 26 in ein unteres und ein oberes Byte.

Die Spalte Zugriff:

| Zugriff | Bedeutung |
| --- | --- |
| identity | nur lesen, wird nie geschrieben |
| telemetry | nur lesen; eine Änderung schaltet den ESC auf ein anderes Telemetrieformat und beendet diese Leitung |
| raw | Bedeutung unbekannt oder nur von einer Quelle benannt; als Zahl angezeigt, nur lesen, wie gelesen zurückgeschrieben |
| CHECK | innerhalb der schreibbaren Werte änderbar; ein falscher Wert kann Modell, Empfänger oder Servos beschädigen, deshalb ist das Feld markiert |
| edit | innerhalb der schreibbaren Werte änderbar |

Ein gelesener Wert außerhalb der schreibbaren Werte wird angezeigt und wie
gelesen zurückgeschrieben. Werte ohne Einheit sind reine Zahlen.

| # | Einstellung | Werte, Skalierung, Einheit | Schreibbar | Zugriff | Quelle | Widerspruch |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | Parameteranzahl | 32; bestimmt die Tabellenlänge | – | identity | OY, RF-F | – |
| 1 | ESC mode | 0 frei, ohne Governor; 1 Hubschrauber, externer Governor; 2 Hubschrauber-Governor; 3 Hubschrauber-Governor Store; 4 Segler mit Bremse; 5 Flugzeug ohne Bremse; 6 Flugzeug mit F3A-Bremse | 0–6 | CHECK | RF-L, M | OY: freies Gas oder Governor aus internem oder externem Sollwert °, keine Werte |
| 2 | BEC voltage | Rohwert × 0,1V; 55–84 = 5,5–8,4V; bis 123 = 12,3V, solange Bit 3 von Index 26 gesetzt ist | 55–84; 55–120, solange 26 Bit 3 gesetzt ist | CHECK | RF-L, OY, M | oberes Ende: RF-L 123 = 12,3V, M 12,0V (Saphir V2, Aureus V2); M 8,4V bei den LVT-Modellen |
| 3 | Motor timing | 0 auto normal; 1–6 = 0°, 6°, 12°, 18°, 24°, 30°; 16 auto normal; 17 auto efficient; 18 auto power; 19 auto extreme | 0–6, 17–19 | edit | RF-L, OY, M | OY: Codes 0–6, keine weiteren °; 16–19 nur aus RF-L; RF-L zeigt 0 und 16 als eine Einstellung und schreibt dafür 0 |
| 4 | Startup response | RF-L: 0 normal, 1 smooth | – | raw | RF-L, OY | OY: Anfangsmoment, Antrieb aus dem Stillstand ° |
| 5 | Governor P gain | Zahl | 1–9 | edit | RF-L, OY | RF-L 1–10, OY 0–9 |
| 6 | Governor I gain | Zahl | 1–9 | edit | RF-L, OY | RF-L 1–10, OY 0–9 |
| 7 | Throttle response | 0 slow, 1 medium, 2 fast, 3 custom (Kurve aus YGEs PC-Programm) | 0–3 | edit | RF-L, OY | OY nennt 2 „high“; dieselben 4 Werte |
| 8 | Cut-off handling | an der Unterspannungsgrenze: 0 aus; 1 slow down, Leistung allmählich reduziert; 2 cut off | 0–2 | CHECK | RF-L, OY | – |
| 9 | Cut-off voltage per cell | 0–5 = 2,9–3,4V, 0,1V je Stufe, LiPo | 0–5 | edit | RF-L, OY, M | – |
| 10 | Active freewheel | 0 aus, 1 auto, 2 unbenutzt, 3 immer an | 0, 1, 3 | edit | RF-L, OY, M | OY: an oder aus °; RF-L: 4 Codes, 2 unbenutzt |
| 11 | ESC type | Modellcode, Tabelle unten | – | identity | RF-L, OY | – |
| 12–13 | Firmware version | u32, 12 das untere Wort; Version = Wert / 100000, 103555 = 1.03555 | – | identity | RF-L, OY | – |
| 14–15 | Serial number | u32, 14 das untere Wort | – | identity | RF-L, OY | – |
| 16 | mAh alarm limit | OY: Verbrauch, bei dem der ESC eine Telemetriewarnung auslöst | – | raw | OY | RF-L: unbenannt |
| 17 | Stick zero | 900–1900µs, Eingangspulsweite, die als Gas null gilt | 900–1900 | edit | RF-L, OY | – |
| 18 | Stick range | 600–1500µs, Spanne von null bis Vollgas | 600–1500 | edit | RF-L, OY | – |
| 19 | Stick period | OY: erwartete Frameperiode des Eingangs ° | – | raw | OY | RF-L: unbenannt |
| 20 | Motor pole pairs | RF-L: 1–100 Polpaare | – | raw | RF-L, OY | RF-L Polpaare, OY Pole: Faktor 2 auseinander |
| 21 | Pinion teeth | 1–255 | 1–255 | edit | RF-L, OY | – |
| 22 | Main gear teeth | 0–1800 | 0–1800 | edit | RF-L, OY | – |
| 23 | Minimum start power | 0–26% | 0–26 | edit | RF-L, OY | – |
| 24 | Maximum start power | 0–31% | 0–31 | edit | RF-L, OY | – |
| 25 | Telemetry type | OY: welches Telemetrieformat der ESC sendet °; M: YGE, Jeti EX Bus, HoTT V4 oder MSBv2, gewählt in YGEs PC-Programm | – | telemetry | OY, M | RF-L: unbenannt; die Codes sind nicht bekannt |
| 26 | Flags | unteres Byte: Bit 0 Drehrichtung (0 normal, 1 umgekehrt), Bit 1 F3C-Autorotation (0 aus, 1 an), Bit 2 Verbrauchszähler behalten, Bit 3 BEC bis 12V; Bits 4–7 und das obere Byte unbenannt | Bit 0 und Bit 1; jedes andere Bit wie gelesen | Bit 0 CHECK, Bit 1 edit, der Rest raw | RF-L, OY | OY: Bitfeld, Inhalt unbekannt |
| 27 | Current limit | Rohwert / 100 = A; 1–65500 = 0,01–655,00A | 1–65500 | CHECK | RF-L, OY, M | – |
| 28–29 | Soft start, soft run | OY: 28 Hochlaufrate aus dem Stand °, 29 Ratenbegrenzung für Gasänderungen im Betrieb ° | – | raw | OY | RF-L: ein unbenanntes u32 |
| 30–31 | Soft blend, RPM setpoint | OY: 30 Übergang von der Soft-Start-Rampe zum normalen Lauf °, 31 Sollwert des Governors ° | – | raw | OY | RF-L: ein unbenanntes u32 |

Hinweise je Index:

- 0: RF-F schreibt Index 0 nie.
- 1: RF-L markiert Modus 0 als Achtung. M führt die Modi 1 bis 6 für die
  Modusprogrammierung per Knüppel und nennt Modus 1 für einen externen
  Governor wie VBar.
- 2: M (Saphir V2, Aureus V2, 35–135 LVT): die Spannung des BEC (Battery
  Eliminator Circuit) wird in 0,1V-Schritten in YGEs PC-Programm
  eingestellt. Ein Wert über der Nennspannung der Servos beschädigt sie.
- 3: M: automatisches Timing oder 6 feste Stufen.
- 7: Wert 3 wählt eine Kurve, die nur YGEs PC-Programm einstellt.
- 9: M (ProgCard III): 2,9 bis 3,4V je Zelle für LiPo.
- 26: RF-L schreibt nur die Bits 0 und 1 und jedes andere Bit wie gelesen
  zurück. Bit 3 hebt das obere Ende von Index 2 von 84 auf 123.
- 27: RF-L ändert ihn in Schritten von 100, 1,00A. M (Opto 255):
  Strombegrenzung bis 400A.
- 21 und 22 ergeben mit der Motordrehzahl die Kopfdrehzahl (Abschnitt 4).

Die Kommentare der Referenz sind um 26–28 falsch nummeriert (drei
aufeinanderfolgende Einträge tragen die 26). RF-L legt die Flags auf 26
und die Strombegrenzung auf 27.

Bestätige die Indizes vor dem Schreiben auf einen echten ESC. Lies
zur Zuordnung zunächst alle Werte aus, ändere eine Einstellung mit
YGEs eigenem Werkzeug und lies die Werte erneut aus. Der geänderte
Index gehört zu dieser Einstellung (Schritt H5, Abschnitt 8).

#### ESC-Typcodes, Index 11

| Code | Modell |
| ---: | --- |
| 848 | 35 LVT BEC |
| 1616 | 65 LVT BEC |
| 2128 | 85 LVT BEC |
| 2384 | 95 LVT BEC |
| 4944 | 135 LVT BEC |
| 2304 | 90 HVT Opto |
| 4608 | 120 HVT Opto |
| 5712 | 165 HVT |
| 8272 | 205 HVT |
| 8273 | 205 HVT BEC |
| 4177 | Aureus 105 |
| 4179 | Aureus 105 v2 |
| 5025 | Aureus 135 |
| 5027 | Aureus 135 v2 |
| 4689 | Saphir 125 |
| 5457 | Saphir 155 |
| 5459 | Saphir 155 v2 |
| 4928 | Opto 135 |
| 9552 | Opto 255 |
| 16464 | Opto 405 |

Quelle: RF-L. Ein anderer Code wird als Zahl angezeigt. Hexadezimal
gelesen ergeben die oberen Ziffern bei 17 der 20 Codes den Nennstrom des
Modells, 848 = 0x350 für den 35 LVT BEC °.

---

## 6. `status1`

Dieses Byte enthält den Motorzustand und die Warnungen.

### Unteres Nibble: Motorzustand

| | | |
| ---: | --- | --- |
| 0x0 | `DISARMED` | steht |
| 0x1 | `POWER_CUT` | Leistung abgeschaltet; die Warnbits sagen, warum |
| 0x2 | `FAST_START` | Bailout |
| 0x4 | `ALIGN_FOR_POS` | positioniert |
| 0x6 | `BRAKING_NORM_FINI` | |
| 0x7 | `BRAKING_SYNC_FINI` | |
| 0x8 | `STARTING` | |
| 0x9 | `BRAKING_NORM` | |
| 0xA | `BRAKING_SYNC` | |
| 0xC | `WINDMILLING` | dreht, wird nicht getrieben |
| 0xE | `RUNNING_NORM` | läuft normal |

0x3, 0x5, 0xB, 0xD und 0xF sind reserviert. Ein unbekannter Zustand wird als
Zahl angezeigt.

### Oberes Nibble: Warnungen

| Maske | |
| --- | --- |
| 0x10 | Unterspannung |
| 0x20 | Übertemperatur |
| 0x40 | Überstrom |
| 0x80 | die Warnungen beziehen sich auf das BEC, nicht auf den ESC |

Die Kodierung ist doppelt belegt: `0x80 | 0x40` (BEC-Überstrom) kann nicht
vorkommen, und diese Kombination bedeutet Sollwertrauschen, ein gestörtes
Eingangssignal. Dekodiere in dieser Reihenfolge:

1. Oberes Nibble genau `0xC0`: Sollwertrauschen. Ende.
2. Andernfalls wählt Bit 0x80 die betroffene Komponente: gesetzt BEC,
   gelöscht ESC.
3. Die Bits 0x10 / 0x20 / 0x40 zeigen die Warnungen dieser Komponente.

### Warnungen im Kontext des Zustands

Ein Warnbit allein ist kein Fehler:

| Warnung | Ist ein Fehler, wenn |
| --- | --- |
| keine gesetzt | der Zustand `POWER_CUT` ist: Überspannung |
| Unterspannung | der Zustand unter `STARTING` liegt (< 0x08) |
| Übertemperatur | der Zustand `POWER_CUT` ist |
| Überstrom | der Zustand `POWER_CUT` ist |

Überspannung hat kein eigenes Bit; sie ist das Fehlen aller Warnungen bei
abgeschalteter Leistung.

---

## 7. Fehler im Referenzcode

An sechs Stellen weicht die Referenzimplementierung vom Protokoll ab.
Wer sie unverändert übernimmt, übernimmt auch diese Fehler.

| | |
| --- | --- |
| 1. Init gibt nichts zurück | deklariert mit einem Erfolgs-Flag als Rückgabewert, erreicht das Ende ohne `return` |
| 2. Eine übergroße Anfrage beschädigt das Senden | das Längenfeld wird vor der Größenprüfung gesetzt, die vorzeitig zurückkehrt; die nächste Sendung schickt alte Bytes über das Frameende hinaus |
| 3. Timing-Argumente werden ignoriert | Frameperiode und Timeout werden entgegengenommen und nie zugewiesen |
| 4. Die Sequenznummer liegt im Sendepuffer | dort hochgezählt, sodass ein vorzeitiges Zurückkehren (2) sie erhöht, ohne dass ein Frame die Leitung erreicht |
| 5. Struct-Cast über den Empfangspuffer | setzt Little-Endian, fehlendes Padding und ausgerichtete Zugriffe voraus |
| 6. Falsch nummerierte Parameterkommentare | drei aufeinanderfolgende Einträge tragen die 26 |

Die Referenz verwirft zur Resynchronisation einzelne Pufferbytes. Der
Dekoder in `protocols/openyge/` prüft jeden Sync-Kandidaten und übernimmt
den frühesten vollständigen Frame. So kann er einen Frame auch dann
erkennen, wenn davor Rauschen mit einem plausiblen Sync liegt.

---

## 8. Offene Messungen

Jede Unbekannte nennt den Hardwareschritt, der sie klärt. Die Schritte H3
bis H6 brauchen einen YGE-ESC mit OpenYGE-Firmware ab V1.03547, Telemetrietyp
YGE, einen Akku und keinen Propeller. H5 und H6 brauchen zusätzlich YGEs
USB-Adapter und PC-Programm.

| Schritt | Was er tut |
| --- | --- |
| H3 | nur lesen: Zeit vom Einschalten bis zum ersten Frame, Auto-Frames, 100 Anfragen, Schwellen-Sweep des Logikanalysators von 1,0 bis 3,0V, Adresse, Echo der Sequenznummer; Versorgungsstrom über 10s aus dem PD mini; Gaskabel angeschlossen und abgezogen |
| H4 | Index 21 (Ritzel, ohne Wirkung auf den Antrieb) um 1 erhöhen, lesen, aus- und einschalten, lesen, 20 Mal; jedes schreibbare Feld einmal auf Minimum und Maximum; Schreibvorgänge im Abstand von 4ms, 20ms und 50ms |
| H5 | in YGEs PC-Programm eine Einstellung ändern, die Tabelle am Prüfstand lesen, vergleichen; eine Modusprogrammierung per Knüppel ausführen, vergleichen |
| H6 | Logikanalysator am Telemetriekabel, während YGEs PC-Programm verbindet, liest und schreibt; am Gaskabel, während eine ProgCard liest und schreibt |

| # | Unbekannt | Geklärt durch |
| --- | --- | --- |
| U1 | Sendepegel des ESC (3,3V oder 5V), ein Pull-up auf der ESC-Seite, der Ruhepegel | H3, Schwellen-Sweep und Oszilloskop |
| U2 | Umschaltzeit vom Ende einer Anfrage bis zum ersten Antwortbyte und die Lücken zwischen Antwortbytes | H3, 100 Anfragen |
| U3 | die Adresse des ESC; ob Gerät 1 und Broadcast 0 beantwortet werden | H3 |
| U4 | ob `WRITE_PARAM_RESP` den geschriebenen Index und Wert in `pidx` und `pdata` trägt | H4 |
| U5 | wann ein Schreiben gespeichert wird und ob es Aus- und Einschalten übersteht | H4, 20 Zyklen |
| U6 | was der ESC mit einem Wert außerhalb des Bereichs tut | nicht gemessen: der Schreibschutz lehnt einen solchen Wert ab, er wird nie gesendet |
| U7 | die Bedeutungen von 4, 16, 19, 25, 26 oberes Byte und 28 bis 31 sowie die Widersprüche aus Abschnitt 5.3: Index 20 Polpaare oder Pole, Verstärkungen 1–10 oder 0–9, Timing-Codes 16–19, Freilaufwerte | H5 oder eine Auskunft von YGE |
| U8 | ob eine Modusprogrammierung per Knüppel den Telemetrietyp und andere Indizes zurücksetzt | H5 |
| U9 | ob ein angeschlossenes Gaskabel oder ein Gassignal beeinflusst, ob der ESC antwortet | H3, Variante mit Gaskabel |
| U10 | Zeit vom Einschalten bis zum ersten Frame; ob überhaupt `TELE_AUTO`-Frames kommen | H3 |
| U11 | Einschalt- und Anlaufstrom am PD mini; die niedrigste Spannung, bei der jedes Modell antwortet | H3, Stromaufzeichnung |
| U12 | Opto-Modelle: ob der Telemetrieanschluss Versorgung von der Empfängerseite braucht | H3 mit einem Opto-Modell |
| U13 | das Protokoll der ProgCard auf dem Gaskabel | H6 |
| U14 | ob YGEs PC-Programm OpenYGE-Frames spricht oder ein eigenes Protokoll | H6 |
| U15 | der kürzeste Abstand aufeinanderfolgender Schreibvorgänge; RF-F nutzt 4ms | H4 |
| U16 | ob der ESC `seq` zurückspiegelt, wie Abschnitt 2.1 angibt | H3 |

Prüfungen am Telemetrieframe, aus denselben Aufzeichnungen:

| # | Prüfung | Geklärt durch |
| --- | --- | --- |
| T1 | Skalierung von `rpm`, ×10 gegen ÷10 | ein Lauf mit bekanntem Motor, bekannten Polen und bekannter mechanischer Drehzahl |
| T2 | CRC-Seed: ein aufgezeichneter Frame verifiziert mit 0x0000 und schlägt mit 0xFFFF fehl; RF-F nutzt Seed 0 | Aufzeichnung aus H3 |
| T3 | ein v3-Telemetrieframe hat 34 Bytes, und `frame_length` zählt die CRC mit; RF-F zählt sie mit | Aufzeichnung aus H3 |
| T4 | ein ESC vor v3 lässt `seq` und `device` aus, seine Payload beginnt bei Offset 4 | Aufzeichnung von einem ESC vor v3 |
| T5 | `status2` und `reserved1`; ist `reserved1` ein High-Byte des Verbrauchs, bewegt es sich auf einem langen Lauf | Aufzeichnung über einen Lauf |

---

## 9. Stand in rcbench

Keine der beiden Platinen führt das Protokoll aus. Das IOmcu-Image
bindet das Modul ein, das Panel-Image nicht; keines ruft es auf. Der
Entwurf sieht die Verarbeitung auf dem IOmcu vor: eine
Anfrage-Antwort-Schleife mit 20Hz, Umschaltzeiten im Mikrosekundenbereich
und einen Timeout, der auch ohne eintreffende Bytes abläuft. Das Panel
würde das Ergebnis als `bench_state` über das Page-Protokoll erhalten.
OpenYGE-Frames blieben auf der IOmcu-Seite. Gaskommandos würden als
Schreibzugriffe auf die Control-Page übertragen und unterlägen dem
Heartbeat und dem Failsafe des IOmcu ([Sicherheit](Safety-de.md)).

Ein YGE-ESC meldet Spannung, Strom, Verbrauch, eRPM und vier Temperaturen
selbst, sodass jedes modellierte Feld von `bench_state` eine Quelle hat, bevor
der Stromsensor bestückt ist. Die Angaben des ESC und der Shunt des Prüfstands
sind unabhängige Messungen.

Gebaut, auf dem Host getestet, nicht eingebunden:

    protocols/openyge/
      openyge_frame.c        encode, decode and the CRC, openyge_crc()
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the parameter cache and its completeness rule
      openyge_map.c          the fields of section 5.3: access, writable values
      openyge_guard.c        the gate every outgoing frame passes
      openyge_session.c      boot wait, listen, takeover, read, write, verify
      openyge_file.c         the settings file: write, read, load check

Der Decoder zählt Frames, CRC-Fehler, Resyncs und getrennt davon Frames, deren
CRC stimmte, deren Aufbau aber unmöglich war. Nichts wird über den
Empfangspuffer gecastet. `openyge_params_get` gibt false zurück, bis jeder
Index eingetroffen ist, und ein anstehendes Schreiben zieht die ganze Tabelle
zurück.

Die Session ist der Kern der Programmierung für YGE-ESCs. Sie hält das
Timing aus Abschnitt 10 hinter `oy_driver_t` ein: einen Frame senden, die
empfangenen Bytes holen, eine Uhr lesen. Die Host-Suites lassen sie gegen
einen simulierten ESC laufen, der jeden Frame aufzeichnet. Jeder Frame
läuft direkt vor dem Treiber durch `oy_guard_frame()`. Nur `TELE_REQ` und
`WRITE_PARAM_REQ` gehen hinaus. Ein Schreiben braucht eine vollständig
gelesene Tabelle aus derselben Session, eine Anzahl von 32, einen v3-Header
und Firmware V1.03547 oder neuer. Schreibbar sind die Felder, die
Abschnitt 5.3 mit CHECK oder edit kennzeichnet, innerhalb ihrer
schreibbaren Werte. Ein Schreiben liest die Tabelle zuerst erneut und
bricht ohne gesendeten Frame ab, wenn sie abweicht. Nach den Schreibzugriffen
liest es die ganze Tabelle erneut und vergleicht sie mit dem Ziel.
Pin-Treiber, Link-Pages und Bildschirme sind nicht gebaut, daher führt
keine der beiden Platinen die Session aus.

### 9.1 Einstellungsdatei

`openyge_file.c` schreibt eine gelesene Tabelle in eine Textdatei, liest
sie zurück und hält sie gegen die zuletzt gelesene Tabelle. Das erste
Schreiben einer Session sichert die Tabelle nach
`BENCH/YGE/CONF/YGE001-<type>.TXT`, mit dem Typcode aus Index 11 im Namen
(`YGE001-5712.TXT`, Nummern 001 bis 999). Format 1:

    YGE ESC settings, format 1
    Type: 5712 165 HVT
    Firmware: 1.03555
    Serial: 4660
    Count: 32
    00 PARAMETER COUNT = 32
    01 ESC MODE = 2
    ...
    31 RPM SETPOINT = 0
    checksum 1A2B3C4D

- `Type` ist Index 11, mit dem Modellnamen aus Abschnitt 5.3, wenn der
  Code einen hat. `Firmware` ist Index 12-13 als u32 / 100000 mit 5
  Nachkommastellen. `Serial` ist Index 14-15 als u32. `Count` ist Index 0,
  16 bis 64.
- Eine Zeile je Index von 0 bis count − 1: 2 Ziffern, der Schlüssel des
  Felds aus der Map (`RAW` ab Index 32), ` = `, der Wert 0 bis 65535 dezimal.
- Zeichen 0x20 bis 0x7E, höchstens 64 je Zeile, LF (line feed) oder CR LF
  (carriage return, line feed) auf jeder Zeile, höchstens 4620B je Datei.
- Die Prüfsumme ist CRC-32 (IEEE 802.3: Polynom 0x04C11DB7 reflektiert,
  Start 0xFFFFFFFF, Endinversion) über alle Zeilen davor, jedes Zeilenende
  als ein LF gezählt, in 8 hexadezimalen Großbuchstaben-Ziffern.

Der Parser lehnt ab, mit festem Code und der Zeile: einen Text über 4620B
(2), einen anderen Kopf (3), eine Version außerhalb 1 bis 999 (4) oder
über 1 (5), einen Text ohne Prüfsummenzeile (6), LF und CR LF in einer
Datei (7), eine Zeile außer Form (8), eine Anzahl außerhalb 16 bis 64
oder eine andere Zahl von Indexzeilen (9), eine abweichende Prüfsumme
(10), eine Zeile hinter der Prüfsumme (11), einen Schlüssel, der nicht
der der Map ist, oder eine Kopfzeile, die von ihren Indizes abweicht (12),
und eine letzte Zeile `edited` (13). Eine geänderte Datei hat keine
Prüfsumme, die sie hält; daher lädt keine bearbeitete Datei.

Ein Laden übernimmt aus der Datei die Indizes, die die Map mit CHECK oder
edit kennzeichnet, von Index 26 nur Bit 0 und 1, und behält jeden anderen
Wert des ESC. Es lehnt eine Datei eines anderen Typcodes oder einer
anderen Anzahl ab, und einen Entwurf, den der Guard ablehnt: Der Entwurf
wird von `oy_guard_approve()` auf einer Kopie des Guards beurteilt, daher
gibt ein Laden nie einen Entwurf, den der Guard stoppen würde, und gibt
keinen frei.

Offene Entscheidung: welche ESC-Protokolle außer OpenYGE unterstützt werden.
Das Ausgangsmaterial steht neben Hobbywing, Kontronik, Scorpion, OMP, ZTW,
APD, FLY, Graupner und XDFly; mehrere teilen sich eine State Machine, keines
ein Frameformat. Erst ein Protokoll auf Hardware beweisen, dann ein zweites.

---

## 10. Session-Timing

Die Zeiten, die ein Master beim Pollen und Schreiben einhält. Die Werte
sind die Konstanten von RF-F. RF-F markiert beide Timeouts mit „zu
bestätigen“ und die Schreibperiode als vorläufig; U2, U10 und U15 aus
Abschnitt 8 klären sie. Die Spalte Session enthält die Werte, die
`openyge_session.c` verwendet, als benannte Konstanten in
`openyge_session.h`; ein als gewählt markierter Wert hat keine Quelle.

| Zeit | RF-F | Gezählt ab | Session |
| --- | ---: | --- | --- |
| Boot-Wartezeit | 5000ms | dem Start des Flugcontrollers; RF-F sieht das Einschalten des ESC nicht | 5000ms, nachdem die Versorgung als an gelesen wird; Einstellung 1000–20000ms |
| Auf den ersten Frame hören | – | – | bis 3000ms, gewählt |
| Erste Anfrage nach einem Auto-Frame | 20ms | dem Ende dieses Frames | – |
| Anfrageperiode | 38ms | dem Ende der vorigen Antwort bis zur nächsten Anfrage; etwa 50ms je Zyklus, 20Hz | 40ms, gewählt |
| Schreibperiode | 4ms | dem Ende der vorigen Antwort bis zur Schreibanfrage | ein Schreiben nach jeder Antwort, U15 |
| Lese-Timeout | 200ms | dem Senden der Anfrage; „zu bestätigen“ | 200ms |
| Schreib-Timeout | 400ms | dem Senden der Anfrage; „zu bestätigen“ | 400ms, 3 Versuche |
| Frame-Timeout im Auto-Modus | 900ms bis hinab zu 60ms | dem vorigen Frame; er schrumpft auf den gemessenen Frameabstand plus 10ms, nie unter 60ms | – |
| Tabelle lesen, auch nach einem Schreiben | – | – | bis 10000ms, gewählt |

So führt RF-F die Session:

- Es adressiert in jeder Anfrage Gerät 1 mit gesetztem Master-Bit, 0x81.
- Es erhöht `seq` einmal je neuer Anfrage. Ein Timeout sendet dieselbe
  Anfrage erneut mit derselben `seq`.
- Es verwirft eine Antwort, deren `seq` von der der Anfrage abweicht.
  `TELE_AUTO`-Frames sind ausgenommen.
- Es schreibt nur im Anfrage-Antwort-Modus. Nur empfangend verdrahtet hört
  es auf `TELE_AUTO`-Frames und schreibt nichts.
- Ein Speichern schreibt jeden Index ab 1, dessen Wert abweicht, einen je
  Anfrageslot in aufsteigender Reihenfolge, und kehrt dann zu
  Telemetrieanfragen zurück. Index 0 schreibt es nie.
- Vom Speichern bis zum Senden des letzten Schreibens übernimmt es keinen
  Wert in den Cache. Die Tabelle gibt es erst wieder frei, wenn jeder Index
  neu gelesen wurde.
- Ein ESC mit einem Header unter Version 3 schaltet es aufs Zuhören um.

Wo `openyge_session.c` es anders führt:

- Es übernimmt die Adresse aus dem ersten v3-Frame, den es beim Zuhören
  empfängt. Es nimmt 1, wenn kein Frame kommt oder der Frame 0 nennt.
- Jeder Versuch einer Anfrage oder eines Schreibens trägt eine neue `seq`.
  Eine Antwort, die kommt, nachdem ihre Anfrage erneut gesendet wurde,
  trägt die alte und wird verworfen.
- Eine Antwort zählt, wenn sie innerhalb des Timeouts ihres Sendens
  eintrifft. Drei Sendungen ohne Antwort sind ein Fehlschlag. Eine
  fehlgeschlagene Anfrage beendet die Session mit `SILENT`. Ein
  fehlgeschlagenes Schreiben beendet die Schreibzugriffe, und das Lesen der
  Tabelle danach entscheidet das Ergebnis.
- Es prüft eine Antwort gegen ihren Timeout, wenn es sie übernimmt, sodass
  ein verspäteter Schritt keine abgelaufene Antwort zählen lässt.
- Eine Antwort auf eine Anfrage, die vor Beginn eines Lesens oder
  Schreibens gesendet wurde, trägt keinen Wert in dieses Lesen.
- Eine Antwort, die bei oder nach der Lesegrenze von 10000ms übernommen
  wird, beendet das Lesen mit `NO_TABLE`, nach einem Schreiben mit
  `PARTIAL`, auch wenn sie die Tabelle vervollständigt.
- Nach der Übernahme verwirft es `TELE_AUTO`-Frames.
- Eine Anzahl von 0 oder über 64 beendet ein Lesen sofort mit `NO_TABLE`.
- Jedes Schreiben folgt 40ms nach der vorigen Antwort, der Anfrageperiode.
