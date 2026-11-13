# Die Sensorknoten

<sub>[English](Nodes.md) · **Deutsch**</sub>

Ein Sensorknoten ist ein ESP32-C6-Board an einer eigenen LiPo-Zelle, das
fertige Messwerte über Bluetooth LE (Low Energy, BLE) sendet. Er hat kein
Kabel zum Prüfstand. Der Leistungsknoten liest den INA228 eines MATEK
I2C-INA-BM im XT60-Leistungspfad des ESC (Electronic Speed Controller,
Motorregler). Der Auswuchtknoten wird den Vibrationssensor und den
Indexsensor tragen; seine Firmware ist ein Stub.

Der Leistungsknoten dient der Messung, nicht als Schutzfunktion. Keine
Grenze, keine Abschaltung und kein Fehlerpfad des Prüfstands liest ihn, so
wie heute keiner den verkabelten INA228 liest.

Waveshare, MATEK, GERUI und Texas Instruments sind Namen ihrer Inhaber, und
Bluetooth ist eine Marke ihres Inhabers. rcbench ist unabhängig: nicht mit
ihnen verbunden, nicht von ihnen unterstützt oder gesponsert. Die Namen
sagen, aus welchen Teilen der Knoten gebaut ist.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

| Teil | Wo |
| --- | --- |
| die Pakete, die UUIDs (Universally Unique Identifier), die Steuer-Writes, das Advertising, die 20ms-Fenster | `protocols/rcnode`, kein Pin-Treiber |
| Start, der Schritt einmal je Sekunde, Schlafen und Aufwachen | `firmware/node/main/node_main.c` |
| die Entscheidungen zu Akku und Schlaf, in der Host-Suite getestet | `firmware/node/main/node_policy.c` |
| der GATT-Server (Generic Attribute Profile) und das Advertising auf NimBLE | `firmware/node/main/node_ble.c` |
| die Rolle Leistung: der INA228, die Fenster, die Akkumulatoren | `firmware/node/main/node_pwr.c` |
| der INA228-Treiber, derselbe wie im IOmcu | `shared/sense/ina228.c`, `shared/sense/sense_bus.c` |
| die Rolle Auswuchten, ein Stub | `firmware/node/main/node_bal.c` |
| die Akkumessung | `firmware/node/main/node_bat.c` |
| die Konsole | `firmware/node/main/node_console.c` |

## Die Teile des Leistungsknotens

| Teil | Zweck |
| --- | --- |
| Waveshare ESP32-C6-Zero | ESP32-C6FH4 mit 4MB Flash im Gehäuse, USB-C (USB Serial/JTAG), LDO (Low-Dropout-Regler) ME6217C33M5G vom 5V-Pin auf 3,3V, RGB-LED WS2812 an GPIO8, Taste BOOT an GPIO9, Taste RESET |
| Lade- und Boost-Modul GERUI CD42 | lädt die Zelle über VIN (4,5V bis 5V) und erzeugt aus ihr 5V an OUT |
| eine 1S-LiPo-Zelle | an BAT des Moduls |
| MATEK I2C-INA-BM | INA228, Shunt 200µΩ, Adresse 0x45, XT60 ein und aus |
| 2 × 200kΩ, 1 × 100nF | der Akku-Spannungsteiler |
| 2 × 2,2kΩ | I2C-Pull-ups (Inter-Integrated Circuit) auf 3,3V |

USB-VBUS des Boards erreicht seinen 5V-Pin über eine Schottky-Diode
B5819WS (Schaltplan des Boards). Mit OUT des Moduls am 5V-Pin fließt nichts
zurück in ein USB-Kabel.

## Verdrahtung

| Von | Nach | Hinweis |
| --- | --- | --- |
| Zelle + | CD42 BAT+ | |
| Zelle − | CD42 BAT− | |
| CD42 OUT 5V | ESP32-C6-Zero 5V | der LDO des Boards erzeugt 3,3V |
| CD42 OUT GND | ESP32-C6-Zero GND | |
| CD42 BAT+ | 200kΩ nach GPIO3 | oberer Teil des Teilers |
| GPIO3 | 200kΩ nach GND, 100nF nach GND | unterer Teil des Teilers; GPIO3 ist ADC1 Kanal 3 |
| ESP32-C6-Zero GPIO19 | MATEK SDA | 2,2kΩ von SDA nach 3V3 |
| ESP32-C6-Zero GPIO18 | MATEK SCL | 2,2kΩ von SCL nach 3V3 |
| ESP32-C6-Zero 5V | MATEK 5V | die Versorgung des INA228 |
| ESP32-C6-Zero GND | MATEK G | die einzige Masse des Knotens |

