# Die Pages für ESC-Einstellungen

<sub>[English](LinkEsc.md) · **Deutsch**</sub>

Diese Seite ist ein Abschnitt von [Der Link](Link-de.md), wo Verkabelung,
Frame-Format, Page-Übersicht und die anderen Pages stehen. ESC ist electronic speed
controller, PD power delivery, GPIO general-purpose input/output, CRC cyclic
redundancy check.

`ESC_WIRE` (0x3A) und `ESC_DATA` (0x3B), neu in 4.34, lesen und schreiben die
Einstellungen eines ESC über seinen Signaldraht. Sie halten eine Session von
[`protocols/escwire`](../protocols/escwire/README.md) auf dem Pin eines Throttle-Slots
oder eines DShot-Programmierports. `shared/outputs/esc_port.c` hält die Regeln. Ein
IOmcu, der sie bedient, meldet `LINK_CAP_PROGRAM` (Bit 8).

Es gibt keine rohe Transaktion. Der IOmcu hält die gelesene Kopie und beurteilt jeden
Frame ([der Guard](../protocols/escwire/README.md)). Ein Panel nennt eine Operation
und übergibt einen Entwurf.

## Welcher Pin

| Ziel | Genommen, wenn |
|---|---|
| Ausgangs-Slot 0 bis 7 | sein Treiber gesetzt ist, er gebunden ist und sein erster Kanal die Throttle-Rolle hat (DShot, bidirektionales DShot, PPM oder Servo-PWM) |
| Programmierport 0 bis 3 | sein FUNC DShot ist und der IOmcu seinen Pin hält |

Jeder andere Slot oder Port wird mit `NO_PIN` abgelehnt. Nur dieser Pin wechselt:
andere Throttle-Pins bleiben gebunden und im Leerlauf.

## Der Weg hin und zurück

| Schritt | Was geschieht |
|---|---|
| OPEN | Abgelehnt, solange ARMED oder ARM angefragt ist, solange KST, HITEC oder ein Firmware-Update die Bench hat, oder ohne vertrauten Heartbeat oder bei stillem Link. Ab diesem Frame armt die Bank nicht, CONTROL lehnt ein ARM ungleich 0 ab, SUPPLY und PROG_SUPPLY sind gehalten, PROG, OUTPUTS, CHAN_CFG, BIND, das Set-up von TONE und die Verdrahtung von SUPPLY (ENABLE bis BAUD) werden mit `BUSY` abgelehnt, und KST und HITEC lehnen jeden Port ab. Ebenfalls abgelehnt (`HELD`) mit einer gespeicherten, noch nicht angebundenen PD-mini-Verdrahtung. |
| `STOPPING`, 50ms | Der Ausgang gibt nichts aus. ABORT, ein Stopp oder ein verlorener Link geben hier den Pin frei, ohne etwas anzufassen. |
| `OPEN` | Der Ausgang lässt den Pin los, der Pin ist reserviert, die Leitung öffnet geparkt. Keine State Machine, kein Programmspeicher oder keine 2 DMA-Kanäle (direct memory access): `NO_LINE`, der Pin geht zurück, die Page ist frei. |
| CLOSE | Mit `WRITING` abgelehnt, solange ein Schreiben läuft und 150ms nach seinem letzten Frame. Die Leitung schließt, der Pin geht zurück, wie sein Ausgang ihn hatte; kein anderer Slot oder Port wechselt. Slot und Port wurden nicht beschrieben, Rate, Treiber und Rolle sind unverändert. Ein Ausgang, der den Pin nicht zurücknimmt, endet mit `NOT_BACK`: die Page bleibt in Zustand 9 gehalten, mit gesetztem Quiet-Bit, ARM bleibt also abgelehnt, und die Bank kann ohne Signal auf diesem Pin nicht armen. Ein CLOSE versucht es erneut; ein IOmcu-Neustart löst es. |

## Eintritt

`ENTER` staged die Versorgung (`SWITCHED` oder `BY HAND`), Spannung, Grenze, Hold und
Auszeit.

