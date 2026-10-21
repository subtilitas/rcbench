# The interface's words: one table, include/ui_text.def, with every text's
# English and German in one entry.  Header only: ui, settings, servo and
# webaccess each read the columns they need from it (docs/Language.md).
if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include)
else()
    add_library(rcbench_text INTERFACE)
    target_include_directories(rcbench_text INTERFACE include)
endif()
