# The panel's Wi-Fi page and what is behind it: when the radio runs and who
# the page answers, the card paths it serves, the walk of the card, a file
# and the whole card as a ZIP sent a piece at a time, the page itself, the
# panel's mDNS name, the file API that answers a folder's page, an entry's
# facts and a text file through the file manager's core (shared/files), the
# file manager in the page that reads it (web/, carried as C strings in the
# generated wa_web_blob.c), and the panel asking GitHub for a release.
# Compiled
# by the panel and by the host suite; the radio, the HTTP (hypertext
# transfer protocol) server and client and the sockets are
# firmware/panel/components/wifi_access.
set(SRCS wa_session.c wa_text.c wa_walk.c wa_send.c wa_zip.c wa_page.c wa_guard.c
         wa_mdns.c wa_fetch.c wa_api.c wa_app.c wa_web_blob.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include
                           REQUIRES fwupdate files PRIV_REQUIRES text clock)
else()
    add_library(rcbench_webaccess STATIC ${SRCS})
    target_include_directories(rcbench_webaccess PUBLIC include)
    target_link_libraries(rcbench_webaccess PUBLIC rcbench_fwupdate rcbench_files
                          PRIVATE rcbench_text rcbench_clock)
endif()
