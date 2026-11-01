# ESC (electronic speed controller) settings over the signal wire, without
# hardware: the wire transaction, protocol A's frames, the guard every frame
# passes, identification, the field maps, the session and the settings file.
# The sources include the C standard headers and each other, nothing else, so
# the folder builds in any project.  A pin driver implements ew_driver_t.
set(SRCS ew_wire.c ew_a.c ew_map.c ew_map_s.c ew_map_bj.c ew_map_am.c
         ew_id.c ew_guard.c ew_session.c ew_file.c ew_file_read.c
         ew_file_load.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_escwire STATIC ${SRCS})
    target_include_directories(rcbench_escwire PUBLIC include)
endif()
