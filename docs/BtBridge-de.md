# Die Bluetooth-Bridge

<sub>[English](BtBridge.md) · **Deutsch**</sub>

Die Bluetooth-Bridge ist eine Platine mit einem klassischen ESP32 an der
Stelle des HC-05 zwischen dem Panel und dem WitMotion-BWT901CL-Winkelsensor
([Die Bluetooth-IMU](IMU-de.md)). Sie beantwortet die HC-05-AT-Befehle des
Panels so, wie ein HC-05 als SPP-Master (Serial Port Profile) sie
beantwortet, verbindet sich über Bluetooth Classic mit dem Sensor und
reicht die Bytes zwischen der Verbindung zum Sensor und UART1 (Universal
Asynchronous Receiver-Transmitter) des Panels durch. Die Firmware des
Panels ist für beide dieselbe: Sie kann die Bridge nicht von einem HC-05
unterscheiden.

Die Bridge gibt es, weil ein HC-05 ein echter sein muss, mit dem Chip CSR
BC417 und seinem Flash-Chip, um als Master zu arbeiten. HC-06-Module,
JDY-xx-Module und die als HC-05 verkauften Nachbauten mit Jieli-Chip sind
nur Slaves und können sich nicht mit dem Sensor verbinden.

Der ESP32 muss der ursprüngliche ESP32 sein, etwa das Modul ESP32-WROOM-32:
Er ist der einzige ESP32 mit Bluetooth Classic. Die späteren Familien, S2,
S3, C3, C6, H2 und P4, haben keines. Die Pins unten sind
die einer Platine D1 mini ESP32 (ESP32-WROOM-32, 4MB Flash).

| Teil | Wo |
| --- | --- |
| die AT-Befehle, die Modi und die Verbindungsversuche | `protocols/hc05emu`, kein Pin-Treiber ([README](../protocols/hc05emu/README.md)) |
| UART, KEY, STATE, LED und Task | `firmware/btbridge/main/main.c` |
| Bluedroid: Inquiry, Namen, Pairing, die SPP-Verbindung | `firmware/btbridge/main/bridge_bt.c` |
| die Einstellungen im NVS (Non-Volatile Storage, nichtflüchtiger Speicher) | `firmware/btbridge/main/bridge_store.c` |

## Verdrahtung

