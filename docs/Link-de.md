# Der Link

<sub>[English](Link.md) · **Deutsch**</sub>

Die beiden Platinen kommunizieren über CAN (Controller Area Network) mit
1Mbit/s. Zuerst die Verkabelung, dann die Protokollreferenz.

## Verkabelung

| | |
| --- | --- |
| Bus | Classic CAN (kein CAN FD (Flexible Data Rate)), 1Mbit/s, nur 29-Bit-Identifier |
| Panel | TWAI-Controller (Two-Wire Automotive Interface, der CAN-Controller des ESP32-P4) auf GPIO21 (RX) und GPIO22 (TX; GPIO: General-Purpose Input/Output) zum Transceiver TJA1051T/3 der Platine, getaktet vom 40-MHz-Quarz |
| IOmcu | XL2515 (MCP2515-kompatibel) an `spi1`: SCK GPIO 10, MOSI GPIO 11, MISO GPIO 12, CS GPIO 9, INT GPIO 8; SPI-Takt 10MHz (SPI: Serial Peripheral Interface) |
| Transceiver | SIT65HVD230 (3,3V) auf dem IOmcu-Modul |
| Abschluss | 120Ω an beiden Enden |
| Quarz | der XL2515 braucht einen 16-MHz-Quarz; 8MHz begrenzen den Controller auf 500kbit/s |

Die CAN-Pins des Panels gehören CAN allein: CAN und die USB-Konsole (USB:
Universal Serial Bus) laufen gleichzeitig. Pin 1 des CAN-Headers H11 des
Panels ist die 5-V-Schiene der Platine, das Link-Kabel kann das Panel also
versorgen; die Stromaufnahme des Panels auf diesem Weg ist nicht gemessen.

Kommen keine Frames durch: [Den Link in Betrieb nehmen](Bringup-de.md).

## Verhalten bei Linkausfall

Der IOmcu setzt nach 200ms ohne Anfrage Failsafe-Werte; das Panel
eskaliert nach 1s ohne Antwort. Das Failsafe rastet ein. Zurückkehrender
Verkehr stoppt den Stillezähler, hebt das Failsafe aber nicht auf; verlassen
wird es durch das Schreiben von 0x5AFE in das Register CLEAR der
Control-Page.

