# The bench's own model: what the numbers mean and where they come from.
#
# The rules for asking a motor to spin used to be here too, in throttle.c.
# They are in shared/outputs now, because they were never only a throttle's:
# arming, slew and refusing to hold a command nobody is watching are the same
# question for every output, and having them here meant the servo page had to
# answer them again and answered differently.
#
# bench_state is the seam the two-processor split turns on -- the screens see
# it and never a register, so the same screen renders measured and modelled
# numbers without knowing which it has.
set(SRCS
    bench_state.c
    telemetry_sim.c
    log_writer.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES link)
else()
    add_library(rcbench_bench STATIC ${SRCS})
    target_include_directories(rcbench_bench PUBLIC include)
    target_link_libraries(rcbench_bench PUBLIC rcbench_link m)
endif()
