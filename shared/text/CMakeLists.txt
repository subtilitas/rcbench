# The interface's words: one table, include/ui_text.def, with every text's
# English and German in one entry.  Header only: ui, settings, servo and
# webaccess each read the columns they need from it (docs/Language.md).
#
# RCBENCH_TOUCH_INJECTOR adds INJECT's mode name and bubble texts to the
# table (the panel's default; idf.py -D RCBENCH_TOUCH_INJECTOR=OFF drops
# them).  The define is on this component's interface, as the include path
# is, so every file that reads the table sees the same set of entries.  The
# host suite tests INJECT's bubble and builds with them.
if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include)
    if(RCBENCH_TOUCH_INJECTOR)
        target_compile_definitions(${COMPONENT_LIB} INTERFACE RCBENCH_TOUCH_INJECTOR=1)
    endif()
else()
    add_library(rcbench_text INTERFACE)
    target_include_directories(rcbench_text INTERFACE include)
    if(NOT DEFINED RCBENCH_TOUCH_INJECTOR OR RCBENCH_TOUCH_INJECTOR)
        target_compile_definitions(rcbench_text INTERFACE RCBENCH_TOUCH_INJECTOR=1)
    endif()
endif()
