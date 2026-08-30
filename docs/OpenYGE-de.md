# Das OpenYGE-Telemetrie- und Parameterprotokoll

<sub>[English](OpenYGE.md) · **Deutsch**</sub>

Eine schriftliche Spezifikation des Protokolls: Frameformat, Telemetrie-Payload,
Parameterzugriff und die Statuskodierung — mit der Arithmetik, die man zur
Umsetzung braucht.

> **Maßgeblich ist die englische Seite.** Diese Übersetzung gibt jedes Feld,
> jeden Namen und jeden Wert unverändert wieder, aber die Spezifikation geht in
> dieser Form zu YGE zur Prüfung, und wer daraus implementiert, sollte im
> Zweifel dort nachsehen: zwei Fassungen eines Byte-Layouts können sich
> widersprechen, und nur eine kann recht haben.

> **Die Implementierung liegt in einem eigenen Repository.** Diese Seite bleibt
> hier, weil sie die Spezifikation von Rang ist, weil sie zur Prüfung zu YGE
> geht, und weil der CRC-Seed und die Decoderform, die sie festlegt, mit dem
> eigenen Link dieses Projekts geteilt werden. Der Codec unter
> `shared/openyge/` ruht.

**Mit ° markiert = hergeleitet, nicht angegeben.** Diese Bedeutungen stammen aus
einem Feldnamen und gewöhnlicher ESC-Praxis und nicht aus etwas Autoritativem,
und sie sind die Teile, die am meisten eine Korrektur verdienen. Abschnitt 8
listet auf, was noch eine Messung oder eine Antwort braucht; zusammen sind ° und
Abschnitt 8 alles, was hier unsicher ist.

## 1. Bitübertragungsschicht

| | |
| --- | --- |
| Signalisierung | UART, **115200 Baud, 8N1** |
| Bytereihenfolge | durchgehend **Little-Endian** |
| Topologie | Half Duplex, ein Master und bis zu 127 adressierbare ESCs |
| Rate | ~20 Hz Telemetrie |

Der Master hört seine eigene Sendung — ein Sender verwirft genau so viele
empfangene Bytes, wie er gerade gesendet hat —, die Verbindung ist also eine
**einzelne Leitung** mit gemeinsamem TX und RX.

**Das braucht einen eigenen UART.** Der Panel-Link von rcbench ist
[CAN](Link-de.md) und trägt sonst nichts; OpenYGE ist ein asynchroner Bytestrom
mit 115200 und kann ihn nicht mitbenutzen. (Als der Panel-Link noch RS485 war,
gab es einen zweiten Grund — seine Auto-Direction-Schaltung hatte eine
Baud-*Untergrenze* nahe 125 kBaud, unter der 115200 liegt —, aber diesen
Transceiver gibt es nicht mehr.) Also: ein eigener Half-Duplex-UART auf dem
Koprozessor. Entweder ein Hardware-UART mit TX und RX über einen Widerstand
verbunden und verworfenem Echo, oder ein PIO-Soft-UART. Der Koprozessor plant
ohnehin PIO-UARTs für den Empfängerbus-Analyser, der zweite Weg kostet also eine
State Machine von zwölf und gibt sauberere Kontrolle über die Umschaltung.

Timing bei 115200 8N1 — 86,81 µs je Byte:

| | |
| --- | ---: |
| 34-Byte-Telemetrieantwort | 2,95 ms |
| 12-Byte-Anfrage | 1,04 ms |
| Ein Poll, beide Richtungen | **3,99 ms** plus die Umschaltzeit des ESC |

8 % der Leitung gegen einen 50-ms-Zyklus. Platz für einen zweiten ESC an einem
eigenen UART, oder für Parameterverkehr zwischen der Telemetrie.

---

## 2. Frameaufbau

Jeder Frame ist `header · payload · CRC`. `frame_length` zählt **den ganzen
Frame, Header und CRC eingeschlossen**.

### 2.1 Header, Protokollversion 3 und neuer — 6 Bytes

