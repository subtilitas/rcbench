# The panel's wall clock rules: whether the clock holds a real date, whether
# a file's FAT (File Allocation Table) stamp says anything, the time zones
# SETUP offers as POSIX TZ strings, the console line of an NTP (Network Time
# Protocol) answer and the one request a join makes.  No clock, network or
# card is read here; the panel's end is components/wifi_access/wifi_ntp.c
# and main/panel_settings.c, and the host suite holds every rule.
set(SRCS wall_clock.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_clock STATIC ${SRCS})
    target_include_directories(rcbench_clock PUBLIC include)
endif()