| `SWITCHED`, am PD mini | Leitung | Versorgung |
|---|---|---|
| `POWER_OFF`: mindestens 250ms, innerhalb von 3000ms als aus gelesen, dann die Auszeit (0 bis 10000ms) | geparkt | aus |
| `LEAD`, 50ms | high getrieben | aus |
| `POWER_ON`: bis der PD mini als ein liest, höchstens 3000ms | high getrieben | ein angefragt, mit 3000 bis 20000mV und 50 bis 3000mA, mindestens 700mV unter dem Eingang |
| `RUN`: der Hold (500 bis 120000ms), gezählt ab dem Lesen als ein, dann First Contact oder `info` | high getrieben, dann der Draht | ein |

| `BY HAND`, an der eigenen Versorgung des ESC | Leitung | Versorgung |
|---|---|---|
| `HAND_WAIT`, bis zu 60000ms | high getrieben, in einen ESC ohne Versorgung, falls verbunden; der Strom ist nicht gemessen | nicht angefasst |
| DONE, dann `RUN` wie oben | | nicht angefasst |

Eine ESC-Session versorgt den ESC auf jedem Board aus dem PD mini, auch wo es PROG_SUPPLY
gibt. Sie schaltet PROG_SUPPLY nie ein: von OPEN bis zur Rückgabe wird ein ON des Panels
dort mit `BAD_VALUE` abgelehnt, und ein Fehler, STOP oder ABORT schaltet es mit dem PD mini aus. Ein CLOSE ohne diese lässt es, wie das Panel es hat. `READY`
folgt, wenn ein ESC geantwortet hat.

## Was jedes Ereignis tut

| Ereignis | Leitung | Versorgung | Danach |
|---|---|---|---|
| ARM angefragt, Page frei | die des Ausgangs | wie sie ist | ein späteres OPEN wird abgelehnt, solange es steht |
| ARM angefragt, Page gehalten | keine Änderung | keine Änderung | ein ARM ungleich 0 wird von OPEN bis zur Rückgabe mit `NOT_ARMED` abgelehnt, und das Bit der Page in der Quiet-Maske (das eines Slots ist Bit 4) hält `safety_gate_arm_now()` zusätzlich unten. Die Rückgabe setzt ARM und THROTTLE auf 0: das nächste ARM ist eine neue Anfrage, und STATUS liest bis dahin IDLE |
| STOP (eine Stopp-Flanke oder die Operation STOP), kein Schreiben | sofort geparkt | PD mini und PROG_SUPPLY aus | gehalten, `STOPPED` |
| STOP, in einem Schreiben | wie das Schreiben es braucht, bis zum Rücklesen, dann geparkt | PROG_SUPPLY sofort aus; beim PD mini wartet das OFF des Panels, höchstens 10000ms, dann aus am Ende des Rücklesens, ohne die 150ms: nach der Spezifikation ist die Page mit ihrem `0x30` und dem `OK` eines `save` im Flash | gehalten, `STOPPED`; das Ergebnis sagt, was das Schreiben getan hat |
| ABORT | sofort geparkt, auch in einem Schreiben | PD mini und PROG_SUPPLY aus | gehalten |
| Heartbeat verloren oder Link 200ms still | sofort geparkt, auch in einem Schreiben | sofort aus | gehalten, `STOPPED` |
| Fehler | sofort geparkt | PD mini und PROG_SUPPLY aus | gehalten, `FAULT` und `STOPPED` |
| IOmcu-Neustart | der Reset-Zustand des Chips | aus beim Booten | frei; der Slot bindet aus dem Flash |
| Die Versorgung aus, still oder nach dem Ein als aus gelesen, in `RUN` oder `READY` | sofort geparkt | aus | gehalten, `FAULT`, Ergebnis `SUPPLY` |

Ein Schreiben, das ein verlorener Link zwischen Erase und Program abschneidet,
hinterlässt die Page gelöscht. Siehe
[die Tabelle der Session](../protocols/escwire/README.md).

Geparkt ist ein Eingang mit Pull-down und abgeschaltetem Puffer. Kein Zustand oben
sendet ein Throttle-Signal: solange die Page den Pin hält, hat der Ausgang ihn
losgelassen, ein Apply lässt diesen Slot ungebunden (`esc_port_may_bind()`), und das
Modul hat keine Funktion, die ein Pulsmuster erzeugt.

