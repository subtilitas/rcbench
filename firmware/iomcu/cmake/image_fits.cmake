# Fail the build when the image would not fit the module actually fitted.
#
# The linker checks against PICO_FLASH_SIZE_BYTES, which is what the board
# file says: sixteen megabytes for the pimoroni_pico_plus2_rp2350 this build
# defaults to. The bring-up module is a Waveshare RP2350-CAN with four, so the
# linker would accept an image that does not fit it -- and the first sign
# would be a board that does not boot.
#
# SPDX-License-Identifier: MIT

# Said before either is used, so a script invoked without them fails saying
# which one is missing rather than comparing against an empty string.
if(NOT DEFINED IMAGE OR IMAGE STREQUAL "")
    message(FATAL_ERROR "image_fits.cmake needs -DIMAGE=<file>")
endif()
if(NOT DEFINED LIMIT OR LIMIT STREQUAL "")
    message(FATAL_ERROR "image_fits.cmake needs -DLIMIT=<bytes>")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "image ${IMAGE} is missing")
endif()

file(SIZE "${IMAGE}" size)
if(size GREATER LIMIT)
    message(FATAL_ERROR
        "the image is ${size} bytes and the smallest module this build runs "
        "on holds ${LIMIT} bytes before the output store's sector. Either "
        "the artwork or the code has outgrown it.")
endif()

math(EXPR pct "${size} * 100 / ${LIMIT}")
message(STATUS "rcbench: image ${size} bytes, ${pct}% of the ${LIMIT} bytes "
               "a four-megabyte module leaves below the store")
