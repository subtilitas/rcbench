# Panel UI texts

Every text of the panel UI, from `shared/ui/include/ui_text.def` (English) and `shared/ui/ui_text_de.c` (German). One table per screen, grouped by the source file that uses the text. A text used by more than one screen is under "Shared by several screens". "No direct reference found" lists texts reached through a table or macro that this scan does not follow.

1421 texts.

## Contents

- [ANALYSER](#analyser) (11)
- [Alert band messages](#alert-band-messages) (77)
- [BALANCE](#balance) (57)
- [BATTERY](#battery) (20)
- [BUS FAULT](#bus-fault) (71)
- [COPROCESSOR UPDATE](#coprocessor-update) (22)
- [ESC PROGRAMMER](#esc-programmer) (308)
- [FILES & UPDATE](#files--update) (56)
- [Keypads](#keypads) (8)
- [LOG VIEWER](#log-viewer) (69)
- [MAIN MENU](#main-menu) (10)
- [MOTOR & ESC](#motor--esc) (16)
- [OUTPUTS](#outputs) (21)
- [PICK A PIN](#pick-a-pin) (12)
- [Plots](#plots) (2)
- [SERVO](#servo) (148)
- [SERVO PROGRAMMER (KST, Chaservo)](#servo-programmer-kst-chaservo) (304)
- [SETUP](#setup) (18)
- [SPLASH](#splash) (17)
- [SUPPLY](#supply) (38)
- [Top band and help bubbles](#top-band-and-help-bubbles) (27)
- [WI-FI](#wi-fi) (57)
- [Shared by several screens](#shared-by-several-screens) (52)

## ANALYSER

| Text id | Where it is used | English | German |
|---|---|---|---|
| `AN_TAB_CHANNELS` | `shared/ui/analyser_screen.c:67` | CHANNELS | KANÄLE |
| `AN_TAB_RAW` | `shared/ui/analyser_screen.c:68` | RAW DATA | ROHDATEN |
| `AN_LAST_FRAME` | `shared/ui/analyser_screen.c:313` | LAST FRAME | LETZTER FRAME |
| `AN_NOTHING_YET` | `shared/ui/analyser_screen.c:317` | No data decoded yet. | Noch keine Daten dekodiert. |
| `AN_BYTES` | `shared/ui/analyser_screen.c:336` | %u bytes; a %u us gap separates frames | %u Bytes; %u us Pause trennen die Frames |
| `AN_SILENT` | `shared/ui/analyser_screen.c:355` | NO SIGNAL | KEIN SIGNAL |
| `AN_LIVE` | `shared/ui/analyser_screen.c:361` | LIVE | LIVE |
| `AN_WHY_SILENT` | `shared/ui/analyser_screen.c:375` | No signal on the wire. | Kein Signal auf der Leitung. |
| `AN_WHY_FAILSAFE` | `shared/ui/analyser_screen.c:376` | These are fallback values. | Dies sind Ersatzwerte. |
| `AN_WHY_LOST` | `shared/ui/analyser_screen.c:377` | A damaged frame was received. | Ein fehlerhafter Frame wurde empfangen. |
| `AN_WHY_LIVE` | `shared/ui/analyser_screen.c:378` | Signal received from the transmitter. | Signal vom Sender empfangen. |

## Alert band messages

| Text id | Where it is used | English | German |
|---|---|---|---|
| `ALERT_TOUCH_LOST_STOP` | `firmware/panel/main/main.c:1235` | Touch signal lost while holding STOP. Stopped. | Touch-Signal beim Halten von STOP verloren. Gestoppt. |
| `ALERT_TOUCH_LOST_ARMING` | `firmware/panel/main/main.c:1348` | Touch signal lost while switching to ARMED. Stopped. | Touch-Signal beim Wechsel zu ARMED verloren. Gestoppt. |
| `ALERT_NO_CARD_RUN` | `firmware/panel/main/main.c:2375` | No card. This run is not being recorded. | Keine Karte. Dieser Lauf wird nicht aufgezeichnet. |
| `ALERT_CARD_UNREADABLE` | `firmware/panel/main/main.c:2405` | Cannot read the card. Run not recorded. | Karte nicht lesbar. Lauf nicht aufgezeichnet. |
| `ALERT_CARD_FULL` | `firmware/panel/main/main.c:2459` | Card full or not writable. Run not recorded. | Karte voll oder nicht beschreibbar. Lauf nicht aufgezeichnet. |
| `ALERT_CARD_ROWS_SHORT` | `firmware/panel/main/main.c:2481` | Data could no longer be saved to the card. Recording incomplete. | Daten konnten nicht mehr auf die Karte geschrieben werden. Aufzeichnung unvollständig. |
| `ALERT_CARD_LAST_WRITE` | `firmware/panel/main/main.c:2496` | The last write to the card failed. Recording incomplete. | Letzter Schreibvorgang auf die Karte fehlgeschlagen. Aufzeichnung unvollständig. |
| `ALERT_CARD_SERVO` | `firmware/panel/main/main.c:2636` | Card missing or full. Servo test not recorded. | Karte fehlt oder ist voll. Servotest nicht aufgezeichnet. |
| `ALERT_CARD_SERVO_SHORT` | `firmware/panel/main/main.c:2656` | Card error. Servo test files are incomplete. | Kartenfehler. Dateien des Servotests sind unvollständig. |
| `ALERT_CARD_ROWS_PAST` | `firmware/panel/main/main.c:2920` | Data can no longer be saved to the card. Recording stopped here. | Daten können nicht mehr auf die Karte gespeichert werden. Aufzeichnung endet hier. |
| `ALERT_FW_UPDATED` | `firmware/panel/main/main.c:1943` | Panel firmware %s installed. | Panel-Firmware %s installiert. |
| `ALERT_FW_ROLLED_BACK` | `firmware/panel/main/main.c:1948` | Panel firmware %s failed to start. Version %s is running again. | Panel-Firmware %s ist nicht gestartet. Version %s läuft wieder. |
| `ALERT_FW_UNCONFIRMED` | `firmware/panel/main/main.c:1952` | Panel firmware %s was not kept. The next restart restores the previous version. | Panel-Firmware %s wurde nicht übernommen. Beim nächsten Start läuft wieder die vorherige Version. |
| `ALERT_POLES` | `firmware/panel/main/main.c:3588` | The coprocessor rejected the pole count. | Der Koprozessor hat die Polzahl abgelehnt. |
| `ALERT_PULSE_RANGE_UNKNOWN` | `firmware/panel/main/main.c:3647` | The coprocessor did not report its pulse range. | Der Koprozessor hat seinen Pulsbereich nicht gemeldet. |
| `ALERT_PULSE_ORDER` | `firmware/panel/main/main.c:3656` | Idle pulse must be shorter than full-throttle pulse. Not sent. | Leerlaufpuls muss kürzer als Vollgaspuls sein. Nicht gesendet. |
| `ALERT_PULSE_REFUSED` | `firmware/panel/main/main.c:3670` | The coprocessor rejected the pulse range. | Der Koprozessor hat den Pulsbereich abgelehnt. |
| `ALERT_PDMINI_OLD` | `firmware/panel/main/main.c:4298` | Coprocessor firmware too old to reset the PD mini. | Koprozessor-Firmware zu alt, um den PD mini zurückzusetzen. |
| `ALERT_PDMINI_WIRING` | `firmware/panel/main/main.c:4375` | PD mini wiring changed in SETUP. Output off. | PD-mini-Anschlüsse in SETUP geändert. Ausgang aus. |
| `ALERT_SUPPLY_CHANGED` | `firmware/panel/main/main.c:4381` | Supply changed in SETUP. Output off. | Netzteil in SETUP geändert. Ausgang aus. |
| `ALERT_SUPPLY_SILENT` | `firmware/panel/main/main.c:4445` | Supply not responding. Output off. | Netzteil antwortet nicht. Ausgang aus. |
| `ALERT_SUPPLY_NO_READING` | `firmware/panel/main/main.c:4452` | No readings from the supply. Output off. | Keine Messwerte vom Netzteil. Ausgang aus. |
| `ALERT_SUPPLY_TRIP` | `firmware/panel/main/main.c:4482` | Supply exceeded %u.%02u %s. Output off. | Netzteil hat %u.%02u %s überschritten. Ausgang aus. |
| `ALERT_TOUCH_ARM_AGAIN` | `firmware/panel/main/main.c:4588` | Touch signal lost while switching to ARMED. Hold ARM again. | Touch-Signal beim Wechsel zu ARMED verloren. ARM erneut halten. |
| `ALERT_TOUCH_STOPPED` | `firmware/panel/main/main.c:4720` | Touchscreen stopped responding. Switched to SAFE. | Touchscreen antwortet nicht mehr. Auf SAFE geschaltet. |
| `ALERT_SERVO_NOT_RELEASED` | `firmware/panel/main/main.c:4774` | Servo output not released. Hold ARM again. | Servoausgang nicht freigegeben. ARM erneut halten. |
| `ALERT_SERVO_RATE` | `firmware/panel/main/main.c:4779` | Servo frame rate unknown. Hold ARM again. | Pulsrate des Servos unbekannt. ARM erneut halten. |
| `ALERT_ARM_REFUSED` | `firmware/panel/main/main.c:4809,4813,4823,4827,4846` | The coprocessor refused to switch to ARMED. | Der Koprozessor hat den Wechsel zu ARMED abgelehnt. |
| `ALERT_ARM_PROG` | `firmware/panel/main/main.c:4734,6077` | Cannot switch to ARMED: a port is in use by the servo programmer. | ARMED gesperrt: Die Servoprogrammierung belegt einen Port. |
| `ALERT_SWEEP_RESUME_OLD` | `firmware/panel/main/main.c:5495` | Coprocessor firmware older than 4.6. Sweep starts again. | Koprozessor-Firmware älter als 4.6. Sweep beginnt von vorn. |
| `ALERT_SWEEP_RESUME_REFUSED` | `firmware/panel/main/main.c:5497` | Coprocessor could not resume. Sweep starts again. | Koprozessor hat das Fortsetzen abgelehnt. Sweep beginnt von vorn. |
| `ALERT_SWEEP_RESUME_UNTIMED` | `firmware/panel/main/main.c:5499` | Pause was not confirmed. Sweep starts again. | Pause wurde nicht bestätigt. Sweep beginnt von vorn. |
| `ALERT_CARD_SLOW` | `firmware/panel/main/main.c:6209` | Card too slow. Run not recorded. | Karte zu langsam. Lauf nicht aufgezeichnet. |
| `ALERT_CARD_GAPS` | `firmware/panel/main/main.c:6211` | The card was too slow. Some data is missing from the recording. | Die Karte war zu langsam. In der Aufzeichnung fehlen Daten. |
| `ALERT_TOUCH_SWITCH_ON` | `firmware/panel/main/main.c:6249` | Touch signal lost while switching on. Output off. | Touch-Signal beim Einschalten verloren. Ausgang aus. |
| `ALERT_PDMINI_OFF` | `firmware/panel/main/main.c:6320` | PD mini switched its output off. Output off. | PD mini hat seinen Ausgang abgeschaltet. Ausgang aus. |
| `ALERT_SUPPLY_OFF_REMOTE` | `firmware/panel/main/main.c:6322` | Supply switched off by the coprocessor. | Netzteil vom Koprozessor ausgeschaltet. |
| `ALERT_SUPPLY_REFUSED_ON` | `firmware/panel/main/main.c:6323` | Supply refused to switch on. Output off. | Netzteil ließ sich nicht einschalten. Ausgang aus. |
| `ALERT_PDMINI_PINS` | `firmware/panel/main/main.c:6339` | PD mini pins rejected. Check SETUP > INTERFACES. | PD-mini-Pins abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_PDMINI_SWITCH` | `firmware/panel/main/main.c:6345` | PD mini output could not be switched. | PD-mini-Ausgang ließ sich nicht schalten. |
| `ALERT_PDMINI_SET` | `firmware/panel/main/main.c:6348` | PD mini did not accept the set values. | PD mini hat die Sollwerte nicht übernommen. |
| `ALERT_PDMINI_SAG` | `firmware/panel/main/main.c:6333` | PD mini input %u.%02u V is below the required %u.%02u V + 0.5 V. Output off. | PD-mini-Eingang %u.%02u V liegt unter den nötigen %u.%02u V + 0.5 V. Ausgang aus. |
| `ALERT_PDMINI_WIRING_LIVE` | `firmware/panel/main/main.c:6342` | PD mini wiring rejected. Its output may still be on. | PD-mini-Anschlüsse abgelehnt. Sein Ausgang kann noch eingeschaltet sein. |
| `ALERT_COPRO_DISARMED` | `firmware/panel/main/main.c:3711` | Coprocessor switched to SAFE. Hold ARM again. | Koprozessor hat auf SAFE geschaltet. ARM erneut halten. |
| `ALERT_MISMATCH` | `firmware/panel/main/main.c:7191` | Protocol versions do not match. ARMED is blocked. | Protokollversionen passen nicht zusammen. ARMED ist gesperrt. |
| `ALERT_NO_SUPPLY_PAGE` | `firmware/panel/main/main.c:7428` | Coprocessor has no SUPPLY page. PD mini cannot be controlled. | Koprozessor hat keine SUPPLY-Page. PD mini wird nicht angesteuert. |
| `ALERT_TOUCH_NO_ANSWER` | `firmware/panel/main/main.c:9414` | Touchscreen not responding. ARMED is blocked. | Touchscreen antwortet nicht. ARMED ist gesperrt. |
| `ALERT_NO_SENSE_PAGE` | `firmware/panel/main/main.c:6567` | Coprocessor has no SENSE page. Current monitors cannot be read. | Koprozessor hat keine SENSE-Page. Stromsensoren werden nicht ausgelesen. |
| `ALERT_SENSE_PINS_UNSET` | `firmware/panel/main/main.c:6564` | Sensor SDA or SCL not set. Check SETUP > INTERFACES. | SDA oder SCL der Sensoren nicht festgelegt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_SENSE_SAME_ADDR` | `firmware/panel/main/main.c:6559` | INA228 and INA3221 both use 0x%02X. Check SETUP > INTERFACES. | INA228 und INA3221 nutzen beide 0x%02X. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_SENSE_PINS` | `firmware/panel/main/main.c:6554` | Sensor pins GP%d/GP%d rejected. Check SETUP > INTERFACES. | Sensor-Pins GP%d/GP%d abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_SENSE_I228_SETUP` | `firmware/panel/main/main.c:6551` | INA228 shunt or maximum current rejected. Check SETUP > INTERFACES. | INA228-Shunt oder Höchststrom abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_SENSE_I3221_SETUP` | `firmware/panel/main/main.c:6548` | INA3221 settings rejected. Check SETUP > INTERFACES. | INA3221-Einstellungen abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_SENSE_SILENT` | `firmware/panel/main/main.c:6532` | %s is not responding at 0x%02X. | %s antwortet nicht unter 0x%02X. |
| `ALERT_SENSE_SILENT_FOUND` | `firmware/panel/main/main.c:6529` | %s is not responding at 0x%02X. A device responds at 0x%02X. | %s antwortet nicht unter 0x%02X. Ein Gerät antwortet unter 0x%02X. |
| `ALERT_SENSE_WRONG` | `firmware/panel/main/main.c:6538` | 0x%02X reports %04Xh, not an %s. Device not used. | 0x%02X meldet %04Xh, kein %s. Gerät wird nicht verwendet. |
| `ALERT_SENSE_STUCK` | `firmware/panel/main/main.c:6545` | Sensor bus blocked: SDA low. Trying clock pulses to release it. | Sensorbus blockiert: SDA auf Low. Freitakten wird versucht. |
| `ALERT_SENSE_I228_CLIPPED` | `firmware/panel/main/main.c:6515` | INA228 current at its range limit. Reading shows only the limit. | INA228-Strom am Messbereichsende. Anzeige zeigt nur den Grenzwert. |
| `ALERT_SENSE_I3221_CLIPPED` | `firmware/panel/main/main.c:6509` | INA3221 CH%u clipped at %u.%02u A. Reading shows only the limit. | INA3221 CH%u bei %u.%02u A übersteuert. Anzeige zeigt nur den Grenzwert. |
| `ALERT_ENC_OLD` | `firmware/panel/main/main.c:6570` | Coprocessor firmware older than 4.9. AS5600 not read. | Koprozessor-Firmware älter als 4.9. AS5600 nicht ausgelesen. |
| `ALERT_SERVO_METER_OLD` | `firmware/panel/main/main.c:6677` | Coprocessor firmware older than 4.11. Servo current is read from the PD mini. | Koprozessor-Firmware älter als 4.11. Servostrom wird vom PD mini ausgelesen. |
| `ALERT_I3221_RESET` | `firmware/panel/main/main.c:6679` | INA3221 has reset. Check its power supply. | INA3221 hat sich zurückgesetzt. Versorgung prüfen. |
| `ALERT_ENC_SILENT` | `firmware/panel/main/main.c:6573` | AS5600 is not responding at 0x36. | AS5600 antwortet nicht unter 0x36. |
| `ALERT_ENC_NO_MAGNET` | `firmware/panel/main/main.c:6580` | AS5600 detects no magnet. Check the magnet on the servo horn shaft. | AS5600 erkennt keinen Magneten. Magnet an der Servoabtriebswelle prüfen. |
| `ALERT_ENC_WEAK` | `firmware/panel/main/main.c:6581` | AS5600: magnetic field too weak. Move the magnet closer. | AS5600: Magnetfeld zu schwach. Magnet näher heranbringen. |
| `ALERT_ENC_STRONG` | `firmware/panel/main/main.c:6582` | AS5600: magnetic field too strong. Move the magnet farther away. | AS5600: Magnetfeld zu stark. Abstand zum Magneten vergrößern. |
| `ALERT_STORE_OFF` | `firmware/panel/main/main.c:6501` | Coprocessor storage unavailable. Settings will be lost at restart. | Koprozessor-Speicher nicht verfügbar. Einstellungen gehen beim Neustart verloren. |
| `ALERT_NO_TONE_PAGE` | `firmware/panel/main/main.c:6805` | Coprocessor has no TONE page. Phase tap not read. | Koprozessor hat keine TONE-Page. Phasenabgriff nicht ausgelesen. |
| `ALERT_TONE_PIN` | `firmware/panel/main/main.c:6807` | Phase tap on GP%u, %u to %u Hz rejected. Check SETUP > INTERFACES. | Phasenabgriff an GP%u, %u bis %u Hz abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_TONE_SETUP` | `firmware/panel/main/main.c:6812` | Phase tap split, gap or periods rejected. Check SETUP > INTERFACES. | Phasenabgriff: Sprung, Pause oder Perioden abgelehnt. SETUP > ANSCHLÜSSE prüfen. |
| `ALERT_TONE_BUSY` | `firmware/panel/main/main.c:6814` | Phase tap pin GP%u is in use. Capture not running. | Pin GP%u für den Phasenabgriff ist belegt. Erfassung läuft nicht. |
| `ALERT_TONE_OVERRUN` | `firmware/panel/main/main.c:6818` | Phase tap capture overflow. Beeps are incomplete. | Phasenabgriff: Erfassung übergelaufen. Pieptöne unvollständig. |
| `ALERT_CU_OFFER` | `firmware/panel/main/main.c:8069` | Coprocessor runs a different build. An update is available in the menu. | Koprozessor nutzt einen anderen Build. Update im Menü verfügbar. |
| `ALERT_CU_USB` | `firmware/panel/main/main.c:8081,8108` | Coprocessor cannot update over CAN. Install firmware once via USB. | Koprozessor kann nicht über CAN aktualisiert werden. Firmware einmal über USB installieren. |
| `ALERT_CU_DONE` | `firmware/panel/main/main.c:8102` | Coprocessor updated to %u.%u.%u. | Koprozessor auf %u.%u.%u aktualisiert. |
| `ALERT_CU_KEPT` | `firmware/panel/main/main.c:8148` | New coprocessor firmware kept. | Neue Koprozessor-Firmware dauerhaft übernommen. |
| `ALERT_CU_FAILED` | `firmware/panel/main/main.c:8041` | Coprocessor update stopped: %s. Previous firmware is running. | Koprozessor-Update abgebrochen: %s. Vorherige Firmware läuft. |

## BALANCE

| Text id | Where it is used | English | German |
|---|---|---|---|
| `BAL_TAB_BALANCE` | `shared/ui/balance_screen.c:52` | BALANCE | WUCHTEN |
| `BAL_TAB_RIG` | `shared/ui/balance_screen.c:52` | TEST RIG | PRÜFGESTELL |
| `BAL_TAB_AIRCRAFT` | `shared/ui/balance_screen.c:53` | AIRCRAFT | FLUGMODELL |
| `BAL_ACCELEROMETER` | `shared/ui/balance_screen.c:266,382` | ACCELEROMETER | BESCHLEUNIGUNGSSENSOR |
| `BAL_ONE_MARK` | `shared/ui/balance_screen.c:269,386` | ONE MARK | EINE MARKIERUNG |
| `BAL_OPTICAL` | `shared/ui/balance_screen.c:270` | OPTICAL | OPTISCH |
| `BAL_FIREWALL` | `shared/ui/balance_screen.c:383` | FIREWALL | MOTORSPANT |
| `BAL_TIED_DOWN` | `shared/ui/balance_screen.c:387` | SECURED | GESICHERT |
| `BAL_RIG_1` | `shared/ui/balance_screen.c:476` | ACROSS THE SHAFT | QUER ZUR WELLE |
| `BAL_RIG_1A` | `shared/ui/balance_screen.c:476` | An unbalanced disc vibrates | Eine unwuchtige Scheibe schwingt |
| `BAL_RIG_1B` | `shared/ui/balance_screen.c:476` | sideways, not along the shaft. | quer zur Welle, nicht längs. |
| `BAL_RIG_2` | `shared/ui/balance_screen.c:477` | CLOSE TO THE BEARING | NAH AM LAGER |
| `BAL_RIG_2A` | `shared/ui/balance_screen.c:477` | Joints between bearing and sensor | Verbindungen zwischen Lager und |
| `BAL_RIG_2B` | `shared/ui/balance_screen.c:477` | can flex and affect the reading. | Sensor können federn und stören. |
| `BAL_RIG_3` | `shared/ui/balance_screen.c:478` | BOLT IT ON, DO NOT TAPE IT | VERSCHRAUBEN, NICHT KLEBEN |
| `BAL_RIG_3A` | `shared/ui/balance_screen.c:478` | Foam absorbs the vibrations | Schaum dämpft die Schwingungen, |
| `BAL_RIG_3B` | `shared/ui/balance_screen.c:478` | you need to measure. | die du messen möchtest. |
| `BAL_RIG_4` | `shared/ui/balance_screen.c:479` | ONE MARK PER TURN | EINE MARKE PRO UMDREHUNG |
| `BAL_RIG_4A` | `shared/ui/balance_screen.c:479` | Mark the motor bell. Using blades | Markiere die Motorglocke. Blätter |
| `BAL_RIG_4B` | `shared/ui/balance_screen.c:479` | gives one pulse per blade. | erzeugen einen Impuls je Blatt. |
| `BAL_RIG_5` | `shared/ui/balance_screen.c:480` | USE A PEN, NOT TAPE | STIFT STATT KLEBEBAND |
| `BAL_RIG_5A` | `shared/ui/balance_screen.c:480` | Tape adds weight and changes | Klebeband bringt Gewicht mit und |
| `BAL_RIG_5B` | `shared/ui/balance_screen.c:480` | the balance you are measuring. | verändert die gemessene Unwucht. |
| `BAL_RIG_6` | `shared/ui/balance_screen.c:481` | KEEP THE CABLE OUT OF AIRFLOW | KABEL AUS DEM LUFTSTROM |
| `BAL_RIG_6A` | `shared/ui/balance_screen.c:481` | A flapping cable can affect | Ein flatterndes Kabel kann |
| `BAL_RIG_6B` | `shared/ui/balance_screen.c:481` | the vibration measurement. | die Messung verfälschen. |
| `BAL_AIR_1` | `shared/ui/balance_screen.c:487` | THE FIREWALL, NOT THE COWL | MOTORSPANT, NICHT HAUBE |
| `BAL_AIR_1A` | `shared/ui/balance_screen.c:487` | The cowl can move separately | Die Haube kann sich unabhängig |
| `BAL_AIR_1B` | `shared/ui/balance_screen.c:487` | and affect the measurement. | bewegen und die Messung stören. |
| `BAL_AIR_2` | `shared/ui/balance_screen.c:488` | FLAT MOUNTING IS FINE | FLACHE MONTAGE IST MÖGLICH |
| `BAL_AIR_2A` | `shared/ui/balance_screen.c:488` | Mount flat. Use a sensor axis | Flach montieren. Eine Sensorachse |
| `BAL_AIR_2B` | `shared/ui/balance_screen.c:488` | parallel to the firewall. | parallel zum Motorspant nutzen. |
| `BAL_AIR_3` | `shared/ui/balance_screen.c:489` | NEAR THE BOLTS | NAHE DEN SCHRAUBEN |
| `BAL_AIR_3A` | `shared/ui/balance_screen.c:489` | As on the rig, avoid joints | Wie am Gestell: Verbindungen |
| `BAL_AIR_3B` | `shared/ui/balance_screen.c:489` | between the sensor and bearing. | zwischen Sensor und Lager meiden. |
| `BAL_AIR_4` | `shared/ui/balance_screen.c:490` | MARK THE MOTOR BELL | MOTORGLOCKE MARKIEREN |
| `BAL_AIR_4A` | `shared/ui/balance_screen.c:490` | A spinner may sit at a different | Ein Spinner kann nach der Montage |
| `BAL_AIR_4B` | `shared/ui/balance_screen.c:490` | angle after it is refitted. | in einem anderen Winkel sitzen. |
| `BAL_AIR_5` | `shared/ui/balance_screen.c:491` | TIE THE TAIL DOWN | HECK FESTBINDEN |
| `BAL_AIR_5A` | `shared/ui/balance_screen.c:491` | A model that can rock adds | Ein schaukelndes Modell erzeugt |
| `BAL_AIR_5B` | `shared/ui/balance_screen.c:491` | unwanted movement to the reading. | Störbewegungen bei der Messung. |
| `BAL_AIR_6` | `shared/ui/balance_screen.c:492` | KEEP THE CABLE OUT OF AIRFLOW | KABEL AUS DEM LUFTSTROM |
| `BAL_AIR_6A` | `shared/ui/balance_screen.c:492` | Secure it. A flapping cable can | Kabel befestigen. Ein flatterndes |
| `BAL_AIR_6B` | `shared/ui/balance_screen.c:492` | affect the measurement. | Kabel kann die Messung verfälschen. |
| `BAL_WRONG` | `shared/ui/balance_screen.c:599` | COMMON MISTAKES | TYPISCHE FEHLER |
| `BAL_BLADES` | `shared/ui/balance_screen.c:523` | BLADES | BLÄTTER |
| `BAL_VIBRATION` | `shared/ui/balance_screen.c:557` | VIBRATION | VIBRATION |
| `BAL_ANGLE` | `shared/ui/balance_screen.c:558` | ANGLE | WINKEL |
| `BAL_ADD` | `shared/ui/balance_screen.c:559` | ADD | HINZUFÜGEN |
| `BAL_WHERE` | `shared/ui/balance_screen.c:560` | WHERE | WO |
| `BAL_HUB` | `shared/ui/balance_screen.c:547` | hub, %d deg | Nabe, %d Grad |
| `BAL_BETWEEN` | `shared/ui/balance_screen.c:553` | between %d and %d | zwischen %d und %d |
| `BAL_MODELLED` | `shared/ui/balance_screen.c:121,579,582` | SIMULATED | SIMULIERT |
| `BAL_MODEL_NOTE` | `shared/ui/balance_screen.c:126,584` | Not measured. | Nicht gemessen. |
| `BAL_NOTE_1` | `shared/ui/balance_screen.c:588` | Four runs: one baseline, then a | Vier Läufe: Referenz, dann eine |
| `BAL_NOTE_2` | `shared/ui/balance_screen.c:590` | trial mass at 0, 120 and 240 deg. | Testmasse bei 0, 120 und 240 Grad. |
| `BAL_NOTE_3` | `shared/ui/balance_screen.c:592` | An index pulse halves the runs. | Ein Indexpuls halbiert die Laufzahl. |

## BATTERY

| Text id | Where it is used | English | German |
|---|---|---|---|
| `BAT_NO_MONITOR` | `shared/ui/battery_screen.c:159` | No monitor on the balance lead. | Kein Messgerät am Balancer-Anschluss. |
| `BAT_NO_PACK` | `shared/ui/battery_screen.c:234` | NO PACK | KEIN AKKU |
| `BAT_NO_PACK_WHY` | `shared/ui/battery_screen.c:234` | No battery connected to the balance lead. | Kein Akku am Balancer-Anschluss. |
| `BAT_REPLACE` | `shared/ui/battery_screen.c:237` | REPLACE | ERSETZEN |
| `BAT_REPLACE_WHY` | `shared/ui/battery_screen.c:237` | One cell differs from the others. | Eine Zelle weicht von den anderen ab. |
| `BAT_WATCH` | `shared/ui/battery_screen.c:240` | WATCH | BEOBACHTEN |
| `BAT_WATCH_WHY` | `shared/ui/battery_screen.c:240` | The difference between cells is growing. | Der Unterschied zwischen den Zellen wächst. |
| `BAT_HEALTHY` | `shared/ui/battery_screen.c:243` | HEALTHY | GUT |
| `BAT_HEALTHY_WHY` | `shared/ui/battery_screen.c:243` | Cell readings stay even under load. | Zellwerte sind unter Last gleichmäßig. |
| `BAT_CELLS` | `shared/ui/battery_screen.c:270,288` | CELLS | ZELLEN |
| `BAT_PACK` | `shared/ui/battery_screen.c:270,290` | PACK | AKKU |
| `BAT_MEAN` | `shared/ui/battery_screen.c:270,292` | AVERAGE | MITTELWERT |
| `BAT_SPREAD` | `shared/ui/battery_screen.c:270,294` | SPREAD | UNTERSCHIED |
| `BAT_PACK_IR` | `shared/ui/battery_screen.c:271,299` | PACK iR | AKKU iR |
| `BAT_USED` | `shared/ui/battery_screen.c:271,302` | USED | ENTNOMMEN |
| `BAT_USED_OF` | `shared/ui/battery_screen.c:300` | %d of %d mAh | %d von %d mAh |
| `BAT_NOTE_1` | `shared/ui/battery_screen.c:305` | Cell readings are compared under load. | Zellwerte werden unter Last verglichen. |
| `BAT_NOTE_2` | `shared/ui/battery_screen.c:307` | At rest, a weak cell may look | Ohne Last kann eine schwache Zelle |
| `BAT_NOTE_3` | `shared/ui/battery_screen.c:309` | just like a healthy one. | unauffällig sein. |
| `BAT_DIVERGENCE` | `shared/ui/battery_screen.c:333` | CELL DIFFERENCE | ZELLENABWEICHUNG |

## BUS FAULT

| Text id | Where it is used | English | German |
|---|---|---|---|
| `BF_BUS_OFF_TEXT` | `shared/ui/busfault_screen.c:109` | Panel disconnected from the bus. | Panel vom Bus getrennt. |
| `BF_BUS_REJOIN_TEXT` | `shared/ui/busfault_screen.c:110` | Panel is reconnecting. | Panel verbindet sich neu. |
| `BF_BUS_STOPPED_TEXT` | `shared/ui/busfault_screen.c:111` | The panel's CAN controller has stopped. | CAN-Controller des Panels ist gestoppt. |
| `BF_BUS_RUNNING_TEXT` | `shared/ui/busfault_screen.c:112` | The link stopped responding. | Verbindung antwortet nicht mehr. |
| `BF_BUS_UNKNOWN_TEXT` | `shared/ui/busfault_screen.c:114` | Cannot read the controller's state. | Controller-Zustand nicht auslesbar. |
| `BF_BUS_REJOIN_WORD` | `shared/ui/busfault_screen.c:124` | reconnecting | verbindet sich neu |
| `BF_BUS_STOPPED_WORD` | `shared/ui/busfault_screen.c:125` | stopped | gestoppt |
| `BF_BUS_RUNNING_WORD` | `shared/ui/busfault_screen.c:126` | running | läuft |
| `BF_BUS_UNKNOWN_WORD` | `shared/ui/busfault_screen.c:128` | not running | läuft nicht |
| `BF_BUS_OFF_MEANS` | `shared/ui/busfault_screen.c:135` | Too many frames were not confirmed. The controller stopped sending. | Zu viele Frames blieben unbestätigt. Der Controller sendet nicht mehr. |
| `BF_BUS_REJOIN_MEANS` | `shared/ui/busfault_screen.c:136` | The controller is waiting for the bus to be quiet before reconnecting. Wait 3 s. | Der Controller wartet vor dem Neuverbinden auf Busruhe. Warte 3 s. |
| `BF_BUS_STOPPED_MEANS` | `shared/ui/busfault_screen.c:137` | The controller stopped and was not restarted. This is a fault in the panel. | Der Controller wurde nach dem Stopp nicht neu gestartet. Das ist ein Fehler im Panel. |
| `BF_BUS_RUNNING_MEANS` | `shared/ui/busfault_screen.c:138` | The controller is connected to the bus, but no device confirms its frames. | Der Controller ist am Bus, aber kein Gerät bestätigt seine Frames. |
| `BF_BUS_UNKNOWN_MEANS` | `shared/ui/busfault_screen.c:140` | The driver is not running. No data can be sent. | Der Treiber läuft nicht. Es können keine Daten gesendet werden. |
| `BF_REJOIN_1` | `shared/ui/busfault_screen.c:149` | The panel tries to reconnect once a second. | Das Panel versucht jede Sekunde, sich neu zu verbinden. |
| `BF_REJOIN_2` | `shared/ui/busfault_screen.c:150` | If the connection returns on its own, this is not a defect. | Kommt die Verbindung von selbst zurück, liegt kein Defekt vor. |
| `BF_REJOIN_3` | `shared/ui/busfault_screen.c:151` | If the problem remains, take a photo of this screen and report it. | Bleibt das Problem, fotografiere diese Seite und melde den Fehler. |
| `BF_RUNNING_1` | `shared/ui/busfault_screen.c:154` | Check the coprocessor's power LED. Is it still powered? | Prüfe die LED am Koprozessor. Hat er noch Strom? |
| `BF_RUNNING_2` | `shared/ui/busfault_screen.c:155` | Check that every connector is fully seated. | Prüfe, ob alle Stecker richtig sitzen. |
| `BF_RUNNING_3` | `shared/ui/busfault_screen.c:156` | If both are OK, take a photo of this screen. | Ist beides in Ordnung, fotografiere diese Seite. |
| `BF_STOPPED_1` | `shared/ui/busfault_screen.c:161` | This is a fault in the panel's firmware, | Der Fehler liegt in der Firmware des Panels, |
| `BF_STOPPED_2` | `shared/ui/busfault_screen.c:162` | `  not in the connected bench hardware.` | `  nicht an der angeschlossenen Prüfstand-Hardware.` |
| `BF_STOPPED_3` | `shared/ui/busfault_screen.c:163` | Take a photo of this screen and report the fault. | Fotografiere diese Seite und melde den Fehler. |
| `BF_SILENT_MEANS` | `shared/ui/busfault_screen.c:171` | No response on the bus. The two boards cannot communicate. | Keine Antwort am Bus. Die beiden Platinen können keine Daten austauschen. |
| `BF_CORRUPT_MEANS` | `shared/ui/busfault_screen.c:172` | Frames arrive with changed data. Check the wiring and bit timing. | Frames kommen mit veränderten Daten an. Verkabelung und Bit-Timing prüfen. |
| `BF_LOSSY_MEANS` | `shared/ui/busfault_screen.c:173` | Some frames are lost, and the bus reports errors. | Einige Frames gehen verloren. Der Bus meldet Fehler. |
| `BF_DROPPED_MEANS` | `shared/ui/busfault_screen.c:174` | Frames arrived intact, then were discarded unread. Not a wiring fault. | Frames kamen korrekt an, wurden aber ungelesen verworfen. Kein Verkabelungsfehler. |
| `BF_SILENT_1` | `shared/ui/busfault_screen.c:192` | Check the coprocessor's power LED, not the panel's. | Prüfe die Versorgungs-LED am Koprozessor, nicht die am Panel. |
| `BF_SILENT_2` | `shared/ui/busfault_screen.c:193` | Connect CANH to CANH and CANL to CANL. Do not cross them. | CANH mit CANH und CANL mit CANL verbinden. Nicht vertauschen. |
| `BF_SILENT_3` | `shared/ui/busfault_screen.c:194` | Both boards must use firmware from the same release. | Beide Platinen müssen Firmware aus demselben Release nutzen. |
| `BF_SILENT_4` | `shared/ui/busfault_screen.c:195` | With the bench off, measure about 60 ohms | Bei ausgeschaltetem Prüfstand etwa 60 Ohm |
| `BF_SILENT_5` | `shared/ui/busfault_screen.c:196` | `  between CANH and CANL for correct termination.` | `  zwischen CANH und CANL messen: Busabschluss prüfen.` |
| `BF_CORRUPT_1` | `shared/ui/busfault_screen.c:199` | With the bench off, expect about 60 ohms between CANH and CANL. | Prüfstand ausschalten. Zwischen CANH und CANL sollten etwa 60 Ohm liegen. |
| `BF_CORRUPT_2` | `shared/ui/busfault_screen.c:200` | `  A reading of 120 ohms means one terminator is missing.` | `  Bei 120 Ohm fehlt einer der beiden Abschlusswiderstände.` |
| `BF_CORRUPT_3` | `shared/ui/busfault_screen.c:201` | Keep the branch to each board under 30 cm. | Stichleitung zu jeder Platine unter 30 cm halten. |
| `BF_TWISTED` | `shared/ui/busfault_screen.c:202,207` | Twist CANH and CANL together. Keep them away from motor wires. | CANH und CANL verdrillen. Abstand zu Motorleitungen halten. |
| `BF_LOSSY_1` | `shared/ui/busfault_screen.c:205` | With the bench off, check for about 60 ohms between CANH and CANL. | Prüfstand ausschalten. Zwischen CANH und CANL sollten etwa 60 Ohm liegen. |
| `BF_LOSSY_2` | `shared/ui/busfault_screen.c:206` | Keep the total bus below 5 m and each branch below 30 cm. | Gesamten Bus unter 5 m und jede Stichleitung unter 30 cm halten. |
| `BF_LOSSY_4` | `shared/ui/busfault_screen.c:208` | Check that every connector is fully seated. | Prüfe, ob alle Stecker richtig sitzen. |
| `BF_DROPPED_1` | `shared/ui/busfault_screen.c:211` | The wiring is not the cause. The frames arrived intact. | Die Verkabelung ist nicht die Ursache. Die Frames kamen korrekt an. |
| `BF_DROPPED_2` | `shared/ui/busfault_screen.c:212` | Frames were not read in time. Please report this fault | Frames wurden nicht rechtzeitig gelesen. Melde diesen Fehler |
| `BF_DROPPED_3` | `shared/ui/busfault_screen.c:213` | `  and include the numbers shown on the right.` | `  mit den Zahlen auf der rechten Seite.` |
| `BF_HEAD_MEANS` | `shared/ui/busfault_screen.c:469` | WHAT THIS MEANS | WAS DAS BEDEUTET |
| `BF_HEAD_CHECK` | `shared/ui/busfault_screen.c:469` | CHECK IN THIS ORDER | IN DIESER REIHENFOLGE PRÜFEN |
| `BF_NOTE_1` | `shared/ui/busfault_screen.c:494` | After acknowledging, the bench stays in simulation. | Nach dem Bestätigen bleibt der Prüfstand in der Simulation. |
| `BF_NOTE_2` | `shared/ui/busfault_screen.c:497` | It shows simulated values. No outputs are driven. | Die Werte sind simuliert. Kein Ausgang wird angesteuert. |
| `BF_NOTE_LOST` | `shared/ui/busfault_screen.c:500` | These numbers are also saved in RCBENCH.LOG on the card. | Diese Zahlen stehen auch in RCBENCH.LOG auf der Karte. |
| `BF_NOTE_TEST` | `shared/ui/busfault_screen.c:500` | The test runs again at every start-up. | Der Test läuft bei jedem Start erneut. |
| `BF_ACKNOWLEDGED` | `shared/ui/busfault_screen.c:518` | ACKNOWLEDGED | BESTÄTIGT |
| `BF_HOLD` | `shared/ui/busfault_screen.c:518` | HOLD 2 s TO ACKNOWLEDGE | 2 s HALTEN ZUM BESTÄTIGEN |
| `BF_THIS_PANEL` | `shared/ui/busfault_screen.c:328,396` | THIS PANEL | DIESES PANEL |
| `BF_DOWN` | `shared/ui/busfault_screen.c:332` | down      %lu s | getrennt      %lu s |
| `BF_REQUESTS` | `shared/ui/busfault_screen.c:335` | requests  %lu | Anfragen      %lu |
| `BF_NO_ANSWER` | `shared/ui/busfault_screen.c:338` | no answer %lu | unbeantwortet %lu |
| `BF_REJOINS` | `shared/ui/busfault_screen.c:362` | rejoins %lu | Neuverbindungen %lu |
| `BF_MEASURED` | `shared/ui/busfault_screen.c:372` | MEASURED | GEMESSEN |
| `BF_SENT` | `shared/ui/busfault_screen.c:377` | sent      %lu | gesendet   %lu |
| `BF_RETURNED` | `shared/ui/busfault_screen.c:380` | returned  %lu | zurück     %lu |
| `BF_ALTERED` | `shared/ui/busfault_screen.c:385` | altered   %lu | verändert  %lu |
| `BF_LOST` | `shared/ui/busfault_screen.c:390` | lost      %lu | verloren   %lu |
| `BF_COPROCESSOR` | `shared/ui/busfault_screen.c:412` | COPROCESSOR | KOPROZESSOR |
| `BF_NO_REPLY` | `shared/ui/busfault_screen.c:421` | No response. | Keine Antwort. |
| `BF_CAN_UP` | `shared/ui/busfault_screen.c:425` | CAN up | CAN aktiv |
| `BF_CAN_DOWN` | `shared/ui/busfault_screen.c:425` | CAN DOWN | CAN INAKTIV |
| `BF_DROPPED` | `shared/ui/busfault_screen.c:438` | dropped %u | verworfen %u |
| `CAN_OK` | `shared/ui/busfault_screen.c:223` | All test frames returned intact. | Alle Testframes kamen korrekt zurück. |
| `CAN_SILENT` | `shared/ui/busfault_screen.c:224` | No test frames returned. | Kein Testframe kam zurück. |
| `CAN_CORRUPT` | `shared/ui/busfault_screen.c:225` | Test frames returned with changed data. | Testframes kamen mit veränderten Daten zurück. |
| `CAN_LOSSY` | `shared/ui/busfault_screen.c:226` | Some test frames were lost. | Einige Testframes gingen verloren. |
| `CAN_DROPPED` | `shared/ui/busfault_screen.c:227` | Test frames lost without a bus error. | Testframes gingen ohne Busfehler verloren. |
| `CAN_RUNNING` | `shared/ui/busfault_screen.c:229` | running | läuft |

## COPROCESSOR UPDATE

| Text id | Where it is used | English | German |
|---|---|---|---|
| `CU_TITLE` | `shared/ui/coproc_update_screen.c:234,282` | COPROCESSOR UPDATE | KOPROZESSOR-UPDATE |
| `CU_Q_VERSIONS` | `shared/ui/coproc_update_screen.c:239` | Coprocessor version: %s. Version on this panel: %s. | Koprozessor-Version: %s. Version auf diesem Panel: %s. |
| `CU_Q_OLDER` | `shared/ui/coproc_update_screen.c:245` | The firmware on this panel is older. | Die Firmware auf diesem Panel ist älter. |
| `CU_Q_SAME` | `shared/ui/coproc_update_screen.c:248` | Same version number, but a different build. | Gleiche Versionsnummer, aber ein anderer Build. |
| `CU_Q_WHAT` | `shared/ui/coproc_update_screen.c:251` | Outputs stay off. ARMED is blocked until the update ends. | Ausgänge bleiben aus. ARMED ist bis zum Ende des Updates gesperrt. |
| `CU_START` | `shared/ui/coproc_update_screen.c:262` | HOLD 2 s TO START | 2 s HALTEN ZUM STARTEN |
| `CU_P_PREPARE` | `shared/ui/coproc_update_screen.c:273` | Preparing firmware. | Firmware wird vorbereitet. |
| `CU_P_SEND` | `shared/ui/coproc_update_screen.c:268` | Sending firmware. | Firmware wird gesendet. |
| `CU_P_VERIFY` | `shared/ui/coproc_update_screen.c:269` | Coprocessor is checking the firmware. | Koprozessor prüft die Firmware. |
| `CU_P_RESTART` | `shared/ui/coproc_update_screen.c:270` | Coprocessor is starting the new firmware. | Koprozessor startet die neue Firmware. |
| `CU_P_BUY` | `shared/ui/coproc_update_screen.c:271` | Keeping the new firmware. | Neue Firmware wird dauerhaft übernommen. |
| `CU_P_FROM_TO` | `shared/ui/coproc_update_screen.c:290` | %s to %s | %s auf %s |
| `CU_P_BLOCKS` | `shared/ui/coproc_update_screen.c:310` | Block %u of %u, %u %% | Block %u von %u, %u %% |
| `CU_P_NOTE_OFF` | `shared/ui/coproc_update_screen.c:293` | Outputs are off. ARMED is blocked until the update ends. | Ausgänge sind aus. ARMED ist bis zum Ende des Updates gesperrt. |
| `CU_P_NOTE_POWER` | `shared/ui/coproc_update_screen.c:295` | If you switch off now, the coprocessor will start with the previous firmware. | Wenn du jetzt ausschaltest, startet der Koprozessor mit der vorherigen Firmware. |
| `CU_P_NOTE_PANEL` | `shared/ui/coproc_update_screen.c:297` | The panel checks that the new firmware is running. Only then is this version kept. | Das Panel prüft, ob die neue Firmware läuft. Erst dann wird sie dauerhaft übernommen. |
| `CU_WHY_NO_ANSWER` | `firmware/panel/main/main.c:8020` | no response | keine Antwort |
| `CU_WHY_REFUSED` | `firmware/panel/main/main.c:8036` | refused, error %u | abgelehnt, Fehler %u |
| `CU_WHY_DIGEST` | `firmware/panel/main/main.c:8023` | firmware check failed | Firmware-Prüfung fehlgeschlagen |
| `CU_WHY_BUSY` | `firmware/panel/main/main.c:8026` | flash memory stayed busy | Flash-Speicher blieb belegt |
| `CU_WHY_RESTARTS` | `firmware/panel/main/main.c:8029` | coprocessor restarted 3 times | Koprozessor 3-mal neu gestartet |
| `CU_WHY_NOT_STARTED` | `firmware/panel/main/main.c:8032` | new firmware did not start | neue Firmware ist nicht gestartet |

## ESC PROGRAMMER

| Text id | Where it is used | English | German |
|---|---|---|---|
| `PG_BLURB_ESC` | `shared/ui/programmer_screen.c:151` | bootloader, CLI and data packets | Bootloader, CLI und Datenpakete |
| `PG_BLURB_SERVO` | `shared/ui/programmer_screen.c:152` | servos with published protocols | Servos mit veröffentlichten Protokollen |
| `PG_BLURB_STICK` | `shared/ui/programmer_screen.c:153` | throttle menus and beep codes | Gasmenüs und Pieptöne |
| `PG_GROUP_MOTOR` | `shared/ui/programmer_screen.c:174,201,225,246` | MOTOR | MOTOR |
| `PG_GROUP_STARTUP` | `shared/ui/programmer_screen.c:183,210,234` | STARTUP | ANLAUF |
| `PG_GROUP_PROTECTION` | `shared/ui/programmer_screen.c:189,216,237,252,282` | PROTECTION | SCHUTZ |
| `PG_GROUP_SOUND` | `shared/ui/programmer_screen.c:195` | SOUND | TON |
| `PG_GROUP_TRAVEL` | `shared/ui/programmer_screen.c:264` | TRAVEL | WEG |
| `PG_GROUP_RESPONSE` | `shared/ui/programmer_screen.c:273` | RESPONSE | VERHALTEN |
| `PG_GROUP_REPORTING` | `shared/ui/programmer_screen.c:258` | REPORTING | MELDUNG |
| `PG_HELP_DIRECTION` | `shared/ui/programmer_screen.c:175,202,226` | Sets the direction of rotation and whether reverse is allowed. | Legt die Drehrichtung fest und ob Rückwärtslauf erlaubt ist. |
| `PG_HELP_TIMING_STEPS` | `shared/ui/programmer_screen.c:178` | Sets how early the ESC commutates. Higher timing suits high-kV motors. | Legt fest, wie früh der ESC kommutiert. Höheres Timing passt zu Motoren mit hohem kV. |
| `PG_HELP_PWM` | `shared/ui/programmer_screen.c:181,208,232` | Higher: quieter, but warmer. Lower: more efficient, but audible. | Höher: leiser, aber wärmer. Niedriger: effizienter, aber hörbar. |
| `PG_HELP_STARTUP` | `shared/ui/programmer_screen.c:184,214` | Sets the starting power before the ESC can detect the rotor position. | Legt die Anlaufleistung fest, bevor der ESC die Rotorposition erkennt. |
| `PG_HELP_DEMAG` | `shared/ui/programmer_screen.c:187` | Reduces power if the magnetic field decays too slowly. Helps prevent stalls under load. | Senkt die Leistung, wenn das Magnetfeld zu langsam abklingt. Hilft gegen Blockieren unter Last. |
| `PG_HELP_BRAKE` | `shared/ui/programmer_screen.c:190,238` | Holds the motor still at zero throttle instead of letting it spin freely. | Hält den Motor bei null Gas fest, statt ihn frei auslaufen zu lassen. |
| `PG_HELP_LVC` | `shared/ui/programmer_screen.c:193,220,256` | Cell voltage at which power is reduced to protect the battery. | Zellspannung, ab der die Leistung zum Schutz des Akkus gesenkt wird. |
| `PG_HELP_BEEP` | `shared/ui/programmer_screen.c:196` | Sets the volume of startup tones and the signal-loss beep. | Legt die Lautstärke der Starttöne und des Warntons bei Signalverlust fest. |
| `PG_HELP_TIMING_DEG` | `shared/ui/programmer_screen.c:205,229` | Timing in degrees ahead of the rotor. More: higher speed and heat. Less: cooler. | Timing in Grad vor dem Rotor. Mehr: höhere Drehzahl und Wärme. Weniger: kühler. |
| `PG_HELP_SINE` | `shared/ui/programmer_screen.c:211,235` | Uses a sine wave at low speed, until the ESC can detect the rotor position. | Nutzt beim Anlauf eine Sinuswelle, bis der ESC die Rotorposition erkennen kann. |
| `PG_HELP_COMPLEMENTARY` | `shared/ui/programmer_screen.c:217` | Drives both sides of the bridge. Runs cooler, but needs correct timing. | Steuert beide Brückenhälften an. Läuft kühler, braucht aber passendes Timing. |
| `PG_HELP_TELEMETRY` | `shared/ui/programmer_screen.c:241,259` | Sends voltage, current and speed back over the signal wire. | Sendet Spannung, Strom und Drehzahl über die Signalleitung zurück. |
| `PG_HELP_MOTOR_TYPE` | `shared/ui/programmer_screen.c:247` | Selects six-step commutation or field-oriented control. | Wählt Sechsschritt-Kommutierung oder feldorientierte Regelung. |
| `PG_HELP_CURRENT` | `shared/ui/programmer_screen.c:250` | Limits the current through the motor, regardless of the requested power. | Begrenzt den Motorstrom unabhängig von der angeforderten Leistung. |
| `PG_HELP_REGEN` | `shared/ui/programmer_screen.c:253` | Feeds braking energy back into the battery instead of the motor. | Leitet Bremsenergie zurück in den Akku statt in den Motor. |
| `PG_HELP_CENTRE` | `shared/ui/programmer_screen.c:265` | Sets neutral in microseconds relative to 1500. | Legt die Neutralstellung in Mikrosekunden um 1500 fest. |
| `PG_HELP_ENDPOINT` | `shared/ui/programmer_screen.c:268` | Limits travel in each direction from the centre. | Begrenzt den Weg von der Mitte aus in beide Richtungen. |
| `PG_HELP_HORN` | `shared/ui/programmer_screen.c:271` | Sets which way the servo horn moves as the pulse gets longer. | Legt fest, in welche Richtung sich das Ruderhorn bei längeren Pulsen bewegt. |
| `PG_HELP_SPEED` | `shared/ui/programmer_screen.c:274` | Slows the servo horn for scale models and gentle linkage movement. | Bewegt das Ruderhorn langsamer, für Scale-Modelle und sanfte Anlenkungen. |
| `PG_HELP_DEADBAND` | `shared/ui/programmer_screen.c:277` | Sets how much position error is allowed before correcting. A wider dead band runs cooler. | Legt fest, ab welcher Abweichung das Servo nachregelt. Ein breiteres Totband verringert die Erwärmung. |
| `PG_HELP_RESOLUTION` | `shared/ui/programmer_screen.c:280` | Sets how finely the commanded position is resolved. | Legt fest, wie fein die vorgegebene Position aufgelöst wird. |
| `PG_HELP_OVERLOAD` | `shared/ui/programmer_screen.c:283` | The servo reduces power if it stays blocked for too long. This helps prevent overheating. | Das Servo senkt die Leistung, wenn es zu lange blockiert. Das hilft, Überhitzung zu vermeiden. |
| `PG_HELP_FAILSAFE` | `shared/ui/programmer_screen.c:286` | Moves to a set position when the signal stops, instead of releasing the servo. | Fährt bei Signalverlust auf eine feste Position, statt das Servo kraftlos zu schalten. |
| `PG_TRANSPORT_ONEWIRE` | `shared/ui/programmer_screen.c:291,293` | one-wire bootloader, 19200 baud | Eindraht-Bootloader, 19200 Baud |
| `PG_TRANSPORT_CLI` | `shared/ui/programmer_screen.c:295` | text commands over the signal wire | Textbefehle über die Signalleitung |
| `PG_TRANSPORT_PACKETS` | `shared/ui/programmer_screen.c:297` | framed data packets, 115200 baud | Datenpakete mit Rahmen, 115200 Baud |
| `PG_TRANSPORT_HITEC` | `shared/ui/programmer_screen.c:302` | D-series servo protocol | Servoprotokoll der D-Serie |
| `PG_NAME_HITEC` | `shared/ui/programmer_screen.c:510` | Hitec servo | Hitec-Servo |
| `PG_DEVICE_BLHELI` | `shared/ui/programmer_screen.c:291` | BLHeli_S 16.7  on  EFM8BB21 | BLHeli_S 16.7  auf  EFM8BB21 |
| `PG_DEVICE_AM32` | `shared/ui/programmer_screen.c:293` | AM32 2.15  on  STM32G071 | AM32 2.15  auf  STM32G071 |
| `PG_DEVICE_ESCAPE32` | `shared/ui/programmer_screen.c:295` | ESCape32 v9  on  AT32F421 | ESCape32 v9  auf  AT32F421 |
| `PG_DEVICE_VESC` | `shared/ui/programmer_screen.c:297` | VESC 6.05  on  STM32F405 | VESC 6.05  auf  STM32F405 |
| `PG_DEVICE_HITEC` | `shared/ui/programmer_screen.c:302` | Hitec servo, D-series | Hitec-Servo, D-Serie |
| `PG_PROFILES_RUN` | `shared/ui/programmer_screen.c:1011` | %d of %d profiles can run | %d von %d Profilen ausführbar |
| `PG_PROTOCOL` | `shared/ui/programmer_screen.c:1015` | %d protocol | %d Protokoll |
| `PG_PROTOCOLS` | `shared/ui/programmer_screen.c:1015` | %d protocols | %d Protokolle |
| `PG_WHAT` | `shared/ui/programmer_screen.c:989` | WHAT ARE YOU PROGRAMMING? | WAS WIRD PROGRAMMIERT? |
| `PG_NOTHING_ANSWERED` | `shared/ui/programmer_screen.c:647` | No device has responded yet. | Noch keine Antwort von einem Gerät. |
| `PG_CONNECT` | `shared/ui/programmer_screen.c:1071` | CONNECT | VERBINDEN |
| `PG_DISCONNECT` | `shared/ui/programmer_screen.c:1070` | DISCONNECT | TRENNEN |
| `PG_PARAMETERS` | `shared/ui/programmer_screen.c:1115` | PARAMETERS | PARAMETER |
| `PG_NO_DEVICE` | `shared/ui/programmer_screen.c:1121` | No device connected. Nothing to show. | Kein Gerät verbunden. Keine Daten zum Anzeigen. |
| `PG_RANGE_OF` | `shared/ui/programmer_screen.c:1129,3702` | %d-%d of %d | %d-%d von %d |
| `PG_WRITE_N` | `shared/ui/programmer_screen.c:1201` | WRITE %d | SCHREIBEN %d |
| `PG_WRITE` | `shared/ui/programmer_screen.c:1203` | WRITE | SCHREIBEN |
| `PG_READ` | `shared/ui/programmer_screen.c:1205` | READ | AUSLESEN |
| `PG_MODELLED` | `shared/ui/programmer_screen.c:530,538,633` | SIMULATED | SIMULIERT |
| `PG_MODEL_NOTE` | `shared/ui/programmer_screen.c:638,1068` | Simulated on the panel. No device is contacted. | Auf dem Panel simuliert. Kein Gerät wird angesprochen. |
| `SP_NEEDS_V` | `shared/ui/programmer_screen.c:1527` | needs %u.%u V; limit %u.%u V | benötigt %u.%u V; Grenze %u.%u V |
| `SP_AT_MOST` | `shared/ui/programmer_screen.c:1904` | Up to %u changes per run. | Höchstens %u Änderungen pro Lauf. |
| `SP_NO_CELLS` | `shared/ui/programmer_screen.c:1916` | Profile has no cell count. Set VOLTAGE on the TIMING page. | Keine Zellenzahl im Profil. SPANNUNG auf der Seite TIMING einstellen. |
| `SP_ABOVE_CAP` | `shared/ui/programmer_screen.c:1935` | %u.%02u V exceeds the SUPPLY limit of %u.%02u V. | %u.%02u V liegt über der SUPPLY-Grenze von %u.%02u V. |
| `SP_BELOW_MIN` | `shared/ui/programmer_screen.c:1941` | VOLTAGE is below the supply minimum of %u.%02u V. | SPANNUNG liegt unter der Netzteil-Untergrenze von %u.%02u V. |
| `SP_ITEM_NOT_HERE` | `shared/ui/programmer_screen.c:1925` | %u %s is not available on this model. | %u %s ist bei diesem Modell nicht verfügbar. |
| `SP_V_OVER_ESC` | `shared/ui/programmer_screen.c:1444` | %u.%u V; ESC maximum %u.%u V | %u.%u V; ESC höchstens %u.%u V |
| `SP_V_OVER_ESC_NOTE` | `shared/ui/programmer_screen.c:1442` | VOLTAGE %u.%u V exceeds the ESC limit of %u.%u V. | SPANNUNG %u.%u V liegt über der ESC-Grenze von %u.%u V. |
| `SP_V_CELLS_OVER_ESC_NOTE` | `shared/ui/programmer_screen.c:1443` | The selected cell count gives %u.%u V, above the ESC limit of %u.%u V. | Die gewählte Zellenzahl ergibt %u.%u V, über der ESC-Grenze von %u.%u V. |
| `SP_V_UNRATED` | `shared/ui/programmer_screen.c:1481` | ESC voltage rating unknown. | Zulässige ESC-Spannung unbekannt. |
| `SP_V_UNRATED_CELLS_NOTE` | `shared/ui/programmer_screen.c:1486` | %u.%u V uses the family's lowest cell count. This ESC's rating is unknown. | %u.%u V aus der kleinsten Zellenzahl dieser Baureihe. Grenze dieses ESC unbekannt. |
| `SP_V_UNRATED_NOTE` | `shared/ui/programmer_screen.c:1483` | VOLTAGE %u.%u V: check the ESC's voltage rating. It is unknown. | SPANNUNG %u.%u V: Zulässige ESC-Spannung unbekannt. Bitte prüfen. |
| `SP_V_UNDER_ESC` | `shared/ui/programmer_screen.c:1451` | %u.%u V; ESC minimum %u.%u V | %u.%u V; ESC mindestens %u.%u V |
| `SP_V_UNDER_ESC_NOTE` | `shared/ui/programmer_screen.c:1451` | VOLTAGE %u.%u V is below the ESC minimum of %u.%u V. | SPANNUNG %u.%u V liegt unter der ESC-Untergrenze von %u.%u V. |
| `SP_I_OVER_ESC` | `shared/ui/programmer_screen.c:1458` | %u.%u A; ESC maximum %u A | %u.%u A; ESC höchstens %u A |
| `SP_I_OVER_ESC_NOTE` | `shared/ui/programmer_screen.c:1458` | CURRENT LIMIT %u.%u A exceeds the ESC rating of %u A. | STROMBEGRENZUNG %u.%u A liegt über der ESC-Grenze von %u A. |
| `SP_I_ABOVE_CAP` | `shared/ui/programmer_screen.c:1946` | CURRENT LIMIT exceeds the SUPPLY limit of %u.%02u A. | STROMBEGRENZUNG liegt über der SUPPLY-Grenze von %u.%02u A. |
| `SP_SUPPLY_NOT_OFF` | `shared/ui/programmer_screen.c:1964` | No current reading confirms that the supply is off. | Kein aktueller Messwert bestätigt, dass das Netzteil aus ist. |
| `SP_OUTPUT_LIVE` | `shared/ui/programmer_screen.c:1957` | Switch the supply output off first. | Schalte zuerst den Netzteilausgang aus. |
| `SP_REFUSED` | `shared/ui/programmer_screen.c:1413` | refused | abgelehnt |
| `SP_SUPPLY_MODEL` | `shared/ui/programmer_screen.c:1880` | Supply is simulated. RUN needs a PD mini. | Netzteil ist simuliert. START benötigt einen PD mini. |
| `SP_BIND_NOT_READ` | `shared/ui/programmer_screen.c:1883` | Output assignment not read. Throttle pin unknown. | Ausgangszuordnung nicht ausgelesen. Gas-Pin unbekannt. |
| `SP_NO_THROTTLE` | `shared/ui/programmer_screen.c:1886` | No throttle pin assigned. Assign one on OUTPUTS. | Kein Gas-Pin zugeordnet. Auf der Seite OUTPUTS zuordnen. |
| `SP_KST_HELD` | `shared/ui/programmer_screen.c:1970` | Servo programmer is using a port. Tap LEAVE on SERVO first. | Servoprogrammierung belegt einen Port. Zuerst auf SERVO auf VERLASSEN tippen. |
| `SP_LIST_COUNT` | `shared/ui/programmer_screen.c:2968` | %d-%d of %d, %d can run | %d-%d von %d, %d ausführbar |
| `SP_LIST_FOUND` | `shared/ui/programmer_screen.c:2965` | %d-%d of %d found, %d can run | %d-%d/%d gefunden, %d ausführbar |
| `SP_MAKERS_COUNT` | `shared/ui/programmer_screen.c:2974` | %d-%d of %d makers, %d can run | %d-%d/%d Hersteller, %d ausführbar |
| `SP_MAKERS_FOUND` | `shared/ui/programmer_screen.c:2971` | %d-%d of %d makers found, %d can run | %d-%d/%d Hersteller, %d ausführbar |
| `SP_MAKER_MODELS` | `shared/ui/programmer_screen.c:2839` | %u OF %u CAN RUN | %u VON %u AUSFÜHRBAR |
| `SP_MAKER_FOUND` | `shared/ui/programmer_screen.c:2836` | %u OF %u FOUND, %u CAN RUN | %u VON %u GEFUNDEN, %u AUSFÜHRBAR |
| `SP_FIND` | `shared/ui/programmer_screen.c:2746` | SEARCH | SUCHE |
| `SP_FIND_TITLE` | `shared/ui/programmer_screen.c:2513` | SEARCH  * = any text | SUCHE  * = beliebiger Text |
| `SP_FIND_NONE` | `shared/ui/programmer_screen.c:2948` | No matching profiles found. | Keine passenden Profile gefunden. |
| `SP_CARD` | `shared/ui/programmer_screen.c:2906` | CARD | KARTE |
| `SP_ITEM` | `shared/ui/programmer_screen.c:2903` | %u ITEM | %u PUNKT |
| `SP_ITEMS` | `shared/ui/programmer_screen.c:2903` | %u ITEMS | %u PUNKTE |
| `SP_TWO_STAGE` | `shared/ui/programmer_screen.c:2908` | TWO-STAGE | ZWEISTUFIG |
| `SP_ONE_STAGE` | `shared/ui/programmer_screen.c:2909` | ONE-STAGE | EINSTUFIG |
| `SP_UNVERIFIED_ALL` | `shared/ui/programmer_screen.c:2986` | No profiles are verified. No ESC recordings are available. | Kein Profil ist geprüft. Es gibt keine ESC-Aufzeichnungen. |
| `SP_WHY_STOP` | `shared/ui/programmer_screen.c:3093` | STOP was pressed. | STOP wurde gedrückt. |
| `SP_WHY_BENCH_STOP` | `shared/ui/programmer_screen.c:3094` | The bench stopped because of touch or the coprocessor. See the alert band for details. | Der Prüfstand hat wegen Touch oder Koprozessor gestoppt. Der Grund steht in der Meldungsleiste. |
| `SP_WHY_DISARMED` | `shared/ui/programmer_screen.c:3095` | The bench switched to SAFE during the run. | Der Prüfstand wurde während des Laufs auf SAFE geschaltet. |
| `SP_WHY_LINK` | `shared/ui/programmer_screen.c:3096` | The coprocessor stopped responding. | Der Koprozessor antwortet nicht mehr. |
| `SP_WHY_SUPPLY_OFF` | `shared/ui/programmer_screen.c:3097` | The supply output switched off: a protection trip or a lost ON state. | Netzteilausgang ausgeschaltet: Schutzabschaltung oder EIN-Zustand verloren. |
| `SP_WHY_SUPPLY_LOST` | `shared/ui/programmer_screen.c:3098` | The supply stopped responding. | Das Netzteil antwortet nicht mehr. |
| `SP_WHY_STALE` | `shared/ui/programmer_screen.c:3099` | No new supply reading for 1000 ms. | Seit 1000 ms kein neuer Messwert vom Netzteil. |
| `SP_WHY_RATE` | `shared/ui/programmer_screen.c:3100` | 3 readings in a row arrived too late for BEEP MIN or GAP MIN. | 3 Messwerte in Folge kamen zu spät für PIEPTON MIN oder PAUSE MIN. |
| `SP_WHY_NOT_ARMED` | `shared/ui/programmer_screen.c:3101` | The bench did not reach ARMED within 3000 ms. | Der Prüfstand hat ARMED nicht innerhalb von 3000 ms erreicht. |
| `SP_WHY_NO_POWER` | `shared/ui/programmer_screen.c:3102` | The supply did not confirm output ON within 3000 ms. | Das Netzteil hat den Ausgang nicht innerhalb von 3000 ms als EIN bestätigt. |
| `SP_WHY_NO_BEEPS` | `shared/ui/programmer_screen.c:3103` | No beep during SILENCE. Check the load and THRESHOLD. | Kein Piepton während STILLE. Last und SCHWELLE prüfen. |
| `SP_WHY_HIGH` | `shared/ui/programmer_screen.c:3104` | Current stayed above THRESHOLD. The motor may be running, or THRESHOLD may be too low. | Strom blieb über SCHWELLE. Der Motor läuft möglicherweise, oder SCHWELLE ist zu niedrig. |
| `SP_WHY_TIMEOUT` | `shared/ui/programmer_screen.c:3105` | The expected beep sequence was not detected within TIMEOUT. | Die erwartete Pieptonfolge wurde nicht innerhalb von ZEITLIMIT erkannt. |
| `SP_WHY_HAND` | `shared/ui/programmer_screen.c:3108` | The manual step was not confirmed with DONE within %u s. | Der Handgriff wurde nicht innerhalb von %u s mit ERLEDIGT bestätigt. |
| `SP_WHY_SUPPLY_ON` | `shared/ui/programmer_screen.c:3112` | The supply did not confirm output OFF within 3000 ms. | Das Netzteil hat den Ausgang nicht innerhalb von 3000 ms als AUS bestätigt. |
| `SP_WHY_TOUCH` | `shared/ui/programmer_screen.c:3113` | Touch events were lost before the bench reached ARMED. | Touch-Ereignisse gingen vor dem Wechsel zu ARMED verloren. |
| `SP_WHY_USER` | `shared/ui/programmer_screen.c:3114` | ABORT was pressed. | ABBRECHEN wurde gedrückt. |
| `SP_WHY_LEFT` | `shared/ui/programmer_screen.c:3115` | The screen was left during the run. | Die Seite wurde während des Laufs verlassen. |
| `SP_PH_READY` | `shared/ui/programmer_screen.c:3125` | READY | BEREIT |
| `SP_PH_ARMING` | `shared/ui/programmer_screen.c:3126` | ARMING | FREIGABE |
| `SP_PH_SIGNAL` | `shared/ui/programmer_screen.c:3127` | SIGNAL | SIGNAL |
| `SP_PH_POWER` | `shared/ui/programmer_screen.c:3128` | POWER ON | EINSCHALTEN |
| `SP_PH_ENTRY` | `shared/ui/programmer_screen.c:3129` | ENTERING MENU | MENÜEINSTIEG |
| `SP_PH_ITEMS` | `shared/ui/programmer_screen.c:3130` | ITEMS | PUNKTE |
| `SP_PH_VALUES` | `shared/ui/programmer_screen.c:3131` | VALUES | WERTE |
| `SP_PH_STORE` | `shared/ui/programmer_screen.c:3132` | STORING | SPEICHERN |
| `SP_PH_CYCLE` | `shared/ui/programmer_screen.c:3133` | POWER CYCLE | AUS- UND EINSCHALTEN |
| `SP_PH_HAND_OFF` | `shared/ui/programmer_screen.c:3134` | MANUAL STEP | HANDGRIFF |
| `SP_PH_HAND_ON` | `shared/ui/programmer_screen.c:3135` | MANUAL STEP, POWER ON | HANDGRIFF, SPANNUNG AN |
| `SP_PH_HAND_END` | `shared/ui/programmer_screen.c:3136` | WAITING FOR THE ESC | WARTEN AUF DEN ESC |
| `SP_PH_OFF` | `shared/ui/programmer_screen.c:3137` | POWER OFF | AUSSCHALTEN |
| `SP_PH_DONE` | `shared/ui/programmer_screen.c:3138,3511` | DONE | FERTIG |
| `SP_PH_ABORTED` | `shared/ui/programmer_screen.c:3139` | ABORTED | ABGEBROCHEN |
| `SP_R_STOP` | `shared/ui/programmer_screen.c:3148` | STOP | STOP |
| `SP_R_BENCH_STOP` | `shared/ui/programmer_screen.c:3149` | BENCH STOPPED | PRÜFSTAND GESTOPPT |
| `SP_R_DISARMED` | `shared/ui/programmer_screen.c:3150` | SAFE | SAFE |
| `SP_R_LINK` | `shared/ui/programmer_screen.c:3151` | LINK LOST | VERBINDUNG VERLOREN |
| `SP_R_SUPPLY_OFF` | `shared/ui/programmer_screen.c:3152` | SUPPLY OFF | NETZTEIL AUS |
| `SP_R_SUPPLY_LOST` | `shared/ui/programmer_screen.c:3153` | SUPPLY NOT ANSWERING | NETZTEIL ANTWORTET NICHT |
| `SP_R_STALE` | `shared/ui/programmer_screen.c:3154` | NO READINGS | KEINE MESSWERTE |
| `SP_R_RATE` | `shared/ui/programmer_screen.c:3155` | READING RATE | MESSRATE |
| `SP_R_NOT_ARMED` | `shared/ui/programmer_screen.c:3156` | STILL SAFE | WEITERHIN SAFE |
| `SP_R_NO_POWER` | `shared/ui/programmer_screen.c:3157` | POWER NOT CONFIRMED | EINSCHALTEN NICHT BESTÄTIGT |
| `SP_R_SUPPLY_ON` | `shared/ui/programmer_screen.c:3158` | SUPPLY STAYS ON | NETZTEIL BLEIBT EIN |
| `SP_R_TOUCH` | `shared/ui/programmer_screen.c:3159` | TOUCH SIGNAL LOST | TOUCH-SIGNAL VERLOREN |
| `SP_R_NO_BEEPS` | `shared/ui/programmer_screen.c:3160` | NO BEEPS | KEINE PIEPTÖNE |
| `SP_R_HIGH` | `shared/ui/programmer_screen.c:3161` | CURRENT STAYS HIGH | STROM BLEIBT HOCH |
| `SP_R_TIMEOUT` | `shared/ui/programmer_screen.c:3162` | TIMEOUT | ZEITLIMIT |
| `SP_R_HAND` | `shared/ui/programmer_screen.c:3163` | NOT CONFIRMED | NICHT BESTÄTIGT |
| `SP_R_USER` | `shared/ui/programmer_screen.c:3164` | ABORTED | ABGEBROCHEN |
| `SP_R_LEFT` | `shared/ui/programmer_screen.c:3165` | SCREEN LEFT | SEITE VERLASSEN |
| `SP_RUN` | `shared/ui/programmer_screen.c:3312,3317` | RUN | LAUF |
| `SP_BEEPS_GROUP` | `shared/ui/programmer_screen.c:3331` | BEEPS IN THIS GROUP | PIEPTÖNE IN DIESER GRUPPE |
| `SP_SELECTION` | `shared/ui/programmer_screen.c:3350` | SELECTION %u OF %u   %s | AUSWAHL %u VON %u   %s |
| `SP_DO_ARMING` | `shared/ui/programmer_screen.c:3357` | Switching to ARMED. Throttle at MIN, supply off. | Wechsel zu ARMED. Gas auf MIN, Netzteil aus. |
| `SP_DO_SIGNAL` | `shared/ui/programmer_screen.c:3360` | Throttle at %s for menu entry. Supply switches on in %u ms. | Gas auf %s zum Menüeinstieg. Netzteil schaltet in %u ms ein. |
| `SP_DO_POWER` | `shared/ui/programmer_screen.c:3368` | Supply is switching on at %u.%02u V. | Netzteil schaltet mit %u.%02u V ein. |
| `SP_DO_ENTRY` | `shared/ui/programmer_screen.c:3388` | Powered on at %s. Menu expected in %u.%u s. | Mit %s eingeschaltet. Menü in %u.%u s erwartet. |
| `SP_DO_HOLD` | `shared/ui/programmer_screen.c:3380` | Keep holding for another %u.%u s. | Noch %u.%u s weiter halten. |
| `SP_DO_LET_GO` | `shared/ui/programmer_screen.c:3384` | Release now. Menu expected in %u.%u s. | Jetzt loslassen. Menü in %u.%u s erwartet. |
| `SP_DO_ITEMS` | `shared/ui/programmer_screen.c:3399` | Counting menu groups. Item %u is selected with the stick at %s. | Menügruppen werden gezählt. Punkt %u wird mit Knüppelstellung %s gewählt. |
| `SP_DO_VALUES` | `shared/ui/programmer_screen.c:3408` | Counting value groups. Value %u is saved with the stick at %s. | Wertgruppen werden gezählt. Wert %u wird mit Knüppelstellung %s gespeichert. |
| `SP_DO_STORE_MOVE` | `shared/ui/programmer_screen.c:3418` | Stick at %s to save the selection. Holding for %u ms. | Knüppel auf %s zum Speichern der Auswahl. Halten für %u ms. |
| `SP_DO_STORE_HOLD` | `shared/ui/programmer_screen.c:3422` | Holding the selection for %u ms while the ESC responds. | Auswahl wird %u ms gehalten, während der ESC antwortet. |
| `SP_DO_CYCLE` | `shared/ui/programmer_screen.c:3427` | Supply off for %u ms before switching on again. | Netzteil bleibt vor dem nächsten Einschalten %u ms aus. |
| `SP_DO_OFF` | `shared/ui/programmer_screen.c:3430` | Supply is switching off. Stick stays at %s until it is off. | Netzteil schaltet aus. Knüppel bleibt bis dahin auf %s. |
| `SP_LAST_NONE` | `shared/ui/programmer_screen.c:3440` | LAST GROUP  none yet | LETZTE GRUPPE  noch keine |
| `SP_LAST` | `shared/ui/programmer_screen.c:3442` | LAST GROUP  %u  %s | LETZTE GRUPPE  %u  %s |
| `SP_NOT_CLEAN` | `shared/ui/programmer_screen.c:3444` | UNCLEAR BEEP GROUP | UNKLARE PIEPTONGRUPPE |
| `SP_TWO_IN_ROW` | `shared/ui/programmer_screen.c:3445` | IN ORDER, TWO IN A ROW | IN FOLGE, ZWEI HINTEREINANDER |
| `SP_IN_ORDER` | `shared/ui/programmer_screen.c:3446` | IN ORDER | RICHTIGE REIHENFOLGE |
| `SP_NOT_IN_ORDER` | `shared/ui/programmer_screen.c:3446` | OUT OF ORDER | FALSCHE REIHENFOLGE |
| `SP_STATS` | `shared/ui/programmer_screen.c:3449` | GROUPS %u   POWER-UPS %u   THROTTLE %s   SUPPLY %s | GRUPPEN %u   EINSCHALTUNGEN %u   GAS %s   NETZTEIL %s |
| `SP_CURRENT` | `shared/ui/programmer_screen.c:3453` | CURRENT %d mA   BASELINE %d mA   READINGS EVERY %u ms | STROM %d mA   GRUNDSTROM %d mA   MESSWERTE ALLE %u ms |
| `SP_TONE_HEAD` | `shared/ui/programmer_screen.c:3285` | TONE  %s   PITCH %s   LOST %s   MISSED %u | ABGRIFF %s   TONHÖHE %s   VERLOREN %s   VERPASST %u |
| `SP_TONE_RUNNING` | `shared/ui/programmer_screen.c:3232` | RUNNING | LÄUFT |
| `SP_TONE_OVERRUN` | `shared/ui/programmer_screen.c:3231` | OVERRUN | ÜBERLAUF |
| `SP_TONE_WAITING` | `shared/ui/programmer_screen.c:3229` | WAITING | WARTET |
| `SP_TONE_NO_PAGE` | `shared/ui/programmer_screen.c:3227` | NO TONE PAGE | KEINE TONE-PAGE |
| `SP_TONE_REFUSED` | `shared/ui/programmer_screen.c:3234` | PIN NOT FREE | PIN BELEGT |
| `SP_TONE_STOPPED` | `shared/ui/programmer_screen.c:3237` | NOT RUNNING | LÄUFT NICHT |
| `SP_TONE_BEEPS` | `shared/ui/programmer_screen.c:3289,3290` | BEEPS | PIEPTÖNE |
| `SP_TONE_NONE` | `shared/ui/programmer_screen.c:3293` | none yet | noch keine |
| `SP_TONE_BEEP` | `shared/ui/programmer_screen.c:3245` | #%u  %u.%u ms  %u.%u Hz | #%u  %u.%u ms  %u.%u Hz |
| `SP_ENDS_RUN` | `shared/ui/programmer_screen.c:3462` | STOP or ABORT ends the run: throttle MIN, supply off, bench SAFE. | STOP oder ABBRECHEN beendet den Lauf: Gas auf MIN, Netzteil aus, Prüfstand auf SAFE. |
| `SP_ABORT` | `shared/ui/programmer_screen.c:3464,4174` | ABORT | ABBRECHEN |
| `SP_RESULT` | `shared/ui/programmer_screen.c:3509` | RESULT | ERGEBNIS |
| `SP_ABORTED_WHY` | `shared/ui/programmer_screen.c:3513` | ABORTED: %s | ABGEBROCHEN: %s |
| `SP_MADE_N` | `shared/ui/programmer_screen.c:3481` | %u of %u selections completed. | %u von %u Auswahlen ausgeführt. |
| `SP_MADE` | `shared/ui/programmer_screen.c:3531` | COMPLETED | AUSGEFÜHRT |
| `SP_NOT_MADE` | `shared/ui/programmer_screen.c:3531` | NOT COMPLETED | NICHT AUSGEFÜHRT |
| `SP_MADE_CUT` | `shared/ui/programmer_screen.c:3530` | UNCERTAIN | UNKLAR |
| `SP_MORE` | `shared/ui/programmer_screen.c:3499` | AND %u MORE, %u COMPLETED | UND %u WEITERE, %u DAVON AUSGEFÜHRT |
| `SP_MORE_UNSURE` | `shared/ui/programmer_screen.c:3496` | AND %u MORE, %u COMPLETED, %u UNCERTAIN | UND %u WEITERE, %u AUSGEFÜHRT, %u UNKLAR |
| `SP_TONES_1` | `shared/ui/programmer_screen.c:3605` | The ESC's confirmation tones are not checked after a selection. | Die Bestätigungstöne des ESC werden nach der Auswahl nicht geprüft. |
| `SP_TONES_2` | `shared/ui/programmer_screen.c:3608` | Listen for them, or read the ESC settings with its programming card. | Höre auf die Töne oder lies den ESC mit seiner Programmierkarte aus. |
| `SP_SAFE_NOW` | `shared/ui/programmer_screen.c:3621` | Throttle MIN, supply off, bench SAFE. | Gas auf MIN, Netzteil aus, Prüfstand auf SAFE. |
| `SP_FROM_PROFILE` | `shared/ui/programmer_screen.c:3632` | FROM PROFILE | AUS PROFIL |
| `SP_TIMING_TITLE` | `shared/ui/programmer_screen.c:3646` | TIMING AND DETECTION | TIMING UND ERKENNUNG |
| `SP_TIMING_NOTE` | `shared/ui/programmer_screen.c:3649` | No ESC recordings are available. All values are defaults, not measurements. | Es gibt keine ESC-Aufzeichnungen. Alle Werte sind Vorgaben, keine Messwerte. |
| `SP_DEFAULTS` | `shared/ui/programmer_screen.c:3683` | DEFAULTS | VORGABEN |
| `SP_CHANGE` | `shared/ui/programmer_screen.c:3694` | CHANGE | ÄNDERN |
| `SP_ACTION` | `shared/ui/programmer_screen.c:3740` | ACTION, NOT A SETTING | AKTION, KEINE EINSTELLUNG |
| `SP_NOTHING_TO_CHOOSE` | `shared/ui/programmer_screen.c:3741` | NO OPTIONS | KEINE AUSWAHL |
| `SP_KEEP` | `shared/ui/programmer_screen.c:3743` | KEEP | BEHALTEN |
| `SP_ONLY_WHEN` | `shared/ui/programmer_screen.c:3765` | %u %s, only when %s | %u %s, nur wenn %s |
| `SP_ONLY_ON` | `shared/ui/programmer_screen.c:3768` | %u %s, only on %u of the models | %u %s, nur bei %u der Modelle |
| `SP_DEFAULT_MARK` | `shared/ui/programmer_screen.c:3785` | (default) | (Vorgabe) |
| `SP_PICK_VALUE` | `shared/ui/programmer_screen.c:3803` | Choose a value to change. KEEP leaves it unchanged. | Wähle einen Wert zum Ändern. BEHALTEN lässt ihn unverändert. |
| `SP_UNVERIFIED` | `shared/ui/programmer_screen.c:3803` | This profile has not been verified. | Dieses Profil ist noch nicht geprüft. |
| `SP_RUN_N` | `shared/ui/programmer_screen.c:3808` | RUN %u | START %u |
| `SP_RUN_BTN` | `shared/ui/programmer_screen.c:3810` | RUN | START |
| `SP_TWO_STAGE_MENU` | `shared/ui/programmer_screen.c:3877` | TWO-STAGE MENU | ZWEISTUFIGES MENÜ |
| `SP_ONE_STAGE_MENU` | `shared/ui/programmer_screen.c:3878` | ONE-STAGE MENU | EINSTUFIGES MENÜ |
| `SP_SHORT_LONG` | `shared/ui/programmer_screen.c:3879` | SHORT AND LONG BEEPS | KURZE UND LANGE PIEPTÖNE |
| `SP_COUNTED` | `shared/ui/programmer_screen.c:3880` | COUNTED BEEPS | GEZÄHLTE PIEPTÖNE |
| `SP_ONE_CHANGE` | `shared/ui/programmer_screen.c:3881` | ONE CHANGE PER POWER-UP | EINE ÄNDERUNG PRO EINSCHALTEN |
| `SP_MANY_CHANGES` | `shared/ui/programmer_screen.c:3882` | MULTIPLE CHANGES PER POWER-UP | MEHRERE ÄNDERUNGEN PRO EINSCHALTEN |
| `SP_SUPPLY_ENTRY` | `shared/ui/programmer_screen.c:3894` | SUPPLY %u.%02u V  %u.%02u A   ENTRY %u.%u s%s | NETZTEIL %u.%02u V  %u.%02u A   EINSTIEG %u.%u s%s |
| `SP_FROM_PROFILE_MARK` | `shared/ui/programmer_screen.c:3898` | (profile) | (Profil) |
| `SP_FROM_SETTING_MARK` | `shared/ui/programmer_screen.c:3899` | (setting) | (Einstellung) |
| `SP_WARN_TITLE` | `shared/ui/programmer_screen.c:4001` | PROPELLER REMOVED? MOTOR SECURED? | PROPELLER AB? MOTOR GESICHERT? |
| `SP_WARN_1` | `shared/ui/programmer_screen.c:4003` | This run powers the ESC from the supply and moves the throttle to MAX | Dieser Lauf versorgt den ESC aus dem Netzteil und setzt das Gassignal auf MAX |
| `SP_WARN_2` | `shared/ui/programmer_screen.c:4003` | and back to step through its programming menu. In rare cases, the motor | und zurück, um das Programmiermenü zu bedienen. In seltenen Fällen kann |
| `SP_WARN_3` | `shared/ui/programmer_screen.c:4003` | may start running. Secure any connected motor firmly | der Motor anlaufen. Befestige einen angeschlossenen Motor sicher |
| `SP_WARN_4` | `shared/ui/programmer_screen.c:4003` | and remove the propeller. A resistor load can replace the motor. | und entferne den Propeller. Ein Lastwiderstand kann den Motor ersetzen. |
| `SP_WARN_5` | `shared/ui/programmer_screen.c:4004` | Beeps are detected and counted from the current. | Die Pieptöne werden anhand des Stroms erkannt und gezählt. |
| `SP_WARN_SUPPLY` | `shared/ui/programmer_screen.c:4024` | SUPPLY %u.%02u V  %u.%02u A   %u CHANGE | NETZTEIL %u.%02u V  %u.%02u A   %u ÄNDERUNG |
| `SP_WARN_SUPPLY_N` | `shared/ui/programmer_screen.c:4024` | SUPPLY %u.%02u V  %u.%02u A   %u CHANGES | NETZTEIL %u.%02u V  %u.%02u A   %u ÄNDERUNGEN |
| `SP_WARN_UNVERIFIED` | `shared/ui/programmer_screen.c:4039` | This profile is unverified. All beep times are defaults. | Dieses Profil ist ungeprüft. Alle Pieptonzeiten sind Vorgaben. |
| `SP_HOLD_TO_RUN` | `shared/ui/programmer_screen.c:4042` | HOLD TO RUN | HALTEN ZUM STARTEN |
| `SP_WARN_HAND` | `shared/ui/programmer_screen.c:3970` | AT THE ESC, BEFORE HOLD TO RUN: | AM ESC, VOR HALTEN ZUM STARTEN: |
| `SP_WARN_HAND_WAIT` | `shared/ui/programmer_screen.c:3965` | Supply OFF is not confirmed. Switch it off. Manual steps appear once OFF is confirmed. | Netzteil AUS ist nicht bestätigt. Schalte es aus. Die Handgriffe erscheinen, sobald AUS bestätigt ist. |
| `SP_WARN_HAND_LATER` | `shared/ui/programmer_screen.c:3992` | The run pauses for each later manual step. Tap DONE to continue. | Der Lauf pausiert bei jedem späteren Handgriff. Tippe danach auf ERLEDIGT. |
| `SP_WARN_HAND_READ` | `shared/ui/programmer_screen.c:3977` | %u steps: read them all under ALL STEPS to enable HOLD TO RUN. | %u Handgriffe: Lies alle unter ALLE HANDGRIFFE. Danach ist HALTEN ZUM STARTEN verfügbar. |
| `SP_STEPS_BTN` | `shared/ui/programmer_screen.c:4047` | ALL STEPS | ALLE HANDGRIFFE |
| `SP_HAND_BTN` | `shared/ui/programmer_screen.c:3547,3697,4065` | MANUAL INTERVENTION REQUIRED | MANUELLER EINGRIFF NÖTIG |
| `SP_HAND_TAG` | `shared/ui/programmer_screen.c:2758` | MANUAL | MANUELL |
| `SP_HAND_WHEN_BEFORE_POWER` | `shared/ui/programmer_screen.c:1366` | BEFORE POWER-UP | VOR DEM EINSCHALTEN |
| `SP_HAND_WHEN_AT_POWER` | `shared/ui/programmer_screen.c:1369` | WHILE THE SUPPLY SWITCHES ON | WÄHREND DAS NETZTEIL EINSCHALTET |
| `SP_HAND_WHEN_AT_POWER_MS` | `shared/ui/programmer_screen.c:1371` | WHILE THE SUPPLY SWITCHES ON, HOLD FOR %u.%u s | BEIM EINSCHALTEN DES NETZTEILS, %u.%u s HALTEN |
| `SP_HAND_WHEN_BEFORE_MENU` | `shared/ui/programmer_screen.c:1376` | AFTER ENTRY, BEFORE THE MENU | NACH DEM EINSTIEG, VOR DEM MENÜ |
| `SP_HAND_WHEN_DURING_MENU` | `shared/ui/programmer_screen.c:1378` | WHILE THE MENU BEEPS PLAY | WÄHREND DER MENÜ-PIEPTÖNE |
| `SP_HAND_WHEN_BEFORE_OFF` | `shared/ui/programmer_screen.c:1380` | BEFORE THE SUPPLY GOES OFF | BEVOR DAS NETZTEIL AUSSCHALTET |
| `SP_HAND_WHEN_AFTER` | `shared/ui/programmer_screen.c:1382` | AFTER PROGRAMMING | NACH DEM PROGRAMMIEREN |
| `SP_HAND_ASKS` | `shared/ui/programmer_screen.c:4089` | Each manual step is shown when needed. The run waits for DONE. | Jeder Handgriff wird zur passenden Zeit angezeigt. Der Lauf wartet auf ERLEDIGT. |
| `SP_HAND_NOT_RUN` | `shared/ui/programmer_screen.c:4091` | The bench cannot run this profile: %s. | Der Prüfstand kann dieses Profil nicht ausführen: %s. |
| `SP_HAND_OVER` | `shared/ui/programmer_screen.c:4087` | The run has ended. Complete the steps listed for after programming. | Der Lauf ist beendet. Führe jetzt die Handgriffe für nach dem Programmieren aus. |
| `SP_HAND_OVER_LIVE` | `shared/ui/programmer_screen.c:3556,4085` | Supply OFF is not confirmed. Do not touch the ESC until it is. | Netzteil AUS ist nicht bestätigt. Berühre den ESC erst, wenn AUS bestätigt ist. |
| `SP_HAND_AFTER_N` | `shared/ui/programmer_screen.c:3575` | %u steps remain after programming. See MANUAL INTERVENTION REQUIRED above. | %u Handgriffe nach dem Programmieren. Siehe oben unter MANUELLER EINGRIFF NÖTIG. |
| `SP_HAND_UNDO` | `shared/ui/programmer_screen.c:3599` | Check the ESC. Anything fitted before power-up may still be attached. | Prüfe den ESC. Vor dem Einschalten angebrachte Teile können noch angeschlossen sein. |
| `SP_HAND_LOCK` | `shared/ui/programmer_screen.c:3617` | Power went off before the ESC confirmed. It may be locked. Check it. | Versorgung aus, bevor der ESC bestätigt hat. Er kann gesperrt sein. Prüfe ihn. |
| `SP_HAND_CUT` | `shared/ui/programmer_screen.c:3618` | Power went off before the ESC confirmed. The mode may not have been saved. | Versorgung aus, bevor der ESC bestätigt hat. Der Modus wurde möglicherweise nicht gespeichert. |
| `SP_PROMPT_TITLE` | `shared/ui/programmer_screen.c:4126` | MANUAL STEP REQUIRED NOW | JETZT HANDGRIFF AUSFÜHREN |
| `SP_PROMPT_OFF` | `shared/ui/programmer_screen.c:4146` | Supply off, throttle at %s. Complete this step, then tap DONE. | Netzteil aus, Gas auf %s. Führe den Handgriff aus und tippe auf ERLEDIGT. |
| `SP_PROMPT_AT_POWER` | `shared/ui/programmer_screen.c:4144` | Begin now. Continue this step while tapping DONE to switch the supply on. | Jetzt beginnen. Handgriff beim Tippen auf ERLEDIGT fortsetzen: Das Netzteil schaltet ein. |
| `SP_PROMPT_HOLD` | `shared/ui/programmer_screen.c:4151` | Keep holding for %u.%u s after the supply switches on. | Halte nach dem Einschalten noch %u.%u s weiter. |
| `SP_PROMPT_ON` | `shared/ui/programmer_screen.c:4140` | ESC powered, throttle at %s. Complete this step, then tap DONE. | ESC unter Spannung, Gas auf %s. Führe den Handgriff aus und tippe auf ERLEDIGT. |
| `SP_PROMPT_END` | `shared/ui/programmer_screen.c:4142` | ESC powered, throttle at %s. Do not touch it. Watch, then tap DONE. | ESC unter Spannung, Gas auf %s. Nicht berühren. Beobachte ihn und tippe dann auf ERLEDIGT. |
| `SP_PROMPT_LISTEN` | `shared/ui/programmer_screen.c:4138` | ESC powered, throttle at %s. Complete this step now. Beep detection is active. | ESC unter Spannung, Gas auf %s. Führe den Handgriff jetzt aus. Pieptöne werden erkannt. |
| `SP_PROMPT_HEARD` | `shared/ui/programmer_screen.c:4158` | The run continues when the correct menu sequence is detected. DONE continues immediately. | Bei der richtigen Menüfolge geht es automatisch weiter. ERLEDIGT setzt den Lauf sofort fort. |
| `SP_PROMPT_LEFT` | `shared/ui/programmer_screen.c:4115,4165` | Without DONE within %u s, the run ends: throttle MIN, supply off, bench SAFE. | Ohne ERLEDIGT innerhalb von %u s endet der Lauf: Gas auf MIN, Netzteil aus, Prüfstand auf SAFE. |
| `SP_PROMPT_LEFT_END` | `shared/ui/programmer_screen.c:4117,4166` | Without DONE within %u s, the run ends and power switches off. The ESC may lock itself. | Ohne ERLEDIGT innerhalb von %u s endet der Lauf und das Netzteil schaltet aus. Der ESC kann sich sperren. |
| `SP_PROMPT_LEFT_CUT` | `shared/ui/programmer_screen.c:4118,4167` | Without DONE within %u s, the run ends and power switches off. The mode may not be saved. | Ohne ERLEDIGT in %u s endet der Lauf und das Netzteil schaltet aus. Der Modus ist vielleicht nicht gespeichert. |
| `SP_HAND_DONE` | `shared/ui/programmer_screen.c:4171` | DONE | ERLEDIGT |
| `SP_POWER_UP_AT` | `shared/ui/programmer_screen.c:3205,3315` | POWER-UP AT %s | EINSCHALTEN BEI %s |
| `ESC_WHY_NO_PROFILE` | `shared/ui/programmer_screen.c:1393` | no profile | kein Profil |
| `ESC_WHY_PERSON` | `shared/ui/programmer_screen.c:1393` | manual work at the ESC required | Handgriff am ESC erforderlich |
| `ESC_WHY_NO_PROCEDURE` | `shared/ui/programmer_screen.c:1393` | no usable programming procedure | kein nutzbarer Programmierablauf |
| `ESC_WHY_AFTER_POWER` | `shared/ui/programmer_screen.c:1394` | menu entry is after power-on | Menüeinstieg erst nach dem Einschalten |
| `ESC_WHY_MELODY` | `shared/ui/programmer_screen.c:1394` | melody menu | Melodie-Menü |
| `ESC_WHY_YES_NO` | `shared/ui/programmer_screen.c:1394` | yes/no menu | Ja/Nein-Menü |
| `ESC_WHY_POSITION` | `shared/ui/programmer_screen.c:1395` | menu uses stick positions | Menü nutzt Knüppelstellungen |
| `ESC_WHY_OWN_KIND` | `shared/ui/programmer_screen.c:1395` | custom menu type | eigener Menütyp |
| `ESC_WHY_PITCH` | `shared/ui/programmer_screen.c:1395` | beeps differ by pitch | Pieptöne unterscheiden sich in der Tonhöhe |
| `ESC_WHY_NO_ITEMS` | `shared/ui/programmer_screen.c:1396` | no menu items | keine Menüpunkte |
| `ESC_WHY_NO_SELECT` | `shared/ui/programmer_screen.c:1396` | no selection movement defined | keine Auswahlbewegung festgelegt |
| `ESC_WHY_REST_ENTRY` | `shared/ui/programmer_screen.c:1396` | rest movement defined, but no entry time | Ruhebewegung festgelegt, aber keine Einstiegszeit |
| `ESC_WHY_STORE_TWO` | `shared/ui/programmer_screen.c:1397` | save movement with a two-stage menu | Speicherbewegung bei zweistufigem Menü |
| `ESC_WHY_TWO_MOVES` | `shared/ui/programmer_screen.c:1397` | two movements, but no value beeps | 2 Bewegungen, aber keine Wert-Pieptöne |
| `ESC_WHY_SELECT_REST` | `shared/ui/programmer_screen.c:1397` | selection position equals rest position | Auswahlstellung entspricht der Ruhestellung |
| `ESC_WHY_VALUE_SELECT` | `shared/ui/programmer_screen.c:1398` | value and selection use the same movement | Wert und Auswahl nutzen dieselbe Bewegung |
| `ESC_WHY_ONE_MOVE` | `shared/ui/programmer_screen.c:1398` | item and value use one movement | Punkt und Wert nutzen 1 Bewegung |
| `ESC_WHY_MANY_ONE` | `shared/ui/programmer_screen.c:1398` | multiple changes in a one-stage menu | mehrere Änderungen in einem einstufigen Menü |
| `ESC_WHY_COUNTED_ONE` | `shared/ui/programmer_screen.c:1399` | counted items in a one-stage menu | gezählte Punkte in einem einstufigen Menü |
| `ESC_WHY_REPEAT` | `shared/ui/programmer_screen.c:1399` | values repeat between menu items | Werte wiederholen sich bei mehreren Menüpunkten |
| `ESC_WHY_STORE_SELECT` | `shared/ui/programmer_screen.c:1399` | save and selection use the same movement | Speichern und Auswahl nutzen dieselbe Bewegung |
| `ESC_WHY_NO_TIMING` | `shared/ui/programmer_screen.c:1400` | no timing defined | kein Timing festgelegt |
| `ESC_WHY_NOTHING` | `shared/ui/programmer_screen.c:1400` | nothing to change | nichts zu ändern |
| `ESC_WHY_TOO_MANY` | `shared/ui/programmer_screen.c:1400` | too many changes | zu viele Änderungen |
| `ESC_WHY_NO_ITEM` | `shared/ui/programmer_screen.c:1401` | menu item does not exist | Menüpunkt nicht vorhanden |
| `ESC_WHY_ACTIONS` | `shared/ui/programmer_screen.c:1401` | reset and exit are actions, not settings | Zurücksetzen und Beenden sind Aktionen, keine Einstellungen |
| `ESC_WHY_NO_VALUE` | `shared/ui/programmer_screen.c:1401` | value does not exist | Wert nicht vorhanden |
| `ESC_WHY_ZERO` | `shared/ui/programmer_screen.c:1402` | value 0 has no beep code | Wert 0 hat keinen Pieptoncode |
| `ESC_WHY_ONE_PER_ITEM` | `shared/ui/programmer_screen.c:1402` | only one change per menu item | nur eine Änderung pro Menüpunkt |
| `ESC_WHY_BEEP_GAP` | `shared/ui/programmer_screen.c:1402` | BEEP MIN and GAP MIN must be above 0 | PIEPTON MIN und PAUSE MIN müssen über 0 liegen |
| `ESC_WHY_LONG` | `shared/ui/programmer_screen.c:1403` | LONG must be above BEEP MIN | LANG muss über PIEPTON MIN liegen |
| `ESC_WHY_LONG_MAX` | `shared/ui/programmer_screen.c:1403` | LONG MAX must be above LONG | LANG MAX muss über LANG liegen |
| `ESC_WHY_GROUP_GAP` | `shared/ui/programmer_screen.c:1403` | GROUP GAP must be above GAP MIN | GRUPPENPAUSE muss über PAUSE MIN liegen |
| `ESC_WHY_THRESHOLD` | `shared/ui/programmer_screen.c:1404` | THRESHOLD must be above HYSTERESIS | SCHWELLE muss über HYSTERESE liegen |
| `ESC_WHY_ENTRY` | `shared/ui/programmer_screen.c:1404` | ENTRY must be above 500 ms | EINSTIEG muss über 500 ms liegen |
| `ESC_WHY_SELECT_WINDOW` | `shared/ui/programmer_screen.c:1404` | GROUP GAP is outside the selection window | GRUPPENPAUSE liegt außerhalb des Auswahlfensters |
| `ESC_WHY_VALUE_WINDOW` | `shared/ui/programmer_screen.c:1405` | GROUP GAP is outside the value window | GRUPPENPAUSE liegt außerhalb des Wertfensters |
| `ESC_WHY_NO_RUN` | `shared/ui/programmer_screen.c:1405` | no run | kein Lauf |
| `ESC_WHY_ONE_VALUE` | `shared/ui/programmer_screen.c:1405` | only one value, so no choice available | nur ein Wert, keine Auswahl möglich |
| `ESC_WHY_HAND` | `shared/ui/programmer_screen.c:1406` | manual step | Handgriff |
| `ESC_WHY_ENTRY_POS` | `shared/ui/programmer_screen.c:1406` | changes require different power-up positions | Änderungen brauchen unterschiedliche Einschaltstellungen |
| `ESC_WHY_ENTRY_TIME` | `shared/ui/programmer_screen.c:1406` | changes require different entry times | Änderungen brauchen unterschiedliche Einstiegszeiten |
| `ESC_WHY_MENU_REST` | `shared/ui/programmer_screen.c:1407` | menu starts at a position other than rest | Menü startet außerhalb der Ruhestellung |
| `ESC_WHY_AFTER_NONE` | `shared/ui/programmer_screen.c:1407` | post-value movement does not change position | Bewegung nach dem Wert ändert die Position nicht |
| `ESC_WHY_AFTER_TWO` | `shared/ui/programmer_screen.c:1407` | post-value movements with a two-stage menu | Bewegungen nach dem Wert bei zweistufigem Menü |

## FILES & UPDATE

| Text id | Where it is used | English | German |
|---|---|---|---|
| `UPD_CHECK` | `shared/ui/upd_screen.c:503` | CHECK FOR UPDATES | NACH UPDATES SUCHEN |
| `UPD_REMIND_ON` | `shared/ui/upd_screen.c:507` | REMINDER AT START: ON | ERINNERUNG BEIM START: EIN |
| `UPD_REMIND_OFF` | `shared/ui/upd_screen.c:507` | REMINDER AT START: OFF | ERINNERUNG BEIM START: AUS |
| `UPD_HOLD_DL` | `shared/ui/upd_screen.c:510,749` | HOLD 2 s: DOWNLOAD & INSTALL | 2 s HALTEN: LADEN & INSTALLIEREN |
| `UPD_KEEP_NET` | `shared/ui/upd_screen.c:281,532` | Save a network under LOGS > WI-FI. | Speichere ein Netzwerk unter LOGS > WLAN. |
| `UPD_ABOUT_1` | `shared/ui/upd_screen.c:568` | With reminders on, the panel checks GitHub at startup. | Mit Erinnerung prüft das Panel beim Start auf GitHub. |
| `UPD_ABOUT_2` | `shared/ui/upd_screen.c:570` | Wi-Fi stays on for no more than 30 s. | WLAN bleibt dabei höchstens 30 s eingeschaltet. |
| `UPD_ASKING` | `shared/ui/upd_screen.c:234` | Checking GitHub. | GitHub wird geprüft. |
| `UPD_LEFT` | `shared/ui/upd_screen.c:262` | %u s left | noch %u s |
| `UPD_NEWER` | `shared/ui/upd_screen.c:270` | Version %s is available. | Version %s ist verfügbar. |
| `UPD_CURRENT` | `shared/ui/upd_screen.c:273` | Up to date. Panel version: %s. | Aktuell. Panel-Version: %s. |
| `UPD_LATEST` | `shared/ui/upd_screen.c:274` | Latest release on GitHub: %s. | Neuestes Release auf GitHub: %s. |
| `UPD_STAGED` | `shared/ui/upd_screen.c:277` | Download checked. Installing now. | Download geprüft. Installation läuft. |
| `UPD_WAIT_COPROC` | `shared/ui/upd_screen.c:201` | Waiting for the coprocessor update to finish. | Wartet auf das Ende des Koprozessor-Updates. |
| `UPD_WAIT_DRAWING` | `shared/ui/upd_screen.c:202` | Waiting: a board drawing is being downloaded or saved. | Wartet: Eine Platinenzeichnung wird geladen oder gespeichert. |
| `UPD_WAIT_BUSY` | `shared/ui/upd_screen.c:208,224` | Waiting: a check or installation is already running. | Wartet: Eine Prüfung oder Installation läuft bereits. |
| `UPD_STOP_FILES` | `shared/ui/upd_screen.c:203,220` | Stopped: FILES & UPDATE is using Wi-Fi. | Abgebrochen: FILES & UPDATE nutzt das WLAN. |
| `UPD_STOP_CANCEL` | `shared/ui/upd_screen.c:227` | Cancelled. | Abgebrochen. |
| `UPD_STOP_COPROC` | `shared/ui/upd_screen.c:221` | Stopped: the coprocessor update has started. | Abgebrochen: Koprozessor-Update gestartet. |
| `UPD_STOP_INSTALL` | `shared/ui/upd_screen.c:222` | Stopped: panel firmware installation has started. | Abgebrochen: Installation der Panel-Firmware gestartet. |
| `UPD_STOP_DRAWING` | `shared/ui/upd_screen.c:223` | Stopped: a board drawing is being downloaded. | Abgebrochen: Eine Platinenzeichnung wird geladen. |
| `UPD_FAIL_TIMEOUT` | `shared/ui/upd_screen.c:296` | No response within %u s. | Keine Antwort innerhalb von %u s. |
| `UPD_FAIL_CONNECT` | `shared/ui/upd_screen.c:299` | No connection to GitHub. | Keine Verbindung zu GitHub. |
| `UPD_FAIL_REFUSED` | `shared/ui/upd_screen.c:302` | GitHub rejected the request: HTTP %d. | GitHub hat die Anfrage abgelehnt: HTTP %d. |
| `UPD_FAIL_UNEXPECTED` | `shared/ui/upd_screen.c:305` | Unexpected answer from GitHub. | Unerwartete Antwort von GitHub. |
| `UPD_FAIL_SUMS` | `shared/ui/upd_screen.c:309` | Cannot use the SHA256SUMS file. | SHA256SUMS-Datei nicht verwendbar. |
| `UPD_FAIL_HASH` | `shared/ui/upd_screen.c:317` | The SHA-256 checksum does not match SHA256SUMS. | SHA-256-Prüfsumme stimmt nicht mit SHA256SUMS überein. |
| `UPD_FAIL_IMAGE` | `shared/ui/upd_screen.c:320` | The firmware did not pass the panel's checks. | Die Firmware hat die Prüfungen des Panels nicht bestanden. |
| `UPD_FAIL_MEMORY` | `shared/ui/upd_screen.c:324` | Not enough memory for the firmware. | Nicht genug Speicher für die Firmware. |
| `UPD_REL_MISSING` | `shared/ui/upd_screen.c:149` | The response has no Location header. | In der Antwort fehlt der Location-Header. |
| `UPD_REL_LONG` | `shared/ui/upd_screen.c:150` | The address is too long. | Die Adresse ist zu lang. |
| `UPD_REL_CHAR` | `shared/ui/upd_screen.c:151` | A byte is outside printable ASCII. | Ein Byte liegt außerhalb von druckbarem ASCII. |
| `UPD_REL_PLACE` | `shared/ui/upd_screen.c:152` | The address is in an unexpected location. | Die Adresse steht an einer unerwarteten Stelle. |
| `UPD_REL_VERSION` | `shared/ui/upd_screen.c:157` | The version text has an invalid format. | Der Versionstext hat ein ungültiges Format. |
| `UPD_REL_IMG_VERSION` | `shared/ui/upd_screen.c:156` | The firmware file specifies a different version. | Die Firmware-Datei nennt eine andere Version. |
| `UPD_REL_STATUS` | `shared/ui/upd_screen.c:311` | HTTP status %d. | HTTP-Status %d. |
| `UPD_REL_HOPS` | `shared/ui/upd_screen.c:158` | More than 3 redirects. | Mehr als 3 Weiterleitungen. |
| `UPD_REL_HOST` | `shared/ui/upd_screen.c:159` | Redirect to a host that is not allowed. | Weiterleitung zu einem nicht erlaubten Host. |
| `UPD_REL_NO_LENGTH` | `shared/ui/upd_screen.c:160` | The response does not specify a size. | Die Antwort enthält keine Größenangabe. |
| `UPD_REL_EMPTY` | `shared/ui/upd_screen.c:161` | The response is empty. | Die Antwort ist leer. |
| `UPD_REL_TOO_BIG` | `shared/ui/upd_screen.c:162` | The download is too large for the panel. | Der Download ist zu groß für das Panel. |
| `UPD_REL_SHORT` | `shared/ui/upd_screen.c:163` | Received less data than expected. | Weniger Daten als angekündigt empfangen. |
| `UPD_REL_OVERRUN` | `shared/ui/upd_screen.c:164` | Received more data than expected. | Mehr Daten als angekündigt empfangen. |
| `UPD_REL_SUMS_BAD` | `shared/ui/upd_screen.c:165` | A checksum line has an invalid format. | Eine Prüfsummenzeile hat ein ungültiges Format. |
| `UPD_REL_SUMS_NONE` | `shared/ui/upd_screen.c:166` | No checksum entry for this firmware. | Kein Prüfsummeneintrag für diese Firmware. |
| `UPD_REL_SUMS_TWICE` | `shared/ui/upd_screen.c:167` | Two checksum entries for this firmware. | Zwei Prüfsummeneinträge für diese Firmware. |
| `UPD_R_TITLE` | `shared/ui/upd_screen.c:724` | PANEL UPDATE AVAILABLE | PANEL-UPDATE VERFÜGBAR |
| `UPD_R_VERSIONS` | `shared/ui/upd_screen.c:727` | Version %s is available. This panel runs %s. | Version %s ist verfügbar. Auf diesem Panel läuft %s. |
| `UPD_R_WHAT_1` | `shared/ui/upd_screen.c:729` | Hold to download the firmware from GitHub and check it. | Halte die Taste, um die Firmware von GitHub zu laden und zu prüfen. |
| `UPD_R_WHAT_2` | `shared/ui/upd_screen.c:731` | It is then installed and the panel restarts. | Danach wird sie installiert und das Panel startet neu. |
| `UPD_R_NEVER` | `shared/ui/upd_screen.c:733` | Turn reminders back on under SETUP > UPDATES. | Erinnerungen unter SETUP > UPDATES wieder einschalten. |
| `UPD_IGNORE` | `shared/ui/upd_screen.c:745` | NOT NOW | JETZT NICHT |
| `UPD_NEVER` | `shared/ui/upd_screen.c:747` | NEVER REMIND ME | NICHT MEHR ERINNERN |
| `UPD_DL_SUMS` | `shared/ui/upd_screen.c:235` | Reading SHA256SUMS. | SHA256SUMS wird gelesen. |
| `UPD_DL_IMAGE` | `shared/ui/upd_screen.c:236` | Downloading firmware. | Firmware wird heruntergeladen. |
| `UPD_DL_CHECKING` | `shared/ui/upd_screen.c:237` | Checking the SHA-256 checksum and firmware. | SHA-256-Prüfsumme und Firmware werden geprüft. |

## Keypads

| Text id | Where it is used | English | German |
|---|---|---|---|
| `SPACE` | `shared/ui/ui_textkey.c:42` | SPACE | LEERZEICHEN |
| `KEYPAD_RANGE` | `shared/ui/ui_keypad.c:247` | %.*f to %.*f %s | %.*f bis %.*f %s |
| `TEXTKEY_COUNT` | `shared/ui/ui_textkey.c:355` | %d of %d | %d von %d |
| `TEXTKEY_NEEDED` | `shared/ui/ui_textkey.c:353` | Enter a name. | Gib einen Namen ein. |
| `TEXTKEY_SHORT` | `shared/ui/ui_textkey.c:347` | Enter at least %d characters. | Gib mindestens %d Zeichen ein. |
| `TEXTKEY_SSID_NEEDED` | `shared/ui/ui_textkey.c:350` | Enter a network name. | Gib einen Netzwerknamen ein. |
| `TK_SHOW` | `shared/ui/ui_textkey.c:140` | SHOW | ANZEIGEN |
| `TK_HIDE` | `shared/ui/ui_textkey.c:140` | HIDE | VERBERGEN |

## LOG VIEWER

| Text id | Where it is used | English | German |
|---|---|---|---|
| `LOG_NO_SOURCE` | `shared/ui/log_viewer_screen.c:457` | No file source available. | Keine Dateiquelle verfügbar. |
| `LOG_CANNOT_OPEN` | `shared/ui/log_viewer_screen.c:463,657` | Cannot open %s. | %s lässt sich nicht öffnen. |
| `LOG_IS_FOLDER` | `shared/ui/log_viewer_screen.c:690` | %s is a folder. | %s ist ein Ordner. |
| `LOG_CANNOT_DELETE` | `shared/ui/log_viewer_screen.c:1335` | Cannot delete %s. | %s lässt sich nicht löschen. |
| `LOG_DELETED` | `shared/ui/log_viewer_screen.c:1351` | %s deleted. | %s gelöscht. |
| `LOG_ERR_OK` | `shared/ui/log_viewer_screen.c:408` | ok | ok |
| `LOG_ERR_READ` | `shared/ui/log_viewer_screen.c:409` | Cannot read the file. | Datei lässt sich nicht lesen. |
| `LOG_ERR_NO_HEADER` | `shared/ui/log_viewer_screen.c:410` | The header row is missing. | Die Kopfzeile fehlt. |
| `LOG_ERR_NO_ROWS` | `shared/ui/log_viewer_screen.c:411` | No data rows after the header. | Keine Datenzeilen nach der Kopfzeile. |
| `LOG_ERR_NO_NUMERIC` | `shared/ui/log_viewer_screen.c:412` | No numeric columns found. Check the separator. | Keine Spalten mit Zahlen gefunden. Trennzeichen prüfen. |
| `LOG_ERR_COLUMNS` | `shared/ui/log_viewer_screen.c:413` | Too many columns. | Zu viele Spalten. |
| `LOG_ERR_MEMORY` | `shared/ui/log_viewer_screen.c:414` | Not enough memory. | Nicht genug Speicher. |
| `LOG_ERR_ARG` | `shared/ui/log_viewer_screen.c:415` | Invalid argument. | Ungültiges Argument. |
| `LOG_ERR_UNKNOWN` | `shared/ui/log_viewer_screen.c:416` | Unknown error. | Unbekannter Fehler. |
| `LOG_DELETE_TITLE` | `shared/ui/log_viewer_screen.c:724` | DELETE FROM CARD | VON KARTE LÖSCHEN |
| `LOG_DELETE_WHAT` | `shared/ui/log_viewer_screen.c:732` | This deletes the file from the card. | Die Datei wird von der Karte gelöscht. |
| `LOG_DELETE_FINAL` | `shared/ui/log_viewer_screen.c:734` | This cannot be undone. | Das lässt sich nicht rückgängig machen. |
| `LOG_DELETE` | `shared/ui/log_viewer_screen.c:738,821` | DELETE | LÖSCHEN |
| `LOG_SD_CARD` | `shared/ui/log_viewer_screen.c:746` | SD CARD | SD-KARTE |
| `LOG_NO_LOGS` | `shared/ui/log_viewer_screen.c:761` | NO RECORDINGS | KEINE AUFZEICHNUNGEN |
| `LOG_NO_CARD` | `shared/ui/log_viewer_screen.c:762` | No card. Insert one and tap RESCAN. | Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN. |
| `LOG_NO_CSV` | `shared/ui/log_viewer_screen.c:762` | No .csv files in the top-level folder. | Keine .csv-Dateien im Hauptverzeichnis. |
| `LOG_RUNS_HERE` | `shared/ui/log_viewer_screen.c:765` | Runs are saved from BENCH001.CSV onwards, in numbered order. | Läufe werden ab BENCH001.CSV fortlaufend nummeriert gespeichert. |
| `LOG_N_OF_FILES` | `shared/ui/log_viewer_screen.c:778` | %d OF %d FILES | %d VON %d DATEIEN |
| `LOG_FILES` | `shared/ui/log_viewer_screen.c:781` | FILES | DATEIEN |
| `LOG_DIR` | `shared/ui/log_viewer_screen.c:804` | FOLDER | ORDNER |
| `LOG_RESCAN` | `shared/ui/log_viewer_screen.c:811` | RESCAN | AKTUALISIEREN |
| `LOG_OPEN` | `shared/ui/log_viewer_screen.c:818` | OPEN | ÖFFNEN |
| `LOG_WIFI` | `shared/ui/log_viewer_screen.c:815` | WI-FI | WLAN |
| `LOG_CANNOT_READ` | `shared/ui/log_viewer_screen.c:854` | CANNOT READ | NICHT LESBAR |
| `LOG_UNREADABLE` | `shared/ui/log_viewer_screen.c:856` | unreadable | nicht lesbar |
| `LOG_TRY_ANOTHER` | `shared/ui/log_viewer_screen.c:859` | Choose a different separator below, or open another file. | Wähle unten ein anderes Trennzeichen oder öffne eine andere Datei. |
| `LOG_DETECTED` | `shared/ui/log_viewer_screen.c:862` | DETECTED | ERKANNT |
| `LOG_KEY_SEPARATOR` | `shared/ui/log_viewer_screen.c:880` | SEPARATOR | TRENNZEICHEN |
| `LOG_KEY_NUMBERS` | `shared/ui/log_viewer_screen.c:890` | NUMBER FORMAT | ZAHLENFORMAT |
| `LOG_KEY_ROWS` | `shared/ui/log_viewer_screen.c:899` | ROWS | ZEILEN |
| `LOG_KEY_COLUMNS` | `shared/ui/log_viewer_screen.c:904` | COLUMNS | SPALTEN |
| `LOG_KEY_TIME` | `shared/ui/log_viewer_screen.c:915` | TIME AXIS | ZEITACHSE |
| `LOG_KEY_RAGGED` | `shared/ui/log_viewer_screen.c:922` | UNEVEN ROWS | UNGLEICHE ZEILEN |
| `LOG_SEMICOLON` | `shared/ui/log_viewer_screen.c:424` | SEMICOLON | SEMIKOLON |
| `LOG_COMMA` | `shared/ui/log_viewer_screen.c:425` | COMMA | KOMMA |
| `LOG_TAB` | `shared/ui/log_viewer_screen.c:426` | TAB | TAB |
| `LOG_PIPE` | `shared/ui/log_viewer_screen.c:427` | PIPE | SENKRECHTER STRICH |
| `LOG_GERMAN` | `shared/ui/log_viewer_screen.c:887` | GERMAN | DEUTSCH |
| `LOG_ENGLISH` | `shared/ui/log_viewer_screen.c:888` | ENGLISH | ENGLISCH |
| `LOG_SET` | `shared/ui/log_viewer_screen.c:884` | (SET) | (GESETZT) |
| `LOG_CAPPED` | `shared/ui/log_viewer_screen.c:898` | (CAPPED) | (BEGRENZT) |
| `LOG_ROW_INDEX` | `shared/ui/log_viewer_screen.c:913` | ROW INDEX | ZEILENINDEX |
| `LOG_PLUS_LONG` | `shared/ui/log_viewer_screen.c:921` | +LONG | +LANG |
| `LOG_EVIDENCE` | `shared/ui/log_viewer_screen.c:941` | MATCHES  de %d  en %d  unclear %d | TREFFER  de %d  en %d  unklar %d |
| `LOG_MIXED` | `shared/ui/log_viewer_screen.c:948` | MIXED GERMAN AND ENGLISH NUMBER FORMATS | DEUTSCHE UND ENGLISCHE ZAHLENFORMATE GEMISCHT |
| `LOG_TOO_LONG` | `shared/ui/log_viewer_screen.c:953` | %d ROW(S) TOO LONG. SOME CELLS MISSING. | %d ZEILE(N) ZU LANG. EINIGE ZELLEN FEHLEN. |
| `LOG_ROWS_DIFFER` | `shared/ui/log_viewer_screen.c:957` | UNEVEN ROWS. CHECK THE SEPARATOR. | UNGLEICHE ZEILEN. TRENNZEICHEN PRÜFEN. |
| `LOG_LAST_CUT` | `shared/ui/log_viewer_screen.c:968` | INCOMPLETE LAST ROW. SKIPPED. | LETZTE ZEILE UNVOLLSTÄNDIG. ÜBERSPRUNGEN. |
| `LOG_UNSETTLED` | `shared/ui/log_viewer_screen.c:960` | FORMAT UNCLEAR. CHECK BELOW. | FORMAT UNKLAR. UNTEN PRÜFEN. |
| `LOG_HINT_NUM` | `shared/ui/log_viewer_screen.c:975` | WRONG VALUES? CHANGE NUM. | FALSCHE WERTE? ZAHLEN ÄNDERN. |
| `LOG_HINT_SEP` | `shared/ui/log_viewer_screen.c:977` | WRONG COLUMNS? CHANGE SEP. | FALSCHE SPALTEN? TRENNER ÄNDERN. |
| `LOG_PLOT_N` | `shared/ui/log_viewer_screen.c:982` | PLOT  %d/%d | GRAFIK  %d/%d |
| `LOG_TIME` | `shared/ui/log_viewer_screen.c:1022` | TIME | ZEIT |
| `LOG_TEXT` | `shared/ui/log_viewer_screen.c:1024` | TEXT | TEXT |
| `LOG_BTN_SEP` | `shared/ui/log_viewer_screen.c:1039` | SEP %s | TRENNER %s |
| `LOG_BTN_NUM` | `shared/ui/log_viewer_screen.c:1045` | NUM %s | ZAHLEN %s |
| `LOG_PLOT` | `shared/ui/log_viewer_screen.c:1052` | PLOT | GRAFIK |
| `LOG_NOTHING_LOADED` | `shared/ui/log_viewer_screen.c:1105` | NO FILE LOADED | KEINE DATEI GELADEN |
| `LOG_PICK_FIRST` | `shared/ui/log_viewer_screen.c:1107` | Choose a file first. | Wähle zuerst eine Datei. |
| `LOG_SUMMARY` | `shared/ui/log_viewer_screen.c:1112` | %s   %d ROWS   %.2f s   %.0f Hz | %s   %d ZEILEN   %.2f s   %.0f Hz |
| `LOG_AT_TIME` | `shared/ui/log_viewer_screen.c:1216` | t = %.3f s   sample %d/%d | t = %.3f s   Messpunkt %d/%d |
| `LOG_AT_ROW` | `shared/ui/log_viewer_screen.c:1221` | row %d of %d   (no time column) | Zeile %d von %d   (keine Zeitspalte) |
| `LOG_UNREADABLE_CELLS` | `shared/ui/log_viewer_screen.c:1227` | %d CELLS UNREADABLE | %d ZELLEN UNLESBAR |

## MAIN MENU

| Text id | Where it is used | English | German |
|---|---|---|---|
| `OV_MOTOR` | `shared/ui/overview_screen.c:46` | drive, V/A, rpm | Antrieb, V/A, Drehzahl |
| `OV_SERVO` | `shared/ui/overview_screen.c:48` | travel, current | Weg, Strom |
| `OV_SUPPLY` | `shared/ui/overview_screen.c:50` | set and log V/A | V/A einstellen, aufzeichnen |
| `OV_ANALYSER` | `shared/ui/overview_screen.c:52` | buses and frames | Busse, Frames |
| `OV_LOGS` | `shared/ui/overview_screen.c:54` | record and view data | Daten aufzeichnen und ansehen |
| `OV_SETUP` | `shared/ui/overview_screen.c:56` | pack and output | Akku und Ausgang |
| `OV_BATTERY` | `shared/ui/overview_screen.c:58` | cells, iR, mAh | Zellen, iR, mAh |
| `OV_BALANCE` | `shared/ui/overview_screen.c:60` | vibration, phase | Vibration, Phase |
| `OV_PROGRAMMER` | `shared/ui/overview_screen.c:62` | ESC and servo | ESC und Servo |
| `OV_MODELLED` | `shared/ui/overview_screen.c:179` | SIMULATED | SIMULIERT |

## MOTOR & ESC

| Text id | Where it is used | English | German |
|---|---|---|---|
| `MO_RPM` | `shared/ui/motor_screen.c:138` | RPM | RPM |
| `MO_RATE_ERR` | `shared/ui/motor_screen.c:868` | %d Hz   ERR %lu | %d Hz   FEHLER %lu |
| `MO_TABLE_HEAD` | `shared/ui/motor_screen.c:1045` | CHANNEL        NOW            PEAK | KANAL          JETZT          SPITZE |
| `MO_ROW_RPM` | `shared/ui/motor_screen.c:1031` | RPM | RPM |
| `MO_TABLE_MAX` | `shared/ui/motor_screen.c:1040,1040,1040` | max | max |
| `MO_TABLE_DIFF` | `shared/ui/motor_screen.c:1021` | diff | Abw. |
| `MO_TABLE_CLIPPED` | `shared/ui/motor_screen.c:1052` | CLIPPED | ÜBERSTEUERT |
| `MO_ESC_SAYS` | `shared/ui/motor_screen.c:1014` | ESC SAYS | ESC MELDET |
| `MO_TAG_LIVE` | `shared/ui/motor_screen.c:1191` | LIVE TELEMETRY | TELEMETRIE LIVE |
| `MO_TAG_HELD` | `shared/ui/motor_screen.c:1191` | LAST TELEMETRY | LETZTE TELEMETRIE |
| `MO_TAG_IDLE` | `shared/ui/motor_screen.c:1191` | TELEMETRY IDLE | TELEMETRIE INAKTIV |
| `MO_NO_RUN` | `shared/ui/motor_screen.c:1281` | No run recorded yet. | Noch kein Lauf aufgezeichnet. |
| `MO_HINT` | `shared/ui/motor_screen.c:1400` | DRAG THE BAR. TAP EITHER END TO CHANGE BY ONE POINT. | BALKEN ZIEHEN. AN DEN ENDEN TIPPEN, UM EINEN PUNKT ZU ÄNDERN. |
| `MO_TARGET` | `shared/ui/motor_screen.c:572,576` | TARGET: %s | ZIEL: %s |
| `MO_TARGET_NONE` | `shared/ui/motor_screen.c:568` | TARGET: NO MOTOR PIN ASSIGNED | ZIEL: KEIN MOTOR-PIN ZUGEORDNET |
| `MO_TARGET_UNREAD` | `shared/ui/motor_screen.c:564` | TARGET: ASSIGNMENT NOT READ | ZIEL: ZUORDNUNG NICHT AUSGELESEN |

## OUTPUTS

| Text id | Where it is used | English | German |
|---|---|---|---|
| `OUT_PROTOCOL` | `shared/ui/outputs_screen.c:689` | PROTOCOL | PROTOKOLL |
| `OUT_PINS_OF` | `shared/ui/outputs_screen.c:698` | %u OF %u PINS | %u VON %u PINS |
| `OUT_CHANNELS_ON_PIN` | `shared/ui/outputs_screen.c:702` | %u CHANNELS ON ONE PIN | %u KANÄLE AUF EINEM PIN |
| `OUT_TAKES_PIN` | `shared/ui/outputs_screen.c:618` | %s TAKES %u PIN | %s BELEGT %u PIN |
| `OUT_TAKES_PINS` | `shared/ui/outputs_screen.c:618` | %s TAKES %u PINS | %s BELEGT %u PINS |
| `OUT_ALL_SLOTS` | `shared/ui/outputs_screen.c:623` | ALL %u SLOTS IN USE | ALLE %u PLÄTZE BELEGT |
| `OUT_NEEDS_CHANNEL` | `shared/ui/outputs_screen.c:628` | NEEDS %u CHANNEL, %u FREE | BENÖTIGT %u KANAL, %u FREI |
| `OUT_NEEDS_CHANNELS` | `shared/ui/outputs_screen.c:629` | NEEDS %u CHANNELS, %u FREE | BENÖTIGT %u KANÄLE, %u FREI |
| `OUT_SLICE_OF` | `shared/ui/outputs_screen.c:559` | SAME PWM SLICE AS GP%u | GLEICHER PWM-SLICE WIE GP%u |
| `OUT_COMPARE_OF` | `shared/ui/outputs_screen.c:562` | SAME PWM CHANNEL AS GP%u | GLEICHER PWM-KANAL WIE GP%u |
| `OUT_SLICE_WHY` | `shared/ui/outputs_screen.c:590` | GP%u: SHARES PWM SLICE WITH PROG PORT GP%u | GP%u: TEILT PWM-SLICE MIT PROG-PORT GP%u |
| `OUT_COMPARE_WHY` | `shared/ui/outputs_screen.c:591` | GP%u: SAME PWM CHANNEL AS GP%u | GP%u: GLEICHER PWM-KANAL WIE GP%u |
| `OUT_LAST_WRITE` | `shared/ui/outputs_screen.c:727` | LAST WRITE | LETZTER SCHREIBVORGANG |
| `OUT_WRITTEN` | `shared/ui/outputs_screen.c:490` | WRITTEN | GESCHRIEBEN |
| `OUT_REFUSED` | `shared/ui/outputs_screen.c:492` | REFUSED | ABGELEHNT |
| `OUT_NOT_WRITTEN` | `shared/ui/outputs_screen.c:494` | NOT WRITTEN | NICHT GESCHRIEBEN |
| `OUT_NO_LINK` | `shared/ui/outputs_screen.c:491` | NO LINK | KEINE VERBINDUNG |
| `OUT_OFF_VIEW` | `shared/ui/outputs_screen.c:608` | OFF: VIEW ALL, NO EDITING | AUS: ALLES ANZEIGEN, NICHTS ÄNDERN |
| `OUT_NOT_READ` | `shared/ui/outputs_screen.c:538` | ASSIGNMENT NOT READ. EDITING LOCKED. | ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT. |
| `OUT_NOT_MAPPED` | `shared/ui/outputs_screen.c:542` | NO VALID ASSIGNMENT IN THE PAGES | KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES |
| `OUT_HOLD_UNBIND` | `shared/ui/outputs_screen.c:721` | HOLD: CLEAR ALL PIN ASSIGNMENTS | HALTEN: ALLE PIN-ZUORDNUNGEN LÖSCHEN |

## PICK A PIN

| Text id | Where it is used | English | German |
|---|---|---|---|
| `PK_PINS` | `shared/ui/picker_screen.c:895` | PINS | PINS |
| `PK_PAD` | `shared/ui/picker_screen.c:898` | PAD | PAD |
| `PK_SLOTS_OF` | `shared/ui/picker_screen.c:696` | %u OF %u SLOTS IN USE | %u VON %u PLÄTZEN BELEGT |
| `PK_NONE_YET` | `shared/ui/picker_screen.c:919` | NONE YET | NOCH KEINE |
| `PK_IN_USE` | `shared/ui/picker_screen.c:1004` | IN USE | BELEGT |
| `PK_BY_OTHERS` | `shared/ui/picker_screen.c:1008` | BY OTHERS | VON ANDEREN |
| `PK_NONE` | `shared/ui/picker_screen.c:1038` | NONE | KEINE |
| `PK_NOT_READ` | `shared/ui/picker_screen.c:709` | NOT READ | NICHT AUSGELESEN |
| `PK_NO_ART_1` | `shared/ui/picker_screen.c:1234` | NO BOARD IMAGE AVAILABLE | KEIN PLATINENBILD VERFÜGBAR |
| `PK_NO_ART_2` | `shared/ui/picker_screen.c:1234` | THE BOARD DOES NOT REPORT ITS PAD POSITIONS. | DIE PLATINE MELDET KEINE PAD-POSITIONEN. |
| `PK_NO_ART_3` | `shared/ui/picker_screen.c:1234` | AN ACCURATE DRAWING IS NOT POSSIBLE. | EINE GENAUE ZEICHNUNG IST DAHER NICHT MÖGLICH. |
| `PK_NO_ART_4` | `shared/ui/picker_screen.c:1235` | SEE ITS PINS ON THE OUTPUTS SCREEN. | DIE PINS FINDEN SICH AUF DER SEITE OUTPUTS. |

## Plots

| Text id | Where it is used | English | German |
|---|---|---|---|
| `PLOT_NOW` | `shared/ui/ui_plot.c:374` | NOW | JETZT |
| `PLOT_END` | `shared/ui/ui_plot.c:375` | END | ENDE |

## SERVO

| Text id | Where it is used | English | German |
|---|---|---|---|
| `SV_PG_OUTPUT` | `shared/ui/servo_screen.c:195` | OUTPUT | AUSGANG |
| `SV_PG_TEST` | `shared/ui/servo_screen.c:195` | TEST | TEST |
| `SV_PG_LIMITS` | `shared/ui/servo_screen.c:195` | LIMITS | GRENZEN |
| `SV_PG_DUT` | `shared/ui/servo_screen.c:195` | DUT | PRÜFLING |
| `SV_ROW_TYPE` | `shared/ui/servo_screen.c:213` | TYPE | TYP |
| `SV_ROW_RATE` | `shared/ui/servo_screen.c:214,3899,4015` | FRAME RATE | PULSRATE |
| `SV_ROW_MIN` | `shared/ui/servo_screen.c:215,3924` | PULSE MIN | PULS MIN |
| `SV_ROW_CENTRE` | `shared/ui/servo_screen.c:216,3933` | PULSE CENTRE | PULS MITTE |
| `SV_ROW_MAX` | `shared/ui/servo_screen.c:217,3941` | PULSE MAX | PULS MAX |
| `SV_ROW_TRIM` | `shared/ui/servo_screen.c:218` | TRIM | TRIMMUNG |
| `SV_ROW_TRAVEL` | `shared/ui/servo_screen.c:219,3947` | TRAVEL | WEG |
| `SV_ROW_REVERSE` | `shared/ui/servo_screen.c:220` | REVERSE | UMKEHREN |
| `SV_ROW_CURVE` | `shared/ui/servo_screen.c:222` | CURVE | KURVE |
| `SV_ROW_SPEED` | `shared/ui/servo_screen.c:223` | SPEED | TEMPO |
| `SV_ROW_RANGE` | `shared/ui/servo_screen.c:224` | RANGE | BEREICH |
| `SV_ROW_LEN_BY` | `shared/ui/servo_screen.c:225` | TEST LENGTH BY | TESTDAUER NACH |
| `SV_ROW_LEN_S` | `shared/ui/servo_screen.c:226` | TEST TIME | TESTZEIT |
| `SV_ROW_MOVES` | `shared/ui/servo_screen.c:227` | MOVEMENTS | BEWEGUNGEN |
| `SV_ROW_DWELL` | `shared/ui/servo_screen.c:228` | DWELL TIME | HALTEZEIT |
| `SV_ROW_SETTLE` | `shared/ui/servo_screen.c:229` | SETTLING TIME | EINSCHWINGZEIT |
| `SV_ROW_STEP_48` | `shared/ui/servo_screen.c:230` | STEP 4.8 V | STUFE 4.8 V |
| `SV_ROW_STEP_60` | `shared/ui/servo_screen.c:231` | STEP 6.0 V | STUFE 6.0 V |
| `SV_ROW_HV` | `shared/ui/servo_screen.c:232` | HV SERVO | HV-SERVO |
| `SV_ROW_STEP_74` | `shared/ui/servo_screen.c:233` | STEP 7.4 V | STUFE 7.4 V |
| `SV_ROW_STEP_84` | `shared/ui/servo_screen.c:234` | STEP 8.4 V | STUFE 8.4 V |
| `SV_ROW_BROWNOUT` | `shared/ui/servo_screen.c:235` | BROWN-OUT | BROWN-OUT |
| `SV_ROW_V_MAX` | `shared/ui/servo_screen.c:237` | VOLTAGE MAX | SPANNUNG MAX |
| `SV_ROW_I_MAX` | `shared/ui/servo_screen.c:238` | CURRENT MAX | STROM MAX |
| `SV_ROW_STALL` | `shared/ui/servo_screen.c:239` | STALL AT | BLOCKIERT AB |
| `SV_ROW_IDLE` | `shared/ui/servo_screen.c:240` | IDLE CURRENT | RUHESTROM |
| `SV_ROW_HOLD` | `shared/ui/servo_screen.c:241` | HOLD CURRENT | HALTESTROM |
| `SV_ROW_TRAVEL_TIME` | `shared/ui/servo_screen.c:242` | TRAVEL TIME | STELLZEIT |
| `SV_ROW_NAME` | `shared/ui/servo_screen.c:246` | NAME | NAME |
| `SV_ROW_REPORT` | `shared/ui/servo_screen.c:247` | REPORT | BERICHT |
| `SV_ROW_ENC` | `shared/ui/servo_screen.c:248` | AS5600 | AS5600 |
| `SV_ROW_ENC_CENTRE` | `shared/ui/servo_screen.c:249` | ENC CENTRE | SENSOR-MITTE |
| `SV_ROW_TARGET` | `shared/ui/servo_screen.c:250` | PWM TARGET | PWM-ZIEL |
| `SV_ROW_PORT_PWM` | `shared/ui/servo_screen.c:251` | PORT PWM | PORT-PWM |
| `SV_SERVO_TYPE` | `shared/ui/servo_screen.c:3893` | SERVO TYPE | SERVOTYP |
| `SV_CUSTOM` | `shared/ui/servo_screen.c:3908` | CUSTOM | EIGENE EINSTELLUNG |
| `SV_DUT_TITLE` | `shared/ui/servo_screen.c:3978` | DEVICE UNDER TEST | PRÜFLING |
| `SV_NOT_RECORDED` | `shared/ui/servo_screen.c:5027,5115` | NOT RECORDED | NICHT AUFGEZEICHNET |
| `SV_WRITING` | `shared/ui/servo_screen.c:5028,5116` | WRITING | SCHREIBEN LÄUFT |
| `SV_AUTO_TEST` | `shared/ui/servo_screen.c:5047` | AUTOMATIC TEST | AUTOMATISCHER TEST |
| `SV_STEP_OF` | `shared/ui/servo_screen.c:5049` | %s %u OF %u  %.2f V | %s %u VON %u  %.2f V |
| `SV_PHASE_OF` | `shared/ui/servo_screen.c:5057` | %s %u OF %u | %s %u VON %u |
| `SV_TEST_VERDICT` | `shared/ui/servo_screen.c:5073` | TEST %s | TEST %s |
| `SV_TRAVEL_HOLD` | `shared/ui/servo_screen.c:5081` | TRAVEL %lu ms  HOLD %.2f A | STELLZEIT %lu ms  HALTESTROM %.2f A |
| `SV_STOP_TEST` | `shared/ui/servo_screen.c:5096,5899` | STOP TEST | TEST BEENDEN |
| `SV_START_TEST` | `shared/ui/servo_screen.c:5899` | START TEST | TEST STARTEN |
| `SV_COMMANDED` | `shared/ui/servo_screen.c:5622` | COMMANDED | SOLL-PULSBREITE |
| `SV_MEASURED` | `shared/ui/servo_screen.c:5628,5631,5633` | MEASURED | GEMESSEN |
| `SV_CURRENT` | `shared/ui/servo_screen.c:5635` | CURRENT | STROM |
| `SV_SOURCE_INA` | `shared/ui/servo_screen.c:5408` | INA3221 CH1 | INA3221 CH1 |
| `SV_MODELLED` | `shared/ui/servo_screen.c:5450,5453` | SIMULATED | SIMULIERT |
| `SV_METER_MODEL` | `shared/ui/servo_screen.c:5102` | SIMULATION | SIMULATION |
| `SV_SET` | `shared/ui/servo_screen.c:5509` | SET | SOLL |
| `SV_SPEED` | `shared/ui/servo_screen.c:5650` | SPEED | TEMPO |
| `SV_SPEED_LIMITS` | `shared/ui/servo_screen.c:5648` | SPEED LIMITS THE SWEEP | TEMPO BEGRENZT DEN SWEEP |
| `SV_TARGET` | `shared/ui/servo_screen.c:5578` | TARGET | ZIEL |
| `SV_TARGET_NONE` | `shared/ui/servo_screen.c:3234,5282` | NO SERVO PWM PIN | KEIN SERVO-PWM-PIN |
| `SV_TARGET_UNREAD` | `shared/ui/servo_screen.c:3234,5278` | PINS NOT READ | PINS NICHT AUSGELESEN |
| `SV_TARGET_PORT_PIN` | `shared/ui/servo_screen.c:5272` | PROG PORT GP%u | PROG-PORT GP%u |
| `SV_TARGET_PORT` | `shared/ui/servo_screen.c:5269` | PROG PORT | PROG-PORT |
| `SV_TARGET_PORT_PIN_OFF` | `shared/ui/servo_screen.c:5272` | PROG PORT GP%u PWM OFF | PROG-PORT GP%u PWM AUS |
| `SV_TARGET_PORT_OFF` | `shared/ui/servo_screen.c:5269` | PROG PORT PWM OFF | PROG-PORT PWM AUS |
| `SV_TARGET_UNKNOWN` | `shared/ui/servo_screen.c:5259` | UNKNOWN | UNBEKANNT |
| `SV_TARGET_NO_LINK` | `shared/ui/servo_screen.c:5255` | NO LINK | KEINE VERBINDUNG |
| `SV_CENTRE_BTN` | `shared/ui/servo_screen.c:5659` | CENTRE | ZENTRIEREN |
| `SV_PAUSE` | `shared/ui/servo_screen.c:5668` | PAUSE | PAUSE |
| `SV_PAUSED` | `shared/ui/servo_screen.c:5668` | PAUSED | PAUSIERT |
| `SV_RELEASE` | `shared/ui/servo_screen.c:5672` | RELEASE | FREIGEBEN |
| `SV_IN_FORCE_OUT` | `shared/ui/servo_screen.c:5817` | Active: OUTPUTS run at %u Hz. | Aktiv: AUSGÄNGE laufen mit %u Hz. |
| `SV_IN_FORCE_PORT_PIN` | `shared/ui/servo_screen.c:5822` | Active: PROG PORT GP%u runs at %u Hz. | Aktiv: PROG-PORT GP%u läuft mit %u Hz. |
| `SV_IN_FORCE_PORT` | `shared/ui/servo_screen.c:5819` | Active: %u Hz on PROG PORT. No pin is assigned to it. | Aktiv: %u Hz am PROG-PORT. Ihm ist kein Pin zugeordnet. |
| `SV_IN_FORCE_UNKNOWN` | `shared/ui/servo_screen.c:5807` | Active rate: %u Hz. Target unknown. | Aktive Pulsrate: %u Hz. Ziel unbekannt. |
| `SV_IN_FORCE_NO_LINK` | `shared/ui/servo_screen.c:5800` | No link. The active frame rate is unknown. | Keine Verbindung. Die aktive Pulsrate ist unbekannt. |
| `SV_IN_FORCE_NOT_SET` | `shared/ui/servo_screen.c:5826` | The PWM TARGET setting is not active. | Die Einstellung PWM-ZIEL ist nicht aktiv. |
| `SV_IN_FORCE_PWM_OFF` | `shared/ui/servo_screen.c:5828` | PORT PWM is OFF. The port sends no pulses. | PORT-PWM ist AUS. Der Port gibt keine Pulse aus. |
| `SV_IN_FORCE_SURFACES_1` | `shared/ui/servo_screen.c:5860` | Active: all PWM control surfaces run at %u Hz. | Aktiv: Alle PWM-Ruder laufen mit %u Hz. |
| `SV_IN_FORCE_2` | `shared/ui/servo_screen.c:5801,5808,5831,5862` | PPM outputs keep their own frame rate. | PPM-Ausgänge behalten ihre eigene Frame-Rate. |
| `SV_REFUSED_1` | `shared/ui/servo_screen.c:5866` | Rejected: a pulse is longer than the frame, or a pin | Abgelehnt: Puls länger als die Periode, oder ein Pin |
| `SV_REFUSED_2` | `shared/ui/servo_screen.c:5867` | shares a PWM slice at a different rate. Settings unchanged. | teilt einen PWM-Slice mit anderer Rate. Einstellungen unverändert. |
| `SV_UNSUPPORTED_1` | `shared/ui/servo_screen.c:5870` | This coprocessor cannot set the frame rate. | Dieser Koprozessor unterstützt keine einstellbare Pulsrate. |
| `SV_UNSUPPORTED_2` | `shared/ui/servo_screen.c:5871` | Each PWM output keeps its assigned rate of 50 Hz. | Jeder PWM-Ausgang behält seine zugeordnete Rate von 50 Hz. |
| `SV_UNSENT_1` | `shared/ui/servo_screen.c:5875` | The frame rate will be sent to the coprocessor | Die Pulsrate wird mit der nächsten Position |
| `SV_UNSENT_2` | `shared/ui/servo_screen.c:5876` | with the next position command. | an den Koprozessor gesendet. |
| `SV_RUNNING` | `shared/ui/servo_screen.c:5938` | RUNNING: STEP %u OF %u | LÄUFT: STUFE %u VON %u |
| `SV_LAST` | `shared/ui/servo_screen.c:5944,6009` | LAST: %s %s | ZULETZT %s%.0s |
| `SV_TAP_HV` | `shared/ui/servo_screen.c:5952` | TAP: HV WARNING | TIPPEN: HV-WARNUNG |
| `SV_HOLD_START` | `shared/ui/servo_screen.c:5952,6012` | HOLD 2 S TO START | 2 S HALTEN ZUM STARTEN |
| `SV_FASTEST` | `shared/ui/servo_screen.c:6081` | Highest rate with these pulses: %u Hz (%s gap). | Höchste Rate mit diesen Pulsen: %u Hz (%s Pause). |
| `SV_AT_START` | `shared/ui/servo_screen.c:6084` | At startup, type and rate reset to STANDARD PWM 50 Hz. | Beim Start gelten wieder STANDARD PWM und 50 Hz. |
| `SV_LIMITS_1` | `shared/ui/servo_screen.c:6093` | VOLTAGE MAX and CURRENT MAX are the limits | SPANNUNG MAX und STROM MAX sind die Grenzen |
| `SV_LIMITS_2` | `shared/ui/servo_screen.c:6093` | set on SUPPLY. Pass/fail limits set to 0 | auf SUPPLY. Prüfgrenzen mit dem Wert 0 |
| `SV_LIMITS_3` | `shared/ui/servo_screen.c:6093` | are not checked. Above STALL AT, the servo | werden nicht geprüft. Über BLOCKIERT AB |
| `SV_LIMITS_4` | `shared/ui/servo_screen.c:6094` | is considered stalled. | gilt das Servo als blockiert. |
| `SV_ROW_LIM_MODE` | `shared/ui/servo_screen.c:243` | LIMIT RUN | ENDLAGENLAUF |
| `SV_LIM_SEARCH` | `shared/ui/servo_screen.c:5752` | SEARCH ONLY | NUR SUCHE |
| `SV_LIM_PROGRAM` | `shared/ui/servo_screen.c:5752` | SEARCH + PROGRAM | SUCHEN + PROGRAMMIEREN |
| `SV_ROW_LIM_PULSES` | `shared/ui/servo_screen.c:244` | SERVO PULSES | SERVOPULSE |
| `SV_LIM_PULSES_KEEP` | `shared/ui/servo_screen.c:5756` | KEEP | BEHALTEN |
| `SV_LIM_PULSES_STD` | `shared/ui/servo_screen.c:5756` | 1000-2000 | 1000-2000 |
| `SV_LIM_START` | `shared/ui/servo_screen.c:5979` | START LIMIT RUN | ENDLAGENLAUF STARTEN |
| `SV_LIM_STOP` | `shared/ui/servo_screen.c:5232,5979` | STOP RUN | LAUF BEENDEN |
| `SV_LIM_LINE` | `shared/ui/servo_screen.c:5993` | OUTPUT %s, CURRENT INA3221 CH1 | AUSGANG %s, STROM INA3221 CH1 |
| `SV_LIM_RUNNING` | `shared/ui/servo_screen.c:6004` | RUNNING: %s | LÄUFT: %s |
| `SV_LIM_INA_SETUP` | `shared/ui/servo_screen.c:3179` | INA3221 NOT SET UP TO MEASURE SERVO SUPPLY CURRENT | INA3221 NICHT ZUR SERVOSTROMMESSUNG EINGERICHTET |
| `SV_LIM_INA_OFFLINE` | `shared/ui/servo_screen.c:3184` | INA3221 OFFLINE | INA3221 NICHT ERREICHBAR |
| `SV_LIM_INA_NO_WIN` | `shared/ui/servo_screen.c:3187` | NO INA3221 MEASUREMENT WINDOWS YET | NOCH KEINE INA3221-MESSFENSTER |
| `SV_LIM_PWM_OFF` | `shared/ui/servo_screen.c:3229` | PORT PWM OFF. NO PULSES ON THIS PORT. | PORT-PWM AUS. KEINE PULSE AN DIESEM PORT. |
| `SV_LIM_RAIL_LOW` | `shared/ui/servo_screen.c:3226` | SERVO SUPPLY BELOW 3.0 V OR NOT READ | SERVOVERSORGUNG UNTER 3.0 V ODER NICHT AUSGELESEN |
| `SV_LIM_REFUSED` | `shared/ui/servo_screen.c:3237,3519` | CENTRE MUST BE BETWEEN PULSE MIN AND MAX | MITTE MUSS ZWISCHEN PULS MIN UND PULS MAX LIEGEN |
| `SV_LIM_SEARCH_NOTE` | `shared/ui/servo_screen.c:6029` | SEARCH ONLY: NO VALUES ARE WRITTEN TO THE SERVO | NUR SUCHE: KEINE WERTE WERDEN INS SERVO GESCHRIEBEN |
| `SV_LIM_PULSES_NOTE` | `shared/ui/servo_screen.c:6028` | SEARCH ONLY: SETS PULSE LOWER/UPPER ONLY | NUR SUCHE: SETZT NUR PULSE LOWER/UPPER |
| `SV_LIM_PROG_NOTE` | `shared/ui/servo_screen.c:6027` | REDUCES LEFT/RIGHT ANGLE ON A KST OR CHASERVO SERVO | VERRINGERT LEFT/RIGHT ANGLE BEI KST- ODER CHASERVO-SERVOS |
| `SV_LIM_PROG_PORT` | `shared/ui/servo_screen.c:3254` | PROGRAMMING REQUIRES A SERVO ON THE PROG PORT | PROGRAMMIEREN ERFORDERT EIN SERVO AM PROG-PORT |
| `SV_LIM_PROG_BUSY` | `shared/ui/servo_screen.c:3258` | PROGRAMMING: PORT IS NOT IN PWM MODE | PROGRAMMIEREN: PORT IST NICHT IM PWM-MODUS |
| `SV_LIM_PROG_LOW` | `shared/ui/servo_screen.c:3269` | PROGRAMMING: SUPPLY SET BELOW 4.8 V | PROGRAMMIEREN: VERSORGUNG AUF UNTER 4.8 V EINGESTELLT |
| `SV_LIM_PROG_PD_OFF` | `shared/ui/servo_screen.c:3264` | PROGRAMMING: PD MINI OUTPUT OFF | PROGRAMMIEREN: PD-MINI-AUSGANG AUS |
| `SV_LIM_PROG_RAIL` | `shared/ui/servo_screen.c:3277` | PROGRAMMING: SERVO RAIL BELOW 4.8 V | PROGRAMMIEREN: SERVOVERSORGUNG UNTER 4.8 V |
| `SV_LIM_TITLE` | `shared/ui/servo_screen.c:5156` | LIMIT SEARCH | ENDLAGENSUCHE |
| `SV_LIM_TITLE_PROG` | `shared/ui/servo_screen.c:5153` | LIMIT RUN, %u OF %u VALUES | ENDLAGENLAUF, %u VON %u WERTEN |
| `SV_LIM_AT` | `shared/ui/servo_screen.c:5162` | %s %s END %u us | %s %s ENDE %u us |
| `SV_LIM_LOW` | `shared/ui/servo_screen.c:5163` | LOW | UNTERES |
| `SV_LIM_HIGH` | `shared/ui/servo_screen.c:5164` | HIGH | OBERES |
| `SV_LIM_VALUES` | `shared/ui/servo_screen.c:5167` | %s LEFT %u RIGHT %u | %s LINKS %u RECHTS %u |
| `SV_LIM_VERDICT` | `shared/ui/servo_screen.c:5183` | LIMITS %s | ENDLAGEN %s |
| `SV_LIM_ENDS` | `shared/ui/servo_screen.c:5198,5223` | LOW %s  HIGH %s | UNTEN %s  OBEN %s |
| `SV_LIM_SET` | `shared/ui/servo_screen.c:5191` | LEFT %u>%u  RIGHT %u>%u | LINKS %u>%u  RECHTS %u>%u |
| `SV_LIM_RESTORE` | `shared/ui/servo_screen.c:5203` | RESTORE IN %s | ZURÜCKSPIELEN IN %s |
| `SV_LIM_RANGE` | `shared/ui/servo_screen.c:5207` | %u-%u NOT IN %u-%u us | %u-%u NICHT IN %u-%u us |
| `SV_DUT_NOTE` | `shared/ui/servo_screen.c:6101` | This name appears at the top of every test report. | Dieser Name steht oben auf jedem Testbericht. |
| `SV_ENC_NOTE_1` | `shared/ui/servo_screen.c:6101` | AS5600: angle sensor on the servo horn shaft. | AS5600: Winkelsensor an der Servoabtriebswelle. |
| `SV_ENC_NOTE_2` | `shared/ui/servo_screen.c:6101` | Centre the servo, then tap ENC CENTRE. | Stelle das Servo auf Mitte und tippe auf SENSOR-MITTE. |
| `SV_DESTROY` | `shared/ui/servo_screen.c:6156` | CAN DESTROY THE SERVO | KANN DAS SERVO ZERSTÖREN |
| `SV_DESTROY_WHAT` | `shared/ui/servo_screen.c:6160` | %s at %u Hz, %u-%u us | %s bei %u Hz, %u-%u us |
| `SV_DESTROY_1` | `shared/ui/servo_screen.c:6167` | Use only with a servo rated for these settings. Check its datasheet. | Nur mit einem für diese Werte geeigneten Servo nutzen. Datenblatt prüfen. |
| `SV_DESTROY_2` | `shared/ui/servo_screen.c:6167` | An unsuitable servo will overheat or jam | Ein ungeeignetes Servo überhitzt oder blockiert |
| `SV_DESTROY_3` | `shared/ui/servo_screen.c:6167` | within seconds and be destroyed. | innerhalb von Sekunden und wird zerstört. |
| `SV_DESTROY_4` | `shared/ui/servo_screen.c:6168` | Do not exceed 60 Hz with an analogue servo. | Bei einem analogen Servo höchstens 60 Hz verwenden. |
| `SV_DESTROY_5` | `shared/ui/servo_screen.c:6168` | Every restart returns to STANDARD PWM 50 Hz. | Nach jedem Neustart gilt wieder STANDARD PWM 50 Hz. |
| `SV_HV_ONLY` | `shared/ui/servo_screen.c:6200` | HV SERVOS ONLY | NUR HV-SERVOS |
| `SV_HV_ON_AT` | `shared/ui/servo_screen.c:6203` | OUTPUT ON AT %.2f V | AUSGANG EIN BEI %.2f V |
| `SV_HV_STEPS` | `shared/ui/servo_screen.c:6206` | TEST STEPS UP TO %.2f V | TESTSTUFEN BIS %.2f V |
| `SV_HV_VOLTAGE` | `shared/ui/servo_screen.c:6209` | VOLTAGE %.2f -> %.2f V | SPANNUNG %.2f -> %.2f V |
| `SV_HV_LIVE` | `shared/ui/servo_screen.c:6224` | The output is on. The voltage will change immediately. | Der Ausgang ist eingeschaltet. Die Spannung ändert sich sofort. |
| `KP_PROGRAM` | `shared/ui/servo_screen.c:5203,5616` | PROGRAM | PROGRAMMIEREN |

## SERVO PROGRAMMER (KST, Chaservo)

| Text id | Where it is used | English | German |
|---|---|---|---|
| `ON` | `shared/ui/kst_panel.c:529,2965` | ON | EIN |
| `KP_TAB_SERVO` | `shared/ui/kst_panel.c:101` | SERVO | SERVO |
| `KP_TAB_TUNE` | `shared/ui/kst_panel.c:101` | TUNE | ABSTIMMEN |
| `KP_TAB_PULSE` | `shared/ui/kst_panel.c:101` | PULSE | PULS |
| `KP_TAB_FAILSAFE` | `shared/ui/kst_panel.c:101` | FAILSAFE | FAILSAFE |
| `KP_TAB_LOCKED` | `shared/ui/kst_panel.c:102` | LOCKED | GESPERRT |
| `KP_RESTORE` | `shared/ui/kst_panel.c:63` | RESTORE | ZURÜCKSPIELEN |
| `KP_MORE` | `shared/ui/kst_panel.c:64,1727` | MORE | WEITERES |
| `KP_ENABLE` | `shared/ui/kst_panel.c:65` | ENABLE WRITE TO SERVO | SCHREIBEN INS SERVO FREIGEBEN |
| `KP_CHANGES` | `shared/ui/kst_panel.c:66` | CHANGES | ÄNDERUNGEN |
| `KP_SPEED_LIMIT` | `shared/ui/kst_panel.c:73` | SPEED LIMIT | TEMPOGRENZE |
| `KP_SPEED` | `shared/ui/kst_panel.c:74` | SPEED | TEMPO |
| `KP_FS_MOVE` | `shared/ui/kst_panel.c:83` | MOVE ON SIGNAL LOSS | BEWEGEN BEI SIGNALVERLUST |
| `KP_FS_AFTER` | `shared/ui/kst_panel.c:85` | AFTER | NACH |
| `KP_CONNECT` | `shared/ui/kst_panel.c:2951` | CONNECT | VERBINDEN |
| `KP_LEAVE` | `shared/ui/kst_panel.c:2951` | LEAVE | VERLASSEN |
| `KP_ABORT` | `shared/ui/kst_panel.c:2949` | ABORT | ABBRECHEN |
| `KP_READ_ALL` | `shared/ui/kst_panel.c:2954` | READ ALL | ALLES AUSLESEN |
| `KP_CHOOSE` | `shared/ui/kst_panel.c:2958,2962` | CHOOSE | WÄHLEN |
| `KP_WRITE_N` | `shared/ui/kst_panel.c:815` | WRITE %u VALUES | %u WERTE SCHREIBEN |
| `KP_WRITE_ONE` | `shared/ui/kst_panel.c:813` | WRITE 1 VALUE | 1 WERT SCHREIBEN |
| `KP_FROM_SESSION` | `shared/ui/kst_panel.c:1259` | Backup of the first settings read in this session. | Backup der zuerst in dieser Sitzung ausgelesenen Werte. |
| `KP_FROM_JOURNAL` | `shared/ui/kst_panel.c:1261` | Settings before the interrupted write. | Werte vor dem unterbrochenen Schreibvorgang. |
| `KP_FROM_RUN` | `shared/ui/kst_panel.c:1260` | Settings before the limit run. | Werte vor dem Endlagenlauf. |
| `KP_FORGET` | `shared/ui/kst_panel.c:1723` | Discard the record of the interrupted write. | Vermerk zum unterbrochenen Schreibvorgang verwerfen. |
| `KP_VERIFY` | `shared/ui/kst_panel.c:1742` | VERIFY: compare the servo with the values already read. | PRÜFEN: Servo mit den ausgelesenen Werten vergleichen. |
| `KP_DISCARD` | `shared/ui/kst_panel.c:1743` | DISCARD EDITS: return to the servo's settings. | ÄNDERUNGEN VERWERFEN: Zurück zu den Servowerten. |
| `KP_SUPPLY_V` | `shared/ui/kst_panel.c:1334,1732` | PROGRAMMING VOLTAGE: %u.%u V | PROGRAMMIERSPANNUNG: %u.%u V |
| `KP_SUPPLY_A` | `shared/ui/kst_panel.c:1735` | CURRENT LIMIT: %u mA | STROMBEGRENZUNG: %u mA |
| `KP_PWM_WAIT` | `shared/ui/kst_panel.c:1738` | WAIT BEFORE PWM: %u ms | WARTEZEIT VOR PWM: %u ms |
| `KP_SOFT_TITLE` | `shared/ui/kst_panel.c:1139` | CHECK THIS CHANGE | ÄNDERUNG PRÜFEN |
| `KP_CHECK_TITLE` | `shared/ui/kst_panel.c:1295` | CHECK THIS BACKUP | BACKUP PRÜFEN |
| `KP_ENABLE_TITLE` | `shared/ui/kst_panel.c:1147` | ENABLE WRITE TO SERVO | SCHREIBEN FREIGEBEN |
| `KP_RESTORE_TITLE` | `shared/ui/kst_panel.c:1266,1715` | RESTORE | ZURÜCKSPIELEN |
| `KP_RELEASE_TITLE` | `shared/ui/kst_panel.c:1304,1744` | RELEASE PAIRING | PAIRING LÖSEN |
| `KP_PROG_VOLTAGE` | `shared/ui/kst_panel.c:1332,1814` | PROGRAMMING VOLTAGE | PROGRAMMIERSPANNUNG |
| `KP_PROG_CURRENT` | `shared/ui/kst_panel.c:1820` | CURRENT LIMIT | STROMBEGRENZUNG |
| `KP_PWM_WAIT_TITLE` | `shared/ui/kst_panel.c:1825` | WAIT BEFORE PWM | WARTEZEIT VOR PWM |
| `KP_LEAVE_TITLE` | `shared/ui/kst_panel.c:1317` | LEAVE PROGRAMMING | PROGRAMMIERMODUS VERLASSEN |
| `KP_HOLD_ENABLE` | `shared/ui/kst_panel.c:1147` | HOLD TO ENABLE | HALTEN ZUM FREIGEBEN |
| `KP_HOLD_WRITE` | `shared/ui/kst_panel.c:1247,1304` | HOLD TO WRITE | HALTEN ZUM SCHREIBEN |
| `KP_HOLD_RESTORE` | `shared/ui/kst_panel.c:1266` | HOLD TO RESTORE | HALTEN ZUM ZURÜCKSPIELEN |
| `KP_HOLD_NEXT` | `shared/ui/kst_panel.c:1295` | HOLD TO CONTINUE | HALTEN ZUM FORTFAHREN |
| `KP_HOLD_LEAVE` | `shared/ui/kst_panel.c:1317` | HOLD TO LEAVE | HALTEN ZUM VERLASSEN |
| `KP_HORN_FREE` | `shared/ui/kst_panel.c:1051` | Keep the servo horn clear. Disconnect the linkage. | Halte das Ruderhorn frei. Löse das Gestänge. |
| `KP_BENCH_FIRST` | `shared/ui/kst_panel.c:1052` | Test the servo on the bench before fitting it to a model. | Teste das Servo am Prüfstand, bevor du es ins Modell einbaust. |
| `KP_ENABLE_1` | `shared/ui/kst_panel.c:1148` | These limits are based on 1 servo of unknown model. | Diese Grenzen beruhen auf 1 Servo unbekannten Typs. |
| `KP_ENABLE_2` | `shared/ui/kst_panel.c:1149` | Wrong settings can overheat or stall the servo. | Falsche Werte können das Servo überhitzen oder blockieren. |
| `KP_ENABLE_3` | `shared/ui/kst_panel.c:1150` | An interrupted write can make the servo unusable. | Ein unterbrochener Schreibvorgang kann das Servo unbrauchbar machen. |
| `KP_ENABLE_4` | `shared/ui/kst_panel.c:1151` | The first write backs up the servo's settings to the card. | Beim ersten Schreiben werden die Servowerte auf der Karte gesichert. |
| `KP_ENABLE_5` | `shared/ui/kst_panel.c:1152` | Writing is disabled for each new servo and whenever this screen opens. | Schreiben wird bei jedem neuen Servo und jedem Aufruf dieser Seite gesperrt. |
| `KP_WRITE_1` | `shared/ui/kst_panel.c:1251` | Only changed values are written. Each value is checked afterwards. | Nur geänderte Werte werden geschrieben und danach einzeln geprüft. |
| `KP_WRITE_2` | `shared/ui/kst_panel.c:1253` | Do not disconnect the servo or the bench while writing. | Trenne beim Schreiben weder das Servo noch den Prüfstand. |
| `KP_WRITE_CARD` | `shared/ui/kst_panel.c:1183` | The current settings are backed up to the card before writing. | Vor dem Schreiben werden die bisherigen Werte auf der Karte gesichert. |
| `KP_WRITE_NO_CARD` | `shared/ui/kst_panel.c:1184` | No card. The previous settings are kept in RAM only. | Keine Karte. Die bisherigen Werte bleiben nur im Arbeitsspeicher. |
| `KP_RESTORE_ALL` | `shared/ui/kst_panel.c:1270` | All registers that differ are restored. | Alle abweichenden Register werden zurückgespielt. |
| `KP_RESTORE_N` | `shared/ui/kst_panel.c:1274` | %u values will be restored. | %u Werte werden zurückgespielt. |
| `KP_RESTORE_ONE` | `shared/ui/kst_panel.c:1272` | 1 value will be restored. | 1 Wert wird zurückgespielt. |
| `KP_RESTORE_UNCHECKED` | `shared/ui/kst_panel.c:1281` | This backup will be restored without checking the limits. | Dieses Backup wird ohne Prüfung der Grenzwerte zurückgespielt. |
| `KP_CHECK_1` | `shared/ui/kst_panel.c:1298` | These warnings apply to the backup values: | Für die Backup-Werte gelten diese Warnungen: |
| `KP_VALUES_MORE` | `shared/ui/kst_panel.c:1093` | and %u more | und %u weitere |
| `KP_WRITING` | `shared/ui/kst_panel.c:2686` | Writing. Do not disconnect. | Schreibvorgang läuft. Nicht trennen. |
| `KP_PORT_SUPPLY` | `shared/ui/kst_panel.c:461` | PORT  %u.%u V  %u mA | PORT  %u.%u V  %u mA |
| `KP_ANGLE_NOTE` | `shared/ui/kst_panel.c:2928` | LEFT ANGLE, RIGHT ANGLE: raw servo values. The unit is unknown. | LEFT ANGLE, RIGHT ANGLE: Werte aus dem Servo. Die Einheit ist unbekannt. |
| `KP_CHECK_MORE` | `shared/ui/kst_panel.c:1127` | And %u more warnings. | Und %u weitere Warnungen. |
| `KP_CHECK_MORE_ONE` | `shared/ui/kst_panel.c:1124` | And 1 more warning. | Und 1 weitere Warnung. |
| `KP_STAY` | `shared/ui/kst_panel.c:753` | %u locked values differ. They will not be changed. | %u gesperrte Werte weichen ab. Sie bleiben unverändert. |
| `KP_STAY_ONE` | `shared/ui/kst_panel.c:751` | 1 locked value differs. It will not be changed. | 1 gesperrter Wert weicht ab. Er bleibt unverändert. |
| `KP_RELEASE_1` | `shared/ui/kst_panel.c:1305` | Sets the servo's pairing value in register 1D to 00. | Setzt den Pairing-Wert des Servos in Register 1D auf 00. |
| `KP_RELEASE_2` | `shared/ui/kst_panel.c:1306` | Its effect inside the servo is unknown. | Die Wirkung im Servo ist unbekannt. |
| `KP_RELEASE_3` | `shared/ui/kst_panel.c:1307` | RESTORE with this session's backup undoes this change. | ZURÜCKSPIELEN mit dem Sitzungsbackup macht die Änderung rückgängig. |
| `KP_LEAVE_DOUBT_1` | `shared/ui/kst_panel.c:1319` | The servo's state is uncertain. Leaving ends this session | Der Servozustand ist unklar. Verlassen beendet diese Sitzung |
| `KP_LEAVE_DOUBT_2` | `shared/ui/kst_panel.c:1320` | and its restore option. Before leaving, tap CANCEL, then RESTORE. | und die Möglichkeit zum Zurückspielen. Erst ABBRECHEN, dann ZURÜCKSPIELEN. |
| `KP_LEAVE_CHANGES` | `shared/ui/kst_panel.c:1323` | Unwritten changes will be lost. | Noch nicht geschriebene Änderungen gehen verloren. |
| `KP_LEAVE_1` | `shared/ui/kst_panel.c:1325` | The port returns to servo PWM. The supply stays unchanged. | Der Port wechselt zurück zu Servo-PWM. Die Versorgung bleibt unverändert. |
| `KP_LEAVE_2` | `shared/ui/kst_panel.c:1326` | The servo will receive the tester's signal and may move. | Das Servo erhält wieder das Testsignal und kann sich bewegen. |
| `KP_NO_PAGE_2` | `shared/ui/kst_panel.c:2837` | Update the coprocessor to version 4.13 or later. | Koprozessor auf Version 4.13 oder neuer aktualisieren. |
| `KP_STATE_SYNC` | `shared/ui/kst_panel.c:2779` | Reading the port state from the coprocessor. | Portzustand wird vom Koprozessor ausgelesen. |
| `KP_STATE_PWM` | `shared/ui/kst_panel.c:2785` | Tester is using port %u. CONNECT sets %u.%u V and %u mA. | Tester nutzt Port %u. VERBINDEN stellt %u.%u V und %u mA ein. |
| `KP_STATE_PWM_AS_IS` | `shared/ui/kst_panel.c:2790` | Tester is using port %u. No PD mini: supply stays unchanged. | Tester nutzt Port %u. Kein PD mini: Versorgung bleibt unverändert. |
| `KP_STATE_ENTERING` | `shared/ui/kst_panel.c:2795` | Connecting on port %u. PWM is off. | Verbindung an Port %u wird aufgebaut. PWM ist aus. |
| `KP_STATE_PROGRAMMING` | `shared/ui/kst_panel.c:2804` | Port %u is in programming mode. PWM is off. | Port %u ist im Programmiermodus. PWM ist aus. |
| `KP_STATE_PROG_V` | `shared/ui/kst_panel.c:2799` | Port %u in programming mode. PWM off. %u.%02u V, %u mA. | Port %u im Programmiermodus. PWM aus. %u.%02u V, %u mA. |
| `KP_STATE_WORKING` | `shared/ui/kst_panel.c:2821` | Operation in progress: %u frames. | Vorgang läuft: %u Frames. |
| `KP_STATE_WORKING_ONE` | `shared/ui/kst_panel.c:2819` | Operation in progress: 1 frame. | Vorgang läuft: 1 Frame. |
| `KP_STATE_WRITING_N` | `shared/ui/kst_panel.c:2814` | Writing %u registers. Frames sent: %u. | %u Register werden geschrieben. Gesendete Frames: %u. |
| `KP_STATE_WRITING_ONE` | `shared/ui/kst_panel.c:2811` | Writing 1 register. Frames sent: %u. | 1 Register wird geschrieben. Gesendete Frames: %u. |
| `KP_STATE_WRITING` | `shared/ui/kst_panel.c:2817` | Writing. Frames sent: %u. | Schreibvorgang läuft. Gesendete Frames: %u. |
| `KP_STATE_PLANNING` | `shared/ui/kst_panel.c:2809` | Preparing the write order. No frames sent yet. | Schreibreihenfolge wird vorbereitet. Noch kein Frame gesendet. |
| `KP_STATE_LEAVING` | `shared/ui/kst_panel.c:2825` | Leaving programming mode. PWM is still off. | Programmiermodus wird verlassen. PWM ist noch aus. |
| `KP_NEED_TARGET` | `shared/ui/kst_panel.c:159,2783` | Use the programming port to program a servo. | Zum Programmieren muss das Servo am Programmierport angeschlossen sein. |
| `KP_NEED_TARGET_2` | `shared/ui/kst_panel.c:159,2840` | On SERVO > DUT, set PWM TARGET to PROG PORT. | Unter SERVO > PRÜFLING das PWM-ZIEL auf PROG-PORT stellen. |
| `KP_DOUBT` | `shared/ui/kst_panel.c:2719` | The settings currently in the servo are unknown. | Die aktuellen Werte im Servo sind unbekannt. |
| `KP_JOURNAL_KEPT` | `shared/ui/kst_panel.c:2730` | Settings from before the interrupted write are available under RESTORE. | Werte von vor dem unterbrochenen Schreiben liegen unter ZURÜCKSPIELEN bereit. |
| `KP_EDIT_NOTE_1` | `shared/ui/kst_panel.c:2902,2929,2933` | * marks a value that differs from the servo's setting. | * markiert einen Wert, der vom Wert im Servo abweicht. |
| `KP_EDIT_NOTE_2` | `shared/ui/kst_panel.c:965,2903,2930,2934` | Write these values under CHANGES on the SERVO tab. | Schreibe diese Werte über ÄNDERUNGEN im Reiter SERVO. |
| `KP_FS_NOTE_1` | `shared/ui/kst_panel.c:2900` | ON: the servo moves to POSITION when the signal | EIN: Das Servo fährt auf POSITION, wenn das Signal |
| `KP_FS_NOTE_2` | `shared/ui/kst_panel.c:2901` | has been missing for the time set in AFTER. | für die unter NACH eingestellte Zeit fehlt. |
| `KP_LOCKED_NOTE` | `shared/ui/kst_panel.c:2906` | Display only. Function or data format is not confirmed. | Nur Anzeige. Funktion oder Datenformat sind nicht bestätigt. |
| `KP_PAIRING` | `shared/ui/kst_panel.c:2910` | Pairing value, register 1D: %02X. | Pairing-Wert, Register 1D: %02X. |
| `KP_PAIRING_CARD` | `shared/ui/kst_panel.c:2913` | The vendor's tool can read this servo only with card %02X. | Das Herstellerprogramm kann dieses Servo nur mit Karte %02X auslesen. |
| `KP_M_READ` | `shared/ui/kst_panel.c:110` | All 32 registers read. | Alle 32 Register ausgelesen. |
| `KP_M_READ_ONLY` | `shared/ui/kst_panel.c:111,233,2724` | Read only. This servo type is unknown. | Nur Auslesen möglich. Dieser Servotyp ist unbekannt. |
| `KP_M_READ_ONLY_2` | `shared/ui/kst_panel.c:111,859,2725` | Its registers do not match the expected pattern. Writing is disabled. | Die Register passen nicht zum erwarteten Muster. Schreiben ist gesperrt. |
| `KP_M_WRITTEN` | `shared/ui/kst_panel.c:112,894,898` | %u values written and verified. | %u Werte geschrieben und geprüft. |
| `KP_M_WRITTEN_ONE` | `shared/ui/kst_panel.c:896` | 1 value written and verified. | 1 Wert geschrieben und geprüft. |
| `KP_M_VERIFIED` | `shared/ui/kst_panel.c:113` | Check passed. The servo matches the values already read. | Prüfung erfolgreich. Das Servo enthält die ausgelesenen Werte. |
| `KP_M_VERIFY_DIFF` | `shared/ui/kst_panel.c:114,901,906` | %u registers differ from the values read earlier. Tap READ ALL. | %u Register weichen von den ausgelesenen Werten ab. ALLES AUSLESEN tippen. |
| `KP_M_VERIFY_DIFF_ONE` | `shared/ui/kst_panel.c:904` | 1 register differs from the value read earlier. Tap READ ALL. | 1 Register weicht vom ausgelesenen Wert ab. ALLES AUSLESEN tippen. |
| `KP_M_RESTORED` | `shared/ui/kst_panel.c:115` | Backup restored and verified. | Backup zurückgespielt und geprüft. |
| `KP_M_RELEASED` | `shared/ui/kst_panel.c:116` | Pairing value set to 00. | Pairing-Wert auf 00 gesetzt. |
| `KP_M_LEFT` | `shared/ui/kst_panel.c:117,162,163` | The port is back in tester mode. | Der Port ist wieder im Testermodus. |
| `KP_M_LEAVE_SUPPLY` | `shared/ui/kst_panel.c:160` | Leaving with a PD mini requires coprocessor firmware 4.15 or later. | Verlassen mit PD mini erfordert Koprozessor-Firmware ab 4.15. |
| `KP_M_LEAVE_SUPPLY_2` | `shared/ui/kst_panel.c:161,871` | STOP ends the session and switches the supply off. | STOP beendet die Sitzung und schaltet die Versorgung aus. |
| `KP_M_LEFT_NO_WAIT` | `shared/ui/kst_panel.c:162` | Coprocessor firmware older than 4.15. PWM starts without a delay. | Koprozessor-Firmware älter als 4.15. PWM startet ohne Wartezeit. |
| `KP_M_LEFT_DOUBT` | `shared/ui/kst_panel.c:163` | The session state was uncertain. PWM starts without a delay. | Sitzungszustand war unklar. PWM startet ohne Wartezeit. |
| `KP_M_LEAVE_CUT` | `shared/ui/kst_panel.c:164` | Leaving was interrupted before the waiting time ended. | Verlassen wurde vor dem Ende der Wartezeit unterbrochen. |
| `KP_M_NEED_DISARM` | `shared/ui/kst_panel.c:165` | Tap DISARM first. The port cannot be taken over while ARMED. | Zuerst DISARM tippen. Bei ARMED kann der Port nicht übernommen werden. |
| `KP_M_SUPPLY_CAPS` | `shared/ui/kst_panel.c:172` | Programming voltage or current exceeds the SUPPLY limits. | Programmierspannung oder -strom überschreitet die SUPPLY-Grenzen. |
| `KP_M_SUPPLY_CAPS_2` | `shared/ui/kst_panel.c:173` | Lower the value under MORE, or raise the limit. Nothing was applied. | Wert unter WEITERES senken oder Grenze erhöhen. Nichts übernommen. |
| `KP_M_BACKUP_OTHER` | `shared/ui/kst_panel.c:166` | This is a different servo. The session backup does not belong to it. | Das ist ein anderes Servo. Das Backup dieser Sitzung gehört nicht dazu. |
| `KP_M_BACKUP_OTHER_2` | `shared/ui/kst_panel.c:167` | Tap LEAVE, then CONNECT to start a new session with its own backup. | VERLASSEN und dann VERBINDEN startet eine neue Sitzung mit eigenem Backup. |
| `KP_M_STORE_FAILED` | `shared/ui/kst_panel.c:120` | Could not save the file to the card. Nothing was written to the servo. | Datei nicht auf Karte gespeichert. Keine Werte ins Servo geschrieben. |
| `KP_M_FILE_BAD` | `shared/ui/kst_panel.c:121` | Cannot read KST.TXT on the card. The file will not be used. | KST.TXT auf der Karte ist nicht lesbar. Die Datei wird nicht verwendet. |
| `KP_M_JOURNAL` | `shared/ui/kst_panel.c:122,2707` | Writing was interrupted. The settings may be incomplete. | Schreiben wurde unterbrochen. Die Einstellungen sind vielleicht unvollständig. |
| `KP_M_JOURNAL_2` | `shared/ui/kst_panel.c:122,781,953,2708` | Do not use the servo. Connect it to port %u and tap READ ALL. | Benutze das Servo nicht. An Port %u anschließen und ALLES AUSLESEN tippen. |
| `KP_M_JOURNAL_2_PIN` | `shared/ui/kst_panel.c:779` | Do not use the servo. Connect it to GP%u and tap READ ALL. | Benutze das Servo nicht. An GP%u anschließen und ALLES AUSLESEN tippen. |
| `KP_M_JOURNAL_OTHER` | `shared/ui/kst_panel.c:139,2704` | The interrupted write affected a different servo. | Der unterbrochene Schreibvorgang betraf ein anderes Servo. |
| `KP_M_JOURNAL_OTHER_2` | `shared/ui/kst_panel.c:140,771,776,953,2705` | It was connected to port %u. Discard the record under RESTORE. | Es war an Port %u angeschlossen. Vermerk unter ZURÜCKSPIELEN verwerfen. |
| `KP_M_JOURNAL_OTHER_2_PIN` | `shared/ui/kst_panel.c:773` | It was connected to GP%u. Discard the record under RESTORE. | Es war an GP%u angeschlossen. Vermerk unter ZURÜCKSPIELEN verwerfen. |
| `KP_FILES` | `shared/ui/kst_panel.c:62` | FILES | DATEIEN |
| `KP_WAIT` | `shared/ui/kst_panel.c:2957` | WAIT | WARTEN |
| `KP_TYPE` | `shared/ui/kst_panel.c:87` | TYPE | TYP |
| `KP_SERVO_V` | `shared/ui/kst_panel.c:452` | SERVO V%u | SERVO V%u |
| `KP_TYPE_V6` | `shared/ui/kst_panel.c:2844` | Servo type V6: 20 kHz drive. | Servotyp V6: Ansteuerung mit 20 kHz. |
| `KP_TYPE_V8` | `shared/ui/kst_panel.c:2844` | Servo type V8: 333 Hz drive. | Servotyp V8: Ansteuerung mit 333 Hz. |
| `KP_BACKUP_FILE` | `shared/ui/kst_panel.c:2891` | Backup file of this session: %s. | Backup-Datei dieser Sitzung: %s. |
| `KP_LOCKED_SKIPPED` | `shared/ui/kst_panel.c:2921` | ! %u locked values differ in the file. They will not be written. | ! %u gesperrte Werte weichen in der Datei ab. Sie werden nicht geschrieben. |
| `KP_LOCKED_SKIPPED_ONE` | `shared/ui/kst_panel.c:2919` | ! 1 file value differs. It is locked and will not be written. | ! 1 Dateiwert weicht ab. Er ist gesperrt und wird nicht geschrieben. |
| `KP_FILES_TITLE` | `shared/ui/kst_panel.c:1453` | SERVO FILES ON THE CARD | SERVODATEIEN AUF DER KARTE |
| `KP_FILES_TITLE_N` | `shared/ui/kst_panel.c:1450` | FILES %u TO %u OF %u | DATEIEN %u BIS %u VON %u |
| `KP_FILES_NEXT` | `shared/ui/kst_panel.c:1470` | Show more files. | Weitere Dateien anzeigen. |
| `KP_FILE_DIFF` | `shared/ui/kst_panel.c:1224` | %s: %u values differ from the servo settings. | %s: %u Werte weichen von den Servowerten ab. |
| `KP_FILE_DIFF_ONE` | `shared/ui/kst_panel.c:1222` | %s: 1 value differs from the servo settings. | %s: 1 Wert weicht von den Servowerten ab. |
| `KP_FILE_SAVE` | `shared/ui/kst_panel.c:1456` | Save this servo's settings in a new file. | Werte dieses Servos in einer neuen Datei speichern. |
| `KP_NAME_TITLE` | `shared/ui/kst_panel.c:1423` | FILE NAME | DATEINAME |
| `KP_NAME_BAD` | `shared/ui/kst_panel.c:1422` | INVALID NAME | UNGÜLTIGER NAME |
| `KP_NAME_EXISTS` | `shared/ui/kst_panel.c:1421` | NAME IN USE. CHOOSE ANOTHER. | NAME VERGEBEN. ANDEREN WÄHLEN. |
| `KP_FILE_TITLE` | `shared/ui/kst_panel.c:1219` | CHECK THIS FILE | DIESE DATEI PRÜFEN |
| `KP_HOLD_LOAD` | `shared/ui/kst_panel.c:1219` | HOLD TO LOAD | HALTEN ZUM LADEN |
| `KP_FILE_EDITED` | `shared/ui/kst_panel.c:157,794,955,1207` | Edited file. Values are checked as manual entries. | Bearbeitete Datei. Werte werden wie manuelle Eingaben geprüft. |
| `KP_FILE_UNTYPED` | `shared/ui/kst_panel.c:158,955,1208` | Edited file with no servo type. Values are checked as manual entries. | Bearbeitete Datei ohne Servotyp. Werte werden wie manuelle Eingaben geprüft. |
| `KP_FILE_EDITED_N` | `shared/ui/kst_panel.c:795` | Edited file: %u of %u values, checked as manual entries. | Bearbeitete Datei: %u von %u Werten, wie manuelle Eingaben geprüft. |
| `KP_FILE_UNTYPED_N` | `shared/ui/kst_panel.c:797` | Edited file, no servo type: %u of %u values, checked as manual entries. | Bearbeitete Datei ohne Servotyp: %u von %u Werten, wie manuelle Eingaben geprüft. |
| `KP_FILE_DENIED` | `shared/ui/kst_panel.c:171` | Cannot save: card write-protected, or KST is not a folder. | Speichern nicht möglich: Karte schreibgeschützt oder KST kein Ordner. |
| `KP_FILE_STAY` | `shared/ui/kst_panel.c:806` | %u locked values differ. They will not be written. | %u gesperrte Werte weichen ab. Sie werden nicht geschrieben. |
| `KP_FILE_STAY_ONE` | `shared/ui/kst_panel.c:804` | 1 locked value differs. It will not be written. | 1 gesperrter Wert weicht ab. Er wird nicht geschrieben. |
| `KP_WRITE_BACKUP` | `shared/ui/kst_panel.c:1181` | The current servo settings are saved to a new card file before writing. | Die bisherigen Servowerte werden vor dem Schreiben in einer neuen Kartendatei gesichert. |
| `KP_NO_FILE_1` | `shared/ui/kst_panel.c:1169` | No card. No backup file will be saved. | Keine Karte. Es wird keine Backup-Datei gespeichert. |
| `KP_NO_FILE_2` | `shared/ui/kst_panel.c:1170` | This session's backup is kept only in the bench's memory. | Das Backup dieser Sitzung liegt nur im Speicher des Prüfstands. |
| `KP_NO_FILE_3` | `shared/ui/kst_panel.c:1171` | It is lost when the session ends or the bench loses power. | Es geht beim Sitzungsende oder bei Stromausfall am Prüfstand verloren. |
| `KP_NO_BACKUP_FILE` | `shared/ui/kst_panel.c:2895` | No card, no backup file. Restore is available only within this session. | Keine Karte, keine Backup-Datei. Zurückspielen nur innerhalb dieser Sitzung möglich. |
| `KP_M_SAVED` | `shared/ui/kst_panel.c:118,919,920` | Servo settings saved to the card: %s. | Servowerte auf der Karte gespeichert: %s. |
| `KP_M_CARD_NONE` | `shared/ui/kst_panel.c:119` | No card. Nothing saved or written to the servo. | Keine Karte. Nichts gespeichert oder ins Servo geschrieben. |
| `KP_M_CARD_FULL` | `shared/ui/kst_panel.c:141` | Card full. Nothing saved or written to the servo. | Karte voll. Nichts gespeichert oder ins Servo geschrieben. |
| `KP_M_CARD_WRITE` | `shared/ui/kst_panel.c:142` | Card write error. Nothing was written to the servo. | Schreibfehler auf der Karte. Keine Werte ins Servo geschrieben. |
| `KP_M_CARD_READ` | `shared/ui/kst_panel.c:143` | Cannot read the card. | Die Karte lässt sich nicht lesen. |
| `KP_M_NO_FILES` | `shared/ui/kst_panel.c:144` | No servo files on the card. | Keine Servodateien auf der Karte. |
| `KP_M_NAME_BAD` | `shared/ui/kst_panel.c:145` | Invalid name: reserved name, or a dot at the start or end. | Ungültiger Name: reserviert oder mit einem Punkt am Anfang oder Ende. |
| `KP_M_NAME_EXISTS` | `shared/ui/kst_panel.c:146` | This file name is already in use. No file was replaced. | Dieser Dateiname ist bereits vergeben. Keine Datei wurde ersetzt. |
| `KP_M_LOAD_BAD` | `shared/ui/kst_panel.c:147` | Not a servo settings file. Not loaded. | Keine Servodatei. Nicht geladen. |
| `KP_M_LOAD_LINE` | `shared/ui/kst_panel.c:835` | Cannot read line %u. File not loaded. | Zeile %u ist nicht lesbar. Datei nicht geladen. |
| `KP_M_LOAD_CHECKSUM` | `shared/ui/kst_panel.c:148` | File damaged: checksum mismatch. Not loaded. | Datei beschädigt: Prüfsumme stimmt nicht. Nicht geladen. |
| `KP_M_LOAD_CHECKSUM_2` | `shared/ui/kst_panel.c:149` | Without the checksum line, it is treated as an edited file. | Ohne Prüfsummenzeile wird sie als bearbeitete Datei geladen. |
| `KP_M_LOAD_NEWER` | `shared/ui/kst_panel.c:150` | This file needs a newer bench version. Not loaded. | Diese Datei braucht eine neuere Prüfstand-Version. Nicht geladen. |
| `KP_M_LOAD_TYPE` | `shared/ui/kst_panel.c:151,923,924` | File is V%u, servo is V%u. Not loaded. | Datei ist V%u, Servo ist V%u. Nicht geladen. |
| `KP_M_LOAD_VALUE` | `shared/ui/kst_panel.c:152,842` | Line %u: %s is outside its allowed range. Not loaded. | Zeile %u: %s liegt außerhalb des erlaubten Bereichs. Nicht geladen. |
| `KP_M_LOAD_VALUE_ANY` | `shared/ui/kst_panel.c:844` | Line %u: a value is outside its allowed range. Not loaded. | Zeile %u: Ein Wert liegt außerhalb des erlaubten Bereichs. Nicht geladen. |
| `KP_M_LOAD_REFUSED` | `shared/ui/kst_panel.c:153` | File not loaded. It fails a limit check: | Datei nicht geladen. Grenzwertprüfung fehlgeschlagen: |
| `KP_M_LOAD_SAME` | `shared/ui/kst_panel.c:154,927,928` | %s matches the servo settings. Nothing to write. | %s stimmt mit den Servowerten überein. Nichts zu schreiben. |
| `KP_M_LOAD_SOFT` | `shared/ui/kst_panel.c:155` | Confirm the warnings before loading this file. | Bestätige die Warnungen, bevor du diese Datei lädst. |
| `KP_M_LOADED` | `shared/ui/kst_panel.c:156,157,158,931,936,961` | %s loaded. %u values differ from the servo settings. | %s geladen. %u Werte weichen von den Servowerten ab. |
| `KP_M_LOADED_ONE` | `shared/ui/kst_panel.c:933` | %s loaded. 1 value differs from the servo settings. | %s geladen. 1 Wert weicht von den Servowerten ab. |
| `KP_M_JOURNAL_ASK` | `shared/ui/kst_panel.c:123,125,765,769,891,2692` | Is this the servo whose write was interrupted on port %u? | Ist dies das Servo, dessen Schreibvorgang an Port %u unterbrochen wurde? |
| `KP_M_JOURNAL_ASK_PIN` | `shared/ui/kst_panel.c:767` | Is this the servo whose write was interrupted on GP%u? | Ist dies das Servo, dessen Schreibvorgang an GP%u unterbrochen wurde? |
| `KP_M_JOURNAL_SAME` | `shared/ui/kst_panel.c:124,2695` | It has the old values. If this is the same servo, discard the record under RESTORE. | Es enthält die alten Werte. Ist es dasselbe Servo, den Vermerk unter ZURÜCKSPIELEN verwerfen. |
| `KP_M_JOURNAL_DONE` | `shared/ui/kst_panel.c:126,2695` | It has the new values. If this is the same servo, discard the record under RESTORE. | Es enthält die neuen Werte. Ist es dasselbe Servo, den Vermerk unter ZURÜCKSPIELEN verwerfen. |
| `KP_M_INCONSISTENT` | `shared/ui/kst_panel.c:127` | The servo contains both old and new settings. | Das Servo enthält alte und neue Werte gemischt. |
| `KP_M_DO_NOT_USE` | `shared/ui/kst_panel.c:127,865` | Do not use this servo. Recover the backup with RESTORE. | Benutze dieses Servo nicht. Stelle das Backup mit ZURÜCKSPIELEN wieder her. |
| `KP_M_FORGOTTEN` | `shared/ui/kst_panel.c:128` | Record of the interrupted write discarded. | Vermerk zum unterbrochenen Schreibvorgang verworfen. |
| `KP_M_OUTSIDE` | `shared/ui/kst_panel.c:129,736` | The value is outside this field's allowed range. | Der Wert liegt außerhalb des erlaubten Bereichs für dieses Feld. |
| `KP_M_EDIT_LOCKED` | `shared/ui/kst_panel.c:130` | Display only. This value cannot be edited. | Nur Anzeige. Dieser Wert lässt sich nicht ändern. |
| `KP_M_NO_CHANGES` | `shared/ui/kst_panel.c:131` | No settings changed. | Keine Werte geändert. |
| `KP_M_NEED_ENABLE` | `shared/ui/kst_panel.c:132,183,206` | ENABLE WRITE TO SERVO is off. | SCHREIBEN INS SERVO FREIGEBEN ist aus. |
| `KP_M_NEED_READ` | `shared/ui/kst_panel.c:133,184,210,2720` | Read the servo settings first. Tap READ ALL. | Lies zuerst das Servo aus. Tippe auf ALLES AUSLESEN. |
| `KP_M_NEED_PORT` | `shared/ui/kst_panel.c:134` | No servo connected. Tap CONNECT. | Kein Servo verbunden. Tippe auf VERBINDEN. |
| `KP_M_NO_RESTORE` | `shared/ui/kst_panel.c:135` | The servo already contains these values. | Das Servo enthält diese Werte bereits. |
| `KP_M_RESTORE_REFUSED` | `shared/ui/kst_panel.c:136` | Backup not written. It fails a limit check: | Backup nicht geschrieben. Grenzwertprüfung fehlgeschlagen: |
| `KP_M_STOPPED` | `shared/ui/kst_panel.c:137,2680` | STOP: the operation was aborted. | STOP: der Vorgang wurde abgebrochen. |
| `KP_M_TIMEOUT` | `shared/ui/kst_panel.c:138` | No result from the coprocessor. Tap READ ALL. | Kein Ergebnis vom Koprozessor. ALLES AUSLESEN tippen. |
| `KP_M_UNKNOWN` | `shared/ui/kst_panel.c:885` | Result %u. | Ergebnis %u. |
| `KP_AT_REG` | `shared/ui/kst_panel.c:973` | Register %02X. %u writes completed. | Register %02X. %u Schreibvorgänge abgeschlossen. |
| `KP_AT_REG_ONE` | `shared/ui/kst_panel.c:970` | Register %02X. 1 write completed. | Register %02X. 1 Schreibvorgang abgeschlossen. |
| `KP_ST_OK` | `shared/ui/kst_panel.c:178` | Started. | Gestartet. |
| `KP_ST_DOWN` | `shared/ui/kst_panel.c:179,2773` | No connection to the coprocessor. | Keine Verbindung zum Koprozessor. |
| `KP_ST_NO_PAGE` | `shared/ui/kst_panel.c:180,2776` | Unavailable: the coprocessor has no programming port. | Nicht verfügbar: Der Koprozessor hat keinen Programmierport. |
| `KP_ST_BUSY` | `shared/ui/kst_panel.c:181,226` | Another operation is running. | Ein anderer Vorgang läuft. |
| `KP_ST_ARG` | `shared/ui/kst_panel.c:182` | Invalid request. | Ungültige Anfrage. |
| `KP_ST_NO_OP` | `shared/ui/kst_panel.c:185` | The coprocessor does not support this operation. Update its firmware. | Der Koprozessor unterstützt diesen Vorgang nicht. Firmware aktualisieren. |
| `KP_O_NONE` | `shared/ui/kst_panel.c:190` | No result. | Kein Ergebnis. |
| `KP_O_DONE` | `shared/ui/kst_panel.c:191,201,225` | Done. | Fertig. |
| `KP_O_REFUSED` | `shared/ui/kst_panel.c:192` | The coprocessor refused the request. | Der Koprozessor hat die Anfrage abgelehnt. |
| `KP_O_NACK` | `shared/ui/kst_panel.c:193,909,910` | The coprocessor rejected the frame. Reason: %u. | Der Koprozessor hat den Frame abgelehnt. Grund: %u. |
| `KP_O_NO_ANSWER` | `shared/ui/kst_panel.c:194` | No response from the coprocessor. Tap READ ALL. | Keine Antwort vom Koprozessor. ALLES AUSLESEN tippen. |
| `KP_O_RESTARTED` | `shared/ui/kst_panel.c:195,875` | Coprocessor restarted. Tap CONNECT, then READ ALL. | Koprozessor neu gestartet. VERBINDEN, dann ALLES AUSLESEN tippen. |
| `KP_O_RESTARTED_2` | `shared/ui/kst_panel.c:877` | Switch the servo supply off and on first. | Schalte zuerst die Servoversorgung aus und wieder ein. |
| `KP_M_SESSION_ENDED` | `shared/ui/kst_panel.c:168` | The port ended the session: power off or restart. | Der Port hat die Sitzung beendet: Versorgung aus oder Neustart. |
| `KP_M_SESSION_ENDED_2` | `shared/ui/kst_panel.c:169` | Switch the servo supply off and on, then tap CONNECT. | Servoversorgung aus- und einschalten, dann VERBINDEN tippen. |
| `KP_M_UNKNOWN_CODE` | `shared/ui/kst_panel.c:170,912,913` | The coprocessor reports code %u. This panel does not recognise it. | Koprozessor meldet Code %u. Dieses Panel kennt ihn nicht. |
| `KP_O_DROPPED` | `shared/ui/kst_panel.c:196` | Connection lost during the operation. Tap READ ALL. | Verbindung während des Vorgangs verloren. ALLES AUSLESEN tippen. |
| `KP_R_BUSY` | `shared/ui/kst_panel.c:202` | Port busy. Try again. | Port belegt. Erneut versuchen. |
| `KP_R_PORT` | `shared/ui/kst_panel.c:203` | This programming port does not exist on the coprocessor. | Diesen Programmierport gibt es auf dem Koprozessor nicht. |
| `KP_R_NO_PIN` | `shared/ui/kst_panel.c:215` | No pin assigned to the programming port. Nothing sent. | Dem Programmierport ist kein Pin zugeordnet. Nichts gesendet. |
| `KP_R_SUPPLY_RANGE` | `shared/ui/kst_panel.c:216` | The supply cannot provide this voltage or current limit. | Das Netzteil unterstützt diese Spannung oder Strombegrenzung nicht. |
| `KP_R_LEAVE_MS` | `shared/ui/kst_panel.c:218` | Wait before PWM is out of range. Allowed: 0 to 10000 ms. | Wartezeit vor PWM außerhalb des Bereichs. Erlaubt: 0 bis 10000 ms. |
| `KP_R_SUPPLY_LOW` | `shared/ui/kst_panel.c:220` | Supply below 4.8 V. Programming requires at least 4.8 V. | Versorgung unter 4.8 V. Programmieren benötigt mindestens 4.8 V. |
| `KP_R_LOCKED` | `shared/ui/kst_panel.c:219` | Servo state uncertain. The session stays open. Use RESTORE first. | Servozustand unklar. Sitzung bleibt offen. Zuerst ZURÜCKSPIELEN. |
| `KP_R_NO_SUPPLY` | `shared/ui/kst_panel.c:217` | Cannot switch the supply. PD mini is not responding or is off. | Versorgung nicht schaltbar. PD mini antwortet nicht oder ist aus. |
| `KP_R_UNSAFE` | `shared/ui/kst_panel.c:204` | The coprocessor has not yet accepted the link. Try again. | Koprozessor hat die Verbindung noch nicht freigegeben. Erneut versuchen. |
| `KP_R_NOT_PROGRAMMING` | `shared/ui/kst_panel.c:205` | Port not in programming mode. Tap CONNECT. | Port nicht im Programmiermodus. VERBINDEN tippen. |
| `KP_R_STAGED` | `shared/ui/kst_panel.c:207` | Received values were damaged. Nothing was written. | Werte kamen fehlerhaft an. Nichts geschrieben. |
| `KP_R_START` | `shared/ui/kst_panel.c:208` | Servo settings changed since the read. Tap READ ALL. | Servowerte haben sich seit dem Auslesen geändert. ALLES AUSLESEN tippen. |
| `KP_R_MUST_READ` | `shared/ui/kst_panel.c:209` | Read the servo settings first. Tap READ ALL. | Zuerst das Servo auslesen. ALLES AUSLESEN tippen. |
| `KP_R_PLAN_RULES` | `shared/ui/kst_panel.c:211` | These values fail the coprocessor's limit checks. | Diese Werte verletzen die Grenzwerte des Koprozessors. |
| `KP_R_PLAN_R00` | `shared/ui/kst_panel.c:212` | Register 00 differs. It is never written. | Register 00 weicht ab. Es wird nie geschrieben. |
| `KP_R_PLAN_PAIRING` | `shared/ui/kst_panel.c:213` | The pairing value differs. Use RELEASE PAIRING. | Der Pairing-Wert weicht ab. PAIRING LÖSEN verwenden. |
| `KP_R_PLAN_NO_PATH` | `shared/ui/kst_panel.c:214,245,247` | These values cannot be written in a safe order. | Für diese Werte gibt es keine sichere Schreibreihenfolge. |
| `KP_S_BUSY` | `shared/ui/kst_panel.c:227` | An operation is running in this session. | In dieser Sitzung läuft ein Vorgang. |
| `KP_S_ARG` | `shared/ui/kst_panel.c:228` | This session rejected the request. | Die Anfrage wurde in dieser Sitzung abgelehnt. |
| `KP_S_NO_SERVO` | `shared/ui/kst_panel.c:229` | No servo responds. Check the plug, supply and port. | Kein Servo antwortet. Stecker, Versorgung und Port prüfen. |
| `KP_S_NOT_IN_MODE` | `shared/ui/kst_panel.c:230` | Servo not in programming mode. Tap CONNECT. | Servo nicht im Programmiermodus. VERBINDEN tippen. |
| `KP_S_READ` | `shared/ui/kst_panel.c:231` | Could not read the servo. Check the plug, then tap READ ALL. | Servo konnte nicht ausgelesen werden. Stecker prüfen, dann ALLES AUSLESEN tippen. |
| `KP_S_NO_BACKUP` | `shared/ui/kst_panel.c:232` | No backup for this session. Tap READ ALL first. | Kein Backup dieser Sitzung. Zuerst ALLES AUSLESEN tippen. |
| `KP_S_LOCKED` | `shared/ui/kst_panel.c:234,2714` | Servo state uncertain after a write. Further writes are locked. | Servozustand nach dem Schreiben unklar. Weitere Schreibvorgänge gesperrt. |
| `KP_S_LOCKED_2` | `shared/ui/kst_panel.c:861,2715` | Only this session's backup can be restored with RESTORE. | Nur das Backup dieser Sitzung kann mit ZURÜCKSPIELEN wiederhergestellt werden. |
| `KP_S_BAD_PLAN` | `shared/ui/kst_panel.c:235` | The write plan was rejected for this session. | Der Schreibplan wurde für diese Sitzung abgelehnt. |
| `KP_S_UNCONFIRMED` | `shared/ui/kst_panel.c:236` | Restore was not confirmed. | Zurückspielen wurde nicht bestätigt. |
| `KP_S_CHANGED` | `shared/ui/kst_panel.c:237` | Servo settings differ from the values read earlier. Tap READ ALL. | Servowerte weichen von den ausgelesenen Werten ab. ALLES AUSLESEN tippen. |
| `KP_S_WRITE_FAILED` | `shared/ui/kst_panel.c:238,853` | Write failed. No values were left partly written. | Schreiben fehlgeschlagen. Keine Werte sind unvollständig geschrieben. |
| `KP_S_ROLLED_BACK` | `shared/ui/kst_panel.c:239,854` | Write failed. The previous value was restored. | Schreiben fehlgeschlagen. Der vorherige Wert wurde wiederhergestellt. |
| `KP_S_WRITE_PART` | `shared/ui/kst_panel.c:856` | Write failed. Earlier writes remain. Tap READ ALL. | Schreiben fehlgeschlagen. Zuvor geschriebene Werte bleiben. ALLES AUSLESEN tippen. |
| `KP_S_TORN` | `shared/ui/kst_panel.c:240` | Write failed and could not be undone. | Schreiben fehlgeschlagen. Rückgängig machen war nicht möglich. |
| `KP_S_UNINTENDED` | `shared/ui/kst_panel.c:241` | A register changed without being written. | Ein Register hat sich ohne Schreibzugriff geändert. |
| `KP_S_VERIFY` | `shared/ui/kst_panel.c:242` | Servo settings do not match the expected values. | Das Servo enthält nicht die erwarteten Werte. |
| `KP_S_DRIVER` | `shared/ui/kst_panel.c:243` | Port driver failed. Tap READ ALL. | Porttreiber ausgefallen. ALLES AUSLESEN tippen. |
| `KP_S_ABORTED` | `shared/ui/kst_panel.c:244` | Operation aborted. Tap READ ALL. | Vorgang abgebrochen. ALLES AUSLESEN tippen. |
| `KP_S_SUPPLY` | `shared/ui/kst_panel.c:246,915,916` | The supply did not switch within %u ms. Check the supply. | Versorgung hat nicht innerhalb von %u ms geschaltet. Versorgung prüfen. |
| `KP_S_TOO_COMPLEX` | `shared/ui/kst_panel.c:248` | Too many changes to plan at once. Nothing written. | Zu viele Änderungen für einen gemeinsamen Plan. Nichts geschrieben. |
| `KP_S_TOO_COMPLEX_2` | `shared/ui/kst_panel.c:863` | Split the changes. Write fewer values at a time. | Teile die Änderungen auf. Schreibe weniger Werte auf einmal. |
| `KP_H_DUTY_HIGH` | `shared/ui/kst_panel.c:253` | Max. duty exceeds its upper limit. | Max. Duty überschreitet die Obergrenze. |
| `KP_H_DUTY_LOW` | `shared/ui/kst_panel.c:254` | Max. duty is below its lower limit. | Max. Duty liegt unter der Untergrenze. |
| `KP_H_DUTY_COPY` | `shared/ui/kst_panel.c:255` | The two duty registers contain different values. | Die beiden Duty-Register enthalten unterschiedliche Werte. |
| `KP_H_BOOST_HIGH` | `shared/ui/kst_panel.c:256` | Boost exceeds its upper limit. | Boost überschreitet die Obergrenze. |
| `KP_H_STRETCH_INDEX` | `shared/ui/kst_panel.c:257` | This stretch value has not been confirmed. | Dieser Stretch-Wert ist nicht bestätigt. |
| `KP_H_SPD_HIGH` | `shared/ui/kst_panel.c:258` | Speed exceeds its upper limit. | Tempo überschreitet die Obergrenze. |
| `KP_H_SPD_SEL_LOW_SPD` | `shared/ui/kst_panel.c:259` | SPEED LIMIT requires SPEED to be above the minimum. | TEMPOGRENZE erfordert ein TEMPO über dem Mindestwert. |
| `KP_H_UNCONT_POS_LOW` | `shared/ui/kst_panel.c:260` | Failsafe position is below its lower limit. | Failsafe-Position liegt unter der Untergrenze. |
| `KP_H_UNCONT_POS_HIGH` | `shared/ui/kst_panel.c:261` | Failsafe position exceeds its upper limit. | Failsafe-Position überschreitet die Obergrenze. |
| `KP_H_UNCONT_POS_SPAN` | `shared/ui/kst_panel.c:262` | Failsafe position must be between Pulse Lower and Pulse Upper. | Failsafe-Position muss zwischen Pulse Lower und Pulse Upper liegen. |
| `KP_H_UNCONT_TIME_LOW` | `shared/ui/kst_panel.c:263` | Failsafe delay is below its lower limit. | Failsafe-Zeit liegt unter der Untergrenze. |
| `KP_H_UNCONT_TIME_HIGH` | `shared/ui/kst_panel.c:264` | Failsafe delay exceeds its upper limit. | Failsafe-Zeit überschreitet die Obergrenze. |
| `KP_H_LOWER_LOW` | `shared/ui/kst_panel.c:265` | Pulse Lower is below its lower limit. | Pulse Lower liegt unter der Untergrenze. |
| `KP_H_LOWER_MARGIN` | `shared/ui/kst_panel.c:266` | Pulse Lower is too close to Neutral. | Pulse Lower liegt zu nah an Neutral. |
| `KP_H_UPPER_HIGH` | `shared/ui/kst_panel.c:267` | Pulse Upper exceeds its upper limit. | Pulse Upper überschreitet die Obergrenze. |
| `KP_H_UPPER_MARGIN` | `shared/ui/kst_panel.c:268` | Pulse Upper is too close to Neutral. | Pulse Upper liegt zu nah an Neutral. |
| `KP_H_SPAN` | `shared/ui/kst_panel.c:269` | Pulse Lower and Pulse Upper are too close together. | Pulse Lower und Pulse Upper liegen zu nah beieinander. |
| `KP_H_NEUTRAL_LOW` | `shared/ui/kst_panel.c:270` | Neutral is below its lower limit. | Neutral liegt unter der Untergrenze. |
| `KP_H_NEUTRAL_HIGH` | `shared/ui/kst_panel.c:271` | Neutral exceeds its upper limit. | Neutral überschreitet die Obergrenze. |
| `KP_H_LEFT_ZERO` | `shared/ui/kst_panel.c:272` | Left Angle must not be 0. | Left Angle darf nicht 0 sein. |
| `KP_H_RIGHT_ZERO` | `shared/ui/kst_panel.c:273` | Right Angle must not be 0. | Right Angle darf nicht 0 sein. |
| `KP_H_GYRO_MODE` | `shared/ui/kst_panel.c:274` | Gyro mode limits are unknown. Writing is blocked. | Grenzen im Gyro-Modus unbekannt. Schreiben ist gesperrt. |
| `KP_H_LOCKED` | `shared/ui/kst_panel.c:275` | A locked value differs. It will not be written. | Ein gesperrter Wert weicht ab. Er wird nicht geschrieben. |
| `KP_H_NEVER` | `shared/ui/kst_panel.c:276` | A value that is never written differs. | Ein Wert, der nie geschrieben wird, weicht ab. |
| `KP_W_DUTY_RAISED` | `shared/ui/kst_panel.c:277` | Higher duty increases stall current and heat. | Höheres Duty erhöht den Blockierstrom und die Erwärmung. |
| `KP_W_DUTY_BELOW_HALF` | `shared/ui/kst_panel.c:278` | Low duty weakens the servo. It may not hold the control surface. | Niedriges Duty schwächt das Servo. Es hält das Ruder möglicherweise nicht. |
| `KP_W_BOOST_RAISED` | `shared/ui/kst_panel.c:279` | Higher boost can cause buzzing and heat. | Mehr Boost kann das Servo brummen und heiß werden lassen. |
| `KP_W_STRETCH_RAISED` | `shared/ui/kst_panel.c:280` | Higher stretch can cause servo oscillation. | Mehr Stretch kann das Servo zum Schwingen bringen. |
| `KP_W_SOFT_START_OFF` | `shared/ui/kst_panel.c:281` | Without soft start, the servo moves abruptly at power-on. | Ohne Soft Start bewegt sich das Servo beim Einschalten ruckartig. |
| `KP_W_SPD_SEL_ON` | `shared/ui/kst_panel.c:282` | Speed limit active. The servo moves at the set speed. | Tempogrenze aktiv. Das Servo bewegt sich mit dem eingestellten Tempo. |
| `KP_W_SPD_SLOW` | `shared/ui/kst_panel.c:283` | A slow servo can cause loss of control of the model. | Ein zu langsames Servo kann zum Kontrollverlust über das Modell führen. |
| `KP_W_DEAD_BAND_NARROWED` | `shared/ui/kst_panel.c:284` | A narrow dead band can cause back-and-forth movement and heat. | Ein schmales Dead Band kann Pendeln und Erwärmung verursachen. |
| `KP_W_DEAD_BAND_BELOW_1US` | `shared/ui/kst_panel.c:285` | Very narrow dead band. Back-and-forth movement and heat are likely. | Sehr schmales Dead Band. Pendeln und Erwärmung sind wahrscheinlich. |
| `KP_W_ALLOW_UNCONT_ON` | `shared/ui/kst_panel.c:286` | On signal loss the servo moves to the failsafe position. | Ohne Signal fährt das Servo auf die Failsafe-Position. |
| `KP_W_UNCONT_POS_FAR` | `shared/ui/kst_panel.c:287` | Failsafe position is far from the centre. | Die Failsafe-Position liegt weit von der Mitte entfernt. |
| `KP_W_UNCONT_TIME_SHORT` | `shared/ui/kst_panel.c:288` | Short dropouts will trigger the failsafe move. | Kurze Aussetzer lösen die Failsafe-Bewegung aus. |
| `KP_W_END_NEAR` | `shared/ui/kst_panel.c:289,291` | End point close to Neutral. Little travel remains on this side. | Endpunkt nah an Neutral. Auf dieser Seite bleibt wenig Weg. |
| `KP_W_END_MOVED` | `shared/ui/kst_panel.c:290,292` | End point changed. The servo travel per stick movement changes too. | Endpunkt geändert. Dadurch ändert sich auch der Servoweg je Knüppelbewegung. |
| `KP_W_NEUTRAL_MOVED` | `shared/ui/kst_panel.c:293` | The centre shifts. One side moves closer to the mechanical stop. | Die Mitte verschiebt sich. Eine Seite kommt näher an den mechanischen Anschlag. |
| `KP_W_REVERSION_CHANGED` | `shared/ui/kst_panel.c:294` | Direction reversed. Check all control surfaces before flight. | Drehrichtung umgekehrt. Prüfe vor dem Flug alle Ruder. |
| `KP_W_ANGLE_RAISED` | `shared/ui/kst_panel.c:295,297` | More travel can push the servo against its stop and burn it out. | Mehr Weg kann das Servo gegen den Anschlag fahren und durchbrennen lassen. |
| `KP_W_ANGLE_SMALL` | `shared/ui/kst_panel.c:296,298` | Small angle. Full stick movement gives little servo travel. | Kleiner Winkel. Voller Knüppelweg ergibt nur wenig Servoweg. |
| `KP_W_GYRO_CHANGED` | `shared/ui/kst_panel.c:299` | Pulse standard changed. The wrong standard drives the servo into its stop. | Pulsstandard geändert. Ein falscher Standard fährt das Servo in den Anschlag. |
| `KP_W_NARROW_BAND_GAIN` | `shared/ui/kst_panel.c:300` | Narrow dead band and higher gain can cause oscillation. | Schmales Dead Band und mehr Gain können Schwingungen verursachen. |
| `KP_W_HIGH_DUTY_TRAVEL` | `shared/ui/kst_panel.c:301` | High duty and more travel: a stall can destroy the servo. | Hohes Duty und mehr Weg: Blockieren kann das Servo zerstören. |

## SETUP

| Text id | Where it is used | English | German |
|---|---|---|---|
| `SET_COUNT` | `shared/ui/settings_screen.c:140,883` | %d SETTINGS | %d EINSTELLUNGEN |
| `SET_HUB_OUTPUTS` | `shared/ui/settings_screen.c:148` | PROTOCOLS AND PINS | PROTOKOLLE UND PINS |
| `SET_HUB_PICKER` | `shared/ui/settings_screen.c:149` | ON THE BOARD DRAWING | AUF DER PLATINE |
| `SET_HUB_UPDATES` | `shared/ui/settings_screen.c:150` | RELEASES ON GITHUB | VERSIONEN AUF GITHUB |
| `SET_RESET` | `shared/ui/settings_screen.c:991` | RESET CATEGORY | KATEGORIE ZURÜCKSETZEN |
| `SET_SAVED` | `shared/ui/settings_screen.c:863` | SAVED | GESPEICHERT |
| `SET_WHEN_IDLE` | `shared/ui/settings_screen.c:864` | WHEN IDLE | BEI STILLSTAND |
| `SET_NOT_SAVED` | `shared/ui/settings_screen.c:865` | NOT SAVED | NICHT GESPEICHERT |
| `SET_SAVE` | `shared/ui/settings_screen.c:865,1042` | SAVE | SPEICHERN |
| `SET_UNHELD` | `shared/ui/settings_screen.c:916` | Not accepted by the coprocessor. | Vom Koprozessor nicht übernommen. |
| `SET_LEAVE_TITLE` | `shared/ui/settings_screen.c:1017` | UNSAVED CHANGES | UNGESPEICHERTE ÄNDERUNGEN |
| `SET_LEAVE_WHY` | `shared/ui/settings_screen.c:1019` | Your changes are active, but not saved. Tap SAVE to keep them after a restart. | Deine Änderungen sind aktiv, aber noch nicht gespeichert. Tippe auf SPEICHERN, sonst gehen sie beim Neustart verloren. |
| `SET_LEAVE_MORE` | `shared/ui/settings_screen.c:433` | and %d more | und %d weitere |
| `SET_LEAVE_FAILED` | `shared/ui/settings_screen.c:1028` | Saving failed. Tap SAVE to try again. | Speichern fehlgeschlagen. Tippe erneut auf SPEICHERN. |
| `SET_LEAVE_ARMED` | `shared/ui/settings_screen.c:1033` | DISCARD waits until the bench is SAFE. | VERWERFEN wartet, bis der Prüfstand SAFE ist. |
| `SET_STAY` | `shared/ui/settings_screen.c:1038` | STAY | BLEIBEN |
| `SET_DISCARD` | `shared/ui/settings_screen.c:1040` | DISCARD | VERWERFEN |
| `SET_DUT_NAME` | `shared/ui/settings_screen.c:169` | DUT name | Name des Prüflings |

## SPLASH

| Text id | Where it is used | English | German |
|---|---|---|---|
| `SPLASH_BOARD` | `shared/ui/splash_screen.c:22` | BOARD | PLATINE |
| `SPLASH_DISPLAY` | `shared/ui/splash_screen.c:22` | DISPLAY | DISPLAY |
| `SPLASH_TOUCH` | `shared/ui/splash_screen.c:22` | TOUCH | TOUCH |
| `SPLASH_STORAGE` | `shared/ui/splash_screen.c:22` | STORAGE | SPEICHER |
| `SPLASH_SETTINGS` | `shared/ui/splash_screen.c:23` | SETTINGS | EINSTELLUNGEN |
| `SPLASH_LINK` | `shared/ui/splash_screen.c:23` | LINK | VERBINDUNG |
| `SPLASH_IOMCU` | `shared/ui/splash_screen.c:23` | COPROCESSOR | KOPROZESSOR |
| `SPLASH_TAGLINE` | `shared/ui/splash_screen.c:118` | motor, ESC and servo test bench | Prüfstand für Motor, ESC und Servo |
| `SPLASH_CREDIT` | `shared/ui/splash_screen.c:122` | Designed by JuWi | Entwickelt von JuWi |
| `SPLASH_NO_PANEL` | `firmware/panel/main/main.c:1912` | no panel | kein Panel |
| `SPLASH_NO_ANSWER` | `firmware/panel/main/main.c:1926,2076` | no response | keine Antwort |
| `SPLASH_NO_CARD` | `firmware/panel/main/main.c:1975` | no card | keine Karte |
| `SPLASH_NVS_MISSING` | `firmware/panel/main/main.c:1981` | NVS unavailable | NVS nicht verfügbar |
| `SPLASH_NOT_OPENED` | `firmware/panel/main/main.c:1993` | not opened | nicht geöffnet |
| `SPLASH_REFUSED` | `firmware/panel/main/main.c:2078` | refused | abgelehnt |
| `SPLASH_WRONG_REPLY` | `firmware/panel/main/main.c:2080` | unexpected response | unerwartete Antwort |
| `SPLASH_NO_LINK` | `firmware/panel/main/main.c:2085` | no link | keine Verbindung |

## SUPPLY

| Text id | Where it is used | English | German |
|---|---|---|---|
| `SUP_ROW_V_MAX` | `shared/ui/supply_screen.c:152` | VOLTAGE MAX | SPANNUNG MAX |
| `SUP_ROW_I_MAX` | `shared/ui/supply_screen.c:153` | CURRENT MAX | STROM MAX |
| `SUP_ROW_V_START` | `shared/ui/supply_screen.c:154` | START VOLTAGE | STARTSPANNUNG |
| `SUP_ROW_I_START` | `shared/ui/supply_screen.c:155` | START CURRENT | STARTSTROM |
| `SUP_ROW_TRIP_I` | `shared/ui/supply_screen.c:156` | CURRENT TRIP | ÜBERSTROM |
| `SUP_ROW_TRIP_V` | `shared/ui/supply_screen.c:157` | VOLTAGE TRIP | ÜBERSPANNUNG |
| `SUP_ROW_TRIP_MS` | `shared/ui/supply_screen.c:158` | TRIP TIME | ABSCHALTZEIT |
| `SUP_ROW_CONFIRM_SLIDE` | `shared/ui/supply_screen.c:159` | SLIDER AND STEPS | REGLER UND SCHRITTE |
| `SUP_ROW_CONFIRM_KEYS` | `shared/ui/supply_screen.c:160` | KEYPAD | TASTENFELD |
| `SUP_MODE_TRIP` | `shared/ui/supply_screen.c:1409` | TRIPPED | ABGESCHALTET |
| `SUP_MODE_OFF` | `shared/ui/supply_screen.c:1415` | OFF | AUS |
| `SUP_LINE_TRIP_I` | `shared/ui/supply_screen.c:1428` | Overcurrent trip. | Wegen Überstrom abgeschaltet. |
| `SUP_LINE_TRIP_V` | `shared/ui/supply_screen.c:1431` | Overvoltage trip. | Wegen Überspannung abgeschaltet. |
| `SUP_LINE_CC` | `shared/ui/supply_screen.c:1434` | Current limit active. | Strombegrenzung aktiv. |
| `SUP_LINE_CV` | `shared/ui/supply_screen.c:1436` | At the set voltage. | Sollspannung erreicht. |
| `SUP_LINE_PORT` | `shared/ui/supply_screen.c:1425` | Servo programmer. | Servoprogrammierung. |
| `SUP_MODEL` | `shared/ui/supply_screen.c:1447` | SIMULATED SUPPLY | SIMULIERTES NETZTEIL |
| `SUP_PD_PINS` | `shared/ui/supply_screen.c:1449` | PD MINI TX GP%d RX GP%d | PD MINI TX GP%d RX GP%d |
| `SUP_PD_NO_PINS` | `shared/ui/supply_screen.c:1451` | PD MINI NO PINS | PD MINI OHNE PINS |
| `SUP_ONLINE` | `shared/ui/supply_screen.c:1454` | ONLINE | ONLINE |
| `SUP_NOT_ANSWERING` | `shared/ui/supply_screen.c:1454` | NOT ANSWERING | ANTWORTET NICHT |
| `SUP_MODE` | `shared/ui/supply_screen.c:1574` | MODE | MODUS |
| `SUP_TABLE_HEAD` | `shared/ui/supply_screen.c:1585` | CHANNEL        SET            NOW | KANAL          SOLL           JETZT |
| `SUP_OV_TITLE` | `shared/ui/supply_screen.c:1725` | SUPPLY SETTINGS | NETZTEIL-OPTIONEN |
| `SUP_RESET_PDMINI` | `shared/ui/supply_screen.c:1732` | RESET PD MINI | PD MINI ZURÜCKSETZEN |
| `SUP_CAPS` | `shared/ui/supply_screen.c:1739` | LIMITS | OBERGRENZEN |
| `SUP_AFTER_RESTART` | `shared/ui/supply_screen.c:1741` | AFTER RESTART | NACH DEM NEUSTART |
| `SUP_TRIPS` | `shared/ui/supply_screen.c:1743` | TRIPS | ABSCHALTUNGEN |
| `SUP_CONFIRM_ON` | `shared/ui/supply_screen.c:1745` | CONFIRM WHILE ON | RÜCKFRAGE IM BETRIEB |
| `SUP_HELP_1` | `shared/ui/supply_screen.c:1757` | The output switches off if a reading | Der Ausgang schaltet aus, wenn |
| `SUP_HELP_2` | `shared/ui/supply_screen.c:1757` | stays above its trip limit for | ein Messwert für die Abschaltzeit |
| `SUP_HELP_3` | `shared/ui/supply_screen.c:1757` | the set trip time. | über seiner Abschaltgrenze liegt. |
| `SUP_ASK_OFF` | `shared/ui/supply_screen.c:1811` | Turn off this prompt under SETTINGS > CONFIRM WHILE ON. | Diese Rückfrage unter OPTIONEN > RÜCKFRAGE IM BETRIEB ausschalten. |
| `SUP_TAG_LIVE` | `shared/ui/supply_screen.c:1817` | LIVE OUTPUT | AUSGANG LIVE |
| `SUP_TAG_HELD` | `shared/ui/supply_screen.c:1817` | LAST OUTPUT VALUES | LETZTE AUSGANGSWERTE |
| `SUP_TAG_IDLE` | `shared/ui/supply_screen.c:1817` | OUTPUT IDLE | AUSGANG INAKTIV |
| `SUP_SET_POINTS` | `shared/ui/supply_screen.c:1849` | SET POINTS | SOLLWERTE |
| `SUP_NOT_ON_YET` | `shared/ui/supply_screen.c:1901` | Output has not been switched on yet. | Ausgang wurde noch nicht eingeschaltet. |

## Top band and help bubbles

| Text id | Where it is used | English | German |
|---|---|---|---|
| `BAND_SAFE` | `shared/ui/ui_band.c:82` | SAFE | SAFE |
| `BAND_FAULT` | `shared/ui/ui_band.c:84` | FAULT %02X | FEHLER %02X |
| `BAND_NO_LINK` | `shared/ui/ui_band.c:89` | NO LINK | KEINE VERBINDUNG |
| `LINK_REFUSED` | `shared/ui/ui_band.c:66` | Rejected link frames: %lu | Abgelehnte Verbindungsframes: %lu |
| `BH_LINK_UP` | `shared/ui/ui_band.c:267` | The coprocessor is responding on the CAN bus. The screens control its outputs and show its readings. The panel requests data every 50 ms. If the coprocessor stops responding while ARMED, the bench switches to SAFE. | Der Koprozessor antwortet am CAN-Bus. Die Seiten steuern seine Ausgänge und zeigen seine Messwerte. Das Panel fragt alle 50 ms Daten ab. Antwortet der Koprozessor bei ARMED nicht mehr, schaltet der Prüfstand auf SAFE. |
| `BH_NO_LINK` | `shared/ui/ui_band.c:267` | No coprocessor is responding. The panel tries once a second. The screens show simulated data, and the coprocessor outputs stay off. Check the coprocessor's power supply and CAN cable. A different major protocol version also causes NO LINK. Install firmware from the same release on both boards. | Kein Koprozessor antwortet. Das Panel versucht es einmal pro Sekunde. Die Seiten zeigen simulierte Daten. Die Ausgänge des Koprozessors bleiben aus. Prüfe seine Versorgung und das CAN-Kabel. Eine andere Hauptversion des Protokolls führt ebenfalls zu KEINE VERBINDUNG. Installiere auf beiden Platinen Firmware aus demselben Release. |
| `BH_MODE_BENCH` | `shared/ui/ui_band.c:278` | The coprocessor is responding. The screens control its outputs and show its data. If it stops responding, the mode changes to SIM. | Der Koprozessor antwortet. Die Seiten steuern seine Ausgänge und zeigen seine Daten. Antwortet er nicht mehr, wechselt der Modus zu SIM. |
| `BH_MODE_SIM` | `shared/ui/ui_band.c:281` | No coprocessor is responding. The panel simulates the values; they are not measurements. ARM does not activate any output. When a coprocessor responds, the mode changes to BENCH and any simulated ARMED state resets to SAFE. | Kein Koprozessor antwortet. Das Panel simuliert die Werte. Es sind keine Messwerte. ARM aktiviert keinen Ausgang. Sobald ein Koprozessor antwortet, wechselt der Modus zu BENCH. Ein in der Simulation gesetztes ARMED wird auf SAFE zurückgesetzt. |
| `BH_MODE_INJECT` | `shared/ui/ui_band.c:286` | This is a test-bench build, not a release version. Touch input also comes from the injector at I2C address 0x14. It is combined with input from the panel touchscreen, which still works. LINK shows whether a coprocessor responds. The older of the two sources determines the touch age. A contact on the touchscreen counts as detected touch. Without a controller at 0x5D, the status always shows INJECT. If the injector was absent at startup, a restart is needed. | Dies ist ein Testbuild für den Prüfstand, keine Release-Version. Touch-Eingaben kommen auch vom Injector an der I2C-Adresse 0x14. Sie werden mit den Eingaben des weiterhin nutzbaren Touchscreens zusammengeführt. LINK zeigt, ob ein Koprozessor antwortet. Für das Touch-Alter zählt die ältere der beiden Quellen. Eine Berührung am Touchscreen zählt als erkannter Touch. Ohne Controller an 0x5D steht die Anzeige immer auf INJECT. Fehlte der Injector beim Start, ist ein Neustart nötig. |
| `BH_MODE_INJECT_LOST` | `shared/ui/ui_band.c:287` | With a controller at 0x5D, injector contacts are released after 3 failed reads in a row. If the touchscreen has no contact, touch is marked silent 200 ms after the injector's last reply. After another 500 ms, the bench switches to SAFE and blocks ARM. After 1000 ms of silence, the injector is detached. The touchscreen alone then sets the touch age, ARM is available again, and the status shows BENCH or SIM. The injector is checked every 1000 ms and reconnected when it replies. | Mit einem Controller an 0x5D werden Injector-Kontakte nach 3 fehlgeschlagenen Abfragen in Folge aufgehoben. Ohne Berührung am Touchscreen gilt Touch 200 ms nach der letzten Injector-Antwort als stumm. Weitere 500 ms später schaltet der Prüfstand auf SAFE und sperrt ARM. Nach 1000 ms Stille wird der Injector getrennt. Dann bestimmt allein der Touchscreen das Touch-Alter. ARM ist wieder möglich, und die Anzeige zeigt BENCH oder SIM. Der Injector wird alle 1000 ms abgefragt und bei einer Antwort wieder verbunden. |
| `BH_SAFE` | `shared/ui/ui_band.c:293` | Motor and servo outputs send no pulses. To activate them, hold ARM for 2 s on MOTOR & ESC or SERVO. The PD mini output on SUPPLY is switched separately. STOP turns it off too. | Motor- und Servoausgänge geben keine Pulse aus. Halte ARM auf MOTOR & ESC oder SERVO für 2 s, um sie zu aktivieren. Der PD-mini-Ausgang auf SUPPLY wird getrennt davon geschaltet. STOP schaltet auch ihn aus. |
| `BH_ARMED_MOTOR` | `shared/ui/ui_band.c:297` | Motor outputs send pulses. Servo outputs do not. | Motorausgänge geben Pulse aus. Servoausgänge nicht. |
| `BH_ARMED_SERVO` | `shared/ui/ui_band.c:298` | Servo outputs send pulses. Motor outputs do not. | Servoausgänge geben Pulse aus. Motorausgänge nicht. |
| `BH_ARMED_ALL` | `shared/ui/ui_band.c:299` | All assigned motor and servo outputs send pulses. | Alle zugeordneten Motor- und Servoausgänge geben Pulse aus. |
| `BH_ARMED_REST` | `shared/ui/ui_band.c:301` | A channel with no command for 500 ms returns to rest: throttle at 0, servo at the centre of its travel. STOP, DISARM or leaving the screen switches the bench to SAFE. | Erhält ein Kanal 500 ms keinen Befehl, geht er in Ruhestellung: Gas auf 0, Servo in die Mitte seines Wegs. STOP, DISARM oder das Verlassen der Seite schaltet den Prüfstand auf SAFE. |
| `BH_ARMED_SIM` | `shared/ui/ui_band.c:303` | No coprocessor is responding. ARM runs the panel's simulation, not real outputs. | Kein Koprozessor antwortet. ARM startet nur die Simulation des Panels. Keine echten Ausgänge werden angesteuert. |
| `BH_CLOCK` | `shared/ui/ui_band.c:307` | Shows the run time since switching to ARMED. At SAFE, the clock stops and keeps the last run time until the next ARM. It shows minutes and seconds, adding hours from 60 min. | Zeigt die Laufzeit seit dem Wechsel zu ARMED. Bei SAFE stoppt die Uhr und zeigt die letzte Laufzeit bis zum nächsten ARM. Anzeige in Minuten und Sekunden, ab 60 min auch in Stunden. |
| `BH_F_LINK_SILENT` | `shared/ui/ui_band.c:208` | The coprocessor received no data for 200 ms and switched itself to SAFE. The PD mini output is off. A panel restart or loose CAN cable can cause this. The fault bit stays set until the next ARM. If it returns, check the CAN cable. | Der Koprozessor hat 200 ms keine Daten empfangen und selbst auf SAFE geschaltet. Der PD-mini-Ausgang ist aus. Ursache kann ein Panel-Neustart oder ein lockeres CAN-Kabel sein. Das Fehlerbit bleibt bis zum nächsten ARM gesetzt. Tritt es erneut auf, prüfe das CAN-Kabel. |
| `BH_F_OVERCURRENT` | `shared/ui/ui_band.c:209` | Stopped due to overcurrent. | Wegen Überstrom gestoppt. |
| `BH_F_OVERTEMP` | `shared/ui/ui_band.c:210` | Stopped due to overheating. | Wegen Übertemperatur gestoppt. |
| `BH_F_STALL` | `shared/ui/ui_band.c:211` | Stopped because the motor stalled. | Wegen blockiertem Motor gestoppt. |
| `BH_F_HEARTBEAT` | `shared/ui/ui_band.c:212` | The safety signal from the panel to the coprocessor has stopped. After any stop, the panel keeps it stopped until the next ARM. Otherwise, the emergency stop is pressed or the heartbeat wire is open. The coprocessor is SAFE and blocks ARMED. The PD mini output is off. Release the emergency stop or check the wire, then hold ARM. | Das Sicherheitssignal vom Panel zum Koprozessor ist ausgefallen. Nach jedem Stopp hält das Panel es bis zum nächsten ARM an. Andernfalls ist der Not-Aus gedrückt oder die Heartbeat-Leitung unterbrochen. Der Koprozessor ist SAFE und sperrt ARMED. Der PD-mini-Ausgang ist aus. Löse den Not-Aus oder prüfe die Leitung und halte danach ARM. |
| `BH_F_VERSION` | `shared/ui/ui_band.c:213` | The versions on the two devices do not match. | Die Versionen der beiden Geräte passen nicht zusammen. |
| `BH_F_STORE_OFF` | `shared/ui/ui_band.c:214` | The coprocessor cannot write flash during this boot. Sent settings work from RAM but are lost at restart. Restart it to try again. Whether that clears the fault bit has not been tested. | Der Koprozessor kann seit diesem Start nicht in den Flash schreiben. Übertragene Einstellungen wirken nur aus dem RAM und gehen beim Neustart verloren. Starte ihn neu und versuche es erneut. Ob das Fehlerbit dadurch verschwindet, wurde nicht geprüft. |
| `BH_F_NEVER` | `shared/ui/ui_band.c:237` | This release never sets this bit. | Dieses Release setzt dieses Bit nie. |
| `BH_F_OTHER` | `shared/ui/ui_band.c:245` | Other bits (%04X) are unknown to this panel. | Weitere Bits (%04X) sind diesem Panel unbekannt. |
| `BH_F_FLASH` | `shared/ui/ui_band.c:251` | If a bit appears that this release never sets, install firmware from the same release on both boards. | Erscheint ein Bit, das dieses Release nie setzt, installiere auf beiden Platinen Firmware aus demselben Release. |

## WI-FI

| Text id | Where it is used | English | German |
|---|---|---|---|
| `WF_AP` | `shared/ui/wifi_screen.c:512` | ACCESS POINT | WLAN-HOTSPOT |
| `WF_JOIN` | `shared/ui/wifi_screen.c:512` | JOIN HOME NETWORK | MIT HEIMNETZ VERBINDEN |
| `WF_CHOOSE` | `shared/ui/wifi_screen.c:512` | CHOOSE HOME NETWORK | HEIMNETZ WÄHLEN |
| `WF_FORGET` | `shared/ui/wifi_screen.c:513` | FORGET HOME NETWORK | HEIMNETZ LÖSCHEN |
| `WF_CODE_ON` | `shared/ui/wifi_screen.c:514` | ACCESS CODE: ON | ZUGANGSCODE: EIN |
| `WF_CODE_OFF` | `shared/ui/wifi_screen.c:514` | ACCESS CODE: OFF | ZUGANGSCODE: AUS |
| `WF_JOBS_1` | `shared/ui/wifi_screen.c:527` | Use a browser to download files from the card | Im Browser kannst du Dateien von der Karte herunterladen |
| `WF_JOBS_2` | `shared/ui/wifi_screen.c:528` | or upload panel firmware. | oder Panel-Firmware hochladen. |
| `WF_SAVE_FAILED_1` | `shared/ui/wifi_screen.c:555` | Not saved. Writing to flash failed. | Nicht gespeichert. Schreiben in den Flash fehlgeschlagen. |
| `WF_SAVE_FAILED_2` | `shared/ui/wifi_screen.c:557` | Saving will be tried again with the next change. | Bei der nächsten Änderung wird es erneut versucht. |
| `WF_OPEN_1` | `shared/ui/wifi_screen.c:573` | With the access code off, any device | Ohne Zugangscode kann jedes Gerät |
| `WF_OPEN_2` | `shared/ui/wifi_screen.c:574` | on the network can open the page. | im Netzwerk die Seite öffnen. |
| `WF_OFF_IDLE` | `shared/ui/wifi_screen.c:489` | Wi-Fi switched off after 10 min without a request. | WLAN nach 10 min ohne Anfrage ausgeschaltet. |
| `WF_OFF_GUESSES` | `shared/ui/wifi_screen.c:490` | Wi-Fi switched off after 16 wrong access codes. | WLAN nach 16 falschen Zugangscodes ausgeschaltet. |
| `WF_TRY_AP` | `shared/ui/wifi_screen.c:566` | ACCESS POINT is still available. | WLAN-HOTSPOT ist weiterhin verfügbar. |
| `WF_STARTING` | `shared/ui/wifi_screen.c:593` | Starting the access point. | WLAN-Hotspot wird gestartet. |
| `WF_WAIT_ADDRESS` | `shared/ui/wifi_screen.c:601` | Waiting for an IP address. | Wartet auf eine IP-Adresse. |
| `WF_NETWORK` | `shared/ui/wifi_screen.c:606,611` | Network | Netzwerk |
| `WF_PASSWORD` | `shared/ui/wifi_screen.c:607` | Password | Passwort |
| `WF_ADDRESS` | `shared/ui/wifi_screen.c:608,614` | IP address | IP-Adresse |
| `WF_NAME` | `shared/ui/wifi_screen.c:616` | Name | Name |
| `WF_CODE` | `shared/ui/wifi_screen.c:620` | Access code | Zugangscode |
| `WF_OPEN_ALL` | `shared/ui/wifi_screen.c:623` | Access code off. Any device on the network can open the page. | Zugangscode aus. Jedes Gerät im Netzwerk kann die Seite öffnen. |
| `WF_PLAIN` | `shared/ui/wifi_screen.c:628` | HTTP: access codes and files are sent without encryption. | HTTP: Zugangscode und Dateien werden unverschlüsselt übertragen. |
| `WF_CHANNEL` | `shared/ui/wifi_screen.c:632` | 2.4 GHz, channel %u | 2.4 GHz, Kanal %u |
| `WF_IDLE` | `shared/ui/wifi_screen.c:636` | Off in %u min unless a request arrives. | Aus in %u min, wenn keine Anfrage kommt. |
| `WF_RECEIVING` | `shared/ui/wifi_screen.c:643` | Receiving %lu of %lu bytes (%u %%) | Empfang: %lu von %lu Bytes (%u %%) |
| `WF_UP_REFUSED` | `shared/ui/wifi_screen.c:648` | The last upload was rejected. The web page shows why. | Der letzte Upload wurde abgelehnt. Der Grund steht auf der Webseite. |
| `WF_OFF` | `shared/ui/wifi_screen.c:652` | WI-FI OFF | WLAN AUS |
| `WF_SCANNING` | `shared/ui/wifi_screen.c:659,662` | Searching for networks. | Netzwerke werden gesucht. |
| `WF_SCAN_NONE` | `shared/ui/wifi_screen.c:661` | No WPA2 or WPA3 network found. | Kein WPA2- oder WPA3-Netzwerk gefunden. |
| `WF_RESCAN` | `shared/ui/wifi_screen.c:688` | SCAN AGAIN | NEU SUCHEN |
| `WF_HIDDEN` | `shared/ui/wifi_screen.c:691` | HIDDEN NETWORK | VERSTECKTES NETZWERK |
| `WF_KEY_SSID` | `shared/ui/wifi_screen.c:320` | NETWORK NAME | NETZWERKNAME |
| `WF_KEY_PASS` | `shared/ui/wifi_screen.c:329,353` | PASSWORD FOR %s | PASSWORT FÜR %s |
| `WF_Q_TITLE` | `shared/ui/wifi_screen.c:710` | INSTALL PANEL FIRMWARE? | PANEL-FIRMWARE INSTALLIEREN? |
| `WF_Q_NEW` | `shared/ui/wifi_screen.c:712` | Received: %s | Empfangen: %s |
| `WF_Q_OLDER` | `shared/ui/wifi_screen.c:719` | Version %s is older than the running version %s. | Version %s ist älter als die laufende Version %s. |
| `WF_Q_REBUILD` | `shared/ui/wifi_screen.c:725` | Same version number, different build. | Gleiche Versionsnummer, anderer Build. |
| `WF_Q_INSTALLED` | `shared/ui/wifi_screen.c:729` | This build is already running. No installation needed. | Dieser Build läuft bereits. Keine Installation nötig. |
| `WF_Q_NOTE_1` | `shared/ui/wifi_screen.c:736` | INSTALL writes and checks the firmware, then restarts the panel. | INSTALLIEREN schreibt und prüft die Firmware. Danach startet das Panel neu. |
| `WF_Q_NOTE_2` | `shared/ui/wifi_screen.c:737,971` | Coprocessor outputs stay in failsafe until the connection returns. | Die Ausgänge des Koprozessors bleiben im Failsafe, bis die Verbindung wieder steht. |
| `WF_Q_DRAWING` | `shared/ui/wifi_screen.c:741` | Installation is waiting while a board drawing is downloaded or saved. | Installation wartet, während eine Platinenzeichnung geladen oder gespeichert wird. |
| `WF_Q_COPROC` | `shared/ui/wifi_screen.c:743` | Installation is waiting for the coprocessor update to finish. | Installation wartet auf das Ende des Koprozessor-Updates. |
| `WF_HOLD_INSTALL` | `shared/ui/wifi_screen.c:751` | HOLD 2 s: INSTALL | 2 s HALTEN: INSTALLIEREN |
| `UP_TITLE` | `shared/ui/wifi_screen.c:912` | WRITING PANEL FIRMWARE %s | PANEL-FIRMWARE %s WIRD GESCHRIEBEN |
| `UP_BYTES` | `shared/ui/wifi_screen.c:921` | %lu of %lu bytes (%u %%) | %lu von %lu Bytes (%u %%) |
| `UP_NOTE_1` | `shared/ui/wifi_screen.c:967` | Do not switch off the panel. | Schalte das Panel nicht aus. |
| `UP_NOTE_2` | `shared/ui/wifi_screen.c:969` | The display may flicker while flash memory is erased. | Die Anzeige kann beim Löschen des Flash-Speichers flackern. |
| `UP_RESTART` | `shared/ui/wifi_screen.c:957` | Firmware written and checked. Restarting now. | Firmware geschrieben und geprüft. Neustart läuft. |
| `UP_FAILED` | `shared/ui/wifi_screen.c:960` | Writing failed. | Schreiben fehlgeschlagen. |
| `UP_FAILED_2` | `shared/ui/wifi_screen.c:953,963` | The current firmware is unchanged and stays installed. | Die aktuelle Firmware bleibt unverändert installiert. |
| `UP_CLOSE` | `shared/ui/wifi_screen.c:955,965` | CLOSE | SCHLIESSEN |
| `UPD_DL_TITLE` | `shared/ui/wifi_screen.c:912` | DOWNLOADING PANEL FIRMWARE %s | PANEL-FIRMWARE %s WIRD GELADEN |
| `UPD_DL_NOTE_1` | `shared/ui/wifi_screen.c:935` | The firmware is written only after all checks pass. | Die Firmware wird erst geschrieben, wenn alle Prüfungen bestanden sind. |
| `UPD_DL_NOTE_2` | `shared/ui/wifi_screen.c:938` | The panel then installs the firmware and restarts. | Danach installiert das Panel die Firmware und startet neu. |
| `UPD_DL_FAILED` | `shared/ui/wifi_screen.c:942` | Download stopped. | Download abgebrochen. |

## Shared by several screens

| Text id | Where it is used | English | German |
|---|---|---|---|
| `OFF` | `shared/ui/kst_panel.c:529,2965`<br>`shared/ui/servo_screen.c:5775`<br>`shared/ui/supply_screen.c:1714`<br>`shared/ui/wifi_screen.c:620` | OFF | AUS |
| `CANCEL` | `shared/ui/coproc_update_screen.c:257`<br>`shared/ui/kst_panel.c:3080,3106`<br>`shared/ui/log_viewer_screen.c:737`<br>`shared/ui/programmer_screen.c:4051`<br>`shared/ui/servo_screen.c:6127,6176,6228,6271`<br>`shared/ui/supply_screen.c:1808`<br>`shared/ui/ui_keypad.c:279`<br>`shared/ui/ui_textkey.c:42`<br>`shared/ui/upd_screen.c:503`<br>`shared/ui/wifi_screen.c:694,749,940` | CANCEL | ABBRECHEN |
| `OUT_PROG_PORT` | `shared/ui/outputs_screen.c:660`<br>`shared/ui/picker_screen.c:616,1017` | PROG PORT | PROG-PORT |
| `HERO_PEAK` | `shared/ui/motor_screen.c:132,133,133,1106`<br>`shared/ui/supply_screen.c:1552,1556` | pk | max |
| `HERO_MIN` | `shared/ui/motor_screen.c:132,1040`<br>`shared/ui/supply_screen.c:1547` | min | min |
| `MO_VOLT` | `shared/ui/motor_screen.c:137`<br>`shared/ui/supply_screen.c:272,1546` | VOLT | SPANN. |
| `MO_CURR` | `shared/ui/motor_screen.c:137`<br>`shared/ui/supply_screen.c:273,1550` | CURR | STROM |
| `MO_PWR` | `shared/ui/motor_screen.c:138`<br>`shared/ui/supply_screen.c:274,1554` | PWR | LEIST. |
| `MO_TAB_PLOT` | `shared/ui/motor_screen.c:146`<br>`shared/ui/supply_screen.c:124` | PLOT | GRAFIK |
| `MO_TAB_TABLE` | `shared/ui/motor_screen.c:147`<br>`shared/ui/supply_screen.c:125` | TABLE | TABELLE |
| `MO_ROW_VOLTAGE` | `shared/ui/motor_screen.c:1031`<br>`shared/ui/supply_screen.c:1591` | VOLTAGE | SPANNUNG |
| `MO_ROW_CURRENT` | `shared/ui/motor_screen.c:1031`<br>`shared/ui/supply_screen.c:1591` | CURRENT | STROM |
| `MO_ROW_POWER` | `shared/ui/motor_screen.c:1031`<br>`shared/ui/supply_screen.c:1591` | POWER | LEISTUNG |
| `MO_CONTROL` | `shared/ui/motor_screen.c:1226`<br>`shared/ui/supply_screen.c:1852` | CONTROL | STEUERUNG |
| `MO_RESET_PEAKS` | `shared/ui/motor_screen.c:1408`<br>`shared/ui/supply_screen.c:1942` | RESET PEAKS | SPITZENWERTE ZURÜCKSETZEN |
| `SUP_VOLTAGE` | `shared/ui/servo_screen.c:2658,6252`<br>`shared/ui/supply_screen.c:892,1645,1790` | VOLTAGE | SPANNUNG |
| `SUP_CURRENT_LIMIT` | `shared/ui/servo_screen.c:2661,6254`<br>`shared/ui/supply_screen.c:895,1646,1791` | CURRENT LIMIT | STROMBEGRENZUNG |
| `SUP_OUTPUT_ON` | `shared/ui/servo_screen.c:5531`<br>`shared/ui/supply_screen.c:1397` | OUTPUT ON | AUSGANG EIN |
| `SUP_OUTPUT_OFF` | `shared/ui/servo_screen.c:5531`<br>`shared/ui/supply_screen.c:1397` | OUTPUT OFF | AUSGANG AUS |
| `SUP_SETTINGS` | `shared/ui/servo_screen.c:5611`<br>`shared/ui/supply_screen.c:1477` | SETTINGS | OPTIONEN |
| `SUP_CLOSE` | `shared/ui/kst_panel.c:3050`<br>`shared/ui/programmer_screen.c:3685`<br>`shared/ui/servo_screen.c:5096,5232,6037`<br>`shared/ui/supply_screen.c:1727` | CLOSE | SCHLIESSEN |
| `SUP_SAVED` | `shared/ui/servo_screen.c:5700`<br>`shared/ui/supply_screen.c:1766` | SAVED | GESPEICHERT |
| `SUP_SAVE_WAITING` | `shared/ui/servo_screen.c:5700`<br>`shared/ui/supply_screen.c:1766` | SAVE PENDING | SPEICHERN AUSSTEHEND |
| `SUP_NOT_SAVED` | `shared/ui/servo_screen.c:5700`<br>`shared/ui/supply_screen.c:1766` | NOT SAVED | NICHT GESPEICHERT |
| `SUP_SETUP_NOT_SAVED` | `shared/ui/servo_screen.c:5701`<br>`shared/ui/supply_screen.c:1767` | SETUP CHANGES NOT SAVED | ÄNDERUNGEN IN SETUP NICHT GESPEICHERT |
| `SUP_OUTPUT_IS_ON` | `shared/ui/servo_screen.c:6245`<br>`shared/ui/supply_screen.c:1783` | OUTPUT IS ON | AUSGANG IST EINGESCHALTET |
| `SUP_ASK_WHY` | `shared/ui/servo_screen.c:6247`<br>`shared/ui/supply_screen.c:1785` | Changing a set value affects the connected load immediately. | Ein geänderter Sollwert wirkt sofort auf die angeschlossene Last. |
| `SUP_APPLY` | `shared/ui/servo_screen.c:6269`<br>`shared/ui/supply_screen.c:1806` | APPLY | ÜBERNEHMEN |
| `LOG_BACK` | `shared/ui/log_viewer_screen.c:1049,1207`<br>`shared/ui/programmer_screen.c:981,3857` | BACK | ZURÜCK |
| `SV_HOLD_TO_APPLY` | `shared/ui/kst_panel.c:1139,1332`<br>`shared/ui/servo_screen.c:6140,6183` | HOLD TO APPLY | HALTEN ZUM ÜBERNEHMEN |
| `SV_HV_1` | `shared/ui/kst_panel.c:1336`<br>`shared/ui/servo_screen.c:6215` | Standard servos are rated for 4.8 to 6.0 V. | Standardservos sind für 4.8 bis 6.0 V ausgelegt. |
| `SV_HV_2` | `shared/ui/kst_panel.c:1337`<br>`shared/ui/servo_screen.c:6215` | Above 6.0 V, use only an HV (high-voltage) servo | Über 6.0 V nur ein HV-Servo (Hochvolt) verwenden, |
| `SV_HV_3` | `shared/ui/kst_panel.c:1338`<br>`shared/ui/servo_screen.c:6215` | rated for the selected voltage. | das für die gewählte Spannung zugelassen ist. |
| `SV_HV_4` | `shared/ui/kst_panel.c:1339`<br>`shared/ui/servo_screen.c:6215` | A standard servo can be destroyed immediately. | Ein Standardservo kann sofort zerstört werden. |
| `SV_HV_5` | `shared/ui/kst_panel.c:1340`<br>`shared/ui/servo_screen.c:6215` | Check the servo's datasheet before applying the voltage. | Prüfe das Datenblatt des Servos, bevor du die Spannung übernimmst. |
| `WF_HOME_IS` | `shared/ui/upd_screen.c:527`<br>`shared/ui/wifi_screen.c:535` | Home network: %s | Heimnetz: %s |
| `WF_HOME_NONE` | `shared/ui/upd_screen.c:280,531`<br>`shared/ui/wifi_screen.c:537` | No home network saved. | Kein Heimnetz gespeichert. |
| `WF_NEEDS_1` | `shared/ui/upd_screen.c:198`<br>`shared/ui/wifi_screen.c:549,745` | Wi-Fi requires SAFE, the supply output off, | Für WLAN muss SAFE aktiv und das Netzteil aus sein. |
| `WF_NEEDS_2` | `shared/ui/upd_screen.c:199`<br>`shared/ui/wifi_screen.c:550,746` | and no open recording session. | Es darf kein Aufzeichnungslauf offen sein. |
| `WF_UNAVAIL_1` | `shared/ui/upd_screen.c:523`<br>`shared/ui/wifi_screen.c:543` | Wi-Fi setup failed at startup. It is unavailable. | WLAN wurde beim Start nicht eingerichtet und ist nicht verfügbar. |
| `WF_UNAVAIL_2` | `shared/ui/upd_screen.c:524`<br>`shared/ui/wifi_screen.c:545` | Restart to try again. | Starte für einen neuen Versuch neu. |
| `WF_OFF_ARMED` | `shared/ui/upd_screen.c:216`<br>`shared/ui/wifi_screen.c:485` | Wi-Fi switched off: bench ARMED. | WLAN ausgeschaltet: Prüfstand ARMED. |
| `WF_OFF_SUPPLY` | `shared/ui/upd_screen.c:217`<br>`shared/ui/wifi_screen.c:486` | Wi-Fi switched off: supply output on. | WLAN ausgeschaltet: Netzteilausgang ein. |
| `WF_OFF_RUN` | `shared/ui/upd_screen.c:218`<br>`shared/ui/wifi_screen.c:487` | Wi-Fi switched off: a recording session started. | WLAN ausgeschaltet: Aufzeichnungslauf gestartet. |
| `WF_OFF_STOP` | `shared/ui/upd_screen.c:219`<br>`shared/ui/wifi_screen.c:488` | Wi-Fi switched off: STOP. | WLAN ausgeschaltet: STOP. |
| `WF_OFF_FAILED` | `shared/ui/upd_screen.c:293`<br>`shared/ui/wifi_screen.c:491` | Wi-Fi could not start. | WLAN konnte nicht starten. |
| `WF_JOIN_PASSWORD` | `shared/ui/upd_screen.c:177`<br>`shared/ui/wifi_screen.c:494` | Could not connect. Password rejected. | Verbindung fehlgeschlagen. Passwort abgelehnt. |
| `WF_JOIN_NOT_FOUND` | `shared/ui/upd_screen.c:182`<br>`shared/ui/wifi_screen.c:499` | Could not connect. Network not found. | Verbindung fehlgeschlagen. Netzwerk nicht gefunden. |
| `WF_JOIN_NO_ADDRESS` | `shared/ui/upd_screen.c:178`<br>`shared/ui/wifi_screen.c:495` | Connection failed. No IP address within 15 s. | Keine Verbindung. Nach 15 s noch keine IP-Adresse. |
| `WF_JOIN_LOST` | `shared/ui/upd_screen.c:179`<br>`shared/ui/wifi_screen.c:496` | Connection to the home network lost. | Verbindung zum Heimnetz verloren. |
| `WF_JOINING` | `shared/ui/upd_screen.c:258`<br>`shared/ui/wifi_screen.c:598` | Connecting to %s | Verbindung mit %s wird aufgebaut |
| `WF_Q_RUN` | `shared/ui/upd_screen.c:519`<br>`shared/ui/wifi_screen.c:714` | Running: %s | Aktuelle Version: %s |
