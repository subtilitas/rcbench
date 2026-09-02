# Das OpenYGE-Telemetrie- und Parameterprotokoll

<sub>[English](OpenYGE.md) · **Deutsch**</sub>

Spezifikation des Protokolls: Frameformat, Telemetrie-Payload,
Parameterzugriff und Statuskodierung, mit der Arithmetik, die eine Umsetzung
braucht.

> **Maßgeblich ist die englische Seite.** Diese Übersetzung gibt jedes Feld,
> jeden Namen und jeden Wert unverändert wieder, aber die Spezifikation geht in
> dieser Form zu YGE zur Prüfung, und wer daraus implementiert, sollte im
> Zweifel dort nachsehen: zwei Fassungen eines Byte-Layouts können sich
> widersprechen, und nur eine kann recht haben.

> Die Implementierung wird in einem eigenen Repository verfolgt. Diese Seite
> ist die Spezifikation von Rang; sie geht zur Prüfung an YGE. Der Codec unter
> `shared/openyge/` ist gebaut und getestet, aber nicht in die Firmware
> eingebunden.

**° markiert eine erschlossene Bedeutung.** Diese Einträge stammen aus dem
Feldnamen und aus üblicher Praxis bei ESCs (Electronic Speed Controller,
Motorregler), nicht aus einer verbindlichen Quelle. Abschnitt 8 listet auf,
was eine Messung oder eine Antwort braucht. ° und Abschnitt 8 zusammen sind
alles, was auf dieser Seite unsicher ist.

## 1. Bitübertragungsschicht

| | |
| --- | --- |
| Signalisierung | UART (Universal Asynchronous Receiver-Transmitter), 115200 Baud, 8N1 |
| Bytereihenfolge | durchgehend Little-Endian |
| Topologie | Half Duplex, ein Master und bis zu 127 adressierbare ESCs |
| Rate | etwa 20 Hz Telemetrie |

Der Master hört seine eigene Sendung mit: ein Sender verwirft so viele
empfangene Bytes, wie er gesendet hat. Die Verbindung ist eine einzelne
Leitung mit zusammengeführtem TX (Sendeleitung) und RX (Empfangsleitung).

Das Protokoll braucht auf dem Koprozessor einen eigenen UART: einen
Hardware-UART mit über einen Widerstand verbundenem TX und RX und verworfenem
Echo, oder einen Soft-UART in PIO (Programmable Input/Output). Den Panel-Link
kann es nicht mitbenutzen; der ist CAN (Controller Area Network); siehe [Der
Link](Link-de.md).

Zeitbedarf bei 115200 8N1, 86,81 µs je Byte:

| | |
| --- | ---: |
| 34-Byte-Telemetrieantwort | 2,95 ms |
| 12-Byte-Anfrage | 1,04 ms |
| Ein Poll, beide Richtungen | 3,99 ms plus die Umschaltzeit des ESC |

Das sind 8 % der Leitung bei einem 50-ms-Zyklus.

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

`link_crc()` nimmt den Seed als erstes Argument, dieselbe Routine bedient also
beide.

---

## 3. Frametypen

| Wert | Name | Von | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | unaufgeforderte Telemetrie: ein ESC vor v3 immer, ein v3-ESC, bis der Master übernimmt |
| 0x02 | `TELE_RESP` | ESC | Telemetrie als Antwort auf eine Anfrage |
| 0x03 | `TELE_REQ` | Master | Telemetrie anfordern |
| 0x04 | `WRITE_PARAM_RESP` | ESC | quittiert ein Parameterschreiben; trägt zugleich Telemetrie |
| 0x05 | `WRITE_PARAM_REQ` | Master | einen Parameter schreiben |

Anfragen tragen die 4-Byte-Control-Payload aus Abschnitt 5.2. Jeder Frame vom
ESC zum Master trägt die vollständige Telemetrie-Payload aus Abschnitt 4, die
Quittung eines Parameterschreibens eingeschlossen.

Das Ausgangsmaterial gibt an, dass Bit 7 von `frame_type` ein Schreiben
markiert (0x81 für ein Parameter-Update). Keiner der gelisteten Werte benutzt
es. `frame_type` als schlichte Aufzählung behandeln und alles Ungelistete
abweisen; ob Bit 7 reserviert ist, ist eine Frage an YGE.

---

## 4. Telemetrie-Payload: 26 Bytes

Offsets innerhalb der Payload und absolut in einem Version-3-Frame, mit der
Umrechnung in SI-Einheiten (Internationales Einheitensystem). Ein
v3-Telemetrieframe hat 34 Bytes, ein Legacy-Frame 32.

