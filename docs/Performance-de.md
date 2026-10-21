# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

Das Zeichenbudget für alle, die einen Bildschirm hinzufügen oder ändern.

## Die Randbedingung

Die Frame Rate dieses Panels begrenzt die Bandbreite des PSRAM (Pseudo-Static
Random-Access Memory), nicht die CPU (Central Processing Unit). Die
LCD-Einheit (Liquid-Crystal Display) liest ununterbrochen einen Framebuffer
mit etwa 30 MB/s aus dem PSRAM, und die Framebuffer liegen hinter einem
Write-Back-, Write-Allocate-Datencache (64 KB, 8-fach assoziativ,
64-Byte-Lines). Jedes Pixel, das die CPU schreibt, kostet einen
64-Byte-Line-Fill und ein 64-Byte-Write-Back, sofern die Line nicht schon
geladen ist. Die Kosten eines Frames sind deshalb eine Anzahl von
Cache-Line-Fills, und die lässt sich auf dem Host exakt messen.

`tools/frame_cost.py` baut die echten Bildschirme für den Host und lässt sie
unter cachegrind mit der Cache-Geometrie des ESP32-S3 laufen. Es meldet die
Differenz zwischen einem und elf gerenderten Frames, sodass Prozessstart und
das erste Füllen jedes Buffers herausfallen.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame            7,623      953 KiB     25.0      39.0
frame-idle        1,276      160 KiB      4.2      39.0
held             4,194      524 KiB     13.8      39.0
sim              8,639     1080 KiB     28.4      19.5
throttle         9,346     1168 KiB     30.7      19.5
chrome          31,564     3946 KiB    103.6       7.8
overview           956      120 KiB      3.1      39.0
servo           14,675     1834 KiB     48.2      19.5
servo-grip        2,998      375 KiB      9.8      39.0
servo-current        4,018      502 KiB     13.2      39.0
supply           7,749      969 KiB     25.4      39.0
supply-chrome       30,089     3761 KiB     98.8       9.8
analyser           869      109 KiB      2.9      39.0
logs               877      110 KiB      2.9      39.0
settings           869      109 KiB      2.9      39.0
settings-list          869      109 KiB      2.9      39.0
battery            885      111 KiB      2.9      39.0
balance            864      108 KiB      2.8      39.0
motor-stick        1,018      127 KiB      3.3      39.0
balance-sim        2,424      303 KiB      8.0      39.0
settings-sim        2,405      301 KiB      7.9      39.0
battery-sim        2,435      304 KiB      8.0      39.0
analyser-chrome       38,002     4750 KiB    124.7       7.8
logs-chrome       16,687     2086 KiB     54.8      13.0
settings-chrome       20,718     2590 KiB     68.0      13.0
settings-list-chrome       23,416     2927 KiB     76.9      13.0
battery-chrome       36,614     4577 KiB    120.2       7.8
balance-chrome       40,453     5057 KiB    132.8       6.5
motor-stick-chrome       31,464     3933 KiB    103.3       7.8
picker             902      113 KiB      3.0      39.0
picker-chrome       22,218     2777 KiB     72.9      13.0
wifi               873      109 KiB      2.9      39.0
wifi-chrome       13,770     1721 KiB     45.2      19.5
wifi-keys       23,050     2881 KiB     75.7      13.0
clear           12,006     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

| Modus | Was er misst |
| --- | --- |
| `frame` | der Motorprüfstand auf einem Frame, in dem ein Telemetriesample eintrifft |
| `frame-idle` | der Motorprüfstand auf einem Frame zwischen zwei Samples, ohne Berührung |
| `held` | der Motorprüfstand zwischen zwei Läufen: live Anzeigen über einem Plot, der den letzten hält |
| `sim` | wie `frame`, mit dem SIMULATION-Watermark |
| `throttle` | der Motorprüfstand mit einem Finger am Gas, der Drag-Fall |
| `chrome` | der Motorprüfstand ohne Cache, vollständig neu gezeichnet |
| `overview` | das Menü, Chrome gecacht |
| `servo` | der Servobildschirm mit neu gezeichnetem Arm |
| `servo-grip` | der Servobildschirm, nur der Griff neu gezeichnet |
| `servo-current` | wie `servo-grip`, mit dem INA3221 als Messgerät der Servo-Schiene und einem Fenster in jedem Frame, dessen Strom sich in den gezeigten Ziffern unterscheidet: der Griff, der Wert der Zeile STROM, die Zeile über dem Plot und der Plot neu gezeichnet, jedes auf sich selbst beschnitten |
| `supply` | der Netzteilbildschirm mit eingeschaltetem Ausgang, ein Sample in jedem Frame |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `motor-stick`, `picker`, `wifi` | ein ruhiger Frame dieses Bildschirms, Chrome gecacht; `settings` sind die Kacheln von SETUP, `motor-stick` ESC STICK offen auf MOTOR & ESC > PROGRAMMIEREN |
| `settings-list` | SETUP mit offener Liste PANEL, Chrome gecacht |
| `wifi-keys` | WI-FI mit offener WLAN-Tastatur, in jedem Frame eine Taste getippt |
| `<screen>-sim` | derselbe Bildschirm mit dem SIMULATION-Watermark |
| `<screen>-chrome` | derselbe Bildschirm, auf jedem Frame invalidiert |
| `clear` | ein Löschen des ganzen Bildschirms |
| `vlines` | siebzehn senkrechte Linien über die volle Höhe |
| `hlines` | dieselbe Pixelzahl als waagerechte Linien |

