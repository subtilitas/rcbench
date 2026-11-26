# The generic label printer engine: the printer file reader, templates, the
# image stream and the print job.  The sources include the C standard
# headers and each other, nothing else, so the folder builds in any
# project: the panel and the host suite.  No pin driver and no radio code.
set(SRCS prn_parse.c prn_parse_img.c prn_parse_ext.c prn_check.c prn_tmpl.c
         prn_image.c prn_job.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_prnengine STATIC ${SRCS})
    target_include_directories(rcbench_prnengine PUBLIC include)
endif()
