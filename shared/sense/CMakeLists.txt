# Parts on an I2C (Inter-Integrated Circuit) bus: the INA228 current monitor
# in the ESC (electronic speed controller) power path, the INA3221 on the
# servo rail, and the AS5600 magnetic angle sensor on a servo's output
# shaft.  Register codecs, drivers and the sampling schedule in pure C,
# talking through a read and a write callback, so the host suite runs them
# against a fake that answers as each part does.  The schedule's move
# capture judges moves by servo_move's rules.  sense_svc.c is what the
# coprocessor's core 1 runs each 1 ms tick: the schedule driven by core 0's
# orders, and a snapshot of what it read.  Both images compile them,
# through shared/outputs, whose SENSE page validates an INA228 set-up with
# ina228_calibrate() and publishes the snapshot; the coprocessor runs them.
#
# sense_trace.c turns CH1's 1 ms samples into text lines for a debug build
# of the coprocessor (its SENSE_TRACE option).  The panel does not compile
# it; a coprocessor image built without the option does not link it.
set(SRCS sense_bus.c ina228.c ina3221.c as5600.c sense_sched.c sense_svc.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include REQUIRES servo)
else()
    add_library(rcbench_sense STATIC ${SRCS} sense_trace.c)
    target_include_directories(rcbench_sense PUBLIC include)
    target_link_libraries(rcbench_sense PUBLIC rcbench_servo)
endif()