Die Leitung zum MATEK ist 10cm lang oder kürzer. Der Bus läuft mit 400kHz.
Die Firmware schaltet die Pull-ups der Pads ab; der Bus läuft auf den
2,2kΩ-Pull-ups und denen des Moduls.

Der Teiler zieht bei 4,2V 10,5µA aus der Zelle, auch während der Knoten
schläft oder aus ist.

**Vor dem ersten Anschließen.** An welcher Schiene die I2C-Pull-ups des
MATEK hängen, ist nicht bekannt. Den MATEK allein mit 5V versorgen, SDA und
SCL offen lassen und beide gegen G messen: Beide müssen 3,3V oder weniger
zeigen. Ein höherer Wert braucht einen Pegelwandler; die Pins des ESP32-C6
vertragen 3,3V.

### Die Masseregel

GND des Knotens ist G des MATEK, und G ist der Minuspol des Akkupacks. Der
Minuspol der Zelle ist ebenfalls GND des Knotens. Der Knoten hat kein
weiteres Kabel: Der gemessene Pfad ist vom Prüfstand getrennt.

- Kein USB-Kabel am Knoten, solange ein Pack am XT60 steckt. Die Masse des
  Kabels ist dann der Minuspol des Packs, und ein Laptop oder das Panel am
  selben USB-Baum schließt eine Schleife über die Rückleitung des ESC.
- Den Knoten mit abgezogenem XT60 laden oder an einer Powerbank, an der
  sonst nichts hängt.
- Das Gehäuse des Knotens isoliert das Board von der Shunt-Seite des Moduls.

## Das CD42-Modul

Der GERUI "5V 2A Laderegler" trägt einen Chip mit der Aufschrift CD42 und
eine Ladeanzeige aus 4 LEDs (25%, 50%, 75%, 100%).

| Anschluss | Zweck |
| --- | --- |
| VIN | 4,5V bis 5V aus einer Quelle für 2A bis 3A, zum Laden |
| BAT | die 3,7V-Zelle |
| OUT | 5V, aus der Zelle hochgesetzt |
| KEY | eine Taste |

Nicht bekannt, am ersten Modul zu messen:

- die Abschaltschwelle bei geringer Last: der Laststrom, unter dem das Modul
  OUT abschaltet;
- der Ruhestrom des Moduls an der Zelle;
- ob KEY oder eine Last den Boost startet.

**Was die Schlafregel annimmt.** Das Modul hält OUT auf 5V, während der
Knoten schläft. Die Last an OUT im Schlaf ist der ESP32-C6 im Deep Sleep
(7µA, Angabe des Chips), der LDO und die WS2812 des Boards im Ruhezustand
(nicht gemessen), ein eigener Regler des MATEK, falls vorhanden (nicht
bekannt), und der abgeschaltete INA228 (5µA). Schaltet das Modul OUT unter
dieser Last ab, wird der Knoten beim ersten Schlafen dunkel: Der 10s-Timer
läuft nie, die Taste RESET bewirkt nichts, und nur was den Boost des Moduls
wieder startet (KEY oder ein Ladegerät an VIN, nicht bekannt, was davon),
holt den Knoten zurück.

**So wird es gemessen.**

1. Ein Messgerät in Reihe in OUT: der Strom mit dem Knoten wach, beim
   Advertising und verbunden.
2. Auf der Konsole `sleep`. Ein Messgerät an OUT für 2min: OUT bleibt bei
   5V oder fällt ab.
3. Der LA2016 (Logikanalysator) an SCL: alle 10s ein Lesen der Busspannung
   zeigt, dass der Knoten über seinen Timer aufwacht.
4. Die Schwelle: OUT mit Widerständen belastet, der Knoten nicht an OUT. Der
   kleinste Strom, der OUT 5min lang eingeschaltet hält.
5. Der Ruhestrom: ein Messgerät in der Leitung der Zelle, OUT ohne Last.

Schaltet OUT im Schlaf ab, bleiben ein Lastwiderstand an OUT, der die Last
über der Schwelle hält (er kostet die Zelle diesen Strom), oder ein
Schiebeschalter in der Leitung der Zelle und kein Schlaf. Die Wahl wartet
auf die Messwerte.

## Schlafen und Aufwachen

