# The bench's own model: what the numbers mean and where they come from.
#
# Arming, slew and the silence timeout live in shared/outputs, not here: they
# apply to every output, not only to a throttle.
#
# bench_state is the seam of the two-processor split: the screens see it and
# never a register, so one screen renders measured and modelled numbers
# without knowing which it has.  The IOmcu fills it from the ESC's
# DShot telemetry in the units of protocols/dshot (bench_state_from_telem()).
set(SRCS
    bench_state.c
    telemetry_sim.c
    log_writer.c
    log_cols.c
    log_cadence.c
    perf_watch.c
    supply.c
    supply_link.c
    sense_link.c
    sense_link_view.c
    sense_map_link.c
    sense_win_link.c
    sense_limit_link.c
    servo_source.c
    tone_link.c
    endpoints_link.c
    hitec_rx_link.c
    kst_link.c
    kst_prog.c
    kst_prog_card.c
    kst_prog_file.c
    prog_link.c
    knob.c
    panel_i2c.c
)

# kst_card.c stands between the programmer and the card layer in
# shared/logfile.  It is built where that layer is: on the panel and on the
# host, which adds shared/logfile before this directory.  The IOmcu has
# no card and builds this directory without it.
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} kst_card.c INCLUDE_DIRS include
                           REQUIRES link kst dshot logfile)
else()
    add_library(rcbench_bench STATIC ${SRCS})
    target_include_directories(rcbench_bench PUBLIC include)
    target_link_libraries(rcbench_bench PUBLIC rcbench_link rcbench_kst
                          rcbench_dshot m)
    if(TARGET rcbench_logfile)
        target_sources(rcbench_bench PRIVATE kst_card.c)
        target_link_libraries(rcbench_bench PUBLIC rcbench_logfile)
    endif()
endif()
