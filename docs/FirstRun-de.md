# Erster Lauf auf der Hardware

<sub>[English](FirstRun.md) · **Deutsch**</sub>

Für das erste Mal, dass beide Platinen mit gestecktem Heartbeat-Draht mit
Strom versorgt werden. Geschrieben für 0.8.0. Nichts davon wurde je gemacht,
also sagt jeder Schritt, wie „gut“ aussieht und was aufzuschreiben ist, wenn
es das nicht tut.

Die Liste von oben nach unten abarbeiten. Jeder Schritt setzt voraus, dass
der darüber bestanden hat.

---

## 0. Vor dem Einschalten

**Bereitlegen:** ein Oszilloskop, ein Servo, einen ESC, der bidirektionales
DShot spricht, ein Labornetzteil mit Strombegrenzung und das USB-Kabel für
jede Platine.

**Noch keinen Motor an den ESC.** Schritt 5 ist das erste Mal, dass diese
Firmware je einen Pin getrieben hat; das Erste, worauf man schaut, ist das
Oszilloskop und kein Propeller.

**Strombegrenzung:** so niedrig, dass ein kurzgeschlossener Output sie
auslöst, statt eine Leiterbahn zu verbrennen.

> ### Den UART-Schalter des Panels vor allem anderen auf `UART1` stellen
>
> Neben den Tastern **BOOT** und **RESET** sitzt ein Schiebeschalter,
> beschriftet mit **`UART1`** und **`UART2`**. Er wählt aus, womit die
> serielle Seite der gebrückten USB-C-Buchse verbunden ist; die Konsole
> braucht ihn auf **`UART1`**.
>
> In der anderen Stellung meldet sich die Buchse trotzdem an: ein COM-Port
> erscheint, das Betriebssystem benennt den CH343. Es kommt in keine Richtung
> etwas durch — keine Konsolenausgabe, kein Flashen — also dasselbe Bild, das
> ein defektes Kabel oder eine defekte Platine liefert.
>
> Die andere USB-C-Buchse des Panels führt natives USB auf GPIO19 und GPIO20,
> die der Multiplexer etwa eine Sekunde nach jedem Start an CAN übergibt. Sie
> flasht die Platine und kann eine laufende nicht beobachten. Die gebrückte
> Buchse ist die einzige Konsole des Panels im Betrieb.

---

## 1. Beide Platinen flashen