| Bedingung | Was der Knoten tut |
| --- | --- |
| 120s ohne Verbindung, ohne Pack am XT60 und ohne `stay on` | Deep Sleep mit einem 10s-Timer |
| SLEEP auf CONTROL geschrieben oder `sleep` auf der Konsole | dasselbe, sofort |
| ein Timer-Wecken | der INA228 wandelt die Busspannung einmal (540µs), der Knoten liest sie und schaltet das Bauteil ab; 5,0V oder mehr startet den Knoten vollständig, weniger schläft weiter |
| eine Zelle zwischen 1,0V und 3,3V, 3 Messungen in Folge oder bei einem Timer-Wecken | aus: Deep Sleep ohne Timer |
| OFF auf CONTROL geschrieben oder `off` auf der Konsole | aus, sofort |
| die Taste RESET, ein Aus- und Einschalten, ein Flashen | ein vollständiger Start |

Ein Teilerwert unter 1,0V ist keine Zelle, das Board nur an USB, und
schaltet nichts ab. Unter 3,5V meldet STATUS den Akku als schwach. Vor dem
Schlafen beendet der Knoten die Verbindung, wartet 200ms und schreibt
ADC_CONFIG des INA228 auf Shutdown.

Die Taste BOOT ist GPIO9, und GPIO9 kann den ESP32-C6 nicht aus dem Deep
Sleep wecken (nur GPIO0 bis GPIO7 können das). Die Taste RESET ist die
Wecktaste.

Ein Timer-Wecken lädt das Image ohne erneute Prüfung
(`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`). Wie lange ein
Timer-Wecken dauert und welchen mittleren Strom der Knoten im Schlaf zieht,
ist nicht gemessen. Die Schätzung des Plans ist 100µA: alle 10s 30ms wach
bei 30mA, dazu der Strom im Schlaf.

## Der INA228

Der Knoten schreibt die Einstellung des Prüfstands (`INA228_ADC_BENCH`):
Bus- und Shuntspannung je 540µs, Temperatur 50µs, Mittelung 1, ein
Ergebnis alle 1130µs. ADCRANGE 1 (±40,96mV) für 204,8A am 200µΩ-Shunt:
390,625µA je Schritt. Der Treiber ist derselbe wie im IOmcu.

| Alle | Gelesen | Wohin |
| --- | --- | --- |
| 1130µs | CURRENT und VBUS | das 20ms-Fenster |
| 20ms | | ein Fensterpaket: mittlerer und höchster Strom, mittlere und niedrigste Spannung, die Zahl der Samples, Clip-Flags |
| 40ms | ADC_CONFIG, gegen den geschriebenen Wert | ein zurückgesetztes Bauteil wird 1000ms später neu eingestellt und gezählt |
| 500ms | DIAG_ALRT, dann CHARGE und ENERGY (roh, 40 Bit; das Lesen löscht ihre Überlauf-Flags), DIETEMP | ein Akkumulatorpaket und die Extremwerte seit dem letzten RUN-Write |

Ein Fenster hält 17 oder 18 Samples. Ein Stromsample an einem Ende des
20-Bit-Bereichs ist ein Clip: gezählt und markiert, nie ein Wert. Der
Knoten löscht die Akkumulatoren nie. Das Bauteil löscht sie bei seinem
eigenen Reset und bei jeder Einstellung, und epoch im Akkumulatorpaket
zählt die Einstellungen. Die Pakete stehen in
[protocols/rcnode](../protocols/rcnode/README.md).

## Bluetooth LE

Der Knoten sendet sein Advertising als `rcbench-pwr-XXXX`, die letzten zwei
Bytes seiner Adresse in Hex, alle 100ms, solange keine Verbindung steht.
Die Herstellerdaten tragen die Rolle, die Scan Response die UUID des
Knotendienstes. Eine Central zur Zeit, kein Pairing, kein Bonding. Der
Knoten fragt eine ATT-MTU (Attribute Protocol, Maximum Transmission Unit)
von 247 an und keine eigenen Verbindungsparameter.

| Characteristic | Eigenschaften | Inhalt |
| --- | --- | --- |
| STREAM | NOTIFY | Fenster alle 20ms, Akkumulatoren und Extremwerte alle 500ms |
| STATUS | READ, NOTIFY | Akku, Flags, Firmwareversion, Zähler, alle 1000ms |
| CONTROL | WRITE, WRITE WITHOUT RESPONSE | RUN mit einem Tag, SLEEP, OFF; BLADES und RPM am Auswuchtknoten |
| INFO | READ | Protokoll, Rolle, Board, Adresse, die Einstellung des INA228 |