Ein DISARM ist kein Stopp: da ARM ab OPEN abgelehnt wird, ist nichts geARMt, das es beenden könnte. Das STOP des Panels ist die Operation STOP; eine Failsafe-Flanke ist von selbst ein Stopp.

**Was als Fehler zählt.** Jede Operation der Session, die anders als `EW_SES_OK`
endet, darunter `EW_SES_ERR_SILENT`, `NOT_KNOWN`, `READ`, `CHANGED`, `CLIPPED`, `TORN`
und `LOST`; eine Ablehnung eines Entwurfs durch den Guard; eine Versorgung, die
ausfällt; ein abgelaufenes Warten bei `BY HAND`. Ein Befehl, den die Session vor jedem
Frame ablehnt (`EW_SES_ERR_NO_READ`, `MUST_READ`, `DIRTY`, `ERASED`, `BACKUP`,
`NOT_WRITABLE`, `STATE`), ist eine Ablehnung dieses Befehls und ändert nichts.

**Nach `TORN` und `LOST`.** Die Session selbst hält einen ESC nach diesen beiden im
Modus, für eine Reparatur in derselben Session. Die Page nicht: sie bricht die
Session ab, was die Leitung parkt, und schaltet die Versorgung aus, sodass der ESC
seine Versorgung verliert. Eine Reparatur ist CLEAR, ENTER und READ. Was ein
Aus-und-Ein mit einer halb geschriebenen Page macht, steht in den Worten der
Spezifikation: ein ESC der Familie S oder Bluejay schreibt Defaults neu und seine 48
Tag-Bytes lesen 0xFF, was ihn hier nur lesbar macht; ein AM32 mit gelöschten
Einstellungen bleibt in seinem Bootloader, und nur RESTORE schreibt ihn. Nicht an einem
ESC gemessen. Bei `BY HAND` kann die Bench die eigene Versorgung des ESC nicht trennen:
seine Leitung ist geparkt, und ein Mensch trennt ihn.

**Wer den Rest schaltet.** Die Bank ist SAFE, solange die Page gehalten ist; die
Leitung der Page, der PD mini und PROG_SUPPLY sind alles, was der IOmcu an hat.
`LINK_EW_F_FAULT` sagt es dem Panel, das dem in den SAFE-Zustand der Bench folgt.

## Die Hälfte des Panels

`shared/bench/esc_link.c` fragt und folgt; es läuft auf jedem Panel.
[ESC-Signalleitung](EscWire-de.md) beschreibt den Screen; der Alert unten erscheint auf jedem Screen.

| Ereignis | Was das Panel tut |
|---|---|
| Register 0 bis 20 | alle 5ms in einer Anfrage, 100ms bei gehaltenem Pin, 500ms sonst |
| Erstes Lesen nach einem Panel-Neustart, ein Pin gehalten | CLOSE; erneut alle 1000ms, solange die Page `WRITING` antwortet oder in `NOT_BACK` bleibt. Keine Anfrage, bis die Page frei ist. |
| Link zurück, kein Neustart | nichts wird geschlossen |
| Die STOP-Taste oder jeder Stop des Panels | die Operation `STOP`, sobald die Page bekannt ist und einen Pin hält; ein Stop in einer Link-Lücke geht danach. Ein `BAD_VALUE`, eine SEQ eines anderen Schreibers: lesen und erneut, höchstens 6 Mal. |
| `FAULT` steigt, oder der Zustand erreicht `NOT_BACK` | ein Fehler: alle Ausgänge aus (`prog_faults_follow()`). Ein `FAULT`, der beim ersten Lesen nach Link-Aufbau schon steht, zählt nicht. |
| Der Zustand erreicht `NOT_BACK` | ein Alert, der Slot oder Port nennt |
| Ein Pin gehalten, von wem auch immer | ARM auf MOTOR und SERVO, die TARGET-Zeile des Motors, ESC STICK, der Limit-Lauf und der Screenshot warten; ein angefragtes, abgelehntes ARM entfällt auf MOTOR und SERVO |
| Ein Stop eingerastet | kein Befehl außer CLOSE, LEAVE und ABORT geht (`esc_prog_cmd_allowed()`) |
| 10 Lesezugriffe der Page in Folge fehlgeschlagen, in einer Anfrage | die Anfrage endet. Mit einem Halten im letzten Lesen: ein Fehler, und `STOP` |
| Eine Lesekopie mit neuem `READS` oder `READ_CRC` | in Blöcken geholt und gegen `READ_CRC` geprüft, 3 Versuche |

