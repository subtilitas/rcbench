# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

Das Zeichenbudget für alle, die einen Bildschirm hinzufügen oder ändern.

## Die Randbedingung

Die Frame Rate dieses Panels begrenzt die Bandbreite des PSRAM (Pseudo-Static
Random-Access Memory), nicht die CPU (Central Processing Unit). Der ESP32-P4
zeichnet in einen von zwei DPI-Framebuffern (Display Pixel Interface) zu
1 228 800 Bytes im PSRAM, während die DPI den anderen ausgibt: 1024 x 600 px
in RGB565 mit 60,38 Hz, 74 MB/s Lesezugriffe. Die CPU erreicht die Buffer über
zwei Write-Back-Caches: den L1-Datencache (64 KB, 2-fach assoziativ,
64-Byte-Lines) und den L2-Cache (128 KB, 8-fach assoziativ, 64-Byte-Lines).
Jede Line, die der L2 nicht hält, kostet einen 64-Byte-Fill aus dem PSRAM und,
sobald sie beschrieben ist, ein 64-Byte-Write-Back. Die Kosten eines Frames
sind deshalb eine Anzahl von L2-Fills, und die lässt sich auf dem Host exakt
messen.

`tools/frame_cost.py` baut die echten Bildschirme für den Host und lässt sie
unter cachegrind mit den beiden Caches des ESP32-P4 laufen: den L1 als erste
Stufe von cachegrind, den L2 als letzte, bemessen nach
`firmware/panel/sdkconfig.defaults`. Es meldet die Differenz zwischen einem
und elf gerenderten Frames, sodass Prozessstart und das erste Füllen jedes
Buffers herausfallen. Das Modell schickt jeden Datenzugriff durch beide
Caches, auch die des Stacks, und bildet kein Laden von Instruktionen nach: der
Host führt x86-Code aus.

