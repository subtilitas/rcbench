# Reading logs off the card.  The number parser decides a file's decimal
# convention from every value in it and rejects values that do not conform:
# stripping the dot out of German 1.234,56 and reading it as English gives
# 1.23456.
#
# The CSV (comma-separated values) reader never holds the file: the source is
# a read/rewind callback, analysis is two streaming passes, and only the
# columns being plotted become arrays.
set(SRCS
    log_numbers.c
    log_csv.c
    log_fields.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_logfile STATIC ${SRCS})
    target_include_directories(rcbench_logfile PUBLIC include)
    target_link_libraries(rcbench_logfile PUBLIC m)
endif()
