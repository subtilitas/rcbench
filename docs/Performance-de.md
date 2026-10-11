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
frame            8,785     1098 KiB     28.8      19.5
frame-idle        1,243      155 KiB      4.1      39.0
held             4,711      589 KiB     15.5      39.0
sim              9,745     1218 KiB     32.0      19.5
throttle        10,508     1314 KiB     34.5      19.5
chrome          32,953     4119 KiB    108.2       7.8
overview           909      114 KiB      3.0      39.0
servo           15,390     1924 KiB     50.5      19.5
servo-grip        3,035      379 KiB     10.0      39.0
servo-current        4,056      507 KiB     13.3      39.0
supply           8,502     1063 KiB     27.9      19.5
supply-chrome       31,405     3926 KiB    103.1       7.8
analyser           855      107 KiB      2.8      39.0
logs               904      113 KiB      3.0      39.0
settings           844      106 KiB      2.8      39.0
battery            854      107 KiB      2.8      39.0
balance            846      106 KiB      2.8      39.0
programmer          874      109 KiB      2.9      39.0
balance-sim        2,376      297 KiB      7.8      39.0
settings-sim        2,384      298 KiB      7.8      39.0
battery-sim        2,380      298 KiB      7.8      39.0
analyser-chrome       39,184     4898 KiB    128.6       6.5
logs-chrome       16,020     2002 KiB     52.6      13.0
settings-chrome       23,595     2949 KiB     77.4       9.8
battery-chrome       36,937     4617 KiB    121.2       7.8
balance-chrome       40,650     5081 KiB    133.4       6.5
programmer-chrome       28,426     3553 KiB     93.3       9.8
picker             872      109 KiB      2.9      39.0
picker-chrome       15,842     1980 KiB     52.0      13.0
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
| `analyser`, `logs`, `settings`, `battery`, `balance`, `programmer`, `picker` | ein ruhiger Frame dieses Bildschirms, Chrome gecacht |
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
also auch bei einem Coprozessor, der mit simulierten Werten antwortet.
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
| die sieben Bildschirmmodi | 1 200 | einen Bildschirm, der neu zu zeichnen begonnen hat |
| die drei `-sim`-Modi | 2 800 | ein Watermark, das über die volle Canvas hinauswächst |
| die acht `-chrome`-Modi | 45 000 | ein wachsendes vollständiges Neuzeichnen |

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
| `main` | `main_task`, ruft `app_main` und betreibt die UI | 8 192 | 3 984 | 3 184 |
| `control` | `control_task` | 6 144 | 3 904 | 1 216 |
| `runlog` | `log_task` | 4 352 | 2 976 | 352 |
| `artkeep` | `art_keep_task` | 4 096 | 944 | 2 128 |
| `knob` | `knob_task`, der Leser des Drehknopfs | 3 072 | 1 360 | 688 |
| `touch` | `touch_task`, der GT911-Leser (`components/gt911`) | 4 096 | 1 808 | 1 264 |

Gemessen mit ESP-IDF v5.4 bei -O2. Von der Marge gehen 528 Bytes außerhalb
der Frames auf: 320 für den gesicherten Zustand von FPU (Floating-Point Unit)
und Vektoreinheit am oberen Ende jedes Stacks, 192 für den Frame, den ein
Interrupt ablegt, und 16 unter dem tiefsten Frame. Die übrigen 496 Bytes
decken ab, was das Werkzeug nicht sieht, und jede Tiefe oben ist deshalb eine
Untergrenze:

- Aufrufe über einen Funktionszeiger, außer den Aufrufen des Routers in einen
  Bildschirm, die das Werkzeug aus der Tabelle jedes Bildschirms liest: 176
  solche Aufrufe sind von `main_task` aus erreichbar, die meisten in den
  Speicher- und Display-Treibern von ESP-IDF;
- Aufrufe in das ROM (Read-Only Memory) des ESP32-S3, dessen Frames nicht in
  der ELF-Datei stehen;
- Rekursion, die das Werkzeug einmal zählt.

CI führt das Werkzeug auf dem Build mit ESP-IDF v5.4 mit `--check-doc` aus;
das schlägt fehl, wenn die Tabelle oben oder die Zahl der Aufrufe über einen
Zeiger von diesem Build abweicht.

### Der Koprozessor

