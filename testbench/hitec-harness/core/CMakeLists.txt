# The Hitec bench harness's pure-C core: the command lines and the reports.
# The RP2350 build in the directory above compiles hitec_harness.c itself;
# this file is the host suite's (test/host), which builds it with plain
# gcc against rcbench_hitec.
#
# SPDX-License-Identifier: MIT
add_library(rcbench_hitec_harness STATIC hitec_harness.c)
target_include_directories(rcbench_hitec_harness PUBLIC include)
target_link_libraries(rcbench_hitec_harness PUBLIC rcbench_hitec)
