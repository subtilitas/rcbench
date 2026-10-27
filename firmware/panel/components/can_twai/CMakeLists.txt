idf_component_register(
    SRCS "can_twai.c"
    INCLUDE_DIRS "include"
    REQUIRES board can link driver
)

# The legacy TWAI (Two-Wire Automotive Interface) driver's header
# driver/twai.h carries a #warning from ESP-IDF v6.0 on, and the build turns
# warnings into errors.  This component keeps the legacy driver; the warning
# stays in the log and does not stop the build.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cpp)