| Payload | v3 abs | Größe | Feld | Einheit | nach SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C + 40 | `°C = v − 40`, −40…215 |
| 2 | 8 | u16 | `voltage` | 10 mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10 mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | unverändert |
| 8 | 14 | u16 | `rpm` | 10 eRPM (elektrische Umdrehungen pro Minute) | `eRPM = v × 10`, siehe unten |
| 10 | 16 | i8 | `pwm` | % | Ausgangstastgrad, vorzeichenbehaftet |
| 11 | 17 | i8 | `throttle` | % | Eingangssollwert, vorzeichenbehaftet |
| 12 | 18 | u16 | `bec_voltage` | mV | unverändert |
| 14 | 20 | u16 | `bec_current` | mA | unverändert |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | Abschnitt 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | Kondensatorpaket |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undokumentiert; mitführen, nicht interpretieren |
| 21 | 27 | u8 | `reserved1` | | möglicherweise ein High-Byte des Verbrauchs über 65 Ah ° |
| 22 | 28 | u16 | `pidx` | | Parameterindex, Abschnitt 5 |
| 24 | 30 | u16 | `pdata` | | Parameterwert, Abschnitt 5 |

Jedes 16-Bit-Feld landet bei beiden Headerlängen auf einem geraden Offset.
Darauf verlassen darf man sich nicht: Byte für Byte parsen. Einen Puffer auf
eine Struktur zu casten setzt Little-Endian und fehlendes Padding voraus und
führt unausgerichtete Zugriffe aus, die auf Architekturen mit strikter
Ausrichtung Undefined Behaviour sind.

### Skalierung von `rpm`

Das Feld ist als „0.1 eRPM" beschrieben, der Referenzcode multipliziert aber
mit zehn. Multiplizieren ist hier die Annahme: 65535 × 10 = 655 350 eRPM ist
eine plausible Obergrenze, 6 553 nicht. Messen (Abschnitt 8).

Die mechanische Drehzahl in RPM (Umdrehungen pro Minute) braucht die Polzahl,
Parameter 20:

    motor RPM = eRPM / (poles / 2)
    head RPM  = motor RPM × pinion teeth / main gear teeth

---

## 5. Parameter

Das Protokoll liest und schreibt die Konfiguration des ESC.

### 5.1 Übertragung: ein Parameter je Frame

Jeder Telemetrieframe trägt ein Paar `(pidx, pdata)`; der ESC geht seine
Tabelle selbst durch, der Master sammelt. Ein Master baut die Tabelle auf,
indem er jedes Paar zwischenspeichert, Parameter 0 (die Parameteranzahl)
liest und die Tabelle erst dann als vollständig behandelt, wenn jeder Index
`0 … count−1` gesehen wurde.

Bei 20 Hz und 32 Parametern dauert das etwa 1,6 s. Als Fortschritt anzeigen;
eine unvollständige Tabelle nie als die Einstellungen des ESC ausgeben.

- Solange Schreibvorgänge offen sind, keine zwischengespeicherten Werte mehr
  übernehmen, sonst überschreibt ein Frame, der schon unterwegs ist, den
  neuen Wert.
- Steht ein Schreiben an, die ganze Tabelle zurückziehen und erst wieder
  veröffentlichen, wenn jeder Index neu gelesen ist.

Der Cache in `shared/openyge/` ist durch eine 64-Bit-Bitmap auf 64 Parameter
begrenzt; die aktuelle Firmware hat 32.

### 5.2 Control-Payload: 4 Bytes

Getragen von `TELE_REQ` und `WRITE_PARAM_REQ`:

| Off | Größe | Feld |
| ---: | ---: | --- |
| 0 | u16 | `index` |
| 2 | u16 | `param`: zu schreibender Wert |

Eine reine Telemetrieanfrage sendet beide als null. Ein Anfrageframe hat
12 Bytes. Geschrieben wird ein Parameter je Frame, jeder mit eigener
Sequenznummer. Blockweises Schreiben gibt es nicht; ein „Speichern" ist eine
Warteschlange einzelner Schreibvorgänge.

### 5.3 Parametertabelle

Nach Funktion gruppiert. Die Indizes sind die auf der Leitung beobachteten;
° markiert eine aus dem Namen erschlossene Bedeutung.

Die Indizes vor dem Schreiben auf einen echten ESC bestätigen. Lesen ist
gefahrlos: alle Indizes lesen, eine Einstellung in YGEs eigenem Werkzeug
ändern, erneut lesen; der Index, der sich bewegt hat, ist der gemeinte.

