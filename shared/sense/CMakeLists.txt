# Current monitors on an I2C (Inter-Integrated Circuit) bus: the INA228 in
# the ESC (electronic speed controller) power path and the INA3221 on the
# servo rail.  Register codecs, drivers and the sampling schedule in pure C,
# talking through a read and a write callback, so the host suite runs them
# against a fake that answers as each part does.  The schedule's move
# capture judges moves by servo_move's rules.  Both images compile them,
# through shared/outputs, whose SENSE page validates an INA228 set-up with
# ina228_calibrate(); nothing else of them is called yet.
#
# The tone detector, tone.c, turns the edges of one ESC motor phase into
# beeps and their pitch.  Nothing calls it yet either.
set(SRCS sense_bus.c ina228.c ina3221.c sense_sched.c tone.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES servo)
else()
    add_library(rcbench_sense STATIC ${SRCS})
    target_include_directories(rcbench_sense PUBLIC include)
    target_link_libraries(rcbench_sense PUBLIC rcbench_servo)
endif()