Die Hälfte der SUPPLY-Page (`shared/bench/supply_link.c`) unter einem Halten:

| Ereignis | Was das Panel tut |
|---|---|
| Sein erster Schreibzugriff nach einem Start | keiner vor einem Lesen. Ein dort gefundenes Halten wird übernommen; kein eigenes OFF geht darüber. Das OFF des Bedieners und der Cut eines Fehlers bleiben. |
| Seine Verdrahtung, die Page antwortet `BUSY` | keine Ablehnung: die Verdrahtung der Page gilt, und sie wird erneut geschrieben, wenn ein Lesen das Ende des Haltens zeigt |
| Ein Halten zu Ende, seine Verdrahtung nicht geschrieben | OFF, Lesen, Verdrahtung, wie bei jedem Start; kein ON gilt als verloren |
| Eine abgelehnte Verdrahtung, nicht `BUSY`, bei einem ON, das ein Port hält | sofort `ON_REFUSED`: ein Fehler, alle Ausgänge aus |

## Aktuelle Einschränkungen

- **Ein zurückgekehrter Link behält das Halten.** Bedingung: der Link war über 200ms still,
  ohne Panel-Neustart. Die Page bleibt `OPEN` und `STOPPED` mit gesetztem Quiet-Bit, die Bank
  kann also nicht armen. Das ist die sichere Seite. Heraus führen CLOSE auf dem Screen, `esc close`
  auf der Bench-Konsole oder ein IOmcu- oder Panel-Neustart.
- **Ein von der Bench-Konsole gehaltener Pin geht mit einem Panel-Neustart.** Bedingung:
  `esc open` auf der Konsole, dann ein Panel-Neustart. Das Panel unterscheidet ihn nicht vom
  eigenen.
- **Ein Fehler in einem Schreiben kostet die Reparatur in derselben Session.**
  Bedingung: ein Schreiben, das mit `TORN` oder `LOST` endet. Die Versorgung geht sofort
  aus, der ESC verliert also seine Versorgung mit halb geschriebener Page. Nach der
  Spezifikation, nicht gemessen: ein ESC der Familie S oder Bluejay schreibt seine
  Defaults neu und verliert seine 48 Tag-Bytes (0x40 bis 0x6F lesen 0xFF), und die Page
  ist hier von da an nur lesbar; die Tags kommen über diese Page nicht zurück. Ein AM32
  bleibt gelöscht (Byte 0 liest 0xFF) in seinem Bootloader, bis RESTORE ein Backup
  schreibt. Bei `BY HAND` behält der ESC seine eigene Versorgung, bis ein Mensch ihn
  trennt.
- **Ein Pin mit bidirektionalem DShot ruht auf SAFE high.** Bedingung: ein Slot oder
  Port, gebunden als bidirektionales DShot, die Bank SAFE und der ESC versorgt. Der
  Pin ruht auf seinem Pull-up (`out_dshot_stop()`), und eine Leitung, die nach
  Signalverlust high ist, ist die Eintrittsbedingung der Bootloader von Familie S,
  Bluejay und AM32. Ein solcher ESC kann nach einem SAFE in seinem Bootloader sitzen,
  mit oder ohne diese Page. Was das nächste ARM dann tut, ist nicht gemessen. Plain
  DShot und PPM ruhen low. Die Page ändert das nicht.
- **Eine Leitung mit Last.** Der Pull-up ist der des Pads, 50 bis 80kOhm. Eine Leitung,
  die eine Last unten hält, etwa der Signaldraht eines Servos, bleibt darauf low, und
  ein First Contact schlägt fehl. Ob er eine Open-Drain-Antwort mit 52us pro Bit hebt,
  ist nicht gemessen.
