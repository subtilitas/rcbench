# The panel's update of its own application: the image checks, receiving an
# image into memory, the write into the slot that is not running, and what
# GitHub says about the releases (fw_release.c), when a first start is
# kept (fw_trial.c), and the C6 radio's update from the image the panel
# carries (fw_c6.c).
# Compiled by the panel, which owns the flash, and by the host suite, which
# runs the same sequence against memory.  The ESP-IDF calls are
# firmware/panel/components/fw_ota.
set(SRCS fw_sha256.c fw_image.c fw_upload.c fw_install.c fw_boot.c fw_trial.c
         fw_release.c fw_c6.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES safety)
else()
    add_library(rcbench_fwupdate STATIC ${SRCS})
    target_include_directories(rcbench_fwupdate PUBLIC include)
    target_link_libraries(rcbench_fwupdate PUBLIC rcbench_safety)
endif()
