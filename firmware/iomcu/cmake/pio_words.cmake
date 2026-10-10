# Fail the build when pioasm's words for a PIO program differ from the ones
# the host suite runs.
#
# test/host/test_tone_pio.c assembles
# protocols/phase_tap/rp2350/tone_cap.pio with a small
# assembler and runs the words in a model of the state machine.  This holds
# pioasm's output for the same file to the list the host test is held to, so
# the model runs what the chip runs.
#
# SPDX-License-Identifier: MIT

foreach(var HEADER WORDS NAME)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "pio_words.cmake needs -D${var}=<value>")
    endif()
endforeach()

file(READ "${HEADER}" header)
string(REGEX MATCH "${NAME}_program_instructions\\[\\] = \\{[^}]*\\}" block
       "${header}")
if(block STREQUAL "")
    message(FATAL_ERROR "no ${NAME}_program_instructions in ${HEADER}")
endif()
string(REGEX MATCHALL "0x[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F],"
       built "${block}")
set(got "")
foreach(w IN LISTS built)
    string(REGEX REPLACE "^0x([0-9a-fA-F]+),$" "\\1" w "${w}")
    string(TOLOWER "${w}" w)
    list(APPEND got "${w}")
endforeach()

file(STRINGS "${WORDS}" lines)
set(want "")
foreach(l IN LISTS lines)
    string(STRIP "${l}" l)
    if(NOT l STREQUAL "")
        string(TOLOWER "${l}" l)
        list(APPEND want "${l}")
    endif()
endforeach()

if(NOT got STREQUAL want)
    message(FATAL_ERROR
        "pioasm assembled ${NAME} to\n  ${got}\nand ${WORDS} holds\n  ${want}\n"
        "The host test runs the second. Update the file from the first, and "
        "run test_tone_pio.")
endif()
list(LENGTH got count)
message(STATUS "rcbench: ${NAME} is ${count} words, as the host test runs it")