- **Ein Firmware-Update**, das startet, während die Page gehalten ist, zählt als Stopp.

## Register

`ESC_WIRE` liest:

| Reg | Name | Wert |
|---|---|---|
| 0 | `STATE` | 0 `FREE`, 1 `STOPPING`, 2 `OPEN`, 3 `POWER_OFF`, 4 `LEAD`, 5 `HAND_WAIT`, 6 `RUN`, 7 `READY`, 8 `POWER_ON`, 9 `NOT_BACK` |
| 1 | `SEQ` | die zuletzt genommene SEQ |
| 2 | `OP` | die letzte Operation |
| 3 | `RESULT` | Low-Byte das Ergebnis, High-Byte die Ablehnung des letzten Befehls |
| 4 | `FLAGS` | Bit 0 `STOPPED`, 1 `FAULT`, 2 diese Session hat die Versorgung eingeschaltet, 3 ein PD mini antwortet, 4 schreibbar, 5 AM32-Einstellungen gelöscht, 6 AM32 für CAN-Boards (Controller Area Network) gebaut, 7 im Bootloader, 8 SUPPLY hält das OFF des Panels zurück, 9 eine Page halb geschrieben, 10 eine ganze Page gelesen, 11 ein Text-`save` nicht bewiesen |
| 5 | `LINE` | Low-Byte 0 geparkt, 1 high getrieben, 2 Pull-up, 3 low getrieben, 4 eine Transaktion; High-Byte der GPIO, 0xFF für keinen. Ein Screen zeigt den aktiven Ausgang aus diesem Register. |
| 6 | `TARGET` | wie gestaged; 0 wenn frei |
| 7 | `SES` | Low-Byte das letzte Ergebnis der Session, High-Byte die letzte Ablehnung des Guards |
| 8 | `ID` | Low-Byte die Firmware, High-Byte Layout oder Revision |
| 9 | `WHY` | Low-Byte warum nicht geschrieben oder abgelehnt, High-Byte der Prozessor |
| 10 | `SIGNATURE` | die eines EFM8 |
| 11 | `LEN` | Bytes der Page: 112, 192, 255 oder 92 |
| 12, 13 | `BAD_OFF`, `CLIPPED` | das Byte, das der Guard ablehnte; was ein begrenzendes `set` speicherte |
| 14 | `TRIES` | Low-Byte begonnene Schreibvorgänge, High-Byte die letzte beurteilte Antwort |
| 15, 16 | `READS`, `READ_CRC` | Lesevorgänge dieser Session; `link_crc()` der `LEN` Bytes der gelesenen Kopie |
| 17 | `T_MS` | ms in diesem Zustand, stoppt bei 65535 |
| 18, 19 | `CONTACT`, `PROTOCOL` | AM32: Pin-Code und Flash-Code; Protokoll des Bootloaders |
| 20 | `BLOCK` | der Block, den `ESC_DATA` zeigt |

`ESC_WIRE` schreibt:

| Reg | Name | Wert |
|---|---|---|
| 0 | `CMD` | 1 OPEN, 2 CLOSE, 3 ABORT, 4 STOP, 5 CLEAR, 6 ENTER, 7 DONE, 8 READ, 9 WRITE, 10 RESTORE, 11 LEAVE |
| 1 | `SEQ` | `SEQ` + 1 modulo 65536 |
| 2 | `KEY` | 0x45C3 für WRITE und RESTORE, sonst 0 |
| 3 | `CRC` | `link_kst_crc()` der Register 4 bis 13, für OPEN, ENTER, WRITE und RESTORE |
| 4 | `TARGET` | ein Slot, oder 0x0100 und ein Port |
| 5 | `POWER` | 0 `SWITCHED`, 1 `BY HAND` |
| 6, 7 | `MV`, `MA` | die Sollwerte der Versorgung |
| 8, 9 | `HOLD_LO`, `HOLD_HI` | der Hold, ms |
| 10 | `OFF_MS` | die Auszeit, ms |
| 11, 12 | `READS`, `READ_CRC` | die gelesene Kopie, aus der der Entwurf stammt |
| 13 | `DRAFT_CRC` | `link_crc()` der `LEN` Bytes des Entwurfs |
| 14 | `BLOCK` | nicht gestaged; 0 bis 19 |

