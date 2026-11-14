# Der Funk über den ESP32-C6

<sub>[English](C6.md) · **Deutsch**</sub>

Der ESP32-C6 auf der Panel-Platine liefert dem ESP32-P4 Wi-Fi und Bluetooth LE
(Low Energy, BLE). Diese Seite beschreibt die Verdrahtung des C6, seine
Firmware, das Update über SDIO, die Wiederherstellung über Header H4 und den
Bluetooth-LE-Dienst.

## Wi-Fi über den ESP32-C6

Der ESP32-P4 hat kein Funkteil. Das 7-Zoll-Display-Board von Waveshare trägt
ein ESP32-C6-MINI-1-Modul, das als SDIO-Gerät (Secure Digital input/output)
am SDMMC-Host (SD/MMC host) des P4 auf Slot 1 hängt; die SD-Karte liegt auf
Slot 0. Der Wi-Fi-Code des Panels (`firmware/panel/components/wifi_access/`)
ruft die `esp_wifi`-API auf; zwei Komponenten aus der Espressif Component
Registry übertragen jeden Aufruf und jeden Netzwerk-Frame zum C6. lwIP
(lightweight IP stack), der Server der Seite und der TLS-Client (transport
layer security) laufen auf dem P4; der C6 betreibt das Funkteil.

| Signal | P4 | C6 |
| --- | --- | --- |
| CLK | GPIO18 | IO19 |
| CMD | GPIO19 | IO18 |
| D0 bis D3 | GPIO14 bis GPIO17 | IO20 bis IO23 |
| EN (chip enable), Low setzt zurück | GPIO54 | EN |