Der IOmcu hält außerdem einen Arm-Latch. Er wird bei jedem Start
gesetzt, an der Flanke in die Stille auf dem Link, wenn dem Heartbeat nicht
vertraut wird, und durch ein ARM, das der IOmcu abweist. Solange
er gesetzt ist, wird ein Schreiben von ARM mit NOT_ARMED abgewiesen. Dasselbe
CLEAR löst ihn. Er ist nicht das Failsafe: STATUS meldet ihn nicht, und das
ON der Versorgung liest ihn nicht. Die Regeln stehen in
[Sicherheit](Safety-de.md#arm-latch).

### Bus off

Ein CAN-Transmitter, der kein Acknowledge bekommt, wiederholt von sich aus und
addiert je Versuch 8 auf seinen Transmit Error Counter. Bei 1Mbit/s erreicht
ein Frame, den niemand beantwortet, die Bus-off-Schwelle von 256 in etwa 4ms
— jeder Moment ohne zweiten Knoten am Bus nimmt den Controller des Panels also
vom Bus: IOmcu ohne Strom, nach einem Reset, oder lange genug mit
abgeschalteten Interrupts.

Der TWAI-Controller (Two-Wire Automotive Interface) des ESP32-P4 kommt nicht
von selbst zurück. `can_twai_recover()` läuft am Identity-Poll, solange der
Link unten ist, einmal pro Sekunde: aus dem Zustand Bus off ruft es
`twai_initiate_recovery()`, wartet die 128 Bus-frei-Signale ab, die der
Controller im Zustand Recovering zählt, und startet ihn aus dem Zustand
Stopped wieder. Die Erholung dauert damit bis zu drei Polls, also rund 3s.
Ohne sie ist der erste stille Moment am Bus endgültig und nur ein
Aus-und-Einschalten hilft.

Der Report, der bei liegendem Link alle 5s ausgegeben wird, trägt die Zähler:

```
LINK ...
  bus    tx errors 0 rx errors 0 bus errors 0
```

Ein Transmit Error Counter, der auf 256 zuläuft, heißt, dass niemand die
Frames mit einem ACK (Acknowledge) bestätigt. Die Zeile endet auf `-- BUS
OFF`, wenn das Panel bereits aufgehört hat zu senden. Steht dort statt der
Zähler `the controller is not running`, ist der CAN-Controller gar nicht erst
gestartet — das ist eine andere Diagnose als ein Bus ohne Fehler.

## Protokoll

Pages mit bis zu 32 Sechzehn-Bit-Registern, gelesen und geschrieben in
Fenstern. Der IOmcu sendet nur als Antwort auf eine Anfrage.
Protokollversion 4.32. Die Major-Version ist Register 0 der Page 0. Die Major
ändert sich, wenn ein Register seine Bedeutung wechselt oder eine Page
umnummeriert wird; die Minor, wenn eine Page oder ein Register am Ende
hinzukommt, was ein älteres Panel ignorieren kann.

Ein IOmcu, der eine andere Protokoll-Major meldet als das Panel, gilt
als abwesend: der Link bleibt unten, der Splash markiert den
IOmcu-Schritt als fehlgeschlagen neben der gemeldeten Version, das
Panel protokolliert beide Majors und meldet `Protokollversionen passen nicht zusammen. ARMED ist gesperrt.`, und kein Write erreicht den IOmcu. Seine Ausgänge bleiben aus,
und das Panel läuft wie ohne angeschlossenen IOmcu. Panel und
IOmcu auf verschiedenen Seiten eines Major-Sprungs werden deshalb
zusammen geflasht.

Register 0 bis 6 von IDENTITY und die beiden Update-Pages, UPDATE_CTL (0xF0)
und UPDATE_DATA (0xF1), sind eingefroren: sie behalten ihre Nummern, ihren
Aufbau und ihre Bedeutung unter jeder späteren Major, und ein IOmcu
jeder späteren Major beantwortet sie. Eine Änderung an ihnen ist eine neue
Page, keine Major-Änderung. Ein Panel, das auf eine andere Major trifft, liest
deshalb trotzdem, was der IOmcu laufen hat, und bietet sein eigenes
IOmcu-Image an
([Firmware-Update über den Link](#firmware-update-über-den-link)).

### Kompatibilität

Verglichen wird nur die Major. Der Link kommt hoch und der Prüfstand schaltet
auf ARMED, gleich welche Minor die beiden haben. Das Panel liest die Minor des
IOmcu beim Link-up und nutzt nichts, was diese Minor nicht hat: die
Frame Rate von SERVO ab 4.1, seinen Sweep ab 4.2, SUPPLY ab 4.3, RESUME des
Sweeps ab 4.6, SENSE und SERVO_SENSE ab 4.7, TONE ab 4.8, den Ausgangsencoder von SENSE ab 4.9,
BIND_CFG, BIND_OUT und BIND ab 4.10, SERVO_WIN, `RESETS` von SENSE und ein
vorzeichenbehaftetes `CAP_HOLD_MA` ab 4.11, KST ab 4.12, PROG, die
Programmierports der Servo-Programmier-Page (`KST`, 0x32), Flag-Bit 11 von SUPPLY und das Port-Bit von
SERVO_SENSE ab 4.13. Ab 4.14 plant der Programmierport einen Schreibauftrag über mehrere
Durchläufe und meldet die Ergebnisse 33 bis 35; das Panel schreibt dafür
nichts anders. Die Operation LEAVE und das Schreibregister 28 dieser Page
werden ab 4.15 gesendet. Ab 4.16 weist der IOmcu einen Slew über 3000
und einen PWM-Endpunkt ab, der länger als sein Frame ist, und das Panel schreibt einem Programmierport
seinen Bereich vor dem Schreiben, das den Puls des Ports einschaltet
([Ein Puls nicht länger als sein Frame](#ein-puls-nicht-länger-als-sein-frame)). Ab 4.17 weist die TONE-Page einen belegten Pin mit
BUSY statt BAD_VALUE ab, und das Panel bietet einen mit BUSY abgewiesenen
Frame erneut an ([Ein belegter Pin am Phasenabgriff](#ein-belegter-pin-am-phasenabgriff)).
Ab 4.18 nennt ARM des Panels die Rollen, die es auf ARMED schaltet
([Ein Arm für eine Rolle](#ein-arm-für-eine-rolle)). Ab
4.19 treibt die Frame Rate von SERVO nur das PWM-Ziel, und eine Änderung
von TARGET setzt sie auf 0; das Panel zeigt die Rate dann als nicht
gesendet, bis es sie wieder schreibt. Ab 4.20 liefert ARTWORK die Zeichnung
der Platine mit einem Hash über sie in Register 7 und 8, und das Panel liest
ARTWORK erst ab 4.20 ([Die Zeichnung der Platine](#die-zeichnung-der-platine)).
Ab 4.21 zählt STATUS in Register 8 und 9 die Frames, die der IOmcu
abgelehnt hat, und das Panel liest sie erst ab 4.21
([Abgelehnte Frames](#abgelehnte-frames)). Ab 4.22 nehmen UPDATE_CTL und
UPDATE_DATA ein neues IOmcu-Image über den Link an; ein älterer
IOmcu lehnt beide mit BAD_PAGE ab, und das Panel sagt dann, dass ein
USB-Laden (Universal Serial Bus) nötig ist
([Firmware-Update über den Link](#firmware-update-über-den-link)).
Ab 4.23 fährt HITEC (0x34) rohe Transaktionen der Hitec-Servoleitung auf
einem Programmierport, und die KST-Page weist ENTER und ein REBOOT aus PWM
mit BUSY ab, solange HITEC den Port hält; ein älterer IOmcu weist HITEC
mit BAD_PAGE ab. Ab 4.24 nimmt HITEC den Empfangsmodus einer Leitung der
D-Serie im Schreibregister 23; das Panel schreibt den Hitec-D-Empfang aus
SETUP dorthin, und ein IOmcu mit 4.23 weist das Register mit READ_ONLY ab
und empfängt mit dem Pull-up. Das Panel sendet keine Hitec-Transaktion
([Der Programmierport für Servos von Hitec](LinkHitec-de.md)). Ab 4.25
richtet SENSE_MAP (0x35) bis zu 4 INA3221 mit einem Shunt und einem
Beobachtungsziel je Kanal ein, und SENSE_WIN_A und SENSE_WIN_B (0x37, 0x38)
tragen das letzte Fenster jedes Kanals; ein älterer IOmcu weist die drei mit
BAD_PAGE ab. Das Panel dieses Builds schreibt SENSE_MAP aus SETUP > SENSORS
und liest keine der beiden Fenster-Pages ([Die INA3221-Map und ihre Fenster](LinkSensors-de.md)).
Ab 4.26 hat PROG 28 Register: die Register 24 bis 27 sind die FUNC jedes
Ports, und der Build hat 4 Ports; das Panel liest 24 Register eines
älteren IOmcu und bietet dessen Ports nur SERVO PWM an
([Die Programmierports](#die-programmierports)). Ab 4.27 hält
SENSE_LIMIT (0x36) eine Stromgrenze je Kanal und markiert jeden Kanal, dessen
Fenster sie überschritten hat, was das Panel als Fehler behandelt
([Sicherheit, Fehler](Safety-de.md#fehler)); ein älterer IOmcu weist die
Page mit BAD_PAGE ab, und das Panel dieses Builds schreibt keine Grenze.
Ab 4.28 nimmt die FUNC eines Ports auch DSHOT150, DSHOT300 und DSHOT600,
und die FLAGS-Bits 12 bis 15 sagen, dass das DShot eines Ports seinen Pin
hält; ein älterer IOmcu weist diese Werte mit BAD_VALUE ab, und das Panel
bietet sie erst ab 4.28 an.
Ab 4.29 hält HITEC die SUPPLY-Page vom OPEN, das einen Port nimmt, bis zum
CLOSE, das ihn zurückgibt, so wie KST sie außerhalb von PWM hält; ein
älterer IOmcu hält sie nur, solange ein POWER läuft. Das Panel liest das
Flag-Bit 11 von SUPPLY in beiden Fällen und schreibt nichts anders
([Die Versorgung, solange ein Port gehalten ist](LinkHitec-de.md#die-versorgung-solange-ein-port-gehalten-ist)).
Ab 4.30 nimmt KST Bit 5 im Befehl eines REBOOT aus PROGRAMMING: nach dem
Versorgungszyklus wartet der Port in LEAVING für die Zeit im
Schreibregister 28, bevor PWM folgt. Ein älterer IOmcu weist das Bit mit
BAD_VALUE ab; das Panel setzt es erst ab 4.30 und schickt einem älteren
IOmcu den REBOOT ohne das Bit, dessen Zyklus in PWM endet
([Die Wartezeit nach einem Versorgungszyklus](LinkKST-de.md#die-wartezeit-nach-einem-versorgungszyklus)).
Ab 4.31 hält SUPPLY ein OFF, das das Panel schreibt, während ein
Programmierport es hält, vom PD mini zurück, solange der Port in den
Speicher des Servos schreibt, höchstens 10.000ms nach dem OFF, und weist
danach das eigene ON des Ports ab, bis der Port loslässt; HITEC nimmt Bit 4
im Kommando eines Schritts, weitere Schritte dieses Schreibens folgen. Ein
älterer IOmcu schaltet den PD mini beim OFF aus und weist das Bit mit
BAD_VALUE ab. Das Panel schaltet eine Versorgung, die ein Port hält, bei
einem Fehler erst ab 4.31 aus und setzt das Bit erst ab 4.31
([Sicherheit, Die Versorgung eines Programmierports](Safety-de.md#die-versorgung-eines-programmierports)).
Ab 4.32 schaltet PROG_SUPPLY (0x39) die eigene Versorgung der
Programmierports auf einem Board, das eine hat, GPIO 21 des
rbmod-Carriers; ein älterer IOmcu lehnt die Page mit BAD_PAGE ab
([Carrier, Versorgung](Carrier-de.md#versorgung)).
Der IOmcu liest
die Minor des Panels nie; eine Page, die ein älteres Panel nicht kennt, schreibt es
nie.

| Panel | IOmcu | Link und Schalten auf ARMED | SENSE (0x2B) und SERVO_SENSE (0x2C) |
| --- | --- | --- | --- |
| 4.7 | 4.7 | hoch, schaltet auf ARMED | bedient |
| 4.7 | 4.6 | hoch, schaltet auf ARMED | das Panel sendet an keine der beiden Pages; der IOmcu beantwortet beide mit BAD_PAGE. BENCH-Bits 5 und 6 lesen 0 |
| 4.6 | 4.7 | hoch, schaltet auf ARMED | das Panel liest und schreibt keine der beiden Pages und übergeht BENCH-Bits 5 und 6. Eine Konfiguration, die ein 4.7-Panel im Flash des IOmcu hinterlassen hat, bleibt in Kraft, und ihre zwei Pins bleiben den Ausgängen entzogen. Sobald der IOmcu einen INA228 liest, trägt BENCH dessen Spannung und Strom in denselben Skalen, und das Panel zählt Ladung und Energie selbst aus diesem Strom |

Der IOmcu dieses Builds bedient beide Pages, hält die Konfiguration im
Flash und hält ihre Pins. Sein Core 1 liest die freigegebenen Bauteile im
1-ms-Takt, und Core 0 trägt ein, was Core 1 gelesen hat; die Nur-Lese-Register
lesen 0, bis Core 1 unter der geltenden Konfiguration gelesen hat, etwa 1ms
nach einer Änderung. Solange der INA228 antwortet, trägt BENCH seine
Spannung und seinen Strom (die Mittelwerte des letzten 50-ms-Fensters), ihr
Produkt als Leistung, die Spitzen des Laufs aus seinen 500-Hz-Messungen
sowie seine Ladung und Energie, mit Bits 5 und 6. An der Flanke zum Treiben
löscht er Bit 6 und die Register für Ladung und Energie im selben Durchlauf
und nicht erst bei seiner nächsten 50-Hz-Abtastung, sodass kein Lesen nach
dem Schalten auf ARMED die Summen des letzten Laufs trägt. Ein Lauf, der mit
antwortendem INA228 beginnt, behält ihn bis zu seinem Ende als Quelle von
BENCH: hört er auf zu antworten, bleiben diese Felder leer, statt zur
Telemetrie des ESC zurückzukehren. Bei einer Messung der Bewegung ist der
Pegel vor dem Befehl der Mittelwert von CH1 über die 50ms vor der Flanke;
eine Messung, die scharfgeschaltet wird, während der INA3221 CH1 nicht
liest, endet sofort als verloren und zählt in CAP_SEQ. Das Panel dieses Builds liest bei jedem Link-Aufbau die Register 0
bis 11 von SENSE, schreibt die Einstellung aus SETUP INTERFACES Frame für
Frame, wo sie abweicht (`shared/bench/sense_link.c`, `test_sense_link`), und
liest die Identity-Page erneut, nachdem ein Schreiben angenommen ist. Auf
SERVO_SENSE schreibt es keine Messung. SERVO_WIN (0x31) liest es an einem
IOmcu mit 4.11: [Der Fenster-Ring](#der-fenster-ring).

Die Page weist SDA und SCL, die nicht das Paar eines Blocks sind, ab, was
auch immer ENABLE hält; ein IOmcu von 0.15.0 oder älter nimmt sie mit
ENABLE 0 an und weist das danach freigegebene Bauteil ab. Das Format auf dem
Draht und die Protokoll-Minor sind dieselben. Ein Panel von 0.15.0 oder
älter, dessen zwei Pin-Einstellungen unabhängig sind, sieht die Abweisung bei
der Änderung statt bei der Freigabe: sein Alert `Sensor-Pins GPIO 15/GPIO 17 abgelehnt. SETUP > INTERFACES prüfen.`, einmal je Änderung und je Link-Aufbau,
und die Page behält die Pins, die sie hatte. Seine Pins auf −1 werden als
ENABLE 0 mit 0 für den Pin geschrieben und angenommen. Eine
Konfiguration im Flash des IOmcu, deren Pins kein Paar sind, wird
beim Start als Ganzes abgewiesen, und die Page beginnt mit ihren Vorgaben
ohne freigegebenes Bauteil; ein Panel schreibt beim Link-Aufbau, was
abweicht. Das Panel dieses Builds bietet nur Paare an.

TONE (0x2D) ist eine eigene Page, und 4.8 verschiebt sonst nichts:

| Panel | IOmcu | TONE (0x2D) |
| --- | --- | --- |
| 4.8 | 4.8 | bedient |
| 4.8 | 4.7 | der IOmcu beantwortet jedes Lesen und Schreiben der Page mit BAD_PAGE; ein Panel liest beim Link-up die Minor und sendet dorthin nichts |
| 4.7 | 4.8 | das Panel liest und schreibt die Page nicht. Ein Tap, der im Flash des IOmcu steht, startet beim Booten, läuft und wird nie gelesen; sein Pin bleibt den Ausgängen entzogen |

Die Page hat kein Capability-Bit. Ein Panel, das die Minor liest, weiß, dass
die Page existiert; FLAGS sagt, ob der Tap läuft.

Der IOmcu dieses Builds bedient die Page und hält die Register 0 bis 6
im Flash (Speicher-Record Version 8, die Register des Taps ab Version 6; ein
Record der Version 3 bis 5 liest mit
ausgeschaltetem Tap auf seinen Standardwerten). Core 0 besitzt die
PIO-State-Machine (PIO: Programmable Input/Output) und ihren DMA-Kanal (DMA:
Direct Memory Access), Core 1 liest den Ring im 1-ms-Takt und lässt den
Detektor laufen, und Core 0 trägt die fertigen Beeps und den Status in die
Page ein. Die Page, der Ring-Leser, der Service und das PIO-Programm, in einem
taktgenauen Modell der State Machine ausgeführt, sind auf dem Host getestet
(`test_tone_page`, `test_edge_ring`, `test_tone_svc`, `test_tone_pio`).

Der Ausgangsencoder gehört zu SENSE, und 4.9 verschiebt sonst nichts: ENABLE
bekommt Bit 2, und die Page wächst von 26 auf 32 Register, 26 bis 30 neu und
31 reserviert.

| Panel | IOmcu | Der Encoder (SENSE Bit 2, Register 26 bis 31) |
| --- | --- | --- |
| 4.9 | 4.9 | bedient |
| 4.9 | 4.8 oder 4.7 | das Panel schreibt ENABLE Bit 2 nicht und liest von SENSE nur die Register 12 bis 25; mit dem Encoder in SETUP freigegeben meldet es `IOmcu-Firmware älter als 0.15.0. AS5600 nicht ausgelesen.`, beim Link-up und wenn der Encoder freigegeben wird, während der IOmcu antwortet. Die Page eines 4.8-IOmcu hat 26 Register und weist ein Lesen darüber hinaus ab |
| 4.9 | 4.6 oder älter | an SENSE wird nichts gesendet; dieselbe Meldung |
| 4.8 oder 4.7 | 4.9 | das Panel schreibt Bit 2 nie und liest die Register 26 bis 31 nie. Ein Encoder, den ein 4.9-Panel im Flash des IOmcu hinterlassen hat, bleibt freigegeben und wird gelesen; seine zwei Pins bleiben den Ausgängen entzogen |

Der Encoder ist ein AS5600 von ams OSRAM auf der Ausgangswelle des Servos, an
seiner festen Adresse 0x36 auf dem Bus, den SENSE schon betreibt, standardmäßig
GPIO 16 und GPIO 17. Er hat auf der Page keinen Parameter außer dem Freigabe-Bit:
Pins und die 400kHz sind die des Busses. Jedes Register wird in einer
eigenen Transaktion gelesen, da RAW ANGLE und MAGNITUDE Register sind, die das
Bauteil besonders behandelt: der Adresszeiger läuft nicht über sie hinaus,
und ein Lesen, das von STATUS oder AGC her durch das Weiterzählen dort
ankommt, wird nicht vorausgesetzt. Der IOmcu liest STATUS (1 Byte) und
RAW ANGLE (2 Byte) in jedem zweiten 1-ms-Takt, 500Hz, in den Takten ohne
Rotationseintrag: 217,5µs Buszeit bei 400kHz. In einem von 25 dieser Plätze
liest er stattdessen RAW ANGLE, AGC (1 Byte) und MAGNITUDE (2 Byte):
337,5µs. Das sind 500 Winkellesungen je Sekunde im Abstand von 2ms, 480
STATUS-Lesungen und 20 Lesungen von AGC und MAGNITUDE. Eine Transaktion von N
Byte dauert 9 × (3 + N) + 3 Takte; die Zeit des Controllers zwischen den
Transaktionen ist nicht gezählt und nicht gemessen. Ein ungerader Takt ist
CH1 des INA3221, ein weiterer Kanal des INA3221, der Platz des INA228 und
der Encoder: 600µs, 720µs in einem Platz mit MAGNITUDE. Ein gerader Takt
bleibt unter 690µs. Der Encoder fügt der Buszeit einer Sekunde 11,1%
hinzu: mit allen drei Kanälen des INA3221 und dem INA228 43,4% ohne ihn
und 54,5% mit ihm.

| Register | Name | Liest |
| ---: | --- | --- |
| 26 | `AS5600_FLAGS` | Bit 0 antwortet an 0x36 mit einem STATUS, den ein AS5600 geben kann; Bit 1 STATUS MD (Magnet erkannt); Bit 2 ML (Feld zu schwach); Bit 3 MH (Feld zu stark); Bit 4 an 0x36 antwortet etwas mit ML und MH zugleich, was kein AS5600 gibt, und wird nicht benutzt; Bit 5 der Winkel und die Bits 1 bis 3 enthalten einen Messwert dieser Konfiguration. Bits 1 bis 3 lesen 0, solange Bit 0 es tut. `AS5600_ANGLE` ist nur eine Position, solange die Bits 0, 1 und 5 alle gesetzt sind |
| 27 | `AS5600_ANGLE` | RAW ANGLE, 0 bis 4095 für eine Umdrehung (360 / 4096 = 0,0879 Grad je Schritt), zuletzt gelesen und ohne Mitte; 0 bis zum ersten Lesen |
| 28 | `AS5600_MAGNITUDE` | die CORDIC-Magnitude (CORDIC: Coordinate Rotation Digital Computer), 12 Bit, mit 20Hz gelesen; 0 bis zum ersten Lesen |
| 29 | `AS5600_SAMPLES` | Winkellesungen modulo 65536: zwei Lesungen der Page mit demselben Zählerstand sind eine Messung |
| 30 | `AS5600_STILL_MS` | wie lange der Winkel innerhalb von 12 Schritten (1,05 Grad) eines Ankers geblieben ist, in ms, bei 65535 gesättigt; 0 ohne Messung und solange MD nicht gesetzt ist |
| 31 | `RESETS` | seit 4.11, und nicht vom Encoder: die Strommonitore, die zurückgesetzt gefunden wurden, der INA228 im Low-Byte und der INA3221 im High-Byte, je modulo 256, seit die geltende Konfiguration angenommen wurde. [Der Fenster-Ring](#der-fenster-ring) nennt die Regel. Ein IOmcu mit 4.10 oder älter liest 0 |

Der Anker ist der Winkel der letzten Messung, die mehr als 12 Schritte vom
vorigen Anker lag, auf dem Kreis über den Sprung von 4095 auf 0 gerechnet. Eine
Bewegung, die zur Zeit t_ende endet und zur Zeit t_lesen gelesen wird, zeigt
`AS5600_STILL_MS` = t_lesen - t_ende auf das 2-ms-Messintervall genau, das
Panel misst das Ende einer Bewegung also in der Auflösung des IOmcu,
gleich wie oft es selbst abfragt. Der eigene Fehler des Panels ist die Zeit
zwischen dem Lesen im IOmcu und dem Eintreffen der Antwort im Panel:
nicht gemessen. Fällt das Bauteil aus oder gilt eine neue Konfiguration, werden
der Anker und die Register 26 bis 31 gelöscht.

Die Magnetbits sind STATUS (0x0B) Bit 5 MD, Bit 4 ML und Bit 3 MH. Das
Datenblatt (ams AS5600 DS000365 v1-06) sagt zu nicht gesetztem MD: "If the
measured magnet field strength goes below the minimum specified level
(Bz_ERROR), the output is driven low [...] and the MD bit in the STATUS
register is 0", mit Bz_ERROR bei 8 mT. Der Zählerstand ist dann keine
Position: eine Messung ohne MD löscht den Anker, `AS5600_STILL_MS` liest 0,
und das Panel gibt den Winkel an nichts weiter: nicht an die Seite SERVO,
nicht an SENSOR-MITTE, nicht an einen Lauf. ML und MH sind "AGC maximum gain
overflow, magnet too weak" und "AGC minimum gain overflow, magnet too
strong"; das Datenblatt nennt keine Wirkung auf den Winkel und spezifiziert
sein Rauschen nur für 30 bis 90 mT. Mit gesetztem MD wird der Zählerstand
verwendet, was auch immer ML und MH sagen, und das Panel meldet sie. Ein
Slot, der MAGNITUDE liest, liest kein STATUS und richtet sich nach dem 2ms
davor gelesenen.

Die STATUS-Bits 7, 6, 2, 1 und 0 sind in der Registerübersicht des
Datenblatts leer, und deren Anmerkung sagt: "Blank fields may contain
factory settings". Sie werden ausmaskiert und entscheiden nichts. Das
Bauteil hat kein Identitätsregister: der Probe nimmt, was an 0x36 antwortet,
als AS5600, außer bei einem STATUS mit ML und MH zugleich: die Verstärkung
an beiden Enden ihres Bereichs, was ein Gerät zeigt, das 0xFF antwortet, und
ein AS5600 nicht. Diese Ausnahme folgt aus der Bedeutung der beiden Bits; das
Datenblatt nennt sie nicht.

STATUS und RAW ANGLE sind für
die Fehlerzählung eine Messung: ein fehlschlagendes Lesen von RAW ANGLE zählt
als Fehler, auch wenn das STATUS-Lesen davor geantwortet hat; 3 Messungen in
Folge mit fehlschlagendem RAW-ANGLE-Lesen nehmen das Bauteil offline (3
fehlgeschlagene Transaktionen in Folge, wie bei den INA-Bauteilen). Das Panel liest die Register 12
bis 31 alle 40ms, solange der Encoder auf der Page freigegeben ist. Die
Register 26 bis 31 sind nur lesbar; ein Schreiben wird mit READ_ONLY abgewiesen.

### Ein belegter Pin am Phasenabgriff

Die TONE-Page weist einen ersten Frame (Register 0 bis 3) aus einer von
zwei Arten von Gründen ab. BAD_VALUE ist ein Frame, den keine andere
Konfiguration gut macht: ein Wert außerhalb seines Bereichs, ein
Tonbereich, den der Detektor ablehnt, ein GPIO hinter der Bank, von der
Platine reserviert oder ein ADC-Pin, oder, mit gesetztem ENABLE, ein
IOmcu, dessen Core 1 beim Booten nicht gestartet ist und der bis zu
einem Neustart keine Erfassung ausführt. BUSY (seit 4.17) ist ein Pin, den ein
Ausgang, SENSE, SUPPLY oder ein Programmierport hält, oder eine Erfassung,
die der IOmcu vorerst nicht starten kann. Derselbe Frame wird
angenommen, sobald dieser Pin oder diese State-Machine frei ist.

Das Panel (`shared/bench/tone_link.c`) meldet einen abgewiesenen ersten
Frame einmal im Band. Einen mit BUSY abgewiesenen Frame bietet es alle 5s
erneut an (`TONE_LINK_RETRY_MS`), je ein Write, und einen mit BAD_VALUE
abgewiesenen schreibt es erst wieder, wenn sich die Konfiguration unter
SETUP INTERFACES ändert oder der Link wieder hochkommt.

| Panel | IOmcu | Ein abgewiesener erster Frame |
| --- | --- | --- |
| 4.17 | 4.17 | BUSY: alle 5s erneut angeboten. BAD_VALUE: nach der nächsten Änderung der Konfiguration oder dem Link-up erneut geschrieben |
| 4.17 | 4.16 oder älter | der IOmcu sagt für beides BAD_VALUE. Das Panel bietet den Frame alle 5s erneut an, wenn sein Tonbereich und seine Lücke solche sind, die die Page nimmt (der höchste Ton über dem tiefsten, die Lücke mindestens eine Periode des tiefsten Tons), sonst nicht. Ein Frame, der für einen GPIO hinter der Bank, einen reservierten oder einen ADC-Pin abgewiesen wurde, geht dann alle 5s hinaus und wird jedes Mal abgewiesen |
| 4.16 oder älter | 4.17 | das Panel nimmt BUSY als Abweisung und bietet den ersten Frame alle 5s erneut an, wie bei BAD_VALUE |

`test_tone_page` prüft jeden Grund gegen seinen Code und `test_link_pages`
den Code durch den Dispatcher. `test_tone_link` prüft einen belegten Pin,
der von einer 4.17- und einer 4.16-Page angenommen wird, sobald er frei ist,
ein BAD_VALUE einer 4.16-Page, das in 15s dreimal angeboten wird und nicht
öfter, und ein BAD_VALUE, das in 15s nicht erneut geschrieben wird: von
einer 4.17-Page für einen Pin der Platine und von beiden Pages für Töne,
die nicht gültig sind.

### Eine Bindung als Ganzes

BIND_CFG (0x2E), BIND_OUT (0x2F) und BIND (0x30) sind neu in 4.10, und 4.10
verschiebt sonst nichts. Eine Bindung sind zwei Pages mit 32 Registern,
CHAN_CFG und OUTPUTS, 16 Frames. Ein Schreiben auf eine der beiden gilt Frame
für Frame, eine Folge, die mittendrin endet, lässt also die ersten Einträge
der einen Bindung und den Rest einer anderen stehen. Die drei Pages machen aus
der Änderung einen Schritt:

| Page | Register | Regeln |
| --- | --- | --- |
| BIND_CFG | 32, wie CHAN_CFG | eine CHAN_CFG-Page, die nicht gilt. Ein Frame wird nach den Wertregeln von CHAN_CFG geprüft (Rolle 0 oder 1, Slew 0 bis 3000, Endpunkte 400 bis 2500µs) und bei einem Verstoß mit BAD_VALUE abgelehnt, ohne etwas zu speichern. Kein Ausgang ändert sich |
| BIND_OUT | 32, wie OUTPUTS | eine OUTPUTS-Page, die nicht gilt. Ein Frame wird nach den Wertregeln von OUTPUTS geprüft (ein Treiber 0 bis 4, eine Pin-Nummer bis 63) und bei einem Verstoß mit BAD_VALUE abgelehnt. Kein Ausgang ändert sich |
| BIND | 1: `COMMIT` | ein Schreiben trägt die CRC (Cyclic Redundancy Check) der 64 vorbereiteten Register. Liest die CRC dessen, was vorbereitet ist |

Die CRC ist CRC-16/CCITT-FALSE: Polynom 0x1021, Startwert 0xFFFF, keine
Spiegelung, kein abschließendes XOR, über die 32 Register von BIND_CFG und
dann die 32 von BIND_OUT, jedes als Low-Byte und dann High-Byte, 128 Bytes.

Ein Schreiben von `COMMIT`:

1. wird mit BAD_VALUE abgelehnt, wenn sein Wert nicht die CRC dessen ist, was
   der IOmcu vorbereitet hält. Nichts ändert sich. Ein Frame, der nicht
   ankam, oder ein IOmcu, der seit dem Vorbereiten neu gestartet ist,
   endet hier;
2. beurteilt die vorbereitete CHAN_CFG-Page nach jeder Regel eines
   CHAN_CFG-Schreibens und setzt sie in Kraft, oder lehnt mit BAD_VALUE ab
   und ändert nichts;
3. beurteilt die vorbereitete OUTPUTS-Page nach jeder Regel eines
   OUTPUTS-Schreibens — ARMED, die Pins des Netzteils, des Sensorbusses und
   des Phasenabgriffs, die SERVO-Rate, was das Silizium bindet — und setzt
   sie in Kraft, oder lehnt mit BAD_VALUE ab und setzt die CHAN_CFG-Page von
   vor dem Commit zurück, und mit ihr Rolle, Kommando und Ausgang jedes
   Kanals, wie sie waren;
4. wird bestätigt, und die Bindung wird einmal gespeichert.

Die geltenden Pages sind also nach jedem Ende der Folge beide die alte
Bindung oder beide die neue. Ein Commit, dessen Bestätigung verloren geht,
wurde übernommen: der Host liest die Pages, um es zu erfahren.

Was vorbereitet ist, beginnt als die beim Start geltenden Pages. Es wird
nicht im Flash gehalten und von Link-Stille nicht gelöscht; die CRC bindet
einen Commit an die Frames, die ein Host gesendet hat. CHAN_CFG und OUTPUTS
bleiben Eintrag für Eintrag beschreibbar und gelten mit jeder Bestätigung:
so schreibt der SERVO-Bildschirm den Bereich eines Kanals und das Panel die
Endpunkte des Throttle.

| Panel | IOmcu | Eine auf OUTPUTS oder PICK A PIN geänderte Bindung |
| --- | --- | --- |
| 4.10 | 4.10 | vorbereitet und übernommen: SERVO `FRAME_HZ` = 0, 8 Frames an BIND_CFG, 8 an BIND_OUT, `COMMIT`, dann OUTPUTS und CHAN_CFG zurückgelesen. 20 Austausche |
| 4.10 | 4.9 oder älter | das Panel liest die Minor beim Link-up und sendet nichts an die drei Pages. SERVO `FRAME_HZ` = 0 (ab 4.1), 8 Frames an CHAN_CFG, 8 an OUTPUTS, jeder ein eigener bestätigter Austausch und mit der Bestätigung in Kraft, dann beide Pages zurückgelesen. 19 Austausche. Ein mittendrin verlorener Frame lässt die Einträge davor in Kraft; das Panel zeigt die Pages dann, wie sie sich lesen, und schreibt nichts darüber außer einer Bindung ohne Pin |
| 4.9 oder älter | 4.10 | das Panel schreibt die drei Pages nie. Es schreibt CHAN_CFG und OUTPUTS als 8 Frames direkt hintereinander in je einem Austausch, wie zwischen zwei älteren Builds |
| ein anderer Host | 4.10 | ein `COMMIT`, der nicht die CRC der vorbereiteten Pages nennt, wird mit BAD_VALUE abgelehnt; BIND_CFG und BIND_OUT werden Frame für Frame abgelehnt wie oben |

Jedes Schreiben dieses Panels, das breiter als ein Frame ist, geht mit einem
Frame von bis zu 4 Registern je Austausch hinaus, jeder bestätigt, bevor der
nächste gesendet wird (`link_write_acked()` in `shared/link/link_port.c`). Der
XL2515 hält 2 empfangene Frames, und der IOmcu liest ihn aus seiner
Hauptschleife; mit einem Anfrage-Frame gleichzeitig unterwegs verzögert ein
verspäteter Durchlauf einen Frame und verliert keinen an einen vollen Puffer.
Dasselbe Schreiben als 8 Frames direkt hintereinander verliert 6 davon an
einen Durchlauf von 20ms (`test_bind_link`). Eine Antwort kommt weiterhin
als Frames direkt hintereinander: der TWAI-Treiber des Panels puffert 16.

Die Kosten einer Änderung, und woran sie gemessen werden:

| | 0.14.0 | Dieser Build, IOmcu mit 4.10 |
| --- | ---: | ---: |
| Austausche | 5 | 20 |
| Anfrage-Frames | 19 | 20 |
| Antwort-Frames | 33 | 34 |
| Anfrage-Frames gleichzeitig auf dem Bus, höchstens | 8 | 1 |
| CHAN_CFG und OUTPUTS auf die Ausgänge angewendet | 16-mal, einmal je Frame | je einmal, beim Commit |
| Speicheranforderungen an den Flash-Speicher | 16 | 1 |

54 Frames zu etwa 130µs sind in beiden 7ms Buszeit. Ein Austausch fügt den
Schleifendurchlauf des IOmcu und das Aufwachen der Panel-Task hinzu:
[Den Link in Betrieb nehmen](Bringup-de.md) gibt für die Laufzeit eines
Austauschs 334 bis 1400µs aus, das legt die 20 zwischen 7 und 28ms; die
Folge selbst ist auf Hardware nicht gemessen. Die Control-Task des Panels
läuft alle 5ms und sendet die Folge aus einem Durchlauf, dieser Durchlauf
dauert also 2 bis 6 Perioden; die Sicherheitsschleife — die 20-ms-Flanken des
Heartbeats, STOP — läuft darin, zwischen zwei Frames einer Page, sobald 5ms
seit ihrem letzten Lauf vergangen sind, und in allen 5ms, die auf eine
Antwort gewartet wird. Der 50-ms-Poll mit
seinem Schreiben von ARM und THROTTLE verspätet sich um dieselbe Zeit; eine
Änderung wird bei einem Prüfstand in ARMED abgelehnt, kein Prüfstand in ARMED
wartet also darauf. Die 200-ms-Stillegrenze des IOmcu zählt ab der
letzten Anfrage, die er gehört hat, und jeder Austausch der Folge ist eine:
die Folge kann sie nicht aushungern, wie lange sie insgesamt auch dauert,
und ein einzelner Frame, den der IOmcu 200ms lang nicht annimmt, setzt
`FEHLER 01` wie jede Anfrage. Ein Frame, der auf der Leitung verloren geht,
beendet die Folge nach den 1000ms seines eigenen Austauschs, der Link wird
abgebaut, und die geltende Bindung bleibt unverändert.

`shared/outputs/out_stage.c` ist die Hälfte des IOmcu und
`shared/outputs/bind_link.c` die des Panels, unter `test_bind_link`: die
Folge durch ein Modell des 2-Frame-Puffers mit einem tauben Fenster von
20ms, geöffnet alle 125µs, ein verlorener Frame an jeder der 18 Stellen,
die Timeouts über den Überlauf bei 2^32ms.

### Der Fenster-Ring

SERVO_WIN (0x31) ist neu in 4.11. 4.11 gibt außerdem Register 31 von SENSE
eine Bedeutung, `RESETS`, und macht `CAP_HOLD_MA` von SERVO_SENSE zu einem
vorzeichenbehafteten Register. Keine Page und kein Register wird verschoben.

SERVO_SENSE zeigt von jedem Kanal das letzte 50-ms-Fenster und kein anderes.
Ein Host, dessen Lesezugriffe mehr als 50ms auseinanderliegen, verpasst dort
ein Fenster. SERVO_WIN hält die letzten 4 vollständigen Fenster von INA3221
CH1, dem Servo im Test, 200ms. Ein Host, der jede Fensternummer einmal
nimmt, verpasst keines, solange zwei seiner Lesezugriffe höchstens 200ms
auseinanderliegen. Ein Lesen beendet nichts, eine auf dem Link verlorene
Antwort verliert also nichts. Die Page ist nur lesbar: jedes Schreiben wird
mit READ_ONLY abgewiesen.

| Register | Name | Liest |
| ---: | --- | --- |
| 0 | `WINDOW` | die Nummer des neuesten vollständigen Fensters, modulo 65536: die Nummer, die Register 12 von SERVO_SENSE zeigt |
| 1 | `FLAGS` | Bit 0 unter der geltenden Konfiguration hat sich ein Fenster geschlossen, `WINDOW` ist also eine Nummer; Bit 7 das Bit 7 von Register 13 von SERVO_SENSE, eine Messung der Bewegung lag an einem Ende des Bereichs |
| 2 | `CAP_STATE` | Register 18 von SERVO_SENSE, wie es in diesem Moment liest |
| 3 | `CAP_SEQ` | Register 19 von SERVO_SENSE |
| 4 + 6 k | `MEAN_MA` | Eintrag k, 0 bis 3, ist Fensternummer `WINDOW` - k modulo 65536: der mittlere Strom in mA, vorzeichenbehaftet |
| 5 + 6 k | `MAX_MA` | die höchste Strommessung in mA, vorzeichenbehaftet |
| 6 + 6 k | `MIN_MA` | die niedrigste Strommessung in mA, vorzeichenbehaftet |
| 7 + 6 k | `MEAN_MV` | die mittlere Busspannung in mV, auf der Lastseite des Shunts |
| 8 + 6 k | `MIN_MV` | die niedrigste Messung der Busspannung in mV |
| 9 + 6 k | `E_FLAGS` | Bit 15 mit dieser Nummer hat sich ein Fenster geschlossen; Bit 8 es enthält Strommessungen; Bit 9 es enthält Spannungsmessungen; Bit 10 eine Strommessung lag am oberen Ende des Bereichs, 163,8mV über dem Shunt; Bit 11 eine am unteren; Bits 0 bis 7 die Zahl der Strommessungen an einem Ende, bei 255 gehalten |

Die Page hat 28 Register. Register 0 bis 15 sind der Kopf und die zwei
neuesten Fenster: eine Anfrage und vier Daten-Frames, wie ein Lesen von
BENCH. Ströme sind auf das mA gerundet und auf -32767 bis 32767 begrenzt,
Spannungen auf 0 bis 65535mV. Der IOmcu liest den Strom von CH1 jede
1ms und seine Busspannung alle 24ms: ein Fenster enthält bis zu 50
Strommessungen und 2 oder 3 Spannungsmessungen.

Ein Eintrag mit gelöschtem Bit 15 liest in allen sechs Registern 0. Seine
Nummer hat sich nie geschlossen: der 1-ms-Takt des IOmcu lief 50ms
oder mehr zu spät und keine Messung fiel in dieses Fenster, oder die Nummer
liegt vor dem ersten Fenster der Konfiguration. Ein Eintrag mit gesetztem
Bit 15 und gelöschten Bits 8 und 9 hat sich ohne Messung geschlossen: der
INA3221 war nicht online.

Eine Strommessung am Ende des Bereichs zählt in einem Eintrag als der
Messwert, der sie ist, das Ende des Bereichs: 4095 Schritte von 40µV über
dem Shunt oben und 4096 unten, 1,638A und -1,6384A am 0,1-Ω-Shunt. Sie geht
mit diesem Wert in den Mittelwert, den höchsten und den niedrigsten Wert
ein, und die Bits 0 bis 7 zählen sie. Der Strom war mindestens so groß; um
wie viel größer, ist nicht bekannt. Die Register 0 bis 11 von SERVO_SENSE
lassen eine solche Messung aus ihren Werten heraus, die beiden Pages
unterscheiden sich also für ein Fenster mit einer Messung am Ende des
Bereichs, und nur für ein solches. Der IOmcu wendet auf die Zahl keine
Regel an.

`CAP_STATE`, `CAP_SEQ` und Bit 7 von `FLAGS` sind die von SERVO_SENSE, wie
jene Page im selben Durchlauf liest: ein Scharfschalten liest hier ab der
Bestätigung des Schreibens scharf (1), und eine beendete Messung zeigt sich
in dem Lesen, das die Fenster holt.

Nichts wird gehalten: nach einem Neustart des IOmcu liest alles 0,
ebenso nach einer angenommenen SENSE-Konfiguration, bis sich unter ihr das
erste Fenster schließt.

**Ein Bauteil, das sich selbst zurücksetzt.** Beide Strommonitore beantworten
nach einem eigenen Reset jedes Lesen, mit ihrer Einschalt-Konfiguration. Der
INA228 rechnet seinen Strom dann mit ADCRANGE 0: eine Konfiguration mit
ADCRANGE 1, jede, deren Shuntspannung beim Maximalstrom höchstens 40,96mV
beträgt, darunter die Voreinstellung des MATEK I2C-INA-BM, liest ein Viertel
des Stroms.
Der INA3221 wandelt alle drei Kanäle mit 1,1ms, ein CH1-Ergebnis alle
6,6ms, wo die Konfiguration des Prüfstands eines alle 280µs (CH1 allein)
bis 840µs (drei Kanäle) gibt. Der INA228 setzt sich zurück, wenn seine
Versorgung unter etwa 1,26V fällt (TI SLYS021A §7.4.2).

Der IOmcu liest ADC_CONFIG des INA228 und Configuration des INA3221
zurück, je einmal in 40ms. Ein anderer Wert als der geschriebene wird im
selben Takt ein zweites Mal gelesen, ein einzelnes verfälschtes Lesen ist
also kein Reset. Ein Rücklesen von FFFFh wird nicht wiederholt: ein Bus, den
kein Bauteil treibt, liest lauter Einsen, und das Bauteil gilt nach diesem
einen Lesen als verloren. Weichen beide Lesungen ab, oder ist das eine Lesen
FFFFh:

- das Bauteil verliert ONLINE in `FLAGS` von SENSE und wird 1000ms später
  erneut geprüft und konfiguriert;
- sein Byte von `RESETS` (SENSE-Register 31) zählt eins: der INA228 im
  Low-Byte, der INA3221 im High-Byte, je modulo 256, seit die geltende
  Konfiguration angenommen wurde. Der Zähler zeigt einen Reset, der zwischen
  zwei Lesungen von `FLAGS` gefunden und behoben wurde;
- die Fenster, die gerade aus dem Bauteil gefüllt werden, werden geleert.
  Beim INA3221 liest der SERVO_WIN-Eintrag dieses Fensters geschlossen und
  ohne Messung, ebenso die Einträge der nächsten 1000ms;
- beim INA228 lesen Ladung und Energie 0, und BENCH-Bit 6 bleibt bis zum
  nächsten Schalten auf ARMED gelöscht; Spannung, Strom und Leistung von BENCH
  lesen leer, solange das Bauteil offline ist;
- eine laufende Messung der Bewegung endet verloren (Zustand 8) und zählt in
  `CAP_SEQ`.

Die Messwerte selbst werden nicht auf lauter Einsen geprüft. Ein Strom- oder
Spannungsregister aus lauter Einsen liest sich als Code -1: -390µA an einem
INA228-Shunt von 200 µΩ, -400µA an einem INA3221-Shunt von 0,1Ω und -8mV
Busspannung des INA3221. Solche Messwerte stehen in den Fenstern, bis das
nächste Rücklesen das Bauteil offline nimmt, höchstens 40ms später.

Ein Reset wird innerhalb von 40ms gefunden. Die Messungen zwischen dem
Reset und seiner Entdeckung liegen im Fenster vor dem geleerten, wenn eine
Fenstergrenze dazwischen fällt: die des INA228 bei einem Viertel für eine
Konfiguration mit ADCRANGE 1, die des INA3221 im Wert richtig und je bis zu
6,6ms alt.

Die Rücklesungen haben eigene Plätze in der Rotation des Zeitplans aus 20
Plätzen, einer in jedem zweiten 1-ms-Takt, 40ms: ADC_CONFIG des INA228 und
Configuration des INA3221 mit je 25Hz, wie die Chip-Temperatur des INA228
und Mask/Enable des INA3221. Eine Rücklesung ist ein Lesen von 2 Byte,
120µs. Ein gerader Takt bleibt bei 690µs Buszeit; der Takt, in dem ein
Konfigurationsregister ein zweites Mal gelesen wird, hat 742,5µs.

| Panel | IOmcu | SERVO_WIN (0x31), `RESETS` und `CAP_HOLD_MA` |
| --- | --- | --- |
| 4.11 | 4.11 | bedient. Solange SENSE den INA3221 mit CH1 freigibt, liest das Panel dieses Builds SERVO_WIN bei jedem Poll und von SENSE die Register 12 bis 31, darunter `RESETS`. Es schaltet keine Messung scharf |
| 4.11 | 4.10 oder älter | der IOmcu beantwortet eine Anfrage an 0x31 mit BAD_PAGE; das Panel liest die Minor beim Link-up und sendet unter 11 nichts dorthin. Es nimmt die Fenster des INA3221 von SERVO_SENSE, das letzte je Lesen, liest den PD mini als Messgerät der Servo-Schiene und meldet einmal `IOmcu-Firmware älter als 0.16.0. Servostrom wird vom PD mini ausgelesen.`. Register 31 liest 0, und ein Bauteil, das sich zurückgesetzt hat, bleibt ONLINE mit seiner Einschalt-Konfiguration. Ein `CAP_HOLD_MA` von 32768 bis 65535 wird mit BAD_VALUE abgewiesen |
| 4.10 oder älter | 4.11 | das Panel liest 0x31 nie und liest Register 31 als reserviertes Register, das es übergeht. Ein Haltestrom, den es schreibt, 0 bis 32767mA, bedeutet dasselbe. Ein zurückgesetzt gefundenes Bauteil liest 1000ms lang offline und dann wieder online |

**Die Lesezugriffe des Panels.** `shared/bench/sense_link.c` nimmt jede
Fensternummer von CH1 einmal und gibt die Fenster weiter, das älteste
zuerst. Es liest SERVO_WIN, solange die Minor des IOmcu 11 oder mehr
ist und SENSE den INA3221 mit CH1 freigibt; ohne CH1 oder an einem älteren
IOmcu sind die Fenster die von SERVO_SENSE, das letzte je Lesen
(`shared/bench/sense_link_view.c`).

- Bei jedem Poll liest es die Register 0 bis 15, den Kopf und die zwei
  neuesten Fenster.
- Sind 100ms oder mehr vergangen, seit ein Lesen zuletzt kein Fenster
  schuldig blieb, kann ein drittes Fenster geschlossen sein, und gelesen
  wird die ganze Page, die Register 0 bis 27, an Stelle der Register 0 bis
  15. Jedes Fenster kommt dann aus einer Antwort. Einträge aus zwei
  Antworten werden nicht zusammengesetzt: ein Fenster, das dazwischen
  schließt, verschiebt jeden Eintrag um eine Nummer.
- Ein Lesen der Register 0 bis 15, dem mehr als zwei Fenster fehlen, gibt
  nichts weiter, und im selben Poll folgt ein Lesen der ganzen Page.
- Eine Fensternummer, die mehr als 4 hinter der neuesten liegt, hat den
  Ring verlassen. Sie wird als verloren gezählt und nicht weitergegeben.
  Bei Lesezugriffen im Abstand von höchstens 200ms geht keines verloren;
  bei 250ms Abstand eines von fünf.
- Ein Eintrag mit gelöschtem Bit 15 wird als kein Fenster weitergegeben und
  nicht gezählt. Eine übersprungene Nummer, die den Ring vor einem Lesen
  verlassen hat, zählt wie ein verlorenes Fenster: die Page sagt nicht, was sie war.
- Eine auf dem Link verlorene Antwort ändert nichts: dem nächsten Lesen
  fehlen dieselben Fenster und die seither geschlossenen.
- Das erste Lesen nach einem Link-up gibt nichts weiter. Die Weitergabe
  beginnt mit dem Fenster nach dem neuesten, das dieses Lesen zeigt; ein
  Fenster, das vor dem Verlust des Links genommen wurde, wird also nicht
  noch einmal genommen.
- Nach einer Konfiguration, die das Panel geschrieben hat, nach einem Lesen
  mit gelöschtem Bit 0 in `FLAGS` und wenn die neueste Nummer hinter der
  zuletzt genommenen liegt, hat der Ring neu begonnen: jeder geschlossene
  Eintrag des nächsten Lesens wird weitergegeben.
- Höchstens 4 Fenster warten auf die Control-Task. Sie nimmt sie in jedem
  Durchlauf, keines wartet also länger als einen Durchlauf.

CH2 und CH3 haben keinen Ring. Ihre Werte sind das letzte Fenster von
SERVO_SENSE, und das Log des Prüfstands schreibt sie in die Zeile, deren
Fensternummer dieses Lesen zeigte. Schließt ein Fenster zwischen dem Lesen
von SERVO_SENSE und dem von SERVO_WIN eines Polls, hat die Zeile des
dazwischen geschlossenen Fensters keine Werte von CH2 und CH3.

**Das Messgerät der Servo-Schiene.** `shared/bench/servo_source.c`
entscheidet einmal je Poll, welches Messgerät die Servo-Schiene misst. Der
Kanal des INA3221, der das Ziel des Servotesters misst, ist das Messgerät,
solange alles Folgende gilt:

| # | Bedingung |
| ---: | --- |
| 1 | SETUP SENSORS hat einen bestückten Baustein mit einem Kanal, der das Ziel des Servotesters misst; dieser Kanal ist CH1 des Bausteins |
| 2 | die Protokoll-Minor des IOmcu ist 11 oder mehr |
| 3 | die Register 0 bis 11 von SENSE sind die verlangte Konfiguration: kein Schreiben offen, kein Frame abgewiesen; mit SENSE_MAP sind sein Enable-Bit des INA3221 und die Register 8 bis 11 die der Zuordnung, und die Register 0 bis 25 von SENSE_MAP sind die verlangte Zuordnung: seit dem Link-Aufbau gelesen, nicht abgelehnt, kein Schreiben offen |
| 4 | das letzte Lesen von SENSE ist jünger als 200ms, und in seinen `FLAGS` sind die Bits 4 und 5 des INA3221 gesetzt und Bit 9 gelöscht |
| 5 | das neueste Fenster von SERVO_WIN hält Strommessungen, und seine Nummer hat sich vor weniger als 200ms bewegt |
| 6 | das Byte des INA3221 in `RESETS` hat sich seit dem Poll davor nicht bewegt |
| 7 | die Bedingungen 1 bis 6 gelten seit 1000ms ohne Unterbrechung |

Das Messgerät ist der PD mini ab dem ersten Poll, in dem eine von 1 bis 6
nicht gilt, und wieder der INA3221 1000ms, nachdem alle gelten. Antwortet
kein IOmcu, ist es das Modell des Panels. Ein übersteuertes Fenster
und ein negativer Strom sind Messwerte und keine Bedingung. Das Modul zählt
die Wechsel des Messgeräts, modulo 2^32. Die Antwort und dieser Zähler
stehen im Snapshot der Control-Task mit der Zahl der verlorenen Fenster, und
die Fenster liegen in einer Queue zur Render-Task, 8 tief; ist sie voll,
fällt das älteste weg. Jedes trägt die Antwort und den Zähler des Polls, der
es genommen hat; die Samples des Netzteils tragen sie in ihrer Queue
ebenfalls. Der SERVO-Bildschirm nimmt die Antwort des Snapshots, jedes
Fenster und jedes Sample des Netzteils einmal je Frame und zeigt den
Messwert des Messgeräts in seiner Zeile STROM, der Zeile über dem Plot und
dem Plot ([Screens](Screens-de.md#servo)). Von zwei Antworten behält er die
mit dem späteren Zähler. Ein Fenster, dessen Zähler hinter dem geltenden
Messgerät liegt, verwirft er, und ein Sample des Netzteils, dessen Zähler
dahinter liegt, ist kein Punkt des Plots: Snapshot und Queues sind drei
Wege, und jeder kann der älteste sein. Ein Zähler, der dem geltenden um
mehr als eins voraus ist, bedeutet Wechsel, die der Bildschirm nicht gesehen
hat, und leert den Plot, welches Messgerät es auch ist. Ein automatischer
Servotest liest das Messgerät, das bei seinem Start gilt, bis zu seinem
Ende: ein Lauf am INA3221 endet, wenn ein Fenster, ein Sample des Netzteils
oder der Snapshot ein anderes Messgerät oder einen anderen Wechselzähler
bringt ([Servo](Servo-de.md#der-strommesser-des-laufs)). Das Panel behält,
warum der INA3221 zuletzt aufhörte, das Messgerät zu sein, mit dem Zähler
jenes Polls (`servo_source_dropped()`) und reicht es mit jedem der drei
weiter, damit der Bericht des Laufs die Bedingung nennt: ein Reset-Zähler,
der sich in jenem Poll bewegte, ist der Grund, der behalten wird, weil der
IOmcu einen Baustein, den er zurückgesetzt findet, im selben Lesen
offline nimmt. Jedes Fenster trägt den spätesten Zeitpunkt, zu dem es
geschlossen haben kann: den Tick des Panels beim Lesen, abzüglich 50ms für
jedes Fenster nach ihm in diesem Lesen (`sense_link_win_t.taken_ms`).

`test_sense_windows` lässt den modellierten INA3221, den Zeitplan und die
Pages des IOmcu und `sense_link` an einer Uhr laufen: Polls im
Abstand von 50, 53, 55, 100, 150, 199 und 200ms ohne verlorenes Fenster,
von 201 und 250ms mit einer Zahl verlorener Fenster gleich den fehlenden
Nummern, eine verlorene Antwort in einem, zwei und drei Polls
hintereinander, die Fensternummer über 65535 hinweg, die Lesezugriffe über
den Überlauf des Ticks bei 2^32ms, eine geschriebene Konfiguration, ein
Neustart des IOmcu, ein verspäteter Takt des IOmcu, ein
verlorener und wiedergekehrter Link, ein Bauteil, das sich zurücksetzt, und
ein IOmcu mit 4.10. `test_servo_source` hält jede Bedingung, die
allein nicht gilt, ein Lesen, das mit 199ms frisch ist und mit 200 und
201ms nicht, den INA3221 zurück bei 1000ms und nicht bei 999ms, einen
zwischen zwei Lesungen behobenen Reset und jeden Timer über den Überlauf
des Ticks.

`shared/sense/sense_sched.c` hält den Ring und führt die Rücklesungen aus,
unter `test_sense_sched`: der Ring nach 1, 4, 5 und 6 Fenstern, über Fenster
65535 hinweg, mit einem um 49, 50 und 51ms und um 5 Fenster verspäteten
Takt, mit 0, 1, 44, 45 und 50 Messungen am Ende des Bereichs; ein Reset,
gefunden an jedem der 40 Takte einer Rotation, das Bauteil zurück bei
1000ms und nicht bei 999ms, über den Überlauf bei 2^32ms; die Buszeit
jedes Takts. `shared/outputs/sense_page.c` hält die Page, unter
`test_sense_page` und `test_link_pages`.

### Die Programmierports

PROG (0x33) ist neu in 4.13. Keine Page und kein Register wandert. Ein
Programmierport ist ein Ausgang eigener Art: er hat einen eigenen Pin, ist
kein Slot von OUTPUTS und hält keinen Kanal von CHANNELS. Sein Pin trägt
eines davon, nie zwei:

- SERVO PWM (Pulsweitenmodulation): ein Servopuls aus dem PWM-Generator, der
  auch die Standard-Servoports treibt. Die Funktion `FUNC` des Ports ist 0.
- KEINE (NONE, seit 4.26): kein Treiber. Der Pin ist für den Port als
  Eingang mit seinem Pull-down gehalten und gibt nichts aus. `FUNC` 1.
- DSHOT150, DSHOT300, DSHOT600 (seit 4.28): einfaches DShot mit 150, 300
  oder 600kbit/s aus einer eigenen State Machine der PIO (Programmable
  I/O), ein Frame alle 1ms. Der Port ist ein Throttle: MOTOR & ESC treibt
  ihn. `FUNC` 2, 3 und 4. Keine Telemetrie-Leitung: bidirektionales DShot
  gibt es auf einem Port nicht. Eine KST- oder HITEC-Sitzung weist einen
  DShot-Port mit NO_PIN ab: an dieser Leitung hängt ein ESC.
- PROGRAMMING: das Leitungsprotokoll für KST- und Chaservo-Servos, das die
  Servo-Programmier-Page (`KST`, 0x32) fährt, siehe
  [Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md); oder,
  seit 4.23, die Hitec-Leitung, die die Hitec-Page (`HITEC`, 0x34) fährt,
  siehe [Der Programmierport für Servos von Hitec](LinkHitec-de.md). Eine
  Page hält einen Port zur Zeit.

Die Page hält 4 Ports. Register 0 sagt, wie viele der Build hat: 4 ab
4.26, 1 davor. `shared/outputs/prog_port.c` hält die Regeln.

| Register | Name | Zugriff | Hält |
| ---: | --- | --- | --- |
| 0 | `COUNT` | lesen | die Ports dieses Builds, 0 bis 4 |
| 1 | `FLAGS` | lesen | Bit n: der PWM-Generator hat den Pin von Port n (SERVO PWM). Bit 4 + n (seit 4.26): der Pin von Port n ist geparkt (NONE). Bit 8 + n: Port n nennt einen Pin, den er nicht hält: der gespeicherte Pin war beim Start vergeben, das Silizium hat ihn abgewiesen, oder für sein DShot war keine PIO-State-Machine frei. Bit 12 + n (seit 4.28): das DShot von Port n hält seinen Pin |
| 2 | `PWM` | lesen, schreiben | Bit n: Port n gibt seinen Puls aus, solange die Bank für die Surfaces ARMED ist. Nicht gehalten |
| 3 | `TARGET` | lesen, schreiben | was Frame Rate, Sweep und Hold von SERVO treiben: 0 die Standard-Servoports, n + 1 Port n. Eine Änderung setzt die Frame Rate von SERVO auf 0 (seit 4.19). Nicht gehalten |
| 4 + 5 n | `PIN` | lesen, schreiben | der IOmcu-GPIO (General-Purpose Input/Output) von Port n, oder 0xFFFF für keinen. Gehalten |
| 5 + 5 n | `CMD` | lesen, schreiben | das Kommando, 0 bis 1000; ein höherer Wert wird als 1000 genommen. Auf einem DShot-Port mit BAD_VALUE abgewiesen, sein Kommando ist das Prüfstandsgas (seit 4.28). Nicht gehalten |
| 6 + 5 n | `SLEW` | lesen, schreiben | wie der Slew von CHAN_CFG: 0 bis 3000, ein höherer Wert wird mit BAD_VALUE abgelehnt, nichts wird gespeichert (seit 4.16). Auf einem DShot-Port nicht angewandt. Gehalten; ein gehaltener Slew über 3000 startet als 3000 |
| 7 + 5 n | `MIN_US` | lesen, schreiben | der Puls bei Kommando 0, 400 bis 2500µs, Standard 1000; solange das `PWM`-Bit des Ports gesetzt ist, höchstens der Frame der Port-Rate minus 1µs (seit 4.16). Gehalten |
| 8 + 5 n | `MAX_US` | lesen, schreiben | der Puls bei Kommando 1000, 400 bis 2500µs, Standard 2000; solange das `PWM`-Bit des Ports gesetzt ist, höchstens der Frame der Port-Rate minus 1µs (seit 4.16). Gehalten |
| 24 + n | `FUNC` | lesen, schreiben | seit 4.26: die Funktion von Port n, 0 SERVO PWM, 1 NONE; seit 4.28 auch 2 DSHOT150, 3 DSHOT300, 4 DSHOT600. Jeder andere Wert wird mit BAD_VALUE abgewiesen, und 2 bis 4 von einem IOmcu mit 4.26 oder 4.27. Standard 0 für Port 0, 1 für die anderen. Gehalten |

Der Puls eines Ports ist ein Kanal der Ausgangsbank: Port n ist Bankkanal
9 + n, über den 8 Kanälen von CHANNELS und dem Kanal 8 des Prüfstandsgases.
das Schalten auf ARMED, die Begrenzung auf 0 bis 1000, der Slew, die Endpunkte und
der Kommando-Timeout von 500ms sind die der Bank und wirken auf einen Port
wie auf einen Standard-Servoport. Ein SERVO-PWM-Port ist eine Surface: er gibt einen
Puls aus, solange die Bank für die Surfaces ARMED ist (ARM 1, 0x0004 oder
0x0006), sein `PWM`-Bit gesetzt ist, der Generator seinen Pin hat
und die Servo-Programmier-Page den Port in PWM hält.

Ein DShot-Port ist ein Throttle (seit 4.28): sein Bankkanal hat die Rolle
und die Ruhe des Throttles, 0. Das Prüfstandsgas auf CONTROL erreicht ihn,
wie es jeden als Motor gebundenen Slot erreicht, und der Port sendet alle
1ms einen Frame, solange die Bank für den Throttle ARMED ist (ARM 1,
0x0002 oder 0x0006): das Gas als DShot 48 bis 2047, oder 0 in Ruhe.
Solange die Bank nicht für den Throttle ARMED ist (SAFE, nur für die
Surfaces ARMED, STOP, Heartbeat-Verlust, Link-Stille), sendet der Port
keinen Frame, und der ESC läuft in seinen Timeout, wie an einem
Motor-Slot. `PWM`, `TARGET` und `CMD` nennen nie einen DShot-Port; sein
`SLEW`, `MIN_US` und `MAX_US` bleiben für SERVO PWM auf der Page und
werden nicht angewandt. Ein Wechsel der `FUNC` von oder zu DShot setzt den
Kanal auf die Ruhe der neuen Rolle. Der Port, den `TARGET`
nennt, läuft mit der Frame Rate von SERVO (Register 0 von SERVO), oder mit
50Hz, solange dieses 0 liest. Jeder andere Port läuft mit 50Hz, und die
PWM-Slots laufen mit ihren eigenen OUTPUTS-Raten, solange ein Port das Ziel
ist (seit 4.19).

Der Servotester erreicht einen Port auf drei Wegen:

- `CMD` ist das manuelle Kommando. Es wird mindestens alle 500ms
  geschrieben, wie ein Kanal von CHANNELS.
- `TARGET` richtet Frame Rate, Sweep, Hold und RESUME von SERVO auf einen
  Port. Eine Änderung von `TARGET` beendet einen laufenden oder gehaltenen
  Sweep wie ein SWEEP von 0 und setzt die Frame Rate von SERVO auf 0 (seit
  4.19); das neue Ziel startet also mit 50Hz oder, für die
  Standard-Servoports, mit der eigenen Rate jedes Slots. Eine Änderung weg
  von den Standard-Servoports, solange SERVO eine andere Rate als 0 liest,
  wird mit BAD_VALUE abgewiesen, wenn ein PWM-Slot zurück auf seiner eigenen
  Rate einen Endpunkt hätte, der länger als dieser Frame ist.
- Ein Capture von SERVO_SENSE nennt einen Port mit Bit 11 seines
  Arm-Registers (seit 4.13): die Bits 8 bis 10 sind dann der Port, kein
  Ausgangskanal. Mit BAD_VALUE abgewiesen für einen Port, der keinen Puls
  ausgibt. Ein Port, dessen Puls endet, beendet ein Capture, das nicht
  fertig ist.

Pins. Der Pin eines Ports ist nie der Pin eines Slots. Ein Schreiben von
`PIN` wird mit BAD_VALUE abgewiesen für einen Pin, der reserviert ist,
hinter GPIO 63 liegt, einem Slot gehört, von SUPPLY, SENSE oder TONE gehalten
wird oder einem anderen Port gehört. Für einen SERVO-PWM-Port wird er auch
abgewiesen für einen Pin hinter GPIO 47 (kein PWM-Slice), einen, der einen
PWM-Slice oder ein Compare-Register mit dem Pin eines PWM-Slots teilt, und
einen auf dem Compare-Register eines anderen SERVO-PWM-Ports; der Pin eines
NONE- oder DShot-Ports steht unter keiner der PWM-Regeln. Ein DShot-Port
braucht einen PIO-Block, der seinen Pin erreicht, und darin eine freie
State Machine; ohne sie hält der Port keinen Pin, und Bit 8 + n von
`FLAGS` ist gesetzt. Die Ausgangs-Slots gehen vor: ein Schreiben von
OUTPUTS oder BIND, das einen DShot- oder PPM-Slot neu bindet, lässt jeden
DShot-Port los, bindet die Slots und bindet die Ports wieder auf den State
Machines, die übrig sind. Der DShot-Treiber des IOmcu hält 12 Pins, die 8
Slots und die 4 Ports. Ein Schreiben von `FUNC` oder `PIN`, das einen
DShot-Port bindet, wird mit BAD_VALUE abgewiesen und ändert nichts,
solange der PD-mini-UART der Versorgung auf sein Speichern wartet und der
Port ihm keinen PIO-Platz ließe, wie ein Schreiben eines Slots oder von
TONE. Ein Pin, den der Chip nicht
hat, wird abgewiesen, wenn der IOmcu ihn nicht parken kann, und das
Schreiben wird zurückgenommen; ein Schreiben von
OUTPUTS oder BIND, das einen Slot auf den Pin eines Ports oder im Silizium
neben ihn legt, wird ebenso abgewiesen, und auch ein Schreiben von SUPPLY,
SENSE oder TONE, das ihn nimmt. Kanäle. Der Kanalbereich eines Slots reicht
nie in die Bank-Kanäle 9 bis 12 der Ports: ein Schreiben von OUTPUTS oder
BIND mit einem solchen Slot wird mit BAD_VALUE abgewiesen, und ein
gespeicherter Slot mit einem solchen Bereich startet ungebunden. Zwei Ports
dürfen einen Slice teilen. Der RP2350 betreibt beide Kanäle eines PWM-Slices
mit einem Teiler und einem Wrap-Register, beide Pins laufen also mit einer
Rate. Ein Port, dessen Pin seinen Slice mit dem Pin eines anderen gehaltenen
Ports teilt, nimmt als Rate von SERVO nur 0 oder 50Hz, jede andere wird mit
BAD_VALUE abgewiesen; und solange der `TARGET`-Port mit einer anderen Rate
als 50Hz läuft, wird ein Schreiben von `PIN`, das einen anderen Port auf
seinen Slice legt, mit BAD_VALUE abgewiesen (beides seit 4.19). Weder `PIN`
noch `FUNC` ändern sich, solange die Bank ARMED ist, das `PWM`-Bit des
Ports gesetzt ist, er das `TARGET` ist (vor oder nach dem Frame) oder eine
Leitungs-Page (KST, HITEC) einen Port hält; ein `PIN` wird unter der `FUNC`
beurteilt, die der Frame lässt. `PWM` und `TARGET` nennen nur SERVO-PWM-Ports,
deren Pin der Generator hat, und keinen Port, dessen `PIN` oder `FUNC`
derselbe Frame ändert. Ein Schreiben, das ein Register eines Ports erreicht, den
der Build nicht hat, wird mit BAD_VALUE abgewiesen, ein Schreiben von
Register 0 oder 1 mit READ_ONLY. Ein abgewiesenes Schreiben ändert nichts, auch
nicht Frame Rate und Sweep von SERVO.

Ein IOmcu, der keinen Port gespeichert hat, hat Port 0 auf GPIO 13, Pad
17 des Bring-up-Moduls, und die anderen Ports ohne Pin. Der Slice-Partner
von GPIO 13, GPIO 12, ist reserviert, und der Pin mit seinem Compare-Register,
GPIO 29, liegt nicht auf der Stiftleiste. GPIO 13 ist deshalb kein Pin eines
Slots, bis der `PIN` von Port 0 auf einen anderen Pin oder auf 0xFFFF
geschrieben ist. Ein gespeicherter Pin, den beim Start ein Slot, SUPPLY,
SENSE oder TONE hält, bleibt deren: der Port hält keinen Pin, und Bit 8 + n
von `FLAGS` ist gesetzt, bis ein anderer `PIN` geschrieben ist.

Die Funktionen werden in einem Wort des Speicherdatensatzes gehalten, dem
Wort vor den Registern der Ports, das bis 4.26 frei war (`prog_func`): die
Bits 4n bis 4n + 3 halten 1 + die `FUNC` von Port n, 0 keine gespeichert.
Ein Datensatz ohne eine liest sich als Port 0 SERVO PWM und die anderen
NONE. Das Wort hat keine eigene Version: Datensätze der Version 7 und 8
tragen es an derselben Stelle.

Eine DShot-Funktion wird als 3 bis 5 gespeichert.

| Image, auf das die Platine wechselt | Was es mit dem Wort tut | Zurück auf 4.26 oder neuer |
| --- | --- | --- |
| 4.26 oder 4.27 | liest 3 bis 5 eines DShot-Ports als keinen Wert, den es nimmt: der Port startet auf seiner Standardfunktion und behält seinen Pin, außer die Standardfunktion ist SERVO PWM und der Pin ist kein PWM-Pin | der Port steht auf der Funktion, die das ältere Image zurückgeschrieben hat, der Standardfunktion |
| 4.25 | liest es als frei und schreibt es als 0 zurück | die Ports behalten ihre Pins und stehen auf den Standardfunktionen; ein gehaltener Pin, den ein SERVO-PWM-Port nicht halten kann (hinter GPIO 47), fällt weg |
| älter als 4.25 | liest keinen Datensatz der Version 8 und startet mit leerem Speicher | jeder Port steht auf seinem Standardpin und seiner Standardfunktion |

Das Panel schreibt `PIN` und `FUNC` nur, wenn der Bediener darum bittet,
auf dem Eintrag PROG-PORT von OUTPUTS oder in PICK A PIN
([Bildschirme](Screens-de.md#der-eintrag-prog-port)), nie aus einer
gespeicherten Einstellung: die Ports gehören zur Platine, wie die Bindung
der Ausgänge. Eine Bitte wartet auf einen Prüfstand in Ruhe. Ein Port, der
das `TARGET` ist, wird zuerst losgelassen (sein `PWM`-Bit aus, `TARGET`
0), und das Ziel, das der Bildschirm will, wird zurückgeschrieben, nachdem
die Page erneut gelesen ist.

`PWM` und `TARGET` werden nicht gespeichert. Ein IOmcu, der neu startet,
liest in beiden 0: die Standard-Servoausgänge sind das Ziel, und kein
Port pulst. Das Panel schreibt das Ziel, das der SERVO-Bildschirm will,
auf einem Prüfstand in Ruhe zurück, sein `PWM`-Bit danach, innerhalb von
500ms, nachdem der Link wieder steht oder das Lesen die Änderung zeigt.
Eine Position, die vor dem Neustart an den Port ging, wird nicht erneut
gesendet: die Freigabe, die für sie offen ist, geht an die
Standard-Servoausgänge, die der IOmcu dann nennt, und ohne gebundene
Surface dort gilt sie ohne Schreiben als bezahlt. Ein Prüfstand, der
ARMED war, stoppt mit dem Linkverlust (STOP); der Port gibt keinen Puls
aus, bis der Bediener wieder auf ARMED schaltet, und das Schalten auf ARMED
zentriert zuerst das geltende Ziel. `test_servo_link_restart` fährt einen Neustart in
SAFE und in ARMED, während einer Bitte, einen, den das Panel nicht als
Linkverlust sieht, zwei hintereinander, einen während eines laufenden
automatischen Servotests, dessen Freigabe ohne Antwort bleibt, einen
Linkverlust ohne Neustart und eine Programmiersitzung, die das Ziel
genommen hat.

| Panel | IOmcu | PROG (0x33) |
| --- | --- | --- |
| 4.13 | 4.13 | bedient |
| 4.13 | 4.12 oder älter | der IOmcu beantwortet eine Anfrage an 0x33 mit BAD_PAGE; das Panel liest die Minor beim Link-up und sendet unter 13 nichts dorthin |
| 4.12 oder älter | 4.13 | das Panel liest und schreibt 0x33 nie: kein Port gibt einen Puls aus, und Port 0 hält GPIO 13 |
| 4.26 | 4.13 bis 4.25 | das Panel liest 24 Register und nimmt jeden Port als SERVO PWM; es erbittet keine `FUNC` und sagt `IOMCU ÄLTER ALS 0.22.0: NUR SERVO PWM` |
| 4.13 bis 4.25 | 4.26 | das Panel liest 24 Register und schreibt `FUNC` nie; ein Port, den es als Ziel setzt, muss SERVO PWM sein, sonst weist der IOmcu das `TARGET` ab |
| 4.28 | 4.26 oder 4.27 | das Panel bietet nur SERVO PWM und NONE an und erbittet keine DShot-`FUNC` |
| 4.26 oder 4.27 | 4.28 | das Panel zeigt einen DShot-Port als NONE und setzt ihn nie als Ziel; das Gas von MOTOR & ESC erreicht ihn trotzdem |

`test_prog_port_fn` fährt die Funktionen: die fünf Werte angenommen und
5, 15, 32768 und 65535 abgewiesen, den Pin eines NONE- oder DShot-Ports
unter den Pin-Regeln und keiner der PWM-Regeln, den Kanal eines
DShot-Ports als Throttle in Ruhe ohne Slew, sein `CMD` abgewiesen, `PWM`
und `TARGET` auf ihm abgewiesen, FLAGS-Bit 12 + n, die gehaltenen Nibbles
3 bis 5, einen Funktionswechsel unter jeder
Sperre, SERVO PWM abgewiesen auf einem Pin, den der Generator nicht nehmen
kann, das Park-Flag, das gehaltene Wort und einen Datensatz, den ein
älteres Image zurückgeschrieben hat. `test_prog_link_ask` fährt die Bitten
des Panels. `test_prog_port` fährt die Page: jede Pin-Abweisung in beiden Richtungen,
die Endpunkte bei 399, 400, 2500 und 2501µs, den Slew bei 3000 und 3001 und
einen gehaltenen Slew von 3001 und 65535, eine Pin-Änderung bei einer
Bank in ARMED, einem gesetzten `PWM`-Bit und einem `TARGET`, 0 und 5 Ports
an einem Modell mit 4, einen gespeicherten Pin, der beim Start vergeben
ist, die Rate von SERVO, die nur den Ziel-Port erreicht, eine Änderung des
Ziels, beurteilt bei 50Hz, einen Pin, der nicht auf den Slice eines Ziels
mit eigener Rate darf, und eine Änderung des Ziels, die einem Slot einen
Puls länger als sein Frame ließe. `test_outputs_hw_ports` fährt einen
DShot-Port: gebunden mit 150, 300 und 600kbit/s, bewegt von einem
Wechsel der Rate oder Funktion, keine State Machine mehr frei, einen neu
gebundenen DShot-Slot, der die State Machine des Ports nimmt, ein Frame
alle 1ms nur in ARMED für den Throttle, das Gas bei 1000 als 2047, keiner
bei SAFE, ARM SURFACE und STOP, und die Periode über den Überlauf der
Mikrosekunden-Uhr. Kein Puls und kein Frame eines Ports ist an Hardware
gemessen.

PIO-Budget. Der RP2350 hat 3 PIO-Blöcke mit je 4 State Machines und 32
Instruktionen. Das pico-sdk lädt für jeden Pin eine eigene Kopie eines
Programms. Nach den erzeugten Programmlängen braucht ein einfacher
DShot-Pin 1 State Machine und 4 Instruktionen, ein bidirektionaler 2 und
16, eine Hitec-Leitung 1 und 29 und ihr Precharge-Sampler 1 weitere und
bis zu 12. Zwei bidirektionale Slots, zwei DShot-Ports und eine
Hitec-Leitung mit Precharge brauchen 8 von 12 State Machines und 81 von 96
Instruktionen; die Rechnung ist nicht an Hardware gelaufen. Ein Block
erreicht GPIO 0 bis GPIO 31 oder GPIO 16 bis GPIO 47, ein Pin über GPIO 31 kann also
früher abgewiesen werden.

### Der Programmierport für Servos von KST und Chaservo

Die Servo-Programmier-Page (`KST`, 0x32) hat [eine eigene Seite](LinkKST-de.md):
die Versorgung, das Verlassen ohne Aus- und Einschalten der Versorgung, die
Register, und was STOP, ein Schalten auf SAFE, Heartbeat-Verlust und Link-Verlust
mit einer Session machen.

### Der Programmierport für Servos von Hitec

Die Hitec-Page (`HITEC`, 0x34) hat [eine eigene Seite](LinkHitec-de.md):
hinein und hinaus, die Transaktionen, POWER, was ein Stopp mit einem Port
macht, und die Register.

### Die INA3221-Map und ihre Fenster

SENSE_MAP (0x35), SENSE_LIMIT (0x36), SENSE_WIN_A (0x37) und SENSE_WIN_B
(0x38) haben [eine eigene Seite](LinkSensors-de.md): bis zu 4 INA3221 und
ihre 12 Kanäle, der FAST-Kanal, dem SERVO_WIN und eine Messung auf
SERVO_SENSE folgen, wie SENSE eine geltende Map spiegelt, die Stromgrenzen,
das Fenster jedes Kanals, und der Flash-Record über zwei Pages.

### Identifier

Ein 29-Bit-Extended-Identifier trägt die ganze Adresse; ein Read ist deshalb
ein Frame ohne Nutzdaten.

| Bits | Feld | Breite | Werte |
| --- | --- | ---: | --- |
| 28..26 | Priorität | 3 | 0 control (die Pages CONTROL, LIMITS, FAILSAFE und SUPPLY, ihre Quittungen eingeschlossen), 1 normal (darunter SENSE, SERVO_SENSE und TONE), 2 bulk (reserviert); niedriger gewinnt die Arbitrierung |
| 25..22 | op | 4 | 1 READ, 2 WRITE, 3 DATA, 4 ACK (Acknowledge), 5 NACK (Negative Acknowledge) |
| 21..14 | page | 8 | Page Map unten |
| 13..6 | offset | 8 | erstes Register in diesem Frame |
| 5..0 | count | 6 | Register in diesem Frame, 0..32 |

Die Priorität folgt aus der Page: die Pages CONTROL, LIMITS und FAILSAFE und
ihre Quittungen sind Klasse 0; alles andere ist Klasse 1.

### Frames

Ein Frame trägt bis zu vier Register (8 Byte, Little-Endian). Jeder Frame
trägt seinen eigenen Offset und Count; eine Antwort über mehr als vier
Register besteht also aus mehreren unabhängigen Frames in beliebiger
Reihenfolge, und ein verlorener Frame kostet einen Registerbereich. Der
Host-Poller kennt das angefragte Fenster und ist fertig, sobald jedes
Register eingetroffen ist; der Transport setzt nichts zusammen. In den
Nutzdaten steckt keine CRC (Cyclic Redundancy Check, Prüfsumme); es gelten
die 15-Bit-CRC, der Acknowledge-Slot und die Wiederholung von CAN.

Ein NACK trägt seinen Grund in Register 0:

| Wert | Grund |
| ---: | --- |
| 1 | BAD_PAGE |
| 2 | BAD_RANGE: Offset + Count über das Ende der Page hinaus |
| 3 | READ_ONLY |
| 4 | BAD_VALUE |
| 5 | NOT_ARMED |
| 6 | BUSY: die Page nimmt den Wert, und etwas anderes hält vorerst, was er nennt; derselbe Write wird angenommen, sobald es frei ist. Nur TONE, seit 4.17 ([Ein belegter Pin am Phasenabgriff](#ein-belegter-pin-am-phasenabgriff)) |

Ein Write gilt ganz oder gar nicht. Jedes Register eines Frames wird geprüft,
bevor eines davon gespeichert wird; ein NACK lässt die Page also genau so, wie
sie war: keines der bis zu vier Register eines Frames wird übernommen, und
keine Nebenwirkung eines Registers läuft. Das Aufheben eines eingerasteten
Failsafe ist eine solche Nebenwirkung.

Ein NACK ist eine Antwort, kein Linkfehler. Das Panel zählt sie in seinem
Link-Bericht (`nack`, [Inbetriebnahme](Bringup-de.md#link-bericht)): eine
Einstellung aus SETUP INTERFACES, die eine Page ablehnt, ist ein NACK je
abgelehntem Wert, einmal geschrieben, bis sich der Wert ändert, und ein Pin,
den eine Page als anderweitig gehalten ablehnt, eines je Angebot alle 5s.

### Page Map

| Page | Name | Zugriff | Register |
| ---: | --- | --- | --- |
| 0x00 | IDENTITY | lesen | Protokoll major, Protokoll minor, Firmware major, minor, patch, Board (1 das von Hand verdrahtete RP2350-CAN, 2 der rbmod-Carrier; [Boards mit verlöteten Pins](#boards-mit-verlöteten-pins)), Capabilities-Bitmap |
| 0x01 | STATUS | lesen | 10 Register (seit 4.21; davor 8): Zustand (0 idle, 1 armed, 2 failsafe), Fault-Bitmap, Uptime in ms (zwei Register), angenommene Anfragen (zwei Register), Empfangsfehlerzähler des XL2515, Sendefehlerzähler des XL2515, seit dem Einschalten abgelehnte CAN-Frames (zwei Register, niederwertige Hälfte zuerst; [Abgelehnte Frames](#abgelehnte-frames)) |
| 0x10 | CONTROL | lesen, schreiben | ARM (0 schaltet auf SAFE; 1 schaltet jeden Ausgang auf ARMED; ab 4.18 schaltet 0x0002 die Throttle-Kanäle, 0x0004 die Surfaces und die SERVO-PWM-Programmierports, 0x0006 beide auf ARMED; ein DShot-Programmierport ist ein Throttle (seit 4.28); jeder andere Wert wird mit BAD_VALUE abgewiesen, [Ein Arm für eine Rolle](#ein-arm-für-eine-rolle)), THROTTLE (0..10000, Hundertstel Prozent, und kommandiert jeden Kanal, den CHAN_CFG als Throttle führt, und seit 4.28 jeden DShot-Programmierport), MOTOR_POLES, CLEAR (0x5AFE schreiben, um das Failsafe zu verlassen). Register 0 bis 2 sind der Frame, der auf ARMED schaltet |
| 0x11 | LIMITS | | deklariert, nicht bedient |
| 0x12 | FAILSAFE | | deklariert, nicht bedient |
| 0x13 | CHANNELS | lesen, schreiben | ein Kommando je Ausgangskanal, 0..1000 des Kanalwegs; acht Kanäle |
| 0x20 | BENCH | lesen | Spannung (10mV), Strom (10mA), Leistung (W), Drehzahl (min⁻¹), Temperatur des ESC (Electronic Speed Controller, Motorregler) und des Motors (0,1°C, vorzeichenbehaftet), Ladung (mAh), Energie (0,1Wh), Minimalspannung, Maximalstrom, Maximalleistung, Maximaldrehzahl, Flags |
| 0x21 | reserviert | | nicht vergeben; nicht wiederzuverwenden |
| 0x22 | OUTPUTS | lesen, schreiben | je Slot: Treiber (0 keiner, 1 PWM (Pulsweitenmodulation), 2 PPM (Pulspositionsmodulation), 3 DShot, 4 bidirektionales DShot), Pin, erster Kanal und Kanalzahl in einem Register, Rate in Hz (kbit/s für beide DShot-Treiber); acht Slots zu vier Registern |
| 0x23 | CHAN_CFG | lesen, schreiben | je Kanal: Rolle (0 throttle, 1 surface), Slew (Tausendstel der Spanne je Sekunde, 0 bis 3000, 0 = sofort; ein höherer Wert wird mit BAD_VALUE abgelehnt und nichts wird gespeichert (seit 4.16); ein Slew über 3000, den ein älterer Build im Flash des IOmcu hinterlassen hat, startet als 3000), minimaler und maximaler Puls in µs, 400 bis 2500 und auf einem Kanal, den ein PWM-Slot ausgibt, höchstens dessen Frame minus 1µs (seit 4.16); acht Kanäle zu vier Registern |
| 0x24 | CATALOGUE | lesen | die eigenen Pins der Platine, je ein Register: GPIO-Nummer (General-Purpose Input/Output) in 6 Bit, die daneben aufgedruckte Pad-Nummer in 6, was ihn hält in 4 (0 frei, 1 Heartbeat, 2 CAN, 3 Flash, 4 Debug, 5 Sensor, 15 sonstiges); 32 Slots, und eine Pad-Nummer von 0 heißt, in diesem Slot ist kein Pin |
| 0x25 | SHAPE | lesen | wo diese Pads liegen: Umriss-Breite und -Höhe in 0,01mm, die Ecke, an der Pad 1 sitzt, und die Pads in einer Reihe gepackt als (Ecke << 8) \| je Reihe, das Raster in 0,01mm und der Abstand von der Kante zur Mitte einer Pad-Reihe. Zwei Reihen auf einem Raster, nummeriert von Pad 1 weg entlang seiner Kante und zurück entlang der gegenüberliegenden. Alles null, wenn der IOmcu keine Form für seine Platine hat — dann wird sie gelistet und nicht gezeichnet |
| 0x26 | ARTWORK | lesen | was die Zeichnung der Platine ist, 9 Register (seit 4.20; davor 7): Blöcke Nutzdaten (0, wenn der IOmcu keine trägt), Breite und Höhe des Umrisses in 0,01mm, Format (0 keines, 2 die Zeichnung der Platine; 1, ein RGB565-Foto, liefern nur 4.19 und älter), Nutzdatenlänge in zwei Registern, eine CRC (zyklische Redundanzprüfung) über die gesamten Nutzdaten mit Startwert null und ein FNV-1a-Hash (Fowler-Noll-Vo) über sie in zwei Registern, die niederwertige Hälfte zuerst. [Die Zeichnung der Platine](#die-zeichnung-der-platine) hat das Layout |
| 0x27 | ART_DATA | lesen, schreiben | die Zeichnung selbst: Register 0 schreiben, um den Block zu nennen, dann die Page lesen. Register 0 liest den gerade bedienten Block zurück, Register 1 bis 31 tragen 62 Bytes davon. Ein Block rückt beim Lesen nicht vor, eine verlorene Antwort wird also erneut angefordert statt übersprungen |
| 0x28 | PADS | lesen | die Pads, die keine Pins sind, je ein Register: die Pad-Nummer in 6 Bit, was es ist in 2 (0 kein Pad und die Liste endet, 1 Masse, 2 eine Versorgung, 3 keines von beiden) und die Spannung in 8 Bit zu Zehntelvolt. Null Volt bei einer Versorgung heißt, sie ist keine feste Spannung — das ist nicht dasselbe wie die 0V einer Masse. 32 Slots, in Pad-Reihenfolge |
| 0x29 | SERVO | lesen, schreiben | Register 0: die Frame Rate in Hz des PWM-Ziels, das `TARGET` von PROG nennt, 40 bis 560, oder 0 für seine eigene Rate. Mit den Standard-Servoports als Ziel: jeder PWM-Ausgang, dessen erster Kanal die Rolle surface hat, 0 für die eigene Rate jedes Slots aus OUTPUTS; mit BAD_VALUE abgewiesen, und nichts ändert sich, wenn ein PWM-Slice damit zwei Raten hätte, und solange sie nicht 0 ist, ebenso ein Schreiben auf CHAN_CFG oder OUTPUTS, das das täte (seit 4.1). Mit einem Programmierport als Ziel: nur der Pin dieses Ports, 0 für 50Hz (seit 4.19, [Die Frame Rate des Ziels](#die-frame-rate-des-ziels)). Ebenso abgewiesen, wenn ein PWM-Ausgang oder ein pulsender Programmierport damit einen Endpunkt hätte, der länger als sein Frame ist (seit 4.16, [Ein Puls nicht länger als sein Frame](#ein-puls-nicht-länger-als-sein-frame)). Eine Änderung von `TARGET` setzt Register 0 auf 0 (seit 4.19). Register 1 bis 4, ein Frame: ein Sweep der Surfaces, Kurve (0 gestoppt, 1 Rechteck, 2 Sinus, 3 Dreieck), Geschwindigkeit in Tausendstel Zyklen je Sekunde (50 bis 5000), Amplitude in Befehlseinheiten zu jeder Seite der Mitte (0 bis 500), Haltezeit an jedem Ende in ms (0 bis 5000); ein Sweep startet oder ändert sich nur, wenn alle vier zusammen geschrieben werden, sonst mit BAD_VALUE abgewiesen, und bei einem Prüfstand in SAFE oder nur für Throttle in ARMED mit NOT_ARMED (seit 4.18); 0 in Register 1 allein hält ihn an, und 4 allein hält jede Surface dort, wo ihr Ausgang gerade ist, unter denselben Regeln für das Schalten auf SAFE und 500ms. Ein Sweep, der beim Beginn des Haltens läuft, behält seine Phase (seit 4.6): wie weit er in der Kurve war, und damit seinen Punkt, sein Verweilen und die erreichten Enden; einer, der seine Bewegungen gemacht hat, wird in der Mitte gehalten, auf der er endete. Ein Schreiben wird an der Page gemessen, wie der nächste Durchlauf des IOmcu sie in diesem Moment hinterließe: ein Sweep nach seiner letzten Bewegung, 500ms ohne Schreiben oder Schalten auf SAFE ist beendet, auch wenn das Schreiben vor dem Durchlauf bedient wird, der ihn beendet. 5 allein (RESUME, seit 4.6) setzt diesen Sweep ab der behaltenen Phase fort; die Surfaces werden sofort wieder entlang der Kurve angesteuert und fahren mit ihrer eigenen Rate von dort hin, wo sie gehalten wurden. RESUME wird mit BAD_VALUE abgewiesen, wenn keine Phase behalten ist (beim Beginn des Haltens lief kein Sweep, oder das Halten endete durch ein Schreiben von 0, eine darübergeschriebene Kurve, ein Schalten auf SAFE, 500ms ohne Schreiben oder einen Neustart), und wenn Register 2 bis 5 etwas anderes lesen, als der angehaltene Sweep fährt; mit NOT_ARMED bei einem Prüfstand in SAFE oder nur für Throttle in ARMED. Ein IOmcu mit 4.5 weist 5 mit BAD_VALUE ab. Register 5: wie viele Enden ein danach gestarteter Sweep erreicht, bevor er anhält, 0 für kein Ende. Register 6, nur lesen: die Enden, die der Sweep erreicht hat. Ein Sweep hält bei einem Schreiben von 0, beim Schalten auf SAFE und wenn er 500ms nicht geschrieben wurde an und lässt jede Surface dort, wo ihr Ausgang gerade ist; Wiederholen hält seine Kurve am Laufen, und ein Sweep, der seine Bewegungen gemacht hat, startet durch Wiederholen nicht neu (seit 4.2). Nichts wird gespeichert: Nach einem Neustart des IOmcu steht überall 0 |
| 0x2A | SUPPLY | lesen, schreiben | ein WeAct PD Power Mini V1 Buck an einem PIO-UART (PIO: Programmable Input/Output) auf zwei Pins des IOmcu (seit 4.3). Register 0 bis 3, ein Frame: Freigabe, der GPIO, der sendet (zum DM des Moduls), und der, der empfängt (sein DP), und die UART-Baudrate-Einstellung des Moduls, 0 bis 6 für 9600, 19200, 38400, 57600, 115200, 230400 und 460800 Baud, oder 7 (seit 4.4), um sie zu finden: der UART versucht nach jedem WHO_AM_I ohne gültige Antwort die nächste Rate, beginnend bei 19200, bis ein Modul einmal geantwortet hat; mit BAD_VALUE abgewiesen bei einem Pin, der reserviert, an einen Ausgang gebunden oder der andere Pin ist, und bei jeder Änderung, solange der Ausgang an verlangt ist oder an sein kann (Bit 6 der Flags). Seit 4.5 wird eine Änderung, nachdem ein Modul geantwortet hat, quittiert und zurückgehalten: Register 0 bis 3 lesen die geltende Verdrahtung, und Flag-Bit 8 ist gesetzt, bis eine nach der Änderung gesendete Zustandslesung des Moduls antwortet, höchstens etwa 1,2s; zeigt sie den Ausgang aus, wird die Änderung übernommen, zeigt sie ihn an oder schlägt sie fehl, wird sie abgewiesen, und Bit 9 ist gesetzt bis zum nächsten Schreiben von Register 0 bis 3. Eine solche Änderung kommt allein, ohne Register 4 bis 6, und ein ON wird mit BAD_VALUE abgewiesen, solange eine zurückgehalten wird. Ein Host älter als 4.5, der die Verdrahtung währenddessen noch einmal schreibt, wird quittiert, und das Zurückhalten geht weiter. Die Pins gehören keinem Ausgang, solange sie gehalten werden. Register 4 bis 6, ein Frame: Ausgang (1 an) und die Sollwerte in mV und mA, bis 20000mV und 3000mA; ein ON wird ohne lebenden Heartbeat mit NOT_ARMED abgewiesen, ebenso solange eine eben geschriebene Verdrahtung noch nicht im Flash ist (gespeichert wird 400ms nach dem letzten Schreiben), und der Ausgang geht aus, wenn der Heartbeat ausbleibt. Register 7 bis 16, nur lesen: Flags (Bit 0 online, Bit 1 Ausgang an, Bits 3..2 Modus 0 normal, 1 Konstantstrom, 2 Überstrom, Bit 4 ein Ausgang, der nicht schalten wollte, Bit 5 Sollwerte, die nicht übernommen wurden, Bit 6 ein Ausgang, der an ist oder an sein kann (an gelesen, ein ON noch nicht bestätigt oder ein OFF, das einem Modul geschuldet ist, das aufgehört hat zu antworten), Bit 7 ein Ausgang, den das Modul selbst abgeschaltet hat, während ON verlangt war; aus gehalten, bis OUTPUT mit 0 geschrieben wird; seit 4.5 Bit 8 eine Verdrahtungsänderung, die auf eine Zustandslesung wartet, und Bit 9 eine, die diese Lesung abgewiesen hat, Bit 10 ein Ausgang, abgeschaltet, weil der Eingang des Moduls bei 2 Lesungen hintereinander unter dem Sollwert plus 500mV lag; aus gehalten, bis OUTPUT mit 0 geschrieben wird, seit 4.13 Bit 11 ein Programmierport hält die Register 4 bis 6: ein Schreiben, das kein OFF ist, wird mit BAD_VALUE abgewiesen (seit 4.15 jede Änderung eines Sollwerts; davor ein ON und ein Heben der Spannung), siehe [Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md); seit 4.31 wartet ein OFF, das geschrieben wird, während Bit 11 gesetzt ist, auf das Schreiben des Ports in den Speicher des Servos, höchstens 10.000ms nach dem OFF, und das eigene ON des Ports wird danach abgewiesen, bis Bit 11 erlischt, siehe [Sicherheit, Die Versorgung eines Programmierports](Safety-de.md#die-versorgung-eines-programmierports)), mV und mA am Ausgang, die zurückgelesenen Sollwerte, Zustand und mV des Eingangs, genommene Messungen und fehlgeschlagene Transaktionen modulo 65536, und (seit 4.4) die Rate in Gebrauch, 0 bis 6, oder 7, solange AUTO keine gefunden hat. Register 17 (seit 4.4), allein als 1 geschrieben, bei verlangtem Ausgang aus und eingeschaltetem Netzteil, startet das Modul neu (SYSTEM_RESET), sobald es geantwortet hat und sein Ausgang aus gelesen wird; es liest 0. Die Verdrahtung wird im Flash des IOmcu gehalten und beim Start mit abgeschaltetem Ausgang gefahren, damit ein Modul, das an blieb, nach einem Neustart abgeschaltet wird; der Befehl wird nicht gespeichert |
| 0x2B | SENSE | lesen, schreiben | zwei I2C-Strommonitore (I2C: Inter-Integrated Circuit) an einem Bus auf zwei Pins des IOmcu, ein TI INA228 im Leistungspfad des ESC und ein TI INA3221 an der Servoversorgung (seit 4.7). Register 0 bis 3, ein Frame: Freigabe (Bit 0 INA228, Bit 1 INA3221, Bit 2 der Ausgangsencoder AS5600 seit 4.9), der SDA-GPIO, der SCL-GPIO und der Takt, 400kHz und kein anderer Wert: bei 100kHz dauert ein Lesezugriff 480 bis 750µs, und der 1-ms-Zeitplan des IOmcu passt nicht. Der Zeitplan liest INA3221 CH1 mit 1000Hz, CH2 und CH3 abwechselnd mit je 500Hz, die Busspannung jedes Kanals alle 24ms (41,7Hz), Strom und Spannung des INA228 mit je 500Hz, seine Chip-Temperatur, Mask/Enable des INA3221 und das zurückgelesene Konfigurationsregister jedes Bauteils (seit 4.11) mit je 25Hz und DIAG_ALRT, ENERGY und CHARGE des INA228 mit 50Hz. Beide Module tragen Pull-ups an SDA und SCL, und die liegen parallel: der DAOKAI-INA3221 hat 10kΩ nach VS (3,3V); Wert und Schiene der Pull-ups des MATEK-INA228 sind unbekannt. Der Gesamtwert muss über etwa 1kΩ bleiben: ein I2C-Ausgang zieht 3mA bei 0,4V, und (3,3V − 0,4V) / 3mA sind 967Ω. Unter 10kΩ verkürzt er die Anstiegszeit: die Grenze von 300ns bei 400kHz erlaubt mit 10kΩ allein 35pF Buskapazität, mit weniger mehr. Die GPIO-Nummer von SDA ist modulo 4 gleich 0 oder 2, und SCL ist der GPIO danach, das Paar eines I2C-Blocks (standardmäßig GPIO 16 und GPIO 17). Register 4 bis 7, ein Frame: die Adresse des INA228, 0x40 bis 0x4F (Standard 0x45, die des MATEK I2C-INA-BM ab Werk; seine Lötbrücken geben 0x44 oder 0x41); sein Shunt in µΩ, 50 bis 20000 (Standard 200); der Strom, auf den sein Bereich eingestellt ist, in 0,1A, 10 bis 3000 (1,0 bis 300,0A, das Auslegungsmaximum des Prüfstands; Standard 2048). Das Maximum wählt nur ADCRANGE: 1, solange die Shuntspannung dabei höchstens 40,96mV beträgt, 0 bis 163,84mV, und darüber wird das Schreiben abgewiesen. CURRENT_LSB ist der Schritt des Shunt-ADC geteilt durch den Shunt (78,125 nV oder 312,5 nV durch R), damit begrenzen CURRENT und die Shuntspannung gemeinsam, und SHUNT_CAL ist in beiden Bereichen 4096. Ein Shunt, dessen Vollausschlag im gewählten Bereich 2000A übersteigt, wird ebenfalls abgewiesen; unter 81,92 µΩ gilt also nur ADCRANGE 1. Die Regel ist die des Treibers, `ina228_calibrate()`; Register 7 reserviert. Register 8 bis 11, ein Frame: die Adresse des INA3221, 0x40 bis 0x43 (Standard 0x40); sein Shunt in 0,1mΩ, 50 bis 10000 (5mΩ bis 1Ω, Standard 1000, die 0,1Ω, die bis 1,638A messen); die gelesenen Kanäle, Bits 0..2 für CH1 bis CH3, mindestens einer, solange er freigegeben ist (Standard CH1); Register 11 reserviert. Die reservierten Register lesen 0 und nehmen nur 0. Mit BAD_VALUE abgewiesen: ein Wert außerhalb seines Bereichs, SDA und SCL nicht das Paar eines Blocks, was auch immer ENABLE hält, bei freigegebenem Bauteil ein Pin, der reserviert, an einen Ausgang gebunden oder von SUPPLY gehalten ist, beide Bauteile auf einer Adresse, solange beide freigegeben sind, und jede Änderung bei einem Prüfstand in ARMED; ein Schreiben der geltenden Konfiguration wird angenommen. Ein Frame, der kein Paar ist, wird angenommen: ENABLE 0 mit SDA oder SCL 0, so schreibt ein Panel nicht gesetzte Pins. Die Pins gehören keinem Ausgang, solange eines der Bauteile freigegeben ist, und ein Schreiben auf OUTPUTS, das einen davon bindet, wird abgewiesen. Register 12 bis 25, nur lesen: Flags (Bit 0 INA228 online, Bit 1 seine letzte Identitätslesung war die eines INA228, Bit 2 an seiner Adresse antwortet etwas anderes, Bit 3 ein Strom lag im letzten 50-ms-Fenster oder seit dem Schalten des Laufs auf ARMED am Ende seines Bereichs, Strom und Leistung in BENCH oder ihre Spitzen sind dann Grenzen und keine Werte; Bits 4 bis 6 dieselben drei für den INA3221; Bit 8 der Bus ist auf seinen Pins offen, Bit 9 SDA oder SCL wird low gehalten und wird freigetaktet; Leitungen, die im Ruhezustand low sind und innerhalb von 50µs an den eigenen Pad-Pull-ups von 32 bis 86kΩ steigen, hält nichts (Pins ohne angeschlossenes Bauteil und ohne Pull-up), sie lassen Bit 9 frei, und die Bauteile antworten nicht), die Adressen, die beim letzten Scan geantwortet haben (Bit n für 0x40 + n), DEVICE_ID des INA228 und Die-ID des INA3221 wie gelesen, fehlgeschlagene Transaktionen modulo 65536, die Chiptemperatur des INA228 in 0,1°C (vorzeichenbehaftet) und DIAG_ALRT, seine Ladung in 0,01mAh (vorzeichenbehaftet, 32 Bit, Register 19 und 20, niederwertiges zuerst) und Energie in 0,01Wh (32 Bit, Register 21 und 22, niederwertiges zuerst) seit dem Schalten des Laufs auf ARMED, die Telemetriespannung (10mV) und der Telemetriestrom (10mA) des ESC selbst und ihre Gültigkeitsbits (Bit 0 Spannung, Bit 1 Strom). Register 26 bis 30, nur lesen, seit 4.9: Flags, RAW ANGLE, MAGNITUDE, Zähler und Ruhezeit in ms des Ausgangsencoders (siehe unten). Register 31, nur lesen, seit 4.11: `RESETS`, die zurückgesetzt gefundenen Strommonitore, der INA228 im Low-Byte und der INA3221 im High-Byte, je modulo 256 ([Der Fenster-Ring](#der-fenster-ring)); ein IOmcu mit 4.10 oder älter liest 0. Register 0 bis 11 werden im Flash des IOmcu gehalten |
| 0x2C | SERVO_SENSE | lesen, schreiben | die Kanäle des INA3221 und eine Bewegung, gemessen auf dem Takt des IOmcu (seit 4.7). Register 0 bis 11, nur lesen, vier je Kanal ab CH1: mittlerer Strom (mA, vorzeichenbehaftet), höchster Strom (mA, vorzeichenbehaftet), mittlere Busspannung (mV) und niedrigste Busspannung (mV) über das letzte 50-ms-Fenster, die Spannung auf der Lastseite des Shunts. Register 12, nur lesen: die Fensternummer modulo 65536; ein Lesen beendet kein Fenster. Register 13, nur lesen: Bits 0..2 das Fenster eines Kanals enthält Messungen, Bits 4..6 eine davon lag am Ende des Bereichs (163,8mV über dem Shunt), womit mittlerer und höchster Strom dieses Kanals Untergrenzen sind, Bit 7 dasselbe für die Messung der Bewegung. Register 14 bis 17, ein Frame: eine Messung, Bit 7 gesetzt, der INA3221-Kanal in Bits 0..1, nur CH1 (2 und 3 werden abgewiesen; das Feld bleibt für einen Kanal, der später schnell genug gelesen wird), und in Bits 8..10 der Ausgangskanal (0 bis 7), dessen nächstes geändertes Kommando die Zeitmessung startet, oder mit gesetztem Bit 11 (seit 4.13) der Programmierport in diesen Bits ([Die Programmierports](#die-programmierports)); der Haltestrom, bei dem die Bewegung endet, in mA, vorzeichenbehaftet, -32768 bis 32767 (seit 4.11; davor 0 bis 32767, ein größerer Wert abgewiesen); die Schwelle für Bewegung und das Band für Ankunft, je 1 bis 32767mA. Ein Scharfschalten ist der ganze Frame und startet eine laufende Messung neu. 0 in Register 14 entschärft und wird nie abgewiesen; am Anfang des Frames geschrieben, werden die anderen drei nicht gespeichert. Mit BAD_VALUE abgewiesen: jedes andere Schreiben, das nicht der ganze Frame ist, andere Bits in Register 14, ein Wert außerhalb seines Bereichs, ein anderer Kanal als CH1 oder einer, den SENSE nicht liest, und ein Ausgangskanal, der keine Surface an einem PWM-Slot ist, den der IOmcu gebunden hat (ein Pin, dessen Compare-Register ein anderer Pin hält, ist es nicht); mit NOT_ARMED bei einem Prüfstand in SAFE oder nur für Throttle in ARMED (seit 4.18). Ein Prüfstand, der aufhört, die Surfaces zu treiben, beendet eine nicht fertige Messung. Register 18 bis 24, nur lesen: der Zustand (0 idle, 1 scharf, 2 wartet auf Bewegung, 3 bewegt sich, 4 angekommen, 5 an einem Endanschlag eingeschwungen, 6 zu spät: Bewegung und keine Ankunft innerhalb von 3000ms plus der Verzögerung des Strommessers, 7 unerkannt: keine Bewegung in dieser Zeit, 8 verloren: der INA3221 hat aufgehört zu antworten, oder binnen 3000ms nach dem Scharfschalten kam keine PWM-Flanke), fertige Messungen modulo 65536, die Zeit vom PWM-Frame mit dem neuen Puls bis zur Bewegung und bis zur Ankunft in 0,1ms, aufgelöst auf das Abtastintervall von CH1, 1ms, höchster und mittlerer gefilterter Strom der Bewegung (mA, vorzeichenbehaftet) und die Zahl der Messungen darin. Nichts wird gespeichert: Nach einem Neustart des IOmcu steht überall 0 |
| 0x2D | TONE | lesen, schreiben | die Beeps eines ESC, gehört an einer Motorphase über einen Vorwiderstand und eine Z-Dioden-Klemme an einem GPIO des IOmcu, gestempelt von einer PIO-State-Machine mit 26,7ns (seit 4.8). Register 0 bis 3, ein Frame: Freigabe (Bit 0), der GPIO (Standard 22, Pad 29), der tiefste gehörte Ton in Hz (50 bis 2000, Standard 400) und der höchste (über dem tiefsten, bis 6900, Standard 6500). Register 4 bis 7, ein Frame: die Tonhöhenänderung in Prozent, die ohne Stille einen neuen Beep beginnt (0 trennt nur an Stille, höchstens 50, Standard 8), die Stille, die einen Beep beendet, in ms (1 bis 100 und mindestens die Periode des tiefsten Tons, Standard 3), die Tonperioden, die einen Beep ausmachen (1 bis 64, Standard 3), und Register 7 reserviert. Das reservierte Register liest 0 und nimmt nur 0. Mit BAD_VALUE abgewiesen: ein Wert außerhalb seines Bereichs, eine Kombination, die der Detektor ablehnt (eine Stille, die kürzer ist als die Periode des tiefsten Tons), und, solange der Tap freigegeben ist, ein GPIO hinter der Bank (63), von der Platine reserviert oder ein ADC-Pin (ADC: Analog-Digital-Wandler): GPIO 26 bis GPIO 29 des RP2350A und GPIO 40 bis GPIO 47 des RP2354B, die nicht fehlertolerant sind, sowie jeder GPIO an einem IOmcu, dessen Core 1 beim Booten nicht gestartet ist (bis zu einem Neustart läuft keine Erfassung). Mit BUSY abgewiesen (seit 4.17; davor BAD_VALUE), solange der Tap freigegeben ist: ein GPIO, der an einen Ausgang gebunden oder von SENSE, SUPPLY oder einem Programmierport gehalten ist, und eine Erfassung, die der IOmcu vorerst nicht starten kann (Core 1 in einem Durchlauf, oder keine PIO-State-Machine und kein DMA-Kanal übrig neben den Ausgängen und der Versorgung) ([Ein belegter Pin am Phasenabgriff](#ein-belegter-pin-am-phasenabgriff)). Der GPIO gehört keinem Ausgang, solange der Tap freigegeben ist, und ein Schreiben auf OUTPUTS, SENSE oder SUPPLY, das ihn nimmt, wird abgewiesen. Eine Änderung wird in ARMED wie in SAFE angenommen: der Tap ist ein Eingang und treibt nichts. Register 8 bis 12, nur lesen: Flags (Bit 0 die Erfassung läuft, Bit 1 der Tap ist freigegeben und sein Pin konnte nicht genommen werden, Bit 2 der Erfassungsring oder die FIFO der State Machine ist seit Beginn der Erfassung übergelaufen, Bit 3 eine Folge von Bursts läuft, Bit 4 das letzte Fenster enthielt einen Ton), die Fensternummer modulo 65536 (Fenster zu 8ms), der Ton des letzten Fensters in 0,1Hz (0 für keinen) und die Tonperioden darin, und die Nummer des neuesten Beeps. Der IOmcu hält die letzten 64 Beeps und nummeriert sie von 1 bis 65535, dann wieder ab 1; die neueste Nummer ist vor dem ersten Beep 0. Register 13, EVT_SEL, ist das einzige beschreibbare Register nach Register 7 und wird nicht gespeichert: die Nummer des Beeps, den die Register 14 bis 21 zeigen. Ein Lesen verbraucht keinen Beep, eine auf dem Link verlorene Antwort verliert also nichts. Register 14 bis 21, nur lesen: noch einmal EVT_SEL, solange dieser Beep unter den 64 ist, sonst 0, wobei Register 15 bis 21 0 lesen; der erste Anstieg des Beeps in ms seit Beginn der Erfassung (32 Bit, Register 15 und 16, niederwertiges zuerst); seine Länge bis zur letzten Flanke in 0,1ms; seine mittlere Tonhöhe in 0,1Hz; seine Bursts; der Träger, mit dem er zerhackt war, in 100-Hz-Schritten (0 nicht zerhackt); Flags (Bit 0 er begann bei einer Tonhöhenänderung ohne Stille davor, Bit 1 er endete bei einer). Register 22 und 23, nur lesen: verlorene Beeps und Lows unter 500ns, die der Detektor ignoriert hat, je modulo 65536. Der 8-µs-Hold-off der Erfassung entfernt jedes Low unter 8µs, bevor der Detektor es sieht; Register 23 liest deshalb am Tap 0. Die Erfassung beginnt, wenn der Tap freigegeben wird oder sein Pin wechselt, und leert dann die 64. Register 0 bis 6 werden im Flash des IOmcu gehalten, und der Tap startet beim Booten. Solange der Tap gesperrt ist, ist der Pin ein Eingang mit eingeschaltetem Pull-down (32 bis 86kΩ), und der bleibt an, solange der Tap läuft |
| 0x2E | BIND_CFG | lesen, schreiben | eine vorbereitete CHAN_CFG-Page, die nicht gilt (ab 4.10): die Register und die Wertregeln von CHAN_CFG. [Eine Bindung als Ganzes](#eine-bindung-als-ganzes) beschreibt die drei Pages |
| 0x2F | BIND_OUT | lesen, schreiben | eine vorbereitete OUTPUTS-Page, die nicht gilt (ab 4.10): die Register und die Wertregeln von OUTPUTS |
| 0x30 | BIND | lesen, schreiben | Register 0 `COMMIT` (ab 4.10): ein Schreiben der CRC-16 der 64 vorbereiteten Register setzt beide vorbereiteten Pages in Kraft oder keine; abgelehnt mit BAD_VALUE für einen anderen Wert und für eine Page, die ihre eigenen Regeln ablehnen. Liest die CRC dessen, was vorbereitet ist |
| 0x31 | SERVO_WIN | lesen | die letzten 4 vollständigen 50-ms-Fenster von INA3221 CH1, das neueste zuerst (seit 4.11). Register 0: die Nummer des neuesten Fensters modulo 65536. Register 1: Bit 0 ein Fenster hat sich geschlossen, Bit 7 das Bit der Messung der Bewegung für eine Messung am Ende des Bereichs. Register 2 und 3: Zustand und Zähler der Messung von SERVO_SENSE. Dann 4 Einträge zu 6 Registern, Eintrag k ab Register 4 + 6 k für Fensternummer Register 0 - k: mittlerer, höchster und niedrigster Strom (mA, vorzeichenbehaftet), mittlere und niedrigste Busspannung (mV) und Flags (Bit 15 mit dieser Nummer hat sich ein Fenster geschlossen, Bit 8 Strommessungen, Bit 9 Spannungsmessungen, Bit 10 eine Messung am oberen Ende des Bereichs, Bit 11 eine am unteren, Bits 0 bis 7 die Messungen an einem Ende, bei 255 gehalten). Eine Messung am Ende des Bereichs zählt mit dem Ende des Bereichs. Jedes Schreiben wird mit READ_ONLY abgewiesen. [Der Fenster-Ring](#der-fenster-ring) nennt die Regeln. Nichts wird gehalten |
| 0x32 | `KST` | lesen, schreiben | die Servo-Programmier-Page: das Programmierprotokoll für KST- und Chaservo-Servos auf einem Programmierport (seit 4.12; ein Port und die Versorgung seit 4.13). Ein Lesen gibt 32 Register: Zustand, Flags und Port, `SEQ`, Operation und Frames, Ergebnis und Abweisung, das fehlerhafte Register, 4 Registermasken, den Fingerprint, die gemessene Halbzelle und Antwortverzögerung und das Servo-Image in 16 Registern. Ein Schreiben nimmt 28: das Kommando in den Registern 0 bis 3, ein Frame, das bereitgestellte Image, die Unlock-Menge und die Start-Prüfsumme in 4 bis 25 und Versorgungsspannung und -strom eines ENTER in 26 und 27. [Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md) nennt die Regeln. Nichts wird gehalten |
| 0x33 | PROG | lesen, schreiben | die Programmierports (seit 4.13): 28 Register, vor 4.26 24. Die Zahl der Ports, Flags, der Pulsschalter und das Ziel des Servotesters in den Registern 0 bis 3, dann 5 Register je Port für 4 Ports: Pin, Kommando, Slew und die zwei Endpunkte, dann die FUNC jedes Ports in 24 bis 27 (seit 4.26). [Die Programmierports](#die-programmierports) nennt die Regeln. Pin, Slew, Endpunkte und FUNC werden im Flash des IOmcu gehalten |
| 0x34 | `HITEC` | lesen, schreiben | rohe Transaktionen der Hitec-Servoleitung auf einem Programmierport (seit 4.23). Ein Lesen gibt 32 Register: Zustand, Flags und Port, `SEQ`, Operation und Zahl der Transaktionen, Ergebnis und Abweisung, Anzahl und Flags des Mitschnitts, das Ergebnis des Dekoders, den dekodierten Wert, `VIEW`, die empfangenen Bytes in 8 Registern und in 16 Registern die Startzeiten der Bytes oder den Lauf. Ein Schreiben nimmt 24: das Kommando in den Registern 0 bis 3, ein Frame, die bereitgestellte Transaktion, Dekoder und Sollwerte eines POWER in 4 bis 21, `VIEW` in 22 und den Empfangsmodus einer Leitung der D-Serie in 23 (seit 4.24). [Der Programmierport für Servos von Hitec](LinkHitec-de.md) nennt die Regeln. Nichts wird gehalten |
| 0x35 | SENSE_MAP | lesen, schreiben | bis zu 4 INA3221 an 0x40 bis 0x43 auf dem Bus von SENSE (seit 4.25). Register 0 die bestückten Bauteile, Bit a für 0x40 + a; 1 `FAST`, der Kanal, der jede 1ms gelesen wird (0 bis 11, 0xFF keiner); 2 bis 13 der Shunt jedes Kanals in 0,1mΩ (0 aus, 50 bis 10000); 14 bis 25 was jeder Kanal misst (0 nichts, 0x01 bis 0x08 ein Ausgangskanal, 0x10 bis 0x13 ein Programmierport, 0x20 die Versorgung); nur lesbar: 26 bis 29 die Die-ID jedes Bauteils, 30 Flags, 31 reserviert. Die Register 0 bis 25 hält der IOmcu im Flash. Solange die Map in Kraft ist, spiegeln Bit 1 von ENABLE und die Register 8 bis 10 von SENSE sie. [Die INA3221-Map und ihre Fenster](LinkSensors-de.md) nennt die Regeln |
| 0x36 | SENSE_LIMIT | lesen, schreiben | eine Stromgrenze je Kanal von SENSE_MAP (seit 4.27); der IOmcu markiert eine überschrittene Grenze, das Panel behandelt sie als Fehler. Register 0 bis 11 die Grenze jedes Kanals in mA (0 keine, 1 bis 32767), im Flash des IOmcu gehalten; nur lesbar: 12 `OVER`, Bit c ein Fenster von Kanal c über seiner Grenze seit dem letzten Lesen des Registers, das es löscht; 13 `OVER_COUNT`, Kanal-Fenster über der Grenze, modulo 65536; 14 und 15 reserviert. 16 Register. [Die INA3221-Map und ihre Fenster](LinkSensors-de.md#sense_limit) nennt die Regeln |
| 0x37 | SENSE_WIN_A | lesen | das letzte 50-ms-Fenster der Kanäle 0 bis 5 (seit 4.25): die Fensternummer, Flags, und 5 Register je Kanal: mittlerer und höchster Strom (mA, mit Vorzeichen), mittlere und niedrigste Busspannung (mV), Flags. Jedes Schreiben wird mit READ_ONLY abgewiesen. Nichts wird gehalten |
| 0x38 | SENSE_WIN_B | lesen | das der Kanäle 6 bis 11, wie SENSE_WIN_A (seit 4.25) |
| 0x39 | PROG_SUPPLY | lesen, schreiben | die eigene Versorgung der Programmierports auf einem Board, das sie mit einem Pin des IOmcu schaltet: GPIO 21 des rbmod-Carriers, der Enable seines 5V-Abwärtswandlers M7 (seit 4.32). Register 0: Ausgang, 1 ein, 0 aus. Ein ON wird ohne lebenden Heartbeat mit NOT_ARMED abgelehnt, und mit BAD_VALUE auf einem Board ohne den Schalter und solange ein Programmierport die Versorgung hält. Ein OFF wird immer genommen; während ein Port in den Speicher des Servos schreibt, wartet es auf das Ende des Schreibens, höchstens 10.000ms nach dem OFF, und das eigene ON des Ports wird danach abgelehnt, bis der Port loslässt. Der Ausgang geht aus, wenn der Heartbeat endet oder der Link verstummt, und ein OFF wartet auf das Ende eines Frames, den ein Port auf der Leitung hat, höchstens 12ms. Register 1 bis 3, nur lesen: Flags (Bit 0 das Board hat den Schalter, Bit 1 der Pin schaltet die Versorgung ein, Bit 2 ein Programmierport hält sie, Bit 3 das OFF des Panels wartet auf das Schreiben eines Ports), der schaltende GPIO (255 ohne) und die Spannung in mV (5000 auf dem Carrier, 0 ohne). Nicht gespeichert: bei jedem Start aus. Auf dem Carrier schalten die Ports für KST und Hitec diese Versorgung statt des PD mini |
| 0xF0 | UPDATE_CTL | lesen, schreiben | das Firmware-Update des IOmcu selbst (seit 4.22; über Majors eingefroren): 32 Register. Version, Zustand, Fehler, Slot, Sektoren je Slot, nächster Block, Größe des Images, dann das Kommando in Register 8 mit bis zu 3 Argumenten und ein SHA-256-Digest in Register 16 bis 31. Ein Schreiben, das kein Kommando ist, wird mit READ_ONLY abgelehnt. [Firmware-Update über den Link](#firmware-update-über-den-link) hat die Regeln. Nichts wird behalten |
| 0xF1 | UPDATE_DATA | lesen, schreiben | die 64 Byte des aktuellen Blocks, in jedem Register das niederwertige Byte zuerst (seit 4.22; über Majors eingefroren): 32 Register. FINISH nimmt den erwarteten SHA-256 aus Register 0 bis 15. Nichts wird behalten |

Fault-Bitmap: Bit 0 Link still, Bit 1 Überstrom, Bit 2 Übertemperatur, Bit 3
Stall, Bit 4 Heartbeat ausgeblieben, Bit 5 Protokollversion abweichend,
Bit 6 der Flash-Speicher des IOmcu ist für diesen Start aus (seit
4.7): sein zweiter Core hat sich nicht für die Flash-Sperre angemeldet,
also wird nichts gespeichert, und über den Link geschriebene
Konfigurationen laufen bis zu einem Neustart aus dem RAM.
Faults bleiben gesetzt, bis sie gelesen und gelöscht werden; Bit 6 bleibt
bis zu einem Neustart gesetzt. Bit 0 setzt
voraus, dass über den Link mindestens eine Anfrage gelaufen ist: der
IOmcu ist wach, bevor das Panel pollt, und das Warten auf die erste
Anfrage ist keine Stille.

Capabilities-Bitmap: Bit 0 ESC-Ansteuerung, Bit 1 ESC-Telemetrie, Bit 2
Servo-PWM, Bit 3 Servo-Strommessung, Bit 4 Akkumessung, Bit 5 Empfängerbus,
Bit 6 Vibrationssensor und Indeximpuls, Bit 7 Zellenmonitor, Bit 8
Programmierung. Das Panel leitet daraus die Marken im Menü ab. Bits 3 und 4
sagen, was die SENSE-Konfiguration freigibt: Bit 3 den INA3221, Bit 4 den
INA228, also bestückt laut Konfiguration, nicht gerade antwortend (seit
4.7). Sie ändern sich nur, wenn ein Schreiben auf SENSE angenommen wird,
und ein Panel liest sie nach einem Schreiben auf SENSE neu. Ob ein Bauteil
antwortet, sagen die FLAGS von SENSE und BENCH-Bit 5, die das Panel pollt. Die
Page TONE fügt kein Bit hinzu.

BENCH-Flags: Bit 0 Spannung gültig, Bit 1 Strom gültig, Bit 2 Drehzahl
gültig, Bit 3 Temperatur des ESC gültig, Bit 4 Temperatur des Motors gültig,
Bit 5 Spannung, Strom und Leistung sind die des INA228 und nicht die
Telemetrie des ESC (seit 4.7), Bit 6 Ladung und Energie sind die Zähler des
INA228, beim Schalten dieses Laufs auf ARMED gelöscht, und das Bauteil hat
durchgehend geantwortet (seit 4.7), Bit 7 simuliert. Ohne Bit 6 zählt ein
Panel Ladung und Energie selbst aus dem Strom; mit Bit 6 zeigt das Panel
dieses Builds die des INA228, in den Schritten von 0,01mAh und 0,01Wh der
SENSE-Page aus einem Lesen jünger als 100ms, ab 100ms nach Laufbeginn. Die beiden Temperaturen haben getrennte Bits, weil sie aus
verschiedenen Quellen kommen und eine davon meist gar nicht kommt: ein ESC
meldet seine eigene Temperatur über die erweiterte DShot-Telemetrie und weiß
nichts über den Motor, den er treibt. Bis Protokoll 3.0 galt Bit 3 für beide —
deshalb ist diese Änderung ein Major. Ein IOmcu ohne
Mess-Frontend setzt Bit 7, und das Panel zeichnet SIMULATION über den
Bildschirm.

MOTOR_POLES ist die Magnetzahl des geprüften Motors, gerade und zwischen 2 und
42, oder null: dann hat es niemand gesagt. Ein bidirektionaler DShot-ESC meldet
elektrische Perioden und weiß nicht, woran er angeschraubt ist; das ist also die
eine Zahl, die die Leitung tragen muss, damit der IOmcu eine mechanische
Drehzahl melden kann. Bei null meldet er keine Drehzahl statt einer aus einer
Schätzung abgeleiteten. Das Panel sendet sie aus der Einstellung `Motorpole`,
sobald ein IOmcu zu antworten beginnt, erneut bei jeder Änderung der
Einstellung und im Frame, der auf ARMED schaltet, ob eine Änderung offen ist oder
nicht. Ein Schreibvorgang,
den niemand beantwortet, bleibt offen und geht beim nächsten 50-ms-Poll
erneut hinaus; einer, den der IOmcu ablehnt, wird nicht wiederholt,
denn dieselbe einmal abgelehnte Anfrage wird jedes Mal abgelehnt, und er
wartet stattdessen auf die nächste Änderung oder die nächste
Link-up-Flanke. Der Schutz bei null greift für eine nie gesendete Zahl, nicht
für eine veraltete: jede Zahl, die die Einstellung zulässt, liegt im Bereich,
den die Page annimmt.

Die Endpunkte der Gaskanäle, die Einstellungen `Leerlaufpuls` und
`Vollpuls`, gehen über CHAN_CFG und nicht im Frame, der auf ARMED schaltet.
Das Panel sendet sie, wenn ein IOmcu zu antworten beginnt, nach jeder
Änderung und nach jeder Zuordnung, sobald die Änderung 300ms geruht hat, bei
einem Poll, dessen Schreiben von ARM = 0 quittiert wurde: es liest die Page,
schreibt sie nur, wenn die Gaskanäle abweichen, und prüft die gespeicherten
Register, die die Quittung trägt. Ein Lesen oder Schreiben, das niemand
beantwortet, bleibt offen. Ein abgelehntes Lesen, ein abgelehntes Schreiben,
ein Leerlaufpuls, der nicht mindestens 100us unter dem Vollpuls liegt, oder eine Quittung, deren
Gaskanäle ein anderes Paar halten, beendet die Schuld mit einem Hinweis und
sperrt das Schalten auf ARMED, bis eine Änderung, eine Zuordnung oder ein
Link-up das Paar in Kraft bringt. Solange sie bei bestehendem Link offen oder
gesperrt sind, schreibt das Panel kein Schalten auf ARMED und nennt den Grund,
von `Pulsbereich noch nicht an den IOmcu gesendet. ARM erneut halten.` bis
`Kein Pulsbereich vom IOmcu. IOmcu-Firmware aktualisieren, dann ARM erneut
halten.`; [Ein Pulsbereich unterwegs](Safety-de.md#ein-pulsbereich-unterwegs)
hat die Regeln und die Texte. Die Regel ist die des Panels: Der IOmcu übernimmt ein
Schreiben von CHAN_CFG, bevor er es quittiert, und hat keine eigene Regel für
eine Änderung, die er nicht empfangen hat.

Der IOmcu verweigert einen Pin, den er nicht treiben darf — die
Safety-Leitung, die Pins des CAN-Controllers, GPIO 23, GPIO 24, GPIO 25 und GPIO 29 (vom
Modul belegt, nicht herausgeführt) und jede Nummer über GPIO 29 — auf der
OUTPUTS- und der SUPPLY-Page gleichermaßen. Ein OUTPUTS-Write mit einem Slot, den die Hardware nicht
binden kann — ein solcher Pin, oder eine PIO-Zustandsmaschine, Instruktionsspeicher oder ein
DMA-Kanal, den der Phase-Tap oder die UART der Versorgung belegt —, wird mit BAD_VALUE verweigert;
die geltenden Slots bleiben, und nichts wird gespeichert. [DShot und die
Output-Treiber](DShot-de.md) hat den Rest.

Einträge in CHAN_CFG und OUTPUTS werden ganz geschrieben, vier Register auf
einmal. Der Pulsbereich eines Kanals ist standardmäßig 1000..2000µs;
Endpunkte außerhalb von 400..2500µs werden mit BAD_VALUE abgewiesen, ebenso
ein Endpunkt, der länger ist als der Frame des PWM-Ausgangs, der ihn ausgibt
([Ein Puls nicht länger als sein Frame](#ein-puls-nicht-länger-als-sein-frame)). Ein
Kommando außerhalb seines Bereichs wird begrenzt. Zwei Slots auf einem Pin
oder zwei Slots, die denselben Kanal ausgeben, werden abgewiesen. Über das
Schalten auf ARMED entscheidet der IOmcu: ein Schreiben von ARM wird mit
NOT_ARMED abgewiesen, solange der Link im Failsafe ist, dem Heartbeat nicht
vertraut wird oder der Arm-Latch gesetzt ist.

Solange der Prüfstand ARMED ist (die Bank treibt, oder das Register ARM
ist gesetzt), wird ein Schreiben auf OUTPUTS, das irgendein Register ändert,
mit BAD_VALUE abgewiesen, ebenso ein Schreiben auf CHAN_CFG, das die Rolle
eines Kanals ändert oder Rolle, Slew oder Endpunkte eines Kanals mit der
Rolle Throttle. Slew und Endpunkte einer Surface werden in ARMED angenommen,
ebenso ein Schreiben der geltenden Page; ein Schreiben der
geltenden OUTPUTS-Page bindet nichts neu. Die Regel steht in
`outputs_chan_cfg_armed_check()` und `outputs_slots_armed_check()`, unter
`test_link_pages`.

Der Arm-Latch und die Ablehnung in ARMED ändern kein Register und
keinen Frame und tragen deshalb keine eigene Protokollversion. Was eine
Gegenstelle sieht, die ohne sie gebaut wurde:

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| älter | dieser Stand | der Link kommt hoch und der Prüfstand schaltet auf ARMED: dieses Panel schreibt vor jedem Schalten auf ARMED CLEAR. Sein erstes Schalten auf ARMED nach einem STOP kann einmal mit NOT_ARMED abgewiesen werden, weil es feste 100ms wartet; die Ablehnung lässt den Latch gesetzt, und das zweite Halten schaltet auf ARMED. Ein ARM = 1, das es nach einem Neustart des IOmcu schreibt, wird abgewiesen, und bei ihm rastet ein Stopp ein |
| dieser Stand | älter | der Link kommt hoch und der Prüfstand schaltet auf ARMED. Das Panel stoppt an der Flanke, an der der Link ausfällt; ein Neustart, den es bemerkt, schaltet also nichts auf ARMED. Ein Neustart, den es nicht bemerkt (die Anfrage wird wiederholt, bis der IOmcu wieder antwortet), wird vom nächsten Poll wieder auf ARMED geschaltet, wie zwischen zwei älteren Ständen. Ein Heartbeat, dem kürzer als ein Poll-Abstand nicht vertraut wurde, setzt dort keinen Latch |
| ein anderer Host | dieser Stand | ein ARM ohne CLEAR seit dem Start des IOmcu wird mit NOT_ARMED abgewiesen; ein Schreiben auf CHAN_CFG oder OUTPUTS wie oben wird mit BAD_VALUE abgewiesen, solange ARM gesetzt ist |

Ein Schalten des Panels auf ARMED sind zwei Transaktionen. CLEAR geht zuerst und allein:
der IOmcu prüft ARM gegen sein Failsafe, bevor er ein CLEAR aus
demselben Frame anwendet, also wird ein Frame mit beidem genau dann mit
NOT_ARMED abgewiesen, wenn das Clear nötig war. Danach gehen ARM, THROTTLE
und MOTOR_POLES als ein Frame mit drei Registern ab Offset 0, sodass der
IOmcu den Lauf mit der Polzahl beginnt, die das Panel gesendet hat,
oder ihn nicht beginnt. Jeder Poll alle 50ms danach schreibt ARM und
THROTTLE; eine während eines Laufs geänderte Polzahl geht beim nächsten Poll
in einem eigenen Write. Die Regeln der Page (der Throttle-Bereich, die
Polzahl, die CLEAR-Magic, ARM im Failsafe abgewiesen, und dass eine
Ablehnung nichts speichert) stehen in `shared/link/link_control.c`, unter
`test_link_pages`. Ob der Prüfstand auf ARMED schalten darf und was den Latch setzt
und löst, steht in `shared/safety/safety_gate.c`, unter `test_safety_gate`.

Vor dem CLEAR wartet das Panel, bis der IOmcu dem Heartbeat vertraut:
100ms nach dem vollendeten Halten liest es einmal je Durchlauf von 5ms das
Register 1 von STATUS, bis Bit 4 gelöscht ist, höchstens 200ms länger. Der
IOmcu füllt die Register 0 und 1 von STATUS beim Lesen.

### Ein Arm für eine Rolle

Ab 4.18 nennt das Register ARM die Rollen, die ein Arm umfasst. Die Rolle
eines Kanals ist die aus CHAN_CFG: throttle oder surface. Ein
SERVO-PWM-Programmierport ist eine Surface, ein DShot-Programmierport ein
Throttle (seit 4.28).

| ARM | Name | Treibt |
| ---: | --- | --- |
| 0x0000 | kein Name | nichts: ein Schalten auf SAFE |
| 0x0001 | `LINK_ARM_ALL` | jeden Ausgang, wie ein IOmcu vor 4.18 bei jedem Wert außer 0 |
| 0x0002 | `LINK_ARM_THROTTLE` | die Slots, deren Kanäle Throttles sind: Motoren und ESCs; und die DShot-Programmierports (seit 4.28) |
| 0x0004 | `LINK_ARM_SURFACE` | die Slots, deren Kanäle Surfaces sind, und die SERVO-PWM-Programmierports mit gesetztem `PWM`-Bit |
| 0x0006 | beide Rollen-Bits | jeden Ausgang, wie 0x0001 |

Jeder andere Wert wird mit BAD_VALUE abgewiesen und ändert nichts. Er wird
vor dem Failsafe geprüft und setzt daher keinen Arm-Latch. Ein Kanal einer
Rolle, die der Arm nicht umfasst, bleibt in seiner Ruhe, und sein Slot gibt
keinen Puls aus: ein PWM-Pin bleibt low, ein DShot-Slot (digital shot)
sendet keinen Frame. Ein PPM-Slot (Pulspositionsmodulation) trägt alle seine
Kanäle in einem Frame; er läuft, solange der Arm einen davon umfasst, die
anderen in ihrer Ruhe.

Die Rollen ändern sich nur über ein Schalten auf SAFE. Solange ARM einen Wert
ungleich 0 hält, wird ein Schreiben eines anderen Werts ungleich 0 mit
BAD_VALUE abgewiesen und ändert nichts; dasselbe noch einmal wird angenommen.
Das Panel schreibt zuerst ARM = 0 und den neuen Wert, wenn sich die Leitung
gesetzt hat, frühestens nach 100ms. Ein Ausgang, der den Arm verlässt, geht
sofort in seine Ruhe, ohne Slew.

STOP, ein Heartbeat, dem nicht vertraut wird, Link-Stille und der Failsafe
setzen ARM auf 0, gleich welche Rollen es nannte, und die ganze Bank geht in
Ruhe. Arm-Latch, CLEAR und der Frame, der auf ARMED schaltet, sind wie unter
[Kompatibilität](#kompatibilität).

Was die Bildschirme schreiben:

| Bildschirm | ARM |
| --- | --- |
| MOTOR & ESC, dessen PROGRAMMIEREN eingeschlossen (der Stick-Lauf) | 0x0002 |
| SERVO | 0x0004 |

SWEEP, HOLD und RESUME der Page SERVO und eine Messung von SERVO_SENSE
brauchen die Surfaces in ARMED: unter 0x0002 werden sie mit NOT_ARMED
abgewiesen. Eine KST-Sitzung nimmt keinen Port, solange die Bank für
irgendeine Rolle ARMED ist.

Die Regeln sind `link_arm_value_ok()` und `link_control_write()` in
`shared/link/link_control.c`, `outputs_arm_roles()` in
`shared/outputs/outputs.c` und `arming_request_arm_roles()` in
`shared/safety/arming.c`, unter `test_link_pages`, `test_outputs`,
`test_safety_gate` und `test_arming`.

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.18 | 4.18 | wie oben |
| 4.18 | 4.17 oder älter | das Panel schreibt 0x0001, und der IOmcu schaltet seine ganze Bank wie vor 4.18 auf ARMED: ein Arm auf dem SERVO-Bildschirm treibt auch die Throttle-Kanäle in ihrer Ruhe, einer auf MOTOR & ESC auch die Surfaces und einen pulsenden Programmierport |
| 4.17 oder älter | 4.18 | dieses Panel schreibt 1, und jeder Ausgang ist ARMED |

### Ein Puls nicht länger als sein Frame

Ab 4.16 passt jeder Endpunkt, den ein PWM-Ausgang (Pulsweitenmodulation)
ausgibt, in seinen Frame. Der längste Puls, den ein Frame trägt, ist die
Periode minus 1µs, abgerundet: der letzte Zählerstand bei 1MHz, der den Pin
1µs auf Low lässt. Die Regel ist `out_pwm_pulse_max()` in
`shared/outputs/out_pwm_map.c`.

| Rate | Periode | Längster Endpunkt |
| ---: | ---: | ---: |
| 560Hz | 1785µs | 1784µs |
| 500Hz | 2000µs | 1999µs |
| 400Hz | 2500µs | 2499µs |
| 399Hz und darunter | 2506µs und mehr | 2500µs, die Obergrenze von CHAN_CFG |

Beurteilt wird die Rate, mit der der Ausgang läuft: Register 0 von SERVO für
einen Slot, dessen erster Kanal die Rolle surface hat, solange dieses
Register nicht 0 ist und die Standard-Servoports das Ziel sind, sonst die
eigene OUTPUTS-Rate des Slots; für den Programmierport, den `TARGET` nennt,
Register 0 von SERVO, oder 50Hz, solange es 0 liest, und 50Hz für jeden
anderen Port ([Die Frame Rate des Ziels](#die-frame-rate-des-ziels)). Ein
Kanal wird nach dem längeren seiner beiden Endpunkte beurteilt, ein
vertauschtes Paar zählt also gleich. PPM- (Pulspositionsmodulation) und
DShot-Slots werden nicht beurteilt: ihre Treiber bauen ihre Frames selbst.

Ein Schreiben, das einen längeren Endpunkt hinterließe, wird mit BAD_VALUE
abgewiesen und ändert nichts:

| Schreiben | Abgewiesen, wenn |
| --- | --- |
| CHAN_CFG (0x23) | ein Endpunkt eines Kanals, den ein PWM-Slot ausgibt, länger ist als der Frame dieses Slots |
| OUTPUTS (0x22) | die Rate eines PWM-Slots für die Endpunkte seines Kanals zu schnell ist |
| BIND (0x30) `COMMIT` | die vorbereitete CHAN_CFG-Page, beurteilt mit der vorbereiteten OUTPUTS-Page, die Regel bricht; jede Page für sich darf es |
| SERVO (0x29) Register 0 | das Ziel (eine Surface oder der Ziel-Programmierport mit gesetztem `PWM`-Bit) mit der Rate einen Endpunkt hätte, der länger als sein Frame ist. 0 wird mit den Standard-Servoports als Ziel ebenso beurteilt: es schickt jede Surface zurück auf die eigene Rate ihres Slots |
| PROG (0x33) `TARGET` | die Änderung die Standard-Servoports verlässt, solange Register 0 von SERVO nicht 0 ist, und ein PWM-Slot zurück auf seiner eigenen OUTPUTS-Rate einen Endpunkt hätte, der länger als dieser Frame ist |
| PROG (0x33) `PWM`, `MIN_US`, `MAX_US` | ein Port, dessen `PWM`-Bit nach dem Schreiben gesetzt ist, einen Endpunkt hat, der länger ist als der Frame der Rate dieses Ports |

Die Prüfungen sind `outputs_pulse_check()`, `outputs_chan_cfg_pulse_check()`
und `outputs_slots_pulse_check()` in `shared/outputs/outputs_pages.c` und
`prog_port_write()` in `shared/outputs/prog_port.c`, unter `test_outputs`,
`test_servo_page` und `test_prog_port`.

Ein Standardservo mit 1000 bis 2000µs nimmt 499Hz und wird bei 500Hz und
560Hz abgewiesen. Die Rate von SERVO gilt nur für ihr Ziel; mit einem
Programmierport als Ziel wird 560Hz also nur abgewiesen, solange der Puls
dieses Ports eingeschaltet ist und einen Endpunkt über 1784µs hat. Eine
gebundene PWM-Surface behält ihre eigene Rate und wird nicht beurteilt.

Beim Start stellt der IOmcu CHAN_CFG und OUTPUTS aus seinem Flash
wieder her. Ein gespeicherter PWM-Slot, dessen Kanal einen Endpunkt hat, der
länger ist als der Frame der eigenen Rate des Slots, startet ungebunden:
OUTPUTS liest für ihn Treiber 0, wie für einen gespeicherten Slot auf den
Kanälen der Ports. Register 0 von SERVO und die `PWM`-Bits von PROG werden
nicht gehalten und starten bei 0, beim Start wird also nichts weiter
beurteilt.

Der PWM-Treiber (`firmware/iomcu/src/out_pwm.c`) kürzt einen Pegel über den
letzten Zählerstand des Frames hinaus weiterhin auf diesen Zählerstand. Kein
Schreiben einer Page erreicht das.

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.16 | 4.16 | wie oben. Der SERVO-Bildschirm des Panels hält zwischen zwei Pulsen eine Pause von mindestens 1000µs (500µs für ein Heli-Servo) und erhöht die Rate erst, nachdem der schmalere Bereich angekommen ist; mit OUTPUTS als Ziel verlangt er also keine abgewiesene Kombination. Mit einem Programmierport als Ziel schreibt er nur den Bereich des Ports, und seine Rate wird abgewiesen, solange eine gebundene PWM-Surface einen Endpunkt hat, der länger als der Frame ist; der Bildschirm zeigt dann ABGELEHNT. Vor einem Schreiben, das den Puls eines Ports einschaltet, schreibt das Panel dem Port den Bereich, den der Bildschirm nennt |
| 4.16 | 4.15 oder älter | der IOmcu nimmt einen längeren Endpunkt. Sein PWM-Treiber kürzt den Puls auf den letzten Zählerstand des Frames, 1784µs bei 560Hz, während Page, Panel und Log den geschriebenen Endpunkt zeigen. Den Bereich, den das Panel vor einem Puls schreibt, nimmt er wie jedes Schreiben eines Bereichs |
| 4.15 oder älter | 4.16 | der SERVO-Bildschirm verhält sich wie oben. Ein `PWM`-Schreiben auf einen Port wird abgewiesen, solange der Port einen Bereich aus einem früheren Profil hält, der nicht zur Port-Rate passt; dieses Panel liest PROG erneut und schreibt es bei seinem nächsten Durchlauf wieder, und der Port gibt keinen Puls aus, bis ein Kommando ihm einen passenden Bereich geschrieben hat |

### Die Frame Rate des Ziels

Ab 4.19 treibt Register 0 von SERVO nur das PWM-Ziel, den Ausgang, den
`TARGET` von PROG für den Servotester nennt:

| `TARGET` | Läuft mit der Rate von SERVO | Läuft mit eigener Rate |
| --- | --- | --- |
| 0, die Standard-Servoports | jeder PWM-Slot, dessen erster Kanal eine Surface ist | jeder andere Slot mit seiner OUTPUTS-Rate; jeder Programmierport mit 50Hz |
| n + 1, Programmierport n | der Pin von Port n; 0 heißt 50Hz | jeder Slot mit seiner OUTPUTS-Rate; jeder andere Port mit 50Hz |

Eine Änderung der Rate von SERVO bewegt also kein Gas, keinen anderen Slot
und keinen anderen Port. Eine Änderung von `TARGET` setzt Register 0 auf 0:
das neue Ziel startet mit seiner eigenen Rate, und das alte geht auf seine
eigene zurück. Das Panel schreibt die Rate mit der nächsten Position wieder.

Der RP2350 betreibt beide Kanäle eines PWM-Slices mit einem Taktteiler und
einem Wrap-Register; die zwei Pins eines Slices können also nicht mit zwei
Raten laufen. Die Regeln, die einen Slice bei einer Rate halten:

- Ein Slot-Pin teilt keinen Slice mit einem Port-Pin ([Die Programmierports](#die-programmierports)).
- Mit den Standard-Servoports als Ziel wird eine Rate, die eine Surface und
  einen Slot mit anderer Rate auf einen Slice legte, mit BAD_VALUE
  abgewiesen, wie seit 4.1.
- Ein Port, dessen Pin seinen Slice mit dem Pin eines anderen gehaltenen
  Ports teilt, nimmt als Rate von SERVO nur 0 oder 50Hz; jede andere wird
  mit BAD_VALUE abgewiesen.
- Solange der Ziel-Port mit einer anderen Rate als 50Hz läuft, wird ein
  Schreiben von `PIN`, das einen anderen Port auf den Slice des Ziels legt,
  mit BAD_VALUE abgewiesen.

Die Regeln sind `servo_page_write()` in `shared/outputs/servo_page.c` und
`prog_port_write()` in `shared/outputs/prog_port.c`; der IOmcu bindet
die Raten in `ports_apply()` und `target_sync()` in
`firmware/iomcu/src/iomcu_pages.c`.

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.19 | 4.19 | wie oben. Das Panel verfolgt `TARGET` auf PROG: eine Änderung, die es liest oder bestätigt schreibt, zählt als Rate 0, und ein `TARGET`-Schreiben ohne Antwort zählt als unbekannte Rate, bis ein Schreiben der Rate ankommt. Der SERVO-Bildschirm zeigt die Rate als nicht gesendet, bis die nächste Position sie trägt |
| 4.19 | 4.16 bis 4.18 | der IOmcu fährt jede PWM-Surface und jeden Programmierport mit der Rate von SERVO, gleich was `TARGET` nennt, und behält sie über eine Änderung von `TARGET`. Das Panel verfolgt unter Minor 19 keine Änderung von `TARGET`, und sein SERVO-Bildschirm sagt, dass jede PWM-Surface mit der Rate läuft. Mit einem Programmierport als Ziel wird die Rate abgewiesen, solange eine gebundene PWM-Surface einen Endpunkt hat, der länger als der Frame ist |
| 4.18 oder älter | 4.19 | der IOmcu setzt die Rate bei einer Änderung von `TARGET` auf 0, während das Panel die Rate zählt, die es zuletzt geschrieben hat. Das Panel zählt dann eine Rate, die mindestens so schnell ist wie die geltende, die Seite, auf der kein Puls zu breit für seinen Frame gesendet wird, und es schreibt die Rate mit seiner nächsten Position wieder |

`test_servo_page`, `test_prog_port`, `test_prog_link` und `test_link_pages`
halten die Rate unabhängig von den Slots, ein Port-Ziel bei 39, 40, 499,
500, 560 und 561Hz gegen den PWM-Bereich und seine Endpunkte, einen
geteilten Slice, der 0 und 50Hz nimmt und 40, 300 und 560Hz abweist, eine
Änderung des Ziels bei einer Bank in ARMED und das Verfolgen einer Änderung von
`TARGET` durch das Panel.

### Die Zeichnung der Platine

Ab 4.20 liefert der IOmcu auf ARTWORK (0x26) und ART_DATA (0x27) eine
Zeichnung seiner eigenen Platine: den Umriss, die Bauteile als flache Formen
und jedes Pad mit dem, was es ist. PICK A PIN zeichnet die Platine daraus
maßstäblich. Eine neue Platine bringt ihre Zeichnung im eigenen
IOmcu-Image mit; das Panel braucht dafür keine Änderung. Die Zeichnung
des RP2350-CAN hat 1 560 Bytes in 26 Blöcken: 40 Pads und 102 Formen auf
einem Umriss von 51,00 × 21,00mm.

Die Nutzdaten sind Little-Endian, in 0,01mm, mit dem Ursprung in der linken
oberen Ecke des Umrisses von der Bauteilseite gesehen, x entlang der
Pad-Reihen.

| Teil | Bytes | Felder |
| --- | --- | --- |
| Kopf | 16 | Version (1), Pads (1 bis 64), Formen (0 bis 400, 16 Bit), Breite und Höhe des Umrisses, Farbe der Platine und der Pad-Ringe in RGB565, Eckenradius des Umrisses, Ringradius, Lochradius (kleiner als der Ringradius), 0 |
| ein Pad | 8 | Pad-Nummer (1 bis 63, aufsteigend), Art (1 Masse, 2 eine Versorgung, 3 keines von beiden, 4 ein GPIO), Wert (die GPIO-Nummer eines GPIO-Pads, die Spannung einer Versorgung in 0,1V, sonst 0), Flags (Bit 0 ein quadratischer Ring), x und y der Mitte |
| eine Form | 12 | Art (1 Rechteck, 2 abgerundetes Rechteck, 3 Kreisscheibe, 4 Linie), Eckenradius eines abgerundeten Rechtecks oder Breite einer Linie (sonst 0), Farbe in RGB565, dann vier Koordinaten: links, oben, Breite und Höhe eines Rechtecks; Mitte und Radius einer Kreisscheibe; beide Enden einer Linie |

Die längsten Nutzdaten haben 5 328 Bytes, 86 Blöcke. Jeder Ring und jede
Form liegt im Umriss, und die Länge ist genau Kopf, Pads und Formen; alles
andere weist das Panel als Ganzes ab und zeichnet die Platine stattdessen
aus SHAPE (0x25). Das Layout ist `LINK_DRAW_*` in
`shared/link/include/link_pages.h`; `link_drawing_parse()` in
`shared/link/link_drawing.c` ist die Prüfung des Panels.

Das Panel weist auch eine Zeichnung ab, die den anderen Pages der Platine
widerspricht: ein anderer Umriss als der von SHAPE, ein Pad nicht auf dem
Raster von SHAPE oder ein Pad, dessen Art oder Wert von CATALOGUE (0x24) und
PADS (0x28) abweicht. Die Pin-Listen bleiben auf CATALOGUE und PADS; die
Zeichnung platziert sie nur.

Das Panel behält eine geholte Zeichnung in seiner Flash-Partition
`boardart` unter der Board-ID, zusammen mit jedem ARTWORK-Register ab
Register 1. Beim Link-Aufbau liest es ARTWORK und zeigt die behaltene
Zeichnung nur, wenn alle übereinstimmen: Format, Umriss, Länge, CRC und Hash.
Jede andere Zeichnung unter derselben Board-ID wird neu geholt; ein
IOmcu, der mit einer geänderten Zeichnung geflasht wurde, wird also nie
mit der alten gezeigt. Ein Abruf prüft die Nutzdaten gegen CRC und Hash,
bevor sie gezeigt oder behalten werden.

`tools/gen_board_drawing.py` schreibt die Zeichnung des IOmcu,
`firmware/iomcu/src/board_drawing_rp2350_can.c`, aus
`configurator/drawings/panel.json` des Konfigurators und weist eine
Zeichnung ab, die das Panel abweisen würde. Die CI (Continuous Integration)
führt es mit `--check` aus.

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.20 | 4.20 | wie oben |
| 4.20 | 4.19 oder älter | das Panel liest ARTWORK nicht und zeichnet die Platine aus SHAPE. Das Log sagt `hardware 1 serves no board drawing (protocol 4.19); PICK A PIN draws its shape` |
| 4.19 oder älter | 4.20 | das Panel liest sieben ARTWORK-Register, findet Format 2 und holt nichts: `no photograph from hardware 1: it describes a photograph that cannot be one`. Ein früher behaltenes Foto erscheint weiter |

`test_link_drawing`, `test_board_drawing`, `test_link_artxfer`,
`test_art_fetch`, `test_art_store` und `test_picker_screen` halten das Layout
an seinen Grenzen (64 Pads, 400 Formen, ein Ring, der die Kante berührt, je
einer darüber), abgeschnittene, fehlerhafte oder falsch geprüfte Nutzdaten,
einen Hash, der in einer der beiden Hälften abweicht, einen Linkverlust bei
jeder Transaktion eines Abrufs, eine behaltene Zeichnung unter derselben
Board-ID mit anderem Hash und eine Platine, die der Build des Panels nicht
kennt. `test/tools/test_gen_board_drawing.py` hält den Generator an dieselben
Regeln.

### Boards mit verlöteten Pins

Register 5 von IDENTITY nennt das Board, für das ein IOmcu-Image gebaut ist.
Das Image wird je Board gebaut (CMake `IOMCU_BOARD`), der Wert steht also
fest im Image.

| Wert | Board | Image | Pins |
|---|---|---|---|
| 1 | RP2350-CAN-Modul, von Hand verdrahtet | `rcbench-iomcu.*` | am Panel gewählt |
| 2 | rbmod-Carrier: dasselbe Modul auf einer Trägerplatine | `rcbench-iomcu-rbmod.*` | verlötet; jede Rolle auf einem Pin |

Die Pins und Stecker des rbmod-Carriers stehen in [Der rbmod-Carrier](Carrier-de.md)
(`shared/outputs/out_board_rbmod.c`).

Auf Board 2 hält der IOmcu jede Rolle auf ihrem Pin:

- CATALOGUE listet die 26 Header-Pins des Moduls. Die Pins der Rollen sind
  frei; jeder andere Pin ist reserviert und wird jeder Page verweigert.
- OUTPUTS und BIND_OUT: ein Slot mit Treiber liegt auf PWM01 bis PWM04
  (GPIO 2, 4, 5 oder 1). Eine Page mit einem Slot mit Treiber auf einem
  anderen Pin wird mit BAD_VALUE abgelehnt. Ein Slot mit Treiber NONE wird
  nicht geprüft.
- PROG: der PIN jedes Ports ist sein eigener Pin (Port 0 GPIO 13, Port 1
  GPIO 14, Port 2 GPIO 15, Port 3 GPIO 0), und FUNC ist NONE. Ein
  Programmierport trägt die Leitungsprotokolle von KST und HITEC und kein
  Signalprotokoll. Ein PIN-Schreiben eines anderen Pins, auch
  `LINK_PG_PIN_NONE`, und ein FUNC-Schreiben jeder anderen Funktion werden
  mit BAD_VALUE abgelehnt.
- TX und RX von SUPPLY sind GPIO 7 und GPIO 6, SDA und SCL von SENSE GPIO 16
  und GPIO 17, der Pin von TONE GPIO 22. Ein Schreiben eines anderen Pins
  wird mit BAD_VALUE abgelehnt. Die übrigen Register der Pages folgen ihren
  eigenen Regeln.
- Beim Start beginnt jede Rolle auf ihrem Pin, was auch immer der Flash
  behalten hat. Ein Datensatz, den das Image des Moduls geschrieben hat,
  behält seine Enables, Bereiche, Raten und Endpunkte. Ein Slot mit Treiber
  auf einem Pin, der kein Servoausgang ist, wird geleert, und jeder Port
  beginnt als NONE.

GPIO 21 wird ab der ersten Anweisung von `main()` low getrieben; ab der
Hauptschleife treibt ihn die Page PROG_SUPPLY (0x39, seit 4.32), aus, bis
eine Sitzung für KST oder Hitec ihn einschaltet. EN von M7 hat einen
Pull-down, die Versorgung der Programmierports ist also aus, solange der
IOmcu GPIO 21 nicht treibt: im Reset, im BOOTSEL und ohne Versorgung. Auf
dem Carrier schalten die Ports für KST und Hitec diese Versorgung statt
des PD mini, und kein Port hält die Page SUPPLY.

Ein Panel bietet jedem IOmcu das Image des Boards an, das er meldet. Das
Release-Panel trägt beide Images; ein Board, für das es kein Image trägt,
bekommt kein Angebot, und UPDATES zeigt `Kein IOmcu-Image für dieses Board
in diesem Panel-Build.`.

`test/host/test_board_fix.c` hält den Katalog des Carriers gegen seine
Rollen, die Pins jeder Page am Pin der Rolle und an den Pins daneben, einen
Datensatz des Moduls auf den Pins des Carriers und das je Board gewählte
Image.

### Abgelehnte Frames

Ab 4.21 zählt der IOmcu die CAN-Frames, die er empfängt und nicht
dekodiert, und veröffentlicht die Zahl in STATUS (0x01) Register 8 und 9
(`LINK_ST_REFUSED_LO` und `LINK_ST_REFUSED_HI`), die niederwertige Hälfte
zuerst. Ein Frame wird an einer von zwei Stufen abgelehnt:

| Stufe | Abgelehnt | Code |
| --- | --- | --- |
| Empfangspuffer | ein 11-Bit-Identifier, ein Remote Frame (RTR, Remote Transmission Request), ein DLC (Data Length Code) über 8 | `mcp2515_recv_next()` in `shared/can/mcp2515.c` |
| Link-Dekodierung | ein unbekannter Op, ein Count über das Ende einer Page, ein DLC, der nicht zum Count passt (ein READ mit Nutzdaten, ein DATA, dessen Nutzdaten nicht 2 Byte je Register sind) | `link_can_decode_counted()` in `shared/link/link_can.c` |

Die Frames des CAN-Selbsttests werden beantwortet, nicht abgelehnt, und
nicht gezählt. Ein READ, den ein Panel mit ESP-IDF v5.5.0 oder v5.5.1 als
DLC 8 statt DLC 0 sendet, wird bei der Link-Dekodierung abgelehnt und
gezählt.

Die Zahl ist eine vorzeichenlose 32-Bit-Zahl. Sie ist 0 nach dem Einschalten
und nach jedem Neustart des IOmcu, nichts über den Link setzt sie
zurück, und sie läuft von 4.294.967.295 auf 0 über.

Das Panel liest STATUS alle 500ms. Einen 4.21-IOmcu fragt es nach 10
Registern, einen älteren nach den 8, die seine Page hat: eine 4.20-Page
weist einen Lesezugriff auf 10 Register mit BAD_RANGE ab, und damit fehlte
jeder Zähler von STATUS. Das Panel vergisst die Zahl bei jedem Link-up und
übernimmt sie wieder aus dem ersten STATUS-Lesezugriff danach.

MOTOR & ESC zeigt die Zahl in seiner Kopfzeile nach der Abfragerate und der
Fehlerzahl, solange der Link steht und die Zahl über 0 liegt:
`Abgelehnte Verbindungsframes: 37`. Kein anderer Screen zeigt sie. Die Logzeile beim Link-up `iomcu  frames ...
refused N` enthält sie ebenfalls. `ui_link_refused_label()` in
`shared/ui/ui_band.c` baut das Label.

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.21 | 4.21 | wie oben |
| 4.21 | 4.20 oder älter | das Panel liest 8 STATUS-Register; die Zahl ist 0, und MOTOR & ESC zeigt kein Label |
| 4.20 oder älter | 4.21 | das Panel liest 8 STATUS-Register und sieht die Zahl nicht |

`test_mcp2515` und `test_link_can` prüfen die Zahl an jeder Stufe, einen
angenommenen Frame, der nichts hinzufügt, und den Überlauf bei 2^32.
`test_link_bringup` trägt die Zahl über CAN-Frames von der Page eines
IOmcu zum Panel, mit einem abgelehnten READ mit DLC 8, 0x0001FFFF
und dem Überlauf, und liest eine 4.20-Page ohne sie. `test_text` prüft das
Label: verborgen bei 0 und ohne Link, gezeigt über 0 auf Englisch und
Deutsch. Der Screenshot `docs/img/de/motor-refused.png` zeigt die Zahl 37.

### Firmware-Update über den Link

Ab 4.22 schreibt das Panel ein neues IOmcu-Image über CAN. Der
IOmcu ist ein RP2350; sein Boot-ROM hält zwei gleich große Image-Slots
hinter einer Partitionstabelle und startet ein neues Image auf Probe, als
TBYB (Try Before You Buy). Einen eigenen Bootloader hat der Prüfstand nicht.

Der Flash des IOmcu (Modul mit 4MB, RP2350A):

| Bereich | Größe | Vor 4.22 | Ab 4.22 |
| --- | ---: | --- | --- |
| 0x000000 bis 0x001FFF | 8KiB | Image | Partitionstabelle |
| 0x002000 bis 0x1FEFFF | 2036KiB | Image | Slot A (Partition 0) |
| 0x1FF000 bis 0x3FBFFF | 2036KiB | Platz für das Image, ungenutzt | Slot B (Partition 1) |
| 0x3FC000 bis 0x3FDFFF | 8KiB | ungenutzt | Ausgangsspeicher, Datenpartition `store` (Partition 2) |
| 0x3FE000 bis 0x3FFFFF | 8KiB | Ausgangsspeicher | ungenutzt: einmal für alte Bindungen gelesen, nie geschrieben |

Ein Slot hat 2.084.864 Byte, 509 Sektoren zu 4KiB. Das Image von 0.18 hat
etwa 192KB, 9% eines Slots. Der letzte Sektor bleibt ungenutzt, weil die
Umgehung für RP2350-E10 beim Laden einer UF2 (USB Flashing Format) auf einen
Chip der Stepping A2 ihn löschen kann. `firmware/iomcu/partitions.json` ist
die Tabelle; der Build gibt ihre Offsets an den Code weiter und schlägt fehl,
wenn das Image nicht in einen Slot passt.

Der Speicher wird über seine Speicheradresse gelesen, durch das
XIP-Fenster (Execute In Place), das weder übersetzt noch cacht
(0x1C000000 + Offset): das Boot-ROM bildet den laufenden Slot auf 0x10000000
ab, also ist 0x10000000 + Offset eine Adresse im Slot, nicht im Speicher.
Ein Speicher ohne Eintrag liest einmal den alten Speicher bei 0x3FE000,
sodass die Bindungen das USB-Laden überstehen, das die Tabelle schreibt. Das
erste Speichern geht in den neuen Speicher.

Ein IOmcu ohne die Tabelle (ein Build vor 4.22 oder ein 4.22-Image,
allein über USB auf ein Modul ohne Tabelle geladen) läuft weiter und
meldet keine Slots: das Panel sagt dann, dass ein USB-Laden nötig ist. Das
eine USB-Laden, das die Tabelle schreibt, steht in
[Erster Lauf](FirstRun-de.md#die-partitionstabelle-des-iomcu).

#### Die Pages

UPDATE_CTL (0xF0), 32 Register:

| Register | Name | Bedeutung |
| ---: | --- | --- |
| 0 | `VERSION` | die eigene Version der Update-Register, 1; sie wächst, wenn am Ende ein Register oder Kommando hinzukommt |
| 1 | `STATE` | 0 untätig, 1 hasht den laufenden Slot, 2 empfängt, 3 empfangen, 4 prüft, 5 geprüft, 6 fehlgeschlagen, 7 aktiviert |
| 2 | `ERROR` | Grund der letzten Ablehnung: 0 keiner, 1 keine Slots, 2 Sperre zu, 3 Größe, 4 Reihenfolge, 5 CRC, 6 Flash, 7 Digest, 8 Zustand, 9 ein Image auf Probe, 10 Link still, 11 BUY abgelehnt, 12 kein solches Kommando |
| 3 | `SLOT` | der laufende Slot in Bit 0 bis 3 (0A, 1B, 15 keiner); Bit 8 das laufende Image ist auf Probe und wartet auf BUY; Bit 9 der erste Sektor des anderen Slots wartet auf sein Löschen |
| 4 | `SLOT_SECTORS` | Sektoren zu 4KiB in einem Slot, 509; 0 ohne Tabelle |
| 5 | `NEXT_BLOCK` | der Block, den COMMIT als nächsten nimmt |
| 6, 7 | `SIZE` | Bytes des Images der Sitzung oder des laufenden Slots, den HASH_RUNNING hasht, niederwertige Hälfte zuerst |
| 8 | `CMD` | ein Schreiben ist ein Kommando; ein Lesen gibt das zuletzt angenommene |
| 9 bis 11 | `ARG0` bis `ARG2` | die Argumente des Kommandos |
| 12 | `DIGEST_OF` | was `DIGEST` hält: 0 nichts, 1 den laufenden Slot, 2 das geschriebene Image |
| 13 bis 15 | | lesen 0 |
| 16 bis 31 | `DIGEST` | ein SHA-256-Digest (Secure Hash Algorithm), 32 Byte, in jedem Register das niederwertige Byte zuerst |

Ein Kommando ist ein Frame: ein Schreiben von 1 bis 4 Registern ab Register
8, die Argumente dahinter. Jedes andere Schreiben auf UPDATE_CTL wird mit
READ_ONLY abgelehnt. Ein Kommando, das nicht zum Zustand passt, wird mit
BAD_VALUE abgelehnt, der Grund steht in `ERROR`.

| Kommando | Wert | Argumente | Wirkung |
| --- | ---: | --- | --- |
| HASH_RUNNING | 1 | Bytes, niederwertige und höherwertige Hälfte | SHA-256 der ersten Bytes des laufenden Slots, das TBYB-Flag als gelöscht gelesen; nur untätig |
| BEGIN | 2 | Bytes des Images, niederwertige und höherwertige Hälfte | beginnt eine Sitzung: 1 Byte bis ein Slot, löscht den ersten Sektor des anderen Slots |
| COMMIT | 3 | Blocknummer, CRC-16 | UPDATE_DATA ist dieser Block |
| FINISH | 4 | | vergleicht den SHA-256 des Slots mit UPDATE_DATA Register 0 bis 15 |
| ACTIVATE | 5 | | schreibt den ersten Sektor, liest ihn zurück und startet neu in den Slot |
| ABORT | 6 | | beendet die Sitzung; der Slot hält kein Image |
| BUY | 7 | | behält das Image auf Probe; ohne ein solches bestätigt, ohne etwas zu tun |

UPDATE_DATA (0xF1) hält die 64 Byte eines Blocks in 32 Registern, in jedem
Register das niederwertige Byte zuerst. Nichts auf beiden Pages übersteht
einen Neustart.

#### Die Übertragung

1. Das Panel liest IDENTITY, dann UPDATE_CTL. BAD_PAGE auf UPDATE_CTL oder
   `SLOT` 15 beendet hier: der IOmcu nimmt Images nur über USB.
2. Das Panel hasht das Image, das es mitbringt, 8KiB je Schritt, und sendet
   BEGIN mit seiner Größe.
3. Für jeden 64-Byte-Block, der Reihe nach ab Block 0: 8 Daten-Frames in
   UPDATE_DATA, dann COMMIT mit der Blocknummer und ihrem CRC-16 (Cyclic
   Redundancy Check, CCITT-Polynom 0x1021 mit Startwert 0xFFFF, über die
   Bytes des Blocks). Der letzte Block ist der Rest des Images, und sein CRC
   deckt nur diese Bytes. Jeder Frame wartet auf seine Bestätigung; nichts
   anderes ist unterwegs.
4. Der IOmcu puffert 64 Blöcke und schreibt einen 4-KiB-Sektor, wenn
   der letzte davon committet. Dieses COMMIT wird erst beantwortet, wenn der
   Sektor gelöscht und programmiert ist, etwa 19,2ms.
5. FINISH: der IOmcu hasht den Slot wie geschrieben, mit dem ersten
   Sektor, wie er sein wird, 1KiB je Durchlauf, und vergleicht den Digest.
6. ACTIVATE: der IOmcu schreibt den ersten Sektor zuletzt, liest ihn
   zurück, antwortet und startet 20ms später als Flash-Update-Boot in den
   neuen Slot.
7. Das Boot-ROM prüft den SHA-256 des Images und startet es auf Probe unter
   seinem Watchdog von 16,7s. Das Panel wartet 500ms, liest IDENTITY, bis
   das neue Image antwortet, und schreibt BUY. Das Boot-ROM löscht das
   TBYB-Flag im Flash. Danach löscht der IOmcu den ersten Sektor des
   Slots, aus dem er kam, sodass das Boot-ROM beim nächsten Start ein Image
   zur Wahl hat. BUY wird bestätigt, sobald das Boot-ROM es angenommen hat.
   Ein Löschen, das scheitert oder auf die Sperre des Speichers wartet, bleibt
   offen: `SLOT` Bit 9 ist gesetzt, der IOmcu versucht es einmal je
   Sekunde erneut, solange die Sperre offen ist, und ein Start, der den
   ersten Sektor des anderen Slots außerhalb einer Sitzung oder einer Probe
   beschrieben findet, schuldet das Löschen erneut. Das Panel zeigt dafür
   nichts an. Ein IOmcu ohne eine Partitionstabelle, die er schreibt,
   hat keinen anderen Slot und löscht nichts.

Ein Block vor `NEXT_BLOCK` wird bestätigt und nicht noch einmal geschrieben:
ein COMMIT, dessen Bestätigung verloren ging, wird folgenlos erneut gesendet.
Jede andere Blocknummer wird mit ORDER abgelehnt. Ein Block, der seinen CRC
nicht besteht, wird abgelehnt und bis zu 3-mal neu gesendet. Ein teilweise
geschriebener Slot hält kein Image, das das Boot-ROM startet: der erste
Sektor wird bei BEGIN gelöscht und bei ACTIVATE geschrieben.

Das Image, das das Panel sendet, trägt das TBYB-Flag. Das Binary eines
Builds trägt keines, also startet ein über USB geladenes Image als normaler
Boot. Das Panel setzt das Flag in seiner Kopie beim Senden. Die
picobin-Version des Images für das Boot-ROM ist 1000 x Major + Minor und
Patch, 0.18.0 ist also Version 18.0.

#### Grenzen und Fehler

| Bedingung | IOmcu | Panel |
| --- | --- | --- |
| eine Sitzung oder ein Image auf Probe | lehnt ARM mit NOT_ARMED ab; SAFE wird nie abgelehnt | zeigt den Fortschritt, kein Band |
| Sperre des Speichers zu (Bank treibt, Ausgang der Versorgung an, Sitzung der Servo-Programmierung) | lehnt BEGIN, COMMIT und ACTIVATE mit BUSY ab | fragt erneut, gibt nach 10s auf |
| 5s keine Anfrage an UPDATE_CTL oder UPDATE_DATA in einer Sitzung; Anfragen an andere Pages zählen nicht | beendet sie mit Fehler 10 | beginnt wieder bei Block 0 |
| eine Sitzung bei der Prüfung gefunden, die dieses Panel vor einem Neustart oder einem verlorenen Link hinterlassen hat | mit ABORT beendet | prüft weiter: liest den Zustand erneut und fragt nach dem Hash |
| ACTIVATED bei der Prüfung gefunden | startet innerhalb von 20ms neu | liest erneut bis zum Neustart, gibt nach 10s auf |
| ein Neustart mitten in der Übertragung | die Sitzung ist weg | beginnt wieder bei Block 0, höchstens 3-mal |
| 4s keine Antwort | | gibt auf: `keine Antwort` |
| SHA-256 passt bei FINISH nicht | Zustand fehlgeschlagen, Fehler 7 | `Firmware-Prüfung fehlgeschlagen`, das alte Image läuft |
| kein BUY innerhalb 16,7s nach dem Neustart | das Boot-ROM startet neu in den alten Slot | gibt nach 12s auf: `neue Firmware ist nicht gestartet` |
| Hash des laufenden Slots oder des Images | | gibt nach 10s auf |

Ein gescheitertes oder abgebrochenes Update lässt das alte Image laufen und
den anderen Slot ohne Image. Das nächste Update schreibt ihn wieder ab
Block 0.

#### Welcher Build läuft

Versionen allein erkennen keinen Entwicklungs-Build. Beim Link-up vergleicht
das Panel den SHA-256 des mitgebrachten Images, das TBYB-Flag als gelöscht
gelesen, mit HASH_RUNNING des IOmcu über dieselbe Zahl von Bytes.

| Ergebnis | Panel |
| --- | --- |
| gleich | nichts |
| verschieden | der Alarm `IOmcu nutzt einen anderen Build. Update unter SETUP > UPDATES.`; die Frage öffnet sich auf dem Menü, solange der Prüfstand SAFE ist, der Ausgang der Versorgung aus, der Funk des Panels ganz aus und keine Panel-Installation im Gang |
| ein Image auf Probe | schreibt sofort BUY und meldet `Neue IOmcu-Firmware dauerhaft übernommen.` |
| BAD_PAGE oder keine Slots | der Alarm `IOmcu kann nicht über CAN aktualisiert werden. Firmware einmal über USB installieren.` |

Die Prüfung läuft auch bei einer abweichenden Protokoll-Major: die
Identitätsregister und die Update-Pages antworten über Majors hinweg, und das
Update bringt den IOmcu auf die Major des Panels. Ein CANCEL gilt für
diese Version des IOmcu, bis das Panel neu startet. Ein älteres
mitgebrachtes Image wird ebenfalls angeboten; die Frage sagt das dann.
[Screens](Screens-de.md#iomcu-update) zeigt Frage und Fortschritt.

Ein Panel, das ohne `RCBENCH_IOMCU_BIN` gebaut ist, bringt kein Image mit,
prüft nichts und bietet nichts an.

#### Zeit

Für das Image von 0.18, 191.592 Byte: 2.994 Blöcke, 9 Exchanges je Block,
26.946 Exchanges und 47 Sektoren. Ein Frame dauert bei 1Mbit/s 131 bis
160µs, die Buszeit von Anfrage und Bestätigung ist also 7,1 bis 8,6s, und
das Schreiben der Sektoren fügt 0,9s hinzu. Mit der Round Trip von 334 bis
1400µs aus [Budget](#budget) dauern die Exchanges 9,0 bis 37,7s. Das ganze
Update, die Hashes des IOmcu und sein Neustart sind nicht gemessen.

Der simulierte IOmcu der Host-Suite führt ein Update in voller Größe
mit dem Image von 0.18.0 (184.872 Byte, 2.889 Blöcke) in 26.104 Exchanges
aus.

#### Kompatibilität des Updates

| Panel | IOmcu | Verhalten |
| --- | --- | --- |
| 4.22 mit Image | 4.21 oder älter | BAD_PAGE: der USB-Alarm; der Link läuft weiter |
| 4.22 mit Image | 4.22, ohne Tabelle | keine Slots: der USB-Alarm |
| 4.22 mit Image | 4.22 oder spätere Minor, mit Tabelle | prüfen, anbieten, aktualisieren |
| 4.22 mit Image | eine andere Major, 4.22 oder später | der Link bleibt unten; prüfen, anbieten, aktualisieren über die eingefrorenen Pages |
| 4.21 oder älter | 4.22 | die Update-Pages werden nie gefragt; der IOmcu läuft wie ein 4.21-IOmcu |

`test_link_update` führt die Pages des IOmcu und die Engine des Panels
über die Link-Schicht gegen einen simulierten Flash und ein simuliertes
Boot-ROM aus: Block 0, der letzte Teilblock, ein Image von genau einem Slot
und eines mit einem Byte mehr, ein CRC-Fehler, Blöcke außer der Reihe und
wiederholt, ein Neustart mitten in der Übertragung, NACKs, ein Linkausfall,
ARM während der Sitzung, eine geschlossene Sperre, ein falscher Digest, ein
fehlendes BUY, das BAD_PAGE eines 4.21-IOmcu sowie eine andere Minor
und Major. Dazu kommen ein Neustart des Panels, der die Sitzung in RECEIVING,
RECEIVED, VERIFYING und VERIFIED zurücklässt, während das gewöhnliche Polling
läuft, eine Sitzung, mit der nur das gewöhnliche Polling spricht, eine
Prüfung, die den fälligen Neustart findet oder einen Neustart, der nie kommt,
ein ABORT bei der Prüfung, das verloren geht, abgelehnt wird oder nicht
greift, und ein Löschen nach BUY, das scheitert oder auf die Sperre wartet, auch über einen
Neustart hinweg. `test_link_sha256` prüft den Hash an den Vektoren aus FIPS 180-4.
`test_link_pages` prüft die eingefrorenen Registernummern.

### Bit Timing

`shared/can/can_timing.c` berechnet die Segmentierung für den Takt jedes
Controllers und verlangt, dass die Bitrate exakt aufgeht. Beide Enden tasten
bei 75% des Bits ab:

| Controller | Takt | Prescaler | Quanta je Bit | tseg1 | tseg2 | sjw |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TWAI (ESP32-P4) | 40-MHz-Quarz | 2 | 20 | 14 | 5 | 4 |
| XL2515 | 16-MHz-Quarz, intern halbiert | 1 | 8 | 5 | 2 | 2 |

Acht Quanta sind das Minimum für ein Bit. Deshalb braucht der XL2515 für
1Mbit/s einen 16-MHz-Quarz, und deshalb liegt sein Sample Point fest bei
75%; das TWAI-Timing ist passend dazu gewählt. `test_can_timing` hält die Zeile des
XL2515 fest. Für das TWAI prüft es den Sample Point von 75% und eine
Sprungbreite von mindestens 2, nicht die Werte der Zeile.

### Budget

Ein Poll der Bench-Page ist ein Anfrage-Frame und vier Daten-Frames (13
Register). Bei 20Hz sind das 1,55% des Busses. Solange ein Strommonitor
eingeschaltet ist, liest das Panel die Register 12 bis 25 von SENSE (14
Register, ein Anfrage-Frame und vier Daten-Frames) mit 20Hz, etwa 1,6%, mit
freigegebenem Ausgangsencoder die Register 12 bis 31 (20 Register, ein
Anfrage-Frame und fünf Daten-Frames), etwa 1,9%, und solange der INA3221 eingeschaltet ist, die Register 0 bis 13 von SERVO_SENSE
mit 20Hz, jedes 50-ms-Fenster, etwa 1,6%: zusammen etwa 3,2%.

An einem IOmcu mit 4.11 und mit dem INA3221 auf CH1 liest das Panel
außerdem die Register 0 bis 15 von SERVO_WIN mit 20Hz (16 Register, ein
Anfrage-Frame und vier Daten-Frames, 0,78ms Buszeit je Poll, etwa 1,6%),
und das Lesen von SENSE geht für `RESETS` bis Register 31 (ein Daten-Frame
mehr, 0,16ms, etwa 0,3%). Die drei Lesezugriffe sind dann 16 Frames je
Poll, 2,5ms, etwa 5,0% des Busses, gegen 10 Frames und 3,1% ohne den
Ring. Ein Poll, der 100ms oder mehr nach dem letzten Lesen beginnt, das
kein Fenster schuldig blieb, liest stattdessen die ganze Page, 28 Register,
ein Anfrage-Frame und sieben Daten-Frames: 19 Frames, 2,9ms. Ein Lesen der
Register 0 bis 15 mit folgendem Lesen der ganzen Page, nach einem Ring, der
neu begonnen hat, sind 24 Frames, 3,7ms, einmal. Mit dem Lesen von BENCH
und dem von TONE sind die Lesezugriffe eines Polls höchstens 34 Frames,
5,3ms Buszeit im 50-ms-Poll, und SERVO_WIN ist ein Austausch mehr darin.
Der Ring hält 200ms, die Fenster überstehen also einen Poll, der viermal
so lange dauert wie seine Periode. `test_sense_windows` hält die Zahl der
Frames. Die Dauer eines Austauschs auf dem Prüfstand ist nicht gemessen.

Solange der
Tap freigegeben ist, sind das Lesen der Register 8 bis 23 von TONE (16
Register, ein Anfrage-Frame und vier Daten-Frames wie beim BENCH-Lesen) mit
20Hz etwa 1,7% mehr, unter 2%, für die drei Pages zusammen 4,9%, und das Lesen von
EVT_SEL und der Register 14 bis 21 je neuem Beep ein Schreib-Frame, eine
Anfrage und zwei Daten-Frames. Jedes Lesen ist ein Austausch
mehr im 50-ms-Poll. Die Nutzlast von Classic CAN
bei 1Mbit/s mit 29-Bit-Identifiern und vollem Bit Stuffing liegt im
ungünstigsten Fall bei etwa 52kB/s; der erwartete Verkehr liegt bei 12 bis
30kB/s.

### Tests

Das Identifier-Layout wird Bit für Bit über den gesamten 29-Bit-Raum
geprüft; der Timing-Rechner ist an von Hand nachgerechnete Beispiele
gebunden; `test_link_loopback` lässt den Host-Poller gegen den
Device-Dispatcher laufen, über einen Bus, der Frames verwirft, verzögert und
umsortiert: geteilte Antworten in umgekehrter Reihenfolge, abgewiesene
Schreibzugriffe, verlorene Teilstücke, die eine Anfrage unbeantwortet lassen
statt halb beantwortet, und der Watchdog des Geräts, der auf einem stillen
Bus feuert. `test_bind_link` lässt die Schreib- und Lesefolge einer Bindung
gegen die Page-Regeln des IOmcu laufen, über einen Bus mit dem
2-Frame-Puffer des XL2515: siehe [Eine Bindung als
Ganzes](#eine-bindung-als-ganzes).