Die Kommentare der Referenz sind um 26–28 falsch nummeriert (drei
aufeinanderfolgende Einträge tragen die 26), also ist mindestens einer der
drei in der Referenz falsch.

#### Identität, nur lesen

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 0 | Parameteranzahl | 32 ab Firmware v1.03503. Zuerst lesen; definiert die Tabelle |
| 11 | ESC type | Modellkennung, unteres Wort |
| 12–13 | Firmware version | unteres, oberes Wort |
| 14–15 | Serial number | unteres, oberes Wort |

#### Motor und Getriebe

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 20 | Motor pole count | Magnetpole; `motor RPM = eRPM / (poles / 2)` |
| 21 | Pinion teeth | |
| 22 | Main gear teeth | ergibt mit 21 die Kopfdrehzahl |
| 3 | Motor timing | Kommutierungsvorlauf. `0` automatisch; sonst `1` = 0° bis `6` = 30°, 6° je Stufe ° |

#### Governor

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 1 | Device mode | frei laufendes Gas, oder Governor aus internem oder externem Sollwert ° ; entscheidet, ob 5, 6 und 31 eine Wirkung haben |
| 5 | Governor P gain | 0–9 |
| 6 | Governor I gain | 0–9 |
| 31 | RPM setpoint | Sollwert des Governors ° |

#### Anlauf und Rampen

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 4 | Initial torque | Antrieb aus dem Stillstand ° |
| 23 | Minimum start power | untere Grenze der Anlauframpe ° |
| 24 | Maximum start power | obere Grenze der Anlauframpe ° |
| 28 | Soft start | Hochlaufrate aus dem Stand ° |
| 29 | Soft run | Ratenbegrenzung für Gasänderungen im Betrieb ° |
| 30 | Soft blend | Übergang zwischen Soft-Start-Rampe und normalem Lauf ° |
| 7 | Throttle response | slow / medium / high / custom |
| 10 | Freewheel demand | aktiver Freilauf (komplementäre PWM (Pulsweitenmodulation)) an oder aus ° |

#### Schutz und Grenzen

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 8 | Cut-off type | an der Unterspannungsgrenze: `0` nichts, `1` Leistung allmählich reduzieren, `2` Leistung abschalten |
| 9 | Cut-off voltage per cell | Offset mit `0` = 2,9 V; die Schrittweite ist nicht angegeben, 0,1 V je Zähler ist die Annahme ° |
| 27 | Current limit | ° |
| 16 | mAh alarm limit | Verbrauch, bei dem der ESC eine Telemetriewarnung auslöst |

#### BEC

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 2 | BEC voltage | geregelte Ausgangsspannung des BEC (Battery Eliminator Circuit) zu Empfänger und Servos, in 0,1 V. Ein Wert über der Nennspannung der Servos beschädigt sie |

#### Kalibrierung des Eingangssignals

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 17 | `STK_ZERO` | Eingangspulsweite, die als Gas null gilt ° |
| 18 | `STK_RANGE` | Spanne von null bis Vollgas ° |
| 19 | `STK_PERIOD` | erwartete Frameperiode des Eingangs ° |

Die Einheiten sind vermutlich Mikrosekunden ° .

#### Telemetrie und Flags

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 25 | Telemetry type | welches Telemetrieformat der ESC ausgibt ° ; ein ESC mit einem anderen Format antwortet auf dieses Protokoll nicht |
| 26 | Flags | Bitfeld, Inhalt unbekannt; mitführen, nicht interpretieren |

---

## 6. `status1`

Ein Byte mit einem Motorzustand und einem Satz Warnungen.

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
Eingangssignal. In dieser Reihenfolge decodieren:

1. Oberes Nibble genau `0xC0`: Sollwertrauschen. Ende.
2. Sonst wählt Bit 0x80 das Subjekt: gesetzt BEC, gelöscht ESC.
3. Die Bits 0x10 / 0x20 / 0x40 sind die Warnungen dieses Subjekts.

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

Sechs Stellen, an denen Referenzimplementierung und Protokoll
auseinandergehen. Eine daraus abgeschriebene Umsetzung erbt alle sechs.