| Off | Feld | |
| ---: | --- | --- |
| 0 | `sync` | immer **0xA5** |
| 1 | `version` | 3 für das aktuelle Protokoll |
| 2 | `frame_type` | Abschnitt 3 |
| 3 | `frame_length` | gesamt, Header und CRC eingeschlossen |
| 4 | `seq` | der Master zählt je Anfrage hoch; der ESC **spiegelt ihn zurück** |
| 5 | `device` | Bit 7 (**0x80**) gesetzt = Sender ist der Master. Bits 0–6 sind die ESC-Adresse **0x01–0x7F**; **0x00 adressiert alle ESCs** |

### 2.2 Legacy-Header, Version unter 3 — 4 Bytes

Nur `sync`, `version`, `frame_type`, `frame_length`. Ohne Sequenznummer und ohne
Adresse **kann ein ESC vor v3 weder gepollt noch adressiert werden**: er sendet
Telemetrie und das war es. Parameterzugriff gibt es unterhalb v3 nicht.

Eine Umsetzung muss deshalb `version` lesen, bevor sie weiß, wo die Payload
beginnt.

### 2.3 CRC — 2 Bytes, Little-Endian, am Ende

Über **jedes Byte von `sync` bis ausschließlich der CRC**.

> **Der Algorithmus ist CRC-16/XMODEM, nicht CRC-16/CCITT-FALSE**, wie immer er
> in einer bestimmten Umsetzung heißen mag. Gleiches Polynom, anderer Seed, und
> der Unterschied ist jedes Byte jedes Frames:
>
> | | Poly | Init | Refl. | XorOut | Check über `"123456789"` |
> | --- | --- | --- | --- | --- | --- |
> | **OpenYGE** | 0x1021 | **0x0000** | keine | keiner | **0x31C3** |
> | rcbenchs eigener Link | 0x1021 | 0xFFFF | keine | keiner | 0x29B1 |
>
> Beide Prüfwerte wurden zur Bestätigung berechnet.

rcbenchs `link_crc()` nimmt den Seed als erstes Argument, die vorhandene,
getestete Routine bedient also beide Links unverändert — `0x0000` hier,
`LINK_CRC_INIT` für den Panel-Link. Ein neuer Vektor, kein neuer Code.

---

## 3. Frametypen

| Wert | Name | Von | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | unaufgeforderte Telemetrie. Was ein ESC vor v3 tut, und was ein v3-ESC tut, bis der Master übernimmt |
| 0x02 | `TELE_RESP` | ESC | Telemetrie als Antwort auf eine Anfrage |
| 0x03 | `TELE_REQ` | Master | Telemetrie anfordern |
| 0x04 | `WRITE_PARAM_RESP` | ESC | quittiert ein Parameterschreiben; trägt zugleich Telemetrie |
| 0x05 | `WRITE_PARAM_REQ` | Master | einen Parameter schreiben |

Anfragen tragen die 4-Byte-Control-Payload aus Abschnitt 5.2; jeder Frame vom
ESC zum Master trägt die vollständige Telemetrie-Payload aus Abschnitt 4, die
Parameterschreib-Quittung eingeschlossen.

Das Material behauptet, Bit 7 von `frame_type` markiere ein Schreiben — 0x81 für
ein Parameter-Update. **Kein Wert oben benutzt es, und nichts testet es.**
`frame_type` als schlichte Aufzählung behandeln und alles Ungelistete abweisen.
Es lohnt sich, YGE zu fragen, ob Bit 7 für etwas noch nicht Genutztes reserviert
ist — denn ein Decoder, der es heute abweist, weist es auch morgen ab.

---

## 4. Telemetrie-Payload — 26 Bytes

Offsets innerhalb der Payload und absolut in einem Version-3-Frame. Ein
v3-Telemetrieframe ist **34 Bytes**, ein Legacy-Frame 32.