Die absoluten Zahlen verschieben sich zwischen Maschinen um einige Fills, weil
argv und die Umgebungsvariablen denselben Cache belegen wie der Framebuffer.
`frame_cost.py --check-doc` prüft die Tabelle deshalb mit einer Toleranz von
1 %.

## Regeln

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe
kosten 8 160 Fills; dieselbe Pixelzahl als waagerechte Linien kostet null,
weil jede Line vom vorigen Pixel noch geladen ist. Die Oberfläche ist in
waagerechte Bänder gegliedert, und eine senkrechte Trennlinie ist eine
bewusste Ausgabe.

**Das Chrome cachen.** Alles neu zu zeichnen kostet etwa das Dreißigfache des
eingeschwungenen Zustands. Jeder Bildschirm führt je Framebuffer eine
Bitmaske dessen, was er schon gezeichnet hat; dafür ist das Argument
`buffer_index` von `render()` da. Das Panel wechselt zwischen zwei Buffern;
ein Bildschirm, der nur den gerade gezeichneten Buffer invalidiert, lässt den
anderen einen Frame zurück, was als Flackern erscheint.

**Ein Stencil, das sich nicht bewegt, wird gecacht.** SIMULATION wird auf
jedem Frame gezeichnet, sobald die Prüfstandswerte `LINK_BN_SIMULATED` tragen,
also auch bei einem IOmcu, der mit simulierten Werten antwortet.
Gedrehter Text scannt seine gedrehte Bounding-Box, von Ecke zu Ecke also die
ganze Canvas, und rotiert und dividiert je Pixel, um die 3 439 Pixel zu
schreiben, die das Watermark bedeckt: 0,9 % der Canvas. `ui_watermark` nimmt
diese Punkte einmal auf und schreibt sie danach nur noch, mit 144 721
Instruktionen je Frame statt 8 412 078.

**Eine Zahl von Fills ist keine Zahl von Zyklen.** Die Tabelle oben misst
Cache-Line-Fills, und das Watermark kostet davon nur 1 586: es ist Arithmetik
je Pixel, kein Traffic. Es kostete das 58-Fache dessen, was die Tabelle nahelegte,
und die Tabelle konnte das nicht zeigen. Wo die gemessene Frame-Zeit eines Modus
über dem liegt, was seine Fills vorhersagen, erst Instruktionen zählen, dann der
Schätzung trauen.

**Ein Bedienelement, das sich jeden Frame bewegt, bekommt seinen eigenen
Zähler.** Ein Drag liefert eine Berührung je Frame. Die Buttons und das Gas
des Motorbildschirms teilten eine Revision, weshalb ein Drag jedes
Bedienelement daneben neu zeichnete. Das Gas hat jetzt seinen eigenen
und zeichnet die Box der Anzeige und die bemalte Region des Sliders neu:
Der Modus `throttle` in der Tabelle oben misst das gegen einen Sample-Frame.
Der Flip quantisiert auf ganze Panel-Frames: ein Drag liegt entweder
innerhalb von zwei davon oder wartet auf einen dritten, also 19,5 fps oder
13,0. Auf der Hardware mit 50,5 ms gemessen, nahm der alte Drag den dritten.

Zu löschen ist, was ein Widget bemalt, nicht was es einnimmt. Der Daumen des
Sliders und sein Schatten stehen 5 px oben und 7 px unten über den Track
hinaus, und die Hero-Ziffern sind 32 px hoch mit 3 px Schräge auf einer 30 px
hohen Zeile. Ein Clear in der Grösse des Tracks oder der Zeile lässt daher an
jeder Position, die der Finger passiert hat, einen Daumen und die Füsse der
Ziffern stehen. `ui_slider_painted_rect()` liefert die Region, statt sie den
Aufrufer herleiten zu lassen.

**Nur auf Frames zeichnen, die etwas zu zeichnen haben.** Samples kommen mit
20 Hz, das Panel zeichnet mit 39 Hz, also hat etwa jeder zweite Frame nichts
Neues. Der Prüfstandsbildschirm führt den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. `each_framebuffer_is_updated_independently` in `test_motor` hält die
Zähler je Buffer fest.

## Obergrenzen

Das Panel bewegt 976 KiB je Frame bei 39 Hz. Ein Frame, der das Doppelte
kostet, landet bei 19,5 fps (Frames pro Sekunde), also einem Frame je
20-Hz-Telemetriesample; schneller zu zeichnen würde identische Pixel neu
malen, langsamer würde Samples verlieren. CI (Continuous Integration) hält
jeden Modus an eine Obergrenze:

| Modi | Obergrenze (Fills) | Fängt |
| --- | ---: | --- |
| `frame`, `sim`, `supply` | 15 600 | einen Prüfstandsframe, der ein Telemetriesample überschreitet |
| `overview` | 2 000 | einen Bildschirm mit gecachtem Chrome, der neu zu zeichnen begonnen hat |
| `servo` | 17 000 | ein Wachsen der Arm- und Griffzeichnung |
| `servo-grip` | 4 000 | ein Atmen, das die ganze Karte neu zeichnet |
| `servo-current` | 4 500 | eine geänderte Ziffer des Stroms, die die ganze Karte neu zeichnet |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `motor-stick`, `picker`, `wifi` | 1 200 | einen Bildschirm, der neu zu zeichnen begonnen hat |
| `settings-list` | 1 200 | eine SETUP-Liste, die neu zu zeichnen begonnen hat |
| die drei `-sim`-Modi | 2 800 | ein Watermark, das über die volle Canvas hinauswächst |
| die zehn `-chrome`-Modi | 45 000 | ein wachsendes vollständiges Neuzeichnen |
| `wifi-keys` | 24 000 | eine Taste der WLAN-Tastatur, die mehr als den Bildschirm neu zeichnet |