`tools/stack_check.py --iomcu` liest das RP2350-Image auf dieselbe Weise.
Kern 0 führt `main()` auf dem Main-Stack des pico-sdk aus: 4096 Bytes,
`__StackBottom` bis `__StackTop` in der ELF-Datei, festgelegt durch
`PICO_STACK_SIZE=0x1000` in `firmware/iomcu/CMakeLists.txt`. Kern 1 führt
`core1_main()` auf dem 4096 Bytes großen Array aus, das sein Start übergibt.
Keiner der beiden Stacks hat eine Schutzzone. Ein Interrupt läuft auf dem
Stack des Kerns, den er unterbricht; jedem Kern werden deshalb seine tiefste
Kette und ein Interrupt angerechnet: ein Exception-Frame von 108 Bytes und
der tiefste Handler, den das Image installiert. CI führt das Werkzeug nach
dem Koprozessor-Build aus und schlägt fehl, wenn Kette, ein Interrupt und
eine Marge von 256 Bytes den Stack eines Kerns überschreiten.

| Kern | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | Ein Interrupt (Bytes) | Reserve unter der Marge (Bytes) |
| --- | --- | ---: | ---: | ---: | ---: |
| Kern 0 | `main` | 4 096 | 2 392 | 528 | 920 |
| Kern 1 | `core1_main` | 4 096 | 640 | 528 | 2 672 |

Gemessen am Image, das CI baut, pico-sdk 2.3.0 mit arm-none-eabi-gcc 13.2.1.
CI führt das Werkzeug mit `--check-doc` aus; das schlägt fehl, wenn die
Zeile von Kern 0 von diesem Build abweicht. Die Zeile von Kern 1 wird nicht
gehalten: ihre Kette ändert sich mit dem Compiler (ARM GNU 14.2 ergibt 644
Bytes), und CI baut das Image mit dem Compiler aus dem Paket des Runners.
Auch die Kette von Kern 0 ändert sich damit: ARM GNU 14.2 ergibt 2 416
Bytes, und die Frames unten stammen aus diesem Build.

Die tiefste Kette von Kern 0 ist ein Schreibkommando an den
KST-Programmierport: `main` (392 Bytes), `can_service` (344),
`link_dev_dispatch`, `kst_write`, `kst_port_write` (104),
`kst_session_write` (256) und der Schreibplaner aus `protocols/kst`, 1 272
Bytes ab `kst_plan_edit`, davon 640 für den Frame seines Suchschritts
`detour`. Der Plan und das bereitgestellte Image liegen im Zustand des
Ports, nicht auf dem Stack. Der tiefste Handler ist der Worker des USB-Stacks,
`low_priority_worker_irq`, mit 420 Bytes; auch seine Kette endet in
`panic()`.

Jede Tiefe ist eine Untergrenze:

- Aufrufe über ein Register werden nicht verfolgt, außer dem Lese- und dem
  Schreib-Handler einer Page, die das Werkzeug aus der Page-Tabelle des
  Links liest: 91 solche Aufrufe sind von `main` aus erreichbar;
- der Frame von 1 088 Bytes von `two_way_long_needle()` der newlib bleibt
  außen vor. `strstr()` ruft sie für ein Suchmuster ab 255 Zeichen auf, und
  die beiden Suchmuster der Firmware sind 3 und 5 Zeichen lang. Das Werkzeug
  schlägt bei einem `strstr()`-Aufruf unter `shared/` oder
  `firmware/iomcu/src` fehl, dessen Suchmuster kein Stringliteral unter 255
  Zeichen ist;
- 17 handgeschriebene Rechenroutinen des pico-sdk tragen in der ELF-Datei
  keine Größe, und ihre Frames werden nicht gelesen;
- gezählt wird ein Interrupt, kein zweiter auf dem ersten;
- Rekursion wird einmal gezählt.

Der Stack von Kern 0 ist der gesamte RAM-Bereich `SCRATCH_Y`, 0x20081000
bis 0x20082000; das Image legt nichts anderes hinein, und der Link schlägt
fehl, sobald etwas hineingelegt wird. Darunter, in `SCRATCH_X`, liegt das
4096 Bytes große Array, das das pico-sdk als eigenen Stack für Kern 1
reserviert und auf dem dieses Image nicht läuft. Dieselbe Definition
bestimmt die Größe dieses Arrays.

`-v` listet sie alle auf, und die tiefste Kette jeder Task.

Die UI hält ihre Frames klein, wo das wenig kostet. Der Zeichner einer Seite
ist eine eigene Funktion, sodass nur die angezeigte Seite ihre Puffer hält:
auf PROGRAMMER ist der Frame von `render()` 32 Bytes groß und der der größten
Seite 464. Eine Zeile, die zur Anzeige kopiert wird, wird ohne `snprintf()`
kopiert, das die Gleitkommawandlung von newlib erreicht: 1 952 Bytes tief im
Aufrufgraphen.