| | |
| --- | --- |
| 1. Init gibt nichts zurück | deklariert mit einem Erfolgs-Flag als Rückgabewert, erreicht das Ende ohne `return` |
| 2. Eine übergroße Anfrage beschädigt das Senden | das Längenfeld wird vor der Größenprüfung gesetzt, die vorzeitig zurückkehrt; die nächste Sendung schickt alte Bytes über das Frameende hinaus |
| 3. Timing-Argumente werden ignoriert | Frameperiode und Timeout werden entgegengenommen und nie zugewiesen |
| 4. Die Sequenznummer liegt im Sendepuffer | dort hochgezählt, sodass ein vorzeitiges Zurückkehren (2) sie erhöht, ohne dass ein Frame die Leitung erreicht |
| 5. Struct-Cast über den Empfangspuffer | setzt Little-Endian, fehlendes Padding und ausgerichtete Zugriffe voraus |
| 6. Falsch nummerierte Parameterkommentare | drei aufeinanderfolgende Einträge tragen die 26 |

Die Referenz synchronisiert sich neu, indem sie ihren Puffer Byte für Byte
verwirft. Der Decoder in `shared/openyge/` prüft jeden Sync-Kandidaten und
nimmt den frühesten vollständigen Frame; so wird ein Frame zurückgewonnen, der
hinter Rauschen mit einem plausiblen Sync ankommt.

---

## 8. Was zu messen ist, bevor diese Seite als gesichert gilt

1. Skalierung von `rpm`: bekannter Motor, bekannte Polzahl, bekannte
   mechanische Drehzahl. Entscheidet ×10 gegen ÷10.
2. CRC-Seed: einen Frame aufzeichnen; bestätigen, dass er mit 0x0000
   verifiziert und mit 0xFFFF fehlschlägt.
3. Framelänge: bestätigen, dass ein v3-Telemetrieframe 34 Bytes hat und
   `frame_length` die CRC mitzählt.
4. Legacy-Header: bestätigen, dass ein ESC vor v3 `seq` und `device` auslässt
   und die Payload bei Offset 4 beginnt.
5. Umschaltzeit: die Lücke zwischen dem letzten Byte einer Anfrage und dem
   ersten Byte der Antwort. Bestimmt das Timing der Session-Schicht.
6. Parameterindizes, mit YGE oder per Lesen-Ändern-Lesen gegen deren eigenes
   Werkzeug, vor jedem Schreiben. Abschnitt 5.3, und die Schrittweite der
   Abschaltspannung mit ihnen.
7. `status2` und `reserved1`: beide über eine Sitzung mitschreiben; ist
   `reserved1` ein High-Byte des Verbrauchs, bewegt es sich auf einem langen
   Lauf.

---

## 9. Stand in rcbench

Das Protokoll läuft auf dem Koprozessor: eine Anfrage-Antwort-Schleife mit
20 Hz, einer Umschaltzeit im Mikrosekundenbereich und einem Timeout, das
feuert, ob Bytes kommen oder nicht. Das Panel sieht das Ergebnis als
`bench_state` über das Page-Protokoll; kein OpenYGE-Frame überquert den
Panel-Link. Ein Gaskommando reist als Schreiben auf die Control-Page hinter
dem Heartbeat und dem Failsafe des Koprozessors ([Sicherheit](Safety-de.md)).

Ein YGE-ESC meldet Spannung, Strom, Verbrauch, eRPM und vier Temperaturen
selbst, sodass jedes modellierte Feld von `bench_state` eine Quelle hat, bevor
der Stromsensor bestückt ist. Die Angaben des ESC und der Shunt des Prüfstands
sind unabhängige Messungen.

Gebaut, auf dem Host getestet, nicht eingebunden:

    shared/openyge/
      openyge_frame.c        encode and decode, reusing link_crc with seed 0
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the parameter cache and its completeness rule

Der Decoder zählt Frames, CRC-Fehler, Resyncs und getrennt davon Frames, deren
CRC stimmte, deren Aufbau aber unmöglich war. Nichts wird über den
Empfangspuffer gecastet. `openyge_params_get` gibt false zurück, bis jeder
Index eingetroffen ist, und ein anstehendes Schreiben zieht die ganze Tabelle
zurück.

Nicht gebaut: `openyge_session.c`, die Anfrage-Antwort-State-Machine (Pollen
im Frametakt, Abgleich der Sequenznummer, Lese- und Schreibtimeouts, Rückfall
aufs Zuhören bei einem ESC vor v3). Sie braucht zuerst die Messung der
Umschaltzeit aus Abschnitt 8. Die Implementierung wird in einem eigenen
Repository verfolgt.

Offene Entscheidung: welche ESC-Protokolle außer OpenYGE unterstützt werden.
Das Ausgangsmaterial steht neben Hobbywing, Kontronik, Scorpion, OMP, ZTW,
APD, FLY, Graupner und XDFly; mehrere teilen sich eine State Machine, keines
ein Frameformat. Erst ein Protokoll auf Hardware beweisen, dann ein zweites.
