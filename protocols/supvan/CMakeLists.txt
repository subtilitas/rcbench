# The print protocol of Supvan T50-series thermal label printers: the
# command and answer frames, the status and material record, the print
# buffers and data frames, the print job's machine and a literal-only LZMA
# encoder.  The sources include the C standard headers and each other,
# nothing else, so the folder builds in any project: the panel and the host
# suite.  No pin driver and no radio code.
set(SRCS lzlit.c supvan_frame.c supvan_page.c supvan_job.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_supvan STATIC ${SRCS})
    target_include_directories(rcbench_supvan PUBLIC include)
endif()
