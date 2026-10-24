# Host build of everything in shared/ that the panel and the IOmcu also
# compile.  Nothing here links a vendor SDK (software development kit), so it
# runs anywhere gcc does, which is what gives the coverage number in STATUS.md
# its meaning and lets the link codec be fed corrupt frames without a wire.
cmake_minimum_required(VERSION 3.16)
project(rcbench_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_COVERAGE "Build with gcov instrumentation" OFF)

# -Wconversion is the one that earns its place: this code does arithmetic on
# pixel coordinates and register widths, and a silent narrowing there is a
# wrong number rather than a crash.
set(STRICT -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)

# ASan (AddressSanitizer) and UBSan (UndefinedBehaviorSanitizer), for the
# runtime faults -Werror cannot see: a parser fed a hostile frame reading off
# the end of a buffer, a signed shift overflowing, an unaligned access.  This
# is a separate CI (continuous integration) job from the -Werror one, and it
# drops -Werror: UBSan's instrumentation makes GCC re-issue -Wconversion on
# shift expressions whose casts are correct, and those false positives would
# fail the build before a single test ran.  The warnings print; the enforcing
# -Werror lives in the ordinary job.
#
# These flags are set before the add_subdirectory() calls below, because a
# directory takes its copy of COMPILE_OPTIONS as it is added.  Set after the
# copy, they reach the test executables and none of the shared code those
# tests exercise, which is the half the sanitizers are here for.
option(ENABLE_SANITIZERS "Build the suite under AddressSanitizer + UBSan" OFF)
if(ENABLE_SANITIZERS)
    list(REMOVE_ITEM STRICT -Werror)
    set(SAN -fsanitize=address,undefined -fno-sanitize-recover=all
            -fno-omit-frame-pointer -g)
    add_compile_options(${SAN})
    add_link_options(${SAN})
endif()

set(SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")
# The modules that leave this repository whole: a protocol core and, for
# the IOmcu only, its pin driver (CONTRIBUTING.md, "Where code goes").
set(PROTOCOLS "${CMAKE_CURRENT_SOURCE_DIR}/../../protocols")

# Each module's CMakeLists answers to whichever build system is asking; out
# here that is the add_library() branch.
add_subdirectory("${SHARED}/gfx"      gfx)
add_subdirectory("${SHARED}/touch"    touch)
add_subdirectory("${SHARED}/text"     text)
add_subdirectory("${SHARED}/ui"       ui)
add_subdirectory("${SHARED}/settings" settings)
add_subdirectory("${SHARED}/logfile"  logfile)
add_subdirectory("${SHARED}/files"    files)
add_subdirectory("${SHARED}/link"     link)
add_subdirectory("${SHARED}/artwork"  artwork)
add_subdirectory("${SHARED}/bench"    bench)
add_subdirectory("${SHARED}/outputs"  outputs)
add_subdirectory("${SHARED}/safety"   safety)
add_subdirectory("${SHARED}/servo"    servo)
add_subdirectory("${PROTOCOLS}/openyge"  openyge)
add_subdirectory("${SHARED}/can"      can)
add_subdirectory("${PROTOCOLS}/sbus"     sbus)
add_subdirectory("${PROTOCOLS}/dshot"    dshot)
add_subdirectory("${PROTOCOLS}/ppm"      ppm)
add_subdirectory("${PROTOCOLS}/pdmini"   pdmini)
add_subdirectory("${PROTOCOLS}/phase_tap" phase_tap)
add_subdirectory("${PROTOCOLS}/kst"      kst)
add_subdirectory("${PROTOCOLS}/hitec"    hitec)
add_subdirectory("${SHARED}/esc"      esc)
add_subdirectory("${SHARED}/sense"    sense)
add_subdirectory("${SHARED}/fwupdate" fwupdate)
add_subdirectory("${SHARED}/webaccess" webaccess)
add_subdirectory("${SHARED}/clock"    clock)
# The touch injector's core, a bench tool rather than a module of the
# product: testbench/touch-injector (README there).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../testbench/touch-injector/core"
                 touch_injector)

# The rasteriser counts the pixels its loops visit (gfx_work()), so a suite
# can hold a frame's drawing work to a budget: test_frame_work.  The panel
# builds without it.
target_compile_definitions(rcbench_gfx PUBLIC GFX_WORK_COUNT)