| Payload | v3 abs | Größe | Feld | Einheit | nach SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C **+ 40** | `°C = v − 40`, −40…215 |
| 2 | 8 | u16 | `voltage` | 10 mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10 mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | unverändert |
| 8 | 14 | u16 | `rpm` | **10 eRPM** | `eRPM = v × 10` — siehe unten |
| 10 | 16 | i8 | `pwm` | % | Ausgangstastgrad, **vorzeichenbehaftet** |
| 11 | 17 | i8 | `throttle` | % | Eingangssollwert, **vorzeichenbehaftet** |
| 12 | 18 | u16 | `bec_voltage` | mV | unverändert |
| 14 | 20 | u16 | `bec_current` | mA | unverändert |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | Abschnitt 6 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | Kondensatorpaket |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undokumentiert; mitführen, nicht interpretieren |
| 21 | 27 | u8 | `reserved1` | | möglicherweise ein High-Byte für Verbrauch über 65 Ah ° |
| 22 | 28 | u16 | `pidx` | | Parameterindex — Abschnitt 5 |
| 24 | 30 | u16 | `pdata` | | Parameterwert — Abschnitt 5 |

Jedes 16-Bit-Feld landet zufällig bei beiden Headerlängen auf einem geraden
Offset, eine gewöhnliche ABI legt das also ohne Padding aus. **Verlass dich
nicht darauf.** Byte für Byte parsen: einen Puffer auf eine Struktur zu casten
setzt Little-Endian voraus, setzt kein Padding voraus und führt nicht
ausgerichtete Zugriffe aus, die auf Zielen mit strikter Ausrichtung Undefined
Behaviour sind. Der Link-Decoder von rcbench parst bereits byteweise.

### `rpm` — der eine echte Widerspruch

Das Feld wird als „0.1 eRPM" beschrieben, während der Code mit zehn
multipliziert. Beides kann nicht stimmen. Multiplizieren ist richtig: es ist
das, was die meiste ESC-Telemetrie sendet, und 65535 × 10 = 655 350 eRPM ist
eine plausible Obergrenze, wo 6 553 es nicht wäre.

**Trotzdem messen** — ein bekannter Motor bei bekannter Drehzahl, ein Messwert.
Das ist der Unterschied zwischen einem Drehzahlmesser und einer Zierde.

Mechanische Drehzahl braucht die Polzahl, Parameter 20:

    motor RPM = eRPM / (poles / 2)
    head RPM  = motor RPM × pinion teeth / main gear teeth

---

## 5. Parameter

Die Hälfte dieses Protokolls, die es zu mehr als einem Telemetriestrom macht:
**es liest und schreibt die Konfiguration des ESC**, und das ist
ESC-Programmierung über eine dokumentierte, nicht proprietäre Schnittstelle.

### 5.1 Wie sie ankommen: tropfenweise, einer je Frame

Jeder Telemetrieframe trägt **ein** Paar `(pidx, pdata)`; der ESC läuft seine
eigene Tabelle ab, und der Master sammelt ein. Ein Master baut die Tabelle, indem
er jedes Paar zwischenspeichert, **Parameter 0, die Parameteranzahl**, liest und
die Tabelle erst dann als vollständig behandelt, wenn jeder Index `0 … count−1`
gesehen wurde.

Bei 20 Hz und 32 Parametern sind das etwa **1,6 Sekunden**. Als Fortschritt
zeigen; eine halb gelesene Tabelle nie als die Einstellungen des ESC ausgeben.

Zwei Regeln, die es zu behalten lohnt:

- **Keine zwischengespeicherten Werte mehr annehmen, solange Schreibvorgänge
  offen sind** — sonst landet ein bereits unterwegs befindlicher Frame auf einem
  neuen Wert, und der ESC sieht aus, als hätte er das Schreiben ignoriert.
- **Die ganze Tabelle zurückziehen, sobald ein Schreiben ansteht**, und erst
  wieder veröffentlichen, wenn jeder Index neu gelesen ist. Teils alt, teils neu
  ist schlimmer als gar nichts.

Der Cache ist durch eine 64-Bit-Bitmap auf **64 Parameter** begrenzt. 32 ist die
heutige Wirklichkeit.

### 5.2 Control-Payload — 4 Bytes

