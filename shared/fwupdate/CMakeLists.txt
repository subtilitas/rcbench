# The panel's update of its own application: the image checks, receiving an
# image into memory, the write into the slot that is not running, and what
# GitHub says about the releases (fw_release.c), when a first start is
# kept (fw_trial.c), and the C6 radio's update from the image the panel
# carries (fw_c6.c).  The IOmcu's two update slots on the RP2350 boot ROM:
# which slot runs and which one an update writes, the trial and the BUY
# (fw_slots.c).
# Compiled by the panel, which owns the flash, by the IOmcu, which links
# fw_slots.c only, and by the host suite, which runs the same sequence
# against memory.  The ESP-IDF calls are firmware/panel/components/fw_ota;
# the boot ROM calls are firmware/iomcu/src/update_hw.c.
set(SRCS fw_sha256.c fw_image.c fw_upload.c fw_install.c fw_boot.c fw_trial.c
         fw_release.c fw_c6.c fw_slots.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES safety link)
else()
    add_library(rcbench_fwupdate STATIC ${SRCS})
    target_include_directories(rcbench_fwupdate PUBLIC include)
    target_link_libraries(rcbench_fwupdate PUBLIC rcbench_safety rcbench_link)
endif()