`tools/check_docs.py` hält diese Tabelle an den `--max-lines`-Argumenten in
`.github/workflows/ci.yml`: eine abweichende Obergrenze, ein Modus, den CI
hält und der hier keine Zeile hat, und eine Zeile, die CI nicht ausführt,
schlagen jeweils fehl.

Braucht ein künftiger Bereich mehr Platz, sind die verbleibenden Hebel vom
gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu
clippen.

Die Logzeile `DRAW … WAIT …` des ESP32-S3, alle 300 Frames ausgegeben, ist
die Prüfung auf der Hardware: DRAW ist die Zeichenzeit, WAIT die Zeit, die
der Buffer-Wechsel blockiert hat. Ein gesunder Frame besteht überwiegend aus
WAIT.

## Stacks

Jede Task des Panels läuft auf einem festen Stack, und eine Aufrufkette, die
über sein Ende hinausläuft, startet das Panel neu. `tools/stack_check.py`
liest die tiefste Aufrufkette jeder Task aus der ELF-Datei (Executable and
Linkable Format) des Panels: der Frame jeder Funktion ist das
`entry a1, N`, mit dem sie beginnt, und die Tiefe ist die größte Summe der
Frames entlang einer Kette vom Einstiegspunkt der Task. Ein Sprung aus einer
Funktion heraus, die Form eines Tail Calls, zählt als Aufruf. Die Tasks sind
jedes `xTaskCreatePinnedToCore()` und `xTaskCreate()` in `firmware/panel`
und die Main-Task. CI (Continuous Integration) führt es nach beiden
Panel-Builds aus und schlägt fehl, wenn die Tiefe einer Task ihren Stack
abzüglich 1024 Bytes überschreitet.

| Task | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, ruft `app_main` und betreibt die UI | 8 192 | 5 184 | 1 984 |
| `control` | `control_task` | 6 144 | 4 512 | 608 |
| `runlog` | `log_task` | 6 656 | 4 576 | 1 056 |
| `knob` | `knob_task`, der Leser des Drehknopfs | 3 072 | 1 328 | 720 |
| `touch` | `touch_task`, der GT911-Leser (`components/gt911`) | 4 096 | 1 680 | 1 392 |
| `wifi_ctl` | `worker`, startet und stoppt den Funk und den Server der Seite (`components/wifi_access`) | 5 120 | 3 360 | 736 |
| `mdns` | `mdns_task`, der mDNS-Responder (Multicast Domain Name System), solange das Panel mit einem Heimnetz verbunden ist | 4 096 | 1 808 | 1 264 |
| `wifi_get` | `fetch_task`, eine Release-Suche oder ein Download von GitHub (`components/wifi_access`) | 8 192 | 2 832 | 4 336 |

Der HTTP-Server (Hypertext Transfer Protocol) der Seite läuft in einem Task
mit 8 192 Bytes, der WLAN-Treiber, der TCP/IP-Stack (Transmission Control
Protocol, Internet Protocol) und die Event-Loop in eigenen Tasks. ESP-IDF
legt sie an, deshalb stehen sie nicht in der Tabelle: Ihre Tiefe ist nicht
gemessen. Die Tiefe von `wifi_get` lässt den TLS-Handshake (Transport Layer
Security) und jedes Lesen darunter aus: esp_http_client erreicht seinen
Transport über einen Funktionszeiger. Sein Stack ist der des Servers der
Seite, der nie neben ihm läuft; die Tiefe des Handshakes ist nicht gemessen.

Die tiefste Kette von `runlog` bindet die SD-Karte ein: eine in
`BENCH/KST/CONF` gelistete Servodatei (`kst_store_preset()`), das Lesen des
Ordners mit `storage_walk()` und das erneute Einbinden einer Karte, die
nicht mehr antwortet (`storage_read_end()`, `esp_vfs_fat_sdspi_mount()`).
Das Werkzeug folgt jedem Aufruf, den das Einbinden machen kann, darunter
`f_mkfs()`, das ein Einbinden ohne Formatieren nie erreicht. Der Start
eines Laufs bindet die Karte ebenfalls auf diesem Task ein
(`storage_check()`). Die Kette braucht 4 576 Bytes; der Stack von 6 656
Bytes lässt 1 056 über der Marge von 1 024 Bytes.

Gemessen mit ESP-IDF v5.4 bei -O2. Von der Marge gehen 528 Bytes außerhalb
der Frames auf: 320 für den gesicherten Zustand von FPU (Floating-Point Unit)
und Vektoreinheit am oberen Ende jedes Stacks, 192 für den Frame, den ein
Interrupt ablegt, und 16 unter dem tiefsten Frame. Die übrigen 496 Bytes
decken ab, was das Werkzeug nicht sieht, und jede Tiefe oben ist deshalb eine
Untergrenze:

- Aufrufe über einen Funktionszeiger, außer den Aufrufen des Routers in einen
  Bildschirm, die das Werkzeug aus der Tabelle jedes Bildschirms liest, und
  den Callbacks des Link-Ports, den Flash-Callbacks des Zeichnungsspeichers,
  denen der Startprüfung nach einem Update, den Callbacks des Links des
  SERVO-Bildschirms in das Panel, den Hooks der Sitzungen, die
  SERVO > PROGRAMMIEREN und MOTOR & ESC > PROGRAMMIEREN öffnen, und den
  Kartenfunktionen, über die die Lauf-Logs, das Fehlerprotokoll und die
  Servodateien geschrieben werden, denen es nach Namen folgt
  (`PORT_CALLS`, `FLASH_CALLS`, `BOOT_CALLS`, `SERVO_CALLS`,
  `PROG_CALLS`, `TAKE_CALLS`, `CARD_CALLS`, `FAULT_CALLS`, `KST_CALLS`):
  240
  solche Aufrufe sind von `main_task` aus erreichbar, die meisten in den
  Speicher- und Display-Treibern von ESP-IDF;
- Aufrufe in das ROM (Read-Only Memory) des ESP32-S3, dessen Frames nicht in
  der ELF-Datei stehen;
- Rekursion: Eine Funktion, die schon in einer Kette steht, wird nicht
  noch einmal betreten; eine Kette läuft also höchstens einmal durch einen
  Zyklus.

CI führt das Werkzeug auf dem Build mit ESP-IDF v5.4 mit `--check-doc` aus;
das schlägt fehl, wenn die Tabelle oben oder die Zahl der Aufrufe über einen
Zeiger von diesem Build abweicht.

### Der IOmcu

`tools/stack_check.py --iomcu` liest das RP2350-Image auf dieselbe Weise.
Kern 0 führt `main()` auf dem Main-Stack des pico-sdk aus: 4096 Bytes,
`__StackBottom` bis `__StackTop` in der ELF-Datei, festgelegt durch
`PICO_STACK_SIZE=0x1000` in `firmware/iomcu/CMakeLists.txt`. Kern 1 führt
`core1_main()` auf dem 4096 Bytes großen Array aus, das sein Start übergibt.
Keiner der beiden Stacks hat eine Schutzzone. Ein Interrupt läuft auf dem
Stack des Kerns, den er unterbricht; jedem Kern werden deshalb seine tiefste
Kette und ein Interrupt angerechnet: ein Exception-Frame von 108 Bytes und
der tiefste Handler, den das Image installiert. CI führt das Werkzeug nach
dem IOmcu-Build aus und schlägt fehl, wenn Kette, ein Interrupt und
eine Marge von 256 Bytes den Stack eines Kerns überschreiten.

