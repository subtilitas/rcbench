# Programmierung für YGE-ESCs

<sub>[English](LinkYge.md) · **Deutsch**</sub>

Diese Seite beschreibt die Link-Pages der Programmierung für YGE-ESCs.
[Der Link](Link-de.md) behandelt Verkabelung, Frame-Format, Page-Übersicht
und die anderen Pages. ESC steht für Electronic Speed Controller, PD für
Power Delivery, GPIO für General-Purpose Input/Output und CRC für Cyclic
Redundancy Check.

Ab Protokoll 4.42 lesen und schreiben `YGE_PROG` (0x44) und `YGE_DATA` (0x45)
die Parametertabelle eines YGE-ESC über sein Telemetriekabel. Sie halten eine
Session von [`protocols/openyge`](../protocols/openyge/README.md) auf dem Pin
eines Programmierports. `shared/outputs/yge_port.c` hält die Regeln. Ein IOmcu,
der sie bedient, meldet `LINK_CAP_YGE` (Bit 9). [Die
OpenYGE-Spezifikation](OpenYGE-de.md) nennt Draht, Tabelle und das Timing der
Session.

Die Pages bieten keine rohen Transaktionen an. Der IOmcu behält die gelesene
Kopie und schickt jeden Frame durch den Guard
([OpenYGE, Abschnitt 9](OpenYGE-de.md#9-stand-in-rcbench)). Das Panel wählt
eine Operation und übergibt einen Entwurf.

YGE ist ein Name oder eine Marke seines Inhabers. Er steht hier nur, um zu
sagen, mit welchen ESCs dies arbeitet.

## Welcher Pin

| Ziel | Genommen, wenn |
|---|---|
| Programmierport 0 bis 3 | der IOmcu seinen Pin hält und sein FUNC nicht DShot ist, wie für KST und HITEC (`prog_port_wire_pin()`) |

Jeder andere Port wird mit `NO_PIN` abgelehnt, eine Portnummer jenseits der
Ports des Builds mit `TARGET`. Das FUNC des Ports ändert sich nicht. Nur
dieser Pin wird umgeschaltet.

Verkabelung: das Telemetriesignal des ESC an den Signalpin des Ports, seine
Masse an die Masse des Ports. Der Mittelleiter des Gaskabels (BEC) bleibt vom
Prüfstand getrennt; das Gaskabel ist abgezogen. Ob ein angeschlossenes
Gaskabel ändert, ob der ESC antwortet, ist nicht bekannt (OpenYGE-de.md U9).

## Der Weg hin und zurück

| Schritt | Was geschieht |
|---|---|
| OPEN | Abgelehnt, solange ARMED oder ARM angefragt ist (`ARMED`); solange KST, HITEC, ESC_WIRE oder ein Firmware-Update die Bench hat oder eine gespeicherte PD-mini-Verdrahtung noch nicht angebunden ist (`HELD`, jeder Port); auf Board 3, solange die Schiene das CONNECT des Ports ablehnt (`HELD`, dieser Port); ohne vertrauten Heartbeat oder bei stillem Link (`UNSAFE`). Ab diesem Frame armt die Bank nicht, CONTROL lehnt ein ARM ungleich 0 ab, SUPPLY und PROG_SUPPLY sind gehalten, eine Änderung der Verdrahtung von SUPPLY (ENABLE bis BAUD) wird mit `BUSY` abgelehnt, und KST, HITEC und ESC_WIRE lehnen jeden Port ab. |
| `STOPPING`, 50ms | Der Port gibt nichts aus. ABORT, ein Stopp oder ein verlorener Link geben hier den Pin frei, ohne etwas anzufassen. |
| `OPEN` | Der Port lässt den Pin los, die Leitung öffnet geparkt. Keine State Machine, kein Programmspeicher oder keine 2 DMA-Kanäle (direct memory access): `NO_LINE`, der Pin geht zurück, die Page ist frei. |
| CLOSE | Mit `WRITING` abgelehnt vom ersten Frame eines Schreibens bis zum Ende seines Lesens danach. Die Session endet, die Leitung schließt, der Pin geht an seinen Port zurück. Ein Port, der den Pin nicht zurücknimmt, endet mit `NOT_BACK`: die Page bleibt in Zustand 8 gehalten, mit gesetztem Quiet-Bit, ARM bleibt also abgelehnt. Ein CLOSE versucht es erneut; ein IOmcu-Neustart löst es. |

## Eintritt

`ENTER` bereitet die Versorgungsart (`SWITCHED` oder `BY HAND`), die Spannung,
die Strombegrenzung und die Boot-Wartezeit vor. Es braucht den Schlüssel
`LINK_YGE_KEY` (0x59C7), den ein Panel am Ende des 2-s-Halts des Bedieners
sendet, und mit keinem anderen Kommando als WRITE.

| `SWITCHED`, am PD mini (der Standard) | Leitung | Versorgung |
|---|---|---|
| `POWER_OFF`: mindestens 250ms, innerhalb von 3000ms als aus gelesen | geparkt | aus |
| `POWER_ON`: bis der PD mini als ein liest, höchstens 3000ms | geparkt | ein angefragt, mit 3000 bis 20000mV (8400mV auf Board 3) und 50 bis 3000mA, mindestens 700mV unter dem Eingang |
| `RUN`: die Boot-Wartezeit (1000 bis 20000ms, Standard 5000ms), gezählt ab dem Lesen als ein, Zuhören bis zu 3000ms, dann Anfragen, bis eine beantwortet ist | hört zu | ein |

| `BY HAND`, am eigenen Akku des ESC | Leitung | Versorgung |
|---|---|---|
| `HAND_WAIT`, HAND_DONE bis 60000ms nach ENTER angenommen; bei 60001ms ein Fehler | geparkt: nichts speist einen ESC ohne Versorgung | nicht angefasst |
| HAND_DONE, dann `RUN` wie oben | hört zu | nicht angefasst |

Die Anleitungen von YGE erlauben als Versorgung des ESC nur Akkus
(OpenYGE-de.md K16); der PD mini liefert höchstens 3A. Ob ein Modell am PD
mini startet und ab welcher Spannung, ist nicht bekannt (OpenYGE-de.md U11).

Eine Session nutzt auf jeder Platine den PD mini zur Versorgung des ESC, auch
auf Platinen mit PROG_SUPPLY. Sie schaltet PROG_SUPPLY nie ein. Von OPEN bis
zur Freigabe wird ein ON des Panels dort mit `BAD_VALUE` abgelehnt. Ein
Fehler, STOP oder ABORT schaltet PROG_SUPPLY zusammen mit dem PD mini aus.
CLOSE ohne eines dieser Ereignisse lässt den vom Panel gesetzten Zustand
unverändert. Der Zustand wird `READY`, sobald der ESC eine Anfrage
beantwortet oder der erste Frame eines Legacy-ESC ankommt.

In `READY` sendet die Session 40ms nach jeder Antwort eine Telemetrieanfrage.
3 verpasste Anfragen beenden sie mit `SILENT`: ein Fehler.

## Was jedes Ereignis tut

| Ereignis | Leitung | Versorgung | Danach |
|---|---|---|---|
| ARM angefragt, Page frei | die des Ports | wie sie ist | ein späteres OPEN wird abgelehnt, solange es steht |
| ARM angefragt, Page gehalten | keine Änderung | keine Änderung | ein ARM ungleich 0 wird von OPEN bis zur Rückgabe mit `NOT_ARMED` abgelehnt, und das Bit der Page in der Quiet-Maske hält `safety_gate_arm_now()` zusätzlich unten. Die Rückgabe setzt ARM und THROTTLE auf 0 |
| STOP (eine Stopp-Flanke oder die Operation STOP), auch in einem Schreiben | sofort geparkt | PD mini und PROG_SUPPLY aus | gehalten, `STOPPED` |
| ABORT, auch in einem Schreiben | sofort geparkt | PD mini und PROG_SUPPLY aus | gehalten |
| Heartbeat verloren oder Link 200ms still, auch in einem Schreiben | sofort geparkt | sofort aus | gehalten, `STOPPED` |
| Fehler | sofort geparkt | PD mini und PROG_SUPPLY aus | gehalten, `FAULT` und `STOPPED` |
| LEAVE | geparkt | PD mini aus | gehalten, `OPEN` |
| IOmcu-Neustart | der Reset-Zustand des Chips | aus beim Booten | frei; der Port bindet, wie die Page PROG ihn hat |
| Die Versorgung aus, still oder nach dem Ein als aus gelesen, in `RUN` oder `READY` | sofort geparkt | aus | gehalten, `FAULT`, Ergebnis `SUPPLY` |

Ein Stopp schneidet ein Schreiben sofort ab; auch das OFF des Panels auf der
Page SUPPLY wird für ein Schreiben nicht zurückgehalten und ist für die Session
ein Fehler. Ein Schreiben, das nach seinem ersten Frame abgeschnitten wird,
lässt die Tabelle des ESC unbekannt: `LINK_YP_F_CUT_WRITE`. Ob das Schreiben
eines Parameters ganz gespeichert wird, wenn darin die Versorgung geht, ist
nicht bekannt (OpenYGE-de.md U5). Bei `BY HAND` kann der Prüfstand die eigene
Versorgung des ESC nicht abschalten: er parkt die Leitung, und der Bediener
muss den Akku trennen.

Geparkt ist ein Eingang mit Pull-down und abgeschaltetem Eingangspuffer.
Zuhören ist ein Eingang mit Pull-up; getrieben wird die Leitung nur, während
ein Frame hinausgeht.

**Was als Fehler zählt.** Jede Operation der Session, die anders als `OY_OK`
endet: `SILENT`, `NO_TABLE`, `CHANGED`, `NOT_TAKEN`, `PARTIAL`; eine Ablehnung
eines Entwurfs durch den Guard (`GUARD`); eine Versorgung, die versagt; ein
Warten bei `BY HAND`, das abläuft. Ein Kommando, das die Session vor jedem
Frame ablehnt (`NO_TABLE` für ein Schreiben vor einem Lesen, `NOT_KNOWN` für
eine nur lesbare Tabelle, ein Zustand), ist eine Ablehnung dieses Kommandos
und ändert nichts.

**Wer den Rest schaltet.** Die Bank ist SAFE, solange die Page gehalten ist;
die Leitung der Page, der PD mini und PROG_SUPPLY sind also alles, was der
IOmcu an hat. `LINK_YP_F_FAULT` sagt es dem Panel, das ihm in den SAFE-Zustand
des Prüfstands folgt.

## Die Hälfte des Panels

`shared/bench/yge_link.c` sendet die Anfragen und verfolgt sie auf jedem
Panel; `shared/bench/yge_prog.c` und `yge_prog_file.c` halten den Ablauf der
Sitzung, ihre Ablehnungen und ihre Sicherung. Die Seiten sind noch nicht
gebaut: das Panel verfolgt eine Page, die die Bench-Konsole hält, und führt
keine eigene Sitzung.

| Ereignis | Was das Panel tut |
|---|---|
| Register 0 bis 22 | alle 5ms in einer Anfrage, 100ms bei gehaltenem Pin, 500ms sonst |
| Erstes Lesen nach einem Panel-Neustart, ein Pin gehalten | CLOSE; erneut alle 1000ms, solange die Page `WRITING` antwortet oder in `NOT_BACK` bleibt. Keine Anfrage, bis die Page frei ist. |
| Link zurück, kein Neustart | nichts wird geschlossen |
| Die STOP-Taste oder jeder Stopp des Panels | die Operation `STOP`, sobald die Page bekannt ist und einen Pin hält; ein Stopp in einer Link-Lücke geht danach. Ein `BAD_VALUE`, eine SEQ eines anderen Schreibers: lesen und erneut, höchstens 6 Mal. |
| `FAULT` steigt, oder der Zustand erreicht `NOT_BACK` | ein Fehler: alle Ausgänge aus (`prog_faults_follow()`). Ein `FAULT`, der beim ersten Lesen nach Link-Aufbau schon steht, zählt nicht. |
| Der Zustand erreicht `NOT_BACK` | ein Alarm, der den Port nennt |
| Ein Pin gehalten, von wem auch immer | ARM abgelehnt, mit dem Alarm der ESC-Programmierung; der SAFE-Chip des Bands nennt den Port; die PROG-Page wird nicht geschrieben |
| Ein Stopp eingerastet | kein Befehl außer CLOSE, LEAVE und ABORT geht (`yge_prog_cmd_allowed()`) |
| 10 Lesezugriffe der Page in Folge fehlgeschlagen, in einer Anfrage | die Anfrage endet. Mit einem Halten im letzten Lesen: ein Fehler, und `STOP` |
| Eine Anfrage läuft 90000ms, nachdem die Page sie nahm | ABORT |
| Eine Lesekopie mit neuem `READS`, `READ_CRC` oder `ENTRIES` | aus Block 0 und, bei mehr als 32 Einträgen, Block 1 geholt und gegen `oy_guard_table_crc()` = `READ_CRC` geprüft; 3 Abrufe, dann aufgegeben, bis die Page eine andere Kopie nennt. Keine Anfrage, solange sie kommt. |

Was eine Sitzung anfragt, in dieser Reihenfolge:

| Anfrage | Was geht |
|---|---|
| CONNECT, am Ende des 2s-Halts des Bedieners | OPEN, ENTER mit dem Schlüssel, READ, jedes nachdem das vorige mit `LINK_YGE_RESULT_DONE` endete; `BY HAND` hält bei `HAND_WAIT` bis HAND_DONE. Abgelehnt, ohne dass etwas gesendet wird: ARMED, ein Stopp eingerastet, keine Page, ein Pin gehalten, ein Port über 3, eine Boot-Wartezeit außerhalb 1000 bis 20000ms; bei `SWITCHED` eine Spannung außerhalb 3000 bis 20000mV oder ein Limit außerhalb 50 bis 3000mA, oder über dem von SUPPLY für das Board. |
| WRITE, am Ende des 2s-Halts des Bedieners | abgelehnt, ohne dass etwas gesendet wird, bei einer Tabelle ohne `LINK_YP_F_WRITABLE`, einem Entwurf einer anderen Lesekopie, einem Entwurf gleich der Kopie, oder einem Entwurf, den der Guard von `protocols/openyge` ablehnt (mit dem Index, den er nennt). Das erste Schreiben einer Sitzung sichert zuerst, siehe unten. |
| LEAVE | LEAVE, wenn der ESC antwortet, dann CLOSE; sonst CLOSE allein. Geht auch mit eingerastetem Stopp. |

**Die Sicherung der Sitzung.** Die Tabelle, wie das erste vollständige
Lesen der Sitzung sie fand. Das erste WRITE der Sitzung lässt die Karte sie
als `BENCH/YGE/CONF/YGE001-<type>.TXT` speichern (das Format von
`protocols/openyge/include/openyge_file.h`, Typcode 5712 gibt
`YGE001-5712`), und das WRITE geht, wenn die Karte geantwortet hat. Ohne
Karte, mit einer Karte ohne FAT32 oder exFAT oder bei einem gescheiterten
Speichern geht das WRITE, und die bisherigen Werte bleiben nur im
Arbeitsspeicher; die Meldung sagt es. Eine Karte, die 10000ms schweigt, ein
Stopp, ein ARM oder ein ESC, der vor der Antwort fehlt: das WRITE geht
nirgends hin.

## Aktuelle Einschränkungen

- **Ein zurückgekehrter Link behält den Halt.** Bedingung: der Link war über
  200ms still, ohne Neustart des Panels. Die Page bleibt `OPEN` und `STOPPED`
  mit gesetztem Quiet-Bit, die Bank kann also nicht armen. Der Ausweg ist
  `yge close` an der Bench-Konsole oder ein Neustart von IOmcu oder Panel.
- **Ein Stopp in einem Schreiben.** Bedingung: ein STOP, eine Stopp-Flanke,
  ein Fehler oder ein Linkverlust zwischen dem ersten Frame eines Schreibens
  und dem Ende seines Lesens danach. Die Tabelle, die der ESC hält, ist bis
  zum nächsten Lesen nicht bekannt.
- **Keine Seiten.** Das Panel verfolgt die Page und schließt ein Halten, das
  es nach einem Neustart findet; `yge` an der Bench-Konsole führt eine
  Sitzung.
- **Ein von der Bench-Konsole gehaltener Pin geht mit einem
  Panel-Neustart.** Bedingung: `yge open` an der Konsole, dann ein
  Panel-Neustart. Das Panel kann ihn nicht von seinem eigenen Halten
  unterscheiden.
- **Ein Firmware-Update**, das startet, während die Page gehalten ist, zählt
  als Stopp.

## Register

`YGE_PROG` liest:

| Reg | Name | Wert |
|---|---|---|
| 0 | `STATE` | 0 `FREE`, 1 `STOPPING`, 2 `OPEN`, 3 `POWER_OFF`, 4 `POWER_ON`, 5 `HAND_WAIT`, 6 `RUN`, 7 `READY`, 8 `NOT_BACK` |
| 1 | `SEQ` | die zuletzt genommene SEQ |
| 2 | `OP` | die letzte Operation |
| 3 | `RESULT` | Low-Byte das Ergebnis, High-Byte die Ablehnung des letzten Kommandos |
| 4 | `FLAGS` | Bit 0 `STOPPED`, 1 `FAULT`, 2 diese Session hat die Versorgung eingeschaltet, 3 ein PD mini antwortet, 4 beschreibbar, 5 Legacy-Header, 6 übernommen, 7 schreibt, 8 eine ganze Tabelle gelesen, 9 ein Schreiben nach seinem ersten Frame abgeschnitten, 10 `BY HAND` |
| 5 | `LINE` | Low-Byte 0 geparkt, 1 hört zu; High-Byte der GPIO, 0xFF für keinen. Ein Bildschirm zeigt den aktiven Ausgang aus diesem Register. |
| 6 | `PORT` | der gehaltene Port, 0xFF wenn frei |
| 7 | `SES` | Low-Byte das letzte Ergebnis der Session (`OY_OK` 0, `BUSY` 1, `SILENT` 2, `NO_TABLE` 3, `NOT_KNOWN` 4, `CHANGED` 5, `NOT_TAKEN` 6, `PARTIAL` 7, `GUARD` 8, `ABORTED` 9), High-Byte die letzte Ablehnung des Guards |
| 8 | `BAD_INDEX` | der Index, den der Guard abgelehnt hat |
| 9 | `TYPE` | der Typcode der gelesenen Kopie, Index 11 |
| 10, 11 | `FW_LO`, `FW_HI` | ihre Firmware, Indizes 12 und 13: 103555 ist V1.03555 |
| 12, 13 | `SERIAL_LO`, `SERIAL_HI` | ihre Seriennummer, Indizes 14 und 15 |
| 14 | `ENTRIES` | Einträge der gelesenen Kopie, 0 vor einem Lesen |
| 15, 16 | `READS`, `READ_CRC` | vollständige Lesevorgänge, umlaufend bei 65535; `oy_guard_table_crc()` der gelesenen Kopie |
| 17 | `PROGRESS` | Low-Byte die schon gesehenen Indizes der Tabelle, die gelesen wird, High-Byte der Index, der geschrieben wird, 0xFF keiner |
| 18 | `T_MS` | ms in diesem Zustand, hält bei 65535 |
| 19 | `BLOCK` | der Block, den `YGE_DATA` zeigt |
| 20 | `SES_STATE` | Low-Byte der Zustand der Session, High-Byte warum die gelesene Kopie nur lesbar ist (der Ablehnungscode des Guards) |
| 21, 22 | `MISSES`, `DROPPED` | Anfragen und Schreibvorgänge ohne rechtzeitige Antwort; verworfene Frames. Beide halten bei 65535 |

`YGE_PROG` schreibt:

| Reg | Name | Wert |
|---|---|---|
| 0 | `CMD` | 1 OPEN, 2 CLOSE, 3 ABORT, 4 STOP, 5 CLEAR, 6 ENTER, 7 HAND_DONE, 8 READ, 9 WRITE, 10 LEAVE |
| 1 | `SEQ` | `SEQ` + 1 modulo 65536 |
| 2 | `KEY` | 0x59C7 für ENTER und WRITE, sonst 0 |
| 3 | `CRC` | `link_kst_crc()` der Register 4 bis 11, für OPEN, ENTER und WRITE |
| 4 | `PORT` | der Programmierport, 0 bis 3 |
| 5 | `POWER` | 0 `SWITCHED`, 1 `BY HAND` |
| 6, 7 | `MV`, `MA` | die Sollwerte der Versorgung |
| 8 | `BOOT_MS` | die Boot-Wartezeit, 1000 bis 20000ms |
| 9, 10 | `READS`, `READ_CRC` | die gelesene Kopie, aus der der Entwurf stammt |
| 11 | `DRAFT_CRC` | `oy_guard_table_crc()` der `ENTRIES` Werte des Entwurfs |
| 12 | `BLOCK` | nicht bereitgestellt; 0 bis 5 |

Die Register 0 bis 3 sind ein Frame. Eine SEQ ungleich `SEQ` + 1 ist
`BAD_VALUE`; der zuletzt genommene Frame, erneut gesendet, wird bestätigt und
tut nichts. Ein genommenes Kommando wird bestätigt, und was daraus wurde,
steht auf der Page: das High-Byte von `RESULT` sagt, warum es nichts begann,
oder 0. Ablehnungen: 1 `BUSY`, 2 `TARGET`, 3 `HELD`, 4 `ARMED`, 5 `UNSAFE`, 6
`NOT_OPEN`, 7 `NO_KEY`, 8 `STAGED`, 9 `STOPPED`, 10 `NO_PIN`, 11 `RANGE`, 12
`NO_SUPPLY`, 13 `STALE`, 14 `DRAFT`, 15 `WRITING`, 16 `SESSION`, 17
`SUPPLY_OFF`. Ergebnisse: 1 läuft, 2 erledigt, 3 `NO_LINE`, 4 `SUPPLY`, 5
`TIMEOUT`, 6 `CUT`, 7 `SESSION`, 8 `NOT_BACK`.

`YGE_DATA` sind 32 Register, je ein Wert der Tabelle, aus dem Block, den
`BLOCK` nennt. Der erste Block jedes Paars hält die Indizes 0 bis 31, der
zweite 32 bis 63.

| Block | Hält | Zugriff |
|---|---|---|
| 0, 1 | die gelesene Kopie | lesen |
| 2, 3 | der Entwurf | lesen, schreiben; `BUSY`, solange eine Operation läuft |
| 4, 5 | die zuletzt empfangene Tabelle, auch ein abgebrochenes Lesen | lesen |

WRITE braucht den Schlüssel, die CRC, die CRC des Entwurfs und die Werte
`READS` und `READ_CRC` des Lesens, auf dem der Entwurf beruht; eine Abweichung
gibt `STALE`. Die Regeln des Guards gelten zusätzlich: ein Entwurf, den er
ablehnt, ist ein Fehler. Ein Schreiben liest die Tabelle zuerst erneut und
endet mit `CHANGED`, ohne etwas zu senden, wenn sie von der gelesenen Kopie
abweicht; nach den Schreibvorgängen liest es die ganze Tabelle erneut, die mit
neuem `READS` zur gelesenen Kopie wird, ein Entwurf der früheren Tabelle ist
dann veraltet.

## Älter und neuer

| Panel | IOmcu | Was geschieht |
|---|---|---|
| jedes | 4.41 oder älter | beide Pages `BAD_PAGE`; `LINK_CAP_YGE` wird nicht gemeldet |
| 4.41 oder älter | 4.42 | es fasst die Pages nie an; nichts ist gehalten |

## Was nicht gemessen ist

Die Bench-Konsole gibt mit `yge` Zugriff
([Die Bench-Konsole](BenchConsole-de.md#die-yge-page)). Die Zeiten stammen aus
[OpenYGE Abschnitt 10](OpenYGE-de.md#10-session-timing) oder sind
Entscheidungen der Implementierung: 50ms in `STOPPING`, 250ms aus vor dem
Ein, 3000ms zum Lesen als aus und als ein und 60000ms für `BY HAND`. Die
Stationsschritte H1 und H2 ([OpenYGE Abschnitt
8](OpenYGE-de.md#8-offene-messungen)) prüfen die Zustände der Leitung und eine
ganze Session gegen den simulierten ESC des Pi; H3 bis H5 brauchen einen
YGE-ESC.
