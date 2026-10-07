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
Protokollversion 4.7. Die Major-Version ist Register 0 der Page 0. Die Major
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
Sweeps ab 4.6, SENSE und SERVO_SENSE ab 4.7. Der Koprozessor liest die Minor
des Panels nie; eine Page, die ein älteres Panel nicht kennt, schreibt es
nie.

| Panel | Koprozessor | Link und Schärfen | SENSE (0x2B) und SERVO_SENSE (0x2C) |
| --- | --- | --- | --- |
| 4.7 | 4.7 | hoch, wird scharf | bedient |
| 4.7 | 4.6 | hoch, wird scharf | das Panel sendet an keine der beiden Pages; der Koprozessor beantwortet beide mit BAD_PAGE. BENCH-Bits 5 und 6 lesen 0 |
| 4.6 | 4.7 | hoch, wird scharf | das Panel liest und schreibt keine der beiden Pages und übergeht BENCH-Bits 5 und 6. Eine Konfiguration, die ein 4.7-Panel im Flash des Koprozessors hinterlassen hat, bleibt in Kraft, und ihre zwei Pins bleiben den Ausgängen entzogen. Sobald der Koprozessor einen INA228 liest, trägt BENCH dessen Spannung und Strom in denselben Skalen, und das Panel zählt Ladung und Energie selbst aus diesem Strom |

Der Koprozessor dieses Builds bedient beide Pages, hält die Konfiguration im
Flash und hält ihre Pins. Er liest keines der beiden Bauteile: die FLAGS von
SENSE lesen 0 (kein Bus offen), jeder Messwert auf beiden Pages liest 0, und
BENCH-Bits 5 und 6 bleiben gelöscht. Das Panel dieses Builds schreibt keine
der beiden Pages.

### Identifier

Ein 29-Bit-Extended-Identifier trägt die ganze Adresse; ein Read ist deshalb
ein Frame ohne Nutzdaten.

