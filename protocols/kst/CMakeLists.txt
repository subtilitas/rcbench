# KST servo programming protocol, without hardware: frames, the reply
# decoder, the register image, limits, write plans and the session.  The
# sources include the C standard headers and each other, nothing else, so the
# folder builds in any project.  A pin driver implements kst_driver_t.
set(SRCS kst_wire.c kst_reg.c kst_limits.c kst_plan.c kst_session.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_kst STATIC ${SRCS})
    target_include_directories(rcbench_kst PUBLIC include)
endif()