| Kern | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Ein Interrupt (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: | ---: |
| Kern 0 | `main` | 4 096 | 1 484 | 528 | 1 828 |
| Kern 1 | `core1_main` | 4 096 | 640 | 528 | 2 672 |

Gemessen am Image, das CI baut, pico-sdk 2.3.0 mit arm-none-eabi-gcc 13.2.1.
CI führt das Werkzeug mit `--check-doc` aus; das schlägt fehl, wenn die
Zeile von Kern 0 oder die Zahl der Aufrufe über ein Register weiter unten
von diesem Build abweicht. Die Zeile von Kern 1 wird nicht
gehalten: ihre Kette ändert sich mit dem Compiler (ARM GNU 14.2 ergibt 644
Bytes), und CI baut das Image mit dem Compiler aus dem Paket des Runners.
Die unten genannten Frames stammen aus einem Build mit ARM GNU 14.2, der
für Kern 0 1 508 Bytes ergibt.

Die tiefste Kette von Kern 0 ist eine Anfrage, die einen Ausgang bindet und
im pico-sdk scheitert: `main` (264 Bytes), `can_service` (344),
`link_dev_dispatch`, `slots_write`, `slots_take`, `ports_apply`,
`slots_rerate`, `outputs_hw_apply_only`, `out_dshot_bind`, die Belegung einer PIO-State-Machine (PIO: Programmable
Input/Output) im pico-sdk und `panic()` mit dem, was es zur Ausgabe
aufruft, 228 Bytes. Der tiefste Handler ist der Worker des USB-Stacks,
`low_priority_worker_irq`, mit 420 Bytes; auch seine Kette endet in
`panic()`.

Ein Schreibkommando an den Programmierport für KST- und Chaservo-Servos erreicht 1 104 Bytes: `main`
(264), `can_service` (344), `link_dev_dispatch` (32), `kst_write` (24),
`kst_port_write` (88), `kst_session_edit` (40) und der Start eines
Planer-Jobs, 312 Bytes ab `kst_plan_edit_begin`: `kst_plan_edit_begin`
(40), die Grenzwertprüfung `kst_limits_edit` (248), `load` (16) und
`kst_field_get` (8). Ein Durchlauf, der plant, erreicht 824 Bytes: `main`,
`kst_port_step` (48), `step_programming` (40), `kst_session_step` (264) und ein Slice des Planers,
208 Bytes ab `kst_plan_step`. Der Planer ruft sich nicht selbst auf. Der
Job des Planers, der Plan und das bereitgestellte Image liegen im Zustand
der Session und des Ports, nicht auf dem Stack.

Der Port plant einen Schreibauftrag in Slices. Das Schreibkommando prüft
den Auftrag und startet den Job des Planers, 64 us Planerarbeit. Jeder
Durchlauf der Hauptschleife von Kern 0 fährt dann 1 Slice in
`kst_session_step()`, 100 bis 109 Einheiten Arbeit: höchstens 892 us, der
längste gefundene 816 us. Das Planen endet nach 216 000 Einheiten,
höchstens 2 161 Durchläufen, mit einem Plan, ohne Plan oder mit dem
Ergebnis "too complex", wenn die Einheiten vorher verbraucht sind. Der
teuerste gefundene Auftrag braucht 120 136 Einheiten, höchstens 1 202
Durchläufe und 397 bis 647 ms Planerzeit. Alle Zeiten gelten für 150 MHz,
sind in einem Emulator aus den ausgeführten Instruktionen mit ARM GNU 14.2
bei -O3 berechnet und nicht auf Hardware gemessen. Die Zeit eines
Durchlaufs mit Link, Heartbeat und Ausgängen ist nicht gemessen. Die
Heartbeat-Lücke, die alle Ausgänge abschaltet, beträgt 150 ms.

Jede Tiefe ist eine Untergrenze:

- Aufrufe über ein Register werden nicht verfolgt, außer dem Lese- und dem
  Schreib-Handler einer Page, die das Werkzeug aus der Page-Tabelle des
  Links liest: 159 solche Aufrufe sind von `main` aus erreichbar, im Image,
  das CI baut (arm-none-eabi-gcc 13.2.1; ARM GNU 14.2 ergibt 165);
- der Frame von 1 088 Bytes von `two_way_long_needle()` der newlib bleibt
  außen vor. `strstr()` ruft sie für ein Suchmuster ab 255 Zeichen auf, und
  die beiden Suchmuster der Firmware sind 3 und 5 Zeichen lang. Das Werkzeug
  schlägt bei einem `strstr()`-Aufruf unter `shared/` oder
  `firmware/iomcu/src` fehl, dessen Suchmuster kein Stringliteral unter 255
  Zeichen ist;
- 17 handgeschriebene Rechenroutinen des pico-sdk tragen in der ELF-Datei
  keine Größe, und ihre Frames werden nicht gelesen;
- gezählt wird ein Interrupt, kein zweiter auf dem ersten;
- Rekursion: Eine Funktion, die schon in einer Kette steht, wird nicht
  noch einmal betreten; eine Kette läuft also höchstens einmal durch einen
  Zyklus.

Der Stack von Kern 0 ist der gesamte RAM-Bereich `SCRATCH_Y`, 0x20081000
bis 0x20082000; das Image legt nichts anderes hinein, und der Link schlägt
fehl, sobald etwas hineingelegt wird. Darunter, in `SCRATCH_X`, liegt das
4096 Bytes große Array, das das pico-sdk als eigenen Stack für Kern 1
reserviert und auf dem dieses Image nicht läuft. Dieselbe Definition
bestimmt die Größe dieses Arrays.

`-v` listet sie alle auf, und die tiefste Kette jeder Task.

Die UI hält ihre Frames klein, wo das wenig kostet. Der Zeichner einer Seite
ist eine eigene Funktion, sodass nur die angezeigte Seite ihre Puffer hält:
in ESC STICK ist der Frame von `esc_stick_panel_render()` 32 Bytes groß und
der der größten Seite 464. `render()` von MOTOR & ESC nimmt 48 Bytes und ruft
den Weg der Sitzung und den seines Körpers, 416 Bytes, als zwei Funktionen
auf, sodass die Seiten einer Sitzung nicht auf dem Frame des Körpers
liegen. Eine Zeile, die zur Anzeige kopiert wird, wird ohne `snprintf()`
kopiert, das die Gleitkommawandlung von newlib erreicht: 2 368 Bytes tief im
Aufrufgraphen.

## Interner RAM

Das WLAN braucht internen RAM (Random-Access Memory), für den PSRAM nicht
einspringen kann. `esp_wifi_init()` nimmt die statischen RX- und
TX-Puffer (Empfangen, Senden) des WLAN-Treibers: je 16 zu etwa 1 600
Bytes, 51 200 Bytes DMA-fähiger (Direct Memory Access) interner RAM.
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` macht die TX-Puffer statisch.
Passen sie nicht, startet das WLAN nicht, und WI-FI zeigt „WLAN konnte nicht starten.“.

`tools/ram_budget.py` liest den internen Heap, den ein Panel-Build übrig
lässt, und zieht ab, was das Panel zur Laufzeit belegt, dann, was jeder
WLAN-Modus dazu belegt. CI führt es nach beiden Panel-Builds aus und
schlägt fehl, wenn mit dem größten Modus weniger als 32 768 Bytes Heap
übrig bleiben: die Größe des Vorrats, den ESP-IDF zur Laufzeit für interne
und DMA-Belegungen zurückhält (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`).
Daraus nehmen die Sockets des WLANs, die Puffer der Hardware-Einheiten für
AES (Advanced Encryption Standard) und SHA (Secure Hash Algorithm) und eine
Kartenübertragung, solange das WLAN läuft.