Getragen von `TELE_REQ` und `WRITE_PARAM_REQ`:

| Off | Größe | Feld |
| ---: | ---: | --- |
| 0 | u16 | `index` |
| 2 | u16 | `param` — zu schreibender Wert |

Eine reine Telemetrieanfrage sendet beides als null. Ein Anfrageframe ist **12
Bytes**. Schreibvorgänge sind **ein Parameter je Frame**, jeder mit eigener
Sequenznummer. Es gibt kein seitenweises Schreiben; ein „Speichern" ist eine
Warteschlange einzelner Schreibvorgänge.

### 5.3 Die Parametertabelle

Nach Funktion gruppiert. Die Indizes sind die, die auf die Leitung gehen, und wie
beobachtet; **°** markiert eine aus dem Namen hergeleitete Bedeutung statt einer
angegebenen.

**Diese bestätigen, bevor irgendeiner davon auf einen echten ESC geschrieben
wird.** Ein falscher Index in die Konfiguration eines ESC ist kein umkehrbares
Experiment. Lesen ist gefahrlos und beantwortet das meiste von selbst: jeden
Index lesen, eine Einstellung in YGEs eigenem Werkzeug ändern, erneut lesen —
und der Index, der sich bewegt hat, ist der, der es bedeutet.

Die Kommentare der Referenz sind um 26–28 herum falsch nummeriert — drei
aufeinanderfolgende Einträge alle mit 26 beschriftet —, mindestens einer dieser
drei steht also falsch auf der Seite, selbst wenn die Werte stimmen.

#### Identität — nur lesen

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 0 | **Parameteranzahl** | 32 ab Firmware v1.03503. Zuerst lesen; sie definiert die Tabelle |
| 11 | ESC type | Modellkennung, unteres Wort |
| 12–13 | Firmware version | unteres, oberes Wort |
| 14–15 | Serial number | unteres, oberes Wort |

#### Motor und Getriebe

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 20 | **Motor pole count** | Magnetpole. Für die mechanische Drehzahl nötig: `motor RPM = eRPM / (poles / 2)` |
| 21 | Pinion teeth | Motorritzel |
| 22 | Main gear teeth | zusammen mit 21 ergibt sich die Kopfdrehzahl. Ein Prüfstand, der diese liest, meldet die Kopfdrehzahl, ohne dass man ihm etwas sagt |
| 3 | **Motor timing** | Kommutierungsvorlauf. `0` = automatisch; sonst `1` = 0° bis `6` = 30°, also **6° je Stufe** ° . Mehr Vorlauf bringt Drehzahl und Leistung auf Kosten von Wärme, Wirkungsgrad und Reserve gegen Desync. Automatisch passt sich dem Motor an und ist die vernünftige Vorgabe |

#### Governor — Kopfdrehzahlregelung

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 1 | **Device mode** | welche Aufgabe der ESC erfüllt: frei laufendes Gas für ein Flächenmodell, oder ein Governor, der eine konstante Kopfdrehzahl hält, aus eigenem Sollwert oder aus einem externen ° . Das entscheidet, ob die Parameter 5, 6 und 31 überhaupt etwas tun |
| 5 | Governor P gain | Proportionalanteil, 0–9. Wie hart der Governor einen Kopfdrehzahlfehler korrigiert. Zu niedrig, und der Kopf sackt weg, wenn das Pitch ihn belastet; zu hoch, und er pendelt hörbar |
| 6 | Governor I gain | Integralanteil, 0–9. Beseitigt die bleibende Abweichung, die P allein lässt. Zu hoch ergibt eine langsame Schwingung, die die P-Verstärkung nicht heilt |
| 31 | RPM setpoint | Zielkopfdrehzahl für den Governor ° |

