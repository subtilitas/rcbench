# Der Link

<sub>[English](Link.md) · **Deutsch**</sub>

Die beiden Platinen kommunizieren über CAN (Controller Area Network) mit
1 Mbit/s. Zuerst die Verkabelung, dann die Protokollreferenz.

## Verkabelung

| | |
| --- | --- |
| Bus | Classic CAN (kein CAN FD (Flexible Data Rate)), 1 Mbit/s, nur 29-Bit-Identifier |
| Panel | TWAI-Controller (Two-Wire Automotive Interface, der CAN-Controller des ESP32-S3) auf GPIO19 (RX) und GPIO20 (TX; GPIO: General-Purpose Input/Output), über den USB/CAN-Multiplexer der Platine (USB: Universal Serial Bus) |
| Koprozessor | XL2515 (MCP2515-kompatibel) an `spi1`: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8; SPI-Takt 10 MHz (SPI: Serial Peripheral Interface) |
| Transceiver | SIT65HVD230 (3,3 V) auf dem Koprozessormodul |
| Abschluss | 120 Ω an beiden Enden |
| Quarz | der XL2515 braucht einen 16-MHz-Quarz; 8 MHz begrenzen den Controller auf 500 kbit/s |

Wer CAN wählt, verliert das native USB des Panels. GPIO19 und GPIO20 führen
beides, und der Multiplexer FSUSB42UMX (CH422G EXIO5: 0 = USB, 1 = CAN) wählt
eines aus. Die Konsole liegt deshalb auf UART0 (Universal Asynchronous
Receiver-Transmitter), der zweiten USB-C-Buchse der Platine hinter der
USB-UART-Bridge, mit USB-Serial-JTAG (der eingebauten USB-Seriell- und
Debug-Bridge des ESP32-S3) als Zweitkonsole.

Kommen keine Frames durch: [Den Link in Betrieb nehmen](Bringup-de.md).

## Verhalten bei Linkausfall

Der Koprozessor setzt nach 200 ms ohne Anfrage Failsafe-Werte; das Panel
eskaliert nach 1 s ohne Antwort. Das Failsafe rastet ein. Zurückkehrender
Verkehr stoppt den Stillezähler, hebt das Failsafe aber nicht auf; verlassen
wird es durch das Schreiben von 0x5AFE in das Register CLEAR der
Control-Page.

