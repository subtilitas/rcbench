# The touch injector's pure-C core: the GT911 register file, the wiring
# check and the command and gesture engine.  The RP2350 build in the
# directory above compiles these sources itself; this file is the host
# suite's (test/host), which builds them with plain gcc.
#
# SPDX-License-Identifier: MIT
set(INJ_CORE_SRCS gt911_emu.c inj_line_id.c injector.c)

add_library(rcbench_touch_injector STATIC ${INJ_CORE_SRCS})
target_include_directories(rcbench_touch_injector PUBLIC include)
