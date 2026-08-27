# The bench's own model: what the numbers mean, where they come from, and the
# rules for asking a motor to spin.
#
# bench_state is the seam the two-processor split turns on -- the screens see
# it and never a register, so the same screen renders measured and modelled
# numbers without knowing which it has.
set(SRCS
    bench_state.c
    telemetry_sim.c
    log_writer.c
    throttle.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_bench STATIC ${SRCS})
    target_include_directories(rcbench_bench PUBLIC include)
    target_link_libraries(rcbench_bench PUBLIC rcbench_link m)
endif()
