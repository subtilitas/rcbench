---
screen: UPDATE
title: Panel-Update
order: 52
---

# Panel-Update

PANEL UPDATE schreibt ein neues Firmware-Image des Panels in den Flash-Speicher. Die Seite öffnet sich nach 2s Halten auf INSTALLIEREN unter [WLAN](wifi.md) oder nachdem ein Download unter [Updates](updates.md) seine Prüfungen bestanden hat. Sie belegt die gesamte Anzeige, ohne Statusband und ohne STOP. Solange sie geöffnet ist, lässt sich der Prüfstand nicht auf ARMED schalten.

## Schreiben

![Das Image wird geschrieben](view:panel-update)

Das Halten schaltet zuerst das Funkmodul aus. Danach stellt das Panel seine Abfragen ein und sendet keinen Heartbeat. Dadurch bleiben die Ausgänge des IOmcu im Failsafe, wie bei jedem Panel-Neustart. Das Panel schreibt den inaktiven Slot in Stücken von 64KiB und zeigt die geschriebenen Bytes und den Fortschritt in Prozent. Das Anzeigeverhalten während der einzelnen Flash-Löschvorgänge ist nicht gemessen. Schalte das Panel nicht aus.

Wie lange Löschen und Schreiben dauern, ist nicht gemessen.

## Geschrieben und geprüft

![Geschrieben und geprüft: das Panel startet neu](view:panel-update-done)

Nach dem Schreiben liest das Panel den Slot zurück, prüft ihn und wählt ihn für den nächsten Start. Anschließend startet es neu. Der Splash zeigt die neue Version, und das Band meldet `Panel-Firmware 0.22.1 installiert.`.

Der erste Start eines neuen Builds ist ein Probestart. Ist Touch nicht verfügbar, stellt das Panel sofort den vorigen Build wieder her, da STOP nicht bedienbar ist. Wenn Display und Touch funktionieren, wird der Build dauerhaft übernommen, sobald die Karte geprüft, die Einstellungen geladen, Steuer- und Logger-Task gestartet und ein vollständiges Bild angezeigt sind. Auch eine fehlende Karte zählt als abgeschlossene Kartenprüfung. Das Panel wartet nicht auf den IOmcu.

Nach 30s wird der Build übernommen, sobald die Anzeige läuft, auch wenn andere Komponenten fehlen. Die Übernahme wartet, solange der Prüfstand ARMED oder der Netzteilausgang eingeschaltet ist.

Ein Fehler, Watchdog-Reset, Neustart oder Stromausfall vor der Übernahme stellt den vorigen Build wieder her. Das Band meldet dann `Panel-Firmware 0.22.1 ist nicht gestartet. Version 0.22.0 läuft wieder.`. Scheitert die Übernahme eines funktionierenden Builds, erscheint `Panel-Firmware 0.22.1 wurde nicht übernommen. Beim nächsten Start läuft wieder die vorherige Version.`. Vor dem Neustart speichert die Installation die geschriebene Version im NVS (nichtflüchtiger Speicher). Dadurch übersteht die Meldung `ist nicht gestartet` einen Neustart, Fehler oder Stromausfall während des Probestarts und erscheint einmal.

## Das Schreiben schlug fehl

![Das Schreiben schlug fehl](view:panel-update-failed)

Ein fehlgeschlagener Schreibvorgang ändert die Startauswahl nicht; die bestehende Firmware läuft weiter. Nach einem Upload führt SCHLIESSEN zu der Seite zurück, die [WLAN](wifi.md) geöffnet hat: [SETUP](setup.md), [LOGS](logs.md) oder [Updates](updates.md). Nach einem Download führt es zu [Updates](updates.md) oder zum Menü.

## Aktuelle Einschränkungen

- Kann die Installation ihre Neustartnotiz nicht im NVS (nichtflüchtiger Speicher) speichern, meldet die Konsole `restart note not kept`. Bei einer Rückkehr von diesem Build zur vorherigen Version erscheint dann keine Meldung im Band.
- Beim ersten Start nach einer Installation aus Version 0.23.2 oder älter liegt keine Notiz im NVS vor. Diese Builds speichern sie im LP-RAM (Low-Power Random-Access Memory).
- Bleibt ein Probestart auf dem Splash stehen, etwa weil das Einhängen der Karte nicht zurückkehrt, wird der Build weder übernommen noch automatisch neu gestartet. Ein Neustart oder Stromausfall stellt den vorigen Build wieder her.
- Flashe das Panel vor dem ersten Update über WLAN einmal über USB (Universal Serial Bus), um die Aufteilung mit zwei Anwendungs-Slots zu installieren. Siehe [Erste Inbetriebnahme](https://github.com/subtilitas/rcbench/blob/main/docs/FirstRun-de.md).
