# Nayuki's QR Code generator library, C version 1.8.0, unchanged (README.md
# in this folder).  It keeps its own compile flags: -Werror, the warning set,
# coverage and the analysers apply to rcbench code only.
if(ESP_PLATFORM)
    idf_component_register(SRCS qrcodegen.c INCLUDE_DIRS .)
else()
    add_library(rcbench_qrcodegen STATIC qrcodegen.c)
    target_include_directories(rcbench_qrcodegen PUBLIC .)
endif()
