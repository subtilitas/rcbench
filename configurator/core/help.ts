// What a function with a tick does, in one or two sentences per language.
//
// The help bubble of a tick in the chooser opens with this text. The catalogue
// holds the name of a function and no description; a part has its `role`,
// which the bubble of a part reads instead. The parts a function needs and the
// supply of a part are not written here: the page derives them from the
// catalogue and the resolver, so they cannot drift from the data.
//
// Every fact below restates a source the catalogue entry of the function
// names, or a check entry of the catalogue. `tools/check.ts` holds one entry
// per function with a tick, no other, and applies the text rules of the data.

import type { FunctionId, Text } from '../contract.ts';

function t(en: string, de: string): Text {
  return { en, de };
}

export const FUNCTION_HELP: Readonly<Partial<Record<FunctionId, Text>>> = Object.freeze({
  servo: t(
    'Sends a PWM signal to a servo from the SERVO screen, with controls for pulse width, trim, travel and reverse. The default frame rate is 50Hz.',
    'Gibt auf der Seite SERVO ein PWM-Signal an ein Servo aus. Du kannst Pulsbreite, TRIMMUNG, WEG und UMKEHREN einstellen. Die voreingestellte Pulsrate beträgt 50Hz.',
  ),
  servotest: t(
    'Measures servo current at rest, during movement and at each end position, travel time between the ends, and the voltage below which motion stops. The PD mini sets 4.8V and 6.0V; with HV SERVO on, it also sets 7.4V and 8.4V.',
    'Misst den Servostrom in Ruhe, während der Bewegung und an jeder Endlage, die Stellzeit zwischen den Endlagen und die Spannung, unter der die Bewegung aussetzt. Der PD mini stellt 4,8V und 6,0V ein, bei HV-SERVO auf EIN auch 7,4V und 8,4V.',
  ),
  servocur: t(
    'Measures servo current on channel 1 of the INA3221 at 1000 samples per second over the 400kHz sensor bus.',
    'Erfasst den Servostrom an Kanal 1 des INA3221 mit 1000 Abtastwerten pro Sekunde über den 400kHz-Sensorbus.',
  ),
  angle: t(
    'Measures the servo horn angle with an AS5600 magnetic sensor on the output shaft: 12-bit resolution, 500 readings per second.',
    'Erfasst den Winkel des Ruderhorns mit einem AS5600-Magnetsensor an der Abtriebswelle: 12 Bit Auflösung, 500 Messwerte pro Sekunde.',
  ),
  servoprog: t(
    'Reads the 32 registers of a KST or Chaservo servo over its signal wire, displays their settings and writes changes back. The setup routes the servo signal to the programming port, GPIO 13.',
    'Liest die 32 Register eines KST- oder Chaservo-Servos über dessen Signalleitung aus, zeigt die Einstellungen an und schreibt Änderungen zurück. Der Aufbau führt das Servosignal zum Programmierport an GPIO 13.',
  ),
  esc: t(
    'Sends a PWM or DShot throttle signal to an ESC over signal and GND. The ESC takes its power from its own battery (ESC power A) or from the PD mini (ESC power B).',
    'Gibt ein PWM- oder DShot-Gassignal über Signal und GND an einen ESC aus. Der ESC wird aus seinem eigenen Akku (ESC-Versorgung A) oder vom PD mini (ESC-Versorgung B) versorgt.',
  ),
  escpow: t(
    'Measures ESC supply voltage and current with the INA228 at 500 readings per second each, and accumulates the energy used.',
    'Erfasst Versorgungsspannung und Strom des ESC mit dem INA228 mit jeweils 500 Messwerten pro Sekunde und summiert die verbrauchte Energie.',
  ),
  stick: t(
    'Changes ESC settings through the throttle-stick menu; 24 of the 72 ESC profiles are runnable. The bench selects the throttle positions defined by the profile and counts menu beeps as pulses in the PD mini supply current. Use a resistive load or a securely mounted motor without a propeller.',
    'Ändert ESC-Einstellungen über das Gasknüppelmenü; 24 der 72 ESC-Profile sind ausführbar. Der Prüfstand fährt die im Profil festgelegten Gasstellungen an und zählt Menüpieptöne als Pulse im Versorgungsstrom des PD mini. Verwende Lastwiderstände oder einen fest montierten Motor ohne Propeller.',
  ),
  tap: t(
    'Measures the pitch and duration of ESC beeps on phase A through the phase tap. The motor simulator replaces the motor.',
    'Erfasst Tonhöhe und Dauer der ESC-Pieptöne an Phase A über den Phasenabgriff. Der Motorsimulator ersetzt den Motor.',
  ),
  supply: t(
    'The panel sets the PD mini output: 1 to 20V, 0.05 to 3A (vendor values, not measured). Output voltage must stay at least 0.7V below the module input voltage. The bench reads the module every 102 to 106ms.',
    'Das Panel stellt den Ausgang des PD mini ein: 1 bis 20V, 0,05 bis 3A (Herstellerangaben, nicht gemessen). Die Ausgangsspannung muss mindestens 0,7V unter der Eingangsspannung des Moduls bleiben. Der Prüfstand liest das Modul alle 102 bis 106ms aus.',
  ),
  logs: t(
    'Records runs on the microSD card in BENCH/LOGS while the bench is ARMED, or SAFE with the supply output on. Automatic servo tests and limit runs use USER/LOGS. The SD CARD tile in SETUP lets you disable each destination or choose another folder. The panel log viewer opens these files.',
    'Zeichnet Läufe auf der microSD-Karte in BENCH/LOGS auf, solange der Prüfstand ARMED ist oder bei SAFE der Netzteilausgang eingeschaltet ist. Automatische Servotests und Endlagenläufe verwenden USER/LOGS. Unter SETUP → SD CARD kannst du jedes Ziel abschalten oder einen anderen Ordner wählen. Die Aufzeichnungsansicht des Panels öffnet diese Dateien.',
  ),
  estop: t(
    'A latching button in the heartbeat wire. Pressing it opens the normally closed (NC) contact. After 150ms without a heartbeat edge, the IOmcu switches the bench to SAFE and turns off its outputs and the PD mini output. The button does not interrupt power: an ESC battery or separate servo supply stays connected.',
    'Ein rastender Taster in der Heartbeat-Leitung. Beim Drücken öffnet sein Öffnerkontakt (NC). Nach 150ms ohne Heartbeat-Flanke schaltet der IOmcu den Prüfstand auf SAFE und schaltet seine Ausgänge sowie den Ausgang des PD mini ab. Der Taster unterbricht keine Versorgung: Ein ESC-Akku oder eine separate Servoversorgung bleibt angeschlossen.',
  ),
});
