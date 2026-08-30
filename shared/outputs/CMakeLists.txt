# The intermediary every output goes through: arming, clamping, slew and
# staleness in one place, with a table saying what each protocol is shaped
# like.  Pure logic and no pin, so it is host-testable to the last edge case
# before anything toggles.
set(SRCS outputs.c outputs_pages.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_outputs STATIC ${SRCS})
    target_include_directories(rcbench_outputs PUBLIC include)
    target_link_libraries(rcbench_outputs PUBLIC rcbench_link)
endif()
