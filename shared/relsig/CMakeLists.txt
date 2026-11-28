# The release signature's raw form, the one a device takes over Bluetooth
# LE (protocols/bleota, protocol 2), and its verification over a SHA-256
# with Mbed TLS.  Compiled by the Dial and the nodes (firmware/devota),
# which verify, by the panel, which turns the published DER form into the
# raw one, and by the host suite, which builds it as a library of its own
# so that only the suites that verify link Mbed TLS.
if(ESP_PLATFORM)
    idf_component_register(SRCS relsig.c INCLUDE_DIRS include
                           PRIV_REQUIRES mbedtls)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror
                           -Wshadow)
else()
    if(TARGET tfpsacrypto)
        add_library(rcbench_relsig STATIC relsig.c)
        target_include_directories(rcbench_relsig PUBLIC include)
        target_link_libraries(rcbench_relsig PUBLIC tfpsacrypto)
    endif()
endif()
