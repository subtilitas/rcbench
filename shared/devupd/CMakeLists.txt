# The update of a wireless device (the M5Stack Dial, a sensor node) from
# the image the panel fetches for it, over Bluetooth LE: the transfer engine on
# bleota's service, the panel's client of it on a bleuart machine, and
# SETUP > UPDATES' row for a device.  Compiled by the panel and by the host
# suite, which runs the engine against the device side of
# protocols/bleota; the NimBLE calls are firmware/panel/main/panel_devupd.c.
# devimg.c names, checks and tracks the images the panel fetches for an
# update (the C6 radio's among them); firmware/panel/main/panel_devimg.c
# fetches and reads them.
set(SRCS devupd.c devupd_client.c devimg.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES bleota bleuart fwupdate rcnode)
else()
    add_library(rcbench_devupd STATIC ${SRCS})
    target_include_directories(rcbench_devupd PUBLIC include)
    target_link_libraries(rcbench_devupd PUBLIC rcbench_bleota rcbench_bleuart
                          rcbench_fwupdate rcbench_rcnode)
endif()
