# Fail the build when the image would not fit the module actually fitted.
#
# The linker checks against PICO_FLASH_SIZE_BYTES, which is what the board
# file says: sixteen megabytes for the pimoroni_pico_plus2_rp2350 this build
# defaults to. The bring-up module is a Waveshare RP2350-CAN with four, so the
# linker would accept an image that does not fit it -- and the first sign
# would be a board that does not boot.
#
# SPDX-License-Identifier: MIT

if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image ${IMAGE} is missing")
endif()

file(SIZE "${IMAGE}" size)
if(size GREATER LIMIT)
    message(FATAL_ERROR
        "the image is ${size} bytes and the smallest module this build runs "
        "on holds ${LIMIT} before the output store's sector. Either the "
        "artwork or the code has outgrown it.")
endif()

math(EXPR pct "${size} * 100 / ${LIMIT}")
message(STATUS "rcbench: image ${size} bytes, ${pct}% of the ${LIMIT} a "
               "four-megabyte module leaves below the store")
