# A firmware image over BLE from a central to a peripheral: the service's
# control writes, notifications and chunks with their bounds, and the
# device's side of a transfer (the checks before the first erase, the
# confirmation, the credit window, the SHA-256 compare, the timeouts).  The
# sources include the C standard headers and each other, nothing else, so
# the folder builds in the panel, the Dial's and the nodes' firmware and the
# host suite.  No pin driver and no radio code.
set(SRCS bleota.c bleota_dev.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_bleota STATIC ${SRCS})
    target_include_directories(rcbench_bleota PUBLIC include)
endif()
