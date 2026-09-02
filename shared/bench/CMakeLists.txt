# The bench's own model: what the numbers mean and where they come from.
#
# Arming, slew and the silence timeout live in shared/outputs, not here: they
# apply to every output, not only to a throttle.
#
# bench_state is the seam of the two-processor split: the screens see it and
# never a register, so one screen renders measured and modelled numbers
# without knowing which it has.
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
