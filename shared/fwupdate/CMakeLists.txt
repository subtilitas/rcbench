# The panel's update of its own application: the image checks, receiving an
# image into memory, and the write into the slot that is not running.
# Compiled by the panel, which owns the flash, and by the host suite, which
# runs the same sequence against memory.  The ESP-IDF calls are
# firmware/panel/components/fw_ota.
set(SRCS fw_sha256.c fw_image.c fw_upload.c fw_install.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES safety)
else()
    add_library(rcbench_fwupdate STATIC ${SRCS})
    target_include_directories(rcbench_fwupdate PUBLIC include)
    target_link_libraries(rcbench_fwupdate PUBLIC rcbench_safety)
endif()
