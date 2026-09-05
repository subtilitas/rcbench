# The panel's cache of board photographs.  Compiled by the panel, which owns
# the flash it lives in, and by the host suite, which runs the same store
# against memory -- the flash is passed in so the order of a write can be
# interrupted on purpose and the result inspected.
set(SRCS art_store.c art_fetch.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES link)
else()
    add_library(rcbench_artwork STATIC ${SRCS})
    target_include_directories(rcbench_artwork PUBLIC include)
    target_link_libraries(rcbench_artwork PUBLIC rcbench_link)
endif()
