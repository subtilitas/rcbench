# Current monitors on an I2C (Inter-Integrated Circuit) bus: the INA228 in
# the ESC (electronic speed controller) power path and the INA3221 on the
# servo rail.  Register codecs and drivers in pure C, talking through a
# read and a write callback, so the host suite runs them against a fake
# that answers as each part does.  Both images compile them, through
# shared/outputs, whose SENSE page validates an INA228 set-up with
# ina228_calibrate(); nothing else of them is called yet.
set(SRCS sense_bus.c ina228.c ina3221.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_sense STATIC ${SRCS})
    target_include_directories(rcbench_sense PUBLIC include)
endif()
