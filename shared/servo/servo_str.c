/*
 * The automatic servo test's words: the CSV header, the phase and abort
 * names, and the line formats of the TXT report (servo_report.c).
 *
 * Every string the run writes or the SERVO screen shows about it is in
 * k_str, and nowhere else, so a translation is another table of the same
 * entries (the panel's German is in shared/ui/ui_text_de.c).  The report is
 * written in the language of the table the run was started with, and the
 * CSV always in English, because tools read it by its words.  A report is
 * UTF-8, as the table's strings are.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_str.h"
#include "servo_test.h"

#include <stddef.h>

static const char *const k_str[SERVO_STR_COUNT] = {
    [SERVO_STR_PHASE_NONE]       = "",
    [SERVO_STR_PHASE_SET]        = "SET",
    [SERVO_STR_PHASE_SETTLE]     = "SETTLE",
    [SERVO_STR_PHASE_IDLE]       = "IDLE",
    [SERVO_STR_PHASE_MOVE]       = "MOVE",
    [SERVO_STR_PHASE_HOLD]       = "HOLD",
    [SERVO_STR_PASS]             = "PASS",
    [SERVO_STR_FAIL]             = "FAIL",
    [SERVO_STR_ABORTED]          = "ABORTED",
    [SERVO_STR_NOT_MEASURABLE]   = "NOT MEASURABLE",
    [SERVO_STR_TEST_STEP]        = "STEP",
    [SERVO_STR_TEST_BROWNOUT]    = "BROWN-OUT",
    [SERVO_STR_AB_NONE]          = "",
    [SERVO_STR_AB_STOP]          = "STOP",
    [SERVO_STR_AB_DISARMED]      = "bench SAFE",
    [SERVO_STR_AB_LINK]          = "link lost",
    [SERVO_STR_AB_LEFT]          = "SERVO screen left",
    [SERVO_STR_AB_OPERATOR]      = "stopped by the operator",
    [SERVO_STR_AB_SETTINGS]      = "servo settings changed",
    [SERVO_STR_AB_TOUCH]         = "touch events lost",
    [SERVO_STR_AB_SUPPLY_LOST]   = "supply not answering",
    [SERVO_STR_AB_TRIPPED]       = "supply tripped",
    [SERVO_STR_AB_SUPPLY_OFF]    = "supply output went off",
    [SERVO_STR_AB_STALE]         = "no new supply reading for 1.5 s",
    [SERVO_STR_AB_NOT_ON]        = "supply output did not come on",
    [SERVO_STR_AB_SET_NOT_TAKEN] = "set point not read back in 3 s",
    [SERVO_STR_AB_CAP]           = "step above the voltage cap",
    [SERVO_STR_AB_STALL]         = "above STALL AT for 1 s",
    /* The run's reasons are also a line of the SERVO screen's result box:
     * 31 characters at most. */
    [SERVO_STR_AB_CC]            = "constant current for 1 s",
    [SERVO_STR_AB_WIN_STALE]     = "no INA3221 window for 0.2 s",
    [SERVO_STR_AB_INA_RESET]     = "INA3221 reset itself",
    [SERVO_STR_AB_INA_SILENT]    = "INA3221 not answering",
    [SERVO_STR_AB_INA_NO_WINDOW] = "INA3221 window without current",
    [SERVO_STR_AB_INA_SETUP]     = "INA3221 set-up not held",
    [SERVO_STR_AB_INA_METER]     = "INA3221 no longer the meter",
    [SERVO_STR_START_OK]         = "",
    [SERVO_STR_START_NO_STEPS]   = "NO STEP CHOSEN",
    [SERVO_STR_START_ABOVE_CAP]  = "A STEP IS OUTSIDE THE CAPS",
    [SERVO_STR_START_NO_SUPPLY]  = "SUPPLY NOT ANSWERING",
    [SERVO_STR_START_BAD_ENDS]   = "RANGE TOO SMALL",
    [SERVO_STR_START_NOT_ARMED]  = "ARM FIRST",
    [SERVO_STR_START_BUSY]       = "LAST REPORT STILL WRITING",

    /* The report.  The labels pad to column 17, so the values line up. */
    [SERVO_STR_R_TITLE]          = "RCBENCH SERVO TEST REPORT",
    [SERVO_STR_R_RESULT]         = "Result:         %s",
    [SERVO_STR_R_RESULT_WHY]     = "Result:         %s - %s",
    [SERVO_STR_R_RESULT_UNSEEN]  = "Result:         %s - %u of %u counted "
                                   "moves showed no movement in the current",
    [SERVO_STR_R_RESULT_BO_UNSEEN] = "Result:         %s - no movement seen "
                                     "at %.2f V, the brown-out walk's first "
                                     "voltage",
    [SERVO_STR_R_DEVICE]         = "Device:         %s",
    [SERVO_STR_R_FIRMWARE]       = "Firmware:       rcbench %s",
    [SERVO_STR_R_LOG]            = "Log:            the .CSV with this "
                                   "file's number, one row per supply "
                                   "reading",
    [SERVO_STR_R_SUPPLY]         = "Supply:         %s",
    [SERVO_STR_R_SUPPLY_MODEL]   = "the panel's model: every reading is "
                                   "simulated",
    [SERVO_STR_R_READINGS]       = "Readings:       %.1f /s taken by the "
                                   "supply, %.1f /s reached the test",
    [SERVO_STR_R_READINGS_FEW]   = "Readings:       fewer than two",
    [SERVO_STR_R_SKIPPED]        = "Skipped:        %lu readings the supply "
                                   "took never reached the test",
    [SERVO_STR_R_RESOLUTION]     = "Resolution:     one reading every %lu "
                                   "ms: a travel time is late by up to that",
    [SERVO_STR_R_RESOLUTION_UNKNOWN] = "Resolution:     not known",
    [SERVO_STR_R_LAG]            = "Lag:            about %u ms from a change "
                                   "of current to the reading that shows it",
    [SERVO_STR_R_REPEATS]        = "Repeats:        a reading can repeat the "
                                   "last value for several readings",
    [SERVO_STR_R_UPPER_BOUND]    = "Travel times:   an upper bound, not "
                                   "checked against the limit",
    [SERVO_STR_R_DURATION]       = "Duration:       %lu.%01lu s",
    [SERVO_STR_R_ROWS]           = "Log rows:       %lu written, %lu lost to "
                                   "a full queue",
    [SERVO_STR_R_SETTINGS]       = "SETTINGS IN FORCE",
    [SERVO_STR_R_TYPE]           = "Type:           %s, centre %u us, %u-%u "
                                   "us, trim %+d us, reverse %s",
    [SERVO_STR_R_RATE]           = "Frame rate:     %u Hz",
    [SERVO_STR_R_DANGER]         = "Can destroy:    %s",
    [SERVO_STR_R_DANGER_NONE]    = "none in force",
    [SERVO_STR_R_HV]             = "HV servo:       %s",
    [SERVO_STR_R_HV_OFF]         = "OFF, no step above 6.0 V",
    [SERVO_STR_R_HV_ON_RUN]      = "ON, steps above 6.0 V run",
    [SERVO_STR_R_HV_ON_NONE]     = "ON, no step above 6.0 V chosen",
    [SERVO_STR_R_ENDS]           = "Ends:           %u us and %u us (RANGE "
                                   "%u %% of TRAVEL +/-%u deg)",
    [SERVO_STR_R_STEPS]          = "Steps:         ",
    [SERVO_STR_R_STEPS_NONE]     = " none",
    [SERVO_STR_R_BROWNOUT]       = "Brown-out:      from %.2f V down in "
                                   "%.2f V steps to %.2f V",
    [SERVO_STR_R_BROWNOUT_NOT_RUN] = "Brown-out:      not run",
    [SERVO_STR_R_I_LIMIT]        = "Current limit:  %.2f A",
    [SERVO_STR_R_TIMING]         = "Timing:         settle %u ms, idle %u "
                                   "ms, dwell %u ms (hold measured %u ms)",
    [SERVO_STR_R_LEN_MOVES]      = "Length:         %u movements a step",
    [SERVO_STR_R_LEN_TIME]       = "Length:         %u s a step",
    [SERVO_STR_R_LIMITS]         = "Limits:         idle %s, holding %s, "
                                   "travel %s, stall %.2f A",
    [SERVO_STR_R_ON]             = "ON",
    [SERVO_STR_R_OFF]            = "OFF",
    [SERVO_STR_R_PER_STEP]       = "RESULTS PER STEP (currents in A, times "
                                   "in ms)",
    /* Over the columns of the step lines' format in step_lines(). */
    [SERVO_STR_R_COLUMNS]        = "Set V  Meas V  V min  Idle   Thresh "
                                   "Moving Peak   Hold lo Hold hi Travel "
                                   "Longest Moves Late Unseen",
    [SERVO_STR_R_STEP_NOT_RUN]   = "%5.2f  not run",
    [SERVO_STR_R_CUT_SHORT]      = " (cut short)",
    [SERVO_STR_R_NO_STEP]        = "No step ran.",
    [SERVO_STR_R_LATE]           = "Late: moves seen moving that did not "
                                   "arrive within %u ms.",
    [SERVO_STR_R_UNSEEN]         = "Unseen: moves with no movement seen; not "
                                   "timed, not counted late.",
    [SERVO_STR_R_THRESHOLD]      = "Thresh: movement is a reading max(%.3f A, "
                                   "%.0f x idle noise) from the level before "
                                   "the command.",
    [SERVO_STR_R_ARRIVAL]        = "Arrival: after a reading Thresh above "
                                   "the end's holding level, the first back "
                                   "within %.2f A of it.",
    [SERVO_STR_R_BO_HEAD]        = "BROWN-OUT",
    [SERVO_STR_R_BO_NOT_RUN]     = "Not run.",
    [SERVO_STR_R_BO_NOT_REACHED] = "Not reached.",
    [SERVO_STR_R_BO_STOPPED]     = "Moved at %.2f V; no movement seen at "
                                   "%.2f V.",
    [SERVO_STR_R_BO_ALL]         = "Moved at every step down to %.2f V; "
                                   "lower not tested.",
    [SERVO_STR_R_BO_NONE]        = "No movement seen at %.2f V, the first "
                                   "step: not measurable.",
    [SERVO_STR_R_BO_RULE]        = "No movement: no reading of a move more "
                                   "than Thresh, %.3f A at %.2f V, from the "
                                   "level before it.",
    [SERVO_STR_R_LIM_HEAD]       = "AGAINST THE PASS/FAIL LIMITS",
    [SERVO_STR_R_LIM_IDLE]       = "Idle current     highest %.3f A, limit "
                                   "%s: %s",
    [SERVO_STR_R_LIM_HOLD]       = "Holding current  highest %.3f A, limit "
                                   "%s: %s",
    [SERVO_STR_R_LIM_TRAVEL]     = "Travel time      longest %lu ms, limit "
                                   "%u ms: %s",
    [SERVO_STR_R_LIM_TRAVEL_OFF] = "Travel time      longest %lu ms, limit "
                                   "OFF: %s",
    [SERVO_STR_R_LIM_TRAVEL_BOUND] = "Travel time      longest %lu ms, limit "
                                     "%u ms: upper bound, not checked "
                                     "against the limit",
    [SERVO_STR_R_LIM_TRAVEL_NONE] = "Travel time      longest --, limit %s: "
                                    "not measured, no move arrived",
    [SERVO_STR_R_LIM_STALL]      = "Stall threshold  highest %.3f A, STALL "
                                   "AT %.2f A: %s",
    [SERVO_STR_R_LIM_LATE]       = "Moves arrived    %u late: %s",
    [SERVO_STR_R_LIM_UNSEEN]     = "Moves seen       %u unseen: %s",
    [SERVO_STR_R_LIM_BO_SEEN]    = "Brown-out start  movement seen at %.2f "
                                   "V: %s",
    [SERVO_STR_R_LIM_BO_UNSEEN]  = "Brown-out start  no movement seen at "
                                   "%.2f V: %s",
    [SERVO_STR_R_NOT_CHECKED]    = "not checked",
    [SERVO_STR_R_NOT_MEASURED]   = "not measured",
    [SERVO_STR_R_UNM_HEAD]       = "NOT MEASURED",
    [SERVO_STR_R_UNM_POSITION]   = "Position: nothing measures the horn; "
                                   "every result is the supply's current.",
    [SERVO_STR_R_UNM_PEAKS]      = "Current peaks between two readings: the "
                                   "supply reports one value a reading.",
    [SERVO_STR_R_UNM_PATH]       = "The command's way from the panel to the "
                                   "pin, inside every travel time.",

    /* The output encoder. */
    [SERVO_STR_R_ENC_DEVICE]     = "Encoder:        AS5600 on the horn shaft, "
                                   "centre count %u, %lu angle readings",
    [SERVO_STR_R_ENC_HEAD]       = "ENCODER (angles in degrees from the centre "
                                   "count, times in ms)",
    [SERVO_STR_R_ENC_COLUMNS]    = "Set V  End lo   Err lo   End hi   Err hi   "
                                   "Travel Longest Moves Unmoved Late",
    [SERVO_STR_R_ENC_CMD]        = "Commanded: %+.1f deg at the low end, %+.1f "
                                   "deg at the high end.",
    [SERVO_STR_R_ENC_END]        = "End: the settled angle, -180 to +180 deg "
                                   "from the centre count. Err: End minus "
                                   "commanded, the shortest way round.",
    [SERVO_STR_R_ENC_SETTLED]    = "Settled: the angle has stayed within %.2f "
                                   "deg for %u ms.",
    [SERVO_STR_R_ENC_TRAVEL]     = "Travel: the command to the start of that "
                                   "stillness, with the command's way from "
                                   "the panel to the pin.",
    [SERVO_STR_R_ENC_UNMOVED]    = "Unmoved: the angle did not leave %.1f deg "
                                   "of its start.",
    [SERVO_STR_R_ENC_LATE]       = "Late: the angle moved and was not still "
                                   "for %u ms before the next command.",
    [SERVO_STR_R_ENC_DEADBAND]   = "Deadband: not measured; the moves go end "
                                   "to end.",
    [SERVO_STR_R_ENC_NONE]       = "No angle reading reached the run.",
    [SERVO_STR_R_ENC_NO_MAGNET]  = "No magnet: the AS5600 reported none %lu "
                                   "time(s). No angle is logged then, and an "
                                   "unsettled move is not counted.",
    [SERVO_STR_R_ENC_FIELD]      = "Field: %lu reading(s) with the magnet too "
                                   "weak, %lu too strong. The angle is used; "
                                   "its noise is not specified then.",
    [SERVO_STR_R_UNM_POSITION_ENC] = "Position: the AS5600 measures the horn; "
                                     "the current's travel time is reported "
                                     "beside the angle's.",

    /* The meter. */
    [SERVO_STR_R_LOG_WIN]        = "Log:            the .CSV with this "
                                   "file's number, one row per INA3221 "
                                   "window",
    [SERVO_STR_R_SUPPLY_PDMINI]  = "PD mini",
    [SERVO_STR_R_CURRENT]        = "Current:        %s",
    [SERVO_STR_R_CURRENT_INA]    = "Current:        %s, shunt %.1f "
                                   "mOhm, range %.3f A",
    [SERVO_STR_R_VOLTAGE]        = "Voltage:        %s",
    [SERVO_STR_R_VOLTAGE_INA]    = "Voltage:        %s, load side "
                                   "of the shunt",
    [SERVO_STR_R_SHUNT]          = "Shunt:          up to %.3f V lost across "
                                   "it at the range; the set point is not "
                                   "raised for it",
    [SERVO_STR_R_INA_UNUSED]     = "INA3221:        not used: %s",
    [SERVO_STR_R_INA_OLD]        = "the IOmcu is older than link "
                                   "protocol 4.11",
    [SERVO_STR_R_INA_NOT_HELD]   = "the IOmcu does not hold its set-up",
    [SERVO_STR_R_INA_SILENT]     = "it does not answer",
    [SERVO_STR_R_INA_NO_WINDOW]  = "no window with current in the last 200 "
                                   "ms",
    [SERVO_STR_R_INA_RESET]      = "it reset itself",
    [SERVO_STR_R_INA_SETTLING]   = "it has worked for less than 1 s",
    [SERVO_STR_R_INA_MODEL]      = "the supply is the panel's model",
    [SERVO_STR_R_READINGS_WIN]   = "Readings:       %.1f /s windows closed by "
                                   "the INA3221, %.1f /s reached the test",
    [SERVO_STR_R_SKIPPED_WIN]    = "Skipped:        %lu windows the INA3221 "
                                   "closed never reached the test",
    [SERVO_STR_R_RESOLUTION_WIN] = "Resolution:     one window every %lu ms: "
                                   "a travel time is late by up to two "
                                   "windows and the poll that reads them",
    [SERVO_STR_R_CLIPPED]        = "Clipped:        %lu windows hold a sample "
                                   "at an end of the range, %.3f A: their "
                                   "figures are a lower bound",
    [SERVO_STR_R_CC]             = "Const. current: %lu supply readings, "
                                   "longest stretch %lu ms",
    [SERVO_STR_R_NEGATIVE]       = "Current reads negative at rest: shunt "
                                   "direction",
    [SERVO_STR_R_LIM_STALL_LIMIT] = "STALL AT %.2f A cannot be reached: "
                                    "current limit %.2f A",
    [SERVO_STR_R_LIM_STALL_RANGE] = "STALL AT %.2f A cannot be reached: "
                                    "INA3221 range %.3f A",
    [SERVO_STR_R_UNM_POSITION_INA] = "Position: nothing measures the horn; "
                                     "every result is the current of the "
                                     "INA3221 channel named above.",
    [SERVO_STR_R_UNM_PEAKS_INA]  = "Current between two 1 ms samples of that "
                                   "channel: a window holds their mean, "
                                   "highest and lowest.",
    [SERVO_STR_R_METER_MODEL]    = "the panel's model",

    /* The limit run.  Its reasons are a line of the SERVO screen's box
     * too: 31 characters at most. */
    [SERVO_STR_L_FOUND]          = "FOUND",
    [SERVO_STR_L_FREE]           = "FREE",
    [SERVO_STR_L_PH_SEARCH]      = "SEARCH",
    [SERVO_STR_L_PH_SAFE]        = "SAFE",
    [SERVO_STR_L_PH_CONNECT]     = "CONNECT",
    [SERVO_STR_L_PH_WRITE]       = "WRITE",
    [SERVO_STR_L_PH_LEAVE]       = "LEAVE",
    [SERVO_STR_L_PH_ARM]         = "ARM",
    [SERVO_STR_L_PH_PROBE]       = "PROBE",
    [SERVO_STR_L_PH_CHECK]       = "CHECK",
    [SERVO_STR_L_WHY_HARD]       = "a sample at 3.0 A or more",
    [SERVO_STR_L_WHY_CLIPPED]    = "INA3221 at the end of its range",
    [SERVO_STR_L_WHY_STALL]      = "above 1.0 A for 400 ms",
    [SERVO_STR_L_WHY_SEARCH_LONG] = "a search took longer than 40 s",
    [SERVO_STR_L_WHY_SUPPLY]     = "supply or rail voltage changed",
    [SERVO_STR_L_WHY_BENCH]      = "bench did not switch in 3 s",
    [SERVO_STR_L_WHY_NOT_KST]    = "no KST or Chaservo servo read",
    [SERVO_STR_L_WHY_PROG]       = "programming port did not finish",
    [SERVO_STR_L_WHY_WRITE]      = "a write refused or lost",
    [SERVO_STR_L_WHY_OTHER]      = "servo holds other values",
    [SERVO_STR_L_WHY_MAPPING]    = "LEFT/RIGHT ANGLE side not found",
    [SERVO_STR_L_WHY_SMALLEST]   = "binds at the smallest angle",
    [SERVO_STR_L_WHY_CAP]        = "write cap of 8 values reached",
    [SERVO_STR_L_WHY_STILL]      = "an end binds after programming",
    [SERVO_STR_L_WHY_NO_VOLTAGE] = "no rail voltage in a window",
    [SERVO_STR_L_WHY_NO_STOP]    = "no stop after the overset write",
    [SERVO_STR_L_WHY_BEFORE_NEUTRAL] = "free end not past NEUTRAL",
    [SERVO_STR_L_WHY_NEUTRAL]    = "NEUTRAL outside LOWER/UPPER",
    [SERVO_STR_L_WHY_RANGE]      = "servo pulses outside MIN/MAX",
    [SERVO_STR_L_R_TITLE]        = "RCBENCH SERVO LIMIT REPORT",
    [SERVO_STR_L_R_MODE_SEARCH]  = "Mode:           search only, nothing "
                                   "written to the servo",
    [SERVO_STR_L_R_MODE_PROGRAM] = "Mode:           search, then LEFT ANGLE "
                                   "and RIGHT ANGLE programmed",
    [SERVO_STR_L_R_MODE_PULSES]  = "Mode:           search only; PULSE LOWER "
                                   "and PULSE UPPER set to 1000 and 2000 us "
                                   "first",
    [SERVO_STR_L_R_SEARCH]       = "Searched:       from %u us out to %u us "
                                   "and %u us",
    [SERVO_STR_L_R_OUTPUT]       = "Output:         %s, %u Hz",
    [SERVO_STR_L_R_PULSE]        = "Pulse range:    centre %u us, %u-%u us",
    [SERVO_STR_L_R_METER]        = "Current:        %s, 50 ms "
                                   "windows, shunt %.1f mOhm",
    [SERVO_STR_L_R_RULE]         = "Search rule:    10 us steps, settle 120 "
                                   "ms, mean of 80 ms; bound above %.1f x "
                                   "free and +%.2f A",
    [SERVO_STR_L_R_PROTECT]      = "Protections:    a sample at %.1f A, above "
                                   "%.1f A for %u ms, no window for %u ms, "
                                   "%u s a search",
    [SERVO_STR_L_R_ENDS_HEAD]    = "ENDS (the search before programming)",
    [SERVO_STR_L_R_CHECK_HEAD]   = "ENDS AFTER PROGRAMMING (both searched "
                                   "again)",
    [SERVO_STR_L_R_LOW]          = "Low end:        ",
    [SERVO_STR_L_R_HIGH]         = "High end:       ",
    [SERVO_STR_L_R_END_BOUND]    = "%sbound at %u us, endpoint %u us; free "
                                   "%.3f A, bound %.3f A",
    [SERVO_STR_L_R_END_FREE]     = "%sfree to %u us; free %.3f A",
    [SERVO_STR_L_R_END_NOT_RUN]  = "%snot searched",
    [SERVO_STR_L_R_ANGLE]        = "                AS5600 %+.1f deg at the "
                                   "last free position",
    [SERVO_STR_L_R_ANGLE_NONE]   = "                AS5600: no angle at the "
                                   "last free position",
    [SERVO_STR_L_R_ENC_OFF]      = "AS5600:         not fitted, no angles",
    [SERVO_STR_L_R_PROG_HEAD]    = "PROGRAMMING (LEFT ANGLE and RIGHT ANGLE, "
                                   "unit not known)",
    [SERVO_STR_L_R_PROG_OFF]     = "Not run: search only.",
    [SERVO_STR_L_R_PROG_FREE]    = "Not run: neither end binds.",
    [SERVO_STR_L_R_PROG_NO_SERVO] = "No servo read: nothing written.",
    [SERVO_STR_L_R_SERVO]        = "Servo:          type %u, REVERSION %s",
    [SERVO_STR_L_R_PULSES]       = "Servo pulses:   NEUTRAL %.1f us, PULSE "
                                   "LOWER %.1f us, PULSE UPPER %.1f us",
    [SERVO_STR_L_R_PULSE_SET]    = "Pulse write:    PULSE LOWER %.1f -> %.1f "
                                   "us, PULSE UPPER %.1f -> %.1f us",
    [SERVO_STR_L_R_RANGE]        = "Range:          servo pulses %.1f-%.1f us "
                                   "outside PULSE MIN/MAX %u-%u us",
    [SERVO_STR_L_R_LEFT]         = "LEFT ANGLE:     ",
    [SERVO_STR_L_R_RIGHT]        = "RIGHT ANGLE:    ",
    [SERVO_STR_L_R_REG]          = "%s%u before, %u after; %s",
    [SERVO_STR_L_R_REG_LIN]      = "                share free %s, bound %s; "
                                   "overset %s, share %s; final %s",
    [SERVO_STR_L_R_SIDE_LO]      = "limits the low end",
    [SERVO_STR_L_R_SIDE_HI]      = "limits the high end",
    [SERVO_STR_L_R_SIDE_UNKNOWN] = "end it limits not found",
    [SERVO_STR_L_R_METHOD]       = "Method:         linear in the pulse; "
                                   "overset +%u %% on the share at the first "
                                   "bound pulse, then the share its search "
                                   "found free",
    [SERVO_STR_L_R_WRITES]       = "Writes:         values written %u, writes "
                                   "sent %u, cap %u values; sessions %u",
    [SERVO_STR_L_R_BACKUP]       = "Backup:         %s, the values before "
                                   "the run",
    [SERVO_STR_L_R_BACKUP_MEM]   = "Backup:         the panel's memory only, "
                                   "no card",
    [SERVO_STR_L_R_RESTORE]      = "Restore:        RESTORE on the programming "
                                   "page, from the values before the limit run",
    [SERVO_STR_L_R_UNIT]         = "Unit: the unit of LEFT ANGLE and RIGHT "
                                   "ANGLE is not known; the travel is taken "
                                   "as linear in the pulse from NEUTRAL to "
                                   "PULSE LOWER and PULSE UPPER, which is "
                                   "not measured.",
    [SERVO_STR_L_R_BENCH]        = "Bench: the ends found are not applied to "
                                   "PULSE MIN and PULSE MAX.",
};

