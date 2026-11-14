# The INA228 driver and the sensor bus of shared/sense, the IOmcu's own,
# compiled for the power node.  Only these two files: the rest of
# shared/sense (the schedule, the INA3221, the AS5600) is the IOmcu's.
set(SENSE "${CMAKE_CURRENT_LIST_DIR}/../../../../shared/sense")
idf_component_register(SRCS "${SENSE}/sense_bus.c" "${SENSE}/ina228.c"
                       INCLUDE_DIRS "${SENSE}/include")