set(UNDER_TEST
    rcbench_gfx
    rcbench_touch
    rcbench_ui
    rcbench_settings
    rcbench_logfile
    rcbench_files
    rcbench_link
    rcbench_bench
    rcbench_outputs
    rcbench_artwork
    rcbench_safety
    rcbench_servo
    rcbench_openyge
    rcbench_can
    rcbench_sbus
    rcbench_dshot
    rcbench_ppm
    rcbench_pdmini
    rcbench_phase_tap
    rcbench_esc
    rcbench_sense
    rcbench_kst
    rcbench_hitec
    rcbench_fwupdate
    rcbench_webaccess
    rcbench_clock
    rcbench_touch_injector
)

foreach(lib ${UNDER_TEST})
    target_compile_options(${lib} PRIVATE ${STRICT})
    if(ENABLE_COVERAGE)
        target_compile_options(${lib} PRIVATE --coverage -O0 -g)
        target_link_options(${lib} PUBLIC --coverage)
    endif()
endforeach()

# No fakes: the CAN (Controller Area Network) loopback test runs the real host
# and the real device against each other over a bus this suite controls.
set(FAKES "")

# Every test is stopped after 120s unless it sets its own TIMEOUT.  CTest
# reads the default from DartConfiguration.tcl, which include(CTest) writes
# from DART_TESTING_TIMEOUT; the ctest default without it is 1500s.
set(DART_TESTING_TIMEOUT 120 CACHE STRING "Default timeout of a test, in s")
include(CTest)