#### Anlauf und Rampen

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 4 | Initial torque | wie kräftig der Motor aus dem Stillstand getrieben wird ° . Ein Kopf mit hoher Trägheit will mehr; zu viel riskiert Synchronverlust in dem Moment, in dem am wenigsten Gegen-EMK da ist, auf die man synchronisieren könnte |
| 23 | Minimum start power | untere Grenze der Anlauframpe ° |
| 24 | Maximum start power | obere Grenze der Anlauframpe ° . Die beiden klammern ein, wie hart der ESC beim Hochlaufen drücken darf |
| 28 | Soft start | Hochlaufrate aus dem Stand ° . Der Parameter, der einen Helikopter davon abhält, bei jedem Start seinen Antriebsstrang zu reißen |
| 29 | Soft run | Ratenbegrenzung für Gasänderungen im Betrieb ° |
| 30 | Soft blend | Übergang zwischen Soft-Start-Rampe und normalem Lauf ° . Einer der drei, deren Bestätigung sich am meisten lohnt, denn ein falscher Wert hier ist bei jedem Hochlauf zu spüren |
| 7 | Throttle response | slow / medium / high / custom. Wie zügig der Ausgang dem Eingang folgt. Slow schont den Antriebsstrang; high ist bissiger und härter zu ihm |
| 10 | Freewheel demand | ob der ESC die Low Side während der PWM-Auszeit aktiv treibt — „active freewheeling" oder „complementary PWM" ° . An gibt besseren Wirkungsgrad und bessere Linearität im Teillastbereich und echtes Bremsen; aus lässt den Motor austrudeln. An einem Helikopter ändert es das Verhalten des Kopfes in der Autorotation, und deshalb ist es eine Einstellung und keine Konstante |

#### Schutz und Grenzen

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 8 | **Cut-off type** | was an der Unterspannungsgrenze passiert. `0` = nichts, der Pilot entscheidet; `1` = langsamer werden, Leistung wird allmählich zurückgenommen, sodass die Kontrolle bleibt — die einzige vernünftige Wahl in der Luft; `2` = abschalten, die Leistung endet. `2` gehört an ein Boot oder ein Auto |
| 9 | Cut-off voltage per cell | Schwelle, die obiges auslöst, als Offset kodiert, wobei `0` = 2,9 V. **Die Schrittweite ist nicht angegeben** — 0,1 V je Zähler ist die naheliegende Vermutung ° und eine Messung wert, denn eine Stufe daneben ist ein leergeflogener Akku oder eine zu früh beendete Landung |
| 27 | Current limit | die eigene Stromobergrenze des ESC ° |
| 16 | mAh alarm limit | Verbrauch, bei dem der ESC eine Telemetriewarnung auslöst |

#### BEC

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 2 | **BEC voltage** | geregelte Ausgangsspannung zu Empfänger und Servos, in **0,1 V**. Höher gesetzt, als die Servos vertragen, zerstört sie diese leise — was das zur einzigen gefährlichsten Zahl der Tabelle macht, die man falsch schreiben kann |

#### Kalibrierung des Eingangssignals

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 17 | `STK_ZERO` | Eingangspulsweite, die der ESC als Gas null behandelt ° |
| 18 | `STK_RANGE` | Spanne von null bis Vollgas ° |
| 19 | `STK_PERIOD` | erwartete Frameperiode des Eingangs, also die Signalrate ° |

Zusammen sind diese drei die Kalibrierung des Gassignals — das Gegenstück zum
Anlernen des Knüppelbereichs, das die meisten ESCs beim Einschalten mit Piepsen
erledigen. Die Einheiten sind vermutlich Mikrosekunden ° .

#### Telemetrie und Flags

| # | Einstellung | Was sie tut |
| ---: | --- | --- |
| 25 | Telemetry type | welches Telemetrieformat der ESC ausgibt ° . Ein ESC, der auf etwas anderes als OpenYGE eingestellt ist, antwortet auf dieses Protokoll gar nicht — es ist also das Erste, was man liest, wenn ein angeblich unterstützter ESC stumm bleibt |
| 26 | Flags | Bitfeld, Inhalt unbekannt. Mitführen, nicht interpretieren |

---

## 6. `status1`

Ein Byte, das zwei unzusammenhängende Dinge trägt.

### Unteres Nibble — Motorzustand