const char *servo_str_in(const char *const *table, servo_str_t id)
{
    if ((unsigned)id >= (unsigned)SERVO_STR_COUNT) {
        return "";
    }
    if (table != NULL && table[id] != NULL) {
        return table[id];
    }
    return (k_str[id] != NULL) ? k_str[id] : "";
}

const char *servo_str(servo_str_t id)
{
    return servo_str_in(NULL, id);
}

servo_str_t servo_test_phase_str(servo_test_phase_t ph)
{
    if ((unsigned)ph > (unsigned)SERVO_TEST_PH_HOLD) {
        return SERVO_STR_PHASE_NONE;
    }
    return (servo_str_t)(SERVO_STR_PHASE_NONE + (int)ph);
}

servo_str_t servo_test_abort_str(servo_test_abort_t why)
{
    if ((unsigned)why >= (unsigned)SERVO_TEST_AB_COUNT) {
        return SERVO_STR_AB_NONE;
    }
    return (servo_str_t)(SERVO_STR_AB_NONE + (int)why);
}

servo_str_t servo_test_verdict_str(servo_test_verdict_t v)
{
    switch (v) {
    case SERVO_TEST_PASS: return SERVO_STR_PASS;
    case SERVO_TEST_FAIL: return SERVO_STR_FAIL;
    case SERVO_TEST_NOT_MEASURABLE: return SERVO_STR_NOT_MEASURABLE;
    default:              return SERVO_STR_ABORTED;
    }
}

