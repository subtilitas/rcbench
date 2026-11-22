# The panel's update of its own application: the image checks, receiving an
# image into memory, the write into the slot that is not running, and what
# GitHub says about the releases (fw_release.c), when a first start is
# kept (fw_trial.c), the note that an update run goes on after a restart
# (fw_run.c), and the C6 radio's update from the image the panel
# carries (fw_c6.c).  The IOmcu's two update slots on the RP2350 boot ROM:
# which slot runs and which one an update writes, the trial and the BUY
# (fw_slots.c).  The verdict on an image's release signature and what the
# panel does with it (fw_sig.c), and the verification itself with Mbed TLS
# (fw_sign.c), which the host suite builds as a library of its own so that
# only the suites that verify link Mbed TLS.
# Compiled by the panel, which owns the flash, by the IOmcu, which links
# fw_slots.c only, and by the host suite, which runs the same sequence
# against memory.  The ESP-IDF calls are firmware/panel/components/fw_ota;
# the boot ROM calls are firmware/iomcu/src/update_hw.c.
set(SRCS fw_sha256.c fw_image.c fw_upload.c fw_install.c fw_boot.c fw_run.c
         fw_trial.c fw_release.c fw_c6.c fw_slots.c fw_sig.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} fw_sign.c INCLUDE_DIRS include
                           REQUIRES safety link mbedtls)
else()
    add_library(rcbench_fwupdate STATIC ${SRCS})
    target_include_directories(rcbench_fwupdate PUBLIC include)
    target_link_libraries(rcbench_fwupdate PUBLIC rcbench_safety rcbench_link)
    # Only where the build has Mbed TLS (the host suite); the IOmcu links
    # fw_slots.c and has none.
    if(TARGET tfpsacrypto)
        add_library(rcbench_fwsign STATIC fw_sign.c)
        target_link_libraries(rcbench_fwsign PUBLIC rcbench_fwupdate
                              tfpsacrypto)
    endif()
endif()
