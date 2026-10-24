---
screen: none
title: Sensoren
order: 62
---

# Sensoren

SENSOREN, die vierte Kategorie auf [SETUP](setup.md), stellt bis zu vier TI INA3221 ein, Dreikanal-Strommonitore am Sensorbus auf 0x40 bis 0x43, und sagt für jeden Kanal, was er misst. Der INA228 und die Pins des Busses bleiben unter [Anschlüsse](setup-interfaces.md). Die Einstellungen werden wie jede andere Einstellung auf SETUP gehalten und gespeichert.

## Die Bausteine

![Zwei Bausteine bestückt, 0x40 online](view:setup-sensors)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| 0x40 bestückt bis 0x43 bestückt | EIN, AUS | AUS | ein INA3221 an dieser Adresse; das DAOKAI-Modul nimmt 0x40 mit A0 nach GND gebrückt |

Jeder bestückte Baustein hat eine Überschrift, `INA3221 0x40` bis `INA3221 0x43`, und drei Zeilen für jeden seiner drei Kanäle. Ein nicht bestückter Baustein zeigt keine Zeilen.

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| 0x40 CH1 misst | KEINE, AUSGANG 1 bis AUSGANG 8, PORT 1 bis 4, NETZTEIL | AUSGANG 1 für 0x40 CH1, sonst KEINE | worin der Shunt des Kanals liegt: ein Ausgangskanal, ein Programmierport oder die Versorgungsschiene |
| 0x40 CH1 Shunt | AUS, 5,0 bis 1000,0mOhm, Schritte von 0,1mOhm | 100,0 | AUS: der Kanal wird nicht gelesen; 100mOhm misst bis 1,64A |
| 0x40 CH1 Grenze | AUS, 10 bis 32767mA, Schritte von 10mA mit den Pfeilen | AUS | das Band sagt, wenn der höchste Strom eines 50-ms-Fensters sie überschreitet; nur eine Meldung, nichts stoppt |

- `+` auf einem Shunt bei AUS gibt 5,0mOhm, `-` bei 5,0mOhm gibt AUS.
- Ein Tippen auf den Wert eines Shunts öffnet das Tastenfeld: 44,4mOhm vom Standardwert 100,0 sind `4`, `4`, `.`, `4` und OK, wo `-` 556 Schritte braucht. 0 ist AUS; 0,1 bis 4,9 wird abgelehnt: Das Tastenfeld bleibt offen und zeigt bis zur nächsten Taste `0 oder 5.0 bis 1000.0mOhm` statt des Bereichs.
- Der Kanal, der jede Millisekunde gelesen wird und dessen Strom der Bildschirm [SERVO](servo.md#das-messgerät-der-servoschiene) und der Servotest nutzen, ist der niedrigste Kanal mit Shunt an einem bestückten Baustein, der das Ziel des Servotesters misst: einen Ausgang, solange das Ziel OUTPUTS ist, PORT n, solange es Programmierport n ist.
- Das Panel schreibt die Einstellung 500ms nach der letzten Änderung, nur bei einem Prüfstand in SAFE. Die Grenzen gehen 500ms nach ihrer letzten Änderung in jedem Zustand an einen IOmcu mit Protokoll 4.27 oder neuer.

## Grenzen

![Eine Grenze bei einem IOmcu älter als 4.27](view:setup-sensors-limit)

Solange eine Grenze gesetzt ist, prüft der IOmcu jedes 50-ms-Fenster eines Kanals, und das Panel liest alle 200ms, welche Kanäle sie überschritten haben. Das Band sagt, höchstens einmal alle 5s und einmal alle 30s für einen Kanal, der sie weiter überschreitet. Kanäle, die gleichzeitig darüber liegen, kommen reihum dran: sind alle 12 darüber, wird jeder innerhalb von 60s gesagt.

| Band | Bedeutung |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) über der Grenze von 0.80A.` | ein Fenster hat die Grenze des Kanals überschritten; in Klammern, was der Kanal misst; die Grenze, an der das Fenster geprüft wurde |
| `Ein INA3221-Kanal hat seine Grenze überschritten; welcher, ging auf dem Link verloren.` | die Antwort, die den Kanal nannte, ging verloren; gesagt nach dem nächsten Link-Aufbau, nicht nach einem Neustart des IOmcu |
| `INA3221-Grenzen abgelehnt. SETUP > SENSOREN prüfen.` | der IOmcu hat die Grenzen abgelehnt und behält seine, oder er hat die Page abgelehnt |

Eine Zeile GRENZE, die der IOmcu nicht hält, steht in der Warnfarbe: solange ihr Schreiben wartet, nach einer Ablehnung und bei einem IOmcu älter als 4.27, der keine Grenze prüft (`IOmcu älter als 4.27: keine Grenze wird geprüft.`).

![Das Tastenfeld von 0x40 CH1 Shunt, 44,4 getippt](view:setup-sensors-keypad)

![3mOhm abgelehnt, der Grund statt des Bereichs](view:setup-sensors-why)

## Was ein Baustein tut

Rechts in der Überschrift eines Bausteins steht, was er tut:

| Zustand | Bedeutung |
|---|---|
| `ONLINE` | der IOmcu liest den Baustein |
| `SUCHT DEN BAUSTEIN` | die Einstellung ist angenommen; die ersten 2,5s |
| `ANTWORTET NICHT` | an der Adresse antwortet nichts |
| `ANDERE ID ANTWORTET` | dort antwortet etwas anderes |
| `NOCH NICHT GESCHRIEBEN` | eine Änderung wartet auf ihr Schreiben |
| `NOCH NICHT GELESEN` | noch antwortet kein IOmcu |
| `VOM IOMCU NICHT ÜBERNOMMEN` | der IOmcu hat die Einstellung abgelehnt |
| `ALLE KANÄLE AUS` | alle drei Shunts stehen auf AUS; nichts wird geschrieben |
| `INA228 AUF DIESER ADRESSE` | der INA228 ist auf dieser Adresse eingeschaltet; keiner wird gelesen |
| `IOMCU ÄLTER ALS 4.25: NICHT GELESEN` | ein älterer IOmcu liest nur einen Baustein |
| `SENSOR-SDA ODER -SCL NICHT FESTGELEGT` | ANSCHLÜSSE legt keinen Sensorbus fest; nichts wird gelesen |

![Ein IOmcu älter als 4.25](view:setup-sensors-old)

Die Zeilen eines Bausteins, den der IOmcu nicht liest, stehen in der Warnfarbe, mit dem Grund statt der Hilfezeile. Ein IOmcu älter als 4.25 liest einen INA3221: den Baustein des Kanals, der das Ziel des Servotesters misst, sonst den niedrigsten bestückten.

Ein Panel, das von 0.21 aktualisiert wurde, nimmt seine erste Einstellung von dem einen INA3221, den es hatte: der Baustein an der alten Adresse bestückt, sein CH1 auf dem alten Shunt und misst AUSGANG 1, oder PORT 1, wenn das Ziel des Servotesters der Programmierport war; jede Grenze AUS. Das nächste SPEICHERN behält sie.

## Verwandte Seiten

- [Setup](setup.md)
- [Anschlüsse](setup-interfaces.md)
- [Servo](servo.md)