static servo_str_t servo_test_start_str(servo_test_start_t why)
{
    if ((unsigned)why > (unsigned)SERVO_TEST_START_BAD_ENDS) {
        return SERVO_STR_START_OK;
    }
    return (servo_str_t)(SERVO_STR_START_OK + (int)why);
}

const char *servo_test_phase_name(servo_test_phase_t ph)
{
    return servo_str(servo_test_phase_str(ph));
}

const char *servo_test_abort_name(servo_test_abort_t why)
{
    return servo_str(servo_test_abort_str(why));
}

const char *servo_test_verdict_name(servo_test_verdict_t v)
{
    return servo_str(servo_test_verdict_str(v));
}

const char *servo_test_start_name(servo_test_start_t why)
{
    return servo_str(servo_test_start_str(why));
}

/* The columns every run has, the encoder's, and the meter's: the last
 * group follows whatever is before it, so a column of an older file keeps
 * its place. */
#define CSV_BASE  "time (s);test;step;phase;command (us);position (us);" \
                  "set (V);voltage (V);limit (A);current (A);power (W);" \
                  "mode;travel (ms)"
#define CSV_ENC   ";angle (deg);travel angle (ms)"
#define CSV_METER ";meter;window;current max (A);current min (A);" \
                  "voltage min (V);clipped"

const char *servo_test_csv_header(void)
{
    return CSV_BASE CSV_METER;
}

const char *servo_test_csv_header_enc(void)
{
    return CSV_BASE CSV_ENC CSV_METER;
}

const char *servo_test_meter_word(uint8_t kind)
{
    switch ((servo_test_meter_kind_t)kind) {
    case SERVO_TEST_METER_INA3221: return "INA3221";
    case SERVO_TEST_METER_MODEL:   return "MODEL";
    default:                       return "PDMINI";
    }
}
