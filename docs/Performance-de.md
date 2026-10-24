# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

Das Zeichenbudget für alle, die einen Bildschirm hinzufügen oder ändern.

## Die Randbedingung

Der DSI-Host (Display Serial Interface) des Panels gibt einen von zwei
Framebuffern zu 1024 x 600px mit 60,38Hz aus dem PSRAM (Pseudo-Static
Random-Access Memory) aus, etwa 74MB/s, per DMA (Direct Memory Access).
Die CPU (Central Processing Unit) zeichnet über die Caches des ESP32-P4 in
den anderen Buffer, und beide teilen sich die Bandbreite des PSRAM. Was ein
Frame auf dem ESP32-P4 kostet, in Cache-Line-Fills, in PSRAM-Traffic oder
in Zeit, ist nicht gemessen, und kein Profil seiner Caches bildet es nach
([Obergrenzen](#obergrenzen)).

`tools/frame_cost.py` baut die echten Bildschirme für den Host und lässt
sie unter cachegrind mit einem Modell des ESP32-S3-Panels laufen: ein
Datencache von 64KiB, 8-fach assoziativ, mit 64-Byte-Lines, eine Ausgabe
mit 39,0Hz und 39MB/s PSRAM-Durchsatz für die CPU, bei 800 x 480px. Es
meldet die Differenz zwischen einem und elf gerenderten Frames, sodass
Prozessstart und das erste Füllen jedes Buffers herausfallen. Der Renderer
zeichnet 1024 x 600px, und der Cache des Modells ist nicht der des
ESP32-P4, deshalb enthält diese Seite keine Tabelle seiner Ausgabe.

<!-- framecost:start -->
Keine Tabelle: die Kosten je Frame auf dem ESP32-P4-Panel sind nicht gemessen.
<!-- framecost:end -->

Die Modi des Werkzeugs:

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
`frame_cost.py --check-doc` prüft eine Tabelle deshalb mit einer Toleranz von
2%, und mindestens 100 Fills.

## Regeln

**Zeilenweise zeichnen.** Eine Zeile des Framebuffers liegt in
aufeinanderfolgenden Bytes, also füllt eine waagerechte Linie jede
Cache-Line einmal und eine senkrechte Linie eine je Pixel. Die Oberfläche
ist in waagerechte Bänder gegliedert, und eine senkrechte Trennlinie ist
eine bewusste Ausgabe. Was beides auf dem ESP32-P4 kostet, ist nicht
gemessen.

**Das Chrome cachen.** Alles neu zu zeichnen besucht jedes Pixel der
Canvas, 614 400 bei 1024 x 600px, wo ein ruhiger Frame besucht, was sich
geändert hat. Jeder Bildschirm führt je Framebuffer eine Bitmaske dessen,
was er schon gezeichnet hat; dafür ist das Argument `buffer_index` von
`render()` da. Das Panel wechselt zwischen zwei Buffern; ein Bildschirm,
der nur den gerade gezeichneten Buffer invalidiert, lässt den anderen einen
Frame zurück, was als Flackern erscheint.

**Ein Stencil, das sich nicht bewegt, wird gecacht.** SIMULATION wird auf
jedem Frame gezeichnet, sobald die Prüfstandswerte `LINK_BN_SIMULATED` tragen,
also auch bei einem IOmcu, der mit simulierten Werten antwortet.
Gedrehter Text scannt seine gedrehte Bounding-Box, von Ecke zu Ecke also die
ganze Canvas, und rotiert und dividiert je Pixel, um die Pixel zu
schreiben, die das Watermark bedeckt: 5 377 auf 1024 x 600px.
`ui_watermark` nimmt diese Punkte einmal auf, in eine Tabelle mit 6 144
Plätzen, und schreibt sie danach nur noch. Die Instruktionen je Frame bei
1024 x 600px, mit der Tabelle und ohne sie, sind nicht gemessen.

**Eine Zahl von Fills ist keine Zahl von Zyklen.** Ein Cache-Modell zählt
Cache-Line-Fills. Das Watermark schreibt wenige Pixel und rechnet je Pixel
viel, also sind seine Kosten Arithmetik, kein Traffic, und eine Zahl von
Fills zeigt sie nicht. Wo die gemessene Frame-Zeit eines Modus über dem
liegt, was seine Fills vorhersagen, erst Instruktionen zählen, dann der
Schätzung trauen.

**Ein Bedienelement, das sich jeden Frame bewegt, bekommt seinen eigenen
Zähler.** Ein Drag liefert eine Berührung je Frame. Das Gas des
Motorbildschirms führt eine eigene Revision, getrennt von den Buttons
daneben, und zeichnet die Box der Anzeige und die bemalte Region des
Sliders neu: der Modus `throttle` von `tools/frame_cost.py` misst das gegen
einen Sample-Frame. Der Flip quantisiert auf ganze Panel-Frames: ein Drag
liegt entweder innerhalb von zwei davon oder wartet auf einen dritten, bei
60,38Hz also 30,2fps oder 20,1fps. Die Frame-Zeit des Drags auf dem
ESP32-P4 ist nicht gemessen.

Zu löschen ist, was ein Widget bemalt, nicht was es einnimmt. Der Daumen des
Sliders und sein Schatten stehen 5px oben und 7px unten über den Track
hinaus, und die Hero-Ziffern sind 32px hoch mit 3px Schräge auf einer 30px
hohen Zeile. Ein Clear in der Grösse des Tracks oder der Zeile lässt daher an
jeder Position, die der Finger passiert hat, einen Daumen und die Füsse der
Ziffern stehen. `ui_slider_painted_rect()` liefert die Region, statt sie den
Aufrufer herleiten zu lassen.

**Nur auf Frames zeichnen, die etwas zu zeichnen haben.** Samples kommen
mit 20Hz, das Panel zeichnet mit 60,38Hz (gemessen, der VSYNC-Wert der
Konsole), also hat der größte Teil der Frames nichts Neues. Der DPI-Takt
(Display Pixel Interface) ist 48MHz, nicht die 52MHz, die `board_pins.h`
anfordert; bei 48MHz ergäbe sein Frame von 1354 x 636 Takten allein
55,74Hz. ESP-IDF hält die angeforderte Zeilendauer, indem es die Front
Porch der Bridge auf eine Zeile von 1250 Takten kürzt, also dauert ein
Frame so lange wie 1354 x 636 Takte bei 52MHz: 60,38Hz. Dass dies die Rate
bestimmt, ist aus dem Quelltext von ESP-IDF gelesen, nicht aus den
Registern. Der Prüfstandsbildschirm führt den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. `each_framebuffer_is_updated_independently` in `test_motor_draw` hält die
Zähler je Buffer fest.

**Ein geclippter Repaint läuft nur über das, was in seinem Clip liegt.** Ein
Clip begrenzt, was ein Primitiv schreibt, nicht was es rechnet. Die SERVO-Karte
zeichnet ihren atmenden Griff durch eine Box von 73px Seitenlänge neu und ein
bewegtes Horn durch Boxen von etwa 170px, jeweils indem sie die ganze Karte
unter dem Clip zeichnet. Deshalb überspringt der Rasterizer eine Glyphe oder
eine Siebensegment-Ziffer, deren Zelle ausserhalb des Clips liegt, läuft einen
Bogen nur in den Richtungen ab, in denen der Clip liegt, läuft Kreisscheibe,
Kapsel und Ring mit Antialiasing je Zeile nur über die Pixel, die sie dort
bedecken können, und rechnet den Winkel eines Teilrings je Pixel nur nahe
seinen Enden und seiner Blende. `test_gfx_clip` hält jede Abkürzung Pixel für
Pixel an der Schleife über die ganze Box fest.

**Das Band wird neu gezeichnet, wenn es sich ändert.** Ganz gezeichnet kostet
das Band 102 288 Pixelbesuche SAFE und 114 281 ARMED, mit rotem Chip und
Laufuhr. Der Router merkt sich je Framebuffer `ui_band_key()` des zuletzt
gezeichneten Bands und zeichnet es nur neu, wenn sich der Schlüssel bewegt: die
Laufuhr einmal je Sekunde, solange der Prüfstand ARMED ist, ein Chip, ein Druck
auf STOP oder auf den Home-Tag. Über einem simulierten Prüfstand wird es jeden
Frame gezeichnet, unter dem Watermark. `test_frame_work` zählt die Pixel, die
der Rasterizer je Frame besucht (`gfx_work()`, nur in die Host-Suite gebaut),
und hält ARMED in Ruhe bei SAFE in Ruhe plus dem Band einmal je Sekunde, eine
gehaltene Position bei SAFE plus sechs Lagen der Box des Griffs, ein gesweeptes
oder gezogenes Horn bei 140 000 Besuchen je Frame und MOTOR & ESC LIVE ARMED in
Ruhe bei 300 000.

## Obergrenzen

Kein CI-Schritt (Continuous Integration) hält einen Frame an eine Obergrenze in
Cache-Lines; `test_frame_work` hält Budgets in Pixelbesuchen, wie oben.
`tools/frame_cost.py` bildet den Cache und die Bandbreite des ESP32-S3 bei
800 x 480px nach. Der Host-Renderer zeichnet 1024 x 600px, daher meldet
`frame_cost.py --check-doc` die Tabelle als nicht geprüft, und
`--update-doc` schreibt nichts. Ein Profil der Caches des ESP32-P4 und die
Obergrenzen, die CI hält, brauchen Messungen auf dem Board.
`tools/check_docs.py` schlägt bei einer Obergrenzen-Tabelle auf dieser
Seite fehl, solange `.github/workflows/ci.yml` keine hält.

Braucht ein künftiger Bereich mehr Platz, sind die verbleibenden Hebel vom
gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu
clippen.

Die Logzeile `… fps  DRAW … us  WAIT … us  VSYNC … Hz` des Panels, alle
300 Frames ausgegeben (`panel_frame.c`), ist die Prüfung auf der Hardware: DRAW ist die Zeichenzeit, WAIT die Zeit, die
der Buffer-Wechsel blockiert hat. Ein gesunder Frame besteht überwiegend aus
WAIT.

## Stacks

Jede Task des Panels läuft auf einem festen Stack, und eine Aufrufkette, die
über sein Ende hinausläuft, startet das Panel neu. Die Tiefe der tiefsten
Kette jeder Task auf dem ESP32-P4 ist nicht gemessen, und kein CI-Schritt
prüft den Stack einer Panel-Task.

`tools/stack_check.py` liest die tiefste Aufrufkette jeder Task aus einer
ELF-Datei (Executable and Linkable Format) des Panels mit Xtensa-Code: der
Frame jeder Funktion ist das `entry a1, N`, mit dem sie beginnt, und die
Tiefe ist die größte Summe der Frames entlang einer Kette vom
Einstiegspunkt der Task. Das Werkzeug schlägt fehl, wenn die Tiefe einer Task
ihren Stack abzüglich 1024 Bytes überschreitet. Es hält beim Image des
ESP32-P4 an, das RISC-V-Code ist. Ein RISC-V-Dekoder im Werkzeug oder ein
Laufzeit-Log der High-Water-Marks aller Task-Stacks gibt dem ESP32-P4
seine Zahlen. Die Stacks des IOmcu prüft CI, wie unten beschrieben.

Die Tasks, die das Panel anlegt, aus `xTaskCreatePinnedToCore()` und
`xTaskCreate()` in `firmware/panel`, und die Main-Task:

| Task | Einstieg | Stack (Bytes) | Tiefste Kette |
| --- | --- | ---: | --- |
| `main` | `main_task`, ruft `app_main` und betreibt die UI | 8 192 | nicht gemessen |
| `control` | `control_task` | 6 144 | nicht gemessen |
| `runlog` | `log_task` | 6 656 | nicht gemessen |
| `knob` | `knob_task`, der Leser des Drehknopfs | 3 072 | nicht gemessen |
| `touch` | `touch_task`, der GT911-Leser (`components/gt911`) | 4 096 | nicht gemessen |
| `wifi_ctl` | `worker`, startet und stoppt den Funk und den Server der Seite (`components/wifi_access`) | 5 120 | nicht gemessen |
| `mdns` | `mdns_task`, der mDNS-Responder (Multicast Domain Name System), solange das Panel mit einem Heimnetz verbunden ist | 4 096 | nicht gemessen |
| `wifi_get` | `fetch_task`, eine Release-Suche oder ein Download von GitHub (`components/wifi_access`) | 8 192 | nicht gemessen |
| `cardjob` | `files_task`, die Kartenaufträge von FILES, gestartet bei der ersten Anfrage von FILES (`main/panel_files.c`) | 6 656 | nicht gemessen |

Der HTTP-Server (Hypertext Transfer Protocol) der Seite läuft in einem Task
mit 8 192 Bytes, der WLAN-Treiber, der TCP/IP-Stack (Transmission Control
Protocol, Internet Protocol) und die Event-Loop in eigenen Tasks. ESP-IDF
legt sie an, und auch ihre Tiefe ist nicht gemessen.

`runlog` bindet die SD-Karte ein: beim Start eines Laufs
(`storage_check()`) und erneut für eine Karte, die nicht mehr antwortet
(`storage_read_end()`, `esp_vfs_fat_sdmmc_mount()`). Sein Stack hält neben
dieser Kette eine Zeile eines Servotests, `SERVO_TEST_LINE_MAX` Bytes.

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
den Auftrag und startet den Job des Planers, 64us Planerarbeit. Jeder
Durchlauf der Hauptschleife von Kern 0 fährt dann 1 Slice in
`kst_session_step()`, 100 bis 109 Einheiten Arbeit: höchstens 892us, der
längste gefundene 816us. Das Planen endet nach 216 000 Einheiten,
höchstens 2 161 Durchläufen, mit einem Plan, ohne Plan oder mit dem
Ergebnis "too complex", wenn die Einheiten vorher verbraucht sind. Der
teuerste gefundene Auftrag braucht 120 136 Einheiten, höchstens 1 202
Durchläufe und 397 bis 647ms Planerzeit. Alle Zeiten gelten für 150MHz,
sind in einem Emulator aus den ausgeführten Instruktionen mit ARM GNU 14.2
bei -O3 berechnet und nicht auf Hardware gemessen. Die Zeit eines
Durchlaufs mit Link, Heartbeat und Ausgängen ist nicht gemessen. Die
Heartbeat-Lücke, die alle Ausgänge abschaltet, beträgt 150ms.

Jede Tiefe ist eine Untergrenze:

- Aufrufe über ein Register werden nicht verfolgt, außer dem Lese- und dem
  Schreib-Handler einer Page, die das Werkzeug aus der Page-Tabelle des
  Links liest: 161 solche Aufrufe sind von `main` aus erreichbar, im Image,
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

Die UI des Panels hält ihre Frames klein, wo das wenig kostet. Der
Zeichner einer Seite ist eine eigene Funktion, sodass nur die angezeigte
Seite ihre Puffer hält: in ESC STICK ruft `esc_stick_panel_render()` die
angezeigte Seite auf, und `mo_render()` von MOTOR & ESC ruft den Weg der
Sitzung und den seines Körpers als zwei Funktionen auf, sodass die Seiten
einer Sitzung nicht auf dem Frame des Körpers liegen. Eine Zeile, die zur
Anzeige kopiert wird, wird ohne `snprintf()` kopiert, das die
Gleitkommawandlung von newlib erreicht. Die Frame-Größen auf dem ESP32-P4
sind nicht gemessen.

## Interner RAM

Der ESP32-P4 hat kein Funkmodul. Das WLAN läuft auf dem ESP32-C6 des
7-Zoll-Boards, den der P4 über SDIO (Secure Digital Input Output) erreicht.
Der interne RAM, der zur Laufzeit
frei ist, ist nicht gemessen, und kein Werkzeug und kein CI-Schritt
schätzt ihn. Das Panel gibt den freien Speicher beim Start aus:

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

Die Kartenaufgabe `cardjob` von FILES belegt 6.656 Bytes internen RAM für
ihren Stack und 16.384 Bytes DMA-fähigen internen RAM für ihren
Kopierpuffer. Beides wird bei der ersten Anfrage von FILES belegt, nicht
beim Start, und das Panel schreibt den freien internen RAM davor und
danach:

```text
I (…) rcbench: files: cardjob started; internal RAM <n>B free before, <n>B after
```

Ohne den RAM lautet die Zeile `W (…) rcbench: files: no internal RAM for
cardjob`, und FILES meldet `Nicht genug Speicher.` Die
Zahlen auf einem Panel sind nicht gemessen.

`firmware/panel/main/psram_bss.lf` legt die nullinitialisierten Daten (.bss)
von 30 Objekten in den PSRAM (Pseudo-Static Random-Access Memory): den
Zustand der Bildschirme und der Helfer für Karte, Seite, Einstellungen und
Ausgänge. Das Fragment nennt jedes Objekt und die Regeln, die es erfüllt.
`tools/psram_bss.py` liest die Linker-Map des Builds und lässt CI
fehlschlagen, wenn ein genanntes Objekt .bss im internen RAM behält.
Statische Zahlen des P4-Builds mit ESP-IDF v5.5.5, aus `idf.py size`:

| Posten | Mit dem Fragment | Ohne es |
| --- | ---: | ---: |
| .bss im internen RAM (DIRAM) | 30 912 | 243 464 |
| .bss im PSRAM (`.ext_ram.bss`) | 212 552 | 0 |
| interner RAM, den das Image belegt: .text, .data und .bss im DIRAM, von 655 360 | 128 576 | 341 128 |
| Anwendungs-Image, gebaut ohne `RCBENCH_IOMCU_BIN` (ohne IOmcu-Image) | 2 435 744 | 2 435 744 |

Alle Zahlen sind Bytes. Die Spalte ohne Fragment zählt das .bss im PSRAM
zu den internen Zahlen; sie ist kein eigener Build. Die PSRAM-Einträge von ESP-IDF für lwIP
(Lightweight IP Stack) und wpa_supplicant nutzen ein Schema, das nur das
Fragment von `esp_wifi` definiert, und der P4-Build bindet es nicht ein;
die 3 774 Bytes .bss von lwIP bleiben im internen RAM.

## WLAN-Downloads

Eine Kartendatei oder die ZIP-Datei geht über eine TCP-Verbindung
(Transmission Control Protocol) vom Panel zum Browser. Der Server der
Seite beantwortet eine Anfrage nach der anderen: Eine Anfrage, die während
eines Downloads kommt, wartet auf dessen Ende. In einem Heimnetz lief ein
Download einer Datei von 16MiB mit 688-757kB/s, Kartenlesen und Funk
zusammen, nicht getrennt, bei einem Ping von im Mittel 14,6ms, und ein
Upload eines Image von 2,56MB über `/update` mit 556-599kB/s. Das Panel
meldete keinen SDIO-Fehler (Secure Digital Input/Output) und zeichnete
während der Downloads 60fps. Was die Rate begrenzt:

| Glied | Einstellung | Grenze |
| --- | --- | --- |
| Power Save | keiner, solange das Panel einem Heimnetz beigetreten ist (`esp_wifi_set_ps(WIFI_PS_NONE)`); ein Access Point schläft nicht | keine |
| Sendepuffer | 11 520 Bytes, 8 Segmente zu 1 440 Bytes (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`) | 11 520 Bytes pro Umlauf: 1 152 000B/s bei 10ms Umlaufzeit, 576 000B/s bei 20ms |
| Karte | SDMMC (der SD/MMC-Host), 4 Datenleitungen mit 20MHz (`BOARD_SD_FREQ_KHZ`), 10 000 000B/s auf der Leitung | nicht gemessen |
| Funk | der ESP32-C6 über SDIO, 4 Datenleitungen mit 40MHz (`BOARD_C6_FREQ_KHZ`), 20 000 000B/s auf der Leitung, am SDMMC-Host, den auch die Karte nutzt | nicht gemessen |
| Prozessor | die Task des Servers mit Priorität 1 auf Kern 0, neben der UI-Schleife (User Interface) mit Priorität 1 | nicht gemessen |

Der Power Save, den ESP-IDF vorgibt, `WIFI_PS_MIN_MODEM`, weckt das Funkteil
nur zu jedem DTIM-Beacon (Delivery Traffic Indication Message). Bis dahin
hält der Access Point die Frames für das Panel zurück, die Bestätigungen
von TCP darunter, also wartet jeder Sendepuffer bis zu einer DTIM-Periode:
102,4ms beim üblichen Beacon-Intervall und einer DTIM-Periode von 1,
307,2ms bei einer DTIM-Periode von 3. Mit dem Sendepuffer von 5 760 Bytes,
den ESP-IDF vorgibt, sind das 18 750 bis 56 250B/s. Ohne Power Save
braucht das Funkteil mehr Strom, solange es läuft; wie viel mehr am Panel,
ist nicht gemessen.

Der Stückpuffer der Seite, 4 096 Bytes, liegt im PSRAM. ESP-IDF v5.5.5
liest ein Stück per DMA (Direct Memory Access) hinein, wenn der Puffer auf
eine Cache-Line ausgerichtet ist, sonst über einen eigenen Puffer; welchen
der beiden Wege der Puffer der Seite nimmt, ist nicht gemessen. Bei der
gemessenen Umlaufzeit von 14,6ms begrenzt der Sendepuffer einen Download auf
789kB/s (11 520 Bytes je Umlauf, hergeleitet), über den gemessenen
688-757kB/s. Während ein Download läuft, nimmt die Task des Servers der
UI-Schleife Prozessorzeit; das Panel zeichnete während der gemessenen
Downloads 60fps.

Ein Download endet vorzeitig, und die Verbindung wird zurückgesetzt (Reset),
wenn

- der Empfänger die Verbindung schließt: erkannt innerhalb von 250ms
  (`WA_SEND_SLICE_MS`, `shared/webaccess/include/wa_send.h`);
- die Verbindung fehlschlägt;
- der Empfänger 30s lang nichts annimmt (`WA_SEND_QUIET_MS`);
- das WLAN ausgeht: innerhalb von 250ms;
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
   dann nach 35s
   `curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   Die Konsole protokolliert `file: nothing taken for 30 s`; die
   Anfrage antwortet `200`.

## Das Dateisystem der SD-Karte

Das Panel liest FAT12, FAT16, FAT32 und exFAT (extended File Allocation
Table) über FatFs, die Dateisystem-Bibliothek in ESP-IDF. ESP-IDF v5.5 baut
FatFs ohne exFAT, und keine Kconfig-Option schaltet es ein. Der Panel-Build
nimmt eine Kopie der Komponente `fatfs` von ESP-IDF mit `FF_FS_EXFAT` auf 1
(`firmware/panel/fatfs_exfat.cmake`), und `storage_list.c` hält einen Build
an, dessen FatFs kein exFAT hat. Lange Dateinamen bleiben, wie
`sdkconfig.defaults` sie setzt: an, 255 Zeichen, der Arbeitspuffer auf dem
Heap.

Was exFAT kostet, gelesen aus dem P4-Build mit ESP-IDF v5.5.5 gegen
denselben Stand, gebaut ohne das Einbinden von `fatfs_exfat.cmake` und ohne
diesen Halt, beide ohne `RCBENCH_IOMCU_BIN`:

| Posten | Ohne exFAT | Mit exFAT | Wo |
| --- | ---: | ---: | --- |
| Anwendungs-Image | 2 425 520 | 2 434 880 | Flash, `ota_0` fasst 8 126 464 |
| interner RAM, den das Image belegt: .text, .data und .bss im DIRAM, aus `idf.py size` | 128 576 | 128 576 | intern |
| der Kontext des Mounts: `FATFS`, 4 offene Dateien (`FIL`) und der Zustand des VFS (Virtual File System) von ESP-IDF | 25 376 | 25 560 | Heap, zuerst PSRAM |
| der Arbeitspuffer für lange Namen, für einen FatFs-Aufruf belegt | 512 | 1 120 | Heap, zuerst PSRAM |
| der Puffer eines Verzeichnisdurchlaufs (`storage_walk()`) | 328 | 368 | Heap, intern |
| `FILINFO`, ein Eintrag, wie FatFs ihn liest, im Puffer von `storage_stat()` neben dem Pfad, für einen Aufruf | 280 | 288 | Heap, intern |

Alle Werte sind Bytes. Die Heap-Zeilen sind `sizeof()`-Werte, mit GDB aus
den beiden Builds gelesen; der Heap zur Laufzeit ist nicht gemessen. FatFs
legt seinen Puffer für lange Namen auf den Heap, nicht auf den Stack des
aufrufenden Tasks.

Die Größe einer Datei ist von FatFs bis zum Bildschirm 64 Bit breit. stat()
über das VFS liefert die Größe in `st_size`, einem `off_t` von 32 Bit auf
dem Panel; jede Größe, die das Panel verwendet, kommt deshalb aus
`f_stat()` oder `f_readdir()` von FatFs.

`FF_LBA64` (64-Bit-Sektornummern) bleibt 0. Es ist nötig für eine Karte mit
GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier) und für ein Volume über 2TiB.
Damit wächst das P4-Image um weitere 5 056 Bytes, `FATFS` von 4 160 auf
4 184 Bytes und `FIL` von 4 176 auf 4 192 Bytes.