| Bits | Feld | Breite | Werte |
| --- | --- | ---: | --- |
| 28..26 | Priorität | 3 | 0 control (die Pages CONTROL, LIMITS, FAILSAFE und SUPPLY, ihre Quittungen eingeschlossen), 1 normal (darunter SENSE und SERVO_SENSE), 2 bulk (reserviert); niedriger gewinnt die Arbitrierung |
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
| 0x2B | SENSE | lesen, schreiben | zwei I2C-Strommonitore (I2C: Inter-Integrated Circuit) an einem Bus auf zwei Pins des Koprozessors, ein TI INA228 im Leistungspfad des ESC und ein TI INA3221 an der Servoversorgung (seit 4.7). Register 0 bis 3, ein Frame: Freigabe (Bit 0 INA228, Bit 1 INA3221), der SDA-GPIO, der SCL-GPIO und der Takt, 100 oder 400 kHz. Die GPIO-Nummer von SDA ist modulo 4 gleich 0 oder 2, und SCL ist der GPIO danach, das Paar eines I2C-Blocks (standardmäßig GP16 und GP17). Register 4 bis 7, ein Frame: die Adresse des INA228, 0x40 bis 0x4F (Standard 0x45, die des MATEK I2C-INA-BM ab Werk; seine Lötbrücken geben 0x44 oder 0x41); sein Shunt in µΩ, 50 bis 20000 (Standard 200); der Strom, auf den sein Bereich eingestellt ist, in 0,1 A, 10 bis 6553 (1,0 bis 655,3 A, Standard 2048); daraus ist CURRENT_LSB dieser Strom / 2^19 und SHUNT_CAL Strom in 0,1 A × Shunt in µΩ / 400, × 4 bei ADCRANGE 1, das gilt, solange die Shuntspannung bei diesem Strom höchstens 40,96 mV beträgt; über 163,84 mV wird das Schreiben abgewiesen; Register 7 reserviert. Register 8 bis 11, ein Frame: die Adresse des INA3221, 0x40 bis 0x43 (Standard 0x40); sein Shunt in 0,1 mΩ, 50 bis 10000 (5 mΩ bis 1 Ω, Standard 1000, die 0,1 Ω, die bis 1,638 A messen); die gelesenen Kanäle, Bits 0..2 für CH1 bis CH3, mindestens einer, solange er freigegeben ist (Standard CH1); Register 11 reserviert. Die reservierten Register lesen 0 und nehmen nur 0. Mit BAD_VALUE abgewiesen: ein Wert außerhalb seines Bereichs, SDA und SCL nicht das Paar eines Blocks, ein Pin, der reserviert, an einen Ausgang gebunden oder von SUPPLY gehalten ist, beide Bauteile auf einer Adresse, solange beide freigegeben sind, und jede Änderung bei scharfem Prüfstand; ein Schreiben der geltenden Konfiguration wird angenommen. Die Pins gehören keinem Ausgang, solange eines der Bauteile freigegeben ist, und ein Schreiben auf OUTPUTS, das einen davon bindet, wird abgewiesen. Register 12 bis 25, nur lesen: Flags (Bit 0 INA228 online, Bit 1 seine letzte Identitätslesung war die eines INA228, Bit 2 an seiner Adresse antwortet etwas anderes, Bit 3 sein letzter Strom lag am Ende seines Bereichs, Strom und Leistung in BENCH sind dann Untergrenzen; Bits 4 bis 6 dieselben drei für den INA3221; Bit 8 der Bus ist auf seinen Pins offen, Bit 9 SDA wird low gehalten und wird freigetaktet), die Adressen, die beim letzten Scan geantwortet haben (Bit n für 0x40 + n), DEVICE_ID des INA228 und Die-ID des INA3221 wie gelesen, fehlgeschlagene Transaktionen modulo 65536, die Chiptemperatur des INA228 in 0,1 °C (vorzeichenbehaftet) und DIAG_ALRT, seine Ladung in 0,01 mAh (vorzeichenbehaftet, 32 Bit, Register 19 und 20, niederwertiges zuerst) und Energie in 0,01 Wh (32 Bit, Register 21 und 22, niederwertiges zuerst) seit dem Schärfen des Laufs, die Telemetriespannung (10 mV) und der Telemetriestrom (10 mA) des ESC selbst und ihre Gültigkeitsbits (Bit 0 Spannung, Bit 1 Strom). Register 0 bis 11 werden im Flash des Koprozessors gehalten |
| 0x2C | SERVO_SENSE | lesen, schreiben | die Kanäle des INA3221 und eine Bewegung, gemessen auf dem Takt des Koprozessors (seit 4.7). Register 0 bis 11, nur lesen, vier je Kanal ab CH1: mittlerer Strom (mA, vorzeichenbehaftet), höchster Strom (mA, vorzeichenbehaftet), mittlere Busspannung (mV) und niedrigste Busspannung (mV) über das letzte 50-ms-Fenster, die Spannung auf der Lastseite des Shunts. Register 12, nur lesen: die Fensternummer modulo 65536; ein Lesen beendet kein Fenster. Register 13, nur lesen: Bits 0..2 das Fenster eines Kanals enthält Messungen, Bits 4..6 eine davon lag am Ende des Bereichs (163,8 mV über dem Shunt), womit mittlerer und höchster Strom dieses Kanals Untergrenzen sind, Bit 7 dasselbe für die Messung der Bewegung. Register 14 bis 17, ein Frame: eine Messung -- Bit 7 gesetzt, der INA3221-Kanal (1 bis 3) in Bits 0..1 und in Bits 8..10 der Ausgangskanal (0 bis 7), dessen nächstes geändertes Kommando die Zeitmessung startet; der Haltestrom, bei dem die Bewegung endet, 0 bis 32767 mA; die Schwelle für Bewegung und das Band für Ankunft, je 1 bis 32767 mA. Ein Scharfschalten ist der ganze Frame und startet eine laufende Messung neu. 0 in Register 14 entschärft und wird nie abgewiesen; am Anfang des Frames geschrieben, werden die anderen drei nicht gespeichert. Mit BAD_VALUE abgewiesen: jedes andere Schreiben, das nicht der ganze Frame ist, andere Bits in Register 14, ein Wert außerhalb seines Bereichs, ein Kanal, den SENSE nicht liest, und ein Ausgangskanal, der keine Surface an einem PWM-Slot ist; mit NOT_ARMED bei entschärftem Prüfstand. Ein Prüfstand, der aufhört zu treiben, beendet eine nicht fertige Messung. Register 18 bis 24, nur lesen: der Zustand (0 idle, 1 scharf, 2 wartet auf Bewegung, 3 bewegt sich, 4 angekommen, 5 an einem Endanschlag eingeschwungen, 6 zu spät, nach 3000 ms), fertige Messungen modulo 65536, die Zeit vom PWM-Frame mit dem neuen Puls bis zur Bewegung und bis zur Ankunft in 0,1 ms, höchster und mittlerer gefilterter Strom der Bewegung (mA, vorzeichenbehaftet) und die Zahl der Messungen darin. Nichts wird gespeichert: Nach einem Neustart des Koprozessors steht überall 0 |

