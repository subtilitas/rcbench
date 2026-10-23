# The file manager's and the text editor's core: no card access, no screen.
# text_buf.c holds a text file in memory as a gap buffer with an index of
# its lines, at most 1 MiB and 65,536 line ends.  text_hl.c classes one
# line of a text for the viewer's colours.  fm_name.c holds the rules for
# the names a person gives files and folders and builds the paths.  Every
# buffer is the caller's, so the host suite runs each with small buffers.
set(SRCS
    text_buf.c
    text_hl.c
    fm_name.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_files STATIC ${SRCS})
    target_include_directories(rcbench_files PUBLIC include)
endif()
