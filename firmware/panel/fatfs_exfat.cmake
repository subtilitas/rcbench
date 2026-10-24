# exFAT (extended File Allocation Table) on the panel's SD (Secure Digital)
# card.  Included by CMakeLists.txt before project.cmake.
#
# ESP-IDF v5.4 and v5.5 build FatFs with FF_FS_EXFAT 0, set in
# components/fatfs/src/ffconf.h line 282 by a plain #define.  No Kconfig
# option (CONFIG_FATFS_...) reaches it in either release, and a -D on the
# compiler line cannot either: the header's #define comes after it.  A card
# formatted exFAT, which every SDXC (SD eXtended Capacity, 64GB and up)
# card is out of the box, then fails to mount with FatFs 13
# (FR_NO_FILESYSTEM).
#
# So the build takes a copy of the fatfs component of the ESP-IDF it runs
# on, under the build directory, with that one line set to 1, and lists it
# in EXTRA_COMPONENT_DIRS.  A component found there takes precedence over
# ESP-IDF's own of the same name.  The copy is made when CMake configures:
# a fresh build directory has it from the first build, and an older build
# directory configures again because this file is new to it.  Everything
# else in the component, the VFS (virtual file system) glue and the SD
# diskio included, is the running ESP-IDF's own, so the copy follows the
# release.  The configure stops when ffconf.h no longer holds the line in
# the form below, rather than build without exFAT.
#
# FF_USE_LFN stays as sdkconfig.defaults sets it (CONFIG_FATFS_LFN_HEAP,
# 255 characters): exFAT needs long names on.  FF_LBA64 stays 0: an SD card
# of up to 2 TB, partitioned MBR (master boot record) as the SD
# Association's formatter does it, needs no 64-bit sector numbers.

set(_rcbench_idf_fatfs "$ENV{IDF_PATH}/components/fatfs")
set(_rcbench_fatfs_root "${CMAKE_BINARY_DIR}/fatfs_exfat")
set(_rcbench_fatfs "${_rcbench_fatfs_root}/fatfs")

if(NOT EXISTS "${_rcbench_idf_fatfs}/src/ffconf.h")
    message(FATAL_ERROR "exFAT: no ${_rcbench_idf_fatfs}/src/ffconf.h")
endif()

file(READ "${_rcbench_idf_fatfs}/src/ffconf.h" _rcbench_ffconf)
string(REGEX MATCHALL "\n#define[ \t]+FF_FS_EXFAT[ \t]+0[ \t]*\n"
       _rcbench_exfat_lines "${_rcbench_ffconf}")
list(LENGTH _rcbench_exfat_lines _rcbench_exfat_n)
if(NOT _rcbench_exfat_n EQUAL 1)
    message(FATAL_ERROR
        "exFAT: ${_rcbench_idf_fatfs}/src/ffconf.h has ${_rcbench_exfat_n} "
        "lines '#define FF_FS_EXFAT 0', 1 expected; see fatfs_exfat.cmake")
endif()
string(REGEX REPLACE "\n#define[ \t]+FF_FS_EXFAT[ \t]+0[ \t]*\n"
       "\n#define FF_FS_EXFAT\t\t1\t/* rcbench: fatfs_exfat.cmake */\n"
       _rcbench_ffconf "${_rcbench_ffconf}")

# The component without its tests, and without ffconf.h: that one is
# written below, and only when it differs, so a configure that changes
# nothing recompiles nothing.
file(COPY "${_rcbench_idf_fatfs}" DESTINATION "${_rcbench_fatfs_root}"
     PATTERN "host_test" EXCLUDE
     PATTERN "test_apps" EXCLUDE
     PATTERN "test_fatfsgen" EXCLUDE
     PATTERN "ffconf.h" EXCLUDE)
set(_rcbench_ffconf_out "${_rcbench_fatfs}/src/ffconf.h")
set(_rcbench_ffconf_old "")
if(EXISTS "${_rcbench_ffconf_out}")
    file(READ "${_rcbench_ffconf_out}" _rcbench_ffconf_old)
endif()
if(NOT _rcbench_ffconf_old STREQUAL _rcbench_ffconf)
    file(WRITE "${_rcbench_ffconf_out}" "${_rcbench_ffconf}")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "${_rcbench_fatfs}")
# An ESP-IDF update that changes the header configures again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${_rcbench_idf_fatfs}/src/ffconf.h")