Was ein Fill an Zeit kostet, ist die effektive Bandbreite der Schreibzugriffe
der CPU in den PSRAM, während die DPI liest. Sie ist nicht gemessen; die
Tabelle gibt deshalb Fills und Traffic und keine Zeit. Das Panel misst sie
beim Start ([Auf dem Board](#auf-dem-board)); `PROBE_CLEAR_US` im Werkzeug
nimmt diese Zahl auf, und die Tabelle gibt dann für jeden Modus eine
geschätzte Zeit und Frame Rate.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 60.4 Hz, 64-byte lines; effective bandwidth not measured

mode                   lines/frame     traffic
----------------------------------------------
frame                       11,501    1438 KiB
frame-idle                     765      96 KiB
held                         7,311     914 KiB
sim                         12,950    1619 KiB
throttle                    13,314    1664 KiB
chrome                      51,504    6438 KiB
overview                       878     110 KiB
servo                       25,759    3220 KiB
servo-grip                   4,750     594 KiB
servo-current                7,866     983 KiB
supply                      10,737    1342 KiB
supply-chrome               45,636    5704 KiB
analyser                       603      75 KiB
logs                           476      60 KiB
settings                       587      73 KiB
settings-list                  586      73 KiB
battery                        598      75 KiB
balance                        624      78 KiB
motor-stick                  3,316     414 KiB
balance-sim                  3,085     386 KiB
settings-sim                 3,072     384 KiB
battery-sim                  3,064     383 KiB
analyser-chrome             59,443    7430 KiB
logs-chrome                 23,811    2976 KiB
settings-chrome             35,827    4478 KiB
settings-list-chrome        37,175    4647 KiB
battery-chrome              58,264    7283 KiB
balance-chrome              61,597    7700 KiB
motor-stick-chrome          41,809    5226 KiB
picker                         753      94 KiB
picker-chrome               43,609    5451 KiB
wifi                           515      64 KiB
wifi-chrome                 20,894    2612 KiB
wifi-keys                   36,372    4546 KiB
clear                       19,207    2401 KiB
vlines                      10,202    1275 KiB
hlines                         180      22 KiB

est. ms and est. fps: not measured
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
2 %, mindestens 100 Fills. Es schlägt auch fehl, wenn die Zeile, die die Rate
des Panels und die Bandbreite nennt, vom Profil des Werkzeugs abweicht.

## Regeln

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe
kosten 10 202 Fills; dieselbe Pixelzahl als waagerechte Linien kostet 180,
weil jede Line vom vorigen Pixel noch geladen ist. Die Oberfläche ist in
waagerechte Bänder gegliedert, und eine senkrechte Trennlinie ist eine
bewusste Ausgabe.

**Das Chrome cachen.** Alles neu zu zeichnen kostet das 40- bis 100-Fache
eines gecachten Frames desselben Bildschirms: 59 443 Fills für das
vollständige Neuzeichnen von RECEIVER gegen 603 für seinen ruhigen Frame. Jeder Bildschirm führt je Framebuffer eine
Bitmaske dessen, was er schon gezeichnet hat; dafür ist das Argument
`buffer_index` von `render()` da. Das Panel wechselt zwischen zwei Buffern;
ein Bildschirm, der nur den gerade gezeichneten Buffer invalidiert, lässt den
anderen einen Frame zurück, was als Flackern erscheint.

**Ein Stencil, das sich nicht bewegt, wird gecacht.** SIMULATION wird auf
jedem Frame gezeichnet, sobald die Prüfstandswerte `LINK_BN_SIMULATED` tragen,
also auch bei einem IOmcu, der mit simulierten Werten antwortet.
Gedrehter Text scannt seine gedrehte Bounding-Box, von Ecke zu Ecke also die
ganze Canvas, und rotiert und dividiert je Pixel, um die Pixel zu
schreiben, die das Watermark bedeckt: 3 439 auf einer Canvas von
800 x 480 px, 0,9 % davon, und 5 377 auf 1024 x 600 px. `ui_watermark` nimmt
diese Punkte einmal auf, in eine Tabelle mit 6 144 Plätzen, und schreibt sie
danach nur noch: bei 800 x 480 px mit 144 721 Instruktionen je Frame statt
8 412 078. Die Zahl bei 1024 x 600 px ist nicht gemessen.

**Eine Zahl von Fills ist keine Zahl von Zyklen.** Die Tabelle oben misst
Cache-Line-Fills, und das Watermark kostet davon nur 1 449, `sim` gegen
`frame`: seine Zeit ist Arithmetik je Pixel, kein Traffic, und die Tabelle
kann sie nicht zeigen. Wo die gemessene Frame-Zeit eines Modus
über dem liegt, was seine Fills vorhersagen, erst Instruktionen zählen, dann der
Schätzung trauen.

**Ein Bedienelement, das sich jeden Frame bewegt, bekommt seinen eigenen
Zähler.** Ein Drag liefert eine Berührung je Frame. Eine Revision, die es mit den
Bedienelementen daneben teilte, würde sie bei jeder Berührung alle neu
zeichnen. Das Gas des Motorbildschirms hat seine eigene und zeichnet die Box
der Anzeige und die bemalte Region des Sliders neu:
Der Modus `throttle` in der Tabelle oben misst das gegen einen Sample-Frame.
Der Flip quantisiert auf ganze Panel-Frames: 60,4, 30,2 oder 20,1 fps. Die
Frame-Zeit eines Drags auf dem Board ist nicht gemessen.

Zu löschen ist, was ein Widget bemalt, nicht was es einnimmt. Der Daumen des
Sliders und sein Schatten stehen 5 px oben und 7 px unten über den Track
hinaus, und die Hero-Ziffern sind 32 px hoch mit 3 px Schräge auf einer 30 px
hohen Zeile. Ein Clear in der Grösse des Tracks oder der Zeile lässt daher an
jeder Position, die der Finger passiert hat, einen Daumen und die Füsse der
Ziffern stehen. `ui_slider_painted_rect()` liefert die Region, statt sie den
Aufrufer herleiten zu lassen.

**Nur auf Frames zeichnen, die etwas zu zeichnen haben.** Samples kommen mit
20 Hz, das Panel zeichnet mit 60,38 Hz, also haben zwei von drei Frames nichts
Neues. Der Prüfstandsbildschirm führt den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. `each_framebuffer_is_updated_independently` in `test_motor_draw` hält die
Zähler je Buffer fest.

## Obergrenzen

CI (Continuous Integration) hält jeden Modus unten an eine Obergrenze von
Fills je Frame. Eine Obergrenze liegt 10 bis 35 % über der Zahl des Modus in
der Tabelle oben; sie fängt also eine Änderung, die das Modell mehr zeichnen
lässt, nicht einen Frame, der das Panel verpasst:

| Modi | Obergrenze (Fills) | Fängt |
| --- | ---: | --- |
| `overview` | 2 000 | einen Bildschirm mit gecachtem Chrome, der neu zu zeichnen begonnen hat |
| `servo` | 30 000 | ein Wachsen der Arm- und Griffzeichnung |
| `servo-grip` | 6 000 | ein Atmen, das die ganze Karte neu zeichnet |
| `servo-current` | 9 000 | eine geänderte Ziffer des Stroms, die die ganze Karte neu zeichnet |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `picker`, `wifi` | 1 000 | einen Bildschirm, der neu zu zeichnen begonnen hat |
| `settings-list` | 1 000 | eine SETUP-Liste, die neu zu zeichnen begonnen hat |
| `motor-stick` | 4 000 | ein Wachsen des ruhigen Frames von ESC STICK, 3 316 Fills in der Tabelle oben |
| die drei `-sim`-Modi | 3 600 | ein Watermark, das über die volle Canvas hinauswächst |
| die zehn `-chrome`-Modi | 68 000 | ein wachsendes vollständiges Neuzeichnen |
| `wifi-keys` | 40 000 | eine Taste der WLAN-Tastatur, die mehr als den Bildschirm neu zeichnet |

`tools/check_docs.py` hält diese Tabelle an den `--max-lines`-Argumenten in
`.github/workflows/ci.yml`: eine abweichende Obergrenze, ein Modus, den CI
hält und der hier keine Zeile hat, und eine Zeile, die CI nicht ausführt,
schlagen jeweils fehl.

Keine Obergrenze hält den Prüfstandsframe (`frame`, `sim`, `supply`) an ein
Telemetriesample. Ein Sample kommt alle 50 ms, jeden dritten Panel-Frame, und
ein Frame hält mit den Samples Schritt, solange sein Traffic bei der
effektiven Bandbreite in drei Panel-Frames passt. Diese Bandbreite ist nicht
gemessen, daher ist die Obergrenze nicht gesetzt.

Braucht ein künftiger Bereich mehr Platz, sind die verbleibenden Hebel vom
gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu
clippen.

## Auf dem Board

Die Konsole des Panels gibt alle 300 Frames zwei Zeilen aus, alle 5 s bei
60,38 Hz:

```text
I (…) rcbench: <n> fps  VSYNC <n> Hz  FLIP TIMEOUTS <n>  TOUCHLOST <panel>/<driver>
I (…) rcbench: frames <n> on <title> (screen <id>): DRAW <min>/<mean>/<max> us  WAIT <min>/<mean>/<max> us (min/mean/max)
```

VSYNC ist die Bildwiederholrate des Panels, wie das Panel sie zählt. DRAW ist
die Zeichenzeit eines Frames, WAIT die Zeit, die der Buffer-Wechsel danach
blockiert hat. Die zweite Zeile umfasst die Frames, die seit der Zeile davor
auf dem gezeigten Bildschirm gezeichnet wurden: weniger als 300, wenn der
Bildschirm dazwischen gewechselt hat. Ein gesunder Frame besteht überwiegend
aus WAIT; steigt DRAW, bis WAIT null erreicht, ist das Frame-Budget
aufgebraucht.

Einmal beim Start, vor dem ersten Frame des Splash, misst das Panel die Zeit
von vier Löschvorgängen des hinteren Buffers, nach einem, den es nicht misst:

```text
I (…) rcbench: PSRAM probe: 4 clears of 1228800 B in <n> us, <n> us each
```

Ein Löschen schreibt den Buffer einmal: 2 457 600 Bytes Traffic im Modell,
ein Fill und ein Write-Back je Line. Die Zeit eines Löschens, gesetzt als
`PROBE_CLEAR_US`, macht aus dem Traffic der Tabelle eine Zeit.

| Modus | Auf dem Panel | DRAW min (ms) | DRAW Mittel (ms) | DRAW max (ms) | WAIT Mittel (ms) | fps |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `clear` | die Messung beim Start; DRAW Mittel ist die Zeit eines Löschens | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `overview` | OVERVIEW, unberührt | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `frame` | MOTOR & ESC > LIVE während eines ESC-Laufs | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `sim` | MOTOR & ESC > LIVE mit den simulierten Werten des IOmcu | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `servo` | SERVO > LIVE während eines Sweeps | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `servo-grip` | SERVO > LIVE mit ruhendem Arm | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `supply` | SUPPLY mit eingeschaltetem Ausgang | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `analyser` | RECEIVER ohne angeschlossenen Empfänger | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `logs` | LOGS, die Dateiliste | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `settings` | die Kacheln von SETUP | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `settings-list` | die Liste von SETUP > PANEL | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `battery` | BATTERY | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `balance` | BALANCE | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `motor-stick` | ESC STICK offen auf MOTOR & ESC > PROGRAMMIEREN | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `picker` | PICK A PIN | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `wifi` | WI-FI mit eingeschaltetem WLAN-HOTSPOT | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |
| `wifi-keys` | WI-FI > HEIMNETZ WÄHLEN, die Passwort-Tastatur, jede Sekunde eine Taste getippt | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen | nicht gemessen |

`frame_cost.py --check-doc` liest diese Tabelle: ein Modus, den das Werkzeug
nicht zeichnet, schlägt fehl, und DRAW Mittel der Zeile `clear` und
`PROBE_CLEAR_US` sind zusammen gemessen oder gar nicht und stimmen auf
0,05 ms überein. Die übrigen Zahlen sind eine Aufzeichnung des Boards, die
keine Prüfung hält; Mittel und Extreme der Frames eines Bildschirms sind das,
woran die Schätzung des Modells für seinen Modus gemessen wird.

## Stacks

Jede Task des Panels läuft auf einem festen Stack, und eine Aufrufkette, die
über sein Ende hinausläuft, startet das Panel neu. `tools/stack_check.py`
liest die tiefste Aufrufkette jeder Task aus der ELF-Datei (Executable and
Linkable Format) des Panels. Der ESP32-P4 führt RISC-V-Code aus, den
`tools/stack_riscv.py` dekodiert: der Frame jeder Funktion ist der tiefste
Punkt, den ihr Stack Pointer erreicht (`addi sp, sp, -N` und seine
komprimierten Formen), und die Tiefe ist die größte Summe der Frames entlang
einer Kette vom Einstiegspunkt der Task. Ein Sprung aus einer Funktion
heraus, die Form eines Tail Calls, zählt als Aufruf. Die Tasks sind jedes
`xTaskCreatePinnedToCore()` und `xTaskCreate()` im eigenen Code des Panels
unter `firmware/panel`, ohne `managed_components/`, und die Main-Task. CI
führt das Werkzeug nach dem Panel-Build aus und schlägt fehl, wenn die Tiefe
einer Task ihren Stack abzüglich 1024 Bytes überschreitet.

| Task | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, ruft `app_main` und betreibt die UI | 8 192 | 5 424 | 1 744 |
| `control` | `control_task` | 6 144 | 4 688 | 432 |
| `runlog` | `log_task` | 6 656 | 4 336 | 1 296 |
| `knob` | `knob_task`, der Leser des Drehknopfs | 3 072 | 1 024 | 1 024 |
| `touch` | `touch_task`, der GT911-Leser (`components/gt911`) | 4 096 | 1 280 | 1 792 |
| `wifi_ctl` | `worker`, startet und stoppt den Funk und den Server der Seite (`components/wifi_access`) | 5 120 | 2 384 | 1 712 |
| `mdns` | `mdns_task`, der mDNS-Responder (Multicast Domain Name System), solange das Panel mit einem Heimnetz verbunden ist | 4 096 | 1 344 | 1 728 |
| `wifi_get` | `fetch_task`, eine Release-Suche oder ein Download von GitHub (`components/wifi_access`) | 8 192 | 3 040 | 4 128 |

Der HTTP-Server (Hypertext Transfer Protocol) der Seite läuft in einem Task
mit 8 192 Bytes, der TCP/IP-Stack (Transmission Control Protocol, Internet
Protocol), die Event-Loop und die Verbindung von esp_hosted zum ESP32-C6 in
eigenen Tasks. ESP-IDF und esp_hosted legen sie an, deshalb stehen sie nicht
in der Tabelle: Ihre Tiefe ist nicht gemessen, und die Konsole meldet, was
sie unbenutzt lassen (unten). Die Tiefe von `wifi_get` lässt den
TLS-Handshake (Transport Layer Security) und jedes Lesen darunter aus:
esp_http_client erreicht seinen Transport über einen Funktionszeiger. Sein
Stack ist der des Servers der Seite, der nie neben ihm läuft; die Tiefe des
Handshakes ist nicht gemessen.

Die tiefste Kette von `runlog` bindet die SD-Karte ein: eine in
`BENCH/KST/CONF` gelistete Servodatei (`kst_store_preset()`), das Lesen des
Ordners mit `storage_walk()`, das erneute Einbinden einer Karte, die nicht
mehr antwortet (`storage_read_end()`, `mount_locked()`), und das
`snprintf()` darunter. Das Werkzeug folgt jedem Aufruf, den das Einbinden
machen kann, darunter `f_mkfs()`, das ein Einbinden ohne Formatieren nie
erreicht. Der Start eines Laufs bindet die Karte ebenfalls auf diesem Task
ein (`storage_check()`). Die Kette braucht 4 336 Bytes; der Stack von 6 656
Bytes lässt 1 296 über der Marge von 1 024 Bytes.

Gemessen auf dem ESP32-P4-Build mit ESP-IDF v5.5.5 bei -O2. Von der Marge
gehen 560 Bytes außerhalb der Frames auf:

- 128 für die Register, die ein Interrupt auf dem Stack der Task sichert,
  bevor er auf den Interrupt-Stack wechselt;
- 32 für den Kopf des Sicherungsbereichs der Koprozessoren am oberen Ende
  jedes Stacks;
- 384 für die Kontexte, die der Port vom unteren Ende eines Stacks nimmt,
  sobald seine Task die Einheit zum ersten Mal benutzt: die FPU
  (Floating-Point Unit) 132, die Hardware-Schleifen 24, die Vektoreinheit
  216 und bis zu 12 für die Ausrichtung;
- 16 für den Canary von FreeRTOS am unteren Ende des Stacks.

Der Thread-Local Storage am oberen Ende jedes Stacks wird aus der ELF-Datei
gelesen und obendrauf gerechnet; der Build des Panels hat keinen. Die
übrigen 464 Bytes decken ab, was das Werkzeug nicht sieht, und jede Tiefe
oben ist deshalb eine Untergrenze:

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
  214
  solche Aufrufe sind von `main_task` aus erreichbar, die meisten in den
  Speicher- und Display-Treibern von ESP-IDF;
- Sprünge über ein Register: eine Sprungtabelle, die in ihrer Funktion
  bleibt, oder ein Tail Call über einen Zeiger, was die Dekodierung nicht
  unterscheiden kann;
- Aufrufe in das ROM (Read-Only Memory) des ESP32-P4, dessen Frames nicht in
  der ELF-Datei stehen;
- eine Funktion, die ihren Stack Pointer um einen erst zur Laufzeit
  bekannten Betrag verschiebt: ihr Frame ist der Teil, den die Konstanten
  ergeben;
- Rekursion: Eine Funktion, die schon in einer Kette steht, wird nicht
  noch einmal betreten; eine Kette läuft also höchstens einmal durch einen
  Zyklus.

CI führt das Werkzeug mit `--check-doc` aus; das schlägt fehl, wenn die
Tabelle oben oder die Zahl der Aufrufe über einen Zeiger vom Build abweicht.

### High-Water-Marks auf dem Board

Die Konsole meldet den Stack jeder Task, während das Panel läuft. Mit jedem
Paar von Frame-Zeilen bekommt jede Task, deren High-Water-Mark seit der
letzten Meldung gefallen ist, eine Zeile; die erste Meldung, 5 s nach dem
Start, listet jede Task, auch die von ESP-IDF und esp_hosted:

```text
I (…) rcbench: stack <task>: <n> B never used
```

Die Zahl ist die High-Water-Mark von FreeRTOS: die Bytes am unteren Ende des
Stacks, die seit dem Start der Task kein Frame beschrieben hat, ohne die
Kontexte der Koprozessoren. Sie umfasst nur die Pfade, die das Panel
durchlaufen hat, und begrenzt deshalb nichts; neben die tiefste Kette oben
gestellt, zeigt sie, was das Werkzeug nicht verfolgt.

| Task | Stack (Bytes) | Auf dem Board nie benutzt (Bytes) |
| --- | ---: | ---: |
| `main` | 8 192 | nicht gemessen |
| `control` | 6 144 | nicht gemessen |
| `runlog` | 6 656 | nicht gemessen |
| `knob` | 3 072 | nicht gemessen |
| `touch` | 4 096 | nicht gemessen |
| `wifi_ctl` | 5 120 | nicht gemessen |
| `mdns` | 4 096 | nicht gemessen |
| `wifi_get` | 8 192 | nicht gemessen |

Die Tasks von ESP-IDF und esp_hosted stehen nicht in der Tabelle: ihre Namen
und Zahlen kommen aus der Konsole eines Musters, und keine ist aufgezeichnet.

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
der der größten Seite 464. `mo_render()` von MOTOR & ESC nimmt 48 Bytes und ruft
den Weg der Sitzung und den seines Körpers, 416 Bytes, als zwei Funktionen
auf, sodass die Seiten einer Sitzung nicht auf dem Frame des Körpers
liegen. Eine Zeile, die zur Anzeige kopiert wird, wird ohne `snprintf()`
kopiert, das die Gleitkommawandlung von newlib erreicht: 2 368 Bytes tief im
Aufrufgraphen.

## Interner RAM

Der ESP32-P4 hat kein Funkmodul. Das WLAN läuft auf dem ESP32-C6 des
7-Zoll-Boards, den der P4 über SDIO (Secure Digital Input Output) mit
`esp_wifi_remote` und `esp_hosted` erreicht. Kein Werkzeug schätzt den
internen RAM, der zur Laufzeit frei ist: die Host-Seite des Funks belegt
ihre Puffer und Tasks zur Laufzeit, was die Map des Builds nicht zeigt. Das
Panel gibt die Zahlen stattdessen aus. Beim Start, mit allen Tasks des
Panels und ausgeschaltetem Funk:

```text
I (…) rcbench: internal RAM: <n> B free, largest block <n> B; PSRAM: <n> B free
```

Wenn der Funk läuft, mit dem Modus nummeriert wie in `wa_session.h`
(0 WLAN-HOTSPOT, 1 mit einem Heimnetz verbunden, 2 eine Netzsuche, 3 eine
Release-Suche oder ein Download):

```text
I (…) wifi: radio up, mode <n>; internal RAM <n> B free
```

Und mit den Frame-Zeilen, wenn die tiefste freie Zahl seit dem Start
gefallen ist:

```text
I (…) rcbench: internal RAM: <n> B free at the lowest, <n> B free now, largest block <n> B
```

| Wann | Frei (Bytes) | Tiefster Stand seit dem Start (Bytes) |
| --- | ---: | ---: |
| Start, Funk aus | nicht gemessen | nicht gemessen |
| Modus 0, WLAN-HOTSPOT | nicht gemessen | nicht gemessen |
| Modus 1, mit einem Heimnetz verbunden | nicht gemessen | nicht gemessen |
| Modus 2, eine Netzsuche | nicht gemessen | nicht gemessen |
| Modus 3, eine Release-Suche | nicht gemessen | nicht gemessen |

Freier PSRAM beim Start: nicht gemessen.

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

`firmware/panel/main/psram_bss.lf` legt die nullinitialisierten Daten (.bss)
von 29 Objekten in den PSRAM (Pseudo-Static Random-Access Memory): den
Zustand der Bildschirme und der Helfer für Karte, Seite, Einstellungen und
Ausgänge. Das Fragment nennt jedes Objekt und die Regeln, die es erfüllt.
`tools/psram_bss.py` liest die Linker-Map des Builds und lässt CI
fehlschlagen, wenn ein genanntes Objekt .bss im internen RAM behält.
Statische Zahlen des P4-Builds mit ESP-IDF v5.5.5, aus `idf.py size`:

| Posten | Mit dem Fragment | Ohne es |
| --- | ---: | ---: |
| .bss im internen RAM (DIRAM) | 30 000 | 201 144 |
| .bss im PSRAM (`.ext_ram.bss`) | 171 144 | 0 |
| interner RAM, den das Image belegt: .text, .data und .bss im DIRAM, von 655 360 | 127 474 | 298 618 |
| Anwendungs-Image, gebaut ohne `RCBENCH_IOMCU_BIN` (ohne IOmcu-Image) | 2 286 272 | 2 286 272 |

Alle Zahlen sind Bytes. Die PSRAM-Einträge von ESP-IDF für lwIP
(Lightweight IP Stack) und wpa_supplicant nutzen ein Schema, das nur das
Fragment von `esp_wifi` definiert, und der P4-Build bindet es nicht ein;
die 3 551 Bytes .bss von lwIP bleiben im internen RAM.

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