| | | |
| ---: | --- | --- |
| 0x0 | `DISARMED` | steht |
| 0x1 | `POWER_CUT` | Leistung abgeschaltet; die Warnbits sagen, warum |
| 0x2 | `FAST_START` | „Bailout" |
| 0x4 | `ALIGN_FOR_POS` | positioniert |
| 0x6 | `BRAKING_NORM_FINI` | |
| 0x7 | `BRAKING_SYNC_FINI` | |
| 0x8 | `STARTING` | |
| 0x9 | `BRAKING_NORM` | |
| 0xA | `BRAKING_SYNC` | |
| 0xC | `WINDMILLING` | dreht sich, wird nicht getrieben — Leerlauf |
| 0xE | `RUNNING_NORM` | läuft normal |

0x3, 0x5, 0xB, 0xD und 0xF sind reserviert. Einen unbekannten Zustand als seine
Zahl anzeigen; ihn nicht auf einen Nachbarn abbilden.

### Oberes Nibble — Warnungen, und die Falle darin

| Maske | |
| --- | --- |
| 0x10 | Unterspannung |
| 0x20 | Übertemperatur |
| 0x40 | Überstrom |
| 0x80 | diese Warnungen beziehen sich auf das **BEC**, nicht auf den ESC |

**Die Kodierung ist überladen.** `0x80 | 0x40` — „BEC-Überstrom" — kann nicht
auftreten, genau diese Kombination wird also für **Sollwertrauschen**
wiederverwendet: der ESC meldet, dass sein *Eingangssignal* schmutzig ist. In
dieser Reihenfolge decodieren:

1. Oberes Nibble genau `0xC0` → **Sollwertrauschen**. Ende.
2. Sonst wählt Bit 0x80 das Subjekt: gesetzt → BEC, gelöscht → ESC.
3. Die Bits 0x10 / 0x20 / 0x40 sind die Warnungen dieses Subjekts.

Schritt 1 falsch zu machen heißt, dass eine verrauschte Servoleitung als
BEC-Überstromfehler gelesen wird und jemand einem Kurzschluss hinterherläuft,
den es nicht gibt.

### Die Warnungen werden vom Zustand qualifiziert

Ein Warnbit allein ist kein Fehler:

| | Ist ein Fehler, wenn |
| --- | --- |
| kein Warnbit gesetzt | der Zustand `POWER_CUT` ist → **Überspannung** |
| Unterspannung | der Zustand **unter** `STARTING` liegt (< 0x08) |
| Übertemperatur | der Zustand `POWER_CUT` ist |
| Überstrom | der Zustand `POWER_CUT` ist |

Dasselbe Bit ist im Betrieb eine Vorwarnung und ein Fehler, sobald die Leistung
abgeschaltet ist — und Überspannung hat gar kein eigenes Bit, sie ist *die
Abwesenheit* von Warnungen, während die Leistung abgeschaltet ist. Ein Decoder,
der Bits ohne den Zustand meldet, wird sowohl falschen Alarm schlagen als auch
die eine Bedingung ohne Flag verpassen.

---

## 7. Punkte, die im Referenzcode zu prüfen sind

Sechs Fehler, gefunden beim Lesen der Referenzimplementierung. Jeder ist eine
Stelle, an der *was der Code tut* und *was das Protokoll bedeutet*
auseinandergehalten werden müssen — und eine daraus abgeschriebene Umsetzung
erbte alle sechs.