Die Bridge wird wie der HC-05 verdrahtet
([Die Bluetooth-IMU: Verdrahtung](IMU-de.md#verdrahtung)): 5V und GND vom
Header H9, die Signale auf den Header P3.

| D1 mini ESP32 | Signal | Panel |
| --- | --- | --- |
| 5V (VCC) | Versorgung, zum 3,3V-Regler der Platine | Core_5V des Headers H9 (RS485), Pin 1 |
| GND | Masse | GND des Headers H9 (RS485), Pin 2 |
| GPIO 17 (D3) | UART2 TX, 3,3V | IO5 des Headers P3, UART1 RX (`PANEL_IMU_PIN_RX`) |
| GPIO 16 (D4) | UART2 RX | IO4 des Headers P3, UART1 TX, 3,3V (`PANEL_IMU_PIN_TX`) |
| GPIO 18 (D5) | KEY-Eingang, interner Pull-down: high für den AT-Modus | IO29 des Headers P3 (`PANEL_IMU_PIN_KEY`) |
| GPIO 19 (D6) | STATE-Ausgang: high bei offener Verbindung | nicht verdrahtet (`PANEL_IMU_PIN_STATE` ist `GPIO_NUM_NC`) |
| GPIO 2 | die LED der Platine | |

Beide Seiten arbeiten mit 3,3V, ein Pegelwandler ist nicht nötig. Die Pins
des ESP32 sind nicht 5V-fest. GPIO 16 bis GPIO 19 sind keine
Strapping-Pins; GPIO 2 ist einer, und die Platine selbst hat ihre LED daran.
Die Pins, die LED und ihre Polarität sind Optionen in
`firmware/btbridge/main/Kconfig.projbuild` (`idf.py menuconfig`, "rcbench
Bluetooth bridge"). Ein Modul ESP32-WROVER nutzt GPIO 16 und GPIO 17 für
sein PSRAM (Pseudo-Static Random-Access Memory); dort braucht UART2 zwei
andere Pins.

Der Strom, den die Platine aus Core_5V von H9 zieht, ist nicht gemessen. Ob
eine Platine ihre USB-5V und ihren 5V-Pin ohne Diode aus einem Netz
speist, ist hier nicht geprüft; trenne die 5V von H9, solange der USB der
Platine angeschlossen ist.

## Flashen

Das Release-Asset `rcbench-btbridge-vX.Y.Z.bin` ist das zusammengeführte
Image: Bootloader, Partitionstabelle und Anwendung, geschrieben an Offset
0x0. `esptool` kommt mit ESP-IDF v6.1, oder `pip install esptool`.
`/dev/ttyUSB0` heißt unter Windows `COMx`.

```bash
esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x0 rcbench-btbridge-vX.Y.Z.bin
```

Aus den Quellen, mit ESP-IDF v6.1 (`. $IDF_PATH/export.sh`):

```bash
idf.py -C firmware/btbridge build
idf.py -C firmware/btbridge -p /dev/ttyUSB0 flash monitor
```

`sdkconfig.defaults` setzt das Ziel `esp32`. Fehlt die Xtensa-Toolchain von
ESP-IDF v6.1, findet CMake keinen Compiler; installiere sie mit:

```bash
$IDF_PATH/tools/idf_tools.py install --targets esp32
```

Die Konsole am USB-Port der Platine läuft mit 115200 Baud. Sie zeigt die
Version, ob eine gespeicherte Einstellung gelesen wurde, den Pegel von KEY
beim Start, jeden Wechsel von Modus und Verbindung und jeden Funkvorgang.

## Betrieb am Panel

Am Panel ändert sich nichts. Mit IMU EIN findet die Probe des Panels die
Bridge bei 115200 Baud, und die Seite IMU zeigt `HC-05: btbridge-X.Y.Z`.
`SENSOREN SUCHEN` listet die Sensoren in Reichweite, ein Tipp auf einen
bindet ihn: Die Bridge koppelt mit PIN 1234, speichert die Adresse und
startet im Datenmodus neu, in dem sie sich mit dem Sensor verbindet. Das
Panel bietet `MIT JEDEM SENSOR VERBINDEN` nur an, nachdem eine Suche
ERROR:(1F) beantwortet hat; das antwortet die Bridge nie.

Die Bridge behält die Einstellungen, die ein HC-05 in seinem Flash behält:
Rolle, CMODE (Verbindungsmodus), die gebundene Adresse, die PIN, die UART
und den Inquiry-Modus. `AT+ORGL` stellt die Einstellungen einer neuen
Bridge her und vergisst die Adresse; `esptool --chip esp32 erase-flash`
löscht sie mit dem Image.

Eine neue Bridge ist Master, CMODE 0 ohne Adresse, PIN 1234, 115200 Baud 8N1
(8 Datenbits, keine Parität, 1 Stoppbit). Im Datenmodus verbindet sie sich
mit der gebundenen Adresse, versucht es 1000ms nach einem fehlgeschlagenen
Verbindungsaufbau oder einer verlorenen Verbindung erneut und bricht einen
Verbindungsaufbau ab, der nach 15000ms nicht beendet ist. KEY high über
einer offenen Verbindung hält die Verbindung und ihre Bytes zurück, bis KEY
fällt. `AT+RESET` startet die Bridge 300ms nach ihrem OK neu, in dem Modus,
den KEY dann wählt. Das README von `protocols/hc05emu` nennt jeden Befehl,
jede Antwort, jeden Fehler und jedes Timeout.

## LED

| LED | Zustand |
| --- | --- |
| 1000ms an, 1000ms aus | AT-Modus: KEY high |
| 100ms an, 100ms aus | Datenmodus, keine Verbindung |
| an | Datenmodus, Verbindung zum Sensor offen |
| aus | der Neustart von 300ms nach `AT+RESET` |

STATE (GPIO 19) ist high, solange die Verbindung offen ist, in beiden
Modi, wie der STATE-Pin eines HC-05.

## Grenzen

| Punkt | Wert |
| --- | --- |
| Rollen | nur Master; `AT+ROLE=0` wird gespeichert und beantwortet, die Bridge verbindet sich dann mit nichts |
| Pairing | Legacy-Pairing mit PIN, 1 bis 16 Bytes; kein SSP (Secure Simple Pairing) |
| Verbindungen | eine |
| das Image | 651.232 Bytes in einer Anwendungspartition von 1.536.000 Bytes, 716.768 Bytes zusammengeführt (0.23.8) |
| die überlastete Verbindung | 512 Bytes von der UART gehalten, mehr verworfen |
| Befehle außerhalb des HC-05-Satzes, den sie nutzt | `ERROR:(0)` ([README](../protocols/hc05emu/README.md#not-covered)) |

## Nicht gemessen

Der Strom aus Core_5V von H9; die Zeit vom Einschalten bis zur ersten
AT-Antwort; die Zeit bis zur Verbindung mit einem BWT901CL; die
Reichweite; ob Frames mit 200Hz ohne Verlust durchgehen und welche
Verzögerung die Bridge hinzufügt. Keine Bridge ist mit einem Panel oder
einem Sensor gelaufen.

WitMotion, BWT901CL, HC-05, HC-06, JDY, Jieli, CSR, BC417, ESP32 und D1
mini sind Namen von Produkten ihrer Hersteller, und Bluetooth ist eine
Marke ihres Inhabers. rcbench ist unabhängig: nicht mit ihnen verbunden,
nicht von ihnen unterstützt oder gesponsert. Die Namen sagen, wen die
Bridge ersetzt und mit wem sie sich verbindet.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.