Fault-Bitmap: Bit 0 Link still, Bit 1 Überstrom, Bit 2 Übertemperatur, Bit 3
Stall, Bit 4 Heartbeat ausgeblieben, Bit 5 Protokollversion abweichend.
Faults bleiben gesetzt, bis sie gelesen und gelöscht werden. Bit 0 setzt
voraus, dass über den Link mindestens eine Anfrage gelaufen ist: der
Koprozessor ist wach, bevor das Panel pollt, und das Warten auf die erste
Anfrage ist keine Stille.

Capabilities-Bitmap: Bit 0 ESC-Ansteuerung, Bit 1 ESC-Telemetrie, Bit 2
Servo-PWM, Bit 3 Servo-Strommessung, Bit 4 Akkumessung, Bit 5 Empfängerbus,
Bit 6 Vibrationssensor und Indeximpuls, Bit 7 Zellenmonitor, Bit 8
Programmierung. Das Panel leitet daraus die Marken im Menü ab.

BENCH-Flags: Bit 0 Spannung gültig, Bit 1 Strom gültig, Bit 2 Drehzahl
gültig, Bit 3 Temperatur des ESC gültig, Bit 4 Temperatur des Motors gültig,
Bit 5 Spannung, Strom und Leistung sind die des INA228 und nicht die
Telemetrie des ESC (seit 4.7), Bit 6 Ladung und Energie sind die Zähler des
INA228, beim Schärfen dieses Laufs gelöscht, und das Bauteil hat
durchgehend geantwortet (seit 4.7), Bit 7 simuliert. Ohne Bit 6 zählt ein
Panel Ladung und Energie selbst aus dem Strom; das Panel dieses Builds tut
das immer. Die beiden Temperaturen haben getrennte Bits, weil sie aus
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
Safety-Leitung, die Pins des CAN-Controllers und jede Nummer über dem letzten
GPIO des Bauteils. Ein verweigerter Slot bleibt ungebunden, während die Page
weiterhin zurückliest, was gefordert wurde. [DShot und die
Output-Treiber](DShot-de.md) hat den Rest.

Einträge in CHAN_CFG und OUTPUTS werden ganz geschrieben, vier Register auf
einmal. Der Pulsbereich eines Kanals ist standardmäßig 1000..2000 µs;
Endpunkte außerhalb von 400..2500 µs werden mit BAD_VALUE abgewiesen. Ein
Kommando außerhalb seines Bereichs wird begrenzt. Zwei Slots auf einem Pin
oder zwei Slots, die denselben Kanal ausgeben, werden abgewiesen. Über das
Schärfen entscheidet der Koprozessor: ein Schreiben von ARM wird mit
NOT_ARMED abgewiesen, solange der Link im Failsafe ist oder dem Heartbeat
nicht vertraut wird.

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
`test_link_pages`.

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
Register). Bei 20 Hz sind das 1,55 % des Busses. Ein Lesen der Register 12
bis 25 von SENSE und aller 25 Register von SERVO_SENSE sind zwei
Anfrage-Frames und elf Daten-Frames, etwa das 2,5-Fache der Bits eines Polls
der Bench-Page: bei 20 Hz etwa 3,9 % des Busses. Die Nutzlast von Classic CAN
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
Bus feuert.