| | |
| --- | --- |
| **1. Der Init-Einsprungpunkt gibt nichts zurück** | deklariert als Rückgabe eines Erfolgs-Flags, fällt dann ohne `return` hinten heraus. Undefined Behaviour: die Initialisierung meldet Erfolg oder Misserfolg zufällig |
| **2. Eine übergroße Anfrage beschädigt das Senden** | der Builder setzt das Längenfeld und kehrt *danach* vorzeitig zurück, wenn diese Länge den Puffer übersteigt — und lässt eine Länge zurück, die mehr behauptet, als geschrieben wurde. Die nächste Sendung schickt alte Bytes über das Frameende hinaus |
| **3. Timing-Argumente werden ignoriert** | der Builder nimmt eine Frameperiode und einen Timeout entgegen und weist keines von beiden zu; beide Zuweisungen sind auskommentiert. Möglicherweise ein Artefakt davon, ein Protokoll aus einem Treiber herausgezogen zu haben, der ein Dutzend beherrschte |
| **4. Die Sequenznummer lebt im Sendepuffer** | an Ort und Stelle hochgezählt statt als Zähler gehalten — ein Builder, der vorzeitig zurückkehrt (2), zählt die Sequenz also weiter, ohne dass ein Frame hinausgeht |
| **5. Struct-Cast über den Empfangspuffer** | setzt Little-Endian voraus, setzt kein Padding voraus, führt nicht ausgerichtete 16-Bit-Zugriffe aus. Es funktioniert zufällig auf den ABIs, auf denen es gelaufen ist |
| **6. Falsch nummerierte Parameterkommentare** | drei aufeinanderfolgende Einträge mit 26 beschriftet. Ob die *Werte* um eins verschoben sind oder nur die Kommentare, ist genau das, was beantwortet sein muss, bevor auf einen ESC geschrieben wird |

Ein Unterschied, der kein Defekt ist: jener Code synchronisiert sich neu, indem
er seinen Puffer byteweise verwirft. **Der Decoder von rcbench prüft bereits
jeden Sync-Kandidaten und bevorzugt den frühesten vollständigen Frame** — und
holt so einen guten Frame zurück, der hinter Rauschen ankommt, das einen
plausiblen Sync enthält, ein Fall, den der byteweise Ansatz fallen lässt. Siehe
[der Link](Link-de.md); diese Form wiederverwenden.

---

## 8. Was zu messen ist, bevor man dem hier traut

Jeder Punkt macht aus einer Herleitung eine Tatsache.

1. **`rpm`-Skalierung.** Bekannter Motor, bekannte Polzahl, bekannte mechanische
   Drehzahl. Entscheidet ×10 gegen ÷10 (Abschnitt 4).
2. **CRC-Seed.** Einen Frame aufzeichnen; bestätigen, dass er mit `0x0000`
   verifiziert und mit `0xFFFF` fehlschlägt.
3. **Framelänge.** Bestätigen, dass ein v3-Telemetrieframe 34 Bytes hat und dass
   `frame_length` die CRC mitzählt.
4. **Legacy-Header.** Bestätigen, dass ein ESC vor v3 `seq` und `device`
   auslässt und dass die Payload bei Offset 4 beginnt.
5. **Umschaltzeit.** Die Lücke zwischen dem letzten Byte einer Anfrage und dem
   ersten der Antwort. Sie legt das Timing des Koprozessors fest, wie es
   `LINK_TURNAROUND_US` für den Panel-Link tat.
6. **Parameterindizes**, mit YGE oder per Lesen-Ändern-Lesen gegen deren eigenes
   Werkzeug — **vor jedem Schreiben**. Abschnitt 5.3, und die Schrittweite der
   Abschaltspannung mit ihnen.
7. **`status2` und `reserved1`.** Beide über eine Sitzung mitloggen; wenn
   `reserved1` ein High-Byte des Verbrauchs ist, bewegt es sich auf einem langen
   Lauf.

---

## 9. Wo das in rcbench landet

**Auf dem Koprozessor.** Eine Anfrage-/Antwortschleife mit 20 Hz, einer
Umschaltzeit im Mikrosekundenbereich und einem Timeout, der feuern muss, ob
Bytes ankommen oder nicht — das Bedienteil hat dort nichts zu suchen. Das
Bedienteil sieht das Ergebnis als `bench_state` über das vorhandene
Page-Protokoll; **kein OpenYGE-Frame überquert den Panel-Link**, und das ist die
Regel, auf der [die Zwei-Prozessor-Teilung](Link-de.md) ruht. Es ändert auch
nichts an der Sicherheitsanordnung: ein Gaskommando reist weiterhin als
Registerschreiben hinter dem Heartbeat und dem Failsafe des Koprozessors
([Sicherheit](Safety-de.md)).