Register 0 bis 3 sind ein Frame. Eine andere SEQ als `SEQ` + 1 ist `BAD_VALUE`; der
gerade genommene Frame, erneut gesendet, wird bestätigt und tut nichts. Ein genommener
Befehl wird bestätigt, und was daraus wurde, steht auf der Page: das High-Byte von
`RESULT` sagt, warum er nichts begann, oder 0. Ablehnungen: 1 `BUSY`, 2 `TARGET`, 3
`HELD`, 4 `ARMED`, 5 `UNSAFE`, 6 `NOT_OPEN`, 7 `NO_KEY`, 8 `STAGED`, 9 `STOPPED`, 10
`NO_PIN`, 11 `RANGE`, 12 `NO_SUPPLY`, 13 `STALE`, 14 `DRAFT`, 15 `WRITING`, 16
`SESSION`, 17 `SUPPLY_OFF`. Ergebnisse: 1 läuft, 2 fertig, 3 `NO_LINE`, 4 `SUPPLY`, 5 `TIMEOUT`, 6
`CUT`, 7 `SESSION`, 8 `NOT_BACK`.

`ESC_DATA` sind 32 Register, 64B, Low-Byte zuerst, des Blocks, den `BLOCK` nennt:

| Block | Hält | Zugriff |
|---|---|---|
| 0 bis 3 | die Page wie gelesen | lesen |
| 4 bis 7 | der Entwurf | lesen, schreiben; `BUSY`, solange eine Operation läuft |
| 8 bis 11 | die Page wie zuletzt gelesen, auch nach einem fehlgeschlagenen Schreiben | lesen |
| 12 | der Name oder das Text-Target, endet auf 0 | lesen |
| 13, 14 | `info`, 106B: ein Byte, das sagt, ob die Revision gelesen ist, die Revision, 16 Bit Sub-Revision, das Target (30B), dann je 12B Temperatur, zweite Temperatur, Spannung, Strom, Ladung und eRPM, jedes endet auf 0 | lesen |
| 15 bis 18 | `music`, endet auf 0 | lesen |
| 19 | die Identität für RESTORE: Pin-Code, Flash-Code, Protokoll, 0, der Name, endet auf 0 | lesen, schreiben |

Eine ESCape32-Page sind ihre 46 Werte als 16-Bit-Integer in der Reihenfolge der Map.

WRITE braucht den Key, die CRC, die CRC des Entwurfs und `READS` und `READ_CRC` des
Lesens, aus dem er stammt (sonst `STALE`), und die Regeln der Session. Ein Schreiben, das die Page geändert hat, bewegt `READS` und `READ_CRC`, auch unter einem Stopp: ein Entwurf der Page davor ist veraltet. RESTORE braucht
dasselbe außer `READS` und `READ_CRC`.

## Älter und neuer

| Panel | IOmcu | Was geschieht |
|---|---|---|
| jedes | 4.33 oder älter | beide Pages `BAD_PAGE`; `LINK_CAP_PROGRAM` wird nicht gemeldet |
| 4.33 oder älter | 4.34 | es fasst die Pages nie an; nichts ist gehalten |

Der Screen [ESC-Signalleitung](EscWire-de.md) des Panels treibt beide Pages.

## Was nicht gemessen ist

An der Bench-Konsole treibt `esc` sie
([Die Bench-Konsole](BenchConsole-de.md#die-esc-einstellungs-page)). Ihre Zeiten kommen
aus der Spezifikation oder sind die Wahl dieses Moduls
([`protocols/escwire`](../protocols/escwire/README.md)): Eintritts-Hold, Auszeit, 50ms
vor dem Ein und 60000ms bei `BY HAND` sind gewählt. Der Strom, den eine high getriebene
Leitung in einen unversorgten ESC speist, ist nicht gemessen, ebenso wenig der Zustand
der Endstufe in seinem Bootloader.

Aus einer Verhaltensspezifikation geschrieben, ohne Zugriff auf die Firmware-Quellen.