| Posten | Was es ist | Bytes |
| --- | --- | ---: |
| `static` | interner RAM, den Code und Daten des Images belegen, bis `_heap_start` | 129 944 |
| `heap` | interner Heap beim Start: `_heap_start` bis 0x3FCE9710 und der Bereich des ROM (Read-Only Memory) bis 0x3FCF0000, den der Heap nach dem Start übernimmt | 296 040 |
| `start-up` | die Stacks und Kontrollblöcke der Tasks von ESP-IDF und des Panels, jede Queue und jede Semaphore, die beiden Bounce-Puffer des Displays und ein Zuschlag von 12 288 Bytes für die Treiber | 107 248 |
| `before` | übriger Heap mit laufendem Panel und ausgeschaltetem WLAN | 188 792 |
| `AP` | WLAN-HOTSPOT: das WLAN, der Server der Seite und seine Task | 98 724 |
| `JOIN` | mit einem Heimnetz verbunden: das von AP und der mDNS-Responder | 103 160 |
| `SCAN` | die Netzsuche: das WLAN allein | 86 096 |
| `FETCH` | eine Release-Suche oder ein Download: das WLAN, die Task `wifi_get` und ein Zuschlag von 4 096 Bytes für TLS | 98 724 |
| `left` | übriger Heap mit dem größten Modus | 85 632 |

Gelesen aus dem Build mit ESP-IDF v5.4. CI führt das Werkzeug auf diesem
Build mit `--check-doc` aus; das schlägt fehl, wenn die Tabelle von ihm
abweicht. `-v` listet jede Belegung und woher ihre Größe stammt.

Jede Zahl ist eine Schätzung. Der Stack einer Task, die Länge einer Queue
und die Zahl der WLAN-Puffer stammen aus dem Build und dem Quelltext; die
Größen des Kontrollblocks einer Task, des Kopfes einer Queue und eines
Queue-Eintrags sind `sizeof()`-Werte, mit GDB aus dem v5.4-Build gelesen.
Drei Zuschläge stammen aus nichts davon: 12 288 Bytes für die Belegungen
der Treiber beim Start, 16 384 Bytes für die Blöcke des WLAN-Treibers
neben seinen Puffern und seiner Task und 4 096 Bytes für TLS. Das Werkzeug
lässt den Fast-Speicher der RTC (Real-Time Clock) aus, etwa 8 KB Heap, der
nicht DMA-fähig ist. Der freie Heap auf einem Panel ist nicht gemessen. Das
Panel gibt ihn beim Start aus:

```text
I (…) rcbench: internal RAM: <n> B free, largest block <n> B; PSRAM: <n> B free
```

Ein WLAN-Start, der fehlschlägt, protokolliert den Schritt, den Namen des
Fehlers und den internen Heap in diesem Moment:

```text
E (…) wifi: esp_wifi_init: ESP_ERR_NO_MEM; internal RAM <n> B free, largest block <n> B
```

Die Schritte sind `esp_netif_init`, `esp_event_loop_create_default`, die
beiden Event-Handler, die Netzwerkschnittstelle, `esp_wifi_init`,
`esp_wifi_set_mode`, `esp_wifi_set_config`, `esp_wifi_start`,
`httpd_start` und das Anlegen der Tasks `wifi_ctl`, `wifi_get` und `mdns`.
Eine Netzwerkschnittstelle, die ESP-IDF nicht belegen kann, hält das Panel
stattdessen mit einer Assertion in ESP-IDF an, und das Panel startet neu.

Was stattdessen im PSRAM liegt: die nullinitialisierten Daten (.bss) von 25
Objekten, 164 024 Bytes, benannt in `firmware/panel/main/psram_bss.lf`, und
über `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` nach der Auswahl von
ESP-IDF selbst die .bss von lwIP (Lightweight IP Stack), net80211, pp und
wpa_supplicant, 13 742 Bytes.

| Objekt | Komponente | .bss (Bytes) |
| --- | --- | ---: |
| `log_viewer_screen` | ui | 35 984 |
| `servo_screen` | ui | 29 517 |
| `ui_watermark` | ui | 20 504 |
| `supply_screen` | ui | 20 508 |
| `motor_screen` | ui | 20 088 |
| `kst_card` | bench | 6 208 |
| `esc_stick_panel` | ui | 5 832 |
| `kst_panel` | ui | 4 253 |
| `log_csv` | logfile | 4 904 |
| `esc_registry` | esc | 4 228 |
| `analyser_screen` | ui | 4 036 |
| `wa_zip` | webaccess | 2 433 |
| `wa_page` | webaccess | 1 408 |
| `wifi_screen` | ui | 1 320 |
| `ui_band` | ui | 1 040 |
| `kst_panel_draw` | ui | 932 |
| `esc_stick_pages` | ui | 252 |
| `esc_stick_draw_run` | ui | 128 |
| `motor_settings_page` | ui | 128 |
| `motor_workbench` | ui | 124 |
| `esc_stick_list` | ui | 96 |
| `motor_program_page` | ui | 44 |
| `esc_stick_draw` | ui | 40 |
| `servo_screen_link` | ui | 16 |
| `log_reader` | logfile | 1 |

Gelesen aus der Map-Datei des Builds mit ESP-IDF v5.4. CI führt
`tools/ram_budget.py --check-doc` auf diesem Build aus; das schlägt fehl,
wenn eine Zeile, die Anzahl, die Summe oder die 13 742 Bytes von ESP-IDF
von der Map abweichen oder eine Zeile doppelt steht. Es schlägt auch fehl
bei einer Zeile in `psram_bss.lf`, die kein Eintrag `object (extram_bss)`
unter einem Kopf `[mapping:...]` ist, bei einem Objekt, das das Fragment
nennt und von dem die Map keine .bss platziert, und bei einem Objekt im
PSRAM, das das Fragment nicht nennt, außerhalb von lwIP, net80211, pp und
wpa_supplicant. `-v` listet jedes Objekt.

