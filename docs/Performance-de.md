# Performance

<sub>[English](Performance.md) · **Deutsch**</sub>

> Für die Arbeit am Zeichencode, nicht für die Benutzung des Prüfstands. Wer
> einen Bildschirm hinzufügt oder ändert, findet hier das Budget, in das er
> passen muss.

Die Frame Rate dieses Panels bestimmt weder die CPU noch der Buffer-Wechsel,
sondern die **PSRAM-Bandbreite**.

Die LCD-Einheit liest den Framebuffer ununterbrochen mit rund 30 MB/s aus dem
PSRAM, und der Framebuffer liegt hinter einem Write-Back-,
Write-Allocate-Cache. Jedes Pixel, das die CPU schreibt, kostet deshalb
128 Byte Busverkehr — außer die zugehörige 64-Byte-Cache-Line ist schon
geladen. Die Kosten eines Frames sind damit eine Anzahl von Cache-Line-Fills,
keine Stoppuhr-Messung — und genau deshalb exakt messbar statt ungefähr
stoppbar.

`tools/frame_cost.py` misst das: es baut die echten Bildschirme für den Host
und lässt sie unter cachegrind laufen, mit dem Datencache des ESP32-S3 als
Modell (64 KiB, 8-fach assoziativ, 64-Byte-Lines). Gemessen wird die
*Differenz* zwischen null gerenderten Frames und vielen — Prozessstart, das
erste Füllen der Buffer und der Overhead von cachegrind kürzen sich so heraus.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame           10,904     1363 KiB     35.8      19.5
frame-idle          944      118 KiB      3.1      39.0
sim             11,716     1464 KiB     38.5      19.5
chrome          30,426     3803 KiB     99.9       9.8
overview           915      114 KiB      3.0      39.0
servo           15,448     1931 KiB     50.7      19.5
servo-grip        2,978      372 KiB      9.8      39.0
clear           12,006     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

## Die zwei Befunde, die jeden Bildschirm prägen

**Zeilenweise zeichnen.** Siebzehn senkrechte Linien über die volle Höhe
kosten 8 160 Cache-Line-Fills. Dieselben Pixel als waagerechte Linien kosten
**null** — jede Line ist vom Pixel davor noch geladen. Deshalb gliedert sich
die Oberfläche in waagerechte Bänder statt in Spalten, und deshalb ist eine
senkrechte Trennlinie eine bewusste Ausgabe, kein kostenloser Schmuck.

**Das Chrome cachen.** Jeden Frame alles neu zu zeichnen kostet 24 896 Fills;
der eingeschwungene Zustand kostet **740**. Das Dreißigfache — für Pixel, die
sich gar nicht geändert haben. Jeder Bildschirm führt deshalb je Framebuffer
eine Bitmaske dessen, was schon gezeichnet ist; dafür gibt es das Argument
`buffer_index` von `render()`. Das Panel wechselt zwischen zwei Buffern — wer
nur den gerade gezeichneten invalidiert, lässt den anderen einen Frame
zurückhängen, und bei wechselnden Buffern sieht das nach *Flackern* aus, nicht
nach einem veralteten Pixel.

## Was der Prüfstand kostet, und warum 19,5 fps das Ziel sind

| | Fills/Frame | fps |
| --- | ---: | ---: |
| Das Menü, und jeder Bildschirm mit gecachtem Chrome | **821** | 39,0 |
| Der Motorprüfstand, zwischen zwei Samples | **740** | 39,0 |
| Der Motorprüfstand, wenn ein Sample eintrifft | **10 264** | 19,5 |
| Derselbe, mit dem Simulations-Watermark | **11 825** | 19,5 |
| Nichts gecacht | 24 896 | 9,8 |

Der Prüfstandsbildschirm läuft mit der halben Panelrate — und das ist der
Auslegungspunkt, kein Mangel. Das Panel bewegt 976 KiB pro Frame bei 39 Hz;
ein Frame, der das Doppelte kostet, landet bei **19,5 fps: genau ein Frame pro
20-Hz-Telemetriesample**. Schneller zu zeichnen, als Zahlen ankommen, hieße
identische Pixel neu malen; langsamer hieße Samples verlieren.

Die Obergrenze für die Prüfstandsmodi ist deshalb **15 600 Fills** — diese
Schwelle, keine Geschmacksfrage: darüber schafft das Panel kein Frame pro
Sample mehr, und genau diese Regression soll auffallen. Bildschirme mit
gecachtem Chrome sind auf **2 000** festgelegt; das fängt ein Menü, das
aufgehört hat zu cachen.

**Nur zeichnen, wenn es etwas zu zeichnen gibt.** Samples kommen mit 20 Hz,
das Panel zeichnet mit 39 — ungefähr jeder zweite Frame hat nichts Neues. Der
Prüfstandsbildschirm führt deshalb den Push-Zähler des Plots und eine
Revisionsnummer der Bedienelemente, jeweils je Framebuffer, und zeichnet Plot,
Anzeigen und Bedienelemente nur neu, wenn der zugehörige Zähler sich bewegt
hat. Ein Frame zwischen zwei Samples, ohne Berührung, kostet **740 Fills** —
das gecachte Chrome und sonst nichts.

Dieser Umbau hat den Spielraum geschaffen, den die Tabelle zeigt: der
schlimmste Frame in der Simulation fiel von 15 603 auf 11 825 gegen die Grenze
von 15 600, der typische von 14 042 auf 740. Das zählt mehr, als die
Mittelwerte vermuten lassen, denn der vermiedene Verkehr ist nicht bloß
Verschwendung — er konkurriert mit dem Scan-out des LCD um dasselbe PSRAM, und
genau daran ist beim ersten Hardwarestart das Bild zerrissen (Tearing).

Die Zähler laufen je Framebuffer, aus dem oben genannten Grund: das Panel
wechselt zwischen zweien, und ein Buffer, der zuletzt vor einem Sample
gezeichnet wurde, braucht ein Update — auch wenn der andere aktuell ist.
`each_framebuffer_is_updated_independently` in `test_motor` hält das fest;
wer einen der Zähler auf einen einzigen Platz zusammenlegt, sieht den Test
umfallen.

Braucht ein künftiger Bereich noch mehr Platz, sind die verbleibenden Hebel,
vom gröbsten zum feinsten: die Höhe des Plots, seine Breite, und das
Simulations-Watermark auf den tatsächlich neu gezeichneten Bereich zu clippen.

Die Tabelle oben prüft `frame_cost.py --check-doc` — mit Toleranz statt Byte
für Byte, denn die absoluten Fills verschieben sich zwischen Maschinen um ein
paar: argv und Umgebungsvariablen landen im selben Cache, um den auch der
Framebuffer konkurriert.