Ein Write, den die Rolle nicht annimmt oder der eine andere Länge hat,
wird mit dem ATT-Fehler 0x13 oder 0x0D abgelehnt.

STREAM-Pakete warten in einer Warteschlange für 32 Pakete auf den Sender.
Eine volle Warteschlange oder ein Stack ohne Puffer verwirft das Paket und
zählt es in `dropped` von STATUS.

## Bauen und Flashen

Die Rolle wird beim Bauen gewählt (`main/Kconfig.projbuild`). Ein Board ist
für einen Sensor verdrahtet, also ist die Rolle Teil des Images, keine
gespeicherte Einstellung, die ein vollständiges Löschen des Flash verliert.

```sh
. ~/esp/esp-idf-v6.1/export.sh
cd firmware/node
idf.py -B build-pwr -D SDKCONFIG=build-pwr/sdkconfig build
idf.py -B build-pwr merge-bin -o rcbench-node-pwr-merged.bin
```

Der Stub des Auswuchtknotens:

```sh
idf.py -B build-bal -D SDKCONFIG=build-bal/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.balance" build
```

Ein Release enthält `rcbench-node-pwr-<version>.bin`, die Anwendung, und
`rcbench-node-pwr-merged-<version>.bin` für 0x0. Der CI-Job (Continuous
Integration) "Node image" baut beide.

Zum Flashen zuerst das Pack vom XT60 abziehen (die Masseregel), dann USB-C
des Knotens anschließen:

```sh
python -m esptool --chip esp32c6 -p /dev/ttyACM0 -b 460800 \
  write-flash 0x0 rcbench-node-pwr-merged-<version>.bin
```

Ein schlafender Knoten antwortet nicht über USB. RESET drücken, um ihn zu
starten, oder BOOT halten und RESET drücken für den Download-Modus des ROM.

## Die Konsole

USB Serial/JTAG an USB-C des Boards. Eine Zeile je Sekunde:

```text
I (5123) node: pwr 50 win/s, n 18, 22.18V 14.300A, part online, epoch 1; link up -58dBm, sub 1, bat 3.92V, errors 0, resets 0, dropped 0
```

| Feld | Bedeutung |
| --- | --- |
| `win/s` | in der letzten Sekunde geschlossene Fenster, 50, wenn jedes Fenster ein Sample hat |
| `n` | Samples im letzten Fenster |
| Volt und Ampere | die Mittelwerte des letzten Fensters |
| `part` | `online` oder `not answering` |
| `epoch` | Einstellungen des INA228 seit dem Start |
| `link` | none ohne Verbindung, sonst up und der RSSI (Received Signal Strength Indication) |
| `sub` | 1, wenn STREAM abonniert ist |
| `bat` | die Zelle; unter 1.00V ohne Zelle |
| `errors`, `resets`, `dropped` | die Zähler von STATUS |

| Befehl | Wirkung |
| --- | --- |
| `status` | Name, Firmware, Rolle und der letzte Status vollständig |
| `stay on`, `stay off` | hält den Knoten wach oder lässt ihn wieder schlafen; bis zu einem Neustart |
| `sleep` | Deep Sleep mit der 10s-Prüfung, in der nächsten Sekunde |
| `off` | aus, in der nächsten Sekunde |

Der Start schreibt `rcbench node <version>, role pwr, <n> timer wakes before
this start`.

## Aktuelle Einschränkungen

- Das Panel verbindet sich mit keinem Knoten. Der BLE-Hub und die Seite des
  Panels sind spätere Schritte; ein BLE-Scanner auf einem Telefon zeigt den
  Dienst und den Stream.
- Kein Knoten ist auf Hardware gelaufen: Die Boards sind bestellt, nicht
  geliefert. Die Pins, das Aufwachen, die Ströme und das Verhalten des CD42
  sind nicht gemessen.
- Die Abschaltschwelle des CD42 bei geringer Last ist nicht bekannt (oben).
- Die Rolle Auswuchten ist ein Stub: Sie sendet ihr Advertising als
  `rcbench-bal-XXXX`, nimmt BLADES und RPM an und sendet keinen Bericht.
- Die WS2812 wird nicht angesteuert. Ihr Ruhestrom ist nicht gemessen.
- Die Firmware wird nur über USB aktualisiert.
- Fenster ohne Sample zwischen zwei Fenstern werden nicht gesendet: Ein
  stehender Sampler zeigt sich als Sprung in `t_node` ohne Sprung in `seq`.
