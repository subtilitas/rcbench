---
screen: UPDATE
title: Panel-Update
order: 52
---

# Panel-Update

PANEL UPDATE schreibt ein neues Firmware-Image des Panels in dessen Flash. Der Bildschirm öffnet nach dem Halten von 2s auf INSTALL unter [WLAN](wifi.md) oder wenn ein Download unter [Updates](updates.md) seine Prüfungen bestanden hat, und er belegt den ganzen Bildschirm: kein Band und kein STOP. Hinter ihm lässt sich nichts auf ARMED schalten.

## Schreiben

![Das Image wird geschrieben](view:panel-update)

Das Halten schaltet zuerst das Funkmodul ab. Danach stellt das Panel seine Abfragen ein und hält den Heartbeat zurück; der IOmcu hält seine Ausgänge im Failsafe, wie bei jedem Neustart des Panels. Der Bildschirm schreibt den Slot, der nicht läuft, in Stücken von 64KiB und zeigt die geschriebenen Bytes und den Prozentwert. Was die Anzeige während jedes Löschens des Flash-Speichers zeigt, ist nicht gemessen. Schalte das Panel nicht aus.

Wie lange Löschen und Schreiben dauern, ist nicht gemessen.

## Geschrieben und geprüft

![Geschrieben und geprüft: das Panel startet neu](view:panel-update-done)

Ist das Schreiben fertig, wird der Slot zurückgelesen und geprüft, er wird zum Slot, der startet, und das Panel startet neu. Der Splash zeigt die neue Version, und das Band meldet `Panel-Firmware 0.22.1 installiert.`.

Der erste Start eines neuen Builds ist ein Probestart. Ohne Touch kehrt das Panel sofort zum vorigen Build zurück: kein Touch, kein STOP. Laufen Display und Touch, wird der Build behalten, sobald das Panel als Ganzes läuft: Karte versucht (keine Karte zählt), Einstellungen geladen, Steuer- und Logger-Task laufen, ein ganzes Bild auf dem Display. Auf den IOmcu wird nicht gewartet. 30s nach dem Start wird der Build behalten, sobald der Bildschirm läuft, was auch sonst fehlt. Das Behalten wartet, solange der Prüfstand ARMED oder der Netzteilausgang an ist. Ein Start, der vor dem Behalten neu startet, durch einen Fehler, einen Watchdog, einen Neustart oder einen Stromausfall, kehrt zum vorigen Build zurück, und das Band meldet `Panel-Firmware 0.22.1 ist nicht gestartet. Version 0.22.0 läuft wieder.`. Scheitert das Behalten eines guten Starts, meldet das Band `Panel-Firmware 0.22.1 wurde nicht übernommen. Beim nächsten Start läuft wieder die vorherige Version.`.

## Das Schreiben schlug fehl

![Das Schreiben schlug fehl](view:panel-update-failed)

Ein Schreiben, das fehlschlägt, ändert nichts an dem, was startet: Die laufende Firmware bleibt. SCHLIESSEN führt nach einem Upload zurück zu dem Bildschirm, der [WLAN](wifi.md) geöffnet hat, [SETUP](setup.md) oder [LOGS](logs.md), nach einem Download zu [Updates](updates.md) oder zum Menü.

## Aktuelle Einschränkungen

- Ein Stromausfall während des Probestarts verliert die Zeile im Band; das Panel kehrt trotzdem zum vorigen Build zurück.
- Ein Probestart, der auf dem Splash stehen bleibt, etwa in einem Einhängen der Karte, das nicht zurückkehrt, wird nicht behalten und startet nicht von selbst neu. Ein Neustart oder Stromausfall kehrt zum vorigen Build zurück.
- Vor dem ersten Update über WLAN braucht das Panel einmal einen Flash über USB (Universal Serial Bus), um die Aufteilung mit zwei Anwendungs-Slots zu bekommen: [Erste Inbetriebnahme](https://github.com/subtilitas/rcbench/blob/main/docs/FirstRun-de.md).
