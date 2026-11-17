---
screen: COPROC_UPDATE
title: IOmcu-Update
order: 100
---

# IOmcu-Update

Jedes Firmware-Image des Panels enthält ein IOmcu-Image. Das Panel bietet es an, wenn auf dem verbundenen IOmcu ein anderer Build läuft. Die Prüfung erfolgt bei jedem Verbindungsaufbau über den CAN-Bus (Controller Area Network). Sie vergleicht SHA-256-Prüfsummen (Secure Hash Algorithm, 256 Bit) statt Versionsnummern.

## Das Angebot

![UPDATES, von einem Angebot geöffnet](view:updates-offer)

Ein Angebot öffnet [Updates](updates.md) mit Schritt 2, IOMCU, als aktuellem Schritt, einmal je Angebot. Es öffnet sich im [Menü](overview.md), wenn der Prüfstand SAFE ist, Netzteilausgang und WLAN-Funkmodul ausgeschaltet sind und kein Panel-Update läuft. Das Band meldet `IOmcu nutzt einen anderen Build. Aktualisiere ihn auf UPDATES.`. Hält eine dieser Bedingungen das Öffnen zurück, nennt das Band sie stattdessen, etwa `IOmcu nutzt einen anderen Build. UPDATES öffnet, sobald das WLAN des Panels aus ist.`, und erneut, wenn das Öffnen auf etwas anderes wartet. Wartet nach einem Neustart ein Update-Lauf darauf, UPDATES zu öffnen, zeigt das Band nur die Zeile des Laufs, und UPDATES öffnet für beide einmal.

Die Karte des Schritts zeigt die laufende Version des IOmcu und die im Panel enthaltene Version. Ein älteres Image wird ebenfalls angeboten. Die Karte zeigt dann `Die Firmware auf diesem Panel ist älter.` in der Warnfarbe.

| Bedienelement | Funktion |
|---|---|
| 2s HALTEN ZUM IOMCU-UPDATE | startet das Update nach 2s Halten, mit derselben Geste und demselben Übergang wie ARM |
| SCHRITT ÜBERSPRINGEN | lehnt das Angebot ab; bis zum Neustart bietet das Panel diese Version nicht erneut an. Du kannst das Update weiterhin unter [Einzelne Updates](updates.md#der-iomcu) starten. |

Das Home-Feld, das UPDATES verlässt, lehnt das Angebot ebenso wie SCHRITT ÜBERSPRINGEN ab. Über OPTIONEN, EINZELNE UPDATES oder der Geräteseite schließt es zuerst diese Seite und lehnt nichts ab. Öffnet das Panel eine andere Seite über UPDATES, gilt das nicht als Ablehnung. Das betrifft die [Fehlerseite](busfault.md) bei einer verlorenen Verbindung und den Download eines Panel-Updates. Beim nächsten Verbindungsaufbau wird das Update erneut angeboten, und UPDATES öffnet sich im Menü, sobald dieselben Bedingungen erfüllt sind. Ein neues Angebot holt einen übersprungenen Schritt 2 zurück und beendet ein laufendes Halten darauf.

## Der Fortschritt

![Firmware wird gesendet.](view:coproc-update-progress)

Nach vollständigem Halten übernimmt das Update das Panel. Die Verbindung wird unterbrochen, und die Ausgänge sind ausgeschaltet. Der IOmcu lehnt ARM bis zum Ende des Updates ab. Die Fortschrittsanzeige belegt den ganzen Bildschirm, ohne Statusband und ohne STOP.

| Schritt | Vorgang |
|---|---|
| `Firmware wird vorbereitet.` | Das Panel berechnet die Prüfsumme des Images, 8KiB je Schritt. |
| `Firmware wird gesendet.` | Das Panel sendet Blöcke von je 64 Bytes und zeigt den Fortschritt, etwa `Block 1497 von 2994, 50%`. |
| `IOmcu prüft die Firmware.` | Der IOmcu berechnet die Prüfsumme des beschriebenen Slots. |
| `IOmcu startet die neue Firmware.` | Der IOmcu startet den neuen Slot auf Probe. |
| `Neue Firmware wird dauerhaft übernommen.` | Das Panel liest die Identität des neuen Images aus und übernimmt es dauerhaft. |

Die Übertragungsdauer ist nicht gemessen.

## Wie es endete

Nach dem Update kehrt das Panel zu [Updates](updates.md) zurück. Das Alarmband meldet das Ergebnis, und die Karte von Schritt 2 behält die Zeile eines Fehlschlags bis zum nächsten Versuch oder zum nächsten Start:

| Alarm | Bedeutung |
|---|---|
| `IOmcu auf 0.22.0 aktualisiert.` | Das neue Image läuft und ist dauerhaft übernommen. |
| `IOmcu-Update abgebrochen: keine Antwort. Vorherige Firmware läuft.` | Seit 4s kam keine Antwort. |
| `IOmcu-Update abgebrochen: abgelehnt, Fehler 6. Vorherige Firmware läuft.` | Der IOmcu hat einen Schritt abgelehnt. |
| `IOmcu-Update abgebrochen: Firmware-Prüfung fehlgeschlagen. Vorherige Firmware läuft.` | Die SHA-256-Prüfsumme des Slots stimmt nicht mit der des Images überein. |
| `IOmcu-Update abgebrochen: Flash-Speicher blieb belegt. Vorherige Firmware läuft.` | Der Flash-Speicher blieb 10s belegt. |
| `IOmcu-Update abgebrochen: IOmcu 3-mal neu gestartet. Vorherige Firmware läuft.` | Die Übertragung begann dreimal bei Block 0. |
| `IOmcu-Update abgebrochen: neue Firmware ist nicht gestartet. Vorherige Firmware läuft.` | Das neue Image lieferte innerhalb von 12s keine Identität. |
| `IOmcu kann nicht über CAN aktualisiert werden. Installiere die Firmware einmal über USB.` | Dem IOmcu fehlen die Update-Pages oder die Partitionstabelle. Flashe ihn einmal über USB (Universal Serial Bus). |
| `Neue IOmcu-Firmware dauerhaft übernommen.` | Ein beim Verbindungsaufbau auf Probe laufendes Image wurde dauerhaft übernommen. |

Bei einem Stromausfall während des Updates bleibt das alte IOmcu-Image erhalten. Beim nächsten Verbindungsaufbau wird das Update erneut angeboten.

## Verwandte Seiten

- [Panel-Update](update.md): ein Panel-Image über WLAN installieren.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Übertragung, die Flash-Aufteilung und die Fehlernummern.