foreach(suite gfx gfx_faces gfx_subpixel gfx_clip gfx_damage gfx_turn layout touch_map overlays nav nav_tiles nav_alert nav_mark nav_contact nav_drag nav_stop nav_bubble widgets workbench prog_registry keypad keypad_sign keypad_rows_setup keypad_rows_stick keypad_rows_motor keypad_rows_supply keypad_rows_servo keypad_rows_kst pinlist bench supply supply_screen supply_screen_tasks supply_screen_home supply_screen_draw supply_layout pdmini motor motor_draw motor_throttle motor_arm motor_workbench motor_program motor_layout value_state logview logview_report log_text logview_plot logview_rank logview_pages logview_tabs logview_delete logview_touch logview_layout log_name log_take card_tree fm_tree fm_ops fm_recheck fault_log logwriter servo_runlog servo_runlog_cols servo_log_figures log_cadence perf_watch log_cols settings settings_screen settings_gesture settings_pins_hub settings_leave settings_folders settings_sensors settings_keypad settings_rows setting_keypad_eq log_numbers log_reader log_csv link_crc link_can link_pages link_watchdog link_loopback link_bringup link_artxfer link_drawing art_store art_fetch outputs outputs_hw outputs_hw_ports outstore heartbeat arming safety_gate flash_gate reset_cause touch_loss frame_watch servo_arm safe_press servo_limit servo_sync servo_sweep servo_move servo_test servo_test_judge servo_test_replay servo_test_enc servo_test_win servo_test_win_read servo_test_win_chain servo_page supply_page sense_page supply_link sense_link sense_link_view sense_map_link sense_win_link sense_limit_link sense_windows servo_source openyge_frame openyge_status openyge_params can_timing can_selftest mcp2515 sbus dshot_frame dshot_telem dshot_edt ppm outbind outputs_screen outputs_screen_draw outputs_screen_off outputs_screen_port outputs_layout picker_screen picker_screen_hold picker_screen_table picker_layout port_screens board_drawing busfault_screen text servo servo_knob servo_drag servo_repaint frame_work servo_dial_sweep servo_letgo servo_curve servo_supply servo_run servo_target servo_rail servo_meter servo_settings servo_trim_keypad servo_test_panel servo_limit_panel servo_workbench analyser prog_models esc_stick_panel esc_stick_panel_phases esc_stick_panel_list esc_stick_panel_steps esc_stick_panel_makers esc_stick_panel_hold esc_stick_panel_touch esc_stick_panel_home esc_stick_panel_keypad esc_stick_layout balance battery instruments_layout esc_profiles esc_det esc_stick_rules esc_stick esc_stick_hand esc_stick_entry ina228 ina3221 as5600 sense_sched sense_parts sense_map sense_limit sense_svc sense_trace sense_trace_feed sense_trace_next sense_trace_setup sense_trace_lines tone edge_ring tone_svc tone_page tone_pio tone_link endpoints_link hitec_rx_link knob panel_i2c bind_link servo_link fuzz_engines kst_wire kst_reg kst_plan kst_session kst_pio kst_port kst_link prog_port prog_port_fn kst_file kst_store kst_card prog_link prog_link_ask port_mail kst_prog kst_prog_card kst_panel kst_panel_leave kst_panel_edit kst_panel_write kst_panel_texts kst_panel_writepage kst_panel_screen kst_panel_notes kst_panel_files kst_panel_nocard kst_panel_limrun kst_panel_ports kst_panel_layout hitec_wire hitec_pio hitec_pre hitec_port hitec_port_stop hitec_port_rx page_console wifi_screen wifi_layout fw_image fw_install fw_trial fw_upload fw_release fw_release_scan wa_session wa_scan wa_fetch wa_fetch_channel wa_fetch_task upd_screen wa_text wa_walk wa_send wa_zip wa_page wa_page_de wa_mdns link_sha256 link_update coproc_update_screen upd_coproc upd_layout touch_merge touch_merge_multi inj_line_id inj_emu inj_multi servo_limrun servo_limrun_abort servo_limrun_report sense_i2c storage storage_swap text_buf text_hl text_hl_limrun fm_name fm_req files_screen files_ops files_layout wall_clock)
    add_executable(test_${suite} test_${suite}.c)
    target_link_libraries(test_${suite} PRIVATE ${UNDER_TEST})
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    # The libraries' flags, and with them -Werror in the ordinary job.  A
    # case that is defined and not in main()'s RUN list is a static function
    # nothing calls: an error in this job and, through the extra flag, in the
    # sanitizer one, which drops -Werror.
    target_compile_options(test_${suite} PRIVATE ${STRICT}
                           -Werror=unused-function)
    add_test(NAME ${suite} COMMAND test_${suite})
    # The CSV (comma-separated values) fixtures are the corpus the log parser
    # is held to; the suite finds them relative to its own source directory,
    # not the build tree.
    set_tests_properties(${suite} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
# The board drawing the IOmcu image carries, generated by
# tools/gen_board_drawing.py: the suites that draw the picker draw the
# drawing an IOmcu serves.
set(BOARD_DRAWING
    "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/iomcu/src/board_drawing_rp2350_can.c")
foreach(suite board_drawing picker_screen picker_screen_hold picker_screen_table
              picker_layout port_screens)
    target_sources(test_${suite} PRIVATE "${BOARD_DRAWING}")
endforeach()
# The file manager's suites run its core over a card in memory.
foreach(suite fm_tree fm_ops fm_recheck fm_req)
    target_sources(test_${suite} PRIVATE
                   "${CMAKE_CURRENT_SOURCE_DIR}/fm_fake.c")
endforeach()
# FILES' suites run the screen over that card behind a card task on the
# test's own thread (files_host_io.c), and reach its layout through
# files_int.h.
foreach(suite files_screen files_ops files_layout)
    target_sources(test_${suite} PRIVATE
                   "${CMAKE_CURRENT_SOURCE_DIR}/fm_fake.c"
                   "${CMAKE_CURRENT_SOURCE_DIR}/files_host_io.c")
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui")
endforeach()
# test_port_screens holds picker_port.c and the OUTPUTS entry's guards
# through their screen-private headers.
target_include_directories(test_port_screens PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui")
# test_esc_stick_layout holds ESC STICK's controls through the session's
# private seam, esc_stick_panel_int.h.
target_include_directories(test_esc_stick_layout PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/ui")
# The IOmcu's output layer, built from its firmware source over fake
# pin drivers (in the suite) and the two clocks of pico_stub/pico/stdlib.h,
# so test_outputs_hw holds the telemetry ages across the millisecond wrap.
set(IOMCU_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/iomcu/src")
# test_outputs_hw_ports holds the programming ports there the same way.
foreach(suite outputs_hw outputs_hw_ports)
    target_sources(test_${suite} PRIVATE "${IOMCU_SRC}/outputs_hw.c")
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/pico_stub"
        "${IOMCU_SRC}"
        "${PROTOCOLS}/dshot/rp2350"
        "${PROTOCOLS}/ppm/rp2350")
    # As the IOmcu build sets it: slots and programming ports.
    target_compile_definitions(test_${suite} PRIVATE OUT_DSHOT_MAX_PINS=12u)
endforeach()
# The IOmcu's sensor bus wire, built from its firmware source over a
# modelled pair of pads (sense_i2c_stub/), so test_sense_i2c holds what a
# transaction comes to on lines low at idle.
target_sources(test_sense_i2c PRIVATE "${IOMCU_SRC}/sense_i2c.c")
target_include_directories(test_sense_i2c PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/sense_i2c_stub"
    "${IOMCU_SRC}")
# The panel's SD card mount, built from its firmware source over the
# ESP-IDF calls of storage_stub/, so test_storage holds what a mount tried
# again with no card sets up and logs.
set(PANEL_STORAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/panel/components/storage")
# test_storage_swap holds the card taken out, swapped or put in while the
# panel runs over the same slot (storage_stub/storage_fake.c).
foreach(suite storage storage_swap)
    target_sources(test_${suite} PRIVATE "${PANEL_STORAGE}/storage.c"
                   "${PANEL_STORAGE}/storage_list.c"
                   "${CMAKE_CURRENT_SOURCE_DIR}/storage_stub/storage_fake.c")
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/storage_stub"
        "${PANEL_STORAGE}" "${PANEL_STORAGE}/include")
endforeach()
# test_fm_name holds the file manager's name and path limits to the
# storage component's walk (STORAGE_NAME_MAX), from its own header.
target_include_directories(test_fm_name PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/storage_stub" "${PANEL_STORAGE}/include")
# The pins the IOmcu reserves at boot, from its own header, so
# test_settings_pins_hub holds Sensor SDA's values to the page as the
# firmware starts it.  settings_rig.h includes the header, so every
# settings suite finds it.
foreach(suite settings settings_screen settings_gesture settings_pins_hub settings_keypad
              keypad_rows_setup
              settings_leave settings_folders settings_sensors settings_rows
              wall_clock)
    target_include_directories(test_${suite} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/iomcu/include")
endforeach()
# Bench recordings the servo test replays, by an absolute path, so
# test_servo_test_replay and test_servo_test_win read them from whatever
# directory they are started in.
target_compile_definitions(test_servo_test_replay PRIVATE
    "FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\"")
target_compile_definitions(test_servo_test_win PRIVATE
    "FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\"")
# The profiles of record, by an absolute path, so test_esc_profiles reads
# them from whatever directory it is started in.
target_compile_definitions(test_esc_profiles PRIVATE
    "PROFILE_DIR=\"${SHARED}/esc/profiles\"")
# test_text_hl classes a profile of record as JSON, from the same folder.
target_compile_definitions(test_text_hl PRIVATE
    "PROFILE_DIR=\"${SHARED}/esc/profiles\"")
# The phase tap's PIO program, assembled from its source by the test and
# held to the words pioasm gives (the firmware build checks the same file).
target_compile_definitions(test_tone_pio PRIVATE
    "TONE_CAP_PIO=\"${CMAKE_CURRENT_SOURCE_DIR}/../../protocols/phase_tap/rp2350/tone_cap.pio\""
    "TONE_CAP_WORDS=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tone_cap_words.txt\"")
# The KST line's PIO program, the same way.
target_compile_definitions(test_kst_pio PRIVATE
    "KST_LINE_PIO=\"${CMAKE_CURRENT_SOURCE_DIR}/../../protocols/kst/rp2350/kst_line.pio\""
    "KST_LINE_WORDS=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kst_line_words.txt\"")
# The Hitec line's PIO program, the same way.
target_compile_definitions(test_hitec_pio PRIVATE
    "HITEC_LINE_PIO=\"${CMAKE_CURRENT_SOURCE_DIR}/../../protocols/hitec/rp2350/hitec_line.pio\""
    "HITEC_LINE_WORDS=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hitec_line_words.txt\"")

# The ZIP archives test_wa_zip writes, opened by Python's zipfile and by
# unzip(1): an archive the panel sends has to open in programs other than
# the code that wrote it.
target_compile_definitions(test_wa_zip PRIVATE
    "ZIP_OUT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/wa_zip\"")
set_tests_properties(wa_zip PROPERTIES FIXTURES_SETUP wa_zip_archives)

# The pages test_wa_page writes, their script handed to node: a text that
# breaks the script leaves the Upload button without its handler.
target_compile_definitions(test_wa_page PRIVATE
    "PAGE_OUT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/wa_page\"")
set_tests_properties(wa_page PROPERTIES FIXTURES_SETUP wa_page_files)

# The headers of protocols/kst as C++: a driver may be written in it.  The
# program includes all 7, calls one function of each module and links against
# the C library, so a header that is not valid C++ fails the build and a
# missing extern "C" guard fails the link.  Without a C++ compiler the check
# is not built, and the configure step fails when the environment variable CI
# is set.
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
    add_executable(kst_headers_cxx kst_headers.cpp)
    target_link_libraries(kst_headers_cxx PRIVATE rcbench_kst)
    target_compile_options(kst_headers_cxx PRIVATE -Wall -Wextra -Werror)
    set_target_properties(kst_headers_cxx PROPERTIES
        CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    add_test(NAME kst_headers_cxx COMMAND kst_headers_cxx)
    # The headers of protocols/hitec, the same way.
    add_executable(hitec_headers_cxx hitec_headers.cpp)
    target_link_libraries(hitec_headers_cxx PRIVATE rcbench_hitec)
    target_compile_options(hitec_headers_cxx PRIVATE -Wall -Wextra -Werror)
    set_target_properties(hitec_headers_cxx PROPERTIES
        CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    add_test(NAME hitec_headers_cxx COMMAND hitec_headers_cxx)
elseif(DEFINED ENV{CI})
    message(FATAL_ERROR
        "No C++ compiler and CI is set: the KST and Hitec headers cannot be checked")
else()
    message(STATUS "No C++ compiler: the KST and Hitec headers are not checked as C++")
endif()

# tools/sense_trace.py, the host side of the IOmcu's SENSE_TRACE
# build.  sense_trace_replay is the program the tool drives: the move rules
# of shared/servo, linked.  sense_trace_gen writes the console log the
# trace code gives for servo_sim's servo, which is the fixture the tool is
# tested on; the test fails when the checked-in fixture is not what the
# generator writes.
add_executable(sense_trace_replay sense_trace_replay.c)
target_link_libraries(sense_trace_replay PRIVATE ${UNDER_TEST})
add_executable(sense_trace_gen sense_trace_gen.c)
target_link_libraries(sense_trace_gen PRIVATE ${UNDER_TEST})
target_compile_options(sense_trace_replay PRIVATE ${STRICT})
target_compile_options(sense_trace_gen PRIVATE ${STRICT})
target_include_directories(sense_trace_gen PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
# Registered where the tool is: a copy of the tree without tools/, as
# tools/mutate.py makes one, builds the two programs and runs no tool.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/sense_trace.py")
add_test(NAME sense_trace_tool
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_sense_trace_tool.py"
            --replay $<TARGET_FILE:sense_trace_replay>
            --gen $<TARGET_FILE:sense_trace_gen>
            --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            --work "${CMAKE_CURRENT_BINARY_DIR}/sense_trace_tool")
endif()

add_test(NAME wa_zip_check
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/check_wa_zip.py"
            "${CMAKE_CURRENT_BINARY_DIR}/wa_zip")
set_tests_properties(wa_zip_check PROPERTIES
    FIXTURES_REQUIRED wa_zip_archives)

# node reads the script of each page.  Without node on the PATH the test is
# skipped (exit 77); the CI runner image carries node, and a CI step fails
# the job if it does not.
add_test(NAME wa_page_script
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/check_wa_page.py"
            "${CMAKE_CURRENT_BINARY_DIR}/wa_page")
set_tests_properties(wa_page_script PROPERTIES
    FIXTURES_REQUIRED wa_page_files SKIP_RETURN_CODE 77)

# The profile generator against the card reader: fuzz_parity.py changes the
# profiles of record, reads each result with tools/gen_esc_profiles.py and,
# through esc_parse_dump, with esc_profile_parse(), and fails on a file the
# two read differently.  2 fixed seeds of 1500 cases each: 6.2s measured on
# one x86-64 host, 8.1s in the sanitizer build, stopped after 120s.
# Not in the coverage build: the cases follow Python's random module, and
# the figure in STATUS.md is what the C suites reach on any Python.
if(NOT ENABLE_COVERAGE)
    add_executable(esc_parse_dump esc_parse_dump.c)
    target_link_libraries(esc_parse_dump PRIVATE rcbench_esc)
    target_compile_options(esc_parse_dump PRIVATE ${STRICT})
    add_test(NAME esc_parity
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/fuzz_parity.py"
                     $<TARGET_FILE:esc_parse_dump>)
    set_tests_properties(esc_parity PROPERTIES TIMEOUT 120)
endif()
