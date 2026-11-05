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

> Die Implementierung wird in einem separaten Repository entwickelt.
> Diese Seite gehört zur maßgeblichen Spezifikation für die Prüfung
> durch YGE. Der Codec unter `protocols/openyge/` ist gebaut und getestet,
> aber nicht an die Firmware angebunden.

**° markiert eine erschlossene Bedeutung.** Diese Einträge beruhen auf
Feldnamen und üblicher Praxis bei ESCs (Electronic Speed Controller,
Motorregler), nicht auf einer verbindlichen Quelle. Abschnitt 8 nennt
die Punkte, die durch Messungen oder Auskünfte geklärt werden müssen.
Die Markierung ° und Abschnitt 8 kennzeichnen zusammen alle unsicheren
Angaben dieser Seite.

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
eine plausible Obergrenze, 6 553 nicht. Messen (Abschnitt 8).

Die mechanische Drehzahl in RPM (Umdrehungen pro Minute) braucht die Polzahl,
Parameter 20:

    motor RPM = eRPM / (poles / 2)
    head RPM  = motor RPM × pinion teeth / main gear teeth

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

Nach Funktion gruppiert. Die Indizes sind die auf der Leitung beobachteten;
° markiert eine aus dem Namen erschlossene Bedeutung.

Bestätige die Indizes vor dem Schreiben auf einen echten ESC. Lies
zur Zuordnung zunächst alle Werte aus, ändere eine Einstellung mit
YGEs eigenem Werkzeug und lies die Werte erneut aus. Der geänderte
Index gehört zu dieser Einstellung.

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
| 9 | Cut-off voltage per cell | Offset mit `0` = 2,9V; die Schrittweite ist nicht angegeben, 0,1V je Zähler ist die Annahme ° |
| 27 | Current limit | ° |
| 16 | mAh alarm limit | Verbrauch, bei dem der ESC eine Telemetriewarnung auslöst |

#### BEC

| # | Einstellung | Bedeutung |
| ---: | --- | --- |
| 2 | BEC voltage | geregelte Ausgangsspannung des BEC (Battery Eliminator Circuit) zu Empfänger und Servos, in 0,1V. Ein Wert über der Nennspannung der Servos beschädigt sie |

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
