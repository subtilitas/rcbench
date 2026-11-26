# Reading logs off the card.  The number parser decides a file's decimal
# convention from every value in it and rejects values that do not conform:
# stripping the dot out of German 1.234,56 and reading it as English gives
# 1.23456.
#
# The CSV (comma-separated values) reader never holds the file: the source is
# a read/rewind callback, analysis is two streaming passes, and only the
# columns being plotted become arrays.
#
# A run log's name carries its order.  The board has no clock that survives a
# power cycle, so the number in MOTORnnn.CSV, ATESTnnn.CSV and the rest is
# the only age on the card; the logger writes that name and the viewer reads
# it back, both through log_name.c.  log_take.c takes a run's number in its
# folder, card_tree.h names the folders, card_tree.c makes them and checks a
# log folder set on SETUP, and fault_log.c keeps the fault log under its
# 1MiB cap.  log_info.c writes and reads the `info` cell, a run's own
# description on the first data row of its file; its date follows the wall
# clock's rules (shared/clock).  log_marks.c reads the traces and markers
# of a LIMIT RUN's and a RUDDER AUTO's file for LOGS' graph.
#
# A labelled servo files its runs in a folder of its own, Servos/<id>:
# servo_id.c checks, builds and counts the ids, servo_name.c names the runs
# in the folder, servo_active.c holds the servo the next run files under,
# servo_take.c numbers the runs in a servo's folder, and servo_trend.c
# hears each run filed there.
set(SRCS
    log_numbers.c
    log_csv.c
    log_reader.c
    log_fields.c
    log_preset.c
    log_marks.c
    log_name.c
    log_info.c
    log_take.c
    card_tree.c
    fault_log.c
    servo_store.c
    servo_id.c
    servo_name.c
    servo_active.c
    servo_take.c
    servo_trend.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           PRIV_REQUIRES clock)
else()
    add_library(rcbench_logfile STATIC ${SRCS})
    target_include_directories(rcbench_logfile PUBLIC include)
    target_link_libraries(rcbench_logfile PUBLIC m PRIVATE rcbench_clock)
endif()
