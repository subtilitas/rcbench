# The rasteriser and its three fonts.  Pure C over a block of RGB565 pixels,
# with no vendor SDK in it -- which is what lets the identical code render to
# the panel, to a PNG on a laptop, and into the test suite.
set(SRCS
    gfx.c
    gfx_font8x16.c
    gfx_font16x28.c
    gfx_font_num24x30.c
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
    target_link_libraries(rcbench_gfx PUBLIC m)
endif()
