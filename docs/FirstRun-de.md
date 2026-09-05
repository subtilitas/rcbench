# Erster Lauf auf der Hardware

<sub>[English](FirstRun.md) · **Deutsch**</sub>

Für das erste Mal, dass beide Platinen mit gestecktem Heartbeat-Draht mit
Strom versorgt werden. Geschrieben für 0.4.0. Nichts davon wurde je gemacht,
also sagt jeder Schritt, wie „gut" aussieht und was aufzuschreiben ist, wenn
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

    -- rcbench: image 269828 bytes, 6% of the 4190208 bytes a four-megabyte
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

[Den Link in Betrieb nehmen](Bringup-de.md) durcharbeiten — der
CAN-Echo-Selbsttest. Er beantwortet eine Frage: kommen Frames unversehrt über
den Bus? Er benutzt kein Page-Protokoll; besteht er und der Link arbeitet
trotzdem nicht, liegt der Fehler oberhalb des Drahts.

```bash
idf.py -C firmware/panel -DRCBENCH_CAN_SELFTEST=1 build flash
```

Auf die **UART-Buchse** schauen, nicht auf natives USB: GPIO19 und GPIO20
tragen beides, und der Multiplexer wählt eines aus.

Gut sieht so aus:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

**Vorher nicht weitergehen.** Jeder Schritt darunter setzt voraus, dass
Frames ankommen.

Danach das Panel **ohne** `-DRCBENCH_CAN_SELFTEST=1` neu flashen.

---

## 3. Der Heartbeat-Draht — das, was alles blockiert hat

| | |
|---|---|
| Panel-Seite | **GPIO6**, an **J8** (dreipolige Stiftleiste mit 3V3, GND, GPIO6) |
| Koprozessor-Seite | **GP3** |
| Dazwischen | das retriggerbare Monoflop auf der Tochterplatine |

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

Jeder dieser Punkte ist host-getestet. **Keiner wurde auf Hardware gesehen.**

---

## 6. Erster je getriebener Pin

Ein **Servo** nehmen, nicht den ESC. Ein Servo ist der gutmütige Fall und der,
den das Oszilloskop am leichtesten liest.

**Freie Pins für einen Output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13,
GP14, GP15 und aufwärts.
**Reserviert und verweigert:** GP3 (Heartbeat), GP8–GP12 (CAN).

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

**Danach:** aufhören zu kommandieren und bestätigen, dass der Output nach
**500 ms** aufhört zu treiben (`OUT_DEFAULT_TIMEOUT_MS`).

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
| Output hört nach ½ s von selbst auf | Arbeitet wie vorgesehen — es hat nichts hineingeschrieben. |

**Verhält sich der Prüfstand so, dass es aufs Panel zeigt**, sind die beiden
neuesten und am wenigsten bewährten Dinge nur vom Compiler geprüft:

- die **Flash-Keeper-Task** (`artkeep`) und
- die **Artwork-Scheibe** in der Poll-Schleife der Control-Task.

Beide sind bei einem Koprozessor, der kein Foto meldet, wirkungslos — das ist
der schnellste Weg, sie auszuschließen.

---

## 9. Was aufzuschreiben ist

Für jeden Schritt: was gemessen wurde, wogegen es erwartet wurde und was das
Oszilloskop zeigte. `STATUS.md` trägt eine Tabelle „Open items" — die Zeilen
über Treiber auf Hardware, die Control-Page und den Flash-Speicher sind die,
die dieser Lauf beantwortet.

**Was nicht gemessen ist, bleibt als nicht gemessen geschrieben.** Eine in
diese Tabelle geratene Zahl ist schlimmer als eine leere Zelle.