Der Koprozessor hält außerdem einen Arm-Latch. Er wird bei jedem Start
gesetzt, an der Flanke in die Stille auf dem Link, wenn dem Heartbeat nicht
mehr vertraut wird, und durch ein ARM, das der Koprozessor abweist. Solange
er gesetzt ist, wird ein Schreiben von ARM mit NOT_ARMED abgewiesen. Dasselbe
CLEAR löst ihn. Er ist nicht das Failsafe: STATUS meldet ihn nicht, und das
ON der Versorgung liest ihn nicht. Die Regeln stehen in
[Sicherheit](Safety-de.md#arm-latch).

### Bus off

Ein CAN-Transmitter, der kein Acknowledge bekommt, wiederholt von sich aus und
addiert je Versuch 8 auf seinen Transmit Error Counter. Bei 1 Mbit/s erreicht
ein Frame, den niemand beantwortet, die Bus-off-Schwelle von 256 in etwa 4 ms
— jeder Moment ohne zweiten Knoten am Bus nimmt den Controller des Panels also
vom Bus: Koprozessor ohne Strom, nach einem Reset, oder lange genug mit
abgeschalteten Interrupts.

Der TWAI-Controller (Two-Wire Automotive Interface) des ESP32-S3 kommt nicht
von selbst zurück. `can_twai_recover()` läuft am Identity-Poll, solange der
Link unten ist, einmal pro Sekunde: aus dem Zustand Bus off ruft es
`twai_initiate_recovery()`, wartet die 128 Bus-frei-Signale ab, die der
Controller im Zustand Recovering zählt, und startet ihn aus dem Zustand
Stopped wieder. Die Erholung dauert damit bis zu drei Polls, also rund 3 s.
Ohne sie ist der erste stille Moment am Bus endgültig und nur ein
Aus-und-Einschalten hilft.

Der Report, der bei liegendem Link alle 5 s ausgegeben wird, trägt die Zähler:

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
Fenstern. Der Koprozessor sendet nur als Antwort auf eine Anfrage.
Protokollversion 4.11. Die Major-Version ist Register 0 der Page 0. Die Major
ändert sich, wenn ein Register seine Bedeutung wechselt oder eine Page
umnummeriert wird; die Minor, wenn eine Page oder ein Register am Ende
hinzukommt, was ein älteres Panel ignorieren kann.

Ein Koprozessor, der eine andere Protokoll-Major meldet als das Panel, gilt
als abwesend: der Link bleibt unten, der Splash markiert den
Koprozessor-Schritt als fehlgeschlagen neben der gemeldeten Version, das
Panel protokolliert beide Majors und meldet `Protokoll passt nicht -- kein ARM`, und kein Write erreicht den Koprozessor. Seine Ausgänge bleiben aus,
und das Panel läuft wie ohne angeschlossenen Koprozessor. Panel und
Koprozessor auf verschiedenen Seiten eines Major-Sprungs werden deshalb
zusammen geflasht.

### Kompatibilität

Verglichen wird nur die Major. Der Link kommt hoch und der Prüfstand wird
scharf, gleich welche Minor die beiden haben. Das Panel liest die Minor des
Koprozessors beim Link-up und nutzt nichts, was diese Minor nicht hat: die
Frame Rate von SERVO ab 4.1, seinen Sweep ab 4.2, SUPPLY ab 4.3, RESUME des
Sweeps ab 4.6, SENSE und SERVO_SENSE ab 4.7, TONE ab 4.8, den Ausgangsencoder von SENSE ab 4.9,
BIND_CFG, BIND_OUT und BIND ab 4.10, SERVO_WIN, `RESETS` von SENSE und ein
vorzeichenbehaftetes `CAP_HOLD_MA` ab 4.11. Der Koprozessor liest
die Minor des Panels nie; eine Page, die ein älteres Panel nicht kennt, schreibt es
nie.

| Panel | Koprozessor | Link und Schärfen | SENSE (0x2B) und SERVO_SENSE (0x2C) |
| --- | --- | --- | --- |
| 4.7 | 4.7 | hoch, wird scharf | bedient |
| 4.7 | 4.6 | hoch, wird scharf | das Panel sendet an keine der beiden Pages; der Koprozessor beantwortet beide mit BAD_PAGE. BENCH-Bits 5 und 6 lesen 0 |
| 4.6 | 4.7 | hoch, wird scharf | das Panel liest und schreibt keine der beiden Pages und übergeht BENCH-Bits 5 und 6. Eine Konfiguration, die ein 4.7-Panel im Flash des Koprozessors hinterlassen hat, bleibt in Kraft, und ihre zwei Pins bleiben den Ausgängen entzogen. Sobald der Koprozessor einen INA228 liest, trägt BENCH dessen Spannung und Strom in denselben Skalen, und das Panel zählt Ladung und Energie selbst aus diesem Strom |

Der Koprozessor dieses Builds bedient beide Pages, hält die Konfiguration im
Flash und hält ihre Pins. Sein Core 1 liest die freigegebenen Bauteile im
1-ms-Takt, und Core 0 trägt ein, was Core 1 gelesen hat; die Nur-Lese-Register
lesen 0, bis Core 1 unter der geltenden Konfiguration gelesen hat, etwa 1 ms
nach einer Änderung. Solange der INA228 antwortet, trägt BENCH seine
Spannung und seinen Strom (die Mittelwerte des letzten 50-ms-Fensters), ihr
Produkt als Leistung, die Spitzen des Laufs aus seinen 500-Hz-Messungen
sowie seine Ladung und Energie, mit Bits 5 und 6. An der Flanke zum Treiben
löscht er Bit 6 und die Register für Ladung und Energie im selben Durchlauf
und nicht erst bei seiner nächsten 50-Hz-Abtastung, sodass kein Lesen nach
dem Scharfschalten die Summen des letzten Laufs trägt. Ein Lauf, der mit
antwortendem INA228 beginnt, behält ihn bis zu seinem Ende als Quelle von
BENCH: hört er auf zu antworten, bleiben diese Felder leer, statt zur
Telemetrie des ESC zurückzukehren. Bei einer Messung der Bewegung ist der
Pegel vor dem Befehl der Mittelwert von CH1 über die 50 ms vor der Flanke;
eine Messung, die scharfgeschaltet wird, während der INA3221 CH1 nicht
liest, endet sofort als verloren und zählt in CAP_SEQ. Nichts davon ist auf Hardware
gelaufen. Das Panel dieses Builds liest bei jedem Link-Aufbau die Register 0
bis 11 von SENSE, schreibt die Einstellung aus SETUP ANSCHLÜSSE Frame für
Frame, wo sie abweicht (`shared/bench/sense_link.c`, `test_sense_link`), und
liest die Identity-Page erneut, nachdem ein Schreiben angenommen ist. Auf
SERVO_SENSE schreibt es keine Messung, und an SERVO_WIN (0x31) sendet es
nichts.

TONE (0x2D) ist eine eigene Page, und 4.8 verschiebt sonst nichts:

| Panel | Koprozessor | TONE (0x2D) |
| --- | --- | --- |
| 4.8 | 4.8 | bedient |
| 4.8 | 4.7 | der Koprozessor beantwortet jedes Lesen und Schreiben der Page mit BAD_PAGE; ein Panel liest beim Link-up die Minor und sendet dorthin nichts |
| 4.7 | 4.8 | das Panel liest und schreibt die Page nicht. Ein Tap, der im Flash des Koprozessors steht, startet beim Booten, läuft und wird nie gelesen; sein Pin bleibt den Ausgängen entzogen |

Die Page hat kein Capability-Bit. Ein Panel, das die Minor liest, weiß, dass
die Page existiert; FLAGS sagt, ob der Tap läuft.

Der Koprozessor dieses Builds bedient die Page und hält die Register 0 bis 6
im Flash (Speicher-Record Version 6; ein Record der Version 3 bis 5 liest mit
ausgeschaltetem Tap auf seinen Standardwerten). Core 0 besitzt die
PIO-State-Machine (PIO: Programmable Input/Output) und ihren DMA-Kanal (DMA:
Direct Memory Access), Core 1 liest den Ring im 1-ms-Takt und lässt den
Detektor laufen, und Core 0 trägt die fertigen Beeps und den Status in die
Page ein. Die Page, der Ring-Leser, der Service und das PIO-Programm, in einem
taktgenauen Modell der State Machine ausgeführt, sind auf dem Host getestet
(`test_tone_page`, `test_edge_ring`, `test_tone_svc`, `test_tone_pio`). Nicht
auf Hardware gelaufen: das PIO-Programm, der DMA-Ring, das Pad und die
Z-Diode, der Durchlauf auf Core 1, das Laden aus dem Flash und die Töne
jedes ESC.

Der Ausgangsencoder gehört zu SENSE, und 4.9 verschiebt sonst nichts: ENABLE
bekommt Bit 2, und die Page wächst von 26 auf 32 Register, 26 bis 30 neu und
31 reserviert.

| Panel | Koprozessor | Der Encoder (SENSE Bit 2, Register 26 bis 31) |
| --- | --- | --- |
| 4.9 | 4.9 | bedient |
| 4.9 | 4.8 oder 4.7 | das Panel schreibt ENABLE Bit 2 nicht und liest von SENSE nur die Register 12 bis 25; mit dem Encoder in SETUP freigegeben meldet es `Koprozessor älter als 4.9 -- AS5600 nicht gelesen`, beim Link-up und wenn der Encoder freigegeben wird, während der Koprozessor antwortet. Die Page eines 4.8-Koprozessors hat 26 Register und weist ein Lesen darüber hinaus ab |
| 4.9 | 4.6 oder älter | an SENSE wird nichts gesendet; dieselbe Meldung |
| 4.8 oder 4.7 | 4.9 | das Panel schreibt Bit 2 nie und liest die Register 26 bis 31 nie. Ein Encoder, den ein 4.9-Panel im Flash des Koprozessors hinterlassen hat, bleibt freigegeben und wird gelesen; seine zwei Pins bleiben den Ausgängen entzogen |

Der Encoder ist ein AS5600 von ams OSRAM auf der Ausgangswelle des Servos, an
seiner festen Adresse 0x36 auf dem Bus, den SENSE schon betreibt, standardmäßig
GP16 und GP17. Er hat auf der Page keinen Parameter außer dem Freigabe-Bit:
Pins und die 400 kHz sind die des Busses. Jedes Register wird in einer
eigenen Transaktion gelesen, da RAW ANGLE und MAGNITUDE Register sind, die das
Bauteil besonders behandelt: der Adresszeiger läuft nicht über sie hinaus,
und ein Lesen, das von STATUS oder AGC her durch das Weiterzählen dort
ankommt, wird nicht vorausgesetzt. Der Koprozessor liest STATUS (1 Byte) und
RAW ANGLE (2 Byte) in jedem zweiten 1-ms-Takt, 500 Hz, in den Takten ohne
Rotationseintrag: 217,5 µs Buszeit bei 400 kHz. In einem von 25 dieser Plätze
liest er stattdessen RAW ANGLE, AGC (1 Byte) und MAGNITUDE (2 Byte):
337,5 µs. Das sind 500 Winkellesungen je Sekunde im Abstand von 2 ms, 480
STATUS-Lesungen und 20 Lesungen von AGC und MAGNITUDE. Eine Transaktion von N
Byte dauert 9 × (3 + N) + 3 Takte; die Zeit des Controllers zwischen den
Transaktionen ist nicht gezählt und nicht gemessen. Ein ungerader Takt ist
CH1, das Paar, wenn es läuft, der Platz des INA228 und der Encoder: 480 µs,
600 µs in einem Platz mit MAGNITUDE, mit dem Paar 720 µs und 840 µs. Ein
gerader Takt bleibt unter 690 µs. Der Encoder fügt der Buszeit einer Sekunde
11,1 % hinzu: 44,0 % insgesamt, 66,8 % mit dem Paar.

| Register | Name | Liest |
| ---: | --- | --- |
| 26 | `AS5600_FLAGS` | Bit 0 antwortet an 0x36 mit einem STATUS, den ein AS5600 geben kann; Bit 1 STATUS MD (Magnet erkannt); Bit 2 ML (Feld zu schwach); Bit 3 MH (Feld zu stark); Bit 4 an 0x36 antwortet etwas mit ML und MH zugleich, was kein AS5600 gibt, und wird nicht benutzt; Bit 5 der Winkel und die Bits 1 bis 3 enthalten einen Messwert dieser Konfiguration. Bits 1 bis 3 lesen 0, solange Bit 0 es tut. `AS5600_ANGLE` ist nur eine Position, solange die Bits 0, 1 und 5 alle gesetzt sind |
| 27 | `AS5600_ANGLE` | RAW ANGLE, 0 bis 4095 für eine Umdrehung (360 / 4096 = 0,0879 Grad je Schritt), zuletzt gelesen und ohne Mitte; 0 bis zum ersten Lesen |
| 28 | `AS5600_MAGNITUDE` | die CORDIC-Magnitude (CORDIC: Coordinate Rotation Digital Computer), 12 Bit, mit 20 Hz gelesen; 0 bis zum ersten Lesen |
| 29 | `AS5600_SAMPLES` | Winkellesungen modulo 65536: zwei Lesungen der Page mit demselben Zählerstand sind eine Messung |
| 30 | `AS5600_STILL_MS` | wie lange der Winkel innerhalb von 12 Schritten (1,05 Grad) eines Ankers geblieben ist, in ms, bei 65535 gesättigt; 0 ohne Messung und solange MD nicht gesetzt ist |
| 31 | `RESETS` | seit 4.11, und nicht vom Encoder: die Strommonitore, die zurückgesetzt gefunden wurden, der INA228 im Low-Byte und der INA3221 im High-Byte, je modulo 256, seit die geltende Konfiguration angenommen wurde. [Der Fenster-Ring](#der-fenster-ring) nennt die Regel. Ein Koprozessor mit 4.10 oder älter liest 0 |

Der Anker ist der Winkel der letzten Messung, die mehr als 12 Schritte vom
vorigen Anker lag, auf dem Kreis über den Sprung von 4095 auf 0 gerechnet. Eine
Bewegung, die zur Zeit t_ende endet und zur Zeit t_lesen gelesen wird, zeigt
`AS5600_STILL_MS` = t_lesen - t_ende auf das 2-ms-Messintervall genau, das
Panel misst das Ende einer Bewegung also in der Auflösung des Koprozessors,
gleich wie oft es selbst abfragt. Der eigene Fehler des Panels ist die Zeit
zwischen dem Lesen im Koprozessor und dem Eintreffen der Antwort im Panel:
nicht gemessen. Fällt das Bauteil aus oder gilt eine neue Konfiguration, werden
der Anker und die Register 26 bis 31 gelöscht.

Die Magnetbits sind STATUS (0x0B) Bit 5 MD, Bit 4 ML und Bit 3 MH. Das
Datenblatt (ams AS5600 DS000365 v1-06) sagt zu nicht gesetztem MD: "If the
measured magnet field strength goes below the minimum specified level
(Bz_ERROR), the output is driven low [...] and the MD bit in the STATUS
register is 0", mit Bz_ERROR bei 8 mT. Der Zählerstand ist dann keine
Position: eine Messung ohne MD löscht den Anker, `AS5600_STILL_MS` liest 0,
und das Panel gibt den Winkel an nichts weiter -- nicht an die Seite SERVO,
nicht an ENC-MITTE, nicht an einen Lauf. ML und MH sind "AGC maximum gain
overflow, magnet too weak" und "AGC minimum gain overflow, magnet too
strong"; das Datenblatt nennt keine Wirkung auf den Winkel und spezifiziert
sein Rauschen nur für 30 bis 90 mT. Mit gesetztem MD wird der Zählerstand
verwendet, was auch immer ML und MH sagen, und das Panel meldet sie. Ein
Slot, der MAGNITUDE liest, liest kein STATUS und richtet sich nach dem 2 ms
davor gelesenen.

Die STATUS-Bits 7, 6, 2, 1 und 0 sind in der Registerübersicht des
Datenblatts leer, und deren Anmerkung sagt: "Blank fields may contain
factory settings". Sie werden ausmaskiert und entscheiden nichts. Das
Bauteil hat kein Identitätsregister: der Probe nimmt, was an 0x36 antwortet,
als AS5600, außer bei einem STATUS mit ML und MH zugleich -- die Verstärkung
an beiden Enden ihres Bereichs, was ein Gerät zeigt, das 0xFF antwortet, und
ein AS5600 nicht. Diese Ausnahme folgt aus der Bedeutung der beiden Bits; das
Datenblatt nennt sie nicht.

STATUS und RAW ANGLE sind für
die Fehlerzählung eine Messung: ein fehlschlagendes Lesen von RAW ANGLE zählt
als Fehler, auch wenn das STATUS-Lesen davor geantwortet hat; 3 Messungen in
Folge mit fehlschlagendem RAW-ANGLE-Lesen nehmen das Bauteil offline (3
fehlgeschlagene Transaktionen in Folge, wie bei den INA-Bauteilen). Das Panel liest die Register 12
bis 31 alle 40 ms, solange der Encoder auf der Page freigegeben ist. Die
Register 26 bis 31 sind nur lesbar; ein Schreiben wird mit READ_ONLY abgewiesen.

Nicht auf Hardware gelaufen: der AS5600 am Bus neben den INA-Bauteilen, seine
Adresse, die Pull-ups, die Zeit des Ablaufs auf dem Bus, das Messintervall und
die Toleranz von 12 Schritten gegen das Rauschen eines echten Servos.

### Eine Bindung als Ganzes

BIND_CFG (0x2E), BIND_OUT (0x2F) und BIND (0x30) sind neu in 4.10, und 4.10
verschiebt sonst nichts. Eine Bindung sind zwei Pages mit 32 Registern,
CHAN_CFG und OUTPUTS, 16 Frames. Ein Schreiben auf eine der beiden gilt Frame
für Frame, eine Folge, die mittendrin endet, lässt also die ersten Einträge
der einen Bindung und den Rest einer anderen stehen. Die drei Pages machen aus
der Änderung einen Schritt:

| Page | Register | Regeln |
| --- | --- | --- |
| BIND_CFG | 32, wie CHAN_CFG | eine CHAN_CFG-Page, die nicht gilt. Ein Frame wird nach den Wertregeln von CHAN_CFG geprüft (Rolle 0 oder 1, Endpunkte 400 bis 2500 µs) und bei einem Verstoß mit BAD_VALUE abgelehnt, ohne etwas zu speichern. Kein Ausgang ändert sich |
| BIND_OUT | 32, wie OUTPUTS | eine OUTPUTS-Page, die nicht gilt. Ein Frame wird nach den Wertregeln von OUTPUTS geprüft (ein Treiber 0 bis 4, eine Pin-Nummer bis 63) und bei einem Verstoß mit BAD_VALUE abgelehnt. Kein Ausgang ändert sich |
| BIND | 1: `COMMIT` | ein Schreiben trägt die CRC (Cyclic Redundancy Check) der 64 vorbereiteten Register. Liest die CRC dessen, was vorbereitet ist |

Die CRC ist CRC-16/CCITT-FALSE: Polynom 0x1021, Startwert 0xFFFF, keine
Spiegelung, kein abschließendes XOR, über die 32 Register von BIND_CFG und
dann die 32 von BIND_OUT, jedes als Low-Byte und dann High-Byte, 128 Bytes.

Ein Schreiben von `COMMIT`:

1. wird mit BAD_VALUE abgelehnt, wenn sein Wert nicht die CRC dessen ist, was
   der Koprozessor vorbereitet hält. Nichts ändert sich. Ein Frame, der nicht
   ankam, oder ein Koprozessor, der seit dem Vorbereiten neu gestartet ist,
   endet hier;
2. beurteilt die vorbereitete CHAN_CFG-Page nach jeder Regel eines
   CHAN_CFG-Schreibens und setzt sie in Kraft, oder lehnt mit BAD_VALUE ab
   und ändert nichts;
3. beurteilt die vorbereitete OUTPUTS-Page nach jeder Regel eines
   OUTPUTS-Schreibens — scharf, die Pins des Netzteils, des Sensorbusses und
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

| Panel | Koprozessor | Eine auf OUTPUTS oder PICK A PIN geänderte Bindung |
| --- | --- | --- |
| 4.10 | 4.10 | vorbereitet und übernommen: SERVO `FRAME_HZ` = 0, 8 Frames an BIND_CFG, 8 an BIND_OUT, `COMMIT`, dann OUTPUTS und CHAN_CFG zurückgelesen. 20 Austausche |
| 4.10 | 4.9 oder älter | das Panel liest die Minor beim Link-up und sendet nichts an die drei Pages. SERVO `FRAME_HZ` = 0 (ab 4.1), 8 Frames an CHAN_CFG, 8 an OUTPUTS, jeder ein eigener bestätigter Austausch und mit der Bestätigung in Kraft, dann beide Pages zurückgelesen. 19 Austausche. Ein mittendrin verlorener Frame lässt die Einträge davor in Kraft; das Panel zeigt die Pages dann, wie sie sich lesen, und schreibt nichts darüber außer einer Bindung ohne Pin |
| 4.9 oder älter | 4.10 | das Panel schreibt die drei Pages nie. Es schreibt CHAN_CFG und OUTPUTS als 8 Frames direkt hintereinander in je einem Austausch, wie zwischen zwei älteren Builds |
| ein anderer Host | 4.10 | ein `COMMIT`, der nicht die CRC der vorbereiteten Pages nennt, wird mit BAD_VALUE abgelehnt; BIND_CFG und BIND_OUT werden Frame für Frame abgelehnt wie oben |

Jedes Schreiben dieses Panels, das breiter als ein Frame ist, geht mit einem
Frame von bis zu 4 Registern je Austausch hinaus, jeder bestätigt, bevor der
nächste gesendet wird (`link_write_acked()` in `shared/link/link_port.c`). Der
XL2515 hält 2 empfangene Frames, und der Koprozessor liest ihn aus seiner
Hauptschleife; mit einem Anfrage-Frame gleichzeitig unterwegs verzögert ein
verspäteter Durchlauf einen Frame und verliert keinen an einen vollen Puffer.
Dasselbe Schreiben als 8 Frames direkt hintereinander verliert 6 davon an
einen Durchlauf von 20 ms (`test_bind_link`). Eine Antwort kommt weiterhin
als Frames direkt hintereinander: der TWAI-Treiber des Panels puffert 16.

Die Kosten einer Änderung, und woran sie gemessen werden:

| | 0.14.0 | Dieser Build, Koprozessor mit 4.10 |
| --- | ---: | ---: |
| Austausche | 5 | 20 |
| Anfrage-Frames | 19 | 20 |
| Antwort-Frames | 33 | 34 |
| Anfrage-Frames gleichzeitig auf dem Bus, höchstens | 8 | 1 |
| CHAN_CFG und OUTPUTS auf die Ausgänge angewendet | 16-mal, einmal je Frame | je einmal, beim Commit |
| Speicheranforderungen an den Flash-Speicher | 16 | 1 |

54 Frames zu etwa 130 µs sind in beiden 7 ms Buszeit. Ein Austausch fügt den
Schleifendurchlauf des Koprozessors und das Aufwachen der Panel-Task hinzu:
[Den Link in Betrieb nehmen](Bringup-de.md) gibt für die Laufzeit eines
Austauschs 334 bis 1400 µs aus, das legt die 20 zwischen 7 und 28 ms; die
Folge selbst ist auf Hardware nicht gemessen. Die Control-Task des Panels
läuft alle 5 ms und sendet die Folge aus einem Durchlauf, dieser Durchlauf
dauert also 2 bis 6 Perioden; die Sicherheitsschleife — die 20-ms-Flanken des
Heartbeats, STOP — läuft darin, zwischen zwei Frames einer Page, sobald 5 ms
seit ihrem letzten Lauf vergangen sind, und in allen 5 ms, die auf eine
Antwort gewartet wird. Der 50-ms-Poll mit
seinem Schreiben von ARM und THROTTLE verspätet sich um dieselbe Zeit; eine
Änderung wird bei scharfem Prüfstand abgelehnt, kein scharfer Prüfstand
wartet also darauf. Die 200-ms-Stillegrenze des Koprozessors zählt ab der
letzten Anfrage, die er gehört hat, und jeder Austausch der Folge ist eine:
die Folge kann sie nicht aushungern, wie lange sie insgesamt auch dauert,
und ein einzelner Frame, den der Koprozessor 200 ms lang nicht annimmt, setzt
`FEHLER 01` wie jede Anfrage. Ein Frame, der auf der Leitung verloren geht,
beendet die Folge nach den 1000 ms seines eigenen Austauschs, der Link wird
abgebaut, und die geltende Bindung bleibt unverändert.

`shared/outputs/out_stage.c` ist die Hälfte des Koprozessors und
`shared/outputs/bind_link.c` die des Panels, unter `test_bind_link`: die
Folge durch ein Modell des 2-Frame-Puffers mit einem tauben Fenster von
20 ms, geöffnet alle 125 µs, ein verlorener Frame an jeder der 18 Stellen,
die Timeouts über den Überlauf bei 2^32 ms. Nicht auf Hardware gelaufen.

### Der Fenster-Ring

SERVO_WIN (0x31) ist neu in 4.11. 4.11 gibt außerdem Register 31 von SENSE
eine Bedeutung, `RESETS`, und macht `CAP_HOLD_MA` von SERVO_SENSE zu einem
vorzeichenbehafteten Register. Keine Page und kein Register wird verschoben.

SERVO_SENSE zeigt von jedem Kanal das letzte 50-ms-Fenster und kein anderes.
Ein Host, dessen Lesezugriffe mehr als 50 ms auseinanderliegen, verpasst dort
ein Fenster. SERVO_WIN hält die letzten 4 vollständigen Fenster von INA3221
CH1, dem Servo im Test, 200 ms. Ein Host, der jede Fensternummer einmal
nimmt, verpasst keines, solange zwei seiner Lesezugriffe höchstens 200 ms
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
| 9 + 6 k | `E_FLAGS` | Bit 15 mit dieser Nummer hat sich ein Fenster geschlossen; Bit 8 es enthält Strommessungen; Bit 9 es enthält Spannungsmessungen; Bit 10 eine Strommessung lag am oberen Ende des Bereichs, 163,8 mV über dem Shunt; Bit 11 eine am unteren; Bits 0 bis 7 die Zahl der Strommessungen an einem Ende, bei 255 gehalten |

Die Page hat 28 Register. Register 0 bis 15 sind der Kopf und die zwei
neuesten Fenster: eine Anfrage und vier Daten-Frames, wie ein Lesen von
BENCH. Ströme sind auf das mA gerundet und auf -32767 bis 32767 begrenzt,
Spannungen auf 0 bis 65535 mV. Der Koprozessor liest den Strom von CH1 jede
1 ms und seine Busspannung alle 20 ms: ein Fenster enthält bis zu 50
Strommessungen und 2 oder 3 Spannungsmessungen.

Ein Eintrag mit gelöschtem Bit 15 liest in allen sechs Registern 0. Seine
Nummer hat sich nie geschlossen: der 1-ms-Takt des Koprozessors lief 50 ms
oder mehr zu spät und keine Messung fiel in dieses Fenster, oder die Nummer
liegt vor dem ersten Fenster der Konfiguration. Ein Eintrag mit gesetztem
Bit 15 und gelöschten Bits 8 und 9 hat sich ohne Messung geschlossen: der
INA3221 war nicht online.

Eine Strommessung am Ende des Bereichs zählt in einem Eintrag als der
Messwert, der sie ist, das Ende des Bereichs: 4095 Schritte von 40 µV über
dem Shunt oben und 4096 unten, 1,638 A und -1,6384 A am 0,1-Ω-Shunt. Sie geht
mit diesem Wert in den Mittelwert, den höchsten und den niedrigsten Wert
ein, und die Bits 0 bis 7 zählen sie. Der Strom war mindestens so groß; um
wie viel größer, ist nicht bekannt. Die Register 0 bis 11 von SERVO_SENSE
lassen eine solche Messung aus ihren Werten heraus, die beiden Pages
unterscheiden sich also für ein Fenster mit einer Messung am Ende des
Bereichs, und nur für ein solches. Der Koprozessor wendet auf die Zahl keine
Regel an.

`CAP_STATE`, `CAP_SEQ` und Bit 7 von `FLAGS` sind die von SERVO_SENSE, wie
jene Page im selben Durchlauf liest: ein Scharfschalten liest hier ab der
Bestätigung des Schreibens scharf (1), und eine beendete Messung zeigt sich
in dem Lesen, das die Fenster holt.

Nichts wird gehalten: nach einem Neustart des Koprozessors liest alles 0,
ebenso nach einer angenommenen SENSE-Konfiguration, bis sich unter ihr das
erste Fenster schließt.

**Ein Bauteil, das sich selbst zurücksetzt.** Beide Strommonitore beantworten
nach einem eigenen Reset jedes Lesen, mit ihrer Einschalt-Konfiguration. Der
INA228 rechnet seinen Strom dann mit ADCRANGE 0: eine Konfiguration mit
ADCRANGE 1, jede, deren Shuntspannung beim Maximalstrom höchstens 40,96 mV
beträgt, darunter die MATEK-Voreinstellung, liest ein Viertel des Stroms.
Der INA3221 wandelt alle drei Kanäle mit 1,1 ms, ein CH1-Ergebnis alle
6,6 ms, wo die Konfiguration des Prüfstands eines alle 280 µs (CH1 allein)
bis 840 µs (drei Kanäle) gibt. Der INA228 setzt sich zurück, wenn seine
Versorgung unter etwa 1,26 V fällt (TI SLYS021A §7.4.2).

Der Koprozessor liest ADC_CONFIG des INA228 und Configuration des INA3221
zurück, je einmal in 40 ms. Ein anderer Wert als der geschriebene wird im
selben Takt ein zweites Mal gelesen, ein einzelnes verfälschtes Lesen ist
also kein Reset. Weichen beide Lesungen ab:

- das Bauteil verliert ONLINE in `FLAGS` von SENSE und wird 1000 ms später
  erneut geprüft und konfiguriert;
- sein Byte von `RESETS` (SENSE-Register 31) zählt eins: der INA228 im
  Low-Byte, der INA3221 im High-Byte, je modulo 256, seit die geltende
  Konfiguration angenommen wurde. Der Zähler zeigt einen Reset, der zwischen
  zwei Lesungen von `FLAGS` gefunden und behoben wurde;
- die Fenster, die gerade aus dem Bauteil gefüllt werden, werden geleert.
  Beim INA3221 liest der SERVO_WIN-Eintrag dieses Fensters geschlossen und
  ohne Messung, ebenso die Einträge der nächsten 1000 ms;
- beim INA228 lesen Ladung und Energie 0, und BENCH-Bit 6 bleibt bis zum
  nächsten Scharfschalten gelöscht; Spannung, Strom und Leistung von BENCH
  lesen leer, solange das Bauteil offline ist;
- eine laufende Messung der Bewegung endet verloren (Zustand 8) und zählt in
  `CAP_SEQ`.

Ein Reset wird innerhalb von 40 ms gefunden. Die Messungen zwischen dem
Reset und seiner Entdeckung liegen im Fenster vor dem geleerten, wenn eine
Fenstergrenze dazwischen fällt: die des INA228 bei einem Viertel für eine
Konfiguration mit ADCRANGE 1, die des INA3221 im Wert richtig und je bis zu
6,6 ms alt.

Die beiden Rücklesungen nehmen in jedem zweiten Durchgang der Rotation des
Zeitplans die Plätze der Chip-Temperatur des INA228 und von Mask/Enable des
INA3221: diese beiden werden mit 25 Hz gelesen, jede andere Rate ist
unverändert. Eine Rücklesung ist ein Lesen von 2 Byte, 120 µs. Ein gerader
Takt bleibt bei 690 µs Buszeit; der Takt, in dem ein Konfigurationsregister
ein zweites Mal gelesen wird, hat 742,5 µs.

| Panel | Koprozessor | SERVO_WIN (0x31), `RESETS` und `CAP_HOLD_MA` |
| --- | --- | --- |
| 4.11 | 4.11 | bedient. Das Panel dieses Builds sendet nichts an SERVO_WIN, liest Register 31 nur mit den Registern des Encoders und nutzt es nicht, und schaltet keine Messung scharf |
| 4.11 | 4.10 oder älter | der Koprozessor beantwortet eine Anfrage an 0x31 mit BAD_PAGE; ein Host liest die Minor beim Link-up und sendet unter 11 nichts dorthin. Register 31 liest 0, und ein Bauteil, das sich zurückgesetzt hat, bleibt ONLINE mit seiner Einschalt-Konfiguration. Ein `CAP_HOLD_MA` von 32768 bis 65535 wird mit BAD_VALUE abgewiesen |
| 4.10 oder älter | 4.11 | das Panel liest 0x31 nie und liest Register 31 als reserviertes Register, das es übergeht. Ein Haltestrom, den es schreibt, 0 bis 32767 mA, bedeutet dasselbe. Ein zurückgesetzt gefundenes Bauteil liest 1000 ms lang offline und dann wieder online |

`shared/sense/sense_sched.c` hält den Ring und führt die Rücklesungen aus,
unter `test_sense_sched`: der Ring nach 1, 4, 5 und 6 Fenstern, über Fenster
65535 hinweg, mit einem um 49, 50 und 51 ms und um 5 Fenster verspäteten
Takt, mit 0, 1, 44, 45 und 50 Messungen am Ende des Bereichs; ein Reset,
gefunden an jedem der 40 Takte einer Rotation, das Bauteil zurück bei
1000 ms und nicht bei 999 ms, über den Überlauf bei 2^32 ms; die Buszeit
jedes Takts. `shared/outputs/sense_page.c` hält die Page, unter
`test_sense_page` und `test_link_pages`. Nicht auf Hardware gelaufen: die
Page, und ob eines der Module sich bei einem Einbruch seiner Versorgung
zurücksetzt.

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

Ein Write gilt ganz oder gar nicht. Jedes Register eines Frames wird geprüft,
bevor eines davon gespeichert wird; ein NACK lässt die Page also genau so, wie
sie war: keines der bis zu vier Register eines Frames wird übernommen, und
keine Nebenwirkung eines Registers läuft. Das Aufheben eines eingerasteten
Failsafe ist eine solche Nebenwirkung.

### Page Map

| Page | Name | Zugriff | Register |
| ---: | --- | --- | --- |
| 0x00 | IDENTITY | lesen | Protokoll major, Protokoll minor, Firmware major, minor, patch, Hardware-Revision, Capabilities-Bitmap |
| 0x01 | STATUS | lesen | Zustand (0 idle, 1 armed, 2 failsafe), Fault-Bitmap, Uptime in ms (zwei Register), angenommene Anfragen (zwei Register), Empfangsfehlerzähler des XL2515, Sendefehlerzähler des XL2515 |
| 0x10 | CONTROL | lesen, schreiben | ARM (ungleich null schärft), THROTTLE (0..10000, Hundertstel Prozent, und kommandiert jeden Kanal, den CHAN_CFG als Throttle führt), MOTOR_POLES, CLEAR (0x5AFE schreiben, um das Failsafe zu verlassen). Register 0 bis 2 sind der Frame, der scharfschaltet |
| 0x11 | LIMITS | | deklariert, nicht bedient |
| 0x12 | FAILSAFE | | deklariert, nicht bedient |
| 0x13 | CHANNELS | lesen, schreiben | ein Kommando je Ausgangskanal, 0..1000 des Kanalwegs; acht Kanäle |
| 0x20 | BENCH | lesen | Spannung (10 mV), Strom (10 mA), Leistung (W), Drehzahl (min⁻¹), Temperatur des ESC (Electronic Speed Controller, Motorregler) und des Motors (0,1 °C, vorzeichenbehaftet), Ladung (mAh), Energie (0,1 Wh), Minimalspannung, Maximalstrom, Maximalleistung, Maximaldrehzahl, Flags |
| 0x21 | reserviert | | nicht vergeben; nicht wiederzuverwenden |
| 0x22 | OUTPUTS | lesen, schreiben | je Slot: Treiber (0 keiner, 1 PWM (Pulsweitenmodulation), 2 PPM (Pulspositionsmodulation), 3 DShot, 4 bidirektionales DShot), Pin, erster Kanal und Kanalzahl in einem Register, Rate in Hz (kbit/s für beide DShot-Treiber); acht Slots zu vier Registern |
| 0x23 | CHAN_CFG | lesen, schreiben | je Kanal: Rolle (0 throttle, 1 surface), Slew (Spanne je Sekunde, 0 = sofort), minimaler und maximaler Puls in µs; acht Kanäle zu vier Registern |
| 0x24 | CATALOGUE | lesen | die eigenen Pins der Platine, je ein Register: GPIO-Nummer (General-Purpose Input/Output) in 6 Bit, die daneben aufgedruckte Pad-Nummer in 6, was ihn hält in 4 (0 frei, 1 Heartbeat, 2 CAN, 3 Flash, 4 Debug, 5 Sensor, 15 sonstiges); 32 Slots, und eine Pad-Nummer von 0 heißt, in diesem Slot ist kein Pin |
| 0x25 | SHAPE | lesen | wo diese Pads liegen: Umriss-Breite und -Höhe in 0,01 mm, die Ecke, an der Pad 1 sitzt, und die Pads in einer Reihe gepackt als (Ecke << 8) \| je Reihe, das Raster in 0,01 mm und der Abstand von der Kante zur Mitte einer Pad-Reihe. Zwei Reihen auf einem Raster, nummeriert von Pad 1 weg entlang seiner Kante und zurück entlang der gegenüberliegenden. Alles null, wenn der Koprozessor keine Form für seine Platine hat — dann wird sie gelistet und nicht gezeichnet |
| 0x26 | ARTWORK | lesen | was ein Bild der Platine ist: Blöcke Nutzdaten (0, wenn der Koprozessor keines trägt), Breite und Höhe in Pixeln, Format (0 keines, 1 RGB565 mit dem niederwertigen Byte zuerst), Nutzdatenlänge in zwei Registern und eine CRC (zyklische Redundanzprüfung) über die gesamten Nutzdaten mit Startwert null |
| 0x27 | ART_DATA | lesen, schreiben | das Bild selbst: Register 0 schreiben, um den Block zu nennen, dann die Page lesen. Register 0 liest den gerade bedienten Block zurück, Register 1 bis 31 tragen 62 Bytes davon. Ein Block rückt beim Lesen nicht vor, eine verlorene Antwort wird also erneut angefordert statt übersprungen |
| 0x28 | PADS | lesen | die Pads, die keine Pins sind, je ein Register: die Pad-Nummer in 6 Bit, was es ist in 2 (0 kein Pad und die Liste endet, 1 Masse, 2 eine Versorgung, 3 keines von beiden) und die Spannung in 8 Bit zu Zehntelvolt. Null Volt bei einer Versorgung heißt, sie ist keine feste Spannung — das ist nicht dasselbe wie die 0 V einer Masse. 32 Slots, in Pad-Reihenfolge |
| 0x29 | SERVO | lesen, schreiben | Register 0: die Frame Rate in Hz jedes PWM-Ausgangs, dessen erster Kanal die Rolle surface hat, 40 bis 560, oder 0 für die eigene Rate jedes Slots aus OUTPUTS; mit BAD_VALUE abgewiesen, und nichts ändert sich, wenn ein PWM-Slice damit zwei Raten hätte, und solange sie nicht 0 ist, ebenso ein Schreiben auf CHAN_CFG oder OUTPUTS, das das täte (seit 4.1). Register 1 bis 4, ein Frame: ein Sweep der Surfaces -- Kurve (0 gestoppt, 1 Rechteck, 2 Sinus, 3 Dreieck), Geschwindigkeit in Tausendstel Zyklen je Sekunde (50 bis 5000), Amplitude in Befehlseinheiten zu jeder Seite der Mitte (0 bis 500), Haltezeit an jedem Ende in ms (0 bis 5000); ein Sweep startet oder ändert sich nur, wenn alle vier zusammen geschrieben werden, sonst mit BAD_VALUE abgewiesen, und bei nicht scharfem Prüfstand mit NOT_ARMED; 0 in Register 1 allein hält ihn an, und 4 allein hält jede Surface dort, wo ihr Ausgang gerade ist, unter denselben Regeln für Disarm und 500 ms. Ein Sweep, der beim Beginn des Haltens läuft, behält seine Phase (seit 4.6): wie weit er in der Kurve war, und damit seinen Punkt, sein Verweilen und die erreichten Enden; einer, der seine Bewegungen gemacht hat, wird in der Mitte gehalten, auf der er endete. Ein Schreiben wird an der Page gemessen, wie der nächste Durchlauf des Koprozessors sie in diesem Moment hinterließe: ein Sweep nach seiner letzten Bewegung, 500 ms ohne Schreiben oder entschärft ist beendet, auch wenn das Schreiben vor dem Durchlauf bedient wird, der ihn beendet. 5 allein (RESUME, seit 4.6) setzt diesen Sweep ab der behaltenen Phase fort; die Surfaces werden sofort wieder entlang der Kurve angesteuert und fahren mit ihrer eigenen Rate von dort hin, wo sie gehalten wurden. RESUME wird mit BAD_VALUE abgewiesen, wenn keine Phase behalten ist -- beim Beginn des Haltens lief kein Sweep, oder das Halten endete durch ein Schreiben von 0, eine darübergeschriebene Kurve, ein Disarm, 500 ms ohne Schreiben oder einen Neustart --, und wenn Register 2 bis 5 nicht mehr lesen, was der angehaltene Sweep fährt; mit NOT_ARMED bei entschärftem Prüfstand. Ein Koprozessor mit 4.5 weist 5 mit BAD_VALUE ab. Register 5: wie viele Enden ein danach gestarteter Sweep erreicht, bevor er anhält, 0 für kein Ende. Register 6, nur lesen: die Enden, die der Sweep erreicht hat. Ein Sweep hält bei einem Schreiben von 0, bei einem Disarm und wenn er 500 ms nicht geschrieben wurde an und lässt jede Surface dort, wo ihr Ausgang gerade ist; Wiederholen hält seine Kurve am Laufen, und ein Sweep, der seine Bewegungen gemacht hat, startet durch Wiederholen nicht neu (seit 4.2). Nichts wird gespeichert: Nach einem Neustart des Koprozessors steht überall 0 |
| 0x2A | SUPPLY | lesen, schreiben | ein WeAct PD Power Mini V1 Buck an einem PIO-UART (PIO: Programmable Input/Output) auf zwei Pins des Koprozessors (seit 4.3). Register 0 bis 3, ein Frame: Freigabe, der GPIO, der sendet (zum DM des Moduls), und der, der empfängt (sein DP), und die UART-Baudrate-Einstellung des Moduls, 0 bis 6 für 9600, 19200, 38400, 57600, 115200, 230400 und 460800 Baud, oder 7 (seit 4.4), um sie zu finden: der UART versucht nach jedem WHO_AM_I ohne gültige Antwort die nächste Rate, beginnend bei 19200, bis ein Modul einmal geantwortet hat; mit BAD_VALUE abgewiesen bei einem Pin, der reserviert, an einen Ausgang gebunden oder der andere Pin ist, und bei jeder Änderung, solange der Ausgang an verlangt ist oder an sein kann (Bit 6 der Flags). Seit 4.5 wird eine Änderung, nachdem ein Modul geantwortet hat, quittiert und zurückgehalten: Register 0 bis 3 lesen die geltende Verdrahtung, und Flag-Bit 8 ist gesetzt, bis eine nach der Änderung gesendete Zustandslesung des Moduls antwortet, höchstens etwa 1,2 s; zeigt sie den Ausgang aus, wird die Änderung übernommen, zeigt sie ihn an oder schlägt sie fehl, wird sie abgewiesen, und Bit 9 ist gesetzt bis zum nächsten Schreiben von Register 0 bis 3. Eine solche Änderung kommt allein, ohne Register 4 bis 6, und ein ON wird mit BAD_VALUE abgewiesen, solange eine zurückgehalten wird. Ein Host älter als 4.5, der die Verdrahtung währenddessen noch einmal schreibt, wird quittiert, und das Zurückhalten geht weiter. Die Pins gehören keinem Ausgang, solange sie gehalten werden. Register 4 bis 6, ein Frame: Ausgang (1 an) und die Sollwerte in mV und mA, bis 20000 mV und 3000 mA; ein ON wird ohne lebenden Heartbeat mit NOT_ARMED abgewiesen, ebenso solange eine eben geschriebene Verdrahtung noch nicht im Flash ist (gespeichert wird 400 ms nach dem letzten Schreiben), und der Ausgang geht aus, wenn der Heartbeat ausbleibt. Register 7 bis 16, nur lesen: Flags (Bit 0 online, Bit 1 Ausgang an, Bits 3..2 Modus 0 normal, 1 Konstantstrom, 2 Überstrom, Bit 4 ein Ausgang, der nicht schalten wollte, Bit 5 Sollwerte, die nicht übernommen wurden, Bit 6 ein Ausgang, der an ist oder an sein kann -- an gelesen, ein ON noch nicht bestätigt oder ein OFF, das einem Modul geschuldet ist, das nicht mehr antwortet, Bit 7 ein Ausgang, den das Modul selbst abgeschaltet hat, während ON verlangt war; aus gehalten, bis OUTPUT mit 0 geschrieben wird; seit 4.5 Bit 8 eine Verdrahtungsänderung, die auf eine Zustandslesung wartet, und Bit 9 eine, die diese Lesung abgewiesen hat, Bit 10 ein Ausgang, abgeschaltet, weil der Eingang des Moduls bei 2 Lesungen hintereinander unter dem Sollwert plus 500 mV lag; aus gehalten, bis OUTPUT mit 0 geschrieben wird), mV und mA am Ausgang, die zurückgelesenen Sollwerte, Zustand und mV des Eingangs, genommene Messungen und fehlgeschlagene Transaktionen modulo 65536, und (seit 4.4) die Rate in Gebrauch, 0 bis 6, oder 7, solange AUTO keine gefunden hat. Register 17 (seit 4.4), allein als 1 geschrieben, bei verlangtem Ausgang aus und eingeschaltetem Netzteil, startet das Modul neu (SYSTEM_RESET), sobald es geantwortet hat und sein Ausgang aus gelesen wird; es liest 0. Die Verdrahtung wird im Flash des Koprozessors gehalten und beim Start mit abgeschaltetem Ausgang gefahren, damit ein Modul, das an blieb, nach einem Neustart abgeschaltet wird; der Befehl wird nicht gespeichert |
| 0x2B | SENSE | lesen, schreiben | zwei I2C-Strommonitore (I2C: Inter-Integrated Circuit) an einem Bus auf zwei Pins des Koprozessors, ein TI INA228 im Leistungspfad des ESC und ein TI INA3221 an der Servoversorgung (seit 4.7). Register 0 bis 3, ein Frame: Freigabe (Bit 0 INA228, Bit 1 INA3221, Bit 2 der Ausgangsencoder AS5600 seit 4.9), der SDA-GPIO, der SCL-GPIO und der Takt, 400 kHz und kein anderer Wert: bei 100 kHz dauert ein Lesezugriff 480 bis 750 µs, und der 1-ms-Zeitplan des Koprozessors passt nicht. Der Zeitplan liest INA3221 CH1 mit 1000 Hz, CH2 und CH3 mit 50 Hz oder für ein synchronisiertes Paar mit 1000 Hz, Strom und Spannung des INA228 mit je 500 Hz, seine Chip-Temperatur, Mask/Enable des INA3221 und das zurückgelesene Konfigurationsregister jedes Bauteils (seit 4.11) mit je 25 Hz und alles andere mit 50 Hz. Beide Module tragen Pull-ups an SDA und SCL, und die liegen parallel: der DAOKAI-INA3221 hat 10 kΩ nach VS (3,3 V); Wert und Schiene der Pull-ups des MATEK-INA228 sind unbekannt. Der Gesamtwert muss über etwa 1 kΩ bleiben: ein I2C-Ausgang zieht 3 mA bei 0,4 V, und (3,3 V − 0,4 V) / 3 mA sind 967 Ω. Unter 10 kΩ verkürzt er die Anstiegszeit: die Grenze von 300 ns bei 400 kHz erlaubt mit 10 kΩ allein 35 pF Buskapazität, mit weniger mehr. Die GPIO-Nummer von SDA ist modulo 4 gleich 0 oder 2, und SCL ist der GPIO danach, das Paar eines I2C-Blocks (standardmäßig GP16 und GP17). Register 4 bis 7, ein Frame: die Adresse des INA228, 0x40 bis 0x4F (Standard 0x45, die des MATEK I2C-INA-BM ab Werk; seine Lötbrücken geben 0x44 oder 0x41); sein Shunt in µΩ, 50 bis 20000 (Standard 200); der Strom, auf den sein Bereich eingestellt ist, in 0,1 A, 10 bis 3000 (1,0 bis 300,0 A, das Auslegungsmaximum des Prüfstands; Standard 2048). Das Maximum wählt nur ADCRANGE: 1, solange die Shuntspannung dabei höchstens 40,96 mV beträgt, 0 bis 163,84 mV, und darüber wird das Schreiben abgewiesen. CURRENT_LSB ist der Schritt des Shunt-ADC geteilt durch den Shunt (78,125 nV oder 312,5 nV durch R), damit begrenzen CURRENT und die Shuntspannung gemeinsam, und SHUNT_CAL ist in beiden Bereichen 4096. Ein Shunt, dessen Vollausschlag im gewählten Bereich 2000 A übersteigt, wird ebenfalls abgewiesen; unter 81,92 µΩ gilt also nur ADCRANGE 1. Die Regel ist die des Treibers, `ina228_calibrate()`; Register 7 reserviert. Register 8 bis 11, ein Frame: die Adresse des INA3221, 0x40 bis 0x43 (Standard 0x40); sein Shunt in 0,1 mΩ, 50 bis 10000 (5 mΩ bis 1 Ω, Standard 1000, die 0,1 Ω, die bis 1,638 A messen); die gelesenen Kanäle, Bits 0..2 für CH1 bis CH3, mindestens einer, solange er freigegeben ist (Standard CH1); Register 11 reserviert. Die reservierten Register lesen 0 und nehmen nur 0. Mit BAD_VALUE abgewiesen: ein Wert außerhalb seines Bereichs, SDA und SCL nicht das Paar eines Blocks, ein Pin, der reserviert, an einen Ausgang gebunden oder von SUPPLY gehalten ist, beide Bauteile auf einer Adresse, solange beide freigegeben sind, und jede Änderung bei scharfem Prüfstand; ein Schreiben der geltenden Konfiguration wird angenommen. Die Pins gehören keinem Ausgang, solange eines der Bauteile freigegeben ist, und ein Schreiben auf OUTPUTS, das einen davon bindet, wird abgewiesen. Register 12 bis 25, nur lesen: Flags (Bit 0 INA228 online, Bit 1 seine letzte Identitätslesung war die eines INA228, Bit 2 an seiner Adresse antwortet etwas anderes, Bit 3 ein Strom lag im letzten 50-ms-Fenster oder seit dem Schärfen des Laufs am Ende seines Bereichs, Strom und Leistung in BENCH oder ihre Spitzen sind dann Grenzen und keine Werte; Bits 4 bis 6 dieselben drei für den INA3221; Bit 8 der Bus ist auf seinen Pins offen, Bit 9 SDA wird low gehalten und wird freigetaktet), die Adressen, die beim letzten Scan geantwortet haben (Bit n für 0x40 + n), DEVICE_ID des INA228 und Die-ID des INA3221 wie gelesen, fehlgeschlagene Transaktionen modulo 65536, die Chiptemperatur des INA228 in 0,1 °C (vorzeichenbehaftet) und DIAG_ALRT, seine Ladung in 0,01 mAh (vorzeichenbehaftet, 32 Bit, Register 19 und 20, niederwertiges zuerst) und Energie in 0,01 Wh (32 Bit, Register 21 und 22, niederwertiges zuerst) seit dem Schärfen des Laufs, die Telemetriespannung (10 mV) und der Telemetriestrom (10 mA) des ESC selbst und ihre Gültigkeitsbits (Bit 0 Spannung, Bit 1 Strom). Register 26 bis 30, nur lesen, seit 4.9: Flags, RAW ANGLE, MAGNITUDE, Zähler und Ruhezeit in ms des Ausgangsencoders (siehe unten). Register 31, nur lesen, seit 4.11: `RESETS`, die zurückgesetzt gefundenen Strommonitore, der INA228 im Low-Byte und der INA3221 im High-Byte, je modulo 256 ([Der Fenster-Ring](#der-fenster-ring)); ein Koprozessor mit 4.10 oder älter liest 0. Register 0 bis 11 werden im Flash des Koprozessors gehalten |
| 0x2C | SERVO_SENSE | lesen, schreiben | die Kanäle des INA3221 und eine Bewegung, gemessen auf dem Takt des Koprozessors (seit 4.7). Register 0 bis 11, nur lesen, vier je Kanal ab CH1: mittlerer Strom (mA, vorzeichenbehaftet), höchster Strom (mA, vorzeichenbehaftet), mittlere Busspannung (mV) und niedrigste Busspannung (mV) über das letzte 50-ms-Fenster, die Spannung auf der Lastseite des Shunts. Register 12, nur lesen: die Fensternummer modulo 65536; ein Lesen beendet kein Fenster. Register 13, nur lesen: Bits 0..2 das Fenster eines Kanals enthält Messungen, Bits 4..6 eine davon lag am Ende des Bereichs (163,8 mV über dem Shunt), womit mittlerer und höchster Strom dieses Kanals Untergrenzen sind, Bit 7 dasselbe für die Messung der Bewegung. Register 14 bis 17, ein Frame: eine Messung -- Bit 7 gesetzt, der INA3221-Kanal in Bits 0..1, nur CH1 (2 und 3 werden abgewiesen; das Feld bleibt für einen Kanal, der später schnell genug gelesen wird), und in Bits 8..10 der Ausgangskanal (0 bis 7), dessen nächstes geändertes Kommando die Zeitmessung startet; der Haltestrom, bei dem die Bewegung endet, in mA, vorzeichenbehaftet, -32768 bis 32767 (seit 4.11; davor 0 bis 32767, ein größerer Wert abgewiesen); die Schwelle für Bewegung und das Band für Ankunft, je 1 bis 32767 mA. Ein Scharfschalten ist der ganze Frame und startet eine laufende Messung neu. 0 in Register 14 entschärft und wird nie abgewiesen; am Anfang des Frames geschrieben, werden die anderen drei nicht gespeichert. Mit BAD_VALUE abgewiesen: jedes andere Schreiben, das nicht der ganze Frame ist, andere Bits in Register 14, ein Wert außerhalb seines Bereichs, ein anderer Kanal als CH1 oder einer, den SENSE nicht liest, und ein Ausgangskanal, der keine Surface an einem PWM-Slot ist, den der Koprozessor gebunden hat (ein Pin, dessen Compare-Register ein anderer Pin hält, ist es nicht); mit NOT_ARMED bei entschärftem Prüfstand. Ein Prüfstand, der aufhört zu treiben, beendet eine nicht fertige Messung. Register 18 bis 24, nur lesen: der Zustand (0 idle, 1 scharf, 2 wartet auf Bewegung, 3 bewegt sich, 4 angekommen, 5 an einem Endanschlag eingeschwungen, 6 zu spät: Bewegung und keine Ankunft innerhalb von 3000 ms plus der Verzögerung des Strommessers, 7 unerkannt: keine Bewegung in dieser Zeit, 8 verloren: der INA3221 antwortet nicht mehr, oder binnen 3000 ms nach dem Scharfschalten kam keine PWM-Flanke), fertige Messungen modulo 65536, die Zeit vom PWM-Frame mit dem neuen Puls bis zur Bewegung und bis zur Ankunft in 0,1 ms, aufgelöst auf das Abtastintervall von CH1, 1 ms, höchster und mittlerer gefilterter Strom der Bewegung (mA, vorzeichenbehaftet) und die Zahl der Messungen darin. Nichts wird gespeichert: Nach einem Neustart des Koprozessors steht überall 0 |
| 0x2D | TONE | lesen, schreiben | die Beeps eines ESC, gehört an einer Motorphase über einen Vorwiderstand und eine Z-Dioden-Klemme an einem GPIO des Koprozessors, gestempelt von einer PIO-State-Machine mit 26,7 ns (seit 4.8). Register 0 bis 3, ein Frame: Freigabe (Bit 0), der GPIO (Standard 22, Pad 29), der tiefste gehörte Ton in Hz (50 bis 2000, Standard 400) und der höchste (über dem tiefsten, bis 6900, Standard 6500). Register 4 bis 7, ein Frame: die Tonhöhenänderung in Prozent, die ohne Stille einen neuen Beep beginnt (0 trennt nur an Stille, höchstens 50, Standard 8), die Stille, die einen Beep beendet, in ms (1 bis 100 und mindestens die Periode des tiefsten Tons, Standard 3), die Tonperioden, die einen Beep ausmachen (1 bis 64, Standard 3), und Register 7 reserviert. Das reservierte Register liest 0 und nimmt nur 0. Mit BAD_VALUE abgewiesen: ein Wert außerhalb seines Bereichs, eine Kombination, die der Detektor ablehnt (eine Stille, die kürzer ist als die Periode des tiefsten Tons), und, solange der Tap freigegeben ist, ein GPIO hinter der Bank (63), reserviert, an einen Ausgang gebunden, von SENSE oder SUPPLY gehalten oder ein ADC-Pin (ADC: Analog-Digital-Wandler): GP26 bis GP29 des RP2350A und GP40 bis GP47 des RP2354B, die nicht fehlertolerant sind. Der GPIO gehört keinem Ausgang, solange der Tap freigegeben ist, und ein Schreiben auf OUTPUTS, SENSE oder SUPPLY, das ihn nimmt, wird abgewiesen. Eine Änderung wird bei scharfem wie bei unscharfem Prüfstand angenommen: der Tap ist ein Eingang und treibt nichts. Register 8 bis 12, nur lesen: Flags (Bit 0 die Erfassung läuft, Bit 1 der Tap ist freigegeben und sein Pin konnte nicht genommen werden, Bit 2 der Erfassungsring oder die FIFO der State Machine ist seit Beginn der Erfassung übergelaufen, Bit 3 eine Folge von Bursts läuft, Bit 4 das letzte Fenster enthielt einen Ton), die Fensternummer modulo 65536 (Fenster zu 8 ms), der Ton des letzten Fensters in 0,1 Hz (0 für keinen) und die Tonperioden darin, und die Nummer des neuesten Beeps. Der Koprozessor hält die letzten 64 Beeps und nummeriert sie von 1 bis 65535, dann wieder ab 1; die neueste Nummer ist vor dem ersten Beep 0. Register 13, EVT_SEL, ist das einzige beschreibbare Register nach Register 7 und wird nicht gespeichert: die Nummer des Beeps, den die Register 14 bis 21 zeigen. Ein Lesen verbraucht keinen Beep, eine auf dem Link verlorene Antwort verliert also nichts. Register 14 bis 21, nur lesen: noch einmal EVT_SEL, solange dieser Beep unter den 64 ist, sonst 0, wobei Register 15 bis 21 0 lesen; der erste Anstieg des Beeps in ms seit Beginn der Erfassung (32 Bit, Register 15 und 16, niederwertiges zuerst); seine Länge bis zur letzten Flanke in 0,1 ms; seine mittlere Tonhöhe in 0,1 Hz; seine Bursts; der Träger, mit dem er zerhackt war, in 100-Hz-Schritten (0 nicht zerhackt); Flags (Bit 0 er begann bei einer Tonhöhenänderung ohne Stille davor, Bit 1 er endete bei einer). Register 22 und 23, nur lesen: verlorene Beeps und Lows unter 500 ns, die der Detektor ignoriert hat, je modulo 65536. Der 8-µs-Hold-off der Erfassung entfernt jedes Low unter 8 µs, bevor der Detektor es sieht; Register 23 liest deshalb am Tap 0. Die Erfassung beginnt, wenn der Tap freigegeben wird oder sein Pin wechselt, und leert dann die 64. Register 0 bis 6 werden im Flash des Koprozessors gehalten, und der Tap startet beim Booten. Solange der Tap gesperrt ist, ist der Pin ein Eingang mit eingeschaltetem Pull-down (32 bis 86 kΩ), und der bleibt an, solange der Tap läuft |
| 0x2E | BIND_CFG | lesen, schreiben | eine vorbereitete CHAN_CFG-Page, die nicht gilt (ab 4.10): die Register und die Wertregeln von CHAN_CFG. [Eine Bindung als Ganzes](#eine-bindung-als-ganzes) beschreibt die drei Pages |
| 0x2F | BIND_OUT | lesen, schreiben | eine vorbereitete OUTPUTS-Page, die nicht gilt (ab 4.10): die Register und die Wertregeln von OUTPUTS |
| 0x30 | BIND | lesen, schreiben | Register 0 `COMMIT` (ab 4.10): ein Schreiben der CRC-16 der 64 vorbereiteten Register setzt beide vorbereiteten Pages in Kraft oder keine; abgelehnt mit BAD_VALUE für einen anderen Wert und für eine Page, die ihre eigenen Regeln ablehnen. Liest die CRC dessen, was vorbereitet ist |
| 0x31 | SERVO_WIN | lesen | die letzten 4 vollständigen 50-ms-Fenster von INA3221 CH1, das neueste zuerst (seit 4.11). Register 0: die Nummer des neuesten Fensters modulo 65536. Register 1: Bit 0 ein Fenster hat sich geschlossen, Bit 7 das Bit der Messung der Bewegung für eine Messung am Ende des Bereichs. Register 2 und 3: Zustand und Zähler der Messung von SERVO_SENSE. Dann 4 Einträge zu 6 Registern, Eintrag k ab Register 4 + 6 k für Fensternummer Register 0 - k: mittlerer, höchster und niedrigster Strom (mA, vorzeichenbehaftet), mittlere und niedrigste Busspannung (mV) und Flags (Bit 15 mit dieser Nummer hat sich ein Fenster geschlossen, Bit 8 Strommessungen, Bit 9 Spannungsmessungen, Bit 10 eine Messung am oberen Ende des Bereichs, Bit 11 eine am unteren, Bits 0 bis 7 die Messungen an einem Ende, bei 255 gehalten). Eine Messung am Ende des Bereichs zählt mit dem Ende des Bereichs. Jedes Schreiben wird mit READ_ONLY abgewiesen. [Der Fenster-Ring](#der-fenster-ring) nennt die Regeln. Nichts wird gehalten |

Fault-Bitmap: Bit 0 Link still, Bit 1 Überstrom, Bit 2 Übertemperatur, Bit 3
Stall, Bit 4 Heartbeat ausgeblieben, Bit 5 Protokollversion abweichend,
Bit 6 der Flash-Speicher des Koprozessors ist für diesen Start aus (seit
4.7): sein zweiter Core hat sich nicht für die Flash-Sperre angemeldet,
also wird nichts gespeichert, und über den Link geschriebene
Konfigurationen laufen bis zu einem Neustart aus dem RAM.
Faults bleiben gesetzt, bis sie gelesen und gelöscht werden; Bit 6 bleibt
bis zu einem Neustart gesetzt. Bit 0 setzt
voraus, dass über den Link mindestens eine Anfrage gelaufen ist: der
Koprozessor ist wach, bevor das Panel pollt, und das Warten auf die erste
Anfrage ist keine Stille.

Capabilities-Bitmap: Bit 0 ESC-Ansteuerung, Bit 1 ESC-Telemetrie, Bit 2
Servo-PWM, Bit 3 Servo-Strommessung, Bit 4 Akkumessung, Bit 5 Empfängerbus,
Bit 6 Vibrationssensor und Indeximpuls, Bit 7 Zellenmonitor, Bit 8
Programmierung. Das Panel leitet daraus die Marken im Menü ab. Bits 3 und 4
sagen, was die SENSE-Konfiguration freigibt -- Bit 3 den INA3221, Bit 4 den
INA228 --, also bestückt laut Konfiguration, nicht gerade antwortend (seit
4.7). Sie ändern sich nur, wenn ein Schreiben auf SENSE angenommen wird,
und ein Panel liest sie nach einem Schreiben auf SENSE neu. Ob ein Bauteil
antwortet, sagen die FLAGS von SENSE und BENCH-Bit 5, die das Panel pollt. Die
Page TONE fügt kein Bit hinzu.

BENCH-Flags: Bit 0 Spannung gültig, Bit 1 Strom gültig, Bit 2 Drehzahl
gültig, Bit 3 Temperatur des ESC gültig, Bit 4 Temperatur des Motors gültig,
Bit 5 Spannung, Strom und Leistung sind die des INA228 und nicht die
Telemetrie des ESC (seit 4.7), Bit 6 Ladung und Energie sind die Zähler des
INA228, beim Schärfen dieses Laufs gelöscht, und das Bauteil hat
durchgehend geantwortet (seit 4.7), Bit 7 simuliert. Ohne Bit 6 zählt ein
Panel Ladung und Energie selbst aus dem Strom; mit Bit 6 zeigt das Panel
dieses Builds die des INA228, in den Schritten von 0,01 mAh und 0,01 Wh der
SENSE-Page aus einem Lesen jünger als 100 ms, ab 100 ms nach Laufbeginn. Die beiden Temperaturen haben getrennte Bits, weil sie aus
verschiedenen Quellen kommen und eine davon meist gar nicht kommt: ein ESC
meldet seine eigene Temperatur über die erweiterte DShot-Telemetrie und weiß
nichts über den Motor, den er treibt. Bis Protokoll 3.0 galt Bit 3 für beide —
deshalb ist diese Änderung ein Major. Ein Koprozessor ohne
Mess-Frontend setzt Bit 7, und das Panel zeichnet SIMULATION über den
Bildschirm.

MOTOR_POLES ist die Magnetzahl des geprüften Motors, gerade und zwischen 2 und
42, oder null: dann hat es niemand gesagt. Ein bidirektionaler DShot-ESC meldet
elektrische Perioden und weiß nicht, woran er angeschraubt ist; das ist also die
eine Zahl, die die Leitung tragen muss, damit der Coprozessor eine mechanische
Drehzahl melden kann. Bei null meldet er keine Drehzahl statt einer aus einer
Schätzung abgeleiteten. Das Panel sendet sie aus der Einstellung `Motorpole`,
sobald ein Coprozessor zu antworten beginnt, erneut bei jeder Änderung der
Einstellung und im Frame, der scharfschaltet, ob eine Änderung offen ist oder
nicht. Ein Schreibvorgang,
den niemand beantwortet, bleibt offen und geht beim nächsten 50-ms-Poll
erneut hinaus; einer, den der Coprozessor ablehnt, wird nicht wiederholt,
denn dieselbe einmal abgelehnte Anfrage wird jedes Mal abgelehnt, und er
wartet stattdessen auf die nächste Änderung oder die nächste
Link-up-Flanke. Der Schutz bei null greift für eine nie gesendete Zahl, nicht
für eine veraltete: jede Zahl, die die Einstellung zulässt, liegt im Bereich,
den die Page annimmt.

Der Coprozessor verweigert einen Pin, den er nicht treiben darf — die
Safety-Leitung, die Pins des CAN-Controllers, GP23, GP24, GP25 und GP29 (vom
Modul belegt, nicht herausgeführt) und jede Nummer über GP29 — auf der
OUTPUTS- und der SUPPLY-Page gleichermaßen. Ein OUTPUTS-Write mit einem Slot, den die Hardware nicht
binden kann — ein solcher Pin, oder eine PIO-Zustandsmaschine, Instruktionsspeicher oder ein
DMA-Kanal, den der Phase-Tap oder die UART der Versorgung belegt —, wird mit BAD_VALUE verweigert;
die geltenden Slots bleiben, und nichts wird gespeichert. [DShot und die
Output-Treiber](DShot-de.md) hat den Rest.

Einträge in CHAN_CFG und OUTPUTS werden ganz geschrieben, vier Register auf
einmal. Der Pulsbereich eines Kanals ist standardmäßig 1000..2000 µs;
Endpunkte außerhalb von 400..2500 µs werden mit BAD_VALUE abgewiesen. Ein
Kommando außerhalb seines Bereichs wird begrenzt. Zwei Slots auf einem Pin
oder zwei Slots, die denselben Kanal ausgeben, werden abgewiesen. Über das
Schärfen entscheidet der Koprozessor: ein Schreiben von ARM wird mit
NOT_ARMED abgewiesen, solange der Link im Failsafe ist, dem Heartbeat nicht
vertraut wird oder der Arm-Latch gesetzt ist.

Solange der Prüfstand scharf ist -- die Bank treibt, oder das Register ARM
ist gesetzt -- wird ein Schreiben auf OUTPUTS, das irgendein Register ändert,
mit BAD_VALUE abgewiesen, ebenso ein Schreiben auf CHAN_CFG, das die Rolle
eines Kanals ändert oder Rolle, Slew oder Endpunkte eines Kanals mit der
Rolle Throttle. Slew und Endpunkte einer Surface werden scharf angenommen,
ebenso ein Schreiben der geltenden Page; ein scharfes Schreiben der
geltenden OUTPUTS-Page bindet nichts neu. Die Regel steht in
`outputs_chan_cfg_armed_check()` und `outputs_slots_armed_check()`, unter
`test_link_pages`.

Der Arm-Latch und die Ablehnung im scharfen Zustand ändern kein Register und
keinen Frame und tragen deshalb keine eigene Protokollversion. Was eine
Gegenstelle sieht, die ohne sie gebaut wurde:

| Panel | Koprozessor | Verhalten |
| --- | --- | --- |
| älter | dieser Stand | der Link kommt hoch und der Prüfstand schärft: dieses Panel schreibt vor jedem Schärfen CLEAR. Sein erstes Schärfen nach einem STOP kann einmal mit NOT_ARMED abgewiesen werden, weil es feste 100 ms wartet; die Ablehnung lässt den Latch gesetzt, und das zweite Halten schärft. Ein ARM = 1, das es nach einem Neustart des Koprozessors schreibt, wird abgewiesen, und bei ihm rastet ein Stopp ein |
| dieser Stand | älter | der Link kommt hoch und der Prüfstand schärft. Das Panel stoppt an der Flanke, an der der Link ausfällt; ein Neustart, den es bemerkt, schärft also nichts. Ein Neustart, den es nicht bemerkt -- die Anfrage wird wiederholt, bis der Koprozessor wieder antwortet --, wird vom nächsten Poll wieder scharf geschaltet, wie zwischen zwei älteren Ständen. Ein Heartbeat, dem kürzer als ein Poll-Abstand nicht vertraut wurde, setzt dort keinen Latch |
| ein anderer Host | dieser Stand | ein ARM ohne CLEAR seit dem Start des Koprozessors wird mit NOT_ARMED abgewiesen; ein Schreiben auf CHAN_CFG oder OUTPUTS wie oben wird mit BAD_VALUE abgewiesen, solange ARM gesetzt ist |

Ein Schärfen vom Panel sind zwei Transaktionen. CLEAR geht zuerst und allein:
der Koprozessor prüft ARM gegen sein Failsafe, bevor er ein CLEAR aus
demselben Frame anwendet, also wird ein Frame mit beidem genau dann mit
NOT_ARMED abgewiesen, wenn das Clear nötig war. Danach gehen ARM, THROTTLE
und MOTOR_POLES als ein Frame mit drei Registern ab Offset 0, sodass der
Koprozessor den Lauf mit der Polzahl beginnt, die das Panel gesendet hat,
oder ihn nicht beginnt. Jeder Poll alle 50 ms danach schreibt ARM und
THROTTLE; eine während eines Laufs geänderte Polzahl geht beim nächsten Poll
in einem eigenen Write. Die Regeln der Page -- der Throttle-Bereich, die
Polzahl, die CLEAR-Magic, ARM im Failsafe abgewiesen, und dass eine
Ablehnung nichts speichert -- stehen in `shared/link/link_control.c`, unter
`test_link_pages`. Ob der Prüfstand schärfen darf und was den Latch setzt
und löst, steht in `shared/safety/safety_gate.c`, unter `test_safety_gate`.

Vor dem CLEAR wartet das Panel, bis der Koprozessor dem Heartbeat vertraut:
100 ms nach dem vollendeten Halten liest es einmal je Durchlauf von 5 ms das
Register 1 von STATUS, bis Bit 4 gelöscht ist, höchstens 200 ms länger. Der
Koprozessor füllt die Register 0 und 1 von STATUS beim Lesen.

### Bit Timing

`shared/can/can_timing.c` berechnet die Segmentierung für den Takt jedes
Controllers und verlangt, dass die Bitrate exakt aufgeht. Beide Enden tasten
bei 75 % des Bits ab:

| Controller | Takt | Prescaler | Quanta je Bit | tseg1 | tseg2 | sjw |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TWAI (ESP32-S3) | 80 MHz APB (Advanced Peripheral Bus) | 4 | 20 | 14 | 5 | 4 |
| XL2515 | 16-MHz-Quarz, intern halbiert | 1 | 8 | 5 | 2 | 2 |

Acht Quanta sind das Minimum für ein Bit. Deshalb braucht der XL2515 für
1 Mbit/s einen 16-MHz-Quarz, und deshalb liegt sein Sample Point fest bei
75 %; das TWAI-Timing ist passend dazu gewählt. `test_can_timing` hält beides
fest.

### Budget

Ein Poll der Bench-Page ist ein Anfrage-Frame und vier Daten-Frames (13
Register). Bei 20 Hz sind das 1,55 % des Busses. Solange ein Strommonitor
eingeschaltet ist, liest das Panel die Register 12 bis 25 von SENSE (14
Register, ein Anfrage-Frame und vier Daten-Frames) mit 20 Hz, etwa 1,6 %, mit
freigegebenem Ausgangsencoder die Register 12 bis 31 (20 Register, ein
Anfrage-Frame und fünf Daten-Frames), etwa 1,9 %, und solange der INA3221 eingeschaltet ist, die Register 0 bis 13 von SERVO_SENSE
mit 20 Hz, jedes 50-ms-Fenster, etwa 1,6 %: zusammen etwa 3,2 %. Solange der
Tap freigegeben ist, sind das Lesen der Register 8 bis 23 von TONE (16
Register, ein Anfrage-Frame und vier Daten-Frames wie beim BENCH-Lesen) mit
20 Hz etwa 1,7 % mehr, unter 2 %, für die drei Pages zusammen 4,9 %, und das Lesen von
EVT_SEL und der Register 14 bis 21 je neuem Beep ein Schreib-Frame, eine
Anfrage und zwei Daten-Frames. Jedes Lesen ist ein Austausch
mehr im 50-ms-Poll. Die Nutzlast von Classic CAN
bei 1 Mbit/s mit 29-Bit-Identifiern und vollem Bit Stuffing liegt im
ungünstigsten Fall bei etwa 52 kB/s; der erwartete Verkehr liegt bei 12 bis
30 kB/s.

### Tests

Das Identifier-Layout wird Bit für Bit über den gesamten 29-Bit-Raum
geprüft; der Timing-Rechner ist an von Hand nachgerechnete Beispiele
gebunden; `test_link_loopback` lässt den Host-Poller gegen den
Device-Dispatcher laufen, über einen Bus, der Frames verwirft, verzögert und
umsortiert: geteilte Antworten in umgekehrter Reihenfolge, abgewiesene
Schreibzugriffe, verlorene Teilstücke, die eine Anfrage unbeantwortet lassen
statt halb beantwortet, und der Watchdog des Geräts, der auf einem stillen
Bus feuert. `test_bind_link` lässt die Schreib- und Lesefolge einer Bindung
gegen die Page-Regeln des Koprozessors laufen, über einen Bus mit dem
2-Frame-Puffer des XL2515: siehe [Eine Bindung als
Ganzes](#eine-bindung-als-ganzes).
