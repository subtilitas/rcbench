idf_component_register(
    SRCS "can_twai.c"
    INCLUDE_DIRS "include"
    REQUIRES board can link driver
)

# This component drives the legacy TWAI (Two-Wire Automotive Interface)
# driver.  Its header driver/twai.h carries a #warning from ESP-IDF v6.0 on,
# and the build turns warnings into errors.  -Wno-cpp drops the directive's
# message for this component only, so the build log holds no warning.  The
# replacement, esp_twai.h, changes the error and bus-off recovery calls the
# IOmcu link relies on and needs a test on the CAN bus.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-cpp)