```bash
# Koprozessor — erzeugt rcbench-iomcu.uf2, bei gedrücktem BOOTSEL kopieren
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# Panel
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

Der Koprozessor-Build gibt seine eigene Größenprüfung aus:

    -- rcbench: image 272040 bytes, 6% of the 4186112 bytes a four-megabyte
       module leaves below the store

**Achtung:** das Modul für das Bring-up ist ein Waveshare RP2350-CAN mit
**4 MB**, während `PICO_BOARD` auf eine Board-Datei zeigt, die **16 MB**
angibt. Der Linker misst gegen 16 MB und warnt nicht. Die Zeile oben ist das
Einzige, was es tut — also lesen.

**Die Partitionstabelle des Panels hat sich geändert.** Sie trägt jetzt eine
2 MB große `boardart`-Partition. Wurde das Panel davor geflasht, das
zusammengeführte Image bei Offset 0 schreiben statt nur die App:

```bash
idf.py -C firmware/panel merge-bin -o rcbench-panel-merged.bin
esptool.py -p /dev/ttyACM0 write_flash 0x0 firmware/panel/build/rcbench-panel-merged.bin
```

---

## 2. Der Link, vor allem anderen

Das Panel führt den CAN-Echo-Selbsttest selbst aus, bei jedem Start, 1200 ms
lang innerhalb des Splash. Er beantwortet eine Frage: kommen Frames unversehrt
über den Bus? Er benutzt kein Page-Protokoll; besteht er und der Link arbeitet
trotzdem nicht, liegt der Fehler oberhalb des Drahts.

**Gut sieht nach nichts aus:** der Splash zeigt `LINK  OK  CAN 1 Mbit/s` und
übergibt ans Menü.

**Ein Fehlschlag ist ein Bildschirm**, vor dem Menü, mit dem Urteil, der Liste
dessen, was der Reihe nach zu prüfen ist, und den Zählern beider Enden. Ihn zu
verlassen kostet zwei Sekunden Halten. [Den Link in Betrieb
nehmen](Bringup-de.md#der-bus-fehler-bildschirm) hat die Urteile und was jedes
bedeutet.

**Vorher nicht weitergehen.** Jeder Schritt darunter setzt voraus, dass Frames
ankommen.

Die Einzelheiten stehen auf der Konsole, wenn man sie will — auf der
**UART-Buchse**, nicht auf nativem USB, denn GPIO19 und GPIO20 tragen beides
und der Multiplexer wählt eines aus:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. Der Heartbeat-Draht — das, was alles blockiert hat

| | |
|---|---|
| Panel-Seite | **GPIO6**, an **J8** (dreipolige Stiftleiste mit 3V3, GND, GPIO6) |
| Koprozessor-Seite | **GP3** |
| Dazwischen | das retriggerbare Monoflop, sobald es eines gibt. Es ist auf keiner Platine, der Aufbau-Prüfstand fährt daher eine direkte Leitung und hat keine Hardware-Rückfallebene. Die Leitung deckt ein Panel ab, das aufhört zu schlagen, solange der Koprozessor gesund ist: Er entschärft nach 150 ms Stille. Unabgedeckt bleibt ein Panel, das aufhört zu schlagen, während der Koprozessor nicht handeln kann -- dann nimmt nichts die Ausgänge weg, und genau das täte das Monoflop ohne jede Firmware |

Ohne diesen Draht verweigert der Koprozessor jedes Arm, und das ist die
Verriegelung, die arbeitet — kein Fehler.

**Die Zahlen, an denen er gemessen wird:**

| | |
|---|---|
| Flankenperiode | 20 ms |
| Akzeptierte Lücke | 4 ms bis 150 ms |
| Flanken, bis er als lebendig gilt | 4 |

Die Leitung muss also rund **80 ms** flanken, bevor ein Arm gelingen kann.

**Am Oszilloskop, an GP3:** ein Rechteck, Flanke zu Flanke 20 ms.
Überschreitet die Lücke je 150 ms, verwirft der Koprozessor sie und das
nächste Arm wird verweigert.

**Woher die Flanken kommen:** aus der Control-Task des Panels, innerhalb
ihrer Poll-Schleife. Alles, was diese Task anhält, hält den Heartbeat an —
deshalb darf dort nichts Langes laufen.

---

## 4. Erster Link-Aufbau — mit rund einer Minute ungewöhnlichem Verkehr rechnen

**Das ist neu und passiert beim allerersten Link-Aufbau.** Der Koprozessor
trägt jetzt ein 201 kB großes Foto von sich selbst, und das Panel holt es
einmal und behält es im Flash.

Auf der Panel-Konsole, in dieser Reihenfolge:

    I (…) rcbench: coprocessor answered
    I (…) rcbench: hardware 1 says where its pads are
    I (…) rcbench: hardware 1 says which pads are grounds and rails
    I (…) rcbench: fetching hardware 1's photograph: 500 x 206, 206000 bytes
    …
    I (…) rcbench: hardware 1's photograph kept

**Was währenddessen zu erwarten ist:**

- Zusätzlicher CAN-Verkehr über Dutzende Sekunden. Es nimmt 15 ms von jedem
  50-ms-Poll, ist also in Wanduhrzeit langsamer, als der Bus allein bräuchte.
- **Ein sichtbarer Stillstand des Displays**, wenn es fertig ist und in den
  Flash schreibt. Eine Flash-Operation schließt den Cache, durch den das
  Nachladen des Bounce-Buffers PSRAM liest, also steht das Panel für die
  Dauer des Schreibens. Settings-Speichern kostet das schon heute.
- **Der Heartbeat darf davon nicht gestört werden.** Die Übertragung ist in
  Scheiben geteilt und das Flash-Schreiben läuft auf einer eigenen Task genau
  deswegen. **Fällt der Heartbeat in dieser Minute aus, anhalten und
  aufschreiben** — das ist das Wichtigste, was dieser erste Lauf finden kann,
  und es ist Code, der noch nie gelaufen ist.

Beim **zweiten** Link-Aufbau ist das Foto schon behalten und nichts davon
passiert. Wer es ganz überspringen will: ein Koprozessor ohne einkompiliertes
Artwork meldet null Blöcke, und das Panel zeichnet die Platine aus ihrem
Umriss.

---

## 5. Armen ohne angeschlossene Last

Noch ist nichts an einem Output verdrahtet. Dieser Schritt prüft die
Verriegelung, nicht einen Pin.

1. **Heartbeat-Draht abgezogen** → Armen muss **verweigert** werden. Der
   Koprozessor antwortet `NOT_ARMED`, weil `!beat.alive`. Das Panel zeigt die
   Verweigerung.
2. **Heartbeat-Draht gesteckt, Panel armed** → der Koprozessor nimmt es an.
3. **Heartbeat-Draht im armierten Zustand ziehen** → muss binnen **150 ms**
   in den sicheren Zustand fallen.
4. **STOP drücken** → rastet ein. Es zu verlassen braucht ein ausdrückliches
   Arm, keinen Link, der sich erholt.
5. **CAN-Draht im armierten Zustand ziehen** → der Koprozessor gibt nach
   **200 ms** auf; das Panel eskaliert nach **1 s**.
6. **Touch abdecken / sterben lassen** → nach **500 ms** Stille ist Armen
   gesperrt.

Jeder dieser Punkte ist host-getestet. **Keine der Zahlen unten wurde an
einem Messgerät gesehen.** Ein Servo und ein Motor sind seither auf einem
Aufbau-Prüfstand vom Panel aus gelaufen, ein Pin treibt also; was kein
Oszilloskop und kein Logikanalysator gelesen hat, ist irgendeine Impulsbreite,
Rahmenperiode oder Antwortverzögerung in diesem Baum.

---

## 6. Erster Pin an einem Messgerät

Ein **Servo** nehmen, nicht den ESC. Ein Servo ist der gutmütige Fall und der,
den das Oszilloskop am leichtesten liest.

**Freie Pins für einen Output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13,
GP14, GP15 und aufwärts.
**Reserviert und verweigert:** GP3 (Heartbeat), GP8–GP12 (CAN).
**Als zweiter PWM-Pin verweigert:** ein Pin, dessen PWM-Compare-Register schon
von einem gebundenen Pin belegt ist. Auf dem RP2350 ist die Slice unterhalb
von GP32 `(Pin / 2) modulo 8` und der Kanal das niedrigste Bit des Pins, also
teilen sich GP0 und GP16, GP1 und GP17, GP2 und GP18, GP4 und GP20, GP5 und
GP21, GP6 und GP22 je ein Compare-Register. Der zweite eines Paares wird
verweigert, statt auf die Pulsbreite des ersten gemuxt zu werden. Die
OUTPUTS-Seite führt kein Bit mit, das „gebunden“ sagt, der Bildschirm sieht
also weiter konfiguriert aus, während die Leitung keinen Impuls liefert.

Am Panel: **Setup → OUTPUTS**, `SERVO PWM` wählen, einen Pin anhaken. Oder
**Setup → PICK A PIN** für das Platinenbild — Massen sind mit `G` markiert,
Versorgungen tragen ihre Spannung.

**Den Pin vor dem Anschließen des Servos messen.**

| | |
|---|---|
| Framerate | 40 bis 400 Hz (voreingestellt 50 Hz) |
| Puls | 500 bis 2500 µs, außerhalb verweigert |
| Auflösung | 1 µs |

**Messen und aufschreiben:** die tatsächliche Frameperiode, den Puls an
beiden Enden des Wegs und den Jitter. Bit-Timings sind die größte
unbestätigte Fläche im Baum.

**Danach:** bestätigen, dass der Servo-Bildschirm loslässt, wenn man es ihm
sagt, und nur dann. Den Finger zu heben stoppt den Ausgang nicht: der
Bildschirm hält die gegebene Stellung und wiederholt sie alle **100 ms**
(`SERVO_HOLD_MS`) gegen die **500 ms** des Koprozessors
(`OUT_DEFAULT_TIMEOUT_MS`), ein Servo bleibt also stehen, wo es hingestellt
wurde. **RELEASE** führt die Ruderflächen auf die Mitte zurück; es löscht den
Slot nicht, und der Pin pulst weiter. Beendet werden die Flanken durch
Unscharfschalten, STOP oder das Verlassen des Bildschirms, was entschärft. Am
Oszilloskop prüfen, dass RELEASE den Impuls in die Mitte des Kanalwegs führt
und dass ein Unscharfschalten ihn beendet.

Ein Kanal, den niemand auffrischt, geht nach 500 ms weiterhin in seine
Ruhelage; das betrifft einen gebundenen Pin, den der Servo-Bildschirm nicht
hält.

---

## 7. Die Zahlen, die niemand gemessen hat

Aus der Spezifikation geschrieben, nur gegen Frames geprüft, die derselbe
Code baut. Für jede einen echten Wert aufschreiben:

- **Jedes DShot-Bit-Timing**, gegen die Toleranz eines ESC statt gegen die
  Spezifikation.
- **Die Antwortrate** von fünf Vierteln der DShot-Rate.
- **Die Konvention des führenden Bits** des Gruppencodes.
- **Die Umschaltverzögerung** — ob 30 µs das ist, was ein ESC wirklich
  wartet.
- **Die erweiterten Telemetrie-Frametypen** und ihre Einheiten.
- **Der PPM-DMA-Ring**, der einen Frame abspielt.
- **Das Flash-Löschfenster** auf dem Koprozessor, und ob der Heartbeat über
  ein Speichern hinweg wieder greift.

---

## 8. Wenn etwas schiefgeht

| Symptom | Zuerst hier nachsehen |
|---|---|
| Arm verweigert, Draht gesteckt | Heartbeat hat noch keine 4 Flanken, oder Lücke über 150 ms. GP3 messen. |
| Arm verweigert, kein offensichtlicher Grund | Touch 500 ms tot sperrt es. Das Panel berühren. |
| Link steht, Bildschirm bietet keine Pins an | Der Katalog der Platine kam nicht an. Die Konsole sagt, welche Page scheiterte. |
| Heartbeat fällt in der ersten Minute aus | **Die Artwork-Übertragung oder der Flash-Keeper.** Neuester Code, nie gelaufen. Siehe §4. |
| Display friert kurz ein | Ein Flash-Schreiben. Einmalig erwartet, wenn das Foto behalten wird. |
| Koprozessor bootet nach dem Flashen nicht | Image über 4 MB. Die Größenzeile aus §1 prüfen. |
| Panel bootet, aber keine `boardart`-Partition | Nur die App über eine alte Tabelle geflasht. Merge-bin bei Offset 0. |
| Output geht nach ½ s in die Mitte | Arbeitet wie vorgesehen — in diesen Kanal hat nichts geschrieben, also ist er in seine Ruhelage gegangen: Mitte beim Servo, null beim Motor. Die Impulse laufen weiter, solange der Prüfstand scharf ist |
| Impulse hören ganz auf | Nicht der Timeout. Etwas hat den Pin freigegeben, unscharf geschaltet oder den Prüfstand gestoppt |

**Verhält sich der Prüfstand so, dass es aufs Panel zeigt**, sind die drei
neuesten und am wenigsten bewährten Dinge nur vom Compiler geprüft:

- die **Flash-Keeper-Task** (`artkeep`),
- die **Artwork-Scheibe** in der Poll-Schleife der Control-Task und
- die **Run-Log-Task** (`runlog`), der jeder Schreibzugriff auf die SD-Karte
  (Secure Digital) gehört.

Die ersten beiden sind bei einem Koprozessor, der kein Foto meldet, wirkungslos —
das ist der schnellste Weg, sie auszuschließen. Die dritte ist ohne Karte im
Schacht wirkungslos und tut nichts, bevor der Prüfstand scharf ist.

---

## 9. Was aufzuschreiben ist

Für jeden Schritt: was gemessen wurde, wogegen es erwartet wurde und was das
Oszilloskop zeigte. `STATUS.md` trägt eine Tabelle „Open items“ — die Zeilen
über Treiber auf Hardware, die Control-Page und den Flash-Speicher sind die,
die dieser Lauf beantwortet.

**Was nicht gemessen ist, bleibt als nicht gemessen geschrieben.** Eine in
diese Tabelle geratene Zahl ist schlimmer als eine leere Zelle.