Ein Objekt zieht nur um, wenn seinen Zustand

- kein Interrupt-Handler liest oder schreibt: die IRAM-Handler (Instruction
  RAM) des Panels sind die von `display.c` und `touch.c`, und ihr Zustand
  bleibt intern;
- kein DMA liest oder schreibt: der SPI-Treiber (Serial Peripheral
  Interface) der Karte kopiert einen Puffer, der nicht DMA-fähig ist, über
  einen, der es ist;
- kein Atomic, kein Spinlock, kein Mutex und keine Queue enthält: der
  Compare-and-Swap-Befehl des ESP32-S3 arbeitet nur auf internem RAM.

Ein Flash-Schreibvorgang schließt den Cache und mit ihm den PSRAM. Für
seine Dauer läuft die schreibende Task aus dem IRAM, und der andere Kern
wartet im IRAM; kein Code, der diese Objekte liest, läuft.

Der Zustand der Bildschirme wird über denselben 64-KB-Datencache gelesen
wie die Framebuffer. `tools/frame_cost.py` bildet die Frames, die es zeichnet,
mit jedem Lesen und Schreiben über diesen Cache nach, das dieser Objekte
eingeschlossen. Die Frame Rate auf einem Panel mit ihnen im PSRAM ist nicht
gemessen.

## WLAN-Downloads

Eine Kartendatei oder die ZIP-Datei geht über eine TCP-Verbindung
(Transmission Control Protocol) vom Panel zum Browser. Der Server der
Seite beantwortet eine Anfrage nach der anderen: Eine Anfrage, die während
eines Downloads kommt, wartet auf dessen Ende. Die Rate eines Downloads ist
nicht gemessen. Was sie begrenzt:

| Glied | Einstellung | Grenze |
| --- | --- | --- |
| Power Save | keiner, solange das Panel einem Heimnetz beigetreten ist (`esp_wifi_set_ps(WIFI_PS_NONE)`); ein Access Point schläft nicht | keine |
| Sendepuffer | 11 520 Bytes, 8 Segmente zu 1 440 Bytes (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`) | 11 520 Bytes pro Umlauf: 1 152 000 B/s bei 10 ms Umlaufzeit, 576 000 B/s bei 20 ms |
| Karte | SPI (Serial Peripheral Interface) mit 20 MHz, 2 500 000 B/s auf der Leitung; jedes Stück von 4 096 Bytes als 8 einzelne Sektoren zu 512 Bytes gelesen | nicht gemessen |
| Prozessor | die Task des Servers mit Priorität 1 auf Kern 0, neben der UI-Schleife (User Interface) mit Priorität 1; die SPI-Übertragungen der Karte warten durch Abfragen (Polling) | nicht gemessen |

Der Power Save, den ESP-IDF vorgibt, `WIFI_PS_MIN_MODEM`, weckt das Funkteil
nur zu jedem DTIM-Beacon (Delivery Traffic Indication Message). Bis dahin
hält der Access Point die Frames für das Panel zurück, die Bestätigungen
von TCP darunter, also wartet jeder Sendepuffer bis zu einer DTIM-Periode:
102,4 ms beim üblichen Beacon-Intervall und einer DTIM-Periode von 1,
307,2 ms bei einer DTIM-Periode von 3. Mit dem Sendepuffer von 5 760 Bytes,
den ESP-IDF vorgibt, sind das 18 750 bis 56 250 B/s. Ohne Power Save
braucht das Funkteil mehr Strom, solange es läuft; wie viel mehr am Panel,
ist nicht gemessen.

Der Stückpuffer der Seite liegt im PSRAM, in den der Treiber der SD-Karte
(Secure Digital) nicht per DMA (Direct Memory Access) liest: ESP-IDF liest
ein solches Stück Sektor für Sektor über einen eigenen Puffer von
512 Bytes. Die erwartete Rate, aus den Grenzen oben, ist 300 000 bis
1 000 000 B/s in einem Heimnetz mit einer Umlaufzeit unter 20 ms. Während
ein Download läuft, nimmt die Task des Servers der UI-Schleife
Prozessorzeit; die Bildrate während eines Downloads ist nicht gemessen.

Ein Download endet vorzeitig, und die Verbindung wird zurückgesetzt (Reset),
wenn

- der Empfänger die Verbindung schließt: erkannt innerhalb von 250 ms
  (`WA_SEND_SLICE_MS`, `shared/webaccess/include/wa_send.h`);
- die Verbindung fehlschlägt;
- der Empfänger 30 s lang nichts annimmt (`WA_SEND_QUIET_MS`);
- das WLAN ausgeht: innerhalb von 250 ms;
- das Lesen der Karte fehlschlägt oder die Datei nicht die angekündigte
  Größe hat.

Der Browser hat dann weniger Bytes als angekündigt und meldet den Download
als fehlgeschlagen. Der Reset gibt die Bytes, die noch für die Verbindung
anstehen, sofort frei (`CONFIG_LWIP_SO_LINGER`); der Server nimmt die
nächste Anfrage an. Die Konsole protokolliert jede Datei:

```text
I (…) wifi: file: sent, 16777216 of 16777216 B read in <n> ms
W (…) wifi: file: receiver gone, <n> of 16777216 B read in <n> ms
```

Die anderen Enden lauten `nothing taken for 30 s`, `radio going off` und
`card read failed`.

### Einen Download messen

Auf einem Rechner im selben Heimnetz wie das Panel, mit `curl` und, unter
Linux, `iw`:

1. Eine Datei von 16 777 216 Bytes ins Wurzelverzeichnis der Karte
   kopieren: `head -c 16777216 /dev/urandom > RATE16M.BIN`.
2. Am Panel WI-FI öffnen, dem Heimnetz beitreten und den Zugangscode
   ausschalten. `<panel>` unten ist die Adresse, die WI-FI zeigt.
3. Die eigene Verbindung des Rechners notieren und seinen Power Save
   ausschalten: `iw dev wlan0 link`, `iw dev wlan0 get power_save`,
   `sudo iw dev wlan0 set power_save off`. Die Umlaufzeit notieren:
   `ping -c 20 <panel>`.
4. Die Rate, 3-mal:
   `curl -s -o /dev/null --max-time 600 -w '%{http_code} %{size_download} B %{time_total} s %{speed_download} B/s\n' http://<panel>/card/RATE16M.BIN`.
   Jeder Lauf antwortet `200 16777216 B`, und die Konsole protokolliert
   `file: sent, 16777216 of 16777216 B read in <n> ms`.
5. Ein Download, der mittendrin abbricht, und die nächste Anfrage:
   `curl -s -o /dev/null --max-time 5 http://<panel>/card/RATE16M.BIN; curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   Die zweite antwortet `200`; die Konsole protokolliert
   `file: receiver gone`.
6. Ein Empfänger, der nicht mehr liest: eine Verbindung, die die Datei
   anfragt und dann nichts liest,
   `python3 -c 'import socket,time; s=socket.create_connection(("<panel>",80)); s.sendall(b"GET /card/RATE16M.BIN HTTP/1.1\r\nHost: p\r\n\r\n"); time.sleep(60)' &`,
   dann nach 35 s
   `curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   Die Konsole protokolliert `file: nothing taken for 30 s`; die
   Anfrage antwortet `200`.

## Das Dateisystem der SD-Karte

Das Panel liest FAT12, FAT16, FAT32 und exFAT (extended File Allocation
Table) über FatFs, die Dateisystem-Bibliothek in ESP-IDF. ESP-IDF v5.4 und
v5.5 bauen FatFs ohne exFAT, und keine Kconfig-Option schaltet es ein. Der
Panel-Build nimmt eine Kopie der Komponente `fatfs` von ESP-IDF mit
`FF_FS_EXFAT` auf 1 (`firmware/panel/fatfs_exfat.cmake`). Lange Dateinamen
bleiben, wie `sdkconfig.defaults` sie setzt: an, 255 Zeichen, der
Arbeitspuffer auf dem Heap.

Was exFAT kostet, gelesen aus dem Build mit ESP-IDF v5.4 gegen denselben
Stand ohne:

| Posten | Ohne exFAT | Mit exFAT | Wo |
| --- | ---: | ---: | --- |
| Anwendungs-Image | 1 967 360 | 1 976 544 | Flash, `ota_0` fasst 4 063 232 |
| interner RAM, den das Image belegt (`static` oben) | 128 208 | 128 208 | intern |
| der Kontext des Mounts: `FATFS`, 4 offene Dateien (`FIL`) und der Zustand des VFS (Virtual File System) von ESP-IDF | 25 376 | 25 560 | Heap, zuerst PSRAM |
| der Arbeitspuffer für lange Namen, für einen FatFs-Aufruf belegt | 512 | 1 120 | Heap, zuerst PSRAM |
| der Puffer eines Verzeichnisdurchlaufs (`storage_walk()`) | 328 | 368 | Heap, intern |
| `FILINFO`, ein Eintrag, wie FatFs ihn liest, im Puffer von `storage_stat()` neben dem Pfad, für einen Aufruf | 280 | 288 | Heap, intern |
| .bss von `log_viewer_screen`: 48 Listenzeilen mit 64-Bit-Größen | 35 576 | 35 968 | PSRAM |

Alle Werte sind Bytes. Die Heap-Zeilen sind `sizeof()`-Werte, mit GDB aus
den beiden Builds gelesen; der Heap zur Laufzeit ist nicht gemessen. Die
Stack-Tiefen der Tabelle unter [Stacks](#stacks) sind in beiden Builds
gleich: FatFs legt seinen Puffer für lange Namen auf den Heap, nicht auf
den Stack des aufrufenden Tasks.

Die Größe einer Datei ist von FatFs bis zum Bildschirm 64 Bit breit. stat()
über das VFS liefert die Größe in `st_size`, einem `off_t` von 32 Bit auf
dem Panel; jede Größe, die das Panel verwendet, kommt deshalb aus
`f_stat()` oder `f_readdir()` von FatFs.

`FF_LBA64` (64-Bit-Sektornummern) bleibt 0. Es ist nötig für eine Karte mit
GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier) und für ein Volume über 2 TiB.
Damit wächst das Image um weitere 4 768 Bytes, `FATFS` von 4 160 auf 4 184
Bytes und `FIL` von 4 176 auf 4 192 Bytes.