**Was es ändert.** Das ist Schritt 3 — *die Zahlen echt machen* —, der ohne den
Sensor ankommt, den Schritt 3 vorausgesetzt hatte. Ein YGE-ESC meldet Spannung,
Strom, Verbrauch, eRPM und vier Temperaturen selbst, jedes modellierte Feld von
`bench_state` bekommt also eine echte Quelle, und das SIMULATION-Watermark kann
weg, bevor der bis 2027 rückbestellte INA228 kommt. Es ersetzt diesen Shunt
nicht: der ESC meldet, was er über sich selbst glaubt, ein unabhängiger Shunt
ist, was der Prüfstand weiß, und die Abweichung ist eine eigene Messung.

**Was es hinzufügt, das nicht geplant war.** ESC-Programmierung über eine
dokumentierte Schnittstelle mit Unterstützung des Herstellers, was BLHeli_S und
AM32 auf derselben Liste wohl voraus ist. Kopfdrehzahl umsonst aus den
Parametern 20–22. Und eine Fehleranzeige mit echtem Inhalt, denn Abschnitt 6 ist
eine Diagnose und keine Status-LED.

**Was gebaut ist:**

    shared/openyge/          pure C, host-tested, no SDK
      openyge_frame.c        encode and decode, reusing link_crc with seed 0
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the drip-fed cache and its completeness rule

Der Decoder hat die Form des Panel-Links: jedes Sync-Byte im Puffer ist ein
Kandidat, und der früheste vollständige gewinnt — ein echter Frame, der hinter
Rauschen ankommt, das zufällig einen plausiblen Sync enthält, wird also trotzdem
zurückgeholt. Er zählt Frames, CRC-Fehler, Resyncs und getrennt davon die
Frames, deren CRC hielt, deren Form aber unmöglich war — Letzteres ist eine
Versionsabweichung oder ein Fehler am anderen Ende und keine verrauschte
Leitung, und die beiden wollen unterschiedlich behandelt werden. Der
Analyser-Bildschirm verspricht *„eine Rohansicht: Bytes, Lücken, Fehler,
Framing"*, und diese Zähler sind das.

Zwei Entscheidungen im Codec, die man kennen sollte:

- **Nichts wird über den Empfangspuffer gecastet.** Jedes Feld wird byteweise
  zusammengesetzt, denn die Leitung ist gepackt little-endian, und ein Host muss
  weder das eine noch das andere sein.
- **Eine halb gelesene Parametertabelle lässt sich gar nicht lesen.**
  `openyge_params_get` gibt false zurück, bis jeder Index angekommen ist, und
  ein anstehendes Schreiben zieht die ganze Tabelle zurück statt nur der Indizes,
  die geschrieben werden. Teils alt und teils neu liest sich wie die
  Einstellungen des ESC und ist es nicht.

**Was nicht gebaut ist:** `openyge_session.c`, die Anfrage-/Antwort-State-Machine
— Pollen im Frametakt, Abgleich der Sequenznummer, die Lese- und Schreibtimeouts,
und der Rückfall aufs Zuhören, wenn sich herausstellt, dass der ESC vor v3 ist.
Sie will die Messung der Umschaltzeit aus Abschnitt 8, bevor ihr Timing etwas
bedeutet, also ist sie als Nächstes dran und nicht jetzt.

**Eines zuerst entscheiden: welche ESCs unterstützt werden.** OpenYGE gehört
YGE. Das Material steht neben Hobbywing, Kontronik, Scorpion, OMP, ZTW, APD, FLY,
Graupner und XDFly — mehrere teilen sich eine State Machine, keiner ein
Frameformat. Die Session-Schicht so zu bauen, dass ein zweites Protokoll
dazukommen kann, ist jetzt fast umsonst und später teuer; zwei zu bauen, bevor
eines davon Hardware gesehen hat, heißt Anforderungen zu erfinden. Ein Protokoll,
bewiesen, dann das zweite.