SDIO läuft mit 4 Bit Breite und 40MHz.
`components/board/include/board_pins.h` nennt die Pins (`BOARD_C6_*`),
`firmware/panel/sdkconfig.defaults` gibt sie an esp_hosted, und
`components/c6_link/c6_link.c` bricht den Build ab, wenn beide abweichen.
`c6_link` bringt die Verbindung hinter einem Mutex hoch, sodass sich jeder
Nutzer des C6 einen Reset und einen Start teilt: der Wi-Fi-Task ruft
`c6_link_up()` vor `esp_wifi_init()` auf, der Start von Bluetooth LE vor
NimBLE ([Bluetooth LE über den
ESP32-C6](#bluetooth-le-über-den-esp32-c6)).

### Die managed components

| Komponente | Version | Lizenz | Aufgabe |
| --- | --- | --- | --- |
| `espressif/esp_wifi_remote` | 1.6.5 | Apache-2.0 | die `esp_wifi`-API auf einem Chip ohne Funkteil |
| `espressif/esp_hosted` | 2.13.0 | Apache-2.0 | der SDIO-Transport und die Aufrufe zum C6 |
| `espressif/wifi_remote_over_eppp` | 0.3.3 | Apache-2.0 | von `esp_wifi_remote` verlangt; kompiliert, nicht gelinkt |
| `espressif/eppp_link` | 1.1.6 | Apache-2.0 | von der Zeile darüber verlangt; kompiliert, nicht gelinkt |
| `espressif/esp_serial_slave_link` | 1.1.2 | Apache-2.0 | von der Zeile darüber verlangt; kompiliert, nicht gelinkt |

esp_hosted 2.x: jede Datei, die das Panel aus der Registry kompiliert, steht
unter Apache-2.0, außer der protobuf-c-Laufzeit in esp_hosted, BSD-2-Clause.
Der aus der `.proto`-Datei erzeugte Nachrichtencode von esp_hosted gehört
Espressif, Apache-2.0; `dummy_src.c` von `esp_wifi_remote` ist eine leere
Datei ohne Lizenzzeile.
[NOTICE](https://github.com/subtilitas/rcbench/blob/main/NOTICE) enthält
Copyright und Lizenz von protobuf-c, die bei der Weitergabe eines
Panel-Images mitgegeben werden müssen.

`components/wifi_access/idf_component.yml` nennt die ersten beiden mit
exakter Version. `firmware/panel/dependencies.lock` hält alle fünf mit ihren
Hashes und ist eingecheckt. Der erste Build lädt sie von
`components.espressif.com` nach `firmware/panel/managed_components/`, das git
ignoriert, und prüft jede gegen ihren Hash. Ein späterer Build stellt keine
Anfrage: mit gefülltem Ordner läuft ein Build ohne Netzwerk durch. Ändere für eine
andere Version `idf_component.yml`, führe `idf.py update-dependencies`
aus und checke die neue Lock-Datei ein.

### Die Firmware des C6

Der C6 betreibt Espressifs esp_hosted-Co-Prozessor-Firmware. Mit welcher
Version das Board ausgeliefert wird, ist nicht veröffentlicht und hier nicht
bekannt. Beim ersten Wi-Fi-Start nach dem Einschalten setzt esp_hosted den C6
zurück: GPIO54 (EN) geht für 10ms auf Low, dann wartet esp_hosted fest
1 500ms (`CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS`), startet den C6 als
SDIO-Karte und fragt alle 200ms nach seinem Start-Event. Danach schreibt
das Panel die Version des C6 auf die Konsole:

```
c6: C6: esp_hosted firmware 2.13.0, host 2.13.0
```

Eine C6-Firmware ohne die Versionsabfrage antwortet nach der Zeitgrenze von
esp_hosted für die Anfrage, 1s, mit
`c6: C6: firmware version not reported, host 2.13.0`. esp_hosted meldet
außerdem `Version mismatch: Host [2.13.0] > Co-proc [x.y.z]`, wenn Haupt-
oder Nebenversion abweichen. Laut Espressif kann ein Host mit einem älteren
Co-Prozessor laufen; dabei sind Zeitüberschreitungen bei Anfragen möglich. Das
Verhalten des Panels mit einer älteren C6-Firmware ist nicht gemessen. Ein
antwortender C6 wird einmal je Panel-Start zurückgesetzt. Spätere
Wi-Fi-Starts nutzen die bestehende Verbindung ohne erneuten Reset.

Am Panel der Station (2026-11-06) meldet der ausgelieferte C6
`transport: capabilities: 0xd` (Wi-Fi, HCI (Host Controller Interface) über
SDIO, BLE (Bluetooth Low Energy)) und `Version mismatch: Host [2.13.0] >
Co-proc [0.0.0]` und beantwortet die Versionsabfrage nicht. Ob er das Update
über SDIO annimmt, ist nicht bekannt.

#### Das Image, das das Panel mitbringt

CI und das Release bauen die Firmware des C6 aus dem Ordner `slave/` der
esp_hosted-Komponente, die das Panel festlegt, 2.13.0, mit ESP-IDF v6.1 für den
ESP32-C6, mit der eigenen Konfiguration der Komponente: Wi-Fi und nur der
BT-Controller, dessen HCI über die SDIO-Verbindung im RAM läuft (VHCI); das
braucht der NimBLE-Host des Panels über VHCI. Der Workflow ist
`.github/workflows/c6.yml`; `tools/c6_image.py` kopiert das Projekt mit
`firmware/c6/dependencies.lock` (die 5 Registry-Komponenten des Slave mit
festen Versionen und Hashes), hängt `firmware/c6/sdkconfig.rcbench` an seine
Target-Defaults an und schlägt fehl, wenn das Image keine ESP32-C6-Anwendung
`network_adapter` der festgelegten Version mit angehängtem SHA-256 ist, wenn es
nicht in den Anwendungs-Slot des C6 passt (`ota_0` und `ota_1` aus
`slave/partitions.esp32c6.csv`, je 1 966 080 Bytes), wenn der sdkconfig eine
dieser Einstellungen fehlt oder wenn der Build eine Festlegung der Lock-Datei
verschoben hat.

| Punkt | Wert |
| --- | --- |
| `rcbench-c6-2.13.0.bin` (über SDIO gesendet) | 1 283 616 Bytes, 65,3% des C6-Slots, mit `CONFIG_BT_LE_MAX_CONNECTIONS=5` wie mit 3 gleich; jede Verbindung belegt beim Start des Controllers etwa 1kB DRAM des C6 (Kconfig-Hilfe), der freie Heap ist nicht gemessen |
| `rcbench-c6-merged-2.13.0.bin` (über H4 ab 0x0 geflasht) | 1 349 152 Bytes: Bootloader, Partitionstabelle, otadata und die Anwendung |
| Wachstum des Panel-Images, wenn es mitgebracht wird | 1 284 272 Bytes Flash; 1 408 Bytes interner RAM (ein Puffer für einen Block von 1 400 Bytes) |
| Panel-Image damit, mit NimBLE und einem IOmcu-Image | 4 419 968 von 8 126 464 Bytes im Update-Slot, 45,6% frei; das Release-Image mit beiden IOmcu-Images ist nicht gemessen |

Ein Build bringt das Image mit `-D RCBENCH_C6_BIN=<Pfad>` mit, wie die
IOmcu-Images; ein Build ohne bringt keines mit, und die Zeile C6 RADIO auf
UPDATES sagt das.

```bash
# das Image des C6 aus der Managed Component des Panels (ein Panel-Build
# lädt sie vorher nach firmware/panel/managed_components)
. ~/esp/esp-idf-v6.1/export.sh
cd firmware/c6
python3 ../../tools/c6_image.py prepare \
  ../panel/managed_components/espressif__esp_hosted slave
cd slave && IDF_TARGET=esp32c6 idf.py build \
  && idf.py merge-bin -o rcbench-c6-merged.bin
cd ../../.. && python3 tools/c6_image.py check firmware/c6/slave \
  firmware/c6/slave/build c6-dist
# das Panel, das es mitbringt
cd firmware/panel && idf.py -D RCBENCH_C6_BIN=../../c6-dist/rcbench-c6-2.13.0.bin build
```

#### Das Update über SDIO

Die Zeile C6 RADIO auf SETUP > UPDATES zeigt die Version, die der C6 beim
ersten Wi-Fi-Start gemeldet hat, oder `nicht gemeldet`, die mitgebrachte
Version und `2s HALTEN ZUM C6-FUNK-UPDATE`, bei der mitgebrachten Version `2s
HALTEN: C6-FIRMWARE DES PANELS NEU INSTALLIEREN`. Das Halten ist aktiv, wenn
der Build ein Image mitbringt, das seine Prüfung bestanden hat, die Verbindung
zum C6 nicht ausgefallen ist, die Bench SAFE ist und kein ARM wartet, der
Netzteilausgang aus ist, kein Aufzeichnungslauf offen ist, keine
Platinenzeichnung geladen wird, weder das IOmcu-Update noch Download oder
Installation des Panels laufen, das WLAN aus ist, die Start-Task `blestart`
von Bluetooth LE nicht läuft und kein Update eines Funkgeräts läuft
([Bildschirme: Das Dial](Screens-de.md#das-dial)). Ein Tippen auf das graue Halten zeigt den Grund
im Alarmband. Das Update läuft auf der Task `c6upd` (Kern 0, Priorität 2, Stack
4 096 Bytes, beim ersten Update angelegt), ein Schritt nach dem anderen:

| Schritt | Aufruf | Grenze | Endet als |
| --- | --- | ---: | --- |
| Verbindung | `c6_link_up()`, dann die Version des C6 | 30 000ms | `Der C6-Funk hat nicht geantwortet. WLAN ist bis zum Neustart des Panels aus.`; ein C6, der die mitgebrachte Version meldet, bekommt das Image trotzdem |
| Beginn | `esp_hosted_slave_ota_begin()`: der C6 löscht den Slot, aus dem er nicht läuft | 20 000ms | ein Fehler: `Der C6-Funk nimmt kein Update über SDIO an. Flashe ihn über Header H4.` |
| Schreiben | `esp_hosted_slave_ota_write()`, 1 400 Bytes je Aufruf, 4 Aufrufe je Schritt | 10 000ms je Schritt | ein Fehler: noch einmal der Ende-Aufruf, der das OTA-Handle des C6 freigibt, dann `C6-Update abgebrochen: Schreiben fehlgeschlagen. Vorherige Firmware läuft.` |
| Ende | `esp_hosted_slave_ota_end()`: der C6 prüft den SHA-256 des Images | 10 000ms | `... Prüfung im C6 fehlgeschlagen ...` |
| Aktivieren | `esp_hosted_slave_ota_activate()`, nur bei einem C6, der 2.6.0 oder neuer gemeldet hat | 10 000ms | `... Aktivierung fehlgeschlagen ...` |

Ein Schritt über seiner Grenze beendet das Update mit `C6-Update
abgebrochen: keine Antwort. Vorherige Firmware läuft.`; danach wird nichts
mehr gesendet, und die späte Antwort des Schritts wird verworfen. Jede Grenze
liegt über der eigenen Antwortzeitgrenze von esp_hosted für den Aufruf (15s
für Beginn, 5s für die anderen). Der C6 behält den Slot, aus dem er startet,
bis zum Aktivieren (C6-Firmware ab 2.6.0) oder bis zum Ende-Aufruf (ältere
Firmware und ein C6, der keine Version meldet: er bekommt keinen
Aktivieren-Aufruf). Nach einem Erfolg lautet der Alarm `C6-Funk auf 2.13.0
aktualisiert. Das Panel startet neu.`, und das Panel startet 3s später neu,
damit Host und C6 ihre Verbindung neu aufbauen. Solange das Update läuft und
bis ein Schritt über seiner Grenze zurückkehrt, startet kein WLAN, ARM wird
abgelehnt (`ARMED gesperrt: Das C6-Funk-Update läuft.`), und Installation des
Panels, GitHub-Prüfung und IOmcu-Update warten. Die Dauer eines Updates ist
nicht gemessen.

Konsolenzeilen, Tag `rcbench`:

```
I (…) rcbench: C6: carries esp_hosted 2.13.0, 1283616 bytes
I (…) rcbench: C6 update: esp_hosted 2.13.0, 1283616 bytes
I (…) rcbench: C6: updated to esp_hosted 2.13.0, 1283616 bytes; restarting in 3000ms
E (…) rcbench: C6 update: begin refused, error 0x<e>; the C6 takes no update over SDIO
E (…) rcbench: C6 update ended <result> in phase <phase>, error 0x<e>, <n> of 1283616 bytes; the C6 keeps its firmware
W (…) rcbench: C6 update refused: row <row>, reason <why>
```

#### Wiederherstellung über Header H4

Ein C6 ohne startende Firmware, oder einer, der das Update über SDIO
ablehnt, nimmt das zusammengeführte Image über seine eigene serielle
Schnittstelle an. Header H4 führt UART0 des C6 (TXD0, RXD0), GND und seinen
Strapping-Pin IO9; die Reihenfolge der Pins von H4 ist hier nicht erfasst und
wird vom Bestückungsdruck des Boards abgelesen. IO9 auf Low, wenn der C6 aus
dem Reset kommt, startet seinen ROM-Download-Modus. EN liegt nicht auf H4:
GPIO54 des Panels treibt ihn.

1. Schalte das Panel aus. Prüfe das Image:
   `sha256sum -c SHA256SUMS --ignore-missing` im Ordner mit
   `rcbench-c6-merged-2.13.0.bin` und `SHA256SUMS` des Release.
2. Verdrahte einen 3,3V-USB-UART-Adapter (Universal Asynchronous
   Receiver-Transmitter): sein RX an H4 TXD0, sein TX an H4 RXD0, sein GND an
   H4 GND. Lege H4 IO9 auf GND.
3. Schalte das Panel ein. Öffne weder WI-FI noch das Halten C6 RADIO: ein
   Wi-Fi-Start setzt den C6 über GPIO54 zurück und beendet das Flashen.
4. Flashe ohne Reset durch den Adapter (seine Leitungen DTR und RTS
   erreichen keinen Pin des C6):

   ```bash
   python -m esptool --chip esp32c6 -p /dev/ttyUSB0 -b 460800 \
     --before no-reset --after no-reset \
     write-flash 0x0 rcbench-c6-merged-2.13.0.bin
   ```

   esptool 5 schreibt den Befehl `write-flash`, esptool 4 `write_flash`.
5. Schalte das Panel aus, nimm IO9 von GND, schalte es ein und starte Wi-Fi
   einmal. Die Konsole zeigt `c6: C6: esp_hosted firmware 2.13.0, host
   2.13.0`, die Zeile C6 RADIO `C6-Funk: 2.13.0. Auf diesem Panel: 2.13.0.`

Das zusammengeführte Image schreibt die ganze Partitionstabelle, also werden
NVS (Non-Volatile Storage, 16KiB ab 0x9000) und otadata des C6 neu
geschrieben; der C6 hält keine Einstellung, die das Panel braucht.

### Fehler

`esp_hosted_connect_to_slave()` gibt `ESP_OK` zurück, ob der C6
hochgekommen ist oder nicht (`esp_hosted_api.c` 177-192). Das Panel nimmt
das Event `ESP_HOSTED_EVENT_TRANSPORT_UP` von esp_hosted als Antwort und
wartet darauf höchstens 1 000ms, nachdem dieser Aufruf zurückgekehrt ist
(`c6_link.c`). Ein zweiter Aufrufer wartet am Mutex, bis der erste Aufruf
endet, und bekommt dann dessen Ergebnis.

- Ein C6, der nicht als SDIO-Karte startet: esp_hosted gibt nach dem Start
  der Karte auf, mindestens 1 500ms nach dem Reset von 1 520ms; 1 Reset.
- Ein C6, der als Karte startet und kein Start-Event schickt: esp_hosted
  fragt 100-mal im Abstand von 200ms und setzt den C6 nach 50 und nach 100
  Abfragen erneut zurück (`transport_drv.c` 227-252); 3 Resets, etwa 25s.
- In beiden Fällen scheitert der Start, WI-FI zeigt den gescheiterten
  Start, und das Panel hält den C6 bis zum nächsten Neustart im Reset
  (GPIO54 auf Low). Spätere Starts scheitern sofort, ohne Reset. Prüfstand
  und Anzeige laufen derweil weiter; der Start des Funkteils läuft in einem
  eigenen Task.
- Eine Verbindung, die während einer Sitzung ausfällt: esp_hosted sendet
  `ESP_HOSTED_EVENT_TRANSPORT_FAILURE`. Das Panel schreibt einmal
  `c6: C6 link failed: C6 held in reset, Wi-Fi and Bluetooth are off until
  the panel restarts`, hält den C6 im Reset, und die Sitzung endet als gescheiterter
  Start. Mit dem C6 im Reset halten die 51-kΩ-Pull-ups seine SDIO-Leitungen
  auf High, sodass der Lese-Task von esp_hosted, Priorität 23 auf Kern 0,
  wartet, statt in einer Schleife zu scheitern; auf einem Board ist das
  nicht gemessen. Das Panel startet dafür nicht neu
  (`CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n`): ein Neustart würde
  den Link zum IOmcu und den Heartbeat unterbrechen.
- Vom Ausfall bis Wi-Fi aus: die Event-Schleife stellt das Event zu (nicht
  gemessen), der Task des Funkteils sieht es binnen 100ms und nimmt das
  Funkteil ohne die zwei Anfragen an den C6 herunter, `esp_wifi_stop()` und
  `esp_wifi_deinit()`, die jeweils erst mit der Antwort-Zeitüberschreitung
  von esp_hosted nach 5s enden würden. Das Herunterfahren wartet bis zu
  1 500ms auf eine laufende GitHub-Abfrage. Auf einen Task, der danach
  noch in einer Namensauflösung steht, wartet es nicht: Er endet, wenn lwIP
  die Auflösung aufgibt, und bis dahin startet kein Abruf.
  Die Zeit auf einem Board ist nicht gemessen.

Die folgenden Einschränkungen liegen in esp_hosted. Jede führt zu einem
Abbruch und anschließendem Neustart des Panels:

- Meldet der Co-Prozessor in seinem Start-Event einen anderen Chip als den
  ESP32-C6 (`transport_drv.c` 697-702), ein Init-Event länger als 64 Byte
  (`transport_drv.c` 875-881) oder einen Streaming-SDIO-Modus, den der Host
  nicht unterstützt (`transport_drv.c` 944-947). Das Start-Event kommt beim ersten
  Wi-Fi-Start nach dem Einschalten.
- Scheitert ein CMD52-Zugriff (ein einzelnes Register) auf den C6, während
  esp_hosted die Karte startet: `ESP_ERROR_CHECK` auf die Register für
  Interrupt-Freigabe, Busbreite und Blockgröße
  (`port_esp_hosted_host_sdio.c` 131-134, 195-217). Ein C6, der während
  eines Wi-Fi-Starts nur teilweise antwortet, kann das auslösen.
- Lassen sich der SDMMC-Controller oder sein Slot 1 nicht einrichten, oder
  fehlt der Speicher für die Queues, die Pufferpools oder die Tasks von
  esp_hosted, hält esp_hosted das Panel beim Start mit einer Assertion an,
  vor `app_main()` und bevor die Anzeige etwas zeigt. Es richtet diese bei
  jedem Start ein, auch mit Wi-Fi aus.

Die Release-Prüfung beim Start startet das Funkteil von selbst: mit der
Erinnerung in SETUP > UPDATES an (`SET_UPD_REMIND`, ab Werk an) und einem
gespeicherten Heimnetz fragt das Panel nach jedem Einschalten GitHub nach
einem Release. Ein anhaltender Fehler einer der oben genannten Arten startet das Panel dann
bei jedem Einschalten neu, bis du die Erinnerung ausschaltest oder das gespeicherte Heimnetz
löschst.

Die SD-Karte auf Slot 0 und der C6 auf Slot 1 teilen sich einen
SDMMC-Controller:

- Der SDMMC-Treiber von ESP-IDF v6.1 nimmt für jeden Befehl den Mutex des
  Controllers und setzt den Taktteiler des Slots, zu dem der Befehl
  gehört, darin (`sd_host_slot_sdmmc_do_transaction()`,
  `components/esp_driver_sdmmc/src/sd_trans_sdmmc.c`); das Anmelden eines
  Slots setzt keinen Takt. Ein Einbinden auf Slot 0 und der Verkehr des C6
  auf Slot 1 kommen Befehl für Befehl abwechselnd dran. Welche Auswirkungen ein
  Taktwechsel zwischen zwei Befehlen auf eine Übertragung über den anderen Slot
  hat, ist nicht gemessen.
- ESP-IDFs Einbinden meldet Slot 0 selbst an dem Controller an, den
  esp_hosted angelegt hat (`sdmmc_host_init()` gibt den vorhandenen
  Controller zurück). Bei einem Fehler danach nimmt es Slot 0 wieder
  heraus, und der Controller verschwindet nur, wenn kein Slot mehr
  angemeldet ist (`components/fatfs/vfs/vfs_fat_sdmmc.c`,
  `components/esp_driver_sdmmc/legacy/src/sdmmc_host.c`). Ein Slot 0, der
  nicht gestartet ist, hat kein Handle, und sein Herausnehmen ändert
  nichts. `storage.c` überlässt den Start von Slot 0 dem Einbinden: Ein
  vorher gestarteter Slot 0 lässt den eigenen Start des Einbindens mit
  `ESP_ERR_INVALID_STATE` scheitern.

### Kerne und Speicher

Auf dem ESP32-P4 läuft ein Task, der über `xTaskCreate()` oder
`xTaskCreatePinnedToCore()` ohne Kern angelegt wird, auf Kern 0, und Kern 1
gehört der Control-Task und den dort festgelegten Tasks. Dafür linkt das
Panel mit `--wrap=xTaskCreatePinnedToCore`
(`components/c6_link/c6_link.c`): esp_hosted legt 6 Tasks mit
Priorität 23 an, ohne einen Kern zu nennen. `xTaskCreateStatic()` und die
Aufrufe `xTaskCreate*WithCaps()` sind nicht umgeleitet; esp_hosted nutzt
`xTaskCreateStatic()` nur mit `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`,
das den Build abbricht. Die Konsole listet beim Start jeden Task,
`task <Name> core <0|1|any> priority <n>`.

Beim Start wartet Kern 1 in `esp_startup_start_app_other_cores()`
(ESP-IDF `components/freertos/app_startup.c`), bis der Scheduler von
Kern 0 läuft. ESP-IDF v6.1 liest dort `port_xSchedulerRunning[0]` in einer
Schleife aus zwei Befehlen ohne Pause, während Kern 0 den Main-Task anlegt
und seinen Stack löscht. Das Panel baut eine Kopie der Komponente freertos,
in der diese Schleife etwa alle 10us einmal liest; zwischen zwei Lesezugriffen
zählt sie in Registern 2000 herunter (`firmware/panel/core1_wait.cmake`).
Das Konfigurieren bricht ab, wenn die Schleife nicht genau einmal gefunden
wird.

| Image | Starts | Hänger vor `app_main()` |
| --- | ---: | ---: |
| Release 0.23.9, Schleife unverändert | 60 | 11 |
| Quelle von Release 0.23.9, Schleife etwa alle 10us | 60 | 0 |
| Quelle von Release 0.23.9, Schleife ohne Pause, nicht komprimiert | 60 | 0 |

Gemessen an einem Waveshare-ESP32-P4-Board (Chip-Revision 3.2) am
2026-11-08, abwechselnd in Blöcken zu 20 Starts im Abstand von 20s. Ein
Hänger endet nach `sleep_gpio: Enable automatic switching of GPIO sleep
configuration`: Kern 0 bleibt an einem Schreib- oder Lesezugriff im
internen Speicher hängen und der Watchdog setzt den Chip nach 10s zurück,
oder der Start endet in einem Store Access Fault. Bei jedem Hänger mit
gesicherten Programmzählern stand Kern 1 in der Schleife. Images mit der
unveränderten Schleife, einem komprimierten Paar `lw`/`beqz` in einem
16-Byte-Block, hingen in 18 von 24 erprobten Layouts; eine Schleife über
eine 16-Byte-Grenze hinweg hing in keinem von 9 (380 Starts). Die Layouts
wurden zu verschiedenen Stunden gemessen. Die Ursache in der Hardware ist
nicht bekannt.

Die SD-Karte und der C6 teilen sich den Anfrage-Mutex des SDMMC-Treibers
von ESP-IDF, der Priorität vererbt. Hält der Run-Log-Task auf Kern 1 ihn für
einen Schreibvorgang auf die Karte und wartet ein Task von esp_hosted
darauf, läuft der Run-Log-Task mit Priorität 23 auf Kern 1, über der
Control-Task. Die längste Periode der Control-Task unter dieser Last ist
nicht gemessen.

Speicher von esp_hosted, alles interner RAM:

| Teil | Byte | Quelle |
| --- | ---: | --- |
| statische Daten | 1 637 | esp_hosted 1 576, esp_wifi_remote 61 (Map-Datei) |
| Task-Stacks | 20 480 | 4 x 3 072 und 2 x 4 096 |
| SDIO-Pufferpool | 47 680 | 31 Blöcke zu 1 536 Byte, auf 64 Byte ausgerichtet (`sdio_drv.c` 246-262) |
| gemeinsamer Pufferpool | 38 464 | 25 Blöcke zu 1 536 Byte, auf 64 Byte ausgerichtet (`transport_drv.c` 298-316) |

Die beiden Pools belegen DMA-fähigen (Direct Memory Access) internen RAM
(`port_esp_hosted_host_os.c` 130-147) und werden vor `app_main()` angelegt,
auch mit Wi-Fi aus. `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM` würde sie ins
PSRAM legen; es ist aus, und der freie interne RAM damit ist nicht gemessen.
Weitere Speicherbelegungen von esp_hosted sind hier nicht erfasst. Die
Konsolenzeile `wifi: radio up` nennt den freien internen RAM mit laufendem
Funkteil.

## Bluetooth LE über den ESP32-C6

Der C6 betreibt den BLE-Controller (Bluetooth Low Energy), der P4 den
BLE-Host, NimBLE. esp_hosted überträgt die HCI-Pakete (Host Controller
Interface) zwischen beiden als VHCI (virtuelles HCI) über die SDIO-Verbindung
des Wi-Fi, Schnittstelle `ESP_HCI_IF`: keine zusätzlichen Pins.
`firmware/panel/sdkconfig.defaults` enthält die Konfiguration unter
"Bluetooth LE through the ESP32-C6": Central und Observer, 5 Verbindungen,
kein Security Manager, keine Funktionen von Bluetooth 5.0, der Speicher von
NimBLE im PSRAM, sein Host-Task auf Kern 0.

`firmware/panel/main/panel_ble.c` startet den Host einmal, mit
`panel_ble_start()`, in einem eigenen Task, `blestart`, und der Host bleibt
bis zum Neustart des Panels. Der Task `blec` ruft es beim ersten IMU EIN auf
([Die Bluetooth-IMU](IMU-de.md)), `ble scan` des Konsolen-Builds ([Die
Prüfstandskonsole](BenchConsole-de.md)) ebenso. Die Schritte und ihre
Zeilen auf der Konsole:

1. `c6_link_up()`, geteilt mit dem Wi-Fi: wer von beiden zuerst kommt, setzt
   den C6 zurück und startet ihn. Eine gescheiterte Verbindung beendet den
   Start: `ble: no link to the C6: no Bluetooth until the panel restarts`.
2. Die Controller-Anfragen an den C6, `esp_hosted_bt_controller_init()` und
   `_enable()`. esp_hosted ab 2.5.2 auf dem C6 lässt den Controller aus, bis
   der Host fragt. Ältere Firmware kennt diese Anfragen nicht
   (FeatureControl, 2.5.2 laut `docs/implemented_rpcs.md` von esp_hosted)
   und startet ihren Controller selbst. Die Versionsanfrage gibt es seit
   1.0.0, deshalb entscheidet der Start nach der Version des C6
   (`c6_link_version()`):
   - keine Version gemeldet, wie beim C6 des Boards mit 0.0.0, oder eine vor
     2.5.2: keine Anfragen, und `ble: C6 firmware version not reported:
     older than 2.5.2, no controller requests; its controller runs from its
     own start` (oder `ble: C6 firmware <x.y.z>: older than 2.5.2, ...`).
     Hat der Host dann binnen 3 000ms keinen Sync, schreibt der Start `ble:
     no sync within 3000ms without the controller requests: sending them`,
     schickt sie und wartet weitere 3 000ms;
   - 2.5.2 oder neuer: die Anfragen gehen hinaus. esp_hosted beendet eine
     unbeantwortete Anfrage nach 5s, und der Start schreibt eine von
     `ble: C6 controller init and enable answered in <n>ms and <n>ms`,
     `ble: C6 controller init not answered in <n>ms: a C6 firmware before
     2.5.2, its controller runs from its own start` (das Enable geht dann
     nicht hinaus) oder `ble: C6 controller init <error> in <n>ms, enable
     <error> in <n>ms: the host start goes on`.

   Die Versionsanfrage selbst kostet auf einem C6, der sie nicht
   beantwortet, einmal 1s (`c6_link_up()`). Auf dem C6 des Boards bleiben
   die Controller-Anfragen unbeantwortet: gesendet kosten sie 5 003ms
   (Station, 2026-11-05).
3. `nimble_port_init()` und der Host-Task `nimble_host`, dann das Warten auf
   den Sync des Hosts, seinen HCI-Reset und die Abfragen danach, die der C6
   beantwortet, höchstens 3 000ms (`PANEL_BLE_SYNC_MS`):
   - `ble: synced in <n>ms, own address type <n>`, die Zeit ab dem Start
     des Host-Tasks; oder
   - `ble: no sync within 3000ms: the C6 answers no HCI; the host keeps
     trying`. Ein späterer Sync schreibt seine eigene Zeile `synced in` mit
     der längeren Zeit, und Bluetooth LE ist ab dann bereit.
4. `ble: host started; internal RAM <n>B free before, <n>B after; PSRAM
   <n>B before, <n>B after`, und `blestart` endet.

Ein späterer Reset des Hosts, bei einem HCI-Fehler oder einem C6, der
verstummt, schreibt `ble: host reset, reason <n>`; NimBLE synchronisiert
von selbst neu und schreibt `ble: synced again`. Die Events aus den
Callbacks des Hosts gehen in eine FreeRTOS-Queue mit 64 Elementen zu 64
Bytes (`panel_ble.h`); eine volle Queue verwirft das Event und zählt es.

Jeder Start des Hosts schreibt `NimBLE: Failed to persist local IRK
(rc=8)`. Der Host erzeugt bei seinem Start einen zufälligen IRK (Identity
Resolving Key) und gibt ihn dem Bond-Speicher (`ble_hs_pvcy.c`). Das Panel
konfiguriert keinen Speicher: kein Security Manager, kein Bonding. Das
Schreiben antwortet `BLE_HS_ENOTSUP`, 8. Das Panel scannt und verbindet mit
seiner öffentlichen Adresse und löst keine privaten Adressen auf; der
Schlüssel wird nie benutzt.

Auf dem C6 des Boards (Firmware 0.0.0, `capabilities: 0xd`) hat die Station
am 2026-11-05 gemessen: ein Sync 13ms nach dem Start des Host-Tasks,
interner RAM 264 307 Bytes frei vor dem Start und 259 155 danach (5 152
Bytes), PSRAM 25 994 264 und 25 969 088 (25 176 Bytes), zwei Scans mit 2
und 3 Berichten und keinem verworfenen Event, und der WLAN-Hotspot lief vor
und nach den Scans, mit 251 103 Bytes freiem internem RAM bei laufendem
Hotspot nach dem Start des Hosts. Dieser Start schickte noch die
Controller-Anfragen und wartete 5 003ms auf das unbeantwortete Init. Mit
eingeschaltetem BWT901CL erschien sein Advertising einmal je Scan von
10 000ms, mit -82dBm: `ble: adv 00:0C:BF:08:19:29 type 0 rssi -82dBm
evtype 0 data 02010A0303F018170948432D3036...`, Flags 0x0A, der
16-Bit-Dienst 0x18F0 und der Name `HC-06`. Der Sensor sendet etwa alle
256ms (am Handy abgelesen), der Duplikatfilter des Controllers hält die
Wiederholungen also zurück; für einen Scan, der Geräte auflistet, ist das
gewollt.

Aktuelle Einschränkungen:

- Der Transport-Start von NimBLE (`ble_transport_ll_init()` in
  `vhci_drv.c` von esp_hosted) hält das Panel mit einem Abort an, wenn die
  SDIO-Verbindung nicht steht. `panel_ble.c` startet NimBLE erst, nachdem
  `c6_link_up()` die Verbindung gemeldet hat; eine Verbindung, die zwischen
  dieser Antwort und dem Transport-Start ausfällt, löst den Abort aus.
- Die Firmware des ausgelieferten C6 meldet Version 0.0.0 und beantwortet
  weder die Versionsanfrage noch die Controller-Anfragen; ihr Controller
  antwortet auf HCI.
- Wi-Fi und Bluetooth LE teilen sich das eine 2,4-GHz-Funkteil des C6
  (Software-Koexistenz des C6) und den SDMMC-Controller mit der SD-Karte.
  Ihre Wirkung aufeinander ist nicht gemessen.
