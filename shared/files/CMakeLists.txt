# The file manager's and the text editor's core: no card access, no screen.
# text_buf.c holds a text file in memory as a gap buffer with an index of
# its lines, at most 1MiB and 65,536 line ends.  text_hl.c classes one
# line of a text for the viewer's colours.  fm_name.c holds the rules for
# the names a person gives files and folders and builds the paths.  Every
# buffer is the caller's, so the host suite runs each with small buffers.
# The card is a table of functions (fm_io.h): one page of a folder kept
# while it is read (fm_select.c), a pane of the card (fm_tree.c), the
# places the bench keeps and the write gate (fm_rules.c), and the jobs that
# copy, move, rename, delete, make, save and load (fm_ops.c), and the
# runner of the FILES screen's requests on the card's task (fm_req.c).
# png_enc.c encodes a picture row by row and shot_file.c writes it as a
# screenshot.  Nothing here draws or reaches a card: the panel supplies the card
# (firmware/panel), the host suite a card in memory that fails on request.
set(SRCS
    text_buf.c
    text_hl.c
    fm_name.c
    fm_select.c
    fm_rules.c
    fm_tree.c
    fm_ops.c
    fm_req.c
    fm_crc.c
    png_enc.c
    shot_file.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES logfile)
else()
    add_library(rcbench_files STATIC ${SRCS})
    target_include_directories(rcbench_files PUBLIC include)
    target_link_libraries(rcbench_files PUBLIC rcbench_logfile)
endif()
