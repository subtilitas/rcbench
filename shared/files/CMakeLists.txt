# The file manager's core: the card as a table of functions (fm_io.h), one
# page of a folder kept while it is read (fm_select.c), a pane of the card
# (fm_tree.c), the places the bench keeps and the write gate (fm_rules.c),
# and the jobs that copy, move, rename, delete, make, save and load
# (fm_ops.c).  Nothing here draws or reaches a card: the panel supplies the
# card (firmware/panel), the host suite a card in memory that fails on
# request.
set(SRCS
    fm_select.c
    fm_rules.c
    fm_tree.c
    fm_ops.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES logfile)
else()
    add_library(rcbench_files STATIC ${SRCS})
    target_include_directories(rcbench_files PUBLIC include)
    target_link_libraries(rcbench_files PUBLIC rcbench_logfile)
endif()
