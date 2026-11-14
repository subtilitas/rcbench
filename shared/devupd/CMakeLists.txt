# The update of a wireless device (the M5Stack Dial, a sensor node) from
# the image the panel carries, over Bluetooth LE: the transfer engine on
# bleota's service, the panel's client of it on a bleuart machine, and
# SETUP > UPDATES' row for a device.  Compiled by the panel and by the host
# suite, which runs the engine against the device side of
# protocols/bleota; the NimBLE calls are firmware/panel/main/panel_devupd.c.
set(SRCS devupd.c devupd_client.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES bleota bleuart fwupdate)
else()
    add_library(rcbench_devupd STATIC ${SRCS})
    target_include_directories(rcbench_devupd PUBLIC include)
    target_link_libraries(rcbench_devupd PUBLIC rcbench_bleota rcbench_bleuart
                          rcbench_fwupdate)
endif()
